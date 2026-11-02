# 85 — Native-boot plan: CRC image + A/B staging + RPMB factory gate

Status: strategic plan, 2026-10-25. Static/live analysis only; no device writes
in this session. Supersedes the "next steps" prose in the handoff; the evidence
is in `75`–`84`.

## 0. Current state (important)

- **Unit inventory (2026-10-25):** two working **64 GB** units — the connected
  reference (running 2.0.0.4869, rooted from boot) and one spare for testing;
  two **bricked 32 GB** units (NVRAM/RPMB damage from earlier tinkering). The
  32 GB units are recovery targets, not test beds.
- **Running OS on the connected unit: 2.0.0.4869**, downgraded from
  2.1.0.1917. 4869 is vulnerable; the user's exploit gives **root SSH straight
  from boot**. `/dev/rpmb` writes worked historically on a 32 GB unit, but
  that unit is now bricked; the spare 64 GB unit is the first test bed.
- **`0x2801` is already open.** Live `A=+0x18=0`, `B=+0x1c=10`; the gate
  returns 10 and the bootloader is in the `0x80014A1E` retry branch. Do **not**
  patch it. The unlock password is `0000`; its verifier is proven in `84`.
- **`0x2074` is the A/B region/update record**, an `OsRegionV3` (72 bytes):
  `+0x04 requested_region`, `+0x05 actual_region`, `+0x06 boot_count_0`,
  `+0x08 boot_count_1`, `+0x0A max_boot_count`, `+0x0C flags`, `+0x10 epoch`,
  `+0x18`/`+0x30` 24-byte version IDs. Live: requested=0, actual=0,
  boot_count_0=0x017C, max=0. On-media regions are 0/1; `hd1`/`hd2` are the
  1-based drive names. It is **not** a signature bypass.
- **Active IFS header** (`hd1` offset `0x100000`, first 0x200 bytes):
  load address `0x800FF800`, entry `0x803BF610`, cookie `0xD7A82D1F`, and
  **byte `+0xd4 = 0`**. The current product image is therefore **not
  CRC-eligible** (`region[0xd4] & 0xff != 6`). The CRC path requires a modified
  image; setting `BOOTCTR_FCTRY` alone would make the bootloader take CRC and
  fail on the existing image.
- **Inactive region (`hd2`) is all zero at the IFS offset**; it is free for
  staging.
- `0x2002` = hardware PIN `0x50175127`; irrelevant to the boot path.
- `0x2819`/downgrade is **already done**; no further work needed there.

## 1. What the bootloader actually checks

```
0x80014904 boot main
0x80014974 read 0x2040 (NukeStatus; absent -> 2)
0x8001498A tbb: 0/2 -> gate, 1 -> nuke, 3..8 -> normal, >=9 -> nuke
0x80014998 bl 0x80014F84        ; 0x2801 password-attempt budget
0x8001499E bne 0x80014A1E       ; live A=0 -> retry branch
0x800149BA bl 0x80014F0C        ; normal path only: A = max(A,10)
0x800149BE bl 0x8000911A        ; BOOTCTR_FCTRY selector
0x800149C2 cbnz r0, 0x80014A16  ; normal path: any nonzero -> nuke
0x800149C4 product verifier     ; SHA-512 + secp521r1
```

Retry loop `0x80014A1E` -> `0x8001453C` (flag=1) -> driver `0x800142A8` ->
selector `0x8000911A`:

| selector | path |
|---|---|
| 0 | ECDSA (`0x80014716` -> `0x80018688`, SHA-512 + secp521r1) |
| 1 | CRC gate `0x80013FB6` (requires `region[0xd4] & 0xff == 6`) -> `0x80014788` CRC-32 over `[start,end-4)` vs `end-4` |
| 2 | status `0x0f`, fail |

The driver verifies the **requested** region first and falls back to the other
region; `boot_count_*`/`max_boot_count` gate the attempts. `switch()` sets
requested=`actual^1`, counts 0, max 4; `sane()` sets requested=actual, max 0
(unlimited). The bootloader writes `actual` after a successful verify.

**Consequence:** the CRC path is the forgeable route. The ECDSA path is the
hard one (P-521 key; no private key). The CRC path is gated only by
`BOOTCTR_FCTRY` (RPMB, authenticated) and `region[0xd4] == 6` + a valid CRC-32
trailer. The live image fails the second condition; a modified image can fix it.

## 2. Plan

### Phase 0 — region/image audit (read-only)

Dump both regions and parse the IFS:

- Region 0 = `/dev/hd1`, region 1 = `/dev/hd2`.
- Offsets per region (`winchester.py`): MBR `0x0`, SIG `0xF0000` (64-byte
  fingerprint), IFS `0x100000`, RFS `0xC00000`, END `0x20000000`.
- Read the IFS header at `0x100000`; find the total image size/end, the ECDSA
  trailer (`end-0x230`), and the last 4 bytes (CRC). Confirm `+0xd4` on both
  regions.
- The live `hd1` header is already captured:
  `analysis/playbook-live-2026-10-21/`; `hd2` is zero at `0x100000`.

