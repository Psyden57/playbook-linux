# Handoff — session 37 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 37 = Linux-era 31**.  See
`docs/sessions/NUMBERING.md`.

> **Session-36 result: item 9 step 5 has started, and the touch bus is solved
> (report 193, trap 138, DECISIONS §44).**  The operator formally stopped the
> WL1283 hunt; **do not restart traps 116–137**.  The Cypress touch is on
> **OMAP I²C2 `0x48072000` — Linux `i2c-1`, QNX `/dev/i2c1` — at slave
> `0x65`**: 1917 `graphics.conf` selects `driver = munin`
> (`i2c_lib=i2c-resmgr-client,tp_intr=1035,i2c_slave=0x65`), and
> `libmtouch-munin` opens `/dev/i2c1` (the Pegasus `/dev/i2c2` string belongs
> to the driver this board does not use).  **The Linux bus was dead because its
> pads were never muxed:** `0x4A100126`/`0x4A100128` read `0x011F` = safe mode,
> so every i2c-1 transaction timed out (`Connection timed out`, not a NAK).
> Writing both to `0x0110` (mux 0, input, pull-up) made `0x65` ACK at once and
> a register read return data.  The durable fix is an `i2c2_pins` group and
> `pinctrl-0 = <&i2c2_pins>` on `&i2c2`; **the test image `0x307615B3`
> (`0x91A300`, region 0 only) is on media and proves it on a plain boot**: the
> pads read `0x0110` from the DTS and `0x65` ACKs with no `devmem` help
> (reads `0x00`/`0x09`).  Munin wiring: power GPIO1_14, reset GPIO4_8 (both
> already output-high), IRQ `tp_intr=1035` = GPIO2_3 (inferred).
> The unit is on **`0x307615B3`** in region 0, region 1 the 7.3.3 charger
> rootfs (`ba275807…`), `BOOTCTR_FCTRY = 0x2e`, pack Full 4.155 V.  **The
> protocol is decoded (report 194, DECISIONS §45):** the chip boots into its
> Munin bootloader, so the driver runs `reg0=0x81` → read 21 bytes at `reg1` →
> `reg0=0x80` (start app) → `reg0=0x02` → `reg 0x1D=1` → `reg 0x1F=1`, then
> reads the 18-byte report at `reg1` (4 × 12-bit contacts).  No firmware upload
> is needed (the app/build match `Pegasus_50.hex`).  **The touch WORKS on the media image `0x04AF1433`:** the driver boots
> the controller and `input0` reports touches.  **Next (operator direction,
> session 36 end): archive the WL1283 code, re-derive the board DTS from
> 2.1.0.1917, and restart MMC5 from a minimal host; the WM8994 audio half
> follows.**  The known-good baseline remains `0xE36C679A`.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain — **the panel shows a clean, stable
image** — and a **64 MiB ext4 BusyBox root in region 1** at raw `0x29000000`;
the initramfs mounts it read-write, chroots and execs `/sbin/init`.
Peripherals: I²C4 motion sensors (BMA150/HMC5883L/MPU3050), TMP103, the
BQ27541 gauge, the FAN5702 status LED, the CM3217 light sensor, and **the
ISL9519 charger (session 31)**.  WDT2 runs at the reference's 58.6 s window
with a 10 s pet.

**Last known-good image (7.3.3):** region-0 **`0xE36C679A`**
(`local/images/kernel-0xE36C679A-wdt58s.img`, 0x91A748 bytes) + region-1
`local/images/rootfs-region1-7.3.3-charger.ext4` (17 modules).  **The media now
holds the session-36 touch image `0x04AF1433` (`0x91A8C4`, region 0 only);
re-flash the `0xE36C679A` region-0 image to return to the known-good
baseline.**  The session-33 final image `0x0353A241` and the
test images `0x0B38612D` (200 ms delay), `0xAC98BCAD` (CMD52 skip),
`0x5920CAAC` (session-32 code) and `0x90BDE221` (FREF off, pulse on) are
superseded.  **Build from `0xE36C679A`'s source tree plus the charger/WDT2
changes and (session 36) the `i2c2_pins` touch pinctrl.**

**The 6.15.11 backup/reference is intact:** `local/pristine/linux-6.15.11`,
`local/kernel-tree-6.15.11` (byte-exact copy of the old working tree) and
`kernel/patches/0001-playbook-winchester-6.15.11.patch`.  Do not delete any of
them.  The 6.15.11 patch is **incomplete** (trap 106); do not use it for a
clean rebuild.

**The 7.3.3 patch is `kernel/patches/0001-playbook-winchester-7.3.3.patch`** —
**7 620 lines / 48 files**, sha256 `28e48f56…`, applied to pristine 7.3.3 with
0 rejects and **48/48 files byte-identical**.  It is the session-32 tree with
the FREF2 enable and the WL_EN reset pulse removed (both negative for CMD5;
the pulse also caused the USB warm-reset regression), the session-33 MMC
experiments reverted, the session-36 `i2c2_pins` touch-bus pinctrl, and the
Munin touch driver.  Config: `kernel/winchester-7.3.3.config` (includes
`CONFIG_CHARGER_ISL9519=y`, `CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`;
`CONFIG_KERNEL_XZ=y` keeps the image under `0xB00000`).

**Parked by the operator:** the `hd3` cut-over, BB10, and the PlayBook OS image
work.  **The WL1283 (item 9 step 4) was formally stopped at the start of
session 36 and re-opened at its end** for a 1917-truth DTS audit and a
from-basics restart (§0c, DECISIONS §46).  The negative results of reports
180–192 stand; **do not repeat traps 116–137 as-is** — the redo is a new
derivation, and any overlapping test must state what is different.  The WM8994
audio half follows.  The USB warm-reset regression remains **fixed** (traps
130/141); the image enumerates by itself.

## 0a. The way back (the unit is on the touch image `0x04AF1433`; the 1917 IFS is not on media)

To return to PlayBook OS 2.1.0.1917:

```bash
nohup python3 tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs \
    --dest-block 0x825 --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
sleep 2; PBCON_TMO=10 ./tools/pbcon.py 'reboot -f'
until grep -qE 'COMPLETE|done|error' /tmp/f.log; do sleep 2; done; tail -5 /tmp/f.log
```

