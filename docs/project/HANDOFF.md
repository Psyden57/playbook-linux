# Handoff — session 67 (absolute) → session 68

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 68 = Linux-era 62**, written at the
session-67 close.  See `docs/sessions/NUMBERING.md`.

> **Session-67 result, in two lines.**  **The mailbox fault is fixed** (a
> TWL6030 comparator freed by a failed probe — report 265), and **the second
> core was doing no userspace work at all**: PID 1 was affinity-masked to `{0}`,
> so `nproc` read 1 with `online=0-1`; an inittab `taskset -p 3 1` fixes it
> (report 266).  Image **`0x3547D613`** (`#109 SMP`, `panic=0`), 3/3 clean
> dual-core boots; `cpufreq-dt` is bound and scales (no CPU supply attached);
> region 1 `6f5b8ca7…`; hd3 `07555bde…` (62.5 G Nura, now with the inittab
> line); patch **67 files / 9 136 lines**, sha256 `d19adff8…`;
> `BOOTCTR_FCTRY = 0x24`.  **Next: re-measure throughput/UI on two real cores,
> attach the CPU supply, and replace the inittab line with a kernel fix.**

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**, with
a GPU-accelerated compositor.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console **and its RNDIS link with ssh**, the whole display chain plus the
DSI-init TCON text on the panel, the **Nura root on hd3** (read-write, grown to
62.5 G), **dual-core at 1008 MHz on the ARMv7 kernel**, **SGX540** (`pvrsrvkm`
bound; `catacomb` compositing), the peripherals (I²C4 motion sensors, TMP103,
BQ27541, FAN5702, CM3217, ISL9519 charger), the **Munin touch**, the **WM8994
audio** (headphones + both speakers, 48 kHz), and the **WL1283 WLAN**
(associates, DHCP, traffic).  The WL1283 **BT core** works (real inquiry; `hci0`
registers with mgmt) with the controller-open parked (§0c).

**What is left, in order:** re-measure the throughput/UI numbers on two *real*
cores (§0e — they were taken single-core), attach the CPU supply and settle the
1.35 V clamp (§0e), replace the inittab affinity line with a kernel fix (§0e),
then UI performance / hardware GL and the rebake (§0f).  The **mailbox fault is
fixed** (report 265).  The **USB/VUSB teardown** is intermittent and open (§0d).

## 0a. The media, and the way back

* **region 0 = `0x3547D613`** — `boot/chainloader/kernel.img` (10,448,868 B,
  sha256 `6ecd068d…`): kernel `7.3.3 #109 SMP`, `-march=armv7-a`, the fast
  chainloader, the loud DTB **without `maxcpus`**, the DSI-init TCON hook, the
  **stock MUSB mailbox + comparator lifetime fix**, an inert
  `console=ttyGS0,115200`, and **`panic=0`**.  The previous
  `0xC9FEE2D1` (`#105`, device-mode-only workaround, `panic=10`) is the
  known-good fallback; the session-64 recovery image is
  `local/work/kernel-usbdev-cpu1.img` (`0xC675E206`).
* **region 1 = `6f5b8ca7…`** (`49e318bf…` was the older copy), 64 MiB ext4 at
  `0x29000000` — the BusyBox fallback root, **not booted**; its 43 modules
  mismatch the current kernel (trap 320).
