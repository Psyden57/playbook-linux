# Session 17 (absolute) — the MMC write path measured, and a smaller hd3 FS is reseeded

> Numbering: absolute **17**.  The reports call it Linux-era **"session 11"**
> (`NN-6`).  Images: `0xEB617C60` (write-path measurements) and `0xFC77CD47`
> (QNX6 read path).  Reports: **136**, **137** (superseded), **138**.

Date / span: 2026-10-30.
Unit state at the end: **TabletOS from the restored 58 GB UFS** (QNX 6.6,
`df /` = 58 G, 2.2 G used), door/SSH up, `0fca:8010`; **sealed IFS on media**
(sha256 `c72b0530…`, `os_type` 6, CRC32 `0x116B2562`); `BOOTCTR_FCTRY = 0x32`;
**no chainloader/kernel image on media**.

## Goal of this session

The handoff's first task: find why Linux writes the eMMC as single 512-byte
`CMD25`s with a `CMD13` poll each (trap 49, report 135 §3).  Then the recreated-
hd3 "new install" cause, and a root filesystem.  Mid-session the operator
approved a read-only QNX6 mount of hd3 and an hd3 retry with the UFS factory
tree as the source.

## Confirmed (measured, read in a file, or seen on the bench)

* **The MMC driver has no write request-size limit.**  With the new `trace`
  switch on, `PBREQ` reads `CMD25 … W blksz=512 blocks=512 bytes=262144 sg=64`,
  one `CMD13` per request (`mmc_blk_card_busy`).  [image `0xEB617C60`, report 136
  §3.2]
* **Writes are fast with a sane writer:** O_DIRECT 4 KiB chunks 925 KB/s,
  64 KiB 5 914 KB/s, 256 KiB 6 985 KB/s, 1 MiB stream 5 514 KB/s; the
  deterministic pattern read back with the same FNV-1a64 (`529aaa6348e22325`).
  [report 136 §3.1, `docs/reports/data/mmc-write-0xEB617C60-2026-10-30.txt`]
* **The old 17–32 KB/s was the writer plus printk:** `recv` `pwrite`d one
  tty-sized buffer at a time and every request printed 4–7 `pr_info`s; the only
  console is tty0 (fbcon), drawn synchronously.  [report 136 §4; the DTS has no
  `stdout-path`, capture `printk: legacy console [tty0] enabled`]
* **The ACM console is not a bulk transport:** `sink 16777216` = 134 KB/s
  (124.7 s), host blast >12 MB/s, bytes dropped; `lsusb -v` = High Speed
  (480 Mbps), 512-byte bulk.  [report 136 §3.3]
* **The old `recv` transfer corrupted data:** 11 159 480 B did not finish in
  300 s; media FNV `25a2c15560ac5bda` vs file `473dcd0176320fba`.  [report 136
  §3.4]
* **The flashed UFS is 10.0.4.197** (`/.rootfs.os.version` =
  `31 30 2e 30 2e 34 2e 31 39 37 0a`); the live RW version is 2.0.0.4869 because
  `migrate_rootfs.sh` rewrote it on the first boot (`OS_VERSION` from
  `/base/etc/os.version`).  [report 137 §3a, live `cat`]
* **The UFS factory tree mounts and is 298 MB apparent** (`bb10mt qnx6 mount`);
  a tar without `-S`, `--numeric-owner`, is libarchive-clean (`bsdtar` rc=0) and
  preserves numeric owners (0/0, 0/1000, 89/1000).  [report 137 §3a]
* **bb10mt does not resize the UFS:** `TRamLoader.FlashFile` (ramloader.pas)
  streams the payload verbatim and never touches the QNX6 superblock.  [bb10mt
  source, report 138 §4]
* **The QNX6 read path works:** `CONFIG_QNX6FS_FS=y` + console
  `qnx6 [off-hex]` (read-only loop over `/dev/mmcblk1`, `LO_FLAGS_READ_ONLY`,
  `MS_RDONLY`) mounts hd3 read-only on `/mnt`.  [image `0xFC77CD47`,
  `initramfs/console.c`]
* **hd3's raw start is `0x49000000`, not `0x48F00000`** (1 MiB higher): the
  image's boot block `eb 10 90 …` is there, the superblock at `0x49002000`.
  [report 138 §2, `blk rd` measurements]
