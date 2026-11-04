# 172 — mkxfs / mkifs stream semantics for RIM BB10 IFS containers

**Status:** complete (Q1–Q4)
**Date:** 2026-11-03
**Target:** `local/toolchains/qnx660-master/host/linux/x86/usr/bin/mkxfs`
(behaves as `mkifs` per `argv[0]`; QNX SDP 6.6.0, ELF i386, unstripped, full DWARF)
**Ground truth A (10.0.9.1103):** `local/work/bb10/win1103/win1103.4.ifs` — 0x540000 B
**Ground truth B (10.3.03.2049):** `Z10_10.3.03.2049_STL100-1.0.0.ifs` — 0x5A0000 B
**Method:** `objdump -d -M intel`, `readelf --debug-dump=info`, `nm -S`, `strings`, DWARF
compile-unit / `DW_AT_location` reconstruction with `pyelftools`, plus controlled re-runs of
the tool itself in the session scratchpad (`[image=0x8000000 compress=3]`, one file).

> **Headline correction to the task's premise.** The imagefs does **not** start at
> `0x908 + startup_size`. In a stock RIM container `startup_size` *includes* the 0x100-byte
> startup_header, so the payload starts at `0x808 + startup_size` (0x59910 / 0x86910), exactly
> 0x100 lower than the candidate in the brief. With that one correction every number reconciles
> — including `stored_size` — to the byte. Also, mkifs 6.6 does **not** keep the imagefs header
> plaintext in `compress=3` mode; the header is the first thing *inside* the compressed stream
> (proved below with a locally built image).

---

## Q1 — Where does the compressed imagefs payload actually start and end, and what does `stored_size` count?

### Question
`0x908 + startup_size` (= 0x86A10 for 10.3) does not reconcile with `stored_size`
(0x598100 + 0x86A10 > file size), and `stored_size` looks like ~1.095x a plausible compressed
size. What is the real formula, and what exactly does `stored_size` measure?

### Answer
Two independent facts:

1. **`startup_size` is measured from the startup_header, not from the end of the header.**
   The 0x100-byte `startup_header` is *inside* the `startup_size` span. So:
   * `startup_header` is at `H = 0x808`
   * startup code begins at `H + 0x100 = 0x908`
   * **imagefs payload begins at `H + startup_size`** (= **0x59910** / **0x86910**)
   * **imagefs payload ends at `H + stored_size`** (= **0x53F0D0** / **0x598908**)

2. **`stored_size` = distance from the startup_header to the end of the mkifs image**, i.e. it
   counts `startup_header + startup image + startup_trailer + entire compressed imagefs +
   align padding + 4-byte image trailer`, but **not** the 8-byte preboot trampoline, **not** the
   0x800-byte RIM preamble before it, and **not** RIM's appended `QNXH-OS-1` signature block.

   In mkifs terms (the actual writer, see Evidence): `stored_size = ftell(EOF) - bsize`
   where `bsize` = `booter.data_len` = `preboot_size` = 8.

   Therefore, with `C` = length of the compressed imagefs payload (including its 8-byte
   align-pad + image-trailer tail):

   ```
   payload_start = H + startup_size                ; H = 0x808
   payload_end   = H + stored_size
   C             = payload_end - payload_start = stored_size - startup_size - 8
   stored_size   = startup_size + C + 8
   ```

**The 1.095x "anomaly" is an artifact of comparing `stored_size` against
`file_size − (0x908 + startup_size)`, which still contains the 0x100 header, the startup stub and
RIM's trailing signature block.** `stored_size / C` = 1.1285 (A) and 1.0950 (B) — both are simply
`(startup_size + C + 8) / C`.

### Evidence
Header fields (little-endian, at `H = 0x808`; `signature = 0x00FF7EEB`, `version = 1`,
`header_size = 0x100`, `machine = 0x28` (ARM), `paddr_bias = 0`, `imagefs_paddr = 0`,
`preboot_size = 8`, `flags1 = 0x0D`, `flags2 = 0` in both images):

