# Handoff — session 51 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 52 = Linux-era 46**, written at the
session-51 close.  See `docs/sessions/NUMBERING.md`.

> **Session-51 result, in one line:** **the WL1283 WLAN works — it scans.**  Two
> blockers fell: the `-110` recovery loop was the chip's **ELP** sleep (the
> driver now keeps it in **CAM**), and the never-completing scan was the
> **5-sr firmware** — with `wl128x-fw-4-sr.bin` (`Rev 7.3.5.0.98`) `wlan0` is
> stable **and** `iw dev wlan0 scan` returns real results (17–21 BSS, both
> bands).  The floor is lowered to 7.3.5.0 and the SR firmware name points at
> 4-sr.  Region 0 is **unchanged** (`0x9712A086`); region 1 is the new
> **`f159a221…`**; patch **59 files / 8 713 lines**, sha256 `2be9e99c…`.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a **real userland on the
eMMC** (region 1), and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console **and its RNDIS network link with ssh**, the whole display chain,
the 64 MiB ext4 BusyBox root in region 1, the peripherals (I²C4 motion sensors,
TMP103, BQ27541, FAN5702, CM3217, ISL9519 charger), the **Munin touch**
(calibrated, IRQ wired), the **WM8994 audio** (headphones + both speakers,
48 kHz), and the **WL1283 WLAN** (enumerates, firmware `Rev 7.3.10.0.142`
boots, `wlan0` **stable**; MAC `14:74:11:8f:9c:c3`).  **ssh over USB is the
bench default** (`tools/pbssh.sh`, 2.3 MB/s).

**Remaining WLAN work:** **association** — the WLAN scans (17–21 BSS, both
bands), and the client side is shipped (`iw`, `wpa_supplicant`, `wpa_cli`,
`wpa_passphrase`, signed `regulatory.db`), so a station config is the next step
and the point where real throughput can be measured.  Then the per-unit
**calibration NVS** (located in NVRAM; matters for TX power) and a Linux **BT**
bring-up.  Two open questions, neither blocking: *why* the 5-sr firmware panics
on scan (report 235 §3) and *why* the ELP wake is not honoured (232 §6).

## 0a. The media, and the way back

