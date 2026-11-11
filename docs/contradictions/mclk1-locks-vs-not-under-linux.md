# Does MCLK1 clock under our Linux image? Report 204 says no; session 41 measured yes, but unreliably

**Status: OPEN (session 41).**  Both sides have live measurements.  The
disagreement matters because the whole audio design choice rests on it: if MCLK1
is reliable, the reference's design (`MCLK1 → FLL1 → AIF1CLK`, codec as AIF1
master, FLL locked permanently) is available to us; if it is not, the driver must
keep the BCLK-referenced FLL and the delayed-repair workaround.

## Claim A — "MCLK1 does not clock under our image"

**Sources:** report 203 §5, report 204 §3–§4, and the session-41 handoff.

**Evidence.**  With `AUXCLK1 = 0x00020100` (÷3 = 12.8 MHz), pad `0x19C` mux 0,
and the reference's exact FLL1 programme written to the codec, `R0x732` bit 5
(`FLL1_LOCK_STS`) stayed 0 — at every AUXCLK1 divider (÷1…÷8 swept), with the
codec reset, and with the PM1/PM2 analogue bias on.  The datasheet's
free-running FLL also produced no lock.  The reference, on the same board,
locks (`0x732 = 0x6020`).

**Strength:** strong as an *observation* — it was repeated many times and the
register values read back correctly.

## Claim B — MCLK1 *can* lock under our image, but not reliably

**Source:** session 41, report 205 §4b and §5h.

**Evidence.**  With the codec **thawed first** (`R0x200 = 0x0000`, which report
203 established as the only write a frozen codec accepts), the reference's FLL1
programme and `R0x224 = 0x0000` (`REFCLK_SRC = MCLK1`), `R0x221 = 0x0700`,
`R0x223 = 0x00E0`, `R0x222 = 0xAE14`, `R0x220 = 0x0005`, PM2 supplies `0x6310`:

* **FLL1 locked** — verified `R0x732 = 0x6020`, read back through `i2ctransfer`.
* Then, after a later reboot and again on a battery-pulled cold boot, the
  **identical register file** left `R0x732 = 0x6000` (no lock) with
  `AUXCLK1 = 0x00020100` verified stable by three reads 2 s apart and
  `auxclk1_ck` enabled at 12.8 MHz in the clock framework.
* Meanwhile every SoC-side comparison against QNX is byte-identical:
  `AUXCLK1`, pad `0x19C`, `CONTROL_CORE_PADCONF_MODE` (`0x00000000`), the three
  SMART3 drive registers, `DPLL_PER` M3x2, and all PMIC rails.

**Strength:** the one lock is a direct measurement; the intermittency is a
direct observation across several boots.

## What is uncertain

* Whether Claim A's failures were **all** the frozen-codec artefact.  Report 203
  itself warned (trap 159) that a frozen codec makes a write test prove nothing,
  and Claim A's sweeps did not always thaw first.  But Claim B's cold-boot
  failure *did* thaw first and still did not lock, so thawing is necessary but
  apparently not sufficient.
* Whether the intermittency is a **clock at the pin** problem (needs a meter),
  a **timing/enable-order** property of AUXCLK1 vs FLL enable, or something in
  the codec's analogue side that varies with temperature/boot.
* Whether the kernel's `clk_summary` can be trusted for this clock — it reported
  38 400 000 even after SCRM was written to `0x00020100` and verified, so it is
  demonstrably stale and **cannot** be used as evidence either way.

## What would settle it

1. **A meter or scope on MCLK1** (CORE pad `0x19C` / the codec's MCLK1 pin) on a
   boot where the FLL locks and a boot where it does not.  Presence and
   amplitude settle Claim A outright.
2. A **codec-only detector**: with the codec thawed, `AUXCLK1 = 0x00020100`
   forced, and nothing else configured, program the FLL from MCLK1 and read
   `R0x732` — repeated across 5+ boots to quantify the intermittency.
3. If a differential is found, check whether `startup-playbook`'s ordering
   (`AUXCLK1` programmed before the FLL is ever enabled) is what the reference
   relies on.

## Current working position

The driver uses the **BCLK**-referenced FLL, which locked on every trial in
session 41, and does not depend on MCLK1.  That is a deliberate, documented
workaround (DECISIONS §52), not a conclusion that MCLK1 is dead.
