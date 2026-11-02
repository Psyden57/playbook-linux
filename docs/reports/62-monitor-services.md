# Monitor (SMC) services `0xF0`, `0xFE`, `0xF1` — structural identification

Status: classification of the three service IDs left open in `HANDOFF.md`.
Date: 2026-10-19. Target: `omap4430-bootrom-0x40028000.bin`
(SHA-256 `fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`).

All disassembly and byte values are **LLVM 23-derived observations**, not REA
Evidence. The secure-side dispatcher is not present in either readable dump, so
the *semantics* below are explicitly separated from the *structure*. TRM
citations are public-document context (`swpu231ap.pdf`), not target evidence.

## 1. What the TRM establishes

TRM §27.5 "Services for HLOS Support" (pp. 5703-5708) says the ROM provides
services "on GP devices for L2 cache maintenance, wake up of slave CPU(s),
etc.", implemented in monitor mode: **the function ID is written to `R12` and
the `SMC` instruction is used**. The example is:

```asm
PUSH {R1-R12, LR}
LDR  R12, =API_MONITOR_L2CACHE_SETDEBUG_INDEX
SMC  0x1
POP  {R1-R12, PC}
```

Documented function IDs are `0x100`–`0x113` (L2 debug/control, AUX_CORE_BOOT,
WKG_CONTROL, SCU power, TLB lockdown, PL310 aux/prefetch). The TRM does **not**
document `0xF0`, `0xFE`, or `0xF1`.

The TRM's ROM architecture figure also names a **`SEC_ENTRY`** module alongside
`IRQ`, `CLOCKS`, and `BOOTING` (Figure 27-5), so a security-entry component is
expected even though its services are not listed.

## 2. The eight SMC sites in this window

Sweep: `(word & 0x0FFFFFF0) == 0x01600070` over every 4-byte-aligned offset, plus
a Thumb `0xDFxx` sweep over every byte offset. Result: **8 A32 SMC sites, no
Thumb SMC** in either dump.

| # | address | encoding | `r12` | enclosing path | live? |
|---:|---|---|---:|---|---|
| 1 | `0x40028134` | `smc #0` | `0x107` | CPU1 wake: clear `WKG_CONTROL` | yes |
| 2 | `0x4002813C` | `smc #0` | `0x103` | CPU1 wake: read `AUX_CORE_BOOT_0/1` | yes |
| 3 | `0x400289C8` | `smc #0` | `0x107` | CPU1 secondary path | yes |
| 4 | `0x4002CEB8` | `smc #0` | `0x0F0` | ARM veneer `0x4002CEB0` | callback, indirect |
| 5 | `0x4002F4C8` | `smc #1` | `0x000` | cache wrapper A `0x4002F4AC` | yes |
| 6 | `0x4002F4E8` | `smc #1` | `0x0FE` | wrapper A alt entry `0x4002F4D0` | **no reference found** |
| 7 | `0x4002F510` | `smc #1` | `0x000` | cache wrapper B `0x4002F4F4` | yes |
| 8 | `0x4002F530` | `smc #1` | `0x0F1` | wrapper B alt entry `0x4002F518` | **no reference found** |

The `0x103`/`0x107` services match the TRM table exactly. `0xF0` uses `SMC #0`,
not `SMC #1`, so it is not one of the documented HLOS monitor services. `0xFE`
and `0xF1` use `SMC #1` but are outside the documented `0x100`–`0x113` range.

## 3. The cache wrappers and their selector ABI

### 3.1 Wrapper A (`0x4002F4AC`)

```
4002f4ac  e92d5ff0   push {r4-r12,lr}
4002f4b0  e3a060ff   mov  r6,#0xff
4002f4b4  e3a0c000   mov  r12,#0
4002f4b8  ee070fd5   mcr  p15,0,r0,c7,c5,6     ; BPIALL (r0 operand ignored)
4002f4bc  f57ff04f   dsb  sy
4002f4c0  f57ff06f   isb  sy
4002f4c4  f57ff05f   dmb  sy
4002f4c8  e1600071   smc  #1                  ; service r12=0
4002f4cc  ea000006   b    0x4002f4ec          ; epilogue

4002f4d0  e320f000   nop                       ; alt entry, no push
4002f4d4  e3a0c0fe   mov  r12,#0xfe
4002f4d8  ee07cfd5   mcr  p15,0,r12,c7,c5,6   ; BPIALL again (operand ignored)
4002f4dc  f57ff04f   dsb  sy
4002f4e0  f57ff06f   isb  sy
4002f4e4  f57ff05f   dmb  sy
4002f4e8  e1600071   smc  #1                  ; service r12=0xFE
4002f4ec  e8bd5ff0   pop  {r4-r12,lr}
4002f4f0  e12fff1e   bx   lr
```

