# 91 — Linux entry state: the `0x80014A90` hand-off, the real jump, and a chainloader design

Track D of the PlayBook native-boot effort (`85-linux-boot-plan.md`, Phase 5).
Date: 2026-10-25. **Read-only**: no USB, no NVRAM/RPMB/eMMC/flash access, no
reboots, no device contact of any kind. Everything below is static analysis of
files already in the workspace plus host-side scripts.

Deliverable of record for: enter-OS path reversal, jump-target derivation,
second-stage load-vs-entry question, bootloader-visible memory map, a minimal
ARM chainloader specification, live-read wishlist, kexec comparison.

---

## 0. Provenance and method

| item | value |
|---|---|
| target | `bootdumps-2026-09-11/hd0head.bin` (`/dev/hd0` offset 0x0–0x200000) |
| SHA-256 | `dda9d955003302adf1f0d41580c50c0c9035126487e7ac53793f4d38a3a23e3d` |
| load base | **0x80000000**, file offset **==** (address − 0x80000000) |
| relocation | pre-relocation image, linked at 0x7FFF7000 (`73`): absolute **literals** are 0x9000 low. Code addresses in this report are the *runtime* addresses (as executed); literal **values** are quoted as-stored, with the runtime value given where it matters |
| tools | `llvm-objdump` (dual-mode, `--triple={arm,thumb}v7-none-eabi --mcpu=cortex-a9`), `llvm-mc`/`ld.lld` wrapper ELF, Capstone 5.0.7 sweep scripts. **No Ghidra on this host** (`analyzeHeadless` not found); existing Ghidra output from `analysis/hd0-modemap-2026-10-20/` was used read-only where it agreed with the bytes |
| mode discipline | every range below was decoded in the mode stated; the ARM/Thumb islands of `71` §1 were respected (0x80009000 vector table + 0x800194xx CP15 helpers are **ARM**; 0x80013FB6–0x800148AC and 0x80014904+ are **Thumb**) |

Scratch scripts (throwaway): `$COMMANDCODE_SCRATCHPAD/{sweep3.py,who.py,cp15.py,find_entries.py}`, `wrap.elf`.
Nothing in this report is REA MCP Evidence.

### 0.1 Three corrections to the facts this track was handed

1. **`0x803BF610` is the image *end*, not the entry point.**
   `cfp info` on the live 64 GB unit reports `OS Address: 0x800FF800-0x803BF60F`
   (`playbook-dev/PLAYBOOK-REFERENCE.md` §1.6), and the live header's dword at
   `+0x08` is `0x803BF610`. `0x803BF610 − 0x800FF800 = 0x2BFE10` = 2,882,064
   bytes = the image length. There is **no code at 0x803BF610 to analyse**; it
   is the first byte past the image (which is exactly what `0x80014610` and
   `0x80014788` want: `end-8` = cookie, `end-4` = CRC).
2. **`0x80014A90` is not a jump.** It is a label inside the Thumb boot main
   (`0x80014904`) that logs `"Leaving bootrom and entering OS"` and then
   **returns** (`pop {r4,r5,r6,r7,pc}` at `0x80014ACC`). The actual transfer of
   control is in **ARM** code at **`0x800192A8`** (`bx r0`), in the stage's
   `main`, after the boot main has returned (§2.2).
3. The `0x800FF800` window `0x800FF800`–`0x800102B0` from `70`/`71` is a
   *descriptor* window used by the verifier's pointer sanity check
   (`0x80013FDC`), not the image extent. The image extent is
   `0x800FF800`–`0x803BF610`.

Labels used below: **PROVEN** = byte-exact from the image; **INFERRED** =
consistent with the bytes but one live read away from certainty.

---

## 1. The complete boot chain, as executed (PROVEN)

```
OMAP4430 boot ROM
   └─ loads hd0 0x4000.. (`CertISW` + code) at 0x80000000
      └─ 0x80009000  ARM exception vector table, entry 0 = `b 0x80019224`
           0x80009000  b 0x80019224        ; reset
           0x80009004  b 0x80009040        ; undef   -> ldr pc,[0x4030D004]
           0x80009008  b 0x80009044        ; svc     -> ldr pc,[0x4030D008]
           0x8000900C  b 0x80009048        ; pabt    -> 0x4030D00C
           0x80009010  b 0x8000904C        ; dabt    -> 0x4030D010
           0x80009014  b 0x80009050        ; (rsvd)  -> 0x4030D014
           0x80009018  b 0x80009054        ; irq     -> 0x4030D018
           0x8000901C  b 0x80009058        ; fiq     -> 0x4030D01C
           0x80009020  " An illegal Device "
           0x80009030  b 0x80019224        ; secondary entry
           0x80009034  0xD7B02D1F          ; bootrom-metrics cookie
           0x80009038  0x80000000
           0x8000903C  0x80014040          ; -> bootrom metrics blob (0x8001D040 runtime)
         └─ 0x80019224  ARM `main` (§2.2)
            └─ 0x80014904  Thumb boot main: gates + region verify
               └─ 0x80014A90  log "Leaving bootrom and entering OS"; return
            └─ 0x800192A8  bx *(0x8002EE74)   <-- THE hand-off
               └─ 0x800FF800  loaded OS image, word 0 = 0xEA0001FE (ARM `b`)
                  └─ 0x80100000  image+0x800 = first real code of the OS image
```

`0xEA0001FE` disassembles as ARM `b #+0x800` from `0x800FF800` → **0x80100000**.
So the *effective* first instruction of the "second stage" is at image+0x800,
not at 0x800FF800 and not at 0x803BF610. (PROVEN arithmetic; the assumption that
the loader leaves the header byte-identical in RAM is INFERRED — §9 Q2.)

Two independent pieces of corroboration that this vector-table-at-0x80009000
layout is the ROM interface: the +0x9000 relocation (`0x7FFF7000 + 0x9000 =
0x80000000`) puts offset 0x9000 exactly at `0x80009000`; and `0x80009030`
carries the `0xD7B02D1F` bootrom-metrics cookie described in
`PLAYBOOK-REFERENCE.md` §2.

---

## 2. Walk-through with addresses

### 2.1 Thumb boot main `0x80014904` — the gates (from `71`, byte-confirmed here)

