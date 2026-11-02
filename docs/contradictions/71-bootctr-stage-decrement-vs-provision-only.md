# Does the `hd0` stage decrement `BOOTCTR_FCTRY`, or only provision/delete it?

**Status: resolved (session 20) — the stage decrements.**  `71` §7b (session 3)
says the stage never decrements; `89` §3.5 (session 6), `95` §4 and `116` §2 say
a successful factory/CRC boot does, and the **Session 20 evidence** section at
the end of this file gives the instruction-level mechanism.  This file exists
because `71` §7b is still in the tree and its "never decrements" sentence is used
for budget reasoning if read alone.

**Related `BOOTCTR_FCTRY` files (not merged):**
`bootctr-drain-vs-session10-readings.md` asks *whether* the counter drains during
a session of boots (the rate); `expired-counter-nuke-vs-retry-halt.md` asks what an
out-of-range value (`0xFFFFFFFF`) does at boot. This file is only the
**mechanism** — whether the stage decrements at all and through which caller.

## The claims

**`71` §7b (session 3, 2026-10-20/21).** After decoding the RPMB record path:

> “`0x800090a0(1)` **provisions** the record with value `0x32` (50) via
> `0x800115e6("BOOTCTR_FCTRY", &0x32, 4)` … `0x800090a0(0)` deletes it via
> `0x8001167e`. **This stage only reads/provisions/deletes the record — it never
> decrements it (`0x800115e6` has exactly one caller, `0x800090c0`; `0x8001167e`
> exactly one, `0x800090a6`). The budget is consumed by the factory/CFP
> tooling.**”

The handoff written from that session repeats the practical conclusion: the
general writer is `/dev/rpmb` on a booted OS; the bootloader can provision an
*absent* record at `0x32`, but it does not repair `0xFFFFFFFF`.

**`89` §3.5 (session 6, 2026-10-25).** From the disassembly of the successful
factory path:

> “on a successful factory verify (`0x80014A3E`–`0x80014A4A`) **[P]**:
> `bl 0x8000911A` → non-zero → `*(0x80026150) = 1`; `bl 0x8000911E` →
> read record, `value-1`, write back via `0x800115E6`. **One factory boot = one
> budget unit.** If the RPMB write fails it logs and continues booting.”

**`95` §4 (session 6, 2026-10-25).** The 5-byte reseal test (`os_type` 4 → 6,
CRC `0x8D46FB4A` → `0x116B2562`) booted TabletOS and the counter went **50 →
49**; `0x2074` and `0x2801` were byte-identical. That is a direct measurement of
one successful factory boot consuming one unit.

**`116` §2 and its addenda (sessions 10/11).** The TabletOS-side
`top_up_bootctr` script exists precisely because every successful factory/CRC
boot decrements; the counter was later measured draining `0x32` → `0x28` over
~10 boots with the repair refusing. The script's own comment states the
decrement semantics.

## Evidence (each claim's own)

* **For `71` §7b:** the caller audit of the two record routines — `0x800115E6`
  (create/update) with exactly one caller `0x800090C0` (inside `0x800090A0(1)`,
  the provision path) and `0x8001167E` (delete) with exactly one caller
  `0x800090A6`. Nothing on the verify path appeared in that count
  (`docs/reports/71-hd0-mode-map-and-verifier.md` §7b).
* **For `89` §3.5:** the disassembly of the successful factory verify
  `0x80014A3E`–`0x80014A4A` shows `bl 0x8000911A` (selector) then
  `bl 0x8000911E`, which reads the record, subtracts one and writes it back
  through `0x800115E6`; a failed RPMB write logs and the boot continues
  (`docs/reports/89-ifs-crc-recipe.md` §3.5).
* **For `95` §4:** the controlled 5-byte reseal (`os_type` 4 → 6, CRC
  `0x8D46FB4A` → `0x116B2562`) booted TabletOS and the counter went **50 → 49**,
  with `0x2074` and `0x2801` byte-identical
  (`docs/reports/95-crc-path-proof-2026-10-25.md` §4).
* **For `116` §2 and addenda:** the TabletOS-side `top_up_bootctr` exists because
  each successful factory/CRC boot decrements, and the counter was measured
  draining `0x32` → `0x28` over ~10 boots while the repair was refusing
  (`docs/reports/116-rpmb-bootctr-plan-2026-10-27.md` §2, addenda 1–2;
  `session-11.md`).

## Why they cannot both be right

