# Session 5 (absolute) — `0x2801` is the password record, `0x2074` is the A/B update state, and the pivot to a native CRC boot plan

> Numbering: absolute **5** of 12, pre-Linux.  The reports for this session are
> `72`–`79` and `83`–`87` (the core ones are `73`, `75`, `76`, `79`, `83`–`85`);
> its handoff is `HANDOFF-2026-10-21-next-session.md`.
> `docs/history/newdocs/session-notes/session-05.md` is a **different, older**
> kexec-phase note — not this session.  See `NUMBERING.md`.

Date / span: 2026-10-21 → 2026-10-25 (device logs are ART = UTC−3).  The live
audit was 2026-10-21; the deckard decompilation, the password/PIN arithmetic and
the native-boot plan continued to 2026-10-25.  **No NVRAM, RPMB, eMMC or flash
writes were made.**

Unit state at the end: the connected **reference 64 GB** unit (hardware PIN
`0x50175127`, bootrom `0xE7`) was on TabletOS **2.0.0.4869**, rooted over the
qconn door + SSH.  `0x2801` was `A=0`, `B=10` (gate 10, retry branch); `0x2074`
was `OsRegionV3`, requested=actual=0, `boot_count_0=0x017C`, `max=0`;
`0x2802` unchanged; `0x2004` and `0x2040` absent; `0x2002` = `0x50175127`.
The unit was left in TabletOS with the `nvram` editor and the BB10
`libnvram.so.1` on `/accounts/devuser`; the `/proc/boot/libnvram.so.1` symlink
was re-created after the reboot for the post-reboot reads and left in place, but
it is RAM/temporary and will vanish on the next reboot; the qconn door was
stopped after the audit.  The spare 64 GB — the test bed of the next sessions,
later identified as `0x50176EC1`, BSN `1063043093` — was **not** connected this
session.  The two 32 GB units were already bricked.

## Goal of this session

Start from the 2026-10-21 handoff and close its open items: the ECDSA key-pointer
anomaly, the `loaders.dat`/`0xDE` setter search, and the loader audit; the main
unknown left was **who writes NVRAM `0x2801`**.  The second question — whether
the boot gate can be opened for an unsigned or modified image — followed from
that writer analysis.

The operator then forced a re-check of the record identities (“isn't that just
the device pin/password?”), corrected the `0x2074` reading (a region/partition
selector, not a boot counter), pointed at the decompiled `deckard` Python and
asked for the strategic question to be answered: **how does any of this help boot
Linux natively, and what are the next steps?**

“Done” became: the writer/identity question settled with evidence, the live state
audited read-only, a native-boot plan with explicit risk, and a subagent fan-out
for the next session.  A Linux boot was **not** attempted; the session ends with
a plan, not a device write.

## Confirmed (measured, read in a file, or seen on the bench)

### 1. The product verifier, the loader and the boot gate

- **`hd0head.bin` is a pre-relocation image.**  It is linked at `0x7FFF7000` and
  loaded at `0x80000000`; a load-time fixup adds **`+0x9000`**.  Absolute
  *literals* are 0x9000 low; branch/call targets are already correct at the
  runtime base.  With the fixup, the ECDSA key pointer `0x800143D9 + 0x9000 =
  0x8001D3D9` is an uncompressed secp521r1 point on the curve, and the curve
  constants land at `0x1D460` (`3`), `0x1D4A4` (`b`) and `0x1D4E8`
  (`p = 2^521−1`).  The product path is functional; the earlier “broken key
  pointer” was the relocation.  [73 §0–§3; `91` §0.1/§1 repeats the relocation]
- **No recoverable loader in `loaders.dat` implements `0xDE` action 2.**
  `bb10mt/loaders.dat` has 82 entries; 81 extract and decompress (LDR_00 is
  corrupt: `dataOffset = 0xa`, inside the entry table).  PlayBook model
  `06001A06` maps to `LDR_01` (`loader_06001A06-00.bin`, 238,128 B, base
  `0x80100000`).  Every `0xDE` handler accepts **action 0 (read) only**; actions
  1/2 log and return without a response, and the setter/decrement helpers have
  no callers or pointer literals.  There is therefore **no pre-OS
  `BOOTCTR_FCTRY` writer**.  `LDR_01`’s action-0 reader additionally masks
  `0xFFFFFFFF` as “absent”; the newer OMAP readers and the bootloader treat it
  as invalid (selector 2).  [72 §1–§5]
- **The bootloader can provision an absent `BOOTCTR_FCTRY` at `0x32`.**  The
  path is `0x800090A0(1)`, gated by `0x80010EB8` reporting that RPMB is not
  provisioned.  It does **not** repair `0xFFFFFFFF` (the bootloader reader
  returns 2), so this is not a recovery route for the expired unit.  [72 §6]
- **Boot main and the gate.**  `0x80014974` reads NVRAM `0x2040` (NukeStatus);
  an absent record leaves the pre-initialised `2`.  `tbb [pc,r0]` at
  `0x8001498A` (table at `0x8001498E`): 0/2 → gate at `0x80014998`, 1 → nuke,
  3..8 → normal path, ≥9 → nuke.
  `FUN_80014F84` returns `B − A` (0 if `B < A`), where
  `B = clamp(record[+0x1C], 3, 10)` and `A = record[+0x18]`.  A nonzero result
  jumps to the retry loop `0x80014A1E`; the normal path calls `FUN_80014F0C`
  (`A = max(A,10)`), then the RPMB selector `0x8000911A`, and `0x800149C2`
  nukes on **any** nonzero selector.  The retry loop’s selector is 0 → ECDSA,
  1 → CRC (requires `region[0xd4] & 0xFF == 6`), 2 → failure status `0x0F`.
  [75 §3; 79 §1.1–§1.2; 85 §1]

