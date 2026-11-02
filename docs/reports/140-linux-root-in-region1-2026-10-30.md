# 140 — A Linux root filesystem in media region 1, and its `/init` running

**Session 18 (absolute), Linux-era session 12.  2026-10-30.**
Image: **`0xFB3A9F5D`** (`local/images/kernel-0xFB3A9F5D-rootfs.img`) — the
`0xFC77CD47` kernel/DTB with an initramfs that mounts the region-1 root
read-only, runs its `/init`, and a console `rootfs` command.
Root: **`local/images/rootfs-region1.ext4`**, 32 MiB, sha256 `0b87c29f…`.

This closes the handoff's first task: **a multi-MB Linux root reaches the eMMC
without the ACM console**, through the pre-OS `0xF7` loop.  It also proves the
other half: the kernel mounts it read-only through a loop device and executes
code from it.

---

## 1. Result

| question | answer |
|---|---|
| Where does the root live? | media **region 1** (`/dev/hd2`), region-1 offset **16 MiB** = raw **`0x29000000`**, loader block **`0x2915`** |
| How did it get there? | `tools/pre_os_ifs_flash.py --payload local/images/rootfs-region1.ext4 --dest-raw 0x29000000` (pre-OS `0xF7`, 2055 frames) |
| What is it? | ext4, 32 MiB, **no journal**, label `pbroot`, built by `tools/build_rootfs.py` from `rootfs/`; a static ARM `/init` plus `/etc/pbroot-version` |
| Is the media exact? | **yes** — read back over the door (QNX `dd`) sha256 `0b87c29f…` = the image; from Linux `blk sumb 0x29000000 33554432` = `c2e78e1f09b50208` = the host FNV-1a64 |
| Does it mount? | **yes** — `EXT4-fs (loop0): mounted filesystem 5b10c0de-… ro without journal` |
| Does its init run? | **yes** — `PB-ROOTFS: /init is running from the region-1 ext4 image (Linux 6.15.11)` |
| Is it writable? | **no, by design.**  `LO_FLAGS_READ_ONLY`, `MS_RDONLY`, ext4 `noload`, no journal.  The approved policy is read-only |
| Did region 0 survive? | **yes** — sealed IFS `c72b0530…` and the RCFS head both re-verified afterwards; `BOOTCTR_FCTRY = 0x32` |

Boot log (the full `kmsg`, operator request):
`local/logs/boot-20261030-063824.log`, 1452 lines / 67,902 bytes.  Key lines:

```
EXT4-fs (loop0): mounted filesystem 5b10c0de-0000-4b00-8000-000000000018 ro without journal.
PB-ROOTFS: /init is running from the region-1 ext4 image (Linux 6.15.11)
```

(`grep` needs `-a`: the log contains one non-text `/dev/kmsg` record — the known
anomaly in report 136 §5.)

---

## 2. Why region 1

Region 0 is full (the IFS zone is 11.25 MB and the image is 11.17 MB), and an
FS-level `hd3` shrink is dead (report 138).  Region 1 is the only pre-partitioned
512 MB space inside the loader's `0x1B` allow-list, so it needs **no MCT edit**
(reports 98 §4, 134 §6).  Its first sector is a QNX boot sector with one 500 MB
type-`0xB3` partition whose contents were operator-confirmed test data; the
Linux root starts 16 MiB in, past the region's own boot sector/partition table.

The honest note: the region-1 `t179` partition's data from 16 MiB on was
overwritten by this root.  The operator confirmed it was prior test data, and a
full pre-write snapshot is kept in
`local/backups/region1-pre-session18.img` (512 MiB, sha256 `5993096d…`).

---

## 3. The root image

`tools/build_rootfs.py` (new):

* compiles `rootfs/init.c` statically with the workspace ARM toolchain;
* stages `/init` and `/etc/pbroot-version`, fixes their timestamps;
* `mke2fs -t ext4 -b 1024 -O ^has_journal -m 0 -U <fixed uuid> -d <stage>`,
  then `debugfs sif` sets root ownership (the build is unprivileged);
* prints the sha256, the destination, and the exact flash command.

`has_journal` is off on purpose: the console/init mount is read-only, and a
journal-free filesystem cannot replay anything on a read-only mount.  The mount
also passes `noload` (belt and braces).

`rootfs/init.c` reads `/etc/pbroot-version` and writes one line to `/dev/kmsg`
and one to `/dev/ttyGS0`.  Reaching it proves the whole chain.

---

## 4. The initramfs side

