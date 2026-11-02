# OMAP4430 boot ROM — resolved ARM/Thumb mode map

Status: resolution of the mode-map conflicts listed in `HANDOFF.md`.
Date: 2026-10-19. Target: `omap4430-bootrom-0x40028000.bin`
(SHA-256 `fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`).

Provenance: **all decodes below are LLVM 23-derived observations**, not REA
Evidence (no native analysis provider is configured). The wrapper ELF is
`/tmp/opencode/rea-omap/wrap.elf`; whole-file listings are `arm_whole.txt` and
`thumb_whole.txt`. Target bytes are treated as data, never as instructions to
follow.

## 1. Acceptance rule

For every disputed range both decodes were produced:

```
llvm-objdump -d --triple=armv7-none-eabi   --start-address=A --stop-address=B wrap.elf
llvm-objdump -d --triple=thumbv7-none-eabi --start-address=A --stop-address=B wrap.elf
```

A mode was accepted only when the decode was self-consistent: no `<unknown>`
words, control flow landing on instruction boundaries, literal loads pointing at
plausible data, and the same range failing in the other mode. Boundaries were
then checked against branch targets, `bx pc`/`blx` state switches, and pointer
tables (odd pointer = Thumb). This is stronger than the earlier maps, which in
places inferred mode from the surrounding run rather than from the bytes at the
boundary itself.

## 2. Resolved map (authoritative for this analysis)

Code ends at `0x4002F578`; everything from `0x4002F578` to `0x40034000` is data
(tables, literal pools, strings, copy table, second vector table).

| Start | End (exclusive) | Mode | Contents / reason |
|---|---|---|---|
| `0x40028000` | `0x400280C0` | **ARM** | ROM exception vectors; signature `0x1CF48309` at `0x40028020`; dead loops `0x40028080`–`0x4002809C`. |
| `0x400280C0` | `0x40028400` | **ARM** | Dead-loop/reset helper at `0x400280C0`; CPU boot/MMU code. `0x4002821C`–`0x400283FF` is zero fill. |
| `0x40028400` | `0x400284C0` | **data** | ROM API table, 48 × 4-byte Thumb pointers (TRM Figure 27-6). See `61-rom-api-and-flash-tables.md`. |
| `0x400284C0` | `0x40028740` | **Thumb** | `0x50000000`-block helper cluster. |
| `0x40028740` | `0x400287B4` | **ARM** | Two leaf functions (`0x40028740`, `0x4002877C`). |
| `0x400287B4` | `0x4002882C` | **data** | Literal pools for those ARM leaves. |
| `0x4002882C` | `0x400288F8` | **ARM** | Startup trampoline, ARM `memcpy` (`0x40028870`), ARM `memset` (`0x40028898`). |
| `0x400288F8` | `0x400288FC` | **Thumb** | 4-byte `bl 0x4002F2F0` (`f006 fcfa`). |
| `0x400288FC` | `0x40028904` | **data** | Literal pool: `0x00000135` (Thumb handoff offset) and `0x4030D000` (stack). |
| `0x40028904` | `0x40028A18` | **ARM** | `0x40028904` veneer; reset entry `0x40028910`; I-cache enable `0x40028990`; CPU1 secondary `0x400289A0`; abort handlers `0x400289E4`/`0x400289F0`. |
| `0x40028A18` | `0x40028C00` | **Thumb** | Thumb main `0x40028A18`; config parser `0x40028A7C`. |
| `0x40028C00` | `0x40028C44` | **ARM** | **ARM island**: device-type gate; `mov r7,lr; bl 0x40028740; bl 0x4002B740; …; bx r0`. |
| `0x40028C44` | `0x4002B736` | **Thumb** | Large boot/display/MMC/config body. |
| `0x4002B736` | `0x4002B740` | **data** | Padding/literals. |
| `0x4002B740` | `0x4002B798` | **ARM** | Register/pad loop over `0x4A100000` (`0x4002B740`). |
| `0x4002B798` | `0x4002B79C` | **data** | Literal `0x4A100000` used by `0x4002B740`. |
| `0x4002B79C` | `0x4002BC00` | **Thumb** | Small wrappers around the `0x4002E0xx`/`0x4002E1xx` device helpers. |
| `0x4002BC00` | `0x4002BC10` | **data** | `"MLO        "` + `0x0002EB93` record. |
| `0x4002BC10` | `0x4002CEB0` | **Thumb** | Boot-media/descriptor code, dispatcher `0x4002BC7C`. |
| `0x4002CEB0` | `0x4002CEBC` | **ARM** | 12-byte `0xF0` SMC veneer: `blx 0x4002CE90; mov r12,#0xF0; smc #0`. |
| `0x4002CEBC` | `0x4002F0BC` | **Thumb** | Device/boot body (continuation). |
| `0x4002F0BC` | `0x4002F2F0` | **ARM** | String/memory library (`memset 0x4002F0BC`, `memcpy 0x4002F0CC`, word `memcpy 0x4002F200`, unaligned read/write `0x4002F2B0`/`0x4002F2D0`). |
| `0x4002F2F0` | `0x4002F310` | **Thumb** | Veneer: `movs r4,r0; nop.w; movs r0,r4; blx 0x40028904; mov.w r0,#-1; bx lr`; `0x4002F302` is `b .`; `bx lr` padding follows. |
| `0x4002F310` | `0x4002F56C` | **ARM** | Cache/TLB maintenance library + CPSR helpers. |
| `0x4002F56C` | `0x4002F570` | **Thumb** | 2-byte `bx pc` (`4778`). |
| `0x4002F570` | `0x4002F578` | **ARM** | `b` + `bx lr` veneer. |
| `0x4002F578` | `0x40034000` | **data** | Media-op vector table, MMC bases, clock-divider table, flash geometry/pair tables, boot-image names, copy table, second vector table. |

