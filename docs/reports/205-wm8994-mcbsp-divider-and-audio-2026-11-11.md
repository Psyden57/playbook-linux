# 205 — WM8994 audio: the codec path now works; the McBSP divider is stale (session 41)

Date: 2026-11-11 (session 41, Linux-era 35).
Bench: unit on our Linux image `0xF97CD9C7` (region 0); region 1 flashed three
times this session (`rootfs-region1-audio-oss.ext4`, current sha256
`3124c322…`).  Only region 1 changed; no RPMB, NVRAM or MCT write.

## 0. Headline

**The codec side of playback is now correct and measured.**  Under a running
48 kHz stream the codec reports `AIF1CLK` sourced from a **locked** FLL1, a
16-bit word length, and the analogue output stages enabled — none of which was
true before this session.

**The remaining fault is on the CPU side, not the codec.**  The McBSP is
transmitting, but `SRGR1` still holds the **8 kHz** bit-clock divider
(`CLKGDV = 47`, /48) while the PCM asked for 48 kHz, so the stream runs 12×
slow — subsonic, and therefore inaudible.

Three separate defects were found and fixed this session; one remains.

## 1. Defect 1 — `pbplay` overran its own ring buffer on the first write

`rootfs/pbplay.c` asked for `SETFRAGMENT 0x00040008` (4 fragments × 2^8 bytes =
the OSS default, 256-byte periods → **64 frames** for S16_LE stereo) and then
wrote `CHUNK_FRAMES = 1024` frames per `write()`.  Every stream therefore
overran on its first write and landed in `XRUN` with `hw_ptr` pinned at one
period, so **nothing downstream was ever exercised**.

Fixed: read the granted period back with `SNDCTL_DSP_GETBLKSIZE` and write
exactly that, and ask for a larger fragment (`0x0004000E` = 4 × 16 KiB →
4096 frames) because a 64-frame period at 48 kHz is 1.3 ms and the period
interrupt cannot keep up.

**This is why earlier sessions concluded "no audible output":** the test tool
never streamed at all.  (Qualified in §5o: brief transient output *was* heard
this session once the chain was powered — "no audible output" was never strictly
true once the codec was driven, and the transients are evidence in their own
right.)

## 2. Defect 2 — the word length was never programmed (24-bit reset default)

`R0x300` (`AIF1 Control 1`) reset value is **`0x4050`**, i.e. `AIF1_WL = 0b10`
= **24-bit**.  `wm8994_aif1_hw_params()` computes `aif1 = 0` for 16-bit and
calls `update_bits(WM8994_AIF1_WL_MASK, 0)` — which is a *no-op write of the
existing value* as far as the frozen codec is concerned, so the part stayed at
24-bit while `pbplay` sent 16-bit samples.

Measured before the fix: `R0x300 = 0x6050` on every boot.
After: `R0x300 = 0x6010` (16-bit, I2S).

The machine driver now programs the word length itself, while thawed.

## 3. Defect 3 — the codec is frozen at `hw_params` time (the known AIF1CLK rule)

Confirmed again and now worked around in the driver.  `playbook_wm8994_hw_params()`
clears `R0x200` (the one write a frozen WM8994 still accepts) before touching
anything else, and pre-arms the enables.

**Proof the fix works:** with the old driver, `R0x224` stayed `0x0c80`
(`REFCLK_SRC = MCLK1`, dead).  With the thaw, it reads **`0x0c83`**
(`REFCLK_SRC = 3` = **BCLK**) and `R0x732 = 0x6020` — **FLL1 locked**.  That
was not achievable in any earlier session.

Note the ordering that makes this necessary (report 203 §6, unchanged):
`.prepare` → DAPM stream event (sets `AIF1CLK_ENA = 1`) → `.trigger(START)`
(starts BCLK).  Because the FLL is referenced from BCLK, the codec freezes at
the DAPM event, before any live reference exists.

## 4. Defect 4 (open) — the McBSP bit-clock divider is never updated for 48 kHz

The decisive measurement.  `omap_mcbsp_dump_reg()` was changed from `dev_dbg`
to `dev_info` for this board, because the L4_ABE target aborts on a user-mode
read (`devmem 0x40124000` → `Bus error`, trap 158), so these registers cannot
be read from userspace at all.

Dumped at the end of `omap_mcbsp_start()` during a **48 000 Hz** stream, and
decoded with the authoritative bit positions in
`sound/soc/ti/omap-mcbsp-priv.h` (not by hand):

```
SPCR2: 0x02f5   XRST=1  XRDY=0  XEMPTY=1  XSYNCERR=0  GRST=1  FRST=1
SPCR1: 0x0030   RRST=0  RRDY=0  RFULL=0  RSYNCERR=0
XCR1:  0x0040   XCR2: 0x0041
SRGR1: 0x0f2f   CLKGDV=47 (/48)   FWID=15 (16 bit)
SRGR2: 0x101f   FPER=31 (frame = 32 BCLK)  CLKSM=0  FSGM=0  GSYNC=0
PCR0:  0x0f0f   CLKXM=1 FSXM=1 (outputs) + all four polarity bits (I2S NB_NF)
DXR1:  0x0000   DXR2: 0x0000
```

**The transmitter is running** (`XRST=1`, `GRST=1`, `FRST=1`) and needs no
reset fix.  Two facts do stand out and they are consistent with each other:

1. **`CLKGDV = 47` while `FPER = 31`.**  The frame *period* was updated for
   48 kHz but the bit-clock *divider* still holds the 8 kHz value.  With
   `fclk = 12.288 MHz`, `BCLK = 12.288 MHz / 48 = 256 kHz`, and 32 BCLK per
   frame gives **8 kHz**, not 48 kHz.