### 2. `0x2801` — `NV_OSSTORE_PasswordStruct`

- **Identity, three independent ways.**  The QNX resmgr name/ID table stores
  `0x2801` immediately before `NV_OSSTORE_PasswordStruct_NUM` and `0x2802`
  before `NV_OSSTORE_PasswordStructReserve_NUM`; `LDR_01` logs
  `"Password record not found"` / `"Flash Failure, Password"`; and the boot
  stage implements the bootrom `0x0A`/`0x0F` password exchange.
  [75 §1; 77 §1/§2; 79 §3]
- **Layout (0x70 bytes).**  `+0x00` format `0x00020000`; `+0x04` length `0x70`;
  `+0x08` flag `1`; `+0x0C` iteration count; `+0x10/+0x14` 8-byte salt;
  `+0x18` failed-attempt counter **A**; `+0x1C` maximum **B** (clamped 3..10);
  `+0x20` sequence/nonce; `+0x24` 0; `+0x28` blob offset `0x30`; `+0x2C` blob
  length `0x40`; `+0x30..+0x70` 64-byte digest.  `0x2802` is a fallback copy
  written **only when the primary write fails**, not a success-path mirror.
  [75 §2/§4.1; 76 §2]
- **The live record.**  `0x2801` = `00020000 70000000 01000000 c4130000
  b543a0b9 b5862d22 00000000 0a000000 ce64e8a1 00000000 30000000 40000000`
  then the 64-byte digest; `A=0`, `B=10`, `record_exists=1`, `is_protected=0`,
  `get_permissions=0`.  The gate is 10; the unit was **already** in the
  `0x80014A1E` retry branch.  The post-reboot read was byte-identical
  (SHA-256 `5311a186…0628`).  [76 §1/§1.1]
- **The digest is the verifier for `0000`.**  With `state = "0000"`,
  `salt = record[0x10:0x18]` and `n = record[0x0C] = 5060`, the loop
  `state = SHA-512(LE32(counter) ‖ salt ‖ state)` for `counter = 0..5059`
  reproduces the 64-byte blob **exactly**.  The bootrom `0x0A` reply carries the
  same salt/5060, and the `0x0F` 64-byte answer equals both
  `HashPassV2("0000", challenge, salt, 5060)` and
  `pass(challenge ‖ record_digest)`.  The plaintext is not stored.  [84 §4/§5;
  75 §1]
- **Writers.**  `FUN_80014DEE` creates a default record (`A=0`, `B=10`, blob
  `FF`); `FUN_80014E34` validates/repairs it, called from the pre-boot
  trampoline `0x800159AC → 0x80015A18` **before** boot main; `FUN_80014F0C`
  sets `A=max(A,10)` on the normal path; `FUN_80014F3C` increments `A` on a
  password mismatch; `FUN_80014F5C` sets `A=0` and bumps `+0x20` on a match; the
  nuke `FUN_800156E8` deletes the record.  The booted OS reaches it through
  `/dev/nvram` (`0xC0080509` write; `0xC008050C` wipe, which sets `A=B` and
  `memset(+0x30,0xFF,0x40)`).  [75 §4; 77 §4; 79 §1.3]
- **`LDR_01` is read-only in practice.**  `FUN_8011DF04` reads `0x2801` and is
  wired into SETMODE; `FUN_8011DD20`/`FUN_8011DDEC` read/clamp/write it but have
  **zero** callers and zero pointer literals.  [75 §4.3; 79 §1.5]
- **A=0 is sticky.**  The retry branch never calls `FUN_80014F0C`; the record
  stays `A=0` until a correct `0x0F` exchange or an explicit write restores
  `A=10`.  [75 §5; 76 §1.1]

### 3. `0x2802`, `0x2002`, `0x2040`, `0x2004`

- **`0x2802` is the reserve**, 112 bytes; live content is all zero except a few
  bytes near `+0x53`, not a copy of `0x2801`.  Patching it is not required by
  the gate.  [76 §2; 75 §4.1]
- **`0x2002` is `NV_OSSTORE_PIN_NUM`, the hardware/device PIN** — live
  `27 51 17 50` = `0x50175127`, 4 bytes, permission word `0x0001` (conditional
  protection), published by the resmgr as `PIN`/`PIN64`.  The resmgr reads it in
  exactly one place and **never compares it**; it cannot be `0000` under any
  encoding (ASCII, numeric, BCD, 4/8-byte).  The 4-digit unlock code is the
  **device password** (`0x2801`); the hardware PIN is an identity value.
  [84 §0/§6; 77 §2 row 8; 76 §1]
- **`0x2040` and `0x2004` are absent** (`Recieved bytes: 0`).  Because `0x2040`
  is pre-initialised to `2` and the retry path never calls the normal-path
  `0x2040` increment or the post-verify `0x2004 == 0xAAAAAAAA` check, neither
  record is ever created.  This is why `A=0` is a stable fixed point.
  [76 §3]
- **Other IDs from the resmgr table:** `0x2004` `NV_OSSTORE_JvmStart_NUM`
  (read by the boot stage as the committed-image flag), `0x2040`
  `NV_OSSTORE_NukeStatus_NUM`, `0x2075` `NV_OSSTORE_BOOT_MODE_BLOCK`, `0x2821`
  `NV_OSSTORE_ResetInfo_NUM`, `0x2823` `NV_OSSTORE_MMCBootInfo_NUM`, `0x2819`
  `NV_OSSTORE_NotSupportedOS_NUM`, `0x2050`
  `NV_OSSTORE_NVR_CONDITIONAL_WRITE_NUM`.  [79 §3; 77 §2; 84 §6]

