# Session 2 (absolute) — ROM-API sweep, the live bootrom/ramloader, and the first `hd0` pass

> Numbering: absolute **2** of 12, **pre-Linux** (no Linux-era number). This
> session wrote `67`, `68`, `69` and `70`; its handoff is
> `HANDOFF-2026-10-20-next-session.md`. Session 3 (newer) wrote `71` and the
> CFP/RPMB addenda; sessions 4–6 wrote `72`–`102`. Read *Superseded* before
> reusing `70`'s mode/`0x2004`/`0x2074`/`0x205a` wording. `NUMBERING.md`.

Date / span: 2026-10-20 (device logs are ART = UTC−3). The `71`/RPMB review at
the handoff boundary is recorded in §8; that is session 3's analysis, not this
session's static pass.

Unit state at the end: **no NVRAM, RPMB, eMMC, flash or boot-chain writes.**
The loader was written to RAM only; `hd0head.bin` still hashed
`dda9d955003302adf1f0d41580c50c0c9035126487e7ac53793f4d38a3a23e3d`. The
reference unit was used read-only (password `0000`; loader `0xE7` read
`PIN 0x50175127`); the expired 32 GB unit stayed parked. The live `0x2074`
record was captured as a **pre-CFP baseline only** — no `resetqnxosregion`,
`0xC0` or `0xE0` run, so there is no after snapshot. No boot ROM or `hd0head.bin`
bytes were modified.

## Goal of this session

Start from `HANDOFF.md` and close its next-work list:

1. seed the 65 boot-ROM starts Ghidra's recursive descent missed and decompile
   them;
2. label the 31 undocumented ROM-API entries;
3. read NVRAM `0x2074` before and after a CFP `resetqnxosregion`, then inspect
   `0xC0`/`0xE0`;
4. test the Sachesi Debug Mode EP1 transport (`0x01`/`0x81`, `0x11`);
5. fold the SAR/monitor ABI into the kexec CPU1 work.

The operator then redirected the project: `resetqnxosregion` was only an
example of a hidden CFP command, never the goal; the goal is to boot an
unsigned/modified OS or Linux. The kexec project lives at
`debian-dev:/home/psyden/playbook-dev/`, and the findings were to be published
there. The last third of the session therefore became the first pass over the
RIM second-stage bootloader on `hd0` at `0x14900`.

"Done" became: items (3) and (4) complete, the live bootrom→ramloader→NVRAM read
working, EP1 answered as a firm negative, `hd0` mapped far enough to name the
verifier and its two leads, and a self-contained handoff.

Safety rules in force for the live work: only read-only loader opcodes
(`0xB0`, `0xB4`, `0xE7`, `0xD1`, `0xE9`); no `0xC8`/`0xC6`/`0xD4`/`0xD5`/
`0xE8`/`0xEE`/`0xF3`/`0xFD`; `0xC0`/`0xE0`/`0xE8`/`0xDA` are NVRAM writes and
the known brick path; CREAD stays RSA-gated; no NVRAM/RPMB/eMMC/flash write
without a recovery plan. The probe and the Sachesi harness are read-only by
construction. [handoff §6; `69` §10]

Process rule: static work was to go into background agents to keep the main
context lean. The heavy Ghidra runs were ultimately done inline because an agent
hit a tool-permission denial; only report writing was delegated. That is why
`67` and `68` carry the exact `analyzeHeadless` command lines and the raw dump
paths. [session tooling]

## Confirmed (measured, read in a file, or seen on the bench)

### 1. The 65 missed boot-ROM starts, seeded and decompiled (`67`)

- Target: `omap4430-bootrom-0x40028000.bin`, 49 152 B, raw, base `0x40028000`,
  ARMv7-A, mixed A32/Thumb-2, SHA-256
  `fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`
  (`67` header). Ghidra 12.1.4 headless, `ARM:LE:32:v7`, `BinaryLoader` at
  `0x40028000`, a Java post-script that sets the ARM `TMode` context
  (1 = Thumb, 0 = ARM) at each entry, disassembles with `followFlow`, creates a
  function, then decompiles it (`67` §1).
