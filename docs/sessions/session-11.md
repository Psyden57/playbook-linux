# Session 11 (absolute 11, Linux-era "session 5") — display mode measured live from the reference; omapdrm brought up; the userspace-prover dead end

> Numbering: this is absolute session **11** of 12. In the reports and handoffs it
> is called **"session 5"** (`HANDOFF-2026-10-27-session5.md` was written by
> session 10 for this session; `HANDOFF-2026-10-27-session6.md` is this session's
> output). See `NUMBERING.md`.

Date: 2026-10-27.
Unit state at the end: TabletOS from the sealed IFS (sha256 `c72b0530…` verified),
door up, `BOOTCTR_FCTRY = 0x32`. Last image built and booted:
`chainloader/kernel.img` crc **`0xCEB270AC`** (VAUX1/2/3 at 2.8 V, fork-safe
`i2c`/`bl scan` console); media restored afterwards.

## Goal of this session

Read the handoff, then do the display work: 111's R1 (the H/V↔register assignment
and the DSI PLL/PHY values) plus R4/R5 (DSS clocking and register readback). "Done"
meant the numbers were measured rather than inferred — not necessarily a lit panel.

## Confirmed

**The reference's whole display mode, read live, with zero flashes (report 118).**
* QNX had the display powered *down*: `PM_DSS_PWRSTCTRL = 0x00030000`
  (`POWERSTATE = 0`), `CM_DSS_DSS_CLKCTRL = 0x00070000` (`MODULEMODE = 0`),
  `IDLEST = 3`. In that state DISPC/DSI reads SIGBUS (§1).
* The working order is power domain → clock domain → module:
  `PM_DSS_PWRSTCTRL = 3`, `CM_DSS_CLKSTCTRL = 2`,
  `CM_DSS_DSS_CLKCTRL = 0xF02`; `IDLEST` then reads 0 and the readback is
  `0x00040F02` — the same value the bootloader hands Linux (§1).
* Writing `MODULEMODE = 2` alone does nothing while the domain is off: the write
  latches, `IDLEST` stays 3, and every access aborts. This is the answer to
  111/112's "the DSI PHY page aborts" — a power domain, not a firewall (§1).
* `winch_lcdctl` does **not** enable its own clocks. Run with the domain off it
  dies with SIGBUS at `_btext+0xde1c`; with the domain on the identical binary
  runs to completion (§1).
* A QNX-side register *write* tool was needed for that, so a QNX ARM build of the
  workspace's `memw32` (`memw32.c`, "single 32-bit physical register write
  (PlayBook kexec work)") was uploaded to `/accounts/devuser/memw32`. It was not
  there before. PRCM/clock pokes only — never RPMB/eMMC (§1). **This is new
  device state that later sessions inherit.**
* The DSS *top* target is sparse: `0x48040000` (`REVISION`), `+0x10`
  (`SYSCONFIG`), `+0x14`, `+0x40` (`DSS_CONTROL`) read; `+0x20`, `+0x50`, `+0x60`,
  `+0x70` abort. Not evidence of a firewall (§2).
* Timings, measured and decoded with mainline's exact encoding
  (`_dispc_mgr_set_lcd_timings()`): `TIMING_H = 0x0890A113` ⇒ HSW 19 / HFP 161 /
  HBP 137 (i.e. actual **20 / 162 / 138**); `TIMING_V = 0x00C00C00` ⇒ VSW 0 /
  VFP 12 / VBP 12 (actual **1 / 12 / 12**). Htotal **1344**, Vtotal **625**,
  **50.400 MHz** at 60 Hz (§2.1).
* `DISPC_POL_FREQ = 0x00030000`: ONOFF=1, RF(sync posedge)=1, everything else 0 —
  HSYNC/VSYNC/DE **not inverted**, pixel data on the **positive** edge. 111
  §3.4's "all syncs active-low" was wrong (§2.2).
* `DISPC_CONTROL = 0x02000309`: `LCDENABLE = 1`, `TVENABLE = 0`, `GOTV = 0`,
  `STNTFT = 1` (TFT), `TFTDATALINES = 3` (24-bit); `DISPC_SIZE_DIGIT = 0`.
  ⇒ **DSI1 is fed by the LCD manager**, not the TV/DIGITAL manager; 111 §3.1's
  mask-0x42 reading is an unused TV path (§2.3).
