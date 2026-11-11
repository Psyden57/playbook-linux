# Session 39 (absolute) — WM8994 playback: machine driver, stream, DAPM; FLL chip-side

> Numbering: absolute `39`; the reports call this Linux-era **session 33**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-11.
Unit state at the end: **on our Linux image `0xF97CD9C7`** in region 0
(`0x9396B4`, kernel `7.3.3`; touch + ABE + the audio `sound` node and codec
clock), region 1 = the audio/OSS rootfs `968d2f20…`
(`local/images/rootfs-region1-audio-oss.ext4`, 27 modules + `pbplay`/`pbmix`).
`BOOTCTR_FCTRY` self-replenishing; the unit is left **on our image**.
The 1917 IFS (`local/work/ifs-shim-sealed.ifs`, block `0x825`) is the way back.

## Goal of this session

The operator chose the WM8994 **playback** test (item 9 step 5's audio half):
the card was up from session 38, and the open question was whether the DAI
streams and whether the codec's clocking works.

## Confirmed (measured, or seen on the bench)

### The machine driver and the stream

* **A board machine driver replaces simple-audio-card.**
  `sound/soc/ti/playbook-wm8994.c`: static link
  `40124000.mcbsp` ↔ `wm8994-aif1`, I2S, CPU bit/frame provider; `hw_params`
  gives the McBSP its `12.288 MHz` fclk and `CLKGDV` (BCLK = 32 fs), moves
  `AIF1CLK` to MCLK1, configures `FLL1` from BCLK, then switches `AIF1CLK` to
  `FLL1` at 256 fs.  Board DAPM: `Headphone`/`Speaker` ← `HPOUT1*`/`SPKOUT*`.
* **The card registers** `0 [WM8994]: PlayBook_WM8994`; `/proc/asound/pcm`
  has `PlayBook WM8994 wm8994-aif1-0 playback 1 : capture 1`.
* **The OSS stream works.**  `pbplay` (new, static) opens `/dev/dsp`
  (S16_LE, 2ch, 48 kHz): `SETFMT -> 16`, `CHANNELS -> 2`, `SPEED -> 48000`,
  a 1.9 MB write completes, exit 0.  `snd-mixer-oss`/`snd-pcm-oss` are loaded
  by `rcS`.
* **The DAPM path is complete in the driver's view.**  With the output mixers
  set (the new `pbmix` tool, in `rcS`): the regmap reports `PM4=0x0300`,
  `PM5=0x0303`; `AIF1CLK_ENA=1` was read **directly from the chip**
  (`i2ctransfer` of `0x200`); the DAPM files show
  `DAC1L`/`SPKL`/`HPOUT1L`/`SPKOUTLP` = `On`; and the mixer write
  `0x0601 = 1` was confirmed on the chip.  (The regmap cache can diverge from
  the chip — see the write blocker below.)
* **A second `hw_params` no longer fails.**  The WM8994 refuses to reconfigure
  FLL1 while it is the active SYSCLK (`FLL1 is currently providing SYSCLK`,
  `-EBUSY`); moving `AIF1CLK` to MCLK1 first fixes the OSS
  `SETFMT→CHANNELS→SPEED` reconfiguration.

### The clock: MCLK1 is dead, BCLK works

* **The 1917 FLL1 scheme is MCLK1-based.**  Decoded from
  `deva-ctrl-omap4abe-wm8994.so` (`StereoAudioCodecSelectOutputPath`):
  `REFCLK_SRC=MCLK1`, `REFCLK_DIV=0`, `FRATIO=0`, `OUTDIV=7` (÷8), `N=7`,
  `K=0xAE14` → `FOUT = 12.288 MHz` from a **12.8 MHz** reference.
* **The SoC side is configured:** `AUXCLK1 (0x4A30A314) = 0x00020100`
  (SYS_CLK/3 = 12.8 MHz, enabled; consumer `3-001a MCLK1`), and CORE pad
  `0x19c` = `0x0000` (mux 0 = `fref_clk2_out`; TRM: `fref_clk<N>_out` is
  driven by `AUXCLK<N-1>`).
* **The FLL still does not lock from MCLK1** (`0x732` bit 5 = 0).  Relocking
  it to the incoming **BCLK** (`0x224 = 0x0c83`, `N=8`) while a stream ran set
  `0x732 = 0x6020` — **locked**.  So the FLL and BCLK are good and the MCLK1
  path to the codec is not.

### The blocker: the chip stops accepting register writes

* **Cache vs hardware:** during a stream the driver's cache reads
  `0224: 0c83`, `0220: 0001`; the chip reads `0x0c80`, `0x0000` (reset
  values).  `cache_only=N`, `cache_dirty=N`, the MFD and codec are
  runtime-`active` (`power/control=on`, also after a `pm_runtime_forbid`
  quirk), and a mixer value the driver wrote at boot (`0x0601 = 1`) is
  present.
* **Direct writes are ignored:** `i2ctransfer` writes to `0x0036`, `0x0601`,
  `0x0220`, `0x0224` return success and do not change the register; reads
  still work (ID `0x8994`).  The FLL configuration is therefore lost, and the
  stream runs at the OSS default 8 kHz with `AIF1CLK` from the dead MCLK1.
* **dmesg has no hidden error:** the only lines are the known negative
  WL1283 MMC5, the benign "unable to open an initial console", and the four
  `Mic detect IRQ: -22` jack-detect requests.

## Inferred

* **The MCLK1 pin is not clocked on this unit**, or the codec's MCLK1 input is
  not connected to `fref_clk2_out`.  *Strength: strong (the FLL locks from
  BCLK but not MCLK1; AUXCLK1 and the pad are configured).*
* **The chip enters a write-ignoring state after boot.**  The same writes
  (the `rcS` mixers, DAPM) landed earlier in the boot, so the interface is not
  broken; something (the DAPM `BIAS_OFF` cycle is the prime suspect) makes the
  register file ignore later writes.  *Strength: strong (measured; mechanism
  unknown).*
* **BCLK is a valid FLL reference** (the datasheet lists MCLK1/2, BCLK,
  LRCLK).  *Strength: measured (lock) + datasheet.*

## Dead ends and wrong assumptions

* **"simple-audio-card with `mclk-fs` can clock the WM8994."**  It cannot:
  `mclk-fs` calls `set_sysclk(codec, 0, …)` and `WM8994_SYSCLK_MCLK1 = 1`, so
  it returns `-EINVAL`; and it cannot configure an FLL anyway.
* **"Set MCLK1 and the FLL follows."**  The FLL registers were written and
  read back correctly on an early boot, but the MCLK1 lock never came.
* **"The chip is cache-only."**  `cache_only` was `N`; a `cache_bypass` write
  and plain `snd_soc_component_write` writes still did not stick.
* **"`pm_runtime_forbid` fixes it."**  It keeps the codec active but does not
  restore writes.
* **"The console's `pbmix list` output is complete."**  The ACM link drops
  lines; a filtered `pbmix list | grep` is the reliable form.

## Unknown / open questions

* **What makes the chip ignore writes?**  *Would settle it:* a register write
  at the first prompt (before `pbplay`) and again after one stream, then a
  bisect of `rcS`/DAPM.  Also read `WM8994_WRITE_SEQUENCER_CTRL_1/2`
  (`0x110`/`0x111`) — the mainline driver does not start the sequencer, but a
  stuck sequence owns the register map and would look exactly like this.  The
  QNX comparison below is the stronger form.
* **What do the codec registers look like on the working QNX stack?**  *Would
  settle it:* on live TabletOS, stop `io-audio` (do not race the vendor driver
  — trap 103) and read `0x200`, `0x220`–`0x224`, `0x732`, PM4/5 with `pbi2c`.
  That shows the reference's FLL source/config and whether its MCLK1 lock is
  real on this same board, and whether the register writes survive.
* **Does the 1917 driver write AUXCLK1 at all?**  *Would settle it:* decompile
  the `0x1ecf0` map/unmap region in `deva-ctrl-omap4abe-wm8994.so` with
  Ghidra, and read the `startup-playbook` call site that writes
  `AUXCLK1 = 0x00020100` (`0x7980`) to see when it runs.  If QNX also leaves
  AUXCLK1 as a map-only check, the 12.8 MHz source is elsewhere.
* **Where is the codec's MCLK1 pin actually wired?**  *Would settle it:* a
  board view/schematic, or the QNX read above (if QNX's FLL locks from MCLK1,
  the pin is wired and our pad/source is wrong; if it does not, QNX is using a
  different clock or the FLL is not the AIF clock source there).
* **Does configuring the FLL from `.trigger(START)` (after DAPM power-up)
  work?**  *Would settle it:* move the `set_pll`/`set_sysclk` there; it is the
  first hook after the DAPM stream event.
* **Which rail is SPKVDD?**  The supplies are still provisional
  (`v1v8`/`v2v1`); the speaker rail is unidentified.

## Superseded / uncertain

* **"The WM8994 card is a `simple-audio-card`."**  Superseded by this session:
  the card is now `0 [WM8994]: PlayBook_WM8994`, a board machine driver.
  Report 199's `simple-card - PlayBook WM8994` describes the session-38 state.
* **"The FLL must be clocked from MCLK1 (the 1917 scheme)."**  The 1917
  *programming* is MCLK1-based, but on this unit FLL1 never locks from MCLK1;
  it locks from BCLK.  Report 200 is the current statement.
* **"The 48-file patch reproduces the tree."**  It reproduced the files it
  listed, but it omitted `drivers/input/touchscreen/{Kconfig,Makefile}`; the
  53-file export (sha256 `434d06c3…`) is current (trap 154).

## Artefacts produced

* `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` (+ `kernel/source/`),
  `sound/soc/ti/{Kconfig,Makefile}`, and the `drivers/mfd/wm8994-core.c` board
  quirk.
* Board DTS: the `blackberry,playbook-wm8994` `sound` node, `codec_mclk_pins`
  (`0x19c`), the codec `clocks`/`clock-names`.
* `rootfs/pbplay.c`, `rootfs/pbmix.c`, `rootfs/etc/init.d/rcS` mixer setup,
  `tools/build_rootfs.py` builds both tools.
* Region-0 image **`0xF97CD9C7`** (`0x9396B4`); region-1 rootfs
  **`968d2f20…`**; `local/work/modules-7.3.3` now has 27 modules
  (`snd-pcm-oss`, `snd-mixer-oss`, `snd-soc-playbook-wm8994`).
* Exported patch: `kernel/patches/0001-playbook-winchester-7.3.3.patch` —
  **7 773 lines / 53 files**, sha256 `434d06c3…`, 0 rejects / 53/53
  byte-identical against pristine 7.3.3.  (The previous 48-file export had
  silently omitted `drivers/input/touchscreen/{Kconfig,Makefile}` — trap 106
  class; both are now in.)
* Report **200**; DECISIONS §49; traps 150–153.
* Docs: `STATE.md`, `HANDOFF.md`, `ROADMAP.md`, `KNOWN-ISSUES.md`, this note.
