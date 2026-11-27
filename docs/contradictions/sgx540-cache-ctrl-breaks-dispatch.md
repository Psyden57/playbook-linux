# CACHE_CTRL: the vendor's own launch value breaks event dispatch on our open stack

Raised session 78 (2026-11-27).  Status: **unresolved** — both readings are
measured; what they disagree about is which configuration is wrong.

## The two claims

1. **The vendor's `srvinit` writes `EUR_CR_CACHE_CTRL` (0x0804) =
   `0x0000ffff`** — `PARTDM0..3_MASK` OR'd (sgxinit.c:4079-4087, guard
   `#if defined(EUR_CR_CACHE_CTRL)` which the 540 header satisfies).  The
   vendor's stack ran this exact value in production on SGX540-class silicon,
   and the closed stack ran on *our* unit for months with the GPU rendering.

2. **On our open stack, writing 0x0000ffff breaks event dispatch entirely.**
   Measured (session 78, boots 11-13): with the complete remaining vendor
   launch delta programmed, even the FIRST timer dispatch dies (entry stuck at
   1, tick stuck at the 0x100 seed); bisected over three boots to
   CACHE_CTRL — removing it (leaving the reset value) restores dispatch, and
   with the fix in `EVENT_PDS_INFO`'s USE_PIPELINE the pipeline runs
   continuously.  See report 281 and trap 411.

## What is uncertain

* The semantics of `PARTDM0..3` (b0-3/b4-7/b8-11/b12-15) on this silicon:
  "cache partitions allocated to data master N" is inferred from the field
  names, not measured.  The reset value on our unit is also unknown (the
  register is not in the `status` dump while it stays unwritten).
* Whether the *closed* stack actually runs 0x0000ffff live: the closed UM's
  srvinit is this same GPL source, but the DDK registry apphint
  **"CacheCtrl"** overrides the derived value at load
  (`OSReadRegistryDWORDFromString(0, …, "CacheCtrl", &ui32CacheCtrl)`), and
  the closed system's registry contents are unknown.  The closed stack may
  never have run 0xffff.
* Whether the breakage is an interaction with something else in our launch
  (e.g. our `EVENT_PDS_*` values, or the µkernel's own cache usage) rather
  than with 0xffff alone.

## What would settle it

* Read 0x0804 live on the **closed** stack while the GPU renders (a
  `/dev/mem` read with `pvrsrvkm` bound, as the session-73 identity read did)
  — if the working system holds something other than 0x0000ffff, the vendor's
  srvinit value was overridden and our reset-value choice is right.
* On the open stack: write 0x0804 = each PARTDM field alone (the per-bit
  sweep pattern, trap 386/397) and read back + observe the tick counter —
  field semantics by measurement.
* Until then: **do not write 0x0804** (trap 411).