- **65/65 created and decompiled.** The report's status line counts 61 bodies
  semantically usable and 4 decompiler artefacts; §6.1 names the two SMC-island
  entries `0x4002CEB0`/`0x4002CEBC` (each decodes the other's bytes) and §6.2
  names three structurally merged bodies (`0x400288F8`, `0x40028990`,
  `0x4002882C`), so the count is best read as overlapping categories rather
  than a disjoint list. Function *extents* in those places are Ghidra
  artefacts; do not read the size column as a code range. [`67` §6.1–§6.2]
- Seven entries needed the conflicting instruction cleared before the context
  could be rewritten: `0x40028990`, `0x4002CEBC`, `0x4002D1A4`, `0x4002E0B6`,
  `0x4002E6E4`, `0x4002E6F4`, `0x4002EF14`. Those are exactly the cases where
  Ghidra's prior decode disagreed with the byte-verified mode map of `60`, so
  they are independent support for that map. [`67` §1]
- **`0x4002CE90`–`0x4002DB92` is the OMAP4430 USB OTG (Mentor/MUSB-class)
  device-mode driver**, controller base `0x4A0AB000` (pool `0x4002D2B0` =
  `0x4002D6B8` = `0x4A0AB000`), reached from the per-driver entry for device ID
  **`0x45` = DEVICE_USB**. 26 of the 65 seeded entries belong to it. [`67` §2,
  §3.6, N3]
- The other clusters named in `67` §2 (the groupings overlap, so they do not sum
  to 65): MMCHS host primitives (9), MMC/SD card layer (11), flash/NAND (13),
  control/interrupt helpers (6), startup/IRAM (3), cache/TLB (2), monitor
  services (3), cmd-8 DDR open (1).
- Behaviour that mattered for later boot-chain work:
  - The IRAM copy table at `0x4002FF80` is a 4-word record `{src, dst, len, fn}`;
    `0x4002882C` calls the record's own `fn` indirectly, and bit 0 of `fn`
    selects Thumb. Record 0 is `{0x0002FFC0, 0x4030D000, 0x40, 0x00028870}`
    (ARM `memcpy`), and the table ends at `0x4002FFC0`, where the second vector
    table starts. [`67` §3.1, N1]
  - The CPU1 reset path waits on `[0x48281400] & 0x200`, executes `smc`, then
    loops until `[0x48281800] != 0` and calls through `[0x48281804]`. The ROM
    therefore jumps to an address supplied in the WakeupGen/SAR window, not a
    fixed entry. [`67` §3.1, C10]
  - MMC identification sets a fixed MMC RCA `0x1234`, has an explicit voltage
    failure path (`(R3 & 0x00FF8000) == 0` fails the whole init), sends CMD2
    with the OCR word as its argument, and re-sends CMD55 before every ACMD41
    retry. [`67` §3.3, N7]
  - MMC capacity uses **CMD8 SEND_EXT_CSD** and `SEC_COUNT` at block byte
    `0xD4`, not CMD9/CSD. SD uses CSD-v2 `C_SIZE << 10`. [`67` §3.3, C4/N8]
  - The vendor "region" protocol is three commands: index 45 `0xA1000000` reads
    an 8-word table; index 45 `0xB1000000 | (i<<16)` reads one block per entry
    and stores bit 1 of the word at byte `0x110`; index **43** `0x2B1B0000`
    (R1b) selects exactly one region, first flag `== 1`, scanning 1..7 with 0
    as fallback. [`67` §3.3, N9]
  - NAND geometry has an `ID[3]`-driven fallback when the record's two size
    nibbles sum above 4; Toshiba `ID[0] == 0x98` with the known ID[1] range is
    exempt. The flash ID is cross-checked against a mirrored pair at
    `base+0x1E000`. [`67` §3.4, N10–N11]
  - `0x4002D0BA`/`0x4002D126` are **USB** indexed-window TX/RX, not MMCHS:
    `+0x204`/`+0x214` receive `ep*0x10 + {6,4}`, and `+0x208`/`+0x218` receive
    buffer pointers. `0x4002DDD4` remains the only MMCHS command-word writer.
    [`67` §4, C5]
  - `0x4002CE90` is the argument-setup half of monitor service `0xF0`, reading
    the request block at `0x4030DC1C` and branching on types 1–3; the ARM veneer
    `0x4002CEB0` issues `smc #0` with `r12 = 0xF0`. [`67` §4, C7]
- Method notes: the raw import's auto-analysis produced **145** functions, not
  the 218 the REA-provider session reported; the provider seeds more starts.
  Sixteen of the 31 API targets were already functions, fifteen needed
  `TMode=1` + disassembly + creation. [`68` §1]

### 2. The 31 undocumented ROM-API entries, labelled (`68`)

- All 31 are genuine Thumb entries; 16 were already Ghidra functions and 15 were
  created from the TRM Figure 27-6 table. Confidence totals: **26 high, 3 medium
  (9, 31, 39), 2 low (4, 6)**. [`68` §1–§2]
- Relabels that changed the map:
  - **`0x50000000` is GPMC**, not USB and not a generic "0x50000000 helper";
    the GPMC ECC/CS/timing entries (11–15, 44–47) resolve against it.
    [`68` §2.1]
  - Entry **8 is `SYS_BOOT`**: `ldrb r0, [0x4A0022C4]` returns
    `CONTROL_STATUS[7:0]`, used to index the media table. [`68` §2, idx 8]
  - Entries **36–38 are a FAT reader**: `fat_open`, `fat_find_dirent`,
    `fat_build_lba_list`; the MMC driver calls `fat_open(..., "MLO", ...)`.
    [`68` §2, idx 36–38]
  - Entry **43 is `STD_FUSE_CH_SPEEDUP_DIS`** (bit 13 of `0x4A002220`); when the
    fuse is set, `0x4002C6C0` skips all CH-block processing. This is the gate
    that decides whether the config header is parsed. [`68` §2, idx 43]
  - The peripheral bases behind the labels: `0x4A004000` = CM1
    (`0x4A0041xx` = CKGEN_CM1), `0x4A008000` = CM2
    (`0x4A0081xx` = CKGEN_CM2), `0x4A304000` = 32K timer, `0x4A307000` = PRM
    (`+0xB04` = `PRM_RSTST`), `0x4C000000`/`0x4D000000` = EMIF1/2,
    `0x4E000000` = DMM (`+0x40` LISA map), `0x4A0022C4` = CONTROL_STATUS,
    `0x4A318000` = GPTIMER1 (capture/TCAR1/TCAR2). [`68` §2.1]
- The low-confidence entries are worth remembering as *not* facts: entry 4
  (`detect_mem_device_type`, GPTIMER1 capture timing) and entry 6
  (`rom_clock_config_record`, a copied/patch PRCM record) were inferred from
  table shapes, not from a caller. [`68` §2]

### 3. The live bootrom → ramloader chain (`69`)

- Enumeration: `0fca:0001` bootrom, `0fca:8001` ramloader, `0fca:8010`
  OS/RNDIS. The host interface was `enp8s0f3u1` at `169.254.0.2/16`; the unit is
  `169.254.0.1`. USB nodes defaulted to `root:root 0664`; a scoped udev rule
  (`idVendor=="0fca"`, `idProduct=="0001"`/`"8001"`, `MODE="0666"`) was
  required. [`69` §1]
- The bootrom window is the first seconds of a power-on. A used/abandoned
  bootrom refuses `SETMODE` with `0x09` and eventually stops accepting
  channel-0 writes; a **hard power-cycle** is required (a soft reboot leaves the
  stale instance). The client must already be polling. [`69` §1; session-4
  note's `0x09` correction]
- Control framing (8-byte header): `u16 type=0 | u16 total | u8 command |
  u8 mode | u16 packetId | data`. `total` is data + 8. Channel 1/2 wrap the same
  inner layout with `u16 channel | u16 total(=inner+4) | inner`. [`69` §2]
- Handshake, all confirmed live: PING `0x01` + `14 05 83 19 00 00 00 00` →
  `0x02`; GETVAR(2) `0x05` + `{2000, 2}` → `0x06`, 736 B; SETMODE(1) `0x07` +
  a 16-byte **or** 17-byte `"RIM-BootLoader"` → `0x08`; PASSWORD_INFO `0x0A` →
  `0x0E`, 24-byte challenge; SEND_PASSWORD `0x0F` + `00 00 40 00` +
  `HashPassV2` → `0x10`. [`69` §3]
- The password is **`0000`**. The challenge is always issued:
  `challenge = data[4:8]`, `salt = data[12:20]`, `iterations = u32(data[20:24])`
  = **5060**, attempts `10/10`. Empty password is rejected. After `0x10` the
  device queues a channel-0 `0x13` ReadyForDataTransfer; bb10mt consumes it with
  an extra read, and a client that does not leaves a frame that makes the next
  OUT NAK. [`69` §3]
- Metrics: hardware ID `0x06001A06` is at payload offset **16** (`TBRMetrics`
  starts at payload+4); `"RIM BlackBerry Device"` follows. [`69` §3]
- Upload: `0xF009` setup `{addr=0x80100000, size=238128}`, 916 × `0xF004`
  260-byte chunks, `0xF00A` verify. The whole 238,128 B transfers in **0.8 s**;
  every chunk is acknowledged. [`69` §4]
- **`0xF005` EXECUTE_RAM is acknowledged but does not jump.** The bootrom then
  expects **one further read** after the channel-1 response pair; that third
  read returns `0b 01 01 00` (cmd `0x0B`, mode `0x01`, packetId `0x0001`).
  Only then does the unit stop answering as `0fca:0001` and re-enumerate as
  `0fca:8001`, about 3–4 s after `0xF005`. This was the single difference
  between the working reference and the first client. [`69` §4]
- `0x0B` LoadTransferredData on channel 0 returns `0x0C` but **reboots** the
  unit when no transferred-data session is pending (Sachesi's source warns
  exactly this); it is not an alternative hand-off. [`69` §4]
- Ramloader: SETMODE(2) `"RIM-RAMLoader"` → `0x08`; PASSWORD_INFO → `0x10`
  immediately (no challenge), one extra read follows. [`69` §5]
- **Drain the paged Bugdisp log first**: `0xB0` + 8 zero bytes → `0xB5` pages of
  512 B; repeat until the response is `0xD0`. On this unit the log was
  **>64 pages** (it grows with each session). If not fully drained, every later
  response is a stale `0xB5` page and all commands appear to return the same
  512-byte blob. [`69` §5]
- Then the normal queries work: `0xB4` → `0xD2`, 188/200 B (Samsung `MCGAFA`);
  `0xE7` → `0xD1`, 4 B **PIN `0x50175127`**; `0xD1` → `0xC8`, 4 B SVN `0`.
  [`69` §5; the PIN is the reference unit, later identified as the 64 GB unit]
- Tooling: the probe is `rea-ghidra/playbook-usb-probe.py` (`watch`, `metrics`,
  `e9`, `ep1`). The bb10mt URB trace was taken with an `LD_PRELOAD` interposer on
  `USBDEVFS_SUBMITURB`/`REAPURB`. The first `LD_PRELOAD` attempt saw nothing
  because bb10mt statically links libusb, and `USBDEVFS_BULK` was the wrong ioctl
  (libusb uses async URBs); the async-URB hooks are what produced the trace, in
  `playbook-live-2026-10-20/bb10mt-urb-trace.log`. [session tooling; `69`
  artifacts table]

### 4. NVRAM `0x2074` baseline — item (1), half done (`69` §6)

- `0xE9` reads NVRAM record `0x2074` (24 bytes) and returns command `0xFB`.
  The handler requires the **first payload dword to be zero**. With a
  zero-length request it reads the frame's CRC bytes as that dword, fails the
  check, and returns `0x15` with payload `0x04`. With a **4-byte zero payload**
  the read succeeds. [`69` §6]
- Live result, before any CFP run:

  ```
  resp = 0xFB, payload = 28 bytes
  status dword = 0x00000000            (success)
  record[0:24] = 03 00 00 00 00 00 75 01 00 00 00 00
                 00 00 00 00 00 00 00 00 00 00 00 00
  ```

  Decoded: dword 0 = **3**; u16 at `+4` = 0 (requested/actual region 0); bytes
  `+6/+7` = `0x75 0x01` (little-endian `0x0175`); remaining 16 bytes zero. This
  is the pre-CFP baseline. [`69` §6]
- Cross-check with the later live reads: this is an `OsRegionV3`-shaped record
  (`version=3`, requested=actual=0, `boot_count_0=0x0175`, `max=0`), earlier
  than the `0x017B`/`0x017C` readings in session 5 (`83` §2.3/§3, `76` §4). The
  24-byte `0xE9` read is the prefix of the same record; `70`'s 0xC1-byte buffer
  is the bootloader's prefix view, corrected by `83` §2.4.
- **What was not done:** no CFP `resetqnxosregion`, so no after snapshot; no
  `0xC0` COMPLETE or `0xE0` installer (both write NVRAM/flash and need the
  CFP-equivalent backup and recovery plan); `0xE9` was read once, not twice for
  stability. The report says this explicitly. [`69` §6, §9]

### 5. EP1 / Sachesi Debug Mode — item (2), closed negative (`69` §7)

- Tested two ways: the independent probe, and a CLI harness built from Sachesi
  2.0.3's own `boot.cpp`, so the exact upstream code path runs (`_found = 8000`,
  EP1 `0x01`/`0x81`, 16-byte payload). [`69` §7]
- On a fresh bootrom, with and without a preceding normal EP2 `SETMODE(1)`:
  the first EP1 `SETMODE(1)` OUT (24 bytes on the wire) is **accepted**;
  nothing returns on EP1 IN `0x81` (libusb times out, `-7`); the next EP1 OUT
  (`0x11`, or a repeat setmode) **times out** — the endpoint is no longer
  serviced. Sachesi's own code then prints "Try rebooting your device manually."
  and reuses the stale stage-1 response buffer, which is why a GUI trace can
  appear to succeed. [`69` §7]
- Conclusion: the Debug Mode EP1 transport is **not a usable control channel** on
  the PlayBook bootrom; `kCommandUnkn1 = 0x11` can never be delivered on a live
  transaction. This is a dead end, not a hidden console. Normal EP2 works.
  [`69` §7]

### 6. The first `hd0` pass (`70`)

- Target: `hd0head.bin`, `hd0` offsets `0x0`–`0x200000` (2,097,152 B), load base
  inferred as **`0x80000000`** (file offset == address offset): the boot ROM's
  metrics pointer `0x80014674` lands inside the image, and Thumb disassembly at
  `0x80014900` is coherent while ARM is not. [`70` §0]
- Layout: `0x0` MLO-style header `{0x4000, 0x4D70, 0, 0, 0}`, name `"MLO"` at
  `0x14`; `0x200` `"CertPK_"`; `0xC00` `"CertPPA"`; `0x4000` `"CertISW"`;
  code from `0x8000`; verification strings/code `0x143F8`–`0x148D0`; bootrom
  main `0x80014904`; metrics `0x1D044`/`0x1D050`; `0xFF` fill from `0x20000`.
  [`70` §1]
- Media/region facts used here: `hd0` is the eMMC second stage; `hd0head.bin`
  is its first 2 MB; the region-0 IFS sits at media `0x100000` and is loaded at
  `0x800FF800`, word 0 `0xEA0001FE` → `0x80100000`; `0x2074 +4` bit 0 is the
  region the verifier boots. The 64 KiB block/MCT map and the region-0/1
  extents were measured later (`100` §1). [`70` §1/§3; handoff §4; `100` §1]
- Strings that identify the stage: `Starting RIM bootrom with UART_DEBUG
  enabled`, `OMAP4430 HS`, `SW_RESTORE support enabled`, `RPMB not
  provisioned`, `FACTORY`/`PRODUCT`, `BOOT_MODE: %s`, `BootModeCount
  expended`, `Leaving bootrom and entering OS`. [`70` §1]
- **Mixed ARM/Thumb.** The stage is interleaved. Thumb: `0x80014148` (region
  verifier), `0x800142A8` (verifier main), `0x80014610`, `0x8001468C`,
  `0x800146F4`, `0x80014716`, `0x80014788`, `0x80014904`, `0x80013FB6`,
  `0x80013F64`. ARM: `0x80013770`, `0x80018688`, `0x8001A7B4`+ (bignum/crypto).
  The `0x8001404A` entry was listed as ARM (`e92de9f0`) in `70` §2; **that was
  wrong** — see *Superseded*. [`70` §2; `71` §1]
- `0x2074` was read as `NvOSRegionStruct` into a `0xC1`-byte buffer; the size
  check is `size - 0x18 < 0xA9`, accepted **`0x18..0xC0`**. Fields as read:
  `+4` requested region bit 0, `+5` last verified region, `+6+2*i` u16
  per-region counters, `+0xA` u16 max (`0` = unlimited). The two-region verify
  loop clears the other counter, increments this one, stores the index at `+5`,
  and writes `0x2074` back; at/over max it logs `Exceeded boot count`. [`70`
  §3]
- Per-region verifier `0x80014148` (Thumb) validates a pointer window
  **`0x800FF800`–`0x800102B0`**, `desc+0x9C == 0xC3D0E193`, length
  `> 0x1FFFF`, `end-8 == 0xD7C82D1F`, and `end-4 != 0xFFFFFFFF`. It reads NVRAM
  `0x205A` (8 B), and if `(*(region+0xD4) & 0xFF) < 0x20` selects a bit, changes
  the compared value. It scans records via `0x8001569C(0xB, ...)` for one with
  `[2] == 0x11`; if `[3] == 1`, XORs `0x4000000`. Then the compare
  `(value & 0xFF00FFFF) == (*(region+0x7C) & 0xFF00FFFF)`, bits 16–23 masked
  out. Finally one of two paths: `0x80014716` (mode 0) or `0x80014788` (mode 1,
  10,000-byte chunks + trailer). [`70` §4]
- Trailer path `0x80014788`: `stored = *(u32 *)(region_end - 4)`; if
  `stored == -1`, status 7; otherwise compute a digest over
  `(end - start - 4)` bytes and compare. The assembly is unambiguous at
  `0x80014838`: `cmp r4, r7; beq success`, where `r7 = *(end - 4)` and `r4` is
  the digest finaliser's return (`0x8001A7C8`). **I deliberately did not
  conclude "CRC32, therefore forgeable"**: the digest machinery
  (`0x8001A7B4`, `0x80013770`, `0x8001404A`, `0x80018688`) is ARM, not Thumb,
  and had not been decompiled correctly. Two possibilities remained: a 32-bit
  CRC/checksum compared directly (forgeable), or a wrapper whose 32-bit return
  is a status/handle while the real RSA/ECC check lives in the ARM crypto area
  or the secure monitor. The `CertPK_`/`CertPPA`/`CertISW` blocks and the large
  bignum routine at `0x8001A7D4` made a real asymmetric check likely somewhere
  in the chain. [`70` §5]
- `0x2004 == 0xAAAAAAAA` was read as: reached only after the first
  `0x80014548` (mode-0) verifier returns 1 and several pointer checks pass; it
  branches to `LAB_80014A90` ("Leaving bootrom and entering OS"), skipping the
  **second** (`0x8001453C`, mode-1) loop. It does not skip the first verifier.
  No loader opcode writes `0x2004`. [`70` §6]
- Candidate bug surfaces, in the order `70` §7 put them: (1) the ARM crypto area
  `0x8001A7B4`+; (2) TOCTOU between verify and use, because the valid window is
  `0x800FF800`–`0x800102B0` and the image is walked in 10,000-byte chunks with
  nothing freezing it; (3) the `NvOSRegionStruct` variable size
  `0x18..0xC0`; (4) the `0xFF00FFFF` mask (bits 16–23 ignored); (5) the
  `0x2004` semantics/writer; (6) boot-count logic (`max == 0` is unlimited, not
  skip). [`70` §7]

### 7. The `71`/RPMB review at the handoff boundary (session 3's analysis)

This session's handoff was reviewed by the follow-on session, which produced
`71` and the CFP addenda. I record the corrected facts here because they change
how the `70` leads must be read; the analysis is session 3's, not this
session's static pass (see `session-03.md`).

- **The mode map was corrected.** `0x8001404A` is **Thumb**, not ARM: the bytes
  are `2d e9 f0 41` = Thumb-2 `push.w {r4-r8,lr}`; `e92de9f0` does not occur
  there. The whole verifier region `0x80013FB6`–`0x800148AC` is Thumb. The
  Ghidra auto-analysis run at `0x800139C4`–`0x800139E4` is also Thumb, not ARM.
  [`71` §1]
- **The trailer check is CRC-32/ISO-HDLC**, not a status: init `0xFFFFFFFF`,
  reflected polynomial `0xEDB88320`, final XOR, over `[start, end-4)`, stored
  at `end-4`; the table generator at `0x80013ED4` reproduces the standard table
  and `"123456789" → 0xCBF43926`. [`71` §2.1; `89` §4]
- **The product path is SHA-512 + an ECDSA-family P-521 verify**, not an SMC:
  `FUN_80014716` → `FUN_80018688` → `FUN_8001B194`; 66-byte coordinates,
  68-byte limbs, SHA-512 digest; the QNX `trustzone-omap4` carries
  `secp521r1_b`/`secp521r1_ma` and the SHA-512 family. Neither verifier chain
  executes an SMC. [`71` §2.2; later `73`]
- **The selector is `FUN_8000911A`, which reads RPMB `BOOTCTR_FCTRY`**:
  `0` → product (ECDSA), `1` → CRC (requires the descriptor byte
  `[0xD4] & 0xFF == 6`; `desc = hdr+0x2C`, so this is image offset `0x100`, not
  `hdr+0xD4` — `89` §1.3), `2` → status `0x0F`/failure. The `0x800149C2 cbnz`
  on the **normal** path nukes on any nonzero selector; the CRC-only verifier
  is reached through the `0x80014998` retry branch when `FUN_80014F84() != 0`
  — later identified as the `0x2801` password attempt budget
  (`A < clamp(B,3,10)`). A valid factory counter on the normal path therefore
  nukes; the retry branch is required. [`71` §3; later `75`/`76`]
- **`0x2004 == 0xAAAAAAAA` is not a verification bypass.** It is read only
  *after* the public-key verifier has returned success; its effect is "the
  region is committed/bootable; do not nuke". Verification failure jumps to the
  nuke before the `0x2004` read. `70` §6's "skips the second verifier loop" is
  true only in the narrow sense that success skips the `LAB_80014A16` nuke.
  [`71` §3]
- **`BOOTCTR_FCTRY` is an authenticated RPMB record** (4 bytes, value
  **1..0x32 (50)**). `0` and `>0x32` both fail; `FF FF FF FF` produced
  **"Expired factory mode"** and the unit would not boot. The stage provisions
  an **absent** record with `0x32` via `0x800090A0(1)`; it does not repair an
  invalid one. The backing block is a packed record store with an `0x18`-byte
  header (magic `0x5983BC2D`, 14-byte name, u16 length at `+0x16`, data at
  `+0x18`). [`71` §7b]
- **The 14-byte tag compare is not the security boundary.** The block read
  (`0x80010A34`) gets a 16-byte nonce via secure service **0x12**, builds a
  512-byte frame (`+0x1FE = 4` read request, `+0x1F8` RPMB address, nonce at
  `+0x1E4`), drives the eMMC block layer, checks `+0x1FC == 0`, re-compares the
  nonce, computes a 32-byte MAC via service **0x2E**, compares it with `+0xC4`,
  and on mismatch writes back via service **0x33** (load 16-byte key) then
  **0x2F** (authenticated write), with a write-counter check via **0x31**. All
  service calls go through `0x800183DC → 0x80009078` = ARM `smc #0`, `r6=0xFF`,
  `r12=0`. The key is not in the static image; the `0x2E` request carries a
  runtime 32-byte key/context pointer at `0x80033760`. [`71` §7b; `65`]
- **The OS side is the writer.** `rpmb-omap4` is the `/dev/rpmb` resource
  manager (`iofunc_read_verify`/`iofunc_write_verify`, record catalogue,
  metadata/block consistency, nonce) and derives the RPMB key from a hardware
  KEK (`"RPMB key util: RPMB key gen with KEK %d"`, `select_kek`,
  `choose_hw_kek`), so the key is not a fixed constant. Records are mutable;
  the monotonic counter prevents replay, not a new value. `trustzone-omap4` also
  reads `/dev/rpmb/BSIS_PRIV` and decrypts it with a KEK. [`71` §7b]
- **The operator's `FF FF FF FF` was written from the booted OS over SSH**, by
  creating the `BOOTCTR_FCTRY` file inside `/dev/rpmb` — **not** by CFP. The
  operator corrected this explicitly; the handoff's §4b and `71` §7b were
  rewritten to say `/dev/rpmb`, not CFP. The revised rule is not "never write
  RPMB": the record is writable from userland, and writing `0x32` is the normal
  operation. The hazards are (a) the writer needs a booted OS, so a wrong value
  removes the only easy writer, and (b) the write is secure-world-keyed and
  monotonic. Keep the working unit off-limits; a write is a sacrificial-unit
  experiment. [`71` §7b; operator statement; later `116`/`92`,
  `DECISIONS.md` §8]
- **CFP `RESETQNXOSREGION` is not the counter command.** It is a hidden CLI
  command whose handler calls `FUN_00422270(4)` and logs `"QNX OSREGION NVRAM
  record cleared."`; it drives the loader's OSREGION/NVRAM `0x2074` reset and
  never touches RPMB `BOOTCTR_FCTRY`. The boot-count setter is separate:
  CFP.exe `FUN_0044d400` / cfp-old `0x48BAE0` send loader command **0xDE** with
  `{u32 action, u32 value}` — action 0 read, action 1 switch (1 = FACTORY),
  action 2 set count (1..0x32). No CLI command invokes action 2, and the
  PlayBook loader's `0xDE` is read-only (the setter/decrement helpers exist but
  have no caller). [`71` §7c; `72` §3–§6]

