# Session 36 (absolute) — item 9 step 5 starts: the touch is on I²C2, and Linux never muxed its pads

> Numbering: absolute `36`; the reports call this Linux-era **session 30**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-08.
Unit state at the end: **the touch image `0x04AF1433` in region 0**
(0x91A8C4 bytes; `i2c2_pins` + the Munin driver), region 1 the 7.3.3 charger
rootfs (`ba275807…`, 17 modules); the 1917 IFS is not on media.
`BOOTCTR_FCTRY = 0x2b` (three driver test boots; the boot-time top-up saw
`>= 0x1e`, no write).  Pack Full (100 %, 4.156 V).  The known-good baseline
remains `0xE36C679A`.

## Goal of this session

The operator formally stopped the WL1283 (item 9 step 4) and chose **item 9
step 5**: the WM8994 audio machine and the Cypress CY8CTMA3 touch (report 176).
"Bench-light first" meant resolving the open touch-bus contradiction
(`touch-bus-i2c2-vs-i2c3`) from the 1917 blobs before any driver work.

## Confirmed (measured, read in a file, or seen on the bench)

* **The board selects the `munin` touch driver, not `pegasus`.**  1917
  `/base/usr/lib/graphics/omap4430/graphics.conf`:
  `begin mtouch / driver = munin / filter = ballistic,kalman / options =
  i2c_lib=i2c-resmgr-client,tp_intr=1035,i2c_slave=0x65`.  [report 193 §1]
* **`libmtouch-munin.so` opens `/dev/i2c1`** (the Pegasus library has
  `/dev/i2c2`).  It carries both `i2c-omap4` and `i2c-resmgr-client` back ends
  and the symbols `winchester_tp_init`/`winchester_tp_reset`,
  `munin_existence_check`; `mtouch_driver_init` defaults the address to `0x65`
  and the panel to 1280×768.  [report 193 §1–§2]