* **A smaller hd3 filesystem is reseeded at boot.**  A 1 GiB QNX6 FS built from
  the UFS factory tree, written to hd3 and booted, came up at setup
  (`0fca:8012`); after the boot hd3 was a **58 GB 2.0.0.4869 seed**:
  `num_blocks` 15 273 980, inodes 238 656, `/.rootfs.os.version` 2.0.0.4869,
  `var/pps` without `system@`, `var/log/migrate/current` at
  2.0.0.4869→2.0.0.4869.  [report 138 §2]
* **Final bench state:** TabletOS 6.6, `df /` 58 G/2.2 G, `BOOTCTR_FCTRY`
  `32 00 00 00`, sealed IFS sha256 `c72b0530…` with `os_type` 6 and CRC32
  `0x116B2562`, door up (`0fca:8010`); `/newfs.img`, `/tmp/tar` and
  `/tmp/devb-loopback` are gone (the UFS restore wiped hd3; `/tmp` is RAM).

## Inferred (reasoning from evidence, not directly measured)

* **The boot did not accept the smaller FS as the seeded R/W root, so
  `rootfs.sh` ran its format+seed path.**  Strong: the resulting 58 GB seed is
  exactly what that path produces, and QNX *can* mount a smaller FS (we mounted
  the same 1 GiB image at `/mnt/new` during the build), so the rejection is in
  the bootloader/IFS root selection, not `fs-qnx6`.  The exact check is not
  identified.
* **The vendor UFS's size portability comes from the loader/compact container
  expanding it to the target partition**, not from bb10mt.  Strong for bb10mt
  (source); medium for the loader internals.
* **The ~134 KB/s ACM drain and the byte loss are one gadget/tty throughput
  problem**, not enumeration.  Medium.
* **The base seed is unprovisioned** (`var/pps` has no `system/`), which is why
  a reseeded unit shows the setup wizard.  Strong (measured store listing; the
  `pps_common` check that writes `autorun::1` when the settings object is
  absent).

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`omap_hsmmc` limits writes to one block."**  No: `PBREQ` with
  `blocks/bytes/sg_len` shows 256 KiB `CMD25`s.  Killed by adding those fields.
* **"The `CMD13` per write is the defect."**  It is `mmc_blk_card_busy` on
  completion, one per request.
* **"The old 17–32 KB/s is the media."**  It is 512 B per ~30 ms of fbcon
  printk; `blkwr` with tracing off gives 5.5–7 MB/s.
* **"The link is slow because the gadget is full-speed."**  It enumerates at
  480 Mbps.  Killed by `lsusb -v`.
* **"`recv` just needs patience."**  It also loses bytes; the readback did not
  match.
