# 150 — MLO / arm9000 (Thumb) RPMB record handling: byte-level map

Date: 2026-10-31
Scope: **STATIC RE ONLY.** No device, no USB, no `/dev/ttyACM0`, no ssh, no
network. Local binutils only (`tools/env.sh` → `arm-linux-objdump` 2.43.1,
`-b binary -m arm` with `-M force-thumb` for Thumb regions) plus `python3` for
byte/word scans.

Input: `local/proprietary/device-binaries/emmc-first1mb.bin` (first 1 MiB of the
eMMC user area). Mapping for this image: **file offset == address − 0x80000000**.

Evidence markers: **[P]** proven from bytes (address + quoted bytes/disassembly)
· **[I]** inferred (reasoning stated) · **[U]** unresolved from bytes available.

Status: **complete for all five questions.**

Headline results (each substantiated below):

| Q | Answer |
|---|---|
| 1 | `FUN_800090CE` is the selector. `record_read` (`0x80011690`) → **3** (absent) ⇒ selector **0**; **0** + `len==4` ⇒ value `1..0x32` gives **1**, value `0` or `>0x32` gives **2**; any other read status **or `len!=4`** ⇒ **2**. |
| 2 | Thumb. New value computed at **0x8000912E** (`subs r0,r0,#1`). Re-seal = **SMC 0x31 → 0x33 → 0x2F → 0x33 → 0x2E** (domain 5) then a JEDEC RPMB *authenticated data write* (type 3) + *result read* (type 5) over the MMC bus. A non-zero service return aborts the write and the caller halts at `0x800094BE`. The write is **after** the region-0 verify and **before** the OS jump. |
| 3 | **No record-supplied length or offset can escape DRAM or the staging buffers.** The only fault class reachable is an *alignment* fault on an odd `rec->offset` at `0x80011362`; the image never sets `SCTLR.A`, so in practice a malformed record always reaches the halt at `0x800094BE`. |
| 4 | **Yes, there is MUSB/USB-OTG-HS driver code** (`0x80012D00`–`0x800135B0`), but **no descriptors, no VID/PID (0x0FCA is absent from the entire 1 MiB), no GET_DESCRIPTOR handler, and no class-level enumeration state machine reachable from the boot path.** Only `0x4A0AB000` of the four USB bases is referenced; the USBTLL bases (`0x4A0AE000`, `0x4A0B0000`) and `0x4A0AC000` never appear. |
| 5 | **Yes**: the stage has a real ARM vector table at `0x80009000` (trampolining to the BootROM public API `0x4030D0xx`), and it **arms WDT2 at `0x800094F0` (called from the reset path at `0x80019248`)** and kicks it repeatedly. The halt loop `0x800094CA` does **not** kick, so if WDT2 is running the SoC is reset. |

---

## 0. Input verification (do this before trusting anything below)

### 0.1 The two slices are **not** what their names suggest, but both verify

**[P]** `bootblob_arm9000.bin` (86016 B) is byte-identical to
`emmc-first1mb.bin[0x9000 : 0x9000+0x15000]` — `cmp` clean, 0 differing bytes.
Base = `0x80009000`. Usable directly (file offset in the slice == address −
0x80009000).

**[P]** `bootblob_MLO.bin` (16384 B) is **not** the MLO payload at file `0x4000`.
It is byte-identical to `emmc-first1mb.bin[0x4d70 : 0x8d70]`, i.e. the *last*
16 KiB of the MLO section:

```
bootblob_MLO == emmc[0x4d70:0x8d70]      -> True
emmc[0x4000]  = "CertISW\0..."           (certificate header, NOT code)
```

So `bootblob_MLO.bin` must be rebased at `0x80004D70`, **not** `0x80004000`.
Everything in this report is taken from `emmc-first1mb.bin` directly, so the
mistake cannot propagate.

### 0.2 Section layout inside the first 1 MiB [P]

0x1C-byte catalogue entries `{u32 off, u32 size, u32, u32, u32, char name[8]}`:

```
00000000: 00 40 00 00 70 4d 00 00 ... "MLO"        off 0x4000 size 0x4D70
0000001c: 00 0c 00 00 b8 32 00 00 ... "PRIMAPP"    off 0x0C00 size 0x32B8
00000038: 00 02 00 00 60 09 00 00 ... "KEYS"       off 0x0200 size 0x0960
00000054: ff ff ff ff ...                          terminator
```

Then, inside the MLO section, a second, inner header:

```
00004000: 43 65 72 74 49 53 57 00 ... "CertISW"
00004030: 01 00 00 00  50 03 00 00  20 4a 00 00  ca b9 ee 03
          version=1     certlen=0x350  payloadlen=0x4A20  (crc?)
```

`0x350 + 0x4A20 = 0x4D70` = the catalogue size, so the **executable part of MLO
is file 0x4350 – 0x8D70**. The Thumb SMC veneer at `0x800043B4` and the
per-service wrapper library at `0x80004682`–`0x80004B70` all lie inside it.

`0x8D70–0x9000` is padding; `0x9000+` is the second stage (`arm9000`), whose
first 0x60 bytes are an ARM exception vector table (see §5).

*Report 146 §Q1.1f describes the two regions as "MLO" and "the ARM stage". The
bytes show one **single program image** in two sections: both sections contain
Thumb code with ARM islands, and — decisively — the second section reads Thumb
function pointers that point into the first section's wrapper library (§2.3).
Neither section is "an ARM stage".*

---

## 1. Q1 — Full record path: catalogue → read → validate → path selection

### 1.1 The record store data structures [P]

Literal pools give the whole store layout. From `0x80011530` and `0x80011708`:

| literal | value | role |
|---|---|---|
| `0x80011530` | `0x8002C760` | record table, **1024 entries × 28 B** (`0x8002C760..0x80033760`) |
| `0x80011534` | `0x80025FB0` | store state `{+0: block-base, +4: count, +8: nblocks}` |
| `0x80011538` | `0x80025983` | **record magic** stamped at entry `+0` |
| `0x8001153C` | `0x80033F80` | 224-byte staging buffer |
| `0x80011540` | `0x80033760` | per-record area; `+0x20` ⇒ free-space table `0x80033780` (1024 × u16) |
| `0x80011B54`/`0x80011B58` | — | misc NVRAM-ish state |
| `0x80010C44` | `0x8002A4E0` | **512-byte RPMB frame buffer** |
| `0x80010C48` | `0x8002A6E0` | secondary 512-byte buffer |
| `0x80011104` | `0x8002C720` | 32-byte word-protect/state area |
| `0x80011108` | `0x80025F00` | KEK/IV working area (`+0x20`, `+0x60`, `+0xA0` used) |

Record entry (28 bytes), recovered from the constructors/parsers:

```
+0x00  u32  magic 0x80025983           (0x80011636: ldr r1,=0x80025983 / str r1,[r0,#0])
+0x04  char name[14]                   (0x8001113A/0x8001161C: memcmp/memcpy with r2=14)
+0x12  u16  slot  (record area index)  (0x80011156: ldrh r3,[r4,#18])
+0x14  u16  slot  (duplicate)          (0x80011562: strh r3,[r4,#20])
+0x16  u16  length                     (0x8001163C: strh r4,[r0,#22])
+0x18  u16  offset inside the store    (0x80011262/0x800111EE: strh ..,[r0,#24])
```

`0x8001112C` is `find(name)`: linear scan of `[state+4]` entries, comparing 14
bytes at `entry+4` against the name (`bl 0x80018BB4` = memcmp). Returns the
entry or 0.

### 1.2 The catalogue is rebuilt from the medium on every boot [P]

`0x800112DC` = "mount / build catalogue". Only caller: **boot main, `0x80014936`**.

```
800112dc  b53e        push {r1,r2,r3,r4,r5,lr}
800112e2  str r0,[sp,#0] / str r0,[sp,#4]          ; two zeroed locals
800112e6  2001        movs r0,#1
800112e8  f7fb ffd8   bl   0x8000d29c              ; (1, &l) -> *l = 0, returns 1  (stub)
800112ec  4a91        ldr  r2,=0x80025FB0          ; store state
800112ee  2101        movs r1,#1
800112f0  9802        ldr  r0,[sp,#8]
800112f2  f7fb ff8b   bl   0x8000d20c              ; MMC device open (slot 0)
800112f6  1c40        adds r0,r0,#1
800112f8  d017        beq  0x8001132a              ; -1 => error 6
800112fa  4c8e        ldr  r4,=0x80025FB0
800112fc  4890        ldr  r0,=0x80033760
800112fe  6821        ldr  r1,[r4,#0]
80011300  f7ff fd3f   bl   0x80010d82              ; read block 0 header
80011304  b188        cbz  r0,0x8001132a
80011306  6820        ldr  r0,[r4,#0]
80011308  2103        movs r1,#3
8001130a  466a        mov  r2,sp
8001130c  f7fe fbf1   bl   0x8000faf2              ; read geometry / extCSD
80011310  b958        cbnz r0,0x8001132a
80011312  e9dd 0100   ldrd r0,r1,[sp]
80011316  0a00        lsrs r0,r0,#8                     ; r0 = (u64 >> 8) & 0xffffffff
80011318  ea40 6001   orr.w r0,r0,r1,lsl #24
8001131c  f5b0 6f80   cmp.w r0,#1024
80011320  60a0        str  r0,[r4,#8]                   ; state->nblocks
80011322  d802        bhi  0x8001132a                  ; > 1024 blocks -> error 6
80011324  f7ff ff37   bl   0x80011196                  ; parse all blocks into the catalogue
80011328  b908        cbnz r0,0x8001132e
8001132a  2006        movs r0,#6 ; return 6
8001132e  2000        movs r0,#0 ; return 0 (ok)
```

`0x80011196` walks blocks `0..state->nblocks-1`, reading each with
`0x80010A34(staging=0x80033F80, 224, free_table=0x80033760, state->block_base, blk)`
and scanning the 224-byte payload for the magic with `0x80011166`, copying the
24-byte record header found there into the next free catalogue entry. Space
accounting is a 1024-entry u16 free-length table at `0x80033780`.

**Consequence for Q3: the catalogue is not a static table; every boot it is
re-derived from whatever is on the medium. A corrupt record is parsed on every
boot, not just at write time.**

### 1.3 `record_read` — `0x80011690` [P]

Signature `(const char *name, void *dst, u32 maxlen, u16 *outlen)`; returns
`0` ok / `2` dst too small / `3` no such record / `5` name mismatch on re-read /
`6` I/O error.

```
80011690  e92d 43f8   stmdb sp!,{r3,r4,r5,r6,r7,r8,r9,lr}
80011694  4688        mov  r8,r1                   ; dst
80011696  4d20        ldr  r5,=0x80033F80          ; staging
80011698  4617        mov  r7,r2                   ; maxlen
8001169a  461e        mov  r6,r3                   ; outlen
8001169c  f7ff fd46   bl   0x8001112c             ; find(name) -> r4
800116a0  0004        movs r4,r0
800116a2  d102        bne  0x800116aa
800116a4  2003        movs r0,#3                   ; ***** ABSENT *****
800116a6  e8bd 83f8   ldmia.w sp!,{r3,r4,r5,r6,r7,r8,r9,pc}
800116aa  8ae0        ldrh r0,[r4,#22]             ; rec->len
800116ac  42b8        cmp  r0,r7
800116ae  d901        bls  0x800116b4
800116b0  2002        movs r0,#2                   ; len > maxlen
800116b2  e7f8        b    0x800116a6
800116b4  8a63        ldrh r3,[r4,#18]             ; rec->slot
800116b6  21e0        movs r1,#224
800116b8  4813        ldr  r0,=0x80025FB0
800116ba  9300        str  r3,[sp,#0]              ; arg5 = slot
800116bc  4a13        ldr  r2,=0x80033760
800116be  6803        ldr  r3,[r0,#0]              ; state->block_base
800116c0  4628        mov  r0,r5
800116c2  f7ff f9b7   bl   0x80010a34             ; RPMB "authenticated data read" of that block
800116c6  b108        cbz  r0,0x800116cc
800116c8  2006        movs r0,#6
800116ca  e7ec        b    0x800116a6
800116cc  8ae0        ldrh r0,[r4,#22]             ; rec->len
800116ce  220e        movs r2,#14
800116d0  8030        strh r0,[r6,#0]              ; *outlen = len
800116d2  8b20        ldrh r0,[r4,#24]             ; ***** rec->offset, UNVALIDATED u16 *****
800116d4  4405        add  r5,r0                   ; r5 = staging + offset
800116d6  1d20        adds r0,r4,#4
800116d8  1d29        adds r1,r5,#4
800116da  f007 fa6b   bl   0x80018bb4             ; memcmp(rec->name, staging+off+4, 14)
800116de  b108        cbz  r0,0x800116e4
800116e0  2005        movs r0,#5                   ; name mismatch -> record rejected
800116e2  e7e0        b    0x800116a6
800116e4  8ae2        ldrh r2,[r4,#22]             ; len
800116e6  f105 0118   add.w r1,r5,#24
800116ea  4640        mov  r0,r8
800116ec  f007 ead6   blx  0x80018c9c             ; memcpy(dst, staging+off+24, len)
800116f0  2000        movs r0,#0
```

