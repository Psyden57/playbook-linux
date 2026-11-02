# 159 — Can we rebuild the 2.1.0.1917 IFS here? What blocks `mkifs`, and the way round it

Status: feasibility established by measurement. No image has been built or flashed.

## Answers in brief

* The stock IFS **cannot be patched in place**: everything from the image header
  onward (header, directory and all file data) is one **LZ-compressed stream** —
  `dumpifs`'s "Offset" column is a *decompressed* offset, which is why the last
  entries (`0x785800`) exceed the 3,280,000-byte file.  The `+compress` image is
  ~3.28 MB for a **6.58 MB** extracted tree.
* `mkifs` **cannot build this image here**.  It refuses any bootstrap it cannot
  link: `Couldn't find linker spec for boot/startup file`, for every bootfile
  format (`raw`, `elf`, `binary`, `srec`) and with `[+raw]` or `linker=` tried.
  The reason is in the docs: the default bootstrap linker is
  `qcc -bootstrap -nostdlib …`, i.e. mkifs expects a **linkable** startup; our
  SDP ships **only x86** startup binaries and no ARM BSP, and RIM's startup exists
  only as a flattened blob.
* We *do* have **`ntoarmv7-gcc` 4.7.3** and the ARM QNX target libraries, so we can
  build QNX-native ARM **programs** (just not boot startups).
* Everything needed to write an uncompressed image ourselves is documented:
  `$QNX_TARGET/usr/include/sys/image.h` (image header, dirents, trailer, script
  records) and `sys/startup.h` (the `Startup-header`).  The stock startup header
  decodes cleanly, big-endian, signature `0x00ff7eeb`, `version 0x000d`.

## 1. What the stock image is made of [M]

```
0x000000  0x808   *.boot            (boot prefix from the bootfile)
0x000808  0x100   Startup-header    flags1=0xd  (big-endian; sig 0x00ff7eeb, ver 0x000d)
0x000908  0x44008 startup.*         RIM's own bootstrap: "rifsboot", "imagefs",
                                    "ReadMCTData", "ReadPCBNumber", "get_omap_info"
0x044910  ...     image (LZ)        imagefs header + dirents + data + trailer, compressed
```
Extracting with `dumpifs -x` yields **72 files / 11 directories, 6,581,929 bytes**
(the tree includes `proc/boot/rfs_validator`, `proc/boot/.script`, procnto, the
sanity trio, and the display/HAL servers).

## 2. Why `mkifs` fails here [M]