* `DISPC_DIVISOR = 0x00010002`, `SIZE_LCD = 0x025703FF` (1024x600),
  `GFX_BA0 = 0xAFEF0000`, `GFX_SIZE = 0x003F01ED` (494x64) at
  `GFX_POSITION = 0x010C0109` ⇒ a 494x64 image centred at **(265,268)** in
  1024x600. That is the IFS splash logo, and this capture is it (§2.1, §4).
* DSI PLL: `CONFIGURATION1 = 0x14A5E8BE` ⇒ `REGM = 756`, `REGN = 95`,
  `M4 = M5 = 6`; `CONFIGURATION2 = 0x00056000`; `STATUS = 0x383` (locked).
  On a 38.4 MHz reference that is `CLKDCOLDO = 604.8 MHz` (§3.2). The PLL
  `CONFIGURATION1` word is built **entirely** from record bytes
  `rec[0x74/0x78/0x7c/0x80]` — the 240-byte mode record *is* the PLL table (§6.3).
* DSI PHY words were captured for the first time (they abort while the PLL is
  off): `CFG0 = 0x0B190D13`, `CFG1 = 0x42040A22`, `CFG2 = 0xB8800009`,
  `CFG3 = CFG4 = 0x625D21A0`, `CFG5 = 0xFF000000`, `CFG6 = 0x00010000`,
  `CFG8 = 0x00000410`, `CFG9 = 0x00020001`, `CFG11 = 0x7D20A59A`,
  `CFG12 = 0x04320000`, `CFG14 = 0x3E080000`, `CFG15 = 0x01400003` (§3.3).
* DSI VM timings: `VM1 = 0x01079074` ⇒ HBP 116, HFP 121, HSA 1;
  `VM2 = 0x04010C0C` ⇒ VBP 12, VFP 12, VSA 1, WINDOW_SYNC 4;
  `VM3 = 0x03F00258` ⇒ VACT 600, **TL 1008** (§3.1). The VM1 HBP/HFP values are
  mode-record `+0x5c`/`+0x54` — two fields 111 listed as "unknown".

**The configuration is canonical, not one tool's quirk (§6.1).** After the panel
was woken (see below), `DISPC_TIMING_H/V`, `POL_FREQ`, `DIVISOR`, `SIZE_LCD`,
`PLL_CONFIGURATION1/2`, `VM_TIMING1/2/3` and `PHY_CFG0/1` all read **identically**
to the `winch_lcdctl` capture. Two independent QNX implementations agree
byte-for-byte; only the framebuffer pointer and GFX window changed.

**The panel-ID strap is not valid cold (§6.2, a trap).** One cold
`winch_lcdctl` run selected `LCD is LG`; every other occurrence in `sloginfo`
(boot splash, and the `screen` wake) says `LCD is Hitachi`. `GPIO6_DATAIN`
(`0x4805D138`) followed: `0x00000800` (bit 16 = 0) during the cold run,
`0x00010800` (bit 16 = 1) once `screen` had the display running. So GPIO176
reflects the panel only when its rail/TCON is powered, and a cold read silently
picks the **wrong SPI table**. That run also left the panel **dark** even though
DISPC was scanning, the PLL was locked and packets were sent.

**omapdrm works on this board (report 118 §8).**
* `CONFIG_DRM` was `=m` — i.e. no DRM at all in an initramfs kernel. Making it
  `=y` (plus `DRM_OMAP`, `DRM_PANEL_LVDS`, `DRM_TOSHIBA_TC358764`,
  `DRM_FBDEV_EMULATION`, `LEDS_LM3532`, `BACKLIGHT_LED`) was necessary.
* After the DTS nodes (from 118 §4.1) and three small `tc358764.c` changes
  (below): `/dev/dri/` and `/dev/fb0` exist, `0 omapdrmdrmfb`, `DSS_CONTROL`
  becomes readable (`0x1b`), `DISPC_LINE_STATUS` advances, `DSI_PLL_STATUS = 0x383`
  (§8.2). The DSS module is awake — a driver claiming `dss_clk` fixes exactly what
  the userspace route could not.
* Two expected divergences were recorded there and one of them was wrong; see
  *Superseded*.

