# Handoff — session 84 (absolute) → session 85

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 85 = Linux-era 79**, written at the
session-84 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 84 RESULT: the page fault's writer is BOUNDED and is NOT a client
> field.**  The writer covers **`[0x401000, 0x406000)` — 5 pages, 20 KiB, at
> general-heap base (`0x1000`) + 4 MiB** — and it is a **sequential forward**
> write.  Measured on three clean boots by mapping growing ranges at
> `0x400000`: 4/8/16 KiB land the fault at the range **top**
> (`0x402000`/`0x404000`/`0x405000`), and **24 KiB clears it** and reverts the
> job to `job timed out (… events 0x20000000/0x00000010)` — the session-79
> signature.  **The footprint does not move with the client**: surface
> `64x64`/`256x256`/`512x512`, `-k 1`/`-k 2`, `NO_PRIM` and `NO_DRAW` all leave
> it at `0x00401000`, so `pages`, `tail_pointers_size`, `mt_bytes`,
> `MTRegionArraySize`, region sizes and strides are **ruled out** as the base.
> `SGXGEN_NO_STATE` alone moves the write to `0x0dc00000` = `PDS_EXEC_BASE`
> exactly, so the base is picked from two field classes and the defect is the
> **offset**.  **The render wall is unchanged** (session 82's MTE state emit).
> Trap 433; DECISIONS §125; capture "SESSION 84"; tool `/tmp/frame-mapto2`
> `f6b5c81a…`.

> **The instrument had to be fixed first, and that cost no boot.**  Session 83's
> `SGXGEN_MAP_AT`/`SGXGEN_MAP_SIZE` did **not** add coverage — `bo_alloc`
> **relocated the tool's own next general allocation**, so every point mapped the
> target *and* unmapped the baseline region, and the three smallest prescribed
> sizes were the identical configuration (it only ever grows a size).  Found with
> `-n 0` (setup only, no render).  Rebuilt as `map_extra_range()`: the layout
> stays the baseline's and the tool announces `sgxgen: cover <addr> +<len>`.
> **Consequence: session 83's second fault (`0x0001f000`, requestor `0x0002`)
> does not reproduce** — it was that tool's unmapped baseline ring, not a second
> writer, and trap 432's "two different blocks write into the heap" reading is
> withdrawn.

> **SESSION 83 RESULT (2): a REAL 545-shaped port bug was found in the vendor
> source and fixed, and the render's failure MOVED.**  `EUR_CR_DMS_CTRL`'s
> `MAX_NUM_VERTEX_PARTITIONS` is per-core — shift 27 / 5 bits on the SGX545,
> shift **25** / **3** bits on the SGX540 — and the port sent `10 << 27`
> (`0x50000000`) for every core, i.e. **zero vertex partitions** here.  The pixel
> half was missing too (render details +0x6c, which the µkernel ORs in at the
> 3D/SPM, was inside a private pad).  Both are now per-core fields (SGX540
> `4<<25`/`6<<22`; SGX545 bytes unchanged).  **The failure changed from a silent
> 2 s `job timed out` to, 3/3, `job page fault at 0x00401000 (BIF status
> 0x0000e000, requestor 0x2000, write)`** — a WRITE at 4 MiB + 4 KiB, the shape
> of a relative address used absolutely, in no mapped heap.  The frame still
> fails.  Trap 431; DECISIONS §124; capture "SESSION 83 CONTINUATION";
> module `21833840…` (revert `.pre-dms540` `92e14ad4…`).

> **SESSION 83 RESULT (1): the GPU-side marker was built and submitted, and it is
> NOT VALIDATED — its zeros are not evidence.  The render wall is therefore
> UNCHANGED (session 82's MTE state emit).**  Four marker programs, one
> `H_GENERAL` BO, a `limm` patched at load, cache clean+invalidate on the readback,
> all behind `SGXGEN_STATE_MARK`.  **Every marker read 0, and so did all four
> controls** (state-emit task, the clear's vertex task, the pixel-event ptoff
> task, a `sa0`-base store in the vertex-secondary task), with and without
> `NO_STATE`.  The measured reason: the **vertex pipe services nothing** on this
> stack (`USE0/1_SERV_VERTEX`, `USE0_SERV_PIXEL` EMPTY at idle/mid-hang/after),
> the state-copy task runs on the vertex heap, and the only pipe that services
> tasks (`USE0_SERV_EVENT`) is the **microkernel's** event handler — the client
> pixel-event program runs per 3D tile, which never starts.  **So no client task
> class can positively control the marker in a stalling frame.**  Five instrument
> defects found and fixed (`st.a.d` writes base+4; the `limm` patch must be
> read-modify-write or it drops `skipinv`; the shmem BO is cacheable; the marker
> must follow the `phas`; a `r`-using program needs a nonzero DOUTU temp count).
> Trap 430; `docs/sessions/session-83.md`.

> **SESSION 82 RESULT (context): the render wall is the MTE state EMIT, and the
> CLIENT STREAM IS PROVEN — so the wall is the SGX540 µKERNEL.**  `MTE_EMIT`
> (b17) is what the VDM waits on; the state-copy USSE task never delivers the
> emit.  The DDK's own 540 capture is byte-identical to our clear record, our
> `emit.t1` equals `BuildEMITMTE`, the kernel builds the same TA command, and the
> upstream frame tool renders on CE5310.  Excluded by measurement: record fields
> (416/425), PDS address encoding (427), state-copy `DOUTD`, USE pipe (0/1/all),
> `USE_CTRL`, EDM-32 (426/428), `LASTKICK` (420), `IDXFIFO` (419).

> **SESSIONS 78–81 (context): the dispatch pipeline is alive; the first render job
> is consumed end to end; the stream's record layout and the `TA_TE` tail are
> walked and excluded; the VDM stall is in the STATE_LOAD records.**

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**,
with a GPU-accelerated desktop (compositor **and** clients), and a modern
browser.  The long-term graphics route is the open `powervr_sgx` stack
(DECISIONS §107/§108) — this is that port, driving toward a rendered frame.

## 0a. What sessions 78–83 established (do not re-derive)

* **The dispatch pipeline is alive and jobs complete** (session 78):
  `EVENT_PDS_INFO` 0x0a70 = `0x00013c01` (`USE_PIPELINE_0`).  Keep it.
* **The µkernel's host-kick path is exhausted as a suspect** (session 79).
* **`LASTKICK` is excluded** (session 81, trap 420): it sits downstream of the
  stall; **do not** add a `ta_finished` branch.
* **The VDM stall is in the STATE_LOAD records** (session 81, trap 425).
* **The record's fields are excluded** (traps 416/425), and so is the
  `PDSExecBase`-relative address form (trap 427, measured not the wall).
* **`MTE_EMIT` (b17) is the wall** (session 82, trap 426): clearing it wakes the
  TE; the state-copy task never delivers the emit.
* **The client stream is PROVEN against the DDK 540 capture** (session 82).
* **Two config bugs fixed, neither moves the stall** (trap 428): `USE_CTRL`
  `0x7c000`; `DMS_CTRL` EDM 32 (`0x00208200`).
* **The GPU-side marker is BUILT but UNVALIDATED** (session 83, trap 430): on
  this stack every client task class that could positively control it is
  downstream of the same stall, so its zeros say nothing.  **The session-82
  question — does the state-copy USSE task RUN? — is still open.**
* **The desktop is llvmpipe** (DECISIONS §119); the closed pvr stack is blocked
  at modprobe.
* **A render can (intermittently) storm IRQ 147 and wedge the DMM/TILER**
  (traps 423/424).

## 0b. Where the next session picks up — name the 4 MiB base and the 5-page length

**State.**  Session 84 ran the §0b sweep and answered it — but not in the shape
it was asked.  The fault's writer is **bounded**: it writes **5 pages (20 KiB)
at general-heap base + 4 MiB**, sequentially forward, and **the footprint does
not move with the client** (surface size, kick count, draw, prims all leave it at
`0x00401000`).  So the client-side sizes §0b named are **not** the base.  The
render wall is unchanged (session 82's MTE state emit).  Trap 433.

**The measurement, already done (do not repeat it).**  Coverage at `0x400000`
with `/tmp/frame-mapto2` `f6b5c81a…`, three clean boots: 4/8/16 KiB → the fault
lands at the range top (`0x402000`/`0x404000`/`0x405000`); 20 KiB → `0x405000`;
**24 KiB → no fault**, `job timed out (… events 0x20000000/0x00000010)`.
`SGXGEN_NO_STATE` alone moves the write to `0x0dc00000` = `PDS_EXEC_BASE`
exactly, so the base is picked from two field classes and the **offset** is the
defect.  No register in the mid-hang `status` dump equals `0x400000`,
`0x401000` or `0x405000`, so the base is computed inside the VDM/MTE/ISP block
rather than programmed.

**THE NEXT EXPERIMENTS, cheapest first.**  (a) and (b) below were **done in the
same session**; see the continuation block after them for what they returned and
what is left.

**(a) Add `BIF_FAULT`'s `SB` field to the fault message — DONE, names nothing.**
`EUR_CR_BIF_FAULT_SB_MASK` is b4-8 (`sgx540defs.h:2828`), a sub-block selector
below the requesting master.  One module build (`154bc398…`, reverted to
`.pre-sb` `96368b07…`) read **`sb 0x00`**, so it does not narrow requestor
bit 13 either.

**(b) Read `BIF_CTRL` b8-15 mid-hang (`MMU_BYPASS_{CACHE,VDM,TE,PBE,TSPP,ISP,
USE}`, `sgx540defs.h:2794-2813`).**  Still open, and now the cheapest thing left:
`status` does not carry `BIF_CTRL`, so add it (or read it in the fault message).
A bypass bit set for the writing block would be a different mechanism than a bad
pointer — worth reading *before* assuming a pointer arithmetic bug.

**(c) Then decide with the operator** whether a 20 KiB out-of-allocation access at
a fixed 4 MiB offset is in scope before the render wall.

**CONTINUATION (same session) — what (a) returned, and two corrections.**

* **The fault ENDS the job in ~0.07 s and has been MASKING the 2 s wall.**  Timed
  with `/proc/uptime` (the BusyBox `date` has no `%N`; my first attempt with it
  said "0 ms" for everything): coverage 4 KiB → **0.07 s** `page fault`; 24 KiB →
  **2.05 s** `job timed out`.  So **clear the fault before any mid-hang sample**
  or the window is 28× shorter than it looks.  This also explains 79–82: the fault
  was unreachable until the DMS_CTRL fix (trap 431) got the job far enough to
  touch memory.
* **With the fault cleared the writer deposits NO data** — the coverage range
  reads back `16384 dwords, 16384 untouched` (pre-filled `0xA5A5A5A5`).  Per trap
  430 that is *directional*, not evidence (no validated GPU store into a client BO
  exists here): it looks like a **page-table walk**, not a mis-addressed store.
  Tool `/tmp/frame-cover` `756ffd30…`.
* **`FIX_HW_BRN_31620` checked and NOT implicated.**  It is in the 540 rev-120
  block, our driver implements none of it, and its constants match the arithmetic
  exactly (4 MiB PT range `SHIFT 22`; dummy page at `0x1000` in every directory,
  PDE 0/PTE 1; the fault `0x401000` is PDE 1/PTE 1).  But mapping `0x1000` does
  not move the fault and the successful access deposits nothing — unimplemented
  and **unfalsified**, an MMU-writer candidate, not the cause.
* **Two of my own readings retracted:** "mapping `0x1000` makes the render done"
  was a firmware-down fence artefact (`elapsed 0.04 s`, `firmware running: 0` —
  trap 388/389), and the first readback's all-zeros was the instrument
  (`__clear_cache` *cleans*, so the dirty pre-fill was written back over the GPU's
  data; the fill is now flushed before the render).  Trap 434.

**THIRD PART — THE WALK IS STRUCTURALLY SOUND (trap 435), and it was already
measurable.**  The driver's own `dump_bif()` translates the faulting address **in
the client directory** and prints PDE + PTE, riding the **devcoredump** — it had
never been read.  Two one-variable boots:

| configuration | `job's` directory |
|---|---|
| baseline (nothing at `0x401000`; PDE index 1 never created) | `pde 0x00000000 pte 0x00000000` |
| `0x400000` covered (table at PDE 1, one page at PTE 0) | **`pde 0x8ae68001`** `pte 0x00000000` |

* **Baseline: no page table exists at all** for that 4 MiB block, in the client
  *or* the firmware directory — session 83's "in the general heap, merely
  unallocated" was **too generous**: the heap *range* is declared but no table
  exists inside it.  The GPU faults on the **walk**.
* **With a table present the PDE is valid and the PTE is still empty** — the walk
  is sound: a correct lookup that finds an unmapped page, not a corrupt directory
  or a wild pointer.
* **`0x401000` = `heap_base (0x1000)` + 4 MiB + 1 page**, and 4 MiB is exactly one
  full page table, so the two candidate mechanisms are now separable: a **PT range
  shift (22) used as an address shift**, or **a ~4 MiB size used as an address**.
  Both are the §124 class, one level down, in the addressing.  Not the colour
  buffer (`0x47000`, PDE 0).
* **Instrument note:** no `dismiss` node on this kernel, the dump node is consumed
  by the read, and `devcoredump/disabled` is a **one-way kill switch** (do not
  write it; reboot restores it).  One fault per fresh boot, read in the same pass.

**FIFTH–SEVENTH PARTS — the audit is done and the faulting value is computed
INSIDE the µkernel (traps 436/437).**

* The **timeout-path coredump carries no new 4 MiB** — the only `0x00400000` in
  all 30 389 bytes is `cmd[0x24c]` -> `TE_PSG` `BIT(22)`, the flag trap 436
  explained.
* The **convention audit against the vendor's *client* builder**
  (`eurasia/services4/srvclient/devices/sgx/sgxkick_client.c` — `srvclient`, not
  `srvinit`) is **complete**: `aui32SpecObject[0..2]` -> our `bg_pds[0..2]` is a
  straight copy (:2481-2483); `ui32NumPixelPartitions` is a shifted *count*
  (:896-913); `ui32TEState[]` is zeroed by the client (:2101);
  `sHWRTDataDevAddr` is `base + offsetof + sel*size` (:2092-2096).  **Every
  address the live TA command carries is well-formed** (`TE_PSGREGION_BASE
  0x0f025000`, `TE_TPC_BASE 0x0f025050`, `BIF_TA_REQ_BASE 0x08800000`,
  `render_details 0x0f02c000`, `rt_data 0x0f025000`).  Nothing in the command can
  produce an address at the general heap's base.
* **A "unique arithmetic fit" was withdrawn as an identity, not evidence.**
  `0x401000 = 0x1000 + ((ta_data_base − pb_base) >> 4)` is the only fit of 90 heap
  pairs, but the value that fits is the **TADATA heap base**, so it is the
  difference of two *fixed heap bases* — any fixed layout satisfies it.  Its
  falsifiable prediction is recorded: the fault should move to `0x003f1000` if
  `pb_base` moves to `0x08900000`; if it does not, the line closes.

**EIGHTH–NINTH PARTS — bypass EXCLUDED, and every bg-write input confirmed
correct (traps 438/439).**

* **`BIF_CTRL` b8-15 (`MMU_BYPASS_*`) is zero at idle, 0.5 s, 1.3 s and after**
  (fault cleared, so the job runs its full 2 s): `BIF_CTRL 0x00000000`,
  `BIF_INT_STAT 0x00008000` (`FLUSH_COMPLETE` only), `BIF_FAULT 0`.  No bypass bit
  is ever set, so every block goes through the MMU and **the walk in trap 435 is a
  genuine page-table walk**.  That hypothesis is closed by measurement.
* **`bg_block` is correct** (entry+0x10 = `09863000`, 3DPARAMS, in the VM's own
  `drm_mm`), as are `cmd+0x08/+0x0c` (proper PDS words), `rd+0x114` (a valid
  address) and the PB descriptor (`command+0x24` = `0f010000`).  So the bg-object
  write path is **not** obviously the source.
* **A latent defect, not this fault:** our µkernel computes the third bgblock word
  *through an address* — `r6 = *(rd+0x114)` (a device address), `r7 = *(rd+0x108)`
  (an unwritten pad, hence 0), then `r6 = *(r6 + 4*r7)`.  It dereferences a device
  address and uses an uninitialised field as an index; correct today only because
  `pad_108` is zero.  The vendor writes three words straight from
  `aui32SpecObject` (`3d.asm:3321-3342`).  Fix deliberately, one variable, outside
  a bisect.
* **A parse error of mine:** I read `bg_block` as `0x0f026000` and thought the heap
  was wrong; that was a byte-offset-vs-word-index bug (I was reading `entry_word`).
  Parse dumps by word index and divide byte offsets by four (trap 116).

**So the next experiment is a reading task, then one firmware build:**
1. **The `ldad`-site diff (no boot):** enumerate every `[R_TA3DCtl/R_RTData, #DOFFSET(...)]`
   load in `ta.asm`/`3d.asm` and check the field our table uses at each site — the
   method that found traps 431 and 427.  `sTAHWPBDesc`/`s3DHWPBDesc` (two PB
   descriptors, `ta.asm:605-606`) and `sTARenderContext` vs `sTARenderDetails`
   (`:668` vs `:821`) are the pairs most worth checking.
2. **One firmware trace** (`UK_CCBTRACE=1`, one word, one instrument — trap 422):
   store the address the TA computes just before its first `stad` and read it
   mid-hang.  **This was attempted in session 84 and the instrument failed** — see
   the twelfth-part block below before retrying.
3. **Then `FIX_HW_BRN_33181`** (`IDLETA`/`RESUME_TA`), still unimplemented and now
   more interesting because the TA's USE context is where an address would be
   mis-computed.

**TENTH–ELEVENTH PARTS — the reading route is EXHAUSTED (traps 440/441), so the
next step must be a measurement.**

* The **`ldad`-site diff finds no mixed-up field**: the three PB descriptors
  (`sTAHWPBDesc`/`s3DHWPBDesc`/`sHostHWPBDesc`) are **zeroed at init by the
  vendor itself** (`sgx_init.use.asm:1126-1127`), so 0 is its own "no PB" state and
  ours matches; the render context's `sHWPBDescDevVAddr` is populated and equals
  the command's (`hwctx+0x18 = 0f010000`); and `page_dir = 1` is a **pdtable slot**
  that resolves correctly (session 83 measured `DIR_LIST_BASE0` holding the real
  client PD mid-TA).
* **`0x400000` is a STRUCTURAL constant of this core's layout** — three heaps are
  *exactly* 4 MiB (`sync`, `kernel_code`, `kernel_data`) and several base pairs
  differ by exactly 4 MiB.  So any `(a−b) >> 4` or `size << 12` over these heaps
  yields it for many choices.  **Do not treat a matching number as a mechanism**:
  this session produced four that matched something and three dissolved on
  contact.  `tailptr_bytes` is excluded twice over.
* **So: build the trace.**  A `UK_CCBTRACE`-style instrument that stores the
  address the TA computes immediately before its first `stad`, read mid-hang — one
  word, one instrument (trap 422).  That is the shape that settled LASTKICK in
  session 81 after three sessions of inference failed, and the same build recipe
  applies (`UK_CCBTRACE` **without** `UK_WAITLOG`, or the two collide on word
  0x38).

**TWELFTH PART — the trace was BUILT and is NOT VALIDATED (trap 442).  Its zeros
are not evidence; validate the store path FIRST.**

`UK_ADDRLOG=1 UK_ADDRLOG_REG=<hex>` (records the value just written to a chosen
config register: word 0x3d = the register, 0x3e = the value) and `UK_STAGELOG=1`
(stamps milestones 1–6 through `do_ta` into word 0x3e) are in
`ukernel/ukernel.py`, default off.  **Production is byte-identical** — rebuilding
kick18ccb's exact recipe gives `d4087521803889e860e6eccf1cb3c995`, the deployed
image — SGX545 golden `83b5b0a7…` intact, `test_emu.py` all OK with the flags on.

**Six deploys read 0 at 0x3d/0x3e, including a stamp of 100 at the `kick` entry**
— an entry that demonstrably executes (it reads the CCB, the TA is kicked, the
render faults).  Word 0x37 = `0x22222222` (the existing TICKLOG marker) proves
ctlblk stores from this image *do* land, so the defect is in the instrument.

**THE ONE EXPERIMENT FOR NEXT SESSION (no render, no fault, one boot):** copy the
**proven** TICKLOG store verbatim to a different word, and read both.  Under
`UK_STAGELOG=1`, emit at the tick site

```
limm r4, #0x22222222
st.a.d r17, #0x37, r4 [moe]      # known-good control: word 0x37
limm r4, #0x12345678
st.a.d r17, #0x3e, r4 [moe]      # the question: which word does #0x3e write?
```

then read words 0x37, 0x3d, 0x3e and 0x3f by index.  Outcomes:

* **0x37 = `0x22222222` and 0x3e = `0x12345678`** → `#N` addresses word N; the
  earlier failures were something else (then bisect the *form*, not the word).
* **0x37 = `0x22222222` and 0x3f = `0x12345678`** → `#N` is 1-based after all
  (`ISA.md:1484` right, the TICKLOG comment's `#0x37`→word 0x37 the coincidence);
  use `#(W−1)` everywhere.
* **0x37 correct and 0x3e/0x3f both unchanged** → the store to that word is
  suppressed for some other reason (word 0x3e may be special); pick a different
  free word and repeat.

Do not build another ctlblk instrument until one of those three is settled.  The
unvalidated images are on the unit as `sgx540_ukernel.bin.stagetrace-unvalidated`;
the firmware is reverted and proven (`kick18ccb` `d4087521…`).

**Do NOT re-run** (measured this session): the size sweep itself; the
surface-size / kick-count / `NO_PRIM` / `NO_DRAW` invariance tests; the VDM-ring
doubt (doubling it does not move the boundary); the `0x0001f000`/requestor-`0x0002`
fault (it was session 83's relocating instrument, not a second writer); the
`SB`-field build (`sb 0x00`); covering `0x1000` on its own (identical fault).

**Do NOT re-open** (settled earlier): `TA_CTL`'s provenance (trap 431),
`TE_STATE = 0`, `TE_PSG` b24, the `0x618` page-table base, the `0x234` OTPM base.
**Do NOT** read a marker zero as evidence (trap 430) — the marker is still
unvalidated.

**Still open, unchanged:** `FIX_HW_BRN_33181` (`IDLETA`/`RESUME_TA`,
`usedefs.h:687`) unimplemented; the frame tool's two-kick layout; the owed
patch-series export (§110).

**Reference inputs (all present).**
* The DDK drop at `/home/psyden/Pdrive/GFX_Linux_DDK-master` (GPL-2; DECISIONS
  §118).  **Both trees:** `eurasia/` (client + microkernel asm + hwdefs) and
  **`eurasia_km/services4/srvkm/devices/sgx/`** (the kernel module:
  `sgxkick.c`, `sgxutils.c`, `sgxinit.c`, `mmu.c`, `sgxreset.c`).
* `local/work/powervr_sgx/docs/log/entries/` — the upstream's own bring-up logs.
* `docs/STATE.md` §2 — the DDK's own 540 capture words (the client reference).
* The frame tool: `tools/gen/sgxgen.c`, `tools/gen/mkusse.py`,
  `drm/tests/powervr_sgx_frame.c`.
* **`local/work/powervr_sgx/docs/ISA.md:1604`** — the `limm` encoding (its immediate lives *inside* one
  instruction word; do not read a bare `0x00400000` word as an immediate — that
  cost a wrong reading this session before it was withdrawn).

**Do NOT re-run:** the excluded record fields (traps 416/425), the PDS address
(trap 427), the state-copy `DOUTD`, the USE pipe (0/1/all), `USE_CTRL`/EDM-32,
`LASTKICK` (420), `IDXFIFO` (419), the TA register tables, the doorbell read-back
(trap 407), `CACHE_CTRL`'s PARTDM (trap 411), the vendor-image load (§5), Mesa
work, **and do not add more marker variants without a positive control** (trap
430: four controls already all read zero).

**SETTLED (trap 417): do not build an argument on `EVENT_STATUS2`.**

## 0c. The media, and the way back

* **region 0 = `0x02C1DDC2`** — `boot/chainloader/kernel.img`, 10 450 520 B,
  sha256 `70474c01…`, magic `0xD7C82D1F`.  Keep the image FILE.
* **region 1 = `20a5edd3…`** (64 MiB at raw `0x29000000`).
* **hd3 = the session-71 rebake** `5d3eac8f…`, block `0x4915`, grown to 62.5 G.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`; qconn `169.254.0.1:9090`, `PB_LINK=door tools/pbssh.sh`.
* **To stock TabletOS:** the 1917 IFS at `0x825` **and**
  `local/work/rcfs-1917.rcfs` at `0x8D5`, plus the vendor UFS at `0x4915` if
  hd3 is repurposed.
* **Flash loop:** `python3 tools/bbusb.py flash --component <img>
  --dest-block 0x4915 --from-bootrom --password 0000 --apply`; **start the
  flasher, then reset with `reboot -f`**.  A region-0 flash is block `0x825`.
* **A region-0 flash needs the initramfs rebuilt too** — `make … zImage` after
  `rm -f $K/usr/initramfs_data.cpio*`.

## 1. Bench state at the session-84 close (read on the unit)

| item | value |
|---|---|
| OS | **Linux `7.3.3`**, `Nura edge` — not TabletOS.  **No restore required** |
| region 0 | **`0x02C1DDC2`** — no flash.  sha256 `70474c01…`, magic `0xD7C82D1F`, 10 450 520 B |
| region 1 | sha256 **`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`** (live) |
| hd3 | the session-71 rebake **`5d3eac8f…`** at `0x4915`, 62.5 G, `/dev/loop0` ext4 rw |
| `BOOTCTR_FCTRY` | **`0x21`** at the session-84 close (`pbrpmb: topup: 0x21 >= threshold 0x1e, no write`) |
| CPU | `nproc` 2; PID 1 affinity `0,1` |
| GPU | `card1`/`renderD129` = `powervr_sgx`; `card0`/`renderD128` = `omapdrm`.  `resets` climbs **one per render** in a sweep, so start every measurement set from a fresh boot (`resets 0`) — a swept GPU returns empty messages (traps 423/424).  `BIF_FAULT 0` at idle |
| firmware | `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`kick18ccb`** (`d4087521…`, 14 760 B) — **unchanged in session 84**.  Preserved `.kick17ccbtrace` `79bffc34…` (invalid), `.kick16tail` `744b072d…`, …, `.working` = kick10 `3fbbd11b…` |
| module | **`96368b07…`** — **unchanged in session 84** (per-core `DMS_CTRL` partitions + the fault message: requestor, direction, `DIR_LIST_BASE0` vs kernel PD, `*_REQ_BASE`, `MEM_REQ_STAT`).  Reverts: `.pre-dms540` `92e14ad4…`, `.pre-reqlog` `91729a08…`, `.pre-pdlog` `21833840…`, `.pre-reqbase` `2c191e1a…`, plus `.pre-edm32` `6ca3b4bf…`, `.pre-usectrl` `f54af284…`, `.pre-serv` `4a928e15…`, `.pre-render` = `.working` `10c6f435…` |
| desktop | **llvmpipe** (DECISIONS §119).  Do **not** conclude "on the SGX" from `map_files` |
| panel | LVDS `card0-LVDS-1`, `bl_power` 0, dpms On; gesture handle 0 height (temporary, DECISIONS §114) |
| WLAN | **`wlan0` UP** (`10.194.186.131/13`); `usb0` `169.254.0.1/16` UP, dropbear running.  **`iw` is not installed** |
| device-side tools | **`/tmp/frame-mapto2` `f6b5c81a…`** (session 84: `SGXGEN_MAP_AT`/`SGXGEN_MAP_SIZE` as a **coverage** map — layout unchanged, announces `sgxgen: cover …`; **this is the one to use**), `/tmp/frame-vdm8k` `56a55161…` (the VDM-ring negative), `/tmp/powervr_sgx_frame` `1177fafd…` (baseline), **`.loc` `68bc15af…`** (production default + the `SGXGEN_*` knobs), **`.s83` `3da50fab…`** (the marker build), `.relpds` `ebf463f3…`, `.vend` `a0d48593…` (**wrong form — do not cite**), `.doutd` `d285cd17…`, `.pipe` `c7ef9997…`, `.v540` `ef49eb03…`; `/tmp/powervr_sgx_test` `16545fd5…`.  **`/tmp/frame-mapto` `775889e7…` (session 83) RELOCATES the tool's own rings — its layouts must not be cited**; `/tmp` is on the ext4 (trap 354) |
| host | clone `local/work/powervr_sgx` (upstream `95f78ca` + uncommitted patches; DECISIONS §110).  Session 84 touched `tools/gen/{sgxgen.c,sgxgen.h}` (`map_extra_range`) — **not in a patch**.  Session 83 touched `tools/gen/mkusse.py`, `drm/tests/powervr_sgx_frame.c`, and `tools/gen/usse_progs.h` carries the four added marker programs (production programs byte-identical).  Trees `local/kernel-tree-sgx` (compiles) and `local/kernel-tree`; DDK 1.8 at `/home/psyden/Pdrive/GFX_Linux_DDK-master` |

## 2. Working rules (unchanged, plus what this session cost)

* Read `docs/project/SAFETY.md` before any write.  One variable per boot.  An
  agent never writes RPMB; NVRAM read-only.
* Kernel printk goes to the panel; `console=ttyGS0` is inert.  Use `reboot -f`.
  **Ask the operator what is on the panel**; do not screenshot.
* **Do not restart `tinydm`** (trap 353), **do not leave Kumo running or kill it**
  (trap 367) — reboot.
* **Do not call `sync`** (trap 300).  `/tmp` is on the ext4 (trap 354).
* **Register reads:** only from inside the driver (trap 384).
* **Revert experiments and prove the revert.**  Look up every external fact.
  Write the durable fact into the repository in the same turn.
* **A uAPI "pass" is only evidence once the fence is understood** (trap 388).
* **Parse every ctlblk dump BY WORD INDEX**; **one word, one instrument** (trap
  422); **a word is free only if no store anywhere touches it** (trap 403).
* **New this session (trap 430):** a GPU-side marker's zero is not evidence
  until a task of the **same class and pipe** has been seen to write.  On this
  stack **no client task class can be that control in a stalling frame** — the
  vertex pipe services nothing, and the event pipe that runs is the µkernel's.
  Instrument rules that cost a defect each: `st.a.d base,#0` writes `base+4`
  (ISA.md:1485); patch a `limm` with read-modify-write or you drop `skipinv`;
  a shmem BO is **cacheable** (`map_wc` unset) so clean+invalidate before
  reading; put the marker **after** the `phas`; a program that uses `r` needs a
  nonzero DOUTU temp count (PDS.md:301-305).
* **New this session (trap 433, DECISIONS §125): a knob that RELOCATES is not a
  knob that COVERS.**  Check what an instrument does to the *layout* it is meant
  to probe, before trusting a sweep built on it — and prove it with `-n 0`
  (setup only, no render, so no GPU reset).  Session 83's
  `SGXGEN_MAP_AT`/`SGXGEN_MAP_SIZE` moved the tool's own general allocations
  instead of adding a range, so its size sweep was three duplicate points and its
  readings also unmapped the baseline.  **Also: an instruction's encoding is part
  of reading a binary** — a `limm` carries its immediate *inside one instruction
  word* (`ISA.md:1604`), so a bare `0x00400000` word is data, not an immediate.
* **New this session (trap 433): a footprint that does not respond to the client
  is not a client field.**  Before matching a measured size against a field, try
  to move it with that field's inputs; the writer here was invariant to surface
  size, kick count and the draw, which ruled out every candidate in one pass.
* **New this session (trap 434, DECISIONS §126): time every failure mode, with a
  real clock.**  The page fault ends the job in ~0.07 s while the timeout takes
  2.05 s — the fault has been *masking* the wall, so **clear it before any
  mid-hang sample**.  Use `/proc/uptime`: the device's BusyBox `date` has no `%N`,
  and "0 ms" for every run is the tell that the clock is not working.
* **New this session (trap 434): a readback must be shown able to see the write.**
  `__clear_cache` on ARM *cleans*, so a read-time clean of a dirty pre-fill writes
  the pattern back over the GPU's data; flush the fill to DRAM once, before the
  render, and pre-fill with a pattern (not zeros) so "wrote zeros" and "never
  wrote" differ.  A zero from an unvalidated readback is *directional*, not
  evidence (trap 430 still stands).
* **Firmware builds**: explicit output paths; `build.py`'s output is raw and must
  be wrapped by `mkukfw --manifest`; the SAME `UK_*` flags for `test_emu.py`.
  **A module change costs a reboot**; never unbind pvrsrvkm live (trap 383).
  **GPU resets take `wlan0` down.**
* **Reboot before measuring anything if the boot's `open microkernel` line has
  wrapped out of dmesg** (trap 390) — or confirm the firmware file's md5.
* **A render can (intermittently) wedge the DMM and storm IRQ 147** (traps
  423/424): reboot, or read the GPU's registers mid-hang.
* **The client is proven (session 82)** — match the DDK capture/encoder before
  touching the µkernel; the wall is there.
* **Do not trust the SGX545's init-register offsets for the 540** (trap 428).

## 3. The build and deploy recipe, exactly

```bash
cd local/work/powervr_sgx
ROOT=$(cd ../../.. && pwd); export PATH=$ROOT/local/toolchains/armv7-eabihf/bin:$PATH
export SGX_ASM=$PWD/../sgxasm/sgx-asm
# 1. firmware (only if the µkernel changes): explicit output paths; the SAME
#    UK_* flags for the gate, and a CCBTRACE build must NOT also set UK_WAITLOG.
UK_TICKLOG=1 UK_ENTRYFIRST=1 UK_NO_PHAS=1 UK_TIMERDIV=100 UK_WAITLOG=1 \
  UK_BUILD_ID=<id> python3 ukernel/build.py sgx540 /tmp/<id>.bin /tmp/<id>.json
SGX_CORE=sgx540 UK_TICKLOG=1 UK_ENTRYFIRST=1 UK_NO_PHAS=1 UK_TIMERDIV=100 \
  UK_WAITLOG=1 python3 ukernel/test_emu.py                        # all OK
python3 ukernel/golden/check.py                    # SGX545 golden 83b5b0a7…
python3 drm/tools/mkukfw.py /tmp/<id>.bin -o /tmp/<id>w.bin --manifest /tmp/<id>.json
python3 drm/tools/mkukfw.py --check /tmp/<id>w.bin                            # ok
# push (hd3 has NO scp/sftp-server — trap 391); preserve the old file first:
cat /tmp/<id>w.bin | ../../../tools/pbssh.sh 'cat > /lib/firmware/powervr_sgx/<name>.new; md5sum …'
# 2. module: build BOTH source copies (env.sh at the REPO root)
. $ROOT/tools/env.sh
cp drm/powervr_sgx/*.c drm/powervr_sgx/*.h $ROOT/local/kernel-tree-sgx/drivers/gpu/drm/powervr_sgx/
cd $ROOT/local/kernel-tree-sgx && make ARCH=arm CROSS_COMPILE=$CROSS_COMPILE \
  M=drivers/gpu/drm/powervr_sgx modules
#    deploy to /lib/modules/7.3.3/kernel/drivers/gpu/drm/powervr_sgx.ko, keeping
#    the old as .pre-<change>; then reboot -f (a module change costs a boot).
# 3. the frame tool (client-side; no reboot).  Plain usse_progs.h = production.
python3 tools/gen/mkusse.py > tools/gen/usse_progs.h     # needs SGX_ASM
arm-buildroot-linux-gnueabihf-gcc -static -O2 -Wall -DSGXGEN_DRM \
  -Idocs/uapi -Itools/gen \
  -I$ROOT/local/toolchains/armv7-eabihf/arm-buildroot-linux-gnueabihf/sysroot/usr/include/drm \
  -o /tmp/pb-frame drm/tests/powervr_sgx_frame.c tools/gen/sgxgen.c -lm
#    push as /tmp/powervr_sgx_frame.* and chmod +x (an upload loses the exec bit).
# 4. confirm `open microkernel <id>` in dmesg; null family FIRST; then background
#    the frame tool and sample /sys/kernel/debug/dri/129/status at ~0.5/1.4 s:
#      S=/sys/kernel/debug/dri/129/status
#      /tmp/powervr_sgx_frame.loc -n 1 >/tmp/r.txt 2>&1 & P=$!
#      sleep 0.5; grep -E 'CLKGATESTATUS|TE_STATE|DPM_PAGE_STATUS|DMS_CTRL|USE_CTRL' $S
#      sleep 0.9; grep CLKGATESTATUS $S; wait $P; grep -E 'frame 0|frames right' /tmp/r.txt
```

## 4. Useful local extracts

* `docs/captures/sgx540-first-render-2026-11-27.txt` — **the whole campaign,
  sessions 79–83**; read "SESSION 83" first (the marker result), then "SESSION
  82" and "SESSION 82 CONTINUATION", before quoting anything.
* `docs/reports/283-*.md` — the session-82 report (§7/§8 are the continuation:
  the client proof, the two fixes, the `a74` anomaly).
* `docs/sessions/session-83.md`, `session-82.md` — the notes (dead ends
  included).
* `docs/STATE.md` §2 — the DDK's own 540 capture words (the client reference).
* `docs/reference/{sgx540-factsheet,sgx540-ddk18-facts}.md` — the C/K facts.
* **The DDK drop**: `services4/srvinit/devices/sgx/{usedefs.h,ta.asm,
  sgx_utils.asm,sgxinit.c}`, `hwdefs/{usecodegen.h,sgxerrata.h,sgx540defs.h}`,
  `codegen/{usegen,pds}`, `unittests/sgx_render_flip_test/`.
* `local/work/powervr_sgx/{ukernel,docs/log/entries/}` — the µkernel build and
  the upstream's own bring-up logs (the `2026-11-03-0925` entry is the
  TA-completion fix to diff against — §0b step 1).

## 5. What could not be verified

* **Whether the ctlblk store instrument works at all** — `UK_ADDRLOG`/`UK_STAGELOG`
  read 0 six times, including their positive control (trap 442).  Blocks the
  µkernel trace.  Settle with the TICKLOG-store-copy experiment above.
* **Which field supplies the fault's 4 MiB base and its 5-page length.**  Not a
  client field (measured), not a `limm` literal, not a register the driver reads,
  not the TA command.  Needs the trace.

* **Whether the state-copy USSE task runs at all** — §0b step 1.  The marker that
  would answer it is built but **unvalidated** (trap 430): on this stack every
  client task class that could positively control it is downstream of the same
  stall.
* **Why the state-copy USSE task never emits to the MTE** — §0b's subject.  All
  client-side and config causes are excluded.
* **Why `do_ta`'s `cmd+0x14` → `0xa74` RMW does not land** (trap 429).  (Note:
  trap 429 was closed as *provenance* — `0x50000000` is a partition constant in
  the wrong field — but the idle/mid-hang `DMS_CTRL` now reads `0x08208200`, so
  the live RMW's landing is visible; what stays open is nothing.)
* **WHICH field supplies the fault's 4 MiB base and the 5-page length** — the
  session-84 subject.  The base is not a client surface/kick field (measured),
  not a `limm` literal in the µkernel (measured), and not a register the driver
  can read (no `status` word equals it).  Next: `BIF_FAULT`'s `SB` field and
  `BIF_CTRL`'s `MMU_BYPASS_*` bits (§0b (a)/(b)).
* **Whether `FIX_HW_BRN_33181` (`IDLETA`/`RESUME_TA`) matters** — not implemented.
* **Whether the frame tool's two-kick layout matters** — not changed.
* **The intermittent DMM wedge / IRQ-147 storm** — measured on some boots, not
  others; the trigger is not identified.
* **`TE_PSG` (0x218) reads `0x01000000`** (b24 `ENABLE_CONTEXT_STATE_RESTORE`) —
  read live again this session.  The *value* is settled as the vendor's
  unconditional OR (`ta.asm:4346-4352`), so what a write/read-back would still
  settle is only whether the register accepts writes at all.
* **Why the client vertex pipe services nothing** — `USE0/1_SERV_VERTEX` EMPTY
  at idle, mid-hang and after (session 83).  Whether that counter is simply not
  a VDM-TA observable, or no vertex-pipe task ever runs, is not established; it
  is why the marker has no control.
* **The owed patch-series export** for the session-77…84 driver, µkernel and tool
  changes — **nothing is in a patch** (DECISIONS §110).  Session 84 added
  `tools/gen/{sgxgen.c,sgxgen.h}` (`map_extra_range`); session 83 added
  `tools/gen/mkusse.py` and `drm/tests/powervr_sgx_frame.c`.

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 85 (absolute; Linux-era 79)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a (a4f then a4e), KNOWN-ISSUES.md §2 (traps 416-436) and
§3 (3.36), DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 112-126,
docs/reports/INDEX.md, report 283 AND 282,
docs/reference/sgx540-factsheet.md AND docs/reference/sgx540-ddk18-facts.md,
docs/captures/sgx540-first-render-2026-11-27.txt (ALL session-81, session-82,
session-83 and session-84 sections incl. CONTINUATION),
docs/contradictions/sgx540-cache-ctrl-breaks-dispatch.md,
docs/sessions/session-84.md, session-83.md, session-82.md, session-81.md and
session-80.md.

WHERE IT STANDS: Linux 7.3.3 on hd3 (Nura).  region 0 0x02C1DDC2 (no flash),
region 1 20a5edd3..., hd3 5d3eac8f grown to 62.5 G, BOOTCTR_FCTRY 0x21.
card1/renderD129 = powervr_sgx running the open microkernel kick18ccb
(d4087521); module 96368b07 (per-core DMS_CTRL + fault diagnostics).  wlan0 up,
usb0 up, desktop LLVMPIPE -- measured.  Do NOT conclude "on the SGX" from
/proc/PID/map_files -- ask GL (DECISIONS 119).

THE RENDER STALL IS UNCHANGED: the VDM waits for the MTE state EMIT (MTE_EMIT
b17), and the state-copy USSE task never emits.  THE CLIENT IS PROVEN (session
82): the DDK's own 540 capture (43040e00 0e022403) is byte-identical to our
clear record, our emit.t1 equals the vendor's BuildEMITMTE, the kernel builds
the same TA command, and the upstream frame tool renders on CE5310.  Excluded BY
MEASUREMENT: the record fields (416/425), the PDS address (427), the state-copy
DOUTD, the USE pipe (0/1/all), USE_CTRL and EDM-32 (426/428), LASTKICK (420),
IDXFIFO (419).

SESSION 84 RESULT: THE PAGE FAULT'S WRITER IS BOUNDED AND IS NOT A CLIENT FIELD.
It writes [0x401000, 0x406000) -- 5 pages, 20 KiB, at general-heap base (0x1000)
+ 4 MiB -- sequentially forward; 4/8/16 KiB coverage lands the fault at the
range TOP and 24 KiB clears it (reverting the job to "job timed out ... events
0x20000000/0x00000010").  It is INVARIANT to surface 64x64/256x256/512x512, to
-k 1/-k 2, and to NO_PRIM/NO_DRAW -- so pages, tail_pointers_size, mt_bytes,
MTRegionArraySize, region sizes and strides are RULED OUT as the base.
SGXGEN_NO_STATE alone moves the write to 0x0dc00000 = PDS_EXEC_BASE exactly, so
the base is picked from two field classes and the OFFSET is the defect.  Trap
433.  THE INSTRUMENT HAD TO BE FIXED FIRST and it cost no boot: session 83's
SGXGEN_MAP_AT/MAP_SIZE RELOCATED the tool's own general allocations instead of
adding coverage (found with `-n 0`), which also means session 83's second fault
(0x1f000, requestor 0x0002) does not reproduce -- it was the unmapped baseline
ring, not a second writer.

WORK NEXT (HANDOFF section 0b).  ALL of the cheap discriminations are now done
and NEGATIVE: the SB field names nothing (sb 0x00), MMU bypass is excluded by
measurement (BIF_CTRL is 0x00000000 at idle and mid-hang), and the ldad-site diff
finds no mixed-up field.  Session 84 also built the trace instrument and COULD NOT
VALIDATE IT -- its positive control reads 0 too (trap 442).

SO THE NEXT STEP IS NOT THE TRACE YET.  It is: settle whether `st.a.d r17, #N`
addresses word N or word N+1, by copying the PROVEN TICKLOG store
(`limm r4, #0x22222222` + `st.a.d r17, #0x37`) verbatim and adding a second store
of `0x12345678` to `#0x3e`, then reading words 0x37/0x3d/0x3e/0x3f by index.  The
three outcomes and what each means are spelled out in the TWELFTH PART block
above -- do not build another ctlblk instrument until one of them is settled.
Only then: the µkernel trace of the address the TA computes before its first
`stad`, and `FIX_HW_BRN_33181`.

CARRIED FORWARD from this session, they change how to measure: the page fault ENDS
the job in ~0.07 s and has been MASKING the 2 s wall (clear it before any mid-hang
sample); with the fault cleared the writer deposits NO data -- 16384 dwords all
untouched -- which is directional (a page-table walk, not a mis-addressed store)
but NOT evidence (trap 430).  Do NOT re-run the size sweep, the invariance tests,
the VDM-ring doubt, the SB build, covering 0x1000 alone, or the trace as built.
Every measurement set starts from a FRESH boot (resets 0): a swept GPU returns
empty messages.  Time every failure with /proc/uptime -- BusyBox date has no %N,
and "0 ms" for everything is the tell.

SETTLED EARLIER -- do not re-open: TA_CTL's provenance (trap 431), TE_STATE=0,
TE_PSG b24, the 0x618 page-table base, the 0x234 OTPM base.  Do NOT read a marker
zero as evidence (trap 430): the instrument is still unvalidated.

READ THE DDK DROP, IT IS ALL EVIDENCE (DECISIONS 118).  DECISIONS 115 stands: the
microkernel we RUN is ours; the vendor binary is evidence, never a deployment
target.

THE METHOD THAT WORKED: read the driver's status node MID-HANG (background the
tool, sample at ~0.5/1.4 s) and follow CLKGATESTATUS/DMS_CTRL/USE_CTRL; advance
one VARIABLE per boot; match the DDK capture/encoder before probing the uKERNEL.
AND (session 83): a GPU-side instrument needs a positive control of the SAME task
class and pipe -- on this stack none exists in a stalling frame (trap 430).
AND (session 84): an instrument that changes the LAYOUT it probes is not an
instrument (trap 433/DECISIONS 125) -- prove a layout knob with `-n 0` before
spending a boot, and try to MOVE a measured size with the client before matching
it against a field.

TRAPS THAT STILL APPLY: a uAPI "pass" is only evidence once the fence is
understood (388); "consumed" after a reset equals "issued" (389); dmesg's "open
microkernel <build>" is the only truth about which firmware ran (390) --
/var/log/dmesg survives when the ring wraps; hd3 has no scp/sftp-server -- push
with cat FILE | tools/pbssh.sh 'cat > /path' and md5sum first (391), chmod +x
after; /dev/mem cannot reach the GPU (384); edm_init=1 is required (385); a
module change costs a reboot; never unbind pvrsrvkm live (383); GPU resets take
wlan0 down; parse every ctlblk dump BY WORD INDEX; one word, one instrument
(422); do not build an argument on EVENT_STATUS2 (417); a render can storm IRQ
147 and wedge the DMM/TILER, INTERMITTENTLY (423/424); the SGX545 init offsets
are NOT the 540's (428); a GPU marker's zero needs a same-pipe control (430); a
knob that relocates is not a knob that covers (433).

BENCH: region 0 0x02C1DDC2; region 1 20a5edd3...; hd3 live /dev/loop0 ext4 rw;
BOOTCTR 0x21; nproc 2, PID 1 affinity 0,1; battery Full; panel
bl_power 0, dpms On; usb0 169.254.0.1/16 (PB_LINK=usb tools/pbssh.sh works; if
it times out check the cable, the host VPN (trap 341) and ufw (trap 342) first).
iw is NOT installed.  Firmware kick18ccb (d4087521), .working = kick10
(3fbbd11b); module 96368b07 (per-core DMS_CTRL + fault diagnostics), reverts
.pre-dms540 (92e14ad4), .pre-reqlog (91729a08), .pre-pdlog (21833840),
.pre-reqbase (2c191e1a).  Tools: /tmp/frame-cover (756ffd30, coverage + READBACK + pre-fill
flush -- use this one), /tmp/frame-mapto2 (f6b5c81a, the COVERAGE map only), /tmp/frame-vdm8k (56a55161, the VDM-ring negative),
/tmp/powervr_sgx_frame (1177fafd), .loc (68bc15af, production default +
SGXGEN_* knobs), .s83 (3da50fab, the marker build), .doutd (d285cd17),
.pipe (c7ef9997), .relpds (ebf463f3), .vend (a0d48593, wrong form -- do not
cite).  /tmp/frame-mapto (775889e7, session 83) RELOCATES the tool's own rings --
do not cite its layouts.  The clone's tools/gen/usse_progs.h is production PLUS
the four added marker programs.  Full recipe: HANDOFF section 3.

Kernel printk goes to the panel; use reboot -f; ask the operator for visual
checks; do not screenshot; do not restart tinydm (353) or kill Kumo (367).  An
agent never writes RPMB; NVRAM read-only.  Revert experiments and prove the
revert; look up every external fact; do not call sync (300); /tmp is on the ext4
(354).  There is NO RAM-boot path.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
Start the flasher, then reset with reboot -f; the bootrom holds ~20 s.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  WRAP-UP: only the operator
decides when the session stops; then record the bench state, write the session
note, update the canonical docs, verify with tools/docs_audit.py and
tools/tests/test_offline.py, commit privately and publish the snapshot
(docs/project/REPO-MANAGEMENT.md).

Read the document files in full, to be thorough and not miss anything
important.
```