### 4. `0x2074` — `NV_OSSTORE_OS_REGION`, the A/B update state

- **`0x2074` is 8308 decimal and the record is 72 bytes**, exactly
  `OsRegionV3 = '=IBBHHHIQ24s24s'`.  The “70-byte” reading came from treating
  `+0x06` as a `u16[5]` and losing two bytes of the `u32 flags`; there is no
  70-byte version.  [83 §0/§2.4]
- **Fields:** `+0x00 u32 version=3`; `+0x04 u8 requested_region`;
  `+0x05 u8 actual_region`; `+0x06 u16 boot_count_0`; `+0x08 u16 boot_count_1`;
  `+0x0A u16 max_boot_count`; `+0x0C u32 flags`; `+0x10 u64 epoch`;
  `+0x18/+0x30` 24-byte version IDs.  Live: version 3, requested=actual=0,
  `boot_count_0=0x017C`, `boot_count_1=0`, `max=0`, `flags=0`, `epoch=0`,
  version IDs empty; `boot_count_0` was `0x017B` one boot earlier.
  [83 §2.3/§3; 76 §4]
- **On-media regions are 0/1, not 1/2.**  Region 0 = `/dev/hd1`, region 1 =
  `/dev/hd2`; “1 or 2” is the 1-based drive numbering.  **0 is a valid region**,
  not “unset”; the bootloader masks `+0x04` with `&1`, so a stored `2` would
  read as region 0.  The special zeros are `version=0` (OS refuses the record),
  `epoch=0` (unknown install time → grace), `max_boot_count=0` (unlimited).
  [83 §4/§9]
- **The bootloader’s use of `0x2074`.**  `0x800142DC` reads it; `0x8001430C`
  takes `requested & 1`; it verifies that region, writes `actual` at `+0x05`,
  increments `boot_count_<region>` at `+0x06/+0x08`, clears the loser’s counter
  if it booted the non-requested region, and writes the record back with the
  same length.  `max_boot_count=0` means **unlimited**; a nonzero value is a
  budget of successful boots (`switch()` sets 4).  [83 §5/§6]
- **The factory path does not read `0x2074`.**  If `FUN_8000911A()` (the RPMB
  `BOOTCTR_FCTRY` selector) is nonzero, `0x800142BC–0x800142C6` sets the region
  index to **0** and verifies region 0 directly; the `0x800142A8` driver with
  requested/actual and fallback is the **product** path.  [83 §6; this is the
  evidence the later `94`/`95`/`97` confirmed]
- **`switch()` / `sane()`.**  `Platform.switch()` → `OsRegionV3.switch()` sets
  `requested = actual ^ 1`, `boot_count_0 = boot_count_1 = 0`,
  `max_boot_count = 4`, `epoch = now`.  `sane()` sets `requested = actual` and
  `max_boot_count = 0`; it does **not** zero the counters or the epoch.  The V2
  `switch()` has a `_replace(reqested_region=…)` typo and raises, so only V3 can
  switch.  The resmgr’s rollback helper `0xD680` sets `max=5` **and**
  `count[actual]=5`, forcing the next boot to the other region.  [83 §4.4/§5.3]
- **`0x207C` is the radio-region sibling** (`NV_OSSTORE_EXTRA_REGION`, 8316):
  header `'=4BII'` with magic `MRMC`, version 1, up to 15 `'=8sHH'` entries
  `{name[8], requested_region, actual_region}`; deckard looks for `MODEM`.
  [84 §2]

### 5. Who can write NVRAM

- **The booted OS is the general writer.**  `nvram_resource_manager` owns
  `/dev/nvram`; devctl `0xC0080502` reads a record, `0xC0080509` writes (`id` at
  `msg+4`, data at `msg+8`, length = size−8), `0xC008050C` is the password
  wipe.  The per-ID descriptor table is 288 entries × 56 bytes at
  file `0x12D54`; entry #133 is `0x2801`; the name table is at
  `0x14a6c`/`0x14aa4`.  The resmgr has exactly **two** code references to
  `0x2801` (`0x9D30` read, `0x9E04` write) and both are inside the wipe; it
  never computes or checks the digest.  [77 §1–§4; 75 §4.5]
- **The practical client is `nvram-editor/nvram`.**  It dlopens
  `/proc/boot/libnvram.so.1`, opens `/dev/nvram` `O_RDWR`, and binds
  `nv_get_record`/`nv_write_record`/`nv_write_secure_record`/…  `patch`
  re-reads the live record, preserves its length, patches in place, then writes
  insecure then secure; it returns early if the insecure write fails, so a
  protected record may never reach the secure writer.  `write` uses the supplied
  size as the **new record length**, so a 4-byte `write 0x2801 …` would truncate
  the 0x70-byte record.  Use `patch` only.  [74 §1–§2; 75 §4.6]
- **The editing environment is mismatched.**  The uploaded
  `libnvram.so.1` is **BB10.0.9.388**, 12,716 bytes; the stock TabletOS
  `/base/lib/libnvram.so.1` is 5,740 bytes and lacks
  `readOSMetrics`/`readBootromMetrics`, which is the editor’s
  `Error: Symbol not found`.  The record devctl ABI is compatible, so
  read/patch work.  `get_bootrom_metrics` **SIGSEGVs** because the BB10 library
  allocates `0x4E8` bytes while `nvram.c` writes `0x4FC` (a 0x14-byte over-read
  in `memcpy`), not because NVRAM is damaged; the partial dump is
  `brom-partial.bin`.  [76 §5; 78 §2.9; 74 §1]
