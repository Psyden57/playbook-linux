# 168 — IFS format internals: checksums, parsing requirements, data layout, dirent defaults

Status: **static RE only** (objdump / readelf / python, plus `dumpifs` runs on *copies* in
a scratch dir). No device, no USB, no vendor data was touched or executed.

Targets and provenance:

| what | path | notes |
|---|---|---|
| `dumpifs` | `local/toolchains/qnx660-master/host/linux/x86/usr/bin/dumpifs` | ELF32 i386, **not stripped**, has debug info |
| `mkifs` → `mkxfs` | `.../usr/bin/mkifs` → symlink to `mkxfs` | ELF32 i386, not stripped, static funcs named |
| headers | `.../target/qnx6/usr/include/sys/image.h`, `sys/startup.h` | QNX 6.6.0 |
| subject image | `local/work/ifs1917.imagefs` (copy only) | 7,900,540 B; startup @ `0x808`, uncompressed imagefs @ `0x44910` |

`mkxfs` retains its static function names, so every claim below is anchored to a symbol,
not to a guessed code region. `sys/image.h` source file recorded in the binary:
`mk_image_fsys.c`.

Reproduce-everything scratch work used copies `t1.bin … t10.bin` of
`local/work/ifs1917.imagefs`; nothing in the repo was written except this file.

---

## 0. Two facts that frame everything else

1. **`dumpifs` never computes either checksum.** It `fseek`s + `fread`s one 4-byte word
   and `printf`s it. Both values in a `dumpifs` listing are *fields*, not results.
   (§Q1, §Q2)
2. **`dumpifs` exits 0 no matter what happens.** `main` ends with
   `0x804940a: mov eax,0x0` — the return value of `process()` is discarded, and
   `error()`'s first argument is inverted relative to BSD `error()`: `error(0,…)`
   prints and `exit(1)`; `error(nonzero,…)` prints and **returns** (`0x8048fac–0x804901b`).
   Every parse failure in `process()` is called as `error(1,…)`, so a malformed image
   produces messages on stderr and **still exit status 0**.

Header field offsets used throughout (32-bit, `unsigned long` = 4; confirmed against the
disassembly, not just the header):

```
startup_header (0x100 B):  +0x00 sig  +0x04 version +0x06 flags1 +0x07 flags2
                           +0x08 header_size +0x0a machine +0x0c startup_vaddr
                           +0x10 paddr_bias +0x14 image_paddr +0x18 ram_paddr
                           +0x1c ram_size  +0x20 startup_size  +0x24 stored_size
                           +0x28 imagefs_paddr +0x2c imagefs_size +0x30 preboot_size
                           +0x32 zero0     +0x34 zero[3]       +0x40 info[48]
image_header (0x5c read):  +0x00 "imagefs" +0x07 flags +0x08 image_size
                           +0x0c hdr_dir_size +0x10 dir_offset +0x14 boot_ino[4]
                           +0x24 script_ino +0x28 chain_paddr +0x2c spare[10]
                           +0x54 mountflags +0x58 mountpoint[]
image_attr (0x18 B):       +0x00 size(u16) +0x02 extattr_offset(u16) +0x04 ino
                           +0x08 mode +0x0c gid +0x10 uid +0x14 mtime
```

The stock 2.1.0.1917 container, decoded (confirms the older notes and corrects one):
`flags1 = 0x0d` = `VIRTUAL(0x01) | COMPRESS_UCL(0x0c)`, i.e. **the `BIGENDIAN` bit 0x02 is
clear** — the startup header is written **little-endian** in this image
(`signature` bytes on disk are `eb 7e ff 00`). `startup_size = 0x44108`,
`image_paddr = ram_paddr = 0x80100008`, `imagefs_size = 0x74446c`,
`imagefs_paddr = 0`. Image header: `flags = 0x04` (INO_BITS), `image_size = 0x74446c`,
`hdr_dir_size = 0x1748`, `dir_offset = 0x5c`, `script_ino = 3`,
`boot_ino = {0xE0000002, 0, 0, 0}`, mountpoint `/`.

---

## Q1 — Image trailer checksum

### Answer

`cksum = (uint32)0 − (Σ 32-bit target-endian words of [header, end−4))`, i.e. the two's
complement of the word sum, **computed by `mkifs`**, and **`dumpifs` only prints the
stored word**. Word width 4, endianness = the *image*'s target endianness, and a trailing
1–3 bytes are **dropped** (never folded in).

### Where `dumpifs` reads it (it does not compute)

`process()` @ `0x8049c6d`:

