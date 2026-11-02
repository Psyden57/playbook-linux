# 66 — PlayBook ramloader opcode map and Sachesi hidden modes

Status: static analysis of the signed PlayBook ramloader, cross-checked against
bb10mt and Sachesi source.
Date: 2026-10-20.
Target: `/home/psyden/rea-work/loader_06001A06-00.bin`
- 238,128 bytes, raw ARM, load base `0x80100000`
- SHA-256 `c7cc33c796f9518bbc0eb929c6096f2473937ad486c579764aa4c57d8165393f`
- REA 6.7.0 / Ghidra 12.1.4: one segment `0x80100000`-`0x8013A230`,
  **988 procedures**, 92 defined strings.

Provenance:

- **REA Evidence (Ghidra 12.1.4)** for the loader disassembly/pseudocode.
  Evidence IDs are in §9. The portable bundle is
  `analysis/rea-ghidra/loader-evidence-bundle-2026-10-20.json`
  (26 records, 238,921 bytes, 0 unknowns); the machine-readable opcode table is
  `analysis/rea-ghidra/loader-opcodes.csv`.
- `bb10mt-main/` and `Sachesi-2.0.3` sources are **source-code context**, not
  target Evidence. They are used to name the opcodes and to locate the hidden
  UI paths.
- No device writes were performed for this document. The loader target is
  independent of the boot ROM dump; no annotations were persisted.

## 1. The main router

The main loop is `FUN_80116e28`. It reads an inner channel-2 frame and switches
on the byte at frame offset `+2` (the low byte of the `u16` command word); the
high byte at `+3` is the mode/sequence byte passed back in responses:

```
FUN_80125318() -> 0/1/2/3
  3 -> respond 0x15/0x11 (busy/error)
  0 -> idle/poll
  1 -> command frame: bVar1 = *(u8 *)(frame + 2)
```

Unknown opcodes fall to `switchD_801173dc_default`, which logs and returns
`0x15`. The router dispatches to four functions:

| router | dispatcher | role |
|---|---|---|
| `0x801032ac` | query/info | info blobs, logs, hashes, NVRAM reads |
| `0x80103b90` | actions | NVRAM/security/OS-region/LED |
| `0x8011ae30` | erase/write/complete | erase, stage, complete, checksum |
| `0x80102dc4` | CREAD | `0xE4`/`0xE5`/`0xE6`/`0xEB` and `0x39`/`0x3A` |
| direct | router | `0xEF` reboot, `0xF7`/`0xF8` write, `0xF9` signature |

## 2. Implemented opcode map

Response codes are the values observed in the decompiled `FUN_80125424`
calls. "Status" is relative to the bb10mt/PLAYBOOK-REFERENCE opcode table.

