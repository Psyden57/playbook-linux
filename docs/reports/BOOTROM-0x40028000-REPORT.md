# OMAP4430 Boot ROM at 0x40028000 — Consolidated Reverse-Engineering Report

Status: final consolidation of the completed static-RE investigation.
Date: 2026-10-19. Supersedes the topic notes where they disagree (see §7).

This report is self-contained. Raw dumps and per-topic disassembly are **not**
reproduced here; each statement points at the topic report that holds the raw
listing (`10-boot-media.md`, `20-config-parser.md`, `30-monitor-security.md`,
`40-iram-and-tables.md`, `50-code-map.md`) and at the exact address(es) it rests
on.

> Naming caveat: every human-readable function name below (`reset entry`,
> `Thumb main`, `config parser`, `do_warm_reset`, …) is a **descriptive label
> assigned during this analysis**, not a symbol recovered from the image. No
> symbol table or build metadata exists in the dump.

---

## 1. Target identity

| Property | Value | Source |
|---|---|---|
| File | `/home/psyden/rea-work/bootdumps-2026-09-11/omap4430-bootrom-0x40028000.bin` | — |
| Size | 49152 bytes (0xC000) | byte count |
| Load / dump base | `0x40028000` | dump provenance |
| End of window | `0x40033FFF` inclusive | size + base |
| SHA-256 | `fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961` | hash of the file |
| Architecture | ARMv7-A (Cortex-A9), little-endian, mixed A32 + Thumb-2, **no file header** | decode (50-code-map §1) |
| Boot alias | `0x00028000`-based; VBAR is set to `0x00028000` (literal `0x40028A00`) | 30 §3.1, 40 §5 |
| Provenance | Read as NS-visible memory from a rooted QNX device with `memdump3`, fresh post-battery-pull (DRAM+RTC wiped) | `README.md`, `BOOTROM-RE.md` (prior claims) |
| Separate evidence | `bootrom-lower-0x40020000.bin` = 28456 bytes, base `0x40020000`, covers `0x40020000`-`0x40026F24` (a different dump; only referenced where marked) | 30 §1.3/§3.2 |

There is **no REA Evidence for this target**. See §2.

---

## 2. Method and evidence provenance

### 2.1 There is no native analysis provider

The REA MCP server `rea` (v6.7.0, 207 tools) is registered and connected, but
**no native analysis provider is configured on this host**: Ghidra, Hopper, IDA
and Kuna are all unavailable. `rea setup` and engine installation were
deliberately **not** run for this task (`METHOD.md`, `HANDOFF.md`).

Consequently **no tool output in this investigation is REA Evidence.** Every
disassembly, mode boundary, cross-reference and literal value was produced by
the LLVM 23 toolchain plus Python, and must be treated as **LLVM-derived
observation** only.

### 2.2 The LLVM 23 recipe actually used

1. Wrap the raw image as an ELF with a `.text` section based at `0x40028000`:
   `llvm-mc -triple=armv7-none-eabi -mcpu=cortex-a9 wrap.s -o wrap.o` then
   `ld.lld -Ttext=0x40028000`.
2. Disassemble per range, forcing the mode with
   `llvm-objdump -d --triple=armv7-...` or `--triple=thumbv7-...`
   (`--triple` overrides the ELF mapping, so one wrapper ELF suffices).
3. Python `struct` reads over the raw bytes for word/halfword dumps and
   literal cross-reference scans.

Whole-file listings and the wrapper live under `/tmp/opencode/rea-omap/`
(`arm_whole.txt`, `thumb_whole.txt`, `wrap.elf`).

### 2.3 Dump provenance and boundaries

- This file is the TRM "Boot ROM internal" 48-KB window, NS-readable.
- The upper boundary `0x40034000` was probed and gave SIGBUS (prior claim,
  `README.md`).
- A separate lower dump covers `0x40020000`-`0x40026F24`; `0x40026F28`-`0x40027FFF`
  is a ~4 KB non-readable hole (see §6/§8, inference).
- First-pass prior work (`README.md`, `BOOTROM-RE.md`) is treated as **claims to
  verify**, not ground truth; corrections are in §7.

### 2.4 Evidence tiers used throughout

- **Observation** — directly reproducible from the bytes (a decoded
  instruction, a literal value, a byte-exact table).
- **Inference** — a reading that goes beyond the bytes (peripheral identity,
  protocol semantics, why something exists).
- **Unknown** — not established by either.

### 2.5 Public documentation used

The OMAP4430/4460 TRM `swpu231ap.pdf` was added to the workspace during this
session. It is used as **public-document context**, not as evidence about the
dump. The sections that bear on this report are:

- **Figure 27-6 / §27.4.2.1** (p. 5638) — ROM memory map: "API table" at
  `0x28400`, code at `0x28100`, dead loops at `0x28080`, CRC at `0x28020`,
  vectors at `0x28000`. This identifies the 48-word block at `0x40028400`.
- **Figure 27-5** (p. 5637) — ROM architecture includes a **`SEC_ENTRY`**
  module.
- **§27.5** (pp. 5703-5708) — HLOS monitor services `0x100`–`0x113`, called with
  the function ID in `R12` and `SMC`; the example uses `SMC #1` and saves
  `R1-R12`.
- **Figure 27-7** (p. 5639) — IRAM layout: downloaded image at `0x40300000`,
  stack at `0x4030C000`, static variables/vectors/tracing at `0x4030D000`+.

Where the TRM and the bytes agree, the report cites both. Where the TRM is
silent (`0xF0`, `0xFE`, `0xF1`), the report keeps the observation/inference
split.

---

## 3. Boot sequence walkthrough (reset → handoff → Thumb main)

All addresses are ROM addresses; `0x0002xxxx` values are the **boot-alias**
(VA `0x00028000` maps to PA `0x40028000`, because VBAR is `0x00028000`).

**Step 1 — Reset vector (`0x40028000`, ARM).**
Word `0xEA000242` = `b 0x40028910`. The next 7 entries (`0x40028004`-`0x4002801C`)
are `ldr pc,[pc,#0x18]`, each resolving into the literal block at
`0x40028024`-`0x4002803C` holding IRAM pointers `0x4030D004`-`0x4030D01C`.
`0x40028020` holds the signature word `0x1CF48309` (meaning unknown, §6).
Evidence: 40 §5, 50 §2a.

**Step 2 — Reset entry (`0x40028910`, ARM).**
Reads MIDR (`mrc p15,0,r0,c0,c0,5`). If `MIDR[1:0] != 0` it branches to the CPU1
secondary path `0x400289A0` (§4.4). Otherwise (CPU0):

- device-type check; if `[0x4A0022C4]` bits[10:8]==3 (mask `0x700`) it `bleq
  0x40028C00` (the ARM "device-type gate" island). Evidence: `0x4002891C`,
  literals `0x400289FC`; 30 §3.3, 50 §2a.
- `msr CPSR_c,#0xD3` (SVC, IRQ/FIQ masked).
- Write **VBAR = `0x00028000`** at `0x40028954` (literal `0x40028A00`).
- Load **SP = `0x4030D000`** (literal `0x40028A04`).
- `r0 = [0x4A307B04]` (PRM_RSTST) and `blx 0x4002A4D8` to record the reset
  reason into an IRAM descriptor ring.
- `blx 0x4002A51C(0)` — set boot-progress bit 0.
- 8 filler `bl .+4` at `0x40028970`-`0x4002898C`.
- Falls into `0x40028990`: reads SCTLR, `orr #0x1800` (I + Z), writes SCTLR —
  **I-cache on** — then `b 0x4002882C`.

Evidence: 20 §1.1, 30 §3.1, 50 §2a.

