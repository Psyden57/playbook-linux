# Session 76 (absolute) — the SGX interrupt reaches the CPU, five defects fall, and the entry counter turns out to have been lying

> Numbering: absolute `76` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era session 70**
> (`absolute = Linux-era + 6`).

Date: 2026-11-26.  Unit at the end: **Linux `7.3.3`** on hd3 (root
`/dev/loop0` ext4 rw), region 0 **`0x02C1DDC2`**, region 1 **`20a5edd3…`**,
`BOOTCTR_FCTRY` **`0x25`** at the close (`>= 0x1e`, no write; `0x23` after the
wrap-up re-read's two reboots, by arithmetic — not re-read), `card1`/`renderD129` =
`powervr_sgx`, panel `bl_power 0` / `card0-LVDS-1` connected / **0 DSI errors**,
`wlan0` **down**.  Full bench record: STATE.md, "Bench at the close (session 76)".

## Goal of this session

HANDOFF-75 §0b: verify what `EVENT_PDS_EXEC` points at, then the doorbell, and
get the EDM to dispatch.  The operator's direction was to stop treating a boot as
a cost — "there's no cost for a reboot/flash really" — so the session moved from
static reading to measuring as soon as a hypothesis was testable.  Mid-session
the operator redirected to the desktop (llvmpipe, the panel blank), then back to
the GPU, and at the close asked for the re-emit work to be pursued.

## Confirmed (measured, read in a file, or seen on the bench)

**The interrupt had never been wired to the CPU.  That is the session's solid
result and it stands.**

* **The SGX's request reaches the CPU only through the OCP wrapper's gate.**  On
  OMAP4 it enters the `ti,sysc` target module's `IRQSTATUS_2` (window offset
  **0xfe38**) and is forwarded only while `IRQENABLE_SET_2` (**0xfe44**) bit 0
  is set; the vendor's OMAP4 code enables exactly that pair
  (`sysconfig.c` `SysEnableSGXInterrupts`) and maps the whole 64 KiB window.  Our
  driver mapped 0x4000 and never touched it.  With the gate opened, **irq 147 ran
  0 → 100000** (the kernel's unhandled-IRQ limit).  *Artefact:* live
  `ocp irq: status_2 0x00000000 enable_set_2 0x00000001`; fact sheet.
* **The storm it exposed was ours.**  The image's `init_set` ORed TIMER (b29)
  into `EVENT_HOST_ENABLE`; `srvinit` writes SW_EVENT only.  Now
  `EVENT_HOST_ENABLE = 0x00004000`, no stuck b31.  *Artefact:* register dump.
* **Three PDS defects fixed**, each verified byte-exact through the new `pds`
  node: the `edm_init` branch assembled a 12-word data segment into PDS0's
  0x18-byte slot (PDS1 at +0x20 overwrote its DS1 word and its DOUTU/HALT); the
  segment is now 4 words; `EVENT_PDS_DATA` said 3 = 48 B for a 24 B slot (now
  1 = 16 B); `EUR_CR_PDS` (0x0abc) was never written
  (`DOUT_TIMEOUT_DISABLE`, now `0x40`).
* **`FIX_HW_BRN_30852` wired.**  The PDS must not auto-gate its clock;
  `CLKGATECTL` reads **`0x000aa6aa` → `0x000a66aa`** (PDS field `01` = ON).
* **A fourth real defect: `EUR_CR_DMS_CTRL` (0x0a74) was never written at all.**
  Added to `status`; it read **`0x00208200`** (EDM 32 / vertex 32 / pixel 32,
  the reset default) where the vendor derives **`0x003f8200`** — EDM **63**,
  because `FIX_HW_BRN_23632` is in the rev-120 errata ("at least 33 EDM tasks",
  `sgxinit.c:4451/4492`).  Now programmed per-core and reads the vendor's value.
  **It did not change the symptom.**  (The pixel-task field is shift **4**, not
  5 — `sgx540defs.h:2215`.)
* **The entry instrument, corrected, says the event program is fetched TWICE.**
  Counter on control-block **word 0x07** (`UK_ENTRYLOG`): `entry = 2`,
  `tick (0x2c) = 1`, `SC_VERSION (0x18) = 0x10000`, stable over 94 s **and
  unchanged across eight host kicks** (`resets` moved, so they reached the core).
  One init pass plus one dispatch pass, and the dispatch pass is the timer's.
  *Artefact:* firmware `fix1` (wrapped md5 `f468a71d…`); capture
  `sgx540-entry-counter-2026-11-26.txt`.
* **One PDS-task emit from the USSE task end does NOT re-fetch the program.**
  `rearm9` puts the vendor's `emit.t2 r20, r18, r16 [f12 f13 imm=2115]` on
  `tmr_tick_end` (the pass that runs), excludes `init_set` with a per-call-site
  flag, and carries its four loader patches.  Measured: **the one-shot flag goes
  1 → 0, so the emit executed**, and `entry = 2, tick = 1` is unchanged, stable
  at 85 s.  Control: `fix1`, the same instrument without `UK_REARM`, also reads
  `entry = 2, tick = 1`.  *Artefact:* `rearm9` (wrapped `e3717aad…`).
* **Emitting from `init_set`'s task end faults the launch outright.**  Measured:
  `microkernel did not start (EDM launch faulted: BIF int 0xe000 fault
  0xfef4000, event status 0x20000000)`, probe `-5`, no `card1`.
* **Re-issuing the launch kick from the host does not raise the entry count**
  (new `relaunch` node writes `cr->event_kick2` exactly as
  `pvrsgx_fw_start()` does; `resets` does not move).  Weak — the count was read
  on the bad word, so this rests on `resets` staying put.
* **The vendor's DDK 1.8 drop contains the SGX540 microkernel as SOURCE and as a
  prebuilt binary** (`/home/psyden/Pdrive/GFX_Linux_DDK-master`, outside the
  repo).  `eventhandler.pds.asm` is the program `EVENT_PDS_EXEC` points at — a
  **PDS dispatcher** over `ir0`/`ir1` whose `INPUT_*` constants the host patches
  at load (`sgxinit.c:2507-2510`; word indices in the generated
  `eventhandler_pds.h`: IR0 `[17]`, IR1 `[20]`, timestamp `[16]`, DOUTU1
  `[1],[5],[9],[13]`, DOUTU2 `[29]`, generic DOUTU0 `[12]`).  **The event bit
  map is not `EUR_CR_EVENT_STATUS`:** `EURASIA_PDS_IR0_EDM_EVENT_TIMER` is
  **bit 1**, versus b29 in STATUS.  Prebuilt images
  `binary2_540_omap4430_android_{debug,release}/…/ukernel.bin` (`cfc577aa…`
  22 192 B / `ff69e400…` 19 984 B).  **Reference only** (DECISIONS §115).
  *Artefact:* `sgx540-vendor-ukernel-source-2026-11-26.txt`.
* **The emit is encodable and byte-identical to the vendor's.**  `sgx-asm` takes
  **stdin with `-b`**: `emit.t2 r0, r2, r4 [f12 f13 imm=3138]` → `00800104
  fb20b188`, the vendor's own instruction at that kind of site, round-tripping
  back to the same text.
* **Desktop: llvmpipe restored and measured.**  The session-71 rebake had dropped
  it (6.9 MB softpipe, **no `libLLVM*` on the root**).  Installed from the cached
  Alpine Mesa 21.3.9 gallium build; after a reboot `epitaph` maps
  `swrast_dri.so` + `libLLVM-13.so` and `catacomb` maps `kms_swrast_dri.so` +
  `libLLVM-13.so`; `GL_RENDERER = llvmpipe (LLVM 13.0.1, 128 bits)`.  Offscreen
  1024×585 `glClear+glFinish`: **4.3 ms vs softpipe's 48.0 ms**.  *Artefacts:*
  md5s in STATE; operator "snappier".  **The hd3 image still lacks both.**
* **The fast-tap panel blank is catacomb's own double-tap sleep gesture**, not a
  rendering race.  kprobes on `drm_panel_disable`/`_enable`/`bl_power_store`: a
  fast-tap burst gives **4 disable + 4 enable, all `comm="catacomb"`**, one
  cycle `enable @196.06 → disable @196.09` (34 ms); `bl_power_store` 0 hits, and
  the DRM connector's `dpms` stays `On` (a real `catacomb msg dpms off` moves
  *both*).  `windows/mod.rs on_double_tap()` sleeps for a double-tap in the
  centre 20 % of the handle; `input.rs:505` wakes on *every* double-tap.  Guard
  deployed (`gesture-handle --height 0`, `/etc/catacomb/initrc` md5
  `56888215…`) — measured **0** disable/enable after.  **Temporary; the real fix
  is owed** (DECISIONS §114).