2. **`CLKSM = 0` and `FSGM = 0`.**  The sample-rate generator is not clocked
   from the internal fclk, and is not driving FS.

Fact 1 explains a measurement taken earlier in the session: the PCM reported
`rate: 48000` while `hw_ptr` advanced at **~4025 frames/s** (measured three
times; the 8 kHz path concurrently measured 8038/8032, i.e. correct).  A 12×
slow stream makes a 440 Hz tone come out at ~37 Hz — subsonic, which sounds
exactly like silence.  This is **not** a phantom: it is reproducible and it
matches `CLKGDV = 47` exactly (`48000 / 12 = 4000`).

The register-level contradiction is internally consistent and therefore
trustworthy: `FPER` proves 48 kHz was requested, `CLKGDV` proves 8 kHz was
programmed.  The two disagree.

**Mechanism.**  `omap_mcbsp_dai_set_clkdiv()` only writes the driver's shadow
(`mcbsp->clk_div`, `regs->srgr1`); hardware is programmed later by
`omap_mcbsp_config()` from `omap_mcbsp_dai_hw_params()`.  But that function
**returns early when `mcbsp->configured` is already set** ("McBSP already
configured by another stream", `omap-mcbsp.c`), so only the *first* stream
after the port is opened programs SRGR1.  OSS opens the device at its default
8000 Hz format before the caller's `SETFMT`/`CHANNELS`/`SPEED` are applied, so
the first — and only applied — SRGR1 is the 8 kHz one.

**Fixed and verified.**  The early return now also compares the rate and sample
width (`mcbsp->active_rate`/`active_width`, added to `struct omap_mcbsp`), so a
format change re-programs the port.  Measured after the fix, same stream:

```
SRGR1: 0x0f07   CLKGDV=7 (/8)
hw_ptr advance: 96355 frames / 2 s = 48177 frames/s
```

i.e. **48 177 frames/s instead of 4 025** — the stream now runs at the rate the
PCM advertises.

`CLKSM = 0` is the same class: `omap_mcbsp_dai_set_dai_sysclk()` sets it per
`clk_id`, but early-returns `-EBUSY` when the port is `active` with a
different frequency.

## 4b. Correction to report 204: MCLK1 **does** clock under our image

Report 204 concluded "MCLK1 does not clock under our Linux image, at any
AUXCLK1 divider, with the codec reset or biased".  That conclusion is **wrong**,
and the reason is now clear: every one of those tests wrote the FLL registers
while the codec was **frozen** (`AIF1CLK_ENA = 1` with a stopped source), so
the FLL programme was silently dropped and "no lock" was the expected outcome
of *not having programmed the FLL at all*.

Re-run with the thaw in place (write `R0x200 = 0x0000`, wait, then program),
using the reference's exact FLL1 programme and **no** other change:

```
R0x220 = 0x0005   (ENA|FRAC)
R0x224 = 0x0000   REFCLK_SRC = MCLK1   REFCLK_DIV = /1
R0x732 = 0x6020   FLL1_LOCK_STS = 1     <- LOCKED
AUXCLK1 (0x4A30A314) = 0x00020100
```

**FLL1 locks from MCLK1 exactly as it does on the reference.**  So:

* MCLK1 is real on this board under our image.
* AUXCLK1 `0x00020100` (÷3 = 12.8 MHz on `fref_clk2_out`, CORE pad `0x19C`)
  is correct and sufficient.
* The pad question is closed from the reference (see §8).
* **There is no missing clock.**  The earlier "one clock below the registers"
  framing in the handoff and reports 203/204 should be retired.

This makes the reference's design (`MCLK1 -> FLL1 -> AIF1CLK`, codec as AIF1
master) available to us, which is what §6 recommends.

## 4c. The codec can be put in the reference's exact configuration

Verified by hand, register by register, with a stream running:

| register | reference (report 204) | ours, set by hand |
|---|---|---|
| `R0x300` AIF1 Ctrl 1 | `0x4018` | `0x4018` (16-bit, DSP mode) |
| `R0x302` AIF1 Mstr/Slv | `0x4000` (master) | `0x4000` |
| `R0x732` FLL1 lock | `0x6020` | `0x6020` |
| `R0x224` FLL1 Ctrl 5 | `0x0c80` (MCLK1) | `0x0000` (MCLK1, ÷1) |

**Still no audible output.**  Note one flaw in how that test was performed: the
codec was made AIF1 **master** while the board DTS still configures the McBSP
as the clock **master** (`CLKXM=1`, `FSXM=1`), so both were driving the clock
net.  That is not a valid reference-exact comparison and must be redone with
the McBSP switched to clock-consumer (`SND_SOC_DAIFMT_BC_FC`) together with
`set_sysclk` to the codec's BCLK.

## 5. The reference's live state, captured (new this session)

The 1917 IFS was reflashed (region 0 only) and TabletOS booted with `io-audio`
running, so the codec could be read in its **working** configuration.  Root
ssh over the door (`tools/pbssh.sh`), `tools/pbi2c dump16` for the codec and
`tools/memdump3` for the SoC pads.

* **The full codec register file (1905 registers) is saved** at
  `docs/captures/wm8994-reference-live-2026-11-11.txt`.  This is the first
  complete capture of a *working* WM8994 on this board and it is the baseline
  every future comparison should use.
* With audio running the reference has `R0x200 = 0x0011` (AIF1CLK **enabled**,
  source FLL1) and `R0x732 = 0x2020`.  Report 204's idle capture
  (`0x0010` / `0x6020`) was taken with no stream.
