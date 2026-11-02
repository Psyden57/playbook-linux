# Session 1 (absolute) — the OMAP4430 boot ROM, the live SAR/IRAM, and the ramloader opcode map

> Numbering: absolute **1** of 12, **pre-Linux** (no Linux-era number).  This note
> covers the boot-ROM phase: the 2026-09-11 dump, the 2026-10-19 static
> consolidation (reports `10`–`62`), and the 2026-10-20 continuation (`63`–`66`)
> plus the first live SAR/IRAM and ramloader/Sachesi work.  Reports `67`+ belong
> to sessions 2–6.  The kexec-phase documents under `docs/history/` are an
> earlier, separate track with their own numbering; they are not the twelve
> sessions described by `NUMBERING.md`.  See `docs/history/READFIRST/Psyden.txt`
> for how the project is run.

Date / span: 2026-09-11 (boot-ROM dump) → 2026-10-20 (live SAR/IRAM and
ramloader map).  Device logs are ART = UTC−3.

Unit state at the end: the reference **64 GB** PlayBook (later identified as
PIN `0x50175127`) was booted in TabletOS; the qconn door was held by
`BerryShell-V4.py hold` and root SSH was used **read-only** with `memdump3`.  No
NVRAM, RPMB, eMMC, flash or boot-chain write was made; the loader was analysed
statically, not uploaded.  The hidden ROM page `0x40027000`–`0x40027FFF` was
still unreadable from the non-secure world, and no JTAG/debug probe was present.
The qconn door was stopped after the live work.  The `0xE9`/`0x2074` live
baseline and the EP1 negative were taken by **session 2** (report `69` §6–§7),
not this session.

## Goal of this session

Understand the OMAP4430 boot ROM at `0x40028000` well enough to decide whether it
is a route to a modified OS or Linux, and close the specific gaps left by the
static pass:

1. reconcile Ghidra 12.1.4's 218 procedures with the established 282-row
   ARM/Thumb mode map, and label the still-undocumented ROM-API entries if an
   external OMAP4 API list exists;
2. finish the boot-media details around `0x4002F9F8`, the `0x4002F7C4` gating,
   and the `MMCHS_CMD` command-word flags;
3. if a secure/JTAG path exists, dump `0x40026F28`–`0x40027FFF` and the live IRAM
   at `0x4030D000` to settle the monitor semantics;
4. when (3) is blocked, use the rooted QNX side to read the non-secure SAR/IRAM
   windows and statically map the signed ramloader, especially undocumented
   commands and the Sachesi Debug/RimBoot modes.

"Done" became: the ROM-API/media questions answered, the Ghidra inventory
reconciled, the hidden page and monitor ABI bounded by live evidence, and a
read-only ramloader opcode map with a safe next probe.  Booting Linux was not a
goal of this session.

## Confirmed (measured, read in a file, or seen on the bench)

### 1. Target, dump provenance, and the boot-ROM window

- The target is `omap4430-bootrom-0x40028000.bin`: **49,152 B (0xC000)**, raw,
  base `0x40028000`, SHA-256
  `fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`.
  It was read as **NS-visible memory** from a rooted QNX device with `memdump3`,
  fresh after a battery pull (DRAM/RTC wiped), and has no file header; the CPU
  sees ARMv7-A little-endian mixed A32/Thumb-2.  The boot alias
  `0x00028000` maps to `0x40028000`; `VBAR` is set to `0x00028000`.
  [`docs/reports/BOOTROM-0x40028000-REPORT.md` §1, §3; `docs/reports/30-monitor-security.md` §3.1]
- A separate lower dump, `bootrom-lower-0x40020000.bin`, is **28,456 B** at base
  `0x40020000` and covers `0x40020000`–`0x40026F24`.  The next word was not
  readable on the device.  The original static pass therefore described
  `0x40026F28`–`0x40027FFF` as a single ~4 KB hole; the live work in §8 corrects
  that.  [`BOOTROM-0x40028000-REPORT.md` §1/§2.3; `30` §5]
- The 48-KB window ends at `0x40033FFF`; `0x40034000` gave SIGBUS on the device.
  The last word of the dump is `0x40033FFC = 0x00000003` (meaning unknown).
  [`BOOTROM-0x40028000-REPORT.md` §1/§4.1; `40-iram-and-tables.md` §3c]
- There is **no symbol table, build metadata or file header**.  Every name in
  the reports (`reset entry`, `Thumb main`, `config parser`, …) is a descriptive
  label assigned during analysis.  [`BOOTROM-0x40028000-REPORT.md` §1]

### 2. Reset → Thumb main → config parser

- `0x40028000` is ARM `b 0x40028910` (`0xEA000242`).  The next seven vector slots
  are `ldr pc,[pc,#0x18]` and load IRAM pointers `0x4030D004`–`0x4030D01C` from
  the literal block at `0x40028024`–`0x4002803C`.  `0x40028020` holds
  `0x1CF48309` (unidentified signature); `0x40028080`–`0x4002809C` are eight
  `b .` hang stubs.  [`BOOTROM-0x40028000-REPORT.md` §3; `40` §4/§5]
- `0x40028910` reads MIDR.  If `MIDR[1:0] != 0` it branches to the CPU1 secondary
  path at `0x400289A0`; otherwise CPU0 sets `CPSR_c = 0xD3`, writes
  `VBAR = 0x00028000`, loads `SP = 0x4030D000`, records `PRM_RSTST`
  (`0x4A307B04`) through `0x4002A4D8`, sets progress bit 0 through
  `0x4002A51C`, executes eight `bl .+4` filler instructions, enables I-cache
  (`SCTLR |= 0x1800`), and branches to `0x4002882C`.
  [`BOOTROM-0x40028000-REPORT.md` §3; `20-config-parser.md` §1; `50-code-map.md` §2a]
- `0x4002882C`/`0x40028834` walk the **four-record copy table** at
  `0x4002FF80`; each record is `{src,dst,len,fn}`.  The four entries are:
  `{0x0002FFC0, 0x4030D000, 0x40, memcpy}`, `{0x00030000, 0x4030D100, 8,
  memcpy}`, `{0x00030008, 0x4030D108, 0x98, memcpy}`, and
  `{0x000300A0, 0x4030D1A0, 0xABC, memset}`.  The last one zeroes the IRAM BSS;
  its source region is 2,748 zero bytes and is never read.  [`40` §1/§3c]
- `0x400288D0` loads `SP = 0x4030D000`, `blx 0x400288C4`, then computes the
  Thumb entry from `0x135 + pc` and `bx`es to **`0x40028A18`**.
  [`BOOTROM-0x40028000-REPORT.md` §3; `50` §2a; `60-mode-map-resolution.md` §4.1]
- Thumb main `0x40028A18` reads the reset reason, sets progress bit 1, enables
  PRCM module 6, runs the WDT block at `0x4A314000` (sysconfig/start/load/stop),
  and calls `0x40028DB6` then the **config parser `0x40028A7C`**.  The parser is
  a **persistent polling loop**, not a normal boot function that returns: each
  outer pass clears the result word to `0xFFFFFFFF`, iterates the 8-byte command
  table, and loops forever.  [`20` §1/§2; `50` §2a]
- Command encoding: bytes `1..8` go to the media handler `0x4002AD5C`; bytes
  `0x41..0x47` (`'A'..'G'`) go to the image handler `0x4002AFFE`; anything else
  is ignored.  The table is mutable: one result path writes `table[r4+1] =
  res>>8`, another zeroes every entry `>= 0x40`.  Result codes observed:
  `0xFFFFFFFF` (absent), `0xF0030002`, `0xF0030003`, and the masked family
  `(res & ~0xFF00) == 0xF0030006`.  [`20` §2]
- The config-block reader `0x4002B64E(type,out)` walks the list at
  `[0x4A326A00]` up to `0x4A327000`, matches a 32-bit header
  `0x00CF00AA | type`, and copies `len` bytes from `p+8`.  Types 1, 2 and 3 are
  requested.  Main clears `*(0x4A326A00)` on a cold reset.
  [`20` §1.2; `BOOTROM-0x40028000-REPORT.md` §4.3]
