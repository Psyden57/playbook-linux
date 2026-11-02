# 75 — NVRAM `0x2801` is `NV_OSSTORE_PasswordStruct`: writers and the real gate

Status: closes item 1 of `HANDOFF-2026-10-21-next-session.md` §6 (the
`0x2801` writer question) and renames the record correctly. Complements
`74-nvram-editor-and-0x2801.md`, which documents the live editor.
Date: 2026-10-21.
Targets:

| file | SHA-256 | base |
|---|---|---|
| `bootdumps-2026-09-11/hd0head.bin` | `dda9d955003302adf1f0d41580c50c0c9035126487e7ac53793f4d38a3a23e3d` | `0x80000000` |
| `bbusb/loaders/loader_06001A06-00.bin` (`LDR_01`) | `c7cc33c796f9518bbc0eb929c6096f2473937ad486c579764aa4c57d8165393f` | `0x80100000` |
| `playbook-dev/dumped4869ifs/proc/boot/nvram_resource_manager` | (ELF32 ARM) | `0x80650000` runtime |

Provenance: Capstone 5.0.7 ARM/Thumb decode, Ghidra 12.1.4 headless
(`ARM:LE:32:v7`) for the boot stage and `LDR_01`, `strings`/`readelf` for the
QNX resource manager. No device, NVRAM, RPMB or flash writes were made.

**Result in one line.** `0x2801` is not a time/recovery record. It is the
**device-password record** (`NV_OSSTORE_PasswordStruct`, 0x70 bytes); `0x2802`
is its reserve (`NV_OSSTORE_PasswordStructReserve`). Its `+0x18` word is the
password-attempt counter and `+0x1c` the clamped maximum (3..10). The boot
gate returns `clamp(+0x1c,3,10) - +0x18` (0 if negative), so the factory/CRC
retry branch is taken while **`+0x18 < +0x1c`**. The pre-OS writers are all in
the boot stage itself; the shipped ramloader's `0x2801` write helpers are dead
code, and the booted OS reaches the record through `/dev/nvram`
(`nvram-editor/` is the user-facing client).

## 1. Identity: `0x2801` is `PasswordStruct`

Three independent matches:

1. The QNX resource manager's name/ID table stores `0x2801` immediately before
   the string `NV_OSSTORE_PasswordStruct_NUM`, and `0x2802` before
   `NV_OSSTORE_PasswordStructReserve_NUM`
   (file offsets `0x14a6c`/`0x14aa4` and `0x14a71`/`0x14aa9`; the same table
   gives `NV_OSSTORE_OS_REGION = 0x2074`, which is already known).
2. `LDR_01`'s `0x2801` accessors log `"Password record not found"` and
   `"Flash Failure, Password"` (`0x8011DF9C`, `0x8011DFC4`).
3. The boot stage implements the bootrom password pair at `FUN_80012182`:
   `0x0A` builds a PASSWORD_INFO response, `0x0F` accepts a 0x40-byte
   password payload and reports a verdict.

The record name explains the "recovery/time condition" language in `69`/`71`:
the handler reads and clamps the *password attempt budget*, not a clock.

**Independent confirmation (`77`/`78`/`84`).** The resmgr's own 288-entry
descriptor table (56 bytes/entry at file `0x12D54`) maps entry #133: `0x2801`,
flags `0x0000`, `NV_OSSTORE_PasswordStruct_NUM`, at `0x14A6C`. The only two
`0x2801` code sites in the whole resmgr are the password wipe at `0x9CE8`
(read `0x9D30`, write `0x9E04`); the resmgr never computes or checks the
digest. The device **hardware PIN** is a different record: `0x2002`
`NV_OSSTORE_PIN_NUM`, 4/8 bytes, protected, live `0x50175127` (the bootrom
`0xE7` PIN), republished as `PIN64`. The `0x2074` `NV_OSSTORE_OS_REGION` is the
OS A/B region/update record, not a plain boot counter (`83`). The editor's
bundled library is **BB10.0.9.388**, not the TabletOS 2.x stock
`/base/lib/libnvram.so.1`; the stock build lacks the metrics symbols
(`readOSMetrics`/`readBootromMetrics`), which is the editor's
`Symbol not found` against it.

**The digest is proven, not inferred (`84`).** The live 64-byte blob at
`+0x30` is reproduced exactly by:

```
state = "0000"
repeat 5060 times: state = SHA-512(LE32(counter) || salt || state)
salt = record[0x10:0x18]
```

