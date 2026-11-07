# Session 29 (absolute) — the WL1283's real bus, and a working CM3217

> Numbering: absolute `29`; the reports call this Linux-era **session 23**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-06 → 2026-11-07.
Unit state at the end: **our Linux image `0xFC32F008` in region 0** with the
region-1 root `0faeadff…`; console up, CM3217 reporting lux.  **The 1917 IFS is
not on media** — the operator ended the session here.  `BOOTCTR_FCTRY` read
`0x32` at the last TabletOS boot but has **not** been re-read since.

## Goal of this session

`HANDOFF.md` §1 / ROADMAP item 9 step 4: bring up the TI WL1283 WiFi/BT/GPS combo
on its SDIO bus.  After the WL1283 proved not to enumerate, the operator
redirected to the CM3217 (item 9 step 2, report 181's protocol).  **The CM3217
now works; the WL1283 is parked for a session with fresh eyes.**

## Confirmed (measured, read in a file, or seen on the bench)

* **The WL1283 combo is on OMAP MMC5 (`0x480D5000`), not MMC1.**  `pidin mem` on
  TabletOS shows io-pkt's `devnp-ti1283.so` holding `/dev/mem 0x480d5000`
  (8 KiB); `pidin irq` shows it attaching QNX vector `0x5b` (91 = **GIC SPI 59 =
  mmc5**) plus `0x41d` (1053 = `1000 + gpio 53`).  The `sdmmc5` pads
  (`0x148`–`0x152`) read `0x0118` while every `sdmmc1`/`sdmmc3` pad reads
  `0x000f` (safe mode).  [report 180 §1]
* **WLAN_EN = gpio 54 = GPIO2_22** (pad `gpmc_nwp` `0x07c`, active high, 120 ms),
  **WLAN_IRQ = gpio 53 = GPIO2_21** (pad `gpmc_ncs3` `0x07a`, input, falling,
  active low).  Static RE of `devnp-ti1283.so` **and** the live GPIO diff: the
  reference's `GPIO2_DATAOUT` is `0x31413200` vs `0x31013200` in report 118 —
  bit 22 is the only output that changes when the WLAN driver runs.
  [report 180 §0/§2]
* **GPIO4 differs and is now matched.**  Our boot had `OE=0xffffffff`,
  `DATAOUT=0`; the reference drives `OUT=0x90000120` with `OE=0x6ffffe1c`, i.e.
  bits **5, 8, 28, 31** — exactly report 118 §10.1's untested candidates.
  Mirroring them gives `OE=0x6ffffedf OUT=0x90000120`.  [report 180 §6]
* **The whole PMIC register space matches.**  All 256 TWL6030 registers compared
  across the two OSes: 4 non-rail differences in `0x00`–`0x7F` (0x00, 0x01,
  0x20, 0x2d) and 8 in `0xB0`–`0xFF` (0xb0, 0xb1, 0xb2, 0xbd, 0xbe, 0xe4, 0xf4,
  0xfa).  Every regulator `CFG_STATE` is identical, and where they differ Linux
  has *more* enabled.  [report 180 §4]
* **The clock/power space matches in the safe direction.**  `CM_PER`
  (`0x4A008000`–`0x81FC`), all of `CM2` and `PRM` captured on both OSes: 9
  `CM_PER` and 41 `CM2` words differ, but the reference idles in `HW_AUTO`
  (`0x00030000`) while our boot holds dozens of domains in `SW_WKUP`
  (`0x00000002`).  `CM_DIV_M5_DPLL_PER` is `0x329` (ref) vs `0x204` (ours), yet
  bit 9 `ST_HSDIVIDER_CLKOUT2` — read-only status — is 1 in both, so that output
  runs either way.  [report 180 §6]
* **`&mmc5` probes and clocks**: `PBX: mmc4 probe-pm fck=48000000 … idlest=0`,
  `CON=00000600 HCTL=00000b00 SYSCTL=000e1e07 CAPA=04e10080`; `HCTL` is
  byte-identical to what QNX writes.  The card never answers CMD5
  (`attach_sdio no` → `mmc4: Failed to initialize a non-removable card`), and
  `pb_trace` shows `STAT=00018000` (`ERRI|CTO` = command timeout).
  [report 180 §3]
* **The chip does not run its digital core**: an `HCI_Reset` on UART2
  (`0x4806C000`, the combo's BT transport) at 115200 8N1 returns no bytes.
  [report 180 §6; `rootfs/hci.c` → `/bin/pbhci`]
* **The CM3217 WORKS under Linux.**  `cm3217.ko` probes `3-0010`, IIO device
  `cm3217`, and `in_illuminance_input` reads **32 32 32 32 37** with the room
  lights off and **191 ×6** (189/191 later) with them on.  [report 182 §0]
* **The CM3217 has no settle delay.**  A delay sweep on the reference
  (0/1/5/10/20/50/100/200 ms after `0x26`) returns data **only at delay 0**.
  [report 182 §1; `tools/qnx-cm3217.c`]
* **Leaving the CM3217 enabled wedges the panel's automatic brightness**, and
  writing the vendor idle state back (`0x60`→0x11, `0x27`→0x10) restores it.
  [**operator observation**, twice: it froze during probing, recovered after the
  restore]
* **The boot log is clean.**  `local/logs/boot-20261107-095720.log`, 1041 lines:
  no WARN/Oops/backtrace, no L3 errors; the only taint is the expected
  `loading out-of-tree module taints kernel` from `cm3217`.  [that file]
* **The loader leaves WL_EN low**; the QNX driver owns it — turning Wi-Fi off in
  Settings drives gpio 54 low *and* the chip's IRQ line low.  [report 180 §6]

## Inferred (reasoning from evidence, not directly measured)

* **The remaining WL1283 failure is inside the chip or its power-up.**  Every
  SoC-side surface that can be read now matches the reference (pads CORE+WKUP,
  all six GPIO banks, all 256 PMIC registers, CM2/PRM, the FREF mux), yet the
  chip neither answers SDIO nor drives its IRQ.  *Strength: strong.*
* **The WL1283 may need a reference clock the SoC is not supplying.**  The
  vendor's own `tiwlan_winchester.ini` says `STRFRefClock = 2` (38.4 MHz) with a
  CLK_REQ handshake.  Only half of that was tested (see §Superseded).
  *Strength: moderate; untested.*
* **`WLAN_EN_GPIO`/`WLAN_INT_GPIO` defaults 54/53 are the ones in force.**  The
  live GPIO diff corroborates 54; 53 follows from the exact `1000 + gpio`
  interrupt convention.  *Strength: strong, not proven — the image has no
  `hwinfo` tool to dump the tree.*
* **A second enable may exist that nothing on our boot drives.**  Suggested only
  by the BT core also being silent.  *Strength: weak.*

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The WL1283 is on MMC1."**  Report 176, ROADMAP and the session-28 DTS all
  said so.  Killed by `pidin mem`/`pidin irq` — one read-only query.
* **"GPIO2_21 is a display enable, drive it low."**  It is the chip's interrupt
  output; the old DTS hogged it low as an output.  Killed by the RE
  (`set_direction(53, 1)` = input) and the live GPIO2 dump.
* **"The enable line is the missing piece."**  It was a real defect, and fixing
  it changed nothing.  Killed by four boots: hog at boot, regulator-driven at
  MMC power-up, `ti,no-reset-on-init`, and 5 s of settling — all `attach_sdio no`.
* **"A power cycle will revive it."**  Killed by a full power-button cycle.
* **"The OCR window is 3.3 V like the eMMC."**  Killed by testing both 1.8 V and
  3.3 V; `CAPA=0x04e10080` is all-voltage-capable and QNX negotiates nothing.
* **"`MMCHS_CON` differs (`0x600` vs `0xE0080`)."**  Wrong — the disassembly puts
  `0xE0080` in **`MMCHS_SYSCTL`** (`CLKD=2`, `DTO=14`), and `CON` is only
  `&= ~OD` in both.  Killed by reading the store targets at `0xcd250`–`0xcd290`.
* **"The MMC5 functional clock is 96 MHz on QNX."**  Wrong — my arithmetic;
  mainline gives MMC5 the 48 MHz `func_48m_fclk` parent.  Killed by the
  clock-tree table and the CM diff.
* **"The CM3217 is dead."**  Wrong, and it contradicted a direct operator
  observation.  Killed by the operator ("the screen gets brighter when I turn on
  the lights") and then by the driver reading 32→191 lux.
* **"The CM3217 protocol is not settled."**  Killed by the delay sweep already in
  hand: data appears at delay 0, so the 200 ms sleep was the only bug.
* **"QNX `SIGBUS` on MMCHS5 is an L4 firewall."**  Killed by
  `tools/qnx-iotest.c`: `ThreadCtl(_NTO_TCTL_IO)` returns `-1/EINVAL` *and* the
  same session reads CORE/WKUP padconf, GPIO2 and CM2 happily, and
  `procmgr_ability(ALLOW|AID_IO)` changes nothing.  The refusal is block-specific
  and only io-pkt can read the SDIO host (trap 101).
* **"Reading GPIO3–6 from Linux user mode is fine."**  Each read logs an L3
  custom error and an unhandled external abort and taints the kernel.
* **A cover/uncover test of the CM3217** was run as two unbounded 48 s loops
  never synchronised with the operator, then read as "covered" vs "uncovered"
  from the order I issued them.  **Discarded as invalid** (trap 102), and doubly
  wrong because our accesses were racing RIM's driver (trap 103).
* **"Probing the CM3217 from TabletOS characterises the part."**  Killed by the
  operator noticing the panel froze exactly while I probed: our external I2C
  accesses interleave with `ls-cm3217`'s own sequence and break it.

## Unknown / open questions

* **Why does the WL1283 not come up?**  *Would settle it:* a bus capture of the
  enable/clock/CMD lines, the chip datasheet, or a QNX-vs-Linux comparison of the
  same cold-boot path.
* **Is there a reference clock, and where from?**  *Would settle it:* enable the
  FREF_CLK2 parent in CM and re-run `pbhci`; or trace the CLK_REQ pin.
* **Does the PlayBook's BSP pass a `WLAN` hwinfo node?**  *Would settle it:* a
  QNX `hwinfo` dump tool (none on the image) or RE of the BSP `startup`.
* **Is the CM3217's lux value accurate?**  32–37 indoors with lights off and 191
  with lights on is plausible but uncalibrated.  *Would settle it:* compare with
  a reference light meter.
* **`BOOTCTR_FCTRY` at session end** is unverified (last read `0x32` on
  TabletOS).  *Would settle it:* restore the 1917 IFS and read it.
* **The IRQ-driven sensor path** (BMA150/HMC5883L/MPU3050 on GPIO2 2/18/27) is
  still polling-only — unchanged from session 28.

* **My own knowledge was out of date, twice, and the operator caught it twice.**
  I said kernel `7.3.3` "doesn't match the 6.x series" — wrong;
  `kernel.org/releases.json` shows **7.3.3 as the current stable** (2026-11-06).
  Earlier in the same session I read the operator's 2026-09-01 pmaports snapshot
  as if it were current pmaports.  **New sessions: look external facts up and
  cite them; treat recalled knowledge as a hypothesis** (DECISIONS §32, ROADMAP
  working rule, trap 105).

## Superseded / uncertain

* **The FREF pad muxing now in the DTS** (`0x4A10019C` = `fref_clk2_out`, WKUP
  `0x4A31E054/058/05C`) is a faithful copy of the reference, but the reference
  puts those pads in **mux 3 = GPIO with GPIO6 `OE` all-inputs** — high-Z, not a
  driven clock.  The earlier claim "the reference muxes a reference clock out" is
  superseded by that decode.
* **The 1917 IFS is not on media.**  Every earlier session ended with TabletOS
  restored; this one ends on our Linux image (operator's call).  Restoring 1917
  is the documented way back, and `BOOTCTR_FCTRY` is unverified until then.
* **Report 180 §1/§8** blamed an L4 firewall for the QNX `SIGBUS`; trap 101 is
  current.
* **Trap 95's** "the CM3217 needs a different protocol" is superseded — the
  protocol was right except for the settle delay (report 182).
* **Report 176 §2's `mmc1`** references for the WL1283 are wrong; the rest of
  that inventory stands.
* **The `PBD:`/`PBGPIO:` dumps** in `omap_hsmmc.c` are instrumentation in the
  patch, like the `PBM:`/`PBSIO:` prints; strip them once the bus works.

## Artefacts produced

* `docs/reports/180-wl1283-sdio-2026-11-06.md` — bus/wiring measurement, four
  build variants, eliminated causes, padconf/PMIC/CM diffs.
* `docs/reports/181-cm3217-protocol-2026-11-06.md` — static RE of
  `ls-cm3217.so` (two addresses, bare bytes, no ID).
* `docs/reports/182-cm3217-driver-and-dead-part-2026-11-07.md` — the working
  driver, the real protocol, and the three wrong turns.
* `kernel/source/cm3217.c` (+ the copy in `local/kernel-tree/drivers/iio/light/`),
  `Kconfig`/`Makefile` entries, `CONFIG_CM3217=m`.
* `kernel/source/omap4-winchester.dts` — `&mmc5` (4-bit, `sdmmc5_pins`, `vwl1283`
  supply, `ti,non-removable`), `wl1283_ctrl_pins`, the WL_EN hog, GPIO4 mirror
  hogs, `fref_clk_pins` + `playbook_twl_wkup_pins`, `&uart2`, the `capella,cm3217`
  node, and `ti,no-reset-on-init` on `gpio2_target`.
* `local/kernel-tree/arch/arm/boot/dts/ti/omap/omap4-l4.dtsi` — added the
  `gpio2_target:` label.
* `local/kernel-tree/drivers/mmc/host/omap_hsmmc.c` — `PBGPIO:`/`PBD:` dumps in
  probe only; `trace`/`init_hz` knobs.
* `kernel/patches/0001-playbook-winchester-6.15.11.patch` — **re-exported**,
  5 703 lines / 34 files, `patch -p1 --dry-run` clean against pristine 6.15.11.
* `rootfs/hci.c` → `/bin/pbhci`; `rootfs/etc/init.d/rcS` loads `cm3217`.
* `tools/qnx-cm3217.c` (bare-byte probe + delay sweep), `tools/qnx-iotest.c`
  (`ThreadCtl`/block-read probe), `tools/build_rootfs.py --firmware DIR`
  (added, **UNTESTED** — no firmware tree was passed to it this session).
* `local/logs/boot-20261107-095720.log` — first full console dump of this image.
* `docs/project/KERNEL-PORT.md` — the operator's next focus: port the patch set
  to `linux-postmarketos-stable` 7.3.3-r0, keeping 6.15.11 as backup.  Plan and
  per-file risk only; **nothing ported, no boot spent**.
* Images: region-0 **`0xFC32F008`**
  (`local/images/kernel-0xFC32F008-cm3217.img`) + region-1
  `local/images/rootfs-region1.ext4` sha256 `0faeadff…`.  Superseded
  intermediates: `0x74FA74A3`, `0xA7D3BC13`, `0x0005424F`, `0x4949969A`,
  `0x1A982E7B`, `0x0DAFC14B`, `0x224A189E`, `0x05401177`, `0x2227760F`,
  `0x870183C3`, `0x7A272B07`, `0x6F7DD650`.
