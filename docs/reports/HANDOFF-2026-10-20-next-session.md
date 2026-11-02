# Session handoff — 2026-10-20 → next session

Read this first, then `HANDOFF.md` (continuations c/d/e), `69`, and `70`.
The long-term goal is unchanged: **boot Linux (or a modified TabletOS) on the
PlayBook**. The kexec route is the working one; the bootloader route is the
new research thread started this session.

## 1. What is finished

From the previous handoff's "next work" list:

- **Item (3) — DONE.** `67-ghidra-seeded-starts.md`: all 65 unseeded boot-ROM
  starts created and decompiled (61 usable, 4 artefacts); raw dump in
  `rea-ghidra/seeded-starts-decompiled.txt`.
- **Item (4) — DONE.** `68-rom-api-undocumented-labels.md`: all 31 undocumented
  ROM-API entries labelled; raw dump in
  `rea-ghidra/rom-api-undocumented-decompiled.txt`. Key relabels: `0x50000000`
  is GPMC, API entry 8 is `SYS_BOOT`, entries 36-38 are a FAT reader, entry 43
  is `STD_FUSE_CH_SPEEDUP_DIS`.
- **Items (1)/(2) — advanced to a working read.**
  `69-live-bootrom-loader-usb.md`: full live bootrom→ramloader chain, NVRAM
  `0x2074` baseline, EP1 closed as a dead end. Details in §3.
- **Bootloader first pass — started.** `70-rim-bootrom-hd0-stage.md`: layout,
  NVRAM region table, verifier flow, two leads. Details in §4.

## 2. Access / tooling (verified this session)

- REA 6.7.0 + Ghidra 12.1.4 are configured for Command Code. Reopen a target
  with `open_binary` (`raw-binary` / `arm` / base / `ghidra`):
  - boot ROM: `/home/psyden/rea-work/bootdumps-2026-09-11/omap4430-bootrom-0x40028000.bin`,
    base `0x40028000`;
  - ramloader: `/home/psyden/rea-work/loader_06001A06-00.bin`, base `0x80100000`;
  - RIM bootrom stage: `/home/psyden/rea-work/bootdumps-2026-09-11/hd0head.bin`,
    base `0x80000000` (mixed ARM/Thumb — see §4).
- Live USB client: `analysis/rea-ghidra/playbook-usb-probe.py` (needs pyusb;
  a venv was used). It implements `watch`, `metrics`, `e9`, `ep1`.
- udev rule installed: `/etc/udev/rules.d/99-playbook-bootrom.rules`
  (`0fca:0001` and `0fca:8001`, `MODE="0666"`).
- **Device password is `0000`.** The bootrom always issues a challenge with
  **5060** SHA-512 iterations; the client uses bb10mt's `HashPassV2` + the
  `00 00 40 00` prefix.
- Bootrom window: first seconds of a power-on. A used/abandoned bootrom refuses
  `SETMODE` (`0x09`) and stops taking writes — always power-cycle and have the
  client polling first.
- Remote kexec project: `debian-dev:/home/psyden/playbook-dev/`
  (SSH alias `debian-dev` → 192.168.122.210). This session published
  `bootrom-findings-2026-10-20/` there (README, monitor SAR ABI, loader path,
  probe).

## 3. Live USB results to reuse (69)

- Bootrom control frame: `u16 type=0 | u16 total | u8 cmd | u8 mode | u16 pktId | data`.
- Handshake: PING `0x01` → `0x02`; GETVAR(2) `0x05` → `0x06`; SETMODE(1) `0x07`
  → `0x08` (16- or 17-byte payload); PASSWORD_INFO `0x0A` → `0x0E`;
  SEND_PASSWORD `0x0F` → `0x10`.
- Upload: `0xF009` setup, 916 × `0xF004` 260-byte chunks, `0xF00A` verify
  (0.8 s for 238,128 bytes).
- **Hand-off:** `0xF005` EXECUTE_RAM is acknowledged but does not jump. Do the
  **third read** after the channel-1 response pair; it returns `0b 01 01 00`,
  then the unit re-enumerates as `0fca:8001` in ~3-4 s. This was the missing
  piece.
- Loader: `SETMODE(2)` → `0x08`; PASSWORD_INFO → `0x10`; **drain the paged
  Bugdisp log** (`0xB0` + 8 zeros → `0xB5`/512-byte pages until `0xD0`;
  this unit had >64 pages). Then `0xB4`→`0xD2`, `0xE7`→`0xD1` (PIN
  `0x50175127`), `0xD1`→`0xC8`, `0xE9`→`0xFB`.
