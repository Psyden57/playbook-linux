# Session 84 (absolute) — the write page fault is bounded and structurally sound and is NOT a client field; the ctlblk trace instrument was built but could not be validated

> Numbering: absolute `84` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era 78**
> (`absolute = Linux-era + 6`).  Continues session 83 (the DMS_CTRL fix moved
> the render's failure from a timeout to a write page fault at `0x00401000`).

Date / span: 2026-11-27 (local; UTC crossed to 2026-11-28 during the session).
Unit state at the end: **Linux 7.3.3 "Nura edge"** on hd3, not TabletOS.
Region 0 `0x02C1DDC2` (unchanged; no flash this session); region 1
`20a5edd3…`; hd3 `/dev/loop0` ext4 rw (985.0 M used, 53.2 G free);
`BOOTCTR_FCTRY 0x21` at the last boot (`0x21 >= threshold 0x1e, no write`);
firmware **`kick18ccb`** (`d4087521…`, reverted and proven); module
**`96368b07…`** (unchanged; one SB build reverted); `wlan0` up
(`10.194.186.131/13`), `usb0` `169.254.0.1/16` up, dropbear running;
battery `Full` 100 %, 4.143 V; panel `bl_power` 0; desktop llvmpipe.

## Goal of this session

HANDOFF-84 §0b, the one experiment: **bound the page fault's writer.**  Map
growing general-heap ranges at `0x400000` with `/tmp/frame-mapto`
(`SGXGEN_MAP_AT` / `SGXGEN_MAP_SIZE`) on a fresh boot, and find the size at
which the fault stops and the job reverts to `job timed out`.  That size was
supposed to be "the buffer the writer touches", matched against `pages`,
`tail_pointers_size`, `mt_bytes`, `MTRegionArraySize`, region sizes and strides.

## Confirmed (measured, read in a file, or seen on the bench)

**The handed-over instrument could not answer the question, and that was
established for free (no boot).**  `SGXGEN_MAP_AT` / `SGXGEN_MAP_SIZE` did not
map an extra range: it **relocated the tool's own next general-heap allocation**
to the chosen address (`tools/gen/sgxgen.c`, `bo_alloc`).  Measured with `-n 0`
(setup only, no render submitted, so no GPU reset):

| | general-heap layout |
|---|---|
| baseline | `gen 0x2000 +0x40000`, `vdm 0x42000 +0x5000`, `colour 0x47000 +0x40000` |
| old knob, `MAP_AT=0x400000` | `gen 0x400000 +0x40000`, `vdm 0x440000`, `colour 0x445000` |

Two consequences, both real defects in the instrument:
1. It changed **two** things at once — it maps the target **and unmaps** the
   region the baseline occupies, so a "fault disappeared" reading could not be
   attributed to the mapping.
2. `MAP_SIZE` only ever **grows** the allocation (`if (big > cb.size)`), and the
   first general allocation is the 256 KiB `gen` ring — so the prescribed sizes
   `0x1000`, `0x10000`, `0x40000` all produced the **identical**
   configuration.  Three of the five prescribed sweep points were duplicates.

**The instrument was fixed and is now a coverage map, not a relocation**
(`/tmp/frame-mapto2`, md5 `f6b5c81a…`).  `map_extra_range()` allocates and binds
a separate range at `SGXGEN_MAP_AT` of `SGXGEN_MAP_SIZE` bytes and sets
`d->map_extra_lo/hi` so the bump allocator steps over it.  Verified with `-n 0`:
the general-heap layout is **byte-for-byte the baseline's** in every coverage
case, and the tool announces the mapping
(`sgxgen: cover 0x00400000 +0x6000 (coverage only)`).  The frame-tool sources
are otherwise the session-83 build; the production path is untouched.

**The writer is bounded, and the boundary is sharp and reproducible.**  Three
clean boots (`resets 0`, `open microkernel kick18ccb`):

| coverage at `0x400000` | result |
|---|---|
| none | page fault `0x00401000`, requestor `0x2000`, status `0xe000`, **write** |
| 4 KiB | page fault `0x00402000` |
| 8 KiB | page fault `0x00404000` |
| 16 KiB | page fault `0x00405000` |
| 20 KiB | page fault `0x00405000` |
| 24 KiB | **no fault** — `job timed out (… events 0x20000000/0x00000010)` |
| 64 KiB, 1 MiB, 4 MiB | **no fault** — the session-79 timeout |

So the writer's footprint is **`[0x401000, 0x406000)` — 5 pages, 20 KiB,
starting at general-heap base (`0x1000`) + 4 MiB**.  It is a *sequential
forward* write: the fault lands at the **top of whatever range is mapped**, and
mapping `0x401000` alone still faults at `0x402000`.  Refined on a second and
third boot with the same answers (4/8/16 KiB fault at the range top; 24 KiB
clears).

**The footprint does NOT track the client — this is the new result, and it
rules out §0b's candidate list.**  Held the coverage at 4 KiB (so a fault is
guaranteed) and varied one client dimension per render:

| varied | result |
|---|---|
| surface `64x64` | fault `0x00401000` |
| surface `256x256` | fault `0x00401000` |
| surface `512x512` | fault `0x00401000` |
| `-k 1` (one kick) | fault `0x00401000` |
| `-k 2` (default) | fault `0x00401000` |
| `SGXGEN_NO_PRIM` | fault `0x00401000` |
| `SGXGEN_NO_DRAW` | fault `0x00401000` |
| `SGXGEN_NO_STATE` | fault **`0x0dc00000`** (= `PDS_EXEC_BASE` exactly) |

The address and the footprint are **invariant to surface size, kick count and
the draw**.  The client-side sizes §0b pointed at are therefore not the base:
for 256×256 the candidates are `region_bytes 0x600`, `tailptr_bytes 0x400`,
`mt_tiles*12 0x180`, colour `0x40000` — and for 512×512 `region 0x1800`,
`tailptr 0x1000`, colour `0x100000` — none of which moves the fault.

**The 4 MiB is also not the VDM ring.**  The tool's VDM ring is `0x5000` — the
same 20 KiB as the footprint, an obvious hypothesis.  Enlarging it to `0x8000`
(`/tmp/frame-vdm8k`, md5 `56a55161…`) **did not move the boundary** (still
faults at 20 KiB, clears at 24 KiB), so the match is a coincidence.  Reverted
and proven (`/tmp/pb-frame-final` recompiles to the measured tool's md5).

**`SGXGEN_NO_STATE` moves the write to `0x0dc00000`.**  That is
`PDS_EXEC_BASE` (`sgx540_heaps.pds_pixel.base`) to the byte.  Read together with
the invariance above: the writer's *base* is chosen from two different field
classes depending on the stream, so it is a base register plus an offset, and
what is wrong is the **offset** (and, in the state-present case, that it lands in
the general heap at all).

**Session 83's second fault (`0x0001f000`, requestor `0x0002`) does NOT
reproduce with the corrected knob.**  With the old knob the baseline `gen` ring
at `[0x2000, 0x42000)` was unmapped, so `0x1f000` was simply the next unmapped
page the writer met; with the coverage map the layout is the baseline's and the
fault stays at `0x40xxx` with requestor `0x2000`.  So session 83's
"two different requestors" reading was an artefact of the relocating instrument,
not a second writer.  Corrected here.

**Nothing was found in the microkernel image by the easiest route, and the
easy route was wrong.**  The live image (`/tmp/k18.bin`, 14 640 B) contains the
word `0x00400000` three times (`0x2f8`, `0x508`, `0x540`), and reading them as
`limm` immediates suggested three sites that compute the fault's base.  They are
**not** immediates: per `local/work/powervr_sgx/docs/ISA.md:1604` a `limm` packs its 32-bit immediate
*inside one instruction word* (bits 0-20, h4-8, h12-17), so a `[00400000]
[fc200000]` pair is data followed by an unrelated instruction.  Decoding every
instruction of the image for `imm == 0x400000` finds **none**.  So the 4 MiB is
computed at run time from the client's structures or a heap base, not carried as
a literal — consistent with the invariance result.

## Inferred (reasoning from evidence, not directly measured)

* **The 4 MiB base is not a client surface/kick field.**  The fault address is
  identical for 64×64, 256×256 and 512×512, one and two kicks, and with the draw
  removed — while every client size that scales with those changes.  Strength:
  strong (six one-variable renders, three boots).
* **It is a base + fixed offset in the general heap.**  The footprint is exactly
  5 pages at a fixed 4 MiB offset from the general heap's base, and the write
  walks forward through it.  4 MiB is `BIT(22)`, which is `PVRSGX_TA2_RENDER` —
  a *coincidence*: that bit is only set on the `last` kick and the fault occurs
  on kick 1's TA (`-k 1` faults at the same address).  Strength: medium for
  "base + offset"; the coincidence is explicitly excluded, not the basis.
* **The writer is in the per-kick path, not the stream.**  Removing every stream
  record except the vertex-secondary + terminate still faults (§83 fifth part,
  re-confirmed here as invariant to `NO_PRIM`/`NO_DRAW`), and removing the state
  records only changes *which* heap base the bogus write uses.  Strength:
  medium-strong.
* **The 20 KiB length is a count-of-pages written from a register, not a buffer
  size.**  5 pages × 4 KiB; the candidates that are not client-derived
  (`PB_EXTRA_PAGES 0x22`, the free-list 0x40000, the PB's 0x1022 pages) are all
  far larger.  Strength: weak — this is the open question.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"Run the §0b sweep as handed over."**  It could not answer the question:
  three of its five sizes were the same configuration and every point also
  unmapped the baseline region.  Found for free with `-n 0`, before any boot.
* **"The VDM ring is the 20 KiB."**  It matches numerically and does not move
  the boundary when doubled.  Killed by one tool rebuild.
* **"A `[00400000][fc200000]` word pair in the ukernel is a `limm` immediate."**
  Wrong encoding: `limm` carries its immediate inside one word.  My first
  reading of it as three sites computing the fault base was wrong and is
  withdrawn before it reached a claim.
* **"The second fault address (`0x1f000`) with requestor `0x0002` is a second
  writer."**  It was the old knob's relocation artefact.  Corrected.
* **"`0x00401000` lies in no heap the VM maps."**  Session 83 already corrected
  this (it is in the general heap, unallocated); this session confirms the tool's
  own allocations there are `[0x2000, 0x87000)`.
* **"A degraded GPU is a witness."**  After ~15 resets the sweep started
  returning empty messages (trap 423/424 class); every measurement above is from
  a boot with `resets 0`, and the sweeps were re-run after a reboot when the
  counter climbed.
* **"The ctlblk trace instrument works" — it does not, and its positive control
  proves it.**  `UK_ADDRLOG`/`UK_STAGELOG` were built, statically verified, and
  production stayed byte-identical (`d4087521…`), but six deploys all read 0 at
  words 0x3d/0x3e, **including a stamp of 100 at the `kick` entry** — an entry that
  demonstrably executes.  Word 0x37 = `0x22222222` proves ctlblk stores land, so
  the defect is mine.  Three hypotheses tried, each changing form *and* word
  together: (a) narrow offset field → rebase via `imae` → still 0; (b) `st.a.d
  base,#i` 1-based per `ISA.md:1484` → `#(W−1)` → still 0, **and that reading
  contradicts the working TICKLOG site** (`st.a.d r17, #0x37` lands `0x22222222` at
  word 0x37); (c) register clobber → moved to r4 → still 0.  Per trap 430 the
  instrument is unvalidated.

## Unknown / open questions

* **Does `st.a.d r17, #N` write word N or word N+1 on this core?**  Blocks every
  ctlblk-store instrument (trap 442).  Settle by copying the proven TICKLOG store
  (`limm r4, #0x22222222` + `st.a.d r17, #0x37` + `idf`) *verbatim* to a different
  word and reading both — one thing changed at a time.
* **Which field supplies the 4 MiB base, and which supplies the 5-page length?**
  Not a client surface/kick field (measured).  Next discriminators, cheapest
  first: (a) read `BIF_FAULT`'s `SB` field (b4-8) and the full requestor mask
  during the fault — the driver already prints the mask, but not `SB`; (b) with
  the coverage mapped the job hangs for 2 s, so sample the whole `status` dump
  mid-hang for any register in `[0x401000, 0x406000)` (done once: **no register
  equalled it**, so the base is computed in the VDM/MTE/ISP block, not a
  programmed register); (c) the per-block `MMU_BYPASS_*` bits in `BIF_CTRL`
  b8-15 name the blocks that bypass the MMU, and one of them being set would
  make an unmapped address a fault-free write — worth reading, since the fault
  *is* a mapped-address fault and not a bypass.
* **Whether the out-of-allocation write is in scope before the render wall.**
  Operator's call (it is a real memory-safety defect, but it is not the wall).
* Unchanged: the MTE state emit (session 82's wall), `FIX_HW_BRN_33181`, the
  frame tool's two-kick layout, the owed patch-series export (§110).

## Superseded / uncertain

* **The `SGXGEN_MAP_AT`/`MAP_SIZE` semantics as session 83 shipped them** —
  superseded: they relocate, not cover.  The knob now covers, and the tool
  announces the range.
* **Session 83's "two different blocks write into the general heap (requestor
  `0x2000` and `0x0002`)"** — withdrawn: the `0x0002` fault was the old
  instrument's unmapping of the baseline ring, not a second writer.
* **This session's own first reading of the ukernel's `0x00400000` words as
  `limm` immediates** — withdrawn on the ISA check, before it was used.

## Artefacts produced

* **Generator** (`local/work/powervr_sgx`, uncommitted, not in a patch — §110):
  `tools/gen/{sgxgen.c,sgxgen.h}` — `map_extra_range()`, the
  `d->map_extra_lo/hi` step-over in `bo_alloc`, the call from `sgx_dev_open`,
  and the unconditional `cover` announcement.  Replaces the relocating knob.
* **Device tools**: `/tmp/frame-mapto2` `f6b5c81a…` (the corrected coverage
  tool; this session's instrument), `/tmp/frame-vdm8k` `56a55161…` (the VDM-ring
  negative).  Pre-session `/tmp/powervr_sgx_frame` `1177fafd…` and
  `.loc` `68bc15af…` untouched; `/tmp/frame-mapto` `775889e7…` (session 83's
  relocating tool) left in place and marked superseded.
* **µkernel trace instrument** (`ukernel/ukernel.py`): `UK_ADDRLOG` /
  `UK_ADDRLOG_REG` and `UK_STAGELOG` with the `stage()` helper and its `do_ta`
  stamps, all default off; production byte-identical.
* **Driver SB build** (added `BIF_FAULT`'s `SB` field to the fault message,
  `pvrsgx_job.c`): `154bc3987d4bae51bc4e9ca2807fe843` — **reverted** to `.pre-sb`
  `96368b07e75f591c1147c765d874fb87`, file restored and md5 verified.
* **Unvalidated firmware** kept as `sgx540_ukernel.bin.stagetrace-unvalidated`:
  `addr21c` `c998729a…`, `addr618` `1318bc9e…`, `stage1` `6f75ffa4…`, `stage2`
  `3fce473b…`, `stage3` `68ea209e…`, `stage5` `1d12b34f…`.
* **Docs**: traps 433–442, DECISIONS §125–§127, the capture's SESSION 84 sections,
  STATE/HANDOFF/ROADMAP updates, this note.
* **No media write, no flash, no RPMB write.**  Firmware and module both reverted
  to the session-83 pair and proven.

## Continuation (same session) — the fault masks the wall, the write deposits nothing, and one of my own readings is retracted

After the bounded-writer result the session went after *which* block writes, using
what the boundedness now allows: the range can be read back, and the fault can be
cleared on demand.  Four fresh boots, one module build (reverted), one tool build
(two instrument defects found and fixed).

**Confirmed.**

* **The page fault ends the job in ~0.07 s; it has been MASKING the 2 s wall, not
  replacing it.**  Timed with `/proc/uptime` (the device's BusyBox `date` has no
  `%N`; my first attempt with it reported "0 ms" for every run): coverage 4 KiB →
  **0.07 s** `page fault`; coverage 24 KiB → **2.05 s** `job timed out`.  So any
  mid-hang measurement must clear the fault first or it samples a window 28×
  shorter than it believes.  It also explains sessions 79–82: the fault was not
  absent, it was unreachable until the DMS_CTRL fix (trap 431) got the job far
  enough to make a memory access.
* **With the fault cleared, the writer deposits no data.**  The coverage range is
  read back (`sgx_cover_map()`, pre-filled `0xA5A5A5A5`): `16384 dwords — 16384
  untouched, 0 zeroed, 0 foreign`.  Not evidence per trap 430 (no validated GPU
  store into a client BO exists on this stack), but *directional*: the access
  writes nothing even when it succeeds, which is the shape of a **page-table
  walk**, not a mis-addressed data store.
* **`BIF_FAULT`'s SB field names nothing**: `sb 0x00`.  The sub-block selector
  below the requesting master does not narrow requestor bit 13.  Module
  `154bc398…`, reverted to `.pre-sb` `96368b07…` and the file verified.
* **`FIX_HW_BRN_31620` checked and NOT implicated.**  It is in the 540 rev-120
  block, our driver implements none of it, and its constants match the fault's
  arithmetic exactly (4 MiB page-table range `SHIFT 22`; a dummy page at offset
  `0x1000` in every directory, PDE 0/PTE 1; `0x401000` is PDE 1/PTE 1).  But
  mapping `0x1000` does not move the fault, and covering the faulting page gives
  a walk that deposits nothing — so it stays unimplemented and **unfalsified**, a
  candidate for the MMU writer rather than the cause.

**Dead ends and wrong assumptions (mine, this session).**

* **"Mapping `0x1000` makes the render done."**  One run printed `render done`
  with `elapsed 0.04 s` and `firmware running: 0` right after.  That is the fence
  returning with the firmware momentarily down (trap 388/389), not a render; on a
  clean boot the same mapping gives the identical fault.  **Retracted** before it
  reached a claim.
* **"The readback shows all-zeros, so the writer may write zeros."**  It was the
  instrument: `__clear_cache` on ARM *cleans*, so the read-time clean wrote the
  dirty pre-fill back over the GPU's data.  Fixed by flushing the pattern to DRAM
  before the GPU runs; and a zeroed range can never distinguish "wrote zeros" from
  "never wrote", hence the fill pattern and the untouched/zeroed/foreign split.
* **"`date +%s%N` gives milliseconds."**  It does not on this BusyBox; every run
  read "0 ms".  `/proc/uptime` is the clock to use.

**Unknown / open questions.**

* **Which block walks, and what is the 5-page/20 KiB extent?**  No driver or tool
  constant is 20 KiB; the requestor bit 13 and SB are both uninformative.  The
  next instrument that could name it is a µkernel-side trace of the BIF walk, or
  a per-block `MMU_BYPASS_*` read (trap 433 §0b (b)).
* **Still no validated GPU store into a client BO on this stack** — the readback's
  0 is directional only (trap 430 stands).

**Artefacts.**  Tool `/tmp/frame-cover` `756ffd30…` (coverage + readback + pre-fill
flush); module `154bc398…` built and reverted (`.pre-sb` `96368b07…` restored);
trap 434; capture "SESSION 84 CONTINUATION".  No flash, no media write.

## Third part — the walk is structurally sound (the coredump already had the answer)

The instrument this needed already existed and had never been read:
`pvrsgx_dump.c`'s `dump_bif()` calls `dump_lookup("job's", bad->ctx->vm->mmu, addr)`,
which translates the faulting address **in the client directory** and prints PDE and
PTE; it rides the **devcoredump** (`CONFIG_DEV_COREDUMP=y` is in the image).  Two
one-variable boots:

| configuration | `job's` directory |
|---|---|
| baseline (nothing at `0x401000`; PDE index 1 never created) | `pde 0x00000000 pte 0x00000000` |
| `0x400000` covered (table at PDE 1, one page at PTE 0) | **`pde 0x8ae68001`** `pte 0x00000000` |

* **Baseline: no page table exists at all** for that 4 MiB block — in the client
  *and* the firmware directory.  **This corrects session 83:** "inside the general
  heap, merely unallocated" was too generous.  The heap's *range* is declared in
  the layout but **no table exists inside it**, so the GPU faults on the **walk**.
* **With a table present the PDE is valid (`address | 4K | VALID`) and the PTE is
  still empty.**  The walk is **structurally sound**: a correct table lookup that
  finds an unmapped page — not a corrupt directory, not a wild pointer.
* **The arithmetic is exact:** `0x401000` = `heap_base (0x1000)` + **4 MiB + 1
  page**, and 4 MiB is precisely **one full page table** (1024 × 4 KiB).  Two
  mechanisms fit: a **PT range shift (22) used as an address shift**, or **a ~4 MiB
  size used as an address**.  Both are the §124/trap-431 class one level down, in
  the addressing rather than a register field.  Not the colour buffer (that is
  `0x47000`, PDE 0).
* `type 1` is the **read/write bit**, not a fault class — both cores set
  `bif_int_stat_fault_type = BIT(14)` and the 540 header defines no separate
  fault-type field.

**Instrument cost (a boot):** this kernel has **no `dismiss` node**, the dump node
is **consumed by the read**, and `/sys/class/devcoredump/disabled` is a **one-way
global kill switch** — do not write it; reboot restores it.  One fault per fresh
boot, read in the same pass.

**Artefacts.**  Trap 435; DECISIONS §127; capture "SESSION 84 THIRD PART".  No
code change, no module build, no flash, no media write.

## Fourth part — the 4 MiB is not in the TA command either

`0x00400000` does appear in the live TA command, so it was mapped word by word
against `TA_TE`: it is `cmd[0x24c]` → register `0x218` `TE_PSG`, and `BIT(22)`
there is `EUR_CR_TE_PSG_COMPLETEONTERMINATE` — a documented **flag**, not an
address.  Every actual address the command programs is correct and aligned
(`TE_PSGREGION_BASE 0x0f025000`, `TE_TPC_BASE 0x0f025050` — 64-byte aligned;
`BIF_TA_REQ_BASE 0x08800000` — 1 MiB aligned).  Greps find **no** `0x400000`, no
`<< 22`/`>> 22` and no 4 MiB constant in `ukernel/` or the generator's address
paths.  So the 4 MiB is computed at run time from a base (trap 436).

**Side observation of the same class:** the vendor's TA does not *write* `TE_PSG`
from the command — it reads the register and ORs into it (`ta.asm:4346-4352`).
Our port writes a constant.  Benign here, but the pattern (a hardware
read-modify-write replaced by a constant write) is worth checking on other
registers.

**Artefacts.**  Trap 436; capture "SESSION 84 FOURTH PART".  Read-only work.

## Fifth–seventh parts — a caution about my own arithmetic, and the audit completed

**The timeout-path coredump carries nothing new.**  Took one with the fault
cleared (so the job reaches the real stall) and grepped all 30 389 bytes: the only
`00400000` is `cmd[0x24c]` → `TE_PSG` `BIT(22)`, the flag trap 436 already
explained.  The client structures are all well-formed.

**A "unique arithmetic fit" I found is an identity, not evidence — and I withdrew
it.**  `0x401000 = 0x1000 + ((ta_data_base − pb_base) >> 4)` is the only fit out
of 90 heap pairs, which is seductive.  But the value that fits is the **TADATA
heap base**, not any allocated object, so the fit is the difference of two *fixed
heap bases* — an identity any fixed layout satisfies, not something pointer
arithmetic produced.  Checked against the live allocations: `tailptr` matches
(and *is* the heap base), `ta_data[1]`/`ta_data[0]`/`bg_block` do not.  Recorded as
a hypothesis generator with a falsifiable prediction (the fault should move if
`pb_base` moves) rather than as a cause.

**The convention audit is complete.**  Against the vendor's *client* builder
(`srvclient/devices/sgx/sgxkick_client.c`): `aui32SpecObject[0..2]` → our
`bg_pds[0..2]` is a straight copy (:2481-2483) and ours are proper `pds_word()`
encodings; `ui32NumPixelPartitions` is a shifted count (:896-913); `ui32TEState[]`
is zeroed by the client (:2101); `sHWRTDataDevAddr` is `base + offsetof + sel*size`
(:2092-2096), a finished device address.  **Every address the live TA command
carries is well-formed** (`TE_PSGREGION_BASE 0x0f025000`, `TE_TPC_BASE 0x0f025050`,
`BIF_TA_REQ_BASE 0x08800000`, `render_details 0x0f02c000`, `rt_data 0x0f025000`).

**So the faulting value is computed inside the µkernel.**  Next: read `BIF_CTRL`
b8-15 (`MMU_BYPASS_*`) mid-hang (one boot, settles a mechanism class), then diff
the µkernel's address arithmetic for `sTARenderDetails`/`sTARTData`/`uRTDataEntry`
and the `sBGObjBaseDevAddr` **write** path (`3d.asm:3335-3341` writes three words
*through* a client-supplied address — the one place a wrong base lands exactly
like this).

**Artefacts.**  Traps 436/437; captures "SESSION 84" fifth/seventh parts.  All
read-only: no code change, no module build, no flash, no media write.

## Eighth–ninth parts — bypass excluded; bg_block confirmed; a second indexing slip of mine

**MMU bypass is excluded by measurement.**  `BIF_CTRL` (0x0c00) is already in the
`status` dump, so this cost no code change.  With the fault cleared, sampled at
idle / 0.5 s / 1.3 s / after: `BIF_CTRL = 0x00000000` every time, `BIF_INT_STAT
0x00008000` (`FLUSH_COMPLETE` only), `BIF_FAULT 0`.  Not one
`MMU_BYPASS_{CACHE,VDM,TE,PBE,TSPP,ISP,USE}` bit is set, so every block goes
through the MMU, trap 435's walk is a genuine page-table walk, and the bypass
hypothesis is **closed**.

**A latent defect found (not this fault).**  The vendor's 3D writes three words
from `aui32SpecObject` straight out of the render details in the branch our core
takes (`SGX_FEATURE_RENDER_TARGET_ARRAYS` is not set for the 540):
`stad.f3` (`3d.asm:3321-3342`).  Our µkernel instead computes the third word
*through an address* — `r6 = *(rd+0x114)` (a device address), `r7 = *(rd+0x108)`
(an unwritten pad, so 0), then `r6 = *(r6 + 4*r7)` — i.e. it **dereferences a
device address** and reads an uninitialised field as an index.  It yields a sane
PDS word today only because `pad_108` is zero.  Wrong on its own terms; not the
walk.

**`bg_block` is correct, and my scare was my own bug.**  I first read it as
`0x0f026000` (kernel_data) and thought the heap was wrong; the real value is
`09863000` (3DPARAMS, in the VM's own `drm_mm` at `params_3d.base`).  My error was
indexing the dump with a **byte** offset instead of dividing by 4 — I was reading
`entry_word` and calling it `bg_block`.  Second indexing slip this session; the
rule is trap 116's.  No conclusion was written from the bad parse.

**So every input to the bg-object write is confirmed correct** — `bg_block` valid
and in the client PD, `cmd+0x08/+0x0c` proper PDS words, `rd+0x114` a valid
address, the PB descriptor from `command+0x24` (`0f010000`).  That weakens the
write path as the source and leaves the `ldad`-site diff as the next step.

**Artefacts.**  Traps 438/439; capture "SESSION 84" eighth/ninth parts.  No code
change, no module build, no flash, no media write.

## Tenth–eleventh parts — the reading route is exhausted; and why the arithmetic was worthless

**The `ldad`-site diff finds no mixed-up field.**  Three candidates checked:

- **The three PB descriptors** (`sTAHWPBDesc`/`s3DHWPBDesc`/`sHostHWPBDesc`,
  consecutive in `SGXMK_TA3D_CTL`): `sgx_init.use.asm:1126-1127` **zeroes the first
  two at init**, so 0 is the vendor's own "no PB" state — and ours are zero too.
  Not a divergence.
- **The render context's `sHWPBDescDevVAddr`**: live `hwctx+0x18 pb_desc =
  0f010000`, the same value the TA command carries at `cmd+0x24`; `flags = 0x21`
  (VALID|OWN_PB).  The value is right even though our flat struct differs from the
  vendor's nested one byte-for-byte (which is why no vendor offset may be copied
  into ours).
- **`page_dir = 1` is a pdtable slot**, resolved by `pd_load()`; session 83 had
  already measured it working (`DIR_LIST_BASE0` = the real client PD mid-TA).

**And 0x400000 turns out to be a structural constant of the layout**, which is the
durable reason the arithmetic reasoning was worthless: three heaps are *exactly*
4 MiB (`sync`, `kernel_code`, `kernel_data`) and several base pairs differ by
exactly 4 MiB.  Any `(a−b)>>4` or `size<<12` over these heaps gives 0x400000 for
many choices.  This session produced four numbers that matched something; three
dissolved on contact, and the fourth is this.

`tailptr_bytes` is excluded twice over: it *does* give 0x400000 at 256×256
(`0x400 × 4096`) but the fault did not move across 64²/256²/512².

**Conclusion: the reading route is exhausted.**  Command clean, client structures
well-formed, bypass excluded, PB descriptors and page-directory resolution
matching the vendor, arithmetic meaningless.  **The next step must be a
measurement:** a `UK_CCBTRACE`-style build storing the address the TA computes
immediately before its first `stad`, read mid-hang — one word, one instrument —
the same shape that settled LASTKICK in session 81.

**Artefacts.**  Traps 440/441; capture "SESSION 84" tenth/eleventh parts.  No code
change, no module build, no flash, no media write.

## Twelfth part — the trace instrument was built and is NOT VALIDATED (a negative about the tool, not the µkernel)

Built the measurement trap 441 asked for, deployed it six times, and could not
make it write.  Recorded as a **negative about the instrument**: per trap 430 a
store path whose positive control also reads 0 has not been validated, so its zeros
are worth nothing.

**What was built, and it is correct as far as static checking goes.**
`UK_ADDRLOG=1 UK_ADDRLOG_REG=<hex>` records the value just written to a chosen
config register (word 0x3d = the register, 0x3e = the value); `UK_STAGELOG=1`
stamps a milestone number (1 do_ta entered … 6 TA_TE programmed) into word 0x3e.
The disassembly shows the intended sequence (`limm r5, #540`, `st.a.d r17, #61`,
`st.a.d r17, #62` with r4 holding the value just `str`'d).  Both flags default off
and **production is byte-identical** — rebuilding kick18ccb's exact recipe gives
`d4087521803889e860e6eccf1cb3c995`, the deployed image — with the SGX545 golden
`83b5b0a7…` intact and `test_emu.py` all OK with the flags on.

**Every deployed build read 0 — including the positive control.**  `addr21c`,
`addr618`, `stage1`, `stage2`, `stage5` all read 0 at 0x3d/0x3e; `stage3` stamped
100 at the `kick` entry, which demonstrably executes (it reads the CCB, the TA is
kicked, the render faults) — and that read 0 too.  Word 0x37 = `0x22222222` (the
existing UK_TICKLOG marker) proves ctlblk stores from this image *do* land, so the
defect is in my store path.

**Three hypotheses, each wrong or unresolved.**  (1) The store offset field is
narrow, so rebase through `imae` — still 0.  (2) `st.a.d base,#i` is 1-based
(`ISA.md:1484`: `base + 4·(i+1)`), so use `#(W−1)` — still 0, **and that reading
contradicts the working TICKLOG site**, which writes `st.a.d r17, #0x37` and lands
`0x22222222` at word 0x37 exactly.  (3) The stamp clobbers live registers — moved
to r4 — still 0.  Each attempt changed the form *and* the word together, which is
why none isolated it.

**The firmware was reverted and the revert proven** (`kick18ccb` `d4087521…`,
named by dmesg).  The unvalidated images are kept on the unit as
`sgx540_ukernel.bin.stagetrace-unvalidated`: `addr21c` `c998729a…`, `addr618`
`1318bc9e…`, `stage1` `6f75ffa4…`, `stage2` `3fce473b…`, `stage3` `68ea209e…`,
`stage5` `1d12b34f…`.  No media write, no flash.
