# 67 — Ghidra seeded-start decompilation of the 65 boot-ROM entries recursive descent missed

Status: **complete** — closes item (3) of `HANDOFF.md` ("seed the 65 genuine boot-ROM
starts with explicit TMode context and decompile"). All 65 entries were created and
decompiled; 61 bodies are semantically usable, 4 are decompiler artefacts (§6.1).
Date: 2026-10-20.
Target: `omap4430-bootrom-0x40028000.bin` — 49152 bytes, raw binary, load base
`0x40028000`, ARMv7-A Cortex-A9, mixed A32 + Thumb-2, SHA-256
`fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`.

**Provenance: Ghidra 12.1.4 headless (analyzeHeadless + Java post-script) output;
this is NOT REA MCP Evidence.**

---

## 1. Method and provenance

Ghidra 12.1.4 at `/home/psyden/tools/ghidra_12.1.4_PUBLIC`, JDK 21:

```
analyzeHeadless <proj> rea-seeded \
  -import omap4430-bootrom-0x40028000.bin \
  -processor "ARM:LE:32:v7" -loader BinaryLoader -loader-baseAddr 0x40028000 \
  -scriptPath <scratch> -postScript SeedStarts.java <starts.txt> <out>
```

`SeedStarts.java` reads the 65 `address mode` pairs from `starts.txt`
(`/tmp/.../scratchpad/ghidra/starts.txt`), sets the ARM `TMode` context register
(1 = Thumb, 0 = ARM) at each entry, disassembles with `followFlow`, calls
`createFunction` when no function is present, then decompiles the containing function
with `DecompInterface`. **Result: 65/65 created and decompiled.** For 7 entries
Ghidra had already decoded the byte in the other mode, so the conflicting instruction
was cleared at the entry before re-writing context: `0x40028990`, `0x4002cebc`,
`0x4002d1a4`, `0x4002e0b6`, `0x4002e6e4`, `0x4002e6f4`, `0x4002ef14`. Those seven are
exactly the cases where Ghidra's prior decode disagreed with the byte-verified mode map
of `60-mode-map-resolution.md` — i.e. they are further independent support for that map.

Also used, and clearly separated from the Ghidra output: **literal-pool word reads**
(Python `struct` over the same image, data only, no disassembly). Every literal quoted
below as `pool 0x4002xxxx = …` is such a read; the pseudocode itself only names the pool
addresses. Pseudocode dump: `rea-ghidra/seeded-starts-decompiled.txt` (authoritative for
this report). Cross-check documents: `50-code-map.md`, `10-boot-media.md`,
`60-mode-map-resolution.md`, `63-ghidra-218-reconciliation.md`,
`64-rom-api-and-media-final.md`.

---

## 2. Results at a glance

* 65/65 seeded entries reached `createFunction` + `DecompInterface` success.
* The dump resolves the purpose of **every** entry except the two inside the ARM
  `smc` island (`0x4002CEB0`, `0x4002CEBC`), which decompile to `halt_baddata` (§6.1).
* One large subsystem becomes visible that no earlier report named: **`0x4002CE90`–`0x4002DB92`
  is the OMAP4430 USB OTG (Mentor/MUSB-class) device-mode driver, controller base
  `0x4A0AB000`** (pool `0x4002D2B0` = `0x4002D6B8` = `0x4A0AB000`), reached from the
  per-driver entry for device ID **`0x45` = DEVICE_USB** (`64` §1.2/§2.2). 26 of the 65
  seeded entries belong to it.
* Secondary clusters: MMCHS host primitives (9), MMC/SD card layer (11), flash/NAND
  (13), control/interrupt helpers (6), startup/IRAM (3), cache-TLB (2), monitor
  services (3), cmd-8 DDR open (1).

Address-space note: `0x4002...` = ROM code, `0x4030D0xx`/`0x4030C...` = IRAM statics,
`0x4A0AB000` = USB OTG, `0x4A100620`/`0x4A00233C` = control-module windows,
`0x4A304000` = 32-kHz-class timer, `0x48241000` = GIC distributor, `0x48281000` =
WakeupGen/SAR block, `0x48241000+0x204` = GICD set-pending.

---

## 3. Function table

Grouped by subsystem. "Callees/globals/literals" lists only what the pseudocode shows
(plus the resolved pool value where the pool word is decisive). Field/register offsets
are shown as `+0xNN` relative to the base named in the row.

### 3.1 Startup / IRAM copy / reset chain (3)

| address | mode | size | inferred purpose | notable callees / globals / literals |
|---|---|---|---|---|
| `0x4002882c` | arm | 60 | IRAM-copy **dispatcher**: loads record 0 from `0x4002FF80` as {src, dst, len, fn} and **calls the record's own function word indirectly** (bit 0 of the word selects Thumb). When the record pointer reaches `0x4002FFC0` it calls `0x400288D0` (SP load + Thumb handoff). | pool `0x40028868`=`0x7718`, `0x4002886C`=`0x7758`; dispatches `0x00028870` (ARM memcpy) with (`0x0002FFC0`, `0x4030D000`, `0x40`); `0x400288D0` |
| `0x400288f8` | thumb | 236 | The 4-byte Thumb `bl 0x4002F2F0` veneer **plus** (Ghidra merged) the ARM reset body that follows it: MIDR test → CPU1 handoff loop; else device-type gate → ARM island `0x40028C00`, `VBAR = 0x00028000`, reset-reason record, progress bit 0, `SCTLR \|= 0x1800`, copy dispatch. | `0x400289FC`=`0x4A0022C4` (device type), `0x40028A00`=`0x00028000`, `0x40028A08`=`0x4A307B04` (PRM_RSTST), `0x40028A0C`=`0x48281000`; `0x4002A4D8`, `0x4002A51C`, `0x40028C00` |
| `0x40028990` | arm | 16 | `mrc c1 / orr #0x1800 / mcr c1` — **I-cache (bit 12) + branch prediction (bit 11)** — then tail-branches into the `0x4002882C` dispatcher (Ghidra prints that shared tail as part of the body). | CP15 c1; tail = `0x4002882C` |

### 3.2 Boot media — MMCHS host-controller primitives (9)

Base for every row: `*(u32*)(0x0002FE20 + idx*4)` (pool `0x4002E048` = `0x0002FE20`).

| address | mode | size | inferred purpose | notable callees / globals / literals |
|---|---|---|---|---|
| `0x4002dc68` | thumb | 110 | Controller reset: `SYSCONFIG(0x110)\|=2`, poll `SYSSTATUS(0x114)[0]`; `HCTL(0x228)=0xC10`; `CAPA(0x240)\|=1<<25`; `CON(0x12C)=0` (whole word); `HCTL\|=0x100`; `IE(0x234)=0x307F0033`; `BLK(0x204)=0x200`. | pool `0x4002E04C`=`0x307F0033` |
| `0x4002dd8e` | thumb | 70 | Bus width: `CON` bit 5 / `HCTL` bit 1 (`bfi`-style read-modify-write). 1→(0,0), 2→(1,0), 4→(0,1); anything else → 1. | `CON 0x12C`, `HCTL 0x228` |
| `0x4002ddd4` | thumb | 140 | **Send command**: poll `PSTATE(0x224)` bit 1; `STAT(0x230)=0x337F87FF`; `ARG(0x208)=arg`; `CMD(0x20C)=cmdword` verbatim; on CC clear with `STAT=1` and store `RSP10`, plus `RSP32/54/76` only when `(cmdword>>16)&3 == 1`; ERRI(bit 15) → 1. | pool `0x4002E058`=`0x337F87FF`, `0x4002E054`=`0x4A304000`; timeout constant `0xccb` |
| `0x4002de60` | thumb | 108 | **Read block(s)**: per BRR (STAT bit 5, cleared with `STAT=0x20`) pull `(BLK&0xFFF)>>2` words from `DATA(0x220)`; stop after `param_3` blocks (when non-zero) or on ERRI(bit 15). | `STAT 0x230`, `DATA 0x220`, `BLK 0x204` |
| `0x4002decc` | thumb | 22 | `IE(0x234)` bit 21 ← 0. | |
| `0x4002dee2` | thumb | 22 | `IE(0x234)` bit 21 ← 1. | |
| `0x4002def8` | thumb | 28 | `CON(0x12C)` bit 0 ← (`param_2 == 1`). | |
| `0x4002df14` | thumb | 34 | `SYSCTL(0x22C) \|= 1<<25`, poll bit 25 — CMD-line soft reset. | |
| `0x4002df36` | thumb | 34 | `SYSCTL(0x22C) \|= 1<<26`, poll bit 26 — DAT-line soft reset. | |

### 3.3 Boot media — MMC/SD card layer (11)

| address | mode | size | inferred purpose | notable callees / globals / literals |
|---|---|---|---|---|
| `0x4002e7ac` | thumb | 138 | Bus-width / SCR probe. SD (`dev+0x04==2`): CMD55 then **CMD51 SEND_SCR** (`0x333A0010`), one-block read; `dev+0x18` (supported-width mask) built from two SCR bits (bit 8 → 1, bit 11 → 2), `dev+0x10` = SCR low nibble. MMC: switch on `dev+0x10` → mask `1` for 0–3, mask `7` for 4, else error. | `0x371A0000`, `0x333A0010`; `0x4002DECC/DEE2`, `0x4002DE60` |
| `0x4002e844` | thumb | 138 | **Capacity**. SD: CSD v2 `C_SIZE = (w1>>16) \| (w2&0x3F)<<16` (CSD bits[69:48]) → `dev+0x1C = C_SIZE << 10`. MMC: **CMD8 SEND_EXT_CSD** (`0x083A0010`) + 512-byte read → `dev+0x1C` = the word at block byte **0xD4 = EXT_CSD SEC_COUNT**; 0 → fallback `0x800000`. | `0x083A0010`, `0x09090000` |
| `0x4002e8ce` | thumb | 54 | **TRAN_SPEED → CLKD**: `M = (w3>>3)&0xF`, `N = w3&7` where `w3` = CSD bits[127:96] (low byte = TRAN_SPEED, bits[103:96]); guard `N<4 && M*8-8 < 0x71` is the table's own extent; `*out = u16[0x2FE3C + M*8 + N*2 - 8] << 1`. | pool `0x4002EBB4` = `0x0002FE3C` |
| `0x4002e904` | thumb | 44 | CMD9 SEND_CSD with `RCA<<16`; copies the four response words into **CSD word order** (`out[0]`=bits[31:0] … `out[3]`=bits[127:96]) — the reverse of the RSP10..RSP76 hardware order. | `0x09090000` |
| `0x4002e930` | thumb | 302 | Card init, part 1: `dev+0x20=0x1234` (RCA preset); CMD0 → CMD8(`0x1AA`) → CMD55; SD → `dev+0x04=2`, `dev+0x18=3`, ACMD41 arg `0x40FF8000`\|`0x00FF8000`; MMC → `dev+0x04=1`, `dev+0x18=1`, CMD1 `0x01020000`, `0x4002DEF8(ctrl,1)`. Polls while response bit 31 (busy) with a `0x7FFF`-tick guard, re-sending CMD55 for SD. Then **voltage check**: `(resp & 0x00FF8000)==0` → fail. `dev+0x14 = 2` when MMC `R3[30:29]==2` or SD CCS (bit 30 set), else 1. CMD2 (arg = the OCR word) → CMD3 (MMC: arg `0x1234<<16`; SD: arg 0 then `dev+0x20 = resp>>16`). | `0x371A0000`, `0x081A0000`, `0x29020000`, `0x01020000`, `0x02090000`, `0x031A0000`, `0x00FF8000`, `0x40FF8000` |
| `0x4002ea5e` | thumb | 158 | Card open, part 2: `CLKD=0xF2` → init (retried **once**) → CMD9/CSD → (MMC) seed `dev+0x10` from two CSD bits → CMD4 (`cmdword 0x04000000`, arg `0x04040000`) → CLKD from TRAN_SPEED → CMD7 (`RCA<<16`) → CMD16(`0x200`) → capacity (`0x4002E844`) → bus width (`0x4002E7AC`). | `0x04040000`, `0x071B0000`, `0x101A0000`, `0xEBD8`; `0x4002DCD6`, `0x4002E930/E904/E8CE/E844/E7AC` |
| `0x4002eb92` | thumb | 58 | Read **one** block of an outer device: arg = `req[0]<<9` (inner+0x14==1, byte addressing) or `req[0]` (block addressing); CMD17 `0x113A0010`; one block into `req[2]`. | `0x113A0010`; inner = `*(outer+0x18)` |
| `0x4002eca4` | thumb | 68 | Vendor command 45 (`0x2D3A0010`) arg `0xA1000000` + one-block read → eight words into `dev+0x24..0x40`. | `0x2D3A0010` |
| `0x4002ece8` | thumb | 96 | Per-region loop: for `i=0..7` with `dev[0x24+4i]!=0`, send cmd 45 arg `0xB1000000\|(i<<16)`, read one block, store **bit 1 of the word at block byte 0x110** into `dev[0x44+i]`. | `0x2D3A0010` |
| `0x4002ed48` | thumb | 46 | Select the active region: **command 43** `0x2B1B0000` (INDX 0x2B, RSP_TYPE 3 = 48-bit busy, no data) with arg `idx<<24`; on success `dev+0x13` = idx. | pool `0x4002EDA0` = `0x2B1B0000` |
| `0x4002ed76` | thumb | 38 | Choose **one** region: scan `dev[0x45..0x4B]` for the first entry `== 1` (i = 1..7; 0 if none) and call `0x4002ED48` once. | `0x4002ED48` |

### 3.4 Boot media — flash (NAND-class) path (13)

| address | mode | size | inferred purpose | notable callees / globals / literals |
|---|---|---|---|---|
| `0x4002b970` | thumb | 56 | ID consistency check: read the mirrored pair at `base+0x1E000` and the `0x90`-command pair at `base`; return 0 only if **both** 16-bit halves agree. | `0x4002E6E4`, `0x4002E6F4` |
| `0x4002dba0` | thumb | 20 | Reset a second L4 module: `SYSCONFIG(0x10)\|=2`, poll `SYSSTATUS(0x14)` bit 0. | pool `0x4002DC58` = `0x48078000` |
| `0x4002dbb4` | thumb | 32 | Configure that module: `u32[0x20] = (old & 0xF800FFFC) \| 0x07FF0000 \| (param & 3)`; `u32[0x1C] \|= 1` (interrupt 0 enable). | pool `0x4002DC5C`=`0xF800FFFC`, `0x4002DC60`=`0x07FF0000` |
| `0x4002e25c` | thumb | 66 | Read 4 ID bytes: first `0x40028718(0)` (enable in the `0x50000000` block); write `0x90` to `dev->cmd` and `0` to `dev->addr` — 16-bit when `dev+0x14==1`, else 8-bit; then 4 consecutive byte reads of `*dev->base`. Confirms the descriptor is `{base_ptr, cmd_ptr, addr_ptr, …, width@+0x14}`. | `0x40028718` |
| `0x4002e68e` | thumb | 44 | Region bitmap: for `i=0..3`, if `0x4002E626(dev,i)==0` set bit `i` of `dev+0x17`. | `0x4002E626` |
| `0x4002e6ba` | thumb | 42 | Probe: flash reset → **generic** geometry `0x4002EE94`; only if that fails, reset again and fall back to ID-table geometry `0x4002EF14`. Any failure → 1. | `0x4002E29E`, `0x4002EE94`, `0x4002EF14` |
| `0x4002e6e4` | thumb | 16 | Read the mirrored 16-bit pair at `base+0x1E000` / `+0x1E002`. | |
| `0x4002e6f4` | thumb | 20 | Read ID through command `0x90`: write `0x90` (16-bit) to `base`, read `base`, **re-write `0x90`**, read `base+2`. | |
| `0x4002e708` | thumb | 28 | Status wait: poll `u16[base+0x1E482]` bit 15, then return 1 if `u16[base+0x1E480]` has bit 0 or bit 10 set, else 0. | |
| `0x4002e724` | thumb | 48 | Issue a command: `u16[0x1E200] = param_2 & 0x1FF` (9-bit field), `u16[0x1E202]=0`, `u8[0x1E20E]=param_3`, `u16[0x1E400]=0x801`, `u16[0x1E482]=0`, `u16[0x1E440]=0`, then `0x4002E708()`. | |
| `0x4002e754` | thumb | 38 | Page read: issue the command, then copy `0x80` words (512 B) from `base+0x400` to the caller's buffer. | `0x4002E724` |
| `0x4002e77a` | thumb | 50 | Command + blank test: zero `u16[base+0x10020]`, issue the command, `*out = (u16[base+0x10020] == 0xFFFF)`. | `0x4002E724` |
| `0x4002ef14` | thumb | 236 | **Geometry decode from ID**: linear scan of the 2-byte pair table `0x0002FECF` (code, index; 0-code terminates) keyed on **ID[1]**; record = 3 bytes at `0x0002FEB4 + index*3`; `rec[1]` nibbles → `dev+0x10`/`dev+0x0C`(=1<<exp) and `dev+0x12`(=1<<n); `rec[0]` → `dev+0x15`/`dev+0x16`; `rec[2]` → `dev+0x18`/`dev+0x14`; `dev+0x19 = 0xD << dev[0x18]` for 1/2. **Override** from **ID[3]** when `dev[0x16]+dev[0x15] > 4` (unless ID[0]==0x98 with ID[1] in 0x83–0x87/0x93–0x97): `dev+0x0C = 0x400<<(ID[3]&3)`, `dev+0x10 = (ID[3]&3)+10`, `dev+0x12 = (0x10000<<((ID[3]>>4)&3)) >> dev+0x10`; plus an ID[0]==0x98 / `(ID[3]&0xF)>>2==2` case setting `dev+0x18=(ID[3]>>6)&3` and `dev+0x19=0x34`. | pool `0x4002F000`=`0x0002FECF`, base−0x1B = `0x0002FEB4` |

### 3.5 Boot media — image/descriptor open, command 8 (1)

| address | mode | size | inferred purpose | notable callees / globals / literals |
|---|---|---|---|---|
| `0x4002bc10` | thumb | 72 | **cmd-8 open (SDRAM/DDR boot)**: only when `*ctx==0`; zero a 4-word request, `0x4002A2CA(0x0002F9F8, req)` (EMIF/DDR descriptor), accept only `ret==0 && req[1]==2` (EMIF1, subchannel 1); then `ctx[2]=1`, `u8[+5]=1`, `u16[+0xC]=1`, `ctx[4]=0xC0000000` (DDR CS1), `u16[+0x14]=3`, `ctx[6]=0`, `ctx[0]=1`. | pool `0x4002BC78`=`0x0002F9F8`; `0x4002A2CA` |

### 3.6 Device abstraction — USB OTG device-mode per-driver and transfer engine (17)

Controller base for every row: pool `0x4002D2B0` = `0x4002D6B8` = **`0x4A0AB000`**.
Shared state: transfer context at `0x4030DBEC` (pool `0x4002D2BC`/`0x4002D6BC`/`0x4002DB2C`)
and device context at `0x4030DC38` (pool `0x4002D2B4`/`0x4002DB10`).

| address | mode | size | inferred purpose | notable callees / globals / literals |
|---|---|---|---|---|
| `0x4002bfd4` | thumb | 86 | **per-driver close for device 0x45 (USB)**: NULL → 6; `*0x4030DC38==0` (not powered) → 1; if the handle's `options(+0x10)` has bit 12 set and bit 13 clear, power the interface down via `0x4002CC6C(2, options)` and clear the control bit; then clear the transfer context words `+0x00/+0x04/+0x0C/+0x10/+0x14/+0x1C`, call `0x4002D1A4()`, progress bits `0x5C`/`0x5D`. | `0x4002C240`=`0x4030DC38`, `0x4002C244`=`0x4030DBEC`; `0x4002E0B6`, `0x4002CC6C`, `0x4002A51C` |
| `0x4002cf5a` | thumb | 110 | OTG bring-up / session control: wait `u32[0x408]`; `u32[0x414]=0`; `u32[0x404]=0x1008`; `u32[0x40C]` bit 0 ← (`param==0`), reject `param>1`. EP0 setup: `INTRTXE(0x06)\|=1`, `INTRUSBE(0x0B)=4` (bus-reset only), `INDEX(0x0E)=0`, FIFO sizes `[0x62]=[0x63]=3`, `u16[0x64]=u16[0x66]=0`, `POWER(0x01)\|=0x20`. | `0x4002D2B0` |
| `0x4002cfc8` | thumb | 20 | Enable bus-event interrupts: `u8[0x0B] \|= 0x03` (suspend/resume + reset). | |
| `0x4002cfdc` | thumb | 132 | Power-on: `u8[0x01] \|= 0x40`, `*0x4030DC38 = 1`, then wait for `((u8[0x60]&0x1F)>>3)==3` bounded by `transfer_ctx+0x24` ticks (constant `0x51E`); progress bit `0x26` on entry, `0x27` + return 0 on timeout. | `0x4002D2B8`=`0x4A304000`, `0x4002D2BC`, `0x4002A51C` |
| `0x4002d060` | thumb | 90 | FIFO push: copy `min(req[3], u16[0x110])` bytes from `req[2]` to the FIFO window `0x24` (word loop + byte tail), advance `req[2]`/`req[3]`. `u16[0x110]` is the per-call burst cap. | `0x4002D2B0` |
| `0x4002d0ba` | thumb | 108 | **Start TX** on endpoint `param_1`. Handle `xfer_mode(+0x14)==0` (FIFO): `INDEX(0x0E)=ep`, `u16[0x112] \|= 0x2000`, kick `0x4002D060` then `\|=1` (or `\|=8` when already running). Else indexed window A: `u32[0x204] = ep*0x10+6`, `u16[0x112] \|= 0xB400`, `u32[0x208]=buf`, `u32[0x20C]=len`, `u32[0x204] \|= 9`. | caller `0x4002C1CA` (write method); `0x4002D060` |
| `0x4002d126` | thumb | 126 | **Start RX** (mirror of the previous): window B `u32[0x214] = ep*0x10+4` (`&~2` then `\|=9`), `u32[0x218]=buf`, `u32[0x21C]=len`; FIFO mode sets `u16[0x116] \|= 0x8000` and clears `u16[0x112]` bit 13; the `0x9000`/`0xA800`/`0xB800` alternatives are selected by **POWER bit 5** (high-speed). | caller `0x4002C1FC` (read method) |
| `0x4002d1a4` | thumb | 118 | Disable/abort: clear `INTRTXE` bits 0–1, `INTRRXE(0x08)` bit 1, `INTRUSBE` bits 0–2; set `u16[0x102]` bit 8, `u16[0x112]` bit 3, `u16[0x116]` bit 4; zero `0x0E/0x62/0x63/u16 0x64/u16 0x66`; clear `POWER(0x01)` bit 6; zero the two context flag words and `0x4030DC38[3]/[4]`. | callers `0x4002C016` (close), `0x4002DB4C` |
| `0x4002d2c2` | thumb | 52 | Completion **A**: if `u16[0x112]` bit 5 set → clear it, status 1; else status = bit 0 set ? 1 : 0. Write `handle+0x1C` (status), then call `*(handle+0x04)` — the per-handle callback. | `0x4030DBEC` |
| `0x4002d2f6` | thumb | 84 | Completion/continuation **A**: if idle and `handle+0x0C` (remaining) ≠ 0 → FIFO push, `u16[0x112]\|=1`, status = **4 (continue, callback deferred)**; else status 0/1 + callback. | `0x4002D060` |
| `0x4002d34a` | thumb | 58 | FIFO pop: copy `param_2` bytes from the FIFO window `0x24` to a buffer (word loop + byte tail). | |
| `0x4002d384` | thumb | 136 | Completion **B** (single-shot): the peer context's `+0x1C` status must equal 4; if `u16[0x116]` bit 4 set → clear it; else when bit 0 is set and the residual count matches, pop the bytes, clear `[0x116]` bit 0 and `[0x214]` bits 0/3, status 0; otherwise set `[0x116]` bit 4 and status 1. | `0x4002D34A` |
| `0x4002d40c` | thumb | 146 | Completion **B** (chunked): when `(ctx[0x18]-ctx[0x14])+u16[0x118] <= ctx[0x1C]`, pop `u16[0x118]` bytes and advance `ctx[0x18]`; status 0 when the total matches, else 4. | `0x4002D34A` |
| `0x4002da34` | thumb | 108 | **Interrupt-driven transfer state machine**: on `state(ctx+0x10)==0` handle completion (`u16[0x102]` bit 0; when `ctx[4]==1` rewrite `FADDR(0x00)` from `ctx[3]`); else require `u16[0x108]!=0`, call `0x4002D9E0`, then dispatch on the request byte `ctx[7]` — types 0,1,3,5,6,8,9,10,11 → `0x4002D724/520/5D0/830/8FE/7B2/4FC/612`; `ctx[6]` bit 7 selects the flag variant (`0x20`/`0x40`/`0x48`/`0x60` writes to `u16[0x102]`), unknown type sets the error bit. | `0x4002DB10`=`0x4030DC38`, `0x4002DB14`=`0x4A0AB000`, `0x4002DB1C`=`0x4A0AB102` |
| `0x4002daa0` | thumb | 26 | Finish/abort notify: progress bit `0x2A`, then call the context's **second callback at `ctx+0x2C`** with 0. | `0x4002DB2C`=`0x4030DBEC` |
| `0x4002db8c` | thumb | 6 | Byte setter: `0x4030D108[0] = param`. | pool `0x4002DB9C`=`0x4030D108` |
| `0x4002db92` | thumb | 6 | Byte setter: `0x4030D108[1] = param`. | same |

### 3.7 Monitor services (SMC) (3)

| address | mode | size | inferred purpose | notable callees / globals / literals |
|---|---|---|---|---|
| `0x4002ce90` | thumb | 114 | Argument setup for the `0xF0` monitor service: accepts only selector `1`; if the request block `0x4030DC1C` has `type ∈ {1,2,3}`, dispatch — type 1 → `0x4002CDAA(0x45, …)` (device 0x45 = USB), type 2 → `0x4002CD1E(6, …)`, type 3 → `0x4002CD1E(4, …)`; otherwise return 0. | pool `0x4002CEA8` = **`0x4030DC1C`** |
| `0x4002ceb0` | arm | 32 | `0xF0` SMC veneer (`blx 0x4002CE90; mov r12,#0xF0; smc #0`) — **decompilation failed** (`bad instruction data`); body also overlaps `0x4002CEBC`. | `0x4002CE90` |
| `0x4002cebc` | thumb | 20 | Thumb resume after the SMC — **decompilation failed** the same way (the first bytes are the ARM half of the island, decoded as Thumb). | |

### 3.8 Control and interrupt helpers (6)

| address | mode | size | inferred purpose | notable callees / globals / literals |
|---|---|---|---|---|
| `0x4002e0a8` | thumb | 14 | Set bit 30 of `u32[0x4A100620]` (control-module window). | pool `0x4002E124`=`0x4A100620` |
| `0x4002e0b6` | thumb | 14 | Clear bit 30 of the same register. | same |
| `0x4002e0ce` | thumb | 16 | Test bit 13 of the same register. | same |
| `0x4002e0de` | thumb | 16 | Test bit 14 of the same register. | same |
| `0x4002e0fc` | thumb | 38 | One-way power-up sequence on `u32[0x4A00233C]`: `\|=1`, `\|=4`, `&=~8`, `\|=0x10` (no polling). | pool `0x4002E128`=`0x4A00233C` |
| `0x4002e202` | thumb | 22 | GIC **set-pending**: `u32[0x48241000 + 0x204 + (n>>5)*4] = 1<<(n&31)` = `GICD_ISPENDR` for **IRQ 32+n**; one-instruction "raise interrupt" wrapper. | pool `0x4002E250`=`0x48241000`; caller `0x4002B7F6` |

### 3.9 Cache / TLB maintenance (2)

| address | mode | size | inferred purpose | notable callees / globals / literals |
|---|---|---|---|---|
| `0x4002f310` | arm | 72 | Invalidate the whole D-cache by set/way: reads `CCSIDR` (`mrc c0,c0,1`), ways = `CCSIDR[12:3]` (associativity), sets = `CCSIDR[27:13]` shifted to the `DCISW` set field (`<<5`), loops `DCISW` (`mcr c7,c6,2`) with `way<<30`. | pool `0x4002F4A8`=`0x0FFFE000`; first CP15 write is rendered as `mcr p15,0,rX,c2,c0,0` — see §6.4 |
| `0x4002f384` | arm | 8 | `ICIALLU` (`mcr c7,c5,0`). | |

---

## 4. Corrections and refinements

Each item names the earlier statement it changes. "Pseudocode" = this dump.

**C1 — `10` §3.2, `0x4002ED48` row: wrong command.** `10` says it "sends
`0x2D3A0010` arg `idx<<24`". Pseudocode: the command word is a **different** literal,
`0x2B1B0000` (pool `0x4002EDA0`, INDX 0x2B = 43, RSP_TYPE 3 = 48-bit-with-busy, no
data), and the argument is `idx<<24`. So the vendor access splits into two commands:
index **45** (`0x2D3A0010`, data-read) for the register table and per-region flags, and
index **43** (`0x2B1B0000`, R1b) to *select* the region. `64` §2.3's observed-word table
lists only the index-45 words; `0x2B1B0000` is missing from it.

**C2 — `10` §3.2, `0x4002ED76` row: one region, not all.** `10` says it "calls
`0x4002ED48` for each set bit". Pseudocode: it scans `dev[0x45..0x4B]` (starting at
**1**, not 0) for the first byte `== 1` and calls `0x4002ED48` **once** (with 0 if none
matches).