so `0x2801` is the verifier for the unlock password `0000`. The bootrom
`0x0A`/`0x0F` exchange is built from this record: the `0x0A` reply carries the
same salt and `5060`, and the `0x0F` 64-byte answer equals
`HashPassV2("0000", challenge, salt, 5060)` and also
`pass(challenge || record_digest)`. The plaintext is not stored. `0x2002` is
the separate hardware/device PIN identifier, not the unlock code.

## 2. Record layout (0x70 bytes, little-endian)

From the initializer `FUN_80014DEE` (`0x80014DEE`) and the writers in §3:

| offset | size | meaning |
|---|---|---|
| `+0x00` | u32 | format/id — `0x00020000` on a fresh record; the QNX resmgr also recognizes `0x00010002` as an older format |
| `+0x04` | u32 | record length, `0x70` |
| `+0x08` | u32 | `1` |
| `+0x0C` | u32 | hash iteration count (live `5060`; `0` on a fresh init) |
| `+0x10` | u32 | salt/challenge word 1 (live `0xB9A043B5`; `0` fresh) |
| `+0x14` | u32 | salt/challenge word 2 (live `0x222D86B5`; `0` fresh) |
| `+0x18` | u32 | **failed-attempt counter A** (fresh `0`, i.e. the full 10-attempt budget; `10` = exhausted) |
| `+0x1C` | u32 | **maximum attempts B** (fresh `10`, clamped to `3..10`) |
| `+0x20` | u32 | sequence/generation counter (fresh `0xFFFFFFFF`; `-1` is not incremented, `-2` wraps to `0`) |
| `+0x24` | u32 | `0` |
| `+0x28` | u32 | offset of the password blob, `0x30` |
| `+0x2C` | u32 | blob length, `0x40` |
| `+0x30` | 0x40 | password/challenge blob; all `0xFF` means "no password set" |

`0x2802` is a fallback copy written only when the primary write fails
(`FUN_80014D88`).

## 3. The boot gate, exactly

`FUN_80014F84` (`0x80014F84`) reads the record and returns:

```c
B = clamp(record[+0x1C], 3, 10);   /* FUN_80014F28 -> FUN_80014FC6 */
A = record[+0x18];
return (B < A) ? 0 : B - A;
```

`FUN_80014FC6` stores the clamped value back into the caller's buffer only; it
is `FUN_80014D88` that commits it.

Boot-main ordering is the critical part (`0x80014904`):

```
0x80014974  read NVRAM 0x2040
0x80014988  cmp 0x2040,#9 / bhs 0x80014a16       ; >=9 -> nuke
0x8001498a  tbb  [pc,r0]                          ; 0/2 -> gate, 1 -> nuke,
                                                  ; 3..8 -> normal path
0x80014998  bl  FUN_80014f84                      ; password budget
0x8001499e  bne 0x80014a1e                        ; nonzero -> retry/CRC loop
...
0x800149ba  bl  FUN_80014f0c                      ; A = max(A,10)
0x800149be  bl  FUN_8000911a                      ; BOOTCTR_FCTRY selector
0x800149c2  cbnz r0, 0x80014a16                   ; factory(1)/bad(2) -> nuke
```

So the `0x2801` check at `0x80014998` runs **before** the RPMB selector read at
`0x800149BE`. A nonzero gate jumps straight to the retry/CRC loop and never
reaches the "any nonzero selector nukes" branch. That is the whole reason a
factory counter can be benign, and it requires `A < B`.

On the normal path, `FUN_80014F0C` sets `A = 10`; with `B = 10` the gate is 0
on the next boot. **A = B = 10 is the "budget exhausted / normal boot" state.**
A fresh or reset record has `A = 0`, `B = 10`, i.e. ten attempts of headroom.

## 4. Writers

### 4.1 Boot stage (hd0) — the real writers

All commits go through `FUN_80014D88` (`0x80014D88`): write `0x2801`, on error
delete/repair `0x2802` and rewrite both. `0x8000A9FE` is the NVRAM writer;
`0x8000AA26 -> 0x8000CA6E` is the delete/repair path.

