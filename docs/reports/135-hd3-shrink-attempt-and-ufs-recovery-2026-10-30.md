# 135 — The hd3 shrink attempt, why it failed, and the UFS recovery

**Session 16 (absolute), Linux-era session 10.  2026-10-29/30.**
Status: **the shrink was reverted.**  The unit is back on the full 58 GB
TabletOS UFS, door up, `BOOTCTR_FCTRY = 0x32`.  The shrink's failure mode is
recorded and **not explained**; do not retry it without a diagnosis.

This report is the durable record of an experiment that was approved by the
operator, executed carefully, and backed out.  It also documents the `bb10mt`
UFS recovery procedure and a newly found Linux eMMC **write** defect.

---

## 1. What was attempted

Goal: free ~54 GB of the 58 GB `hd3` (TabletOS root, QNX6) for a Linux root,
without touching the MCT.

1. **Backup** the whole root with QNX GNU `tar -cSf` (excluding the
   pseudo-filesystems) to the host: **372,408,320 B**, 3,927 entries, extracted
   365 MB.  Verified by hashing five files on both sides, including a 55 MB
   binary and root-only files — all matched.
2. **Map the filesystem** with a new on-device oracle
   (`tools/qnx6-usage.c`): blocksize 4096, `num_blocks` 15,273,980,
   `free_blocks` 15,128,701 → **145,279 used blocks ≈ 595 MB**, 800,000 inodes,
   3,777 live.  (The data is scattered up to ~55 GiB; irrelevant for a
   reformat+restore, which compacts it.)
3. **Build a 2 GiB QNX6 image on the device** without touching the live FS:
   create a 2 GiB file, serve it with `devb-loopback` (`/dev/lo1`),
   `mkqnx6fs -b 4096`, mount it, and `tar -xSf` the backup into it.
   Result: 446 MB used, 1.5 GiB free; sample hashes **and numeric ownership**
   (`apps:nto`, `root:nto`, `accounts/1000`) matched the live filesystem.
4. **Copy the image to the host** (2,147,483,648 B, sha256
   `96e14d47…cc213f`) and prove the round trip.
5. **Write it to `hd3` from QNX**: `sync; mount -ur /; dd of=/dev/hd3
   bs=1048576` streamed from the host; read back the first 2 GiB and hash it —
   byte-identical to the image.
6. Reboot.

**Result: the unit booted as a brand-new device** (setup screen), USB product
`0fca:8012`, no door, no SSH.  The new filesystem was there and mounted, but the
system behaved as a first install.

## 2. What was ruled out

The backup and the new image were checked for the state chain the operator
named:

* `accounts/devuser/startup.sh` — present (4490 B in the tar).
* `radio/scripts/env.sh` — present (774 B, the runlevel watcher that launches
  `post-boot.sh`).
* `.seed/var/pps/system/navigator/config` — present (352 `.seed` entries).
* `var/pps/system@0.0.775/navigator@0.0.775/config@89.0.660` — present, content
  identical to the mounted UFS source.

So the "new install" behaviour was **not** a missing file or a skipped backup.

> **Update, report 137 (newer):** the cause is identified as a startup race, not
> an identity/serial effect.  Setup mode is `autorun::1` in
> `/pps/system/navigator/config`, appended by `startup.sh`'s `pps_common()` when
> `/pps/system/settings` has no `lang_countryCode`.  `pps -l 2` loads the
> persistent store asynchronously and startup waits only for `/pps`, so a slow
> first boot can lose the race; the restored data itself was correct.  This is
> inferred and not yet captured on a boot.  The candidate list below is
> superseded.

The earlier candidates — the recreated filesystem's identity/serial, the
installer/PPS state, or a first-boot marker not on `hd3` — are **superseded by
report 137**.  The retry preconditions are in report 137 §5.

## 3. A separate, real defect: the Linux eMMC write path

While trying to use the Linux initramfs as the offline writer (a new `recv`
console command), the device wrote at **~17–32 KB/s**.  The kernel log shows
why:

```
PBREQ: mmc1 CMD25 arg=00160000      <- 512 bytes per write
PBIRQ: mmc1 stat=00000001 req=1
PBIRQ: mmc1 stat=00000001 req=1
PBIRQ: mmc1 stat=00000002 req=1
PBREQ: mmc1 CMD13 arg=00010000      <- status poll after every write
... ~30 ms per 512-byte write
```

Reads are 4 KiB `CMD18`s and are fine; **writes are single 512-byte `CMD25`s
with a status poll each**, i.e. ~30 ms per 512 B.  A 2 GiB image would take
~18 hours.  This blocks any Linux-side large write (rootfs, image restore) and
is the next thing to fix in the MMC driver.  The QNX driver writes its own
filesystem at normal speed.

