# Session 44 (absolute) — power/CM comparison negative, the reference codec-GPIO init in the driver, and the QNX chain RE

> Numbering: absolute `44` (the project's chronological scheme — see
> `NUMBERING.md`).  This is Linux-era **session 38** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-12 (single-day session, running past local midnight).
Unit state at the end: **on our Linux image** `boot/chainloader/kernel.img`,
CRC **`0xB43F169B`** (0x93A3E0 bytes); region 1 = the audio/OSS rootfs sha256
**`3df8056c…`** (67,108,864 B); console `pb> ` on `ttyACM0`; no door;
`BOOTCTR_FCTRY` read **`0x28`** at the last boot (≥ threshold, no write).
No device-side files were deployed this session (the oracle's UFS
`/accounts/devuser/bin` tools are from earlier sessions and remain).

## Goal of this session

1. Run the session-43 handoff's one experiment: the **power-domain / CM
   comparison** between the minimal 1917 oracle and Linux, then set a differing
   domain to the reference's value and re-run the codec freeze detector
   (`R0x220 = 0x0004` means the domain was the gate; `0x0002` means
   pad/board/PMIC).
2. Fix `CM_ABE_PLL_REF_CLKSEL = SYS_CLK` in the DTS.
3. After the negative result, the operator's direction: stop treating the
   software surface as exhausted, reverse the reference's own chain
   (bootloader → IFS → drivers), and test by implementing changes in the
   driver, rebuilding and flashing — not ad-hoc pokes.

## Confirmed (measured, read in a file, or seen on the bench)

* **The power-domain state matches in every field that is a state.**  All
  `POWERSTATEST`/`LOGICSTATEST`/memory-bank fields agree; the only high-half
  difference is `PM_*_PWSTST[25:24] = LASTPOWERSTATEENTERED`, a W1toSet debug
  latch (TRM Table 3-416).  `PM_ABE_PWSTST` reads 0 on both, idle and streaming.
  [report 209 §1–§3; `docs/captures/wm8994-pm-cm-2026-11-12.txt`]
* **CM differences are policy, in domains that do not own the pad.**
  `CM2_CORE_CLKSTCTRL` — the pad/SCRM domain — is identical (`0x00000103`) on
  both.  The reference forces `SW_WKUP` on L4PER/L3INIT and `SW_SLEEP` on ABE;
  Linux uses `HW_AUTO`.  [report 209 §4]
* **Causal test on the working oracle: negative.**  With `io-audio` running and
  FLL1 locked (`R0x732 = 0x6020`), the freeze detector lands (`R0x220` reads
  `0x0004`).  Setting every differing CM policy to Linux's value left it
  landing.  So the domain is **not** the gate.  [report 209 §5; capture §4]
* **The complementary Linux test is negative**, and the kernel re-asserts its
  own `CLKTRCTRL` policy after a `devmem` poke (only `CM2_L4SEC` stuck).
  [report 209 §6; capture §5; trap 183]
* **`CM_ABE_PLL_REF_CLKSEL` is forced by the kernel's own clock init**
  (`drivers/clk/ti/clk-44xx.c` sets `sys_32k_ck` and 98.304 MHz), not left by
  the bootloader.  Reparenting the mux alone leaves M/N for 32 kHz and DPLL_ABE
  fails to lock (7 × `failed transition to 'locked'`).  Parent **plus**
  `assigned-clock-rates = <98304000>` locks it: `CM_ABE_PLL_REF_CLKSEL = 0`,
  `ST_DPLL_CLK = 1`, `CM_CLKSEL_DPLL_ABE = 0x00804018` (M=64, N=24 →
  98.304 MHz), **zero** failures; survives a warm reboot.  [report 209 §7;
  capture §6]
* **The board's complete pad database is extracted.**  `hwio-config-winchester.so`
  holds a 249-record, 20-byte pad table at file `0x604`; `hwio-omap4.so` holds
  the `u32[0xE7]` bank/offset/mask resolver at `0x4FF8`/`0x5394`/`0x5730`.
  Validated against known pads (`0xB7 → 0x4A10019C` low, `0x94/0x95 →
  0x4A100148` low/high).  [report 210 §1; `docs/reference/qnx-hwio/`]
