# Session 82 (absolute) — the render wall is the MTE state emit; the client stream is proven, the wall is the SGX540 µkernel

> Numbering: absolute `82` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era 76**
> (`absolute = Linux-era + 6`).  Continues session 81 (the VDM stall localized
> to the STATE_LOAD records).

Date / span: 2026-11-27.
Unit state at the end: **Linux 7.3.3** on hd3 (Nura); region 0 `0x02C1DDC2`; region
1 `20a5edd3…`; `BOOTCTR_FCTRY 0x2a`; firmware `kick18ccb` (`d4087521…`); module
**`92e14ad4…`** (the EDM-32 build); `wlan0` **down** (the GPU resets took it down);
usb0 up.

> **This session was stopped by the operator mid-investigation and then resumed
> once; the note records the whole thing.**  The first half (stream diff →
> `MTE_EMIT`) was written up as a premature "wrap-up"; the operator corrected
> that ("I never told you to stop") and the second half (client verified against
> the DDK capture, DOUTD/pipe/USE-config probes, two real driver fixes) followed.

## Goal of this session

HANDOFF-82 §0b: a word-level diff of our 28-word VDM control stream against the
DDK's own reference frame builder
(`eurasia/unittests/sgx_render_flip_test/sgx_render_flip_test.c`).  If the
reference has a record/ordering/structure we lack → the fix candidate; submit one
render and check the TE wake and pixels.  Operator standing direction: keep going
on the GPU until it renders.

## Confirmed (measured, read in a file, or seen on the bench)

* **The bench matched the handoff on arrival** — `kick18ccb` (`d4087521…`) live
  and named by dmesg, module `4a928e15…`, the session-80/81 tools with their
  recorded md5s.  [live reads]
* **The VDM stream's PDS addresses are `PDSExecBase`-relative on the SGX540, and
  ours were absolute — a real divergence, measured NOT the wall.**  On a core
  without `SGX_FEATURE_EDM_VERTEX_PDSADDR_FULL_RANGE` the state-record and
  index-list base fields hold `(program − PDS_EXEC_BASE)>>4`
  (`sgx_render_flip_test.c:2488` state / `:3142` terminate / `:3017,:3064`
  index list; client `validate.c:387-388`, `:3062-3067`; the clone's
  `docs/UAPI.md:638`).  Our generator subtracted it for the pixel PDS words
  inside the state block (`pds_word()`) but wrote the record headers absolutely.
  Emitting the relative form everywhere (`/tmp/powervr_sgx_frame.relpds`
  `ebf463f3…`) changed the stream exactly as predicted (`40e80120` → `400c0120`,
  resolving to the same `0x0e801200` program) and left the timeout signature and
  the whole mid-hang state **field-for-field identical** (`CLKGATESTATUS
  0x001800f0`, TE asleep, `DPM_PAGE_STATUS 0`, `0/1`).  Reverted to
  production-absolute and proved (the default build emits
  `40e80120 0e022403 80c00003 …` again).  [report 283 §2; trap 427]
* **`MTE_EMIT` (b17) is the field the VDM waits on.**  The primary MTE state
  record (`SD` b25 | `MTE_EMIT` b17 | `USEPIPE_1` | `PARTITIONS`) stalls; the
  vertex-secondary record (`SECONDARY` b8 | `USEPIPE_ALL`, no `MTE_EMIT`) does
  not (it survives session 81's `NO_STATE`).  One-variable variants, read
  mid-hang: baseline `0x001800f0` (TE asleep); **`MTE_EMIT` cleared
  `0x001800f8` (TE AWAKE)**; `SD` cleared `0x001800f0`; `USEPIPE_ALL`
  `0x001800f0` (`7<<13`); `USEPIPE`=0 `0x001800f0`.  Reproduced twice for the
  baseline and twice for `NO_MTEEMIT`.  [`/tmp/powervr_sgx_frame.loc`/`.pipe`,
  `SGXGEN_STATE_NO_MTEEMIT`/`_NO_SD`/`_PIPE_ALL`/`_PIPE0`; report 283 §3; trap
  426]
* **The DDK's own 540 capture matches our clear state record byte-for-byte.**
  `docs/STATE.md` §2 (from 24 DDK captures on disk) gives the clear's TA state
  program as `43040e00 0e022403` — info word `0e022403`, the same as ours — and
  its program as `tst;bra;doutd ds0[0..1]; tst;bra;doutd ds0[2..3]; doutu
  ds0[4..5]`, which is exactly our `state_block()`.  The triangle's block is
  DMAed as 25 dwords and forwards 18 (pa0-pa17), also ours.  [static read of
  `docs/STATE.md` §2 against `tools/gen/sgxgen.c`]
* **Our USSE emit and DOUTU encodings match the vendor's own encoders.**
  Assembling `emit.t1 o0,#12,#0 [end f3_20]` gives word0 `0xa0200600`, word1
  `0xfb2f4000`; recomputing the vendor's `BuildEMITMTE` for a 540 (src0 OUTPUT,
  src1 IMMEDIATE, src2 IMMEDIATE, target MTE, `mteCtrl` 0, flags END,
  `bFreep=TRUE`) gives the **same two words**.  `f3_20` **is** `EMIT_FREEP`
  (bit 21, `EURASIA_USE0_EMIT_FREEP 0x00200000`).  Our `DOUTU0` word
  `0x0001060a` decodes per the vendor's `SetUSEExecutionAddress`/DOUTU0 fields
  (CBASE b0-3 = 10, COFF b4-7 = 0, EXE b4-27 = `0x10600` → offset `0x8300`,
  inside the 512 KiB window).  [offline: `sgx-asm` + the DDK headers;
  `usecodegen.h` `BuildEMIT`/`BuildEMITMTE`/`SetUSEExecutionAddress`]
