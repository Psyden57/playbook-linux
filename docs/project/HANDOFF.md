# Handoff — session 55 (absolute)

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real userland, and the
board's own peripherals working — now with the end state being **Nura on hd3
with GPU acceleration**.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console **and its RNDIS network link**, the whole display chain, the
64 MiB ext4 BusyBox root in region 1 (ssh + the wireless stack), the peripherals
(I²C4 motion sensors, TMP103, BQ27541, FAN5702, CM3217, ISL9519 charger), the
**Munin touch**, the **WM8994 audio** (headphones + both speakers, 48 kHz), the
**WL1283 WLAN** (associates, DHCP, traffic on the region-1 root), and **the
WL1283's BT core** (real inquiry; `hci0` registers with mgmt).

**hd3:** the **Nura** root was flashed at loader block **`0x4915`** and it
**boots** (7.3.3, Nura edge, OpenRC, the display and the Munin touch) — but the
first ext4 journal/udev write stalls the eMMC (`mmc1: Card stuck being busy!`),
aborts the journal and remounts the root read-only, so `tinydm`/`catacomb`
cannot start.  The **read-only hd3 + overlayfs root is implemented and built**
(report 242); **the flash did not run** (the operator stopped before it).  The
unit is still on the old `0x060B25F2` + the dirty hd3.

**What is left, in order:** flash the overlay build and boot the DE (§0d), then
SGX540 (§0e).  BT's controller open and FM/GPS are parked with their blockers
recorded (§0f).

## 0a. The media, and the way back

* **region 0 = `0x060B25F2`** (`0x939D04` B, kernel `7.3.3`, the `lstat`
  hand-off with `ROOTFS_RAW_OFF = 0x49000000`; sha256 `b548ec6c…`;
  `boot/chainloader/kernel.img` holds the newer `0xE85217E5`).  **Unchanged.**
* **region 1 = `49e318bf…`** (`local/images/rootfs-region1-wl1283-bt.ext4`,
  67 108 864 B, **43 modules**).  **This stays intact as the fallback through
  the whole overlay move.**
* **hd3 (`0x4915`, raw `0x49000000`) = the dirty Nura root** from the session-54
  flash (the journal aborted; the pristine host image
  `local/work/nura/images/blackberry-playbook-root-hd3.img`, sha256
  `076bafcf…`).  The new edited image
  `local/work/nura/images/blackberry-playbook-root-overlay.img` (sha256
  `1f14915f…`) and the packed container
  `local/work/nura/images/both-0xE85217E5-overlay.signed` (sha256 `c3ed1f61…`)
  are **built, not flashed**.