* **hd3 = the Nura ext4**, from `local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img`
  (sha256 `07555bde…`, 625,999,872 B), written at block `0x4915`, grown to
  **62.5 G** (15,273,984 × 4 KiB), with the ARMv7 module set installed and
  `depmod` run.  The same image is the flasher-restorable base.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`, then reboot.  It enumerates `0fca:8012`; qconn
  `169.254.0.1:9090`, root ssh — use **`PB_LINK=door tools/pbssh.sh`**.  The
  region-0 RCFS slot holds the **full 1917 RCFS** (trap 227); for the *minimal*
  oracle flash `local/work/minimal/setB.signed`.  `/tmp` is wiped by a reboot.
* **To stock TabletOS:** the 1917 IFS at `0x825` **and** `local/work/rcfs-1917.rcfs`
  at `0x8D5`, plus the vendor UFS (`local/work/ufs-1917-devuser.ufs`,
  sha256 `482e0473…`, 707 MB) at `0x4915` if hd3 has been repurposed.  **Do not
  restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).
* **hd3 no longer holds the QNX userland.**  The operator has the UFS image, so
  **no backup is required**; a reflash restores TabletOS.  **No restore is
  required at wrap-up.**

## 0b. The WLAN — it associates (session 52, unchanged)

The blocker was the missing `ccm`/`ctr` crypto modules
(`ieee80211_key_alloc()` needs a `ccm(aes)` AEAD; without it the PTK install is
`-ENOENT`, which wpa_supplicant reports as `WRONG_KEY`).  Working sequence after
a boot (`/tmp` is wiped):

```sh
ip link set wlan0 up                 # rcS loads the stack, it does not raise it
iw reg set <CC>                      # e.g. AR — required for a 5 GHz AP
mkdir -p /var/run/wpa_supplicant
{ echo "disable_scan_offload=1"; wpa_passphrase '<SSID>' '<PSK>'; } > /tmp/wpa.conf
iw dev wlan0 scan >/dev/null 2>&1
wpa_supplicant -B -i wlan0 -D nl80211 -c /tmp/wpa.conf -C /var/run/wpa_supplicant
wpa_cli -p /var/run/wpa_supplicant -i wlan0 status      # want wpa_state=COMPLETED
```

Wi-Fi is 1×1 HT20 (72 Mbit/s) — **do not advertise HT40** (trap 247).  Do **not**
use `IRQ_TYPE_LEVEL_LOW` for WL_IRQ (trap 238), **not** `wlcore_sdio dump=1`
(trap 239), **not** 5-sr (trap 243).

## 0c. BT — what works, and where it stopped (parked)

**Proven (report 238):** over a plain H4 tty (`/dev/ttyS1`, 115200 8N1) the core
answers `HCI_Reset`, reports `hci ver 6` / `lmp ver 6` / `mfr 0x002b` (TI) /
BD_ADDR `8e:e4:3c:22:61:00`, and **runs a full BR/EDR inquiry that finds a peer**
(`f0:a3:b2:bb:d1:00`, class `0x011c00`), reproducibly.  Replaying
`local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` answers **315/315** and
moves LMP subversion `0x2b0f` → `0x2b33`.  Loaded **before** the attach it makes
`hci0` **register with mgmt** (0 → 1 controller; negative control both ways) by
fixing the `0x201c` reply length.

**Parked blocker (trap 250):** a stray `0x30` byte.  After the last init reply
completes, ~200 ms later a single `0x30` arrives (6 of 6 runs, instrumented
`h4_recv()`); it is not an H4 type, so the kernel correctly returns `-EILSEQ` and
`SET_POWERED` gives `0x05`.  **Which side emits it is unknown.**  The experiment
to resume is in STATE.md §"What is next" 1a(b) — and note trap 251 (verify a
module swap by content) before running it.

**LE** is refused by the controller, but the chip reports BT 4.0 with the
LE-supported bit, so no enable sequence is known and impossibility is **not**
demonstrated.  **FM:** mainline carried `drivers/media/radio/wl128x` from 2.6.39
to 6.13 and removed it; a forward port is the route.  **GPS:** no mainline
driver; the operator has a 2024 LKML patch to evaluate, the reference's own path
is the fallback.

## 0d. CLOSED — the MUSB mailbox fault (reports 264/265)

**The fault was a freed TWL6030 `phy_companion`, not corruption of
`musb->xceiv`.**  `twl6030_usb_probe()` stores `&twl->comparator` in the
`omap-usb2` PHY *before* `twl6030_usb_ldo_init()`; the ldo init's
`regulator_get(dev, "usb")` (the board's `vusb`) can return `-EPROBE_DEFER`,
but the code returned `-ENODEV` for **every** error.  The probe therefore failed
permanently, devres freed the `devm_kzalloc`'d `twl`, and the PHY kept a
dangling `phy->comparator`; a later reuse put a heap pointer in its first word
(`set_vbus`), and the stock `omap_musb_set_mailbox()` jumped through it.  The
device-mode-only workaround had only hidden the dereference.  [report 264 for
the Oops, **report 265 for the cause and fix**]

The fix, now in the patch: propagate `PTR_ERR()` from the regulator lookup;
clear the comparator with a `devm_add_action_or_reset()` action on any probe
failure/remove; `usb_put_phy()` the reference `omap_usb2_set_comparator()`
leaked.  The winchester early return is removed (the `set_mailbox_lock` mutex
stays), and the stock call runs.  **Validated: image `0x3547D613` (`#109 SMP`),
3/3 clean dual-core boots, gadget bound at ~1.41 s, no Oops.**  OTG host mode
is reachable again (untested).

