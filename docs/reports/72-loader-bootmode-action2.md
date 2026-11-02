# 72 — `loaders.dat` audit: BOOT_MODE (`0xDE`) action 2

Status: closes the loader-search item in
`HANDOFF-2026-10-21-next-session.md` §4.1.
Date: 2026-10-21.
Provenance: `TMyArcReader` layout parser, Python `lzma` (`FORMAT_ALONE`),
Capstone 5.0.7, and Ghidra 12.1.4 headless (`ARM:LE:32:v7`, raw binary at the
header load address). No device, RPMB, NVRAM, or flash writes were made.

**Result: no recoverable loader in the archive implements `0xDE` action 2.**
Every `0xDE` handler accepts action 0 (read) only. The setter functions exist
in the OMAP builds but are unreachable; the Qualcomm builds have only a
decrement helper. The pre-OS factory-counter writer therefore still does not
exist. (`LDR_00` cannot be checked: it is corrupted in this archive, §1. It
is a Qualcomm/Classic entry, not a PlayBook loader.)

## 1. Archive inventory

`bb10mt/loaders.dat`: 5,884,641 B, SHA-256
`37e34cb65bbeef615bc6c84913a38c5935fcbbaab41202df5c51c5746ee907f9`.

Layout (exactly `uarchive.pas:TMyArcReader`):

```
"BB10MT"                         6 bytes
u32 count                        82
repeat count:
  u32 nameLen; char name[nameLen]
  u64 origSize; u64 compSize; u64 dataOffset; u32 crc32
LZMA-alone streams (5-byte props + 8-byte size + data), CRC-32 over the
compressed stream
```

81 of 82 entries extract and decompress. **LDR_00 is corrupt in this archive**:
its `dataOffset` is `0xa`, the first byte after the count, which is inside the
entry table (table ends at `0xc36`). The stored CRC is `0xe092b8e5`; the CRC
over the bytes actually at `0xa` is `0xc06aed4b`, and the LZMA stream fails.
This is a `TMyArcWriter` bug: `AddFileFromStream` writes entry 0 at offset
`0xa`, then `UpdateHeader` overwrites `6..0xc36` with the table.

All other entries decompress to signed ramloaders (`D7D32D1F` at offset 4,
load address at offset 8) and are byte-identical to the copies shipped in
`bbusb/loaders/` where present.

## 2. PlayBook mapping

`bb10mt.ini` is the model-ID → archive-entry map. PlayBook-family keys:

| Model ID | Device | Entry | Load address |
|---|---|---|---|
| `06001A06` | P100-16WF | `LDR_01` | `0x80100000` |
| `07001A06` | P150 (not in the device table) | `LDR_28` | `0x80100000` |

`LDR_01` is exactly `loader_06001A06-00.bin` (238,128 B, SHA-256
`c7cc33c796f9518bbc0eb929c6096f2473937ad486c579764aa4c57d8165393f`), the same
image embedded in CFP.exe. The `0c/0d/0e…1a06` P150 variants in
`ramloader.pas` are not keys in this archive.

## 3. `0xDE` = BOOT_MODE protocol

The loader dispatcher confirms the request framing and the action selector: the
first word of the 8-byte payload is the action, and action 0 is the only value
it accepts. The CFP-side semantics (0 read, 1 switch, 2 set count) are from
`71` §7c (`FUN_0044d400` in CFP.exe; the request struct is built by the
caller):

```
request : channel 2, cmd 0xDE, 8-byte payload { u32 action, u32 value }
response: cmd 0xFA, 12-byte payload { u32 status, u32 mode, u32 count }

action 0  read boot mode
action 1  switch mode (value 1 = FACTORY, 0 = PRODUCT)
action 2  set factory boot count (value 1..50)
```

`status`: 0 ok, 1 not allowed, 2 count out of range, 3 cannot set in this
device mode. `mode`: 0 product, 1 factory, 2 expired (`FUN_8011e074` /
`FUN_8021e8e8` return values).

### 3.1 `0xFFFFFFFF` is masked as "absent" by the PlayBook loader (`LDR_01`)

The PlayBook loader's action-0 reader (`FUN_8011e074` in `LDR_01`, the image
CFP also uses) has an explicit sentinel:

```c
if (local_10 == 0xffffffff) return 0;   /* treated as "record absent" */
```

The newer OMAP readers (e.g. `FUN_8011ef78` in `LDR_13`) do **not** have this
case: they read the value, and `value == 0 || value > 0x32` falls through to
`return 2`, matching the bootloader's reader (`FUN_8000911a` -> `0x800090CE`).
So `0xFFFFFFFF` is *absent* to `LDR_01` but *invalid/expired* (selector 2) to
the newer loaders and to the bootloader.

Consequence: reading `0xDE` through `LDR_01` on a unit whose record is
`0xFFFFFFFF` reports `status=0, mode=product, count=0`, not "expired". This was
observed on the expired unit; its bootloader still nukes it. Do not use that
read to infer the bootloader selector for the sentinel. (This refines `71`
§7b, which said the loader reader fails on `>0x32`; the `LDR_01` sentinel is
the exception.)

**Unsupported actions produce no response at all.** Every handler logs the
action number and returns (`goto LAB_…_f48`), so the host sees a USB timeout,
not an error frame. This is why CFP retries on `0x6b`/timeout.

## 4. Entries that reference the counter

String scan of all 81 extracted images:

- `BOOTCTR_FCTRY`: `LDR_01, 13, 14, 15, 22, 24, 28, 29, 44, 47, 49`
- `BOOTCTR_SIG` (a second, newer record): `LDR_13, 14, 15, 22, 24, 29, 44, 47, 49`

No other entry references the record. All eleven also carry
`Fail to write RPMB`.

## 5. Action handling per loader

All addresses are virtual (header load address + file offset).

| Entry | Model key | Base | `0xDE` case | Action test | Setter | Decrement | Setter reachable |
|---|---|---|---|---|---|---|---|
| LDR_01 | 06001A06 | 80100000 | 80103D50 | `action != 0` → log/return | 8011E250 | 8011E1EC | no |
| LDR_13 | 04002607 | 80100000 | 80103BE8 | `action != 0` → log/return | 8011F240 | 8011F1B8 | no |
| LDR_22 | 0500260A | 80100000 | 80103BE8 | same as LDR_13 | 8011F240 | 8011F1B8 | no |
| LDR_24 | 0600260A | 80100000 | 80103D60 | `action != 0` → log/return | 8011E988 | 8011E924 | no |
| LDR_28 | 07001A06 | 80100000 | 80104060 | `action != 0` → log/return | 801208A8 | 80120844 | no |
| LDR_44 | 1C000807 | 80100000 | 80103D5C | `action != 0` → log/return | 8011DFEC | 8011DF88 | no |
| LDR_47 | 1F000807 | 80100000 | 80103D60 | same as LDR_24 | 8011E988 | 8011E924 | no |
| LDR_49 | 24000807 | 80100000 | 80103BE8 | same as LDR_13 | 8011F240 | 8011F1B8 | no |
| LDR_14 | 04002E07 | 80200000 | 802040F0 | `action != 0` → log/return | — | 8021E8A8 | — |
| LDR_15 | 04002E0A | 80200000 | 80204144 | `action != 0` → log/return | — | 8021E990 | — |
| LDR_29 | 07002E0A | 80200000 | 8020414C | `action != 0` → log/return | — | 8021EB8C | — |

Representative decompilation (`LDR_13`, Ghidra):

```c
if (param_1 != 0xde) goto LAB_80103e5c;
iVar8 = *(int *)(param_2 + 2);              /* action */
puVar6 = FUN_8011940c();                    /* device metrics */
... device/mode checks ...
if (uVar5 == 0) { ... local_34 = 1; }       /* reject */
else {
  if (iVar8 != 0) {                         /* <-- action 1/2 rejected */
    FUN_80134af4(DAT_801040b8, iVar8);
    goto LAB_80103e5c;                      /* no response is sent */
  }
  local_34 = 0; local_30 = 0; local_2c = 0;
  ... local_30 = FUN_8011ef78(&local_2c);   /* action 0 read */
  FUN_80127990(0xfa, seq, 0xc, &local_34);
}
```