| attempt | result |
|---|---|
| `[virtual=armle-v7,{raw,elf,binary,srec} +compress] .bootstrap = { startup.stock }` | `Couldn't find linker spec for boot/startup file` (all four) |
| `[+raw] startup.stock` | same |
| `linker="cp %i %o"` on the virtual line, and as a separate attribute | same (attributes don't reach the bootstrap) |

The `mkifs.html` utilities page gives the default linker as `qcc -bootstrap …`
and says a *bootfile* may override it; the ARM bootfiles in this SDP
(`binary.boot`, `elf.boot`) contain only `attr=`, `vboot=`, `pagesizes=` and a
`filter=`, no `linker=`, and no ARM startup exists to link.

## 3. The route that avoids `mkifs` entirely [P]

Keep RIM's bootstrap **verbatim** and write only the filesystem part:

1. **bootstrap shim instead of a script edit.**  Leave `proc/boot/.script`
   unchanged and replace `proc/boot/rfs_validator` with a small `ntoarmv7-gcc`
   program that mounts the root filesystem at `/base` and exits 0.  That removes
   the need to author compiled-script records (`struct script_*`) and keeps the
   boot sequence byte-identical apart from one binary.
   Mount equivalent, measured from the live unit: qnx6, `-r`, `noatime`,
   `crypto=enable,sync=optional`, device `hd1t179`/`hd2t179`/`hd7t179`/`hd7t178`
   (probe, as the validator does).
2. **image writer (new tool).**  Emit an **uncompressed** image:
   `image_header` + dirents (attrs, modes, mtimes, symlinks, devices) + file data
   + `image_trailer` with its checksum, from the `dumpifs -x` tree.  Uncompressed
   sizes (~6.6 MB + overhead) still fit the 11 MiB between the IFS at
   `0x100000` and the RCFS at `0xC00000`.
3. **copy the prefix**: bytes `0x0 .. 0x44910` (boot prefix + Startup-header +
   RIM's startup) go in unchanged, with the `Startup-header` fields that describe
   the image (size/checksum/compression flag) recomputed per `sys/startup.h`.
4. **reseal** with `os_type 4→6` + CRC (`ARMING-TUTORIAL.md` §3); the descriptor's
   image length must match the new size, so the reseal tool must be able to set it.

**Self-check before the unit sees it:** `dumpifs` must list the same 72 entries
from our output, with the same names/sizes/modes, and `dumpifs` must print a
`Checksums:` line.  That is a real validation: `dumpifs` is QNX's own reader.

## 4. Unknowns that must be closed before flashing

| unknown | what settles it |
|---|---|
| the exact `Startup-header` field meanings and which ones must change for a different image size | `sys/startup.h` + the stock header decode (in progress) |
| whether RIM's `rifsboot` requires a *compressed* image, or accepts uncompressed | `startup.h` flags + a boot test |
| where the bootloader's CRC path takes the image **length** (the descriptor field) | report 89 §3.1 + `89-crc_tool.py` |
| the QNX `mount()` call the shim must make (fs type resolution, crypto option) | QNX libc headers + a test build |

## 5. Alternatives, and why they are worse

* **Obtain a QNX 6.6 ARM BSP** (or RIM's startup as a linkable ELF): then `mkifs`
  works natively.  Cheapest *if* such a BSP exists for this target — worth a look
  before writing the image writer.
* **Patch in place**: impossible, the image is compressed as a whole (§1).
* **Re-sign `/base`**: impossible, no key (§158).

## 6. Checksums, and what the startup header actually constrains [M/U]

`dumpifs` prints two checksums (`startup=0x90310cb3`, `image=0x4278436d`) and
`sys/startup.h`/the `mkifs` docs say the startup trailer holds a checksum "for
use by startup" and the image trailer one "for the image".  The algorithm is not
documented, and **none of the obvious ones match** the stock startup value over
any plausible range (`0x0/0x800/0x808/0x908 .. 0x44910`):

| candidate | result over `0x808..0x44910` |
|---|---|
| sum of LE u32 | `0x00000000` |
| sum + carry fold | `0x00006c95` |
| sum of bytes | `0x0105dc1f` |
| sum of BE u32 | `0x6670059d` |
| zlib crc32 | `0x7a4a4734` |
| crc32c (Castagnoli) | `0x83910681` |

So it is a QNX-specific function.  **[U]** — but it may not matter, for the
reason in the next paragraph.

`sys/startup.h` marks each field `I` (set by the IPL) or `S` (set by the startup)
or `IS` (both).  Crucially:

* `imagefs_paddr` is `IS` and commented *"Set by IPL to where the imagefs is when
  startup runs"*, and `imagefs_size` is `S` — i.e. **the startup discovers the
  image itself** (consistent with RIM's startup carrying the `imagefs` signature
  string).
* `stored_size` is `I` and `startup_size` is `I`.

If the startup scans for the imagefs (very likely, and testable with one boot),
then **the prefix does not have to be modified at all** for a differently-sized
image — which also sidesteps the startup checksum.  Our writer would then be an
**imagefs emitter only**: `image_header` + dirents + data + `image_trailer`,
appended to the stock `0x0..0x44910` prefix unchanged.

The remaining checksum is the image trailer's, which our writer computes over its
own bytes — so we can use a self-consistent value once the algorithm is either
derived from a dumpifs-readable test image or shown not to be enforced.

**What does grow:** the bootloader's CRC path needs the image **length**, which
lives in the descriptor, not in the trailer.  A bigger image therefore requires
`89-crc_tool.py` (or its replacement) to set that length as well as `os_type` and
the CRC — currently it does not.

## 7. Next work item

1. Write `tools/ifs_build.py`: emit an uncompressed imagefs from the
   `dumpifs -x` tree (header + dirents + data + trailer), appended to the stock
   prefix.  **Milestone: `dumpifs` lists the same 72 entries with the same names,
   sizes and modes, and prints a `Checksums:` line.**
2. Build the `rfs_validator` shim with `ntoarmv7-gcc` (mount `/base`, exit 0) and
   prove it runs with `/tmp/<shim> -c` **on a stock unit** — that tests the mount
   call and the crypto/option arguments without touching media.
3. Extend the reseal tool to set the descriptor's image length.
4. Then, and only then, the boot test: mount line over stock `/base` first.