- **`/proc/boot` is RAM/temporary.**  The `libnvram.so.1` symlink vanishes on a
  reboot and must be re-created; `/accounts/devuser` persists.  [74 §1; 76 §1]
- **CFP/bb10mt/bbusb do not write `0x2801`.**  The apparent `cfp-old.exe` hit is
  a `call rel32` displacement, not a record reference.  [75 §4.4]

### 6. Deckard / the OS-side Python

- **Two decompiled trees:** `deckard-2.1.0.1917` (25 modules) and
  `deckard-2.0.0.4869` (22 modules), from Python 2.7 `.pyc`
  (magic `03f3 0d0a`).  Python 2.7.18 was built from the operator’s tarball with
  `-std=gnu99` on GCC 16; `uncompyle6 3.8.0` produced the sources.
  [84 §1; session tooling]
- **`deckard.nvram` is a thin `/dev/nvram` layer:** it opens the device and
  calls `nto.pread/pwrite(fd, size, offset)` with the **record ID as the
  offset**.  Across both versions it touches exactly **four** IDs: `0x2074`
  (OS region), `0x207C` (radio region), `0x2819` (OS blocklist) and `0x2852`
  (radio blocklist).  It never touches `0x2801`, `0x2802` or `0x2002`.
  [84 §2]
- **No Python handles a device password or PIN.**  The only `password` strings
  are cellular APN credentials for the update PDP context; the only `pin`
  hits are substrings.  `platform/deviceinfo.py` reads the PPS `deviceinfo`
  object but uses only `BootromBuildDate`/`BootromBuildTime`, never `PIN`.
  [84 §3]
- **`0x2819` is the downgrade blocklist:** 10 × 12-byte
  `{rangeStart, rangeEnd, osTypeBitMask}` (`OSRangeAndType '=III'`, 120 bytes);
  `0x2852` is the radio equivalent.  The downgrade to 2.0.0.4869 is already
  done; `0x2819` is the anti-downgrade lock, not a future task.  The 4869
  vulnerability that gives root SSH from boot is a separate issue.  [84 §2;
  operator statement]
- **`PasswordManager`** is the named device-password service
  (`sanity.cfg:110`, boot-critical, priority 550); its binary/PPS object are on
  the RCFS and are **not** in the corpus.  The only password machinery present
  is the boot stage/loader, the resmgr wipe, and the name.  [84 §7.3]

### 7. The live image and the CRC path

- **Active IFS header at `hd1 + 0x100000`:** word 0 `0xEA0001FE` (ARM
  `b +0x800`), `+0x04` load `0x800FF800`, `+0x08` end `0x803BF610`,
  `+0x20` cookie `0xD7A82D1F`, `+0x24` base `0x800FF800`, `+0x28` metrics
  `0x800FF82C`, `+0x34` `0x00030012`, `+0x38` `0x000001CC`, `+0x3C`
  `0x800FF804`, `+0x40` `0x800FF808`; **`+0xD4 = 0`**, so the live image is
  **not CRC-eligible**.  `hd2` at the IFS offset is all zero — free for staging.
  [85 §0/§4]
- **The CRC path requirements:** `BOOTCTR_FCTRY` present with value 1..0x32
  (RPMB, authenticated) **and** `region[0xD4] & 0xFF == 6` **and** a matching
  CRC-32/ISO-HDLC trailer over `[start, end−4)` at `end−4`.  The structural
  checks also include `desc+0x9C == 0xC3D0E193`, `end−8 == 0xD7C82D1F`,
  `end−4 != 0xFFFFFFFF`, size ≥ 0x20000, and the model-ID match.  [85 §1;
  89 §3]
- **The live unit’s gate interaction.**  Because `A=0 < B=10`, the boot main
  takes the retry branch; a factory selector would be read **inside** the retry
  loop (CRC) rather than on the normal path (where any nonzero selector nukes).
  The code says this, but the region prerequisites were untested and **no RPMB
  write was made**.  [76 §1/§6; 79 §2/§4]

### 8. Unit inventory, the downgrade, and the Cellebrite lead

- **Inventory (operator correction):** two working **64 GB** units — the
  connected **reference** (`0x50175127`) and one **spare** (later identified as
  `0x50176EC1`, BSN `1063043093`); two **bricked 32 GB** units (one NVRAM wipe,
  one invalid RPMB flag).  There is **no working 32 GB unit**.  2.0.0.4869 is
  already the downgraded OS (from 2.1.0.1917) and gives root SSH from boot;
  `/dev/rpmb` writes were proven historically on a 32 GB unit that is now
  bricked.  The spare 64 GB is the first test bed; the reference must stay
  untouched.
  [85 §0; operator statement]
- **Cellebrite pack:** `ufedblackberrypack.epr/06001A06_0000000A` is a signed
  PlayBook loader (159,968 B, SHA-256 `03ffaf26…c6dc`, `D7D32D1F` at +4,
  load `0x80100000`, entry `0x801234FC`, tail-8 `D7C82D1F`), with the PlayBook
  strings (`BOOTCTR_FCTRY`, `Fail to write RPMB`, `Password record not found`).
  `05001A06_0000000A` is a Qualcomm loader (load `0x02000008`).  The pack is
  **not** the BBOS exploit: the patched-loader signature `DB9F07EB` is absent;
  the writeup’s three-address `WRITE_RAM_SETUP` trick does not work on
  BB10/PlayBook because BB10 strictly checks address parameters.  The signed
  loader was still a candidate for a length/offset/sector-check bug in its
  command handlers.  [86 §1–§3]