* **region 0 = the session-50 image** (`boot/chainloader/kernel.img`, CRC
  **`0x9712A086`**, `0x939A74` bytes; kernel `7.3.3` **#60**).  **Unchanged in
  session 51** — session 51 is module-only, so only region 1 was flashed.
* **region 1 = the WLAN rootfs** `f159a221…`
  (`local/images/rootfs-region1-wl1283-scan.ext4`, 67 108 864 B, 34 modules,
  **both** `wl128x` SR firmware files, dropbear; the changed module is
  `wl12xx.ko` md5 `2fb86c4d…`, built **in-tree** so it carries no `(O)`),
  **plus the client tooling**: `/usr/bin/iw` (6.17), `/usr/sbin/wpa_supplicant`
  + `wpa_cli` + `wpa_passphrase` (2.12) and the signed
  `/lib/firmware/regulatory.db` + `.p7s` (wireless-regdb 2026.09.03).
* The unit is **on our Linux image** (`0fca:8010`, console `pb> `, **ssh on
  `169.254.0.1`**).  `rcS` loads the wireless stack but does **not** raise the
  link — `ip link set wlan0 up` is a manual step.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`, then reboot.  It enumerates `0fca:8012`; qconn
  `169.254.0.1:9090`, root ssh — use **`PB_LINK=door tools/pbssh.sh`**.  The
  region-0 RCFS slot holds the **full 1917 RCFS** (trap 227); for the *minimal*
  oracle flash the pruned pair `local/work/minimal/setB.signed`.  `/tmp` is
  wiped by a reboot.  Its `hd3` carries `/var/ti1283/…` from session 49.
* **To stock TabletOS:** flash **two components** — `local/work/ifs-shim-sealed.ifs`
  at `0x825` **and** `local/work/rcfs-1917.rcfs` at `0x8D5`.
  `wayback.signed` does **not** boot this armed unit (trap 226).  **Do not
  restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).

## 0b. The WL1283 — it works now, and why

### The `-110` was ELP (report 232)

The recovery loop is **not** a firmware/chip mismatch, the block size or the
calibration.  It is the firmware's **ELP** (its own sleep):

* The failing transfer is the **FW-status read**: CMD53 to **`0x1efc0`, 84
  bytes (`0x54`), byte mode** (`fixed = 0`), block size **256**, from the
  WL_IRQ thread right after `firmware booted`; it then times out (~1.5 s after
  the boot line, with no `mmc_ios` change in between).
* The same read **succeeds** whenever the chip is awake, so the transfer is
  legal and the *state* was the fault.
* Measured causally: `power/control = on` (no runtime PM) is stable;
  `power/control = auto` + `sleep_auth = 0` (CAM) is stable; and
  `echo 2 > /sys/kernel/debug/ieee80211/phyN/wlcore/sleep_auth` on that working
  system reproduces the `-110` and the loop within seconds.

**The fix:** `wl12xx_default_conf.conn.sta_sleep_auth = WL1271_PSM_CAM`
(`drivers/net/wireless/ti/wl12xx/main.c`, was `WL1271_PSM_ILLEGAL` → ELP).
Dynamic PS is untouched; only the host-driven ELP is off.

**How the address was found, and the trap in the old recipe:** `wlcore_sdio
dump=1` prints **per transfer**, and on this board that changes the timing
enough to hide the failure (trap 52) — the prescribed "read the last READ before
the `-110`" does not work.  What worked was a temporary `dev_err` **on the error
path only** in `wl12xx_sdio_raw_read()` (`addr`, `len`, `fixed`,
`func->cur_blksize`); it prints nothing on success, so it does not perturb.  It
was reverted (`wlcore/sdio.c` is `cmp`-identical to pristine).

**The scan was the firmware (report 235).**  `wl128x-fw-5-sr.bin`
(`Rev 7.3.10.0.142`, the newest wl128x blob in linux-firmware *and* in TI's own
firmware tree — nothing newer exists) boots and keeps the interface up but
**panics on the first scan** (no scan-complete event → the driver's 30 s
`WL1271_SCAN_TIMEOUT` → unintended recovery; the panic-log read proved the
crash, trap 241).  `wl128x-fw-4-sr.bin` (`Rev 7.3.5.0.98`) **scans** on the same
driver, NVS and board: 17, 20 and 21 BSS on three runs, both bands, no errors —
and the same from flashed media after a reboot.  The port therefore lowers
`wl12xx`'s SR floor to **7.3.5.0** (`WL128X_MAJOR_SR_VER 5`,
`WL128X_MINOR_SR_VER 0`) and sets `WL128X_FW_NAME_SINGLE` to the 4-sr file; both
files stay installed so going back is one `#define` (trap 243).  The board
itself was exonerated on the way: stock TabletOS on this unit runs `ti0`
UP/RUNNING and **associated** on the vendor `Rev 7.1.4.10.77` with
`IPC_EVENT_SCAN_COMPLETE` 6 s after boot (report 234).

### Left to do

**THE ONE EXPERIMENT: associate to a real AP.**  Everything is in place — the
radio scans (17–21 BSS) and the client tooling is in the image.

* **Inputs:** one SSID + passphrase (ask the operator; the bench saw
  `MovistarFibra`, `Peperina`, `MovistarFibra-5GHz-…`), a free IP subnet, and
  the unit on our image with `wlan0` up.
* **Steps (not yet run — this is the experiment):**
  `wpa_passphrase '<SSID>' '<psk>' > /tmp/wpa.conf`;
  `wpa_supplicant -B -i wlan0 -D nl80211 -c /tmp/wpa.conf
  -C /var/run/wpa_supplicant`; watch `wpa_cli -p /var/run/wpa_supplicant -i
  wlan0 status` and `dmesg` unfiltered; then `udhcpc -i wlan0` (BusyBox) and a
  throughput measurement (e.g. `nc`/`wget` against a host on the same AP).
  What *was* run this session: `wpa_supplicant -B -i wlan0 -D nl80211 -C
  /var/run/wpa_supplicant` (initializes), `wpa_cli scan` (`OK`), `wpa_cli
  status`, `wpa_cli scan_results` — all with no `-c` config and no credentials.
* **Results mean:**
  * `wpa_state=COMPLETED` + a DHCP lease ⇒ the WLAN is finished; record the
    real throughput and move to the per-unit NVS / BT.
  * Scan works but association times out / deauths ⇒ RF/TX side: try the
    per-unit NVS (`local/work/wl128x-nvs-perunit.bin`) and watch the driver
    log; that is the next fork.
  * `4-way handshake` failures only ⇒ config/PSK, not the driver.
  * Any `-110`/recovery again ⇒ back to traps 239/243 before touching anything.

1. **The per-unit calibration NVS** — located (the 468-byte NVRAM record at raw
   `0x152cc`, `local/work/wl128x-nvs-perunit.bin`); media ships the generic
   file.  Now that the radio transmits, measure with/without it (TX power).
2. **BT** (untouched): `BT_EN` = gpio 55 = `GPIO2_23`, UART2 `/dev/ttyS1`.  Use
   linux-firmware's **`TIInit_7.2.31.bts`** ("ORCA-Trio BTIP 2.0"), not the
   reference's `WL1283.bts` (a 6.0.15 "Quattro L" script, `TI_P6_15.51`).
   **Mainline 7.3.3 has no `btwilink.c` and no `drivers/misc/ti-st/`** — both
   were removed upstream, so plan on H4 over UART2 plus a userspace `.bts`
   loader, unless the reference turns out to use TI-ST (report 216 argues it
   does not: `devc-seromap-winbt` + `btp-btmgr`).
3. **Open:** *why* the ELP wake is not honoured (report 232 §6).  CAM is a
   workaround that costs battery; a finer instrument (the DRAM ring, not
   printk) is what would separate "wake latency" from "`WL_IRQ` around the
   sleep transition".  Only worth it if the WLAN must run on battery.

Do **not** re-run the session-49/50 negatives: the readable-state diff (218),
the two replays (217), the boot-path RE (221–226), the domain test (225), the BT
probe (228) or the clock sweep (229).  Do **not** use `IRQ_TYPE_LEVEL_LOW` for
WL_IRQ (trap 238), and do **not** use `dump=1` to measure a timing fault (trap
239).

## 0c. WM8994 audio — closed

Headphones and both speakers play `pbplay`'s 440 Hz tone at 48 kHz ±1 %
(operator, re-verified session 48 at DAC1 volume 40).  The path is session 46's
(AIF1 on **McBSP3**; `mcbsp2 + wm8994-aif1` clocks, `mcbsp3 + wm8994-aif2` data;
codec AIF1 slave, FLL1 from our BCLK; `XCR2 = 0x8041`; no ABE), and the speaker
route is session 47's (codec **line outputs**, `spk` = GPIO2_12 **active low**,
GPIO1_15 `audio_boost` **high**).  Only the optional MCLK1 probe (trap 165)
remains.