| address | action |
|---|---|
| `0x804ab58` | `fseek(fp, startup?… )` → actually `image_size`(`+0x08`) + `imagefs_off` − 4 |
| `0x804ab85` | `fread(&v, 4, 1, fp)` |
| `0x804aba8` | on short read → `error(1, "Early end reading image trailer")` |
| `0x804abc1` | `if (startup.flags1 & 0x02) v = bswap32(v)` — endianness fix-up only |
| `0x804ac12`/`0x804ac31` | `printf(" image=%#lx", v)` (fmt `0x80557c0` = `"Checksums: image=%#lx"`) |

There is no accumulation loop anywhere in `dumpifs` and no comparison against a computed
value. `strings` finds no "checksum mismatch"/"bad checksum" text.

### Where `mkifs` computes it

`iwrite()` @ `0x8054de2` — every byte the image writer emits passes through it, and after
writing it always folds that byte into the global accumulator:

```
0x8054f5a  phase = img_offset & 3                  ; 0x8095b44 = bytes emitted so far
0x8054f68  stage[phase] = *buf                     ; 0x8092128 = 4-byte staging buffer
0x8054f74  if (phase == 3) {
0x8054f7e     if (bigendian_flag(0x80917a4))
0x8054f87        word = b0<<24|b1<<16|b2<<8|b3     ; big-endian target
0x8054fcf     else
0x8054fcf        word = b3<<24|b2<<16|b1<<8|b0     ; little-endian target
0x8054fc0     image_cksum(0x8092120) += word       ; 32-bit add, wraps mod 2^32
           }
0x8055022  img_offset++
```

Trailer emission (`ifs_make_fsys` @ `0x80581c2`):

```
0x8058e64  eax = image_cksum
0x8058e69  neg eax                       ; <-- two's complement
0x8058e78  swap32(endian, eax)           ; target endianness
0x8058ea3  iwrite(&val, 4, fp, "image_trailer")
0x8058f57  image_cksum = 0               ; reset for a following section
```

**Tail handling, precisely.** The fold only happens when `phase == 3`, so a body whose
length is not a multiple of 4 leaves its final 1–3 bytes in the staging buffer and they
contribute **nothing** to the checksum. (Because `stage[]` and `img_offset` are globals,
a partial word from one `iwrite` call is completed by bytes of the *next* call — the sum
is over the continuous byte stream, not per-call.) For this image the tail is not
exercised: both bodies are multiples of 4 — startup body `0x44104`, image body `0x744468`.

### Independent reproduction (python, on `local/work/ifs1917.imagefs`)

```
startup: Σ words [0x808, 0x4490c) = 0x6fcef34d   stored @0x4490c = 0x90310cb3   total = 0x00000000
image  : Σ words [0x44910, 0x788d78) = 0xbd87bc93 stored @0x788d78 = 0x4278436d total = 0x00000000
```

Both sum to 0 mod 2^32, exactly as the model predicts.

### What happens on a wrong checksum (test, temp copies only)

| test | patch | result |
|---|---|---|
| baseline | none | `Checksums: image=0x4278436d startup=0x90310cb3`, 106 lines, **rc=0** |
| **t2** | image trailer → `0xdeadbeef` | `Checksums: image=0xdeadbeef …`, **rc=0**, full listing, **no warning** |
| t8 | 4 bytes at `0x50000` (imagefs data) | `image=0x4278436d` unchanged, rc=0 |

**Conclusion: nothing enforces it.** `dumpifs` happily prints `image=0xdeadbeef`. The
`image_size`-derived seek is the only thing tying the trailer to the image, and it is not
a check. Whether RIM's `rifsboot`/IPL enforces the *startup* checksum cannot be settled
from these two binaries (see Q2); the imagefs trailer is only ever seen by the mounted
filesystem driver on the target, which is not among our artifacts.

---

## Q2 — `startup=0x90310cb3`

### Answer: it is a **stored field**, at container offset **`0x4490c`**

`dumpifs` reads it the same way it reads the image trailer (`process()`):

| address | action |
|---|---|
| `0x804ac40` | `fseek(fp, startup_off + startup_size − 4)` — `startup_size` is `startup_header+0x20` |
| `0x804ac86` | `fread(&v,4,1,fp)` |
| `0x804ac90` | short read → `error(1, "Early end reading startup trailer")` (`0x80557d8`) |
| `0x804acb5` | `if (flags1 & 0x02) v = bswap32(v)` |
| `0x804ad0d`/`0x804ad25` | `printf(" startup=%#lx", v)` (fmt `0x80557fa`) |

Exact location for this image:

```
startup_off   = 0x808
startup_size  = 0x44108   (startup_header+0x20)
stored value  = 0x808 + 0x44108 − 4 = 0x4490C = 0x90310cb3
```

i.e. the four bytes **immediately preceding the imagefs**, which is why
`imagefs_off = 0x44910 = 0x808 + startup_size` in the uncompressed container.

### Why changing byte `0x80e` (flags1) does not change it

Because nothing recomputes, and because `0x80e` is *inside* the covered span but the
value shown is not derived from that span. Tests (copies, `flags1` already set to
`0x01` so `dumpifs` treats the body as uncompressed):

| test | patch | printed `startup=` |
|---|---|---|
| t1 | none | `0x90310cb3` |
| **t7** | 4 bytes at **`0x900`** → `ff ff ff ff` (inside the startup region, inside `startup.*`) | **`0x90310cb3` — unchanged** |
| **t4** | the stored word at **`0x4490c`** → `0x11223344` | **`0x11223344` — changes 1:1** |

That pair is conclusive: the number is read from `0x4490c`, not computed over
`[0x808, 0x4490c)`.

### Is the value nonetheless *a* checksum of that range?

Yes — the same algorithm as Q1 reproduces it bit-exactly:
`Σ 32-bit LE words of [0x808, 0x4490c) = 0x6fcef34d`, and `0x6fcef34d + 0x90310cb3 = 0`.
This matches `sys/startup.h`'s IPL contract (`I checksum(image_paddr, startup_size)`,
lines 116/143/173 of the header), so it was *written* as the two's-complement word sum
over `[startup_off, startup_off + startup_size − 4)`. **But `dumpifs` does not verify it**
— Q1's t2 result (a bogus image trailer printed without complaint) and t7 (an edited
startup body printed without a new checksum) settle that. Whether `mkifs` emits this word
through the same `iwrite` accumulator: the writer's startup path (`copy_boot` `0x80550e6`,
`section_write` `0x8055774`) also writes via `iwrite`, and `image_cksum` is only reset
(0x80584fa / 0x8058549) at the transition from startup to imagefs — consistent, but I did
not isolate the exact `iwrite` of the startup trailer word, so that last link is
**inferred**, while "the value equals −(word sum over that range)" is *measured*.

---

## Q3 — `dumpifs` parsing requirements

### 3a. How it locates the imagefs — **scan for the literal, not the header fields**

`process()` @ `0x8049c6d`:

1. `0x8049d0b` `find(fp, "imagefs", 7, limit=0)` — `find()` @ `0x804941b` is a naive
   byte-window matcher over `fgetc`; `limit=0` restricts it to **offset 0**.
2. If that fails: `rewind`, `0x8049d28` `find(fp, &0x00ff7eeb, 4, -1)` (unlimited) —
   locate the startup header; on failure it byte-swaps the signature and retries
   (`0x8049d5a`), then `error(1,"Unable to find startup header in %s")` (`0x8049dd5`).
3. Reads the 0x100-byte startup header at `0x8049df5`, checks the zero fields with
   `zero_ok()` @ `0x804beee` (tests `info[0]`/`zero[0..2]` = +0x34/+0x38/+0x3c).
4. Decompresses if `flags1 & 0x1c` (3b below) into a `tmpfile`, and **keeps working on
   that temp stream** — this is why the listing's "Offset" column is a *decompressed*
   offset.
5. `0x804a3fd` `fseek(fp, startup_off + startup_size, SEEK_SET)` then
   `0x804a440` `find(fp, "imagefs", 7, limit=-1)`; failure →
   `error(1,"Unable to find image header in %s")` (`0x804a44e`).

So: **it uses `startup_header.startup_size` (+0x20) to skip the startup, then scans for
the 7-byte literal.** It never reads `imagefs_paddr` (+0x28) or `imagefs_size` (+0x2c)
for locating. (`imagefs_size` is only displayed, by `display_shdr`; the only other use of
the image header fields is `render` of the listing.)

Nice confirmation of *why* the seek comes first: the literal `"imagefs"` also occurs at
container offset **`0x2a348`**, inside RIM's startup binary (`rifsboot`). A naive
whole-file scan would latch onto that; starting the scan at `0x44910` avoids it.

### 3b. Decompression selection — flags1 read at `startup_header+6`

```
0x8049e76  movzx eax, BYTE PTR [ebp-0x136]   ; [ebp-0x13c] = startup_header base, +6 = flags1
0x8049e80  and   eax, 0x1c                   ; STARTUP_HDR_FLAGS1_COMPRESS_MASK
0x8049e85  je    no-compression              ; -> 0x804a2d4
```
Dispatch at `0x8049fc1` (permits only these four values):

