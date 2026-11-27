# Current state

## Current state — 2026-11-27 (session 82)

**THE WALL IS THE MTE STATE EMIT — AND THE CLIENT STREAM IS NOW PROVEN, so the
wall is the SGX540 µKERNEL's dispatch of the VDM state-copy USSE task.**
`MTE_EMIT` (b17) is the field the VDM waits on and the task never delivers the
emit.  Session 82 ran HANDOFF-82 §0b (the word-level diff of our 28-word VDM
stream against the DDK's own reference frame builder), multiple boots and
renders, no media writes.  Report 283.

* **The diff found one structural difference not previously varied: the VDM
  stream's PDS addresses are `PDSExecBase`-relative, and ours were absolute.**
  On a core without `SGX_FEATURE_EDM_VERTEX_PDSADDR_FULL_RANGE` (the 540) the
  state-record and index-list base fields hold `(program − PDS_EXEC_BASE)>>4`
  (reference `sgx_render_flip_test.c:2488/3142/3017/3064`; client
  `validate.c:387-388/3062-3067`; the clone's `docs/UAPI.md:638`).  Our
  generator already subtracted it for the pixel PDS words *inside* the state
  block (`pds_word()`) but wrote the record headers absolutely.
* **Emitting the relative form changed the stream exactly as predicted and moved
  NOTHING.**  `40e80120 → 400c0120` (state[0] field `0xc0120`, resolving to the
  same `0x0e801200` program), plus the index-list/draw/vsec/terminate fields;
  the timeout signature and whole mid-hang state were field-for-field the
  baseline's (`CLKGATESTATUS 0x001800f0`, TE asleep, `DPM_PAGE_STATUS 0`,
  `0/1`).  Reverted to production-absolute and proved (the default tool emits
  `40e80120 0e022403 80c00003 …` again).  [trap 427]
* **`MTE_EMIT` (b17) is the wall.**  The primary MTE state record (SD b25 |
  MTE_EMIT b17 | USEPIPE_1 | PARTITIONS) stalls; the vertex-secondary record
  (SECONDARY b8 | USEPIPE_ALL, no MTE_EMIT) does not (it survives session 81's
  NO_STATE, where the TE wakes).  One-variable variants, read mid-hang:
  baseline `0x001800f0` (TE asleep); **`MTE_EMIT` cleared `0x001800f8` (TE
  AWAKE)**; `SD` cleared `0x001800f0`; `USEPIPE_ALL` `0x001800f0`; `USEPIPE`=0
  `0x001800f0`.  Reproduced twice each.  ⇒ the VDM waits for the MTE state emit
  from the state-copy task and it never arrives.  [trap 426]
* **The client stream is PROVEN against the DDK's own 540 capture.**  The
  clear's record is `43040e00 0e022403` — info word `0e022403`, identical to
  ours — with the program `tst;bra;doutd ds0[0..1]; tst;bra;doutd ds0[2..3];
  doutu ds0[4..5]`, exactly our `state_block()`; our `emit.t1` (`a0200600
  fb2f4000`) equals the vendor's `BuildEMITMTE`, and `f3_20` IS `EMIT_FREEP`;
  the kernel builds the same TA command (`pvrsgx_render.c`); the upstream tool
  renders on CE5310.  So the stream/command/encodings are correct.  [traps
  426/427; capture "SESSION 82 CONTINUATION"]
* **Two real SGX540 config bugs fixed to the vendor's value — neither moves the
  stall** (trap 428): `EUR_CR_USE_CTRL` (0x0a00) was unset (must be `0x7c000`,
  `srvinit.c:4124`; now live `0x0007c000`); `DMS_CTRL`'s EDM count was 63 (trap
  401 wrongly assumed rev 120 has BRN-23632 — `sgxerrata.h` puts it only in the
  SGX530/SGX531 blocks) and is now 32 (`0x00208200`).  Excluded this round: the
  state-copy `DOUTD`, the USE pipe (0/1/all), the service counters (not VDM-task
  observables).
* **Next (µkernel-side):** the unused rev-120 `FIX_HW_BRN_33181`
  (`IDLETA`/`RESUME_TA`, idle the TA USE core, `usedefs.h:687`); a firmware
  trace of the `do_ta` `cmd+0x14`/`a74` path (trap 429 — `DMS_CTRL` never gets
  the TA field); the frame tool's two-kick layout (kick 0 carries a terminate).
* **Trap 423 is intermittent as recorded**: the first measurement boot drove
  `irq 147` to 100001 and the kernel disabled it; later boots ran renders with
  `irq 147: 0`.  Trap 424 (DMM wedge) not observed this session.

**Bench at the session-82 close (read on the unit).**  Region 0 `0x02C1DDC2`
(no flash; magic `0xD7C82D1F`, sha256 `70474c01…`, 10 450 520 B, verified from
the tracked file); region 1 live `20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`;
hd3 the session-71 rebake, `/dev/loop0` ext4 rw 56.5 G (957.6 M used).
**`BOOTCTR_FCTRY 0x2a`** (`≥ 0x1e, no write`).  Live firmware **`kick18ccb`**
(`d4087521…`, rebuilt byte-identical from source), module **`92e14ad4…`** (the
EDM-32 build: USE_CTRL + service counters + status; backups `.pre-edm32`
`6ca3b4bf…`, `.pre-usectrl` `f54af284…`, `.pre-serv` `4a928e15…` = the
session-81 module); `resets 1`, `BIF_FAULT 0`.  **`wlan0` DOWN** (the GPU resets
took it down), `usb0` `169.254.0.1/16` up and SSH works; battery `Full` 4.149 V;
panel `bl_power 0`, dpms On; desktop **llvmpipe** (DECISIONS §119).  Tools on
the ext4: `/tmp/powervr_sgx_frame` `1177fafd…` (baseline), `.loc` `68bc15af…`
(production default + the `SGXGEN_*` knobs), `.relpds` `ebf463f3…`, `.vend`
`a0d48593…` (the wrong form — do not cite), `.doutd` `d285cd17…`, `.pipe`
`c7ef9997…`, `.v540` `ef49eb03…`.  **The clone's `tools/gen/usse_progs.h` is
regenerated plain (production); the driver source changes are uncommitted and
not in a patch (DECISIONS §110).**

## Previous state — 2026-11-27 (session 81)

**THE ONE EXPERIMENT IS ANSWERED: the µkernel DOES see the TA flags, and
`SGXMKIF_TAFLAGS_LASTKICK` is NOT the wall — it sits downstream of the stall.**
Session 81 ran HANDOFF-81 §0b step 1 (`UK_CCBTRACE=1`, ctlblk word 0x38 at
`do_ta`) over two boots and three render submissions, no media writes.

* **Build 1 (`kick17ccbtrace`, `79bffc34…`) was INVALID** — the handoff's
  "usual flags + `UK_CCBTRACE`" includes `UK_WAITLOG`, which writes
  `0x38 = EVENT_STATUS2` on every timer tick and overwrote the CCBTRACE store;
  the read value was the trap-417 constant `0x10`.  New **trap 422** (a word is
  not free while another instrument writes it).
* **Build 2 (`kick18ccb`, `d4087521…`, `UK_CCBTRACE` without `UK_WAITLOG`)**
  gives a clean measurement: idle `0x38 = 0`; the default **`-k 2`** render
  leaves `do_ta` count 1 and `0x38 =` **`0x00000071`** = `FLAGS_BASE 0x60 |
  FIRST 1 | INVAL_USE 0x10` — **no LASTKICK**; a **`-k 1`** render gives
  **`0x00000073`** (adds `LAST 2`).  `0x39 = 1` (`cmd+0x58` ready) proves the
  µkernel read the command after the host wrote it, so **delivery is clean and
  the µkernel sees LASTKICK when a kick carries it**.
* **Why that excludes it:** with `nkicks = 2`, `last = (k == nkicks-1)` puts
  LASTKICK on **kick 2**, which the µkernel never services because kick 1's TA
  never finishes.  A last-kick branch in `ta_finished` can never run for a TA
  that never finishes.  **Do not add that branch as a fix attempt.**  This also
  corrects HANDOFF-81 §0b's premise that "kick 1 carries LAST".  The stall is
  still inside kick 1's TA execution (TE never starts, `DPM_PAGE_STATUS` 0).

**New: a render submission can storm IRQ 147 and the kernel then disables it
(trap 423) — intermittent.**  Measured on two boots (clock override off and
on): the first render's PB is allocated at `t≈37.0 s` and within ~2.6 s dmesg
prints `irq 147: nobody cared` → `Disabling IRQ #147` (count **100001**).  The
OCP wrapper reads `status_2 = 1` with `EVENT_STATUS = 0` and
`EVENT_HOST_ENABLE = 0x4000`, so it is **not** the trap-396 TIMER shape; the
handler has no OMAP-wrapper clear (its `vdc_ack` is the Intel VDC latch, a
no-op here).  **Not on every render:** the bisection boot ran 13 resets with
`irq 147: 0` and no storm, so a post-storm timeout is not pure GPU evidence
only *when it happens*.  The null family still passed before it
(`600 jobs/91 ms`).

**New: the same render can wedge the DMM/TILER and take the compositor's
scanout with it (trap 424) — the operator's "frames and black fighting".**
It is **intermittent** (absent on the bisection boot, which ran 13 resets with
no `dmm`/pin/plane errors and `irq 147: 0`).  When it happens the window is
`dmm 4e000000.dmm: timed out waiting for done` (37.25 s) → `Disabling IRQ
#147` → `omapdrm: could not pin: -110` → **13× `Failed to setup plane
plane-0`** through t=78 s.  `omap_dmm_tiler.c:438` waits on the PAT engine's
completion and never gets it, so every atomic commit fails; catacomb keeps
drawing on llvmpipe but its flips stop reaching the scanout.  **A reboot
clears it.**  So a healthy panel is no evidence about the GPU either; use the
GPU's registers, not the desktop.

**IDXFIFO pinned ON — also excluded (trap 419).**  Module `4a928e15…` adds a
default-off parameter (`idx_clk_on`) that forces the 540's `CLKGATECTL`
IDXFIFO field ON, the vendor's own `DisableClockGating` shape
(`sgxinit.c:4778-4784`, ON 1 / AUTO 2).  With it on, `CLKGATECTL = 0x000966aa`
and idle `CLKGATESTATUS = 0x001800a0` (b19 set), but a render gives **the same
hang**: `CLKGATESTATUS 0x001800f0`, `TE_SCREEN 0xf007`, `VDM_STREAM 0x42000`,
`MTE_CTRL 0xa2`, `DPM_PAGE_STATUS 0`, `0/1 frames right`.  Reverted and proven:
with `idx_clk_on=N`, `CLKGATECTL` is back to `0x000a66aa` and idle
`CLKGATESTATUS 0x001000a0`.

**The launch-stub comparison is closed (static) and the VDM-stream bisection
localizes the stall to the STATE_LOAD records (trap 425).**  The vendor's
launch (two USE programs; Primary1 one `emitpds.tasks.taske`, Primary2
`IUKP2_PDSExec/Data` + `InitConfigRegisters` + timer) versus our driver-init +
one `kick` is the known structural difference (DECISIONS §115); `KickTA`'s
per-kick path matches ours.  The bisection (one frame-tool build, env-gated
omits) gives: **removing the PRIMs changes nothing; removing the STATE_LOADs
lets the TE clock wake** (`CLKGATESTATUS 0x001800f8`, b3) — so `IDXFIFO`'s
latch is not the draw and the VDM stops in the state loads.  Three real 540
record divergences (`USEPIPE = (1+pipe)<<13`, `PARTITIONS = 1` at b9,
`PDSDMADEPENDENCY` b21) were corrected in variants and **none moves it**; a
HALT-only program also stalls; the data-segment size (4 rows/64 bytes) and the
USE-side variables — `EMITMTE src1 = n` (ours passed 0) and the 545-only
`emit.t3`/`op0xd6` pair — are excluded too.  **Next: diff the whole 28-word
stream against the DDK's own reference frame builder**
(`eurasia/unittests/sgx_render_flip_test/sgx_render_flip_test.c`), which builds
a complete known-good stream with the same codegen, instead of varying fields.

**Bench at the session-81 close (read on the unit).**  Region 0 `0x02C1DDC2`
(no flash; magic `0xD7C82D1F`, sha256 `70474c01…`, 10 450 520 B, re-verified
from the tracked file); region 1 **re-hashed live** =
`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`; hd3 the
session-71 rebake, `/dev/loop0` ext4 rw 56.5 G (941.7 M used).
**`BOOTCTR_FCTRY` read `0x1d` on the first deploy boot and the top-up fired
(`0x1d -> 0x32`, `PASS`, RPMB counter 12659); the wrap-up boot reads `0x30`,
`no write`** — self-maintaining (read at `dmesg | grep pbrpmb`).  Live
firmware **`kick18ccb`** (`d4087521…`, the clean CCBTRACE build); preserved
`.kick17ccbtrace` (`79bffc34…`), `.kick16tail` (`744b072d…`) and `.working` =
kick10 (`3fbbd11b…`).  Live module **`4a928e15…`** (the gated `idx_clk_on`,
**default off and the option reverted**); preserved `.pre-idxfifo` =
`1a2949ab…`, and `.pre-render` = `.working` = `10c6f435…`.  `wlan0` up with a
lease; `usb0` up; desktop **llvmpipe** (measured: catacomb holds only
`card0`/omapdrm and maps `kms_swrast_dri.so` + `libLLVM-13`).  **Operator
symptom, now explained:** the panel alternated between the desktop and black on
two boots where a render was submitted — the render's DMM/TILER wedge (trap
424), reproduced live and cleared by the reboot; **it did not recur on the
bisection boot**, so it is intermittent.  The panel is `dpms On`,
`bl_power 0`.

