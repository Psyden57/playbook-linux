# 30 — Monitor (SMC) usage and security-related CP15 configuration

Target: `/home/psyden/rea-work/bootdumps-2026-09-11/omap4430-bootrom-0x40028000.bin`
(49152 B, base `0x40028000`, SHA-256 `fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`).

Separate evidence (used only where explicitly marked): `bootrom-lower-0x40020000.bin`
(28456 B, base `0x40020000`, covers `0x40020000-0x40026F24`).

> Update 2026-10-19: the `0xF0`/`0xFE`/`0xF1` service structure and the
> `r12`-is-the-function-ID correction are resolved in
> `62-monitor-services.md`. In particular, `r12` is **not** a BPIALL operand,
> the `0xFE`/`0xF1` entries skip the wrapper prologue and are unreferenced, and
> `0xF0` is a terminal `SMC #0` descriptor-driven image-load/hand-off entry.
> The 4-site SMC count in older work is superseded by the 8-site inventory here.

Method: LLVM 23 (`llvm-mc` -> raw-binary ELF wrappers, `llvm-objdump` ARM/Thumb-2
listings) plus Python over the raw bytes. **All observations below are LLVM-derived,
not REA Evidence** (no native analysis provider is configured on this host). Prior
work (`README.md`, `BOOTROM-RE.md`, `analysis/NOTES.md`) is treated as claims to verify.

---

## 1. SMC site inventory

### 1.1 Sweep method and completeness

* **A32 encoding**: `(word & 0x0FFFFFF0) == 0x01600070`.
  * `SMC #0` = `0xE1600070` (imm4 = 0); `SMC #1` = `0xE1600071` (imm4 = 1).
  * Swept every 4-byte-aligned offset of both files. Result: **8 sites in the
    0x40028000 window, 1 site in the lower window** — exhaustive for aligned A32.
* **Thumb SMC**: halfword bank `(h & 0xFF00) == 0xDF00`.
  * Swept *every* byte offset: all `0xDFxx` matches land on **odd** offsets
    (`0x40028B5B`, `0x40028F43`, `0x40029607`, `0x40029AED`, `0x40029CA7`,
    `0x4002A961`, `0x4002B0D9`, `0x4002B341`, `0x4002B373`, `0x4002B37D`,
    `0x4002BCA5`, `0x4002C271`, `0x4002D85B`, `0x4002DDDD`, `0x4002DE63`,
    `0x4002DFAF`, `0x4002E763`, `0x4002E823`, `0x4002E96F`, `0x4002E97B`,
    `0x4002ED87`) plus one in the ARM region (`0x4002841B`).
    Every one is the low half of a 32-bit instruction at an unaligned offset.
    **There are no Thumb-mode SMC instructions in either file** (a halfword-aligned
    sweep over `range(0, len-1, 2)` returns zero hits). This confirms the prior
    claim in `BOOTROM-RE.md`.
* **`r12` selection**: the SMC operand is always `r12`, written immediately before.
  ARM sites use `movw r12,#imm` (encoding `0xE30C_xxxx`) except the stub, which
  uses `mov r12,#0xF0` (`0xE3A0C0F0`). The `0x4002F4xx`
  sites use `mov r12,#imm`. No `movw/movt` pair, no `ror`, no `ldr`-loaded service
  IDs. No other write to `r12` feeds an SMC.

### 1.2 The eight SMC sites in the 0x40028000 window