* The unit is **on the Nura root** (`0fca:8010`, console `playbook:~#` on the
  ACM `/dev/ttyACM0`, `ttyACM0` `crw-rw-rw-`); **no sshd**.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`, then reboot.  It enumerates `0fca:8012`; qconn
  `169.254.0.1:9090`, root ssh — use **`PB_LINK=door tools/pbssh.sh`**.  The
  region-0 RCFS slot holds the **full 1917 RCFS** (trap 227); for the *minimal*
  oracle flash the pruned pair `local/work/minimal/setB.signed`.  `/tmp` is
  wiped by a reboot.
* **To stock TabletOS:** flash **two components** — `local/work/ifs-shim-sealed.ifs`
  at `0x825` **and** `local/work/rcfs-1917.rcfs` at `0x8D5`, plus the vendor UFS
  (`local/work/ufs-vendor.signed`, 707 MB) if hd3 has been repurposed.
  **Do not restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).
* **The way back** from a bad overlay is the same: region 1 boots the BusyBox
  root; the 1917 IFS restores TabletOS; hd3 is always a flasher-restorable
  image (the operator's design).

## 0b. The WLAN — it associates (session 52, unchanged)

The blocker was the missing `ccm`/`ctr` crypto modules
(`ieee80211_key_alloc()` needs a `ccm(aes)` AEAD; without it the PTK install is
`-ENOENT`, which wpa_supplicant reports as `WRONG_KEY`).  Working sequence after
a boot on the region-1 root (`/tmp` is wiped):

```sh
ip link set wlan0 up                 # rcS loads the stack, it does not raise it
iw reg set <CC>                      # e.g. AR — required for a 5 GHz AP
mkdir -p /var/run/wpa_supplicant
{ echo "disable_scan_offload=1"; wpa_passphrase '<SSID>' '<PSK>'; } > /tmp/wpa.conf
iw dev wlan0 scan >/dev/null 2>&1
wpa_supplicant -B -i wlan0 -D nl80211 -c /tmp/wpa.conf -C /var/run/wpa_supplicant
wpa_cli -p /var/run/wpa_supplicant -i wlan0 status      # want wpa_state=COMPLETED
```

The **Nura hd3 image now carries `ti-connectivity/{wl128x-fw-4-sr.bin,
wl128x-fw-5-sr.bin,wl128x-nvs.bin}`** (report 242 §3); the region-1 root already
has them.  Wi-Fi is 1×1 HT20 (72 Mbit/s) — **do not advertise HT40** (trap 247).
Do **not** use `IRQ_TYPE_LEVEL_LOW` for WL_IRQ (trap 238), **not**
`wlcore_sdio dump=1` (trap 239), **not** 5-sr (trap 243).

## 0c. BT — what works, and where it stopped

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
demonstrated.

## 0d. THE ACTIVE ITEM — the read-only hd3 + overlayfs root (report 242, ROADMAP 7a)

### The design, which is the operator's

**hd3 holds a flasher-restorable ext4 image**, and the eMMC must not be written
on boot.  Report 241 §3 measured the stall: after the 597 MiB bootrom image, the
first ext4 journal/udev rewrite gives `mmc1: Card stuck being busy!
__mmc_poll_for_busy`, the loop write fails, the journal aborts and ext4 remounts
`emergency_ro`; a later 4 KiB write returns `I/O error` while reads still work.
The region-1 root has **no journal** and is stable.  So the overlay build mounts
hd3 read-only and holds every write in a tmpfs upper:

* the backing `/dev/mmcblk1` is opened `O_RDONLY`, the loop is
  `LO_FLAGS_READ_ONLY`;
* the lower is `MS_RDONLY` + `noload` (the dirty media journal is not replayed);
* a 256 MiB tmpfs at `/overlay` holds `upper`/`work`; overlayfs is at
  `/newroot` with `lowerdir=/rootfs,upperdir=/overlay/upper,workdir=/overlay/work`;
* the `/dev`/`/proc`/`/sys` binds and the `lstat` hand-off are on the overlay;
  the chroot/exec is unchanged.

### THE ONE EXPERIMENT: flash the packed container and boot the overlay root

**Inputs (all built):**
`local/work/nura/images/both-0xE85217E5-overlay.signed` — 635 961 936 B, sha256
`c3ed1f61…`; it carries the region 0 (`0xE85217E5`) at `16,0` and the hd3 image
(`1f14915f…`) at `16640,0`; `parse` gives V1 runs 151/9552 and CRC
`0x493FA440 OK`.

**The flash — one bootrom session, from the booted Nura root:**

```bash
lsusb | grep -i 0fca                       # 0fca:8010 = OS; 0001 = bootrom; 8001 = ramloader
nohup python3 tools/bbusb.py flash --signed \
      local/work/nura/images/both-0xE85217E5-overlay.signed \
      --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
