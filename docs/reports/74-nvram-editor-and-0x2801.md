# 74 — `nvram-editor` and the `0x2801` recovery record

Status: new capability added to the handoff (2026-10-21, final).
Date: 2026-10-21.
Provenance: `nvram-editor/nvram.c` (workspace), ARM QNX binary; Thumb
disassembly of `hd0head.bin` (`0x80014f28`, `0x80014f84`, `0x80014fa6`,
`0x80014d88`). No writes were made.

## 1. The tool

`nvram-editor/nvram` is an ARM/QNX executable (source `nvram.c`) that dlopens
`/proc/boot/libnvram.so.1` and exposes the NVRAM driver:

```
nvram read <blockid> [hex|dec|ascii]
nvram partialread <blockid> <from> <count> [format]
nvram patch <blockid> <offset> <data...>
nvram write <blockid> <data...>
nvram delete <blockid>          # insecure + secure
nvram delete_secure <blockid>
nvram get_permissions <blockid>
nvram is_protected <blockid>
nvram record_exists <blockid>
nvram get_bootrom_metrics
nvram get_os_metrics
nvram get_record_count
nvram get_security_mode
```

It only works from the **booted OS** (the working unit). It cannot help the
expired unit, which has no OS. `patch`/`write` call `nv_write_record` and then
`nv_write_secure_record` with the same buffer, but they return early if the
first call fails (`nvram.c:391-395` and `420-424`) — so a secure-only record
may never reach the secure writer. Check `is_protected`/`get_permissions`
first; if `0x2801` is protected, patch the tool to attempt
`nv_write_secure_record` regardless of the insecure result (or call it first)
and report both results.

**Build provenance and the symlink.** The bundled `libnvram.so.1` is from
**BB10.0.9.388**, newer than the TabletOS 2.x on this unit; `nvram.c` was
written against BB10. The stock `/base/lib/libnvram.so.1` is 5740 bytes and
lacks `readOSMetrics`/`readBootromMetrics` (the BB10 `0x40080513` metrics
API), which is exactly the `Error: Symbol not found` the editor prints when
symlinked to the stock library. The record devctl ABI is compatible, so the
`read`/`patch` paths work. `/proc/boot` is RAM/temporary: the
`/proc/boot/libnvram.so.1` symlink does **not** survive a reboot and must be
re-created each boot.

Read-only first (working unit):

```
./nvram get_bootrom_metrics > brom.bin
./nvram read 0x2004 hex
./nvram read 0x2040 hex
./nvram read 0x2074 hex
./nvram read 0x2801 hex
./nvram read 0x2802 hex
./nvram record_exists 0x2801 ; ./nvram is_protected 0x2801 ; ./nvram get_permissions 0x2801
```

## 2. `0x2801` / `0x2802` — `NV_OSSTORE_PasswordStruct`

> Identity corrected in `75`: this is the device-password record, not a
> generic boot-info record. The QNX resmgr's ID table names `0x2801`
> `NV_OSSTORE_PasswordStruct_NUM` and `0x2802`
> `NV_OSSTORE_PasswordStructReserve_NUM`; `LDR_01` logs
> `"Password record not found"`. Full writer analysis is in
> `75-nvram-2801-password-record.md`; the independent ID map is `77` and the
> library analysis is `78`. The device **hardware PIN** is `0x2002`
> (`NV_OSSTORE_PIN_NUM`, live `0x50175127`); `0x2074` is the OS A/B
> region/update record (`83`), not a plain boot counter and not `0x2801`.

`FUN_80014fa6` reads 0x70 bytes from NVRAM `0x2801` via `FUN_8000A9D8`.
`FUN_80014d88` writes it via `FUN_8000A9FE`. It writes `0x2802` **only when
the primary write fails** (delete/repair/retry); it is not a success-path
mirror. `FUN_80014e34` repairs `0x2802` only when its read is short. The boot
gate reads only `0x2801`.

Known fields:

| Offset | Meaning | Evidence |
|---|---|---|
| `+0x18` | failed-attempt counter A (`0` = full budget, `10` = exhausted); comparison value in `FUN_80014f84` | `ldr r1,[sp,#0x18]`; `FUN_80014f0c` sets it to 10, `FUN_80014f3c` increments it, `FUN_80014f5c` zeroes it |
| `+0x1c` | maximum attempts B, clamped to `3..10` | `FUN_80014fc6`: `cmp #3`, `cmp #0xa`, `str [r0,#0x1c]` |
| `+0x20` | sequence/generation counter with wrap handling | `0x80014f5c`: `adds #1`, `beq` on `-1`, wrap `-2 -> 0`, write |
| `+0x30` | 0x40-byte password blob (`0xFF` = unset) | `FUN_80014dee` initializer |

`FUN_80014f28` reads 0x2801, clamps `+0x1c` via `FUN_80014fc6`, and returns
`[+0x1c]`.