* `R0x300 = 0xC018` — 16-bit, DSP mode (`AIF1_FMT = 0b11`), **not** the `0x4018`
  report 204 recorded.  Our driver programs I2S (`0x4010`/`0x4018`); the
  reference runs **DSP mode**.
* `PM2 = 0x6310` (ours `0x6000`): the reference additionally sets
  `MIXINR_ENA`, `TSHUT_ENA` and `TSHUT_OPDIS`.
* Rails are **identical** on both OSes, read with
  `pmic-twl6030-ctl get_state/get_vout`: V1V8 1823 mV, V2V1 2127 mV,
  VANA 2100 mV, VDAC 1800 mV, VAUX1/2/3 2800 mV, all ON.  **The supply theory
  is dead** — a working system runs on exactly these rails.

## 5a. The McBSP pads were checked against the reference and are correct

> **Correction:** an earlier draft of this report claimed the pad values were
> wrong.  A full live comparison against QNX (§5h) shows they are **right**, and
> the `dx`-in-safe_mode difference is a consequence of the reference being the
> clock master, not a defect.  The analysis is kept here because the reasoning
> is instructive.

Live reads, QNX vs ours:

| register | reference (QNX) | ours (Linux) |
|---|---|---|
| `0x4A1000F4` | `0x0100000F` | `0x0000010F` |
| `0x4A1000F8` | `0x000F1108` | `0x00000100` |
| `0x4A1000FC` | `0x01000100` | `0x010F0000` |
| `0x4A100100` | `0x00001108` | `0x010F010F` |

Decoded against the TRM register map (confirmed by the reference's own
`hwio-config-winchester.so` pad table):

* **`0x4A1000F8` low = `abe_mcbsp2_dr`, high = `abe_mcbsp2_dx`**
  (TRM Table 18-420).  `0x4A1000F4` high is `abe_mcbsp2_clkx`; `0x4A1000FC`
  low is `abe_mcbsp2_fsx`.
* The reference has `dr` = `0x1108` (mux 0, input enable, **pull-up**) and leaves
  `dx` at `0x000F` = **mux 7, safe_mode** — because on the reference the codec
  is the AIF1 **master**, so nothing needs to drive `dx` into it.
* Ours drives `dx` (mux 0) because our McBSP is the master, which is correct for
  our design.  Our `dr` lacks the pull-up, which was tried live and made no
  difference.

**Conclusion: pin muxing is not the blocker.**  Both were verified by setting
our pads to the reference's exact values and re-testing — no change.

## 5b. Audio is audible: the noise floor is up (new this session)

**The DAC now genuinely converts.**  With the registers below set, the codec's
own output noise floor is audible on the headphones — measured by the operator
on the bench, repeatedly, and absent on every Linux boot before this session.
That is the first time our image has produced *any* output from the codec.

The five blocks that must be open, all found by diffing the live reference
capture (`docs/captures/wm8994-reference-live-2026-11-11.txt`, 1905 registers)
against a live dump of our own:

| register | value needed | what it gates |
|---|---|---|
| `R0x03` PM3 | `0x00F0` | **`MIXOUTL/R_ENA` + `MIXOUTLVOL/RVOL_ENA`** — the output mixer |
| `R0x4C` Charge pump 1 | bit 15 `CP_ENA` = 1 | the headphone amplifier's supply |
| `R0x2D`/`R0x2E` Output mixer 1/2 | `0x0001` | `DAC1L/R_TO_MIXOUTL/R` un-mute |
| `R0x420`/`R0x422` AIF1 DAC filters | bit 9 = **0** | `AIF1DAC1/2_MUTE` soft-mute |
| `R0x60` Analogue HP | `0x00EE` | `HPOUT1L/R_OUTP` + `RMV_SHORT` |

**Corrections to what earlier sessions (and this one) assumed:**

* **`MIXOUTL_ENA`/`MIXOUTR_ENA` are in PM3 (`R0x03`), not PM4 (`R0x04`).**
  PM4 is the *digital microphone* control block.  This session wrote `0x0330` to
  PM4 for a long time, which sets DMIC bits and touches nothing in the output
  path.  DAPM's own debug line said `R3(0x3) mask 0x20` and was correct.
* **`CP_ENA` (charge pump) was never enabled.**  The reference has
  `R0x4C = 0x9F25`; ours was `0x1F25`.  Without the charge pump the headphone
  amplifier can energise (the audible "pop") but cannot drive — which is exactly
  the symptom every earlier test reported.
* **`AIF1DAC1_MUTE` (R0x420 bit 9) is set by `wm8994_aif_mute()` at stream
  prepare**, which runs *before* the DAPM event that freezes the codec, so the
  un-mute is dropped.  The reference has it clear.

## 5c. The reference's audio format is DSP mode with the codec as AIF1 master

Live reference: `R0x300 = 0xC018` (`AIF1_FMT = 0b01` = **I2S**, 16-bit — note the
format field is bits `[4:3]`, so `0x18` is I2S, not DSP) and `R0x302 = 0x4000`
(**codec is master**).  Our design is the inverse: I2S, codec **slave**, McBSP
master.

> **Correction:** this report first read `0x18` as DSP mode.  `AIF1_FMT` is
> `[4:3]`: `0x0` = right-justified, `0x1` = **I2S**, `0x2` = DSP.  `0xC018` is
> therefore **16-bit I2S**, the same format we use; only `AIF1ADCL_SRC` (bit 15)
> and the master/slave bit differ.

