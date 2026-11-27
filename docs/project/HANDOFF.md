# Handoff — session 78 (absolute) → session 79

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 79 = Linux-era 73**, written at the
session-78 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 78 RESULT: THE OPEN MICROKERNEL TAKES AND COMPLETES JOBS.**
> `null job signalled in 946 µs`, `600 null jobs in 91 ms`, **zero GPU resets**
> for the whole uAPI suite, the CCB ring draining (`issued 615 consumed 615`),
> host interrupts delivered.  The fix was ONE BIT: `EVENT_PDS_INFO` (0x0a70)
> had `USE_PIPELINE = TOGGLE` (round-robin between the two USE pipes); the
> vendor's derivation is `USE_PIPELINE_0`.  Report **281**; the full campaign
> (16 boots) is in `docs/captures/sgx540-kick-entry-counter-2026-11-27.txt`;
> traps **406–412**; DECISIONS **§116**.

> **SESSION 77 RESULT: the kick entry is never entered, and NO dispatch source
> works after the launch.**  The re-instrumented kick counter (control-block
> word 0x3f, the `kick` entry's first instruction, mechanically verified free)
> reads **0 across eight job submits that each cost a GPU reset**.  Combined
> with the timer's latched b29 + static tick and the `relaunch` result, the EDM
> fetched the event program **twice during launch** and **never again for any
> trigger**.  HANDOFF-77 §0b's doorbell follow-up came back
> **unmeasurable-with-that-instrument**: even the provably-landing launch kick
> reads `EVENT_KICK` as 0 in all 8 immediate samples.  And two instrument
> corrections: the `UK_ENTRYLOG` snapshot store has never worked (limm-bytes
> bug), and **"the dispatch pass is the timer's" is withdrawn — there is no
> dispatch pass** (the `tick = 1` readings were misparse candidates; word 0x2a
> reads 1 and its writer is unidentified).  Report **280**; traps **406–408**;
> capture `docs/captures/sgx540-kick-entry-counter-2026-11-27.txt`.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**, with
a GPU-accelerated desktop (compositor **and** clients), and a modern browser.

**The long-term graphics route is the open `powervr_sgx` stack** (DECISIONS
§107/§108): the closed pvr DDK's GBM cannot export dmabufs, which is what blocks
the browser (report 275).  This is that port.

## 0a. What sessions 77–78 established (do not re-derive)

* **THE FIX (session 78, report 281): `EVENT_PDS_INFO` (0x0a70) carried
  `USE_PIPELINE = TOGGLE` where the vendor derives `USE_PIPELINE_0` — one
  bit, and the whole dispatch blocker.**  `0x00013d01 → 0x00013c01`; live
  read-back at the wrap-up: `EVENT_PDS_INFO 0x00013c01` ✓.  The pipeline now
  runs continuously (entry/tick climbing hours after boot) and jobs complete.
* **The tick counter works — after a GPU-side seed (trap 409).**  The GPU's
  initial view of the control-block tail is NOT the CPU's zero-filled view:
  the old counter read 0xffffffff, +1 wrapped to 0, and "tick = 0" looked
  like "no dispatch pass".  `init_set` now seeds 0x100; the first dispatch
  pass reads 0x101 at exactly word 0x2c.  **Seed every ctlblk counter from
  the GPU side with a non-identity value.**
* **"4 fetches per life" was double counting (trap 410).**  With
  `UK_ENTRYFIRST` and `UK_ENTRYLOG` both set the old counter still emits, so
  word 0x07 increments TWICE per fetch.  The true count was 2.  **The live
  `kick10` still double-counts** (both counters are in the deployed image) —
  divide an `entry(0x07)` reading by 2, or rebuild with one counter.
* **The kick entry is never entered (session 77, boots 1-4).**  `UK_KICKLOG`
  re-instrumented: the counter sits on the **first instruction of the `kick`
  entry**, on control-block word **0x3f** — mechanically verified free and
  disassembly-verified (`st.a.d r17, #63`; the tick site then read
  `st.a.d r17, #44`).  Zero across eight job submits that each cost a GPU
  reset, with the entry counter's reads as the negative control.  (This was
  the pre-fix pipeline; re-measure under `USE_PIPELINE_0`.)
