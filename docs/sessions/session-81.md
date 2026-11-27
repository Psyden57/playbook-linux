# Session 81 (absolute) — LASTKICK and IDXFIFO excluded; the VDM stall localized to the STATE_LOAD records

> Numbering: absolute `81` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era 75**
> (`absolute = Linux-era + 6`).  Continues session 80 (the stream's record
> layout walked and excluded; the `TA_TE` tail pruned).

Date / span: 2026-11-27.
Unit state at the end: **Linux 7.3.3** on hd3 (Nura); region 0 `0x02C1DDC2`
(no flash this session); region 1 `20a5edd3…`; hd3 `/dev/loop0` ext4 rw 56.5 G;
`BOOTCTR_FCTRY 0x30` on the wrap-up boot (no write); firmware `kick18ccb`
(`d4087521…`); module `4a928e15…` (`idx_clk_on` default off, option reverted);
desktop llvmpipe, panel on; `wlan0` up with a lease; usb0 up.

## Goal of this session

HANDOFF-81 §0b: **does the microkernel see
`SGXMKIF_TAFLAGS_LASTKICK`?** — a measurement first (build with
`UK_CCBTRACE=1`, read ctlblk word 0x38 at `do_ta`), and only then consider the
last-kick branch.  The operator then directed: keep going on the GPU, batch
variables per boot, and read the DDK drop freely.

## Confirmed (measured, read in a file, or seen on the bench)

* **The bench matched the handoff on arrival**: `kick16tail` (`744b072d…`)
  live and named by dmesg, module `1a2949ab…`, `.working` = kick10, `wlan0`
  up, `usb0` up.  [live reads]
* **The µkernel reads `ta_flags` correctly and sees LASTKICK when a kick
  carries it.**  With `UK_CCBTRACE=1` (and **no** `UK_WAITLOG`) the default
  two-kick render leaves ctlblk word 0x38 = **`0x00000071`**
  (`FLAGS_BASE 0x60 | FIRST 1 | INVAL_USE 0x10`) and a one-kick render
  **`0x00000073`** (adds `LAST 2`); `0x39 = 1` (`cmd+0x58` ready) proves the
  read followed the host write.  [firmware `kick18ccb`, `d4087521…`; capture
  `sgx540-first-render-2026-11-27.txt`, "SESSION 81"]
* **LASTKICK is on kick 2, not kick 1.**  `last = (k == nkicks - 1)` with
  `nkicks = 2`; the µkernel services only kick 1 (`do_ta` count = 1) because
  kick 1's TA never finishes.  So a last-kick branch in `ta_finished` cannot
  run for this stall.  [same capture; `pvrsgx_render.c:806,874-876`]
* **The `UK_CCBTRACE` measurement collides with `UK_WAITLOG` unless WAITLOG is
  off.**  WAITLOG writes `0x38 = EVENT_STATUS2` every timer tick; the first
  build (`kick17ccbtrace`, `79bffc34…`) read the trap-417 constant `0x10`.
  [disassembly + `ukernel.py:758-762`; trap 422]
* **A render submission can storm IRQ 147 and wedge the DMM/TILER — both
  intermittent.**  Two boots reached `irq 147: nobody cared`, count `100001`,
  then `dmm 4e000000.dmm: timed out waiting for done` → `omapdrm: could not
  pin: -110` → 13× `Failed to setup plane plane-0` (the operator's
  "frames and black").  The bisection boot ran **13 resets with `irq 147: 0`**
  and zero DMM/pin/plane errors, panel stable.  [dmesg on both kinds of boot;
  traps 423/424]