| quantity | win1103 (10.0.9.1103) | Z10 (10.3.03.2049) |
|---|---|---|
| file size | 0x540000 | 0x5A0000 |
| H (startup_header) | 0x808 | 0x808 |
| startup_size (H+0x20) | 0x59108 | 0x86108 |
| stored_size (H+0x24) | 0x53E8C8 | 0x598100 |
| imagefs_size (H+0x2C) | 0x122628C | 0xD19EF4 |
| ram_size (H+0x1C) | 0x127F394 | 0xD9FFFC |
| **payload start = H+startup_size** | **0x59910** | **0x86910** |
| **payload end = H+stored_size** | **0x53F0D0** | **0x598908** |
| C = stored − startup − 8 | **0x4E56B8** | **0x511FF0** |
| check: startup + C + 8 == stored | 0x59108+0x4E56B8+8 = 0x53E8C8 ✔ | 0x86108+0x511FF0+8 = 0x598100 ✔ |
| imagefs_size / C (compression ratio) | 3.7066 | 2.5839 |
| bytes after payload end | 0xF30 (RIM `QNXH-OS-1` block + zero pad) | 0x76F8 (same) |
| `ram_size == startup_size + imagefs_size` | 0x59108+0x122628C = 0x127F394 ✔ | 0x86108+0xD19EF4 = 0xD9FFFC ✔ |

Independent structural confirmation that the payload really ends at `H + stored_size`: the last
8 bytes before that offset are `00 00 00 00` (align pad) + a 4-byte trailer (`78 60 e9 ab` /
`2b 66 e2 8c`), and the very next byte begins the ASCII tag **`QNXH-OS-1`** (RIM's signature
block), followed by zero fill to the end of the file.

mkifs writer, from DWARF (frame base = ebp+8 was resolved from the `DW_AT_location` entries in
`readelf --debug-dump=info`; every slot below was cross-checked against the struct member offsets
in the debug info, e.g. `shdr` @ fbreg−0x260 ⇒ ebp−0x258):

* `ifs_make_fsys` @ **0x80581C2** (`mk_image_fsys.c`), header buffer `shdr` at `[ebp-0x258]`.
* startup header build / writes: memset 0x100 @ 0x8058A99, field stores 0x8058AB7–0x8058D32,
  whole header written with `iwrite(shdr,0x100)` @ **0x8058D58**.
* `stored_size` is written **0** into the header for the compression case at 0x8058D07, then
  patched after compression completes:
  * `0x8059B30–0x8059B3E`: `eax = ftell(dst_fp); eax -= [ebp-0x6C]` (`[ebp-0x6C]` = `bsize`)
  * `0x8059B56`: `[ebp-0x234]` (= header offset 0x24) = `swap32(eax)`
  * `0x8059B76`: `fwrite(shdr, 0x100)` back over the header at file offset `[ebp-0x34]` (= bsize)
  * ⇒ **`stored_size = ftell(EOF) − bsize`**, i.e. the mkifs image length minus the preboot blob.
* `bsize` = `booter.data_len` (`0x8094AB4`), stored to `[ebp-0x6C]` @ 0x805848F; the 8-byte
  preboot blob (`booter.data`) is `iwrite`-n @ 0x805848A **before** compression starts.
* `startup_size` (`dsize`) is computed @ 0x80585D6–0x80585F2:
  `dsize = align8(startup_file_size + bsize + 0x10B) − bsize`, and the imagefs is placed at
  `bsize + dsize` ⇒ the header is inside the `startup_size` span (0x10B = 0x100 header + trailer
  + alignment).
* Self-reproduced with the tool: `[image=0x8000000 compress=3]` + one 12-byte file ⇒ 360-byte
  image, `startup_size = 0x104` (no startup program: `dsize = 0x100 header + 4 trailer`, set at
  0x8058611), `stored_size = 0x168` **= whole file** (bsize = 0), `imagefs_size = 0xBC`. That is
  the "tiny mkifs output" behaviour quoted in the brief and it matches the formula above
  (`stored = startup + C + 8` with `bsize = 0`).

---

## Q2 — Is the imagefs one whole stream, per-file, or combined with the startup?

### Question
(a) one whole stream, (b) per-file, or (c) combined startup+imagefs? Which, and which bytes are
plaintext vs high entropy?

### Answer
**(a) — one single compressed stream covering the entire imagefs, whose very first bytes are the
92-byte image_header.** It is delivered as a chain of independently compressed blocks, each
prefixed with a **2-byte big-endian compressed length** (NRV2B / UCL block framing), but it is one
logical stream: block boundaries have nothing to do with file boundaries.

* The startup stub is **not** part of the stream: it is written *before* `compress_start()` and
  is plaintext ARM code.
