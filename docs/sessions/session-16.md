# Session 16 (absolute) — the eMMC proven under the DSS clock regime, and the real media offset

> Numbering: absolute 16.  The reports call it Linux-era **"session 10"** (`NN-6`).
> Images: **`0xC07AB9FA`** (the proof image).  Report: `134`.

Date: 2026-10-29.
Unit state at the end: see "Bench at the end" below (written after the restore).

## Goal of this session

The handoff's FIRST TASK: verify the eMMC still works under the new clock regime
(the DSS fix moves the PER-PLL output dividers at probe time; eMMC is on M2,
which did not move), then get a minimal root on `mmcblk1`.  The operator decided
before any bench write: **read-only proof only**, root-location decision
deferred, no region-1 write, and the DSS clock cleanup deferred too.

## Confirmed (measured, read in a file, or seen on the bench)

**The eMMC is healthy and the DSS clock did not disturb it**

* A freshly flashed 11 152 064-byte image reads back with the identical
  FNV-1a64: `8308f0ff29452390` at raw `0x8100000`, and `8615f45509bff390` over
  the payload padded to `0xAB0000`.  [image `0xC07AB9FA`; report 134 §3]
* Static pre-existing media matches the pre-flash TabletOS capture:
  region-0 sector 0 = `c4feeffb5dbcdfb0` (raw `0x8000000`), and media LBA 0 =
  `41b9c0a35a996242`.  [same]
* `mmcblk1` byte 0 is the same byte as QNX `hd0` byte 0 — the two devices are
  the same view.  [same]
* MMC2 functional clock reads **96 MHz**, matching the `func_96m_fclk` branch
  from `dpll_per_m2x2_ck` (192 MHz, DPLL_PER M2, unchanged); `func_64m_fclk` is
  the M4 branch at 64 MHz.  The mux bit was not read — the rate is the evidence.
  [`grep mmc/dpll_per/func_96m /sys/kernel/debug/clk/clk_summary`; report 134 §4]
* Display regression: `fb 00ff00` → full green panel (operator).  [same]

**The media offset in the canonical docs is wrong by 1.3125 MiB**

* Five verified points fit `raw (mmcblk1 = hd0) = (loader_block - 0x15) * 0x10000`:
  region 0 at `0x8000000`, the IFS at `0x8100000`, the RFS table at `0x8C00000`,
  region 1 at `0x28000000`, and `mmcblk1 == hd0` at LBA 0.  [report 134 §5]
* The loader strips the mfcp/qcfp container header: the flashed image's first
  byte lands at the destination block, not the template header.  [same §3]
* Region 0 has no room for a rootfs (the IFS zone is 11.25 MB and the image is
  11.15 MB).  Region 1 is 512 MB, already partitioned and empty.  [same §6]
* The MBR TLV range (raw `0x7F00000..0x8000000`) reads all zero.  [same §5]

**Tooling**

* `console` gained `blk info|sum|sumb|rd` (read-only FNV-1a64 over
  `/dev/mmcblk1`) and `grep <substr> <path>` (whole-file filter for
  `clk_summary`); the stale 180-s banner text is gone.  [report 134 §8]
* The new image is `0xC07AB9FA`, 11 152 064 B, same kernel/DTB as `0x8C146805`.
* The shrink/recovery numbers are in the continuation below and report 135: tar
  **372,408,320 B** (3,927 entries), 2 GiB image sha256 **`96e14d47…`**, packed
  UFS-only container **426,246,184 B**, restored UFS image **`e11f705f…`**,
  `BOOTCTR_FCTRY` drained to **`0x2d`** and restored to **`0x32`**, UFS back at
  58 G / 2.2 G used, sealed IFS `c72b0530…`.

## Inferred (reasoning from evidence, not directly measured)

* **The eMMC was always safe from the DSS fix.**  The strong evidence is the
  measured clock branch (M2-derived, 96 MHz) plus the byte-exact read-back.
  Why `func_64m` moved while `func_96m` did not follows from the register table
  in report 133, but the register-field encoding was not re-decoded.
* **The 1.3125 MiB offset is a boot-ROM prefix in front of the OS-visible
  device.**  It is the simplest model that fits all five points (and the low
  MCT entries).  Whether the physical eMMC user area really starts 1.3125 MiB
  in is **not** measured; nothing in the current procedures depends on it.
* **Region 1 is the right first home for a Linux root.**  It needs no MCT edit,
  cannot touch TabletOS/NVRAM/RPMB, and is 512 MB.  Medium-strong: it is
  measured as empty and inside the loader's allowed `0x1B` range, but no write
  has been attempted.
* **The "new install" is a state/identity effect, not a missing file.**  The
  files were all present and hash-correct (strong), so the trigger is the
  recreated filesystem's identity/serial, the installer/PPS state, or a
  first-boot marker not on hd3 (weak; none explored).