sleep 2
python3 tools/pbcon.py 'reboot -f'          # Nura ttyGS0 BusyBox shell; -f skips the shutdown
until grep -qE 'COMPLETE|done|error' /tmp/f.log; do sleep 2; done; tail -5 /tmp/f.log
```

`--from-bootrom` **uploads the ramloader**: the unit must be **booted**, not in
the ramloader; the flasher catches the 60 s bootrom window opened by
`reboot -f`.  One session carries both halves.  Success is
`COMPLETE 0x40C0 -> cmd 0x4006` and `done`.  The `ttyGS0` shell is a plain
`/bin/sh -l`; if `pbcon` says "no prompt — the shell is still running a
command", send Ctrl-C with `tools/pbraw.py 2 03` (or wait).  `ttyACM0` is
`0666`; if it comes back `root:uucp`, `pkexec chmod 666 /dev/ttyACM0`.

**What each result means.**

* **`COMPLETE`/`done`, the overlay boots** → check
  `dmesg | grep -E 'mmc1|EXT4|overlay|Card stuck|Aborting'` (no stall, no
  journal abort, `overlay` on `/newroot`), `dmesg | grep pbrpmb` (the counter),
  `rc-status`/the tinydm log, `/run/user/10000`, `/dev/dri/card0`, `lsmod`/
  `wlcore`, the touch, the backlight.  If the DE runs → SGX540 (§0e).
* **The overlay mount fails** → `dmesg` for `overlay: mount` / `tmpfs` /
  `rootfs: mount`; the `0xE85217E5` must carry `CONFIG_OVERLAY_FS=y`; the lower
  must be mounted read-only.
* **The eMMC still stalls** → the lower was not read-only (check the mount
  flags/the loop `ro`), or the card is in the failed state from the earlier
  boot; a cold power cycle + a fresh hd3 flash decides.  The writable-root fix
  (busy timeout / cache / no-journal) is a separate experiment.
* **The DE still fails** → read the tinydm/elogind log and `/run/user/10000`;
  the likely causes are the seatd/logind backend or the PAM stack.  The root is
  writable in the overlay, so a `seatd`/`elogind` test is possible.
* **The WLAN firmware loads but no link** → the NVS/regdomain; `iw reg set`.
* **The flash fails** → `0xF7 -> cmd 0x15` means the gate rejected (nothing
  written); check the container's runs (16/16640) and the `--from-bootrom`
  timing.  If the unit is left in the ramloader, `bbusb.py reset-loader`.

### Facts about the overlay/images (measured)

* **region 0 `0xE85217E5`**: `0x9662A4` B (< `0xB00000`), sha256 `fbeb6df0…`;
  `local/images/kernel-0xE85217E5-overlay.img`.
* **hd3 image `1f14915f…`**: 625 999 872 B; the root `/etc/fstab` line removed
  (only the header comment), the `tty1` getty removed from `/etc/inittab` (the
  `ttyGS0` shell kept), the `ti-connectivity` firmware added, the 43 modules
  replaced from `local/work/modules-7.3.3`; `e2fsck -fn` exit 0.
* **The vermagic matches:** both the `0xE85217E5` kernel
  (`local/kernel-tree/vmlinux`) and the new `wl12xx.ko` are
  `7.3.3 mod_unload modversions ARMv6 p2v8`.
* **The container** `c3ed1f61…`; the V1 runs at offset 16 (151 blocks) and
  16640 (9552 blocks); the signature CRC `0x493FA440 OK`.
* **Counter:** `BOOTCTR_FCTRY = 0x1e` at the session-54 boot; the same boot (no
  reboot since); the next boot spends one → `0x1d` and `/init` tops it to `0x32`.

## 0e. SGX540 (after the overlay boots)

**Mainline 7.3.3 has no driver** — nothing binds `ti,omap4430-gpu` /
`img,powervr-sgx540`, though `omap4.dtsi` supplies the target-module, `prm_gfx`
and the L3_GFX clock, and our board DTS leaves `&sgx_module` disabled.  The route
is `gitlab.com/pvrports/pvrports`: `linux-openpvrsgx`
(`CONFIG_SGX=y`, `CONFIG_SGX_OMAP=m`, `CONFIG_SGX_DRM=y`, tagged 6.1 → rebase to
7.3.3) plus **proprietary** `sgx-ddk-um` (`pkgver=1.17.4948957`, SoC subpackage
`ti443x`).  Its reference devices are the **Samsung espresso** — OMAP4430 with
TWL6030, the same SoC family and PMIC generation, including the `clk32kg` work we
already did for the WLAN.  **Blobs stay gitignored in `local/`.**  The
reference's own QNX SGX userspace (`libsrv_um.so`, `libusc.so`, GLESv1/v2, EGL)
is in `local/work/1917/base/usr/lib/graphics/omap4430/` with
`gpu-string = SGX540rev120`, but it is QNX ABI — **there is no separate SGX
firmware file anywhere in the reference**.

## 0f. FM and GPS (parked, after SGX)

**FM:** mainline carried `drivers/media/radio/wl128x` from 2.6.39 to 6.13 and
removed it; a forward port is the route.  **GPS:** no mainline driver; the
operator has a 2024 LKML patch (`lists.openwall.net/linux-kernel/2024/06/06/1445`)
to evaluate, and the reference's own path is the fallback.

## 0g. WM8994 audio, touch — closed

Audio: headphones and both speakers play `pbplay`'s 440 Hz tone at 48 kHz.
Listening tests: lower `pbmix set "DAC1 Volume" 40` **once**, restore `96` at the
end; a bare `pbplay` plays a 2 s tone (trap 212).  Touch: I²C2 `0x48072000` =
`i2c-1` at `0x65`, GPIO2_3 edge IRQ (`irq 110`); a **cold** boot leaves it down
until GPIO1_14/GPIO4_8 are pulsed, which the driver does not drive.

## 0h. The minimal 1917 oracle

Debloated 2.1.0.1917 IFS+RCFS (`tools/build_minimal_qnx.py`), qconn on
`169.254.0.1:9090` + root ssh.  Its `/base` is pruned (no firmware dir).  No
wifi/BT service runs, so `ifconfig ti0 up` and the BT start are manual.  Tools
are not on the UFS — push `memdump3`/`memw32`/`pbi2c` to `/tmp` after every boot.
**Bench only** (trap 173).

## 1. Useful local extracts

* `local/work/nura/images/` — the **hd3 Nura root** (`…-root-hd3.img` pristine
  `076bafcf…`; `…-overlay.img` edited `1f14915f…`) and the **packed container**
  (`both-0xE85217E5-overlay.signed`, `c3ed1f61…`).
* `local/images/kernel-0xE85217E5-overlay.img` — the region-0 overlay image
  (the saved copy of `boot/chainloader/kernel.img`).
* `local/work/modules-7.3.3/` — the staged modules (**43**, the `0xE85217E5`
  build), replaced into the hd3 image.
* `local/work/firmware-nvs/` — the firmware tree (the WLAN firmware added to the
  hd3 image).
* `rootfs/bt/` — **the BT bench tools and their README** (session 53).
* `local/work/1917/` — the 1917 `/base`, IFS tree and the WLAN calibration.
* `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` — the **correct** BT
  service pack (md5 `78bc1f0a…`; `Compatibility: Quattro, 6.0.15 ROM`).
* `local/work/1917/base/usr/lib/graphics/omap4430/` — the reference's QNX SGX
  userspace and `graphics.conf` (`gpu-string = SGX540rev120`).
* Reports **242** (the overlay build), **241** (the hd3 boot/eMMC), **240** (the
  pmbootstrap root), **239** (hd3/Nura/SGX scoping), 238 (BT), 232–237.

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

The new `initramfs/init.c` is the read-only/overlay code; the `0xE85217E5`
image above was built this way.  **Build modules IN-TREE, not with `M=`.**  Strip
with `--strip-debug`, never `strip -s`.  A module/firmware-only change keeps the
region-0 CRC (trap 190) and needs a **region-1-only flash** at block `0x2915`:

```bash
python3 tools/build_rootfs.py --size-mb 64 \
  --busybox local/work/src/busybox-1.36.1/busybox \
  --busybox-links local/work/src/busybox-1.36.1/busybox.links \
  --modules local/work/modules-7.3.3 --firmware local/work/firmware-nvs \
  --extras local/work/rootfs-extras --krel 7.3.3 --out local/images/<name>.ext4
