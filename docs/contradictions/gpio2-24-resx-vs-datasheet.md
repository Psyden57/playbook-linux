# Is `GPIO2_24` really the TC358764's RESX?

**Status: open.**  Two sources disagree, and this session's measurements do not
settle it.

## The claims

**`GPIO2_24` is RESX (report 122, rating itself "medium").**  `winch_lcdctl`
toggles that ball at the bridge-programming point: `GPIO2_CLEARDATAOUT =
0x01000000` at vaddr `0x192d4`, `usleep(20000)`, `winch_lcdctl 0x19338`
`SETDATAOUT = 0x01000000`, `usleep(20000)` (report 122; report 125 §1 repeats the
ordering).  The inference is that a 20 ms active-low pulse there is the bridge
reset, taken after the DSI PHY/PLL programming and before the bridge is
programmed.

**RESX is pin B3 and is low-active.**  The TC358764XBG pin table in
`hardware/TC358764XBG.PDF` lists `RESX` on balls B3 (BGA49) and E4 (BGA64) as
"Hardware reset, low active".  So *asserted = low*; a working bridge must sit
**high**, and the pulse above (low → high) is a correct reset-then-release.

## The evidence that conflicts

* **The reference reads the bit LOW while its display is working.**  Report 122
  §7.7 and report 127 §2 captured `GPIO2_DATAIN = 0x08000008` / `0x08000018` /
  `0x0800001c` during QNX's `-t` self-test; **bit 24 is clear in all three**.
  Re-read this session on TabletOS during the self-test: `0x08000018` — bit 24
  still clear.  Taken literally, the ball is low while the bridge works, which
  contradicts "low = reset" together with "GPIO2_24 = RESX".
* **The reference's own `DATAOUT` has the bit SET.**  Report 118 §10.1 records
  `GPIO2_DATAOUT = 0x31013200` — bit 24 set — with `GPIO2_OE` bit 24 = 0
  (output).  So QNX *drives the ball high* while the same readback word shows
  `DATAIN` bit 24 = 0.
* **The pad's input path is disabled.**  `CTRL_CORE_PAD 0x4A100080 = 0x…4003`
  on both systems: mux 3 (GPIO), `INPUTENABLE` (bit 8) **clear**.  With the input
  disabled, `GPIO2_DATAIN` is not a trustworthy view of the ball — which is the
  most likely reason for the contradiction above.

## What is uncertain

Whether the `DATAIN` bit-24 readings mean anything at all given the disabled
input path.  If they do not, the "low while working" evidence collapses and
report 122's inference stands.  If they do, `GPIO2_24` is not the reset.

The bench cannot separate these: **pulsing RESX and not pulsing it produced
identical results** (image `0xDED99C5E` with the pulse restored versus
`0x48048809` without any pulse — both `IDREG = 0x00006500`, both
`LANESTATUS0 = 0`), because the boot chain resets the bridge anyway (`IDREG` is
`-5` before our sequence on every boot).

## What would settle it

1. A board schematic or a continuity check from ball B3 of the TC358764 to
   `GPIO2_24`.
2. Reading `GPIO2_DATAOUT` and `GPIO2_DATAIN` bit 24 **together** on the
   reference, before and after a pulse, with the pad's `INPUTENABLE` temporarily
   set (a pad write, which QNX does anyway).
3. A scope (or a logic analyser, which only needs DC levels) on the ball during
   `winch_lcdctl power_on`.

Until then, treat `GPIO2_24 = RESX` as **unproven**, exactly as report 125 §5 item 2
already flagged.  Do not build an experiment that depends on it.
