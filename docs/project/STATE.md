# Current state

## Current state — 2026-11-09 (session 37)

**The session-36 direction is done: the WL1283 code is archived out of the
active tree and patch (Task 1), the board DTS is re-derived from 2.1.0.1917
(Task 2, report 195), and the minimal 1917-truth `&mmc5` host was booted
(Task 3) — **CMD5 is still negative** (report 196), so the DT model is not the
missing piece.  The per-unit **WLAN calibration was found in the QNX `hd5`
partition** (report 197).  The unit ends on **TabletOS 2.1.0.1917** (region 0 =
the 1917 IFS, `ti0` UP, `BOOTCTR_FCTRY = 0x32`); the Linux Task-3 image
`0x0DC36A11` is the way back.**

* **Archive:** `kernel/archive/wl1283/wl1283-instrumentation-7.3.3.patch`
  (517 lines) + `README.md`.  It is the exact diff `clean tree → pre-archive
  snapshot` for six files and **round-trips** (applied to a copy of the tree,
  all six files reproduce byte-for-byte).  It carries the SDIO CMD5
  `arg=0x00ffff00` quirk (`sdio.c`, `host.h`), `pb_sdio_ocr_quirk`,
  `pb_init_hz`, `pb_enable_fref_clk2`, `pb_wl1283_reset_pulse`, the
  `PBCMD`/`PBCLK`/`PBD`/`PBOCR`/`PBVOLT` prints, the `480d5000` PBGPIO dump,
  and the DTS `vwl1283`/`sdmmc5_pins`/`fref*`/`wl1283_ctrl_pins`/
  `wl1283-en-hog`/`&mmc5` nodes.
* **Scope (operator, session 37):** named WL1283 code only.  The general eMMC
  bring-up prints (`PBM`/`PBINI`/`PBSIO`/`PBX`/`PBREQ`/`PBIRQ` and `pb_event`)
  stay; KERNEL-PORT §7.2's full PB* prune is still a separate change.
* **Base patch:** `kernel/patches/0001-playbook-winchester-7.3.3.patch` is
  **7 305 lines / 46 files (36 modified + 10 new)**, sha256 `d0d00009…`,
  **0 rejects and 46/46 byte-identical** against pristine 7.3.3.  `sdio.c` and
  `host.h` are pristine again, so they left the patch.
* **DTS:** the re-derived minimal `&mmc5` (report 195 §3.4) is now in the tree
  and **enabled** (`status = "okay"`): `sdmmc5_pins`, `wl1283_ctrl_pins`
  (WL_EN pad `0x07c`), `wl1283-en-hog` (gpio 22), `vmmc-supply = <&vwl1283>`
  (1.8 V OCR window), 4-bit, `non-removable`, `ti,non-removable`, `no-sd`,
  `no-mmc`, `max-frequency = <24000000>`; WL_IRQ `0x07a` stays in
  `bridge_pins`.  It probes and **fails CMD5** (report 196).  `kernel/source/`
  and `local/kernel-tree/` are identical.
* **Build:** clean (`make zImage modules`, no warnings); the 1917-truth
  `&mmc5` image is **`0x0DC36A11`** (`0x91A514` bytes, kernel `7.3.3 #24`),
  flashed **region 0 only** (588 frames, `COMPLETE`).  The earlier
  WL1283-free build `0x05819994` (mmc5 disabled) is superseded.
* **Task 3 result (report 196):** `mmc4` (480d5000) probes with
  `f_max=24000000` but 400 kHz init; `sdio_reset`(CMD52) → `go_idle`(CMD0) →
  `attach_sdio`(CMD5) → **`attach_sdio no` at 2.829 s**, `Failed to initialize
  a non-removable card` at 2.831 s; no voltage warning, so the failure is at
  CMD5.  eMMC (`mmcblk1` MCGAFA 59.4 GiB), the Munin touch (`event0`, boot
  `0xb0`), bma150 and the loop-root are all healthy.  Log
  `local/logs/boot-0x0DC36A11-1917mmc5.log`.  **The redo reproduced the old
  state.**  `devmem` confirms the DT model is applied and 1917-identical
  (`0x148/14C/150 = 0x0118`, `0x07C = 0x0003`, `0x07A = 0x410B`) with
  `GPIO2_DATAOUT = 0x31413200` (WL_EN high) but `DATAIN = 0x01000010`
  (WL_IRQ bit21 = 0): the chip is not up (report 188, reconfirmed).
* **Media / unit:** region 0 = **the 1917 IFS** (`ifs-shim-sealed.ifs`,
  restored for the calibration check; TabletOS 2.1.0.1917 is running, `ti0`
  UP with 40+ scans, MAC `14:74:11:8f:9c:c3`, `BOOTCTR_FCTRY = 0x32`), region 1
  = the 7.3.3 charger rootfs (`ba275807…`, 17 modules).  The Linux Task-3
  image is `0x0DC36A11` (reflash region 0 to return).  The known-good
  `0xE36C679A` / touch `0x04AF1433` remain.
* **New lead (operator) — CHECKED (report 197):**  QNX `hd5` (512 KiB) is the
  **factory/calibration partition**: unit serials + 2011-04 factory timestamps,
  a 19 118-byte **TI WL128x radio-parameters INI** (TxPower, XTAL trim, TX BiP
  reference, FEM/trace insertion loss, 2.4/5 GHz TXPD tables) and a 44 405-byte
  binary calibration blob; `hd4` is the MCT header, `hd6` empty.  The assigned
  WLAN MAC is in NVRAM `deviceinfo` (`1474118f9cc3`).  Extracted to
  `local/work/1917/calib/`.  This is the NVS/calibration source for the
  post-enumeration `wl12xx` path; it cannot fix CMD5.
* **Next (operator's call):** (a) build `wl128x-nvs.bin` from the hd5
  calibration (report 197) for the post-enumeration path; (b) VBAT/TCXO/SLOW_CLK
  + MMC5_CLK/CMD/DAT0 (needs a board view or logic analyser); (c) the WM8994
  codec-ID experiment (HANDOFF §0d).  Session note
  `docs/sessions/session-37.md`; reports 195/196/197; DECISIONS §46.
* **Task 2 (the 1917-truth DTS audit) is written up:** report **195**.  Node by
  node with provenance classes, and the `&mmc5` re-derivation (pads `0x148`–
  `0x152` = mux 0/pull-up, WL_EN `0x07c` mux 3, WL_IRQ `0x07a` mux 3 input,
  4-bit, 24 MHz, no host rail, VIO 1.8 V, no `ti,dual-volt`, no pwrseq), each
  cited to the 1917 `/base`/`pidin`/`devnp-ti1283.so`.  The header is corrected
  (no longer 2.0.0.4869) and two stale comments fixed (I²C1 = `0x48070000`;
  I²C3 is not the touch bus).  The re-derived `&mmc5` (report 195 §3.4) is now
  in the tree and enabled (Task 3).  Patch re-exported after the comment
  edits: 46 files, 7 305 lines, sha256 `d0d00009…`, 0 rejects / 46/46 identical.
* **The 1917 `/base` was mounted read-only this session** (`bb10mt qnx6 mount`
  of `local/work/rcfs-1917.rcfs`) and the pad/GPIO tables, `graphics.conf`,
  `tiwlan_winchester.ini` and `board.tdf` read directly; the 1917
  `devnp-ti1283.so` matches `local/work/qnx-libs` (`a5fd35de…`).  gpio 53/54
  confirmed; report 189's `gpio_config` field labels corrected (KNOWN-ISSUES
  §3.22).  **Persistent extracts** (operator suggestion): `local/work/1917/base`
  (the whole `/base`, 333 MB) and `local/work/1917/ifs`
  (`proc/boot/startup-playbook` extracted, sha256 `bc7d3a41…`) — no remount
  needed next time.  startup-playbook's Winchester GPIO table (image
  `0x80122750`) is byte-identical to the hwio `gpio_config` (gpio 53
  input/falling, gpio 54 output/low).

## Previous state — 2026-11-08 (session 36)

**Item 9 step 5 has started, and the touch bus is solved.  The Cypress touch is
on OMAP I²C2 `0x48072000` — QNX `/dev/i2c1`, Linux `i2c-1` — at slave `0x65`,
because the board's `graphics.conf` selects `driver = munin` and
`libmtouch-munin` opens `/dev/i2c1`.  The Linux bus was dead for a second,
independent reason: our DTS enabled `&i2c2` but never muxed its pads, which sat
in safe mode (`0x4A100126`/`0x4A100128` = `0x011F`).  Writing them to `0x0110`
(mux 0, input, pull-up) made `0x65` ACK immediately.  The fix is in the DTS
(`i2c2_pins`), and the **Munin driver works**: the test image `0x04AF1433` is
on media, the controller boots to app mode and `input0` reports touches.**

* **Bus resolution (report 193).**  `graphics.conf` (1917 `/base`,
  `usr/lib/graphics/omap4430/graphics.conf`) has
  `begin mtouch / driver = munin / options =
  i2c_lib=i2c-resmgr-client,tp_intr=1035,i2c_slave=0x65`.  The Munin library
  carries `/dev/i2c1` (Pegasus has `/dev/i2c2`); QNX `pidin ar` maps
  `/dev/i2c1` to `0x48072000` and report 176's scan found `0x65` there.  Live
  Linux: `i2c-0` = `0x48070000`, **`i2c-1` = `0x48072000`**, `i2c-2` =
  `0x48060000`, `i2c-3` = `0x48350000`.  The old "scan `-r 2`/`-r 3`" plan was
  aimed at the wrong adapters.  The `touch-bus-i2c2-vs-i2c3` contradiction is
  resolved.
* **Root cause of the dead bus (trap 138).**  `0x4A100126` (i2c2_scl) and
  `0x4A100128` (i2c2_sda) read `0x011F` = mux 7 safe mode, versus `0x0110` on
  the working I²C1 pads.  The symptom was a **timeout** (`Connection timed
  out`), not a NAK (`Remote I/O error`), and a whole-bus `i2cdetect -y -r 1`
  blocked the console.  One 16-bit `devmem` write per pad to `0x0110` fixed it:
  `i2cdetect -y -r 1 0x65 0x65` showed `65` and
  `i2ctransfer -y 1 w1@0x65 0x00 r1` returned `0x00`.  This is startup-playbook
  board-pad setup that our chainloader never runs (same class as trap 135).
* **Munin wiring** (`winchester_tp_init` `0x9120`, `winchester_tp_reset`
  `0x90a0`): GPIO1 `0x4A310000` and GPIO4 `0x48059000` are mapped; reset =
  **GPIO4_8** (CLEARDATAOUT/SETDATAOUT `0x100`, then released after the power
  pulse), power = **GPIO1_14** (`0x4000`, low 500 ms → high 500 ms).  Live both
  were already output-high (`GPIO1_DATAOUT = 0x00006044`,
  `GPIO4_DATAOUT = 0x90000120`), and the part answered from the pad mux alone —
  no reset pulse was required.  `tp_intr = 1035` = GPIO2_3 by the QNX
  `1000 + gpio` convention (inferred).
* **Touch protocol decoded (report 194, DECISIONS §45).**  The chip boots into
  its **Munin bootloader**, not an app: live `reg1 = 0x09` (bit3 = bootloader,
  bit0 = just-reset) and the 21-byte block is `status 0x09, bootloader ID 0xB0
  (Pegasus), ver 0x05, app start 0x900, checksum 0xB72F, app version 0x020A,
  silicon 0x0586, build 0x3B`.  The vendor boot is `reg0=0x81` → read 21 bytes
  at `reg1` → `reg0=0x80` (start app) → `reg0=0x02` → `reg 0x1D=1` →
  `reg 0x1F=1`.  Run live in this order, the app came up (status `0x10`) and
  the report at `reg1` is **18 bytes**: a status byte + four 4-byte contacts,
  `X=b0|((b2&0x0F)<<8)`, `Y=b1|((b2&0xF0)<<4)`, `b3`=down, `0xFFF`=no contact
  (all four sentinels with no finger).  **No firmware upload is needed:** the
  chip's app/build match `Pegasus_50.hex` (= `Pegasus2_50.hex`; D4_A is older).
  **Decision: a new small input driver, not `cyttsp_core`/`cyttsp5`.**
* **DTS change:** an `i2c2_pins` group (`OMAP4_IOPAD(0x126, 0x0110)` /
  `(0x128, 0x0110)`) and `pinctrl-0 = <&i2c2_pins>` on `&i2c2`; `&i2c3` is
  left enabled-and-unmuxed (empty, one variable).  Copied to
  `local/kernel-tree`; the DTB builds.  The 7.3.3 patch now includes the
  `i2c2_pins` change and the Munin driver: **7 620 lines / 48 files**, sha256
  `28e48f56…`, applied to pristine 7.3.3 with 0 rejects and 48/48 files
  byte-identical (trap 139: the tree walk otherwise includes build-generated
  sources).
* **Test image `0x04AF1433`** (`0x91A8C4` bytes; the touch driver) was flashed
  to **region 0 only** and booted; it **enumerates by itself (26 s)** and the
  driver boots the controller.  The earlier bus test `0x307615B3` and the two
  intermediate driver images (`0x86024669`, `0x31AD9A77`) are superseded.  The
  known-good baseline remains `0xE36C679A`; this test image is the current
  media (region 1 unchanged).
* **Unit:** region 0 = **`0x04AF1433`**, region 1 = the 7.3.3 charger rootfs
  (`ba275807…`, 17 modules); the 1917 IFS is not on media.
  `BOOTCTR_FCTRY = 0x2b` (three test boots; the boot-time top-up saw
  `0x2b >= 0x1e`, no write).  Pack Full (100 %, 4.156 V).