```

`build_rootfs.py --out` **overwrites** (trap 213).

**The hd3 image edits (report 242 §3)** were done with `debugfs -w` on a copy:
remove the root `/etc/fstab` line, remove the `tty1` getty from `/etc/inittab`,
write the `ti-connectivity` firmware, replace the 43 `.ko` from
`local/work/modules-7.3.3`; then `e2fsck -fn` (exit 0).

**The pack (one bootrom session):**

```bash
printf 'boot/chainloader/kernel.img=16,0\nlocal/work/nura/images/blackberry-playbook-root-overlay.img=16640,0\n' > /tmp/both.lst
python3 tools/bbusb.py pack -l /tmp/both.lst -v 1,2 -s -f -c /tmp/both.signed
# the coordinate is the loader block minus 0x815: 0x825->16, 0x4915->16640
```

**Flash the packed container** (`--from-bootrom`, `--password 0000`, `--apply`)
and `reboot -f` from the booted Nura; see §0d.  **Patch export** (traps
161/211): walk the tree against pristine, filter build artifacts, apply to a
staged pristine copy and compare.  Current: **63 files / 8 998 lines**, sha256
`48c14a41…` (no kernel source change this session; the `init.c` is userspace).

## 3. Bench

* The unit is on the **Nura root** (image `0x060B25F2`), read-only
  (`emergency_ro`); the ACM is `0fca:8010`, `ttyACM0` 0666; **no sshd**.
* **The Nura `ttyGS0` shell is a plain `/bin/sh -l`.**  Use
  `tools/pbcon.py 'cmd'`; if it says "no prompt — the shell is still running a
  command", Ctrl-C with `tools/pbraw.py 2 03` (or wait).  `tools/pbssh.sh` is
  for the **region-1** root, not the Nura.
* **Reboot:** `tools/pbcon.py 'reboot -f'` (BusyBox; `-f` skips the shutdown).
  **The flash:** start the flasher first, then the reboot; one bootrom session;
  the 60 s window.
* **`BOOTCTR_FCTRY`** read **`0x1e`** at the session-54 boot (the same boot; no
  reboot).  `tools/pbcon.py 'dmesg | grep pbrpmb'` → the record and the
  threshold line.  A logged read, not an RPMB access (`SAFETY.md` §2).
* **The eMMC checks:** `dmesg | grep -E 'Card stuck|Aborting|EXT4|overlay'`;
  the overlay on `/newroot`; the lower read-only.
* **hd3 checks:** `dd` the superblock at `0x49000000`; mount from the running
  Linux with `losetup -o`; the device-side sha256 (the read-only root makes it
  safe).
* **WLAN checks (region-1):** `lsmod` must show **`ccm` and `ctr`**;
  `ip link set wlan0 up` → `wlcore: firmware booted (Rev 7.3.5.0.98)`.
* **BT checks:** `BT_EN` at pad `0x4A10007C` mux 3 and `GPIO2_DATAOUT` bit 23;
  UART2 pads `0x4118`/`0x411A`; then `rootfs/bt/`'s tools.  **`btdump` first** —
  it hexdumps and cannot hide a reply.  Check `cat /proc/tty/ldiscs` before
  concluding "silent".  After a cold boot **export `gpio535` first** (trap 252).
* WM8994: `i2ctransfer` args are DECIMAL (trap 166); bus 4, addr `0x1a`.
* QNX device tools build `-static`; `/tmp` is wiped by a reboot.

## 4. Traps added last session (full list 14–271 in `KNOWN-ISSUES.md` §2)

* **266.** `--from-bootrom` uploads the ramloader and needs the unit **booted**;
  start the flasher, then reboot (`reboot -f`); the bootrom window is 60 s.
  "Both halves in one bootrom session" means one packed container.
* **267.** The `bbusb.py pack` coordinate is the loader block **minus `0x815`**
  (`0x825`→16, `0x2915`→8448, `0x4915`→16640); `parse` must show the V1 runs and
  the CRC `OK`.
* **268.** The Nura `ttyGS0` shell is a plain `/bin/sh -l`; `pbcon.py` works at
  a prompt; "no prompt" means `pbraw.py 2 03` (Ctrl-C).  `reboot -f` skips the
  shutdown.
* **269.** The read-only overlay: `MS_RDONLY`+`noload`, `LO_FLAGS_READ_ONLY`, an
  `O_RDONLY` backing, a tmpfs upper/work, `/newroot`; the binds and the `lstat`
  hand-off go on the overlay.  `CONFIG_OVERLAY_FS=y` is in the `0xE85217E5`.
* **270.** The new 43 modules and the `0xE85217E5` kernel share the vermagic
  `7.3.3 mod_unload modversions ARMv6 p2v8`; the image's modules were replaced,
  the `modules.dep` unchanged.
* **271.** The hd3 image needs the root fstab line and the `tty1` getty removed
  and the `ti-connectivity` firmware; `e2fsck -fn` must exit 0.
* Still current: **265** (`CONFIG_DYNAMIC_DEBUG`/`OVERLAY_FS`; the modules), 264
  (the `PB*` prints are `pr_debug`), 261 (the eMMC stall), 260 (`lstat`), 259
  (`0x4915`), 253–258 (pmbootstrap), **250** (the `0x30`), **248** (the UART2
  CTS/RTS pads), **247** (no HT40), **244** (`ccm`/`ctr`), **246** (MUSB
  alignment), **243** (4-sr not 5-sr), **241**, **239**, **238**, **237**, **236**
  (`CLK32KG`), **227**, **224** (`reboot -f` wedge), **221** (`slay`), **214**
  (composite net iface), **212** (bare `pbplay`), **211** (tree walk + `Kconfig.*`),
  **190** (module-only CRC).

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **the active plan (hd3/Nura/SGX)** | `docs/project/ROADMAP.md` item 7a, report **242** |
| **the overlay root code** | `initramfs/init.c` (`rootfs_mount`/`overlay_mount`) |
| **the packed both-halves container** | `local/work/nura/images/both-0xE85217E5-overlay.signed` |
| **the hd3 images** | `local/work/nura/images/blackberry-playbook-root{-hd3,-overlay}.img` |
| **the region-0 overlay image** | `local/images/kernel-0xE85217E5-overlay.img` |
| flasher (region 0, region 1, **hd3**) | `tools/bbusb.py` |
| the module staging | `local/work/modules-7.3.3/` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| BT service pack | `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` |
| reference SGX userspace | `local/work/1917/base/usr/lib/graphics/omap4430/` |
| OSS player / mixer / BT probe | `rootfs/pbplay.c`, `rootfs/pbmix.c`, `rootfs/hci.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| reports | `docs/reports/` (**242** current) |
| sessions | `docs/sessions/session-54.md` (including the Continuation) |

