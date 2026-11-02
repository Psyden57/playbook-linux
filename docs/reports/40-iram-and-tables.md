# 40 - Bootstrap copy table, IRAM handoff image, ROM data region 0x4002FF80-0x40030B60

Target: `omap4430-bootrom-0x40028000.bin`, base `0x40028000`, 49152 B, SHA-256
`fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`.

All observations below are **LLVM-derived** (LLVM 23 `llvm-objdump` / `llvm-mc`) plus
Python `struct` reads over the raw dump. No REA native provider was used (see
`METHOD.md`). Everything here is *data derived from the target* and is treated as
analysis input only.

---

## 1. The 4-record copy table at 0x4002FF80

Occupies exactly `0x4002FF80..0x4002FFBF` (4 records x 0x10). Each record is
`{arg0, arg1, size, fn}` (little-endian):

| # | arg0 (src/a) | arg1 (dst/b) | size | fn | meaning |
|---|--------------|--------------|------|----|---------|
| 0 | 0x0002FFC0 | 0x4030D000 | 0x40 | 0x00028870 | **memcpy** vector table -> IRAM |
| 1 | 0x00030000 | 0x4030D100 | 0x08 | 0x00028870 | **memcpy** 8 zero bytes -> IRAM |
| 2 | 0x00030008 | 0x4030D108 | 0x98 | 0x00028870 | **memcpy** USB descriptor template -> IRAM |
| 3 | 0x000300A0 | 0x4030D1A0 | 0xABC | 0x00028898 | **memset** (dst=arg1, len=size) -> zero 0xABC B |

`fn 0x00028870` uses `r0=src, r1=dst, r2=len` (register-confirmed): it is
`memcpy` (`ldmhs r0!,{r3-r6}` / `stmhs r1!,{r3-r6}`, 16-B block then remainder).

`fn 0x00028898` writes only `r1` (dst) with `r2` (len), zeroing from r3-r6=0:
it is `memset`. **arg0 (0x000300A0) is not read**. The 4th record is therefore the
only memset; records 0-2 are memcpy.

### Walker at 0x4002882C (ARM)

Entry is `b 0x4002882C` from the reset stub. There are two adjacent entry bytes:

- `0x4002882C: bl 0x40028834` (real body)
- `0x40028834: add r0, pc, #44` -> r0 = 0x40028868 (literal pool)
- `0x40028838: ldm r0, {r10, r11}` -> r10 = 0x00007718, r11 = 0x00007758
- `0x4002883C/40: add r10,r10,r0` / `add r11,r11,r0` -> **r10 = 0x4002FF80 (table
  start), r11 = 0x4002FFC0 (table end)**
- `0x40028844: sub r7, r10, #1` (the pointer-veneer base)
- loop `0x40028848..0x40028864`:
  - `cmp r10,r11` ; when equal `bl 0x400288D0` (leaves the walker, never returns)
  - `ldm r10!, {r0,r1,r2,r3}` -> the 4 record fields
  - `sub lr, pc, #24` -> lr = 0x40028848 (loop head, so each callee returns to the loop)
  - `tst r3, #1` ; `subne pc, r7, r3` ; `bx r3`
    -> **`bx r3` is the actual call** (works for ARM and Thumb pointers).
    With the four in-ROM entries (`bit0 = 0`, ARM) the `subne` thumb-veneer path is
    not taken. The exact arithmetic of that veneer (`pc = r7 - r3`) is *not*
    resolved for real thumb entries (see Unknowns).

So the walker is a generic `{src,dst,len,fn}` dispatcher; only the record size
field drives the operation.

### What lands where in IRAM (definitive)

| IRAM range | bytes | source | contents |
|------------|-------|--------|----------|
| 0x4030D000-0x4030D03F | 0x40 | 0x4002FFC0 | 8-slot ARM vector table (`ldr pc` x8 + 8 target words) |
| 0x4030D100-0x4030D107 | 0x08 | 0x40030000 | 8 **zero** bytes (IRQ-mode `{r6,r7}` save / mini stack) |
| 0x4030D108-0x4030D19F | 0x98 | 0x40030008 | USB descriptor template (see section 3) |
| 0x4030D1A0-0x4030DC5B | 0xABC | **memset** | zeroed BSS / scratch (IRQ handler table, boot config struct) |

