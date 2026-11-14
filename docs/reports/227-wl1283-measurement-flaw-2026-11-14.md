# 227 — The "the card is silent" measurement was flawed; corrected, it says CTO only (a real negative)

Date: 2026-11-14 (session 49, Linux-era 43).
Bench: three instrumented images flashed and booted (`0x7E211599`, `0xDEB38EFD`,
and their predecessor), then reverted.  No NVRAM/MCT/RPMB write.

## 0. Headline

The operator asked the right question — **"how are we measuring that the card is
still silent, could that be flawed?"** — and the answer is **yes, it was
flawed**, in three ways.  All three are now fixed, and the corrected measurement
is a *much* stronger negative than the one the project has been quoting since
session 35.

The three flaws:

1. **`WL_IRQ` (GPIO2_21) is not a card-presence signal.**  Every report since
   session 33 has used "`GPIO2_DATAIN` bit 21 = 0 → the chip is not up" as
   evidence.  It is not evidence.  **The reference's own chip reads 0 before its
   driver runs** — the oracle's pre-driver state is `GPIO2_DATAIN = 0x08000018`
   (bit 21 = 0), and it only becomes `0x08200018` (bit 21 = 1) *after* the
   driver has powered the chip and downloaded firmware.  So "WL_IRQ low" is the
   normal state of a WL1283 that simply has not been spoken to yet, and the
   comparison in reports 196/217/218/220 (Linux 0 vs reference 1) compared our
   *pre-driver* state against the reference's *post-firmware* state.
2. **A 64-clock timeout cannot see a slow card.**  `STAT` was never decoded —
   only the errno.  See §2.
3. **The bus state at the instant of the command was never read.**  We knew the
   pads and the module clock, but not whether `CEN`, `SDBP`, `SDVS` and `CLKD`
   were right *at the moment* the command went out.

## 1. What the corrected measurement does — and does not — prove

Two instruments were restored (archived since session 37, now in
`kernel/archive/wl1283/measurement-instrumentation-2026-11-14.patch`):

* **`PBX`** — printed in `omap_hsmmc_start_command()`, immediately before the
  `CMD` register write: `SYSCTL` (with `CEN`, `ICE`, `CLKD` decoded), `HCTL`
  (`SDBP`, `SDVS`), and `CON`.
* **`PBIRQ`** — printed in the IRQ handler for every error, with `STAT` decoded
  bit by bit and `PSTATE`/`RSP10`/`CMD` raw.