## Previous state — 2026-11-27 (session 80)

**THE VDM CONTROL STREAM'S RECORD LAYOUT IS WALKED AND EXCLUDED — it is the
third field on the render path to fall to measurement, and the stall did not
move.**  Session 79 read the client's stream as "well-formed but
545-derived"; this session found the vendor's own *client-side* builder in the
GPL-2 DDK 1.8 drop and it splits every record on `#if defined(SGX545)`:
ours emitted the SGX545 values for **three** fields — the index-list header's
b24 `IDXPRES45` (clear here, set by every non-545 builder), the state record's
`PARTITIONS` nibble (2 here, 1 on the 540 — in `validate.c` *and* `srvinit`),
and the draw's word 5 (the same four numbers at different bit positions,
`sgxdefs.h:1124-1155`).  Emitted for real render jobs — the first two together
(4 of the stream's 28 words), then word 5 alone (2 of 28, `0x05800102`, the
540 packing byte-for-byte) — **the timeout signature was identical each time**
(`EVENT_STATUS2 TRIG_TA`, no `TA_FINISHED`, `DPM_PAGE_STATUS 0`).  Both
reverted; the stream is byte-identical to session 79's again.  So the record
layout is a real divergence to fix for correctness and **not** the wall.

**Also done: the `TA_TE` tail is pruned** (firmware `kick16tail` `744b072d…`).
I re-derived the vendor 540 release binary's write set rather than trusting the
note — 92 `str #imm` sites over 41 offsets, none of the SGX545-only tail
(0x2a8, 0x2b0-0x2d4, 0x324, 0x364-0x374, 0x520, 0x74c) among them, and
0x230/0x234 written as `MTE_OTPM_CSM_FLUSH/LOAD_BASE` addresses, not the 545's
`rt_flags`.  `TA_TE` 28 → 16 entries, SGX545 golden byte-identical, 8/8 offline
tests.  One boot: null family intact, and the render's timeout **and** its whole
mid-hang TA/VDM/DPM state are field-for-field session 79's.

**Next: `SGXMKIF_TAFLAGS_LASTKICK` (trap 420) — the best structural suspect
left.**  The operator cleared the whole DDK 1.8 drop as evidence (DECISIONS
§118), and its microkernel spec says the scene becomes renderable **only** on a
kick carrying LASTKICK.  Our µkernel masks `ta_flags` with `#1` (FIRSTKICK only)
and `ta_finished` does none of the last-kick work (`TACOMPLETE`,
`sTARTData = 0`, TPC flush); the vendor tests the flag in seven places
(`ta.asm:425, 892, 1135, 1136, 2841, 2842`).  The kernel *does* deliver it
(`pvrsgx_render.c:806,874-876`).  **First step is a measurement, not a code
change**: build with `UK_CCBTRACE=1` and read ctlblk word 0x38 at `do_ta`.  If
that does not move it, the next discriminator is the **launch stub** — ours is
`[0]=0x0020000f [1]=0x00000002 … [4]=0x07000305 [5]=0xaf000000`, while the
vendor launches with two `emitpds.tasks.taske` instructions
(`sgx_init.use.asm.preproc:4622,4643`).  Traps **416-420**; the capture's
"SESSION 80 CONTINUATION" sections are the record.

**Also found: `IDXFIFO` wakes mid-hang and LATCHES, and it exists only on the
540** (trap 419).  `CLKGATESTATUS` sampled 7× at 0.3 s through the whole hang is
`0x001000a0` at t=0 then `0x001800f0` at t=1…6; the wake set over idle is
{MTE b4, VDM b6, **IDXFIFO b19**}, TE never wakes, and the driver's reset
restores it bit-identically.  IDXFIFO is the index FIFO — the VDM's first read
of a draw — and **the SGX545 header does not mention it once**, so it is the one
unit this 545-derived port was written without.  `USE0_SERV_EVENT` also climbs
through the hang (`0x10bdb` → `0x10c11`) while USE1 idles, i.e. the EDM is alive
and the *render* is what is stuck.

**Two corrections landed in the same session (traps 417/418).**  (1)
**`EVENT_STATUS2 = 0x10` is non-discriminating** — it reads `0x10` on an *idle*
driver and through every path, verified by disassembling our own image (zero
stores to word 70 `EVENT_STATUS2`, seven to word 69 `HOST_CLEAR2`), so
**session 79's "the TA task STARTS" is not supported by that bit**; read it as
"the host delivered the kick and the µkernel consumed it".  What still shows the
VDM running: `CLKGATESTATUS` (VDM/MTE/UCACHEL2/USE0 awake), the VDM/TE
register block, `VDM_CTRL_STREAM_BASE = 0x42000`.  (2) The 540 header **names**
what our code called "unnamed" — `0x200` = `EUR_CR_VDM_START` (and there is no
`VDM_TASK_KICK` on this core, so `VDM_START` *is* the TA kick), `0x428` =
`EUR_CR_ISP_START_RENDER`, `0x694/0x698` = `EUR_CR_DPM_TASK_STATE`/`_CONTROL`
(`CLEAR`=2), `0x684/0x680` = `EUR_CR_DPM_TASK_TA_FREE`/`_3D_FREE` (`LOAD`=1).
Checked against the vendor and **matching**: the `TA_TE` register order, the
17-word `SGXMKIF_TAREGISTERS`, `dpm_load`'s CLEAR write + `0x240` CLEAR-wait,
`dpm_freelist`'s LOAD/ack shape.  The dumped **state-load programs decode clean**
as our own `state_block()` records.  Comments corrected; the wrapped firmware
rebuilds byte-identical (`744b072d…`), so nothing was re-deployed.

**Bench**: region 0 `0x02C1DDC2` (no flash), region 1 `20a5edd3…`, hd3 the
session-71 rebake.  Live firmware **`kick16tail`** (`744b072d…`); preserved
beside it `.kick15pds` (`24322c2a…`), the earlier variants and **`.working` =
kick10 `3fbbd11b…`** as the revert.  Module `1a2949ab…` unchanged
(`.pre-render` = `.working` = `10c6f435…`).  `/tmp/powervr_sgx_frame`
`1177fafd…` (session 79's dump restored + the state-program extension), with
`.old79` and `.s80base` kept.  `BOOTCTR_FCTRY 0x20` (read from
`/var/log/dmesg`: `pbrpmb: topup: … = 20000000`; the live dmesg ring had
wrapped — trap 390).  region 0 re-verified this session from the tracked file:
CRC `0x02C1DDC2`, magic `0xD7C82D1F`, sha256 `70474c01…`, 10 450 520 B;
region 1 re-hashed **live** (64 MiB at raw `0x29000000`) =
`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`.
hd3 is `/dev/loop0` ext4 **rw**, 56.5 G, 935.7 M used.  **`wlan0` UP** with a
DHCP lease (`192.168.0.232/24`) — it re-associated after the GPU resets, just
slowly (an earlier "it will not re-associate" reading in this same session was
premature and is corrected here); `usb0` `169.254.0.1/16` UP, SSH works;
desktop **llvmpipe** (DECISIONS §119), untouched.
**Trap 390 note:** the `open microkernel kick16tail` line had wrapped out of
dmesg by the close, so the live firmware is identified by
`md5sum /lib/firmware/powervr_sgx/sgx540_ukernel.bin` = `744b072d…`.

## Previous state — 2026-11-27 (session 79)

**THE FIRST RENDER JOB WAS SUBMITTED, CONSUMED BY THE MICROKERNEL, AND ITS
PRECISE FAILURE POINT IS MEASURED: the TA task is kicked and STARTS
(`EVENT_STATUS2 TRIG_TA` latches at the driver's timeout) and never finishes —
no TA_FINISHED, and it wrote nothing (no region headers, no tail pointers,
BIF_FAULT 0).**  The render job type is wired (module `85fe59a2…`:
`sgx540_render/ta_kick/transfer` carry the sgx545 command tables verbatim and
`.supported = true`, `pds_row_bytes = 16`; the job-types query now advertises
RENDER + TRANSFER, the suite reads 31/50), the null-job family still passes
(640 µs; 600 jobs/120 ms, resets 0), and THREE SGX545-shaped µkernel bugs were
found and fixed on the way (traps 413/414): the 0x138 invalidate-done masks
(0x1f → per-request 0x03/0x05), the OTPM OP bit 3 (545-only), and the STATUS2
mem-clear wait (545-only, compiled out on the 540).  Four firmware builds
(`kick11wl/kick12inv/kick13inv/kick14otpm`, all gated, SGX545 golden
byte-identical) with UK_WAITLOG naming each stuck wait site; the campaign is
in `docs/captures/sgx540-first-render-2026-11-27.txt`.

**Next: the VDM control-stream record formats and the client state-load
PDS/USE programs against the 540's encodings** (the 0xb14 method on the
stream).  The continuation (boots 6-7) fixed two more µkernel registers from
the vendor binary's write-set diff (the DPM PDS-page threshold is 0x614 on
the 540, not the 545's 0x668; no free-list mode selector — kick15pds
`24322c2a…`), extended the driver's status dump with the TA/VDM/DPM
registers (module `1a2949ab…`), and measured the mid-hang state: the TA
registers are programmed correctly, the VDM/MTE/UCACHEL2/USE0 are clocked
awake, the TE never starts, the DPM allocates zero pages, no BIF fault, and
the vendor's TA register block matches ours field-for-field.  Known-bogus
in our TA_TE tail, unfixed: cmd+0x2a4 (rt_flags) → 0x230
MTE_OTPM_CSM_FLUSH_BASE, and the SGX545-only register tail (0x2a8,
0x2b0-0x2d4, 0x324, 0x364-0x374, 0x520, 0x74c).

**Bench**: region 0 `0x02C1DDC2` (no flash all session), region 1
`20a5edd3…` (re-hashed live at the wrap-up: `dd` 64 MiB at raw
`0x29000000`), hd3 the session-71 rebake, `BOOTCTR_FCTRY 0x21` read fresh at
the wrap-up (`pbrpmb: topup: … = 21000000`, above threshold, no write; the
session's ~10 boots each spent one unit and the top-up fired during the
campaign — self-maintaining as designed).  Live firmware `kick15pds`
(`24322c2a…`, instrument: kick10 flags + UK_WAITLOG + the four fixes),
preserved: `.kick14otpm/.kick13inv/.kick12inv/.kick11wl/.kick10live` and
`.working` = kick10.  Live module `1a2949ab…` (render wiring + the extended
status dump), `.pre-render` = `10c6f435…` (= `.working`, the revert) and
`.predumpregs` = `85fe59a2…`.  `/tmp/powervr_sgx_frame` (`efaefc61…`, with
the VDM-stream dump) + `/tmp/powervr_sgx_test` on the ext4.  `wlan0`
re-associated between boots (the GPU resets take it down); usb0 up.

## Previous state — 2026-11-27 (session 78)

**THE OPEN MICROKERNEL TAKES AND COMPLETES JOBS.**  `null job signalled in
946 µs`, `600 null jobs in 91 ms`, **zero GPU resets** for the whole uAPI
suite, the CCB ring draining (`issued 615 consumed 615`), host interrupts
delivered, the tick counter and entry counters climbing continuously.  **The
fix was ONE BIT: `EVENT_PDS_INFO` (0x0a70) had `USE_PIPELINE = TOGGLE`
(round-robin between the two USE pipes); the vendor's derivation is
`USE_PIPELINE_0`.**  Under round-robin the event tasks alternated pipes and a
stuck pipe blocked the toggle forever — the one-dispatch-then-stuck
signature.  Report **281**; the full campaign (16 boots) is in the capture
`sgx540-kick-entry-counter-2026-11-27.txt`.

**How the one bit was found** (every instrument proven first — DECISIONS
§116): the tick counter needed a **GPU-side seed** (the GPU's initial view of
the control-block tail is not the CPU's zeros — the old counter read
0xffffffff and wrapped to 0; trap 409); "4 fetches" was **double counting**
(trap 410); the negatives were re-run correctly (DMS_AGE no effect; the emit
with the vendor's operand derivation — session 76's negative was garbage
operands; `UK_NO_PHAS` — the vendor's image has no `phas`; the DOUTU
task-control words fixed to `MODE_PERINSTANCE` — kept, no effect alone); the
launch-script diff was completed and **`CACHE_CTRL = 0x0000ffff` — the
vendor's own value — breaks event dispatch entirely** (bisected; trap 411;
not written).

**What still fails**: 30/50 on the uAPI suite — the failures are SGX545-shaped
test expectations (core_id 0x0115, va_bits 32, heap base 0x3e000000; our core
is 0x0114 / 28-bit), so `vm_map` at 32-bit addresses correctly refuses.  The
suite needs its addresses parametrized by `va_bits`.  No render/transfer job
submitted yet — the TA/3D register tables (hypothesis) are the next mountain
toward an actual frame.