* **QNX `/dev/i2c1` is OMAP I²C2 `0x48072000`** (report 176's `pidin ar`), and
  report 176's live scan found `0x65` there.  [report 176 §1]
* **Linux maps `i2c-1` to `0x48072000`** — read from
  `/sys/bus/i2c/devices/i2c-1/of_node` (`target-module@72000`) on the live
  image.  The other adapters: `i2c-0` = `0x48070000`, `i2c-2` = `0x48060000`,
  `i2c-3` = `0x48350000`.  [report 193 §1]
* **The i2c2 pads were in safe mode.**  Live 16-bit reads: `0x4A100126`
  (i2c2_scl) = `0x011F`, `0x4A100128` (i2c2_sda) = `0x011F`; the working I²C1
  pads (`0x122`/`0x124`) and I²C4 pads (`0x12e`/`0x130`) are `0x0110`/`0x0118`.
  [report 193 §3]
* **The symptom was a bus timeout, not a NAK.**  `0x65` on i2c-1 gave `--` +
  `Connection timed out`; an absent address on the working i2c-3 gave `--` +
  `Remote I/O error`.  A whole-bus `i2cdetect -y -r 1` blocked the console for
  more than a minute; Ctrl-C on `/dev/ttyACM0` recovered it.  [report 193 §3;
  trap 138]
* **The pad write fixed it.**  `devmem 0x4A100126 16 0x0110` and
  `devmem 0x4A100128 16 0x0110`, then `i2cdetect -y -r 1 0x65 0x65` showed
  `65`, and `i2ctransfer -y 1 w1@0x65 0x00 r1` returned `0x00`.  A read-only
  sweep of registers `0x00`–`0x07` returned `00 09 b0 05 09 00 b7 2f`.
  [report 193 §4]
* **The Munin wiring** (`winchester_tp_init` `0x9120`, `winchester_tp_reset`
  `0x90a0`): maps GPIO1 `0x4A310000` and GPIO4 `0x48059000`; drives
  **GPIO4_8** (`0x100`) as reset (clear, 20 ms, then set after the power
  pulse) and **GPIO1_14** (`0x4000`) as power (clear 500 ms → set 500 ms).
  `tp_intr=1035`.  [report 193 §2]
* **Both control lines already read output-high under Linux:**
  `GPIO1_OE = 0xFFFF9FBB` (bit14 output), `GPIO1_DATAOUT = 0x00006044`;
  `GPIO4_OE = 0x6FFFFEDF` (bit8 output), `GPIO4_DATAOUT = 0x90000120`.  The
  decoded reset/power pulse changed nothing; the part answered from the pad
  mux alone.  [report 193 §4]
* **The DTS now muxes the bus:** an `i2c2_pins` group
  (`OMAP4_IOPAD(0x126, 0x0110)` / `(0x128, 0x0110)`) and
  `pinctrl-0 = <&i2c2_pins>` on `&i2c2`; copied to `local/kernel-tree`; the
  DTB compiles.  [report 193 §5]
* **The test image `0x307615B3` is on media and proves it on a plain boot
  (later superseded by `0x04AF1433`).**
  Built by re-wrapping the existing zImage with the new DTB (`0x91A300` bytes,
  no kernel rebuild), flashed **region 0 only** (`COMPLETE 0x40C0 -> 0x4006`,
  ~588 frames); the unit enumerated in 20 s.  With **no `devmem`**, the plain
  boot reads `0x4A100126`/`0x4A100128` = `0x0110`, `i2cdetect -y -r 1 0x65
  0x65` shows `65`, and `i2ctransfer -y 1 w1@0x65 0x00 r1` / `… 0x01 r1`
  return `0x00` / `0x09`.  `BOOTCTR_FCTRY = 0x2e` (top-up saw `>= 0x1e`, no
  write); pack Full 4.155 V.
* **The touch boots into its Munin bootloader (report 194, DECISIONS §45).**
  Live `reg1 = 0x09` (bit3 = bootloader, bit0 = just-reset); the 21-byte block
  is `status 0x09, bootloader ID 0xB0 (Pegasus), ver 0x05, app start 0x900,
  checksum 0xB72F, app version 0x020A, silicon 0x0586, build 0x3B`.  The
  vendor boot `reg0=0x81 → read 21 bytes at reg1 → reg0=0x80 → reg0=0x02 →
  reg 0x1D=1 → reg 0x1F=1` was run live; the app came up (status `0x10`) and
  the **18-byte report** at `reg1` is a status byte + four 4-byte contacts
  (`X=b0|((b2&0x0F)<<8)`, `Y=b1|((b2&0xF0)<<4)`, `b3`=down, `0xFFF`=none),
  all four sentinels with no finger.  **No firmware upload is needed:** the
  chip's app/build match `Pegasus_50.hex` (= `Pegasus2_50.hex`; `D4_A` is the
  older build `0x35`).
* **Driver decision and result (DECISIONS §45):** a **new small input
  driver**, not `cyttsp_core` (Gen2) or `cyttsp5` (Gen5).
  `drivers/input/touchscreen/playbook-munin.c` + the DTS `touchscreen@65`
  node + `CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`.  On image `0x04AF1433`
  (0x91A8C4, on media) it **works**: `input0` at 2.379 s, the boot block
  `0xb0/0x020a/0x3b` at 2.958 s, `ready` at 3.259 s, and touching the panel
  produces `event0` (`ABS_MT_SLOT`, `ABS_MT_POSITION_X/Y`, `ABS_X/Y`; the
  observed touch values were X ≈ 368–374, Y ≈ 170–261).  It
  **self-enumerates in 26 s**.  The intermediate `0x31AD9A77` image, which
  retried inside `probe()`, broke USB: the gadget bound at 5.78 s but the host
  configured it only after a physical replug at 74.7 s; 0x04AF1433 is the fix.
  The probe must not block — the boot now runs in a delayed work (trap 141).
* **Bench state:** unit on the touch image `0x04AF1433`, `/dev/ttyACM0` mode
  666 after each `pkexec chmod`; no door holder (our Linux image).  No
  RPMB/NVRAM/MMC write; the media writes were the region-0 image flashes
  (`0x307615B3`, `0x86024669`, `0x31AD9A77`, `0x04AF1433`).  The touch app
  start/init writes were to the touch only; they revert on a power cycle.

## Inferred (reasoning from evidence, not directly measured)

* **The missing piece was host-side, not the touch chip.**  The chip answered
  as soon as the controller's pads were connected, with no reset or firmware
  upload; the same part ACKed under QNX where startup-playbook muxes the pads.
  *Strength: strong (measured).*  [report 193 §4]