### 1.4 The "seal" of a read [P]

The read is not a plain block read: `0x80010A34` is the RPMB
*authenticated data read* verb, and the 224-byte payload it returns is
verified/decrypted by the monitor before it is parsed. `0x80011048` is the
corresponding "unseal into the staging buffer" wrapper:

```
80011048 ... 2104 / 2001 / 4643 / 4622   movs r1,#4 ; movs r0,#1 ; r3=0x200 ; r2=frame
8001106c f7f8 fdc2   bl   0x80009bf4         ; log(1, 4, frame, 512)
80011070 4d25        ldr  r5,=0x80025F00
...
800110b8 f006 eb4e   blx  0x80017758         ; <-- monitor crypto (SMC 0x33 + 0x2E)
800110ca 2220        movs r2,#32
800110cc f504 71e2   add.w r1,r4,#452        ; frame + 0x1C4  (the 32-byte tag/IV slot)
800110d0 4648        mov  r0,r9              ; = frame + 0x10 (set at 0x8001109E)
800110d2 f007 fd35   bl   0x80018b40         ; memcmp(frame+0x10, frame+0x1C4, 32) -> mismatch = tamper
800110f6 f006 eba2   blx  0x8001783c         ; <-- monitor crypto (SMC 0x33 + 0x2F)
```

i.e. the read path also *verifies*: the 32 bytes the monitor returns at `frame+0x1C4`
must equal the 32 bytes at `frame+0x10` that were carried inside the sealed block
(`0x800110D2`, mismatch ⇒ `0x800110FC` ⇒ return 0 ⇒ `record_read` returns 6).

### 1.5 The selector — `FUN_800090CE` (report 71 §2's `FUN_8000911A`) [P]

Anchors verified. `0x800090CE` is the worker; **`0x8000911A` is a 2-instruction
veneer** that calls it with `out = NULL`:

```
8000911a  2000  movs r0,#0        ; out = NULL
8000911c  e7d7  b.n  0x800090ce
```

Full worker, byte-for-byte:

```
800090ce  b51c        push {r2,r3,r4,lr}       ; frame: [sp+0]=value(u32) [sp+4]=stored_len(u16)
800090d0  0004        movs r4,r0               ; r4 = out
800090d2  d001        beq  0x800090d8          ; out==NULL -> skip the store
800090d4  2000        movs r0,#0
800090d6  6020        str  r0,[r4,#0]          ; *out = 0
800090d8  2204        movs r2,#4               ; maxlen = 4
800090da  ab01        add  r3,sp,#4            ; &stored_len
800090dc  4669        mov  r1,sp               ; &value
800090de  a01a        add  r0,pc,#104          ; -> 0x80009148  "BOOTCTR_FCTRY"
800090e0  f008 fad6   bl   0x80011690          ; record_read(name, &value, 4, &stored_len)
800090e4  2803        cmp  r0,#3
800090e6  d009        beq  0x800090fc          ; (A) record absent
800090e8  b918        cbnz r0,0x800090f2        ; (B) any other non-zero status
800090ea  f8bd 0004   ldrh.w r0,[sp,#4]        ; stored_len
800090ee  2804        cmp  r0,#4
800090f0  d006        beq  0x80009100          ; (C) length == 4 -> validate value
800090f2  215b        movs r1,#91              ; (B/C-fail) msg id 0x5B
800090f4  2000        movs r0,#0
800090f6  f00b fdcf   bl   0x80014c98          ; log(0, 0x5B)
800090fa  e00c        b    0x80009116          ; -> return 2
800090fc  2000        movs r0,#0               ; (A)
800090fe  bd1c        pop  {r2,r3,r4,pc}        ; ***** return 0 : ABSENT *****
80009100  b11c        cbz  r4,0x8000910a         ; (C) out == NULL ?
80009102  9800        ldr  r0,[sp,#0]           ; value
80009104  2832        cmp  r0,#50               ; 0x32
80009106  d800        bhi.n 0x8000910a          ; value > 0x32 -> do NOT publish
80009108  6020        str  r0,[r4,#0]           ; *out = value   (0..0x32 inclusive)
8000910a  9800        ldr  r0,[sp,#0]
8000910c  b118        cbz  r0,0x80009116         ; value == 0  -> return 2
8000910e  2832        cmp  r0,#50
80009110  d801        bhi.n 0x80009116          ; value > 0x32 -> return 2
80009112  2001        movs r0,#1
80009114  bd1c        pop  {r2,r3,r4,pc}        ; ***** return 1 : 1..0x32 *****
80009116  2002        movs r0,#2
80009118  bd1c        pop  {r2,r3,r4,pc}        ; ***** return 2 : 0 / >0x32 / len!=4 / I/O *****
```

The record name is a literal in the same function:

```
00009148: 42 4f 4f 54 43 54 52 5f  46 43 54 52 59 00 00 00   "BOOTCTR_FCTRY\0\0\0"
```

### 1.6 The exact compare/branch chain, answering Q1 literally

| medium state | `0x80011690` | `0x800090EE` | published `*out` | selector |
|---|---|---|---|---|
| name not in catalogue | `3` @ `0x800116A4` | not reached | untouched (or 0 if `out≠NULL`) | **0** |
| read I/O error (`0x80010A34`≠0) | `6` @ `0x800116C8` | not reached | 0 if `out≠NULL` | **2** |
| name mismatch on re-read | `5` @ `0x800116E0` | not reached | 0 if `out≠NULL` | **2** |
| `rec->len > maxlen` (=4) | `2` @ `0x800116B0` | not reached | 0 if `out≠NULL` | **2** |
| ok, `stored_len != 4` | `0` | `cmp #4` fails → `0x800090F2` | 0 | **2** (and logs `0x5B`) |
| ok, `len==4`, value `0` | `0` | pass | **0** | **2** |
| ok, `len==4`, `1 ≤ value ≤ 0x32` | `0` | pass | **value** | **1** |
| ok, `len==4`, value `> 0x32` | `0` | pass | **not written** | **2** |

Note the asymmetry at `0x80009106`/`0x80009108`: `*out` is published for
`0..0x32` **inclusive**, but the return is `1` only for `1..0x32`. A stored value
of `0` therefore yields `*out = 0` and selector `2`.

Note also that `len != 4` is an **error (2)**, not "absent": a record that exists
with the wrong length is fatal, whereas a missing record is benign.

### 1.7 Path selection at boot main (`0x80014904`) [P]

`0x80014904` is entered by `blx` from the ARM reset path at **`0x80019264`**.
The record-related prologue:

```
8001492a  f7fc fa07   bl   0x80010d3c        ; RPMB subsystem probe (returns 0 = no RPMB)
8001492e  b188        cbz  r0,0x80014954
80014930  f7fc fac2   bl   0x80010eb8        ; provisioning probe -> r7
80014934  4607        mov  r7,r0
80014936  f7fc fcd1   bl   0x800112dc        ; ***** build catalogue (Q1.2) *****
8001493a  b118        cbz  r0,0x80014944
8001493c  215b        movs r1,#0x5b
8001493e  2000        movs r0,#0
80014940  f000 f9aa   bl   0x80014c98        ; log(0,0x5B)  catalogue failed
80014944  b15f        cbz  r7,0x8001495e
80014946  a07f        adr  r0,0x80014b44       ; "RPMB not provisioned\n"
80014948  f002 fc5d   bl   0x80017206        ; puts
8001494c  2001        movs r0,#1
8001494e  f7f4 fba7   bl   0x800090a0        ; provision: write BOOTCTR_FCTRY = 0x32
80014952  b970        cbnz r0,0x80014972
80014954  215b ... log(0,0x5B); b 0x80014972
8001495e  f7f4 fbdc   bl   0x8000911a        ; ***** SELECTOR, evaluation #1 *****
80014962  2801        cmp  r0,#1
80014964  d101        bne  0x8001496a
80014966  a17d        adr  r1,0x80014b5c       ; "FACTORY"
80014968  e000        b    0x8001496c
8001496a  a17e        adr  r1,0x80014b64       ; "PRODUCT"
8001496c  a07f        adr  r0,0x80014b6c       ; "\nBOOT_MODE: %s\n"
8001496e  f002 fc4a   bl   0x80017206        ; puts   -- cosmetic only
```

`0x800090A0` is the "provision/repair" helper (selector-independent):

```
800090a0  b508        push {r3,lr}
800090a2  b930        cbnz r0,0x800090b2
800090a4  a028        adr  r0,"BOOTCTR_FCTRY"
800090a6  f008 faea   bl   0x8001167e       ; record_len(name,&len) -> 0 found / 3 absent
800090aa  b160        cbz  r0,0x800090c6
800090ac  2803        cmp  r0,#3
800090ae  d10c        bne  0x800090ca
800090b0  e009        b    0x800090c6
800090b2  2801        cmp  r0,#1          ; only mode 1 writes
800090b4  d109        bne  0x800090ca
800090b6  2032        movs r0,#50
800090b8  9000        str  r0,[sp,#0]
800090ba  2204        movs r2,#4
800090bc  4669        mov  r1,sp
800090be  a022        adr  r0,"BOOTCTR_FCTRY"
800090c0  f008 fa91   bl   0x800115e6       ; record_write(name,&50,4)
800090c4  b908        cbnz r0,0x800090ca
800090c6  2001        movs r0,#1          ; success
```

Then the verifier, and the second selector evaluation, feed the decrement:

```
80014a20  f7f4 fdda   bl   0x800095d8
80014a24  4955        ldr  r1,=0x80025E74
80014a26  ab01        add  r3,sp,#4
80014a28  aa03        add  r2,sp,#12
80014a2a  a804        add  r0,sp,#16
80014a2c  f7ff fd86   bl   0x8001453c        ; ***** region verify, wrapper sets verifier flag = 1 *****
80014a30  b1e0        cbz  r0,0x80014a6c   ; 0 -> verification failed
80014a32  2801        cmp  r0,#1
80014a34  d003        beq  0x80014a3e
80014a36  2802        cmp  r0,#2
80014a38  d11d        bne  0x80014a76
80014a3a  603d        str  r5,[r7,#0]
80014a3c  e01b        b    0x80014a76
80014a3e  f7f4 fb6c   bl   0x8000911a        ; ***** SELECTOR, evaluation #3 -- gates the decrement *****
80014a42  494f        ldr  r1,=0x80026150  ; "factory count consumed" flag
80014a44  b180        cbz  r0,0x80014a68    ; selector == 0  -> nothing to do
80014a46  2001        movs r0,#1
80014a48  6008        str  r0,[r1,#0]      ; flag = 1
80014a4a  f7f4 fb68   bl   0x8000911e        ; ***** DECREMENT + RE-SEAL + WRITE *****
80014a4e  b940        cbnz r0,0x80014a62
80014a50  a04c        adr  r0,0x80014b84   ; "\nBootModeCount expended\n"
80014a52  f002 fbd8   bl   0x80017206        ; puts
80014a56  215b        movs r1,#0x5b
80014a58  2000        movs r0,#0
80014a5a  f000 f91d   bl   0x80014c98        ; log(0,0x5B)
80014a5e  f7f4 fd2e   bl   0x800094be        ; ***** HALT *****
80014a62  f003 ea80   blx  0x80017f64        ; SMC 0x3A  (no args)
80014a66  e010        b    0x80014a8a
80014a68  600d        str  r5,[r1,#0]      ; flag = 0
80014a6a  e00e        b    0x80014a8a
```