**Bench**: region 0 `0x02C1DDC2` (no flash all session), region 1
`20a5edd3…`, hd3 the session-71 rebake.  `BOOTCTR_FCTRY 0x28` (the top-up
fired during the heavy rebooting — self-maintaining).  **The working pair is
preserved AND live**: firmware `sgx540_ukernel.bin.working` = `kick10`
(`3fbbd11b…`), module `powervr_sgx.ko.working` = `10c6f435…`.  Preserved
beside them: `.kick2w/.fix1w/.tick2/.before-hostirq` firmware variants
(**`.kick8w`/`.kick9w` are not on the unit** — corrected session 79) and module
`.pre-doutu a2656c72 / .prelaunch 127a5fad / .pre-loopback be02c71f /
.dmsage be3d4b35 / .predump 801d08a5 / .pre-doorbell 94eaea61`
(hashes corrected session 79 against the unit's md5sums).  `card1` = `powervr_sgx` (`open microkernel
kick10`), desktop llvmpipe, `wlan0` **UP** (re-verified live, session 79: it
has a DHCP lease again — the report-281 "down" reading was mid-session), usb0 up.

## Previous state — 2026-11-27 (session 78, mid-session, before the USE_PIPELINE fix)

The pre-fix snapshot: the dispatch happened exactly once per microkernel life
(the launch's init pass, then the FIRST timer edge); the tick counter had just
been fixed with the GPU-side seed; the launch-script diff was complete with
CACHE_CTRL identified as load-bearing.  Full detail in the capture
`docs/captures/sgx540-kick-entry-counter-2026-11-27.txt` (boots 1-13) and
report 281.  Superseded by the current state above.

## Previous state — 2026-11-27 (session 77)

**HANDOFF §0b is answered, and broader than it asked: the kick entry is never
entered, and NO dispatch source works after the launch — the EDM fetched the
event program twice during launch and never again, for any trigger.**  The
re-instrumented kick counter (control-block word 0x3f, the `kick` entry's first
instruction, mechanically verified free) reads **0 across eight job submits
that each cost a GPU reset**; the timer's b29 stays latched while the entry
stays 2 and the tick counter stays 0; and the session-76 wrap-up's `relaunch`
node already showed re-writing `EVENT_KICK2` itself does not re-fetch.  Report
**280**; traps **406–408**; capture
`docs/captures/sgx540-kick-entry-counter-2026-11-27.txt`.

**Two instrument corrections fell out, both of the trap-403 family:**

* The `UK_ENTRYLOG` STATUS-snapshot store has **never worked** (in `fix1`
  too): it runs `limm r5, #0x39` — bytes — where its own comment says
  "word 0x39 → 0xe4", so the store lands misaligned and word 0x39 has read 0
  in every dump of every build (trap 406; found by disassembling the image).
* **"One init pass and one dispatch pass, and the dispatch pass is the
  timer's" is withdrawn in its second half — there is no dispatch pass.**  The
  `tick(0x2c) = 1` readings of sessions 75–76 are misparse candidates:
  control-block **word 0x2a reads 1** on every boot (writer still unidentified
  — nothing in the image stores there, the driver memsets the block), 0x2a is
  the 3rd word of dump row 6 while 0x2c is the 5th, and a word-index parse
  reads tick = 0 on every session-77 boot.

**The doorbell read-back is not evidence on this silicon (trap 407):** the new
`ccb_write()` sampling (8× after the write) and the launch-kick control both
read all-zero — even the provably-landing launch kick never shows `EVENT_KICK`
NOW in a read-back.  A self-clearing register needs a landing control.

**The complete dispatch-register dump (now in `status`) measured the never-
written ones:** `DMS_AGE = 0` (the vendor's launch writes
`ENABLE | PVRSRV_SGX_MAX_DMS_AGE`; the value is underivable from the DDK 1.8
drop), the **PIXEL event-PDS pair entirely 0** (`0x0a5c/0x0a60/0x0a64`),
`EVENT_PDS_ENABLE2 = 0`, `EVENT_KICK1/KICK3 = 0`.  The next candidates are the
launch-script registers the driver never writes — **`DMS_AGE` first** (a stuck,
never-completed launch task with aging disabled would block every later
dispatch, which is exactly the observed shape), then `PDS_DMS_CTRL2`/
`USE_G0/G1`/`USE_CTRL`/`USE_MOE`/`USE_CODE_BASE`.

**Bench at the close (boot 3, read on the unit):** region 0 **`0x02C1DDC2`**
(no flash this session), region 1 **`20a5edd3…`**, hd3 = the session-71 rebake
`5d3eac8f…` live as `/dev/loop0` ext4 rw.  **`BOOTCTR_FCTRY 0x23`** read fresh
on boot 1; boots 2–3 cost one unit each (0x22/0x21 expected — **not yet
re-read**; read `dmesg | grep pbrpmb` on the next boot).  Kernel `7.3.3`.
`card1`/`renderD129` = `powervr_sgx` running **`open microkernel kick2`**
(`12847879…`, the kick-entry instrument), `EDM init: event status
0x20000000, BIF int 0x8000`, `microkernel started`, `BIF_FAULT 0`;
`card0`/`renderD128` = `omapdrm`.  Module `powervr_sgx.ko` = **`ea10d582…`**
(the kick instrument ride-along: the `doorbell` node, `ccb_write`/launch-kick
sampling, and the six new `status` registers), with `.pre-doorbell`
(`94eaea61…`) and `.predump` (`801d08a5…`) preserved; firmware variants beside
the live file: `.fix1w` `f468a71d…`, `.kick2w` `12847879…`, `.tick2`
`7b44b33a…`, `.before-hostirq` `12f12abb…`.  Desktop llvmpipe; panel
`bl_power` 0; **`wlan0` DOWN** (the GPU resets take it over — the session-76
caution); `usb0` `169.254.0.1/16` up; `/tmp/powervr_sgx_test` on the ext4.

## Previous state — 2026-11-26 (session 76)

**The SGX interrupt reaches the CPU; five defects fixed; the entry counter**
**corrected** (word 0x1c was `CLEANUP_DEFER_CT`; on word 0x07 the program is**
**fetched twice).  Full snapshot moved to
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md); its
"dispatch dead after the launch" conclusion was superseded by report 281
(`EVENT_PDS_INFO`'s `USE_PIPELINE` bit).

## Previous state — 2026-11-26 (session 74)

**The open SGX540 microkernel started and a NULL job printed `1/1 passed`; a
running EDM was the open question.**  Superseded by session 75, which showed that
the `1/1 passed` was the driver's own 2 s timeout, and then by session 76, which
found that the interrupt had never reached the CPU and **corrected session 75's
"entered once and never again"** — the corrected counter reads two fetches.  Full
snapshots:
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
(`## Previous state — 2026-11-26 (session 74)` and `… (session 75)`); its report
is 277, with two figures withdrawn by 278 and one more by 279.

## Previous state — 2026-11-26 (session 73)

**The open SGX540 port reached hardware: the driver binds and identifies the
core, and the first microkernel builds.  The next blocker is the microkernel's
list-0 / PD-table path.**  Superseded by session 74, which gave it that path;
the full snapshot is in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
(`## Previous state — 2026-11-26 (session 73)`).

## Previous state — 2026-11-26 (session 72)

**The Kumo/WPE WebKit experiment ran and is a proven negative: the engine is on
the SGX, but the PowerVR GBM backend cannot export dmabufs, so the page never
composes.**  Full snapshot in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
(`## Previous state — 2026-11-26 (session 72)`).

## Previous state — 2026-11-25 (session 71)

Superseded; the full snapshot is in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
(`## Previous state — 2026-11-25 (session 71)`).

## Previous states

* **sessions 61-70** (2026-11-16 … 2026-11-24) — moved to
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md) at the
  session-79 wrap-up (session 61 the ring method and its withdrawal, 62-63
  the SMP bring-up and the fast chainloader, 64-66 the dual-core/MUSB
  campaign, 67 the mailbox fix and the CPU affinity find, 68-69 the GPU
  client path and the TILER/pitch fixes, 70 the desktop on the GPU).
* **session 60** (2026-11-16) — the `epitaph` panel fixed (r10005), the drawer,
  the llvmpipe swap latency, and the one-core 400 MHz state.  The full snapshot
  is in [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
  (`## Previous state — 2026-11-16 (session 60)`).
* **session 59** (2026-11-16) — the root became writable and grew to 56.5 G,
  SSH came back (kernel `#77`, region 0 `0x294F8E36`), the touch position ghost
  was fixed (kernel `#74`), the eMMC cache-flush item was closed by measurement,
  and wvkbd fixed the OSK repeat.  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
  (`## Previous state — 2026-11-16 (session 59)`).
* **session 58** (2026-11-16) — the root became writable and grew to 56.5 G,
  SSH came back (kernel `#77`, region 0 `0x294F8E36`), the touch position ghost
  was fixed (kernel `#74`); eMMC flush first isolated.  Full snapshot at git
  commit `00228e4` and `docs/sessions/session-58.md`.
* **session 57** (2026-11-16) — apps/background, the touch release fix (kernel
  `#73`), the audio diagnostics gated and the idle suspend removed; a position
  ghost remained.  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
  (`## Previous state — 2026-11-16 (session 57)`).
* **session 56** (2026-11-15) — catacomb runs on the SGX540 (the smithay GLES2
  fallback), the Munin Y axis is fixed, and the SGX driver is vendored; region
  0 `0xAB899C49`, hd3 `89399744…`.  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
  (`## Session 56 snapshot`).
* **session 55** (2026-11-15) — the read-only hd3 + overlayfs
  Nura root booted (region 0 `0xA8F0F30C`, hd3 `1fcf0cb0…`), the eMMC stall
  left the boot path, and the SGX540 kernel driver bound
  (`pvrsrvkm 56000000.gpu`) with the pvrports userspace running EGL/GLES 2.0
  (`PowerVR SGX 540`).  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md).
* **session 54** (2026-11-15) — pmbootstrap built the Nura root, hd3 was flashed
  at `0x4915` and Nura booted, but the eMMC write stall remounted the root
  read-only; the read-only overlay root was implemented and built (reports
  240–242).  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md).
* **session 53** (2026-11-15) — the WL1283 BT core works (a full BR/EDR
  inquiry; the reference's `WL1283.bts` replays 315/315 and makes `hci0`
  register with mgmt), and item 7a (hd3/Nura/SGX) was scoped.  The full
  snapshot is in [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
  (`## Session 53 snapshot`).

The per-session snapshots are in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md):

* **session 52** (2026-11-15) — the WL1283 WLAN associates (the `ccm`/`ctr`
  modules), the USB link gets its DMA, and the BT core answers HCI (the UART2
  CTS/RTS pads); reports 236/237.
* **session 51** (2026-11-14) — the WL1283 WLAN works: the `-110` loop was the
  chip's ELP sleep (now CAM) and the never-completing scan was the 5-sr
  firmware (4-sr scans); reports 232–235.
* **session 50** (2026-11-14) — the WL1283 enumerates (`CLK32KG`) and firmware
  boots; the interface recovery-looped (reports 230/231).
* **session 49** (2026-11-14) — the blocker measured from both sides, the host
  program excluded, the "silent card" instrument corrected (reports 216–227).
* **session 48 and older** — see the same file.

## Per-subsystem state