| writer | address | effect | triggered by |
|---|---|---|---|
| `FUN_80014DEE` | `0x80014DEE` | creates the default record: `A=0`, `B=10`, `+0x28=0x30`, `+0x2C=0x40`, blob `= 0xFF` | `FUN_80014E34` when the read is short or `record[0]>>16 != 2` |
| `FUN_80014E34` | `0x80014E34` | validates/repairs `0x2801`, repairs `0x2802`, clamps `B`, rewrites | pre-boot trampoline `0x800159AC` (`0x80015A18`) |
| `FUN_80014F0C` | `0x80014F0C` | `A = max(A,10)` (exhaust budget) | normal boot path `0x800149BA` |
| `FUN_80014F3C` | `0x80014F3C` | `A++` while `A < B` | password mismatch in the `0x0F` handler (`0x8001246E`) |
| `FUN_80014F5C` | `0x80014F5C` | `A=0`; bump `+0x20` with `-1` skip / `-2 -> 0` wrap | password match in the `0x0F` handler (`0x80012438`) |
| nuke `FUN_800156E8` | `0x8001587E` | **deletes** `0x2801` (`thunk_FUN_8000CA6E`) | `0x800149D2`/`0x800149C2` failures, or `FUN_800158A2` |

`FUN_80012182` (`0x80012182`) is the stage's control-command dispatcher:

- case `0x0A` — PASSWORD_INFO: `FUN_80014EF0` checks whether the blob is all
  `0xFF`; if it is not, `FUN_8001212E` builds the response with
  `FUN_80014F28` (B) and `FUN_80014F84` (remaining budget) plus the `+0x20`
  sequence value.
- case `0x0F` — password verdict: requires a 0x40-byte payload, hashes the
  record blob / sequence with `FUN_80018420` (SHA-512), compares at
  `0x8001245A`. Mismatch → `FUN_80014F3C` (`0x8001246E`); match →
  `FUN_80014F5C` (`0x80012438`). It then re-evaluates `FUN_80014F84`; if the
  budget is exhausted it calls `FUN_800158A2`, which either bumps `0x2040` to 3
  or runs the nuke.

### 4.2 Boot trampoline and init ordering

`0x80019240` is the ARM hand-off block:

```
0x80019260  blx 0x800159ac     ; status init -> FUN_80014E34
0x80019264  blx 0x80014904     ; boot main / verifier
```

`0x800159AC` calls `0x80014E34` at `0x80015A18`, then itself calls
`FUN_80014F84` at `0x80015A2A`. Therefore **an absent/invalid `0x2801` is
recreated with `A=0` before boot main reads the gate, on the same boot.**
This is the one path that turns an invalidated record into the retry/CRC
branch without any RPMB write.

### 4.3 `LDR_01` — read-only in practice

`LDR_01` contains a port of the same logic:

- `FUN_8011DF04` (`0x8011DF04`) reads `0x2801`, 0x70 bytes, and returns
  "blob is not all `0xFF`". It is wired into SETMODE: `FUN_80125018` case 7,
  call at `0x8012518C`.
- `FUN_8011DD20` / `FUN_8011DDEC` (`0x8011DD20`, `0x8011DDEC`) read, clamp
  `+0x1C` via `FUN_8011DCC8`, and write `0x2801` back.
- Those two writers have **no `BL`/`BLX` callers and no little-endian pointer
  literals** in the image. They are linked-in dead code, exactly like the
  `0xDE` factory-counter setters in `72` §5.1.

Polyglot loader builds (`LDR_02…81`) all carry the
`"Password record not found"` string and their own copies of the family, but
`LDR_01` is the only PlayBook (`06001A06`) entry, and it cannot write the
record.

### 4.4 CFP / host tools

Neither `CFP.exe` nor `cfp-old.exe` contains a genuine `0x2801` reference
(the one apparent hit in `cfp-old.exe` at file `0x2B8EB` is the displacement of
a `call rel32`). `bb10mt`/`bbusb` have no command that writes it. `0xDE` is
read-only (`72`).

### 4.5 Booted OS — `/dev/nvram` is the general writer

`nvram_resource_manager` attaches `/dev/nvram` (started from
`sanity.cfg`). Its devctl dispatcher `0xB840` maps:

| devctl | handler | meaning |
|---|---|---|
| `0xC0080502` | `0x8FF0` | read record (`{u16 id, …}` → buffer/len) |
| `0xC0080509` | `0x9BE0` → `0x913C` | **write record** (`id` at msg+4, data at msg+8, length = msg size − 8) |
| `0xC0080507` / `0xC0080508` | `0x9E7C` → `0x913C(id,0,0,flag)` | delete/commit variants |
| `0xC008050C` | `0x9CE8` | password-record migration |

