# 20 — Thumb main flow and the "CH*" boot-configuration parser

Target: `omap4430-bootrom-0x40028000.bin` (base 0x40028000, 49152 bytes).
Method: LLVM-23 `llvm-objdump` (Thumb-2 and ARM, per-range mode) + Python word
reads. **All statements below are LLVM-derived observations, not REA Evidence.**
Everything here should be treated as "verified by disassembly", with the
explicit unknowns listed in §7.

> Update 2026-10-19: the `0x4002FEF0` "per-name offset/length table" hypothesis
> is definitively resolved in `61-rom-api-and-flash-tables.md`: the bytes are the
> middle/tail of the 46-record `{device-ID, geometry-index}` table at
> `0x4002FECF`, and `0x4002FEE0` is one byte into record 8. The ROM API table at
> `0x40028400` is also identified there (TRM Figure 27-6).

Companion notes: `METHOD.md` (tooling), `NOTES.md` (prior pass). This file
supersedes/extends NOTES §"Thumb main / boot flow" and §"Strings / boot image
names", and resolves NOTES open-question #2 and (partly) #1/#3.

---

## 1. Thumb main flow (0x40028A18 → return)

Entry 0x40028A18 (reached from the ARM startup trampoline 0x400288D0; see
NOTES). It is one function that (a) snapshots the reset reason, (b) brings up
the watchdog, (c) reads/prepares the boot-config flags, then (d) enters the
config parser and never comes back with a meaningful value.

Pseudo-flow (r5 = pointer to a small flag word on the stack, `sp+4`):

```
0x40028A18 push {r2,r3,r4,lr}
0x40028A1C mov  r0, sp            ; out slot for reset reason
0x40028A1E str  r4(=0),[sp,#4]    ; flags word = 0
0x40028A20 bl   0x4002A4C0        ; *sp = PRM_RSTST  (reset reason)
0x40028A24 progress(1)            ; 0x4002A51C(1)
0x40028A2E bl   0x40029AE6(6,1)   ; PRCM module #6 enable
0x40028A32 bl   0x4002A41C        ; WDT enable / sysconfig
0x40028A36 bl   0x4002A430        ; WDT start pass (0xAAAA/0x5555 -> WSPR)
0x40028A3C bl   0x4002A478(0x4002BF20) ; WDT load/timeout program
0x40028A40 bl   0x4002A454        ; WDT stop pass (0xBBBB/0x4444 -> WSPR)
0x40028A44 if *sp == 1:           ; cold / power-on style reset
0x40028A4C     progress(2)
0x40028A54     flags |= 1
0x40028A5C     *(0x4A326A00) = 0  ; discard stale boot-config pointer
0x40028A5E bl   0x40028DB6(sp+4)  ; flags prep / config source
0x40028A64 bl   0x40028A7C(sp+4, (u8)*sp)  ; *** the config parser ***
0x40028A70 return 0
```

Literal pool at 0x40028A74/0x40028A78 (both loaded by this function):

| literal | value | use |
|---|---|---|
| 0x40028A74 | `0x0002BF20` (=0x4002BF20) | arg to 0x4002A478 |
| 0x40028A78 | `0x4A326A00` | cleared on reset==1 |

### 1.1 Callees

**0x4002A4C0** — reset-reason reader. `if r0==0 return 6; else *r0 = *(0x4A307000+0xB04); return 0`.
The word read is `PRM_RSTST` for the device (0x4A307B04 per NOTES). The low
byte is what the parser receives as its second argument.

**0x4002A4D8** — reset-reason *descriptor* init (called from the ARM reset path,
not from main; confirmed by literal layout). It builds a 3-pointer descriptor
in IRAM using literals 0x4002A558 = `0x4030D040` and 0x4002A55C = `0x4030D060`:
stores `{0x4030D040, 0x4030D04C, 0x4030D058}` at 0x4030D060, then (if
`r0==1`) zeroes the 0x4030D04C record, else copies the previous record; finally
writes the reset reason to `*(0x4030D058)`.