### Still open: the VUSB teardown (second item)

The only writer of `VUSB: disabling` is `twl6030_usb_irq()` in
`drivers/usb/phy/phy-twl6030-usb.c`, on a `CONTROLLER_STAT1.VBUS_DET` read of 0.
Measured timings: BusyBox root, a command every 15 s → link dead at ~30 s; Nura,
SSH traffic → ~90 s; some boots survive >6 min, and session 65/66 saw a 376 s
boot then a ~60 s drop.  So it is intermittent, not an idle timer (trap 337;
`docs/contradictions/usb-vusb-drop-idle-vs-intermittent.md`).  Run
`local/work/kmsgcap.py` (or a userspace `cat /dev/kmsg`) before the link dies
and let it run through a drop; a `VBUS_DET`/`linkstat` log in that handler would
settle it directly.  On the Nura root the RNDIS/usb0 path did not answer this
session — the ACM (`playbook:~#`, `tools/pbraw.py`) was the console.

## 0e. THE ACTIVE ITEM — the CPU: both cores must take userspace work

**The defect (report 266, trap 340).**  With `online = 0-1` and
`SMP: Brought up 1 node, 2 CPUs`, **PID 1 was affinity-masked to `{0}`** —
`taskset -p 1` → `1`, `nproc` → `1` — so every userspace process inherited
`{0}`; only kernel threads had `0-1`.  `taskset -c 1 <work>` always worked, so
CPU 1 was healthy and simply never scheduled userspace.  3/3 fresh boots.  This
is why report 263 §4b's "two cores in parallel" showed no scaling and why the
UI latency numbers are single-core numbers.

**Workaround, in place and verified** (2 boots each way): on **hd3's root**,
`/etc/inittab` has, before the OpenRC entries,

```
::sysinit:/usr/sbin/taskset -p 3 1
```

→ PID 1 `{0,1}`, `nproc` 2, a respawned shell `0-1`, a two-thread busy loop
splitting `cpu0` 2643 / `cpu1` 2622 user ticks.  Backups on the unit:
`/etc/inittab.session67-orig`, `/etc/inittab.fixed`.  It is a root change, so
the region-0 CRC is unchanged — but it must go into any hd3 rebake.

**Owed — the kernel fix.**  `sched_init_smp()` widens init with
`set_cpus_allowed_ptr(current, housekeeping_cpumask(HK_TYPE_DOMAIN))`, which is
`cpu_possible_mask` (0-1) with no `isolcpus=`; `kernel/sched/core.c` and
`isolation.c` are byte-identical to pristine, and there is no `isolcpus=`/
`nohz_full=` in `/proc/cmdline`.  The suspicion is that `cpu_active_mask` has
not yet gained CPU 1 when the call runs (the `CPUHP_AP_ACTIVE` /
`sched_cpu_activate` state during secondary bring-up), so
`cpumask_any_and_distribute(cpu_valid_mask, new_mask)` clamps to CPU 0.  **One
instrumented boot** printing `cpu_active_mask`/`cpu_online_mask` at the top of
`sched_init_smp()` and PID 1's `cpus_mask` immediately after the call settles
it; the `PBCPU`/`PBDBG` `printascii` markers are already in
`local/kernel-tree/init/main.c` (and are deliberately not in the patch).

**cpufreq — corrected.**  `cpufreq-dt` **is** bound and scales:
`scaling_driver = cpufreq-dt`, OPPs `300000 600000 800000 1008000`, governor
`ondemand`; setting `scaling_max_freq = 300000` drops the clock to 300000 and
restoring it returns 1008000.  The old "no `cpufreq` policy exists" note was
stale.  **What is missing is the CPU supply**: no `supply-*` entries under
`/sys/kernel/debug/opp/cpu0/`, so the OPP voltages are never applied.  The
`twl6030_uv_to_vsel: OUT OF RANGE` lines are **startup VC init**, not a runtime
cpufreq fault: `omap4_mpu_vc_data.on = 1375000` and
`OMAP4_VP_MPU_VLIMITTO_VDDMAX = 1410000` exceed `twl6030_vsel_to_uv(0x39)` =
1316660, and the function clamps to VSEL `0x3A` = 1.35 V — exactly the value the
chainloader commands (`PRM_VC_VAL_BYPASS = 0x003A5512`, `vsel 0x3A`, `reg 0x55`,
slave `0x12`).  So 1008 MHz @ 1.35 V is consistent and intended; decide
"1.35 V for all OPPs" (silence/skip the out-of-range entry) vs a real supply
binding — this TWL6030 revision cannot express >1.317 V except the hardcoded
1.35 V.  **1.2 GHz crashes the unit** (report 257).  SmartReflex MPU is present
but idle (`autocomp = 0`; Class3 init only) — that is the next lever (report 219
§5, report 257 §6).

