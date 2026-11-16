# Handoff — session 56 (absolute)

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real userland — now a
**Nura** root on hd3 with a **GPU-accelerated compositor**.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console and its RNDIS link, the whole display chain, the 64 MiB ext4
BusyBox root in region 1 (ssh + wireless), the peripherals (I²C4 motion
sensors, TMP103, BQ27541, FAN5702, CM3217, ISL9519), the **Munin touch**, the
**WM8994 audio** (headphones + speakers, 48 kHz), the **WL1283 WLAN** (associates
on the region-1 root), the **WL1283 BT core** (real inquiry; `hci0` registers),
the read-only hd3 + overlayfs **Nura root**, and now **catacomb on the SGX540**.

**THE COMPOSITOR WORKS (session 56, report 246).**  The SGX540 stack was never
the problem: smithay 0.7.0 hard-requires `GL_EXT_unpack_subimage`, which the
GLES **2.0** SGX540 lacks.  A smithay patch relaxes the check and implements the
per-row/full-row upload fallbacks; **catacomb 1.5.0** then composites on
`libGLESv2_PVR_MESA`/`libsrv_um`/`libusc`/`pvr_dri.so` (no `swrast`), and
`squeekboard` is composited.  **The touch Y axis was inverted** (physical top →
evdev `Y 577–591`, bottom → `Y 35–51`); `playbook-munin.c` now flips the mapped
Y, and the cursor tracks exactly.

**Open: performance and content.**  Tapping OSK keys still drops frames badly;
dragging is better but hitchy.  The v2 full-row partial-upload optimization did
not remove the key-press cost.  The UI is otherwise empty: the root was built
with `--no-recommends`, so `tabula` (background), `epitaph`, `chatty`,
`gnome-calls`, `swayidle` are missing.  **Next session: apps/background +
profile the upload path** (§"What is next" in `STATE.md`, ROADMAP 7a step 5).

## 0a. The media, and the way back

* **region 0 = `0xAB899C49`** (`0x9676A0` B, sha256 `c37ee2fe…`, kernel
  **`7.3.3 #69`**) — the overlay init with `&sgx_module` and the **Munin Y
  fix**; `boot/chainloader/kernel.img` holds the same image;
  `local/images/kernel-0xAB899C49-touch.img`.
* **hd3 = `89399744…`** (625 999 872 B; `local/work/nura/images/
  blackberry-playbook-root-compositor2.img`) — the pvr userspace + **catacomb
  1.5.0-r10000** (the smithay fallback), mounted **read-only** at boot.
* **region 1 = `49e318bf…`** (the BusyBox fallback, not booted).
* **BOOTCTR_FCTRY = `0x2c`** (the session-56 boots read `0x2d`, `0x2c`; both
  `>= 0x1e`, no write).  Read it with `dmesg | grep pbrpmb`.