**C3 — `10` §3.2 and `64` §2.3: CMD4's command word.** `10`'s command-word table and
`64`'s observed-word table both give CMD4 as `0x04040000`, and `64` infers that the ROM
writes TRM-"reserved" bit 18. Pseudocode at `0x4002EA5E`: the **command word** is
`0x04000000` (Ghidra renders it as `0x04040000 << 8`, a 32-bit truncation) and
`0x04040000` is the **argument** (DSR). There is no `0x04000000` word anywhere in the
image, so the shift is real code, not a literal. Consequence: the "reserved bit 18
anomaly" in `64` §2.3 is a field-extraction artefact — no reserved bit is written to
`MMCHS_CMD` for CMD4.

**C4 — `10` §3.2, `0x4002E844` row: the MMC capacity path uses EXT_CSD, not CSD.**
`10` says "MMC issues CMD9 and reads". Pseudocode: the MMC branch issues
**CMD8 = SEND_EXT_CSD** (`0x083A0010`, DP=1, DDIR=1), reads one 512-byte block, and
takes the word at **block offset 0xD4 = EXT_CSD `SEC_COUNT`** (Ghidra names the local
`iStack_14c`, exactly 0xD4 bytes past the 212-byte block buffer). The SD branch is
CSD-v2 `C_SIZE` (bits[69:48]) `<< 10` — with no `+1` term, i.e. capacity in KiB units,
which is also worth noting against the spec.