* **The Linux write defect is a driver request-size limit**, not a media
  problem: reads at 4 KiB are fine, writes are single 512-byte `CMD25`s with a
  status poll (strong from the `PBREQ`/`PBIRQ` cadence; the exact limit is not
  yet identified).

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The MCT media offsets are `block * 64 KiB`."**  They are not: every raw
  offset in `SAFETY.md` §4, report 100 and the flasher's log is 1.3125 MiB
  high.  Killed by: hashing the region-0 sector and the flashed image on the
  device.
* **"`/dev/hd0` is the whole media from byte 0 and the loader's blocks are
  absolute."**  This produced a contradiction: the loader's `0x825` write
  appeared at `0x8100000`, and the MCT's low entries went negative under a
  uniform shift.  Killed by: the five-point map; the rule is `(block-0x15)`.
* **"The loader writes the mfcp/qcfp container header to media."**  It strips
  it; the payload starts at the destination block.  Killed by: `blk rd` at the
  predicted image offset.
* **"A large QNX `dd` from `/dev/hd0` at a 1.2 GiB skip is reliable."**  One
  1 MiB capture came back all zero for no established reason, while two other
  block sizes at the same offset agreed.  Killed by: re-reading the same range
  at two block sizes.  Do not trust a single unexplained read; re-read.
* **"Hex offsets are safe to compute by hand."**  One mis-addition
  (`0x8250000 + 0x10020`) was caught only by the operator.  New rule: compute
  every address with Python.  [operator, this session; recorded in taste]
* **"A verified QNX6 image of hd3 restores the system."**  The files were all
  there — 372 MB tar, contents and numeric ownership matching, a 2 GiB image
  read back byte-exactly — and the unit still booted as a **brand-new
  install** (report 135 §1/§2).  Killed by: the reboot after the write.
* **"The Linux initramfs can be the offline eMMC writer."**  Linux writes the
  eMMC as single 512-byte `CMD25`s with a `CMD13` poll each (~17–32 KB/s), so a
  2 GiB write is not viable (report 135 §3).  Killed by: the `recv` experiment
  and the `PBREQ`/`PBIRQ` log.
* **"`--one-file-system` keeps the backup out of QNX's `/dev`."**  It does not
  — `/dev` is not a separate mount there.  The tar descended into the
  resource-manager nodes and the unit rebooted mid-backup.  Killed by: the
  aborted backup; fixed with explicit `--exclude=./dev` etc.
* **"A reboot costs one `BOOTCTR_FCTRY` unit."**  False for a **TabletOS**
  boot: the device-side `accounts/devuser/startup.sh` tops the counter back to
  `0x32` on every TabletOS start, so a power-cycle into TabletOS is net-free.
  Only a boot of *our* image spends a unit.  [operator, this session]

## Unknown / open questions

* Does the physical eMMC user area really start at raw `+0x150000`?  Settled by
  a measurement that can see the boot ROM's own view (a loader query), or by
  reading the eMMC's physical geometry.  Not needed for the current work.
* Where should the Linux root live: region 1 (512 MB, no MCT edit), or a
  repartitioned 50+ GB after shrinking the 58 GB `hd3`?  The operator deferred
  this.  The region-1 route is the only one an agent can do; the `hd3` route is
  operator-only, needs an MCT rewrite and a backup of a live QNX root.
* Can the DSS clock fix become a clock-init property?  Deferred by the operator;
  report 133 §6 and trap 45 still stand.
* **What makes a recreated QNX6 root boot as a "new install"?**  `.seed` (352
  entries), the PPS navigator config, `env.sh`, `startup.sh` and all the files
  were present and hash-correct.  Candidates (none confirmed): the filesystem's
  identity/serial, the installer/PPS state, or a first-boot marker not on hd3.
  Settled by a controlled experiment that varies one candidate at a time.
  Do not retry the shrink until it is known (report 135 §2).
* **Why does Linux issue single 512-byte `CMD25` writes?**  A request-size /
  DMA / block-count limit in `omap_hsmmc.c`.  Settled by inspecting the queue
  limits and the `mmc_blk_issue_rw_rq` path, then an A/B write test (trap 49).

## Superseded / uncertain

* **All previously published raw media offsets** ("IFS at media `0x8250000`",
  "MBR at `0x8050000`", "region 1 at `0x28150000`") — they are loader-block
  coordinates, not raw device offsets.  The flash procedure is unchanged; the
  raw offsets are `(block-0x15)*0x10000`.  See report 134 §5; the canonical
  docs were corrected in place.
* **"A read-only eMMC mount is forbidden."**  `SAFETY.md` §4 says "never mount
  or write the eMMC from Linux", while the session-15 handoff and ROADMAP §5
  propose mounting `mmcblk1` read-only.  This session did neither; the
  disagreement is recorded in
  `docs/contradictions/safety-no-emmc-mount-vs-readonly-root.md`.
