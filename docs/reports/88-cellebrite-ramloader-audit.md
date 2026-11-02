# 88 — Cellebrite UFED signed PlayBook ramloader audit (track A)

Status: **CLOSED.** Static, read-only audit. No device was touched; no USB,
NVRAM, RPMB, eMMC or flash access of any kind was performed for this document.
Date: 2026-10-25. Supersedes the open item in `86-cellebrite-loader-lead.md` §3
and `HANDOFF-2026-10-21-next-session.md` §6 item 2 / §7 (track A of `87`).

---

## 0. Verdict (read this first)

1. **`06001A06_0000000A` is a genuine signed PlayBook-class ramloader**
   (`D7D32D1F` at +4, load `0x80100000`, `D7C82D1F` at len-8, non-blank
   signature area, and — unlike the shipped `LDR_01` — an **intact CRC-32** in
   the last 4 bytes). Both Cellebrite files verify byte-for-byte against the
   expected SHA-256.
2. **Its `0xDE` BOOT_MODE handler is read-only**, with the *same* policy as
   `LDR_01`: `if (action != 0) return without answering`. Action 1 (mode
   switch) and action 2 (set factory count) are silently dropped. Proven from
   the instruction stream (§6).
3. **The loader has no BOOTCTR writer at all.** The string `BOOTCTR_FCTRY`
   (`0x80115070`) is referenced exactly once in the whole image, from the
   reader `FUN_80114fa8`. There is no setter and no decrement helper — this
   build is *smaller* than `LDR_01` in exactly the place that matters.
4. **The flash write/erase path is address-driven and host-controlled**, but it
   is **not arbitrary**: the destination LBA comes from a host-supplied QCFM
   V2 container, and the only address gate is `FUN_8011b774`, which checks the
   range against an allow-list of MCT partitions (types `0x19`,`0x1a`,`0x1b`,
   `0x1c`,`0x1f`,`0x24`,`0x34`) rebuilt at boot by `FUN_8011b920`. Anything
   outside those extents is rejected (`0xCE` + `0x15`).
5. **Every flash-write session is RPMB-gated.** Before the first byte of a
   `0xF7`/`0xF8` session is programmed, the loader reads, modifies and
   **writes back the authenticated `FS_DIRTY` RPMB record**
   (`FUN_80115430` → `FUN_80115480` → `FUN_8010d35c`). If that authenticated
   write fails, the flash write is abandoned (`goto LAB_8010f894`). This is the
   concrete blocker for the bricked 32 GB units (§9).
6. **No arbitrary read.** `0x39`/`0x3A` do not exist in this build at all;
   `0xE4`/`0xE5`/`0xE6`/`0xEB` are gated on a session state byte (`+0xAEC`)
   which only `0xE4` sets from host-supplied key material, the `0xE5` payload
   is emitted through the session cipher, and `0xE5` is *also* range-gated by
   `FUN_8011b774`. `0xDC` is a 32-bit rolling hash over a range, not a bulk
   read. There is no plaintext eMMC read primitive.
7. **The Cellebrite patch signature is absent and irrelevant here.**
   `DB9F07EB` does not occur in any file. The two `6A0000EA` words in
   `06001A06` are both ordinary ARM `b` instructions — `0x801055F8` inside a
   flash-read helper and `0x8011F2E8` as the tail branch of the `memset`
   helper `FUN_8011F2DC`. Neither is a dispatcher `case` branch.
8. **Answer to the go/no-go question:** `06001A06` is **not** a route to
   arbitrary eMMC read/write, and it **cannot** repair the RPMB
   `BOOTCTR_FCTRY` state that bricks the expired 32 GB unit. It *is* a
   host-addressed eMMC **write/erase** primitive inside the allow-listed
   partitions, which may be usable for the *eMMC-side* part of a repair — but
   only if that unit's RPMB still accepts an authenticated `FS_DIRTY` write
   and only if the target partition is in the allow-list. See §9 for the exact
   preconditions and the remaining unknown that decides it.

**One correction to prior work:** the dword at header `+0xc`
(`0x801234FC` for `06001A06`, `0x801356D8` for `LDR_01`) is **not an entry
point**. It points at a non-code descriptor table inside the image. Execution
starts at `base+0x10` via the `b` at offset 0. `86` §1 and `66` §1 should drop
the "entry" label.

---

## 1. Provenance

### 1.1 Inputs (hashes recomputed this session)

| # | Path | Size | SHA-256 | Expected | Match |
|---|---|---:|---|---|---|
| 1 | `cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A` | 159,968 | `03ffaf26dc0ce640c2f44dc5e223bb34d15175234f26d9876846484022a5c6dc` | same | ✅ |
| 2 | `cellebrite-blackberry/ufedblackberrypack.epr/05001A06_0000000A` | 138,796 | `b5f345d36e042a582ce27e383da87250af268ded046facdef05d67ca15e18a3f` | same | ✅ |
| 3 | `bbusb/loaders/loader_06001A06-00.bin` (`LDR_01`) | 238,128 | `c7cc33c796f9518bbc0eb929c6096f2473937ad486c579764aa4c57d8165393f` | same | ✅ |
| 4 | `cellebrite-blackberry/ufedblackberrypack.epr/*` (82 files) | — | CRC-32 trailer verified per file (§4) | — | 81/82 |
| 5 | `cellebrite-blackberry/BlackBerry OS cellebrite exploit - wumbomumbo.txt`, `messages.txt` | — | source text | — | — |
| 6 | `bb10mt-main/ramloader.pas`, `bb10mt-main/uarchive.pas`, `bb10mt-main/qcfm.pas` | — | source context (not target evidence) | — | — |

### 1.2 Tools

| Tool | Version / path | Use |
|---|---|---|
| Capstone | 5.0.7 (`python3 -c 'import capstone'`) | independent disassembly, opcode-set scan |
| Ghidra headless | 12.1.4, `/home/psyden/tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless`, `JAVA_HOME=/usr/lib/jvm/java-21-openjdk` | decompilation, function/string/CFG recovery |
| Processor model | `ARM:LE:32:v7`, `BinaryLoader -loader-baseAddr 0x80100000` | 770 functions, 57 defined strings, image `0x80100000`–`0x801270E0` |
| Python | 3.14 (`zlib`, `struct`) | header/CRC/signature-area checks |

Ghidra script used (throwaway, not in the repo):
`$COMMANDCODE_SCRATCHPAD/gs/DumpAll.java` — writes `functions.txt`,
`decompiled.txt`, `insns.txt`, `strings.txt` into
`$COMMANDCODE_SCRATCHPAD/out06001A06/`.

All addresses in this document are **virtual = base + file offset**:
`VA = 0x80100000 + off` for both OMAP images, `VA = 0x02000008 + off` for the
Qualcomm image. So `0x8010F280` is file offset `0x0000F280`.

Labels: **[P]** proven from instruction/decompiled evidence at the cited VA;
**[I]** inference from structure or from a sibling build.

---

## 2. Verified facts — signed-loader structure

| Field | `06001A06_0000000A` | `05001A06_0000000A` | `LDR_01` (`loader_06001A06-00.bin`) |
|---|---|---|---|
| +0x00 `b` | `EA000002` → `base+0x10` | `EA000002` | `EA000002` |
| +0x04 magic | `D7D32D1F` ✅ | `D7D32D1F` ✅ | `D7D32D1F` ✅ |
| +0x08 load addr | `0x80100000` (OMAP) | `0x02000008` (Qualcomm) | `0x80100000` |
| +0x0C | `0x801234FC` — **pointer to a non-code descriptor table** (verify §2.2) | `0x0201E728` (same shape) | `0x801356D8` (same shape) |
| code starts | `base+0x10`: `mov r0,#0xD3; msr cpsr_c,r0; …; bl 0x8010F280` | same | same |
| len-16 | `000000B4` (=180) | `000000B4` | `000000B4` |
| len-12 | `00010001` (1.1) | `00010001` | `00010001` |
| len-8 | `D7C82D1F` ✅ | `D7C82D1F` ✅ | `D7C82D1F` ✅ |
| len-4 | `87B5B04D` = **CRC-32 of `[0,len-4)` — verified match** | `43527C84` — verified match | `FFFFFFFF` (**blanked/placeholder**) |
| signature area `[len-0x90, len-0x10)` | **126/128 bytes non-`FF`** ✅ | 128/128 non-`FF` ✅ | 128/128 non-`FF` ✅ |
| `isSignedLoader()` (`ramloader.pas:587`) passes | yes | yes | yes |

