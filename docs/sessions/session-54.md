# Session 54 (absolute) — hd3/Nura: pmbootstrap root built, hd3 flashed at 0x4915, Nura boots; the eMMC write stall blocks the DE

> Numbering: absolute `54` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 48** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-15.
Unit state at the end: **on our Linux initramfs image `0x060B25F2`** (region 0),
booting the **Nura** root from **hd3** (read-only: the ext4 journal aborted) at
loader block **`0x4915`**; region 1 **`49e318bf…`** intact and not booted;
`BOOTCTR_FCTRY = 0x1e`; console `playbook:~#` on the ACM (`/dev/ttyACM0`), no
sshd; `usb0` `169.254.0.1/16`; backlight on; **`tinydm` failed, so no DE**.  The
new quiet-print image **`0xE84693A1` is built but NOT flashed**.

## Goal of this session

The handoff's one experiment (ROADMAP item 7a): a host-side `pmbootstrap init` +
`install` with a scaffolded PlayBook port and `ui=catacomb`, split images, no
device writes, answering (a) can pmbootstrap build a rootfs for a device whose
kernel it does not own, and (b) can a Nura `linux-*` package wrap the 7.3.3
tree.  Then, if both hold, flash the root to hd3, verify it, re-point
`ROOTFS_RAW_OFF`, flash region 0, and boot the Nura DE.

## Confirmed (measured, read in a file, or seen on the bench)

* **The rootfs is built without pmbootstrap owning the kernel, but the install
  is not kernel-agnostic.**  `device-blackberry-playbook` (`flash_method="none"`,
  no kernel dep) installs `postmarketos-base`; `create_device_rootfs()` then
  runs `mkinitfs`, which requires exactly one `/usr/share/kernel/*/kernel.release`
  and fails `only one kernel release/flavor is supported, found: []`.  [report
  240 §0; `local/work/nura/work/log.txt`]
* **A Nura `linux-*` artifact package wraps the 7.3.3 tree.**
  `linux-blackberry-playbook` installs `/boot/vmlinuz`, the DTB,
  `/usr/share/kernel/winchester/kernel.release` and `/lib/modules/7.3.3`
  (**43 modules**); `mkinitfs`/`boot-deploy` complete.  [report 240 §0–§1]
* **The split root image is a 597 MiB bare ext4** (`pmOS_root`), Nura catacomb,
  **464 packages**, `/sbin/init` → BusyBox init → OpenRC, `tinydm` →
  `catacomb.desktop`; the 512 MiB boot image is unused.  [report 240 §1;
  `debugfs`]
* **The host-side hd3 flash plan at `0x4905` passed the host gate but the
  loader rejected it.**  The device flash stopped at `0xF7 seq 4 -> cmd 0x15
  data 03` (the gate `FUN_80100118`, "nothing written"); hd3's MBR/QNX6 magic
  were intact afterwards.  [report 98 §2.3; `0xF7` log]
* **The hd3 filesystem starts at loader block `0x4915`, not `0x4905`.**
  `(0x4915-0x15)*0x10000 = 0x49000000`; `0x4905` is the MCT entry's 1 MiB
  prefix.  The dry-run at `0x4915` shows raw `0x49000000`, and the flash wrote
  **626 065 440/626 065 440** bytes, signature + `COMPLETE 0x4006`.  [trap 259;
  report 241 §1]
* **The hd3 content is byte-exact.**  The device-side
  `sha256(dd@0x49000000, 152832×4096)` = **`076bafcf…`**, equal to the host
  `blackberry-playbook-root-hd3.img`; the ext4 magic `53 ef` is at
  `0x49000438`.  [report 241 §1; bench]
* **Nura boots from hd3.**  Console: `Linux playbook 7.3.3 #65 …`, prompt
  `playbook:~#`, `PRETTY_NAME="Nura edge"`; `omapdrm`/`fb0`, the Munin touch
  ready (irq 110), the Nura inittab/OpenRC.  [report 241 §2; capture
  `docs/captures/playbook-live-2026-11-15-hd3-nura-boot.txt`]