* **`tp_intr = 1035` is GPIO2_3.**  QNX numbers GPIO interrupts as
  `1000 + gpio` (report 180 used `0x41d = 1053 = 1000 + 53`), so 1035 = gpio 35
  = GPIO2_3.  *Strength: moderate — the convention is attested, this instance
  is not independently read.*  [report 193 §2]
* **This is the same class as the WL1283 QNX-only GPIOs.**  QNX's
  `startup-playbook` configures board pads/GPIOs; our chainloader never runs
  it, so a bus can be enabled but unwired.  *Strength: strong.*  [trap 135,
  trap 138]

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The touch is on I²C3 `0x48060000` because `libmtouch-pegasus` opens
  `/dev/i2c2`."**  False: the board selects `driver = munin`, whose library
  opens `/dev/i2c1`; Pegasus is not used.  [report 193 §1]
* **"Scan `i2cdetect -y -r 2` and `-r 3`."**  The wrong adapters: the touch is
  on `i2c-1`.  [report 193 §1]
* **"The touch needs the decoded reset/power pulse before it will answer."**
  It was already powered and out of reset; the pulse changed nothing, and the
  pad mux alone made `0x65` ACK.  [report 193 §4]
* **"A full-bus `i2cdetect` is a safe first probe."**  With an unmuxed bus
  every transfer times out and the scan blocks the ttyGS0 console.  Probe one
  address.  [trap 138]
* **"`i2cdetect -y` (quick-write) will show `0x65`."**  The OMAP adapter
  rejects the SMBus quick command, so the tool skipped the cell; use
  `-y -r` with an address range, or `i2ctransfer`.
* **"`i2ctransfer w0@0x65` is a safe address-only probe."**  The OMAP driver
  returns `Operation not supported` for a zero-length transfer.