`LDR_29` has an extra pre-check (`action == 1 && value == 3 &&
(metrics[0x84] & 0x10000)`), but its true branch still falls into the same
`if (action != 0)` rejection at `0x80204300`; it does not add a write path.

### 5.1 The setters are dead code

For every OMAP setter (`cmp value,#0x32` → read → RPMB writer), there are zero
`BL`/`BLX` callers and zero little-endian pointer references anywhere in the
image. The same is true of the decrement helpers. Only the decrement exists in
the Qualcomm builds, and it is equally unreferenced. The functions are linked
in from a shared source tree but not called by the shipped command dispatcher.

This reproduces and extends the `LDR_01` finding in `71` §7b: the record
writer works (`0x80113D9C` is reached for record index 2 at boot), but the
index-0 `BOOTCTR_FCTRY` caller is absent from every signed build.

## 6. Consequence

- There is **no pre-OS action-2 writer** in any recoverable entry of
  `loaders.dat`. Adding a `0xDE` action-2 request to a host tool cannot
  recover or provision the counter; every shipped loader will simply not
  answer.
- The general writer remains `/dev/rpmb` on a booted OS unit. The bootloader's
  `0x800090a0(1)` can provision an **absent** record at `0x32`, but it does not
  fire for `0xFFFFFFFF` (the bootloader reader returns `2`), so it does not
  help the expired unit.
- Read (action 0) works; mode switch (action 1) and set (action 2) do not.
  `0xDE` is read-only in practice.
- Boot-flow safety from the handoff is unchanged: a valid factory counter on
  the normal path still reaches the nuke branch (`0x80014a16`); the CRC path
  still needs the NVRAM `0x2801` recovery condition.

## 7. Host-side support added

The protocol is now wired into both tool stacks, primarily so a unit with a
different/patched loader can be probed, and so the loader's status/mode/count
are available without CFP:

- `bb10mt-main/bbloader.pas`
  - `LOADER_CMD_BOOT_MODE = $DE`, action constants, `TBootModeInfo`
  - `TBBLoader.BootMode(Action, Value): TBytes`
  - `ParseBootModeResponse`, `BootModeStatusText`, `BootModeValueText`
- `bb10mt-main/uflash.pas`
  - `info` now prints `Boot Mode` + counter via a read-only `0xDE` action 0
  - `bootmode` command: default reads; `--factory`/`--product` switch mode;
    `--set-count N` sets the counter. Writes require `--force` and print the
    "no shipped loader implements this" warning.
- `bbusb/bbusb.py`
  - `CMD_BOOT_MODE`, `BOOT_MODE_*` constants and status/mode tables
  - `boot_mode()`, `parse_boot_mode()`, `read_boot_mode()`,
    `set_boot_mode()`, `set_boot_count()`
  - `hash_pass_v2()` and the `0x0E`/`0x0F` bootrom password challenge;
    `--password` (or an interactive prompt) unlocks password-protected units
  - `--debug` prints the raw `GetVar`/`set_mode`/`password_info` responses

A timeout is surfaced as an empty response, which is the loader's real
behaviour for an unsupported action.

## 8. Reproduction

Scripts and per-loader decompilations are in
`analysis/loader-audit-2026-10-21/`; the entry inventory is `loaders.csv`.
Run the commands from that directory.

Parse and extract all entries:

```
python3 parse_loaders.py     # TMyArcReader layout + lzma.FORMAT_ALONE
```

Locate and disassemble the `0xDE` case (Capstone, ARM):

```
python3 scan_de2.py LDR_13   # finds cmp r0,#0xde in the dispatcher chain
python3 disarm.py loaders/LDR_13 80100000 80103be8 arm 80
```

Authoritative decompilation (Ghidra headless):

```
analyzeHeadless ghidra_proj pb29 -import loaders/LDR_13 \
  -processor ARM:LE:32:v7 -loader BinaryLoader -loader-baseAddr 0x80100000 \
  -scriptPath ghidra_scripts -postScript FindDisp.java LDR_13-de.txt
```

`FindDisp.java` decompiles every function containing `CMP #0xde`; the
dispatcher is the one with the `0xDA/0xB3/0xDE` compare chain.