**Backlight-era measurements that survived (report 118 §9, §10).**
* Reference rails: `vaux1/2/3` **ON at 2800 mV**, `vmmc` OFF, `regen1` ON,
  `regen2` OFF (`pmic-twl6030-ctl get_state/get_vout`).
* Linux exposed `VAUX1/2/3_6030` **enabled but with no voltage programmed at all**
  (`regulator.3/4/5/microvolts` empty, while VUSB reads 3300000 and VANA 2100000).
  A fixed `min == max` constraint makes the core apply it (`apply_uV`), and all
  three then read 2800000 (§9.6, §9.6.1). It did **not** make `0x38` answer.
* Forcing VAUX3 did remove a real fault: before it, **any** transfer to `0x38`
  hung the I²C bus and reset the unit; after it the client enumerated and merely
  deferred (`waiting_for_supplier`), and `fw_devlink=permissive` cleared even that
  flag without making it bind (§9.2, §9.4).
* The LM3532 never answered at all: `bl scan 0` on the reference's own bus
  (`/dev/i2c-0` = `0x48070000`) found `0x09, 0x48, 0x49, 0x4a, 0x55, 0x77` and
  **no `0x38`** (§9.5). `backlight_win` has `/dev/i2c0` open (`pidin -p … fds`),
  and QNX's three controllers are `0x48070000`/`0x48072000`/`0x48350000`
  (IRQ 88/89/94) (§9.6).
* `&i2c2` (`0x48072000`) and `&i2c3` (`0x48350000`) **hang on first access from
  Linux**; only `0x48070000` is usable (§9.6).
* TWL register access over `i2c-dev` returned `EAGAIN`/`ETIMEDOUT` when called
  straight from the console process, and worked when moved into a forked child.
  Hypothesis (not proven): it raced with the kernel TWL IRQ thread. Everything
  touching I²C in the console therefore runs forked with a bounded `waitpid`
  (§9.2, trap 16).
* The reference GPIO output map was captured with `memdump3`
  (`OE/DATAIN/DATAOUT` at `+0x134/0x138/0x13C` for all six banks). It showed
  `GPIO1_2` (`regen1`) and `GPIO2_13` driven high in `DATAOUT` while `DATAIN`
  read 0. My reading — "not muxed as GPIO" — was directionally right for *our*
  side and wrong about the reference; see *Superseded*.

**"Disabled in effect" audit (report 118 §11).** The DT carries
`ti,twl6030-pwm`, `ti,twl6030-pwmled` and `ti,twl6030-gpadc`, but
`CONFIG_PWM_TWL`, `CONFIG_PWM_TWL_LED` and `CONFIG_TWL6030_GPADC` are `=m`, and
this kernel loads no modules — so they silently never bind. `CONFIG_GPIO_TWL4030`
is `=y` but the driver is TWL4030/TPS659x0 only and the board DT has no `gpio`
child, so **TWL GPIOs are unreachable from Linux**. This is the general shape of
the mistake to look for: a DT node whose driver is a module.

**The kernel patch was regenerated (report 118 §5 + bootstrap §5).** Against a
real pristine 6.15.11 at
`/home/psyden/SSDdrive/wsl-migration/home/psyden/kernel/pristine`: 28 files /
2 482 lines, verified with `patch -p1 --dry-run`, emitted in both label styles
(`pristine/… clean/…` and `a/… b/…`) with byte-identical bodies. Two pitfalls
worth keeping: `diff -rq` **follows** `scripts/dtc/include-prefixes`, so every
`arch/arm/boot/dts/**` change is reported three times; and **new files are
reported as "Only in"** mixed in with build artifacts, so the board DTS is
silently omitted unless it is added by hand. `…/kernel/linux` is the *kexec-phase
modified* tree per the operator, not pristine and not ours.

**`BOOTCTR` repair script: two failures (report 116 addenda 1–2).**
* `top_up_bootctr` built the value with
  `printf '\062' | dd of=/tmp/bc32.bin …`. **`printf` exists neither as a builtin
  nor as a binary on TabletOS**, so the pipe produced nothing, the temp file
  stayed `00 00 00 00`, and the script wrote **`0x00000000`** — the brick value —
  six times per boot. `.startup.log` reported it as
  `startup.sh[40]: printf: cannot execute`, where `40` is the *call site* of the
  function, not the failing line (25).