- **`0xE9` requires a 4-byte zero payload.** Zero-length returns `0x15/0x04`.
  With zeros: `0xFB`, status dword 0, record `0x2074` =
  `03 00 00 00 00 00 75 01 00 00 00 00 …` (rest zero). This is the pre-CFP
  baseline.
- **EP1 (`0x01`/`0x81`) is dead.** Verified with the probe and with a CLI
  harness built from Sachesi 2.0.3's own `boot.cpp`
  (`rea-ghidra/sachesi-debug-harness/`). The first SETMODE OUT is accepted,
  nothing returns on `0x81`, the next OUT times out. `0x11` never reaches a
  live transaction.

## 4. Bootloader first pass (70) — the new thread

Target `hd0head.bin` = `hd0` offsets 0x0-0x200000, load base `0x80000000`
(file offset == address offset; confirmed by the boot ROM metrics pointer
`0x80014674`).

Layout: MLO header at 0, `CertPK_`/`CertPPA`/`CertISW` certificate blocks up to
`0x4000`, code from `0x8000`, bootrom main at `0x80014904`, metrics at
`0x1d050`, 0xFF fill from `0x20000`.

**Mixed ARM/Thumb — do this before trusting any decompile.** Known modes:

- **Thumb:** `0x80014148` (region verifier), `0x800142a8` (verifier main),
  `0x80014610`, `0x8001468c`, `0x800146f4`, `0x80014716`, `0x80014788`,
  `0x80014904`, `0x80013fb6`, `0x80013f64`.
- **ARM:** `0x80013770`, `0x8001404a`, `0x80018688`, `0x8001a7b4`+ (bignum /
  crypto), `0x8001a7c8`, `0x8001a7d4`.

Verified findings (Thumb path):

- **NVRAM `0x2074` = `NvOSRegionStruct`** (0xC1 buffer, accepted size
  0x18-0xC0): `+4` boot mode/region bit, `+5` last verified region,
  `+6+2*i` per-region u16 boot counters, `+0xA` u16 max count. Two-region
  verify loop; on success it clears the other counter, increments this one,
  writes `0x2074` back; at/over max it logs `Exceeded boot count`.
- Verifier validates a pointer window **`0x800ff800`-`0x800102b0`**, magic
  `0xC3D0E193` at `+0x9C`, length > `0x1FFFF`, OS signature cookie
  `0xD7C82D1F` at `-8`, and `-4 != -1`.
- **NVRAM `0x2004 == 0xAAAAAAAA`** branches straight to
  `Leaving bootrom and entering OS`, skipping the **second** verifier loop
  (the first `0x80014548` call still has to return 1).
- **Caveat / lead:** the trailer check at `0x80014838` is `cmp r4,r7; beq` —
  a 32-bit equality against the 4 bytes before the region end. Whether that is
  a CRC (forgeable) or a status while the real RSA/ECC check lives in the ARM
  crypto area is **not settled**. The Thumb decompile of the ARM crypto is
  invalid and was kept only as evidence of the mode-map problem.

## 4b. RPMB correction (user domain knowledge, 2026-10-20)

`BOOTCTR_FCTRY` is an **RPMB** file, not NVRAM. Its value is a factory-boot
counter/budget that must be **1..0x32 (50)**. Writing `FF FF FF FF` on a unit
produced **"Expired factory mode"** in CFP and it would not boot; RPMB is keyed
and monotonic, so that is irreversible.

- The factory gate (CRC-only verification path) is therefore **RPMB-gated**, not
  an NVRAM value you can set.
- **Write path correction:** the record is writable from the booted OS as root
  via **`/dev/rpmb`** (a named-record RPMB interface), not only from
  CFP/secure tooling. The user wrote `FF FF FF FF` this way over SSH. The
  secure-service MAC path is the OS's own RPMB driver, so a rooted OS is an
  authorized writer. The factory/CRC path is therefore a realistic userland
  route; the gate is the record value plus the CRC chain, not a key we lack.
- The catch is asymmetric: a bad value prevents the OS from booting, which
  removes the writer, and RPMB is monotonic. The `FF FF FF FF` unit is the
  proof.
- Two record paths in the stage: plain NVRAM (`FUN_8000A9D8`/`0xFE`, used for
  `0x2074`/`0x2040`/`0x2004`) and RPMB (`0x800090ce → 0x80011690 →
  0x80010a34(...,0xE0,...) → FUN_80018bb4(...,0xE)`).