```
80014904  push {r4-r7,lr}; sub sp,#0x14
8001490e  str r6(0),[sp,#4]            ; <-- the "region descriptor" slot the verifier fills
80014910  bl  0x800094E4               ; WDT2 kick (see §2.3)
80014914  adr r0,"\nStarting RIM bootrom with UART_DEBUG enabled\n"; bl 0x80017206 (log)
80014926  bl  0x8001376C               ; early flash/record probe
8001492a  bl  0x80010D3C               ; RPMB/record layer
...
80014974  movw r7,#0x2040 ; bl 0x8000A9D8 (NVRAM read)   ; NukeStatus
80014986  cmp r0,#9 ; bhs 0x80014A16   ; >=9 -> nuke
8001498a  tbb [pc,r0]                  ; 0/2 gate, 1 nuke, 3..8 normal
80014998  bl  0x80014F84               ; NVRAM 0x2801 attempt budget (live: 10)
8001499e  bne 0x80014A1E               ; live unit takes THIS branch (A=0,B=10)
...        (normal path 0x800149A0-0x800149CC: 0x2040++, BOOTCTR_FCTRY, ECDSA)
80014A1E  ldr r7,=0x80025E74           ; [runtime 0x8002EE74]
80014A20  bl  0x800095D8
80014A24  ldr r1,=0x80025E74           ; param_2 = &(image start global)
80014A26  add r3,sp,#4 ; add r2,sp,#0xc ; add r0,sp,#0x10
80014A2C  bl  0x8001453C               ; flag=1 wrapper -> 0x800142A8 driver
80014A30  cbz r0,0x80014A6C
80014A32  cmp r0,#1 ; beq 0x80014A3E
80014A36  cmp r0,#2 ; bne 0x80014A76
80014A3A  str r5(0),[r7]               ; failure: *(0x8002EE74) = 0
80014A3E  bl  0x8000911A               ; BOOTCTR_FCTRY selector
80014A42  ldr r1,=0x80026150
80014A46  movs r0,#1 ; str r0,[r1]
...
80014A8A  cbnz r4,0x80014A90
80014A8C  bl  0x800095B4
```

### 2.2 The two sites that matter

**(a) `0x80014A90` — "enter OS" label (Thumb, returns):**

```
80014A90  bl   0x8001217E     ; -> 0x80012884 (b.w); flash/host transport quiesce  [INFERRED]
80014A94  movs r0,#0
80014A96  bl   0x80015E14     ; r0 = (*(*(0x80026150+... ))+0xC)->+0x3C  indirect call;
                              ;   returns 0xFFFFFFFF when the slot is empty
80014A9A  cbz  r0,0x80014AA4
80014A9C  movs r1,#0x17 ; movs r0,#0 ; bl 0x80014C98   ; log/record status 0x17 (non-fatal)
80014AA4  movs r3,#0 ; movs r1,#3 ; movs r0,#0x1F ; mov r2,r3
80014AAC  bl   0x80009BF4     ; platform/HW control: case 3, flags 0x1F      [semantics open]
80014AB0  movs r3,#0 ; movs r1,#1 ; movs r0,#0x1F ; mov r2,r3
80014AB8  bl   0x80009BF4     ; platform/HW control: case 1, flags 0x1F      [semantics open]
80014ABC  blx  0x8001943C     ; ARM: SCTLR.M = 0, BPIALL, ISB, bx lr   <-- MMU OFF
80014AC0  bl   0x800094E4     ; WDT2 kick (increment WTGR @0x4A314030)
80014AC4  adr  r0,"\nLeaving bootrom and entering OS\n"
80014AC6  bl   0x80017206     ; log()
80014ACA  add  sp,#0x14
80014ACC  pop  {r4,r5,r6,r7,pc}   ; <-- RETURNS to 0x80019268
```

*(the `adr` at `0x80014AC4` resolves to `0x80014BA0` — `Align(PC,4)+0xD8` — i.e. the byte `0x0A` before
`"Leaving…"`; the log strings in this image carry a leading newline and the
first char is at an odd address — this is why a naive `strings` search reports
`0x80014BA1`.)*

**(b) `0x800192A8` — the actual hand-off (ARM, does not return):**

```
80019224  mov  r0,#0xD3            ; SVC mode, IRQ+FIQ masked
80019228  msr  CPSR_c,r0
8001922C  ldr  r0,=0x800517C0      ; [rt 0x8005A7C0] "saved entry SP" slot
80019230  str  sp,[r0]             ; save the ROM's / caller's SP
80019234  ldr  r0,=0x80024674      ; [rt 0x8002D674] BSS base
80019238  add  r0,r0,#0x1800       ; 6 KB
8001923C  mov  sp,r0               ; SP = 0x80025E74  [rt 0x8002EE74]
80019240  sub  sp,sp,#4
80019244  blx  0x80014ACE          ; f(0x10, *(0x80014040+0xA0))   [metrics-driven helper]
80019248  blx  0x800094B0          ; WDT2 start/poll helper
8001924C  blx  0x80014ADA          ; f(0x20, *(0x80014040+0xA0))
80019250  blx  0x8000A86C          ; NVRAM record table init
80019254  bl   0x8001947C          ; ARM: TTBR0/TTBCR/DACR + TLBIALL + ICIALLU   (§3.2)
80019258  bl   0x80019400          ; ARM: TLBIALL(IS) + TLBIALL + SCTLR.M = 1     (MMU ON)
8001925C  blx  0x8000983C
80019260  blx  0x800159AC          ; IRAM boot-interface struct init @0x4030B000
80019264  blx  0x80014904          ; boot main (Thumb) -> returns at 0x80014ACC
80019268  blx  0x80015A58          ; `bx lr` stub
8001926C  blx  0x800094CC          ; clears a global, -> 0x8001AF94
80019270  blx  0x800158E4          ; cache clean/invalidate of 3 regions (if 0x80026150 != 0)
80019274  cmp  r0,#0 ; bne 0x80019298
8001927C  ...zero-fill 0x80024674..0x80025E74 (6 KB BSS)...
80019298  ldr  r0,=0x800517C0 ; ldr sp,[r0]      ; restore the entry SP
800192A0  ldr  r0,=0x80025E74 ; ldr r0,[r0]      ; r0 = *(0x80025E74) = image start
800192A8  bx   r0                                ; <-- HAND-OFF (ARM state, MMU off)
800192AC  b    0x800192AC                        ; hangs if it ever comes back
```