| op | dispatcher | response | behavior (observed) | status |
|---:|---|---:|---|---|
| `0x20` | `0x801032ac` | `0x39` / `0x15` | persistent data; magic `0x03C6806C`/`0x36159469` at +4/+8; returns data if the magic matches | known |
| `0x39` | `0x80102dc4` | `0x38` / `0xE7` | CREAD block read: `FUN_80111ed8` reads a flash block into a 0x4000 buffer, `FUN_80104984` encrypts/copies it; requires an E4 session | **undocumented in the table** |
| `0x3A` | `0x80102dc4` | `0x20` / `0x11` | CREAD block-read variant; requires a length >= 0x4001, then errors 0x11 | **undocumented** |
| `0xAD` | `0x801032ac` | `0x33` (8 B) | PMIC info | known |
| `0xAE` | `0x80103b90` | `0x2E` / `6` | blocklist erase gate `FUN_80104180` (RIM-signed payload) | known |
| `0xAF` | `0x80103b90` | `0x29` | `*DAT_80118A00 = arg` (mode flag consumed by F7/FD) | known |
| `0xB1` | `0x801032ac` | `0xC8` (0x3FF0) | flash IDs (`FUN_8011A7A0`/`FUN_8011A784`) | known |
| `0xB2` | `0x801032ac` | `0xC8` (0x3FF0) | DSP OS metrics | known |
| `0xB3` | `0x80103b90` | `0xB4` | NVRAM **0x2033** action-log append/read (`FUN_80118688`) | known (bb10mt calls it "GetLAL" - wrong) |
| `0xB4` | `0x801032ac` | `0xD2` (0xBC) | flash regions info | known |
| `0xB5` | `0x801032ac` | `0xD3` (0x1EC) | flash info | known |
| `0xB7` | `0x801032ac` | `0xF3` (0x14) | 20-byte hash of a bootrom/firmware image (`FUN_8011874C`) | known |
| `0xBD` | `0x801032ac` | `0xBE` (8 B) | hardware-override ID | known |
| `0xBF` | `0x801032ac` | `0xD9` (0x24) | DRAM info | known |
| `0xC0` | `0x8011ae30` | `6` / `0x14` / `0x15` / `0x2E` | COMPLETE: writes NVRAM **0x2074** `{2,0,byte}`, requires `0x207C`, conditional signature check | known |
| `0xC2` | `0x801032ac` | `0xC2` (0x20) | 0x20-byte status/debug query (`FUN_80117A00`) | **undocumented** |
| `0xC3` | `0x80103b90` | `6` | LED/PWM control (`FUN_8013241C`, `FUN_801247A0/74C/774`) | known (reference says "log cfg" - wrong) |
| `0xC4` | `0x80103b90` | `0x28` | READVERIFY: erase/fill a flash range and compare | known |
| `0xD1` | `0x801032ac` | `0xC8` (4 B) | 4-byte SVN (`FUN_801180B8`) | **undocumented** |
| `0xD2` | `0x801032ac` | `0xC8` (0x3FF0) | kernel/app metrics (`FUN_80117FBC`) | known |
| `0xD3` | `0x801032ac` | `0xC8` (0x3FF0) | kernel/app metrics variant (`FUN_80117DD8`) | known |
| `0xD8` | `0x801032ac` | `0xC8` (0x3FF0) | OS metrics (`FUN_80117A4C`) | known |
| `0xD9` | `0x801032ac` | `0xC9` (u16 len) | MCT (`FUN_8011814C`) | known |
| `0xDA` | `0x80103b90` | `0xCA` / `0x15` | NVRAM **0x2027** read/delete (`FUN_8011DEAC`) | **undocumented** |
| `0xDB` | `0x801032ac` | `0xCB` (4 B) | vendor ID (`FUN_80102C44`/`FUN_80102CF0`) | known |
| `0xDC` | `0x8011ae30` | `0xCC` / `0xCE` | checksum/hash over a flash range (`FUN_80100118`/`FUN_80101F00`) | known |
| `0xDE` | `0x80103b90` | `0xFA` | security/RPMB query (`FUN_8011E074`) | **undocumented** |
| `0xE0` | `0x80103b90` | `0xC3` | installer/OS-region operation; reads NVRAM **0x206C** and calls `FUN_80119538` | known (bb10mt "RemoveInstaller") |
| `0xE1` | `0x801032ac` | `0xC8` (0x3FF0) | 0x3FF0-byte log/crash buffer (`FUN_80117C08`) | **undocumented** |
| `0xE4` | `0x80102dc4` | `0xE8` | CREAD init: reads host material 0x20/0x10/0x20/0x100, sets up crypto, `+0xAEC=1` (`FUN_80104970`) | known (RSA-gated) |
| `0xE5` | `0x80102dc4` | `0xE9` | CREAD read: flash page read + crypto copy (`FUN_80100118`/`FUN_80119E0C`/`FUN_80104984`) | known (RSA-gated) |
| `0xE6` | `0x80102dc4` | `0xEA` | CREAD variant (`FUN_80104BB0`) | **undocumented** |
| `0xE7` | `0x801032ac` | `0xD1` (4 B) | PIN (`FUN_80102668`) | known |
| `0xE8` | `0x80103b90` | `6` / `0x2E` | security bitmap NVRAM **0x2814**, bit 4 set/clear (`FUN_8010288C(4,1)`) | known |
| `0xE9` | `0x80103b90` | `0xFB` (0x18) | read NVRAM **0x2074** (OS boot region record) | known |
| `0xEA` | `0x801032ac` | `0xFC` (4 B) | BSN (`FUN_8011DC60`) | known |
| `0xEB` | `0x80102dc4` | `0xEC` | CREAD variant (`FUN_80104D08`) | **undocumented** |
| `0xEC` | `0x801032ac` | `0xFD` (0x7C) | blocked OS list (`FUN_8011859C`) | known |
| `0xED` | `0x801032ac` | `0xFE` (0x7C) | blocked radio list (`FUN_801185F0`) | known |
| `0xEE` | `0x8011ae30` | `0xDE` / `0xD4` / `0x15` / `0x2E` | erase sector, address-driven (`FUN_801001C8`) | known |
| `0xEF` | direct | `0xC7` | reboot (`0x80EF` -> `0x80C7`) | known |
| `0xF3` | `0x8011ae30` | `0xF4` | EraseMCT (`FUN_80118644`) | known |
| `0xF7` | direct | `0xDF` | write data block (`FUN_8011FDD8`) | known |
| `0xF8` | direct | `0xDF` | write data block (`FUN_8011FDD8`) | known |
| `0xF9` | direct | `0x4006` / `0x402E` | 560-byte signature trailer (`FUN_801205CC`) | known |
| `0xFD` | `0x8011ae30` | `0xDF` / `0x11` / `0x15` / `0x16` | stage/append write (`FUN_8011AE30`) | known |
| `0xFF` | `0x801032ac` | `0xFF` (0x78) | DDR info (`FUN_80103258`) | known |

