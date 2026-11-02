# 68 — Labelling the 31 undocumented ROM API entries

Status: item (4) of the `HANDOFF.md` continuation list (label the 31 entries of
the TRM Figure 27-6 table that the external headers do not name).
Date: 2026-10-20 (new session).
Target: `omap4430-bootrom-0x40028000.bin`, 49152 B, base `0x40028000`,
SHA-256 `fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`
(re-hashed before the run; not modified).

**Provenance: Ghidra 12.1.4 headless decompilation; this is NOT REA MCP Evidence.**
The REA MCP server was not used. Peripheral register names/bit fields quoted
below are public-document context from `swpu231ap.pdf` (TRM) and are marked as
such; every code/offset observation is from the dump itself.

Raw pseudocode: `rea-ghidra/rom-api-undocumented-decompiled.txt` (4952 lines,
125 KB), which contains all 31 decompilations, the helper dumps and the raw
data tables.

---

## 1. Method and exact command

Fresh import each pass, raw binary, ARM:LE:32:v7, base `0x40028000`, default
analyzers, then one post-script:

```sh
export GHIDRA_INSTALL_DIR=/home/psyden/tools/ghidra_12.1.4_PUBLIC
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk
S=/tmp/commandcode-1000/-home-psyden-rea-work/e422b20b-4139-4a61-b770-3906053e3769/scratchpad
$GHIDRA_INSTALL_DIR/support/analyzeHeadless $S/ghproj omap4430 \
  -import /home/psyden/rea-work/bootdumps-2026-09-11/omap4430-bootrom-0x40028000.bin \
  -processor "ARM:LE:32:v7" -loader BinaryLoader -loader-baseAddr 0x40028000 \
  -scriptPath $S/scripts -postScript DumpApi.java $S/api-dump.txt -deleteProject
# second/third passes: same options, project ghproj2/ghproj3,
# -postScript DumpExtra.java / DumpApi3.java
```

The post-script, for each of the 31 targets:

1. if a Ghidra function already exists at the target, use it;
2. otherwise clear 4 bytes, set the `TMode` context register to 1 (Thumb),
   run `DisassembleCommand`, then `CreateFunctionCmd(USER_DEFINED)`;
3. decompile (`DecompInterface`, 120 s budget) and record status, entry bytes,
   body size, callees, callers, xrefs and pseudocode.

Result: **16 of 31 targets were already Ghidra functions** after raw-import
auto-analysis (which produced **145 functions**, not the 218 that the prior
REA-provider session reported — the provider seeds more starts than a plain
`-import`); **15 required TMode=1 + disassembly + function creation**
(indices 4, 6, 8, 10, 11, 12, 14, 19, 35, 36, 37, 38, 39, 44, 45). All 31 API
targets are genuine Thumb entries: every created function decoded to a coherent
body and a `push`/`bx lr` frame; none had to be rejected as data.

Helpers were resolved by dumping every callee of the 31 (and their callees) and
then reading the literal pools those helpers use. Examples: `0x40028730 =
0x500001F4` (GPMC_ECC_CONFIG), `0x40029F5C = 0x4A004000` (CM1/CKGEN_CM1),
`0x40029F64 = 0x4A304000` (32K timer), `0x4002A418 = 0x4A004110`
(CM_DLL_CTRL), `0x4002B738 = 0x0002F9EC` (GPTIMER base table),
`0x4002E250 = 0x48241000` (GIC distributor).

---

## 2. The 31 entries

`target = 0x40000000 | (raw & ~1)`; all targets are halfword-aligned Thumb
entries. "Body" is Ghidra's function body size in bytes.

