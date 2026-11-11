# `startup-playbook` — the 1917 board bring-up binary

Reference notes for `proc/boot/startup-playbook` from the **BlackBerry PlayBook
2.1.0.1917** IFS (the working reference OS).  This folder is our reconstruction:
what the binary does, which pads/GPIOs/registers it sets, and which of it matters
for the Linux bring-up.  The vendor binary itself is **not** in the tree — it
lives in the gitignored payload at
`local/work/1917/ifs/proc/boot/startup-playbook` (sha256 `bc7d3a41…`, 278 536 B).

> Why this exists: session 40 measured that the WM8994's MCLK1 does not clock
> under our Linux image but does under TabletOS, with every visible register
> identical.  The reference's board programming lives here — the pads, GPIOs,
> clocks and PMIC settings that the chainloader never runs.

## Provenance and load coordinates

| item | value |
|---|---|
| source | 1917 IFS, `proc/boot/startup-playbook` |
| size / sha256 | 278 536 B / `bc7d3a414fb1c9f67bdaf71349792af2ee22f1beba7c8691ae8daa8650a4ecda` |
| container | raw ARM image (no ELF header); 16-byte header, code starts at file `0x10` |
| load address | **`0x80100000`** (file offset 0) |
| address rule | **VA = 0x80100000 + file offset** (e.g. file `0x22750` = VA `0x80122750`) |
| entry | `0x80100010` (first instruction; `0x80100000`–`0x0F` is a zeroed header) |
| analysis | Ghidra 12.1.4 via the REA MCP: `open_binary` `format=raw-binary`,
  `architecture=arm`, `entry_point=0x80100000` — 461 procedures, 484 strings |
| also in file | an embedded ELF at file `0x2cef0` (not the start-up code) |

Method: Ghidra for the pseudo-code, `arm-linux-objdump -D -b binary -m arm
--adjust-vma=0x80100000` for exact instruction/address work, and raw scanning of
the file for tables and constants.  The binary is fully position-independent:
strings and tables are addressed through **relative offsets** resolved at run
time (`add r4, pc, r4`), so a plain pointer search finds nothing — the tables
below were resolved by computing the base.

## What the binary is

The board's pre-driver bring-up, run by the QNX `procnto` start-up before any
driver loads.  It:

* reads the **manufacturing data** (`Reading MFG data...`) and derives the
  **board type** and **silicon revision** (`Board type = %s`), defaulting to
  **Winchester** when the product ID cannot be decoded;
* applies the board's **pad and GPIO tables** (the 75-record GPIO table is
  decoded in [`gpio-pins.md`](gpio-pins.md));
* initialises the UART / SPI / I²C / MMC controllers and GPIO banks
  ([`mmio-map.md`](mmio-map.md));
* programs the clocks and the watchdog, including **SCRM `AUXCLK1` = 12.8 MHz**
  and **WDT2 = 58.6 s** ([`clocks-and-wdt.md`](clocks-and-wdt.md));
* registers the platform (`FUN_80102a48`) and hands over.

## Board and silicon identity

Strings (VA = `0x8012ba30`…): `Reading MFG data...`, `Unable to determine board
type from productID in manufacturing data. Setting to Winchester`, `Board type =
%s`, and the name table:

```
0x8012baa0  UNKNOWN   0x8012baa8  ES1.0   0x8012bab0  ES1.1   0x8012bab8  ES2.0
0x8012bac0  ES2.1     0x8012bac8  ES2.2   0x8012bad0  ES2.3   0x8012bad8  EMU
0x8012badc  HS        0x8012bae0  GP      0x8012bae4  Winchester
0x8012baf0  RU2       0x8012baf4  Winchester2   0x8012bb00  RU3
0x8012bb04  Colt      0x8012bb0c  Rubicon       0x8012bb14  Tedeschi
0x8012bb20  Cyclone   0x8012bb28  CoolRiver     0x8012bb34  London
```

The version list (`ES1.0`…`ES2.3`) and the board list (`Winchester`…`London`) are
distinct tables; the enum values used by the pin-selection code are documented in
[`gpio-pins.md`](gpio-pins.md) §2.  Our unit reports **WM8994 revision D** in
Linux (`wm8994 3-001a: WM8994 revision D`), and 1917's own `pin`/slog output is
the authority for the board type value.

## Status

| section | state |
|---|---|
| GPIO table (75 records) | **decoded** — [`gpio-pins.md`](gpio-pins.md) |
| WLAN/BT pin selection (the 151/152, 151/163, 54/53, 171, 55 question) | **decoded** — [`gpio-pins.md`](gpio-pins.md) §2 |
| SCRM `AUXCLK1` writers | **decoded** — [`clocks-and-wdt.md`](clocks-and-wdt.md) §1 |
| WDT2 policy | **decoded** — [`clocks-and-wdt.md`](clocks-and-wdt.md) §3 |
| MMIO blocks touched | **enumerated** — [`mmio-map.md`](mmio-map.md) |
| signal-group / pad table at `0x80122638` (14 × 20 bytes) | **partially decoded** — [`pads.md`](pads.md); field semantics open |
| the 20-byte block at `0x801230b0` | **open** — layout not yet confirmed |
| board-type enum values / MFG record | **open** — needs the caller of the `Board type = %s` function |
| the per-driver init tables (function-pointer dispatch, e.g. `0x80107980`) | **open** — call sites are indirect |
| PMIC (TWL6030) writes | **open** — see `mmio-map.md`; reached through I²C, not direct MMIO |

Nothing here has been used to change the kernel yet.  The actionable item is
`AUXCLK1` (§1 of [`clocks-and-wdt.md`](clocks-and-wdt.md)): the reference sets it
to `0x00020100` (÷3 = 12.8 MHz, enabled) early, and our Linux boot does not.