| `flags1 & 0x1c` | meaning | code |
|---|---|---|
| `0x00` | none — parse the file in place | jump `0x804a2d4` |
| `0x04` | zlib | `gzdopen(fd,"rb")` @ `0x804a030` + `gzread` loop `0x804a08f` → temp file |
| `0x08` | LZO | `0x804a1d9` |
| `0x0c` | UCL/NRV2B | `0x804a0a2` |
| other | — | `0x804a29f` |

Note the values are *tested exhaustively* (`cmp 0x8 / cmp 0xc / cmp 0x4 / else`) — a
`flags1` with, say, `0x10` in the mask bits falls to the `else` and fails.

Also note the endianness flag is re-tested at `0x804a2d4` (`flags1 & 0x02`) and, when set,
byte-swaps the 16-bit fields `+4 (version)`, `+8 (header_size)`, `+0xa (machine)`,
`+0x30 (preboot_size)` and the 32-bit fields `+0x0c … +0x2c` (`0x804a2e9–0x804a3fd`).

### 3c. Validation of `image_size` / `hdr_dir_size` / `dir_offset` — **none**

* No `stat`/`fstat` call exists in `dumpifs` at all (grep over the full disassembly:
  only `lseek64` inside zlib). There is therefore **no comparison of the file's real
  length against `image_size`** anywhere.
* `dir_offset` is used blindly as the start of the dirent walk (`0x804a52b`:
  `cur = imagefs_off + dir_offset`).
* `hdr_dir_size` is used only for the pretty-printed `Image-directory` line
  (`0x804a625`: `hdr_dir_size − dir_offset`, printed via fmt `0x805572a`).

### 3d. The 4-byte zero terminator **is required**

Dirent loop head @ `0x804a65b` … loop back-edge @ `0x804ab3b`:

```
0x804a679  fseek(fp, cur, SEEK_SET)
0x804a69b  fread(attr, 0x18, 1, fp)          ; struct image_attr
0x804a6a5   failing -> error(1,"Early end reading directory")   ; 0x8055749
0x804a77f  size = attr.size (u16)
0x804a785  if (size <= 0x17) {
0x804a791      if (size == 0) break;                            ; <-- the terminator
0x804a79a      error(1,"Invalid dir entry")                     ; 0x8055765
           }
0x804a7d4  extra = size - 0x18 (capped: size>0x400 -> 0x3e8)
0x804a80b  fread(buf+0x18, extra, 1, fp)  ; failing -> error(1,"Error reading directory") ;0x8055777
0x804a837  cur += size
```
The loop is terminated **only** by a zero `attr.size`; the "end of directory" is not
derived from `hdr_dir_size`. Test t9 (zero word at `0x46054` replaced by
`18 00 00 00`): the listing grew from **106 to 108 lines** — `dumpifs` dutifully parsed
two bogus entries out of the following file data, then stopped on a word that happened to
be zero. So a writer **must** emit the 4-byte zero word (and it does — see Q5's
"align4 + 4 zero bytes" note, which makes that zero word fall out naturally).

Allowed dirent sizes: `0x18` for a bare attr, up to a sane cap of `0x400`
(larger sizes are read truncated to `0x3e8` extra bytes but still advance `cur` by the
declared size).

### 3e. Exact "Early end reading directory" condition

`fread()` of the **0x18-byte `image_attr`** returns != 1 — i.e. fewer than 24 bytes remain
between `cur` and EOF, *or* the read failed. Triggered from `0x804a6a5`. It is **not**
raised when `hdr_dir_size` is exceeded; `hdr_dir_size` plays no part in the loop.

Related, distinct conditions:

| message | string addr | raised at | cause |
|---|---|---|---|
| `Early end reading directory` | `0x8055749` | `0x804a6a5` | `fread(attr,0x18)` short — no zero terminator before EOF |
| `Invalid dir entry` | `0x8055765` | `0x804a79a` | `0 < size <= 0x17` |
| `Error reading directory` | `0x8055777` | `0x804a815` | `fread` of `size−0x18` extra bytes short |
| `Early end reading image trailer` | `0x80557a0` | `0x804aba8` | `image_size` points past EOF |
| `Early end reading startup trailer` | `0x80557d8` | `0x804ac90` | `startup_size` points past EOF |

All are `error(1,…)` → message on stderr, **exit status still 0**.

### 3f. Actual file length vs `image_size` — rejected? **No**

