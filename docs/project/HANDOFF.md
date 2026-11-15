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

**hd3:** the **Nura** root boots on the read-only + overlayfs design (session 55,
report 243): region 0 `0xA8F0F30C`, hd3 `1fcf0cb0…`, both written in one bootrom
session.  The lower is `MS_RDONLY`+`noload` on a `LO_FLAGS_READ_ONLY` loop,
writes go to a 256 MiB tmpfs upper at `/overlay`, and the boot has **no eMMC
stall and no journal abort**.  `elogind`, `tinydm` and **`catacomb` run**,
`/run/user/10000` and the catacomb Wayland socket exist, the touch is ready and
the backlight is on (255).  The screen is black with only catacomb's empty bar
because the GL path is not there yet.

**SGX540 (session 55, reports 244/245): THE GPU WORKS.**  The
`openpvrsgx-devgroup` 7.3 driver (DDK 1.17.4948957) is in-tree and binds
(`pvrsrvkm 56000000.gpu`, `card1`/`renderD129`).  The pvrports userspace
(`libglvnd` + `mesa-pvr-dri-classic` 21.3.9 + `sgx-ddk-um` 1.17.4948957, with
`gcompat` and `wayland-libs-server`) initializes it —
`PVR_K: UM DDK-(4948957) and KM DDK-(4948957) match. [ OK ]` — and a headless
EGL/GLES 2.0 context reports **`GL_RENDERER = PowerVR SGX 540`**,
`GL_VERSION = OpenGL ES 2.0 build 1.17@4948957`.  **catacomb cannot use it**:
smithay needs `GL_EXT_unpack_subimage` and panics at renderer creation
(trap 282); pvrports supports Phosh (patched phoc) and Plasma.

**What is left:** the stack is **baked into hd3** (`85b44deb…`: module
autoload, service, env) and verified on a fresh boot; the only failing piece is
the **compositor** — catacomb panics on `GL_EXT_unpack_subimage`, so the panel is
black with the backlight on.  Choose a compatible compositor (§0e).  BT's
controller open and FM/GPS are parked with their blockers recorded (§0f).

## 0a. The media, and the way back

* **region 0 = `0xA8F0F30C`** (`0x967880` B, kernel `7.3.3 #68`, the read-only
  hd3 + overlayfs init **with `&sgx_module` enabled**; sha256 `fb9905f3…`;
  `boot/chainloader/kernel.img` holds the same image).  **Flashed in session 55.**
* **region 1 = `49e318bf…`** (`local/images/rootfs-region1-wl1283-bt.ext4`,
  67 108 864 B, **43 modules**).  **This stays intact as the fallback.**
* **hd3 (`0x4915`, raw `0x49000000`) = the Nura overlay image + the SGX pvr
  userspace** (`85b44deb…`, 625 999 872 B), flashed **alone** with
  `--component --dest-block 0x4915`.  It carries the module (in `/etc/modules`
  + `modules.dep`), the pvrports stack, the `sgx-ddk-um` service and the
  `MESA_LOADER_DRIVER_OVERRIDE=pvr` tinydm env.  It is mounted read-only at
  boot.  The pre-pvr images (`1fcf0cb0…`, `1f14915f…`) and the pristine
  `076bafcf…` remain on the host.
* The unit is **on the Nura overlay root** (`0fca:8010`, console
  `playbook:~#` on the ACM `/dev/ttyACM0`, `ttyACM0` `crw-rw-rw-`); **no sshd**;
  **`usb0` is DOWN** on this root (bring it up by hand if a fast transfer is
  needed).
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

## 0d. DONE (session 55) — the read-only hd3 + overlayfs root is flashed and booting

### The design, which is the operator's

**hd3 holds a flasher-restorable ext4 image**, and the eMMC must not be written
on boot.  Report 241 §3 measured the stall: after the 597 MiB bootrom image, the
first ext4 journal/udev rewrite gives `mmc1: Card stuck being busy!
__mmc_poll_for_busy`, the loop write fails, the journal aborts and ext4 remounts
`emergency_ro`.  So the overlay build mounts hd3 read-only and holds every write
in a tmpfs upper:

* the backing `/dev/mmcblk1` is opened `O_RDONLY`, the loop is
  `LO_FLAGS_READ_ONLY`;
* the lower is `MS_RDONLY` + `noload` (the media journal is not replayed);
* a 256 MiB tmpfs at `/overlay` holds `upper`/`work`; overlayfs is at
  `/newroot` with `lowerdir=/rootfs,upperdir=/overlay/upper,workdir=/overlay/work`;
* the `/dev`/`/proc`/`/sys` binds and the `lstat` hand-off are on the overlay;
  the chroot/exec is unchanged.

### The result (reports 243 and 244)

Two packed containers were flashed, each in one bootrom session from the booted
Nura root (`bbusb.py flash --signed … --from-bootrom --password 0000 --apply`,
then `reboot -f` from the `ttyGS0` shell):

* `both-0xE85217E5-overlay.signed` (sha256 `c3ed1f61…`) — the overlay root,
  boot `7.3.3 #67`;
* `both-0xA8F0F30C-sgx.signed` (sha256 `3be2c73a…`, CRC `0x9CD674DA`) — the
  same with `&sgx_module` enabled and `pvrsrvkm_omap4_sgx540_120.ko` in hd3,
  boot `7.3.3 #68`; the **current** media.

Both reported `Connected in bootrom`, `Model ID: 0x06001A06`, the ramloader
upload, both runs, `COMPLETE 0x40C0 -> cmd 0x4006`, `done`, `loader exit: device
released`.  The unit boots:

```
overlay / overlay rw,relatime,lowerdir=/rootfs,upperdir=/overlay/upper,workdir=/overlay/work,uuid=on
EXT4-fs (loop0): mounted filesystem aed244ce-… ro without journal
```

**No `Card stuck`, no journal abort.**  The first boot read `BOOTCTR_FCTRY =
0x1d` and `/init` wrote `0x32` (`PASS`); the second read `0x31` and correctly
wrote nothing (`0x31 >= 0x1e`), so the counter is **`0x31`**.  `rc-status`:
`elogind`, `tinydm`, `dbus`, `networkmanager`, `wpa_supplicant`, `bluetooth`,
`iio-sensor-proxy` started; `/run/user/10000` + `catacomb-wayland-1.sock` +
`wayland-1` exist; `catacomb` and `squeekboard` run; the Munin touch is ready
(irq 110); the backlight is 255; `wlcore: firmware booted (Rev 7.3.5.0.98)`.

**The screen is black with catacomb's empty bar** because the GL path is not
there yet: `MESA: error: ZINK: failed to load libvulkan.so.1` /
`smithay … syncobj … ENOTSUP` on the pre-SGX boot, and on the SGX boot the
kernel driver is loaded but the **userspace** stack is not (§0e).  The missing
DE apps (`swayidle`, `tabula`, `epitaph`, `chatty`) are a separate, cosmetic
gap.

### Generals that carried over

* The `ttyGS0` shell prompt is `playbook:~#`; `tools/pbcon.py` matches the old
  `pb> ` and **does not drive it** (trap 274) — use a raw send/read helper.
* `--from-bootrom` uploads the ramloader; the unit must be **booted**, the
  flasher started first, then `reboot -f` opens the 60 s bootrom window (trap
  266).  A success is `COMPLETE 0x40C0 -> cmd 0x4006` + `done`.
* If a flash ever fails with `0xF7 -> cmd 0x15`, the gate rejected (nothing
  written); check the runs and the `--from-bootrom` timing.  If left in the
  ramloader, `bbusb.py reset-loader`.
* `ttyACM0` may come back `root:uucp`; `pkexec chmod 666 /dev/ttyACM0`.

## 0e. SGX540 — the kernel half is DONE; the userspace is the remaining piece

### Kernel side (session 55, report 244) — working