Setting the codec to the reference's master/DSP configuration on our image
**silenced the noise floor** at the time, most plausibly because the McBSP was
then still configured as I2S clock master, so the two disagreed about the frame
(that specific test is not reproducible on demand and is not relied on
anywhere).  Matching the reference requires changing *both* sides at once:

1. board DTS: DAI link format and
2. the CPU DAI to clock-consumer (`SND_SOC_DAIFMT_BC_FC`), with
   `set_sysclk` taking the codec's BCLK.

That remains a valid experiment, but it is **not** required to explain the
silence: with the codec as slave in I2S (our design) the DAC still converts and
the failure is upstream, at the McBSP (§5i).

## 5g. The reference's design, established by capturing it live

Two full 1905-register dumps were taken from the working reference this session,
**both while audio was playing**:

* `docs/captures/wm8994-reference-live-2026-11-11.txt`
* `docs/captures/wm8994-reference-playing-2026-11-11.txt`

> **Correction:** an earlier draft of this section called the first file an
> "idle" capture and claimed the two differ in only two registers *because the
> reference keeps everything powered*.  Both are **playback** captures; they
> differ in only `R0x402`/`R0x403` (`0x007D → 0x0073`) simply because they were
> taken moments apart with the volume nudged.  The **idle** state was measured
> separately with targeted `pbi2c rd16` reads, not with a full dump.

Key values, identical in both dumps:

```
  R0x220 = 0x0005   FLL1 ENA|FRAC
  R0x221 = 0x0700   OUTDIV /8, FRATIO /1
  R0x222 = 0xAE14   K
  R0x223 = 0x00E0   N = 7
  R0x224 = 0x0C80   REFCLK_SRC = MCLK1, DIV /1
  R0x732 = 0x2020   FLL1_LOCK_STS (bit 5) = 1
  R0x003 = 0x00F0   output mixers
  R0x005 = 0x0303   DAC1 enables
  R0x04C = 0x9F25   charge pump
  R0x420 = 0x0030   AIF1 DAC1 un-muted
  R0x300 = 0xC018   16-bit I2S (AIF1_FMT is bits [4:3]; 0x1 = I2S)
  R0x302 = 0x4000   codec is AIF1 MASTER
```

**The decisive fact is the idle reading**, taken with single-register reads while
no stream was open:

```
  R0x200 = 0x0010   AIF1CLK_ENA = 0   (clock gated OFF)
  R0x732 = 0x6020   FLL1 still LOCKED
  R0x220 = 0x0005   FLL still enabled
  R0x003 = 0x0000   output mixers OFF
  R0x005 = 0x0000   DACs OFF
  R0x04C = 0x1F25   charge pump OFF (CP_ENA clear)
```

So QNX **runs the FLL continuously from MCLK1 and gates only AIF1CLK**,
powering the analogue chain down when idle.  It is *never* in the "ENA = 1 with
a stopped source" state, so its codec never freezes.

Our driver instead configures the FLL per-stream inside `hw_params`, so it
passes through exactly the window that freezes the codec.

## 5h. MCLK1 is programmed identically on both OSes but the FLL does not lock for us

Every SoC-side value was compared live against QNX in the same session:

| item | QNX | Linux | verdict |
|---|---|---|---|
| SCRM `AUXCLK1` (`0x4A30A314`) | `0x00020100` | `0x00020100` | identical |
| CORE pad `0x19C` | `0x41000000` | `0x41180000` | low half `0x0000` both |
| `CONTROL_CORE_PADCONF_MODE` (`0x4A1005A4`) | `0x00000000` | `0x00000000` | identical |
| `CONTROL_SMART3IO_PADCONF_0` (`0x4A1005B8`) | `0xBD7F5501` | `0xBD7F5501` | identical |
| `CONTROL_SMART3IO_PADCONF_1` (`0x4A1005BC`) | `0x55703057` | `0x55703057` | identical |
| `CONTROL_SMART3IO_PADCONF_2` (`0x4A1005C0`) | `0x67000510` | `0x67000510` | identical |
| `DPLL_PER` M3x2 (`0x4A008140`) | `0x00000007` | `0x00000007` | identical |
| PMIC rails (V1V8/V2V1/VANA/VDAC/VAUX1-3) | — | — | identical, all ON |

And yet: with `R0x220 = 0x0005`, `R0x224 = 0x0000`, `R0x221 = 0x0700`,
`R0x223 = 0x00E0`, `R0x222 = 0xAE14`, PM2 supplies `0x6310`, the codec thawed,
and a 5 s settle, **our `R0x732` stays `0x6000`** while the reference reads
`0x6020` on the same boot-independent register set.

Two facts bracket the problem but do not yet explain it:

* The FLL locked from MCLK1 **once** earlier this session (verified:
  `R0x732 = 0x6020`, `R0x224 = 0x0000`) — so it is *possible* on this hardware
  under our image, and the capability is not missing.
* `clk_summary` reports `auxclk1_ck = 38 400 000` even after SCRM is written to
  `0x00020100` and verified stable by three reads 2 s apart.  The TI divider is
  zero-based (`_get_div()` returns `val + 1`), so field `2` **is** ÷3, and the
  SCRM value is correct — the framework's cached rate is simply stale.  That
  means the framework cannot be used to confirm the pin rate, and a meter on
  MCLK1 (or a scope) is the only way to know what the codec actually receives.

**Conclusion:** the remaining difference is not any register we can read.  It is
either the actual clock at the pad (needs a meter/scope) or a timing property of
how AUXCLK1 is enabled relative to the FLL.  This is the same wall report 204
hit, now narrowed to a single measurement rather than a family of guesses.

