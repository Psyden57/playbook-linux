# 138 — A smaller hd3 filesystem is reseeded at boot: the shrink is not an FS-level operation

**Session 17 (absolute), Linux-era session 11 — continuation.  2026-10-30.**
Image: **`0xFC77CD47`** — the `0xEB617C60` kernel plus `CONFIG_QNX6FS_FS=y` and a
console `qnx6` command (read-only loop mount of hd3).  This image was flashed to
region 0 for the inspection and is the only new kernel in this report.

**Result in one sentence:** a 1 GiB QNX6 filesystem built from the **UFS factory
tree** (`ufsroot-10.0.4.197`), written to `hd3` and booted, made the unit come up
at the setup screen — and the read-only inspection shows the boot **reformatted
`hd3` to the full 58 GB and reseeded it from `/base`**.  A smaller-than-partition
filesystem is therefore not kept; the shrink cannot be done at the filesystem
level.

This supersedes the pps-race hypothesis of report 137: the recreated filesystem
was not left in place to race over `autorun` — it was **replaced by the base
seed**, which is an unprovisioned root.

---

## 1. The experiment

1. **Source:** the restored UFS image (`PB-4869-root.0.4.ufs`, factory
   **10.0.4.197**), mounted read-only on the host with `bb10mt qnx6 mount`, its
   tree tarred **without `-S`** and with `--numeric-owner`
   (`local/backups/ufsroot-10.0.4.197-numeric.tar`, 313 733 120 B, 2 416
   members, `bsdtar` rc=0).
2. **Build on the device** (TabletOS up, door up): a 1 GiB file `/newfs.img`;
   `devb-loopback loopback fd=/newfs.img` → `/dev/lo0`;
   `/sbin/mkqnx6fs -E -b 4096 -r 5 -q /dev/lo0`; mount it; extract the tar with
   `/tmp/tar --numeric-owner -xpf -` (2 m 07 s, ~2.5 MB/s over the door);
   unmount; stop the driver.
3. **Verify inside the image before writing:** `etc/` is a directory (factory
   style), `/.rootfs` 1 byte, versions `10.0.4.197`, owners correct
   (`accounts/1000` root:1000, `appdata` 89:1000, `devuser`), `startup.sh`
   4490 B, `settings` `lang_countryCode::en_US`.
4. **Write to hd3:** pull the 1 GiB image to the host (3 m 39 s), `sync;
   mount -ur /`, stream it to `dd of=/dev/hd3 bs=1048576` (3 m 38 s, `DD=0`),
   reboot.

The unit came up at **`0fca:8012` (setup)**.

## 2. The inspection (image `0xFC77CD47`)

Region 0 was flashed with `0xFC77CD47` (hd3 untouched), and the console's new
`qnx6` command mounted hd3 read-only over a loop device.  The first mount attempt
with the project's old offset failed; the driver said it all:

```
qnx6: wrong signature (magic) at position (0x2000) - will try alternative position (0x0000).
qnx6: wrong signature (magic) in superblock #1.
qnx6: unable to read the first superblock
```

**The FS partition is at raw `0x49000000`, not `0x48F00000`** (1 MiB higher);
its boot block (`eb 10 90 …`) is there, and its superblock at `0x49002000`.
Mounted at the corrected offset, hd3 is **not** the image that was written.
(The mount was `qnx6 49000000`, an explicit offset: image `0xFC77CD47` still
carried the old `0x48F00000` default; the source default is corrected after the
test, so the next build mounts with no argument.)

| field | what was written (1 GiB, UFS tree) | what is on hd3 after the boot |
|---|---|---|
| `num_blocks` | 262 140 (1 GiB) | **15 273 980 (58 GB)** |
| `num_inodes` | 32 768 | **238 656** (the 58 GB `mkqnx6fs` default) |
| `/.rootfs.os.version` | `10.0.4.197` | **`2.0.0.4869`** |
| `/.rootfs.pps.version` | `10.0.4.197` | **`2.0.0.4869`** |
| `/etc` | directory | directory (seed) |
| `var/pps` | unversioned `system/settings` etc. | only `accounts@0.0.775`, `services@0.0.775` — **no `system@`** |
| `var/log/migrate/current` | — | `FS MIGRATE … 2.0.0.4869 USERFS VERSION: 2.0.0.4869` |