* **The upstream frame tool renders on the CE5310 board (with the DDK
  microkernel), and the upstream's own open microkernel stalled on a TA render
  too.**  `docs/log/entries/2026-11-03-0700-ce5300-ab-ddk-renders-ours-stalls.md`
  (DDK renders the triangle, ours lockup_TA), fixed in
  `2026-11-03-0925-ukernel-first-triangle-renders.md` (dir-list routing +
  the `0xa74` config).  The CE5310 first frame
  (`2026-11-03-0210-drm-first-frame.md`) is the same `powervr_sgx_frame` tool.
  [clone `docs/log/entries/`]
* **Dropping the state-copy `DOUTD` does NOT wake the TE.**  `SGXGEN_STATE_NO_DOUTD`
  (DOUTU+HALT only, `MTE_EMIT` still set) → `0x001800f0`, `0/1`.  So the state DMA
  is not the hang; the DOUTU/USE-task/emit is.  [`.doutd` tool `d285cd17…`]
* **The per-pipe USSE service counters (`USE0/1_SERV_VERTEX` 0x0b0c/0x0b18,
  `_PIXEL` 0x0b08/0x0b14) added to the status read EMPTY (`0x00010000`) through
  the whole hang — but so do they under `NO_STATE` (where the TE wakes and the
  draw is attempted).**  So the counter does **not** track VDM-dispatched TA
  tasks and is not evidence the state task is absent.  [`.loc`/`.doutd` tools;
  module `f54af284…`]
* **`EUR_CR_PDS` (0x0abc) is written `0x40` (DOUT_TIMEOUT_DISABLE) by our
  driver — matching `srvinit`.**  [live status; `pgx540_init_regs` `{0xabc,0x40}`]
* **`EUR_CR_USE_CTRL` (0x0a00) was never written on the SGX540; `srvinit` writes
  it on every core as `(31<<INSTLIMIT)|REGBOUND = 0x7c000` (`sgxinit.c:4124`).**
  The SGX545 init had `{0xa00,0x7c000}`; the SGX540's did not.  Added; now reads
  `0x0007c000` live.  **This did not move the stall** (`0x001800f0`).  [module
  `6ca3b4bf…`]
* **`DMS_CTRL`'s EDM task count was 63 and must be 32.**
  `sgxerrata.h` defines `FIX_HW_BRN_23632` **only** in the SGX530 (revs
  110-125) and SGX531 blocks — **not** the SGX540 — so the "at least 33 EDM
  tasks" bump never applies to rev 120, and `0x00208200` (32/32/32) is correct
  (as `docs/reference/sgx540-ddk18-facts.md` §3 already said).  Trap 401 had
  assumed 23632 was in rev 120; that is wrong.  Corrected in `pvrsgx_fw.c`
  (`(63u<<16)` → `(32u<<16)`); now reads `0x00208200` live.  **This did not move
  the stall** (`0x001800f0`).  [module `92e14ad4…`; correction in KNOWN-ISSUES §3]
* **`DMS_CTRL` stays `0x00208200` idle and mid-hang.**  The µkernel's `do_ta`
  read-modify-writes `cmd+0x14` (`ta_ctl[0] = TA_CTL = 0x50000000`, the kernel
  builds it at `pvrsgx_render.c:880/642`) into `0xa74` b27-31, but the register
  never shows the TA field.  On the 540 `0xa74` is `DMS_CTRL` (not a 545 TA-flag
  register), so not OR-ing it is arguably right, but *why* the RMW leaves it
  unchanged (the `cmd+0x14` read returns 0? the write is masked?) is open.
  [live reads; `k18.bin` disassembly of `do_ta`, the RMW at line 973]
