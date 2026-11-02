# 99 — eMMC access from the CRC-booted bare-metal payload

Track B of the PlayBook boot-RE effort. Serves `97-region-architecture-and-live-usb.md`
(one-shot flag + restore region 0 from region 1) and continues `91`/`95`/`96`.
Date: 2026-10-26.

**Read-only.** No USB uploads, no NVRAM/RPMB/eMMC/flash writes, no reboots, no device
contact of any kind. Everything below is static analysis of files already in the
workspace plus host-side (Ghidra/Capstone) computation. The test payload in §7 is a
**design**, not something that was run.

---

## 0. Verdict / summary

1. **The bootloader stage at `hd0` contains a complete OMAP4 MMCHS driver** — controller
   reset/clock/config, CMD0/1/2/3/4/5/6/7/8/9/10/12/13/15/16/17/18/22/23/24/25/27/28/
   29/30/35/36/37/38/41/42/45/50/51/52/53/55/56/57/58/62, PIO block read and block
   write, EXT_CSD parsing, and a full 59-entry opcode→`MMCHS_CMD` mapping table that is
   **byte-identical in encoding** to the one decoded from the boot ROM in `64` §2.3.
   Controller instance map in hd0: **1 → `0x4809C000` (MMCHS1), 2 → `0x480B4000`
   (MMCHS2)**. (PROVEN)
2. **Transfers are PIO/FIFO only.** Every command word in the driver's table has
   `DE = 0` (bit 0), so DMA is never enabled for the normal paths; data moves through
   `MMCHS_DATA` (`+0x220`) 128 words per 512-byte block. A DMA path exists
   (`ctx+0x1D0` gates it, and the sender then sets `DE`) but **no call site in the image
   sets `ctx+0x1D0`**, so it is dead on this build. (PROVEN for the table and the call
   sites; the DMA branch itself is read but untaken.)
3. **Calling the bootloader's MMC code from the payload is NOT safe.** Four independent
   reasons (§4): the driver's public entry points have **no caller anywhere in the
   image**; the driver's entire state lives in IRAM at **`0x40308AB8`–`0x40309140`**
   which is not part of the dumped image and not initialised by any reachable code; every
   command completes through a runtime **function pointer** (`ctx+0x1B4`) plus per-device
   step counters, so a stale pointer is a jump to garbage in SVC mode; and the only
   MMC-based image loader in the image (`0x800044BA`) targets a 512 KB TI layout at
   `0x80000000` and therefore **cannot be the code that put the OS image at
   `0x800FF800`** — the stage's image-load path is not in this dump at all.
4. **Therefore the payload must carry its own driver.** §5 gives a complete PIO design
   for MMCHS2 `0x480B4000` (CMD0/8/55/ACMD41/CMD1/2/3/9/16/17/24/13, 512-byte blocks,
   single and multi-block) with the exact register values taken from the TRM and
   cross-checked against the bootloader's own constants.
5. **Region 1 layout** (§6): backup of the sealed TabletOS IFS at region-1 offset
   `0x000000`, payload area at `0x01000000`, and a control block + scratch + ring in the
   reserved **last 1 MB** (`LBA 0x1FF800`–`0x1FFFFF`) which no boot path touches.
6. **2.75 MB restore copy** (§8): PIO single-block ≈ **1.6–3.5 s** for the round trip;
   PIO multi-block ≈ **0.7–1.4 s**; DMA would save < 20 % because the bus, not the CPU,
   is the limit at 4-bit/24 MHz. A 2.75 MB copy is well inside any watchdog budget as
   long as WDT2 is kicked between chunks.

Nothing here needs a device write; the single live unknown that would change the design
is listed in §9 (whether the MMCHS2 functional clock is still on at payload entry).

---

## 1. Provenance and method

| item | value |
|---|---|
| target | `/home/psyden/rea-work/bootdumps-2026-09-11/hd0head.bin` (`/dev/hd0` bytes 0x0–0x200000) |
| SHA-256 | `dda9d955003302adf1f0d41580c50c0c9035126487e7ac53793f4d38a3a23e3d` (re-verified this session, unchanged) |
| load base | `0x80000000`; **file offset == address − 0x80000000** |
| relocation | pre-relocation image linked at `0x7FFF7000` with a load-time **+0x9000** fixup on image-internal literals (`73`); peripheral/IRAM literals are **not** fixed up |
| tools | Ghidra 12.1.4 headless (`analyzeHeadless`, `ARM:LE:32:v7`, raw `BinaryLoader` @ `0x80000000`) with a TMode-seeding post-script; Capstone 5.0.7 (`CS_MODE_THUMB`, `skipdata`) cross-checks; `llvm-objdump` spot checks; TRM `swpu231ap.pdf.txt` for register semantics |
| mode discipline | the MMC driver is **Thumb-2 throughout** (`0x80004000`–`0x80011000`); the ARM islands of `71` §1 (`0x80009000` vectors, `0x80013770`, `0x800175DC`–`0x8001C87C`, `0x800193F4`+) were not touched except for one SMC veneer at `0x800043B4` |
| artifacts | `$S = $COMMANDCODE_SCRATCHPAD`; `$S/pbdis.py` (Capstone dumper), `$S/starts.py` (function-start sweep), `$S/calls.py` (caller finder), `$S/cmdreg.py` (MMCHS register-access byte scan), `$S/SeedCorrect.java`, `$S/seed2.txt`, `$S/seed-mmc.txt`, `$S/mmc-decompiled.txt` (130 KB, 173 functions), `$S/mmc2-decompiled.txt` (184 functions). Ghidra project dir kept at `analysis/ghidra-mmc-2026-10-26/` (no project data, `-deleteProject`). |
| nothing modified | `hd0head.bin` was only read; no annotations were written back |

**Evidence class.** This report is **not** REA MCP Evidence. Every statement is marked
**PROVEN** (byte-exact from the image / TRM) or **INFERRED** (consistent with the bytes,
not settled). Where a claim relies on `71`/`91`, that report's proof stands and is
referenced rather than repeated.

### 1.1 What the hd0 image actually contains (structure that matters here)

```
file offset       runtime addr       content
0x000000          0x80000000         MLO header: 3 named records
                                        {0x4000,0x4D70} "MLO"      -> 0x80004000..0x80008D70
                                        {0x0C00,0x32B8} "PRIMAPP"
                                        {0x0200,0x0960} "KEYS"
0x000200          0x80000200         "CertPK_", 0x0A48 "PK_SIG_INFO_ROM_PKC"
0x000C00          0x80000C00         "CertPPA"
0x004000          0x80004000         "CertISW" ... then **module A**: TI-derived bootstrap
0x004398          0x80004398         " OMAP4430 Bootstrap @"  (only copy of this string in any dump)
0x004000-0x008D70                     module A (MMCHS driver + MBR/partition loader + DDR init)
0x009000          0x80009000         ARM exception vector table (b 0x80019224 ...)
0x009148          0x80009148         "BOOTCTR_FCTRY"
0x00C000-0x011000                     module B: a **second instance of the same MMCHS driver**
0x014900          0x80014900         RIM stage boot main (Thumb) — the verifier/hand-off code of `71`/`91`
0x01D044          0x8001D044         metrics blob / end of stage code
0x020000+         0x80020000+        0xFF fill (file is padded to 2 MB)
```

**PROVEN:** the same driver source appears twice — e.g. `FUN_80006458` at `0x80006458`
and `FUN_8000E2EC` at `0x8000E2EC` are the same body; `FUN_80007268` ↔ `FUN_8000EFC0`
class; the ROM's command-sender/`CMD` word encoder in `64` §2.3 is reproduced here.
Both instances reference the **same** IRAM state area, so they are two loaded copies,
not two builds. `0x80004398`'s neighbour literal `0x403044BB` (= `0x40300000 + 0x44BB`,
i.e. the address of `FUN_800044BA` if the module were linked at `0x40300000`) suggests
module A is a **relocatable / IRAM-target bootstrap**; see §4.2.

---

## 2. The bootloader MMC map (functions, addresses, state)

### 2.1 Controller instance map and register map