Wrapper B (`0x4002F4F4`) is byte-for-byte the same pattern with `r12=0`
(`0x4002F510`) and an alt entry `r12=0xF1` (`0x4002F530`).

Three observations matter:

1. **`r12` is the SMC service ID**, not a cache operand. The TRM says the
   function ID goes in `R12`; the alternate entries put `0xFE`/`0xF1` there.
   The `mcr ... c7,c5,6` is `BPIALL`, whose data register is architecturally
   ignored, so passing `r12` to it has no effect. Any earlier suggestion that
   `r12=0` is "merely the BPIALL operand" is therefore wrong: `r12=0` is the
   live service selector.
2. **The alt entries skip the `push`.** `0x4002F4D0`/`0x4002F518` begin after the
   prologue and fall into the epilogue (`pop {r4-r12,lr}`). They are only usable
   if a caller has already built the matching frame; they are not standalone
   functions. Combined with the absence of any branch, literal, or BL/BLX
   reference to either address, they are **dormant from this ROM**.
3. `r6=0xff` is set by both main entries. This matches the public Linux
   `omap-smc.S` convention for `omap_smc3` (PPA services: `r6=0xff`, service ID
   in `r12`, `smc #1`). It is corroborating context, not target evidence.

### 3.2 Live callers

The only references to the wrappers are Thumb calls from the argument-marshalling
block at `0x40028F04`/`0x40028F16`:

```
40028f04  push {r0-r3}
40028f06  push {r4,lr}
40028f08  add  r3,sp,#0x14      ; &saved r3 (4th arg pointer)
40028f0a  ldr  r2,[sp,#0x10]    ; 3rd arg
40028f0c  blx  0x4002f4ac       ; wrapper A
40028f10  pop  {r4}
40028f12  ldr  pc,[sp],#20

40028f16  push {r0-r3}
40028f18  push {r4,lr}
40028f1a  add  r2,sp,#0x10      ; &saved r2
40028f1c  ldr  r1,[sp,#0xc]     ; 2nd arg
40028f1e  blx  0x4002f4f4       ; wrapper B
```

Thus wrapper A takes `(r0, r1, r2, *r3)` and wrapper B takes `(r0, r1, *r2,
r3)`. Small stubs select the wrapper and the argument count:

| stub | selector `r0` | wrapper |
|---|---:|---|
| `0x40028F24` | 0 | A |
| `0x40028F36` | 1 | A |
| `0x40028F48` | 2 | A |
| `0x40028F5A` | 3 (two args) | A |
| `0x40028F6E` | 9 | A |
| `0x40028F80` | 0xA | A |
| `0x40028F92` | 0xB | A |
| `0x40028F9C` | 3 (record arg) | B |
| `0x40028FA6` | 6 | B |
| `0x40028FAC` | 1 (pointer arg) | B |

Every live path uses `r12=0`; the `r0` values above are service arguments, not
SMC IDs. `0x4002F4D0` and `0x4002F518` are never selected.

## 4. `0xF0` — terminal secure image-load entry

### 4.1 Veneer and selector helper

```
4002ceb0  fafffff6   blx  0x4002ce90       ; ARM; r0 = selector/argument
4002ceb4  e3a0c0f0   mov  r12,#0xf0
4002ceb8  e1600070   smc  #0
4002cebc  220048f9   ...                   ; Thumb resumes here
```

There is **no `bx lr`** and the fall-through bytes are Thumb, so the ARM veneer
has no return path of its own.

Helper `0x4002CE90` (Thumb) is an index resolver:

```
4002ce90  subs r1,r0,#0
4002ce92  mov.w r0,#0
4002ce96  ble  0x4002ce9c        ; selector <= 0 -> return 0
4002ce98  cmp  r1,#2
4002ce9a  blt  0x4002cea0        ; selector == 1
4002ce9c  movs r0,#0
4002ce9e  bx   lr               ; selector >= 2 -> return 0
4002cea0  cmp  r1,#1
4002cea2  bne  0x4002ce9e
4002cea4  ldr  r0,[pc,#0]        ; = 0x4030DC1C (literal at 0x4002CEA8)
4002cea6  b    0x4002ce36        ; dispatch on descriptor[0]
```

So the veneer only does useful work when `r0 == 1`; it then tail-calls the
descriptor dispatcher with `r0 = 0x4030DC1C`.

### 4.2 Descriptor dispatcher `0x4002CE36`

