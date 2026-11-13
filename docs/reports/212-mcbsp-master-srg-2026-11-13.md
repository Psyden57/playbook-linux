# 212 — E5: the McBSP is the bit/frame provider again; the SRG stall is `CLKSM`, and the SRG clock is the ABE interface clock (session 45)

Date: 2026-11-13 (session 45, Linux-era 39).
Bench: unit on our Linux image **`0x7A5DD941`** (0x939AA8 B, kernel 7.3.3 #31) and
a region-1 rootfs rebuilt twice (final sha256 `5af4af3e…`).  Region 0 and region 1
were flashed together once (DTS pads + driver), then region 1 twice more (driver
only).  No NVRAM/MCT/RPMB write; the image's own `/init` top-up ran at boot.

## 0. Headline

**The session-41 McBSP-master stall is explained and fixed.**  The old design
left `SRGR2.CLKSM = 0`, which per TRM Table 23-52 derives the sample-rate
generator clock from the **CLKS pad**.  This board drives no CLKS: the clock
tree shows `pad_clks_ck` at 12 MHz with **no consumer and not enabled**, while
the internal path is live.  So the SRG never produced `CLKG`, no FSX/BCLK was
generated, and the transmitter sat with `XRDY = 0` / `XEMPTY = 1` — exactly the
signature report 205 §5l recorded.

With `CLKSM = 1` the transport runs: the codec is now the AIF1 **slave**
(`R0x302 = 0x0000`), its **FLL1 locks from our BCLK** (`R0x732 = 0x6020`,
`R0x224 = 0x0C83`), `AIF1CLK` exists from the FLL (`R0x200 = 0x0011`) — **no
MCLK1 anywhere in the path** — and `pbplay` writes a full stream at
**48 096 frames/s**.

**It is still not audible.**  A second, independent finding: the SRG divides the
ABE **interface** clock, not the 12.288 MHz functional clock the clock framework
reports.  Measured 4.009× fast with `CLKGDV = 8`; corrected to `CLKGDV = 32`
(49.152 MHz / 1.536 MHz) and the rate is right.  With correct rate, clocked
codec, open output path, and the application's samples *verified in the DMA
ring*, the codec still outputs silence (operator: a start crackle, then the
noise floor).  The next question is therefore the codec's **data pin / frame
format**, not the transport — see §7.

## 1. What changed (one logical variable: back to the McBSP as clock master)

`sound/soc/ti/playbook-wm8994.c`:

* `.dai_fmt = I2S | CBC_CFC | NB_NF` (codec is consumer) — the ASoC core flips
  it for the CPU DAI, so `omap-mcbsp` takes the `BP_FP` branch: `CLKXM/FSXM/
  CLKRM/FSRM` set and `SRGR2.FSGM = 1` (the SRG's FSG drives FSX).
* `hw_params` calls `snd_soc_dai_set_sysclk(cpu_dai, OMAP_MCBSP_SYSCLK_CLK,
  fclk, …)` (**`CLKSM = 1`**) and `snd_soc_dai_set_clkdiv(cpu_dai,
  OMAP_MCBSP_CLKGDV, fclk / bclk)`; the codec FLL is locked from **BCLK**
  (`WM8994_FLL_SRC_BCLK`), the thaw and the deferred repair are unchanged.
* `PB_MCBSP_FCLK_HZ = 49 152 000` — see §3.

`arch/arm/boot/dts/ti/omap/omap4-winchester.dts`: `mcbsp2_pins` `clkx` and
`fsx` go back to `PIN_OUTPUT` (`0x0000`); `dr` stays `0x0100`, `dx` `0x0000`.

Both sides in one boot (the clock role is one variable).

## 2. The transport now runs (measured)

```
dmesg: WM8994: rate 48000 Hz: AIF1CLK 12288000 Hz, BCLK 1536000 Hz
              (McBSP bit/frame provider, CLKSM=1, CLKGDV=32, FLL1 from BCLK)
       SRGR1 0x0f1f   CLKGDV=31 (/32), FWID=15
       SRGR2 0x301f   FPER=31 (32 CLKG/frame), FSGM=1, CLKSM=1
       PCR0  0x0f0f   CLKXM=1 FSXM=1 (outputs)
       wm8994-codec: repair: done, FLL1 locked
codec: R0x302 0x0000 (slave)   R0x732 0x6020 (FLL1 locked)
       R0x224 0x0c83 (REFCLK_SRC = BCLK, /1)   R0x200 0x0011 (AIF1CLK from FLL1)
```

Two reads of `/proc/asound/card0/pcm0p/sub0/status` six seconds apart, stream
RUNNING: `hw_ptr` 91 171 → 379 746 = **288 575 / 6 s = 48 096 frames/s**
(cross-check at t = 103.05 s with `trigger_time = 23.02`: 48 065/s).

`pbplay 10 440` writes its full 1 933 312 bytes and exits cleanly; the old
failure was `Input/output error` after 82 176 bytes.

## 3. The SRG divides 49.152 MHz, not 12.288 MHz (new)

The first build of E5 kept `PB_MCBSP_FCLK_HZ = 12 288 000`, i.e. `CLKGDV = 8`,
and the stream ran **4.009× fast**:

```
hw_ptr 355 524 -> 1 510 225 over 6 s = 192 450 frames/s = 4.009 x 48 kHz
```

`CLKGDV = 7` was read back from `SRGR1 = 0x0f07`, so the register is not the
explanation.  The clock tree, read live with `debugfs` mounted:

```
dpll_abe_ck            98304000      (session-44 fix; locks from SYS_CLK)
 dpll_abe_m2_ck        49152000
  abe_clk              98304000
   ocp_abe_iclk        49152000   <-- the SRG divides this
   abe_24m_fclk        12288000   <-- what the framework calls the McBSP fclk
    abe-clkctrl:0030:24 12288000  consumer 40124000.mcbsp, "fck"
pad_clks_src_ck        12000000
 pad_clks_ck           12000000  enable 0, no consumer (CLKSM=0 would take it)
```

4 × 12.288 = 49.152 = `ocp_abe_iclk` = `DPLL_ABE/2`, and 49.152 MHz / 1.536 MHz
= **32**, so `CLKGDV = 31`.  With that the rate is 48 096/s (§2).  Note this is
also *why* the session-41 build could not have worked even had `CLKSM` been
right: **before session 44 DPLL_ABE never locked** (`ST_DPLL_CLK = 0`), so the
ABE clock the SRG divides was not at its nominal rate.

## 4. Codec-side state matches the reference, register for register

A full 1905-register dump taken during a stream (silence tone, so the trunk
path is quiet) diffed against `docs/captures/wm8994-reference-playing-2026-11-11.txt`
gives 76 differences.  The ones that matter are by design or are the unresolved
format question; the **playback chain is identical**:

| register | reference | ours | meaning |
|---|---|---|---|
| `R0x610`/`R0x611` DAC1 volume | `0x00c0` | `0x00c0` | 0 dB |
| `R0x601`/`R0x602` DAC1 mixer | `0x0001` | `0x0001` | AIF1DAC1L/R → DAC1L/R |
| `R0x02d`/`R0x02e` OUTMIX | `0x0001` | `0x0001` | DAC1 → MIXOUT |
| `R0x060` ANALOGUE_HP_0 | `0x00ee` | `0x00ee` | HPOUT1 path on |
| `R0x420` DAC1 filters | `0x0030` | `0x0000` | `AIF1DAC1_MUTE` clear on both |
| `R0x210` AIF1_RATE | `0x0083` | `0x0083` | SR = 8 (48 kHz), AIF1CLK_RATE = 256 |
| `R0x300` AIF1_CONTROL_1 | `0xc018` | `0x6010` | **format: DSP_A vs I2S** |
| `R0x302` master/slave | `0x4000` | `0x0000` | by design (E5 inverts it) |
| `R0x305` AIF1DAC_LRCLK | `0x0040` | `0x0020` | BCLK/LRCLK ratio 64 vs 32 |

Datasheet check (Rev 4.6, Table 106): `AIF1CLK_RATE = 0011` = **256** and
`AIF1_SR = 1000` = 48 kHz on both sides, so the codec's own clocking model
agrees with the 12.288 MHz our FLL produces.

## 5. The samples are real and they reach the transmitter

Two diagnostics were added to the drivers this session (they are in the patch;
retire them when the audio path works):

* `playbook_wm8994_repair()` reads the substream's `runtime->dma_area` at
  `trigger(START) + 50 ms`:

  ```
  pcm: rate 48000 fmt 2 width 16 area 65536 B first -12000 -12000 -12000 -12000
       | at hw_ptr 2734: -12000 -12000 -12000 -12000
  ```

  i.e. the application's ±12000 square wave is in the ring the sDMA copies
  from, both at the header and at the current position.
* `omap_mcbsp_steady_dump()` now samples the port eight times, 20 ms apart:

  ```
  live 0: DXR 0x00000000 DRR 0xc8318118 XBUFFSTAT 0x0000 RBUFFSTAT 0x0000
          SPCR2 0x02f5 IRQST 0x0710
  … XBUFFSTAT alternates 0x0000/0x0001; IRQST constant 0x0710
  ```

  `XBUFFSTAT` toggling shows the sDMA filling the transmit register; `IRQST`
  carries the XFSX/XBR/XEOF events, i.e. the transmitter is framing.  **`DXR`
  and `DRR` are not readable data registers on this silicon** — they return a
  constant (`0` / `0xc8318118`) regardless of traffic, so the "DXR = 0 means the
  transmitter is empty" reading in report 205 §5l is withdrawn.

## 6. What is heard (operator, on headphones)

* a **crackle/pop at stream start** — the analogue power-up of the repair, as in
  report 205 §5o;
* then **only the DAC's noise floor** — the converter and the HPOUT1 path are
  alive, but no tone.

One listening test in this session was invalid: it was run against
`pbplay 900 0` (freq 0 = silence) started for a register dump, so "no tone" was
correct behaviour.  The valid test used `pbplay 240 440`.

## 7. Where the fault now is, and what would settle it

The transport, the DMA feed, the codec's clocking and the codec's output chain
are all verified.  What remains is **the codec's AIF1 data input**:

1. **The ball.**  The reference's own playback capture leaves
   `abe_mcbsp2_dx` at mux **7 = safe_mode** (`0x000F`) and `abe_mcbsp2_dr` as an
   **input** (`0x1108`), i.e. the reference drives *neither* data ball while the
   capture was taken.  Either that capture was not taken during real playback,
   or the codec's DACDAT is not on `dx` and the board's data ball is something
   else.  This is the highest-value open question.
2. **The format.**  `R0x300 = 0xc018` on the reference is **DSP_A**
   (`AIF1_FMT = 0b11`), not I2S — the correction in report 205 §5c ("0x18 is
   I2S") is itself wrong: the codec driver writes `0x18` for DSP_A and `0x10`
   for I2S.  Ours is I2S (`0x10`), which is self-consistent with our McBSP
   framing, so this is a candidate only if the two sides disagree about edges.
   A DSP_A build (DAI format `SND_SOC_DAIFMT_DSP_A`, 1-bit data delay on both
   sides) is one boot and is the reference's proven format.

`would settle it:` (1) a live oracle capture of `0x4A1000F0`–`0x4A100100` and
the codec's AIF1 registers **while the reference is actually playing**, or the
QNX driver's own pad setup from static RE (report 210 §5 lists it as unread);
(2) the DSP_A build, judged by ear and by the same `hw_ptr` measurement.

## 8. Corrections and dead ends

* **"`SRGR2 = 0x101f` means CLKSM = 0, FSGM = 0"** (report 205 §4/§5l) —
  **wrong decode**: bit 12 is `FSGM` and it was set; bit 13 (`CLKSM`) was clear.
  The old build was "SRG from the CLKS pad", which is a different fault.
* **"`DXR = 0` means the transmit buffer is empty / the port is idle"** —
  withdrawn: the register is not a readable data register.
* **"CLKSM = 1 was tried and did not help"** (report 205 §5l) — that test was
  not reproducible from this session's evidence, and the ABE DPLL did not lock
  at that time, so the SRG clock was not the nominal 24M/49.152 MHz anyway.
* **"The reference uses I2S"** (report 205 §5c correction) — contradicted by the
  codec driver's own encoding; the reference's `0x18` is DSP_A.

## 9. Evidence

* `docs/captures/wm8994-e5-master-srg-2026-11-13.txt` — both boots, the rate
  measurements, the clock tree, the codec reads.
* `docs/captures/wm8994-reference-playing-2026-11-11.txt` — the comparison
  target (unchanged).
* Images: region 0 `0x7A5DD941` (E5), region 1 sha256 `5af4af3e…`.

## 10. E6 — the codec's DATA ball is `abe_mcbsp1_dx`, and the two engines are split (same session, after §7)

The oracle was booted (IFS at `0x825`, report-207 image) and made to **actually
play** a wav with `/base/usr/bin/audio_out -i /tmp/long.wav`.  The operator
confirmed the tone is audible on the unit.  With the stream live
(`R0x200 = 0x0011`, AIF1CLK on) the reference's pads were read:

```
0x4A1000F4 = 0x0100000F   abe_mcbsp2_clkx = 0x0100  input
0x4A1000F8 = 0x000F1108   abe_mcbsp2_dr   = 0x1108  input, pull-up
                          abe_mcbsp2_dx   = 0x000F  mux 7 = SAFE MODE
0x4A1000FC = 0x01000100   abe_mcbsp2_fsx  = 0x0100  input
                          abe_mcbsp1_clkx = 0x0100  input
0x4A100100 = 0x00001108   abe_mcbsp1_dr   = 0x1108  input, pull-up
                          abe_mcbsp1_dx   = 0x0000  MUX 0, OUTPUT   <--
```

A before/after dump of `0x4A100000`–`0x4A1001FC` (128 registers) taken during
playback and after it shows **no pad changes**: the ball configuration is
static, so the "what does it drive" question had to be answered by looking for
outputs, not by a diff.

**So the codec's DACDAT is on `abe_mcbsp1_dx` (pad `0x102`), and `abe_mcbsp2_dx`
— the ball E5 drives — is not connected to the codec at all.**  That is the
silence: the codec receives valid clocks, its FLL locks, and its data input is
a pin our kernel never drives.

**E6 (one boot, both regions): McBSP1 as the port** — `&mcbsp1` enabled,
`&mcbsp2` disabled, `mcbsp1_pins` = clkx `0x0000`, dr `0x0100`, dx `0x0000`,
fsx `0x0000`; the machine driver's DAI moved to `40122000.mcbsp`; `DSP_A` kept.
Measured: pads took (`0x0fe`/`0x102`/`0x104` = `0x0000`), `SRGR2 = 0x301f`,
`SRGR1 = 0x001f`, `PCR0 = 0x0f03`, and the stream ran at **48 053 frames/s**
(`hw_ptr` 142 032 → 430 352 over 6 s) — **but FLL1 did not lock**:
`R0x732 = 0x6000` with the FLL programmed for BCLK (`R0x224 = 0x0c83`,
`R0x220 = 0x0001`).

**Conclusion (strong):** the codec's **bit/frame clock is on the McBSP2 balls**
(proven twice — E5 here and the pre-E1 BCLK-referenced FLL in report 205 §3),
and its **data is on `abe_mcbsp1_dx`**.  The board therefore uses the two
engines for different signals: mcbsp2 for clocks, mcbsp1 for data.  Driving
mcbsp1's clocks does not reach the codec.

**The design this implies (not attempted):** keep McBSP2 as the clock master
(E5, proven) and add **McBSP1 as a clock *consumer*** whose clkx/fsx **inputs**
are wired to the same nets — it transmits the samples on `mcbsp1_dx`.  ASoC
carries one CPU DAI per link, so the clean form is a second `snd_soc_dai_link`
with `snd-soc-dummy` as its codec, both links driven from the same four
`hw_params`/`trigger` hooks, or a hand-configured McBSP1 in the machine driver.
Before building it, the open sub-question is whether **mcbsp1's clock inputs
see mcbsp2's clock outputs**; E6 shows only that mcbsp1's clock *outputs* do not
reach the codec.  *Would settle it:* a consumer-mode McBSP1 build and its
`SPCR2`/steady dump (`XEMPTY`/`XRDY` moving means its inputs are clocked).

The bench was left on the restored E5 pair (§11), because E6 loses the codec
clock and is not a better state to hand over.

## 11. Final bench state

* Region 0 = **`0x92458D00`** (0x939C5C B, kernel 7.3.3 #33): E5 as built this
  session, plus the (disabled) `&mcbsp1` node and its pad group so the E6
  groundwork is in the tree.
* Region 1 = the audio/OSS rootfs sha256 **`a026a454…`**, machine driver back on
  McBSP2/I2S.
* Verified after the restore: `SRGR1 = 0x0f1f`, `SRGR2 = 0x301f`,
  `PCR0 = 0x0f0f`, codec `R0x302 = 0x0000`, `R0x732 = 0x6020` (FLL1 locked),
  stream at 48 kHz.

## 12. E7/E8 — the two engines DO share the clocks, and how not to program them

**E7: McBSP1 is clocked by the nets McBSP2 drives.**  Progamming McBSP1 as a
clock consumer (PCR0 = `0x000f`, no `CLKXM`/`FSXM`) and writing one word
(`0x12345678`) to its DXR while McBSP2 streamed:

```
mcbsp1: SPCR2 after enable 0x0233
mcbsp1: CLOCKED - word consumed after 5 ms (SPCR2 0x0237, PCR0 0x000f)
```

`XRDY` (bit 1) set 5 ms later means the shifter took the word, i.e. a frame
sync arrived.  So **the two engines share the BCLK/LRCLK nets**: mcbsp2 can
generate, mcbsp1 can consume, and the codec sits on the same nets.  This
supersedes the 0x0fe GPIO probe earlier in the session, which read a constant
zero and wrongly suggested the nets were separate (the probe was invalid: with
the pad muxed to GPIO and the engine idle, it measured a floating line).

**E8: programming McBSP2 from raw registers is not safe.**  With the data link
moved to McBSP1, the machine driver tried to be the clock generator itself
(ioremap `0x40124000`, then SRGR1/SRGR2/PCR0/SPCR2).  The writes landed (the
readback logged `SRGR1 0x0f1f SRGR2 0x301f PCR0 0x0f0f`) but the codec's FLL did
not lock, and the unit then died with

```
Unhandled fault: imprecise external abort (0x1406) at 0x00082000
```

— trap 158's class at kernel level: the L4_ABE target was idle (`clk_summary`
showed `abe-clkctrl:0030:24` enable count 0), so the access aborted and took the
kernel down.  `pm_runtime_get_sync()` on the mcbsp device did not make the
target active.  **The clock generator must therefore be an ASoC link driven by
`omap-mcbsp`**, which owns the runtime PM; raw pokes are not an option.

**What that leaves for the design.**  Two links, both managed by the driver:
`mcbsp2 + wm8994-aif1` (clocks + codec, exactly E5) and `mcbsp1 + a codec DAI
that accepts S16` (data, clock consumer).  A `snd-soc-dummy` codec was tried
for the data link and the OSS side rejected the format
(`SNDCTL_DSP_SETFMT: Invalid argument` → the CPU DAI's `hw_params` saw a
non-S16 format and returned `-EINVAL`), so the open item is which codec DAI the
data link uses — `wm8994-aif2` (a second DAI of the real codec, needing no
board wiring) is the obvious candidate.

## 13. Bench state after the E7/E8 work

Restored to the committed E5 pair: region 0 **`0x5A04ACD9`** (0x9395C4 B,
kernel 7.3.3 #35, the exact tree at `HEAD` — no `&mcbsp1` node), region 1
sha256 **`c6dfd25f…`**.  Verified: `SRGR1 = 0x0f1f`, `SRGR2 = 0x301f`,
`R0x302 = 0x0000`, `R0x732 = 0x6020` (FLL1 locked), stream at 48 kHz.

## 14. E9 — the two-link build: the link format, the rate symmetry, and the wall

Built the two-engine design properly (session 45, after §12): link 1 =
`mcbsp2 + wm8994-aif1` exactly as E5 (clocks + codec), link 2 = `mcbsp1` as a
clock consumer + `wm8994-aif2` (data on `mcbsp1_dx`).  Three findings, one
negative result:

**1. The data link must be `CBP_CFP`, not `CBC_CFC`.**  The link format is
codec-perspective and the core flips it for the CPU DAI.  With `CBC_CFC` the CPU
Dai becomes the clock *provider*, so `omap_mcbsp_dai_hw_params()` ran the
master-mode bandwidth check and failed:

```
omap_mcbsp_dai_hw_params: not enough bandwidth for desired rate and channels
omap-mcbsp 40122000.mcbsp: ASoC error (-22): at snd_soc_dai_hw_params()
```

an `-EINVAL` that looks like a format problem but is not.  `CBP_CFP` gives the
CPU DAI the `BC_FC` (consumer) branch: no SRG, no bandwidth check.

**2. `wm8994-aif2` is a usable codec DAI for the data link.**  It advertises the
same S16/S24/S32 set as AIF1, so the OSS format negotiation lands on S16 (a
`snd-soc-dummy` codec advertises 8-bit and drags the negotiation to U8, which
`omap-mcbsp` rejects).  AIF2's dividers need `aifclk[1]`, so the machine driver's
`hw_params` calls `snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_FLL1, …)`.

**3. The card enforces rate symmetry between the two links.**  `hw_params` at
8 kHz on the data PCM is rejected while the clock PCM runs at 48 kHz:

```
ASoC error (-22): unmatched rate symmetry: 40122000.mcbsp:8000 - soc_pcm_params_symmetry:48000
```

so the data PCM must be opened at the same rate.  With `pbplay` setting `SPEED`
before `CHANNELS`/`SETFMT` (OSS re-negotiates the whole parameter set on every
`SNDCTL_DSP_*` ioctl, using the runtime's current rate), the data PCM opens
cleanly at 48 kHz/S16/2ch.

**4. The data port still stalls — and this is the wall.**  With the data PCM
open at 48 kHz the write dies with `Input/output error (after 82176 bytes)` and
McBSP1 sits at

```
SPCR2 = 0x0235    XRST = 1, XRDY = 0, XEMPTY = 1
```

i.e. the transmit buffer is full and the shifter never loads a word: **no frame
sync arrives at McBSP1**.  Repeating the E7 experiment with the *driver*
configuring the port (instead of a hand-written DXR poke) does not reproduce
E7's "word consumed".

**E7 is therefore withdrawn**: its "CLOCKED - word consumed after 5 ms" was an
artifact of reading `XRDY` (which reads 1 both for "ready" and for "nothing
pending").  **McBSP1's clock pins are not on the same nets as McBSP2's**, which
agrees with the pad-as-GPIO probe (§12) after all.

**What this means.**  The board splits its audio signals across two engines —
clocks on McBSP2's balls, data on McBSP1's DX — and the only way to clock
McBSP1 from the same reference is the SoC's internal audio back end: the
reference driver's strings include `StereoAudioCodecRequestMcbspClk` and
`abe_connect_serial_port … mcbsp_id=%d`, i.e. QNX clocks the McBSP from the ABE
rather than from a pad.  Mainline `omap-mcbsp` has no such path.  **A Linux
design for this board's codec has to route the codec's BCLK/LRCLK to McBSP1
through the ABE, or accept that the reference's clocking cannot be reproduced
with the plain McBSP driver.**  That is the next question, and it is a design
question, not a register one.

## 15. Boot stability (session 45, operator-observed)

One boot after the E8 build came up with the backlight on, hung ~10 s and reset
(the *loader's* 7.8 s WDT, i.e. the kernel died before its own 58.6 s WDT was
programmed).  It did not reproduce: three warm reboots on the same image and a
**cold boot** (battery pulled) all came up cleanly with no `external abort` /
`oops`.  Treated as leftover state from the E8 crash and the pad pokes, not a
code regression — but it is recorded because it was real.

The cold boot also settled a standing worry: the WM8994 starts at `R0x732 =
0x6000` (FLL1 enabled, unlocked) and `R0x224 = 0x0c80` (MCLK1), and E5's stream
then locks it (`0x6020`, `0x224 = 0x0c83`).  **The FLL lock from BCLK is genuine
on a cold codec**, not a stale bit carried across a warm reset.
