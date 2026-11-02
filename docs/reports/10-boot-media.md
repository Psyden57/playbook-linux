# 10 — Boot media / device selection

Target: `omap4430-bootrom-0x40028000.bin`, base `0x40028000`, 49152 bytes, SHA-256
`fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`.

> Update 2026-10-19b: `0x4002F9F8` is now decoded as an **EMIF/DDR descriptor**
> (two 0x24-byte records mapping to `EMIF_SDRAM_CONFIG/REF_CTRL/TIM_1..3/
> PWR_MGMT/DDR_PHY_CTRL`), and command 8 is the SDRAM/memory-boot path that
> opens the `0xC0000000` DDR CS1 window. The `0x4002F7C4` gating and the
> `MMCHS_CMD` flag bits are resolved in `64-rom-api-and-media-final.md` §2.
> In particular `DE` is **DMA Enable**, not "data enable".

Method: LLVM-23 `llvm-objdump` (forced Thumb-2 / ARM per range, see `METHOD.md`) +
Python `struct` reads of the raw dump. **Every statement below is LLVM-derived
observation from the raw image, not REA Evidence.** Inference is marked
"*(inferred)*"; unresolved items are listed in §10. Companion reports: `20-config-parser.md`
(parser + media handler dispatch), `40-iram-and-tables.md` (bootstrap/tables).

---

## 1. Scope and summary

This report covers the code that (a) drives the MMC/SD controller block (MMCHS),
(b) drives the flash/NOR path, (c) presents either as a uniform "boot media"
device, and (d) selects which media to use.

Findings, in one paragraph: the ROM talks to the OMAP4430 **MMCHS controllers**
through a 7-entry base table at `0x4002FE20`; all controller primitives take a
*controller index* (`r0`) that indexes that table. A *card/device layer* on top
(`0x4002E7AC..0x4002ED76`) implements SD/MMC identification (CMD0/CMD8/CMD55/
ACMD41/CMD1/CMD2/CMD3/CMD9/CMD7/CMD16/CMD6/CMD17), reads the CSD to program the
clock divider via the table at `0x4002FE3C`, and computes capacity. A *device
abstraction layer* (`0x4002BA54..0x4002BC73`) exposes `open/read/ioctl` and maps
an outer "device type" byte `{5,6,7,8}` to MMC controller indices `{0,1,2,6}` at
`0x4002EC38`. A parallel *flash/NOR path* (`0x4002E25C..0x4002EF14`) identifies
the flash device by its ID (read-ID command `0x90`) and derives geometry from two
tables at `0x4002FEB4`/`0x4002FECF`; its memory window is `0x08000000` and its
register window is `0x50000000`-based. The top-level **config parser**
(`20-config-parser.md`) dispatches one command byte at a time (1..8 = media
source, `'A'..'G'` = named image) to the media handler `0x4002AD5C`, which picks a
3-function vector from `0x4002F578 + cmd*12`. Read/select order is therefore
driven by the mutable 8-byte command table the parser iterates, not by a fixed
probe list.

---

## 2. MMC controller layer

### 2.1 Controller base table — `0x4002FE20`

7 little-endian u32, referenced from the literal pools at `0x4002E048` and
`0x4002E0A4` (both hold the **boot alias** `0x0002FE20`):

| idx | base | *(inferred)* block |
|----|------|--------------------|
| 0 | `0x4809C000` | MMC1 |
| 1 | `0x480B4000` | MMC2 |
| 2 | `0x480B4000` | MMC2 (duplicate of idx 1 — see §10) |
| 3 | `0x480AD000` | MMC3 |
| 4 | `0x480D1000` | MMC4 |
| 5 | `0x480D5000` | MMC5 |
| 6 | `0x480B6000` | unidentified (MMC-class) |

Other pool words next to the table (used by the MMC primitives):

| literal | value | use |
|---|---|---|
| `0x4002E04C` | `0x307F0033` | value written to `IE` (0x234) in controller reset |
| `0x4002E050` | `0x00000FFA`→`0xFFF0FFFA` | mask in `SYSCTL` clock-divider update |
| `0x4002E054` | `0x4A304000` | timer base (offset `+0x10` = free-running counter) |
| `0x4002E058` | `0x337F87FF` | write-1-clear mask for `STAT` (0x230) |
| `0x4002E05C` | `0x00200030` | command word used by the transfer-setup routine |
| `0x4002E0A4` | `0x0002FE20` | second copy of the table pointer |

### 2.2 Register offsets (base + offset, all 32-bit unless noted)

Observed (MMCHS block, SDHCI-compatible plus TI SYSCONFIG):