## 6. Working rules

* After every console command, dump the log tail and read it.  Read
  `dmesg`/`sloginfo` **unfiltered** after a reboot or structural change.
* **Don't clear `dmesg` under the operator** without saying so.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  **A test boot is not a budget concern.**
* **Never present a guess as a fact.**  Prefer a fresh read.
* **Use the reference before the bench** (DECISIONS §61).  Subagents:
  static/file-only (trap 109) — and **time-box them**.
* **Look up every external fact.**  postmarketOS was renamed **Nura** on
  2026-09-27; search "Nura", and expect `postmarketos`/`pmaports` in package
  names and many URLs.
* **Revert experiments and prove the revert.**
* **The flash:** `--from-bootrom` needs the unit **booted**, not the ramloader;
  one bootrom session for both halves; the pack coordinate is the loader block
  minus `0x815`; the 60 s window; `reboot -f` from the Nura `ttyGS0`.
* **The read-only hd3:** never mount it read-write; the writes live in the
  tmpfs overlay; the lower is `MS_RDONLY`+`noload`; the media stays pristine.
* **The kernel/module match:** the `7.3.3 mod_unload modversions ARMv6 p2v8`
  vermagic; the 43 modules; the `0xE85217E5` config.
* **Only the operator decides when to stop / wrap up.**

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 55 (absolute; Linux-era 49)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a AND item 9, KNOWN-ISSUES.md §2 (traps 14-271,
especially 266-271) and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md
(61-76), docs/reports/INDEX.md, reports 240-242, and sessions/session-54.md
(including its Continuation).