* **`EVENT_KICK` read-backs cannot show landing on this core (trap 407).**
  The doorbell instrument's control — the provably-landing launch kick —
  reads all-zero too.  Do not re-run it expecting it to discriminate.
* **The dispatch blocker was NOT**: DMS aging (`DMS_AGE = ENABLE|0x2c`
  sticks, no effect; the vendor's own default is 0), the task-end emit with
  the vendor's exact operand derivation (fires, no fetch — session 76's
  "cannot re-fetch" was garbage operands, withdrawn), `UK_NO_PHAS` (the
  vendor's working SGX540 image has no `phas` at all; ours was not where
  anything died), or the DOUTU task-control words (now `MODE_PERINSTANCE`,
  kept, not sufficient alone).
* **The launch-script diff is COMPLETE, and `CACHE_CTRL` is load-bearing
  (trap 411 + contradiction file): the vendor's own `0x0000ffff` breaks
  event dispatch entirely** (bisected over three boots).  It is NOT written
  and must not be.  The other five remaining registers (`USE_MOE`,
  `ISP_DEPTHSORT`, `USE_G0/G1`, `DMS_AGE`) are compatible and programmed;
  `USE_CTRL` was removed in the bisect and stayed out.
* **The never-written dispatch registers, measured and in `status`**:
  `DMS_AGE` (now programmed), the PIXEL event-PDS pair (0 — it belongs to
  the VDM context-store terminate program, not event dispatch),
  `EVENT_PDS_ENABLE2 = 0`, `EVENT_KICK1/KICK3 = 0`, `EUR_CR_LOOPBACK` (1 =
  space — the vendor's EmitLoopBack spins while it is ZERO),
  `USE0/1_SERV_EVENT` (per-pipe event service queues).
* **"One init pass and one dispatch pass, and the dispatch pass is the
  timer's" (session 76)** — CONFIRMED, after two corrections: the `tick = 1`
  readings of sessions 75–76 were the tick counter wrapping through
  0xffffffff (trap 409), not a misparse; and word 0x2a — which read 1 on
  every pre-fix boot and fuelled the misparse theory — reads **0** since the
  tick rework (boots 5+); its writer was never named and the line is now
  GPU-written by the seed.
* **The vendor's DDK 1.8 drop (`/home/psyden/Pdrive/GFX_Linux_DDK-master`,
  outside the repo) contains the SGX540 microkernel as source AND as a prebuilt
  binary** — reference only, never deploy it (DECISIONS §115).  Its event
  program is a **PDS dispatcher over `ir0`/`ir1`** (`EURASIA_PDS_IR0_EDM_EVENT_
  TIMER` is bit 1 there versus b29 in `EUR_CR_EVENT_STATUS`), and its
  `INPUT_*` constants are host-patched at load while our loader patches only
  `ctlblk` and `pdtable`.  Its spec PDFs are "Strictly Confidential" and
  unusable.  Capture: `docs/captures/sgx540-vendor-ukernel-source-2026-11-26.txt`.
