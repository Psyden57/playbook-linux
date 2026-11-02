# 161 — CFP `info` / `flashinfo` baseline for the reference unit

Date: 2026-11-01
Unit: reference, PIN `50175127` (BSN `1063035517`)
Tool: CFP 3.10.0.22 (Win32), Feb 20 2013 — run from Windows by the operator
Raw dumps: `docs/reports/data/cfp-2026-11-01/`
(`CFP-info.txt` = `help` + `info`; `CFP-info-V5.txt` = `info -V5` trace;
`CFP-flashinfo.txt`, `CFP-flashinfo-V5.txt`)

CFP is the only tool that prints this unit's info correctly — `bb10mt info` is
mostly wrong for the PlayBook (operator, 2026-11-01).  These dumps are therefore
the **baseline** any replacement (`tools/bbusb.py info`) must reproduce.

## 1. The armed state — finally recorded

```
Boot Mode:         Factory (Remaining boot count: 50)
Boot OS Count:     383,0,0 (Region0,Region1,Max)
                   3,0,0 (Version,Requested Region,Actual Region)
```

`ACTUAL REGION = 0`, and `BOOTCTR_FCTRY = 50`, mode `FACTORY` (mode id 1).
This agrees exactly with our own loader probe (report 100: `0xDE` action 0 →
`status 0, mode 1 = FACTORY, count 0x32 = 50`) — **two independent sources**, one
through RIM's tool and one through our own `bbusb` path.

These are the same numbers session 21 measured the hard way in **report 160**
(`BOOTCTR_FCTRY = 0x32`, mode Factory) — this dump corroborates them through
RIM's own tool, and adds the `Boot OS Count` line (region0 = 383, region1 = 0:
region 1 has never been booted from).

## 2. What is in region 0 right now

```
OS Version:            2.0.0.4869 DEV
    Hardware ID:       0x06001A06 RIM BlackBerry Device
    Metrics Version:   3.18
    FS Code Version:   0.0.0.0
    Build Date:        Nov 15 2011
    Build Time:        14:51:36
    OS Address:        0x800FF800-0x803BF60F
```

**Region 0 holds 4869**, not 1917.  The loader action log confirms the load and
its date:

```
Sun Aug 30 05:41:38 2026:3.11.0.27:DONE:QNXLOAD:2.0.0.4869 DEV-ErrorCode:0-Success!
Sun Aug 30 05:38:10 2026:3.11.0.27:QNXLOAD:2.0.0.4869 DEV
```

Consequence for the pending flash: replacing region 0 with the **resealed 4869
IFS** (`PB-4869-root.0.3-crc.ifs`) is **release-consistent with the IFS** — same
OS release as the one in region 0 now.  What is *inferred, not measured*: that the
`/base` (RCFS) currently on the unit is also the 4869 one.  The LAL shows a
successful 4869 load on 2026-08-30 preceded by 2.1.0.1917 and 10.0.9.1103 loads,
so the RCFS could in principle be from any of them — read its version before
treating the pair as matched.

## 3. Bootrom / hardware

```
Bootrom Version:    5.27.0.20          Hardware ID:  0x06001A06
BR ID:              0xFF000000         Metrics Ver:  6.24 (Mar 24 2011, ec_agent)
Security:           Enabled            Supported Options: 0x00000009
Drivers:            0x00000010         Processor:    0x00000020
LDR Blocks:         0x80014674         Bootrom Size: 0x00020000
External MCT/HWV:   Enabled
JTAG SW Support / RPMB Support / WP Mode / Slack Mode: Unknown
OS Blocking by Date: Not supported
```

* `Security: Enabled` — the device is on the **secure** (ECDSA) path, consistent
  with the factory/CRC hand-off that the arming procedure performs.