### 2.1 What `isSignedLoader` actually checks (source context)

`bb10mt-main/ramloader.pas:572` `IsValidSignature` is *not* a cryptographic
check: it returns true if **any** of the 0x80 bytes at
`[len-16-0x80, len-17]` is not `0xFF`. `isSignedLoader` (line 587) additionally
requires length ≥ 10240, `D7D32D1F` at +4 and `D7C82D1F` at `len-8`. That is
exactly the "non-0xFF signature area" test the task asked for, and both
Cellebrite images pass it. **The host tool's test does not verify the
signature**; the real check is on-device (bootrom `0xF00A` / execute path). Do
not read "signed" as "verified by `ramloader.pas`".

### 2.2 The `+0xc` field is data, not an entry point [P]

`06001A06` file offset `0x234FC` decodes as
`09 00 01 00 | 80 00 00 00 | 0A 00 01 00 | 0A 00 00 00 | 02 00 00 00 |
00 00 00 20 | 00 00 00 00 | 9D 00 09 03 …` — a table of `u32` descriptors, not
ARM code. `LDR_01` at `0x801356D8` has the identical shape. Execution is
reached from the branch at offset 0, then `bl 0x8010F280` at `base+0x3c`
(router). Treat `+0xc` as "descriptor-table pointer".

### 2.3 Address map of the audited image

| Region | VA range | Notes |
|---|---|---|
| reset stub | `0x80100010`–`0x80100040` | sets CPSR/SP, `bl 0x8010F280` |
| flash/NVRAM/QCFM/MMU library | `0x80100040`–`0x8010F0xx` | 700+ small functions |
| **command router** | `0x8010F280`–`0x8011066C` | one function, 5,116 bytes (Ghidra) |
| RPMB record layer | `0x8010D35C`, `0x8010D454`, `0x8010D470` | `FS_DIRTY`, `BOOTCTR_FCTRY`, record I/O |
| CREAD/cipher | `0x80102084`, `0x80102098`, `0x801022B0`, `0x80102408` | session keyed at `0x80129EC0` |
| region allow-list | `0x8011B774` (test), `0x8011B920` (build), `0x8011B858` (post-check) | table lives in RAM at `0x8020C0E4` |
| QCFM parser | `0x80115250`, `0x80115354`, `0x801153EC` | `mfcq` / `qcfp` / `pfcq` / `rrcq` |
| flash write engine | `0x8010F188` (stage flush), `0x8010F208` (program) | 0x20000-byte staging buffer at `0x8017EDE0` |
| strings | 57 defined, incl. `0x8010D348` "Fail to write RPMB", `0x80115070` "BOOTCTR_FCTRY", `0x80118A1C` "RIM-BootLoader", `0x80118A2C` "RIM-RAMLoader", `0x80114E7C` "Password record not found" | same family as `LDR_01` |

---

## 3. Command framing and the initial `0xB0` handshake [P]

Same outer framing as documented in `66`/`69`: channel-2 frame
`u16 type | u16 total | u8 cmd | u8 seq | u16 packetId | payload`, opcode at
**frame+2**, sequence/mode byte at **frame+3**, argument dwords from **frame+4**.

`FUN_8010F280` has a **pre-loop that discards every command until `0xB0`**:

```
8010F3D0  ldrbeq r0,[r0,#2]
8010F3D4  cmpeq  r0,#0xB0
8010F3D8  bne    0x8010F38C      ; anything else: loop (a 0x15/0x11 busy reply
                                 ;   is only sent for uVar7 == 3 "busy")
8010F3DC  … build reply 0xD0 …   ; 4-byte payload from DAT_80110120, then a
                                 ;   10-byte ASCII "D:<8 hex digits>" frame
```

**Difference from `LDR_01`:** `LDR_01` answers `0xB0` with paged `0xB5` log
blocks until the log is drained and only then sends `0xD0` (`69` §5). This
build sends **one** `0xD0` and immediately enters the main loop. A client that
waits for `0xB5` pages will hang; a client that sends `0xB0` twice will get
`0x15` the second time. Count this as a protocol-level difference, not a bug.

---

## 4. Archive inventory — `ufedblackberrypack.epr/` (82 entries)

Every entry except `BB.bin` is a header-framed signed ramloader with a
**verified CRC-32 trailer**. Grouped by header load address:

| Load base | Count | Family |
|---|---:|---|
| `0x40000000` | 20 | BBOS 9900-class (the Cellebrite-exploit family) |
| `0x10000000` | 14 | BBOS/other |
| `0x18000000` | 8 | BBOS/other |
| `0x80000000` | 7 | BB10 phone-class |
| `0xB8000000` | 7 | BB10 phone-class |
| `0x02000000` / `0x02000008` | 2 | Qualcomm/MSM (`05001A06` here) |
| `0x20000000` | 2 | BB10 phone-class |
| `0x8030000` | 1 | `02000C08` |
| `0x8` | 1 | `17001507` (pre-relocated variant) |
| **`0x80100000`** | **1** | **`06001A06_0000000A` — the only OMAP PlayBook-class entry** |
| n/a (`0xEA000008`) | 4 | `*_00000008` files: different header (`EA000008`, no `D7D32D1F`) |
| raw code | 1 | `BB.bin` |

**`BB.bin` (6,980 B) is the Cellebrite BBOS payload, not a loader.**
`sub sp,sp,#8` prologue, position-independent (`add sb,pc,#0x5000`), strings
`"This boot-loader belongs to Cellebrite"`, `"Invalid Command"`, `"Read
Failed"`, `"EOT"`, `"rply"`, `"ok"`, `"eom"` [P]. This is the payload the
writeup places at `0x40200000` behind the patched loader. It is generic 32-bit
ARM with a self-contained host command set; it contains nothing
PlayBook/OMAP-specific and nothing QNX-specific.

**Nothing else in the pack is PlayBook/OMAP relevant.** `06001A06_0000000A` is
the only image whose load address matches the OMAP4430 PlayBook loader window
(`0x80100000`), and it is the only one with the PlayBook string set
(`RIM-RAMLoader`, `BOOTCTR_FCTRY`, `Fail to write RPMB`).

---

## 5. Dispatcher and opcode map

### 5.1 Structure: one flat router, not four dispatchers

`LDR_01` uses a router (`0x80116E28`) plus four sub-dispatchers
(`0x801032AC` query, `0x80103B90` actions, `0x8011AE30` erase/write,
`0x80102DC4` CREAD) and three directly-handled opcodes (`0xEF`, `0xF7`/`0xF8`,
`0xF9`). **`06001A06` has none of that**: `FUN_8010F280` is a single 5 KB
function containing a binary-search compare chain plus **two jump tables**, and
every handler body is inlined in it.

```
8010F500  ldr  ip,[fp]                 ; frame
8010F504  ldrb r0,[ip,#2]              ; opcode
8010F508  cmp  r0,#0xDE ; beq 0x80110494
8010F510  bgt  0x8010F5F0
8010F514  cmp  r0,#0xD1 ; beq 0x80110218
8010F51C  bgt  0x8010F5B4
8010F520  cmp  r0,#0xB4 ; beq 0x80110290   ; (0xB1/0xB2/0xB3 chain)
…
8010F5B4  sub  r0,r0,#0xD2 ; cmp r0,#0xB
8010F5BC  addlo pc,pc,r0,lsl #2            ; jump table 1: 0xD2..0xDC
8010F5FC  sub  r0,r0,#0xE0 ; cmp r0,#0xC
8010F604  addlo pc,pc,r0,lsl #2            ; jump table 2: 0xE0..0xEB
8010F660  … 0xF7/0xF8 write path
8010F6B8  … (same block, op re-tested to set the 0xF7/0xF8 flag)
8010F71C  … 0xFD staging
8010FC34  … 0xF9 signature trailer
80110660  switchD default → 0x15 / status 4
```