At session-36 handoff the media holds the **touch image `0x04AF1433`**
(region 0 only).  To return to the known-good baseline, flash
`local/images/kernel-0xE36C679A-wdt58s.img` at block `0x825`; to return to
TabletOS use the IFS flash above.  `BOOTCTR_FCTRY` reads **`0x2b`** (three
driver test boots; the boot-time top-up saw `>= 0x1e`, no write).
Region 0 is the **only** region that changed on this trip (the region-1 rootfs
is unchanged), so restoring **region 0 only**
is enough — the old "flash both regions" rule applies only when both were
modified (operator correction, session 31).  The RCFS at region-0 `0xC00000`
and the 1917 UFS are still on media and untouched.  After the boot:
`tools/pbssh.sh 'uname -a; mount | grep base'` and read
`/dev/rpmb/BOOTCTR_FCTRY` (expect `0x32` after the TabletOS top-up).  **Do not
restore `local/images/ifs0.sealed.bin`** — it is the 2.0.0.4869 IFS (trap 84).

## 0c. The next step — archive the WL1283 code, re-derive the DTS from 1917, then a minimal MMC5

**Operator direction (session 36 end).**  The WL1283 is the priority.  Before
another CMD5 experiment: (1) archive the accumulated WL1283 code, (2) re-derive
the board DTS node by node from **2.1.0.1917** rather than the 2.0.0.4869
header, and (3) start `&mmc5` again from a minimal 1917-truth host.  The old
negative results (reports 180–192, traps 116–137) stand; this is a new
derivation, not a re-run.  DECISIONS §46.

**Task 1 — archive the WL1283 code (mechanical, no boot).**
* Create `kernel/archive/wl1283/README.md` and
  `kernel/archive/wl1283/wl1283-instrumentation-7.3.3.patch`.
* Move out of the active tree/patch: the MMC instrumentation
  (`PBCMD`/`PBCLK`/`PBD`/`PBOCR`/`PBVOLT`), `pb_sdio_ocr_quirk`,
  `pb_enable_fref_clk2`, `pb_wl1283_reset_pulse`,
  `MMC_CAP2_SDIO_OCR_PROBE_FFFF00`, and the DTS `vwl1283`, `fref*`,
  `wl1283_ctrl_pins`, `wl1283-en-hog` and `&mmc5` nodes.
* Re-export the base patch (display, sensors, charger, touch) and verify it
  reproduces the tree byte-for-byte.

**Task 2 — the 1917-truth DTS audit (file-only).**
* For every node, cite the 1917 artifact: live `pidin mem`/`pidin irq`, the
  1917 `/base` (`hwio-config-winchester.so`, the enums), the 1917 IFS tree, the
  live registers.
* Re-derive `&mmc5` from scratch: pads `0x148`–`0x152`, `wl1283_ctrl_pins`
  (`0x07a`/`0x07c`), the EN/IRQ lines, `vmmc`/`vqmmc`, `bus-width`,
  `non-removable`, `max-frequency`, `mmc-pwrseq`, `ti,dual-volt`, `no-1-8-v`.
  Compare with the 1917 `hwio` pad/GPIO tables and the live QNX state.
* Correct the DTS header to name which nodes are 1917.

**Task 3 — the one experiment (first WL1283 boot).**  A minimal `&mmc5` host
from the 1917 evidence: pads, WL_EN, WL_IRQ; **no** OCR quirks, no FREF, no
reset pulse.  Boot and read the CMD trace.

* CMD5 answers → the redo found the missing piece; proceed to firmware/NVS.
* CMD5 still times out with the minimal host → the redo reproduced the old
  state; the next measurement is chip-side (VBAT/TCXO/MMC5_CLK/CMD/DAT0), as
  reports 180–192 concluded.

**Inputs:** the unit on `0x04AF1433`; the 1917 oracle (`tools/pbssh.sh`, the
door); `local/work/rcfs-1917-mnt`; `local/work/ifs1917-tree`;
`tools/qnx-hwi.c`, `tools/pbi2c`; `local/kernel-tree`; the patch.
**One variable per boot.**  Do not inject rails or pins.

## 0d. After that — the WM8994 codec ID (item 9 step 5)

**The touch is done and works** (image `0x04AF1433`: `input0`/`event0`, the
driver boots the Munin controller; traps 140/141).  The audio half starts with
a read-only codec check at **I²C4 `0x1a`** (Linux `i2c-3`), 16-bit registers:

```
i2ctransfer -y 3 w2@0x1a 0x00 0x00 r2
```

The WM8994 returns **`0x8994`** there (mainline `wm8994-core.c` switches on
`0x8994`; the regmap is `reg_bits = 16, val_bits = 16`).

**Decision table.**

* `89 94` → the codec is alive and powered.  Then the kernel-probe experiment:
  enable `CONFIG_MFD_WM8994` and `CONFIG_SND_SOC_WM8994`, add `wm8994@1a`
  under `&i2c4` with its required supplies (`DBVDD`, `DCVDD`, `AVDD1`,
  `AVDD2`, `CPVDD`, `SPKVDD1`, `SPKVDD2` — fixed always-on regulators or the
  TWL phandles), boot, and check the codec revision in dmesg.
* ACK but a different ID → wrong part/revision; record it before enabling
  anything.
* NAK/timeout → the codec is not powered or is held in reset; the I²C4 pads
  are already `0x0118` (the sensor bus works), so check the TWL V1V8/VCXIO and
  the codec reset/clock.  Do not enable the driver yet.
* `Remote I/O error` on i2c-3 → no device at `0x1a`; re-scan that one address,
  never the whole bus.

**Then the machine link** (a separate experiment): the 1917 stack is
`deva-ctrl-omap4abe-wm8994` (McBSP + ABE, `aif=0,secure_capture`).  Mainline
has no OMAP4+WM8994 machine driver; the likely path is `simple-audio-card` +
McBSP.  The blocker is `&l4_abe` (currently disabled; WD_TIMER3 hangs on
enable) — enable the segment with the non-audio targets disabled.  The McBSP
instance is probably **2**; confirm it.

**Inputs:** the unit on `0x04AF1433`, `tools/pbcon.py`, the 1917 `/base`
(`local/work/rcfs-1917-mnt`), report 176, `local/kernel-tree` and
`kernel/winchester-7.3.3.config`.

**One variable at a time.**  Do not inject rails or pins; do not re-add the
WL_EN pulse (trap 130).

## 1. The WL1283 (item 9 step 4) — the software road is exhausted