* The readback check could not notice: it only re-read the counter, which cannot
  distinguish "our write landed" from "nothing changed". `.bootctr.log` therefore
  logged `bootctr -> 0x32 ok` on every boot while the script asked the device to
  store zero.
* The counter really does drain: held at `0x32`, it was found at `0x28` after
  ~10 boots with the repair refusing. So pre-session-11 "it always read 50"
  observations are **not** evidence the repair worked.
* RPMB writes **do** land from this unit: setting `0x28` by hand and running the
  fixed script produced `bootctr: wrote 0x32, readback ok (was '(')` and the
  counter read `2\000\000\000`. That also showed the old zero-writes were being
  *refused* (out-of-range), which is the only reason the unit survived.
* Second failure mode: the temp file lived in **`/tmp`**, while `post-boot.sh`
  starts the script **in the background** next to the other boot scripts. On one
  boot the build silently produced nothing and the guard refused
  (`temp byte '' != '2'`), correctly, with the cause invisible because `dd` had
  `2>/dev/null`. Fixed by building `/accounts/devuser/.bc32.bin`, capturing `dd`
  stderr, and logging `sed -n l` of the file plus the pre-write value.
* QNX tool availability, measured: no `od`, `xxd`, `hexdump`, `cksum`; `sed`,
  `awk`, `strings`, `dd` present. `sed -n l` renders `32 00 00 00` as
  `2\000\000\000$` and both sides compare exactly in a shell test; from the host,
  `pbssh 'dd …' | xxd -p` gives true hex.

**Bench workflow.** A flash cycle does **not** need TabletOS: the flasher only
needs the bootrom, so `pre_os_ifs_flash.py … & ; pbcon.py 'reboot'` works with the
test image on media (`HANDOFF…session6.md` §4). A console-only cycle measured
14–20 s including the 238 KB ramloader upload, versus 1–2 min whenever the unit
had to boot TabletOS first.

## Inferred

* `CLKINP` for the DSI PLL is 38.4 MHz — inferred from the round numbers
  (`REGM 756 / REGN 95` → 604.8 MHz); there is no CM register to read it from
  (report 118 §4.3). Strength: good, but it is an inference.
* The DSS functional clock source differs between the two boot paths: `DPLL_PER`
  `CM_DIV_M2..M7` read `0x808/0x0d/0x2c/0x224/0x24/0x25` on QNX and
  `0x822/0x23/0x23/0x28/0x24/0x23` on Linux (report 118 §7.2). Strength: the
  register values are measured; "this is why the module sits in standby" is the
  inference.
* That Linux's DSS module standby is *caused* by no consumer holding `dss_clk` is
  an inference; what is measured is `0x00040F02` with `STBYST = 1`, reads
  aborting, and writes hanging (report 118 §7.1).
* 111's "≈150 Mbps aggregate" DSI rate was wrong; the measured PLL gives
  ~604.8 Mbps/lane. Strength: the PLL words are measured; the bit-rate
  interpretation depends on the `CLKDCOLDO` convention (report 118 §3.2).

## Dead ends and wrong assumptions (the expensive part)

* **A userspace `/dev/mem` prover cannot bring up the DSS here.** I built
  `disp init|stat|mux|col|off` and `bl init|dump|rd|wr` into the console and
  attempted it. On the Linux path the DSS is `MODULEMODE = 2` with
  `STBYST = 1`; reads SIGBUS, and a **write to `DSS_CONTROL` never completes** —
  it wedged the interconnect, the console child, *and* PID 1's `reboot()`, so the
  unit needed a physical power cycle. A `MODULEMODE 0 → 2` cycle did not clear
  standby and afterwards even DISPC reads aborted (report 118 §7.1, trap 14).
  The fix is architectural: let `omapdrm` claim the clocks.
* **Keeping `reset-gpios` optional in `tc358764.c` hid a real need.** I made it
  optional because the pin was unknown; on this board the bridge does need its
  reset (later found to be `GPIO2_24`). Treating "we do not know the pin" as
  "there is no pin" was wrong (report 118 §8.1; corrected by reports 121/122).
* **The in-tree `tc358764` driver leaves `hs_rate = 0`, and the OMAP host derives
  its PLL search range from it** (`omap_dsi_host_attach()`:
  `dsi->config.hs_clk_max = client->hs_rate`), so `pll_max = 0` and *every* mode
  is rejected. It also hardcodes 4 lanes. Patching both from DT was necessary
  before any mode could be accepted (report 118 §8.1).