`*(0x80025E74)` is written by the verifier, not by a literal:

```
800141D8  ldr r0,[r4]        ; p  = *param_4  (region/image descriptor)
800141DC  ldr r0,[r0,#8]     ;    *(p+8)     (pointer to a pointer)
800141DE  ldr r0,[r0]        ;    ** (p+8)
800141E0  str r0,[r7]        ; *(0x80025E74) = <image start>
800141E2  ldr r0,[r4] ; ldr r0,[r0,#0xC] ; ldr r1,[r0]
800141EA  str r1,[r8]        ; *param_3 = <image end>
...
80014A3A  str r5(0),[r7]     ; any failure path writes 0 instead
```

**Consequence (PROVEN):** the jump target is not a fixed address and not a
literal in the bootloader — it is taken from the *loaded image's own descriptor
in RAM*, via `0x80014148`, and is zeroed on any verification/flow failure.
Only the base of the image (`0x800FF800`) can plausibly come out of that path
(it is the field the descriptor's `+4`/`+0x34` chain points at), and the entry
to actual OS code is then the header's ARM `b` to image+0x800.

### 2.3 What the pre-jump calls establish

| call | what it does | status |
|---|---|---|
| `0x800094E4` | `[0x4A314000+0x30]++` = **WDT2 WTGR increment (watchdog kick)**; literal at `0x80009544` is `0x4A314000`. The same routine is the boot main's first call (`0x80014910`) | PROVEN |
| `0x8001943C` | `mrc TTBR0; mov; sub pc,pc,#4` (forced ARM state) → `mrc SCTLR; bic #1 (M=0); mcr SCTLR; mrc TTBR0; mcr p15,0,r0,c7,c5,6 (BPIALL); isb; bx lr` — **MMU off + branch-predictor invalidate only** | PROVEN |
| `0x8001947C` | TTBR0 = `0x80054000│0x59` (literal at `0x80019550`; runtime 0x8005D059), TTBCR bit4 (N) cleared → **N=0, TTBR1 unused**, DACR = `0x55555555` (all 16 domains *client*), TLBIALLIS+TLBIALL, ICIALLUIS+ICIALLU, DSB/ISB, `CONTEXTIDR`=0 | PROVEN |
| `0x80019400` | TLBIALLIS + TLBIALL + DSB/ISB, then `mrc SCTLR; orr #1; mcr SCTLR` = **enable MMU** | PROVEN |
| `0x800194DC` | `SCTLR & 1` = "MMU on?" predicate | PROVEN |
| `0x80019500` | `SCTLR |= 2` (A, alignment faults) | PROVEN |
| `0x80019324` / `0x80019340` | `SCTLR &= ~4` (D-cache off) / `SCTLR &= ~0x1000` + ICIALLU (I-cache off) — **not called from either hand-off path** | PROVEN (absence) |
| `0x80000FBC` | `mcr p15,0,r0,c12,c0,0` = **VBAR write**, a callable ARM veneer; **no in-image caller found** | PROVEN (absence) |
| `0x80009BF4` | multi-purpose platform control (`arg1` = case 0..5, `arg0` = bit flags). In *both* hand-off paths it is called twice — `(0x1F,3)` then `(0x1F,1)` — immediately before MMU-off | PROVEN pattern, semantics **open** |
| `0x80015E14` | indirect call through `(*(*(0x80026190)+0xC))+0x3C`; returns `-1` when the slot is absent, which only logs status `0x17` and continues | PROVEN |
| `0x8001217E` | `b.w 0x80012884` — transport/flash poll | INFERRED |
| `0x800159AC` | builds the IRAM boot interface at `0x4030B000` (magic `0x84D93A96`, sets `+0xBC=0`, `+0x58=0xBC`, `+0x5C=4`) | PROVEN |

Note the **host-loader sibling** of this path: `0xF005 EXECUTE_RAM` in the same
stage's own USB command handler lands at `0x800152CC`, which executes the
**identical** five-step tail (`0x80015E14` → `0x80009BF4(0x1F,3)` →
`0x80009BF4(0x1F,1)` → `blx 0x8001943C` → `blx r0`), differing only in that the
target is the host-supplied address in the request (`[req+0xC]`). That is
byte-level proof that this sequence *is* RIM's "leave the bootloader and jump"
idiom, and that a payload entered this way must survive MMU-off.

---

## 3. CPU / MMU / cache state at the jump

State at `0x800192A8` (`bx r0`), i.e. the first instruction the loader hands us:

| resource | value at hand-off | basis |
|---|---|---|
| CPU state | **ARM** (`CPSR.T = 0`) — the last instruction is ARM `bx r0`; target `0x800FF800` is even so no interworking switch | PROVEN |
| mode | **SVC** (`0xD3` written at `0x80019228`, never changed on this path; `0x80009BF4` may touch CPSR, not observed) | PROVEN / INFERRED |
| IRQ / FIQ | **masked** (`0xD3` = I=1, F=1) | PROVEN |
| `SCTLR.M` | **0 (MMU off)** — `0x8001943C` | PROVEN |
| `SCTLR.C` (D-cache) | **not cleared on this path** → whatever the stage enabled. The stage has an explicit disable-D helper (`0x80019324`) and an enable/invalidate helper (`0x80019400`), so it ran with caches; expect **C = 1** | PROVEN that it is untouched; value INFERRED |
| `SCTLR.I` (I-cache) | likewise untouched (`0x80019340` exists but is not called) → expect **I = 1** | same |
| `SCTLR.A` | set (bit1) by `0x80019500` if that helper ran; not on the hand-off path | INFERRED |
| `SCTLR.V` | never written anywhere in the image → **low vectors** inherited from the ROM | PROVEN (absence) |
| `VBAR` | not written on this path; the ROM/stage vector table lives at **`0x80009000`**. A write veneer exists at `0x80000FBC` with **no caller in the image** | PROVEN |
| `TTBR0` | `0x80054059` (pre-reloc) / `0x8005D059` (runtime) → table at **0x8005D000**, 16 KB = 4096 × 1 MB sections = a **flat 4 GB 1:1 map** | PROVEN |
| `TTBR1` | unused: `TTBCR.N = 0` (bit4 cleared at `0x80019498`) | PROVEN |
| `TTBCR` | N=0, PD0/PD1 untouched (reset 0) | PROVEN |
| `DACR` | `0x55555555` — all domains **client** (AP bits enforced) | PROVEN |
| `PRRR` / `NMRR` | **never written** in the whole image (no `mcr p15,…c10,c2,0/1`) → reset defaults | PROVEN (absence) |
| `CONTEXTIDR` | 0 | PROVEN |
| TLBs | invalidated twice (`0x8001947C`, `0x80019400`), then MMU off → irrelevant | PROVEN |
| I-cache | `ICIALLUIS`+`ICIALLU` at `0x800194B4/B8`, `BPIALL` at `0x80019470` | PROVEN |
| D-cache maintenance | only the *range* flush at `0x800158E4` (3 descriptor pairs at `0x8001431C`, done before the jump, gated by `*(0x80026150)`); **no clean/invalidate-all** | PROVEN |
| `SP` | restored from `0x800517C0` (rt `0x8005A7C0`) = the SP the stage itself inherited from the ROM → **unknown value, not the stage's stack** | PROVEN (mechanism), value unknown |
| `r0` | **the jump target** (`0x800FF800`) | PROVEN |
| `r1`–`r3`, `r12` | **undefined** — leftovers from `0x800158E4`'s `ldm` | PROVEN |
| `LR` | `0x80019274` (from `blx 0x800158E4`) — useless as a return path; `0x800192AC` is `b .` anyway | PROVEN |
| WDT2 | **RUNNING** — armed by the stage, kicked by `0x800094E4` three instructions before the jump. WTGR at `0x4A314030`, WSPR at `0x4A314024`; the OS-side kicker uses a 15 s period | PROVEN |
| TWL6030 PMIC WDT | not armed on this path (only the *booted OS* `omap4430-wdtkick -P` arms it) | INFERRED |

**The four facts that shape any payload:** ARM state · SVC · MMU off · **caches
possibly on** · WDT2 counting.

---

## 4. Jump mechanics: load address vs entry

```
image start (load)   0x800FF800   header word 0 = 0xEA0001FE = ARM `b` +0x800
effective entry      0x80100000   image + 0x800
image end            0x803BF610   (= start + 0x2BFE10), first byte past the image
```

- The bootloader never materialises `0x803BF610` or `0x800FF800` as a literal:
  `0x800FF800` occurs only in the verifier's window check (`0x800143D8`), in the
  host-loader destination lower bound (`0x800153F0`), and in one data word
  (`0x8001D962`). `0x803BF610` occurs nowhere. The only path by which control
  reaches the image is the descriptor → `0x80025E74` → `bx` chain of §2.2.
- There is **no separate "second-stage load address vs entry" logic**: one
  `bx` goes to the image base and the *image's own first instruction* (an ARM
  branch) is what reaches the startup code at +0x800. RIM therefore does not
  need to know the startup address — which also means a replacement payload
  only has to preserve that first branch.
