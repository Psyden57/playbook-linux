# Handoff — session 76 (absolute) → session 77

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 77 = Linux-era 71**, written at the
session-76 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 76 RESULT, and it is a correction: the SGX's interrupt now reaches
> the CPU (it never had), four more real defects fell with it, and the entry
> counter this session built turned out to be reading a control-block word the
> microkernel itself clears — so session 75's "entered once and never again" and
> this session's own three "fetched exactly once" conclusions are all withdrawn.
> With a sound counter the event program is fetched TWICE: one init pass and one
> dispatch pass, and the dispatch pass is the timer's.  A PDS-task emit from the
> USSE task end then measurably EXECUTES and produces NO re-fetch.**  Report
> **279**; traps **398–404**; DECISIONS **§114/§115**; captures
> `docs/captures/sgx540-{vendor-ukernel-source,entry-counter}-2026-11-26.txt`.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**, with
a GPU-accelerated desktop (compositor **and** clients), and a modern browser.

**The long-term graphics route is the open `powervr_sgx` stack** (DECISIONS
§107/§108): the closed pvr DDK's GBM cannot export dmabufs, which is what blocks
the browser (report 275).  This is that port.

## 0a. What session 76 established (do not re-derive)

* **The interrupt was never wired to the CPU.**  On OMAP4 the SGX's request
  enters the `ti,sysc` wrapper's `IRQSTATUS_2` (window **0xfe38**) and is
  forwarded only while `IRQENABLE_SET_2` (**0xfe44**) bit 0 is set.  Our driver
  mapped 0x4000 of a 64 KiB window and never touched it; the vendor's OMAP4 code
  enables exactly that pair on every clock-enable.  With it opened, **irq 147 ran
  0 → 100000** (the kernel's unhandled-IRQ limit).  Userspace cannot do this — a
  `/dev/mem` write is an L4 external abort.
* **The storm was ours.**  `init_set` ORed TIMER (b29) into
  `EVENT_HOST_ENABLE`; `srvinit` writes SW_EVENT only (`0x4000`).  The *dispatch*
  mask is `EVENT_PDS_ENABLE` (0x0a58), a different register.  Fixed.
* **Four defects around it, all fixed and measured:** the `edm_init` launch
  program assembled a 12-word segment into PDS0's 0x18-byte slot (PDS1 at +0x20
  overwrote it) — now 4 words; `EVENT_PDS_DATA` said 3 (48 B) for a 24 B slot —
  now 1; `EUR_CR_PDS` (0x0abc) was never written — now `0x40`
  (`DOUT_TIMEOUT_DISABLE`); `FIX_HW_BRN_30852` (the PDS must not auto-gate) —
  `CLKGATECTL` `0x000aa6aa` → `0x000a66aa`.
* **`EUR_CR_DMS_CTRL` (0x0a74) was never written at all** — it read
  `0x00208200` (the reset default, EDM 32 tasks) where the vendor derives
  `0x003f8200` (EDM **63**, `FIX_HW_BRN_23632`).  Fixed per-core; **it did not
  change the symptom**, so do not re-run it expecting one.
* **The event program is fetched TWICE, and the tick counter reads 1.**
  Corrected instrument on control-block **word 0x07**, firmware `fix1`
  (`f468a71d…`, deployed): `entry = 2`, `tick (0x2c) = 1`,
  `SC_VERSION (0x18) = 0x10000`, stable over 94 s **and unchanged across eight
  host kicks**.  One init pass, one dispatch pass, and the dispatch pass is the
  timer's.
* **A PDS-task emit from the USSE task end executes and does NOT re-fetch.**
  `UK_REARM` puts the vendor's own instruction — verified byte-identical —
  `emit.t2 r20, r18, r16 [f12 f13 imm=2115]` on `tmr_tick_end`.  The one-shot
  flag goes 1 → 0 (so it ran), and `entry`/`tick` do not move.  Control `fix1`,
  the same instrument without the emit, reads identically.
* **Emitting from `init_set`'s task end faults the launch outright:**
  `microkernel did not start (EDM launch faulted: BIF int 0xe000 fault
  0xfef4000)`, probe `-5`, no `card1`.  The EDM must not be re-emitted while it
  is starting.
