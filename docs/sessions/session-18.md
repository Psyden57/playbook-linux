# Session 18 (absolute) — a root in region 1, the V1 `qcfp` trap, and a Linux RPMB path

> Numbering: absolute **18**.  The reports call it Linux-era **"session 12"**
> (`NN-6`).  Reports: **139** (the misrouted write and the recovery), **140**
> (the root in region 1), **141** (RPMB top-up feasibility), **142** (boot-log
> hygiene: the DSI CIO errors and the DSS clock warning).  Images:
> `0xFB3A9F5D` (region-1 root) and **`0x0D1DA0D3`** (current best; adds the
> boot-log fixes).

Date / span: 2026-10-30.
Unit state at the end: **TabletOS** (QNX 6.6) from the sealed IFS, door/SSH up,
`0fca:8010`; **sealed IFS** sha256 `c72b0530…`, `os_type` 6, CRC `0x116B2562`;
**region-1 Linux root present and read-only** (`0b87c29f…`); **RCFS head
verified** against the vendor component; `BOOTCTR_FCTRY = 0x32`;
`accounts/devuser/startup.sh` 4490 B; **no chainloader/kernel image on media**.

## Goal of this session

The handoff's first task: **bulk data onto the eMMC without the ACM console** —
a Linux root in media region 1 through the pre-OS `0xF7` loop, mounted read-only
and running.  Then the transport and the direction for postmarketOS/nura.

## Confirmed (measured, read in a file, or seen on the bench)

* **The loader uses the V1 `qcfp` run record, and its offset is
  region-relative.**  Destination = `region_start 0x815 + qcfp.run[0].offset`.
  The vendor `.lst` uses the same units (`ifs=16`→0x100000, `rcfs=192`→0xC00000).
  Patch **both** records: V1 `dest-0x815`, V2 `dest-0x805`.  [reports 98
  §2.3/§5, 139; trap 55]
* **A `bb10mt pack -v 2` container is rejected `0x11`** (V1 header, no `qcfp`
  block).  Vendor autoloaders are built with **`-v 1,2`**.  [operator, report 139]
* **A 32 MiB ext4 root was written to region 1** at raw `0x29000000` through the
  pre-OS `0xF7` loop (2055 frames).  Read back over the door: sha256
  `0b87c29f…` = the image; from Linux `blk sumb 0x29000000 33554432` =
  `c2e78e1f09b50208` = the host FNV-1a64.  [image `0xFB3A9F5D`, report 140]
* **The kernel mounts it read-only and runs it:** `EXT4-fs (loop0): mounted
  filesystem 5b10c0de-… ro without journal`; `PB-ROOTFS: /init is running from
  the region-1 ext4 image (Linux 6.15.11)`.  [boot log
  `local/logs/boot-20261030-063824.log`, report 140]
* **The console `rootfs [off-hex]` command** mounts it read-only on `/rootfs`;
  `ls /rootfs` shows `etc/ init lost+found/`, `cat /rootfs/etc/pbroot-version`
  returns the marker.  [image `0xFB3A9F5D`]
* **The incident:** the first attempt left the V1 record at `0x10` while
  sending 32 MiB meant for region 1, so it wrote at **region-0 block `0x825`**
  (raw `0x8100000`), destroying the sealed IFS and the first **21 MiB** of the
  RCFS at region-0 `0xC00000`.  The unit red-LED'd, then booted the splash and
  reset-looped.  Recovery: sealed IFS at `0x825` (181 frames), then the first
  32 MiB of `PB-4869-root.0.1.rcfs` at block `0x8D5` (2055 frames).  The unit
  booted.  [report 139]
* **The recovery is proven by read-back:** IFS `c72b0530…` (os_type 6, CRC
  `0x116B2562`); RCFS first 32 MiB `1e04336b…` == the vendor component; the MBR
  zone intact; region 1 still held its old test blob before the retry.  [report
  139 §5]
* **The loader's base is `0x815`, not `p34.start = 0x805`.**  The RCFS restore
  with V1 offset `0xC0` landed at `0x8D5` and the media matched the vendor
  component; a base of `0x805` would have landed 1 MiB low.  [report 139 §5.2]
* **`force_reset_loader.py` returns the unit to the bootrom**, not to TabletOS
  (operator).  [bench]
* **The RPMB key is not a secure-world-only secret.**  `rpmb-omap4` hands
  `smc #1` a descriptor with a **pointer to the 32-byte key in normal-world
  non-cached memory** (`0x2774`–`0x2790`).  When both KEK queries (`0x31`)
  fail, the **seed** is the 32-byte literal `"retreating is advancing in the
  o"` (file `0x71b4`); secure service `0x2F` op 1 turns it into the frame key
  at `page+0x460` (out is **16 bytes**, not 32).  [report 141; static RE of
  `local/proprietary/dumped4869ifs/proc/boot/rpmb-omap4`]