`0x800115E6` is the record create/update routine. `71` §7b counted its callers
as exactly one (`0x800090C0`, inside `0x800090A0(1)`) and concluded the stage
only provisions/deletes. `89` §3.5 says the successful factory path calls
`0x8000911E`, which reads the record, subtracts one, and writes it back through
`0x800115E6` — i.e. a **second** caller. If `89` is right, `71` §7b's caller
count is incomplete (it may have missed the `0x8000911E` call because the
function is in a different Thumb/ARM region or was reached through a tail
call), and the “never decrements” sentence is wrong. If `0x8000911E` does not
actually reach `0x800115E6`, then `89` is wrong and the 50 → 49 measurement
needs another explanation.

The empirical evidence (50 → 49 on one controlled factory boot; the 0x32 → 0x28
drain) is hard to explain without a decrement. The most likely resolution is
that the stage does decrement, and `71` §7b’s caller audit was incomplete.

## What is still uncertain

*This section is historical: the mechanism is settled by the Session 20
evidence below.  The only remaining question is the session-10 rate anomaly,
tracked in `bootctr-drain-vs-session10-readings.md`.*

* **Whether the decrement is unconditional.** The `0x80014A42`–`0x80014A4A`
  path is on the successful factory verify; a failed verify or a reset-loop
  iteration may not reach it. The existing
  `bootctr-drain-vs-session10-readings.md` records the separate puzzle that
  session 10 read `0x32` throughout many cycles while the repair was broken; a
  conditional decrement (or another repair) is one candidate.
* **Whether the loader or CFP ever writes the counter independently.** `71`
  §7b and `72` §6 say the shipped loader has no live setter; the host CFP
  setter is not invoked by any CLI. The decrement is the only stage-side write
  found.
* **The exact `0x8000911E` disassembly.** `71` §7b did not include it in its
  caller count; `89` §3.5 cites the call but does not reproduce the function.
  A single disassembly of `0x8000911E`–`0x8000915x` would settle the mechanism.

## What would settle it

1. Re-disassemble `0x8000911E` and `0x800115E6` in the `hd0head.bin` mode map
   (`71` §1) and count every `BL`/`BLX` and literal reference to
   `0x800115E6`. If `0x8000911E` calls it, `71` §7b’s “exactly one caller” is
   wrong.
2. One controlled cycle on a sacrificial/working unit: read the counter, boot
   the factory/CRC image **once** (restore promptly, no reset loop), boot
   TabletOS, read again. The fixed `startup.sh` logs the pre-write value
   (`wrote 0x32, readback ok (was '1')`), which is itself a one-boot
   measurement. Pair it with the chainloader breadcrumb/ring-header count so
   reset-loop iterations are not miscounted.
3. Until then: **use `89`/`95`/`116` for budget math, not `71` §7b’s “never
   decrements”.** The current docs say one successful factory boot costs one
   unit; treat the ≤50 budget as a real budget, and never test a write on the
   working unit (`116` §2.2).

## References

* `docs/reports/71-hd0-mode-map-and-verifier.md` §7b — the “never decrements”
  claim and the caller counts.
* `docs/reports/89-ifs-crc-recipe.md` §3.5 — the successful-factory decrement
  (`0x80014A42`–`0x80014A4A` → `0x8000911E` → `0x800115E6`).
* `docs/reports/95-crc-path-proof-2026-10-25.md` §4 — the 50 → 49 measurement.
* `docs/reports/116-rpmb-bootctr-plan-2026-10-27.md` §2 and addenda — the
  top-up semantics, the broken script, the 0x32 → 0x28 drain.
* `docs/contradictions/bootctr-drain-vs-session10-readings.md` — the separate
  drain-rate puzzle.

## Session 20 evidence (reports 150 and 154) — the mechanism, from the bytes

`150` traced the decrement at instruction level and `154` corrected two points:

* the decrement is `0x8000912E subs r0,r0,#1`, written by `record_write`
  `0x800115E6` → `0x800114FA` → `0x80010B2E`, **after** the region-0 verify and
  **before** the jump to the image [P] 150 §2;
* the re-seal uses the **ARM dispatcher `0x800183DC`** (SMC
  `0x31`→`0x33`→`0x2F`→`0x33`→`0x2E`, domain 5) and then RPMB type 2/3/5 [P]
  150 §2;
* **if that write fails, the stage does NOT continue booting**: the create and
  rewrite paths call the blink emitter with level 0 and code **`0x2F`** and never
  return — i.e. a failed write produces the **1-4** LED pattern and the unit
  hangs in the emitter loop [P] 154 §Q2.  This corrects `89` §3.5's "logs and
  continues booting" **and** report 150's own "returns 0 either way".

So the answer to the file's title is now: **the stage reads, provisions, deletes
AND rewrites (decrements) the record**, and a failure of that rewrite is fatal to
the boot.  `71` §7b's "never decrements" and the `FACTORY-PATH.md` §2 sentence
"it has no write path" are both superseded by the measured decrement and the
disassembled write path.
