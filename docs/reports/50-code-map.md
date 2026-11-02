# 50 — Whole-ROM function / mode map and call graph (OMAP4430 boot ROM)

> Update 2026-10-19: two boundary corrections are resolved in
> `60-mode-map-resolution.md`: `0x400288F8`-`0x400288FB` is a 4-byte Thumb
> `bl 0x4002F2F0` followed by literals at `0x400288FC`-`0x40028903` (ARM resumes
> at `0x40028904`), and `0x4002B798` is the literal `0x4A100000` with Thumb
> resuming at `0x4002B79C`. The rest of this file's mode map is confirmed.

Target dump: `/home/psyden/rea-work/bootdumps-2026-09-11/omap4430-bootrom-0x40028000.bin`
(49152 bytes, load base `0x40028000`, SHA-256
`fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`).

Tooling: LLVM 23 (`llvm-mc` / `ld.lld` / `llvm-objdump`) plus Python, per `METHOD.md`.
REA has **no native analysis provider** on this host, so **every observation below is
LLVM-derived, not REA Evidence**. A single raw-binary wrapper ELF
(`/tmp/opencode/rea-omap/wrap.elf`, text at `0x40028000`) is disassembled with
`--triple=armv7-...` or `--triple=thumbv7-...` to force a mode per range.

Method for the mode map: candidate ranges were disassembled **in both modes** and the
mode that produced coherent code was kept; every claimed mode switch below has a
`blx`/`bx`/`.arm`-style boundary that was disassembled. Function starts were derived
from branch targets, Thumb literal-pool function pointers (odd = Thumb), and mode-run
starts; sizes are the extent up to the next start/`bx lr`/`pop pc`. Data regions are
**not** disassembled as code.

---

## 1. Mode map

Code ends at ~`0x4002F578`; `0x4002F578`–`0x40034000` is data (literal pools, function
pointer tables, peripheral tables, strings, and the second vector table at
`0x4002FFC0`). ARM/Thumb transitions happen only through `blx`, `bx pc`/`bx rN`, or
1–2 instruction veneers.

| Start | End | Mode | Confirmed content / boundary evidence |
|---|---|---|---|
| `0x40028000` | `0x400280C0` | ARM | Vector area. `0x40028000 b 0x40028910`; `0x40028004`–`0x4002801C` = 7×`ldr pc,[pc,#0x18]` reading `0x40028024`–`0x4002803C` (`0x4030D004`..`0x4030D01C`); `0x40028020` = signature `0x1CF48309`; `0x40028040`–`0x4002807F` zero; `0x40028080`–`0x400280BC` hang stubs (`b .`). |
| `0x400280C0` | `0x40028400` | ARM | Real code from `0x400280C0`; `0x4002821C`–`0x400283FF` zero-filled. |
| `0x40028400` | `0x400284C0` | data | 48 × 4-byte entries (function-pointer-like); not code. |
| `0x400284C0` | `0x40028740` | Thumb | Helpers for the `0x50000000` block (`+0x10/+0x14/+0x18/+0x40/+0x60/+0x240`–`0x24C`). |
| `0x40028740` | `0x400287B4` | ARM | Two leaf funcs (`0x40028740`, `0x4002877C`) writing `0x50000000+0x40/+0x60/+0x78`; literal `0x400287B4`. |
| `0x400287B4` | `0x4002882C` | data | Literal pools for the ARM funcs. |
| `0x4002882C` | `0x400288F8` | ARM | Startup trampoline `0x4002882C`/`0x40028834`/`0x400288D0`, ARM `memcpy` `0x40028870`, ARM `memset` `0x40028898`; literal pool `0x40028868` = `0x00007718`, `0x4002886C` = `0x00007758`. |
| `0x400288F8` | `0x40028904` | Thumb | Veneer `bl 0x4002F2F0`. |
| `0x40028904` | `0x40028A18` | ARM | `0x40028904` veneer (`blx 0x400288CC`, `blx 0x4002F302`); reset entry `0x40028910`; icache enable `0x40028990`; CPU1 secondary `0x400289A0`; abort handlers `0x400289E4`/`0x400289F0`. |
| `0x40028A18` | `0x40028C00` | Thumb | Thumb main `0x40028A18`; config parser `0x40028A7C`. |
| `0x40028C00` | `0x40028C44` | ARM | **ARM island**: `0x40028C00 mov r7,lr; bl 0x40028740; bl 0x4002B740; …`; reached by `bleq 0x40028C00` from `0x40028948`. |
| `0x40028C44` | `0x4002B736` | Thumb | Large Thumb body (boot/display/MMC/config logic). |
| `0x4002B736` | `0x4002B740` | data | Padding/literals. |
| `0x4002B740` | `0x4002B798` | ARM | Register/pad loop over base `0x4A100000`; literal `0x4002B798` = `0x4A100000`. |
| `0x4002B798` | `0x4002BC00` | Thumb | Small wrappers that call the `0x4002E1xx`/`0x4002E0xx` device helpers. |
| `0x4002BC00` | `0x4002BC10` | data | `"MLO        "` + `0x0002EB93` (the MLO record). |
| `0x4002BC10` | `0x4002CEB0` | Thumb | Boot-media/descriptor code incl. dispatcher `0x4002BC7C`. |
| `0x4002CEB0` | `0x4002CEBC` | ARM | SMC stub: `blx 0x4002CE90; mov r12,#0xF0; smc #0`. |
| `0x4002CEBC` | `0x4002F0BC` | Thumb | Continuation of the device/boot body. |
| `0x4002F0BC` | `0x4002F2F0` | ARM | String/memory library (see §2). |
| `0x4002F2F0` | `0x4002F310` | Thumb | Veneer `movs r4,r0; nop.w; movs r0,r4; blx 0x40028904; mov.w r0,#-1; bx lr`; `0x4002F302 b .` hang; `bx lr` padding. |
| `0x4002F310` | `0x4002F56C` | ARM | Cache/TLB maintenance library + CPSR helpers. |
| `0x4002F56C` | `0x4002F570` | Thumb | `bx pc` veneer (2 bytes). |
| `0x4002F570` | `0x4002F578` | ARM | `b`+`bx lr` veneer. |
| `0x4002F578` | `0x40034000` | data | Fn-pointer table (`0x4002F578`), MMC bases (`0x4002FE20`), 16-bit tables (`0x4002FE3C`, `0x4002FEB4`, `0x4002FEE0`, `0x4002FEF0`), boot-image names (`0x4002FF2C`), IRAM copy table (`0x4002FF80`), second vector table (`0x4002FFC0`, targets `0x4002FFE0`). |

Notes that supersede `METHOD.md`'s bulk map:

* `0x400280C0` is **ARM**, not the start of a Thumb region (ARM decodes
  `ldr r1,[pc,#0x2c]; ldr r1,[r1]; and r1,r1,#0x700; lsr r1,r1,#8; cmp r1,#3` cleanly;
  Thumb does not).
* The tail is **not** a single Thumb run: `0x4002F0BC`–`0x4002F2F0` is ARM (memset/memcpy
  and unaligned load/store decode cleanly and are called as ARM via `blx`), and
  `0x4002F2F0`–`0x4002F310` is a Thumb veneer inside it.
* `0x4002CE90` is **Thumb** (clean `subs/cmp/blt/bx lr`); only `0x4002CEB0`–`0x4002CEBC`
  is ARM. The ARM island `0x40028C00`–`0x40028C44` sits inside the big Thumb run.