The gap `0x4030D040-0x4030D0FF` is **not** initialised by the table; the ROM uses it
as globals (see IRAM layout below).

---

## 2. IRAM layout (from literal xref scan of the whole image)

A full little-endian word scan for values in `[0x4030D000,0x4030DC5C)` gives the
following in-image users (rom address of the literal -> IRAM address):

```
0x40028900 -> 0x4030D000   initial SP (ldr sp,[pc,#0x28] at 0x400288D0)
0x40028A04 -> 0x4030D000   (2nd copy of the SP constant)
0x4030D004..0x4030D01C     first vector table pool (0x40028024..0x4002803C)
0x4002A558 -> 0x4030D040   descriptor ring base
0x4002A55C -> 0x4030D060   descriptor ring control block
0x4030D100/0x4030D104      IRQ-mode stack/save (0x40029474 / 0x40029470)
0x4002DB9C -> 0x4030D108   USB descriptor base (byte setters)
0x4002DB18 -> 0x4030D10C
0x4002D2AC -> 0x4030D10E   = device descriptor (offset +6)
0x40028BF8 -> 0x4030D1A0   boot-config struct (parser 0x40028A7C)
0x4002C918 -> 0x4030D1A0
0x40028EFC -> 0x4030D238
0x40029410 -> 0x4030DB08   IRQ dispatch table base (GIC handler)
0x4002978C -> 0x4030DB4C
0x4002B250 -> 0x4030DB8C
0x4002BFCC -> 0x4030DBE0
0x4002C244/0x4002D2BC/0x4002D6BC/0x4002DB2C -> 0x4030DBEC
0x4002CEA8 -> 0x4030DC1C
0x4002C240/0x4002D2B4/0x4002DB10 -> 0x4030DC38
0x4002D6C0 -> 0x4030DC48
```

Working layout:

```
0x4030D000-0x4030D03F  vector table (copied)
0x4030D040             descriptor ring node A   (init at 0x4002A4D8)
0x4030D04C             descriptor ring node B
0x4030D058             descriptor ring node C
0x4030D060             ring control block {ptr=0x4030D040, +4=0x4030D04C, +8=0x4030D058}
0x4030D100-0x4030D107  8 zero bytes: IRQ-mode {r6,r7} save / stack base
0x4030D108-0x4030D19F  USB descriptor template
0x4030D1A0             boot-config struct  (parser writes fields here)
0x4030D238             config-related global
0x4030DB08             GIC IRQ software dispatch table (zero -> entries registered at run time)
0x4030DB4C..0x4030DC48 misc runtime globals / handler slots
```

The init function at **0x4002A4D8** (Thumb) loads literals `0x4030D040` and
`0x4030D060`, builds a 3-node linked ring (`0x4030D040 -> +0xc -> +0x18`) and stores
the ring pointer into the control word at 0x4030D060: a small runtime log/queue.

---

## 3. Data region parsing

### 3a. 8-byte header at 0x40030000

`00 00 00 00 00 00 00 00` - **eight zero bytes**. Copied to 0x4030D100. Most likely
reserved/null padding (or a "size=0" placeholder); no code reads it as a header.
*Guess:* it exists so the IRQ save area at 0x4030D100 is pre-zeroed by the memcpy
(complementing the memset of the BSS).

### 3b. 0x98 bytes at 0x40030008 (-> IRAM 0x4030D108)

This is **not 4 x 0x20 records**. The natural parse is a **USB descriptor
template**: `6-byte prefix + 18-byte Device descriptor + 4 x 32-byte
Configuration descriptor blocks` = 6+18+128 = 0x98. The config headers sit at
offsets 0x18/0x38/0x58/0x78 (every 0x20), which is why a naive 0x20 slice starting
at 0x40030008 appears "shifted by one byte".

