# The SGX540's second kick register is at 0x0ac0 or 0x0ae8 — the docs disagreed

**Status: RESOLVED (session 75), and the suspicion that raised it is RETRACTED.**
Settled from the GPL-2 DDK's own kernel driver, without a boot.  Kept as a
record because the wrong reading was written down first and would otherwise be
re-derived.

## The apparent conflict

**(a) 0x0ac0 — `docs/reference/sgx540-factsheet.md` and our `sgx540_regs`.**
The fact sheet's register table gives the SGX540 `EVENT_KICK1`/`EVENT_KICK2` at
**0x0ab0 / 0x0ac0**, and `pvrsgx_core.c`'s SGX540 entry sets
`.event_kick2 = 0x0ac0`.

**(b) 0x0ae8 — `drm/powervr_sgx/pvrsgx_regs.h`.**  The driver's own header has
`SGX_CR_EVENT_KICK2 0x0ae8`, i.e. an SGX545-family offset.

Both are in the tree, so the two cannot both be describing the same register.

## Why it looked like the bug

`pvrsgx_fw_start()` takes

```c
if (cr->event_kick2)
        sgx_wr(sdev, cr->event_kick2, cr->event_kick2_now);
else
        sgx_wr(sdev, SGX_CR_EVENT_KICK, SGX_CR_EVENT_KICK_NOW);
```

while `ccb_write()` unconditionally writes `SGX_CR_EVENT_KICK` (0x0ac8).  With
`.event_kick2 = 0x0ac0`, the **init** kick and every **job** kick are aimed at
different registers — which would explain "the init runs once and nothing
after" (report 278) exactly.  It was recorded as the first thing to check.

## Resolved: the vendor does the same thing

`sgx540defs.h` defines **four** kick registers — `KICK1` 0x0ab0, `KICK2`
0x0ac0, `KICK` 0x0ac8, `KICK3` 0x0ad8 — and the DDK's own kernel driver picks
between them by feature:

| call site | `SGX_FEATURE_MULTI_EVENT_KICK` | without |
|---|---|---|
| init (`eurasia_km/…/sgx/sgxinit.c:469-483`) | `EUR_CR_EVENT_KICK2` **0x0ac0** | `EUR_CR_EVENT_KICK` 0x0ac8 |
| per command (`eurasia_km/…/sgx/sgxutils.c:512-546`) | `EUR_CR_EVENT_KICK` 0x0ac8, or `EUR_CR_EVENT_KICK2` under `FIX_HW_BRN_26620 && SGX_FEATURE_SYSTEM_CACHE` | `EUR_CR_EVENT_KICK` 0x0ac8 |

`SGX_FEATURE_MULTI_EVENT_KICK` **is** defined for the SGX540, and
`FIX_HW_BRN_26620`'s `SYSTEM_CACHE` combination does not apply here (the 540
has no system cache — see the fact sheet's errata note), so the vendor's
sequence on this core is **init → KICK2 (0x0ac0), per command → KICK (0x0ac8)**.
That is precisely what our driver does.

Also, in the non-multi branch only, the vendor increments the kernel CCB's
**event-kicker** word (the memory word `EVENT_KICKER` 0x0ac4 points at):

```c
*psDevInfo->pui32KernelCCBEventKicker = (*psDevInfo->pui32KernelCCBEventKicker + 1) & 0xFF;
```

The multi-event path does not.  Our `ccb_write()` increments it
unconditionally, which is at most cosmetic on this core.

## Where (b) came from

`pvrsgx_regs.h`'s `SGX_CR_EVENT_KICK2 0x0ae8` is the **SGX545-family** value
(the SGX545 header has `KICK1`/`KICK2` at 0x0ae4/0x0ae8).  It is a shared
header constant, and the SGX540's `regs` entry overrides the field with its own
0x0ac0 — so nothing on this core ever reads 0x0ae8.  Not a defect, just two
cores' constants in one header.

## Consequence

**The differing offsets are not the cause of the stall.**  Report 278 §6 and the
session-75 note listed them as the first thing to read back; that item is
withdrawn.  The doorbell question stays open in its own right — whether the
writes *land*, and whether the event program's delivery address is right — and
those are now the next boot's reads, not because of this.

## Recorded

Raised and retracted in session 75; trap **392** in `docs/project/KNOWN-ISSUES.md`
carries the lesson ("check a differing constant against the vendor's call sites
before calling it a defect").  Evidence: the DDK 1.8 drop at
`/home/psyden/Pdrive/GFX_Linux_DDK-master`, files and line numbers above.
