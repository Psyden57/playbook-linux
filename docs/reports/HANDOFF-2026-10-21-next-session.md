# Session handoff — 2026-10-21 (final) → next session

Read this first, then `71-hd0-mode-map-and-verifier.md`,
`72-loader-bootmode-action2.md`, `73-mlo-ecdsa-key-pointer.md`,
`74-nvram-editor-and-0x2801.md`, `75-nvram-2801-password-record.md`,
`76-live-0x2801-audit.md`, `77-nvram-resmgr-id-map.md`,
`78-libnvram-analysis.md`, `79-hd0-2801-crosscheck.md`,
`83-osregion-semantics.md`, `84-password-vs-pin.md`,
`85-linux-boot-plan.md`, `86-cellebrite-loader-lead.md`,
`87-next-session-subagents.md`, `69`, `70`,
`HANDOFF.md`, `analysis/ramloader-commands.txt`, the `bb10mt-main/` sources,
and the decompiled deckard tree in `analysis/deckard-2.1.0.1917/`
(`platform/osregion.py` is the `0x2074` reference). The
long-term goal is unchanged: **boot Linux (or a modified TabletOS) on the
PlayBook**. kexec is the working route; the bootloader thread remains
research.

## 0. TL;DR — what changed in this session

- **ECDSA product path: RESOLVED.** `hd0head.bin` is a **pre-relocation**
  image: linked at `0x7FFF7000`, loaded at `0x80000000`, with a load-time
  `+0x9000` fixup. The four verifier literals resolve exactly to the secp521r1
  key and curve constants:
  - `0x800143D9` -> `0x8001D3D9` valid uncompressed P-521 public key (on curve)
  - `0x800144E8` -> `0x8001D4E8` `p = 2^521 - 1`
  - `0x800144A4` -> `0x8001D4A4` secp521r1 `b` (exact)
  - `0x80014460` -> `0x8001D460` `3` (the `a = -3` term)
  There is **no key-pointer anomaly**. Verified numerically by
  `analysis/loader-audit-2026-10-21/check_reloc.py`; see `73`.
- **`loaders.dat` audit: CLOSED.** 82 declared entries, 81 recoverable
  (`LDR_00` is corrupt from a `TMyArcWriter` header-overwrite bug). Only
  `LDR_01` is the PlayBook loader (`06001A06` = `loader_06001A06-00.bin`).
  **No recoverable loader implements `0xDE` action 2**; every handler accepts
  action 0 only, and the OMAP setters/decrements are unreachable dead code.
  See `72`.
- **Device reads.** The working unit boots and reports
  `mode=product, count=0`, consistent with the now-functional ECDSA product
  path. The expired unit also *reports* `product/0`, but that is `LDR_01`'s
  reader masking `0xFFFFFFFF`; its bootloader reader returns `2` and still
  nukes (`72` §3.1).
- **Tooling.** `bbusb.py` now has a working BOOT_MODE CLI, the bootrom
  password challenge (`--password`), and `--debug`. `bb10mt` has a `bootmode`
  command and an `info` line. PyUSB 1.3.1 is installed user-local.
- **NVRAM `0x2801` identity.** It is `NV_OSSTORE_PasswordStruct` (`0x2802` is
  its reserve), not a time/boot-info record: `+0x18` is the password-attempt
  counter, `+0x1c` the clamped maximum (3..10). `+0x18 < +0x1c` selects the
  retry/CRC branch. The pre-OS writers are in the boot stage; the shipped
  ramloader's are dead, and the booted OS writes it through `/dev/nvram`.
  `0x2802` is not a success-path mirror. See `75`.
- **Live NVRAM audit (2026-10-21).** `0x2801` reads `A=+0x18=0`,
  `B=+0x1c=10`, so `FUN_80014f84` returns 10 and the working unit is
  **already** in the retry/ECDSA branch. `0x2040` and `0x2004` are absent;
  `0x2802` is all-zero (not a mirror); `0x2074 +6` advanced
  `0x017B -> 0x017C` across the reboot. The `patch +0x18 = 0` test is
  unnecessary. See `76`.