* **The vendor's DDK 1.8 drop (`/home/psyden/Pdrive/GFX_Linux_DDK-master`,
  outside the repo) contains the SGX540 microkernel as source AND as a prebuilt
  binary.**  Its event program is a **PDS dispatcher** over `ir0`/`ir1` — and
  **`EURASIA_PDS_IR0_EDM_EVENT_TIMER` is bit 1 there, versus b29 in
  `EUR_CR_EVENT_STATUS`, which is what our entry polls.**  The dispatcher's
  `INPUT_*` constants are host-patched at load (`sgxinit.c:2507-2510`; word
  indices in the generated `eventhandler_pds.h`: IR0 `[17]`, IR1 `[20]`,
  timestamp `[16]`, DOUTU1 `[1],[5],[9],[13]`, DOUTU2 `[29]`, generic DOUTU0
  `[12]`), and **our loader patches only `ctlblk` and `pdtable`** — no PDS-input
  routing at all.  Reference only, never deploy it (DECISIONS §115).  Capture:
  `docs/captures/sgx540-vendor-ukernel-source-2026-11-26.txt`.
* **The desktop is back on llvmpipe** (the session-71 rebake had dropped it):
  4.3 ms vs softpipe's 48.0 ms for a 1024×585 clear+finish,
  `GL_RENDERER = llvmpipe (LLVM 13.0.1, 128 bits)`, operator-visible.  **The hd3
  flasher image still lacks llvmpipe and `libLLVM-13`** — a reflash loses it.
* **The fast-tap panel blank is catacomb's own double-tap sleep gesture**, and
  the guard (`gesture-handle --height 0`) is **temporary** — the real fix is one
  guarded line in catacomb (DECISIONS §114).  kprobe-measured, not inferred.

## 0b. THE ONE EXPERIMENT FOR SESSION 77

**Question: does the *kick* path reach the microkernel at all — i.e. is there a
task end, on any path, after init?**

Why this is the leader.  The re-emit is correctly placed, correctly gated, and
measurably fires — and produces no re-fetch.  Eight host kicks change neither the
entry count nor the tick, and the `kick`/empty-CCB counters read 0.  So the whole
re-emit question is *blocked* on something simpler: an idle boot apparently never
reaches a second task end at all, and there is nothing for a re-emit to repeat
and nothing to measure.  Find out whether the kick path executes before anything
else.

**Method — one boot, one firmware flag, no new machinery.**  The `kick` counters
`0x3d`/`0x3e` read 0 in every build, including ones where the path reportedly
ran, so they cannot be trusted as they stand.  Re-instrument the `kick` entry
with a counter on a control-block word **proven free**, exactly as `UK_ENTRYLOG`
now is on 0x07:

1. **Verify the candidate word mechanically, first.**  Of every constant in
   `local/work/powervr_sgx/ukernel/cores/sgx540.py` that is reached through `r17`,
   and of every `st.a.d/ld.a.d rN, r17, #…` in `ukernel.py`, no other site may
   touch it.  **This is the check that failed invisibly this session**
   (0x1c = `CLEANUP_DEFER_CT`); do it before writing any store.
2. Put the counter as the **first instruction of the `kick` entry**, so it counts
   the entry rather than any later state.
3. Build with `UK_TICKLOG=1 UK_ENTRYLOG=1` too, so `entry`/`tick` come from the
   same dump, and deploy through `mkukfw --manifest` (the operands must be
   patched; recipe in §3).
