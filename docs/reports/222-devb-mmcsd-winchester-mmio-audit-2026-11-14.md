# 222 — `devb-mmcsd-winchester`: the complete MMIO audit (MMC1/MMC2, L3INIT CM2/PRM, sDMA)

Date: 2026-11-14.  Static, file-only RE: no bench, no USB, no network,
no `tools/` scripts.  All addresses below are reproduced from the one
binary named in §0.

## 0. Target and method

* `local/work/1917/ifs/proc/boot/devb-mmcsd-winchester`
* 85 011 bytes, ELF32 ARM EABI5, `ET_DYN`, stripped; SHA-256
  `7f86323c662c4fcc851f0e52b781b587ce6bf04456c57a9da40093be1841b120`
  (identical to `local/work/ifs1917-tree/proc/boot/devb-mmcsd-winchester`).
* `readelf -l`: `LOAD 0x000000 vaddr 0x00000000 paddr 0x80600000` (RX,
  filesz 0x12a18) and `LOAD 0x012f0c vaddr 0x00013f0c paddr 0x80612f0c`
  (RW).  QNX loads the image at **0x80600000**; in this report **link VA**
  is the ELF's 0-based address and **run VA = link VA + 0x80600000**.
  The *device* VA is not in the file: it is the value `mmap_device_io`
  returns at run time.  The tables below therefore give the **store
  instruction's** link/run VA and the **device register's physical
  address**.
* The code segment has no section headers (`readelf -S` shows only
  `QNX_info`, `QNX_usage`, `.shstrtab`), and this host's `objdump` has no
  ARM backend (`objdump: can't use supplied machine arm`).  The code was
  therefore carved from the RX `LOAD` segment and disassembled with
  Python 3 + Capstone 5.0.7 (ARM, little-endian).  Strings, dynsym,
  PLT/GOT and the QNX relocation tables were decoded with `python3`,
  `strings`, `xxd` and `readelf`.
* The binary contains **no absolute MMIO word anywhere**: a 32-bit-word
  scan of the whole file for 0x4A0xxxxx/0x480xxxxx values returns zero
  hits.  Every device address is either (a) a `movw`/`movt` pair or
  (b) the return of `mmap_device_io()` on a physical address that comes
  from the hardware database/options (the MMCHS and DMA bases) or is a
  compile-time constant (CM2/PRM).
* Four device mappings are made, by `mmap_device_io` (PLT 0x2ab4):

| link VA | call | physical base | length | stored in | log string (VA) |
|---|---|---|---|---|---|
| 0xcd10 | `mmap_device_io` | `[softc+0xa4]` (hwinfo/option; default 0x480B4000) | 0x200 | unit+0x10 | `"%s: MMCHS base mmap_device_io %s"` @0x12738 |
| 0xcdc0 | `mmap_device_io` | **0x4A009300** | 0x100 | unit+0x18 | `"%s: MMCHS L3INIT_CM2 mmap_device_io %s"` @0x1275c |
| 0xce34 | `mmap_device_io` | **0x4A307300** | 0x100 | unit+0x1c | `"%s: MMCHS L3INIT_PRM mmap_device_io %s"` @0x12784 |
| 0xd004 | `mmap_device_io` | `[softc+0xa8]` (hwinfo/option; default 0x48056000) | 0x1000 | unit+0x34 | `"%s: DMA base mmap_device_io %s"` @0x127c0 |

The second mapped block is used with the **sDMA channel register
layout** (§1.4).  The compiled default is `0x48056000` (set at
0x825c–0x8288 when `[softc+0xa0]==0`), which the TRM calls
`PER_TA_GPIO2`; the binary's own usage text documents the sDMA instead,
in two strings: `"devb-mmcsd-winchester mmcsd bw=4,nohs,ioport=0x4809c000,"`
(@0x13b83) and
`"ioport=0x4a056000,irq=115,dma=30,dma=61,dma=62"` (@0x13bc1).  If the
hwinfo/option parser filled `[softc+0xa0]`, that value is used and the
default is not; the register offsets can only be sDMA's (§1.4).

The driver's operations are installed as a **function-pointer table** in
the controller/sim structure (`softc->hw`, the pointer at `[softc+8]`)
by `omap4_attach` (0xcc1c): hw+0x410 = 0xc160, hw+0x414 = 0xc7fc (init),
hw+0x418 = 0xcad4 (power-down/reset), hw+0x41c = 0xc100 (bus width),
hw+0x420 = 0xc414 (clock), hw+0x424 = 0xc22c (block length), hw+0x428 =
0xc264 (command send), hw+0x42c = 0xd3ac (DMA setup), hw+0x430 = 0xd1dc
(start CCB), hw+0x434 = 0xc168 (DMA completion), hw+0x438 = 0xbf80,
hw+0x43c = 0xc00c (PIO), hw+0x440 = 0xc580 (`omap_reset`), hw+0x444 =
0xcb5c (cleanup), hw+0x448 = 0xc96c (`omap_core_pm`).  hw+0x3f4 is not
a pointer: it receives the input-clock value from `[unit+0xc]` at
0xd084–0xd088.  The per-unit struct itself is at `hw+0x40c`; all
`[unit+0xNN]` fields below are in that struct.  Nothing else reaches
MMIO.

