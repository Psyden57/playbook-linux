# FAN5702 dimming registers: `0b11xxxxxx` vs the bootloader's `0x36`

**Status: open.**  The on/off path is unaffected; this is about the per-channel
dimming registers only.

## Claims

1. **External `LED-RE.md`** (`~/SSDdrive/wsl-migration/home/psyden/playbook-dev/
   LED-RE.md`, §3): the FAN5702 dimming registers `CH3/CH4/CH5` at
   `0x30/0x40/0x50` are written as **`0b11xxxxxx`** — bits 7:6 must stay `1`,
   and the low 6 bits are the dimming code `0..63` (the datasheet's Table 4).
2. **The bootloader and the Linux `leds-fan5702` driver** write
   `0x30/0x40/0x50 = 0x36` (bits 7:6 = `00`, low 6 = 54) during init, and the
   LED lights (operator saw red/green/blue/white, report 179).  The QNX
   `led-fan5702.so` never writes the dimming registers at all; it relies on the
   bootloader's value.

## Evidence for each

* Claim 1: the datasheet-derived text in `LED-RE.md` §3 (FAN5702/D Table 4);
  the same report says the QNX driver never writes the dimming registers.
* Claim 2: the bootloader disassembly (`emmc-first1mb.bin`, writes at
  `0x8000966A`/`0x80009670`/`0x80009678`, value `0x36` after an `& 0x3f`);
  the `leds-fan5702` init (`fan5702_chip_init()`, report 179); the operator's
  observed red/green/blue/white with the on/off path (which only touches
  `GENERAL 0x10`).

## What is uncertain

* Whether `0x36` is a valid dimming code, a different field of the CHx
  register, or a value where bits 7:6 are "don't care"; the QNX driver only
  needs `GENERAL`, so the vendor never exercised dimming.
* Whether the external report's `0b11xxxxxx` is the same register revision /
  board as ours.  The two `led-fan5702.so` builds differ slightly (18,320 vs
  18,324 B), so this may be a build/board difference, not a contradiction.

## What would settle it

* Write `0x30` = `0x00`, `0x3F`, `0x7F`, `0xC0|0x3F` (one at a time, with
  `GENERAL` bit1 set) and observe the LED brightness; or the FAN5702 datasheet
  Table 4 for the exact CHx field layout.
* Until then the driver keeps the bootloader's `0x36` and only exposes on/off
  (report 179).
