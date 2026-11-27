# The SGX540's real errata list, and the DDK 1.8 facts that change the port (session 74)

The operator supplied the GPL-2 DDK drop **`GFX_Linux_DDK-master`** — "Android
SGX DDK UM version **1.8**", `PVRVERSION_BUILD 869593`, **TI OMAP**.  It is the
same vendor family as our silicon, GPL-2 throughout (`NOTICE` = GPLv2, and every
file carries the GPL-2 header), so it is the project's **K** evidence class — and
it carries what our vendored 1.17 drop does not: a **540-specific 3150-line
`eurasia/hwdefs/sgx540defs.h`**, the **user-mode `srvinit`** that sets the
microkernel up, and `sgxerrata.h`'s full rev table.

This note records what it changes.  Nothing here is a copy of closed material;
every line is a GPL-2 header or a GPL-2 source file.

## 1. The errata list in the fact sheet was wrong — twice over

`docs/reference/sgx540-factsheet.md` says the rev-120 SGX540 has **exactly three**
BRNs (`26620`, `28011`, `34028`) and calls the longer list "the SGX543's".  The
DDK 1.8 `sgxerrata.h`, in the `#if defined(SGX540)` block's `#if SGX_CORE_REV == 120`
branch, defines **about fifty**:

```
23055 23720 25941 26202 26352 26620 26681 26704 26998 27002 27006 27235 27251
27272 27311 27330 27408 27534 27738 27723 27919 27984 28011 28249 28475 28922
29684 29773 29967 30313 30427 31474 31938 32774 33181 33631 34028 34043 34351
35498
```

(with `25161` deliberately disabled "due to BRN35441").  Several are marked
"workaround in uKernel" (`23055`, `31474`, `33181`) — i.e. they change what the
**microkernel** must do, which is exactly the layer the port is writing.

So the earlier correction was itself wrong: the 1.17 drop's three-BRN reading was
of a *different* configuration, and the 29954–31780 group the first report quoted
was not simply "the SGX543's" either.  Treat the list above as the rev-120 set and
re-derive the fact sheet from this header.

## 2. `EVENT_PDS_ENABLE` (0x0a58) is the real dispatcher, and its value is derivable

The 1.17 header our driver was written against does not contain `0x0a58` at all.
The DDK 1.8 does, and its own `srvinit`
(`eurasia/services4/srvinit/devices/sgx/sgxinit.c`) derives the value per
core class.  `SGX_FEATURE_MP` is **not** defined for the SGX540 (neither DDK
defines it), so our core takes the non-MP branch:

```
0x2204f00f = BREAKPOINT | DPM_OUT_OF_MEMORY_ZLS | SW_EVENT | TIMER
           | TA_FINISHED | TA_TERMINATE | DPM_REACHED_MEM_THRESH
           | DPM_OUT_OF_MEMORY_GBL | DPM_OUT_OF_MEMORY_MT | DPM_3D_MEM_FREE
           | PIXELBE_END_RENDER
```

The driver's `SGX545_CR_EVENT_PDS_ENABLE_ALL` is `0x2204b00f` — **one bit
different: `SW_EVENT` (0x4000) is missing**.  Fixed in our tree as
`SGX540_CR_EVENT_PDS_ENABLE_ALL` with the citation, applied to the `sgx540`
entry only.

## 3. Registers the image uses that are not what it thinks

| offset | the image treats it as | DDK 1.8 `sgx540defs.h` says |
|---|---|---|
| `0x0138` | invalidate-done, SGX545 two-group masks `0x27`/`0x4b` | `EUR_CR_PDS_CACHE_STATUS`, **one** group: `CSC_INV` b0, `DSC_INV0` b1, `DSC_INV1` b2, `DSC_INV3` b4 |
| `0x0b14` | EDM task progress (the keep-alive counter) | `EUR_CR_USE1_SERV_PIXEL`, a **USSE pixel service queue** (COUNT b0-15, EMPTY b16) |
| `0x0a74` | TA/3D flags word | `EUR_CR_DMS_CTRL` — but see below: **the value happens to be right** |