`initramfs/init.c` gains `rootfs_try()`: after the gadget is bound, it takes a
free loop device, attaches `/dev/mmcblk1` with `LO_FLAGS_READ_ONLY` at
`0x29000000`, mounts ext4 `MS_RDONLY` with `noload` on `/rootfs`, and forks
`/rootfs/init` with a 5 s alarm.  A missing image is logged, not fatal.

`initramfs/console.c` gains `rootfs [off-hex]`, the ext4 sibling of `qnx6`,
sharing one `loop_mount_ro()` helper.  On demand:

```
$ rootfs
rootfs: /dev/loop1 (ext4 region-1 root) over /dev/mmcblk1 at 0x29000000 mounted read-only on /rootfs
$ ls /rootfs
etc/  init  lost+found/
$ cat /rootfs/etc/pbroot-version
pbroot 0.1 (session 18): first Linux root in media region 1
```

---

## 5. The container correction that made it possible

The first attempt (report 139) was rejected `0x11` (V2-only container) and then
misrouted into region 0 because the flasher patched only the V2 `rrcq` while the
loader uses the **V1 `qcfp`** record with a **region-relative** offset.  The
fixed builder patches both:

| record | offset | count |
|---|---|---|
| V1 `qcfp.run[0]` | `dest - 0x815` = **`0x2100`** | 512 |
| V2 `rrcq` | `dest - 0x805` = **`0x2110`** | 512 |

Both resolve to `0x2915`, so the write is correct whichever record a loader
prefers.  The host-side gate also refuses a region-1 destination below `0x2815`
and any raw offset that is not 64 KiB aligned.

---

## 6. Bench cycle

| # | action | result |
|---|---|---|
| — | TabletOS up, door up, `BOOTCTR_FCTRY = 0x32` | — |
| 1 | build rootfs (32 MiB) and image `0xFB3A9F5D` | sha256 `0b87c29f…`, CRC `0xFB3A9F5D` |
| 2 | `--payload rootfs-region1.ext4 --dest-raw 0x29000000` | 2055 frames, loader reset |
| 3 | boot TabletOS (sealed IFS still on region 0) | `dd if=/dev/hd2 …` = `0b87c29f…` **MATCH** |
| 4 | flash `0xFB3A9F5D` at block `0x825` | 689 frames, loader reset |
| 5 | boot Linux, `pkexec chmod 666 /dev/ttyACM0` | console up |
| 6 | `tools/pb_bootlog.sh` | `local/logs/boot-20261030-063824.log` (1452 lines) |
| 7 | `rootfs`, `ls /rootfs`, `cat`, `blk sumb` | mounted; content read; FNV `c2e78e1f09b50208` = host |
| 8 | `fb ff00ff` | framebuffer repainted (panel sanity) |
| 9 | restore `local/images/ifs0.sealed.bin` | 181 frames |
| 10 | final verification | IFS `c72b0530…`, RCFS head `1e04336b…`, region 1 `0b87c29f…`, `BOOTCTR = 0x32`, `startup.sh` 4490 B |

---

## 7. What this changes, and what is next

* **The bulk path is proven end-to-end without the console**: 32 MiB of root
  filesystem went through the pre-OS `0xF7` loop, was verified on media, was
  read back from Linux byte-exactly, and ran.  The ACM console remains a
  control/debug channel (trap 51); it is no longer on the critical path.
* **Region 1 is a working, read-only root home.**  The next decisions are the
  userland and writable-root strategy, not the transport.
* **Next**: put a real userland in the image (busybox or a postmarketOS/nura
  root), and decide between (a) a read-only root plus tmpfs/overlay, (b) a
  writable root in region 1 (which needs a new, non-SAFETY write path decision),
  or (c) `switch_root` into region 1 with the gadget brought up by the root's
  own init.  The portable part — a board with a working display, USB console and
  a 512 MB root home — is what a device port carries.
* **Still open**: the ACM link's ~134 KB/s and byte loss (report 136 §3.3), the
  DSS clock as a clock-initialisation property (report 133 §6), and the
  read-only-mount policy wording.

---

## 8. Residual

* The root is minimal: a static `/init` and one text file.  It proves mount and
  exec, not a usable userspace.
* Region 1's old `t179` data (16 MiB on) is gone; a pre-write snapshot exists.
* Region-0's IFS-zone padding still contains ext4 bytes from the report-139
  incident; it is unused (the IFS is `0x2BFE10`, inside the first 44 blocks).
* A cold reboot of the unit while on our image would still cost one
  `BOOTCTR_FCTRY`; the counter was topped up by the TabletOS boot after the
  sealed-IFS restore and reads `0x32`.