**Then re-measure** the memory throughput (report 263 §4b) and the UI latency
now that CPU 1 takes userspace work.

## 0f. SGX540, the desktop, and what is owed

**Working:** `pvrsrvkm_omap4_sgx540_120` binds (`pvrsrvkm 56000000.gpu: Enabling
quirks`), `catacomb` runs, `card0-LVDS-1` is connected and the backlight is 255
(report 246; session-65 log).  **The baked image on hd3 is the session-59
state** and predates `epitaph` r10005, the llvmpipe `swrast_dri.so` replacement
and the udev/polkit/gorm rules (report 251/253); those fixes lived only in the
session-59 live root, which the session-63/65 reflashes overwrote.  **A rebake
is owed** before the desktop can be judged.

**Memory throughput (measured 2026-11-18):** the EMIF is correctly configured —
both instances identical, 400 MHz LPDDR2-800 (`REF 0x618`), 32-bit, CL 6,
power-down disabled (`PWR=0`), L3 at 200 MHz — and the CPU is at 1008 MHz, but a
userspace benchmark reads only **0.4–0.6 GB/s** (writes ~2 GB/s; no scaling with
8 streams or 2 cores), 3–8× below the 3.2 GB/s the DDR implies.  It is an open
performance item (report 263 §4b): candidates are the 200 MHz L3/EMIF port, the
conservative `tRP/tRCD = 20/17.5 ns` timings, or the measurement; a retune is a
boot-time EMIF write, not a Linux setting.  A QNX comparison would settle it.

**UI performance:** llvmpipe fixed the 2 s client draw (render 2.7–6 ms); what
remains is its deferred `swap` (420–550 ms per full-screen drawer frame), the
first-draw LLVM JIT, and now two cores at 1008 MHz instead of one at 400.  A
hardware GL path for Wayland clients is still untried (trap 314).  **Kumo** needs
a WPEWebKit/WebKit-fork build (not packaged, report 247 §4).

## 0g. WM8994 audio, touch, sensors — closed

Audio: headphones and both speakers play `pbplay`'s 440 Hz tone at 48 kHz.
Listening tests: lower `pbmix set "DAC1 Volume" 40` **once**, restore `96` at the
end; a bare `pbplay` plays a 2 s tone (trap 212).  Touch: I²C2 `0x48072000` =
`i2c-1` at `0x65`, GPIO2_3 edge IRQ (`irq 110`); a **cold** boot leaves it down
until GPIO1_14/GPIO4_8 are pulsed, which the driver does not drive.  The sensors,
charger, FAN5702 and CM3217 work; the ISL9519 is the board's charger (the TWL6030
BCI is inert).

## 0h. The minimal 1917 oracle

Debloated 2.1.0.1917 IFS+RCFS (`tools/build_minimal_qnx.py`), qconn on
`169.254.0.1:9090` + root ssh.  Its `/base` is pruned (no firmware dir).  No
wifi/BT service runs, so `ifconfig ti0 up` and the BT start are manual.  Tools
are not on the UFS — push `memdump3`/`memw32`/`pbi2c` to `/tmp` after every boot.
**Bench only** (trap 173).  The full 1917 RCFS is in the region-0 slot; the
minimal pair is `local/work/minimal/setB.signed`.

## 1. Useful local extracts

* `local/kernel-tree/` — the 7.3.3 build tree (gitignored).  It carries the
  session-62/63/64 **diagnostics** that are deliberately not in the patch:
  `PBDBG` markers (`init/main.c`), `PBCPU`/`PBIPI1` (`arch/arm/kernel/smp.c`,
  `arch/arm/mach-omap2/omap-smp.c`), `PBSMF` (`kernel/smp.c`), the `head.S`
  `CRR` stamp, and the `MINI4x6` font link (`lib/fonts/Makefile`).