| idx | off | raw word | target | proposed name | evidence (key instructions / registers / globals) | conf |
|---:|---:|---|---|---|---|---|
| 0 | `0x28400` | `0x00028F05` | `0x40028F04` | `cache_op_smc` — cache maintenance by MVA + `smc #1` monitor thunk | 18-B body repacks its 4 args and tail-calls `0x4002F4AC`, which is `mcr p15,0,r0,c7,c5,6` (ICIMVAU by MVA), `dsb/isb/dmb`, `mov r6,#0xFF; mov r12,#0; smc #1`. Four 2-instruction wrappers `0x40028F24/F36/F48/F5A` call it with r0=0..3 and one/two extra args. Callers: only those four wrappers. | high |
| 3 | `0x2840C` | `0x00029D45` | `0x40029D44` | `prcm_dpll_bypass(sel)` — put a DPLL in MN bypass | Writes `(reg & ~7) + 4` to `0x4A004160`/`0x4A004120`/`0x4A008140`/`0x4A008180` = CM_CLKMODE_DPLL_MPU / _CORE / _PER / _USB for sel 0/1/2/3. DPLL_EN=0x4 is "MN bypass, MULT cleared" (TRM PRCM table). Returns 1 for sel>3. Caller: `0x40028C44`. | high |
| 4 | `0x28410` | `0x00028D4F` | `0x40028D4E` | `detect_mem_device_type(out)` — GPTIMER1 capture-timing media/device-type detect | `0x40029AE6(5,0)` enables CM module 5; resets GPTIMER1 `0x4A318000` (`+0x10` bit1 = SOFTRESET, poll `+0x14` bit0); `TCLR(+0x24)=0x2101` (ST + CE + CAPTURE); reads `TCAR2(+0x44) − TCAR1(+0x3C)`; buckets to 1/3/4/5/7 at thresholds 0x1B8/0x227/0x2B4/0x3D7; `0x40029CA0(5,0)` disables the module. Output set matches `mem_device.device_type` values, i.e. the mem-driver table at `0x4002F578` (1/2, 3, 4 = the three GPMC-backed drivers, 5/6/7 = MMC). No ROM-internal caller. | low |
| 5 | `0x28414` | `0x00028C45` | `0x40028C44` | `prcm_clock_config_from_block(desc)` — DPLL/clock-tree configuration (CH block type 0xC0C0C0C1) | 266-B body tests `desc[0]` bits 0,2..11. bit0 required; bits 8/9/7/10 = bypass MPU/CORE/PER/USB (`0x40029D44(0/1/2/3)`); bits 4/5/3/6 = program M/N+dividers (`0x40029D9C`) then lock+wait (`0x40029E1C`); bit 2 writes CM_CLKSEL_CORE `0x4A004100`; bit 11 writes CM_DLL_CTRL `0x4A004110` bit0 via `0x4002A2BC`; the CORE branch first disables CM module 9 clocks (EMIF). Caller: `0x4002C6C0` for CH magic `0xC0C0C0C1` (also progress codes 0x15/0x16). | high |
| 6 | `0x28418` | `0x00029CD1` | `0x40029CD0` | `rom_clock_config_record(idx, out)` — copy ROM PRCM/clock settings record | `memcpy(out, 0x4002F760, 100)`; then patches `out+0x1C` and `out+0x3C` ([6:0] and [18:8]) and `out+0x5C` ([7:0] and [19:8]) from per-slot tables `0x4002F6E0`, `0x4002F6A0`, `0x4002F720` at `idx*8`. idx>7 -> 1, out==NULL -> 6. Sits inside the DPLL cluster but no ROM-internal caller. | low |
| 7 | `0x2841C` | `0x00029EDF` | `0x40029EDE` | `prcm_dpll_is_locked(sel)` | Reads bit0 (ST_DPLL_CLK) of `0x4A004164`/`0x4A004124`/`0x4A008144`/`0x4A008184` = CM_IDLEST_DPLL_MPU/_CORE/_PER/_USB; returns 1 if set, 0 otherwise (also 0 for sel>3). Caller: I2C init `0x4002956C` calls it with sel=2 (PER). | high |
| 8 | `0x28420` | `0x00029795` | `0x40029794` | `get_sys_boot()` — latched SYS_BOOT pins | 7-B body: `ldrb r0,[0x4A0022C4]`; returns `CONTROL_STATUS[7:0]` = SYS_BOOT (TRM Table 18-168). Used by `0x40028E72` to index the media table and by CH code. | high |
| 9 | `0x28424` | `0x0002979D` | `0x4002979C` | `get_device_type_class(out)` | `(*0x4A0022C4 & 0x7FF) >> 8` = CONTROL_STATUS[10:8] DEVICE_TYPE; returns 3 when DEVICE_TYPE==3 (GP), otherwise 2; out==NULL -> 6. Callers: `0x4002AB0E`, `0x4002AF42`, `0x4002C352`, `0x4002C638` (CH/image code). | medium |
| 10 | `0x28428` | `0x0002A4C1` | `0x4002A4C0` | `get_reset_reason(out)` | 17-B body: `*out = *(u32 *)0x4A307B04` = DEVICE_PRM PRM_RSTST (DEVICE_PRM offset 0x04, TRM §3 PRM); out==NULL -> 6, else 0. No xref (invoked from Thumb main via a literal per `HANDOFF.md`). | high |
| 11 | `0x2842C` | `0x00028613` | `0x40028612` | `gpmc_ecc_disable()` | 13-B body: `*0x500001F4 &= ~1` = GPMC_ECC_CONFIG[0] ECCENABLE = 0. Sibling of idx 44. | high |
| 12 | `0x28430` | `0x000284C1` | `0x400284C0` | `gpmc_softreset()` | Sets bit1 (SOFTRESET) of `0x50000010` = GPMC_SYSCONFIG; spin on `0x50000014` bit0 = GPMC_SYSSTATUS.RESETDONE. Called by the three GPMC-backed driver inits (`0x4002B808`, `0x4002B878`, `0x4002B9A8`). | high |
| 13 | `0x28434` | `0x00028567` | `0x40028566` | `gpmc_configure_cs(desc, cs)` — GPMC chip-select/timing + WAIT0 IRQ | Clears GPMC_TIMEOUT_CONTROL[0] (`0x50000040`, TIMEOUTENABLE); GPMC_CONFIG bit8 = `desc[+6]` bit8 (WAIT0PINPOLARITY); `0x400284FC(desc+8, cs)` programs GPMC_CONFIG1..7 at `0x50000060 + 0x30*cs` from 7 descriptor words; then ORs 0x100 into `0x5000001C` (IRQENABLE) and into `0x50000018` (IRQSTATUS clear) = WAIT0EDGEDETECTION. Caller: `0x4002C6C0` for CH magic `0xC0C0C0C3`. | high |
| 14 | `0x28438` | `0x000285CB` | `0x400285CA` | `gpmc_wait_read_monitoring(enable, cs)` | Sets/clears GPMC_CONFIG1[22] WAITREADMONITORING at `0x50000060+0x30*cs`; when enabling, polls GPMC_STATUS `0x50000054` bit `0x100<<cs` (WAITnSTATUS) until deasserted via `0x400285A0` (which also re-reads the 32K timer for a timeout). | high |
| 15 | `0x2843C` | `0x000284D7` | `0x400284D6` | `gpmc_set_device_width(cs, width)` | Sets GPMC_CONFIG1[13:12] DEVICESIZE at `0x50000060+0x30*cs` to 1 (16-bit) when arg==1, else 0 (8-bit). Caller: `0x400284FC` (from descriptor `desc[0]` bits[13:12]). | high |
| 17 | `0x28444` | `0x00029343` | `0x40029342` | `irq_register_handler(cb, id, flags)` | CPU id from `mrc p15,0,r0,c0,c0,5` & 3 selects a 0x20-B block at `0x4030DB08 + cpu*0x20`; stores `{id, cb}` in the first free of 4 slots (returns 1 if full); enables the GIC line via `0x4002B7B0` -> GICD `0x48241000` ISENABLER/ICFGR/ITARGETSR. Callers: `0x4002956C` (I2C init), `0x4002BD5C`. | high |
| 18 | `0x28448` | `0x0002938B` | `0x4002938A` | `irq_unregister_handler(id)` | Searches the same 4-slot per-CPU table for `id`, calls `0x4002B7CA` (GICD ICENABLER) and clears `{id, cb}`; returns 1 if not found. Caller: I2C close `0x40029738`. | high |
| 19 | `0x2844C` | `0x000293C3` | `0x400293C2` | `gic_irq_dispatch()` | `0x4002B7D4` reads GICC_IAR `0x4824010C`, masks 0x3FF, rejects <0x20 / 0x3FE / 0x3FF, subtracts 0x20; looks up the per-CPU 4-slot table; calls the handler; `0x4002B7EA` writes GICC_EOIR `0x48240110`. Entry point of the Thumb IRQ vector (`0x40029420`). | high |
| 28 | `0x28470` | `0x0002A2CB` | `0x4002A2CA` | `emif_init_from_descriptor(desc, out4)` — EMIF/DDR init (CH block 0xC0C0C0C2, cmd-8) | `0x40029AE6(9,0/1)` (CM module 9); `*(u32*)0x4E000040 = desc[0x48]` (DMM_LISA_MAP_0); `0x4002A2BC(1)` sets CM_DLL_CTRL[0]; channel mask `desc[0x4C]` selects EMIF1/EMIF2 and subchannels; `0x4002A212` writes EMIF +0x08/0x10/0x14/0x18…0x38, `0x4002A1BA` writes +0xE4/+0xE8/+0xEC, `0x4002A0E6` runs the LPDDR2 MR protocol using idx 29/30/31; writes 4 status words to `out`; returns 0/1/6. Callers: `0x4002C6C0` (CH 0xC0C0C0C2), cmd-8 open `0x4002BC10`. | high |
| 29 | `0x28474` | `0x00029F9D` | `0x40029F9C` | `emif_lpddr2_mr_cfg(emif, cs, mr_addr, refresh_en)` | Validates emif 0/1 and cs 0/1; writes `EMIF+0x50` = EMIF_LPDDR2_MODE_REG_CFG = `(cs ? 0x80000000 : 0) OR (refresh_en ? 0x40000000 : 0) OR mr_addr` (bits 31 REG_CS, 30 REG_REFRESH_EN, 7:0 REG_ADD). Callers: `0x4002A056`, `0x4002A0E6`. | high |
| 30 | `0x28478` | `0x00029F7D` | `0x40029F7C` | `emif_lpddr2_mr_write(emif, cs, data)` | Validates emif/cs then `*(u32*)(EMIF+0x40) = data` = EMIF_LPDDR2_MODE_REG_DATA. Caller: `0x4002A0E6`. (`cs` is validated but not used.) | high |
| 31 | `0x2847C` | `0x00029FD7` | `0x40029FD6` | `emif_lpddr2_mr_read(emif, cs, buf, width)` | Reads `EMIF+0x40` (MODE_REG_DATA); `buf[0] = reg & 0x7F`; then width 0 -> 1 byte, 1 -> low byte + bits[23:16], 2 -> low byte + bits[15:8], 3 -> 4 bytes; other width -> 1; buf==NULL -> 6. Callers: `0x4002A056`, `0x4002A0E6`. | medium |
| 35 | `0x2848C` | `0x0002A561` | `0x4002A560` | `get_sysboot_media_record(idx, out)` | 25-B body: idx>0x1F -> 1; out==NULL -> 6; `*out = 0x4002F7C4 + idx*16`. Table is the 6-record (16-B) SYSBOOT->{media,0x45} table used by `0x40028E72`; idx 6..31 point past the populated records. | high |
| 36 | `0x28490` | `0x0002925D` | `0x4002925C` | `fat_open(driver, read_cb, name, file_desc, mount)` — find a boot file on a FAT partition | Builds a 12-B read request `{lba,count,buf}` and calls `mount[1]` (driver read callback); sector 0 -> `0x40029130` FAT BPB check (0xAA55 at +0x1FE, 0x200 at +0xB, cluster size, FAT type); `0x4002B32E` scans the 4 MBR entries at +0x1BE; `0x4002B36A` selects the FAT partition; `0x40029050` finds the 11-char name in the root directory; `0x40028FB4` builds the LBA list. Caller: MMC driver `0x4002BA54` with name `"MLO"` at `0x4002BC00`, cb `0x4002EB92`. | high |
| 37 | `0x28494` | `0x00029051` | `0x40029050` | `fat_find_dirent(name, mount, &cluster, &size)` | Scans 16 x 32-B directory entries in a 512-B sector; `0x4002F004` memcmp of 11 bytes; rejects attr (`+0xB`) & 0x18 (directory/volume); returns first cluster (`+0x1A` low, `+0x14` high) and file size (`+0x1C`). Caller: `0x4002925C`. | high |
| 38 | `0x28498` | `0x00028FB5` | `0x40028FB4` | `fat_build_lba_list(file, bytes, desc, lba_out)` | `desc+0x400 = ceil(bytes/512)`, `desc+0x404 = min(that,0x100)`; walks the FAT chain with `0x4002B4A0`/`0x4002B4FA` and fills `desc[0..]` with 512-B LBA values; `desc+0x408` = last cluster. Caller: `0x4002925C` (and `0x4002BB1A` on demand). | high |
| 39 | `0x2849C` | `0x000297B7` | `0x400297B6` | `device_type_is_1()` | 15-B body: `((*0x4A0022C4 & 0x7FF) >> 8) == 1` (CONTROL_STATUS DEVICE_TYPE == 1). No xref. | medium |
| 43 | `0x284AC` | `0x000297C7` | `0x400297C6` | `ch_speedup_disabled()` — STD_FUSE_CH_SPEEDUP_DIS | 18-B body: `*(int *)(0x4A0022C4 - 0xA4) << 0x12 < 0` = bit 13 of `0x4A002220` CONTROL_STD_FUSE_CONF = STD_FUSE_CH_SPEEDUP_DIS (TRM Table 18-136). Callers: `0x4002C6C0` (early-out: fuse set -> skip all CH block processing), `0x4002EDA4`. | high |
| 44 | `0x284B0` | `0x00028605` | `0x40028604` | `gpmc_ecc_enable()` | 13-B body: sets bit0 (ECCENABLE) of `0x500001F4` = GPMC_ECC_CONFIG. Called by NAND driver init `0x4002B878` right after idx 45. | high |
| 45 | `0x284B4` | `0x00028621` | `0x40028620` | `gpmc_ecc_configure_bch(cs, bch_sel, size, is16bit)` | GPMC_ECC_CONFIG `0x500001F4`: [3:1]=cs (ECCCS), [6:4]=0 (ECCTOPSECTOR=1 sector), [7]=is16bit (ECC16B), [11:8]=1 (ECCBCHWRAPMODE=1), [13:12]=bch_sel (ECCBCHTSEL), [16]=1 (ECCALGORITHM=BCH); GPMC_ECC_CONTROL `0x500001F8` [3:0]=1 (ECCPOINTER=1); GPMC_ECC_SIZE_CONFIG `0x500001FC` [19:12]=size (0xD/0x1A/0x34) and [29:22]=0/2/3 — the three size/TSEL pairs used for BCH4/8/16. Caller: NAND driver `0x4002B878`. | high |
| 46 | `0x284B8` | `0x0002869D` | `0x4002869C` | `gpmc_ecc_get_bch_results(buf, size)` | Copies `0x50000240..0x24C` = GPMC_BCH_RESULT0_0…_3 (4 words) and, when size > 0x1A, `0x50000300..0x308` = RESULT4…6 (3 more words); the caller chooses the word count from the same 0xD/0x1A/0x34 size code. Caller: NAND read `0x4002B932`. | high |
| 47 | `0x284BC` | `0x000286D5` | `0x400286D4` | `gpmc_ecc_reset()` | Sets bit8 (ECCCLEAR) of `0x500001F8` (GPMC_ECC_CONTROL) and low nibble = 1 (ECCPOINTER=1). Caller: NAND/flash read paths (`0x4002E550`, `0x4002B932`). | high |

