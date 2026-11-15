# 241 — hd3 flash at 0x4915, Nura boots, and the eMMC write stall

Date: 2026-11-15 (session 54, Linux-era 48).
Bench: unit on the Linux initramfs image **`0x060B25F2`** (region 0) + the Nura
root on **hd3** (read-only, journal aborted); region 1 **`49e318bf…`** intact;
`BOOTCTR_FCTRY = 0x1e`; console `playbook:~#` on `/dev/ttyACM0`, no sshd.
Capture: `docs/captures/playbook-live-2026-11-15-hd3-nura-boot.txt` (1 746
lines of `dmesg`).

## 0. Headline

1. **The hd3 filesystem starts at loader block `0x4915`, not `0x4905`.**  The
   host dry-run at `0x4905` passes the MCT gate, but the loader's own gate
   rejects it (`0xF7 seq 4 -> cmd 0x15 data 03`, nothing written); `0x4915`
   maps raw `0x49000000`, writes **626 065 440/626 065 440** bytes, and the
   device-side SHA-256 equals the host image (`076bafcf…`).
2. **Nura boots from hd3** (kernel 7.3.3, Nura edge, OpenRC, the display and the
   Munin touch), once the initramfs's `access("/rootfs/sbin/init")` is replaced
   with `lstat()` — the Nura `/sbin/init` is an **absolute** symlink and the
   pre-chroot check resolved it inside the initramfs.
3. **The first ext4 journal/udev write stalls the eMMC** (`mmc1: Card stuck
   being busy! __mmc_poll_for_busy`), aborts the journal and remounts the root
   read-only; `tinydm` then fails and the DE does not start.  The Nura rootfs
   also lacks the WLAN firmware.  A read-only hd3 + overlayfs root (now
   `CONFIG_OVERLAY_FS=y`) is the next step.

## 1. The flash: 0x4905 is the MCT entry; the filesystem is at 0x4915

The first attempt used the handoff's `--dest-block 0x4905`:

```
building container: …-root-hd3.img -> block 0x4905 (Delta 16624)
write block 0x04905..0x06E54 (raw 0x48F00000..0x6E3FFFFF) offset 16624
PreFlash 0x20EE (21) -> cmd 0x20DE len 4
0xF7 seq 4 -> cmd 0x15 data 03
```

The `0xF7` response `cmd 0x15`, `data 03` is the gate
`FUN_80100118` returning 0 (report 98 §2.3, "nothing written").  The unit
booted the old region-0 image; hd3's MBR (`eb 10 90 …`) and QNX6 magic
(`22 11 19 68 = 0x68191122`) were intact.  The four acknowledged `0xF7` frames
were the **header** (QCFM `0x10020` bytes), not writes.

The measured hd3 filesystem start is **raw `0x49000000`** (report 138; the QNX6
superblock at `0x49002000`).  Under the loader's 64 KiB rule
`raw = (block - 0x15) * 0x10000`, that is **block `0x4915`**; the MCT type-`0x1C`
entry `0x4905` carries a **1 MiB prefix**.  The dry-run at `0x4915`:

```
building container: …-root-hd3.img -> block 0x4915 (Delta 16640)
write block 0x04915..0x06E64 (raw 0x49000000..0x6E4FFFFF) offset 16640
```

The flash ran **626 065 440/626 065 440** bytes and returned
`0x40F9 signature -> cmd 0x4006`, `COMPLETE 0x40C0 -> cmd 0x4006`, `done`.
The unit booted the old region-0 image; from its Linux:

* `dd@0x49000000`, first 16 bytes = `00 …`; ext4 magic `53 ef` at
  **`0x49000438`**; superblock head `c0 2a 01 00 00 55 02 00 …` (inode count
  76 480, block count 152 832).
* `sha256(dd if=/dev/mmcblk1 bs=4096 skip=(0x49000000/4096) count=152832)` =
  **`076bafcfbb59b08a9fa397eec90a5b52d0cb96098fb80eb9589e2917fdbfd428`** =
  `local/work/nura/images/blackberry-playbook-root-hd3.img`.  The write is
  byte-exact.