`0x9CE8` reads `0x2801` (0x70 bytes) and, for the current format
(`record[0] >= 0x20000`), does:

```
record[+0x18] = record[+0x1C];      /* A = B: exhaust the budget */
memset(record+0x30, 0xFF, 0x40);    /* clear the password blob */
write 0x2801, 0x70 bytes;
```

i.e. a "clear device password / normal-boot state" operation, not a way to
create headroom. The generic `0xC0080509` write is the one that can set
`+0x18` to any value (0 for the recovery branch).

### 4.6 Live writer: `nvram-editor/` (booted OS)

`nvram-editor/nvram` (ARM QNX, source `nvram.c`) is the user-facing client for
§4.5. It dlopens `/proc/boot/libnvram.so.1`, opens `/dev/nvram` `O_RDWR`, and
binds `nv_get_record`, `nv_write_record`, `nv_write_secure_record`,
`nv_delete_insecure`/`nv_delete_secure`, `nv_is_protected`,
`nv_get_permissions`, `nv_record_exists`.

`nvram patch <id> <off> <data>` does:

1. `nv_get_record(id, buf, 1)` to learn the length,
2. re-read the whole record into `buf`,
3. patch the bytes in place,
4. `nv_write_record(id, buf, len)` then `nv_write_secure_record(id, buf, len)`.

So a patch preserves the live record length (0x70 for `0x2801`), it does not
synthesize a record. It returns early if the insecure write fails, so the
secure copy may never be written; check `is_protected`/`get_permissions` first
and always read the record back. This is the practical writer for the working
unit; it cannot help the expired unit (no OS).

`./nvram patch 0x2801 0x18 0x00000000` writes a u32 `0` at `+0x18` and leaves
`+0x1C = 10`, so the next boot's gate is 10 and takes `0x80014A1E`. Use
`patch`, **not** `write`: `write` passes `data_size` as the new record length,
so `nvram write 0x2801 0x00000000` would replace the 0x70-byte record with a
4-byte one (the stage would then treat it as invalid and reinitialize it).
Note the argument form: `0x00000000` is parsed as 4 bytes, whereas `0x0a` is a
single byte (use `patch 0x2801 0x18 0x0a` to restore `A = 10`;
`0x0a000000` would produce the byte order `00 00 00 0A`). Patching `0x2802` is
**not** required by the gate: `0x2802` is only written on a primary-write
failure (§4.1) and the gate reads only `0x2801`. It is harmless belt-and-braces,
not a mirror.

## 5. Consequence for the factory/CRC plan

- A working unit in the normal state has `A = B = 10` and gate 0, so a valid
  `BOOTCTR_FCTRY` still hits `0x800149C2` and nukes. The handoff's warning is
  correct.
- To make a factory counter benign you need the gate nonzero at
  `0x80014998`, i.e. **`+0x18 < clamp(+0x1C)`** at that instant. Two writers
  produce that state: the initializer `FUN_80014DEE` (`A=0`) and the
  correct-password path `FUN_80014F5C` (`A=0`). The normal-path
  `FUN_80014F0C` destroys it (`A=10`).
- The only pre-OS ways to get there are:
  1. invalidate/delete `0x2801` so `0x800159AC` recreates it with `A=0`; the
     shipped ramloader cannot do this, and the nuke delete is destructive;
  2. pass the stage's `0x0F` exchange, which sets `A=0` and re-enters the
     retry loop. The payload is a 0x40-byte hash over the record blob and
     sequence; the host-side derivation is not yet mapped (it is not simply
     the bootrom `SEND_PASSWORD` frame — that is a different, 0x18-byte
     message).
- The retry loop re-reads the selector through `FUN_8000911a`; a factory
  counter (1) takes the CRC gate, a product counter (0) the ECDSA path with
  `flag=1`, and the expired `0xFFFFFFFF`/2 case still fails, so this does not
  by itself fix the expired unit.
- On a booted OS the general writer exists: `/dev/nvram` devctl
  `0xC0080509` for record `0x2801`, or `0xC008050C` for the
  clear-password/exhaust state. This is the NVRAM analogue of `/dev/rpmb`.
  The `nvram-editor` tool wraps the same driver: read the record, set
  `+0x18 = 0`, write it back.