- **NVRAM ID map proven (`77`/`78`/`83`/`84`).** `0x2801` is
  `NV_OSSTORE_PasswordStruct`; the **hardware PIN is a different record,
  `0x2002` `NV_OSSTORE_PIN_NUM`** (4/8 bytes, protected, live `0x50175127` =
  the bootrom `0xE7` PIN). `0x2074` is the **OS A/B region/update record**
  (`requested`/`actual` 0/1 at `+0x04`/`+0x05`, per-region boot counts at
  `+0x06`/`+0x08`, max at `+0x0A`, epoch, version IDs), not a boot or factory
  counter. `0x2004` is `NV_OSSTORE_JvmStart`, `0x2040` is
  `NV_OSSTORE_NukeStatus`. The resmgr never computes or checks the password
  digest; the `hd0` stage does. The editor's bundled `libnvram` is
  **BB10.0.9.388**, not TabletOS 2.x; the stock 5740-byte
  `/base/lib/libnvram.so.1` lacks `readOSMetrics`/`readBootromMetrics`, which
  is the editor's `Symbol not found`. The `/proc/boot` symlink is
  RAM/temporary. See `77`, `78`, `79`, `83`.
- **Password verifier proven (`84`).** The live `0x2801` 64-byte digest is
  reproduced exactly by `state₀ = "0000"; 5060 × state =
  SHA-512(LE32(counter) ‖ salt ‖ state)` with `salt = +0x10..+0x17`. The
  bootrom `0x0A`/`0x0F` exchange is built from this record (same salt/5060;
  the answer equals `HashPassV2("0000", challenge, salt, 5060)`). The
  plaintext is not stored; `0x2002` is the hardware PIN, not the unlock code.
- **Native-boot plan (`85`).** Running OS is **2.0.0.4869** (downgraded from
  2.1.0.1917) with root from boot. The **spare working 64 GB unit** is the
  first destructive test bed; the connected 64 GB unit is the reference. The
  live active IFS has **`region[0xd4] = 0`**, so the current image is not
  CRC-eligible; the inactive `hd2` is zero at the IFS offset. Plan: stage
  CRC-valid images in **both** A/B regions, write `BOOTCTR_FCTRY` 1..50 from
  the rooted OS, reboot (retry loop selector 1 -> CRC), then replace the
  payload with Linux. Do not set the counter until both regions are CRC-valid.
  See `85`.
- **Bricked 32 GB units / Cellebrite lead (`86`).** The UFED pack contains a
  **signed PlayBook loader** `06001A06_0000000A` (`D7D32D1F`, load
  `0x80100000`, 159,968 bytes, unpatched) with the same RPMB/password strings
  as `LDR_01`; `05001A06` is a Qualcomm loader. Audit it for arbitrary eMMC
  read/write to repair the bricked units. The BBOS three-address bootrom
  exploit does not apply to BB10 (strict address checks).

## 1. Device / access state

- **Unit inventory (2026-10-25):** two working **64 GB** units — the connected
  reference (`modelID 0x06001A06`, `BR_ver 0x051B0014`, device password
  **`0000`**, `0xDE` read `status=0, mode=product, count=0`) and one spare
  test bed. Two **bricked 32 GB** units from NVRAM/RPMB tinkering; one is the
  previously described expired unit (`BOOTCTR_FCTRY = 0xFFFFFFFF`, no
  password, `0xDE` masked sentinel, bootloader nukes). The 32 GB units are
  recovery targets, not test beds.
- **Tooling caveat:** the bootrom can get stuck after a failed attempt. The
  Python flow retries once with a reboot; if it still fails, power-cycle the
  unit. The loader itself has a host-activity timeout, so do the upload and the
  command in one run.
- `CFP.exe` 3.10.0.22 / `cfp-old.exe` 3.9.0.165 are present; Wine runs them but
  they cannot connect (missing `BlackBerry Device Manager` COM class). No
  Windows VM confirmed.