Confidence totals: **26 high, 3 medium (9, 31, 39), 2 low (4, 6)**.

### 2.1 Peripheral bases behind the labels

The addresses that the prior reports left as bare numbers now resolve
(TRM §15/§18/§3/§22):

| base | block | used by |
|---|---|---|
| `0x50000000` | **GPMC** (not USB / not a generic "0x50000000 helper") | idx 11-15, 44-47 |
| `0x4A004000` | CM1 instance; `0x4A0041xx` = CKGEN_CM1 (DPLL_CORE at +0x120, DPLL_MPU at +0x160) | idx 3, 5, 7 |
| `0x4A008000` | CM2 instance; `0x4A0081xx` = CKGEN_CM2 (DPLL_PER at +0x140, DPLL_USB at +0x180) | idx 3, 5, 7 |
| `0x4A304000` | 32K timer (`+0x10` sync counter), used as the poll timeout base | idx 4, 28 |
| `0x4A307000` | PRM (DEVICE_PRM); `+0xB04` = PRM_RSTST | idx 10 |
| `0x4C000000` / `0x4D000000` | EMIF1 / EMIF2 | idx 28-31 |
| `0x4E000000` | DMM (LISA map at +0x40) | idx 28 |
| `0x4A0022C4` | CONTROL_STATUS (SYS_BOOT[7:0], DEVICE_TYPE[10:8]) | idx 8, 9, 39 |
| `0x4A002220` | CONTROL_STD_FUSE_CONF | idx 43 |
| `0x4A318000` | GPTIMER1 (capture/TCAR1/TCAR2) | idx 4 |
| `0x48241000` / `0x48240100` | GIC distributor / GIC CPU interface | idx 17-19 |

