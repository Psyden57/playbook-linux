# Session 28 (absolute) — a real userland, and the I²C4/FAN5702 peripherals

> Numbering: absolute `28`; the reports call this Linux-era **session 22**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-06.
Unit state at the end: **PlayBook OS 2.1.0.1917 running** (the restored
**uncompressed** IFS `local/work/ifs-shim-sealed.ifs`, sha256 `b52195bb…`),
`/dev/hd1t179 on /base`, **`BOOTCTR_FCTRY = 0x32`**, door up with root ssh.

## Goal of this session

`HANDOFF.md` §1: **ROADMAP item 5** — a real userland in the region-1 root, then
the `hd3` cut-over, then packaging.  Mid-session the operator **parked the
`hd3` cut-over** and redirected the work to the device's own **sensors and
peripherals** (accelerometer, magnetometer, touchscreen, FAN5702 LED, audio,
WiFi, GPS/AGPS, battery/thermal).

## Confirmed (measured, read in a file, or seen on the bench)

* **A real region-1 userland boots.**  `local/images/rootfs-region1.ext4`
  (64 MiB, ext4, no journal) carries BusyBox 1.36.1 (static, 401 applet links),
  BusyBox init + `/etc/inittab`, `rcS`/`profile`, `/bin/pbconsole`,
  `/bin/pbrpmb`, and `/lib/modules/6.15.11`.  Written at raw `0x29000000`
  (loader block `0x2915`) through the pre-OS `0xF7` loop.  The first build was
  sha256 `e1ec2bf3…`; the final one (15 modules) is `0a55a170…`.
* **The initramfs hands PID 1 to it.**  `rootfs_boot()` mounts region 1
  **read-write**, bind-mounts `/dev` `/proc` `/sys`, `chroot`s and execs
  `/sbin/init`; `/dev/loop0 on / type ext4 (rw)`, `uname -a` = `6.15.11`,
  `sh -l` on `/dev/ttyGS0` with `PS1='pb> '`.  With no `/sbin/init` it falls
  back to the bring-up console.  [image `0xAEDF68D0`]
* **Modules load.**  `insmod /lib/modules/6.15.11/kernel/pbhello.ko` and
  `modprobe pbhello` both work; `dmesg` logs `pbhello: loaded from the
  region-1 root`.  `CONFIG_MODVERSIONS=y`, so `Module.symvers` exists (the
  whole `=m` tree was built once).
* **A read-write loop mount needs a writable backing fd.**  Opening
  `/dev/mmcblk1` `O_RDONLY` and mounting the loop `rw` gave
  `PB: rootfs: mount: Permission denied`; opening it `O_RDWR` fixed it.
* **The way back is verified, twice.**  The uncompressed 1917 IFS was flashed
  from the Linux image with `bbusb.py` (489 frames, `COMPLETE 0x4006`),
  TabletOS came up, `/base` mounted, and `BOOTCTR_FCTRY` reads `0x32`.  The
  Linux image's boot-time top-up log read `0x2f >= threshold 0x1e, no write`
  (so the counter was still healthy before the restore).
* **The peripheral inventory (report 176).**  QNX runs three I²C controllers
  (`0x48070000`, `0x48072000`, `0x48350000`) and the **sensor cluster is on the
  third — OMAP I²C4 `0x48350000`, QNX `/dev/i2c3`**:
  - BMA150 accelerometer `0x38` (chip ID `0x02`), `sensor-accel-bma150.so`;
  - HMC5883L magnetometer `0x1E` (`H43`), `sensor-mag-hmc5883l.so`;
  - MPU-3050 gyroscope `0x68`, `sensor-gyro-mpu3050.so`;
  - CM3217 ambient light `0x10`/`0x11`, `ls-cm3217.so`;
  - FAN5702 LED driver `0x36`, `led-fan5702.so`;
  - WM8994 audio codec `0x1a`, `deva-ctrl-omap4abe-wm8994.so`.
  On I²C1: LM3532 `0x38`, TWL6030 `0x48`–`0x4a`, BQ27541 gauge `0x55`,
  TMP103 `0x77`.  RIM's own map is
  `/base/etc/system/config/sensor_config.xml` (board `Winchester` rev 0:
  accel/mag/gyro with IRQ GPIOs 34/59/50).