Jump table 1 (`0x8010F5C4`, index = op−0xD2): `0xD2→0x8011020C`,
`0xD3→0x80110200`, `0xD4..0xD7→default`, `0xD8→0x801101A8`,
`0xD9→0x80110238`, `0xDA→0x80110458`, `0xDB→0x801104F0`,
`0xDC→0x80110518`.
Jump table 2 (`0x8010F60C`, index = op−0xE0): `0xE0→0x801101D0`,
`0xE1→0x801101B4`, `0xE2,0xE3→default`, `0xE4→0x8010FD04`,
`0xE5→0x8010FDEC`, `0xE6→0x8010FD64`, `0xE7→0x80110184`,
`0xE8,0xE9,0xEA→default`, `0xEB→0x8010FDA8`.

Everything below `0xB1` reaches the default path (`0x80110660` → response
`0x15`, status 4). Verified independently by an exhaustive scan of all
`CMP <reg>,#imm` encodings in the image: the only opcode-range immediates in
the router are `0xB0,0xB1,0xB2,0xB3,0xB4,0xB7,0xBD,0xC0,0xD1,0xDE,0xEE,0xEF,
0xF3,0xF7,0xF8,0xF9,0xFD` plus the two tables.

### 5.2 Implemented opcode map (this build)

`R` = response command byte. "Ctx" = channel-2, after a `0xB0`.

| op | handler VA (base+off) | R | behaviour | vs `LDR_01` (`66` §2) |
|---:|---|---:|---|---|
| `0xB0` | pre-loop `0x8010F3D4` | `0xD0` | single-shot log/ID reply, then main loop | **differs**: LDR_01 pages `0xB5` |
| `0xB1` | `0x80110260` | `0xC8` | `{0xFFFFFFFF, FUN_801129D0(0), FUN_801129B4()}` — sector size and total-sector count (field meanings [I]) | same |
| `0xB2` | `0x801101F4` | `0xC8` | `FUN_80110D0C` — **zero-fills the 0x3FF0 buffer and returns** (no metrics in this build) | **differs** (LDR_01 returns DSP metrics) |
| `0xB3` | inline `0x8010F544` | `0xB4` | NVRAM `0x2033` action-log read (`arg==0`) **or host-string append** (`arg!=0`), capped 0x200 | same semantics |
| `0xB4` | `0x80110290` | `0xD2` | flash regions, built from the partition table | same |
| `0xB5` | — | — | **not implemented** | LDR_01 has flash info |
| `0xB7` | `0x801105BC` | `0xF3` / `0x15` | 20-byte image hash | same |
| `0xBD` | `0x80110624` | `0xBE` | hardware-override ID | same |
| `0xC0` | inline `0x8010F590` | `6` / `0x14` / `0x15` | COMPLETE → NVRAM `0x2074` write (§7.6) | same |
| `0xC2`,`0xC3`,`0xC4` | — | `0x15` | **not implemented** (no READVERIFY) | LDR_01 has all three |
| `0xD1` | `0x80110218` | `0xC8` | version/SVN byte pair | same |
| `0xD2` | `0x8011020C` | `0xC8` | zero-filled buffer | **differs** (LDR_01: kernel metrics) |
| `0xD3` | `0x80110200` | `0xC8` | metrics (`FUN_80110D30`) | same |
| `0xD4`–`0xD7` | — | `0x15` | **not implemented** | LDR_01: n/a |
| `0xD8` | `0x801101A8` | `0xC8` | OS metrics (`FUN_801109A0`) | same |
| `0xD9` | `0x80110238` | `0xC9` | MCT descriptor | same |
| `0xDA` | `0x80110458` | `0xCA` | NVRAM `0x2027` read; **first payload byte `1` ⇒ delete** | same |
| `0xDB` | `0x801104F0` | `0xCB` | vendor ID | same |
| `0xDC` | `0x80110518` | `0xCC` | rolling 32-bit hash over `frame[8]` consecutive sectors from `frame[4]` | same |
| `0xDD` | — | `0x15` | **not implemented** (no FLASH_DUMP) | LDR_01 also lacks it |
| `0xDE` | `0x80110494` | `0xFA` | BOOT_MODE **read only** (§6) | same policy |
| `0xE0` | `0x801101D0` | `0xC3` | MCT rewrite ("remove installer") | same |
| `0xE1` | `0x801101B4` | `0xC8` | log/crash buffer | same |
| `0xE2`,`0xE3` | — | `0x15` | **not implemented** | same |
| `0xE4` | `0x8010FD04` | `0xE8`/`0xE7` | CREAD session init from host key material | same |
| `0xE5` | `0x8010FDEC` | `0xE9` / `3` / `0x11` | CREAD read (session-ciphered) | same |
| `0xE6` | `0x8010FD64` | `0xEA` | CREAD variant | same |
| `0xE7` | `0x80110184` | `0xD1` | device PIN | same |
| `0xE8`,`0xE9`,`0xEA` | — | `0x15` | **not implemented** (no WIPE_SECURITY, no NVRAM `0x2074` read, no BSN) | LDR_01 has all three |
| `0xEB` | `0x8010FDA8` | `0xEC` | CREAD variant (requires session state 2) | same |
| `0xEC`,`0xED` | — | `0x15` | **not implemented** | LDR_01 has both |
| `0xEE` | `0x8010F7BC` | `0xDE`/`0x15` | ERASE_SECTOR, address-driven (§7.4) | same |
| `0xEF` | `0x80110604` | `0xC7` | reboot | same |
| `0xF3` | `0x8010F874` | `0xF4` | EraseMCT (`0x80111308`) | same |
| `0xF7`,`0xF8` | `0x8010F660` | `0xDF` | QCFM write (§7.3) | same |
| `0xF9` | `0x8010FC34` | `6`/`0x2E` | 0x230-byte signature trailer into RAM state | same |
| `0xFD` | `0x8010F71C` | `0xDF`/`0x11` | stage into the 0x80000 RAM buffer | same |
| other | — | `0x15` | default | — |

### 5.3 Opcodes that differ from `LDR_01` — the answer to task item 2

**Dropped in `06001A06` (present in `LDR_01`, absent here):**

```
0x20  persistent data / magic 0x03C6806C
0x39  0x3A  CREAD block read + variant      <-- the two "undocumented" reads
0xAD  0xAE  0xAF  PMIC / blocklist erase / mode flag
0xB5  flash info
0xBF  DRAM info
0xC2  0xC3  0xC4   status / LED / READVERIFY
0xE8  0xE9  0xEA   WIPE_SECURITY / NVRAM 0x2074 read / BSN
0xEC  0xED         blocked-OS and blocked-radio lists
0xFE               (unused in LDR_01 too)
```

**Behaviourally different:** `0xB0` (single `0xD0` instead of paged `0xB5`),
`0xB2` and `0xD2` (zero-filled buffer instead of device metrics).

**Added relative to `LDR_01`:** nothing. This build is a strict subset in
command surface plus a rearranged implementation.

The practical consequence is the opposite of what the lead hoped for: the
opcodes a Cellebrite extraction workflow would care about (`0x39`/`0x3A`
block read, `0xC4` READVERIFY, `0xDD` FLASH_DUMP) are **absent**, and the
remaining read path is the RSA/AES-session `0xE5`.

---

## 6. `0xDE` BOOT_MODE — read-only, proven

```
80110494  ldr   r0,[ip,#4]        ; frame+4 = action
80110498  cmp   r0,#0
8011049C  bne   0x80110674        ; <-- action != 0: jump to the main-loop head.
                                  ;     NO response frame is produced.
801104A0  add   r0,sp,#0x224      ; resp = {status, mode, count}
801104B4  ldr   r1,[sp,#0x230]    ; "metrics ok" flag
801104B8  cmp   r1,#0
801104BC  movne r0,#1 ; strne r0,[sp,#0x224]     ; status = 1
801104C8  add   r0,r0,#8 ; bl 0x80114FA8         ; mode = read BOOTCTR_FCTRY
801104D4  str   r6,[sp,#0x224]                   ; status = 0
801104D8  … bl 0x80118A40 with cmd 0xFA, len 0xC
```

* **[P] Read-only.** Action 1 and action 2 produce **no response at all** —
  the host sees a USB timeout, identical to `LDR_01` (`72` §3). A a host tool
  that "supports" `--factory`/`--set-count` cannot make this build act.