* **IDXFIFO pinned ON moves nothing.**  Module `4a928e15…` (default-off
  `idx_clk_on`) forced the 540's `CLKGATECTL` IDXFIFO field ON — the vendor's
  `DisableClockGating` shape (`sgxinit.c:4778-4784`; ON 1 / AUTO 2) — giving
  `CLKGATECTL = 0x000966aa` and idle `CLKGATESTATUS = 0x001800a0`, but the
  render's hang is field-for-field identical (`0x001800f0`, `TE_SCREEN
  0xf007`, `VDM_STREAM 0x42000`, `DPM_PAGE_STATUS 0`).  Reverted and proved:
  `idx_clk_on=N`, `CLKGATECTL 0x000a66aa`, idle `CLKGATESTATUS 0x001000a0`.
  [firmware + module hashes; trap 419]
* **The launch-stub comparison is closed (static) and yields no one-variable
  test.**  The vendor launches with two USE programs (Primary1 = one
  `emitpds.tasks.taske`; Primary2 = `IUKP2_PDSExec/Data`,
  `InitConfigRegisters`, `ui32InitStatus`, timer); `KickTA` copies the 17
  `sTARegs` words into `TE_AA…BIF_TA_REQ_BASE` and pulses `VDM_START`, as
  ours does; `TE_TPC`/GSG/`MTE_MULTISAMPLECTL` are 545-or-feature gated and
  absent from the 540 binary's write set.  [`sgx_init.use.asm.preproc`,
  `ta.asm:3020-3460`; capture, "DDK DROP READING"]
* **The VDM stall is inside the STATE_LOAD records; the draw/index path is
  not the wall.**  One frame-tool build with env-gated omits, sampled by
  register state: removing the PRIM records changes nothing; removing the
  STATE_LOADs wakes the **TE** (`CLKGATESTATUS 0x001800f8`, b3) and the PRIMs
  reach it.  `IDXFIFO`'s b19 latches even with no PRIM in the stream, so it is
  not the draw's index fetch.  [capture, "VDM-STREAM BISECTION"; trap 425]
* **Three real 540 state-record divergences, and none is the wall.**  The
  vendor's own builder (`validate.c:3055-3072`) writes `USEPIPE =
  (USEPIPE_1 + pipe) << 13` with the pipe rotating 0..3, `PARTITIONS = 1`
  (b9), and `PDSDMADEPENDENCY` (b21) in the copy task; ours writes
  `0x2000 + counter` (never rotates, corrupts the attribute-size field),
  `PARTITIONS = 2` (b10), and omits b21.  Corrected in variants, alone and
  together: identical stall.  [firmware-independent tool tests; trap 425]
* **The data-segment size and two USE-program variables are excluded too.**
  Padding the state block to 4 rows/64 bytes
  (`PDS_MTESTATECOPY_DATA_SEGMENT_SIZE`), passing `ui32EmitSrc1 = n` to the
  EMITMTE (ours always passed `#0`, per `USEGenWriteStateEmitProgram`,
  `codegen/usegen/usegen.c:592`), and dropping the 545-only
  `emit.t3`/`op0xd6` pair (`SGX_FEATURE_VCB`): each stalls identically.
  [same capture]
* **The MADD b26 wait+ack is implemented**, and `USEPIPE = 7` in the state
  records is the header's `USEPIPE_ALL` for a 4-pipe core — neither is a
  divergence.  [`ukernel.py` `invalidate()`; `sgxdefs.h:861-881`]
* **Bench at the wrap-up** (read on the unit): region 0 CRC `0x02C1DDC2`, magic
  `0xD7C82D1F`, sha256 `70474c01…`, 10 450 520 B, re-verified from the tracked
  file; region 1 **live** sha256
  `20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`;
  hd3 `/dev/loop0` ext4 rw, 56.5 G, 941.7 M used; `BOOTCTR_FCTRY 0x30`,
  `no write`; firmware `kick18ccb` (`d4087521…`), module `4a928e15…`;
  GPU status `resets 13 irqs 0`; panel `bl_power 0`, dpms `On`; `wlan0`
  `192.168.0.232/24`; `usb0` `169.254.0.1/16`.  [live reads at the close]

## Inferred (reasoning from evidence, not directly measured)

* **The state load is the only remaining early-stage stall, and the record is
  accepted but never completes.**  Evidence: every stream with a STATE_LOAD
  record stalls before the TE (including a HALT-only program and every
  field/segment/USE variant), while PRIMs alone reach the TE.  Strength:
  strong for "the stall is at the state load"; medium for "the record is
  accepted" (no BIF fault, no lockup, the VDM/timer stay alive).
