# Handoff — session 55

**The one next step: a read-only hd3 + overlayfs Nura root, with the WLAN
firmware and no tty1 getty, flashed and booted to the DE.**  This is ROADMAP
item 7a's remaining work; SGX540 follows when the DE is up.  Everything below is
measured unless marked otherwise (reports 240/241, session 54).

## State you inherit

* **The unit is on the Nura root from hd3**, via our initramfs image
  **`0x060B25F2`** (region 0).  hd3's filesystem starts at loader block
  **`0x4915`** (raw `0x49000000`) — **not** the MCT entry `0x4905`, which has a
  1 MiB prefix and which the loader gate rejects.  hd3 was flashed byte-exactly
  (`sha256 076bafcf…`) and Nura boots (kernel 7.3.3, Nura edge, OpenRC, display,
  Munin touch).
* **The root is read-only after the eMMC write stall**: the first ext4
  journal/udev write gives `mmc1: Card stuck being busy! __mmc_poll_for_busy`,
  aborts the journal and ext4 remounts `emergency_ro`; `tinydm` fails and
  `catacomb` cannot get a libseat session.  A later 4 KiB write to hd3 returned
  `I/O error`; reads are fine.  The region-1 root (no journal) is stable.
* **Region 0 = `0x060B25F2`** (the lstat-fix image, sha256 `b548ec6c…`); **region
  1 = `49e318bf…`** intact and **not booted**; **hd3** holds the Nura root (the
  journal is dirty; the pristine host file is below).  **`BOOTCTR_FCTRY = 0x1e`**
  (the next boot spends one → `0x1d`; `/init` tops it to `0x32`).
* **Built, not flashed:** `local/images/kernel-0xE84693A1-quietpb.img`
  (`0x967A94` B, CRC `0xE84693A1`, sha256 `663348d0…`; the `PB*` prints are
  `pr_debug`, `CONFIG_DYNAMIC_DEBUG=y`, `CONFIG_OVERLAY_FS=y`).  The patch is
  **63 files / 8 998 lines**, sha256 `48c14a41…`.
* **The unit:** `0fca:8010`, console `playbook:~#` on `/dev/ttyACM0`, no sshd;
  `usb0` 169.254.0.1/16 (RNDIS; host 169.254.0.2/16).  The live `/var`/`/home`/
  `/tmp` tmpfs mounts are volatile.  No persistent device files were added.
* **The way back:** the 1917 IFS at block `0x825` (TabletOS), the region-1 root
  (the fallback), the saved `0xDD66D8C3` readback.  **No restore is required.**

## Inputs

| item | path / value |
|---|---|
| pristine hd3 root | `local/work/nura/images/blackberry-playbook-root-hd3.img` (625 999 872 B, sha256 `076bafcf…`; the fstab `/boot` line already removed, `ttyGS0` shell present) |
| firmware | `local/work/firmware-nvs/` — `ti-connectivity/wl128x-fw-4-sr.bin`, `wl128x-nvs.bin`, `wl128x-fw-5-sr.bin`, `regulatory.db`, `regulatory.db.p7s` (and the per-unit NVS if present) |
| modules | `local/work/modules-7.3.3/` — 43 modules from the `0xE84693A1` build (check vermagic) |
| initramfs | `initramfs/init.c` (add the read-only/overlay mount) |
| region 0 | `local/images/kernel-0xE84693A1-quietpb.img` (or rebuild from the tree) |
| flash tools | `tools/bbusb.py`; `debugfs` for the image edits; `pkexec chmod 666 /dev/ttyACM0` if the ACM is `root:uucp` |

## Procedure