* **WiFi/BT/GPS is one TI WL1283 combo**: `devnp-ti1283.so` (SDIO, `ti0`),
  `devc-seromap-winbt` (UART `0x4806c000`), `devlp-ti128x.so` + `btp-gps.so`
  (GPS with RRLP **AGPS** aiding), firmware in `/base/lib/firmware/ti1283`
  (`firmware.bin`, `tiwlan_winchester.ini`), GPS patch in `.../gps`.
* **Touch is a Cypress CY8CTMA3 (TrueTouch)** loaded by RIM's
  `libmtouch-pegasus`/`libmtouch-munin` with `Pegasus_50.hex`
  (`/base/lib/firmware/munin`); `libmtouch-pegasus.so` references
  `/dev/i2c2` (OMAP I²C3 `0x48060000`, the controller QNX does **not**
  instantiate).
* **The board DTS had the sensor bus inverted**: `&i2c4` (`0x48350000`) was
  `disabled` with the comment "the one QNX does not instantiate", while
  `&i2c3` (`0x48060000`) was enabled.  It is the other way round (trap 91);
  fixed and measured.
* **The I²C4 sensor bus and the three motion sensors work (report 177).**
  `&i2c4` enabled + the BMA150/HMC5883L/MPU3050 nodes, `INPUT_BMA150=m` and
  `SENSORS_HMC5843_I2C=m`, and an `of_match` table added to the BMA150 driver
  (it predates DT).  `i2c-3` carries `3-0038` (BMA150, `input0`, live `EV_ABS`
  on `/dev/input/event0`), `3-001e` (HMC5883L, IIO raw `-103/-268/-518`) and
  `3-0068` (MPU3050, IIO, `part no 3, version 14`, die ID `0E9E`).
* **The `=m` IIO closure is required**: `industrialio`,
  `industrialio-triggered-buffer`, `kfifo_buf`, `i2c-mux`, `evdev`; without
  them the sensor modules fail "unknown symbol" (trap 94).
* **`tools/bbusb.py` is the flasher** (operator): both regions were written in
  one bootrom session (`pack -l both.lst -v 1,2 -s -f` + `flash --signed`),
  and single components via `--component --dest-block`; success is
  `COMPLETE 0x40C0 -> cmd 0x4006`.  The bootrom window is 60 s (was 10 s); from
  our Linux shell `reboot -f` is the reset (a graceful shutdown took ~90 s).
* **Step 2 (report 178): the BQ27541 gauge and the TMP103 work.**  DTS nodes on
  `&i2c1` at `0x77` (`ti,tmp103`) and `0x55` (`ti,bq27541`): `hwmon0` reads
  `temp1_input=37000` (needs `hwmon.ko`), and `bq27541-0` reports capacity
  100 %, 4.093 V, 28.8 °C, 5.307 Ah full / 5.400 Ah design, cycle count 5,
  Li-ion, TI.  **The CM3217 is open**: 0x10/0x11 ACK, but the CM32181 sequence
  is refused (byte reads inconsistent, word read `0xff00`, CMD word write
  `ENXIO`); the QNX `ls-cm3217` CMD byte is at `+0x40` (trap 95).
* **Step 3 (report 179): the FAN5702 status LED works.**  A mainline
  `leds-fan5702` driver (`fairchild,fan5702`) probes `3-0036`; the three
  channels are `playbook:…:{red,green,blue}:status` and `brightness` turns them
  on/off.  EN = GPIO1_13 (pad `0x1b0`, safe mode at boot) is driven by the
  driver's pinctrl + `enable-gpios`; raw I2C was `ENXIO` before.  Protocol
  (external `LED-RE.md` + `led-fan5702.so`): `GENERAL 0x10` bits 1/2/3 = R/G/B,
  `0x20 = 0`, `0x30/0x40/0x50 = 0x36`; 400 kHz works.  **The operator saw
  red/green/blue/white and off.**

## Inferred (reasoning from evidence, not directly measured)

* The Cypress touch is on **OMAP I²C3 `0x48060000`** (the uninstantiated
  controller) at the Cypress address, and does not ACK until firmware is
  loaded — it was not seen on any bus the scan reached.  *Strength: plausible;
  the lib's `/dev/i2c2` is the only direct evidence.*
