# 86 — Cellebrite PlayBook loader lead (signed, unpatched)

Status: lead inventory, 2026-10-25. Read-only; no device or flash writes.
Complements `85` Phase 7.

## 1. What is in the UFED pack

`/home/psyden/rea-work/cellebrite-blackberry/ufedblackberrypack.epr/` is the
Cellebrite UFED BlackBerry pack. The two entries relevant to the PlayBook
model family:

| file | size | SHA-256 | header |
|---|---|---|---|
| `06001A06_0000000A` | 159,968 | `03ffaf26dc0ce640c2f44dc5e223bb34d15175234f26d9876846484022a5c6dc` | `D7D32D1F` at +4, load `0x80100000`, tail-8 `D7C82D1F` |
| `05001A06_0000000A` | 138,796 | `b5f345d36e042a582ce27e383da87250af268ded046facdef05d67ca15e18a3f` | `D7D32D1F` at +4, load `0x02000008`, tail-8 `D7C82D1F` |

`06001A06` is the PlayBook model ID used by the connected unit; it is a
**signed ramloader** (the header signature and tail cookie match the
`isSignedLoader` check in `ramloader.pas`). It is not the standard `LDR_01`
(`loader_06001A06-00.bin`, 238,128 bytes, SHA-256 `c7cc33c7…5393f`,
entry `0x801356D8`): the pack loader is smaller, entry `0x801234FC`, and
contains the same PlayBook strings:

```
Fail to write RPMB
Password record not found
Flash Failure, Password
BOOTCTR_FCTRY
RIM-BootLoader
RIM-RAMLoader
```

`05001A06` loads at `0x02000008` (Qualcomm-style address), so it is a BB10
phone loader, not the OMAP PlayBook.

**Note:** the writeup and `messages.txt` concern the BBOS exploit, but the UFED
pack is a general extraction pack and does contain a PlayBook-model signed
loader. That loader is a separate artifact from the BBOS patched-loader
exploit; the pack entry is not the `DB9F07EB` patch.

The pack is **not** the Cellebrite exploit: the writeup's patched-loader
signature `DB9F07EB` does not occur in either file. `6A0000EA` occurs at
`0x55F8` and `0x1F2E8` in `06001A06`, but those are ordinary occurrences, not
the documented patch. The pack appears to contain the signed loaders used for
UFED extraction.

## 2. The BBOS exploit vs BB10

`BlackBerry OS cellebrite exploit - wumbomumbo.txt` + `messages.txt`:

- BBOS bootrom accepts `WRITE_RAM_SETUP`/`WRITE_RAM` at loosely checked
  addresses. The exploit writes a patched loader at the normal load address, a
  payload at `addr + delta`, then the original signed loader at
  `addr + 2*delta`, verifies, and executes the original; the patched loader
  and payload are already resident and take over.
- The patched BBOS loader changes a dispatcher branch (case `0x8`) to a
  `bl` into the payload at `0x40200400`.
- `messages.txt`: **BB10 strictly checks address parameters**, so the BBOS
  three-address trick does not work on BB10/PlayBook as-is.
- The OMAP bootrom's `0xF009`/`0xF004`/`0xF00A` handlers are in the hidden
  page/secure world (`0x40027000`), so the bootrom side is not statically
  auditable from the NS dump.

## 3. Why the `06001A06` loader is still useful

The signed loader is the code that runs after the bootrom; its command
handlers are the more accessible surface. If it exposes a validated-but-buggy
read/write/erase path, we get arbitrary eMMC/RPMB access without an unsigned
bootrom image. That is exactly what the bricked 32 GB units need (their
NVRAM/RPMB is damaged; the eMMC is otherwise intact).

Audit plan:

1. Disassemble `06001A06_0000000A` at base `0x80100000` and compare its
   dispatcher/opcode map with `LDR_01` (`66`, `72`).
2. Look for commands whose address/length/offset parameters are not fully
   checked: `0xE5`/`0x39`/`0x3A` CREAD, `0xF7`/`0xF8` write, `0xEE` erase,
   `0xC4` READVERIFY, `0xE0` installer, `0xC0` COMPLETE. The BBOS exploit is
   an address-check bug; the analogous BB10 bug would be a length/offset or
   sector-index check in these handlers.
3. Check whether the loader's `0xDE` BOOT_MODE handler is read-only in this
   build too (likely; `LDR_01` is read-only).
4. Test the loader read-only on the **spare working 64 GB unit** through the
   bootrom (`bbusb`/`bb10mt` upload path), if its model/signature is accepted.
   Do not write/flash on the connected reference.
5. If the loader gives arbitrary eMMC read/write, use it to repair the 32 GB
   units' RPMB/NVRAM (the `cellebrite-blackberry/` pack also has the BBOS
   loaders/payloads; the PlayBook loader is the one to audit).

## 4. Safety

- Signed does not mean safe to run blindly: a loader with a different build
  may behave differently. Test on the spare 64 GB unit first, read-only.
- No flashing/NVRAM/RPMB writes on the connected reference.
- The bricked 32 GB units remain recovery targets; the loader audit is the
  pre-OS route, eMMC desoldering is the hardware route.