| subsystem | state | detail / evidence |
|---|---|---|
| Chainloader image (CRC path) | **working** | 828 B loader at image `+0x800` = `0x80100000`; copies zImage/DTB high, checks the DTB magic `0xEDFE0DD0`, jumps (`r0=0`, `r1=~0`, `r2=DTB`). Report 109 §8.1, 103 §5.2 |
| DRAM / memory map | **working** | 1 GB at `0x80000000`; kernel runs from `0x84000000`-loaded zImage, decompressed to `0x80008000`; DTS comment |
| Kernel self-capture | **working** | DRAM ring at PA `0x88000000` (32 KB) + breadcrumb/event log; `tools/pb_ring.py`, `tools/pb_events.py`. Bootstrap §3 |
| WDT2 | **working — 58.6 s window, 10 s pet (session 31)** | The loader arms `WLDR = 0xFFFC1800` (7.8125 s); the kernel reprograms it to the reference's `0xFFE2B400` (58.6 s) at the late initcall via `WSPR` (`0xAAAA`→`0x5555` stop, write `WLDR`, `0xBBBB`→`0x4444` start), **polling `WWPS`** after each write, and pets every 10 s (`TGR` read→invert→write); fallback 2 s if the readback fails.  Verified on `0xE36C679A` by `WCRR` reload timing (~10 s) and uptime >275 s.  The expected dmesg line is missing on 7.3.3 (printk anomaly).  Report 109 §8.1 + correction; trap 115; decision §38 |
| eMMC (MMC2) | **reads proven; writes healthy — but writers must use large blocks** | `mmcblk1` = MCGAFA 59.4 GiB, `boot0`/`boot1`/`rpmb`; needs the `vemmc` fixed supply for `ocr_avail` (report 109 §9).  Session 16 read a freshly flashed 11 152 064-byte image back **byte-exactly** and matched a pre-flash TabletOS capture (report 134); the MMC2 functional clock is 96 MHz.  Session 17 measured the write path (report 136): the driver has no request-size limit — an O_DIRECT write gives **256 KiB `CMD25`s, 5.5–7.0 MB/s** (4 KiB chunks: 0.93 MB/s), one `CMD13` per request (normal `mmc_blk_card_busy`).  The old 17–32 KB/s was `recv`'s tiny buffered writes plus 4–7 unthrottled `printk`s per request to **fbcon**.  Use ≥64 KiB (ideally 256 KiB–1 MiB) writes with the MMC trace off.  **Session 18: region 1 now holds a bootable Linux root** (32 MiB ext4 at raw `0x29000000`), written through the pre-OS `0xF7` loop (2055 frames) and read back byte-exactly (QNX `dd` and Linux `blk sumb`); it mounts read-only via a loop and its `/init` runs (report 140).  **Session 54 (report 241): the hd3 writable root stalls this path.**  The first ext4 journal/udev write after the 597 MiB bootrom image gives `mmc1: Card stuck being busy! __mmc_poll_for_busy`; the loop write fails, the journal aborts, and ext4 remounts `emergency_ro`.  A later 4 KiB write to scratch hd3 returns `I/O error`; reads are fine.  The region-1 root is **no journal** and stable.  Workaround: read-only hd3 + overlayfs (`CONFIG_OVERLAY_FS=y`); **session 55 (report 243) confirmed it: the overlay boot has no `Card stuck`, no journal abort, the lower `ro without journal` and all writes in the tmpfs upper.**  The eMMC write-latency/cache fix is a separate experiment. |
| USB device + gadget | **working — DMA now actually used (session 52)** | enumerates `0fca:8010`: CDC-ACM (console) + **RNDIS** (network) in one configuration; High Speed (480 Mbps).  **Session 52 (report 236 §7): the RNDIS path had been running MUSB's PIO fallback** — `dma_channel_program()` refuses buffers that are not 4-byte aligned on RTL 2.00 (`MUSB_HWVERS = 0x0800`), and u_ether hands it 2-mod-4 addresses.  Fixed with `musb->g.quirk_avoids_skb_reserve = 1` (RX) and a TX realign in `gether_start_xmit()`: **RX 3.3 → 6.3 MB/s, TX 3.3 → 4.1 MB/s** (raw `nc`).  The RNDIS function's one-frame-per-transfer structure is now the limit; CDC-NCM is the untested next step.  The ACM console is still only ~134 KB/s and **drops bytes when the host blasts** (report 136 §3.3) — do not send multi-MB images through it.  Reports 114, 136, 236 |
| **WL1283 (MMC5)** | **WORKING — associates, leases and passes traffic (session 52); scans (session 51)** | **Session 52 (report 236): the association blocker was the missing `ccm`/`ctr` crypto modules.**  `ieee80211_key_alloc()` allocates a `ccm(aes)` AEAD for every CCMP key; without it the PTK install returns `-ENOENT` and wpa_supplicant mislabels it `WRONG_KEY` (the handshake itself is fine — message 3 decrypts).  `ccm.ko`/`ctr.ko` ship in region 1 and `rcS` modprobes them before the stack: `wpa_state=COMPLETED`, WPA2-CCMP on 5785 MHz at −41 dBm, MCS7/65 Mbit/s, DHCP **192.168.0.212**, gateway and 1.1.1.1 reachable.  A 5 GHz AP needs `iw reg set <CC>` (the world domain marks 5735–5835 MHz `PASSIVE-SCAN`).  The **per-unit NVS** is shipped (`wl128x-nvs-perunit.bin`).  **Session 51 (report 232): the recovery loop was the firmware's ELP sleep.**  The failing transfer is the **FW-status read** — CMD53 to `0x1efc0`, 84 bytes, byte mode, 256-byte block — from the WL_IRQ thread right after `firmware booted`; it fails only once the chip has slept, and the identical read succeeds when it is awake.  Fix: `wl12xx_default_conf.conn.sta_sleep_auth = WL1271_PSM_CAM` (was `ILLEGAL` → ELP).  Verified after a real region-1 flash + reboot: one firmware boot, zero failures, `wlan0` `UP`, debugfs `sleep_auth` 0.  `wl128x-fw-4-sr.bin` is the **shipped** SR firmware (report 235): 5-sr (7.3.10.0.142) boots but panics on the first scan, so `wl12xx`'s floor is lowered to 7.3.5.0 and `WL128X_FW_NAME_SINGLE` names the 4-sr file; both files stay installed.  **WLAN (report 231):** `wlcore`/`wl12xx`/`mac80211`/`cfg80211` + `wl128x-nvs.bin`, the `ti,wl1283` child (`reg = <2>`, WL_IRQ gpio 53), `cap-power-off-card`, and an `mmc-pwrseq-simple` pulsing `WL_EN` (low → 50 ms → high → 120 ms) → firmware `Rev 7.3.5.0.98`, MAC `14:74:11:8f:9c:c3` (the assigned address, patched into the generic NVS; the radio calibration is still default).  **Enumeration (report 230):** the TWL6030 `CLK32KG` 32.768 kHz SLOW_CLK is physical `0xBC`–`0xBE` and was OFF; `CONFIG_CLK_TWL=y` + an enable in `clk-twl`'s probe (~1.9 s, before MMC5 at ~3.0 s) fixes it.  **Remaining (report 235):** the WLAN **associates** and passes traffic (session 52); open and not blocking use are *why* the 5-sr firmware panics on scan (235 §3) and *why* the ELP wake is not honoured (232 §6).  **Session-49/50 negatives (do not repeat):** the reference's MMCHS5 program and WL_EN ordering replayed, the stock-TabletOS diff, the boot-path RE, the clock sweep (reports 217/218/221–229).  The BT half's enable is `BT_EN` = gpio 55 = `GPIO2_23`, held low while the WLAN works (report 216). |
| **WL1283 BT** | **the CORE WORKS — real inquiry; `hci0` now registers with mgmt (session 53)** | UART2 `0x4806C000` = `/dev/ttyS1`, H4 at 115200 8N1; `BT_EN` = gpio 55 = `GPIO2_23` (pad `0x4A10007C` mux 3), pulsed low → 50 ms → high.  **Measured (report 238):** the core answers `HCI_Reset`, reports `hci ver 6` / `lmp ver 6` / `mfr 0x002b` (TI, BT 4.0), and **runs a full BR/EDR inquiry that finds a peer** (`f0:a3:b2:bb:d1:00`, class `0x011c00`), reproducibly.  **The reference's `WL1283.bts` loads 315/315** and moves LMP subversion `0x2b0f` → `0x2b33`; loaded **before** the attach it makes `hci0` **register with mgmt** (0 → 1 controller, negative control both ways) by fixing the `0x201c` reply length.  **Left:** opening the controller — a stray `0x30` byte arrives ~200 ms after the last init reply and the kernel's H4 receiver correctly rejects it as not-an-H4-type (`-EILSEQ` → `Frame reassembly failed (-84)` → `SET_POWERED` `0x05`); the instrumented `h4_recv()` shows it in 6 of 6 runs, but **which side emits it is open** (it never appears from userspace).  Then ship `l2cap`/`sco`/`rfcomm`/`hidp` (not in region 1 yet) and a stack.  **LE is refused by the controller**, though the chip reports BT 4.0 with the LE-supported bit — no known sequence, and impossibility not demonstrated.  The pack survives `HCI_Reset` but not a `BT_EN` power cycle.  Tools in `rootfs/bt/`.  Reports 216/237/238, traps 248–252, DECISIONS §72/§73 |
| **GPIO1 pads / board lines** | **fixed (session 49): three pads were driving GPMC alternate functions** | `0x4A100050` (`gpmc_ad0/1`) and `0x4A100074` (`gpmc_ncs0/1`) were left at mode 0, so pad logic drove GPMC bus lines on a board with no GPMC device while gpiolib could not touch them (trap 228); `0x4A31E058` (GPIO1_31) was unmuxed with its input buffer off.  The DTS now sets mode 3 + pull-down and hogs GPIO1 0/18/28/31 low, so `GPIO1_OE = 0x6FFB1FBA` against the reference's `0x6FFB1FAA` (the missing bit 4 is pad `0x058`'s low half = GPIO2_4, owned by `bridge_pins`, trap 229) and `GPIO1_DATAOUT` is identical.  Image `0x0114FFD3`.  **Not** the WL1283 blocker (card still silent).  Report 220 |
| **Munin touch (CY8CTMA3)** | **FIXED — the position ghost and the lost burst taps are gone (session 58, kernel `#74`)** | I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` boots the app and maps the 12-bit report through the 1917 `scaling.conf` calib (raw x 172→1108 → 102→922, y 110→658 → 60→540) to 1024×600; IRQ gpio 35 = GPIO2_3 pad `0x056` = `0x410B`, edge-falling, `irq 110` (~174/s idle).  The controller asserts its line for the whole gesture and reuses slots, so the driver re-reads a torn report until the counter parity matches, **keeps polling while the line is asserted (and for 200 ms after the last contact)**, and **holds a new down back one sample when it repeats the slot's previous coordinates within 300 ms**; `irq-gpios` gives it the line level (traps 296/298).  Contacts are separate and fast typing is correct (report 248).  The backspace/enter repeat was squeekboard's, now fixed by wvkbd (trap 299); power/reset GPIO1_14/GPIO4_8 are not driven.  `pb-touchwake.py` wakes a manually blanked panel; auto-blank removed (session 59 — DPMS resume wedges DSI).  Reports 198/247/248/250, DECISIONS §47/§82/§83/§85 |
| **WM8994 audio** | **WORKING — stereo 440 Hz on headphones AND speakers at 48 kHz; diagnostics `dev_dbg` (session 57)** | I²C4 `0x1a` (`0x8994`, revision D); the board machine driver makes `0 [WM8994]: PlayBook_WM8994`.  **Data path (session 46):** the codec's AIF1 is on McBSP3 (the oracle's routing log `AIF 0 Downlink: MM_DL_PORT->MM_EXT_OUT_PORT->MCBSP3`, the live `PCR0 = 0x3`, the PDM pads `0x106/0x108/0x10a/0x10c` = `0x0109/0x0009/0x0109/0x0109`); the card is two ASoC links (`40124000.mcbsp + wm8994-aif1` clocks, `CBC_CFC`; `40126000.mcbsp + wm8994-aif2` data, `CBP_CFP`); the codec is the AIF1 slave, FLL1 locks from our BCLK (`R0x302 = 0x0000`, `R0x732 = 0x6020`, `R0x200 = 0x0011`), no MCLK1; the `omap-mcbsp` channel-layout guard keeps `XCR2 = 0x8041`.  **Speaker (session 47, report 215):** the speakers are on the codec's **line outputs**, so the output stage follows the jack: headphones → HPOUT1 + charge pump (`R0x060 = 0xcc`); jack out → LINEOUT1P/2P (PM3 `0x3cf0`, `R0x01e = 0`, `R0x034/0x035 = 1`, `R0x036 = 0`, `R0x038 = 0x80`, `R0x04c = 0x1f25`, `R0x060 = 0`) with **`spk` (GPIO2_12) driven low** and **GPIO1_15 (the `audio_boost` enable) high**.  **Session 48** removed the `pcm:` ring readback and the `live N:` FIFO dump and re-verified both outputs at DAC1 volume 40: `jack out (speaker)` / `jack in (headphone)`, "done, FLL1 locked", both PCMs `RUNNING`, the full byte count written, `spk` low during the speaker route and high on the headphone route, `boost` high.  Reports 214/215, DECISIONS §59/§60.  **Session 57:** the remaining info-level diagnostics (`omap_mcbsp_dump_reg`, the per-stream `rate/AIF1CLK` and `repair:` lines) are `dev_dbg`, and the PM-QoS remove is guarded, so a failed 48 kHz `hw_params` no longer taints the kernel (trap 294); the 48 kHz rejection itself is open. |
| Userspace (initramfs) | **working, minimal** | `/init` = PID 1, mounts, creates the configfs gadget (**ACM console + RNDIS**, session 48), forks `/console` in the fallback path.  **No auto-reboot since session 15** — PID 1 reboots only on request (the timer spent a `BOOTCTR_FCTRY` unit per iteration).  **Session 28:** after the gadget and the boot-time top-up, `/init` mounts the region-1 ext4 root **read-write**, bind-mounts /dev//proc//sys, chroots and execs `/sbin/init`; with no `/sbin/init` it falls back to the console.  Report 176, session-28 note |
| **USB network + ssh** | **working — commands and interactive sessions (session 48); 2.3 MB/s is the ssh bound, not the USB one** | The gadget carries an **RNDIS** function next to ACM, so the host sees four interfaces and a net device (`enp8s0f3u1i2`, MAC `02:1a:11:00:00:02`); the region-1 `rcS` brings `usb0` up at **`169.254.0.1/16`** and starts **dropbear** listening on `169.254.0.1:22` only.  Key-only auth, host keys shipped, password/PAM/shadow off.  **`tools/pbssh.sh 'cmd'` works**; measured before session 52 at 2.3 MB/s, which is dropbear's cost — the raw path is **6.3 MB/s (host→device) / 4.1 (device→host)** after the session-52 DMA fix, so use raw `nc`/`socat` for bulk copies and ssh for command access.  An **interactive** session works under a real terminal (`script -qec` from this side); an agent's own shell has no tty, so an apparent "closes with no output" is a harness artefact (trap 216).  DECISIONS §62. |
| Userspace (region-1 root) | **working — a real BusyBox userland with ssh, the WLAN stack and the BT stack** | 64 MiB ext4 at raw `0x29000000`; BusyBox 1.36.1 + init + 401 applet links + `pbconsole`/`pbrpmb`/`pbplay`/`pbmix` + `/lib/modules/7.3.3`; boots to `sh -l` on `/dev/ttyGS0` and serves **dropbear on `usb0`**; `insmod`/`modprobe` proven.  Current image: `local/images/rootfs-region1-wl1283-bt.ext4` (sha256 `49e318bf…`, **43 modules** — the sensor/charger/audio set, the wireless stack with **`ccm`/`ctr`**, the seven Bluetooth modules, both wl128x SR firmware files, the per-unit NVS, `iw`, `wpa_*` and the signed `regulatory.db`, session 52).  Earlier: `…-wl1283-final.ext4` (`877c42c3…`, 36 modules), `…-wl1283-assoc.ext4` (`6c2cfb30…`), `…-wl1283-scan.ext4` (`f159a221…`), `…-rndis-ssh.ext4` (`a4e76b18…`) and the plain userland `0xAEDF68D0`. |
| Console | **working** | `help ls cat peek poke kick disp bl i2c i2c16 spi fb kmsg grep blk recv sink blkwr qnx6 rootfs write reboot`; `spi tcon` (low-first) and `spi tconbe` (high-first); `blk info/sum/sumb/rd` is read-only FNV-1a64 over `/dev/mmcblk1` (**`blk info` prints size 0 B — `fstat` bug, see KNOWN-ISSUES**), `grep <substr> <path>` scans any-size text (for `clk_summary`); `sink <len>` measures the link, `blkwr <off> <len> [chunk]` is a timed O_DIRECT write with a deterministic pattern (report 136); `qnx6 [off-hex]` mounts hd3 read-only (report 138); `rootfs [off-hex]` mounts the region-1 ext4 root read-only on `/rootfs` (report 140); argument limit 24.  `pb_reg` op set is now `w a:v` / `r a` / `t a` (16 reads, re-arm) / `u a` (16 reads, no re-arm) / `d from:to` (range dump) / `v` (video liveness).  Every hardware access forks with a bounded wait |
| QNX6 read access (hd3) | **working, read-only** | `CONFIG_QNX6FS_FS=y` + console `qnx6 [off-hex]`: a read-only loop over `/dev/mmcblk1` at raw **`0x49000000`** (superblock `0x49002000`) mounted qnx6 on `/mnt`.  Verified on `0xFC77CD47`; the old `0x48F00000` offset gives `wrong signature (magic)` from the driver.  Report 138 |
| **SGX540 / PowerVR** | **WORKING — catacomb composites on the GPU (session 56)** | The `openpvrsgx-devgroup` 7.3 driver (DDK 1.17.4948957) is in-tree and bound (`pvrsrvkm 56000000.gpu`); `catacomb` 1.5.0 runs with a smithay 0.7.0 GLES2 fallback (`GL_EXT_unpack_subimage` absent → per-row/full-row uploads) and maps `libGLESv2_PVR_MESA`/`libsrv_um`/`libusc`/`pvr_dri.so` (no `swrast`); `squeekboard` composites.  The OSK key-press path still janks (open).  The driver is vendored at `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz`.  Report 246. |
| DSS / DISPC via omapdrm | **working — and the core clock is now right** | `/dev/dri`, `/dev/fb0`, `card0-LVDS-1` connected+enabled, `LINE_STATUS` advancing (37 lines/ms), GFX overlay scanning the painted buffer.  The DSS functional clock, which is the DISPC's **core** clock, was left at 48 MHz by the boot chain while the pixel clock is 50.4 MHz — that was the last display defect.  `dss_probe()` now asks the PER PLL for the reference's rate and gets **96 MHz**.  **Session 18:** the boot-time `pb` bridge-read probes are behind `pb_probe` (default **off**), so a normal boot has **no DSI CIO errors**; the DSS rate-mismatch WARN is gone and the real rate is recorded (report 142).  Report 133 §2/§3 |
| DSI1 link | **byte-identical to the reference, PLL included** | `DSI_CTRL = 0x00EAEF9D`, `CLK_CTRL = 0xA0346005`, `TIMING1 = 0x79190999`, `TIMING2 = 0x7FFF7FFF`, `VM_TIMING1/2/3`, `CLK_TIMING = 0x00000E0B`, `VM_TIMING7/8`, PHY, PLL `CFG1 = 0x14A5E8BE` / `CFG2 = 0x00056000`.  All re-read live in session 15. Reports 127 §4, 132 §6 |
| TC358764 bridge | **programmed; configuration now identical to a working system** | The whole 353-register map was read from **TabletOS with its display working** (`tools/pbdsi`) and diffed against ours.  After fixing `VP_HTIM1/2` (§ below) only four registers differ, three of them live status/counters.  `IDREG = 0x00006500`, `LV_CFG = 1`, `LV_PHY0 = 0x0004C02D`, `VP_HTIM1 = 0x00780001`, `VP_HTIM2 = 0x00760400`, `VP_VTIM1/2`, `VFUEN` self-clears. Report 132 §2/§5/§6 |
| Bridge **reads** | **reliable — with `PPI_TX_RX_TA = 0x00040006`** | The value is latched by `PPI_STARTPPI (0x0104)`, so it must be right **in `pb_bridge_seq`**.  Boot with `4` → 4/8 at boot, 8/16 sampled; boot with `0x00040006` → 8/8 and 16/16.  Live writes after boot change nothing.  The VC re-arm does not affect the rate. Report 132 §3 |
| Backlight (LM3532 @ `0x38`) | **working** | binds, `bl_power = 0`, brightness 255; gate is `GPIO2_16`. Report 120 |
| Panel TCON (MCSPI4) | **working — sent automatically at bring-up** | The 9-word Hitachi table is sent by `/init` right after the display comes up (high byte first, 450 kHz, 100 ms after words 0/2/last), matching the reference's own order (`winch_lcdctl 0x193c4`).  Without it the panel stays black even with the bridge programmed. Report 133 §3/§7 |
| Panel identity | **Hitachi (definitive)** | on TabletOS: `GPIO6_DATAIN = 0x00010800` (bit 16 = 1), slog `LCD is Hitachi`.  Report 127 §1 |
| **Image on the panel** | **WORKING** | With `0x8C146805` (and the same source as `0x068C867F`) the panel shows a clean, stable image: solid white, solid red, and the kernel console as legible text.  The three necessary fixes were the bridge delivery (VC0/HS null packet), the LVDS porches + TCON table at bring-up, and the DISPC core clock. Report 133 |
| QNX oracle tool | **`tools/qnx-dsiread.c` → `tools/pbdsi`** | Reads any TC358764 register over DSI from a TabletOS user process (generic read request `0x24` on hw VC1, BTA, RX FIFO pop; response is a generic long read, DT `0x1A`).  Read-only.  This is how the reference values in this document were obtained. Report 132 §2 |
| Kernel patch + config | **7.3.3 (session 67 export)** | `kernel/patches/0001-playbook-winchester-7.3.3.patch` is **9 136 lines / 67 files**, sha256 `d19adff8…`, 0 rejects / 67/67 identical against pristine 7.3.3.  Session 67 adds `drivers/usb/phy/phy-twl6030-usb.c` + `drivers/phy/ti/phy-omap-usb2.c` (the comparator lifetime fix) and restores the **stock `omap_musb_set_mailbox()` call** in `omap2430.c` (mutex kept); the board DTS moves to `panic=0`.  Session 65 added `drivers/usb/musb/omap2430.c` (device-mode-only enable + `set_mailbox_lock`), the DSI-init TCON hook in `dsi.c`, and the board DTS without `maxcpus`; the session-62/63/64 markers, the `head.S` `CRR` stamp and the `MINI4x6` font link are not in it.  Session 58 fixes the Munin touch position ghost (`playbook-munin.c`: poll while the line is asserted + the fresh-down confirm; the board DTS gains `irq-gpios`; report 248).  Session 54 makes the `PB*` prints `pr_debug` (config `CONFIG_DYNAMIC_DEBUG=y`, `CONFIG_OVERLAY_FS=y`); the rebuilt image is `0xE84693A1` (built, not flashed).  Session 52 adds `drivers/usb/musb/musb_gadget.c` (the `quirk_avoids_skb_reserve` flag) and `drivers/usb/gadget/function/u_ether.c` (the TX realign); the 40 MHz experiment in `wl12xx/main.c` was reverted and is not in the patch.  Session 51 added `wl12xx/main.c` (CAM + the 4-sr firmware name) and `wl12xx.h` (the SR floor 7.3.5.0).  Session 49 added the report-220 board-DTS GPIO1 pad fix and `arch/arm/Kconfig.debug` (missing from every export since session 30 — the tree walk found it, trap 211 class).  Session 48 retires the two diagnostic blocks and adds `sound/soc/ti/omap-mcbsp-priv.h`, which every export since session 41 had omitted although `omap-mcbsp.c` reads the fields it declares (trap 211 — the patch could not compile from pristine until now).  Session 47 added the speaker path (line-output route, `spk`/`boost` GPIOs, the `disp-en-12-hog` removal); session 46 the `&l4_abe` ABE-refclk assignment, the E5 clock-role change, the McBSP3 two-link card and the `omap-mcbsp` channel-layout guard; session 39 the board machine driver (`sound/soc/ti/playbook-wm8994.c`, Kconfig/Makefile), the `wm8994-core.c` runtime-PM quirk and the board `sound` node/codec clocks, and it restored `drivers/input/touchscreen/{Kconfig,Makefile}`, which the 48-file export had silently omitted (trap 154).  Config: `kernel/winchester-7.3.3.config` (SMP, `CONFIG_NR_CPUS=2`, no `ARCH_MULTI_V6`; includes `CONFIG_CLK_TWL=y`, `CONFIG_CHARGER_ISL9519=y`, `CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`, `CONFIG_KERNEL_XZ=y`, `CONFIG_SND_PCM_OSS=m`, `CONFIG_SND_MIXER_OSS=m`).  The 6.15.11 patch (`5 703 lines / 34 files`) stays as the bisect reference; it is **incomplete** — it omits `cm3217.c`, `leds-fan5702.c`, `pbmon-pair.h`, `pbmon-page.h` and their Kconfig/Makefile lines (trap 106) |
| **ISL9519 charger** | **WORKING under Linux (session 31)** | I²C1 `0x48070000`, slave `0x09`; VendorID `0x0049`, DeviceID `0x0003`.  `isl9519.c` (built-in, DTS `charger@9`) sets the four registers in RIM's order with the masked fields and refreshes every 5 s.  Measured **+358 → +397 mA**, 3.775 → 3.794 V, capacity 8 → 9 %, `Charging`; still on at 197 s (watchdog).  Registers read back `0x14=0x0280`, `0x15=0x1070`, `0x3E=0x0D00`, `0x3F=0x0180`.  Report 183 |
| TWL6030 BCI | **probes after the session-31 fix; NOT the board's charger** | `twl6030_charger` probes with the `simple-battery` profile and sets `CONTROLLER_CTRL1=0x10`, `VOREG=0x21`, `VICHRG=0x08`, `CINLIMIT=0x09`, but the stock OS leaves the whole block at bootloader reset values — it charges through the ISL9519.  The BCI node/modules are inert; `twl6030_usb` reports `ONLINE=1`, VBUS 4.81–4.91 V |

## Current images

The unit table below is authoritative for what is on media.  The durable record
is the CRC and what it demonstrated, **not** the file.  **Retention rule
(operator, session 70):** keep the **latest dual-core image**, the **last working
single-core image**, the **read-back instrument**, and the **freshest hd3
filesystem** — everything else is rebuildable from the recorded CRC and is
deleted rather than accumulated.  All QNX assets (`*.ifs`, `*.rcfs`, `*.ufs`) are
kept regardless: they are the way back.  `LOCAL-PAYLOAD-INVENTORY.md` records
what was deleted and what remains.

**Only five kernel images are retained** (session-70 cleanup): the current
dual-core `0x3547D613` (in `boot/`, tracked by name), the single-core
`0x8B83421F`, the read-back instrument `0x5BBCD22E`, and the two fallbacks
`0xC9FEE2D1` / `0xC675E206`.  **The table
below is a historical record of what each image demonstrated — most of those
files are no longer on disk.**  A CRC is the durable artefact; the file was
rebuildable and was deleted (`LOCAL-PAYLOAD-INVENTORY.md` §7).

| image | base | role |
|---|---|---|
| `0x3547D613` | 7.3.3 | **current region 0 — ARMv7 dual-core, stock MUSB mailbox** (session 67: 10,448,868 B, `#109 SMP`, `-march=armv7-a`, no `maxcpus`, inert `console=ttyGS0`, **comparator lifetime fix**, `panic=0`; 3/3 clean dual-core boots, gadget bound) |
| `0xC9FEE2D1` | 7.3.3 | session-65 region 0 — `0x9F74BC` B, `#105 SMP`, device-mode-only mailbox workaround, `panic=10` (known-good fallback; 6/6 boots, 376 s soak) |
| `0x158D8A85` | 7.3.3 | session-65 ARMv7 base (`0x9F74A8` B, same kernel/DTB without the console bootarg; 6/6 clean boots) |
| `0x19E24D8F` | 7.3.3 | **rejected** session-65 mutex experiment — stock `omap_musb_set_mailbox` with a glue mutex still panics on dual-core (`panic=10` loop) |
| `0xBEF06B78` | 7.3.3 | session-65 mutex build that booted single-core (DTB still had `maxcpus=1`) |
| `0xC675E206` | 7.3.3 | session-64 dual-core image — the device-mode-only mailbox fix (known-good fallback; used to recover the panic loop) |
| `0x294F8E36` | 7.3.3 | earlier region 0 — read-write root + SSH + grown FS (session 58: `0x9E0980` B, `boot/chainloader/kernel.img` sha256 `f0738625…`, kernel `#77`: mounts hd3 rw, installs dropbear, `barrier=0` + write-through, one-shot `resize2fs`) |
| `0xED58EF36` | 7.3.3 | previous region 0 — the touch position-ghost fix (session 58: `0x967CE0` B, sha256 `c67b75c5…`, kernel `#74`) |
| `0xB9DD91E3` | 7.3.3 | previous region 0 — the touch poll-on-touch fix (session 57: `0x967398` B, `boot/chainloader/kernel.img` sha256 `5ac47496…`, kernel `#73`) |
| `0x23D88CBD` | 7.3.3 | session-57 experiment — both-edge IRQ (no interrupts, touch dead; trap 297; `0x967800` B) |
| `0x373674A4` | 7.3.3 | session-57 experiment — the `PBTS` trace kernel (`0x967AA8` B) |
| `0xA544A731` | 7.3.3 | session-57 first touch fix — the parity re-read (`0x967CB0` B) |
| `0xAB899C49` | 7.3.3 | previous region 0 — the overlay init with `&sgx_module` and the Munin Y fix (session 56: `0x9676A0` B, sha256 `c37ee2fe…`, kernel `#69`; `local/images/kernel-0xAB899C49-touch.img`) |
| `0xA8F0F30C` | 7.3.3 | previous region 0 — the overlay init with `&sgx_module` (session 55: `0x967880` B, sha256 `fb9905f3…`, kernel `#68`; `local/images/kernel-0xA8F0F30C-sgx.img`) |
| `0xE85217E5` | 7.3.3 | previous region 0 — the overlay init without SGX (session 55: `0x9662A4` B, sha256 `fbeb6df0…`; kernel `#67`; `local/images/kernel-0xE85217E5-overlay.img`) |
| `0xE84693A1` | 7.3.3 | **built, NOT flashed** (session 54: the PB prints `pr_debug`/dyndbg, `PBREQ`/`PBIRQ` behind `pb_trace`; `CONFIG_DYNAMIC_DEBUG=y`, `CONFIG_OVERLAY_FS=y`; `0x967A94` B, sha256 `663348d0…`; `local/images/kernel-0xE84693A1-quietpb.img`) |
| `0x060B25F2` | 7.3.3 | previous region 0 (session 54: `ROOTFS_RAW_OFF=0x49000000`, the `lstat` hand-off, read-write root; `0x939D04` B, sha256 `b548ec6c…`) |
| `0x8B709950` | 7.3.3 | session-54 intermediate (`ROOTFS_RAW_OFF=0x49000000` with the old `access()` hand-off; `0x939D5C` B, sha256 `835fa863…`) |
| `0xDD66D8C3` (readback) | 7.3.3 | the session-52 image, read back before the hd3 switch and CRC-verified (`local/images/kernel-0xDD66D8C3-readback.img`, sha256 `06e61587…`) |
| `0xDD66D8C3` | 7.3.3 | session 52 (the UART2 **CTS/RTS pads** — the BT core answers HCI; `0x939E4C` B) — no longer current region 0 |
| `0x50CD2A48` | 7.3.3 | session-52 (the musb HSDMA fix — `quirk_avoids_skb_reserve` + the u_ether TX realign; `0x939E3C` B) |
| `0x5C37F181` | 7.3.3 | session-52 intermediate (the TX realign, with the one-shot `PBDMA` diagnostic still in) |
| `0x5A7F15D8` | 7.3.3 | session-52 intermediate (the RX quirk + the diagnostic) |
| `0x9712A086` | 7.3.3 | previous region 0 (session 50: the CLK32KG fix + the `ti,wl1283` child, `cap-power-off-card` and the pwrseq; kernel `#60`) |
| `0xF64A680D` | 7.3.3 | session-50 intermediate (`+cap-power-off-card`; the second firmware boot still failed) |
| `0x53F7F97B` | 7.3.3 | session-50 intermediate (`+ti,wl1283` child) |
| `0x69CDAC94` | 7.3.3 | session-50 first fix (`CONFIG_CLK_TWL=y` + `clk32kg` enable; the card enumerates) |
| `0x17C2E119` | 7.3.3 | previous region 0 (session 50: the session-49 tree rebuilt after the reverted experiments; kernel `#59`) |
| `0x91E22899` | 7.3.3 | session-50 experiment (the slow-identification-clock test: `f_min=100000` + the archived `PBX`/`PBIRQ`/`PBCMD` trace; kernel `#58`) |
| `0xBAFD9B3B` | 7.3.3 | previous region 0 (session 49: the session-48 tree + report 220's GPIO1 pad fix; kernel `#57`) |
| `0x0114FFD3` | 7.3.3 | session-49 experiment (the GPIO1 pad fix, kernel `#51`) |
| `0x71A79419` | 7.3.3 | session-49 intermediate (the session-48 tree rebuilt after the WL1283 experiments were reverted; kernel `#50`) |
| `0x12C8C320` | 7.3.3 | previous region 0 (session 48: the RNDIS + dropbear initramfs; kernel `#45`) |
| `0xAD85DF0F` | 7.3.3 | previous region 0 (session 47: the speaker path — line outputs, `spk`/`boost` GPIOs, `disp-en-12-hog` removed; kernel `#44`) |
| `0xF8BADFF3` | 7.3.3 | previous region 0 (session 46: McBSP3 two-link card, kernel `#42`) |
| `0xE5E2AEAF` | 7.3.3 | session-47 intermediate (jack-aware speaker state; `spk` still a hog) |
| `0x47ED0450` | 7.3.3 | session-46 experiment: mcbsp1 as the data engine (still stalled; pads input-enabled) |
| `0x58F85337` | 7.3.3 | previous region 0 (session 45: the committed E5 tree; `#40`) |
| `0x5A04ACD9` | 7.3.3 | session-45 E5 restore (`#35`) |
| `0xE5A13DE7`, `0x6AD9E204` | 7.3.3 | session-45 E9 two-link experiments (data port stalls — §14) |
| `0x92458D00` | 7.3.3 | session-45 E5 + the disabled `&mcbsp1` node (experiment, not on media) |
| `0x3DC4508D` | 7.3.3 | session-45 E6 experiment (McBSP1 as the port; codec clock lost) |
| `0xA66B40EF` | 7.3.3 | session-45 E7/E8 (two PCMs; raw clock pokes crashed the SoC — see trap 193) |
| `0x7A5DD941` | 7.3.3 | session-45 E5 as first built (McBSP bit/frame provider, pads back to outputs) |
| `0xB43F169B` | 7.3.3 | previous region 0 (session 44: reference codec-GPIO init — boot-on `audio-ldo` + pulse, hp input) |
| `0x7C56BDC0` | 7.3.3 | previous region 0 (session 44: `CM_ABE_PLL_REF_CLKSEL` = SYS_CLK + DPLL_ABE 98.304 MHz; E1 clock roles) |
| `0x36597040` | 7.3.3 | previous region 0 (session 42: E1 clock roles — codec AIF1 master, McBSP consumer, input pads) |
| `0xB8830701` | 7.3.3 | previous region 0 (session 41: AUXCLK1 DTS fix, McBSP dump) |
| `0xE36C679A` | 7.3.3 | known-good region 0 (WDT2 + ISL9519 charger) |
| `0x0F6AC47F` | 7.3.3 | last full-regression image (sensors, CM3217) |
| `0x0D1DA0D3` | 6.15.11 | clean pre-userland fallback |
| `0x8C146805` | 6.15.11 | first image with a correct display |

Full image history and the older per-image notes are in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md).