| # | Address | Enc | `r12` set by | `r12` | Mode / function | Context |
|---|---------|-----|--------------|-------|-----------------|---------|
| 1 | `0x40028134` | `smc #0` | `movw r12,#0x107` | **0x107** | ARM `0x40028100` CPU1-wake path A | `mrc p15,c0,c0,5` (MIDR) → `and #3`; WFE loop polling `[0x48281400]` bit 0x200; `r0=1`, `r1=0x200`; call; then `movw r12,#0x103` |
| 2 | `0x4002813C` | `smc #0` | `movw r12,#0x103` | **0x103** | ARM, same path, chained | reads AUX_CORE_BOOT_0/1 into `r0/r1`; `cmp r0,#0`; on `r0!=0` `mov r10,r1`, `movs r0,#1`, `b 0x400281C0` |
| 3 | `0x400289C8` | `smc #0` | `movw r12,#0x107` | **0x107** | ARM `0x400289A0` CPU1 secondary entry | reached from `0x40028918` when `MIDR[1:0]!=0`; polls `[0x48281400+0x400]` bit 0x200; `r0=1`,`r1=0x200`; then reads `[+0x800]` (non-zero test) and `[+0x804]` → `bx r1` |
| 4 | `0x4002CEB8` | `smc #0` | `mov r12,#0xF0` | **0x0F0** | ARM veneer `0x4002CEB0` (inside Thumb region) | see §1.3 |
| 5 | `0x4002F4C8` | `smc #1` | `mov r12,#0` | **0x000** | ARM cache-lib wrapper A `0x4002F4AC` | BPIALL + DSB/ISB/DMB first; see §2 |
| 6 | `0x4002F4E8` | `smc #1` | `mov r12,#254` | **0x0FE** | ARM wrapper A alt-entry `0x4002F4D0` | idem |
| 7 | `0x4002F510` | `smc #1` | `mov r12,#0` | **0x000** | ARM cache-lib wrapper B `0x4002F4F4` | idem |
| 8 | `0x4002F530` | `smc #1` | `mov r12,#241` | **0x0F1** | ARM wrapper B alt-entry `0x4002F518` | idem |

Service-ID cross-check against the TRM table quoted in `BOOTROM-RE.md` §27.5:
`0x103` = Read AUX_CORE_BOOT_0/1, `0x107` = Clear WKG_CONTROL_0/1 bits — both match
sites #1–#3 exactly (site #2 keeps `r0/r1` as the `0x103` outputs). `0x0F0`, `0x0FE`,
`0x0F1` and `0x9` are **outside** the documented `0x100-0x113` HLOS table.

### 1.3 The ARM stub `0x4002CEB0-0x4002CEBC` and its caller

Exact bytes (verified from the file):

```
0x4002CEB0  f6 ff ff fa   -> ARM: blx 0x4002CE90
0x4002CEB4  f0 c0 a0 e3   -> ARM: mov r12, #0xF0
0x4002CEB8  70 00 60 e1   -> ARM: smc #0
0x4002CEBC  f9 48 00 22   -> Thumb resumes (ldr r0,[pc,#0x3e4] / movs r2,#0 ...)
```

The three words decode cleanly as ARM and as nonsense in Thumb (`<unknown>` /
`stm r0!`), while the bytes at `0x4002CEBC` decode cleanly only in Thumb
(`<unknown>` / `andcs`/`stmdavs` garbage in ARM). So `0x4002CEB0-0x4002CEBB` is a
deliberate **12-byte A32 veneer interleaved into Thumb code**.