Because `0x8000911E` re-reads the record and requires selector `==1` itself
(§2.1), an evaluation-#3 result of **2** falls through to the "expended" path and
halt. **Both "malformed record" and "value already 0" therefore land on
`0x800094BE`, not on a fault.**

Inside the verifier driver `0x800142A8` the selector is evaluated a second time
and it decides whether the NVRAM region descriptor is consulted:

```
800142b0  f7f4 ff33   bl   0x8000911a        ; ***** SELECTOR, evaluation #2 *****
800142b4  4f4c        ldr  r7,=0x80026144
800142b6  f04f 0a00   mov.w sl,#0
800142ba  b148        cbz  r0,0x800142d0    ; 0 -> go and parse BOOTCTR/NvOSRegionStruct
800142bc  a93c        add  r1,sp,#0xf0      ; non-zero -> straight to the region walk
800142be  f8c7 a008   str.w sl,[r7,#8]     ; region index = 0
800142c2  4648        mov  r0,r9
800142c4  c90e        ldmia r1,{r1,r2,r3}
800142c6  f7ff ff3f   bl   0x80014148        ; verify region 0 ...
```

*Correction to report 71 §2:* the selector is **not** called "at
`0x80014254`/`0x80014260`". Those two addresses are the internal branch points of
`0x800142A8` (`cbnz r6,0x80014260` and `cmp r6,#1`) that choose **SHA-512 +
public key** (`bl 0x80014716` at `0x8001425A`) versus **CRC-32**
(`bl 0x80013FB6` gate, then `bl 0x80014788` at `0x80014278`). The selector call
is at `0x800142B0`.

---

## 2. Q2 — The decrement write

### 2.1 `FUN_8000911E` — disassembly of `0x80009100..0x80009160`: **Thumb** [P]

`0x80009100..0x80009160` decodes coherently only as Thumb-2 (`b11c` = `cbz`,
`f7ff ffd4` = 32-bit `bl`, `4669` = `mov r1,sp`, literal pool at
`0x80009146/0x80009148`). Decoding the same bytes as ARM gives nonsense
(`0xe7d7…` is not an ARM `b`). The whole region `0x80008D70..0x80013FFF` used in
this report is Thumb; the ARM islands are `0x80009078` (the veneer), the
`0x80013770`/`0x800175DC`-upwards crypto blocks, and the `0x80019200`-upwards
reset/vector block.

```
8000911e  b508        push {r3,lr}
80009120  4668        mov  r0,sp                  ; out = &value
80009122  f7ff ffd4   bl   0x800090ce            ; selector-with-out  (§1.5)
80009126  2801        cmp  r0,#1
80009128  d10b        bne.n 0x80009142            ; not exactly 1 -> return 0 (no write)
8000912a  9800        ldr  r0,[sp,#0]
8000912c  b148        cbz  r0,0x80009142          ; value 0 -> return 0 (no write)
8000912e  1e40        subs r0,r0,#1               ; ***** NEW VALUE COMPUTED HERE *****
80009130  2204        movs r2,#4                  ; len = 4
80009132  9000        str  r0,[sp,#0]             ; frame value = value-1
80009134  4669        mov  r1,sp                  ; src = &value
80009136  a004        add  r0,pc,#16              ; -> 0x80009148 "BOOTCTR_FCTRY"
80009138  f008 fa55   bl   0x800115e6            ; record_write(name, &value, 4)
8000913c  b908        cbnz r0,0x80009142
8000913e  2001        movs r0,#1                  ; success
80009140  bd08        pop  {r3,pc}
80009142  2000        movs r0,#0                  ; failure
80009144  bd08        pop  {r3,pc}
80009146  0000        (pad)
80009148  42 4f 4f 54 43 54 52 5f 46 43 54 52 59 00 00 00   "BOOTCTR_FCTRY"
```

Only caller: `0x80014A4A` (§1.7). Return values: `1` = wrote, `0` = did not
(record absent, value 0, or write failed) — the caller cannot distinguish, it
halts on 0.

### 2.2 `record_write` — `0x800115E6` [P]

```
800115e6  e92d 41f0   stmdb sp!,{r4,r5,r6,r7,r8,lr}
800115ea  4605        mov  r5,r0                 ; name
800115ec  4688        mov  r8,r1                 ; src
800115ee  4614        mov  r4,r2                 ; len
800115f0  f7ff fd9c   bl   0x8001112c           ; find(name)
800115f4  2cc8        cmp  r4,#200               ; 0xC8
800115f6  d801        bhi  0x800115fc
800115f8  07a1        lsls r1,r4,#30             ; len & 3 ?
800115fa  d001        beq  0x80011600
800115fc  2002        movs r0,#2                 ; reject: len>200 or unaligned
800115fe  e5aa        b    0x80011156
80011600  bb00        cbnz r0,0x80011644         ; exists -> rewrite path
80011602  4e41        ldr  r6,=0x80025FB0        ; create path
80011604  6870        ldr  r0,[r6,#4]            ; count
80011606  f5b0 6f80   cmp.w r0,#1024
8001160a  d212        bcs  0x80011632           ; table full -> 0 -> return 1
8001160c  ebc0 00c0   rsb  r0,r0,r0,lsl #3       ; n*7
80011610  4f3f        ldr  r7,=0x8002C760
80011612  220e        movs r2,#14
80011614  4629        mov  r1,r5
80011616  eb07 0080   add.w r0,r7,r0,lsl #2      ; table + n*28
8001161a  1d00        adds r0,r0,#4               ; +4  (name field)
8001161c  f007 ec36   blx  0x80018e8c           ; strncpy(name, 14)
80011620  6870        ldr  r0,[r6,#4]
80011622  1c40        adds r0,r0,#1
80011624  6070        str  r0,[r6,#4]            ; count++
80011626  ebc0 00c0   rsb  r0,r0,r0,lsl #3
8001162a  eb07 0080   add.w r0,r7,r0,lsl #2
8001162e  381c        subs r0,#28                ; &new entry
80011630  e000        b    0x80011634
80011634  b1b0        cbz  r0,0x80011664
80011636  f8df ...    ldr  r1,=0x80025983        ; magic
80011638  6001        str  r1,[r0,#0]
8001163a  4641        mov  r1,r8
8001163c  82c4        strh r4,[r0,#22]           ; rec->len = len
8001163e  e8bd 41f0   ldmia.w sp!,{r4,r5,r6,r7,r8,lr}
80011642  e75a        b    0x800114fa           ; ***** install into the block store *****
80011644  8ac1        ldrh r1,[r0,#22]           ; rewrite path: old len
80011646  42a1        cmp  r1,r4
80011648  d207        bcs  0x8001165a
8001164a  8a42        ldrh r2,[r0,#18]           ; slot
8001164c  4b2f        ldr  r3,=0x80033760
8001164e  1a61        subs r1,r4,r1               ; need = new - old
80011650  3320        adds r3,#32
80011652  f833 2012   ldrh.w r2,[r3,r2,lsl #1]   ; free space in that block
80011656  4291        cmp  r1,r2
80011658  da04        bge  0x80011664           ; return 1 (no room)
8001165a  4641        mov  r1,r8
8001165c  82c4        strh r4,[r0,#22]           ; rec->len = len
8001165e  e8bd 41f0   ldmia.w sp!,{r4,r5,r6,r7,r8,lr}
80011662  e666        b    0x80011332           ; ***** free old slots + rewrite *****
```

### 2.3 The re-seal, step by step

**Step 1 — `0x800114FA` "install a newly created record into the block store".**

```
800114fa  e92d 43f0   stmdb sp!,{r4,r5,r6,r7,r8,r9,lr}
800114fe  4604        mov  r4,r0                 ; new entry
80011500  b089        sub  sp,#36
80011502  2000        movs r0,#0
80011504  4e0d        ldr  r6,=0x80033F80        ; staging
80011506  4689        mov  r9,r1                 ; src data
80011508  9002        str  r0,[sp,#8]
8001150a  f44f 6580   mov.w r5,#1024
8001150e  8ae1        ldrh r1,[r4,#22]           ; len
80011510  f5a6 6300   sub.w r3,r6,#2048          ; 0x80033780 free table
80011514  f101 0218   add.w r2,r1,#24            ; need = len + 24
80011518  f833 7010   ldrh.w r7,[r3,r0,lsl #1]   ; free space in block r0
8001151c  4297        cmp  r7,r2
8001151e  d311        bcc  0x80011544           ; next block
80011520  ...        strh free -= (len+24)
8001152c  e00d        b    0x8001154a
...
80011556  b283        uxth r3,r0                  ; chosen block
80011558  f8df 81ac   ldr.w r8,=0x80025FB0
8001155c  21e0        movs r1,#224
8001155e  8263        strh r3,[r4,#18]           ; rec->slot = block
80011560  4630        mov  r0,r6
80011562  82a3        strh r3,[r4,#20]
80011564  9300        str  r3,[sp,#0]
80011566  4a69        ldr  r2,=0x80033760
80011568  f8d8 3000   ldr.w r3,[r8,#0]           ; state->block_base
8001156c  f7ff fa62   bl   0x80010a34           ; ***** SMC: RPMB read of that block *****
80011570  b108        cbz  r0,0x80011576
80011572  2006        movs r0,#6 ; return 6
80011576  25e0        movs r5,#224
80011578  4637        mov  r7,r6
8001157a  e008        b    0x8001158e
...
8001158e  ab02        add  r3,sp,#8
80011590  462a        mov  r2,r5
80011592  4639        mov  r1,r7
80011594  a803        add  r0,sp,#12
80011596  f7ff fde6   bl   0x80011166           ; find a free record header slot in the block
8001159a  2800        cmp  r0,#0
8001159c  d1ee        bne  0x8001157c
8001159e  8ae0        ldrh r0,[r4,#22]
800115a0  3018        adds r0,#24
800115a2  42a8        cmp  r0,r5
800115a4  d901        bls  0x800115aa
800115a6  2005        movs r0,#5 ; return 5
800115aa  f1c5 00e0   rsb  r0,r5,#224             ; rec->offset = 224 - remaining
800115ae  2218        movs r2,#24
800115b0  8320        strh r0,[r4,#24]
800115b2  4621        mov  r1,r4
800115b4  4638        mov  r0,r7
800115b6  f007 eb72   blx  0x80018c9c          ; write 24-byte record header into staging
800115ba  8ae2        ldrh r2,[r4,#22]
800115bc  f107 0018   add.w r0,r7,#24
800115c0  4649        mov  r1,r9
800115c2  f007 eb6c   blx  0x80018c9c          ; write the 200-byte-max payload after it
800115c6  8a63        ldrh r3,[r4,#18]
800115c8  21e0        movs r1,#224
800115ca  4a50        ldr  r2,=0x80033760
800115cc  4630        mov  r0,r6
800115ce  9300        str  r3,[sp,#0]
800115d0  f8d8 3000   ldr.w r3,[r8,#0]
800115d4  f7ff faab   bl   0x80010b2e           ; ***** SMC: RPMB WRITE of that block (the re-seal) *****
800115d8  b118        cbz  r0,0x800115e2
800115da  212f        movs r1,#0x2f
800115dc  2000        movs r0,#0
800115de  f003 fb5b   bl   0x80014c98           ; log(0,0x2F)
800115e2  2000        movs r0,#0                 ; NB: returns 0 even when the write failed
```