* `0x09` (I²C1) and `0x65` (I²C2) are a board EEPROM/expander and the NCP5623
  LED, respectively; neither is claimed by a named RIM driver yet.
  *Strength: weak — recorded, not assigned.*
* The WL1283 firmware/NVS can be converted to the mainline `wl128x-fw`/`nvs`
  formats from the QNX `firmware.bin` + `tiwlan_winchester.ini`.  *Strength:
  plausible; not tried.*
* The FAN5702's `GENERAL` bits 1/2/3 are red/green/blue and the `0x30/0x40/0x50`
  order matches; the physical die assignment is the vendor software model plus
  the bootloader's "error blink drives bit1, observed red" correlation.
  *Strength: strong for the bits, board wiring not measured.*

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The region-1 image is empty."**  The first `ls /rootfs` after the flash
  was empty and the media at `0x29000000` read zero — but the ext4 superblock
  is at `0x400`: `blk rd 0x148002` shows `53 ef`/"pbroot".  The real fault was
  the `EACCES` mount (next item).
* **"The loop can be read-only and mounted rw."**  The first switch_root opened
  `/dev/mmcblk1` `O_RDONLY`, so the `rw` mount returned `EACCES`; `O_RDWR`
  fixed it (trap 92).
* **"`umount -a -r` at shutdown is the console noise."**  It is noise, but the
  operator's observation was **eMMC errors/warnings and a stuck tty**, which
  are *not* explained by the umount.  The inittab now uses `sync`; the eMMC/tty
  part is open (trap 93).
* **"The flasher will catch the bootrom after `pbcon reboot`."**  The first
  restore attempt timed out (`waiting for bootrom …`) and exited; the unit was
  then sitting in `0fca:0001`, and re-running the flasher completed in
  seconds.  The 10 s bootrom window was the cause; it is now 60 s
  (`DECISIONS.md` §29).
* **"All the sensor drivers are `=y`/built."**  Most are `=m` and several were
  not enabled at all (`INPUT_BMA150`, HMC5843, CM32181, SENSORS_TMP103,
  MFD_WM8994); the module-capable root is what makes trying them cheap.
* **"The QNX `pbi2c` reads tell us the CM3217 is dead."**  `pbi2c` returned
  0x00 for every register; the Linux `i2cget` later returned 0x4d once at
  0x10/0x04, so the device is live but the protocol/state is not the
  CM32181's.  The QNX read path is not a reliable oracle for this part.
* **"The FAN5702 EN can be driven with raw `devmem`."**  Writing the pad/OE/
  SETDATAOUT by hand left the chip in reset (`ENXIO`).  The driver's
  `pinctrl` + `devm_gpiod_get_optional` is what made it answer; the pad at
  `0x1b0` was in safe mode (trap 96).
* **"The compressed IFS was re-verified this session."**  It was not — the
  kickoff's line was copied from session 27 and is stale; this session used
  the uncompressed `ifs-shim-sealed.ifs` for every restore.  The compressed
  IFS's boot is session-27 evidence (report 175 / `DECISIONS.md` §26).

## Unknown / open questions

* **The shutdown eMMC errors and stuck tty** (trap 93).  *Would settle it:*
  capture the full console `dmesg` and the shutdown output on the next Linux
  boot; check the MMC2 idle-stall, ext4 writeback and the ttyGS0 respawn path.
* **The CM3217 protocol/CMD** (trap 95).  *Would settle it:* a static RE of
  `ls-cm3217.so` (the `+0x40` CMD byte, the two-address ALS read, the IT/cal
  table) and a write test; the external challenge is that the CM32181
  sequence is refused.
* **The touch bus/address and the Gen3 protocol.**  *Would settle it:*
  instantiate OMAP I²C3 (`0x48060000`) and scan it, or RE
  `libmtouch-pegasus`/`libmtouch-munin` and the `Pegasus_50.hex` loader.
* **`0x09`/`0x65`.**  *Would settle it:* read their register maps against the
  NCP5623/EEPROM datasheets.
* **Control of the WL1283's power/32k GPIOs and the SDIO bus (`mmc1`)** in
  mainline, and the firmware/NVS conversion.
* **WM8994 machine glue**: no mainline OMAP4+WM8994 machine driver; ABE
  firmware and routing (speaker/headset/mics) are the open part.
