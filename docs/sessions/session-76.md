# Session 76 (absolute) — the SGX's interrupt reaches the CPU at last, and five defects fall with it

> Numbering: absolute `76` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era session 70**
> (`absolute = Linux-era + 6`).  Continues session 75, which measured that the
> event program is entered once and never again and that the `1/1 passed` was
> the driver's own 2 s timeout.

Date: 2026-11-26.  Unit at the end: **Linux `7.3.3 #5 SMP`** on hd3, region 0
**`0x02C1DDC2`**, region 1 **`20a5edd3…`**, `BOOTCTR_FCTRY` **`0x25`**, `nproc` 2
/ PID 1 affinity **`0,1`**, battery Full 4.115 V, `wlan0` down (the GPU resets
knocked it over — see the caution below), `card1` = `powervr_sgx`,
`firmware running: 1`, `uk-lockups 0`, `BIF_FAULT 0`.

## Goal of this session

HANDOFF-75 §0b: verify what `EVENT_PDS_EXEC` points at, then the doorbell, and
get the EDM to dispatch.  The operator's direction at the start was to stop
treating a boot as a cost — "there's no cost for a reboot/flash really" — so the
session moved from static reading to measuring as soon as a hypothesis was
testable.

## Confirmed (measured, read in a file, or seen on the bench)

**The interrupt had never been wired to the CPU, and that is the session's
result.**

* **The SGX's request reaches the CPU only through the OCP wrapper's gate.**
  On OMAP4 the core's interrupt is not wired to the GIC directly: it enters the
  `ti,sysc` target module's `IRQSTATUS_2` (window offset **0xfe38**) and the
  wrapper forwards it only while **`IRQENABLE_SET_2` (0xfe44) bit 0** is set.
  The DDK's own OMAP4 platform layer does exactly that
  (`eurasia_km/services4/system/omap4/sysconfig.c`, `SysEnableSGXInterrupts()`:
  `IRQSTATUS_2 <- 1` then `IRQENABLE_SET_2 <- 1`), and it maps the **whole
  64 KiB** window (`SYS_OMAP4430_SGX_REGS_SIZE` = 0xFFFF,
  `gpvOCPRegsLinAddr = gsSGXRegsCPUVAddr`).  On Linux the same drop defines
  **`SGX_OCP_NO_INT_BYPASS`** (`syslocal.h:86`), so `THALIA_INT_BYPASS` in
  `OCP_DEBUG_CONFIG` (0xff08) is deliberately **not** used there.
  Our driver mapped only **0x4000** (`PVRSGX_REGS_MIN`) and never touched the
  wrapper.  Measured before: `IRQENABLE_SET_2 = 0`, irq 147 **count 0**, while
  `EVENT_STATUS` b31 (MASTER_INTERRUPT) sat set.  With the gate opened, irq 147
  ran **0 → 100000** — the kernel's unhandled-interrupt limit, which is itself
  proof the line is now delivering.
* **Userspace cannot open the gate.**  A `/dev/mem` write to the wrapper is an
  **external abort** (the L4 firewall), so this can only be done in kernel code.
  Recorded so nobody tries the userspace route again.
* **The storm's cause is in our own microkernel, not the driver.**
  `init_set` ORed **`EVENT_TIMER` (b29) into `EVENT_HOST_ENABLE`**.  But
  `EVENT_HOST_ENABLE` says *which sources interrupt the host*, and `srvinit`
  writes **SW_EVENT only** (`0x4000`) — the DDK's ISR acks only SW_EVENT
  (`sgxinit.c`).  The **dispatch** mask is a different register,
  `EVENT_PDS_ENABLE` (0x0a58), which the driver already writes as `0x2204b00f`
  with TIMER in it.  So every 1 kHz tick raised a host interrupt that had to be
  rejected; the count reached 100000 and the kernel disabled the line, losing the
  real completion interrupts.  With the OR removed, `EVENT_HOST_ENABLE` reads
  `0x00004000` and no MASTER_INTERRUPT sticks.