**0x4002A51C** — boot-progress bitmap setter. `p = *(0x4030D060)` (=0x4030D040);
sets bit `r0` (0..0x5F) in the 3-word bitmap `p[0..2]`; returns 0, or 1 if
`r0 >= 0x60`. This is the **boot-progress / trace vector** in IRAM at
0x4030D040. Milestones observed in this subsystem: bits 1,2 (main), 3,4,5,6,8,9
(parser), 0x1D (0x40028DB6), 0x41–0x4B/0x56 (media handler), 0x0B–0x12/0x1E
(image handler), 0x14, 0x31.

**0x40029AE6(cmd, arg)** — PRCM **module clock-enable dispatcher**. `tbb [pc,r2]`
on `cmd` (0..9; `cmd>=0xA` returns 1). Each case performs the OMAP
"enable + IDLEST handshake" pattern on a CM/PRM block:
`reg = [base+off]; reg = (reg & ~3) + 2; [base+off] = reg; while ([base+off] & 0x30000) ;`
Bases seen as literals: 0x4A008000, 0x4A008B00, 0x4A009000, 0x4A307840,
0x4A0022C4-relative. Main uses `cmd=6`; 0x40028D4E uses `cmd=5`. So
`0x40029AE6(6,1)` = "enable clock module #6 before touching the watchdog".

**0x4002A41C / 0x4002A430 / 0x4002A454 / 0x4002A478** — all operate on base
`[0x4002A4B8] = 0x4A314000`, which is the **watchdog timer (WDTIMER) block**.
Evidence for the WDT identification: offset **0x48** receives the classic
OMAP start/stop magic pairs and offset **0x34** is the polled write-posting
status (WDT_WWPS):

| fn | accesses | effect |
|---|---|---|
| 0x4002A41C | `[+0x10] |= 2`; poll `[+0x14]` bit0 | WDT sysconfig/clock-enable handshake |
| 0x4002A430 | poll `[+0x34]==0`; `[+0x48]=0xAAAA`; poll `[+0x34]` bit5; `[+0x48]=0x5555`; poll | WDT **start** sequence |
| 0x4002A454 | same but `0xBBBB`, `0x4444` | WDT **stop** sequence |
| 0x4002A478 | poll `[+0x34]`; `[+0x24] \|= 0x20`; compute value; write `[+0x2C]` and `[+0x30]` | program counter/reload (timeout) |

0x4002A478 computes `v = -(high32(0xC49BA5E2 * r0) + (r0<<5))` with
r0 = 0x4002BF20. The multiplier 0xC49BA5E2 ≈ 2^32/1.3021 makes the expression
≈ `-(r0 * 32.768)`, i.e. a **32.768 kHz (RTC-tick) count converted to a
down-counter load**. The exact start/stop ordering (start → program → stop) is
as observed; the magic-pair assignment to *start* vs *stop* follows the
standard OMAP WSPR convention but should be confirmed against the TRM
(see §7).

**0x40028DB6** — boot-config source selection.
```
if (flags & 1) == 0:
    if 0x4002B64E(2, buf) == 0:     ; read stored config block of type 2
        flags |= 4; progress(0x1D)
else:
    0x40028D4E(buf+0x64);           ; probe/classify boot media
    0x40029F26(*(buf+0x64));
    0x40029CD0(buf, *(buf+0x64))
0x40028C44(buf)                     ; dispatch on *(buf)
```
It also calls **0x40028D4E** ("probe boot media"): enable module 5, call
0x4002B6C0(0) and 0x4002B6DC(0,&sz), then classify the returned value against
thresholds 0x3D6 / 0x2B4 / 0x226 / 0x1B8 → result 7/5/4/3/1, stored to
`*out`. So the ROM measures/queries the boot media and gives it a class index.