* **The copy task's completion is what the VDM waits for.**  A HALT-only
  program also stalls; that is consistent with the PDS task never signalling
  completion (our program's DOUTU/USE task may not run to its end).
  Strength: medium.
* **The DDK's own reference frame builder is the fastest way to a known-good
  stream.**  `eurasia/unittests/sgx_render_flip_test/sgx_render_flip_test.c`
  builds a complete stream with the same codegen on this DDK.  Strength:
  strong (it is source, not inference).

## Dead ends and wrong assumptions (valuable — do not skip)

* **"Add `UK_CCBTRACE=1` to the usual build and read word 0x38."**  The usual
  build includes `UK_WAITLOG`, which writes `EVENT_STATUS2` there every tick.
  The first read (`0x10`) looked like a result.  Killed by reading the
  WAITLOG block and rebuilding without it.  [trap 422]
* **"The frame tool submits 2 kicks, so kick 1 carries LAST."**  `last = k ==
  nkicks - 1` puts it on kick 2.  Killed by measurement (`0x71` on kick 1,
  `0x73` with `-k 1`).  [trap 420]
* **"A render always wedges the DMM / storms IRQ 147."**  A third boot ran 13
  resets with `irq 147: 0` and no DMM errors.  Both are races.  Killed by the
  bisection boot's counters.  [traps 423/424]
* **"IDXFIFO is the VDM's index fetch, so the draw stalls there."**  Forcing
  the gate ON changed nothing, and b19 latches with no PRIM in the stream.
  Killed by the gate test and the `NO_PRIM` run.  [traps 419/425]
* **"The state record's fields are the wall" (USEPIPE, PARTITIONS,
  PDSDMADEPENDENCY, segment size).**  Each corrected alone and together;
  identical stall.  [trap 425]
* **"The USE program's EMITMTE count / the 545-only VCB pair is the wall."**
  Corrected against the DDK generator; identical stall.  [trap 425]
* **An agent scripted two prose edits with `python`** instead of the native
  tools (the project's session-45 rule, trap 200).  It worked, but one pattern
  missed and it invalidated the read state for later edits.  Do not do it.

## Unknown / open questions

* **What in the state record/program the VDM waits for.**  Would be settled by
  a word-level diff against the DDK's own reference stream
  (`sgx_render_flip_test.c`) for a small clear — the next experiment.
* **Whether the copy task ever runs.**  A GPU-side counter/flag written by the
  state-emit USE program (its first instruction) would answer it; the state
  program is client-supplied, so the tool can instrument it.
* **Why the DMM/IRQ events are intermittent.**  Would be settled by capturing
  the boot-time state on several renders; they do not block the GPU work.
* **The wrong `USEPIPE`/`PARTITIONS`/`PDSDMADEPENDENCY` fields** are still in
  the production generator (variants are env-gated).  They are known-wrong and
  should be corrected once the stall is understood, not deployed blind.
* Unchanged: `CACHE_CTRL`'s PARTDM semantics (trap 411), the `USE_CODE_BASE`
  existence sweep, the owed patch-series export (DECISIONS §110).

## Superseded / uncertain

* **HANDOFF-81 §0b's outcome table** — "expected `0x..3` on kick 1" and "only
  then add the branch" are superseded: the first kick of a two-kick scene has
  no LASTKICK by construction, and the branch is not a candidate.  The current
  statement is in STEP 1's replacement in `HANDOFF.md` and trap 420.
* **Session 80's "the frame tool submits 2 kicks, so kick 1 carries LAST"** —
  corrected in trap 420 in place.
* **My mid-session claim that the desktop's black frames were the GPU's** —
  the display is omapdrm/llvmpipe; the black came from a DMM wedge, itself
  downstream of a render.  Corrected in trap 424.
* **My claim that the panel/DMM were "wedged by the renders as expected"** —
  corrected by the operator and by the third boot's counters: intermittent.

## Artefacts produced

* **Firmware** `kick18ccb` (`d4087521…`, `UK_CCBTRACE` without `UK_WAITLOG`),
  live; preserved `.kick17ccbtrace` (`79bffc34…`, the invalid build — do not
  cite its value), `.kick16tail` (`744b072d…`) and `.working` = kick10
  (`3fbbd11b…`).
* **Module** `4a928e15…` — `clkgatectl_idxfifo` in `pvrsgx_core_regs`
  (SGX540 `GENMASK(17,16)`) plus the `pvrsgx_fw_start()` gate override, behind
  a default-off `idx_clk_on`; `/etc/modprobe.d/powervr-sgx-opts.conf` reverted
  to `edm_init=1` only.  Preserved `.pre-idxfifo` = `1a2949ab…`.
* **Tools** — `/tmp/powervr_sgx_frame.bisect` (frame tool + generator with
  `SGXGEN_NO_STATE`, `SGXGEN_NO_PRIM`, `SGXGEN_NO_CLEAR`, `SGXGEN_NO_DRAW`,
  `SGXGEN_STATE_STUB`, `SGXGEN_STATE_540`, `SGXGEN_STATE_DEP`,
  `SGXGEN_STATE_SEG4`), `/tmp/pb-emitn`, `/tmp/pb-emitn-novcb`; the sources
  are `tools/gen/sgxgen.c` and `tools/gen/mkusse.py` in the clone
  (`MKUSSE_EMIT_N`, `MKUSSE_NO_VCB`).  **The clone's `tools/gen/usse_progs.h`
  currently holds the `EMIT_N + NO_VCB` build** — regenerate with plain
  `python3 tools/gen/mkusse.py` for the production programs.
* **Docs** — the capture's four new session-81 sections; traps **422** (the
  instrument collision), **423** (the IRQ-147 storm, intermittent), **424**
  (the DMM wedge, intermittent), **425** (the stall is in the STATE_LOAD
  records); traps 419/420 updated in place; DECISIONS **§120** (LASTKICK
  retired; one word, one instrument) and **§121** (the bisection method);
  ROADMAP a4d; STATE/HANDOFF; this note.
* No report was added (the capture is the record, as in session 80).