The driver is the **active** `openpvrsgx-devgroup/linux_openpvrsgx` branch
**`pvrsrvkm-7.3`** (`https://github.com/openpvrsgx-devgroup/linux_openpvrsgx`,
HEAD `101c687ac73c384e1ae1a0c50cc40efd6e1f6ba9`) — **not** the old
`tmlind` fork the pvrports APKBUILD pins.  It carries DDK **1.17.4948957**.

* the tree is `drivers/gpu/drm/pvrsgx` (Kconfig, Makefile, `pvr-drv.c/h`, and
  `1.17.4948957/eurasia_km`, **358 files / 6.3 MB**);
* `drivers/gpu/drm/Kconfig` sources the pvrsgx Kconfig, the Makefile adds
  `obj-$(CONFIG_SGX) += pvrsgx/`;
* config: `CONFIG_SGX=y`, `CONFIG_PVRSGX_1_17_4948957=y` (+ABI),
  `CONFIG_SGX_DRM=y`, `CONFIG_SGX_OMAP=m`; the pvrsgx Makefile builds
  `SOC=omap4 SGX=sgx540 SGX_REV=120` → `pvrsrvkm_omap4_sgx540_120.ko`;
* **one kernel export is required**: `EXPORT_SYMBOL(__vmalloc_node_range_noprof);`
  in `mm/vmalloc.c` (the devgroup branch does the same; without it modpost
  fails);
* the board DTS `&sgx_module` is `status = "okay"` (the `omap4.dtsi` node
  supplies `gpu@0` with `ti,omap4430-gpu` / `img,powervr-sgx540`, IRQ
  `GIC_SPI 21`, 32 MB, `prm_gfx`, the L3_GFX clock).

Result on the unit (boot `#68`):

```
insmod /lib/modules/7.3.3/kernel/pvrsrvkm_omap4_sgx540_120.ko
pvrsrvkm 56000000.gpu: Enabling quirks 00000001
[drm] Initialized pvr 1.17.4948957 for 56000000.gpu on minor 1
```

`/dev/dri/card1` + `renderD129` exist; `uevent` reports `DRIVER=pvrsrvkm`,
`/ocp/target-module@56000000/gpu@0`.  catacomb stayed up.

### Userspace — DONE for the GPU (session 55, report 245)

The working stack, installed on the live overlay for the test:

* **`libglvnd` 1.6.0-r2** — the EGL/GLES dispatch (`libEGL.so.1`,
  `libGLESv2.so.2`).
* **`mesa-pvr-dri-classic` 21.3.9-r1** — the `amber-pvr` Mesa: `libEGL_amber`,
  `libgbm.so.1`, `/usr/lib/xorg/modules/dri/pvr_dri.so`, and the libglvnd
  vendor JSON `50_amber.json`.
* **`sgx-ddk-um` 1.17.4948957-r1** + **`sgx-ddk-um-ti443x`** +
  **`sgx-ddk-um-openrc`** — `pvrsrvinit`, `libsrv_init`, `libsrv_um`, `libusc`,
  `libGLESv1/2_PVR_MESA`, `libpvr2d`, `libpvr_dri_support`, `pvrsrvctl`.
* **`gcompat` 1.1.0-r4** (+ `musl-obstack`, `libucontext`) for the glibc blobs,
  and **`wayland-libs-server` 1.26.0-r0**.
* `MESA_LOADER_DRIVER_OVERRIDE=pvr` selects the DRI (`/etc/profile.d/mesa.sh`,
  or a tinydm `env-wayland.d` file).

The packages are **prebuilt** at
`https://pvrports.antonialoytorrens.com/pvrports/v23.12/armv7/` and also build
from source in the edge chroot (`pmbootstrap build --arch armv7
mesa-pvr-dri-classic`, with `pkexec` for root — the host has no passwordless
sudo).

