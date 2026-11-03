# IFS rebuild — working notes for the 2.1.0.1917 boot image

Everything a session needs to build a **modified** 2.1.0.1917 IFS.  Written
at the end of session 20 (absolute) after establishing feasibility.

**Status (2026-11-03, session 24).**  **All four milestones are done, the image
boots, and the `/base` hook works.**  `tools/ifs_build.py` rebuilds the stock
uncompressed imagefs (dumpifs accepts 72 files + 26 symlinks and prints a
`Checksums:` line); the `rfs_validator` shim mounts `/base`; the reseal sets
the image length, `os_type` and the CRC; and the set booted:
`/dev/hd1t179 on /base type qnx6`, `/base/etc/os.version` = `2.1.0.1917`,
`/proc/boot/rfs_validator` = 8,759 B (our shim), OS up with its web/qconn/SSH
services.  Report 169 has the boot evidence.  `/base` carries one guarded hook
that runs `/accounts/devuser/startup.sh`; the operator added the script and the
`BOOTCTR_FCTRY` top-up works (report 170).  The UFS is the vendor 1917
`.5.ufs` **with the operator's `autorun::0` change**; the ROOT V1.4 UFS was rejected (its door never authenticates).  The
1917 OS logs in as **`devuser`** only (`PB_USER=devuser tools/pbssh.sh …`).
**The repeatable build/flash procedure for any image set is
`docs/project/IMAGE-REBUILD-GUIDE.md`.**  Next: a custom 1917 `/base`
(bloatware removal).

**Goal.** Replace `proc/boot/rfs_validator` (or the `.script` line that runs it)
so `/base` no longer has to be signed, then reseal the image for the factory
path.  Secondary goal: make image building repeatable for bloatware removal and
for later PlayBook OS image porting.

## 1. Environment (all of this was run, not guessed)

| what | where / how |
|---|---|
| QNX 6.6 SDP | `local/toolchains/qnx660-master` — `. tools/env.sh` puts it on PATH (`QNX_HOST`, `QNX_TARGET`) |
| ARM QNX compiler | `ntoarmv7-gcc` 4.7.3 in `$QNX_HOST/usr/bin` — builds QNX-native ARM **programs** |
| IFS tools | `dumpifs`, `mkifs` in `$QNX_HOST/usr/bin` |
| format headers | `$QNX_TARGET/usr/include/sys/image.h` (imagefs), `.../sys/startup.h` (startup header) |
| QNX docs (offline) | utilities guide is zipped: `$QNX_TARGET/usr/help/eclipse/plugins/com.qnx.doc.neutrino.utilities_*/doc.zip` → `topic/m/mkifs.html` |
| vendor components | `$BB10MT/PlayBook_2.1.0.1917.{0.mbr,1.rcfs,2.mbr,3.sig,4.ifs,5.ufs}` and the `.signed` container (outside the repo) |
| unpacked `/base` tree | `<bb10mt-dev-tree>/2.1.0.1917-RCFS/` |
| extracted IFS tree | `tools/ifs_extract.sh PlayBook_2.1.0.1917.4.ifs <dir>` → 72 files / 11 dirs / 6,581,929 B in 0.4 s |
| a few key binaries | `local/proprietary/device-binaries/ifs-1917/` (gitignored payload: `rfs_validator`, `sanity`, `sanityChecking`, `.script`) |
| container tooling | `tools/bbusb.py pack`/`unpack`/`split`/`parse` — byte-identical to bb10mt, no device and no vendor binary needed |
| media verifier | the loader's `0xC4` (read/verify a range) and `0xDC` (range hash of N blocks) — report 164 addendum 9; they check what is on media **without booting**, and `0xDC` is a before/after fingerprint (its algorithm is not zlib CRC-32) |
| offline checks | `tools/tests/test_offline.py` — stubbed device, no USB |

`$BB10MT` is your bb10mt working directory (defined in `DEVELOPMENT.md` §1).

## 2. The stock image

```
0x000000  0x808    boot prefix      (mkifs bootfile output; unchanged)
0x000808  0x100    Startup-header   sig 0x00ff7eeb (LE), ver 1, flags1=0x0d
                                    (VIRTUAL|COMPRESS_UCL), flags2=0
0x000908  0x44008  startup.*        RIM's own: "rifsboot", "imagefs", ReadMCTData,
                                    ReadPCBNumber, get_omap_info
0x044910  ...      imagefs          LZ-compressed: header + dirents + data + trailer
```

