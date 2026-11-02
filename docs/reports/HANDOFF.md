# HANDOFF - OMAP4430 boot ROM RE (0x40028000)

Session state frozen 2026-10-19. This chat is context-limited; continue in a NEW
session by reading this file plus `NOTES.md`, the per-topic reports below, and
`60-mode-map-resolution.md` / `61-rom-api-and-flash-tables.md` /
`62-monitor-services.md` (the 2026-10-19 continuation).

## Target / identity

- `/home/psyden/rea-work/bootdumps-2026-09-11/omap4430-bootrom-0x40028000.bin`
- 49152 bytes, load base 0x40028000, ARMv7-A (Cortex-A9), mixed ARM + Thumb-2, LE.
- SHA-256 `fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`.
- Provenance: NS-readable from QNX via memdump3, fresh post-battery-pull
  (see `../README.md`, `../BOOTROM-RE.md` - prior work; treat as claims to verify).

## Tooling (updated 2026-10-19)

- REA MCP server `rea` 6.7.0 is registered for **Command Code** and OpenCode,
  and the server handshake exposes **207 tools**. The old "opencode only / no
  native provider" constraint is obsolete.
- Native provider: **Ghidra 12.1.4** at
  `/home/psyden/tools/ghidra_12.1.4_PUBLIC`, with full 64-bit **JDK 21** at
  `/usr/lib/jvm/java-21-openjdk`. `GHIDRA_INSTALL_DIR`, `JAVA_HOME`, and
  `REA_ANALYSIS_PROVIDER=ghidra` are stored in `~/.commandcode/mcp.json`.
- After registration changes, restart Command Code so the MCP server is spawned
  with the new environment. In a live session, use `/mcp` to confirm `rea` is connected.
- REA/Ghidra Evidence is now available. LLVM 23 output remains an independent
  cross-check and is **LLVM-derived, not REA Evidence**.
- See `analysis/METHOD.md` for the CLI/MCP recipe and the LLVM wrapper method.

## Setup complete / live evidence (2026-10-19, post-relaunch)

- Command Code's `/mcp` shows `rea` green with **207 tools**; live server
  identity: `rea` 6.7.0, client `commandcode` 0.1.0, negotiated protocol
  `2025-03-26`, catalog `af39310c…`.
- `binary_session` confirms Ghidra as an available provider: 12.1.4, headless
  `analyzeHeadless`, native `decompile` (linux/x64), Java 21 full JDK.
- The bootrom was opened through MCP with `format=raw-binary`,
  `architecture=arm`, `entry_point=0x40028000`, `provider_id=ghidra`;
  SHA-256 matched the target above.
- Portable evidence bundle: `analysis/rea-ghidra/evidence-bundle.json`
  (15,187 bytes, **3 records**, 0 unknowns). Export this before switching
  sessions; MCP Evidence IDs are scoped to one server process.
- Live Evidence IDs from that server (for reference only):
  - `binary_overview` - `ev_bdcb80e5c405a1149c83025a60af2f9599036bd6637d6a5c9526bfa7fc6fefb9`
    (1 segment `0x40028000`-`0x40034000`, 218 procedures, 5 strings).
  - `procedure_pseudo_code 0x4002A51C` - `ev_549862489aee3f6af7159c3da6bdf5d6bf959fbcbf4e44ae968cec20426f3cb1`
    (Thumb 32-bit progress-bitmap setter).
- A new session spawns a **fresh** MCP server, so the target is not open there.
  Reopen it with `open_binary` (`raw-binary` / `arm` / `0x40028000` / `ghidra`);
  use the exported bundle if the old Evidence IDs are needed.
- The verification Ghidra session was closed after export; no annotations were
  made, and the original bytes were never modified.

## Files produced this session

- `analysis/METHOD.md` - tooling recipe (REA/Ghidra + LLVM), mode map, boundaries.
- `analysis/NOTES.md` - consolidated working notes (my synthesis; some mode-map
  lines are superseded by 50-code-map.md).
- `analysis/10-boot-media.md` - MMC/MMCHS, flash, device selection.
- `analysis/20-config-parser.md` - Thumb main flow, CH/config parser, watchdog.
- `analysis/30-monitor-security.md` - SMC sites, cache/TLB lib, CP15, aborts.
- `analysis/40-iram-and-tables.md` - copy table, IRAM image, USB template, vectors.
- `analysis/50-code-map.md` - function/mode inventory (282 rows) + call graph.
- `analysis/60-mode-map-resolution.md` - authoritative dual-mode map + boundary corrections.
- `analysis/61-rom-api-and-flash-tables.md` - ROM API table + flash geometry/pair tables.
- `analysis/62-monitor-services.md` - `0xF0`/`0xFE`/`0xF1` structural identification.
- `analysis/BOOTROM-0x40028000-REPORT.md` - consolidated report (updated 2026-10-19).
- `analysis/rea-ghidra/overview.json` - Ghidra: 1 segment 0x40028000-0x40034000,
  49152 bytes, 218 procedures, 5 strings.
- `analysis/rea-ghidra/entry-40028000.json` - Ghidra reset entry / CPU1 wait /
  SMC / ARM-Thumb handoff pseudocode.