| item | value | status |
|---|---|---|
| instance selector | `1` → `0x4809C000` (MMCHS1), `2` → `0x480B4000` (MMCHS2); any other value → NULL and a fault | **PROVEN** — 8 literal pairs `{0x4809C000,0x480B4000}` at `0x80007254`, `0x80007670`, `0x80007BC4`, `0x80007DDC`, `0x8000E890`, `0x8000ECB4`, `0x8000F25C`, `0x8000F4B4`, all used by the same `if (x==1) A; else if (x==2) B; else 0` idiom |
| base/offset form | register = base + imm12 in the `ldr/str` (no `0x480B4200`-style base literals exist) | **PROVEN** (byte scan `$S/cmdreg.py`) |
| MMCHS2 is the eMMC | the OS drives eMMC on MMCHS2 (`devb-mmcsd-winchester`, `97` §0) and hd0's driver is instance-parameterised, so hd0 alone does not pin the *boot* instance | **PROVEN** for the OS side; **INFERRED** that the boot device is also MMCHS2 |
| last write of `MMCHS_REV`/`CAPA` | no `0x2FC`/`0x240` access in the driver | **PROVEN** (absence) |

Register offsets confirmed in code (TRM §24.6, Table 24-58/59):

| off | reg | used by hd0 as | TRM |
|---|---|---|---|
| `0x100` | `MMCHS_SDMASA` (SDMA system address) | only the (dead) DMA branch touches +0x100 region in module B's DDR code, not MMC | — |
| `0x110`/`0x114` | `SYSCONFIG`/`SYSSTATUS` | **not used** (no MMCHS soft reset in hd0) | Tbl 24-67 |
| `0x12C` | `CON` | bit0 `INIT`, bit1 `INIT` stream (CMD0), bit5 `DW` (1 = 4-bit) | Tbl 24-75 |
| `0x204` | `BLK` | block size in `[11:0]`, block count in `[31:16]` for multi-block | Tbl 24-80 |
| `0x208` | `ARG` | command argument | Tbl 24-82 |
| `0x20C` | `CMD` | command word (see §2.3) | Tbl 24-84 |
| `0x210`–`0x21C` | `RSP10/32/54/76` | response, 1 or 4 words per `RSP_TYPE` | Tbl 24-86 |
| `0x220` | `DATA` | PIO FIFO, word access | Tbl 24-94 |
| `0x224` | `PSTATE` | `CMDInhibit` (bit0) / `DATInhibit` (bit1) wait | Tbl 24-96 |
| `0x228` | `HCTL` | bit1 `DTW` (bus width), bit8 `SDBP` | Tbl 24-98 |
| `0x22C` | `SYSCTL` | `CLKD[15:6]`, `DTO[19:16]`, `CEN` bit2, `ICS` bit1, `SRC` bit25, `SRD` bit26 | Tbl 24-100 |
| `0x230` | `STAT` | write-1-clear; bit0 `CC`, bit1 `TC`, bit4 `BWR`, bit5 `BRR`, 16–23 error class, 24–28 more | Tbl 24-102 |
| `0x234` | `IE` | same bit layout as STAT; driver writes `0x33FF00xx` | Tbl 24-104 |
| `0x238` | `ISE` | not used | Tbl 24-106 |

### 2.2 Function map

All addresses are **runtime** addresses in the `0x8000xxxx` window as stored in the file
(see §4.2 for why the *executed* address may differ). "A" = module A
(`0x80004xxx`–`0x80008xxx`), "B" = module B (`0x8000e2xx`–`0x80010xxx`).

| A | B | role | calling convention (PROVEN from the decompiler) |
|---|---|---|---|
| `0x800044BA` | — | **OS/partition image loader**: DDR init → MMC open → read MBR → parse 16 partition entries → read `[size, 0x80)` 4 KB units to `0x80000000` → magic compare. **No caller.** | `void f(void)` |
| `0x800043DC` | — | MBR read + partition-table walk, returns the largest partition end rounded to 4 KB | `int f(u32 *size_out)` |
| `0x8000458E` | — | same partition-table walk on a caller buffer | `int f(void *buf, u32, u32 *size_out)` |
| `0x8000440E` | — | installs the driver's IRQ handlers (a table of u16 IRQ numbers at `0x80004524`+) | `void f(u32, u32 *)` |
| `0x80004CE0` | — | **block READ** top-level: bounds-check, then up to 5 retries of the transfer | `int f(dev /*0..2*/, u32 start, u32 count, void *buf)` — start/count in **4 KB units** |
| `0x80004D6A` | — | **block WRITE** top-level, identical shape | `int f(dev, u32 start, u32 count, void *buf)` |
| `0x80004DF4` | — | capacity query | `int f(dev, u32 *units_out, desc)` |
| `0x80004F0C` | — | **device open** (clock/power/CMD0/card-init/EXT_CSD) | `int f(ctrl, int dev, int *dev_out, u32)` |
| `0x80005256` | — | read thunk → `0x80005E9E` | 6 args |
| `0x8000526E` | — | write thunk → `0x800060B4` | 6 args |
| `0x80005E9E` | — | READ wrapper: lock, transfer, wait for `dev+0xBC`, unlock | `int f(start, buf, count, 0, dev, ctrl)` |
| `0x800060B4` | — | WRITE wrapper (same, with reliable-write flag) | 7 args |
| `0x80005D66` | — | READ core: CMD23 (`0x39`) if multi, arm FIFO ctx, CMD17 (`0xf`) / CMD18 (`0x10`) | `int f(start, buf, count, 0, dev, xfer_class)` |
| `0x80005F48` | — | WRITE core: CMD23 (`0x39`, arg `count\|0x80000000`) then CMD24 (`0x11`) / CMD25 (`0x12`) | `int f(start, buf, count, 0, dev, xfer_class, reliable)` |
| `0x800055BA` | `0x8000D44E` | **command issue** wrapper: picks response/interrupt class 3/4/5, stores count/ptr in the device record, calls the sender, waits on the per-device step counter, returns 0/1 | `int f(op, arg, count, resp_ptr, extra, ctrl)` |
| `0x80005568` / `0x8000559C` | — | the only caller of the sender; binds `ctrl` to the device record's `+4` | — |
| `0x80006E7E` | `0x8000E1xx` | **command sender / sequencer**: waits `PSTATE`, writes `CON.INIT` for CMD0/1/2/3/30, computes `BLK` for data ops, sets `CON.DW`, writes `STAT=0xFFFFFFFF` clear, `ARG`, `CMD`, then dispatches completion through `ctx+0x1B4` | `void f(op, arg, count, resp, extra, extra2, ctrl)` — `ctrl` is the **7th** argument (on the stack, `[sp,#0x38]`) |
| `0x80007196` | `0x8000EDxx` | copies `RSP10..RSP76` (1 or 4 words) to the caller's buffer, then checks `STAT & 0xE0000` and `ctx` type | `int f(mode, u32 *out, ctrl)` |
| `0x80007268` | `0x8000EFC0` | **arm FIFO context for READ direction**: `ctx+0x1C0 = buf`, `+0x1C4 = len`, `+0x1C8 = 0`, and if not DMA `IE |= 0x33FF0022` (bit1 `TC`, bit5 `BRR`) | `void f(buf, len, ctrl)` |
| `0x800072BE` | `0x8000F01x` | same for **WRITE** direction: `IE |= 0x33FF0012` (bit1 `TC`, bit4 `BWR`) | `void f(buf, len, ctrl)` |
| `0x8000731C` (entered via `0x80007316`) | `0x8000F07x` | **PIO read pump**: up to 0x200 bytes per call from `+0x220`, word loop + byte tail, advances `ctx+0x1C8`; called from the status service at `0x8000771A` | `void f(ctrl, , dev)` |
| `0x80007D22` (entered via `0x80007D1C`) | `0x8000FAxx` | **PIO write pump**: same shape, writes `+0x220`; called from the status service at `0x80007768` | `void f(ctrl, , dev)` |
| `0x80007858` | `0x8000F5xx` | **CMD0 / go-idle**: `CON \|= 2`, `CMD = 0`, poll `STAT.CC`, `STAT = 1`, `CON &= ~2`, `STAT = 0xFFFFFFFF` | `void f(ctrl)` |
| `0x80007930` | `0x8000F6xx` | **clock select**: `SYSCTL &= ~4` (CEN off), `CLKD = {0x0F0, 4, 2}[sel]`, poll `ICS`, `SYSCTL \|= 4` (CEN on) | `void f(ctrl, sel)` sel 0 = init (200 kHz), 1 = 12 MHz, 2 = 24 MHz (**INFERRED** from `CLKD` against a 96 MHz functional clock; see §5.6) |
| `0x800073A6` | `0x8000FExx` | **status/error service**: reads `STAT`, write-1-clears `0x600000`, `0xE0000`, `0x110000`, `0x11800000`… masks the matching `IE` bits, does `SYSCTL.SRC/SRD` soft resets, invokes `ctx+0x1B4` callback and pumps the FIFO on `CC`/`BRR`/`BWR`. Used both as the GIC handler and as the **poll** step inside the wait loops | `int f(ctrl)` |
| `0x80006E04` / `0x80006E1C` | `0x8000E340`/`358` | blocking wait: `while (*counter < 1) FUN_800073A6(2); (*counter)--` | `void f(int *counter)` |
| `0x800067C4` | — | **card init part 1**: CMD0 (`op 0x00`) → CMD1 (`op 0x01`) poll loop with OCR from `0x800069A0` → CMD2 (`op 0x02`) → CID into `dev+0x164` | `int f(dev)` |
| `0x8000687A` | — | card init part 2: EXT_CSD read (`op 0x2D` = CMD8 with data) → bus-width / HS_TIMING from EXT_CSD, `OP 0x2E` = CMD6 SWITCH, `SYSCTL` re-clock | `int f(dev, buf, x, y, z)` |
| `0x80005CAA` | — | **single command with a data buffer** (the primitive used by EXT_CSD reads): sets `ctx+0x1D0 = dma`, arms the FIFO, sends, waits on `dev+0xBC` | `int f(dev, op, buf, len, dma)` |
| `0x800056EC` | — | bus/clock re-arm before a transfer | — |
| `0x80005784` | — | CMD16 (`op 0x0E`) set block length 0x200 | — |
| `0x80006C3A` | — | set eMMC bus width via EXT_CSD `BUS_WIDTH` (CMD6 `op 0x2E`, arg `0x03B7_00nn`) | — |
| `0x8000819A`/`0x800082A6`/`0x800083A2`/`0x800080D0` | — | **DDR/EMIF/DMM init** (`0x4C000000`, `0x4D000000`, `0x4E000040`), i.e. module A re-initialises the memory controller before loading | — |
| `0x80008A3C` | — | delay: spin on a count (units of the 32 kHz-class timer, see `0x4A304000` in `10` §2.1) | `void f(n)` |
| `0x800043B4` | — | `mcr p15,…c7,c5,6` (BPIALL) + `smc #1` — an ARM/Thumb SMC veneer, **no caller** | — |