### 9. Tooling

- Python 2.7.18 built from `Python-2.7.18.tgz` with `-std=gnu99` (GCC 16
  rejects the old default); `uncompyle6 3.8.0`; the `.pyc` magic is
  `03f3 0d0a` = Python 2.7.  [session tooling; 84 §1]
- Capstone 5.0.7 and Ghidra 12.1.4 headless for `hd0head.bin`/`LDR_01`;
  `analysis/loader-audit-2026-10-21/` has `parse_loaders.py`, `scan_de2.py`,
  `disarm.py`, `check_reloc.py`, `find_p521.py`, `loaders.csv` and the Ghidra
  scripts.  [72 §8; 73 §6; 75 §7]
- The editor and the BB10 library were uploaded to `/accounts/devuser` and
  verified by pulling them back and comparing SHA-256; the `/proc/boot` symlink
  was re-created for the audit and is RAM-only.  [76 §1]

## Inferred (reasoning from evidence, not directly measured)

- **The live gate is a stable fixed point.**  `0x2040`/`0x2004` are absent, the
  boot main substitutes `0x2040=2`, the gate returns 10, and the retry branch
  never calls `FUN_80014F0C`; the byte-identical post-reboot record is the
  measurement.  Strength: strong for this unit; the general rule “a normal boot
  sets A=10” is from the code.  [76 §1.1/§3; 75 §5]
- **`0x2801` is a one-way verifier for `0000`.**  The exact 512-bit match, the
  `0x0A` salt/5060 echo, and the `0x0F` equality are arithmetic, not a field
  guess; the plaintext is absent.  Strength: very strong.  [84 §4/§5]
- **`0x2002` is an identity value, never a credential.**  The resmgr only
  reads/publishes it; there is no comparison, hash, counter or salt.  Strength:
  strong (code + live value).  [84 §6]
- **`0x2074` is update state, not a device boot tally.**  `boot_count_0` grows
  because `requested == actual` and `max=0`; the bootloader increments it after
  a successful region verify.  Strength: strong.  [83 §3/§5]
- **The factory/CRC path is region 0 only.**  Our own `83` §6 already said the
  non-zero selector sets region 0 and never reads `0x2074`; `85` did not follow
  that through.  The later `94` §0 / `95` §5 / `97` §0 prove it with a region-0
  reseal and boot.  At the time this was an inference from `83`; it is now a
  confirmed fact, and the plan-level correction is recorded below.
- **The 32 GB units are recoverable only by eMMC desoldering or a loader bug.**
  The NVRAM/RPMB damage is not reachable from the booted OS on those units (no
  OS), and the shipped loaders have no repair command.  Strength: strong for the
  shipped-loader route; the Cellebrite loader was the remaining unknown.  [85
  §7; 86; later `88` closes the loader route]
- **A signed Cellebrite loader might expose arbitrary eMMC access.**  It was a
  hypothesis from its command set and the BBOS precedent, not a measurement;
  the audit was handed to track A.  Later `88` found no arbitrary read/write,
  no `0x39/0x3A`, no `0xC4/0xDD`, no RPMB writer, and `0xDE` read-only, so the
  hypothesis is dead.  [86 §3; 88 §0/§9]
- **The `0x2801` record is writable but the secure copy’s authority is
  unknown.**  `is_protected=0` and the editor’s insecure write lands; there is
  one stored copy per ID plus a provenance bit, and the bootloader’s preference
  is not proven.  Strength: medium; would need a write+reboot.  [76 §1; 77
  §7.5; 78 §4.4]

## Dead ends and wrong assumptions (valuable — do not skip)

1. **`0x2801` was a “time/recovery record”.**  The handoff and `69`/`71` read
   the attempt budget as a clock/recovery condition.  The resmgr name table,
   `LDR_01` strings, the boot-stage `0x0A`/`0x0F` handler and the exact digest
   proof all say **`NV_OSSTORE_PasswordStruct`**.  The label was wrong, not the
   field offsets.  [75 §1; 84 §4]
2. **The planned `patch 0x2801 0x18 0x00000000` was moot.**  The live record
   was already `A=0`, so the write would have changed nothing; `B=10` made the
   gate 10 regardless.  I did not write it.  [76 §1/§6]
3. **`0x2802` is not a success-path mirror.**  It is written only when the
   primary `0x2801` write fails; the live reserve is not a copy.  Patching it is
   unnecessary.  [74 §2; 75 §4.1; 76 §2]
4. **`0x2002` is not the unlock password.**  The operator used “PIN” for the
   unlock code, but the hardware PIN is a separate identity value.  `0x2002` is
   the hardware PIN `0x50175127`; it cannot encode `0000` in any of ASCII, u32,
   BCD or 8-byte form, and the resmgr never compares it.  The password verifier
   is `0x2801`.  [84 §0/§6]
5. **`0x2074` is not a plain “boot counter”.**  It is `OsRegionV3`: requested/
   actual region, per-region boot budget, max, epoch, flags, version IDs.
   `+0x06` *is* a counter, but a per-region budget counter, not a device tally.
   [83 §0/§9]