* The imagefs header, the mountpoint string, all dirents, all file payloads, the alignment pad and
  the 4-byte `Image-trailer` are all **inside** the stream.
* `compress=3` therefore does **not** leave the imagefs header plaintext (contrary to the task's
  premise). It *appears* readable only because NRV2B emits the first ~8 header bytes as a literal
  run; control bytes are interleaved, so it is not a parseable header in place.

### Evidence
Code path (`mk_image_fsys.c` / `ifs_make_fsys` @ 0x80581C2):

| address | action |
|---|---|
| 0x805848A | `iwrite(booter.data, data_len = 8)` — preboot, **not** compressed |
| 0x8058D58 | `iwrite(shdr, 0x100)` — startup_header, **not** compressed (`compress_fp == 0`) |
| 0x8058EA3 | `iwrite(startup_trailer, 4)` — not compressed |
| **0x8059014–0x8059027** | `if (compressed) { cimage_offset = image_offset; compress_start(); }` |
| 0x8059052 / 0x8059081 | `iwrite(image_header, 0x58)` + `iwrite(mountpoint, strlen)` — **compressed** |
| 0x8059190… | per-file `copy_elf`/`copy_data`, dirents, all via `iwrite` → compressed |
| 0x805995D / 0x8059AE1 | `padfile(...,"Image-trailer")` + 4-byte trailer — compressed |
| 0x8059A19 | `compress_stop()`; the whole temp file is then copied out verbatim (0x8059A71) |

`iwrite` @ **0x8054DE2**: if `compress_fp` (`0x8095B40`) is non-NULL it dispatches to `uclwrite`
(mode 3), `lzowrite` (2) or `gzwrite` (1); otherwise plain `fwrite`. `compress_start` /
`compress_stop` @ 0x8054C7C / 0x8054D72 take the codec from `compressed` (`0x8095F60`).
`uclflush` @ **0x80549E8** compresses the 64 KiB staging buffer with
`ucl_nrv2b_99_compress(level 9)` and emits **`putc(len>>8); putc(len&0xff)` before every block**
(0x8054A93–0x8054AB9); if the compressed block would exceed 0xFFFF it retries with 0x1000 fewer
input bytes (0x8054A75).

Byte-level evidence:

1. **Payload head is high entropy, the stub is not.** Shannon entropy of 8 KiB windows:

   | region | win1103 | Z10 |
   |---|---|---|
   | pre-header 0x000 (+8 KiB) | 2.33 | 2.20 |
   | startup_header 0x808 (0x100 B) | 1.07 | 0.96 |
   | startup body 0x908 (+8 KiB, ARM) | 5.99 | 6.00 |
   | **payload head** (0x59910 / 0x86910) | **7.77** | **7.70** |
   | payload mid | 7.82 | 7.84 |
   | payload tail | 7.44 | 7.74 |
   | `QNXH-OS-1` block | 4.56 | 4.30 |

2. **No plaintext `imagefs` at the payload start; it is a literal *inside* block 1.**

   ```
   win1103 @0x59910: 6e a0 | ff 69 6d 61 67 65 66 73 04 ff 8c 62 22 01 4c 55 00 00 cb 5c 00 02 …
   Z10     @0x86910: 6f 9b | ff 69 6d 61 67 65 66 73 04 ff f4 9e d1 00 30 51 00 00 cb 5c 00 02 …
   ```

   Reading it: 2-byte BE block length (`0x6EA0` / `0x6F9B`), then NRV2B data whose first literal
   run is `"imagefs"` (image_header.signature[7], written by `memcpy(&ihdr,"imagefs",7)` @
   0x8058668 from the string at 0x8085F25), then `04` = `ihdr.flags` (mkfs `|= 4` @ 0x8058FD2),
   then **`image_size` = 0x0122628C / 0x00D19EF4 — bit-for-bit equal to the startup_header's
   `imagefs_size` field of the same image**. The literal run is then broken by the next NRV2B
   control byte (`ff`), which is why the following dword (`0x0000554C`) is *not* a sane
   `hdr_dir_size`/`dir_offset` pair — a plaintext header would be self-consistent there.

3. **Block chain walks to the exact end of the payload.** Parsing `[u16 BE len][data]` from
   `H+startup_size` to `H+stored_size−8`:
   * win1103: **293 blocks**, lands at **0x53F0C8 = payload_end − 8 exactly** (remainder 0).
   * Z10: **213 blocks**, lands at 0x5988FF (1 byte short of 0x598900 — see Unresolved).

   The 8-byte tail is `00 00 00 00` + the `Image-trailer` word in both images.

