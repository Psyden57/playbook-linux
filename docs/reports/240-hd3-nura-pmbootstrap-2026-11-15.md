# 240 — hd3/Nura: pmbootstrap builds the rootfs; a linux-* package wraps 7.3.3; split images produced

Date: 2026-11-15 (session 54, Linux-era 48).
Bench: image **`0xDD66D8C3`** + region-1 rootfs **`49e318bf…`**, unit on Linux,
ssh `169.254.0.1`, `BOOTCTR_FCTRY = 0x22`.
Status: **host-side experiment only.  No writes to the device, hd3, NVRAM, the
MCT or RPMB.**  The hd3 and region-0 images below are prepared but not flashed.

> **Correction (same session, report 241).**  The hd3 flash plan in §2 uses
> `--dest-block 0x4905`; the *host* gate passes that, but the **loader's** gate
> rejected it (`0xF7 seq 4 -> cmd 0x15 data 03`, nothing written).  The hd3
> filesystem starts at loader block **`0x4915`** (`0x4905` is the MCT entry's
> 1 MiB prefix; `(0x4915-0x15)*0x10000 = 0x49000000`).  The device flash used
> `0x4915`, wrote 626 065 440 B and verified `sha256 076bafcf…`.  Report 241 and
> trap 259 are current for the device step; the §2 dry-run below is the initial
> `0x4905` attempt.

## 0. The two questions, answered

**Question (a): will pmbootstrap build a rootfs for a device whose kernel it
does not own?**

**Yes — and then it refuses to finish without a kernel.**  A scaffolded
`device-blackberry-playbook` with `depends="postmarketos-base"` and
`deviceinfo_flash_method="none"` installs the base and the device package
normally, but `create_device_rootfs()` always calls
`pmb.chroot.initfs.build()`, and `mkinitfs` requires exactly one kernel release:

```
18:19:09 only one kernel release/flavor is supported, found: []
ERROR: Command failed (exit code 1): (rootfs_blackberry-playbook) % mkinitfs
```

`mkinitfs`'s `GetKernelVersion()` globs `/usr/share/kernel/*/kernel.release`
and errors unless the glob has exactly one entry (`internal/osutil/osutil.go`).
So the rootfs itself is kernel-agnostic, but the install pipeline is not.

**Question (b): can a Nura `linux-*` package wrap the 7.3.3 tree?**

**Yes, as an artifact package.**  `linux-blackberry-playbook` (7.3.3-r1)
installs the tree's build output in the layout Nura expects:

| path | content |
|---|---|
| `/boot/vmlinuz` | `local/kernel-tree/arch/arm/boot/zImage` (XZ, 6 693 704 B) |
| `/boot/dtbs/ti/omap/omap4-winchester.dtb` | our board DTB |
| `/usr/share/kernel/winchester/kernel.release` | `7.3.3` |
| `/lib/modules/7.3.3/kernel/*.ko` | **43 modules** + `modules.dep` and friends |

The device package then depends on it, `mkinitfs` and `boot-deploy` complete,
and the split images are produced.  This is a **prebuilt-artifact** wrapper, not
a source rebuild inside pmbootstrap: a production port would use the pristine
7.3.3 tarball + `kernel/patches/0001-playbook-winchester-7.3.3.patch` and
`pmbootstrap build --envkernel` (`helpers/envkernel.sh`); the packaging layout
is the same either way.

## 1. What was run

Host: CachyOS, pmbootstrap **3.12.1** (`main`, HEAD `e0dfe2c`), pmaports
`main` (**06c12d4**, dirty with the new port), Python 3.14.  `pkexec` was used
for the privileged chroot/loop work.

```
pmbootstrap init-equivalent config (no interactive init):
  device = blackberry-playbook, kernel = none, ui = catacomb,
  user = playbook, hostname = playbook, boot_size = 512
pmbootstrap install --zap --split --no-sshd --no-firewall --no-recommends \
  --password playbook
```

The full run took ~3 minutes of package installation plus ~10 s of package
builds and image filling.  464 packages are installed.

### The scaffolded port

`device/community/device-blackberry-playbook/`:

* `deviceinfo`: `arch="armv7"`, `dtb="omap4-winchester"`, `chassis="tablet"`,
  `drm="true"`, `flash_method="none"`, `generate_bootimg="false"`,
  `flash_kernel_on_update="false"`, 1024×600.
* `APKBUILD`: `depends="postmarketos-base linux-blackberry-playbook"`,
  `makedepends="devicepkg-dev"`, `devicepkg_build`/`devicepkg_package`.
* It deliberately has **no `heimdall-bootimg`-style flash method**: the PlayBook
  kernel is our CRC-sealed chainloader image and is written to region 0 by the
  pre-OS `0xF7` loop; the rootfs goes to hd3 and is mounted by our initramfs.

### The images