## Inferred (reasoning from evidence, not directly measured)

- **The boot ROM is a real USB device stack, not a stub.** 26 of the 65 seeded
  entries are a MUSB-class device-mode driver with a transfer state machine,
  FIFO and indexed-window paths, and an asynchronous status/callback ABI
  (`handle+0x1C` status; `0 = complete, 1 = error/idle, 4 = more to do, call me
  again`; callback at `handle+0x04`; second callback at `+0x2C`). Strength:
  strong from the decompiled call graph; bit *names* are inferred from usage.
  [`67` §3.6, N3–N5]
- **The ROM's boot-media stack is a complete MMC + FAT + NAND chain.** The
  `fat_open(..., "MLO", ...)` caller, the CMD8/EXT_CSD capacity path, the
  vendor region select (45/43), and the NAND ID/geometry path together explain
  how the ROM finds and loads `MLO`; this is the layer the RIM second stage
  replaces. Strength: strong for the functions; the exact media/partition
  selection is inferred from the tables. [`67` §3.2–§3.4; `68` idx 36–38]
- **The `0xE9` baseline is the pre-CFP `OsRegionV3` record.** Field decode:
  `version=3`, requested=actual=0, `boot_count_0=0x0175`, `max=0`, flags/epoch
  zero. This matches the later 72-byte `NvOSRegionStruct` V3 (`83` §2.3) and is
  earlier than the `0x017B`/`0x017C` readings. Strength: strong for the field
  decode; the count gap is consistent with normal boots, but it was not measured
  per boot. [`69` §6; `83` §2.3/§3; `76` §4]