* **The first hand-off attempt failed on an absolute symlink.**  The ext4
  mounted (`EXT4-fs (loop0): mounted filesystem aed244ce…`), but
  `access("/rootfs/sbin/init")` followed the Nura `/sbin/init` → `/bin/busybox`
  **absolute** symlink inside the *initramfs*, where `/bin/busybox` does not
  exist, so the console fallback ran.  `lstat()` does not follow the final
  symlink; the rebuilt image `0x060B25F2` boots.  [report 241 §2; trap 260]
* **The eMMC write path stalled.**  `mmc1: Card stuck being busy!
  __mmc_poll_for_busy` at 21.376 s, `I/O error` on `mmcblk1`/`loop0`,
  `Aborting journal on device loop0-8`, ext4 `Remounting filesystem read-only`
  (`emergency_ro`), and a second stall at 45.756 s.  A later 4 KiB write to
  hd3 (outside the image) returned `I/O error`.  [capture; report 241 §3]
* **`tinydm` fails on the read-only root.**  `rc-status` `tinydm [failed]`;
  the live tmpfs `/var`/`/home`/`/tmp` + `/var/run -> /run` made `tinydm` start
  (`supervise-daemon`/`autologin` running) but `autologin` never created
  `/run/user/10000`, and a direct `catacomb` failed with `Unable to start
  libseat session: Ensure logind/seatd service is running with no active
  session`.  [report 241 §4; bench]
* **The Nura rootfs lacks the WLAN firmware.**
  `Direct firmware load for ti-connectivity/wl128x-fw-4-sr.bin failed with error
  -2` (and `wl128x-nvs.bin`); the pmbootstrap root was not given
  `local/work/firmware-nvs`.  [capture; report 241 §5]
* **The `PBSIO`/`PBC`/`PBINI`/`PBX`/`PBM`/`PBG`/`PBS`/`PB:`/`PBR` prints were
  unconditional `pr_info`.**  They flood the log per MMC/probe operation (trap
  52); they are now `pr_debug` (dynamic debug), with `PBREQ`/`PBIRQ` still
  behind the `pb_trace` module parameter.  `CONFIG_DYNAMIC_DEBUG=y` and
  `CONFIG_OVERLAY_FS=y` are enabled.  [report 241 §6; traps 264/265]
* **The kernel patch is re-exported and verified.**  **63 files / 8 998 lines**,
  sha256 `48c14a41…`, 0 rejects and 0 mismatches against pristine 7.3.3.
  [bench; `kernel/patches/0001-playbook-winchester-7.3.3.patch`]
* **New region-0 image `0xE84693A1`** (`0x967A94` B, sha256 `663348d0…`, below
  the `0xB00000` bound), saved as
  `local/images/kernel-0xE84693A1-quietpb.img`; **not flashed**.  The media
  still has `0x060B25F2` (`0x939D04` B, sha256 `b548ec6c…`).
  [`build_kernel_image.py`]
* **The factory counter is `0x1e`** (`pbrpmb … = 1e000000`, `>= threshold 0x1e,
  no write`).  The next boot spends one → `0x1d`; `/init` tops it back to `0x32`.
  [capture line 179]

## Inferred (reasoning from evidence, not directly measured)

* **The eMMC stall is fresh-NAND write/erase latency, not the `PB*` prints.**
  Medium-strong: the stall at 21.376 s precedes the `mmc1` rescan/prints; the
  region-1 no-journal root is stable; the stack wrote 597 MiB via the bootrom,
  and the first kernel rewrites (journal/udev) hit those fresh blocks.  A
  no-journal image alone may not help; the read-only root avoids the writes.
