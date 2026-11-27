# Session 79 (absolute) — the first render job: submitted, consumed, and the stall localized to the VDM

> Numbering: absolute `79` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era session 73**
> (`absolute = Linux-era + 6`).  Continues session 78 (the USE_PIPELINE fix
> that made the dispatch pipeline alive).

Date / span: 2026-11-27.
Unit state at the end: **Linux 7.3.3** on hd3 (Nura), region 0 `0x02C1DDC2`
(no flash all session), region 1 `20a5edd3…` (re-hashed live at the wrap-up),
`BOOTCTR_FCTRY 0x21` (self-maintaining), firmware `kick15pds` (`24322c2a…`),
module `1a2949ab…`, desktop untouched.

## Goal of this session

HANDOFF-79 §0b: wire the render job type for the sgx540 entry, verify the
null-job family still passes, submit ONE minimal TA+3D frame, and read the
microkernel's own markers.  Side task: read HANDOFF in full and check it.

## Confirmed (measured, read in a file, or seen on the bench)

* **HANDOFF/STATE inventory errors** — `.kick8w`/`.kick9w` are not on the
  unit; `.pre-doutu`/`.prelaunch`/`.pre-loopback` were mislabelled (device
  md5sums: `a2656c72`/`127a5fad`/`be02c71f`); `wlan0` is UP with a DHCP lease
  (report 281's "down" was mid-session).  [md5sum over SSH, this session;
  fixed in commit `b4ec34d`]
* **The render job type is wired and advertised** — `sgx540_render`/
  `sgx540_ta_kick`/`sgx540_transfer` now carry the sgx545 command tables
  verbatim plus `.supported = true`, and `pds_row_bytes = 16` (Mesa's
  `pds_emit()` lays PDS programs out in 16-byte rows on every core of this
  family; our µkernel data segments are 16-byte units).  The job-types query
  advertises RENDER + TRANSFER; the suite reads **31/50** (one more test than
  session 78's 30/50); the **null family passes unchanged** (`submit-null-job`
  640 µs; `submit-null-job-many` 600 jobs/120 ms; `resets: 0`).
  [module `85fe59a2…`, `pvrsgx_cmd_defs.c` lines 359-411; test runs this
  session]
* **The first render job was consumed by the microkernel** — kernel CCB
  issued 3676 → 3678, and the context CCB advanced `RO 0x2c0` = exactly one
  TA command of the two `-k 2` writes; `EVENT_STATUS 0x24000000` (TIMER b29 +
  MADD_CACHE_INVALCOMPLETE b26) at the timeout; the TA wrote nothing.
  [dmesg timeout line + the `render` debugfs node, boot 1]
* **Three SGX545-shaped µkernel bugs found and fixed with UK_WAITLOG**
  (firmware `kick11wl/kick12inv/kick13inv/kick14otpm`, all gated,
  SGX545 golden byte-identical; the wait-site id at ctlblk word 0x35 named
  each stall): the 0x138 invalidate-done mask 0x1f (bit 3 does not exist;
  then 0x17 still hung — b4's request register `PDS_INV3` 0xadc is never
  pulsed; final fix per-request **INV_DONE_TA = 0x03 / INV_DONE_3D = 0x05**,
  the vendor's BRN_31474 shape, confirmed against the vendor binary's own
  ack `0x140 ← 2`); the OTPM OP write 9 (the 540's `MTE_OTPM_OP` 0x24c
  defines bits 0-2 only; now 1 = CSM_INV); the `EVENT_STATUS2` 0x1000 wait
  (545-only `OTPM_MEM_CLEARED`; compiled out on the 540 — the 540's OTPM
  done is `EVENT_STATUS` b20, already waited).  [traps 413/414; capture
  `sgx540-first-render-2026-11-27.txt` boots 2-5]
* **On `kick14otpm` no wait spins at all** — the µkernel consumes the kick,
  completes every invalidate and the DPM/OTPM loads, and kicks the TA:
  `EVENT_STATUS2 TRIG_TA` (b4) latches at the driver's timeout,
  `EVENT_STATUS = 0` (no TA_FINISHED).  [dmesg timeout line, boot 5]