## Inferred (reasoning from evidence, not directly measured)

* **The EDM's remaining fault is downstream of every host-visible input.**  Strong
  as a location, weak as to mechanism: with `EVENT_PDS_ENABLE` (TIMER set),
  `EVENT_PDS_EXEC` (resolves onto our PDS0 stub), `EVENT_PDS_DATA`, `INFO`,
  `EUR_CR_PDS`, `CLKGATECTL`, `EVENT_HOST_ENABLE`, the OCP gate and the DMS
  counts all confirmed, the entry count still does not pass 2.
* **The one dispatch pass is the timer's** (`entry 2`/`tick 1`, kick counters 0,
  unchanged by eight kicks).
* **Our event entry polls `EUR_CR_EVENT_STATUS` while the vendor's PDS
  dispatcher reads `ir0`/`ir1`.**  Certain as a difference (both sources read);
  unproven as the cause — no experiment has yet put the two designs on equal
  footing, and the vendor's HOST patches are absent from our loader entirely.
* **`tick2` cannot be reproduced from the current source** (nor session 75's
  `kick1`/`tick1`): the ukernel tree carries this port's *uncommitted* work —
  `git show HEAD:ukernel/ukernel.py | grep -c SINGLE_CONTEXT` = 0, and
  `cores/sgx540.py`/`ukernel.py` differ from HEAD by 378/251 lines.  Any
  single-variable firmware comparison needs that resolved first.