**Re-opened at session 36 end for a 1917-truth DTS audit and a from-basics
restart (§0c, DECISIONS §46).  The results below are the evidence and are not
invalidated; do not repeat traps 116–137 as-is.**

**Session-34 result (report 189).**  The QNX-side RE that session 33 left open
is complete:

* `hw_info_pps` publishes no WLAN node (runtime-probed inventory only); the
  `WLAN` node is in the **syspage**, and the live read is
  `WLAN_EN_GPIO=54,WLAN_INT_GPIO=53` with an empty `location` — no rail, clock
  or second enable.  `tools/qnx-hwi.c` is the read-only dumper (built
  `-static`, trap 131).
* `hwio-config-winchester.so` contains only the six known WLAN lines
  (sdmmc5 pads mux 0; WL_EN/WL_IRQ mux 3; FREF2_OUT mux 0) and WKUP `0x058`
  mux 3 both halves — no power/clock pad.
* `devb-mmcsd-winchester`'s powerman client registers only `-power on/off`;
  no voltage, clock rate or bus power.  Its base constants are MMC1/MMC2 only,
  and the WL1283 is on MMC5 (owned by `devnp-ti1283.so`).
* `devpm-omap4.so` registers only `iva_opp`; no MMCHS5/`CM_L4PER_HSMMC5` write
  and no WLAN rail; the `sdmmc5_*` names are mmap-only globals, never read.
* `tiwlan_winchester.ini` clock/power fields are all firmware-time (26 MHz
  TCXO, 38.4 MHz FREF wired-OR, 5 ms settling, 2.5 V internal TCXO LDO,
  4 mA SDIO drive).

**Session-33 result (reports 187/188).**  The QNX power manager is closed: the
`winchester` preset has **no WLAN rail or clock** (its WLAN authorities only
gate the standby/OSWR decision; `ldo_vaux1`/`ldo_vaux3` are the display/
backlight rails; `devpm-omap4.so` is the PRCM/voltage/DVFS plugin;
`winchester_phy_ctrl` is the USB PHY).  On the bare board the chip is **alive
under QNX** (`ti0` UP; `GPIO2_DATAIN` bit 21 = 1) and **not under Linux** with
WL_EN high (bit 21 = 0); the rightmost front pad is that IRQ (1.8 V ↔ 0 V),
WL_EN is not exposed, VIO is 1.8 V, and no 3.3 V chip rail is visible.  A
**200 ms MMC5 post-power-on settle** (image `0x0B38612D`) and a **CMD52 skip**
(image `0xAC98BCAD`, CMD0 → CMD8 → CMD5) are both negative.  Every accessible
pad matches QNX (`0x078 = 0x410B4103`, `0x07A = 0x0003410B`,
`0x07C = 0x00030003`, `0x148/14C/150 = 0x01180118`), and `GPIO2_DATAOUT` is
`0x31413200` (WL_EN high) on both.

**The remaining candidates are not bench-visible:** the chip's 26 MHz TCXO /
VBAT at its BGA pins (the accessible points are 1.8 V and 0 V; the ~4.7 V point
is a VBUS-side resistor), or OMAP HSMMC signal sampling.  A multimeter cannot
measure the TCXO or see CMD; **a schematic/board view or a logic analyser is
required.**  Do not inject rails or pins.

### 1.1 What is closed (do not re-run)