```
4002ce36  push {r2,r3,r4,lr}
4002ce38  cbz  r0, fail
4002ce3a  ldr  r2,[r0]            ; type = descriptor[0]
4002ce3c  cmp  r2,#0
4002ce3e  ble  fail
4002ce40  cmp  r2,#4
4002ce42  bge  fail               ; valid types: 1,2,3
...
type 1 (0x4002CE68):
  r2=[desc+0x14], r3=[desc+0x18] -> stack args
  r3=[desc+4], r1=[desc+8], r2=[desc+0xC]
  r0 = 0x45 ('E')
  bl  0x4002CDAA                 ; image operation 0x45
type 2 (0x4002CE82):
  r3=[desc+4] -> stack
  r2=[desc+8], r3=[desc+0xC], r1=[desc+0x10]
  r0 = 6
  bl  0x4002CD1E                 ; media command 6
type 3 (0x4002CE56):
  same marshalling
  r0 = 4
  bl  0x4002CD1E                 ; media command 4
```

`0x4002CDAA` resolves image op `0x45` through `0x40028E4A` and then runs the
image record's open/read callbacks (`0x4002CDAA`–`0x4002CE28`). Op `0x45` is the
same `'E'` entry used by the image handler `0x4002AFFE` (vector table
`0x4002F5E4`; `'E'` → `0x4002C03F` family). `0x4002CD1E` is the media-command
dispatcher used by the same image path (commands 4 and 6 are the flash/MMC read
classes).

### 4.3 The descriptor object

`0x4002CD10` hands out a single static descriptor:

```
4002cd10  cbnz r0,0x4002cd16
4002cd12  movs r0,#1
4002cd14  bx   lr                 ; NULL input -> error
4002cd16  ldr  r1,[pc,#0x190]     ; = 0x4030DC1C (literal at 0x4002CEA8)
4002cd18  str  r1,[r0]
4002cd1a  movs r0,#0
4002cd1c  bx   lr
```

`0x4030DC1C` is in the IRAM static-variable area (TRM Figure 27-7: static
variables live at the top of `0x4030D000`–`0x4030DFFF`). It is a shared request
object, not a per-call allocation.

### 4.4 Installation sites and the image range

The veneer address is never a branch target. It is built into a two-word
record `{fn = 0x0002CEB0, arg = 0x4030DC1C}` at two sites:

- `0x4002AC10`–`0x4002AC2C` (Thumb): obtain the descriptor via `0x4002CD10`,
  zero descriptor fields `+0`, `+8`, `+0xC`, `+0x10` (`r8=0`), build
  `{0x0002CEB0, 0x4030DC1C}` at `sp+4`, then call `0x4002C584`. Because
  `descriptor[0]` (the dispatcher's type field) is zero here, this path cannot
  be invoking `0xF0` immediately: the type must be filled by the secure side or
  a later stage. That supports reading service `0` as a **registration/hand-off**
  rather than an immediate call.
- `0x4002C5F6`–`0x4002C61C` (Thumb): obtain the descriptor, build a larger
  structure `{0x0002CEB0, 0x4030DC1C, 0x40304000, 0x40304000, arg}` at
  `sp+0x10`, then call `0x4002C584`.

Adjacent literals in the same region bound the image buffer and the failure
path:

```
0x4002AE44  0x40304000     ; image base
0x4002AE48  0x4030BFFF     ; image end (inclusive)
0x4002AE4C  0x000280A8     ; dead-loop stub (0x280A8)
0x4002C6B4  0x000280A8
0x4002C6B8  0x40304000
```

TRM Figure 27-7 places the downloaded-image area at `0x40300000`–`0x4030BFFF`
(stack at `0x4030C000`), so `0x40304000` is an offset into that documented image
area, not an arbitrary buffer.

`0x4002C584` validates its arguments and then calls `0x40028F9C`, which sets
`r0=3, r1=0, r2=caller context, r3=record` and tail-branches to wrapper B:

```
4002c584  ...
4002c5be  mov  r1,r6           ; record
4002c5c0  mov  r0,r5
4002c5c2  bl   0x40028f9c      ; r0=3,r1=0,r2=context,r3=record -> wrapper B

40028f9c  mov  r3,r1
40028f9e  movs r1,#0
40028fa0  mov  r2,r0
40028fa2  movs r0,#3
40028fa4  b    0x40028f16      ; wrapper B: SMC #1, r12=0
```

So monitor service `0` receives `r0=3`, `r1=0`, `r2=<caller context/handle
passed to 0x4002C584>`, and `r3=record`. The record's first word is the `0xF0`
veneer and its second word is the static descriptor `0x4030DC1C`; the caller
context in `r2` is a separate request object, not the static descriptor.

### 4.5 Classification and confidence

**Observed (high confidence):**

- `0xF0` is an `SMC #0` entry, not `SMC #1`; it is not in the TRM's HLOS table.
- It is reached only through a two-word callback record
  `{0x4002CEB0, 0x4030DC1C}` passed to service `0`.
- Its helper dispatches a request descriptor by type:
  type 1 → image operation `0x45` (`'E'`), types 2/3 → media commands 6/4.
- The surrounding data names the IRAM image range
  `0x40304000`–`0x4030BFFF` and the `0x280A8` hang stub.
- The veneer has no `bx lr`; it is terminal in the ARM instruction stream.

**Inferred (medium confidence):**

- `0xF0` is the **secure image-load / hand-off stage**: the public ROM prepares
  a descriptor that selects a media/image read into IRAM `0x40304000`–
  `0x4030BFFF`, registers the `0xF0` callback with the monitor via service `0`,
  and the monitor later enters `0xF0` to fetch/verify/execute the image. The
  descriptor-driven image op `0x45` and the media commands 6/4 are the read
  path; the "execute" part is not directly observable.
- The absence of a return path and the `SMC #0` convention suggest the monitor
  either never returns to the caller or unwinds the saved context itself. Which
  one is true cannot be decided without the secure bytes.

**Not claimed:** any specific secure-side verification (signature, CRC, keys) or
the exact meaning of descriptor fields `+4`, `+8`, `+0xC`, `+0x10`, `+0x14`,
`+0x18`. Those are request parameters, not decoded yet.

## 5. `0xFE` and `0xF1` — dormant alternate monitor selectors

**Observed (high confidence):**

- `0xFE` (`0x4002F4D0`, site `0x4002F4E8`) and `0xF1` (`0x4002F518`, site
  `0x4002F530`) are alternate entries in wrappers A/B, after `BPIALL` +
  `DSB/ISB/DMB`, with `smc #1` and `r12 = 0xFE`/`0xF1`.
- They are **not standalone functions**: they skip the `push {r4-r12,lr}` and
  fall into its matching `pop`.
- A whole-file scan finds **no** literal, word, halfword, ARM branch, or Thumb
  BL/BLX reference to `0x4002F4D0`, `0x4002F4D1`, `0x4002F518`, or
  `0x4002F519` in either dump. The only live wrapper entries are
  `0x4002F4AC` (from `0x40028F0C`) and `0x4002F4F4` (from `0x40028F1E`).
- Every live call uses `r12=0`; `0xFE`/`0xF1` are never selected by this ROM.

**Inferred (low–medium confidence):**

- They are **secure-side cache/branch-predictor or PPA maintenance selectors**,
  kept as alternate entry points in the shared wrapper. The local `BPIALL` +
  barriers immediately before the SMC is consistent with cache maintenance, and
  `r6=0xff`/`smc #1` matches the public `omap_smc3` PPA convention. However, the
  SMC itself could implement an unrelated secure operation; the BPIALL only
  establishes the caller-side context.
- Because the alt entries are not callable without a caller-built frame, they
  may be dead compiler output or entry points intended for a later boot stage
  that supplies its own frame. There is no evidence in this ROM for the latter.

**Not claimed:** that `0xFE`/`0xF1` are L2 cache operations specifically. The
TRM's documented L2 services are `0x100`–`0x113`; `0xFE`/`0xF1` are undocumented
and their secure-side effect is unobservable here.

## 6. Corrections to earlier statements

| Earlier statement | Correction |
|---|---|
| "`smc #1` selectors `0x00`/`0x0FE`/`0x0F1` = secure-side cache/branch-predictor/L2 maintenance; `r12=0` may merely be the BPIALL operand" (`BOOTROM` §6.2) | `r12` is the SMC function ID per TRM §27.5. The live selector is `0`; `0xFE`/`0xF1` are alternate, unreferenced selectors. The BPIALL data register is ignored, so `r12=0` is not a cache operand. |
| "`smc #0` `0xF0` veneer = image-load/verify callback" | Structure confirmed and refined: descriptor-driven image op `0x45` / media commands 6/4, image range `0x40304000`–`0x4030BFFF`, installed as `{fn,arg}` and passed to service `0`. "Verify" and "execute" remain inferred. |
| "`0xF0` has no return path" | Confirmed in the ARM stream: no `bx lr`, next bytes are Thumb. Whether the monitor unwinds or never returns is unobservable. |
| "4 SMC sites" (prior `BOOTROM-RE.md`) | 8 in this window; `0xFE`/`0xF1` sites are dormant but present. |