The old region-0 image was read back before the new flashes and verified:
CRC `0xDD66D8C3`, `end-8` `1f2dc8d7`, size `0x939E4C`; saved as
`local/images/kernel-0xDD66D8C3-readback.img` (sha256 `06e61587…`).

## 2. The Nura boot and the lstat hand-off

The first hd3-pointing region-0 image (`0x8B709950`, `ROOTFS_RAW_OFF =
0x49000000`) mounted hd3 but fell back to the `pb> ` initramfs console.  The
full dmesg (`0x060B25F2`, with console loglevel 3) shows:

```
EXT4-fs (loop0): recovery complete
EXT4-fs (loop0): mounted filesystem aed244ce-021b-4bd8-9031-05c4a56dd213 r/w
…
PB: rootfs: no /sbin/init, keeping the initramfs console
```

The root **had** `/sbin/init`; it is a symlink to `/bin/busybox` (an absolute
path).  The pre-chroot `access("/rootfs/sbin/init")` followed that absolute
target **inside the initramfs**, where `/bin/busybox` does not exist, so the
check failed.  `lstat()` does not follow the final symlink; `init.c` now uses
it, and image **`0x060B25F2`** boots:

```
Linux playbook 7.3.3 #65 Sun Nov 15 16:22:36 -03 2026 armv7l Linux
playbook:~#
PRETTY_NAME="Nura edge"
```

The kernel/DTB/display are ours: `omapdrm 1.0.0` → `fb0`; the DSI PLL forced
(`clkin=38400000 … clkdco=604800000`); `playbook-munin 1-0065: Munin
touchscreen ready (irq 110)`.  The Nura root mounts read-write; `/var`, `/home`,
`/tmp` are on the ext4; `/run` is tmpfs.

## 3. The eMMC write stall

At 21.376 s, on the first udev/journal writes:

```
mmc1: Card stuck being busy! __mmc_poll_for_busy
I/O error, dev mmcblk1, sector 0 op 0x1:(WRITE) flags 0x800800 …
Buffer I/O error on dev mmcblk1, logical block 0, lost async page write
I/O error, dev loop0, sector 543592 op 0x1:(WRITE) flags 0x809800 …
Aborting journal on device loop0-8.
EXT4-fs error (device loop0): ext4_journal_check_start:94: comm udevd: Detected aborted journal
…
EXT4-fs (loop0): Remounting filesystem read-only
```

A second stall at 45.756 s repeats the pattern.  After it, a 4 KiB write to a
scratch hd3 area (raw `0x70000000`, outside the image) returned
`dd: /dev/mmcblk1: I/O error`; `/sys/block/mmcblk1/ro` is 0.  The card is in a
failed write state, while reads (the whole root, the earlier sha256) work.

The trigger is the **first rewrite of freshly bootrom-written NAND** (the ext4
journal/udev), whose erase/program latency exceeds the MMC busy poll; it is
not the `PB*` prints (the stall precedes the `mmc1` rescan and the prints are a
consequence).  The region-1 rootfs has no journal and is stable.  The
workaround is a **read-only hd3** (no eMMC writes); `CONFIG_OVERLAY_FS=y` is now
built in so a tmpfs upper can hold the writable state.

## 4. The DE: `tinydm` / `elogind` / `libseat`

`rc-status` says `tinydm [ failed ]`; the mount is `ext4 (rw,relatime,
emergency_ro)`.  In a live test the read-only root was overlaid with tmpfs on
`/var`/`/home`/`/tmp` and the standard `/var/run -> /run`, `/var/lock ->
/run/lock` symlinks recreated.  `tinydm` then started (`supervise-daemon
tinydm … /usr/bin/autologin -- playbook tinydm-run-session`), but
`/run/user/10000` never appeared and `/home/playbook/.local/state/tinydm.log`
was not created.  A direct `catacomb` (as root, `WLR_RENDERER=pixman`,
`WLR_BACKENDS=drm,libinput`, `XDG_RUNTIME_DIR=/run/user/0`) failed:

```
catacomb::udev: [error] Unable to start libseat session: Ensure logind/seatd
service is running with no active session
```

The bind-tmpfs `/var` hides the lower `/var/lib/elogind`/dbus state, so no
session is created; the `tty1` getty also owns the VT.  The overlayfs root (with
the lower `/var`/`/etc` visible) plus removing the tty1 getty is the expected
fix; the full elogind/PAM detail is still open.