- The boot-image name pointer table is at `0x4002FA84` (14 entries, terminated
  at `0x4002FABC`): `ISSW`, `X-LOADER`, `2ND`, `ULO`, `MLO`, `HLO`, `KEYS`,
  `PRIMAPP`, `R&D`, `CH`, `CHSETTINGS`, `CHRAM`, `CHFLASH`, `CHMMCSD`.  The
  strings are at `0x4002FF2D`–`0x4002FF7E`.  The `0x4002BC00` record is
  `"MLO        "` followed by the Thumb pointer `0x0002EB93`; it is consumed by
  an entry scan, not by a direct literal reference.
  [`20` §4.1/§4.2; `50` §4.1]

### 3. Boot media: MMC, flash, and command 8

- The MMC controller base table is `0x4002FE20`, seven u32 entries:
  `0x4809C000`, `0x480B4000`, `0x480B4000`, `0x480AD000`, `0x480D1000`,
  `0x480D5000`, `0x480B6000`.  Entries 1 and 2 are duplicates; entry 6 has no
  confirmed identity.  All controller primitives take a controller index in
  `r0`.  [`10-boot-media.md` §2.1]
- MMCHS offsets observed: `SYSCONFIG 0x110`, `SYSSTATUS 0x114`, `CON 0x12C`,
  `BLK 0x204`, `ARG 0x208`, `CMD 0x20C`, `RSP10..76 0x210..0x21C`,
  `DATA 0x220`, `PSTATE 0x224`, `HCTL 0x228`, `SYSCTL 0x22C`, `STAT 0x230`,
  `IE 0x234`, `CAPA 0x240`, `REV 0x2FC`.  [`10` §2.2]
- `0x4002DDD4` writes the command descriptor **verbatim** to `MMCHS_CMD`
  (`0x20C`).  The complete field decode, from TRM Table 24-84 and the observed
  words, is: `INDX[29:24]`, `CMD_TYPE[23:22]`, `DP[21]`, `CICE[20]`,
  `CCCE[19]`, `RSP_TYPE[17:16]`, `MSBS[5]`, `DDIR[4]`, `ACEN[2]`, `BCE[1]`,
  `DE[0]`.  **`DE` is DMA Enable, not "data enable"**: the ROM does CPU FIFO
  transfers (`0x4002DE60` reads `MMCHS_DATA`), so CMD17 `0x113A0010` and the
  vendor CMD45 word `0x2D3A0010` correctly leave `DE = 0`.  CMD2/CMD9 have
  `RSP_TYPE = 1` (four response words); CMD4 `0x04040000` sets the reserved
  bit 18 and the hardware ignores it.  [`10` §2.3; `64-rom-api-and-media-final.md` §2.3]
- Card init part 1 (`0x4002E930`) sends CMD0, CMD8 `0x1AA`, CMD55 (success ⇒
  SD, `dev[4]=2`; failure ⇒ MMC, `dev[4]=1`), then CMD1/ACMD41, CMD2, CMD3.
  MMC uses a fixed RCA `0x1234`; SD takes the RCA from the response.  Part 2
  (`0x4002EA5E`) sends CMD9, CMD4, sets the clock from the response, CMD7,
  CMD16, and computes capacity.  [`10` §3.1/§3.2]
- MMC capacity uses **CMD8 SEND_EXT_CSD** and `SEC_COUNT` at block byte `0xD4`,
  not CMD9/CSD.  SD uses CSD-v2 `C_SIZE << 10`.  The vendor "region" protocol is
  three commands: index 45 `0xA1000000` reads an 8-word table; index 45
  `0xB1000000 | (i<<16)` reads one block per entry; index **43** `0x2B1B0000`
  selects one region.  [`67-ghidra-seeded-starts.md` §3.3; `10` §3]
- The clock-divider table is `0x4002FE3C`: 15 rows × 4 u16, ending exactly at
  `0x4002FEB4`.  The consumer `0x4002E8CE` indexes it as
  `table[M*8 + N*2 - 8] << 1`, with `N = resp[0xc] & 7` and
  `M = (resp[0xc] >> 3) & 0xF`.  The index derivation is proven; the physical
  column meaning is not.  [`10` §3.3]
- The device abstraction maps the boot-device type byte `{5,6,7,8}` to controller
  indices `{0,1,2,6}` at `0x4002EC38`.  The media-op vector table is
  `0x4002F578` (stride 12, commands 1..8).  Commands 1/2 are XIP at
  `0x08000000`; 3/4 are flash/GPMC-class at `0x50000000`; 5/6/7 are MMC/SD;
  8 is a separate memory source at `0xC0000000`.  [`10` §4]
- The flash path reads the device ID with command `0x90`, then searches the
  46-record `{device-ID, geometry-index}` pair table at `0x4002FECF`; the 9
  three-byte geometry records are at `0x4002FEB4`, and the consumer proves the
  relationship with `0x4002FECF - 0x1B = 0x4002FEB4`.  The `0x4002FEE0` block is
  **not a separate table**: it is one byte into record 8 of the pair table.
  [`10` §5; `61-rom-api-and-flash-tables.md` §3.3]
- **`0x4002F9F8` is an EMIF/DDR descriptor, not a media blob or UART table.**
  It is `0x50` bytes: two `0x24`-byte EMIF records, then `+0x48` =
  `DMM_LISA_MAP_0` (written to `0x4E000040`) and `+0x4C` = channel/subchannel
  mask.  Record 0 maps to `EMIF_SDRAM_CONFIG = 0x80800C00`,
  `REF_CTRL = 0x000000C3`, `TIM_1 = 0x110D1624`, `TIM_2 = 0x3058161B`,
  `TIM_3 = 0x000060B2`, `PWR_MGMT_CTRL = 0`, `DDR_PHY_CTRL_1 = 0x901FF416`,
  `DDR_PHY_CTRL_2 = 0`, and `+0x20 = 0x00020123`; record 1 is all zero.  The
  header is `DMM_LISA_MAP_0 = 0`, mask `2` (EMIF1, subchannel 1 only), so this
  dump initialises **EMIF1 only**.  [`64` Part 2.1]
- Command 8 (`0x4002BC10`) calls `0x4002A2CA(0x4002F9F8, sp)`, accepts only
  `return == 0 && sp[1] == 2`, and then sets the device base to
  `0xC0000000` (DDR CS1) and copies from it.  **Command 8 is the
  SDRAM/memory-boot path**, not an opaque mmap.  [`64` Part 2.1]
- **`0x4002F7C4` gating is pinned.**  `0x40028E72(flags,dst,src)` zeroes 8 bytes;
  if `flags & 0x200` it copies four bytes from the stored config block
  (`src+2,+4,+6,+8`).  Otherwise it reads the device type from
  `[0x4A0022C4] & 0xFF`, indexes the six 16-byte records at
  `0x4002F7C4 + (type & 0x1F)*16`, picks the low or high half by bit 5, and
  emits each u16 whose bit 15 is set or, when flags bit 0 is set, whose bit 15
  is clear.  The emitted command bytes are `[media, 0x45]` or `[0x45, media]`,
  where `media ∈ {1,2,3,5,6,8}` and `0x45` is `DEVICE_USB`.  [`64` Part 2.2]
- The WDT-class block at `0x4A314000` is the ROM's watchdog: `+0x10` sysconfig,
  `+0x14` status, `+0x24/+0x2C/+0x30` counter/load, `+0x34` write-posting, and
  `+0x48` the WSPR magic pair.  The external API list forces the direction:
  **`0x4002A454` = `wdtimer_enable` (`0xBBBB`/`0x4444`)** and
  **`0x4002A430` = `wdtimer_disable` (`0xAAAA`/`0x5555`)** — the opposite of
  the earlier descriptive labels in reports `20`/`50`.  [`64` Part 1.4;
  `20` §1.1; `50` §2a]