* **The Munin driver WORKS.**  `drivers/input/touchscreen/playbook-munin.c`
  (input multitouch, 1-byte register I2C, the `0x81 → 0x80 → 0x02 → 0x1D/0x1F`
  boot, the 18-byte report, a 20 ms poll pending the IRQ pad) plus the DTS
  `touchscreen@65` node and `CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`.  On image
  **`0x04AF1433`** (`0x91A8C4` bytes, on media): `input0` registers at
  2.379 s, the boot work logs `Munin bootloader id 0xb0 … app 0x020a build
  0x3b` at 2.958 s, `ready` at 3.259 s, and touching the panel produces
  `event0` (`ABS_MT_SLOT`, `ABS_MT_POSITION_X/Y`, `ABS_X/Y`, SYN).  The
  **probe must not block** (trap 141): the boot runs in a delayed work, or the
  USB gadget misses its connect.  The 7.3.3 patch is **7 620 lines / 48
  files**, sha256 `28e48f56…`, 0 rejects and 48/48 byte-identical.
* **Next:** the WM8994 audio half of step 5, plus the touch axis
  calibration/orientation and the IRQ pad.  Report **194**, traps
  **138/139/140/141**, decisions **§44/§45**.

## Previous state — 2026-11-08 (session 35)

**S1, S2, S3 and H1 are all done; the WL1283 software surface is now
exhausted.**  S1: `startup-playbook` is the source of the QNX-only GPIO outputs
(report 192).  S3: on the live 1917 every one of the eight QNX-only lines
(GPIO1 0/4/15/18/28/31, GPIO2 5/7) released to input leaves `ti0`
UP/RUNNING and `DATAIN` bit21 = 1 — **all negative** (trap 136).  S2: both-OS
reads show **`CLK32KG`'s state is `0x05` = ON in both** (group/transition differ,
the known non-switching difference); `0xB0`–`0xB2` is **REGEN2** (display rail);
`0xBC`–`0xBE` is an unnamed non-regulator resource, not `CLK32KG`.  **H1 is
negative:** image `0x29784F48` sent CMD0 → CMD5 only at 24 MHz with arg
`0x00FFFF00` and the card still timed out (`err=-110`; `HCTL=0x00000b00`,
`CAPA=0x04e10080`) — the host sequence is not the difference either (trap 137).
The test image was replaced by the known-good `0xE36C679A`; region 1 unchanged;
`BOOTCTR_FCTRY = 0x2f` (two bench boots), pack Full 4.152 V; the source hunks
were reverted, so the tree/patch are at the `0xE36C679A` baseline.  Report
**192** has the evidence.

* **Answer to HANDOFF §1.4 / report 191 §1.3:** startup-playbook configures the
  board pads and GPIOs from runtime-selected tables (`FUN_8010ccf4` / `FUN_8010cb9c`;
  20-byte pad records, 32-byte GPIO records with `+4` direction, `+8` level,
  `+0xc` detect, `+0x14/+0x15` rev, `+0x16` phase).  The Winchester GPIO table is
  at image `0x80122750` (75 records); variants at `0x80127250` (66) and
  `0x80128d64` (43).  The `151/152` and `151/163` strings are the other boards.
* **The QNX-only outputs are `gpio 0/4/15/18/28/31` and `37/39`, all
  output-low**; `gpio 54` is output-low (the driver raises it later) and
  `gpio 53` is input/falling/pull.  This *confirms* report 191 §1.1: the OE
  difference is real and is a startup action.
* **WDT2:** startup writes `WLDR = 0xFFE2B400` through the `WSPR`
  stop/start and `WWPS` polling — the same 58.6 s policy as decision §38.
* **S3 (live 1917, one boot) — all eight negative.**  Baseline
  `GPIO1_OE=0x6ffb1faa`, `GPIO2_OE=0xce3ecd5f`, `DATAIN=0x08201018` (bit21=1).
  `gpio31` (`gpio_wk31`, WKUP `0x4A31E058` mux 3) was checked in detail: with
  the pad input buffer enabled `DATAIN` bit31 = 0, the OE write to input
  (`0xeffb1faa`) stuck at +1/+2 s (no re-drive), the pin stayed low (external
  pull-down).  The other seven flips likewise left `ti0` RUNNING, `DATAIN`
  bit21 = 1 and `wifi_power::on`; each restored.  **None is the WLAN
  enable/reset/power** (trap 136).
* **S2 (both OSes) — `CLK32KG` APP-ON in both; `0xB0` is REGEN2; `0xBC`
  unnamed.**  `0x8C/0x8D/0x8E` = `CFG_GRP`/`CFG_TRANS`/`CFG_STATE`: QNX
  `01/05/05`, Linux `07/05/55`; `CFG_TRANS` (`0x8D`) is identical, `CFG_STATE`
  (`0x8E`) has APP bits = ON in both (the differing bits are CON/MOD), and
  `CFG_GRP` differs (report 180's known difference, labels corrected).  `0xB0`–`0xB2` = REGEN2 (QNX `00/01/00`, GPIO-controlled on
  DVT3+; Linux `07/05/55`); REGEN1/2 are the display/panel rails.  `0xBC`–`0xBE`
  = QNX `07/05/55`, Linux `07/01/00` — an unnamed non-regulator resource, not
  `CLK32KG` (raw captures `docs/captures/twl6030-{linux,qnx}-2026-11-08.txt`).
* **H1 (image `0x29784F48`) — negative.**  Four hunks: `MMC_CAP2_NO_SDIO_RESET
  (1<<30)`, skip `sdio_reset()` when set, `480d5000` gets
  `NO_SDIO_RESET|NO_SD|NO_MMC` (never `NO_SDIO`), and
  `init_hz=24000000`/`sdio_ocr_quirk=1`.  Trace: `sdio_reset skipped` →
  `go_idle` → `CLKD=2 actual=24000000` → `op=0 err=0` → `attach_sdio` →
  `op=5 arg=00ffff00 err=-110 resp0=0` → `attach_sdio no`; no CMD8/CMD55/CMD1.
  `HCTL=0x00000b00` (SDVS18|SDBP), `CAPA=0x04e10080` → H2 verified.  Reverted;
  log `local/logs/boot-H1-0x29784F48.log`.
* **Next:** only the chip-side measurements remain (VBAT/TCXO/SLOW_CLK,
  MMC5_CLK/CMD/DAT0) — a board view or logic analyser.  Do not repeat traps
  116–137.

## Previous state — 2026-11-08 (session 34)

**The QNX-side WL1283 RE is complete and names no WLAN rail, clock or extra
pad.  The operator has kept the item open (session 34) and asked for a
fresh-eyes dossier (report 190) plus two external reviews (report 191).  The
reviews found a real GPIO OE difference (QNX-only outputs), the unaudited QNX
`startup-playbook` binary and the TWL `CLK32KG` lead, and corrected the
SDVS/`vmmc` reasoning.  The unit is back on the known-good Linux image
`0xE36C679A` and charging.**

* **Every HANDOFF §1.4 target is read (report 189).**  `hw_info_pps` publishes
  only `/pps/services/hw_info/inventory`, built at runtime from
  `confstr(_CS_MACHINE)`, `/dev/hd0` devctls, `/proc` and ALSA; it has **no
  WLAN node and no HWI library**, and there is no static hwinfo database in
  the 1917 trees.  The `WLAN` node is in the **syspage**, and the live dump is
  `WLAN_EN_GPIO=54,WLAN_INT_GPIO=53` with an empty `location` tag — no rail,
  clock or second enable.
* **The board pad/GPIO database contains only the six known lines.**
  `hwio-config-winchester.so` (`pad_config` 248 × 20 B, `gpio_config`
  72 × 32 B; resolver in `hwio-omap4.so`): sdmmc5 pads at mux 0 (signals
  `0x52`–`0x57`), WL_IRQ `0x07A` mux 3/signal `0x59`, WL_EN `0x07C`
  mux 3/signal `0x58`, 0x07E mux 3/signal `0x5D`, FREF2_OUT `0x19C` mux 0,
  and WKUP `0x4A31E058` **mux 3 on both halves**.  GPIO 53 is
  input/pull/falling, GPIO 54 output/no-pull; no other GPIO maps to a WLAN
  signal.
* **`devb-mmcsd-winchester` has a powerman client but only `-power on/off`.**
  It requests no voltage, clock rate or bus power; its init sets
  `SDBP = 1`/`SDVS18`, enables `CM2_L3INIT` MMC1/2 module clocks and clears
  `PRM_L3INIT` `LOSTCONTEXT`, but writes no CM divider/DPLL and touches no
  regulator.  Its base constants are MMC1/MMC2 only; the WL1283 is on MMC5
  (owned by `devnp-ti1283.so`).
* **`devpm-omap4.so` registers only `iva_opp`.**  No `system/power/dev`
  literal, no MMCHS5/`CM_L4PER_HSMMC5` write and no WLAN rail; the
  `sdmmc5_*`/`gpmc_*` names are mmap-only globals, never read, and the OSWR
  wake table (Winchester2) uses the WL_IRQ pad only on OSWR entry/exit.
* **The WLAN `.ini` clock/power fields are firmware-time:** `TCXO_Clk = 1`
  (26 MHz), `STRFRefClock = 2` (38.4 MHz wired-OR), settling 5 ms each,
  `ClockValidOnWakeup = 0`, `TCXO_LDO_Voltage = 0` (2.5 V internal LDO),
  `Platform_configuration = 2` (4 mA SDIO drive, no level shifter/EEPROM).
* **The live 1917 boot confirmed the chip works on QNX:** `/pps/services/
  wifi/status` has `wifi_power::on`, `wifi_interface::ti0`, MAC
  `14:74:11:8f:9c:c3` and 40+ scan results; `ifconfig ti0` is UP/RUNNING;
  `sloginfo` shows `TIWLAN SDIO probe WAS CALLED`, `SDIO clock ... 24Mhz`,
  `sdioAdapt_ConnectBus: Send commands sequence: 0, 5, 3, 7`.
* **The operator's Linux RPMB top-up test succeeded** (the boot-time
  `pbrpmb topup` wrote once at/below the threshold and the value read back);
  the 1917 boot then topped `BOOTCTR_FCTRY` to `0x32`, and the restored Linux
  boot reads **`0x31`** (`0x31 >= threshold 0x1e, no write`).
* **Media:** region 0 = **`0xE36C679A`** (the known-good 7.3.3 image with the
  ISL9519 charger and the 58.6 s WDT2 policy, restored after the QNX read);
  region 1 = `rootfs-region1-7.3.3-charger.ext4` (`ba275807…`, 17 modules).
  The 1917 IFS is **not** on media.  Pack **Full, 4.126 V, +265 mA,
  `Charging`** (ISL9519 `STATUS=Charging`, 640 mA / 4208 mV / 384 mA).
* **The 7.3.3 patch is 7 258 lines / 47 files**, sha256 `e7b7fc2e…`, and
  reproduces the tree byte-for-byte; config `kernel/winchester-7.3.3.config`
  (`CONFIG_CHARGER_ISL9519=y`, `CONFIG_KERNEL_XZ=y`).
* **Next: the operator has kept the item open (session 34).**  Report **190**
  is the fresh-eyes dossier; report **191** records two external reviews and
  the verified corrections.  The next work is, in order: (1) audit the QNX
  **`startup-playbook`** binary (GPIO1/GPIO2/TWL/`CLK32KG` init — it never runs
  under our chainloader); (2) decode the TWL **`0xBC`–`0xBE`** (`CLK32KG`)
  group/transition/state and the other 9 differences; (3) bisect the
  **QNX-only GPIO outputs** on the live 1917 (GPIO1 bits 0/4/15/18/28/31,
  GPIO2 bits 5/7) one at a time; (4) run **H1**, the exact QNX sequence —
  CMD0 → CMD5 only, no CMD52 *and no CMD8*, 24 MHz, CMD5 arg `0x00FFFF00`
  (this needs the session-33 `NO_SDIO_RESET` quirk re-added; **do not set
  `MMC_CAP2_NO_SDIO`**).  **H2 is corrected:** MMC5 has no `ti,dual-volt`, so
  the 3.3 V `vmmc` is an OCR advertisement only; do not blindly revert it —
  verify `HCTL = 0x00000B00` and `CAPA` VS18.  Do not repeat traps 116–120,
  124, 126–134.

## Previous state — 2026-11-08 (session 33)

**The last software layer is closed: the QNX `winchester` power-manager preset
does not switch a WLAN rail or clock, and the chip-side electrical comparison
found no missing board rail.  The chip is alive under QNX (it drives WL_IRQ
high) and not under Linux with the same pads, WL_EN, VIO and command sequence.
The 200 ms MMC5 settle and the CMD52 skip are both negative.  The remaining
difference is at the chip's non-exposed power/clock pins or in OMAP HSMMC
sampling; a schematic/board view or logic analyser is required.**

* **The powerman `winchester` preset has no WLAN rail** (report 187).  Its
  WLAN authorities only gate the standby shutdown/OSWR decision; the
  `ldo_vaux1`/`ldo_vaux3` strings are the display/backlight rails (VAUX3 →
  LM3532, REGEN1/2 → panel).  `devpm-omap4.so` is the PRCM/voltage/DVFS
  plugin, and `winchester_phy_ctrl` is the **USB** PHY.
* **The chip is up under QNX, not under Linux.**  On the bare board with the
  1917 IFS, `ti0` is UP/RUNNING and `GPIO2_DATAIN = 0x08221018` (WL_IRQ bit 21
  = 1).  Under our image with `GPIO2_DATAOUT = 0x31413200` (WL_EN high),
  `DATAIN = 0x01000010` (bit 21 = 0).  `ifconfig ti0 down` on QNX drops both;
  the rightmost front pad follows the IRQ (1.8 V ↔ 0 V) and is the only
  accessible chip-alive indicator.  WL_EN is not brought out.