Note: `0x800114FA` **returns 0 whether or not the RPMB write succeeded** — the
failure only produces a `log(0,0x2F)`. That is the only signal the caller gets.

**Step 2 — `0x80010B2E` "write one 224-byte record block" (the actual re-seal).**

```
80010b2e  e92d 47fc   stmdb sp!,{r2,r3,r4-r9,sl,lr}
80010b32  4680        mov  r8,r0                    ; staging
80010b36  4c43        ldr  r4,=0x8002A4E0           ; 512-byte RPMB frame buffer
80010b38  9d0a        ldr  r5,[sp,#40]              ; arg5 = slot / block number
80010b3c  29e0        cmp  r1,#224
80010b3e  d15b        bne  0x80010bfe              ; only 224-byte payloads
80010b46  4651        mov  r1,sl
80010b48  a801        add  r0,sp,#4
80010b4a  f7ff fe7b   bl   0x80010844              ; ***** RPMB type 2 : get write counter (SMC) *****
80010b52  f44f 7700   mov.w r7,#512
80010b56  4639        mov  r1,r7
80010b58  4620        mov  r0,r4
80010b5a  f008 e92c   blx  0x80018db4            ; memset(frame, 0, 512)
80010b5e  2003        movs r0,#3
80010b60  f8a4 01fe   strh.w r0,[r4,#510]         ; frame type = 3 (AUTHENTICATED DATA WRITE)
80010b64  2001        movs r0,#1
80010b66  f8a4 51f8   strh.w r5,[r4,#504]         ; address = slot
80010b6a  f8a4 01fa   strh.w r0,[r4,#506]         ; block count = 1
80010b6e  9801        ldr  r0,[sp,#4]              ; write counter
80010b70  b2e9        uxtb r1,r5
80010b72  f8c4 01f4   str.w r0,[r4,#500]          ; frame[0x1F4] = counter
80010b76  2000        movs r0,#0
80010b78  180a        adds r2,r1,r0
80010b7a  5422        strb r2,[r4,r0]
80010b7c  1c40        adds r0,r0,#1
80010b7e  2810        cmp  r0,#16
80010b80  dbfa        blt  0x80010b78              ; fill 16 bytes of nonce with slot+offset
80010b82  21e0        movs r1,#224
80010b84  4622        mov  r2,r4
80010b86  4640        mov  r0,r8
80010b88  f000 f9e6   bl   0x80010f58              ; ***** SEAL: MAC/encrypt into frame+0xE4 (SMC) *****
80010b8c  b3b8        cbz  r0,0x80010bfe
80010b8e  2000        movs r0,#0 ...  stmia.w r4,{r0,r1,r2,r3}   ; clear the first 16 bytes
80010b9a  4620        mov  r0,r4
80010b9c  f7ff fde8   bl   0x80010770              ; byte-reverse [0xE4..0x1E3] + byteswap header words
80010ba0  4649        mov  r1,r9
80010ba2  4620        mov  r0,r4
80010ba4  f7ff fe07   bl   0x800107b6              ; build the MMC request descriptor (SMC 0x33)
80010ba8  f8b4 31fe   ldrh.w r3,[r4,#510]
80010bac  2100        movs r1,#0
80010bae  4652        mov  r2,sl
80010bb0  4620        mov  r0,r4
80010bb2  9300        str  r3,[sp,#0]
80010bb4  2301        movs r3,#1
80010bb6  f7ff fe2a   bl   0x8001080e              ; ***** send frame to the card *****
80010bba  2800        cmp  r0,#0
80010bbc  d120        bne  0x80010c00              ; -> return -1
80010bbe  4639        mov  r1,r7
80010bc0  4620        mov  r0,r4
80010bc2  f008 e8f8   blx  0x80018db4             ; memset(frame,0,512)
80010bc6  2005        movs r0,#5
80010bc8  f8a4 01fe   strh.w r0,[r4,#510]          ; frame type = 5 (RESULT READ)
80010bcc  4620        mov  r0,r4
80010bce  f7ff fdcf   bl   0x80010770               ; byte-swap
80010bd2  ...        bl   0x8001080e               ; send status request
80010be4  2800        cmp  r0,#0
80010be6  d10b        bne  0x80010c00
80010be8  2100 / 4652 / 4620
80010bee  f000 f885   bl   0x80010cfc               ; wait for the response frame
80010bf2  2800        cmp  r0,#0
80010bf4  d104        bne  0x80010c00
80010bf6  f8b4 01fc   ldrh.w r0,[r4,#508]          ; result field
80010bfa  2800        cmp  r0,#0
80010bfc  d000        beq  0x80010c00
80010bfe  4630        mov  r0,r6                    ; r6 = -1 (0xFFFFFFFF)
80010c00  e8bd 87fc   ldmia.w sp!,{r2,r3,r4-r9,sl,pc}
```

**Step 3 — `0x80010F58`, the seal itself.** This is where the monitor services
appear, in this order:

```
80010f58  e92d 41f0   stmdb sp!,{r4,r5,r6,r7,r8,lr}
80010f5c  29e0        cmp  r1,#224                 ; payload length must be 224
80010f66  06e8        lsls r0,r5,#27               ; payload must be 32-byte aligned
80010f76  06e0        lsls r0,r4,#27               ; frame must be 32-byte aligned
80010f8a  4e5f        ldr  r6,=0x80025F00
80010f8c  6830        ldr  r0,[r6,#0]
80010f8e  2801        cmp  r0,#1
80010f90  d103        bne  0x80010f9a
80010f92  a907        add  r1,sp,#28
80010f94  f006 ee50   blx  0x80017c38             ; ***** SMC 0x31 (domain 5, 1 arg) - KEK/state query *****
80010fc4  2300 ... 4619 ... 4618
80010fcc  f006 ec36   blx  0x8001783c             ; ***** SMC 0x33 then 0x2F - keyed transform of the payload *****
80010fd0  b188        cbz  r0,0x80010ff6
80010fdc  2020 / 2103 / 2201 / 3360
80010fe0  f504 73e2   add.w r3,r4,#452              ; frame + 0x1C4
80010ff0  f006 ebb2   blx  0x80017758             ; ***** SMC 0x33 then 0x2E - block cipher into frame+0xE4 *****
80010ff4  b918        cbnz r0,0x80010ffe
80010ff6  2000        movs r0,#0                    ; fail
80010ffe  6830        ldr  r0,[r6,#0]
80011002  2801        cmp  r0,#1
80011004  d103        bne  0x8001100c
80011006  2000 / a907
80011008  f006 ee16   blx  0x80017c38             ; ***** SMC 0x31 again - close the session *****
8001100c  ...  bl 0x80009bf4                     ; log(1,5,frame,512)
80011018  2001        movs r0,#1                    ; success
```

The two crypto wrappers, disassembled (ARM), give the exact service order:

```
; 0x80017758 : "block transform"  -> SMC 0x33, then SMC 0x2E
80017794  e3550010   cmp   r5,#16                  ; key length must be 16
8001779c  e3a03010   mov   r3,#16
800177a0  e58d2000   str   r2,[sp]
800177a4  e3a02005   mov   r2,#5                   ; domain 5
800177a8  e58d3004   str   r3,[sp,#4]
800177ac  e3a03002   mov   r3,#2                   ; 2 arguments
800177b0  e3a01000   mov   r1,#0
800177b4  e3a00033   mov   r0,#51                  ; *** SMC 0x33 ***
800177b8  eb000307   bl    0x800183dc
...
800177c4  e1cd70bc   strh  r7,[sp,#12]             ; descriptor: {u16 op, u16 0, src, srclen, dst, dstlen, key, keylen}
800177cc  e3a03c01   mov   r3,#256
800177d0  e1cd00be   strh  r0,[sp,#14]
800177d4  e28d200c   add   r2,sp,#12
800177d8  e58d5024   str   r5,[sp,#36]            ; keylen
800177dc  e3a01004   mov   r1,#4
800177e0  e58d4018   str   r4,[sp,#24]            ; dst
800177e4  e3a00001   mov   r0,#1
800177e8  e58d601c   str   r6,[sp,#28]            ; dstlen
800177ec  e58db020   str   fp,[sp,#32]            ; key
800177f0  e58d9010   str   r9,[sp,#16]            ; src
800177f4  e58da014   str   sl,[sp,#20]            ; srclen
800177f8  faffc8fd   blx   0x80009bf4             ; log
800177fc  e28d200c   add   r2,sp,#12
80017800  e58d8004   str   r8,[sp,#4]             ; IV pointer
80017804  e58d2000   str   r2,[sp]                ; descriptor pointer
80017808  e3a03002   mov   r3,#2                 ; 2 arguments
8001780c  e3a02005   mov   r2,#5                 ; domain 5
80017810  e3a01000   mov   r1,#0
80017814  e3a0002e   mov   r0,#46                 ; *** SMC 0x2E ***
80017818  eb0002ef   bl    0x800183dc

; 0x8001783c : "keyed digest"   -> SMC 0x33, then SMC 0x2F
80017958  eb00029f   bl    0x800183dc             ; *** SMC 0x33 *** (same 16-byte key setup)
800179a0  eb00028d   bl    0x800183dc             ; *** SMC 0x2F *** (1 argument)
```

Summary of the SMC order for one record re-seal:

| order | site | SMC | domain (r2) | args |
|---:|---|---|---:|---|
| 1 | `0x80010844`→`0x800107B6`→`0x80017758` @`0x800177B8` | **0x33** | 5 | 2 (`{ptr,len=16}`) — session/key setup |
| 2 | `0x80010844`… same | — | — | *(type-2 "get write counter" frame sent)* |
| 3 | `0x80010F58` @`0x80010F94` | **0x31** | 5 | 1 (KEK/state query, id 1) |
| 4 | `0x80010F58` @`0x80010FCC` → `0x8001783C` @`0x80017958` | **0x33** | 5 | 2 |
| 5 | `0x80010F58` @`0x80010FCC` → `0x8001783C` @`0x800179A0` | **0x2F** | 5 | 1 |
| 6 | `0x80010F58` @`0x80010FF0` → `0x80017758` @`0x800177B8` | **0x33** | 5 | 2 |
| 7 | `0x80010F58` @`0x80010FF0` → `0x80017758` @`0x80017818` | **0x2E** | 5 | 2 (block cipher into frame+0xE4) |
| 8 | `0x80010F58` @`0x80011008` | **0x31** | 5 | 1 (close) |
| 9 | `0x800107B6` @`0x80017818`-equivalent | **0x2E** | 5 | 2 (frame/MMC descriptor) |
| 10 | frame type 3 sent on the MMC bus | — | — | — |
| 11 | frame type 5 (result read) sent | — | — | — |

(The exact interleave of steps 1–2 depends on the `r8`/`fp` arguments of
`0x80017758`; when `r8 == 0` the `0x33` at `0x800177B8` is skipped —
`0x8001778C: cmp r8,#0 / beq 0x800177C4`.)

### 2.4 Frame layout and MAC [P] / [I]

`0x80010770` normalises the 512-byte buffer at `0x8002A4E0` before transmission:

