# Session 46 (absolute) — the ABE question is closed, and the WM8994 plays stereo on McBSP3

> Numbering: absolute `46` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 40** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-13 (single day).
Unit state at the end: **on our Linux image `0xF8BADFF3`** (region 0,
`0x93993C` B, kernel 7.3.3 **#42**), region 1 = rootfs **`c0719d28…`**
(67,108,864 B, 27 modules).  `0fca:8010`, console `pb> ` on `ttyACM0`, no
door/ssh.  `BOOTCTR_FCTRY` read **0x20** at the start (from the `/init` log;
`0x20 >= threshold 0x1e, no write`); the image tops it up at boot.  No
device-side files were deployed; the oracle IFS was flashed at `0x825` for the
routing probe and the unit was returned to Linux.  The final tone was heard on
headphones.

## Goal of this session

Run ROADMAP item 9's step (1): **static feasibility of the OMAP4 ABE** — can an
ABE serial port be clocked and pointed at a McBSP without the DSP firmware?
The prompt's "done" was a written answer and a revised plan.  The answer led
directly to the audio goal, so the session continued through the corrected
two-engine build and closed item 9's audio item.

## Confirmed (measured, read in a file, or seen on the bench)

* **Initial bench state:** `uname -a` = `7.3.3 #40`; region 0 `0x58F85337`;
  the `/init` log `pbrpmb: topup: BOOTCTR_FCTRY ... 20000000`,
  `0x20 >= threshold 0x1e, no write`.  The kickoff's `0x5A04ACD9`/#35 line was
  stale.  [console read]
* **The ABE serial port is a firmware data path.**  The ABE/HAL Addendum
  (SWPU245F) §3.2.7 and the shipped HAL: `abe_connect_serial_port` writes the
  AE port table/ATC descriptor, `abe_init_atc`'s serial-port case is a no-op,
  `abe_load_fw` loads `abe_firmware_array`.  [report 213 §2–§3;
  `libabe.so.1` VAs `0x4a4c`/`0x8a34`/`0x59d0`]
* **The McBSP clocks are not part of that path.**  TRM §23.5.4.3/§23.5.4.9:
  `CLKS` is `MCBSPx_FCLK` or `abe_clks`; the SRG selects CLKS/`ABE_ICLK2`/
  `CLKX`; `CM1_ABE_MCBSPx_CLKCTRL[25:24]/[27:26]` is the mux.  Mainline
  `omap-mcbsp` and `clk-44xx.c` already program it.  [report 213 §4]
* **The reference's playback port is a codec-master slave.**  `R0x302 =
  0x4000` (AIF1 master), `R0x304 = 0x0040` (AIF2 slave), `R0x300 = 0xc018`
  (DSP_A), `R0x200 = 0x0011`, `R0x732 = 0x6020`; `PCR0 = 0x3` (consumer), no
  `GRST`/`FRST`; `StereoAudioCodecRequestMcbspClk` is a stub.  [report 213 §5;
  oracle `pbi2c`/`memdump3`]
* **The WM8994's AIF1 is on McBSP3.**  The oracle's slog:
  `AIF 0 Downlink: MM_DL_PORT->MM_EXT_OUT_PORT->MCBSP3`.  McBSP3
  (`0x49026000`) is the only L4_ABE target that reads during playback;
  McBSP1/2 reads `SIGBUS`.  Its pads: `0x106/0x108/0x10a/0x10c` =
  `0x0109/0x0009/0x0109/0x0109` (the PDM group in mode 1).  [capture
  `wm8994-e10-mcbsp3-2026-11-13.txt`; report 214 §1]
* **`abe_mcbsp1_dx` is a different route** (`AIF 4 Downlink: MCBSP1`), not the
  codec's DACDAT.  The E6 capture did not quote the McBSP3/PDM pads.
  [report 214 §1/§6]
* **The mcbsp1 consumer build still stalls even with input-enabled pads.**
  Image `0x47ED0450`: `mcbsp1_pins` `clkx`/`fsx` = `0x0100`, `XCR2` mono,
  `SPCR2 = 0x0235`, `XBUFFSTAT = 0`, `IRQST = 0x0010`, the write dies at
  82,176 bytes.  So the pad input buffer was necessary but not sufficient; the
  engine was wrong.  [console/dmesg]
* **The McBSP3 two-link build drains the data port.**  Clock link
  `mcbsp2 + wm8994-aif1` (`/dev/dsp`, 45 s, 8,650,752 B); data link
  `mcbsp3 + wm8994-aif2` (`/dev/adsp`, 40 s, 7,684,096 B); `XCR2 = 0x8041`
  (`XPHASE`); `SPCR2 = 0x0235`; `XBUFFSTAT` toggles; `IRQST = 0x0712`.
  [report 214 §4]
* **The left-only result was the OSS mono-first negotiation.**  The raw codec
  registers had both channels on (`PM1=0x3303`, `PM3=0x03f0`, `PM5=0x0303`,
  `0x02d/0x02e=1`, `0x601/2=1`, `0x060=0x00ee`); `XCR2` was `0x0041`
  (`XPHASE` clear).  `omap_mcbsp_dai_hw_params` returned early on the later
  2-channel call because it compared rate/width only.  Comparing `XPHASE`
  gives `0x8041`.  [report 214 §3; regmap debugfs]
* **The operator hears the 440 Hz tone loud in both ears** after the
  `XPHASE` fix, at 48 kHz.  [operator observation; the data stream
  40 s/7,684,096 B]
* **The patch re-exported and verified:** 54 files / **8,440 lines**, sha256
  `fd2774fb…`, 0 rejects, 54/54 byte-identical by apply-and-compare.
  [patch regenerated and staged]

## Inferred (reasoning from evidence, not directly measured)

* **The codec's BCLK/LRCLK net is shared between McBSP2 and McBSP3.**  The E5
  mcbsp2 output locks the codec FLL (so it is on the codec's BCLK pin), and
  the oracle's McBSP3 is a consumer of the same codec master; the working
  mcbsp3 data link proves the clock reaches it.  *Strong*, but the net itself
  was not probed.
* **The speaker path was not exercised.**  The operator's test was on
  headphones; `SPKMIX` was not tested.  The handoff says it is not routed, but
  this session did not verify that; the codec's speaker controls read routed.
  *Moderate* — carried from the handoff, not measured this session.
* **The diagnostics are safe to retire.**  The `pcm:` and `live N:` prints
  were the debugging aid; the path works.  *Strong*, but they are still in
  the patch.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The reference clocks its McBSP through the ABE, so Linux needs an ABE
  bring-up."**  Withdrawn.  The ABE serial port is a firmware data path and
  the reference's port is a codec-master slave; the working path is the
  legacy McBSP path.  [report 213; DECISIONS §58]
* **"The corrected E9 with mcbsp1 and input-enabled pads will work."**  It
  did not: the data port still stalled.  The input-pad change was right but
  the engine was wrong.  [image `0x47ED0450`, `SPCR2 = 0x0235`]
* **"The codec's DACDAT is `abe_mcbsp1_dx`."**  Wrong.  It is
  `abe_mcbsp3_dx` (pad `0x108`); `mcbsp1_dx` is the `AIF 4→MCBSP1` route.
  The E6 pad dump covered the range but the report quoted only 0xF4–0x100.
  [report 214 §1/§6; trap 204]
* **"The E9 stall proves McBSP1's clock pins are not on McBSP2's net."**  It
  proves mcbsp1 is not the codec's engine; the codec's clock net reaches
  **McBSP3**.  [report 214; trap 194 resolution]
* **"Muting the left output mixer will move the tone to the right."**  It
  left a very faint left tone and no right — the right path was data-silent.
  The codec registers showed both channels on, which pointed at `XPHASE`.
  [operator observation; regmap]
* **"`memdump3` can read the McBSP registers."**  It `SIGBUS`es on the idle
  McBSP1/2 targets; only the active McBSP3 target reads.  The codec's regmap
  debugfs is the safe codec path.  [trap 206]
* **"The kickoff's `0x5A04ACD9`/#35 is the bench state."**  The unit was on
  `0x58F85337`/#40 at the start; the kickoff line was stale.  [uname]
* **"A dummy codec can be the data link's codec DAI."**  It drags the OSS
  format negotiation to U8 and `omap-mcbsp` rejects it.  `wm8994-aif2` is the
  working stand-in.  [report 212 §14; reused]

## Unknown / open questions

* **The speaker path.**  It was not exercised (headphones only); the codec's
  controls read routed.  *Would settle it:* unplug the headphones, play the
  same tone, and diff the raw speaker registers (`R0x22`/`R0x23` SPKMIXL/R,
  `R0x26`/`R0x27` SPKLVOL/SPKRVOL) and the `spk`/`hp` lines against the
  oracle.
* **The faint left tone with the left output mixer off.**  Likely crosstalk
  or the speaker path; not investigated because the right channel was the
  real fault.  *Would settle it:* a left/right-only wav and the SPKMIX state.
* **MCLK1.**  Still dead at the pin (reports 206/208/209); the working design
  does not need it.  *Would settle it:* the trap-165 probe, optional.
* **The `pcm:`/`live N:` diagnostics** are still in the drivers; retire them
  and re-verify the path.  *Would settle it:* a module-only build with the
  prints removed.
* **Touch cold boot** still needs the GPIO1_14/GPIO4_8 pulse.
* **WL1283 / `wl128x-nvs.bin`** remain stopped/open as before.

## Superseded / uncertain

* **Report 212 §10's "the codec's DACDAT is `abe_mcbsp1_dx`"** — corrected by
  report 214: it is `abe_mcbsp3_dx`.  The E6 evidence was a partial pad quote.
* **Report 212 §16 / trap 199's "clocked through the ABE"** — corrected by
  report 213/214: the reference's port is a codec-master consumer; the ABE is
  a data path.  The consumer half stands.
* **Trap 194's "the clock nets are separate"** — resolved: the E9 stall was
  the wrong engine, and the mcbsp2 clock net reaches McBSP3.
* **Trap 192's "the data ball is `mcbsp1_dx`"** — corrected in place; the
  actual ball is `mcbsp3_dx`.
* **The kickoff's physical state `0x5A04ACD9`/#35** — stale at the session
  start (`0x58F85337`/#40); the final media is `0xF8BADFF3`/#42.
* **DECISIONS §58's "the corrected E9 with mcbsp1"** — executed as mcbsp3;
  §59 records the working design.

## Artefacts produced

* **Report 213** — `docs/reports/213-abe-feasibility-2026-11-13.md`.
* **Report 214** — `docs/reports/214-wm8994-mcbsp3-stereo-2026-11-13.md`.
* **Capture** — `docs/captures/wm8994-e10-mcbsp3-2026-11-13.txt`.
* **The ABE/HAL Addendum** — `hardware/abe-hal-addendum.txt` (PDF gitignored;
  the operator supplied the URL).
* **DTS** — `mcbsp3_pins` (PDM pads mode 1), `&mcbsp3` enabled, `&mcbsp1`
  disabled, `mcbsp2_pins` unchanged.
* **Machine driver** — the two-link card (`40124000.mcbsp + wm8994-aif1`,
  `40126000.mcbsp + wm8994-aif2`).
* **McBSP driver** — the `XPHASE` channel-layout check in the `hw_params`
  early-return guard.
* **Images** — region 0 `0x47ED0450` (mcbsp1 experiment, saved), **`0xF8BADFF3`**
  (current, kernel #42); region 1 `a726bb49…` (mcbsp3, first) and
  **`c0719d28…`** (stereo fix, current); E5 rootfs `6d92428e…` backed up as
  `local/images/rootfs-region1-audio-oss-e5.ext4`.
* **Patch** — `kernel/patches/0001-playbook-winchester-7.3.3.patch`, 54 files /
  8,440 lines, sha256 `fd2774fb…`, verified 0 rejects / 54/54.
* **DECISIONS §58/§59**, KNOWN-ISSUES traps 201–206 (and 192/194/199
  corrections), ROADMAP audio closure, STATE/HANDOFF/NUMBERING updates.