Listening tests: lower `pbmix set "DAC1 Volume" 40` **once**, run every test,
restore `96` **once at the end**; a bare `pbplay` plays a 2 s tone (trap 212);
state what is playing and that both PCMs are `RUNNING` (traps 191/208).

## 0d. Touch — done, survives a warm reboot; cold boots need the pulse

I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` maps the 1917
`scaling.conf` calibration to 1024×600; GPIO2_3 edge IRQ (`irq 110`).  A **cold**
boot leaves the controller down until GPIO1_14/GPIO4_8 are pulsed — the driver
still does not drive them.

## 0e. The minimal 1917 oracle

Debloated 2.1.0.1917 IFS+RCFS (`tools/build_minimal_qnx.py`), qconn on
`169.254.0.1:9090` + root ssh.  Its `/base` is pruned (no firmware dir); for
WLAN work push `firmware.bin` + the ini to `/var/ti1283`.  No wifi service runs,
so `ifconfig ti0 up` is manual.  Tools are not on the UFS — push
`memdump3`/`memw32`/`pbi2c` to `/tmp` after every boot.  **Bench only** (trap
173).

## 1. Useful local extracts

* `local/work/1917/base/`, `ifs/`, `calib/` — the 1917 `/base`, IFS tree and the
  WLAN calibration (`hd5`).
* `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` — the reference's BT
  firmware (6.0.15 "Quattro L", `TI_P6_15.51`, Jan 2012) for comparison;
  linux-firmware's `TIInit_7.2.31.bts` is the newer one to use.
* `local/work/firmware/ti-connectivity/` — **both** SR firmware files
  (`wl128x-fw-4-sr.bin` is the shipped one, `wl128x-fw-5-sr.bin` the spare that
  panics — §0b) and `wl128x-nvs.bin`.
* `local/work/modules-7.3.3/` — the staged modules (34); source of truth for
  region 1.
* `local/images/rootfs-region1-wl1283-scan.ext4` — the current region-1 image.
* `tools/build_minimal_qnx.py`, `local/work/minimal/` — the oracle.
* Captures: `wl1283-elp-…` (new), `wl1283-wlan-up-…`, `wl1283-enumerate-…`,
  `wlan-oracle-up-…`, `wl1283-{bt-probe,slow-clock}-…`.
* Reports **232** (current), **230/231**, **216–229**; `hardware/twl6030.pdf`,
  `hardware/WM8994.PDF` (gitignored), `hardware/swpu231ap.pdf.txt`.

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

**The CRC is per-build** (the version string carries the build time), so it
identifies an image but cannot prove a source revert — compare files against
`local/pristine/linux-7.3.3` + the patch.

**Build modules IN-TREE, not with `M=`.**  `make -C $K … M=drivers/net/...` marks
the module out-of-tree, prints `(O)` in every later dmesg and taints the kernel;
it also leaves a partial `Module.symvers` that breaks the next in-tree build.
Use the full `make … modules` (it is incremental), then strip with
**`--strip-debug`**, never `strip -s` (a fully stripped module is rejected with
"module has no symbols").  Session 51 confirmed a full in-tree rebuild
reproduces every other module **byte-identically**.

**The wireless modules and firmware live in region 1**, not the kernel image.  A
module/firmware-only change keeps the region-0 CRC (trap 190) and needs a
**region-1-only flash** at block `0x2915`:

```bash
python3 tools/build_rootfs.py --size-mb 64 \
  --busybox local/work/src/busybox-1.36.1/busybox \
  --busybox-links local/work/src/busybox-1.36.1/busybox.links \
  --modules local/work/modules-7.3.3 --firmware local/work/firmware \
  --krel 7.3.3 --out local/images/rootfs-region1-wl1283-elpcam.ext4