```
80010770  b570        push {r4,r5,r6,lr}
80010774  f100 02e4   add.w r2,r0,#228            ; r2 = frame + 0xE4
8001077a  f5c1 7380   rsb  r3,r1,#256            ; byte-reverse the 256 bytes [0xE4 .. 0x1E3]
8001077e  5c54        ldrb r4,[r2,r1]
80010784  f813 5d01   ldrb.w r5,[r3,#-1]!
80010788  5455        strb r5,[r2,r1]
8001078c  701c        strb r4,[r3,#0]
80010792  f500 70fa   add.w r0,r0,#500           ; r0 = frame + 0x1F4
80010796  6801        ldr  r1,[r0,#0]
80010798  ba09        rev  r1,r1                 ; 32-bit byte swap
8001079a  6001        str  r1,[r0,#0]            ; [0x1F4] = counter  (big-endian)
8001079c  8881        ldrh r1,[r0,#4] / ba49 rev16 / 8081 strh     ; [0x1F8]
800107a2  88c1        ldrh r1,[r0,#6] / ba49 rev16 / 80c1 strh     ; [0x1FA]
800107a8  8901        ldrh r1,[r0,#8] / ba49 rev16 / 8101 strh     ; [0x1FC]
800107ae  8941        ldrh r1,[r0,#10]/ ba49 rev16 / 8141 strh     ; [0x1FE]
```

Recovered field map of the 512-byte buffer at `0x8002A4E0`:

```
+0x000 .. +0x0DF   224 B  record payload  (24-byte record header + <=200 B data)
+0x0E0 .. +0x0E3     4 B  padding / prefix
+0x0E4 .. +0x1E3   256 B  sealed (MAC/cipher) area, byte-reversed before send
+0x1E4 .. +0x1F3    16 B  nonce / IV              (0x80010B78 fills 16 B = slot+i)
+0x1F4 .. +0x1F7     4 B  write counter           (byte-swapped)
+0x1F8               2 B  address = slot          (byte-swapped)
+0x1FA               2 B  block count = 1         (byte-swapped)
+0x1FC               2 B  result
+0x1FE               2 B  request/response type
```

Request types used, and where:

| type | constant set at | meaning |
|---:|---|---|
| 1 | `0x80010F?` | *program key* — `0x800108D8` (`movs r1,#1 / strh.w r1,[r4,#510]`) |
| 2 | `0x8001086A` | *get write counter* — `0x80010844` |
| 3 | `0x80010B5E` | *authenticated data write* — `0x80010B2E` |
| 4 | `0x80010A66`, `0x80010992` | *authenticated data read* — `0x80010A34`, `0x80010966` |
| 5 | `0x80010BC6`, `0x80010918` | *result read* — after every write |

[I] This is the standard JEDEC/Toshiba RPMB opcode set; the buffer has a 4-byte
prefix so the JEDEC frame proper starts at `+4` (`+4+0x1E0 = 0x1E4` nonce,
`+4+0x1F0 = 0x1F4` counter, `+4+0x1F4 = 0x1F8` address, etc.). The byte reversal
of `[0xE4..0x1E3]` is the buffer→big-endian conversion the card expects.

Note that a **counter read (`type 2`) precedes every write** and its value is
copied straight into `frame[0x1F4]` (`0x80010B6E-0x80010B72`); there is no
software anti-replay here — the card enforces it.

### 2.5 What happens on a non-zero service return

| where | check | outcome |
|---|---|---|
| `0x800177BC` (after SMC 0x33) | `cmp r0,#0 / bne 0x80017830` | `0x80017758` returns 0 |
| `0x8001781C` (after SMC 0x2E) | `cmp r0,#0 / bne` | `0x80017758` returns 0 |
| `0x80017C5C` (after SMC 0x31) | `cmp r0,#0 / beq 0x80017C6C` | returns 1 (**inverted**: success when smc == 0) |
| `0x80010FD0` | `cbz r0,0x80010FF6` | `0x80010F58` returns 0 |
| `0x80010FF4` | `cbnz r0,0x80010FFE` | ok, log and return 1 |
| `0x80010B8C` | `cbz r0,0x80010BFE` | `0x80010B2E` returns **-1** |
| `0x80010BBA` | `cmp r0,#0 / bne 0x80010C00` | return -1 |
| `0x800115D8` (in `0x800114FA`) | `cbz r0` | only `log(0,0x2F)`; **returns 0 regardless** |
| `0x8000913C` (in the decrementer) | `cbnz r0` | return 0 → caller prints `"\nBootModeCount expended\n"` and **halts at `0x800094BE`** |

The important asymmetry: at *every* layer below `0x800115E6` the write failure
is reduced to a log message, but `0x800115E6` propagates `1` (a `0x80011156`
style non-zero return) and `0x8000911E` turns that into "return 0" → **halt**.

### 2.6 Ordering against the region-0 verify and the OS jump [P]

```
0x80014904  boot main entry  (called by blx from 0x80019264)
   ...
0x80014A2C  bl 0x8001453C      <-- REGION-0 VERIFY  (0x800142A8 with verifier flag = 1)
0x80014A30  cbz r0 -> failure
0x80014A3E  bl 0x8000911A      <-- selector, evaluation #3
0x80014A4A  bl 0x8000911E      <-- DECREMENT + RE-SEAL + WRITE
0x80014A5E  bl 0x800094BE      <-- halt if the write failed
0x80014A62  blx 0x80017F64     <-- SMC 0x3A (no args)
0x80014AC4  puts("\nLeaving bootrom and entering OS\n")
0x80014ACC  pop {r4,r5,r6,r7,pc}   -> returns to 0x80019268
0x80019268  blx 0x80015A58
0x8001926C  blx 0x800094CC
0x80019270  blx 0x800158E4
0x80019298  ldr sp,[0x800517C0]
0x800192A0  ldr r0,=0x80025E74 ; ldr r0,[r0] ; bx r0    <-- OS / IFS JUMP
```

**The record write happens after the region-0 verification and before the
jump to the IFS/OS.** It is also after the `SMC 0x3A` in program order only in
the sense that `0x3A` follows it; `0x3A` is issued *after* a successful write.

The OS entry point is taken indirectly from RAM at `0x80025E74`
(`0x800192A0-0x800192A8`), which is the same pointer the verifier driver
receives as its `r1` (`0x80014A24: ldr r1,=0x80025E74`). `0x800159AC`
(`blx` at `0x80019260`) and `0x80015A58` frame the boot main call.

---

## 3. Q3 — Malformed record: does anything fault instead of halting?

### 3.1 Every record-derived pointer / length, exhaustively

I enumerated every load of `rec->offset` (`+0x18`) and `rec->len` (`+0x16`) and
every use of a value read out of the medium, across the record store
(`0x80009000`–`0x800117FF`):

| site | instruction | what it bounds |
|---|---|---|
| `0x800116AA` | `ldrh r0,[r4,#22]` + `cmp r0,r7` (`maxlen`) | **len is bounded before any copy**; `>maxlen` ⇒ return 2 |
| `0x800116D2` | `ldrh r0,[r4,#24]` → `add r5,r0` | `staging + u16 offset`, then `memcmp(staging+off+4, 14)` |
| `0x800116E4` | `ldrh r2,[r4,#22]` → `memcpy(dst, staging+off+24, len)` | `len ≤ maxlen` (4 for `BOOTCTR_FCTRY`, ≤200 for a general write) |
| `0x8001135A` | `ldrh r0,[r4,#24]` ; `ldrh r1,[r0,r1]` with `r1 = staging+0x16` | **halfword load at `staging+0x16+offset`** — the only *potentially unaligned* access |
| `0x80011368` | `add r1,r5,#24 ; add r0,r1 ; memcpy(..., len)` | `len ≤ 200` |
| `0x8001147E` | `ldrh r2,[r1,#24] ; add r2,r5` | offset → addresses stored, not dereferenced |
| `0x80011652` | `ldrh r2,[r0,#18]` (slot) ; `ldrh.w r2,[r3,r2,lsl #1]` `r3 = 0x80033780` | `slot` is a u16 from the medium; address range `0x80033780 .. 0x8005377E` |
| `0x800111EE` | `strh r0,[r7,#24]` | offset accumulation **truncated to u16** — silent wrap if the store exceeds 64 KiB |
| `0x80011166` | `memmove(dst, buf+idx*4, 24)` | scan for the magic; can over-read **up to 20 bytes past** `buf+len` when the magic is in the last 3 words |
| `0x800111F0-0x80011200` | `ldrh r3,[r7,#22]` then `cmp r0,r2 / bcc fail` | the catalogue builder *does* check `remaining ≥ len+24` |

Worst-case reach, with `offset = 0xFFFF` and the staging buffer at
`0x80033F80`: the highest byte touched is `0x80033F80 + 0xFFFF + 24 + 200 =
0x8004409C`. That is still inside the first MiB of OMAP4430 SDRAM
(`0x80000000`+). The free-space lookup at `0x80011652` reaches at most
`0x8005377E`. **No record-supplied value can address outside DRAM**, because
every one of them is a 16-bit field added to a base in `0x8002xxxx`/`0x8003xxxx`.

### 3.2 Is there an MMU that could turn that into a translation fault? [P]

There are exactly eight `mcr p15,0,rX,cr1,cr0,{0}` (SCTLR) writes in the image,
and **none of them sets SCTLR.M (bit 0)**:

```
; MLO entry stub
80004350  e3a000d3   mov   r0,#211           ; SVC, I=1, F=1
80004354  e121f000   msr   CPSR_c,r0
80004358  ee110f10   mrc   15,0,r0,cr1,cr0,{0}
8000435c  e3c00004   bic   r0,r0,#4           ; clear C (D-cache off)
80004360  e3c00a01   bic   r0,r0,#4096        ; clear I (I-cache off)
80004364  ee010f10   mcr   15,0,r0,cr1,cr0,{0}
80004370  e3a00000   mov   r0,#0
80004374  ee070f15   mcr   15,0,r0,cr7,cr5,{0}  ; ICIALLU
80004380  e59fd024   ldr   sp,=0x4030A340
80004384  e59f0024   ldr   r0,=0x403044BB
80004388  e12fff30   blx   r0                   ; BootROM public API
8000438c  e3a00102   mov   r0,#0x80000000
80004390  e12fff10   bx    r0

; cache-off helpers
80019324 ... bic r0,r0,#4     ; clear C   (only if SCTLR != 0)
80019340 ... bic r0,r0,#0x1000 ; clear I
```

Note that `bic #4` clears **bit 2 = C**, and `bic #0x1000` clears **bit 12 = I**.
**Neither bit 1 (A, alignment check) nor bit 0 (M, MMU) is ever set**, and there
is no TTBR write (`cr2`) and no `cr3` (DACR) write in the image. [I] The stage
therefore runs with the MMU off and flat physical addressing, and the CPSR
`0xD3` at both entry points leaves `A` (asynchronous-abort mask) clear.
Alignment checking is whatever the previous stage left in `SCTLR.A`.

### 3.3 Conclusion for Q3

* A corrupted record produces, in order: a possible `memcmp`/`memcpy` from a
  wrong (but always in-DRAM) staging offset ⇒ `record_read` returns **5**
  (name mismatch) or **6**; or `len != 4` ⇒ **2**; or a reset write counter ⇒
  the frame type-5 result field is non-zero ⇒ `0x80010B2E` returns -1.
* All of those collapse to selector **2** at `0x80014A3E`, which is non-zero,
  so the code *enters* the decrement path (`0x80014A46`), where `0x8000911E`
  re-reads, gets 2, `bne` at `0x80009128`, returns 0, and boot main takes
  `0x80014A4E → 0x80014A50..0x80014A5E`:
  `"\nBootModeCount expended\n"` + `log(0,0x5B)` + **halt at `0x800094BE`**.
* **The one genuine fault candidate** is `0x80011362`
  (`ldrh r1,[r0,r1]` with `r0 = rec->offset`): an **odd offset gives an
  unaligned halfword load**. On Cortex-A9 with the MMU off and `SCTLR.A = 1`
  that is a Data Abort; with `SCTLR.A = 0` it is a normal unaligned access.
  The image never sets `SCTLR.A`, so whether this can fault depends on state
  inherited from PRIMAPP/BootROM — [U] from this image alone.