| image | size | sha256 | notes |
|---|---|---|---|
| `blackberry-playbook-root.img` | 625 999 872 B (597 MiB) | `d684e8fd…` | pristine pmbootstrap output; ext4 `pmOS_root`, 64bit/extents |
| `blackberry-playbook-root-hd3.img` | 625 999 872 B | `076bafcf…` | **flash-ready**: `/etc/fstab` `/boot` line removed, `ttyGS0` bench shell added |
| `blackberry-playbook-boot.img` | 536 870 912 B (512 MiB) | `9ba758b2…` | ext2 `pmOS_boot`; **unused** by our chainloader |

Root image internals verified on the host with `debugfs`/`e2fsck`:

* `/sbin/init` → `/bin/busybox`; `/etc/inittab` runs
  `/sbin/openrc sysinit|boot|default`.
* `/var/lib/tinydm/default-session.desktop` →
  `/usr/share/wayland-sessions/catacomb.desktop`, whose `Exec=catacomb`.
* `tinydm` is in the `default` runlevel; it has `need localmount`.
* `/usr/bin/catacomb` 1.5.0-r0, `postmarketos-ui-catacomb` 1-r0, `tinydm`
  1.3.1-r2, `mesa`/`mesa-egl` 26.2.6-r0, `networkmanager` 1.58.1-r1,
  `elogind` 255.24-r2, `postmarketos-base` 78-r0.
* `/usr/share/kernel/winchester/kernel.release` = `7.3.3`; 43 `.ko` files and
  `modules.dep` under `/lib/modules/7.3.3`.
* `/usr/lib/os-release` = `PRETTY_NAME="Nura edge"`, `ID=nura`.

### The two host-side fixes the image needed

1. **`/etc/fstab` `/boot` entry.**  pmbootstrap's split install writes
   `UUID=951d… /boot ext2 …`.  Our chainloader has no separate boot partition,
   and `tinydm` `need`s `localmount`; `localmount` runs `mount -at …` over fstab
   and would fail on the absent `/boot` device, blocking the DE.  The
   flash-ready image has that line removed (the root UUID line stays; `/` is
   already mounted by the initramfs and has pass 0).
2. **Bench console.**  The Nura `inittab` starts gettys on tty1–6 only.  The
   flash-ready image adds `ttyGS0::respawn:/bin/sh -l`, mirroring the current
   BusyBox rootfs, so the USB ACM console is a root shell on the new root.

### The region-0 image

`initramfs/init.c`'s `ROOTFS_RAW_OFF` moved **0x29000000 → 0x49000000** and the
stale region-1 wording was updated.  Rebuilt with the same 7.3.3 tree:

* zImage 6 693 464 B, DTB 96 465 B, `init` 595 604 B (cpio mtime 16:00).
* **image 0x939D5C bytes, CRC `0x8B709950`** (old: `0xDD66D8C3`, 0x939E4C).
* saved as `local/images/kernel-0x8B709950-hd3.img`, sha256 `835fa863…`.
* image < 0xB00000, so it fits the region-0 IFS-slot bound.
* The kernel patch is **unchanged** (61 files / 8 762 lines, sha256 `20556c5f…`);
  the change is initramfs userspace, not DTS/driver.

## 2. The hd3 dry-run

```
python3 tools/bbusb.py flash \
  --component local/work/nura/images/blackberry-playbook-root-hd3.img \
  --dest-block 0x4905
container 626 066 000 bytes, headersz 0x10020
write block 0x04905..0x06E54 (raw 0x48F00000..0x6E3FFFFF) offset 16624
dry run: stopping before USB
```

The run lies inside MCT type `0x1C` (`0x4905..0xFFFFF`), so the gate passes.
The printed raw start (`0x48F00000`) is the loader-block arithmetic; the
measured hd3 filesystem starts at **0x49000000** (report 138), and the write is
by loader block, so the flash is unaffected.  Verification after a write must
use `0x49000000`.

## 3. What is not done, and the risks that remain

* **No device write has been made.**  The hd3 flash is the destructive step
  (it overwrites the QNX userland; the UFS images are the way back).
* The Nura rootfs has **no sshd** (`--no-sshd`); bench access after the switch
  is the `ttyGS0` shell on the USB ACM console (and the panel/touch).
* The DE will run on **software rendering** until SGX540 is brought up
  (item 7a step 5).  Catacomb on wlroots/pixman should start, but this is
  unverified on the unit.
* `localmount`/`tinydm` are the first things to check in the boot log if the
  session does not start.
* Region 1 and the sealed IFS remain the fallback.  The old region-0 image
  (`0xDD66D8C3`) is on media now and should be read back **before** the new
  region-0 image replaces it.

## 4. Artefacts

* `local/work/nura/pmaports/device/community/device-blackberry-playbook/`
  (`APKBUILD`, `deviceinfo`) and
  `local/work/nura/pmaports/device/community/linux-blackberry-playbook/`
  (`APKBUILD`, the artifact tarball, `post-install`).
* `local/work/nura/images/blackberry-playbook-root.img`,
  `…-root-hd3.img`, `…-boot.img`.
* `local/images/kernel-0x8B709950-hd3.img` (sha256 `835fa863…`).
* `initramfs/init.c` (the only tracked source change).
* All of `local/` is gitignored; the port and images are not committed.
