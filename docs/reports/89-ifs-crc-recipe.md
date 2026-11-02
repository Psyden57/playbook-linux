# 89 — IFS header layout, image end, and the CRC-seal recipe (Phase 0 / Phase 3)

Track **B** of the native-boot effort (`85-linux-boot-plan.md` §Phase 0 / §Phase 3).
**Strictly read-only on hardware**: no USB, no NVRAM/RPMB/eMMC/flash access, no
reboots, no writes to the unit. Everything below is static analysis of
`hd0head.bin` plus host-side code that was executed on this workstation only.

Date: 2026-10-25.
Target: `bootdumps-2026-09-11/hd0head.bin` (`hd0` 0x0–0x200000, load base
`0x80000000`, pre-relocation image: absolute literals need `+0x9000`,
`73`/`HANDOFF-2026-10-21` §0).
Deliverable: this report + `analysis/89-crc_tool.py`.

Method: Capstone 5.0.7 (`CS_ARCH_ARM`, per-function `CS_MODE_THUMB` /
`CS_MODE_ARM` — the mode map of `71` §1 was reused, not re-derived), raw-byte
reads for every literal, plus `bb10mt-main/uconsts.pas` / `uinfo.pas` as an
independent description of the same on-disk structures (Barry-derived
`TLoadPtr`, `TOSMetrics`, `TSigsBlockv2`, `GenDummySig`).

Confidence labels used throughout: **[P] proven** = read directly out of the
disassembly or a byte comparison; **[I] inferred** = the only reading
consistent with all the evidence, but not individually proved; **[?] open** =
cannot be settled without the raw `hd1` image.

---

## 0. Executive summary

1. **The verifier consumes a RAM structure, not the raw header.** `0x80014610`
   walks `global -> *(global+0x28) -> descriptor`, where `global` is the image
   load base. With the live header words this resolves **exactly** onto the
   `TLoadPtr` + `TOSMetrics` layout that `bb10mt` already documents
   (`hdr+0x04` = `TLoadPtr`, `MetricsPtr` at `hdr+0x28` = `hdr+0x2C` = the
   `TOSMetrics` block). Every one of the five `0x80014610` predicates then lands
   on a named field. **[P] for the arithmetic, [I] for the media mapping.**
2. **`0x803BF610` is the image END, not the entry point.** It is the `LoadEnd`
   cell (`hdr+0x08`), reached as `end` through `desc+0x0C -> hdr+0x38 ->
   hdr+0x08`. `85`/`87` call it "entry"; that is a misreading. Image size =
   `LoadEnd - LoadStart` = **0x2BFE10 (2,882,064 bytes)**. **[I→P]** (see §2).
3. **The total size is not stored in a length field.** `desc+0x04` = `0x1CC` is
   the *metrics-block* length. The image end comes from the header's
   `LoadEnd`. The `0x20000000 END` and the `0xC00000 RFS` boundary in
   `winchester.py` are zone boundaries the verifier never looks at. **[P]**
4. **Trailer offsets confirmed from disassembly**: ECDSA trailer =
   `[end-0x230, end)`, magic `0xB5A60BFD` at `end-0x178`, key id at
   `end-0x210`, `(r,s)` at `end-0x208`, cookie `0xD7C82D1F` at `end-8`;
   **CRC = `u32` at `end-4`**, over `[start, end-4)`. **[P]**
5. **The `(value & 0xff00ffff)` compare is a MODEL-ID match, not anti-rollback.**
   The reference value is `*(0x8001D04C)` = the bootrom metrics **`modelID`**
   (`0x06001A06` on this unit), compared against `desc+0x7C` =
   `TOSMetrics.hardware_id`. The `0xff00ffff` mask ignores bits 16–23. **[P]**
6. **`desc+0xD4 == 6` means "OS type 6" = `STP_PROTOCOL_TYPE_UNTRUSTED_OS`
   (bit 6, value 64)** in the `bb10mt` type table. The CRC path is literally the
   *untrusted / unsigned OS* path. **[P for the constant, I for the name]**
7. **NEW, and it changes `85` Phase 3/4: the factory path verifies REGION 0
   ONLY.** `0x800142B0` reads the `BOOTCTR_FCTRY` selector; if it is non-zero
   the driver pins the region index to 0 and makes exactly **one** call to
   `0x80014148`, then returns (`0x800142BC`–`0x800142C8`). It never reads NVRAM
   `0x2074`, never tries the other region, and has **no A/B fallback**. The
   plan's "CRC-valid original fallback in region 0 while the modified image
   lives in region 1" is therefore impossible: on the factory path region 1 is
   never examined. **[P]**
8. **NEW: a successful factory boot DECREMENTS `BOOTCTR_FCTRY`**
   (`0x80014A42`–`0x80014A4A` → `0x8000911E` → `0x800115E6`). Every factory-mode
   boot costs one of the ≤50 budget units. **[P]**
9. The CRC is `zlib.crc32` (`CRC-32/ISO-HDLC`) byte for byte: the runtime table
   generator `0x80013ED4` was emulated and produces the standard reflected
   table from poly `0x04C11DB7`; `"123456789"` → `0xCBF43926`. **[P]** Tool:
   `analysis/89-crc_tool.py` (`selftest` passes).
10. The CRC covers **the header itself, including the `+0xD4` byte and
    everything else in the first 0x200 bytes** — `start` = `LoadStart` = the
    first byte of the header. So the byte must be patched *before* the CRC is
    computed. **[P]**

---

## 1. The IFS header, first 0x200 bytes, field by field

### 1.1 How the fields were recovered

`0x80014610(global, out)` is the structural check. Reconstructed from bytes
(reproduce with the snippet in §10):

```
80014612  mov r3,r0                 ; r3 = global  (param 1)
80014614  add.w r2,r0,#0x28
8001461c  bl 0x80013fdc             ; ptr_valid(&global->f28)
80014622  ldr r2,[r2]               ; r2 = desc = *(global+0x28)
80014626  bl 0x80013fdc             ; ptr_valid(desc)
8001462c  ldr r1,[pc,#0x2c4]        ; 0xC3D0E193   (pool @0x800148F4)
8001462e  ldr.w r0,[r2,#0x9c]       ; desc->magic
80014632  cmp r0,r1 ; bne fail
80014636  ldr r1,[r2,#4]            ; r1 = desc->length
80014638  subs r0,r2,#4
8001463a  add r0,r1                 ; r0 = desc-4+length
8001463c  bl 0x80013fdc             ; ptr_valid
80014642  ldr r0,[r2]               ; desc->version
80014644  cmp.w r0,#0x20000 ; blo fail
8001464a  ldr r5,[r2,#8]            ; r5 = desc->load_base_ptr
8001464e  bl 0x80013fdc             ; ptr_valid
80014654  ldr r0,[r5]
80014656  cmp r0,r3 ; bne fail      ; *(desc->load_base_ptr) == global
8001465a  ldr r3,[r2,#0xc]          ; r3 = desc->load_end_ptr
8001465e  bl 0x80013fdc             ; ptr_valid
80014664  ldr r0,[r3]
80014666  subs r0,r0,#4
80014668  bl 0x80013fdc             ; ptr_valid(*(desc->load_end_ptr)-4)
8001466e  ldr r0,[r3]
80014670  ldr r1,[r0,#-4]           ; *(end-4)
80014674  adds r1,#1 ; beq fail     ; end-4 != 0xFFFFFFFF
80014678  ldr r0,[r0,#-8]           ; *(end-8)
8001467c  ldr r1,[pc,#0x278]        ; 0xD7C82D1F   (pool @0x800148F8)
8001467e  cmp r0,r1 ; beq ok
80014688  str r2,[r4]               ; *out = desc
```