## Dead ends and wrong assumptions (valuable — do not skip)

**The big one: this session's entry counter was reading a control-block word the
microkernel itself writes, and three separate "decisive" conclusions came off it.**

* **"The event program is fetched exactly once and never again."**  Believed from
  `entry = 1` across 96 s idle, eight kicks, a host re-arm, and a counter moved
  to the program's first instruction ("decisive").  **Killed by:** the `rearm2`
  build booting with the counter at **0** while `SC_VERSION = 0x10000` proved init
  had run — impossible for a program that executes.  The constant was written
  `0x07 * 4` and consumed as a **word index**, so the counter sat on word
  **0x1c = `CLEANUP_DEFER_CT`** (`cores/sgx540.py:65`), which `init_set` clears
  and the cleanup path writes twice more.  Corrected: **fetched twice**.
  Everything inferred from "once, never again" is withdrawn — including the
  reasoning that the program "must be re-emitted because it never re-runs".
* **Third instance of one unit bug in this session, in a *reader*:** the
  `relaunch` debugfs node read `ctl[0x1c]` and printed "entry counter 0", which I
  nearly read as "the launch never ran".  **My first "fix" changed only the
  comment, not the index** — caught in the wrap-up re-read, and now reading
  `ctl[0x07]`, verified on the unit: it prints **`0x00000002`**, the real entry
  count.  **The rule that catches all three:** read an existing, working
  use of the same constant in the same file and copy its form exactly, and never
  trust a numeral whose expression is byte-shaped but whose consumer is
  word-indexed.