Two things are true at once and they are the handover:

**Working (measured):**
* The codec's output chain is fully mapped and was driven by hand to the point
  where the DAC's own noise floor is audible — the first time our image has
  produced any codec output.
* All five previously-missing blocks are identified with authoritative bit
  positions: PM3 `0x00F0` (output mixers), `CP_ENA` (charge pump),
  `R0x2D`/`R0x2E` (DAC→mixer un-mute), `R0x420` bit 9 (AIF1 DAC1 un-mute),
  `R0x60 = 0x00EE` (output stage).
* The 48 kHz rate bug is fixed (`SRGR1` `CLKGDV` 47→7, 4 025→48 177 frames/s).
* `pbplay` and the 24-bit word length are fixed.
* With AIF1CLK sourced from an FLL locked to **MCLK1**, the codec stops
  freezing: PM3 and `CP_ENA` now **hold through a live stream**, which they
  never did with a BCLK-referenced FLL.  The driver was changed to match the
  reference on this point.

**Unresolved, and stated plainly:**
* **The FLL will not lock from MCLK1 on every boot.**  It locked earlier in
  this session with `R0x224 = 0x0000` and `R0x732 = 0x6020`, and after a later
  reboot the identical register file (`0x221 = 0x0700`, `0x222 = 0xAE14`,
  `0x223 = 0x00E0`, `0x224 = 0x0C80`) leaves `0x732 = 0x6000` with PM2 supplies
  set and `auxclk1_ck` enabled at 12.8 MHz.  So the *register* state is proven
  correct but the *clock at the pin* is not reliably present.
* That is the same class as the session-40 note that `AUXCLK1` can boot as
  `0x00000100` (÷1 = 38.4 MHz) instead of `0x00020100`; here the SCRM value is
  correct (`0x00020100`) yet the FLL still does not lock, so the remaining
  variable is the pad's **output driver** (`CONTROL_CORE_PADCONF_MODE[30]
  VDDS_DV_BANK1` and the SMART3 drive registers), which is the one thing not yet
  compared against QNX.
* Until that is settled the driver must not depend on MCLK1 alone.  The
  practical next step is to test the SMART3 pad-drive registers against the
  reference (live QNX read), then either make MCLK1 reliable or revert the FLL
  to BCLK with the freeze-thaw applied at the right point.

## 5i. The codec reaches the reference's exact state; the audio still does not appear

With a cold battery-pulled boot, `AUXCLK1` forced to 12.8 MHz by DT, and every
codec register set by hand to the reference's *playing* values, all of the
following hold **simultaneously and stably during a live 48 kHz stream**:

```
PM1 0x0313  PM2 0x6310  PM3 0x00F0  PM5 0x0303
R0x04C 0x9F25   charge pump on
R0x02D 0x0001   R0x02E 0x0001   DAC -> MIXOUT un-muted
R0x420 0x0030   AIF1 DAC1 un-muted
R0x601 0x0001   R0x602 0x0001   AIF1 -> DAC1 routed
R0x1C  0x00F8   R0x1D 0x00F8    headphone 0 dB
R0x200 0x0011   AIF1CLK from FLL1
R0x732 0x6020   FLL1 locked
R0x60  0x00EE   output stage connected
dmesg:  "repair: done, FLL1 locked"
```

**Result: the DAC's own noise floor is audible, and no programme audio.**

Two inferences made earlier in the session were **wrong** and are retracted:

1. **"The noise floor proves the DAC is converting AIF1 data."**  Disabling the
   AIF1->DAC1 routing (`R0x601/0x602 = 0`) while the floor was audible made **no
   difference**.  So the floor is the analogue output stage idling, not AIF1
   data being converted.  It is still a real milestone — it is the first audio
   this image has produced — but it says nothing about the data path.
2. **"`DXR1`/`DXR2` = 0 means the McBSP is not transmitting."**  The dump comes
   from `omap_mcbsp_dump_reg()` at the end of `omap_mcbsp_start()`, i.e. the
   instant the port is enabled and *before* the sDMA has filled anything.  Zero
   there is expected.  It is **not** evidence of a fault.

## 5j. Where the fault now stands

* SoC side: identical to QNX on every measured register (§5h), including
  AUXCLK1, padconf, SMART3 drive, DPLL_PER and all PMIC rails.
* Codec side: identical to the reference's live playing register file (§5i).
* Stream: `RUNNING`, `hw_ptr` at 48 185 frames/s, sDMA period interrupts at
  exactly 12/s.
* Output: audible noise floor, no programme audio.

The remaining gap is therefore between **the sample data and the codec's AIF1
input**, and it cannot be resolved by reading registers — every one of them is
already correct.  And it is **not** "no data ever moves": the operator heard a
brief descending sweep at every stream start (§5o), which a DAC fed silence
cannot produce.  The failure mode is a **stall after start**.

**Hardware probing is not available** (no scope, no boardview, no pad
knowledge — operator, session 41 end), so the routes are:

1. **A capture stream** — the sDMA then delivers what the McBSP actually
   transmitted into a `/dev/dsp` read; compare against the square wave `pbplay`
   wrote.  This distinguishes "our data" from the DC-servo/FLL-relock
   alternatives (§5o) and needs no hardware.
2. **A `/dev/mem` loopback** — `dx` and `dr` are the two halves of
   `0x4A1000F8`, so mux one as GPIO, drive it from userspace and read the
   other's `DATAIN`.
3. **The reference's own McBSP registers mid-stream** — we have its codec file
   (§5g) but not its `SPCR2`/`SRGR1`/`SRGR2`/`PCR0` during playback, which is
   the register set whose meaning differs between clock master and consumer.
   `memdump3` reads the `0x49024000` window on QNX; Linux userspace cannot
   (trap 158).
4. **Adopt the reference's clock roles** — codec as AIF1 master, McBSP as
   consumer, changed on both sides at once.  This removes the sample-rate
   generator from the path entirely.

Route 4 is the cheapest and most likely decisive: the reference is the clock
master, so its McBSP is a *consumer*.  Ours is the master.  That is the one
structural difference left, and it is a two-sided change (DAI link format plus
CPU DAI clock role), not a register poke.

## 5l. The McBSP transmit register stays empty in steady state (measured correctly)

An earlier version of this section misread the register.  Reading the **32-bit**
`DXR` (offset `0x8`, the only legal access per TRM 23.5.4.8) from a delayed work
item half a second into playback:

```
DXR:   0x00000000     <- transmit register EMPTY during playback
DRR:   0xc8318118     <- receive register has data (playback-only stream!)
SPCR2: 0x02f5         XRST=1 XRDY=0 XEMPTY=1 GRST=1 FRST=1
SRGR1: 0x0f07         CLKGDV=7 (/8), FWID=15 (16 bit)
SRGR2: 0x101f         FPER=31 (32 BCLK/frame), CLKSM=0, FSGM=0
PCR0:  0x0f0f         CLKXM=1 FSXM=1, I2S NB_NF polarity
XCR1:  0x0040         XWDLEN1=16 bit, XFRLEN1=0
XCR2:  0x8041         XPHASE=1, XWDLEN2=16 bit, XFRLEN2=0
```

with `omap-dma-engine` interrupts advancing **12/s** (exactly 48 000 / 4096) and
`hw_ptr` at 48 185 frames/s.

**`XRDY = 0` means the transmit buffer is full**, and `XEMPTY = 1` means the
shift register is empty — i.e. data sits in the buffer and is not shifting out.
Combined with `DXR` reading 0, the transmit path is **not draining** at that
moment.

**Read this as a stall, not as absence.**  Audio *was* heard — a brief
descending sweep at every stream start (§5o), and a DAC fed silence cannot
chirp.  So the sequence is: the sDMA fills the FIFO at start, the McBSP shifts a
burst out (the sweep), and then **both** stop — which is exactly what "buffer
full, shifter empty, input port idle" describes.  The wrong framing, used
earlier in this session, was "the McBSP never transmits"; the correct one is
"it transmits once and then stalls".

**The DMA address is not the problem.**  The McBSP node declares two reg
windows (`omap4-l4-abe.dtsi`):

```
reg = <0x0 0xff>,        /* "mpu"  -> 0x40124000  */
      <0x49024000 0xff>; /* "dma"  -> 0x49024000  */