WHERE IT STANDS: audio (headphones + both speakers, 48 kHz), the touch, the
sensors/charger, ssh over USB and the display all work; the WL1283 WLAN
associates and passes traffic on the region-1 root; the WL1283 BT core works
(a real BR/EDR inquiry; the reference's WL1283.bts 315/315 makes hci0 register
with mgmt) with the controller open parked on the stray 0x30 byte (trap 250).

hd3 holds the Nura root and NURA BOOTS FROM IT (7.3.3, Nura edge, OpenRC, the
display and the Munin touch) — but the first ext4 journal/udev write stalls the
eMMC (mmc1: Card stuck being busy!), aborts the journal and remounts the root
read-only, so tinydm/catacomb cannot start.  The READ-ONLY hd3 + OVERLAYFS root
is IMPLEMENTED AND BUILT (report 242); THE FLASH DID NOT RUN (the operator
stopped before it).  The unit is still on the old 0x060B25F2 + the dirty hd3.

THE ACTIVE ITEM (operator): ROADMAP item 7a — the read-only hd3 + overlayfs
root, then SGX540.  The design is the operator's: hd3 holds a FLASHER-RESTORABLE
ext4 image, and the eMMC must not be written on boot.  The initramfs mounts the
loop LO_FLAGS_READ_ONLY over an O_RDONLY /dev/mmcblk1, the ext4 MS_RDONLY +
noload, and a 256 MiB tmpfs upper/work overlaid at /newroot; the /dev//proc//sys
binds and the lstat hand-off are on the overlay.