* **The unit is on the Nura overlay root** (`0fca:8010`, console
  `playbook:~#` on the ACM `/dev/ttyACM0`, **no sshd**).  **`usb0` is down
  after boot** — bring it up by hand if a fast transfer is needed:
  `ip link set usb0 up; ip addr add 169.254.0.1/16 dev usb0`.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`, then reboot; `0fca:8012`, `PB_LINK=door tools/pbssh.sh`.  **To stock
  TabletOS:** that IFS at `0x825` + `local/work/rcfs-1917.rcfs` at `0x8D5`
  (+ the vendor UFS if hd3 was repurposed).  `/tmp` is wiped by a reboot.
  **Do NOT restore `local/images/ifs0.sealed.bin`** (trap 84).

## 0b. The WLAN — it associates (session 52, unchanged)

The association blocker was the missing `ccm`/`ctr` modules (`ieee80211_key_alloc`
needs a `ccm(aes)` AEAD; without it the PTK install is `-ENOENT`, reported as
`WRONG_KEY`).  Working sequence on the region-1 root (`/tmp` is wiped):

```sh
ip link set wlan0 up                 # rcS loads the stack, it does not raise it
iw reg set <CC>                      # e.g. AR — required for a 5 GHz AP
mkdir -p /var/run/wpa_supplicant
{ echo "disable_scan_offload=1"; wpa_passphrase '<SSID>' '<PSK>'; } > /tmp/wpa.conf
iw dev wlan0 scan >/dev/null 2>&1
wpa_supplicant -B -i wlan0 -D nl80211 -c /tmp/wpa.conf -C /var/run/wpa_supplicant
wpa_cli -p /var/run/wpa_supplicant -i wlan0 status   # want wpa_state=COMPLETED
```

The Nura hd3 image carries `ti-connectivity/wl128x-fw-4-sr.bin`,
`wl128x-fw-5-sr.bin` and `wl128x-nvs.bin`.  Wi-Fi is 1×1 HT20 (72 Mbit/s) —
**do not advertise HT40** (trap 247).  Do **not** use `IRQ_TYPE_LEVEL_LOW` for
WL_IRQ (trap 238), not `wlcore_sdio dump=1` (trap 239), not 5-sr (trap 243).

## 0c. BT — what works, and where it stopped

Over a plain H4 tty (`/dev/ttyS1`, 115200 8N1) the core answers `HCI_Reset`,
reports `hci ver 6` / `lmp ver 6` / `mfr 0x002b` (TI, BT 4.0), and runs a full
BR/EDR inquiry that finds a peer, reproducibly.  The reference's
`WL1283.bts` loads **315/315** and, loaded **before** the attach, makes `hci0`
**register with mgmt** (fixes the `0x201c` reply length).  **Parked blocker
(trap 250):** a stray `0x30` byte ~200 ms after the last init reply
(`-EILSEQ` → `SET_POWERED 0x05`); the origin is unknown.  The resume experiment
is in `STATE.md`.  LE is refused by the controller but the chip reports BT 4.0
with the LE bit — no known sequence, impossibility **not** demonstrated.
`BT_EN` = gpio 55; UART2 pads `0x4118`/`0x411A`; tools in `rootfs/bt/`.

## 0d. DONE — the read-only hd3 + overlayfs root (session 55)

hd3 is a **flasher-restorable ext4 image**; the eMMC is not written at boot.
`initramfs/init.c` opens `/dev/mmcblk1` `O_RDONLY`, uses a
`LO_FLAGS_READ_ONLY` loop, mounts the lower `MS_RDONLY`+`noload`, and overlays
a 256 MiB tmpfs upper (`/overlay`) at `/newroot`.  The boot has **no eMMC
stall, no journal abort**, and the session/seat work (`/run/user/10000`,
`elogind`, `tinydm`, `squeekboard`, catacomb).  The image has the root fstab
line and the tty1 getty removed, the WLAN firmware, the 43 modules, the pvr
stack and catacomb.

## 0e. SGX540 — catacomb works; the patch, the build, the export

**The driver (kernel):** `openpvrsgx-devgroup/linux_openpvrsgx`, branch
`pvrsrvkm-7.3`, HEAD `101c687a…`, DDK **1.17.4948957**, at
`drivers/gpu/drm/pvrsgx` (needs `EXPORT_SYMBOL(__vmalloc_node_range_noprof)` in
`mm/vmalloc.c`, `&sgx_module` enabled, `CONFIG_SGX=y`,
`CONFIG_PVRSGX_1_17_4948957=y`, `CONFIG_SGX_DRM=y`, `CONFIG_SGX_OMAP=m`).
`insmod` binds (`pvrsrvkm 56000000.gpu`, `card1`/`renderD129`).

**The compositor patch (smithay):** catacomb 1.5.0 pins smithay 0.7.0
(`dba255cd…`).  `src/backend/renderer/gles/mod.rs` hard-requires
`GL_EXT_unpack_subimage` (or GLES 3.0); the SGX540 has neither.  The patch
(1) replaces that error with a warning, (2) implements the upload fallbacks:
full tight → one `TexImage2D`; full padded → NULL alloc + per-row; partial
tight → **whole damaged rows** (one call per rect); partial padded → per-row;
`update_memory` → full-texture-layout rows.  The patch lives at
`local/work/nura/pmaports/main/catacomb/smithay-gles2-fallback.patch`
(sha512 `1c07cd03…` for v2).

**The build:** the Alpine `testing/catacomb` APKBUILD is copied into
`local/work/nura/pmaports/main/catacomb/` with `pkgrel=10000` and
`options="net !check"`, and `prepare()` applies the smithay patch to the
**fetched cargo git checkout**.  Build:

```bash
cd local/work/nura/pmbootstrap
sudo env HOME=$HOME XDG_CONFIG_HOME=$PWD/../config PYTHONPATH=$PWD \
  python3 pmbootstrap.py --as-root --no-cross --no-ccache \
  build --lax --arch armv7 catacomb