* **The electrical points match otherwise:** all WLAN pads read as QNX
  (`0x078 = 0x410B4103`, `0x07A = 0x0003410B`, `0x07C = 0x00030003`,
  `0x148/14C/150 = 0x01180118`), VIO is 1.8 V, and the Linux regulator set is
  V1V8/VCXIO/VDAC 1.8 V, VUSB 3.3 V, V2V1/VANA 2.1 V, VAUX1/2/3 2.8 V,
  VMMC/VPP/VUSIM off.  No 3.3 V **chip** rail is exposed; the ~4.7 V point is
  a resistor on the VBUS side.
* **The 200 ms MMC5 post-power-on settle is negative** (image `0x0B38612D`,
  DTB-only `post-power-on-delay-ms = <200>` on `&mmc5`): the clock runs ~400 ms
  before the first command, and CMD52/CMD8/CMD5 still time out (`err=-110`);
  the IRQ stays low.
* **Skipping the SDIO reset is negative** (image `0xAC98BCAD`): QNX's sequence
  is CMD0 → CMD5; Linux sends CMD52 first.  `MMC_CAP2_NO_SDIO_RESET` (bit 30)
  on the `480d5000` host gives CMD0 → CMD8 → CMD5, and CMD5 still times out.
* **The USB re-enumeration regression is the session-32 WL_EN reset pulse.**
  The gadget bound but the host missed the connect; a warm-reboot A/B isolates
  it: `0x90BDE221` (FREF off, pulse on) fails, `0x0353A241` (FREF off, pulse
  off) enumerates by itself in 14 s (gadget configured at 5.28 s).  The FREF2
  enable is excluded and the session-33 MMC changes are not involved.  Both
  experiments are negative for CMD5 and are removed from the tree; the
  `soft_connect` toggle remains a proven fallback (trap 130).
* **The unit is on the final session-33 image `0x0353A241`** (the session-32
  tree with the FREF2 enable and the WL_EN pulse removed; the session-33 MMC
  experiments reverted), saved as
  `local/images/kernel-0x0353A241-final.img` (0x91A434); it enumerates by
  itself on a warm reboot.  Region 1 is
  unchanged (the 7.3.3 charger rootfs).  The known-good image remains
  **`0xE36C679A`**; the 1917 IFS is **not** on media.  `BOOTCTR_FCTRY = 0x27`;
  pack **Full: 100 %, 4.155 V, +5 mA**.  The device is reassembled.
* **The 7.3.3 patch is re-exported:** **7 258 lines / 47 files**, sha256
  `e7b7fc2e…`, 0 rejects and 47/47 files byte-identical against pristine
  7.3.3.
* **Next is the operator's call:** if the WL1283 is formally stopped, item 9
  step 5 (WM8994 audio + CY8CTMA3 touch, report 176) follows.  The WLAN hunt
  needs a schematic/board view or a logic analyser; do not re-run the closed
  experiments (traps 116–120, 124, 126–129).

## Previous state — 2026-11-08 (session 32)

**The WL1283's SoC-side comparison is exhausted: the `FREF_CLK2_REQ` input is
not used by the reference, the one real difference (the `fref_clk2_out`
generator) has been reproduced on Linux, and the chip still does not answer
CMD5.  The remaining gap is at the chip's TCXO/power/reset and needs the
electrical measurement report 184 recommended.**

* **The fresh QNX capture (1917, WLAN up):** `0x4A31E058 = 0x000B0003` — pad0
  **mux 3 = `gpio_wk31`**, input buffer off; the second opinion's mux 1
  (`fref_clk2_req`) is **not selected**.  `0x4A10019C = 0x41000000` (pad0
  `fref_clk2_out`), `0x4A30A314 = 0x00020100` (SCRM AUXCLK1: SYS_CLK/3 =
  **12.8 MHz**, enabled).  Linux had the pad in safe mode and AUXCLK1 = 0.
* **The FREF2 output is validly not causal.**  Images `0xF521AB06` (pad mux +
  gate) and `0x18D49C22` (`AUXCLK1 = 0x00020100`, 12.8 MHz) both booted, but
  the enable was a `late_initcall` (level 7) — **after** the `omap_hsmmc`
  device-init probe (level 6), so the clock was not on for CMD5.  Image
  `0xF38CF87E` moves the enable into `omap_hsmmc_probe()` before
  `mmc_add_host()`; `PLAYBOOK: fref_clk2_out 12800000 Hz enable=0` prints at
  2.567 s, and the card still times out.  The negative is now valid.
* **The WL_EN reset pulse is also negative.**  Image `0xAF4FC103` drives
  GPIO2_22 low for 500 ms and high again in `omap_hsmmc_probe()` before
  `mmc_add_host()` (the reference's `hPlatform_hardResetTnetw`); CMD52/CMD8/
  CMD5 still time out.
* **The QNX SDIO protocol differs and was tested; the software audit settles
  the Linux side.**  QNX enumerates at **24 MHz** (CLKD=2) with **CMD5 arg
  `0x00FFFF00`** (static RE of `devnp-ti1283.so`; no 400 kHz phase).  Linux's
  default is **one try at 400 kHz** (`f_min = f_init = ios.clock = 400000`,
  `CLKD = 120`, actual 400 kHz) with the sequence **CMD52 → CMD0 → CMD8 →
  CMD5(arg 0)**; every command times out except CMD0.  A runtime 24 MHz
  attempt (`SYSCTL=0x000e0087`) and a host quirk with arg `0x00FFFF00` also
  fail.  The card is silent from CMD52 onward — it never responds to any
  command (report 186).
* **The BT driver is not the missing enable:** `devc-seromap-winbt`'s
  `gpio3_base` is GPIO4 (`0x48059000`); its automatic line is GPIO4 bit 28
  (force RTS) high, which Linux matches; its `gpio_set_output` line is GPIO2
  bit 23 (BT_EN), low in both captures.  It enables only the UART2 CM clock
  and touches no PMIC/regulator.
* **The remaining candidate is the 26 MHz TCXO / chip power / reset**
  (`tiwlan_winchester.ini: TCXO_Clk = 1`; `wl128x_boot_clk()` selects it
  unless the HW reports FREF or the TCXO is 16.368/32.736).  The Motorola
  148603 guide says the TCXO is powered from the chip's **internal 1.8 V**:
  VBAT 3.3 V, VIO 1.8 V, WL_EN active high, WL_IRQ active low, SLOW_CLK
  32.768 kHz.  The QNX WLAN driver does not configure or gate any of them.
  **Next: multimeter VBAT/VIO/WL_EN/WL_IRQ/SLOW_CLK at the chip.**  The
  boot-time WL_EN reset pulse was already tested (negative, image
  `0xAF4FC103`).  No logic analyser is available, so the waveform comparison
  is out.
* **The unit is on the session-32 test image `0xAF4FC103`** (FREF enable +
  WL_EN reset pulse in the MMC5 probe, 3.3 V OCR, `PBCMD` trace; the CMD5
  quirk is default-off); the known-good image remains **`0xE36C679A`**.
  Region 1 is unchanged (the 7.3.3 charger rootfs).  The 1917 IFS is **not**
  on media.  `BOOTCTR_FCTRY = 0x2A` (TabletOS topped it to `0x32`; eight Linux
  boots).  Pack **100 %, 4.149 V, `Full`**.
* **The test image boots and charges**, but it is not a "known good": it
  carries the FREF enable, the WL_EN reset pulse, the 3.3 V dummy OCR window,
  the `PBCMD` trace and the (default-off) CMD5 quirk.  Revert to `0xE36C679A`
  for a clean regression.

## Previous state — 2026-11-08 (session 31)

**The board's charger is an Intersil ISL9519 at I²C1 `0x48070000`, slave
`0x09` — not the TWL6030 BCI.  A minimal Linux driver charges the pack at
~0.4 A from a host USB port (`CONFIG_CHARGER_ISL9519=y`).  The kernel also
reprograms WDT2 to the reference's 58.6 s window and pets it every 10 s
(image **`0xE36C679A`**).  Region 0 holds that image; region 1 the 7.3.3
charger rootfs.**

* **The charger, measured on the running 1917 (report 183):** ISL9519
  VendorID `0xFE = 0x0049` ('I'), DeviceID `0xFF = 0x0003`;
  `0x14 = 0x0280` (charge current **640 mA**), `0x15 = 0x1070` (max system
  voltage **4208 mV**), `0x3F = 0x0180` (input current limit **384 mA**),
  `0x44 = 0x0000` (control).  The `/pps/services/power/charger` values match
  exactly (`ChargeCurrent 640`, `MaxSystemVoltage 4208`,
  `InputCurrentLimit 384`, `ChargingState CC`, source `USB HOST`).  RIM's
  `/base/bin/battery_win` drives it (`isl_9519_init`).
* **The TWL6030 BCI is not used by the stock OS:** MAIN_CHARGE still holds the
  bootloader's reset values (`CONTROLLER_CTRL1 = 0x00`, `VOREG = 0x02`,
  `VICHRG = 0x04`, `CINLIMIT = 0x01`); QNX only writes
  `CONTROLLER_INT_MASK = 0xDC` for VBUS/VAC detection.  The session-31 BCI
  probe fix (`0x306E3DA5`, battery-profile node) is correct but cannot charge
  this pack.
* **The pack is healthy:** QNX says **SoC 7 %, 3.683 V, `StateOfHealth 90`,
  `StateOfBattery::OK`**; it charged on the dock.  Linux's `HEALTH=Dead` was
  the gauge's SOC-final flag at 2 %, not a pack fault.
* **Why 1917 does not appear to charge from a PC port:** its host-port input
  limit is **384 mA** while `InputCurrentMonitor` reads **430 mA**, so the
  gauge sits at `AverageCurrent = -16 mA` and the icon stays "not charging".
  The operator remembers 2.0.0.4869 charging from a PC port — a different
  input-limit policy.
* **A minimal ISL9519 driver works (report 183 §3):** `drivers/power/supply/
  isl9519.c` (built-in, `CONFIG_CHARGER_ISL9519=y`, DTS `charger@9` under
  `&i2c1`) verifies VendorID/DeviceID, writes `0x3E`, `0x15`, `0x14`, `0x3F`
  in RIM's order with the **masked field** steps (`0x14`/`0x3F` 128 mA/LSB,
  `0x15` 16 mV/LSB, `0x3E` 256 mV/LSB), and refreshes every 5 s for the part's
  175 s watchdog.  Measured: **BQ27541 `CURRENT_NOW` +358 → +397 mA**,
  `STATUS=Charging`, `VOLTAGE_NOW` 3.775 → 3.794 V, capacity 8 → 9 %;
  register read-back `0x14=0x0280`, `0x15=0x1070`, `0x3E=0x0D00`,
  `0x3F=0x0180`.
* **WDT2 is now the reference's 58.6 s window, petted every 10 s.**  The
  kernel reprograms `WLDR = 0xFFE2B400` at the late initcall via the `WSPR`
  stop/start sequence, polling **`WWPS`** after each write (the TRM requires
  it; fixed delays silently dropped half the sequence), and falls back to the
  2 s pet if the readback does not match.  Verified on image `0xE36C679A` by
  sampling `WCRR`: it reloads every **~10 s**, and the unit is alive past
  275 s.  The expected `PLAYBOOK WDT` dmesg line does not appear on 7.3.3
  (an unexplained printk anomaly — verify by registers).  Trap 115,
  decision §38.
* **Counter:** **`0x2D`** on the current boot (`0x30` → three WDT-test boots).
  Pack now **13 %, 3.842 V, +393 mA, `Charging`**.
* **Patch:** `kernel/patches/0001-playbook-winchester-7.3.3.patch` is now
  **7 103 lines / 45 files** (36 modified + 9 new), sha256 `26babb10…`,
  re-exported from the tree and verified 0 rejects / 0 mismatches.  Config:
  `kernel/winchester-7.3.3.config` includes **`CONFIG_CHARGER_ISL9519=y`**
  (copied to `kernel/patches/winchester-7.3.3.config`).
* **Media state:** region 0 = **`0xE36C679A`** (our Linux image with the
  ISL9519 driver and the 58.6 s WDT2 policy, flashed this session); region 1 =
  `rootfs-region1-7.3.3-charger.ext4` (`ba275807…`, 17 modules).  The 1917 IFS
  is **not** on media; restore it with `local/work/ifs-shim-sealed.ifs` at
  block `0x825`.  Region 0 is the only region that changed in the return trip.
* **WL1283 (step 4): the FREF route is tested and closed.**  Session 31 muxed
  the FREF pad and forced `CM_DIV_M5_DPLL_PER` to the reference's `0x329` with
  WL_EN high; MMC5 still did not answer CMD5 and UART2 HCI still got nothing.
  The write also **changes the DSS clock** (`dpll_per_m5x2_ck` is the DSS
  parent) and glitched the panel; `0x204` recovers it, and the cause was
  isolated command-by-command (trap 116, report 180 §8).  **Do not retry it.**
  The WL1283 now needs the chip datasheet/schematic or a bus capture
  (report 180 §6).
* **Audio/touch (step 5) is untouched.**

## Previous state — 2026-11-07 (session 30)

**The patch set is ported to Linux 7.3.3 and the unit boots it: region-0
`0x0F6AC47F` + region-1 `rootfs-region1-7.3.3.ext4` (`0b3b83e8…`, 15 modules).
Panel clean (operator-confirmed), eMMC and the ext4 root work, and the CM3217
reads 26–30 lux on the new base.  The unit is powered off and docked on the
rapid charger at session end (pack was 2.817 V / 0 %).**

* **The port (ROADMAP item 10) is CLOSED.**  Base = **upstream 7.3.3** (current
  pmaports still has `linux-postmarketos-stable` at 7.3.3-r0 with
  `arch="all !armhf !s390x"`, so the package is not buildable for our SoC).
  `local/kernel-tree` is 7.3.3; `local/kernel-tree-6.15.11` is a byte-exact copy
  of the old working tree; `local/pristine/linux-6.15.11` and the 6.15.11 patch
  are untouched.  Full log: `KERNEL-PORT.md` §7, decision `DECISIONS.md` §33.