**Result:** `pvrsrvinit` → `PVR_K: UM DDK-(4948957) and KM DDK-(4948957) match.
[ OK ]`; a headless surfaceless EGL/GLES2 probe
(`local/work/egltest.py`, `MESA_LOADER_DRIVER_OVERRIDE=pvr`) →
`GL_VENDOR = Imagination Technologies`, **`GL_RENDERER = PowerVR SGX 540`**,
`GL_VERSION = OpenGL ES 2.0 build 1.17@4948957`.

**Not usable with catacomb:** smithay requires `GL_EXT_unpack_subimage`, which
the SGX540 GLES2 driver does not expose; catacomb panics at renderer creation
(`src/udev.rs:400`).  pvrports supports Phosh (patched phoc) and Plasma; a
wlroots DE is the other candidate.

**The install is baked into hd3** (`85b44deb…`): the nine APKs, the module in
`/etc/modules` + `modules.dep`, the `sgx-ddk-um` service in the default
runlevel, and `MESA_LOADER_DRIVER_OVERRIDE=pvr`.  Verified on a fresh boot
(module autoloads, service starts, `UM/KM DDK match`, EGL probe OK).

**The patch re-export is pending a packaging decision:** the driver adds 358
files to a patch that is currently 63 files / 8 998 lines.  Either commit it in
the main patch or vendor the driver archive under `kernel/` with a documented
copy step.

### Reference material

The reference's own QNX SGX userspace (`libsrv_um.so`, `libusc.so`, GLESv1/v2,
EGL) is in `local/work/1917/base/usr/lib/graphics/omap4430/` with
`gpu-string = SGX540rev120`, but it is QNX ABI — **there is no separate SGX
firmware file anywhere in the reference**.  Its reference devices are the
**Samsung espresso** (OMAP4430 + TWL6030).  **Blobs stay gitignored in
`local/`.**

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

* The unit is on the **Nura overlay root** (image `0xA8F0F30C`, `7.3.3 #68`),
  writable via a tmpfs upper, the lower read-only; the ACM is `0fca:8010`,
  `ttyACM0` 0666; **no sshd**; **`usb0` DOWN**.
* **The Nura `ttyGS0` shell prompt is `playbook:~#`, not `pb> `** — so
  `tools/pbcon.py` reports "no prompt" and sends nothing (trap 274).  Use a raw
  send/read helper (`tools/pbraw.py` style).  `tools/pbssh.sh` is for the
  **region-1** root, not the Nura.
* **Reboot:** send `reboot -f` over the raw ACM (BusyBox; `-f` skips the
  shutdown).  **The flash:** start the flasher first, then the reboot; one
  bootrom session; the 60 s window.
* **`BOOTCTR_FCTRY`** is **`0x31`** (the two session-55 boots read `0x1d` →
  `/init` wrote `0x32`, then `0x31` → no write).  `dmesg | grep pbrpmb` gives
  the record and the top-up line.  A logged read, not an RPMB access
  (`SAFETY.md` §2).
* **The SGX check:** `insmod
  /lib/modules/7.3.3/kernel/pvrsrvkm_omap4_sgx540_120.ko` → `pvrsrvkm
  56000000.gpu: Enabling quirks 00000001`, `[drm] Initialized pvr …
  minor 1`; `ls /dev/dri` shows `card1`/`renderD129`; `uevent` says
  `DRIVER=pvrsrvkm`.
* **The eMMC checks:** `dmesg | grep -E 'Card stuck|Aborting|EXT4|overlay'`;
  the overlay on `/`; the lower `ro without journal`.
* **hd3 checks:** `dd` the superblock at `0x49000000`; mount from the running
  Linux with `losetup -o`; the lower is never written, so a read-back is safe.
* **WLAN checks:** `lsmod` shows `wlcore`/`wl12xx`/`mac80211`/`cfg80211` and
  `wlcore: firmware booted (Rev 7.3.5.0.98)`; `wlan0` is UP.  The Nura root has
  NetworkManager, so association is a separate item.
