# 171 — mkifs imagefs compression: what is established

> **Correction (same day, report 172).**  Two statements below were wrong and
> are corrected by the narrower pass in **report 172**: (a) the imagefs is
> **one whole chunked stream**, not per-file compressed — the `imagefs` literal
> appears *inside* block 1, interleaved with NRV2B control bytes; (b) the
> `stored_size` semantics are settled: the payload starts at
> `0x808 + startup_size` (the header is inside `startup_size`) and ends at
> `0x808 + stored_size`, with `stored_size = startup_size + C + preboot_size`.
> The 171 sections below are left as written; where they conflict, **report 172
> is current**.  The splice test at the end of this report is valid.

Status: **static RE, session 25 (2026-11-03).**  Read-only; no device, no USB.
Written while a second, narrower pass was running (report 172).  This report
records only what is measured or read from the binaries and the two stock
images; the unresolved items are listed explicitly.

## Why

A backported BB10 10.3 IFS for the PlayBook (route B, `BB10-PORT.md` §4b) does
not fit the region-0 IFS slot uncompressed:

| item | value |
|---|---|
| PlayBook region-0 IFS slot (`0x100000`..`0xC00000`) | **11,534,336 B** (`0xB00000`) |
| 10.3 imagefs uncompressed (`dumpifs -u`; `imagefs_size` field) | **13,738,740 B** (`0x00D19EF4`) |
| 10.0.9.1103 imagefs uncompressed | `0x0122628C` (19,018,380 B) |
| 10.3 IFS file on disk | 5,898,240 B (`0x5A0000`) |
| 10.0.9.1103 IFS file on disk | 5,505,024 B (`0x540000`) |

So the stored image is compressed, and any rebuilt BB10 IFS must be too.

## Confirmed: the `startup_header` layout (DWARF, `mkxfs`)

`mkifs` is a symlink to `mkxfs`; it is **unstripped with DWARF**, and the
`startup_header` (256 bytes) and `image_header` (92 bytes) are defined there:

```
startup_header                            image_header
+0x00 signature     u32                   +0x00 signature[7]
+0x04 version       u16                   +0x07 flags        u8
+0x06 flags1        u8                    +0x08 image_size   u32
+0x07 flags2        u8                    +0x0c hdr_dir_size u32
+0x08 header_size   u16                   +0x10 dir_offset   u32
+0x0a machine       u16                   +0x14 boot_ino[4]
+0x0c startup_vaddr u32                   +0x24 script_ino   u32
+0x10 paddr_bias    u32                   +0x28 chain_paddr  u32
+0x14 image_paddr   u32                   +0x2c spare[40]
+0x18 ram_paddr     u32                   +0x54 mountflags   u32
+0x1c ram_size      u32                   +0x58 mountpoint[]
+0x20 startup_size  u32
+0x24 stored_size   u32
+0x28 imagefs_paddr u32
+0x2c imagefs_size  u32
+0x30 preboot_size  u32
+0x32 zero0         u16
+0x34 zero
+0x40 info
```

Measured field values (both are `version = 1`, `header_size = 0xd`,
`machine = 0x100`, `flags1 = 0x0d`, `flags2 = 0`, `paddr_bias = 0`,
`image_paddr = ram_paddr = 0x80100008`, `imagefs_paddr = 0`,
`preboot_size = 8`):

| field | 10.0.9.1103 | 10.3.03.2049 |
|---|---|---|
| file size | `0x540000` | `0x5A0000` |
| `startup_size` | `0x00059108` | `0x00086108` |
| `stored_size` | `0x0053E8C8` | `0x00598100` |
| `imagefs_size` | `0x0122628C` | `0x00D19EF4` |
| `ram_size` | `0x0127F394` | `0x00D9FFFC` |

**`ram_size = startup_size + imagefs_size` holds exactly in both images**
(10.0: `0x59108 + 0x122628C = 0x127F394`; 10.3: `0x86108 + 0xD19EF4 =
0xD9FFFC`).  So the startup and the imagefs are loaded into adjacent RAM and
`ram_size` is their total.

**The preamble:** file offset `0x808` is the 256-byte `startup_header`; ARM code
begins at `0x908`.  In the 10.3 image, `0x908 + startup_size = 0x86A10`, and the
bytes there are high-entropy — consistent with the compressed imagefs starting
at that offset.  The 10.0 image behaves the same way (`0x908 + 0x59108 =
0x59A10`).

## Confirmed: mkifs emits both modes, and `compress=` selects the codec

Standalone imagefs with a one-file build file (`[image=0x8000000]`):

| build directive | output | `flags1` | notes |
|---|---|---|---|
| `compress=0` | 200,176 B | n/a | **imagefs only**, starts `imagefs`; no startup header |
| `compress=1` | 260 B | `0x04` | header + 4-byte `startup.*` + compressed imagefs |
| `compress=2` | 1,452 B | `0x08` | same shape |
| `compress=3` | 688 B | `0x0c` | same shape; **UCL** (stock is `0x0d`, i.e. `0x0c | 0x01`) |

So `flags1` bit 2 (`0x04`) and bit 3 (`0x08`) select two codecs; `0x0c` is both
set (mkifs's "type 3"), and the stock images add bit 0 (`0x01`).  `mkxfs`'s
strings include `lzo1x_9x.c`/`lzo1x_o.c` and `uclopen`/`uclwrite`/`uclflush`,
consistent with LZO and UCL as the two codecs.

**In mkifs's `compress=3` output the imagefs header is plaintext** (the literal
`imagefs` appears at `0x263` in the 688-byte file), i.e. that path compresses
**per file**, not the imagefs as one stream.