| test | patch | result |
|---|---|---|
| t5 | `image_size += 0x100` (file now shorter than `image_size`) | prints the full 106-line listing, then `dumpifs: Early end reading image trailer` on stderr, **rc=0** |
| t6 | `image_size = 0x1000` (much smaller than the file) | prints `Checksums: image=0 … startup=0x90310cb3`, **rc=0**, no complaint |

So: *shorter than `image_size`* → only the trailer `fread` fails, after the whole listing
has already been printed. *Longer than `image_size`* → completely undetected; the trailer
is simply read from `imagefs_off + image_size − 4`, i.e. from the middle of the file.

---

## Q4 — `mkifs` data layout

### 4a. The rule: alignment is applied to the **physical address**, not the file offset

`locate_files()` @ `0x8057afd` (called from `ifs_make_fsys` before anything is written)
precomputes each entry's `dirent.offset` into the entry node at `+0x24`:

```
; --- choose the alignment, [ebp-0x24] ---
0x8057b43  if (st->[0x3d] & 0x40)          align = pagesize
0x8057b5c  if (st->[0x3d] & 0xffffff80)    align = max(pagesize, ent->[0x60])
0x8057b81  if (st->[0x40])                 align = max(pagesize, st->[0x40])
0x8057ba5  ent->[0x60] = align             ; remembered
0x8057bda  if (pagesize > align && (st->[0x38] & 1)) align = max(pagesize, align)

; --- place it, [ebp+0xc] = running offset, ent->[0x1c] = data size ---
0x8057bcb  cur = image.paddr(0x8095e80) + running_offset
0x8057c54  cur = (cur + align - 1) & -align          ; <-- align in ADDRESS space
0x8057c84  ent->[0x24] = cur - image.paddr           ; dirent.offset (from header)
0x8057eda  running_offset = ent->[0x24] + ent->[0x1c]
```

`pagesize` is the global `0x8094ac4`, defaulted to **`0x1000`** at `0x8058266`
(`if (!pagesize) pagesize = 0x1000;`). The `align` value is `max(page_align, page size)`
— i.e. `page_align` can only *raise* the granularity, never lower it below a page.

Emitting is then exact: `padfile()` @ `0x805505a` zero-fills the container until
`img_offset == ent->[0x24]`, and the call site for file data is `0x805966e`
(`padfile(fp, ent->[0x24], ent->0x4)`), guarded by `mode == S_IFREG` at `0x8059648`.
`padfile` refuses to run backwards (`0x8055068`: `img_offset > target` →
`error_exit`), so the precomputed offsets are authoritative.

### 4b. Why the phase is `0x800` (absolute) / `0xEF0` (imagefs-relative)

Because `cur` is a **RAM address** and `image.paddr` is not page-aligned. `image` (24 bytes
@ `0x8095e80`) is a BSS struct filled by `fill_addr_defaults()` @ `0x80542d3` from
`default_image` (`0x8094ad0`), which in turn is produced by
`parse_addr_space_spec(&default_image, str)` @ `0x8049dac`, called from
`parse_booter_attr` — i.e. from the bootfile's **`[image=…]`** attribute. (That attribute
lives in RIM's `.build`, which we do not have; `default_image` is `.bss`, so the literal
cannot be read out of `mkifs`.)

For this image the imagefs base in RAM is

```
startup_header.image_paddr + startup_size = 0x80100008 + 0x44108 = 0x80544110
0x80544110 mod 0x1000 = 0x110
⇒ dirent.offset ≡ -0x110 ≡ 0xEF0 (mod 0x1000)
⇒ container offset = 0x44910 + 0xEF0 ≡ 0x800 (mod 0x1000)     <-- the observed phase
```

Check against the actual table — every file lands exactly on a 4 KB page in RAM:

| file | `dirent.offset` | + `0x80544110` | page-aligned |
|---|---|---|---|
| `proc/boot/procnto-smp-instr` | `0x1EF0` | `0x80546000` | yes |
| `proc/boot/.script` | `0x9CEF0` | `0x805E1000` | yes |
| `proc/boot/splash_script` | `0x9EEF0` | `0x805E3000` | yes |
| `proc/boot/bmetrics_cli` | `0x740EF0` | `0x80C85000` | yes |

This also explains the second observation directly: **successive files are not
`align_up(end, 0x800)`** because (a) the granularity is **`0x1000`** (page), not `0x800`,
and (b) the alignment is performed in address space carrying a `0x110` phase, so the
*file-offset* phase is `0xEF0`, and `0x800` is what that looks like once the `0x44910`
imagefs start (itself `0x910` into a page) is added.