### 2.2 CH block tags (boot-flow relation)

`0x4002C6C0` (config-header block walker) dispatches by the block's first word,
and the tag selects exactly one of the newly labelled entries:

| tag | handler | meaning |
|---|---|---|
| `0xC0C0C0C1` | idx 5 `0x40028C44` | DPLL / clock-tree configuration |
| `0xC0C0C0C2` | idx 28 `0x4002A2CA` | EMIF / DDR (LPDDR2) initialisation |
| `0xC0C0C0C3` | idx 13 `0x40028566` | GPMC chip-select / NAND-NOR timing |
| `0xC0C0C0C4` | `0x40028E2A` + driver `configure` | media-driver specific configuration |

Each successful block sets a bit in the config-header status byte (`+0x0A`) and
calls the progress-bitmap setter `0x4002A51C` with a code (0x15, 0x16, 0x17,
0x18, 0x19), which is the ROM's boot-progress ring. The whole CH path is gated
by idx 43 (`STD_FUSE_CH_SPEEDUP_DIS`): when the fuse reads 1, `0x4002C6C0`
returns without touching any block.

The media path is: idx 8 (SYS_BOOT) -> idx 35 (record lookup in `0x4002F7C4`)
-> `0x40028E72` (build candidate media/0x45 list) -> `get_driver_mem` (idx 1)
-> driver init; driver 5/6/7 (MMC) then calls idx 36 with the FAT name `"MLO"`,
which internally uses idx 37/38 to produce the sector list. Driver 3 (NAND)
uses the GPMC primitives idx 12/13/14/15 and the BCH ECC helpers idx
44/45/46/47; the other two GPMC-backed drivers (1/2, 4) use idx 12/13, and
driver 1/2 reads its media through the memory map (`memcpy` from the
`0x8000000` window).