* **`EVENT_PDS_EXEC` resolves exactly to the PDS0 buffer — for the first time
  compared against the buffer, not against our own derivation.**  A new `pds`
  debugfs node reads the register, subtracts `pds_pixel.base`, and prints both
  that result and the buffer's actual device address plus its words.  Trap 393
  said the old `check_event_pds()` could not detect a wrong target because both
  sides came from the same assumption; this compares the register with the
  memory the EDM fetches from.  It also *found* a defect (below).

**Three PDS launch-program defects, each then fixed.**

* **The launch program did not fit its slot.**  `layout[]` gives
  `PVRSGX_FW_PDS0` **0x18** bytes at `pds_base` and `PVRSGX_FW_PDS1` at **+0x20**.
  The single-context `edm_init` branch of `pvrsgx_pds_entry()` assembled with
  `data = 12`, i.e. code at word 12, needing words 0..13 — past the slot.  PDS1
  is built **after** PDS0 and its own program occupies words 8..21, so it
  **overwrote the launch program's `DS1[0]` (word 8) and its `DOUTU`/`HALT`
  (words 12/13)**.  Init still ran only because PDS1's `DOUTU` happens to encode
  the same code base and offset; the task's inputs were PDS1's, not this
  program's.  Fixed by using a **4-word data segment** (`data = 4`): words 0-2
  data, 4 `MOVS`, 5 `HALT`.  Verified through `pds`: `[0]=0x0020000f
  [1]=0x04000000 [2]=0x00000001 [4]=0x07000305 [5]=0xaf000000`.
* **`EVENT_PDS_DATA` described attributes the slot could not hold.**
  `EUR_CR_EVENT_OTHER_PDS_DATA_SIZE_ALIGNSHIFT` is **4**, so 0x0a6c holds
  `bytes >> 4`.  It said **3** = 48 bytes (a 12-word segment) for a 24-byte slot.
  Now **1** = 16 bytes, consistent with the 4-word segment.
* **`EUR_CR_PDS` (0x0abc) had never been written.**  Both the DDK 1.8 `srvinit`
  and our own SGX545 init list write it; on this core it holds
  `ATTRIBUTE_CHUNK_START` (b0-5) and **`DOUT_TIMEOUT_DISABLE` (b6)**, so the PDS
  data-store timeout was still armed.  Added to the SGX540 init list as `0x40`
  (chunk start 0 on a `UNIFIED_TEMPS_AND_PAS` core, timeout disabled).  Measured
  live: `0x00000040`.

**`FIX_HW_BRN_30852`: the PDS must not auto-gate its clock.**

* The SGX540's **rev-120** errata block defines it, commented *"turns off PDS
  auto clock gating"*.  `srvinit` writes `EUR_CR_CLKGATECTL_PDS_CLKG_ON` where
  every other unit gets `_AUTO`, and excludes `CLKGATESTATUS_PDS_CLKS` from the
  gating poll mask.  Our driver set **every** unit to AUTO: `CLKGATECTL` read
  **`0x000aa6aa`** and `CLKGATESTATUS` bit 7 (PDS) read **set** — the PDS was
  parked, which is exactly what "the event is raised and the program is never
  entered" looks like.  Added as a per-core quirk (`PVRSGX_QUIRK_BRN_30852`);
  `CLKGATECTL` now reads **`0x000a66aa`** (PDS field 1 = ON).

**Measured, and it corrects a natural misreading**

* **`submit-null-job-many`'s "600 null jobs in 2160 ms / ok" is NOT dispatch.**
  The boot logged **one** timeout for the whole run and the CCB's `issued`
  counter moved by **1**.  The driver's noop accounting advances on
  `pvrsgx_ccb_send()` before anything reaches the microkernel, so 600 submits
  are acknowledged host-side.  This is the trap-388 family again: the test's
  `ok` says nothing about the GPU.  Recorded so it is not read as progress.
