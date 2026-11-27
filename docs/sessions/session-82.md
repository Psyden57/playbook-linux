# Session 82 (absolute) — the stream diff answers: `MTE_EMIT` is the wall

> Numbering: absolute `82` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era 76**
> (`absolute = Linux-era + 6`).  Continues session 81 (the VDM stall localized
> to the STATE_LOAD records).

Date / span: 2026-11-27.
Unit state at the end: **Linux 7.3.3** on hd3 (Nura); region 0 `0x02C1DDC2`
(no flash this session); region 1 `20a5edd3…`; hd3 `/dev/loop0` ext4 rw 56.5 G;
`BOOTCTR_FCTRY 0x2e` on the wrap-up boot (no write); firmware `kick18ccb`
(`d4087521…`); module `4a928e15…`; desktop llvmpipe, panel on; `wlan0` up with a
lease; usb0 up.

## Goal of this session

HANDOFF-82 §0b: a word-level diff of our 28-word VDM control stream against the
DDK's own reference frame builder
(`eurasia/unittests/sgx_render_flip_test/sgx_render_flip_test.c`).  If the
reference has a record/ordering/structure we lack, that is the fix candidate:
submit one render and check the TE wake and pixels.  Operator standing direction
carried from session 81: keep going on the GPU, batch variables per boot, read
the DDK drop freely.

## Confirmed (measured, read in a file, or seen on the bench)

* **The bench matched the handoff on arrival** — `kick18ccb` (`d4087521…`) live
  and named by dmesg, module `4a928e15…`, tools `/tmp/powervr_sgx_frame`
  `1177fafd…` / `.bisect` `b3ee9342…` / `pb-emitn` `411088d8…` /
  `pb-emitn-novcb` `18d2c85c…`, `wlan0` up, `usb0` up.  [live reads]
* **The DDK's reference stream makes every PDS address `PDSExecBase`-relative,
  and ours were absolute** — the single structural difference the diff found
  that had not been varied.  On a core WITHOUT
  `SGX_FEATURE_EDM_VERTEX_PDSADDR_FULL_RANGE` (the SGX540) the state-record and
  index-list base address fields are offsets from `PDS_EXEC_BASE` (the PDS-pixel
  heap base): the reference builder subtracts `uPDSExecBase`
  (`sgx_render_flip_test.c:2488` in `srft_WriteMTEStateToVDM`, `:3142` in
  `srft_WriteMTETerminateCode`), as does the vendor's GLES2 client
  (`opengles2/validate.c:387-388`, `:3062-3063`).  Our generator subtracted it
  for the pixel PDS words *inside* the state block (`pds_word()`) but wrote the
  records' `0x40000000 | addr>>4` absolutely.  [static read; the clone's own
  `docs/UAPI.md:638` states the same rule.]
* **Emitting the relative form for every record changed the stream exactly as
  predicted and moved NOTHING.**  One frame-tool build wrote
  `40e80120 -> 400c0120`, `70e80128 -> 700c0128`, `400c0140`, `600c0114`, … —
  the field for e.g. state[0] becomes `0xc0120`, resolving to the same
  `0x0e801200` program (the tool's `SGXGEN_DUMP_PROGS` reducer now adds the base
  back and prints the programs).  One render: the timeout signature and the
  whole mid-hang state were **field-for-field the baseline's**
  (`CLKGATESTATUS 0x001800f0`, TE asleep, `TE_SCREEN 0xf007`,
  `DPM_PAGE_STATUS 0`, `EVENT_STATUS 0`, `BIF_FAULT 0`, `0/1 frames right`).
  So the record address encoding is a real divergence from the vendor, **not the
  wall**.  Reverted to production-absolute and proved: the default build emits
  `40e80120 0e022403 80c00003 …` again (28 words), the stall reproduced.
  [`.relpds` tool `ebf463f3…`; the production-default `.loc` tool `68bc15af…`]