* **Patch:** `kernel/patches/0001-playbook-winchester-7.3.3.patch` — **6 648
  lines / 42 files**, applied to pristine 7.3.3 targets with **0 rejects** and
  all 42 files byte-identical to the tree.  It includes the prepared (untested)
  TWL6030 charger node + `twl6030.dtsi` label; the last **tested** image remains
  `0x0F6AC47F`.  Config:
  `kernel/winchester-7.3.3.config` (sha256 `7e0aa261…`).
* **The 6.15.11 patch was incomplete** (trap 106): it omitted `cm3217.c`,
  `leds-fan5702.c`, `pbmon-pair.h`, `pbmon-page.h` and their Kconfig/Makefile
  lines because the export recipe derives its file list from itself.  The 7.3.3
  export fixes this; do not use the old recipe.
* **A real driver bug was found and fixed** (trap 107): `cm3217_remove()` did
  `i2c_get_clientdata()` without probe ever calling `i2c_set_clientdata()`, so
  deleting/unbinding the client oopsed and killed the shell.  Verified fixed by
  an unbind/bind cycle on 7.3.3 (shell survives, lux 30).
* **Image:** region-0 **`0x0F6AC47F`**, 0x91AB20 bytes
  (`local/images/kernel-0x0F6AC47F-7.3.3.img`, sha256 `fcddda8d…`).  The zImage
  is **XZ**-compressed (6 568 696 B); with gzip the assembled chainloader image
  (2.88 MB sealed-IFS base + zImage + DTB) was **0xB8ABB4**, over the
  `0xB00000` region-0 IFS slot, and the builder refused it.
* **Peripherals on 7.3.3:** all 15 modules load — BMA150 (`input0`), HMC5843L,
  MPU3050 (die ID `0E9E`), TMP103 (`hwmon0`), BQ27541
  (`/sys/class/power_supply/bq27541-0`), FAN5702 (`playbook:{red,green,blue}:status`),
  and **CM3217 auto-probes `3-0010`** (IIO `cm3217`, 26–30 lux lights-off).
* **Media state: our Linux image is in region 0 — the 1917 IFS is NOT on
  media** (the same as session 29 end).  `BOOTCTR_FCTRY` read **`0x2E`** at the
  final planned boot, then the unit **warm-reset on its own** (12:50:17, power
  brownout — see below) and the post-reset boot read **`0x2D`**.  `pbrpmb topup`
  saw `>= threshold 0x1e, no write` both times.  Restore 1917
  with `tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs
  --dest-block 0x825 --from-bootrom --password 0000 --apply` and re-read the
  counter.
* **BATTERY CRITICAL at session end: 2.817 V, capacity 0 %, `Critical`,
  Discharging** (BQ27541; it read 3.153 V / 71 % eleven minutes earlier).  The
  12:50 reset is a power brownout, not a software fault (trap 108).  **Charge
  with the proper charger before any further bench work**; every boot at this
  level can also spend a counter unit.
* **The TWL6030 charger was never instantiated** (found at session end): the
  board DTS had no `charger` child and `twl6030_charger.ko` was not in the
  rootfs, so nothing configured the BCI.  A fix is **prepared but not
  flashed**: image **`0x91AF34DF`**
  (`local/images/kernel-0x91AF34DF-charger.img`, sha256 `7d4b9187…`) + rootfs
  **`ba275807…`** (`local/images/rootfs-region1-7.3.3-charger.ext4`,
  17 modules).  Test it before any other bench work.
* **Next work (operator order, session 30 end): step 0 charger test, then
  step 4 (resume the WL1283 — the FREF reference-clock parent), then step 5**
  (WM8994 audio + Cypress CY8CTMA3 touch).  `hd3`, BB10 and the PlayBook OS
  image work remain parked by the operator.

## Previous state — 2026-11-06 (session 29)

**The WL1283's bus and board lines are measured (MMC5, WL_EN GPIO2_22, WL_IRQ
GPIO2_21) and `&mmc5` probes, but the card still does not answer CMD5.  The unit
was restored to PlayBook OS 2.1.0.1917 and `BOOTCTR_FCTRY = 0x32` verified.**

* **Report 176's "mmc1" was wrong.**  The TI WL1283 combo is on **OMAP MMC5
  (`0x480D5000`)**.  `pidin mem` on TabletOS shows io-pkt's `devnp-ti1283.so`
  holding `/dev/mem 0x480d5000` (8 KiB) and `pidin irq` shows it attaching QNX
  vector `0x5b` = **GIC SPI 59 = `mmc5`**; the `sdmmc5` pads (`0x148`–`0x152`)
  read `0x0118` on the reference while every `sdmmc1`/`sdmmc3` pad is `0x000f`
  (safe mode).  Trap 97, report 180 §1.
* **WL_EN = gpio 54 = GPIO2_22** (pad `gpmc_nwp` `0x07c`, active high, 120 ms
  before the chip is touched) and **WL_IRQ = gpio 53 = GPIO2_21** (pad
  `gpmc_ncs3` `0x07a`, input, falling, active low).  From `devnp-ti1283.so`
  **and** the live GPIO diff against report 118: the reference's
  `GPIO2_DATAOUT` is `0x31413200` vs `0x31013200` — bit 22 is the only output
  that changes when the WLAN driver runs.  The old DTS drove GPIO2_21 low as an
  output; that hog is gone and GPIO2_22 now has an output-high hog.
* **`&mmc5` works as a host**: 4-bit, `sdmmc5_pins` `0x0118`, a 1.8 V fixed
  `vmmc` window (for `ocr_avail`), `HCTL=0x00000b00` (identical to QNX),
  `CAPA=0x04e10080`, functional clock 48 MHz.  **The card never answers CMD5**
  (`attach_sdio no` → `mmc4: Failed to initialize a non-removable card`).
* **Eliminated, with evidence** (report 180 §4): the enable line (at boot and
  re-pulsed at runtime), the pads (full padconf diff), the IRQ pin, **every
  TWL6030 rail and `CLK32KG`** (register-for-register identical `CFG_STATE` to
  QNX), the OCR window (1.8 V and 3.3 V) and a full power cycle.  The chip is
  **alive**: TabletOS's own `wpa_supplicant_ti` logs `CTRL-EVENT-SCAN-RESULTS`
  after the restore.
* **Corrected the same session: `CON` is not a difference.**  QNX's
  `0x000E0080` goes to `MMCHS_SYSCTL` (`CLKD = 2`, `DTO = 14`), not to `CON`;
  `CON` is only `&= ~OD` in both, and our `HCTL` is byte-identical to QNX's.
  Also eliminated since: a 12 MHz init clock (new `omap_hsmmc.init_hz` knob),
  WL_EN driven by a regulator at MMC power-up, `ti,no-reset-on-init` on
  `gpio2_target`, and 5 s of EN settling.  **What is left** needs the WL1283's
  power-up requirements (datasheet/schematic) or a bus capture; a cheap next
  probe is UART2/HCI (the combo's BT core) to split "not powered" from "SDIO
  path broken".  The whole clock/power-manager space (`CM_PER`, `CM2`, `PRM`)
  has now been diffed too and matches in the *safe* direction — the reference
  idles in `HW_AUTO` while our boot holds more domains awake — so no clock is
  missing (trap 101 also closes the "read MMCHS5 on QNX" route: only io-pkt
  can).  **Two of this session's own probes were bugs and are now
  traps**: 99 (a GPIO walk one bank past GPIO6 reset-looped the unit) and 100
  (a register dump inside `omap_hsmmc_set_ios` faulted on a runtime rebind and
  killed the console) — both fixed and both verified.  Trap 98, report 180 §6.
* **Working image:** region-0 **`0xFC32F008`**
  (`local/images/kernel-0xFC32F008-cm3217.img`) with the region-1 root
  `local/images/rootfs-region1.ext4` (sha256 `0faeadff…`, 16 modules,
  including `cm3217.ko`).  The kernel patch was **re-exported** (5 703 lines /
  34 files).  **This image is on media** — region 0 holds it, not the 1917 IFS.
* **The CM3217 works (report 182) and is the step-2 close:** `cm3217.ko` binds
  `3-0010`, `in_illuminance_input` = **32–37 lux with the room lights off, 191
  with them on**.  No settle delay (poll for the first nonzero pair); the part
  must be left in its idle state (`0x60`→0x11, `0x27`→0x10) or the panel's own
  automatic brightness wedges (trap 104).
* **Method worth reusing (DECISIONS §30):** a peripheral's bus is identified from
  the reference OS's own `pidin mem`/`pidin irq`, read-only, before any DT work.
* **The CM3217 WORKS (report 182) — ROADMAP item 9 step 2 is complete.**
  `cm3217.ko` probes `3-0010` (`capella,cm3217`) and
  `in_illuminance_input` reads **32-37 lux with the room lights off and 191 with
  them on**, stable and repeatable.  Two facts were the whole problem:
  **there is no settle delay** (data is valid the instant `0x26` is written —
  every wait from 1 ms to 200 ms reads 0, so the driver polls for the first
  nonzero pair), and the part **must be returned to its idle state**
  (`0x60`->0x11, `0x27`->0x10) or the panel's own automatic brightness wedges.
  Earlier QNX-side readings were artefacts of racing RIM's driver on the same
  bus (trap 103).

## Previous state — 2026-11-06 (session 28)

**ROADMAP item 5 is CLOSED: the region-1 root is a real BusyBox userland and the
unit boots to a shell on it, with modules loadable.  The unit was restored to
PlayBook OS 2.1.0.1917 and `BOOTCTR_FCTRY = 0x32` verified.**

* **The real userland is on media.**  `local/images/rootfs-region1.ext4` (64 MiB
  ext4, no journal, sha256 `e1ec2bf3…`) carries BusyBox 1.36.1 (static, 401
  applet links), `/sbin/init` (BusyBox init), `/etc/inittab`, `rcS`/`profile`,
  the bring-up console as `/bin/pbconsole`, `pbrpmb`, and a test module under
  `/lib/modules/6.15.11`.  It was written at raw `0x29000000` (loader block
  `0x2915`) through the pre-OS `0xF7` loop.
* **The initramfs hands PID 1 to it.**  `rootfs_boot()` mounts region 1
  **read-write** (our own ext4, `DECISIONS.md` §18), bind-mounts /dev, /proc and
  /sys into it, chroots, and execs `/sbin/init`.  BusyBox init starts `sh -l` on
  `/dev/ttyGS0`; `PS1='pb> '` keeps `tools/pbcon.py` working.  If there is no
  `/sbin/init` the old console comes up, so a bad root never costs the debug
  channel.
* **Measured:** `/dev/loop0 on / type ext4 (rw)`; `uname -a` = `6.15.11`;
  `insmod`/`modprobe pbhello` load and `dmesg` logs `pbhello: loaded`; the
  boot-time top-up saw `0x30 >= threshold 0x1e, no write`.
* **Recovery verified:** the sealed 1917 IFS was flashed from the Linux image
  (489 frames), TabletOS booted, `/base` mounted, `BOOTCTR_FCTRY` reads `0x32`.
* **The working images:** `local/images/kernel-0xAEDF68D0-rootuserland.img`
  (region 0, CRC `0xAEDF68D0`) and the region-1 image above.  Region 0 on media
  is now the restored 1917 IFS; re-flash the kernel image to return to Linux.
* **New focus (operator): the device's own peripherals.**  Report 176 is the
  inventory.  The sensor cluster is on **OMAP I²C4 `0x48350000`**, which the
  board DTS currently **disables** (it enables `&i2c3` = `0x48060000`, which QNX
  never instantiates): BMA150 accel `0x38`, HMC5883L mag `0x1E`, MPU3050 gyro
  `0x68`, CM3217 light `0x10`, FAN5702 LED `0x36`.  The BQ27541 gauge (`0x55`),
  TMP103 (`0x77`) and LM3532 (`0x38`) are on I²C1; the WM8994 codec is on I²C4
  `0x1a`; the TI WL1283 combo (WiFi/BT/GPS+AGPS) is SDIO/UART on `mmc1`.  Most
  mainline drivers exist but are disabled; FAN5702, NCP5623, the Cypress
  CY8CTMA3 touch and the OMAP4+WM8994 machine glue are real work.  The `hd3`
  repurpose is **parked**.
* **The sensor bus and the three motion sensors are WORKING (report 177).**
  `&i2c4` (`0x48350000`, Linux `i2c-3`) is enabled and carries `3-0038`
  (BMA150), `3-001e` (HMC5883L) and `3-0068` (MPU3050).  BMA150 is `input0`
  with live `EV_ABS` on `/dev/input/event0`; HMC5883L and MPU3050 are IIO
  devices with live raw values (MPU die ID `0E9E`).  The **working image** is
  region-0 `0x3E479F6F` + region-1 `local/images/rootfs-region1.ext4` sha256
  `6aa322bd…`; region 0 on media is now the restored 1917 IFS, so re-flash both
  with `tools/bbusb.py` to return to Linux.  The `=m` IIO module closure (`industrialio`,
  `industrialio-triggered-buffer`, `kfifo_buf`, `i2c-mux`, `evdev`) and the
  BMA150 `of_match` table are part of it (traps 91/94).  `tools/bbusb.py` is
  now the flasher and both regions flash in one bootrom session
  (`DECISIONS.md` §29).
* **Step 2 is part-done (report 178).**  The **BQ27541 fuel gauge** works
  (`/sys/class/power_supply/bq27541-0`: capacity 100 %, 4.093 V, 28.8 °C,
  5.307 Ah full / 5.400 Ah design, cycle count 5, Li-ion, TI), and the
  **TMP103** works (`hwmon0` name `tmp103`, `temp1_input=37000`); the TMP103
  needs the `hwmon` module (`CONFIG_HWMON=m`).  The **CM3217 light sensor is
  open**: 0x10/0x11 on I²C4 ACK but read all-zero, and the mainline `cm32181`
  accepts only IDs 0x18/0x81/0x82 — it needs the QNX `ls-cm3217` RE (trap 95).
  Working image: region-0 `0x1D9159E0` + rootfs `c689ca9e…` (14 modules).
* **Step 3 (report 179): the FAN5702 status LED works.**  `drivers/leds/
  leds-fan5702.c` (compatible `fairchild,fan5702`) probes `3-0036`; the three
  channels appear as `playbook:…:{red,green,blue}:status` and the `brightness`
  writes turn them on/off.  **EN = GPIO1_13 (pad `0x1b0`)**, which boots in
  safe mode; the driver's pinctrl + `enable-gpios` pulse is what makes the chip
  answer (raw I2C was `ENXIO` before).  The protocol is in the external
  `LED-RE.md` and `led-fan5702.so`: `GENERAL 0x10` bits 1/2/3 = R/G/B,
  `0x20 = 0`, `0x30/0x40/0x50 = 0x36`.  Working image: region-0
  `0xB759C3A2` + rootfs `0a55a170…` (15 modules).

## Previous state — 2026-11-06 (session 27)

**The RPMB block transform is reproduced and the factory counter was topped up
FROM LINUX (`0x2E -> 0x32`, verified).  The unit is back on PlayBook OS
2.1.0.1917 with the counter at `0x32`.**

* **The monitor transform is solved** (report 175).  `pbmon` runs the
  bootloader's two-stage sequence — `0x33` → `0x2F` (**cipher**, 224 in/out,
  `dir 1` seals) → `0x33` → `0x2E` (**digest**, 224 → 32, tag gate) — with:
  * digest key = `seed[0:16]` (`page+0x440`, proven offline and on the device);
  * cipher key = the `0x33` preload `page+0x20` (`86 78 7c 2a…`), **16 bytes**;
  * **no `0x31(id 1)`** — the bootloader's own `0x31` call changes the plaintext
    and breaks it; `id 0` or no call both work (trap 89).
  Seal and unseal both reproduce the device's bytes exactly (`ct=OK tag=OK` on
  the captured block-0x3B pair; `seal(P) == block` on live 0x43).