Because the status service itself drives the FIFO pumps (`0x8000771A` read, `0x80007768`
write), the whole transfer is completed by **polling `0x800073A6`**, so the driver *can*
run with IRQ/FIQ masked and needs no ISR. That removes one objection against reuse and
leaves §4.2 (unprovable IRAM state) and §4.1 (no reachable entry) as the decisive ones.

### 2.3 The command table (the single most useful artifact)

At **image `0x80008AB8`, 59 entries × 8 bytes**, `{u32 mmc_index_and_mask, u32 cmd_word_high}`;
the sender computes `cmd_word = (word0 & 0x3F) << 24 | word1`. The pointer the code uses is
`0x40308AB8` (IRAM, see §4.2). Decoded (PROVEN, byte-exact):

| op | MMC | cmd word | op | MMC | cmd word |
|---|---|---|---|---|---|
| `0x00` | CMD0 | `0x00000000` | `0x1F` | CMD42 | `0x2A3A0000` |
| `0x01` | CMD1 | `0x01020000` | `0x20` | CMD52 | `0x341A0000` |
| `0x02` | CMD2 | `0x02090000` | `0x21` | CMD53 | `0x353A0000` |
| `0x03` | CMD3 | `0x031A0000` | `0x22` | CMD55 | `0x371A0000` |
| `0x04` | CMD4 | `0x04000000` | `0x23` | CMD56 | `0x38020000` |
| `0x05` | CMD5 | `0x05020000` | `0x24` | CMD58 | `0x3A020000` |
| `0x06` | CMD6 | `0x063A0010` (data read) | `0x25` | CMD59 | `0x3B020000` |
| `0x07` | CMD7 | `0x071A0000` | `0x26` | CMD6 | `0x061A0000` |
| `0x08` | CMD8 | `0x081A0000` (SEND_IF_COND) | `0x27` | CMD13 | `0x0D3A0010` (status w/ data) |
| `0x09` | CMD9 | `0x09090000` | `0x28` | CMD22 | `0x163A0000` |
| `0x0A` | CMD10 | `0x0A090000` | `0x29` | CMD23 | `0x171A0000` |
| `0x0B` | CMD12 | `0x0C030000` (R1b) | `0x2A` | **CMD41/ACMD41** | `0x29020000` |
| `0x0C` | CMD13 | `0x0D1A0000` | `0x2B` | CMD42 | `0x2A020000` |
| `0x0D` | CMD15 | `0x0F000000` | `0x2C` | CMD51 | `0x333A0010` |
| `0x0E` | **CMD16** | `0x101A0000` | `0x2D` | **CMD8 + data** | `0x083A0010` (**EXT_CSD read**) |
| `0x0F` | **CMD17** | `0x113A0010` | `0x2E` | **CMD6** | `0x061B0000` (SWITCH, R1b) |
| `0x10` | **CMD18** | `0x123A0036` (MSBS+ACEN+BCE) | `0x2F`/`0x30` | CMD35/36 | `0x23020000`/`0x24020000` |
| `0x11` | **CMD24** | `0x183A0000` | `0x31` | CMD34 | `0x223B0032` |
| `0x12` | **CMD25** | `0x193A0026` (MSBS+ACEN+BCE) | `0x32` | CMD35 | `0x233B0022` |
| `0x13` | CMD27 | `0x1B020000` | `0x33` | CMD36 | `0x243A0010` |
| `0x14` | CMD28 | `0x1C1B0000` | `0x34` | CMD37 | `0x251B0000` |
| `0x15` | CMD29 | `0x1D1B0000` | `0x35` | CMD50 | `0x323B0032` |
| `0x16` | CMD30 | `0x1E3A0000` | `0x36` | CMD57 | `0x393B0022` |
| `0x17` | CMD32 | `0x201A0000` | `0x37` | CMD62 | `0x3E1B0000` |
| `0x18` | CMD33 | `0x211A0000` | `0x38` | CMD5 | `0x051B0000` |
| `0x19` | CMD34 | `0x22020000` | `0x39` | **CMD23** | `0x171A0000` (SET_BLOCK_COUNT) |
| `0x1A` | CMD35 | `0x23020000` | `0x3A` | CMD31 | `0x1F3A0010` |
| `0x1B` | CMD36 | `0x24020000` | | | |
| `0x1C` | CMD37 | `0x25020000` | | | |
| `0x1D` | CMD38 | `0x261B0000` | | | |
| `0x1E` | CMD40 | `0x28000000` | | | |

Flag bits are exactly the TRM Table 24-84 ones decoded in `64` §2.3: `[29:24] INDX`,
`[21] DP`, `[20] CICE`, `[19] CCCE`, `[17:16] RSP_TYPE` (1 = 136-bit), `[5] MSBS`,
`[4] DDIR` (1 = read), `[2] ACEN`, `[1] BCE`, `[0] DE`. **`DE = 0` in every entry.**

Two corrections this table forces on the ROM-side reading in `10`/`64`:
* the ROM's "index 45 with args `0xA1000000`/`0xB1000000`" is the **SD-only** vendor read
  (`op 0x2D` here is `CMD8`+data = EXT_CSD, a *different* command on the MMC path) —
  hd0 uses `op 0x2D` for EXT_CSD, which matches `68`'s CMD8/EXT_CSD finding;