**Step 3 — Startup trampoline (`0x4002882C`, ARM).**
`bl 0x40028834` then `bl 0x400288D0`. The walker `0x40028834` computes the copy
table bounds from literals `0x40028868=0x00007718` / `0x4002886C=0x00007758`
added to `pc`, giving the table `0x4002FF80` and end `0x4002FFC0`. It iterates
`{src,dst,len,fn}` records, loading each with `ldm r10!,{r0,r1,r2,r3}` and
calling it with `bx r3` (loop `0x40028848`-`0x40028864`). This is the bootstrap
that stages the IRAM image (§4.1). Evidence: 40 §1, 50 §2a.

**Step 4 — ARM → Thumb handoff (`0x400288D0`, ARM).**
`ldr sp,=0x4030D000` (literal `0x40028900`); `blx 0x400288C4`; then
`ldr r12,=0x00000135` (literal `0x400288FC`), `add r12,r12,pc` → `0x40028A19`,
`bx r12`. The odd target means **Thumb main at `0x40028A18`**. Evidence: 40 §1/§5,
50 §2a.

**Step 5 — Thumb main (`0x40028A18`, Thumb-2).**

```
bl 0x4002A4C0            ; *sp = PRM_RSTST [0x4A307B04]
progress(1)              ; 0x4002A51C
bl 0x40029AE6(6,1)       ; PRCM module #6 clock enable
bl 0x4002A41C            ; 0x4A314000 sysconfig
bl 0x4002A430            ; WDT start  (WSPR 0xAAAA / 0x5555)
bl 0x4002A478(0x4002BF20); WDT load/timeout
bl 0x4002A454            ; WDT stop   (WSPR 0xBBBB / 0x4444)
if *sp==1:  progress(2); flags|=1; *(0x4A326A00)=0
bl 0x40028DB6(sp+4)      ; boot-config source prep
bl 0x40028A7C(sp+4,*sp)  ; config parser  (does not return)
```

Evidence: 20 §1, 50 §2a.

**Step 6 — Config parser (`0x40028A7C`) is a persistent polling loop** that
processes an 8-byte command table and re-processes it forever (§4.3). Control
never returns to the caller as a normal boot "handoff"; the ROM's *onward* handoff
to the boot chain is through the handlers (image load into IRAM
`0x40300000`/`0x40304000`) and the monitor services, not a return value.

---

## 4. Subsystems

### 4.1 Bootstrap and the IRAM image

**Copy table at `0x4002FF80`** — 4 records × 16 bytes, `{arg0, arg1, size, fn}`:

| # | arg0 (src / a) | arg1 (dst / b) | size | fn | operation |
|---|---|---|---|---|---|
| 0 | `0x0002FFC0` | `0x4030D000` | `0x40` | `0x00028870` | memcpy: second vector table → IRAM |
| 1 | `0x00030000` | `0x4030D100` | `0x08` | `0x00028870` | memcpy: 8 zero bytes (IRQ save area) |
| 2 | `0x00030008` | `0x4030D108` | `0x98` | `0x00028870` | memcpy: USB descriptor template |
| 3 | `0x000300A0` | `0x4030D1A0` | `0xABC` | `0x00028898` | memset: zero `0xABC` bytes (BSS) |

`0x40028870` is `memcpy(src=r0,dst=r1,len=r2)`; `0x40028898` is
`memset(dst=r1,len=r2,val=0)` and **does not read arg0** (`0x400300A0`).
Evidence: 40 §1 (byte-exact); see §7-C2 for why record 2/3 descriptions differ
from `NOTES.md`.

**What lands in IRAM:**

| IRAM range | len | contents |
|---|---|---|
| `0x4030D000`-`0x4030D03F` | `0x40` | 8-slot ARM vector table (`ldr pc` ×8 + 8 target words) |
| `0x4030D040`/`0x4030D04C`/`0x4030D058` | — | descriptor-ring nodes (init at `0x4002A4D8`, literals `0x4002A558`/`0x4002A55C`) |
| `0x4030D060` | — | ring control block `{0x4030D040,0x4030D04C,0x4030D058}` |
| `0x4030D100`-`0x4030D107` | `0x08` | zeroed: IRQ-mode `{r6,r7}` save / mini-stack |
| `0x4030D108`-`0x4030D19F` | `0x98` | USB descriptor template |
| `0x4030D1A0`-`0x4030DC5B` | `0xABC` | zeroed BSS/scratch — boot-config struct (`0x4030D1A0`), media/image descriptors |

**The USB template at `0x40030008`** parses as a 6-byte prefix + 18-byte Device
descriptor + 4 × 32-byte Configuration blocks (offsets `0x18/0x38/0x58/0x78`),
total `0x98`. Byte setters exist at `0x4002DB8C`/`0x4002DB92`. `idVendor`/
`idProduct` are zero and are presumably OTP-patched at run time (patch site not
located). Evidence: 40 §3b.

**Data tail:** `0x400300A0`-`0x40030B5C` is **2748/2748 zero bytes**;
`0x40030B5C`-`0x40033FFF` is zero except a single word `0x40033FFC = 0x00000003`
(unknown). Evidence: 40 §3c.

**Vector indirection (why it exists):** the ROM's first table (`0x40028000`)
routes every exception *into IRAM* (`0x4030D004`+), and record 0 copies the second
table (`0x4002FFC0`) onto `0x4030D000`. So payload/runtime code can patch the
**live** vector table in IRAM without touching ROM. The second table's targets,
at `0x4002FFE0`-`0x4002FFFC`, are `0x00028090, 0x00028080, 0x00028084,
0x000289E4, 0x000289F0, 0x00028090, 0x00029420, 0x00028098`. Evidence: 40 §4/§5.

**Reset-reason / progress machinery:** `0x4002A4C0` reads PRM_RSTST
`[0x4A307B04]`; `0x4002A4D8` builds the `0x4030D040`/`0x4030D060` descriptor ring
and stores the reset reason; `0x4002A51C` sets bit `r0` (0..0x5F) in a 3-word
bitmap at `[0x4030D060]` — the **boot-progress/trace vector**, and the single
most-called routine in the ROM (63 call sites). Evidence: 20 §1.1, 50 §2a/§3.

**Watchdog-class block at `0x4A314000`** (literal `0x4002A4B8`): `0x4002A41C`
(sysconfig `+0x10`, poll `+0x14`), `0x4002A430` (WSPR `+0x48` = `0xAAAA`/`0x5555`),
`0x4002A454` (`0xBBBB`/`0x4444`), `0x4002A478` (loads `+0x2C`/`+0x30`; computes a
32.768 kHz count from literal `0xC49BA5E2` at `0x4002A4BC`), with write-posting
status at `+0x34`. Evidence: 20 §1.1. The "WDT" identity is an inference (§6).

### 4.2 Boot media (MMC/SD/flash) and device selection

**MMC controller base table `0x4002FE20`** — 7 u32, indexed by a controller
index in `r0` (referenced as `0x0002FE20` from `0x4002E048` and `0x4002E0A4`):

| idx | base | idx | base |
|---|---|---|---|
| 0 | `0x4809C000` | 4 | `0x480D1000` |
| 1 | `0x480B4000` | 5 | `0x480D5000` |
| 2 | `0x480B4000` | 6 | `0x480B6000` |
| 3 | `0x480AD000` | | |

Entries 1 and 2 are duplicates; entry 6 has no confirmed identity (§8).

**MMCHS register offsets** (observed, not TRM-confirmed): SYSCONFIG `0x110`,
SYSSTATUS `0x114`, CON `0x12C`, BLK `0x204`, ARG `0x208`, **CMD `0x20C`**,
RSP10/32/54/76 `0x210`-`0x21C`, DATA `0x220`, PSTATE `0x224`, HCTL `0x228`,
SYSCTL `0x22C`, STAT `0x230`, IE `0x234`, CAPA `0x240`, REV `0x2FC`. Pool words:
`0x4002E04C=0x307F0033` (IE), `0x4002E050=0xFFF0FFFA` (SYSCTL mask),
`0x4002E054=0x4A304000` (timer), `0x4002E058=0x337F87FF` (STAT W1C),
`0x4002E05C=0x00200030` (transfer-setup command). Evidence: 10 §2.2.