`0x80013FDC` (`ptr_valid`) **[P]**:

```
80013fdc  lsls r1,r0,#0x1e  ; 4-byte aligned?
80013fe0  ldr r1,[pc,#0x3f4]  ; 0x800FF800   (pool @0x800143D8)  -> lower bound
80013fe4  cmp r0,r1 ; blo -> 0
80013fe6  ldr r1,[pc,#0x3f4]  ; 0x800102B0   (pool @0x800143DC)
80013fe8  ldr r1,[r1]         ; upper bound is a RAM word, not a constant
80013fea  cmp r0,r1 ; bhi -> 0
80013fee  movs r0,#1
```

So the valid window is `[0x800FF800, *(0x800102B0))` — the lower bound is the
image load base, the upper bound is a runtime word (the reserved image window).
Both bounds are consistent with `global == 0x800FF800` **[P]**.

`global` itself comes from `*(0x80026144)` (`0x80014178`, literal `0x800143E8`);
`0x8001417E`–`0x8001419C` requires `ptr_valid(global)`,
`ptr_valid(global+0x24)`, `*(global+0x24) == global`, and only then calls
`0x80014610`. That predicate is exactly `TLoadPtr.LoadBase == image base`, and
the live word at header `+0x24` **is** `0x800FF800`. **[P]**

### 1.2 The layout

Live sample = `85` §4 (`hd1+0x100000`, 15 u32; the 16th word was not quoted).
`desc` = `*(hdr+0x28)` = `0x800FF82C` = `hdr+0x2C`.

#### A. Startup header, `hdr+0x000 .. 0x003` **[P for the word, I for the role]**

| off | size | field | live | note |
|---|---|---|---|---|
| `0x000` | u32 | `startup_branch` | `0xEA0001FE` | ARM `b` with `imm24=0x1FE` → `PC+8+0x7F8` = image `+0x800` = `0x80100000`. Executed, never compared. |

#### B. `TLoadPtr` at `hdr+0x004` (0x28 bytes) — matches `bb10mt` `TLoadPtr` **[P]**

| off | size | field | live | verified by the stage? |
|---|---|---|---|---|
| `0x004` | u32 | `LoadStart` (RAM) | `0x800FF800` | ✔ **yes** — `*(desc->load_base_ptr)` must equal `global` (`0x80014654`), and it is used as `start` for both verifiers |
| `0x008` | u32 | `LoadEnd` (RAM) | `0x803BF610` | ✔ **yes** — read as `end` through `desc->load_end_ptr`; checked by `*(end-4)!=0xFFFFFFFF` and `*(end-8)==0xD7C82D1F` |
| `0x00C` | 20 B | reserved, zero | 0 | – (all five words are 0, matching the 20-byte `Reserved` array) |
| `0x020` | u32 | `Cookie` | `0xD7A82D1F` | ✘ **not checked by the stage** (`bb10mt` uses it; the stage checks `end-8` = `0xD7C82D1F` instead) |
| `0x024` | u32 | `LoadBase` | `0x800FF800` | ✔ **yes** — `*(global+0x24) == global` (`0x8001418E`) |
| `0x028` | u32 | `MetricsPtr` | `0x800FF82C` | ✔ **yes** — dereferenced to become `desc` (`0x80014622`) |

#### C. `TOSMetrics` at `hdr+0x02C` (= `desc`), `desc+0x00 .. 0x1CB` **[P for the
offsets, I for the names beyond +0x10]**

| off | size | field (bb10mt `TOSMetrics`) | live | verified by the stage? |
|---|---|---|---|---|
| `desc+0x000` | u32 | `version` | `0x00030012` | ✔ **yes** — `>= 0x00020000` at `0x80014644`; also `>= 0x0003000C` at `0x80013F74` (feature gate) |
| `desc+0x004` | u32 | `length` (metrics-block length) | `0x000001CC` | ✔ **yes** — `ptr_valid(desc-4+length)` at `0x8001463C`; `ptr_valid(desc+0x1CC)` also in both verifiers. **This is NOT the image size.** |
| `desc+0x008` | u32 | `load_base_ptr` | `0x800FF804` | ✔ **yes** — must be a window pointer, and `*it == global` |
| `desc+0x00C` | u32 | `load_end_ptr` | `0x800FF808` | ✔ **yes** — must be a window pointer, and `*it - 4` must be one too |
| `desc+0x010` | u32 | `ram_end` | — | – |
| `desc+0x014` | 16 B | `build_user` | — | – |
| `desc+0x024` | 12 B | `build_date` | — | – |
| `desc+0x030` | 12 B | `build_time` | — | – |
| `desc+0x03C` | 64 B | `device_string` | — | – |
| `desc+0x07C` | u32 | `hardware_id` | **must** be `0x06??0?06` | ✔ **yes** — `(value & 0xff00ffff) == (desc+0x7C & 0xff00ffff)` at `0x80014240`, value = bootrom `modelID` (§3.3) |
| `desc+0x080` | u32 | `os_version` | — | ✔ used — looked up in the OS-type blacklist (`0x80013F64` → `0x80013F34`) |
| `desc+0x084 .. 0x09B` | 24 B | *unnamed here* | — | – |
| `desc+0x09C` | u32 | **magic `0xC3D0E193`** | — | ✔ **yes** — `0x80014632` |
| `desc+0x0A0 .. 0x0D3` | 52 B | *unnamed here* | — | – |
| `desc+0x0D4` | u8 | **`os_type`** | **must be 6** | ✔ **yes** — `0x80013FB6` (`uxtb`, `cmp #6`); also the bit index for the blacklist and for the NVRAM `0x205A` model override |
| `desc+0x0D5 .. 0x1CB` | 0xF7 B | *unnamed here* | — | – |
| `desc+0x1CC` | u32 | *tail* | — | pointer-validity probe only (`0x8001472A`, `0x800147A2`) |

`0x2C + 0x1CC = 0x1F8`, so the metrics block ends at `hdr+0x1F8`; the payload
therefore starts somewhere in `0x1F8..0x200`. **[I]** A 0x200-byte header block
is the natural reading and is the only block the `ptr_valid` probes cover.

#### D. Payload and trailer

| range (image offsets) | content |
|---|---|
| `0x200 .. size-0x230` | QNX IFS payload (hashed by the ECDSA path) |
| `size-0x230 .. size` | 0x230-byte signature trailer (§3.2) |
| `size-4 .. size` | the CRC-32 trailer word |

### 1.3 ⚠ The one field whose media offset is not yet proven: `os_type`

`71` §2 and `85` §0 both say "`region[0xd4]`", and the live dump was reported as
"byte **+0xd4 = 0**". Two readings are possible:

* **[I] `desc+0xD4` = header `+0x100`** — this is what the code literally does
  (`0x80014622` makes `desc = *(global+0x28)`, and every `0xD4` access is
  `desc`-relative). Under the live header, `desc = hdr+0x2C`, so the byte is at
  **`hdN+0x100000+0x100` = `hdN+0x100100`**. The earlier "+0xd4" note was then
  measured at the wrong base.