## 5. The WLAN firmware gap

```
wl12xx_driver wl12xx.1.auto: Direct firmware load for
  ti-connectivity/wl128x-nvs.bin failed with error -2
wl12xx_driver wl12xx.1.auto: Direct firmware load for
  ti-connectivity/wl128x-fw-4-sr.bin failed with error -2
wlcore: ERROR could not get firmware ti-connectivity/wl128x-fw-4-sr.bin: -2
wlcore: ERROR firmware boot failed despite 3 retries
```

The pmbootstrap root was not given `local/work/firmware-nvs`
(`ti-connectivity/wl128x-fw-4-sr.bin`, `wl128x-nvs.bin`,
`wl128x-fw-5-sr.bin`, `regulatory.db`, `regulatory.db.p7s`).  This is a
packaging omission, not a driver/radio failure.

Other non-fatal messages: `wm8994-codec: Failed to request Mic1/Mic2 … IRQ:
-22` (the mic-detect IRQs are not wired; playback is unaffected),
`Warning: unable to open an initial console` (the kernel's `/dev/console`
before the initramfs), and `urandom` warnings before `crng init done`.

## 6. The quiet-PB kernel, config and image

The general bring-up instrumentation was unconditional `pr_info`:
`PBSIO`/`PBINI`/`PBX`/`PBM` in `omap_hsmmc.c`; `PBC` in
`mmc/core/{core,host,mmc_ops}.c`; `PBR` in `drivers/base/dd.c`; `PBG` in
`gpio-omap.c`; `PBS` in `ti-sysc.c`; `PB:` in `arch/arm/mm/mmu.c`.  All are
now `pr_debug` (dynamic debug; enable with `dyndbg`), except `PBREQ`/`PBIRQ`,
which stay behind the `pb_trace` module parameter.  `CONFIG_DYNAMIC_DEBUG=y`
and `CONFIG_OVERLAY_FS=y` are enabled.

The patch export: **63 files / 8 998 lines**, sha256 `48c14a41…`, 0 rejects and
0 mismatches against pristine 7.3.3.  The new region-0 image is `0x967A94` B,
**CRC `0xE84693A1`**, sha256 `663348d0…`, saved as
`local/images/kernel-0xE84693A1-quietpb.img`; it is **not flashed** (the media
has `0x060B25F2`).

## 7. Next

1. Implement the read-only hd3 + overlayfs root in `initramfs/init.c`
   (`lowerdir=/rootfs`, a tmpfs upper/work, `noload` on the lower).
2. Build a fresh hd3 image from `blackberry-playbook-root-hd3.img`: remove the
   root `/etc/fstab` line (the overlay root), remove the `tty1` getty, add
   `local/work/firmware-nvs` under `/lib/firmware`, and update the 43 modules
   from `local/work/modules-7.3.3` (or bump the pmbootstrap kernel package).
3. Flash hd3 pristine at **`0x4915`**, flash region 0 (`0xE84693A1` or the
   overlay build), boot; read `/run/user/10000`, the `tinydm` log, the DRM, the
   touch, and `lsmod`/`wlcore`.
4. If the DE runs, SGX540 (item 7a step 5).  If a writable root is required,
   investigate the eMMC busy/erase timeout (driver timeout, cache, no-journal)
   as a separate experiment.

## 8. Artefacts

* This report; the capture; the session-54 note.
* `kernel/patches/0001-playbook-winchester-7.3.3.patch` (63 files / 8 998 lines,
  sha256 `48c14a41…`); `kernel/winchester-7.3.3.config`
  (`CONFIG_DYNAMIC_DEBUG=y`, `CONFIG_OVERLAY_FS=y`); `initramfs/init.c`
  (`ROOTFS_RAW_OFF = 0x49000000`, `lstat`).
* Images: `local/images/kernel-0xE84693A1-quietpb.img` (`0x967A94`, not
  flashed), `kernel-0x060B25F2-hd3.img` (`0x939D04`, media),
  `kernel-0x8B709950-hd3.img`, `kernel-0xDD66D8C3-readback.img`; the hd3
  images in `local/work/nura/images/`.
* Traps 259–265; DECISIONS §74/§75.