- **The `0x800187D0 → 0x800143D9` key pointer is not key data in the static
  image.** The byte at `0x800143D9` is `0xF8`, which the SEC1 parser rejects,
  and `*(0x8001B188) = 0x800144E8` is Thumb code, not a bignum constant. I
  listed three candidate explanations: (a) a multi-section MLO whose other
  sections populate a key slot, (b) a load-time fixup, (c) the path is not
  exercised on this build. Strength: the observations are exact; the
  explanation was unresolved this session. [`70` §8; `71` §5]
- **The trailer could be a CRC or a status, and the probability still favoured
  a real asymmetric check somewhere.** The direct 32-bit compare is CRC-shaped,
  but the certificate blocks and the bignum/crypto area make a wrapper or a
  separate verifier plausible. Strength: moderate; this was deliberately left
  as a lead, not a conclusion. [`70` §5]
- **The factory/CRC path is RPMB-gated, not NVRAM-gated.** The selector reads
  the RPMB record; NVRAM writes cannot reach it. The CRC path is therefore a
  factory/unsigned-OS path, not a product bypass. Strength: strong —
  the selector and the range check are read directly. [`71` §2/§7b]
- **The expired unit cannot be recovered through `/dev/rpmb`.** That writer
  needs a booted OS; a value outside 1..50 prevents the OS from booting. The
  remaining routes are CFP or the ramloader's RPMB path (both run before the
  OS), or physical eMMC access. Strength: strong for the known interfaces; the
  ramloader path was a lead, not tested. [`71` §7b/§8; later `92`/`116`]