* **BT checks:** `BT_EN` at pad `0x4A10007C` mux 3 and `GPIO2_DATAOUT` bit 23;
  UART2 pads `0x4118`/`0x411A`; then `rootfs/bt/`'s tools.  **`btdump` first** —
  it hexdumps and cannot hide a reply.  Check `cat /proc/tty/ldiscs` before
  concluding "silent".  After a cold boot **export `gpio535` first** (trap 252).
* WM8994: `i2ctransfer` args are DECIMAL (trap 166); bus 4, addr `0x1a`.
* QNX device tools build `-static`; `/tmp` is wiped by a reboot.

## 4. Traps added last session (full list 14–284 in `KNOWN-ISSUES.md` §2)

* **272.** The overlay root is confirmed: no eMMC stall, `/run/user/10000`, and
  `catacomb` running; the boot is repeatable.
* **273.** A black screen with catacomb's empty bar is the **GL path**, not the
  root; check the renderer before re-flashing.
* **274.** The Nura prompt is `playbook:~#`; `pbcon.py` matches `pb> ` and does
  not drive it.
* **275.** The SGX driver is the **openpvrsgx-devgroup** `pvrsrvkm-7.3` branch
  (not the `tmlind` 6.1 fork); DDK 1.17.4948957; it needs
  `EXPORT_SYMBOL(__vmalloc_node_range_noprof)` in `mm/vmalloc.c`.
* **276.** The driver builds nine SoC variants (`pvrsrvkm_omap4_sgx540_120` is
  ours); package only that one, stripped with `--strip-debug`.
* **277.** The SGX DDK blobs are **glibc/armhf**; the Nura root is musl, so
  `gcompat` and the patched Mesa (`mesa-pvr-dri-classic`, `pvr` override) are
  required for anything to draw.
* **278.** `BOOTCTR_FCTRY` tops up only **below** `0x1e`; a boot at `0x31`
  correctly writes nothing.  Read the value, don't assume `0x32`.
* **279.** The vendor SGX `pvrsrvctl`/DDK blobs **do not run** under a
  hand-installed `gcompat` 1.1.0 on this musl root (`pvrsrvctl --help` →
  segfault, rc 139).  Build the userspace the pvrports/Alpine way.
* **280.** The pvrports prebuilt stack (`v23.12/armv7`) installs on the edge
  root and **replaces mainline Mesa**; it needs `gcompat` + `wayland-libs-server`.
* **281.** The SGX540 works headless: EGL/GLES 2.0,
  `GL_RENDERER = PowerVR SGX 540`, `GL_VERSION = OpenGL ES 2.0 build
  1.17@4948957` (`local/work/egltest.py`, surfaceless).  `es2_info` insists
  on X and is useless here.
* **282.** catacomb panics on `GL_EXT_unpack_subimage` — a compositor
  requirement, not a GPU failure; use Phosh/phoc or a wlroots DE.
* **283.** The pvr stack is **baked into hd3 offline**: loop-mount the image,
  `apk --root … --arch armv7 --allow-untrusted --no-network --no-scripts
  --repositories-file /dev/null --repository <dir with armv7/APKINDEX.tar.gz>
  add …`, then `depmod -b`, `/etc/modules`, the runlevel link and `e2fsck -fn`.
  Image `85b44deb…`; hd3-only flash (`--component --dest-block 0x4915`).
* **284.** After the bake, a **black panel with the backlight on is expected**
  (catacomb is the only compositor and panics); the display and GPU are fine.
  Do not `dd` `mmcblk1` for a checksum — it is an unnecessary eMMC read on the
  live overlay (it caused a visible flicker).