**Command descriptor word written raw to CMD `0x20C`** (by `0x4002DDD4`):

- **bits[31:24] = MMC command index.** Verified against CMD0 `0x00000000`,
  CMD1 `0x01020000`, CMD2 `0x02090000`, CMD3 `0x031A0000`, CMD4 `0x04040000`,
  CMD6 `0x061A0000`/`0x061B0000`, CMD7 `0x071B0000`, CMD8 `0x081A0000`,
  CMD9 `0x09090000`, CMD16 `0x101A0000`, CMD17 `0x113A0010`, CMD55 `0x371A0000`,
  ACMD41 `0x29020000`.
- **bits[17:16] = response-length selector.** `0x4002DE34` does
  `ubfx r1,r1,#0x10,#2; cmp r1,#1`: value 1 → a 128/136-bit response
  (RSP10..RSP76), else 48-bit. Both 128-bit commands (CMD2, CMD9) carry flag
  byte `0x09` → bits[17:16]=1, matching R2 CID/CSD semantics.
- Remaining flag bits are **not decoded** (§8).

> Note: this is **not** the SDHCI 16-bit command-register layout. Stated as
> observation of the bytes; the TRM was not consulted. Evidence: 10 §2.3.

**Controller primitives** (Thumb, `r0` = controller index): reset `0x4002DC68`,
set clock divider `0x4002DCD6`, power-on init stream `0x4002DD2C`, set bus width
`0x4002DD8E`, send command `0x4002DDD4`, read block `0x4002DE60`, write block
`0x4002DF58`, transfer setup/abort `0x4002DFB0`, command0+wait `0x4002E060`.
Timeouts use the free-running timer at `0x4A304000` (`+0x10`). Evidence: 10 §2.4.

**Card/device layer**: init part 1 `0x4002E930` sends CMD0 → CMD8(arg `0x1AA`)
→ CMD55 (success ⇒ SD, `dev[4]=2`; failure ⇒ MMC, `dev[4]=1`) → CMD1/ACMD41 poll
→ CMD2 → CMD3, with RCA `0x1234` for MMC. Init part 2 `0x4002EA5E` (CMD9, CMD4,
clock-divider lookup, CMD7, CMD16, capacity). Inner-record card type is
`dev[+0x04]` (1=MMC, 2=SD). Evidence: 10 §3.1/§3.2.

**Clock-divider table `0x4002FE3C`** — 15 rows × 4 u16, ending exactly at
`0x4002FEB4`; indexed at `0x4002E8CE` as `table[M*8+N*2-8]<<1` with
`N=resp[0xc]&7`, `M=(resp[0xc]>>3)&0xF`. Row 1 = `0x03CB/0x0062/0x000A/0x0001`,
decreasing monotonically. Interpretation (TRAN_SPEED → CLKD) is an inference.
Evidence: 10 §3.3.

**Outer device record / abstraction**: `+0x04` is the **boot-device type byte**
(the media command 5/6/7/8); `+0x18` points to the inner record. The type→
controller map is at **`0x4002EC38`**: `5→idx 0`, `6→idx 1`, `7→idx 2`, `8→idx 6`,
after which the ROM enables the matching PRCM module via `0x4002A95A(1,idx)` /
`0x40029AE6(1,idx)`. Open/read/ioctl are `0x4002BA54` / `0x4002BB1A` /
`0x4002BBD0`. Evidence: 10 §4.

**Media-op vector table `0x4002F578`** (stride 12, one entry per command 1..8):

| cmd | open | read | ioctl | inferred class |
|---|---|---|---|---|
| 1, 2 | `0x4002B809` | `0x4002B855` | `0x4002B86F` | memory-mapped/XIP at `0x08000000` |
| 3, 4 | `0x4002B879` | `0x4002B933` | `0x4002B963` | flash/NOR via `0x50000000` GPMC-class regs |
| 5, 6, 7 | `0x4002BA55` | `0x4002BB1B` | `0x4002BBD1` | MMC/SD (device type = cmd byte) |
| 8 | `0x4002BC11` | `0x4002BC59` | `0x4002BC73` | separate mmap source at `0xC0000000` |

Evidence: 10 §4, 20 §3.1.

**Flash/NOR path**: read-ID command `0x90` at `0x4002E25C`; probe `0x4002E6BA`;
identify `0x4002B970`; geometry decode `0x4002EF14` uses the 3-byte table at
`0x4002FEB4` (9 records, `0x1B` bytes) indexed by the pair table at `0x4002FECF`
(46 `{code,index}` pairs + `00` terminator at `0x4002FF2B`; e.g. `83→3`,
`93→4`, `F0→1`, …). The consumer loads base `0x0002FECF` from the literal at
`0x4002F000`, subtracts `0x1B` to get `0x4002FEB4`, and indexes `index*3`.
Memory window `0x08000000`, register window `0x500000007C/80/84`-class.
Byte-exact decode, field semantics and the proof that `0x4002FEE0` is the
mis-aligned tail of this table are in `61-rom-api-and-flash-tables.md` §3.
Evidence: 10 §5, 61 §3.

### 4.3 Configuration parsing (the "CH*" / config-header path)

**Config parser `0x40028A7C(flags_ptr, reset_reason)`** — a **persistent polling
loop**. Each outer pass clears the result word to `0xFFFFFFFF`, then iterates the
8 command bytes at `sp+0x18`:

- byte `1..8` → media handler `0x4002AD5C(flags, c, struct+0x64)`;
- byte `0x41..0x47` (`'A'..'G'`) → image handler
  `0x4002AFFE(flags, c, &slot, struct+0x64)`;
- anything else ignored.

The table is **mutable**: on one result path it writes `table[r4+1]=res>>8`, on
another it zeroes every entry `>= 0x40`. Result codes seen: `0xFFFFFFFF`,
`0xF0030002`, `0xF0030003`, and the masked family `(res & ~0xFF00)==0xF0030006`
(accumulator literal `0x40028BFC = 0xF0030003`). Evidence: 20 §2.

**Result structure base is an IRAM address**: literal `0x40028BF8 = 0x4030D1A0`
(the BSS zeroed by bootstrap record 3). Sub-fields: `+0x64`=current-command
result (`0x4030D204`), `+0x68`, `+0x6C`=command index, `+0x6D`=reset-reason byte;
`+0x70`=image descriptor, `+0x98`=media descriptor, `+0xB8`=media sub-record.
The literal is referenced by exactly three words: `0x40028BF8`, `0x4002C918`,
`0x4002FFB4`. Evidence: 20 §5.

**Command-table builder `0x40028E72(flags,dst,src)`**: if `flags & 0x200`, copies
four u16 command bytes from the stored config block; else indexes a
16-byte-per-entry device-type table (base literal `0x4002A57C = 0x0002F7C4`) via
`0x4002A560`. Evidence: 20 §2.1.

**Boot-config block reader `0x4002B64E(type,out)`**: walks a list from
`[0x4A326A00]` up to `0x4A327000`, matching a 32-bit header
`0x00CF00AA | type` (signature bytes `CF 00 AA` then a type byte); on match it
copies `len` bytes from `p+8`. Types requested: 1, 2, 3. Main clears
`*(0x4A326A00)` on a cold reset (`*sp==1`). Evidence: 20 §1.2.

**Boot-image / config-name table `0x4002FA84`** — 14 pointers, terminated by 0 at
`0x4002FABC`, used by matcher `0x4002C254` (literal `0x4002C418=0x0002FA84`).

Byte-exact string offsets in `0x4002FF2D`-`0x4002FF7E`:

| addr | text | addr | text |
|---|---|---|---|
| `0x4002FF2D` | `R&D` | `0x4002FF5A` | `PRIMAPP` |
| `0x4002FF31` | `2ND` | `0x4002FF62` | `X-LOADER` |
| `0x4002FF35` | `CHMMCSD` | `0x4002FF6B` | `CHSETTINGS` |
| `0x4002FF3D` | `CH` | `0x4002FF76` | `KEYS` |
| `0x4002FF40` | `CHFLASH` | `0x4002FF7B` | `ISSW` |
| `0x4002FF48` | `CHRAM` | `0x4002FF4E` | `HLO` |
| `0x4002FF52` | `MLO` | `0x4002FF56` | `ULO` |

Pointer-table order (`0x4002FA84`): ISSW, X-LOADER, 2ND, ULO, MLO, HLO, KEYS,
PRIMAPP, R&D, CH, CHSETTINGS, CHRAM, CHFLASH, CHMMCSD. Evidence: 20 §4.1/§4.2.

**Media probe `0x40028D4E`**: enables PRCM module 5, calls `0x4002B6C0(0)` and
`0x4002B6DC(0,&sz)`, classifies `sz` (`>0x3D6→7`, `≥0x2B4→5`, `>0x226→4`,
`≥0x1B8→3`, else 1). Peripheral identities and thresholds **undecoded** (§8).
Evidence: 10 §6, 20 §1.1.

**Image handler `0x4002AFFE`** loads to IRAM `0x40300000` (or `0x40304000` when
device-type==3), via the 5-word vector table at `0x4002F5E4 + (cmd-0x40)*20`;
`'A'`-`'D'` → `{0x4002BD5D, 0x4002BF21, 0x4002BEC3, 0x4002BF5F, 0x4002BFC9}`;
`'E'`/`'F'` → `{0x4002C03F, 0x4002C1D3, 0x4002C1A3, 0x4002BFD5, 0x4002C205}`
(`'G'`=0x47 is rejected by `0x40028E4A`). Evidence: 20 §3.2.

### 4.4 Monitor (SMC) and CP15 security

**SMC inventory — 8 sites in this window, all A32.** Sweep:
`(word & 0x0FFFFFF0)==0x01600070` over every 4-byte-aligned offset; Thumb sweep
`(h & 0xFF00)==0xDF00` over every byte offset returns only misaligned
false positives ⇒ **there are no Thumb-mode SMC instructions** in either file.

| # | Address | Enc | r12 (service) | Context |
|---|---|---|---|---|
| 1 | `0x40028134` | `smc #0` | `0x107` | CPU1-wake path A: clear WKG_CONTROL (`r0=1,r1=0x200`) |
| 2 | `0x4002813C` | `smc #0` | `0x103` | same path: read AUX_CORE_BOOT_0/1 into r0/r1 |
| 3 | `0x400289C8` | `smc #0` | `0x107` | CPU1 secondary path |
| 4 | `0x4002CEB8` | `smc #0` | `0x0F0` | ARM veneer `0x4002CEB0` (in Thumb region); terminal image-load handoff |
| 5 | `0x4002F4C8` | `smc #1` | `0x000` | cache wrapper A `0x4002F4AC` (live) |
| 6 | `0x4002F4E8` | `smc #1` | `0x0FE` | wrapper A alt entry `0x4002F4D0` (dormant, no reference) |
| 7 | `0x4002F510` | `smc #1` | `0x000` | cache wrapper B `0x4002F4F4` (live) |
| 8 | `0x4002F530` | `smc #1` | `0x0F1` | wrapper B alt entry `0x4002F518` (dormant, no reference) |

`0x103` (read AUX_CORE_BOOT_0/1) and `0x107` (clear WKG_CONTROL bits) match the
TRM `0x100`-`0x113` HLOS table quoted in `BOOTROM-RE.md` §27.5, and the TRM's
calling convention (function ID in `R12`, `SMC #1`, save `R1-R12`) matches the
wrapper prologue. `0x0F0`, `0x0FE`, `0x0F1` are **outside** that table.
Evidence: 30 §1, 62 §2.