* **The vendor 540 binary's register-write set diffed against ours** (all 93
  `str #imm` sites decoded from `sgx-disasm -c sgx540`): two more µkernel
  fixes — the DPM PDS-page threshold is **0x614** on the 540 (`TA_PB` wrote
  the 545's 0x668, undefined here), and the free-list loads write **no mode
  selector** (0x664 is the 545's `DPM_CONTEXT_PB_BASE`; `CR_DPM_FL_MODE = 0`
  compiles the write out).  [capture boots 6; kick15pds `24322c2a…`]
* **The mid-hang TA/VDM/DPM state is measured and correct** — the driver's
  status dump (module `1a2949ab…`, extended with the TA/VDM/DPM registers,
  all named in sgx540defs.h): TE_MTILE1/2 `0x01004004`/`0x02008008` and
  TE_SCREEN `0x0000f007` (exact for 256x256), TE_PSG `0x01000000`,
  VDM_CTRL_STREAM_BASE `0x00042000`, DPM_PDS_PAGE_THRESHOLD `0xf00` (the
  kick15 fix landed), free list `0x10210000` / alloc `0x10200000`,
  **DPM_PAGE_STATUS 0 = the TA allocated nothing**; CLKGATESTATUS gains
  **VDM b6, MTE b4, UCACHEL2 b11, USE0 b12** over idle while **TE b3, ISP,
  TSP never wake**.  [status node read mid-hang, boot 7]
* **The vendor's TA register block matches ours field-for-field** —
  `SGXMKIF_TAREGISTERS` (540 config, 17 words) ↔ our `TA_TE` from the same-
  shaped command words; `BIF_TA_REQ_BASE`'s vendor derivation
  (`(params-heap base >> 20) << 20`) equals our `params_base`; the only
  producer of `ui32USELoadRedirect` in the whole DDK tree writes 0.
  [sgx_mkif_client.h lines 280-321; sgxkick_client.c lines 1130-1134]
* **The client's VDM control stream is present and well-formed** — 28 words
  at 0x42000, dumped with an `SGXGEN_DUMP_PROGS` addition to
  `tools/gen/sgxgen.c`: STATE_LOAD records whose PDS programs sit in the
  PDS-vertex heap (0x0e801200+), two 3-vertex PRIM records, the kick
  terminates.  [frame tool `efaefc61…`, this session]
* **Upstream's own first-TA record confirms the bring-up shape** — the DDK
  microkernel under a job directory, the 3D-params-range fault, and the
  two-kick form (`docs/log/entries/2026-11-03-0135-drm-first-ta.md`,
  `…-0210-drm-first-frame.md` in the powervr_sgx clone).

## Inferred (reasoning from evidence, not directly measured)

* **The stall is inside the VDM's control-stream execution.**  Evidence: the
  µkernel side is exhausted (no wait spins; every programmed register verified
  sane; the vendor block matches); the VDM and MTE are clocked awake, the TE
  never starts; the DPM allocates nothing; no BIF fault.  Strength: strong
  for "the stall is at or after the VDM", medium for "the stream itself" —
  an alternative is that a hardware unit the status dump cannot see (the
  USE state-load task behind USE0's clock) is stuck.
* **The stream/program encodings are the next 0xb14-class suspect** because
  every record and program in the client path was derived from 545 captures;
  three separate 545-shaped bugs fell to exactly that check this session.
  Strength: plausible, unproven.
* **"render done" from `powervr_sgx_frame` after a reset is not completion**
  (trap 388 again): the syncobj fence completes without error at the 2 s
  expiry.  Strength: certain — two of the session's "done" runs coincide
  with `resets` advancing and no pixel change.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"0x1f is every PDS-cache done bit the 540 defines"** — the core config's
  own comment listed b0/b1/b2/b4 and then summed them to 0x1f; the sum is
  0x17 (bit 3 does not exist).  Killed by: `_wait8` still hanging with 0x17.
* **"0x17 is the right mask"** — still hung: b4 belongs to `PDS_INV3`
  (0xadc), which no code path pulses.  Killed by: the wait id staying 8;
  settled by the vendor binary's `0x140 ← 2` ack and the per-request masks
  0x03/0x05 (kick13inv moved the stall to `_wait15`).
* **"The OTPM write of 9 is fine — the register is named"** — named in the
  *545's* header; the 540's `MTE_OTPM_OP` stops at bit 2, and the STATUS2
  b12 wait it feeds has no 540 counterpart.  Killed by: `_wait15` hanging
  after both the DPM loads and the b20 OTPM invalidate done had completed.
* **"The PDS-page threshold at 0x668 is where the 545 puts it, so it
  transfers"** — the 540's field is at 0x614; found by the vendor write-set
  diff.  (It was not the stall — kick15pds changed nothing — but it was a
  real defect.)
* **"The µkernel's TA register programming is the stall"** — ruled out:
  every value verified sane mid-hang and the vendor block matches
  field-for-field.  The search moves to the stream/program encodings.

## Unknown / open questions

* **Why does the VDM's control-stream execution stall?**  Would be settled
  by: checking each record type's encoding against sgx540defs.h (STATE_LOAD's
  PDS-address field width, the PRIM record) and dumping the client
  state-load PDS/USE programs from the PDS-vertex heap for the same check
  (the frame tool's `SGXGEN_DUMP_PROGS` already dumps the pixel programs;
  the pds_vtx ring needs the same treatment).
* **Is the 0x664/0x668/0x674/0x73c/0x74c/0x520 region truly absent on the
  540, or implemented-but-unnamed?**  Would be settled by a per-bit
  write/read-back sweep (trap 397's method) on a working system.
* **The µkernel's `DMS_CTRL` vertex-partition update** (the vendor does it
  per kick; ours relies on the reset value 32) — would be settled by a
  frame with more geometry than 32 vertex partitions allow; irrelevant for
  the current stall.
* The owed **patch-series export** (`rootfs/powervr-sgx/`) now also covers
  the render wiring and the four µkernel fixes (nothing is upstream,
  DECISIONS §110).

## Superseded / uncertain

* **HANDOFF-79 §0b's "verify the null-job family still passes first"** —
  done, and it held; nothing superseded.
* **Report 281's "`wlan0` down"** — superseded by the live re-check (UP with
  a lease); report 281's reading was mid-session, not wrong at its time.