- "Return or branch": `bx r0` — a **branch**, and the fall-through is an
  infinite loop. There is no return convention, no argument convention beyond
  `r0` = target.

---

## 5. Memory map visible from the bootloader code

### 5.1 DRAM (PROVEN addresses, sizes from live `cfp`/`pidin mem`)

| range | use |
|---|---|
| `0x80000000`–`0x80009000` | stage code (PRIMAPP/MLO section, `CertISW` at 0x80004000) |
| `0x80009000`–`0x8000FFFF` | ARM vector table + arm/thumb veneers + early ARM CP15 helpers (0x80000E60–0x80001070) |
| `0x80010000`–`0x80024000` | Thumb code: USB host protocol (0xFF00–0xFF0A), NVRAM record layer, RPMB/eMMC block layer |
| `0x80024000`–`0x8002D000` | loader/hand-off code, literal pools, strings ("Leaving bootrom…"), `0x800191B4` fatal stubs |
| `0x8002D674`–`0x8002EE74` | **BSS** (6 KB, pre-reloc `0x80024674`+0x1800), zeroed at the end of `main` |
| `0x8002EE74` | **image-start global** — word written by `0x80014148`, read by `0x800192A0` |
| `0x8002F144` | `*PTR_DAT_800143e8` — current descriptor pointer (also must equal `+0x24` of the descriptor) |
| `0x8005A7C0` | saved-entry-SP slot |
| `0x8005D000`–`0x80061000` | **MMU level-1 table** (16 KB, 4096 × 1 MB sections, flat 4 GB map) |
| `0x800FF800`–`0x803BF610` | **loaded OS image** (2.75 MB): RIM header/metrics blob + startup |
| `0x84000000`+ | free DRAM — the kexec work already proved this region clear of QNX allocations |
| `0x90000000` | kexec breadcrumb page (established by the OS side, not the loader) |
| DRAM end | 1 GB: `0x80000000`–`0x9FFFFFFF` + `0xA0000000`–`0xBFFFFFFF` (two 512 MB banks, live `cfp`) |

### 5.2 On-chip / peripheral windows referenced by the bootloader

| address | what the code does with it | identification |
|---|---|---|
| `0x40300000`–`0x4030FFFF` | boot-interface struct at `0x4030B000` (magic `0x84D93A96`, fields `+0x58/+0x5C/+0xBC`); abort vectors via `0x4030D004`–`0x4030D01C`; the literal `0x40304000` sits unused at `0x800192B4` | **OCMC/IRAM**; `cfp info` reports `IRAM Base: 0x40304000` — the same window the kexec stub reuses |
| `0x4A314000` | `0x800094E4` increments `+0x30` (WTGR) — watchdog kick; `0x800094F0` sets `+0x10` bit1 then polls bit30 | **WDT2** (matches `omap4430-wdtkick` + mainline `wdt2@4a314000`) |
| `0x4A004000` | via `0x80009BF4`/secure services | CM1 / CM_MPU (per `PLAYBOOK-REFERENCE.md` §1.10) |
| `0x48020000` | debug console used before the OS (`devc-seromap`), matches the stage's UART output | **UART3**, 48 MHz/16, 115200 |
| `0x480B4000` | eMMC | **MMC2** |
| `0x48240000` / `0x48242000` | — | GICD/GICC + **PL310 L2** |
| SMC (`smc #1`) | `0x80019394`–`0x800193F4` ARM veneers, services `0x100`,`0x102`,`0x109`,`0x112` — **no caller in the image** (`71` §2.2); the *live* dispatcher at `0x80009078` uses `r6=0xFF, r12=0` | secure monitor, N/A for the hand-off |