- **The reference unit is the 64 GB `PIN 0x50175127`.** The PIN is measured
  (`69` §5, `0xE7`); the capacity/identity mapping is from later notes. The
  expired 32 GB unit is the one with the invalid RPMB value; it was parked.
  Strength: the PIN is measured; the inventory is later. [`69` §5; session-5
  note]

## Dead ends and wrong assumptions (valuable — do not skip)

1. **"EP1 / Sachesi Debug Mode is a hidden console."** The first EP1
   `SETMODE(1)` OUT is accepted, nothing comes back on `0x81`, and the next OUT
   times out. Sachesi's own code reuses a stale response buffer and can print
   false success. `kCommandUnkn1 = 0x11` never reaches a live transaction.
   [`69` §7]
2. **"`0xE9` with a zero-length payload is an unimplemented opcode."** It
   returns `0x15/0x04`, which looks like "unimplemented", but the handler is
   reading the frame's CRC as its first argument. A 4-byte zero payload is
   required. [`69` §6/§8]
3. **"`0xF005` alone jumps to the loader."** It is acknowledged and nothing
   happens. The third read after the channel-1 response pair returns
   `0b 01 01 00` and is the actual go-ahead; without it the bootrom stays alive.
   [`69` §4/§8]
4. **"`0x0B` is an alternative hand-off."** It returns `0x0C` and then
   **reboots** when no transferred-data session is pending; Sachesi's own
   source warns this. [`69` §4]