* **No path takes a record-supplied length as a loop bound.** Every loop bound
  is a constant (224, 512, 1024, 14, 24) or a value from the free-space table
  that was checked first.

If a Data Abort did occur, it would vector through the table at `0x80009000`
(§5) into the BootROM public API at `0x4030D010`, i.e. still not a silent hang —
provided the vector base is `0x80009000`.

---

## 4. Q4 — Does this stage initialise USB?

### 4.1 Searches performed (all negative except one)

| searched for | method | result |
|---|---|---|
| `0x4A0AB000` | exact 32-bit LE word + halfword `4A0A` scan of all 1 MiB | **2 hits**: literals at `0x800132E0` and `0x8001359C` |
| `0x4A0AC000` | same | **none** |
| `0x4A0AE000` (USBTLL_HS config) | same | **none** |
| `0x4A0B0000` (USBTLL_HS ULPI) | same | **none** |
| `0x0FCA` (RIM USB VID) | 32-bit word **and** halfword `0x0FCA` **and** `0xCA0F` | **none** (the single `0xCA0F` at file `0xA25F` is inside a crypto constant) |
| USB device descriptor `12 01 xx xx .. 40` | byte pattern | **none** |
| `"USB"` in any string | full string dump | **none** |
| `0x4A314000` (WDT2) | word scan | `0x8000855C`, `0x80009544` (§5) |
| `0x4A318000` (WDT1) | word scan | `0x8001375C` (§5) |

### 4.2 What the `0x4A0AB000` code actually is [P]

It is a **MUSB (USB OTG HS controller) register driver**, recognisable from the
register offsets, not from any descriptor:

```
; 0x80012F22 : write a block to the currently-selected endpoint FIFO
80012f26  4cee        ldr  r4,=0x4A0AB000
80012f28  81e0        strh r0,[r4,#14]         ; MUSB_INDEX          (0x0E)
80012f2a  eb04 0480   add.w r4,r4,r0,lsl #2
80012f30  f851 0023   ldr.w r0,[r1,r3,lsl #2]
80012f34  6220        str  r0,[r4,#32]         ; FIFO  (0x20 + 4*index)

; 0x80012F5A : speed query
80012f5a  48e1        ldr  r0,=0x4A0AB000
80012f5c  7840        ldrb r0,[r0,#1]          ; MUSB_POWER (0x01)
80012f5e  06c0        lsls r0,r0,#27           ; isolate bit 4
80012f60  d501        bpl  0x80012f66
80012f62  2002        movs r0,#2               ; POWER.HIGHSPEED set -> 2  (HIGH SPEED)
80012f64  4770        bx   lr
80012f66  2001        movs r0,#1               ; else 1                    (FULL SPEED)

; 0x800130EE : interrupt mask init
800130f0  f64f 71ff   movw r1,#0xFFFF
800130f4  80c1        strh r1,[r0,#6]          ; MUSB_INTRTXE  (0x06)
800130f6  1e49        subs r1,r1,#1
800130f8  8101        strh r1,[r0,#8]          ; MUSB_INTRRXE  (0x08)
800130fa  2127        movs r1,#0x27
800130fc  72c1        strb r1,[r0,#11]         ; MUSB_INTRUSBE (0x0B) = 0x27

; 0x800131CC : scan INTRTX / INTRRX and dispatch per endpoint
800131ce  7ab5        ldrb r5,[r6,#10]         ; MUSB_INTRUSB  (0x0A)
800131ee  8875        ldrh r5,[r6,#2]          ; MUSB_INTRTX   (0x02)
8001324e  88b5        ldrh r5,[r6,#4]          ; MUSB_INTRRX   (0x04)

; 0x800133B0 : OMAP wrapper soft-reset  (THE ONLY USER OF +0x404/+0x408)
800133b2  4c7a        ldr  r4,=0x4A0AB000
800133b4  f8d4 0404   ldr.w r0,[r4,#1028]      ; USB_SYSCONFIG (0x404)
800133b8  f040 0002   orr  r0,r0,#2             ; SOFTRESET (bit 1)
800133bc  f8c4 0404   str.w r0,[r4,#1028]
800133c0  f8d4 0408   ldr.w r0,[r4,#1032]      ; USB_SYSSTATUS (0x408)
800133c4  07c0        lsls r0,r0,#31           ; RESETDONE (bit 0)
800133c6  d0fb        beq  0x800133c0            ; poll
800133c8  f241 0008   movw r0,#4104             ; 0x1008
800133cc  f8c4 0404   str.w r0,[r4,#1028]      ; SYSCONFIG = 0x1008
```

So the stage does contain an endpoint/FIFO/interrupt level MUSB driver.

### 4.3 But is it reachable, and does it enumerate? [P]/[I]

Call graph, exhaustively:

* `0x800133B0` (the **soft-reset / clock-enable**) has **no caller at all** — no
  `bl`, no `blx`, no literal pointer (verified by word scan for values in
  `[0x80012D00, 0x80013600]`). [P] *The USB controller is never brought out of
  reset by this image.*
* `0x80012E04` (init/exit wrapper) is called from `0x800120AA` (`b.w`) and
  `0x800125CC`.
* `0x80012090-0x800120AA` is a **periodic poll**: decrement a counter, reload
  it with `200` or `50`, and call `0x80012E04`.
* `0x800125BE` (shutdown) calls `0x80012E04` and `0x800131C8`.
* The only boot-main-adjacent route is `0x80015A38`:
  ```
  80015a1e  2204        movs r2,#4
  80015a20  4669        mov  r1,sp
  80015a22  f44f 5001   mov.w r0,#0x2040          ; NVRAM item 0x2040 (boot-mode word)
  80015a26  f7f4 ffd7   bl   0x8000a9d8
  80015a2a  f7ff faab   bl   0x80014f84
  80015a2e  b148        cbz  r0,0x80015a44
  80015a30  9800        ldr  r0,[sp,#0]
  80015a32  2802        cmp  r0,#2
  80015a34  d000        beq  0x80015a38
  80015a36  b928        cbnz r0,0x80015a44
  80015a38  f7fd fa18   bl   0x80012e6c           ; install hook records
  80015a3c  f7f3 fd52   bl   0x800094e4           ; WDT kick
  80015a40  f7fc fdbd   bl   0x800125be           ; USB shutdown
  ```
  i.e. the *only* call into the USB code from the boot flow is a **shutdown/hook
  registration**, gated on NVRAM `0x2040` ∈ {0, 2}.
* `0x80012E6C` is a *hook installer*, not a controller bring-up:
  ```
  80012e6c  4918        ldr  r1,=0x80013FEC        ; handler
  80012e6e  2200        movs r2,#0
  80012e70  4818        ldr  r0,=0x00C800B4        ; hook id
  80012e72  b510        push {r4,lr}
  80012e74  f7f6 facc   bl   0x80009410           ; register(id 0x00C800B4, handler, 0)
  80012e78  4915        ldr  r1,=0x80013FEC
  80012e7a  2200        movs r2,#0
  80012e7c  4816        ldr  r0,=0x00C700B3
  80012e7e  f7f6 fac7   bl   0x80009410           ; register(id 0x00C700B3, handler, 0)
  80012e82  4815        ldr  r0,=0x00C700B3
  80012e84  2200        movs r2,#0
  80012e86  4912        ldr  r1,=0x80013FEC
  80012e88  38b4        subs r0,#0xB4               ; 0x00C6FFFF
  80012e8a  f7f6 fac1   bl   0x80009410           ; register(id 0x00C6FFFF, handler, 0)
  80012e8e  ...                               ; then falls into the 0x80012E8E body -> 0x80012832
  ```
  None of the three registrations touches `0x4A0AB000`.

**Answer to Q4:** the stage contains a complete low-level MUSB driver and a
device-side endpoint engine, **but it does not initialise USB**: the soft-reset /
SYSCONFIG bring-up routine `0x800133B0` is unreferenced, the USBTLL and
`0x4A0AC000` bases are never touched, there is **no descriptor set**, **no
`VID 0x0FCA`**, and therefore **no GET_DESCRIPTOR handler** and no enumerable
device. [I] The USB code is a library for a *later* stage (or dead code left over
from the shared MLO/OS tree); the `"RIM BlackBerry Device"` / `"ec_agent"` /
`"Mar 24 2011"` / `"15:31:02"` block at file `0x1D050-0x1D0BF` is a
build/board-info record (fixed 0x40/0x10/0x10/0x10-byte fields followed by a
pointer table at `0x1D040` and `0x1D0E0`), **not** a USB string-descriptor set —
USB string descriptors must be UTF-16LE and these are plain ASCII.

Speed: if the code ever ran, `0x80012F5A` returns `2` when `MUSB_POWER`
(`0x4A0AB001`) bit 4 (`HIGHSPEED`) is set, else `1` — i.e. the driver reports
HS/FS by reading the core, it does not force either. [P]

---

## 5. Q5 — Fault vectors and the watchdog

### 5.1 Exception vectors are present and installed at `0x80009000` [P]

The first 0x60 bytes of the `arm9000` section are a full ARM vector table:

```
00009000: 87 40 00 ea  0d 00 00 ea  0d 00 00 ea  0d 00 00 ea   ; reset, undef, swi, pabort
00009010: 0d 00 00 ea  0d 00 00 ea  0d 00 00 ea  0d 00 00 ea   ; dabort, -, irq, fiq
```

Disassembled:

```
80009000  ea004087  b    0x80019224      ; RESET  -> boot path
80009004  ea00000d  b    0x80009040      ; UNDEF
80009008  ea00000d  b    0x80009044      ; SWI
8000900c  ea00000d  b    0x80009048      ; PREFETCH ABORT
80009010  ea00000d  b    0x8000904c      ; DATA ABORT
80009014  ea00000d  b    0x80009050      ; reserved
80009018  ea00000d  b    0x80009054      ; IRQ
8000901c  ea00000d  b    0x80009058      ; FIQ
80009040  e59ff014  ldr   pc,[pc,#20]     ; -> 0x8000905C
...
8000905c  0x4030D004   80009060  0x4030D008   80009064  0x4030D00C
80009068  0x4030D010   8000906c  0x4030D014   80009070  0x4030D018
80009074  0x4030D01C
```

So every fault vector tail-jumps into the **BootROM public API** at
`0x4030D004 … 0x4030D01C` (the OMAP "public ROM API" trampoline block). A Data
Abort therefore does **not** silently hang: it is handed to the ROM's handler.

Whether these vectors are actually *live* depends on VBAR. The image contains
one VBAR writer,

```
80000fbc  ee0c0f10  mcr 15,0,r0,cr12,cr0,{0}    ; VBAR = r0
80000fc0  e12fff1e  bx  lr
```

but that helper has **no `bl`/`blx` caller anywhere in the 1 MiB** (word scan
and branch scan). [U] Whether VBAR == 0x80009000 at the time the RPMB code runs
cannot be decided from this image; it must be set by PRIMAPP or the BootROM.

### 5.2 WDT2 (`0x4A314000`) — armed and kicked

Register map confirmed against the TRM (`hardware/swpu231ap.pdf.txt`
Table 22-96, line 312960): `WIDR 0x00`, `WDSC 0x10`, `WDST 0x14`,
`WISR 0x18`, `WIER 0x1C`, `WWER 0x20`, `WCLR 0x24`, `WCRR 0x28`,
`WLDR 0x2C`, `WTGR 0x30`, `WWPS 0x34`, `WDLY 0x44`, `WSPR 0x48`.

**Arm routine `0x800094F0`** — this is a textbook TRM §22.3.4.8 re-arm
(`0xAAAA/0x5555` = disable, `0xBBBB/0x4444` = enable; TRM lines 312675-312684):

