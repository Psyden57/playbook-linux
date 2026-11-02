# 76 — Live NVRAM audit (2026-10-21): `0x2801` is already `A=0`, gate = 10

Status: first live audit with `nvram-editor`; closes the "what is the live
`0x2801` state" question and makes the planned `patch +0x18 = 0` test moot.
Date: 2026-10-21.
Device: PlayBook, QNX 6.6
`OMAP4430_Winchester_ES2.2_HS_PVT4_Rev:07 armle`, root over the qconn door +
SSH. Access path: `BerryShell-V4.py` (qconn 4455) -> SSH key upload -> plain
`ssh -i rsa root@169.254.0.1`. All NVRAM operations below are **reads**.

Artifacts: `analysis/playbook-live-2026-10-21/`
(`nv-0x2004.txt`, `nv-0x2040.txt`, `nv-0x2074.txt`, `nv-0x2801.txt`,
`nv-0x2802.txt`, `meta.txt`, `uname.txt`, `brom-partial.bin`/`brom.err`).

Tooling installed on the unit for this session:

```
/accounts/devuser/nvram                    (SHA-256 160c51db…df164)
/accounts/devuser/libnvram.so.1            (SHA-256 295f1aba…a4a3f)
/proc/boot/libnvram.so.1 -> /accounts/devuser/libnvram.so.1
```

Uploads verified by pulling both files back and comparing SHA-256; both match
the host copies. The `/accounts/devuser` files are on the user partition and
persist; the `/proc/boot` symlink is runtime boot-image state and may need
re-creating after a reboot.

**Stock vs uploaded library.** The device's own
`/base/lib/libnvram.so.1` is **5740 bytes** (SHA-256 `ccab29ce…1688`) and the
editor fails against it with `Error: Symbol not found`. The bundled
`libnvram.so.1` (12716 bytes, SHA-256 `295f1aba…a4a3f`) is from
**BB10.0.9.388**, newer than the TabletOS 2.x on this unit; `nvram.c` was
written against BB10. The stock build lacks `readOSMetrics` and
`readBootromMetrics` (the BB10 `0x40080513` metrics API), so `dlsym` fails at
the tenth bind. The record devctl ABI is compatible, which is why the reads
work. Both libraries and the resmgr are archived under
`analysis/playbook-live-2026-10-21/device-binaries/`.

## 1. Live `0x2801` (`NV_OSSTORE_PasswordStruct`, 112 bytes)

`./nvram read 0x2801 hex` -> `Recieved bytes: 112` (0x70). Decoded:

| offset | live value | meaning |
|---|---|---|
| `+0x00` | `0x00020000` | format/id (high halfword 2) |
| `+0x04` | `0x00000070` | record length 0x70 |
| `+0x08` | `0x00000001` | flag |
| `+0x0C` | `0x000013C4` = **5060** | SHA-512 iteration count (same as the bootrom password challenge, `69` §3) |
| `+0x10` | `0xB9A043B5` | salt/challenge word |
| `+0x14` | `0x222D86B5` | salt/challenge word |
| `+0x18` | **`0x00000000`** | failed-attempt counter A |
| `+0x1C` | **`0x0000000A`** | maximum attempts B = 10 |
| `+0x20` | `0xA1E864CE` | sequence/nonce |
| `+0x24` | `0` | — |
| `+0x28` | `0x00000030` | blob offset |
| `+0x2C` | `0x00000040` | blob length |
| `+0x30` | `91 1d 08 b1 … cd a1` | 64-byte password hash blob |

`record_exists` = 1, `is_protected` = 0, `get_permissions` = 0. The record is
present and writable.

**Gate:** `clamp(+0x1C,3,10) - +0x18 = 10 - 0 = 10` -> nonzero. The unit is
**already taking the `0x80014A1E` retry branch on every boot**. It does not
need the `patch +0x18 = 0` step in `74`/`75`; that write would be a no-op.

**ID cross-check (`77`/`78`/`84`).** `0x2801` is `NV_OSSTORE_PasswordStruct`;
the device **hardware PIN** is a separate record, `0x2002 NV_OSSTORE_PIN_NUM`
(4/8 bytes, protected, live `0x50175127` = the bootrom `0xE7` PIN), and
`0x2074` is the OS region/A-B update record, not a plain boot counter (`83`).
The gate here is the password-attempt budget; the region record is not
involved in `FUN_80014F84`.

**Password proof (`84`).** The live 64-byte blob is exactly
`state₀ = "0000"; 5060 × state = SHA-512(LE32(counter) ‖ salt ‖ state)` with
`salt = +0x10..+0x17`. The bootrom `0x0A` reply carries the same salt/5060 and
the `0x0F` answer equals `HashPassV2("0000", challenge, salt, 5060)`. The
unlock password is therefore `0000`, and `0x2801` is its verifier.

This also means the "a factory counter on the normal path nukes" warning does
not apply to the unit's current state: the normal path (and its
`0x800149C2` selector nuke) is only reached when the gate is 0. With `A=0` the
selector is evaluated inside the retry loop, where 1 selects CRC and 0 selects
ECDSA. Do not write `BOOTCTR_FCTRY` on this unit to exploit that; the region
prerequisites are still untested.

### 1.1 Post-reboot confirmation