* **`blx` target = `0x4002CE90` (Thumb helper).** The ROM's own state-switch calls
  are internally consistent: every ARM->Thumb `blx` in the window uses the same
  encoding form (bit 24 = 0) and the decoded targets are valid Thumb, invalid ARM —
  e.g. `0x400288D4 -> 0x400288C4`, `0x40028904 -> 0x400288CC`,
  `0x40028964 -> 0x4002A4D8`, `0x4002896C -> 0x4002A51C`, and this stub
  `0x4002CEB0 -> 0x4002CE90` (`0x4002CE90`: `subs r1,r0,#0` = `0x1E01`).
  (Caveat: the architectural "H bit" convention is the opposite of what LLVM
  displays for the sole bit-24=1 site `0x40028908 -> 0x4002F300/302`; only the
  stub's target matters here and it is unambiguous by decode validity.)

* **Helper `0x4002CE90`** (Thumb), a small *index -> pointer* resolver:
  ```
  if r0 == 0        -> r0 = 0 ; bx lr
  if r0 == 1        -> r0 = 0x4030DC1C ; b 0x4002CE36   (ldr from 0x4002CEA8)
  else (r0 >= 2)    -> r0 = 0 ; bx lr
  ```
  `0x4002CE36` then validates `[r0]` (must be 1..3) and dispatches — i.e. the stub
  is invoked with a small selector and passes a candidate pointer to the monitor.

* **Callers.** The veneer's address `0x0002CEB0` is *not* reached by any PC-relative
  `ldr`/`adr`. It is stored as an **indirect callback field** in two boot-media
  descriptor builders:
  * `0x4002AC18-0x4002AC2C` (Thumb): `ldr r0,[sp]`; `ldr r1,[pc]` = `0x0002CEB0`
    (pool at `0x4002AE50`); `strd r1,r0,[sp,#4]` — i.e. `{fn=0x2CEB0, arg}`.
  * `0x4002C610-0x4002C614` (Thumb): `ldr r1,[pc]` = `0x0002CEB0` (pool at
    `0x4002C6BC`); `strd r1,r0,[sp,#16]`.
  The two pools also carry `0x000280A8` (a `b .` hang stub), `0x40304000` and
  `0x4030BFFF` (an SRAM range), and `0x0002F9C4` (string data, `"@ABCD"`), so the
  veneer is one callback among several in an image-load/verification record.
  (Also note `0x4002C696 ld r0,=0x000280A8; blx 0x400280C0` — the warm-reset code
  at `0x400280C0` ends `bx r0`, deliberately returning into the `0x400280A8` hang.)

* **Open point:** the veneer has no `bx lr`; after `smc #0` execution falls into
  Thumb at `0x4002CEBC` while still in ARM state. Either the `0xF0` service is
  terminal (does not return) or the callback frame is unwound by the monitor.
  Flagged in §7.

*Lower window, separate evidence:* one extra SMC — `0x400260D8 smc #0`, `r12=9`
  (`mov r12,#9`), ARM, in the "secure handoff tail" `0x40026098-0x400260E8`.
  It is followed by `mov r11,#4080; str r11,[r12]` and an infinite `b .`.

---

## 2. ARM cache/TLB library `0x4002F2B0-0x4002F550`

One contiguous A32 library. All addresses below are ARM-mode. `mcr/mrc p15` names
are per the ARMv7-A A9 mnemonics; `c7,c6,2` = DCISW, `c7,c10,2` = DCCSW,
`c7,c5,1` = ICIMVAU, `c7,c5,0` = ICIALLU, `c7,c1,0` = ICIALLUIS, `c7,c6,1` = DCIMVAC,
`c7,c11,1` = DCCMVAU, `c7,c5,6` = BPIALL, `c8,c7,0` = TLBIALL, `c8,c3,1` = TLBIASID,
`c0,c0,1` = CCSIDR (op1=1), `c0,c0,0` op1=2 = CSSELR.

| Address | Routine | Operations |
|---------|---------|-----------|
| `0x4002F2B0` | read_unaligned_u32(r0) | 4x `ldrb` + ORR → `r0` |
| `0x4002F2D0` | write_u32(r0=val,r1=addr) | 4x `strb` |
| `0x4002F310` | invalidate_Dcache_setway | `mov r0,#0` ; `mcr p15,2,r0,c0,c0,0` (CSSELR=0) ; read CCSIDR ; loop `mcr p15,0,r2,c7,c6,2` (DCISW) over ways/levels |
| `0x4002F358` | invalidate_Icache_range(r0,r1) | CSSELR=1 ; CCSIDR ; loop `mcr p15,0,r0,c7,c5,1` (ICIMVAU) |
| `0x4002F384` | ICIALLU | `mcr p15,0,r0,c7,c5,0` ; `bx lr` |
| `0x4002F38C` | ICIALLUIS | `mcr p15,0,r0,c7,c1,0` ; `bx lr` |
| `0x4002F394` | invalidate_Dcache_range | CSSELR=0 ; CCSIDR ; loop `mcr p15,0,r0,c7,c6,1` (DCIMVAC) |
| `0x4002F3C0` | clean+invalidate_Dcache_range | loop `mcr c7,c6,1` (DCIMVAC) + `mcr c7,c5,1` (ICIMVAU) |
| `0x4002F3F0` | clean_Dcache_setway | CSSELR=0 ; loop `mcr p15,0,r2,c7,c10,2` (DCCSW) over sets/ways |
| `0x4002F438` | clean_Dcache_range | loop `mcr p15,0,r0,c7,c11,1` (DCCMVAU) |
| `0x4002F464` | dsb_stub | `dsb sy` ; `bx lr` |
| `0x4002F46C` | tlbi_asid(r0) | `mcr p15,0,r0,c8,c3,1` (TLBIASID) ; `bx lr` |
| `0x4002F474` | icache_enabled() | read SCTLR, `tst r1,#4096` (bit12 I) → 0/1 |
| `0x4002F488` | dcache_enabled() | read SCTLR, `tst r1,#4` (bit2 C) → 0/1 |
| `0x4002F49C` | BPIALL | `mcr p15,0,r0,c7,c5,6` ; `bx lr` |
| `0x4002F4A8` | literal | `0x0FFE0000` (CCSIDR NumSets-1 mask) |
| `0x4002F4AC` | **wrapper A** | see below |
| `0x4002F4F4` | **wrapper B** | see below |
| `0x4002F53C` | read MIDR | `mrc p15,0,r0,c0,c0,5` ; `and #3` |
| `0x4002F548` | read CPSR mask | `mrs r0,apsr` ; `and #0xC0` ; `cpsid if` |

### 2.1 The two `smc #1` wrappers

Wrapper A (`0x4002F4AC`), entered as a function; `0x4002F4D0` is a second entry
inside it:

```
4002F4AC: push {r4-r12,lr}            ; preserve r0..r3 (args) for the monitor
4002F4B0: mov  r6,#255
4002F4B4: mov  r12,#0
4002F4B8: mcr  p15,0,r0,c7,c5,6       ; BPIALL   (Rt ignored)
4002F4BC: dsb sy
4002F4C0: isb sy
4002F4C4: dmb sy
4002F4C8: smc  #1                     ; monitor, selector r12=0x00
4002F4CC: b    0x4002F4EC
4002F4D0: nop
4002F4D4: mov  r12,#254
4002F4D8: mcr  p15,0,r12,c7,c5,6      ; BPIALL (Rt=r12 reused as selector)
4002F4DC: dsb sy / isb sy / dmb sy
4002F4E8: smc  #1                     ; monitor, selector r12=0xFE
4002F4EC: pop  {r4-r12,lr}
4002F4F0: bx   lr
```

Wrapper B (`0x4002F4F4`) is byte-for-byte the same pattern with selectors
`r12=0` (`0x4002F510`) and `r12=0xF1` (`0x4002F530`).

**What they do locally:** 360-degree branch-predictor flush (`BPIALL`) plus a full
`DSB/ISB/DMB` barrier sequence, then fall through to `smc #1`.

**What they ask the monitor to do (inference):** an `smc #1` carrying a monitor
selector in `r12` with the caller's `r0-r3` preserved and passed through, issued
immediately after a local BPIALL+barriers. The natural reading is *the secure-side
counterpart of the same maintenance* — i.e. invalidate/flush the monitor's own
branch predictor / cache / TLB state (or an L2/PL310-level operation) that the
NS ROM cannot perform itself. `r12` values `0x000`, `0x0FE`, `0x0F1` sit outside
the documented `0x100-0x113` table, so they are a separate (secure/boot) service
group. It is a *strong* inference, not a decode.

**Callers** (Thumb veneers, `blx` register -> ARM, args copied from the stack):
* `0x40028F04`: `push {r0-r3}; push {r4,lr}; add r3,sp,#0x14; ldr r2,[sp,#0x10];
  blx 0x4002F4AC` → wrapper A, args (r0, r2, r3).
* `0x40028F16`: `push {r0-r3}; push {r4,lr}; add r2,sp,#0x10; ldr r1,[sp,#0xc];
  blx 0x4002F4F4` → wrapper B, args (r0, r1, r2).
Both are itself called from small wrappers at `0x40028F24`/`0x40028F36`.

### 2.2 An embedded Thumb block, `0x4002F2F0-0x4002F30C`

The library contains a *Thumb* island (valid Thumb, invalid ARM):

```
4002F2F0: movs r4,r0 ; nop.w ; movs r0,r4
4002F2F8: blx 0x40028904            ; to A32
4002F2FC: mov.w r0,#-1
4002F300: bx lr
4002F302: b .                       ; hang
4002F304: bx lr ; bx lr ; bx lr ; bx lr
```

It is a Thumb veneer into the A32 trampoline at `0x40028904` (which `blx`es the
Thumb no-op `0x400288CC` and then falls through `0x4002890C` into the CPU0
bring-up at `0x40028910`). It is distinct from the cache routines; do not
disassemble `0x4002F2F0-0x4002F30F` as ARM.

---

## 3. CP15 / security configuration

Full aligned A32 CP15 inventory of the 0x40028000 window (sweep:
`(word & 0x0F000000)==0x0E000000 && coproc==15`). There are **no CP15 accesses in
Thumb regions** — the only Thumb-mode `p15` listings in the forced-Thumb dump are
mis-decodes of these same A32 instructions.

### 3.1 CP15 sites by CRn

| Address | Instruction | Meaning |
|---------|-------------|---------|
| `0x40028100` | `mrc p15,0,r0,c0,c0,5` | MIDR (CPUID) |
| `0x40028178` | `mcr p15,0,r0,c8,c7,0` | **TLBIALL** |
| `0x40028184` | `mrc p15,0,r0,c1,c0,0` | read SCTLR (CPU1) |
| `0x4002818C` | `mcr p15,0,r0,c1,c0,0` | write SCTLR, `orr r0,#4096` → **I (bit12) set** |
| `0x400281C0` | `mrcne p15,0,r0,c2,c0,2` | read TTBCR (CPU1 path, cond NE always taken) |
| `0x400281C8` | `mcrne p15,0,r0,c2,c0,2` | write TTBCR, `bic r0,#7` → split T0SZ/N-width cleared |
| `0x400281D0` | `mcrne p15,0,r0,c2,c0,0` | write **TTBR0** = `[0x40028214]` = `0x40300000` |
| `0x400281D8` | `mcrne p15,0,r0,c2,c0,1` | write **TTBR1** = 0 |
| `0x400281E0` | `mcrne p15,0,r0,c3,c0,0` | write **DACR** = `0xFFFFFFFF` (all domains, manager) |
| `0x400281E4` | `mrcne p15,0,r0,c1,c0,0` | read SCTLR |
| `0x400281EC` | `mcrne p15,0,r0,c1,c0,0` | write SCTLR, `orrne r0,#1` → **M (MMU) set** for CPU1 |
| `0x40028910` | `mrc p15,0,r0,c0,c0,5` | MIDR (CPU0 reset) |
| `0x40028954` | `mcr p15,0,r4,c12,c0,0` | write **VBAR** = `[0x40028A00]` = `0x00028000` |
| `0x40028990` | `mrc p15,0,r0,c1,c0,0` | read SCTLR (CPU0 reset) |
| `0x40028998` | `mcr p15,0,r0,c1,c0,0` | write SCTLR, `orr r0,#6144` (0x1800) → **Z (bit11) + I (bit12)** set |
| `0x400289E4` | `mrc p15,0,r0,c6,c0,2` | **IFAR** (prefetch-abort handler) |
| `0x400289E8` | `mrc p15,0,r1,c5,c0,1` | **IFSR** |
| `0x400289F0` | `mrc p15,0,r0,c6,c0,0` | **DFAR** (data-abort handler) |
| `0x400289F4` | `mrc p15,0,r1,c5,c0,0` | **DFSR** |
| `0x4002F2B0-0x4002F550` | (many) | cache/TLB library, see §2 |
| `0x4002BD10` | `mcr p15,0,r15,c1,c5,5` | **mis-decode** (Thumb region) — ignore |

Security-relevant explicit conclusions:
* **MVBAR (`c12,c0,1`)**: not present in the 0x40028000 window.
* **SCR (`c1,c1,0`) / NSACR (`c1,c1,2`) / SDER (`c1,c1,1`)**: not present in the
  0x40028000 window. These are Secure-only registers; their absence is exactly what
  "the 0x40028000 window is NS-readable ROM" predicts.
* **c13 (FCSE PID / CONTEXTIDR / TPIDR / TLB lockdown)**: **no** `c13` CP15 access
  anywhere in either file.
* The only `c12` use in the window is the `VBAR` write at `0x40028954` (= `0x00028000`,
  i.e. the boot-alias base of this same ROM — consistent with the low-alias vectors
  at `0x00028000`).
* TTBR/domain/MMU programming for the secondary CPU is at `0x400281C0-0x400281EC`
  (TTBR0 `0x40300000`, TTBR1 0, DACR `0xFFFFFFFF`, MMU on), reached from the CPU1
  path (`0x40028150 b 0x400281C0` with `r10` = CPU1 boot address).

### 3.2 Lower window `0x40026098-0x400260E8` (separate evidence, A32)

| Address | Instruction / effect | Value |
|---------|----------------------|-------|
| `0x40026098` | `str r1,[r0]` with r0=`[0x40026134]`=`0x4A00215C`, r1=0 | writes **0** to control reg `0x4A00215C` |
| `0x400260A0` | `mcr p15,0,r0,c1,c1,2` | **NSACR = 0x20C00** (`mov r0,#134144`) — CP10/CP11 (and bit17) granted to NS |
| `0x400260BC` | `mcr p15,0,r4,c12,c0,1` | **MVBAR** = `[0x40026138] - [0x4002613C]` = `0x31C1C0 - 0x300000` = **`0x0001C1C0`** |
| `0x400260C4` | `mcr p15,0,r4,c12,c0,0` | **VBAR = 0** |
| `0x400260CC` | `mcr p15,0,r4,c1,c1,0` | **SCR = 0x30** (`mov r4,#48`) → bits [5:4] = FW|AW set, NS=0 |
| `0x400260D8` | `smc #0`, `r12=9` | monitor call, `r4 = [0x40026140] = 0x00028100`; then `str r11(0xFF0),[r12]`, `b .` |

This independently confirms the prior `BOOTROM-RE.md` claim about the secure
handoff tail (NSACR `0x20C00`, MVBAR from a literal pair, VBAR 0, SCR `0x30`,
`smc #0` service 9). It is the ROM's *secure-side* CP15 configuration and is
**outside** the 0x40028000 window; treat as separate evidence.

### 3.3 `0x4A0022C4` device-type / control-register accesses

`0x4A0022C4` appears as a literal at three places; **all uses are reads**.

| Literal at | Loaded by | Use |
|-----------|-----------|-----|
| `0x400280F4` | `0x400280C0 do_warm_reset` `ldr r1,[pc,#0x2c]; ldr r1,[r1]` | reads `[0x4A0022C4]`, `and #0x700`, `lsr #8`, `cmp #3` — gate for the warm-reset sequence |
| `0x400289FC` | `0x4002891C` (CPU0 reset, A32) | `ldr r0,[r0]; and r0,#1792 (0x700); lsr r0,#8; cmp r0,#3`; if equal, `ldr r0,[r0]` again → `mov r6,r0`, `and r0,#31`, `cmp r0,#0x1F`, `bleq 0x40028C00` — the GP/HS **device-type/class** read |
| `0x400297EC` | `0x400297D8` / `0x400297C8` (Thumb) | `ldr r0,[r0]; subs r0,#0xA4; ldr r0,[r0]; ubfx r0,r0,#0xC/#0xD,#1` → reads `0x4A0022C4-0xA4 = 0x4A002220` bits 12/13 and returns 0/1 |

So the open question in `NOTES.md` (#7) is **answered yes**: the NS-visible ROM does
read the `0x4A0022C4` device-type/feature field to gate behaviour — once in the
reset path (`bits[10:8]==3` and `bits[4:0]` class), and twice through
`0x4A0022C4-0xA4` bit tests at `0x400297C8`/`0x400297D8`. No **write** to
`0x4A0022C4` (or the `0x4A0022xx` neighbourhood) exists in the 0x40028000 window.

---

## 4. Abort handling

Two vector tables exist.

* **Table 1 @ `0x40028000`**: entry 0 = `b 0x40028910` (reset); entries 1-7 at
  `0x40028004-0x4002801C` are 7x `ldr pc,[pc,#0x18]`, each resolving to a word in
  `0x40028024-0x4002803C` that holds an **IRAM** pointer `0x4030D004-0x4030D01C`
  (preceded by the signature word `0x1CF48309` at `0x40028020`).
* **Table 2 @ `0x4002FFC0`** (8x `ldr pc,[pc,#0x18]`, targets at `0x4002FFE0..`):
  `0x28090`, `0x28080`, `0x28084`, **`0x289E4`**, **`0x289F0`**, `0x28090`,
  `0x29420` (IRQ), `0x28098`. The bootstrap table at `0x4002FF80` copies this
  0x40-byte block to IRAM `0x4030D000`, i.e. table 2 is what the IRAM vectors use.

**Abort handlers** (A32):

```
0x400289E4:  mrc p15,0,r0,c6,c0,2     ; r0 = IFAR (Instruction Fault Address)
0x400289E8:  mrc p15,0,r1,c5,c0,1     ; r1 = IFSR (Instruction Fault Status)
0x400289EC:  ldr pc,[pc,#0x1c]        ; -> word at 0x40028A10 = 0x00028088

0x400289F0:  mrc p15,0,r0,c6,c0,0     ; r0 = DFAR (Data Fault Address)
0x400289F4:  mrc p15,0,r1,c5,c0,0     ; r1 = DFSR (Data Fault Status)
0x400289F8:  ldr pc,[pc,#0x14]        ; -> word at 0x40028A14 = 0x0002808C
```

* **Which exception:** `0x400289E4` is the **Prefetch Abort** vector (reads IFAR/IFSR);
  `0x400289F0` is the **Data Abort** vector (reads DFAR/DFSR). They occupy entries
  **3** and **4** of table 2, matching the classic ARM vector order
  (0 reset, 1 undef, 2 svc, 3 prefetch abort, 4 data abort, 5 reserved, 6 IRQ, 7 FIQ).
* **What they record:** the faulting address in `r0` (FAR) and the fault status in
  `r1` (FSR), then `ldr pc` from the literal at `0x40028A10` / `0x40028A14`.
* **Where they jump:** to `0x00028088` (prefetch) and `0x0002808C` (data), i.e. the
  boot-alias of `0x40028088` / `0x4002808C`. Those are part of the
  `0x40028080-0x4002809C` cluster of eight `b .` (`0xEAFFFFFE`) infinite loops
  (`0x40028080/84/88/8C/90/94/98/9C`), so both handlers end in a **spin with the
  fault address in `r0` and status in `r1` preserved**. The *pair of distinct hang
  PCs* is the record: an external observer (JTAG/serial) can tell prefetch vs data
  abort from the PC, and read the reason from `r0`/`r1`. This is a
  record-and-halt debug handler, not a recoverable one — consistent with a ROM
  that is not expected to take aborts at all.

---

## 5. Hidden-region inference (marked INFERENCE)

**INFERENCE, based only on the documented boundary:** the separate file
`bootrom-lower-0x40020000.bin` covers `0x40020000-0x40026F24` (the dump stops at
`0x40026F24`; the next word was non-readable on-device), and the 0x40028000 window
starts at `0x40028000`. That leaves `0x40026F28-0x40027FFF`, a ~4 KB
non-readable hole, directly *below* the NS-readable 48 KB window.

Supporting (not proving) observations from this pass:
* Neither visible block contains any SMC **dispatch/handler** code — no table of
  `r12` values, no handler prologue for `smc #1`, no code reading `r12` on entry.
  The visible ROM only *calls* the monitor (`smc #0`/`smc #1`) and configures secure
  state from the tail block.
* The ROM's NS-visible abort handlers do not route SMC faults anywhere useful — an
  NS caller of an unimplemented service simply spins (§4), which matches a
  dispatch entry that lives outside the readable window.
* The hole's size (4 KB) is the same order as a small monitor dispatcher.

This is **inference from the documented boundary plus the absence of dispatch code**,
not a decode; no bytes at `0x40026F28-0x40027FFF` are available in either dump. It
aligns with the prior `STD_FUSE_ROM_HIDE`/"BOOT ROM upper 4-KB region" claim in
`BOOTROM-RE.md`.

---

## 6. Evidence list (what is directly observed vs inferred)

**Directly observed (LLVM-derived, reproducible from the bytes):**
1. Eight A32 SMC sites in `0x40028000` with exact addresses, `smc #0/#1`, and the
   `movw/mov r12` immediates (§1.2); one extra A32 SMC in the lower window (§1.3).
2. Zero Thumb-mode SMC (aligned sweep) in either file (§1.1).
3. Exact encoding of the `0x4002CEB0-0x4002CEBB` A32 veneer and the Thumb helper
   `0x4002CE90` / `0x4002CE36` selector logic (§1.3).
4. The callback indirect-reference sites `0x4002AC1A` and `0x4002C610` and their
   literals/pools `0x4002AE44-0x4002AE54`, `0x4002C6B4-0x4002C6BC` (§1.3).
5. All 50 CP15 instructions in the window (49 real + 1 mis-decode) and their
   operands (§3.1); the cache/TLB routine map
   (§2); VBAR=`0x00028000`; SCTLR `0x1000`/`0x1800`/`0x1` writes; TTBR0/TTBR1/DACR
   values (§3.1).
6. Lower-window NSACR/MVBAR/VBAR/SCR values and the `r12=9` SMC (§3.2) — separate file.
7. The three `0x4A0022C4` read sites and the `0x4A0022C4-0xA4` bit tests (§3.3).
8. Abort handler byte sequences, the recorded FAR/FSR registers, and the
   `0x40028088`/`0x4002808C` hang targets (§4).
9. The `0x4002F2F0-0x4002F30C` Thumb island inside the ARM library (§2.2).
10. `0x40028080-0x4002809C` = eight `b .` hang stubs.

**Inferred (flagged in text):** what the `smc #1` selectors `0x00/0x0FE/0x0F1`
(branch-predictor/secure-side maintenance) and `0x0F0` (image-load callback) ask
the monitor to do; the role of the `0x4002CEB0` veneer as an image-load/verify
callback; the hidden 4 KB as the SMC dispatch region (§5).

---

## 7. Unknowns / open points

1. **`0x4002CEB0` return path.** The veneer issues `smc #0` with no `bx lr` and the
   next bytes are Thumb. Whether the `0xF0` service is terminal, or the callback is
   unwound by the monitor (LR was clobbered by the preceding `blx`), is unresolved.
2. **`0x0F0` service semantics.** Not in the TRM `0x100-0x113` table; the callback
   takes a selector (helper `0x4002CE90`: selector 1 → pointer `0x4030DC1C`) and is
   stored alongside `0x40304000`/`0x4030BFFF` (SRAM) and `0x000280A8` (hang) in a
   boot-media record. Plausibly image load/verify, unproven.
3. **`smc #1` selectors `0x00`, `0x0FE`, `0x0F1`.** Values known; semantics
   (secure branch-predictor/cache flush vs something else) inferred only. Whether
   `r12=0` is a real selector or just the leftover BPIALL operand is unclear.
4. **The ARM `blx` H-bit convention.** Five ARM→Thumb calls in this ROM use bit 24
   = 0; the independent architecture convention (and LLVM's `+2` target adjustment
   for bit 24 = 1) disagree. It does not change any *readable* target in this ROM,
   but it should be confirmed before relying on encoded state for any single site.
5. **`0x4002F2F0-0x4002F30C` block purpose** (Thumb veneer to `0x40028904`) and why
   the A32 library is split around it.
6. **Four-or-more-arg monitor protocol.** Wrapper A/B pass `r0-r3` through but the
   callers only fill 2-3 of them; the full argument ABI of `smc #1` is unknown.
7. **Semantics of the values read from `0x4A0022C4` / `0x4A002220`** (which GP/HS or
   feature bits the `bits[10:8]==3`, `bits[4:0]`, bit 12/13 tests actually select).
8. **`0x40028C00`** (called when `[0x4A0022C4]` bits `[4:0] <= 0x1F`) and the
   `0x40028C00` feature-gating body were not reversed in this pass.