- The RPMB path is now resolved statically: `0x80011690` looks up a 0x1c-byte
  descriptor by 14-byte name in `*0x80011530 = 0x8002C760` (count via
  `*0x80011534 = 0x80025FB0 + 4`; `+0x12` = RPMB address, `+0x16` = length,
  `+0x18` = offset). `0x80010a34` reads a 512-byte frame with a 16-byte nonce
  from secure service **0x12**, a result field at `+0x1FC`, and a 32-byte MAC
  at `+0xC4`; `0x800107b6 → 0x80017758` computes the MAC through secure service **0x2E**
  and the caller compares it, and the write-back path (`0x80011048`) uses **0x33** (load 16-byte
  key) then **0x2F** (authenticated write) with a write-counter check via
  **0x31**. All go through `0x800183dc → 0x80009078` = ARM `smc #0`,
  `r6=0xFF`, `r12=0` (the `65` SAR/monitor ABI). The 14-byte tag compare is
  only a name check; the key is not in the static image (the 0x2E request
  carries a runtime 32-byte key/context pointer at `0x80033760`).
- Accepted value is **1..50 inclusive**: `0` and `>50` both return failure
  status 2 (the `0x80009100`–`0x80009114` range check).
- `0x800090a0(1)` **provisions** the record with value `0x32` (50) via
  `0x800115e6("BOOTCTR_FCTRY", &0x32, 4)` — the path taken after
  "RPMB not provisioned". `0x800090a0(0)` deletes it via `0x8001167e`. This
  stage never decrements it: `0x800115e6` has one caller (`0x800090c0`) and
  `0x8001167e` one (`0x800090a6`); the budget is consumed by factory/CFP.
- Backing store is a packed block: 0x18-byte record header, magic
  `0x5983BC2D`, 14-byte name, u16 length at `+0x16`, data at `+0x18`;
  `0x80011166` find, `0x8001124e` repack, `0x80010B2E` write-back.
- The record table/buffers (`0x8002C760`, `0x8002A4E0`, `0x80033F80`,
  `0x80033760`) are above the 0x20000-byte file image — runtime/secure-shared.
- **QNX side:** `playbook-dev/dumped4869ifs/proc/boot/rpmb-omap4` is the
  `/dev/rpmb` resmgr (read/write verify, record catalogue, metadata/block
  check, nonce) and derives the RPMB key from a hardware KEK
  (`RPMB key util: RPMB key gen with KEK %d`, `select_kek`, `choose_hw_kek`).
  Records are mutable; the counter blocks replay, not a new value.
  `trustzone-omap4` contains `secp521r1_b`/`secp521r1_ma`, `SHA512_IV`, the
  `husw_SHA512*` family, ECDH and `/dev/rpmb/BSIS_PRIV` — so the curve is
  **P-521**, and RPMB is the OS's KEK-protected key store too.
- **Loader side:** `loader_06001A06-00.bin` has the same record writer
  (`0x80113D9C`, "Fail to write RPMB") and a BOOTCTR_FCTRY reader
  (`0x8011E074`; opcode **0xDE** -> response **0xFA**). It also has a
  decrement (`0x8011E1EC`) and a set-value-<0x33 (`0x8011E250`) helper, but
  this build has no caller for either — a pre-OS factory write needs cfp's
  RAM agent (or another loader build).
- The CRC shortcut is a factory-unit-only path, not a product bypass. The
  remaining product-path surfaces are the ECDSA enforcement (key pointer/base)
  and the RPMB **reader** parser.

## 5. Next work, in value order (post-71)

Items 1-4 of the original handoff are closed by `71` (mode map, CRC vs
public-key split, trailer, `0x2004`/`0x2040`). The curve is now confirmed as
**secp521r1 (P-521)** by the QNX secure-world binary `trustzone-omap4`, and the
RPMB store is the `rpmb-omap4` resmgr with a KEK-derived key. Remaining:

1. **Resolve the key-pointer/base anomaly (top).** `0x800187d0 → 0x800143d9`
   and `*(0x8001b188) → 0x800144e8` are not key data. Decode the `0x0` MLO
   records and the `0x1D95E` table; check for a load-time relocation; if that
   fails, read `0x800143D0`–`0x800144F0`/`0x800187D0` live during the boot
   window. Until this is settled, "ECDSA enforced" is unproven.