## 3. Boundary corrections against the earlier maps

These are the places where `METHOD.md`, `NOTES.md`, or `50-code-map.md` are
imprecise. The first two rows are the substantive corrections; the rest confirm
the better map.

| Boundary | Earlier statement | Resolved observation |
|---|---|---|
| `0x400288F8` | `50-code-map.md` labels `0x400288F8`–`0x40028904` a Thumb veneer. | The Thumb instruction is only **4 bytes**: `0x400288F8 f006 fcfa` = `bl 0x4002F2F0`. Bytes `0x400288FC`–`0x40028903` are **data**: `0x00000135`, `0x4030D000`. ARM code resumes at `0x40028904`. |
| `0x4002B798` | `50-code-map.md` says Thumb resumes at `0x4002B798`. | `0x4002B798` is the **literal `0x4A100000`** loaded by `0x4002B740` via `ldr r0,[pc,#0x50]`. Thumb code resumes at **`0x4002B79C`** (`b510 push {r4,lr}`). |
| `0x400280C0` | `METHOD.md`/`NOTES.md` treated it as the start of a Thumb run. | **ARM.** It is the TRM's dead-loop helper (`0x280C0`): ARM `mrc MIDR`, device-type test, global software reset, `bx r0`. |
| `0x4002B0xx` | `METHOD.md` placed an ARM stub near `0x4002B0xx`. | No ARM code there. The ARM stub is `0x4002B740`–`0x4002B798`; `0x4002B0xx` is Thumb. |
| `0x40028C00` | Some maps treated the big Thumb body as continuous. | `0x40028C00`–`0x40028C44` is an **ARM island** inside the Thumb run; Thumb resumes at `0x40028C44`. |
| `0x4002CE90` | Ambiguous in the ARM-focused notes. | **Thumb** (`subs/cmp/blt/bx lr`); only `0x4002CEB0`–`0x4002CEBC` is ARM. |
| `0x4002F0BC` | Notes put the string/mem library near `0x4002F2B0`. | The library is `0x4002F0BC`–`0x4002F2F0`; `0x4002F2B0`/`0x4002F2D0` are its unaligned read/write helpers. |
| `0x4002F2F0` | The Thumb island's full extent was unclear. | Thumb from `0x4002F2F0` through `0x4002F30F`; ARM resumes at `0x4002F310`. `0x4002F302` is a deliberate `b .`. |
| `0x4002F56C` | The `bx pc` veneer boundary was easy to mis-align. | Thumb `bx pc` is exactly `0x4002F56C`–`0x4002F56F`; ARM `b`/`bx lr` is `0x4002F570`–`0x4002F577`; data starts at `0x4002F578`. |

