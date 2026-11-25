# Local payload inventory — what was in `local/`, and what was deleted

Date: 2026-11-25 (session 70 close).  The survey was read-only; the deletions in
§7 were then approved by the operator and performed.  Disk went from **85 % full
(33 GB free)** to **81 % (40 GB free)**.

All of `local/` is **gitignored** (`local/` in `.gitignore`; `git ls-files local/`
returns 0), so anything removed here is **not recoverable from git**.

> **Sections 1–4 are the survey as taken** (before any deletion) — they record
> what each file *was*, which is the useful part.  **Section 7 is what was
> actually deleted**, and the current state of each directory is in §7's tables.
> Do not read §1–§4 as an inventory of what is on disk now.

The governing rule is already written in `docs/project/STATE.md`:

> "Keep the current image, one known-good fallback, and anything an open
> contradiction still needs; **the durable record is the CRC and what it
> demonstrated, not the file.**"

That is the test used below: a file is *needed* if it is a way back, a current
fallback, or required by an open question.  A file that is only *cited as
history* is already documented by its CRC.

## 0. Never touch — the way back and the working set

| path | size | why |
|---|---|---|
| `local/work/ifs-shim-sealed.ifs` | 7.6 MB | the 1917 IFS; how TabletOS returns |
| `local/work/ifs1917-compressed.ifs` | 3.4 MB | the smaller alternative IFS |
| `local/work/ufs-1917-devuser.ufs` | 2.4 GB | the vendor UFS for hd3 (TabletOS) |
| `local/work/minimal/setB.signed` | 347 MB | the minimal 1917 oracle |
| `local/work/nura/images/…-osk-wvkbd.img` (`07555bde…`) | 597 MB | **the hd3 flasher-restorable base** — the rebake copies it |
| `local/work/kernel-usbdev-cpu1.img` (`0xC675E206`) | 11 MB | known-good recovery image |
| `local/work/kernel-armv7-console-cpu1.img` (`0xC9FEE2D1`) | 10 MB | known-good fallback |
| `boot/chainloader/kernel.img` (`0x3547D613`) | 10 MB | current region 0 |
| `local/toolchains/{armv7-eabihf,armv7-musl-eabihf,qnx660-master}` | 1.6 GB | the compilers |
| `local/pristine/` | 2.1 GB | the pristine 7.3.3 tree — needed for patch export/diffs |
| `local/kernel-tree/` | 5.0 GB | the live build tree |
| `local/work/nura/{pmaports,pmbootstrap,work/packages}` | ~1.9 GB | how the GPU packages are rebuilt |

## 1. `local/work/nura/images` — survey: 8.1 GiB, 14 files (now 1)

Every one is a **Nura root build step**, oldest first.  All 597 MB (the 607 MB
ones are packed two-half containers).  The first column is the sha256 prefix
(measured this session).

| file | sha | refs | what it is | keep? |
|---|---|---|---|---|
| `…-root.img` | `d684e8fd` | 1 | the very first Nura root build (session 54) | history |
| `…-root-hd3.img` | `076bafcf` | 4 | first flash-ready hd3 build | history |
| `…-root-overlay.img` | `1f14915f` | 3 | read-only hd3 + overlayfs root (session 54) | history |
| `…-root-overlay-sgx.img` | `1fcf0cb0` | 3 | overlay + SGX driver (session 55) | history |
| `…-root-pvr.img` | `85b44deb` | 2 | pvr userspace baked in (session 55) | history |
| `…-root-compositor.img` | `9f1fbe11` | 0 | catacomb r10000 first bake | **orphan** |
| `…-root-compositor2.img` | `89399744` | 2 | catacomb on SGX + touch-Y fix (session 56) | history |
| `…-root-apps.img` | `711cd5ec` | 1 | app set baked (session 57) | history |
| `…-root-apps-audio.img` | `f7858110` | 3 | + audio fixes (session 57) | history |
| **`…-root-osk-wvkbd.img`** | **`07555bde`** | **8** | **the flasher-restorable base — CURRENT** | **KEEP** |
| `…-boot.img` | `9ba758b2` | 1 | the 512 MB pmOS_boot partition (unused by our chainloader) | history |
| `both-0xE85217E5-overlay.signed` | `c3ed1f61` | 5 | packed container: overlay image + hd3 (session 54) | history |
| `both-0xA8F0F30C-sgx.signed` | `3be2c73a` | 2 | packed: SGX image + hd3 | history |
| `both-0xAB899C49-compositor.signed` | `426ed972` | 2 | packed: compositor image + hd3 | history |

**Observation:** 13 of the 14 are superseded build steps of the single file we
actually keep.  They are reproducible (each documented with a CRC and the
procedure in reports 240/241/245/246/247/250).  Removing the 13 non-current
builds (including the orphan) frees **7.5 GiB**.

## 2. `local/work/minimal` — 3.3 GiB, 6 files