**`0xF0` — terminal secure image-load entry (SMC #0).** The ARM veneer
`0x4002CEB0` is a deliberate 12-byte A32 island interleaved into Thumb:
`blx 0x4002CE90` (`0x4002CEB0`), `mov r12,#0xF0` (`0x4002CEB4`),
`smc #0` (`0x4002CEB8`); Thumb resumes at `0x4002CEBC`. It has **no `bx lr`**.
Helper `0x4002CE90` resolves selector 1 to the single IRAM descriptor
`0x4030DC1C` and tail-calls dispatcher `0x4002CE36`, which decodes
`descriptor[0]`: type 1 → image operation `0x45` (`'E'`, the image-handler
family at `0x4002C03F`); type 2 → media command 6; type 3 → media command 4.
`0x4002CD10` hands out that descriptor. The veneer is never a branch target; it
is built into a two-word callback record `{fn=0x0002CEB0, arg=0x4030DC1C}` at
`0x4002AC18`-`0x4002AC2C` (literal `0x4002AE50`) and `0x4002C610`-`0x4002C614`
(literal `0x4002C6BC`), beside the IRAM image range literals
`0x40304000`/`0x4030BFFF` and the `0x000280A8` dead-loop stub. The record is
passed to monitor service 0 (`r0=3, r1=0, r2=caller context, r3=record`) through
`0x4002C584` → `0x40028F9C` → wrapper B. The record's second word is the static
descriptor `0x4030DC1C`; `r2` is a separate caller request object. The public
ROM zeroes `descriptor[0]/[8]/[0xC]/[0x10]` before passing the record, so the
dispatcher's type field is not set on this path; the monitor or a later stage
must fill it. This supports reading service 0 as a registration/hand-off rather
than an immediate call. Classification: **secure image-load /
hand-off stage**, descriptor-driven, terminal; "verify/execute" is inferred,
not observed. Evidence: 62 §4.

**`0xFE` / `0xF1` — dormant alternate monitor selectors.** They are alternate
entries in the two cache wrappers, after `BPIALL` + `DSB/ISB/DMB`, with
`smc #1`. They **skip the wrapper's `push {r4-r12,lr}`** and fall into its
`pop`, so they are not standalone functions, and a whole-file scan finds no
literal, word, halfword, ARM branch or Thumb BL/BLX reference to
`0x4002F4D0`/`0x4002F518` in either dump. Every live caller uses `r12=0`.
They are most plausibly undocumented secure-side cache/branch-predictor or PPA
maintenance selectors (the `r6=0xff`/`smc #1` shape matches Linux
`omap_smc3`), but the secure-side effect is unobservable. Evidence: 62 §5.

**Lower-window SMC (separate evidence):** `0x400260D8` `smc #0` `r12=9` inside the
"secure handoff tail" `0x40026098`-`0x400260E8`.

**CP15 / security (all A32; no Thumb CP15):**

- **VBAR** write at `0x40028954` = `0x00028000` — the only `c12` use in this
  window.
- SCTLR: `0x4002818C` `orr #4096` (I, CPU1) and `0x40028998` `orr #0x1800`
  (Z+I, CPU0). TTBR0 `0x40300000` at `0x400281D0`, TTBR1 `0` at `0x400281D8`,
  DACR `0xFFFFFFFF` at `0x400281E0`, MMU enable `0x400281EC` (CPU1 path).
- **MVBAR (`c12,c0,1`), SCR (`c1,c1,0`), NSACR (`c1,c1,2`), SDER: absent** from
  the `0x40028000` window — exactly what an NS-readable ROM predicts. No `c13`
  access anywhere.
- **Lower window** (separate evidence) holds the secure side: NSACR=`0x20C00`
  at `0x400260A0`, MVBAR=`0x0001C1C0` at `0x400260BC`, VBAR=0 at `0x400260C4`,
  SCR=`0x30` at `0x400260CC`.
- Full cache/TLB routine map: `0x4002F310` (DCISW set/way), `0x4002F384`
  (ICIALLU), `0x4002F46C` (TLBIASID), `0x4002F4A0` (BPIALL), and wrappers
  `0x4002F4AC`/`0x4002F4F4` (BPIALL + DSB/ISB/DMB + `smc #1`). Evidence: 30 §2/§3.

**Device-type / feature reads of `0x4A0022C4`** (answered "yes" to a NOTES open
question — the ROM *does* gate on it): read at `0x400280C0` and `0x4002891C`
(mask `0x700`, bits[10:8]), and twice through `0x4A0022C4-0xA4 = 0x4A002220`
bits 12/13 at `0x400297C8`/`0x400297D8`. **No write** to that register exists in
the window. Evidence: 30 §3.3.

### 4.5 Interrupts and abort vectors

- **IRQ entry `0x40029420` (ARM)** saves context and `blx 0x400293C2` to the
  **Thumb GIC dispatcher**. The dispatcher (`0x400293C2`-`0x40029411`) acks via
  `0x4002E218` reading **GICC_IAR `0x4824010C`**, looks up a runtime table at
  `0x4030DB08` (literal at `0x40029410`; entries `{id,handler}` at
  `0x4030DB0C+(idx<<5)`, idx = MIDR&3 from `0x4002F53C`), calls the handler, then
  EOI via `0x4002E246` writing **GICC_EOIR `0x48240000+0x110`**. The table ships
  zeroed (inside the memset BSS), so all IRQ registration is runtime.
  Evidence: 40 §4.
- **Abort handlers (ARM):** `0x400289E4` = prefetch (IFAR→r0 via
  `mrc c6,c0,2`; IFSR→r1 via `mrc c5,c0,1`), then `ldr pc` to `0x40028088`;
  `0x400289F0` = data (DFAR via `mrc c6,c0,0`; DFSR via `mrc c5,c0,0`), then
  `ldr pc` to `0x4002808C`. They occupy vector entries 3 and 4. Both end in a
  `b .` spin in the `0x40028080`-`0x4002809C` hang cluster with the fault address
  in r0 and status in r1 — a record-and-halt debug path, not a recovery path.
  Evidence: 30 §4, 40 §4.

### 4.6 Data tables

| addr | contents | consumer | status |
|---|---|---|---|
| `0x4002FE20` | 7 MMC controller bases | `0x4002DC68`… (`r0`=index) | decoded |
| `0x4002FE3C` | 15×4 u16 clock-divider table | `0x4002E8CE` | decoded (index proven) |
| `0x4002FEB4` | 9×3-byte flash geometry (nibble-packed), `0x1B` bytes | `0x4002EF14` | decoded (meaning partly inferred) |
| `0x4002FECF` | 46 × `{device-ID, geometry-index}` pairs + `00` terminator at `0x4002FF2B` | `0x4002EF14` | decoded byte-exact |
| `0x4002F578` | media-op vectors (cmd 1..8, stride 12) | `0x4002AD5C` | decoded |
| `0x4002F5E4` | image-op vectors (`'A'`-`'F'`, stride 20) | `0x4002AFFE` | decoded |
| `0x4002F9C4` | media cmd → progress bit (`40 41 42 43 44 45 47 4B`) | `0x4002AD5C` | decoded |
| `0x4002F9EC` | media clock-enable bases (`0x4A318000`,`0x48032000`,`0x48086000`) | `0x4002B6C0`/`0x4002B6DC` | bases decoded, identity unknown |
| `0x4002FA84` | boot-image name pointers (14) | `0x4002C254` | decoded |
| `0x4002F7C4` | 16-byte-per-entry device-type table | `0x4002A560` | shape decoded, gating unresolved |
| `0x4002F9F8` | cmd-8 descriptor (`80800C00 …`) | `0x4002BC11` | not decoded |
| `0x40028400` | **ROM API table**: 48 × 4-byte Thumb function pointers, `0x40028400`-`0x400284BF` | external callers (fixed addresses) | **identified** (TRM Figure 27-6) |
| `0x4002FEE0`-`0x4002FF2A` | **not a separate table**; middle/tail of the `0x4002FECF` pair table, starting one byte into record 8 | `0x4002EF14` | **resolved** |

The `0x4002FEF0` hypothesis "per-name offset/length table" was **tested and
rejected** (`20` §4.3). The block is now fully explained: the real base is
`0x4002FECF`; starting at `0x4002FEE0` is one byte into the record for device ID
`0x87` (`0x4002FEDF` = `87`, `0x4002FEE0` = `03`), so reading halfwords there
produces `0x9703`, `0xF004`, `0xC001`, … — a shifted view of the same pairs. The
only consumer loads `0x0002FECF` from the literal at `0x4002F000`. The ROM API table
at `0x40028400` is likewise identified by the TRM's ROM memory map; every entry
is an odd `0x0002xxxx` Thumb pointer and the table ends exactly at the code
boundary `0x400284C0`. Full byte-exact decode: `61-rom-api-and-flash-tables.md`.

### 4.7 ROM API table at `0x40028400`

The TRM's ROM memory map (Figure 27-6) places the "API table" at `0x28400`;
the dump has exactly **48 words** there, `0x40028400`-`0x400284BF`, all odd
`0x0002xxxx` values, i.e. valid Thumb entry points in the `0x00028000` boot
alias. The table ends exactly where Thumb code resumes at `0x400284C0`, so its
length is fixed by the code boundary. It is an **export table**: a whole-file
scan finds no word, halfword, literal or branch reference to `0x40028400` in
either dump, which is expected because external software calls fixed addresses
from the table. Entries 10/19/20-23/28/38/40/42 are already identifiable
(`PRM_RSTST` read, GIC dispatcher, watchdog kicks, boot-media query, image-load
helper, PRCM dispatcher/module enable); the rest carry descriptive labels only.
This table is **not** the SMC dispatch table — monitor services are a separate
`R12`+`SMC` interface (`62-monitor-services.md`). Full entry list:
`61-rom-api-and-flash-tables.md` §2.

---

## 5. Evidence table (finding → exact address)

Every row is an **LLVM-derived observation** (no REA Evidence; §2). "Addrs" are
the exact bytes/instructions the finding rests on.

| # | Finding | Address / literal |
|---|---|---|
| 1 | Reset branches to CPU0 entry | `0x40028000` (`b 0x40028910`) |
| 2 | First vector table redirects into IRAM | `0x40028004`-`0x4002801C`; targets `0x40028024`-`0x4002803C` = `0x4030D004`-`0x4030D01C` |
| 3 | Signature word | `0x40028020` = `0x1CF48309` |
| 4 | Hang stubs | `0x40028080`-`0x4002809C` (`b .`) |
| 5 | `do_warm_reset`, device-type gate bits[10:8]==3 | `0x400280C0`; literal `0x400280F4=0x4A0022C4` |
| 6 | CPU1 wake: SMC 0x107 then 0x103 | `0x40028134`, `0x4002813C` |
| 7 | CPU1 MMU bring-up (TTBR0/TTBR1/DACR/MMU) | `0x400281C0`-`0x400281EC` |
| 8 | Reset entry, VBAR=0x00028000, SP=0x4030D000 | `0x40028910`, `0x40028954`; literals `0x40028A00`, `0x40028A04` |
| 9 | I-cache enable | `0x40028998` (`orr #0x1800`) |
| 10 | CPU1 secondary: SMC 0x107 | `0x400289A0`, `0x400289C8` |
| 11 | Prefetch-abort capture → `0x40028088` | `0x400289E4`; literal `0x40028A10` |
| 12 | Data-abort capture → `0x4002808C` | `0x400289F0`; literal `0x40028A14` |
| 13 | ARM→Thumb handoff into Thumb main | `0x400288D0`; literal `0x400288FC=0x135` → `0x40028A19` |
| 14 | Copy-table walker | `0x4002882C`, `0x40028834`; literals `0x40028868=0x7718`, `0x4002886C=0x7758` |
| 15 | `memcpy` / `memset` | `0x40028870` / `0x40028898` |
| 16 | Bootstrap copy table (4 records) | `0x4002FF80`-`0x4002FFBF` |
| 17 | Second vector table + targets | `0x4002FFC0`; targets `0x4002FFE0`-`0x4002FFFC` |
| 18 | IRQ entry → GIC dispatcher | `0x40029420` → `0x400293C2`; IAR `0x4824010C`, EOIR `0x48240110` |
| 19 | Thumb main | `0x40028A18`-`0x40028A7A` |
| 20 | Reset-reason read / descriptor / progress bitmap | `0x4002A4C0`, `0x4002A4D8`, `0x4002A51C`; literals `0x4002A558`, `0x4002A55C` |
| 21 | Watchdog block ops | `0x4002A41C/430/454/478`; literal `0x4002A4B8=0x4A314000`, `0x4002A4BC=0xC49BA5E2` |
| 22 | PRCM clock-enable dispatcher (`tbb`) | `0x40029AE6` |
| 23 | Config parser + result struct | `0x40028A7C`; literal `0x40028BF8=0x4030D1A0`, `0x40028BFC=0xF0030003` |
| 24 | Command-table builder | `0x40028E72`; literal `0x4002A57C=0x0002F7C4` |
| 25 | Boot-config block reader (magic/type) | `0x4002B64E`; literals `0x4002B6B4=0x4A326A00`, `0x4002B6B8=0x4A327000`, `0x4002B6BC=0x00CF00AA` |
| 26 | Media handler / image handler | `0x4002AD5C` / `0x4002AFFE`; vectors `0x4002F578`, `0x4002F5E4` |
| 27 | Boot-image name pointer table | `0x4002FA84`-`0x4002FABC`; strings `0x4002FF2D`-`0x4002FF7E` |
| 28 | Media probe + thresholds | `0x40028D4E` |
| 29 | MMC controller base table | `0x4002FE20` |
| 30 | MMCHS command descriptor layout | `0x4002DDD4`; `ubfx` at `0x4002DE34` |
| 31 | Card init (CMD0/8/55/41/1/2/3) | `0x4002E930` |
| 32 | Card init part 2 (CMD9/4/7/16) | `0x4002EA5E`, `0x4002E904`, `0x4002E8CE`, `0x4002E844` |
| 33 | Clock-divider table | `0x4002FE3C`-`0x4002FEB3` |
| 34 | Device-type → controller map | `0x4002EC38` |
| 35 | dev open/read/ioctl | `0x4002BA54` / `0x4002BB1A` / `0x4002BBD0` |
| 36 | Flash read-ID, geometry/pair tables | `0x4002E25C`, `0x4002EF14`; geometry `0x4002FEB4`-`0x4002FECE`, pairs `0x4002FECF`-`0x4002FF2B` |
| 37 | SMC stub (service 0xF0) | `0x4002CEB0`-`0x4002CEBB`; helper `0x4002CE90`; dispatcher `0x4002CE36`; descriptor `0x4030DC1C` |
| 38 | SMC cache wrappers (0/0xFE/0x0F1) | `0x4002F4AC`/`0x4002F4F4`; sites `0x4002F4C8/4E8/510/530`; live calls `0x40028F0C/1E` |
| 39 | CP15: only VBAR=0x00028000 in window | `0x40028954` |
| 40 | Lower-window secure CP15 (separate file) | `0x400260A0` NSACR, `0x400260BC` MVBAR, `0x400260C4` VBAR, `0x400260CC` SCR, `0x400260D8` smc `r12=9` |
| 41 | Device-type/feature reads | `0x400280C0`, `0x4002891C`, `0x400297C8`, `0x400297D8` |
| 42 | USB descriptor template | `0x40030008` (len `0x98`) → `0x4030D108` |
| 43 | Zero BSS source (memset operand region) | `0x400300A0`-`0x40030B5C` (all zero) |
| 44 | Tail word | `0x40033FFC` = `0x00000003` |
| 45 | ROM API table (48 Thumb pointers) | `0x40028400`-`0x400284BF`; TRM Figure 27-6 |
| 46 | `0x4002FEE0` is mis-aligned pair-table tail | record 8 `0x4002FEDF` = `87 03`; literal `0x4002F000` = `0x0002FECF` |
| 47 | `0xF0` callback record / image range | `0x4002AC18`-`0x4002AC2C`, `0x4002C610`-`0x4002C614`; literals `0x4002AE50`, `0x4002C6BC`, `0x40304000`, `0x4030BFFF` |
| 48 | Dormant alt wrapper entries | `0x4002F4D0`, `0x4002F518`; no xref in either dump |

---

## 6. Observations vs inferences vs unknowns

### 6.1 Direct observations (reproducible from the bytes)

- Reset vector `b 0x40028910`; the two vector tables and their double-indirection
  through IRAM (`0x40028000`-`0x4002803F`, `0x4002FFC0`-`0x4002FFFC`,
  `0x4030D000`-`0x4030D03F`).
- The 4-record copy table `0x4002FF80` and its walker `0x4002882C`; `memcpy`
  `0x40028870`, `memset` `0x40028898`.
- The ARM→Thumb handoff at `0x400288D0` into `0x40028A18`.
- Thumb main flow and its callee addresses (`0x40028A18`-`0x40028A7A`).
- The config parser's command encoding (`1..8` → `0x4002AD5C`; `0x41..0x47` →
  `0x4002AFFE`), its mutable 8-byte table, and result codes.