## Unresolved (do not use as fact)

* **`stored_size` does not reconcile with the obvious layout.**  For 10.3,
  `0x86A10 + 0x598100 = 0x5FEB10 > 0x5A0000` (file size), and for 10.0,
  `0x59A10 + 0x53E8C8 = 0x5822D8 > 0x540000`.  In both images `stored_size` is
  ~1.095x the bytes actually remaining after the startup
  (`0x5195F0` and `0x4E65F0`).  Either the field means something else (a RAM
  figure, or a size including the startup stub) or the stream does not start at
  `startup_size`'s end.  **Report 172 is the pass that settles this.**
* **Which mode RIM used**: mkifs's own `compress=3` keeps the imagefs header
  plaintext (per-file), while the stock 10.3 bytes at `0x86A10` are high
  entropy.  Whether RIM's image uses a whole-stream mode, a combined
  startup+imagefs stream (`F3S_FUCL` appears in `mkxfs`'s strings), or per-file
  data inside a compressed container is **not yet established**.
* **No UCL encoder is available here.**  The QNX 6.6 SDP ships `libucl.a` +
  headers for x86 and armle-v7, but the x86 archive is QNX-ABI: linking it into
  a Linux host program crashes (the decompressor's first call returns a UCL
  error, the compressor segfaults).  There is no UCL source in the SDP and no
  `upx`/`ucl` host tool.

## Consequence for the port

Three ways to fit a BB10 IFS, none of which needs a device write to evaluate:

1. **Get a compressor.**  Use the QNX 8 `mkifs` (the natural tool) if it
   materialises, or implement NRV2B encoding plus the exact framing once report
   172 pins it.  This preserves the stock layout.
2. **Move the region-0 RCFS start** in the region-0 MBR (it is a normal image
   component, `.2.mbr` at delta 0), e.g. from `0xC00000` to `0x1000000`, and
   flash the RCFS at the new delta.  That gives the IFS 15 MiB, enough for the
   uncompressed 13.9 MB image, and does not touch the MCT.  It is a structural
   change (recoverable with the working 2.1.0.1917 set) and a new variable.
3. **Trim** the unified image's Snapdragon-only content; measured, the
   London/MSM-named files alone are ~1.8 MB and would not close the ~3.7 MB gap
   once the winchester set is added.

No decision is taken in this report; `BB10-PORT.md` §4b and `DECISIONS.md` §22
record the operator's route, and the slot/compression choice is the next
decision.

## Artefacts

* `tools/ifs_build.py` — `--add NAME=PATH` (new files for the backport).  It does
  **not** compress.
* Struct dump: this report §"Confirmed".
* Test builds and the DWARF/objdump outputs are in the session scratchpad
  (not kept).

## Validated: an mkifs payload splices into RIM's container

Session 25 proved the practical route that avoids writing a UCL encoder:

1. build/compress the imagefs with **mkifs** (`[image=...]` + `+compress`, which
   is UCL/NRV2B, and mkifs owns the chunk framing and the `Image-trailer`);
2. take mkifs's compressed payload (`[H+startup_size, H+stored_size)` in its own
   output) and place it at **RIM's** `0x808 + startup_size` (`0x86910` for both
   BB10 images), leaving the 0x800 preamble, the 0x100 `startup_header` and the
   ARM decompressor stub untouched;
3. patch `ram_size` (`+0x1c` = `startup_size + imagefs_size`), `stored_size`
   (`+0x24` = `startup_size + C + preboot_size`), `imagefs_size` (`+0x2c`), keep
   `flags1`, and recompute the startup trailer at `H+startup_size-4` so the
   LE32 sum over `[H, H+startup_size)` is zero;
4. the bytes after `H+stored_size` are RIM's `QNXH-OS-1` block and zero fill.

Test: the 10.3 stock container with a 428-byte `mkifs +compress` payload (built
from a one-file `[image=0x8000000]` build file) spliced at `0x86910`, with
`ram_size`, `stored_size` and `imagefs_size` patched and the trailer recomputed,
is read by `dumpifs` exactly as expected:

```
808 100 Startup-header flags1=0xd flags2=0 paddr_bias=0
908 86008 startup.*
86910 5c Image-header
8696c 50 Image-directory
869bc 30d40 proc/boot/big
```

That is the whole mechanical path; what remains is to generate a build file that
reproduces the 10.3 tree (files, symlinks, modes) plus the winchester additions,
and to confirm mkifs's `boot_ino`/`script_ino` handling for the prebuilt
`proc/boot/.script`.

### Caveat found after this test: `boot_ino`

mkifs sets `script_ino` when the `.script` entry carries the `[+script]`
attribute (measured: `script_ino = 2`), but a plain or `+script` file entry
leaves **`boot_ino[0] = 0`**.  The stock BB10 image has `boot_ino[0] =
0xE0000002` (the `procnto` inode with the boot flag).  `boot_ino` is inside the
compressed imagefs header, so it cannot be patched without recompressing that
block.  The splice route therefore also needs either a `.bootstrap` that mkifs
can build (there is no ARM BSP startup in this SDP) or a host NRV2B encoder so
`tools/ifs_build.py` can emit the imagefs with the right inodes and compress it.