* `desc == hdr` (i.e. `*(hdr+0x28) == global`, so the metrics pointer is
  self-referential) — then the byte is `hdr+0xD4`.

**Discriminator (cheap, one 0x200-byte read):** the magic `0xC3D0E193` sits at
`desc+0x9C`. If `u32(hdr+0xC8) == 0xC3D0E193` then `desc = hdr+0x2C` and the
byte is `hdr+0x100`; if `u32(hdr+0x9C) == 0xC3D0E193` then `desc = hdr` and the
byte is `hdr+0xD4`. `89-crc_tool.py probe` performs this test automatically and
refuses to guess.

---

## 2. How the image end / total size is determined

### 2.1 What `end` is for each path **[P]**

Both verifiers get `start` and `end` from the same two indirections:

```
0x800147A4  ldr r0,[r2,#8]   ; desc->load_base_ptr
0x800147B8  ldr r5,[r0]      ; start = *(desc+0x08)
0x800147BC  ldr r0,[r2,#0xc] ; desc->load_end_ptr
0x800147BE  ldr r2,[r0]      ; end   = *(desc+0x0C)
```
(`0x80014716`'s ECDSA wrapper does the identical thing at `0x8001473C`–`0x80014742`.)

* `start` = `hdr+0x04` = `LoadStart` = `0x800FF800` = the **first byte of the IFS
  header**.
* `end` = `hdr+0x08` = `LoadEnd` = `0x803BF610` = the **first byte past the
  image** (a RAM address).
* **image size = `end - start` = `0x2BFE10` = 2,882,064 bytes.**
* media mapping: image offset `X` ↔ RAM `0x800FF800 + X` ↔ region media offset
  `0x100000 + X`.

  | quantity | image offset | media offset in the region |
  |---|---|---|
  | header start / CRC start (`start`) | `0x0` | `0x100000` |
  | payload start | `0x200` | `0x100200` |
  | ECDSA trailer start (`end-0x230`) | `0x2BF9E0` | `0x3BF9E0` |
  | `0xB5A60BFD` cookie (`end-0x178`) | `0x2BFC98` | `0x3BFC98` |
  | ECDSA `(r,s)` (`end-0x208`) | `0x2BFC08` | `0x3BFC08` |
  | `0xD7C82D1F` cookie (`end-8`) | `0x2BFE08` | `0x3BFE08` |
  | CRC trailer (`end-4`) | `0x2BFE0C` | `0x3BFE0C` |
  | image end (`end`) | `0x2BFE10` | `0x3BFE10` |

  (media offset = `0x100000` + image offset; the image ends well inside the IFS
  zone, which runs to `0xC00000`.)

* CRC path (`0x80014788`) **[P]**:
  ```
  800147D8  ldr r7,[r2,#-0x4]      ; stored = u32(end-4)
  800147DC  subs r4,r2,r5          ; len = end-start
  800147DE  adds r0,r7,#1 ; beq    ; stored == 0xFFFFFFFF -> status 7, fail
  800147EE  blx 0x8001a7b4         ; crc = 0xFFFFFFFF
  800147F2  subs r4,r4,#4          ; len -= 4
  800147F6  movw r6,#0x2710        ; 10000-byte chunks
  80014802  blx 0x8001a7c0         ; crc = update(crc, p, 0x2710)
  80014820  cmp r4,r6 ; bhi loop
  8001482A  blx 0x8001a7c0         ; final partial chunk (0..0x2710)
  8001482E  blx 0x8001a7c8         ; crc ^= 0xFFFFFFFF
  80014838  cmp r4,r7 ; beq ok     ; equal -> verified (1)
  8001483C  movs r1,#5 ; str [r8]  ; else status 5
  ```
  → **CRC over `[start, end-4)`, compared with `u32` at `end-4`.**

* ECDSA path (`0x80014716` → `0x80018688`, ARM) **[P]**:
  ```
  800186D0  sub r4,r1,#0x230       ; r4 = end-0x230 = trailer base
  800186DC  blx 0x80013fdc         ; ptr_valid(trailer)
  800186EC  ldr r1,[r4,#0xb8]      ; *(end-0x178)
  800186F0  ldr r2,[pc,#0xd4]      ; 0xB5A60BFD
  800186FC  ldr r1,[r4,#0x20]      ; *(end-0x210) keyID  (must be !=0, !=-1)
  8001870C  sub r6,r4,r5           ; hash length = end-0x230-start
  80018744  bl 0x80018420          ; SHA-512 over [start, end-0x230) in 0x80 or 0x4B00 chunks
  8001879C  add r0,r4,#0x28        ; sig block = end-0x230+0x28 = end-0x208
  800187A0  bl 0x8001b194          ; ECDSA (secp521r1) verify
  ```
  → **SHA-512 over `[start, end-0x230)`; `0x230`-byte trailer at `end-0x230`.**

### 2.2 Resolving "IFS length" vs "region length" vs `0x20000000 END`

| phrase | what it really is | consumed by the boot stage? |
|---|---|---|
| **"IFS length"** (`85` Phase 0) | `LoadEnd - LoadStart` from `hdr+0x08`/`hdr+0x04`. There is no length field for it. | ✔ yes — it *is* `end` |
| `desc+0x04` "length" (`0x1CC`) | size of the metrics/descriptor block only (0x1CC = 460 B, ending at `hdr+0x1F8`) | ✔ only as a window-bound probe |
| **"region length"** | the A/B slot extent. `winchester.py` says MBR `0x0`, SIG `0xF0000`, IFS `0x100000`, RFS `0xC00000`, END `0x20000000` — so the IFS zone is `0x100000..0xC00000` (11 MiB) | ✘ never. The stage reads exactly `[0x100000, 0x100000+size)`; it never touches `0xC00000`, and `0x20000000` is only deckard's `ZONE_RFS` upper sentinel (`winchester.py:32`) |
| `0x20000000 END` | deckard's zone boundary, not a hardware boundary | ✘ |
| the last 4 bytes of the *zone* | irrelevant. Only `end-4`, where `end` = `LoadEnd`, matters | — |

A practical consequence: the image (`0x2BFE10` = 2.75 MiB) leaves ~8 MiB of the
IFS zone unused, so there is room to grow, **but growing the image requires
bumping `LoadEnd` at `hdr+0x08`**, because that is the only thing that defines
the end. There is no other size field to keep in sync. **[I]**

---

## 3. Trailers and the field the CRC gate depends on

### 3.1 CRC gate `0x80013FB6` **[P]**

```
80013fb6  push {r4,lr}
80013fb8  mov r4,r0                      ; r0 = desc
80013fba  bl 0x80013fdc                  ; ptr_valid(desc)
80013fc0  ldr.w r0,[r4,#0xd4]
80013fc4  uxtb r0,r0
80013fc6  cmp r0,#6
80013fc8  bne -> return 0
80013fca  movs r0,#1
```
Exact equality with 6 — not a range, not a mask. Combined with the `bb10mt`
`STP_PROTOCOL_TYPE_*` table (`2/8/16/32/64` = SFI/RMA/MFI/RFA/UNTRUSTED), index
6 = `1<<6` = 64 = **UNTRUSTED_OS**. The CRC path is the unsigned/untrusted-OS
factory path. **[I for the name]**

### 3.2 The 0x230-byte trailer (proven shape) **[P]**

Cross-confirmed by `bb10mt` `TSigsBlockv2` + `GenDummySig` (`uconsts.pas:394`),
which builds exactly this block:

| trailer off | size | field | note |
|---|---|---|---|
| `0x000` | 32 B | `ECC_512_keyname` | |
| `0x020` | u32 | `ECC_512_keyID` | checked `!= 0`, `!= 0xFFFFFFFF` at `end-0x210` |
| `0x024` | u32 | `ECC_512_Len` = `0x88` (136) | |
| `0x028` | 136 B | `ECC_512_Sig` — P-521 `(r‖s)`, 66+66 B | **`end-0x208`**, passed to `0x8001B194` |
| `0x0B0` | u32 | `BlockSize` = `0xBC` | |
| `0x0B4` | u32 | `Ver` = `0x00010001` | |
| `0x0B8` | u32 | **`0xB5A60BFD`** (ECC-512 cookie) | checked at `end-0x178` |
| `0x0BC` | 0xBC | ECC-256 block, cookie `0xC6B71C0E` at `0x174` | unused by the stage |
| `0x178` | 0xB4 | RSA block, cookie `0xD7C82D1F` (`OS_SIGNATURE_COOKIE`) at `0x228` | `end-8` |
| `0x22C` | 4 B | **spare / CRC slot** | `end-4` = the CRC-32 trailer |

`0x178 + 0xB4 = 0x22C`, so the three signature blocks fill 556 of the 560
trailer bytes and the **last 4 bytes are the CRC slot**. This is why the CRC
trailer and the ECDSA trailer overlap exactly at `end-4`. **[P]**

### 3.3 The `(value & 0xff00ffff)` compare — model ID, not anti-rollback **[P]**

`0x800141E8`–`0x80014250`:

```
800141e8  ldr r0,[pc,#0x208]   ; 0x800143F4 -> relocates to 0x8001D040
800141f2  ldr r7,[r0,#0xc]     ; r7 = *(0x8001D04C) = bootrom metrics modelID
800141f8  movw r0,#0x205a ; bl 0x8000a9d8   ; read NVRAM 0x205A (8 bytes)
80014204  ldr.w r0,[sb,#0xd4]  ; desc->os_type
80014208  uxtb r0 ; cmp r0,#0x1f ; bhi default
80014210  lsl.w r2,fp,r0       ; 1 << os_type
80014214  tst r1,r2 ; beq default
80014218  ldr r7,[sp,#4]       ; override: second word of 0x205A
...  0x8001569c(0xb, ...) scan: if rec[2]==0x11 && rec[3]==1 -> r7 ^= 0x04000000
80014240  ldr.w r0,[sb,#0x7c]  ; desc->hardware_id
80014244  bic r1,r7,#0xff0000
80014248  bic r0,r0,#0xff0000
8001424c  cmp r1,r0 ; beq continue ; else return 0
```

`0x8001D040` is the bootrom metrics block (`bb10mt` `TBRMetrics`):
`+0x00 ver=0x00060018`, `+0x04 len=0x2DC`, `+0x08 BR_ver=0x051B0014`,
**`+0x0C modelID=0x06001A06`**, `+0x10 "RIM BlackBerry Device"`. So
`r7 = modelID` and the compare is

```
(desc->hardware_id & 0xFF00FFFF) == (modelID & 0xFF00FFFF)
```

with bits 16–23 ignored (the middle byte). The NVRAM `0x205A` path only lets a
per-OS-type model ID be substituted (and one bit flipped) — it is not a version
check and there is **no rollback counter anywhere in this compare**.

**Consequence for the recipe:** `desc+0x7C` (header `+0xA8`) must be left at the
value the shipping image already has (`0x06??0?06`); a payload edit must not
touch it.

### 3.4 What the framework does with a successful CRC **[P]**

`0x80014254`–`0x800142A4` (driver) then `0x80014296` → `0x80013F64`:

```
0x80013F64  r5 = *(desc+0x80)  (os_version); bitmask = 1 << desc[0xD4]
            if desc->version < 0x0003000C: bitmask = 0xFFFFFFFF
            0x80013F34(os_version, bitmask, table@0x8001D010)   ; in-image list
            0x80013F34(os_version, bitmask, NVRAM 0x2819 buffer) ; 0x78-byte list
            returns 0 (-> region FAILS) if EITHER list matches
```

`0x80013F34` walks `{u32 rangeStart, u32 rangeEnd, u32 osTypeBitMask}` entries
(`bb10mt` `TOSRangeAndType`) and matches when
`rangeStart <= value <= rangeEnd && (osTypeBitMask & bitmask)`. The tables are
therefore **blacklists** ("this version may not run as this OS type"). The
in-image table lives at `0x8001D010` (+0x9000 of the literal `0x80014010`) and
is empty in this build (`entry[1] == 0` terminates the walk). The only live
blacklist is NVRAM `0x2819`; if that record is absent the buffer is zeroed and
the check passes. **[P]**

Then `0x80014504`/`0x80014508` write `0x2074` back — **not reached on the
factory path**, because `0x800142BC`–`0x800142C8` returns before it.

### 3.5 The factory path is single-region and consumes budget **[P]**

```
800142b0  bl 0x8000911a            ; BOOTCTR_FCTRY selector
800142ba  cbz r0,0x800142d0        ; 0 (absent/product) -> normal 2-region path
800142bc  add r1,sp,#0xf0          ; factory path:
800142be  str.w sl,[r7,#8]         ;   region index := 0
800142c6  bl 0x80014148            ;   verify region 0 ONCE
800142ca  add sp,#0xfc ; pop       ;   return  (no 0x2074, no region 1)
```

`0x8000911A` → `0x800090CE` **[P]**: reads RPMB record `"BOOTCTR_FCTRY"`
(`0x80011690`), requires length 4; returns **1** if `1 <= value <= 0x32`,
**2** if `value == 0 || value > 0x32`, **0** if the record is absent.

And on a successful factory verify (`0x80014A3E`–`0x80014A4A`) **[P]**:
`bl 0x8000911A` → non-zero → `*(0x80026150) = 1`; `bl 0x8000911E` →
read record, `value-1`, write back via `0x800115E6`. **One factory boot = one
budget unit.** If the RPMB write fails it logs and continues booting.

---

## 4. CRC-32 algorithm proof

### 4.1 The three primitives **[P]**

```
8001a7b4  0000e0e3  mvn r0,#0                 ; init   = 0xFFFFFFFF
8001a7b8  1eff2fe1  bx lr
8001a7c0  eae3ffea  b #0x80013770             ; update -> table-driven loop
8001a7c8  0010e0e3  mvn r1,#0
8001a7cc  010020e0  eor r0,r0,r1              ; final  = crc ^ 0xFFFFFFFF
8001a7d0  1eff2fe1  bx lr
```

`0x80013770` (ARM, table driven, `r4` = table at `0x8004CD3C`):

```
8001377c  ldrb r5,[r1],#1
80013780  and  r3,r0,#0xff
80013788  eor  r3,r3,r5
8001378c  ldr  r3,[r4,r3,lsl #2]
80013790  eor  r0,r3,r0,lsr #8
```
= `crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8)` → **reflected** form.

`0x80013ED4` builds that table at runtime (`0x8004CD3C` is `0xFF` in the file —
it is RAM, not image data):

```
80013edc  movs r1,#1 ; bl 0x80013f0c        ; bitrev8(i)
80013ee4  lsls r0,r0,#0x18                  ; i << 24 in the top byte
80013ee8  ..8 x: if bit31: r0 = (r0<<1) ^ 0x04C11DB7 else r0 <<= 1
80013efa  movs r1,#4 ; bl 0x80013f0c        ; bitrev32()
80013f00  str.w r0,[r6,r4,lsl #2]
```
with `0x04C11DB7` at pool `0x80013F2C`.

### 4.2 Equivalence **[P — executed]**

`89-crc_tool.py selftest`:

```
0x80013ED4 table == reflected CRC-32 table : True
table[0]=00000000 table[1]=77073096 table[0xFF]=2D02EF8D     (= the standard table)
check vector '123456789' : 0xCBF43926 (expect 0xCBF43926) OK
empty string             : 0x00000000 (expect 0x00000000)
table==bitwise           : True
table==zlib.crc32        : True
chunked(0x2710)==zlib    : True
synthetic image round trip: unsealed rejected / resealed accepted / 1-bit flip rejected
```

So the trailer word is exactly `zlib.crc32(image[0:size-4])` =
`CRC-32/ISO-HDLC` (init `0xFFFFFFFF`, reflected poly `0xEDB88320`, final XOR),
which is also `binascii.crc32`. Chunking at 0x2710 (as the bootloader does) does
not change the result.

### 4.2b What could be validated, and what could not

**No raw `hd1`/`hd2` image exists anywhere in the workspace** (searched: the
largest non-archive files are the two CFP binaries and the SWPU231AP datasheet;
`playbook-dev/dumped4869ifs/` is the *extracted* QNX tree, 5.6 MB of files, no
header). Validation of the algorithm is therefore:

| check | result |
|---|---|
| `0x80013ED4` table generator emulated bit-for-bit == standard reflected CRC-32 table | ✔ [P] |
| standard check vector `"123456789"` → `0xCBF43926` | ✔ |
| table-driven == bit-by-bit (no table) == `zlib.crc32` on 1.3 MB of random/patterned data | ✔ |
| chunking at exactly 0x2710 gives the same digest as a single pass (crosses many chunk boundaries) | ✔ |
| synthetic 0x30430-byte image: unsealed rejected, resealed accepted, 1-bit flip rejected | ✔ |
| the 0x230 trailer layout independently reproduced by `bb10mt` `GenDummySig` (`uconsts.pas:394`), which writes `+0x24=0x88`, `+0xB0=0xBC`, `+0xB4=0x00010001`, `+0xB8=0xB5A60BFD`, `+0xE0=0x88`, `+0x16C=0xBC`, `+0x170=0x00010001`, `+0x174=0xC6B71C0E`, `+0x180=0x80`, `+0x220=0xB4`, `+0x224=0x00010001`, `+0x228=0xD7C82D1F`, total 560 = 0x230` | ✔ — every one of the stage's trailer offsets (`end-0x178`, `end-0x210`, `end-8`) is a field of that block |
| a real captured image | ✘ — **not possible in this workspace**; the Phase-0 numerator (§7) plus `verify` on the real `hd1` image is the final validation |

### 4.3 What the CRC covers **[P]**

`start` = `LoadStart` = the address of the **first header byte**. Therefore the
CRC covers `hdr[0x000 .. 0x1FF]` (branch, `TLoadPtr`, `TOSMetrics`, the
`0xC3D0E193` magic, **the `+0xD4` OS-type byte**, the whole payload) up to
`end-4`. The last 4 bytes (which live inside the ECDSA trailer's spare slot) are
excluded because they *are* the CRC.

---

## 5. Host tool: `analysis/89-crc_tool.py`

Pure Python 3, no dependencies (stdlib only). Read-only on device; it only reads
and writes host files.

```
selftest                                   # algorithm proof + round trip
table                                      # reproduce the 0x80013ED4 table
crc FILE [--offset N] [--length N]         # CRC-32/ISO-HDLC of a slice
probe FILE [--image-off N] [--desc-off N]  # parse + print the IFS header block
verify FILE [--image-off N] [--size N]     # full 0x80014610/0x80014788 model
reseal IN OUT [--image-off N] [--size N] [--type 6] [--patch OFF:HEX ...]
```

* `--image-off` auto-detects: `0x100000` when the file is a raw region dump
  (≥ `0x100200` bytes), `0` when the file *is* the image.
* `probe` auto-detects the descriptor base (`MetricsPtr-relative` vs `hdr+0`) by
  looking for `0xC3D0E193` at `desc+0x9C`, so it settles the §1.3 ambiguity from
  the data instead of from an assumption.
* `verify` mirrors the stage step for step and prints `PASS`/`FAIL` per check,
  in the stage's order: `LoadStart == 0x800FF800`;
  `desc->load_base_ptr == LoadStart+4`; `desc->load_end_ptr == LoadStart+8`;
  `MetricsPtr == LoadStart+desc_off`; `u32(end-8)==0xD7C82D1F`;
  `u32(end-4)!=0xFFFFFFFF`; desc magic `0xC3D0E193`; `version>=0x20000`;
  `desc[0xD4]==6`; `crc32[0,size-4) == u32(size-4)`; plus the ECDSA trailer shape
  (`end-0x178`, `end-0x210`, `end-8`) and the SHA-512 digest, informational.
  Exit 0 = verified, 1 = failed, 2 = parse/usage error.
* `reseal` never edits in place: it copies, applies `--patch` edits, sets the
  OS-type byte, then writes the CRC **last**, then re-verifies.

Quick start:

```bash
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
python3 89-crc_tool.py selftest
python3 89-crc_tool.py probe  region0.bin            # raw /dev/hd1 dump
python3 89-crc_tool.py verify region0.bin
python3 89-crc_tool.py reseal region0.bin region0.sealed.bin
python3 89-crc_tool.py verify region0.sealed.bin
```

---

## 6. The recipe

### 6.0 Two corrections the operator must accept first

1. **Only region 0 (`/dev/hd1`) is verified on the factory path** (§3.5). The
   "keep a CRC-valid original in region 0 as a fallback while the modified image
   sits in region 1" plan does not work: region 1 is never read once
   `BOOTCTR_FCTRY != 0`.
2. **There is no A/B fallback at all on that path.** If region 0 fails, the
   stage spins in the retry loop `0x80014A1E` → `0x80014A76` (which keeps polling
   the host command channel `0x8001217E`), i.e. the unit does not boot the OS but
   is not necessarily dead (bootrom/loader recovery stays available). Treat every
   `BOOTCTR_FCTRY` write as destructive.

### 6.1 Order of operations (proven sequence)

Because the CRC covers the header, the order is mandatory:

```
(1) copy/backup      (5) reseal (CRC last)
(2) patch payload    (6) host verify
(3) patch desc[0xD4] (7) write to media, read back, verify again
(4) NOT the CRC      (8) only then arm BOOTCTR_FCTRY
```

### 6.2 Step-by-step, host side

Assume the Phase-0 dumps are `region0.bin` (= `dd if=/dev/hd1`) and
`region1.bin`. `SIZE` = `LoadEnd - LoadStart` from `probe` (expected
`0x2BFE10`). All arithmetic below is done on the extracted image, then written
back at media offset `0x100000`.

**a. Take the active IFS**

```bash
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
T=89-crc_tool.py
# region0.bin is a full read-only image of /dev/hd1 (Phase 0)
python3 $T probe region0.bin            # -> note SIZE, desc_off, os_type
# extract the bare image (all offsets below are image-relative)
python3 - <<'EOF'
import struct
d = open('region0.bin','rb').read()
h = d[0x100000:0x100200]
size = (struct.unpack_from('<I',h,0x08)[0] - struct.unpack_from('<I',h,0x04)[0]) & 0xFFFFFFFF
open('ifs0.img','wb').write(d[0x100000:0x100000+size])
print('ifs0.img = 0x%X bytes (from hdr+0x08 - hdr+0x04)' % size)
EOF
sha256sum region0.bin ifs0.img | tee backup-$(date +%s).sha256
```

**b. Apply the payload change, then set `desc[0xD4] = 6`**

Do **not** hand-edit the file. Use `reseal`, which applies the byte change and
then recomputes the CRC — in that order:

```bash
# no payload change at all -- the correct FIRST test (§6.5)
python3 $T reseal ifs0.img ifs0.sealed.img --type 6

# with a payload change, e.g. a 4-byte patch at image offset 0x1234:
python3 $T reseal ifs0.img ifs0.sealed.img --type 6 --patch 0x1234:deadbeef
```

`reseal` prints the exact offset it touched, e.g.

```
  os_type: image offset 0x100 0 -> 6          <- or 0xD4, see §1.3
  CRC trailer at image offset 0x2BFE0C = 0x162CA580
```

**c. Recompute and write the CRC trailer** — done by `reseal`. Manually the
sequence is:

```
crc  = zlib.crc32( image[0 : SIZE-4] )            # init 0xFFFFFFFF, poly 0xEDB88320, final XOR
assert crc != 0xFFFFFFFF                          # else the stage reports status 7
write_le32( image, SIZE-4, crc )
```

**d. Verify — host and bootloader logic**

```bash
python3 $T verify ifs0.sealed.img
# every line must be PASS and the last line "RESULT: VERIFIED (CRC path)";
# exit status 0
```

The tool's checks are the bootloader's checks, in the bootloader's order
(`0x80014610` structure → `0x80013FB6` gate → `0x80014788` CRC). Any `FAIL` means
the unit would take the corresponding status (`7` = trailer untouched,
`5` = CRC mismatch) and never reach `0x80014846`.

**e. Write it back to media (main session only, after a verified backup)**

```bash
# host side: rebuild a region image with the sealed IFS in place
python3 - <<'EOF'
d=bytearray(open('region0.bin','rb').read())
img=open('ifs0.sealed.img','rb').read()
d[0x100000:0x100000+len(img)] = img
open('region0.new.bin','wb').write(d)
EOF
python3 $T verify region0.new.bin            # image-off auto-detects 0x100000
```

Then on the unit: backup, write `region0.new.bin` to `/dev/hd1` at offset
`0x100000` (length = `SIZE`), read it back, and `verify` the readback. Do not
write anything outside `[0x100000, 0x100000+SIZE)`.

**f. The two images the plan asks for, reinterpreted**

| plan item | what it actually has to be |
|---|---|
| "modified image (region 1)" | **modified image in region 0 (`hd1`)** — the only region the factory path verifies |
| "CRC-valid original fallback (region 0)" | **the same slot, sealed once with no payload change** — used as the *first* factory boot (§6.5). It cannot coexist with the modified image; pick one per boot cycle |
| "both regions CRC-valid before `BOOTCTR_FCTRY`" | not required and not sufficient. Required: **region 0 CRC-valid with `desc[0xD4]==6`**. Making region 1 CRC-valid too is harmless and keeps a route back if the counter ever returns to 0 (then `0x2074` and the ECDSA path come back into play) |

**g. Backup requirements (non-negotiable)**

* Full read-only images of `/dev/hd1` and `/dev/hd2` (Phase 0), SHA-256'd, stored
  off-device, plus the original `ifs0.img`.
* The original 0x230 trailer bytes (`end-0x230 .. end`) saved separately — they
  are the only copy of the product ECDSA signature once you reseal.
* NVRAM `0x2074`, `0x2801`, `0x2802`, `0x2819`, `0x205A`, `0x2040`, `0x2004`
  read-only, saved.
* A written rollback note: "region 0 now carries a resealed image with
  `desc[0xD4]=6`; the ECDSA signature is invalid; the product path will fail
  until the original `ifs0.img` is restored."

### 6.3 Why "both regions CRC-valid" is still worth doing anyway

If `BOOTCTR_FCTRY` ever goes back to 0 (expiry) or the record is lost, the
product path returns: `0x800142D0` reads `0x2074` and loops over both regions
with the ECDSA verifier. A `desc[0xD4]=6` image fails the *gate* on the product
path only in the sense that the gate is not consulted — the ECDSA check runs
regardless and fails. So a resealed region 0 is **unbootable on the product
path**. Keep `ifs0.img` (original, signed) somewhere you can restore it from,
and prefer to leave region 1 (`hd2`) untouched so that `switch()` still has a
target.

### 6.4 Failure modes and what they mean

| symptom (host `verify` or boot) | cause |
|---|---|
| `u32(end-4) == 0xFFFFFFFF` → status 7 | the CRC was never written (or `end` is wrong) |
| CRC mismatch → status 5 | CRC computed before the `+0xD4` patch, or `SIZE` wrong, or the loader patches bytes inside `[0, end-4)` |
| gate fail (no status written) | `desc[0xD4] != 6`; wrong descriptor base (§1.3) |
| `0x80014610` fails immediately | `u32(end-8) != 0xD7C82D1F`, `desc` magic wrong, or `version < 0x20000` — i.e. `end`/`desc` are mis-derived |
| CRC ok but the region still fails | `0x80013F64` blacklist (NVRAM `0x2819`) or the `modelID` compare at `0x80014240` |
| boots then dies | payload/entry problem, not the verifier — the verifier only checks the header + trailer |

### 6.5 Recommended first test (the safe one)

1. Phase 0 dumps + backup.
2. `reseal` the **unmodified** active image with `--type 6`. Verify.
3. Write it to `hd1`, read back, verify the readback.
4. Write `BOOTCTR_FCTRY = 50` from the rooted 2.0.0.4869 OS (`/dev/rpmb`).
5. Reboot. If the CRC path is right, **the same OS boots** — and the boot also
   proves that media bytes == RAM bytes (no load-time patching inside the CRC
   range). It also decrements the counter by 1.
6. Only then swap in a modified payload, reseal, write, reboot.

This costs 2 of the ≤50 budget units and never puts an unproven payload behind
the (fallback-free) factory path.

---

## 7. Phase 0 — read-only dump/parse procedure for the live unit

Run from the rooted 2.0.0.4869 OS over SSH. **Read-only**: nothing below writes
to the device. Save everything under
`bootdumps-2026-09-11/analysis/playbook-live-<date>/regions/`.

```bash
D=/accounts/devuser/regions; mkdir -p $D; cd $D
# 0. identity of the run
uname -a > meta.txt; date >> meta.txt
df -k /dev/hd1 /dev/hd2 >> meta.txt 2>&1 || true
sha256sum /dev/hd1 /dev/hd2 >/dev/null 2>&1 || true   # optional: 2x512MB, slow

# 1. MBR zone head (0x0) and SIG/fingerprint (0xF0000) for BOTH regions
for r in hd1 hd2; do
  dd if=/dev/$r bs=4096 count=1                 of=$r-0x00000000.bin 2>/dev/null
  dd if=/dev/$r bs=4096 skip=$((0xF0000/4096)) count=1 of=$r-0x000F0000.bin 2>/dev/null
done

# 2. IFS header block: 0x200 bytes at 0x100000, BOTH regions
for r in hd1 hd2; do
  dd if=/dev/$r bs=512 skip=$((0x100000/512)) count=1 of=$r-hdr0x200.bin 2>/dev/null
done

# 3. the first 1 MiB of the IFS zone (header + startup + TOC) for parsing
for r in hd1 hd2; do
  dd if=/dev/$r bs=65536 skip=16 count=16 of=$r-ifs-0x100000-1M.bin 2>/dev/null
done

# 4. RFS zone head (0xC00000) -- only to confirm the boundary, not verified
for r in hd1 hd2; do
  dd if=/dev/$r bs=4096 skip=$((0xC00000/4096)) count=1 of=$r-0x00C00000.bin 2>/dev/null
done
sha256sum *.bin > SHA256SUMS
```

Bring the files back, then:

```bash
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
T=89-crc_tool.py
R=playbook-live-<date>/regions
for f in $R/hd1-hdr0x200.bin $R/hd2-hdr0x200.bin; do echo "== $f"; python3 $T probe $f --image-off 0; done
```

Record, per region:

| field | where | what to record |
|---|---|---|
| `0xC3D0E193` location | `hdr+0xC8` vs `hdr+0x9C` | **settles §1.3** (`desc = hdr+0x2C` vs `desc = hdr`) |
| `LoadStart` / `LoadEnd` | `hdr+0x04` / `hdr+0x08` | → `SIZE`; confirm `0x800FF800` / `0x803BF610` |
| `MetricsPtr` | `hdr+0x28` | expect `LoadStart + 0x2C` |
| `version` / `length` | `desc+0x00` / `desc+0x04` | expect `0x00030012` / `0x1CC` |
| `hardware_id` / `os_version` | `desc+0x7C` / `desc+0x80` | must not change in a reseal |
| `os_type` | `desc+0xD4` | expect `0` today; must become `6` |
| tail | `end-8`, `end-4` | expect `0xD7C82D1F`, `0xFFFFFFFF` (unsealed) |
| ECDSA trailer | `end-0x178`, `end-0x210` | expect `0xB5A60BFD`, `0x00000088` |

Then the **second pass** (needs `SIZE` from above) — the trailer and a whole-image
CRC baseline:

```bash
# trailer (0x230 bytes) and the last 4 bytes, for BOTH regions
for r in hd1 hd2; do
  START=$((0x100000 + SIZE - 0x230))
  dd if=/dev/$r bs=1 skip=$START count=$((0x230)) of=$r-trailer.bin 2>/dev/null
done
# the full image span, for a host-side CRC/ECDSA baseline (≈2.75 MiB per region)
for r in hd1 hd2; do
  dd if=/dev/$r bs=1M iflag=skip_bytes,count_bytes skip=$((0x100000)) count=$((SIZE)) \
     of=$r-image.bin 2>/dev/null
done
sha256sum *-image.bin *-trailer.bin
```

Host-side baseline (this is what makes step 5 of §6.5 predictable):

```bash
python3 89-crc_tool.py crc   $R/hd1-image.bin --offset 0 --length $((SIZE-4))
python3 89-crc_tool.py verify $R/hd1-image.bin --image-off 0
# expect: structure PASS, gate FAIL (os_type=0), CRC "not sealed" (stored==0xFFFFFFFF)
# and the printed sha512[0, SIZE-0x230) as the ECDSA digest baseline
```

Also capture read-only NVRAM for the cross-checks in §3.3/§3.4 (the editor is
already installed at `/accounts/devuser/nvram`):

```bash
./nvram read 0x205A hex   # per-OS-type model-ID override table (8 bytes)
./nvram read 0x2819 hex   # OS-type blacklist (0x78 bytes) -- expect absent/zero
./nvram read 0x2074 hex   # A/B state (not read on the factory path, keep for the record)
./nvram read 0x2801 hex   # gate: +0x18 < +0x1c selects the retry loop
```

**Do not** read `/dev/rpmb` for this phase (the `0xDE` loader read masks
`0xFFFFFFFF`; see `72` §3.1) and **do not** write anything.

---

## 8. Risks

| # | risk | severity | mitigation |
|---|---|---|---|
| R1 | **No A/B fallback on the factory path** (§3.5). A bad region-0 image is an unbootable OS, recovered only via bootrom/loader. | high | §6.5 staged test; keep the original `ifs0.img` + full region dump |
| R2 | **Every factory boot decrements `BOOTCTR_FCTRY`** (§3.5). 50 boots max, and a value of 0 sends `0x80014904` to the nuke path. | high | budget the campaign; re-arm via `/dev/rpmb` only from a booted OS |
| R3 | **`desc[0xD4]`'s media offset is still ambiguous** (§1.3). Patching `hdr+0xD4` if the byte is really at `hdr+0x100` silently does nothing (CRC still verifies, gate still fails). | medium | `probe` settles it before any write; never hand-pick the offset |
| R4 | **The loader may patch the image at load time**, which would invalidate a media-computed CRC. | medium | the `0x04`/`0x08` cells already contain absolute RAM addresses, so no patch should be needed; §6.5 step 5 proves it empirically |
| R5 | Resealing **destroys the product ECDSA signature**, so the region becomes unbootable on the product path. | medium | keep `ifs0.img` and the original 0x230 trailer; document the rollback |
| R6 | The `modelID` compare (`0x80014240`) and the NVRAM `0x2819` blacklist can reject an otherwise-valid CRC image. | low | do not touch `desc+0x7C`; read `0x2819` in Phase 0 |
| R7 | The `0x2074` record is not read on the factory path, so `requested_region`/`actual_region` are not updated — the OS will think it is in the region `actual` says. | low | keep `requested == actual == 0`; that matches a region-0 image |
| R8 | Writing at a wrong offset (e.g. `0x100000+0x200` instead of `0x100000`) corrupts the header. | high | write only `[0x100000, 0x100000+SIZE)`, always read back and `verify` the readback |

---

## 9. Residual unknowns

1. **`desc[0xD4]` media offset** — `hdr+0x100` (if `desc = hdr+0x2C`) or
   `hdr+0xD4` (if `desc = hdr`). Settled by the Phase-0 magic-location test. All
   other conclusions are independent of it.
2. **Does the CRC range see the media bytes verbatim?** The chunks are copied
   into RAM at `LoadStart` and the CRC is computed on the RAM copy. Nothing in
   `0x8001404A`/`0x80013FF6` patches the image, and the `LoadStart`/`LoadEnd`
   cells already hold absolute addresses, so a verbatim copy is expected — but it
   is not proven. §6.5 step 5 is the experiment.
3. **Is the trailer length fixed at 0x230?** Yes for this stage (`sub r4,r1,#0x230`,
   `0x800186D0`). Whether a future/bigger image could declare a different trailer
   size is not modelled anywhere — there is no size field for it.