* `local/pristine/linux-7.3.3` — the pristine tree for exports and diffs
  (`local/pristine/linux-7.3.3.tar.xz`).
* `boot/chainloader/kernel.img` (`0x3547D613`) — the current region-0 image
  (stock mailbox + comparator fix, `panic=0`); the previous
  `local/work/kernel-armv7-console-cpu1.img` (`0xC9FEE2D1`),
  `kernel-armv7-cpu1.img` (`0x158D8A85`) and the recovery
  `kernel-usbdev-cpu1.img` (`0xC675E206`) are beside it.
* `local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img`
  (`07555bde…`, 625,999,872 B) — the hd3 root and its flasher-restorable base.
* `local/work/modstage/lib/modules/7.3.3` and
  `local/work/modules-7.3.3-armv7.tar.gz` — the ARMv7 module set (29 MB
  installed / 8.4 MB tarball).
* `local/work/soak65.py` — the reboot+soak driver (console shell `playbook:~#`).
* `local/work/kmsgcap.py` — the ACM capture (retries `cat /dev/kmsg`; also
  `--raw`).
* `tools/pb_ring.py`, `tools/bench/ringdump.c` — the DRAM ring decode and its
  device-side reader.
* `tools/bbusb.py`, `local/work/flash825.sh` — the flasher and the region-0
  wrapper; hd3 takes `--component … --dest-block 0x4915`.
* `tools/build_kernel_image.py`, `tools/env.sh` — wrap and toolchains.
* Reports **263** (current for the CPU/USB), 262 (MUSB), 259–261 (reference
  core-up, `cpu_startnext`, panel/TCON), 257 (1008 MHz/OPP), 256/UUID; captures
  `docs/captures/playbook-live-2026-11-18-nura-{armv7-boot,kmsg-stream}.txt`.

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

**Build modules IN-TREE, not with `M=`.**  Strip with `--strip-debug`, never
`strip -s`.  A module/firmware-only change keeps the region-0 CRC (trap 190);
with the root on hd3, `modules_install` into a staging dir, tar it, and install
it over ssh (`/lib/modules/7.3.3` + `depmod`).  `.config` changes are expensive:
they change the vermagic (`SMP`, `ARMv7`) and require rebuilding **every**
module.  The tracked `kernel/winchester-7.3.3.config` now matches the bench
(`CONFIG_SMP=y`, `CONFIG_NR_CPUS=2`, no `ARCH_MULTI_V6`).

**Flash region 0** at block `0x825`, hd3 at `0x4915`
(`python3 tools/bbusb.py flash --component <img> --dest-block 0x4915
--from-bootrom --password 0000 --apply`; the 626 MB write takes ~20 min).
Start the flasher first, then reset/power on; the power-on bootrom is caught
reliably (trap 323: it can hold ~20 s).  Region-1-only flashes are at `0x2915`.

**Patch export (traps 161/211).**  Current: **67 files / 9 136 lines**, sha256
`d19adff8…`, verified **0 rejects / 67/67** against pristine 7.3.3 (session 67).
Method used (reliable): start a scratch tree from pristine + the *previous*
patch (it applies 0 rejects), overwrite the files whose functional delta you
intend (here `omap2430.c`, the DTS, and the two PHY drivers), regenerate with
`diff -u -N --label a/… --label b/…` over the file list, then apply the new
patch to a fresh pristine scratch and `cmp` every file byte-for-byte.  Also run
the tree-vs-pristine walk (trap 211, filtering `Kconfig.*`, the vendored
`pvrsgx/`, and build artifacts) and confirm every remaining difference is a
known diagnostic.  Update `kernel/patches/README.md` and `KERNEL-PORT.md`.
Local helper/workspace this session: `local/work/export67/`.

## 3. Bench

* **SSH on Nura:** `PB_LINK=usb tools/pbssh.sh 'cmd'` (root, key-only,
  `169.254.0.1:22`).  The ACM shell is `playbook:~#`, **not** `pb> ` — so
  `tools/pbcon.py` does **not** drive it (trap 274); use `tools/pbraw.py` or a
  prompt-aware helper.  dropbear came up without an entropy seed (crng init at
  13.1 s); if it blocks, trap 328 applies.