* **The Linux RPMB read path works, with zero writes.**  `pbrpmb` (image
  `0xDDB9D733`) did `GET_WRITE_COUNTER` → `result=0`, `req_resp=0x0200`,
  **counter 4179**; `block_count` must be **0** (1 gives result 1).  An
  authenticated `READ_DATA` of block 0 → `result=0`, `req_resp=0x0400`, nonce
  echoed.  `/dev/mmcblk1rpmb` accepts only `MMC_IOC_CMD`/`MMC_IOC_MULTI_CMD`;
  the kernel never sees a key and never computes a MAC.  [report 141 §5/§10;
  `docs/reports/data/rpmb-probe-0xDDB9D733-2026-10-30.txt`]
* **The RPMB write counter advances per boot** (4177 then 4179 two boots
  later) — each factory/CRC boot writes RPMB (the per-boot decrement).
  [report 141 §10]
* **The 121 `DSI CIO error, cio irqstatus 800000` lines were our own 121 DSI
  bridge reads**, and `0x800000` is bit 23 = `DSI_CIO_IRQ_ERRCONTENTIONLP1_2`,
  the LP contention of a BTA turnaround (it fires on successful reads too).
  The boot probes are now gated behind `pb_probe` (default off); the duplicate
  `pb_reg` sysfs warning and the `clk rate mismatch` WARN are fixed.
  [report 142; image `0x0D1DA0D3`]
* **The boot log of `0x0D1DA0D3` is clean:** 0 `DSI CIO error`, 0 duplicate
  sysfs, 0 `Tainted:`/`WARNING:`/`Oops`/`BUG`, `pb dss … fck 96000000`, fbcon
  up, region-1 root mounted.  [report 142; `local/logs/boot-20261030-084331.log`]
* **The loop capacity numbers are 512-byte sectors, not bytes.**  124,583,936
  sectors = 59.4 GiB; the loop at `0x29000000` exposes 123,240,448 sectors =
  ~58.76 GiB (the difference is exactly `0x29000000/512`).  [report 142 §4]
* **The kernel patch is current:** re-exported to **4247 lines / 32 files**,
  applies cleanly to pristine 6.15.11, and all 32 files compare byte-identically
  to `local/kernel-tree`.  [report 142 §5]

## Inferred (reasoning from evidence, not directly measured)

* **Report 98 §2.3's `dest = p34.start + rrcq.offset` is incomplete for this
  loader.**  The `.lst`/empirical evidence says the V1 record is used and the
  base is the region start `0x815`; the V2 `rrcq` appears unused here.
  [report 139]
* **`f` (the seed→frame-key derivation) lives in the monitor.**  It is not
  identity, SHA-256, HMAC-SHA256 with a zero key, or AES-ECB/CMAC with a zero
  key: all those candidates were tested against the captured 284-byte MAC input
  and failed.  [report 141 §10]
* **The read-only root is a sound first home** — `LO_FLAGS_READ_ONLY`,
  `MS_RDONLY`, ext4 `noload`, no journal: no write path exists in principle.
* **`hd3` can host the Linux root with no MCT edit.**  `hd3` *is* the MCT
  type-`0x1C` partition (raw `0x49000000`, ~58 GiB); we overwrite its contents.
  The loop exposes the full ~58.76 GiB (above).  [roadmap/decision, report 141]

## Dead ends and wrong assumptions (do not repeat)

* **"`bb10mt pack -v 2` is enough."**  No: the first frame is rejected `0x11`.
  Use `-v 1,2` (or clone the vendor hybrid template).
* **"Patch the V2 `rrcq`; the V1 record is legacy."**  The loader used V1; the
  32 MiB root went to region 0.  This is the incident.
* **"The restored IFS hash is wrong (`561e3fae…`)."**  The read was `0x2C0000`
  (the 44-block padded slot); the image is `0x2BFE10`.  Hash the image length.
* **"The unit is in the bootrom after `force_reset`; wait for TabletOS."**  It
  returns to the bootrom, ready to flash (operator).
* **"A region-1 payload is isolated from region 0 by construction."**  Only if
  the V1 record is patched; with both records patched it is.
* **"The RPMB key is one of the simple derivations."**  Literal seed identity,
  `HMAC(0^16,seed)`, `SHA256(seed)`, `HMAC(seed[0:16],seed[16:32])`,
  `HMAC(seed,0^16)`, the CID-derived seed variants, SHA-256/512, AES-128-ECB and
  AES-CMAC combinations — none reproduces the frame MAC.  The `0x2F` op 1
  output is only 16 bytes; the 32-byte key at `page+0x460` is assembled
  separately.  [report 141 §10]