* **The store reads from Linux**: 512/512 frames authenticate, **107 blocks
  carry a valid tag and all 107 unseal**, 405 are unused.
  **`BOOTCTR_FCTRY` is at block `0x3A`, name offset 196, len 4**; the value read
  `0x2B` after the session's seven factory boots.
* **The raw RPMB relay is the wire view** — reverse the 256-byte data field
  before the monitor (trap 86).
* **Item 4 (brightness) is CLOSED**: `write /sys/class/backlight/backlight/
  brightness 64` sets LM3532 register `0x74` to `0x40` (read back with `bl rd
  0x74`; `0xFF` at 255), and the panel visibly follows.  The LED classdev's
  `EBUSY` is `led-backlight` owning it, which is correct.
* **The CRNG now has a seeding path**: the SoC RNG is firewalled, so the CRNG
  never initialises by itself (`entropy_avail` 18, no `crng init done`).  The
  console's new **`entropy <hex>`** command credits host bytes
  (`RNDADDENTROPY`, root) — measured `credited 256 bits` → `crng init done` —
  and `pbrpmb` now takes its nonce from `/dev/random` and warns loudly if it
  cannot.  **Do this before any RPMB write** (trap 87).
* **The top-up was performed from Linux (item 6 CLOSED).**  The operator ran
  the prepared `pbrpmb write 0x3a yes`: unseal, patch 4 bytes to `0x32`,
  re-seal, verify the seal round-trip, one type-3 frame, result read, and a
  read-back that unsealed the block and printed `32 00 00 00`.  A read-only
  scan afterwards still showed 512/512 frames authenticating and 107/107
  blocks unsealing.  An agent still never writes RPMB (`SAFETY.md` §6).
* **The top-up is now automatic at boot**: `/init` runs `pbrpmb topup 0x1E`
  after the console is up, and writes `0x32` only when the value is inside
  `1..0x32` and below the threshold (one sealed frame, one read-back, never a
  loop; the outcome is one `pbrpmb: topup:` line in `kmsg`).  Measured at
  `0x30`: `>= threshold 0x1e, no write`.  The record is looked for at block
  `0x3A` then scanned `0..0x7F`, so a re-tiled store does not disable it.
* **The region-0 IFS is COMPRESSED and boots.**  `local/work/ifs1917-compressed.ifs`
  (3 466 232 B, sha256 `f14db207…`, `os_type` 6, CRC verified) is the shipping
  1917 IFS + our `rfs_validator` shim, spliced into the stock NRV2B stream by
  `tools/bb10_ifs.py splice` and resealed.  It booted on the factory path
  (217 frames) with `/base` mounted.  The uncompressed
  `local/work/ifs-shim-sealed.ifs` (`b52195bb…`, 7 900 548 B) is the
  independently booting alternative; both are in the tree.
* **Bench (end of session):** TabletOS 2.1.0.1917 up from the compressed IFS
  (`/dev/hd1t179 on /base`), door up with **root ssh** (`tools/pbssh.sh`, no
  `PB_USER`), **`BOOTCTR_FCTRY = 0x32`** — the bootloader read and decremented
  the very record that was written from Linux, and `startup.sh` topped it back.
  The restore-boot is the IFS verification (the read-back hash rule is
  dropped).
* **Next / the one open work item:** `ROADMAP.md` item 5 — a real userland in
  the region-1 root, then the `hd3` cut-over.  **There is no current blocker**:
  items 4 and 6 are closed, the counter repairs itself at boot, and the region-0
  slot now has a working compression path if the Linux image needs the room.
  Item 7 (packaging) is untouched.

## Previous state — 2026-11-04 (session 26)

**The unit is back on PlayBook OS 2.1.0.1917 and stable.**  Session 26 restored
the working set at the start (the full 1917 way back, 67,498 frames), ran the
chainloader probe series for `ROADMAP.md` item 6, hit a mount-probe reset loop,
and restored the unit again.  Final state:

* **OS:** QNX 6.6, 2.1.0.1917; `/dev/hd1t179 on /base type qnx6`; door up
  (devuser).
* **region-0 IFS:** the **1917 IFS** (`local/work/ifs-shim-sealed.ifs`, sha256
  `b52195bbf85b7618f9860ad73aebbae65932443b63335f57053020402b771861`), block
  `0x825`.  (`local/images/ifs0.sealed.bin` is the 2.0.0.4869 IFS and is the
  wrong asset for this RCFS — trap 84.)