6. **The on-media region values are 0/1, not 1/2.**  “1 or 2” is the 1-based
   `hd1`/`hd2` drive numbering.  A stored `2` would be masked to 0 by the
   bootloader; 0 is a valid region, not “unset”.  [83 §4]
7. **The A/B fallback does not apply to the factory/CRC path.**  This is the
   big plan-level error.  `85` §1–§3 assumed the selector is global, the retry
   loop’s region driver verifies the requested region and falls back, and
   therefore both regions must be CRC-valid before writing `BOOTCTR_FCTRY`;
   Phase 1/2 staged region 1 and expected fallback to region 0.  Our own `83`
   §6 already said the non-zero selector sets region **0** and never reads
   `0x2074`; the `0x800142A8` fallback driver is the **product** path.  Later
   `94` §0, `95` §5 and `97` §0 proved region 0 only, once, no fallback, with
   the 5-byte reseal boot.  Only region 0 needs to be CRC-valid; the A/B
   staging is product-path work, not a factory-boot step.  [83 §6; 94; 95; 97]
8. **`0x803BF610` is not the entry point, and `0x80014A90` is not the jump.**
   `85` Phase 5 called `0x803BF610` the entry and `0x80014A90` the enter-OS
   path.  `0x803BF610` is `LoadEnd` (first byte past the 0x2BFE10 image); the
   effective entry is `0xEA0001FE → 0x80100000`; `0x80014A90` is a Thumb label
   that logs “Leaving bootrom and entering OS” and **returns**; the real
   hand-off is ARM `bx r0` at `0x800192A8`, with `r0 = *(0x80025E74)`.  [91
   §0.1/§1/§2.2/§4]
9. **No recoverable loader in `loaders.dat` has a working `0xDE` action-2
   setter.**  Every recoverable `0xDE` handler accepts action 0 only; actions
   1/2 return no response; the setters and decrement helpers have no
   callers/pointers.  `LDR_01` even masks `0xFFFFFFFF` as “absent”, so its
   action-0 read is not the bootloader’s selector.  [72 §3–§5]
10. **`LDR_01` cannot write `0x2801`.**  Its two writers are dead code; only
    the reader is wired into SETMODE.  [75 §4.3; 79 §1.5]
11. **`get_bootrom_metrics` is not an NVRAM fault.**  The BB10 library allocates
    0x4E8 bytes and `nvram.c` writes 0x4FC; the crash is the editor/library size
    mismatch.  The partial dump is not trustworthy for the metrics block.
    [76 §5; 78 §2.9]
12. **`/proc/boot/libnvram.so.1` is not persistent.**  It is a RAM/temporary
    symlink; re-create it after every boot.  [74 §1; 76 §1]
13. **The BBOS Cellebrite exploit does not work on BB10/PlayBook.**  The BBOS
    bootrom accepts loosely checked `WRITE_RAM_SETUP`/`WRITE_RAM` addresses; the
    writeup’s own `messages.txt` says BB10 strictly checks address parameters.
    The UFED pack contains signed loaders, not the `DB9F07EB` patched exploit.
    [86 §1–§2]
14. **There is no working 32 GB test bed.**  The two 32 GB units are bricked
    (NVRAM wipe / invalid RPMB flag); the spare 64 GB is the test bed, and the
    connected 64 GB is the reference.  [operator correction; 85 §0]
15. **The downgrade is not future work.**  2.0.0.4869 is already the downgraded
    OS and gives root SSH from boot; the 2.1.0.1917 path and its blocklists are
    history, not a next step.  [operator statement; 84 §2]
16. **`A=0/B=10` does not prove “the password was entered correctly this
    boot”.**  It is the full-budget state, reachable from the initializer
    (blob `FF`), a correct `0x0F` entry, or the pre-boot recreate; a normal boot
    sets `A=10`.  On these units the blob is non-`FF` and is the digest of
    `0000`, so a password *is* set; `A=0` alone cannot timestamp the entry.
    [75 §3/§4; 76 §1; 84 §4]
17. **`A=0` is sticky.**  The retry branch never calls `FUN_80014F0C`; it will
    not self-clear.  Restoring `A=10` while `BOOTCTR_FCTRY` is `1..0x32` is
    dangerous because the normal path’s `0x800149C2` nukes on a nonzero
    selector.  [75 §3/§5]
18. **The stage’s `0x0F` command is not the bootrom `SEND_PASSWORD` frame.**
    The stage’s verdict payload is a 0x40-byte hash over the record blob and
    sequence; the bootrom’s `SEND_PASSWORD` is a different, 0x18-byte message.
    The host sequence that reaches `FUN_80012182` case `0x0F` is still unmapped.
    [75 §8; 84 §9]
19. **`0x2074` V2 cannot switch.**  The V2 `switch()` has a
    `_replace(reqested_region=…)` typo and raises; only V3 works.  `sane()` does
    not zero counters or epoch; it only sets requested=actual and max=0.
    [83 §4.4]
20. **`max_boot_count=4` is a hard trap for A/B tests.**  A freshly switched
    image gets four boots; the fifth falls back.  Use `max=0` for tests and
    restore `sane()` after.  [83 §5; 85 §3]
21. **`0x2074` is 72 bytes, not 70.**  The 70-byte reading is the `u16[5]`
    misread; the resmgr accepts 0x14/0x18/0x48 and the OS reads `STRUCT.size`.
    [83 §2.4]

## Unknown / open questions

- **Which host command reaches `FUN_80012182` case `0x0F`?**  The dispatcher is
  polled by `FUN_80012516`, separate from `LDR_01`; CFP’s exact pre-OS sequence
  is unmapped.  Resolve with a live trace or a CFP command-map audit.  [75 §8]