5. **"Draining one Bugdisp page is enough."** `0xB0` pages `0xB5` until `0xD0`;
   this unit had **>64 pages**. A partial drain makes every later response a
   stale 512-byte page, so all commands look identical. [`69` §5]
6. **"`SETMODE(1)` returning `0x09` is a success."** `0x09` is a stale/used
   bootrom refusal; it needs a **hard power-cycle**, not a soft reboot, and the
   client must already be polling. The Python port's acceptance of `0x09` was a
   workaround, not a protocol fact. [`69` §1; session-4 note]
7. **"`0x8001404A` is ARM."** It is Thumb: `2d e9 f0 41` = `push.w {r4-r8,lr}`;
   `e92de9f0` does not occur there. The whole verifier region is Thumb. This
   came from `70` §2 and the handoff; the byte read settled it. [`70` §2;
   `71` §1]
8. **"`0x2004 == 0xAAAAAAAA` skips verification."** It is read only after the
   first verifier returns success; it skips the nuke, not the verifier. `70`
   §6's "second loop" wording was too broad. [`70` §6; `71` §3]
9. **"`0x2074` is a 0xC1-byte buffer / 0x18..0xC0."** The 0xC1 buffer is the
   bootloader's prefix view; the record is the 72-byte `OsRegionV3`. The
   70-byte reading came from treating `+0x06` as a `u16[5]` and losing two
   bytes of the `u32 flags`; there is no 70-byte version. [`70` §3; `83` §2.4]
10. **"`0x205A` is an anti-rollback/version compare."** It is the bootrom
    `modelID` match (`0x06001A06`); bits 16–23 are ignored. [`70` §4; `89`
    §3.3]
11. **"`0x80014A90` is the enter-OS jump."** It is a log/return label; the real
    hand-off is ARM `bx r0` at `0x800192A8`, with `r0 = *(0x80025E74)`, written
    by `0x80014148` from the loaded descriptor. [`70` §6; `91` §2.2]
12. **"The trailer is CRC32, therefore the boot path is open."** I explicitly
    did **not** conclude this in `70`; the ARM crypto had not been decompiled.
    The later work proved it *is* CRC-32/ISO-HDLC, but the CRC path is
    factory-RPMB-gated, and a valid factory counter on the normal path nukes.
    The CRC chain alone does not bypass the selector or the `0x2801` gate.
    [`70` §5; `71` §2/§3; `89` §4; `95`]
13. **"The 14-byte tag is the security boundary."** It is a plain `memcmp`;
    the secure-service `0x2E` MAC and the nonce/replay check are the boundary.
    A tag-only patch or a forged value cannot pass unless that MAC/key path is
    broken. [`71` §7b]
14. **"The stage never decrements `BOOTCTR_FCTRY`."** I concluded this from the
    call graph (`0x800115E6` one caller `0x800090C0`; `0x8001167E` one caller
    `0x800090A6`). Later `89` §3.5 shows a successful factory verify at
    `0x80014A42`–`0x80014A4A` calls `0x8000911E`, which does
    read/decrement/write via `0x800115E6`; `95` measured **50 → 49**. See
    `docs/contradictions/71-bootctr-stage-decrement-vs-provision-only.md`.
    [`71` §7b; `89` §3.5; `95` §4]
15. **"Never write RPMB."** The blanket was too strong. The record is writable
    from a rooted OS via `/dev/rpmb`; writing `0x32` is the normal operation.
    The real rule is: never write a **wrong** value (0 or >50), never on the
    working unit, and never without a booted OS as the writer. The `FF FF FF
    FF` unit is the proof of the asymmetry. [`71` §7b; `116`; `92`;
    `DECISIONS.md` §8]
16. **"The `FF FF FF FF` was written by CFP."** The operator corrected this: it
    was written from the booted QNX system over SSH, by creating the
    `BOOTCTR_FCTRY` file inside `/dev/rpmb`. CFP was never the writer. The
    handoff and `71` §7b were corrected accordingly. [operator statement;
    `71` §7b]
17. **"`resetqnxosregion` might reset the boot count / is the goal."** It is a
    hidden CFP command that clears NVRAM `0x2074` (`FUN_00422270(4)`); it never
    touches the RPMB counter. It was only an example of a hidden command; the
    goal is an unsigned/modified OS or Linux. [`71` §7c; operator statement]