### 2.1 Not implemented in this loader build

These fall to the router's unknown-command path and return `0x15`:

```
0xDD, 0xDF, 0xE2, 0xE3, 0xF0, 0xF1, 0xF2,
0xF4, 0xF5, 0xF6, 0xFA, 0xFB, 0xFC, 0xFE
```

This is an important correction to the client-side table: `0xDD` is *not*
implemented (matching the earlier "FLASH_DUMP silently ignored" observation),
and the `0xF0`/`0xF1`/`0xF2`/`0xF4`/`0xF5`/`0xF6`/`0xFA`/`0xFB`/`0xFC`/`0xFE`
entries are **not handled by the PlayBook loader**. The bb10mt table is a
superset covering other BB10 builds.

## 3. The interesting handlers

### 3.1 `0x39`/`0x3A`/`0xE4`/`0xE5`/`0xE6`/`0xEB` — CREAD, still RSA-gated

`FUN_80102dc4` is the CREAD dispatcher:

- `0xE4` (`FUN_80104970`) clears a 0xAF0-byte session, reads host material
  (0x20/0x10/0x20/0x100 bytes), calls `FUN_801292DC`/`FUN_8012A278`, and sets
  `+0xAEC=1`.
- `0xE5` (`FUN_80100118` bounds-check, `FUN_80119E0C` page read,
  `FUN_80104984` crypto copy) reads flash pages into the response.
- `0x39` calls `FUN_80111ED8` to read a flash block (sector count × 512) into a
  0x4000 buffer, then `FUN_80104984` to encrypt/copy it.
- `0x3A` is a read variant that rejects lengths below 0x4001.
- `0xE6`/`0xEB` are further CREAD variants (`FUN_80104BB0`/`FUN_80104D08`).

The gate is `FUN_80104984`: it only proceeds when
`*(u8 *)(DAT_80104D4C + 0xAEC) == 1`, which only `0xE4` sets. Therefore
`0x39`/`0x3A` are **not** a free raw-flash read; they need the E4 session and
the host-side key material. The earlier conclusion in `PLAYBOOK-REFERENCE`
§4.6 stands: CREAD dumps are RSA-gated.

### 3.2 `0xB7` — `HASH_BOOTROM` returns a 20-byte hash, not bytes

`FUN_801032AC` case `0xB7` calls `FUN_8011874C` and returns 0x14 bytes
(response `0xF3`). `FUN_8011874C` parses the loaded image/ROM descriptor at
`DAT_801181A0`, validates the magic at `+0x34`, and hashes the regions with
`FUN_801023D8`/`FUN_80102010`/`FUN_801024D0`. It is a SHA-1-class digest, not a
memory-read primitive. It cannot be inverted to recover the hidden 4 KB page.

### 3.3 `0xDA` — NVRAM record `0x2027`

