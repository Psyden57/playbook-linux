# Who programs the TC358764?

**Status: resolved 2026-10-28 (report 125); the register-port residual closed
2026-10-29 (report 132).**  The reference DOES program the bridge.
The 2026-10-27 resolution below ("nothing does, and it needs no programming")
was wrong: `winch_lcdctl` calls a full bridge programmer at vaddr `0x13e50`
from the splash sequencer at `0x1938c`, unconditionally, between the DSI
PHY/PLL programming and the DSI VC enable.  Report 125 recovers the sequence.
The history below is kept because the mistake is instructive: report 122 saw the
call (`1938c 0x13e50` in its own §7.1 sequence) but did not identify it.

**Related display files (not merged):** `118-vs-121-dsi-link-rate.md` is about the
DSI **link** (four data lanes vs two) and `open-dispc-dsi-timing-fields.md` about
the **timing-field split** inside matching totals.  This file asks who
configures the bridge; it is now answered, and the answer is "the IFS does".

## 2026-10-28 correction (report 125)

`winch_lcdctl`'s function at `0x13e50` writes the complete bridge register set
over DSI generic long-write packets: PPI timings and counters, lane enables,
`PPI_STARTPPI`, `DSI_STARTDSI`, `LV_PHY0` (reset assert → 100 ms → release),
the `LV_MX` mapping (identical to upstream's VESA-24 words), `VP_CTRL`,
`VP_HTIM*`/`VP_VTIM*`, `LV_CFG = 1` (`LVEN`, *no* clock-polarity inversion —
upstream writes `0xD`), `0x0520/0x0524 = 5`, `SYS_RST = 4` (`SYS_RST_LCD`,
never `0x1f`), and `VP_VFUEN = 1`.  The call is on every path through the splash
branch; it is not the `-t/-T` self-test.

Two independent facts confirm it: the datasheet names `LV_CFG[0]` (`0x049C`) as
the LVDS enable and `0x04A0[8]` as the PLL power-down; and the call sits exactly
where the reference needs it (after DSI PHY/PLL, before VC enable).

Consequence: our kernel must program the bridge with this sequence.  It now does
(`tc358764_init()`).  At the time of report 125 the panel was still black, so the
remaining question was delivery or the reset/power pin; that was fixed in
sessions 14/15 (reports 131–133) and the display now works.  The historical
claims and evidence follow unchanged.

## The claims

**Claim A (report 111 §8 item 4, session 5).**  No 16-bit-address I²C table was
found in `winch_lcdctl`, so "the RIM bootloader configures the bridge" (or it is
strapped for "auto" operation).  This must be resolved on hardware.

**Claim B (session 6, while chasing a black panel).**  The bridge must be
programmed by us over DSI generic packets; when our transfers fail at the DSI bus
turnaround (BTA), the bridge is therefore unconfigured and the panel is black.
Acting on B, we wrote the upstream init table and a full `SYS_RST <- 0x1f`.

**Claim C (report 122 + relay, 2026-10-27).**  **Nothing programs the bridge, in
any stage of the boot, and it does not need programming.**  It runs from its
power-on/strapped state, and the reference display works that way.

## Evidence

For C:

* Boot chain is boot ROM → eMMC second-stage loader → IFS → RFS.
* The **boot ROM cannot**: a DSI host with PHY, PLL and packet engine is required
  to speak the bridge's register protocol.
* The **second-stage loader** (`hd0head.bin`, first 2 MB checked) contains zero
  occurrences of `0x48041000`/`0x48044000`/`0x48044200`/`0x48044300` and no
  display-related strings; it does carry I²C/GPIO/MMC/UART board constants.
* The **IFS startup image** — the code our chainloader replaces — references only
  `0x48242000` (L2), `0x4A307B00` (PRM) and `0x4A318000` (WDTIMER1).
* **`winch_lcdctl`** never sends bridge configuration packets (report 111 §4.3).
* The **IFS splash proves it in one observation**: `winch_lcdctl -s splash` puts
  the logo up before the RFS is mounted (`GFX_SIZE 0x003F01ED` = 494×64 at
  `GFX_POSITION 0x010C0109` = 265,268 in a 1024×600 field), i.e. **DISPC + DSI +
  the SPI TCON init + the LM3532 backlight all work with the TC358764 untouched**.
* Counter-evidence against B, measured: after our reset pulse the bridge is alive
  on its own terms (`GPIO2_DATAIN` bit 4 = GPIO36 = the bridge's own status ball,
  the condition QNX's self-test requires), yet its register channel still does not
  answer reads — so "does not answer" was never evidence that it was unprogrammed.

Cost of believing B: the full `SYS_RST <- 0x1f` write was transmitted even though
the ack failed, and it left the **reference** display with faint vertical
artifacts until a cold battery pull (trap 28 in `docs/project/HANDOFF.md`).

## Resolution

**Superseded 2026-10-28 by report 125.**  The 2026-10-27 text read:

> Claim C.  Report 111 §8 item 4 was corrected in place; the board driver no
> longer programs the bridge and keeps only the reference's 20 ms active-low
> reset pulse (`GPIO2_24`), which is what the reference itself does.

That is the opposite of the evidence in report 125: the reference *does* program
the bridge (at `winch_lcdctl 0x13e50`) and our driver now reproduces the exact
sequence.  The "IFS startup image references only 0x48242000/0x4A307B00/
0x4A318000" argument was about the small startup image, not about
`winch_lcdctl`, which is in the same IFS and is the display actor.

## Residual — closed 2026-10-29 (report 132)

The bridge's register port **was** reached.  With `PPI_TX_RX_TA = 0x00040006`
(latched at `PPI_STARTPPI`), DSI generic reads answer reliably,
`IDREG = 0x00006500`, and the whole 353-register map was read from a working
TabletOS and diffed against ours (report 132 §3/§6).  The earlier 0/`-EIO`
readings were the latched turnaround value, not a dead port.

What remains true from the old text: a failed BTA proves only that a write was
not acknowledged, not that it did not land.  Keep that distinction when reading
old notes.