Worked example (the `.cfg` pair, sizes that are not multiples of the page):
`0x506EF0 + 1765 = 0x5075D5` → next is `0x507EF0 = 0x508000 − 0x110`, i.e.
`align_up(0x5075D5 + 0x110, 0x1000) − 0x110`. ✔

The `[image=…]`-derived value `0x80544110` is the one thing here I am **inferring** rather
than reading out of the binary (the attribute is in RIM's bootfile); the congruence
`image.paddr ≡ 0x110 (mod 0x1000)` is *derived from the observed data* and the code path
that produces it is observed. Everything else in 4a is read off the disassembly.

`phys_align=` and `pagesizes=`/`big_pages` are also IFS attributes (see Q5's attribute
table); `phys_align` acts at the container level, `page_align` per file — and
`locate_files` honours only `page_align` (plus the page-size floor).

### 4c. Dirent order

1. **The root dirent is written first and unconditionally** — `ifs_make_fsys` @
   `0x80590bc`: `ino = 1`, `mode = 0x41ed` (`S_IFDIR|0775` = `040755`), `uid = gid = 0`,
   `mtime` = the global `0x8095e5c`, `extattr_offset = 0`, `size = 0x1c`, path `""`
   (the single byte at `+0x18` set to 0), labelled `"Root-dirent"` (`0x8094b40` buffer).
2. **Then the entry list, in the order `mkifs` built it from the `.build` file.** It is
   not sorted: the stock order is `proc/boot/procnto-smp-instr`, `.script`,
   `splash_script`, `pmic-twl6030-ctl_script`, the 13 `base/…` symlinks, `root/.profile`,
   then the library cluster, then the rest of `proc/boot`. That is exactly the `.build`
   order, so dirent order = bootfile order (with the startup pulled out, below).
3. The **startup/bootstrap entry is removed from the list** before numbering
   (`0x8058385`: entries with `flags & 4` are recorded in `[ebp-0x40]` and unlinked,
   `*[ebp-0x48] = ent->[0]`) and is written as the `startup.*` pseudo-entry; its dirent
   offset is `hdr_dir_size + 0x100`-based (`0x8058553–0x8058567`).

### 4d. `ino` assignment, `boot_ino[0] = 0xE0000002`, `script_ino = 3`

Numbering pass @ `0x80582e1–0x80583ad`:

```
0x80582d4  counter([ebp-0x4c]) = 1
0x805837e  if (ent->[0x38] & 4) -> startup entry: skip (no number)
0x805839a  ++counter
0x80583a4  ent->[0x40] = counter        ; the entry's ino
```

So: root = **1** (hard-coded, `0x805910c`: `swap32(1)` → `dirent+4`), the first listed
entry = **2**, then 3, 4, 5, … . `proc/boot/.script` is the third numbered entry →
`script_ino = 3` ✔ (and its dirent ino is 3 in the table).

Tags are OR'd in a later pass @ `0x805884b–0x80588a8`:

```
0x805885c  if (ent->flags & 0x110) ent->ino |= 0x80000000   ; IFS_INO_PROCESSED_ELF
0x805887d      if (flags & 0x40)   ent->ino |= 0x40000000   ; IFS_INO_RUNONCE_ELF
0x80588a2  if (flags & 0x01)       ent->ino |= 0x20000000   ; IFS_INO_BOOTSTRAP_EXE
```

`0xE0000002 = 0x80000000 | 0x40000000 | 0x20000000 | 2` — the startup entry is all three
(processed ELF + run-once + bootstrap executable) and it is the **second** entry, hence
`boot_ino[0] = 0xE0000002`. That is an exact match, not an approximation.

**One thing I did not fully resolve (stated rather than guessed):** the *processed-ELF
cluster* carries low inos that restart at 4 (`libc.so.3 = 0xC0000004`,
`libc.so = 0xC0000005`, … `libsbgse56.so = 0xC0000011`, `libsecure_dispatcher = 0xC0000013`)
even though those entries are the 20th+ in emission order, under which the single counter
above would give `0xC0000014…`. There *is* a second counter initialised to **4** at
`0x80582c6` (`[ebp-0x58] = 4`), consumed at `0x80589f6` (`add eax,[ebp-0x58]`) and advanced
at `0x8058a6a`. The observed `0xC000000N` run (4,5,…,0x11, skipping over the plain-ino
symlinks 31/34, resuming at 0x13) is consistent with that counter, but I did not trace
which entries take it, so the exact interleaving of the two counters is **open**. What is
certain: `ino = counter | tags`, root = 1, `boot_ino[0]` = 2|0xE0000000,
`script_ino` = 3, and plain (non-ELF) entries carry the pure counter value
(`libhuapi.so.5.6 = 31`, `libsbgse56.so.0.0 = 34`, `sanity.cfg = 90`).

---

## Q5 — Dirent defaults, `extattr_offset`, symlink encoding

### 5a. Field values

Root dirent (built at `0x80590bc–0x8059180`):

| field | value | where |
|---|---|---|
| `size` | `0x1c` | `0x80590e8` (via `swap16`) |
| `extattr_offset` | **0** | `0x80590f1` `mov WORD PTR [eax+0x2],0x0` |
| `ino` | **1** | `0x805910c` `swap32(1)` → `+0x4` |
| `mode` | `0x41ED` = `040755` | `0x8059124` `swap32(0x41ed)` → `+0x8` |
| `gid` | **0** | `0x8059153` `mov DWORD PTR [eax+0xc],0x0` |
| `uid` | **0** | `0x805915a` `mov DWORD PTR [eax+0x10],0x0` |
| `mtime` | global `0x8095e5c` | `0x8059131` → `+0x14` |

Per-entry dirent (filled at `0x80593b1–0x80594a0`):

| field | value | where |
|---|---|---|
| `size` | computed per type (below) | `0x80593b4` |
| `extattr_offset` | **0**, again explicit | `0x80593ba` |
| `ino` | `ent->[0x40]` (counter + tags) | `0x80593dd` |
| `mtime` | `fiptime(ent)` — the host file's mtime, or the `mtime=` attribute | `0x80593e6` |
| `mode` | base from the stat block, then dir/file permission masks; `\|= 0x49` when the `flags & 0x20` "exec" bit is set (`0x805941b`); directories take `st->[0x30]/[0x34]`, files `st->[0x28]/[0x2c]`; the `+x`/`0x200` fixups at `0x805948c–0x80594a0` | `0x8059405–0x80594a0` |
| `uid`, `gid` | **never assigned from the host file — observed `0` for all 99 dirents** (the root dirent hard-codes 0; the per-entry path has no store to `+0xc`/`+0x10`) | — |

Measured over the real image: `uid ∈ {0}`, `gid ∈ {0}`, `extattr_offset ∈ {0}` for all
99 entries; modes are only `040755`, `100700`, `100600`, `100755`, `101700`, `101755`,
`120755`, `121755` (dir / plain file / config / executable / script / lib / symlinks);
6 distinct `mtime` values, i.e. mtimes are genuine source timestamps, not a build stamp.

So the answer to "what does `mkifs` put in": **uid = gid = 0, `mode` from the source tree
plus the exec attribute, `mtime` from the source, `extattr_offset` = 0 always.**

### 5b. `extattr_offset` is dead

It is written as `0` by *both* dirent builders (`0x80590f1`, `0x80593ba`) and never
populated. `mkxfs`'s IFS attribute table (`ifs_attr_table` @ `0x8091a20`, 21 entries of
`{name, id}`) has no extattr entry at all:

```
autolink, chain=, code=, data=, image=, keeplinked, linker=, physical=, ram=, raw,
relocs, script, virtual=, compress, compress=, page_align, keepsection=, module=,
phys_align=, pagesizes=, big_pages
```

So extended attributes are simply not produced by this tool version; a reader should
treat `extattr_offset != 0` as unimplemented.

### 5c. Symlink encoding — `sym_offset` is relative to the **path field**, `= strlen(path)+1`

Code @ `0x80591d6–0x8059278`:

```
0x80591d6  len = 0x1c                              ; path offset for a symlink dirent
0x80591ff  n = sprintf(buf + 0x1c, "%s", name) + 1 ; path incl. NUL
0x8059215  dirent[+0x18] = swap16(n)               ; sym_offset = strlen(path)+1
0x8059226  len += n
0x805924b  m = sprintf(buf + len, "%s", target)    ; link text (no separate +1)
0x805925e  dirent[+0x1a] = swap16(m)               ; sym_size = strlen(target)
0x8059272  len = len + m + 1
```

Layout: `attr(0x18) | sym_offset(u16) | sym_size(u16) | path NUL | target` — so the target
text begins at **`dirent + 0x1c + sym_offset`**, and `sym_size` **excludes** the NUL.
`name`/`target` are written with `"%s"` (fmt `0x8055be3`), and paths carry **no leading
slash** (confirmed on all 99 entries).

Verified for **all 26 symlinks in the image, 0 mismatches**:

| path | strlen+1 | `sym_offset` | `sym_size` | target |
|---|---|---|---|---|
| `base/bin/login` | 15 | 15 | 16 | `/proc/boot/login` |
| `bin/sh` | 7 | 7 | 14 | `/proc/boot/ksh` |
| `tmp` | 4 | 4 | 10 | `/dev/shmem` |
| `proc/boot/libhuapi.so.5.6` | 26 | 26 | 13 | `libhuapi.so.5` |
| `base/usr/hmi/images/request_charge.bmp` | 39 | 39 | 29 | `/proc/boot/request_charge.bmp` |

(Relative targets such as `libhuapi.so.5` and absolute ones such as `/dev/shmem` are
stored verbatim — no rewriting.)

### 5d. Dirent size formulas (and where the zero terminator comes from)

From `0x80591a5–0x8059397`, `size = align4(body)` with a guaranteed 4-byte zero pad
(`0x805936e`: `memset(buf+len, 0, 4)`; `0x8059394`: `len = (len+3) & ~3`):

| type | body | path offset |
|---|---|---|
| `S_IFLNK` `0xA000` | `0x1c + strlen(path)+1 + strlen(target)+1` | `+0x1c` |
| `S_IFREG` `0x8000` | `0x20 + strlen(path)+1` (plus `offset` @`+0x18`, `size` @`+0x1c`) | `+0x20` |
| `S_IFDIR` `0x4000` | `0x18 + strlen(path)+1` | `+0x18` |
| device `0x1/0x2/0x5/0x6` | `0x20 + strlen(path)+1` (`dev` @`+0x18`, `rdev` @`+0x1c` = 0 here) | `+0x20` |

Each dirent therefore ends with at least 4 bytes of zero padding, and the *last* dirent's
padding is what `dumpifs` reads as the mandatory 4-byte zero terminator (Q3d) — which is
why the stock image's directory area ends at `hdr_dir_size = 0x1748` with a `0x00000000`
word at `0x46054`.

---

## What is settled / what is not

**Settled by reading code + measuring the image**

* Image trailer = two's complement of the 32-bit target-endian word sum over
  `[header, end−4)`; trailing 1–3 bytes dropped; written by `iwrite`/`ifs_make_fsys`
  (`0x8054de2`, `0x8058e64`); **not verified by `dumpifs`**, which prints the stored word
  (`0x804abc1`) and exits 0 even when it is nonsense.
* `startup=…` is a **stored field at `0x4490c`** (`startup_off + startup_size − 4`), also
  unverified; editing the startup body cannot change it, editing that word changes it
  1:1.
* `dumpifs` finds the imagefs by scanning for `"imagefs"` from `startup_off+startup_size`,
  decompresses per `flags1 & 0x1c` (`zlib`/`LZO`/`UCL` only), validates nothing about
  `image_size`/`hdr_dir_size`/`dir_offset` or the real file length, and **requires** the
  zero dirent terminator (`fread(attr,0x18)` failure ⇒ `Early end reading directory`).
  It always exits 0.
* File data placement = `align_up(image.paddr + running_offset, align) − image.paddr`,
  `align = max(page_align, pagesize=0x1000)`; hence the `0xEF0` relative / `0x800`
  absolute phase, and hence *not* `align_up(end, 0x800)`.
* Dirent order = `.build` order after the root; ino = one increasing counter (root = 1,
  startup skipped) OR'd with `IFS_INO_*`; `boot_ino[0] = 0xE0000002` falls out exactly;
  `script_ino = 3`.
* uid = gid = 0, `extattr_offset` always 0, symlinks `sym_offset = strlen(path)+1`
  relative to the path field with `sym_size = strlen(target)`.

**Not settled / explicitly not guessed**

1. Whether anything on the *target* enforces either checksum (IPL/`rifsboot` for the
   startup word, the imagefs driver for the trailer). Not answerable from `dumpifs`/`mkifs`;
   `dumpifs` certainly does not.
2. The literal `[image=…]` value RIM's `.build` passes (hence the exact `image.paddr`).
   Derived only as `≡ 0x110 (mod 0x1000)`; `0x80544110 = image_paddr + startup_size` fits
   every observed offset, but it is an inference, not a read.
3. The exact rule interleaving the two ino counters that yields `0xC0000004` for
   `libc.so.3` (§4d). Two counters exist (`0x80582d4` start 1, `0x80582c6` start 4); the
   detailed per-entry choice was not traced.
4. `dumpifs` crashes (SIGSEGV, rc=139) rather than erroring when `flags1` advertises
   compression for a body that is not compressed — reproducible: an uncompressed imagefs
   with `flags1 & 0x1c != 0` kills it with no output. Worth knowing before building an
   image by hand: **clear `0x1c` in `flags1` for an uncompressed imagefs**, as
   `docs/project/IFS-REBUILD.md` §5 intends.