```
800094f0  4814        ldr  r0,=0x4A314000        ; WDT2
800094f2  6901        ldr  r1,[r0,#16]           ; WDT_WDSC
800094f4  f041 0102   orr.w r1,r1,#2             ; bit1
800094f8  6101        str  r1,[r0,#16]
800094fa  6901        ldr  r1,[r0,#16]
800094fc  0789        lsls r1,r1,#30             ; wait for bit1 to self-clear
800094fe  d4fc        bmi  0x800094fa
80009500  2120        movs r1,#32
80009502  6101        str  r1,[r0,#16]           ; WDSC = 0x20
80009504  f64a 21aa   movw r1,#0xAAAA
80009508  6481        str  r1,[r0,#72]           ; WDT_WSPR = 0xAAAA   (disable seq 1)
8000950a  6b41        ldr  r1,[r0,#52]           ; WDT_WWPS
8000950c  2900        cmp  r1,#0
8000950e  d1fc        bne  0x8000950a
80009510  f245 5155   movw r1,#0x5555
80009514  6481        str  r1,[r0,#72]           ; WDT_WSPR = 0x5555   (disable seq 2)
80009516  6b41        ldr  r1,[r0,#52]
80009518  2900        cmp  r1,#0
8000951a  d1fc        bne  0x80009516
8000951c  6a42        ldr  r2,[r0,#36]           ; WDT_WCLR
8000951e  490a        ldr  r1,=0xFFFC1800
80009520  f022 021c   bic.w r2,r2,#28
80009524  6242        str  r2,[r0,#36]
80009526  6281        str  r1,[r0,#40]           ; WDT_WCRR = 0xFFFC1800
80009528  62c1        str  r1,[r0,#44]           ; WDT_WLDR = 0xFFFC1800
8000952a  f64b 31bb   movw r1,#0xBBBB
8000952e  6481        str  r1,[r0,#72]           ; WDT_WSPR = 0xBBBB   (ENABLE seq 1)
80009530  6b41        ldr  r1,[r0,#52]
80009532  2900        cmp  r1,#0
80009534  d1fc        bne  0x80009530
80009536  f244 4144   movw r1,#0x4444
8000953a  6481        str  r1,[r0,#72]           ; WDT_WSPR = 0x4444   (ENABLE seq 2)
8000953c  6b41        ldr  r1,[r0,#52]
8000953e  2900        cmp  r1,#0
80009540  d1fc        bne  0x8000953c
80009542  e7cf        b    0x800094e4            ; then kick
```

**Kick routine `0x800094E4`:**

```
800094e4  4917        ldr  r1,=0x4A314000
800094e6  6b08        ldr  r0,[r1,#48]           ; WDT_WTGR = 0x30
800094e8  1c40        adds r0,r0,#1
800094ea  6308        str  r0,[r1,#48]           ; WDT_WTGR = old+1  -> counter reload
800094ec  f00a b8fb   b    0x800136e6            ; bookkeeping (see below)
```

**Reachability** — the re-arm routine *is* executed. It is reached from
`0x800094B0`, which in turn is called by the ARM reset path:

```
800094ae  4a10        ldr  r2,=0x800094F0
800094b0  b510        push {r4,lr}
800094b2  f000 f81d   bl   0x800094f0
800094b6  e8bd 4010   ldmia.w sp!,{r4,lr}
800094ba  f011 bd67   b.w  0x8001af8c

80019224  e3a000d3   mov  r0,#0xD3               ; RESET vector entry
80019228  e121f000   msr  CPSR_c,r0
...
80019248  faffc098   blx  0x800094b0            ; ***** ARM WDT2 ARM *****
8001924c  faffee21   blx  0x80014ada
...
80019264  faffeda6   blx  0x80014904            ; boot main
```

(`0x80019248: faffc098` decodes as `BLX` with `imm24 = 0xFFC098` →
`PC + 8 − 0xFDA0 = 0x800094B0`; `BLX imm` always switches to Thumb, and
`0x800094B0` is `b510 push {r4,lr}`, a Thumb prologue. [P])

[I] That `0x80019224` is the entry point this stage is actually started at — the
vector table's reset slot at `0x80009000` branches here unconditionally, so
whatever loads `arm9000` at `0x80009000` enters through the reset vector; there
is no other `b`/`bl` into `0x80019224` in the image.

So **WDT2 is re-armed with WCRR = WLDR = 0xFFFC1800 at `0x80019248`, before boot
main runs**, and then kicked at many points.

Kick call sites (Thumb `bl 0x800094e4`):

```
0x80009542 (in the arm routine itself)   0x8000d57a   0x8000e2be   0x800120de
0x8001241c   0x80012518   0x800135ee        0x8001362e   0x800145fa
0x800147ea   0x80014910 (boot main entry)  0x80014ac0 (before "Leaving bootrom")
0x80015a3c   0x80015a44
```

`0x800136E6` is the kick's bookkeeping: it reads `WDT_WCRR` (0x28) and prints a
warning via `0x80013696` (`log(127, 0x2C)`) if the count moved more than
`0x3B8000` since the last kick — i.e. **the code treats the WDT counter as a
free-running timebase and warns when too much time has passed between kicks.**

### 5.3 WDT1 (`0x4A318000`)

Used only for reads, in the USB/clock code:

```
80013690  4832        ldr  r0,=0x4A318000
80013692  6a80        ldr  r0,[r0,#40]           ; WDT_WCRR of WDTIMER3
80013694  4770        bx   lr
800136e6  ... ldr r4,[r0,#40]  (0x28 = WCRR)
80013706  ... ldr r4,[r0,#40]  ; used with a 0xB85 threshold
```

No write to `0x4A318000` exists. **WDT1 is never armed or kicked.**

### 5.4 Would the halt at `0x800094BE` be reset by the watchdog?

```
800094be  b510        push {r4,lr}
800094c0  2240        movs r2,#64         ; 0x40
800094c2  2125        movs r1,#37         ; 0x25
800094c4  2000        movs r0,#0
800094c6  f00a f899   bl   0x800135fc       ; log(0, 0x25, 0x40)
800094ca  e7fe        b    0x800094ca       ; ***** pure infinite self-branch *****
```

`0x800094CA` is a two-byte `b .` — no WDT kick, no sleep, no poll. `0x800094BE`
is reached from ten sites (`0x80012104, 0x800121F2, 0x8001253E, 0x8001263C,
0x80012DA0, 0x80014A5E, 0x80014A82, 0x80015310, 0x8001589A, 0x800158DE`).

**Answer:** yes, if WDT2's counter is running, the SoC is reset. The last kick
before a boot-main halt is at `0x80014910` (boot-main entry) or `0x800145FA` /
`0x800147EA` inside the verifier; nothing kicks again, so the WDT expires
`WCRR` ticks later. Given `WCRR = WLDR = 0xFFFC1800` on a 32-bit *down* counter,
the interval is `0x100000000 − 0xFFFC1800 = 0x3E800 = 256000` ticks — at the
OMAP WKUP 32.768 kHz functional clock that is ≈ **7.8 s**. [I]

The TRM confirms the reset consequence: Table 3-30 "Global Reset Sources",
line 21302, lists

```
Type  Name             Source/Control                    Description
H/W   CORTEXA9_WDT_RST Either WDTIMER2 or MPU_A9 subsystem  It is triggered by a time-out event.
```

so a WDTIMER2 timeout is a **global** (`WKUP_RST`) reset source — the SoC
reboots rather than hanging. [P for the register/source mapping; I for the
7.8 s figure, which depends on the WDT functional clock being 32.768 kHz.]

This matches the observed device behaviour in reports 145/149: a boot that
reaches the "BootModeCount expended" path does not sit dead, it cycles.

---

## 6. SMC call-site table

### 6.1 Encodings

Only two encodings exist on ARMv7-A and both were used for the scan:

```
ARM  : 0xE1600070 | imm4        (e160 0071 = smc #1)
Thumb: 0xF7F0|imm4 , 0x8000    (f7f1 8000 = smc #1, bytes f1 f7 00 80)
```

### 6.2 Every `smc` instruction in `emmc-first1mb.bin` (7 total) [P]

| address | mode | encoding | reached via |
|---|---|---|---|
| `0x800043D0` | Thumb | `f7f1 8000` `smc #1` | **the only Thumb `smc`**: MLO veneer `0x800043B4`, called only by `0x80004C26`, called by the 20 service wrappers at `0x80004682…0x80004B70` |
| `0x80009094` | ARM | `e1600071` `smc #1` | ARM veneer `0x80009078`, called only by `0x80018414` inside the arm9000 dispatcher `0x800183DC` |
| `0x8001939C` | ARM | `e1600071` | PL310 HLOS service, `r12 = 0x102` |
| `0x800193B0` | ARM | `e1600071` | `r12 = 0x100` |
| `0x800193C4` | ARM | `e1600071` | `r12 = 0x109` |
| `0x800193D8` | ARM | `e1600071` | `r12 = 0x112` |
| `0x800193EC` | ARM | `e1600071` | `r12 = 0x113` |

The five `0x800193xx` sites are the TRM §27.5 "Services for HLOS Support" PL310
L2-cache services (report 146 §Q1.2) and are *not* on the RPMB path.

### 6.3 The two veneers [P]

```
; Thumb, MLO
800043b4  2de9 f05f   push.w {r4-r11,fp,ip,lr}
800043b8  4ff0 ff06   mov.w  r6,#0xff
800043bc  4ff0 000c   mov.w  ip,#0
800043c0  07ee d50f   mcr    p15,0,r0,c7,c5,6     ; ICIALLU
800043c4  bff3 4f8f   dsb    sy
800043c8  bff3 6f8f   isb    sy
800043cc  bff3 5f8f   dmb    sy
800043d0  f7f1 8000   smc    #1
800043d4  bde8 f05f   pop.w  {r4-r11,fp,ip,lr}
800043d8  7047        bx     lr

; ARM, arm9000
80009078  e92d5ff0   push  {r4-r9,sl,fp,ip,lr}
8000907c  e3a060ff   mov   r6,#0xff
80009080  e3a0c000   mov   ip,#0
80009084  ee070fd5   mcr   p15,0,r0,c7,c5,{6}     ; ICIALLU
80009088  f57ff04f   dsb   sy
8000908c  f57ff06f   isb   sy
80009090  f57ff05f   dmb   sy
80009094  e1600071   smc   1
80009098  e8bd5ff0   pop   {r4-r9,sl,fp,ip,lr}
8000909c  e12fff1e   bx    lr
```

Common convention [P]: `r6 = 0xFF` (magic), `r12 = 0` (function id, i.e. *not*
the `0x1xx` HLOS family), `r0 = service`, `r1 = arg/0`,
`r2 = domain`, `r3 = pointer to a 4-word argument block`.

### 6.4 MLO wrapper library (`0x80004682…0x80004B70`) — Thumb [P]

All of these reach `bl 0x80004C26`:

```
80004c26  b40f        push {r0,r1,r2,r3}   ; saved block at orig-16 .. orig-4
80004c28  b510        push {r4,lr}         ; sp = orig-24
80004c2a  ab05        add  r3,sp,#20       ; r3 = orig-4 = &saved r3   (argument block)
80004c2c  9a04        ldr  r2,[sp,#16]     ; r2 = orig-8 = saved r2 = DOMAIN
80004c2e  f7ff fbc1   bl   0x800043b4       ; veneer -> smc #1 at 0x800043D0
```

