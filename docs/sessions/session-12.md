# Session 12 (absolute 12, Linux-era "session 6") — backlight solved, SPI/TCON built, DSI matched to the reference

> Numbering: the repository numbers all twelve sessions chronologically
> (`session-01` … `session-12`).  The Linux bring-up sessions are called
> "session 1…6" in the reports and handoffs, so **this note is absolute session 12
> = Linux session 6**.  See `NUMBERING.md`.

Span: 2026-10-27 → 2026-11-15 (this session was long and stayed open across a host
reboot and a cold battery pull).
Unit state at the end: TabletOS from the sealed IFS (sha256 `c72b0530…` verified),
door up, `BOOTCTR_FCTRY = 0x32`.  Best test image: `chainloader/kernel.img`
crc `0xE1F79294` (built, not on media).

## Goal of this session

Make the panel show an image under Linux.  The display video path already worked
at the start of the session (omapdrm, `/dev/dri`, `/dev/fb0`, DISPC scanning, DSI
PLL locked), but the panel was dark.

## Confirmed

**Backlight — the LM3532 was never dead, it was disconnected.**
* The LM3532 at I²C 0x38 is gated by **GPIO2_16** = OMAP `gpio_48`, ball
  `gpmc_a24`, `CONTROL_CORE_PAD0_G 0x4A100070[15:0]`.  Causal proof: line low ⇒
  `0x38` NAKs; high ⇒ ACKs; low again ⇒ NAK (report 120 §0.3).
* Our boot path **resets GPIO2** (`OE=0xffffffff`, `DATAOUT=0`) and leaves that
  pad in **safe mode (mux 7)**; the reference has mux 3 and drives it high
  (report 120, report 118 §10.1).  **GPIO4 is not reset** by our boot path.
* A `regulator-fixed` GPIO loses the race with async probe (same image bound
  `lm3532-led` on one boot and not the next); a **gpio-hog** is deterministic
  (report 120 §1.1).
* `bl_power = 4` (FB_BLANK_POWERDOWN) silently swallows brightness writes;
  `actual_brightness` still echoes your value.  The panel's
  `backlight = <&backlight>` link makes `drm_panel` unblank it (report 120 §1.2).

**Reference rails and GPIO maps (measured live, read-only).**
* All 20 rails were swept with `pmic-twl6030-ctl get_state/get_vout`; they match
  the `pmic-twl6030.cfg` states and Linux's defaults (report 120 §0.2).
* The reference drives seven GPIO2 lines high (`DATAOUT = 0x31013200`: bits 9,
  12, 13, 16, 24, 28, 29) — report 118 §10.1 missed **bit 24**
  (`gpio_56`, pad `0x080`).

**TC358764.**
* Its control port is **MIPI DSI generic packets** (16-bit address + 32-bit
  data), *not* I²C (relay, report 121 §9).
* Its **RESETN is GPIO2_24** (`gpio_56`, pad `0x080`), driven by `winch_lcdctl` as
  a **20 ms active-low pulse** (`CLEARDATAOUT 0x01000000` at vaddr `0x192d4`,
  `usleep(20000)`, `SETDATAOUT` at `0x19338`) — report 122.
* Nothing in the QNX boot set programs it: the IFS startup image (which our
  chainloader replaces) references only `0x48242000`, `0x4A307B00`, `0x4A318000`;
  `winch_lcdctl` has no bridge code; the second-stage eMMC loader has no display
  constants (report 122, report 121 §11).
* After our reset pulse the bridge is **alive**: `GPIO2_DATAIN = 0x01000010`,
  bit 4 = `GPIO36`, the bridge's own status ball — exactly the condition QNX's
  own self-test requires (report 121 §7.6).

**DSI — the link was wrong, and it was the lane count.**
* The reference runs **four** DSI data lanes: `DSI_COMPLEXIO_CFG1 = 0x2A054321`
  (CLK=1 DATA1=2 DATA2=3 DATA3=4 DATA4=5) vs `0x2A000321` for a two-lane build.
* The arithmetic closes exactly: 1024×600×24 bpp at 60 Hz = 1.2096 Gbps ÷ 4 lanes
  = 302.4 Mbps/lane = **37.8 MHz byte clock** = QNX's `TL 1008`
  (1008×625×60).  Two lanes force 75.6 MHz, which is what mainline computed.
* With `lanes = <0 1 2 3 4 5 6 7 8 9>` (5 pairs) and `lanes = <4>` on the bridge:
  `COMPLEXIO_CFG1 = 0x2A054321`, `VM_TIMING3 = 0x03F00258`, PLL `m=126 n=16 mX=4`
  → clkdco 604.8 MHz, txbyteclkhs 37.8 MHz (all matching QNX, report 121 §11).
* `DSI_CTRL` differed in exactly two bits, decoded from TRM Table 10-530:
  `CS_RX_EN` (bit 1, RX-only) and `VP_CLK_POL` (bit 8).  Matching the reference
  gives `0x00EAEF9D` (report 121 §11.2).
* DSI health after that: `IRQSTATUS = 0x80` (PLL lock), `COMPLEXIO_IRQ_STATUS = 0`,
  `VC0 IRQSTATUS = 0x4` (PACKET_SENT).

**Reference firmware probe.**
* The instrumented boot image contains a **ramloader** (`loader_06001A06-00.bin`)
  that is uploaded over USB by host tools; it is never on media and never in the
  boot chain (relay + `pre_os_ifs_flash.py` behaviour).