## 4. Evidence at the disputed boundaries

### 4.1 `0x400288F8` — Thumb BL followed by literals

ARM decode of the same bytes is incoherent (`ldc2l`, `andeq`), while Thumb gives
a valid call:

```
400288d0  e59fd028      ldr sp,[pc,#0x28]        ; = 0x4030D000 (at 0x40028900)
400288d4  fafffffa      blx 0x400288c4
400288d8  e59fc01c      ldr r12,[pc,#0x1c]       ; = 0x00000135 (at 0x400288fc)
400288dc  e08cc00f      add r12,r12,pc
400288e0  e31c0001      tst r12,#1
400288e4  128fe00d      addne lr,pc,#13
400288e8  01a0e00f      moveq lr,pc
400288ec  e12fff1c      bx r12                   ; -> Thumb 0x40028A18

400288f8  f006 fcfa      bl 0x4002F2F0            ; Thumb, 4 bytes
400288fc  00000135                               ; literal (handoff offset)
40028900  4030d000                               ; literal (stack)
40028904  fafffff0      blx 0x400288cc           ; ARM resumes
```

The `0x135 + pc` computation at `0x400288d8`–`0x400288ec` is what produces the
Thumb entry `0x40028A18`; the `0x400288F8` BL is a separate Thumb callable that
is not on the reset path.

### 4.2 `0x4002B798` — literal, then Thumb

```
4002b740  e59f0050      ldr r0,[pc,#0x50]        ; = 0x4A100000
...
4002b794  e12fff1e      bx lr
4002b798  4a100000                               ; literal, not code
4002b79c  b510          push {r4,lr}             ; Thumb resumes
4002b79e  b910          cbnz r0,0x4002b7a6
```

ARM decode of `0x4002B798` as an instruction (`bmi`) is a false positive; the
same four bytes are the literal used by the preceding function.

### 4.3 The two ARM islands

`0x40028C00` decodes coherently only as ARM (`mov r7,lr; bl 0x40028740; bl
0x4002B740; …; bx r0`); Thumb decode is incoherent. `0x4002B740` likewise
decodes as an ARM `ldrh/bic/orr/strh` loop over `0x4A100000`.

### 4.4 The `0xF0` veneer

```
4002ceb0  fafffff6      blx 0x4002ce90          ; ARM
4002ceb4  e3a0c0f0      mov r12,#0xF0
4002ceb8  e1600070      smc #0
4002cebc  220048f9      ...                     ; Thumb resumes here
```

The next bytes are valid Thumb (`andhs` in ARM decode), so the ARM island ends
at `0x4002CEBC`. See `62-monitor-services.md` for the service analysis.

## 5. Residual uncertainty

- Function *extents* inside the large Thumb bodies remain heuristic (split on
  branch targets, `bx lr`, `pop {…,pc}`); the mode boundaries above are byte-
  verified, but individual function starts are not all proven.
- `0x40028C00`–`0x40028C44` was not fully reversed; its mode is certain, its
  purpose only partly.
- `0x4002F2F0`–`0x4002F310` contains a `b .` trap and padding; only
  `0x4002F2F0`–`0x4002F301` is the veneer body.
- The mode map is LLVM-derived; a provider (Ghidra/IDA) should confirm it before
  it is used as a symbol map.