reg-names = "mpu", "dma";
```

so `phys_dma_base = 0x49024000` and the sDMA targets `0x49024008`, while the
CPU reads `0x40124008` through the "mpu" window.  Those are the same peripheral
via two L3 views — the standard OMAP arrangement, and not a bug.

**What this now points at:** a **stall after start**.  The two candidates are a
**DMA channel/port configuration** problem (the sDMA stops being serviced once
its first period completes) and a **missing BCLK at the transmit shifter**
(`CLKSM = 0` with `XEMPTY = 1` makes this the stronger one).  `CLKSM = 1` was
tried and did **not** help; `FSGM = 0` is also worth noting, since the SRG is
supposed to generate FS in master mode.  A capture stream (§0c E2) separates
these without hardware: it shows whether the sDMA keeps delivering after the
first period.

## 5m. Driver bugs found and fixed while chasing this

Recording these because they are real defects, independent of the remaining one:

1. **The driver never re-enabled `AIF1CLK` after its own thaw.**  `hw_params`
   writes `R0x200 = 0` to unfreeze the codec, and with a BCLK-referenced FLL
   nothing turned it back on, so the DAC lost its clock and the audible noise
   floor disappeared.  Now re-enabled last, in the repair work.
2. **The repair's `update_bits()` calls were no-ops.**  The regmap cache still
   held the values the driver had *tried* to write while the codec was frozen,
   so `update_bits()` saw no change and skipped the bus write — which is why
   `PM3` read `0x0000` while the driver had "written" `0x00F0`.  Fixed with
   `regcache_mark_dirty()` + `regcache_sync()` at the top of the repair.
   **This is the same trap as report 200/DECISIONS §49, and it is worth
   generalising: after any period where writes were dropped, discard the
   cache before writing again.**
3. **`MIXOUTL/R_ENA` were written to PM4 instead of PM3.**  PM4 is the digital
   microphone block; the output-mixer enables are PM3 bits 7:4.  The PM3
   value is now `0x03F0`.
4. **`pbplay` wrote 1024-frame chunks into a 64-frame ring** (§1).
5. **The 48 kHz McBSP divider was never applied** (§4).

## 5o. Transient audio **was** produced — and the descending sweep is evidence

Correcting the summary used earlier in this session ("no audible output at
all").  Once the output chain was powered, the operator heard, repeatedly and
in different configurations:

| sound | when | what it shows |
|---|---|---|
| a **descending sweep** ("laser"), ~200 ms, quiet | at `pbplay` start, many times | a chirp needs **non-zero, time-varying samples**; a DAC fed silence cannot produce one |
| a **pop** | writing `R0x60` (`HPOUT1L/R_OUTP` + `RMV_SHORT`) | the analogue output stage energising; no data needed |
| a steady **noise floor** | charge pump + mixers on | the DAC is converting at idle — converter and supplies work |

**The sweep is the important one.**  It means real samples reached the DAC and
were clocked out at least briefly, which is *not* consistent with a reading of
"the transmit path never carried data".  It **is** consistent with what the
registers show, once the stall is taken as the failure mode rather than absence:

```
DXR   = 0x00000000   input port idle (nothing new from the sDMA)
SPCR2 = 0x02f5       XRDY = 0  transmit buffer FULL
                     XEMPTY = 1  shift register EMPTY