* **NEW AND DECISIVE: the stall is the `MTE_EMIT` (b17) handshake.**  The
  record that stalls is the **primary** MTE state record (`SD` b25 | `MTE_EMIT`
  b17 | `USEPIPE_1` | `PARTITIONS`); the record that does **not** stall is the
  vertex-secondary one (`SECONDARY` b8 | `USEPIPE_ALL`, no `MTE_EMIT`) — it
  survives session 81's `NO_STATE` run, where the TE wakes.  One-variable
  variants in a single boot (all read mid-hang at t=0.5/1.4 s):

  | variant (one field of the primary state record) | `CLKGATESTATUS` | TE |
  |---|---|---|
  | baseline | `0x001800f0` | asleep |
  | **`MTE_EMIT` cleared (b17)** | **`0x001800f8`** | **AWAKE (b3)** |
  | `SD` cleared (b25) | `0x001800f0` | asleep |
  | `USEPIPE_ALL` (7<<13) | `0x001800f0` | asleep |

  `b3` set is the same signature as removing the state loads entirely (session
  81).  Reproduced twice (two `NO_MTEEMIT` runs both `0x001800f8`; two baseline
  runs both `0x001800f0`).  So the VDM waits for the **MTE state emit from the
  state-copy task**, and it never arrives.  [`.loc` tool with the
  `SGXGEN_STATE_NO_MTEEMIT/NO_SD/PIPE_ALL` knobs]
* **The vendor's exact 540 state-emit USE program form does NOT fix it.**
  `USEGenWriteStateEmitProgram` (`codegen/usegen/usegen.c:592-700`) for a 540 is
  `USEGenWriteStateEmitFragment` (just the `mov o,pa` copies) + one `EMITMTE`
  with flags `EURASIA_USE1_END`, target `EMIT_TARGET_MTE` (=1, `emit.t1`),
  `mteCtrl = EMIT_MTECTRL_STATE` (=0), `src1 = ui32NumWrites`, and — because
  `SGX_FEATURE_USE_UNLIMITED_PHASES` and `SGX_FEATURE_VCB` are both clear —
  **no leading `phas`, no prologue, no VCB pair**.  A tool built with exactly
  that (`MKUSSE_VENDOR_STATE=1`, `.vend` `a0d48593…`) still stalls
  (`0x001800f0`, `0/1`, TE asleep).  So the emit *form* is not the wall; the
  task most likely never reaches its emit.  [`.vend` tool]
* **Trap 423 reproduced on one boot, not the other — intermittent, as recorded.**
  The first measurement boot drove `irq 147` to 100001 and the kernel disabled
  it; the wrap-up boot ran 6 renders (8 resets) with `irq 147: 0` throughout.
  Trap 424 (DMM wedge) was not observed this session (`irq 131` = 63, no
  `dmm … timed`/`could not pin`).
* **The null family is intact before and after, and the GPU survives the
  renders**: at the close `submit-null-job` signalled in 7.5 ms, `1/1 passed`,
  no new reset.
* **Bench at the wrap-up** (read on the unit): region 0 CRC `0x02C1DDC2`, magic
  `0xD7C82D1F`, sha256 `70474c01…`, 10 450 520 B (tracked file, no flash);
  region 1 `20a5edd3…`; hd3 `/dev/loop0` ext4 rw, 56.5 G; `BOOTCTR_FCTRY 0x2e`
  (`≥ 0x1e, no write`); firmware `kick18ccb` (`d4087521…`); module `4a928e15…`;
  `resets 8`, `BIF_FAULT 0`; `wlan0 192.168.0.232/24`, `usb0 169.254.0.1/16`;
  battery 4.149 V, capacity `Full`; panel `bl_power 0`.

## Inferred (reasoning from evidence, not directly measured)

* **The state-copy task never reaches its `emit.t1`.**  Evidence: the TB waits
  on `MTE_EMIT`; clearing it wakes the TE; the vendor's exact emit form still
  stalls; a HALT-only program also stalls (no emit at all).  The remaining
  suspects are therefore the *launch* of the task — the PDS program's `DOUTD`
  (state DMA) or `DOUTU` (task start) — not the emit instruction.  Strength:
  strong for "no emit arrives", medium for "the task is never launched" vs "it
  launches and the DOUTD hangs".
* **The vendor's compiled state-copy PDS program is a concrete untested
  difference.**  `…/intermediates/pds_mte_state_copy/pds_mte_state_copy.h` is a
  23-word program with a 64-byte data segment, a **loop** (`17000CE0`) and a
  `DOUTD` whose `src2` row is **0x18** (`07030203`, decoded with `MOVS`), while
  ours is a flat `TST;BRA;DOUTD ×2;DOUTU` with `src2 = 0`.  Strength: it is
  source, not inference; not yet submitted.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The PDS address encoding is the wall."**  It is a real divergence (the
  vendor is base-relative), the stream changed exactly as predicted, and the
  stall did not move.  Killed by one render with the full relative stream.
