# The reference's `FREF_CLK2` pad: mux 0 clock output vs mux 3 GPIO high-Z

**Status: RESOLVED (session 32, report 185).**  A fresh read-only dump of the
running 1917 reference with Wi-Fi up confirms **claim (1)**: CORE pad
`0x4A10019C` is `0x41000000` (pad0 mux 0, `fref_clk2_out`), and the SCRM
`AUXCLK1` register `0x4A30A314` is `0x00020100` (source SYS_CLK, divide 3,
enabled) — a real **12.8 MHz** output clock, not a high-Z GPIO.  Claim (2)'s
"mux 3" decode was a **WKUP**-pad result (correct for `0x4A31E058` pad0) that
was generalised to the CORE pad; it does not apply to `0x4A10019C`.

Fresh session-32 values (1917, WLAN up): `0x4A10019C = 0x41000000`;
`0x4A31E050 = 0x00080000`, `0x4A31E054 = 0x000F0003`,
**`0x4A31E058 = 0x000B0003`** (pad0 mux 3 `gpio_wk31`, input off),
`0x4A31E05C = 0x0100000B`; `0x4A30A314 = 0x00020100`.  The GPIO6 `OE` pair was
not re-read, but it does not affect the CORE pad's mux.

## Original claims (kept for the trail)

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

## Resolution and why it matters

The reference **does** mux the CORE pad to `fref_clk2_out` and **does** enable
the SCRM `AUXCLK1` generator.  The output is 12.8 MHz (`38.4 / 3`), not the
38.4 MHz the vendor `.ini`'s `STRFRefClock = 2` names.

Session 31's behavioural test (pad mux 0 + `CM_DIV_M5 = 0x329`) was therefore
half of the reference's configuration: it did not enable `AUXCLK1`.  Session 32
enabled it exactly (`0x00020100`) and muxed the pad, and MMC5 still did not
answer CMD5 — so the FREF2 output is **not** the WL1283's missing variable.
The WL1283's main clock is the 26 MHz TCXO (vendor `.ini`; `wl128x_boot_clk()`),
and the remaining question is the TCXO/power at the chip (report 185 §5).

The `CM_DIV_M5 = 0x329` write remains forbidden (trap 116): it is the DSS
divider, not the FREF generator.