* The "ARM stub near `0x4002B0xx`" claimed in `METHOD.md` was **not found** in
  `0x4002B0xx`; the ARM stub there is `0x4002B740`–`0x4002B798`.

---

## 2. Function table

### 2a. Principal functions (purpose tied to a disassembled address)

| Address | Mode | Purpose (LLVM-derived) |
|---|---|---|
| `0x40028000` | ARM | Reset entry: `b 0x40028910`. |
| `0x40028024`–`0x4002803C` | data | IRAM pointers `0x4030D004`..`0x4030D01C` loaded by the 7 `ldr pc` slots. |
| `0x400280C0` | ARM | `do_warm_reset`: read device-type `[0x4A0022C4]` bits[11:8]; if `==3` clear `PRM_RSTST` `[0x4A307B04]` and set `PRM_RSTCTRL` `[0x4A307B00]` bit0; end `bx r0`. |
| `0x40028100` | ARM | Boot CPU select: `mrc MIDR`; `MIDR[1:0]!=0` → CPU1 path (`wfe`, poll `[0x48281400]` bit9, `movw r12,#0x107; smc`, `movw r12,#0x103; smc`, `mov r10,r1; b 0x400281C0`); else CPU0 path writing `[0x40028208]` and MMU/cache setup at `0x400281C0` (`mcr c2` TTBR/TTBCR, `mcr c3` DACR, `mcr c1` MMU enable, `bxne r0`/`bxne r10`). |
| `0x400284C0` | Thumb | Enable/poll a `0x50000000+0x10/+0x14` device (`orr #2`, poll bit31). |
| `0x400284D6` | Thumb | Set `0x50000000+idx*0x30+0x60` bits[13:12]. |
| `0x400284FC` | Thumb | Program `0x50000000+idx*0x30+0x60` selector (mask `[0x40028728]`). |
| `0x4002869C` | Thumb | Snapshot `0x50000000+0x240`..`+0x24C`/`+0x300` into a caller struct. |
| `0x400286D4` | Thumb | Write a `0x50000000` control word (set bit8/`bic #0xf`+increment). |
| `0x400286EE` | Thumb | Poll a `0x50000000+0x18` status bit (`1<<r0`). |
| `0x40028718` | Thumb | Set `0x50000000+0x18` bit `1<<r0`. |
| `0x40028740` | ARM | Clock/control: `str r0,[0x50000000+0x60]` mask `[0x400287B8]`, `[+0x78]=0xF48`. |
| `0x4002877C` | ARM | Set `[0x50000000+0x40]` bit0 and `[+0x60]` field (`0x400000`). |
| `0x4002882C` | ARM | Startup trampoline: `bl 0x40028834`; `bl 0x400288D0`. |
| `0x40028834` | ARM | Walk IRAM copy table `0x4002FF80` (via `[0x40028868]`=`0x7718`/`0x7758`; records `{src,dst,len,func}`); per record call `0x40028870`/`0x40028898`. |
| `0x40028870` | ARM | `memcpy(dst=r1, src=r0, len=r2)` (word loop + tail). |
| `0x40028898` | ARM | `memset(dst=r1, len=r2, val=0)`. |
| `0x400288D0` | ARM | `ldr sp,=0x4030D000`; `blx 0x400288C4`; load `0x135`, `add r12,r12,pc`, `bx r12` → Thumb `0x40028A18`. |
| `0x400288C4` | ARM | Thumb-side veneer target (`ldr pc` via `0x400288C8`), paired with `0x400288F0` (`add r12,pc,#1; bx r12`). |
| `0x400288F8` | Thumb | Veneer: `bl 0x4002F2F0`. |
| `0x40028904` | ARM | Veneer: `blx 0x400288CC; blx 0x4002F302`. |
| `0x40028910` | ARM | Reset entry (CPU0): `mrc MIDR`; `MIDR[1:0]!=0` → `0x400289A0`; else device-type check, `msr CPSR_c,#0xD3`, VBAR `[0x40028A00]`=`0x00028000`, SP `[0x40028A04]`=`0x4030D000`, `r0=[0x4A307B04]`, `blx 0x4002A4D8`, `blx 0x4002A51C(0)`, 8×`bl .+4` (`0x40028974`..`0x4002898C`), fall into `0x40028990`. |
| `0x40028990` | ARM | Enable I-cache (`mrc c1; orr #0x1800; mcr c1`) then `b 0x4002882C`. |
| `0x400289A0` | ARM | CPU1 secondary: poll `[0x48281400+0x400]` bit9, `movw r12,#0x107; smc`, read `+0x800` (nonzero) and `+0x804`, `bx r1`. |
| `0x400289E4` | ARM | Abort handler: `mrc c6,c0,2; mrc c5,c0,1; ldr pc,[0x40028A10]`. |
| `0x400289F0` | ARM | Abort handler: `mrc c6,c0,0; mrc c5,c0,0; ldr pc,[0x40028A14]`. |
| `0x40028A18` | Thumb | Thumb main entry: `bl 0x4002A4C0` (read `PRM_RSTST`); `bl 0x4002A51C(1)`; `bl 0x40029AE6(6,1)`; `bl 0x4002A41C/0x4002A430/0x4002A478/0x4002A454`; if status==1 set bit2; `bl 0x40028DB6`; `bl 0x40028A7C`. |
| `0x40028A7C` | Thumb | Config/string parser: byte values `1..8` → `0x4002AD5C`; `'A'..'G'` → handlers incl. `0x4002AFFE`; fills a struct at `0x40028BF8+0x64`. |
| `0x40028C00` | ARM | Device-type gate (ARM island): `bl 0x40028740; bl 0x4002B740`; if `r6==32` `bleq 0x4002877C`; compare `[0x8000000]` against `[0x40028C3C]`/`[0x40028C40]`; `bxeq r7` / `bx r0`. |
| `0x40028D4E` | Thumb | Part of the `0x40029AE6`/`0x40029CA0` helper cluster. |
| `0x40028DB6` | Thumb | Shared helper called from `0x40028A60`, `0x40028C44`, `0x40029CD0`, `0x40029F26`, `0x4002B64E`. |
| `0x40028F04` | Thumb | Cache-op wrapper: calls ARM `0x4002F4AC`/`0x4002F4F4`. |
| `0x40029AE6` | Thumb | `tbb [pc,r2]` dispatcher (`r2<10`): targets `0x40029A5E/0x40029A06/0x4002997E/0x4002992C/0x400298A2/0x4002987C/0x40029866/0x40029800`; 12 call sites incl. `0x40028A2E`. |
| `0x4002A2CA` | Thumb | Boot-media/descriptor query (called from `0x4002BC2A`, `0x4002C6F8`). |
| `0x4002A41C` | Thumb | `0x4A314000+0x10` device enable + poll `+0x14` bit31. |
| `0x4002A430` | Thumb | WDT-class kick: wait `[+0x34]==0`, write `0xAAAA`/`0x5555` to `+0x48`. |
| `0x4002A454` | Thumb | WDT-class kick: write `0xBBBB`/`0x4444` to `+0x48`. |
| `0x4002A478` | Thumb | `0x4A314000` config with callback argument (`0x40028A74`). |
| `0x4002A4C0` | Thumb | Read `PRM_RSTST` `[0x4A307B04]` → `*r0`; return 0/6. |
| `0x4002A4D8` | Thumb | Reset-reason descriptor init (`0x4030D040`/`0x4030D060`); stores reason from `r0`. |
| `0x4002A51C` | Thumb | Progress/trace bitmap setter: set bit `r0` (0..0x5F) in a 3-word bitmap at `[0x4030D060]`; returns 1 if out of range. **Most-called function (63 call sites).** |
| `0x4002B740` | ARM | Loop over 16-bit regs at `base+0x40..0x5E` (`bic #31` / `orr` with 0x18 then 0); base `0x4A100000`. |
| `0x4002BC10` | Thumb | Boot-media descriptor init via `0x4002A2CA` (literal `0x4002BC78`); on success sets `[r4+8]=1, [r4+5]=1, [r4+0xC]=1, [r4+0x10]=0xC0000000, [r4+0x14]=3, [r4+0x18]=0, [r4]=1`. |
| `0x4002BC58` | Thumb | `memset` helper: `blx 0x4002F0CC`. |
| `0x4002BC7C` | Thumb | **Dispatch/set-callback** (see §4). |
| `0x4002CE90` | Thumb | Small helper `subs/cmp/blt/bx lr`. |
| `0x4002CEB0` | ARM | SMC stub `blx 0x4002CE90; mov r12,#0xF0; smc #0`. |
| `0x4002F004` | Thumb | `memcmp`. |
| `0x4002F078` | Thumb | `strnlen`-class scan. |
| `0x4002F0BC` | ARM | `memset` (byte replicated to a word). |
| `0x4002F0CC` | ARM | `memcpy` (unaligned-safe). |
| `0x4002F200` | ARM | Word `memcpy` (`pld`, 8-reg `ldm`/`stm`). |
| `0x4002F2B0` | ARM | Read unaligned `u32` (`ldrb`×4 + `orr`). |
| `0x4002F2D0` | ARM | Write unaligned `u32`. |
| `0x4002F2F0` | Thumb | Veneer → ARM `0x40028904`; returns `-1`. |
| `0x4002F310` | ARM | Invalidate D-cache by set/way (`mcr c7,c6,2` DCISW; `mrc c0,c0,1` CCSIDR). |
| `0x4002F384` | ARM | `ICIALLU` (`mcr c7,c5,0`). |
| `0x4002F4AC` / `0x4002F4F4` | ARM | `BPIALL` + barriers + `smc #1` wrappers (called from Thumb `0x40028F04`). |
| `0x4002F53C` | ARM | Leaf helper (called from Thumb `0x400293xx`). |
| `0x4002F548` | ARM | Read APSR/`cpsid` helper (`mrs r0,apsr; and #0xC0; cpsid if`); called by Thumb `0x4002E178`/`0x4002E1E6`. |
| `0x4002EB92` | Thumb | MLO callback: `bl` from `0x4002BBB2` (loop over `[r5+0x404]` entries, `r1=sp`); return checked by `cbz`. |