---

## 1. Every MMIO store, grouped by target

No store was found to any other device range.  In particular there are
**no** stores to the pad control block (0x4A100000+), GPIO1
(0x4A310000+), GPIO2/3 (0x48055000/0x48057000), the CONTROL_MMC1/
PBIASLITE registers (0x4A100628/0x4A100600), any DPLL or CM divider
register, or the PMIC regulator interface.  See §1.5.

### 1.1 CM2 — L3INIT_CM2, 0x4A009300..0x4A0093FF (mapped 0x100 bytes)

All writes are in `omap_core_pm` (0xc96c).  The instance is selected by
comparing the mapped MMCHS base (`[unit+0x14]`) with 0x4809C000
(0xc98c–0xc99c); MMC1 uses offset 0x28 and MMC2 offset 0x30
(0xc9a0 / 0xcab8).

| link VA | run VA | register | value |
|---|---|---|---|
| 0xc9d4 | 0x806c9d4 | **0x4A009328** `CM_L3INIT_HSMMC1_CLKCTRL` (MMC1) / **0x4A009330** `CM_L3INIT_HSMMC2_CLKCTRL` (MMC2) | `old \| 0x01000002` — **CLKSEL=1** (bit24: 96 MHz DPLL_PER output) and **MODULEMODE=2** (bits 1:0: explicitly enabled) |
| 0xcaa8 | 0x806caa8 | same | `old & ~3` — MODULEMODE=0 (power-off path) |

The RMW preserves every other bit.  Per TRM Table 3-1326/3-1328:
CLKSEL=1 selects the 96 MHz DPLL_PER-derived functional clock;
MODULEMODE=2 is "explicitly enabled … power domain sleep transition
cannot happen" while set.

No write exists to 0x4A009300 (`CM_L3INIT_CLKSTCTRL`), 0x4A009304
(`CM_L3INIT_STATICDEP`) or any other L3INIT_CM2 register.

### 1.2 PRM — L3INIT_PRM, 0x4A307300..0x4A3073FF (mapped 0x100 bytes)

All writes are to the per-module RFF context register, selected the same
way: 0x2c for MMC1, 0x34 for MMC2.

| link VA | run VA | register | value |
|---|---|---|---|
| 0xceb8 | 0x806ceb8 | **0x4A30732C** `RM_L3INIT_MMC1_CONTEXT` (MMC1) / **0x4A307334** `RM_L3INIT_MMC2_CONTEXT` (MMC2) | **2** — W1C of bit1 `LOSTCONTEXT_RFF` |
| 0xca70 | 0x806ca70 | same | **2** — same clear, in the `LOSTCONTEXT_RFF` recovery path only |

TRM Table 3-571/3-575: bit1 `LOSTCONTEXT_RFF`, "W1toClr", reset 1,
set on `L3_INIT_RET_RST`; writing 2 writes 0 to bit8
`LOSTMEM_L3INIT_BANK1`, which has no effect because that bit is also
W1toClr.  The driver never writes bit8.

No write exists to 0x4A307300 (`PM_L3INIT_PWRSTCTRL`), 0x4A307304
(`PM_L3INIT_PWRSTST`), nor to the 0x4A307000–0x4A3072FF area (the driver
does not map it).

### 1.3 The MMCHS register file(s)

`B` = the mapped MMCHS base, **0x4809C000 (MMC1)** or **0x480B4000
(MMC2)**.  The offsets are the TRM's absolute offsets (Table 24-57), not
the +0x100-relative Linux macros:

| offset | register | MMC1 physical | MMC2 physical |
|---|---|---|---|
| 0x110 | `MMCHS_SYSCONFIG` | 0x4809C110 | 0x480B4110 |
| 0x114 | `MMCHS_SYSSTATUS` (read/poll) | 0x4809C114 | 0x480B4114 |
| 0x12C | `MMCHS_CON` | 0x4809C12C | 0x480B412C |
| 0x204 | `MMCHS_BLK` | 0x4809C204 | 0x480B4204 |
| 0x208 | `MMCHS_ARG` | 0x4809C208 | 0x480B4208 |
| 0x20C | `MMCHS_CMD` | 0x4809C20C | 0x480B420C |
| 0x220 | `MMCHS_DATA` | 0x4809C220 | 0x480B4220 |
| 0x228 | `MMCHS_HCTL` | 0x4809C228 | 0x480B4228 |
| 0x22C | `MMCHS_SYSCTL` | 0x4809C22C | 0x480B422C |
| 0x230 | `MMCHS_STAT` | 0x4809C230 | 0x480B4230 |
| 0x234 | `MMCHS_IE` | 0x4809C234 | 0x480B4234 |
| 0x238 | `MMCHS_ISE` | 0x4809C238 | 0x480B4238 |
| 0x240 | `MMCHS_CAPA` | 0x4809C240 | 0x480B4240 |
| 0x254 | `MMCHS_ADMAES` (read only on MMC1/2) | 0x4809C254 | 0x480B4254 |
| 0x258 | `MMCHS_ADMASAL` | 0x4809C258 | 0x480B4258 |