18. **"Another loader in `loaders.dat` may implement `0xDE` action 2."** The
    later audit (`72`) found 82 entries, 81 extractable (LDR_00 corrupt), and
    every `0xDE` handler accepts action 0 only; the setter/decrement helpers
    have no callers or pointer literals. `LDR_01` (`loader_06001A06-00.bin`) is
    the PlayBook loader and its `0xDE` is read-only. The PlayBook loader is the
    same file inside CFP's ramimage; there is no pre-OS action-2 writer.
    [`72` §1–§6; operator statement; `71` §7b]
19. **"CFP's ramimage is a different agent."** The operator corrected this:
    the ramimage is the same `loader_06001A06-00.bin`. Do not expect a hidden
    factory counter command in a different CFP build. [`72` §2; `71` §7b]
20. **"The `0x2074` after-snapshot is still pending; run `resetqnxosregion`."**
    It was never taken. The operator redirected the session to the `hd0`/native
    boot thread; there is no after state, and the 69 §6 baseline must not be
    read as a completed before/after experiment. [`69` §9; handoff §5]
21. **"The `LD_PRELOAD` interposer will show the bb10mt USB traffic."** It saw
    nothing because bb10mt statically links libusb. `USBDEVFS_BULK` was also
    the wrong ioctl: libusb uses async URBs. The working trace hooks
    `USBDEVFS_SUBMITURB`/`REAPURB`. The first attempt's empty log is not
    evidence that bb10mt did not talk to the device. [session tooling; `69`
    artifacts]
22. **"The `0x10` response is the last frame."** It is not: the device queues a
    channel-0 `0x13` ReadyForDataTransfer. Leaving it unread makes the next
    channel switch/OUT NAK. bb10mt's `PasswordInfo` consumes it with an extra
    read; a client must do the same. [`69` §3]
23. **"The 16-byte SETMODE form is wrong; only 17 bytes work."** Both are
    accepted on EP2: Sachesi sends 16 (`packetSize = 24`), bb10mt sends 17
    (`packetSize = 25`). Only the EP1 debug transport fails. [`69` §3/§8]

## Unknown / open questions

- **Who applies the `+0x9000` fixup?** The static image has no writer to
  `0x800143D8`–`0x800144F0`, and the boot ROM has no hard-coded `0x80014xxx`
  reference. The boot ROM or a first-stage image loader applies it from the
  signed header; the exact table/agent is unfound. A live read of
  `0x800187D0` during the boot window, or a RAM dump of the MLO after the boot
  ROM starts it, would settle it. [`70` §8; `71` §5]
- **What are the MLO header records?** `0x0` has three `0x20`-byte name records
  (`MLO`/`PRIMAPP`/`KEYS`) with `{0x4000,0x4D70}`, `{0xC00,0x32B8}`,
  `{0x200,0x960}`. I did not decode them. [`70` §1; `71` §5]
- **What is the exact `0x205A` bit/field and the `0x2819` blocklist?** `0x205A`
  is read as a per-OS-type model-ID override/bitmask; `0x2819` is read by the
  verifier but was not dumped. A read-only `nvram read` would settle both.
  [`70` §4; `89` §3.3]
- **Who writes `0x2004`?** No writer in the boot ROM, the lower boot ROM, the
  ramloader or `hd4.bin`/`hd5.bin`; the update path (CFP/OS) is the candidate.
  [`70` §6; `71` §3]
- **What is the exact `0x2074` field layout?** `70` read the bootloader's prefix
  view; the 72-byte `OsRegionV3` fields (`+0x0A` max, `+0x0C` flags, `+0x10`
  epoch, version IDs) were later decoded by `83`. [`70` §3; `83` §2]
- **What is the RPMB read parser's exact trust boundary?** The 14-byte name
  check is plain `memcmp`; the `0x2E` MAC covers the `0xE0`-byte payload, and
  the nonce comes from service `0x12`. Audit `0x80011690`/`0x80010A34` for
  length/offset confusion and nonce handling; the key source is the secure
  world. [`71` §7b]
- **Can the expired unit be recovered in software?** `/dev/rpmb` needs a booted
  OS; the shipped loader's `0xDE` is read-only; CFP's setter has no CLI caller.
  The remaining routes are CFP's RAM agent / ramloader RPMB path (`Fail to
  write RPMB` strings) or physical eMMC access. [`71` §7b/§7c; `72` §6]
- **What is the exact `0xC0` COMPLETE / `0xE0` installer behavior?** Not
  exercised; both write NVRAM/flash and need the CFP-equivalent backup and
  recovery plan. [`69` §9]
- **What is the SAR/monitor ABI integration for kexec CPU1?** Not started. The
  services are `r0`=service, `r6=0xFF`, `0x26`/`0x27` in `65`; the RPMB path
  uses `0x12`/`0x2E`/`0x2F`/`0x31`/`0x33` through the same `smc #0` wrapper.
  [`65`; `71` §7b]
- **What is the hidden `0x40026F28`–`0x40027FFF` page and the secure side of
  services `0xF0`/`0xFE`/`0xF1`?** Still unreadable; nothing in this session
  changes that. [`67` §6.11; `70` §8]
- **What is the USB OTG sub-block/bit naming, the `0x4030DBEC` hybrid context,
  and the request-type handlers?** The ABI-level facts (status enum, callback,
  timeout, second callback) are solid; the bit names and the `+0x10`/`+0x14`/
  `+0x18` roles are inferred. [`67` §6.7–§6.9]
- **The full `hd0` beyond `0x200000`, the MLO header decode and the key
  pointer** — the top handoff items for the next session. [`70` §8; handoff
  §5]

## Superseded / uncertain

- **`0x8001404A` as ARM — superseded by `71` §1.** This session wrote it in
  `70` §2 and the handoff. The bytes are Thumb (`2d e9 f0 41` = `push.w`); the
  verifier region is Thumb.
- **`0x2004 == 0xAAAAAAAA` skips the second verification loop — refined by
  `71` §3.** `70` §6 said it "skips the second (0x8001453C, mode-1) loop"; the
  current reading is that it is read only after the first verifier returns 1,
  and it skips the nuke (`LAB_80014A16`), not verification. The first verifier
  still runs.
- **`0x2074` as a 0xC1-byte buffer / `0x18..0xC0` — refined by `83` §2.4.**
  The record is the 72-byte `OsRegionV3`; the 0xC1 buffer is the bootloader's
  prefix view, and the 70-byte reading is a `u16[5]` misread. `max == 0` is
  unlimited.