### 2b. Complete start-address inventory (`0x40028000`–`0x4002FF20`)

Auto-derived: `mode` from §1; `size` = extent to next start / first `bx lr`/`pop pc`
(so sizes occasionally under/over-shoot at veneers and shared tails); `callers` and
`callees` are `bl`/`blx` sites only (plain `b` joins are excluded). Empty = none seen.
Addresses are Thumb starts (odd-in-pointer form); ARM starts are 4-aligned.

| Address | Mode | Size | Notable callees | Notable callers |
|---|---|---|---|---|
| 0x40028000 | arm | 190 |  |  |
| 0x400280c0 | arm | 50 |  | 4002ab02,4002ac5a,4002c4b6,4002c698 |
| 0x400284c0 | thumb | 22 |  | 4002b818,4002b894,4002b9ba |
| 0x400284d6 | thumb | 38 |  | 40028532,4002b8c0,4002b8e2 |
| 0x400284fc | thumb | 106 | 400284d6 | 40028588 |
| 0x40028566 | thumb | 58 | 400284fc | 4002b820,4002b89c,4002b9c2,4002c760 |
| 0x400285a0 | thumb | 32 |  | 400285fc,4002b8b6 |
| 0x400285ca | thumb | 58 | 400285a0 | 4002b830 |
| 0x40028604 | thumb | 14 |  | 4002b900 |
| 0x40028620 | thumb | 20 |  | 4002b8fc |
| 0x4002869c | thumb | 56 |  | 4002e5e4 |
| 0x400286d4 | thumb | 26 |  | 4002e564 |
| 0x400286ee | thumb | 32 |  | 4002e2be,4002e348,4002e586,4002e5c0,4002e65c |
| 0x40028718 | thumb | 16 |  | 4002e264,4002e2a4,4002e2d4,4002e56a,4002e5a4,4002e640 |
| 0x40028740 | arm | 58 |  | 40028c04 |
| 0x4002882c | arm | 6 | 40028834,400288d0 |  |
| 0x40028834 | arm | 50 | 400288d0 | 4002882c |
| 0x400288c4 | arm | 6 |  | 400288d4 |
| 0x400288cc | arm | 2 |  | 40028904 |
| 0x400288d0 | arm | 30 | 400288c4 | 40028830,40028850 |
| 0x400288f8 | thumb | 12 | 4002f2f0 |  |
| 0x40028904 | arm | 110 | 400288cc,40028974,4002a4d8,4002a51c,4002f302 | 4002f2f8 |
| 0x40028974 | arm | 2 | 40028978 | 40028970 |
| 0x40028978 | arm | 2 | 4002897c | 40028974 |
| 0x4002897c | arm | 2 | 40028980 | 40028978 |
| 0x40028980 | arm | 2 | 40028984 | 4002897c |
| 0x40028984 | arm | 2 | 40028988 | 40028980 |
| 0x40028988 | arm | 2 | 4002898c | 40028984 |
| 0x4002898c | arm | 2 | 40028990 | 40028988 |
| 0x40028990 | arm | 82 |  | 4002898c |
| 0x40028a18 | thumb | 90 | 40028a7c,40028db6,40029ae6,4002a41c,4002a430,4002a454,+3 |  |
| 0x40028a7c | thumb | 388 | 40028e72,40028fac,4002979c,400297d8,4002a430,4002a454,+5 | 40028a6a |
| 0x40028c00 | arm | 58 | 40028740,4002b740 |  |
| 0x40028c44 | thumb | 14 |  | 40028df2,4002c160,4002c72a |
| 0x40028d4e | thumb | 104 | 40029ae6,40029ca0,4002b6c0,4002b6dc | 40028dde |
| 0x40028db6 | thumb | 70 | 40028c44,40028d4e,40029cd0,40029f26,4002a51c,4002b64e | 40028a60 |
| 0x40028dfc | thumb | 6 |  | 4002ad8e,4002cd46 |
| 0x40028e0a | thumb | 6 |  | 4002b028,4002cdd0 |
| 0x40028e1a | thumb | 6 |  | 4002ad98,4002cd62 |
| 0x40028e2a | thumb | 6 |  | 4002ad7c,4002c79a,4002cd3a |
| 0x40028e4a | thumb | 6 |  | 4002b01e,4002cdc4 |
| 0x40028e72 | thumb | 18 |  | 40028b28 |
| 0x40028f04 | thumb | 32 | 4002f4ac,4002f4f4 | 40028f30,40028f42,40028f54,40028f68,40028f7a,40028f8c |
| 0x40028f24 | thumb | 18 | 40028f04 | 4002c478 |
| 0x40028f36 | thumb | 18 | 40028f04 | 4002c486 |
| 0x40028f48 | thumb | 18 | 40028f04 | 4002c49a |
| 0x40028f5a | thumb | 20 | 40028f04 | 4002c438 |
| 0x40028f6e | thumb | 18 | 40028f04 | 4002c8d6 |
| 0x40028f80 | thumb | 18 | 40028f04 | 4002c8dc |
| 0x40028f92 | thumb | 10 |  | 4002c8ea |
| 0x40028f9c | thumb | 10 |  | 4002c5c2 |
| 0x40028fa6 | thumb | 6 |  | 4002c576 |
| 0x40028fac | thumb | 8 |  | 40028aa0 |
| 0x40028fb4 | thumb | 152 | 4002b46c,4002b4a0,4002b4fa | 400292f2,4002bb98 |
| 0x40029050 | thumb | 82 | 4002b3d2,4002b3fe,4002f004 | 400292e4 |
| 0x400290c0 | thumb | 110 | 4002f2b0 | 400291d2 |
| 0x40029130 | thumb | 152 | 4002f2b0 | 400292a6,400292d6 |
| 0x4002925c | thumb | 68 |  | 4002bafa |
| 0x40029300 | thumb | 24 | 4002f53c | 4002bd88,4002c04c,4002ec5a |
| 0x40029342 | thumb | 62 | 4002b7b0,4002b7c0,4002f53c | 400295ee,4002beb6,4002c094,4002c0a2 |
| 0x4002938a | thumb | 46 | 4002b7ca,4002f53c | 4002977a,4002bfbe,4002c01c,4002c022,4002c0aa |
| 0x4002956c | thumb | 142 | 40029342,40029ede,40029f2e | 4002cc90 |
| 0x400295fa | thumb | 140 |  | 4002c986,4002ca48,4002ca74,4002cafa,4002cb4c,4002cbac,+1 |
| 0x40029686 | thumb | 178 |  | 4002c93c,4002c972,4002ca34,4002ca60,4002cae6,4002cb38,+2 |
| 0x40029738 | thumb | 78 | 4002938a | 4002cce4,4002cd00 |
| 0x40029794 | thumb | 8 |  | 40028eaa |
| 0x4002979c | thumb | 6 |  | 40028a8e,4002ab18,4002ad70,4002af82,4002b012,4002c364,+2 |
| 0x400297b6 | thumb | 16 |  | 4002c8a8 |
| 0x400297c6 | thumb | 18 |  | 4002c6d4,4002c828,4002edaa |
| 0x400297d8 | thumb | 16 |  | 40028a98,4002c822 |
| 0x400297f0 | thumb | 16 |  | 40029804,40029b14 |
| 0x4002984c | thumb | 26 |  | 40029868,40029982,40029a62 |
| 0x40029912 | thumb | 26 |  | 40029930,40029a0a |
| 0x40029ae6 | thumb | 42 |  | 40028a2e,40028cea,40028cf2,40028d5c,4002a2fe,4002a306,+6 |
| 0x40029ca0 | thumb | 40 |  | 40028cba,40028cc2,40028daa |
| 0x40029cd0 | thumb | 12 |  | 40028dec,4002c150 |
| 0x40029d44 | thumb | 84 |  | 40028c58,40028c64,40028c70,40028c7c,40028c98,40028cce,+3 |
| 0x40029d9c | thumb | 8 |  | 40028ca6,40028cdc,40028d10,40028d34 |
| 0x40029e1c | thumb | 190 |  | 40028cac,40028ce2,40028d16,40028d3a |
| 0x40029ede | thumb | 40 |  | 40029586 |
| 0x40029f16 | thumb | 6 |  | 40028c88 |
| 0x40029f26 | thumb | 8 |  | 40028de4 |
| 0x40029f2e | thumb | 6 |  | 4002958e,4002c148 |
| 0x40029f7c | thumb | 16 |  | 4002a138,4002a178,4002a192,4002a1ac |
| 0x40029f9c | thumb | 18 |  | 4002a060,4002a0ac,4002a100,4002a12e,4002a164,4002a184,+1 |
| 0x40029fd6 | thumb | 20 |  | 4002a06c,4002a0b8,4002a110 |
| 0x4002a056 | thumb | 144 | 40029f9c,40029fd6 | 4002a154 |
| 0x4002a0e6 | thumb | 210 | 40029f7c,40029f9c,40029fd6,4002a056 | 4002a34c,4002a35e,4002a380,4002a3c4,4002a3d6,4002a3f4 |
| 0x4002a1ba | thumb | 88 |  | 4002a32a,4002a39e |
| 0x4002a212 | thumb | 170 |  | 4002a322,4002a396 |
| 0x4002a2bc | thumb | 14 |  | 40028d46,4002a314 |
| 0x4002a2ca | thumb | 22 |  | 4002bc2a,4002c6f8 |
| 0x4002a41c | thumb | 20 |  | 40028a32 |
| 0x4002a430 | thumb | 36 |  | 40028a36,40028b78 |
| 0x4002a454 | thumb | 36 |  | 40028a40,40028b8e |
| 0x4002a478 | thumb | 62 |  | 40028a3c |
| 0x4002a4c0 | thumb | 6 |  | 40028a20 |
| 0x4002a4d8 | thumb | 68 |  | 40028964 |
| 0x4002a51c | thumb | 54 |  | 4002896c,40028a26,40028a4c,40028a88,40028ab6,40028b5a,+58 |
| 0x4002a560 | thumb | 8 |  | 40028eb6 |
| 0x4002a580 | thumb | 22 |  | 4002a60c,4002ec6e |
| 0x4002a596 | thumb | 42 |  | 4002a5f8,4002a616 |
| 0x4002a5c0 | thumb | 34 |  | 4002a5f4,4002a612 |
| 0x4002a5e6 | thumb | 56 | 4002a51c,4002a580,4002a596,4002a5c0 | 4002ec7a |
| 0x4002a6a6 | thumb | 194 |  | 4002a802,4002eb3e,4002eb6a |
| 0x4002a76c | thumb | 78 |  | 4002a7fa |
| 0x4002a95a | thumb | 54 |  | 4002b814,4002b890,4002b9b6,4002bd90,4002c0c0,4002c0d4,+3 |
| 0x4002a990 | thumb | 52 | 4002c3f8 | 4002aa26,4002ab68,4002abc4,4002ad0e |
| 0x4002a9ec | thumb | 148 | 4002a990 | 4002aeac |
| 0x4002ab0e | thumb | 20 | 4002979c | 4002ab88,4002ac92 |
| 0x4002ab44 | thumb | 180 | 4002a990,4002ab0e | 4002aea6 |
| 0x4002ac64 | thumb | 82 | 4002ab0e,4002c352 | 4002ae7e |
| 0x4002acd6 | thumb | 98 | 4002a990 | 4002ae5c |
| 0x4002ad5c | thumb | 120 | 40028dfc,40028e1a,40028e2a,4002979c,4002a51c,4002f2d0 | 40028b66 |
| 0x4002aee0 | thumb | 98 | 4002c6c0 | 4002af6e |
| 0x4002af42 | thumb | 130 | 4002979c,4002a51c,4002aee0,4002c254,4002c352,4002c638 | 4002b21e |
| 0x4002afc4 | thumb | 30 |  | 4002b0cc,4002b14c |
| 0x4002affe | thumb | 186 | 40028e0a,40028e4a,4002979c,4002a51c,4002b64e,4002c808 | 40028b8a |
| 0x4002b25c | thumb | 18 | 4002f2b0 | 4002b2d4 |
| 0x4002b2ca | thumb | 56 | 4002b25c,4002f2b0 | 4002b354 |
| 0x4002b31a | thumb | 10 |  | 4002b34a |
| 0x4002b32e | thumb | 48 | 4002b2ca,4002b31a | 400292b0 |
| 0x4002b36a | thumb | 98 | 4002f2b0 | 400292bc |
| 0x4002b3d2 | thumb | 36 | 4002b46c | 40029062 |
| 0x4002b3fe | thumb | 82 | 4002b4a0,4002b4fa | 4002906c |
| 0x4002b46c | thumb | 48 |  | 40028fe2,4002b3ec |
| 0x4002b4a0 | thumb | 24 |  | 40029030,4002b414,4002b61a |
| 0x4002b4cc | thumb | 26 |  | 4002b60a |
| 0x4002b4fa | thumb | 124 |  | 40029020,4002b438 |
| 0x4002b638 | thumb | 22 |  | 4002b696 |
| 0x4002b64e | thumb | 80 | 4002b638,4002f2b0 | 40028aae,40028dc6,4002b076 |
| 0x4002b6c0 | thumb | 28 |  | 40028d64 |
| 0x4002b6dc | thumb | 8 |  | 40028d6e |
| 0x4002b740 | arm | 86 |  | 40028c08 |
| 0x4002b798 | thumb | 4 |  |  |
| 0x4002b79c | thumb | 20 | 4002e12c,4002e134 | 40029318 |
| 0x4002b7b0 | thumb | 16 | 4002e16e | 40029372 |
| 0x4002b7c0 | thumb | 10 | 4002e142 | 40029378 |
| 0x4002b7ca | thumb | 10 | 4002e1cc | 400293a6,400293fc |
| 0x4002b7d4 | thumb | 18 | 4002e218 | 400293ce |
| 0x4002b7ea | thumb | 10 | 4002e246 | 4002940a |
| 0x4002b970 | thumb | 52 | 4002e6e4,4002e6f4 | 4002b9cc |
| 0x4002bc10 | thumb | 44 | 4002a2ca |  |
| 0x4002bc7c | thumb | 34 |  | 4002bd0e,4002bd42,4002bf14,4002bf48 |
| 0x4002bfd4 | thumb | 10 |  | 4002c0ea |
| 0x4002c254 | thumb | 118 | 4002f004,4002f078,4002f0bc | 4002ae10,4002af54,4002af7c |
| 0x4002c2cc | thumb | 96 |  | 4002aac6,4002c3b8 |
| 0x4002c32c | thumb | 38 |  | 4002c3ac |
| 0x4002c352 | thumb | 120 | 4002979c,4002a51c,4002c2cc,4002c32c | 4002ac80,4002afa4 |
| 0x4002c3d8 | thumb | 32 |  | 4002ae18,4002c66a |
| 0x4002c3f8 | thumb | 32 |  | 4002a9b6,4002c4f2,4002c50c,4002c526,4002c544,4002c5ec |
| 0x4002c428 | thumb | 28 | 40028f5a | 4002aaf6,4002c564 |
| 0x4002c446 | thumb | 102 | 40028f24,40028f36,40028f48,4002a51c,4002eda4 | 4002aab0,4002c55a |
| 0x4002c4be | thumb | 176 | 4002c3f8,4002c428,4002c446 | 4002c692 |
| 0x4002c56e | thumb | 20 | 40028fa6,4002a51c | 4002ac4e,4002c622 |
| 0x4002c584 | thumb | 30 | 4002a51c | 4002ac42,4002c61c |
| 0x4002c5d2 | thumb | 90 | 4002c3f8,4002c56e,4002c584,4002cd10 | 4002c684 |
| 0x4002c638 | thumb | 38 | 4002979c,4002a51c | 4002afbc |
| 0x4002c69e | thumb | 22 | 4002a51c | 4002acc6 |
| 0x4002c6c0 | thumb | 230 | 40028566,40028c44,40028e2a,400297c6,4002a2ca,4002a51c,+1 | 4002ad54,4002af38 |
| 0x4002c808 | thumb | 24 | 4002979c | 4002b052 |
| 0x4002c91c | thumb | 42 | 40029686 | 4002c9da,4002ca0c,4002cab8,4002cb6a |
| 0x4002c954 | thumb | 148 | 400295fa,40029686,4002c91c | 4002ccca |
| 0x4002c9e8 | thumb | 48 | 4002c91c | 4002ccf0 |
| 0x4002ca18 | thumb | 174 | 400295fa,40029686,4002c91c | 4002cc14 |
| 0x4002cac6 | thumb | 64 | 400295fa,40029686 | 4002cbfe |
| 0x4002cb1c | thumb | 90 | 400295fa,40029686,4002c91c | 4002cbf8 |
| 0x4002cb76 | thumb | 66 | 400295fa,40029686 | 4002cbda |
| 0x4002cbd4 | thumb | 74 | 4002ca18,4002cac6,4002cb1c,4002cb76 | 4002ccdc |
| 0x4002cc32 | thumb | 58 | 400295fa,40029686 | 4002cc9c |
| 0x4002cc6c | thumb | 160 | 4002956c,40029738,40029ae6,4002a51c,4002a95a,4002c954,+3 | 4002c002,4002c054,4002ec76 |
| 0x4002cd10 | thumb | 6 |  | 4002ac12,4002c5f8 |
| 0x4002cd1e | thumb | 82 | 40028dfc,40028e1a,40028e2a,4002f1e0 | 4002ce62 |
| 0x4002cdaa | thumb | 70 | 40028e0a,40028e4a,4002f1e0 | 4002ce78 |
| 0x4002ce90 | thumb | 16 |  | 4002ceb0 |
| 0x4002ceb0 | arm | 10 | 4002ce90 |  |
| 0x4002cebc | thumb | 158 |  | 4002cf5e |
| 0x4002cf5a | thumb | 94 | 4002cebc | 4002c0e2 |
| 0x4002cfc8 | thumb | 20 |  | 4002c13e |
| 0x4002cfdc | thumb | 122 | 4002a51c | 4002c16c |
| 0x4002d060 | thumb | 90 |  | 4002d0e8,4002d322 |
| 0x4002d0ba | thumb | 108 | 4002d060 | 4002c1ca |
| 0x4002d126 | thumb | 126 |  | 4002c1fc |
| 0x4002d1a4 | thumb | 118 |  | 4002c016,4002db4c |
| 0x4002d2c2 | thumb | 50 |  | 4002db46 |
| 0x4002d2f6 | thumb | 82 | 4002d060 | 4002db40 |
| 0x4002d34a | thumb | 58 |  | 4002d3e2,4002d468 |
| 0x4002d384 | thumb | 48 |  | 4002db30 |
| 0x4002d40c | thumb | 50 |  | 4002db08 |
| 0x4002d4fc | thumb | 36 |  | 4002d9ac |
| 0x4002d520 | thumb | 86 |  | 4002d99e |
| 0x4002d5d0 | thumb | 54 |  | 4002d9b8 |
| 0x4002d612 | thumb | 258 |  | 4002d9b2 |
| 0x4002d724 | thumb | 86 |  | 4002d968 |
| 0x4002d7b2 | thumb | 26 |  | 4002d956 |
| 0x4002d7da | thumb | 82 |  | 4002d8ce |
| 0x4002d830 | thumb | 194 | 4002d7da | 4002d962 |
| 0x4002d8fe | thumb | 36 |  | 4002d95c |
| 0x4002d9e0 | thumb | 60 |  | 4002da28 |
| 0x4002da34 | thumb | 76 |  | 4002daf6 |
| 0x4002daa0 | thumb | 24 | 4002a51c | 4002dace |
| 0x4002db8c | thumb | 6 |  | 4002c228 |
| 0x4002db92 | thumb | 6 |  | 4002c232 |
| 0x4002dba0 | thumb | 20 |  | 4002b904 |
| 0x4002dbb4 | thumb | 32 |  | 4002b90a |
| 0x4002dbd4 | thumb | 98 |  | 4002e5f0 |
| 0x4002dc68 | thumb | 110 |  | 4002baa0 |
| 0x4002dcd6 | thumb | 86 |  | 4002dd34,4002ea68,4002eaba |
| 0x4002dd2c | thumb | 18 | 4002dcd6 | 4002baa8 |
| 0x4002dd8e | thumb | 66 |  | 4002eb16 |
| 0x4002ddd4 | thumb | 78 |  | 4002e7c6,4002e7da,4002e872,4002e912,4002e944,4002e956,+16 |
| 0x4002de60 | thumb | 44 |  | 4002e7e6,4002e87e,4002ec0e,4002ecc2,4002ed24 |
| 0x4002decc | thumb | 22 |  | 4002e7ce |
| 0x4002dee2 | thumb | 22 |  | 4002e7ee |
| 0x4002def8 | thumb | 28 |  | 4002e99e,4002ea3a |
| 0x4002df14 | thumb | 34 |  | 4002badc |
| 0x4002df36 | thumb | 34 |  | 4002bae2 |
| 0x4002e0a8 | thumb | 14 |  | 4002c142 |
| 0x4002e0b6 | thumb | 14 |  | 4002bffa |
| 0x4002e0ce | thumb | 16 |  | 4002c124 |
| 0x4002e0de | thumb | 16 |  | 4002c112 |
| 0x4002e0fc | thumb | 38 |  | 4002c104 |
| 0x4002e12c | thumb | 8 |  | 4002b7a2 |
| 0x4002e134 | thumb | 12 |  | 4002b7a8 |
| 0x4002e142 | thumb | 22 |  | 4002b7c2,4002e1be |
| 0x4002e158 | thumb | 22 |  | 4002b800,4002e180,4002e1ee |
| 0x4002e16e | thumb | 92 | 4002e142,4002e158,4002f548 | 4002b7b8 |
| 0x4002e1cc | thumb | 22 |  | 4002b7cc,4002e1f4 |
| 0x4002e202 | thumb | 22 |  | 4002b7f6 |
| 0x4002e218 | thumb | 46 |  | 4002b7d8 |
| 0x4002e246 | thumb | 8 |  | 4002b7ec |
| 0x4002e25c | thumb | 66 | 40028718 | 4002ef1a |
| 0x4002e29e | thumb | 44 | 400286ee,40028718 | 4002e3f2,4002e6be,4002e6ce |
| 0x4002e2ca | thumb | 136 | 400286ee,40028718 | 4002ee9c |
| 0x4002e3fa | thumb | 70 |  | 4002e610 |
| 0x4002e440 | thumb | 8 |  | 4002e5dc |
| 0x4002e45c | thumb | 60 |  | 4002e57e,4002e5b8,4002e654 |
| 0x4002e498 | thumb | 136 |  | 4002e576,4002e5b0,4002e64c |
| 0x4002e536 | thumb | 8 |  | 4002e594 |
| 0x4002e626 | thumb | 86 | 400286ee,40028718,4002e45c,4002e498 | 4002e69e |
| 0x4002e68e | thumb | 44 | 4002e626 | 4002b8e8 |
| 0x4002e6ba | thumb | 38 | 4002e29e,4002ee94,4002ef14 | 4002b8ca |
| 0x4002e6e4 | thumb | 16 |  | 4002b978 |
| 0x4002e6f4 | thumb | 20 |  | 4002b982 |
| 0x4002e708 | thumb | 28 |  | 4002e748 |
| 0x4002e724 | thumb | 48 | 4002e708 | 4002e762,4002e78c |
| 0x4002e754 | thumb | 24 | 4002e724 | 4002ba28 |
| 0x4002e77a | thumb | 28 | 4002e724 | 4002b9f6 |
| 0x4002e7ac | thumb | 148 | 4002ddd4,4002de60,4002decc,4002dee2 | 4002eaee |
| 0x4002e844 | thumb | 134 | 4002ddd4,4002de60 | 4002eae4 |
| 0x4002e8ce | thumb | 32 |  | 4002eaae |
| 0x4002e904 | thumb | 26 | 4002ddd4 | 4002ea86 |
| 0x4002e930 | thumb | 290 | 4002ddd4,4002def8 | 4002ea72,4002ea7a |
| 0x4002ea5e | thumb | 158 | 4002dcd6,4002ddd4,4002e7ac,4002e844,4002e8ce,4002e904,+1 | 4002bab0 |
| 0x4002eb92 | thumb | 162 | 4002ddd4,4002de60 | 4002bbb2 |
| 0x4002ec38 | thumb | 100 | 40029300,40029ae6,4002a580,4002a5e6,4002a95a,4002cc6c | 4002ba98 |
| 0x4002eca4 | thumb | 44 | 4002ddd4,4002de60 | 4002bac0 |
| 0x4002ece8 | thumb | 74 | 4002ddd4,4002de60 | 4002bac8 |
| 0x4002ed48 | thumb | 38 | 4002ddd4 | 4002ed86 |
| 0x4002ed76 | thumb | 28 | 4002ed48 | 4002bad0 |
| 0x4002eda4 | thumb | 26 | 400297c6 | 4002c46a,4002c5b2 |
| 0x4002ee94 | thumb | 64 | 4002e2ca | 4002e6c6 |
| 0x4002ef14 | thumb | 220 | 4002e25c | 4002e6d6 |
| 0x4002f004 | thumb | 68 |  | 40029088,4002c29c |
| 0x4002f078 | thumb | 18 |  | 4002c28e |
| 0x4002f0bc | arm | 14 |  | 4002c26e |
| 0x4002f0cc | arm | 210 |  | 4002b866,4002bc6a,4002c8c2,4002c8ce,4002c8e6 |
| 0x4002f1a0 | arm | 62 |  | 40028e88,4002c83c |
| 0x4002f1e0 | arm | 82 |  | 4002b888,4002cd56,4002cddc |
| 0x4002f234 | arm | 122 |  | 40029cea,4002edc6 |
| 0x4002f2b0 | arm | 30 |  | 400290ea,400290fe,400291ac,400291b6,40029252,4002b264,+16 |
| 0x4002f2d0 | arm | 30 |  | 40028b46,40028ba4,4002adc8,4002b164,4002b20e,4002c89e,+1 |
| 0x4002f2f0 | thumb | 18 | 40028904 | 400288f8 |
| 0x4002f302 | thumb | 4 |  | 40028908 |
| 0x4002f310 | arm | 70 |  | 40028174 |
| 0x4002f384 | arm | 6 |  | 40028170 |
| 0x4002f4ac | arm | 70 |  | 40028f0c |
| 0x4002f4f4 | arm | 70 |  | 40028f1e |
| 0x4002f53c | arm | 10 |  | 40029302,4002934a,4002938e,400293c4 |
| 0x4002f548 | arm | 14 |  | 4002e178,4002e1e6 |
| 0x4002f56c | thumb | 4 |  |  |
| 0x4002f570 | arm | 6 |  |  |