```

`build_rootfs.py --out` **overwrites** (trap 213) — build to a new
purpose-named path and retire the old file deliberately.  Check the module
inside the built image with `debugfs -R "dump /lib/modules/7.3.3/kernel/X.ko …"`.

**Patch export** (traps 161/211): drive the file list from the tree walk, apply
it to a staged pristine copy and compare every file.  Current export: **59 files
/ 8 713 lines**, sha256 `2be9e99c…`, 0 rejects / 59/59.  The walk recipe must
filter `Kconfig.*` (not just `Kconfig`), or it drops `arch/arm/Kconfig.debug`.

## 3. Bench

* **`tools/pbssh.sh 'cmd'`** (root, key-only, `169.254.0.1`, 2.3 MB/s);
  `PB_LINK=usb|door`; the host profile must bind the net interface of whichever
  OS is booted (trap 214).  Dropbear has **no `scp`** — move files with
  `ssh … 'cat > /path' < file` (a `scp -O` still needs a remote `scp`).
* **Reboot:** `tools/pbssh.sh 'reboot -f'`; if it hangs (trap 224) use
  `echo b > /proc/sysrq-trigger`.  Arm the flasher first.
* **Never `slay` by name on the oracle** (trap 221); kill by pid.
* Device password `0000`, once.  An agent never writes RPMB; NVRAM read-only.
  `/dev/rpmb/` exists only on TabletOS; our `pbrpmb` tops up raw at boot.
* **WL1283 checks:**
  * `i2ctransfer -f -y 0 w1@0x48 0xbc r3` → `07 01 55` (CLK32KG ON).
  * `ls /sys/bus/sdio/devices/` → `mmc4:0001:1` and `:2`.
  * `ip link set wlan0 up` → `wlcore: firmware booted`, `wlan0` UP, no carrier.
  * **Stability check:** `dmesg | grep -cE "sdio read failed|wlcore: down"` stays
    **0**; the platform device may sit `suspended` (that is fine — CAM is about
    the chip, not the host).
  * `mount -t debugfs debugfs /sys/kernel/debug` is needed for
    `…/ieee80211/phyN/wlcore/sleep_auth` (rcS does not mount debugfs).
  * **To reproduce the `-110`**: `echo 2 > …/wlcore/sleep_auth` (ELP) on a
    working system; restore by `echo 0` or a module reload.
  * **`iw` and `wpa_supplicant` are in the image** (report 233 §5):
    `/usr/bin/iw`, `/usr/sbin/wpa_{supplicant,cli,passphrase}`, and the signed
    `/lib/firmware/regulatory.db`(+`.p7s`) — `iw reg get` shows the world
    domain with 2.4 GHz active.  **`iw dev wlan0 scan` works** (17–21 BSS on
    firmware 7.3.5.0.98, report 235); `wpa_supplicant -B -i wlan0 -D nl80211
    -c /tmp/wpa.conf -C /var/run/wpa_supplicant` is the association path
    (`rfkill: Cannot open RFKILL control device` is expected).  To change one of
    these tools: put it under
    `local/work/rootfs-extras/<path>`, rebuild with `--extras
    local/work/rootfs-extras`, and flash region 1.
* WM8994: `i2ctransfer` args are DECIMAL (trap 166); bus 4, addr `0x1a`; the
  regmap debugfs is a cache (trap 207) and debugfs is not mounted by `rcS`.
* QNX device tools build `-static`; `/tmp` is wiped by a reboot.

## 4. Traps added last session (full list 14–243 in `KNOWN-ISSUES.md` §2)

* **239.** The WL1283's `-110` is the chip's **ELP**; keep it in **CAM**
  (`sta_sleep_auth` in `wl12xx_default_conf`).  The failing transfer is the
  FW-status read (CMD53, `0x1efc0`, 84 B, byte mode, 256-byte block) from the
  IRQ thread.  `wlcore_sdio dump=1` **hides** it — instrument the error path
  instead.
* **240.** A `wlcore` scan that logs `Scan completed due to error.` after ~30 s
  is the driver's own `WL1271_SCAN_TIMEOUT`, not a hang: `wlcore_scan()` assumes
  failure and only the firmware's scan-complete event clears it.  Count the
  `wl12xx` IRQs in the window (~13 = the firmware never reported the scan).
* **241.** `wlcore fwlog=continuous` **oopses** on wl12xx: the panic-log reader
  parses the block with the wl18xx layout, the FW address goes out of range and
  `wl12xx_copy_fwlog` memcpys unmapped memory (`Oops: 5`; then `ip link show`
  blocks until a reboot).  It does prove the firmware panics.
* **242.** On a stock TabletOS boot the door's `sshd` may be down while `qconn`
  9090 answers — use `tools/QCL.py` (`exec /bin/<cmd>`; `sh -c '…'` does not
  survive its argument handling), kill a stale `door-hold` by pid, and rebind
  `pb-rndis` to the plain `enp8s0f3u1`.
* **243.** The WL1283 must run **4-sr** (`7.3.5.0.98`): **5-sr** (`7.3.10.0.142`)
  boots but **panics on the first scan**.  The floor is 7.3.5.0 and
  `WL128X_FW_NAME_SINGLE` names the 4-sr file; both stay installed.
* Still current: **238** (`LEVEL_LOW` WL_IRQ storms), **237** (the three wlcore
  board properties), **236** (`CLK32KG` at `0xBC`), **235** (`f_min`/ladder),
  **234** (BT silence is not a liveness oracle), **231** (`WL_IRQ` is not
  liveness), **227** (the measurement flaw), **224** (`reboot -f` wedge), **221**
  (`slay` kills `rndis0`), **214** (composite net iface), **212** (bare `pbplay`
  plays), **211** (tree walk + `Kconfig.*`), **200** (native doc edits), **190**
  (module-only CRC), **165** (chip-side pins unmeasurable).

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| wireless rootfs builder | `tools/build_rootfs.py` (`--modules`, `--firmware`) |
| reference WLAN load path | `local/work/1917/base/scripts/wifi.sh` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| TWL6030 datasheet / TRM | `hardware/twl6030.pdf`, `hardware/swpu231ap.pdf.txt` |
| captures | `docs/captures/wl1283-elp-2026-11-14.txt` (new) and the session-50 set |
| audio machine driver | `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` |
| OSS player / mixer / BT probe | `rootfs/pbplay.c`, `rootfs/pbmix.c`, `rootfs/hci.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| reports | `docs/reports/` (**232** current for the WLAN) |
| sessions | `docs/sessions/session-51.md` |