* **"The loop reports 118 MB, not the remaining device."**  It reports
  512-byte sectors: 124,583,936 = 59.4 GiB, and the loop's 123,240,448 is the
  device minus the offset.  [report 142 §4]
* **"The DSS `clk rate mismatch` WARN is harmless / is the display bug."**
  Both readings are stale: the low core clock was the display bug (report 133),
  and the WARN itself is now fixed by requesting an achievable rate before the
  parent is raised and recording the real 96 MHz.  [report 142]
* **"The `pb` probe errors are a DSI/link fault."**  They are the BTA
  turnaround of our own reads; the video path is unaffected.  [report 142]

## Unknown / open questions

* **`f`: what the monitor computes from the seed.**  Resolve by reading the
  32-byte key at `page+0x460` from a live QNX (seed at `page+0x440`; QNX ASLR
  moves the mappings, so scan them) or by calling the recovered `smc #1` ABI
  from Linux.  [report 141 §6.3/§7.3]
* **Which RPMB block holds `BOOTCTR_FCTRY`**, and the exact frame byte order on
  this device.  A zero-write authenticated scan (≤512 reads) settles it once the
  key is pinned.  [report 141 §4.2]
* **Why `COMPLETE` returns `0x4015`** on this loader, and whether it matters.
  The write is on media and the boot verifies it.
* **The ACM ~134 KB/s and byte loss.**  Not investigated; the pre-OS `0xF7` loop
  carried the bulk image.
* **The `/dev/kmsg` record with embedded binary fragments** (including a later
  record's `8,seq,usec,-;` header).  Still unexplained; harmless so far.
  [report 136 §5, report 142 §4]
* **A real userland and the writable-root model.**  The root is a static
  `/init` and one text file; `switch_root` is not done.

## Superseded / uncertain

* **"Bulk images must go through QNX/`bb10mt`."**  They can also go through the
  pre-OS `0xF7` loop to region 1, which is now the tested path (report 140).
* **"`--template-ifs` proves the V2 record is used."**  It does not: the
  template already carries the right V1 offset, so both interpretations give the
  same destination for the IFS; the region-1 attempt distinguished them.
* **"Writable eMMC is forbidden."**  Superseded by the operator's session-18
  decision: writable eMMC for **our own** data is allowed (`DECISIONS.md` §18);
  NVRAM, the MCT and dangerous RPMB changes remain forbidden.
* **"The boot-counter top-up needs QNX."**  Superseded in direction: report 141
  shows the key is normal-world and the Linux read path works; the operator has
  chosen Linux-only independence (`DECISIONS.md` §19, pending the one write).

## Artefacts produced

* **Reports**: `139` (misroute + recovery), `140` (region-1 root), `141` (RPMB
  top-up feasibility), `142` (boot-log hygiene).  Data:
  `docs/reports/data/rpmb-probe-0xDDB9D733-2026-10-30.txt`.
* **Images** (gitignored): `0xFB3A9F5D` (region-1 root mount), `0x6CDAB1AD`,
  `0x81109F8E`, `0xDDB9D733` (pbrpmb iterations), `0x24F93563` (pb gating),
  **`0x0D1DA0D3`** (current best: pb gating + pb_reg + DSS clock).
* **Boot logs** (gitignored): `local/logs/boot-20261030-063824.log`,
  `…083742.log`, `…084002.log`, `…084331.log`.
* **Tools**: `tools/build_rootfs.py` + `rootfs/`; `pre_os_ifs_flash.py`
  `--payload`/`--dest-raw`/`--keep-loader` and V1+V2 template patching;
  `initramfs/rpmb.c` → `/pbrpmb`; `initramfs/init.c` `rootfs_try()`;
  `initramfs/console.c` `rootfs`, `run`, `loop_mount_ro()`.
* **Kernel**: `dsi.c` (`pb_probe` gates, `pb_reg` dev_groups) and `dss.c`
  (winchester `clk_round_rate`, real rate); `kernel/patches/
  0001-playbook-winchester-6.15.11.patch` re-exported to 4247 lines/32 files.
* **Recovery data (gitignored)**: `local/backups/region1-pre-session18.img`
  (512 MiB, sha256 `5993096d…`); the vendor components in
  `/home/psyden/Pdrive/bb10mt/`.
* **Docs**: `STATE.md`, `HANDOFF.md`, `KNOWN-ISSUES.md` (traps 55–58), `SAFETY.md`
  §4/§6, `DECISIONS.md` §18/§19, `ROADMAP.md`, `tools/README.md`, and the
  expanded `rpmb-key-kek-vs-fallback-literal.md` resolution.