4. **Is `desc+0x04` (`0x1CC`) trustworthy as a length?** It is only ever used as
   a pointer bound (`desc-4+length`, `desc+0x1CC`). Treat it as "the metrics block
   must fit the header", never as the image size.
5. **Fields `desc+0x84..0x9B`, `+0xA0..0xD3`, `+0xD5..0x1CB`** are referenced
   only through the magic/`os_type`/tail probes; their meaning is unknown. A
   payload edit inside them is CRC-legal but semantically unverified.
6. **What `os_type` values other than 6 do.** Only 6 opens the CRC gate; 2/3/4/5
   would take the ECDSA path (which we cannot satisfy). Bits 1/3/4/5 are
   SFI/RMA/MFI/RFA in `bb10mt`; index 6 (`UNTRUSTED_OS`) is the only one with a
   forgeable verifier.
7. **Whether the `0x205A` per-OS-type override is populated on this unit.** If it
   is, changing `os_type` from 0 to 6 also changes which word is used as the
   model-ID reference — the Phase-0 `0x205A` read covers this.
8. **`(0x800102B0)` (the window upper bound) is a runtime word.** What writes it
   and how large it is at verify time is not known from static analysis; it must
   be ≥ `end` + 4 for the checks to pass, and the product path already proves it
   is.