The stores, in execution/address order.  `old` = value read from the
register immediately before; `[unit+0xNN]` = field of the per-unit
struct filled by attach/hwinfo/options.

#### Init callback 0xc7fc (`unit+0x414`)

| link VA | run VA | register | value |
|---|---|---|---|
| 0xc81c | 0x806c81c | B+0x234 `IE` | **0** |
| 0xc82c | 0x806c82c | B+0x110 `SYSCONFIG` | `old \| 2` — **SOFTRESET** |
| 0xc4f4 | 0x806c4f4 | B+0x22C `SYSCTL` | `old \| 0x01000000` — **SRA** (called from 0xc850) |
| 0xc89c | 0x806c89c | B+0x12C `CON` | **0x00000600** if `[unit+0x48]==0`, else **0x00100600** — DVAL=3 (bits 10:9) and, with an ADMA buffer mapped, DMA_MNS=1 (bit20) |
| 0xc8a4 | 0x806c8a4 | B+0x228 `HCTL` | **0x00000C00** (MMC1: SDVS=6 = 3.0 V) or **0x00000A00** (MMC2: SDVS=5 = 1.8 V); `+0x10` when ADMA is mapped (DMAS=2) |
| 0xc8a8 | 0x806c8a8 | B+0x240 `CAPA` | `(old & ~0x07000000) \| (MMC1 ? 0x07000000 : 0x04000000)` — MMC1 advertises VS18\|VS30\|VS33, MMC2 advertises **VS18 only** |
| 0xc8cc | 0x806c8cc | B+0x228 `HCTL` | `previous \| 0x100` — **SDBP=1** |
| 0xc8dc | 0x806c8dc | B+0x238 `ISE` | **0x33FF87FF** |
| 0xc8e4 | 0x806c8e4 | — | `delay(10)` — 10 ms |

`omap_core_pm` (0xc96c) also writes `MMCHS_SYSCONFIG` on every power
transition (this is the "set SYSCONFIG idle/clock-activity bits" step):

| link VA | run VA | register | value |
|---|---|---|---|
| 0xca1c | 0x806ca1c | B+0x110 `SYSCONFIG` | `(old & ~0x331D) \| 0x321D` — AUTOIDLE=1, ENAWAKEUP=1, SIDLEMODE=3 (smart-idle wakeup-capable), CLOCKACTIVITY=2, MSTANDBY=3 |
| 0xca9c | 0x806ca9c | B+0x110 `SYSCONFIG` | `old & ~0x331D` (power-off path; same fields cleared) |

#### Clock programming 0xc414 (`unit+0x420`)

| link VA | run VA | register | value |
|---|---|---|---|
| 0xc47c | 0x806c47c | B+0x22C `SYSCTL` | `(old & ~0x000F0005) \| 0x060E0000` — SRC=1, SRD=1, DTO=0xE, ICE=0/CICE=0 |
| 0xc494 | 0x806c494 | B+0x22C `SYSCTL` | `(prev & 0xFFFF003F) \| 1 \| (CLKD << 6)` — ICE=1 and the computed divider |
| 0xc4b8 | 0x806c4b8 | B+0x22C `SYSCTL` | `prev \| 4` — **CICE=1** (card clock on) |
| 0xc4f4 | 0x806c4f4 | B+0x22C `SYSCTL` | `old \| arg` — the shared FSM-reset helper, called with 0x04000000 (SRD), 0x02000000 (SRC) and 0x01000000 (SRA) |

#### Command send 0xc264 (`unit+0x428`)

| link VA | run VA | register | value |
|---|---|---|---|
| 0xc290 | 0x806c290 | B+0x12C `CON` | `old \| 2` — **INIT** stream, when descriptor halfword[1] bit0 is set |
| 0xc29c | 0x806c29c | B+0x12C `CON` | `old` (restore) after `delay(10)` |
| 0xc2ac | 0x806c2ac | B+0x230 `STAT` | **0x117F8033** (W1C clear) |
| 0xc31c | 0x806c31c | B+0x234 `IE` | **0x110F8001**, or `[unit+0x2c] \| 0x11400000 \| 0x003F8000` for a data command |
| 0xc328 | 0x806c328 | B+0x208 `ARG` | command argument (descriptor word[1]) |
| 0xc330 | 0x806c330 | B+0x20C `CMD` | `cmd_index<<24 \| flags`; command type 0b11 (bits 23:22) is added for CMD12, data/transfer bits come from the descriptor |

#### PIO 0xc00c/0xc014 (`unit+0x43c`)

| link VA | run VA | register | value |
|---|---|---|---|
| 0xc054 | 0x806c054 | B+0x234 `IE` | **0x20** (read path, buffer not ready) |
| 0xc0b0 | 0x806c0b0 | B+0x234 `IE` | **0x10** (write path, buffer not ready) |
| 0xc0f0 | 0x806c0f0 | B+0x234 `IE` | **2** (transfer complete, conditional) |
| 0xc0d4 | 0x806c0d4 | B+0x220 `DATA` | one 32-bit word per loop iteration (write path) |