* **"The recreated root is missing `/base`, so the base OS never starts."**  My
  first draft of the new-install cause.  Wrong: `startup.sh` recreates all
  `/base` symlinks, and `rootfs.sh` is gated on `/.rootfs` (present).  The
  operator rejected the chain ("if startup.sh didn't run at all, the system
  wouldn't ever even reach that brand-new install state"); withdrawn.
* **"Setup is `autorun::0`."**  My reading of the code; the operator corrected
  it: `autorun::0` = set up, and `pps_common` writes `autorun::1` when the
  settings object is missing (which is true on the base seed).
* **"The pps load race explains the new install."**  Superseded: the boot had
  **reformatted and reseeded** hd3; there was no recreated root left to race.
* **"The FS partition is at `0x48F00000`."**  Measured `0x49000000`.
* **"`dresize` only shrinks, so the 2 GiB FS is not grown back."**  `dresize`
  does only shrink, but the OS reseeds a smaller FS anyway — the shrink is dead
  for a different reason.
* **"The host FUSE tar loses owners."**  Numeric ids survive (only the names
  differ, `psyden` vs `1000`; QNX resolves numerically).
* **"`bb10mt flash` resizes the UFS."**  No (source).
* **`pkill -f pre_os_ifs_flash` killed my own shell** — trap 32 in action; use
  `ps -eo pid,args | awk '$2=="python3"…'` and kill the pid.
* **`tools/env.sh` shadowed the host `make`** with the QNX SBC's GNU make 3.82;
  the kernel needs ≥ 4.0.  Fixed by appending the QNX bin last.

## Unknown / open questions

* **Which check rejects a smaller R/W FS** (bootloader, `rfs_validator`,
  `sanityChecking`)?  Settle with the bootloader/IFS console (no serial is
  available on the unit) or by booting a **full-partition** `mkqnx6fs` FS built
  from the same tree.
* **Why the ACM drains at 134 KB/s and where bytes are lost.**  Settle by
  instrumenting `f_acm`/the tty, or by replacing the transport (RNDIS/ECM, USB
  mass storage).
* **Does a full-size (58 GB) freshly built QNX6 FS from the UFS tree boot and
  migrate?**  Not tested; the vendor UFS restore does exactly this and works.
* **fsencrypt/security state** of a rebuilt FS vs the vendor FS — not compared.
* **The recreated 2 GiB image's boot** (report 135) was not re-inspected; the
  1 GiB UFS-tree case shows the reseed mechanism, and 2 GiB was almost certainly
  the same.

## Superseded / uncertain

* **Trap 49** ("single 512-byte `CMD25`, 17–32 KB/s") — replaced by the
  writer+printk explanation (report 136).
* **Report 135 §2's "cause unknown"** and its identity/serial candidates —
  report 138: the boot reseeds a smaller FS.
* **Report 137's pps-race causal claim** — superseded by 138; its observations
  (settings, appdata, mount points, UFS/tar, `dresize`) stand.
* **The FS partition offset `0x48F00000`** — `0x49000000` (SAFETY, STATE,
  KNOWN-ISSUES trap 47, report 134, HANDOFF, console default corrected).
* **"Mounting the eMMC from Linux is forbidden"** — the operator approved
  read-only QNX6 mounts (contradiction resolved; see
  `docs/contradictions/safety-no-emmc-mount-vs-readonly-root.md`).
* **"The UFS image represents a 58 GB FS"** — it is a portable compact
  container expanded by the flasher/loader (report 137 §3a correction).
* **"dresize grows the FS"** (my earlier concern) — it only shrinks; the reseed
  is `rootfs.sh`'s format path.

## Artefacts produced

* **Reports**: `134` (FS offset corrected), `136` (write path), `137`
  (superseded), `138` (reseed + QNX6 read path).
  **Data**: `docs/reports/data/mmc-write-0xEB617C60-2026-10-30.txt`.
* **Images**: `0xEB617C60` (`local/images/kernel-0xEB617C60-mmcwrite.img`);
  `0xFC77CD47` (`local/images/kernel-0xFC77CD47-qnx6read.img`, QNX6FS +
  `qnx6`; the image's default offset is the old `0x48F00000`, so mount with
  `qnx6 49000000`; the source default is fixed for the next build).
* **Kernel**: `omap_hsmmc` PBREQ/PBIRQ behind `/sys/module/omap_hsmmc/
  parameters/trace` (default off) with `R/W blksz blocks bytes sg_len dt`;
  `CONFIG_QNX6FS_FS=y` in `kernel/winchester.config`,
  `kernel/patches/winchester.config`, `local/kernel-tree/.config`.  The source
  patch is unchanged (`kernel/patches/0001-…` 4 198 lines / 32 files).
* **Console**: `sink <len>`, `blkwr <off> <len> [chunk]`,
  `qnx6 [off-hex]`; `initramfs/console.c`.
* **Host tools**: `tools/pbsink.py`, `tools/pb_bootlog.sh`, `tools/env.sh`
  PATH-order fix.
* **Recovery/source data (gitignored)**: `local/backups/ufsroot-10.0.4.197
  {-numeric}.tar`; `local/backups/newfs-2g.img`; the 1 GiB UFS-tree image was
  built on the device with `devb-loopback` + `mkqnx6fs -E -b 4096 -r 5 -q` and
  the numeric tar (method in report 138 §1).
* **Docs**: `STATE.md`, `HANDOFF.md`, `KNOWN-ISSUES.md` (traps 49–54),
  `SAFETY.md` (FS offset; read-only mounts), `DECISIONS.md`, `ROADMAP.md`,
  `docs/sessions/NUMBERING.md`, `docs/contradictions/
  safety-no-emmc-mount-vs-readonly-root.md`.