* **"`/base/usr/bin/touch` is the touch daemon."**  It is the POSIX `touch`;
  the touch stack is `libmtouch-munin` loaded by `screen`, selected by
  `graphics.conf`.  [report 176's wording corrected in report 193]
* **"Enabling `&i2c2` in the DTS is enough."**  No: without a pin group the
  pads stay in safe mode and the bus times out.  [trap 138]
* **"The chip is faulted / needs a firmware upload before it answers."**
  False: it boots into its Munin **bootloader** by design.  The 21-byte block
  shows app version `0x020A` / build `0x3B`, matching `Pegasus_50.hex`, and
  `reg0 = 0x80` starts the resident app; no upload was needed.  [report 194]
* **"It is a mainline `cyttsp` Gen2/Gen5 part."**  False: the Gen2/Gen5
  register maps do not match; the protocol is RIM's Munin.  [DECISIONS §45]
* **"A retry loop inside `probe()` is harmless."**  It is not: 40 × 50 ms in
  the built-in probe moved the boot and the host missed the gadget connect
  (`0x31AD9A77`: configured at 74.7 s, only after a replug).  The boot now runs
  in a delayed work.  [trap 141]

## Unknown / open questions

* **The axis map/orientation for the 1024×600 panel.**  The report gives
  12-bit X/Y; the 1917 `scaling.conf` has a 1024×600 calibration.  *Would
  settle it:* a touch test on the panel or the scaling config.
* **The status/counter and `0x1D`/`0x1F` semantics.**  Only the active defaults
  (`0x1D=1` four-finger, `0x1F=1` threshold) are proven.  *Would settle it:*
  the vendor power/mode code or a touch test.
* **Which ball is GPIO4_8?**  The reset line's pad was not mapped; it may have
  been a no-op even if needed.  *Would settle it:* the startup pad table or the
  TRM ball map.
* **Is `tp_intr = 1035` really GPIO2_3?**  *Would settle it:* read the QNX
  interrupt attachment or the board pad table.
* **Why does the direct probe read at ~2.7 s return `-EREMOTEIO` while the
  boot work 500 ms later succeeds?**  The controller is in bootloader and the
  bus is muxed; the settle/clock dependency is not measured.  *Would settle
  it:* a timestamped read loop from early boot, or the i2c controller's
  runtime-PM/clock state at probe.
* **Why does the 18-byte read start with `0x00` while a single `reg1` read
  shows `0x10`?**  The first byte of the multi-byte read is a counter/status
  snapshot; the vendor's parity check uses `buf[17]` and `buf[0]`.  *Would
  settle it:* the Munin report spec or a scope of the register reads.

## Superseded / uncertain

* **"The touch bus is unresolved (I²C2 vs I²C3)."**  Resolved this session:
  I²C2 `0x48072000` (Linux `i2c-1`).  [report 193, DECISIONS §44]
* **"The touch can be probed with `i2cdetect -y -r 2`/`-r 3`."**  Wrong
  adapters; the bus also needed its pads muxed first.  [trap 138]
* **"`/base/usr/bin/touch` is RIM's touch daemon."**  It is the POSIX utility;
  the driver is a `screen` module.  [report 193 §1]
* **"The WL1283 is still open."**  The operator formally stopped it at the
  start of this session; do not repeat traps 116–137.
* **The intermediate images `0x86024669` (probe read failed) and `0x31AD9A77`
  (blocking retry; USB regression) are superseded by `0x04AF1433`; the bus
  test `0x307615B3` is superseded too.**

## Artefacts produced

* Report **193** — `docs/reports/193-touch-bus-and-pad-mux-2026-11-08.md`.
* Report **194** — `docs/reports/194-touch-munin-protocol-2026-11-08.md`
  (the bootloader block, the boot/init sequence, the report format, the
  firmware files, the driver decision).
* The contradiction `docs/contradictions/touch-bus-i2c2-vs-i2c3.md` is
  **resolved** (README index updated; the "still open" list is now five).
* Decision **§44** (touch on I²C2; mux the pads) and **§45** (Munin protocol;
  write a small driver, no firmware upload); traps **138** (an enabled
  controller is not a wired bus), **139** (patch walk picks up build
  artifacts), **140** (the touch boots into its Munin bootloader) and **141**
  (a blocking probe retry breaks USB warm-reset enumeration).
* DTS: `kernel/source/omap4-winchester.dts` and
  `local/kernel-tree/arch/arm/boot/dts/ti/omap/omap4-winchester.dts` —
  `i2c2_pins` + `pinctrl-0` on `&i2c2`; `omap4-winchester.dtb` builds.
* `STATE.md` (session-36 state), `HANDOFF.md` (session-37 kickoff),
  `ROADMAP.md` (step 4 stopped, step 5 started).
* The 7.3.3 patch was re-exported at each step; the final export is **7 620
  lines / 48 files**, sha256 `28e48f56…`, 0 rejects and 48/48 files
  byte-identical against pristine 7.3.3.  The tree walk otherwise picks up
  build-generated sources (trap 139), so the export used the previous patch's
  source set plus the new driver.
* **No RPMB/NVRAM/MMC write.**  The media writes were the region-0 image
  flashes: `0x307615B3`, `0x86024669`, `0x31AD9A77` and the final
  **`0x04AF1433`** (on media); the known-good `0xE36C679A` remains the way
  back.
* **Munin driver + image:** `drivers/input/touchscreen/playbook-munin.c`
  (input multitouch, the `0x81 → 0x80 → 0x02 → 0x1D/0x1F` boot, the 18-byte
  report, a 20 ms poll pending the IRQ pad), the DTS `touchscreen@65` node,
  and `CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`.  Images: `0x86024669` (probe
  read failed), `0x31AD9A77` (blocking retry; USB regression), **`0x04AF1433`**
  (non-blocking; works, on media).  The patch is **7 620 lines / 48 files**,
  sha256 `28e48f56…`, 0 rejects and 48/48 byte-identical.

## Next

Start the **WM8994 audio half** of step 5 (enable `CONFIG_MFD_WM8994` /
`CONFIG_SND_SOC_WM8994`, probe the codec at I²C4 `0x1a`, then the McBSP/`l4_abe`
machine link).  Touch polish: the axis calibration/orientation for the
1024×600 panel and the IRQ pad.  The unit is on the touch image `0x04AF1433`
(counter `0x2b`); re-flash `local/images/kernel-0xE36C679A-wdt58s.img` at block
`0x825` to return to the known-good baseline.
