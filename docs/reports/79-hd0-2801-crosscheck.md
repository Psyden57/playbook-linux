# 79 — hd0 `0x2801` gate cross-check (reconstructed after the subagent died)

Status: independent cross-check requested with the `77`/`78` pass. The
subagent assigned to this died early (permission/tool failure), so this is
reconstructed from the session's verified static pass plus the recovered
`77`/`78`. No device or NVRAM writes.
Date: 2026-10-21.
Target: `bootdumps-2026-09-11/hd0head.bin` (pre-relocation, runtime base
`0x80000000`; absolute literals need `+0x9000`, branch/call targets are
runtime-correct). Loader: `bbusb/loaders/loader_06001A06-00.bin` (base
`0x80100000`).

## 1. Verified addresses and bytes

All of the following were decoded in both modes and checked against the raw
bytes. The verifier region is Thumb; the crypto is ARM; no ARM island exists
between `0x80013FB6` and `0x800148AC`.

### 1.1 Boot main `0x80014904`

```
0x80014974  movw r7,#0x2040
0x80014978  str  r0(=2),[sp,#8]        ; pre-initialized 0x2040 value
0x80014980  bl   0x8000A9D8            ; read 0x2040 (4 bytes)
0x80014984  ldr  r0,[sp,#8]
0x80014986  cmp  r0,#9
0x80014988  bhs  0x80014A16            ; >=9 -> nuke
0x8001498A  tbb  [pc,r0]               ; table at 0x8001498E
0x80014998  bl   0x80014F84            ; the 0x2801 gate (cases 0 and 2)
0x8001499C  cmp  r0,#0
0x8001499E  bne  0x80014A1E            ; nonzero -> retry/CRC loop
0x800149A4  ...                        ; cases 3..8: increment 0x2040 and
                                       ; continue on the normal path
0x800149BA  bl   0x80014F0C            ; normal path: A = max(A,10)
0x800149BE  bl   0x8000911A            ; BOOTCTR_FCTRY selector
0x800149C2  cbnz r0,0x80014A16         ; 1 factory / 2 bad -> nuke
0x800149C4  bl   0x80014548            ; ECDSA product verifier
0x800149D0  cmp  r0,#1
0x800149D2  bne  0x80014A16            ; fail -> nuke
```

The `tbb` table at `0x8001498E` is `05 44 05 0b 0b 0b 0b 0b 0b`:

| `0x2040` | target | result |
|---|---|---|
| 0, 2 | `0x80014998` | gate |
| 1 | `0x80014A16` | nuke |
| 3..8 | `0x800149A4` | normal path |
| >=9 | `0x80014A16` | nuke |

An absent `0x2040` read leaves the pre-initialized `2` in the buffer, so the
live unit (0 bytes) takes the gate path. This is verified live (`76` §3).

### 1.2 Gate `FUN_80014F84`

```
0x80014F84  push {lr}; sub sp,#0x74
0x80014F8A  bl 0x80014FA6              ; read 0x2801, 0x70 bytes
0x80014F8E  bl 0x80014F28              ; B = FUN_80014FC6([+0x1c])
0x80014F92  ldr r1,[sp,#0x18]          ; A
0x80014F94  cmp r0,r1
0x80014F96  bhs 0x80014F9C             ; B >= A -> B - A
0x80014F98  movs r0,#0                 ; B < A  -> 0
0x80014F9C  bl 0x80014F28
0x80014FA0  subs r0,r0,r1              ; B - A
```

`FUN_80014FC6` clamps `+0x1c` to `3..10` and stores the clamped value back into
the caller's stack buffer only; `FUN_80014D88` is what commits it. So:

```
B = clamp(record[+0x1C], 3, 10);
A = record[+0x18];
return (B < A) ? 0 : B - A;
```

The wrapper `FUN_80014FA6` reads 0x70 bytes and logs a short read.

### 1.3 Writers

| site | effect |
|---|---|
| `FUN_80014D88` (`0x80014D88`) | writes `0x2801` 0x70 bytes; only on a primary-write failure deletes/repairs `0x2802` and rewrites both |
| `FUN_80014DEE` (`0x80014DEE`) | default record: `A=0`, `B=10`, `+0x28=0x30`, `+0x2C=0x40`, blob `0xFF` |
| `FUN_80014E34` (`0x80014E34`) | validates/repairs the record; calls the initializer if short or `record[0]>>16 != 2`; clamps `B` |
| `FUN_80014F0C` (`0x80014F0C`) | `A = max(A,10)`; called on the normal path at `0x800149BA` |
| `FUN_80014F3C` (`0x80014F3C`) | `A++` while `A < B`; called at `0x8001246E` (password mismatch) |
| `FUN_80014F5C` (`0x80014F5C`) | `A=0`; bump `+0x20` with `-1` skip / `-2 -> 0`; called at `0x80012438` (match) |
| nuke `FUN_800156E8` | deletes `0x2801` at `0x8001587E` (`0x8000AA26 -> 0x8000CA6E`) |

`0x800159AC` calls `FUN_80014E34` at `0x80015A18`, then the gate at
`0x80015A2A`. The ARM trampoline `0x80019240` calls `0x800159AC` at
`0x80019260` and boot main at `0x80019264`. Therefore an absent/invalid
`0x2801` is recreated with `A=0` **before** boot main on the same boot.

### 1.4 Password dispatcher `FUN_80012182`

- Case `0x0A` (`PASSWORD_INFO`): `FUN_80014EF0` checks the blob for non-`0xFF`;
  `FUN_8001212E` (`0x8001212E`, called at `0x80012354`/`0x80012384`) builds the
  response with `FUN_80014F28` (B) and `FUN_80014F84` (remaining budget) plus
  `+0x20`.