* `External MCT/HWV: Enabled` — the bootrom has an external MCT/HWV source.  This
  is relevant to the open path-2/`0x39` question
  (`docs/contradictions/100-bb10mt-flash-path2-vs-operator-flashes.md`): it is
  the "external MCT" hypothesis of report 98 **U1**, now confirmed to be *enabled*,
  though it still does not show where `0x39` entries would live.

HWV: board rev `0x01-0x01`, CPU ver `0x02-0x74`, SW compat `0x03-0x01`,
POP rev `0x04-0x01`, power mgmt `0x07-0x3E`, POP security `0x0B-0x02`,
RAMImage compat `0x0E-0x01`.

## 4. Mem Config Table (ver 1.24) — matches our `0xD9` read exactly

```
MCT:          block 0            MFG Data:   blocks 1-4
Bootrom:      blocks 5-12        MBR:        blocks 2053-2068
OS Fixed:     blocks 2069-18436  OS NV:      blocks 18437-18500
Branding:     blocks 18501-18692 FS Fixed:   blocks 18693-1048575
MMC Enhanced: blocks 5-2052
QNX region 0: blocks 2069-10244  (total 8176)
QNX region 1: blocks 10261-18436 (total 8176)
CRC: 0x202C0AE2
```

Cross-check against report 100's TLV dump (same numbers, decimal vs hex):

| type | name | CFP (decimal) | ours (hex) |
|---|---|---|---|
| `0x34` | MBR | 2053-2068 | `0x805`-`0x814` |
| `0x1B` | OS Fixed | 2069-18436 | `0x815`-`0x4804` |
| `0x1E` | OS NV | 18437-18500 | `0x4805`-`0x4844` |
| `0x1C` | FS Fixed | 18693-1048575 | `0x4905`-`0xFFFFF` |
| `0x35` id 0 | QNX region 0 | 2069-10244 | `0x815`-`0x2804` |
| `0x35` id 1 | QNX region 1 | 10261-18436 | `0x2815`-`0x4804` |

**No `0x39` entry appears in CFP's renderer either.**  Weaker evidence than our
`0xD9` dump, and worth stating precisely: CFP prints a *named summary* of the
types it knows (`0x1D/0x31/0x18/0x34/0x1B/0x1E/0x36/0x1C/0x35`), so a type it does
not name would not appear at all.  It is therefore consistent with report 100, not
independent proof.  If path 2 matters for the bb10mt question, do the raw TLV walk
(§7) rather than reading this summary.  (CFP's `LOAD` is a different code path and
is not evidence either way.)

## 5. Flash (eMMC) — `flashinfo`

```
Vendor: Samsung  MCGAFA   Product revision 0x10   Serial 0xA81DE12B
Manufacturing date: Year 2011, Month 3
Blocks: 973317 * 64 KiB    Total size: 60832 MB
MMC partitions: Boot0 512 KB, Boot1 512 KB, User 131072 KB (enhanced), User 62160896 KB
```

ExtCSD fields that matter to us:

| field | value | why it matters |
|---|---|---|
| `SECTOR_COUNT` | `0x76D0000` | total sectors |
| `ENH_SIZE_MULT` | `8,0,0` | current enhanced area = 8 × 128 KiB × 8 ≈ 8 MiB |
| `MAX_ENH_SIZE_MULT` | `0x72E` | headroom for growing the enhanced area (the `hd3` idea) |
| `RPMB_SIZE_MULT` | `1` | 128 KiB RPMB — the 512-block device |
| `PARTITION_SUPPORT` | `3` | enhanced + extended user area |
| `PARTITION_SETTING_COMPLETED` | `1` | already committed |
| `BOOT_SIZE_MULT` | `4` | 256 KiB boot partition |

**Write protection table** — this is new information:

```
Start  End     Partition   Type
0      0       Boot 0      None
1      4       Boot 1      None
5      260     User        Permanent
261    973317  User        None
```