4. Read `ctlblk` **before** and **after** submitting a job with
   `/tmp/powervr_sgx_test` (it is on hd3's ext4 and survives a reboot).

**What each result means:**

* **`kick` counter stays 0 while `resets` moves** → the core resets before the
  event program is ever fetched for a kick.  That is the trap-388 timeout path,
  so the fault is upstream of the event program (job submission, CCB, doorbell).
  The next question is then the doorbell: `EUR_CR_EVENT_KICK` (0xac8) /
  `EVENT_KICKER` (0xac4), read from inside `ccb_write()` immediately after the
  write — a read from `status` proves nothing, it is self-clearing.
* **`kick` climbs but `entry` does not** → the doorbell works and the USSE `kick`
  path runs, but the event program is not involved.  Then our entry's
  `EUR_CR_EVENT_STATUS` poll is the wrong design for kicks, and the vendor's
  `ir0`/`ir1` dispatch (§0a) is the thing to adopt.
* **Both climb and `entry` goes above 2** → contradicts §0a's "unchanged across
  eight kicks".  Re-measure before building on it; the first suspicion is the
  counter's word.
* **`resets` does not move and nothing climbs** → the jobs never reached the
  core.  Go back to `ccb wo`/`ro`, and treat `issued` only as a submission count
  (it is host-side, `pvrsgx_ccb.c:93`).

**Do not** spend this boot on the re-emit (blocked on this answer), on
`EUR_CR_DMS_AGE` (underivable from the DDK 1.8 drop — every header grepped), or
on the `ir0`/`ir1` rewrite (a design change that should follow a measurement).

## 0c. The media, and the way back

* **region 0 = `0x02C1DDC2`** — `boot/chainloader/kernel.img`, 10 450 520 B,
  sha256 `70474c01…`, media trailer `1f 2d c8 d7 c2 dd c1 02` (read on media
  this session).  Kernel `7.3.3`, ARMv7, no `maxcpus`, closed DDK off, `panic=0`.
* **region 1 = `20a5edd3…`** (64 MiB at raw `0x29000000`) — the BusyBox fallback
  root, not booted.
* **hd3 = the session-71 rebake** `5d3eac8f…`, flashed at loader block `0x4915`,
  grown to 62.5 G; live root `/dev/loop0` ext4 rw.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`; qconn `169.254.0.1:9090`, `PB_LINK=door tools/pbssh.sh`.
* **To stock TabletOS:** the 1917 IFS at `0x825` **and**
  `local/work/rcfs-1917.rcfs` at `0x8D5`, plus the vendor UFS at `0x4915` if
  hd3 is repurposed.
* **Flash loop:** `python3 tools/bbusb.py flash --component <img>
  --dest-block 0x4915 --from-bootrom --password 0000 --apply`; **start the
  flasher, then reset with `reboot -f`**.  A region-0 flash is block `0x825`.
* **A region-0 flash needs the initramfs rebuilt too.**  `make … zImage` after
  `rm -f $K/usr/initramfs_data.cpio*` — and if the zImage does not relink, the
  cpio is embedded from the *old* one.  Verify by extracting the firmware from
  the built cpio and comparing md5s.

## 1. Bench state at the session-76 close (read on the unit)

| item | value |
|---|---|
| OS | **Linux `7.3.3`**, `Nura edge` — not TabletOS.  **No restore required** |
| region 0 | **`0x02C1DDC2`** — no flash this session.  File `boot/chainloader/kernel.img`, 10 450 520 B, sha256 `70474c01…` |
| region 1 | sha256 **`20a5edd3…`** (unchanged) |
| hd3 | the session-71 rebake **`5d3eac8f…`** at `0x4915`, grown to 62.5 G; `/dev/loop0` ext4 rw, 56.5 G, 733.6 M used |
| `BOOTCTR_FCTRY` | **`0x25`** (`>= threshold 0x1e, no write`).  `/init` refills below `0x1e` |
| CPU | `nproc` 2; PID 1 affinity **`0,1`** |
| battery | Full, 100 % |
| GPU | `card1`/`renderD129` = **`powervr_sgx`**, `open microkernel fix1`, `EDM init: event status 0x20000000, BIF int 0x8000`, `microkernel started`, `BIF_FAULT 0`; `card0`/`renderD128` = `omapdrm`; irq 147 delivers |
| firmware | `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`f468a71d…`** (`fix1` — the *corrected* entry instrument, deliberately not the experimental re-emit).  Durable instrumented image beside it `.tick2` `7b44b33a…`; pre-session durable `.before-hostirq` `12f12abb…` (`invfix`); this session's variants `.emit1w`, `.entry1{,w,bak}`, `.first2w`, `.fix1w`, `.rearm{1,2,5,6,8,9}w` |
| module | `powervr_sgx.ko` = **`bb7a0c68…`** (the session's fixes + the `pds`/`timer_edge`/`relaunch` nodes + the DMS/OCP dump lines).  Pre-session: `.dumpregs-before-ocp` `9a0ebbe3…` |
| desktop | **llvmpipe** for compositor *and* clients (`swrast_dri.so` and `kms_swrast_dri.so` both `1db573b1…`, `libLLVM-13.so` `d1b49590…`; softpipe backups `.amber` `d8d69228…`).  4.3 ms/frame vs softpipe 48.0 ms |
| panel | `bl_power` 0, connector `On`, **0 DSI errors**.  Gesture handle 0 height in `/etc/catacomb/initrc` (`56888215…`) — a **temporary guard**, DECISIONS §114 |
| WLAN | **`wlan0` DOWN** — the GPU resets knocked it over.  `usb0` `169.254.0.1/16` UP |
| SSH | `PB_LINK=usb tools/pbssh.sh` works; if it times out check the cable, the host VPN (trap 341) and ufw (trap 342) |
| device-side files | the firmware/module variants above, `/etc/catacomb/initrc`, `/tmp/powervr_sgx_test` (**`/tmp` is on the ext4**, trap 354 — it survives a reboot) and this session's one-off scripts |
| host | clone `local/work/powervr_sgx` (`95f78ca` + our patches); assembler `local/work/sgxasm/sgx-asm`; trees `local/kernel-tree` (good) and `local/kernel-tree-sgx` (open driver); **GPL-2 DDK 1.8 reference at `/home/psyden/Pdrive/GFX_Linux_DDK-master` (outside the repo)** |

## 2. Working rules (unchanged, plus what this session cost)

* Read `docs/project/SAFETY.md` before any write.  One variable per boot.  An
  agent never writes RPMB; NVRAM read-only.
* Kernel printk goes to the panel; `console=ttyGS0` is inert.  Use `reboot -f`.
  **Ask the operator what is on the panel**; do not screenshot.
* **Do not restart `tinydm`** (trap 353), and **do not leave Kumo running or kill
  it** (trap 367) — reboot.
* The eMMC can log `Card stuck being busy`; a power cut can damage the rw ext4
  (trap 336).  **Do not call `sync`** (trap 300).  `/tmp` is on the ext4
  (trap 354).
* **Register reads:** only from inside the driver now (trap 384); the userspace
  `/dev/mem` path is closed in this configuration.
* **Revert experiments and prove the revert.**  Look up every external fact.
  Write the durable fact into the repository in the same turn.
* **NEW, the session's most expensive lesson: a "free" control-block word is only
  free if no store anywhere touches it.**  `0x07` is (verified by a scan of the
  core's constants); **`0x1c` is `CLEANUP_DEFER_CT`**, which `init_set` clears.
  Read an existing, working use of the same constant and copy its form — and
  **`ld.a.d`/`st.a.d` immediates and the `sa` word selector are WORD indices**,
  while several nearby expressions are byte offsets.  Three of this session's
  bugs were `* 4` applied to a word index.
* **NEW: a tool's silence is evidence about the invocation first.**  An `emitpds`
  count of 0-versus-0 and a "the assembler rejects this" both came from running
  the tool wrongly (`sgx-asm` takes **stdin with `-b`**).
* **NEW: a hand-written cross-check is not a control.**  When two firmware builds
  are compared, confirm they differ in exactly one thing; `rearm8` was cited as
  `rearm9`'s control and differs in its gate too.  Use `fix1`.
* **Build both copies of the driver.**  `local/work/powervr_sgx` is the source of
  truth but `local/kernel-tree-sgx` is what compiles; a stale copy there built a
  module with none of this session's changes, twice.
* **Firmware builds must use explicit output paths** (a default-output build
  writes `sgx540_ukernel.bin` and clobbers the offline emulator's input), **and
  `build.py`'s output is raw** — it must be wrapped by
  `python3 drm/tools/mkukfw.py IMG -o OUT --manifest IMG.json`, or the driver
  rejects it ("not a microkernel file").
* **A new `ukfw_patch_value` must be inserted BEFORE `UKFW_PDTABLE`**, because
  `ukfw_parse()` range-checks `> UKFW_PDTABLE`; appending it fails the load with
  "bad entry or patch table" and the GPU never binds.  `mkukfw.py`'s `VALUES`
  must match the enum order.
* **A module change costs a reboot** (`modprobe -r` is impossible while elogind
  holds `card1`).  **Never unbind pvrsrvkm live** (trap 383).

## 3. The build and deploy recipe, exactly (all of it was run this session)

```bash
cd local/work/powervr_sgx
# 1. firmware: raw image + manifest, EXPLICIT output paths
SGX_ASM=$PWD/../sgxasm/sgx-asm UK_TICKLOG=1 UK_ENTRYLOG=1 UK_BUILD_ID=<id> \
  python3 ukernel/build.py sgx540 /tmp/<id>.bin /tmp/<id>.json
# 2. gates: both must pass before deploying
SGX_CORE=sgx540 SGX_ASM=$PWD/../sgxasm/sgx-asm python3 ukernel/test_emu.py   # 8/8
SGX_ASM=$PWD/../sgxasm/sgx-asm python3 ukernel/golden/check.py                # 83b5b0a7…
# 3. wrap it -- build.py's output is raw and the driver will reject it
python3 drm/tools/mkukfw.py /tmp/<id>.bin -o /tmp/<id>w.bin --manifest /tmp/<id>.json
python3 drm/tools/mkukfw.py --check /tmp/<id>w.bin                            # ok
# 4. push (hd3 has NO scp/sftp-server -- trap 391)
cat /tmp/<id>w.bin | tools/pbssh.sh 'cat > /lib/firmware/powervr_sgx/<name>; md5sum /lib/firmware/powervr_sgx/<name>'
# 5. module: build BOTH source copies, then
. tools/env.sh
cp local/work/powervr_sgx/drm/powervr_sgx/*.c local/work/powervr_sgx/drm/powervr_sgx/*.h \
   local/kernel-tree-sgx/drivers/gpu/drm/powervr_sgx/
cd local/kernel-tree-sgx && make ARCH=arm CROSS_COMPILE=$CROSS_COMPILE \
  M=drivers/gpu/drm/powervr_sgx modules
```

## 4. Useful local extracts

* `docs/captures/sgx540-entry-counter-2026-11-26.txt` — **the instrument, the
  corrected measurement, the re-emit rounds, and an evidence inventory saying
  which of this session's numbers may be cited.**  Read it before quoting
  anything from session 76.
* `docs/captures/sgx540-vendor-ukernel-source-2026-11-26.txt` — the vendor's PDS
  dispatcher, the entry table, the PDS patch indices, and four retractions.
* `docs/reference/sgx540-factsheet.md` — the SGX540 facts, **C** where measured.
* `docs/reference/sgx540-ddk18-facts.md` — every K fact from the DDK 1.8 drop.
* The session-75 captures (`sgx540-{dispatch-regs,pds-enable-sweep,tick-counter,
  ukernel-kick-counter}-2026-11-26.txt`) — read with this session's correction in
  hand: their *register* readings stand, their **entry counts do not**.
* `rootfs/powervr-sgx/` — the patch series 0001–0007; applying 0001→0007 to a
  pristine `95f78ca` reproduces every deployed artifact.  **Nothing is upstream**
  (DECISIONS §110).
* `local/work/powervr_sgx/ukernel/` — the microkernel; `build.py`, `test_emu.py`
  (8/8 per core), `golden/check.py` (the SGX545 byte-identity gate),
  `cores/{sgx540,sgx545}.py` (all per-core values live here).
* Reports **279** (this session), 278 (its predecessor), 277, 276, 275.

## 5. What could not be verified

* **Every entry-count figure in session 75, and in this note's own appendix
  addenda** — measured on control-block word 0x1c.  The corrected figure is
  `entry = 2`.  Session 75's "entered once" should be re-measured, not cited.
* **Why the entry stops at 2 and the tick at 1**, and **who re-emits** — see
  §0b; the kick path is the next thing to establish.
* **Whether host kicks reach the microkernel at all.**  `resets` moves; the kick
  counters 0 and `ccb wo 0 ro 0` say the path may not be running.
* **`EUR_CR_DMS_AGE` (0x0a78)** is unprogrammed and cannot be derived: the vendor
  writes `ENABLE | PVRSRV_SGX_MAX_DMS_AGE`, and **`PVRSRV_SGX_MAX_DMS_AGE` is not
  defined anywhere in the DDK 1.8 drop** (every header grepped).
* **Whether `EUR_CR_PDS_DMS_CTRL2`, `USE_MOE`, `USE_CTRL`, `USE_SPRV`,
  `USE_SECURITY`** — all written by the vendor's launch — are set here.
* **`tick2` cannot be reproduced from the current source** (nor `kick1`/`tick1`):
  the ukernel tree carries this port's *uncommitted* work (`git show
  HEAD:ukernel/ukernel.py | grep -c SINGLE_CONTEXT` = 0).  Resolve that before
  any single-variable firmware comparison.
* **The panel after the final reboot**: not confirmed visually by the operator
  this session; `bl_power` 0 and 0 DSI errors.
* Unchanged: `0xb14`'s write (trap 387), `sgx540_init_regs`, the microkernel
  register tables (0x618/0x62c/0x694/TA 0x200/3D 0x428, hypothesis),
  `USE_CODE_BASE` existence, the DPM/TA/ISP/TSP/PBE/PDS state map.
* Whether the DDK 1.8's ~50 rev-120 errata matter for a *microkernel* port beyond
  the three marked "workaround in uKernel".

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 77 (absolute; Linux-era 71)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a, KNOWN-ISSUES.md §2 (traps 398-404) and §3,
DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 113-115, docs/reports/INDEX.md,
report 279 (this session's predecessor), 278, 277, 276, 275,
docs/reference/sgx540-factsheet.md AND docs/reference/sgx540-ddk18-facts.md,
docs/captures/sgx540-entry-counter-2026-11-26.txt,
docs/captures/sgx540-vendor-ukernel-source-2026-11-26.txt,
docs/sessions/session-76.md and session-75.md.

WHERE IT STANDS: Linux 7.3.3 on hd3 (Nura).  region 0 0x02C1DDC2 (keep the image
FILE — it is not byte-reproducible), region 1 20a5edd3..., hd3 = 5d3eac8f grown
to 62.5 G; BOOTCTR_FCTRY 0x25; card1/renderD129 = powervr_sgx with the
microkernel RUNNING (open microkernel fix1, EDM init event status 0x20000000,
BIF int 0x8000), card0/renderD128 = omapdrm; desktop on llvmpipe (4.3 ms/frame,
was 48.0 on softpipe); wlan0 DOWN; usb0 up; 0 DSI errors.

SESSION 76 CORRECTED THE CENTRAL FIGURE.  The SGX interrupt had never reached the
CPU -- on OMAP4 it enters the ti,sysc wrapper's IRQSTATUS_2 (0xfe38) and is
forwarded only while IRQENABLE_SET_2 (0xfe44) bit 0 is set; our driver mapped
0x4000 of 64 KiB and never touched it.  Opened, irq 147 ran 0 -> 100000.  Then
four more real defects fell: the launch program's 12-word segment in a 0x18-byte
slot, EVENT_PDS_DATA 3, EUR_CR_PDS never written, FIX_HW_BRN_30852, and
EUR_CR_DMS_CTRL (0x0a74) never written at all (0x00208200 -> the vendor's
0x003f8200, EDM 63 for BRN-23632 -- a real defect that did NOT change the
symptom).

BUT the entry counter built this session was reading control-block word 0x1c,
which is CLEANUP_DEFER_CT and which init_set clears.  Session 75's "entered once
and never again", and this session's own three "fetched exactly once"
conclusions, are all WITHDRAWN.  With the counter on word 0x07 the event program
is fetched TWICE -- entry = 2, tick (0x2c) = 1, SC_VERSION = 0x10000, stable over
94 s and UNCHANGED ACROSS EIGHT HOST KICKS.  One init pass, one dispatch pass,
and the dispatch pass is the timer's.

A PDS-task emit from the USSE task end then measurably EXECUTES and produces NO
re-fetch: UK_REARM puts the vendor's own instruction (verified byte-identical,
emit.t2 r20, r18, r16 [f12 f13 imm=2115]) on tmr_tick_end, the one-shot flag goes
1 -> 0, and entry/tick do not move.  Control fix1 (same instrument, no emit)
reads identically.  Also measured: emitting from init_set's task end faults the
launch outright (BIF int 0xe000 fault 0xfef4000, probe -5, no card1).

THE ONE EXPERIMENT (§0b): does the KICK path reach the microkernel at all?  An
idle boot appears never to reach a second task end, which is what blocks the
re-emit from being measurable.  Re-instrument the kick entry with a counter on a
control-block word PROVEN FREE (verify it mechanically first -- that check is what
failed invisibly via 0x1c), read ctlblk before and after submitting a job, and
interpret: kick counter 0 with resets moving = the core resets before the program
is fetched (trap-388 path, go to the doorbell EVENT_KICK 0xac8 read from inside
ccb_write); kick climbs but entry does not = the doorbell works and our STATUS
poll is the wrong design for kicks, adopt the vendor's ir0/ir1 dispatch; both
climb = contradicts the eight-kick measurement, re-measure before believing it.
Do NOT spend the boot on the re-emit, EUR_CR_DMS_AGE (underivable from this drop)
or the ir0/ir1 rewrite.

THE VENDOR'S DDK 1.8 DROP (/home/psyden/Pdrive/GFX_Linux_DDK-master, outside the
repo) CONTAINS THE SGX540 MICROKERNEL AS SOURCE AND AS A PREBUILT BINARY.  Its
event program is a PDS dispatcher over ir0/ir1, where EURASIA_PDS_IR0_EDM_EVENT_
TIMER is bit 1 -- versus b29 in EUR_CR_EVENT_STATUS, which is what our entry
polls -- and its INPUT_* constants are host-patched at load
(sgxinit.c:2507-2510), while our loader patches only ctlblk and pdtable.  It is
REFERENCE ONLY: the operator's direction is that the open driver compiles its own
microkernel.  Its spec PDFs are "Strictly Confidential" and unusable.

TRAPS THAT COST TIME THIS SESSION (all new, traps 398-404): a "free" control-block
word is only free if no store anywhere touches it (0x07 is, 0x1c is NOT); ld.a.d
st.a.d immediates and the sa word selector are WORD indices while nearby
expressions are byte offsets -- three bugs were `* 4` on a word index; a tool's
silence or non-zero exit is evidence about the invocation first (sgx-asm takes
stdin with -b); a hand-written cross-check is not a control; build BOTH driver
copies (local/kernel-tree-sgx is what compiles); build.py's output is raw and
must be wrapped by mkukfw --manifest or the driver rejects it ("not a microkernel
file"); a new ukfw_patch_value must go BEFORE UKFW_PDTABLE or the load fails
"bad entry or patch table" and the GPU never binds.

TRAPS THAT STILL APPLY: a uAPI "1/1 passed" is the driver's timeout here, not a
completion (trap 388); "consumed" after a reset equals "issued" (trap 389);
dmesg's "open microkernel <build>" is the only truth about which firmware ran
(trap 390); hd3 has no scp/sftp-server -- push with cat FILE | tools/pbssh.sh
'cat > /path' and md5sum first (trap 391); /dev/mem cannot reach the GPU (trap
384); edm_init=1 is required and is a supported path (trap 385); a modprobe -r is
impossible while elogind holds card1, so a module change costs a reboot; never
unbind pvrsrvkm live (trap 383); grim captures the compositor's buffers, not the
panel.

BENCH: region 0 0x02C1DDC2; region 1 20a5edd3...; hd3 live /dev/loop0 ext4 rw,
56.5 G; BOOTCTR 0x25; nproc 2, PID 1 affinity 0,1; battery Full; panel bl_power
0, 0 DSI errors; wlan0 DOWN, usb0 169.254.0.1/16 (PB_LINK=usb tools/pbssh.sh
works; if it times out check the cable, the host VPN (trap 341) and ufw (trap
342) first).  Firmware on hd3: sgx540_ukernel.bin = f468a71d... (fix1, the
corrected instrument) with .tick2 7b44b33a..., .before-hostirq 12f12abb... and
this session's variants beside it; module powervr_sgx.ko = bb7a0c68... with
.dumpregs-before-ocp 9a0ebbe3....  Desktop llvmpipe (swrast_dri.so and
kms_swrast_dri.so 1db573b1..., libLLVM-13.so d1b49590...) -- the hd3 flasher
image still lacks it, so a reflash loses it.  The uAPI test binary is at
/tmp/powervr_sgx_test (on the ext4, survives a reboot).  The full build/deploy
recipe is HANDOFF §3.

Kernel printk goes to the panel; use reboot -f; ask the operator for visual
checks; do not screenshot; do not restart tinydm (trap 353) and do not kill Kumo
(trap 367).  An agent never writes RPMB; NVRAM read-only.  Revert experiments and
prove the revert; look up every external fact; do not call sync (trap 300); /tmp
is on the ext4 (trap 354).  There is NO RAM-boot path.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
Start the flasher, then reset with reboot -f; the bootrom holds ~20 s.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  WRAP-UP: only the operator
decides when the session stops; then record the bench state, write the session
note, update the canonical docs, verify with tools/docs_audit.py and
tools/tests/test_offline.py, commit privately and publish the snapshot
(docs/project/REPO-MANAGEMENT.md).
```