- Case `0x0F` (verdict): requires a 0x40-byte payload; SHA-512 is
  `FUN_80018420`; the compare is at `0x8001245A`. Mismatch ->
  `0x8001246E -> FUN_80014F3C`; match -> `0x80012438 -> FUN_80014F5C`. It then
  re-evaluates `FUN_80014F84`; if the budget is exhausted it calls
  `FUN_800158A2` (which bumps `0x2040` to 3 or nukes depending on its value).

### 1.5 `LDR_01`

- `FUN_8011DF04` (`0x8011DF04`) reads 0x2801 (0x70 bytes) and returns
  "blob is not all 0xFF"; it is wired into SETMODE at `0x8012518C`.
- `FUN_8011DD20` / `FUN_8011DDEC` read/clamp/write 0x2801, but have **no**
  `BL`/`BLX` callers and **no** little-endian pointer literals: dead code.
- The loader's strings at `0x8011DF9C`/`0x8011DFC4` are
  `"Password record not found"` / `"Flash Failure, Password"`.

## 2. Semantics and the branch

- `A` (`+0x18`) is the failed-attempt counter; `B` (`+0x1c`) is the maximum,
  clamped to 3..10. A fresh record is `A=0`, `B=10`.
- Gate 0 (`A >= B`) -> normal path: clamp `A=10`, read `BOOTCTR_FCTRY`, and
  any nonzero selector (`1` factory, `2` bad) nukes at `0x800149C2`.
- Gate nonzero (`A < B`) -> `0x80014A1E`: the retry loop runs the shared
  verifier with `flag=1`; the selector at `0x80014254` now chooses `0` ->
  ECDSA, `1` -> CRC gate (`0x80014264`, requires `region[0xd4] == 6`), `2` ->
  status `0x0f`.
- The live unit is `A=0`, `B=10`, so it is **already** in the retry branch and
  boots ECDSA (`76`). It never reaches `0x800149BE` on the normal path.
- Therefore "a factory counter on the normal path nukes" is conditional on the
  gate being 0. On the live unit a factory counter would be read in the retry
  loop and would select CRC, not the nuke. Do not write `BOOTCTR_FCTRY` on the
  working unit to exploit that; the CRC region prerequisites are untested.

## 3. ID cross-check with `77`/`78`

The recovered binary reports settle the naming:

| ID | name | role |
|---|---|---|
| `0x2002` | `NV_OSSTORE_PIN_NUM` | device PIN identifier, 4/8 bytes, protected; republished as `PIN64` to `/pps/system/nvram/deviceinfo`. Not a credential check. |
| `0x2801` | `NV_OSSTORE_PasswordStruct` | device-password verifier: salt, 5060 iterations, 64-byte digest, 10-attempt budget |
| `0x2802` | `NV_OSSTORE_PasswordStructReserve` | reserve; not a success-path mirror |
| `0x2074` | `NV_OSSTORE_OS_REGION` | OS A/B region selector and update state, 0x48 bytes (`OsRegionV3`): `+0x04` requested, `+0x05` actual, `+0x06/+0x08` per-region boot counts, `+0x0A` max (0 = unlimited), `+0x10` epoch, `+0x18/+0x30` version IDs. Not a plain boot counter. See `83`. |
| `0x2004` | `NV_OSSTORE_JvmStart_NUM` | boot-stage reads it as the committed-image flag; no resmgr special-case |
| `0x2040` | `NV_OSSTORE_NukeStatus_NUM` | boot-stage normal-path state; absent live |
| `0x2075` | `NV_OSSTORE_BOOT_MODE_BLOCK` | name-table entry only |
| `0x2821` | `NV_OSSTORE_ResetInfo_NUM` | reset-cause log |
| `0x2823` | `NV_OSSTORE_MMCBootInfo_NUM` | MMC boot info |

So the gate is the **password-attempt budget**, not a boot/factory counter.
The OS A/B region/update state is `0x2074` (`requested_region`/`actual_region`
0/1, per-region boot counts, max; see `83`); the factory counter is the RPMB
`BOOTCTR_FCTRY`; they are three different things. The earlier documents
(`69`/`71`/`74`) used "boot count" for the `0x2801` branch only because the
branch controls the boot decision; the record itself is the password store.

## 4. Residual

1. Whether the bootloader reads the "insecure" or the "secure" copy of
   `0x2801`. `77`/`78` show one stored copy per ID plus a header provenance bit;
   the editor's insecure write lands, but the bootloader's preference is not
   proven. Resolving it needs a write + reboot (not done).
2. The exact derivation of the `0x0F` 0x40-byte payload (the record hash and
   sequence input) is still unmapped.
3. The `0x2074` region/update record's exact semantics are in `83`; the live
   `boot_count_0` is `0x017C` and `max_boot_count` is 0 (unlimited). The
   `region[0xd4]` CRC field is in the image descriptor, not this record.

## 5. Reproduction

```
# gate and boot-main ordering
python3 analysis/loader-audit-2026-10-21/disarm.py \
  bootdumps-2026-09-11/hd0head.bin 80000000 80014904 thumb 90
python3 analysis/loader-audit-2026-10-21/disarm.py \
  bootdumps-2026-09-11/hd0head.bin 80000000 80014f84 thumb 30
python3 analysis/loader-audit-2026-10-21/disarm.py \
  bootdumps-2026-09-11/hd0head.bin 80000000 80014d88 thumb 40

# loader read / dead writers
python3 analysis/loader-audit-2026-10-21/disarm.py \
  bbusb/loaders/loader_06001A06-00.bin 80100000 8011dd20 arm 70
python3 analysis/loader-audit-2026-10-21/disarm.py \
  bbusb/loaders/loader_06001A06-00.bin 80100000 80125018 arm 90
```