**C5 — `64` §2.3: do not read `0x4002D0BA`/`0x4002D126` as MMCHS.** They write 32-bit
words at `+0x204`, `+0x208`, `+0x20C` and `+0x214`, `+0x218`, `+0x21C`, which are exactly
`MMCHS_BLK/ARG/CMD` and `RSP10..76`. The base is **`0x4A0AB000`** (USB OTG), not an
MMCHS: `+0x204`/`+0x214` receive `ep*0x10 + {6,4}` (an indexed register selector) and
`+0x208`/`+0x218` receive a **buffer pointer**, not a command argument. `0x4002DDD4`
remains the only writer of a command word to `0x20C` in this image.

**C6 — `50` §1 / §2 and `60` §2: the "continuation of the device/boot body"
(`0x4002CEBC`–`0x4002F0BC`) is the USB OTG driver.** The range `0x4002CE90`–`0x4002D40C`
plus `0x4002DA34`–`0x4002DB92` is an OTG controller driver on base `0x4A0AB000` whose
only callers are the per-driver methods for device ID `0x45` (`0x4002C03F` init,
`0x4002C1A3` write, `0x4002C1D3` read, `0x4002BFD5` close, `0x4002C205` config — the
`'E'` entry of `10` §4, which `64` §1.4 correctly re-identifies as DEVICE_USB). Naming
this subsystem makes `50` §2b's "start known, purpose unknown" rows for
`0x4002CF5A…0x4002D40C` unnecessary.

