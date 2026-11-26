# Report 277 — the SGX540 microkernel: from "consumed 0" to a completed job

Date: 2026-11-26 (session 74).  Bench session on the reference unit
(`0x50175127`, OMAP4430 ES2.2).  Follows report 276 (the open-stack scope) and
its session-73 addendum, and the session-73 capture
`docs/captures/sgx540-ukernel-2026-11-26.txt`, which ended with the driver
rejecting the SGX540 microkernel for want of the `SGX5`/`UKRN` header and a
list-0 path.

**Headline: the open `powervr_sgx` SGX540 microkernel now starts and completes a
NULL job on silicon.**  Four SGX545-shaped assumptions in the image were found
and corrected, three of them from GPL-2 primary sources; the decisive one turned
the universal `consumed 0` timeout into `1/1 passed`.

Evidence in this session's captures:

* `docs/captures/sgx540-ukernel-list0-2026-11-26.txt` — the one-context image
  builds, wraps and passes the driver's own checks (host only).
* `docs/captures/sgx540-ukernel-start-2026-11-26.txt` — the driver accepts the
  image, starts the microkernel, and its own debugfs dump agrees.
* `docs/captures/sgx540-ukernel-first-job-2026-11-26.txt` — the NULL job, the
  measured pass/stall pattern, the BRN-31620 experiment, and the register-access
  constraint.
* `docs/reference/sgx540-ddk18-facts.md` — every K fact taken from the DDK 1.8
  drop, with citations.

## 1. What the driver says now

```
powervr_sgx 56000000.gpu: [drm] SGX540 has one memory context: clients are not isolated from the firmware or from each other (allow_unisolated=1)
powervr_sgx 56000000.gpu: [drm] SGX core id 0x01140000 revision 0x00010200: TI OMAP4430 (SGX540 1.2.0)
powervr_sgx 56000000.gpu: [drm] powervr_sgx/sgx540_ukernel.bin: open microkernel invfix, interface 1.0, features 0x8
powervr_sgx 56000000.gpu: [drm] EDM init: event status 0xa0000000, BIF int 0x8000, timer 0x0102994b
                                    (the timer word is a free-running counter, so it differs per boot)
powervr_sgx 56000000.gpu: [drm] microkernel started
[drm] Initialized powervr_sgx 1.0.0 for 56000000.gpu on minor 1
```

and its debugfs (`/sys/kernel/debug/dri/1/status`):

```
firmware running: 1 lost: 0 resets: 0 irqs: 0
kernel PD 0x87044000 idle PD 0x87044000
hostctl init 0x1 power 0x0 uk-lockups 0 int 0x0/0x0 assert 0x0
BIF_INT_STAT 0x0c04 0x00008000      BIF_FAULT 0x0c08 0x00000000
BIF_DIR_LIST_BASE0 0x0c84 0x87044000
```

So the microkernel is **running**, the BIF is idle and fault-free, and on a
one-context core `idle PD == kernel PD` as the driver intends.

**Starting it needs the documented single-context launch**: the image alone is
not sufficient.  `modprobe powervr_sgx edm_init=1` makes the probe complete;
without it the probe fails 20 ms after `open microkernel …` with
`BIF int 0xc002, fault 0x8779000` and `probe … failed with error -110`.  That is
the driver's own path for exactly this core class ("the §9 bare-DOUTU launch
folded into the real init", `pvrsgx_edm_init`), so it is a supported mode, not a
workaround.

## 2. The four corrections

Each is one SGX545 assumption in the image, each is gated on the per-core
`SINGLE_CONTEXT` flag, and the SGX545 serial image stays **byte-identical**
(`83b5b0a7…`, 14536 B) throughout — which is what makes them safe to land.

| the image assumed | the SGX540 actually has | effect |
|---|---|---|
| client PD in `BIF_DIR_LIST_BASE1` (0xc38), routed via `BIF_BANK0` (0xc78) | **neither register exists**; one list, `DIR_LIST_BASE0` 0xc84 | loads list 0, writes no bank register |
| a HW context's `page_dir` word is a directory address | a **slot** into the driver's read-only PD table at `kernel_code.base + 0xf000` | resolved through a new `pdtable` patch site (`r14`) |
| `features = SECURE`, no pdtable patch | `SINGLE_CONTEXT` set, `SECURE` clear, pdtable patch required | `ukfw_load` accepts the image |
| `EUR_CR_PDS_CACHE_STATUS` (0x138) masks `0x27`/`0x4b` (two groups) | **one** group: `CSC_INV` b0, `DSC_INV0` b1, `DSC_INV1` b2, `DSC_INV3` b4 | mask `0x1F`; **the job goes from `consumed 0` to `1/1 passed`** |

The first and third are readings of `sgx540defs.h` and of the driver
(`pvrsgx_core_single_context`, `pvrsgx_share_fw_heaps`, `pdtable_init`,
`pvrsgx_vm_pd_word`).  The fourth is the decisive one and rests on the
540-specific header in the DDK 1.8 drop (§4).

## 3. `EVENT_PDS_ENABLE`: the driver's constant was the SGX545's

`0x0a58` is the register that actually dispatches the EDM per event source.  Our
1.17 header does not contain it; the DDK 1.8 does, and its own `srvinit` derives
the value per core class.  `SGX_FEATURE_MP` is **not** defined for the SGX540 in
either drop, so our core takes the non-MP branch and the value is **`0x2204f00f`**.
The driver programs `SGX545_CR_EVENT_PDS_ENABLE_ALL = 0x2204b00f` — **one bit
different: `SW_EVENT` (0x4000)** is missing.  Fixed for the `sgx540` entry only.

## 4. What the DDK 1.8 drop settled (and two retractions)