- REA 6.7.0 + Ghidra 12.1.4 are configured. Prior artifacts:
  `analysis/hd0-modemap-2026-10-20/` (`cfp-*.c`, `loader-rpmb*.txt`,
  `verifier-decompiled.txt`) and `analysis/loader-audit-2026-10-21/`.
- **`nvram-editor/`** (workspace root) has an ARM QNX `nvram` binary + source.
  It dlopens `/proc/boot/libnvram.so.1` and can read/write any NVRAM record
  from the **booted** OS. It is the live counterpart to the static
  `0x2801`/`0x2074` work and is only usable on the working unit.

## 2. `0xDE` BOOT_MODE — protocol and the loader situation

Request: channel 2, cmd `0xDE`, 8-byte `{u32 action, u32 value}`.
Response: cmd `0xFA`, 12-byte `{u32 status, u32 mode, u32 count}`.
Actions: `0` read, `1` switch (1 = FACTORY, 0 = PRODUCT), `2` set count
(1..50). Status: 0 ok, 1 not allowed, 2 out of range, 3 wrong device mode.

- **No shipped loader implements action 1/2.** The `0xDE` cases in
  `LDR_01, 13, 14, 15, 22, 24, 28, 29, 44, 47, 49` all log and return without
  a response for any nonzero action. The host sees a timeout.
- OMAP builds contain the `value <= 0x32` setter and a decrement helper; both
  have zero callers and zero pointer references. Qualcomm builds have only an
  unreferenced decrement.
- **Sentinel discrepancy:** `FUN_8011e074` in `LDR_01` (the PlayBook/CFP
  image) treats `0xFFFFFFFF` as `0` ("record absent"), observed on the expired
  unit. The newer OMAP readers (e.g. `FUN_8011ef78`) and the bootloader reader
  (`0x800090CE`) have no such case and return `2`. Do not use the `LDR_01` read
  to infer the selector for `0xFFFFFFFF`.

## 3. Boot flow / factory counter (unchanged, still the main blocker)

`0x80014904`:
```
0x80014998  bl FUN_80014f84    ; NVRAM 0x2801 password-attempt budget
0x8001499e  bne 0x80014a1e     ; nonzero -> retry/CRC loop
...
0x800149be  bl FUN_8000911a    ; BOOTCTR_FCTRY: 0 product, 1 factory, 2 bad
0x800149c2  cbnz r0, 0x80014a16 ; ANY nonzero -> nuke
0x800149c4  bl 0x80014548      ; product verifier (now known functional)
0x800149d0  cmp r0,#1
0x800149d2  bne 0x80014a16     ; not verified -> nuke
```
A valid factory record alone does **not** select CRC: on the normal path any
nonzero selector nukes. CRC is reached only through the `0x2801 != 0` recovery
branch. **Do not write `BOOTCTR_FCTRY` on a working unit without understanding
`0x2801` and taking a full backup.**

## 4. RPMB / secure storage (prior-session context)

`rpmb-omap4` is `/dev/rpmb`; it derives the RPMB key from a hardware KEK.
The bootloader record read (`0x80011690` -> `0x80010A34` -> `0x800107B6` ->
`0x80017758`) is authenticated through the secure monitor (`0x800183DC` ->
`0x80009078`, ARM `smc #0`, `r6=0xFF`, `r12=0`), services **0x12** (nonce),
**0x2E** (MAC), **0x2F** (authenticated write), **0x31** (write counter),
**0x33** (key load). `BOOTCTR_FCTRY` is `1..0x32`; provisioning writes `0x32`.
The 32-byte MAC is the gate, not the 14-byte name compare. The bootloader's
`0x800090a0(1)` can provision an **absent** record at `0x32`, but it does not
fire for `0xFFFFFFFF` (the bootloader reader returns `2`, not "not found"), so
it does not help the expired unit. The general writer is `/dev/rpmb` on a
booted OS unit.