Do not boot `0x5E40C5C1` or `0xB9A1BC4F` (two lanes), or `0x3B04298E`
(LM3532 without VAUX3).

## The unit

| item | value |
|---|---|
| working units | **one: the reference (`0x50175127`)**, armed AND sealed.  The spare (`0x50176EC1`, BSN `1063043093`) is **down** with the 1-2-2 blink = error `0x5B` (report 153) and presents no USB.  Two 32 GB units are bricked and parked |
| region-0 IFS on media | **the ARMv7 dual-core image `boot/chainloader/kernel.img`, CRC `0x3547D613`** (10,448,868 B, kernel `7.3.3 #109 SMP`, `-march=armv7-a`, the **stock MUSB mailbox + comparator lifetime fix**, `panic=0`, the DSI-init TCON hook), region 0.  The previous `0xC9FEE2D1` (`#105`, device-mode-only workaround, `panic=10`) is the known-good fallback; the session-65 ARMv7 base is `0x158D8A85`; the session-64 known-good dual-core image is `0xC675E206`; the session-58 read-write/SSH image `0x294F8E36` (`0x9E0980` B, kernel `#77`) remains on the host.  The way back is the 1917 IFS (`local/work/ifs-shim-sealed.ifs`, 7,900,548 B, sha256 `b52195bb…`); the compressed `local/work/ifs1917-compressed.ifs` (sha256 `f14db207…`) is the alternative.  Earlier images (the `0xED58EF36`/`0xB9DD91E3` touch, `0xAB899C49` compositor, `0xE85217E5` overlay, `0x060B25F2`, `0xDD66D8C3`, …) remain on the host.  Earlier Linux images (region 0 only, block `0x825`): `0x23D88CBD`, `0x373674A4`, `0xA544A731`, `0xAB899C49`, `0xA8F0F30C`, `0xE85217E5`, `0xE84693A1`, `0x8B709950`, `0x50CD2A48`, `0x5C37F181`, `0x5A7F15D8`, `0x9712A086`, `0xF64A680D`, `0x53F7F97B`, `0x69CDAC94`, `0x17C2E119`, `0xBAFD9B3B`, `0x91E22899`, `0x12C8C320`, `0xAD85DF0F`, `0xF8BADFF3`, `0x36597040`, `0xB43F169B`, `0xB8830701`, `0xF97CD9C7`, `0x3B2ECFED`, `0x50842767`, `0x82FC0D35`, the Task-3 `0x0DC36A11`, `0x05819994`, the known-good `0xE36C679A`, and the touch `0x04AF1433` |
| region-0 RCFS (`/base`) | **now the full 1917 RCFS at `0xC00000`** (`local/work/rcfs-1917.rcfs`, the modified one with the `/accounts/devuser/startup.sh` hook, sha256 `c4ab6e34…`): session 49 wrote it to recover root on a stock-TabletOS boot, and it sits **outside** the 7.3.3 test image (which ends at `0x9E0980`).  The pruned oracle RCFS is no longer on media — re-flash `local/work/minimal/setB.signed` for the minimal oracle (trap 227).  Not readable as `/base` without the 1917 IFS |
| UFS (`/`) | **not on media** — hd3 has held Linux filesystems since session 63 and now the Nura ext4; the 1917 vendor `.5.ufs` with `autorun::0` (the session-27 set, `local/work/ufs-1917-devuser.ufs`, sha256 `482e0473…`) is on the host as the way back.  The BB10 `.0.4.ufs` is not on media.  (Not re-hashed this session) |
| `/accounts/devuser/startup.sh` | the 1917 UFS carries the operator's script; it last ran during the session-35 S3/S2 TabletOS bench and topped `BOOTCTR_FCTRY` back to `0x32` |
| `BOOTCTR_FCTRY` | **`0x24`** at the session-67 wrap-up (measured on the unit).  The three mailbox validation boots read `0x2a → 0x29 → 0x28`, then the affinity half's four reboots took it to `0x24`; every read was `>= 0x1e, no write`.  (The session-64 panic loop had drained it to `0x1c`; the recovery image topped it to `0x32`.)  Block `0x3a` record@196.  **Read it on Linux with `dmesg \| grep pbrpmb`** (the top-up line carries the 4-byte LE record).  Self-replenishing: `/init` tops up at <`0x1e`, a TabletOS boot writes `0x32` (`SAFETY.md` §2, `DEVELOPMENT.md` §5) |
| region-1 Linux root | **present** at raw `0x29000000` (region-1 +16 MiB, block `0x2915`): 64 MiB ext4, no journal, on-media sha256 **`20a5edd3…`** (re-hashed at the session-67 close; earlier readings `6f5b8ca7…`, then the staged file `49e318bf…` = `local/images/rootfs-region1-wl1283-bt.ext4`, session 52: the WLAN fixes, **`ccm`/`ctr`**, the per-unit NVS, the CAM+4-sr `wl12xx.ko` and the seven Bluetooth modules), **43 modules under `/lib/modules/7.3.3`** (the sensor/charger/audio set plus **`cfg80211`, `mac80211`, `wlcore`, `wlcore_sdio`, `wl12xx`, `libarc4`, `crc7`** and **`bluetooth`, `hci_uart`, `btbcm`, `crc-ccitt`, `ecdh_generic`, `ecc`, `kpp`**), `pbplay`, `pbmix`, **dropbear**, **`/usr/bin/iw`**, **`/usr/sbin/wpa_{supplicant,cli,passphrase}`**, and **`/lib/firmware/ti-connectivity/{wl128x-fw-4-sr.bin,wl128x-fw-5-sr.bin,wl128x-nvs.bin}`** plus the signed **`/lib/firmware/regulatory.db`/`.p7s`**; `rcS` loads the wireless stack and `bluetooth hci_uart`.  Earlier images: `…-wl1283-final.ext4` (`877c42c3…`), `…-wl1283-assoc.ext4` (`6c2cfb30…`), `a148c362…`, `7e067560…`, `88084b78…`, `a4e76b18…` and `0342ab06…`.  The modules in any of them can be checked by extracting them with `debugfs` (trap 163).  **Session 55: not booted.**  The region-0 image is the hd3-pointing `0xA8F0F30C`; it mounts hd3 (Nura) and never touches region 1.  This root remains the fallback; its `rcS`/dropbear/WLAN/BT are unchanged. |
| TabletOS / OS | **NOT running** — the unit is on the **Nura root from hd3** (image `0x3547D613`, `#109 SMP` dual-core ARMv7, `0fca:8010`, console `playbook:~#` on the USB ACM, **sshd: dropbear on `169.254.0.1:22`**, `PB_LINK=usb tools/pbssh.sh` — **this works** (verified at the session-67 close, ping ~0.4 ms); if it times out the cause is the **host** (WireGuard VPN, trap 341; `ufw`, trap 342), not the device; the root is the hd3 ext4 mounted **read-write**, grown to **62.5 G**, with the ARMv7 module set installed).  TabletOS 2.1.0.1917 returns by flashing `local/work/ifs-shim-sealed.ifs` at block `0x825`; its door is also `169.254.0.1`, so use `PB_LINK=door` for `tools/pbssh.sh` there |
| hd3 (MCT `0x1C`, FS at loader block `0x4915` / raw `0x49000000`) | **the media was re-created on 2026-11-18 (session 65)** by flashing the baked Nura image `local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img` (`07555bde…`, 625,999,872 B) at block `0x4915` (626,065,440 B, `FLASH_OK`), because the BusyBox ext4 that had been flashed there in session 63 was **damaged by power cycles** (`EXT4-fs: This should not happen!! Data will be lost`, corrupt inode bitmap).  First boot grew it to **62.5 G** (15,273,984 blocks) and installed dropbear; the ARMv7 module set was then installed over SSH and `depmod` run.  It is a rw ext4 with `barrier=0` + write-through, so power cuts can still damage it — prefer a clean `reboot`.  The flasher-restorable image is now the **session-71 rebake** `local/work/nura/images/blackberry-playbook-root-gpu-20261125.img` (sha256 `5d3eac8f…`, 625,999,872 B), which carries the session-69/70 GPU result, the ARMv7 modules and the permissions; the session-59 base (`07555bde…`) is untouched.  Its content is the session-59 state: `tabula` 1.0.2, `epitaph` 1.4.0, `tzompantli` 1.2.0, `gorm` 1.2.1, `swayidle` 1.9.1, `foot` 1.28.0, `grim` 1.5.0, `slurp` 1.5.0, `tremor` 1.1.0, `netsurf` 3.11, `xournalpp` 1.3.8, the pvrports stack (`libglvnd`, `mesa-pvr-dri-classic`, `sgx-ddk-um{,-ti443x,-openrc}`, `gcompat`, `wayland-libs-server`), `MESA_LOADER_DRIVER_OVERRIDE=pvr`, the pvrsrvkm module in `/etc/modules`, wvkbd/DejaVu/pb-touchwake, the icon-theme fix and the no-suspend `/etc/catacomb/initrc`.  **The session-71 rebake** (report 272, DECISIONS §105) applies catacomb `r10003`, epitaph `r10008`, the inittab affinity line, the pitch fix, **the live 842-module ARMv7 tree** (the base had ARMv6 — trap 356) and **the permissions set** (polkit, backlight udev, `/etc/deviceinfo` lzo-rle, `playbook` in `video`).  It leaves the bench pvr `/etc/catacomb/initrc` (md5 `00fdff8481e7c4dde730221ef55d53da`) alone, because the APK's copy is the upstream default (trap 357).  `tools/rebake_hd3.py` does this file by file (root loop mount or `debugfs`) and splices the package DB/world from the live root; `tools/rebake-hd3.sh` is the wrapper.  The llvmpipe `swrast_dri.so`/`libLLVM-13` are still absent and are not part of the working state (the desktop runs on `pvr_dri`).  The pre-pvr images (`1fcf0cb0…`, `1f14915f…`), the pristine `076bafcf…` and the older `f7858110…` remain on the host.  See reports 249/250, 263 §3, 270 and **272**. |
| device-side files | **persistent on hd3 (session 58: the root is writable and grown):** the initramfs installs `/usr/sbin/dropbear`, `/etc/dropbear/*`, `/root/.ssh/authorized_keys`, `/etc/local.d/pb-ssh.start` and `/etc/local.d/pb-grow.start` on first boot, and `/etc/pb-grown` marks the grow.  `/var`, `/home` and `/tmp` are now real directories on the 56.5 G FS, so the DE state and the SSH kit survive a reboot (the state files written before the switch are on the grown FS).  `memdump3`/`memw32`/`pbi2c` are outside the tree: push them (paced) after a reflash (trap 177).  The first-Nura-boot log is `docs/captures/playbook-live-2026-11-15-hd3-nura-boot.txt`.  Session 57's touch tools (`evcap.py`, `keylog.py`, `muninraw`) lived only in `/tmp`; the measured evidence is preserved in the captures.  **Session 59 deployed:** `wvkbd-mobintl` 0.21 (`/usr/sbin/wvkbd-mobintl`), `/usr/share/fonts/dejavu/DejaVuSans{, -Bold}.ttf`, `/usr/bin/pb-touchwake`, `/usr/share/icons/default/index.theme` (`Inherits=Adwaita,hicolor`), `adwaita-icon-theme` + `glycin-svg`, a hicolor netsurf PNG, the edited `/etc/catacomb/initrc` (no swayidle, wvkbd + pb-touchwake), and `playbook` in the `input` group (gid 23).  Tools/daemon sources are tracked in `tools/bench/`; the pmaport edits are under gitignored `local/work/nura/pmaports/`. |