* **[P] No setter exists to reach.** `BOOTCTR_FCTRY` (`0x80115070`) has exactly
  one reference in the image, from `FUN_80114FA8` (the reader). `LDR_01` at
  least carries a dead setter (`0x8011E250`) and decrement (`0x8011E1EC`); this
  build carries neither.
* **[P] Reader semantics differ from `LDR_01` in a way that matters.**
  `FUN_80114FA8` reads the record via `FUN_8010D470("BOOTCTR_FCTRY", &val, 4,
  &len)`:

  | record state | `FUN_80114FA8` | mode reported | count reported |
  |---|---|---:|---:|
  | read returns 3 (record name not found) | `0` | 0 (product) | 0 |
  | value `1..0x32` | `1` | 1 (factory) | value |
  | value `0` or `>0x32`, **including `0xFFFFFFFF`** | `2` | **2 (expired)** | 0 |

  So this build does **not** have `LDR_01`'s `0xFFFFFFFF` "record absent"
  mask (`FUN_8011e074`, `72` §3.1). On the expired 32 GB unit `06001A06` would
  report `status=0, mode=2, count=0` where `LDR_01` reports
  `status=0, mode=product, count=0`. **This makes `06001A06` a strictly better
  diagnostic for the expired unit** — it agrees with the bootloader reader
  (`0x800090CE`) instead of masking the sentinel. It does **not** let you fix
  the record.

---

## 7. Per-handler audit

Definition of "reachable": the router dispatches the opcode to a non-default
handler for a correctly framed channel-2 command sent after the `0xB0`
handshake. All handlers below are reachable.

### 7.1 `0xE5` / `0xE4` / `0xE6` / `0xEB` — CREAD family

| item | finding |
|---|---|
| `0xE4` `0x8010FD04` = base+`0x00FD04` | `FUN_80102084(out, &len, frame[4])`: clears a 0xAF0-byte session at `0x80129EC0`, stores `frame[4]` as the expected total, reads **host** blobs of `0x20`, `0x10`, `0x20`, `0x100` bytes, runs `FUN_8011BD44` + `FUN_8011CD0C` (cipher/curve setup) and finally sets **session byte `+0xAEC = 1`**. Response `0xE8` on success, `0xE7` on failure. |
| `0xE5` `0x8010FDEC` = base+`0x00FDEC` | address gate `FUN_8011B774(frame[4], 0, frame[8], 0)`; length must be `0x3FE8` or `0x4000` after `|3` rounding, else `0x11`; then per-sector `FUN_8011243C(frame[4], idx, staging)` and `FUN_80102098(staging, key, len, &outlen)`. Response `0xE9`. |
| `0xE6` `0x8010FD64` | `FUN_801022B0(...)`; **returns 1 immediately unless `+0xAEC == 1`**, and requires `transferred == expected`, else **clears `+0xAEC` and returns 4**. |
| `0xEB` `0x8010FDA8` | `FUN_80102408(...)`; **requires `+0xAEC == 2`** (only `0xE6` sets 2) and `len >= 0x100`, else returns 1. |
| `0x39` / `0x3A` | **not implemented** — no `CMP #0x39`/`#0x3A` in the image; both take the default path. |