### 4. IRAM, vectors, IRQ, and the `0xF0` hand-off

- The ROM's first vector table at `0x40028000` redirects every exception into the
  **writable IRAM copy** at `0x4030D000`; the second table at `0x4002FFC0` is
  copied there by bootstrap record 0.  The resulting double indirection is:
  ROM vector slot → IRAM `ldr pc` slot → IRAM target word → handler.  IRAM
  targets are `0x00028090`, `0x00028080`, `0x00028084`, `0x000289E4`,
  `0x000289F0`, `0x00028090`, `0x00029420`, `0x00028098`.
  [`40` §4/§5]
- The copied IRAM layout is: vectors `0x4030D000`–`0x4030D03F`; reset-reason /
  progress ring at `0x4030D040`/`0x4030D060`; IRQ save area `0x4030D100`;
  USB descriptor template `0x4030D108`–`0x4030D19F`; zeroed BSS
  `0x4030D1A0`–`0x4030DC5B`.  The config parser uses `0x4030D1A0` + `0x64`
  (`0x4030D204`) as its result record.  [`40` §2; `20` §5]
- The USB template at `0x40030008` is `0x98` bytes: a 6-byte prefix, an 18-byte
  Device descriptor, and four 32-byte Configuration blocks (512/64/64/512 max
  packet, vendor-specific class, EP1 IN/OUT bulk).  `idVendor`/`idProduct` are
  zero in ROM and are presumably OTP-patched at run time; the patch site was not
  located.  [`40` §3b]
- IRQ entry `0x40029420` is ARM; it switches to the interrupted stack and calls
  the Thumb GIC dispatcher `0x400293C2`.  The dispatcher reads `GICC_IAR`
  (`0x4824010C`), looks up a per-index software table at
  `0x4030DB08 + 4 + (idx<<5)` with four `{id,handler}` entries, calls the
  handler, and writes `GICC_EOIR` (`0x48240110`).  The table ships zeroed, so
  all IRQ registration is runtime.  [`40` §4]
- The `0xF0` path is an **ARM 12-byte veneer interleaved into Thumb** at
  `0x4002CEB0`: `blx 0x4002CE90`, `mov r12,#0xF0`, `smc #0`; there is no
  `bx lr`, and the next bytes are Thumb.  The helper `0x4002CE90` only does
  useful work for selector 1: it returns `0x4030DC1C` and tail-calls the
  dispatcher `0x4002CE36`.  The dispatcher accepts descriptor type 1, 2 or 3:
  type 1 → image operation `0x45` (`'E'`); type 2 → media command 6; type 3 →
  media command 4.  The veneer is installed as the two-word callback
  `{0x0002CEB0, 0x4030DC1C}` and passed to monitor service 0 with
  `r0=3, r1=0, r2=caller context, r3=record`; the public ROM zeroes the
  descriptor fields before passing it.  That supports a
  **registration/hand-off** reading rather than an immediate call; the secure
  side is unobserved.  [`62-monitor-services.md` §4; `30` §1.3]
- The `0xFE` and `0xF1` selectors are alternate entries inside the two
  `smc #1` cache wrappers at `0x4002F4D0`/`0x4002F518`.  They skip the wrapper's
  `push {r4-r12,lr}` and fall into its `pop`, and a whole-file scan finds no
  branch/literal reference to either.  Every live path uses `r12=0`; the
  alternates are dormant.  [`62` §3/§5; `30` §2.1]

### 5. Monitor/SMC and the security boundary

- There are **8 A32 SMC sites** in the 48-KB window and **no Thumb SMC**:
  `0x40028134` (`r12=0x107`), `0x4002813C` (`0x103`), `0x400289C8` (`0x107`),
  `0x4002CEB8` (`0xF0`, `smc #0`), `0x4002F4C8` (`0`), `0x4002F4E8` (`0xFE`),
  `0x4002F510` (`0`), `0x4002F530` (`0xF1`).  The lower dump adds
  `0x400260D8` (`smc #0`, `r12=9`).  The `0x103`/`0x107` services match the
  TRM HLOS table; `0xF0`, `0xFE`, `0xF1` are outside it.
  [`30` §1; `62` §2]
- The 48-KB window contains **no SMC dispatcher/handler** — only call sites.  The
  secure CP15 configuration is in the lower dump: `NSACR = 0x20C00` at
  `0x400260A0`, `MVBAR = 0x0001C1C0` at `0x400260BC`, `VBAR = 0` at
  `0x400260C4`, `SCR = 0x30` at `0x400260CC`, then `smc #0` service 9.
  That is the secure-side tail, outside the NS-readable 48-KB window.
  [`30` §3.2]
- The NS window's CP15 use is limited to MIDR, `VBAR = 0x00028000`, `SCTLR`
  I/Z enable, TTBR0/TTBR1/DACR/MMU for CPU1, and the abort handlers' IFAR/IFSR/
  DFAR/DFSR reads.  `MVBAR`, `SCR`, `NSACR` and `SDER` are absent from this
  window; there is no `c13` access anywhere in either dump.  This is exactly
  what an NS-readable ROM predicts.  [`30` §3.1]
- The abort handlers record the fault address/status in `r0`/`r1` and jump to
  `0x40028088`/`0x4002808C`, i.e. into the `b .` cluster.  They do not recover;
  only a JTAG-observed hang could read `r0`/`r1`.  [`30` §4; `40` §4]
- The original static inference was that the ~4 KB `0x40026F28`–`0x40027FFF`
  region is the SMC dispatch region.  The live work in §8 refines the boundary:
  only `0x40027000`–`0x40027FFF` is protected.  The dispatcher's contents are
  still unobserved.  [`30` §5; `65` §3]

### 6. The ROM API table at `0x40028400`

- `0x40028400`–`0x400284BF` is **48 words**, all odd `0x0002xxxx` Thumb
  pointers; code resumes at `0x400284C0`.  The TRM Figure 27-6 calls it the
  "API table" and says external code calls fixed addresses.  A whole-file scan
  finds no internal reference to the table base — expected for an export table.
  [`61-rom-api-and-flash-tables.md` §2; `BOOTROM-0x40028000-REPORT.md` §4.7]
- Four independent public OMAP4 sources agree on the base/offsets:
  `simu/usbboot-omap4` `include/omap4/omap4_rom.h`,
  `swetland/omap4boot`, `bigunclemax/sync3flash` and `minoca/os`.  They name 17
  of the 48 entries:

  | offset | index | target | external name |
  |---:|---:|---|---|
  | `0x04` | 1 | `0x40028E2A` | `get_driver_mem` |
  | `0x08` | 2 | `0x40028E4A` | `get_driver_per` |
  | `0x40` | 16 | `0x40029300` | `irq_initialize` |
  | `0x50` | 20 | `0x4002A454` | `wdtimer_enable` |
  | `0x54` | 21 | `0x4002A430` | `wdtimer_disable` |
  | `0x58` | 22 | `0x4002A41C` | `wdtimer_reset` |
  | `0x5C` | 23 | `0x4002A478` | `wdtimer_set_timeout` |
  | `0x60` | 24 | `0x4002956C` | `i2c_driver_init` |
  | `0x64` | 25 | `0x40029686` | `i2c_driver_write` |
  | `0x68` | 26 | `0x400295FA` | `i2c_driver_read` |
  | `0x6C` | 27 | `0x40029738` | `i2c_driver_close` |
  | `0x80` | 32 | `0x40028DFC` | `get_device_mem` |
  | `0x84` | 33 | `0x40028E0A` | `get_device_per` |
  | `0x88` | 34 | `0x40028E1A` | `get_device_data` |
  | `0xA0` | 40 | `0x40029AE6` | `cm_enable_module_clocks` |
  | `0xA4` | 41 | `0x40029CA0` | `cm_disable_module_clocks` |
  | `0xA8` | 42 | `0x4002A95A` | `ctrl_configure_pads` |

  The other 31 entries still carry descriptive labels only; the external list
  does not name them.  [`64` Part 1.3]