That is `rootfs.sh`'s **format + seed** path: `mkqnx6fs -E -b 4096 -r 5 -q
/dev/hd3` (full partition, default inodes), `cp -Rpf /base/seed/rootfs/* /`,
`seedmedia.sh`, then the version files written as `${OS_VERSION}`
(`/base/etc/os.version` = 2.0.0.4869).  The seed is the **base** root: it has no
`/var/pps/system` (hence no `settings`/`navigator` objects), so the system is
unprovisioned and comes up in setup.

## 3. Why it reseeded (what is proven, what is not)

`rootfs.sh` runs on every boot:

```sh
dresize -v1 /dev/hd3
mount | grep hd3 || mount -tqnx6 -o nosuid,noatime,alimit=... /dev/hd3 /
if [ -e /.rootfs ]; then ... exit 0; fi
# else: fdisk (if whole disk), mkqnx6fs, mount, seed, version files, touch /.rootfs
```

The image **had** `/.rootfs`, and QNX *can* mount a smaller FS (we mounted the
same 1 GiB image at `/mnt/new` with `devb-loopback` + `mount -tqnx6` during the
build).  So the reseed means the boot did **not** see that filesystem at `/` or
did not see its `/.rootfs` — i.e. the root selection / mount in the
bootloader+IFS path rejected or bypassed it.  The exact check is in the closed
RIM bootloader / IFS (`rfs_validator`, `sanityChecking`) and was not identified;
what is proven is the outcome (a full-partition 2.0.0.4869 seed) and that the
FS-level shrink is not preserved.

**Implication:** the long-standing "shrinking hd3 requires an MCT rewrite" is
right for a stronger reason than the docs gave — a smaller filesystem inside the
58 GB partition is simply reseeded.  The partition itself must change.

## 4. The flasher does not resize (bb10mt source)

`bb10mt` is Free Pascal; `TRamLoader.FlashFile` (ramloader.pas) streams the
payload verbatim in `MAX_FLASH_BLOCK` chunks and sends the trailing 560-byte
signature.  It never parses or rewrites the QNX6 superblock.  So the fact that
the vendor UFS image works on 16/32/64 GB units is **not** a bb10mt resize: the
image is a portable compact container and the device-side loader/format expands
it to the partition.  A raw small QNX6 image cannot be substituted into that
path.

## 5. What this changes

* **The shrink plan:** an FS-level shrink `hd3` is dead.  A smaller TabletOS root
  needs an MCT/partition change (operator-only, brick path) — or the root stays
  where it is and the Linux root goes to region 1.
* **The "new install" cause** (report 135 §2, report 137): it is the
  `rootfs.sh` format+seed path, triggered because the smaller FS was not accepted
  as the seeded R/W root.  The base seed is unprovisioned.  The pps-race
  explanation of report 137 is superseded.
* **The media offset:** hd3's raw start is `0x49000000` (superblock
  `0x49002000`), not `0x48F00000`.  Corrected in `SAFETY.md` §4, KNOWN-ISSUES
  trap 47, report 134 §5, HANDOFF and the console default.
* **New capability:** `console: qnx6 [off-hex]` mounts hd3 read-only over a loop
  device at `0x49000000` (with `CONFIG_QNX6FS_FS=y`).  This is how the
  inspection above was done; it is the tool for future rootfs work.

## 6. Bench state / recovery

* Region 0: our `0xFC77CD47` image was flashed for the inspection; the **sealed
  IFS was restored afterwards** (181 frames).
* hd3: left as the 2.0.0.4869 base seed by the failed boot; the operator restored
  the full 58 GB UFS with `bb10mt` (`PB-4869-UFS.signed`, password `0000`).
* The 1 GiB image and the numeric UFS tar are in `local/backups/`
  (`newfs-ufsroot-1g.img` was on the host during the test; the durable pieces
  are the tar and the method).

## 7. Residual uncertainty

* The exact acceptance check for the R/W root (size vs partition, a marker in
  the loader, `rfs_validator`) is not identified.  A controlled test would boot
  the 1 GiB image and capture the bootloader/IFS console — no serial is
  available, so this remains inferred from the outcome.
* Whether a *full-partition* freshly built FS from the same UFS tree boots and
  migrates correctly (the bb10mt-style path) was not tested with `mkqnx6fs` at
  58 GB; the vendor UFS restore does exactly that and works.