The factory boot budget is **self-maintaining in bench use**: every successful
factory/CRC boot spends one unit, and the booted OS puts it back — our Linux
`/init` runs `pbrpmb topup 0x1E`, and every TabletOS boot tops the record to
`0x32`.  **Since session 15 our images no longer reboot themselves**, so a test
boot costs one unit, is replenished at boot, and stays up until asked; it is not
a reason to avoid an experiment.  The hard hazards remain: `0` and `> 0x32` do
not fall back to the product path — they hard-lock the unit (`SAFETY.md` §2) —
and a failed/half-formed write or a flat pack is still a brick risk.

## Blockers in play

**Session 67 — the open blockers are:** (1) the **CPU voltage/Opp** — `cpufreq-dt`
exists and scales, but the CPU supply is unattached and the VC init clamps to
1.35 V (report 266 §2/§3); (2) the remaining **UI performance** — was measured
with CPU 1 starved of userspace (see the closed item), so it needs re-measuring
before it can be worked; llvmpipe's deferred `swap` (420–550 ms per full-screen
drawer frame) and its first-draw JIT are the known costs; (3) **Kumo** needs a
WPEWebKit/WebKit-fork build (not
packaged, report 247 §4); (4) the **intermittent WRITE-busy stall** — not a
cache flush (report 250 §1), mitigated by `barrier=0` + write-through, but it
still fires during write bursts and can damage the rw ext4 if power is cut
(trap 336); (5) the **intermittent USB teardown** (`VUSB` disabled by the TWL
comparator's IRQ; 30 s, 90 s or not in 6 min — trap 337, contradiction file);
(6) the **memory read throughput** — the EMIF is correctly configured
(400 MHz LPDDR2-800, 32-bit, CL 6, power-down off, L3 200 MHz) and the CPU is
at 1008 MHz, but a userspace benchmark reads only ~0.4–0.6 GB/s — **that was
measured single-core, so it must be redone** (report 263 §4b; open).
**Closed in session 67:** the **mailbox fault** — the bad indirect transfer was
a freed TWL6030 `phy_companion` (a failed `twl6030_usb_probe()` on a deferred
`vusb`), fixed by the comparator lifetime change; the stock
`omap_musb_set_mailbox()` call now runs on dual-core (3/3 clean boots; report
265, trap 339), so OTG host mode is reachable again.  Also **the second core
now takes userspace work** — PID 1 was affinity-masked to `{0}` (report 266,
trap 340); the inittab workaround is in place and verified, with a kernel-side
fix still owed.
**Closed since session 60:** the CPU's second core (dual-core stable with the
workaround), the ARMv6 codegen (now `-march=armv7-a`, trap 334), the
**`epitaph` panel** (r10005 — report 251; trap 292); the panel's
**permissions**; the touch position ghost and lost burst taps (kernel `#74`,
report 248); the backspace/enter repeat (wvkbd, report 250 §2); the app-drawer
icons; the eMMC cache item (closed by measurement, report 250 §1).  **Bench
note:** DPMS wake-by-touch works with pb-touchwake, but repeated DPMS resume
wedges the DSI link (black panel — only a reboot clears it), so auto-blank was
removed and the panel stays lit (report 250 §2a/§2b, traps 289/298/301).