9. **The 8 bytes at `hdr+0x1F8..0x1FF`** are outside the declared metrics block
   but inside the 0x200 header block; their role (padding vs a real field) is
   unknown.

---

## 10. Reproduction commands

```bash
# --- disassembly (file offset == address offset, base 0x80000000) ------------
cd /home/psyden/rea-work/bootdumps-2026-09-11
python3 - <<'EOF'
import capstone
D=open('hd0head.bin','rb').read()
def dis(a0,a1,mode=capstone.CS_MODE_THUMB):
    md=capstone.Cs(capstone.CS_ARCH_ARM, mode)
    for i in md.disasm(D[a0-0x80000000:a1-0x80000000],a0):
        print("%08x  %-18s %s %s"%(i.address,i.bytes.hex(),i.mnemonic,i.op_str))
for a0,a1,m in [
    (0x80014610,0x80014720,capstone.CS_MODE_THUMB),   # structural checks
    (0x80013FDC,0x80013FF6,capstone.CS_MODE_THUMB),   # ptr_valid window
    (0x80014788,0x800148AC,capstone.CS_MODE_THUMB),   # CRC path
    (0x80014716,0x80014788,capstone.CS_MODE_THUMB),   # ECDSA wrapper
    (0x80018688,0x800187E0,capstone.CS_MODE_ARM),     # ECDSA core
    (0x80014148,0x800142A8,capstone.CS_MODE_THUMB),   # per-region verifier
    (0x800142A8,0x800143C0,capstone.CS_MODE_THUMB),   # driver: factory = region 0 only
    (0x80013FB6,0x80013FD2,capstone.CS_MODE_THUMB),   # desc[0xD4]==6 gate
    (0x80013F64,0x80013FB6,capstone.CS_MODE_THUMB),   # OS-type blacklist
    (0x80013ED4,0x80013F34,capstone.CS_MODE_THUMB),   # CRC table generator
    (0x8001A7B4,0x8001A7D4,capstone.CS_MODE_ARM),     # CRC init/update/final
    (0x80013770,0x80013820,capstone.CS_MODE_ARM),     # CRC update loop
    (0x800090CE,0x80009120,capstone.CS_MODE_THUMB),   # BOOTCTR_FCTRY selector
    (0x8000911E,0x80009146,capstone.CS_MODE_THUMB),   # BOOTCTR_FCTRY decrement
    (0x80014904,0x80014AB0,capstone.CS_MODE_THUMB),   # boot main / enter-OS
]:
    print("==== 0x%X - 0x%X (%s)"%(a0,a1,"ARM" if m==capstone.CS_MODE_ARM else "Thumb"))
    dis(a0,a1,m)
EOF

# --- literal pools used above ----------------------------------------------
python3 - <<'EOF'
import struct
D=open('hd0head.bin','rb').read()
for a in (0x800148F0,0x800148F4,0x800148F8,0x800143D8,0x800143DC,
          0x800143E0,0x800143E4,0x800143E8,0x800143EC,0x800143F0,0x800143F4,
          0x80013F2C,0x80013F30,0x80013FD4,0x80013FD8,0x80013814,0x800187CC):
    print(hex(a), hex(struct.unpack_from('<I',D,a-0x80000000)[0]))
# bootrom metrics block (pre-relocation 0x80014040 + 0x9000)
for a in range(0x8001D040,0x8001D050,4):
    print(hex(a), hex(struct.unpack_from('<I',D,a-0x80000000)[0]))
EOF

# --- CRC proof + tool self test --------------------------------------------
cd analysis
python3 89-crc_tool.py selftest
python3 89-crc_tool.py table | head
python3 89-crc_tool.py --help

# --- independent cross-check of the polynomial ------------------------------
python3 - <<'EOF'
POLY=0x04C11DB7
def bitrev(x,n):
    r=0
    for _ in range(n): r=((r<<1)|(x&1))&0xFFFFFFFF; x>>=1
    return r
tbl=[]
for i in range(256):
    r0=(bitrev(i,8)<<24)&0xFFFFFFFF
    for _ in range(8):
        r0=(((r0<<1)&0xFFFFFFFF)^POLY) if r0&0x80000000 else (r0<<1)&0xFFFFFFFF
    tbl.append(bitrev(r0,32))
std=[]
for i in range(256):
    c=i
    for _ in range(8): c=(c>>1)^0xEDB88320 if c&1 else c>>1
    std.append(c)
print("0x80013ED4 == reflected CRC-32 table:", tbl==std)
EOF
```