* **`BOOTCTR_FCTRY = 0x32`.**  The mode-3 mount probe reset-looped and the
  wrong-IFS restore added to it; the counter reached `0x28`, then the TabletOS
  boot's `startup.sh` wrote `0x32` back (`bootctr: wrote 0x32, readback ok
  (was '(')`).
* **region-1 Linux root** is unchanged (raw `0x29000000`); no writes this
  session.

**Item-6 findings (report 173) — the important part.**

* **The address base was wrong.**  The bootloader's arm9000 section is loaded at
  `0x80000000` from media file `0x9000`, so every *code* address in reports
  150/151 is `0x9000` too high (data literals are correct).  The "wipe-table
  anomaly" was this error; the table is valid.  Trap 82 / correction 3.18.
* **The bootloader wipes its RPMB state before the OS jump.**  The tail wipe
  covers `[0x80025E78, 0x800517C4)`, including the store state and catalogue.
  A chainloader selector call therefore returns `0` ("absent").
* **The tail disables the CM2 L4SEC crypto clocks** (`0x8000F184`); the
  chainloader re-enabled them (`0x8000F108` returned 1).
* **Calling the bootloader's selector works; calling its mount hangs.**  The
  mount (`0x800082DC`) reset-looped the unit.  The "reuse the bootloader's
  writer from the chainloader" route is not viable as delivered.

**The single current blocker:** the two-stage `0x2E`+`0x2F` transform is not
implemented in `pbmon`.  The frame/block keys are fixed (`probe` prints
`KEY PINNED`, `blockscan` 512/512 MAC-ok) but the unseal still has 0 hits:
`pbmon` does only `0x2F`, preloaded with the wrong key (`page+0x20` instead of
`page+0x60`), and the 224-byte interface lacks the 32-byte tag (report 174).

**Next:** implement the two-stage `pbmon` transform (report 174 §5).  The
frame key and block key were corrected to the reference's (report 174): `probe`
prints `KEY PINNED` and `blockscan` reads/MAC-verifies **512/512** blocks (was
0/512), but the unseal still gets 0 hits because `pbmon` calls only `0x2F`.
The bootloader's transform is `0x2E` (key `page+0x20`, IV `frame+0x10`) → tag
check → `0x2F` (key `page+0x60`); the unseal needs the 32-byte tag in its
interface.  Both domains (5 and 4) give 0 hits, so the domain is not the
variable.  The unit is restored and the counter is `0x32`.

**Bench:** unit on 2.1.0.1917 (`0fca:8012`), door `169.254.0.1`
(`tools/door-hold.sh`; start it with `setsid`, and do not use `pgrep -af
door-hold` — it matches itself, trap 32), `PB_USER=devuser`.  The 1917 OS has
no root SSH; `pkexec chmod 666 /dev/ttyACM0` is the documented console step.
The USB/flash tools still run as the user, never `pkexec`.

## Previous state — 2026-11-03/04 (session 25)

**The unit is held in the bootrom/ramloader with the first BB10 test set on
media.**  Session 25 built and flashed a BB10 10.3.03.2049 set for the PlayBook
(`BB10-PORT.md` §4b/§4c, **`BB10-IMAGE-GUIDE.md`**):

* **IFS** — our rebuilt, compressed 10.3 IFS with the `*Winchester*` branch, the
  winchester driver set, the SGX540 stack at `proc/boot/gfx4430`, and the
  `rfs_rcfs_shim`; 8,555,080 B, `VERIFIED (CRC path)`, sha256 `50e1c04a…`;
* **`/base`** — the 10.3 RCFS with the deckard blocklist writer neutered, the
  seed `autorun::0`, and the startup hook; sha256 `88711f9e…`;
* **UFS** — the 10.3 `.0.4.ufs` with `autorun::0`, sha256 `8bc7c13f…`.

**The image verified on the loader, but BB10 did not run.**  `bbusb.py info`
reports `boot_mode = factory` and `factory_count = 45` (it was 50): the CRC path
accepted the image and the loader jumped five times, so the
container/IFS/RCFS chain is sound and the stall is inside BB10 (kernel/board
support for the OMAP4430).  There was no splash and no USB; the unit re-entered
the bootrom on each reset.  **It is now held in the ramloader (`0fca:8001`)**,
where nothing drains — `bbusb.py info --keep-loader` was the last action.

**The way back is packed and verified, NOT flashed**:
`local/work/back-1917.signed`, 1,104,544,336 B — our 1917 IFS + the edited 1917
RCFS + the vendor `.5.ufs` with `autorun::0`.  Flashing it from the ramloader
(no `--from-bootrom`) restores TabletOS, and its boot tops `BOOTCTR_FCTRY` back
to `0x32`.

**The factory budget is 45 (`0x2D`).**  Only a TabletOS boot refills it; every
successful boot spends one, and a reset-looping image can spend several per
minute.  The operator's `/accounts/devuser/startup.sh` is not on the BB10 UFS,
so nothing on this set tops it up.

**Next:** flash the 1917 way back to stabilise the unit and refill the counter,
then **resume the Linux work** (`HANDOFF.md` is the Linux kickoff).  **The BB10
port and the PlayBook OS image (IFS/RCFS) work are parked by the operator**
(session 25 end): their documents stay (`BB10-PORT.md`,
`BB10-IMAGE-GUIDE.md`, `IFS-REBUILD.md`, `IMAGE-REBUILD-GUIDE.md`) but are not
the current work.  The ordered Linux work is `ROADMAP.md`: item 6 (the factory
top-up) is the blocker, then item 4 (brightness), item 5 (real userland and the
hd3 cut-over) and item 7 (packaging).

**Bench:** unit in the ramloader, `lsusb` = `0fca:8001`; the device password is
`0000` (sent once, never retried).  The door/`devuser` path is unavailable
until a TabletOS set is restored.  The region-1 Linux root is still in place
(it starts at raw `0x29000000`, above the BB10 UFS writes).

**Persistent blockers:** the **Linux `BOOTCTR_FCTRY` top-up** (the monitor's
`0x2F` transform is not reproduced, reports 148/156, so only QNX's `/dev/rpmb`
writes it — an agent never does), and the **BB10 OS compatibility** shown by
this trial.

## Per-subsystem state

| subsystem | state | detail / evidence |
|---|---|---|
| Chainloader image (CRC path) | **working** | 828 B loader at image `+0x800` = `0x80100000`; copies zImage/DTB high, checks the DTB magic `0xEDFE0DD0`, jumps (`r0=0`, `r1=~0`, `r2=DTB`). Report 109 §8.1, 103 §5.2 |
| DRAM / memory map | **working** | 1 GB at `0x80000000`; kernel runs from `0x84000000`-loaded zImage, decompressed to `0x80008000`; DTS comment |
| Kernel self-capture | **working** | DRAM ring at PA `0x88000000` (32 KB) + breadcrumb/event log; `tools/pb_ring.py`, `tools/pb_events.py`. Bootstrap §3 |
| WDT2 | **working — 58.6 s window, 10 s pet (session 31)** | The loader arms `WLDR = 0xFFFC1800` (7.8125 s); the kernel reprograms it to the reference's `0xFFE2B400` (58.6 s) at the late initcall via `WSPR` (`0xAAAA`→`0x5555` stop, write `WLDR`, `0xBBBB`→`0x4444` start), **polling `WWPS`** after each write, and pets every 10 s (`TGR` read→invert→write); fallback 2 s if the readback fails.  Verified on `0xE36C679A` by `WCRR` reload timing (~10 s) and uptime >275 s.  The expected dmesg line is missing on 7.3.3 (printk anomaly).  Report 109 §8.1 + correction; trap 115; decision §38 |
| eMMC (MMC2) | **reads proven; writes healthy — but writers must use large blocks** | `mmcblk1` = MCGAFA 59.4 GiB, `boot0`/`boot1`/`rpmb`; needs the `vemmc` fixed supply for `ocr_avail` (report 109 §9).  Session 16 read a freshly flashed 11 152 064-byte image back **byte-exactly** and matched a pre-flash TabletOS capture (report 134); the MMC2 functional clock is 96 MHz.  Session 17 measured the write path (report 136): the driver has no request-size limit — an O_DIRECT write gives **256 KiB `CMD25`s, 5.5–7.0 MB/s** (4 KiB chunks: 0.93 MB/s), one `CMD13` per request (normal `mmc_blk_card_busy`).  The old 17–32 KB/s was `recv`'s tiny buffered writes plus 4–7 unthrottled `printk`s per request to **fbcon**.  Use ≥64 KiB (ideally 256 KiB–1 MiB) writes with the MMC trace off.  **Session 18: region 1 now holds a bootable Linux root** (32 MiB ext4 at raw `0x29000000`), written through the pre-OS `0xF7` loop (2055 frames) and read back byte-exactly (QNX `dd` and Linux `blk sumb`); it mounts read-only via a loop and its `/init` runs (report 140). |
| USB device + gadget | **working, but not a bulk transport** | enumerates `0fca:8010`, CDC-ACM, product string "PlayBook (Linux bring-up)"; `/dev/ttyACM0` ↔ `/dev/ttyGS0`; High Speed (480 Mbps), 512-byte bulk packets.  **Drains at only ~134 KB/s and drops bytes when the host blasts** (report 136 §3.3), so it is a control/debug channel: do not send multi-MB images through `recv`.  Report 114, 136 |
| **WL1283 (MMC5)** | **software surface exhausted (session 37: the 1917-truth minimal host also fails CMD5; the chip is not up — WL_IRQ low)** | **Session 37:** the WL1283 code is archived (`kernel/archive/wl1283/`), the DTS is re-derived from 2.1.0.1917, and the minimal 1917-truth `&mmc5` (image `0x0DC36A11`) probes but returns `attach_sdio no` (report 196); pads `0x0118`, WL_EN `0x0003`, WL_IRQ `0x410B` match QNX and WL_EN is high, but `GPIO2_DATAIN` bit 21 = 0.  Bus, pads, WL_EN/IRQ, PMIC rails, MMC5 CON/HCTL/SYSCTL and the clock manager all match the reference.  QNX enumerates at 24 MHz with CMD5 arg `0x00FFFF00`; Linux's default 400 kHz/CMD52-first also times out.  **Session 35:** `startup-playbook` (never run by our chainloader) is the source of the QNX-only GPIOs — Winchester table at image `0x80122750`; GPIO1 0/4/15/18/28/31 and GPIO2 5/7 are output-low, gpio54 (WL_EN) output-low, gpio53 input/falling (report 192 §4, trap 135).  All eight released to input on the live 1917 are **negative** (trap 136).  `CLK32KG` is base `0x8C` and APP-ON in both (CFG_TRANS identical; CFG_GRP and the CFG_STATE CON/MOD bits differ; trap 135).  **H1** (CMD0 → CMD5 only, no CMD52/CMD8, 24 MHz, arg `0x00FFFF00`, image `0x29784F48`) still times out (`err=-110`, no R4; `HCTL=0x00000b00`, `CAPA=0x04e10080`; trap 137).  The remaining difference is the chip's non-exposed VBAT/TCXO/reset pins or OMAP HSMMC sampling — needs a schematic/board view or logic analyser |
| Userspace (initramfs) | **working, minimal** | `/init` = PID 1, mounts, creates the configfs ACM gadget, forks `/console` in the fallback path.  **No auto-reboot since session 15** — PID 1 reboots only on request (the timer spent a `BOOTCTR_FCTRY` unit per iteration).  **Session 28:** after the gadget and the boot-time top-up, `/init` mounts the region-1 ext4 root **read-write**, bind-mounts /dev//proc//sys, chroots and execs `/sbin/init`; with no `/sbin/init` it falls back to the console.  Report 176, session-28 note |
| Userspace (region-1 root) | **working — a real BusyBox userland** | 64 MiB ext4 at raw `0x29000000`; BusyBox 1.36.1 + init + 401 applet links + `pbconsole`/`pbrpmb` + `/lib/modules/6.15.11`; boots to `sh -l` on `/dev/ttyGS0`; `insmod`/`modprobe` proven.  `local/images/rootfs-region1.ext4` sha256 `e1ec2bf3…`, kernel image `0xAEDF68D0` |
| Console | **working** | `help ls cat peek poke kick disp bl i2c i2c16 spi fb kmsg grep blk recv sink blkwr qnx6 rootfs write reboot`; `spi tcon` (low-first) and `spi tconbe` (high-first); `blk info/sum/sumb/rd` is read-only FNV-1a64 over `/dev/mmcblk1` (**`blk info` prints size 0 B — `fstat` bug, see KNOWN-ISSUES**), `grep <substr> <path>` scans any-size text (for `clk_summary`); `sink <len>` measures the link, `blkwr <off> <len> [chunk]` is a timed O_DIRECT write with a deterministic pattern (report 136); `qnx6 [off-hex]` mounts hd3 read-only (report 138); `rootfs [off-hex]` mounts the region-1 ext4 root read-only on `/rootfs` (report 140); argument limit 24.  `pb_reg` op set is now `w a:v` / `r a` / `t a` (16 reads, re-arm) / `u a` (16 reads, no re-arm) / `d from:to` (range dump) / `v` (video liveness).  Every hardware access forks with a bounded wait |
| QNX6 read access (hd3) | **working, read-only** | `CONFIG_QNX6FS_FS=y` + console `qnx6 [off-hex]`: a read-only loop over `/dev/mmcblk1` at raw **`0x49000000`** (superblock `0x49002000`) mounted qnx6 on `/mnt`.  Verified on `0xFC77CD47`; the old `0x48F00000` offset gives `wrong signature (magic)` from the driver.  Report 138 |
| DSS / DISPC via omapdrm | **working — and the core clock is now right** | `/dev/dri`, `/dev/fb0`, `card0-LVDS-1` connected+enabled, `LINE_STATUS` advancing (37 lines/ms), GFX overlay scanning the painted buffer.  The DSS functional clock, which is the DISPC's **core** clock, was left at 48 MHz by the boot chain while the pixel clock is 50.4 MHz — that was the last display defect.  `dss_probe()` now asks the PER PLL for the reference's rate and gets **96 MHz**.  **Session 18:** the boot-time `pb` bridge-read probes are behind `pb_probe` (default **off**), so a normal boot has **no DSI CIO errors**; the DSS rate-mismatch WARN is gone and the real rate is recorded (report 142).  Report 133 §2/§3 |
| DSI1 link | **byte-identical to the reference, PLL included** | `DSI_CTRL = 0x00EAEF9D`, `CLK_CTRL = 0xA0346005`, `TIMING1 = 0x79190999`, `TIMING2 = 0x7FFF7FFF`, `VM_TIMING1/2/3`, `CLK_TIMING = 0x00000E0B`, `VM_TIMING7/8`, PHY, PLL `CFG1 = 0x14A5E8BE` / `CFG2 = 0x00056000`.  All re-read live in session 15. Reports 127 §4, 132 §6 |
| TC358764 bridge | **programmed; configuration now identical to a working system** | The whole 353-register map was read from **TabletOS with its display working** (`tools/pbdsi`) and diffed against ours.  After fixing `VP_HTIM1/2` (§ below) only four registers differ, three of them live status/counters.  `IDREG = 0x00006500`, `LV_CFG = 1`, `LV_PHY0 = 0x0004C02D`, `VP_HTIM1 = 0x00780001`, `VP_HTIM2 = 0x00760400`, `VP_VTIM1/2`, `VFUEN` self-clears. Report 132 §2/§5/§6 |
| Bridge **reads** | **reliable — with `PPI_TX_RX_TA = 0x00040006`** | The value is latched by `PPI_STARTPPI (0x0104)`, so it must be right **in `pb_bridge_seq`**.  Boot with `4` → 4/8 at boot, 8/16 sampled; boot with `0x00040006` → 8/8 and 16/16.  Live writes after boot change nothing.  The VC re-arm does not affect the rate. Report 132 §3 |
| Backlight (LM3532 @ `0x38`) | **working** | binds, `bl_power = 0`, brightness 255; gate is `GPIO2_16`. Report 120 |
| Panel TCON (MCSPI4) | **working — sent automatically at bring-up** | The 9-word Hitachi table is sent by `/init` right after the display comes up (high byte first, 450 kHz, 100 ms after words 0/2/last), matching the reference's own order (`winch_lcdctl 0x193c4`).  Without it the panel stays black even with the bridge programmed. Report 133 §3/§7 |
| Panel identity | **Hitachi (definitive)** | on TabletOS: `GPIO6_DATAIN = 0x00010800` (bit 16 = 1), slog `LCD is Hitachi`.  Report 127 §1 |
| **Image on the panel** | **WORKING** | With `0x8C146805` (and the same source as `0x068C867F`) the panel shows a clean, stable image: solid white, solid red, and the kernel console as legible text.  The three necessary fixes were the bridge delivery (VC0/HS null packet), the LVDS porches + TCON table at bring-up, and the DISPC core clock. Report 133 |
| QNX oracle tool | **`tools/qnx-dsiread.c` → `tools/pbdsi`** | Reads any TC358764 register over DSI from a TabletOS user process (generic read request `0x24` on hw VC1, BTA, RX FIFO pop; response is a generic long read, DT `0x1A`).  Read-only.  This is how the reference values in this document were obtained. Report 132 §2 |
| Kernel patch + config | **7.3.3 (session 32 export); 6.15.11 kept as reference** | `kernel/patches/0001-playbook-winchester-7.3.3.patch` is **7 257 lines / 47 files** (38 modified + 9 new), sha256 `ad55511d…`, generated from the tree against pristine 7.3.3, applied with 0 rejects and **47/47 files byte-identical** (session 32 adds `drivers/mmc/core/sdio.c` and `include/linux/mmc/host.h`; extends `omap_hsmmc.c`, `omap4-common.c` and the board DTS).  Config: `kernel/winchester-7.3.3.config` (sha256 `9274c809…`, includes `CONFIG_CHARGER_ISL9519=y`).  The 6.15.11 patch (`5 703 lines / 34 files`) stays as the bisect reference; note it is **incomplete** — it omits `cm3217.c`, `leds-fan5702.c`, `pbmon-pair.h`, `pbmon-page.h` and their Kconfig/Makefile lines (trap 106) |
| **ISL9519 charger** | **WORKING under Linux (session 31)** | I²C1 `0x48070000`, slave `0x09`; VendorID `0x0049`, DeviceID `0x0003`.  `isl9519.c` (built-in, DTS `charger@9`) sets the four registers in RIM's order with the masked fields and refreshes every 5 s.  Measured **+358 → +397 mA**, 3.775 → 3.794 V, capacity 8 → 9 %, `Charging`; still on at 197 s (watchdog).  Registers read back `0x14=0x0280`, `0x15=0x1070`, `0x3E=0x0D00`, `0x3F=0x0180`.  Report 183 |
| TWL6030 BCI | **probes after the session-31 fix; NOT the board's charger** | `twl6030_charger` probes with the `simple-battery` profile and sets `CONTROLLER_CTRL1=0x10`, `VOREG=0x21`, `VICHRG=0x08`, `CINLIMIT=0x09`, but the stock OS leaves the whole block at bootloader reset values — it charges through the ISL9519.  The BCI node/modules are inert; `twl6030_usb` reports `ONLINE=1`, VBUS 4.81–4.91 V |

## Last known-good image

**The session-32 test image is `0xAF4FC103`** (0x91A23C bytes; the build that
produced `boot/chainloader/kernel.img`) — the `0xE36C679A` base plus the
`AUXCLK1` FREF2 enable **inside the MMC5 probe**, the WL_EN 0/500 ms/1 reset
pulse, the (default-off) MMC5 CMD5 quirk, the 3.3 V dummy OCR window, the
`PBCMD` command trace and the computed CLKD/actual print.  It boots and
charges; it is **not** a known-good baseline (reports 185/186).  The previous
test images were `0x9471F6C9` (audit), `0xF38CF87E` (FREF before probe) and
`0xE6F77A9B`.

**The media currently holds the 1917 IFS** (`local/work/ifs-shim-sealed.ifs`,
sha256 `b52195bb…`), flashed region 0 only for the session-37 calibration check;
TabletOS 2.1.0.1917 is running.  The Linux Task-3 image `0x0DC36A11` and the
known-good region-0 image below return to Linux (block `0x825`).

**The known-good image is `0xE36C679A` on the 7.3.3 base**
(`local/images/kernel-0xE36C679A-wdt58s.img`, sha256 `bdbaf5fc…`, 0x91A748
bytes) — the
`0x0F6AC47F` feature set plus the battery-profile DTS node, the ISL9519
charger driver/config, and the WDT2 58.6 s/10 s pet policy.  It boots to the
region-1 userland, the BCI and ISL9519 probe, **the pack charges at ~0.4 A**,
and WDT2 reloads every ~10 s in the 58.6 s window.  The region-1 root for it
is the charger rootfs `local/images/rootfs-region1-7.3.3-charger.ext4`
(`ba275807…`, 17 modules).

**The previous session-31 image is `0xA0B75C43`**
(`local/images/kernel-0xA0B75C43-isl9519.img`, sha256 `a608a579…`, 0x91A1F0
bytes) — the charger driver without the WDT2 change.

**The earlier intermediate is `0x306E3DA5`**
(`local/images/kernel-0x306E3DA5-battery-profile.img`, sha256 `b42f9272…`,
0x91AC14 bytes) — the battery-profile node only; the BCI probes, charging was
still unresolved.

**The last full-regression image is `0x0F6AC47F` on the 7.3.3 base**
(`local/images/kernel-0x0F6AC47F-7.3.3.img`, sha256 `fcddda8d…`, 0x91AB20
bytes) with the region-1 root
`local/images/rootfs-region1-7.3.3.ext4` (sha256 `0b3b83e8…`, 15 modules,
`/lib/modules/7.3.3`).  It boots to the BusyBox userland, panel clean, eMMC and
the ext4 root working, and the CM3217 auto-probing.  It was built from
`local/kernel-tree` (7.3.3) + `kernel/winchester-7.3.3.config`; the patch is
`kernel/patches/0001-playbook-winchester-7.3.3.patch`.

**The previous known-good image is `0xFC32F008` on 6.15.11**
(`local/images/kernel-0xFC32F008-cm3217.img`, session 29: the session-28 feature
set + `&mmc5` for the WL1283 + the WLAN enable hog + the FREF/TWL WKUP pinctrl
+ the `PBD:`/`PBGPIO:` dumps + the CM3217) with the region-1 root
`local/images/rootfs-region1.ext4` (sha256 `0faeadff…`, 16 modules).
Session 29's intermediate images were `0x74FA74A3` (mmc5 only), `0xA7D3BC13`
(+enable/IRQ wiring), `0x0005424F` (+dump), `0x4949969A` (3.3 V, reverted) and
`0x224A189E` (FREF/TWL pinctrl); all superseded.
Session 30's intermediate 7.3.3 images were `0x83E1A570` (first 7.3.3 boot,
DTS without the CM3217 node) and one rootfs-only flash; both superseded.
**`0x0D1DA0D3` remains the clean pre-userland fallback.**  Session 19's images
(`0x87FD6EED`, `0x9CE309CF`, `0xFCCE507A`, `0x65599D90`, `0xC462AE22`,
`0x771355C0`, `0x94640F82`, `0x853C5323`, `0x96FC5CA9`) are RPMB/monitor
diagnostics; the last, `0x96FC5CA9`, is saved as
`local/images/kernel-0x96FC5CA9-pbmon12.img`, and the earlier ones were
overwritten in `boot/chainloader/kernel.img` (rebuild from the source + patch).
Do not boot `0xFCCE507A` (auto selftest).  None of the session-19 images is a
"last known good" for normal use.

**`0x0D1DA0D3`** — `local/images/kernel-0x0D1DA0D3-noirqerr.img` (session 18).
The `0xFB3A9F5D` userspace (region-1 root mount, `pbrpmb`) with the boot-log
fixes: `pb_probe` gates the DSI/bridge read probes (default off, so **0 DSI CIO
errors**), `pb_reg` is a driver-group attribute (no duplicate/WARN/taint), and
the DSS clock mismatch WARN is fixed with the real 96 MHz rate recorded.
Verified: `boot-20261030-084331.log` has 0 CIO errors, 0 duplicate, 0
Tainted/WARNING, `fck 96000000`, fbcon up, region-1 root mounted (report 142).
**This is the image to build from.**

**`0xFB3A9F5D`** — `local/images/kernel-0xFB3A9F5D-rootfs.img` (session 18).
The `0xFC77CD47` kernel/DTB plus an initramfs whose `/init` loop-mounts the
region-1 ext4 root read-only at raw `0x29000000`, runs `/rootfs/init`, and a
console `rootfs` command; 11 166 136 bytes.  Verified on the bench: ext4 mounted
read-only without a journal, `PB-ROOTFS: /init is running from the region-1 ext4
image (Linux 6.15.11)`, `blk sumb 0x29000000 33554432` = `c2e78e1f09b50208`
(host FNV), boot log `local/logs/boot-20261030-063824.log`.  **This is the image
to build from for the region-1 root.**  Its root is
`local/images/rootfs-region1.ext4` (32 MiB, sha256 `0b87c29f…`, no journal);
rebuild it with `tools/build_rootfs.py` and write it with
`tools/pre_os_ifs_flash.py --payload … --dest-raw 0x29000000` (report 140).

**`0xFC77CD47`** — `local/images/kernel-0xFC77CD47-qnx6read.img` (session 17,
end).  The `0xEB617C60` kernel plus `CONFIG_QNX6FS_FS=y` and the console's
`qnx6` command; 11 164 248 bytes.  Verified on the bench: mounted hd3 read-only
at the corrected offset and exposed the reseed (report 138).  Note the image's
`qnx6` default is still the old `0x48F00000`, so mount with `qnx6 49000000`; the
source default is fixed for the next build.

**`0xEB617C60`** — `local/images/kernel-0xEB617C60-mmcwrite.img` (session 17).
The `0xC07AB9FA` kernel/DTB plus the console's `sink` and `blkwr` commands and
the MMC trace behind `/sys/module/omap_hsmmc/parameters/trace` (default off);
11 159 480 bytes.  Verified on the bench: byte-exact O_DIRECT write read-back in
region 1, and the request shape / throughput in report 136.

**`0xC07AB9FA`** — `local/images/kernel-0xC07AB9FA-emmcproof.img` (session 16).
The `0x8C146805` kernel and DTB plus the console's read-only `blk`/`grep`
commands and the corrected banner text; 11 152 064 bytes.  Verified on the bench:
byte-exact eMMC read-back and a green panel.

**`0x8C146805`** — `local/images/kernel-0x8C146805-portable.img`.  **The first
image with a correct display, built from the portable (moved) workspace.**  It
carries everything: the bridge delivery fix (VC0/HS null packet), the DSI block
matched to the reference, `PPI_TX_RX_TA = 0x00040006` (reliable bridge reads),
the corrected LVDS porches (`VP_HTIM1 = 0x00780001`, `VP_HTIM2 = 0x00760400`),
the **Hitachi TCON table sent by `/init` at bring-up**, the **DSS
functional/core clock raised from 48 to 96 MHz**, the reference's
`VDAC`/`VANA` rails, pads `0x078`/`0x07A`, the `pb_reg` hook with `t`/`u`/`d`/`v`,
PID 1 without the 180 s auto-reboot, and `CONFIG_OMAP2_DSS_DEBUGFS`. <!-- audit-ok: image description, historical -->
Verified on the bench: solid clean magenta, and `pb dss: … -> 96000000`.

`0x068C867F` is the *same source* built before `CONFIG_INITRAMFS_SOURCE` became
repo-relative; the only difference is the config string embedded by
`CONFIG_IKCONFIG`, which changes the CRC (and the image size by 8 bytes).  It is
the image that first showed a correct display.

Image CRC convention: `zlib.crc32(image[0:-4])`, stored in the container's last
word by `tools/build_kernel_image.py`.  A rebuild that prints the same CRC as
the previous one has almost certainly **failed to link** — check the build
output (trap 39).

**Do not reuse:** `0x5E40C5C1` (two lanes + sync-pulse video mode: no display at
all), `0xB9A1BC4F` (two lanes), `0x3B04298E` (LM3532 without VAUX3 — it wedges
the I²C bus and resets the unit).

## The unit

| item | value |
|---|---|
| working units | **one: the reference (`0x50175127`)**, armed AND sealed.  The spare (`0x50176EC1`, BSN `1063043093`) is **down** with the 1-2-2 blink = error `0x5B` (report 153) and presents no USB.  Two 32 GB units are bricked and parked |
| region-0 IFS on media | **the 1917 IFS** (`local/work/ifs-shim-sealed.ifs`, 7,900,548 B, sha256 `b52195bb…`), flashed region-0 only for the session-37 calibration check; TabletOS is running.  Linux test images (region 0 only, block `0x825`): the Task-3 `0x0DC36A11` (`0x91A514`; mmc5 enabled, CMD5 negative), `0x05819994` (mmc5 disabled), the known-good `0xE36C679A` (`local/images/kernel-0xE36C679A-wdt58s.img`, 0x91A748, sha256 `bdbaf5fc…`), and the touch `0x04AF1433`.  The compressed `local/work/ifs1917-compressed.ifs` (sha256 `f14db207…`) is the alternative |
| region-0 RCFS (`/base`) | **still the 1917 RCFS at region-0 offset 0xC00000** — outside the 7.3.3 test image (which ends at 0x91A8C4) and not touched this session; not readable as `/base` without the 1917 IFS |
| UFS (`/`) | **the 1917 vendor `.5.ufs` with `autorun::0`** (the session-27 set); the BB10 `.0.4.ufs` is not on media.  (Not re-hashed this session) |
| `/accounts/devuser/startup.sh` | the 1917 UFS carries the operator's script; it ran during the session-35 S3/S2 TabletOS bench and topped `BOOTCTR_FCTRY` back to `0x32` |
| `BOOTCTR_FCTRY` | **`0x32`** on the session-37 TabletOS boot (the TabletOS top-up ran; `cat /dev/rpmb/BOOTCTR_FCTRY` → the byte `0x32`) |
| region-1 Linux root | **present** at raw `0x29000000` (region-1 +16 MiB, block `0x2915`): 64 MiB ext4, no journal, sha256 `ba275807…`, **17 modules under `/lib/modules/7.3.3`** (incl. `twl6030_charger.ko`, `twl6030-gpadc.ko`); the initramfs mounts it **read-write** and chroots into it |
| TabletOS / OS | **running 2.1.0.1917** (`0fca:8012`) — region 0 = the 1917 IFS; `ti0` UP with 40+ scans, MAC `14:74:11:8f:9c:c3`; door available (`169.254.0.1`).  The session-37 Linux Task-3 image `0x0DC36A11` is the way back |
| device-side files | none needed on Linux; QNX `/tmp` tools must be re-pushed after a TabletOS restore |

The factory boot budget is real and one-way: every successful factory/CRC boot
spends one unit, and only a **TabletOS** boot puts one back.  `0` and `> 0x32` do
not fall back to the product path — they hard-lock the unit (`SAFETY.md` §2).
**Since session 15 our images no longer reboot themselves**, so a boot now costs
exactly one unit and stays up until asked.

## The single current blocker

**The single current blocker: the WL1283 does not enumerate under Linux, and
the software surface is exhausted.**  The 1917-truth minimal `&mmc5` host
(report 195) boots and probes but **CMD5 fails** (report 196): the pads, WL_EN
level and IRQ direction are bit-for-bit identical to QNX (`0x148/14C/150 =
0x0118`, `0x07C = 0x0003`, `0x07A = 0x410B`, `GPIO2_DATAOUT = 0x31413200`), yet
`GPIO2_DATAIN` bit 21 (WL_IRQ) is 0 — **the chip is not up under Linux**, while
QNX brings it up (`ti0` UP/RUNNING, 40+ scans).  The DT model is therefore not
the missing piece; the remaining evidence is chip-side (VBAT / 26 MHz TCXO /
SLOW_CLK) or OMAP HSMMC sampling, and needs a **board view or logic analyser**.
Do not repeat traps 116–143 as-is; an overlapping test must state what differs.

**Found this session: the per-unit WLAN calibration is in the QNX `hd5`
partition** (report 197) — a TI WL128x radio-parameters INI + a binary blob, and
the assigned MAC is in NVRAM `deviceinfo` (`14:74:11:8f:9c:c3`).  It is the
source for `wl128x-nvs.bin` **after** enumeration; it cannot fix CMD5.

**The touch works** (I²C2 `0x48072000` = `i2c-1` at `0x65`, `playbook-munin.c`,
`input0`/`event0`; reports 193/194, traps 138–141).  Open touch details: axis
calibration/orientation (1280×768 → 1024×600) and the IRQ pad.  **The WM8994
audio has not started.**

**Media state: region 0 = the 1917 IFS** (TabletOS 2.1.0.1917 is running,
`ti0` UP, `BOOTCTR_FCTRY = 0x32`) — the default restored state; region 1 = the
7.3.3 charger rootfs (`ba275807…`).  The Linux Task-3 image `0x0DC36A11` is the
way back; the known-good `0xE36C679A` and touch `0x04AF1433` remain.  NVRAM and
the MCT remain unwritable, and an agent never writes RPMB (`SAFETY.md` §6).

## What is next

1. **WL1283 chip-side measurement** — the 1917-truth DT redo is done and
   negative (report 196); VBAT / 26 MHz TCXO / SLOW_CLK and MMC5
   CLK/CMD/DAT0 need a board view or logic analyser.
2. **The WM8994 audio half of item 9 step 5** — the read-only codec-ID check at
   I²C4 `0x1a` (`i2ctransfer -y 3 w2@0x1a 0x00 0x00 r2` should return
   `0x8994`), then `CONFIG_MFD_WM8994` / `CONFIG_SND_SOC_WM8994`, the codec
   node, and the McBSP/`l4_abe` machine link (report 176 §3, HANDOFF §0c).
   **Also the one cheap chip-side check:** the board's FEM is a TriQuint
   TQP6M9002 (`tiwlan_winchester.ini FemVendorAndOptions = 1`); probe the FEM
   VCC (and WL1283 VBAT) on live QNX-with-WiFi vs Linux — a rail off under
   Linux would be a shared board rail the TWL comparison missed (report 195
   §3.7).  The FEM alone cannot gate SDIO.
3. **Touch polish:** the axis calibration/orientation for the 1024×600 panel
   and the IRQ pad (GPIO2_3); the bus, protocol and driver are proven.
4. **Build `wl128x-nvs.bin` from the hd5 calibration** (report 197), and add the
   `wlcore@2` child the mainline binding requires (`ti,wl1283`, `reg = <2>`,
   `interrupts` for WL_IRQ, `ref-clock-frequency`/`tcxo-clock-frequency` 26 MHz),
   for the post-enumeration `wl12xx` path — report 195 §3.6, HANDOFF §0b.
5. **The unit is on TabletOS** (1917 IFS in region 0, `BOOTCTR_FCTRY = 0x32`).
   Re-flash `0x0DC36A11` (or `0xE36C679A`) at block `0x825` to return to Linux.

## Workspace location

The workspace moved at the end of session 15 from the old checkout (the root
filesystem) to this repository.  Start sessions in the repository root.
Absolute paths were retargeted in `tools/` (`pbssh.sh`, `door-hold.sh`,
`pb_ring.py`, `pb_events.py`), the shipped kernel configs
(`CONFIG_INITRAMFS_SOURCE`) and the canonical docs; historical reports keep their
original wording.

A **fallback copy of the old tree is deliberately kept at `$HOME/playbook-linux`**
until the operator is happy with the move.  It is stale from the moment of the
move — do not work in it, and do not let anything (`CONFIG_INITRAMFS_SOURCE`,
the door scripts) point at it.

**Public snapshot:** `$HOME/SSDdrive/playbook-linux-public` (remote
`git@github.com:Psyden57/playbook-linux.git`).  The private tree is the source
of truth; publish with `tools/publish_public.sh` (see `REPO-MANAGEMENT.md`).

**Both cross-toolchains are inside the workspace** in the gitignored payload:
`local/toolchains/armv7-eabihf` (Buildroot gcc 14.3.0) and
`local/toolchains/qnx660-master` (QNX SDP 6.6).  `. tools/env.sh` exports
`PATH`/`CROSS_COMPILE`/`QNX660`/`QNX_HOST`/`QNX_TARGET`; `tools/env.sh --check`
validates them and prints the versions.

---

# History

Everything below this divider records what was true earlier.  It is kept for the
evidence and the trail, **not** as current state.

## Session recaps (20–23)

**Session 23 (documentation and publication; no bench).**  The docs audit in
report 167 was executed and the canonical set corrected; the public GitHub
snapshot was built and pushed (`REPO-MANAGEMENT.md`); the earlier-phase document
tree and `SECURITY.md` were deleted; the per-doc session stamps were dropped.
The IFS writer (`tools/ifs_build.py`) was not started.  No USB/device access;
the unit state above is unchanged from the last measurement.

**Session 20 snapshot and the incident (2026-10-31).**  Session 20 performed the
first Linux RPMB write (a captured genuine `BOOTCTR_FCTRY = 0x32` block, with a
verified frame MAC; the device accepted it), and the spare stopped booting and
no longer enumerated on either Linux or Windows.  Report **149**.  The red
**1-2-2** blink is error code **`0x5B`** ("NV-record / RPMB subsystem unusable"),
emitted bit-serially to a FAN5702 LED driver; the SoC does **not** reset and the
loader never brings up a usable USB device (report 153).  The chain is written
down stage by stage in `docs/project/BOOT-CHAIN.md`.

**Session 21 (read-only, report 155).**  The surviving reference (`0x50175127`)
was re-read per `BOOT-CHAIN.md` §5 and every value matched: `BOOTCTR_FCTRY`
**absent**; IFS sha256 **`75be24e8…`** over exactly `0x2BFE10` bytes; `os_type`
**4** at media `0x100100`; CRC **`0x8D46FB4A`**; `end-8` **`0xD7C82D1F`**;
NVRAM `0x2040` **absent** (the loader substitutes `2`), `0x2801` **A=0/B=10**.
The vendor `PB-4869-root.0.3.ifs` and `.0.0.mbr` in
`$BB10MT/` were **byte-identical** to the live IFS/MBR.  **No
writes were performed.**

**Session 21 recapture (report 156).**  A read-only `cat
/dev/rpmb/bgroupcrt_dat` produced a **same-boot pair** for block `0x3B`: P at
page `0xbf0e7000+0x700`, C at frame `0xbfb8e000+0xE4`.  **Both MACs verify** with
the reference's own CID seed and frame key — the block HMAC over the logical
view, the frame MAC over the wire view with a big-endian tail — so the **`0x2E`
oracle is answered**.  The `0x2F` unseal still does **not** match standard
AES/DES with the visible keys (36 162 AES trials).  Files in
`docs/reports/data/session21-capture/`.

**Session 21 trustzone attempt (report 157).**  The proposed
`trustzone-omap4` raw SMC pass-through (devctl **`0xC0280501`**) was tested.  The
resmgr starts and standard devctls work, but the `0x5001` pass-through
**segfaults in libc** (`libc+0x91f8`) on a clean boot; the stock binary, a
bypass patch and a `msg_max_size` patch all fail.  The boot-time `random`
driver's devctl crashes it too.  **The trustzone shortcut is unavailable; the
arm gate remains.**  Tool kept: `tools/qnx-tzsmc.c`.

**Session 21 armed-state measurement (report 160) — superseded for region 0.**
Session 21 found the reference **record-armed but carrying the stock `os_type` 4
image**; on 2026-11-01
the operator **flashed the sealed `os_type` 6 IFS**, so the unit is now **armed
AND sealed**.  The pre-flash facts, for the record: `BOOTCTR_FCTRY` present,
size 4, **`32 00 00 00`** — observed on 2026-11-01 as `50 → 49 → 47 → 49 → 50`.
The falls came from boots into the IFS (operator: reached the splash screen,
then powered off) — not from loader/`info` sessions, which do not consume it.
The rises are the OS top-up.  The **pre-flash** region-0 IFS was sha256
**`75be24e8…`** over exactly `0x2BFE10`, `os_type` **4**, CRC **`0x8D46FB4A`**,
magic `0xC3D0E193`, model `0x06001A06`, `end-8` `0xD7C82D1F` — i.e. **not
CRC-sealed**, which is why it could not boot on the factory path.

**CFP corroboration (report 161, raw dumps in `docs/reports/data/cfp-2026-11-01/`).**
RIM's own tool, run by the operator from Windows, prints the same state from the
loader side: `Boot Mode: Factory (Remaining boot count: 50)`, `Boot OS Count
383,0,0 (Region0,Region1,Max)` / `3,0,0 (Version,Requested Region,Actual Region)`,
`Security: Enabled`, `External MCT/HWV: Enabled`, and **`OS Version: 2.0.0.4869
DEV` in region 0**.  CFP also shows sectors 5-260 of the user area are
**permanently write protected** (MCT/MFG/bootrom), and `ENH_SIZE_MULT = 8`
against `MAX_ENH_SIZE_MULT = 0x72E` — the `hd3` headroom.  CFP is the only
correct `info` for this unit; `bb10mt info` is mostly wrong (operator).

**Static RE of the tooling (reports 162-164, three subagent tracks).**  CFP's
eight unidentified `info` reads are all resolved and confirmed against LDR_01's
dispatcher (`0x8011737C`): `0xBD/0xBE` HW override, `0xBF/0xD9` DRAM, `0xE1/0xC8`
installer metrics, `0xB2/0xC8` DSP metrics (**a stub — always zeros**),
`0xD8/0xC8` OS metrics, `0xE9/0xFB` NVRAM `0x2074`, `0xDB/0xCB` vendor ID,
`0xB5/0xD3` CSD/ExtCSD.  **`RESETQNXOSREGION` is loader op `0xE8 {u32 4}` =
delete NVRAM record `0x2074`** — the same 24-byte record flash `COMPLETE` writes
and `0xE9` reads, so the Windows-only recovery now has an in-tree equivalent
(`tools/bbusb.py reset-osregion`, dry run by default; verify by read-back
because the device returns `0x2E` on both paths).  `0x39` is **not** an MCT
partition type (it is the CREAD opcode), which closes the bb10mt path-2
question.  **One tool now**: `tools/bbusb.py` with subcommands `info`, `flash`,
`reset-osregion`, `boot-mode`, `reset-loader` and `pack`/`unpack`/`split`/`parse`;
offline tests in `tools/tests/test_offline.py` (87 checks, stub device, no USB).

**Session 20's reports** are **147** (the monitor's address view is **identity**
and its reachable DRAM excludes `0xBF040000`: the `0xFF05` refusal was a garbage
command page, not a caller rejection) and **148** (the monitor now serves the
whole seal ABI — all four services return 0 — but the `0x2F` transform does not
reproduce the device's block-0x43 pair with any key/framing/ABI variant tried,
including 512 blocks × 6 framings × 5 keys and 10 560 offline AES combinations).

**Session 19's reports** (the key/seal baseline) are **143** (the RPMB frame key
is **pinned** — read from the live `rpmb-omap4` page with metadata + a physical
read after a `/proc/pid/as` read reset the unit; the block key is `seed[0:16]`)
and **144** (**the 224-byte RPMB record block is encrypted on media by the secure
monitor** — the unseal is `smc` service `0x2F`, and a plaintext block patch is
invalid, which blocks the Linux top-up as written in report 141).

**Session 18's reports** are **139** (a region-1 write was misrouted into region
0 because the loader uses the region-relative V1 `qcfp` record; the sealed IFS
and the RCFS head were restored and verified), **140** (**a Linux root is in
media region 1**: 32 MiB ext4 at raw `0x29000000`, written through the pre-OS
`0xF7` loop, read back byte-exactly, mounted read-only from `/init`, and its
`/init` ran), **141** (**the Linux RPMB key material is normal-world and the read
path works** — its "top-up is feasible" verdict is now qualified by 144), and
**142** (**the boot log is clean**: the DSI CIO errors and the DSS clock warning
are fixed).

**Session 17's reports** are **136** — the Linux eMMC **write path is healthy**
(256 KiB `CMD25`s at 5.5–7.0 MB/s with tracing off); the old 17–32 KB/s was the
writer's tiny buffered writes plus per-request `printk` to the **fbcon** console,
and the ACM console link itself is only ~134 KB/s and drops bytes when blasted
(trap 49 replaced) — **137** (superseded on its causal claim) — and **138**: **a
smaller hd3 filesystem is reformatted and reseeded at boot**; the FS-level shrink
is dead and the partition must change (MCT).

**Session 16's reports** remain **134** (eMMC reads byte-exact under the DSS
clock regime; the MCT/loader block numbers are not raw offsets) and **135** (the
hd3 shrink was attempted, made the unit boot as new, and was reverted; the UFS
was restored with `bb10mt`).

## The display milestone is met

**The panel works.**  On `0x8C146805` it shows solid white, solid red and the
kernel console as legible text, stable, with no bands or noise (operator,
report 133 §4).

Three defects had to be fixed, each necessary, each found by comparing against
the reference rather than by reasoning about it:

1. **Bridge delivery** (session 14): the DSI HS clock was never started.  Fixed
   with the reference's VC0/HS null packet (report 131).
2. **Bridge video configuration + panel TCON** (session 15): our `VP_HTIM1/2`
   had the LVDS porches in the reserved top byte (HBP = HFP = 0), and the
   Hitachi TCON table was only ever sent by hand.  Fixed porches
   (`0x00780001`/`0x00760400`) plus the table sent at bring-up (reports 132,
   133).
3. **The DISPC core clock** (session 15): the DSS functional clock was left at
   48 MHz by the boot chain while the pixel clock is 50.4 MHz.  Raised to 96 MHz
   in `dss_probe()`, matching the reference's 100.8 MHz regime (report 133).

Two things that misled earlier sessions should not be reused: `LANESTATUS0` and
`SYSSTAT` read 0 on a **working** display (report 132 §4), and the session-13
"partial live image" was never ours (report 129 §5).

## Session-18 blocker text (kept for the trail)

**The region-1 root path works and the boot log is clean; the remaining blockers
are the Linux RPMB top-up and a real userland.  `hd3` can then be repurposed
without an MCT edit.**

1. **Solved: bulk data and a bootable root in region 1.**  A 32 MiB ext4 root
   reached region 1 through the pre-OS `0xF7` loop (2055 frames), read back
   byte-exactly on the media and from Linux, mounted **read-only** through a
   loop, and its `/init` ran (report 140).  The eMMC write path itself was
   already known healthy (256 KiB `CMD25`s at 5.5–7.0 MB/s, report 136); the
   missing pieces were a transport and a root, and both now exist.  The loader
   container detail that bit us — the region-relative **V1 `qcfp`** record — is
   trap 55 / report 139.  From Linux the eMMC is accessed read-only by
   `blk`/`rootfs` today; **writable eMMC for our own data is now allowed**
   (`DECISIONS.md` §18).
2. **The ACM console is not a bulk transport.**  It drains at ~134 KB/s and
   **drops bytes** when the host blasts (16 MiB lost ~768 KiB; a 1 MiB blast
   never completed), so `recv` cannot carry a rootfs image and its old transfer
   did not read back correctly.  Bulk images must go through the pre-OS `0xF7`
   loop or QNX/`bb10mt`; a faster userspace transport (RNDIS/ECM, USB mass
   storage) is the real fix for the nura direction.  (Report 136 §3.3/§3.4.)
3. **The hd3 shrink is not an FS-level operation** (report 138; supersedes the
   `pps`-race reading of report 137 and report 135 §2).  A 1 GiB QNX6 FS built
   from the **UFS factory tree** and written to hd3 booted to setup; the
   read-only inspection showed the boot had **reformatted hd3 to 58 GB and
   reseeded it from `/base`** (`num_blocks` 15,273,980, inodes 238,656,
   `/.rootfs.os.version` = 2.0.0.4869, `var/pps` without `system@`).  So
   `rootfs.sh`'s format+seed path ran: the smaller FS was not accepted as the
   seeded R/W root (QNX can mount a smaller FS — the rejection is in the
   boot/root-selection path, not `fs-qnx6`).  The base seed is unprovisioned,
   hence setup.  **A smaller hd3 root needs an MCT/partition change
   (operator-only); do not retry an FS-only shrink.**