* **"The TA register tables are wrong" (HANDOFF §0b's outcome 3)** — refined,
  not wrong: the *host-visible* tables are correct; the carried-over
  hypothesis now shifts to the stream/program encodings.

## Artefacts produced

* **Report 282** (`docs/reports/282-*.md`) and the capture
  `docs/captures/sgx540-first-render-2026-11-27.txt` (boots 1-7); traps
  413/414; DECISIONS §117; ROADMAP 7a (a4b); STATE session-79 block.
* **Driver** (`85fe59a2…`, then `1a2949ab…` with the extended status dump;
  preserved `.pre-render` = `10c6f435…` = `.working` and `.predumpregs`):
  the render job type wired, `pds_row_bytes = 16`, the TA/VDM/DPM status
  registers.  **Module changes are NOT yet in a patch file** — the owed
  export covers sessions 77-79.
* **Firmware** `kick11wl` → `kick15pds` (preserved beside the live file,
  `.working` = kick10 intact): UK_WAITLOG, the invalidate masks, the OTPM
  OP/bit, the STATUS2 wait removal, the PDS threshold, the mode-selector
  removal.
* **Tools**: `/tmp/powervr_sgx_frame` (built from `drm/tests/
  powervr_sgx_frame.c` + `tools/gen/sgxgen.c -DSGXGEN_DRM`,
  `arm-buildroot-linux-gnueabihf-gcc -static`; `mkusse.py` needs
  `SGX_ASM=../sgxasm/sgx-asm`) — build recipe recorded here; the VDM-stream
  dump in `tools/gen/sgxgen.c`.
* Commits: `b4ec34d` (read-first corrections), `a237d22` (boots 1-5),
  `caa599b` (boots 6-7).