- **Does the bootloader read the “insecure” or the “secure” copy of `0x2801`?**
  There is one stored copy per ID plus a provenance bit; the editor’s insecure
  write lands, but the bootloader’s preference is not proven.  A write+reboot
  would settle it.  [77 §7.5; 78 §4.4; 79 §4]
- **Is `0x2801` in the signed “secure records” set?**  The resmgr’s wipe writes
  with `secure=1`, which hints yes, but the per-platform ID list is
  relocation-time data and was not recovered.  [77 §7.3; 84 §9]
- **`0x205A` (per-OS-type model-ID override) and the `0x2819` blocklist
  contents.**  `0x205A` was never dumped; `0x2819` is read by the verifier.
  A read-only `nvram read` would settle it.  [85 §6; 89 §3.3]
- **`0x2074` `flags` (`+0x0C`) bits, the `sane()` caller, and V1 `sw_restore`.**
  No writer/reader interprets `flags`; `sane()`’s trigger is outside the
  decompiled module set; `sw_restore` has no writer/reader.  [83 §10]
- **The `0x2002` conditional-write gate.**  `0x2002` is in the conditional
  protected class; the gate reads NVRAM `0x2050`.  `0x2050` was not read in the
  2026-10-21 audit; `PLAYBOOK-REFERENCE.md` §5.3 (a `rea-work`-era file that is
  **not** in this repository — the claim is unverifiable here) says it is absent
  on the audited units, which would make `0x2002` writable in practice.  Do **not**
  test this; the hardware PIN must stay consistent with the bootrom.  [84 §6]
- **Does the Cellebrite `06001A06` loader expose arbitrary eMMC read/write?**
  This was the Track A question.  The plan was a read-only test on the spare
  64 GB and a handler audit (`0xE5/0x39/0x3A`, `0xF7/0xF8`, `0xEE`, `0xC4`,
  `0xE0`, `0xC0`).  Later `88` found no arbitrary read/write, no
  `0x39/0x3A`, no `0xC4/0xDD`, no RPMB writer and a read-only `0xDE`; it cannot
  repair the 32 GB units.  [86 §3; 88 §0/§9]
- **How to repair the two 32 GB units.**  One is an NVRAM wipe, the other an
  invalid RPMB flag.  Physical eMMC desoldering/programming remains the route;
  the shipped-loader and Cellebrite-loader routes are closed.  [85 §7; 88 §9;
  92 §6]
- **Does a factory selector on the live unit truly take CRC without the normal
  path’s nuke?**  The code says yes because the gate is open and the retry loop
  reads the selector; but the region prerequisites were untested and **no RPMB
  write was made**.  This is a hypothesis for a sacrificial unit, not a measured
  fact.  [76 §1; 79 §2/§4]
- **Why `boot_count_0` is already 0x017C (380).**  It increments once per boot,
  but the unit’s history is not known; it is not a factory/boot tally.  It does
  not affect the decision when `max=0`.  [83 §3]
- **The `0x2802` short-reserve repair quirk.**  `FUN_80014E34` may write a stale
  buffer when the reserve is short; it is a repair-path quirk, not a boot
  decision.  [75 §8.4]
- **The exact `0x2074` product-path fallback behavior on this unit.**  The
  driver reads requested, falls back, writes actual and clears the loser; it was
  not exercised live.  The factory path ignores it, so this is product-path
  documentation only.  [83 §6; 90]

## Superseded / uncertain

- **`0x2074` A/B fallback for the factory/CRC path — wrong.**  I believed the
  selector was global and the retry loop’s region driver would verify the
  requested region and fall back, so `85` Phase 1/2 staged region 1 and Phase 3
  made **both** regions CRC-valid.  The evidence I had was `85` §1’s reading of
  `0x800142A8`, but my own `83` §6 already said the non-zero selector sets
  region **0** and never reads `0x2074`.  Current: `94` §0, `95` §5, `97` §0 —
  the factory path is region 0 only, once, no fallback; the 5-byte reseal
  (`0x100: 04 → 06`, CRC `0x8D46FB4A → 0x116B2562`) booted TabletOS and
  `BOOTCTR_FCTRY` went 50 → 49 with `0x2074` byte-identical.  Only region 0
  needs to be CRC-valid.  See
  `docs/contradictions/85-ab-fallback-vs-factory-region0.md`.
- **`0x803BF610` as the entry point and `0x80014A90` as the jump — wrong.**
  `85` Phase 5/§4 used both.  Current: `91` §0.1/§1/§2.2/§4 — `0x803BF610` is
  `LoadEnd`; `0x80014A90` is a log/return label; the hand-off is
  `0x800192A8 bx r0`, and the effective entry is `0xEA0001FE → 0x80100000`.
  `docs/project/ARCHITECTURE.md` uses the corrected model.
- **`0x2801` as a “time/recovery record” — wrong.**  Our session corrected
  `69`/`71`/`74`; current: `75` §1, `84` §0/§4 — `NV_OSSTORE_PasswordStruct`.
- **`patch 0x2801 +0x18 = 0` as a needed test — moot.**  Live `A=0` already;
  the write is a no-op and was not made.  Current: `76` §1/§6.
- **`0x2802` as a success-path mirror — wrong.**  Current: `74` §2, `75`
  §4.1, `76` §2 — reserve, written only on a primary-write failure.
- **`0x2074` as a “boot counter” — wrong as a name.**  Current: `83` §0/§9 —
  `OsRegionV3`, the A/B update state; `+0x06` is a per-region budget counter.