The unit was rebooted after the first audit. The post-reboot reads
(`after-*.txt`, same directory) are byte-identical for `0x2801` (SHA-256
`5311a186…0628`): `A=0`, `B=10`, same salt/hash/sequence. `0x2004` and
`0x2040` are still absent; `0x2802` is unchanged. `0x2074`'s `+6` u16 advanced
`0x017B -> 0x017C`, i.e. one increment per boot. This confirms `A=0` is the
stable live state across the reboot.

## 2. `0x2802` is not a mirror

`./nvram read 0x2802 hex` -> 112 bytes, all zero except
`00 00 00 00 00 b0 30 40 00` near `+0x53`. It is not a copy of `0x2801`.
This matches the static reading: `FUN_80014D88` writes `0x2802` only on a
primary-write failure, and the boot gate reads only `0x2801`. Patching the
reserve is unnecessary.

## 3. `0x2040` and `0x2004` are absent

`./nvram read 0x2040 hex` and `./nvram read 0x2004 hex` both return
`Recieved bytes: 0` (record not present). This is consistent with the boot
model:

- Boot main pre-initializes the `0x2040` buffer to `2` before
  `FUN_8000A9D8(0x2040,…)`, so an absent record is treated as `2` -> case
  `0`/`2` -> `FUN_80014F84` -> nonzero (A=0) -> `0x80014A1E`.
- The retry path enters the OS without the normal-path `0x2040` increment or
  the post-verify `0x2004 == 0xAAAAAAAA` check, so neither record is ever
  created. This is why A=0 is a stable fixed point.

## 4. `0x2074` (`NvOSRegionStruct`) live

`./nvram read 0x2074 hex` -> 72 bytes (0x48). Dwords:
`03 00 00 00 | 00 00 7b 01 | 00…`. This is an `OsRegionV3`
(`=IBBHHHIQ24s24s`, 72 bytes), not a plain counter. Per `83`:

| offset | field | live |
|---|---|---|
| `+0x00` u32 | version | `3` |
| `+0x04` u8 | requested_region | `0` (region A / hd1) |
| `+0x05` u8 | actual_region | `0` |
| `+0x06` u16 | boot_count_0 | `0x017B` before the reboot, `0x017C` after |
| `+0x08` u16 | boot_count_1 | `0` |
| `+0x0A` u16 | max_boot_count | `0` = unlimited |
| `+0x0C` u32 | flags | `0` |
| `+0x10` u64 | epoch / InstallTime | `0` |
| `+0x18` 24s | version_id_0 | empty |
| `+0x30` 24s | version_id_1 | empty |

`boot_count_0` increments once per successful boot; `max=0` means unlimited;
`switch()` sets `requested = actual^1`, counts 0, max 4. On-media regions are
0/1 (0 = hd1, 1 = hd2); "1 or 2" is drive numbering, and 0 is a valid region.
`region[0xd4]` belongs to the image descriptor, not this NVRAM record.

## 5. Other outputs

- `get_security_mode` -> `0 3`; `get_record_count` -> `27 3` (raw devctl
  replies; interpretation not needed here).
- `get_bootrom_metrics` **crashed**: SIGSEGV in `memcpy` inside
  `ldqnx.so.2@memcpy+0xa4`, after writing 1024 bytes. `78` §2.9 gives the
  cause: the BB10 library allocates **0x4E8** bytes, but `nvram.c` writes
  **0x4FC** (a 0x14-byte over-read in `fwrite`'s `memcpy`). It is an
  editor/BB10-library size mismatch, not an NVRAM fault.
  `brom-partial.bin` is the partial dump; `brom.err` has the fault text.

## 6. Consequence and next step

- The recovery-branch precondition is already satisfied live; no NVRAM write
  is needed for that proof.
- **Reboot confirmed (2026-10-21):** `0x2801` came back byte-identical
  (`A=0`, `B=10`), `0x2040`/`0x2004` still absent, `0x2802` unchanged, and
  `0x2074 +6` advanced `0x017B -> 0x017C`. The `/proc/boot` symlink is
  RAM/temporary and had to be re-created after the reboot.
- The factory/CRC test still requires writing `BOOTCTR_FCTRY` (RPMB) and a
  CRC-region header/`region[0xd4] == 6`; that is destructive and must not be
  done on the working unit without an explicit decision and a full backup.
- The installed editor and `/proc/boot/libnvram.so.1` symlink are left in
  place for the next session; the qconn door process was stopped after the
  audit.

## 7. Reproduction

```
# host
python3 BerryShell-V4.py exec \
  --key /home/psyden/rea-work/rsa --pub /home/psyden/rea-work/rsa.pub -- \
  "uname -a; id"

# device (read-only)
cd /accounts/devuser
for id in 0x2004 0x2040 0x2074 0x2801 0x2802; do ./nvram read $id hex; done
./nvram record_exists 0x2801; ./nvram is_protected 0x2801
./nvram get_permissions 0x2801
```

Gate check (host):

```
python3 - <<'PY'
import struct
b=bytes.fromhex(' '.join(open('nv-0x2801.txt').read().splitlines()[2:]).replace(' ',''))
A=struct.unpack_from('<I',b,0x18)[0]; B=struct.unpack_from('<I',b,0x1c)[0]
Bc=min(10,max(3,B)); print(A,B,Bc,0 if Bc<A else Bc-A)
PY
```
