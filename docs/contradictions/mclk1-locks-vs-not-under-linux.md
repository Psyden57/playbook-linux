# Does MCLK1 clock under our Linux image? Report 204 says no; session 41 measured yes, but unreliably; session 42's direct test says no on its boot

**Status: OPEN (updated session 42, report 206).**  Both sides have live
measurements, and session 42 added a new discriminator that strengthens Claim A
without settling the file: with the FLL completely bypassed, `AIF1CLK` sourced
**directly from MCLK1** still fails.  The disagreement matters because the
audio design now *rests* on it: since session 42's E1 the codec is the AIF1
master (DECISIONS §53) and the FLL must reference MCLK1 — a BCLK-referenced FLL
is circular in master mode.  A live MCLK1 is the one missing clock.

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

## Claim C — session 42's discriminator: the pin does not clock *on its boot*

**Source:** session 42, report 206 §3.

**Evidence.**  With a stream running and the codec thawed, `AIF1CLK` was
sourced **directly from MCLK1** (`R0x220 = 0x0000`, `R0x200 = 0x0001` — SRC = 0
= MCLK1, no FLL anywhere in the path; the FLL's lock detector is not involved).
If MCLK1 toggled at 12.8 MHz, AIF1CLK would be live (2.4 % over the 12.5 MHz
AIF1CLK spec — acceptable for a test) and BCLK would appear; `pbplay` died with
its stalled-path signature (`Input/output error` after five periods, trap 169)
exactly as in the FLL case.  In the same session, the same bench with AIF1CLK
forced from the codec's internal NCO streamed 20 s cleanly — so the test bench
and the codec were capable; the difference was the clock source.

`AUXCLK1 = 0x00020100` and CORE pad `0x19C` low half `0x0000` were re-read
during the same boot (live `devmem`), and the FLL1 register file was
byte-identical to the working reference — so no readable register explains the
difference.

**Strength:** a direct, single-boot, single-trial measurement.  It is the
strongest software-visible evidence yet for Claim A, but one boot cannot
outvote Claim B's measured lock, so the file stays open.

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
3. **Claim C repeated across boots** (the direct-MCLK1 source, no FLL): if the
   stream *ever* survives with `R0x200 = 0x0001`, the pin clocks at least
   sometimes and "marginal" wins; if it never survives in 5+ trials, Claim A
   wins on the software-visible evidence alone.
4. If a differential is found, check whether `startup-playbook`'s ordering
   (`AUXCLK1` programmed before the FLL is ever enabled) is what the reference
   relies on.

## Current working position

Since session 42 the codec is the **AIF1 master** and McBSP2 a clock consumer
(DECISIONS §53, report 206) — the E1 architecture, which proved the transmit
path works when AIF1CLK exists.  The FLL must therefore reference MCLK1
(BCLK is circular in master mode), and the free-running NCO (±10–30 %) is the
accepted *mechanism probe*, not a production clock.  The MCLK1 question is now
the single blocker; session 41's BCLK workaround (DECISIONS §52) is superseded
for the current design.