* The powerman/devpm lead (trap 126, report 189): no WLAN rail or clock; the
  QNX services are powerman (standby/OSWR), pmic (display rails) and phyctrl
  (USB).  `devb-mmcsd-winchester`'s own powerman client is only `-power
  on/off`, and `devpm-omap4.so` registers only `iva_opp`.
* The whole QNX-side hwinfo/pad/GPIO lead (report 189, trap 131): the PPS
  inventory has no WLAN node; the syspage `WLAN` node is only
  `WLAN_EN_GPIO=54,WLAN_INT_GPIO=53` with an empty `location`; the board
  `pad_config`/`gpio_config` has only the six known lines.  Do not re-open it.
* The FREF request input (trap 117), the FREF2 output as a CMD5 fix (trap
  118), the init clock/CMD5 argument (trap 119).
* The WL_EN level/hog, the 0/500 ms/1 reset pulse alone (trap 124), a 200 ms
  post-power-on settle (trap 128), and the CMD52 skip (trap 129).
* `CM_DIV_M5 = 0x329` (trap 116) and a runtime MMC5 `unbind`/`bind` (trap 120).
* The QNX/Linux pad, GPIO, PMIC-rail, OCR, power-cycle and BT-enable
  comparisons (reports 180/185/188).

### 1.2 The clock-source distinction (settled)

* **The vendor `.ini` says `TCXO_Clk = 1` = 26 MHz (the main clock) and
  `STRFRefClock = 2` = 38.4 MHz wired-OR CLK_REQ (the sync clock).**
* `wl128x_boot_clk()` (`drivers/net/wireless/ti/wl12xx/main.c`) selects the
  TCXO unless the hardware reports FREF or the TCXO is 16.368/32.736 MHz; the
  selection runs during firmware init, **after SDIO enumeration**.
* The QNX driver does not configure or gate any TCXO/FREF; the board must
  supply the 26 MHz.  Whether that oscillator runs under Linux is the open
  measurement, and it is not visible in any PMIC/SoC register already
  compared.

### 1.3 If the WL1283 is formally stopped

**The operator has kept it open (session 34).**  Report **190** is the
fresh-eyes dossier: every experiment, the QNX/board comparisons, the remaining
unknowns and ranked next tests (H1/H2 first).  If it is later stopped, go to
**item 9 step 5**: the WM8994 audio machine + the Cypress CY8CTMA3
touch (report 176 is the inventory; the plan is in §1b).  The USB warm-reset
re-enumeration regression was the session-32 WL_EN reset pulse; it is removed
and the final image enumerates by itself (trap 130).  A `soft_connect`
disconnect/connect in `/init` remains a proven fallback if it ever recurs.

### 1.4 QNX-side RE — DONE (report 189); it names no WLAN rail, clock or pad

Every target session 33 left open is now read, statically and (for the syspage
node) live:

* **`/pps/services/hw_info/inventory` has no WLAN node.**  `hw_info_pps`
  publishes only runtime-probed board/eMMC/CPU/audio attributes; it links no
  HWI library, and there is no static hwinfo database in the 1917 trees.  The
  `WLAN` node is in the **syspage**; the live dump is
  `WLAN_EN_GPIO=54,WLAN_INT_GPIO=53` with an empty `location` and nothing
  else.  `tools/qnx-hwi.c` is the read-only dumper (built `-static`, trap 131).
* **`hwio-config-winchester.so`** (`pad_config` 248 × 20 B, `gpio_config`
  72 × 32 B; resolver in `hwio-omap4.so`): only the six known WLAN lines —
  sdmmc5 pads mux 0 (signals `0x52`–`0x57`), WL_IRQ `0x07A` mux 3/signal
  `0x59`, WL_EN `0x07C` mux 3/signal `0x58`, 0x07E mux 3/signal `0x5D`,
  FREF2_OUT `0x19C` mux 0, and WKUP `0x4A31E058` **mux 3 on both halves**.
  GPIO 53 input/pull/falling, GPIO 54 output/no-pull; no other GPIO maps to a
  WLAN signal.
* **`devb-mmcsd-winchester`'s powerman client** registers only a binary
  `-power on/off` mode (cost 3); no voltage, clock rate or bus power.  Its init
  sets `SDBP = 1`/`SDVS18`, enables `CM2_L3INIT` MMC1/2 module clocks and
  clears `PRM_L3INIT` `LOSTCONTEXT`; no CM divider/DPLL and no regulator.  Its
  base constants are MMC1/MMC2 only; the WL1283 is on MMC5 (owned by
  `devnp-ti1283.so`).
* **`devpm-omap4.so`** registers exactly one powerman device, `iva_opp`; no
  `system/power/dev` literal, no MMCHS5/`CM_L4PER_HSMMC5` write and no WLAN
  rail.  The `sdmmc5_*`/`gpmc_*` names are mmap-only globals, never read; the
  OSWR wake table (Winchester2) uses the WL_IRQ pad only on OSWR entry/exit.
* **`tiwlan_winchester.ini`** clock/power fields are all firmware-time:
  `TCXO_Clk = 1` (26 MHz), `STRFRefClock = 2` (38.4 MHz wired-OR), 5 ms
  settling, `ClockValidOnWakeup = 0`, `TCXO_LDO_Voltage = 0` (2.5 V internal
  LDO), `Platform_configuration = 2` (4 mA SDIO drive, no level shifter/EEPROM).

**Live confirmation on the same 1917 boot:** `ti0` UP/RUNNING with 40+ scan
results (MAC `14:74:11:8f:9c:c3`), `sloginfo` shows `TIWLAN SDIO probe WAS
CALLED`, `SDIO clock ... 24Mhz`, `sdioAdapt_ConnectBus: Send commands sequence:
0, 5, 3, 7`.  So the chip works under QNX with exactly the software/board state
we have reproduced; the remaining difference is not in a QNX file.

**The QNX `startup-playbook`/BSP audit is DONE (session 35, report 192, trap
135).**  The `startup.*` entry (`0x44008` bytes at imagefs `0x908`, raw ARM at
`0x80100000`) is the **source of the QNX-only GPIO outputs**: its Winchester
board GPIO table (image `0x80122750`, 75 records) marks `GPIO1` bits
0/4/15/18/28/31 and `GPIO2` bits 5/7 as **outputs driven low**, and `gpio 54`
(WL_EN) output-low / `gpio 53` (WL_IRQ) input/falling.  That is the live QNX OE
subset exactly.  It also builds the syspage `WLAN` node and sets WDT2 to
`0xFFE2B400`.  It has **no `CLK32KG` write**.  Both-OS reads: `0x8C`–`0x8E` =
QNX `01/05/05`, Linux `07/05/55`; `CFG_TRANS` (`0x8D`) is identical and
`CFG_STATE` (`0x8E`) is APP-ON in both (CON/MOD differ), so the 32 kHz clock is
enabled in APP in both (report 180's labels corrected).  The S3 bench
(report 192 §8) then released all eight QNX-only GPIO outputs on the live 1917
and the chip stayed up (**all negative**, trap 136).  H1 (report 192 §9) then
built and booted the exact CMD0 → CMD5-only, 24 MHz, arg `0x00FFFF00` sequence
and the card still timed out (**negative**, trap 137).  **The software surface is
exhausted; do not re-open any of these leads.**

**Do not re-open this list.**  If none of it yields a WLAN rail/clock (it does
not), the remaining evidence is the chip's BGA pins or the MMC5 bus — a
schematic/board view or a logic analyser.  Stop the WL1283 there and go to
step 5, or obtain the hardware.

### 1.5 Stop condition

* The accessible electrical state is identical and the chip still does not
  answer; the remaining difference is at the chip's non-exposed pins or in
  host sampling.
* Do not make speculative clock changes that affect the DSS (trap 116).
* **Delivered in session 33:** the first direct QNX/Linux chip comparison —
  the chip is up under QNX and not under Linux, and the IRQ pad is the
  indicator.  The powerman lead is closed.
* **Delivered in session 34:** the QNX-side RE is complete (report 189): the
  live syspage `WLAN` node is only EN/INT, and no QNX software layer names a
  rail or clock.  **The software stop condition is met; what remains needs
  external evidence** (schematic/board view or logic analyser).
* **Delivered in session 35:** S1 (`startup-playbook` is the source of the
  QNX-only GPIOs), S2 (`CLK32KG` APP-ON in both; `0xB0` = REGEN2; `0xBC`
  unnamed), S3 (all eight GPIOs negative) and H1 (CMD0 → CMD5 only still
  times out) — report 192, traps 135–137.  **The software surface is exhausted;
  only the chip-side measurements remain.**

## 1a. Tools that make this cheap

* **`tools/pbcon.py`** — the USB console (the only way into our Linux image;
  there is **no door**, trap 88).  `PBCON_TMO=30 ./tools/pbcon.py 'cmd'`.
  Reboot with `tools/pbcon.py 'reboot -f'`.
* **`tools/pbssh.sh`** (the door; **root** ssh) and **`tools/door-hold.sh`** —
  only on TabletOS.  Start the door with `setsid`, never `pgrep -af door-hold`.
* **`tools/build_kernel_image.py`** — wraps zImage + DTB into the sealed IFS
  container; prints the CRC; the image must end below **`0xB00000`**.
* **`tools/bbusb.py`** — the flasher: `pack -l both.lst -v 1,2 -s -f` +
  `flash --signed --from-bootrom --password 0000 --apply`; one component with
  `--component FILE --dest-block N`.
* **`tools/build_rootfs.py`** — the region-1 ext4; `--krel 7.3.3 --modules DIR`.
* **`tools/pbi2c`** — the QNX-side I²C tool, now with `rdw`/`dumpw` for 16-bit
  SMBus words (the ISL9519/charger work).  Push it to `/tmp` on TabletOS.
* **`tools/qnx-hwi.c`** — the QNX syspage hwinfo dumper; build
  `ntoarmv7-gcc -std=gnu99 -O2 -static -o /tmp/qnx-hwi tools/qnx-hwi.c`,
  push it to `/tmp`, run `/tmp/qnx-hwi WLAN` (or `all`).  Read-only; the
  `-static` is required (trap 131).
* `tools/env.sh` — both cross-toolchains; `tools/env.sh --check`.
* **Static RE:** the REA MCP server exposes **Ghidra 12.1.4** (`open_binary`,
  `binary_overview`, `analyze_function`, `procedure_pseudo_code`,
  `search_strings`, `xrefs`, `read_bytes`).  Raw ARM import:
  `open_binary(path=…, format="raw-binary", architecture="arm",
  entry_point="0x80100000")`.  Reconcile addresses with
  `ntoarmv7-readelf`/`objdump` — Ghidra may add `+0x10000` on an ELF; the QNX
  toolchain is ground truth.  QNX device tools build `-static` (trap 131).
  See `DEVELOPMENT.md` §7a.

## 1b. The follow-on: item 9 step 5 (audio + touch)

Report **176** is the inventory and bus map; DECISIONS §28 is the order.
**One variable per boot.**

**WM8994 audio.**  MFD + codec on **I²C4 `0x48350000`** (Linux `i2c-3`),
7-bit **`0x1a`**; the codec node exists but `CONFIG_MFD_WM8994` /
`CONFIG_SND_SOC_WM8994` are **not enabled**.  The 1917 driver
(`deva-ctrl-omap4abe-wm8994.so`) is McBSP + **ABE** (`mcbsp=[0..4]`, protocol
default i2s, ABE firmware in `/base/lib/libabe.so`).  The likely Linux path is
to bypass the ABE with `simple-audio-card` + McBSP; the blocker is that McBSP
lives in the disabled `&l4_abe` segment (WD_TIMER3 hangs on enable), so enable
the segment with the non-audio targets disabled.  The McBSP instance is
probably **2** (the parser's default table) — confirm it.  First steps: enable
the two symbols, probe the codec ID, then the machine link.  Speakers, the
3.5 mm jack and the mics are all behind this codec.

**Cypress CY8CTMA3 touch (Gen3) — bus SOLVED (session 36, report 193).**
Mainline has Gen2 `cyttsp_core` and Gen5 `cyttsp5`, not this part.  RIM's stack:
`libmtouch-munin.so.1` (the board's `graphics.conf` selects `driver = munin`),
`muninfw`, and firmware
`/base/lib/firmware/munin/{Pegasus_50,Pegasus2_50,Pegasus_D4_A}.hex`.  (There
is no touch daemon at `/base/usr/bin/touch`; that file is the POSIX `touch`.)
1917 facts: slave **`0x65`**; the bus is **OMAP I²C2 `0x48072000` = Linux
`i2c-1`** (QNX `/dev/i2c1`), because Munin opens `/dev/i2c1`; the Pegasus
`/dev/i2c2` string belongs to the driver this board does not use.  **Linux
never muxed i2c2's pads** (`0x4A100126`/`0x4A100128` = `0x011F` safe mode);
the DTS now has an `i2c2_pins` group (`0x0110`).  Wiring: **GPIO4_8 = reset**
(active low), **GPIO1_14** = power (low 500 ms → high), IRQ `tp_intr=1035` =
GPIO2_3 (inferred).  Do **not** run a full-bus `i2cdetect` (trap 138); probe
`0x65` only.  **The Munin protocol is decoded (report 194, DECISIONS §45):**
the chip boots into its bootloader; the driver runs the `0x81 → 0x80 → 0x02 →
0x1D/0x1F` boot and reads the 18-byte report (4 × 12-bit contacts).  No
firmware upload is needed.  Write a small input driver, not `cyttsp_core`.

## 2. Build and flash (7.3.3)

```bash
REPO=$PWD
. tools/env.sh
K=local/kernel-tree
# 1. userspace images — rebuild EVERY one whose .c you touched:
arm-linux-gcc -static -O2 -Wall -o initramfs/root/console initramfs/console.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/init    initramfs/init.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/pbrpmb  initramfs/rpmb.c
# 2. kernel + DTB + modules (rm the cpio or the old initramfs is reused)
rm -f $K/usr/initramfs_data.cpio*
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 modules
# 3. wrap (prints the CRC; writes boot/chainloader/kernel.img)
ZIMAGE=$K/arch/arm/boot/zImage \
DTB=$K/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb \
python3 tools/build_kernel_image.py
```

**The printed CRC must differ from the previous build** (trap 39), and the
printed `image 0x…` must be **< 0xB00000**.  Check `CONFIG_KERNEL_XZ=y` if the
image grows past the slot.

**Rootfs and both-region flash:**

```bash
python3 tools/build_rootfs.py --size-mb 64 \
  --out local/images/rootfs-region1-7.3.3.ext4 \
  --busybox local/work/src/busybox-1.36.1/busybox \
  --busybox-links local/work/src/busybox-1.36.1/busybox.links \
  --modules local/work/modules-7.3.3 --krel 7.3.3
