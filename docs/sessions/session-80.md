# Session 80 (absolute) — the VDM stream's record encodings, walked and excluded

> Numbering: absolute `80` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era session 74**
> (`absolute = Linux-era + 6`).  Continues session 79 (the first render job,
> the stall localized to the VDM).

Date / span: 2026-11-27.
Unit state at the end: **Linux 7.3.3** on hd3 (Nura), region 0 `0x02C1DDC2`
(no flash), region 1 `20a5edd3…`, firmware `kick16tail` (`744b072d…`),
module `1a2949ab…`, desktop untouched.  **`wlan0` DOWN** at the close: the
render attempts' GPU resets take it down and it would not re-associate
(`regulatory prevented using AP config, downgraded`); the documented fix
`iw reg set` is unavailable because `iw` is not installed on the Nura root.
`usb0` and SSH unaffected.  The `open microkernel` dmesg line had wrapped out
by the close (trap 390), so the firmware is identified by file hash.

## Goal of this session

HANDOFF-80 §0b: the 0xb14 method on the **VDM control stream** — dump the
stream and the state-load programs it names, check every record type against
`sgx540defs.h` and the vendor's own stream builder, then the `TA_TE` tail
hygiene, and one render to see whether the stall moved.

## Confirmed (measured, read in a file, or seen on the bench)

* **The bench matched the handoff exactly on arrival** — `kick15pds`
  (`24322c2a…`) live and named by dmesg, module `1a2949ab…`, `.working` =
  kick10 `3fbbd11b…`, `BOOTCTR_FCTRY 0x21`, `wlan0` up, battery full.  No
  inventory correction was needed this time.
* **The state-load programs are dumpable and are real PDS programs.**
  `dump_prog()` searched only the pixel rings; extended to
  `pds_pix`/`pds_vtx`/`usse_pix` and wired to visit each STATE record's PDS
  address.  The five STATE records name five programs in the PDS-vertex ring
  (`vdm_state[0/8/10/18/23]`), dumped in the capture.
* **The stream's record field layout IS per-core in the vendor's own source,
  and ours was the SGX545's — three divergences, all measured** (trap 416):
  the index-list header's b24 `IDXPRES45` (the vendor's 540 client sets it,
  every non-545 builder does), the state record's `PARTITIONS` nibble (1 on
  the 540, 2 on the 545 — in `validate.c` **and** in `srvinit`), and the draw's
  word 5 (the same four numbers at different bit positions,
  `sgxdefs.h:1124-1155`).
* **Changing them does not move the stall.**  First the header+partitions
  (exactly 4 of 28 words), then separately word 5 (exactly 2 of 28, emitted as
  the 540 packing `0x05800102` — byte-for-byte what the vendor's `clear.c`
  computes for its own 3-vertex clear).  Each was submitted as a real render
  job: the timeout signature is identical both times (`EVENT_STATUS2 TRIG_TA`,
  `EVENT_STATUS = 0`, `DPM_PAGE_STATUS 0`, the same
  `events 0x00000000/0x00000010`).  Both reverted, and the stream is again
  byte-identical to session 79's — which is the proof the reverts are complete.
* **The `TA_TE` tail was a real defect and is now pruned** (firmware
  `kick16tail` `744b072d…`).  I re-derived the vendor 540 release binary's
  write set rather than trusting the note: 92 `str #imm` sites over 41 distinct
  offsets, and none of the SGX545-only tail (0x2a8, 0x2b0/0x2b4/0x2d0/0x2d4,
  0x324, 0x364-0x374, 0x520, 0x74c) is among them; 0x230/0x234 *are* written,
  as `MTE_OTPM_CSM_FLUSH/LOAD_BASE` addresses.  `TA_TE` 28 → 16 entries, the
  SGX545 golden stayed byte-identical, all 8 offline tests pass.  One boot:
  null family intact, and the render's timeout **and** its whole mid-hang
  TA/VDM/DPM state are field-for-field session 79's.

## Inferred (reasoning from evidence, not directly measured)