**`0xa74`: retracted, the image is correct.**  I first wrote that the image
"mis-models" `0xa74` as a TA-flags word.  Decoding the value settles it the
other way: our init writes `0xa74 = 0x00208200`, and against `DMS_CTRL`'s
fields (`MAX_NUM_{VERTEX,PIXEL}_PARTITIONS` b22-28, `MAX_NUM_EDM_TASKS` b16-21,
`MAX_NUM_VERTEX_TASKS` b10-15, `MAX_NUM_PIXEL_TASKS` b4-9, `DISABLE_DM` b0-3)
that is **32 EDM / 32 vertex / 32 pixel tasks, no disabled DM** — which is
*exactly* what the DDK 1.8 `srvinit` computes for our core
(`ui32Max{EDM,Vertex,Pixel}Tasks = 32`; `FIX_HW_BRN_23632`, the only thing that
raises the EDM count to 63, **is not** in the SGX540 rev-120 errata block).
So `0x208200` is the right number for the right reason, and it shows the entry's
author read the SGX545 header's field positions off the same layout.  The
microkernel's later read-modify-writes of `0xa74` (the TA/3D flag fields) still
assume the SGX545's *field* layout, which is the part that remains open.

**False alarm on `DMS_AGE`:** the DDK writes `EUR_CR_DMS_AGE` (0xa78) only under
`SGX_DMS_AGE_ENABLE`, an *optional build flag*, not a per-core feature — and our
init already writes `0xa78 = 0x2c`, which is `ENABLE` b16 clear plus a 44-age
value.  Not a lead.

`0x0138` is fixed (mask `0x1F`, and the waits now take the register and mask from
the core config).  The other two are the open candidates for the remaining
stall: the image's keep-alive writes a queue counter, and its TA/3D flag
read-modify-write lands in `DMS_CTRL`, whose `MAX_NUM_*` fields are *limits the
EDM reads*, not flags.

**Measured consequence of the `0x0138` fix (same session):** the NULL job goes
from a universal `consumed 0` timeout to **`1/1 passed`**, and repeated runs
show `consumed 2..4` — the microkernel now takes commands off the CCB.  Exactly
**one** job then completes per GPU reset, consistent with the EDM being entered
once (the image's own event-entry counter reads 1).

## 4. What this drop does *not* answer

* The microkernel's own register tables (0x618, 0x62c, 0x694, TA 0x200,
  3D 0x428) are still not in any header — this drop's kernel side is the same
  host-touched subset, and the microkernel itself is a binary.
* The `ta3dctl` offsets are *derived* in §7 from the header's structs and
  corroborate ours, but the fields above `0x5c` still need the field-by-field
  diff §7 describes.
* Nothing was verified on silicon in this note except the `0x0138` mask and the
  `SW_EVENT` bit; both were measured as a change in behaviour, not yet as a fix.

## 5. Two `USE_CODE_BASE` facts, from the DDK's own derivation