```

i.e. **a burst was delivered at start, shifted out (the sweep), and then both
the sDMA feed and the buffer→shifter copy stopped.**  So the target is a
**stall after start**, not a dead path — a materially narrower question than the
one in §5l.

**Two alternative explanations that cannot be excluded from the bench alone**,
because both also produce a sweep and neither requires our data:

1. the WM8994's **DC servo** — `wm8994_aif_mute()` and the DC-offset
   cancellation are scheduled at stream prepare, and slewing offset correction
   can be audible;
2. an **FLL re-lock** — if FLL1 relocks when the stream starts while the DACs
   are already running, the pitch sweeps in exactly this way.

Distinguishing them is software-possible: capture a stream (§0c E2) and compare
the samples the McBSP reports against the square wave `pbplay` wrote.  If the
sweep survives with the DC servo and FLL both held static, it is our data.

## 5p. Honest summary of the session's outcome

**Definitively fixed and verified** (measured, not inferred):

* **`AUXCLK1` booted at ÷1 = 38.4 MHz on every cold boot** — outside the WM8994
  FLL's 0.032–13.5 MHz reference range, so FLL1 could never lock.  Fixed
  properly in the board DTS with `assigned-clock-rates = <12800000>`; verified
  on a battery-pulled boot: AUXCLK1 = `0x00020100` and `auxclk1_ck` = 12 800 000
  from the first moment.  **This was a real, long-standing defect.**
* `pbplay` no longer overruns its ring — earlier sessions never streamed at all.
* AIF1 word length is programmed (16-bit) instead of the 24-bit reset default.
* The 48 kHz rate bug: `SRGR1` `CLKGDV` was stuck at the 8 kHz value because
  `omap_mcbsp_dai_hw_params()` early-returns once `configured` is set; now it
  compares rate/width and re-programs.  Verified **4 025 -> 48 177 frames/s**.
* The codec-freeze mechanism is understood; the driver thaws, locks the FLL
  first, and repairs the chain from a delayed work once BCLK is running.
* The complete output chain is mapped with authoritative bit positions, and the
  **DAC audibly converts** — a steady noise floor when idle and **brief
  transient output (a descending sweep) at every stream start** (§5o).  That
  transient is the first time this image has moved real samples through the
  codec, and it narrows the fault from "no data path" to "**stall after
  start**".

**Not achieved: sustained programme audio.**  The chain is right and a burst
gets out, then the sDMA feed and the buffer→shifter copy both stop (§5o).  The
next work is a software-only capture/loopback (§0c E1–E4), not register
investigation and not hardware probing.

**Two corrections to earlier documentation:**
* The pad and register questions in the session-41 handoff are **closed**: pad
  `0xB7` -> `0x4A10019C` low half is `fref_clk2_out`, driven by `AUXCLK1`, both
  confirmed from the reference binary and the TRM.
* The **supply theory is dead**: the reference runs on byte-identical rails.

## 5f. Method note

This is the sharpest measurement of the session and where the investigation now
stops without a scope.

With a 48 kHz stream playing and **every** codec gate verified open at once:

```
R0x601 = 0x0001   AIF1 -> DAC1L routing enabled
R0x602 = 0x0001   AIF1 -> DAC1R routing enabled
R0x300 = 0x4010   16-bit, I2S
R0x302 = 0x0000   codec slave
R0x732 = 0x6020   FLL1 locked (from BCLK)
PM3    = 0x00F0   output mixers on
R0x4C  = 0x9F25   charge pump on
R0x420 = 0x0030   AIF1 DAC1 un-muted
R0x60  = 0x00EE   output stage connected
```

…the McBSP transmit holding registers read **`DXR1 = 0x0000`, `DXR2 = 0x0000`**,
while at the same time:

* `omap-dma-engine` advances **1792 -> 1828** over 3 s = **12/s**, exactly
  48 000 / 4096-frame periods — the sDMA is transferring *something* at the
  right rate;
* `hw_ptr` advances at **48 185 frames/s**;
* `pbplay` is alive and writing.

So the DMA runs and the PCM believes it is consuming, but the McBSP's transmit
registers are empty.

**A hypothesis that was tested and failed:** enabling the AIF1→DAC1 routing
(`R0x601/0x602`) appeared to silence the noise floor, which suggested the DAC
was converting routed (silent) data.  **It does not reproduce** — toggling that
routing on and off while the floor was audible made **no difference** (operator,
twice).  So the routing is not what silences it and the floor cannot be used to
infer anything about AIF1 data.  Recorded because the opposite reading is
tempting.

**Constrain the next step to software.**  The operator has **no scope, no
boardview and no pad-level knowledge** (session 41 end), so none of the obvious
"probe `abe_mcbsp2_dx`" answers are available.  The software-only routes, in
order of cost:

1. **Make the McBSP a clock consumer** — adopt the reference's roles (codec as
   AIF1 master, `R0x302 = 0x4000`) on *both* sides at once: the DAI link format
   plus the CPU DAI taking the codec's BCLK.  This removes the sample-rate
   generator from the path, which is where `XRDY = 0` sits.
2. **Prove the transmit path without the codec** — `dx`/`dr` are the two halves
   of one register (`0x4A1000F8`), so drive one as a GPIO and read the other
   from `/dev/mem`, or use the McBSP's internal loopback with a capture stream so
   the sDMA delivers what was actually transmitted into a `/dev/dsp` read.
3. **Read the reference's McBSP during playback** — we have its codec file
   (§5g) but **not** its `SPCR2`/`SRGR1`/`SRGR2`/`PCR0` mid-stream, and that is
   exactly the register set whose meaning differs between master and consumer.
   `memdump3` can read the "dma" window (`0x49024000`) on QNX, which Linux
   userspace cannot (trap 158).
4. **Static RE of the 1917 sDMA/McBSP setup** — report 201 decompiled the codec
   driver, but the McBSP/sDMA side has not been decompiled; a threshold or port
   mismatch would explain `XRDY` stuck at 0 with the sDMA running.

## 5. Why OSS makes this ordering fragile

`dmesg` on a single `pbplay 5 440` shows **three** `rate 8000` `hw_params`
calls followed by **two** `rate 48000` ones, microseconds apart.  The OSS
emulation layer walks its format table before applying the caller's request,
so the machine driver's `hw_params` runs several times per open.  Any register
that is only latched on the first call (or that `set_*` refuses to rewrite
because the port is already active) keeps the probe's value.

## 6. What the reference does (unchanged from reports 201/204)

The 1917 driver never has this problem because MCLK1 is real there:
`deva-ctrl-omap4abe-wm8994.so` runs FLL1 from MCLK1 with the codec as **AIF1
master** (`R0x302 = 0x4000`), so the codec generates BCLK/LRCLK itself and
there is no McBSP sample-rate generator in the path at all.

Our design inverts that (McBSP master, codec slave) and then has to make the
McBSP SRG produce 48 kHz correctly — which is where it is failing.

**This is a genuine design divergence, not a board defect.**  Aligning with the
reference (codec as AIF1 master, FLL1 from BCLK, McBSP as clock consumer) is
the lower-risk route and should be tried before more SRG debugging.

## 7. Bench state

* Region 0 = `0xF97CD9C7`; region 1 = `rootfs-region1-audio-oss.ext4`
  sha256 `3124c322…` (27 modules, fixed `pbplay`, thawing machine driver,
  info-level McBSP register dump).
* `BOOTCTR_FCTRY` topped up by `/init` each boot; reads `0x2e…` and takes the
  `>= threshold 0x1e, no write` path.
* The unit is on our Linux image; the 1917 IFS at block `0x825` is the way
  back to TabletOS.
* No audible output yet.  Analogue output stage confirmed alive: a distinct
  **pop** is audible when `R0x60` (`HPOUT1L/R_OUTP` + `RMV_SHORT`) is written
  during a stream, so the headphone amp and its supplies do work.

## 8. Corrections and dead ends this session

* **"The pad carrying MCLK1 is unknown / could be `0x19E`."**  Settled
  statically, no meter: the reference's own pad resolver
  (`hwio-omap4.so`, tables at file VAs `0x4FF8`/`0x5394`/`0x5730`) maps pad id
  `0xB7` → CORE `0x4A10019C` **low half**, and only `0xB7`/`0xE6` map to that
  offset.  The TRM names the register
  `CONTROL_CORE_PAD0_FREF_CLK2_OUT_PAD1_SYS_NIRQ1` and gives mux 0 =
  `fref_clk2_out`.  The TRM also states the AUXCLK mapping directly — "for
  example, `fref_clk1_out` is controlled by the `SCRM.AUXCLK0` register" — so
  `fref_clk2_out` ← **AUXCLK1**, i.e. the register we program.  **The pad and
  the register are both right; this was never the difference.**  The `0x19E`
  difference report 204 noted is `sys_nirq1`, not the clock.
* **"AVDD1 at 1.8 V explains the silence."**  Not supported as the cause:
  the analogue output stage demonstrably energises (the pop), and the codec
  driver in the reference performs **no** supply control (report 201), so a
  rail mismatch would not be corrected there either.  Still out of spec and
  still worth fixing, but it is not this bug.
* **"`hw_ptr` advancing at 4025/s was a measurement artifact."**  It is real,
  and now explained by `CLKGDV = 47`.
* **Over-supplying data does not help** — a 5280 Hz tone (expecting 440 Hz out
  at 12× slow) produced nothing, consistent with §4: the transmitter is in
  reset, so the divisor affects only how fast `hw_ptr` is credited.

## 9. Console/tooling defects found and fixed (they cost real time)

* **`tools/pbcon.py` mis-diagnosed a busy shell as a dead console.**  The
  target prints no prompt while a foreground command runs, but the ACM tty
  keeps echoing input, so a `timeout` looked like a hang.  It now waits for a
  prompt up to `PBCON_SYNC` and says plainly that the shell is busy instead of
  queueing bytes into it.  Several "the console wedged" episodes this session
  were this, not the device.
* **`os.write()` on the non-blocking tty raised `BlockingIOError`** right after
  a gadget re-enumeration.  Writes now retry with `select()`.
* **`tools/pbraw.py`** (new) dumps every received byte with timestamps.  It
  showed 217 bytes of buffered output arriving at t=0.00 — i.e. the console had
  answered all along — which is how the false diagnosis was caught.
* **`pkexec chmod 666 /dev/ttyACM0` is only needed after a real
  re-enumeration**, not after every boot; it does nothing for a busy shell.