- **A=0 is sticky.** The retry branch at `0x80014A1E` never calls
  `FUN_80014F0C`, so the device keeps taking the retry loop on every boot
  until the `0x0F` exchange or an explicit write restores `A=10`. The test is
  reversible (`patch 0x2801 0x18 0x0a`), but it is not a one-shot flag.

## 6. Safety

- Do not write `0x2801` on the working unit without a full backup. `A=0`
  routes every subsequent boot through the retry/SW-restore branch until
  something sets it back to 10; combined with a factory counter that is the
  intended CRC path, but with a bad or partial record it is a boot-loop risk.
  A failed retry-loop verification hangs in `FUN_80014C98` rather than
  reaching the nuke, so it does not erase the region, but it also does not
  boot.
- `A=10`, blob `0xFF` is the benign "no password, normal boot" state produced
  by the OS `0xC008050C` migration.
- Deleting `0x2801` is as destructive as writing it: the plus side is the
  stage recreates it; the minus side is that the recreate happens before boot
  main and immediately selects the retry branch.
- With `nvram-editor`, dump the record first and verify `nvram read 0x2801 hex`
  after the patch; `patch` writes the insecure and secure copies in that
  order and stops early if the insecure write fails.

## 7. Reproduction

```
# boot stage: gate, writers, init
python3 analysis/loader-audit-2026-10-21/disarm.py \
  bootdumps-2026-09-11/hd0head.bin 80000000 80014904 thumb 90
python3 analysis/loader-audit-2026-10-21/disarm.py \
  bootdumps-2026-09-11/hd0head.bin 80000000 80014d88 thumb 40
python3 analysis/loader-audit-2026-10-21/disarm.py \
  bootdumps-2026-09-11/hd0head.bin 80000000 80014dee thumb 40

# ramloader: dead 0x2801 writers, live SETMODE reader
python3 analysis/loader-audit-2026-10-21/disarm.py \
  bbusb/loaders/loader_06001A06-00.bin 80100000 8011dd20 arm 70
python3 analysis/loader-audit-2026-10-21/disarm.py \
  bbusb/loaders/loader_06001A06-00.bin 80100000 80125018 arm 90

# OS: name/ID table and devctl dispatcher
python3 - <<'PY'
d=open('playbook-dev/dumped4869ifs/proc/boot/nvram_resource_manager','rb').read()
print(hex(0x2801), d.find(b'NV_OSSTORE_PasswordStruct_NUM'))
print(hex(0x2802), d.find(b'NV_OSSTORE_PasswordStructReserve_NUM'))
PY
```

Live, on the booted working unit (`nvram-editor/nvram` copied over):

```
./nvram get_bootrom_metrics > brom.bin
./nvram read 0x2004 hex > nv-2004.before
./nvram read 0x2040 hex > nv-2040.before
./nvram read 0x2074 hex > nv-2074.before
./nvram read 0x2801 hex > nv-2801.before
./nvram read 0x2802 hex > nv-2802.before
./nvram record_exists 0x2801
./nvram is_protected 0x2801
./nvram get_permissions 0x2801

# only after the dumps are off-device:
./nvram patch 0x2801 0x18 0x00000000
./nvram read 0x2801 hex            # confirm +0x18 == 00 00 00 00, +0x1c == 0a 00 00 00
# restore:
./nvram patch 0x2801 0x18 0x0a
```

Ghidra headless (used for the decompiles above):

```
analyzeHeadless <proj> nvproj -import hd0head.bin \
  -processor ARM:LE:32:v7 -loader BinaryLoader -loader-baseAddr 0x80000000 \
  -scriptPath analysis/hd0-modemap-2026-10-20 \
  -postScript SeedCorrect.java list.txt out.txt
```

## 8. Residual questions

1. Which host command reaches `FUN_80012182` case `0x0F` on a real unit? The
   dispatcher is polled by `FUN_80012516` and is separate from the `LDR_01`
   opcode set; CFP's exact pre-OS sequence for it is still unmapped.
2. `FUN_80014ED4` reads `0x2801` twice into adjacent 0x70-byte buffers and
   returns the second copy's `+8..+0x14`. It looks like a double-buffered
   record read, but the reason is not settled.
3. The QNX `0xC008050C` migration's trigger (which OS operation calls it) is
   not identified beyond the devctl number.
4. `0x2802` handling in `FUN_80014E34` writes a possibly stale buffer when the
   reserve is short; it is a repair-path quirk, not a boot decision.