---

## 3. Prior labels that were wrong or refined

Corrections to the descriptive column of `64-rom-api-and-media-final.md` §1.3:

| idx | prior label | corrected label / why |
|---|---|---|
| 8 | "device-type byte read (`[0x4A0022C4] & 0xff`)" | **Wrong register field.** Low byte of CONTROL_STATUS is `SYS_BOOT[7:0]`, not DEVICE_TYPE (which is [10:8]). The media table index is therefore the SYSBOOT strap, which is what `0x4002F7C4`'s 6 records encode. |
| 5 | "large Thumb body entry" | Not a generic body: it is the **DPLL/clock-tree configuration** handler for CH block `0xC0C0C0C1`, 266 B, bits 0/2..11 decoded. |
| 3 | "boot/display helper" | **DPLL MN-bypass** primitive (`CM_CLKMODE_DPLL_*[2:0] = 4`). Note 4 = MN bypass/disable, **not** enable (DPLL_EN 7 = lock). |
| 6 | "PRCM/boot helper" | 100-B **ROM clock/PRCM settings-record getter** for slot 0..7 (template `0x4002F760` + three override tables). |
| 7 | "boot helper" | **DPLL lock-status** read (`CM_IDLEST_DPLL_*` bit0), used by I2C init for the PER DPLL. |
| 4 | "media probe / class selector" | Refined: media/device-type detection, but the mechanism is a **GPTIMER1 input-capture pulse-width measurement** bucketed to 1/3/4/5/7, not a register probe. |
| 11, 44 | "`0x50000000` helper" | `0x50000000` is **GPMC**; these are GPMC **ECC enable/disable** (ECC_CONFIG[0]). |
| 12, 13, 14, 15 | "`0x50000000` enable/poll/value helper" | GPMC **soft reset**, **chip-select timing + WAIT0 IRQ**, **WAIT read monitoring**, **device width**. |
| 45, 46, 47 | (not previously separated) | GPMC **BCH ECC configure**, **BCH result read**, **ECC clear/pointer reset**. |
| 29, 30, 31 | "EMIF indirect index write / data write / data read" | Exact: **EMIF_LPDDR2_MODE_REG_CFG** (`+0x50`) and **MODE_REG_DATA** (`+0x40`) config/write/read. Not an opaque "PHY/DLL" protocol. |
| 35 | "16-byte-record base lookup" | **SYSBOOT media-record getter** `0x4002F7C4 + idx*16`; accepts 0..0x1F although only 6 records are populated. |
| 36, 37, 38 | "helper", "helper", "image-load helper" | A complete **FAT reader**: open (MBR + BPB + root dir + LBA list), 11-char directory lookup, FAT-chain LBA list. |
| 39 | "feature read helper" | **DEVICE_TYPE == 1** test (`CONTROL_STATUS[10:8]`). |
| 43 | "feature/device-type read" | **`STD_FUSE_CH_SPEEDUP_DIS`** (CONTROL_STD_FUSE_CONF bit 13) — the eFuse that gates config-header processing. |
| 9 | "feature/device-type helper" | **DEVICE_TYPE normaliser**: 3 (GP) -> 3, anything else -> 2. |
| 19 | "GIC dispatcher" | Confirmed, with the mechanism: GICC_IAR/EOIR at `0x4824010C/0x48240110`, per-CPU 4-slot handler table at IRAM `0x4030DB08`. |
| 0 | "cache-op / SMC argument wrapper" | Refined: worker does ICIMVAU (`c7,c5,6`) + DSB/ISB/DMB + `smc #1` with `r6=0xFF, r12=0`; the four `0x40028F2x` wrappers supply op 0..3. |