THE ONE EXPERIMENT: flash the packed both-halves container in one bootrom
session and boot the overlay root.  Start
  nohup python3 tools/bbusb.py flash --signed \
      local/work/nura/images/both-0xE85217E5-overlay.signed \
      --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
then `tools/pbcon.py 'reboot -f'` from the Nura ttyGS0 shell (if "no prompt",
Ctrl-C with tools/pbraw.py 2 03).  --from-bootrom UPLOADS THE RAMLOADER: the
unit must be BOOTED, not in the ramloader; the flasher catches the 60 s bootrom
window opened by reboot -f.  One session carries both halves (the container:
region 0 0xE85217E5 at 16,0; hd3 1f14915f... at 16640,0; parse CRC
0x493FA440 OK).  Success is `COMPLETE 0x40C0 -> cmd 0x4006` and `done`.

THEN CHECK: dmesg | grep -E 'mmc1|EXT4|overlay|Card stuck|Aborting' (no stall,
no journal abort, overlay on /newroot), dmesg | grep pbrpmb (the counter),
rc-status / the tinydm log, /run/user/10000, /dev/dri/card0, lsmod / wlcore, the
touch, the backlight.

WHAT EACH RESULT MEANS:
* COMPLETE + the overlay boots -> the eMMC is out of the boot path; check the
  DE; if it runs -> SGX540.
* the overlay mount fails -> dmesg for overlay/tmpfs/rootfs; the 0xE85217E5
  must have CONFIG_OVERLAY_FS=y; the lower must be read-only.
* the eMMC still stalls -> the lower was not read-only or the card is failed
  from the earlier boot; cold power cycle + a fresh hd3 flash decides.
* the DE still fails -> the tinydm/elogind log + /run/user/10000; the seatd/
  logind backend or the PAM stack; the overlay root is writable.
* the flash fails -> 0xF7 -> cmd 0x15 means the gate rejected (nothing
  written); check the runs (16/16640) and the --from-bootrom timing; if left in
  the ramloader, bbusb.py reset-loader.

THE FACTS: region 0 0xE85217E5 (0x9662A4 B, sha256 fbeb6df0...); the hd3 image
1f14915f... (root fstab line + tty1 getty removed, the ti-connectivity firmware,
the 43 modules; e2fsck -fn exit 0); the kernel/module vermagic
7.3.3 mod_unload modversions ARMv6 p2v8; the container c3ed1f61... (635 961 936
B; the V1 runs at 16/16640; CRC 0x493FA440 OK); BOOTCTR_FCTRY 0x1e (the same
boot; the next boot 0x1d -> /init tops it to 0x32).

SGX (after the overlay boots): mainline 7.3.3 binds NOTHING to ti,omap4430-gpu
though omap4.dtsi has the target-module, prm_gfx and L3_GFX clock.  Route:
gitlab.com/pvrports/pvrports — linux-openpvrsgx (CONFIG_SGX=y, CONFIG_SGX_OMAP=m,
CONFIG_SGX_DRM=y; tagged 6.1, rebase to 7.3.3) plus PROPRIETARY sgx-ddk-um
(1.17.4948957, subpackage ti443x).  Reference devices are OMAP4430 Samsung
espresso.  Blobs stay gitignored in local/.  The reference's QNX SGX userspace
(graphics.conf: gpu-string = SGX540rev120) is QNX ABI and there is NO separate
SGX firmware file in it.

PARKED: BT controller open (trap 250: a lone 0x30 byte breaks the kernel's H4
receiver; origin unknown; flow control ruled out), LE (refused by the
controller, but the chip reports BT 4.0 with the LE bit — no known enable
sequence, impossibility NOT demonstrated), FM (forward-port
drivers/media/radio/wl128x, removed in 6.13), GPS (2024 LKML patch, else RE the
reference).

