# The reference's `FREF_CLK2` pad: mux 0 clock output vs mux 3 GPIO high-Z

**Status: open.**  Discovered while reconciling the WL1283 notes during the
session-31 wrap-up.

## Claims

1. **Report 180 §6 (session 29).**  The pad `0x4A10019C` (CONTROL_CORE_PAD0_
   FREF_CLK2_OUT / PAD1_SYS_NIRQ1) reads **`0x41000000`** on the running
   reference — mux 0, `fref_clk2_out` — while our boot reads **`0x4118010F`**
   (mux 7, safe mode).  It is called "the one that *is* functional" in the
   full padconf diff, and the report's FREF hypothesis is built on it.
2. **Session-29 note, "Superseded / uncertain".**  "The FREF pad muxing now in
   the DTS (`0x4A10019C` = `fref_clk2_out`, WKUP `0x4A31E054/058/05C`) is a
   faithful copy of the reference, but the reference puts those pads in **mux 3
   = GPIO with GPIO6 `OE` all-inputs** — high-Z, not a driven clock.  The
   earlier claim 'the reference muxes a reference clock out' is superseded by
   that decode."

The two claims are about the same register and cannot both be current.  The
note says the *decode* supersedes the report, but report 180 was not corrected
in place and still carries the mux-0 value.

## Evidence for each

* **For (1):** a live padconf dump of the reference with Wi-Fi up; the value
  `0x41000000` is quoted.  The same session wrote `0x41000000` live to our
  unit and re-scanned MMC5 (still no card — but that write does not enable the
  CM parent).
* **For (2):** a later decode in the same session; the note does not quote the
  register values it used, and it may generalise a **WKUP**-pad decode
  (`0x4A31E054/058/05C`) to `0x4A10019C`.
* **Session 31 (this unit, not the reference):** the boot pad reads
  `0x4118010F` (safe mode), the DTS does **not** mux `0x19C`, writing
  `0x41000000` took, and writing `CM_DIV_M5 = 0x329` glitched the panel because
  `dpll_per_m5x2_ck` is the DSS clock parent (trap 116).  **No fresh reference
  read was taken** — the unit was on our image for this session.

## What is uncertain

* Which value the reference actually has with Wi-Fi up (`0x41000000` or a
  GPIO mux), and what GPIO6 `OE` says about it.
* Whether the reference's FREF pads are one decode or two (CORE `0x19C` vs the
  WKUP group).
* Whether the WL1283's 38.4 MHz reference comes from this pad at all — the
  only positive evidence is the vendor `tiwlan_winchester.ini`
  (`STRFRefClock = 2`); a crystal/PMIC source would not appear in the pad
  state.

## What would settle it

A fresh, read-only dump on the live 1917 reference with Wi-Fi up:
`0x4A10019C`, `0x4A31E054/058/05C`, and the GPIO6 `OE`/`DATAOUT` pair — or the
board schematic / WL1283 datasheet showing the reference-clock source.

## Consequence for the WL1283

The session-31 FREF test (pad mux 0 + `CM_DIV_M5 = 0x329`) is a valid
*behavioural* negative — the card still did not answer — but it is only a
faithful reproduction of the reference if claim (1) is right.  If claim (2) is
right, forcing the pad was not the reference's configuration, and the
reference-clock hypothesis rests on the vendor `.ini` alone.  Either way the
route is **closed as untestable without the DSS-independent clock tree and the
schematic**; resolve this file before any future FREF attempt.