* **Kernel printk goes to the panel** (`tty0`/fbcon).  The DTB's
  `console=ttyGS0,115200` is **inert** — the gadget serial driver has no
  kernel-console write path (verified: no `console [ttyGS0] enabled`).  Read the
  panel, or `cat /dev/kmsg` from the shell.
* **Reboot:** on the Nura root use **`reboot -f`** (the normal `reboot` is very
  slow there).  A `reboot -f` wedge is trap 224; use
  `echo b > /proc/sysrq-trigger`.  The root is ext4 with `barrier=0`, so a
  clean reboot still beats a power cut.
* **`BOOTCTR_FCTRY` is readable on our image:**
  `tools/pbssh.sh 'dmesg | grep pbrpmb'` (or the ACM) → `… record@196 = <4
  bytes LE>` then `0x<v> >= threshold 0x1e, no write`.  It read **`0x24`** at
  the session-67 close.  A logged read, not an RPMB access (`SAFETY.md` §2).
* **Check `nproc` after every boot**, not just `online`: on the SMP image they
  disagreed until the inittab widening (trap 340).  `taskset -p 1` should read
  `3`; if it reads `1`, the affinity line is missing.
* **The eMMC WRITE-busy stall is live:** `mmc1: Card stuck being busy` plus a
  lost async WRITE during write bursts (27 s/33 s in the Nura boots).  It has
  already damaged the rw ext4 once (trap 336); prefer clean reboots.
* **`/tmp` is tmpfs** — re-push `ringdump` (paced 8 KiB/5 ms, trap 302).
* **Never `slay` by name on the oracle** (trap 221); kill by pid.  Device
  password `0000`, once.  An agent never writes RPMB; NVRAM read-only.
* **WLAN checks:** `lsmod` must show **`ccm` and `ctr`**;
  `ip link set wlan0 up` → `wlcore: firmware booted (Rev 7.3.5.0.98)`.
* **WM8994:** `i2ctransfer` args are DECIMAL (trap 166); bus 4, addr `0x1a`.
* **Old Nura image:** the panel's `epitaph` may still show the r10005 bug;
  check before judging the desktop (§0f).

## 4. Traps added last session (full list 14–340 in `KNOWN-ISSUES.md` §2)

* **334.** The kernel was built **`-march=armv6k`** because
  `CONFIG_ARCH_MULTI_V6=y` selects `CPU_ARM1176 → CPU_V6K → CPU_32v6`, and
  `arch/arm/Makefile` lets the v6K setting override v7 while `CPU_32v6` is set.
  ARMv6 code runs on Cortex-A9 (v6K is a subset), so it was a performance defect,
  not a fault.  Fix: `scripts/config --disable ARCH_MULTI_V6` + `olddefconfig`.
* **335.** A stock-mailbox image with **`panic=10`** boot-loops and spends
  `BOOTCTR_FCTRY` (measured `0x22 → 0x1c` in ~6 cycles; `/init` never runs, so no
  top-up).  Recovery: reflash `0xC675E206`, whose `/init` topped `0x1c → 0x32`
  (`PASS`).  **Boot a deliberately-faulting image with `panic=0`.**
* **336.** A **power cut damages the read-write hd3 ext4** (`barrier=0` +
  write-through): `EXT4-fs: This should not happen!! Data will be lost`, corrupt
  inode bitmap, a lost `/var/log` file.  Prefer a clean `reboot`; reflashing the
  Nura image at `0x4915` recovers it.
* **337.** The **USB/VUSB teardown is intermittent**, not an idle timer (30 s,
  90 s, and none in a 376 s boot).  Only `twl6030_usb_irq()` disables `usb3v3`;
  the trigger is not settled (contradiction file).
* **338.** The mailbox fault's **bad indirect transfer is
  `phy->comparator->set_vbus`**, a stale TWL6030 `phy_companion`, **not** a wild
  `otg->set_vbus` at entry: entry `sv = 0xc0729b94` is valid kernel text, and the
  Oops jumps to `0xc2c05800` (slab) inside `omap_usb_set_vbus()`'s tail call
  (report 264).  **Resolved in session 67** — see trap 339.