**The audio blocker is closed, speakers included, and the diagnostics are
retired (sessions 47/48, reports 214/215).**
`pbplay`'s 440 Hz tone is audible in both ears on headphones and in both
speakers at 48 kHz ±1 % on the working image (`0xBAFD9B3B` + rootfs
`a4e76b18…`), re-verified after the cleanup at DAC1 volume 40.  The data path
is session 46's (AIF1 on **McBSP3**; two links `mcbsp2 + wm8994-aif1` clocks,
`mcbsp3 + wm8994-aif2` data; the `omap-mcbsp` channel-layout guard fixed the
right channel; the ABE is not needed).  The speakers needed the codec's **line
outputs** (not class-D), `spk` driven **low**, and GPIO1_15 (the `audio_boost`
enable) **high** — all three in the driver/DTS.  **Remaining audio items: none
required** — the optional **MCLK1 probe** (trap 165) stays optional, and the
`audio_boost` net is behaviourally confirmed but not schematic-confirmed.

**The WL1283 WLAN ASSOCIATES and passes traffic (session 52, report 236).**  The
session-51 stability fixes hold (CAM, 4-sr) and the association blocker is gone:
**`ccm.ko`/`ctr.ko` were missing**, so `ieee80211_key_alloc()` could not allocate
the `ccm(aes)` AEAD and the PTK install failed with `-ENOENT` — which
wpa_supplicant reports as `WRONG_KEY`.  They are now in region 1 and loaded by
`rcS`.  Measured: `wpa_state=COMPLETED`, WPA2-PSK/CCMP, 5785 MHz, −41 dBm,
MCS7/65 Mbit/s, DHCP lease 192.168.0.212, gateway and 1.1.1.1 reachable.  The
per-unit NVS ships; the country must be set (`iw reg set <CC>`) for 5 GHz APs
the world domain marks no-IR (trap 245).  **Wi-Fi is at its hardware ceiling**
(1×1 HT20, 72 Mbit/s; TX 2.9 / RX 4.4 MB/s) — 40 MHz is measured harmful
(trap 247).  **Open (not blocking use):** *why* 5-sr panics on scan (report 235
§3), *why* the ELP wake is not honoured (report 232 §6), and the occasional
first-auth timeout that needs a `wpa_cli scan` nudge.  **Next for the combo
chip:** BT (`BT_EN` = gpio 55, UART2), then FM/GPS (§"What is next").

**The per-unit WLAN calibration is in the QNX `hd5`
partition** (report 197, session 50) — a TI WL128x radio-parameters INI + a
binary blob, and the assigned MAC is in NVRAM `deviceinfo`
(`14:74:11:8f:9c:c3`, patched into the generic NVS).  It is the source for
`wl128x-nvs.bin` **after** enumeration; it was never the `-110`.

**The touch is fixed (session 58).**  I²C2 `0x48072000` = `i2c-1` at
`0x65`, `playbook-munin.c`, `input0`/`event0`; the 1917 `scaling.conf`
calibration maps to 1024×600, the GPIO2_3 edge IRQ is `irq 110`, and the Y
inversion is in (kernel `#69`).  The driver re-reads a torn report until the
counter parity matches, keeps sampling while the controller asserts its IRQ
line (plus a 200 ms grace period) and, on a new down that repeats its slot's
previous coordinates within 300 ms, waits one sample and reports the fresh
position (kernel `#74`, report 248).  Contacts are separate and the fast-tap
ghost is gone.  The remaining backspace/enter repeat is squeekboard's, not the
driver (trap 299).  The power/reset lines are still not driven by the driver;
only cold boots need the GPIO pulse.