- `analysis/rea-ghidra/thumb-4002a51c.json` - Ghidra Thumb progress-bitmap setter.
- `analysis/rea-ghidra/evidence-bundle.json` - portable REA Evidence bundle
  (3 records), including the live MCP overview and Thumb decompilation.

## Highest-value verified findings so far

- **Ghidra 12.1.4 independently reproduces the reset path**: CPU ID via
  `mrc p15,c0,c0,5`, CPU1 `wfe` loop on the `+0x400`/bit-0x200 flag, `smc`,
  and the ARM -> Thumb handoff. It also correctly decompiles the Thumb
  progress-bitmap setter at `0x4002A51C` (0x20-bit words), confirming mixed-mode
  auto-analysis works on the raw image.
- Reset 0x40028000 `b 0x40028910`; ROM exception vectors at 0x40028004 redirect
  through IRAM 0x4030D004.. (copied table 0x4002FFC0), so payloads can patch
  live vectors.
- Bootstrap copy table at 0x4002FF80 = 4 x {src,dst,len,fn}; walker 0x4002882C;
  fn 0x40028870=ARM memcpy, 0x40028898=ARM memset. memcpy -> IRAM 0x4030D000
  (vectors), 0x4030D100 (8 bytes), 0x4030D108 (USB template); memset zeroes
  0x4030D1A0..0x4030DC5B (BSS, len 0xABC).
- ARM -> Thumb handoff at 0x400288D0 (`r12=0x135+pc` -> 0x40028A19) into Thumb
  main 0x40028A18.
- Thumb main: reset reason from PRM_RSTST 0x4A307B04 via 0x4002A4C0; progress
  bitmap setter 0x4002A51C (63 callers, top hub); watchdog bring-up at
  0x4A314000 (0x4002A41C/A430/A454/A478, WSPR/WWPS magic AAAA/5555/BBBB/4444);
  config parser 0x40028A7C.
- Config parser: command bytes 1..8 -> 0x4002AD5C, 0x41..0x47 ('A'-'G') ->
  0x4002AFFE; results into struct 0x4030D1A0 (+0x64 etc; base literal 0x40028BF8).
- Boot media: MMC base table 0x4002FE20 (0x4809C000, 0x480B4000, 0x480B4000,
  0x480AD000, 0x480D1000, 0x480D5000, 0x480B6000); MMCHS ctrl via +0x20C;
  command descriptor bits[31:24]=CMD index, bits[17:16]==1 -> 128-bit response;
  card init 0x4002E930 (CMD0/CMD8 0x1AA/CMD55/ACMD41/CMD1/CMD2/CMD3), MMC vs SD
  in inner[4]; device-type map 0x4002EC38 {5,6,7,8}->{0,1,2,6}; clock divider
  table 0x4002FE3C used by 0x4002E8CE.
- Boot-image name table: 14 pointers at 0x4002FA84 (R&D, 2ND, CHMMCSD, CH,
  CHFLASH, CHRAM, HLO, MLO, ULO, PRIMAPP, X-LOADER, CHSETTINGS, KEYS, ISSW).
  Config headers found by magic 0xCF00AA|type scanning 0x4A326A00-0x4A327000
  (0x4002B64E).
- SMC: exactly 8 sites. smc#0: 0x40028134 (r12=0x107), 0x4002813C (0x103),
  0x400289C8 (0x107), 0x4002CEB8 (0xF0, ARM veneer 0x4002CEB0, built into a
  callback record by 0x4002AC1A/0x4002C610 and passed to monitor service 0).
  smc#1: 0x4002F4C8/4E8/510/530 (r12=0/0xFE/0/0xF1) inside ARM cache wrappers.
  No Thumb SMC.
- CP15 security: only VBAR=0x00028000 (0x40028954) in this window. Secure tail
  (NSACR=0x20C00, MVBAR=0x1C1C0, SCR=0x30, smc r12=9) is in the separate lower
  dump 0x40026098, not this file.
- Aborts: 0x400289E4 = prefetch (IFAR/IFSR -> r0/r1, hang 0x40028088),
  0x400289F0 = data (DFAR/DFSR, hang 0x4002808C). IRQ target 0x40029420 ->
  Thumb GIC dispatcher 0x400293C2 (GICC_IAR/EOIR 0x4824010C/0x48240110).

## Known conflicts / caveats

- `NOTES.md` and `50-code-map.md` disagree on a few boundaries and on the ARM
  stub near 0x4002B0xx; 50-code-map.md (per-range dual-mode disassembly) refutes
  an ARM presence there and moves the ARM string/mem library to
  0x4002F0BC-0x4002F2F0, cache/TLB lib to 0x4002F310-0x4002F56C. Treat
  50-code-map.md as the better mode map; its two boundary errors are corrected
  in `60-mode-map-resolution.md`.
- **Resolved 2026-10-19:** two boundary corrections to 50-code-map.md are in
  `60-mode-map-resolution.md`: `0x400288F8`-`0x400288FB` is a 4-byte Thumb
  `bl 0x4002F2F0` followed by literals at `0x400288FC`-`0x40028903`; and
  `0x4002B798` is the literal `0x4A100000`, with Thumb resuming at
  `0x4002B79C`.