4. **A locally built `compress=3` image proves the layout directly.** `[image=0x8000000
   compress=3]` + one 12-byte file ⇒ 360 B:
   * 0x000–0x0FF startup_header (`stored_size = 0x168` = whole file, `startup_size = 0x104`)
   * 0x104 = `00 5c | ff 69 6d 61 67 65 66 73 04 cb bc 00 ac 74 03 5c cc 00 02 …`
     → block length 0x5C, then `"imagefs"` `04` … i.e. the header is *inside* the block, and the
     `image_size` bytes (`bc 00 00 00`) are already interleaved with control bytes, so this is
     not a plaintext header either.
   * The `-v` listing for the same file says `Image-header @ image+0x104` — precisely the offset
     where the *block length prefix*, not the header, is stored.
   * A 400,220-byte imagefs build (`big.ifs`, random 200 KB + patterned 200 KB) produced
     **10 blocks** whose chain lands at `filesize − 6`, confirming the framing and that blocks are
     sized by the 64 KiB compressor buffer, never by files.
   * `compress=2` (LZO) reproduces the same framing: `00 60 1e 69 6d 61 …` at 0x104.

5. **Per-file compression is ruled out** by the same walk: block boundaries fall inside file
   payloads (293 blocks for the ~2 900 files of win1103; 213 blocks for the Z10 imagefs), and the
   dirents themselves are inside the single stream, so there is no per-file compressed-length
   field or per-file stream restart anywhere.

---

## Q3 — Which build-file directive / flag selects RIM's mode?

### Question
Which of `compress=`, `-c`, `preboot`, `imagefs_paddr`, `stored_size` selects RIM's
"UCL/NRV2B single stream, preboot = 8, imagefs_paddr = 0" mode?

### Answer
The **`compress` attribute of the `ifs` filesystem**, used either bare/boolean (`+compress`) or
with a value (`compress=<n>`):

* `+compress` ⇒ `compressed = 3` ⇒ **UCL / NRV2B**, i.e. exactly RIM's mode.
* `compress=1` ⇒ zlib (gzip), `compress=2` ⇒ LZO1X, `compress=3` ⇒ UCL/NRV2B, `compress=0` ⇒ no
  compression. Any other value dies in `compress_start` with "Unsupported compression type %d".
* The codec is recorded in **`flags1` bits [3:2] = `compressed`** (`flags1 |= compressed << 2`,
  0x8058D1F). Both RIM images have `flags1 = 0x0D = 0b1101` ⇒ bits[3:2] = 3 = UCL, plus bit0 = 1
  (`booter.virtual & 1`, 0x8058B22).

The rest of RIM's layout is not a "mode" at all — it is the normal `[image=…]` IFS attribute group:

* `image=0x80100000` → `parse_addr_space_spec(&image)`; `image_paddr` is then
  `image.addr + bsize + booter.paddr_bias` = 0x80100000 + 8 + 0 = **0x80100008** (both images).
* `ram=` (if used) sets `split_image` and a separate `ram.addr`; RIM does **not** use it
  (`ram_paddr == image_paddr`, split flag clear).
* `preboot` is **not** a directive: `preboot_size` (H+0x30, u16) is `bsize = booter.data_len`,
  the size of the `booter.data` blob emitted before the startup header. RIM's value is **8** — and
  bytes 0x800–0x807 of both files are exactly the 8-byte ARM trampoline
  `0c c0 8f e2 00 f0 9c e5` (`add ip,pc,#0xC` / `ldr pc,[ip]`), immediately after a 0x800-byte RIM
  preamble that begins with the ARM branch `fe 01 00 ea`.
* **`imagefs_paddr` (H+0x28) is never written by mkifs** — the header is memset to 0 and no store
  targets that slot anywhere in `ifs_make_fsys`. It stays 0, matching both stock images.
* **`stored_size` has exactly one writer** (0x8059B56, patched into the header by 0x8059B76) and it
  is purely `ftell(EOF) − bsize`; no build-file directive influences it.

`-c` is **not** the compression selector: the getopt string is `a:c:dDf:l:nNp:r:s:t:v`
(0x80878BC) and `-c` feeds `set_cpu()` (main 0x80654D6). Verified empirically — `-c 0/1/2/3/9`
on the same build file all produce byte-identical output to `compress=3`.