#### Bus width 0xc100 (`unit+0x41c`)

| link VA | run VA | register | value |
|---|---|---|---|
| 0xc14c | 0x806c14c | B+0x228 `HCTL` | `(old & ~2) \| (width==4 ? 2 : 0)`; when the unit flag bit 0x1000 is set, also `& ~4` |
| 0xc150 | 0x806c150 | B+0x12C `CON` | `(old & ~0x80000 & ~0x20) \| (width==8 ? 0x20 : 0) \| (DDR ? 0x80000 : 0)` |

#### Block/transfer 0xbf80 and 0xd1dc (`unit+0x438` and start-CCB)

| link VA | run VA | register | value |
|---|---|---|---|
| 0xc000 | 0x806c000 | B+0x204 `BLK` | `[unit+0x50] \| (blocks << 16)` |
| 0xd384 | 0x806d384 | B+0x204 `BLK` | `[unit+0x50] \| (nblks << 16)` |
| 0xd394 | 0x806d394 | B+0x258 `ADMASAL` | `[unit+0x4c]` — the physical ADMA descriptor address obtained by `mphys()` at 0xcf90 |

#### Reset/ISR 0xc580 (`unit+0x440`)

| link VA | run VA | register | value |
|---|---|---|---|
| 0xc5b8 | 0x806c5b8 | B+0x230 `STAT` | `(IE \| 0x8000) & STAT` — W1C of enabled pending status bits |
| 0xc7d8 | 0x806c7d8 | B+0x234 `IE` | **0** (error path) |
| (via 0xc4f4) | | B+0x22C `SYSCTL` | `old \| 0x04000000` (SRD) and `old \| 0x02000000` (SRC), each polled until it self-clears |

#### Power-down 0xcad4 (`unit+0x418`)

| link VA | run VA | register | value |
|---|---|---|---|
| 0xcafc | 0x806cafc | B+0x238 `ISE` | **0** |
| 0xcb04 | 0x806cb04 | B+0x234 `IE` | **0** |
| 0xcb08 | 0x806cb08 | B+0x238 `ISE` | **0** (the writeback pointer is reused) |
| 0xcb14 | 0x806cb14 | B+0x110 `SYSCONFIG` | `old \| 2` — SOFTRESET, followed by a `SYSSTATUS` poll |
| (via 0xc4f4) | | B+0x22C `SYSCTL` | `old \| 0x01000000` — SRA |

No store exists to `MMCHS_CUR_CAPA` (0x248), `FE` (0x250), `CSRE`
(0x124), `SYSTEST` (0x128), `PWCNT` (0x130), `AC12` (0x23C) or the
`HL_` registers at 0x00/0x10.  `RSP10/32/54/76` (0x210–0x21C) and
`PSTATE` (0x224) are read only.

### 1.4 sDMA channel block

`D` = the second `mmap_device_io` base (§0).  The driver computes a
channel pointer `C = D + 0x60*ch`, where `ch = [unit+0x3c]` (default
0x17 at 0x82b4; attach computes the pointer at 0xd068–0xd074).  The
registers it uses are the TRM's `DMA4_*i` block at offset
`0x80 + 0x60*i` (Table 16-x, sDMA mapping summary):

| offset | register |
|---|---|
| C+0x80 | `DMA4_CCRi` |
| C+0x84 | `DMA4_CLNK_CTRLi` |
| C+0x88 | `DMA4_CICRi` |
| C+0x8C | `DMA4_CSRi` |
| C+0x90 | `DMA4_CSDPi` |
| C+0x94 | `DMA4_CENi` |
| C+0x98 | `DMA4_CFNi` |
| C+0x9C | `DMA4_CSSAi` |
| C+0xA0 | `DMA4_CDSAi` |
| C+0xA4 | `DMA4_CSEIi` |
| C+0xA8 | `DMA4_CSFIi` |
| C+0xAC | `DMA4_CDEIi` |
| C+0xB0 | `DMA4_CDFIi` |
| C+0xBC | `DMA4_CCENi` (read) |

Stores:

| link VA | run VA | register | value |
|---|---|---|---|
| 0xcbd8 | 0x806cbd8 | C+0x80 `CCR` | **0** (cleanup) |
| 0xcbe8 | 0x806cbe8 | C+0x8C `CSR` | **0x1FFE** (W1C clear, cleanup) |
| 0xc208 | 0x806c208 | C+0x80 `CCR` | **0** (completion path, 0xc168) |
| 0xc210 | 0x806c210 | C+0x8C `CSR` | **0x1FFE** (completion path) |
| 0xd3f8 | 0x806d3f8 | C+0x8C `CSR` | **0x1FFE** |
| 0xd404 | 0x806d404 | C+0x94 `CEN` | `bytes >> 2` |
| 0xd410 | 0x806d410 | C+0x98 `CFN` | **1** |
| 0xd418 | 0x806d418 | C+0xA4 `CSEI` | **1** |
| 0xd420 | 0x806d420 | C+0xAC `CDEI` | **1** |
| 0xd42c | 0x806d42c | C+0x88 `CICR` | **0** |
| 0xd444 | 0x806d444 | C+0x90 `CSDP` | **0x0001C002** (read path) |
| 0xd468 | 0x806d468 | C+0x80 `CCR` | `([unit+0x40] & 0x1F) \| 0x01044020 \| (([unit+0x40]>>5)<<19)` |
| 0xd478 | 0x806d478 | C+0x9C `CSSA` | `[unit+0x14] + 0x220` (MMCHS_DATA physical address) |
| 0xd480 | 0x806d480 | C+0xA0 `CDSA` | transfer buffer address |
| 0xd490 | 0x806d490 | C+0xA8 `CSFI` | `[unit+0x50] >> 2` |
| 0xd4a0 | 0x806d4a0 | C+0x90 `CSDP` | **0x00000182** (write path) |
| 0xd4c0 | 0x806d4c0 | C+0x80 `CCR` | `([unit+0x44] & 0x1F) \| 0x00041020 \| (([unit+0x44]>>5)<<19)` |
| 0xd4c8 | 0x806d4c8 | C+0x9C `CSSA` | transfer buffer address |
| 0xd4d8 | 0x806d4d8 | C+0xA0 `CDSA` | `[unit+0x14] + 0x220` (MMCHS_DATA physical address) |
| 0xd4e8 | 0x806d4e8 | C+0xB0 `CDFI` | `[unit+0x50] >> 2` |
| 0xd4f4 | 0x806d4f4 | C+0x80 `CCR` | `old \| 0x80` (channel enable) |

`CSR` is written at 0xc210 and 0xd3f8 and is also read at 0xc1a0/0xc1e8
for the completion poll; `CCEN` is read at 0xc18c; neither is written
elsewhere.

### 1.5 Targets with zero stores

| target | evidence |
|---|---|
| pads / CONTROL_PADCONF 0x4A100000–0x4A1007FF | no `movt #0x4a10` and no 0x4A10xxxx word anywhere; no `mmap_device_io` on it |
| `CONTROL_MMC1` 0x4A100628, `CONTROL_PBIASLITE` 0x4A100600 | not present as constants or mappings |
| GPIO1 0x4A310000 / GPIO2 0x48055000 / GPIO3 0x48057000 | no constants; the only 0x4805xxxx constant is the DMA default 0x48056000 |
| CM2 L4PER (0x4A009000 block; **0x4A009560** MMCSD5 `CLKCTRL`) | not mapped; the CM2 mapping is 0x4A009300 + 0x100 |
| PRM 0x4A307000–0x4A3072FF and L4PER_PRM 0x4A307400+ (incl. **0x4A307564** MMCSD5) | not mapped; the PRM mapping is 0x4A307300 + 0x100 |
| DPLL / CM divider registers | only the per-module CLKCTRL and the MMCHS-internal CLKD are written |
| PMIC regulator (`/dev/pmic/regulator`, `regulator_info`) | no such string or import; only the two raw I²C writes of §4, skipped on this board |
| MMCHS5 register file 0x480D5000 | no 0x480Dxxxx constant or mapping |

---

## 2. Which MMCHS instances does it own?  Does anything touch MMC5?

**MMC1 (0x4809C000) and MMC2 (0x480B4000), and nothing else.**

* `omap4_attach` (0xcc1c) takes the controller physical base from the
  hwinfo/option field `[softc+0xa4]` (0xcd00), maps 0x200 bytes at it
  (0xcd10), then compares it at 0xcd88–0xcda4:
  * `0x4809C000` → MMC1 branch;
  * `0x480B4000` → MMC2 branch;
  * **anything else branches to 0xd15c, which returns 1 (failure)**.
* The defaults installed when no option/hwinfo value is present are
  `[softc+0xa4] = 0x480B4000`, `[softc+0xf4] = 0x200` (0x825c–0x826c),
  i.e. MMC2/eMMC.  The boot script runs the driver with **no arguments**
  (`local/work/1917/ifs/proc/boot/.script`: `"devb-mmcsd-winchester"`
  alone), so the live unit uses the hwinfo/default instance.
* There is **no loop over an instance array** and no computed instance
  index: the base is a stored pointer/field, compared against two
  constants.  A hypothetical MMC5 hwinfo node would be rejected at
  0xcda4.
* **0x480D5000 / 0x4A009560 / 0x4A307564 never appear in the image.**
  A scan of every aligned 32-bit word for 0x480D5xxx, 0x480ADxxx,
  0x480D1xxx, 0x4A00956x and 0x4A30756x returns no hit; the `movw`/
  `movt` pair list contains no 0x480d, 0x480a, 0x4a0095 or 0x4a3075
  high half.  So the answer to "even indirectly, e.g. a loop that would
  include 5" is **no**.

The WL1283's MMC5 is owned, in QNX, by `devnp-ti1283.so`; this driver
never touches it.

---

## 3. The domain / LOSTCONTEXT question (the important one)

### 3.1 Exactly what is cleared

Only the **per-module RFF context-loss flag** of the instance being
attached:

* MMC1: `RM_L3INIT_MMC1_CONTEXT`, **0x4A30732C**, bit1
  `LOSTCONTEXT_RFF` (TRM Table 3-571).
* MMC2: `RM_L3INIT_MMC2_CONTEXT`, **0x4A307334**, bit1
  (TRM Table 3-575).

The write is the constant **2** (0xceb8 and 0xca70), i.e. write-1-to-clear
of bit1 only.  The other W1C bit in the same register,
`LOSTMEM_L3INIT_BANK1` (bit8), is not set by the driver (writing 0 to it
has no effect).  There is **no write to `PM_L3INIT_PWRSTCTRL`
(0x4A307300), `PM_L3INIT_PWRSTST` (0x4A307304) or
`CM_L3INIT_CLKSTCTRL` (0x4A009300)**, so this is not a domain-level
"clear the whole L3INIT context" operation.

### 3.2 Order

1. **`omap4_attach`:** map MMCHS (0xcd10), CM2 (0xcdc0), PRM (0xce34);
   then, before enabling any clock, write **2** to the instance's
   `RM_L3INIT_MMCx_CONTEXT` (0xceb8).  This clears a stale flag left by
   the boot/power-on reset.
2. **`omap_core_pm` power-on (0xc96c):**
   * set `CM_L3INIT_HSMMCx_CLKCTRL` = `old | 0x01000002` (0xc9d4):
     CLKSEL=1 (96 MHz), MODULEMODE=2;
   * poll IDLEST (bits 17:16) until 0, 10 000 iterations × 1 µs
     (0xc9e0–0xca00);
   * program `MMCHS_SYSCONFIG` = `0x321D` (0xca1c);
   * **then** read `RM_L3INIT_MMCx_CONTEXT` and test bit1 (0xca24–
     0xca2c).  If bit1 is set the driver logs
     `"%s: LOSTCONTEXT_RFF"` (format string @0x1270c, function name
     `omap_core_pm` @0x1269c), calls 0xc8f4 to re-run the full
     controller init, and finally writes **2** again (0xca70).
3. **`omap_core_pm` power-off:** no PRM access at all; only
   `CM_L3INIT_HSMMCx_CLKCTRL = old & ~3` (0xcaa8).

The normal boot path therefore sees the flag already 0 (cleared in
step 1) and does not re-initialise in `omap_core_pm`; the framework's
init callback (0xc7fc, §1.3) performs the full MMCHS init afterwards.
After a low-power transition that lost the RFF context, the second
power-on sees bit1 set and re-runs the init before clearing it.

### 3.3 Consequence for another module in the same domain

MMC1 and MMC2 are in **PD_L3_INIT** (TRM: HSMMC1/HSMMC2 → PD_L3_INIT);
MMC5 is in **PD_L4_PER** (TRM: HSMMC5 → PD_L4_PER).  The driver's
domain-level writes are:

* per-module CLKCTRL on MMC1/2 only — MODULEMODE=2 prevents the L3INIT
  power domain from entering a sleep transition while those two modules
  are enabled (TRM wording), so the *domain* is kept out of sleep; but
  this is an enable of its own modules, not a disable of anyone else's;
* per-module RFF context clear on MMC1/2 only.

It does **not** clear another module's context, does not gate another
module's clock, and does not change the L3INIT power state.  For MMC5
(different domain) there is *no* effect whatsoever: the driver does not
even map the CM2 L4PER page (0x4A009000) or the PRM L4PER page
(0x4A307400) that contain MMCSD5's `CLKCTRL`/`CONTEXT`.

If one wants the equivalent register for MMC5, the TRM names it:
`CM_L4PER_MMCSD5_CLKCTRL` **0x4A009560** and
`RM_L4PER_MMCSD5_CONTEXT` **0x4A307564** (bit1 `LOSTCONTEXT_RFF`).
Neither is touched here.

---

## 4. Regulators, clock outputs, pads, I²C

### 4.1 No regulator, no pad group, no CONTROL register

* No write to `CONTROL_MMC1` (0x4A100628), `CONTROL_PBIASLITE`
  (0x4A100600) or any VDDS/VDDS_MMC timer.  The 0x4A100000 block is
  never mapped or addressed.
* No pad muxing.  The board pad database lives in
  `hwio-config-winchester.so` (reports 189/210), not in this binary; the
  only hwinfo imports here are `__hwi_find_string`, `hwi_off2tag` and
  `__hwi_base`, used to read the `"inputclk"` property (string @0x1272c;
  compare at 0xccb0) and the instance fields — not to write pads.
* No GPIO bank write (no 0x4A310000/0x48055000/0x48057000 address).
* No DPLL or CM divider write.  The only "clock" writes are the
  per-module `CM_L3INIT_HSMMCx_CLKCTRL` of §1.1 (CLKSEL/MODULEMODE) and
  the MMCHS-internal `SYSCTL.CLKD` divider of §1.3, which divides the
  already-running functional clock for its own card bus.

### 4.2 The only I²C access — and it is dead code on this board

`omap4_attach` contains two raw TWL6030 writes, via `/dev/i2c0`:

* `open("/dev/i2c0", O_RDWR)` at 0x83f0–0x83fc (path string @0x121ec);
* a three-message `devctlv(fd, 0x80100505, 3, …)` helper at 0x7f60:
  the 16-byte header carries **0x48** (the TWL6030 7-bit slave address)
  at `sp+0x30`, followed by two 1-byte iovecs for the register and the
  value;
* call 1 at **0x8464**: slave **0x48**, register **0xF2**, value
  **0x19**; its failure log is `"%s: Blaze unable to enable pullup"`
  (@0x12218);
* call 2 at **0x84bc**: slave **0x48**, register **0xEE**, value
  **0x00**; its failure log is `"%s: Blaze unable to disable auto-off"`
  (@0x1223c).

Both calls are behind **two** gates in the same function (0x833c–0x84c0):

1. the MMCHS base must **not** be 0x480B4000 (0x8380–0x8390), so only
   the MMC1/SD-slot instance reaches the I²C code; and
2. `strstr(uname_buffer, "Winchester2")` must return NULL
   (0x83cc–0x83e4); a match jumps over the whole I²C block to 0x8504.

The PlayBook's QNX machine string is `…Winchester2…` (the `/base`
scripts branch on `*Winchester2*` / `*OMAP4430*_Winchester2_*`, e.g.
`local/work/1917/base/scripts/startup.sh:1789`), and its eMMC is the
MMC2 instance (the compiled default).  **On the PlayBook gate 2 alone
skips both writes; with the default MMC2 base gate 1 does too.**  They
are a "Blaze" (MMC1) board path, not a Winchester2 path.  They are also
not a regulator or vmmc: they are two board-specific TWL6030 register
writes whose own log strings name them "enable pullup" and "disable
auto-off".

The driver never opens `/dev/pmic/regulator` and has no regulator
strings or imports.  No vmmc/VMMC write exists anywhere in the binary.

---

## 5. Sequencing, delays, polls and retries

These are per-instance; there is no multi-instance loop, so they are
applied only to whichever one of MMC1/MMC2 is attached.

**Init order (0xc7fc):**
`IE=0` → `SYSCONFIG.SOFTRESET` → poll `SYSSTATUS[0]` RESETDONE
(10 000 µs, 0xc844) → `SYSCTL.SRA` → poll (100 000 µs, via 0xc4c8) →
`CON`, `HCTL.SDVS/DMAS`, `CAPA` → clock setup → `HCTL.SDBP=1` →
`ISE=0x33FF87FF` → **`delay(10)` = 10 ms**.

**Clock setup (0xc414):**
`SYSCTL` = SRC|SRD|DTO=0xE (0xc47c) → `SYSCTL` = ICE|CLKD (0xc494) →
poll `SYSCTL.ICS` (10 000 µs) → `SYSCTL.CICE=1` (0xc4b8).

**Per-command (0xc264):**
optional `CON.INIT=1` → **10 ms** → restore `CON` → `STAT = 0x117F8033`
→ `IE`/`ARG`/`CMD`.  A data-command descriptor also programs the block
length via 0xc22c, which does `BLK` then **10 ms** (0xc250–0xc258).

**Two-phase poll helper 0xc33c:** for a register/mask/value pair it
first spins up to `min(timeout,1000)` iterations of `nanospin_ns(1000)`
(1 µs), then, if the timeout exceeds 1000, sleeps in `delay(1)` (1 ms)
up to `timeout/1000` more times.  It returns 0 on match and 1 on
timeout.  Timeouts used here: 10 000 (reset/clock), 100 000 (SRA/SRC/
SRD self-clear), 500 000 (`PSTATE` at 0xc744), 10 000 (`CON`-init
`STAT` check).

**Reset / recovery:**
* `omap_reset` (0xc580) reads `STAT`/`IE`, writes the W1C mask
  `(IE|0x8000)&STAT` back to `STAT`, resets the command/data FSMs with
  SRC/SRD and polls them clear, and reads the four response registers.
* `LOSTCONTEXT_RFF` recovery (§3.2) re-runs `omap_core_pm(1)`,
  the full MMCHS init, the clock setup, the bus width and the block
  length before clearing the PRM bit.
* Power-down (0xcad4) disables `ISE`/`IE`, sets `SOFTRESET`, polls
  RESETDONE, then sets `SYSCTL.SRA` and polls.

**DMA:**
each transfer clears the channel with `CSR=0x1FFE` and `CCR=0`
(0xc168/0xcb5c) or programs `CICR=0`, `CSR=0x1FFE`, `CSDP`, `CEN`,
`CFN`, `CSEI`, `CDEI`, addresses/frames, then sets `CCR|=0x80`
(0xd3ac–0xd4f4).  Completion polls `CSR`/`CCEN` at 0xc1b0–0xc1d8 with
up to 2000 iterations of `nanospin_ns(1000)`.

Which of these a single-instance Linux `omap_hsmmc` on MMC5 would not
perform:
* the **PRM `RM_L3INIT_MMCx_CONTEXT` read/clear** and the associated
  full re-init — Linux's in-tree `omap_hsmmc` does a
  `context_restore` by comparing `CON`/`HCTL`/`SYSCTL`/`CAPA` and
  reprogramming them (`omap_hsmmc_context_restore`, omap_hsmmc.c
  lines 675–737), but it never touches the PRM RFF context register;