- `NOTES.md` calls 0x4002FF80 record 4 a memset of a 0xABC blob; 40-*.md shows
  the source region 0x400300A0.. is entirely zero (only the size matters).
- **Resolved 2026-10-19:** `0x4002FEB4`/`0x4002FECF` are a 9-record geometry
  table plus a 46-record `{device-ID, index}` pair table; `0x4002FEE0` is the
  mis-aligned tail of that pair table, not a separate table. `0x40028400` is the
  TRM's ROM API table (48 Thumb pointers). See `61-rom-api-and-flash-tables.md`.
- ~200 of 282 function starts in 50-code-map.md are unclassified. Ghidra found
  218 procedures on its own; reconcile its boundaries with 60-mode-map-resolution.md
  before treating them as new findings.
- The 4-KB non-readable hole 0x40026F28-0x40027FFF (in the lower dump only) is
  the main SMC-dispatch candidate - inference, not observed here.

## 2026-10-19 continuation (SWPU231AP TRM added)

`swpu231ap.pdf` was added to `bootdumps-2026-09-11/`. It is public-document
context, not REA Evidence. New output:

- `analysis/60-mode-map-resolution.md` - authoritative dual-mode map, including
  the `0x400288F8` and `0x4002B798` corrections.
- `analysis/61-rom-api-and-flash-tables.md` - byte-exact 48-entry ROM API table
  (TRM Figure 27-6), 9 flash geometry records, 46 pair records, proof that
  `0x4002FEE0` is a shifted view.
- `analysis/62-monitor-services.md` - `0xF0` is a terminal `SMC #0`
  descriptor-driven image-load/hand-off entry; `0xFE`/`0xF1` are dormant,
  unreferenced alternate selectors in the cache wrappers (they skip the
  prologue, so they are not standalone functions). `r12` is the monitor function
  ID (TRM §27.5), not a BPIALL operand.
- `analysis/BOOTROM-0x40028000-REPORT.md` updated in §2.5, §4.2, §4.4, §4.6,
  §4.7, §5, §6, §7 (C11-C13) and §8.
- `analysis/rea-ghidra/` - Ghidra 12.1.4 setup smoke tests (overview, reset entry,
  Thumb 0x4002A51C) and the portable `evidence-bundle.json`. This is the first
  native-provider Evidence for this dump.

Still blocked: secure-side semantics of `0xF0`/`0xFE`/`0xF1` and the `0xF0`
descriptor fields; the secure bytes (`0x40026F28`-`0x40027FFF`, live IRAM
`0x4030D000`/`0x4030DC1C`) are not available. Ghidra is now installed, so the
remaining native work can use pseudocode instead of LLVM-only listings.

## 2026-10-19 continuation b (Ghidra reconciliation, API labels, media)

Setup re-confirmed in a fresh Command Code session: `rea` 6.7.0 with Ghidra
12.1.4; `open_binary` (`raw-binary` / `arm` / `0x40028000` / `ghidra`) reopened
the same target (SHA-256 matched). `binary_overview` again reports one segment
and **218 procedures**. New output:

- `analysis/63-ghidra-218-reconciliation.md` - procedure-by-procedure
  reconciliation with the 282-row map.
- `analysis/64-rom-api-and-media-final.md` - external ROM API labels, EMIF/DDR
  descriptor decode, `0x4002F7C4` gating, MMCHS command-word flags, and the
  secure/JTAG status.
- `analysis/rea-ghidra/evidence-bundle-2026-10-19b.json` - portable bundle from
  this session (**22 records**, 0 unknowns, 191,550 bytes). The earlier
  `evidence-bundle.json` is unchanged.
- `analysis/rea-ghidra/procedure-reconciliation.csv` - 297-row union of the
  282 map starts and the 15 Ghidra extras, with mode and category notes.

Key results:

- **Ghidra reconciliation:** 203 of 218 starts are exact 282-row map starts; 15
  are legitimate extra starts/boundaries (list in `63`); 79 map rows are not
  Ghidra procedures. Of those 79: 7 are the `bl .+4` filler instructions, 1 is
  the `0x4002B798` literal, 2 are mode-switch veneers, 4 are API-table-only
  targets, and 65 are genuine starts not reached by Ghidra's recursive descent
  (7 + 1 + 2 + 4 + 65 = 79; four further `tbb` targets are absent from both
  inventories). **Zero Ghidra procedures lie at/after `0x4002F578` (data),
  and there are zero mode-map contradictions.** Ghidra's 218 is a decompilable
  subset, not a replacement for the 282-row map; the genuine-start union is
  about 287.
- **External API list found** (`simu/usbboot-omap4`, `swetland/omap4boot`,
  `bigunclemax/sync3flash`, `minoca/os` headers). It names 17 of the 48 entries
  (driver/device getters, IRQ init, WDT enable/disable/reset/timeout, I2C
  init/read/write/close, CM enable/disable, CTRL pads). Corrections:
  **`0x4002A454` is WDT enable/start (`0xBBBB`/`0x4444`) and `0x4002A430` is
  disable/stop (`0xAAAA`/`0x5555`)** - the reverse of the old report;
  `0x4002A95A` is `CTRL_CONFIGUREPADS`, not a PRCM module enable; the `'A'..'F'`
  parser commands are device IDs `0x41..0x46` = UART1-4/USB/USBEXT (`'G'` is
  rejected).