| off | name | bits used |
|-----|------|-----------|
| `0x110` | SYSCONFIG | bit1 = soft reset |
| `0x114` | SYSSTATUS | bit0 = reset done (polled) |
| `0x12C` | CON | bit0 INIT, bit1 init-stream, bit5 bus width, bit17 (`0x20000`) |
| `0x204` | BLK | bits[11:0] block size (bytes) |
| `0x208` | ARG | command argument |
| `0x20C` | CMD | command descriptor (see §2.3) |
| `0x210..0x21C` | RSP10/32/54/76 | response FIFO |
| `0x220` | DATA | block data FIFO |
| `0x224` | PSTATE | bit1 = CMD inhibit |
| `0x228` | HCTL | bit1, bit8; reset writes `0xC10`, then `|= 0x100` |
| `0x22C` | SYSCTL | CLKD bits[15:6]; bits17–19; CEN bit2; ICS bit1 (polled) |
| `0x230` | STAT | bit0 CC, bit5 BRR, bit15 ERRI (write-1-clear) |
| `0x234` | IE | bit10 (`0x400`), bit21 (`0x200000`) |
| `0x240` | CAPA | bit25 set in reset |
| `0x2FC` | REV | revision (read, unused) |

### 2.3 The command descriptor word (written raw to `0x20C`)

The ROM stores commands as a 32-bit descriptor `cmdword`; `0x4002DDD4` writes it
verbatim to `0x20C`. Decoded from strong cross-evidence:

* **bits[31:24] = MMC command index.** Verified against every command used:
  CMD0 `0x00000000`, CMD1 `0x01020000`, CMD2 `0x02090000`, CMD3 `0x031A0000`,
  CMD4 `0x04040000`, CMD6 `0x061A0000`/`0x061B0000`, CMD7 `0x071B0000`,
  CMD8 `0x081A0000`, CMD9 `0x09090000`, CMD16 `0x101A0000`, CMD17 `0x113A0010`,
  CMD55 `0x371A0000`, ACMD41 `0x29020000`.
* **bits[17:16] = response-length selector** (observed directly at
  `0x4002DE34`, `ubfx r1,r1,#0x10,#2`; `cmp r1,#1`): value **1 → a 4-word
  (128/136-bit) response** is stored (`RSP10..RSP76`), any other value → a
  1-word (48-bit) response. The two 128-bit commands in the image are CMD2 and
  CMD9 (both `0x..09....`, flag byte `0x09` → bits[17:16] = 1). This matches
  R2 semantics (CID/CSD).
* The remaining flag bits (bit0..bit5 .. bit23) encode command attributes
  (CRC check / data-present / no-response etc.); their exact meaning is **not**
  decoded (see §10).

> Note: this field placement (index in the top byte) is **not** the SDHCI 16-bit
> command register layout. The ROM writes the descriptor raw to MMCHS_CMD, so the
> hardware MMCHS_CMD register must use this layout. Stated as observation; TRM not
> consulted.

### 2.4 Controller primitive functions (all Thumb-2, `r0` = controller index)

| addr | function | inputs | outputs / effect |
|------|----------|--------|------------------|
| `0x4002DC68` | controller reset/init | `r0` | SYSCONFIG|=2, poll SYSSTATUS b0; HCTL=`0xC10`; CAPA|=1<<25; CON=0; HCTL|=0x100; IE=`0x307F0033`; BLK=`0x200`; ret 0 |
| `0x4002DCD6` | set clock divider | `r0`, `r1`=divider | SYSCTL: bits17–19 set, `[23:22]` masked, `CLKD = (r1<<6)` with bit0 forced set; poll ICS; CEN=1; ret 0 |
| `0x4002DD2C` | power-on init stream | `r0` | calls `0x4002DCD6(0x25E)`; CON|=2; loop 2× (CMD=0, poll CC, STAT=1); CON&=~2; ret 0 |
| `0x4002DD8E` | set bus width | `r0`, `r1`∈{1,2,4} | CON bit5 / HCTL bit1 via `bfi`; ret 0 (1 if bad width) |
| `0x4002DDD4` | **send command** | `r0`, `r1`=cmdword, `r2`=arg, `r3`=resp(buf) | poll PSTATE b1 (with `0xccc` tick timeout, timer `0x4A304000+0x10`); STAT=`0x337F87FF`; ARG=`r2`; CMD=`r1`; poll STAT; ERRI(b15)→ret1; CC(b0)→STAT=1, store RSP10 (and RSP32/54/76 when bits[17:16]==1); ret 0 |
| `0x4002DE60` | **read block(s)** | `r0`, `r1`=buf, `r2`=count | reads `BLK[11:0]` bytes per BRR(b5) event from DATA(0x220); ERRI→ret1 |
| `0x4002DF58` | **write block(s)** | `r0`, `r1`=buf, `r2`=count | BWR(b4) events, writes words to DATA; ERRI→ret1 |
| `0x4002DECC` / `0x4002DEE2` | IE bit21 clear / set | `r0` | IE(0x234) bit21 |
| `0x4002DEF8` | CON bit0 set | `r0`, `r1` (1→1 else 0) | CON(0x12C) bit0 |
| `0x4002DF14` / `0x4002DF36` | SYSCTL software reset (CMD / DAT) | `r0` | SYSCTL |= 1<<25 / 1<<26, poll same bit |
| `0x4002DFB0` | transfer setup/abort | `r0`, `r1` | CON|=0x20000; IE|=0x400; STAT=`0x337F87FF`; ARG=`r1-6`; CMD=`0x00200030`; poll b10 with `0x666` tick timeout; CON&=~0x20000; IE&=~0x400 |
| `0x4002E060` | command0 + wait | `r0` | sends CMD0, waits CC (uses the `0x4002E0A4` pool) |

