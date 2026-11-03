# Session 24 (absolute) — the offline IFS writer (milestone 1)

> Numbering: absolute `24`; the reports call this Linux-era **session 18**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-03
Unit state at the end: **PlayBook OS 2.1.0.1917 running from our IFS, our
edited `/base`, and the stock 1917 UFS.**  `/dev/hd1t179 on /base type qnx6`,
`/base/etc/os.version` = `2.1.0.1917`, `/proc/boot/rfs_validator` = 8,759 B,
`/dev/hd3 on /` = stock `PlayBook_2.1.0.1917.5.ufs`; USB `0fca:8012`, login
**`devuser`** (root denied by `qconnDoor`).  The operator's
`/accounts/devuser/startup.sh` (4,566 B, sha256 `8acf568f…`) runs at boot and
topped `BOOTCTR_FCTRY` up (`0x2E → 0x32`, read back).  No RPMB write by the
agent.  See `STATE.md`, reports 169/170 and `IMAGE-REBUILD-GUIDE.md`.

## Goal of this session

`docs/project/IFS-REBUILD.md` §6: the imagefs emitter (`tools/ifs_build.py`),
the `rfs_validator` mount shim, the reseal tool's image length, and the boot
test.  Then, at the operator's direction, the `/base` startup hook, the UFS
(the ROOT V1.4 was rejected; the stock one works), and the operator's
`BOOTCTR_FCTRY` top-up.

**Result: all four milestones done, plus the `/base` hook and the UFS.**  The
stock 2.1.0.1917 image rebuilds; `dumpifs` reports the same 99 dirents and
`tools/tests/test_ifs_build.py` passes.  The shim was built and tested
read-only on the reference.  The reseal sets the length and produces a
CRC-verified image.  The operator chose to ship the **1917 IFS + 1917 RCFS**
as a matched pair; the pair booted.  Then `/base` was edited (one guarded call
to `/accounts/devuser/startup.sh`), the **stock** 1917 UFS was flashed (the
ROOT V1.4 UFS was rejected for its door), and the operator's script ran at boot
and topped the counter up.

## Confirmed (measured or read in a file)

* The vendor image is `/home/psyden/Pdrive/bb10mt/PlayBook_2.1.0.1917.4.ifs`,
  **3,276,800 B (0x320000)**.  Its prefix `0x0..0x44910` is byte-identical to
  the first `0x44910` bytes of `dumpifs -u` output (session measurement).
* `dumpifs -u OUT STOCK.ifs` writes the **uncompressed container**
  (7,900,540 B), not just the imagefs.  The imagefs header sits at `0x44910`,
  `image_size = 0x74446C`, `hdr_dir_size = 0x1748`, `dir_offset = 0x5c`,
  `mountpoint = "/"`, `flags = 0x04`, `boot_ino[0] = 0xE0000002`,
  `script_ino = 3`.
* There are **98 imagefs dirents after the root** = 72 files + 26 symlinks;
  directories are implicit (only the root dirent exists).  The dirents end at
  `0x1744`, followed by a 4-byte zero word; `hdr_dir_size` includes it
  (`0x1748`).
* The common `image_attr` is **24 bytes** (u16 size, u16 extattr, then ino, mode,
  gid, uid, mtime).  Dirents are 4-byte aligned; `attr.size` is the full length.
* Symlink dirents store `sym_offset = strlen(path)+1`, `sym_size = strlen(target)`,
  the path, then the target **with a NUL terminator** (the extra byte is inside
  the dirent size).  The listing's `----  size  name -> target` size is the
  target length.
* The image trailer is at `image_size-4` and is the **two's complement of the
  32-bit little-endian word sum** of `[header, trailer)`, tail zero-padded:
  `sum(all words including the trailer) == 0 mod 2^32`.  Verified on the stock
  imagefs (`sum = 0xBD87BC93`, stored `0x4278436D`) and on a `mkifs`-built tiny
  image (`stored = 0xE57D8557 = -0x1A827AA9`).
* `dumpifs` selects decompression from `startup_header.flags1` at container
  offset **`0x80e`** (mask `0x1c`).  The stock value is `0x0d` (VIRTUAL|UCL).  An
  uncompressed imagefs must clear those bits or `dumpifs` segfaults (rc 139,
  no output).  With `flags1 = 0x01`, `dumpifs` reads the same stream and prints
  the same `Checksums: image=0x4278436d startup=0x90310cb3`.