CFP's ramimage is the same `loader_06001A06-00.bin` (confirmed by the user);
its `0xDE` handler is read-only. `RESETQNXOSREGION` clears NVRAM `0x2074`,
not the RPMB counter. CFP.exe `FUN_0044d400` (wrapper `0x420ef0`) and cfp-old
`0x48BAE0` build the BOOT_MODE request; no CLI command invokes them. The
standard loader never writes the counter.

## 5. Tooling

- `bbusb/bbusb.py` (PyUSB; installed user-local as `pyusb 1.3.1`):
  ```
  cd ~/rea-work/bbusb
  python3 bbusb.py --boot-mode --from-bootrom [--password 0000] [--debug]
  ```
  - Uploads `loaders/loader_06001A06-00.bin` at the header's `0x80100000`
    with 260-byte chunks.
  - Follows the Pascal sequence: Ping0, GetVar(2), SetMode(1), password,
    SwitchChannel, GetMetrics, upload, run, wait `0x8001`, SetMode(2),
    password, BugdispLog, FlashRegionsInfo.
  - `--password` handles the `0x0E`/`0x0F` challenge (`hash_pass_v2()` port);
    omit it to be prompted. `--debug` prints raw bootrom responses.
  - `--factory`/`--product`/`--set-count` require `--force` and will time out
    on every shipped loader.
- `bb10mt-main`: `info` now prints the boot mode; `bootmode` command exposes
  the same protocol. `TBBLoader.BootMode` + `TBootModeInfo`.
- **NVRAM editor** (`nvram-editor/nvram`, ARM QNX; copy it to the unit). It
  can read/write any NVRAM record while the OS is booted. Read-only first:

  ```
  ./nvram get_bootrom_metrics > brom.bin
  ./nvram read 0x2004 hex
  ./nvram read 0x2040 hex
  ./nvram read 0x2074 hex
  ./nvram read 0x2801 hex
  ./nvram read 0x2802 hex
  ./nvram record_exists 0x2801
  ./nvram is_protected 0x2801
  ./nvram get_permissions 0x2801
  ```

  `0x2801` is 0x70 bytes and is `NV_OSSTORE_PasswordStruct` (`0x2802` is
  `PasswordStructReserve`; the stage writes it only when the primary write
  fails). Fields: `+0x18` attempt counter A, `+0x1c` maximum B clamped 3..10
  (`0x80014fc6`), `+0x20` sequence counter. `FUN_80014f84` returns 0 when
  `[+0x1c] < [+0x18]` (normal), else `[+0x1c] - [+0x18]`; nonzero takes the
  `0x80014a1e` retry/CRC loop. Setting `+0x18 = 0` forces that branch:
  `./nvram patch 0x2801 0x18 0x00000000`, then read back. Patching `0x2802`
  is optional; `A=0` is sticky until restored
  (`./nvram patch 0x2801 0x18 0x0a`).
  Read-only dumps first: `0x2004`, `0x2040`, `0x2074`, `0x2801`, `0x2802` are
  boot-critical. Field map, writers and test order:
  `74-nvram-editor-and-0x2801.md`, `75-nvram-2801-password-record.md`.
  **Live 2026-10-21:** editor installed at `/accounts/devuser/nvram` with
  `/proc/boot/libnvram.so.1` symlinked (RAM/temporary; re-create after every
  reboot); `0x2801` already reads `A=0`, `B=10` (gate 10), `0x2040`/`0x2004`
  absent, `0x2802` all-zero; the reboot kept `0x2801` byte-identical and
  advanced `0x2074 +6` to `0x017C`. The `+0x18 = 0` write is not needed on
  this unit (`76`).
  **Library provenance (`78`):** the bundled 12716-byte `libnvram.so.1` is
  BB10.0.9.388; the stock TabletOS `/base/lib/libnvram.so.1` is 5740 bytes and
  lacks `readOSMetrics`/`readBootromMetrics`, hence `Symbol not found`. The
  record devctl ABI is compatible. `get_bootrom_metrics` SIGSEGVs because the
  editor writes 0x4FC bytes from a 0x4E8-byte BB10 allocation (§2.9).
- The `loaders.dat` reader/writer bug is documented in `72`; `LDR_00` is
  unrecoverable in the current archive.