One more cross-check: `0x4002A95A` (documented `ctrl_configure_pads`) is called
by the three GPMC-backed driver inits (1/2, 3, 4) as
`ctrl_configure_pads(8,0)` before GPMC setup, which is consistent with a pad
group 8 used for the GPMC/NAND balls. The MMC driver (5/6/7) does not call it.

---

## 4. Entries that remain genuinely ambiguous

1. **idx 4 `0x40028D4E` (low).** The mechanism is certain (GPTIMER1 reset +
   capture, `TCLR=0x2101`, `TCAR2-TCAR1`, thresholds 0x1B8/0x227/0x2B4/0x3D7,
   result in {1,3,4,5,7}). What physical edge the GPTIMER1 capture input sees is
   not determinable from this dump, and the coincidence of the output values
   with `mem_device.device_type` / mem-driver indices is an inference, not an
   observation. Candidate readings: boot-device timing signature, board strap,
   or a silicon-speed measurement repurposed as a device-class code.
2. **idx 6 `0x40029CD0` (low).** Behaviour is exact (100-B copy + three packed
   overrides per slot), but the record's consumer and field semantics are not in
   this image: no ROM-internal caller, and the template's first words
   (`0x3BD`, `0x110`) do not match a DPLL M/N pair for any standard OMAP4430
   output frequency under a 38.4 MHz reference.
