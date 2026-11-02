# The `BOOTCTR_FCTRY` drain rate vs session 10's readings

**Status: resolved for the rate (session 22); the session-10 anomaly is still
open.**  The counter moves one step per factory boot and is topped back up by an
OS boot — that is now measured.  Why session 10 read a static `0x32` across many
cycles is not explained.  The consequences are not academic: the counter is the
factory boot budget, and `0` (or `> 0x32`) hard-locks the unit ("expired factory
mode", physical recovery only — report 116 §2.2, report 116 "Session 5
addendum").

**Related `BOOTCTR_FCTRY` files (not merged):**
`71-bootctr-stage-decrement-vs-provision-only.md` is the **mechanism** (does the
stage decrement at all — settled in favour of yes); `expired-counter-nuke-vs-retry-halt.md`
is what an **out-of-range** value does at boot.  This file is only the **rate**:
whether a session of boots visibly moves the counter.

## Claim A — the counter read `0x32` throughout session 10

Session 10 (`docs/sessions/session-10.md`) read the counter at the end of every
flash/boot cycle, i.e. after the sealed IFS had been restored and TabletOS was up:

```
pbssh 'dd if=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1'      -> 32000000
pbssh 'dd if=/dev/rpmb/BOOTCTR_FCTRY bs=1 count=1'      -> 32
```

* It was read at least a dozen times across roughly 25–30 flash cycles (the epoch
  runs, the eMMC run, the SMC/GIC runs, the USB runs, the console runs) and never
  read anything else.
* The session also recorded ~25 `bootctr -> 0x32 ok` lines in
  `/accounts/devuser/.bootctr.log` and read the script's own text, and concluded
  the reading was static **because the script repairs it every TabletOS boot**.

## Claim B — the counter drains one per boot, and the repair was a no-op

Session 5's addenda to report 116 (in
`docs/reports/116-rpmb-bootctr-plan-2026-10-27.md`, "Session 5 addendum" and
"Addendum 2"), and `docs/sessions/session-11.md` §Confirmed:

* `printf` **does not exist on TabletOS** (neither builtin nor binary), so
  `printf '\062' | dd of=/tmp/bc32.bin …` produced nothing and the script wrote
  **`0x00000000`** — six times per boot — which the device refused as out of
  range.  `.startup.log` shows `startup.sh[40]: printf: cannot execute`.
* The readback check only re-read the counter, so it reported `ok` while asking
  the device to store zero.  **The log lines are therefore not evidence that a
  repair happened.**
* Measured: with the repair refusing, the counter went **`0x32` → `0x28` (50 → 40)
  over roughly ten boots** (2026-10-27 15:33 → 16:18).  So it does drain, one per
  factory/CRC boot.
* Follow-on evidence that writes land: the operator set `0x28` by hand, ran the
  fixed script, and it logged `bootctr: wrote 0x32, readback ok (was '(')` and the
  counter read `2\000\000\000`.

## Session 22 update — the rate, measured on one boot sequence

The counter was read repeatedly on 2026-11-01 while the unit was booted into the
IFS and back into TabletOS: **`50 → 49 → 47 → 49 → 50`** (report 160/161,
session-22 note).  The falls came from **boots into the IFS** (the operator
reached the splash and powered off); loader/`info` sessions did not move it.  The
rises came from the **OS top-up** (`accounts/devuser/startup.sh`).  So, going
forward:

* one successful factory/CRC boot = one count (Claim B);
* an OS boot repairs it toward `0x32` (Claim A's "it looks static at the end of a
  cycle" is true *because* the top-up runs);
* the counter is **not monotonic** — it can rise as well as fall.

That settles the rate and the budget arithmetic.  It does **not** explain why
session 10, with the broken repair, still read `0x32` at every checkpoint; the
candidate explanations below remain on the table for that historical anomaly.

## Why they cannot both be right

If the repair was a no-op during session 10 as well (the script was already the
`printf` version — its mtime was 2026-10-26 07:02 and session 10 read that exact
line), and each factory/CRC boot decrements by one, then ~25–30 flash cycles
(plus, while a test image sat on media, a **reset loop every ~7.8 s**, each
iteration another factory boot) should have driven the counter far below `0x28`,
and quite possibly to `0`.  It read `0x32` every time.

## Candidate explanations (all unverified)

1. **The decrement is conditional.**  It may happen only on some factory boots
   (a flag, a first-boot-after-something condition, or a state the reset loop
   does not reach), so a session full of watchdog-reset boots may consume nothing.
2. **Something else repaired the counter during session 10** — an earlier working
   copy of the script, a different consumer on the TabletOS side, or the operator.
   Not supported by anything read in the session, but not excluded either: only
   one file (`/accounts/devuser/startup.sh`) was inspected.
3. **The loader re-writes the counter itself** at some point in the boot, e.g.
   when it takes the factory path with an intact CRC, which would mask a
   decrement.
4. **Session 11's `0x32 → 0x28` had a different cause** (the operator's hand
   operations around the same period, or a burst of boots that happened to
   decrement).  Nothing in the addenda rules this out, but the addenda's
   arithmetic (10 boots, 10 counts) is otherwise clean.

## What would settle it

1. **One controlled cycle — done in session 22** (the `50 → 49 → 47 → 49 → 50`
   sequence above).  The remaining use for it is to reproduce the session-10
   anomaly, not to establish the rate.
2. **Count the boots.**  The reset loop makes "boots" ambiguous; the chainloader
   writes its breadcrumb (`KBLB`) on every boot, and the DRAM ring header is
   zeroed per boot, so a per-boot counter is available (`ring1 +0x80` before and
   after, or a spare word written by the loader).
3. Only then re-derive the budget: whether it is "50 boots" (Claim B, worst case)
   or much more (Claim A).

## Why it matters for the plan

* `docs/project/STATE.md` and every kickoff prompt quote a boot budget of "~20
  boots against `BOOTCTR_FCTRY` (0x32)".  If the drain is conditional, that is
  over-cautious; if it is one-per-boot *including* the 7.8 s reset loop, a single
  bad cycle can spend several counts and the margin is much smaller than quoted.
* Report 116 §5's ladder already forbids write experiments on the working unit;
  this file does not change that.  It changes only how the *budget* should be
  estimated, and it is a reason to measure the counter at both ends of every
  cycle rather than just at the end.