**Media state: region 0 = `0x3547D613`** (kernel `7.3.3 #109 SMP`,
`-march=armv7-a`, 10,448,868 B, no `maxcpus`, the **stock MUSB mailbox** +
comparator lifetime fix, `panic=0`, the DSI-init TCON hook); region 1 = the
WLAN+BT rootfs, **on-media sha256 `20a5edd3…`** (the staged file
`49e318bf…` is 67,108,864 B: **43 modules** + both `wl128x` SR firmware files
+ the CAM/4-sr `wl12xx.ko` + the per-unit NVS +
`iw`/`wpa_supplicant`/`wpa_cli`/`passphrase` + the signed `regulatory.db`;
`local/images/rootfs-region1-wl1283-bt.ext4`); hd3 = the **Nura** ext4 from
`local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img` (`07555bde…`,
625,999,872 B), mounted read-write and grown to 62.5 G, with the ARMv7 module
set.  The unit is on Linux, not TabletOS.  The way back to TabletOS is the 1917
IFS (`local/work/ifs-shim-sealed.ifs`, block `0x825`); the previous Linux images
`0x158D8A85`, `0xC675E206`, `0x19E24D8F`, `0xBEF06B78`, `0x294F8E36`,
`0xED58EF36`, `0xB9DD91E3`, `0xA544A731`, `0x373674A4`, `0x23D88CBD`,
`0xAB899C49`, `0xA8F0F30C`, `0xF64A680D`, `0x53F7F97B`, `0x69CDAC94`,
`0x17C2E119`, `0xBAFD9B3B`, `0x91E22899`, `0xAD85DF0F`, `0xF8BADFF3`,
`0xE5E2AEAF`, `0x58F85337`, `0x47ED0450`, `0x5A04ACD9`, `0x92458D00`,
`0x7A5DD941`, `0x36597040`, `0xB8830701`, `0xF97CD9C7`, `0x3B2ECFED`,
`0x0DC36A11`, `0xE36C679A` and `0x04AF1433` remain.
NVRAM and the MCT remain unwritable, and an agent never writes RPMB
(`SAFETY.md` §6).

## What is next

0. **THE ACTIVE ITEM FOR SESSION 68 — put the UI on the GPU (operator
   directive; report 268 is the plan).**  Measured: the SGX works on libEGL's
   **GBM/DRM** platform (12 window configs, `PowerVR SGX 540`, dmabuf export),
   and only the **Wayland client** path refuses it.  Session 68: (1) diagnose the
   wayland rejection with `EGL_LOG_LEVEL=debug` (do not patch Mesa before that),
   (2) prove the vendor-supported GBM path with pvrports' `phoc`, (3) decide with
   data, (4) only then weigh the open-stack SGX540 port
   (`The1TrueJoe/powervr_sgx`).
   * **Owed after the GPU — the CPU (session 67, reports 265/266, ROADMAP 7a).**
     Both cores now take userspace work, and the **stock
     `omap_musb_set_mailbox()` call is restored** (the mailbox fault was a freed
     TWL6030 `phy_companion`, report 265).  PID 1 was affinity-masked to `{0}`,
     so all userspace ran on one core (`nproc` = 1); the inittab workaround
     `::sysinit:/usr/sbin/taskset -p 3 1` fixes it (`nproc` = 2, load balanced) —
     a **kernel-side fix in `sched_init_smp()` is still owed** (report 266 §1).
     `cpufreq-dt` **does** exist and scale; what is missing is the CPU
     **supply**, and the `twl6030_uv_to_vsel` warnings are startup VC init
     clamped to 1.35 V (report 266 §2/§3).
   * **Then the MPU OPP:** `cpufreq-dt` is bound and scales
     (`300000 600000 800000 1008000`, `ondemand`), so the old "no cpufreq
     policy" note is stale.  What remains is the CPU **supply** (no
     `supply-*` under `/sys/kernel/debug/opp/cpu0/`, so OPP voltages are not
     applied) and the `twl6030_uv_to_vsel: OUT OF RANGE` lines, which are
     startup VC init (`omap4_mpu_vc_data.on` 1.375 V vs the TWL's 1.31666 V
     ceiling) clamped to VSEL `0x3A` = 1.35 V — the chainloader's own value.
     Decide 1.35 V-for-all-OPPs vs a supply binding; SmartReflex MPU is present
     but idle (`autocomp = 0`).  Thermal-check (trap 312; report 266 §2/§3).
   * **Then re-measure:** the memory-throughput numbers (report 263 §4b) and the
     UI latency were taken with CPU 1 starved of userspace; redo them now that
     both cores take load, then shrink/pre-warm the drawer (the swap) and pursue
     a hardware GL path for Wayland clients (trap 314).  Remove the r10006 draw
     timing and the `PBGL_PROFILE` env before the final image.
   * **Durable (corrected in session 70 by measurement):** the flasher-restorable
     hd3 image needs exactly **three** changes, not the list this item used to
     carry — `catacomb` r10003, `epitaph` r10008, and the inittab affinity line.
     The baked image already has the pvr `/etc/catacomb/initrc`
     (md5 `00fdff8481e7c4dde730221ef55d53da`), the pvr `mesa.sh`, the module, and
     the pvrports stack; **no llvmpipe `swrast_dri.so` or `libLLVM-13` is in the
     shipped state at all** (the desktop runs on `pvr_dri`), and the udev/polkit
     rule directories are **empty**.  `tools/rebake-hd3.sh` applies the three to a
     copy of the base image; it has not been run (needs root).
   * **Apps / Wi-Fi are up:** `nmcli radio wifi on` (the radio was disabled)
     gives `wlan0` a scan; `gorm` with `10-gorm.rules` lists ~10 APs (joining
     an AP is untried), and a cleanly launched `tzompantli` runs apps (an
     injected tap on Foot ran `foot`).  Traps 306–308.
   * the **export is current**: 67 files / 9 136 lines (`d19adff8…`), verified
     0 rejects / 67/67 against pristine 7.3.3 in session 67, with the pvr
     driver vendored; the `local/kernel-tree` still carries the session-62/63/64
     diagnostics (not in the patch).  Re-export after any further kernel change.
   * **Build speed:** the armv7 package builds under qemu-user (`--no-cross`)
     and abuild wipes `$srcdir`; the local APKBUILD now sets a persistent
     `CARGO_TARGET_DIR`.  The native `--cross` path is blocked by a
     host/chroot rustc version mismatch — matching them is the real speedup.
1. **WL1283 — the WLAN ASSOCIATES (session 52, report 236).**  Session 51 fixed
   stability (CAM) and the scan (4-sr); session 52 found the association
   blocker — the **missing `ccm`/`ctr` modules** — and the link now reaches
   `COMPLETED`, takes a DHCP lease and passes traffic.  The per-unit NVS is
   shipped.  **Open, none blocking use:** *why* 5-sr panics on scan (235 §3),
   *why* the ELP wake is not honoured (232 §6), and the occasional first-auth
   timeout (restart the supplicant or nudge a scan).
   **Do not** re-run the session-49/50 negatives, do **not** use
   `IRQ_TYPE_LEVEL_LOW` for WL_IRQ (trap 238), do **not** use
   `wlcore_sdio dump=1` to measure a timing fault (trap 239), do **not**
   switch back to 5-sr (trap 243), and do **not** advertise HT40 (trap 247).
1a. **WL1283 BT — the core WORKS; the controller open is parked (session 53,
    report 238).**  Parked by the operator to move to item 7a; the blocker is
    located, so this is resumable in one experiment.
    (a) **What is proven:** the core answers `HCI_Reset`, reports itself
    (`hci ver 6` / `lmp ver 6` / `mfr 0x002b` = TI, BT 4.0) and **runs a full
    BR/EDR inquiry that finds a peer**, reproducibly.  The reference's
    `WL1283.bts` loads **315/315** and moves LMP subversion `0x2b0f` → `0x2b33`;
    loaded **before** the attach it makes `hci0` **register with mgmt** (0 → 1
    controller, negative control both ways).
    (b) **The one experiment when resumed:** find who emits the stray `0x30` that
    breaks the kernel's H4 receiver.  `MGMT_OP_SET_POWERED` returns `0x05`
    (`-ETIMEDOUT`) after `Frame reassembly failed (-84)` (`-EILSEQ`); an
    instrumented `h4_recv()` shows a single `0x30` arriving ~200 ms after the
    last complete init reply, in 6 of 6 runs.  It never appears from userspace.
    Swap the instrument in with `rmmod btbcm` **then** `hci_uart` (trap 251) and
    log the driver's *writes* too, so a host-side echo is visible.
    Excluded by measurement: alignment/padding, chip idle-sleep, leftover bytes,
    UART line errors (`LSR = 0x60`), truncated long replies, and hardware flow
    control (identical A/B on a clean boot).
    (c) **Then** power, an inquiry through the stack, and HID.  **Ship
    `l2cap`/`sco`/`rfcomm`/`hidp` first** — they are modules and none are in
    region 1 yet.
    (d) **LE is refused by the controller** (`0x0c`, `Write_LE_Host_Support` =
    `0x11`, LE feature page zero), but the chip reports BT 4.0 with the
    LE-supported bit and the reference stack being BR/EDR-only reflects the
    vendor's choices, not the silicon — so no enable sequence is known, and
    impossibility is **not** demonstrated.  **The `wl128x-fw-4-sr/5-sr.bin` files
    are WLAN firmware, not BT** — do not swap them expecting a BT change.
    `BT_EN` = gpio 55, UART2 = `/dev/ttyS1`; tools in `rootfs/bt/`.
    **Do not** re-run the "BT is silent" probes — answered.
    **FM:** mainline had `drivers/media/radio/wl128x` from 2.6.39 to 6.13 and
    removed it; bringing it back means porting that driver forward and whatever
    transport it needs.
    **GPS:** no mainline driver; the operator has a 2024 LKML patch to
    evaluate, and the reference's own GPS path is the fallback.
2. **The WM8994 audio — DONE and the diagnostics retired (sessions 47/48,
   reports 214/215).**  Stereo 440 Hz at 48 kHz on headphones and speakers,
   re-verified at DAC1 volume 40 after the cleanup.  The only items left are
   optional: the **MCLK1 probe** (trap 165) and identifying what the GPIO1_15
   `audio_boost` enable supplies if a schematic appears.  The ABE is not needed
   (DECISIONS §58/§59).
3. **Touch: DONE (session 38, report 198) but down after a cold boot.**  The
   calibration and the IRQ are in the tree and proven; after a cold boot the
   controller does not boot (`0x65` NAKs) until the power/reset GPIOs
   (GPIO1_14/GPIO4_8) are pulsed, which the driver still does not drive.  A
   warm reset keeps it up (session 42).
4. **The `wlcore@2` child is in the DTS** (`ti,wl1283`, `reg = <2>`, WL_IRQ,
   26 MHz ref/TCXO), the stack probes, and the firmware stays up; the only
   remaining node-level item is the **per-unit `wl128x-nvs.bin`** from the `hd5`
   calibration (report 197), to replace the generic NVS.
5. **The unit is on the Nura read-write ext4 root on hd3** (region 0
   `0x3547D613`), grown to 62.5 G — not the old session-58 overlay.  **SSH
   works:** `PB_LINK=usb tools/pbssh.sh` (`169.254.0.1:22`, key-only; the host
   is `169.254.0.2/16` on `enp8s0f3u1i2`).  If it times out the cause is the
   **host** — WireGuard VPN (trap 341) or `ufw` (trap 342) — not the device.
   The `ttyGS0` shell on the USB ACM (`/dev/ttyACM0`, `playbook:~#`) remains the
   fallback; note `tools/pbcon.py` expects `pb> ` and does not drive it
   (trap 274).  To return to TabletOS, flash the 1917 IFS at block `0x825`.
6. **The QNX door** uses `PB_LINK=door tools/pbssh.sh` (qconn on
   `169.254.0.1:9090`, root ssh); the minimal oracle is
   `local/work/minimal/setB.signed`.

## Workspace location

The workspace moved at the end of session 15 from the old checkout (the root
filesystem) to this repository.  Start sessions in the repository root.
Absolute paths were retargeted in `tools/` (`pbssh.sh`, `door-hold.sh`,
`pb_ring.py`, `pb_events.py`), the shipped kernel configs
(`CONFIG_INITRAMFS_SOURCE`) and the canonical docs; historical reports keep their
original wording.

A **fallback copy of the old tree is deliberately kept at `$HOME/playbook-linux`**
until the operator is happy with the move.  It is stale from the moment of the
move — do not work in it, and do not let anything (`CONFIG_INITRAMFS_SOURCE`,
the door scripts) point at it.

**Public snapshot:** `$HOME/SSDdrive/playbook-linux-public` (remote
`git@github.com:Psyden57/playbook-linux.git`).  The private tree is the source
of truth; publish with `tools/publish_public.sh` (see `REPO-MANAGEMENT.md`).

**Both cross-toolchains are inside the workspace** in the gitignored payload:
`local/toolchains/armv7-eabihf` (Buildroot gcc 14.3.0) and
`local/toolchains/qnx660-master` (QNX SDP 6.6).  `. tools/env.sh` exports
`PATH`/`CROSS_COMPILE`/`QNX660`/`QNX_HOST`/`QNX_TARGET`; `tools/env.sh --check`
validates them and prints the versions.

---