## 6. Working rules

* After every console command, dump the log tail and read it.  Read
  `dmesg`/`sloginfo` **unfiltered** after a reboot or structural change.
* **Don't clear `dmesg` under the operator** without saying so — experiments
  here start with `dmesg -c`, and the console then looks like it "lost" the
  message they just saw.  Say what you are about to clear, or capture first.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  **A test boot is not a budget concern.**
* **Never present a guess as a fact.**  Prefer a fresh read.
* **Use the reference before the bench.**  Subagents: static/file-only (trap 109).
* **Revert experiments and prove the revert** (DTS vs `kernel/source`, driver vs
  pristine + patch).
* **Only the operator decides when to stop / wrap up.**

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 52 (absolute; Linux-era 46)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 9, KNOWN-ISSUES.md §2 (traps 14-243, especially 232-243)
and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md (63-69),
docs/reports/INDEX.md, reports 232-235, and sessions/session-51.md.

WHERE IT STANDS: audio (headphones + both speakers, 48 kHz), the touch, the
sensors/charger, ssh over USB and the display are all working.  **THE WL1283
WLAN WORKS — it scans (session 51).**  Two blockers fell: the -110 recovery loop
was the chip's ELP sleep (the driver now sets sta_sleep_auth = WL1271_PSM_CAM),
and the never-completing scan was the 5-sr firmware — wl128x-fw-5-sr.bin
(Rev 7.3.10.0.142) boots but panics on the first scan, while
wl128x-fw-4-sr.bin (Rev 7.3.5.0.98) scans normally (17-21 BSS per scan, both
bands, no errors).  The port lowers wl12xx's SR floor to 7.3.5.0 and points
WL128X_FW_NAME_SINGLE at 4-sr; both files stay installed.  The board itself was
exonerated: stock TabletOS on this unit runs ti0 UP/RUNNING, associated, on
vendor firmware Rev 7.1.4.10.77 with IPC_EVENT_SCAN_COMPLETE 6 s after boot.
There is NO newer wl128x firmware anywhere (linux-firmware and TI's own tree
carry the same ten files; 5-sr is the last).