`FUN_80103B90` case `0xDA` writes the first payload byte to `DAT_801040CC`,
then `FUN_8011DEAC` reads NVRAM record **0x2027** (16 bytes) into that buffer.
If the first byte is `1`, it deletes/invalidates record 0x2027
(`FUN_8010B7B8(0x2027)`). Response `0xCA`. This is a record-specific
read/delete primitive, not arbitrary NVRAM.

### 3.4 `0xE8` — security bitmap `0x2814`

`FUN_8010288C(4,1)` reads NVRAM record **0x2814** (128 bytes), sets bit 4, and
writes it back. This is the documented `WIPE_SECURITY` path; it does **not**
clear the OS blocklist (`0x2819`).

### 3.5 `0xE9`/`0xC0`/`0xE0` — the OS-region records (`0x2074`/`0x206C`)

This is the most relevant area for CFP's undocumented `resetqnxosregion`
command.

- **`0xE9`** reads NVRAM record **0x2074** (24 bytes) into response `0xFB`:
  `FUN_8010B954(0x2074, buf, 0x18)`. It is read-only and safe.
- **`0xC0` COMPLETE** (`FUN_801206C8`) writes NVRAM record **0x2074** with
  `{u32 2, u16 0, u8 <first byte of the complete payload>}` (`local_fc =
  *param_2`). It requires the image write cursor at `0x20000` and NVRAM record
  **0x207C** to be present; if 0x207C is missing it returns `0x14`. The
  signature check is the conditional one already documented (`FUN_801184AC`
  / `FUN_8011E0F8` skip path). This is a controlled write of the OS-region
  byte.
- **`0xE0`** (`FUN_801199D4`) is the installer/OS-region operation. It reads
  NVRAM record **0x206C** (0x400 bytes), checks device-info IDs 0x1F/0x1B/0x1C/
  0x24, and calls `FUN_80119538`, which parses a `0x400`-byte installer/region
  table and writes updated region state. Response `0xC3`.

**Lead:** `resetqnxosregion` almost certainly manipulates the `0x2074` /
`0x206C` family. The safe first step is `0xE9` (read 0x2074) on a booted unit.
The write path is `0xC0` (or `0xE0` for the installer table); both are flash/
NVRAM-writing operations and should only be used with a full backup and a
recovery plan. A `0xC0` COMPLETE with a controlled first byte is the most
direct way to change the region flag.

### 3.6 `0xE1`/`0xC2`/`0xD1` — logs and status

- `0xE1` (`FUN_80117C08`) returns a 0x3FF0-byte log/crash buffer; it parses a
  ring/descriptor at `DAT_801181A0`, copies a 0x75-byte header and reads
  pointer/length pairs. This is likely the bugdisp/crash log.
- `0xC2` (`FUN_80117A00`) returns a 0x20-byte status block from
  `FUN_80122010`/`FUN_80121F8C`.
- `0xD1` (`FUN_801180B8`) returns a 4-byte SVN.

### 3.7 Label corrections

| opcode | client label | loader behavior |
|---:|---|---|
| `0xB3` | bb10mt "GetLAL" | NVRAM `0x2033` action-log append/read (`FUN_80118688`); the reference table's "loader action log" is correct |
| `0xC3` | reference "log cfg" | LED/PWM control (`FUN_8013241C`, `FUN_801247A0/74C/774`); bb10mt's "EnableLED" is correct |
| `0xAF` | "write DWORD to 8023977C" | `*DAT_80118A00 = arg`; the mode flag is at the loader's `0x80118A00`-based global, consumed by F7/FD |
| `0xDA` | "HIS (rec 0x2027)" | read/delete NVRAM `0x2027` (`FUN_8011DEAC`) |
| `0xE9` | "boot count / OS region info" | read NVRAM `0x2074` (24 bytes) |
| `0xC0` | "COMPLETE" | writes NVRAM `0x2074` `{2,0,byte}` and requires `0x207C` |

## 4. Sachesi hidden modes

Sachesi 2.0.3 exposes four `Boot` commands (`src/boot.h`):

```
commandInfo = 1, commandBootloader = 2, commandNuke = 3, commandDebug = 4
```

The QML `qml/generic/Boot.qml` buttons map directly:

| UI button | command | what the source actually does |
|---|---:|---|
| Info | 1 | set mode 1 (`RIM-BootLoader`), `GetVar(2)` metrics, write `info.txt` |
| RimBoot | 2 | set mode 1, `commandSendPass`, `commandSendLoader` (upload the ramloader) |
| Nuke | 3 | set mode 1, then mode 4 (`RIM-BootNUKE`, wipe user partition) - destructive |
| Debug Mode | 4 | set mode 1 **and set `_found = 8000`** |

The "Debug Mode" is a **transport switch**, not a console:

- `sendControlMessage` uses endpoint `_found == 0x1 ? 0x2 : 0x1`.
- `receiveControlMessage` uses endpoint `_found == 0x1 ? 0x82 : 0x81`.
- `_found = 8000` therefore switches the control channel from **EP2 OUT/EP2 IN**
  (`0x02`/`0x82`) to **EP1 OUT/EP1 IN** (`0x01`/`0x81`).
- The mode-set command itself is sent on EP1, so the bootrom sees the
  `RIM-BootLoader` mode request on the alternate endpoint.
- The `ControlMessageCommand` enum defines `kCommandUnkn1 = 0x11` /
  `kCommandUnkn1Response = 0x12`, but **nothing in Sachesi 2.0.3 uses them**.
  There is no debug shell or extra UI behind the button.

So the button is a lead, not a working tool: it selects the bootrom's
alternate EP1 interface. A minimal next test is to send `0x11` (and the normal
ping/get-variable) on EP1 and capture the response. Do this only in bootrom
mode on a unit that can be rebooted.

The `bootloaderModeData` array has five modes:

```
0 RIM REINIT, 1 RIM-BootLoader, 2 RIM-RAMLoader,
3 RIM UPL,     4 RIM-BootNUKE
```

Mode 2 (`RIM-RAMLoader`) is defined but never selected by the UI. The
"RimBoot" button is command 2, which sets **mode 1** and uploads the ramloader;
it is not mode 2. The `commandSendLoader` packet is the 16-byte
`01 00 10 00 7B 09 2B 96 0C 00 00 00 00 00 01 F0` start record, sent on EP2.

Sachesi 2.0.3 has **no `resetqnxosregion` implementation**. That is a CFP.exe
command; the loader-side machinery is the `0x2074`/`0x206C` family above.

## 5. bb10mt/client extras

`bb10mt-main/bbLoader.pas` implements additional client wrappers:

```
$CB GetOSBoot          $F3 EraseMCT          $C3 EnableLED
$B3 GetLAL             $BD HWID_OVERRIDE     $BF DRAMInfo
$D9 GetMCT             $D8 OSMetrics         $E0 RemoveInstaller
$C8 GRS_Wipe           $80EF Reboot
```

`bbusb.pas` also defines bootrom commands `CMD_X1 = 0xF00C` and
`CMD_X2 = 0xF00D`, but no function in bb10mt calls them. A byte search of the
visible lower boot ROM dump (`bootrom-lower-0x40020000.bin`) finds **no**
`0xF00C`/`0xF00D` constants. They are therefore either:

1. in the hidden 4 KB page `0x40027000`-`0x40027FFF` (unreadable from NS), or
2. in the RIM bootrom stage on eMMC (`hd0` offset `0x14900`), which is not in
   the current workspace's bootchain dump set.

If you can provide the RIM bootrom stage dump, those two opcodes can be traced
the same way. The `bbusb/bbusb.py` client carries the same constants but is
incomplete/broken; `bb10mt` is the working reference.

## 6. Safe next probes

1. **Read-only first:** in loader mode, send `0xE9` (NVRAM 0x2074), `0xE7`
   (PIN), `0xEA` (BSN), `0xD1` (SVN), `0xE1` (log), `0xC2` (status), `0xB7`
   (hash). These do not write flash/NVRAM. Capture and archive the responses.
2. **OS-region lead:** compare the `0xE9` 0x2074 record on a known-good unit
   and after a CFP `resetqnxosregion` run. Then inspect `0xC0`/`0xE0` on a
   backed-up unit only.
