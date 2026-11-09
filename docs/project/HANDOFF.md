# Handoff — session 38 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 38 = Linux-era 32**.  See
`docs/sessions/NUMBERING.md`.

> **Session-37 result, in one line:** the WL1283 code is **archived** (Task 1),
> the board DTS is **re-derived from 2.1.0.1917** (Task 2, report 195), the
> minimal 1917-truth `&mmc5` host was **booted and CMD5 is still negative**
> (Task 3, report 196) — the DT model is proven applied and 1917-identical and
> the chip still does not come up under Linux — and the **per-unit WLAN
> calibration was found in the QNX `hd5` partition** (report 197).  The unit is
> on **TabletOS 2.1.0.1917** (`ti0` UP), `BOOTCTR_FCTRY = 0x32`.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain, the 64 MiB ext4 BusyBox root in region
1, and the peripherals: I²C4 motion sensors (BMA150/HMC5883L/MPU3050), TMP103,
BQ27541 gauge, FAN5702 LED, CM3217 light sensor, ISL9519 charger, and the
**Munin touch (works, `input0`/`event0`)**.  WDT2 runs the reference's 58.6 s
window with a 10 s pet.

**The patch.** `kernel/patches/0001-playbook-winchester-7.3.3.patch` is
**7 305 lines / 46 files on pristine linux-7.3.3**, sha256 `d0d00009…`,
**0 rejects and 46/46 files byte-identical** (regenerate with the tree-walk in
`kernel/patches/README.md`; clean the tree first — trap 139).  Config
`kernel/winchester-7.3.3.config` (`CONFIG_CHARGER_ISL9519=y`,
`CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`, `CONFIG_KERNEL_XZ=y`).
**The WL1283 code is NOT in it** — it is in
`kernel/archive/wl1283/wl1283-instrumentation-7.3.3.patch` (517 lines, applies
to the clean tree with `patch -p1`; round-trip verified).
**The 6.15.11 backup is intact** (`local/pristine/linux-6.15.11`,
`local/kernel-tree-6.15.11`, `kernel/patches/0001-playbook-winchester-6.15.11.patch`);
do not delete it; its patch is incomplete (trap 106).

**The board DTS** (`kernel/source/omap4-winchester.dts` ↔
`local/kernel-tree/.../omap4-winchester.dts`, keep identical) now carries the
**re-derived minimal 1917-truth `&mmc5`** (report 195 §3.4): `sdmmc5_pins`
(0x148–0x152 mux 0/pull-up), `wl1283_ctrl_pins` (WL_EN pad 0x07c = 0x0003), the
`wl1283-en-hog` (gpio 22), `vwl1283` fixed **1.8 V** OCR window, 4-bit,
`non-removable`, `ti,non-removable`, `no-sd`, `no-mmc`, `max-frequency 24 MHz`;
WL_IRQ 0x07a stays in `bridge_pins` (input/`0x410b`).  It probes and **fails
CMD5**.  Its header is corrected (no longer 2.0.0.4869).

## 0a. The media, and the way back

* **region 0 = the 1917 IFS** (`local/work/ifs-shim-sealed.ifs`, sha256
  `b52195bb…`, 489 frames), flashed this session for the calibration check;
  **TabletOS 2.1.0.1917 is running** (`0fca:8012`; `ti0` UP, 40+ scans,
  MAC `14:74:11:8f:9c:c3`, `wifi_power::on`), `BOOTCTR_FCTRY = 0x32`.
  This is the **default restored state** (the session-end rule).
* **region 1 = the 7.3.3 charger rootfs** (`ba275807…`, 17 modules); unchanged.
* **Linux images** (region 0 only): the Task-3 experiment **`0x0DC36A11`**
  (`0x91A514`, kernel `7.3.3 #24`; mmc5 enabled, CMD5 negative, touch works);
  the WL1283-free `0x05819994` (mmc5 disabled); the known-good
  **`0xE36C679A`** (`local/images/kernel-0xE36C679A-wdt58s.img`) and the touch
  image **`0x04AF1433`**.  To return to Linux, flash one of them at block
  `0x825` (region 0 only).  **Do not restore `local/images/ifs0.sealed.bin`**
  (2.0.0.4869, trap 84).
* The 1917 RCFS at region-0 `0xC00000` and the 1917 UFS are on media, untouched.

```bash
# back to TabletOS (what is on media now), region 0 only:
nohup python3 tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs \
    --dest-block 0x825 --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
# then reboot whatever is running (pbcon for Linux, pbssh for TabletOS), >60 s window
```

## 0b. Item 9 step 4 (WL1283) — software surface exhausted AGAIN

**Not invalidated: reports 180–192 and traps 116–141 stand.**  Session 37 added
the 1917-truth redo the operator asked for, and it is **negative**:

* **Task 1 (report — archive).**  The old WL1283 instrumentation is out of the
  active tree/patch into `kernel/archive/wl1283/` (SDIO CMD5 `arg=0xFFFF00`
  quirk, `pb_sdio_ocr_quirk`, `pb_init_hz`, `pb_enable_fref_clk2`,
  `pb_wl1283_reset_pulse`, the `PBCMD`/`PBCLK`/`PBD`/`PBOCR`/`PBVOLT` prints,
  the `480d5000` PBGPIO dump, and the old DTS nodes).  General eMMC `PB*`
  prints and `pb_event` stay (KERNEL-PORT §7.2's prune is separate).
* **Task 2 (report 195, the 1917-truth DTS audit).**  Every functional node is
  cited from 2.1.0.1917 (live reads, the 1917 `/base`, the 1917 IFS tree, the
  shipped blobs).  The 1917 `/base` was mounted read-only with
  **`local/vendor/bb10mt/bb10mt qnx6 mount`** and read directly: hwio
  `pad_config` 248×20 / `gpio_config` 72×32 (gpio 53 input/falling signal
  `0x59`, gpio 54 output/low signal `0x58`), `graphics.conf` (munin,
  `tp_intr=1035`, `0x65`), `tiwlan_winchester.ini`.  **startup-playbook** was
  extracted (IFS imagefs `0x908`, `0x44008` B, sha256 `bc7d3a41…`) and its
  Winchester GPIO table (image `0x80122750`) is **byte-identical** to the hwio
  `gpio_config`.  Report 189's `gpio_config` field labels corrected
  (KNOWN-ISSUES §3.22).
* **Task 3 (report 196, the one experiment).**  Image `0x0DC36A11`, region 0
  only.  `mmc4` probes (`f_max=24000000`, 400 kHz init), `sdio_reset`(CMD52) →
  `go_idle`(CMD0) → `attach_sdio`(CMD5) → **`attach_sdio no` at 2.829 s**, no
  voltage warning.  **The DT is provably applied and 1917-identical**:
  `0x4A100148/14C/150 = 0x0118`, `0x07C = 0x0003`, `0x07A = 0x410B`,
  `GPIO2_DATAOUT = 0x31413200` (WL_EN high) but **`DATAIN = 0x01000010`
  (WL_IRQ bit 21 = 0 — chip not up)**, exactly report 188.  The eMMC, touch,
  bma150 and loop-root are healthy.
* **Conclusion:** the DT model is not the missing piece; the remaining
  difference is **chip-side (VBAT / 26 MHz TCXO / SLOW_CLK) or OMAP HSMMC
  sampling**, which needs a **board view or logic analyser**.  Do not re-run
  the closed experiments (traps 116–141) without stating what is new.

## 0c. Item 9 step 5 (audio + touch)

**Touch — WORKS.**  The Cypress is on I²C2 `0x48072000` = Linux `i2c-1` at
`0x65` (`graphics.conf driver = munin`), pads `i2c2_pins`; the small driver
`drivers/input/touchscreen/playbook-munin.c` boots the Munin controller and
reports on `input0`/`event0` (reports 193/194, DECISIONS §44/§45).  The probe
must not block (trap 141).  Open: axis calibration/orientation (vendor
1280×768 → 1024×600) and the IRQ pad (`tp_intr=1035` = GPIO2_3, inferred).
**The touch still works on the Task-3 image.**

**WM8994 audio — not started.**  MFD + codec on I²C4 `0x48350000` (Linux
`i2c-3`), 7-bit `0x1a`; `CONFIG_MFD_WM8994`/`CONFIG_SND_SOC_WM8994` are not
enabled.  First step is a **read-only codec-ID check**:
`i2ctransfer -y 3 w2@0x1a 0x00 0x00 r2` should return **`0x8994`**.  Then the
machine link: the 1917 stack is `deva-ctrl-omap4abe-wm8994` (McBSP + ABE); the
likely Linux path is `simple-audio-card` + McBSP, blocked by `&l4_abe`
(disabled; WD_TIMER3 hangs on enable) — enable the segment with the non-audio
targets off.  McBSP is probably 2.  See reports 176/183, `HANDOFF` history in
git.

## 0d. The WLAN calibration partition (new, report 197)

The operator's lead was right.  QNX exposes **`hd4`** (512 KiB, the MCT header,
magic `0x92BE564A`), **`hd5`** (512 KiB, the **factory/calibration partition**)
and `hd6` (empty).  `hd5` holds:
* the unit serials (`PRU2QCIK014M03D23`, `DRU2QCI14P7530`) + 2011-04 factory
  test timestamps;
* a **19 118-byte TI WL128x radio-parameters INI** (`TxPower=205`,
  `XTALItrimVal=4`, `TxBiPReferencePower`/`RxFemInsertionLoss`/
  `RxTraceInsertionLoss` 2.4/5 GHz, `TxPDVs*`; "Quanta Ver 4.3");