### 1.2 Config-block reader 0x4002B64E(type, out)

Walks a list of configuration blocks searching by tag:
```
p = *(0x4A326A00)                       ; list base pointer register
while 0x4A326A00 < p < 0x4A327000:
    w = read_unaligned_u32(p)           ; helper 0x4002F2B0
    if (w >> 8) == 0x00CF00AA and (u8)w == type:
        len = read_unaligned_u32(p+4)
        if p+8+len <= 0x4A327000: copy len/4 words from p+8 to out; return 0
    p += 8 + read_unaligned_u32(p+4)
return 1
```
Magic: a 32-bit header `0xCF00AA | (type<<0)`, i.e. the three signature bytes
`CF 00 AA` followed by a type byte. Types requested in this subsystem: 1, 2, 3.
This is the mechanism that supplies the parsed bytes/config words, and it
explains why main clears `*(0x4A326A00)` on a cold reset (drop a stale list).

---

## 2. The config parser 0x40028A7C

Signature: `parser(flags_ptr /*r0*/, reset_reason_byte /*r1*/)`.

It allocates 0x20 bytes of stack (`sp+4..sp+0x23`) and uses:
* `sp+8`  — scratch read by the command-table builder (0x40028E72)
* `sp+0x14` — current-command result slot (initialised to 0xFFFFFFFF)
* `sp+0x18` — **the 8-byte command table** (source of the command loop)
* `r5` = flags_ptr, `r10` = `struct+0x64` (result base), `r7` = `sp+0x18`

High-level structure:

```
progress(3)
0x4002979C(&sp+4)                 ; device-type query -> value 2 or 3
if value == 2 and 0x400297D8() != 0: 0x40028FAC(struct)
; ---- decide command table source ----
if (flags & 1) == 0:
    0x4002B64E(1, sp+8)           ; config block type 1
    if ok: progress(0x1C); flags |= 0x200;
           scatter bits 8,0,1,2,3,4 of *(sp+8) into flags bits 3..8
0x40028E72(flags, sp+0x18, sp+8)  ; build the 8-byte command table
print_reset_reason:   ; struct[0x64+9] = reset_reason_byte ; struct[0x64+0xA]=0

outer_loop:
    *(u32*)(struct+0x64) = 0xFFFFFFFF      ; 0x4002F2D0 write-u32
    struct[0x64+0x8] = 0
    r4 = 0
  inner_loop:  ; over the 8 command bytes
    c = table[r4]
    if 1 <= c <= 8:                 ; media command
        progress(4); 0x4002AD5C(flags, c, struct+0x64)
    elif 0x41 <= c <= 0x47:         ; image command 'A'..'G'
        progress(5); 0x4002A430(); *sp+0x14 = 0xFFFFFFFF
        0x4002AFFE(flags, c, &sp+0x14, struct+0x64)
        0x4002A454()
        res = *sp+0x14
        if res != 0xFFFFFFFF and res != 0xF0030003:
            write_u32(struct+0x64, res)          ; 0x4002F2D0
            if (res & ~0xFF00) == 0xF0030006:    ; 0x40028BFC + 3
                progress(9); table[r4+1] = res>>8
            elif res != 0xF0030002:
                progress(8); clear all table bytes >= 0x40
    r4++; if r4 < 8: inner_loop
    progress(6)
    goto outer_loop                     ; **never terminates**
```

Key semantics / the exact command encoding:

* The parser is a **persistent polling loop**: each outer iteration clears
  `struct+0x64` (to 0xFFFFFFFF / 0) and re-processes all 8 command bytes, so
  the "CH" table can be updated on the fly by the media handler.
* A command byte is encoded as: **1..8** = "media/source command" (passed to
  0x4002AD5C). **0x41..0x47 = ASCII 'A'..'G'** = "named image command"
  (passed to 0x4002AFFE). Anything else is ignored.