**C7 — `50` §2a (`0x4002CE90` = "small helper `subs/cmp/blt/bx lr`") and `64` Part 3.**
`0x4002CE90` is not a generic helper: it is the **argument-setup half of monitor service
1**, reading the request block at **`0x4030DC1C`** (the IRAM block `64` Part 3 lists as
unobtainable) and branching on request types 1–3 to `0x4002CDAA(0x45,…)` /
`0x4002CD1E(6,…)` / `0x4002CD1E(4,…)`. The ARM veneer `0x4002CEB0` issues `smc #0`
with `r12 = 0xF0` *after* this call, so the caller-side argument structure for service
`0xF0` is now visible even though the secure side is not.

**C8 — `50` §2a (`0x40028990` "enable I-cache").** Confirmed and refined: the write is
`SCTLR |= 0x1800`, i.e. bit 12 (I-cache) **and bit 11 (branch prediction)**; nothing is
cleared, and the routine ends with a tail branch into the `0x4002882C` copy dispatcher.

**C9 — `50` §2.1/§2a (device-type test `bits[11:8] == 3`).** The reset path's gate (in
the `0x400288F8` body, address `0x400289FC` = `0x4A0022C4`) is
`(devtype & 0x700) == 0x300 && (devtype & 0x1F) == 0x1F` — a **10:8** mask, plus a new
low-5-bit condition — and then calls the ARM island `0x40028C00`. The `0x400280C0` gate
described in `50` §2.1 (mask `0x700`, `==3`) is unchanged.

