# Session 41 (absolute) — WM8994 audio: the codec chain solved, the McBSP transmit path isolated

> Numbering: absolute `41` (see `NUMBERING.md`).  This is Linux-era **session
> 35**, and report 205 is the technical record.

Date / span: 2026-11-11 (a long single-day session with several multi-hour gaps
where the operator was away).
Unit state at the end: **on our Linux image `0xB8830701`** in region 0, region 1
= `7cc63990…`; `BOOTCTR_FCTRY` read `0x26` and took the ">= threshold 0x1e, no
write" path.

## Goal of this session

Continue item 9 step 5 (audio).  The handoff's stated single experiment was to
name the pad carrying the codec's MCLK1 from the reference binary and compare
`0x4A10019C` live on both OSes — i.e. still a *diagnosis* goal, not a fix.

What actually happened is broader: the codec side of playback was taken from
"never produces any output" to **driver-driven and audibly converting**, and the
remaining fault was localised to the McBSP transmit path.

## Confirmed (measured, read in a file, or seen on the bench)

* **The MCLK1 pad is `0xB7` → CORE `0x4A10019C` low half (`fref_clk2_out`).**
  Decoded from the reference's own resolver in `hwio-omap4.so` (tables at file
  VAs `0x4FF8`/`0x5394`/`0x5730`; only ids `0xB7`/`0xE6` map to that offset).
  The TRM names the register
  `CONTROL_CORE_PAD0_FREF_CLK2_OUT_PAD1_SYS_NIRQ1` with mux 0 =
  `fref_clk2_out`, and states the AUXCLK mapping explicitly ("`fref_clk1_out` is
  controlled by the `SCRM.AUXCLK0` register"), so `fref_clk2_out` ← **AUXCLK1**.
  Report 205 §8.  **The handoff's `0x19C` vs `0x19E` question is closed: the DTS
  was right.**
* **The DTS supplies are not the difference.**  Live read-only over the door on
  TabletOS vs our Linux: V1V8 1823 mV, V2V1 2127 mV, VANA 2100 mV, VDAC
  1800 mV, VAUX1/2/3 2800 mV, all ON — **identical lists and voltages on both
  OSes** (`pmic-twl6030-ctl get_state/get_vout`).  A working system runs on
  exactly our rails.
* **`AUXCLK1` booted at `0x00000100` (÷1 = 38.4 MHz) on every cold boot**,
  measured on a battery-pulled boot.  38.4 MHz is far outside the WM8994 FLL's
  0.032–13.5 MHz reference range, so FLL1 could never lock.  **Fixed in the
  board DTS** with `assigned-clocks`/`assigned-clock-rates = <12800000>` on the
  codec node; after the fix a cold boot gives `AUXCLK1 = 0x00020100` and
  `clk_summary` reports `auxclk1_ck = 12800000` from the first moment.
* **The 48 kHz rate was 12× slow.**  `SRGR1 = 0x0F2F` → `CLKGDV = 47` (÷48, the
  8000 Hz value) while `FPER` was correct for 48 kHz.  `omap_mcbsp_dai_hw_params()`
  early-returns once `mcbsp->configured` is set, and OSS opens the device at its
  default 8000 Hz format first, so only the 8 kHz divider was ever programmed.
  Fixed by comparing rate/width before the early return; verified **`hw_ptr`
  4 025 → 48 177 frames/s** and `SRGR1 = 0x0F07`.
* **`pbplay` never streamed.**  It asked for `SETFRAGMENT 0x00040008` (a
  64-frame ring for S16_LE stereo) then wrote 1024-frame chunks, so it overran on
  its first write every run; every earlier session's "no audible output" was
  measured with a tool that never streamed.  Fixed to use
  `SNDCTL_DSP_GETBLKSIZE` and a larger fragment.
* **The AIF1 word length was never programmed** (24-bit reset default `0x4050`).
  Now 16-bit.
* **`MIXOUTL_ENA`/`MIXOUTR_ENA` are PM3 (`R0x03`) bits 5:4, not PM4.**  PM4
  (`R0x04`) is the digital-microphone block.  DAPM's own debug line said
  `R3(0x3) mask 0x20` and was correct; this session wrote `0x0330` to PM4 for a
  long time with no effect on the output path.
* **`CP_ENA` (`R0x4C` bit 15) was never set.**  The reference has
  `R0x4C = 0x9F25`; ours read `0x1F25`.  The charge pump *powers the headphone
  amplifier* — without it the output stage energises (an audible pop) but cannot
  drive.  Enabling it produced the first audible output from this image (the
  DAC noise floor), confirmed by the operator.
* **The regmap cache silently swallowed the repair writes.**  After a period of
  dropped writes the cache holds the *intended* values, so `update_bits()` sees
  no change and skips the bus write — `PM3` read `0x0000` while the driver had
  "written" `0x00F0`.  Fixed with `regcache_mark_dirty()` + `regcache_sync()` at
  the top of the repair.  Same class as report 200 / DECISIONS §49.
* **The driver never re-enabled `AIF1CLK` after its own thaw**, so the DAC lost
  its clock and the audible floor vanished after every `hw_params`.  Now
  re-enabled last, in the delayed repair work.
* **A cold battery pull does not clear `AUXCLK1` into the good state** — it
  reproduces `0x00000100`.  The "value survives a warm reset" note in the
  handoff is consistent but the cold-boot value is the *bad* one, not the good
  one.
* **The reference keeps the FLL locked while gating AIF1CLK.**  Measured with
  targeted single-register reads at idle (no stream): `R0x200 = 0x0010`
  (**`AIF1CLK_ENA = 0`**) with `R0x732 = 0x6020` — the FLL is still locked — and
  the analogue chain powered down (`R0x003 = 0x0000`, `R0x005 = 0x0000`,
  `R0x04C = 0x1F25`).  That is why its codec never freezes: it is never in the
  "ENA = 1 with a stopped source" state.  Two full 1905-register dumps were also
  taken, **both during playback**, in `docs/captures/`; they differ in only
  `R0x402`/`R0x403` (a volume nudge).
* **In steady state the McBSP transmit register is empty.**  With everything
  else correct and a live 48 kHz stream: `DXR = 0x00000000` (32-bit register at
  offset 0x8, the only legal access per TRM 23.5.4.8), `SPCR2 = 0x02F5`
  (`XRST=1`, `XRDY=0` buffer full, `XEMPTY=1` shifter empty), while
  `omap-dma-engine` interrupts advance 12/s (= 48 000/4096) and `hw_ptr` runs at
  48 185 frames/s.  Report 205 §5l.
* **The sDMA target address is correct**: the McBSP node declares
  `reg-names = "mpu","dma"` with `"dma"` → `0x49024000`, so `phys_dma_base` is
  the L3 alias and the DMA writes `0x49024008` while the CPU reads
  `0x40124008` — the standard two-view OMAP arrangement.
* **The SoC side is byte-identical to QNX**: `AUXCLK1 = 0x00020100`, pad
  `0x19C` low `0x0000`, `CONTROL_CORE_PADCONF_MODE = 0x00000000`, SMART3 drive
  `0xBD7F5501`/`0x55703057`/`0x67000510`, `DPLL_PER` M3x2 `0x00000007`.

## Inferred (reasoning from evidence, not directly measured)

* **The remaining fault is inside the McBSP transmit path, below its register
  file.**  *Strong* — the codec is at the reference's exact values, the DMA runs
  at the right rate, and `XRDY=0`/`XEMPTY=1` is internally consistent: the FIFO
  holds data that never shifts out.
* **`CLKSM = 0` with `FSGM = 0` is a plausible cause.**  *Moderate* — with
  `CLKSM = 0` the SRG takes its input from CLKS (which
  `OMAP_MCBSP_SYSCLK_CLKS_FCLK` switches to the PRCM fclk, so this is nominally
  fine), and `FSGM = 0` means the SRG does not generate FS in master mode.
  Setting `CLKSM = 1` was tried and did not help.  Needs a scope.
* **The reference's codec-as-AIF1-master design is the structural difference
  worth copying.**  *Moderate* — it makes the McBSP a clock consumer, removing
  the SRG from the path entirely.  Not yet tried as a pair (DAI format + CPU
  clock role).

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The console keeps wedging."**  It never did.  `tools/pbcon.py` timed out
  while the target was still inside a foreground command; the ACM tty echoes
  input regardless, so a busy shell looks dead.  Proven by
  `tools/pbraw.py` (new), which dumped 217 bytes of buffered output arriving at
  t=0.00 — every answer had been there.  The repeated `pkexec chmod` was
  irrelevant except after a genuine re-enumeration, and the operator said so.
* **"The pads are wrong."**  I wrote a whole report section claiming the McBSP
  pad values differed from the reference and that `dx` should be in safe_mode.
  Both wrong: the reference is the clock master so it does not drive `dx`, while
  we are the master and must.  Setting our pads to the reference's exact values
  changed nothing.  Corrected in report 205 §5a.
* **"The noise floor proves the DAC is converting AIF1 data."**  Disabling the
  AIF1→DAC routing while the floor was audible made **no difference** — so the
  floor is the analogue output stage idling, not data.  Retracted in §5i.
* **"`DXR`/`DXR1` reading 0 means the McBSP is not transmitting."**  The first
  dump ran at the end of `omap_mcbsp_start()`, before the sDMA had loaded
  anything, so zero was expected.  Then I read `DXR1` (the OMAP1/2420 16-bit
  layout) instead of the OMAP4 32-bit `DXR`.  Only the delayed 32-bit read is
  meaningful — and it still reads 0, which is why the conclusion survived the
  correction.
* **"`R0x300 = 0x18` is DSP mode."**  `AIF1_FMT` is bits `[4:3]`: `0x1` is I2S.
  The reference and we are both 16-bit I2S.  Corrected.
* **"DSP-mode master matching the reference will fix it."**  Setting the codec
  to the reference's master configuration **silenced the noise floor**, because
  the McBSP was still I2S and still clock master — two masters on one frame.
  An invalid test, not a result.
* **"The charge pump explains the silence."**  It explains the *absence of
  audible output* (no drive), not the absence of programme material — after
  enabling it the floor appeared and the tone still did not.
* **"`MCLK1` locks the FLL reliably now that the codec is thawed."**  It locked
  once, then stopped locking on later boots and on a battery-pulled cold boot
  with an identical register file (`0x224=0x0000`, `0x221=0x0700`,
  `0x222=0xAE14`, `0x223=0x00E0`), PM2 supplies set and `auxclk1_ck` at
  12.8 MHz.  The driver therefore stays on the **BCLK** path, which locked on
  every trial.  §5h.
* **"The kernel's `clk_summary` confirms the pin rate."**  It reported
  `auxclk1_ck = 38 400 000` even after SCRM was written to `0x00020100` and
  verified stable by three reads — the framework's cache is stale, so it cannot
  be used to confirm what the codec receives.
* **"A WAV file instead of `pbplay` would diagnose better."**  The operator
  suggested it; it would only change the sample source, not the transport, so it
  could not have helped while `DXR` was empty.

## Unknown / open questions

* **What does `abe_mcbsp2_dx` (pad `0x0F8` high) actually do during playback?**
  *Would settle it:* a scope.  Toggling ⇒ the data is present and the codec
  mis-frames it; static ⇒ the McBSP never transmits.
* **Is the transmit FIFO blocked by the sDMA's port configuration or by a
  missing shift clock?**  *Would settle it:* a `dx`→`dr` loopback with a capture
  stream (proves the transmit path without the codec), or the scope above.
* **Does `FSGM = 0`/`CLKSM = 0` matter on this silicon?**  *Would settle it:* the
  scope, distinguishing FS generation from BCLK generation.
* **Why does MCLK1 lock the FLL only sometimes, with an identical register file
  and identical SoC programming?**  *Would settle it:* a meter/scope on MCLK1
  during a lock and a no-lock boot.
* **Would the reference's codec-as-AIF1-master design work here?**  *Would
  settle it:* one boot with both sides changed (DAI format + CPU DAI as clock
  consumer).
* **Why does `RX Buffer Underflow!` fire on every playback-only trigger?**  It is
  the receive interrupt with `RRST = 0`; a stale status bit, but unexplained.

## Superseded / uncertain

* **"MCLK1 does not clock under our image"** (report 204, handoff).  Now
  **partly corrected**: the FLL locked from MCLK1 once this session with the
  codec thawed, so the capability exists; but it is **not reliable**, and after a
  cold boot with `AUXCLK1` forced to 12.8 MHz it still refused.  Report 205 §5h is
  current; report 204's blanket denial is too strong.
* **"The blocker is one clock below the registers we can see."**  Retired.  The
  pads, AUXCLK1, padconf mode, SMART3 drive, DPLL_PER and the rails are all
  identical to QNX; the one measured clock defect (AUXCLK1 ÷1) is fixed.
* **"The DTS supply mapping is the problem."**  Dead — identical rails.
* **"`next step: resolve the MCLK1 pad`."**  Done: it is `0xB7`/`0x19C` and the
  DTS was right.

## Artefacts produced

* **Report 205** — `docs/reports/205-wm8994-mcbsp-divider-and-audio-2026-11-11.md`
  (supersedes the audio conclusions of 203/204 and closes the pad question).
* **Captures, new** — `docs/captures/wm8994-reference-live-2026-11-11.txt`
  (idle) and `docs/captures/wm8994-reference-playing-2026-11-11.txt` (during
  playback), 1905 registers each, from the working 2.1.0.1917 reference.
* **Board DTS** — `omap4-winchester.dts`: `assigned-clocks`/`assigned-clock-rates
  = <12800000>` on the WM8994 node (the AUXCLK1 cold-boot fix).
* **Machine driver** — `sound/soc/ti/playbook-wm8994.c`: thaw, FLL-first
  ordering, delayed repair work with `regcache_mark_dirty`/`regcache_sync`,
  PM3 output mixers, charge pump, DAC→mixer un-mute, AIF1→DAC routing,
  `AIF1CLK` re-enable, output stage.
* **McBSP driver** — `sound/soc/ti/omap-mcbsp.c`: re-program on a rate/width
  change instead of early-returning, info-level register dump, steady-state
  delayed dump (32-bit `DXR`/`DRR`).
* **Tools** — `rootfs/pbplay.c` fixed (granted block size, larger fragment,
  louder tone); `tools/pbraw.py` new (raw byte-level console diagnostic);
  `tools/pbcon.py` fixed (busy-shell detection, `EAGAIN`-tolerant writes).
* **Images** — region 0 **`0xB8830701`** (kernel `7.3.3`, `0x93A8F4` bytes);
  region 1 `local/images/rootfs-region1-audio-oss.ext4`, sha256
  `7cc63990…` (27 modules).
* **Kernel patch was NOT re-exported** this session — the tree has the changes
  but `kernel/patches/0001-playbook-winchester-7.3.3.patch` still describes
  session 39's 53 files / 7 773 lines.  **A next-session task.**  See KNOWN-ISSUES.