* All stock file data offsets are **absolute multiples of `0x800`** (relative to
  the `0x44910` header they are `0x...ef0`).  Successive files are not a simple
  align-to-0x800; mkifs leaves variable gaps, so the builder reproduces the
  reference offsets rather than deriving the rule.
* The stock root dirent has three non-zero padding bytes (`2d 57 6c`) between
  the empty path and the next dirent; every other dirent's padding is zero.
  They look like mkifs buffer junk (`-Wl<`).
* `tools/ifs_extract.sh` extracts 72 files / 11 directories / 6,581,929 B in
  0.4 s; every file's bytes, mode and mtime match the image exactly.  It creates
  **no symlinks** (0 in the tree, 26 in the listing).
* Because the tree's data/modes/mtimes match, the rebuilt image is
  **byte-identical** to `dumpifs -u` of the stock image except: the 3 root
  padding bytes, the imagefs trailer, `flags1` (compress bits cleared), and the
  startup checksum that `flags1` invalidates.
* The **startup checksum** is a *stored* word at `startup_off+startup_size-4`
  (`0x4490c` here, i.e. the 4 bytes before the imagefs); the field is the same
  two's-complement word sum over `[startup_off, field)`.  Verified on both the
  1917 and 4869 images (`sum+stored == 0`).  `dumpifs` only prints it; it does
  not compute or verify anything (report 168 §0/§Q2).
* **The shim works on the reference.**  `/tmp/rfs_validator_shim -vc` printed
  `mounting /dev/hd1t179 at /base`, then `/base already mounted`, and exited 0;
  `df -g /base` after it was the same mount (127,996 blocks, flags `0x21`,
  `crypto=enable,sync=optional`).  Read-only; no media write.
* **The reseal model is exact.**  Resealing the sealed 4869 stock
  (`local/images/ifs0.sealed.bin`, `os_type` 6, CRC `0x116B2562`) reproduces it
  **byte-for-byte**.  The new `--length auto --append-os-trailer` writes
  `LoadEnd = LoadStart + size`, the end-8 cookie, and the CRC; the shim image
  (`0x788D84`, CRC `0x38EE7B46`) passes the full structural + CRC verification.
* The shim image keeps the stock reference slot (`proc/boot/rfs_validator` at
  `0x6a0800`, now 8,759 B) and therefore the stock imagefs size (`0x74446C`)
  and the startup's `imagefs_size`/`ram_size`; only `stored_size` is the stale
  compressed value.
* `tools/ifs_build.py check` on the rebuilt image: `CHECK PASSED — 99 entries,
  names/sizes/modes match, checksum line present`.
  `tools/tests/test_ifs_build.py`: `PASSED` (synthetic selftest + full round
  trip).  `tools/tests/test_offline.py`: still 87 checks, exit 0.
  `tools/docs_audit.py`: `clean`.
