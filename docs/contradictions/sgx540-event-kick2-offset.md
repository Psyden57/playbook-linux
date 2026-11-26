# The SGX540's second kick register is at 0x0ac0 or 0x0ae8 — the docs disagree

**Status: OPEN.**  Raised in session 75 while turning the doorbell into the next
experiment.  Read back the three addresses; that settles it in one boot.

## The claims

**(a) 0x0ac0 — `docs/reference/sgx540-factsheet.md`, "Events and kicks".**  The
fact sheet's register table gives the SGX540:

```
EVENT_KICK1 / EVENT_KICK2   0x0ab0 / 0x0ac0     (SGX545: 0x0ae4 / 0x0ae8)
EVENT_KICK                  0x0ac8              (SGX545: 0x0ac8)
EVENT_KICKER                0x0ac4  (ADDRESS b4-27, a 24-bit field at shift 4)
PDS_EXEC_BASE               0x0ab8
EVENT_TIMER                 0x0acc
```

read from the GPL-2 DDK 1.17/1.8 headers, and it states explicitly that
"`SGX_FEATURE_MULTI_EVENT_KICK` **is set** — the microkernel start can use
`EVENT_KICK2` (the SGX530 cannot)".

**(b) 0x0ae8 — `drm/powervr_sgx/pvrsgx_regs.h`.**  The driver's own header has
`SGX_CR_EVENT_KICK2 0x0ae8` (with `SGX_CR_EVENT_KICK 0x0ac8`), i.e. it keeps the
SGX545's offset for the second register.

**(c) 0x0ac0 — `drm/powervr_sgx/pvrsgx_core.c`, `sgx540_regs`.**  Our SGX540
entry sets `.event_kick2 = 0x0ac0` and `.event_kick2_now = BIT(0)` — agreeing
with (a), and disagreeing with (b), in the same tree.

## Why it matters now

`pvrsgx_fw_start()` takes

```c
if (cr->event_kick2)
        sgx_wr(sdev, cr->event_kick2, cr->event_kick2_now);
else
        sgx_wr(sdev, SGX_CR_EVENT_KICK, SGX_CR_EVENT_KICK_NOW);
```

so on a core whose `event_kick2` is non-zero **the init kick goes to
`event_kick2` and the per-job kick in `ccb_write()` goes to `EVENT_KICK`**.  On
this port that means the init kick was written to **0x0ac0** and every job kick
to **0x0ac8**.  If in fact the core has one doorbell (which is what the DDK's own
`BRN_22934` comment in `pvrsgx_fw.c` implies — "a core with one kick register
takes it there") and the correct address is 0x0ac8, then the init kick went to an
unnamed register and the job kicks went to the right one; if the correct address
is 0x0ac0, it is the reverse.  Either way the two kicks are aimed at *different*
registers, which is a sufficient cause for the measured symptom: **the init runs
once and no job is ever dispatched** (report 278).

Note this is not the same as "the video PDS exec address pair is a guess"
(`event_pds_guessed`); that is a separate, already-recorded unknown
(`docs/reference/sgx540-ddk18-facts.md` §9).

## What is uncertain

Whether 0x0ac0 and 0x0ac8 are two registers, one register, or one register plus
an alias.  The DDK's `BRN_22934` suggests some cores have only one, and the
driver's own comment says so — but our entry supplies both, and
`pvrsgx_core_family_usable()` only requires `kick2 == !!cr->event_kick2`, so it
accepts an entry that sets both fields on a core that may not have both.

## What would settle it

One boot, from **inside the driver** (trap 384 — `/dev/mem` is closed in this
configuration): after `pvrsgx_fw_start()`, read back **0x0ab0, 0x0ac0, 0x0ac8
and 0x0ae8**, then submit a NULL job and read them again.  A register that does
not exist on a gated core returns 0 and stays 0; the one the doorbell uses
changes.  Cheapest form: three extra lines in the `ctlblk` debugfs dump
(`pvrsgx_debugfs.c` already prints named registers through `pvrsgx_dump_status`).

## Recorded

Session 75 (turning the doorbell into the next experiment); report 278 §6 and
`docs/project/HANDOFF.md` §0b name it as the first thing to check.  The fact
sheet is not edited here: it is K evidence from a header, and the question is
what the *silicon* does, so this stays open until the read-back.