## 4. The recovery (bb10mt)

The autoloader is Windows-only (USB drivers), so `bb10mt` was used on the host.

```bash
cd /home/psyden/Pdrive/bb10mt

# 1. autoloader .exe -> cap.exe + .signed
./bb10mt split -i PB-4869-root.exe
#   -> PB-4869-root.0.signed (839,188,520 B)

# 2. .signed -> components (.mbr .rcfs .ifs .ufs .lst)
./bb10mt unpack -c PB-4869-root.0.signed
#   -> PB-4869-root.0.4.ufs (2,550,202,368 B) = the compact UFS image

# 3. make a UFS-only component list (do NOT edit the unpacked .lst:
#    unpack rewrites it)
printf 'PB-4869-root.0.4.ufs=16640,0\n' > PB-4869-root-UFS-src.lst

# 4. pack just the UFS into a signed QCFM container
./bb10mt pack -l PB-4869-root-UFS-src.lst -v 1,2 -s true \
             -c PB-4869-UFS.signed
#   -> 426,246,184 B; verified by unpack + sha256 round trip

# 5. bootrom present (0fca:0001), then:
./bb10mt flash -i PB-4869-UFS.signed \
  -r /home/psyden/SSDdrive/playbook-linux/local/vendor/bbusb/loaders
#   it prints the device, then:  Enter password (10/10):
#   the device password is 0000  (interactive; stdin is not used)
```

Notes:
* `split` and `unpack` are different steps — `unpack` is what yields the
  partitions.
* `bb10mt flash` connects to the **bootrom** and prompts for the device
  password; `0000` is the PlayBook password on these units.
* The bootrom is `0fca:0001`; a reboot into it is required (the operator does
  this physically).
* The unpacked `.ufs` is a **compact** image (2,550,202,368 B = `0x98010000`;
  the `.lst` entry is `16640,0`) with no plain QNX6 superblock at 0 or 0x2000;
  only `bb10mt`'s own code reads it.  Our `qnx6-usage` cannot.
* `./bb10mt qnx6 mount -i <ufs> -m <dir>` (run by the operator) mounts it rw
  under FUSE; that is how the image was edited to carry the fixed
  `accounts/devuser/startup.sh` before repacking.

## 5. State after recovery

* `hd3` = the full 58 GB UFS again: `df /` = 58 G, 2.2 G used.
* Sealed IFS in region 0: sha256 `c72b0530…`, `os_type` 6.
* Door/SSH: the restored UFS did not accept our SSH key; `BerryShell-V4.py auth
  --password 0000` re-uploaded it (the first attempt timed out at the qconn
  handshake, the retry succeeded).  The door supervisor must be running for
  `tools/pbssh.sh` — `auth` closes the door when it exits.
* `BOOTCTR_FCTRY` had drained to **0x2d** because the restored UFS carried the
  *older* 3098-byte `startup.sh`, whose first `dd` failed at boot (trap 29: the
  overlay/interpreter is not ready yet) and whose guard correctly refused to
  write.  The **4490-byte** script from the tar backup (with the wait for a
  working `dd`) was restored; the top-up then wrote `0x32` (logged
  `bootctr: wrote 0x32, readback ok`) and the counter reads `32 00 00 00`.
* The UFS image on the host was updated with the same 4490-byte script
  (`PB-4869-root.0.4.ufs` sha256 `e11f705f…`), repacked to
  `PB-4869-UFS.signed`, and the packed container verified by unpacking it and
  comparing the image hashes.

## 6. Lessons

1. **A recreated QNX6 root is not a restored system.**  The files were all
   present and hash-correct, and the unit still booted as new.  The cause is now
   identified as a `pps_common()` startup race on `autorun::1` (report 137);
   confirm it with the retry instrumentation before keeping a shrink.
2. **The Linux MMC write path looked broken for large writes.**  *(Superseded
   by report 136: the driver is fine — `recv` submitted tiny buffered writes and
   per-request printk to fbcon dominated.  Use ≥64 KiB `O_DIRECT`; the remaining
   limit is the ACM transport, not the media.)*
3. **The pre-OS `bb10mt` UFS route works and is reproducible** (split →
   unpack → pack UFS-only → flash), with the bootrom password and the compact
   UFS format documented above.
4. **Keep the devuser `startup.sh` in sync everywhere**: the live unit, the
   UFS image backup, and `bench/`.  The older copy silently stops the
   `BOOTCTR_FCTRY` top-up and drains the factory budget (0x32 → 0x2d here).
5. The `local/backups/` artifacts (tar, 2 GiB image, packed UFS) are the
   recovery set; none are committed to git.
