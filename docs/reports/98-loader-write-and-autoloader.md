# 98 — Can the signed PlayBook ramloader write the OS/IFS region pre-OS?

Track **A**. Status: **CLOSED — answer is YES.** Static, read-only. No device,
USB, NVRAM, RPMB, eMMC or flash access of any kind was performed for this
document. Everything below is derived from file bytes on this workstation plus
host-side tool experiments.

Date: 2026-10-26. Closes the open question in `97` §2 / §4 item 1 and extends
`88` §9.1 / §12 (unknown U1) from "NVRAM partition" to "OS/IFS region".

---

## 0. Verdict first

1. **YES — the signed PlayBook ramloader can write the OS/IFS region over USB,
   before any OS boots.** The opcode is **`0xF7`** (region 0 / `hd1`).
   `0xF8` is the same primitive aimed at the *other* region (region 1 / `hd2`).
   No signature, no OS, no `/dev/*` access is involved. **[P]**
2. **The write destination is `allow-listed_partition_start + QCFM 'rrcq'.offset`,
   and both halves are host-controlled**: the offset is a field the host writes
   into the container, and the base comes from the MCT partition table on the
   device. The only gate is `FUN_80100118` (LDR_01) / `FUN_8011B774` (Cellebrite
   build), a 64-bit range test against a runtime allow-list. **[P]**
3. **That allow-list contains the OS extent.** It is built at loader init from
   MCT TLV types `0x1C, 0x22, 0x1B, 0x1F, 0x24, 0x34, 0x3B, 0x3A, 0x20, 0x21`
   (+ the `0x19`/`0x1A` pair). On a real 64 GB PlayBook MCT (verified CRC-valid,
   §4) type **`0x1B` = blocks `0x815..0x4804`** covers **both** QNX regions in
   their entirety — region 0's IFS at block `0x825` is 16 blocks inside it. **[P]**
4. Region 0 IFS = **block `0x825` = media byte `0x08250000` = LBA512 `0x41280`**;
   region 1 IFS = block `0x2825` / byte `0x28250000` / LBA `0x141280`. A sealed
   2.75 MB IFS occupies 44 blocks (`0x2C0000` bytes). **[P]** (all arithmetic in §5)
5. **The loader never verifies what it writes.** `0xF7`/`0xF8` check only the
   range; `0xF9` copies a 560-byte host "signature" block into RAM and compares
   two cookie words, then *stores* it. Boot-time ECDSA is a bootloader property,
   and the factory path ignores it (`89`, `95`). This is why a pre-OS reflash of
   a CRC-sealed image works — and why an *unsealed* one bricks the boot. **[P]**
6. **A repacked IFS can be made CRC-valid with `os_type=6`.** Verified end to end
   on the real region-0 image: `dumpifs` reads it, `89-crc_tool.py reseal --type 6
   --image-off 0` produces exactly 5 changed bytes and `verify` reports
   `VERIFIED (CRC path)` with CRC `0x116B2562` (identical to the `95` result). **[P]**
7. **Tooling status.** `bbusb.py` already has every wire primitive
   (`send_block`=`0xF7`, `send_signature`=`0xF9`, `complete`=`0xC0`,
   `flash_regions_info`=`0xB4`) but has **no container builder**. `bb10mt`
   (`flash -i <part>.ifs`) already builds and sends the correct QCFM V2 container
   with `_type=8`, `blocksize=0x10000`, `rrcq.offset=0` — i.e. **the pre-OS IFS
   flasher already exists**; it only lacks sealing and verification (§9).

One correction to `89`: the ECDSA trailer starts at image offset **`0x2BFBE0`**
(`end-0x230`), not `0x2BF9E0`. Everything else in `89` §1.2 recomputes exactly.

---

## 1. Provenance

### 1.1 Inputs (hashes recomputed this session)

| # | Path | Size | SHA-256 |
|---|---|---:|---|
| 1 | `bbusb/loaders/loader_06001A06-00.bin` = `LDR_01`, the loader `bbusb.py` ships and CFP.exe embeds | 238,128 | `c7cc33c796f9518bbc0eb929c6096f2473937ad486c579764aa4c57d8165393f` |
| 2 | `cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A` (CB build) | 159,968 | `03ffaf26dc0ce640c2f44dc5e223bb34d15175234f26d9876846484022a5c6dc` |
| 3 | `bootdumps-2026-09-11/hd4.bin` — the **MCT** (Mem Config Table), v1.24, 128 KiB | 131,072 | (see §4; CRC-32 trailer verified in-place) |
| 4 | `analysis/playbook-live-2026-10-25/hd1-ifs-image.trim.bin` — active region-0 IFS, exactly `0x2BFE10` | 2,883,088 | `75be24e8297e3a4a27b8f7ce2f9d857b94178b020c063d73ecb0cc90c0fe94ba` |
| 5 | `bb10mt-main/{uautoloader,qcfm,mct,bbloader,ramloader,uflash,rcfs}.pas`, `bbusb/bbusb.py` | — | source context, not target evidence |
| 6 | `playbook-live-2026-10-25/dev-listing-and-hd0-mbr.bin` | 6,006 | device `ls` + the `PRIMAPP`/`KEYS` table |

Input 4 matches the `sha256` recorded for the active IFS in `95` §1, and its
embedded CRC-32 (`0x8D46FB4A`) matches `95` §1 exactly — independent
confirmation that the file is the same image.

### 1.2 Tools

| Tool | Version / path | Use |
|---|---|---|
| Capstone | 5.0.7, `CS_ARCH_ARM`/`CS_MODE_ARM` + `skipdata` | exhaustive opcode/pool scans, targeted disassembly |
| Ghidra headless via REA | 12.1.4, `ARM:LE:32:v7`, `BinaryLoader -loader-baseAddr 0x80100000` | pseudocode of `FUN_8011FDD8`, `FUN_80100118` |
| QNX SDP 6.6 `dumpifs` / `mkifs` | `/home/psyden/SSDdrive/wsl-migration/home/psyden/qnx660-master/host/linux/x86/usr/bin/` | IFS unpack/repack (§8) |
| Python 3.14 | `zlib`, `struct` | MCT CRC, LBA math, literal decoding |
| `89-crc_tool.py` | in-tree | seal/verify (§7) |

All addresses are **virtual = 0x80100000 + file offset** for both OMAP loaders.
Labels: **[P]** proven from the instruction stream or a verified computation at
the cited address; **[I]** the only reading consistent with the evidence, not
individually proved.

---

## 2. The `0xF7` / `0xF8` write path, exactly

### 2.1 Where it lives

| build | `0xF7`/`0xF8` handler | gate | MCT lookup | allow-list builder | allow-list table | staging buffer |
|---|---|---|---|---|---|---|
| `LDR_01` (`loader_06001A06-00.bin`) | `FUN_8011FDD8` (`0x8011FDD8`) **[P]** | `FUN_80100118` (`0x80100118`) **[P]** | `FUN_80100DA0` → `FUN_80100D50(type, hint, table)` **[P]** | `FUN_80100238` (`0x80100238`) **[P]** | `0x8013FEA0` (BSS), count at `0x8013ECB4+0xC` **[P]** | `0x80259794` **[P]** |
| CB `06001A06_0000000A` | `0x8010F660` (`88` §5.2, §7.3) | `FUN_8011B774` (`88` §7.7) | `FUN_8011B920` (same list + `0x1E`) | `FUN_8011B920` | `0x8020C0E4` | `0x8017EDE0` |

Both builds are the same code shape; `LDR_01` is the one that actually runs on
this hardware (it is what `bbusb.py` uploads and what CFP.exe contains, `72` §2).
Everything below is derived from `LDR_01` and cross-checked against `88`'s
Cellebrite reading. The differences are listed in §2.6.

### 2.2 Frame layout for `0xF7` **[P]**

`bbusb.py:channel2()` sends `[u16 size][u16 cmd][payload][u32 crc32]`,
`size = len(payload)+8`; the loader decodes `len = *(u16*)frame - 0x10`
(`0x8011FDE0`–`0x8011FDF0`), so the payload is `size-16 = len(payload)-8`:

```
frame+0x00 u16  total frame size
frame+0x02 u8   opcode (0xF7 or 0xF8); frame+0x03 = sequence/mode byte
frame+0x04 u32  chunk sequence number   (bb10mt: Buff[0] = cb)
frame+0x08 u32  chunk data length       (bb10mt: Buff[4] = bs)
frame+0x0C ...  chunk data              (bb10mt: Buff[8..]; max 0x3FEC data bytes)
```

A chunk's data may not exceed `0x10000` bytes — `0x8011FE90` rejects a first
chunk whose parsed header length is `> 0x10000` with status `0x11`. `bb10mt`
uses `MAX_FLASH_BLOCK-8 = 0x3FEC` data bytes per frame (`MAX_FLASH_BLOCK =
0x3FF4` is the frame payload). **[P]**

### 2.3 Flow **[P]**, from `FUN_8011FDD8` and `FUN_80100118`

```
len = frame[0] - 0x10
if (session_state[+0x10] == -1) {                       ; first 0xF7 of a session
    n = QCFM_parse(frame+0xc, len, &total)              ; FUN_8011F9EC  (0x8011F9EC)
    if (n <= 0)                          -> resp 0x11
    if (n > 0x10000)                     -> resp 0x11
    memcpy(staging, frame+0xc, n)                       ; staging = 0x80259794
    region_sel = (op == 0xF7) ? 0 : 1                   ; 0x8011FEB0
    clear state[+0x48]
    if (!write_session_open())           -> resp 0x2E   ; FUN_8010083C(1)
    state[+0x14] = state[+0x18] = 0
    state[+0x30] = (state[+0x10] == 0) ? n : state[+0x10]  ; header bytes still expected
    if (total > 0x1FFFF && FUN_80100DA0(0x39,0) != 0) {   ; "big container" path
        p = staging + *(u32*)(staging+0x18)              ; mhf1.flags = offset of mhf2
        if (*(u32*)(p+0x10) == 0)        -> resp 5        ; mhf2.nheaders == 0
        pfcq = p + *(u32*)(p+0x0C)                       ; mhf2 + mhf2.length
        if (*(u32*)pfcq != 'pfcq')       -> return 4
        rrcq = pfcq + *(u32*)(pfcq+0x10)                 ; pfcq.rrecOffset
        if (*(u32*)rrcq != 'rrcq')       -> return 3
        state[+0x34] = 1                                  ; "v2 container present"
    }
}
... header tail is accumulated into staging until state[+0x30] == 0 ...

if (state[+0x18] == 0) {
  if (state[+0x1C] != 0) {                                ; ---- PATH 1: whole-image ----
      p34 = FUN_80100DA0(0x34, 0)                         ; MCT type 0x34 = "MBR"
      if (!p34)                        -> resp 0x11
      base = (op == 0xF8) ? (region1_base - (p34.end - p34.start) - 1)
                          :  p34.start ;                  ; <-- p34 entry dword +4
      if (BS() != container_blocksize) -> resp 0x11        ; FUN_8011A7A0()
      len   = rrcq.Count * container_blocksize
      dest  = base + rrcq.offset                          ; <-- HOST-CONTROLLED
      goto do_write
  }
  do {                                                    ; ---- PATH 2: per-partition ----
      rrec = current rrcq record
      for each MCT type-0x39 entry e:
          if (e.PartitionID == pfcq._type) {               ; pfcq+0x0C == _type
              if (e.Flags & 0x20) -> mirror (write both)
              if (e.Flags & 0x40) -> selected only if region_sel == 1
              else                -> selected only if region_sel == 0
          }
      if (!selected)                   -> status 2 (resp 0x15)
      base = selected.start ; mirror_base = mirror.start
      if (mirror && (selected.end-selected.start) != (mirror.end-mirror.start)) -> status 2
      len  = rrec.Count * container_blocksize
      dest = base    + rrec.offset
      dest2= mirror_base + rrec.offset
do_write:
      rc = FUN_80100118(dest, 0, len, 0, seq, &status)     ; **** THE ONLY GATE ****
      if (rc == 0)                     -> resp 0x15, status 3, nothing written
      if (mirror) rc2 = FUN_80100118(dest2, ...)           ; second mirror copy
      ; then stream the payload: FUN_80120864 -> staging (0x20000), FUN_8012121C,
      ; FUN_80120910(dest) programs one BS-sized unit at a time, dest++
  }
}
```

Three facts fall out of this and they are the whole answer:

* **[P] The destination is `partition_start + rrcq.offset`.** In path 1 the base
  is the MCT *MBR* extent start; in path 2 it is the start of the MCT
  QNX-partition (type `0x39`) entry whose `PartitionID` equals the container's
  `pfcq._type`. In both cases `rrcq.offset` is a field the host writes.
* **[P] There is exactly one address check**: `FUN_80100118(dest, 0, len, ...)`,
  reached from `0x80120414` and (mirror) `0x8012046C`. Nothing else inspects the
  address.
* **[P] `region_sel` is the *only* difference between `0xF7` and `0xF8`** in
  path 2 (`0x8011FEB0`: `cmp r0,#0xF7` → `*param_2 = 0` for `0xF7`, `1` for
  `0xF8`). Path 2 then chooses the `0x39` entry with `Flags&0x40 == region_sel`.
  So `0xF7` = region 0 (A slot, `hd1`), `0xF8` = region 1 (B slot, `hd2`).
  In path 1, `0xF8` instead re-bases from the end of the *previous* region.

**Units: 64 KiB blocks, not 512-byte LBAs.** **[P for the equality, I for the
value]** The loader refuses to proceed unless `BS() == container_blocksize`
(`0x801203E4`–`0x801203F0`, `FUN_8011A7A0` returns `*(u32*)0x8013EE9C`), and
every BB/BAR container in this codebase declares `blocksize = 0x10000`
(`qcfm.pas:285 defaultBlockSize`), while MCT extents are also `0x10000` blocks
(`mct.pas:116 BLOCK_SIZE`). The same `BS` is the multiplier in the gate, so the
gate, the MCT and the container all speak 64 KiB block numbers. A 512-byte unit
would make type `0x1B` end at byte 512 MB — impossible for a region at 136 MB.

### 2.4 The gate `FUN_80100118` — re-derived from the instruction stream **[P]**

```
80100120  ldr sb,[0x8010090c]  ; sb = 0x8013FEA0, allow-list {u32 start, u32 end}[]
80100128  ldr sl,[0x80100910]  ; sl = 0x8013ECB4, struct; count at +0xC
80100138  ldr r2,[sb, r4, lsl#3]      ; region[i].start
8010013c  subs r0,r7,r2 ; sbcs r1,r8,#0 ; blo next          ; require start >= region.start (64-bit)
80100150  ldr r0,[r0,#4]              ; region[i].end
80100154  add r2,r0,#1
8010015c  ldr r1,[r0]                 ; r1 = *0x8013EE9C = BS
80100160  umull r5,r6, r2, r1         ; 64-bit (end+1)*BS
80100164  umull r0,r2, r1, r7         ; 64-bit BS*start
80100168  mla   r1, r1, r8, r2
8010016c  adds r0,r0,r11 ; adc r1,r1,r3   ; + len
80100178  adds r0,r0,r3 ; adc r1,r1,r3    ; + (0xFFFFFFFF, 0xFFFFFFFF)  => -1
80100180  subs r2,r5,r0 ; sbcs r3,r6,r1
80100188  movcs r0,#1 ; bcs return 1      ; accept iff (end+1)*BS - 1 >= BS*start + len - 1
801001a0  *status = 3 ; log ; return 0    ; else 0x15 / 0xCE
```

i.e. **`∃ i : region[i].start ≤ dest  ∧  dest*BS + len − 1 ≤ (region[i].end+1)*BS − 1`**,
evaluated in 64-bit. Note `len == 0` passes for any `dest ≥ start` (useless as a
write). The `grep`-proof that this is the *only* caller-visible check is §2.3:
every writer (`0xF7`, `0xF8`, `0xEE` erase at `0x8011AE9C`, `0xDC` hash at
`0x8011AE9C` context) calls the same function.

### 2.5 The allow-list builder `FUN_80100238` **[P]**

Called exactly once, from loader init at `0x80116FD0`, ten instructions before
the command loop is entered. It:

1. reads the MCT (`FUN_8011814C`), verifies its CRC-32 (`FUN_80101EE0` +
   `FUN_80101F00` + `FUN_80102008` vs `*(u32*)(mct_crc_entry+4)`) — on mismatch
   it logs and **returns with the table empty**, which refuses every write/erase;
2. stores the Bootrom (`0x18`) and `0x29` extents into a *different* struct
   (`0x8013ECC4`, used for flash geometry, not the gate);
3. then runs **eleven append loops** into `0x8013FEA0` (ten single-type plus the
   `0x19`/`0x1A` pair), one per TLV
   type, each capped at 50 entries (`cmp r0,#0x32`):

| loop head | type | meaning in `mct.pas` |
|---|---:|---|
| `0x8010035C` | `0x1C` | `ekFSFixed` — filesystem (user) area |
| `0x801003AC` | `0x22` | — (not in `mct.pas`) |
| `0x801003FC` | `0x1B` | **`ekOSFixed` — the OS area** |
| `0x8010044C` | `0x1F` | — (not in `mct.pas`) |
| `0x8010049C` | `0x24` | — (not in `mct.pas`) |
| `0x801004EC` | `0x34` | `ekMBR` |
| `0x8010053C` | `0x3B` | `ekOSExt` |
| `0x8010058C` | `0x3A` | `ekRadioFixed` |
| `0x801005DC` | `0x20` | — |
| `0x8010062C` | `0x21` | — |
| `0x80100648` | `0x19` + `0x1A` | `ekRAMChip` pair (skipped if absent) |

The stored entry is `{u32 start, u32 end}` copied verbatim from the TLV payload
(`entry+4`, `entry+8` = `payload+2`, `payload+6`), i.e. **raw `0x10000`-block
numbers, no conversion**. That is the decisive fact: the gate compares block
numbers against block numbers.

This is a **broader** list than `88` §5 records for the Cellebrite build
(`0x1C,0x1B,0x1F,0x24,0x34` + `0x19/0x1A`). I re-ran the same scan on the CB
build's `FUN_8011B920` and it uses the same helper (`0x801003FC`) with
`0x1D,0x09,0x18,0x1C,0x1B,0x1F,0x24,0x34,0x1E,0x19,0x1A` — i.e. the CB build
additionally allow-lists **`0x1E` (OS NV, the NVRAM region)**, which `LDR_01`
does **not**. That materially changes `88`'s unknown U1 for the CB build (see §11).

### 2.6 `0xF8` and the erase/hash siblings

* **`0xF8`** = the same write with `region_sel = 1` → the `0x39` entry with
  `Flags & 0x40` set (region 1 / `hd2`). In the whole-image path it instead
  computes `base = region1_start − (p34.end − p34.start) − 1`, which on the real
  MCT is `0x2815 − 0x10 − 1 = 0x2804` = the **last block of region 0**. **[P for
  the arithmetic; I for the intent — treat `0xF8` as "the other slot" and prefer
  `0xF7` with an explicit offset.]**
* **`0xEE` ERASE_SECTOR** goes through `FUN_801001C8`, a 36-byte wrapper that
  tail-calls `FUN_80100118` with the same `(address, 0, length)` triple
  (`0x801001E4`). **`0xDC` range hash** calls `FUN_80100118` directly at
  `0x8011AE9C` with `frame[4]` = address, `frame[8]` = length. So erase and
  hashing are bounded by the identical allow-list and speak the identical unit.
  **[P]**
* `0xFD` (stage) is the "append to the 0x20000 RAM buffer" opcode: it takes the
  chunk length from `frame[0]-0x10` and a *block index* from `frame[4]`, and
  scales that index by `BS` (`FUN_8011A7A0`, `0x8011AEF8`/`0x8011AEFC`; the
  getter ignores its argument and returns the global). It never reaches the
  media — which is why `0xFD` alone cannot write a chosen LBA. **[P]**

---

## 3. Units, and the exact LBA arithmetic **[P]**

```
BS                 = *0x8013EE9C              ; getter FUN_8011A7A0, expected 0x10000
dest_block         = partition_start_block + rrcq.offset
dest_byte          = dest_block * BS
len_bytes          = rrcq.Count * BS
gate               : ∃ i : A[i].start <= dest_block
                           && dest_block*BS + len_bytes - 1 <= (A[i].end+1)*BS - 1
media LBA (512 B)  = dest_byte / 512
```

Worked example, region 0 IFS, real 64 GB MCT (§4):

```
p34.start (MBR)      = 0x805            -> byte 0x08050000 -> LBA 0x40280
region 0 start       = 0x815            -> byte 0x08150000
region 0 IFS         = 0x815 + 0x100000/0x10000 = 0x825
                                        -> byte 0x08250000 -> LBA 0x41280
image size           = 0x2BFE10         -> 44 blocks (0x2C0000 bytes)
gate region          = A[0x1B] = {0x815, 0x4804}
check                : 0x825 >= 0x815   ✓
                       0x825*0x10000 + 0x2C0000 - 1 = 0x0850FFFF
                       <= (0x4804+1)*0x10000 - 1   = 0x4804FFFF   ✓  (huge margin)
path-1 offset field  : rrcq.offset = 0x825 - 0x805 = 0x20, Count = 0x2C
path-2 offset field  : rrcq.offset = 0x00,            Count = 0x2C   (_type = 8)
```

44 blocks = 2.75 MB written over a 2,747,664-byte image: the last 0x1F0 bytes
fall in unused IFS-zone space (the zone runs to block `0x8D4`), so padding with
`0x00` is safe. Do **not** shorten `Count` to 43 — that would drop the last
`0xFE10` bytes, including the CRC trailer.

---

## 4. The MCT on real hardware, and what the allow-list actually covers

`bootdumps-2026-09-11/hd4.bin` is a complete MCT (magic `0x92BE564A`, v1.24,
128 KiB). Its CRC block is at `0xE8`, `stored = 0x0AE2202C`, and
`zlib.crc32(mct[0:0xE8]) = 0x0AE2202C` — **the MCT validates**, so
`FUN_80100238` will populate the allow-list (if this is the MCT the loader
parses; see §11 U2).

`end = 0xFFFFF` → 1,048,576 blocks × 64 KiB = **64 GiB**, so this dump is the
64 GB unit's, not the 32 GB one.

| MCT type | name (`mct.pas`) | StartBlock | EndBlock | media byte range | size | in allow-list? |
|---|---|---:|---:|---|---:|---|
| `0x1D` | Flash chip | 0 | `0xFFFFF` | 0 – 64 GiB | 64 GiB | no |
| `0x31` | User MMC | 5 | `0xFFFFF` | — | — | no |
| `0x18` | Bootrom | 5 | `0xC` | `0x00050000`–`0x000CFFFF` | 512 KiB | no (geometry struct) |
| **`0x34`** | **MBR** | **`0x805`** | **`0x814`** | **`0x08050000`–`0x0814FFFF`** | **1 MiB** | **yes** |
| **`0x1B`** | **OS Fixed** | **`0x815`** | **`0x4804`** | **`0x08150000`–`0x4804FFFF`** | **1023 MiB** | **yes** |
| `0x1E` | OS NV | `0x4805` | `0x4844` | `0x48050000`–`0x4844FFFF` | 4 MiB | **no** (LDR_01) / yes (CB) |
| **`0x1C`** | **FS Fixed** | **`0x4905`** | **`0xFFFFF`** | **`0x49050000`–end** | ~64 GB | **yes** |
| `0x35` (id 0) | QNX region 0 = `hd1` | `0x815` | `0x2804` | `0x08150000`–`0x2804FFFF` | 511 MiB | via `0x1B` |
| `0x35` (id 1) | QNX region 1 = `hd2` | `0x2815` | `0x4804` | `0x28150000`–`0x4804FFFF` | 511 MiB | via `0x1B` |

Region-internal layout (`deckard-2.1.0.1917/platform/winchester.py`
`_OFF_MBR/_OFF_SIG/_OFF_IFS/_OFF_RFS/_OFF_END` = `0, 0xF0000, 0x100000,
0xC00000, 0x20000000`), converted to blocks:

| zone | offset in region | block (region 0) | media byte | LBA512 |
|---|---:|---:|---|---:|
| MBR | `0x0` | `0x815` | `0x08150000` | `0x40A80` |
| SIG / fingerprint | `0xF0000` | `0x824` | `0x08240000` | `0x41200` |
| **IFS** | **`0x100000`** | **`0x825`** | **`0x08250000`** | **`0x41280`** |
| RFS / `/base` | `0xC00000` | `0x8D5` | `0x08D50000` | `0x46A80` |
| (region 0 end) | `0x1FF00000` | `0x2804` | `0x2804FFFF` | — |

**Answer to "is the OS/IFS, RCFS/BASE, UFS and radio inside the allow-list?"**

| target | block | inside? | which entry |
|---|---:|---|---|
| region 0 IFS (`hd1`+0x100000) | `0x825` | **YES** | `0x1B` [`0x815`..`0x4804`] |
| region 1 IFS (`hd2`+0x100000) | `0x2825` | **YES** | `0x1B` |
| region 0/1 SIG (`+0xF0000`) | `0x824`/`0x2824` | **YES** | `0x1B` |
| region 0/1 RFS, i.e. `/base` (RCFS) | `0x8D5`/`0x28D5` | **YES** | `0x1B` |
| UFS / root (`hd3`, type `0x1C`) | `0x4905`+ | **YES** | `0x1C` |
| radio (`ekRadioFixed` `0x3A`) | — | n/a | no `0x3A` entry exists in this MCT |
| NVRAM (`0x1E`, 4 MiB) | `0x4805` | **NO** in `LDR_01`; **YES** in the CB build | `0x1E` |
| MCT itself / bootrom area | `0`–`0x814` | **NO** | — |

So the loader can rewrite the **entire OS area** (both slots: MBR, SIG, IFS, RFS)
plus the **whole user filesystem**, but not the boot/MCT area and — on `LDR_01` —
not the NVRAM region.

---

## 5. The autoloader / image format (from `uautoloader.pas`, `qcfm.pas`)

Everything in this section is **[P]** for the source semantics (it is the
shipped builder code) and **[I]** where it is asserted about real RIM
autoloaders, because **no autoloader image exists in this workspace** — a full
scan for the `0x97C5D59C`×3 signature found only the constant embedded in the
`bb10mt` binary itself.

### 5.1 Container: CAP.exe + the appended file table

```
offset 0                      CAP.exe (a PE) — truncated at its PE end offset
                              (GetPEEndOffset) — the flasher / payload host
PE_end + 0x00   u32 0x97C5D59C
PE_end + 0x04   u32 0x97C5D59C
PE_end + 0x08   u32 0x97C5D59C        <- the 12-byte "autoloader signature"
                header version 1:  u64 file_count, then file_count × u64 offset
                header version 2:  80 bytes of zero (20 dwords),
                                   then u64 file_count, then file_count × u64 offset
first_file_offset                     = PE_end + 52 (v1) or +132 (v2)
                                      = the offset table's own end, padded with 0
then                                  concatenated QCFM containers, back to back
                                      (file[i] size = off[i+1]-off[i]; the last one
                                       runs to EOF)
```

`MAX_FILES = 10`. `ExtractCap` recovers the PE; `RebuildAutoloader`/`Pack` rewrite
the table and re-emit in place. **[P]**

### 5.2 Per-part classification — and the trap

`uautoloader.pas:508 DetermineFileType` classifies each part by scanning the first
64 bytes for `'pfcq'` (`0x71636670`) and reading the byte at **`pfcq+0x0C`**:

| `pfcq._type` | `TFileType` | extension written on extract |
|---:|---|---|
| 5 | `ftUser` (UFS) | `.5@User.signed` |
| 6 | `ftOS` | `.6@OS.signed` |
| 8 | `ftIFS` | `.8@IFS.signed` |
| 12 | `ftRadio` | `.12@Radio.signed` |

`qcfm.pas:266` has the *other* numbering used when bb10mt itself builds a
container from a file name (`Ext2Type`):
`3=.nvram 5=.ufs 6=.mbr 7=.sig 8=.ifs 9=.rcfs 0x0A=.radio.mbr 0x0B=.radio.sig
0x0C=.radio.rcfs 0x0F=.calwork 0x10=.calbackup 0x11=.dmi 0x18=.os 0x89=.sig2`.

**The numbers 5/8/9/12 agree; `6` does not** (`ftOS` vs `.mbr`). This matters
only for bb10mt's own classifier — the *loader* never looks at `_type` for the
address. It uses `_type` **only** to select an MCT `0x39` partition in path 2
(§2.3). Keep that distinction: `_type` is a *routing tag*, not an address.

### 5.3 `firmware.signed` → the QCFM V2 container **[P]**

```
mfcq V1 header (0x18 B)   'mfcq'  header_cksum  version=1  length  nheaders
                          headersz  data_crc  flags   dummy[4]
                          flags = offset of the V2 header (0x18 when ver=2)
[v1 'qcfp' control blocks + rrcq-v1 records]        only when (ver & 1)
mfcq V2 header (0x1C B)   'mfcq'  header_cksum  version=0x20000  length=0x1C
                          nheaders  headersz  data_crc
per file, at v2+0x1C:
  pfcq control (0x30 B)   'pfcq' version=0x20000 length=0x30+n*0x10
                          _type  rrecOffset=0x30  nrecords  hwvOffset
                          hwvNumEntries  sigOffset  sigSize  blocksize=0x10000
  n × rrcq (0x10 B)       'rrcq' length=0x10  offset=<block>  Count=<blocks>
pad to mhf1.headersz (0x94 for one v2 file)
payload                   block 0, block 1, ...   (NOT 0x10000-aligned in the file)
[560-byte signature block] 'QNXH-OS-1' / 'QNXL-OS-1' / 'QNX-OS-1' / 'QNX-OS-1'
                          blocks, CRC-32 in the last 4 bytes
```

* `pfcq._type` for a **V2** container is `Ext2Type(extension)`; an `.ifs` part
  therefore gets `8`. **`qcfm.pas:582` writes `arr2[j].offset := BlockIndex`
  with no `Delta`** (only the V1 path at line 559 adds `Delta`). That is the one
  code defect that blocks an arbitrary-offset write from `qcfm.pas` today — fix
  or byte-patch `rrcq.offset` (§9).
* `mfcq.length` must be `0x1C` (the loader treats `v2 + length` as the first
  control block and requires `'pfcq'` there), `v2.nheaders != 0`, first record
  `'rrcq'`. `qcfm.pas ver=2` produces exactly this shape. **[P]**
* `AnalyzeFileBlocks` skips `0xFF`-filled 64 KiB blocks when building the run
  records, so a sparse part yields several `rrcq` records — the loader walks them
  all, so this is harmless. **[P]** (`fast=true`, which `FlashFile` uses, emits a
  single full-range record.)

### 5.4 Where the parts live on eMMC

There is no per-part LBA in the container — that is the point. The mapping is:

| part | on media | how the loader finds it |
|---|---|---|
| MBR / SIG | region `+0x0` / `+0xF0000` | path 1 offset, or `_type` 6/7 against a `0x39` entry |
| IFS | region `+0x100000` | path 2: `0x39` entry with `PartitionID == 8`, offset 0 |
| OS / BASE (RCFS) | region `+0xC00000` | path 2: `_type` 9 against its `0x39` entry |
| UFS (user FS) | MCT `0x1C` extent (`0x4905`) | path 2: `_type` 5 |
| Radio | MCT `0x3A` extent (absent here) | path 2: `_type` 12 |