* **339.** `twl6030_usb_probe()` stored `&twl->comparator` in the `omap-usb2`
  PHY **before** `twl6030_usb_ldo_init()`; the ldo init returned `-ENODEV` for
  **every** `regulator_get(dev, "usb")` error, so a deferred `vusb` failed the
  probe permanently, devres freed the `devm_kzalloc`'d `twl`, and
  `phy->comparator` dangled (the report-264 `kmalloc-192` devm object whose
  first word was a `kmalloc-1k` heap pointer).  The device-mode-only workaround
  only hid the dereference.  Fix: propagate `PTR_ERR()`, clear the comparator
  with a `devm_add_action_or_reset()` action on probe failure/remove, and
  `usb_put_phy()` the reference `omap_usb2_set_comparator()` leaked.  Validated
  3/3 clean dual-core boots with the stock call restored (report 265).
* **340.** **`online=0-1` does not mean both cores take userspace work.**  On
  the SMP image PID 1 was affinity-masked to `{0}` (`taskset -p 1` → 1,
  `nproc` → 1); every userspace process inherited it, only kernel threads had
  `0-1`, and `taskset -c 1 <work>` still worked.  3/3 fresh boots.  Fix on hd3's
  root: `::sysinit:/usr/sbin/taskset -p 3 1` in `/etc/inittab` before OpenRC
  (verified 2 boots each way; `nproc` 2).  It is a root change, not an image
  change.  A kernel fix in `sched_init_smp()` is owed (report 266, trap 340).
  **Check `nproc`, not just `online`,** after anything CPU-related.
* Still current: **333** (the gadget bind is the trigger; the workaround is the
  device-mode-only call), **332** (dual-CPU `printascii` garble), **331** (power
  ruled out), **330** (the IPI is serviced), **327** (`nosmp` is not
  `nr_cpu_ids=1`), **326** withdrawn, **323** (bootrom ~20 s), **322** (never
  program WDT2 from the chainloader), **321** (ring hand-off), **320** (region-1
  modules mismatch), **302** (paced pushes), **300** (eMMC stall), **274** (the
  Nura ACM shell is not `pb> `), **190** (module-only CRC), **84** (the wrong
  IFS), **55** (containers), **47** (raw media offsets).

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **the active plan** | `docs/project/ROADMAP.md` item 7a, reports **266**, 265 |
| **the CPU/USB evidence** | reports **266**, 265, 264, 263, 262, 259–261, 257, 256 |
| **the CPU-affinity workaround** | hd3 `/etc/inittab` (`::sysinit:/usr/sbin/taskset -p 3 1`); backups `.session67-orig`, `.fixed` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| initramfs root selection | `initramfs/init.c` `ROOTFS_RAW_OFF` (hd3) |
| flasher (region 0/1, hd3) | `tools/bbusb.py` (+ `local/work/flash825.sh`) |
| ring tools | `tools/pb_ring.py`, `tools/bench/ringdump.c` |
| bench drivers | `local/work/soak65.py`, `local/work/kmsgcap.py` |
| the hd3 root image | `local/work/nura/images/…-osk-wvkbd.img` |
| the ARMv7 modules | `local/work/modstage/`, `…-armv7.tar.gz` |
| BT service pack | `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` |
| OSS player / mixer / BT probe | `rootfs/pbplay.c`, `rootfs/pbmix.c`, `rootfs/hci.c` |
| reports / sessions | `docs/reports/` (266 current), `docs/sessions/session-67.md` |

## 6. Working rules

* After every console command, dump the log tail and read it.  Read
  `dmesg`/`sloginfo` **unfiltered** after a reboot or structural change.
* **Don't clear `dmesg` under the operator** without saying so.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  **A test boot is not a budget concern.**
* **Never present a guess as a fact.**  Prefer a fresh read.  A diagnostic can
  manufacture the fault it reports.
* **Revert experiments and prove the revert.**  A reverted experiment still has
  to be shown reverted.
* **Use the reference before the bench** (DECISIONS §61).  Subagents:
  static/file-only (trap 109) — and **time-box them**.
* **Look up every external fact** (the operator's rule; the assistant's
  knowledge is frozen).
* **Only the operator decides when to stop / wrap up.**

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 68 (absolute; Linux-era 62)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a, KNOWN-ISSUES.md §2 (traps 14-340, especially 333-340)
and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 91-99, docs/reports/INDEX.md,
reports 266, 265, 264, 263, 257, and sessions/session-67.md.