* **`submit-null-job` passes exactly once per context, then fails.**  Combined
  with the tick counter reading **1**, that is a *real* dispatch that happens
  once and never again — the same statement from two directions.

**Bench facts re-read at the close (not carried from earlier notes).**

* Region 0 media trailer at `0x8100000 + 10450520 − 8`: `1f 2d c8 d7 c2 dd c1 02`
  = **`0x02C1DDC2`** — unchanged, no flash this session.
* Region 1, 64 MiB at raw `0x29000000`: sha256 **`20a5edd3…`** — unchanged.
* hd3: `/dev/loop0 on / type ext4 (rw,relatime,nobarrier)`, 56.5 G, 733.6 M used.
* `BOOTCTR_FCTRY` **`0x25`** (`dmesg | grep pbrpmb`: `0x25 >= threshold 0x1e, no
  write`).
* Battery **Full, 100 %, 4.115 V**; backlight `bl_power` 0.

## Inferred (reasoning from evidence, not directly measured)

* **The remaining fault is in the microkernel task's admission to the EDM, not
  in any host configuration.**  Strength: strong as a location.  Every host-side
  input is now measured correct — `EVENT_PDS_ENABLE` carries TIMER (0x2204b00f),
  `EVENT_PDS_EXEC` resolves exactly to the PDS0 buffer, `EVENT_PDS_DATA` is
  consistent, `EUR_CR_PDS` has the timeout disabled, the PDS clock is forced ON,
  and `EVENT_TIMER` cycles at 1 kHz.  The tick counter still reads 1 and clearing
  `EVENT_STATUS` b29 through `EVENT_HOST_CLEAR` does **not** revive it (measured
  with a new `timer_edge` node).  So the EDM is not re-fetching the program, and
  nothing the host sets changes that.
* **It is the same class as session 74's "a store at the top of the event entry
  faults".**  That finding showed the event task's context can execute (init
  runs) but its admission is not "the task began".  Both point at how the
  microkernel task is entered, not at the register programming around it.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"§0b's experiment will tell us where the exec address points."**  The
  method as written could not work: it said to index `0x804090` bytes into
  `fw.cpu[PVRSGX_FW_PDS0]`, which is a **6-word (0x18-byte) buffer** — reading
  8 MiB past it.  And the target was already determined arithmetically
  (`0x804090 + pds_pixel.base = kernel_code.base + ALIGN(image, 0x10)`), so a
  boot on it would have confirmed an identity the driver itself constructed.
  Replaced with the `pds` node, which compares against the buffer.
* **"The bench is on the read-only dump module `9a0ebbe3…`."**  It was not:
  `/sys/module/powervr_sgx/srcversion` was `5C23DC94…`, which is the **sweep**
  build (`a4aa13a9…`) — the one whose node *writes 0x0a58 on read*.  Found by
  srcversion before the module was replaced, which is trap 390's lesson applied
  to a module rather than a firmware.
* **"Opening the gate should fix dispatch."**  It fixed *delivery* (irq 147: 0 →
  100000) and exposed a second bug (the storm), but not dispatch.  Two
  independent defects were stacked here.
* **"`/dev/mem` can write the OCP wrapper."**  It faults: the L4 firewall
  rejects user-mode access, the same class as trap 62's GPMC finding.  Kernel
  code only.
* **"600 null jobs in 2.1 s means the microkernel is dispatching."**  Killed by
  the CCB `issued` counter (+1) and one timeout in `dmesg`.
* **"`CLKGATESTATUS` bit 7 set means the PDS is running."**  It means the
  opposite — the bit reports a *gated* unit, and overriding it to always-ON is
  precisely what `FIX_HW_BRN_30852` requires.

## Unknown / open questions

* **Why does the EDM stop after one dispatch?**  *Settle by:* a `GETMISCINFO`
  submit (needs no client PD) — if the doorbell path also fails, the PDS/EDM
  execution itself is the fault rather than event routing.  The `pds` node makes
  the fetched bytes visible for comparison with the manifest.