The open driver's `use_code_base_addr_shift` was written as a guess for the 540
(the fact sheet's Gap 7).  The DDK 1.8 derives it instead of guessing:

```c
/* eurasia/hwdefs/sgxdefs.h */
#define EURASIA_USECODEBASE_DATAMASTER_EDM  (3)
#define EUR_CR_USE_CODE_BASE_ADDR_ALIGNSHIFT  (SGX_FEATURE_USE_INSTRUCTION_CACHE_LINE_BITS + 3)
...
/* eurasia/services4/srvinit/.../sgxinit.c */
ui32USSECodeBase = (heap_base >> EUR_CR_USE_CODE_BASE_ADDR_ALIGNSHIFT)
                 | (EURASIA_USECODEBASE_DATAMASTER_EDM << EUR_CR_USE_CODE_BASE_DM_SHIFT);
ScriptWriteSGXReg(psScript, EUR_CR_USE_CODE_BASE(SGX_KERNEL_USE_CODE_BASE_INDEX), ...);
```

and `sgxfeaturedefs.h` gives, per core,
`SGX_FEATURE_USE_INSTRUCTION_CACHE_LINE_BITS` = **5** for the SGX540 and **4**
for the SGX545.  So

| | SGX540 | SGX545 |
|---|---|---|
| instruction-cache line bits | 5 | 4 |
| `USE_CODE_BASE` **address shift** | **8** (256-byte units) | **7** (128-byte units) |
| data master for the microkernel | **3** (`DATAMASTER_EDM`) | 3 |
| code-base index for the microkernel | 15 | 15 |

Both match our tree exactly — `.use_code_base_addr_shift = 8` and
`.use_code_base_dm_shift = 24` for the 540, `7` and `25` for the 545 (the DM
field is one bit higher on the 545's wider layout).  So **Gap 7's granule is
settled as a K fact** and the entry's guess was right; what remains open about
`USE_CODE_BASE` is only whether the registers *exist* on rev-120 silicon, which
is a write/read-back, not a header question.

## 6. `FIX_HW_BRN_31620`: a dummy page our MMU driver does not create

`sgxerrata.h`'s SGX540 rev-120 block defines `FIX_HW_BRN_31620` — "workaround in
services", i.e. **host-side**, and our `pvrsgx_mmu.c` has no trace of it
(`grep 31620` finds nothing in `drm/powervr_sgx/` or `ukernel/`).  The DDK's
`eurasia_km/services4/srvkm/devices/sgx/mmu.c` implements it like this:

```c
#define BRN31620_PT_ADDRESS_RANGE_SHIFT   22       /* 4 MiB */
#define BRN31620_PDE_CACHE_FILL_SHIFT     26       /* 64 MiB */
#define BRN31620_DUMMY_PAGE_OFFSET        (1 * SGX_MMU_PAGE_SIZE)
#define BRN31620_DUMMY_PDE_INDEX          (BRN31620_DUMMY_PAGE_OFFSET / BRN31620_PT_ADDRESS_RANGE_SIZE)
#define BRN31620_DUMMY_PTE_INDEX          ((BRN31620_DUMMY_PAGE_OFFSET - (BRN31620_DUMMY_PDE_INDEX * BRN31620_PT_ADDRESS_RANGE_SIZE))/SGX_MMU_PAGE_SIZE)
#define BRN31620_DUMMY_PAGE_SIGNATURE     0xFEEBEE01

static IMG_VOID BRN31620InvalidatePageTableEntry(MMU_CONTEXT *psMMUContext,
                                                IMG_UINT32 ui32PDIndex, IMG_UINT32 ui32PTIndex,
                                                IMG_UINT32 *pui32PTE)
{
    if (((ui32PDIndex % (BRN31620_PDE_CACHE_FILL_SIZE/BRN31620_PT_ADDRESS_RANGE_SIZE))
            == BRN31620_DUMMY_PDE_INDEX) && (ui32PTIndex == BRN31620_DUMMY_PTE_INDEX))
        *pui32PTE = (dummy_page_phys >> SGX_MMU_PTE_ADDR_ALIGNSHIFT)
                  | SGX_MMU_PTE_DUMMY_PAGE | SGX_MMU_PTE_READONLY | SGX_MMU_PTE_VALID;
    else
        *pui32PTE = 0;
}
```

which evaluates, for a 4 KiB page and the 28-bit address space, to:

* a **dummy page must be mapped at PDE index 0, PTE index 1** — byte offset
  **4096**, i.e. one 4 KiB page into every page directory;
* the PTE carries `DUMMY_PAGE | READONLY | VALID` and points at a page the host
  allocates and signs `0xFEEBEE01`;
* it must be present in **every** directory, because it is re-established each
  time a PTE is invalidated (`ui32PDIndex % 16 == 0 && ui32PTIndex == 1`);
* the burn is about a 64 MiB PDE cache-fill line and its flush (`CACHE_FLUSH`
  fields), i.e. the BIF's directory cache, not the client's data.

Why it matters for this port specifically: the EDM/BIF walk whatever directory
list 0 holds, and on a single-context core that is the client's own PD — the
one our `pvrsgx_mmu` builds and the one the microkernel now resolves through
`pdtable[slot]`.  A directory without the dummy page is a hardware condition the
DDK treats as fatal enough to encode a workaround for.  **Untested here**: the
observed failure is a *stall*, not a BIF fault (`BIF_INT_STAT 0x8000`,
`BIF_FAULT 0`), so this is a candidate, not a diagnosis, and the dummy page
must not be added without a measurement that distinguishes it.

Two supporting facts from the same header pair:

* `SGX_MMU_PTE_DUMMY_PAGE` and `SGX_MMU_PDE_DUMMY_PAGE` are both **`(0)`**
  (`sgxmmu.h`), so the burn is not a magic bit: the requirement is that
  `PDE[0].PTE[1]` be **valid and read-only**, never zero.
* our `sgx540_heaps.general` base is **`0x00001000`** — exactly the burn's
  offset (4096 = PDE 0, PTE 1).  So the page the workaround names is *the
  general heap's own first page*, which our MMU driver maps only when an
  allocation happens to land there; the DDK makes it unconditionally
  present in every directory, whatever the client allocated.

## 7. The `ta3dctl` layout, derived from the DDK's structs (corroborates ours)

`sgx_mkif.h` gives `PVRSRV_SGX_EDMPROG_SECATTR` and `SGXMK_STATE` field by
field with `/*!< … */` comments, and `static_assert`s several offsets — it is
the authority for the control-block offsets the open image carries as guesses.
Combined with `SGXMK_TA3D_CTL`, and for our configuration
(`FIX_HW_BRN_28889` set; **no** `SGX_FEATURE_2D_HARDWARE`, no `SGX_FEATURE_MP`,
no `SGX_FEATURE_MK_SUPERVISOR`, `FIX_HW_BRN_31939` not in the rev-120 list,
`SGX_DISABLE_UKERNEL_SECONDARY_STATE` clear; and
`SGX_MAX_CONTEXT_PRIORITY = 3`, because `SUPPORT_SGX_LOW_LATENCY_SCHEDULING`
defaults to **1** in the DDK's own `core.mk` and `sgxfeaturedefs.h` then defines
`SUPPORT_SGX_PRIORITY_SCHEDULING`):

| offset | word | field |
|---|---|---|
| `0x00` | 0 | `SECATTR.sTA3DCtl` — this block's own device address |
| `0x04` | 1 | `SECATTR.sHostCtl` — the host control block |
| `0x08` | 2 | `SECATTR.sCCBCtl` — the kernel CCB's write/read offsets |
| `0x0c` | 3 | `MK.sContext0RTData` |
| `0x10` | 4 | `MK.sContext1RTData` |
| **`0x58`** | **0x16** | **`MK.sKernelCCB`** — the kernel CCB base |
| **`0x5c`** | **0x17** | **`MK.sKernelPDDevPAddr`** — the kernel page directory |
| `0x1c` | 7 | `TA.sTACCBCmd`, then `sTARTData`, `sTARenderDetails`, `sTARenderContext` |
| `0x2c` | 11 | `3D.s3DRTData`, `s3DRenderDetails` |
| `0x34` | 13 | `MK.ui32PendingLoopbacks` |
| `0x38` | 14 | `SGXMK_STATE`, `0xc0` bytes, so the block ends at `0xf8` |

**This corroborates our driver, independently.**  `pvrsgx_fwif.h` asserts
`offsetof(struct pvrsgx_fw_ta3d_ctl, ccb) == 0x58` and puts `kernel_pd` at
`0x5c`, and the open image reads the kernel CCB from control-block word
**`0x16`** — all three agree with the derivation above.  The first three words
(`self`/`host_ctl`/`ccb_ctl`) are exactly `SECATTR`, as the image assumes.

Where the two layouts part company is *after* `0x5c`: the vendor's block is
`0x38` bytes of pointers + a `0xc0`-byte `SGXMK_STATE` (run lists, priorities,
active-power counters) and ends at `0xf8`, while the open image keeps its own
scratch in `0x5c`–`0xff`.  So an offset the image writes above `0x5c` names a
*different* field on the vendor stack — worth checking field by field before
trusting any of the carried-over tables, and a reason the image's private-region
choice (`0x60`–`0xff` claimed as its own) is a real, not cosmetic, divergence.

## 8. The vendor's SGX540 microkernel entry table (from source, not a blob)

`eurasia/eurasiacon/binary2_540_omap4430_android_release/target/intermediates/ukernel/`
holds the microkernel as **C source** — `ukernel.h` is
`static const IMG_BYTE pbuKernelProgram[] = { ... }` (~20 KB of bytes) and
`ukernel_labels.h` is the auto-generated entry table.  It is shipped *source*,
like the headers, so it is the same **K** evidence class (there is no closed
blob in the drop; `vendor/lib` is absent).

The label table is the vendor's own entry naming, and it differs in *shape* from
the open image's:

| vendor `ukernel_labels.h` | offset |
|---|---|
| `UKERNEL_EVENTHANDLER_OFFSET` | `0x000030` |
| `UKERNEL_HOSTKICKFLUSHPDCACHE_OFFSET` | `0x0006E8` |
| `UKERNEL_HOSTKICKPOWER_OFFSET` | `0x0006F0` |
| `UKERNEL_HOSTKICKCONTEXTSUSPEND_OFFSET` | `0x000007A8` |
| `UKERNEL_HOSTKICKTRANSFER_OFFSET` | `0x000008A8` |
| `UKERNEL_HOSTKICKTA_OFFSET` | `0x00000978` |
| `UKERNEL_HOSTKICKCLEANUP_OFFSET` | `0x00000A60` |
| `UKERNEL_HOSTKICKPROCESS_QUEUES_OFFSET` | `0x00000B40` |
| `UKERNEL_HOSTKICKGETMISCINFO_OFFSET` | `0x00000C80` |
| `UKERNEL_CHECKTAQUEUE_OFFSET` | `0x00003060` |
| `UKERNEL_CHECK3DQUEUES_OFFSET` | `0x000030C0` |
| `UKERNEL_TIMER_OFFSET` | `0x00002680` |
| `UKERNEL_SETUPDIRLISTBASE_OFFSET` | `0x00004198` |
| `UKERNEL_INVALIDATEBIFCACHE_OFFSET` | `0x00003030` |

**Structural difference worth noting**: the vendor microkernel has a **separate
entry point per kernel-CCB command type** (`HOSTKICKTA`, `…TRANSFER`,
`…POWER`, `…CONTEXTSUSPEND`, `…CLEANUP`, `…GETMISCINFO`, `…PROCESS_QUEUES`,
`…FLUSHPDCACHE`, `…SETHWPERFSTATUS`), patched into the kernel's dispatch; the
open image exposes **one** `kick` entry that reads the command's type and
branches internally.  Both are valid, but it means the two tables cannot be
compared address for address, and the vendor's `SETUPDIRLISTBASE` /
`INVALIDATEBIFCACHE` / `CHECKTAQUEUE` / `CHECK3DQUEUES` entries show the
directory-list and queue handling is *itself* callable microkernel code on this
core — consistent with the one-list model in §3.

## 9. The microkernel's own specification is in the drop (usable — operator, session 80)

`docs/SGX Services Microkernel.Overview.1.1.5.External.pdf` and
`docs/SGX Services Microkernel.Scheduling.1.0.23.External.pdf` are the vendor's
behavioural specs for exactly the layer this port is reconstructing.  They are
marked "Strictly Confidential"; **the operator has since cleared the whole drop
as evidence, PDFs included** (DECISIONS §118, session 80), and the earlier
"therefore not usable" reading is withdrawn.  Extract them with
`pdftotext -layout <pdf> <txt>` — it works on all 30 PDFs in that directory —
and search them for a behavioural question *before* inferring it from a binary.

The one this paid for immediately: `SGX Services Microkernel.Overview.1.1.5`
§6.2.3 states that a TA command carrying `SGXMKIF_TAFLAGS_LASTKICK` is what
makes "the scene ready for rendering", by moving its `SGXMKIF_HWRENDERDETAILS`
onto the 3D run list — and our µkernel never tests that flag (KNOWN-ISSUES
trap 420).  Also in that drop: `SGX Android OMAP4 Blaze.Platform Guide` (our
platform), the services architecture and functional specs, the EGL/GLES
reference-driver specs and the test specifications.

The downloadable public half of the drop still makes exact:

* **The PDS is the scheduler.**  `EUR_CR_EVENT_PDS_ENABLE` (0x0a58) is the
  per-source dispatch mask, and `srvinit` writes it once at init from the value
  derived in §2.  The kernel driver never writes it (only the deinit path zeroes
  it), so it cannot be "re-armed" by the host mid-run.
* **`0x0a50 EVENT_PDS_ENABLE2`** exists as well (`TRIG_TA`, `TRIG_3D`,
  `TRIG_DL`, the two DPM free-load bits) and is **never written by either the
  kernel driver or `srvinit`** in this drop — so it is not the missing piece.
* **The event program is started from a PDS exec address pair**
  (`EUR_CR_EVENT_PIXEL_PDS_EXEC` 0x0a5c / `_OTHER_PDS_EXEC` 0x0a68, each an
  address in 16-byte units, plus `_DATA` 0x0a60/0x0a6c and `_INFO`
  0x0a64/0x0a70).  The open driver programs these from constants; `srvinit`
  computes them from the microkernel's own primary/secondary program sizes.
  That computation is the next thing worth diffing against our values.

## Provenance

`/home/psyden/Pdrive/GFX_Linux_DDK-master/` (operator-provided, outside the
repository).  COPYING/GPL-2; `PVRVERSION_MAJ 1`, `MIN 8`, `BUILD 869593`.  The
repository keeps **no copy** of it — cite the path and the header/line, as with
the QNX material.