| file | size | refs | what it is |
|---|---|---|---|
| `setB.signed` | 347 MB | **9** | **the minimal 1917 oracle — KEEP** |
| `set.signed` | 347 MB | 2 | the earlier oracle set (superseded by setB) |
| `rcfs-noufs.rcfs` | 501 MB | 3 | RCFS variant: no-UFS boot |
| `rcfs-minimal.rcfs` | 501 MB | 3 | RCFS variant: pruned `/base` |
| `rcfs-stageA.rcfs` | 501 MB | 3 | RCFS variant: intermediate debloat stage |
| `rcfs-stock.rcfs` | 501 MB | 1 | the stock 1917 RCFS (unmodified) |

**Observation:** the 4 RCFS files are **build intermediates** of
`tools/build_minimal_qnx.py`, but each *is* cited (3/3/3/1 refs) as the input or
output of a debloat stage, so deleting them costs the ability to re-derive that
stage quickly (the script rebuilds them).  Separately:
`local/work/rcfs-1917.rcfs` (**524,288,000 B**, sha256 `c4ab6e34…`, the
*modified* one with the `/accounts/devuser/startup.sh` hook) is the one STATE
names as on media — it lives **outside** this directory and must be kept.
These 4 free **2.0 GB**.

## 3. `local/images` — survey: 1.9 GiB, 94 files (now 4)

**72 `kernel-0x*.img` images.**  Classified by whether the file is named **or its
CRC is quoted** in the docs:

| class | count |
|---|---|
| cited in `docs/project/*.md` (canonical) | **30** |
| cited only in sessions/reports/history | **41** |
| cited nowhere at all | **1** (`kernel-0x411ECD9B-isl9519.img`) |

So these are **not** junk — they are the evidence trail for 30 canonical claims
and 41 historical ones.  Each is ~11 MB; the whole set is only ~800 MB.

**19 `rootfs-region1-*.ext4` (63 MB each = 1.2 GiB)** — every region-1 userland
build: audio experiment chain (`e5`, `e9pads`, `e9mcbsp3`, `linout`, `stereo`),
`rndis-ssh`, and the wl1283 chain (`elpcam`, `nvs`, `scan`, `ht40`, `assoc`,
`final`, `bt`, `keydiag{2,3}`, `net`).  STATE names
**`rootfs-region1-wl1283-bt.ext4`** (`49e318bf…`) as the staged current one; the
on-media copy is sha256 `20a5edd3…`.  The other 18 are superseded steps.

## 4. `local/work` — the rest (excluding nura/, minimal/, src/)

| path | size | what it is | referenced? |
|---|---|---|---|
| `modules-7.3.3-stage` | 383 MB | staging dir for the ARMv7 module install | no (installed set is on hd3) |
| `llvmpipe` | 1.0 GB | llvmpipe/Mesa/LLVM APKs + tree | session 60/254 only — **and llvmpipe is NOT in the shipped state** (measured: the live `swrast_dri.so` has 0 llvmpipe strings, 0 LLVM symbols) |
| `mesa-pvr-src` | 197 MB | src/gallium/drivers/pvr… (session 70, mine) | **no** — the pinned tarball is in `cache_distfiles` |
| `carve` | 111 MB | session-62 zImage/DTB carve workspace | session 62 |
| `sgxdrv` | 101 MB | SGX driver extraction | session 55 |
| `apks-pvr` | 44 MB | pvr APKs staging | session 55 |
| `rebake-check` | 40 MB | extracted APK binaries for version comparison (session 70, mine) | **no** |
| `modstage` | 29 MB | older module staging | no |
| `x11check` | 8 MB | pulled .so files (session 70, mine) | **no** |
| `s74`, `pvrdri`, `ifs1917-tree`, `arm-prefix`, `sgx-um`, `export67`, `modules*`, `rootfs-extras`, `bin`, `qnx-libs`, … | ~30 MB total | assorted, mostly small | various |

## 5. `local/logs` — DELETED (session 70 close)

13 boot logs (864 KB, 2026-10-30 … 2026-11-09) deleted on the operator's
instruction: they predate the working dual-core image, and a fresh capture beats
a previous session's log.  Capture destination is still
`local/logs/boot-YYYYmmdd-HHMMSS.log` via `tools/pb_bootlog.sh`
(`DEVELOPMENT.md` §"Bench"); it is gitignored and ephemeral by design.

**⚠ Sixteen documents cite a `local/logs/boot-*.log` that no longer exists.**
The claims they make still stand (they were measured when written, and the
decisive values are quoted inline), but the whole capture cannot be re-read.
If a claim needs re-litigating, **re-capture on the current image** — the
working state is dual-core with the pitch fix, so a log from 2026-10 or early
November describes a different kernel and root.