* **The desktop is on llvmpipe** (4.3 ms vs softpipe's 48.0 ms); **the hd3
  flasher image still lacks llvmpipe and `libLLVM-13`** — a reflash loses it.
* **The fast-tap panel blank is catacomb's double-tap sleep gesture**; the
  guard (`gesture-handle --height 0`) is **temporary** — the real fix is one
  guarded line in catacomb (DECISIONS §114).

## 0b. THE ONE EXPERIMENT FOR SESSION 79 — a real render job

**The operator's directive: drive the GPU toward rendering.  The next unit of
work is ONE render job, submitted through the driver, watched end to end in
the microkernel's own markers.**

Where it stands.  The dispatch pipeline is alive (report 281): the EDM
dispatches the event program continuously, the µkernel services kicks, and
the CCB drains.  The driver's `job_types` query advertises **only the NULL
job** (`c.job_types == 4`), so no render/transfer command has ever been
packed into the kernel CCB — and the µkernel's `k_ta`/`do_ta`/`do_3d` path
(the TA/3D register tables, all carried-over **hypothesis**: 0x618/0x61c/
0x648/0x638/0x620/0x624/0x628, 0x62c/0x634, 0x200, 0x428) has never executed.

**Method — wire the render job type at the smallest scale, then submit one
minimal frame.**

1. Find what gates the render job type off for this core — **found in the
   source (verified at this wrap-up)**: `pvrsgx_cmd_defs.c`'s
   `sgx540_render` / `sgx540_ta_kick` / `sgx540_transfer` exist but carry no
   `.supported` flag ("like the sgx530 until a run on the core settles the
   checked fields", the file's own comment) and no field tables;
   `pvrsgx_drv.c`'s `COMMANDS_GET` then advertises NULL only.  The fix:
   `.supported = true` plus field tables on the three `sgx540_*` defs —
   the `sgx545_*` tables in the same file are the reference to copy — for
   the `sgx540` entry only, with the SGX545 serial image staying
   byte-identical (DECISIONS §112's rule).
2. Keep the working firmware (`kick10`) and the working module baseline
   (`.working` pair) as the revert.  Build the module, boot, verify the
   null-job family STILL passes first (one `submit-null-job-many`) — the
   dispatch pipeline must not regress.
3. Submit ONE minimal render (a TA + 3D scene with a tiny PB/RT, as
   `run_ours.py TRACE` models; the DDK's es2gears single-frame path is the
   reference shape).  The µkernel's own markers are the instrument, all
   host-readable via `ctlblk`/`status`: `kick(0x3f)` (the kick entry), the
   tick/entry counters, `ta3dctl`'s TA/3D CCB words, and `USE_SERV_*`.
   **Caveats**: the deployed `kick10` was built WITHOUT `UK_WAITLOG`, so the
   WAIT_LOG stage bytes (0x35/0x37-0x3b: do_ta/ta_finished/do_3d/render_done)
   are NOT populated — using them is a firmware rebuild (one boot); and
   `kick10`'s entry counter double-counts (trap 410) — divide by 2 or rebuild
   with one counter while you are rebuilding anyway.
4. Interpret:
   * **`kick(0x3f)` climbs and the render completes** (`SC_COMPLETED` 0x27
     bumps, SW_EVENT, the fence signals) → the render path works; go to the
     frame loop and then Mesa.
   * **`kick(0x3f)` stays 0 while `ccb wo` advanced** → the render command's
     CCB packing is wrong (the driver's CMDTA layout), not the µkernel; diff
     the packed command against the vendor's SGXMKIF_CMDTA layout
     (sgx_mkif.h, `ta3dctl` derivation in `sgx540-ddk18-facts.md` §7).
   * **`do_ta` marker set but no `ta_finished`** → the TA register tables
     (the hypothesis registers) are wrong; instrument with `UK_WAITLOG` and
     walk the table one register at a time (trap 403's rule: verify each
     offset against sgx540defs.h — the 0xb14 discovery is the precedent).
   * **BIF fault** (`BIF_FAULT` non-zero, `BIF_INT_STAT` fault bits) → the
     render buffers' page-directory mapping; check the client PD slot and
     `pdtable` resolution first.
   * **Lockup + reset** → the µkernel's watchdog path fired; capture
     `ctlblk`'s stage words BEFORE the reset (the driver dumps them in the
     timeout line) — they name the stuck wait site.

**Do not** start Mesa or the compositor work in this boot; the deliverable is
one render job observed end to end (or its precise failure point).  **Do not**
touch `CACHE_CTRL`/`USE_CTRL` (trap 411).  Keep `USE_PIPELINE_0`.

## 0c. The media, and the way back

* **region 0 = `0x02C1DDC2`** — `boot/chainloader/kernel.img`, 10 450 520 B,
  sha256 `70474c01…`.  Keep the image FILE; it is not byte-reproducible.
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

## 1. Bench state at the session-78 close (read on the unit)

| item | value |
|---|---|
| OS | **Linux `7.3.3`**, `Nura edge` — not TabletOS.  **No restore required** |
| region 0 | **`0x02C1DDC2`** — no flash this session.  File `boot/chainloader/kernel.img`, 10 450 520 B, sha256 `70474c01…` |
| region 1 | sha256 **`20a5edd3…`** (unchanged) |
| hd3 | the session-71 rebake **`5d3eac8f…`** at `0x4915`, grown to 62.5 G; `/dev/loop0` ext4 rw |
| `BOOTCTR_FCTRY` | **`0x28`** read fresh at the wrap-up (`pbrpmb: topup: … = 28000000`, above threshold).  The top-up FIRED during the session's heavy rebooting (a boot landed below 0x1e and wrote 0x32) — self-maintaining as designed.  Read it each boot and quote the byte string |
| CPU | `nproc` 2; PID 1 affinity **`0,1`** |
| battery | Full |
| GPU | `card1`/`renderD129` = **`powervr_sgx`** running **`open microkernel kick10`**; `card0`/`renderD128` = `omapdrm`.  **The dispatch pipeline is alive — jobs complete**: the uAPI suite reads **30/50** from a cold boot with `resets 0`, `ccb issued 2460 consumed 2460`, `irqs` delivered; the whole null-job family passes on real timings (946/427 µs; 600 jobs/91 ms).  The 20 remaining failures are SGX545-shaped TEST expectations (core_id 0x0115, va_bits 32, heap 0x3e000000 vs our 0x0114/28-bit) — `vm_map` at 32-bit addresses correctly refuses |
| firmware | `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`kick10`** (wrapped md5 `3fbbd11b…`; UK_NO_PHAS+ENTRYFIRST+seed+TIMERDIV=100) — **preserved as `.working`**, the proven pair.  Beside it: `.kick9w` `db69d7c5…`, `.kick8w` `444a817d…`, `.kick2w` `12847879…` (the kick-entry instrument), `.fix1w` `f468a71d…`, `.tick2` `7b44b33a…`, `.before-hostirq` `12f12abb…`, and the session-76 variants |
| module | `powervr_sgx.ko` = **`10c6f435…`** — **preserved as `.working`**, the proven pair (`USE_PIPELINE_0`, the vendor launch set minus CACHE_CTRL/USE_CTRL, DMS_AGE, the corrected UKFW_PDS emit operands, the DOUTU MODE_PERINSTANCE words, the doorbell node, LOOPBACK + USE_SERV_EVENT in status).  Preserved: `.pre-doutu` `0e0fea3b…`, `.prelaunch` `b84dd8c0…`, `.pre-loopback` `127a5fad…`, `.dmsage` `be3d4b35…`, `.predump` `801d08a5…`, `.pre-doorbell` `94eaea61…`, `.dumpregs-before-ocp` `9a0ebbe3…` |
| desktop | **llvmpipe** for compositor *and* clients (`swrast_dri.so`/`kms_swrast_dri.so` `1db573b1…`, `libLLVM-13.so` `d1b49590…`; softpipe `.amber` `d8d69228…`).  4.3 ms/frame vs softpipe 48.0 ms |
| panel | LVDS `card0-LVDS-1` connected, `bl_power` **0**, 0 DSI errors.  Gesture handle 0 height in `/etc/catacomb/initrc` (`56888215…`) — a **temporary guard**, DECISIONS §114 |
| WLAN | **`wlan0` UP** at the wrap-up boot (the GPU resets of the mid-session boots had knocked it down; the final cold boot brought it back — no wireless fixes were made).  `usb0` `169.254.0.1/16` UP |
| SSH | `PB_LINK=usb tools/pbssh.sh` works; if it times out check the cable, the host VPN (trap 341) and ufw (trap 342) |
| device-side files | the firmware/module variants above, `/etc/catacomb/initrc`, `/tmp/powervr_sgx_test` (**`/tmp` is on the ext4**, trap 354) |
| host | clone `local/work/powervr_sgx` (`95f78ca` + our patches); assembler `local/work/sgxasm/sgx-asm`; trees `local/kernel-tree` (good) and `local/kernel-tree-sgx` (open driver); **GPL-2 DDK 1.8 reference at `/home/psyden/Pdrive/GFX_Linux_DDK-master` (outside the repo)** |

## 2. Working rules (unchanged, plus what this session cost)

* Read `docs/project/SAFETY.md` before any write.  One variable per boot.  An
  agent never writes RPMB; NVRAM read-only.
* Kernel printk goes to the panel; `console=ttyGS0` is inert.  Use `reboot -f`.
  **Ask the operator what is on the panel**; do not screenshot.
* **Do not restart `tinydm`** (trap 353), and **do not leave Kumo running or
  kill it** (trap 367) — reboot.
* The eMMC can log `Card stuck being busy`; a power cut can damage the rw ext4
  (trap 336).  **Do not call `sync`** (trap 300).  `/tmp` is on the ext4
  (trap 354).
* **Register reads:** only from inside the driver now (trap 384); the userspace
  `/dev/mem` path is closed in this configuration.
* **Revert experiments and prove the revert.**  Look up every external fact.
  Write the durable fact into the repository in the same turn.
* **The trap-403 rule, extended (trap 406): the byte/word unit bug lives in
  `limm` immediates too.**  `imae rD,rA,#imm,rB` is `rA*imm + rB` with **no
  scaling** — a ctlblk word address through it needs the limm loaded as
  `word * 4`.  Parse every ctlblk dump **by word index** (0x2a is the 3rd word
  of row 6; 0x2c the 5th), never by eyeball.
* **A self-clearing register's read-back needs a landing control (trap 407).**
  Even the provably-landing launch kick reads `EVENT_KICK` all-zero in 8
  immediate samples.  Do not re-run that instrument expecting it to
  discriminate.
* **Verify a new instrument by disassembling the emitted image** (session 77's
  cheap win): read the actual `st.a.d r17, #N` back out of the `.bin` instead
  of trusting the numeral.  `sgx-disasm` prints instruction lines; address =
  (line-1) * 8.
* **Firmware builds must use explicit output paths**, `build.py`'s output is
  raw and must be wrapped by `mkukfw --manifest`, a new `ukfw_patch_value` goes
  BEFORE `UKFW_PDTABLE`, and the token/env rules of trap 405 now hold
  (`SGX_CORE` conflicts are a hard error).
* **Build both driver copies** (`local/work/powervr_sgx` is the source of
  truth but `local/kernel-tree-sgx` is what compiles).  **A module change
  costs a reboot** (`modprobe -r` impossible while elogind holds `card1`);
  never unbind pvrsrvkm live (trap 383).  **GPU resets take `wlan0` down** —
  run the uAPI suite in small pieces and expect to bring WLAN back.
* **Reboot before measuring anything if the boot's `open microkernel` line has
  wrapped out of dmesg** (trap 390: that line is the only truth about which
  firmware ran; the operator asked for this and it was right).

## 3. The build and deploy recipe, exactly (all of it was run this session)

```bash
cd local/work/powervr_sgx
# 0. the assembler path, once per shell (build.py and the gates both need it)
export SGX_ASM=$PWD/../sgxasm/sgx-asm
# 1. firmware: raw image + manifest, EXPLICIT output paths; the SAME UK_*
#    flags must stay set for the gate in step 2 (test_emu REBUILDS the image
#    from the environment -- without the flags it tests the un-instrumented
#    build, not the one you just made)
UK_TICKLOG=1 UK_ENTRYLOG=1 UK_KICKLOG=1 \
  UK_BUILD_ID=<id> python3 ukernel/build.py sgx540 /tmp/<id>.bin /tmp/<id>.json
# 2. gates: both must pass before deploying, with the SAME environment
SGX_CORE=sgx540 UK_TICKLOG=1 UK_ENTRYLOG=1 UK_KICKLOG=1 \
  python3 ukernel/test_emu.py                                                # 8/8
python3 ukernel/golden/check.py                                             # 83b5b0a7…
# 3. wrap it -- build.py's output is raw and the driver will reject it
python3 drm/tools/mkukfw.py /tmp/<id>.bin -o /tmp/<id>w.bin --manifest /tmp/<id>.json
python3 drm/tools/mkukfw.py --check /tmp/<id>w.bin                            # ok
# 4. push (hd3 has NO scp/sftp-server -- trap 391)
cat /tmp/<id>w.bin | tools/pbssh.sh 'cat > /lib/firmware/powervr_sgx/<name>; md5sum /lib/firmware/powervr_sgx/<name>'
# 5. module: build BOTH source copies (env.sh lives at the REPO root)
. ../../tools/env.sh
cp drm/powervr_sgx/*.c drm/powervr_sgx/*.h \
   ../../kernel-tree-sgx/drivers/gpu/drm/powervr_sgx/
cd ../../kernel-tree-sgx && make ARCH=arm CROSS_COMPILE=$CROSS_COMPILE \
  M=drivers/gpu/drm/powervr_sgx modules
```

## 4. Useful local extracts

* `docs/captures/sgx540-kick-entry-counter-2026-11-27.txt` — **the whole
  campaign, sixteen boots in order**: the kick instrument and its mechanical
  verification, the doorbell's landing control, the dispatch-register dump,
  the tick counter's fix (the GPU-side seed), the double-counting
  correction, the CACHE_CTRL bisect, and **the USE_PIPELINE fix with the
  live-pipeline numbers**.  Read it before quoting anything from sessions
  77-78.
* `docs/contradictions/sgx540-cache-ctrl-breaks-dispatch.md` — the unresolved
  tension behind trap 411: the vendor's own srvinit value breaks dispatch on
  our stack; what would settle it.
* `docs/captures/sgx540-entry-counter-2026-11-26.txt` — the entry instrument,
  the corrected measurement, the re-emit rounds, and the evidence inventory.
  Read with trap 406 in hand: its `tick = 1` figure is a misparse candidate.
* `docs/captures/sgx540-vendor-ukernel-source-2026-11-26.txt` — the vendor's
  PDS dispatcher, the entry table, the PDS patch indices, four retractions.
* `docs/reference/sgx540-factsheet.md` — the SGX540 facts, **C** where
  measured.  `docs/reference/sgx540-ddk18-facts.md` — every K fact from the
  DDK 1.8 drop.
* `local/work/powervr_sgx/ukernel/` — the microkernel; `build.py`,
  `test_emu.py` (8/8 per core), `golden/check.py` (the SGX545 byte-identity
  gate), `cores/{sgx540,sgx545}.py`.  The kick counter lives in
  `ukernel.py` under `UK_KICKLOG`.
* `rootfs/powervr-sgx/` — the patch series 0001–0007; **the session-77/78
  module and firmware changes are NOT yet in a patch file** — export them
  before any upstream move (DECISIONS §110: nothing is published until the
  core draws).
* Reports **281** (session 78), 280 (session 77), 279, 278, 277, 276, 275.

## 5. What could not be verified

* **The GPU-side initial value of the control-block tail** — the tick counter
  needed a GPU-side seed to read sanely (trap 409); the mechanism behind the
  dirty view (DMA restore, cache state, or the task-start DMA) is not
  identified.  Word 0x2a's old "= 1" vanished when the tick code changed and
  the line was written GPU-side; its writer was never named.
* **`CACHE_CTRL`'s PARTDM semantics and reset value** — the register is not
  in the `status` dump while unwritten, and the closed system's live value is
  unmeasured (the contradiction file names the tests that would settle it).
* **Whether the render path works** — never exercised: the driver advertises
  only the NULL job and the TA/3D register tables are carried-over hypothesis.
  That is session 79's §0b.
* **`tick2`/`kick1`/`tick1` still cannot be reproduced from the current
  source** (the ukernel tree carries this port's *uncommitted* work) — resolve
  before any single-variable firmware comparison.
* **The panel**: not confirmed visually by the operator this session;
  `bl_power` 0 and 0 DSI errors.
* Unchanged: `0xb14`'s write (trap 387), `sgx540_init_regs`, the microkernel
  register tables (0x618/0x62c/0x694/TA 0x200/3D 0x428, hypothesis),
  `USE_CODE_BASE` existence, the DPM/TA/ISP/TSP/PBE/PDS state map, and the
  owed patch-series export (`rootfs/powervr-sgx/`) for all the session-77/78
  driver+firmware changes — nothing is upstream (DECISIONS §110).

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 79 (absolute; Linux-era 73)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a, KNOWN-ISSUES.md §2 (traps 398-412) and §3,
DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 112/113-116, docs/reports/INDEX.md,
report 281 (this session's predecessor) and 280,
docs/reference/sgx540-factsheet.md AND docs/reference/sgx540-ddk18-facts.md,
docs/captures/sgx540-kick-entry-counter-2026-11-27.txt,
docs/contradictions/sgx540-cache-ctrl-breaks-dispatch.md,
docs/sessions/session-78.md.

WHERE IT STANDS: Linux 7.3.3 on hd3 (Nura).  region 0 0x02C1DDC2 (keep the image
FILE), region 1 20a5edd3..., hd3 = 5d3eac8f grown to 62.5 G; BOOTCTR_FCTRY 0x28
(read fresh at the session-78 wrap-up; the Linux /init tops up below 0x1e);
card1/renderD129 = powervr_sgx running the open microkernel kick10 -- THE
DISPATCH PIPELINE IS ALIVE: the uAPI suite reads 30/50 from a cold boot with
ZERO GPU resets, the CCB ring drains (issued 2460 consumed 2460), host
interrupts delivered, and the whole null-job family passes on real timings
(null job 946/427 us; 600 null jobs in 91 ms).  The 20 remaining failures are
SGX545-shaped TEST expectations (core_id 0x0115, va_bits 32, heap 0x3e000000;
our core is 0x0114 / 28-bit).  wlan0 was UP at the wrap-up boot; usb0 up;
desktop llvmpipe.  NOTE: the live EVENT_PDS_INFO reads 0x00013c01 (the fix);
and kick10's entry counter DOUBLE-COUNTS (ENTRYFIRST+ENTRYLOG both emitted,
trap 410) -- divide entry(0x07) by 2 or rebuild with one counter.

THE SESSION-78 FIX WAS ONE BIT: EVENT_PDS_INFO (0x0a70) had USE_PIPELINE =
TOGGLE (round-robin between the two USE pipes); the vendor's derivation is
USE_PIPELINE_0.  0x00013d01 -> 0x00013c01.  Under round-robin the event tasks
alternated pipes and a stuck pipe blocked the toggle forever -- the
one-dispatch-then-stuck signature that sessions 74-78 chased.

Also landed this session, all measured: the tick counter fixed with a GPU-side
seed (the GPU's initial view of the control-block tail is NOT the CPU's zeros
-- the old counter read 0xffffffff and wrapped to 0; trap 409); "4 fetches"
was double counting (trap 410); DMS_AGE = ENABLE|0x2c -- no effect; the
task-end emit with the vendor's operand derivation fires and does not fetch
(session 76's negative was garbage operands); the DOUTU task-control words
fixed to MODE_PERINSTANCE; the complete vendor launch-script diff --
CACHE_CTRL = 0x0000ffff, THE VENDOR'S OWN VALUE, BREAKS EVENT DISPATCH
(bisected, trap 411; it is NOT written, and do not write it).

THE ONE EXPERIMENT (§0b): a REAL RENDER JOB.  The driver's job_types query
advertises only the NULL job; the microkernel's k_ta/do_ta/do_3d path (the
TA/3D register tables, all carried-over hypothesis) has never executed.  Wire
the render job type for the sgx540 entry -- the gate is FOUND:
pvrsgx_cmd_defs.c's sgx540_render/ta_kick/transfer carry no .supported flag
and no field tables ("like the sgx530 until a run on the core settles the
checked fields"); set .supported = true and copy the sgx545_* field tables,
sgx540-entry-only.  Keep the .working pair as the revert, verify the
null-job family still passes, then submit ONE minimal TA+3D frame and read
the microkernel's own markers (kick 0x3f, the tick/entry counters, ta3dctl's
TA/3D words, USE_SERV_*).  Caveats: kick10 has NO UK_WAITLOG (the WAIT_LOG
stage bytes are unpopulated -- using them is a firmware rebuild) and its
entry counter double-counts.  Outcomes: render completes (SC_COMPLETED bumps,
SW_EVENT, fence) -> frame loop, then Mesa; kick stays 0 with ccb wo advancing
-> the CMDTA packing is wrong, diff against sgx_mkif.h; do_ta set but no
ta_finished -> the TA register tables are wrong, walk one register at a time
against sgx540defs.h (the 0xb14 discovery is the precedent); BIF fault -> the
render buffers' PD mapping; lockup+reset -> read ctlblk's stage words from
the timeout line BEFORE theorising.

TRAPS THAT COST TIME (409-412, new): seed every ctlblk counter from the GPU
side with a non-identity value (the GPU's initial view of the tail is not the
CPU's zeros); count an instrument's store sites in the DISASSEMBLY before
trusting its count (ENTRYFIRST+ENTRYLOG double-emit); CACHE_CTRL is
load-bearing and its PARTDM semantics are unmeasured; decode multi-bit
constants field-by-field against the vendor's derivation (USE_PIPELINE was
hand-derived and carried TOGGLE where the vendor writes PIPE_0).

TRAPS THAT STILL APPLY: a uAPI "pass" is only evidence once the fence is
understood (trap 388) -- but the null-job timings are now REAL (us-scale);
"consumed" after a reset equals "issued" (trap 389); dmesg's "open
microkernel <build>" is the only truth about which firmware ran (trap 390) --
reboot before measuring if it has wrapped out; hd3 has no scp/sftp-server --
push with cat FILE | tools/pbssh.sh 'cat > /path' and md5sum first (trap 391);
/dev/mem cannot reach the GPU (trap 384); edm_init=1 is required and
supported (trap 385); a module change costs a reboot; never unbind pvrsrvkm
live (trap 383); GPU resets can take wlan0 down; parse every ctlblk dump BY
WORD INDEX; seed counters GPU-side.

BENCH: region 0 0x02C1DDC2; region 1 20a5edd3...; hd3 live /dev/loop0 ext4
rw; BOOTCTR 0x28 at the wrap-up; nproc 2, PID 1 affinity 0,1; battery Full;
panel bl_power 0; usb0 169.254.0.1/16 (PB_LINK=usb tools/pbssh.sh works; if
it times out check the cable, the host VPN (trap 341) and ufw (trap 342)
first).  Firmware on hd3: sgx540_ukernel.bin = kick10 (3fbbd11b), preserved
as .working, with .kick9w/.kick8w/.kick2w/.fix1w/.tick2/.before-hostirq
beside it; module powervr_sgx.ko = 10c6f435, preserved as .working, with
.pre-doutu/.prelaunch/.pre-loopback/.dmsage/.predump/.pre-doorbell beside
it.  Desktop llvmpipe -- the hd3 flasher image still lacks it, so a reflash
loses it.  The uAPI test binary is /tmp/powervr_sgx_test (on the ext4).  The
full build/deploy recipe is HANDOFF §3.

Kernel printk goes to the panel; use reboot -f; ask the operator for visual
checks; do not screenshot; do not restart tinydm (trap 353) and do not kill
Kumo (trap 367).  An agent never writes RPMB; NVRAM read-only.  Revert
experiments and prove the revert; look up every external fact; do not call
sync (trap 300); /tmp is on the ext4 (trap 354).  There is NO RAM-boot path.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only
when only it changed; both regions in one bootrom session only when both
changed.  Start the flasher, then reset with reboot -f; the bootrom holds
~20 s.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  WRAP-UP: only the
operator decides when the session stops; then record the bench state, write
the session note, update the canonical docs, verify with tools/docs_audit.py
and tools/tests/test_offline.py, commit privately and publish the snapshot
(docs/project/REPO-MANAGEMENT.md).

Read the document files in full, to be thorough and not miss anything
important.
```