* `CMD23` is issued **before** multi-block reads and writes, with arg `count` (read) or
  `count | 0x80000000` (reliable write) — that is the correct eMMC idiom.

### 2.4 State the driver keeps (all in IRAM)

| address | contents | status |
|---|---|---|
| `0x40308AB8` | 59-entry command table (§2.3), 0x1D8 bytes | **PROVEN** (pointer literal at `0x80007250`; the same bytes are in the file at `0x80008AB8`) |
| `0x40308C90`, `0x40308CFC`, `0x40308D14`, `0x40308D34`, `0x40308D38`, `0x40308D54`, `0x40308D64`, `0x40308D68`, `0x40308D6C`, `0x40308D70` | misc driver tables / IRQ registration records | **PROVEN** (literal refs), contents **INFERRED** |
| `0x40308E20`, `0x40308EA0` | per-controller descriptors | INFERRED |
| `0x40308EC0` + `ctrl*0x24` | **per-controller record** (3 entries × 0x24): `+0x18` → transfer context, `+0x20` = block size | **PROVEN** (7 literal refs, stride `0x24`) |
| `0x40308F30` + `dev*0x1F0` | **per-device record**, 0x1F0 bytes: `+0x00` dev index, `+0x04` **controller instance (1 or 2)**, `+0x08` 2 = block-addressed, `+0x10` step counter, `+0x18` → inner, `+0x1BC` open flag, `+0x1CC` RCA, `+0x1E4/0x1E8` transfer ptr/len, `+0x1E4..` ext | **PROVEN** (8 literal refs + all field uses) |
| `0x40309120`, `0x40309140` | device-index table / constant pointers | INFERRED |
| transfer context `*(rec+0x18)` | `+0x10` transfer class (3/4/5/12), `+0x1AC` flag, `+0x1B4` **completion callback**, `+0x1C0` buffer, `+0x1C4` length, `+0x1C8` progress, `+0x1D0` **DMA enable**, `+0x1DC` flag | **PROVEN** |

---

## 3. Is the OS image really loaded by this code?

Short answer: **no** — and this is the most important negative result of the track.

* `0x800044BA` is the only "load an image from MMC" routine in the image. It does
  `read unit 0 → 0x80000000`, walks 16 × 0x20-byte partition entries at `0x80000000`,
  then reads units `[size, 0x80)` (**512 KB window**) to `0x80000000`. The PlayBook OS
  image lives at region-0 byte `0x100000` and runs to `0x2BFE10`, i.e. a 2.75 MB window
  at a 1 MB offset — **not** a 512 KB window at offset 0. (PROVEN from the code and from
  `95` §1.)