* **The stream's record field layout is not the wall.**  Two controlled
  client-only experiments, each changing only the words claimed, each leaving
  the failure identical.  Strength: strong — it is a direct measurement, not
  an argument.  It does **not** say the encodings are correct; they are wrong
  per the vendor's builder and should be fixed for correctness, just not as the
  blocker.
* **What remains on this axis is the content of the state-load PDS/USE
  programs** (now dumped, unchecked against the 540's USSE/PDS encoding) **or
  the VDM↔USE handshake itself.**  Strength: this is now the only
  0xb14-class suspect left, by elimination of everything measured.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The render fetch is the µkernel's, so it will read the presence bits."**
  It is not: the vendor release image contains no `0x80c00003`-class constant
  at all, and the only reader of `IDXPRES45` in the drop is `ta.asm`'s
  `SplitControlStream` (the context-switch splitter).  So that field is
  load-bearing for the stream's *walking*, but it is not evidence about our
  stall.  Killed by: scanning the vendor image for the constants.
* **"`git checkout` of the tool is safe because only my changes are in it."**
  Session 79's VDM-stream dump was still **uncommitted** in the clone, so the
  revert took it with it.  Restored verbatim from the session-79 capture and
  re-proved by dumping the stream (`efaefc61…` → `1177fafd…`, same 28 words).
  Cost: one rebuild and one wasted verification pass.
* **"`EVENT_STATUS2 = 0x10` proves the TA started"** (session 79's reading) —
  not withdrawn, but noted as weaker than it reads: the bit can be stale from
  the launch, and the cheap control (read it *before* the kick) was never
  taken.  It reproduces identically on both firmware builds and with every
  stream variant, which is itself a hint (trap 417).

## Unknown / open questions

* **Why does the VDM's control-stream execution stall?**  Would be settled by
  checking the dumped state-load PDS/UUSE programs against the 540's USSE/PDS
  encoding, or by instrumenting the USE side.
* **Whether the 540's VDM really fetches from `VDM_CTRL_STREAM_BASE` the way
  the 545's does** — `MTE_CTRL = 0xa2` and `VDM_CTRL_STREAM_BASE = 0x42000`
  are programmed and the VDM is clocked awake, but nothing in the 540 header
  says what a stalled fetch looks like.
* Unchanged: `CACHE_CTRL`'s PARTDM semantics, the `USE_CODE_BASE`
  write/read-back, the 545-only-register existence question (now moot for the
  tail — pruned), and the owed patch-series export (DECISIONS §110).

## Superseded / uncertain

* **HANDOFF-80 §0b's outcome table was right to include "no change".**  It is
  the branch that happened — twice — and the session's value is that it is now
  a measurement rather than a guess.
* **Session 79's `TA_TE`-tail item (trap 415)** — closed: the tail is pruned
  and the 0x230/0x234 mapping is explained.

## Artefacts produced

* Traps **416** (the per-core record encodings, with the three citations and
  the measured negative) and **417** (`EVENT_STATUS2 TRIG_TA` needs a
  before-kick control); the capture
  `docs/captures/sgx540-first-render-2026-11-27.txt` carries the full
  continuation (both experiments' word-level diffs, the state-program dump,
  the tail measurement, the bench at the close).
* **Firmware** `kick16tail` (`744b072d…`) — the pruned `TA_TE`; `.kick15pds`
  `24322c2a…` and `.working` = kick10 `3fbbd11b…` preserved as the revert.
* **Tool** `/tmp/powervr_sgx_frame` `1177fafd…` — session 79's dump restored
  plus the state-program extension; the pre-experiment binaries kept as
  `.old79` (`efaefc61…`) and `.s80base` (`83c8aef1…`).
* **Clone changes** (`local/work/powervr_sgx`, uncommitted as before):
  `ukernel/cores/sgx540.py`'s `TA_TE` pruned with the vendor-write-set
  citation; `tools/gen/sgxgen.c`'s `dump_prog`/`dump_progs` extended and the
  word-5 divergence documented at its site with the measured negative.