**Secure / non-secure boundary:** the hand-off path executes **no SMC**. The two
`0x80009BF4` calls are plain function calls (they contain no `smc`). The region
signature verification, the MMU setup, the cache maintenance and the jump all
run in the **non-secure world**; only the RPMB record path is secure-gated
(`71` §7b). A chainloader therefore does not need any secure-world service.

### 5.3 The MMU map the loader installs (and we inherit *off*)

`TTBR0 = 0x8005D000`, 4096 × 1 MB sections, `DACR = 0x55555555`, `TTBCR.N = 0`,
`PRRR/NMRR` at reset. Because the stage keeps running after `SCTLR.M = 0`, the
map it installed **must** be 1:1 for at least the code/stack/BSS it touches
after the switch (PROVEN behaviour). The section attributes themselves are not
recoverable statically (the table is built at runtime and is above the dumped
file), which does not matter: the MMU is off when we get control.

---

## 6. A minimal ARM chainloader that satisfies the CRC path

### 6.1 What the image must look like (constraints, PROVEN)

From `0x80013FB6` (CRC gate) + `0x80014788` (CRC verify) + `0x80014610`
(structural) + `0x80014148` (descriptor plumbing):

| requirement | where enforced |
|---|---|
| `desc[0xD4] & 0xFF == 6` | `0x80013FB6` — the CRC path is **only** taken with this byte |
| `desc[0x9C] == 0xC3D0E193` | `0x8001462C` |
| `*(desc+0) >= 0x20000` (≥128 KB) | `0x80014644` |
| `desc - 4 + desc[4]` must be a valid pointer | `0x80014636` |
| `*(desc+8)` must be valid and `**(desc+8) == image start` | `0x8001464A` |
| `*(desc+0xC)` must be valid and `*(*(desc+0xC)) - 4` must be valid (**image end**) | `0x8001465A` |
| `end[-4] != 0xFFFFFFFF` | `0x80014670` |
| `end[-8] == 0xD7C82D1F` (image cookie) | `0x80014678` |
| `CRC32/ISO-HDLC([start, end-4)) == *(u32*)(end-4)` | `0x80014838` |
| descriptor self-pointer `desc[0x24] == desc` and `== *(0x8002F144)` | `0x8001418E` |
| version word `desc[0x7C] & 0xFF00FFFF` must match the NVRAM anti-rollback value | `0x80014240` |
| `desc[0xD4] < 0x20` selects a per-OS-type bit in NVRAM `0x205A` | `0x80014204` |

`89-crc_tool.py` (track B) already models all of this byte-exactly; use it as the
host-side oracle for every build.

### 6.2 Two build variants — start with A

**Variant A (recommended first)** — *change nothing but the payload*:

- Keep the live 2.75 MB image size. `end` stays `0x803BF610`, every header field
  between `0x800FF800` and `0x803BF60C` stays byte-identical to the shipping
  image **except** byte `+0xD4 = 6`, the payload window
  `0x80100000..0x803BF608`, and the trailer.
- Leaves every unknown of §9 Q2 (which header dword is a size, which is a RAM
  fixup) untouched — the image is structurally the same object the loader
  already boots, only the type byte, the payload and the CRC change.
- Still satisfies "≥ 128 KB" trivially.

**Variant B (after A boots)** — grow the image; the IFS zone is
`0x100000..0xC00000` = **11 MB** (`winchester.py`: MBR 0x0, SIG 0xF0000,
IFS 0x100000, RFS 0xC00000), so the CRC'd image must stay under 11 MB. Requires
settling whether `desc[4]`/`desc[8]`/`desc[0x34]`/`desc[0x38]` are the size and
the `&start`/`&end` fixups (§9 Q2).

### 6.3 Proposed layout (Variant A)

| address | size | content |
|---|---|---|
| `0x800FF800` | `0x1CC` | **RIM header / OS-metrics blob, byte-identical to the live image** (`+0xD4` set to `6`, everything else unchanged) |
| `0x800FF9CC` | `0x634` | zero padding to the `+0x800` branch target (keeps `desc[0x9C]`, `[0xD4]`, `[0x7C]` intact and gives a clean page) |
| `0x80100000` | `0x0000400` | **chainloader entry** (ARM, linked at `0x80100000`): cache/MMU/VBAR/WDT setup, DTB relocation, jump |
| `0x80100400` | `0x0000400` | chainloader vector table (ARM) + parameter block: magic, `cpsr/sctlr/vbar/ttbr0/dacr` dump slots, UART3 log ring |
| `0x80100800` | `≤ 0x20000` | **Stage-1 first milestone: a state-probe payload** (see §7) — prints the CP15/CPSR state over UART3 and kicks WDT2 forever |
| `0x80120000` | var | Linux `zImage` (only after the probe has been read back) |
| after kernel, 8-byte aligned | var | DTB (`*.dtb`, copied by the stub to `0x84000000` before the jump) |
| pad to 4 | | zeros |
| `end-8` = `0x803BF608` | 4 | `0xD7C82D1F` cookie |
| `end-4` = `0x803BF60C` | 4 | `CRC32/ISO-HDLC(0x800FF800 .. 0x803BF607)` |

Why `0x80100000` for the stub: it is *forced* by the existing header
(`b +0x800`), it is above the kernel's relocation/decompression base
(`0x80008000`), and it is inside the CRC'd image so no second loading channel
is needed.

### 6.4 What the stub must do, in order