* **A read-only hd3 + overlayfs root should get the DE running.**  Medium: the
  overlay (now `CONFIG_OVERLAY_FS=y`) preserves the lower `/var`/`/etc`
  elogind/dbus/NetworkManager state while writes go to a tmpfs upper; the
  bind-tmpfs live test already let `tinydm`/`autologin` start, but hid the
  lower `/var/lib/elogind`, so no session was created.
* **The tinydm/elogind failure is a consequence of the read-only root + hidden
  `/var` state, plus the tty1 getty.**  Medium: the `autologin` stuck with no
  `/run/user/10000`; `catacomb` needs an active libseat session; the overlay
  plus removing the tty1 getty is the expected fix.
* **Adding `local/work/firmware-nvs` to the hd3 image will fix the WLAN
  firmware load.**  Strong: the exact files exist locally and the errors are
  `-2` (not found), not a driver/radio failure.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The hd3 flash destination is `0x4905`."**  Wrong.  `0x4905` is the MCT
  type-`0x1C` entry start, but the filesystem starts 1 MiB later at `0x4915`;
  the loader's gate rejected `0x4905` (`0xF7 seq 4 -> cmd 0x15 data 03`,
  nothing written).  The host dry-run/gate passed because it only checks the
  MCT extent.  `0x4915` mapped raw `0x49000000` and wrote/verified.
* **"`access("/rootfs/sbin/init")` is enough to detect the userland."**  The
  Nura `/sbin/init` is an absolute symlink to `/bin/busybox`; `access()`
  resolves it in the initramfs, so the ext4 mounted but the hand-off fell back
  to the console.  `lstat()` fixes it.
* **"A kernel-less device port is enough for `pmbootstrap install`."**  The
  rootfs builds, but `mkinitfs` requires a kernel release; a `linux-*` package
  is mandatory.  [trap 253]
* **"The kernel package's `modules.dep` survives the install."**  The kmod
  trigger left only `kernel/*.ko`; a `post-install` `depmod` is required.
  [trap 254]
* **"Changing the device package's `depends` rebuilds it."**  pmbootstrap
  reused the cached device package; a `pkgrel` bump/`build --force` is needed.
  [trap 257]
* **"`ui=none` is a neutral fast test."**  It selects systemd (no
  `postmarketos-ui-none`); `--no-firewall` then runs `systemctl preset` and
  fails.  [trap 258]
* **"The split `/boot` fstab line is harmless."**  `tinydm` needs
  `localmount`; the absent boot device blocks it.  [trap 255]
* **"The `PBSIO` prints cause the eMMC stall."**  The stall at 21.376 s
  precedes the rescan/prints; the prints are a consequence and a log/perf
  problem, not the trigger.  They are still removed/gated.
* **"The RNDIS TCP link can carry the log."**  ICMP/`ping` worked, but `nc` to
  the host listener returned 1 (the host/NM zone/firewall); the full dmesg was
  pulled over the ACM compressed (`gzip | base64`).

## Unknown / open questions

* **Does a read-only hd3 + overlayfs boot start the Catacomb session?**
  **Would settle it:** implement the overlay mount in `init.c`, reflash hd3
  (pristine + firmware + no tty1 getty) and region 0 (`0xE84693A1` or a rebuild),
  and read the tinydm log/`/run/user/10000`.
* **Can the eMMC sustain a writable root with a longer busy timeout / cache
  off / a no-journal image?**  **Would settle it:** a driver timeout/cache
  experiment on a reflashed pristine hd3; the fresh-block erase latency and the
  card's health decide.
* **Is the eMMC's write failure after the stall permanent, or a card state a
  cold boot/reflash clears?**  **Would settle it:** a cold power cycle followed
  by a bootrom reflash of hd3 and a small kernel write test.
* **Which modules must be updated for the `0xE84693A1` kernel?**  The staged 43
  were rebuilt/stripped, but the pmbootstrap hd3 image still carries the older
  modules from `playbook-kernel-7.3.3.tar.gz`; a `pkgrel` bump + reinstall (or
  an in-image update) is needed.