* **This note's earlier "read-only only" framing is superseded by the
  continuation below.**  The operator later approved the shrink experiment;
  hd3 was written (from QNX, with `/` read-only) and then restored with
  `bb10mt`.  The read-only sections above remain accurate for the first half of
  the session.
* **"A reboot costs one `BOOTCTR_FCTRY` unit."**  See the dead-ends entry: a
  TabletOS boot tops the counter up; only our images spend a unit.  The
  operator corrected this in-session, and `SAFETY.md` §2 already said only a
  booted TabletOS puts one back.

## Continuation — the hd3 shrink attempt and recovery

After the read-only proof the operator chose the shrink (option B in the
earlier discussion).  Full detail is in **report 135**; the short version:

* backed up hd3 with QNX GNU tar (372,408,320 B, 3,927 entries, verified by
  sample hashes including a 55 MB binary), mapped the filesystem (4096 B
  blocks, 145,279 used blocks ≈ 595 MB, 3,777 live inodes), built a 2 GiB
  QNX6 image **on the device** with `devb-loopback` + `mkqnx6fs` + `tar`
  restore (contents and numeric ownership matched the live tree), wrote it to
  hd3 from QNX with `/` remounted read-only, and read the 2 GiB back
  byte-identically (sha256 `96e14d47…`).
* the unit then booted as a **brand-new device** (`0fca:8012`, no door, setup
  screen), even though the image contained `.seed` (352 entries), the PPS
  navigator config, `radio/scripts/env.sh` and `accounts/devuser/startup.sh`.
  The cause is **unknown** and is not a missing file.  This is why the shrink
  was reverted.
* the Linux `recv` console path (built to write the image offline) exposed a
  real eMMC **write** defect: writes are single 512-byte `CMD25`s with a
  `CMD13` poll each (~30 ms per 512 B, ~17–32 KB/s) while reads are 4 KiB
  `CMD18`s.  A 2 GiB Linux-side write is not viable.  (Report 135 §3.)
* recovered the UFS with `bb10mt` (split → unpack → UFS-only pack → flash at
  the bootrom; password `0000`), restoring the full 58 GB TabletOS root.  The
  restored UFS did not accept our SSH key, so `BerryShell-V4.py auth` was used
  to re-upload it; the door supervisor had to be restarted afterwards.
* the restored UFS carried the **older** 3098-byte `startup.sh`; its boot-time
  `dd` failed (trap 29) and the guard refused the top-up, draining
  `BOOTCTR_FCTRY` to `0x2d`.  The **4490-byte** script from the tar backup
  (which waits for a working `dd`) was restored; the top-up wrote `0x32`
  (`bootctr: wrote 0x32, readback ok`) and the counter reads `0x32`.
  The UFS image backup was updated with the same script and repacked; the
  packed container was verified by unpacking and comparing the `.ufs` hashes
  (`e11f705f…`).

## Bench at the end

* TabletOS restored from the operator's UFS (`PB-4869-root.exe`), full 58 GB:
  `df /` = 58 G, 2.2 G used.
* Sealed IFS region 0: sha256 `c72b0530…`, `os_type` 6.
* Door/SSH up (`BerryShell-V4.py auth` re-uploaded the key; the door supervisor
  is running).  `BOOTCTR_FCTRY = 0x32` (`32 00 00 00`).
* `accounts/devuser/startup.sh` = the 4490-byte fixed version (sha256
  `c265cfbb…`), live and in the repacked UFS image (`e11f705f…`).
* Media: sealed IFS in region 0; the full 58 GB UFS on hd3.
* Host artifacts (gitignored `local/backups/`): `hd3-2026-10-29.tar`,
  `newfs-2g.img` (the 2 GiB shrunken image, sha256 `96e14d47…`), `verify/`.

## Artefacts produced

**Reports**: `134-emmc-storage-proof-under-dss-clock-2026-10-29.md`,
`135-hd3-shrink-attempt-and-ufs-recovery-2026-10-30.md`.
**Raw evidence**: `docs/reports/data/emmc-proof-0xC07AB9FA-2026-10-29.txt`.
**Images**: `local/images/kernel-0xC07AB9FA-emmcproof.img` (CRC `0xC07AB9FA`),
`local/images/kernel-0xBD8B49D1-recv.img` (the `recv` build, CRC `0xBD8B49D1`).
**Tools**: `tools/qnx6-usage.c` (+ host build), `tools/pbrecv.py`,
`initramfs/console.c` (`blk`, `grep`, `recv`).
**Recovery image**: `/home/psyden/Pdrive/bb10mt/PB-4869-UFS.signed` (UFS-only,
with the fixed `startup.sh`; verified by round trip) and
`PB-4869-root.0.4.ufs` sha256 `e11f705f…`.
**Canonical doc changes**: `STATE.md`, `HANDOFF.md`, `KNOWN-ISSUES.md` (traps
47–50, correction 3.17), `DEVELOPMENT.md` (console + bb10mt recovery),
`SAFETY.md` (media offset), `ROADMAP.md` §5, one contradictions file.