```asm
        .arm
        .global _start
        .set    UART3, 0x48020000
_start:                                   @ entry: 0x80100000, ARM, SVC, MMU off
        ldr     sp, =0x800F0000           @ own stack, below the image, above the loader
        @ 1. no I/O, no supervisor calls: this is still the boot world
        mrc     p15,0,r0,c1,c0,0          @ SCTLR
        bic     r0,r0,#(1<<2)             @ C = 0 (D-cache off)
        bic     r0,r0,#(1<<12)            @ I = 0
        mcr     p15,0,r0,c1,c0,0
        @ 2. clean+invalidate L1 D by set/way, invalidate I, invalidate TLBs
        @    (mandatory if we ever re-enable caching, and cheap: 4 ways x 512 sets)
        @ 3. VBAR := our own vector table (the loader's table at 0x80009000 is
        @    about to be overwritten by the kernel)
        ldr     r0,=0x80100400
        mcr     p15,0,r0,c12,c0,0
        @ 4. WDT2: either keep kicking  [0x4A314030]++  in the wait loop, or
        @    disable: WSPR(0x4A314024)=0xAAAA -> poll WWPS bit1 -> =0x5555
        @ 5. ARM Linux boot protocol (identical to the kexec continuation):
        @    r0 = 0, r1 = 0xFFFFFFFF (or 0), r2 = DTB phys, CPSR = SVC/IF,
        @    MMU off, D-cache off, I-cache off, ARM state
        ldr     r2,=0x84000000            @ DTB, copied out of the image first
        mov     r0,#0
        mvn     r1,#0
        ldr     pc,=0x80120000            @ zImage entry (self-decompressing)
```

Non-negotiables, each traceable to a proven fact:

1. **Set your own SP.** The inherited SP is the ROM's, restored from
   `0x8005A7C0`, and the loader's stack lives at `0x8002EE74` downward — both
   inside/near regions the kernel will overwrite.
2. **Disable caches and clean+invalidate L1.** The hand-off path does *not*
   clear `SCTLR.C`/`I`; the loader's dirty lines (its stack, its BSS, its page
   tables at `0x8005D000`) must not be written back later on top of a running
   kernel.
3. **Set VBAR.** No code in the image ever writes it (`0x80000FBC` is
   uncalled), and the only vector table that exists is the loader's own at
   `0x80009000`, which the kernel will flatten.
4. **Handle WDT2.** It is armed and was kicked three instructions before the
   jump. Either re-kick `0x4A314030` from the boot/wait loop or run TI's
   disable sequence (`WSPR 0xAAAA → 0x5555`, per `newdocs/session-notes/
   session-01.md` erratum). A raw `Image` path must also invalidate L1/L2
   itself; the self-decompressing `zImage` performs its own full cache
   clean+invalidate, which is why `zImage` is the easier first target.
5. **Pass exactly the ARM boot state**: `r0 = 0`, `r1 = ~0`, `r2 = DTB phys`,
   SVC, I/F masked, MMU off — the state the existing kexec continuation already
   proves works on this SoC (`newdocs/ARCHITECTURE.md`).
6. **Keep the DTB out of the decompression footprint**: copy it to `0x84000000`
   (proven free on the live unit) and pass that address.
7. **Do not expect any argument** (`r1`–`r3` at entry are garbage) and **do not
   return** (`0x800192AC` is `b .`).

### 6.5 Boot-order / first-test sequence

1. Build Variant A with only the **state-probe payload** (§7) — no kernel.
2. Verify host-side with `89-crc_tool.py verify/reseal`; require an exact
   match of the CRC, the `+0x9C` magic, the `end-8` cookie, `+0xD4 == 6`,
   `end-4 != 0xFFFFFFFF`, and `size ≥ 0x20000`.
3. Stage into the **inactive** region only, per `85` Phase 1–3 (`hd2`), max
   boot count 0.
4. `BOOTCTR_FCTRY` is only written on the *spare* 64 GB unit and only after
   **both** regions are CRC-valid (`85` Phase 4).
5. Read the probe's output over UART3 (`0x48020000`, 115200 8N1) — the probe
   answers §7 items 1–8 on the first boot, with zero writes.

---

## 7. Live-read wishlist (the only things static analysis cannot settle)

Highest value first. Items 1–8 are all answered **at once** by the state-probe
payload of §6.3 (a read-only payload is itself the instrument — no USB, no
debugger, no NVRAM):

| # | what | exact address / register | why it matters |
|---|---|---|---|
| 1 | value the loader jumps to | `0x80025E74` (runtime `0x8002EE74`) — read as the first thing the probe does | settles "jump to image base" vs anything else |
| 2 | RAM form of the header | dword dump `0x800FF800`–`0x800FF8FF`, especially `+0x04`, `+0x08`, `+0x24`, `+0x34`, `+0x38`, `+0x7C`, `+0x9C`, `+0xD4` | settles §9 Q2 (fixups vs flash form) |
| 3 | descriptor pointer | `0x8002F144` (`*PTR_DAT_800143e8`) and `[p+0x28]` | confirms which struct the verifier used |
| 4 | verifier window bounds | `0x800143D8`, `0x800143DC` (`0x80013FDC` reads the second one *through* the address) | resolves the low-bound oddity in §9 Q3 |
| 5 | `SCTLR` at entry | `mrc p15,0,r0,c1,c0,0` | the cache bits (§3) |
| 6 | `CPSR` at entry | `mrs r0,CPSR` | mode/I/F/T confirmation |
| 7 | `VBAR` at entry | `mrc p15,0,r0,c12,c0,0` | is a vector base set at all? |
| 8 | `TTBR0`/`TTBCR`/`DACR` at entry | `c2,c0,0` / `c2,c0,2` / `c3,c0,0` | confirms the table location survives |
| 9 | entry `SP` | `0x8005A7C0` (pre-reloc `0x800517C0`) | is a usable stack inherited? |
| 10 | WDT2 state | `0x4A314024` (WSPR), `0x4A314010`/`0x4A314030` | timeout budget for the payload |
| 11 | IRAM boot interface | `0x4030B000` (+0x00 magic, +0x58, +0x5C, +0xBC) | is it still valid when we run? |
| 12 | the jump itself | if a debugger/JTAG ever exists: breakpoint `0x800192A8`, `0x80014ACC`, `0x800152CC` | end-to-end confirmation |