3. **idx 9 `0x4002979C` (medium).** The 3-vs-2 mapping is exact, but the TRM only
   documents DEVICE_TYPE = 3 (GP); whether 2 denotes EMU or HS cannot be settled
   from this dump (idx 39's `== 1` test is a separate, equally undocumented
   encoding).
4. **idx 31 `0x40029FD6` (medium).** Register access (`EMIF+0x40`) is certain;
   the `width` selector 0..3 produces overlapping byte layouts (0 and 2 both
   yield two bytes with different second bytes), so the intended data format is
   inferred.
5. **Validated-but-unused arguments.** idx 30/31 check the EMIF subchannel
   argument and then ignore it, and idx 14's `enable` value is the only thing
   that selects the wait path. These look like vestigial parts of a generic
   driver-vector ABI rather than distinct operations.
6. **No Ghidra-resolved xrefs** for idx 4, 6, 8, 10, 11, 12, 14, 19, 35, 36, 39,
   44, 45. They are API-table entries or are reached through literal-pool
   pointers that the raw import does not turn into references (idx 37/38 are
   reached from idx 36). Their boot-flow placement above is therefore inferred
   from the sibling code that does call them (CH walker, media drivers, I2C,
   IRQ vector).

---

## 5. Reproduce / artefacts

- This report: `analysis/68-rom-api-undocumented-labels.md`
- Raw dump: `analysis/rea-ghidra/rom-api-undocumented-decompiled.txt`
  (PART 1 entries, PART 2/3/4 helper pseudocode and disassembly, PART 5 data
  tables and literal values).
- Ghidra projects and the three Java post-scripts were created only under the
  session scratchpad and deleted with `-deleteProject`; the `.bin` was never
  opened for write and its SHA-256 is unchanged.

Public-document context used for register/field names only: `swpu231ap.pdf`
(TRM) §3 (PRCM/CM), §15 (GPMC, EMIF), §18 (control module), §22 (GPTIMER),
§24 (MMCHS).