* The 8-byte table is *mutable*: on one result path the parser writes
  `table[r4+1] = res>>8` and on another it zeroes every entry `>= 0x40`.
  Command bytes are therefore consumed/patched in place.
* Result store: `struct+0x64` (word, via 0x4002F2D0) and `struct[0x64+8]`
  (byte, command index, written by 0x4002AD5C). `struct[0x64+9]` holds the
  reset reason. Magic result codes observed: 0xFFFFFFFF (invalid/absent),
  0xF0030002, 0xF0030003, and the family `(res & ~0xFF00) == 0xF0030006`
  (constant 0x40028BFC = 0xF0030003, used as ±1 and +3).

### 2.1 Command-table builder 0x40028E72(flags, dst=sp+0x18, src=sp+8)

* Zero/clear 8 bytes at `dst` (helper 0x4002F1A0).
* If `flags & 0x200` (bit 9): copy four 16-bit values from `src+2..src+9`
  straight into `dst` — i.e. the command list comes from the stored config
  block.
* Else: `t = 0x40029794()` (device-type low byte); `0x4002A560(&p, t & 0x1F)`
  → `p = [0x4002A57C] + (t&0x1F)*16`; then read 8 bytes from that per-device-type
  table entry, gated by `flags` bits (`&0x8000`, bit0) and writing two bytes per
  iteration (`[p+index*2]` or `[p+8+index*2]`).
  Base literal 0x4002A57C = `0x0002F7C4`.

---

## 3. Handler functions

### 3.1 Media handler 0x4002AD5C(flags, cmd /*1..8*/, struct+0x64)

Also calls the PRCM enabler and WDT helpers indirectly. Steps:

1. `0x4002979C(&sp+0x14)` → device type (abort if != 0).
2. `0x40028E2A(&sp+0x23C, cmd)` → `*sp+0x23C` = pointer to a **3-word
   media-op vector** at `[0x40028F00] + cmd*12` = `0x4002F578 + cmd*12`
   (literal 0x40028F00 = `0x0002F578`). Vectors:
   * cmd1 = cmd2 = {0x4002B809, 0x4002B855, 0x4002B86F}
   * cmd3 = {0x4002B879, 0x4002B933, 0x4002B963}
   * cmd4 = {0x4002B9A9, 0x4002BA03, 0x4002BA4D}
   * cmd5 = cmd6 = cmd7 = {0x4002BA55, 0x4002BB1B, 0x4002BBD1}
   * cmd8 = {0x4002BC11, 0x4002BC59, 0x4002BC73}
3. `0x40028DFC(&sp+0x234)` → `0x4030D238` (=struct+0x98);
   `0x40028E1A(&sp+0x238)` → `0x4030D258` (=struct+0xB8).
4. `progress((u8)[0x4002AE54 + cmd])` where 0x4002AE54 = `0x0002F9C4`; the byte
   table there maps cmd1..8 → progress bits 0x41,0x42,0x43,0x44,0x45,0x47,
   0x4B,0x4A.
5. Initialises the media descriptor at `struct+0x98` (fields [0],[4]=cmd,
   [8],[0xC]=u16 count,[0x10],[0x14]/[0x16]=u16s,[0x18]=struct+0xB8,
   [0x1C]=flags) and writes `write_u32(struct+0x68, ...)`,
   `struct[0x6C] = cmd`.
6. Loop `for r5 = 1 .. descriptor[+5]`: calls `vector[0]` (open) with the
   descriptor, then performs media I/O through `vector[1]`, matching records
   with **0x4002C254** (name matcher) and **0x4002C3D8**, and dispatches to
   **0x4002ACD6 / 0x4002AC64 / 0x4002AB44 / 0x4002A9EC**.
7. On completion it stores the resulting record into `struct+0x64`
   (via 0x4002F2D0) and returns 1.

**Inputs**: flags word, command byte 1..8, `struct+0x64` destination.
**Outputs**: `struct+0x64` (word), `struct[0x64+8]` (byte=cmd), plus side
effects in `struct+0x70/+0x98/+0xB8`; progress bits 0x41–0x4B.