2. **Recover the expired unit via a pre-OS writer.** The CFP side is now
   decoded: `RESETQNXOSREGION` only clears NVRAM `0x2074` (logs "QNX OSREGION
   NVRAM record cleared"), not the counter. The boot-count setter is
   `FUN_0044d400`/cfp-old `0x48BAE0`, sending loader opcode **0xDE** with
   `{action=2, count}`; no CLI command calls it. Run `RESETQNXOSREGION` first
   (safe), then patch a copy of cfp-old or use a debugger to call
   `0x48BAE0(this,0,0,&count,1)` with `count=50`. The standard loader also has
   the record writer and set/decrement helpers (`0x80113D9C`, `0x8011E250`,
   `0x8011E1EC`) but no exposed caller; cfp's agent is the device-side writer.
3. **Validate a valid rewrite on a sacrificial unit.** On a booted unit, read
   `/dev/rpmb` first (enumerate records, current value, devctl write
   semantics), then write 1..50 and reboot. `rpmb-omap4` supports record
   writes; the RPMB counter blocks replay, not a new value. If the bootloader
   flips to FACTORY, the CRC-only path is live.
4. **Image side.** Once the factory path is confirmed: a region header with
   `region[0xd4] == 6` and a CRC-32 trailer over `[start, end-4)`.
5. **RPMB read parser audit** (`0x800090ce`, `0x80011690`, `0x80010a34`,
   `0x800107b6`, `0x80017758`): the 14-byte name `memcmp` is not the boundary;
   the secure service **0x2E** MAC is. Look for nonce/length/offset confusion.
6. TOCTOU (needs a concurrent writer) and the `NvOSRegionStruct` size/mask
   issues stay lower priority.
7. If the bootloader path dead-ends, kexec remains the route. The SAR/monitor
   ABI and the bootrom/loader path are published at
   `debian-dev:~/playbook-dev/bootrom-findings-2026-10-20/`.

## 6. Safety / state

- **RPMB writes are irreversible and secure-world-keyed.** `BOOTCTR_FCTRY` is
  an RPMB file (1..50); `FF FF FF FF` produced "Expired factory mode" and the
  unit would not boot. Keep the working unit off-limits. If the factory path is
  pursued, it is a sacrificial-unit experiment: capture the current record
  read-only, establish the `0x33`/`0x2F`/`0x31` service path, and remember the
  budget is finite and consumed by factory/CFP.
- **No NVRAM/flash/boot-chain writes this session.** NVRAM writes remain the
  known brick path (`0xC0`, `0xE0`, `0xE8`, `0xDA`); do not use `0xC8`, `0xC6`,
  `0xD4`, `0xD5`, `0xE8`, `0xEE`, `0xF3`, `0xFD`, or CREAD without a recovery
  plan.
- The normal RPMB writer (`/dev/rpmb`) needs a booted OS. An expired unit can
  only be recovered if CFP or the bootloader/ramloader RPMB path can rewrite the
  record; the ramloader has RPMB code (`Fail_to_write_RPMB` strings) and runs
  before the OS, so that is the recovery lead to check.
- The device may be left in bootrom or loader mode after a run. Power-cycle it
  to return to the OS. The loader survives idle for minutes but goes stale.
- The Sachesi harness and the probe are read-only by construction; keep it that
  way.

## 7. Suggested prompt for the next session

> Read `bootdumps-2026-09-11/analysis/HANDOFF-2026-10-20-next-session.md` first,
> then `71-hd0-mode-map-and-verifier.md` (including its RPMB addendum), `70`,
> and `HANDOFF.md`. Continue the RIM bootloader thread: (1) resolve the
> key-pointer/base anomaly (`0x800187d0 → 0x800143d9`, `*(0x8001b188) →
> 0x800144e8`) by decoding the MLO header/section table, checking for a
> load-time relocation, and dumping the full hd0 beyond `0x200000`; (2) audit
> the RPMB read path (`0x800090ce`, `0x80011690`, `0x80010a34`,
> `0x800107b6`/`0x80017758`, and its secure services 0x12/0x2E/0x2F/0x31/0x33
> via the `0x80009078` SMC wrapper) for a MAC/nonce/format bypass; the 14-byte
> tag compare is not the security boundary (the secure service 0x2E MAC is).
> `BOOTCTR_FCTRY` is an RPMB file (value 1..50; `FF FF FF FF` gives "Expired
> factory mode"). The standard loader reads it via opcode 0xDE; recover the
> expired unit by extracting cfp's RAM agent and finding the factory-counter
> write, or validate a rewrite on a sacrificial unit via `/dev/rpmb` first.
> `trustzone-omap4` confirms secp521r1/P-521. Do not write NVRAM or flash;
> CREAD stays RSA-gated.