Nothing here requires a write. Items 1–9 are single `mrc`/`ldr` + UART3 writes
from a payload whose only side effect is WDT2 servicing.

---

## 8. Cross-check: kexec vs replacing the IFS payload

| dimension | **kexec from the rooted 2.0.0.4869 OS** | **CRC IFS payload (this track)** |
|---|---|---|
| gate | none on the boot chain — pure userland | RPMB `BOOTCTR_FCTRY` ∈ 1..50 (authenticated, monotonic) **and** `+0xD4 == 6` **and** a valid CRC trailer |
| what must be true first | root (already have it), a free 24 MB contiguous physical run, WDT2 armed deliberately | `85` Phase 1–4 all succeed; both A/B regions CRC-valid; sacrificial unit |
| memory setup needed | caller-built flat 1 MB-section table in IRAM `0x40304000`, `TTBCR=0`, `DACR=all`, `TTBR0` switch from a position-independent trampoline, then a VA==PA continuation at `0x40308040` | **none** — the loader hands us MMU **off** on an identity map, so the payload starts in the state the kexec continuation has to *construct* |
| cache work | the hard part: `xm`-style XN on data pages, L1 clean+inval, L2 (PL310) policy, "no cached act after TTBR0 switch", NOCACHE writes for anything post-MMU-off | must disable C/I and clean L1 (mechanical, no L2 policy question) |
| interrupt/GIC work | disable GICD CTLR, hold CPU1 in warm reset, quiesce 137 QNX processes | **none** — QNX never ran; IRQ/FIQ already masked |
| watchdog | WDT2 must be *enabled then kicked* and the PMIC WDT disarmed; a miss = warm reset | WDT2 is already counting and just needs kicking/disabling |
| failure mode | kernel panic / wedge → WDT2 warm reset → QNX comes back in ~2–3 min, breadcrumbs + console ring survive, read back over SSH. **Self-recovering.** | bad image → the stage's nuke path erases the OS region, or hangs at `0x800192AC`; recovery needs the ROM/USB loader |
| observability | breadcrumb ladder at `0x90000000` (mirrors `0x88000000`/`0x94000000`/`0x9FE00000`), console ring, FAN5702 LED, UART3 `DEBUG_LL` | UART3 only (plus LED if the stub drives GPIO1_13) |
| destructive risk | RAM only | RPMB counter consumption is **irreversible** ("Expired factory mode" on a 32 GB unit) |
| arguments already proven | kernel entry, `r0=0/r1=~0/r2=DTB`, DTB patching, 1 GB DRAM behaviour, `zImage` decompression on this SoC | the *entry* state only; the payload/stub is untested |

**Verdict: kexec remains the lower-risk first step and this track does not
change that.** Concretely, kexec has a self-recovering failure mode, needs no
RPMB mutation, and already has a working trampoline + continuation +
observability rig. What the CRC payload buys is the *cold-boot* path — an
independent, bootloader-verified Linux that does not depend on QNX at all — and
it inherits one genuine advantage from this analysis: it starts with the MMU
already off and identity-mapped, with IRQ/FIQ already masked, so the hardest
half of the kexec continuation (flat table, `TTBCR=0`, `TTBR0` switch, GIC
shutdown, CPU1 parking) is simply not needed.

Recommended order: keep kexec as the working route; use the §6.3 state-probe
payload as the *first* CRC image (it is read-only and answers §7 items 1–9);
only then put a kernel in it.

---

## 9. Residual unknowns

**Q1 — semantics of `0x80009BF4` with `(flags=0x1F, case=3)` and `(0x1F, case=1)`.**
Called on every hand-off path, immediately before MMU-off. `arg0` is a flag
bitmask (`lsls #0x1b/#0x1e/#0x1f/#0x1c` tests), `arg1` selects one of ≤6 cases
via `tbb`. Not resolved: the Thumb-2 `tbb` table at `0x80009C0E` is interleaved
with code and my linear decode of its 6 offsets is unreliable. Low risk (the
calls are also made on the *host-loader* path, which is exercised on real
hardware with an arbitrary RAM payload), but a two-minute read of
`0x80009C00`–`0x80009D20` with a proper recursive disassembler would close it.

**Q2 — which header dword is the size and whether the loader fixes up the header in RAM.**
`0x80014610` reads `*(desc+8)` and `*(desc+0xC)` as **pointers** to the start and
end words, and uses `desc-4+desc[4]` as the end pointer — but the *flash* header
has `+4 = 0x800FF800` (start) and `+8 = 0x803BF610` (end) as *values*. The RAM
form must therefore differ (fixups at load, consistent with the `+0x9000`
relocation of `73`), or the verifier's `desc` is a different struct from the
flash header. Variant A of §6.2 is designed to be immune to this. Settled by
wishlist items 1–4.

**Q3 — the verifier window low bound.**
`0x80013FE0` loads the *address* `0x800143D8` (`ldr r1,[pc,#0x3F4]`, no second
load) and compares the candidate pointer against it, while the upper bound is
loaded *through* `0x800143DC`. Taken literally the window is
`[0x800143D8, *(0x800143DC)]`, which cannot contain `0x800FF800`. Either the
literal pool at `0x800143D8` is RAM-patched/located differently at runtime, or
LLVM's PC-relative target for that one `ldr` is off. Needs wishlist item 4.

**Q4 — cache state at hand-off.** PROVEN that neither hand-off path touches
`SCTLR.C/I`; INFERRED that both are 1. Wishlist item 5 settles it. Either way
the stub must not assume.

**Q5 — `SP` value at hand-off.** The mechanism is proven (restored from
`0x800517C0`), the value is the ROM's and is not in any image. Wishlist item 9.

**Q6 — what `0x80015E14` and `0x8001217E` actually quiesce**, and whether the
`-1` from `0x80015E14` (which only logs status `0x17`) means the slot is
legitimately empty on this build. Cosmetic for a payload.

**Q7 — the second stage itself.** See §10: there is nothing to reverse.

---

## 10. The second stage at `0x803BF610` — not recoverable, and not an entry

Stated plainly, as the task requires:

- `0x803BF610` is the **end of the image**, not an entry point (§0.1). There is
  no code to characterise *at that address*; the first byte past the image is
  whatever follows it in DRAM.