WHAT IS LEFT (WL1283, in order):
(a) ASSOCIATION — the client side is shipped (iw 6.17, wpa_supplicant 2.12,
    wpa_cli, wpa_passphrase, signed regulatory.db; the world regdomain loads
    with 2.4 GHz active).  wpa_passphrase SSID pass > /tmp/wpa.conf, then
    wpa_supplicant -B -i wlan0 -D nl80211 -c /tmp/wpa.conf
    -C /var/run/wpa_supplicant, then wpa_cli -p ... -i wlan0 status.  This is
    also where real throughput gets measured (HT20/MCS0-7, 72 Mbps PHY; the
    old 4-5 MB/s was the reference's USB RNDIS path, not a Wi-Fi limit);
(b) the per-unit CALIBRATION NVS — located: the 468-byte NVRAM record at raw
    0x152cc (local/work/wl128x-nvs-perunit.bin); media ships the generic file;
    now that the radio transmits, measure with/without it;
(c) BT: BT_EN = gpio 55 = GPIO2_23, UART2 /dev/ttyS1.  Use linux-firmware's
    TIInit_7.2.31.bts (ORCA-Trio BTIP 2.0), NOT the reference's WL1283.bts (a
    6.0.15 "Quattro L" script).  Mainline 7.3.3 has no btiwlink/ti-st, so plan
    on H4 over UART2 + a userspace .bts loader unless the reference turns out
    to be TI-ST based;
(d) open, neither blocking: why 5-sr panics on scan (report 235 §3 — fix the
    fwlog parse first, trap 241) and why the ELP wake is not honoured
    (report 232 §6).
Do NOT re-run the session-49/50 negatives (readable diff 218, replays 217,
boot-path RE 221-226, domain test 225, BT probe 228, clock sweep 229).  Do NOT
use IRQ_TYPE_LEVEL_LOW for WL_IRQ (trap 238), do NOT use wlcore_sdio dump=1
to measure a timing fault (trap 239), and do NOT switch back to 5-sr without
expecting the scan panic (trap 243).