* **The matched pair was flashed and booted** (report 169).  Operator decision:
  the 1917 IFS goes with the **vendor 2.1.0.1917 RCFS** (`PlayBook_2.1.0.1917.1.rcfs`,
  sha256 `8ccf518c…`), because a 4869 `/base`'s libraries do not match a 1917
  IFS.  One container `local/work/pair.signed` (363,790,928 B, sha256
  `66f4e301…`; RCFS runs exactly the vendor's, IFS run 16/121) was built with
  `bbusb.py pack -v 1,2 -s`.
* **`bbusb.py flash` works on hardware** (its first run): `--signed`, as the
  user, `22 232` frames, `0x40F9` signature → `0x4006`, `COMPLETE 0x40C0` →
  `0x4006`, `done`, exit 0.
* **Boot evidence** (read-only, over the door): USB `0fca:8012` ("RIM Network
  Device"), `169.254.0.1` up; `uname` = `QNX localhost 6.6.0
  2014/03/19-01:28:41EDT OMAP4430_ES2.2_HS_Winchester_Rev:07 armle`;
  `/dev/hd1t179 on /base type qnx6`; `/base/etc/os.version` = `2.1.0.1917`;
  `/proc/boot/rfs_validator` = **8,759 B**; ports 22, 4455 (qconn), 80, 443
  open.
* **The 1917 OS login is `devuser` only**; the 4869 root key is rejected
  (operator).  `tools/pbssh.sh` now takes `PB_USER` (`pbscp.sh` already did).
* **The `/base` edit is exactly one hunk.**  Against the pristine vendor RCFS,
  `scripts/startup.sh` gains 8 lines after `start_runlevel FINAL` (the guarded
  `/accounts/devuser/startup.sh &` call); pristine 61,238 B → edited 61,713 B.
  The earlier root-SSH/key block was reverted (the diff has no other hunk).
* **The ROOT V1.4 UFS is broken for the door.**  The UFS from
  `PlayBook_2.1.0.1917_ROOT_V1.4.EXE` (split → `.0.signed` 1,152,385,064 B →
  `unpack --only 5` → 2,550,202,368 B) was flashed with the IFS+RCFS
  (1,156,841,504 B, 70,694 frames, exit 0) and booted, but BerryShell never
  authenticated (`Handshake ended before authentication`), so SSH stayed
  closed.  Removing our script from it and reflashing (pair3) gave the same.
* **The stock UFS works.**  `PlayBook_2.1.0.1917.5.ufs` (2,550,202,368 B,
  sha256 `0c0f7d77…`) flashed UFS-only (740,622,928-byte container, 45,259
  frames, exit 0); the door authenticated, the key uploaded, and `devuser` SSH
  worked.  `autorun::0` appears twice and `autorun::1` zero times.
* **Root is denied by `qconnDoor`.**  `/base/bin/qconnDoor` contains
  `DenyUsers=root`; the live sshd is launched with
  `-f /dev/null … -o AllowUsers=devuser -o DenyUsers=root`.  `/base/scripts/
  startup.sh`'s `tools_qconn_sshd` is only `waitfor … && /usr/sbin/sshd`.
  Root is the post-boot `root.sh`/watchdog method, not a `/base` edit.
* **The operator's startup script and the top-up work.**  They added
  `/accounts/devuser/startup.sh` (4,566 B, sha256 `8acf568f…`); the boot log
  has `bootctr: wrote 0x32, readback ok (was 0x2E (decimal 46))`.  The previous
  counter was 46, now 50.
* **Final state:** `/dev/hd3 on /` (stock 1917 UFS), `/dev/hd1t179 on /base`
  (edited 1917 RCFS, `os.version` 2.1.0.1917), `/proc/boot/rfs_validator`
  8,759 B (our IFS), `devuser` SSH.  No RPMB write by the agent.

## Inferred

* `dumpifs` **does not compute** either checksum; it `fread`s and prints a
  stored word, and it exits 0 even on malformed input (report 168 §0).  The
  values it prints are therefore fields, not verification results.
* The 3 root-padding bytes are uninitialised mkifs memory, not a field: they sit
  after the root's NUL path inside the dirent's 4-byte padding, and are the only
  non-zero padding in 99 dirents (report 168 §Q5 says the builders always write
  `extattr_offset = 0`, so this is padding residue).
* Reproducing the reference offsets exactly is safe because the imagefs reader
  uses the dirent offsets; the pack fallback is only for a size increase.
  Report 168 §Q4 derived mkifs' rule (alignment in address space), and the
  builder now uses it.

## Dead ends and wrong assumptions

* I first read the startup header as **big-endian** from IFS-REBUILD §2.  It is
  **target-endian (little-endian)**: `eb 7e ff 00` is `0x00ff7eeb`; `flags1` is
  at `0x808+6 = 0x80e`, not `0x814`.  The section's "big-endian" wording was
  wrong and is corrected.
* I assumed `image_attr` was 20 bytes; the root dirent's size (28) forced the
  correct **24 bytes**.  That one error shifted every dirent interpretation.
* I first treated the listing's offsets as imagefs-relative.  They are
  **container-absolute** (file `0x46800`), while dirent `file.offset` is relative
  to the header (`0x1ef0`); the first build came out `0x44910` too big.
* I encoded symlink targets without a NUL; one dirent (`dead_battery.bmp`, path
  length 36) then came out 92 bytes instead of 96.  Adding the terminator fixed
  it and left the other 25 unchanged (their sizes were already rounded up).
* I initially fed `dumpifs -u` output straight back to `dumpifs` and got a
  **segfault with no output**; the compression flag must be cleared first.
* A full-disk `find` for the vendor components timed out; the files are under
  `/home/psyden/Pdrive/bb10mt/` (the operator pointed at it).
* Plain `dumpifs` listings carry no modes/uid/gid/mtime; my first `check` compared
  invented defaults and reported 72 false mtime failures.  Reference entries now
  carry `has_meta`, and the comparison skips fields the listing did not state.
* **I used the listing's `Startup-header` size (`0x100`) as `startup_size`.**  That
  is the header *struct* size, not the startup block: the real value is the field
  at `startup_off+0x20` (`0x44108`).  The first checksum-recompute therefore
  wrote at `0x904` and corrupted four bytes of the startup header, while
  `0x4490c` stayed stock.  The byte diff (`0x904..0x907`) caught it; the code now
  reads the field, and `load_prefix` documents the distinction.
* I first repacked the shim image because the replacement was smaller; that
  changed the imagefs size and left the startup's `imagefs_size`/`ram_size`
  stale.  A smaller replacement now keeps its reference slot (freed bytes zero),
  which is why the shim image has the stock imagefs size.
* **`pkexec` was the wrong tool for the flash.**  The USB node is
  `crw-rw-rw-+` with `user:psyden:rw-`, so the user can flash.  `pkexec` ran as
  root, which lacks the user's `pyusb` ("pyusb is not installed"), and killing
  the wrapper left an **orphan root `bbusb.py`** holding the interface
  (`set_configuration(1)` / `claim_interface(0)` → `Resource busy`).
* The orphan left the **ramloader wedged**: it still enumerated (`0fca:8001`)
  but every `0x80EF`/`0xEF` and every transfer timed out (`-110`).  A
  **power-cycle** cleared it; no media had been written (the flash failed at
  loader init, before PreFlash).  `ps -ef | grep bbusb` is the check.
* I started the flasher only after seeing the BootROM node; the tool's own
  open window is 10 s and the unit's bootrom window is short.  The working
  pattern is to arm a `lsusb | grep 0fca:0001` wait, then run the flasher — or
  run it with the unit already in the ramloader.
* I first planned the boot test over the **4869 `/base`**; the operator
  corrected the plan: ship the **matched 1917 RCFS** with the 1917 IFS.  The
  old plan is superseded.
* **The root-SSH `/base` edit was wrong.**  I installed `/root/.ssh/
  authorized_keys` and set `PermitRootLogin yes`, but `/base/bin/qconnDoor`
  launches sshd with `-f /dev/null -o DenyUsers=root`; the config and the key
  are ignored.  Reverted (the diff is now only the devuser call).
* **The ROOT V1.4 UFS was the wrong UFS.**  It boots, but its rooted
  `qconnDoor` never completes the door handshake; removing our script did not
  help.  The stock `.5.ufs` works.
* **I lazy-unmounted the FUSE images** when a terminal/kate had their cwd
  inside; it left the FUSE daemons holding the images and later mounts failed.
  The rule (operator): wait for them to close, then `fusermount -u`.
* The operator's `bench/devuser-startup.sh` is **not** the deployed 4,490-byte
  script (`b94d9f96…` vs `c265cfbb…`); the live one is authoritative.  The
  operator's current 1917 script is 4,566 B (`8acf568f…`).

## Unknown / open questions

* **Does RIM's `rifsboot` honour `flags1`/an uncompressed stream?**  **Answered:
  yes.**  The uncompressed image booted (report 169).
* **Does anything enforce the startup checksum?**  It is a stored word (report
  168 §Q2); the builder recomputed it after the `flags1` change and the image
  booted, so it is either not enforced or the recompute is correct.
* **What does `stored_size` mean on an uncompressed image?**  **It did not
  block**: the stock compressed value (`0x3150c0`) was left in place and the
  image booted.
* **`BOOTCTR_FCTRY` now** — **`0x32` (50)**, written and read back by the
  operator's startup script (`0x2E` before).  `devuser` cannot read `/dev/rpmb`
  directly; read `.bootctr.log`.
* **`/base/scripts/startup.sh`** — root-only; `devuser` cannot read it.  The OS
  booting and the `.bootctr.log` entry are the evidence it ran.
* **Root SSH** — not solved as a `/base` edit and not needed: `qconnDoor`
  enforces `DenyUsers=root`; root is the post-boot operator method.
* **Bloatware removal** — **no longer the operator's goal** (2026-11-03).  **The
  next goal is to boot BB10 on the PlayBook** (operator, 2026-11-03).  The
  operator's facts, recorded in `BB10-PORT.md` and **not yet verified by us**:
  BB10 reports as **QNX 8** (not 6.6/7.0); the **Z10 is OMAP-based** (OMAP4460
  code compatible with the OMAP4430); early **BB10 10.0.9.1103 already runs** on
  the PlayBook; the BB10 **`/base` is an RCFS image** (`bb10mt qnx6 mount` does
  not work; dump/edit/repack); a **QNX 8 SDP/toolchain** is needed.  The target
  image is not decided.

## Superseded / uncertain

* `IFS-REBUILD.md` §2 said the component is "3,280,000 B" and the startup header
  is "big-endian" with `ver 0x000d`.  Measured: **3,276,800 B**; the header is
  little-endian, `version = 1`, `flags1 = 0x0d` at `0x80e`.  Corrected in place
  (this is a product document, not another session's note).
* `IFS-REBUILD.md` §4 listed the **image** checksum as unsettled; it is now
  solved and moved to §3.  The **startup** checksum is also solved (stored word
  at `startup_off+startup_size-4`).
* The design note said "append to RIM's **unchanged** prefix"; the builder
  changes `flags1` (one byte) and the startup checksum field it invalidates (4
  bytes).  The other 0x4490B bytes are identical to stock.
* **The earlier "boot over the stock 4869 `/base`" plan** was superseded by the
  operator's decision to ship the matched 1917 IFS + 1917 RCFS pair.
* **`bbusb.py flash` was documented as "not run on hardware"**; it has now
  written this unit four times (reports 169/170).  `pre_os_ifs_flash.py`
  remains the alternative, not the required tool.
* **"The UFS is still 4869"** (earlier in this note) is superseded: the stock
  1917 UFS was flashed last.
* **The ROOT V1.4 UFS and the root-SSH `/base` edit** were tried and rejected
  (§"Dead ends").

## Artefacts produced

* `tools/ifs_build.py` — new: `listing` / `build` / `check` / `selftest`, plus
  the address-space pack rule and the startup-checksum refresh.
* `tools/rfs_validator_shim.c` — new: the mount shim (8,759 B with
  `ntoarmv7-gcc`).
* `tools/89-crc_tool.py` — `reseal --length auto --append-os-trailer`; the
  ECDSA trailer shape is now informational, not part of the CRC-path verdict.
* `tools/pbssh.sh` — `PB_USER` (the 1917 OS needs `devuser`).
* `tools/tests/test_ifs_build.py` — new: synthetic + full 1917 round trip,
  reseal-with-length, and the shim build.
* `docs/reports/168-ifs-format-internals-2026-11-03.md` — static RE of
  `dumpifs`/`mkifs` (checksums, parsing, layout, defaults).
* `docs/reports/169-ifs-shim-boot-2026-11-03.md` — the flash and boot evidence.
* `docs/reports/170-base-hook-ufs-swaps-stock-baseline-2026-11-03.md` — the
  `/base` hook, the UFS experiments, the door/root findings, the top-up.
* `docs/project/IMAGE-REBUILD-GUIDE.md` — **the repeatable procedure for any
  `.ifs` + `/base` (+ UFS)**, the operator's requested runbook.
* `docs/project/IFS-REBUILD.md` — §1 status, §2 sizes/endianness, §3 format
  facts, §4 unknowns, §5 as-built, §6 milestones 1–4 done.
* `docs/project/STATE.md`, `docs/project/HANDOFF.md`, `ROADMAP.md`,
  `KNOWN-ISSUES.md` (traps 71–77), `DECISIONS.md` §22 — the final state.
* `tools/README.md`, `docs/project/DEVELOPMENT.md` — the devuser login, the
  hardware-proven flasher, no-`pkexec` and the 10 s window.
* Working files (gitignored, not artefacts): `local/work/ifs1917-tree/`,
  `local/work/ifs1917.imagefs` (`dumpifs -u` output), `local/work/stock.list`
  (`dumpifs -vv`), `local/work/rebuilt.ifs`, `local/work/final.ifs`,
  `local/work/ifs-shim-sealed.ifs` (`0x788D84`, CRC `0x38EE7B46`),
  `local/work/rcfs-1917.rcfs` (edited RCFS), `local/work/pair2.signed`,
  `local/work/pair3.signed`, `local/work/ufs-stock.signed`, and the extracted
  `local/work/ufs-root-v14/` + `local/work/ufs-orig/` UFS copies.
* Device-side: `/tmp/rfs_validator_shim` was on the pre-flash OS; the running
  unit has the shim in the IFS.
* Media written: region 0 (IFS + RCFS + UFS, several flashes).  **No RPMB write
  by the agent**; the OS's own startup script performs the operator-directed
  top-up.