* a **44 405-byte binary calibration blob**.

The **assigned WLAN MAC** is in NVRAM `deviceinfo` (`WLANMAC = 1474118f9cc3`).
Extracted **read-only** (dd→`/tmp`, scp) to
`local/work/1917/calib/{hd5-header.bin,wlan-calib.ini,wlan-calib-blob.bin}` and
`local/work/1917/parts/{hd4,hd5}.bin`.
**Use:** this is the source for a mainline `wl128x-nvs.bin` (MAC + TX
calibration) **after** the SDIO function enumerates; it cannot fix CMD5.  The
next session can build the NVS file from it (file-only) whenever useful.

## 1. Useful local extracts (new)

* **`local/work/1917/base/`** — the whole 1917 `/base` (333 MB), copied out of
  the mounted `rcfs-1917.rcfs` (`bb10mt qnx6 mount -i … -m …`; unmount with
  `fusermount3 -u`).  No remount needed for 1917 `/base` reads.
* **`local/work/1917/ifs/`** — the 1917 IFS tree + `proc/boot/startup-playbook`
  (extracted from `ifs1917.imagefs` at `0x908`, `0x44008` B).
* **`local/work/1917/calib/`**, **`local/work/1917/parts/`** — the WLAN
  calibration and the pulled partitions (report 197).
* **`local/vendor/bb10mt/bb10mt`** — the QNX6/container tool (version
  0.5.9.999); use it for `qnx6 mount/compact/mkfs/fsck` and `rcfs`.
* Report **193/194** (touch), **195** (DTS audit), **196** (mmc5 negative),
  **197** (calibration).

## 2. Build and flash (7.3.3)

```bash
REPO=$PWD; . tools/env.sh
K=local/kernel-tree
arm-linux-gcc -static -O2 -Wall -o initramfs/root/console initramfs/console.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/init    initramfs/init.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/pbrpmb  initramfs/rpmb.c
rm -f $K/usr/initramfs_data.cpio*
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 modules
ZIMAGE=$K/arch/arm/boot/zImage DTB=$K/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb \
    python3 tools/build_kernel_image.py     # prints CRC; image must be < 0xB00000
```

The printed CRC must differ from the previous build (trap 39).  Flash region 0
only when only it changed; both regions in one bootrom session only when both
did.  Start the flasher immediately before the reboot; the bootrom window is
60 s; `tools/bbusb.py` resets the loader itself after the write.

## 3. Bench (current state: TabletOS)

* **Unit is on TabletOS 2.1.0.1917** (`0fca:8012`).  Door = `169.254.0.1`; the
  host link `enp8s0f3u1` carries `169.254.0.2/16` (do not re-add).  Start the
  door with `setsid tools/door-hold.sh` (never `pgrep -af door-hold` /
  `pkill -f door-hold`); after a cold boot it can take 1–2 min.  `tools/pbssh.sh`
  logs in as **root**.  If SSH asks for a password, kill the stale holder by
  exact PID and restart.
* **On our Linux image** there is no door: `tools/pbcon.py` (prompt `pb> `),
  reboot with `tools/pbcon.py 'reboot -f'`.  `pkexec chmod 666 /dev/ttyACM0`
  after EVERY boot; if it fails, do NOT retry (faillock) — ask the operator.
  Run USB/flash tools as the **user**, never `pkexec`.
* Device password `0000` (sent once, never retried).  An agent **never writes
  RPMB**; NVRAM is read-only.
* Reads via `plink`-style impossible; use the tools.  The 1917 QNX tools that
  run on the device must be `-static` (trap 131).

## 4. Traps added this session

* **142.** A DT node with no `status` is **enabled**; deleting the board
  override does not disable it.  `omap4-l4.dtsi`'s `mmc5` has no status, so the
  base DTS must set `&mmc5 { status = "…" }` explicitly.
* **143.** Archiving the WL1283 `PBCMD`/`PBOCR` prints **removes the CMD5 error
  code** from the log.  The minimal-host negative is `attach_sdio no` +
  `Failed to initialize a non-removable card`; the absence of a "no support for
  card's volts" warning places the failure at CMD5.  Re-add a one-line trace
  (or apply the archive patch) if the exact `err` is needed.

The full trap list (9–143) is `KNOWN-ISSUES.md` §2; the ones that end sessions
are summarised in the previous handoffs (kept in git history).

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| archived WL1283 code | `kernel/archive/wl1283/` |
| 1917 `/base`, IFS, calibration | `local/work/1917/{base,ifs,calib,parts}/` |
| touch driver | `local/kernel-tree/drivers/input/touchscreen/playbook-munin.c` |
| charger driver | `local/kernel-tree/drivers/power/supply/isl9519.c` |
| QNX hwinfo dumper | `tools/qnx-hwi.c`; I²C `tools/pbi2c` |
| reports | `docs/reports/176…197` (195/196/197 are this session) |
| sessions | `docs/sessions/session-37.md` (this session) |