* **The imagefs is one compressed stream** (`+compress`, UCL via
  `STARTUP_HDR_FLAGS1_COMPRESS_MASK` bits `0x0c` in `flags1` at `0x80e`), which
  is why `dumpifs` prints *decompressed* offsets (`0x785800` for a
  3,280,000-byte file) and why **in-place patching is impossible**.
* Sizes (measured on the vendor `PlayBook_2.1.0.1917.4.ifs`, session 24):
  component **3,276,800 B (0x320000)**; the imagefs is **`0x74446C`
  (7,618,668 B)**, so the uncompressed container is **7,900,540 B**.  The
  extracted tree is 6,581,929 B (72 files).  (`§2` previously said
  "3,280,000 B" — that was 3,200 high.)
* The imagefs `image_size` is the length from the header to the end of the
  4-byte trailer; `hdr_dir_size` includes a 4-byte zero dirent terminator.

## 3. Format facts (from `sys/image.h`)

`struct image_header`: `signature[7]` = `"imagefs"`, `flags`, `image_size` (header
→ end of trailer), `hdr_dir_size` (header → last dirent), `dir_offset`,
`boot_ino[4]`, `script_ino`, `chain_paddr`, `spare[10]`, `mountflags`,
`mountpoint` (flexary).  `IMAGE_FLAGS_*`: `0x01` big-endian, `0x02` read-only,
`0x04` inode bits valid.

Dirents (`union image_dirent`), all `unsigned long` = 4 bytes on ARM32.  The
common `image_attr` is **24 bytes** (u16 text size, u16 extattr, then ino, mode,
gid, uid, mtime); the whole dirent is 4-byte aligned and `attr.size` is its
full length:

| kind | fields after `image_attr` |
|---|---|
| `image_attr` (common) | `size` (u16, size of the dirent), `extattr_offset` (u16), `ino`, `mode`, `gid`, `uid`, `mtime` |
| file | `offset` (from header), `size`, then NUL-terminated path (no leading `/`) |
| dir | path only (the root dirent has an empty path and is 28 bytes) |
| symlink | `sym_offset` = `strlen(path)+1` (u16), `sym_size` = target length (u16), path, link text **+ NUL** (all included in the dirent size) |
| device | `dev`, `rdev`, path |

The dirent list ends with a **4-byte zero word**; `hdr_dir_size` includes it.

`struct image_trailer { cksum; }`.  The algorithm is now known (verified against
both the stock imagefs and a `mkifs` image, and in report 168 §Q1):
**`cksum` is the two's complement of the sum of the 32-bit little-endian words
in `[header, trailer)`**; a trailing 1–3 bytes are dropped (mkifs folds whole
words only).  Equivalently, `sum(all words including the trailer) == 0 mod 2^32`.
The trailer sits at `image_size-4`, after the last file (padded to 4 bytes).

* File data offsets in the stock imagefs are **absolute multiples of `0x800`**,
  so relative to the image header at `0x44910` they are `0x...ef0`.  mkifs
  aligns in the image's **address space** (`LoadStart + file offset` to
  `max(page_align, 0x1000)`; report 168 §Q4), which is the same phase.  The
  builder reproduces the reference offsets exactly, lets a **smaller**
  replacement keep its slot (the freed bytes stay zero, so the imagefs size and
  the startup fields do not change), and repacks on the same address-space rule
  only when a file grows.
* `dumpifs` selects decompression from `startup_header.flags1` at container
  offset **`0x80e`** (mask `0x1c`).  An uncompressed imagefs **must** clear
  those bits, or `dumpifs` tries to UCL-decompress it and segfaults.  The
  builder writes `flags1 = 0x01` (VIRTUAL) by default (`--keep-compress-flag`
  keeps the stock value).
* The **startup checksum** that `dumpifs` prints as `startup=...` is a *stored*
  word at `startup_off + startup_size - 4` (for this image `0x4490c`), and it
  is the same two's-complement word sum over `[startup_off, checksum)`.
  `mkifs` writes it; nothing recomputes it, so clearing `flags1` invalidates it.
  The builder **recomputes it** after the flag change.  (Report 168 §Q2.)
* `dumpifs` locates the imagefs by seeking to `startup_off + startup_size` and
  scanning for `"imagefs"`; it does not use `imagefs_paddr`/`imagefs_size` and
  does not validate `image_size`.  It prints the checksum words without
  computing them (report 168 §Q1/§Q3).