`FUN_80014f84` (the `0x80014998` recovery check):

```c
read 0x2801 (0x70 bytes);
a = [+0x1c] (clamped 3..10);
b = [+0x18];
return (a < b) ? 0 : (a - b);
```

The caller at `0x80014998` does `cmp r0,#0; bne 0x80014a1e`. So:

- `[+0x1c] < [+0x18]` → 0 → normal path.
- `[+0x1c] >= [+0x18]` → nonzero → the `0x80014a1e` retry/CRC loop.

Setting `+0x18 = 0` forces the recovery branch (the clamped `+0x1c` is at
least 3). The helpers at `0x80014f3c`/`0x80014f5c` also mutate `+0x18` and
`+0x20` and write the record, confirming these are boot-attempt fields.

Write (only after a full dump and with a recovery path). `patch` re-reads the
live record, keeps its length, and writes the insecure then secure copy:

```
./nvram patch 0x2801 0x18 0x00000000
./nvram read 0x2801 hex      # read-back: +0x18 == 00 00 00 00
# restore: ./nvram patch 0x2801 0x18 0x0a    (single-byte 10)
```

`0x2802` is not read by the gate and is not a success-path mirror; patching it
is optional. Use `patch`, not `write`: `write` passes the supplied data size as
the new record length, so a short `write` would truncate the 0x70-byte record.
Note that `A=0` is sticky on the retry branch — the normal-path
`FUN_80014f0c` that would restore `A=10` is skipped, so every boot keeps taking
the retry loop until the record is written again (or the `0x0F` exchange
succeeds).

## 3. `0x2074` (`NvOSRegionStruct` / `NV_OSSTORE_OS_REGION`)

The region record is read elsewhere with `0xC1` bytes (`0x800142DC`); the
factory CRC gate is `region[0xd4] & 0xff == 6` (`71` §2.1). `0xd4` is outside
the observed `0xC1` read and outside the live 0x48-byte record, so it belongs
to the image/region descriptor, not this NVRAM record. Verify the actual
descriptor on a live dump before patching.

The live `0x2074` record is 72 (0x48) bytes and is exactly an `OsRegionV3`
(`struct.Struct('=IBBHHHIQ24s24s')`). Per `83`:

| offset | size | field | live |
|---|---|---|---|
| `+0x00` | u32 | version | `3` |
| `+0x04` | u8 | requested_region | `0` (region A / hd1) |
| `+0x05` | u8 | actual_region | `0` |
| `+0x06` | u16 | boot_count_0 | `0x017C` (`0x017B` before the reboot) |
| `+0x08` | u16 | boot_count_1 | `0` |
| `+0x0A` | u16 | max_boot_count | `0` = unlimited |
| `+0x0C` | u32 | flags | `0` |
| `+0x10` | u64 | epoch / InstallTime | `0` |
| `+0x18` | 24s | version_id_0 | empty |
| `+0x30` | 24s | version_id_1 | empty |

This is the OS **A/B region selector and update state**, not a plain boot
counter: `requested_region` is the region to try first, `actual_region` is the
one that verified, and the `boot_count_*` fields are the per-region boot budget
(`max_boot_count = 0` = unlimited; `switch()` sets 4). On-media values are
0/1 (region 0 = `/dev/hd1`, region 1 = `/dev/hd2`); "1 or 2" is drive
numbering, and 0 is a valid region, not "unset". The bootloader masks with
`&1`. The resmgr's `0xD680` forces `max=5` and `count[actual]=5` to force a
rollback. It is neither the factory counter nor the `0x2801` gate.

## 4. Boot-flow test order

1. Read-only dump: `0x2004`, `0x2040`, `0x2074`, `0x2801`, `0x2802`,
   `get_bootrom_metrics`. Save off-device.
2. Decode the `0x2801` fields and confirm the live `[+0x1c] < [+0x18]` state.
3. With a full backup, set `+0x18 = 0` in `0x2801`, reboot.
   **Already done in the live state:** `76` reads `A=0`/`B=10` on the working
   unit and it survived a reboot, so this step is a no-op there; do not write.
   With selector 0 the retry loop still boots via the public-key path.
   Restore with `patch 0x2801 0x18 0x0a` if it is ever changed.
4. Only on a sacrificial unit: add a valid factory counter (`/dev/rpmb`) and
   set `0x2074 +0xd4 == 6` to test the CRC path.
5. Never write `BOOTCTR_FCTRY` on the working unit.

## 5. Open

- ~~Exact semantics of `+0x18`/`+0x1c`/`+0x20`.~~ Resolved in `75`: `+0x18`
  is the password-attempt counter, `+0x1c` the clamped maximum (3..10),
  `+0x20` a sequence/generation counter. `0x2801` is
  `NV_OSSTORE_PasswordStruct`.
- Whether the NVRAM driver or bootloader validates a record checksum.
- The real `0x2074` record size and the `+0xd4` field placement.