### 3.2 Image handler 0x4002AFFE(flags, cmd /*'A'..'F'*/, out_slot, struct+0x64)

1. `0x4002979C(&sp+0xC)` → device type.
2. `0x40028E4A(&sp+0x18, cmd)` → `*sp+0x18` = pointer to a **5-word image-op
   vector** at `0x4002F5E4 + (cmd-0x40)*20` (via literal 0x40028F00 +
   0x6C). The 20-byte slot 0x4002F5E4 (index 0x40) is all-zero.
   * 'A','B','C','D' (0x41–0x44) → {0x4002BD5D, 0x4002BF21, 0x4002BEC3,
     0x4002BF5F, 0x4002BFC9}
   * 'E','F' (0x45–0x46) → {0x4002C03F, 0x4002C1D3, 0x4002C1A3, 0x4002BFD5,
     0x4002C205}
   (`0x40028E4A` accepts 0x41..0x46; 'G'=0x47 is rejected.)
3. `0x40028E0A(&sp+0x14)` → `0x4030D210` (=struct+0x70).
4. Load address `r7 = 0x40300000` (or `0x40304000` when device-type==3)
   — literals 0x4002B248 / 0x4002B24C. This is the **image load target in
   IRAM**.
5. `0x4002C808(cmd, [0x4002B250])` (0x4002B250 = 0x4030DB8C) — name/record
   setup.
6. Programs the descriptor at `struct+0x70` (fields [0x18]=cmd, [0x10]=arg,
   [0x14]=1, [0x24]=0x18000 hard-coded, or 0x2666 for 'E'/'F' (0x45/0x46),
   optionally overridden by fields from the config word at `sp+4`).
7. Runs the 5-word vector: `vector[0]` (setup), `vector[1]` (I/O),
   optionally `vector[2]`, then `0x4002AFC4` / `0x4002AF42` / `0x4002AEE0`
   to fetch, and finally `0x4002F2D0` copies the computed value into
   `struct+0x64` (r8).
8. Returns 0 on success, 1 on failure.