**C10 — `50` §2a (CPU1 secondary: "poll `[0x48281400]` bit 9, read `+0x800` and
`+0x804`").** Refined: the base literal is `0x48281000` (pool `0x40028A0C`), so the
status is `+0x400` (bit 9), the handoff word `+0x800` = `0x48281800` and the target
pointer `+0x804`. The pseudocode also shows the exact shape: `wfe` loop → `smc` →
**loop until `[+0x800] != 0`** → indirect call through `[+0x804]`. The ROM therefore
jumps to an address supplied in the WakeupGen/SAR window, not to a fixed entry.

**C11 — `50` §2a (`0x40028834`: "per record call `0x40028870`/`0x40028898`").** The
choice of copy routine is **data-driven**: the record's 4th word *is* the function and
`0x4002882C` calls it indirectly (bit 0 chooses Thumb). Record 0 =
`{0x0002FFC0, 0x4030D000, 0x40, 0x00028870}`, so the first call is ARM `memcpy`; the
last record carries `0x00028898` (`memset`). Table bounds are computed from pool words
`0x7718`/`0x7758` (`0x4002FF80`..`0x4002FFC0`).

**C12 — `10` §5.1 (`0x4002E25C`) and §5.1 (`0x4002E6BA`).** `0x4002E25C` is confirmed
exactly (command `0x90` to `dev->cmd`, `0` to `dev->addr`, width from `dev+0x14`), with
two additions: it first calls **`0x40028718(0)`** (enable in the `0x50000000` block), and
the four ID bytes come from four consecutive reads of one data port (Ghidra dropped the
post-increment, printing the same address four times). `0x4002E6BA` is confirmed as
`reset → 0x4002EE94 → (optional reset) → 0x4002EF14`, but the control flow is sharper
than "optional": the generic-geometry routine is tried **first**, and the ID-table
routine is only a **fallback** when it fails.

**C13 — `10` §10.1 (clock-divider table semantics).** Partly resolved: the index field
is CSD `TRAN_SPEED` (CSD bits[103:96], i.e. the **low byte of response word 3**), split
per the SD spec into `TRANSFER_RATE_UNIT = bits[6:3]` → table row `M` and
`TIME_VALUE = bits[2:0]` → column `N`. The guard `N < 4 && M*8-8 < 0x71` in `0x4002E8CE`
is exactly the 15×4 table's extent (`15*8-8 = 0x70`), so it is a bounds check, not a
semantic test. What remains open is the physical meaning of the four columns (§6.6).

**C14 — `10` §10.5 / §5.2 (ID codes not matched).** Narrowed: `0x4002EF14` compares
the table's code byte against **ID[1]** and separately keys on **ID[0] == 0x98** with
ID[1] in `0x83..0x87`/`0x93..0x97` — `0x98` is the Toshiba JEDEC manufacturer code and
those device codes are its NAND family, so the "flash/NOR path" of `10` §5 is a
**NAND** path (`0x90` = READ ID, 4-byte ID stream, 512-byte page buffer at `+0x400`).
The nibble mapping of `10` §5.2 is confirmed unchanged.

**C15 — `64` §1.5 (`struct per_handle`, `+0x24 = config_timeout`).** Confirmed in use:
`0x4002CFDC` bounds its wait with the word at transfer-context `+0x24`. The same block
also holds a **second function pointer at `+0x2C`** (`0x4002DAA0` calls it with 0), which
the documented layout does not have — the layout is right but not complete for this
ROM's private use.

---

## 5. New behaviour learned

**N1 — The IRAM copy table is a 4-word record with an indirect function word
(`0x4002882C`).** `{src, dst, len, fn}` at `0x4002FF80`; `0x4002882C` loads record 0 and
calls `fn` indirectly with `(src, dst, len)` — record 0 is `{0x0002FFC0, 0x4030D000,
0x40, 0x00028870}` (ARM `memcpy`). The Thumb/ARM distinction is made by testing bit 0 of
`fn`. The end pointer is `0x4002FFC0`, which is also where the second vector table
starts, so the "boot" copy and the table are contiguous by construction.

**N2 — Reset chain, exactly (`0x400288F8` composite, `0x40028990`).** CPU1 branch:
`MIDR[1:0]!=0` → `wfe`; wait `[0x48281400] & 0x200`, `smc`, wait `[0x48281800] != 0`,
call `[0x48281804]`. CPU0 branch: device-type gate (see C9) → `VBAR = 0x00028000` via
`mcr p15,0,c2,c0,0` → reset reason from `0x4A307B04` recorded by `0x4002A4D8` → progress
bit 0 → `SCTLR |= 0x1800`. Notably, **no MMU enable and no CPSR write appear** in this
body; the stack (`0x4030D000`) and the Thumb handoff are loaded later by `0x400288D0`.

**N3 — A complete USB OTG (MUSB-class) device driver at `0x4A0AB000`, 17 seeded entries
(`0x4002CF5A`–`0x4002DB92`).** Structure recovered: init = bring-up `0x4002CF5A`
(OTG control `0x400/0x404/0x408/0x40C/0x414`, EP0 FIFO-size setup, `POWER |= 0x20`) →
`0x4002CFC8` (bus-event interrupts) → `0x4002CFDC` (power-on + bounded wait for
"configured", function bits at `+0x60` == 3). Two transfer paths selected by
`per_handle.xfer_mode (+0x14)`: a **CPU/FIFO** path (`0x4002D060` push, `0x4002D34A`
pop, burst capped by `u16[0x110]`) and an **indexed window** path with two register
windows, A = `select 0x204` + `0x208`/`0x20C` (TX, `0x4002D0BA`, called from the write
method `0x4002C1A3`) and B = `select 0x214` + `0x218`/`0x21C` (RX, `0x4002D126`, called
from the read method `0x4002C1D3`). `POWER` bit 5 (high speed) selects the `0x9000`/
`0xB800` interval encodings in the RX path.

**N4 — A uniform asynchronous per-driver status/callback ABI.** `0x4002D2C2`,
`0x4002D2F6`, `0x4002D384`, `0x4002D40C` all write a status into `handle+0x1C` and then
invoke `*(handle+0x04)`, with the value set: **0 = complete, 1 = error/idle, 4 =
"more to do, call me again"**. `4` suppresses the callback in `0x4002D2F6`/`0x4002D40C`,
so a caller can drain a transfer without re-entering its own completion routine. The
same `4` convention gates the state machine (`0x4002DA34` requires the peer's `+0x1C`
to be 4).

**N5 — The USB interrupt state machine is a 9-way request dispatch (`0x4002DA34`).**
Request byte at `ctx+7`, with `ctx+6` bit 7 selecting a flag variant; types
`0,1,3,5,6,8,9,10,11` map to `0x4002D724 / 0x4002D520 / 0x4002D5D0 / 0x4002D830 /
0x4002D8FE / 0x4002D7B2 / 0x4002D4FC / 0x4002D612`; an unknown type sets the error bits
`0x20`/`0x40` in `u16[0x102]`. The endpoint-0 status word is `u16[0x102]` (bits 0, 2, 4,
5 used), the count is `u16[0x108]`, and `FADDR(0x00)` is rewritten from `ctx[3]` when
`ctx[4]==1` — i.e. the ROM re-addresses the device after bus reset.

**N6 — USB close is a real power-down, not just a flag clear (`0x4002BFD4`).** For
device type `0x45` it (a) returns 6 on a NULL handle, (b) returns 1 if the device
context `*0x4030DC38` says "not powered", (c) if the handle's `options` bit 12 is set and
bit 13 clear, calls `0x4002CC6C(2, options)` — the big PMIC/pad/PRCM helper — to power
the interface down, and (d) clears the per-transfer context words
`+0x00/+0x04/+0x0C/+0x10/+0x14/+0x1C` and calls `0x4002D1A4()` (interrupt mask + abort).
Progress bits `0x5C`/`0x5D` bracket the operation.

**N7 — MMC identification has an explicit voltage failure path and a preset MMC RCA
(`0x4002E930`).** `dev+0x20` starts at `0x1234` (fixed MMC RCA); after the
ACMD41/CMD1 poll the ROM **fails the whole init when `(R3 & 0x00FF8000) == 0`** (no
supported OCR voltage window). Sector/byte mode is chosen by `R3[30:29] == 2` for MMC
(an equality test, not a bit test) and by CCS (bit 30) for SD. CMD2 is issued with the
**OCR word as its argument**, and the poll loop re-sends CMD55 before every ACMD41 retry.

**N8 — Capacity has two different sources per card type (`0x4002E844`).** SD: CSD v2
`C_SIZE` (bits[69:48]) `<< 10`. MMC: **EXT_CSD `SEC_COUNT` at byte offset 0xD4**, read
via one 512-byte CMD8 block, with `0x800000` substituted when the field is zero. This is
the first direct evidence for how the ROM sizes an eMMC.

**N9 — The vendor "region" protocol is three commands, not one (`0x4002ECA4`,
`0x4002ECE8`, `0x4002ED48`, `0x4002ED76`).** Index 45 with `arg = 0xA1000000` reads an
8-word region table into `dev+0x24..0x40`; index 45 with `arg = 0xB1000000 | (i<<16)`
reads one block per populated table entry and stores **bit 1 of block word at byte
0x110** into `dev[0x44+i]`; **index 43 (`0x2B1B0000`, R1b)** then selects exactly one
region (first flag `== 1`, scanning 1..7, 0 as fallback) and records it in `dev+0x4C`.

**N10 — Flash geometry has a second, ID[3]-driven fallback (`0x4002EF14`).** Beyond the
table lookup, the ROM overrides geometry from **ID[3]** when the record's two size
nibbles sum above 4: `dev+0x0C = 0x400 << (ID[3]&3)` (1–8 KiB page), `dev+0x10 =
(ID[3]&3) + 10`, `dev+0x12 = (0x10000 << ((ID[3]>>4)&3)) >> dev[0x10]` (blocks per
unit), and for `ID[0]==0x98` with `(ID[3]&0xF)>>2 == 2` it also sets `dev+0x18 =
(ID[3]>>6)&3`, `dev+0x19 = 0x34` (a 4-bit-wide bus variant). The exception list
(`ID[0]==0x98 && ID[1] ∈ 0x83..0x87/0x93..0x97`) means Toshiba's NAND codes are exempt
from the override — the ID table is trusted for them, the ID[3] heuristic for everything
else.

**N11 — Flash ID is cross-checked against a mirrored register pair (`0x4002B970`).**
After reading the `0x90`-command ID at `base+0`/`+2`, the ROM also reads the pair at
`base+0x1E000`/`+0x1E002` and returns success only when **both halves match** — a
plausible "bus/width sanity" step before geometry is trusted (both callers are the cmd-4
flash open).

**N12 — The `0x48078000` block is programmed by the cmd-3 media open (`0x4002DBA0`,
`0x4002DBB4`).** Reset (`SYSCONFIG|=2`, poll `SYSSTATUS[0]`), then a forced
`0x07FF0000` field plus `(param & 3)` into `u32[0x20]` and interrupt 0 enabled in
`u32[0x1C]`. Together with N10's Toshiba NAND decoding, this is the second half of a
NAND-class open path (block identity still open — §6.5).

**N13 — Small control helpers are single-bit accessors on two fixed registers
(`0x4002E0A8…0x4002E0FC`, `0x4002E202`).** `0x4A100620` bit 30 is set/cleared and bits
13/14 read; `0x4A00233C` gets a fixed four-step `|=1, |=4, &=~8, |=0x10` power-up with no
polling; `0x4002E202` is a one-instruction **GIC set-pending** helper
(`GICD_ISPENDR` for IRQ 32+n). All of their callers are inside the USB per-driver
methods, so they are the USB PHY/control-module glue rather than generic GPIO code.

---

## 6. Still uncertain

**6.1 Two entries never decompiled.** `0x4002CEB0` (ARM, 32 B) and `0x4002CEBC` (Thumb,
20 B) both produce `halt_baddata`. This is inherent: the ARM island
`0x4002CEB0`–`0x4002CEBB` (`blx`, `mov r12,#0xF0`, `smc #0`) is immediately followed by
Thumb code, and Ghidra assigns one mode per function, so each entry misdecodes the
other's bytes. Their semantics are therefore still taken from the byte-verified map
(`60` §4.4) and their Ghidra sizes (32 and 20) **overlap** — do not use these two bodies
for anything.

**6.2 Three bodies are structurally merged, not wrong.** `0x400288F8` (size 236) is the
4-byte Thumb veneer *plus* the ARM reset body of `0x40028904`, and `0x40028990`
(size 16) shares the `0x4002882C` dispatcher tail; `0x4002882C` (size 60) overlaps the
pre-existing `0x40028834` procedure. The content is consistent with the mode map, but
function *extents* in these four places are Ghidra artefacts and the "sizes" column above
must not be read as code ranges.

**6.3 Argument and timeout losses in untyped seeded functions.** Because the entries were
created outside any prototype, Ghidra drops register arguments and folds the tick
snapshot used for timeouts. Concrete symptoms: `0x4002E29E()` and `0x4002E708()` are
called with no arguments; `0x4002D34A` is called with one; `0x4002E724(param_1, param_3,
param_2)` at `0x4002E754` swaps its own parameter names; and four poll loops render as
`[T+0x10] - [T+0x10]` (`0x4002DDD4`, `0x4002CFDC`, `0x4002E930`, `0x4002CFDC`), i.e. the
saved start tick was eliminated. **Timeout durations cannot be recovered from this
dump**; only the constants survive (`0xccb`, `0x666`, `0x51e`, `0x7fff`, `0x8000`).
Call arities in the table above are marked as observed, not as truth.

**6.4 `0x4002F310`'s first instruction.** It decompiles as a CP15 write to `c2,c0,0`
(TTBR0). A TTBR0 write at the head of a D-cache invalidate is implausible; either it is
an opcode-attribution slip by Ghidra or the routine really does clear TTBR0 before
invalidating. Given `50` §2a's LLVM reading (DCISW + CCSIDR only), I treat the `c2` write
as an artefact but flag it here rather than silently dropping it.

**6.5 `0x48078000` and the flash register model.** The base is certain (pool
`0x4002DC58`), the register shape is the generic OMAP target-module layout
(`SYSCONFIG 0x10`, `SYSSTATUS 0x14`, `IRQSTATUS 0x18`, `IRQENABLE 0x1C`, control at
`0x20`), and the forced `0x07FF0000` field plus `(param & 3)` is undecoded. Related: the
flash command block (`+0x1E000`, `+0x1E200`, `+0x1E400`, `+0x1E440`, `+0x1E480`,
`+0x1E482`, `+0x10020`) plus the 512-byte buffer at `+0x400` sit **inside the caller's
flash base**, i.e. this is a memory-mapped device with an internal buffer/register
window, not a controller register file. A OneNAND-class part is the closest structural
match but is unverified; `10` §10.4's caveat stands.

**6.6 Clock-divider table columns.** With the index field now proven to be `TRAN_SPEED`
(C13), the row/column meanings are still unproven: the four columns decrease
monotonically across a row and down a column, consistent with divisor values for four
input-clock assumptions, but nothing in this dump names the input clock or the
`MMCHS.SYSCTL.CLKD` units the returned value is interpreted in.

**6.7 USB OTG sub-block and field names.** `0x4A0AB000` is the controller base and the
offsets are MUSB-shaped (byte registers at `0x00/0x01/0x06/0x0A/0x0B/0x0E`, FIFO window
at `0x24`, FIFO-size bytes at `0x62/0x63/0x66`, an OTG group at `0x400`–`0x414`, an
indexed per-endpoint group at `0x100`/`0x110`/`0x200`/`0x210`). Bit *names* are **not**
established: the meanings of `u16[0x102]` bits 0/2/4/5/8, `u16[0x112]` bits 0/3/10/12/13/15,
`u16[0x116]` bits 0/4/11..15, `u16[0x118]`, and `u16[0x110]` (assumed burst cap because
`0x4002D060` clamps to it) are inferred from usage only. Whether `0x4A0AB000` is the
MUSB core alone or the core plus its TI wrapper could not be settled from pseudocode.

**6.8 The `0x4030DBEC` context is a hybrid.** Some uses read it as a `per_handle`
(`+0x04` callback, `+0x0C` length/remaining, `+0x1C` status, `+0x24` timeout) and others
dereference `+0x10` as a **pointer** to a peer context. Either it is a union of
`per_handle` with the driver's private transfer descriptor (`+0x14` base, `+0x18`
current, `+0x1C` total, `+0x2C` second callback) or Ghidra mixed a pointer and a field
because `_DAT_4002D6BC` is used both as an address and as a value. Which of `+0x10`/
`+0x14`/`+0x18` is "options" vs "buffer" is therefore open; the ABI-level facts (status
enum, callback at `+0x04`, timeout at `+0x24`, second callback at `+0x2C`) are solid.