### Phase 1 — safe A/B switch test (no RPMB, no image write)

Goal: prove the `0x2074` staging/fallback without touching the factory
counter.

1. Set `requested_region=1`, `actual_region=0`, `boot_count_0=0`,
   `boot_count_1=0`, `max_boot_count=0` (unlimited), `epoch=now`. Use
   `deckard` `Platform.switch()` then patch `max_boot_count=0`, or patch the
   record directly with `nvram-editor`; read back.
2. Reboot. Region 1 is empty, so the bootloader should fail it, try region 0,
   and boot normally. That proves the requested/actual/fallback path.
3. After boot, restore the sane state: `requested=actual=0`, counts 0, max 0
   (`Platform.sane()` or `nvram patch`). Do not leave `max=4` for a long test;
   the budget can expire after four fallback boots.

Risk: if the bootloader does not fall back, the unit may hang; the original
region 0 is intact and the bootrom/loader remains the recovery path.

### Phase 2 — stage a copy of the active region

Copy the active IFS/RFS into region 1 (`inactive_ifs()`/`inactive_rfs()`, or a
raw `pread/pwrite` at the offsets above), set `requested=1`, max=0, counts=0,
reboot. If the copy verifies (ECDSA), region 1 boots; if not, region 0
falls back. This proves the region write path and the image copy size/format.
**No factory counter involved.**

### Phase 3 — build CRC-valid images

The live image has `+0xd4=0`; make two CRC-valid images before touching RPMB:

1. **Modified image (region 1):** take the active IFS, apply the payload change
   (later Linux), set byte `+0xd4=6`, recompute the CRC-32/ISO-HDLC trailer over
   `[start, end-4)` and write it at `end-4`.
2. **Original-image CRC fallback (region 0):** take the unmodified active IFS,
   set `+0xd4=6`, recompute the CRC trailer. Its ECDSA signature becomes
   invalid, but the CRC path does not use it. This is the safety net: the
   selector is global, so once `BOOTCTR_FCTRY` is 1..50 both regions use CRC.
3. Verify both images with a host-side CRC-32 script and the bootloader's
   `0x80014788` logic. Keep the original ECDSA region 0 image backed up
   off-device.

Do not set the factory counter until both regions are CRC-valid.

### Phase 4 — write the factory counter

Use the **spare working 64 GB unit** as the first test bed (same model/loader
family as the connected unit; verify with `info`). Keep the connected 64 GB
unit as the reference. The two 32 GB units are bricked and are not test beds.

1. Full backup: NVRAM records, RPMB, region 0/1 images, bootrom/loader access.
2. Stage the two CRC-valid images (Phase 3).
3. Write `BOOTCTR_FCTRY` 1..50 via the booted OS `/dev/rpmb` (the rooted
   2.0.0.4869 OS; historically proven on a 32 GB unit that is now bricked).
   Read it back with the loader `0xDE` action 0 or the
   RPMB tooling; the bootloader must see 1..50.
4. Reboot. The retry loop (already active because `0x2801 A=0`) reads selector
   1 -> CRC. It tries region 1 (modified); if the CRC verifies, it boots our
   image. If not, it tries region 0 (CRC-valid original) and boots the
   fallback.
5. If both fail, the spare 64 GB unit is the sacrificial unit; the connected
   64 GB reference is untouched. Recover via the bootrom/loader or re-flash
   path.

### Phase 5 — Linux payload

Once a CRC image boots, replace its payload with a Linux first-stage:

- The bootloader loads the IFS at `0x800FF800`, entry `0x803BF610`, and jumps
  through the `0x80014A90` enter-OS path. The CRC image only needs a valid
  header (`+0xd4=6`) and CRC trailer; the payload can be a minimal ARM
  chainloader that sets up the CPU/MMU/caches/memory map and jumps to the Linux
  kernel.
- Start from the QNX IFS header/startup layout we dumped; replace the startup
  code, keep the header/trailer fields, recompute the CRC. The Linux kernel and
  DTB are placed at known addresses; the first-stage passes the boot protocol
  state the kernel expects.
- Longer term, a kexec-style payload from a rooted OS remains the fallback if
  the bootloader path stalls.

### Phase 6 — if the CRC path fails

The new evidence narrows the bypass surfaces:

- **RPMB read parser** (`0x80011690` -> `0x80010A34`: 512-byte block, nonce,
  32-byte MAC, length/offset u16): a parser bug that lets a forged record
  return 1. The MAC is the gate; a pure metadata rewrite is not enough.
- **ECDSA parser/verifier** (`0x8001AFEC` SEC1 point parser,
  `0x8001B194`/`0x8001B280`): missing on-curve/`r,s` range checks, hash
  truncation, length confusion.
- **TOCTOU**: the verifier hashes `[start,end-0x230)` in `0x4B00` chunks and the
  retry loop polls host commands (`0x80012516` -> `0x80012182`); no
  freeze/remap between hash and jump.
- **Region selection** (`0x2074` `&1`): a verified-vs-booted mismatch or
  counter/index bug.

The `0x2819` downgrade route is already exploited; no work needed there.