**Inputs**: flags, command letter, out slot (parser's `sp+0x14`), struct+0x64.
**Outputs**: `struct+0x64` (word) and `*out_slot` (word) — the "result" the
parser then compares against the 0xF00300xx codes. `struct+0x70/+0x98`
sub-records, plus progress bits 0x0B–0x12/0x1E/0x0A/0x0F.

### 3.3 Accessor helpers (the small stubs after 0x40028DFC)

| stub | behaviour |
|---|---|
| 0x40028DFC | `*r0 = 0x4030D238` (struct+0x98); 6 if r0==0 |
| 0x40028E0A | `*r0 = 0x4030D210` (struct+0x70) |
| 0x40028E1A | `*r0 = 0x4030D258` (struct+0xB8) |
| 0x40028E2A | `idx` 1..8 → `*r0 = 0x4002F578 + idx*12`; else return 1 |
| 0x40028E4A | `idx` 0x41..0x46 → `*r0 = 0x4002F5E4 + (idx-0x40)*20`; else 1 |

### 3.4 Supporting string/record helpers (called by both handlers)

* **0x4002C254(mask, record, out_bitmap)** — for each of 16 records of 0x20
  bytes (name field at record+0x14) compares against the 14 known names in
  the table at 0x4002FA84 (literal at 0x4002C418 = `0x0002FA84`); builds a
  bitmap of matches and, when `mask` bit is clear, sets the name byte to 0.
* **0x4002C3D8 / 0x4002C352 / 0x4002C638 / 0x4002C6C0 / 0x4002C808** —
  string/record utilities around the same 0x20-byte records.

---

## 4. Strings and tables

### 4.1 String block at 0x4002FF2C

Exact C-string offsets (from the raw image):

| address | text |
|---|---|
| 0x4002FF2D | `R&D` |
| 0x4002FF31 | `2ND` |
| 0x4002FF35 | `CHMMCSD` |
| 0x4002FF3D | `CH` |
| 0x4002FF40 | `CHFLASH` |
| 0x4002FF48 | `CHRAM` |
| 0x4002FF4E | `HLO` |
| 0x4002FF52 | `MLO` |
| 0x4002FF56 | `ULO` |
| 0x4002FF5A | `PRIMAPP` |
| 0x4002FF62 | `X-LOADER` |
| 0x4002FF6B | `CHSETTINGS` |
| 0x4002FF76 | `KEYS` |
| 0x4002FF7B | `ISSW` |

(NOTES' list was off by one for several entries; the above is byte-exact.
Note `HLO`/`MLO`/`ULO` appear before `PRIMAPP` — the OMAP boot-image names.)

### 4.2 The real name-pointer table: 0x4002FA84

The strings are referenced by a **14-entry array of 32-bit pointers** at
0x4002FA84, terminated by 0 at 0x4002FABC, and used by 0x4002C254 via the
literal at 0x4002C418 (=`0x0002FA84`):

| slot | ptr | name |
|---|---|---|
| 0 | 0x0002FF7B | ISSW |
| 1 | 0x0002FF62 | X-LOADER |
| 2 | 0x0002FF31 | 2ND |
| 3 | 0x0002FF56 | ULO |
| 4 | 0x0002FF52 | MLO |
| 5 | 0x0002FF4E | HLO |
| 6 | 0x0002FF76 | KEYS |
| 7 | 0x0002FF5A | PRIMAPP |
| 8 | 0x0002FF2D | R&D |
| 9 | 0x0002FF3D | CH |
| 10 | 0x0002FF6B | CHSETTINGS |
| 11 | 0x0002FF48 | CHRAM |
| 12 | 0x0002FF40 | CHFLASH |
| 13 | 0x0002FF35 | CHMMCSD |

So "CH…" names are `CH` (base), `CHMMCSD` (MMC/SD source), `CHFLASH`,
`CHRAM`, and `CHSETTINGS` (settings block); these are the **config-header
name tags** the parser/matcher recognise inside boot-media records, alongside
the boot-image names (MLO/HLO/ULO/2ND/PRIMAPP/X-LOADER/ISSW/KEYS/R&D).

### 4.3 The 16-bit table at 0x4002FEF0–0x4002FF2A — hypothesis tested and rejected

The 22 halfwords immediately preceding the strings are:

```
C201 F102 C101 A102 B101 DA02 CA03 AA04 BA03 DC04 CC03
AC04 BC03 D304 C305 A306 B305 D506 C507 A508 B507 D708
C707 A708 B707 DE08 CE07 AE08 BE08 0008
```

Test of the "per-name offset/length" hypothesis:
* Name offsets from a common base would be small (<0x30); these values are
  0xC2..0xDE in the high byte — far too large to be offsets or lengths into
  the 0x4002FF2D.. string block.
* No code in the image references 0x4002FEF0 (or any address in
  0x4002FEE0–0x4002FF2A) as a data pointer (full word-scan: zero "word/
  halfword/alias" hits).
* The region disassembles as incoherent Thumb, consistent with raw data, and
  its low bytes cycle 1..8 (0x01,0x02,…,0x08) while the high bytes cycle
  through 0xA1–0xF1 — a shape suggesting *encoded records* (possibly 8-bit
  index + 8-bit tag) rather than offsets.

**Conclusion: rejected.** The genuine per-name indirection is the 32-bit
pointer array at **0x4002FA84** (§4.2). The 0x4002FEF0 table is an
as-yet-unidentified data table (candidate: a switch/dispatch or
device-parameter table belonging to the §5 media machinery, since 0x4002FECF
is referenced from 0x4002F000 and 0x4002FE3C/0x4002FE20 from 0x4002EBB4/
0x4002E048/0x4002E0A4).

### 4.4 Other tables used by this subsystem

* 0x4002F578 — media-op vector base (cmd1..8, stride 12).
* 0x4002F5E4 — image-op vector base ('A'..'F', stride 20).
* 0x4002F9C4 — byte table: media cmd → boot-progress bit (0x40..0x4B,0x56).
* 0x4002F7C4 — 16-byte-per-entry device-type table (entries
  `{0x4580nn, 0, 0x8000nn45, 0}`, nn = 1,2,3,5,6,8).
* 0x4002F9EC — controller-base table used by 0x4002B6C0/0x4002B6DC
  (0x4A318000, 0x48032000, 0x48086000, …).

---

## 5. Structure base 0x40028BF8 (+0x64)

The literal at **0x40028BF8 = 0x4030D1A0**, an **IRAM/SRAM address**. It is
referenced by exactly three words in the image: 0x40028BF8 (the parser),
0x4002C918, and 0x4002FFB4 (the bootstrap copy table). 0x4002FFB4 is the
destination field of bootstrap record #4 from NOTES:

```
{0x000300A0, 0x4030D1A0, 0xABC, 0x00028898}   ; memset(dst=0x4030D1A0, len=0xABC)
```

So 0x4030D1A0 is a **0xABC-byte scratch structure in IRAM that the ROM
bootstrap zeroes on every boot**. Its sub-fields are the ones the config code
uses, all confirmed as `base + fixed offset`:

| offset | address | role |
|---|---|---|
| +0x64 | 0x4030D204 | **current-command result record** |
| +0x70 | 0x4030D210 | image-handler descriptor |
| +0x98 | 0x4030D238 | media-handler descriptor |
| +0xB8 | 0x4030D258 | media sub-record |
| end | 0x4030DC5C | (base+0xABC) |

**The +0x64 area** (0x4030D204) is a small "last command result" record:

| offset | bytes | written by |
|---|---|---|
| +0x64 | u32 | reset to 0xFFFFFFFF each outer pass; set by handlers via 0x4002F2D0 |
| +0x68 | u32 | 0x4002AD5C (write_u32) |
| +0x6C | u8  | command index (cleared each pass; set by 0x4002AD5C) |
| +0x6D | u8  | reset-reason byte |
| +0x6E | u8  | cleared |

A separate, related structure lives immediately after the vector table at
0x4030D040/0x4030D060 (the boot-progress bitmap and the reset-reason
descriptor from §1.1) — distinct from 0x4030D1A0.

---

## 6. Evidence list

All from LLVM-23 `llvm-objdump` (Thumb-2 unless noted) + Python `struct` reads
of `omap4430-bootrom-0x40028000.bin`:

* Main: 0x40028A18–0x40028A7A; literals 0x40028A74=`0x0002BF20`,
  0x40028A78=`0x4A326A00`.
* Reset reason: 0x4002A4C0–0x4002A4D6; 0x4002A4D8–0x4002A51A; literals
  0x4002A558=`0x4030D040`, 0x4002A55C=`0x4030D060`, 0x4002A4D4=`0x4A307000`.
* Progress bitmap: 0x4002A51C–0x4002A554.
* WDT block: 0x4002A41C–0x4002A4B4; literals 0x4002A4B8=`0x4A314000`,
  0x4002A4BC=`0xC49BA5E2`; handlers 0x4002A430, 0x4002A454, 0x4002A478.
* Parser: 0x40028A7C–0x40028BF6; literal 0x40028BF8=`0x4030D1A0`,
  0x40028BFC=`0xF0030003`.
* Table builder: 0x40028E72–0x40028EF8; accessor stubs 0x40028DFC–0x40028E70;
  literals 0x40028EFC=`0x4030D238`, 0x40028F00=`0x0002F578`.
* Handlers: 0x4002AD5C–0x4002AEDE; 0x4002AFFE–0x4002B23E;
  literals 0x4002AE54=`0x0002F9C4`, 0x4002B240=`0x4A304000`,
  0x4002B244=`0x0002F9D0`, 0x4002B248=`0x40300000`, 0x4002B24C=`0x40304000`,
  0x4002B250=`0x4030DB8C`, 0x4002B254=`0x0FFCFFFE`, 0x4002B258=`0x4030BFFF`.
* Config-block reader: 0x4002B64E–0x4002B6B2; literals 0x4002B6B4=`0x4A326A00`,
  0x4002B6B8=`0x4A327000`, 0x4002B6BC=`0x00CF00AA`.
* Media probe: 0x40028D4E–0x40028DB4; 0x4002B6C0/0x4002B6DC; table 0x4002F9EC.
* PRCM dispatcher: 0x40029AE6–0x40029AE4 (tbb table at 0x40029AF2),
  0x400297F0–0x40029A5C; literals 0x40029BE4=`0x4A008B00`,
  0x40029BE8=`0x4A008000`, 0x40029BF4=`0x4A009000`, 0x40029BFC=`0x4A307840`.
* Strings and tables: 0x4002FF2D–0x4002FF7E; name-pointer table
  0x4002FA84–0x4002FABC; 16-bit table 0x4002FEF0–0x4002FF2A;
  0x4002F578, 0x4002F5E4, 0x4002F7C4, 0x4002F9C4, 0x4002F9EC, 0x4002FE20.
* Bootstrap copy table (from NOTES, re-read): 0x4002FF80–0x4002FFBF;
  record 4 dst 0x4030D1A0 len 0xABC.
* String matcher: 0x4002C254–0x4002C2CA; literal 0x4002C418=`0x0002FA84`.
* Global word-scan for references to 0x4030D1A0 → {0x40028BF8, 0x4002C918,
  0x4002FFB4}; scan of 0x4002FEE0–0x4002FF2A → no references (supports §4.3).

---

## 7. Unknowns / next passes

1. **Exact peripheral identity of 0x4A314000.** Register map strongly matches
   the OMAP watchdog (WSPR magic pairs at +0x48, write-posting status at
   +0x34), but +0x10/+0x14/+0x24/+0x2C/+0x30 should be matched to the
   OMAP4430 WDT TRM definition to confirm, and to decide which of
   0x4002A430/0x4002A454 is start vs stop (main calls them start→program→stop).
2. **0x4A326A00 / region 0x4A326A00–0x4A327000.** Holds a pointer to the
   boot-config block list searched by 0x4002B64E. Which hardware block this
   window is (SARM/scratch/register) is unconfirmed; the block format is
   `{u32 hdr = 0xCF00AA|type, u32 len, data…}` with 4-byte-granular payloads.
3. **0x4002FEF0–0x4002FF2A table.** Field layout / consumer unknown. It is
   *not* a per-name offset/length table (§4.3). Candidate consumers:
   0x4002E048/0x4002E0A4/0x4002EBB4 (media init) via 0x4002FE20/0x4002FE3C.
4. **0x40028E72 else-branch** (per-device-type command-table build via
   0x4002A560 / 0x4002F7C4) is only partially decoded; the flag-bit gating of
   the 8 output bytes needs a closer pass.
5. **Semantics of the 0xF00300xx result codes** returned into `struct+0x64`
   (0xF0030002/3, and the masked family 0xF0030006): only the control-flow
   branches are mapped, not the meaning of the codes.
6. **0x4002BF20** (arg to 0x4002A478 and the address of a real function,
   0x4002BF21, which is also vector[1] for image commands 'A'–'D'). Whether
   it is a size, a seed or an image entry point is unresolved.
7. **0x4002F9EC controller-base table** (0x4A318000/0x48032000/0x48086000)
   and the media-classification thresholds in 0x40028D4E are not yet mapped
   to concrete OMAP4430 peripherals.