Timeouts are implemented with the free-running 32 kHz-class timer at
`0x4A304000` (`[+0x10]`), giving constants `0xccc`, `0x666`, `0x8000` in
different routines.

---

## 3. Card / device layer (MMC/SD identification)

The card layer operates on an **inner device record** (pointer usually in the
outer record's `[+0x18]`). Field layout derived from all users:

| off | meaning |
|-----|---------|
| `+0x00` | MMC controller index (index into `0x4002FE20`) |
| `+0x04` | **card type**: 1 = MMC, 2 = SD (set at runtime by the CMD55 probe) |
| `+0x08` | state (0 idle → 1 opened → 2 …) |
| `+0x0C` | block size (`1 << page-exp`), also used as address unit |
| `+0x10` | capacity/geometry field (exponent, or 1<<n) |
| `+0x12` | `1 << n` derived size field (u16) |
| `+0x14` | addressing mode: 1 = byte-addressed, 2 = block-addressed (high-capacity) |
| `+0x15`,`+0x16` | geometry nibbles (flash path) |
| `+0x17` | region/valid bitmask (flash path) |
| `+0x18` | supported bus-width bitmask |
| `+0x19` | derived value `0xd << field` |
| `+0x1C` | capacity (blocks) for the MMC path |
| `+0x20` | RCA (relative card address) for the MMC path |

### 3.1 Card init, part 1 — `0x4002E930` (Thumb)

Sets `dev[0x20]=0x1234`, then:

1. `0x4002DDD4(ctrl, 0, 0, resp)` — CMD0; fail → return 1.
2. `0x4002DDD4(ctrl, 0x081A0000, 0x1AA, resp)` — **CMD8 `0x1AA`**; if OK
   `r5 = 0x40000000` (voltage/HCS base) else 0.
3. `0x4002DDD4(ctrl, 0x371A0000, 0, resp)` — **CMD55**:
   * success → `dev[4] = 2` (**SD**), `dev[0x18] = 3`,
     `r5 |= 0x00FF8000` → `0x40FF8000`, next command `r9 = 0x29020000` (**ACMD41**);
   * failure → `dev[4] = 1` (**MMC**), `dev[0x18] = 1`,
     `r9 = 0x01020000` (**CMD1**), `r5 = 0x40FF8000`, and `0x4002DEF8(ctrl,1)`.
4. Poll loop (`0x4002E9A2..0x4002E9EE`): send `r9` with arg `r5`, poll busy
   (response bit31) with a `0x8000`-tick timeout, re-sending CMD55+ACMD41 while
   SD.
5. `dev[0x14]` = 2 or 1: for MMC from `resp` bits[30:29]; for SD from `resp`
   bit30 (CCS). *(inferred: 2 = block-addressed/high-capacity, 1 = byte.)*
6. `0x4002DDD4(ctrl, 0x02090000, r5, resp)` — **CMD2** (ALL_SEND_CID).
7. `0x4002DDD4(ctrl, 0x031A0000, …)` — **CMD3** (SET_RELATIVE_ADDR):
   * MMC: arg = `dev[0x20]<<16` (RCA fixed at 0x1234), then `0x4002DEF8(ctrl,0)`;
   * SD: arg 0, then `dev[0x20] = lower16(resp)`.
8. Return 0.

### 3.2 Card init, part 2 — `0x4002EA5E` (Thumb)

`0x4002DCD6(ctrl, 0xF2)` → `0x4002E930` (retried once on failure) → `0x4002E904`
(sends **CMD9**, `0x09090000`, arg `RCA<<16`, into a stack response) → MMC extra
field → `0x4002DDD4(ctrl, 0x04000000, 0x04040000, …)` (**CMD4**) →
`0x4002E8CE` clock-divider lookup → `0x4002DCD6` set clock →
**CMD7** (`0x071B0000`, arg `RCA<<16`) → **CMD16** (`0x101A0000`, arg `0x200`) →
`0x4002E844` capacity → `0x4002E7AC` select/probe.

| addr | function | inputs | effect |
|------|----------|--------|--------|
| `0x4002E904` | send CMD9 / copy response | `r0`=dev, `r1`=out | `0x4002DDD4(ctrl, 0x09090000, dev[0x20]<<16)`; copies 16-byte response to out |
| `0x4002E8CE` | **clock divider from response** | `r0`=dev, `r1`=resp, `r2`=out | `N = resp[0xc]&7`, `M = (resp[0xc]>>3)&0xF`; require `N≤3` and `(M*8-8) < 0x71`; `*out = (u16 at 0x4002FE3C + M*8 + N*2 - 8) << 1` |
| `0x4002E844` | **capacity** | `r0`=dev, `r1`=csdbuf | if `dev[0x14]==2`: SD uses CSD bytes, MMC issues CMD9 and reads; else parses `csdbuf[8]` (CSD v2). Stores `dev[0x1c]` (0 → fallback `0x800000`) |
| `0x4002E7AC` | **select/probe** | `r0`=dev, `r1`=buf | `dev[4]==2` (SD): switch to high-speed via CMD55+ACMD, read 1 block, derive `dev[0x18]`/`dev[0x10]` from response bits; else indexed jump on `dev[0x10]` |
| `0x4002EB06` | **set bus width** | `r0`=dev, `r1`=1/2/4 | if `dev[0x18] & width`: `0x4002DD8E`; SD → CMD55 + ACMD6 (`0x4002A6A6`); MMC → CMD6 `0x061B0000` with EXT_CSD BUS_WIDTH arg `0x01B7_00nn` (`00`=1-bit, `01`=4-bit, `02`=8-bit) |
| `0x4002EB92` | **read block** | `r0`=dev, `r1`=request | addr = `r1[0]<<9` (byte mode) or `r1[0]` block (block mode); sends CMD17 `0x113A0010`; then `0x4002DE60` 1 block into `r1[8]` |
| `0x4002ECA4` | SD vendor/runtime read | `r0`=dev | sends `0x2D3A0010` (index 0x2D=45, arg `0xA1000000`), reads 1 block, stores 8 words into `[r0+0x24..0x44]` |
| `0x4002ECE8` | per-region loop | `r0`=dev | for `i=0..7` if `[r0+0x24+4i]`, sends index-45 command arg `0xB1000000|(i<<16)`, reads back one flag byte into `[r0+0x44+i]` |
| `0x4002ED48` | single index-45 op | `r0`=dev, `r1`=idx | sends `0x2D3A0010` arg `idx<<24`, stores `[r0+0x4c]=idx` |
| `0x4002ED76` | finalize bus/regions | `r0`=dev | scans `[r0+0x44+i]` (i=1..7), calls `0x4002ED48` for each set bit |
| `0x4002EC1A` | set addressing mode | `r0`=dev, `r1` | `dev[0x18][+0xc]` (inner) adjusted to 1..4 |
| `0x4002EAFc` | set clock via inner | `r0`=dev, `r1`=div | `0x4002DCD6(inner[0], r1)` |

### 3.3 Clock-divider table — `0x4002FE3C`

15 records × 4 u16 = `0x78` bytes, ending exactly at `0x4002FEB4`. Indexed by the
CSD/response field: `value = table[M*8 + N*2 - 8]` with `M` = 1..15, `N` = 0..3
(`M` from bits[6:3], `N` from bits[2:0] of `resp[0xc]`). Result shifted left 1 and
passed to `0x4002DCD6` as the divider.

| row (M) | N=0 | N=1 | N=2 | N=3 |
|---|---|---|---|---|
| 1 | 0x03CB | 0x0062 | 0x000A | 0x0001 |
| 2 | 0x0329 | 0x0051 | 0x0009 | 0x0001 |
| 3 | 0x02EB | 0x004B | 0x0008 | 0x0001 |
| 4 | 0x0287 | 0x0041 | 0x0007 | 0x0001 |
| 5 | 0x01E6 | 0x0031 | 0x0005 | 0x0001 |
| 6 | 0x0176 | 0x0026 | 0x0004 | 0x0001 |
| 7 | 0x0144 | 0x0021 | 0x0004 | 0x0001 |
| 8 | 0x0116 | 0x001C | 0x0003 | 0x0001 |
| 9 | 0x00F3 | 0x0019 | 0x0003 | 0x0001 |
| 10 | 0x00D8 | 0x0016 | 0x0003 | 0x0001 |
| 11 | 0x00BB | 0x0013 | 0x0002 | 0x0001 |
| 12 | 0x00B1 | 0x0012 | 0x0002 | 0x0001 |
| 13 | 0x00A2 | 0x0011 | 0x0002 | 0x0001 |
| 14 | 0x008B | 0x000E | 0x0002 | 0x0001 |
| 15 | 0x007A | 0x000D | 0x0002 | 0x0001 |

*(inferred)*: the divisions `03CB/03CB≈1000`, `0329≈809`, … `000A/0032` are
monotonically decreasing and column 1 (`N=1`) values look like 96/81/75/65/49/38/…
clock divisors; this is consistent with a TRAN_SPEED → MMCHS CLKD table. Column
semantics are not proven (see §10).

---

## 4. Device abstraction layer (outer record)

Outer record layout (all users agree):

| off | meaning |
|-----|---------|
| `+0x00` | opened/state flag (1 = opened) |
| `+0x04` | **boot-device type byte** — the media command (5/6/7/8) |
| `+0x05` | status byte (set to 1 or 4) |
| `+0x08` | substate (0/2) |
| `+0x0C` | sector/geometry count (u16) |
| `+0x10` | base address (flash: `0x08000000`) |
| `+0x14` | field (u16), `0x11`/`0x03`/`0x410` seen |
| `+0x16` | field (u16) |
| `+0x18` | pointer to inner record (`0x4002FE20` index holder) |
| `+0x1C` | size/param passed to `0x4002A95A`/`0x40029AE6` |

| addr | function | inputs | outputs / effect |
|------|----------|--------|------------------|
| `0x4002EC38` | **device-type → controller** | `r0`=outer | `inner=[r0+0x18]`; `byte[r0+4]`: 5→`inner[0]=0` + setup calls, 6→`inner[0]=1`, 7→`inner[0]=2`, 8→`inner[0]=6`; then `0x4002A95A(1,idx)` and `0x40029AE6(1,idx)` (PRCM enable of the selected MMCHS); else return 1 |
| `0x4002BA54` | **dev_open** | `r0`=outer | validates; if `[0]`=0: zero inner, `0x4002EC38`, `0x4002DC68`, `0x4002DD2C`, `0x4002EA5E`; sets `[0]=1`; SD path adds `0x4002ECA4`, `0x4002ECE8`, `0x4002ED76`; sets `[5]`/`[0x16]`; returns 0 |
| `0x4002BB1A` | **dev_read** | `r0`=outer, `r1`=req | validates; loops `req[4]` sectors; SD/MMC high-capacity path checks bounds at `[inner+0x400]`; calls inner read (`0x4002EB92`) or multi-sector (`0x40028FB4`); returns 0 |
| `0x4002BBD0` | **dev_ioctl** | `r0`=outer, `r1`=req | `req[0]`: 0→`0x4002EAFc`, 1→`0x4002EB06`, 2→`0x4002EC1A`; else 1 |
| `0x4002BB26?` | (inner read) | — | see `0x4002BB1A` |

Media command → operation vector table at **`0x4002F578`** (stride 12, one entry
per media command 1..8; used by `20-config-parser.md` §3.1):

| cmd | open | read | ioctl |
|-----|------|------|-------|
| 1 | `0x4002B809` | `0x4002B855` | `0x4002B86F` |
| 2 | `0x4002B809` | `0x4002B855` | `0x4002B86F` |
| 3 | `0x4002B879` | `0x4002B933` | `0x4002B963` |
| 4 | `0x4002B9A9` | `0x4002BA03` | `0x4002BA4D` |
| 5 | `0x4002BA55` | `0x4002BB1B` | `0x4002BBD1` |
| 6 | `0x4002BA55` | `0x4002BB1B` | `0x4002BBD1` |
| 7 | `0x4002BA55` | `0x4002BB1B` | `0x4002BBD1` |
| 8 | `0x4002BC11` | `0x4002BC59` | `0x4002BC73` |

Interpretation *(inferred)*:
* **cmd 1, 2** — memory-mapped/XIP source: open sets `outer[0x10] = 1<<27 =
  0x08000000`, then read does `memcpy(dst, 0x08000000 + off<<9, len<<9)`.
* **cmd 3, 4** — flash/NOR via GPMC (§5).
* **cmd 5, 6, 7** — MMC/SD: open = `0x4002BA55` (**dev_open**), read =
  `0x4002BB1B` (dev_read), ioctl = `0x4002BBD1` (dev_ioctl). The command byte
  itself becomes `outer[4]`, i.e. the type byte, which `0x4002EC38` maps to
  controller `{0,1,2}`.
* **cmd 8** — a separate device class (`0x4002BC11`); see §5.3.

The **image-op** vector table (`'A'..'F'`) at `0x4002F5E4` (stride 20) is used by
`0x4002AFFE` and mirrors this shape (5 functions per letter):
'A'–'D' → `{0x4002BD5D, 0x4002BF21, 0x4002BEC3, 0x4002BF5F, 0x4002BFC9}`;
'E','F' → `{0x4002C03F, 0x4002C1D3, 0x4002C1A3, 0x4002BFD5, 0x4002C205}`.

---

## 5. Flash / NOR path

### 5.1 Low-level flash controller functions

| addr | function | inputs | effect |
|------|----------|--------|--------|
| `0x4002E25C` | **read device ID** | `r0`=dev, `r1`=out(4) | writes `0x90` (read-ID) to `dev[4]` (16-bit if `dev[0x14]==1` else 8-bit) and 0 to `dev[8]`; reads 4 bytes from `dev[0]` into `out[0..3]` |
| `0x4002E29E` | **flash reset** | `r0`=dev | writes `0xFF` to `dev[4]`, delay `0x1999` |
| `0x4002E6E4` | read two 16-bit ID regs | `r0`=base, `r1`,`r2` out | reads `[base+0x1E000]` and `[+2]` |
| `0x4002E6F4` | read-ID via `0x90` | `r0`=base, `r1`,`r2` out | writes `0x90` to `[base]`, reads `[base]`→`*r1`, `[base+2]`→`*r2` |
| `0x4002E708` | status poll | `r0`=base | polls `[base+0x1E482]` bit15; reads `[base+0x1E480] & 0x401` |
| `0x4002E724` | issue flash command | `r0`=base,`r1`=cmd,`r2`=byte | programs the `0x1E200`-block registers and polls |
| `0x4002E754` | page read | `r0`,`r1`,`r2`,`r3`=buf | `0x4002E724` then copies `0x80` words from `base+0x400` to buf |
| `0x4002E77A` | command + flag | `r0`=base,`r1`,`r2` out | sets `[base+0x10020]`, issues command, `*out = ([base+0x10020]==0xFFFF)` |
| `0x4002E626` | verify/blank check | `r0`=dev,`r1` | reads sectors, compares against `0xFFFF` twice |
| `0x4002E68E` | program region bits | `r0`=dev | `dev[0x17] = bitmap` of regions that pass `0x4002E626` |
| `0x4002E6BA` | **flash probe** | `r0`=dev | `0x4002E29E`, `0x4002EE94`, optional `0x4002E29E`, `0x4002EF14` |
| `0x4002B970` | **identify** | `r0`=base=`0x08000000` | compares `0x4002E6E4` vs `0x4002E6F4` results |

The flash device descriptor for cmd 3/4 (`0x4002B879`/`0x4002B9A9`) sets
`inner[0] = 0x50000084`, `inner[4] = 0x5000007C`, `inner[8] = 0x50000080`, and
`outer[0x10] = 0x08000000` (`4<<25`). *(inferred)*: `0x50000000` is the OMAP
GPMC register window and `0x08000000` is the memory-mapped flash window; the
individual `0x5000007C/80/84` offsets were **not** matched to the GPMC TRM (§10).

### 5.2 Flash ID → geometry tables

`0x4002EF14(dev)`:
1. `0x4002E25C` reads 4 device-ID bytes into `sp[0..3]`.
2. Searches `sp[1]` against the **pair table at `0x4002FECF`** (stride-2 records
   `{code, index}`; terminated by a zero code byte). Found `index` selects a
   record in the **3-byte table at `0x4002FEB4`** (`base = 0x4002FECF - 0x1B`),
   address `0x4002FEB4 + index*3`.
3. Decodes the record's **nibbles** into `dev[0x10..0x19]`, `dev[0xC]`,
   `dev[0x12]`:
   * `rec[1]` low nibble → `1 << n` → `dev[0x12]`; high nibble → exponent
     `dev[0x10]` and `dev[0xC] = 1 << exp`;
   * `rec[0]` low nibble → `dev[0x15]`, high nibble → `dev[0x16]`;
   * `rec[2]` low nibble → `dev[0x18]`, high nibble → `dev[0x14]`;
   * `dev[0x19] = 0xd << dev[0x18]` when `dev[0x18] ∈ {1,2}`.
4. Special-cases ID byte `sp[0]==0x98` and `sp[1]∈{0x83..0x87,0x93..0x97}`.

3-byte records (`0x4002FEB4`, indices 0..8):

| idx | bytes | idx | bytes |
|----|-------|----|-------|
| 0 | `00 00 00` | 5 | `23 C6 01` |
| 1 | `22 B6 01` | 6 | `23 C6 11` |
| 2 | `22 B6 11` | 7 | `23 C7 01` |
| 3 | `23 B6 01` | 8 | `23 C7 11` |
| 4 | `23 B6 11` | | |

Pair table (`0x4002FECF`, `{code,index}`): `83:3 93:4 84:3 94:4 85:3 95:4 86:3
96:4 87:3 97:4 F0:1 C0:2 A0:1 B0:2 A2:1 B2:2 F2:1 C2:2 F1:1 C1:2 A1:1 B1:2
DA:3 CA:4 AA:3 BA:4 DC:3 CC:4 AC:3 BC:4 D3:5 C3:6 A3:5 B3:6 D5:7 C5:8 A5:7
B5:8 D7:7 C7:8 A7:7 B7:8 DE:7 CE:8 AE:7 BE:8` then `00`.

`0x4002EE94(dev)` is the alternative geometry path (used inside `0x4002E6BA`)
which reads geometry directly (no ID lookup): it stores `dev[0x14]`, `dev[0xC]`,
`dev[0x10]`, `dev[0x12]`, `dev[0x15]`, `dev[0x16]`, `dev[0x18]`, `dev[0x19]`.

### 5.3 cmd 8 (`0x4002BC11` open)

`0x4002BC11` zeroes a 4-word request, calls `0x4002A2CA(0x0002F9F8, sp)`; if
`sp[1]==2`, sets `outer[8]=1`, `outer[5]=1`, `outer[0xC]=1`,
`outer[0x10]=0xC0000000`, `outer[0x14]=3`, `outer[0x18]=0`, `outer[0]=1`.
`0x4002BC59` read: `dst = memcpy(outer[0x10] + off<<9, len<<9)` — i.e. another
memory-mapped source with base `0xC0000000`. *(inferred)*: a distinct
memory-mapped boot source (not the same as cmd 1/2). The literal table at
`0x4002F9F8` (`80800C00 C3 110D1624 …`) is the associated descriptor — not
decoded.

---

## 6. Media-class probe

`0x40028D4E(out)` (called from `0x40028DB6` when `flags&1`):

1. `0x40029AE6(5, 0)` — PRCM module 5 enable.
2. `0x4002B6C0(0)` — clock enable on base table entry 0.
3. `0x4002B6DC(0, sp)` — read a 32-bit value from base `0x4A318000`-class block
   (registers `+0x40`, `+0x24`, `+0x44`, `+0x3C`; polls `+0x44`).
4. Classify `*sp`: `>0x3D6`→7, `≥0x2B4`→5, `>0x226`→4, `≥0x1B8`→3, else 1.
5. Store class into `*out`; `0x40029CA0(5,0)`.

Base table for these helpers is at `0x4002F9EC`: `0x4A318000`, `0x48032000`,
`0x48086000` (further entries are unrelated data). *(inferred)*: this is a
variant/device identification read; the peripheral identities were not confirmed
(§10).

---

## 7. Device selection flow (how the pieces connect)

1. **Reset → Thumb main** `0x40028A18` → `0x40028DB6` → cold boot calls
   `0x40028D4E` (media-class probe) and `0x400296…/0x40029F26/0x40029CD0`, then
   `0x40028C44`; then the **config parser** `0x40028A7C` (`20-config-parser.md`).
2. **Parser inner loop** (`20-config-parser.md` §2): for each of 8 command bytes
   `c`: `1..8` → `0x4002AD5C(flags, c, struct+0x64)` (media handler);
   `'A'..'G'` → `0x4002AFFE(…)` (image handler). The table is mutated in place, so
   the effective read order is dynamic.
3. **Media handler** `0x4002AD5C` selects the operation vector at
   `0x4002F578 + c*12` (§4), initialises the media descriptor at `struct+0x98`
   (`[4] = c`), and runs open → read → match/dispatch.
4. **Media command = device type.** For `c ∈ {5,6,7}` the vector's open routine is
   `dev_open` (`0x4002BA54`), and `c` is stored as `outer[4]`. `0x4002EC38` then
   maps `5,6,7,8 → controller index {0,1,2,6}` and enables the corresponding
   PRCM module. So the byte the config parser feeds *is* the MMC controller
   selector.
5. **dev_open** brings the controller up: `0x4002DC68` (reset) → `0x4002DD2C`
   (CMD0/init stream) → `0x4002EA5E` (card init, §3) → (SD) vendor index-45
   setup → marks `outer[5]`/`outer[0]` open.
6. **Commands 1/2/3/4/8** take the flash/XIP/named-image branches instead.
7. **Named images** `'A'..'G'` are resolved by name against the pointer table at
   `0x4002FA84` (ISSW/X-LOADER/2ND/ULO/MLO/HLO/KEYS/PRIMAPP/R&D/CH/CHSETTINGS/
   CHRAM/CHFLASH/CHMMCSD; `20-config-parser.md` §4.2).

---

## 8. Tables decoded (summary)

| addr | contents | consumer |
|------|----------|----------|
| `0x4002FE20` | 7 MMC controller bases | `0x4002DC68`…`0x4002E060` (index = `r0`) |
| `0x4002FE3C` | 15×4 u16 clock-divider table | `0x4002E8CE` |
| `0x4002FEB4` | 9×3-byte flash geometry (nibble-packed) | `0x4002EF14` |
| `0x4002FECF` | flash device-ID → geometry index (pairs) | `0x4002EF14` |
| `0x4002F578` | media-op vectors (cmd 1..8, stride 12) | `0x40028E2A` / `0x4002AD5C` |
| `0x4002F5E4` | image-op vectors ('A'..'F', stride 20) | `0x40028E4A` / `0x4002AFFE` |
| `0x4002F9C4` | media cmd → progress bit (8 bytes: 40 41 42 43 44 45 47 4B) | `0x4002AD5C` |
| `0x4002F9EC` | media clock-enable bases (0x4A318000, 0x48032000, 0x48086000) | `0x4002B6C0` / `0x4002B6DC` |
| `0x4002F000` | literal `0x0002FECF` (pair-table base) | `0x4002EF14` |
| `0x4002FA84` | boot-image name pointers (14) | `0x4002C254` |
| `0x4002F9F8` | cmd-8 descriptor (`80800C00 C3 110D1624 …`) | `0x4002BC11` |

---

## 9. Evidence list

All from LLVM-23 `llvm-objdump` (Thumb-2 unless noted) + Python `struct`:

* MMC base table `0x4002FE20` (7 words); pool words `0x4002E048=0x0002FE20`,
  `0x4002E04C=0x307F0033`, `0x4002E050=0xFFF0FFFA`, `0x4002E054=0x4A304000`,
  `0x4002E058=0x337F87FF`, `0x4002E05C=0x00200030`, `0x4002E0A4=0x0002FE20`.
* Controller primitives: `0x4002DC68–0x4002DCD4`, `0x4002DCD6–0x4002DD2A`,
  `0x4002DD2C–0x4002DD8C`, `0x4002DD8E–0x4002DDD2`, `0x4002DDD4–0x4002DE5E`,
  `0x4002DE60–0x4002DECA`, `0x4002DECC–0x4002DF12`, `0x4002DF14–0x4002DF56`,
  `0x4002DF58–0x4002DFAE`, `0x4002DFB0–0x4002E046`.
* Card layer: `0x4002E7AC–0x4002E842`, `0x4002E844–0x4002E8CC`,
  `0x4002E8CE–0x4002E902`, `0x4002E904–0x4002E92E`, `0x4002E930–0x4002EA5C`,
  `0x4002EA5E–0x4002EAFA`, `0x4002EAFc`, `0x4002EB06–0x4002EB90`,
  `0x4002EB92–0x4002EC18`, `0x4002EC1A–0x4002EC36`.
* Command-word literals: `0x4002EBA8=0x371A0000`, `0x4002EBAC=0x333A0010`,
  `0x4002EBB0=0x083A0010`, `0x4002EBB8=0x09090000`, `0x4002EBBC=0x081A0000`,
  `0x4002EBC4=0x29020000`, `0x4002EBD0=0x02090000`, `0x4002EBD4=0x031A0000`,
  `0x4002EBD8=0x04040000`, `0x4002EBDC=0x071B0000`, `0x4002EBE0=0x101A0000`,
  `0x4002EBE4=0x061A0000`, `0x4002EBE8=0x01B70200`, `0x4002EBEC=0x061B0000`,
  `0x4002EBF0=0x01B70000`, `0x4002EBF4=0x01B70100`, `0x4002ECA0=0x113A0010`,
  `0x4002ED9C=0x2D3A0010`.
* Clock table `0x4002FE3C–0x4002FEB3` (60 halfwords).
* Device layer: `0x4002BA54–0x4002BB18`, `0x4002BB1A–0x4002BBCE`,
  `0x4002BBD0–0x4002BBFC`, `0x4002EC38–0x4002EC9E`, `0x4002BC11–0x4002BC58`.
* Media vectors `0x4002F578` (8×12), `0x4002F5E4–0x4002F66F`;
  `0x4002F9C4` (8 bytes); `0x4002F9EC` (3 words); literals `0x4002B96C=0x50000084`.
* Flash: `0x4002E25C–0x4002E2C8`, `0x4002E600–0x4002E6E2`, `0x4002E6E4–0x4002E7AA`,
  `0x4002EE94–0x4002EFFE`, `0x4002EFFE=0xBDF82001`, `0x4002F000=0x0002FECF`;
  geometry table `0x4002FEB4–0x4002FECC`, pair table `0x4002FECF–0x4002FF2B`.
* Media probe: `0x40028D4E–0x40028DB4`, `0x4002B6C0–0x4002B70A`.
* Cross-reference scans: LE word / word|1 scans for `0x0002FE20`, `0x0002FE3C`,
  `0x0002FECF`, `0x0002F000`, `0x0002F578`, `0x0002F9C4` over the whole image.

---

## 10. Unknowns / gaps

1. **Clock-divider table semantics** (`0x4002FE3C`). The index derivation
   (`N=bits[2:0]`, `M=bits[6:3]` of `resp[0xc]`) is proven, but which register the
   response word is (CSD TRAN_SPEED assumed) and the meaning of the four columns
   (input clock, multiplier, …) are **not** confirmed.
2. **Command-word flag bits.** Only bits[31:24] (index) and bits[17:16] (response
   length) are decoded; the role of the other flag bits and the low byte (e.g.
   `0x…00 10`) is unknown. Index 45 (`0x2D3A0010`, args `0xA1000000` /
   `0xB1000000`) is not a standard MMC command and is **unidentified** (candidate:
   an eMMC/SD vendor register-access command).
3. **Controller table anomalies.** Entries [1] and [2] are both `0x480B4000`, and
   entry [6] (`0x480B6000`) has no confirmed identity. Whether these are real
   aliases or a table error is unresolved.
4. **Flash register window identity.** `0x5000007C/0x80/0x84` are used as flash
   command/aux registers and `0x08000000` as the memory window; the blocks at those
   offsets were **not** matched to the GPMC (or other) TRM map, and the `0x1E000`/
   `0x1E200`/`0x1E480` command-block offsets are unverified.
5. **Flash geometry decode.** The nibble → field mapping is proven *mechanically*
   but the physical meaning (page size, block size, sector count, plane count) is
   inferred only. Device-ID codes `0x83…0xF0` were not matched to a JEDEC/CFI list.
6. **Media-class probe** `0x40028D4E`: the peripheral at `0x4A318000` /
   `0x48032000` / `0x48086000` and the thresholds `0x1B8/0x226/0x2B4/0x3D6` are
   undecoded; the resulting class 1/3/4/5/7 is not mapped to concrete media.
7. **cmd 8 source** (`0x4002BC11`, base `0xC0000000`) and its descriptor table
   `0x4002F9F8` are not decoded.
8. **Top-level ordering.** The effective media probe/read order is set by the
   mutable 8-byte command table produced by `0x40028E72`/`0x4002A560`; the exact
   default byte sequence and how flags select it (`20-config-parser.md` §2.1) are
   only partly resolved, so "which device is tried first" is not fixed here.
9. **Where type byte 8 originates.** `0x4002EC38` handles 8→controller 6, but the
   media handler only supplies 5/6/7; the source of a type-8 device record was not
   located.
10. **Bus-width helper `0x4002A6A6`** (control-module/pinmux setter using the
    literal at `0x4002A978`) was not fully traced.