### Evidence
* `ifs_attr_table` @ **0x8091A20** (11 entries, 16 B each: `{name, type=2i, name_with_eq, 2i+1}`):
  `autolink`, `code=`, `image=`, `linker=`, `ram=`, `relocs`, `virtual`, **`compress=`**,
  `keepsection=`, `phys_align=`, `big_pages`.
* Jump table @ **0x8089320** (indexed by the `decode_attr` result); `compress` dispatch:
  * index 0x0D (`+compress`, boolean form) → 0x8066BEB: `if (value) value = 3;` then falls into
    0x8066BF9 → `compressed = value`
  * index 0x0E (`compress=<n>`) → 0x8066BF9: `compressed = value`
  * **single store to the global**: `mov ds:0x8095F60,eax` @ **0x8066BFC** (verified: the only
    write to `0x8095F60` in the whole binary; 16 references total, the other 15 are reads).
* `compress_start` @ 0x8054C7C: `1 → gzopen64`, `2 → lzoopen`, `3 → uclopen`,
  default → `error_exit("Unsupported compression type %d")` (0x8054D5B, fmt @ 0x80857A8).
* Reproduced (session scratchpad, `QNX_TARGET` set, binary invoked via a `mkifs` symlink):

  | build-file attribute | file size | flags1 | startup_size | stored_size |
  |---|---|---|---|---|
  | *(none)* / `compress=0` | 188 (raw imagefs, **no** startup header) | – | – | – |
  | `compress=1` | fails: "Error writing compression file" | 0x04 | 0x104 | 0 |
  | `compress=2` | 364 | 0x08 | 0x104 | 0x16C |
  | `compress=3` | 360 | 0x0C | 0x104 | 0x168 |
  | `+compress` | 360 (identical to compress=3) | 0x0C | 0x104 | 0x168 |
  | `compress=9` | 260, then dies | 0x24 (= 9<<2) | 0x104 | 0 |

  With `compress=0` the condition `!startup && !compressed` makes `ifs_make_fsys` skip the startup
  header entirely (guard at 0x8058A71 / 0x8058A7A) and write only the raw 0xBC-byte imagefs.
* `flags1` bit semantics from the header build: bit0 ← `booter.virtual & 1`
  (0x8058B14–0x8058B2C), bit1 ← target endianness swap (0x8058A9E–0x8058AB1), bits [3:2] ←
  `compressed` (0x8058D1F–0x8058D32).
* `preboot_size` writer: 0x8058CE2 `swap16([ebp-0x6C])` → header +0x30, with `[ebp-0x6C] = bsize =
  booter.data_len` (0x805848F).
* `image_paddr` writer 0x8058B6F–0x8058B93: `image.addr + [ebp-0x6C] + booter.paddr_bias`;
  `paddr_bias` (+0x10) 0x8058B99–0x8058BB2: `swap32(-booter.paddr_bias)`.

---

## Q4 — What must be patched when the imagefs content changes but the startup stub stays?

### Question
Enumerate every `startup_header` field with its rule, for the "edit the .ifs in place, keep the
startup stub" workflow.

### Answer
Only **three** header fields change, plus the 4-byte `Image-trailer` inside the new stream; the
rest must be left exactly as found. `bsize` (= `preboot_size`) and `H` are fixed at 8 and 0x808.