* **Does the event program run at all past init?**  The tick counter says no;
  `SC_VERSION` says init completed.  The distinction to test is whether a
  *second* PDS fetch ever happens.
* Unchanged and still open: `0xb14`'s write (trap 387), `sgx540_init_regs`'
  remaining values, the microkernel register tables (0x618/0x62c/0x694/TA 0x200/
  3D 0x428, all hypothesis), `USE_CODE_BASE` existence, and the
  DPM/TA/ISP/TSP/PBE/PDS state map.

## Superseded / uncertain

* **HANDOFF-75 §0b's method** — superseded: the offset arithmetic was wrong and
  the answer is arithmetic, not a measurement.
* **Session 75's `EVENT_HOST_ENABLE = 0x20004000` as correct** — superseded: the
  TIMER bit there was our bug, not the vendor's shape.
* **The claim that the bench was on the dump module** — corrected; it was the
  sweep build.

## Artefacts produced

* Driver changes, all in `local/work/powervr_sgx` (gitignored) and mirrored into
  `local/kernel-tree-sgx` to build:
  `pvrsgx_hw.h` (OCP wrapper constants + `pvrsgx_ocp_irq()`, PDS constants),
  `pvrsgx_device.h` (`@ocp`, the gate helper), `pvrsgx_device.c` (map/enable/
  disable), `pvrsgx_of.c` (map the wrapper page for the 540),
  `pvrsgx_core.{h,c}` (`PVRSGX_QUIRK_BRN_30852`, `clkgatectl_pds`, `EUR_CR_PDS`),
  `pvrsgx_fw.c` (the quirk in the CLKGATECTL write, `EVENT_PDS_DATA` = 1),
  `pvrsgx_pds.c` (the 4-word launch segment),
  `pvrsgx_debugfs.c` (new `pds` and `timer_edge` nodes),
  `pvrsgx_dump.c` (the OCP state line).
* Firmware builds, explicit output paths throughout (the session-75 lesson):
  `hostirq` (md5 `76519b2b…`, HOST_ENABLE fix alone) and `tick2` (md5
  `7b44b33a…`, the deployed one, with `UK_TICKLOG`/`UK_KICKLOG`/`UK_CCBLOG`).
* Module builds on hd3 as `.ocp`, `.ocp2`, `.pdsnode`, `.pdsfit`, `.edge`,
  `.brn30852`, `.pdsstat`, `.pdsdata`; the installed one is the last
  (`37a329a6…`).
* `ukernel/ukernel.py` — the `init_set` OR of TIMER into `EVENT_HOST_ENABLE`
  removed, with the reasoning recorded in place.
* **No kernel patch change, no region-0 flash, no region-1 or hd3 image write.**
  Every device-side change was a **file** on hd3.

## Note on reverting

Reverted and proven by `md5sum` at the close:

* `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`7b44b33a…`** (`tick2`,
  the instrumented build that is *deployed on purpose* for this measurement).
  The pre-session durable image is beside it as `.before-hostirq`
  (`12f12abb…`, `invfix`).
* `/lib/modules/7.3.3/kernel/drivers/gpu/drm/powervr_sgx.ko` = **`37a329a6…`**
  (all five fixes plus the `pds`/`timer_edge` nodes).  The pre-session module is
  `.dumpregs-before-ocp` (`9a0ebbe3…`).

Region 0 is untouched at `0x02C1DDC2`.  **The firmware and module on hd3 are
both instrumented test builds; restoring the durable pair is a `cp` from
`.before-hostirq` and `.dumpregs-before-ocp`.**  No restore is required by the
project's rules — the unit is on Linux by the operator's direction — but the next
session should know the durable copies are one `cp` away.

## Addendum 2 — the vendor microkernel SOURCE, and a complete prebuilt image

**The DDK 1.8 drop contains the SGX540 microkernel as source AND as a finished
binary.**  HANDOFF §5 predicted the source might answer §0b; it does, and it
changes the shape of the remaining work.  Full extract:
`docs/captures/sgx540-vendor-ukernel-source-2026-11-26.txt`.

* **The event path is a PDS dispatcher, and `EVENT_PDS_EXEC` points at it.**
  `services4/srvinit/devices/sgx/eventhandler.pds.asm` is a 14 KB PDS program
  that copies the EDM's interrupt registers into a host structure
  (`movs douta, ir0, INPUT_IR0_PA_DEST`), branches on those bits, and `halt`s
  after `movs doutu, INPUT_GENERIC_DOUTU0, INPUT_DOUTU1, INPUT_DOUTU2`.  It does
  no command processing — it is a pure dispatcher into the USSE EventHandler.
* **The event bits live in `ir0`/`ir1`, not in `EUR_CR_EVENT_STATUS`.**
  `EURASIA_PDS_IR0_EDM_EVENT_TIMER` is **bit 1** (hwdefs/sgxdefs.h:3270); the
  same event is **bit 29** in `EUR_CR_EVENT_STATUS`, which is what this port has
  been polling.  The vendor's USSE EventHandler also reads `ui32PDSIn0/1`
  (eventhandler.use.asm:112-141, 324, 369) and never polls STATUS for the timer.
* **The constants are patched by the host at load.**  `eventhandler.pds.asm`
  declares `data dword INPUT_IR0_PA_DEST` etc.; `sgxinit.c:2507-2510` fills
  them via `PDSUKERNEL_EVENTSSetINPUT_*`, and the auto-generated
  `eventhandler_pds.h` gives both the compiled 220-word program and the exact
  words: IR0 at `[17]`, IR1 at `[20]`, timestamp `[16]`, DOUTU1
  `[1],[5],[9],[13]`, DOUTU2 `[29]`, generic DOUTU0 `[12]`, 3D `[0]`, TA `[4]`,
  SPM `[8]`.  **Our loader patches only two limm sites in total** (`ctlblk`,
  `pdtable`) and does no PDS-input routing at all.
* **A complete prebuilt vendor SGX540 microkernel is in the drop**:
  `binary2_540_omap4430_android_{debug,release}/…/ukernel/ukernel.bin`
  (22 192 B `cfc577aa…` / 19 984 B `ff69e400…`), with `ukernel_labels.h` giving
  the per-command entry table (`EVENTHANDLER 0x50`, `TIMER 0x2AD0`,
  `GETMISCINFO 0xEE0`, …) and the 2D/3D/SPM/TA programs split out as well.  The
  disassembler decodes it as SGX540 code.  Every image this port has run is our
  own SGX545 retarget; this is the vendor's own 540 build.
* **It is not a drop-in** — the vendor `.bin` predates this port's `ukfw_header`
  format — but the adapter is bounded, because all three inputs are now known.
* **Also settled:** `EUR_CR_EVENT_PDS_ENABLE`'s comment says *"PDS_ENABLE_SW_EVENT
  is ignored by the PDS — host kick events always enabled"*, which is the reason
  behind session 75's finding that bit 14 does not stick; and
  `ui32EventOtherPDSInfo` is derived at sgxinit.c:3640 from four flag bits we
  have not compared against our `0x00013d01`.
* **One thing I got wrong and retracted in place:** I first recorded "set
  `event_pds_data0` to 9, one-word fix".  `144 >> 4` really is the vendor's
  `EUR_CR_EVENT_OTHER_PDS_DATA` value, but that segment belongs to the vendor's
  220-word dispatcher; our PDS0 is a 4-word launch stub whose 16-byte segment is
  genuinely `1`.  The capture records the retraction.

**This is the decision the next session starts from:** keep retargeting our
reassembly, or write the adapter and run the vendor's own image.  The second is
much more likely to dispatch, because it is the binary that did.

## Caution for the next session

Each GPU reset takes the WLAN down with it (`wlcore: Hardware recovery in
progress`, `ieee80211 phy0: Hardware restart was requested`) — measured three
times in a row.  Run the uAPI suite in small pieces rather than as a whole, and
expect `wlan0` to need bringing back up afterwards.  WiFi was **down** at this
session's close for that reason, not by choice.

## Addendum — llvmpipe restored, and the panel-blank root cause

Two user-facing items were worked after the SGX measurements.

### llvmpipe is back on the live root (the rebake had dropped it)

The desktop ran on plain softpipe: `/usr/lib/xorg/modules/dri/swrast_dri.so` was
6.9 MB with **no `libLLVM*` on the root at all**, i.e. report 253's llvmpipe
install had been lost when the session-71 rebake rebuilt the root.  From the
cached Alpine Mesa 21.3.9 gallium build (`local/work/llvmpipe/`):

| file | md5 | size |
|---|---|---|
| `…/dri/swrast_dri.so` | `1db573b1…` | 15 806 052 |
| `…/dri/kms_swrast_dri.so` | `1db573b1…` (same gallium blob) | 15 806 052 |
| `/usr/lib/libLLVM-13.so` | `d1b49590…` | 79 574 652 |

Softpipe originals kept as `swrast_dri.so.amber` / `kms_swrast_dri.so.amber`
(both `d8d69228…`).  `libdrm` was already 2.4.134 so its sub-libs were unchanged;
`ldconfig` run, ownership fixed to root:root.  After a reboot `epitaph` maps
`swrast_dri.so`+`libLLVM-13.so` and `catacomb` maps `kms_swrast_dri.so`+
`libLLVM-13.so`; a surfaceless probe reports
`GL_RENDERER = llvmpipe (LLVM 13.0.1, 128 bits)`.  An offscreen 1024×585
`glClear+glFinish` benchmark measures **llvmpipe 4.3 ms vs softpipe 48.0 ms** per
frame (~11×), and the desktop is visibly snappier.  **Owed:** the flasher hd3
image still carries neither llvmpipe nor `libLLVM-13`; the rebake must add both.

### The panel blank was the double-tap sleep gesture, not a rendering race

llvmpipe made the desktop faster but did **not** stop the fast-tap blank, which
ruled out the "taps outrun the frame loop" theory and pointed at the input path.
kprobes (`drm_panel_disable`, `drm_panel_enable`, `bl_power_store`,
`backlight_notify_blank`) over a fast-tap burst gave **4 disable + 4 enable, all
`comm="catacomb"`**, `bl_power_store` **0** and nothing from the fb-blank path —
so the toggle is catacomb calling the panel path, while the DRM connector's
`dpms` stayed `On` (a manual `catacomb msg dpms off` moves *both*, `bl_power=4`
*and* `dpms=Off`).  Reading catacomb's source found it: `on_double_tap()`
(`src/windows/mod.rs`) puts `set_display_status(false)` on the **centre 20%** of
the bottom gesture handle, and `src/input.rs` calls `set_display_status(true)` on
**every** double-tap.  A double-tap is two taps within 300 ms and ~44 px, which
is exactly the observed rate dependence (1 s fine, 0.5/0.2 s not); the operator's
taps sat at x≈482–516 of 1024, dead centre.  The measured cycle was
`enable @196.06 → disable @196.09` (**34 ms**); that sub-frame ON→OFF tears the
DSI down while it re-initialises (~2.5 s) — the trap-298 wedge, backlight on and
picture dead until `reboot -f`.

Fix applied and made durable: `catacomb msg gesture-handle --height 0` at the end
of `/etc/catacomb/initrc` (new md5 `56888215…`, previous kept as
`initrc.pre-gesture0`).  `ui_rect(false)` subtracts the handle height, so
`on_double_tap`'s `point.y < bottom` guard always returns and the notch zone no
longer exists.  Measured **0** disable/enable after the fix and the panel stayed
lit.  Trade-off: the handle's window-cycle gestures are gone.  Recorded as
**trap 398**.