3. **Debug endpoint:** in bootrom mode, test EP1 (`0x01`/`0x81`) with the normal
   ping/get-variable and with `kCommandUnkn1 = 0x11`. Do not send `0xC8`/
   `0xC6`/`0xD4`/`0xD5`/`0xE8`/`0xEE`/`0xF3`/`0xFD` without a recovery plan.
4. **Do not expect CREAD to be free:** `0xE4`/`0xE5`/`0x39`/`0x3A` all require
   the E4 session and host key material. The RSA gate stands.

## 7. Evidence index

| operation | Evidence ID |
|---|---|
| `binary_overview` (loader) | `ev_c33aa462c83ff47d0d9ae0d25d2fbda503740107ba316350d18e273edeaf4776` |
| `procedure_pseudo_code 0x80116E28` (main router) | `ev_c4721f77f011170f5dabc9cc4b4e347dab5ac837884381aff8def462f3cf4a1c` |
| `procedure_pseudo_code 0x801032AC` (query) | `ev_3868cd6defee7b06087fed82192bb60539d74cadf3f4276c9ce8c3d23b936b07` |
| `procedure_pseudo_code 0x80103B90` (actions) | `ev_a0e25ccd8122e75321ff90ba9696b8762239709ac9d44c7f609756eb789d1b72` |
| `procedure_pseudo_code 0x8011AE30` (erase/write) | `ev_8b76007a23e7e7df16091e1c2a425fd2f0b7bd57990cbdd4ae5988cab945b485` |
| `procedure_pseudo_code 0x80102DC4` (CREAD) | `ev_adf909099a8d1c4026c90cff95f43f532e65103500b149a86651419d14e5eb13` |
| `procedure_pseudo_code 0x80104970` (CREAD init) | `ev_f7ab5b1bc6e538b204a4c9faf3d8f0c5e6c91debd6b0ab25beccb5f0eb7169cd` |
| `procedure_pseudo_code 0x80104984` (CREAD gate) | `ev_c5b5b6b7ece466d05cc604992cc5e8274d40a09f17ce6e279b589a1d8853866f` |
| `procedure_pseudo_code 0x80111ED8` (flash block read) | `ev_f854d51ca5a7dcaa9d62ae0b2f18100b84ea121256c464e69a01c52a215aef86` |
| `procedure_pseudo_code 0x8011DEAC` (NVRAM 0x2027) | `ev_a45a933a835c6432f5660d0a9a667a675c40bc2ed1b1e840bd91ce3f32c8e044` |
| `procedure_pseudo_code 0x8010288C` (NVRAM 0x2814) | `ev_a76b98af470f10167008d03836b2f1fc4536a9714a4563e7f6ea69d1f48918a4` |
| `procedure_pseudo_code 0x801206C8` (COMPLETE / 0x2074) | `ev_9b4fca74e27ef7fff7873c489d06453809479a72bd92a43ddf342bec31d09ce8` |
| `procedure_pseudo_code 0x80119538` (OS region table) | `ev_5d7126e39c75bd02b685dbda89eeb0864c58390f9fadc79150b4dc554c9229b6` |
| `procedure_pseudo_code 0x801199D4` (installer/region) | `ev_ff76fb14ebc1d32a6ed1aa11df9b1163631df7904c05ac777f485a1da6d104b5` |
| `procedure_pseudo_code 0x8011874C` (hash) | `ev_4306d10e985a530e9f1cb739e70009a8a07d1ef5609db03c9edf96170c610180` |
| `procedure_pseudo_code 0x80118688` (0x2033 log) | `ev_5ead9f21450402200a6c8eb467dbeb439868bb9e8a715fd182f85693e1e990a6` |
| `procedure_pseudo_code 0x801189E8` (0xAF flag) | `ev_5f71f02d42ef7e3fc6aedf125f4987d2c632002f691336c47b76b4c0c962801d` |

Source-code context (not target Evidence): `bb10mt-main/bbLoader.pas`,
`bb10mt-main/bbusb.pas`, `bb10mt-main/ramloader.pas`, `bb10mt-main/uflash.pas`,
`bb10mt-main/uinfo.pas`, `bbusb/bbusb.py`, Sachesi 2.0.3 `src/boot.h`,
`src/boot.cpp`, `qml/generic/Boot.qml`.