- The external list forced four corrections: `0x4002A454` is the WDT
  **enable/start** (`0xBBBB`/`0x4444`) and `0x4002A430` is **disable/stop**
  (`0xAAAA`/`0x5555`); `0x4002A95A` is **`CTRL_CONFIGUREPADS`**, not a PRCM
  module enable; `0x4002956C`–`0x40029738` is I2C init/write/read/close; and
  `0x40029AE6`/`0x40029CA0` are the CM enable/disable pair.  [`64` Part 1.4]
- The external headers also validate the record ABIs already inferred in `10`
  §4/`20` §3: `mem_device` (`initialized +0`, `device_type +4`, `trials_count
  +5`, `xip_device +8`, `search_size +0xC`, `base_address +0x10`,
  `hs_toc_mask +0x14`, `gp_toc_mask +0x16`, `device_data +0x18`,
  `boot_options +0x1C`), `mem_driver` (3 methods), `per_handle` (11 fields) and
  `per_driver` (5 methods).  [`64` Part 1.5]
- The image-handler letters `'A'..'F'` are **device IDs `0x41..0x46` =
  UART1, UART2, UART3, UART4, USB, USBEXT** (`0x40` = NULL); `'G'` = `0x47` is
  correctly rejected by `0x40028E4A`.  [`64` Part 1.4]

### 7. Ghidra 12.1.4 vs the 282-row mode map

- REA 6.7.0 + Ghidra 12.1.4 were configured; the target was reopened with
  `open_binary` (`raw-binary` / `arm` / `0x40028000` / `ghidra`) and the SHA-256
  matched.  Ghidra reports **218 procedures**, one segment
  `0x40028000`–`0x40034000`, five strings.  [`63-ghidra-218-reconciliation.md` §1]
- **203 of the 218** are exact starts in the 282-row map; **15** are legitimate
  extra starts/boundaries; **79** map rows are not Ghidra procedures.  The 79
  break down as: 7 `bl .+4` filler, 1 data literal (`0x4002B798` = `0x4A100000`),
  2 mode-switch veneers, 4 API-table-only targets (`0x400284C0`, `0x400285CA`,
  `0x40028604`, `0x40028620`), and 65 genuine starts not reached by recursive
  descent.  Four further `tbb` targets (`0x4002987C`, `0x400298A2`,
  `0x40029A06`, `0x40029A5E`) are absent from **both** inventories and are not
  part of the 79.  [`63` §2/§3/§4]
- **Zero Ghidra procedures lie in the data region**, and there are **zero mode
  contradictions** with the resolved map.  Ghidra independently places
  `0x4002B740` (ARM) before `0x4002B79C` (Thumb) and does not treat `0x4002B798`
  as code.  [`63` §2]
- The authoritative start set is therefore `282 − 7 − 1 − 2 + 15 ≈ 287` genuine
  starts.  The 282 map remains authoritative; Ghidra is a decompilable subset,
  not a replacement.  [`63` §5]
- Evidence: `analysis/rea-ghidra/evidence-bundle-2026-10-19b.json` (22 records,
  191,550 bytes) and `analysis/rea-ghidra/procedure-reconciliation.csv`
  (297 rows = 282 map + 15 Ghidra extras).  [`63` §1/§4]
- The earlier consolidated report's "no native analysis provider" statement is
  superseded for this continuation by the Ghidra addendum; the earlier
  LLVM-derived mode map is still the authority where Ghidra has no function.
  [`BOOTROM-0x40028000-REPORT.md` addendum; `63` §1]

### 8. Live access: SAR window, IRAM, and the real hole boundary

