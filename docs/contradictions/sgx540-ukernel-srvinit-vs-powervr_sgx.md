# SGX540 microkernel: a `srvinit` two-context image vs a `powervr_sgx` one-context core

## The claims

**A. The open microkernel in `ukernel/ukernel.py` is a complete, validated
microkernel for an SGX5xx core** — it assembles, passes every offline emulator
test, and `docs/UKERNEL.md` / `ukernel/README.md` describe it as the open
replacement for the closed EDM microkernel, with `ukernel/run_ours.py` showing it
programs every struct-sourced register the DDK does.

**B. It cannot be loaded by `powervr_sgx` on a single-context core as it stands.**
`drm/tools/mkukfw.py` (the wrapper the driver requires) enforces, for a core
without `MULTIPLE_MEM_CONTEXTS`, `SINGLE_CONTEXT` set, `SECURE` clear, **and a
`pdtable` patch**; the driver then allocates a PD table at
`kernel_code.base + 0xf000` and expects the microkernel to load directory list 0
from it.  The image instead routes the TA/3D requestors to **list 1** through
`BIF_BANK0` and relies on list 0 being a shared kernel PD that **`srvinit`** sets
up.

## Evidence for each

* **A** is the upstream project's own claim, backed by its emulator tests and the
  golden SGX545 image (`ukernel/golden/sgx545.sha256`).  It is true for the
  configuration the image was written for.
* **B** is in the code, twice over:
  * `drm/tools/mkukfw.py` (`SINGLE_CONTEXT = 0x8`, and `check()` rejecting an
    image whose single-context core has no `pdtable` patch), and
    `drm/powervr_sgx/pvrsgx_fw.c` (`pdtable_init()`, and `fw_build_open()`'s
    `UKFW_PDTABLE` branch).
  * `ukernel/ukernel.py`'s own comment at the transfer handler: *"under `srvinit`
    list 0 is a shared PD that happens to map them; under `powervr_sgx` list 0 is
    an empty idle PD and the transfer BIF-faults on a client address
    (BANK0=0x0f, DIR_LIST_BASE0=idle, FAULT on 0x3e001000).  We do NOT write the
    ctx PD into list 0 — that clobbers the kernel PD the 3D/ISP/PBE read through
    and wedged srvinit."*

On hardware (session 73), the driver found the image and rejected it with
`not a microkernel file` (no `SGX5`/`UKRN` header), the first of the two gates.
The second (`SINGLE_CONTEXT`/`pdtable`) was not reached and is unproven on
silicon — it is a reading of the code.

## What is uncertain

* Whether the list-0 load is *sufficient*, or whether other parts of the image
  also assume two contexts.  `ukernel/ukernel.py` has ~39 references to
  `list 1`/`BANK0`/`0xc78`; how many are load-bearing is unknown.
* Whether the deferred completion semantics (`CLEANUP_V2`, `DONE_SEQ`) that the
  image implements match what `powervr_sgx` expects when `SECURE` is dropped and
  `SINGLE_CONTEXT` is set.

## What would settle it

1. Add the `pdtable` patch site to the microkernel and load list 0 from it; set
   the single-context feature bits; wrap with `mkukfw.py --manifest`; boot.
2. If `powervr_sgx` then accepts the image and the core reaches a kick, **A** is
   true for a one-context core too and this file closes.
3. If it faults on a client address with `DIR_LIST_BASE0` idle, the assumption is
   still unsatisfied somewhere else — record which register and reopen.

Never resolve by guessing: the register tables themselves (0x618, 0x62c, 0x694,
TA kick 0x200, 3D kick 0x428) are also unmeasured on the SGX540 and are carried
over from the SGX545 as hypothesis; they are a *separate* unknown from this one.

**Status: open** (session 73).  Evidence:
`docs/captures/sgx540-ukernel-2026-11-26.txt`,
`docs/reference/sgx540-factsheet.md` §"The microkernel".