The operator supplied a GPL-2 DDK drop, **"Android SGX DDK UM version 1.8"**
(`PVRVERSION_BUILD 869593`, TI OMAP), which carries what our vendored 1.17 drop
lacks: a **540-specific 3150-line `sgx540defs.h`**, the **user-mode `srvinit`**,
the full per-revision errata table, and the microkernel as **C source**.

* **The rev-120 errata list is ~50 BRNs, not three.**  The fact sheet has now
  been wrong twice about this; the current reading is in
  `docs/reference/sgx540-ddk18-facts.md` §1.  Three (`23055`, `31474`, `33181`)
  are marked "workaround in uKernel" — they change the layer this port writes.
* **`USE_CODE_BASE`'s granule is derived, not guessed**: the DDK computes
  `INSTRUCTION_CACHE_LINE_BITS + 3`, i.e. **8** for the 540 (5+3) and **7** for
  the 545 (4+3).  Both already match our entries, so fact-sheet Gap 7 narrows to
  "do the registers exist on rev-120 silicon" — a write/read-back, not a header
  question.
* **The `ta3dctl` layout derives to `sKernelCCB` at +0x58 and
  `sKernelPDDevPAddr` at +0x5c**, independently corroborating our driver's
  `static_assert`s and the image's `0x16` word read.  The layouts part company
  *after* `0x5c` (the vendor block ends at `0xf8`; the image claims `0x60`-`0xff`
  as its own scratch), which is a real divergence to check field by field.  The
  derived offset table is `docs/reference/sgx540-ddk18-facts.md` §7.
* **`FIX_HW_BRN_31620` is a host MMU burn our tree lacks** — every page directory
  must carry a valid read-only PTE at PDE 0 / PTE 1, which is exactly
  `sgx540_heaps.general`'s base.  Tested and **rejected** (§6).
* **Two retractions of my own earlier notes**: `0xa74 = 0x00208200` is *right*
  for `DMS_CTRL` (32 EDM / 32 vertex / 32 pixel tasks is what the DDK computes
  without `FIX_HW_BRN_23632`, which is not in our list), and `DMS_AGE` is an
  optional build flag we already write.  Both are corrected in
  `sgx540-ddk18-facts.md`.

The drop also carries the vendor's own microkernel specifications, but both PDFs
are marked **"Strictly Confidential"**, so the project's cleanroom rule makes
them unusable as evidence.  That boundary is recorded, not re-litigated.

## 5. The measured failure that remains

Every job run costs a timeout, so the pattern was measured repeatedly:

```
  ok 1/1 passed      ok 1/1 passed      FAILED      FAILED      FAILED
job timed out (ccb 5, consumed 4, ...); resetting
microkernel started again
```

**One or two jobs pass immediately after a GPU reset, then none**, and the
driver still times out with the command *consumed* (`consumed 2..5`).  So this
is progress, not a fix.

The sharpest datapoint came from instrumenting the image (kernel-CCB trace on
genuinely free control-block words — an earlier attempt wrote words the image
already uses, and that reading is withdrawn).  In one shot, during and after a
job that passed:

* the job **completes** (`1/1 passed`);
* the driver still reports **`consumed 0`** and times out on the same boot;
* **not one** of the microkernel's own stores ran — not the timer-test pair, not
  the `kick`-entry pair, not `do_ta`'s scratch, not the completion seqno.

So "one pass per reset" is *not* "one job consumed per reset" (different
accountings), and the EDM is not executing a normal dispatch path at all.  That
inverts the priority for the next session: establish **whether the microkernel's
task runs after the init one** before asking another register question.

## 6. Tested and rejected: `FIX_HW_BRN_31620`

Adding the one prealloc (`pvrsgx_mmu_prealloc(kernel_mmu, general.base,
+SGX_MMU_PAGE_SIZE)` to the single-context path), rebuilding **just the module**
and booting: the microkernel still starts cleanly, and the first run of five
NULL jobs showed **two** consecutive passes — but repeats read **0/5** and
**1/5**, and the driver still timed out at `ccb 5, consumed 4`.  Reverted;
`git checkout` plus a rebuild reproduces `2e138afc…`, byte-identical to the
module on hd3.

## 7. A new bench constraint

`/dev/mem` no longer reaches the GPU window in the open-driver configuration —
`[Errno 13] Permission denied` for the GPU, RAM and CTRL windows alike, while
`CONFIG_STRICT_DEVMEM` is off and `IO_STRICT_DEVMEM` is not in the config at all.
`grep 56000000 /proc/iomem` shows `56003fff : 56000000.gpu powervr_sgx`: the
driver reserves the region with its own mapping.  **The zero-risk userspace
register read that started this port is therefore unavailable once the open
driver owns the GPU**; further register evidence must come from inside the driver.
Session 73's identity read necessarily ran with `pvrsrvkm` bound.

## 8. What this does not prove

* No render, no geometry, no pixels: `ccb wo 0 ro 0 issued 0 consumed 0` at the
  clean reading — a started microkernel that completes a NULL job.
* The 28-bit heap layout, `sgx540_init_regs`, the `EVENT_PDS_*` offsets and every
  microkernel register table (0x618, 0x62c, 0x694, TA 0x200, 3D 0x428) remain
  carried-over hypothesis; none has been exercised, because the EDM does not
  dispatch.
* `0xb14` is `EUR_CR_USE1_SERV_PIXEL` — a USSE **pixel service queue**
  (COUNT/EMPTY), not the EDM task-progress register the image treats it as, so
  the "keep-alive" counter is counting the wrong thing.  Unfixed, and the next
  concrete lead after §5.
* Nothing has been pointed at the display stack; the desktop runs on `swrast`
  because the closed DDK is blocked.