- **`0x205A` as an anti-rollback/version compare — refined by `89` §3.3.** It
  is the bootrom `modelID` match (`0x06001A06`), with bits 16–23 ignored.
- **`0x80014A90` as the enter-OS jump — superseded by `91` §2.2.** It is a
  log/return label; the real hand-off is `0x800192A8 bx r0`, with
  `r0 = *(0x80025E74)`.
- **The trailer as "CRC or status" — resolved by `71` §2.1 and `89` §4.** It
  is CRC-32/ISO-HDLC (`zlib.crc32`), over `[start, end-4)`, but it is
  factory-RPMB-gated and requires the descriptor byte `[0xD4] == 6` (image
  offset `0x100`; `89` §1.3); `95` proved the path
  with a 5-byte reseal (`0x100: 04 → 06`, CRC `0x8D46FB4A → 0x116B2562`,
  TabletOS booted, counter 50 → 49).
- **The ECDSA key pointer as "not key data" — resolved by `73`.** The image is
  pre-relocation: linked at `0x7FFF7000`, loaded at `0x80000000`, with a
  `+0x9000` fixup. `0x8001D3D9` is a valid P-521 point; `0x8001D4E8` is
  `p = 2^521−1`; `0x8001D4A4` is `b`; `0x8001D460` is `3`. The product path is
  functional.
- **"The stage never decrements `BOOTCTR_FCTRY`" — contradicted by `89` §3.5
  and `95` §4.** The stage does read/decrement/write on a successful factory
  verify; `95` measured 50 → 49. See
  `docs/contradictions/71-bootctr-stage-decrement-vs-provision-only.md`.
- **"Never write RPMB" — softened by the operator's correction and `116`.**
  `/dev/rpmb` on a booted OS is the general writer; writing `0x32` is the
  normal top-up; a wrong value (0 or >50) is the brick. The agent still must
  not write RPMB; the guarded bench script (`bench/device-startup.sh`) or the
  operator does. [`116` addenda; `92`; `DECISIONS.md` §8; session-10/11 notes]
- **"The `FF FF FF FF` was written via CFP" — wrong.** The operator wrote it
  from the booted QNX system over SSH, creating the file inside `/dev/rpmb`.
  `71` §7b and the handoff §4b were corrected. [operator statement; `71` §7b]
- **"`resetqnxosregion` may reset the counter" — wrong.** It clears NVRAM
  `0x2074` (`FUN_00422270(4)`); it never touches RPMB. [`71` §7c; operator
  statement]
- **"The kexec route is the working one" — a project-direction statement in
  this session's handoff, later abandoned.** The operator found kexec "not
  really in a good state, riddled with bugs, unstable and random"; sessions
  5/6 pivoted to the native CRC/factory path, and the Linux-era work uses the
  CRC chainloader image. The handoff's §0 wording is historical, not current.
  [operator statement; session-6 note; `85`/`89`/`95`]
- **"`0xE9` read once is enough for a before/after" — not a completed
  experiment.** The baseline is real; the after snapshot, a second read for
  stability, and the `0xC0`/`0xE0` writes were not done. [`69` §6/§9]
- **"`0x2074` baseline `0x0175` is the current live count" — a snapshot, not a
  constant.** Later reads are `0x017B`/`0x017C`; the counter advances once per
  boot. The field decode is the durable part. [`83` §3; `76` §4]

## Artefacts produced

- `docs/reports/67-ghidra-seeded-starts.md` — 65/65 seeded starts, the USB OTG
  cluster, the MMC/FAT/NAND behaviour, the corrections to `10`/`50`/`60`/`64`.
- `docs/reports/68-rom-api-undocumented-labels.md` — 31/31 labelled; GPMC,
  SYS_BOOT, FAT reader, the fuse gate, the peripheral bases.
- `docs/reports/69-live-bootrom-loader-usb.md` — the live bootrom handshake,
  the third-read hand-off, the ramloader protocol, the `0x2074` baseline, the
  EP1 negative.
- `docs/reports/70-rim-bootrom-hd0-stage.md` — the first `hd0` map, the
  `NvOSRegionStruct`, the verifier flow, the trailer caveat, the bug surfaces.
- `rea-ghidra/seeded-starts-decompiled.txt` — the 65 seeded decompilations.
- `rea-ghidra/rom-api-undocumented-decompiled.txt` — the 31 API decompilations
  (4 952 lines).
- `rea-ghidra/playbook-usb-probe.py` — the working live client
  (`watch`, `metrics`, `e9`, `ep1`).
- `rea-ghidra/sachesi-debug-harness/` — the CLI harness built from Sachesi
  2.0.3's own `boot.cpp`, used to close EP1.
- `playbook-live-2026-10-20/bootrom-probe.log` and
  `bb10mt-urb-trace.log` — the live session log and the byte-level URB trace
  (via `USBDEVFS_SUBMITURB`/`REAPURB`).
- `analysis/rea-ghidra/hd0-first-pass/` — `hd0-decompiled.txt`,
  `hd0-crypto.txt` (the invalid Thumb decompile of the ARM crypto, kept as
  evidence of the mode-map problem), `SeedStarts.java`, `hd0starts.txt`,
  `hd0crypto.txt`.
- `HANDOFF-2026-10-20-next-session.md` — the handoff (the repo copy is the
  later consolidated version with the session-3 RPMB/`71` additions); the
  `HANDOFF.md` continuation section was updated to point at it.
- Remote: `debian-dev:/home/psyden/playbook-dev/bootrom-findings-2026-10-20/`
  — `README.md`, `monitor-sar-abi.md`, `bootrom-loader-usb.md`, and the probe.
- On-device state: **none**. No NVRAM, RPMB, eMMC, flash or boot-chain write;
  the loader was written to RAM only; `hd0head.bin` hash unchanged.

## Deliberately left out

- The full `71` mode map, CRC proof, SHA-512/ECDSA/P-521 analysis and CFP
  protocol — session 3's note and report carry them; this note records only
  what changes the `70` leads and the RPMB rule.
- The `72`–`102` work (`loaders.dat`, the `+0x9000` relocation, the live
  `0x2801` audit, `0x2074` semantics, the native-boot plan, the pre-OS IFS
  reflash) — sessions 4–6.
- The Linux-era bring-up (`103`–`122`) — sessions 7–12.
- The full `67`/`68` tables and the full `69`/`70` section-by-section detail;
  the reports are the canonical record. This note keeps the measurements that
  explain *why* the later conclusions are what they are, and the dead ends the
  reports do not spell out.
- The SAR/monitor ABI details in `65` beyond the service IDs used by the RPMB
  path; the report is the reference.