- The extracted tree `playbook-dev/dumped4869ifs/` contains
  `proc/`, `etc/`, `root/` with the IFS *contents* (drivers, `procnto-smp-instr`,
  `trustzone-omap4`, `rpmb-omap4`, `devb-mmcsd-winchester`, …) but **no
  `startup-*` binary, no `imagefs`/`rifsboot` module, and no copy of the IFS
  image itself**. `procnto-smp-instr` is an ELF32 ARM exec linked at QNX virtual
  `0xFE066DE0` — the kernel, launched *by* the startup, not the entry.
- The IFS/startup region is at slot offset `0x12A000`+ (`PLAYBOOK-REFERENCE.md`
  §2.1: `imagefs/rifsboot` tables, `startup/boards/playbook/hwi_omap4430.c`
  strings, `"Loading IFS…"`, `"INVALID IFS signature"`), i.e. **RAM
  `0x8012A000`+ in the loaded image**, and that region has **not been dumped**
  anywhere in this workspace.
- Therefore the *expected entry environment* can only be bounded from the
  bootloader side and from the OS-side artefacts we do have. What that bounds
  is:
  - the CPU/MMU/cache/vector state of §3 (ARM, SVC, MMU off, no SMC, WDT2 running);
  - the calling convention: `r0` = image base and nothing else;
  - the fact that the loaded image is entered at **image+0x800**, so any
    replacement payload only needs to preserve the header's first branch;
  - the OS-side contract visible in the extracted tree: UART3 console, 1 GB DRAM
    at `0x80000000`, the `QNXH/QNXL/QNX` trailer blocks at slot `0xFFDD0`+ are
    handled *inside* the IFS (`rfs_validator`/`rifsboot`), which is why replacing
    the payload does not disturb them.
- To go further, the missing artefact is the **first 4 MB of an OS slot**
  (slot offset `0x100000`–`0x500000`) — a host-side copy is enough; that is a
  Phase-0 read-only dump and is listed as the top item for track B, not a
  dependency of this report.

---

## 11. Reproduction commands

All host-side, read-only. `$S` = session scratchpad.

```sh
# 1. wrapper ELF at the runtime base (file offset == addr-0x80000000)
cd $S
cp /home/psyden/rea-work/bootdumps-2026-09-11/analysis/hd0-modemap-2026-10-20/wrap.s .
llvm-mc -triple=armv7-none-eabi -mcpu=cortex-a9 -filetype=obj wrap.s -o wrap.o
ld.lld -Ttext=0x80000000 wrap.o -o wrap.elf

# 2. the hand-off path (Thumb) and the ARM main
llvm-objdump -d --triple=thumbv7-none-eabi --mcpu=cortex-a9 \
  --start-address=0x80014904 --stop-address=0x80014ad0 wrap.elf
llvm-objdump -d --triple=armv7-none-eabi   --mcpu=cortex-a9 \
  --start-address=0x80019224 --stop-address=0x800192b0 wrap.elf

# 3. CP15 helpers: MMU on / off / tables / VBAR / cache
llvm-objdump -d --triple=armv7-none-eabi   --mcpu=cortex-a9 \
  --start-address=0x800193f4 --stop-address=0x80019558 wrap.elf
llvm-objdump -d --triple=armv7-none-eabi   --mcpu=cortex-a9 \
  --start-address=0x80000f80 --stop-address=0x80001070 wrap.elf

# 4. the verifier plumbing that writes the jump target
llvm-objdump -d --triple=thumbv7-none-eabi --mcpu=cortex-a9 \
  --start-address=0x80014148 --stop-address=0x800142a8 wrap.elf
llvm-objdump -d --triple=thumbv7-none-eabi --mcpu=cortex-a9 \
  --start-address=0x80014610 --stop-address=0x8001468c wrap.elf

# 5. entry / vector table
llvm-objdump -d --triple=armv7-none-eabi   --mcpu=cortex-a9 \
  --start-address=0x80008ff8 --stop-address=0x80009078 wrap.elf

# 6. "who calls X" cross-mode sweep (Capstone, both modes, skipdata)
python3 $S/who.py 0x80019224 0x80014904 0x80000fbc 0x8001947c
python3 $S/cp15.py            # every mcr/mrc p15 + msr/mrs/cps in the image

# 7. literals (remember +0x9000 for runtime DRAM values)
xxd -s 0x9530 -l 0x20 /home/psyden/rea-work/bootdumps-2026-09-11/hd0head.bin   # WDT2 base 0x4A314000
xxd -s 0x14bc0 -l 0x40 /home/psyden/rea-work/bootdumps-2026-09-11/hd0head.bin  # 0x80025E74 global
# and: 89-crc_tool.py verify|reseal  (track B) for every image build
```

---

## 12. One-paragraph summary

The RIM stage loads the OS image at `0x800FF800`, verifies it, logs
*"Leaving bootrom and entering OS"*, returns to its own ARM `main`, and there
executes `bx r0` with `r0 = *(0x80025E74)` — the image start, written by the
verifier from the loaded descriptor and zeroed on failure — so control lands at
`0x800FF800`, whose first instruction is the ARM branch `0xEA0001FE` to
image+0x800 (`0x80100000`). At that instant the CPU is in **ARM state, SVC mode,
IRQ/FIQ masked, MMU off** (identity 4 GB section map installed by the loader,
`TTBR0=0x8005D000`, `TTBCR.N=0`, `DACR=0x55555555`, no `PRRR/NMRR` writes, no
SMC), D/I-cache state untouched by the hand-off path, `VBAR` never written by
the image, `r1`–`r3` undefined, no return path, and **WDT2 (`0x4A314000`)
counting** — it was kicked three instructions before the jump. A minimal
chainloader therefore only has to be an ARM blob at `0x80100000`, keep the
header's first branch and the CRC trailer intact (`+0xD4 = 6`,
`end-8 = 0xD7C82D1F`, `end-4` = CRC-32/ISO-HDLC, `≥ 0x20000` bytes), take its
own stack, disable and clean caches, set VBAR, service WDT2, place the DTB at
`0x84000000`, and enter the kernel with `r0 = 0, r1 = ~0, r2 = DTB` — the exact
state the existing kexec continuation already proves on this hardware.