Prefix (0x40030008..0x4003000D) = `c0 32 00 00 01 00`.
Device descriptor (0x4003000E..0x4003001F):

```
12 01 | 10 02 | 00 | 00 | 00 | 40 | 00 00 | 00 00 | 00 00 | 00 | 00 | 00 | 01
bLength=18  bType=1(DEVICE)  bcdUSB=0x0210  class=0 sub=0 proto=0
bMaxPacketSize0=64  idVendor=0x0000  idProduct=0x0000  bcdDevice=0x0000
iManufacturer=0 iProduct=0 iSerial=0  bNumConfigurations=1
```

Four configuration blocks (each `CONFIG(9) + INTERFACE(9) + EP(7) + EP(7) = 32`):

| blk | off | bType | EP maxpacket | IRAM addr |
|-----|-----|-------|--------------|-----------|
| 0 | 0x18 | 2 CONFIGURATION | 0x0200 (512) | 0x4030D120 |
| 1 | 0x38 | 2 CONFIGURATION | 0x0040 (64) | 0x4030D140 |
| 2 | 0x58 | 7 OTHER_SPEED_CONFIG | 0x0040 (64) | 0x4030D160 |
| 3 | 0x78 | 7 OTHER_SPEED_CONFIG | 0x0200 (512) | 0x4030D180 |

Each block: config `09 02 20 00 01 01 01 c0 32` (wTotalLength=0x20, 1 interface,
bmAttributes=0xc0 self-powered, bMaxPower=0x32=100 mA); interface `09 04 00 00 02
ff ff ff 02` (EP count 2, class/sub/proto = 0xFF vendor, iInterface=2); two bulk
endpoints `07 05 81 02 <pkt> 00` (EP1 IN) and `07 05 01 02 <pkt> 00` (EP1 OUT).

Interpretation: this is the **USB peripheral-boot descriptor set** (vendor-specific
class, EP1 IN/OUT bulk). The ordering 512/64/64/512 corresponds to the two speed
variants (CONFIGURATION + OTHER_SPEED_CONFIGURATION) for two settings. `idVendor`
/ `idProduct` are zero in ROM and are almost certainly patched from OTP at run time.
*Guess:* the 6-byte prefix `{0xC0,0x32,...}` mirrors `bmAttributes`/`bMaxPower`;
bytes [0] and [1] of the blob are runtime-writable (setters at 0x4002DB8C writes
byte[0], 0x4002DB92 writes byte[1]); the remaining 4 bytes are unknown/reserved.

### 3c. 0xABC blob at 0x400300A0 (-> 0x40030B5C)

**Byte content is entirely zero (2748/2748 bytes).** It is a table/code/data
neither - it is the *source operand of a memset* whose real effect is to zero the
IRAM range 0x4030D1A0..0x4030DC5B. The ROM image simply reserves/pads 0xABC zero
bytes there so the copy table has a uniform `{src,dst,len,fn}` shape.

That zeroed IRAM range is a **BSS/scratch region**, proven live by the literal
scan: the GIC IRQ dispatcher uses **0x4030DB08** as a software IRQ-handler table
base (section 4), the boot-config parser uses **0x4030D1A0**, and further globals
sit at 0x4030D238 and 0x4030DB4C..0x4030DC48. So "the 0xABC blob" should be read as
"the size of the zeroed BSS", not as embedded data.

Note: everything from 0x40030B5C to the end of the 48-KB dump (0x40034000) is also
zero, except a single word `0x00000003` at 0x40033FFC (unknown: build/version or
checksum residue).

---

## 4. The second vector table (0x4002FFC0) and handlers

`0x4002FFC0..0x4002FFDC` = 8 x `e59ff018` (`ldr pc,[pc,#0x18]`); target words at
`0x4002FFE0..0x4002FFFC`. This block is copied verbatim to IRAM 0x4030D000 by
record 0, so the two tables are identical by construction.