- **`0x2074` values 1/2 — wrong.**  Current: `83` §4 — stored 0/1; 1/2 is
  `hd1`/`hd2` drive numbering; `2` masks to 0.
- **`0x2002` as the unlock password — wrong.**  Current: `84` §0/§6 — hardware
  PIN `0x50175127`; the password verifier is `0x2801`.
- **`A=0/B=10` as “password entered correctly” — not proven.**  Current: `75`
  §3/§4, `76` §1, `84` §4 — full-budget state; the digest proves a password is
  set, but `A=0` alone does not timestamp the entry.
- **`0x2074` `sane()` zeroing counters/epoch — wrong.**  Current: `83` §4.4 —
  `sane()` sets requested=actual and max=0 only.
- **The Cellebrite loader as a possible repair route — dead.**  My `86` lead
  proposed a read-only audit; later `88` §0/§9 found no arbitrary eMMC read/
  write, no `0x39/0x3A`, no `0xC4/0xDD`, no RPMB writer, and `0xDE` read-only.
  `86` remains the lead inventory; `88` is the closure.
- **The `0x2074` A/B switch dry run as a factory-boot step — not applicable.**
  `85` Phase 1/2 and `87` Track C planned it; `90` wrote the product-path
  runbook, but `94`/`95`/`97` show the factory path ignores `0x2074`.  `90` is
  product-path documentation, not a CRC step.
- **“Both A/B regions must be CRC-valid” — not required.**  For the factory
  path, only region 0 is read; the fallback is the pre-OS reflash (`0xF7`), not
  region 1.  Current: `94`/`95`/`97`; `98`/`101` for the reflash.
- **The 32 GB unit as a test bed — wrong.**  There is no working 32 GB unit;
  the two 32 GB units are bricked.  The spare 64 GB is the test bed.  Current:
  `85` §0 and the operator.
- **The `0x2801` secure-copy question is still open.**  My `75`/`79` left it
  open; `77` §7.5 and `78` §4.4 say one stored copy per ID plus a provenance
  bit, but the bootloader’s preference is not proven.  Do not treat the
  insecure readback as proof the bootloader saw the write.
- **The `0x2002` “writable” caveat.**  `84` §6 says `0x2002` is in the
  conditional protected class and the gate reads `0x2050`; `0x2050` was not
  read in the 2026-10-21 audit.  Treat “writable in practice” as
  “`0x2050` absent on the audited units”, not a licence to write the PIN.
- **The stable `0x2801` record says nothing about `BOOTCTR_FCTRY`.**  The
  post-reboot byte-identical `0x2801` is real, but it is an NVRAM record; the
  RPMB counter is separate.  Later sessions found the spare’s
  `BOOTCTR_FCTRY` draining (`0x32 → 0x28` over ~10 boots) because the
  device-side top-up was broken, so do not read this session’s stable `A=0` as
  a counter guarantee.  [76; `116` addenda; session-10/11 notes]

## Artefacts produced

- **Reports:** `72` (`0xDE` action-2 audit), `73` (ECDSA `+0x9000`
  relocation), `74` (nvram-editor/`0x2801`, prior-session file corrected in
  place), `75` (`0x2801` writers/gate), `76` (live `0x2801` audit), `77`
  (resmgr ID map), `78` (`libnvram` analysis), `79` (hd0/`0x2801` cross-check,
  reconstructed after the third subagent died), `83` (`0x2074` semantics), `84`
  (password vs PIN), `85` (native-boot plan), `86` (Cellebrite lead), `87`
  (next-session subagent workstreams).
- **Live captures:** `analysis/playbook-live-2026-10-21/` — `nv-0x2004.txt`,
  `nv-0x2040.txt`, `nv-0x2074.txt`, `nv-0x2801.txt`, `nv-0x2802.txt`,
  `after-*`, `meta.txt`, `uname.txt`, `brom-partial.bin`, `brom.err`, and
  `device-binaries/` (stock/uploaded `libnvram.so.1`, `nvram_resource_manager`).
- **Decompiled sources:** `analysis/deckard-2.1.0.1917/` (25 modules) and
  `analysis/deckard-2.0.0.4869/` (22 modules); the `.pyc` magic is
  `03f3 0d0a`.
- **Loader audit:** `analysis/loader-audit-2026-10-21/` (`parse_loaders.py`,
  `scan_de2.py`, `disarm.py`, `check_reloc.py`, `find_p521.py`, `loaders.csv`,
  Ghidra scripts, per-loader decompiles).
- **Tooling:** `nvram-editor/nvram.c` + ARM binary; Python 2.7.18 built with
  `-std=gnu99`; `uncompyle6 3.8.0`; Capstone 5.0.7; Ghidra 12.1.4;
  `nvdis.py`; the `bbusb` boot-mode/password helpers and `bb10mt` `bootmode`
  command.  [72 §7; 75 §7; 84 §10]
- **On the unit (left, read-only):** `/accounts/devuser/nvram`,
  `/accounts/devuser/libnvram.so.1`; the `/proc/boot/libnvram.so.1` symlink is
  RAM-only and was gone after the reboot.  The qconn door was stopped after the
  audit.  No NVRAM/RPMB/flash write was made.
- **Process artefact:** the first subagent round was killed by the weekly usage
  limit after `77` and `78` had written their reports; the third agent died
  after four tool calls, so `79` was reconstructed from the verified static pass
  plus `77`/`78`.  Lesson: have every agent write its report skeleton first and
  improve it in place; a killed agent may still leave a complete report.
  [77/78/79; `87`]
