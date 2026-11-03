# IFS rebuild — working notes for the 2.1.0.1917 boot image

Everything a session needs to build a **modified** 2.1.0.1917 IFS.  Written
at the end of session 20 (absolute) after establishing feasibility; nothing here
has been built or flashed yet.

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
0x000808  0x100    Startup-header   big-endian, sig 0x00ff7eeb, ver 0x000d, flags1=0x0d
0x000908  0x44008  startup.*        RIM's own: "rifsboot", "imagefs", ReadMCTData,
                                    ReadPCBNumber, get_omap_info
0x044910  ...      imagefs          LZ-compressed: header + dirents + data + trailer
```

* **The imagefs is one compressed stream** (`+compress`, type 3 via
  `STARTUP_HDR_FLAGS1_COMPRESS_MASK`), which is why `dumpifs` prints *decompressed*
  offsets (`0x785800` for a 3,280,000-byte file) and why **in-place patching is
  impossible**.
* Sizes: component 3,280,000 B; extracted tree 6,581,929 B.

## 3. Format facts (from `sys/image.h`)

`struct image_header`: `signature[7]` = `"imagefs"`, `flags`, `image_size` (header
→ end of trailer), `hdr_dir_size` (header → last dirent), `dir_offset`,
`boot_ino[4]`, `script_ino`, `chain_paddr`, `spare[10]`, `mountflags`,
`mountpoint` (flexary).  `IMAGE_FLAGS_*`: `0x01` big-endian, `0x02` read-only,
`0x04` inode bits valid.

Dirents (`union image_dirent`), all `unsigned long` = 4 bytes on ARM32:

| kind | fields after `image_attr` |
|---|---|
| `image_attr` (common) | `size` (u16, size of the dirent), `extattr_offset` (u16), `ino`, `mode`, `gid`, `uid`, `mtime` |
| file | `offset` (from header), `size`, then NUL-terminated path (no leading `/`) |
| dir | path only |
| symlink | `sym_offset` (u16), `sym_size` (u16), path, then the link text |
| device | `dev`, `rdev`, path |

`struct image_trailer { cksum; }` — "checksum from start of header to start of
trailer".

`struct startup_header` (`sys/startup.h`) marks each field `I` (IPL sets), `S`
(startup sets) or `IS`.  Useful: `imagefs_paddr` is `IS` ("Set by IPL to where the
imagefs is when startup runs"), `imagefs_size` is `S` — **RIM's startup discovers
the imagefs itself**, so a differently-sized image probably needs **no change to
the prefix**.

## 4. What is *not* settled

| unknown | note |
|---|---|
| **checksum algorithms** | `dumpifs` prints two: `startup=0x90310cb3` over the startup block (`0x808..0x44910`) and `image=0x4278436d` for the imagefs trailer.  Neither algorithm is documented; sum32, sum+fold, byte sum, BE sum, zlib crc32 and crc32c all fail to reproduce the **startup** value.  Our writer computes its own image trailer, so a self-consistent value may be enough — **[U] whether anything enforces either** |
| **whether the bootloader needs a compressed image** | the stock image is compressed; an uncompressed one is legal per the format, but RIM's `rifsboot` may assume compression — settles with one boot |
| **the descriptor's image length** | the CRC/factory path takes the length from the image descriptor, not the trailer; a bigger image needs `89-crc_tool.py` (or a successor) to set it as well as `os_type` and the CRC |
| `mkifs` route | **blocked**, see below |

**`mkifs` cannot build this image here** [M]: it links every bootstrap with
`qcc -bootstrap …` and fails on RIM's flattened startup in all four bootfile
formats, with `[+raw]` and with `linker=` overrides
(`Couldn't find linker spec for boot/startup file`).  This SDP ships **only x86**
startup binaries; there is no ARM BSP, and RIM's startup exists only as a flat
blob.  A QNX OMAP4430 **PandaBoard** BSP exists upstream but is an EVM startup
(MCT/PCB/DDR handling differs) and cannot replace RIM's.

## 5. Design of `tools/ifs_build.py`

Inputs: the `tools/ifs_extract.sh` tree, the stock `.ifs` (for the `0x0..0x44910`
prefix), the `dumpifs` listing (for symlinks — see gap below), and an optional
replacement file (e.g. a `rfs_validator` shim).

Steps:
1. Walk the tree; build the entry list: dirs, files, symlinks (from the listing),
   plus the two implicit entries (`Root-dirent`, and the `.script`/bootstrap
   inode references).
2. Assign inodes; write `image_header` (mountpoint `/`) + dirents; align file data
   (executables page-aligned); write `image_trailer` with our checksum.
3. Concatenate `prefix + imagefs`.
4. Reseal (`os_type 4→6`, CRC over `[image, end−4)`, **and the descriptor length**).

**Gap found while writing this:** `dumpifs -x` creates files and directories but
**not symlinks** (0 in the tree, 26 in the listing).  The builder must parse the
listing's `----  size  name -> target` lines for those.

## 6. Milestones, each with an acceptance test

1. **Writer round-trip (offline).** `dumpifs` must list the same 72 entries with
   the same names, sizes and modes from our output, and print a `Checksums:` line.
   QNX's own reader accepting our writer is the real test — no device needed.
2. **Mount shim.** Build with `ntoarmv7-gcc` a `rfs_validator` replacement that
   mounts `/base` and exits 0:
   qnx6, `-r`, `noatime`, `crypto=enable,sync=optional`, device probed over
   `hd1t179`/`hd2t179`/`hd7t179`/`hd7t178` (all measured from the live unit —
   `df -g` and `rfs_validator -vc`).  **Test it on the stock unit as
   `/tmp/shim -c`** — it exercises the mount call without touching media.
3. **Reseal with length** (see §4).
4. **Boot test**, one variable at a time: mount line over the *stock* `/base`
   first; only then a custom `/base`.

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