- The config-block header format `0x00CF00AA | type` and list window
  `0x4A326A00`-`0x4A327000`.
- The MMC base table, MMCHS register offsets, command-descriptor index/response
  fields, card-init command sequence, and clock-divider indexing.
- The flash read-ID command and geometry tables.
- The 8 A32 SMC sites and their `r12` immediates; zero Thumb SMC.
- The CP15 inventory (VBAR `0x00028000`; SCTLR/TTBR/DACR values; absence of
  MVBAR/SCR/NSACR in this window).
- Abort handlers reading IFAR/IFSR/DFAR/DFSR and hanging.
- The IRQ → GIC dispatcher path and `0x4824010C`/`0x48240110`.
- The USB descriptor template bytes and the all-zero `0xABC` blob.
- The byte-exact string block and 14-pointer name table.
- The 48-word ROM API table at `0x40028400`, all valid Thumb pointers.
- The 9-record flash geometry table at `0x4002FEB4` and the 46-record
  `{device-ID, geometry-index}` table at `0x4002FECF`, including the
  `0x4002F000` literal and the `base-0x1B` indexing.
- The `0xF0` veneer/helper/descriptor chain and its installation as a
  `{fn,arg}` callback; the `0xFE`/`0xF1` alternate entries have no reference.

### 6.2 Inferences (flagged; not decoded)

- **WDT identity of `0x4A314000`** — from WSPR magic pairs `+0x48` and
  write-posting `+0x34`; the OMAP WDT TRM was not consulted.
- **`smc #1` selectors `0x00`/`0x0FE`/`0x0F1`**: `r12` is the monitor
  function ID (TRM §27.5), not a BPIALL operand. The live selector is `0`; the
  `0xFE`/`0xF1` alternate entries are dormant and unreferenced, and most
  plausibly belong to a secure cache/branch-predictor or PPA maintenance group
  (the `r6=0xff`/`smc #1` shape matches Linux `omap_smc3`).
- **`smc #0` `0xF0` veneer**: a terminal, descriptor-driven secure image-load /
  hand-off entry. It dispatches type 1 to image op `0x45` (`'E'`), types 2/3 to
  media commands 6/4, and is installed beside the IRAM image range
  `0x40304000`/`0x4030BFFF` and the `0x000280A8` hang. "Verify/execute" is
  inferred, not observed.
- **Media-class thresholds** (`0x40028D4E`: `0x1B8/0x226/0x2B4/0x3D6`) classify
  concrete media/device variants (class 1/3/4/5/7) — meaning not established.
- **USB `idVendor`/`idProduct` are OTP-patched** at run time (they are zero in
  ROM); patch site not found.
- **`0x08000000`/`0x50000000`** are the flash memory and GPMC-class register
  windows (offsets `0x7C/0x80/0x84` not matched to a TRM).
