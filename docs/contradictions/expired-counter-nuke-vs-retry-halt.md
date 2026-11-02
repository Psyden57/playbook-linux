# `0xFFFFFFFF`: normal-path nuke vs retry-branch failure

**Status: open (wording clarified; the erase behaviour is untested).** The
expired unit is non-booting either way, but *which* branch it takes and whether
anything is erased is not settled.  There is **no sacrificial unit left** (the
spare is down and the two 32 GB units are the expired ones), so the trace
experiment below is not currently runnable; the question stays open rather than
being resolved by convenience.

**Related `BOOTCTR_FCTRY` files (not merged):**
`71-bootctr-stage-decrement-vs-provision-only.md` is the **decrement mechanism**;
`bootctr-drain-vs-session10-readings.md` is the **drain rate** during normal
booting. This file is only the **out-of-range** case (`0xFFFFFFFF`, `0`, length
≠ 4): which path an expired record takes and whether it erases anything.

## The claims

**Session 4, `docs/reports/72-loader-bootmode-action2.md` §3.1 and §6
(2026-10-21).** “The expired unit's bootloader still nukes it”; the bootloader
reader returns `2` for `0xFFFFFFFF`, and `72` §6 contrasts that with the
normal-path “valid factory counter on the normal path still reaches the nuke
branch (`0x80014a16`).”

**Session 5, `docs/reports/75-nvram-2801-password-record.md` §5–§6 and
`76-live-0x2801-audit.md` §1 (2026-10-21…25).** `0x2801` is the password
record; the live unit is `A=0`, `B=10`, so the gate at `0x80014998` is nonzero
and boot main takes the **retry branch** `0x80014A1E`. On that branch the RPMB
selector is read again, but selector `2` is the dispatcher's failure status
`0x0F` (not the normal-path `0x800149C2` `cbnz` nuke). A failed retry-loop
verification “hangs in `FUN_80014C98` rather than reaching the nuke, so it does
not erase the region, but it also does not boot.”

## Evidence

* `71-hd0-mode-map-and-verifier.md` §2: selector `0` → public-key, `1` → CRC,
  `2` → **status `0x0f`, failure**. `71` §3 step 5 is the retry path; the
  normal-path `0x800149C2 cbnz r0, 0x80014a16` nuke is step 3.
* `75` §5/§6: the gate is `A < clamp(B,3,10)`; a nonzero gate jumps to
  `0x80014A1E` **before** the selector read. The retry branch never reaches the
  normal-path `0x800149C2` nuke.
* `76` §1: the live reference unit is already `A=0`, `B=10`; the gate is 10 and
  the unit takes the retry branch on every boot. (`76` is the reference 64 GB,
  not the expired 32 GB; the expired unit's `0x2801` was not read in that
  audit.)
* `72` §3.1: the `0xDE` read on the expired unit returned `product/0`, which is
  the `LDR_01` reader's explicit `0xFFFFFFFF → 0` sentinel; the bootloader
  reader `0x800090CE` has no sentinel and returns `2`.
* `92` §0/§5 (and `71` §2): a value of `0`, `>0x32`, `0xFFFFFFFF`, or a
  non-4-byte record makes the selector `2`; `92` is the explicit “halt on the
  retry branch” wording. `89` §3.5 documents the factory path's counter
  decrement, not the halt.

## What is still uncertain

* **The expired unit's `0x2801`/`0x2040` state.** Session 5's live `A=0/B=10`
  is the reference unit. The expired 32 GB unit has no OS, so its NVRAM could
  not be read with `nvram-editor`; whether it is in the retry branch (gate
  nonzero) or the normal path (gate 0) is inferred from the model, not
  measured. If it is on the normal path, `0x800149C2` *is* the nuke; if it is
  on the retry branch, selector 2 is `FUN_80014C98` failure.
* **Whether `FUN_80014C98` erases anything.** `75` §6 says it “hangs … rather
  than reaching the nuke, so it does not erase the region.” That is a reading
  of the call graph, not a live observation. The retry failure path was not
  serial-traced on a unit.
* **Whether the “expired” label in CFP maps to selector 2 or to a separate
  CFP-side interpretation.** CFP's “Expired factory mode” is a host-side
  message; it does not prove which bootloader branch ran.

## What would settle it

1. Read `0x2801`/`0x2040` on the expired unit **without an OS** — not possible
   with `nvram-editor`; would need a pre-OS NVRAM reader or a loader command
   that exposes them.
2. Serial-trace or instrument `0x80014998`/`0x80014A1E` and `FUN_80014C98` on a
   sacrificial unit with `0xFFFFFFFF` and a known `0x2801` state.
3. Audit `FUN_80014C98` and its callees for any NVRAM/region erase before the
   halt.

Until then: say “`0xFFFFFFFF` is selector 2; the expired unit does not boot.
Whether that is the normal-path nuke or the retry-branch halt depends on
`0x2801`, which was not read on that unit.”

## Session 15 reading (added 2026-10-29, not a resolution)

The split was re-read from `71` §2/§3, `75` §5–6 and `92` §0.6 while writing
`docs/project/FACTORY-PATH.md`, and it matters for a question the page has to
answer: *what happens when a CRC-sealed image is flashed onto an **unarmed**
unit?*  The answer depends on the branch, so the page now states both:

* **retry branch** (`0x2801 A < clamp(B,3,10)`; the surviving reference is
  `A=0/B=10`):
  the verify fails and the unit halts at `0x800094BE` (`b .`) without erasing
  anything (`92` §0.6; `75` §5–6).
* **normal path** (`A >= B`): `0x800149C2 cbnz` → `0x80014A16` → `0x800156E8`
  (`71` §3.4).  The erase has never been observed.

This does **not** resolve the contradiction above — the expired unit's branch is
still unknown, and `FUN_80014C98`'s erase behaviour is still a call-graph reading.
It only records that the *normal* case for these units is "does not boot", not
"wipes itself", which is what the operator observed in practice and what
`FACTORY-PATH.md` §1/§4 say.  A mid-session draft of that page claimed the nuke
unconditionally; the operator pushed back and the page was corrected the same
turn.