| idx | vector | ROM target word | 0x0002xxxx -> 0x40028xxx | purpose |
|-----|--------|-----------------|---------------------------|---------|
| 0 | reset | 0x00028090 | 0x40028090 | `b .` hang |
| 1 | undefined | 0x00028080 | 0x40028080 | `b .` hang |
| 2 | SVC | 0x00028084 | 0x40028084 | `b .` hang |
| 3 | prefetch abort | 0x000289E4 | 0x400289E4 | abort capture handler |
| 4 | data abort | 0x000289F0 | 0x400289F0 | abort capture handler |
| 5 | reserved | 0x00028090 | 0x40028090 | `b .` hang |
| 6 | IRQ | 0x00029420 | 0x40029420 | GIC IRQ entry (ARM) |
| 7 | FIQ | 0x00028098 | 0x40028098 | `b .` hang |

(0x40028080/84/88/8C/90/94/98..0x400280A8 are all `eafffffe  b .`.)

### Abort handlers (ARM)

- **0x400289E4 (prefetch abort):** `mrc p15,0,r0,c6,c0,2` (IFAR -> r0);
  `mrc p15,0,r1,c5,c0,1` (IFSR -> r1); `ldr pc,[pc,#0x1c]` loads 0x00028088 -> hang.
- **0x400289F0 (data abort):** `mrc p15,0,r0,c6,c0,0` (DFAR -> r0);
  `mrc p15,0,r1,c5,c0,0` (DFSR -> r1); `ldr pc,[pc,#0x14]` loads 0x0002808C -> hang.

So both abort handlers stash fault address + fault status in r0/r1 and fall into an
infinite loop (`b .`). They do not recover - only a JTAG-observed hang would read
r0/r1.

### IRQ entry 0x40029420 (ARM) + Thumb dispatcher 0x400293C2

0x40029420 is genuine **ARM** (coherent ARM decode; the Thumb decode is garbage):

```
ldr  sp,[pc,#0x48]      ; sp = 0x4030D104  (IRQ-mode save area)
stmda sp!,{r6,r7}       ; save r6/r7
mrs  r6,spsr
orr  r6,r6,#0xC0        ; force I/F mask
bics r6,r6,#0x20        ; force ARM state
mrs  r7,apsr
msr  CPSR_fsxc,r6       ; switch into interrupted mode
mov  r6,sp
msr  CPSR_fsxc,r7       ; back to IRQ mode
mov  sp,r6              ; use interrupted stack
push {r0-r12,lr}        ; save full context on the interrupted stack
blx  0x400293C2         ; -> Thumb dispatcher
pop  {r0-r12,lr}
ldr  sp,[pc,#0x10]      ; sp = 0x4030D100
pop  {r6,r7}
subs pc,lr,#4           ; IRQ return
```

So the ARM stub is a tiny prologue that runs the real dispatcher on a dedicated
IRAM save area (0x4030D100/0x4030D104) and returns with `subs pc,lr,#4`.

The **Thumb dispatcher 0x400293C2** (0x400293C2..0x40029411) is a GIC software-IRQ
dispatch:

```
push {r2,r3,r4,lr}
blx  0x4002F53C         ; r0 = MIDR & 3  (small variant/cpu index)
mov  r4,r0
add  r1,sp,#4 ; mov r0,sp
bl   0x4002B7D4         ; ack/get IRQ id -> writes id byte to [sp+4]; ret!=0 => spurious
cbnz r0,done
ldr  r2,=0x4030DB08     ; IRQ handler-table base
movs r1,#0
ldrb.w r0,[sp,#4]       ; requested IRQ id
adds r2,r2,#4
add.w r2,r2,r4,lsl #5   ; per-index block (0x20 B)
loop:  ldr.w r3,[r2,r1,lsl #3]      ; entry.id
       cmp  r3,r0 ; bne next
       add.w r3,r2,r1,lsl #3
       ldr  r3,[r3,#4]              ; entry.handler
       cbz  r3, clear
       blx  r3                      ; call handler
       b    done
clear: str.w r3,[r2,r1,lsl #3]      ; clear entry
       bl   0x4002B7CA
next:  adds r1,#1 ; cmp r1,#4 ; blo loop
done:  ldr  r0,[sp] ; bl 0x4002B7EA  ; EOI
       pop  {r2,r3,r4,pc}
```