* **A bare `panel-lvds` on the DSI port cannot work.** The DSI host needs a
  `mipi_dsi_device` (a `mipi_dsi_driver` child) to attach; with only a panel,
  nothing calls `omap_dsi_host_attach()` and the output never starts. Also
  `dsi_probe_of()` requires a **`lanes`** property listing function/pin *pairs* —
  not `data-lanes` (report 118 §8.1).
* **`i2c-1`/`i2c-2` (0x48072000, 0x48350000) are not alternatives.** Enabling them
  in the DT and scanning produced only timeouts; the controllers are not usable
  from Linux (report 118 §9.6).
* **The LM3532 was repeatedly assumed to be "unpowered" and to need a rail.** All
  three AUX rails at 2.8 V, `regen1` driven high after detaching it from the PMIC,
  and `GPIO2_13` high all failed to make `0x38` answer. The real gate was
  `GPIO2_16` (report 120). The wasted effort was in the *inference*, not the
  measurements — every measurement in 118 §9/§10 stands.
* **`bl dump` (blocking I²C from the console) wedged the unit**, exactly like the
  DSS write; only forking with a bounded `waitpid` made I²C probing safe
  (report 118 §9, trap 16).
* **`--reuse-loader` as a shortcut** is not reliable: it can fail at
  `set_mode(2)` and leave the bootrom blinking red (later trap 30). And starting a
  flash while the unit is already in the ramloader makes the console `reboot` a
  silent no-op, so the flasher waits out the ramloader's own ~90–120 s timeout —
  the "two minutes with the green LED" I diagnosed from `wait_bootrom()` matching
  **only** `0x0001` (later trap 27).
* **`pbcon.py` matched a stale prompt.** The console prints `pb> ` and my reader
  stopped on the *previous* prompt, so every reply lagged one command — an empty
  regulator dump was actually the previous command's output. Fixed by draining
  input before each write. Cheap bug, convincing wrong data.

## Unknown / open questions

* Is the DSS functional clock the difference between QNX and Linux, and can it be
  forced without a driver? Resolve by reading `CM_CLKSEL_*`/`DPLL_PER` and by
  letting a driver own it (the latter worked).
* Why did TWL register reads via `i2c-dev` fail intermittently (`EAGAIN` /
  `ETIMEDOUT`) while a plain 1-byte address ping always worked? The fork workaround
  is empirical; the cause (IRQ-thread race vs bus state) is unproven.
* The `GFX_ATTRIBUTES = 0x04000200` bit for `GFXENABLE` was never looked up (bit 0
  reads 0 yet `GFXEND` fires). Irrelevant to omapdrm, which programs its own
  overlay, but it means 118's §2 table has one field I could not decode.
* Mode-record `+0x68/+0x6c/+0x70 = {60, 90, 300}` remain unassigned.
* Whether the DSI PLL reference is really 38.4 MHz — needs a datasheet or a
  measurement of the DSS `sys_clk`.

## Superseded / uncertain

* **The 2× DSI link rate was not benign.** I recorded (118 §8.2) that mainline's
  `CONFIGURATION1 = 0x1CE1F81F` / `TL = 2016` differed from QNX by 2× and that
  this was an encoding difference with the same line time, and I relayed that to
  session 12. Report 121 §11 shows the reference runs **four** data lanes; two
  lanes force 75.6 MHz and four give 37.8 MHz with `TL = 1008`. My arithmetic was
  right and my conclusion wrong: it compared encodings instead of checking the
  lane count (`docs/contradictions/118-vs-121-dsi-link-rate.md`).  The same
  correction retires the "~604.8 Mbps/lane" figure in *Inferred* below: that is
  the **two-lane** number, and with four lanes the reference runs
  `302.4 Mbps/lane` (37.8 MHz byte clock, `TL = 1008`), report 121 §11.
* **The reference's GPIO2 map in 118 §10.1 is incomplete** — it missed bit 24
  (`gpio_56`, the bridge RESETN). Recorded in `KNOWN-ISSUES.md` §3.
