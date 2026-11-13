# Session 45 (absolute) — E5: the McBSP is the clock master again; the SRG stall is `CLKSM`; the rate is right; the codec still does not decode

> Numbering: absolute `45` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 39** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-13 (single day).
Unit state at the end: **on our Linux image `0x92458D00`** (0x939C5C B, kernel
7.3.3 #33, E5 clock roles + the disabled `&mcbsp1` node for E6), region 1 = the
audio/OSS rootfs sha256 **`a026a454…`** (67,108,864 B, 27 modules, with the two
session-45 diagnostics); console `pb> ` on `ttyACM0`.  No door.  `BOOTCTR_FCTRY`
was not readable from the chroot userland (no `/dev/rpmb` node); the initramfs
`/init` top-up ran at boot as designed.

## Goal of this session

Run the handoff's one experiment: **restore the McBSP as the bit/frame provider
(codec slave, FLL1 from BCLK) and permute `CLKSM`/`FSGM`/`CLKGDV` one per boot,
judged by `pbplay`'s survival past five periods** — the last software-only route
to a live AIF1CLK, since MCLK1 does not clock (reports 206/209).

## Confirmed (measured, read in a file, or seen on the bench)

* **The session-41 master-mode stall has a specific cause: `CLKSM = 0` clocked
  the SRG from the CLKS pad.**  TRM Table 23-52: `CLKSM = 0` (with
  `SCLKME = 0`) derives the SRG clock from `CLKS`; `CLKSM = 1` from the "CPU
  clock".  The live clock tree shows `pad_clks_ck` at 12 MHz with **enable 0
  and no consumer**, and the internal path live
  (`abe_24m_fclk 12.288 MHz -> abe-clkctrl:0030:24 -> 40124000.mcbsp`).  The old
  build therefore had no SRG clock at all: `XRDY = 0` / `XEMPTY = 1`, the
  register picture report 205 §5l recorded.  [report 212 §0/§3]
* **`CLKSM = 1` + `CLKGDV` + `FSGM = 1` runs the transport.**  `SRGR2 = 0x301f`
  (FPER 32, FSGM 1, CLKSM 1), `SRGR1 = 0x0f1f` (CLKGDV 32, FWID 16),
  `PCR0 = 0x0f0f` (CLKXM/FSXM outputs); `pbplay 10 440` writes all 1 933 312
  bytes and exits cleanly.  [report 212 §2]
* **The SRG divides 49.152 MHz, not the 12.288 MHz the framework reports.**
  With `CLKGDV = 8` the stream ran **192 450 frames/s = 4.009×** the request
  (measured over 6 s); 49.152 MHz = `ocp_abe_iclk` = `DPLL_ABE / 2`.  With
  `CLKGDV = 32` (49.152 MHz / 1.536 MHz) the rate is **48 096 frames/s**
  (second measurement: 48 065/s at t = 103 s).  [report 212 §3]
* **The codec is now the AIF1 slave and its FLL1 locks from our BCLK**:
  `R0x302 = 0x0000`, `R0x732 = 0x6020`, `R0x224 = 0x0c83` (REFCLK_SRC = BCLK),
  `R0x200 = 0x0011` (AIF1CLK from FLL1, enabled).  **AIF1CLK exists with no
  MCLK1 anywhere in the path.**  The repair logs `FLL1 locked`.
  [reports 212 §2; capture]
* **The output chain matches the reference register for register**: DAC1
  volumes `0xc0`, `R0x601/0x602 = 0x0001`, `R0x02d/0x02e = 0x0001`,
  `R0x060 = 0x00ee`, `R0x210 = 0x0083` (SR 48 kHz, AIF1CLK_RATE 256), DAC1 mute
  clear on both.  Datasheet Table 106 confirms the codec's clocking model agrees
  with our 12.288 MHz FLL.  [report 212 §4]
* **The application's data is in the DMA ring.**  A session diagnostic reads
  `runtime->dma_area` 50 ms after `trigger(START)`:
  `first -12000 -12000 -12000 -12000 | at hw_ptr 2734: -12000 …` — the ±12000
  square wave, at the header and at the current position.  [report 212 §5]
* **The sDMA feeds the transmitter**: `XBUFFSTAT` alternates 0/1 across eight
  20 ms samples and `IRQST = 0x0710` carries XFSX/XBR/XEOF.  **`DXR`/`DRR` are
  not readable data registers** here (constant `0` / `0xc8318118`).
  [report 212 §5]
* **Still not audible.**  Operator, headphones, valid `pbplay 240 440` test: a
  crackle/pop at stream start, then only the DAC noise floor.  [report 212 §6]
* **Pads are correct as built**: `0x0f6` (clkx) `0x0000`, `0x0f8` (dr)
  `0x0100`, `0x0fa` (dx) `0x0000`, `0x0fc` (fsx) `0x0000`, all claimed by
  `40124000.mcbsp`.  [capture]
* **`omap-mcbsp` is a module**, so a change to it alone does not change the
  chainloader image CRC (the E5 DTS change is what changed `0x7A5DD941`).
* **Kernel patch re-exported and verified**: 54 files / **8 305 lines**,
  sha256 `180f5e19…`, 0 rejects / 54/54 byte-identical.
* **A udev rule for the CDC-ACM console** (`tools/99-playbook-cdc-acm.rules`)
  now makes the tty `0666` on enumeration; installed this session by the
  operator after a flash.  `pkexec chmod 666` remains the documented path when
  the operator is present.

* **The codec's DACDAT is `abe_mcbsp1_dx` (pad `0x102`) — E6, report 212 §10.**
  The oracle was booted (`local/work/ifs-shim-sealed.ifs` at `0x825`) and made
  to play a wav with `/base/usr/bin/audio_out -i /tmp/long.wav`; **the operator
  confirmed the reference is audible**.  With the stream live
  (`R0x200 = 0x0011`) the pads read `0x4A100100 = 0x00001108` → `abe_mcbsp1_dr`
  input, **`abe_mcbsp1_dx` mux 0 OUTPUT**, and `0x4A1000F8 = 0x000F1108` →
  **`abe_mcbsp2_dx` mux 7 = safe_mode**.  A 128-register before/after dump
  (`0x4A100000`–`0x4A1001FC`) shows **no pad changes**: the configuration is
  static.  E5 drives `mcbsp2_dx`, which the codec is not connected to.
* **McBSP1 as the port does not clock the codec — E6, one boot, both regions.**
  `&mcbsp1` enabled (`40122000.mcbsp`, pads `0x0fe`/`0x102`/`0x104` outputs),
  `&mcbsp2` disabled: pads took, `SRGR2 = 0x301f`, `SRGR1 = 0x001f`,
  `PCR0 = 0x0f03`, and the stream ran at **48 053 frames/s**
  (`hw_ptr` 142 032 → 430 352 over 6 s) — but **FLL1 did not lock**
  (`R0x732 = 0x6000` with `R0x224 = 0x0c83`, `R0x220 = 0x0001`).  So the codec's
  BCLK/LRCLK are on the **McBSP2** balls (E5 and report 205 §3 both prove that),
  and the board **splits** the two engines: mcbsp2 = clocks, mcbsp1 = data.
* **DSP_A is not the fault.**  A DSP_A build (`SRGR1 = 0x001f`, `PCR0 = 0x0f03`,
  codec `R0x300 = 0x6018` = the reference's format, FLL1 locked) was also
  silent — consistent with the ball being the cause.
* **The restore is verified**: image `0x92458D00`, `SRGR1 = 0x0f1f`,
  `SRGR2 = 0x301f`, `PCR0 = 0x0f0f`, `R0x302 = 0x0000`, `R0x732 = 0x6020`.

## Inferred (reasoning from evidence, not directly measured)

* **The transport and the codec's clocking are no longer the fault.**  *Strong.*
  Rate correct to 0.2 %, data verified in the ring, FIFO fed, FLL locked,
  output path identical to a working system.
* **The remaining fault is the codec's AIF1 data input** — the ball that carries
  DACDAT, or the frame format.  *Moderate to strong.*  The reference's own
  playback capture leaves **both** `dx` (mux 7 safe mode, driven low) and `dr`
  (input, pull-up) undriven, which is either a capture taken while nothing was
  streaming, or evidence that the codec's DACDAT is not on `dx`.
* **The reference runs DSP_A, not I2S.**  *Strong for the register value*
  (`R0x300 = 0xc018`, and the codec driver itself writes `0x18` for DSP_A), but
  it is not yet known to be the reason our I2S stream is silent.
* **The session-41 "CLKSM = 1 did not help" note is not usable evidence.**
  *Moderate.*  DPLL_ABE was not locked before session 44
  (`ST_DPLL_CLK = 0`), so the ABE clock the SRG divides was not at its nominal
  rate then, and the test's provenance (poke vs build) is not recorded.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The first listening test failed."**  It was run against `pbplay 900 0`
  (freq 0 = silence) that this session had started for a register dump.  The
  valid test needs a non-zero frequency; check what is actually playing before
  drawing a conclusion from "no tone".
* **"`DXR = 0` with `XEMPTY = 1` means the transmit buffer is empty."**  DXR is
  not readable as data on this silicon; the constant read fooled two sessions.
  `XBUFFSTAT` is the register that answers the question.
* **"`SRGR2 = 0x101f` decodes as FSGM = 0."**  Bit 12 is FSGM; it was set.  The
  fault was `CLKSM` (bit 13), not FSGM.
* **"CLKSM = 0 with `prcm_fck` as the parent is a working SRG clock."**
  `clk_set_parent(fclk, prcm_fck)` is not the SRG mux, and `CLKSM = 0` takes the
  CLKS pad, which this board does not drive.
* **"Copying the tracked source into the build tree"** — done in the wrong
  direction once this session, silently reverting a driver edit.  Edit one tree
  and copy **into** the other; verify with `grep` for a new string, not by
  assuming.
* **`pkexec` is the rule.**  It failed here only because the operator was away;
  the documented path is to wait and re-run it, not to invent a workaround.

## Unknown / open questions

* **Which OMAP ball carries the WM8994's DACDAT?**  *Would settle it:* an
  oracle capture of `0x4A1000F0`–`0x4A100100` **and** the codec's AIF1
  registers while the reference is actually playing; or the QNX audio driver's
  pad setup from static RE (report 210 §5).
* **Does DSP_A framing make our stream audible?**  *Would settle it:* the E6
  build — `SND_SOC_DAIFMT_DSP_A` on the link and the CPU DAI, one boot, judged
  by ear plus the `hw_ptr` rate.
* **Why does the codec accept (and lock its FLL to) BCLK but decode nothing?**
  If the ball and the format are both right, the AIF1 slave path itself needs a
  closer look (e.g. an LRCLK polarity/edge mismatch that swaps channels rather
  than silencing — not observed, so unlikely).
* **Is the SRG clock exactly 49.152 MHz?**  The 4.009× measurement is consistent
  with it; a 12.288 MHz SRG with an extra ÷2 is arithmetically impossible given
  `CLKGDV = 7` read back from hardware, so no.

## Superseded / uncertain

* **Report 205 §5c's correction ("`0x18` is I2S")** — the codec driver encodes
  DSP_A as `0x18` and I2S as `0x10`, so the original DSP reading was right and
  the correction is wrong.  Report 212 §8 records it; report 205 is history and
  is left as written.
* **Report 205 §5l's "`DXR = 0`, buffer full" reasoning** — the conclusion
  (a stall after start) still holds for *that* build, but the DXR evidence is
  withdrawn.
* **The session-42 "transmit path proven"** language — proven for *drainage*,
  not for data.  This session separates the two and shows the data never sounded
  even when the ring carried it.

* **"The ball question cannot be answered from the stored capture."**  It can,
  by booting the oracle and looking for pads that are *outputs* (not for pads
  that change).  The stored capture was of an idle system.

## Artefacts produced

* **Report 212** — `docs/reports/212-mcbsp-master-srg-2026-11-13.md`.
* **Capture** — `docs/captures/wm8994-e5-master-srg-2026-11-13.txt`.
* **Machine driver** — E5 clock roles (`CBC_CFC`, CPU `set_sysclk`/`set_clkdiv`,
  FLL1 from BCLK, `PB_MCBSP_FCLK_HZ = 49152000`) and the PCM-ring diagnostic.
* **McBSP driver** — the eight-sample live FIFO dump (`XBUFFSTAT`/`IRQST`).
* **Board DTS** — `mcbsp2_pins` `clkx`/`fsx` back to `PIN_OUTPUT`.
* **Tooling** — `tools/99-playbook-cdc-acm.rules` (udev, CDC-ACM → `0666`).
* **Patch** — `kernel/patches/0001-playbook-winchester-7.3.3.patch`, 54 files /
  8 305 lines, sha256 `180f5e19…`, 54/54 byte-identical.
* **Images** — region 0 `0x92458D00` (0x939C5C B, current; E5 restore + the
  disabled `&mcbsp1` node), the E6 experiment `0x3DC4508D` (not a good state),
  and E5 as first built `0x7A5DD941`; region 1 `a026a454…`.