* the **10 ms `delay()` after the MMCHS init** and the **10 ms
  `CON.INIT` pulse** per data command (Linux's `send_init_stream()` uses
  `CON.INIT` but polls `STAT` rather than waiting 10 ms, and does not
  restore `CON` on a timer);
* the **two-phase µs-spin/ms-sleep** poll helper and its exact
  timeouts (Linux uses `udelay(1)`/`MMC_TIMEOUT_US` loops).

Everything else on the MMCHS register side (SOFTRESET/RESETDONE, SRA,
SRC/SRD, ICE/CLKD/ICS/CICE, SDBP/SDVS, IE/ISE/STAT) has a Linux
counterpart, so the register-level program alone is not where QNX and
Linux diverge.

---

## 6. Verdict

**Nothing in this binary can explain a WL1283 that is silent under
Linux's MMC5.**

* It owns **only** MMC1 (0x4809C000) and MMC2 (0x480B4000); any other
  instance is rejected at 0xcd88–0xcda4/0xd15c.
* It never addresses MMCHS5 (0x480D5000), `CM_L4PER_MMCSD5_CLKCTRL`
  (0x4A009560) or `RM_L4PER_MMCSD5_CONTEXT` (0x4A307564); its CM2 and
  PRM mappings are 0x100-byte windows at 0x4A009300 and 0x4A307300 and
  do not reach those registers.
* Its domain-level writes are per-module L3INIT CLKCTRL (MMC1/2) and
  per-module RFF context clears (MMC1/2); it never writes a domain power
  state, a DPLL, a divider, a pad, a GPIO bank or a regulator.
* It performs no I²C access on the PlayBook: the only two TWL6030
  writes are gated to MMC1 **and** to a machine string that does not
  contain "Winchester2", and the PlayBook is MMC2 + `…Winchester2…`.

The one *transferable* observation is the QNX driver's explicit
per-module `LOSTCONTEXT_RFF` handling: QNX clears it before enabling
the clock and re-runs the full controller init if it ever comes back
set.  If someone wants to test that class of bug for MMC5 under Linux,
the equivalent register is `RM_L4PER_MMCSD5_CONTEXT` **0x4A307564**
(bit1), and the equivalent clock-enable register is
`CM_L4PER_MMCSD5_CLKCTRL` **0x4A009560**.  That is a Linux-side
question; this binary neither reads nor writes either address, so it
is not evidence for the chip's silence.

---

## 7. Verification of report 190 §3.3, line by line

| 190 §3.3 claim | result |
|---|---|
| "sets SDBP=1" | **verified**: 0xc8cc, `HCTL |= 0x100` |
| "SDVS18" | **verified for MMC2 only** (0xc8a4 writes 0xA00 = SDVS=5 = 1.8 V); MMC1 gets 0xC00 = SDVS=6 = 3.0 V. CAPA: MMC2 VS18 only, MMC1 VS18\|VS30\|VS33. |
| "enables CM2_L3INIT MMC1/2 module clocks" | **verified**: 0x4A009328/0x4A009330, `old \| 0x01000002` (CLKSEL=1, MODULEMODE=2) at 0xc9d4. |
| "clears PRM_L3INIT LOSTCONTEXT" | **verified, but per-module**: `RM_L3INIT_MMC1_CONTEXT` 0x4A30732C / `MMC2_CONTEXT` 0x4A307334, bit1, value 2 (0xceb8, 0xca70). Not a domain-level clear. |
| "writes no CM divider/DPLL" | **verified**: no DPLL or CM divider register is addressed; only module CLKCTRL and MMCHS SYSCTL.CLKD. |
| "touches no regulator" | **verified**: no regulator device/import; the only I²C is the two skipped TWL writes (0xF2=0x19, 0xEE=0x00). |
| "base constants are MMC1/MMC2 only" | **verified**: 0x4809C000/0x480B4000, reject otherwise. |
| "two TWL6030 register writes in the Winchester2 board path" (report 189) | **corrected**: they are gated to **not**-Winchester2 and to MMC1 only (0x8380–0x83e4); on the PlayBook they do not run. |

---

## 8. Reproduction

```
readelf -h -S -l -d local/work/1917/ifs/proc/boot/devb-mmcsd-winchester
strings -a -t x local/work/1917/ifs/proc/boot/devb-mmcsd-winchester
# carve the RX LOAD segment and disassemble with Python 3 + Capstone:
#   offset 0..0x12a18 -> vaddr 0 (RX); offset 0x12f0c -> vaddr 0x13f0c (RW)
python3   # capstone.CS_ARCH_ARM, CS_MODE_ARM|CS_MODE_LITTLE_ENDIAN
```

Every address in this report is a link VA from that disassembly; add
0x80600000 for the run VA.  The MMCHS/CM2/PRM/sDMA register names and
bit definitions are from `hardware/swpu231ap.pdf.txt` Tables 3-571,
3-575, 3-1326, 3-1328 and 24-57.