```

`--no-cross` because crossdirect refuses native rustc **1.100.0** against the
armv7 chroot's **1.99.1**; `--no-ccache` because `sccache` is absent from the
QEMU buildroot.  ~18–20 min with `!check`.  Artifact:
`local/work/nura/work/packages/edge/armv7/catacomb-1.5.0-r10000.apk`.

**The export (done, session 56):** `0001-playbook-winchester-7.3.3.patch` is
**64 files / 8 800 lines**, sha256 `014eb8a8…`; verified **0 rejects / 64/64
identical**.  The **366-file / 6.5 MB SGX driver is vendored** as
`kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` (606 964 B, sha256
`a9d04d3c…`; 362/362 identical after extraction).  `kernel/vendor/README.md`
has the source/commit/licence and the **copy step**: apply the patch, then
`tar -xJf … -C <tree>/drivers/gpu/drm`, then build.  The old patch's two
generated `scripts/mod/*.h` are gone.  **Re-run the tree walk after
`make clean`** (trap 211).

**The reference:** the QNX SGX userspace (`graphics.conf:
gpu-string = SGX540rev120`) is QNX ABI; there is **no separate SGX firmware
file** in it.  Blobs stay gitignored in `local/`.

## 0f. FM and GPS (parked)

**FM:** mainline carried `drivers/media/radio/wl128x` from 2.6.39 to 6.13 and
removed it; a forward port is the route.  **GPS:** no mainline driver; the
operator has a 2024 LKML patch to evaluate, and the reference's own path is the
fallback.

## 0g. Audio and touch

Audio: headphones and both speakers play `pbplay`'s 440 Hz tone at 48 kHz.
Listening tests: lower `pbmix set "DAC1 Volume" 40` **once**, restore `96` at the
end; a bare `pbplay` plays a 2 s tone (trap 212).  Touch: I²C2 `0x48072000` =
`i2c-1` at `0x65`, GPIO2_3 edge IRQ (`irq 110`); a **cold** boot leaves it down
until GPIO1_14/GPIO4_8 are pulsed, which the driver does not drive.  The
**Y inversion is fixed** in the driver (session 56).

## 0h. The minimal 1917 oracle

Debloated 2.1.0.1917 IFS+RCFS (`tools/build_minimal_qnx.py`), qconn on
`169.254.0.1:9090` + root ssh.  Its `/base` is pruned (no firmware dir).  No
wifi/BT service runs, so `ifconfig ti0 up` and the BT start are manual.  Tools
are not on the UFS — push `memdump3`/`memw32`/`pbi2c` to `/tmp` after every
boot.  **Bench only** (trap 173).

## 1. Useful local extracts

* `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` + `README.md` — the
  pinned SGX driver and its copy step.
* `local/work/nura/pmaports/main/catacomb/` — the APKBUILD + the smithay
  patch.
* `local/work/nura/work/packages/edge/armv7/catacomb-1.5.0-r10000.apk` — the
  built compositor.
* `local/images/kernel-0xAB899C49-touch.img`;
  `local/work/nura/images/blackberry-playbook-root-compositor2.img`;
  `both-0xAB899C49-compositor.signed`.
* `local/work/nura/images/` — the pristine `076bafcf…`, the pre-pvr
  `1fcf0cb0…`/`1f14915f…`, and the overlay images.
* `local/work/modules-7.3.3/` — the staged 43 modules;
  `local/work/firmware-nvs/` — the firmware tree.
* `rootfs/bt/` — the BT bench tools; `local/work/1917/` — the 1917 `/base`,
  the WLAN calibration and `WL1283.bts`; the reference SGX userspace under
  `…/usr/lib/graphics/omap4430/`.
* Reports **246** (this session), **243–245** (overlay + SGX), 242, 241, 240,
  238, 232–237.

## 2. Build and flash (7.3.3)

The kernel tree is `local/kernel-tree`; the tracked source copies are
`kernel/source/`, the patch is `kernel/patches/`, the pristine tarball is
`local/pristine/linux-7.3.3.tar.xz`.

```bash
REPO=$PWD; . tools/env.sh
K=local/kernel-tree
# after applying the patch to a pristine tree, copy the vendored driver:
tar -xJf kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz -C "$K/drivers/gpu/drm"
arm-linux-gcc -static -O2 -Wall -o initramfs/root/console initramfs/console.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/init    initramfs/init.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/pbrpmb  initramfs/rpmb.c
rm -f $K/usr/initramfs_data.cpio*
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 modules
ZIMAGE=$K/arch/arm/boot/zImage DTB=$K/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb \
    python3 tools/build_kernel_image.py     # prints the CRC; image must be < 0xB00000
```

Modules **in-tree** (`make … modules`), never `M=`; strip with
`--strip-debug`, never `strip -s`.  A module/firmware-only change keeps the
region-0 CRC (trap 190) and needs a region-1/hd3-only flash.  The hd3 image
edits are `debugfs -w`/`apk --root` on a copy, then `e2fsck -fn`.

**Pack and flash** (the coordinate is the loader block minus `0x815`:
`0x825`→16, `0x4915`→16640):

```bash
printf 'boot/chainloader/kernel.img=16,0\nlocal/work/nura/images/<hd3>.img=16640,0\n' > /tmp/both.lst
python3 tools/bbusb.py pack -l /tmp/both.lst -v 1,2 -s -f -c local/work/nura/images/<name>.signed
python3 tools/bbusb.py flash --signed local/work/nura/images/<name>.signed \
    --from-bootrom --password 0000 --apply
# hd3 only:
python3 tools/bbusb.py flash --component local/work/nura/images/<hd3>.img \
    --dest-block 0x4915 --from-bootrom --password 0000 --apply
```

Start the flasher first, then `reboot -f` over the raw ACM (trap 266); one
bootrom session; success is `COMPLETE 0x40C0 -> cmd 0x4006` + `done`.  Region
0 only when only it changed; both when both changed.

## 3. Bench

* The unit is on the **Nura overlay root** (region 0 `0xAB899C49`, `7.3.3
  #69`), writable via a tmpfs upper, the lower read-only; ACM `0fca:8010`,
  `ttyACM0` 0666; **no sshd**; **`usb0` down** after boot.
* **The Nura `ttyGS0` shell prompt is `playbook:~#`, not `pb> `** — `pbcon.py`
  does not drive it (trap 274).  Use a raw send/read helper; `pbssh.sh` is for
  the **region-1** root.
* **File transfer:** the host UFW blocks inbound, so push **host→device**: the
  device runs `nc -l -p 8765 > /tmp/f`, the host
  `python3 -c "import socket;…"` sends it (~10 MB/s).  The ACM drops bytes when
  blasted (trap 51).
* **Reboot:** `reboot -f` over the raw ACM; **the flash:** start the flasher
  first, then the reboot; the 60 s window.
* **The compositor:** `md5sum /usr/bin/catacomb` = `7418d9ea…`; `pgrep -a
  catacomb`; `tail -20 /home/playbook/.local/state/tinydm.log`;
  `catacomb msg cursor on` and the squeekboard `SetVisible` call show the
  debug UI.  **A restart over a wedged DSI does not recover — reboot.**
* **The touch:** raw events in `/dev/input/event0` (16-byte `input_event`);
  udev `ID_INPUT_TOUCHSCREEN=1`; `elogind` is a second, non-exclusive reader.
* **The eMMC/overlay:** `dmesg | grep -E 'Card stuck|Aborting|EXT4|overlay'`;
  the lower `ro without journal`.
* **hd3 checks:** `dd` the superblock at `0x49000000`; a read-back is safe.
* **WLAN/BT:** as §0b/§0c.  `WM8994`: `i2ctransfer` args DECIMAL, bus 4 addr
  `0x1a`.  QNX tools build `-static`; `/tmp` is wiped by a reboot.

## 4. Traps added last session (full list 14–291 in `KNOWN-ISSUES.md` §2)

* **285.** smithay hard-requires `GL_EXT_unpack_subimage`; the fallback patch
  (renderer creation, `import_shm_buffer`, `update_memory`) is the fix.
* **286.** catacomb/smithay are static; rebuild via the pmbootstrap package
  (crossdirect rust 1.100.0 vs 1.99.1 → `--no-cross --no-ccache`; `!check`).
* **287.** The controller's Y is inverted (top→~580, bottom→~35); the driver
  flips the mapped Y (kernel `#69`).
* **288.** The SGX driver is vendored (362 files, `a9d04d3c…`); the main patch
  is 64 files / 8 800 lines (`014eb8a8…`); the two generated `scripts/mod/*.h`
  were removed.
* **289.** Restarting the compositor over a wedged DSI gives `VC busy` /
  `if_enable` errors; a clean boot is required.
* **290.** OSK key presses still jank; the v2 full-row upload did not fix it;
  profile the stride/damage and `glFinish`/fencing.
* **291.** The Nura `usb0` is down after boot; the host UFW blocks inbound;
  push host→device.
* Still current: **266** (`--from-bootrom` uploads the ramloader), **267**
  (pack coordinate), **268/274** (the Nura shell/prompt), **269** (overlay
  flags), **270** (module vermagic), **271** (hd3 image edits), **275–284**
  (SGX), **265**, 264, 261, 260, 259, 253–258, **250**, **248**, **247**,
  **244**, **246**, **243**, **241**, **239**, **238**, **237**, **236**,
  **227**, **224**, **221**, **214**, **212**, **211**, **190**.

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **the active plan (apps + performance)** | `docs/project/ROADMAP.md` item 7a step 5, report **246** |
| **the compositor patch** | `local/work/nura/pmaports/main/catacomb/smithay-gles2-fallback.patch` |
| **the SGX kernel driver (vendored)** | `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` + `README.md` |
| **the kernel tree (source)** | `local/kernel-tree/drivers/gpu/drm/pvrsgx/` |
| **the export** | `kernel/patches/0001-playbook-winchester-7.3.3.patch` |
| **the config** | `kernel/winchester-7.3.3.config` |
| **the overlay root code** | `initramfs/init.c` |
| **the hd3 images** | `local/work/nura/images/` |
| flasher (region 0/1, **hd3**) | `tools/bbusb.py` |
| the module staging | `local/work/modules-7.3.3/` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| BT service pack | `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` |
| reference SGX userspace | `local/work/1917/base/usr/lib/graphics/omap4430/` |
| OSS player / mixer / BT probe | `rootfs/pbplay.c`, `rootfs/pbmix.c`, `rootfs/hci.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| reports | `docs/reports/` (**246** current) |
| sessions | `docs/sessions/session-56.md` |

## 6. Working rules (including the operator's explicit preferences)

* **Long builds are handed to the operator.**  For a long build, give the
  exact command and **stop the turn**; the operator runs it in their terminal
  (watching `local/work/nura/work/log.txt`) and comes back when it finishes or
  errors.  Do not sit and poll a long build.  Short checks/polls are fine.
* **`pkexec` is for the agent; the operator uses `sudo`.**  When handing a
  command to the operator, write `sudo`, not `pkexec`.
* **Explicit preferences go in this file, not the taste system.**  The taste
  system is deliberately off; it auto-adds and bloats the context.
* After every console command, dump the log tail and read it.  Read
  `dmesg`/`sloginfo` **unfiltered** after a reboot or structural change.
  **Don't clear `dmesg`** without saying so.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  A test boot is not a budget concern.
* **Never present a guess as a fact.**  Prefer a fresh read.
* **Use the reference before the bench** (DECISIONS §61).  Subagents:
  static/file-only (trap 109) — and **time-box them**.
* **Look up every external fact.**  Nura was renamed on 2026-09-27; search
  "Nura", and expect `postmarketos`/`pmaports` in package names.
* **Revert experiments and prove the revert.**
* **The flash:** `--from-bootrom` needs the unit **booted**, not the ramloader;
  one bootrom session; the pack coordinate is the loader block minus `0x815`;
  the 60 s window; `reboot -f` from the Nura `ttyGS0`.
* **The read-only hd3:** never mount it read-write at boot; writes live in the
  tmpfs upper; the lower is `MS_RDONLY`+`noload`.
* **The kernel export:** re-export when the DTS or a driver changes; apply the
  patch + extract the vendor driver; verify by apply-and-compare; run the tree
  walk after `make clean` (traps 161/211).
* **Only the operator decides when to stop / wrap up.**

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 57 (absolute; Linux-era 51)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a AND item 9, KNOWN-ISSUES.md §2 (traps 14-291,
especially 285-291) and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md
(61-80), docs/reports/INDEX.md, report 246, and sessions/session-56.md.

WHERE IT STANDS: audio (headphones + both speakers, 48 kHz), the touch (Y axis
fixed), the sensors/charger, ssh over USB, the display, the read-only hd3 +
OVERLAYFS Nura root, and now CATACOMB ON THE SGX540 (the smithay GLES2
fallback).  catacomb 1.5.0-r10000 runs on libGLESv2_PVR_MESA; squeekboard is
composited; the touch cursor tracks.  The UI is otherwise empty (no wallpaper/
apps) and OSK key presses still jank.

THE ACTIVE ITEM (operator): APPS + BACKGROUND for real testing, then profile
the upload path.  The GPU stack is done and baked into hd3 (image 89399744...:
catacomb r10000, the pvr userspace, the module, the service, the pvr tinydm
env); region 0 is 0xAB899C49 (#69, the Munin Y fix).  The smithay patch is at
local/work/nura/pmaports/main/catacomb/smithay-gles2-fallback.patch.  The
kernel export is done: 64 files / 8 800 lines, sha256 014eb8a8..., with the
362-file SGX driver vendored at kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz
(sha256 a9d04d3c...; the copy step is in kernel/vendor/README.md).  Blobs stay
gitignored in local/.

THE FACTS: region 0 = 0xAB899C49 (0x9676A0 B, sha256 c37ee2fe..., kernel #69);
hd3 = 89399744... (the overlay image + the pvr stack + catacomb r10000, mounted
read-only at boot); region 1 = 49e318bf... (the fallback, not booted);
BOOTCTR_FCTRY = 0x2c.  The kernel/module vermagic is 7.3.3 mod_unload
modversions ARMv6 p2v8.

PHYSICAL STATE: the unit is on the Nura overlay root (0fca:8010, console
playbook:~# on the ACM /dev/ttyACM0, no sshd); usb0 is DOWN after boot (bring
it up by hand; the host UFW blocks inbound, so push host->device).  To boot the
ORACLE: flash local/work/ifs-shim-sealed.ifs at 0x825.  To stock TabletOS: that
IFS at 0x825 + local/work/rcfs-1917.rcfs at 0x8D5 (+ the vendor UFS if hd3 was
repurposed).  /tmp is wiped by a reboot.  Do NOT restore
local/images/ifs0.sealed.bin (trap 84).

BENCH: the Nura ttyGS0 prompt is playbook:~#, NOT pb> — pbcon.py does not drive
it (trap 274); use a raw send/read helper.  pbssh.sh is for the REGION-1 root.
REBOOT: reboot -f over the raw ACM.  THE FLASH: start the flasher first, then
the reboot; one bootrom session; the 60 s window; --from-bootrom UPLOADS THE
RAMLOADER, so the unit must be BOOTED (trap 266).  ttyACM0 is 0666.  An agent
NEVER writes RPMB; NVRAM read-only.  Read dmesg UNFILTERED.

BT: BT_EN pad 0x4A10007C mux 3 + GPIO2_DATAOUT bit 23; UART2 pads
0x4118/0x411A; /dev/ttyS1.  After a cold boot EXPORT gpio535 first and pulse low
2 s (trap 252).  ALWAYS check `cat /proc/tty/ldiscs`; swap a module with
`rmmod btbcm` THEN `hci_uart`, verify by content (trap 251).  WL1283 checks:
`i2ctransfer -f -y 0 w1@0x48 0xbc r3` -> 07 01 55; `lsmod` must show ccm/ctr.
WM8994: i2ctransfer args DECIMAL, bus 4 addr 0x1a.  On the oracle use
pbi2c rd16/wr16; push memdump3/memw32/pbi2c to /tmp after every boot.

PARKED: BT controller open (trap 250), LE, FM (forward-port
drivers/media/radio/wl128x), GPS (2024 LKML patch, else RE the reference).

LISTENING TESTS: lower `pbmix set "DAC1 Volume" 40` ONCE, restore 96 ONCE; a
bare `pbplay` plays a 2 s tone (trap 212); state what is playing (trap 191).

BUILDS: modules IN-TREE, strip `--strip-debug`.  The hd3 image edits are
debugfs/apk on a copy, then e2fsck -fn.  The pack coordinate is the loader
block minus 0x815.  The kernel export: apply the patch + extract the vendored
driver, apply-and-compare, tree walk after `make clean`.  For a LONG build,
give the operator the command (with `sudo`) and STOP THE TURN; do not poll.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both in one bootrom session only when both changed; a
module/firmware-only change needs a region-1/hd3-only flash (trap 190).  The
hard hazards (0 / >0x32 / failed write / flat pack) remain.  The read-only hd3
is never mounted read-write.  Operator's call when to stop / wrap up.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  Re-export the kernel patch
whenever the DTS or a driver changes (trap 161); verify apply-and-compare and
run the TREE WALK (trap 211, filtering Kconfig.*).  A reverted experiment still
has to be proven reverted.

WRAP-UP: only the operator decides when the session stops.  Record the bench
state, write the session note, update the canonical docs, verify with
tools/docs_audit.py and tools/tests/test_offline.py, then commit privately and
publish the snapshot (docs/project/REPO-MANAGEMENT.md).  No IFS restore is
required.
```