* **Why does the 5 GHz WLAN first-auth sometimes time out?**  (session-52
  open; unchanged.)
* **What is the exact `elogind`/`pam_elogind`/`/run/user/10000` failure?**
  **Would settle it:** the overlay root, the tty1 getty removed, and the
  `tinydm`/`autologin` log; the PAM stack.

## Superseded / uncertain

* **The session-53 handoff/ROADMAP/report 239 and the host-side report 240 say
  the hd3 flash is at `--dest-block 0x4905`.**  That is the MCT entry; the
  filesystem is at **0x4915**.  The loader gate rejected 0x4905; 0x4915 wrote
  and verified.  `SAFETY.md` §4 already said the FS start is 0x4915.  Report
  241 and trap 259 are current; report 240 is corrected by report 241.
* **The captures README calls the `PBR:`/`PBSIO:`/`PBC:` lines "the project's
  own driver-binding/bus traces" (pre-existing).**  They were unconditional
  `pr_info`; they are now `pr_debug` (dyndbg); `PBREQ`/`PBIRQ` remain behind
  `pb_trace`.  The history is in report 241 §6 and trap 264.
* **The Nura rootfs is not a drop-in for the BusyBox root:** it needs the WLAN
  firmware, the read-only/overlay decision, and the `ttyGS0`/tty1 console
  choices; its `/etc/fstab` `/boot` line was removed.

## Artefacts produced

* `docs/reports/240-hd3-nura-pmbootstrap-2026-11-15.md` — the host-side
  experiment (pmbootstrap rootfs, kernel wrapper, split images, dry-run).
* `docs/reports/241-hd3-flash-nura-boot-emmc-stall-2026-11-15.md` — the device
  result (0x4915 flash/verification, Nura boot, lstat fix, eMMC stall, DE/WLAN).
* `docs/captures/playbook-live-2026-11-15-hd3-nura-boot.txt` — the full first
  Nura boot `dmesg` (1 746 lines; the eMMC stall, journal abort, WLAN firmware
  errors, `BOOTCTR_FCTRY = 0x1e`).
* `kernel/patches/0001-playbook-winchester-7.3.3.patch` — **63 files / 8 998
  lines**, sha256 `48c14a41…`, 0 rejects / 0 mismatches; the `pr_debug` PB
  prints (omap_hsmmc, mmc core/host/ops, base/dd, gpio-omap, ti-sysc,
  arm/mm/mmu) and the existing session changes.
* `kernel/winchester-7.3.3.config` — adds `CONFIG_DYNAMIC_DEBUG=y`,
  `CONFIG_OVERLAY_FS=y` (and the resolved deps).
* `initramfs/init.c` — `ROOTFS_RAW_OFF = 0x49000000` and the `lstat` hand-off
  check.
* Images (host): `local/images/kernel-0xE84693A1-quietpb.img`
  (`0x967A94` B, CRC `0xE84693A1`, sha256 `663348d0…`, **not flashed**);
  `kernel-0x060B25F2-hd3.img` (`0x939D04` B, sha256 `b548ec6c…`, **the media's
  region 0**); `kernel-0x8B709950-hd3.img` (`0x939D5C`, sha256 `835fa863…`);
  `kernel-0xDD66D8C3-readback.img` (the old known-good, sha256 `06e61587…`).
* hd3 image (host): `local/work/nura/images/blackberry-playbook-root-hd3.img`
  (625 999 872 B, sha256 `076bafcf…`; pristine, fstab `/boot` removed, `ttyGS0`
  shell; the root fstab line still present); `…-root.img` (`d684e8fd…`),
  `…-boot.img` (`9ba758b2…`).
* pmbootstrap port: `local/work/nura/pmaports/device/community/
  device-blackberry-playbook/` and `…/linux-blackberry-playbook/`.
* `local/work/modules-7.3.3/` — 43 modules refreshed/stripped from the new
  build.
* Traps **253–265**; DECISIONS **§74/§75**; the ROADMAP item 7a update.
