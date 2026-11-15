# Which BT service pack does the WL1283 need — linux-firmware's `TIInit_7.2.31.bts` or the reference's `WL1283.bts`?

**Status: RESOLVED (session 53, report 238 §3) — use the reference's `WL1283.bts`.**

Settled by reading both files' own headers and by what they do on the chip, not
by inference:

| file | its own header |
|---|---|
| reference `WL1283.bts` | `Description : Quattro L ROM Initialization Script`, `Compatibility: Quattro, 6.0.15 ROM`, `Version: TI_P6_15.51`, `# Quattro LMP Subversion: 2b33` |
| linux-firmware `TIInit_7.2.31.bts` (48 909 B) | `Description : ORCA-Trio BTIP 2.0 ROM Initialization Script`, `Compatibility: BL6450, WL1271/3 2.0 ROM`, `Version: TI_P31.115`, `Notes: Use this script on BL6450, WL1271/3 2.0 ROM device only (FW v2.0.31)` |

`Quattro` is this chip's BT-core generation; `BL6450 / WL1271/3 2.0` is a
different, older family — and the `TIInit_*` files fetched for comparison
(`6.2.31` too) say the same.  So claim 1 (in the original text below) is
**wrong**, and the reference's file is the correct one for this device.

Measured confirmation on the bench: replaying `WL1283.bts` over H4 gives
**315/315 commands answered** and moves the LMP subversion from `0x2b0f` to
**`0x2b33`**, which is exactly what that file's header declares.  It also fixes
the `LE_Read_Supported_States` reply length (11 → 12 bytes), which is what lets
`hci0` register with mgmt at all (report 238 §2, trap 249).

Not related: `wl128x-fw-4-sr.bin` / `wl128x-fw-5-sr.bin` are the **WLAN** core's
firmware (`request_firmware()` in `wlcore/main.c`).  Changing them cannot change
BT behaviour.

The original text is kept below for its history.

---

## Claims

1. **Use linux-firmware's `TIInit_7.2.31.bts`.**  The session-51 handoff (and the
   session-52 kickoff prompt) say to use linux-firmware's file — described as
   "ORCA-Trio BTIP 2.0" — and **not** the reference's `WL1283.bts`, which it
   calls "a 6.0.15 *Quattro L* script (`TI_P6_15.51`)".  The implication is that
   the reference file belongs to a different chip family.
2. **Use the reference's `WL1283.bts`.**  Report 216 §0 analysed the file from
   this device's own 1917 `/base` and found it is a **complete TI service-pack
   download**: 315 HCI commands, of which 227 are
   `HCI_VS_Write_Memory_Block 0xFF05` totalling **56 771 bytes** of patch RAM,
   followed by `0xFD09` ("release Cortex Reset") and `0xFDA8` ("enable Cortex
   Sleep").  That is the shape of the WL1283's own firmware, and it is what the
   stock OS on this unit ships.

## Evidence for each

* **For `TIInit_7.2.31.bts` (claim 1):** the handoff's description of the two
  files' internal version strings; `TIInit_*` is the filename pattern
  linux-firmware uses for the WL127x family.  No measurement supports it, and
  the file is **not even present** in this workspace (`find local/work -iname
  'TIInit*'` returns nothing), so it has never been tried.
* **For `WL1283.bts` (claim 2):** the file is the one the *reference OS on this
  unit* loads
  (`local/work/1917/base/lib/firmware/bluetooth/WL1283.bts`, 76 587 bytes); its
  content decodes as a full patch-RAM download plus Cortex release/sleep
  commands (report 216 §0 item 5), and the same reference's
  `devc-seromap-winbt` + `btp-btmgr` is the stack that uses it (report 216 §2).
  The chip is a WL1283 (PlayBook combo), not a generic WL1271.

## What is uncertain

* Whether "6.0.15 / Quattro L / `TI_P6_15.51`" names a **different chip** or
  just the **BT core firmware version** of this one.  Nothing in the local
  material decodes the string, and the handoff's wording is an interpretation,
  not a measurement.
* Whether the reference's script is safe to replay verbatim over H4 from Linux
  — its commands are ordinary HCI, but no loader has been written (report 237
  §5 item 2).
* Whether the service pack is needed at all for the functions wanted first
  (inquiry/scan): the ROM answers HCI already (report 237 §4).

## What would settle it

1. Power `hci0` (mgmt `SET_POWERED`) and read
   `HCI_Read_Local_Version_Information` plus the TI vendor version command
   **before** any service pack; compare with the version the stock OS reports
   on this unit.
2. Load `WL1283.bts` with a userspace H4 loader and confirm the version/feature
   response changes as the reference expects; then, if desired, do the same with
   `TIInit_7.2.31.bts` (fetch it first) and compare.
3. A datasheet or TI release note naming the WL1283's BT service pack would
   close it without the bench.

Until then, **decisions about BT firmware must name which file they assume**
(report 237 and DECISIONS §72 do).