The complete measured sequence (image `0xDEB38EFD`, kernel #54):

```
PBX:   mmc4 CMD52 arg=00000c00 reg=34020001 SYSCTL=000e1e07 CEN=1 ICE=1 CLKD=120 HCTL=00000b00 SDBP=1 SDVS=5 CON=00000600
PBIRQ: mmc4 status=00018000 CTO=1 CCRC=0 CEB=0 CIE=0 DTO=0 DCRC=0 DEB=0 BADA=0 PSTATE=01f40001 RSP0=00000000 CMD=34020001
PBCMD: mmc4 op=52 arg=00000c00 flags=00000195 err=-110 resp0=00000000
   (same for the second CMD52, arg=80000c08)
PBX:   mmc4 CMD0 arg=00000000 reg=00000001 … (same bus state)
PBCMD: mmc4 op=0 arg=00000000 flags=000000c0 err=0 resp0=00000000
PBX:   mmc4 CMD5 arg=00000000 reg=05020001 … (same bus state)
PBIRQ: mmc4 status=00018000 CTO=1 CCRC=0 … RSP0=00000000 CMD=05020001
PBCMD: mmc4 op=5 arg=00000000 flags=000002e1 err=-110 resp0=00000000
```

What this establishes, and it is worth stating carefully:

* **The bus is correctly set up at command time.**  `CEN=1`, `ICE=1`,
  `CLKD=120` → 48 MHz / 120 = **400 kHz**, `SDBP=1` (bus power on),
  `SDVS=5` (**1.8 V**), `CON=0x600`.  That is exactly what a 1.8 V SDIO
  identification should look like.  So "we never actually powered it" is now
  excluded by measurement rather than by argument.
* **`CTO` (bit 16) is set and nothing else is.**  `CCRC=0`, `CEB=0`, `CIE=0`:
  **no response was received at all**, not a CRC failure, not an end-bit
  failure, not a wrong index.  `RSP0 = 0` and `PSTATE` bit 24 `CLEV = 1` (the
  CMD line is idle *high*, i.e. nothing drove it).
* **`CMD0` works** (`err=0`) — so the controller does transmit, and the code
  path is sound.
* **`STAT` bit 15** in the raw value is `ERRI`, the umbrella bit ("if any of
  `STAT[24:15]` are set"), not an independent error.

So: the card genuinely does not drive the CMD line in response to an
identification command.  That part of the old conclusion survives — but it now
rests on a decoded `CTO`, not on a GPIO.

## 2. The 64-clock problem (a real remaining doubt, stated honestly)

TRM Table 24-102, `STAT[16]` CTO:

> "This bit is set automatically when **no response is received within 64 clock
> cycles from the end bit of the command**.  For commands that reply within 5
> clock cycles — the timeout is still detected at 64 clock cycles."

At 400 kHz, 64 clocks = **160 µs**.  The measured interval from the `CMD` write
to the error IRQ is **~0.7 ms** in the log (the difference includes printk
overhead, so it is an upper bound, not a measurement of the timer).

This matters because a WL1283 that is powered but has not finished its internal
power-on (or whose 26 MHz TCXO has not settled) will miss a 160 µs window and
look exactly like a dead card.  The reference never has this problem because
**it never runs at 400 kHz** — its first command is at 24 MHz, and 64 clocks
there is 2.7 µs, so a card that is not ready also fails… which means the
reference's success implies the card *was* ready by the time it spoke.

The honest reading: a 400 kHz CTO tells us the card did not answer within
160 µs.  It does **not** distinguish "unpowered" from "powered but slower than
160 µs".  That distinction is measurable — the OMAP `DTO` field in `SYSCTL`
extends the *data* timeout, but command timeout is fixed at 64 clocks, so the
only lever is **the clock rate**: raise it (the reference's 24 MHz makes the
window *shorter*, so that is not it) or **retry after a longer settle**.

Which means the one experiment this suggests is: **hold `WL_EN` high for much
longer before the very first command** (e.g. seconds, not 120 ms), and see
whether `CTO` clears.  That is *not* the same as the EN-order experiment of
report 217 §5 (which replayed the reference's own low→high→120 ms), and it has
never been tried.

## 3. The chip-revision question (WL1283C)

The operator notes the board's part is a **WL1283C** in BGA, and asks whether
the Linux driver could be too new or mismatched.  Findings:

* **Enumeration happens before any driver is involved.**  `CMD5` is answered by
  the chip's SDIO *hardware*; `wlcore`/`wl12xx` only binds after a function is
  enumerated.  So no driver version can affect this failure — the firmware,
  the NVS and the driver all come later.  Report 217 already established that
  the reference loads firmware *before* connecting, which is the opposite
  direction; on Linux there is nothing to load until CMD5 answers.
* **`wlcore` does support the part.**  `wlcore_sdio_of_match_table` has
  `ti,wl1283` → `wl128x_data`; the built module even carries
  `MODULE_ALIAS("of:N*T*Cti,wl1283C*")` in `wlcore_spi.mod.c`.  The driver is
  not the missing piece for CMD5.
* **What the reference's driver does *not* do** is anything revision-specific:
  its strings carry no chip-ID or cut check (`wl1283_attach`/`wl1283_detach`
  are the only 1283 names), and the same binary serves the whole family.

So the "too new driver / wrong revision" hypothesis does not explain a CMD5
timeout — but it *would* explain a later failure (firmware/NVS mismatch), and
the local firmware the reference uses is **`firmware.bin` from
`/base/lib/firmware/ti1283`, version Rev 7.1.4.10.77** (report 217), not the
`wl128x-fw-5-sr.bin` that mainline expects.  That mismatch is real and must be
handled when enumeration eventually works; it is not the blocker now.

## 3a. The exact reference command phase was tried — and it hangs, for a reason

The combination no earlier build had (24 MHz first command **and** `CON = 0x85`
**and** 4-bit `DTW`) was run as image `0xE55B3A44`.  It reaches the reference's
state exactly —

```
PBREF: mmc4 command phase = reference (CON=00000085 HCTL=00000b02 SYSCTL=000e0087)
```

— and then **the CMD52 never completes**: the log stops at
`PBC: mmc4 sdio_reset` with no error and no failure line, i.e. the rescan hangs
rather than timing out.  `SYSCTL = 0x000e0087` is `CLKD = 2` → 24 MHz, `CON`
bit 2 is `HR`.

The cause is in the TRM, Table 24-75, `CON[2] HR`:

> "Broadcast host response … This register is used to force the host to generate
> a 48-bit response for bc command type … In order to have the host response to
> be generated in open drain mode, the register `CON[0] OD` must be set to 1."

With `HR = 1` the core **generates a response instead of transmitting the
command**, and it waits for a command that will never complete.  This is why the
earlier replay of report 217 §4 (which set `CON = 0x85` *after* `sdio_reset`,
when the controller had already been through a command) merely timed out, while
setting it *before* the first command hangs.

**Conclusion: `CON = 0x85` is not a value to copy.**  `HR` is an MMC
*interrupt-mode* feature; the reference's own `sdioDrv_ExecuteCmd` writes
`(idx << 24) | (resp << 16)` into `MMCHS_CMD` and never relies on a
host-generated response, so `HR` being set in its `CON` is inert *there* only
because of how it drives the command register.  Our driver does not do that.
The correct port is `CON` with `OD` only (and `CDP`/`DVAL` as mainline has
them), i.e. **not** `0x85`.

This is a genuine result: it removes the last "copy the reference's register
value" hypothesis and explains why the replay could never work.

## 4. What to do next, in order

1. **The settle-time experiment** (§2): keep `WL_EN` high for a long time
   (seconds) before the first command, one variable, and watch whether `CTO`
   clears.  Cheapest remaining software lever, and it is the only one the
   corrected measurement actually points at.  **Do not re-add `CON = 0x85`**
   (§3a).
2. **Then the QNX-in-Linux harness** (the operator's idea): run
   `devnp-ti1283.so`'s bring-up under a shim so its exact byte-level sequence
   executes on this hardware, which removes every reconstruction error at once.
3. **Keep the instrumentation.**  It is archived as a patch and it is the reason
   this report exists; do not let a future session measure silence with a GPIO
   again.

## 5. Traps

* **231.** **`WL_IRQ` (GPIO2_21) is not a "chip alive" signal — the reference
  reads 0 too until its driver has run.**  Using it as the liveness test made
  every comparison since session 33 compare our pre-driver state with the
  reference's post-firmware state.  A liveness claim needs a decoded card
  response (`CTO`/`CRC`/`RSP`), not a board GPIO.
* **232.** **`MMCHS_STAT` bit 16 is `CTO` and bit 15 is the `ERRI` umbrella**;
  `CTO` means "no response within **64 clock cycles**", which at 400 kHz is only
  160 µs.  A raw `status=0x18000` is "Command Timeout, grouped", not two errors,
  and it cannot distinguish "no card" from "card slower than 160 µs".
  Decode `STAT` — the errno (`-110`) collapses `CTO` and `DTO` together.
* **233.** **Read the bus state at the instant of the command, not after the
  probe.**  `CEN`/`ICE`/`CLKD`/`SDBP`/`SDVS`/`CON` in `start_command()` is what
  proves the bus was powered and clocked; the same registers read after a failed
  probe show the *torn-down* state and say nothing.

## 6. Sources

* TRM `hardware/swpu231ap.pdf.txt`: Table 24-102 (`MMCHS_STAT`, bits 16 and 15),
  Table 24-100 (`SYSCTL` `CLKD` — `0x2 = FCLK/2` … `0x3FF = FCLK/1023`).
* `kernel/archive/wl1283/measurement-instrumentation-2026-11-14.patch` (the
  restored `PBX`/`PBIRQ` traces, 66 lines).
* Captures and prior measurements: reports 217 §2.1 (the oracle's pre-driver
  `GPIO2_DATAIN = 0x08000018`), 218, 220, 225.