| off | field | rule when only the imagefs changes |
|---|---|---|
| +0x00 | `signature` | **unchanged** — constant `0x00FF7EEB` (`swap32(0xFF7EEB)` @ 0x8058AC7) |
| +0x04 | `version` | **unchanged** — constant 1 (`swap16(1)` @ 0x8058AE2) |
| +0x06 | `flags1` | **unchanged** — `(booter.virtual&1) \| (endian<<1) \| (compressed<<2)`; stock `0x0D` = virtual + UCL. Only changes if you re-encode with another codec (`compress=1`→0x05, `2`→0x09, `0`→0x01) |
| +0x07 | `flags2` | **unchanged** — 0 |
| +0x08 | `header_size` | **unchanged** — constant 0x100 (`swap16(0x100)` @ 0x8058AFE) |
| +0x0A | `machine` | **unchanged** — copied from the startup file struct (`startup->[0x34]`); 0x28 = ARM |
| +0x0C | `startup_vaddr` | **unchanged** — copied from `startup->[0x2C]` |
| +0x10 | `paddr_bias` | **unchanged** — `-booter.paddr_bias` = 0 |
| +0x14 | `image_paddr` | **unchanged** — `image.addr + bsize + booter.paddr_bias` = 0x80100008 |
| +0x18 | `ram_paddr` | **unchanged** — equals `image_paddr` (non-split) = 0x80100008 |
| **+0x1C** | **`ram_size`** | **PATCH: `ram_size = startup_size + imagefs_size`** — verified exactly on both images (0x59108+0x122628C = 0x127F394; 0x86108+0xD19EF4 = 0xD9FFFC); writer path 0x8058CA9 `[ebp-0x54]+[ebp-0x68]` |
| +0x20 | `startup_size` | **unchanged** — `dsize = align8(startup_file_size + bsize + 0x10B) − bsize`; it *includes* the 0x100 header and the 4-byte startup_trailer |
| **+0x24** | **`stored_size`** | **PATCH: `stored_size = startup_size + C + 8`**, `C` = new compressed payload length (equivalently `= new_payload_end − 0x808`). Writer 0x8059B56 |
| +0x28 | `imagefs_paddr` | **unchanged** — never written by mkifs; 0 |
| **+0x2C** | **`imagefs_size`** | **PATCH: new uncompressed imagefs size** = `image_header (0x5C + mountpoint, 4-aligned) + directory + file payloads + alignment pad + 4`. This is mkifs's `isize`, accumulated at 0x80589E4 |
| +0x30 | `preboot_size` (u16) | **unchanged** — `bsize` = 8 |
| +0x32 | `zero0` (u16) | **unchanged** — 0 |
| +0x34…+0x3F | `zero` | **unchanged** — 0 |
| +0x40…+0xFF | `info` | **unchanged / all zero** — verified: no non-zero bytes anywhere in H+0x32..H+0x100 in either stock image; mkfs leaves the memset region untouched |

Trailer words (one outside the stream, one inside):

* **`startup_trailer`, u32 at `H + startup_size − 4`** (0x5990C / 0x8690C): **unchanged**, and it
  stays valid — the whole startup region sums to zero:
  `Σ LE32 over [H, H+startup_size−4) + trailer ≡ 0 (mod 2^32)`, verified exactly on both images
  (0x0716A624 + 0xF8E959DC = 0x1_0000_0000; 0x901F6027 + 0x6FE09FD9 = 0x1_0000_0000). mkifs
  writes it at 0x8058EA3 and re-writes it at 0x8059BB5 (`fwrite` 4 B after `fseek([ebp-0x30])`).
* **`Image-trailer`, the last 4 bytes of the compressed payload** (at `H+stored_size−4`):
  **must be recomputed** — it is `−(Σ LE32 of the uncompressed imagefs)`. Mechanism: `iwrite`
  folds every written byte into `image_cksum` (`0x8092120`, byte-at-a-time into
  `hold_cksum[image_offset & 3]`, folded as an LE dword every 4 bytes; iwrite tail
  0x8054F5A–0x8055010); `image_cksum` is zeroed at 0x8058F57 (right after the startup trailer)
  and negated into the trailer at 0x80599A2–0x80599E1. `padfile` also goes through `iwrite`
  (0x80550D3), so the pad is checksummed too.
* **RIM's appended `QNXH-OS-1` signature block** (0x53F0D0… / 0x598908…) is *outside* the
  `stored_size` span and is not produced by mkifs at all — it must be regenerated/replaced by
  whatever signs these images. `stored_size` deliberately stops at its first byte.

Operational recipe for an in-place edit:
1. decompress the old payload (`[u16 BE len][NRV2B]` chain from `H+startup_size`);
2. rebuild the imagefs (honouring the `imagefs_size` semantics above);
3. recompress (UCL NRV2B, ≤0xFFFF-byte input blocks, 2-byte BE length prefixes);
4. append the 8-byte align pad + the new `Image-trailer`;
5. patch `stored_size` (+0x24), `imagefs_size` (+0x2C), `ram_size` (+0x1C);
6. leave `startup_size`, `preboot_size`, the 8 preboot bytes and the startup region untouched;
7. re-append / re-sign the `QNXH-OS-1` block.