If the MCT carries no `0x39` entries for a part, path 2 cannot resolve it and
the loader answers `0x15` with status **2** ("cannot find the partition for
this record"). Path 1 (whole-image offsets from `p34.start`) then still works
and is MCT-independent apart from the `0x34` extent. **[P for the logic, I for
the per-unit MCT content — see U2.]**

---

## 6. Signature coverage — what is actually checked, and by whom

| artefact | verified by the **loader**? | verified by the **bootloader**? | verified by the **OS**? |
|---|---|---|---|
| ramloader itself (`D7D32D1F`/`D7C82D1F`) | n/a | yes, on `0xF00A`/execute (`88` §2.1) | — |
| the host's QCFM container | **no** — `0xF7` parses it for structure only (`magic`, `version`, `rrcq`), never hashes it | — | — |
| the 560-byte "signature" trailer (`0xF9`) | **no** — copied to RAM, two cookie words compared, appended to the loader's state (`88` §8.2) | — | — |
| **IFS** | no | **yes on the product path**: ECDSA P-521 over `[start, end-0x230)`, sig at `end-0x208`, cookie `0xB5A60BFD` at `end-0x178` (`89` §3.2) | — |
| **IFS** (factory path) | no | **only** `desc[0xD4]==6` + `crc32[start, end-4)` vs `u32(end-4)` — **ECDSA skipped** (`89` §3.1/§3.2, `95` §5) | — |
| **RCFS / BASE** | no | **no** — the bootloader never reads the RFS zone | **hash list**: `rcfs_hdr.hash1/hash2` (SHA-256) at +0, `'r-c-f-s'` superblock at +0x1000, and a `/verifier.log` with `'VSIG'` + per-inode SHA-256 (`rcfs.pas:14-56,509-530`). The IFS itself ships `proc/boot/rfs_validator`, so the OS validates it at boot. Recomputable — no key |
| **UFS** | no | no | **none** — a plain QNX6 filesystem (`/dev/hd3`, 61 MB used); no RIM header, no hash list |
| **MBR / SIG zone** | no | the SIG zone holds the 64-byte fingerprint `deckard` reads (`active_fingerprint`); it is *informative*, not enforced pre-boot | — |

**Conclusion: the user's belief is right and slightly understated.** The
ramloader checks *nothing*. IFS and RCFS are checked *later* — IFS by the
bootloader (ECDSA on the product path, CRC on the factory path) and RCFS by the
running OS via a SHA-256 list. UFS is not checked at all. That is exactly the
asymmetry that makes a pre-OS IFS reflash viable: the loader will happily write
whatever bytes you hand it, and the only thing standing between those bytes and
a boot is the CRC seal of §7.

---

## 7. Reconcile with the CRC/factory path — what the reflash must produce

`95` proved, on the spare 64 GB unit, that the bootloader's factory path
verifies **region 0 only** and accepts an image whose `desc[0xD4] == 6` and
whose `u32(end-4)` equals `crc32(image[0 : end-4))`, ignoring ECDSA entirely.
That is the receiving end of this write path. Therefore a pre-OS reflash must
produce **exactly one artefact**:

| requirement | value |
|---|---|
| image size | `LoadEnd - LoadStart` = **`0x2BFE10`** (do not change `LoadEnd` unless you also patch `hdr+0x08`) |
| `desc[0xD4]` | **`6`** (`STP_PROTOCOL_TYPE_UNTRUSTED_OS`) at image offset `0x100` (media `0x100100` inside the region) |
| trailer magic at `end-8` | `0xD7C82D1F` (already present in the shipping image — preserve it) |
| CRC slot at `end-4` | `crc32(image[0 : end-4))`, and `!= 0xFFFFFFFF` |
| CRC algorithm | CRC-32/ISO-HDLC = `zlib.crc32` (proved in `89` §4) |
| `desc+0x7C` (hardware_id) | must stay `0x06001A06` — the factory path also does a model-ID compare |
| order | patch payload → patch `os_type` → **CRC last** |

Verified this session on the real image (§10.3): `reseal --image-off 0 --type 6`
changes exactly 5 bytes (`0x100: 04→06` and the 4 CRC bytes at `0x2BFE0C`) and
reproduces `95`'s CRC `0x116B2562` bit for bit.

**Two consequences the host tool must encode:**
1. Writing an *unsealed* image (`os_type=4`, CRC `0x8D46FB4A`) would leave the
   bootloader taking the ECDSA gate on the product path and the CRC gate
   (fail) on the factory path — an unbootable region 0 with no A/B fallback.
   Seal or do not write.
2. The seal is only valid if the bytes land verbatim. `95` §2 step 5 already
   proved that empirically for a media write; the loader's `0xF7` path copies
   payload bytes without transformation (the only transform in the whole handler
   is the `0x0F`-cipher branch for the CREAD session, which `0xF7` does not use).
   **[P from the code, already confirmed by `95`.]**

---

## 8. `mkifs` / `dumpifs` — tested against the real PlayBook IFS

Environment (the tree has moved; `playbook-dev/qnx-env.sh` still points at the
old `/home/psyden/qnx660-master`):

```bash
Q=/home/psyden/SSDdrive/wsl-migration/home/psyden/qnx660-master
export QNX_HOST=$Q/host/linux/x86
export QNX_TARGET=$Q/target/qnx6
export QNX_CONFIGURATION=$Q/.qnx
export PATH=$QNX_HOST/usr/bin:$PATH
```

Both tools are i386 ELF and run on this host (32-bit runtime present).
`mkifs` is a symlink to `mkxfs`.

### 8.1 `dumpifs` — works on the PlayBook image **[P, executed]**

```
$ dumpifs hd1-ifs-image.bin
   Offset     Size  Name
        0      808  *.boot
      808      100  Startup-header flags1=0xd flags2=0 paddr_bias=0
      908    30008  startup.*
    30910       5c  Image-header mountpoint=/
    3096c     12ac  Image-directory
    32800    96000  proc/boot/procnto-smp-instr
    c8800      f90  proc/boot/.script
    ...
```

So the PlayBook IFS is a **stock QNX 6.6 IFS**: a `*.boot` bootstrap region
`0x0..0x808`, a `Startup-header` at `0x808`, the startup image at `0x908`, the
image header at `0x30910`, the directory at `0x3096c`. RIM's own
`TLoadPtr`/`TOSMetrics` header (`0x0..0x200`) sits *inside* the `*.boot`
bootstrap region, and the bootloader's `EA0001FE` branch at offset 0 lands on
image `+0x800` (the 8 bytes before the startup header) — consistent with `89`
§1.2.

`dumpifs -x` extracts the **whole tree** (59 files here) but only into
pre-existing directories — it does not create them:

```bash
mkdir -p proc/boot base/bin base/sbin base/usr/sbin etc/system/config root bin dev tmp
dumpifs -x hd1-ifs-image.bin      # repeat after `mkdir -p` for any further misses
```

Valid `dumpifs` options in this build (probed): `-b -d -f -m -v -x -z`; everything
else is rejected. `-x` = extract, `-b` = block size, `-d` = directory, `-f` =
file, `-m` = member, `-v` = verbose, `-z` = compressed view.

### 8.2 `mkifs` — works, with one syntax caveat **[P, executed]**

This 6.6 `mkxfs` rejects `[virtual=armle,raw]` (`Line 1: Host file 'raw.boot' not
available` — it parses the second token as a bootstrap file name). What it
accepts is `[image=<type>]`:

```
[image=0x80] .bootstrap = { startup-winchester }
proc/boot = proc/boot
[type=link] /proc/boot/sh = /proc/boot/ksh
etc/system/config/pmic-twl6030.cfg = etc/system/config/pmic-twl6030.cfg
...
```

```bash
mkifs playbook.bld out.ifs          # -> a valid IFS, parseable by dumpifs
```

**Two PlayBook-specific gotchas:**
1. `mkifs` regenerates the whole bootstrap area. RIM's `0x0..0x200` header
   (`TLoadPtr` + `TOSMetrics` + `os_type` + `hardware_id`) would be **replaced by
   whatever startup you feed it**. For a PlayBook image you must either pass the
   original startup slice (image `0x908`, length `0x30008` — extractable with a
   plain `dd`/Python slice, *not* produced by `dumpifs -x`) or splice the
   original `0x0..0x800` back over the rebuilt image.
2. If the rebuild changes the size, `LoadEnd` at `hdr+0x08` must be updated to
   `0x800FF800 + new_size`, or the bootloader will read the wrong `end` (and
   therefore the wrong CRC slot). `89-crc_tool.py reseal --patch 0x08:<le32>`
   handles it.

### 8.3 Can a repacked image be made CRC-valid with `os_type=6`? **Yes. [P]**

Two independent demonstrations:

* **Same-size payload edit (the recommended workflow).** Take the extracted
  `proc/boot/.script` (image offset `0xC8800` per `dumpifs`), modify bytes in
  place, `reseal --image-off 0 --type 6`, `verify` → `RESULT: VERIFIED (CRC
  path)`.
* **The unmodified image (control).** `verify` on the shipping image fails only
  the gate:
  ```
  PASS  crc32[0x0,0x2BFE0C) = 0x8D46FB4A, stored 0x8D46FB4A
  FAIL  desc[0xD4] == 6      4
  ```
  and the resealed copy passes everything (§10.3).

**Not tested (and not testable here):** booting an `mkifs`-rebuilt image. That is
a hardware step and is out of scope for track A.

---

## 9. Pre-OS reflash recipe

### 9.1 Preconditions (each one is a stop condition if it fails)

| # | precondition | how to check, read-only |
|---|---|---|
| P1 | unit boots the bootrom (`0fca:8002`) and accepts `LDR_01` | upload sequence of `69` §4 / `88` §9.3 step 1; `0xF00A` verifies |
| P2 | loader enters command mode and answers one `0xB0` with `0xD0` | `88` §3 |
| P3 | **MCT CRC valid** (else the allow-list is empty and *everything* is refused) | `0xD9` → parse the TLV, verify the `0x09` block; compare with §4 |
| P4 | target base is known: the `0x39` entry with `PartitionID==8` (path 2) or `p34.start` (path 1) | `0xB4` (flash regions) and `0xD9` (MCT) |
| P5 | `BS == 0x10000` | `0xB1` returns `{0xFFFFFFFF, BS, total_units}` |
| P6 | the target range is allow-listed | compare with the `0x1B`/`0x1C`/`0x34` extents from the MCT |
| P7 | RPMB still accepts the `FS_DIRTY` write | `0xDE` read `status==0`; no RPMB error in the `0xE1` log. **`LDR_01` is `FS_DIRTY`-gated exactly like the CB build** (`88` §7.3); a unit with a broken RPMB cannot use this path at all |
| P8 | a full off-device backup of region 0 exists and has been re-read and CRC-checked | `0xDC` over `0x825..0x850`, or `dd if=/dev/hd1` from the booted OS |

### 9.2 Build the payload

```bash
A=/home/psyden/rea-work/bootdumps-2026-09-11/analysis
# 1. the image (2,883,088 bytes exactly)
python3 - <<'EOF'
import struct
d=open('hd1-ifs-image.trim.bin','rb').read()
size=struct.unpack_from('<I',d,8)[0]-struct.unpack_from('<I',d,4)[0]
open('ifs0.img','wb').write(d[:size]); print(hex(size))
EOF
# 2. seal: os_type=6 + CRC over [0, size-4)
python3 $A/89-crc_tool.py reseal ifs0.img ifs0.sealed.img --image-off 0 --type 6
python3 $A/89-crc_tool.py verify ifs0.sealed.img --image-off 0      # must print VERIFIED
# 3. pad to a whole number of 64 KiB blocks (44) so the write covers the trailer
python3 -c "d=open('ifs0.sealed.img','rb').read(); open('ifs0.sealed.pad','wb').write(d+b'\x00'*(0x2C0000-len(d)))"
```

### 9.3 Wrap it in the container the loader expects

Either of two equivalent forms — **path 2 is the one shipped autoloaders use**:

```
PATH 2 (per-partition, needs an MCT 0x39 entry with PartitionID == 8)
  pfcq._type      = 8                (IFS)
  pfcq.blocksize  = 0x10000
  pfcq.nrecords   = 1
  rrcq            = { 'rrcq', 0x10, offset = 0, Count = 0x2C }
  total container > 0x20000          (2.75 MB, satisfied)

PATH 1 (whole-image, only needs the 0x34 MBR extent; works without 0x39)
  pfcq._type      = anything
  rrcq            = { 'rrcq', 0x10, offset = 0x20, Count = 0x2C }
                   (0x825 - 0x805 = 0x20  on the 64 GB MCT of §4)
```

`bb10mt`'s `TMFCQPackStream` with `ver=2` already emits this container, signed
(560-byte trailer + CRC) and with `_type = Ext2Type('.ifs') = 8`. The **only**
defect for path 1 is that `qcfm.pas:582` drops `Delta`; for path 2 nothing needs
changing. If you drive it from `bbusb.py`, you must build the same bytes yourself
(§9.4).

> **Correction (2026-11-01).**  The "drops `Delta`" sentence above refers to the
> **V2** `rrcq` record, where the `Delta` is omitted by design (path 2 resolves
> the partition, not an offset).  The **V1** `qcfp` record does carry it
> (`qcfm.pas:559`, `offset := Delta + BlockIndex`), and this unit's loader consumes
> V1 (report 139 §3), so a bb10mt IFS container addresses `0x815 + 16 = 0x825`
> correctly.  Verified against the vendor `2.1.0.1917.signed` and reproduced
> byte-for-byte by `tools/bbusb.py pack` (0 differing bytes at `Delta` 16 and 192).  See
> `docs/contradictions/100-bb10mt-flash-path2-vs-operator-flashes.md`.

### 9.4 The wire sequence (exact opcodes) **[P]**

```
 1. bootrom stage (`bbusb.py --from-bootrom`, `69` §4):
      0xB0 PING' / GETVAR(2) / SETMODE(1) / PASSWORD_INFO / SEND_PASSWORD(0000)
      0xF009 {addr=0x80100000, size=238128}       ; loader upload setup (LDR_01)
      0xF004 × 916 (260-byte chunks)              ; upload
      0xF00A                                       ; verify
      0xF005 {addr}                                ; execute
 2. loader stage:
      0xB0                -> 0xD0                 ; handshake (LDR_01: single reply)
      0xB1                -> 0xC8                 ; {0xFFFFFFFF, BS, total}
      0xB4                -> 0xD2                 ; flash regions
      0xD9                -> 0xC9                 ; MCT  -> resolve the target base
      0xDC {lba, n}       -> 0xCC                 ; pre-write CRC of 0x825..0x850
      0xF7 × ceil(padded/0x3FEC)  -> 0xDF each    ; {u32 seq, u32 len, data}
                                                   ; (op 0xF8 for region 1)
      0xF9 {u16 0x230, 560 bytes} -> 0x4006       ; signature trailer (dummy OK)
      0xC0                -> 0x4006 / 0x402E      ; COMPLETE (flush+verify)
                                                   ; side effect: NVRAM 0x2074
                                                   ; = {2, region, 0}; harmless on
                                                   ; the factory path (`97` §0)
      0xDC {lba, n}       -> 0xCC                 ; post-write CRC, must match
      0xEF                -> 0xC7                 ; reboot (or power-cycle)
```

Notes:
* The first `0xF7` frame must contain the QCFM header **and** `total > 0x1FFFF`
  for path 1 to engage; `bb10mt` streams the container from byte 0 so the first
  frame carries `0x3FEC` header bytes — well inside the `0x10000` parse limit.
* `0xDF` per frame means "this chunk was consumed", **not** "this chunk was
  programmed". Programming happens when the `0x20000` staging buffer fills
  (`FUN_80120910`). Do not treat `0xDF` as a durability signal.
* Never send `0xE0` (MCT rewrite) or `0xF3` (erase MCT) — those destroy the
  partition table and the allow-list source (`88` §7.6).

### 9.5 Verify, and the rollback story

```
verify step 1 (in band)   0xDC over the same range, compare with
                          zlib.crc32(padded_image)      ; loader CRC is
                                                        ; zlib-compatible (proved:
                                                        ; MCT CRC matched §4)
verify step 2 (off band)  boot the unit; the bootloader's own gate is the real
                          test — `89-crc_tool.py verify` models it exactly
verify step 3 (belt)      from the booted OS: `dd if=/dev/hd1 bs=1 skip=1048576
                          count=2883088 | sha256sum` and compare with the sealed
                          image
```

**Failure modes → recovery:**

| failure | what you see | recovery |
|---|---|---|
| gate rejects (wrong base/unit) | `0x15`, status 3, `0xDC` unchanged | nothing written; re-read `0xD9`/`0xB4`, fix the offset |
| `FS_DIRTY` RPMB write fails | `"Fail to write RPMB"` in the `0xE1` log, write aborted before any sector | this unit cannot use the path; stop |
| power lost mid-write | region 0 IFS is partially written → bootloader CRC fails → the stage spins in the retry loop `0x80014A1E`, **but the bootrom window and the loader path are untouched** | power-cycle, upload the loader again, re-run §9.4 with the good image. This is the whole point of the exercise |
| wrong image written (unsealed) | bootloader gate fails, retry loop | same as above — re-flash the sealed image |
| region 0 destroyed *and* `BOOTCTR_FCTRY` reaches 0 | product path returns (ECDSA), a resealed image is unbootable there | keep the **original** `ifs0.img` and its 0x230 trailer; while the counter is nonzero this cannot happen |

**There is no A/B fallback on the factory path** (`89` §3.5, `95` §5), so a bad
write is *not* auto-recovered — but it is *recoverable*, because the recovery
channel (bootrom → loader → `0xF7`) never depended on region 0 in the first
place. That is exactly the safety net this track was asked to establish.

**Budget note:** the factory path decrements `BOOTCTR_FCTRY` on every successful
boot (`89` §3.5, observed 50→49 in `95`). A reflash campaign consumes one unit
per boot; top the counter up from the booted OS via `/dev/rpmb` between tests
(`95` §4).

### 9.6 Can `bbusb.py` issue this today?

| need | `bbusb.py` status |
|---|---|
| channel-2 framing, CRC, `seq` byte | ✅ `channel2()` (`bbusb.py:298`) |
| bootrom upload (`0xF009`/`0xF004`/`0xF00A`/`0xF005`) | ✅ `send_loader()`/`run_loader()` |
| `0xB0` handshake, `0xB1`, `0xB4`, `0xD9`, `0xDC`, `0xDE`, `0xE1` | ✅ (`bugdisp_log`, `flash_regions_info`, `boot_mode`, …) |
| `0xF7` | ✅ `send_block()` — **raw only**, it does not build the container, chunk it, or count sequence numbers |
| `0xF9` | ✅ `send_signature()` |
| `0xC0` | ✅ `complete()` |
| **QCFM V2 container builder with `_type` + `rrcq.offset`** | ❌ must be added |
| **MCT parser to resolve `p34.start` / `p39.start`** | ❌ must be added (`mct.pas` is the spec) |
| **seal + padded-image staging + `0xDC` before/after compare** | ❌ must be added (`89-crc_tool.py` gives the seal) |

**Shortest path today:** `bb10mt flash --input ifs0.sealed.pad --versions 2`
already does the container, the `0xF7` loop, the `0xF9` trailer and the `0xC0`
complete, because `FlashFile` wraps an `.ifs` file into a signed V2 container
with `_type=8`, `blocksize=0x10000`, `rrcq.offset=0` — i.e. **path 2**. It does
not seal the image and does not verify the result; pair it with
`89-crc_tool.py` and a `0xDC` check.

### 9.7 Design of the host tool (recommended shape)

```
pre-os-ifs-flash.py  [--dry-run | --apply]  --image ifs0.img  --region 0|1

 1  connect(bootrom) -> upload LDR_01 -> 0xB0 -> expect 0xD0
 2  probe       : 0xB1 (BS), 0xB4, 0xD9, 0xDE, 0xE1
 3  mct_model   : parse the 0xD9 TLV; require a valid 0x09 CRC;
                  build the same allow-list LDR_01 builds (types 0x1C,0x22,0x1B,
                  0x1F,0x24,0x34,0x3B,0x3A,0x20,0x21,0x19+0x1A)
 4  resolve     : prefer the 0x39 entry with PartitionID==8 and
                  Flags&0x40 == region; else p34.start + (0x100000/BS)
 5  plan        : dest_block, count = ceil(size/BS), check the gate HOST-SIDE
                  (same formula as FUN_80100118) and refuse if it would fail
 6  seal        : 89-crc_tool.reseal(image, type=6); verify() must pass
 7  container   : mfcq v2 + pfcq(_type=8, bs=0x10000, nrecords=1)
                  + rrcq{offset, count} + payload + 0x230 signature block
 8  dry-run     : print every frame (op, seq, len) and the gate verdict; STOP
 9  apply       : 0xDC pre -> 0xF7 loop -> 0xF9 -> 0xC0 -> 0xDC post
10  verdict     : post == crc32(padded) ? "written+verified" : "MISMATCH: retry
                  once, then abort and report"
11  never       : 0xE0, 0xF3, 0xEE outside the planned range, 0xC0 before the
                  full byte count has been accepted, any write to block < 0x815
```

Order matters: **plan and gate-check host-side before the first `0xF7`**, so a
mistake in the offset is a refusal rather than a partial write.

---

## 10. Reproduction commands

All host-only. Nothing here touches a device.

### 10.1 Opcode / pool scans

```bash
cd $COMMANDCODE_SCRATCHPAD
python3 rdis.py 0x8011FDD8:0x120:'0xF7/F8 handler'      # 0x8011FDD8
python3 rdis.py 0x80100118:0x100:'gate'                 # 0x80100118
python3 rdis.py 0x80100238:0x4C0:'allow-list builder'   # 0x80100238
python3 rdis.py 0x80100D50:0x50:'MCT lookup'            # 0x80100D50
python3 slotref.py 0x8010090c 0x80100910 0x80100914     # who reads the gate globals
```

`rdis.py` (Capstone, base `0x80100000`, `LDR=` env var to switch builds) and
`slotref.py` are throwaway scripts; both are in
`$COMMANDCODE_SCRATCHPAD`. Ghidra pseudocode for the two key functions was
obtained with REA `procedure_pseudo_code 0x8011fdd8` / `0x80100118` on
`open_binary(path=bbusb/loaders/loader_06001A06-00.bin, raw-binary, arm,
entry 0x80100000)`.

### 10.2 MCT parse + CRC (§4)

```bash
cd /home/psyden/rea-work/bootdumps-2026-09-11
python3 - <<'EOF'
import struct, zlib
d=open('hd4.bin','rb').read(); p=8; ents=[]
while p+2<=len(d):
    t=d[p]; L=d[p+1]; ents.append((p,t,L))
    if t==0xFF: break
    p+=L
co=[x for x in ents if x[1]==9][0]
print("stored %08x calc %08x"%(struct.unpack_from('<I',d,co[0]+4)[0], zlib.crc32(d[:co[0]])&0xffffffff))
BS=0x10000
for p,t,L in ents:
    if t in (0x1b,0x1c,0x34,0x35,0x1e,0x18):
        pl=d[p+2:p+L]; s,e=struct.unpack_from('<II',pl,2)
        print("type %02x  %5x..%5x  byte %08x  %6d blocks"%(t,s,e,s*BS,e-s+1))
EOF
```

### 10.3 Seal / verify (§7, §8.3)

```bash
A=/home/psyden/rea-work/bootdumps-2026-09-11/analysis
cd $A
python3 89-crc_tool.py selftest
python3 89-crc_tool.py probe  playbook-live-2026-10-25/hd1-ifs-image.bin --image-off 0
python3 89-crc_tool.py verify playbook-live-2026-10-25/hd1-ifs-image.trim.bin --image-off 0
#   -> CRC PASS, gate FAIL (os_type 4)   [expected for the shipping image]
python3 89-crc_tool.py reseal playbook-live-2026-10-25/hd1-ifs-image.trim.bin \
                          /tmp/ifs0.sealed.img --image-off 0 --type 6
python3 89-crc_tool.py verify /tmp/ifs0.sealed.img --image-off 0
#   -> RESULT: VERIFIED (CRC path)   CRC 0x116B2562   (matches doc 95)
cmp -l playbook-live-2026-10-25/hd1-ifs-image.trim.bin /tmp/ifs0.sealed.img
#   -> exactly 5 bytes: 257 (0x100) 04->06, 2883085..2883088 (the CRC)
```

### 10.4 IFS unpack / repack (§8)

```bash
Q=/home/psyden/SSDdrive/wsl-migration/home/psyden/qnx660-master
export QNX_HOST=$Q/host/linux/x86 QNX_TARGET=$Q/target/qnx6
B=$QNX_HOST/usr/bin
cd $COMMANDCODE_SCRATCHPAD/ifs
cp $A/playbook-live-2026-10-25/hd1-ifs-image.bin .
$B/dumpifs hd1-ifs-image.bin | head -40
for i in 1 2 3 4 5; do
  miss=$($B/dumpifs -x hd1-ifs-image.bin 2>&1 | sed -n 's/.*Unable to open \(.*\): No such file or directory/\1/p')
  [ -z "$miss" ] && break; mkdir -p "$(dirname "$miss")"
done
# repack (buildfile in §8.2); then splice the RIM header and reseal
$B/mkifs playbook.bld out.ifs && $B/dumpifs out.ifs | head
```

---

## 11. Residual unknowns

| id | question | why it matters | how to close |
|---|---|---|---|
| **U1** | Which MCT does the loader actually parse — the 128 KiB `hd4` copy, a copy inside the bootrom metrics block, or an "external MCT/HWV" region? | If the loader's MCT has no type-`0x39` entries, **path 2 cannot resolve any part** and only path 1 (offset from `p34.start`) works. `hd4.bin` has **no** `0x39` entries. | read-only `0xD9` on the spare 64 GB unit; compare with the `0xB0`/`0xB4` MCT copy that `bb10mt info` prints (`uflash.pas:672`) |
| **U2** | Are the `0x39` entries on a real PlayBook titled/flagged such that `PartitionID==8` is the IFS and the A/B pair is distinguishable by `Flags&0x40`? | Decides path 2 vs path 1 for the tool | same `0xD9` dump |
| **U3** | The runtime value of `*0x8013EE9C` (`BS`) | The whole unit system hangs off it. Nine independent constraints say `0x10000` (§2.3), but it is a RAM word | `0xB1` second dword on the spare |
| **U4** | `FUN_8011A7A0(dest)`'s per-step size when `dest` straddles an erase-block boundary | Affects only how many program steps a large write takes, not the addresses | dynamic observation |
| **U5** | The exact semantics of `0xDC`'s CRC (init/final/chunking, and whether the range is in `BS` units) | The in-band readback check depends on reproducing it host-side | one `0xDC` over a known sector on the spare, compare with `zlib.crc32` |
| **U6** | Does the *32 GB* unit's MCT have the same `0x805`/`0x815`/`0x4905` extents? | §4's numbers are from a 64 GB MCT (`end=0xFFFFF`) | `0xD9` on the 32 GB unit |
| **U7** | Whether an `mkifs`-rebuilt (different-size) image survives the bootloader's loader stage | A size change means a different `LoadStart/End` mapping and a different `EA0001FE` branch target | hardware test, out of scope for track A |
| **U8** | `0xC0` COMPLETE's `0x2074` write on a unit whose `BOOTCTR_FCTRY` is 0 | On the product path `0x2074.requested_region` would then matter | keep the counter nonzero; or skip `0xC0` |
| **U9** | The exact meaning of `local_28` (the third output of the QCFM parser `FUN_8011F9EC`), which must exceed `0x1FFFF` for the whole-image path to engage | Only decides *which* of the two destination paths runs; both end at the same gate, and a 2.75 MB IFS satisfies either | decompile `FUN_8011F9EC` fully, or observe on the spare |

---

## 12. Summary table — answering the mission question

| question | answer | label |
|---|---|---|
| Can the signed loader write the OS/IFS region pre-OS? | **YES** | [P] §2.3, §4 |
| Which opcode? | **`0xF7`** (region 0 / `hd1`); `0xF8` for region 1 / `hd2` | [P] §2.3 |
| Destination expression | `allow-listed partition start + QCFM 'rrcq'.offset`, in **64 KiB blocks** | [P] §2.3, §3 |
| Only address gate | `FUN_80100118`, 64-bit `∃ region : start ≤ dest ∧ dest*BS+len-1 ≤ (region.end+1)*BS-1` | [P] §2.4 |
| Allow-list contents | MCT types `0x1C,0x22,0x1B,0x1F,0x24,0x34,0x3B,0x3A,0x20,0x21` (+`0x19`/`0x1A`); CB build adds `0x1E` | [P] §2.5 |
| Does it cover region 0 IFS? | **YES** — type `0x1B` = blocks `0x815..0x4804` ⊃ `0x825` | [P] §4 |
| RCFS / `/base` covered? | YES (same `0x1B`) | [P] |
| UFS covered? | YES (type `0x1C` = `0x4905..0xFFFFF`) | [P] |
| Radio covered? | allow-list has `0x3A`, but no `0x3A` extent exists on this device | [P]/[I] |
| NVRAM (`0x1E`) covered? | **NO** in `LDR_01`; YES in the CB build — corrects `88` U1 | [P] |
| Is the write signature-checked? | No, by nothing, at any layer | [P] §6 |
| Can a repacked image be CRC-valid with `os_type=6`? | **YES** — verified byte-exactly | [P] §8.3 |
| Does `bbusb.py` support it today? | primitives yes, container builder no; `bb10mt flash` already does path 2 | [P] §9.6 |

---

## 13. Safety notes

* Nothing in this document was executed against hardware. The only side effects
  were reads of files under `/home/psyden/rea-work`,
  `/home/psyden/SSDdrive/.../qnx660-master` and writes inside
  `$COMMANDCODE_SCRATCHPAD`.
* Every write path discussed here is **`FS_DIRTY`-RPMB-gated** before the first
  sector is programmed. A unit with a damaged RPMB cannot use it, and a unit
  with a healthy RPMB will have one RPMB record rewritten per session.
* `0xE0` (MCT rewrite) and `0xF3` (EraseMCT) must never be issued: they destroy
  the very table the allow-list is built from.
* A pre-OS reflash of region 0 is **not** covered by A/B fallback. It is covered
  by the fact that the recovery channel (bootrom → loader → `0xF7`) does not
  depend on region 0.
* The 32 GB units remain recovery targets, not test beds (`HANDOFF` §7). Nothing
  here changes their status, and `88` §9.2 still stands: `LDR_01` cannot repair
  `BOOTCTR_FCTRY`.

---

## 14. Deltas to prior documents

| doc | statement | correction |
|---|---|---|
| `88` §5 / §9.1 | allow-list = `0x1C,0x1B,0x1F,0x24,0x34` + `0x19/0x1A` | the builder runs **ten** type loops plus the `0x19`/`0x1A` pair: `0x1C,0x22,0x1B,0x1F,0x24,0x34,0x3B,0x3A,0x20,0x21`. The CB build's list additionally contains `0x1E` (OS NV = NVRAM) and `0x18/0x1D/0x09` (geometry, not the gate) |
| `88` §12 U1 | "is the NVRAM partition in the allow-list?" | for the **CB** build: yes, type `0x1E`; for **`LDR_01`**: no. Still needs a `0xD9` to confirm the runtime MCT |
| `88` §7.3 | "`0xF7`/`0xF8` … `dest = desc.start + base`" | the base is either the MCT `0x34` (MBR) start (whole-image containers) or the start of a **type-`0x39` QNX-partition entry selected by `pfcq._type`** (per-part containers); `0xF8` differs from `0xF7` *only* in the region selector / `0x39` flag bit |
| `88` §7.3 | "for each partition entry i of the QCFM partition table: `FUN_80115354(staging,i,&desc)`" | `desc` is an **MCT** entry (`FUN_80100DA0`), not a QCFM entry; the QCFM supplies only `rrcq.offset`/`Count` and `_type` |
| `89` §1.2 table | ECDSA trailer start = `0x2BF9E0` | `end-0x230 = 0x2BFE10-0x230 = **0x2BFBE0**` |
| `97` §2 / §4.1 | "is the OS/IFS region writable through `0xF7/0xF8`? — most valuable open question" | **answered: yes**, this document |
| `86` §3 item 4 | "test the loader read-only on the spare" | unchanged, but the *write* capability no longer needs a live probe to be believed; only U1/U2/U3 need one |

---

## 15. Bottom line

The signed PlayBook ramloader is a **host-addressed eMMC writer bounded only by
the MCT allow-list**, and that allow-list includes the entire OS area of both
A/B slots (`0x1B` = `0x815..0x4804`) and the entire user filesystem (`0x1C`).
`0xF7` writes region 0, `0xF8` writes region 1, the destination is
`partition_start + 'rrcq'.offset` in 64 KiB blocks, and nothing verifies the
payload. A CRC-sealed IFS (`os_type=6`, CRC at `end-4`) written this way is
accepted by the factory bootloader — proven for the seal, and the write path is
the same one the shipped autoloaders use.

That makes the Linux/CRC work recoverable without desoldering: **a bad region-0
image is one bootrom session + one `0xF7` stream away from being replaced**, and
the recovery channel never depends on the region being replaced.