* Still current: **266** (`--from-bootrom` uploads the ramloader, boot first),
  **267** (the pack coordinate is block minus `0x815`), **268** (the `ttyGS0`
  shell), **269** (the overlay flags), **270** (the module vermagic), **271**
  (the hd3 image edits), **265** (`CONFIG_DYNAMIC_DEBUG`/`OVERLAY_FS`), 264
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
| **the active plan (SGX: bake + compositor)** | `docs/project/ROADMAP.md` item 7a step 5, reports **243/244/245** |
| **the SGX kernel driver** | `local/kernel-tree/drivers/gpu/drm/pvrsgx/` (source `openpvrsgx-devgroup/linux_openpvrsgx` branch `pvrsrvkm-7.3`, HEAD `101c687a…`) |
| **the working pvr userspace APKs** | `local/work/apks-pvr/` (pvrports `v23.12/armv7` + `gcompat` chain + `wayland-libs-server`) |
| **the EGL/GLES probe** | `local/work/egltest.py` (Python ctypes, surfaceless) |
| **the overlay root code** | `initramfs/init.c` (`rootfs_mount`/`overlay_mount`) |
| **the packed both-halves container** | `local/work/nura/images/both-0xA8F0F30C-sgx.signed` |
| **the hd3 images** | `local/work/nura/images/blackberry-playbook-root{-hd3,-overlay,-overlay-sgx}.img` |
| **the region-0 image (on media)** | `local/images/kernel-0xA8F0F30C-sgx.img` |
| flasher (region 0, region 1, **hd3**) | `tools/bbusb.py` |
| the module staging | `local/work/modules-7.3.3/` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| BT service pack | `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` |
| reference SGX userspace | `local/work/1917/base/usr/lib/graphics/omap4430/` |
| OSS player / mixer / BT probe | `rootfs/pbplay.c`, `rootfs/pbmix.c`, `rootfs/hci.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| reports | `docs/reports/` (**245** current) |
| sessions | `docs/sessions/session-55.md` |

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
PlayBook (OMAP4430 "winchester") — session 56 (absolute; Linux-era 50)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a AND item 9, KNOWN-ISSUES.md §2 (traps 14-282,
especially 266-282) and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md
(61-78), docs/reports/INDEX.md, reports 242-245, and sessions/session-55.md.

WHERE IT STANDS: audio (headphones + both speakers, 48 kHz), the touch, the
sensors/charger, ssh over USB, the display and the read-only hd3 + OVERLAYFS
Nura root all work.  The WL1283 WLAN associates and passes traffic on the
region-1 root; the Nura root now carries its firmware and wlcore boots it.  The
WL1283 BT core works (a real BR/EDR inquiry; the reference's WL1283.bts 315/315
makes hci0 register with mgmt) with the controller open parked on the stray 0x30
byte (trap 250).

THE OVERLAY ROOT IS DONE (session 55, report 243): region 0 + hd3 were written
in ONE bootrom session (COMPLETE 0x40C0 -> cmd 0x4006, done).  The unit boots
7.3.3 #68 on "overlay / ... lowerdir=/rootfs, upperdir=/overlay/upper", the lower
"ro without journal", NO eMMC stall and NO journal abort.  elogind, tinydm and
CATACOMB run; /run/user/10000 and the catacomb Wayland socket exist; the Munin
touch is ready; the backlight is 255.  The screen is black with only catacomb's
empty bar because the GL path is not there yet.

THE SGX540 WORKS (session 55, reports 244/245): the openpvrsgx-devgroup driver
(branch pvrsrvkm-7.3, DDK 1.17.4948957) is in the tree at drivers/gpu/drm/pvrsgx
(it needs EXPORT_SYMBOL(__vmalloc_node_range_noprof) in mm/vmalloc.c and
&sgx_module enabled), and insmod binds it.  The pvrports userspace (libglvnd,
mesa-pvr-dri-classic 21.3.9, sgx-ddk-um 1.17.4948957 ti443x, with gcompat and
wayland-libs-server) then initializes it: pvrsrvinit logs "PVR_K: UM
DDK-(4948957) and KM DDK-(4948957) match. [ OK ]", and the headless EGL/GLES2
probe (local/work/egltest.py, MESA_LOADER_DRIVER_OVERRIDE=pvr) reports
GL_RENDERER = PowerVR SGX 540, GL_VERSION = OpenGL ES 2.0 build 1.17@4948957.

THE ACTIVE ITEM (operator): a COMPOSITOR that works on the SGX540 GLES2 driver.
The GPU stack is done and BAKED into hd3 (image 85b44deb...: the nine APKs,
module autoload, the sgx-ddk-um service, MESA_LOADER_DRIVER_OVERRIDE=pvr) and
verified on a fresh boot; region 0 is 0xA8F0F30C.  catacomb panics on
GL_EXT_unpack_subimage (trap 282), so the panel is black with the backlight on.
pvrports supports Phosh (patched phoc) and Plasma; a wlroots DE is the other
candidate.  Blobs stay gitignored in local/.  The reference's QNX SGX userspace
(graphics.conf: gpu-string = SGX540rev120) is QNX ABI and there is NO separate
SGX firmware file in it.

THE FACTS: region 0 = 0xA8F0F30C (0x967880 B, sha256 fb9905f3..., kernel #68,
&sgx_module enabled); hd3 = 85b44deb... (the overlay image + the pvr userspace
stack + the module, with /etc/modules, the sgx-ddk-um service and the pvr
tinydm env), mounted read-only at boot; region 1 = 49e318bf... (the fallback,
not booted); BOOTCTR_FCTRY = 0x30 (the last boots read 0x31, 0x30 and did not
top up; the threshold is < 0x1e).  The kernel/module vermagic is
7.3.3 mod_unload modversions ARMv6 p2v8.

PHYSICAL STATE: the unit is on the Nura overlay root (0fca:8010, console
playbook:~# on the ACM /dev/ttyACM0, no sshd); usb0 is DOWN on this root.  To
boot the ORACLE: flash local/work/ifs-shim-sealed.ifs at 0x825.  To stock
TabletOS: that IFS at 0x825 + local/work/rcfs-1917.rcfs at 0x8D5 (+ the vendor
UFS if hd3 was repurposed).  /tmp is wiped by a reboot.  Do NOT restore
local/images/ifs0.sealed.bin (trap 84).

BENCH: the Nura ttyGS0 prompt is playbook:~#, NOT pb> " — tools/pbcon.py does
not drive it (trap 274); use a raw send/read helper (tools/pbraw.py style).
tools/pbssh.sh is for the REGION-1 root, not the Nura.  REBOOT: send `reboot -f`
over the raw ACM (BusyBox; -f skips the shutdown).  THE FLASH: start the flasher
first, then the reboot; one bootrom session; the 60 s window; --from-bootrom
UPLOADS THE RAMLOADER, so the unit must be BOOTED (trap 266).  ttyACM0 is 0666;
if it comes back root:uucp, pkexec chmod 666 /dev/ttyACM0.  An agent NEVER writes
RPMB; NVRAM read-only.  Read dmesg UNFILTERED; do not clear it without saying so.

BT: BT_EN pad 0x4A10007C mux 3 + GPIO2_DATAOUT bit 23; UART2 pads
0x4118/0x411A; /dev/ttyS1.  After a cold boot EXPORT gpio535 first and pulse low
2 s (trap 252).  ALWAYS check `cat /proc/tty/ldiscs` — a stale N_HCI eats the
bytes and the chip looks dead.  To swap a module: rmmod btbcm THEN hci_uart, and
verify by content (trap 251).  WL1283 checks: `i2ctransfer -f -y 0 w1@0x48 0xbc
r3` -> 07 01 55; `lsmod` must show ccm and ctr; `ip link set wlan0 up` ->
firmware Rev 7.3.5.0.98.  WM8994: i2ctransfer args DECIMAL, bus 4 addr 0x1a;
debugfs is not mounted by rcS.  On the oracle use pbi2c rd16/wr16; push
memdump3/memw32/pbi2c to /tmp after every boot.

PARKED: BT controller open (trap 250), LE (refused by the controller, but the
chip reports BT 4.0 with the LE bit — no known enable sequence, impossibility NOT
demonstrated), FM (forward-port drivers/media/radio/wl128x, removed in 6.13),
GPS (2024 LKML patch, else RE the reference).

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