WHERE IT STANDS: the unit runs Linux 7.3.3, dual-core at 1008 MHz, on the ARMv7
image 0x3547D613 (#109 SMP), with the Nura root on hd3 (grown to 62.5 G, ARMv7
modules installed); pvrsrvkm binds and catacomb runs.  THE MAILBOX FAULT IS
FIXED (report 265, trap 339): the Oops was a freed TWL6030 phy_companion —
twl6030_usb_probe() registered &twl->comparator before ldo_init(), a deferred
vusb was collapsed to -ENODEV, the probe failed permanently and devres freed
the devm twl while phy->comparator dangled; the winchester early return is gone
and the stock omap_musb_set_mailbox() call runs (3/3 clean boots).  AND THE
SECOND CORE WAS DOING NO USERSPACE WORK (report 266, trap 340): PID 1 was
affinity-masked to {0}, so nproc read 1 with online=0-1; hd3's /etc/inittab now
has ::sysinit:/usr/sbin/taskset -p 3 1 (nproc 2, load balanced, 2 boots each
way).  cpufreq-dt IS bound and scales (300000 600000 800000 1008000, ondemand);
no CPU supply is attached, and the twl6030_uv_to_vsel OUT OF RANGE lines are
startup VC init clamped to VSEL 0x3A = 1.35 V, the chainloader's own value.

THE ACTIVE ITEMS: (a) re-measure the memory throughput (report 263 §4b) and the
UI latency now that CPU 1 actually takes userspace work — the old numbers are
single-core numbers; (b) decide the CPU voltage: attach a supply binding or keep
1.35 V for all OPPs and skip the out-of-range OPP entry (this TWL6030 revision
cannot express >1.317 V except the hardcoded 1.35 V; 1.2 GHz crashes, report
257); (c) replace the inittab affinity line with a kernel fix — one
instrumented boot printing cpu_active_mask/cpu_online_mask at the top of
sched_init_smp() and PID 1's cpus_mask after the call would settle it (the
PBCPU/PBDBG markers are already in local/kernel-tree/init/main.c); (d) the
intermittent USB teardown (trap 337) — capture before the drop; (e) rebake the
hd3 base with epitaph r10005+, the llvmpipe files, the rules AND the inittab
line.

BENCH: region 0 = 0x3547D613 (#109 SMP, stock mailbox + comparator fix,
panic=0) at block 0x825; region 1 = the BusyBox ext4 6f5b8ca7... at 0x29000000
(fallback, not booted); hd3 = the Nura ext4 07555bde... at 0x4915, grown to
62.5 G, now with the inittab affinity line.  SSH: PB_LINK=usb tools/pbssh.sh
DID NOT ANSWER on the Nura root this session (RNDIS/usb0); the ACM
(playbook:~#, not pb>) drives it — local/work/pbcmd.py runs one command and
waits for its own marker.  Kernel printk goes to the panel; the
console=ttyGS0 bootarg is inert.  Use `reboot -f` on the Nura root (plain
reboot is very slow).  BOOTCTR_FCTRY = 0x24 at the session-67 close; read it
with dmesg | grep pbrpmb.  Start the flasher, then reset/power on (the
power-on bootrom holds ~20 s); the 626 MB hd3 write takes ~20 min.  /tmp is
tmpfs; re-push ringdump.  Read dmesg UNFILTERED.  The eMMC WRITE-busy stall is
live and has damaged the rw ext4 once — prefer clean reboots.  An agent NEVER
writes RPMB; NVRAM read-only.  Never program WDT2 from the chainloader (trap
322).

PATCH: 67 files / 9 136 lines, sha256 d19adff8..., verified 0 rejects / 67/67
against pristine 7.3.3 (session 67).  Re-export (and update
kernel/patches/README.md + KERNEL-PORT.md) after any DTS or driver change.  The
reliable method is in §2: pristine + the previous patch, overwrite the intended
files, diff -u -N, apply to a fresh pristine scratch and cmp every file; run the
tree walk (trap 211).  `local/work/export67/` is the working example.  No kernel
change was needed for the affinity workaround (it is on hd3), so the patch is
unchanged by it.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
The hard hazards (BOOTCTR_FCTRY 0 / >0x32 / failed write / flat pack) remain.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  WRAP-UP: only the operator
decides when the session stops.  Record the bench state, write the session note,
update the canonical docs, verify with tools/docs_audit.py and
tools/tests/test_offline.py, then commit privately and publish the snapshot
(docs/project/REPO-MANAGEMENT.md).  No restore is required.
```
