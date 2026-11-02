# The DSI link rate: "benign 2× divergence" vs "four data lanes"

**Status: resolved 2026-10-27 — 118 §8.2 was wrong; the reference runs four DSI
data lanes.**

**Related display files (not merged):** `111-vs-122-bridge-programming.md` covers
**who configures the TC358764** (the IFS does, report 125) and
`open-dispc-dsi-timing-fields.md` covers the **timing-field split** that still
differs once the lane count and the totals agree.  This file is only the lane
count / link-rate factor of two.

## The claims

**Claim A (report 118 §8.2, session 5).**  With the two-lane build, mainline's DSI
PLL (`CONFIGURATION1 = 0x1CE1F81F`) and `TL = 2016` differ from QNX's
(`0x14A5E8BE`, `TL = 1008`) by exactly 2×, but that is benign: `dsi_vm_calc_blanking()`
"redistributes the horizontal porches so the line fits the DSI byte-clock budget",
the active area, vertical timing and pixel clock are the same, and only the clock
plan differs (`DISPC_DIVISOR` LCD 3 vs 2).

**Claim B (relay answer, 2026-10-27, first version).**  The 2× is a
register-encoding difference, not a wire difference: QNX `n=96, m=756` gives
`fint = 0.4 MHz`, `clkdco = 604.8 MHz`, `txbyteclkhs = 37.8 MHz` with `TL = 1008`
(26.67 µs/line); mainline `n=16, m=252` gives `2.4 MHz`, `1209.6 MHz`, `75.6 MHz`
with `TL = 2016` (also 26.67 µs/line).  Same line time, same frame rate, so it
"cannot be the cause".

**Claim C (report 121 §11, session 6).**  The reference drives **four** DSI data
lanes.  `DSI_COMPLEXIO_CFG1` reads `0x2A054321` on QNX (CLK=1 DATA1=2 DATA2=3
DATA3=4 DATA4=5) and `0x2A000321` on our two-lane build (DATA3/DATA4 unused).
1024×600 at 60 Hz RGB888 is `50.4 Mpx/s × 24 bpp = 1.2096 Gbps`; over four lanes
that is `302.4 Mbps/lane` = `37.8 MHz` byte clock, i.e. exactly QNX's `TL = 1008`.
Two lanes require `75.6 MHz`, which is exactly what mainline produced.

## Evidence

* The live `DSI_COMPLEXIO_CFG1` read on both systems (claim C) is a direct
  measurement and is decisive.
* After `lanes = <0 1 2 3 4 5 6 7 8 9>` (5 pairs) and `lanes = <4>` on the bridge,
  `DSI_COMPLEXIO_CFG1` becomes `0x2A054321` **and** `DSI_VM_TIMING3` becomes
  `0x03F00258` (`TL = 1008`) with no other change — the two observables move
  together exactly as claim C predicts.
* Claim A and claim B both compared register *encodings* and concluded the
  encodings were equivalent.  They were right about the encoding arithmetic and
  wrong about the *hardware*: the arithmetic happened to hold because a two-lane
  link at twice the byte clock has the same line time, but the panel is fed four
  lanes.
* The relay retracted claim B explicitly on 2026-10-27.

## Resolution

Claim C.  `docs/reports/118-live-qnx-display-registers-2026-10-27.md` §8.2 was
corrected in place to carry the four-lane explanation, and
`docs/reports/121-spi-tcon-panel-2026-10-27.md` §11 holds the measurement.

This closes the **lane count** only.  The remaining display delta — the
DISPC/DSI timing *field* split inside matching totals, and the uncompared
`DSI_PHY_CFG*` set — is tracked in `open-dispc-dsi-timing-fields.md`.

## What this cost, and the lesson

Two sessions spent effort "explaining away" a factor of two in a *register
encoding* instead of checking a lane count that is one register read.  The
generalisable lesson: when two systems' timing registers differ by a clean factor,
compare the **lane count** and the **totals** before theorising about encodings.