Helper chain (verified):

- **0x4002F53C** = `mrc p15,0,r0,c0,c0,5` ; `and r0,r0,#3` (MIDR&3) - small index.
- **0x4002B7D4 -> 0x4002E218**: reads `[0x4824010C]` = **GICC_IAR** (GIC CPU-interface
  Interrupt Acknowledge, id = IAR[9:0]), normalises the id (>=0x20 -> small index;
  0x3FE -> 0xFF spurious), stores it to `*r1`, returns 1 when the id is 0xFF.
- **0x4002B7EA -> 0x4002E246**: writes `*r0` to `[0x48240000+0x110]` = **GICC_EOIR**
  (End Of Interrupt).

So index 6 (IRQ) is a real GIC-driven handler: ack IAR, look up a per-index
software table of four `{id, handler}` entries at `0x4030DB0C + (idx<<5)` inside the
memset-zeroed region, call the matching handler, then EOI. All IRQ registration is
therefore runtime; the ROM ships the table zeroed.

---

## 5. Indirection of the ROM vectors (0x40028004 -> IRAM 0x4030D004 -> target)

The ROM's **first** vector table (0x40028000, also aliased at 0x00028000 where
VBAR is set) redirects *into IRAM*:

```
0x40028000: b 0x40028910                     ; reset
0x40028004: ldr pc,[pc,#0x18] -> [0x40028024]=0x4030D004   ; undef
0x40028008: ldr pc,[pc,#0x18] -> [0x40028028]=0x4030D008   ; SVC
0x4002800C: ldr pc,[pc,#0x18] -> [0x4002802C]=0x4030D00C   ; pabort
0x40028010: ldr pc,[pc,#0x18] -> [0x40028030]=0x4030D010   ; dabort
0x40028014: ldr pc,[pc,#0x18] -> [0x40028034]=0x4030D014   ; reserved
0x40028018: ldr pc,[pc,#0x18] -> [0x40028038]=0x4030D018   ; IRQ
0x4002801C: ldr pc,[pc,#0x18] -> [0x4002803C]=0x4030D01C   ; FIQ
0x40028020: 0x1CF48309  (unknown signature word, unused as a vector)
```

Because record 0 copies the **second** table (0x4002FFC0) to IRAM 0x4030D000, the
IRAM slots are themselves `ldr pc,[pc,#0x18]` stubs whose pools the copy also
placed:

```
ROM vector [0x28004+i*4] -> IRAM [0x4030D004+i*4] ("ldr pc,[pc,#0x18]")
                         -> IRAM [0x4030D024+i*4] (= copy of 0x4002FFE0+i*4)
                         -> real handler 0x0002xxxx
```

Resulting mapping (double indirection):

| ROM vector | -> IRAM slot | -> loaded target | final |
|------------|--------------|------------------|-------|
| 0x40028004 undef | 0x4030D004 | 0x00028080 | hang |
| 0x40028008 SVC   | 0x4030D008 | 0x00028084 | hang |
| 0x4002800C pabt  | 0x4030D00C | 0x000289E4 | abort capture |
| 0x40028010 dabt  | 0x4030D010 | 0x000289F0 | abort capture |
| 0x40028014 rsvd  | 0x4030D014 | 0x00028090 | hang |
| 0x40028018 IRQ   | 0x4030D018 | 0x00029420 | GIC IRQ entry |
| 0x4002801C FIQ   | 0x4030D01C | 0x00028098 | hang |
| (IRQ table idx0) | 0x4030D000 | 0x00028090 | hang (unused reset slot) |

