# The DISPC/DSI timing *field distribution* — falsified as the blocker

**Status: resolved (falsified) 2026-10-28 — the fields were forced and the panel
stayed black (report 124).**  Not a disagreement between two documents, but a
measured delta
between our build and the reference; it was the leading hypothesis for the black
panel and it is now falsified.  All four words were forced together and read
back exactly (`DISPC_TIMING_H 0x0890A113`, `DISPC_DIVISOR 0x00010002`,
`DSI_VM_TIMING1 0x01079074`, `DSI_CLK_CTRL 0xA0346005`); the QNX PHY words were
then substituted too (`0B190D13/42040A22/B8800009`); the panel remained
completely black.  The split is not the blocker.

**Related display files (not merged):** `118-vs-121-dsi-link-rate.md` is the DSI
**lane count** — closed (the reference runs four lanes, which is why the 2× in the
PLL/`TL` words is not a free choice), and
`111-vs-122-bridge-programming.md` is the bridge (the IFS programs it; report 125).
This file is the remaining **field distribution inside matching totals**, and it
must not be "fixed" by mixing a QNX lane count with mainline `TL`, or the
reverse.

## The two sets (both measured)

| register | ours | reference (QNX) | note |
|---|---|---|---|
| `DISPC_TIMING_H` | `0x0890B400` = HBP 137 / HFP 180 / HSW 0 | `0x0890A113` = 137 / 161 / 19 | register values are n−1 for H, so actuals 138/181/1 vs 138/162/20 |
| `DISPC_DIVISOR` | `0x00010003` (pck_div 3) | `0x00010002` (pck_div 2) | different DSS functional clock, same 50.4 MHz pixel clock |
| `DSI_VM_TIMING1` | `0x00086067` = HBP 103 / HFP 134 / HSA 0 | `0x01079074` = 116 / 121 / 1 | TXBYTECLKHS units, not pixels |
| `DSI_CLK_CTRL` | `0xA0306008` (LP divisor 8) | `0xA0346005` (5, plus bit 18) | |

Both sets give Htotal 1344, Vtotal 625, 50.4 MHz and 60 Hz; the *totals* agree,
only the split between the blanking fields differs.  Mainline's
`dsi_vm_calc_blanking()` deliberately redistributes the horizontal porches to fit
its byte-clock budget, which is a legitimate thing to do — for a bridge that
decodes DE/HS/VS from the DSI packets and does not care about the split.

## Why it is unresolved

* Nothing in our measurements shows that the TC358764/TCON cares about the split.
  A wrong split would normally shift or tear the image, not black it out.
* But with the bridge unprogrammable **at the time** (see
  `111-vs-122-bridge-programming.md`, since resolved), the timing fields were the last variable we
  can control, and the panel was still black at the time after every other register was matched
  (report 121 §11, §12).
* The `DSI_PHY_CFG*` values also differ (mainline computes its own DDR/HS-LP
  numbers; QNX's are in report 118 §3.3) and have not been compared.

**What is still uncertain, stated plainly:** whether this TCON/panel reads the
blanking split at all (nothing measured says it does); whether the `DSI_PHY_CFG*`
difference matters (not compared); and whether the LVDS mapping is the real
remaining variable.  What *is* established is that the totals, the pixel clock and
the lane count match the reference.

## What would settle it

1. Force **all four registers above together** to the reference values and re-test.
   Never mix: QNX PLL words with mainline `TL` (or the reverse) is a genuine 2×
   error.
2. If that changes nothing, substitute the QNX `DSI_PHY_CFG*` set (same rules:
   all together) and re-test.
3. If that also changes nothing, the remaining suspect is the LVDS mapping the
   bridge is strapped for versus the driver's VESA-24 `LV_MX` table — which we
   cannot change while the bridge's register port is unreachable.

## How to judge the result

The success criterion is the panel showing the framebuffer.  Paint it first
(`fb 0000ff`; a black framebuffer and a dead panel are indistinguishable), and
read the DSI health registers (`IRQSTATUS 0x80`, `COMPLEXIO_IRQ_STATUS 0`) so a
link error is not mistaken for a panel problem.

## Outcome — the display was fixed elsewhere

The timing-field split was falsified as the blocker (report 124), and the real
last defect was the **DISPC core clock** (report 133), which was raised to
96 MHz; the panel now shows a clean image.  The timing fields were later matched
exactly anyway (report 132 §6), so this file is retained only as the record of a
hypothesis that was tested and rejected.