## 6. Next work, in value order

**Main active work: the native-boot plan in `85-linux-boot-plan.md`.** Use
subagents from the start for the parallel read-only tracks described in
`87-next-session-subagents.md`: **A** Cellebrite signed-loader audit (`88`),
**B** IFS/region/CRC recipe (`89`), **C** A/B switch dry run (`90`), **D**
Linux entry state (`91`), **E** RPMB factory write runbook (`92`). Launch
A/B/C in parallel first; the main session keeps the live device and all writes,
collates the reports, and executes Phase 1/2 only after A–C are reviewed.
Phase 0 region audit, Phase 1 safe empty-region A/B switch, Phase 2 copy the
active region into the inactive slot, Phase 3 CRC-valid images in both slots,
Phase 4 `BOOTCTR_FCTRY` on the **spare 64 GB unit**, Phase 5 Linux payload,
Phase 6 bypass research, Phase 7 / `86` Cellebrite signed-loader audit for the
bricked 32 GB units. Key live facts: active IFS `+0xd4=0` (not CRC-eligible),
`hd2` empty, `0x2801` A=0, and the rooted 2.0.0.4869 OS. Do **not** set the
factory counter until region 0 is CRC-valid. The 32 GB units are bricked
recovery targets, not test beds.

1. **NVRAM identity + live state: CLOSED.** `0x2801` is the device-password
   record (`NV_OSSTORE_PasswordStruct`), proven to verify the unlock code
   `0000`; it is not the hardware PIN (`0x2002`, live `0x50175127`) and not the
   OS region record (`0x2074`, an A/B update-state struct, see `83`). Live
   `A=0`/`B=10` already selects the retry/ECDSA branch, and the reboot kept it.
   The `patch +0x18 = 0` step is a no-op; do not write. The only remaining
   NVRAM-adjacent test is the factory/CRC one: it needs `BOOTCTR_FCTRY`
   (`/dev/rpmb`) and `region[0xd4] == 6`, is destructive, and needs an
   explicit decision and a full backup. Do not do it on the working unit.
2. **Expired-unit recovery.** There is no `0xDE` action-2 writer; the
   bootloader only provisions an **absent** record at `0x32` (not
   `0xFFFFFFFF`). The general writer needs a booted OS (`/dev/rpmb`). Remaining
   options: hardware eMMC/RPMB access, a memory-safety bug in the signed loader
   to reach the writer (`0x80113D9C`), or park the unit.
3. **Locate the `+0x9000` fixup table/agent** (low priority; verification is
   already closed). The code/keys are correct after relocation; the exact
   boot-ROM/first-stage fixup is an implementation detail.
4. Lower priority: RPMB read-parser audit (length/offset/nonce), TOCTOU (needs
   a concurrent writer), and the `NvOSRegionStruct` size / `0xff00ffff` mask
   audit.

## 7. Safety

- **No RPMB/flash writes on the working unit.** A valid factory counter is
  harmless only while the `0x2801` gate is nonzero (the live state); if the
  gate ever reaches 0 the normal path reads the counter and nukes on any
  nonzero selector. Treat any `BOOTCTR_FCTRY` write as destructive. Use the
  **spare working 64 GB unit** as the first test bed; keep the connected
  64 GB reference until the recipe is proven. The two 32 GB units are bricked
  and are recovery targets.
- **NVRAM writes are not needed for the current audit.** The editor is
  installed and read-only access works; `0x2801` already satisfies the
  recovery-branch condition. If a write is ever needed, dump and save
  `0x2004`, `0x2040`, `0x2074`, `0x2801`, `0x2802` (and bootrom metrics)
  off-device first. `0x2801` is boot-critical: a malformed write can prevent
  boot. The editor writes the insecure and secure copies in that order and
  stops early if the first fails; verify any write with a read-back.
  Remember `/proc/boot/libnvram.so.1` is a RAM symlink and must be re-created
  after every reboot. The active IFS has `region[0xd4]=0`, so the current
  image is not CRC-eligible; make **both** A/B regions CRC-valid before
  setting `BOOTCTR_FCTRY`.