printf 'boot/chainloader/kernel.img=16,0\nlocal/images/rootfs-region1-7.3.3.ext4=8448,0\n' \
    > /tmp/both.lst
python3 tools/bbusb.py pack -l /tmp/both.lst -v 1,2 -s -f -c /tmp/both.signed
python3 tools/bbusb.py flash --signed /tmp/both.signed --from-bootrom \
        --password 0000 --apply
```

**From a running image**, start the flasher immediately before
`tools/pbcon.py 'reboot -f'`; from **power-off**, start the flasher first and
power on — the bootrom enumerates briefly as `0fca:0001`.  The window is 60 s
(DECISIONS §29).  Run the USB tools as the **user**, never `pkexec` (trap 73).
After every boot: `pkexec chmod 666 /dev/ttyACM0` (the operator must answer).

**Flashing only region 0** when only the kernel image changed (the usual
case now) is enough: `flash --component boot/chainloader/kernel.img
--dest-block 0x825`.  Flash both only when both regions were modified.

## 3. Traps

Full text: `KNOWN-ISSUES.md` §2.  The ones that end sessions:

9. A write to a DSS target in module standby never completes; let `omapdrm` own the DSS.
16. Blocking I²C is the same hazard — fork with a bounded wait.
17. A stale door holder looks like `Connection refused`; kill by explicit pid.
24. `/dev/ttyACM0` is recreated per enumeration (`pkexec chmod 666` after every boot).
26. `pmic-twl6030-ctl set_state vaux1 off` takes the unit down; read rails.
27. A flash cycle's precondition is **media running** (`0fca:8010`), not the ramloader.
28. Panel-chain writes outlive a warm reset (`SYS_RST <- 0x1f`); a cold battery pull clears them.
31. Non-burst DSI video mode is rejected at this pixel clock.
33. Our images no longer auto-reboot; a boot costs one counter unit.
39. A failed link leaves the previous image with the previous CRC.
43. `local/kernel-tree/.config` can point at a different initramfs tree.
45. The DISPC core clock is raised at probe time (96 MHz).
46. Reading DSS/DSI registers from userspace perturbs the picture.
47. MCT/loader block numbers are not raw offsets: `(block-0x15)*0x10000`.
49. eMMC writes are only as large as the writer makes them (≥64 KiB).
51. The ACM console is not a bulk transport.
54. A smaller hd3 filesystem is reformatted and reseeded at boot.
59. **Never `lseek()`+`read()` `/proc/<pid>/as`** — it reset the unit.
60–63. RPMB: the block is reversed; the 224-byte block is monitor-encrypted;
    the monitor reads identity addresses below `0xB0000000`.  **No agent writes RPMB.**
80. The loader's factory count is the first diagnostic after a flash.
82. The bootloader's code addresses in reports 150/151 are `0x9000` too high.
83. The bootloader wipes its own RPMB state before the OS jump.
84. `local/images/ifs0.sealed.bin` is the 2.0.0.4869 IFS — wrong for 1917.
85–90. RPMB keys/transform history (reports 143–175).
91–96. The sensor bus/I²C4 (trap 91), the loop-root `EACCES` (92), shutdown noise
    (93), the `=m` module closure (94), the CM3217 protocol history (95), the
    FAN5702 EN pad (96).
97–98. The WL1283 is on MMC5; the card never answers CMD5 (report 180).
99. A GPIO read one bank past GPIO6 aborts; with `panic=10` it reset-loops.
100. Never read another module's registers from `omap_hsmmc_set_ios()`.
101. A QNX `SIGBUS` on MMCHS5 is block-specific; only io-pkt can read the SDIO host.
102. Synchronise a probe with the operator action it observes.
103. Do not characterise a device on TabletOS if a QNX driver owns it.
104. The CM3217 needs no settle delay and must be left idle.
105. **Your own knowledge is out of date — look external facts up and cite them.**
106. **The old patch-export recipe re-emits only the files the previous patch
     listed** — every new file is silently dropped.  Build the list from the
     tree, apply the patch to pristine, and compare.
107. **`cm3217_remove()` needs `i2c_set_clientdata()`** or unbinding oopses.
108. **A nearly empty battery warm-resets on its own — check the gauge first.**
     The charger now works, so the pack can stay charged on USB.
109. **Subagents share this session's shell — keep them off the bench.**  Give
     them file-only tasks and never a `tools/` script.  The old log-audit
     requirement is dropped (the session-30 reset was low power).
110. **Do not reverse-engineer PlayBook hardware from BB10 images.**  OS 2.x only.
111. **The TWL6030 charger's `monitored-battery` must point at a
     `simple-battery` profile, not the fuel gauge** (`-ENODEV` otherwise).
112. **`twl6030_usb = Charging` does not prove the pack is charging — read the
     gauge.**  (And the TWL BCI is not this board's charger.)
113. **The PlayBook's charger is the ISL9519 at I²C1 0x09**, not the TWL BCI.
     VendorID `0xFE=0x0049`, DeviceID `0xFF=0x0003`; registers `0x14` charge
     current, `0x15` max system voltage, `0x3E` min system voltage, `0x3F`
     input current; field steps 128 mA / 16 mV / 256 mV / 128 mA.  Host-port
     input limit 384 mA on 1917 — below the system load, which is why 1917
     looks like it is not charging on a PC port.
114. **The ISL9519 needs non-zero `0x14`/`0x15` and a refresh every ≤175 s.**
     The driver writes all four in RIM's order and refreshes every 5 s.
115. **WDT2's load is writable only while stopped via `WSPR`**; poll `WWPS`
     after every WDT write (fixed delays drop the sequence).  The kernel now
     uses 58.6 s and pets every 10 s; the expected dmesg line is missing on
     7.3.3 — verify by `WLDR`/`WCRR`.
116. **`CM_DIV_M5_DPLL_PER` is the DSS clock divider — never write the
     reference's `0x329`.**  It glitches the panel; `0x204` restores it.  This
     is why the FREF route is closed.
117. **The WL1283 `FREF_CLK2_REQ` input is not used** — QNX leaves
     `0x4A31E058 = 0x000B0003` (pad0 mux 3 `gpio_wk31`, input off).
118. **The FREF2 output is SCRM `AUXCLK1` (`0x4A30A314`), not the M5
     divider.**  QNX = `0x00020100` (12.8 MHz); matching it does not fix CMD5.
119. **QNX enumerates at 24 MHz with CMD5 arg `0x00FFFF00`**; Linux's
     400 kHz/arg 0 is not the difference (both tested; `err=-110`, no
     response).
120. **A runtime MMC5 `unbind`/`bind` wedges the ttyGS0 shell** — use a boot
     image, not a rescan.
121. **The WL1283 main clock is the 26 MHz TCXO; FREF is not needed before
     enumeration.**  The chip's TCXO/power/reset is the remaining unknown.
122. **Never retry `pkexec` after a failure on this host** — faillock can lock
     the host for 10 minutes; ask the operator.
126. **The QNX `winchester` powerman preset has no WLAN rail or clock** — its
     WLAN authorities only gate standby/OSWR; `ldo_vaux1`/`vaux3` are the
     display rails; `devpm-omap4.so` is PRCM/voltage/DVFS; `winchester_phy_ctrl`
     is the USB PHY.  Do not re-open the powerman lead (report 187).
127. **The WL1283 IRQ is the accessible chip-alive indicator; WL_EN is not
     brought out.**  Rightmost front pad = GPIO2_21/WL_IRQ: 1.8 V when the chip
     is up, 0 V when WL_EN is low; it is 0 V under Linux with WL_EN high.  The
     accessible DC points are 1.8 V and 0 V; no 3.3 V chip rail (report 188).
128. **A 200 ms MMC5 post-power-on settle is negative** — `0x0B38612D` still
     times out CMD52/CMD8/CMD5 (report 188).
129. **Skipping the SDIO reset (CMD52) is negative** — `0xAC98BCAD` runs
     CMD0 → CMD8 → CMD5 and CMD5 still times out (report 188).
130. **The WL_EN reset pulse breaks USB warm-reset enumeration.**  The host
     missed the gadget connect until a re-plug; the A/B isolates the session-32
     `pb_wl1283_reset_pulse()`: `0x90BDE221` (FREF off, pulse on) fails,
     `0x0353A241` (FREF off, pulse off) enumerates by itself in 14 s.  The FREF
     enable and the session-33 MMC changes are excluded.  The pulse is removed
     (it was also negative for CMD5); a UDC `soft_connect` toggle remains a
     fallback (report 188).
131. **The PPS `hw_info` inventory has no WLAN node; the syspage `WLAN` node is
     only EN/INT.**  `hw_info_pps` is a runtime board/eMMC/CPU/audio report and
     links no HWI library; the `WLAN` node lives in the syspage and the live
     dump is `WLAN_EN_GPIO=54,WLAN_INT_GPIO=53` with an empty `location`
     (report 189).  Dump it with `tools/qnx-hwi.c`; build it **`-static`** —
     the device's 2014 libc does not export `hwi_off2tag`/`__hwi_find_string`/
     `hwi_next_tag`/`hwi_next_item`, so a dynamic build dies with
     `ldd:FATAL: Unresolved symbol "hwi_off2tag"`.  Do not expect the PPS
     inventory to name a rail/clock.
132–137. The H1/GPIO/CLK32KG traps of session 35 (full text in
     `KNOWN-ISSUES.md` §2).  The WL1283 is stopped; do not re-run them.
138. **An enabled I²C controller is not a wired bus.**  `&i2c2` was
     `status = "okay"` with no pinctrl, so `0x4A100126`/`0x4A100128`
     (i2c2_scl/sda) stayed at `0x011F` = safe mode; every i2c-1 transfer timed
     out (`Connection timed out`, not `Remote I/O error`) and a full-bus
     `i2cdetect` blocked the console.  Pads `0x0110` (mux 0, input, pull-up)
     fixed it; `0x65` ACKed.  Probe one address, never the whole bus.  The DTS
     now carries `i2c2_pins` (report 193).
139–140. The patch-walk build artifacts (139) and the touch Munin bootloader /
     `reg0` command set (140); full text in `KNOWN-ISSUES.md` §2.
141. **A blocking retry in a built-in probe delays the boot and breaks USB
     warm-reset enumeration.**  The touch probe at ~2.7 s returns
     `-EREMOTEIO`; a 40 × 50 ms retry inside `probe()` moved the boot and the
     host configured the gadget only after a replug.  Run the controller boot
     in a delayed work and keep probe non-blocking (the driver does now;
     report 194).

## 4. Where things are in this workspace

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console commands | `docs/project/DEVELOPMENT.md` |
| host prerequisites, toolchains | `docs/project/SETUP.md` |
| board DTS and patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + exact config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| charger driver | `local/kernel-tree/drivers/power/supply/isl9519.c` |
| WDT2 policy | `local/kernel-tree/drivers/irqchip/irq-gic.c` |
| chainloader source | `boot/chainloader/chainloader_kernel.S.in` |
| tools | `tools/` (see `tools/README.md`); the QNX hwinfo dumper is `tools/qnx-hwi.c` |
| QNX-side WL1283 RE | `docs/reports/189-wl1283-qnx-side-re-2026-11-08.md` |
| WL1283 fresh-eyes dossier | `docs/reports/190-wl1283-dossier-2026-11-08.md` |
| WL1283 external review + next plan | `docs/reports/191-wl1283-external-review-2026-11-08.md` |
| WL1283 startup-playbook audit + S1/S2/S3/H1 | `docs/reports/192-wl1283-startup-playbook-audit-2026-11-08.md` |
| touch bus + i2c2 pad mux (session 36) | `docs/reports/193-touch-bus-and-pad-mux-2026-11-08.md` |
| touch Munin protocol (session 36) | `docs/reports/194-touch-munin-protocol-2026-11-08.md` |
| TWL6030 both-OS register captures | `docs/captures/twl6030-linux-2026-11-08.txt`, `docs/captures/twl6030-qnx-2026-11-08.txt` |
| images, sealed IFS, backups | `local/images/` (gitignored) |
| 6.15.11 reference tree | `local/kernel-tree-6.15.11/` |
| OMAP4430 TRM (text) | `hardware/swpu231ap.pdf.txt` (untracked/local) |
| public/private repositories | `docs/project/REPO-MANAGEMENT.md` |

## 5. Working rules

* **After every console command, dump the log tail and read it.**
* **Write the durable fact into the repository in the same turn.**
* **Do not budget work against a context window**; the operator decides when to
  stop.
* **Static RE only for subagents** (one process owns the USB link).  Give them
  file-only tasks and never let them run a `tools/` script; skeleton first,
  exact paths, an explicit "stop when answered".  The old log-audit
  requirement is dropped (the session-30 reset was low power, not an agent).
* **Never present a guess as a fact.**  External facts are looked up and cited
  (DECISIONS §32, trap 105); device facts prefer a fresh read over an old note.
* **One variable per boot**; the whole-map diff is the highest-yield technique.
* **Update the docs as you go**; `STATE.md` and `KNOWN-ISSUES.md` go stale first.
* **Restore the sealed IFS at the end of every session** unless the operator
  ends on our image.  Session 36 ended on the known-good `0xE36C679A` (region 0;
  no flash this session); region 0 is the only region that needs restoring.

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 37 (absolute; Linux-era 31)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md,
KERNEL-PORT.md (especially §7), ROADMAP.md item 9 (steps 4 and 5),
KNOWN-ISSUES.md (traps 9-141, especially 105-141), DEVELOPMENT.md sections
3/3a/4 and 7a, DECISIONS.md (17-45), reports 176, 180/181/182, 183, 184-194,
sessions/session-31.md … session-36.md.

WHERE IT STANDS: the patch set is on Linux 7.3.3 and the unit boots it.
The known-good image is region-0 0xE36C679A
(local/images/kernel-0xE36C679A-wdt58s.img) + region-1
local/images/rootfs-region1-7.3.3-charger.ext4 (17 modules).  The media
holds the session-36 touch image **0x04AF1433** in region 0; the known-good
0xE36C679A is the way back.  The charger is an Intersil
ISL9519 at I2C1 0x09 (report 183); WDT2 runs the reference's 58.6 s window
with a 10 s pet (trap 115).  The 6.15.11 tree/tarball/patch are intact as the
backup.  The 7.3.3 patch is 7 620 lines / 48 files (sha256 28e48f56...) and
reproduces the tree byte-for-byte; config kernel/winchester-7.3.3.config
(CONFIG_CHARGER_ISL9519=y, CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y,
CONFIG_KERNEL_XZ=y).  Session 36 added an `i2c2_pins` pinctrl group and the
Munin touch driver to the DTS; the touch image `0x04AF1433` is on media and the touch works.

PHYSICAL STATE: REGION 0 HOLDS THE TOUCH IMAGE 0x04AF1433 (the
known-good 0xE36C679A is the way back); region 1 is the 7.3.3 charger rootfs;
the 1917 IFS is NOT on media.  BOOTCTR_FCTRY reads 0x2b; the pack is Full
(100 %, 4.156 V).
To return to TabletOS, flash region 0 only:
tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs
--dest-block 0x825 --from-bootrom --password 0000 --apply, then re-read
/dev/rpmb/BOOTCTR_FCTRY.  Do NOT restore local/images/ifs0.sealed.bin
(2.0.0.4869, trap 84).

WHERE IT IS: the WL1283 hunt was FORMALLY STOPPED at the start of session 36
(do NOT repeat traps 116-137).  Item 9 step 5 has started.  The touch BUS is
solved (report 193, trap 138, DECISIONS §44): the Cypress touch is on OMAP
I2C2 0x48072000 = Linux `i2c-1` (QNX /dev/i2c1) at 0x65, because 1917
graphics.conf selects `driver = munin` and libmtouch-munin opens /dev/i2c1.
The Linux bus was dead because i2c2 pads 0x4A100126/0x4A100128 were 0x011F
(safe mode); writing them to 0x0110 (mux 0, input, pull-up) made 0x65 ACK
immediately.  The DTS now has i2c2_pins.  Wiring: power GPIO1_14, reset
GPIO4_8, IRQ tp_intr=1035 = GPIO2_3 (inferred).  **The touch WORKS** (report
194, DECISIONS §45): the driver boots the Munin controller and `input0`/
`event0` reports touches; the media image is `0x04AF1433`.  NEXT (operator
direction): archive the WL1283 code, re-derive the DTS from 2.1.0.1917, and
restart MMC5 (HANDOFF §0c, DECISIONS §46); then the WM8994 codec-ID
experiment.  Do NOT run a whole-bus i2cdetect (it blocks the console; probe one
address).  hd3/BB10/PlayBook OS work remain parked.

HARDWARE RE RULE: OS 2.x only (live 1917 oracle, the 1917 /base QNX6 image,
the 1917 IFS tree, the 4869 IFS dump).  Never use BB10 images for PlayBook
hardware facts (DECISIONS §34, trap 110).  Ask the reference OS what it maps
(pidin mem / pidin irq) before writing a DT node (DECISIONS 30); do NOT
characterise a device on TabletOS if a QNX driver owns it (DECISIONS 31,
trap 103).

An agent NEVER writes RPMB (SAFETY.md section 6); the CRNG needs `entropy
<hex>` before any write (trap 87).

BENCH: door = 169.254.0.1 (the host link already carries 169.254.0.2/16 —
do not re-add it).  tools/pbssh.sh logs in as ROOT.  Start
tools/door-hold.sh with setsid (never `pgrep -af door-hold` or `pkill -f
door-hold`).  After a cold TabletOS boot the qconn handshake can take 1-2
minutes; wait for "[+] Authenticated. Key uploaded." before SSH.  If SSH then
asks for a password, the holder is stale or failed: list with
`ps -ef | grep '[d]oor-hold'` and `[B]erryShell-V4`, kill the exact PIDs (never
`pkill -f`), and start a fresh `setsid tools/door-hold.sh` (session 34).  On our
Linux image there is NO door: use tools/pbcon.py (the shell prompt is
`pb> `), and reboot with tools/pbcon.py 'reboot -f'.  `pkexec chmod 666
/dev/ttyACM0` after EVERY boot and the operator must answer the prompt.  If
it fails, DO NOT retry: the host's faillock can lock for 10 minutes; ask
the operator to run the chmod.  Run the USB/flash tools as the USER — never
pkexec.  The device password is 0000, sent once, never retried.  Rebuild the
QNX oracle tools after `. tools/env.sh`: pbmem3 = `ntoarmv7-gcc -O2 -o
/tmp/pbmem3 tools/memdump3.c`, and /tmp is wiped on reboot.

STATIC RE: the REA MCP server exposes Ghidra 12.1.4 (`open_binary`,
`binary_overview`, `analyze_function`, `procedure_pseudo_code`, `search_strings`,
`xrefs`, `read_bytes`); raw ARM import `format=raw-binary, architecture=arm,
entry_point=0x80100000`.  Reconcile addresses with `ntoarmv7-readelf`/`objdump`
(Ghidra may add +0x10000 on an ELF); QNX device tools build `-static`.
See DEVELOPMENT.md §7a.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only
when only it changed; flash both regions in one bootrom session only when
both changed.  Operator's call when to stop; the operator also decides when
to wrap up.

SUBAGENTS: static RE only, no bench — they share your shell.  Give them
file-only tasks and never let them run a `tools/` script; skeleton first,
exact paths, an explicit "stop when answered".  The old log-audit
requirement is dropped: the session-30 reset was low power, not an agent
(traps 108/109).  Check the battery before diagnosing any reset (trap 108).

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.
```