---

## 3. Call graph (top level)

```
0x40028000 (ARM reset vector: b 0x40028910)
      |
      v
0x40028910 (ARM reset entry)
   |  MIDR[1:0]!=0  -> 0x400289A0  CPU1 secondary (smc 0x107; read 0x48281400+0x800/+0x804; bx r1)
   |  else (CPU0):
   |     0x40028934..0x4002894C  device-type check; bleq 0x40028C00 (ARM island)
   |     msr CPSR_c,#0xD3; VBAR=0x00028000; SP=0x4030D000
   |     blx 0x4002A4D8   (record reset reason)
   |     blx 0x4002A51C   (progress bit 0)
   |     8x bl .+4        (0x40028970..0x4002898C)
   |     0x40028990       enable I-cache
   v
0x4002882C (ARM trampoline)
   |-- bl 0x40028834 -> walks table 0x4002FF80 -> {memcpy 0x40028870, memset 0x40028898}
   v
0x400288D0 (load SP; blx 0x400288C4; bx r12 -> Thumb 0x40028A18)
   v
0x40028A18 (Thumb main)
   |-- bl 0x4002A4C0 (read PRM_RSTST)
   |-- bl 0x4002A51C (bit 1; later bit 2)
   |-- bl 0x40029AE6 (6,1)  -> tbb dispatch
   |-- bl 0x4002A41C / 0x4002A430 / 0x4002A454   (0x4A314000: init + WDT kicks)
   |-- bl 0x4002A478 (config w/ callback 0x40028A74)
   |-- bl 0x40028DB6 (shared helper)
   `-- bl 0x40028A7C (config parser) -> 0x4002AD5C, 0x4002AFFE, ...