- Host-side enumeration: the PlayBook appeared as `0fca:8010` ("Connect to
  Windows mode"), not bootrom/loader.  The RNDIS interface `enp8s0f3u1` had no
  IPv4 address; a NetworkManager profile (`pb-rndis`) with `169.254.0.2/16`
  made `169.254.0.1` reachable, and the qconn door on TCP **4455** was open.
  [`65-live-playbook-sar-iram.md` §1]
- `BerryShell-V4.py` performed the qconn pairing/handshake and uploaded the
  workspace SSH key (`/home/psyden/rea-work/rsa`); it was then run in `hold`
  mode with 4 s heartbeats.  Plain
  `ssh -i /home/psyden/rea-work/rsa root@169.254.0.1 ...` worked.  The flags
  must come **after** the subcommand (`hold --key ... --pub ...`); putting them
  before `hold` gives `rc=2`.  [`65` §1; `103-clean-kernel-crc-bringup-2026-10-26.md` §3.1]
- All reads used `memdump3`, the QNX `mmap_device_memory` tool
  (`PROT_READ | PROT_NOCACHE | MAP_SHARED | MAP_PHYS`), pinned to CPU0 with
  `on -C 0`.  A bad address produces a **contained SIGBUS** in the tool, not a
  kernel crash.  No register, flash, NVRAM, RPMB or monitor state was written.
  [`65` §1/§2]
- **The SAR/monitor window `0x4A326A00`–`0x4A326CCF` is the CPU1 warm-reset
  trampoline plus context table, not the hidden SMC dispatcher.**
  `0x4A326A04`/`08` point to `0x4A326B00`; `0x4A326A0C = 0x8B872211`;
  ARM code runs `0x4A326B00`–`0x4A326C27`; constants are
  `0x4A326C28 = 1`, `0x4A326C2C = 0x810`, `0x4A326C3C = 1`,
  `0x4A326C40 = 0`; the CPU1 context table is `0x4A326C50`–`0x4A326CCF`;
  CPU0's table at `0x4A326D50` is outside the readable window.  [`65` §4]
- The trampoline calls the monitor with **`r0` = service ID**:
  `r0=0x26`, `r1=0`, `r2=4`, `r3` = pointer to a 4-byte value (`1`),
  `r6=0xFF`, `r12=0`, `SMC #0`.  In the live state `0x26` is called (guard
  `0x4A326C2C = 0x810`) and `0x27` is skipped (guard `0x4A326C40 = 0`).  This
  is a **different ABI** from the public TI HLOS services, which put the
  function ID in `r12`.  [`65` §4.2]
- The restore order is confirmed: CPSR modes IRQ/SVC/ABT/UND/SYS, then ACTLR,
  TTBR0, TTBR1, TTBCR, DACR, PRRR, NMRR, FCSE PID, CONTEXTIDR, TPIDRURW,
  TPIDRURO, VBAR, BPIALL, TLBIALL, BPIALLIS, and finally SCTLR; then
  `ldm ip,{r4-r11,sp,pc}`.  [`65` §4.3]
- **The hole is not a uniform 4 KB.**  `0x40026F28`–`0x40026FFF`
  (`0xD8` bytes) is readable and every word reads zero; the 4 KB page at
  `0x40027000`–`0x40027FFF` SIGBUSes on the first word; the low alias
  `0x00026F28` also SIGBUSes.  The protected page begins at `0x40027000`, not
  at the whole `0x40026F28`–`0x40027FFF` range; whether it contains the
  dispatcher/secure entry is the inference in the next section, not a decoded
  fact.  [`65` §3]
- **Live IRAM `0x4030D000`–`0x4030DFFF`:**
  - `0x4030D000`–`0x4030D01F` is eight `E59FF018` (`ldr pc,[pc,#0x18]`), the
    ROM's copied vector table.
  - `0x4030D020` = `0x80028090`; `0x4030D024`–`0x4030D03C` =
    `0x800101B4`, `0x800101C0`, `0x800101CC`, `0x800101D8`, `0x800101E4`,
    `0x800101F0`, `0x800101FC` — QNX-patched virtual targets.
  - `0x4030D040 = 0x0000001F` (progress bitmap bits 0–4);
    `0x4030D060 = 0x4030D040`; `0x4030D064 = 0x4030D04C`.
  - `0x4030D108 = 0x000032C0` (USB template prefix `C0 32 00 00`);
    `0x4030D10C = 0x01120001` (Device descriptor `01 00 12 01`).
  - `0x4030D204 = 0xFFFFFFFF` (config-parser result initial value);
    `0x4030D238 = 1` (media descriptor); `0x4030D258 = 1` (device-data
    descriptor).
  - **`0x4030DC1C = 0`** — the `0xF0` shared descriptor was never filled on this
    boot.  `0x4030DC5C = 0x0893454D` is the first word past the ROM's `0xABC`
    BSS.  [`65` §5]
- There is **no JTAG/debug probe** on the host: no OMAP/JTAG device, no
  `/dev/ttyUSB*`/`/dev/ttyACM*`, no OpenOCD/J-Link/DS-5/gdb-multiarch, and
  `adb devices -l` is empty.  The secure 4 KB page and the secure-side effects
  of `0xF0`/`0xFE`/`0xF1` therefore remain unreadable/unobserved.
  [`64` Part 3; `65` §6]

### 9. The signed ramloader: main router and opcode map

- Target: `loader_06001A06-00.bin`, **238,128 B**, raw ARM, base
  `0x80100000`, SHA-256
  `c7cc33c796f9518bbc0eb929c6096f2473937ad486c579764aa4c57d8165393f`.
  REA/Ghidra reports one segment `0x80100000`–`0x8013A230`, **988 procedures**,
  92 defined strings.  [`66-ramloader-opcodes-and-sachesi-debug.md` §0/§1]
- The main router is `FUN_80116E28`.  It reads an inner channel-2 frame and
  switches on the byte at frame `+2`; `+3` is the mode/sequence byte returned in
  responses.  Unknown opcodes go to `switchD_801173dc_default` and return
  `0x15`.  Four dispatchers handle the rest: `0x801032AC` (query/info),
  `0x80103B90` (actions/NVRAM/security/OS-region/LED), `0x8011AE30`
  (erase/write/complete), `0x80102DC4` (CREAD), plus direct `0xEF` reboot,
  `0xF7`/`0xF8` write and `0xF9` signature.  [`66` §1]
- **Implemented and useful in this build:** `0x20`, `0xAD`, `0xAE`, `0xAF`,
  `0xB1`, `0xB2`, `0xB3`, `0xB4`, `0xB5`, `0xB7`, `0xBD`, `0xBF`, `0xC0`,
  `0xC3`, `0xC4`, `0xD2`, `0xD3`, `0xD8`, `0xD9`, `0xDB`, `0xDC`, `0xDE`,
  `0xE0`, `0xE4`, `0xE5`, `0xE7`, `0xE8`, `0xE9`, `0xEA`, `0xEC`, `0xED`,
  `0xEE`, `0xEF`, `0xF3`, `0xF7`, `0xF8`, `0xF9`, `0xFD`, `0xFF`.
  [`66` §2]
- **Undocumented but implemented:** `0x39`/`0x3A` CREAD block read; `0xC2`
  0x20-byte status/debug query; `0xD1` 4-byte SVN; `0xDA` NVRAM `0x2027`
  read/delete; `0xDE` (see the contradiction file — later identified as
  BOOT_MODE); `0xE0` installer/OS-region operation reading NVRAM `0x206C`;
  `0xE1` 0x3FF0-byte log/crash buffer; `0xE6`/`0xEB` CREAD variants.
  [`66` §2/§3]
- **Not implemented in this PlayBook build** (unknown-command path, `0x15`):
  `0xDD`, `0xDF`, `0xE2`, `0xE3`, `0xF0`–`0xF2`, `0xF4`–`0xF6`,
  `0xFA`–`0xFC`, `0xFE`.  This matters because the bb10mt client table is a
  superset for other BB10 devices; `0xDD` "FLASH_DUMP" is silently ignored.
  [`66` §2.1]
- **CREAD is RSA-gated, not a free flash read.**  `0xE4` (`FUN_80104970`)
  clears a 0xAF0-byte session, reads host material (0x20/0x10/0x20/0x100 bytes),
  calls `FUN_801292DC`/`FUN_8012A278`, and sets `+0xAEC=1`.  `0xE5`
  (`FUN_80100118` bounds-check, `FUN_80119E0C` page read, `FUN_80104984` crypto
  copy) reads flash pages.  `0x39` calls `FUN_80111ED8` to read a flash block
  into a 0x4000 buffer, then `FUN_80104984`; `0x3A` rejects lengths below
  `0x4001`; `0xE6`/`0xEB` are further variants.  The gate in `FUN_80104984`
  only proceeds when `*(u8 *)(DAT_80104D4C + 0xAEC) == 1`, which only `0xE4`
  sets.  [`66` §3.1]
- `0xB7` `HASH_BOOTROM` (`FUN_8011874C`) returns a **20-byte hash**, not ROM
  bytes; it is a SHA-1-class digest over the loaded image/ROM descriptor.  It
  cannot be inverted to recover the hidden page.  [`66` §3.2]
- `0xDA` reads/deletes NVRAM **`0x2027`** (16 bytes); `0xE8` sets bit 4 of
  NVRAM **`0x2814`** (128 bytes) and is the documented `WIPE_SECURITY` path;
  `0xE1` returns a 0x3FF0-byte log/crash buffer; `0xC2` returns a 0x20-byte
  status block; `0xD1` returns the 4-byte SVN.  [`66` §3.3–§3.6]
- **OS-region lead:** `0xE9` reads NVRAM **`0x2074`** (24 bytes) into response
  `0xFB` (`FUN_8010B954(0x2074, buf, 0x18)`) — read-only and safe.  `0xC0`
  COMPLETE (`FUN_801206C8`) writes NVRAM `0x2074` with `{u32 2, u16 0,
  u8 <first byte>}`, requires the image write cursor at `0x20000` and NVRAM
  **`0x207C`**, and returns `0x14` if `0x207C` is missing.  `0xE0`
  (`FUN_801199D4`) reads NVRAM **`0x206C`** (0x400 bytes), checks device-info
  IDs `0x1F/0x1B/0x1C/0x24`, and calls `FUN_80119538`, which parses a
  0x400-byte installer/region table.  [`66` §3.5]
- The safe conclusion at the end of this session: **start with `0xE9` only**;
  `0xC0`/`0xE0` are NVRAM/flash-writing and need a backup and recovery plan.
  The `resetqnxosregion` command is a **CFP host command**, not a loader opcode;
  it almost certainly drives the `0x2074`/`0x206C` family.  [`66` §3.5/§6;
  `71-hd0-mode-map-and-verifier.md` §7c]

### 10. Sachesi 2.0.3 hidden modes

- Sachesi 2.0.3 exposes four `Boot` commands: `commandInfo = 1`,
  `commandBootloader = 2`, `commandNuke = 3`, `commandDebug = 4`.
  [`66` §4; Sachesi `src/boot.h`]
- **Debug Mode is a transport switch, not a console.**  `commandDebug` sets
  `_found = 8000`; `sendControlMessage` then uses EP1 OUT `0x01` instead of EP2
  `0x02`, and `receiveControlMessage` uses EP1 IN `0x81` instead of EP2 `0x82`.
  The mode-set request itself is sent on EP1, so the bootrom sees the
  `RIM-BootLoader` mode request on the alternate endpoint.  The enum defines
  `kCommandUnkn1 = 0x11` / `kCommandUnkn1Response = 0x12`, but **nothing in
  Sachesi 2.0.3 uses them**.  [`66` §4]
- **RimBoot** (command 2) sets mode 1 (`RIM-BootLoader`), sends the password,
  and uploads the ramloader; it is not mode 2.  **Nuke** (command 3) sets mode 1
  and then mode 4 (`RIM-BootNUKE`, wipe user partition) — destructive, do not
  use.  The `bootloaderModeData` array is `0 RIM REINIT`, `1 RIM-BootLoader`,
  `2 RIM-RAMLoader`, `3 RIM UPL`, `4 RIM-BootNUKE`; mode 2 is defined but never
  selected by the UI.  [`66` §4]
- Sachesi 2.0.3 has **no `resetqnxosregion` implementation**; that is CFP.
  [`66` §4]
- The live EP1 test was **not** run in this session.  It was the next safe
  probe; session 2 ran it and closed it negative: the first EP1 `SETMODE(1)`
  OUT is accepted, nothing returns on `0x81`, the next OUT times out, and
  `0x11` never reaches a live transaction.  [`66` §4/§6; `69` §7]

### 11. Safety rules handed forward (later-confirmed; not tested here)

The live work in this session was read-only.  The rules that later became
canonical, recorded here because the boot-ROM/loader work is where they start:

- **NVRAM is boot-critical.**  Do not write `0x2004`, `0x2040`, `0x2074`,
  `0x2801`/`0x2802`, `0x2819`, or the PIN `0x2002` without a backup and a
  recovery plan.  `0x2074` is `OsRegionV3` (72 bytes: requested/actual 0/1,
  per-region boot counts, max, flags, epoch, version IDs); the loader's `0xE9`
  reads only the 24-byte prefix.  `0x2801` is `NV_OSSTORE_PasswordStruct`, not a
  time/recovery record; `0x2802` is its reserve; `0x2002` is the hardware PIN,
  not the unlock password.  [`83-osregion-semantics.md` §2; `75-nvram-2801-password-record.md`;
  `84-password-vs-pin.md`]
- **RPMB `BOOTCTR_FCTRY` is a 4-byte authenticated record, value 1..0x32.**
  `0` or `>0x32` sends the bootloader to the product/ECDSA path, where a
  self-built `os_type=6` image does not boot.  Every successful factory/CRC
  boot decrements it.  `/dev/rpmb` on a booted OS is the general writer;
  `>`/`>>`/`O_TRUNC` are brick operations.  The agent must not write RPMB.
  [`71` §7b; `72` §3/§5; `92-rpmb-factory-write-runbook.md` §0/§2/§5;
  `116-rpmb-bootctr-plan-2026-10-27.md` §2]
- **CREAD is RSA-gated.**  `0xE4`/`0xE5`/`0x39`/`0x3A`/`0xE6`/`0xEB` need the
  `0xE4` session and host key material; the ramloader is not an arbitrary
  ROM/RAM/flash read.  `0xB7` returns a hash, not bytes.  [`66` §3.1/§3.2]
- **Do not write `0xC0`, `0xE0`, `0xE8`, `0xDA`, `0xC8`, `0xC6`, `0xD4`,
  `0xD5`, `0xEE`, `0xF3`, or `0xFD` without a recovery plan.**  `0xE0`/`0xF3`
  can destroy the MCT; the loader does not verify the payload, only the range.
  [`69` §9; `98-loader-write-and-autoloader.md` §2.5; `101-pre-os-ifs-reflash-2026-10-26.md`]

## Inferred (reasoning from evidence, not directly measured)

- **The hidden 4 KB page contains the SMC dispatcher/secure entry.**  This is
  inferred from the documented boundary, the absence of any dispatch/handler
  code in the two readable windows, and the live SIGBUS at `0x40027000`; no
  bytes of that page were read.  Strength: strong for "the page is protected",
  weak for "it is exactly the dispatcher".  [`30` §5; `65` §3/§6]
- **`0xF0` is a secure image-load/hand-off stage, not a normal function.**  The
  observed structure is descriptor-driven: type 1 → image op `0x45`; types 2/3
  → media commands 6/4; the surrounding literals name the IRAM image range
  `0x40304000`–`0x4030BFFF`; the veneer has no `bx lr`; it is installed as
  `{fn,arg}` and passed to service 0.  The "verify/execute" part is inferred;
  the secure side is unobserved.  [`62` §4.5; `65` §5]
- **`0xFE`/`0xF1` are dormant secure cache/branch-predictor or PPA maintenance
  selectors.**  They sit after `BPIALL` + `DSB/ISB/DMB` in the cache wrappers,
  use `smc #1`, and are unreferenced.  The local context is cache maintenance;
  the secure effect is not observable.  [`62` §5]
- **The boot ROM is a complete USB device stack + MMC/FAT/NAND boot-media
  stack, not a stub.**  26 of the 65 seeded starts are a MUSB-class device-mode
  driver; the MMC/FAT/NAND path explains how the ROM finds and loads `MLO`.
  This is the layer the RIM second stage replaces.  [`67` §2/§3;
  `68-rom-api-undocumented-labels.md` §2]
- **The ROM-side boot chain ends at an image loaded into IRAM or at the
  monitor hand-off, not at a normal return.**  The config parser is a
  persistent polling loop; the image handler loads to `0x40300000` (or
  `0x40304000`), and the `0xF0` path is terminal/registration.  This is why
  "return value" reasoning about the ROM is misleading.  [`20` §2/§3;
  `62` §4]
- **The boot ROM alone is not a route to unsigned Linux.**  It has no
  arbitrary read of the secure page, and the signed ramloader's CREAD path is
  RSA-gated; the useful gate is the RIM second-stage loader/verifier and the
  RPMB/NVRAM state.  This session's static work narrows the problem to the
  `hd0` verifier (product ECDSA vs factory CRC) and `BOOTCTR_FCTRY`.
  Strength: strong as a strategic conclusion; the verifier details are
  sessions 2–6.  [`63`–`66`; `71`; `89`; `95`]
- **The `0x4002F7C4` table is a device-type/command list whose purpose is to
  emit a media command plus USB.**  The six populated records and the
  `[media,0x45]`/`[0x45,media]` output are exact; the "why USB" part is
  inferred from `0x45 = DEVICE_USB`.  [`64` Part 2.2]
- **The `0x4002F9F8` descriptor is the SDRAM init for the memory-boot path.**
  The record-to-EMIF mapping, `DMM_LISA_MAP_0`, and the `0xC0000000` window are
  exact; "memory boot" is the natural reading of a command-8 source at DDR CS1.
  [`64` Part 2.1]
- **`resetqnxosregion` drives the `0x2074`/`0x206C` family.**  CFP's handler
  calls `FUN_00422270(4)` and logs "QNX OSREGION NVRAM record cleared"; the
  loader's `0xE9`/`0xC0`/`0xE0` are the matching read/write/installer paths.
  The exact opcode sequence CFP uses was not traced.  [`66` §3.5; `71` §7c]
- **Sachesi Debug Mode is not a hidden shell.**  The source shows only the
  EP1/EP2 endpoint switch and an unused `0x11`; there is no parser or command
  loop behind the button.  Strength: strong from the source; the live EP1
  negative in session 2 confirms it.  [`66` §4; `69` §7]

## Dead ends and wrong assumptions (valuable — do not skip)

1. **"The ~4 KB `0x40026F28`–`0x40027FFF` region is uniformly non-readable."**
   The live probe found `0x40026F28`–`0x40026FFF` readable and zero, and only
   `0x40027000`–`0x40027FFF` SIGBUS.  The earlier "4 KB hole" was the documented
   boundary plus inference, not a byte-level measurement.  [`30` §5; `65` §3]
2. **"The SAR/monitor window is the hidden SMC dispatcher."**  The live dump
   shows the CPU1 warm-reset trampoline and context table, not a dispatch
   table.  The dispatcher is still at the protected page; the SAR window is
   how the monitor restores CPU1.  [`65` §4]
3. **"There is no secure path at all."**  There is no JTAG, but a rooted QNX
   plus the NS-readable SAR window gives real live evidence; the earlier
   report's "no secure/JTAG path" was too absolute.  The hidden page remains
   blocked.  [`64` Part 3; `65` §1/§6]
4. **"Sachesi Debug Mode is a hidden console."**  The source shows it is only
   the EP1/EP2 transport switch; `kCommandUnkn1 = 0x11` is unused.  The live
   EP1 test (session 2) then showed the endpoint stops being serviced after the
   first OUT.  [`66` §4; `69` §7]
5. **"`0x4002F9F8` is a media blob / UART table."**  It is an EMIF/DDR
   descriptor; its records map to `EMIF_SDRAM_CONFIG`, `REF_CTRL`, `TIM_1..3`,
   `PWR_MGMT_CTRL`, `DDR_PHY_CTRL_1/2`, plus `DMM_LISA_MAP_0` and a channel
   mask.  Command 8 is the SDRAM/memory-boot path.  [`10` §5.3; `64` Part 2.1]
6. **"`MMCHS_CMD` bit 0 is data enable."**  It is **DMA Enable**; the ROM uses
   CPU FIFO transfers, so CMD17/CMD45's `DE=0` is correct.  This also explains
   the earlier "low byte unexplained" caveat.  [`10` §2.3; `64` Part 2.3]
7. **"`0x4002A430` is WDT start and `0x4002A454` is stop."**  The external API
   list reverses it: `0x4002A454` = `wdtimer_enable` (`0xBBBB`/`0x4444`),
   `0x4002A430` = `wdtimer_disable` (`0xAAAA`/`0x5555`).  [`20` §1.1;
   `50` §2a; `64` Part 1.4]
8. **"`0x4002A95A` is a PRCM module enable."**  It is
   `CTRL_CONFIGUREPADS` — 16-bit mux/pad writes at `0x4A100000+0x40..0x1C8`.
   [`50` §2a; `64` Part 1.4]
9. **"Ghidra's 218 procedures are the complete function set."**  They are 203
   exact map starts + 15 legitimate extras; 65 genuine map starts are missed by
   recursive descent, plus 4 API-only targets.  The 282-row map is
   authoritative.  [`63` §2–§4]
10. **"There is no external ROM-API list."**  Four public OMAP4 sources agree on
    the base and 17 offsets; the TRM alone is not the only source.  [`64` Part 1.1]
11. **"`0x4002F7C4` gating is unresolved."**  The flags bit 9 / device-type
    low 5 bits / bit 5 half-select / bit 15 include / flags bit 0 include logic
    is decoded.  [`20` §2.1; `64` Part 2.2]
12. **"`0x4002FEE0` is a separate 16-bit table."**  It is one byte into record 8
    of the pair table at `0x4002FECF`; the real base is `0x4002FECF`, and the
    geometry base is `0x4002FECF - 0x1B`.  [`20` §4.3; `61` §3.3]
13. **"The `0xE9` read is the full `0x2074` record."**  It is a 24-byte prefix;
    the record is 72 bytes (`OsRegionV3`) and the live baseline was taken by
    session 2.  [`66` §3.5; `69` §6; `83` §2]
14. **"`0xDE` is a security/RPMB query."**  At this session the handler was
    observed at `FUN_8011E074` with response `0xFA`, and labelled
    "security/RPMB query"; later reports identify the protocol as BOOT_MODE
    (`{status, mode, count}`) and the live read on the spare returned
    `mode=FACTORY, count=0x32`.  See the contradiction file.
    [`66` §2/§3; `72` §3; `100` §2]
15. **"`0xE0`/`0x206C` and `0xC0`/`0x2074` are the same record."**  `0xC0` writes
    `0x2074`; `0xE0` reads the `0x206C` installer/region table.  Later work
    decoded `0x2074` as `OsRegionV3` and `0x207C` as the radio sibling; the
    three IDs must not be conflated.  [`66` §3.5; `83` §2; `84` §2]
16. **"The `0x4002CEB0` veneer returns normally."**  It has no `bx lr` and the
    next bytes are Thumb; either the `0xF0` service is terminal or the monitor
    unwinds the frame.  The caller-side structure is observed; the return path
    is not.  [`30` §1.3; `62` §4.1]
17. **"The `0x4002BC00` MLO record is a normal literal pointer table."**  No
    word references `0x4002BC00`; the record is consumed by an entry scan, and
    the pointer `0x0002EB93` is called from `0x4002BBB2`.  [`50` §4.1]
18. **"The `0x4002F2F0` Thumb island is part of the ARM cache library."**  It is
    a Thumb veneer into `0x40028904`; the ARM library resumes at
    `0x4002F310`.  [`50` §1; `60` §2]

## Unknown / open questions

- **What is in the protected page `0x40027000`–`0x40027FFF`?**  The SMC
  dispatcher/secure entry is the leading inference; no bytes were read.  A
  secure-context dump, a JTAG probe with OMAP4430 debug access, or a vulnerability
  in the non-secure world would be needed.  [`30` §5; `65` §6]
- **What do `0xF0`/`0xFE`/`0xF1` do on the secure side, and what are the `0xF0`
  descriptor fields `+4/+8/+0xC/+0x10/+0x14/+0x18`?**  The caller-side structure
  is known; the secure effect is not.  [`62` §4/§5]
- **Which 31 ROM-API entries remain unnamed?**  The external list names 17; the
  rest have descriptive labels only.  There is no public per-entry list beyond
  the headers.  [`64` Part 1.3]
- **Who loads the OS image to `0x800FF800`?**  The ROM-side media stack is
  understood, but the second-stage image loader is not in `hd0head.bin`; later
  session 6 shows the only MMC loader there targets a 512 KB TI layout and has
  no caller.  The payload must carry its own MMCHS driver.  [`99-payload-emmc-access.md` §3/§4]
- **What is `0x4002BF20`, and what does the ARM island `0x40028C00` do?**
  `0x4002BF20` is the argument to `0x4002A478` and the address of function
  `0x4002BF21`; `0x40028C00` is the device-type gate, only partly reversed.
  [`20` §7; `50` §2a; `60` §5]
- **What are the media-class thresholds `0x1B8/0x226/0x2B4/0x3D6`, the
  `0x4A318000`/`0x48032000`/`0x48086000` block identities, and the exact
  clock-divider column semantics?**  The indexing is proven; the physical
  meanings are inferred.  [`10` §6/§10]
- **What is the vendor index-45 command (`0x2D3A0010`, args `0xA1000000` /
  `0xB1000000`) in standard terms?**  The flag layout is decoded; the vendor
  meaning is not.  [`10` §10; `64` Part 2.3]
- **What is the ARM `blx` H-bit convention at the five ROM sites?**  The
  decoded targets are valid, but the bit-24 convention disagrees with LLVM's
  display at one site.  [`30` §7; `60` §5]
- **What is the `0x4A326A00` dual use?**  The ROM uses it as a boot-config list
  pointer (`20` §1.2); the live QNX state at the same address is the SAR/CPU1
  trampoline (`65` §4).  These are different boot stages/uses; a live dump at
  the ROM stage would settle whether the ROM list is still present.
- **What is the exact `resetqnxosregion` sequence, and can it be driven by
  `0xE9`/`0xC0`/`0xE0`?**  The CFP handler calls `FUN_00422270(4)` and logs
  "QNX OSREGION NVRAM record cleared"; the loader-side `0xC0` writes `0x2074`
  and requires `0x207C`, while `0xE0` reads `0x206C`.  No write was attempted.
  [`66` §3.5; `71` §7c]
- **Can `0xDE` action 0 be used to read the boot counter, and is its `count`
  the RPMB `BOOTCTR_FCTRY`?**  Later reports identify BOOT_MODE and give the
  12-byte response; the exact relationship to the RPMB record is still partly
  open.  See the contradiction file.  [`72` §3; `100` §2]
- **Is the EP1/Sachesi Debug endpoint usable?**  This session only read the
  source; session 2 tested it and found the first OUT accepted, no IN response,
  and the next OUT timing out.  Treat it as a dead end unless a different
  bootrom build is found.  [`66` §4; `69` §7]
- **What was the boot-ROM dump's exact read method and timing?**  It was
  `memdump3` from a rooted QNX device, fresh post-battery-pull; the raw text
  dumps and reconstructed `.bin` are in `analysis/playbook-live-2026-10-20/`.
  The bootrom's own read timing and whether the dump was taken before or after
  QNX patched the IRAM vectors is not recorded.  [`BOOTROM-0x40028000-REPORT.md`
  §1; `65` §2]

### Not done in this session

- No live `0xE9` read of NVRAM `0x2074`; session 2 took the baseline (`69` §6).
- No CFP `resetqnxosregion`; no after snapshot; no `0xC0`/`0xE0` write.
- No EP1/Sachesi live test; session 2 closed it (`69` §7).
- No loader upload; the loader was analysed statically.
- No NVRAM, RPMB, eMMC or flash write; `memdump3` reads only.

## Superseded / uncertain

- **`0xDE` as "security/RPMB query" — superseded by `72` §3/`100` §2.**  This
  session's report `66` observed the handler `FUN_8011E074` and response
  `0xFA` and labelled it a security/RPMB query.  Later reports identify the
  request as channel-2 `0xDE` with `{u32 action, u32 value}` and the response
  as 12 bytes `{status, mode, count}`; the live spare read returned
  `status=0, mode=FACTORY, count=0x32`.  The label in `66` is too narrow; the
  RPMB/count relationship is still partly open.  See
  `docs/contradictions/66-0xde-security-vs-bootmode.md`.
- **`0xE9` as the full `0x2074` record — refined by `83` §2.**  `0xE9` reads 24
  bytes (the prefix); the record is 72 bytes `OsRegionV3`.  The live baseline
  in `69` §6 is the same 24-byte prefix.
- **`0xE0`/`0x206C` vs `0xC0`/`0x2074` — refined by `83`/`84`.**  `0xC0` writes
  `0x2074`; `0xE0` reads the `0x206C` installer/region table; `0x207C` is the
  radio-region sibling.  Do not treat the three IDs as one record.
- **"The hole is a uniform ~4 KB" — corrected by `65` §3.**  The readable
  `0xD8` tail at `0x40026F28`–`0x40026FFF` is the preceding page; only
  `0x40027000`–`0x40027FFF` is firewalled.
- **"The SAR window is the SMC dispatcher" — corrected by `65` §4.**  It is the
  CPU1 warm-reset trampoline/context table.
- **"There is no secure path" — corrected by `65`.**  No JTAG, but root SSH +
  the SAR window is a real live channel; the hidden page is still blocked.
- **"Sachesi Debug Mode is a hidden console" — superseded by `66` §4 and
  `69` §7.**  It is an EP1/EP2 transport switch; the live endpoint is dead.
- **"`0x4002F9F8` is a media blob" — superseded by `64` Part 2.1.**  It is an
  EMIF/DDR descriptor; command 8 is the SDRAM/memory-boot path.
- **"`MMCHS_CMD` bit 0 is data enable" — superseded by `64` Part 2.3.**  It is
  DMA Enable; CMD17/CMD45's `DE=0` is correct.
- **"`0x4002A430` start / `0x4002A454` stop" — superseded by `64` Part 1.4.**
  The external API list reverses them.
- **"`0x4002A95A` is a PRCM enable" — superseded by `64` Part 1.4.**  It is
  `CTRL_CONFIGUREPADS`.
- **"`0x4002FEE0` is a separate table" — superseded by `61` §3.3.**  It is the
  mis-aligned tail of the `0x4002FECF` pair table.
- **"Ghidra's 218 procedures are the complete set" — superseded by `63`.**  It
  is a decompilable subset; 65 genuine starts are missing.
- **"There is no external ROM-API list" — superseded by `64` Part 1.1.**  Four
  public OMAP4 sources name 17 entries.
- **"`0x4002F7C4` gating is unresolved" — superseded by `64` Part 2.2.**
- **"The `0x4002CEB0` veneer returns normally" — unresolved.**  No `bx lr`;
  the monitor may not return or may unwind the frame.
- **"`0x4002F2F0` is part of the ARM cache library" — superseded by `60` §2.**
  It is a Thumb veneer.
- **"The ROM's `0x4002F9F8` record 1 is used" — corrected by `64` Part 2.1.**
  Record 1 is zero; this dump initialises EMIF1 only.
- **"The low alias `0x00026F28` is readable" — corrected by `65` §3.**  It
  SIGBUSes; only the high alias is mapped.

## Artefacts produced

- `docs/reports/10-boot-media.md` — MMC/flash/media device selection, MMCHS
  command words, card init, clock table, flash geometry.
- `docs/reports/20-config-parser.md` — Thumb main, the persistent config parser,
  the command table, the boot-config block reader, the string/name tables.
- `docs/reports/30-monitor-security.md` — the SMC inventory, the `0xF0`/
  `0xFE`/`0xF1` structure, CP15, abort handlers, the hidden-region inference.
- `docs/reports/40-iram-and-tables.md` — the copy table, IRAM layout, USB
  descriptor template, second vector table, IRQ/GIC path.
- `docs/reports/50-code-map.md` — the whole-ROM function/mode map and call
  graph.
- `docs/reports/60-mode-map-resolution.md` — the byte-verified ARM/Thumb mode
  map and the boundary corrections.
- `docs/reports/61-rom-api-and-flash-tables.md` — the 48-word API table and the
  flash geometry/pair tables.
- `docs/reports/62-monitor-services.md` — the `0xF0` descriptor dispatcher, the
  `0xFE`/`0xF1` alternate entries, and the `r12` service-ID correction.
- `docs/reports/63-ghidra-218-reconciliation.md` — the Ghidra 218 vs 282-row
  reconciliation, the 15 extras and the 79 missing rows.
- `docs/reports/64-rom-api-and-media-final.md` — the external API labels, the
  EMIF/DDR descriptor, the `0x4002F7C4` gating, the complete `MMCHS_CMD` flags,
  and the 2026-10-19 secure/JTAG negative (later refined by `65`).
- `docs/reports/65-live-playbook-sar-iram.md` — the live access path, the SAR
  trampoline, the IRAM state, and the corrected hole boundary.
- `docs/reports/66-ramloader-opcodes-and-sachesi-debug.md` — the ramloader main
  router, the opcode map, CREAD/OS-region, and the Sachesi hidden modes.
- `docs/reports/BOOTROM-0x40028000-REPORT.md` — the consolidated boot-ROM
  report (with the 2026-10-19 Ghidra addendum).
- `docs/reports/METHOD.md`, `docs/reports/HANDOFF.md` — the method and the
  boot-ROM-phase handoff.
- `analysis/rea-ghidra/procedure-reconciliation.csv` — 297 rows (282 map + 15
  Ghidra extras).
- `analysis/rea-ghidra/evidence-bundle-2026-10-19b.json` — 22 records,
  191,550 bytes.
- `analysis/rea-ghidra/loader-opcodes.csv` — the machine-readable ramloader
  opcode table.
- `analysis/rea-ghidra/loader-evidence-bundle-2026-10-20.json` — 26 records,
  238,921 bytes.
- `analysis/playbook-live-2026-10-20/` — `sar-monitor-4A326A00.txt/.bin`,
  `iram-4030D000.txt/.bin`, `prehole.txt`, `hole-start.txt`, `hole-page.txt`,
  `hole-end.txt`, `secure-hole-40026F28.txt`,
  `secure-hole-alias-00026F28.txt`, `ring3-94000000.txt`,
  `qnx-iram-vectors.txt`, and the reconstructed `.bin` files.
- `docs/contradictions/66-0xde-security-vs-bootmode.md` — the `0xDE` label
  disagreement (added by this backfill).
- Tools in the workspace or referenced: `memdump3` (QNX
  `mmap_device_memory` reader), `memtest`, `memw32`, `BerryShell-V3.py`/`V4.py`
  (qconn door), `bbusb` (incomplete Python client), `bb10mt` (working
  reference), Sachesi 2.0.3, REA 6.7.0 + Ghidra 12.1.4.
- On-device state: **none changed**.  The boot ROM, loader, NVRAM, RPMB, eMMC
  and region images were not written.  The qconn door was stopped after the
  live work.