* **"The toolchain cannot encode the PDS-task emit."**  Believed from `sgx-asm`
  rejecting `emitpds…` **and** its own disassembler's `emit.t2 … [imm=]` text.
  **Killed by:** the assembler takes **stdin with `-b`**, as `build.py` calls it;
  my file-argument invocation exits 1 whatever the instruction.  It encodes
  byte-identically.  Cost: a documented wrong recommendation.
* **"An `emitpds` count of 0-versus-0 shows the vendor image has none either."**
  Both zeros carried no information — the mnemonic is not in the ISA at all.  A
  tool's silence is not evidence about the object (trap 397's lesson again).
* **"`issued 8 → 16` proves the kicks reached the core."**  No: `ccb_issued` is
  advanced **host-side** (`pvrsgx_ccb.c:93`) before anything reaches the
  microkernel.  `resets` was the honest half.
* **"`event_pds_data0` must be 9, the vendor's derivation."**  `144 >> 4 = 9` is
  right for the vendor's 220-word dispatcher; our PDS0 is a 4-word stub whose
  16 B is genuinely 1.  Retracted in place.
* **"`sgxinit.c:2821` derives `EUR_CR_EVENT_OTHER_PDS_DATA`."**  It patches a
  *limm inside the USSE init program* (`IUKP2`); the register write is a
  different site fed by a different structure.
* **"The init gate can be a runtime `ui32InitStatus` test."**  The host writes
  `INIT_COMPLETE` ~20 ms *after* the kick, so a runtime gate can exclude the only
  dispatch pass there is.  Replaced by `end(rearm=False)` per call site.  (The
  gate itself also carried the unit bug: `SA_HOSTCTL * 4` → word 4.)
* **"The emit's operands are loader-patched."**  The `end()` version said so in
  its comment and never emitted `pds_size`/`pds_addr`; the image shipped with
  `0xdbeef` sentinels.  Caught by a manifest diff (`{ctlblk, pdtable}` vs all
  four).
* **"A `self.no_rearm` flag can scope the emit to one call site."**  Traced: the
  *second* `end()` call saw the flag, not the first.
* **"Appending new values to `ukfw_patch_value` is safe."**  It makes the image
  fail `ukfw_parse` ("bad entry or patch table") because the range check is
  `> UKFW_PDTABLE`; new values must be inserted **before** it.  The GPU then
  never binds (`/dev/dri` shows card0 only).
* **"`CLKGATESTATUS` bit 7 set means the PDS is running."**  The opposite — it
  reports a *gated* unit, and forcing always-ON is exactly what BRN-30852 needs.
* **"`/dev/mem` can write the OCP wrapper."**  It faults (L4 firewall).
* **"600 null jobs in 2.1 s means the microkernel is dispatching."**  Killed by
  the CCB `issued` counter (+1) and one timeout in dmesg.
* **"§0b's stated method can find where the exec address points."**  It indexed
  0x804090 *bytes* into a 6-word buffer (8 MiB past it), and the target is
  arithmetic the driver itself computed.
* **A self-inflicted wedge, recorded because it looks like a hardware fault:** 20
  back-to-back `catacomb msg dpms on/off` pairs left catacomb in state `D` with
  `bl_power 4` and `drm On`.  Killed the loop, recovered.
* **Read the DMS pixel-task field at shift 5, then 4.**  `sgx540defs.h:2215` says
  shift 4; both live and vendor values then decode sensibly (32/32).

## Unknown / open questions

* **Why does the entry stop at 2 and the tick at 1?**  *Settle by:* getting one
  job to complete — the first thing that produces a task end after init, and what
  makes the re-emit measurable at all.
* **Who re-emits?**  The USSE task end is answered negatively (`rearm9`).
  Remaining: a PDS-level loop in the program itself, or something outside the GPL
  source.  *Settle by:* an oracle capture of `EUR_CR_DMS`/loopback state on the
  reference OS, or making our PDS0 stub DOUTU back into itself instead of `halt`.