* **The FAN5702 dimming-register bit semantics** — see the contradiction file
  `docs/contradictions/fan5702-dimming-register-bits.md`.

## Superseded / uncertain

* **The kickoff's "the compressed IFS seems to have worked and is booting"** was
  copied from session 27 and is stale for this session; the compressed IFS's
  boot is session-27 evidence (report 175; `DECISIONS.md` §26), and this
  session restored the uncompressed `ifs-shim-sealed.ifs`.  Both are valid
  assets; do not read this session as a re-verification of the compressed one.
* **The DTS comment "i2c4 is the one QNX does not instantiate"** was wrong;
  QNX instantiates `0x48070000`, `0x48072000` and `0x48350000`, and the sensors
  are on `0x48350000` (report 176, trap 91).  The current DTS is corrected.
* **The external `LED-RE.md`'s "100 kHz" vs this tree's `led-fan5702.so`
  "400 kHz"**: the two `.so` builds differ slightly (18,320 vs 18,324 B); both
  are plausible.  This session runs the shared `&i2c4` at 400 kHz and the chip
  works, so the speed is not a variable.
* **The session-18 "read-only root" model** is superseded by the session-28
  switch_root to a read-write BusyBox root (`DECISIONS.md` §27).
* **Trap 65's "red channel"** is the bootloader's error blink using GENERAL
  bit1; the QNX/Linux driver has all three channels (report 179).

## Artefacts produced

* `docs/reports/176-peripheral-inventory-2026-11-06.md` — the inventory,
  bus map, support matrix and proposed order.
* `docs/reports/177-sensor-bus-bringup-2026-11-06.md` — the I²C4 + BMA150/
  HMC5883L/MPU3050 result and the module closure.
* `docs/reports/178-fuel-gauge-and-thermal-2026-11-06.md` — the BQ27541 + TMP103
  result and the open CM3217 (with the Linux probe).
* `docs/reports/179-fan5702-status-led-2026-11-06.md` — the FAN5702 driver and
  the EN/pad finding.
* `kernel/source/omap4-winchester.dts` — `&i2c4` enabled + the three motion
  sensor + FAN5702 nodes, and `&i2c1` TMP103 `0x77` + BQ27541 `0x55`;
  `kernel/source/bma150.c` — the `of_match` table; `kernel/source/
  leds-fan5702.c` + `drivers/leds/Kconfig`/`Makefile` — the new LED driver;
  `kernel/winchester.config` — `INPUT_BMA150=m`, `SENSORS_HMC5843_I2C=m`,
  `SENSORS_TMP103=m`, `LEDS_FAN5702=m`.  (The kernel patch was not re-exported
  this session.)
* `tools/build_rootfs.py` — the BusyBox/init/module/tool installer.
* `rootfs/` — `etc/inittab`, `etc/init.d/rcS`, `etc/profile`, `etc/motd`,
  `modules/pbhello.c` (+ Makefile); updated `README.md`/`pbroot-version`.
* `initramfs/init.c` — `rootfs_boot()`: read-write loop, bind mounts, chroot,
  exec `/sbin/init`; `O_RDWR` backing; kmsg logging.
* Images: `local/images/kernel-0xAEDF68D0-rootuserland.img` (region 0), then
  the sensor steps `0x3E479F6F`, `0x1D9159E0`, and the final **`0xB759C3A2`**
  with region-1 `local/images/rootfs-region1.ext4` (sha256 **`0a55a170…`**,
  15 modules).  The media was restored to the 1917 IFS at the wrap-up;
  re-flash both with `tools/bbusb.py` to return to Linux.
* `tools/bbusb.py` — the bootrom/ramloader open window raised from 10 s to
  60 s (`_open_ramloader`); the flasher from now on (`DECISIONS.md` §29).
* `local/work/qnx-libs/` (gitignored) — the RIM driver modules copied for
  static RE; `local/work/modules/` — the sensor module closure.
* Docs: STATE, HANDOFF, ROADMAP (item 5 closed; item 9 steps 1–3 done),
  KNOWN-ISSUES (traps 91–96), DECISIONS (§27–§29), DEVELOPMENT (§3a, and the
  `bbusb.py` runbook), reports 176–179.