```

Key hubs and cross-mode edges:

* `0x4002A51C` — 63 call sites (boot milestones); the single most-referenced function.
* `0x40029AE6` — `tbb [pc,r2]` dispatcher, 12 callers, 10 case targets (indirect).
* `0x40028DB6` / `0x40028D4E` — shared Thumb helpers with many callers.
* Thumb → ARM: `0x400280C0` (from `0x4002AB02/AC5A/C4B6/C698`), `0x40028904`
  (from `0x4002F2F8`), `0x4002F4AC`/`0x4002F4F4` (from `0x40028F0C/1E`),
  `0x4002F53C` (from `0x40029302/34A/38E/3C4`), `0x4002F548` (from `0x4002E178/1E6`),
  `0x4002F0CC` (from `0x4002BC6A`).
* ARM → Thumb: `0x40028964→0x4002A4D8`, `0x4002896C→0x4002A51C`,
  `0x4002CEB0(ARM)→0x4002CE90(Thumb)`.
* ARM → Thumb veneer → ARM: `0x400288F8 (Thumb) → 0x4002F2F0 (Thumb) → 0x40028904 (ARM)`.
* ARM stub `0x4002B740` is reached only from `0x40028C08` (inside the `0x40028C00` ARM island).
* **Indirect / non-returning edges** (candidates for missed graph edges):
  `0x4002BC9C bx r1`, `0x40028C38 bx r0`, `0x400289E0 bx r1`, `0x4002BC7C bx r1`,
  `0x400281F4 bxne r0` / `0x400281F8 bxne r10`, and the self-incrementing veneers
  `0x40028970`..`0x4002898C` (`bl .+4`, `0x4002F302 b .`, hang stubs `0x40028080`–`0x400280BC`).
* `0x400288D0` and `0x40028A7C` share a "fall-through into next function" shape; the
  parser `0x40028A7C` ends at `0x40028BFE` immediately before the ARM island at `0x40028C00`.

No recursion was observed in the direct `bl` graph; recursion would have to pass through
the indirect edges above.

---

## 4. Special items

### 4.1 "MLO" record at `0x4002BC00`

* Bytes `0x4002BC00`–`0x4002BC0B` = `"MLO        "` (12-byte, space-padded name field).
* `0x4002BC0C` (u32) = `0x0002EB93` → a **Thumb** entry at `0x4002EB92` (bit0 set).
* No code references the name field: scanning all 4-byte words for
  `0x0002BC00`/`0x0002BC01` returns **no hits**, so this record is not a normal
  literal-pool pointer table.
* The pointed-to function `0x4002EB92` is reached by `bl 0x4002EB92` from `0x4002BBB2`
  (a loop over `[r5+0x404]` entries that passes `r1 = sp` and checks the return with
  `cbz`) — i.e. the record is consumed by an entry-scan, not by a direct branch to
  `0x4002BC00`.

### 4.2 Function `0x4002BC7C` and its callers

Body (`0x4002BC7C`–`0x4002BC9C`):

```
ldr r2,[pc,#0x350]      @ 0x4002BFD0 -> 0x0002FA48   (per-index table base)
push {r4,r5}
mov r4,r0
ldr r3,[pc,#0x348]      @ 0x4002BFCC -> 0x4030DBE0   (IRAM context)
ldr.w r2,[r2,r4,lsl#2]  @ entry = table[index]
ldr r0,[r3]
ldr r4,[r2,#0x4]
ldrb r3,[r3,#0xa]
bics r4,r3
str r4,[r2,#0x4]
str r1,[r0,#0x1c]
ldr r1,[r0,#0x4]
cmp r1,#0x0
beq 0x4002BC9E
pop {r4,r5}
bx r1                   @ indirect callback
```

So `0x4002BC7C(index r0, value r1)` selects a table entry via the table at
`0x4002BFD0` (`0x0002FA48`), clears the mask from context `0x4030DBE0+0xA`, stores
`r1` at `ctx+0x1C`, and performs an **indirect `bx r1`** callback. Because it ends with
`bx r1`, its "callees" are not statically enumerable.

Callers — exactly five sites, all inside `0x4002BCxx`–`0x4002BFxx`:

| Site | Kind |
|---|---|
| `0x4002BCE4` | `b` |
| `0x4002BD0E` | `bl` |
| `0x4002BD42` | `bl` |
| `0x4002BF14` | `bl` |
| `0x4002BF48` | `bl` |

(All five were disassembled; `0x4002BC7C` is **not** "called from many sites" in the
sense of many distinct functions — they are five branch sites in the surrounding
boot-media block.)

### 4.3 "ARM stub near `0x4002B0xx"` (METHOD claim)

No ARM code exists in `0x4002B0xx` (disassembly in both modes there is Thumb). The ARM
stub is `0x4002B740`–`0x4002B798`, a register/pad loop over a peripheral based at
`0x4A100000` (literal at `0x4002B798`); its only caller is `0x40028C08`. Note that the
literal word at `0x4002B798` is immediately followed by Thumb code (see
`0x4002B79A ldr r2,[pc,#0x40]`), so `0x4002B798` is data, not an ARM instruction.

### 4.4 Other named specials confirmed

* ARM island `0x40028C00`–`0x40028C44` (device-type gate), reached by `bleq 0x40028C00`
  from `0x40028948`.
* SMC stub `0x4002CEB0`: `blx 0x4002CE90; mov r12,#0xF0; smc #0`; referenced via the
  literal `0x0002CEB0` at `0x4002AE50` and `0x4002C6BC`.
* IRAM copy table `0x4002FF80`: `{0x0002FFC0, 0x4030D000, 0x40, 0x00028870}`, …,
  final record `{0x000300A0, 0x4030D1A0, 0xABC, 0x00028898}` (memcpy ×3, memset ×1).
* Second vector table `0x4002FFC0`: 8×`ldr pc,[pc,#0x18]`; targets at
  `0x4002FFE0` = `0x00028090, 0x00028080, 0x00028084, 0x000289E4, 0x000289F0, 0x00028090, 0x00029420, 0x00028098`.

---

## 5. Evidence list (LLVM-derived)

Commands used (wrapper ELF `/tmp/opencode/rea-omap/wrap.elf`):
`llvm-objdump -d --triple={arm,thumb}v7-none-eabi --mcpu=cortex-a9 --no-show-raw-insn
--start-address=… --stop-address=…` (helpers `d.sh`, `dr.sh`), plus Python over the raw
file for word xrefs.

Ranges disassembled to fix the mode map (in **both** modes unless noted):

* `0x40028000`–`0x40028040` (ARM entry/vectors).
* `0x400280C0`–`0x40028120`; `0x40028100`–`0x40028200` (ARM).
* `0x400284C0`–`0x40028530`, `0x40028694`–`0x40028730` (Thumb).
* `0x40028730`–`0x400287C0` (ARM = `0x40028740`/`0x4002877C`).
* `0x40028820`–`0x40028920` (ARM trampoline/memcpy/memset + `0x400288F8` Thumb veneer).
* `0x40028900`–`0x40028980`, `0x40028990`–`0x40028A18` (ARM).
* `0x40028A18`–`0x40028A7C` (Thumb main).
* `0x40028BF0`–`0x40028C60` (ARM island `0x40028C00`).
* `0x4002A2C0`–`0x4002A2E0`, `0x4002A41C`–`0x4002A480`, `0x4002A4D8`–`0x4002A560`,
  `0x40029AE6`–`0x40029B20` (Thumb).
* `0x4002B730`–`0x4002B7A0` (ARM stub `0x4002B740` + Thumb resume), `0x4002B798`–`0x4002B7F4`,
  `0x4002BB90`–`0x4002BBC0` (Thumb).
* `0x4002BC10`–`0x4002BCA0`, `0x4002BCE0`–`0x4002BD60`, `0x4002BF00`–`0x4002BF60` (Thumb, `0x4002BC7C`).
* `0x4002CE80`–`0x4002CEC8` (Thumb `0x4002CE90` + ARM `0x4002CEB0`).
* `0x4002E160`–`0x4002E1D0` (Thumb, CPSR helper use).
* `0x4002EF80`–`0x4002F120`, `0x4002F180`–`0x4002F240`, `0x4002F2A0`–`0x4002F330`,
  `0x4002F540`–`0x4002F590` (Thumb/ARM tail boundary and string/mem library).
* Whole-file ARM/Thumb listings: `/tmp/opencode/rea-omap/arm_whole.txt`,
  `/tmp/opencode/rea-omap/thumb_whole.txt`.

Literal/word confirmations (little-endian reads at the given file offsets):
`0x40028020`=`0x1CF48309`; `0x400280F4`=`0x4A0022C4`, `0x400280F8`=`0x4A307B04`,
`0x400280FC`=`0x4A307B00`; `0x40028204`=`0x48281400`; `0x400289FC`=`0x4A0022C4`,
`0x40028A00`=`0x00028000`, `0x40028A04`=`0x4030D000`, `0x40028A08`=`0x4A307B04`;
`0x4002A558`=`0x4030D040`, `0x4002A55C`=`0x4030D060`; `0x4002B798`=`0x4A100000`;
`0x4002BC00`=`"MLO "`, `0x4002BC0C`=`0x0002EB93`; `0x4002BFCC`=`0x4030DBE0`,
`0x4002BFD0`=`0x0002FA48`; `0x40028868`=`0x00007718`, `0x4002886C`=`0x00007758`,
`0x400288FC`=`0x00000135`; `0x4002F9F8`=`0x80800C00`; `0x4002F4A8`=`0x0FFFE000`;
`0x4002FF80`=`0x0002FFC0`; vector targets `0x4002FFE0`.. `= 0x00028090, 0x00028080,
0x00028084, 0x000289E4, 0x000289F0, 0x00028090, 0x00029420, 0x00028098`.

Xref scans: word scans for `0x000280C0/1`, `0x0002F2B0/1`, `0x0002F310/1`,
`0x0002BC00/1`, `0x0002BC7C/D`, `0x0002EB91/2/3` over the whole file.

---

## 6. Unknowns / honest gaps

1. **Function extents in the big Thumb body (`0x40028C44`–`0x4002B736`,
   `0x4002BC10`–`0x4002F0BC`)** are approximate: the extent heuristic splits on the next
   `bl`/`blx` target or `bx lr`/`pop pc`, so shared tails and `b`-only local labels can
   merge or split neighbouring functions. Many entries in §2b are therefore
   "start known, purpose unknown".
2. **Purpose names are only given where code was actually read.** Roughly 200 of the 282
   starts are unclassified; they are listed by address/mode only.
3. **Data tables not decoded**: `0x4002FE3C`, `0x4002FEB4`, `0x4002FEE0` (16-bit tables),
   `0x4002FEF0`–`0x4002FF20` (offset/length table preceding the boot-image names
   `0x4002FF2C`+), and the `0x40028400` table (48 words; not proven to be pointers).
4. **The two-function question at `0x4002F0BC` vs `0x4002F0CC`**: `0x4002F0BC` replicates a
   byte then `b 0x4002F1A4`; `0x4002F0CC` is the unaligned `memcpy`. Whether these are
   two functions or one `memset`/`memcpy` pair sharing a tail is unresolved.
5. **`0x4A314000`** (`0x4002A41C/430/454/478`) is labelled "WDT-class" from its
   `0xAAAA/0x5555` and `0xBBBB/0x4444` write sequences and `+0x34`/`+0x48` offsets; the
   exact block identity is not confirmed from this image.
6. **`0x40028A7C` parser** return/handlers (`0x4002AD5C`, `0x4002AFFE`) and the structure
   at `0x40028BF8+0x64` are only partially characterised.
7. **The `0x40026F28`–`0x40027FFF` hole** (separate lower dump) remains the best
   candidate for the SMC/monitor dispatch; nothing in this 48-KB window is the SMC
   handler itself (only the `smc` call sites are here).
8. **Indirect edges** (`bx r1` at `0x4002BC9C`, `bx r0` at `0x40028C38`, `bx r1` at
   `0x400289E0`, `bxne r0/r10` at `0x400281F4/F8`) mean the static call graph is
   incomplete by construction.
9. The 48 words at `0x40028400` are guessed to be Thumb function pointers; none was
   verified to be consumed as a pointer by a disassembled load.

*All disassembly, mode boundaries, xrefs and literal values in this document were
produced by LLVM 23 (`llvm-objdump`) and Python over the raw dump; they are
LLVM-derived observations, not REA Evidence.*