* **The reference audio driver's `codec_gpio_init` is decoded.**  It opens
  `/dev/i2c`, then drives three hwio signals: `0x92` "ldo" (`hdq_sio` /
  gpio_127) low → `delay(100)` → high; `0x93` "spk" (`gpmc_a20` / gpio_44)
  high; `0x94` "hp" (`gpmc_a25` / gpio_49) input; then creates the
  `audio_boost` powerman handle, maps SCRM `AUXCLK1`, and runs
  `StereoAudioCodecInit`.  [report 210 §3]
* **The sequence is implemented in the machine driver and tested.**
  `audio-ldo` is a boot-on `regulator-fixed` on GPIO4_31, force-disabled for
  100 ms then re-enabled at probe; `hp` is requested as input; `spk` remains
  high via the gpio2 hog.  Image `0xB43F169B`.  After `pbplay`, the driver
  reports **`repair: done, FLL1 NOT locked`** — so this is **not** the MCLK1
  gate.  [report 211 §1–§2]
* **The implementation fixed a real, separate fault.**  On the two builds
  where the ldo line was left as an input until module load, `wm8994 3-001a`
  failed with `Failed to read ID register` (-121) and no card registered; with
  the boot-on regulator it probes every boot and `0 [WM8994]` is up.
  [report 211 §2; trap 186]
* **Linux gpio numbers are assigned by probe order, not by OMAP bank.**  Bases:
  GPIO2=512, GPIO3=544, GPIO4=576, GPIO5=608, GPIO6=640, GPIO1=676.  So
  `gpio_127` = 607 (`&gpio4 31`), `gpio_44` = 524, `gpio_49` = 529.  [report
  211 §3; trap 185]
* **The kernel patch was re-exported and verified:** 54 files / **8 214
  lines**, sha256 `90391b96…`, 0 rejects / 54/54 byte-identical.  [report 211;
  `kernel/patches/README.md`]
* **No boot regressions:** codec revision D, McBSP2, the sound card, the touch
  (`irq 110`), the display and the ACM gadget all come up on the final image.

## Inferred (reasoning from evidence, not directly measured)

* **The MCLK1 fault is below the readable registers.**  *Strong.*  The last
  readable hypothesis (power/CM) is causally excluded; every readable SCRM/pad/
  CM/PRM value matches the reference; the reference's board-enable sequence,
  implemented in the driver, changes nothing.  What remains is the pad output
  path, the board clock net, or a PMIC/analogue condition (trap 165).
* **The reference's codec-GPIO sequence is not the MCLK1 gate.**  *Moderate to
  strong.*  The driver-level test is the right one, but the pin's electrical
  state is not observable (OE reads 0 and pad INPUTENABLE does not stick), so
  "the pulse did not happen" cannot yet be fully excluded.
* **The ldo line is needed for the WM8994 to answer I2C.**  *Moderate* (two
  failed probes without it, reliable with it), but a small sample and a warm
  reset does not reset the codec; a cold-boot confirmation would strengthen it.
* **The QNX-side RE has not been exhausted.**  *Strong.*  The pad database and
  the driver init are only the first two artifacts; the AUXCLK1 write site and
  `MixerInitAvp` are still unread (report 210 §5).

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The power domain is the gate."**  Killed by the oracle causal test: with
  every Linux CM policy applied, the detector still lands.
* **"Reparenting DPLL_ABE to SYS_CLK is enough."**  Killed by seven
  `dpll_abe_ck failed transition to 'locked'`; the M/N must be recomputed with
  an assigned rate.
* **"A pinctrl group for the audio pads."**  Killed by
  `pin PIN20 already requested by 48055000.gpio` — the gpio2 hog owns pad
  0x068; pinctrl-single pins are exclusive.  The pads are already muxed at
  boot, so no group is needed.
* **"`regulator_disable()` pulses a boot-on regulator."**  Killed by
  `unbalanced disables for audio-ldo` and `-EIO`; use
  `regulator_force_disable()`.
