# `85` A/B fallback vs the factory path's region 0

**Status: resolved** — the factory/CRC path is region 0 only, once, with no A/B
fallback.  `0x2074` is product-path state.

## The claims

**Session 5, `docs/reports/85-linux-boot-plan.md` §1–§3 (2026-10-25).**
The retry loop calls the region driver `0x800142A8`, which verifies the
`requested` region first and falls back to `requested ^ 1`; `boot_count_*` /
`max_boot_count` gate the attempts.  Because the RPMB selector is global, the
plan concluded:

* “the selector is global, so once `BOOTCTR_FCTRY` is 1..50 both regions use
  CRC”;
* Phase 1/2 should stage a copy in region 1 (`hd2`), switch `requested=1`, and
  expect the empty region to fail and region 0 to boot;
* Phase 3 must make **both** regions CRC-valid before Phase 4 writes the
  counter.

**Session 6, `94` §0, `95` §5, `97` §0 (2026-10-25).**
`0x800142B0` reads the RPMB selector `BOOTCTR_FCTRY`; `cbz r0, 0x800142D0` sends
0 to the product path, and a nonzero value does `region := 0; bl 0x80014148;
return`.  It never reads NVRAM `0x2074` and never tries region 1.  The factory
path is region 0 only, once, no fallback.

## Evidence

* **Session 5's own `83` §6 already had the fact.**  “Factory path skips the
  record entirely: if `FUN_8000911A()` (the RPMB `BOOTCTR_FCTRY` probe) is
  non-zero, `0x800142BC-0x800142C6` sets the region index to 0 and verifies
  region 0 directly — `0x2074` is not read at all.”  The `0x800142A8` driver
  with requested/actual and fallback is the **product** path
  (`0x80014548` → `0x800142A8`).  The `85` plan did not follow that through.
* **Session 6's live proof, `95` §2–§4.**  The region-0 image was resealed with
  five bytes (`image 0x100: 04 → 06`), CRC `0x8D46FB4A → 0x116B2562`; the same
  TabletOS booted, `BOOTCTR_FCTRY` went 50 → 49, and `0x2074` and `0x2801` were
  byte-identical.  That is a region-0-only factory boot; nothing read region 1.
* **`97` §0** states the factory path hardcodes region 0; `94` §0 collates the
  correction.

## Resolved

The factory/CRC path is **region 0 only, once, no A/B fallback**; `0x2074` is
product-path state.  `85` §1–§3's plan-level assumption was wrong, and the
corrected reading in `94` §0 / `95` §5 / `97` §0 is current.  Nothing was
retracted about the product path's fallback driver — it exists, it is just not
what a factory boot uses.

## What is still uncertain

* Whether the **product** path's A/B fallback works on this unit.  `90`
  documents the `switch()`/`sane()` runbook, but the factory path does not use
  it and the product fallback was not exercised live.
* A “fallback image” for the CRC path cannot be the other A/B region.  Both
  images would have to occupy region 0, so the fallback is the pre-OS reflash
  (`0xF7`, `98`/`101`), not region 1.

## What would reopen it

A factory-path code path that reads `0x2074`, or a boot log on a nonzero
`BOOTCTR_FCTRY` showing “Requested region 1” / “Verifying region 1”.  None was
found in `94`/`95`/`97`.
