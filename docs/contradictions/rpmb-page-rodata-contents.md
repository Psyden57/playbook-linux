# The RpmbInit page's `+0x20`/`+0x40` slots: `.rodata` constants vs "never written"

**Status: open (mechanism); contents settled by measurement (session 19).**

Two reports disagree about what the 16-byte slots at `page+0x20` and `page+0x40`
of `rpmb-omap4`'s non-cached page contain and whether anything writes them.

## The claims

**Report 92 §2.4.**  The pointer variables `0x18250` and `0x18258` point to
`page+0x020` and `page+0x040`, whose **contents** are the two 16-byte `.rodata`
constants at `0x5A94` (`86 78 7C 2A 63 2F BE 16 48 EC AB A6 D8 84 01 09`) and
`0x5A84` (`44 3A C2 02 EB 50 05 1F 0C 2C 12 B3 4A C9 3B 4D`), and they are
handed to services `0x33`/`0x2E`/`0x2F`.

**Report 141 §3.2.**  The two `.rodata` constants are **dead data**: the image
copies them to the *stack* at `0x301c`–`0x303c` and never reads them again;
nothing ever writes the **contents** of `page+0x40` or `page+0x60` (only the
pointer slots), so the 16-byte key handed to `0x33` is zeros, not the constants.

## Evidence

* **Measured (session 19):** the live page (`rpmb-omap4`, paddr `0xbfb2b000`,
  this session's QNX boot; head slots in
  `docs/reports/data/rpmb-page-head-0x000-2026-10-30.txt`, full dump in
  `docs/reports/data/rpmb-page-0xbfb2b000-2026-10-30.txt` for the `0x400`
  region) holds `+0x020 = 86787c2a632fbe1648ecaba6d8840109` and
  `+0x040 = 443ac202eb50051f0c2c12b34ac93b4d`, with `+0x060 = 0`.  **[P]**: the
  `.rodata` contents are really there; report 141's "zeros" is wrong for
  `+0x20`/`+0x40`.
* **Measured/re-read:** `RpmbInit` (`0x2ffc`–`0x3104`) stores only the
  **pointers** (`page+0x000/+0x020/+0x040/+0x060/…`) into the context; it does
  not copy the constants.  So something else populates `+0x20`/`+0x40`
  (the monitor's key-derivation output? a path not yet decoded).  **[P]** for
  the stores; **[U]** for the writer.
* **Consistent with:** the seal path passes `key33 = [context+0x30] =
  page+0x20` to service `0x33` (`0x2bf0`/`0x2530`); the monitor therefore does
  receive `8678…` as the key on this unit.  Report 146 confirms the marshaller
  preloads `page+0x20` before `0x2F`.

## What is uncertain

Who writes `page+0x20`/`page+0x40` and when (the monitor after the `0x2F` op=1
derivation, or a code path neither report decoded).  Whether the monitor reads
the page slots or only the caller's command-page copy (our Linux call passes its
own copy of `8678…`, so the page contents do not affect it).

## What would settle it

A write watchpoint on the page slots during a QNX boot (or a trace of the
monitor's `0x2F` op=1 output), and a static trace of every store to
`page+0x20`/`+0x40` in `rpmb-omap4`/`trustzone-omap4`.  Until then: read the
measured contents, and do not trust either report's "dead/zeros" claim.

## References

* `docs/reports/92-rpmb-factory-write-runbook.md` §2.4.
* `docs/reports/141-linux-rpmb-topup-feasibility-2026-10-30.md` §3.2.
* `docs/reports/143-linux-rpmb-key-pinned-2026-10-30.md` §2.
* `docs/reports/data/rpmb-page-0xbfb2b000-2026-10-30.txt`.
* `docs/reports/146-monitor-caller-rejection-re-2026-10-30.md` (the marshaller).