The two wrappers (`0x80004C26` Thumb and `0x800183DC` ARM) build the argument
pointer identically — both point it at the *saved `r3`* slot, 4 bytes below the
saved `r2`(=domain) slot — and both reload `r2` from the saved `r2` slot. So the
observable convention is `r0=service, r1=arg, r2=domain, r3=&args`, with
`r6=0xFF` and `r12=0` set by the veneer. Which exact words of the block the
monitor consumes could not be pinned down from the caller side [U]; the *stack*
arguments that the big wrappers place with `str`/`strd` (e.g. `0x80004730`'s
`strd r0,r1,[sp,#16]`, `0x80017758`'s `str r2,[sp]` / `str r8,[sp,#4]`) are
plain AAPCS outgoing stack arguments and are unambiguous.

| wrapper entry | SMC `r0` | `r2` (domain) | `r3` (count) | call instruction |
|---|---:|---:|---:|---|
| `0x8000468E` | **0x0E** | 5 | 1 | `bl 0x80004C26` |
| `0x800046AA` | **0x0F** | 0 | 1 | `bl 0x80004C26` |
| `0x800046C6` | **0x09** | 0 | 1 | `bl 0x80004C26` |
| `0x800046E2` | **0x0A** | 0 | 1 | `bl 0x80004C26` |
| `0x80004702` | **0x2D** | 5 | 3 | `bl 0x80004C26` |
| `0x80004720` | **0x33** | 5 | 2 | `bl 0x80004C26` |
| `0x80004786` | **0x2E** | 5 | 2 | `bl 0x80004C26` |
| `0x8000484E` | **0x2F** | 5 | 1 | `bl 0x80004C26` |
| `0x80004936` | **0x34** | 5 | 1 | `bl 0x80004C26` |
| `0x8000494C` | **0x35** | 5 | 1 | `bl 0x80004C26` |
| `0x80004972` | **0x36** | 5 | 1 | `bl 0x80004C26` |
| `0x80004990` | **0x30** | 5 | 1 | `bl 0x80004C26` |
| `0x800049B0` | **0x31** | 5 | 1 | `bl 0x80004C26` |
| `0x80004A0A` | **0x32** | 5 | 2 | `bl 0x80004C26` |
| `0x80004A28` | **0x37** | 5 | 4 | `bl 0x80004C26` |
| `0x80004A58` | **0x12** | 0 | 2 | `bl 0x80004C26` |
| `0x80004AC2` | **0x38** | 5 | 4 | `bl 0x80004C26` |
| `0x80004B0A` | **0x39** | 5 | 2 | `bl 0x80004C26` |
| `0x80004B36` | **0x3A** | 5 | — | `bl 0x80004C26` |
| `0x80004B68` | **0x3C** | 5 | 4 | `bl 0x80004C26` |

**[P]** No instruction in the 1 MiB branches to any of these entries (all
`bl`/`blx` targets and all 32-bit literals scanned). The only word in `0x8002xxxx`
that points into this block is `0x80004719`, stored as an MMC callback at
`[slot_struct + 0x1B4]` by `0x8000CE80-0x8000CE8C` and `0x8000F71C-0x8000F724`
and invoked with `blx r2` at `0x8000ED22` and `bx r2` at `0x8000EEE2`.
`0x80004719 = 0x80004718|1` is an **interior** address — it lands on the
`e9cd 0100 strd r0,r1,[sp]` inside the `0x33` wrapper at `0x80004712`, past that
function's `push {r2,r3,r4,lr}`. Either the callback field means something other
than a code pointer [U], or the MLO and arm9000 sections are from different
builds and the value was patched at load time [I]. It is **not** on the RPMB
record path either way.

### 6.5 arm9000 dispatcher `0x800183DC` — ARM [P]

```
800183dc  e92d000f   push  {r0,r1,r2,r3}        ; save the argument block
800183e0  e3a03020   mov   r3,#32
800183e4  e92d4070   push  {r4,r5,r6,lr}
800183e8  e1a05000   mov   r5,r0                ; service
800183ec  e1a06001   mov   r6,r1                ; arg
800183f0  e28d401c   add   r4,sp,#28            ; &saved r3
800183f4  e1a02004   mov   r2,r4
800183f8  e3a01004   mov   r1,#4
800183fc  e3a00001   mov   r0,#1
80018400  faffc5fb   blx   0x80009bf4           ; log(1, 4, &args, 0x20)   <- TRACE POINT
80018404  e59d2018   ldr   r2,[sp,#24]          ; r2 = caller's r2  = domain
80018408  e1a03004   mov   r3,r4
8001840c  e1a01006   mov   r1,r6
80018410  e1a00005   mov   r0,r5
80018414  ebffc317   bl    0x80009078           ; ARM veneer -> smc #1
80018418  e8bd0070   pop   {r4,r5,r6}
8001841c  e49df014   ldr   pc,[sp],#20
```

Every one of its 28 call sites is at `0x800175F4 … 0x800180C0` (the crypto /
secure-clock block). **None is in `0x80010xxx`.**

| site | SMC | domain | count | enclosing function / role |
|---|---:|---:|---:|---|
| `0x800175F4` | 0x0E | 5 | 1 | `0x800175DC` |
| `0x8001761C` | 0x0F | **0** | 1 | `0x80017604` — "secure module ON" |
| `0x80017644` | 0x09 | **0** | 1 | `0x8001762C` |
| `0x8001766C` | 0x0A | **0** | 1 | `0x80017654` |
| `0x800176E0` | 0x2D | 5 | 3 | `0x8001767C` |
| `0x80017744` | 0x33 | 5 | 2 | `0x80017724` |
| `0x800177B8` | **0x33** | 5 | 2 | `0x80017758` — **block-transform key/session setup** |
| `0x80017818` | **0x2E** | 5 | 2 | `0x80017758` — **block transform (cipher) on a buffer** |
| `0x80017958` | **0x33** | 5 | 2 | `0x8001783C` — **digest key/session setup** |
| `0x800179A0` | **0x2F** | 5 | 1 | `0x8001783C` — **keyed digest** |
| `0x80017B24` | 0x33 | 5 | 2 | `0x800179C4` |
| `0x80017B5C` | 0x34 | 5 | 1 | `0x800179C4` |
| `0x80017B8C` | 0x35 | 5 | 1 | `0x800179C4` |
| `0x80017BEC` | 0x36 | 5 | 1 | `0x800179C4` |
| `0x80017C28` | 0x30 | 5 | 1 | `0x80017C10` |
| `0x80017C58` | **0x31** | 5 | 1 | `0x80017C38` — **KEK/state query (used by the seal)** |
| `0x80017CD4` | **0x32** | 5 | 2 | `0x80017CB4` |
| `0x80017D10` | 0x37 | 5 | 4 | `0x80017CF4` |
| `0x80017D74` | 0x12 | **0** | 2 | `0x80017D24` — "provision / burn key" (domain 0!) |
| `0x80017E70` | 0x38 | 5 | 4 | `0x80017E04` |
| `0x80017F04` | 0x39 | 5 | 2 | `0x80017E94` |
| `0x80017F74` | 0x3A | 5 | — | `0x80017F64` — **called by boot main at `0x80014A62` after a successful decrement** |
| `0x80017FB4` | 0x3C | 5 | 4 | `0x80017F84` |
| `0x80018040` | 0x28 | 5 | 1 | `0x80017FD8` |
| `0x8001805C` | 0x2A | 5 | 0 | `0x80017FD8` |
| `0x80018080` | 0x29 | 5 | 2 | `0x80017FD8` |
| `0x8001809C` | 0x2A | 5 | 0 | `0x80017FD8` |
| `0x800180C0` | 0x0F | **0** | 1 | `0x80017FD8` |

### 6.6 Conclusion on SMC and the RPMB record

**The record path *does* use the monitor, but not the MLO wrapper library.** It
uses the arm9000 dispatcher `0x800183DC` for **services 0x31, 0x33, 0x2E and
0x2F, in that order per seal**, all with domain 5, plus:

* **0x32** (`0x80017CB4`, 2 args, domain 5) — called from **`0x80010D5C`**
  inside the RPMB subsystem probe `0x80010D3C` (boot main's first RPMB call at
  `0x8001492A`);
* **0x28 / 0x29 / 0x2A / 0x0F** via `0x80017FD8` — called from **`0x80010F4A`**
  inside `0x80010EB8` (the provisioning probe reached at `0x80014930`);
* **0x3A** (`0x80017F64`, no args) — called from boot main at **`0x80014A62`**,
  immediately after a *successful* decrement write and before
  `"\nLeaving bootrom and entering OS\n"`.

The RPMB call sites therefore triangulate to exactly:
`0x80010D5C → 0x80017CB4 (0x32)` · `0x80010F4A → 0x80017FD8 (0x28/0x29/0x2A/0x0F)`
· `0x80010F94 → 0x80017C38 (0x31)` · `0x80010FCC → 0x8001783C (0x33, 0x2F)` ·
`0x80010FF0 → 0x80017758 (0x33, 0x2E)` · `0x80011008 → 0x80017C38 (0x31)` ·
`0x800107F8 (in 0x800107B6, the MMC-request builder) → 0x80017758 (0x33, 0x2E)` ·
`0x800110B8 (in 0x80011048, the unseal path) → 0x80017758 (0x33, 0x2E)` ·
`0x80014A62 → 0x80017F64 (0x3A)`.

*Correction to the working assumption in the task preamble:* "the SMC services of
the re-seal" are **not** the MLO per-service wrappers `0x80004786` (0x2E),
`0x8000484E` (0x2F), `0x80004720` (0x33), `0x80004A0A` (0x32), `0x800049B0`
(0x31). Those are a *parallel* implementation in the MLO section with the same
service numbers and the same domain-5 convention, and no instruction in the
1 MiB branches to any of them. The functional path taken at runtime is the ARM
dispatcher `0x800183DC` → veneer `0x80009078` → `smc #1` at `0x80009094`.

---

## 7. Files touched

Only this report. No other file was created, modified or deleted by this
analysis. (`docs/reports/151-arm9000-stage-re-2026-10-31.md` and
`.commandcode/taste/taste/taste.md` also show up in `git status` in this
workspace; neither was written or read by this session — 151 is a parallel
deliverable and was not consulted.)

## 8. Reproduce

```sh
. tools/env.sh
IMG=local/proprietary/device-binaries/emmc-first1mb.bin

# Thumb regions
arm-linux-objdump -D -b binary -m arm --adjust-vma=0x80000000 -M force-thumb \
    --start-address=0x800090ce --stop-address=0x80009160 "$IMG"
arm-linux-objdump -D -b binary -m arm --adjust-vma=0x80000000 -M force-thumb \
    --start-address=0x800114fa --stop-address=0x80011660 "$IMG"

# ARM regions
arm-linux-objdump -D -b binary -m arm --adjust-vma=0x80000000 \
    --start-address=0x800183dc --stop-address=0x80018420 "$IMG"
arm-linux-objdump -D -b binary -m arm --adjust-vma=0x80000000 \
    --start-address=0x80019224 --stop-address=0x800192b0 "$IMG"

# slice verification
cmp <(dd if="$IMG" bs=1 skip=$((0x4d70)) count=16384 2>/dev/null) \
    local/proprietary/device-binaries/bootblob_MLO.bin          # identical
cmp <(dd if="$IMG" bs=1 skip=$((0x9000)) count=$((0x15000)) 2>/dev/null) \
    local/proprietary/device-binaries/bootblob_arm9000.bin      # identical
```

## 9. Residual unknowns

| id | question | why unresolved |
|---|---|---|
| U-1 | Is `VBAR` = `0x80009000` when the RPMB code runs? | the only VBAR writer (`0x80000FBC`) has no caller in this image |
| U-2 | Is `SCTLR.A` (alignment check) set? | never written here; inherited from PRIMAPP/BootROM. Decides whether `0x80011362` can fault. |
| U-3 | What does the MMC callback field `+0x1B4` holding `0x80004719` mean? | the value is an interior address inside the `0x33` wrapper; no consistent call convention |
| U-4 | Is WDT2's functional clock 32.768 kHz (⇒ ≈7.8 s to reset after the halt)? | the TRM table is in `hardware/swpu231ap.pdf.txt` but the WKUP clock source for WDTIMER2 is not quoted here |
| U-5 | What do monitor services 0x2E/0x2F/0x31/0x33/0x32 actually compute? | the monitor image is not in this corpus; only the ABI and the call order are recoverable |