Sectors 5-260 of the user area are **permanently write protected**: that covers
the MCT, MFG data and bootrom copies (blocks 0-12 in 64 KiB units).  Nothing in
our flashing plans touches that range, but it explains why the boot area cannot
be clobbered by the loader even by accident.

## 6. CFP's command surface (from `CFP help`)

```
HELP, INFO, FLASHINFO, FILEINFO, LOAD, SAVEFS, CSAVEFS, WIPE, ERASE,
READ, CREAD, WRITE, USBMS, NUKE, FRESH, HASHBOOTROM, PATRIOT, GETLOG, READVERIFY
```

`RESETQNXOSREGION` is **not listed** — it is an undocumented command (its string
and the message `QNX OSREGION NVRAM record cleared.` are in `CFP.exe`, see
`docs/contradictions` / the `bbusb` extension plan).  `READ`/`WRITE`/`READVERIFY`
are present, which is what any future `bbusb` read-back verification should
mirror.

## 7. CFP's `info` retrieval sequence — the op map for `bbusb.py info`

From the `-V5` trace, in order:

```
Flash Regions Info      -> 0xB4   (bbusb: flash_regions_info)
MCT                     -> 0xD9   (probed in report 100, 240 B, magic 0x92BE564A)
Loader Action Log       -> 0xB0   (bbusb: bugdisp_log)
Hardware Override ID    -> ?
DRAM Info               -> ?
Installer Metrics       -> ?
DSP OS Metrics          -> ?
OS Metrics              -> ?
Boot Mode               -> 0xDE   (bbusb: read_boot_mode)
boot count info         -> ?
Vendor ID               -> ?
Flash Info (CSD/ExtCSD) -> ?
```

Also visible: CFP sends `0xF00D` on the bootrom and gets *"UNKNOWN Command
(0xF00D)"* — matching `CMD_X2` in `bbusb.py`; harmless, CFP continues.

The first four plus `0xDE` are enough for a useful `info` today; the `?` ops need
either `CFP.exe` static RE or another loader probe.

> **Update (same day, reports 162/164).**  All eight `?` entries are now measured,
> from two independent directions that agree: CFP.exe's constants (`0xBD→0xBE`
> HW override, `0xBF→0xD9` DRAM, `0xE1→0xC8` installer metrics, `0xB2→0xC8` DSP OS
> metrics, `0xD8→0xC8` OS metrics, `0xE9→0xFB` NVRAM `0x2074`, `0xDB→0xCB` vendor
> ID, `0xB5→0xD3` CSD/ExtCSD), and LDR_01's dispatcher at `0x8011737C`.  Two
> corrections to the table above: DSP OS metrics (`0xB2`) is a **stub** in LDR_01
> (`0x80117DBC` memsets and logs) so it always returns zeros, and `0xE9` is not a
> counter read — it reads the 24-byte NVRAM record `0x2074`, the same record that
> `0xE8 arg 4` (`RESETQNXOSREGION`) deletes and that flash `COMPLETE` writes.
> `tools/bbusb.py info` now issues all eight; see `DEVELOPMENT.md` §10.

## 8. Consequences

1. **Armed state is measured**: Factory, 50 remaining, actual region 0.  Record it
   in `STATE.md`/`HANDOFF.md` (done in this commit).
2. **The IFS flash is release-consistent**: region 0 currently holds 2.0.0.4869.
3. **The `0x39` question is closed — by the loader, not by CFP** (report 164 §Q5):
   `0x39` is not an MCT partition type at all, it is the **CREAD opcode**
   (`0x801173B4` router, `0x80102DE8` CREAD dispatcher), and partition resolution
   goes through a fixed four-entry table at `0x80136998`.  So path 2 cannot resolve
   a partition and the only write path is path 1 — which is what a V1 `qcfp` record
   addresses.  CFP's rendered summary (§4) was consistent with this but, as noted
   there, was never proof on its own.
4. **the `info` command's scope is now concrete**: reproduce §1-§5 from the ops in §7.