PHYSICAL STATE: region 0 = 0x060B25F2 (0x939D04, the lstat image; sha256
b548ec6c...); region 1 = 49e318bf... (the fallback); hd3 = the dirty Nura root
at 0x4915 (the journal aborted; the pristine host 076bafcf...).  The unit is on
the Nura root (0fca:8010, console playbook:~# on the ACM, no sshd).  The new
0xE85217E5 / the hd3 overlay image / the packed container are on the HOST ONLY.
To boot the ORACLE: flash local/work/ifs-shim-sealed.ifs at 0x825.  To stock
TabletOS: that IFS at 0x825 + local/work/rcfs-1917.rcfs at 0x8D5 (+ the vendor
UFS if hd3 was repurposed).  /tmp is wiped by a reboot.  Do NOT restore
local/images/ifs0.sealed.bin (trap 84).

BENCH: the Nura ttyGS0 shell is a plain /bin/sh -l — use tools/pbcon.py 'cmd';
if "no prompt -- the shell is still running a command", Ctrl-C with
tools/pbraw.py 2 03 (or wait).  tools/pbssh.sh is for the REGION-1 root, not the
Nura.  REBOOT: tools/pbcon.py 'reboot -f' (BusyBox; -f skips the shutdown).  The
flash: start the flasher first, then the reboot; one bootrom session; the 60 s
window.  ttyACM0 is 0666; if it comes back root:uucp, pkexec chmod 666
/dev/ttyACM0.  An agent NEVER writes RPMB; NVRAM read-only.  Read dmesg
UNFILTERED; do not clear it without saying so.

BT: BT_EN pad 0x4A10007C mux 3 + GPIO2_DATAOUT bit 23; UART2 pads
0x4118/0x411A; /dev/ttyS1.  After a cold boot EXPORT gpio535 first and pulse low
2 s (trap 252).  ALWAYS check `cat /proc/tty/ldiscs` — a stale N_HCI eats the
bytes and the chip looks dead.  To swap a module: rmmod btbcm THEN hci_uart, and
verify by content (trap 251).  WL1283 checks: `i2ctransfer -f -y 0 w1@0x48 0xbc
r3` -> 07 01 55; `lsmod` must show ccm and ctr; `ip link set wlan0 up` ->
firmware Rev 7.3.5.0.98.  WM8994: i2ctransfer args DECIMAL, bus 4 addr 0x1a;
debugfs is not mounted by rcS.  On the oracle use pbi2c rd16/wr16; push
memdump3/memw32/pbi2c to /tmp after every boot.

LISTENING TESTS ARE QUIET AND SHORT: lower `pbmix set "DAC1 Volume" 40` ONCE,
run every test, restore 96 ONCE at the end; a bare `pbplay` plays a 2 s tone
(trap 212); state what is playing (trap 191).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4; for a QNX ELF reconcile
with ntoarmv7-readelf/objdump.  QNX device tools build -static.  USE THE
REFERENCE FIRST, and put static RE in subagents (file-only, trap 109) — and
TIME-BOX them.  The TI TRM is local (hardware/swpu231ap.pdf.txt); the TWL6030
datasheet is hardware/twl6030.pdf.

BUILD: modules IN-TREE (`make ... modules`), never `M=`; strip with
`--strip-debug`, never `strip -s`.  The hd3 image edits are debugfs -w on a
copy, then e2fsck -fn.  The pack coordinate is the loader block minus 0x815.
Re-export the kernel patch only when the DTS or a driver changes; the init.c is
userspace (the patch is 63 files / 8 998 lines, sha256 48c14a41...).

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed; a
module/firmware-only change keeps the CRC and needs a region-1-only flash (trap
190).  The hard hazards (0 / >0x32 / failed write / flat pack) remain.  The
read-only hd3 is never mounted read-write.  Operator's call when to stop and to
wrap up.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  Re-export the kernel patch
whenever the DTS or a driver changes (trap 161) and verify by apply-and-compare,
and run the TREE WALK too (trap 211, filtering Kconfig.*).  A reverted
experiment still has to be proven reverted.

WRAP-UP: only the operator decides when the session stops.  Record the bench
state, write the session note, update the canonical docs, verify with
tools/docs_audit.py and tools/tests/test_offline.py, then commit privately and
publish the snapshot (docs/project/REPO-MANAGEMENT.md).  No IFS restore is
required.
```