- The two 32 GB units are bricked and cannot run the NVRAM editor. The spare
  working 64 GB unit is the OS-capable sacrificial test bed; the connected
  64 GB unit is the reference.
- Back up before any write. CREAD stays RSA-gated. The RPMB MAC key is not in
  the static images; do not assume it can be forged from the normal world.

## 8. Suggested prompt for the next session

> Read `bootdumps-2026-09-11/analysis/HANDOFF-2026-10-21-next-session.md`
> first, then `72-loader-bootmode-action2.md`,
> `73-mlo-ecdsa-key-pointer.md`, `74-nvram-editor-and-0x2801.md`,
> `75-nvram-2801-password-record.md`, `76-live-0x2801-audit.md`,
> `77-nvram-resmgr-id-map.md`, `78-libnvram-analysis.md`,
> `79-hd0-2801-crosscheck.md`, `83-osregion-semantics.md`,
> `84-password-vs-pin.md`, `85-linux-boot-plan.md`,
> `86-cellebrite-loader-lead.md`, `87-next-session-subagents.md`,
> `71` (§7b/§7c), `69`, `70`, `HANDOFF.md`,
> `analysis/ramloader-commands.txt`, and `bb10mt-main/ramloader.pas` /
> `uarchive.pas`. The ECDSA anomaly is resolved: `hd0head.bin` is
> pre-relocation linked at `0x7FFF7000`; add `0x9000` to absolute literals and
> the key/p/b are valid. The loader audit is closed: no recoverable
> `loaders.dat` entry implements `0xDE` action 2; the bootloader only
> provisions an absent record at `0x32`, and the general writer is `/dev/rpmb`
> on a booted OS. NVRAM `0x2801` is `NV_OSSTORE_PasswordStruct`; the live
> digest is exactly `SHA-512^5060(LE32(c) ‖ salt ‖ "0000")`, so it is the
> unlock-password verifier. It is **not** the hardware PIN (`0x2002`, live
> `0x50175127`) and **not** the OS region record (`0x2074`: requested/actual
> 0/1 at `+4/+5`, boot counts `+6/+8`, max `+0x0A`, epoch, version IDs). The
> `0x2801` fields `+0x18`/`+0x1c` are the attempt counter and clamped max
> (3..10), so `+0x18 < +0x1c` selects the `0x80014a1e` retry/CRC branch. The
> live unit already reads `A=0`/`B=10` and kept it across the reboot, so that
> branch is active and no NVRAM write is needed; `0x2040` and `0x2004` are
> absent and `0x2802` is not a mirror. The running OS is **2.0.0.4869**
> (downgraded, rooted from boot). Two **64 GB** units work: the connected
> reference and a spare test bed. Two **32 GB** units are bricked from
> NVRAM/RPMB tinkering; the Cellebrite UFED pack has a signed PlayBook loader
> `06001A06` (`86`) to audit for arbitrary eMMC read/write and their repair.
> The active IFS has `region[0xd4]=0`, so the current image is not
> CRC-eligible; the plan in `85` requires CRC-valid images in both A/B slots
> before `BOOTCTR_FCTRY`. `nvram-editor/` is installed on the unit
> at `/accounts/devuser/nvram`; its bundled libnvram is BB10.0.9.388 and the
> `/proc/boot/libnvram.so.1` symlink is RAM/temporary (re-create after each
> reboot). Do the read-only dumps first; any write
> needs a full backup. Do not write `BOOTCTR_FCTRY` on the working unit. The
> expired unit reads `product/0` only
> because `LDR_01` masks `0xFFFFFFFF`; its bootloader still nukes.
>
> **Use subagents aggressively.** Launch the read-only tracks in
> `87-next-session-subagents.md` in parallel — A (Cellebrite loader audit),
> B (IFS/CRC recipe), C (A/B switch runbook), D (Linux entry state), E (RPMB
> write runbook). Keep the live device and all writes in the main session;
> execute Phase 1/2 only after A–C are reviewed.