| deleted | cited by |
|---|---|
| `boot-20261030-063824.log` | session-18, STATE-HISTORY, report 140 |
| `boot-20261030-083742.log` | session-18 |
| `boot-20261030-084002.log` | session-18 |
| `boot-20261030-084331.log` | session-18, KNOWN-ISSUES §3, report 142 |
| `boot-20261030-101031.log` | session-19 |
| `boot-20261030-110840.log` | (uncited) |
| `boot-20261107-095720.log` | session-29, report 182 |
| `boot-20261107-7.3.3-first.log` | session-30 |
| `boot-20261107-7.3.3-final.log` | session-30 |
| `boot-20261108-sdio-audit.log` | session-32, report 186 |
| `boot-H1-0x29784F48.log` | session-35, DECISIONS, KNOWN-ISSUES, report 192, STATE-HISTORY |
| `boot-0x0DC36A11-1917mmc5.log` | session-37, report 196, STATE-HISTORY |

## 6. `docs/captures` screenshots

Only two are tracked, and both are referenced: `session68-screen-black.png`
(4 KB) and `session68-screen-restored.png` (84 KB).  They are evidence for
trap 346.  **Nothing to clean.**

## 7. What was actually deleted (session 70 close, operator-approved)

The operator's retention rule: **keep the last working single-core image, the
latest dual-core image, the read-back instrument, the QNX assets, and the
freshest hd3 filesystem.**  Applied:

| deleted | count | freed |
|---|---|---|
| kernel images (`local/work/kernel*.img` + `local/images/kernel-*.img`) | **117** | 1.19 GB |
| superseded hd3 root builds in `local/work/nura/images` | **13** | 7.52 GB |
| superseded region-1 rootfs builds in `local/images` | **18** | 1.15 GB |
| boot logs in `local/logs` | **13** | 864 KB |
| **total** | **161 files** | **~9.9 GB** |

Disk went from **85 % → 81 %** used (33 GB → 40 GB free).

### Kernel images — exactly these five are left

| file | CRC | role |
|---|---|---|
| `boot/chainloader/kernel.img` | **`0x3547D613`** | **the latest dual-core image** (current region 0) |
| `local/work/kernel-fastcl-nosmp.img` | **`0x8B83421F`** | **the last working single-core image** (`DECISIONS` §91 names it "the reliable single-core fallback") |
| `local/work/kernel-readback-quiet.img` | **`0x5BBCD22E`** | **the read-back instrument** — recovers the DRAM ring from a hung boot (used as recently as session 66, report 264) |
| `local/work/kernel-armv7-console-cpu1.img` | `0xC9FEE2D1` | the `HANDOFF` "known-good fallback" |
| `local/work/kernel-usbdev-cpu1.img` | `0xC675E206` | the `HANDOFF` recovery image (what cleared the session-65 panic loop) |

Five images in total: four live in `local/work/` (they were candidates), and
`0x3547D613` lives in `boot/chainloader/` and was never a deletion candidate.

### Also deleted, and worth noting

* `kernel-readback-quiet-wdt.img` (`0x0CACDC62`) — the rejected trap-322
  experiment (WDT2 from the chainloader, which leaves the timer **stopped**).
  It was cited nowhere.  Good riddance: it is the one image that must never be
  flashed.
* The 512 MB `blackberry-playbook-boot.img` (the unused `pmOS_boot` partition).

### Kept against the rule, deliberately

* **`local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img`** — it is
  the **only** flasher-restorable hd3 filesystem and the **input to the owed
  rebake** (`tools/rebake-hd3.sh` line 60).  Note it is the session-59 state
  (`catacomb 1.5.0-r10000`, `epitaph 1.4.0-r0`); **a fresher hd3 image does not
  exist as a file** — the live root with the pitch fix is only on the unit, which
  is what the rebake produces.
* **All QNX assets** (`*.ifs`, `*.rcfs`, `*.ufs`, `*.signed`) per the operator —
  they are the way back and the oracle.

### Still on the table, not done (no decision yet)

| item | size | note |
|---|---|---|
| `local/work/llvmpipe` | 1.0 GB | llvmpipe APKs; measured absent from the shipped state |
| `local/work/minimal/rcfs-*.rcfs` (4) | 2.0 GB | debloat intermediate stages, each cited |
| my session scratch: `mesa-pvr-src`, `rebake-check`, `x11check` | 245 MB | no references; the tarball is in `cache_distfiles` |
| `modules-7.3.3-stage`, `carve`, `sgxdrv`, `apks-pvr` | 638 MB | build scratch |
| `local/work/minimal/set.signed` | 347 MB | the earlier oracle (setB is current) |

## 8. Done this session: the musl toolchain moved

It was in `local/work/arm-linux-musleabihf-cross` (my mistake — I unpacked it
where I downloaded it).  Now at **`local/toolchains/armv7-musl-eabihf`**, where
`tools/env.sh` documents toolchains belong, and `tools/env.sh` now:
* discovers it (`PB_MUSL_TOOLCHAIN` overrides), and
* exports **`MUSL_CC`** when present — deliberately **not** on `PATH`, since it
  is not the default compiler.

`tools/env.sh --check` prints all three.  Verified: rebuilding
`tools/bench/gpu/dbmfix.c` with `$MUSL_CC` reproduces the deployed
`dbmfix.so` byte-for-byte (`cc302b42…`).