* **"sysfs line 639 is gpio_127."**  Wrong: Linux numbers by probe order, so
  639 is GPIO5_31.  The early LDO/SPK/HP replay drove GPIO5_31 / GPIO3_12 /
  GPIO3_17 — the wrong pins.  [report 211 §3; trap 185]
* **"Direct gpiod output gives the codec its ldo high early enough."**  Killed
  by two boots of `Failed to read ID register` (-121) when the line was only
  requested at module load; the boot-on regulator (late_initcall) fixed it.
* **"The codec probe failure was caused by the pinctrl error."**  No: the codec
  probes before the sound device, and the failure tracked the ldo line, not the
  pinctrl group.
* **"The kernel writes `CM_ABE_PLL_REF_CLKSEL` because the bootloader left it
  there."**  No: `clk-44xx.c` forces SYS_32K deliberately.

## Unknown / open questions

* **Why is AUXCLK1's output absent under Linux with identical readable state?**
  *Would settle it:* a probe on `fref_clk2_out`/MCLK1 (trap 165), or a
  boardview/level-shifter fact.
* **Where does the reference driver write AUXCLK1, and in what order relative
  to the codec reset?**  *Would settle it:* decompile `MixerInitAvp` /
  `FUN_0002a33c` / `FUN_000296c4` and locate the store through the mapped
  pointer (report 210 §5).
* **Does the ldo line actually toggle?**  *Would settle it:* a driver-level
  readback of the pin, or a probe; GPIO4_OE reads 0 and the pad input buffer
  cannot be enabled, so software cannot see it today.
* **Could the WL1283 negative results have been affected by the gpio-number
  confusion?**  *Would settle it:* audit every WL1283 experiment that used
  sysfs/global gpio numbers (rather than DT/OF phandles) against trap 185.  The
  DTS/OF-based paths are unaffected; the devmem pad writes are not.
* **Is MCLK1 dead or marginal?**  *Would settle it:* the probe, or repeated
  direct tests across boots (session 41's single lock keeps "marginal" alive).
* **Is the ABE DPLL meant to run at 98.304 MHz from SYS_CLK?**  It now locks;
  the reference's exact M/N were not captured.  *Would settle it:* read
  `CM_CLKSEL_DPLL_ABE` on the oracle.

## Superseded / uncertain

* **"`CM_ABE_PLL_REF_CLKSEL = SYS_32K` is a bug / bootloader leftover."**  It is
  upstream's deliberate choice; the register difference is real but is not the
  MCLK1 cause, and fixing it needs parent + rate.  Report 209 §7 and correction
  3.26 are current.
* **"The sysfs LDO/SPK/HP replay is a negative result."**  It is not evidence:
  it drove the wrong pins and used the wrong method (DECISIONS §56).  Report 210
  §4 carries the correction; report 211 is the driver-level test.
* **Report 209's "the domain is not the gate"** stands, and is now the current
  statement.

## Artefacts produced

* **Report 209** — `docs/reports/209-power-domain-cm-comparison-2026-11-12.md`.
* **Report 210** — `docs/reports/210-qnx-audio-chain-re-2026-11-12.md`.
* **Report 211** — `docs/reports/211-codec-gpio-init-in-driver-2026-11-12.md`.
* **Capture** — `docs/captures/wm8994-pm-cm-2026-11-12.txt`.
* **Reference extraction** — `docs/reference/qnx-hwio/README.md` and
  `pad-table-winchester.csv` (249 records).
* **Board DTS** — `&l4_abe` ABE-refclk assignment; `audio-ldo` regulator; sound
  `ldo-supply`/`hp-gpios`; `ref-hog-31` removed.
* **Machine driver** — `playbook_wm8994_board_enables()` in
  `sound/soc/ti/playbook-wm8994.c`.
* **Patch** — `kernel/patches/0001-playbook-winchester-7.3.3.patch`, 54 files /
  8 214 lines, sha256 `90391b96…`, 54/54 byte-identical.
* **Images** — region 0 `0xB43F169B` (0x93A3E0, current), `0x7C56BDC0` (ABE
  fix), `0x5E97FEBE` (parent-only, not kept); region 1 `3df8056c…`
  (67,108,864 B).