PHYSICAL STATE: REGION 0 IS UNCHANGED — our Linux image 0x9712A086 (0x939A74,
kernel 7.3.3 #60).  REGION 1 is now f159a221... (67,108,864 B, 34 modules incl.
wlcore/wl12xx, BOTH wl128x SR firmware files, dropbear, the CAM+4-sr wl12xx.ko,
iw 6.17, wpa_supplicant/wpa_cli/wpa_passphrase 2.12 and the signed
regulatory.db+.p7s) — local/images/rootfs-region1-wl1283-scan.ext4.  Kernel
patch 59 files / 8 713 lines, sha256 2be9e99c...  The unit is on LINUX
(0fca:8010, console
`pb> `, ssh on 169.254.0.1); rcS loads the wireless stack but does not raise the
link — `ip link set wlan0 up`.  To boot the ORACLE: flash
local/work/ifs-shim-sealed.ifs at 0x825 (the RCFS slot holds the FULL 1917 RCFS,
trap 227; the pruned oracle pair is local/work/minimal/setB.signed at 0x8D5).
/tmp is wiped by a reboot.  To stock TabletOS: ifs-shim-sealed.ifs at 0x825 +
rcfs-1917.rcfs at 0x8D5 (NOT wayback.signed, trap 226).  Do NOT restore
local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

BENCH: `tools/pbssh.sh 'cmd'` is the DEFAULT ACCESS.  The HOST's pb-rndis
profile must bind the net interface of the booted OS — enp8s0f3u1i2 for our
composite gadget, enp8s0f3u1 for the oracle's plain RNDIS (trap 214).
Dropbear has no scp: move files with `ssh ... 'cat > /path' < file`.  REBOOT:
if `reboot -f` hangs (trap 224) use `echo b > /proc/sysrq-trigger`.  NEVER
`slay` by name on the oracle (trap 221).  Dropbear is key-only, root, on
169.254.0.1; key local/secrets/rsa.  Device password 0000, once.  An agent
NEVER writes RPMB; NVRAM read-only.  Read dmesg/sloginfo UNFILTERED.  Do not
clear dmesg under the operator without saying so.
WL1283 checks: `i2ctransfer -f -y 0 w1@0x48 0xbc r3` -> 07 01 55 (CLK32KG ON);
`ls /sys/bus/sdio/devices/` -> mmc4:0001:1/:2; `ip link set wlan0 up` ->
`wlcore: firmware booted`; stability = zero `sdio read failed`/`wlcore: down`
lines.  To reproduce the -110: `echo 2 > .../wlcore/sleep_auth` (ELP) on a
working system; `echo 0` restores.  WM8994: i2ctransfer args DECIMAL, bus 4
addr 0x1a; debugfs is not mounted by rcS.  On the oracle use pbi2c rd16/wr16;
push memdump3/memw32/pbi2c to /tmp after every boot.

LISTENING TESTS ARE QUIET AND SHORT: lower `pbmix set "DAC1 Volume" 40` ONCE, run
every test, restore 96 ONCE at the end; a bare `pbplay` plays a 2 s tone (trap
212); state what is playing (trap 191); start the clock link first (trap 208).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4; for a QNX ELF reconcile
with ntoarmv7-readelf/objdump.  QNX device tools build -static.  USE THE
REFERENCE FIRST, and put static RE in subagents (file-only, trap 109).  The TI
TRM is local (hardware/swpu231ap.pdf.txt); the TWL6030 datasheet is
hardware/twl6030.pdf; the WM8994 one hardware/WM8994.PDF (gitignored).

BUILD: modules IN-TREE (`make ... modules`), never `M=` (it marks them (O) and
taints the kernel); strip with `--strip-debug`, never `strip -s`.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed; a
module/firmware-only change keeps the CRC and needs a region-1-only flash (trap
190).  The UFS top-up keeps BOOTCTR_FCTRY at 0x32; the hard hazards (0 / >0x32 /
failed write / flat pack) remain.  Operator's call when to stop and to wrap up.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note — and move replaced or
resolved material to docs/history/ or docs/parked/ with a link stub.  Re-export
the kernel patch whenever the DTS or a driver changes (trap 161) and verify it
by apply-and-compare, and run the TREE WALK too (trap 211, filtering Kconfig.*).
A reverted experiment still has to be proven reverted: DTS against
kernel/source, the driver against pristine + patch.

WRAP-UP: only the operator decides when the session stops.  Record the bench
state, write the session note, update the canonical docs, verify with
tools/docs_audit.py and tools/tests/test_offline.py, then commit privately and
publish the snapshot (docs/project/REPO-MANAGEMENT.md).  No IFS restore is
required.
```