## 6. Working rules

* After every console command, dump the log tail and read it.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  The whole-map diff is the highest-yield technique.
* **Never present a guess as a fact.**  External facts are looked up and cited
  (trap 105); device facts prefer a fresh read over an old note.
* **OS 2.x only** for PlayBook hardware facts (trap 110, DECISIONS §34).  Ask
  the reference OS what it maps before writing a DT node (DECISIONS 30).
* Subagents: static/file-only, never a `tools/` script (trap 109).
* **Restore the sealed IFS at the end of every session** unless the operator
  ends on our image.  Session 37 ended on **TabletOS** (1917 IFS, counter
  `0x32`) — the default restored state.  Region 0 is the only region changed by
  a Linux test image, and reflashing it returns to Linux.

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 38 (absolute; Linux-era 32)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), KERNEL-PORT.md §7, ROADMAP.md item 9 (steps 4 and 5), KNOWN-ISSUES.md
(traps 9-143, especially 105-143, and §3 corrections 3.22), DEVELOPMENT.md
§3/3a/4/7a, DECISIONS.md (17-46), reports 176, 180-197, sessions/session-36.md
and session-37.md.

WHERE IT STANDS: the WL1283 code is archived out of the active 7.3.3 tree/patch
(kernel/archive/wl1283/); the base patch is 7 305 lines / 46 files (sha256
d0d00009...) and reproduces the tree byte-for-byte.  The board DTS carries the
re-derived minimal 1917-truth &mmc5 (report 195) and it PROBES BUT FAILS CMD5
(report 196): the DT model is applied and 1917-identical, WL_EN is high, but
GPIO2_DATAIN bit21 (WL_IRQ) is 0 — the chip is not up under Linux.  The
remaining evidence is chip-side (VBAT/TCXO/SLOW_CLK, MMC5 CLK/CMD/DAT0) and
needs a board view or logic analyser.  The per-unit WLAN calibration was found
in the QNX hd5 partition (report 197) and extracted to local/work/1917/calib/.

PHYSICAL STATE: REGION 0 HOLDS THE 1917 IFS (TabletOS 2.1.0.1917 is running;
ti0 UP, MAC 14:74:11:8f:9c:c3); region 1 is the 7.3.3 charger rootfs;
BOOTCTR_FCTRY = 0x32; the pack was healthy (~97%, 4.03 V).  Linux test images
(region 0 only, flash at block 0x825): 0x0DC36A11 (Task-3, mmc5 enabled),
0x05819994 (mmc5 disabled), 0xE36C679A (known-good), 0x04AF1433 (touch).
Do NOT restore local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

WHERE IT IS: item 9 step 4 (WL1283) is software-exhausted AGAIN; do NOT repeat
traps 116-141 without stating what differs.  Item 9 step 5: the TOUCH WORKS
(reports 193/194); the WM8994 AUDIO HAS NOT STARTED (codec-ID at I2C4 0x1a
first, then the McBSP/l4_abe machine link).  The WLAN calibration partition is
a new asset for the post-enumeration wl12xx path.

BENCH: door = 169.254.0.1 (host link 169.254.0.2/16 on enp8s0f3u1 — do not
re-add).  tools/pbssh.sh logs in as ROOT.  Start tools/door-hold.sh with setsid
(never pgrep/pkill -f).  After a cold TabletOS boot the qconn handshake can
take 1-2 minutes.  On our Linux image there is NO door: use tools/pbcon.py
(prompt `pb> `), reboot with tools/pbcon.py 'reboot -f'; pkexec chmod 666
/dev/ttyACM0 after EVERY boot (operator answers; never retry a failed pkexec).
Run USB/flash tools as the USER, never pkexec.  Device password 0000, once.
An agent NEVER writes RPMB; NVRAM is read-only (SAFETY §1/§6).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4 (open_binary,
binary_overview, analyze_function, procedure_pseudo_code, search_strings,
xrefs, read_bytes); raw ARM import format=raw-binary, architecture=arm,
entry_point=0x80100000.  Reconcile with ntoarmv7-readelf/objdump (Ghidra may
add +0x10000 on an ELF).  QNX device tools build -static.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
Operator's call when to stop and to wrap up.

NEXT (operator's call): (a) the chip-side WL1283 measurements (needs a board
view/logic analyser); (b) the WM8994 codec-ID + machine link; (c) touch axis
calibration/orientation + IRQ; (d) build wl128x-nvs.bin from hd5 (file-only).
The unit is on TabletOS; region 0 is the only region a Linux test changes.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.
```