### Phase 7 — bricked 32 GB units / Cellebrite loader lead

The two 32 GB units are bricked by NVRAM/RPMB damage; they need eMMC
desoldering/programming or an arbitrary-read/write pre-OS loader.

Cellebrite material (`cellebrite-blackberry/`):

- `BlackBerry OS cellebrite exploit - wumbomumbo.txt`: the BBOS exploit uses
  `WRITE_RAM_SETUP`/`WRITE_RAM`/`WRITE_RAM_VERIFY` to write a patched loader,
  a payload, and the original loader at a third address, then executes the
  original. BBOS bootrom address checks are lax. `messages.txt` notes that
  **BB10 strictly checks address parameters**, so the BBOS exploit does not
  work on BB10/PlayBook as-is.
- The UFED pack contains **signed** loaders, not the patched exploit. Its
  `ufedblackberrypack.epr/06001A06_0000000A` is a **signed PlayBook loader**
  (`D7D32D1F` at +4, `D7C82D1F` at end-8, load `0x80100000`, 159,968 bytes,
  unpatched — no `DB9F07EB`) with the same strings as `LDR_01`
  (`BOOTCTR_FCTRY`, `Fail to write RPMB`, `Password record not found`).
  `05001A06_0000000A` is a Qualcomm loader (load `0x02000008`). See `86`.
- Audit the `06001A06` loader against `LDR_01` for command-handler bugs
  (CREAD/write/erase/READVERIFY/0xE5/0x39/0x3A), and test it read-only on the
  spare 64 GB unit through the bootrom. If it exposes arbitrary eMMC
  read/write, it is a path to repair the 32 GB units' RPMB/NVRAM without
  desoldering. The BB10 bootrom's `0xF009`/`0xF004`/`0xF00A` handlers are in
  the hidden page/secure world; the signed loader's handlers are the more
  promising surface.

## 3. Risks / decision points

- **Do not write `BOOTCTR_FCTRY` while region 0 is ECDSA-only**
  (`+0xd4=0`). Once the selector is 1, both regions use CRC; without a
  CRC-valid fallback the unit can hang. Make both regions CRC-valid first.
- **Budget:** `switch()` sets `max_boot_count=4`; a failed staged image can
  exhaust the old region's four fallback boots. Use `max=0` for tests and
  restore `sane()` after.
- **Backups:** full NVRAM/RPMB/region images before any write; the spare 64 GB
  unit first, the connected 64 GB reference only after the recipe is proven.
- **`0x2801`:** already `A=0`/`B=10`; do not patch. The password/gate is not
  the blocker.
- **`0x2002`/PIN:** irrelevant; do not touch.
- **The 0x2074 record is not a bypass:** it stages/selects and rolls back; the
  verifier still runs.

## 4. Offsets and APIs (quick reference)

```
Region 0 = /dev/hd1, region 1 = /dev/hd2
MBR 0x0 | SIG 0xF0000 | IFS 0x100000 | RFS 0xC00000 | END 0x20000000
active_ifs()/inactive_ifs()  -> IFS zone
active_rfs()/inactive_rfs()  -> hdXt179
Platform.switch()  -> requested=actual^1, counts 0, max 4, epoch=now
Platform.sane()    -> requested=actual, max 0
osregion.py:206    -> NV_OSSTORE_OS_REGION = 8308 = 0x2074
osregion.py:453    -> OsRegionV3 = '=IBBHHHIQ24s24s', size 72
```

Live `0x2074` (V3): `version=3`, requested=0, actual=0, boot_count_0=0x017C,
boot_count_1=0, max=0, flags=0, epoch=0, version IDs empty.

Live active IFS header (`hd1+0x100000`, first 0x40): `0xEA0001FE`,
`0x800FF800`, `0x803BF610`, `0`, `0`, `0`, `0`, `0`, `0xD7A82D1F`,
`0x800FF800`, `0x800FF82C`, `0x00030012`, `0x000001CC`, `0x800FF804`,
`0x800FF808`; `+0xd4 = 0`; `hd2` same offset is zero.

## 5. Parallel workstreams (subagents)

Use subagents for the independent read-only tracks; keep the **single live
device and all writes** in the main session. Copy-paste prompts and report
paths are in `87-next-session-subagents.md`.

- **A — Cellebrite/ramloader audit** (`88`): signed `06001A06` loader vs
  `LDR_01`, arbitrary eMMC read/write, bricked-32 GB recovery.
- **B — IFS/region/CRC recipe** (`89`): image size/end, `+0xd4`, ECDSA/CRC
  trailers, CRC-32 builder/verifier.
- **C — A/B switch dry run** (`90`): exact `0x2074`/`Platform.switch()`
  sequence, fallback, backup/recovery.
- **D — Linux entry state** (`91`): IFS entry `0x803BF610`, `0x80014A90`
  handoff, minimal chainloader.
- **E — RPMB factory write runbook** (`92`): `/dev/rpmb` read/write/verify and
  risk checklist.

Launch A, B, C in parallel first; D and E while they run. The main session
collates the reports, performs the live read-only audit, and executes Phase 1/2
only after A–C are reviewed.