* **Do host kicks reach the microkernel at all?**  `resets` moves but the
  `kick`/empty-CCB counters stay 0 and `ccb wo 0 ro 0`.  *Settle by:* a
  microkernel-side counter at the `kick` entry on a word **verified** free.
* **`EUR_CR_DMS_AGE` (0x0a78) is unprogrammed and cannot be derived:** the vendor
  writes `DMS_AGE_ENABLE | PVRSRV_SGX_MAX_DMS_AGE` and
  **`PVRSRV_SGX_MAX_DMS_AGE` is not defined anywhere in the DDK 1.8 drop**
  (grepped every header).  Needs the 1.17 header or an oracle read.
* Unchanged and still open: `0xb14`'s write (trap 387), `sgx540_init_regs`'
  remaining values, the microkernel register tables (0x618/0x62c/0x694/TA
  0x200/3D 0x428, all hypothesis), `USE_CODE_BASE` existence, the
  DPM/TA/ISP/TSP/PBE/PDS state map, and whether
  `EUR_CR_PDS_DMS_CTRL2`/`USE_MOE`/`USE_CTRL`/`USE_SPRV`/`USE_SECURITY` (all
  written by the vendor's launch) are set here.

## Superseded / uncertain

* **Session 75's "the event program is entered once and never again"** — its
  evidence was a static `ctlblk` word and a tick counter reading 0.  This
  session's corrected counter says **two** fetches and **one** tick.  The newer
  statement is current.  (Both readings are consistent with session 75 having
  also sampled a word the microkernel writes, but that is *not* proven — it
  should be re-measured, not cited.)
* **Every "fetched exactly once" claim in this note's appendix** — superseded by
  the corrected measurement; see "Dead ends".
* **HANDOFF-75 §0b's method** — superseded: wrong offset arithmetic; the answer
  is arithmetic, not a measurement.
* **Session 75's `EVENT_HOST_ENABLE = 0x20004000` as correct** — superseded: the
  TIMER bit there was our bug, not the vendor's shape.
* **"The bench was on the read-only dump module `9a0ebbe3…`"** — corrected; it
  was the sweep build (`a4aa13a9…`, srcversion `5C23DC94…`), whose node writes
  0x0a58 on read.
* **Report 253's llvmpipe install** — lost by the session-71 rebake; reinstated
  and re-measured here, and a reflash loses it again.

## Artefacts produced

* **Captures** (both new; indexed in `docs/captures/README.md`):
  `docs/captures/sgx540-vendor-ukernel-source-2026-11-26.txt` (vendor source,
  entry table, PDS patch indices, the retractions) and
  `docs/captures/sgx540-entry-counter-2026-11-26.txt` (the instrument, the
  corrected measurement, the re-emit rounds, an evidence inventory).
* **Driver changes** (`local/work/powervr_sgx`, gitignored; mirrored into
  `local/kernel-tree-sgx` to build — both copies must be updated or the build
  silently uses stale sources): OCP wrapper map/enable (`pvrsgx_of.c`,
  `pvrsgx_device.{c,h}`, `pvrsgx_hw.h`); the three PDS fixes,
  `FIX_HW_BRN_30852`, the `DMS_CTRL` write and the
  `UKFW_PDS_SIZE`/`UKFW_PDS_ADDR` patch values (`pvrsgx_fw.c`, `pvrsgx_pds.c`,
  `pvrsgx_core.{c,h}`, `pvrsgx_hw.h`); the `pds`, `timer_edge`, `relaunch`
  nodes and the DMS/OCP/register dump (`pvrsgx_debugfs.c`, `pvrsgx_dump.c`).
* **Microkernel changes** (`ukernel/ukernel.py`): the `init_set` TIMER OR
  removed; `UK_ENTRYLOG`, `UK_ENTRYFIRST`, `UK_EMITPDS`, `UK_REARM`;
  `Asm.end(rearm=…)`; `mkukfw.py` `VALUES` extended.
* **Firmware builds** (explicit output paths throughout): `hostirq`
  (`76519b2b…`), `tick2` (`7b44b33a…`, durable), `kick1`, `tick1`, `entry1`,
  `first2`, `fix1` (`f468a71d…`, **deployed at the close**), `emit1`,
  `rearm1/2/5/6/8/9` (`e3717aad…`).
* **Module builds**: `37a329a6…` (the session's fixes), then `c01f6490`,
  `e9575b9c`, `4c697fb5`, `41ae69d9`, `cd91ed5a`, `bb7a0c68…` (deployed at
  the close — fixes + `pds`/`timer_edge`/`relaunch` + the DMS/OCP dump lines),
  **`94eaea61…` (deployed at the wrap-up re-read**, which caught that the
  `relaunch` node's first "fix" had changed its comment but not its index).
* **Desktop**: llvmpipe + `libLLVM-13` installed (md5s in STATE); the catacomb
  gesture guard in `/etc/catacomb/initrc`.
* **No kernel patch change, no region-0 flash, no region-1 or hd3 image write.**
  Every device-side change was a **file** on hd3.

## Caution for the next session

Each GPU reset takes the WLAN down with it (`wlcore: Hardware recovery in
progress`, `ieee80211 phy0: Hardware restart was requested`) — measured three
times in a row.  Run the uAPI suite in small pieces rather than as a whole, and
expect `wlan0` to need bringing back up afterwards.  WiFi was **down** at this
session's close for that reason, not by choice.

## Note on reverting

* `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`f468a71d…`** (`fix1`, the
  corrected instrument, no experimental re-emit) — deliberately, so the next
  session inherits a valid counter.  The durable image is beside it as
  `sgx540_ukernel.bin.tick2` (`7b44b33a…`); the pre-session durable as
  `.before-hostirq` (`12f12abb…`, `invfix`); every variant tried this session as
  `.emit1w` / `.entry1*` / `.first2w` / `.fix1w` / `.rearm*w`.
* `powervr_sgx.ko` = **`94eaea61…`** (`bb7a0c68…` at the close); the pre-session
  module is `.dumpregs-before-ocp` (`9a0ebbe3…`), the intermediate experiments
  `.ocp` … `.rel2`.
* Region 0 untouched at `0x02C1DDC2`.  **Both device files are instrumented test
  builds; the durable pair is one `cp` away.**  No restore is required — the unit
  is on Linux by the operator's direction.

---

# Appendix — the chronological record, as written during the session

**Superseded material lives below this line and is kept only so the shape of the
mistakes stays visible.**  The sections above are current.  Where an addendum
says "fetched exactly once", it is wrong — see "Dead ends".  Full detail is in
the two captures.

## Addendum 1 — the vendor microkernel source, and a complete prebuilt image

The DDK 1.8 drop contains the SGX540 microkernel as source **and** as a finished
binary.  This material is also in STATE and in the capture; kept here for the
chronology.

* `services4/srvinit/devices/sgx/eventhandler.pds.asm` — the PDS program
  `EVENT_PDS_EXEC` points at: `movs douta, ir0, INPUT_IR0_PA_DEST`, branch on
  the bits, `halt` after `movs doutu, INPUT_GENERIC_DOUTU0/1/2`.  A pure
  dispatcher; no command processing.
* `EURASIA_PDS_IR0_EDM_EVENT_TIMER` = **bit 1** (`hwdefs/sgxdefs.h:3270`), vs
  b29 in `EUR_CR_EVENT_STATUS`, which this port polls.  The vendor's USSE
  EventHandler also reads `ui32PDSIn0/1` and never polls STATUS for the timer.
* The constants are host-patched at load (`sgxinit.c:2507-2510`); indices in the
  generated `eventhandler_pds.h`.  **Our loader patches only `ctlblk` and
  `pdtable`** — no PDS-input routing at all.
* Prebuilt: `binary2_540_omap4430_android_{debug,release}/…/ukernel.bin`, with
  `ukernel_labels.h`'s per-command entry table (`EVENTHANDLER 0x50`, `TIMER
  0x2AD0`, `GETMISCINFO 0xEE0`, …) and the 2D/3D/SPM/TA programs split out.  It
  decodes as SGX540 code with this repo's disassembler.
* **REFERENCE material, not a deployment target** (operator): *"the open driver
  should compile its own microkernel too, because we could write it cleaner and
  perhaps better, and the vendor might not be written to support or implement
  modern things mesa might need.  It's fine to read the vendor driver and
  microkernel source and to write our own."*  It cleared one suspicion: **rev 120
  defines no `FIX_HW_BRN_29104`**, so the vendor also ends a task with a bare
  `mov.end` and no `phas`, exactly as our `a.end()` does — not a difference, do
  not "fix" it.
* Also settled: `EUR_CR_EVENT_PDS_ENABLE`'s comment says *"PDS_ENABLE_SW_EVENT is
  ignored by the PDS — host kick events always enabled"*, which is why session
  75's bit 14 does not stick; and `ui32EventOtherPDSInfo` is derived at
  `sgxinit.c:3640` from four flag bits not compared against our `0x00013d01`.

## Addendum 2 — the entry counter: **withdrawn conclusion, sound instrument**

Operator: "run the instrumented boot."  Capture:
`sgx540-entry-counter-2026-11-26.txt`.

The instrument (`UK_ENTRYLOG`) is an unconditional counter at the top of the
`event` entry, right after `a.patch(17,"ctlblk")` and before the `ui32InitStatus`
test, plus a dispatch-pass STATUS snapshot.  Placement is the point: `r17` is
valid from the patch, so a ctlblk-relative store is safe there — only stores
through the derived `sa` attributes faulted at the entry top (session 74).
Verified before deploying: `test_emu.py` 8/8 on both cores, SGX545 golden
`83b5b0a7…`, the flag-unset build unchanged, `mkukfw.py --check` accepting the
wrapped file and rejecting the raw.

**What it read before the correction** — `entry = 1` at 44 s and 140 s with
`tick = 1`, and the claim *"fetched exactly once, at init, and never again"*, plus
"the 0x39 snapshot is decisive on its own".  **All withdrawn**: the counter was
on word 0x1c (`CLEANUP_DEFER_CT`), which `init_set` clears.  With it on word
0x07 the reading is **entry 2, tick 1** — see "Confirmed".

Two instrument errors were caught along the way and are in "Dead ends": the
narrow store offset field, and `0x07 * 4` used as a word index.

## Addendum 3 — llvmpipe restored, and the panel-blank root cause

Condensed above in "Confirmed"; full detail (md5s, sizes, the kprobe trace, the
catacomb source lines) is in STATE and trap 398.  The one thing not stated above:
the softpipe originals are kept as `swrast_dri.so.amber` /
`kms_swrast_dri.so.amber` (both `d8d69228…`), `libdrm` was already 2.4.134 so its
sub-libs were unchanged, and `ldconfig` was run with ownership fixed root:root.

## Addendum 4 — DMS_CTRL: a real defect found and fixed, and not the blocker

Condensed above in "Confirmed".  Kept for the sequencing: this was fixed *before*
the `CLEANUP_DEFER_CT` discovery, and at the time it was recorded as "the entry
counter stayed 1 across two samples 25 s apart" — a reading from the bad word.
The conclusion *"the EDM task count does not gate the fetch"* happens to survive
the correction, but it was not soundly established at the time.

This appendix's original closing claim — *"the remaining gap is USSE-side task
emission; emit the PDS task from our USSE entry so a fetch is always pending"* —
is the hypothesis that `rearm9` then tested and **answered negatively**.