* `0x800044BA` has **no caller** — not by `bl`, not by `blx rN` with a loadable literal,
  and no literal `0x800044BB` / `0x8000D4BB` / `0x800044BA` exists anywhere in the file
  (PROVEN by scan). The same is true of the driver's public read/write entries
  `0x80004CE0` (2 callers, both inside module A's own `0x800043xx`/`0x800044xx`) and
  `0x80004D6A` (**0** callers).
* Consequently the 2.75 MB image at `0x800FF800` was **not** placed there by any code in
  `hd0head.bin`. The candidates are the boot ROM (its `cmd 8`/media path, `64` §2.1) or a
  component outside this dump. **Residual unknown U1** (§9).

Practical consequence for this track: *there is no proven "the bootloader's MMC read
function" to call.* The reference implementation is there, and it is excellent as a
**specification oracle** (that is what §5 is built from), but it is not a callable
service.

---

## 4. Callability from the payload

**Verdict: do not call it. Write the driver.** The four reasons are independent, and each
is sufficient on its own.

### 4.1 No reachable entry point
Both the driver's public API and its image loader have no caller and no literal function
pointer in the image (§3). Whatever called them at build time is not in this dump, so
there is no evidence of what the ABI/arguments are at run time, and no evidence that the
state they expect was ever set up.

### 4.2 The IRAM state is not in the image and cannot be validated
Every entry point dereferences IRAM at `0x40308AB8`–`0x40309140` (§2.4): the command
table, the per-controller records (stride `0x24`), the per-device records (stride
`0x1F0`), the step counters, and the **function-pointer callbacks** at `ctx+0x1B4`.
None of that is in `hd0head.bin` (the file's bytes at those offsets are ordinary code —
e.g. `0x80009148` is the string `BOOTCTR_FCTRY`), and **no code path in the image writes
it**. A payload cannot know whether it is initialised, whether a *different* build wrote
it, or whether QNX/the stage has since reused that IRAM. Two further aggravations:

* the ROM's own IRAM footprint is `0x40300000`, `0x40304000`, `0x4030BFFF`,
  `0x4030D000`–`0x4030DCxx` (PROVEN by literal scan of the ROM dump) — so `0x40308Axx`
  is **not** the ROM's, i.e. this is hd0's own scratch and nobody else maintains it;
* module A's image loader is inconsistent with executing at `0x8000xxxx`: it uses
  `0x80000000` as a **destination buffer**, and the literal next to `" OMAP4430
  Bootstrap @"` is `0x403044BB` = `0x40300000 + 0x44BB` = `FUN_800044BA` **as if the
  module were linked at `0x40300000`**. So module A is a relocatable/IRAM-target TI
  bootstrap whose link address is not where it sits in the file. A payload calling
  `0x80004CE0` would be calling whatever the stage happens to have at that address, with
  a completely different register/state convention. (PROVEN literals; the "relocatable
  module" reading is **INFERRED**.)

### 4.3 The completion protocol still needs the runtime state
The driver also installs GIC handlers (`0x8000440E` registers the IRQ list at
`0x80004524`+) and completes every command through a **callback** (`ctx+0x1B4`, invoked
from the status service) plus per-device step counters (`dev+0xB8`, `dev+0xBC`), with the
wait loops polling the status service (`while (*c < 1) FUN_800073A6(2)`). So the *polling*
half works with `CPSR.I/F = 1` (§2.2 note) — but only if the callback pointer and the
counters are valid, which §4.2 says cannot be assumed. A stale callback pointer is a
branch to garbage in SVC mode with the MMU off: an unrecoverable hang before a single
breadcrumb is written, i.e. exactly the failure mode the one-shot design cannot recover
from without a spare unit.

### 4.4 The other payload-entry hazards from `91` apply unchanged
MMU off (fine — the driver uses physical addresses), WDT2 counting (the driver's
`FUN_80008A3C` delay loops are not watchdog-aware: the `0x8000`-tick busy-poll in
`seeded_800055ba`/`FUN_8000565C` can spin for seconds), caches possibly on (the driver
does no cache maintenance, and its buffers would be `ctx+0x1C0` — a payload-supplied
address; with D-cache on and no clean, the card would see stale data), the bootloader's
own stack, and PRCM/CORE clock state we cannot re-establish (see §5.6).

**If a future track still wants to try**, the minimum experiment is a *read-only* one:
publish a breadcrumb, `ldr` `0x40308AB8` and compare it with the 59-entry signature, and
only then consider `0x80004CE0(0, 0x100, 1, 0x84000000)` (read 4 KB at region-0 offset
0x100000 into free DRAM). Any mismatch → park. Do not attempt a write.

---

## 5. Standalone minimal MMC2 driver (the recommended path)

Target: **MMCHS2 `0x480B4000`** (TRM Table 24-56: MMCHS1 `0x4809C000`, MMCHS2 `0x480B4000`,
MMCHS3 `0x480AD000`, MMCHS4 `0x480D1000`, MMCHS5 `0x480D5000`). PIO only, 512-byte blocks,
ARM state, MMU off, IRQ/FIQ masked. All register semantics from TRM §24.6 (tables as
cited in §2.1); all command words from hd0's own table (§2.3) so they are byte-identical
to what the shipping bootloader uses.

### 5.1 Register base and helper macros

```
MMC2   = 0x480B4000
SCONF  = MMC2+0x110   SSTAT = MMC2+0x114   CON  = MMC2+0x12C
BLK    = MMC2+0x204   ARG   = MMC2+0x208   CMD  = MMC2+0x20C
RSP10  = MMC2+0x210   RSP32 = MMC2+0x214   RSP54= MMC2+0x218  RSP76= MMC2+0x21C
DATA   = MMC2+0x220   PSTATE= MMC2+0x224   HCTL = MMC2+0x228
SYSCTL = MMC2+0x22C   STAT  = MMC2+0x230   IE   = MMC2+0x234   ISE= MMC2+0x238
CAPA   = MMC2+0x240   CUR_CAPA = MMC2+0x248  REV = MMC2+0x2FC
```

`STAT`/`IE` bits used here: `CC`=0, `TC`=1, `BWR`=4 (host→card FIFO ready),
`BRR`=5 (card→host FIFO ready), errors `CTO`=16, `CCRC`=17, `CEB`=18, `CIE`=19,
`DTO`=20, `DCRC`=21, `DEB`=22, `CERR`=23. All `STAT` bits are write-1-to-clear.

### 5.2 Init sequence

```
; 0. controller soft reset (idempotent, safe with or without a prior bootloader init)
   ;    NOTE: SOFTRESET only completes if the MMCHS2 functional clock is running.
   ;    The loop below is bounded on purpose: if it expires, the clock is off and the
   ;    payload must park with 0xBAD1_0010 (this is residual unknown U2).
   SCONF = 0x0002                     ; SOFTRESET
   while (SSTAT & 1) == 0 ;           ; reset done          (bounded loop!)
   SCONF = 0x0000                     ; no idle, no wakeup  (hd0 never sets AUTOIDLE)
   HCTL  = 0x00000000                 ; 1-bit bus, SDBP=0
   CON   = 0x00000000                 ; INIT=0, DW=0
   IE    = 0x00000000                 ; no interrupts (we are masked anyway)
   ISE   = 0x00000000
   STAT  = 0xFFFFFFFF                 ; clear everything
   SYSCTL= (2<<16)                    ; DTO = 2  (~1 s at the init clock)
   SYSCTL= (SYSCTL & ~0xFC0) | (0xF0<<6)   ; CLKD = 0xF0  -> ~200 kHz
   SYSCTL|= (1<<0)                    ; ICE = 1 (internal clock enable)
   while (SYSCTL & 2) == 0 ;          ; ICS
   SYSCTL|= (1<<2)                    ; CEN  (this is exactly hd0's 0x80007930 sel 0)
   BLK   = 512                        ; [11:0] (hd0 writes 0x200 here too)

; 1. CMD0  GO_IDLE_STATE  -- the "CON.INIT stream" idiom from hd0 0x80007858
   CON  |= 2 ; CMD = 0x00000000 ; STAT = 1 ; CON &= ~2 ; STAT = 0xFFFFFFFF
   wait STAT.CC, clear it

; 2. CMD1  SEND_OP_COND  (MMC/eMMC: no CMD55/ACMD41 needed; eMMC answers CMD1)
   arg = 0x40FF8000                  ; HCS(30)=1, S18R(24)=1, volt 3.2-3.3/3.3-3.4
   do { send CMD1 (0x01020000, R3/no CRC), delay ~1 ms } while (!(RSP10 & 0x80000000))
   ;  <= 1 s budget, cap the retries at 1000 (hd0 uses 0x100 = 256)

; 3. CMD2  ALL_SEND_CID  0x02090000  R2 (136-bit) -> RSP76..RSP10
; 4. CMD3  SEND_RELATIVE_ADDR 0x031A0000, arg = RCA<<16. Use RCA = 0x0001
;     (hd0 fixes 0x1234; either is fine, remember it)
; 5. CMD9  SEND_CSD 0x09090000 arg = RCA<<16, R2 -> read TRAN_SPEED if you want the
;     exact clock; not required (see 5.6)
; 6. CMD7  SELECT_CARD 0x071A0000 arg = RCA<<16
; 7. CMD16 SET_BLOCKLEN 0x101A0000 arg = 512
; 8. CMD8  SEND_EXT_CSD 0x083A0010  DP=1 DDIR=1 (read) 512 bytes
;     -> EXT_CSD[212] (SEC_COUNT, 4 bytes LE) = card capacity in 512-byte sectors
;     -> EXT_CSD[196] (POWER_CLASS), [185] HS_TIMING, [183] BUS_WIDTH
;     sanity-check SEC_COUNT == 0x03A00000 for a 64 GB unit / 0x01D00000 for 32 GB
;     (accept 0x03A00000, 0x01D00000, or >= 0x01000000; log the raw value)
; 9. optional speed: CMD6 SWITCH (0x061B0000, R1b) arg 0x03B90100 (HS_TIMING=1)
;    then SYSCTL.CLKD = 4  (12 MHz) or 2 (24 MHz)  -- keep 12 MHz first
;10. bus width: CMD6 SWITCH arg 0x03B70100 (BUS_WIDTH=1 -> 4 bit)
;    then CON |= (1<<5) (DW=1) and HCTL |= (1<<1) (DTW=1)
;    (hd0 does the same pair; see 0x80006C3A and the CON/HCTL writes at 0x80006E7E)
```

Every step publishes a breadcrumb and kicks WDT2 (§7). Steps 2–10 are all bounded loops;
**never** write an unbounded `while (!(STAT & x));` with the watchdog running.

### 5.3 Single-block read (CMD17)

```
; precondition: card selected (CMD7), BLK = 512
   STAT  = 0xFFFFFFFF                       ; clear stale status
   IE    = 0x00000022                       ; optional; unused in polled mode
   ARG   = lba                              ; **512-byte-sector number**
   BLK   = 512
   CMD   = 0x113A0010                       ; CMD17, DP, CICE, CCCE, RSP48, DDIR=read
   wait STAT.CC  (clear it)   -> on CTO/CCRC: abort, return ERR_CMD
   wait STAT.BRR (bit5)       -> on DTO/DCRC/DEB: STAT = 0xFFFFFFFF, return ERR_DAT
   for i in 0..127:  word[i] = DATA         ; 512 bytes = 128 word reads
   wait STAT.TC  (bit1), then STAT = 0xFFFFFFFF
   compare the echoed data against expectation, return OK
```

### 5.4 Single-block write (CMD24) and multi-block

```
WRITE (CMD24 = 0x183A0000, DDIR=0):
   STAT = 0xFFFFFFFF
   ARG = lba ; BLK = 512 ; CMD = 0x183A0000
   wait CC ; wait STAT.BWR (bit4) ; for i in 0..127: DATA = word[i]
   wait TC ; STAT = 0xFFFFFFFF
   ; then verify: read the LBA back with CMD17 and memcmp  (mandatory for a test payload)

MULTI-READ (CMD18) :  CMD23 first: ARG = nsect ; CMD = 0x171A0000 ; wait CC
                      BLK = (nsect<<16) | 512 ; ARG = lba ; CMD = 0x123A0036
                      per block: wait BRR, 128 word reads; after the last block TC
MULTI-WRITE (CMD25):  CMD23 first: ARG = nsect | 0x80000000 (reliable write)
                      CMD = 0x193A0026 ; per block: wait BWR, 128 word writes
ABORT  (CMD12 = 0x0C030000, R1b) if a multi-block transfer must be stopped early.
STATUS (CMD13 = 0x0D1A0000, arg = RCA<<16) -> R1: bits[12:9] = CURRENT_STATE (7 = transfer)
```

### 5.5 Status polling rules

* Never poll a status bit without a bound. Use `N` iterations of `udelay(1)` and return an
  error, not a hang. hd0's own budgets: `0x8000`/`0x0CCC`/`0x0666` ticks of the
  `0x4A304000+0x10` timer for bus polls (`10` §2.1) and 1000/256 retries for CMD1/CMD6.
* On any error bit: `STAT = 0xFFFFFFFF`, then `SYSCTL |= (1<<25) | (1<<26)` (`SRC`/`SRD`
  command/data line reset) and poll the bits back to 0. That is exactly hd0's
  `0x800073A6` policy, and it is what makes a retry possible.
* Re-kick WDT2 between every block or every 8 KB (whichever comes first).

### 5.6 Timing / clock assumptions (INFERRED, one live read to settle)

| item | value | basis |
|---|---|---|
| MMCHS functional clock | 96 MHz | OMAP4430 CM_L3INIT/MMC — TRM; hd0's `CLKD` values (`0xF0`, 4, 2) only make sense against 96 MHz |
| `CLKD` encoding | `f = 96 MHz / (2 × CLKD)` | TRM Table 24-100 |
| init clock | `CLKD = 0xF0` → 200 kHz | PROVEN value, computed rate INFERRED (hd0 `0x80007930` sel 0) |
| transfer clock | `CLKD = 4` → 12 MHz; `CLKD = 2` → 24 MHz | PROVEN values (hd0 sels 1/2), rates INFERRED |
| 4-bit @ 24 MHz | 12 MB/s peak, ~8–10 MB/s sustained | arithmetic |
| per-block CMD17 polled | ~60–160 µs | sum of cmd+resp+data+busy at 12–24 MHz |
| **clock enable** | MMCHS2's interface/functional clocks come from PRCM. `probe.S` (`96` §1) reports that **PRCM `CLKCTRL` writes abort from NS**, so the payload cannot re-enable them. They are on at hand-off (the bootloader just used the controller) but nothing proves they stay on. | risk **R1**, §9 |
| PBIAS | OMAP4 needs `CONTROL_PBIASLITE` for **MMC1 only**; MMC2 (eMMC) does not | TRM |

---

## 6. Region 1 layout

### 6.1 Facts

| item | value | source |
|---|---|---|
| region 0 = `/dev/hd1`, region 1 = `/dev/hd2` | each 512 MB = 1,048,576 sectors | `95` §1, `97` §1 |
| region-0 OS IFS | starts at byte `0x100000` (**LBA 2048**), length `0x2BFE10` = **2,883,088 B = 5,631.03 → 5,632 sectors** (`0x1600`) | `95` §1 + `hd1-ifs-image.bin` (2,883,584 B = 0x2C0000) and `*.trim.bin` (2,883,088 B) |
| region-0 IFS sector 0 | `fe 01 00 ea | 00 f8 0f 80 | 10 f6 3b 80 | …` = `0xEA0001FE`, `0x800FF800`, `0x803BF610` | `sector-0x100000.orig.bin` — **PROVEN** |
| region 1 | IFS area all zero; `hd2t179` exists but unused | `95` §1 |
| factory/CRC path | **region 0 only**, no fallback | `95` §5 |

### 6.2 LBA math

```
sector(region, byte_off) = (region == 0 ? 0 : 0x100000) + (byte_off >> 9)
byte (region, lba)       = (lba - (region == 0 ? 0 : 0x100000)) << 9

region 0:  byte X        -> LBA X >> 9                  (X = 0x100000 -> LBA 2048)
region 1:  byte Y        -> LBA 0x100000 + (Y >> 9)      (Y = 0        -> LBA 0x100000)
mirror:    region1(Y)    == region0(Y)      for the same Y
```

### 6.3 Proposed region-1 map (design)

| region-1 byte | LBA (absolute) | sectors | content | why safe |
|---|---|---|---|---|
| `0x00000000` | `0x100000` | `0x1600` (5,632) | **sealed TabletOS IFS backup**, `0x2BFE10` bytes + `0x1F0` pad to 2.75 MB | region-1 low area is zero and the factory bootloader never reads region 1 (`95` §5) |
| `0x0002C000`–`0x00FFFFFF` | `0x100160`–`0x107FFF` | — | **unallocated / do not use** (keeps the backup contiguous with nothing else) | — |
| `0x01000000` | `0x108000` | `0x8000` (32,768 = 16 MB) | **payload area**: `zImage`/`Image` at `+0`, DTB at `+8 MB`, initramfs at `+9 MB` | 16 MB offset is 4 MB above any plausible IFS/RFS structure; region 1 has no RFS |
| `0x02000000` | `0x110000` | … | spare payload slot 2 (A/B for the Linux side) | — |
| `0x1FF00000` | `0x1FF800` | 1 | **flag block A** (magic + flags + attempt counter + CRC32/ISO-HDLC of the block) | last 1 MB, reserved |
| `0x1FF00200` | `0x1FF801` | 1 | **flag block B** (redundant copy; A/B so a torn write cannot brick the switch) | ″ |
| `0x1FF00400` | `0x1FF802` | 2 | **scratch test sectors** (self-test: write nonce, read back, compare) | ″ |
| `0x1FF00800` | `0x1FF804` | 2 | **backup header** (source SHA-256, length, CRC, generation) | ″ |
| `0x1FF01000` | `0x1FF808` | 8 | **payload descriptor** (kernel/DTB/initramfs LBAs + sizes + CRCs) | ″ |
| `0x1FF10000` | `0x1FF880` | `0x380` (896) | **eMMC read-back ring**: 448 × 1 KB breadcrumb snapshots, written by the payload | ″ |
| `0x1FFF0000` | `0x1FFF80` | 128 | reserved (grows the ring / future) | ″ |

Rationale for putting everything either low (the backup, forced by size) or in the last
1 MB: the only other writers are (a) QNX/TabletOS, which confines itself to region 0 —
`winchester.py`: MBR `0x0`, SIG `0xF0000`, IFS `0x100000`, RFS `0xC00000` — and (b) a
CFP/OS-update flash, which writes region 0 (and `95` proved it does not consult region 1).
Sector 0 of region 1 is *not* used for the flag block: if a future loader ever decides to
parse region 1 as a disk it will read LBA `0x100000` first, and that is the backup's
first sector, which begins `0xEA0001FE` — a valid ARM branch, which is the least
surprising possible content.

**Total region-1 use: 5,632 (backup) + 32,768 (payload) + 32,768 (slot 2) + 2,048
(control) ≈ 73,216 sectors ≈ 35 MB of 512 MB.** (PROVEN arithmetic.)

---

## 7. Test payload design (*"eMMC probe"*) — **not to be run**

Entry state assumed (`91` §3): ARM, SVC, `CPSR.I=F=1`, `SCTLR.M = 0`, D/I-cache possibly
on, `VBAR` unset by the image, `r1`–`r3` garbage, no return path, WDT2 counting
(`0x4A314000`, kick `+0x30`). The payload is the `0x80100000` blob of `91` §6.3, zone
`0x80100800`.

### 7.1 Channels

| channel | address | use |
|---|---|---|
| breadcrumb block | `0x90000000` + mirrors `0x94000000`, `0x88000000`, `0x9FE00000` | `[0] = 0x4C424B43` ("KBLB"), `[1] = stage`, `[2] = status`, `[3] = nonce`, `[4] = lba`, `[5..7] = payload` (first word read / second word / capacity), `[8..0xF] = free` | 
| LED | FAN5702 **EN = GPIO1_13**: `GPIO1 = 0x4A310000`, `CLEARDATAOUT +0x190` (off), `SETDATAOUT +0x194` (on) | `stage` blinks, then a long gap; `0xE0|stage` on failure |
| WDT2 | `0x4A314000`, kick = `[+0x30]++` (hd0 `0x800094E4`); disable = `WSPR(+0x24) = 0xAAAA` then poll `+0x24` bit1 then `= 0x5555` | kick at every stage and inside every wait loop |
| UART3 | `0x48020000`, 115200 8N1 | optional one-line log per stage (`96` §3) |
| eMMC ring | region-1 control area (§6.3) | the *durable* log: the probe writes its own breadcrumb block to `LBA 0x1FF880+` before parking, so the result survives even if QNX's `memdump3` is unavailable |

### 7.2 Stage codes (published at `0x90000000[1]`, mirrored)

| code | meaning | expected observation |
|---|---|---|
| `0x01` | entered, own stack, caches disabled (`SCTLR.C=I=0`, clean+invalidate L1) | 1 blink |
| `0x02` | breadcrumbs + nonce published, WDT2 kicked | — |
| `0x03` | SCTLR/CPSR/VBAR/TTBR0 sampled into `[8..0xB]` | — |
| `0x10` | MMCHS2 read-only probe: `REV(0x2FC)`, `CAPA(0x240)`, `PSTATE(0x224)` captured into `[5..7]` | `REV != 0 && != 0xFFFFFFFF` |
| `0x11` | soft reset done (`SSTAT` bit0 = 1), `IE = 0`, `ISE = 0`, `STAT = 0xFFFFFFFF` | — |
| `0x12` | `SYSCTL.CLKD = 0xF0`, `ICS` set, `CEN` set | 2 blinks |
| `0x13` | CMD0 (CON.INIT idiom) completed, `STAT.CC` seen | — |
| `0x14` | CMD1 completed, OCR ready (`RSP10 & 0x80000000`) | `[6]` = OCR |
| `0x15` | CMD2 completed, CID captured | `[5]` = CID[0] |
| `0x16` | CMD3 completed, RCA = `0x0001` | — |
| `0x17` | CMD9 completed, CSD captured | `[6]` = CSD[0] |
| `0x18` | CMD7 completed, `CMD13` says CURRENT_STATE = 7 (transfer) | `[7]` = R1 |
| `0x19` | CMD16 (BLK = 512) completed | — |
| `0x1A` | EXT_CSD read (CMD8+data), `SEC_COUNT` at `[12]`, `BUS_WIDTH`/`HS_TIMING` at `[13]` | `SEC_COUNT >= 0x01000000` |
| `0x1B` | 4-bit bus (`CON.DW = 1`, `HCTL.DTW = 1`), `CLKD = 4` (12 MHz) | 3 blinks |
| `0x20` | **read region-0 IFS sector 0 (`LBA 2048`)** into `0x84000000` | `[4] = 2048`, `[5] = 0xEA0001FE` |
| `0x21` | sector-0 header check | `[5] == 0xEA0001FE`, `[6] == 0x800FF800`, `[7] == 0x803BF610` → PASS |
| `0x30` | **read region-1 `LBA 0x100000`** (backup slot) into `0x84000800` | `[4] = 0x100000`, `[5]` = first word |
| `0x31` | compare with the `0x20` buffer | equal on a unit whose region 1 already holds a backup; **`0xFFFFFFFF` on a fresh region 1** (all-zero flash reads as `0xFFFFFFFF` only if unwritten — record the raw word either way) |
| `0x40` | **write scratch sector** `LBA 0x1FF802` with a 512-byte nonce block (magic `0x4C424B43`, stage, nonce, inverse nonce, CRC32) | — |
| `0x41` | **read back** `LBA 0x1FF802` and `memcmp` | PASS/FAIL in `[2]` |
| `0x42` | second scratch sector `LBA 0x1FF803` (negative test: write `~nonce`, expect mismatch on the first block) | proves the compare is real |
| `0x50` | **park**: write final breadcrumb block to the eMMC ring at `LBA 0x1FF880`, flush, LED steady on, then stop kicking WDT2 → warm reset in ~58.6 s (`96` §2) | LED solid; breadcrumbs survive |
| `0xE0\|stage` | failure at `stage`: LED `stage&0xF` fast blinks + long gap, park as above | distinguishes "failed" from "hung" |

### 7.3 Status word (`0x90000000[2]`)

| value | meaning |
|---|---|
| `0x00000000` | not finished |
| `0x600D0000` | all stages PASS (read + write + verify) |
| `0xBAD0_00nn` | failed at stage `nn` |
| `0xBAD1_00nn` | stage `nn` timed out (bounded poll expired) |
| `0xBAD2_00nn` | stage `nn` saw an MMC error bit (capture `STAT` in `[0xE]`) |

### 7.4 Safety rules the design obeys

1. **Never write outside `LBA 0x1FF802`–`0x1FF803`.** Region 0 is never written by this
   probe at all. A read-only first version (stages up to `0x31`) is strictly safer and
   should be flown first.
2. The nonce is generated from the WDT2 counter + the CPSR-obtained cycle-free junk, not
   from anything the host must know; the host reads it back from the ring.
3. Every loop has a bound; every bound failure publishes `0xBAD1_…` and parks.
4. Cache policy: the buffers at `0x84000000` must be accessed with the D-cache
   **disabled** (`SCTLR.C = 0` at entry, as `91` §6.4 already requires), otherwise a
   write buffer will hold data the card never sees.
5. WDT2 is kicked before the first eMMC access and between every 8 KB.

---

## 8. Reconciliation with the one-shot / restore design (`97` §1)

### 8.1 Minimum capability, itemised

| need | minimum driver feature | probe stage that proves it |
|---|---|---|
| (a) read a Linux `zImage`+DTB from region 1 | CMD0/1/2/3/7/16 + CMD17 single-block (or CMD18+CMD23 multi) + 4-bit bus. **Read-only.** | `0x13`–`0x21`, `0x30` |
| (b) copy the 2.75 MB sealed IFS backup over region 0's IFS area | the above **plus** CMD24 (single) or CMD25+CMD23 with the reliable-write bit, plus a verify pass (read-back + CRC32/ISO-HDLC over `[start, end-4)` — the same checksum the loaders use, `71` §2.1) | `0x40`–`0x42` (2-sector proof), then a 2.75 MB run |
| (c) the flag block | 1-sector read + 1 write + read-back, in the last 1 MB of region 1 | `0x40`–`0x42` |
| **not needed** | DMA, CMD6 HS_TIMING, SD-specific ACMD41/ACMD6, write protection, RPMB, partition switching | — |

So the *minimum* is: **PIO, 512-byte blocks, CMD17 and CMD24, 12 MHz, 1-bit or 4-bit
bus.** Everything else is optimisation.

### 8.2 The 2.75 MB copy, timed

Copy = read 2,883,088 B from region 1 + write 2,883,088 B to region 0 = 5,766,176 B of
bus traffic = 11,262 blocks of 512 B.

| mode | per-block cost | 11,262 blocks | notes |
|---|---|---|---|
| **PIO, single-block, polled** (CMD17/CMD24 one block per command, 12 MHz, 4-bit) | ~140 µs (48-bit cmd + 48-bit resp + 512 B data at ~6 MB/s + TC) | **≈ 1.6 s** (≈ 0.8 s read + 0.8 s write) | this is what §5.3/§5.4 do; add ~10 % for the verify pass |
| **PIO, single-block, polled at 24 MHz** | ~110 µs (bus-limited data phase halves) | **≈ 1.3 s** | the command overhead dominates at 12 MHz |
| **PIO, multi-block** (CMD23+CMD18 / CMD23+CMD25, 64 KB per command) | 512 B per BRR/BWR event, ~4 µs polling latency amortised | **≈ 0.7–1.0 s** | 45 commands per direction instead of 5,631 |
| **DMA** (SDMA/ADMA, if the driver had it) | bus-limited, ~1 µs CPU per 64 KB | **≈ 0.6 s** | saves ≤ 0.3 s over multi-block PIO, i.e. **< 20 %** |

**Verdict:** a 2.75 MB restore takes **~1–2 s** in the planned PIO driver and **~0.7 s**
if multi-block is used. That is 3–5 % of the ~58.6 s WDT2 window, so the watchdog is a
non-issue as long as it is kicked during the transfer; DMA is not worth the extra state
machine. Full erase-before-write is *not* required for eMMC (the card handles program/
erase internally); CMD38 (ERASE) + CMD36 (ERASE_GROUP_START) + CMD35 (ERASE_GROUP_END)
would add seconds and wear for no benefit.

### 8.3 What this changes in `97`

* `97` §1's step 1 ("reusing them, or re-initialising MMC2 `0x480B4000`") is now decided
  in favour of **re-initialising**: §4 rules reuse out.
* `97` §5 Q1 is answered: **the bootloader's routines cannot be called safely** (§4);
  re-init is required and is ~200 lines of ARM.
* `97` §5 Q4 (region-1 flag sector) is answered: **`LBA 0x1FF800`/`0x1FF801`** (A/B), in
  the reserved last 1 MB (§6.3), *not* sector 0.
* The 2.75 MB backup fits in `region1[0x000000..0x2C0000)` and the restore source is
  therefore `LBA 0x100000 + i`, target `LBA 0x800 + i`, `i < 0x1600`. (PROVEN arithmetic.)

---

## 9. Residual unknowns

| id | question | how to settle it | risk |
|---|---|---|---|
| **U1** | **Who loads the OS image to `0x800FF800`?** No code in `hd0` does (§3) and the only MMC loader present (`0x800044BA`) targets a 512 KB TI layout at `0x80000000`. | a payload that publishes `PSTATE`/`SYSCTL`/`REV` of MMCHS1 and MMCHS2 at entry: whichever instance has `CEN` set and a valid `REV` is the one the loader used | **low** (does not block the driver) |
| **U2** | Is the MMCHS2 functional clock still enabled at payload entry, and can it be re-enabled? `probe.S` says PRCM `CLKCTRL` writes abort from NS. | read `CM_L3INIT_MMC2_CLKCTRL` (`0x4A009100`-class; exact address to be confirmed from the TRM CM chapter) at entry and publish it; probe stage `0x10` already does the indirect version (`REV`/`CAPA` non-zero ⇒ clock on) | **high** — if the clock is off and cannot be enabled, the standalone driver cannot work at all and the whole region-1 scheme collapses |
| **U3** | Are MMCHS2's CORE pads still muxed for MMC (the bootloader set them)? | same probe: `PSTATE.CLEV` / `DLEV` — a stuck DAT line at 0 with the card out of reset is a symptom | medium |
| **U4** | Module A's true link address (`0x40300000`-based per §4.2) and whether module A or module B is the one the stage intended to use | needs a live read of `0x40308AB8` during the boot window, or a stage-side disassembly of the code that copies the section | low (we do not call it) |
| **U5** | Does `SEC_COUNT` really equal `0x03A00000` on the 64 GB unit (and `0x01D00000` on the 32 GB)? | payload stage `0x1A` publishes it | none |
| **U6** | Does a WDT2 warm reset preserve the region-1 sectors just written? (eMMC writes are durable, so this should be trivially yes — but the *cache* in the card and any pending write must be flushed by the read-back, which the design does.) | stage `0x41` verifies before parking; `0x50` re-verifies after the reset on the *next* boot | low |
| **U7** | The `CLKD`-to-Hz mapping (§5.6) | measure: time a 2.75 MB read at `CLKD = 0xF0`, 4, 2 and compare | low |
| **U8** | Does the factory/CRC path ever read region 1 (e.g. for a future `os_type`)? Currently proven **no** (`95` §5) | re-check if a new `os_type` value is ever tried | low |

---

## 10. Reproduction commands

`$S` = `$COMMANDCODE_SCRATCHPAD`. All host-side and read-only.

```sh
# 0. integrity
sha256sum /home/psyden/rea-work/bootdumps-2026-09-11/hd0head.bin
#   dda9d955003302adf1f0d41580c50c0c9035126487e7ac53793f4d38a3a23e3d

# 1. the command table (the crown jewel) — decode all 59 entries
python3 - <<'EOF'
import struct
b=open("/home/psyden/rea-work/bootdumps-2026-09-11/hd0head.bin","rb").read()
for n in range(0x3b):
    w0,w1=struct.unpack_from("<II",b,0x8ab8+n*8)
    print(f"{n:#04x} CMD{w0&0x3f:<3} cmd={((w0&0x3f)<<24)|w1:08x}")
EOF

# 2. all MMC-window literals (proves the 1->MMCHS1 / 2->MMCHS2 map and its 8 sites)
python3 - <<'EOF'
import struct
b=open("/home/psyden/rea-work/bootdumps-2026-09-11/hd0head.bin","rb").read()
for i in range(0,0x20000,4):
    w=struct.unpack_from("<I",b,i)[0]
    if (w>>16) in (0x4809,0x480B): print(hex(0x80000000+i),hex(w))
EOF

# 3. all MMCHS register accesses (proves PIO: no SDMASA/ADMA writes, DE=0 in the table)
python3 $S/cmdreg.py | sort -k3

# 4. the two driver instances (module A vs module B): same body at two addresses
python3 $S/pbdis.py 0x80006458 0x80006500 thumb | head
python3 $S/pbdis.py 0x8000e2ec 0x8000e394 thumb | head

# 5. caller analysis (proves 0x800044ba / 0x80004d6a are unreachable)
python3 $S/calls.py 0x800044ba 0x80004ce0 0x80004d6a 0x80004f0c 0x80006e7e 0x8000731c 0x80007d22

# 6. Ghidra headless re-derivation of every pseudocode quoted above
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk
$GHIDRA/support/analyzeHeadless \
  /home/psyden/rea-work/bootdumps-2026-09-11/analysis/ghidra-mmc-2026-10-26 gproj \
  -import /home/psyden/rea-work/bootdumps-2026-09-11/hd0head.bin \
  -processor ARM:LE:32:v7 -loader BinaryLoader -loader-baseAddr 0x80000000 \
  -scriptPath $S -postScript SeedCorrect.java seed2.txt out.txt -deleteProject

# 7. TRM cross-checks (register map / bit fields)
grep -n "Table 24-56\|Table 24-58\|Table 24-100\|Table 24-102" \
  /home/psyden/rea-work/bootdumps-2026-09-11/swpu231ap.pdf.txt

# 8. region arithmetic sanity check
python3 -c "
ifs=0x2BFE10; sec=0x100000
print('ifs bytes',ifs,'sectors',-(-ifs//512),hex(-(-ifs//512)))
print('backup  LBA', hex(0x100000), '->', hex(0x100000+0x1600-1))
print('restore LBA', hex(0x100000>>9), '->', hex((0x100000>>9)+0x1600-1))
print('payload LBA', hex(0x100000+(0x01000000>>9)))
print('control LBA', hex(0x100000+(0x1FF00000>>9)), 'scratch', hex(0x100000+(0x1FF00400>>9)))"

# 9. expected region-0 sector 0 (what the probe must see)
xxd -l 32 /home/psyden/rea-work/bootdumps-2026-09-11/analysis/playbook-live-spare-2026-10-25/sector-0x100000.orig.bin
```

---

## 11. One-paragraph summary

`hd0` carries two copies of a full OMAP4 `MMCHS` PIO driver (module A at
`0x80004000`–`0x80008D70`, module B from `0x8000C000`–`0x80011000`), instance-selected
`1 → 0x4809C000` / `2 → 0x480B4000`, with a 59-entry opcode→`MMCHS_CMD` table at file
`0x80008AB8` (`0x40308AB8` at run time) that is byte-identical in encoding to the boot
ROM's (`64` §2.3), with `DE = 0` everywhere (PIO/FIFO through `+0x220`), a card-init
chain CMD0→CMD1→CMD2→CMD3→CMD9→CMD7→CMD16→CMD8(EXT_CSD)→CMD6, single-block
read/write at `0x80005D66`/`0x80005F48` behind `0x80004CE0`/`0x80004D6A`, and CMD23 for
multi-block. **It is not callable from a payload**: its public entries have no caller, its
whole working state (command table, per-controller records, per-device records, step
counters, completion callbacks) lives in IRAM at `0x40308AB8`–`0x40309140` which is
neither in the image nor initialised by any reachable code, module A's own image loader
uses `0x80000000` as a buffer (so it cannot be executing at `0x8000xxxx`), and no code in
`hd0` loads the OS image to `0x800FF800` at all. The payload therefore needs its own
~200-line PIO driver on `0x480B4000` (CMD0/1/2/3/7/9/16/8-EXT_CSD/17/24 + optional
18/25+23, 512-byte blocks, 200 kHz → 12–24 MHz, 4-bit), which turns the 2.75 MB
region-1→region-0 restore into a ~1–2 s PIO operation well inside the WDT2 window, and
makes `97`'s one-shot/restore scheme implementable in region 1 at
`0x000000` (sealed IFS backup), `0x01000000` (payload) and `LBA 0x1FF800` (A/B flag block
+ scratch + read-back ring) — with the single remaining high risk being whether the
MMCHS2 functional clock is still enabled at hand-off (U2).