## 11. Artifacts

| path | content |
|---|---|
| `analysis/89-ifs-crc-recipe.md` | this report |
| `analysis/89-crc_tool.py` | CRC-32/ISO-HDLC builder + verifier + IFS header parser + resealer (stdlib only, `selftest` passes) |
| `hd0head.bin` | unmodified source (not touched by this track) |

## 12. Corrections this report makes to earlier documents

| earlier statement | where | correction |
|---|---|---|
| "IFS entry `0x803BF610`" | `85` §0, §4, §5; `87` track D | `0x803BF610` is **`LoadEnd` (image end)**, not an entry point. There is no entry field in the header; the stage branches to image `+0x800` = `0x80100000` via `0xEA0001FE`, and enters the OS from `0x80014A90` |
| "the 0x80014A90 enter-OS path jumps to `0x803BF610`" | `85` §5 | not supported by the code; `0x80014A90` only tears down and returns into the OS bootstrap, whose target is derived elsewhere |
| "the driver verifies the requested region first and falls back to the other region" | `85` §1 | true only for the **product** path (selector 0, NVRAM `0x2074`). With `BOOTCTR_FCTRY != 0` the driver verifies **region 0 once** and returns (`0x800142BC`–`0x800142C8`) |
| "anti-rollback compare `(value & 0xff00ffff)`" | `70` §4, `71` §5, `85` §2 | it is a **hardware model-ID match**: bootrom `modelID` (`0x8001D04C` = `0x06001A06`) vs `TOSMetrics.hardware_id` (`desc+0x7C`). Bits 16–23 are ignored |
| "`region[0xd4]&0xff==6`" with the live note "+0xd4 = 0" | `71` §2, `85` §0 | the byte is `desc+0xD4`, i.e. header `+0x100` if `desc = hdr+0x2C` (§1.3). Verify with `89-crc_tool.py probe` before patching |
| "region 1 (hd2) is free for staging" as the place for the modified image | `85` §0, Phase 2/3 | staging in `hd2` is still fine for the ECDSA product path, but **useless for the CRC path**, which never looks at `hd2` |