**6.9 Request-type semantics of the USB state machine.** `0x4002DA34` shows *where* each
request byte is dispatched, but the handlers (`0x4002D4FC`, `0x4002D520`, `0x4002D5D0`,
`0x4002D612`, `0x4002D724`, `0x4002D7B2`, `0x4002D830`, `0x4002D8FE`) were already
functions in the 218-procedure Ghidra set and are **not** part of this seeded dump, so the
meaning of types 0/1/3/5/6/8/9/10/11 is not established here.

**6.10 The `0x4030D108` state byte pair.** `0x4002DB8C`/`0x4002DB92` write
`0x4030D108[0]`/`[1]`; their only callers are in the USB device-0x45 config/close
methods, so they are USB state, but which two states (address? configuration?) is not
determined, and the bytes are IRAM-initialised data (part of the boot-time copy range),
so their reset values live in the `0x40030000`-area table.

**6.11 What this dump does not settle.** No conclusion here changes `64` Part 3: the
secure side of monitor services `0xF0`/`0xFE`/`0xF1` is still unobservable, and the
`0x40026F28`–`0x40027FFF` hole is still missing. C7 only adds the caller-side request
block address (`0x4030DC1C`) and its type field.

---

*Ghidra 12.1.4 headless (analyzeHeadless + Java post-script) output; this is NOT REA MCP
Evidence. Literal-pool values quoted above are plain 32-bit reads of the same image
(Python `struct`), used only to name the addresses the pseudocode references. All
prior-document citations refer to labels in `50-code-map.md`, `10-boot-media.md`,
`60-mode-map-resolution.md`, `63-ghidra-218-reconciliation.md` and
`64-rom-api-and-media-final.md`.*