Purpose of the indirection: the ROM keeps its own first table (needed only to
vector from the boot alias) but routes every exception through a **writable copy in
IRAM**, so runtime/payload code can patch the live vector table at
0x4030D000..0x4030D03F without touching ROM. The targets are expressed as
`0x0002xxxx` (boot-alias) addresses because VBAR = 0x00028000.

---

## 6. Evidence list

- Raw reads (Python `struct` over the dump):
  - copy table words at 0x4002FF80/90/A0/B0; second-table words 0x4002FFC0..0x4002FFFC.
  - 0x40030000-0x40030007 = `0000000000000000`; 0x40030008-0x4003009F descriptor bytes.
  - blob 0x400300A0..0x40030B5C = 2748/2748 zero bytes (verified all bytes).
  - tail 0x40030B5C..0x40034000 = zero except word 0x40033FFC = 00000003.
  - IRAM literal xref scan over all 4-byte offsets (results in section 2).
- LLVM disassembly (ARM, `--triple=armv7-none-eabi --mcpu=cortex-a9`):
  - 0x40028000-0x400280AC (vectors + hangs), 0x4002882C-0x400288E0 (walker, memcpy,
    memset), 0x400288D0-0x40028920 (SP=0x4030D000, ARM->Thumb `bx r12`), 0x400289E0-
    0x40028A18 (abort handlers + literal pool), 0x40029420-0x4002947C (IRQ entry +
    literals 0x4030D104/0x4030D100), 0x4002F530-0x4002F560 (MIDR/pstate helpers).
- LLVM disassembly (Thumb, `--triple=thumbv7-none-eabi --mcpu=cortex-a9`):
  - 0x400293C2-0x40029411 (GIC dispatcher), 0x4002B7C0-0x4002B7FE (IAR/EOI helpers),
    0x4002E218-0x4002E250 (GICC_IAR/EOIR, base 0x48240000), 0x4002A4C0-0x4002A570
    (descriptor-ring init, literals 0x4030D040/0x4030D060), 0x40028BE4-0x40028C00.
- Cross-references (LE word scans): 0x4030DB08 literal only at 0x40029410; 0x4030D108
  literals at 0x4002DB9C(+0x10C at 0x4002DB18, +0x10E at 0x4002D2AC); 0x4030D1A0 at
  0x40028BF8 / 0x4002C918; 0x4002FFC0 referenced only by the copy table (0x4002FF80).

## 7. Unknowns / next steps

1. **`subne pc, r7, r3` (0x40028860) veneer math** - the exact behaviour for Thumb
   table entries (bit0=1) is unresolved; in this image all four records hold ARM
   addresses so the path is dead, but a Thumb-fn table would exercise it.
2. **6-byte descriptor prefix** at 0x40030008 (`c0 32 00 00 01 00`) - confirmed
   runtime-writable at bytes [0],[1] via 0x4002DB8C / 0x4002DB92; exact semantic
   (bmAttributes/bMaxPower vs vendor bytes) not proven. Device idVendor/idProduct
   are zero and presumably OTP-patched - patch site not located.
3. **IRQ table index** comes from `MIDR & 3` (0x4002F53C), which is 0 on Cortex-A9;
   whether `0x4030DB08+4+(idx<<5)` is a per-CPU or per-variant sub-table is
   unclear. Number of handler-table blocks inside the 0xABC region (~10) is not
   confirmed by code.
4. **Signature word 0x40028020 = 0x1CF48309** and the trailing word 0x40033FFC = 3
   (checksum/version?) unidentified.
5. The four IRAM globals at 0x4030D040/0x4030D058/0x4030D060 (descriptor ring) and
   0x4030D238 have known pointers but unconfirmed field semantics.
6. The 0x4030D10C (`01 00`) and 0x4030D10E (device descriptor) users
   (0x4002DB18 / 0x4002D2AC) were not fully traced; the USB enumeration code path
   that consumes the template is a good next target.