* **"Matching the vendor's state-emit USE program form will fix it."**  It did
  not (`MKUSSE_VENDOR_STATE`, `.vend`).  The task's *emit* was never the
  problem; it does not run to an emit.
* **Session 81's "`USEPIPE = (USEPIPE_1 + pipe) << 13`" is a double shift.**
  `EURASIA_TAPDSSTATE_USEPIPE_1` is already `1 << 13` (`sgxdefs.h:863`), and
  both the reference builder (`sgx_render_flip_test.c:2521`) and the client
  (`validate.c:3067`) write `(USEPIPE_1 + pipe)` = **`0x2000 | pipe`**.  Our
  production generator (`c->vdm_counter + 0x2000`) already matched it; the
  session-81 `STATE_540` variant's `(1 + pipe) << 13` was itself the deviation
  (it only differs for pipe > 0).  Corrected here; no measurement depended on
  it (the first state block is pipe 0 and matches either way).
* **The relative-address tool and the production tool are one binary apart.**
  The knob is `SGXGEN_REL_PDS` (opt-in, default absolute per DECISIONS §117);
  both builds report the same generator source and are distinguished only by
  that env var at run time, so the production `usse_progs.h` was regenerated
  plain at the close.

## Unknown / open questions

* **Does the state-copy task run at all?**  Would be settled by a GPU-side
  marker: put a store to a known constant at the top of the state-emit USE
  program (and/or in the PDS program after the `DOUTD`) and read the PDS-vertex
  ring after the render.  This is the next experiment.
* **Does adopting the vendor's compiled `pds_mte_state_copy` (with our DMA
  source and USE address patched in) move the stall?**  It is the one remaining
  *program-content* difference, and it has never been submitted.
* **Why the relative vs absolute PDS address is behaviourally identical on this
  silicon** — whether the VDM ignores the base, masks the field, or resolves
  both to a mapped address.  A write/read-back of `PDS_EXEC_BASE` and a
  deliberate out-of-range address would settle it; not blocking.
* Unchanged: `CACHE_CTRL`'s PARTDM semantics (trap 411), the owed patch-series
  export (DECISIONS §110), the `TE_PSG` read-only question, the intermittent
  DMM/IRQ-147 race (traps 423/424).

## Superseded / uncertain

* **Session 81's "the state record's fields are excluded"** — refined, not
  wrong: USEPIPE/PARTITIONS/DEP/SEG4/attr are excluded, but `MTE_EMIT` was
  never tested and is now measured to be the wall.  Trap 426.
* **Session 81's `STATE_540` USEPIPE derivation** — corrected above (double
  shift); the variant's other two fields (PARTITIONS b9, PDSDMADEPENDENCY b21)
  stand as the vendor's.

## Artefacts produced

* **Tools** on the ext4: `/tmp/powervr_sgx_frame.loc` `68bc15af…` (production
  default + `SGXGEN_STATE_NO_MTEEMIT/NO_SD/PIPE_ALL` and `SGXGEN_REL_PDS`
  knobs), `.relpds` `ebf463f3…` (relative-PDS build), `.vend` `a0d48593…`
  (vendor state-emit form); the pre-session `powervr_sgx_frame` `1177fafd…`,
  `.bisect` `b3ee9342…`, `pb-emitn`, `pb-emitn-novcb` untouched.
* **Generator** (`local/work/powervr_sgx`, uncommitted as before):
  `tools/gen/sgxgen.c` — `pds_record_addr()` (base-relative, `SGXGEN_REL_PDS`),
  all six VDM record sites routed through it, the `dump_progs` reducer adds the
  base back, and the `SGXGEN_STATE_NO_MTEEMIT/NO_SD/PIPE_ALL` knobs;
  `tools/gen/mkusse.py` — `MKUSSE_VENDOR_STATE`/`_NOF3`/`_NO_PHA`/`_NO_PROLOGUE`
  knobs.  `tools/gen/usse_progs.h` regenerated **plain** (production).
* **Docs** — report **283**, the capture's SESSION 82 section, traps **426**
  (`MTE_EMIT` is the wall) and **427** (the PDS record address is
  base-relative), DECISIONS **§122**, ROADMAP a4e, STATE/HANDOFF, this note.
* No media write (region 0/1 untouched); no flash; no RPMB write.