## Inferred

* The panel is black because the **timing-field distribution** differs, not the
  link: totals agree (1344×625 ⇒ 50.4 MHz), but `DISPC_TIMING_H` is 137/180/0 vs
  QNX 137/161/19, `DIVISOR` 3 vs 2, `DSI_VM_TIMING1` 103/134/0 vs 116/121/1, and
  `DSI_CLK_CTRL` LP divisor 8 vs 5.  Strength: moderate — nothing measured proves
  a TCON cares, but nothing else is left.  (report 121 §12.2)
* The reference display works from the bridge's power-on/strapped state, so the
  bridge needs no programming from us (relay; consistent with the IFS splash
  putting the logo up before the RFS mounts, report 111 §8 item 4 as corrected).

## Dead ends and wrong assumptions (the expensive part)

* "The TWL LED boost powers the LM3532" — the reference writes `0x49[0x92]←0x30`
  ("PMIC PWM initialization"), but `0x92` reads **0x00 on the running reference
  too** and the write does not stick.  Dead (report 120 §0.2).
* "The AUX rails are the difference" — all three are at 2.8 V on both systems.
  Dead.  (And `set_state vaux1 off` on the reference **takes the whole unit down**;
  it needed a power cycle.  Never do that.)
* "REGEN1/GPIO1_2 is the backlight enable" — toggling it on the reference leaves
  `0x38` ACKing.  Dead (report 120 §0.2).
* "The reference GPIO4 set matters" — GPIO4 is *not* reset by our boot path, so
  those four lines were already high; the "tests" were no-ops (report 120 §0).
* "Match the DSI link rate by forcing the QNX PLL words" — the 2× was a
  lane-count artefact, not an encoding choice; forcing the PLL would have hidden
  the real bug.
* "The bridge must be programmed" — we wrote a full `SYS_RST <- 0x1f` and the
  upstream init table; it poisoned the **reference** display (faint vertical
  artifacts) until a cold battery pull.  Retired.
* "Non-burst video mode" — omapdrm rejects the mode outright
  (`Cannot find any crtc or sizes`), no `/dev/fb0`, DSS in standby.
* Two flash "slowness" mysteries were our own bugs, not hardware: a flash started
  while the unit sat in the ramloader (console reboot silently fails), and
  `--reuse-loader` failing with `set_mode(2)` and leaving the bootrom blinking red.

## Unknown / open questions

* Does this TCON/panel care about the DISPC/DSI *field distribution* if the totals
  match?  Resolve by forcing the four registers as one set (report 121 §12.2).
* Are the `DSI_PHY_CFG*` DDR/HS-LP values (mainline computes its own; QNX's are in
  report 118 §3.3) part of the remaining difference?  Compare after the timing set.
* Does the panel want a different LVDS mapping than the driver's VESA-24 `LV_MX`
  table?  We cannot change it, because the bridge cannot be programmed.
* Why does `write /sys/class/drm/card0-LVDS-1/dpms` return EACCES on this kernel,
  while the backlight sysfs writes work?  (Workaround: `unbind`/`bind` the bridge
  driver to stop the stream.)

## Superseded / uncertain

* My earlier statement in this session (Linux-era "session 6", i.e. absolute 12 —
  this note) that "the panel is dark because the LM3532 is
  unpowered / the TWL PWM block is disabled" is **superseded** by report 120.
* Report 118 §8.2's "the 2× divergence is benign, mainline redistributes blanking"
  is **wrong** and is corrected in place by report 121 §11
  (`docs/contradictions/118-vs-121-dsi-link-rate.md`).
* Report 111 §8 item 4 ("the RIM bootloader must configure the bridge") is
  **superseded**: nothing configures it, and it needs no configuration
  (`docs/contradictions/111-vs-122-bridge-programming.md`).
* An early belief that `gpiochip512` (GPIO1) did not exist was a truncated `ls`,
  not a fact.

## Artefacts produced

* `docs/reports/120-lm3532-panel-enable-2026-10-27.md` — backlight solved, plus
  the QNX-side I²C oracle.
* `docs/reports/121-spi-tcon-panel-2026-10-27.md` — MCSPI4/TCON, the GPIO maps,
  the four-lane discovery (§11), `DSI_CTRL`, ordering/dpms results (§12).
* `docs/reports/122-re-bridge-reset-2026-10-27.md` — the bridge reset/sweep.
* `tools/qnx-i2c.c` (+ built `pbi2c`) — QNX-side I²C probe: `probe/rd/wr/scan/dump`.
* `initramfs/console.c` — new commands `spi tcon`, `spi w`, `fb`, `kmsg`, `i2c16`.
* Board DTS — 4 DSI lanes, bridge `lanes = <4>` + `reset-gpios = <&gpio2 24>`,
  GPIO1/GPIO2 hogs, MCSPI4 + spidev + `spi3` alias, panel `backlight` link.
* `tc358764.c` — 20 ms reset pulse, init reduced to a log (never programs the
  bridge), DT lanes/rates.
* `dsi.c` — one board quirk: `CS_RX_EN = 0`, `VP_CLK_POL = 1`.
* `bench/device-startup.sh` — the fixed TabletOS BOOTCTR top-up (executes `dd` to
  test readiness, logs the record's previous value).
* Images built this session: `0xD90E0213` … `0xE1F79294` (see
  `docs/project/STATE.md`).