- **Media:** `0x4002F9F8` is an **EMIF/DDR descriptor** (two 0x24-byte records
  mapping to `EMIF_SDRAM_CONFIG/REF_CTRL/TIM_1..3/PWR_MGMT/DDR_PHY_CTRL`, plus
  `DMM_LISA_MAP_0` and a channel mask). `0x4002BC10` (command 8) calls it and,
  on status 2, opens the `0xC0000000` DDR-SDRAM CS1 window - i.e. **command 8 is
  the SDRAM/memory-boot path**. `0x4002F7C4` gating is exact: device type is
  `[0x4A0022C4] & 0xFF`, bits[4:0] index six 16-byte records, bit 5 selects the
  half, bit 15/flag bit 0 gate inclusion, and flags bit 9 switches to the stored
  config block. MMCHS command-word flags are fully decoded: `[29:24] INDX`,
  `[23:22] CMD_TYPE`, `[21] DP`, `[20] CICE`, `[19] CCCE`, `[17:16] RSP_TYPE`,
  `[5] MSBS`, `[4] DDIR`, `[2] ACEN`, `[1] BCE`, **`[0] DE = DMA Enable`** (so
  CMD17/CMD45's `DE=0` is correct for CPU FIFO transfers).
- **Secure/JTAG (item c): no JTAG, but live root access works.** The host has
  no JTAG/debug probe, no `/dev/ttyUSB*`/`ACM*`, no OpenOCD/J-Link/DS-5/
  gdb-multiarch, and `adb devices` is empty. On 2026-10-20 the rooted unit was
  nevertheless reached over RNDIS + the BerryShell/qconn door + root SSH; the
  SAR/monitor window and live IRAM were dumped. The hidden
  `0x40027000`-`0x40027FFF` page remains firewalled. See
  `65-live-playbook-sar-iram.md`.

## 2026-10-20 live-device session (item c follow-up)

Access: the PlayBook enumerated as `0fca:8010` (RNDIS/CDC); the host interface
`enp8s0f3u1` was given `169.254.0.2/16`, the qconn door at
`169.254.0.1:4455` was authenticated with `BerryShell-V4.py` (workspace
`rsa`/`rsa.pub`), and plain root SSH worked. `memdump3` was copied to
`/tmp/memdump3` and run with `on -C 0`. All operations were read-only.

New output:

- `analysis/65-live-playbook-sar-iram.md` - live SAR/monitor and IRAM analysis,
  plus the corrected secure-hole boundary.
- `analysis/playbook-live-2026-10-20/` - raw memdump text, reconstructed
  `.bin` files, and the hole probes.

Live findings:

- **SAR/monitor window `0x4A326A00`-`0x4A326CCF` is NS-readable.** It holds the
  CPU1 warm-reset trampoline and its context table. The monitor ABI is
  **`r0`=service, `r6=0xFF`, `r12=0`, `SMC #0`**; in this live state service
  `0x26` is called and `0x27` is skipped, both with `r1=0`, `r2=4`, `r3` =
  pointer to a 4-byte value. The trampoline then restores ACTLR, TTBR0/1,
  TTBCR, DACR, PRRR/NMRR, FCSE/context/TPIDR, VBAR and SCTLR, and resumes.
- **Live IRAM `0x4030D000`-`0x4030DFFF`.** The ROM's second vector table is
  copied at `0x4030D000` and QNX patched the targets to its own `0x8001xxxx`
  handlers; the USB descriptor template is visible at `0x4030D108`; the
  `0xF0` shared descriptor at `0x4030DC1C` is **zero** (never filled on this
  boot). The ROM progress ring/control words are also visible.
- **The secure hole is not a uniform 4 KB.** `0x40026F28`-`0x40026FFF` (0xD8
  bytes) reads as zero; `0x40027000`-`0x40027FFF` SIGBUSes on the first word;
  the low alias `0x00026F28` SIGBUSes. The hidden dispatcher is the 4 KB page
  at `0x40027000`.
- **Still blocked:** the hidden dispatcher page and the secure-side effect of
  `0xF0`/`0xFE`/`0xF1`; the structure in `62-monitor-services.md` stands.
  The ramloader cannot substitute (HS-verified first stage, `CREAD` RSA-gated,
  no arbitrary RAM/ROM read); `$B7 HASH_BOOTROM` is the only non-invasive
  remaining probe.

## 2026-10-20 ramloader / Sachesi analysis

Analyzed the signed PlayBook ramloader `loader_06001A06-00.bin` (238,128 B,
base `0x80100000`, SHA-256
`c7cc33c796f9518bbc0eb929c6096f2473937ad486c579764aa4c57d8165393f`) with
REA 6.7.0 / Ghidra 12.1.4: one segment, **988 procedures**, 92 strings. The
bb10mt and Sachesi 2.0.3 sources were used as client-side context.

New output:

- `analysis/66-ramloader-opcodes-and-sachesi-debug.md` - main router, full
  opcode map, CREAD/OS-region deep dives, Sachesi hidden modes.
- `analysis/rea-ghidra/loader-opcodes.csv` - 47 implemented opcodes with
  dispatcher/response/behavior.
- `analysis/rea-ghidra/loader-evidence-bundle-2026-10-20.json` - 26 records,
  238,921 bytes, 0 unknowns.

Key findings:

- The main router `FUN_80116E28` switches on the low byte at inner-frame `+2`;
  four dispatchers: `0x801032AC` (query/info), `0x80103B90` (actions),
  `0x8011AE30` (erase/write/complete), `0x80102DC4` (CREAD). Direct cases are
  `0xEF` reboot, `0xF7`/`0xF8` write, `0xF9` signature.
- Not implemented in this loader build (unknown -> `0x15`): `0xDD`, `0xDF`,
  `0xE2`, `0xE3`, `0xF0`-`0xF2`, `0xF4`-`0xF6`, `0xFA`-`0xFC`, `0xFE`.
  The bb10mt table is a superset covering other BB10 builds.
- New/undocumented implemented: `0x39`/`0x3A` CREAD block read (requires the
  `0xE4` session), `0xC2` 0x20-byte status, `0xD1` 4-byte SVN, `0xDA` NVRAM
  `0x2027` read/delete, `0xDE` security/RPMB query, `0xE0` installer/OS-region
  operation (`0x206C`), `0xE1` 0x3FF0-byte log/crash buffer, `0xE6`/`0xEB`
  CREAD variants.
- **`resetqnxosregion` lead:** `0xE9` reads NVRAM `0x2074` (24 bytes, safe);
  `0xC0` COMPLETE writes `0x2074 = {2,0,byte}` and requires `0x207C`; `0xE0`
  reads `0x206C` and calls `FUN_80119538` (installer/region table). The
  CFP command almost certainly manipulates this `0x2074`/`0x206C` family.
- `0xB7 HASH_BOOTROM` returns a **20-byte hash**, not ROM bytes.
- CREAD remains RSA-gated: `0xE4` sets `+0xAEC=1`; `0x39`/`0x3A` call
  `FUN_80104984`, which refuses without that session.
- Sachesi "Debug Mode" (`commandDebug=4`) sets mode 1 and `_found=8000`,
  switching the control channel from EP2 `0x02`/`0x82` to EP1 `0x01`/`0x81`.
  `kCommandUnkn1` `0x11`/`0x12` are defined but unused; there is no debug
  console. "RimBoot" (2) is mode 1 + password + ramloader upload; "Nuke" (3)
  sets mode 4 (destructive). Mode 2 `RIM-RAMLoader` is defined but never
  selected by the UI.
- Label corrections: `0xB3` is the NVRAM `0x2033` action log (not "GetLAL");
  `0xC3` is LED/PWM control (not "log cfg"); `0xAF` writes the
  `DAT_80118A00` mode flag.
- Bootrom `0xF00C`/`0xF00D` are defined in bb10mt but not found in the visible
  lower boot ROM dump. They are likely in the hidden 4 KB page or the RIM
  bootrom stage at `hd0 0x14900` (not in the workspace's current dump set).

## 2026-10-20 continuation c (items 3 and 4 complete)

Both static items from the previous "next work" list are done. No device writes
were made; the boot ROM dump was re-hashed after the runs
(`fc2ad391…b4961`, unchanged).

- `analysis/67-ghidra-seeded-starts.md` — closes item (3). All **65/65** genuine
  unseeded starts were created and decompiled with Ghidra 12.1.4 headless
  (`analyzeHeadless`, `ARM:LE:32:v7`, base `0x40028000`, a Java post-script that
  sets the `TMode` context, disassembles, calls `createFunction`, then uses
  `DecompInterface`). 61 bodies are semantically usable; 4 are decompiler
  artefacts (recorded in §6.1 of that report). Seven entries
  (`0x40028990`, `0x4002CEBC`, `0x4002D1A4`, `0x4002E0B6`, `0x4002E6E4`,
  `0x4002E6F4`, `0x4002EF14`) had to have a conflicting prior-mode instruction
  cleared at the entry before the correct `TMode` could be written.
- `analysis/rea-ghidra/seeded-starts-decompiled.txt` — raw pseudocode for all 65.
- `analysis/68-rom-api-undocumented-labels.md` — closes item (4). All **31/31**
  ROM-API entries not named by the external OMAP4 headers were decompiled and
  labelled (26 high, 3 medium, 2 low confidence).
- `analysis/rea-ghidra/rom-api-undocumented-decompiled.txt` — raw pseudocode
  (4952 lines) for the 31 entries plus helpers and data tables.

High-value results (details and evidence in the two reports):

- **`0x4002CE90`–`0x4002DB92` is a USB OTG (MUSB-class) device driver**, MMIO base
  `0x4A0AB000` (literals at `0x4002D2B0`/`0x4002D6B8`), reached only through the
  per-driver methods for device ID `0x45` (DEVICE_USB). It has FIFO and indexed
  window transfer paths selected by `per_handle.xfer_mode`, and a uniform status
  convention `0 = done, 1 = error, 4 = continue`. This is 26 of the 65 entries.
- **`0x50000000` is the GPMC**, not a generic block: API entries 11/12/13/14/44/
  45/46/47 are GPMC soft reset, CS timing + WAIT0 IRQ, device width, wait
  monitoring and BCH ECC enable/disable/configure/result/clear.
- **API entry 8 reads `SYS_BOOT[7:0]`** (CONTROL_STATUS low byte), not the device
  type; DEVICE_TYPE is still `[10:8]`. Entry 39 tests DEVICE_TYPE==1; entry 9
  normalises GP→3 else 2.
- **API entries 36/37/38 are a FAT reader** (MBR+BPB+root dir+LBA list); the MMC
  driver calls entry 36 with the literal name `"MLO"`.
- **API entry 43 is `STD_FUSE_CH_SPEEDUP_DIS`** (CONTROL_STD_FUSE_CONF bit 13) —
  the gate that makes `0x4002C6C0` skip all config-header blocks.
- **`0x4002ED48` sends command 43 (`0x2B1B0000`, R1b)**, not `0x2D3A0010`:
  `0x4002EDA0 = 0x2B1B0000`; the `0x2D3A0010` literal at `0x4002ED9C` belongs to
  `0x4002ECA4`. `10` §3.2's row for `0x4002ED48` is corrected.
- **CMD4's command word is `0x04000000` with `0x04040000` as its DSR argument**
  (`0x4002EA5E`), so `64` §2.3's "reserved bit 18" entry is a field-extraction
  artefact.
- **MMC capacity uses EXT_CSD**, not CSD: CMD8 SEND_EXT_CSD + 512-byte read,
  `SEC_COUNT` at byte `0xD4`; SD uses CSD-v2 `C_SIZE << 10`.
- **Flash is NAND**: `0x4002EF14` keys on ID[0]==`0x98` and applies an ID[3]-driven
  geometry override; the ID-table geometry is only a fallback behind `0x4002EE94`.

Method caveat for future sessions: a plain Ghidra `-import` of this raw image
produces **145** functions, while the REA-provider path reported **218** (it seeds
additional entries). Run-to-run function counts are therefore not comparable
unless the seeding policy is stated.

## 2026-10-20 continuation d (live bootrom/loader USB, items 1-2)

Live USB work on the rooted PlayBook. Full write-up and raw log:
`analysis/69-live-bootrom-loader-usb.md`,
`analysis/playbook-live-2026-10-20/bootrom-probe.log`; client:
`analysis/rea-ghidra/playbook-usb-probe.py`.

- **Bootrom control path works end-to-end** up to the hand-off: PING, GETVAR(2),
  SETMODE(1) (17-byte payload; 16 bytes is rejected `0x09`), PASSWORD_INFO and
  SEND_PASSWORD. The unit's password is **`0000`**; the challenge is always
  issued, with **5060** SHA-512 iterations (`HashPassV2` + `00 00 40 00` prefix).
- A channel-0 **`0x13` ReadyForDataTransfer** frame is emitted after the password
  and after every channel-1 command. It must be drained before SWITCH_CHANNEL or
  the EP2 write is NAKed.
- **The 238,128-byte signed ramloader uploads reliably** in 0.8 s (0xF009 setup,
  916×0xF004 260-byte chunks, 0xF00A verify) over EP2.
- **Hand-off fails:** `0xF005` EXECUTE_RAM is acknowledged but does not transfer
  control (bootrom stays enumerated); `0x0B` LoadTransferredData returns `0x0C`
  and **reboots the unit**. No `0fca:8001`/`0fca:8017` ever appears. The `0xF00x`
  handler is not in the NS-readable ROM (no `movw #0xF00x`), so this is most
  likely an HS authenticated-transfer/execute gate in the hidden page.
- **Item (2) answered:** the Sachesi Debug-Mode EP1 (`0x01`/`0x81`) transport is
  not usable. On a fresh bootrom the first SETMODE write is accepted but nothing
  is returned on `0x81`, and every later EP1 write times out. `0x11` never
  reaches a live transaction.
- Item (1) remains blocked: with the ramloader never running, `0xE9` (NVRAM
  `0x2074`) could not be read. Shortest next step is a USB trace of a **working**
  cfp.exe/bb10mt loader start to diff the hand-off packets; then `0xE9`.

## 2026-10-20 continuation e (live hand-off solved; 0xE9 read; EP1 closed)

Supersedes the "hand-off fails" and "0xE9 unreachable" statements in
continuation d. Full detail: `analysis/69-live-bootrom-loader-usb.md`.

- **Hand-off solved.** `0xF005` EXECUTE_RAM only *requests* the jump. bb10mt's
  `RunLoader` does one **third read** after the channel-1 response pair; that
  read returns `0b 01 01 00` (cmd `0x0B`, mode `0x01`, packetId `1`). Only then
  does the bootrom jump, and the unit re-enumerates as **`0fca:8001`** ~3-4 s
  later. Without that read it stays in bootrom indefinitely. `0x0B` on channel 0
  is not an alternative: it returns `0x0C` and reboots the unit.
- **Loader protocol:** open `0fca:8001`, `SETMODE(2)` → `0x08`,
  `PASSWORD_INFO` → `0x10` (no challenge), then **drain the paged Bugdisp log**
  (`0xB0` + 8 zero bytes → repeated `0xB5`/512-byte pages until `0xD0`; this
  unit had >64 pages). Skipping the drain shifts every later response.
- **NVRAM `0x2074` read (item 1 baseline).** `0xE9` requires a **4-byte zero
  payload**; a zero-length request reads the CRC as the argument and returns
  `0x15/0x04`. With zero data it returns `0xFB`, a success dword `0`, and the
  24-byte record: `03 00 00 00 00 00 75 01 00 00 00 00 …` (rest zero). This is
  the "before" value to compare against a CFP `resetqnxosregion` run.
- Other loader queries confirmed: `0xB4`→`0xD2` (Samsung MCGAFA flash regions),
  `0xE7`→`0xD1` PIN `0x50175127`, `0xD1`→`0xC8` SVN `0`.
- **Item (2) closed:** EP1 `0x01`/`0x81` is not usable. Verified with the probe
  and with a CLI harness built from Sachesi 2.0.3's own `boot.cpp`
  (`analysis/rea-ghidra/sachesi-debug-harness/`): the first SETMODE OUT is
  accepted, nothing returns on `0x81`, and the next EP1 OUT times out. `0x11`
  cannot be delivered. Normal EP2 works.
- Corrections to `66`: `0xE9` needs the zero dword; `0xF005` needs the third
  read; clients must drain the `0xB0` log first.

Artifacts: `rea-ghidra/playbook-usb-probe.py`,
`playbook-live-2026-10-20/bootrom-probe.log`,
`playbook-live-2026-10-20/bb10mt-urb-trace.log`,
`rea-ghidra/sachesi-debug-harness/`.

Next: on a backed-up unit, run CFP `resetqnxosregion` and re-read `0xE9`; only
then consider `0xC0`/`0xE0`, still with a full recovery plan.

## 2026-10-20 continuation f (hd0 mode map; CRC vs signature settled)

Started from `HANDOFF-2026-10-20-next-session.md` and closed items 1-4 of its
§5. Full write-up: `71-hd0-mode-map-and-verifier.md`; artifacts in
`analysis/hd0-modemap-2026-10-20/`. No device/NVRAM/flash writes.

- **Mode map (item 1).** Ghidra auto-analysis + LLVM dual-mode byte checks
  over `0x80008000`-`0x8001D044`. Two corrections: **`0x8001404a` is Thumb,
  not ARM** (bytes `2d e9 f0 41` = `push.w`; the whole verifier
  `0x80013fb6`-`0x800148ac` is Thumb), and Ghidra's 32-byte ARM run at
  `0x800139c4` is Thumb. The real ARM islands are the early helpers,
  `0x80013770` (CRC-32 update), `0x800175dc`-`0x80018640` (SHA-512),
  `0x80018688`-`0x800187d0` (public-key verifier), and
  `0x80018ac8`-`0x8001c87c` (bignum/ECC).
- **Trailer settled (item 2).** `0x80014838` is a direct 32-bit compare of
  **CRC-32/ISO-HDLC** (`init 0xFFFFFFFF`, reflected table poly `0x04C11DB7`,
  final XOR). The table at `0x8004CD3C` is generated at runtime by
  `0x80013ED4`, called from `0x800159F2`; it is RAM, not image data.
- **Real check beside it.** `0x80014716 -> 0x80018688` hashes
  `[start, end-0x230)` with **SHA-512** (IV verified word-for-word at
  `0x80018640`), parses a 133-byte SEC1-style public-key blob, and verifies
  the signature in the 0x230-byte trailer with a two-scalar wNAF big-integer
  operation (`0x8001B194`/`0x8001B280`, modular inverse, 68-byte limbs) - the
  ECDSA family, most consistent with P-521. The public-key path is the product path.
  The two wrappers (`0x80014548`/`0x8001453c`) only set the hash-checkpoint
  flag; the CRC-vs-ECDSA choice is `FUN_8000911a`, i.e. CRC is strictly
  factory-gated (RPMB `"BOOTCTR_FCTRY"` present, value 1..50) and needs
  `region[0xd4]==6`. The RPMB read is authenticated: `0x80011690` ->
  `0x80010a34` (512-byte frame, 16-byte nonce, 32-byte MAC) ->
  `0x800107b6` -> `0x80017758`, which calls secure-monitor services **0x2E**
  (MAC compute; caller compares), **0x33** (load 16-byte key), **0x2F** (authenticated write) and
  **0x12** (nonce) through `0x800183dc` -> `0x80009078` (ARM `smc #0`,
  `r6=0xFF`, `r12=0`). The 14-byte tag compare is only a name check; the MAC
  is produced by the secure monitor from a runtime key/context buffer. The
  provisioning path `0x800090a0(1)` creates the record with value `0x32` (50).
- **QNX-side confirmation.** `playbook-dev/dumped4869ifs/proc/boot/rpmb-omap4`
  is the `/dev/rpmb` resmgr (record catalogue, metadata/block checks, nonce,
  KEK-derived RPMB key); records are mutable and the counter blocks replay,
  not a new value. `trustzone-omap4` has `secp521r1_b`/`secp521r1_ma`,
  `SHA512_IV`, the `husw_SHA512*` family, ECDH and `/dev/rpmb/BSIS_PRIV`, so
  the bootloader verifier is P-521. `loader_06001A06-00.bin` reads
  BOOTCTR_FCTRY via opcode 0xDE and carries set/decrement helpers
  (`0x8011E250`/`0x8011E1EC`) with no caller in that build; cfp's RAM agent is
  the likely pre-OS writer.
- **Gating.** NVRAM `0x2004 == 0xAAAAAAAA` is **not** a verification bypass: it
  is read only after public-key success, and it selects "enter OS" instead of the
  `0x800156E8` **nuke**. `0x2040` is written by this stage at `0x800149AE`
  (increment), `0x80015700` (=1), `0x8001588C` (=2), `0x800158D6` (=3).
  **No writer of `0x2004` exists** in hd0, the boot ROM, the lower ROM, the
  ramloader, or hd4/hd5; it is written by the update path (CFP/OS updater).
- **No SMC** in the region-verifier chain (`0x80014716`/`0x80018688`/
  `0x80014788`); the RPMB record path above does use SMC #0 via `0x80009078`.
  ARM SMC veneers at
  `0x80019394`-`0x800193F4` (services `0x100`/`0x102`/`0x109`/`0x112`) and the
  Thumb wrapper `0x800043B4` have no caller in this image. The certificate
  blocks (`0x200`/`0xC00`/`0x4000`) are never referenced by this stage.
- **Open:** the public-key blob pointer literal at `0x800187D0` is `0x800143D9`
  (one byte into the valid-window literal pool; first byte `0xF8`, rejected by
  the parser), and the bignum constant pointer `*(0x8001B188)=0x800144E8`
  lands on Thumb code. Either the MLO header (`MLO`/`PRIMAPP`/`KEYS` records at
  `0x0`) loads additional sections / relocations, or the public-key path is not
  used on this build. Resolve before treating the ECDSA key layout as proven.

## Next session

Start from **`HANDOFF-2026-10-21-next-session.md`** (written at the end of the
2026-10-21 CFP/RPMB session). It covers the QNX-side RPMB resmgr/trustzone
findings, the CFP `0xDE` BOOT_MODE protocol, the factory-counter boot-flow
correction (a valid counter alone nukes; the CRC path needs the `0x2801`
recovery branch), and the next work: parse `bb10mt/loaders.dat` for a loader
that implements the `0xDE` action-2 write, then add it to `bb10mt`/`bbusb`.
The ECDSA key-pointer anomaly remains the top product-path item. The prompt
below is the previous one, kept for history.

## Previous session prompt (2026-10-20 earlier)

"Read `bootdumps-2026-09-11/analysis/HANDOFF.md`, `METHOD.md`, the topic reports,
the consolidated report, and the continuation docs (`60`-`66`) plus
`rea-ghidra/` (including `evidence-bundle-2026-10-19b.json` and
`loader-evidence-bundle-2026-10-20.json`) and `playbook-live-2026-10-20/`.
REA 6.7.0 + Ghidra 12.1.4 are configured for Command Code; reopen the bootrom
with `open_binary` (`raw-binary` / `arm` / `0x40028000` / `ghidra`), or the
ramloader with (`raw-binary` / `arm` / `0x80100000` / `ghidra`).

Items (a) and (b) are complete: `63` reconciles the 218 Ghidra boot-ROM
procedures (203 exact + 15 legitimate extras; 79 map rows absent, of which 10
are non-functions and 65 are genuine unseeded starts), and `64` labels 17 public
ROM-API entries, decodes `0x4002F9F8` as an EMIF/DDR descriptor (command 8 =
SDRAM boot), pins `0x4002F7C4` gating, and fully decodes the MMCHS command word
(`DE = DMA Enable`).

Item (c) was advanced live (`65`): root SSH via BerryShell/qconn, the
NS-readable SAR/monitor window (`0x4A326A00`-`0x4A326CCF`), live IRAM, and the
corrected hole boundary (`0x40026F28`-`0x40026FFF` readable-zero;
`0x40027000`-`0x40027FFF` firewalled). The hidden dispatcher page and the secure
effect of `0xF0`/`0xFE`/`0xF1` remain unreadable.

`66` adds the ramloader opcode map and the `resetqnxosregion` lead. Next work,
in value order: (1) on a backed-up unit, read `0xE9` (NVRAM `0x2074`) before
and after a CFP `resetqnxosregion` run, then inspect `0xC0`/`0xE0`; (2) test
the Sachesi Debug Mode EP1 (`0x01`/`0x81`) endpoint with the normal ping and
`kCommandUnkn1=0x11`; (3) create Ghidra function entries for the 65 genuine
unseeded boot-ROM starts and decompile them; (4) label the remaining 31
undocumented ROM-API entries; (5) integrate the SAR/monitor ABI (`r0`=service,
`r6=0xFF`, `0x26`/`0x27`) into the kexec CPU1 work. Do not use destructive
loader opcodes (`0xC8`/`0xC6`/`0xD4`/`0xD5`/`0xE8`/`0xEE`/`0xF3`/`0xFD`) or
unsafe SMC IDs without a recovery plan; CREAD remains RSA-gated."

## Do NOT overwrite

`../README.md` and `../BOOTROM-RE.md` are prior work. New output goes under
`analysis/`.