**Verdict:** *not* an arbitrary read, and **strictly less exposed than
`LDR_01`** (which at least parses `0x39`/`0x3A`). The gate is a state machine,
not a secret — a host that supplies key material can complete `0xE4` — but the
`0xE5` output is produced by `FUN_80102098`, which returns *ciphertext*
(`FUN_8011CD38` + `FUN_80101D40` with the session key), so it is not a
plaintext dump primitive unless the host's own key material is used to decrypt
it. `0xE5` is also range-checked by `FUN_8011B774`, so even a completed session
reads only allow-listed partitions. Cheapest way to think about it: **`0xE5` is
a host-keyed read of the allow-listed partitions, not a memory/disk read
oracle.** [P for the gates; I for "the host can always supply valid key
material" — `FUN_8011BD44`/`FUN_8011CD0C` were not fully resolved.]

### 7.2 `0x39`/`0x3A` — see 7.1. **Absent.** This is the single biggest
improvement over `LDR_01` and the reason the "undocumented read" lead from `66`
§3.1 does not transfer to this build.

### 7.3 `0xF7` / `0xF8` — the write path

Flow (all at `0x8010F660`, i.e. base+`0x00F660`):

```
len = frame[0] - 0x10                                  ; payload bytes
if (no session yet) {
    n = FUN_80115250(frame+0xc, len)                   ; parse QCFM header
    if (n <= 0) -> error
    memcpy(staging 0x8016BAFC, frame+0xc, n)           ; keep the header
    flag = (op != 0xF7)                                ; 0xF7 -> 1, 0xF8 -> 0
    FUN_80103648(3, &write_ctx, 0)                     ; begin write session
    if (rc != 0) -> error
    if (!FUN_80115430(&dirty))   -> error              ; READ  FS_DIRTY (RPMB)
    dirty |= 1
    if (!FUN_80115480(dirty))    -> error              ; WRITE FS_DIRTY (RPMB)
    total = *(u32*)(staging + 0x10)
} …append chunk…
for each partition entry i of the QCFM partition table:
    FUN_80115354(staging, i, &desc)                    ; desc.start, desc.count
if (partition 0x34 exists) {
    base = (op == 0xF8) ? FUN_801106F8(1,&x) - (p34.end - p34.start) - 1
                        : p34.start
    if (sector_size == desc.block_size) {
        dest = desc.start + base                       ; <-- HOST-CONTROLLED LBA
        n    = desc.count * sector_size
        rc   = FUN_8011B774(dest, 0, n, 0, seq, &st)   ; <-- THE ONLY ADDRESS GATE
        if (rc != 0) {
            flush_dcache(frame+0xc, len>>2)
            reply(0xDF, seq, 0)
            while ((rc = FUN_8010F208(dest)) != -1) {  ; program sector
                if (rc < 1) break
                dest += 1
            }
        }
    }
}
```

Findings:

* **[P] The destination LBA is not fixed.** It is
  `QCFM 'rrcq' run-record.offset + base`, i.e. fully host-controlled, restricted
  only by the next bullet. This is the "address-driven write" the lead was
  looking for.
* **[P] The gate is an allow-list, not a fixed base.** `FUN_8011B774` (see
  §7.7) accepts the range only if
  `region.start <= start` **and** `start*ss + len - 1 <= (region.end+1)*ss`
  for some region in a 64-bit-checked table. That table is filled by
  `FUN_8011B920` from **MCT partition types `0x1C, 0x1B, 0x1F, 0x24, 0x34`**
  plus the `0x19`/`0x1A` pair, and the MCT itself must pass a CRC check
  (`FUN_801003FC(9,0)` + `FUN_80100FE4` vs `*(u32*)(mct_part+4)`) or the table
  stays empty and **every** write/erase is refused.
* **[P] The write is RPMB-gated.** `FUN_80115430`/`FUN_80115480` are the
  `FS_DIRTY` record read/modify/write pair; `FUN_80115480` calls
  `FUN_8010D454` (delete) then `FUN_8010D35C` (write), and `FUN_8010D35C` is
  the function that logs `"Fail to write RPMB"`. Any failure aborts before a
  single sector is programmed.
* **[P] Structural preconditions:** a valid QCFM V2 container
  (`'mfcq'` header magic `0x7163666D`, version ≤ 1, file count at `+0xc`,
  `0x30`-byte file entries from `+0x20` with magic `'qcfp'`=`0x71636670` or
  `'pfcq'`=`0x70666371`, per-entry version `< 2`, and `'rrcq'`
  `{magic,len,offset,count}` run records). `bb10mt-main/qcfm.pas` can build
  this format (`mhf1.magic := 'mfcq'`, `cf2.magic := 'pfcq'`,
  `cf1.magic := 'qcfp'`, `rr2.magic := 'rrcq'`, `cf2.version <> $20000`).
* **[I] `0xF8` is the "second region" variant**: it takes its base from
  partition type `0x35` minus the `0x34` extent. Both land in the same
  allow-list check.

**Verdict:** *reachable, address-controlled, bounded.* **Not** arbitrary eMMC —
it cannot leave the allow-listed partitions, and it cannot run if RPMB will not
accept `FS_DIRTY`.

### 7.4 `0xEE` — ERASE_SECTOR

```
8010F7BC  bl 0x80112B00                  ; flush pending write buffer, rc==0 -> status 0x13
8010F7DC  ldmib r2,{r0,ip}               ; r0 = frame[4] (LBA), ip = frame[8] (len)
8010F7F0  bl 0x8011B774(addr,0,len,…)    ; ALLOW-LIST GATE (same as 0xF7)
8010F808  ldr r1,[r0,#0x20] ; cmp r1,#3  ; frame+0x20 == 3  =>  "success" reply
8010F81C  bl 0x801128D0(frame[4])        ; secondary range check + driver op
8010F860  reply 0xDE (4 bytes, value 1)  /  else 0x15 with status 0x12
```

* **[P] Reachable; address and length come straight from `frame+4` and
  `frame+8`.**
* **[P] Two gates:** `FUN_8011B774` (allow-list, as above) and
  `FUN_801128D0` (range check `*0x801285D0 <= arg < *0x801285C8` then an
  indirect driver call through the op table at `*0x801285D8 + 0xC`).
* **[I, residual unknown]** The actual sector-erase is performed behind an
  **unrecovered indirect call** in the flash driver op table (`0x80112E08`
  shows the same pattern with `+0x48`). Ghidra could not recover the jump
  target; the op table lives in RAM. I will not claim it is or is not a raw
  erase — but there is no *additional* host-visible parameter, so whatever it
  does is confined to the validated range.
* **[P] `frame+0x20 == 3` short-circuits the `FUN_801128D0` call** and yields a
  success reply. Whether that also skips the erase is unresolved (see above);
  it does **not** skip the allow-list gate, which runs first.

**Verdict:** *reachable, address-controlled erase inside the allow-list.* Not
arbitrary. Relative to `LDR_01` (`FUN_801001C8` erase, `66` §2) it is the same
shape with an explicit allow-list added.

### 7.5 `0xC4` READVERIFY — **not implemented** in this build

No `CMP #0xC4` exists anywhere in the image; `0xC4 < 0xB1` plus the chain
structure routes it to the default handler (`0x80110660` → `0x15`, status 4).
Verified against `LDR_01`, which does have it at `0x80103C08`.

**Verdict:** absent ⇒ no surface. (This is a **removal** of an LDR_01
surface, not an addition.)

### 7.6 `0xE0` installer/OS-region, `0xC0` COMPLETE, `0xDD` FLASH_DUMP

| op | finding | verdict |
|---:|---|---|
| `0xE0` `0x801101D0` | `FUN_801121F0(0)`: partition lookups `0x1F/0x1B/0x1C/0x24`, metric gate, then `FUN_80111F1C(...)` which **rebuilds the MCT in a 0x200-byte buffer and writes it back** (`FUN_801006F4`) and deletes NVRAM `0x206C`/`0x2028` on some paths. **No host-controlled address or data.** Response `0xC3`. | Reachable, flash-writing, **not** arbitrary — but destructive to the partition table. Do not send. |
| `0xC0` `0x8010F590` | requires `frame[3] & 0x40`; then `FUN_80112B00` (flush) / `FUN_80112B9C` (verify); the second and later `0xC0` require the staged-image cursor `== 0x20000`, build `{u32 2, u32 region (1 if the write session was opened with 0xF7, 0 if 0xF8), u16 0}` and call `FUN_80105420(0x2074, buf, 0x18)` (NVRAM write). Response `6`, or `0x15`/`0x14`. | Reachable, **writes one NVRAM record (`0x2074`) only**. No eMMC address is involved. Same shape as `LDR_01` (`66` §3.5) but the byte written is the region selector, not a payload byte. |
| `0xDD` — **absent** | jump table 1 ends at `0xDC`; `0xDD−0xD2 = 0xB` is not `< 0xB`, so it falls through to the default. | **FLASH_DUMP does not exist here.** Matches `66` §2.1 (it does not exist in `LDR_01` either). |

### 7.7 The allow-list gate `FUN_8011B774` — decoded

```
8011B794  ldr  r2,[sb,r4,lsl#3]      ; region[i].start
8011B798  subs r0,r7,r2 ; sbcs r1,r8,#0
8011B7A0  blo  next                  ; require start >= region.start
8011B7A4  ldr  r0,[+4]               ; region[i].end
8011B7AC  add  r2,r0,#1
8011B7B4  ldr  r1,[sec_size]         ; *0x801285B4
8011B7B8  umull r5,r6, r2, r1        ; 64-bit (end+1)*ss
8011B7BC  umull r0,r2, r1, r7        ; 64-bit ss*start
8011B7C0  mla  r1, r1, r8, r2
8011B7C4  ldrd r2,r3,[sp,#8]         ; 64-bit length (param3,param4)
8011B7C8  adds r0,r0,r2 ; adc r1,r1,r3
8011B7D0  mvn  r3,#0
8011B7D4  adds r0,r0,r3 ; adc r1,r1,r3 ; 64-bit (ss*start + len - 1)
8011B7DC  subs r2,r5,r0 ; sbcs r3,r6,r1
8011B7E4  movhs r0,#1 ; bhs return 1   ; success iff bound >= top
8011B800  strb r0,[fp] (r0=3) ; bl 0x80118A40(0xCE, seq, 1, &status)
```

So: **`∃ region : region.start ≤ start  ∧  start*ss + len − 1 < (region.end+1)*ss`**,
evaluated in 64-bit. I re-derived this from the instruction stream rather than
trusting the decompiler, because the decompiled form is unreadable.

* [P] The arithmetic is 64-bit (`umull`/`adc`), so the obvious
  `start*ss + len` overflow does not apply for plausible `ss` (512–4096) and
  32-bit `start`.
* [I] `len == 0` passes for any `start ≥ region.start` (nothing is written), so
  it is not a write primitive.
* [P] The table is **runtime-built** (`0x8020C0E4`, count at `0x80128E98+4`),
  beyond the loaded image, by `FUN_8011B920` during router init. I cannot read
  its *contents* statically — an honest residual unknown (§12).

### 7.8 Handlers that write NVRAM (not eMMC)

| op | path | content control |
|---:|---|---|
| `0xB3` with `frame[4] != 0` | `FUN_80111334` → `FUN_801053E0(0x2033,…)` read, append `strnlen(frame+8)+1` bytes, `FUN_80105420(0x2033,…)` write, capped at 0x200 | **host string**, bounded to one log record |
| `0xDA` with `frame[4] == 1` | `FUN_80114DB0` → delete NVRAM `0x2027` | n/a |
| `0xC0` | NVRAM `0x2074` ← `{2,0,byte}` | one byte |
| `0xE0` | MCT rewrite + `0x206C`/`0x2028` deletes | none |

None of these is an eMMC address-driven operation.

---

## 8. Cellebrite patch signatures and the BBOS analogue

### 8.1 Patch search (task item 5)

| pattern | `06001A06` | `05001A06` | `LDR_01` |
|---|---|---|---|
| `DB9F07EB` (documented patch) | **0 occurrences** | **0** | **0** |
| `6A0000EA` | 2: file `0x55F8`, `0x1F2E8` | 2: `0x12E54`, `0x1BFDC` | 3: `0x3D98`, `0x25CA4`, `0x311CC` |

**[P] The two `06001A06` `6A0000EA` occurrences are meaningless as patch
sites:**

* `0x801055F8` (`base+0x55F8`) decodes as `EA00006A` = `b #0x801057A8`, an
  ordinary forward branch inside a flash-read helper (neighbours:
  `ldr r0,[r7]; ldr r1,[pc]; cmp r0,r1; blo …; cmp sb,#0; movne r0,#0`).
  It is not a dispatcher `case`.
* `0x8011F2E8` (`base+0x1F2E8`) is also a real instruction, not data: it is the
  tail `b 0x8011F498` of `FUN_8011F2DC` (16 bytes), the ARM `memset` helper
  (`and r3,r2,#0xff; orr r2,r3,r3,lsl #8; orr r2,r2,r2,lsl #16; b 0x8011F498`).
  Confirmed against the Ghidra function list (`0x8011F2DC size=16`) and the
  instruction listing at `0x8011F2E8`.
* The BBOS patch works because `0xEA00006A` was the *specific* `b` at the
  `case 0x8` site of the BBOS dispatcher and `0xEB079FDB` (`DB9F07EB` LE) is a
  `bl` with the same offset field. Turning a `b` into a `bl` is a one-byte
  change. **Nothing in `06001A06` sits at a comparable position:** its `0xF7`/
  `0xF8` case has no `6A0000EA` anywhere near it, and its router's `case 0x8`
  equivalent does not exist (opcodes below `0xB1` are unimplemented).

**Verdict: `6A0000EA` in `06001A06` is a coincidence, not a patchable
dispatcher branch. There is no ready-made "binary patch" for this build.**

### 8.2 BB10 analogue of the three-address `WRITE_RAM_SETUP` weakness

The BBOS exploit needs three things, and BB10/PlayBook supplies none of them
statically:

1. **Loose `WRITE_RAM_SETUP` address checking.** `messages.txt` (Oleksandr,
   the person who documented the leak): *"ram loader pushed by 3 stages:
   WRITE_RAM_SETUP: addr, size / WRITE_RAM: addr, size, data / WRITE_RAM_VERIFY
   — bb10 strictly check addr params but bbos is not"*. `69` §4 records our own
   live flow on this hardware: `0xF009` setup, 916 × `0xF004` 260-byte chunks,
   `0xF00A` verify, `0xF005` execute — one setup/one target, no second setup
   accepted after a transfer is pending. So the "three overlapping setups at
   `addr`, `addr+Δ`, `addr+2Δ`" trick has no documented BB10 equivalent.
   **This is the primary blocker and it is a bootrom property, not a loader
   property.** The bootrom handlers are partly in the hidden page
   (`0x40027000`, `40`/`62`), so it cannot be closed from the NS dump.
2. **No signature re-check after the write.** On BB10 the ramloader is
   signature-checked on the `0xF00A`/execute path (`D7D32D1F`/`D7C82D1F`
   cookie + the 0xB4-block signature area we verified in §2). Nothing in
   `069`/`070` shows a way to make the bootrom accept a modified image.
3. **A dispatcher `case` that can be turned into a jump into a payload.** Even
   *inside* the loader there is no such branch: the router is a compare chain +
   two computed jump tables, and the only indirect transfers are the flash
   driver op-table calls (`FUN_80112E08`, `FUN_80112C8C`), whose tables are in
   RAM and are populated by the loader itself, not by a host frame.

**Secondary but real BB10-specific observation:** `0xF9` SIGNATURE_TRAILER
copies a `0x230`-byte host blob into the loader's state
(`FUN_8011F2EC(&local_290, frame + n + 6, 0x230)`), checks
`local_11C == DAT_80110138` and `local_1B0 == 0x88`, and on success appends
`0xBC` bytes into `DAT_8011013C`. It is a *staging* step for the QCFM
signature, **not a verification step** — the loader does not appear to validate
what it receives [P for the copy, I for "no verification of the blob"]. That is
the closest structural analogue to the BBOS weakness: *the loader trusts a
host-supplied signature trailer for its own bookkeeping.* It does not, by
itself, let anything execute.

---

## 9. Does `06001A06` give arbitrary eMMC read/write? — the verdict

### 9.1 No, and the blockers are concrete

| primitive | available? | binding constraint |
|---|---|---|
| Plaintext eMMC read of an arbitrary sector | **No** | `0x39`/`0x3A` absent; `0xE5` requires the `0xE4` session (`+0xAEC == 1`), emits cipher output, and is range-gated by `FUN_8011B774` |
| 32-bit hash oracle over a sector range | Yes (`0xDC`) | range-gated by `FUN_8011B774`; returns one CRC-32-like value, not bytes |
| eMMC **write** at a host-chosen LBA | **Yes, bounded** | must lie inside an MCT allow-listed partition (`0x19`,`0x1A`,`0x1B`,`0x1C`,`0x1F`,`0x24`,`0x34`); must be wrapped in a valid QCFM V2 container; **must be preceded by a successful authenticated `FS_DIRTY` RPMB write** |
| eMMC **erase** at a host-chosen LBA | **Yes, bounded** | same allow-list + secondary `FUN_801128D0` range check |
| RPMB record write (`BOOTCTR_FCTRY`, `0x2801`, …) | **No** | the loader's only RPMB writes are the internal `FS_DIRTY` flag; `BOOTCTR_FCTRY` has exactly one reference (the reader) |
| NVRAM write at a chosen record | **No** | only `0x2033` (log append), `0x2074` (COMPLETE, 3 fixed bytes), `0x206C`/`0x2028` (delete via `0xE0`) |
| Arbitrary RAM write / code execution | **No** | no memory-safety primitive identified in the audited handlers; all buffers are fixed and length-checked (§7.1, §7.3) |

### 9.2 The exact blocker for the two bricked 32 GB units

The expired unit's fault is **RPMB** `BOOTCTR_FCTRY = 0xFFFFFFFF`, which the
bootloader reads as selector `2` and nukes (`71` §7b, `72` §3.1, handoff §3).
To repair it you need to write that *RPMB record*. `06001A06` cannot: the only
RPMB record it writes is `FS_DIRTY`, and the only BOOTCTR reference in the
image is a read.

Worse, the RPMB dependency runs the other way too: **a damaged RPMB also blocks
the eMMC-side repair.** `FUN_80115480` writes `FS_DIRTY` through
`FUN_8010D35C` (the "Fail to write RPMB" path) and the `0xF7`/`0xF8` handler
abandons the write if it fails. So on a unit whose RPMB MAC/counter state is
broken, the loader **cannot even perform the eMMC write** it is otherwise
capable of.

**Therefore:**
* For the **expired unit** (`BOOTCTR_FCTRY = 0xFFFFFFFF`), `06001A06` provides
  a better *diagnostic* (`mode=2` instead of masked `mode=0`) and **no repair**.
* For a unit whose **eMMC-side** data is corrupt but whose RPMB still works,
  `06001A06` gives a usable host-addressed write **inside the allow-listed
  partitions only**. Whether NVRAM lives in one of those partitions is the one
  undecided fact (§12, unknown U1). If it does not, the write primitive is
  useless for NVRAM repair.
* Remaining routes for the RPMB state are unchanged from `72` §6: hardware
  eMMC/RPMB access, a bootrom-level weakness, or parking the units. This audit
  removes "a memory-safety bug in the signed PlayBook loader" from the list.

### 9.3 Conditional recipe (documented, **not** to be executed without approval)

If a future write test is approved on a unit whose RPMB is healthy, the
sequence would be:

1. Bootrom path (`bbusb`/`bb10mt`, `69` §4): `PING`, `GETVAR(2)`, `SETMODE(1)`,
   `PASSWORD_INFO`/`SEND_PASSWORD` (`0000`), `0xF009{addr=0x80100000,
   size=159968}`, `0xF004` × 615 in 260-byte chunks, `0xF00A` verify, `0xF005`
   execute, then the extra read that lets the bootrom jump.
2. After re-enumeration as `0fca:8001`: `SETMODE(2)`, `PASSWORD_INFO`,
   then **one** `0xB0` and expect `0xD0` (§3).
3. `0xDC` over the target range to record the pre-write hash.
4. `0xF7` with a QCFM V2 container whose `'pfcq'`/`'rrcq'` run record has
   `offset = target_lba` and `count = n_sectors`, `block_size = sector_size`;
   the loader will read/modify/write `FS_DIRTY` in RPMB, then program.
5. `0xDC` again to confirm; on mismatch, repeat step 4 (the loader does not
   verify program success itself).

**Preconditions, all mandatory:** (a) target partition ∈ allow-list;
(b) RPMB accepts the `FS_DIRTY` write; (c) MCT CRC valid (else the allow-list
is empty); (d) valid QCFM container; (e) a full off-device backup and a
verified restore path.

I do **not** recommend running this on either 32 GB unit as the first test.
They are recovery targets, not test beds (handoff §7).

---

## 10. Read-only test plan — spare working 64 GB unit

Goal: confirm the Cellebrite build runs on PlayBook hardware and behaves as
predicted, **without a single write**. Nothing below issues a flash, NVRAM,
RPMB or erase command.

### Phase R0 — preconditions

1. Use the **spare** 64 GB unit only. The connected 64 GB reference stays
   untouched. Take a note of the current loader hash before starting.
2. Fresh power-cycle; have the client already polling (the bootrom window is
   short, `69` §1).
3. Stage the loader under a private directory so the shipped loader is not
   overwritten on disk:

   ```
   mkdir -p ~/rea-work/bootdumps-2026-09-11/analysis/loader-test-2026-10/cb-loaders
   cp cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A \
      ~/rea-work/bootdumps-2026-09-11/analysis/loader-test-2026-10/cb-loaders/loader_06001A06-00.bin
   sha256sum .../cb-loaders/loader_06001A06-00.bin    # must be 03ffaf26…c6dc
   ```

   `bbusb.py` selects by the `loader_XXXXXXXX-*.bin` pattern (`bbusb.py:576`)
   and takes the upload address from header `+8`, so the rename is all that is
   needed; `--addr 0x80100000` is then redundant but harmless.

### Phase R1 — upload and identify (no flash commands)

```
cd ~/rea-work/bbusb
python3 bbusb.py --from-bootrom --loaders <path>/cb-loaders \
                 --password 0000 --debug --boot-mode
```

Success criteria: bootrom ACKs every chunk, `0xF00A` verifies, the unit
re-enumerates as `0fca:8001`, `SETMODE(2)` returns `0x08`, and the single
`0xB0` gets a `0xD0` (not `0xB5` pages — that difference is the fingerprint of
this build, §3).

### Phase R2 — read-only command sweep

Send, in this order, with the expected response. Anything unexpected ⇒ stop.

| order | op | payload | expected | why it is safe |
|---:|---|---|---|---|
| 1 | `0xB4` | none | `0xD2`, 0xBC–0x3FF0 | reads the partition table |
| 2 | `0xB1` | none | `0xC8` | `{0xFFFFFFFF, sector_size, total_sectors}` |
| 3 | `0xD9` | none | `0xC9` | MCT descriptor (read) |
| 4 | `0xDE` | `{0,0}` | `0xFA` | BOOT_MODE read; expect `mode=0,count=0` on a healthy unit |
| 5 | `0xE7` | none | `0xD1` | PIN `0x50175127` |
| 6 | `0xD1` | none | `0xC8` | version bytes |
| 7 | `0xD3`/`0xD8` | none | `0xC8` | metrics |
| 8 | `0xB7` | none | `0xF3` or `0x15` | 20-byte hash |
| 9 | `0xBD` | none | `0xBE` | hardware override ID |
| 10 | `0xE1` | none | `0xC8` | log buffer |
| 11 | `0xDC` | `{lba, n}` for a *known-read-only* small range inside an allow-listed partition | `0xCC` | one CRC-32; no write |
| 12 | `0xB3` | `{0,0}` (arg **must be 0** ⇒ read) | `0xB4` | log read |

Record every raw response. Also capture the `0xDE` result for comparison with
`LDR_01`'s `product/0` — the two builds should agree on a healthy unit and
disagree on an expired one (§6).

### Phase R3 — forbidden list (do not send these, ever, in this plan)

```
0xC0   NVRAM 0x2074 write        (requires the write cursor at 0x20000)
0xE0   MCT rewrite
0xB3   with frame[4] != 0        (NVRAM 0x2033 append)
0xDA   with frame[4] == 1        (NVRAM 0x2027 delete)
0xEE   ERASE_SECTOR
0xF3   EraseMCT
0xF7 0xF8 0xFD                   flash write / stage
0xE4 0xE5 0xE6 0xEB              CREAD session (stateful; leave for the write phase)
0xEF   reboot
```

`0xE8`, `0xE9`, `0xEA`, `0x39`, `0x3A`, `0xC4`, `0xDD` are absent in this build
and will answer `0x15`; a client that "expects" them will mis-report. That
itself is a useful test result: **if `0xE9` answers `0xFB` you are not running
`06001A06`.**

### Phase R4 — exit

`0xEF` (reboot) is the documented exit; a power-cycle is equivalent and
preferable for a read-only session. Confirm afterwards that the unit still
boots and that `0xDE` still reads `product`.

---

## 11. Later write-test plan — **gated on main-session approval**

Do **not** start any of this until: (a) the main session approves in writing,
(b) Phase R0–R4 has passed on the spare unit, (c) a full off-device image of
the spare unit's MCT + NVRAM + all allow-listed partitions exists and has been
read back to verify.

### Phase W0 — capture and baseline

* `0xB4` → full partition map; save. Identify which allow-listed partition
  (`0x1F`, `0x24`, `0x34`, or the `0x1B`/`0x1C` OS regions) can absorb a
  sacrificial 1-sector test.
* `0xDC` hash of the target sector and of its neighbourhood.
* Confirm RPMB health **before** relying on it: `0xDE` reads `status=0`; if the
  unit has any RPMB write error in its log (`0xE1`), stop.

### Phase W1 — single-sector write, reversibility first

1. `0xF7` with a minimal QCFM V2 container targeting **one sector** in the
   sacrificial partition, containing the sector's *original* content
   (a no-op write). This exercises the whole path — QCFM parse,
   `FUN_8011B774`, `FS_DIRTY` RPMB write, program, `0xDF` — without changing
   any data.
2. `0xDC` before/after must be identical. If they are not, **stop**.
3. Only then: a second no-op write to a *different* sector, then a real
   one-sector change with a documented restore value.

### Phase W2 — negative tests (prove the gate exists)

Attempt `0xEE` and `0xF7` with an LBA **outside** every allow-listed partition.
Expected: `0xCE` with status 3, followed by `0x15`, and **no** change to the
`0xDC` hash of that area. A negative result here is as valuable as the
positive: it proves the gate is doing the work the static analysis says it is.

### Phase W3 — stop conditions

Any of: `"Fail to write RPMB"` in the `0xE1` log, a `0xDF` without a matching
hash change, a hash change without a `0xDF`, a `0x15` after a `0xDE` success,
or any unit that stops answering. Power-cycle and re-read; do not retry a write
blindly.

---

## 12. Residual unknowns

| id | question | why it matters | how to close |
|---|---|---|---|
| **U1** | Which MCT partition holds the NVRAM store, and is its type in `{0x19,0x1A,0x1B,0x1C,0x1F,0x24,0x34}`? | Decides whether the `0xF7`/`0xF8` primitive can reach NVRAM at all (§9.2) | read `0xB4`/`0xD9` on the spare unit and map type IDs to extents; cross-check with `analysis/deckard-2.1.0.1917/platform/osregion.py` |
| **U2** | Contents of the allow-list table at `0x8020C0E4` (built at runtime) | The gate's actual extents | live `0xB4`/`0xD9` dump, or a Ghidra memory-map import with a device dump |
| **U3** | Whether the `0xEE` erase is performed at all, and where | `FUN_801128D0` → `FUN_80113038` → indirect call through `*0x801285D8 + 0xC`; Ghidra could not recover the jump table | dynamic observation on the spare unit, or resolve the driver op table |
| **U4** | Whether `0xE4` requires RSA-valid host key material (vs. accepting any 0x100-byte blob) | Would tell us if `0xE5` is reachable without a device secret | decompile `FUN_8011BD44` / `FUN_8011CD0C`; `66` §3.1 left the same question open for `LDR_01` |
| **U5** | Exact QCFM checksum fields the loader validates (`header_cksum`, `data_crc`, per-run-record sum from `FUN_801153EC`) | A write test cannot be built without a container the loader accepts | build one with `qcfm.pas`, byte-compare against a known-good CFP-produced container if one exists |
| **U6** | Whether `(frame[3] & 0x40)` on `0xC0` means "COMPLETE for the OS region" vs. a mode flag | It gates the only NVRAM `0x2074` write | `66` §3.5 / `83` context; low priority |
| **U7** | Does `0xB2`/`0xD2` returning zeros mean the metrics blocks were compiled out, or that they read an uninitialised descriptor? | Cosmetic for this audit; would change the "what Cellebrite can extract" story | compare with `LDR_01`'s `FUN_80117FBC`/`FUN_80117A4C` |

---

## 13. Safety notes

* **Nothing in this document was executed on hardware.** The only side effects
  were reads of files under `/home/psyden/rea-work` and writes inside
  `$COMMANDCODE_SCRATCHPAD`.
* The loader's write path **touches RPMB** (`FS_DIRTY`) before it touches
  eMMC. Any test that reaches `0xF7`/`0xF8` is an RPMB write test as well; plan
  accordingly.
* `0xE0` and `0xF3` rewrite/erase the **partition table**. A mistake there is
  not recoverable by re-flashing a partition; it needs a full MCT restore.
* `0xB3` with a non-zero first dword and `0xDA` with first byte `1` write and
  delete NVRAM records respectively. Both look innocuous in a client.
* `0xEF` reboots. In loader mode that drops you out of the bootrom window; the
  next attempt needs a power-cycle (`69` §1).
* The 32 GB units remain **recovery targets, not test beds** (handoff §7). This
  audit did not produce a repair for them and does not change their status.
* The connected 64 GB reference stays read-only throughout.

---

## 14. Reproduction commands

All commands are read-only and were run from `/home/psyden/rea-work`.

### 14.1 Hash and header verification (§1, §2)

```
cd /home/psyden/rea-work
sha256sum \
  cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A \
  cellebrite-blackberry/ufedblackberrypack.epr/05001A06_0000000A \
  bbusb/loaders/loader_06001A06-00.bin

python3 - <<'EOF'
import zlib, struct
for p in ["cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A",
          "cellebrite-blackberry/ufedblackberrypack.epr/05001A06_0000000A",
          "bbusb/loaders/loader_06001A06-00.bin"]:
    d = open(p, 'rb').read(); L = len(d)
    print(p, L,
          "br=%08x magic=%08x load=%08x ptr=%08x" % struct.unpack_from('<4I', d, 0),
          "tail8=%08x" % struct.unpack_from('<I', d, L-8)[0],
          "crc=%08x calc=%08x" % (struct.unpack_from('<I', d, L-4)[0],
                                  zlib.crc32(d[:-4]) & 0xffffffff),
          "sigarea_nonff=%d/128" % sum(1 for b in d[L-0x90:L-0x10] if b != 0xFF))
EOF
```

### 14.2 Opcode-set scan (independent of Ghidra)

```
python3 - <<'EOF'
import struct
for p, base in [("cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A", 0x80100000),
                ("bbusb/loaders/loader_06001A06-00.bin", 0x80100000)]:
    d = open(p, 'rb').read(); s = {}
    for off in range(0, len(d)-4, 4):
        w = struct.unpack_from('<I', d, off)[0]
        if (w & 0x0FFF0000) == 0x03500000:          # CMP <Rn>,#imm, any cond
            s.setdefault(w & 0xFFF, []).append(base + off)
    print(p)
    print("  ", " ".join("%02X" % i for i in sorted(s) if 0xB0 <= i <= 0xFF))
EOF
```

### 14.3 Ghidra headless (router + all decompilations)

```
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk
cd $COMMANDCODE_SCRATCHPAD
/home/psyden/tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless \
  $PWD/ghproj pb88 \
  -import /home/psyden/rea-work/cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A \
  -processor ARM:LE:32:v7 -loader BinaryLoader -loader-baseAddr 0x80100000 \
  -scriptPath $PWD/gs -postScript DumpAll.java $PWD/out06001A06 \
  -deleteProject -analysisTimeoutPerFile 1800
# -> out06001A06/{functions,decompiled,insns,strings}.txt  (770 functions)
grep -n "8010f280" out06001A06/functions.txt        # the single router
```

### 14.4 Targeted disassembly (Capstone)

```
python3 - <<'EOF'
from capstone import *
md = Cs(CS_ARCH_ARM, CS_MODE_ARM | CS_MODE_LITTLE_ENDIAN)
d = open("/home/psyden/rea-work/cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A", 'rb').read()
B = 0x80100000
for a0, n, label in [(0x8010F4F8, 0x2F0, "router compare chain + jump tables"),
                     (0x8010F7BC, 0xC0,  "0xEE ERASE_SECTOR"),
                     (0x80110494, 0x50,  "0xDE BOOT_MODE"),
                     (0x8011B774, 0xB0,  "FUN_8011b774 allow-list gate"),
                     (0x8010FB00, 0x70,  "0xF7/F8 dest + gate call"),
                     (0x80115430, 0x90,  "FS_DIRTY read/write")]:
    print("==", label)
    for i in md.disasm(d[a0-B:a0-B+n], a0):
        print("%08x  %s\t%s" % (i.address, i.bytes.hex(), i.mnemonic + " " + i.op_str))
EOF
```

### 14.5 String cross-references

```
grep -n "BOOTCTR_FCTRY" $COMMANDCODE_SCRATCHPAD/out06001A06/strings.txt
# 80115070 80114fc4 | BOOTCTR_FCTRY      <- exactly one ref (the reader)
grep -n "Fail to write RPMB" $COMMANDCODE_SCRATCHPAD/out06001A06/strings.txt
```

---

## 15. Corrections and deltas to prior documents

| doc | item | correction |
|---|---|---|
| `86` §1 | "entry `0x801234FC`" | It is a **descriptor-table pointer**, not an entry point. Execution starts at `base+0x10`. Same for `LDR_01`'s `0x801356D8` (`66` §1). |
| `86` §1 | "`6A0000EA` occurs at `0x55F8` and `0x1F2E8`, ordinary occurrences" | Confirmed and extended: `0x801055F8` is a plain `b` in a flash-read helper and `0x8011F2E8` is the tail `b` of the `memset` helper `FUN_8011F2DC`. No dispatcher `case` is involved. |
| `86` §3 item 2/3 | "compare its dispatcher with LDR_01's four dispatchers" | There are no sub-dispatchers: `06001A06` uses one flat router at `0x8010F280`. |
| `86` §3 item 2 | "`0xE5`/`0x39`/`0x3A` CREAD, `0xC4`, `0xDD`" | `0x39`, `0x3A`, `0xC4` and `0xDD` are **not implemented** in this build. Only the `0xE4`/`0xE5`/`0xE6`/`0xEB` session path exists. |
| `72` §5 | "`0xDE` setters are dead code in every build" | Stronger here: in `06001A06` the setter is **not present at all**; `BOOTCTR_FCTRY` has one reference (the reader). |
| `72` §3.1 | "`LDR_01` masks `0xFFFFFFFF` as absent" | Correct, and **this build does not mask it** — `0xFFFFFFFF` ⇒ `mode=2`. Useful: `06001A06` reports the expired state the way the bootloader sees it. |
| `76`/handoff §2 | "-read `0xDE` on a patched loader to probe" | `06001A06` is such a probe: same request/response, no `LDR_01` sentinel. |
| new | — | The `0xF7`/`0xF8` path is **RPMB-gated** (`FS_DIRTY` read/modify/write). This was not documented for `LDR_01` and is the decisive constraint for the bricked units. |
| new | — | The `0xF7`/`0xF8` destination LBA is **host-controlled via a QCFM V2 `'rrcq'` run record**, gated only by `FUN_8011B774`. That is a stronger write primitive than `66`/`72` credit, but still bounded by the MCT allow-list. |

---

## 16. Bottom line for the main session

* **Ship-blocking answer:** `06001A06` does **not** unlock the bricked 32 GB
  units. It cannot write RPMB, so it cannot fix `BOOTCTR_FCTRY`; and a damaged
  RPMB prevents even its eMMC write path from running.
* **What it does buy:** a strictly better BOOT_MODE *diagnostic* (reports
  `mode=2` for the `0xFFFFFFFF` sentinel), a host-addressed eMMC
  write/erase primitive restricted to MCT allow-listed partitions, and a
  clean negative result that removes "signed-loader memory-safety bug" from the
  recovery option list.
* **Cheapest next action:** run the Phase R1/R2 read-only sweep on the **spare**
  64 GB unit. It costs one loader upload and about ten read commands, it is
  fully reversible (power-cycle), and its `0xB4`/`0xD9` output closes unknown
  **U1** — the only fact that could change the conclusion for the 32 GB units.
* **Do not** set `BOOTCTR_FCTRY`, do not `0xEE`, do not `0xE0`, do not `0xF3`
  on any unit until Phase W0/W1 has been approved and executed.