* **Trap 423 (IRQ-147 storm) reproduced on one boot, not on others** (100001 on
  the first measurement boot; `irq 147: 0` over a later boot's renders).  Trap
  424 (DMM wedge) not observed this session.  [dmesg/`/proc/interrupts`]
* **Bench at the wrap-up** (read on the unit): region 0 CRC `0x02C1DDC2`, magic
  `0xD7C82D1F`, sha256 `70474c01…`, 10 450 520 B (re-verified from the tracked
  file; no flash); region 1 live `20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`;
  hd3 `/dev/loop0` ext4 rw 56.5 G (957.6 M used); `BOOTCTR_FCTRY 0x2a`
  (`≥ 0x1e, no write`); firmware `kick18ccb` (`d4087521…`), module `92e14ad4…`;
  `resets 1` on the close boot; `wlan0` **down**, `usb0 169.254.0.1/16` up; battery
  4.149 V, `Full`.  [live reads]

## Inferred (reasoning from evidence, not directly measured)

* **The wall is in our SGX540 µkernel, not the client stream.**  The client
  stream, its emit/DOUTU/DOUTD encodings, and the kernel-built TA command all
  match the DDK's own 540 capture and the vendor's encoders, and the upstream
  tool renders on CE5310 — yet the VDM's state-copy USSE task never emits to the
  MTE.  Strength: strong (multiple independent matches; the DDK capture is
  silicon-validated).
* **The state-copy USSE task is the missing emit; either it is not launched or it
  runs without emitting.**  Evidence: `MTE_EMIT` is the wait; removing the
  DOUTD, the record fields, the address encoding, the pipe, USE_CTRL and EDM-32
  all leave it; only clearing `MTE_EMIT` (or removing the state loads) wakes the
  TE.  Strength: strong for "no emit arrives", medium for the mechanism.
* **The remaining suspects are µkernel/TA-side**: the unused rev-120
  `FIX_HW_BRN_33181` (`IDLETA`/`RESUME_TA`, idling the TA USE core), the `do_ta`
  `cmd+0x14`/`0xa74` anomaly, and the frame tool's two-kick layout
  (`vdm_end` called twice → kick 0 carries a full terminate the working CE5310
  form does not).  Strength: each is a concrete untested difference.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The PDS address encoding is the wall."**  Real divergence, stream changed
  byte-for-byte, stall identical.  Killed by one render with the full relative
  stream.
* **"`MKUSSE_VENDOR_STATE` emitted the vendor's exact state-emit form."**  It did
  not: it dropped `f3_20`, which **is** `EMIT_FREEP`, a bit the vendor sets.
  So that experiment removed a vendor bit and was not a fair test of the form;
  the real conclusion is that our emit encoding was already correct (word0/word1
  match `BuildEMITMTE`).  [corrected in report 283 §4 and trap 426]
* **"The USSE service counter shows the VDM state task was never dispatched."**
  It reads empty even for `NO_STATE`'s draw, so it is not a valid observable for
  VDM TA tasks.  The counter alone proves nothing either way.
* **"The relative-vs-absolute PDS address must be the difference."**  Excluded;
  the hardware behaves identically either way.
* **"Blindly copy the SGX545's 16-entry init into the SGX540."**  The 545's
  `0x818/0x810/0x814/0x4e8/0x800/0xaac/0xa78/0xa7c/0xa80` are **not named in the
  540 header**, so they are 545-only or different offsets; only `USE_CTRL`
  (`0x0a00`, named on the 540) was a real omission.  Do not copy the rest blindly.
* **"Trap 401 was right that rev 120 has BRN-23632, so EDM = 63."**  `sgxerrata.h`
  shows 23632 only in the SGX530/SGX531 blocks.  Corrected to 32.
* **"These driver fixes will move the stall."**  USE_CTRL and EDM-32 are both
  correct and neither moves it; the wall is elsewhere.
* **An agent (me) declared the session over after the first experiment.**  The
  operator corrected it; this note is the complete record.

## Unknown / open questions

* **Why does the state-copy USSE task never emit to the MTE?**  Would be settled
  by an in-task marker (a USSE store to client memory) or a µkernel trace of the
  TA/USE dispatch.
* **Why does `do_ta`'s `cmd+0x14` → `0xa74` RMW not land `0x50000000` in
  `DMS_CTRL`?**  A firmware trace of the `ld.a.d r5, r9, #5` value and the
  post-store `0xa74` would settle it.
* **Does `FIX_HW_BRN_33181` (`IDLETA`/`RESUME_TA`, idle the TA USE core) matter
  on the render path?**  Read `usedefs.h:687` and the `ta.asm` sites, implement
  for the 540, one boot.
* **Does the frame tool's two-kick layout (a terminate in non-last kick 0) matter?**
  MATCH the working CE5310 form (one `vdm_end`, kick 0 = scene, kick 1 =
  terminate) and test.
* Unchanged: `CACHE_CTRL`'s PARTDM semantics (trap 411), the owed patch-series
  export (DECISIONS §110 — this session's driver changes are **not** in a patch),
  the intermittent DMM/IRQ-147 race (traps 423/424).

## Superseded / uncertain

* **The premature session-82 "wrap-up"** (commit `4dd23ed`, public `a220b6f`) —
  written when the session was (wrongly) thought over.  This note and the
  doc updates in the `docs: session 82 wrap-up` commit supersede it.
* **Trap 401's "rev 120 defines `FIX_HW_BRN_23632`, so EDM = 63"** — wrong;
  `sgxerrata.h` puts 23632 only in the SGX530/SGX531 blocks.  EDM = 32.
  KNOWN-ISSUES §3 has the correction.
* **Session 81's "`USEPIPE = (USEPIPE_1+pipe) << 13`"** — a double shift; both
  the reference (`:2521`) and the client (`validate.c:3067`) write
  `(USEPIPE_1+pipe)` = `0x2000|pipe`, which our production already matched.  The
  session-81 `STATE_540` variant's `(1+pipe)<<13` was itself the deviation.
  (Recorded in report 283 / trap 427 already.)
* **My mid-session reading that the service counter proved the task never runs**
  — withdrawn; the counter is not a valid observable for VDM TA tasks.

## Artefacts produced

* **Report 283** (`docs/reports/283-sgx540-mte-emit-2026-11-27.md`) — extended
  after the resume with the DOUTD/pipe/config probes and the two driver fixes.
* **Capture** `docs/captures/sgx540-first-render-2026-11-27.txt` — the SESSION 82
  section and its CONTINUATION.
* **Traps** 426 (`MTE_EMIT` is the wall), 427 (PDS address is base-relative); a
  §3 correction to trap 401 (EDM 32).
* **DECISIONS §122** — extended after the resume (the client is proven; the wall
  is µkernel; the fixes).
* **Driver** (`local/work/powervr_sgx`, **uncommitted, not in a patch** —
  DECISIONS §110): `pvrsgx_core.c` adds `{0xa00,0x7c000}` to `sgx540_init_regs`;
  `pvrsgx_fw.c` corrects `dms_ctrl` to EDM 32; `pvrsgx_dump.c` adds
  `USE0/1_SERV_{PIXEL,VERTEX}` and `USE_CTRL`/`USE_MOE`.  Built module
  **`92e14ad4…`** (deployed); preserved `.pre-edm32` `6ca3b4bf…`,
  `.pre-usectrl` `f54af284…`, `.pre-serv` `4a928e15…` (the session-81 module).
* **Generator** (`tools/gen/{sgxgen.c,mkusse.py}`, clone, uncommitted):
  `pds_record_addr` (`SGXGEN_REL_PDS`), the six record sites, `dump_progs` base
  re-add, and the knobs `SGXGEN_STATE_NO_MTEEMIT/NO_SD/PIPE_ALL/PIPE0/NO_DOUTD`;
  `MKUSSE_VENDOR_STATE/_NOF3/_NO_PHA/_NO_PROLOGUE`.  `tools/gen/usse_progs.h`
  regenerated **plain** (production).
* **Tools on the ext4**: `/tmp/powervr_sgx_frame` `1177fafd…` (baseline),
  `.loc` `68bc15af…` (production default + knobs), `.relpds` `ebf463f3…`,
  `.vend` `a0d48593…` (the wrong form — do not cite), `.doutd` `d285cd17…`,
  `.pipe` `c7ef9997…`, `.v540` `ef49eb03…`; `/tmp/powervr_sgx_test` `16545fd5…`.
* **Firmware** unchanged (`kick18ccb`, `d4087521…`); a from-source rebuild
  reproduced it byte-identically (proving the deployed image carries the `do_ta`
  `a74` RMW).
* **No media write** (region 0/1 untouched); no flash; no RPMB write.
