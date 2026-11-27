# Report 282 — the first render job: consumed end to end, the stall localized to the VDM

Date: 2026-11-27 (session 79, Linux-era 73).  Bench session on the reference
unit.  Follows report 281 (the dispatch fix).  Campaign: seven boots, all
firmware/module file swaps on hd3 — no media writes.

**Headline: the open `powervr_sgx` SGX540 microkernel now takes a real render
job through its whole host-kick path — and the failure point is measured to
the unit.  The TA task is kicked and STARTS (`EVENT_STATUS2 TRIG_TA` latches
at the driver's 2 s timeout) and never finishes: the VDM/MTE/UCACHEL2/USE0
are clocked awake, the TE never starts, the DPM allocates zero pages, nothing
faults, nothing is written.  The stall is inside the VDM's control-stream
execution.**

## 1. The render job type is wired

`pvrsgx_cmd_defs.c`'s `sgx540_render`/`sgx540_ta_kick`/`sgx540_transfer`
carried no `.supported` and no field tables; they now carry the sgx545 tables
verbatim (the base/ext structs are shared uAPI and `pvrsgx_render_job_build`
reads the ext union unconditionally) and `pvrsgx_cmd_core_sgx540.pds_row_bytes
= 16` (Mesa's `pds_emit()` lays PDS programs out in 16-byte rows; our µkernel
data segments are 16-byte units).  The job-types query advertises RENDER +
TRANSFER; the suite reads **31/50**; the **null family intact** (640 µs; 600
jobs/120 ms; `resets: 0`).  [module `85fe59a2…`]

## 2. Five µkernel bugs, each named by an instrument

The campaign's instruments: **UK_WAITLOG** (the live wait-site id at ctlblk
word 0x35; the build manifest maps ids to labels) and, from boot 6, the
**vendor 540 binary's register-write set** (`sgx-disasm -c sgx540` over
`binary2_540_omap4430_android_release/…/ukernel.bin`, all 93 `str #imm`
sites decoded).

| boot | firmware | finding | fix |
|---|---|---|---|
| 1 | kick10 + the render module | the job consumed (`RO 0x2c0` = one TA command); `EVENT_STATUS 0x24000000` (MADD b26 latched); timeout | — |
| 2 | `kick11wl` | spins in `_wait8` — the 0x138 invalidate wait, mask **0x1f** | bit 3 does not exist |
| 3 | `kick12inv` (0x17) | still `_wait8` — b4's request register `PDS_INV3` (0xadc) is never pulsed | per-request masks |
| 4 | `kick13inv` (0x03/0x05) | past the invalidates; spins in `_wait15` — `dpm_load()`'s `EVENT_STATUS2` 0x1000 | 545-only bit |
| 5 | `kick14otpm` | **no wait spins** — the µkernel kicks the TA; `TRIG_TA` latches, no `TA_FINISHED` | the stall moved into the TA |
| 6 | `kick15pds` | the vendor diff: the DPM PDS threshold is **0x614** (we wrote the 545's 0x668), and **no free-list mode selector** exists (0x664 = the 545's `DPM_CONTEXT_PB_BASE`) | both fixed; the stall unchanged |
| 7 | module `1a2949ab…` | the mid-hang TA/VDM/DPM state measured (below) | — |

All builds gated (`test_emu` all-OK, the SGX545 golden byte-identical
`83b5b0a7…`); the null family verified after every deploy.  Traps 413/414.

## 3. The mid-hang state (boot 7, module `1a2949ab…`)

The driver's `status` node gained the TA/VDM/DPM registers (all named in
sgx540defs.h — no unnamed reads).  Mid-hang: `TE_MTILE1/2`
`0x01004004`/`0x02008008`, `TE_SCREEN 0x0000f007` (exact for 256x256),
`TE_PSG 0x01000000`, `VDM_CTRL_STREAM_BASE 0x00042000`, `MTE_CTRL 0xa2`,
`DPM_PDS_PAGE_THRESHOLD 0xf00` (the fix landed), free list `0x10210000` /
alloc `0x10200000`, **`DPM_PAGE_STATUS 0`**; CLKGATESTATUS gains **VDM b6,
MTE b4, UCACHEL2 b11, USE0 b12** over idle while **TE b3, ISP, TSP never
wake**.

## 4. The vendor cross-check

* The vendor's `SGXMKIF_TAREGISTERS` (540 config, 17 words) maps onto our
  `TA_TE` field-for-field; `BIF_TA_REQ_BASE`'s derivation
  (`(params-heap base >> 20) << 20`) equals our `params_base`; the only
  producer of `ui32USELoadRedirect` writes 0 (ours: 0).
* The vendor's TA invalidate acks `0x140 ← 2` — DSC_INV0 only, the
  BRN_31474 shape our `INV_DONE_TA = 0x03` already carries.
* The vendor's `HOSTKICKTA` is a queue stub; the body does settle loops, a
  per-kick `DMS_CTRL` vertex-partition update (ours relies on the reset
  value), and the invalidate/`DIR_LIST_BASE0`/`BIF_CTRL` sequence we do.
* **Found in OUR `TA_TE` tail, unfixed**: cmd+0x2a4 (`rt_flags`) maps to
  0x230 `MTE_OTPM_CSM_FLUSH_BASE` (the vendor uses 0x230/0x234 as
  addresses for its OTPM CSM flush/load, OP 4 / OP 2), and a tail of
  SGX545-only registers (0x2a8, 0x2b0-0x2d4, 0x324, 0x364-0x374, 0x520,
  0x74c) the vendor's 540 build never writes.
* The client's **VDM control stream dumps well-formed** (an
  `SGXGEN_DUMP_PROGS` addition to `tools/gen/sgxgen.c`): 28 words at
  0x42000 — STATE_LOADs into the PDS-vertex heap, two 3-vertex PRIMs,
  terminates.  The encodings came from 545 captures.

## 5. What this means

The µkernel's host-kick path and TA register programming are exhausted as
suspects.  The next unit of work is the 0xb14 method one level down: the
VDM control-stream **record formats** and the client state-load PDS/USE
programs, both 545-derived, against the 540's encodings.  HANDOFF-80 §0b.

## 6. Bench at the close

Region 0 `0x02C1DDC2` (no flash all session), region 1 `20a5edd3…` (re-hashed
live), hd3 the session-71 rebake.  `BOOTCTR_FCTRY 0x21` read fresh at the
wrap-up (self-maintaining; the top-up fired during the campaign).  Live:
firmware `kick15pds` (`24322c2a…`), module `1a2949ab…`; `.working` = the
session-78 pair (kick10 `3fbbd11b…` / `10c6f435…`) intact as the revert.
`wlan0` UP (re-associated); usb0 up.  `/tmp/powervr_sgx_test` and
`/tmp/powervr_sgx_frame` (`efaefc61…`) on the ext4.