- **cmd-8 base `0xC0000000`** is a distinct memory-mapped boot source.
- **`0x40026F28`-`0x40027FFF`** (the ~4 KB hole) is the SMC/monitor dispatch
  region — inferred from the documented boundary plus the *absence* of any
  dispatch code in the two readable blocks.
- **Flash geometry fields** (`dev[0xC]/[0x10]/[0x12]/[0x14]/[0x15]/[0x16]/
  [0x18]/[0x19]`) are byte-exact, but their hardware names (page size, block
  size, bus width, technology) are inferred from the values and the consumer;
  the TRM NAND terminology was not matched one-to-one.
- Command-word flag bits beyond index and response-length, and the clock-divider
  column semantics, are **inferred only**.

### 6.3 Unknowns

- Secure-side semantics of monitor IDs `0xF0`/`0xFE`/`0xF1` (the dispatcher is
  outside both readable dumps), and the exact meaning of descriptor fields
  `+4/+8/+0xC/+0x10/+0x14/+0x18` in the `0xF0` request.
- Exact purpose of the 48 ROM API entries that carry only descriptive labels.
- `0x4002F9F8` cmd-8 descriptor contents.
- `0x4002F7C4` device-type table's flag gating (`0x40028E72` else-branch).
- Meaning of result codes `0xF0030002/3/6`.
- `0x4002BF20` semantics (arg to `0x4002A478`; also the address of function
  `0x4002BF21`).
- Arg ABI of `smc #1` (callers fill only 2-3 of r0-r3).
- The ARM `blx` H-bit convention: five ROM ARM→Thumb calls use bit24=0, which
  disagrees with the architecture convention and LLVM's `+2` target adjustment
  for bit24=1. It changes no readable target here but should be confirmed.
- Signature word `0x1CF48309`; tail word `0x40033FFC=3`.
- Purpose of the Thumb island `0x4002F2F0`-`0x4002F310` inside the ARM library.
- Exactly which GP/HS or feature bits `0x4A0022C4` / `0x4A002220` select.
- `0x40028C00` (ARM device-type gate body) was not fully reversed.

---

## 7. Conflicts and corrections

Resolves the disagreements listed in `HANDOFF.md` "Known conflicts / caveats".

**C1 — Mode map and boundaries (`METHOD.md`/`NOTES.md` vs `50-code-map.md`).**
Resolution: **prefer `50-code-map.md`** (it disassembled each candidate range in
*both* modes and kept the coherent one). Corrections:

| Claim (METHOD/NOTES) | Resolution (50-code-map) | Address evidence |
|---|---|---|
| "ARM stub near `0x4002B0xx`" | **No ARM code exists in `0x4002B0xx`** (Thumb in both decodes); the ARM stub is `0x4002B740`-`0x4002B798`, a register/pad loop over `0x4A100000` (literal `0x4002B798`), reached only from `0x40028C08` | `0x4002B740`, `0x4002B798` |
| ARM string/mem library | Moved to `0x4002F0BC`-`0x4002F2F0` (`memset 0x4002F0BC`, unaligned `memcpy 0x4002F0CC`, word `memcpy 0x4002F200`) | `0x4002F0BC`-`0x4002F2F0` |
| Cache/TLB library `0x4002F2B0`-`0x4002F550` | Cache/TLB lib is `0x4002F310`-`0x4002F56C`; `0x4002F2B0`/`0x4002F2D0` are ARM unaligned read/write helpers, and the Thumb island `0x4002F2F0`-`0x4002F310` splits the ARM run | `0x4002F2B0`, `0x4002F2F0`, `0x4002F310` |
| `0x400280C0` start of a Thumb run | **ARM** | `0x400280C0` |
| No ARM island in the big Thumb body | ARM island `0x40028C00`-`0x40028C44` (device-type gate) inside the Thumb run, reached by `bleq 0x40028C00` from `0x40028948` | `0x40028C00` |

*Where the evidence is thin:* function extents in the large Thumb bodies
(`0x40028C44`-`0x4002B736`, `0x4002BC10`-`0x4002F0BC`) are heuristic (split on the
next branch target / `bx lr` / `pop pc`); ~200 of 282 starts are unclassified.
`0x40028400` is now identified as the ROM API table (C12), not merely a guessed
pointer table. Two boundary corrections to `50-code-map.md` are in C11.

**C2 — Copy-table record 4 and the `0xABC` blob.** `NOTES.md` described record 4
as "memset of a 0xABC blob". Correction (`40 §3c`): the source region
`0x400300A0`-`0x40030B5C` is **2748/2748 zero bytes** and `memset` (`0x40028898`)
never reads arg0, so **only the size `0xABC` matters**; the "blob" is the size of
the zeroed BSS at `0x4030D1A0`. Also, `NOTES.md` called record 2 (`0x40030008`,
len `0x98`) "config records"; the natural parse (`40 §3b`) is a **USB descriptor
template** (6-byte prefix + Device descriptor + 4 Configuration blocks).

**C3 — String offsets off by one.** `NOTES.md` §"Strings" lists offsets that are
one byte low for several names; the byte-exact table is `20 §4.1` (e.g. `R&D` at
`0x4002FF2D`, `ISSW` at `0x4002FF7B`).

**C4 — SMC site count.** Prior work (`BOOTROM-RE.md`, `README.md`) reports **4**
SMC sites (all `smc #0`). The exhaustive aligned sweep (`30 §1`) finds **8** in
this window — the 4 previously known plus 4 `smc #1` sites inside the cache
wrappers at `0x4002F4C8/4E8/510/530` — plus one in the lower dump
(`0x400260D8`, `r12=9`). The prior "no Thumb SMC" claim is **confirmed**.

**C5 — Boot-chain manifest / tables in prior work.** `BOOTROM-RE.md` describes
`0x4002ffc0+` as `{size, addr, …}` triplets and `0x40030000` as "8× `ldr pc` +
target words". Correction: `0x4002FFC0` is the **second vector table** (8×
`ldr pc,[pc,#0x18]`, targets at `0x4002FFE0`), and `0x40030000` is **8 zero
bytes**; the copy table `{src,dst,len,fn}` is at `0x4002FF80`. (40 §3a/§4.)

**C6 — Clock-divider table.** `NOTES.md` calls `0x4002FE3C` "16-bit pairs … used
by `0x4002E8CE`", unverified. `10 §3.3` decodes it as **15 rows × 4 u16**
consumed as `table[M*8+N*2-8]<<1` with `N=bits[2:0]`, `M=bits[6:3]` of
`resp[0xc]`. The indexing is proven; the physical column meaning is not.

**C7 — `0x4002FEF0` "per-name offset/length table".** The hypothesis is
**rejected** (`20 §4.3`). The definitive explanation is C12: the block is the
mis-aligned tail of the `0x4002FECF` pair table, and the only consumer loads
`0x0002FECF`.

**C8 — Minor numeric discrepancy.** `NOTES.md`/`HANDOFF.md` write the
`do_warm_reset` device-type test as bits[11:8]. `30 §3.3` shows the mask is
**`0x700` = bits[10:8]** at `0x400280C0`/`0x4002810x`. Use bits[10:8].

**C9 — The 4 KB hole.** The SMC-dispatch-region claim is an **inference**, not
an observation: no bytes of `0x40026F28`-`0x40027FFF` are available in either
dump. Stated as such in `30 §5`.

**C10 — `NOTES.md` supersession note.** `NOTES.md` itself warns that some of its
mode-map lines are superseded by `50-code-map.md`; that is confirmed here
throughout C1.

**C11 — Two `50-code-map.md` boundary corrections.** Independent dual-mode
disassembly (2026-10-19) found:

| Boundary | Earlier map | Resolved |
|---|---|---|
| `0x400288F8` | Thumb veneer through `0x40028904` | only `0x400288F8`-`0x400288FB` is a 4-byte Thumb `bl 0x4002F2F0`; `0x400288FC`-`0x40028903` are the literals `0x00000135` and `0x4030D000`; ARM resumes at `0x40028904` |
| `0x4002B798` | Thumb resumes at `0x4002B798` | `0x4002B798` is the literal `0x4A100000`; Thumb resumes at `0x4002B79C` (`push {r4,lr}`) |

Full resolved map and the dual-mode evidence: `60-mode-map-resolution.md`.

**C12 — The two "unidentified" data blocks are resolved.** `0x40028400` is the
**ROM API table** (TRM Figure 27-6), 48 words ending exactly at `0x400284C0`;
all are odd `0x0002xxxx` Thumb pointers and none is referenced internally.
`0x4002FEE0`-`0x4002FF2A` is **not a table**: it is the middle/tail of the
46-record `{device-ID, geometry-index}` table at `0x4002FECF`, viewed from one
byte into record 8 (`0x4002FEDF` = `87 03`), which is why halfword reads look
like `0x9703, 0xF004, 0xC001, …`. The geometry records at `0x4002FEB4` and the
`base-0x1B` indexing are byte-exact. Decode:
`61-rom-api-and-flash-tables.md`.

**C13 — `r12` is the monitor function ID, not a BPIALL operand.** TRM §27.5
explicitly puts the function ID in `R12` before `SMC`. In the cache wrappers,
`r12=0` is therefore the live service selector, and the alternate `r12=0xFE` /
`0xF1` entries are dormant monitor IDs. The `mcr p15,...,c7,c5,6` is `BPIALL`,
whose data register is architecturally ignored. The `0xFE`/`0xF1` entries also
skip the wrapper's `push`, so they are not standalone functions. Analysis:
`62-monitor-services.md`.

---

## 8. Remaining open questions and concrete next steps

Open questions (ordered by value):

1. **Secure-side semantics.** The `0xF0`/`0xFE`/`0xF1` *structure* is now
   resolved (`62-monitor-services.md`), but the dispatcher/secure code is
   outside both readable dumps. The `0xF0` descriptor fields and whether the
   service verifies, decrypts or executes the image remain unobserved. The
   `0xFE`/`0xF1` secure effects remain unknown; only their dormant caller-side
   entries are established.
2. **ROM API entry semantics.** The 48-entry table at `0x40028400` is
   identified, but many targets still carry only descriptive labels; mapping
   each API entry to its documented function would require an external API list
   (the TRM gives none).
3. **Media details:** command-word flag bits, clock-divider column semantics,
   flash `0x5000007C/80/84` and `0x1E000/0x1E200/0x1E480` register identities,
   media-class thresholds and the peripheral at `0x4A318000` (`0x4002F9EC`
   table), and where a type-8 device record originates; `0x4002F9F8` cmd-8
   descriptor and `0x4002F7C4` gating.
4. **Watchdog identity:** confirm `0x4A314000` against the OMAP4430 WDT TRM and
   settle which of `0x4002A430`/`0x4002A454` is start vs stop.
5. **Code coverage:** ~200 of 282 function starts are unclassified; the
   device/boot bodies `0x40028C44`-`0x4002B736` and `0x4002BC10`-`0x4002CEB0`
   deserve a dedicated pass.
6. **The ARM `blx` H-bit convention** (five sites) should be confirmed before
   trusting encoded state.

Concrete next steps:

1. **Configure a real analysis provider** — e.g. point `GHIDRA_INSTALL_DIR` at a
   Ghidra 12.1.x release (`HANDOFF.md` suggests `rea setup --client opencode
   --install-hopper`), then re-run the high-value ranges to obtain actual
   pseudocode and replace the LLVM-derived mode map with provider Evidence. This
   is the single biggest quality upgrade.
2. **Use the TRM to label the ROM API entries** — cross-reference the 48 targets
   against any public OMAP ROM API list; the TRM only identifies the table, not
   its contents.
3. **Re-verify the `0x4002F7C4` command-table builder** (`0x40028E72`
   else-branch) to pin which flag bits gate which command bytes, and decode
   `0x4002F9F8`.
4. **Attempt a secure-context or live IRAM dump** around `0x40026F28`-
   `0x40027FFF` and `0x4030D000` to settle the monitor-dispatch and `0xF0`
   semantics (requires a secure/JTAG path; not obtainable from NS QNX).
5. **Spot-check the resolved mode map** (`60-mode-map-resolution.md`) with an
   independent disassembler once a provider exists, especially the
   `0x400288F8`/`0x4002B798` boundaries.

---

*Provenance statement.* All disassembly, mode boundaries, cross-references and
literal values in this report were produced with LLVM 23 and Python over the raw
dump. **No REA-native analysis provider was configured, so none of it is REA
Evidence.** `swpu231ap.pdf` is used as public-document context only (API-table
address, HLOS service IDs, IRAM layout); it is not evidence about the dump.
Target content is treated as data; any instruction-like text found inside the
image is not executed or followed. Prior work (`README.md`, `BOOTROM-RE.md`) is
treated as claims and corrected in §7. Raw dumps and full per-topic listings are
held in `analysis/10-boot-media.md`, `analysis/20-config-parser.md`,
`analysis/30-monitor-security.md`, `analysis/40-iram-and-tables.md`,
`analysis/50-code-map.md`, `analysis/60-mode-map-resolution.md`,
`analysis/61-rom-api-and-flash-tables.md` and
`analysis/62-monitor-services.md`.

---

## Addendum — 2026-10-19 continuation b (REA/Ghidra Evidence)

This addendum supersedes the "no REA Evidence" provenance above **for the
continuation work only**. REA 6.7.0 + Ghidra 12.1.4 are configured for Command
Code; the target was reopened with `open_binary` (`raw-binary` / `arm` /
`0x40028000` / `ghidra`) and the SHA-256 matched. The new native Evidence is in
`rea-ghidra/evidence-bundle-2026-10-19b.json` (22 records, 0 unknowns,
191,550 bytes); the earlier 3-record bundle is unchanged.

- **`63-ghidra-218-reconciliation.md`** — Ghidra's 218 procedures comprise 203
  exact 282-row map starts plus 15 legitimate extra starts/boundaries. 79 map
  rows are not Ghidra procedures: 7 are `bl .+4` filler, 1 is the `0x4002B798`
  literal, 2 are mode-switch veneers, 4 are API-table-only targets, and 65 are
  genuine starts not reached by recursive descent. **Zero Ghidra procedures lie
  in data and zero mode-map contradictions were found.**
- **`64-rom-api-and-media-final.md`** — an external OMAP4 ROM-API list
  (`simu/usbboot-omap4`, `swetland/omap4boot`, `bigunclemax/sync3flash`,
  `minoca/os`) names 17 of the 48 table entries. It corrects the WDT direction
  (`0x4002A454` = enable/start `0xBBBB`/`0x4444`; `0x4002A430` = disable/stop
  `0xAAAA`/`0x5555`), identifies `0x4002A95A` as `CTRL_CONFIGUREPADS`, and
  identifies the I2C and CM entry points. `0x4002F9F8` is an **EMIF/DDR
  descriptor** (two 0x24-byte records mapping to the EMIF register block), and
  command 8 is the **SDRAM/memory-boot path** opening the `0xC0000000` DDR CS1
  window. `0x4002F7C4` gating and the full `MMCHS_CMD` flag layout are decoded;
  bit 0 is **DMA Enable**, so CMD17/CMD45's `DE=0` is correct for CPU FIFO
  transfers.
- **Secure/JTAG item remains blocked.** No JTAG/debug probe, `/dev/ttyUSB*`/
  `ACM*`, OpenOCD/J-Link/DS-5/gdb-multiarch, or ADB device is present on this
  host, so `0x40026F28`-`0x40027FFF` and live IRAM `0x4030D000` could not be
  dumped. The `0xF0`/`0xFE`/`0xF1` secure-side semantics remain as classified
  in `62-monitor-services.md`.