1. **Copy the hd3 image** to a scratch file.  With `debugfs -w` on the copy:
   * remove the root `/etc/fstab` line (`/dev/root / ext4 …`) so the initramfs
     mounts the overlay, not the lower;
   * remove the `tty1` getty from `/etc/inittab` (the `tinydm`/elogind VT
     conflict);
   * add `local/work/firmware-nvs/*` under `/lib/firmware/ti-connectivity/` and
     `/lib/firmware/regulatory.db*`;
   * replace `/lib/modules/7.3.3/` with `local/work/modules-7.3.3/` (the new
     kernel's vermagic), and run `depmod` in a chroot or copy a generated
     `modules.dep`.
2. **`initramfs/init.c`:** mount hd3 read-only (`MS_RDONLY` + `noload`) at
   `/rootfs`; create `/overlay/{upper,work}` on a tmpfs; `mount -t overlay
   overlay -o lowerdir=/rootfs,upperdir=/overlay/upper,workdir=/overlay/work
   /newroot`; then the existing `/proc`/`/sys`/`/dev` binds and `chroot`.
   (Do not mount the lower read-write.  Keep the existing `lstat` hand-off.)
3. **Build region 0** with `build_kernel_image.py`; check the size is below
   `0xB00000` and save the image + CRC.
4. **Flash hd3 at `0x4915`** (dry-run first: it must print raw
   `0x49000000..0x6E4FFFFF`), then verify the device-side SHA-256 over
   152 832 × 4096 bytes equals the host image.  **Do not use `0x4905`.**
5. **Flash region 0** and boot.  At the console/dmesg check:
   * `dmesg | grep -E 'mmc1|EXT4|overlay|Card stuck|Aborting'` — no stall, no
     journal abort, `overlay` mounted;
   * `dmesg | grep pbrpmb` — `BOOTCTR_FCTRY`;
   * `rc-status` / the tinydm log; `/run/user/10000`; `/dev/dri/card0`;
   * `lsmod` / `wlcore` firmware load; the touch (evdev), the backlight.

## What each result means

* **Overlay mounts, Nura boots, `tinydm`/`catacomb` starts** — the read-only
  root fixed the eMMC stall and the elogind state is visible.  Check the DE
  (touch, backlight, Wi-Fi), then start SGX540 (ROADMAP item 9).
* **Overlay mounts, the DE still fails** — read the tinydm/elogind log and
  `/run/user/10000`; the likely remaining causes are the seatd/logind backend,
  the PAM stack, or the tty1 getty.  The root is writable-in-overlay, so a
  `seatd`/`elogind` test is now possible.
* **The eMMC still stalls** — the lower was not read-only (check the mount
  flags) or the card is in the failed state from the earlier boot; a cold power
  cycle + a fresh hd3 flash decides.  If it persists, the eMMC writable-root
  experiment is next (busy timeout / cache / no-journal), not the overlay.
* **The WLAN firmware loads but no link** — the NVS is the per-unit one; check
  `wlcore`/`wl12xx`, the regulatory domain, and the AP's 5 GHz `iw reg set`.
* **The hd3 flash fails / SHA mismatches** — the block is `0x4915`; the gate
  response `0xF7 -> cmd 0x15` means rejected (nothing written).  Re-check the
  dry-run's raw start before retrying.

## Safety / rules

* One variable per flash; hd3 is always a flasher-restorable image; region 1
  stays intact; the 1917 IFS at `0x825` is the way back (SAFETY §4).
* `h3` writes use loader block `0x4915`; the MCT entry `0x4905` is not the FS.
* Re-export the kernel patch after any DTS/driver change (tree walk + apply and
  compare; trap 211).
* No restore at wrap-up unless the operator wants TabletOS or a test image left
  the unit unbootable.

## Paste-ready kickoff

> Read `docs/project/HANDOFF.md`, `docs/sessions/session-54.md` and reports
> 240/241.  Do the session-55 experiment: the read-only hd3 + overlayfs Nura
> root with the firmware and no tty1 getty.  Start by reading `initramfs/init.c`
> and the pristine hd3 image; then edit the image, implement the overlay,
> rebuild region 0, flash hd3 at **0x4915** and region 0, and boot to the DE.