### Evidence
All writer addresses are in the Q1/Q3 sections; the header build is one straight-line block
(memset @ 0x8058A99 → field stores 0x8058AB7–0x8058D32 → `iwrite(shdr,0x100)` @ 0x8058D58), so the
field→source mapping is complete and exhaustive. The three patched fields are the only ones whose
sources depend on imagefs size / compression (`[ebp-0x54]` = isize, `[ebp-0x68]` = dsize,
`ftell`).

---

## Unresolved

1. **Z10 block chain ends 1 byte early.** The `[u16 BE len]` walk from 0x86910 yields 213 blocks
   and terminates at 0x5988FF, one byte before the expected 0x598900 (= `H+stored_size−8`).
   win1103 lands exactly. Either one length prefix is read 1 byte short, or that image carries a
   5-byte pad. 1 byte in 5.3 MB; it does not affect the Q1 formula, which is confirmed
   independently by `startup + C + 8 == stored`.
2. **The exact operand of the final `startup_trailer` rewrite** at 0x8059BB5 is
   `−(ftell−bsize + saved_cksum)`, which under my DWARF slot mapping does not reproduce the
   observed trailer bytes; the *invariant* `Σ LE32[H, H+startup_size) ≡ 0` does hold exactly on
   both images. Either the rewrite lands elsewhere, or `[ebp-0x2C]` is not the saved checksum.
3. **No NRV2B decoder was written**, so the stock payloads were not byte-decompressed. "The
   uncompressed imagefs begins with the 92-byte image_header and totals `imagefs_size`" is
   inferred from (a) the literal header fields at payload+3 matching `imagefs_size` exactly,
   (b) the block chain covering the exact payload span, (c) the code path. No decompressed
   imagefs dump was produced.
4. **RIM's 0x800-byte preamble** (`b +0x800` ARM branch, build tag `ec_agent` / `developer`, 0xAA
   fill) is outside anything mkifs writes; whether the `QNXH-OS-1` signature covers it was not
   determined.
5. **`-c` semantics** — confirmed *not* the compression switch (byte-identical output for
   `-c 0/1/2/3/9`), and `main` @ 0x80654D6 calls `set_cpu()` with it, but the CPU→`machine`
   mapping was not traced (with no startup ELF, `machine` stays 0, so it could not be observed).
6. **`imagefs_size` vs `check_over` / `image.totalsize`**: with `[image=…]` and no explicit
   `total=`, mkfs enforces `payload ≤ align(image.totalsize)`; the Z10 image's `image.totalsize` is
   not recoverable from the container, so the exact value RIM used is unknown.

## What would falsify this

* **F1 (Q1 formula).** Any stock RIM `.ifs` where the first block-length word is not at
  `0x808 + startup_size`, or where `stored_size ≠ startup_size + C + 8` (equivalently, where the
  bytes at `0x808 + stored_size` are not the start of a RIM signature block). One counter-example
  kills the formula.
* **F2 (Q1 base).** An image where the imagefs really begins at `0x908 + startup_size` — i.e. a
  plaintext `imagefs` signature at that offset with `startup_size` excluding the 0x100 header.
* **F3 (Q2 single stream).** Finding a per-file compressed-length field in a dirent, a fresh block
  header aligned to each file start, or two independent decompression contexts needed to recover
  the tree — any of these would mean (b) rather than (a).
* **F4 (Q2 header inside the stream).** Decompressing block 1 and finding its output does *not*
  begin with the 92-byte `imagefs`-signed header, or finding a full, self-consistent plaintext
  `image_header` (`image_size` / `hdr_dir_size` / `dir_offset` / `chain_paddr` all sane) at the
  payload start of a stock image.
* **F5 (Q3 selector).** A build file where `+compress` does not yield `flags1[3:2] = 3`, or a
  command-line option that changes `compressed` (`0x8095F60`) — the disassembly currently shows
  `0x8066BFC` as its only writer.
* **F6 (Q4 `ram_size`).** Any image where `ram_size ≠ startup_size + imagefs_size` (both current
  images satisfy this to the byte).
* **F7 (Q4 trailer invariant).** Any image where `Σ LE32[H, H+startup_size) ≠ 0 mod 2^32`.
* **F8 (`stored_size` semantics).** An image where `stored_size ≠ ftell(EOF) − bsize` — e.g. where
  `stored_size` also covers the appended signature block, or where `preboot_size` is not the size
  of the pre-header blob that precedes the startup_header.