`struct startup_header` (`sys/startup.h`) marks each field `I` (IPL sets), `S`
(startup sets) or `IS`.  Useful: `imagefs_paddr` is `IS` ("Set by IPL to where the
imagefs is when startup runs"), `imagefs_size` is `S` — **RIM's startup discovers
the imagefs itself**, so a differently-sized image probably needs **no change to
the prefix**.

## 4. What is *not* settled

| unknown | note |
|---|---|
| **does anything enforce the startup checksum?** | the value is a stored word at `startup_off+startup_size-4` (report 168 §Q2).  The builder recomputes it after changing `flags1`, and the boot with that image succeeded — so either it is not enforced or our recompute is right.  **[U]** which |
| **whether the bootloader needs a compressed image** | **answered: no.**  The uncompressed image (`flags1 = 0x01`) booted on the factory path (report 169) |
| **`stored_size` on an uncompressed image** | **did not block.**  The stock compressed value (`0x3150c0`) was left in place and the image booted; the IPL overwrites it or does not use it here |
| **the descriptor's image length** | **solved and booted** (milestone 3/4): `89-crc_tool.py reseal --length auto` patches `LoadEnd` (`image+0x08`) and writes the OS trailer; the loader accepted `0x788D84` |
| `mkifs` route | **blocked**, see below |

The **image trailer checksum is settled** (see §3): two's complement of the
32-bit LE word sum.  The stock root dirent also contains three non-zero bytes of
padding (`2d 57 6c`, mkifs junk between the empty root path and the next
dirent); the builder writes zeros there, so the output matches the stock
uncompressed image byte-for-byte except for those 3 bytes, the imagefs trailer,
`flags1`, and the startup checksum it invalidates.

**`mkifs` cannot build this image here** [M]: it links every bootstrap with
`qcc -bootstrap …` and fails on RIM's flattened startup in all four bootfile
formats, with `[+raw]` and with `linker=` overrides
(`Couldn't find linker spec for boot/startup file`).  This SDP ships **only x86**
startup binaries; there is no ARM BSP, and RIM's startup exists only as a flat
blob.  A QNX OMAP4430 **PandaBoard** BSP exists upstream but is an EVM startup
(MCT/PCB/DDR handling differs) and cannot replace RIM's.

## 5. `tools/ifs_build.py` — as built (session 24)

```
ifs_build.py listing STOCK.ifs --out stock.lst     # dumpifs -vv wrapper
ifs_build.py build --tree TREE --listing stock.lst --stock STOCK.ifs \
                   --out OUT.ifs [--replace NAME=PATH]... [--repack]
ifs_build.py check --image OUT.ifs --listing stock.lst
ifs_build.py selftest
```

Inputs: the `tools/ifs_extract.sh` tree (file data), a **`dumpifs -vv`**
listing (dirent order, symlinks, and per-entry mode/ino/uid/gid/mtime), the
stock `.ifs` (for the `0x0..0x44910` prefix), and optional replacements.  A
plain `dumpifs` listing also works: modes then come from the tree, inodes are
assigned sequentially, and the boot/script inodes are inferred by name.

What it does:
1. Parses the listing, parses symlink targets from its `name -> target` lines
   (`dumpifs -x` creates files and directories but **not symlinks** — 0 in the
   tree, 26 in the listing), and loads each file from the tree.
2. Writes the `image_header` (mountpoint `/`), the dirents (24-byte attr;
   symlink text NUL-terminated; zero terminator), then the file data.
3. Reuses the reference offsets.  A **smaller** replacement keeps its slot (the
   freed bytes stay zero), so the imagefs size and the `imagefs_size`/`ram_size`
   fields stay stock; a **larger** one repacks on the address-space rule.
4. Writes the trailer checksum, prepends the stock prefix, clears the `flags1`
   compression bits, and **recomputes the startup checksum** they invalidate
   (see §3).

Proven by `tools/tests/test_ifs_build.py`: the stock 1917 tree rebuilds and
`dumpifs` reports the same 99 dirents (root + 72 files + 26 symlinks) with the
same names, sizes, modes, inodes, uid/gid and mtimes, and prints
`Checksums: image=<our trailer> startup=<recomputed>`.  The output is
byte-identical to `dumpifs -u` of the stock image except the 3 root-padding
bytes, the imagefs trailer, `flags1`, and the startup checksum.

**The shim image.**  Replacing only `proc/boot/rfs_validator` with the 8,759-byte
shim keeps the reference slot `0x6a0800` (the original is 50,554 B), so the
imagefs stays `0x74446C`, the startup's `imagefs_size`/`ram_size` stay valid,
and `dumpifs` lists `proc/boot/rfs_validator 2237`.  The reseal then appends the
8-byte OS trailer and patches `LoadEnd`; the CRC path reports `VERIFIED`.
(`local/work/ifs-shim-sealed.ifs`, 0x788D84 bytes.)  `stored_size` remains the
stock compressed value — see §4.

Not attempted here: compression (the output is uncompressed).

## 6. Milestones, each with an acceptance test

1. **Writer round-trip (offline) — DONE (2026-11-03).** `dumpifs` lists the same
   entries with the same names, sizes and modes from our output and prints a
   `Checksums:` line.  Result: 99 dirents (root + 72 files + 26 symlinks), all
   matching; `tools/ifs_build.py selftest` and
   `tools/tests/test_ifs_build.py` pass.  QNX's own reader accepting our writer
   was the real test — no device needed.
2. **Mount shim — DONE (2026-11-03).**  `tools/rfs_validator_shim.c`, built with
   `ntoarmv7-gcc` (8,759 B, ARM EABI5, `/usr/lib/ldqnx.so.2`, `libc.so.3`),
   probes `hd1t179`/`hd2t179`/`hd7t179`/`hd7t178`, mounts qnx6 read-only with
   `noatime,crypto=enable,sync=optional`, and treats `EBUSY` as success.
   **Tested on the reference as `/tmp/rfs_validator_shim -vc`**: it reached
   `/dev/hd1t179`, found `/base` already mounted, and exited 0; `df -g /base`
   afterwards was byte-for-byte the same mount (127,996 blocks, `0x21`,
   `crypto=enable,sync=optional`).  Read-only, no media write.
3. **Reseal with length — DONE (2026-11-03).**  `89-crc_tool.py reseal IN OUT
   --image-off 0 --length auto --append-os-trailer` appends the 8-byte OS
   trailer, patches `LoadEnd` (`image+0x08`; the descriptor's `load_end_ptr`
   points at it), sets `os_type`, writes the end-8 cookie, and computes the CRC
   over `[0, length-4)`.  Proven two ways: resealing the sealed 4869 stock
   reproduces it byte-for-byte (`0x116B2562`), and the shim image verifies on
   the CRC path (`0x788D84`, CRC `0x38EE7B46`).
4. **Boot test — DONE (2026-11-03).**  The shim image over the **stock 1917
   `/base`** was flashed with `bbusb.py flash --signed` in one container with
   the vendor 1917 RCFS (operator decision: match the pair, because a 4869
   RCFS's libraries do not match a 1917 IFS).  It booted: `/dev/hd1t179 on
   /base type qnx6`, `/base/etc/os.version` = `2.1.0.1917`,
   `/proc/boot/rfs_validator` = 8,759 B, OS up (QNX 6.6.0 2014/03/19; HTTP,
   qconn, SSH on `0fca:8012`).  Report 169 has the evidence and the flash
   gotchas.  The UFS is still 4869; that mix did not block the boot.
5. **`/base` hook + UFS — DONE (2026-11-03).**  `/base` carries exactly one
   guarded call after `start_runlevel FINAL`:
   `if [ -x /accounts/devuser/startup.sh ]; then /accounts/devuser/startup.sh & fi;`
   (pristine 61,238 B → edited 61,713 B; no other hunk).  The 1917
   `.5.ufs` **with the operator's `autorun::0` change** replaced the ROOT V1.4 UFS,
   whose `qconnDoor` never authenticates.  The operator added the 4,566-byte
   startup script and the top-up ran (`0x2E → 0x32`, read back).  Report 170.
6. **The custom 1917 `/base` (bloatware removal) is no longer the goal**
   (operator, 2026-11-03).  The image work on this document is complete.  **The
   next goal is to edit a BB10 image to run on the PlayBook** (`ROADMAP.md` item
   8); the repeatable PlayBook procedure (for any `.ifs`/`/base`/UFS) is
   `IMAGE-REBUILD-GUIDE.md`.

## 7. Risk, stated once and plainly

A failed verification **shuts the unit down and marks the region dirty in NVRAM**;
after that it will not boot the IFS at all until the OSREGION record is cleared.
The in-tree equivalent of CFP's `resetqnxosregion` is
`tools/bbusb.py reset-osregion --apply` (`0xE8` with `arg = 4` deletes NVRAM
record `0x2074`; reports 163/164).  It is a dry run by default and **reads the
record back before and after**, because the device returns `0x2E` on both the
success and the failure path — CFP's "cleared" message proves nothing either.
Combined with "one working unit left", this is why milestones 1–3 are all
verifiable **without booting anything**, and why the mount line must be right on
the first boot.