* **My "the pads are not muxed as GPIO" reading (118 §10.1)** came from
  `DATAIN = 0` while `DATAOUT = 1`. Report 120 shows *our* boot path resets GPIO2
  and leaves the pad in safe mode (mux 7) while the reference has mux 3 and drives
  it high: the reference pads **are** muxed, and the failure was on our side. The
  raw `OE/DATAIN/DATAOUT` numbers in 118 §10.1 still stand.
* **"The bootloader must configure the bridge" / "it may be in `/base`".** I told
  session 12 that a boot stage could program the TC358764, then that the QNX
  `/base` display driver might. Both are superseded: the control port is
  DSI-generic, the second-stage loader contains no DISPC/DSI/PHY/PLL constants
  (scan of `hd0head.bin`, first 2 MB — I²C/GPIO/MMC/UART constants present, no
  display strings), and the IFS splash proves the panel path works with the
  bridge untouched (`docs/contradictions/111-vs-122-bridge-programming.md`).
* **The userspace prover route** (118 §7) is superseded as an approach: the
  console kept the `disp`/`bl` commands, but the path itself is closed until
  something owns `dss_clk`.
* **`kernel/linux` next to `pristine`**: I first described it as a built copy of
  pristine; the operator corrected that it is the kexec-phase modified tree. The
  committed wording in 118/bootstrap reflects the correction.

## Artefacts produced

* `docs/reports/118-live-qnx-display-registers-2026-10-27.md` — the live capture:
  the DSS power-domain recipe (§1), DISPC decode (§2), DSI/PLL/PHY (§3), the DTS
  recipe (§4), the post-wake validation and the panel-ID trap (§6), the
  userspace-prover failure (§7), the working omapdrm bring-up (§8), the rails/bus
  scan/GPIO map (§9, §10) and the disabled-driver audit (§11).
* `docs/reports/117-re-display-r1-2026-10-27.md` — subagent static RE: `0x1cce0`
  is the HAL parameter-block defaults initialiser, not the programmer; the writers
  are `0x18a7c` (DISPC), `0x179dc` (DSI), `0x16610` (`DSS_CONTROL`); the
  `PLL_CONFIGURATION1` derivation matches hardware exactly.
* `docs/reports/119-re-pmic-regen-2026-10-27.md` — subagent static RE:
  `pmic-twl6030-ctl` emits **no I²C** (it is a devctl client of
  `pmic-winchester-twl6030`); `regen1` is OMAP `GPIO1_2` via `SETDATAOUT` on
  MBID ≥5; `vaux3` is `0x48[0x8E] ← 0x21`.
* `docs/reports/116-rpmb-bootctr-plan-2026-10-27.md` — addenda 1 and 2 (the
  `printf` bug, the false-positive readback, the `/tmp` race, the guard).
* Board DTS, at this session's close: `&dss`/`&dsi1` okay, `bridge@0`
  (`toshiba,tc358764`), `panel-lvds` with the measured timing, `&vaux1/2/3` at
  2800 mV (fixed `min == max`), `&lm3532` + `led-backlight`, `&i2c2`/`&i2c3`
  probing, 19 nodes disabled. (Session 12 later adds four lanes, the GPIO hogs,
  `reset-gpios` and MCSPI4/spidev.)
* `drivers/gpu/drm/bridge/tc358764.c`: optional `reset-gpios`;
  `lanes`/`hs-rate`/`lp-rate` from DT; `#include <linux/of.h>`.
* `initramfs/console.c`: `disp init|stat|mux|col|off`, `bl init|dump|rd|wr|scan`,
  `i2c r|w`, `write <path> <text>`; `disp`/`bl scan`/`i2c` forked with a bounded
  wait.
* `tools/pbcon.py` (new): console driver that waits for `pb> `, drains before each
  write, `PBCON_TMO=<s>`.
* `bench/device-startup.sh`: the corrected `top_up_bootctr` (no `printf`, temp
  file outside `/tmp`, value verified before writing, write skipped when already
  `0x32`, distinct log lines).
* Kernel patch + config re-exported: 28 files / 2 482 lines (both label styles),
  verified with `patch -p1 --dry-run` against pristine 6.15.11.
* Images built: `0x06FC37D9`, `0x7777931C` (`kernel-safe`), `0x3B04298E`
  (**never flash** — LM3532 without VAUX3), `0x2AC833B8`, `0x6017A08E`,
  `0x6A040070`, `0x0A8B037E`, `0xCEB270AC`.
