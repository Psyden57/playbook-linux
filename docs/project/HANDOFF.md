# Handoff — session 80 (absolute) → session 81

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 81 = Linux-era 75**, written at the
session-80 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 80 RESULT: the VDM control stream's RECORD LAYOUT is walked and
> excluded — the stall did not move.**  The vendor's own *client-side* builder
> (GPL-2 DDK 1.8) splits every record on `#if defined(SGX545)`, and our
> generator emitted the SGX545 value for **three** fields: the index-list
> header's b24 `IDXPRES45`, the state record's `PARTITIONS` nibble, and the
> draw's word 5 (`sgxdefs.h:1124-1155`).  Emitted for real renders — first two
> together (4 of the stream's 28 words), then word 5 alone (2 of 28,
> `0x05800102`, the 540 packing byte-for-byte) — **identical timeout signature
> both times**.  Both reverted; the stream is byte-identical to session 79's.
> Also: the **`TA_TE` tail is pruned** (firmware `kick16tail` `744b072d…`;
> vendor write set re-derived, 92 `str #imm`/41 offsets, SGX545 golden
> byte-identical, 8/8 offline tests) — one boot, null family intact, and the
> render's mid-hang TA/VDM/DPM state is field-for-field session 79's.  Traps
> **416/417**; the capture's "SESSION 80 CONTINUATION" is the record.

> **SESSION 79 RESULT (context): the first render job went through the open
> microkernel end to end, and its failure point is measured.**  The render job
> type is wired; five µkernel fixes (traps 413/414); on `kick15pds` no wait
> spins, the µkernel consumes the kick and kicks the TA — `EVENT_STATUS2
> TRIG_TA` latches at the timeout and never finishes; VDM/MTE/UCACHEL2/USE0
> clocked awake, TE never starts, DPM allocates zero pages, no BIF fault.
> Report 282; DECISIONS §117.

> **SESSION 78 RESULT (context): the dispatch pipeline is ALIVE** — the
> USE_PIPELINE_0 fix (one bit in `EVENT_PDS_INFO`), the null family passing
> on real timings, zero resets.  Report 281; traps 406-412; DECISIONS §116.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**,
with a GPU-accelerated desktop (compositor **and** clients), and a modern
browser.  The long-term graphics route is the open `powervr_sgx` stack
(DECISIONS §107/§108) — this is that port, driving toward a rendered frame.

## 0a. What sessions 77-79 established (do not re-derive)

* **The dispatch pipeline is alive and jobs complete** (session 78,
  report 281): `EVENT_PDS_INFO` 0x0a70 = `0x00013c01` (`USE_PIPELINE_0` — the
  vendor's derivation; TOGGLE was the whole dispatch blocker).  Live
  read-back confirmed at the session-79 start.  Keep it.
* **The µkernel's host-kick path is exhausted as a suspect** (session 79):
  the render kick is consumed, both context-CCB TA commands' worth of
  protocol run, every cache-invalidate and DPM/OTPM load completes with the
  vendor's own shapes, and the per-scene TA register block matches the
  vendor's `SGXMKIF_TAREGISTERS` field-for-field (verified mid-hang: the
  registers carry correct values — TE_SCREEN/MTILE exact for 256x256, the
  thresholds right, the free list loaded).  **Do not re-audit the TA
  register tables** — the mid-hang dump is in the capture.
* **Four µkernel bugs of the 0xb14 class were fixed** (traps 413/414): the
  0x138 invalidate-done masks (per-request **0x03/0x05** — each done bit
  belongs to one request register: b1=0xad0, b2=0xad4, b4=0xadc, b0=0xae0);
  the OTPM OP write (**1 = CSM_INV** — the 540's `MTE_OTPM_OP` 0x24c has no
  bit 3); the STATUS2 b12 wait (545-only, compiled out); the DPM PDS-page
  threshold (**0x614**, not the 545's 0x668); and no free-list mode selector
  (0x664 = the 545's `DPM_CONTEXT_PB_BASE`; the vendor's 540 loads write no
  mode register).
* **The vendor 540 binary's write-set diff is the sharpest tool so far.**
  Disassemble `binary2_540_omap4430_android_release/…/ukernel.bin` with
  `sgx-disasm -c sgx540` and decode every `str #imm` site (93 of them) —
  it settles "what register / what mask / what value" for this silicon
  without a boot.  Re-run it before deploying any register change.
* **The vendor's own TA source is readable** (`ta.asm`, `sgx_mkif_client.h`
  in the DDK 1.8 drop — GPL-2, outside the repo at
  `/home/psyden/Pdrive/GFX_Linux_DDK-master`).  Its per-side invalidate
  masks come from `sgxdefs.h`'s `FIX_HW_BRN_31474` branch — and 31474 IS in
  our rev-120 errata list ("workaround in uKernel").
* **The driver's `status` node now carries the TA/VDM/DPM registers**
  (module `1a2949ab…`; all named in sgx540defs.h, trap-369-safe).  A
  mid-hang read names the stuck unit by CLKGATESTATUS and shows every
  programmed value.  Extend rather than rebuild instruments.
* **The desktop is on llvmpipe**; the hd3 flasher image still lacks
  llvmpipe/`libLLVM-13` (a reflash loses it).  The fast-tap guard
  (`gesture-handle --height 0`) is temporary (DECISIONS §114).
* **The kick10-era entry counter double-counts** — but the live firmware is
  now `kick15pds`, built with `UK_ENTRYFIRST` and (since kick15) one counter;
  entry(0x07) reads true.  The tick counter is GPU-side seeded (0x100) at
  word 0x2c.

## 0b. THE ONE EXPERIMENT FOR SESSION 81 — the state-load programs, or the USE side

**Where it stands (session 80 answered §0b of the last handoff).**  Excluded
by MEASUREMENT now, not argument: the kick path, the TA register block
(session 79), the invalidate/load protocol (session 79), the **VDM control
stream's record field layout** (session 80 — three per-core fields, emitted
for real renders, identical timeout both times), and the `TA_TE` tail
(session 80 — pruned, and the mid-hang state is unchanged).  The TA task still
starts and never finishes: `EVENT_STATUS2 TRIG_TA` at the driver's 2 s
timeout, no `TA_FINISHED`, `DPM_PAGE_STATUS 0`, the TE never starts, nothing
faults, nothing is written.

**What has never been checked on the 540 is now ONE thing: the VDM↔USE
state-load handshake itself.**  (The previous handoff named two; session 80
checked the first.)

1. ~~The content of the client state-load PDS/USE programs~~ — **DONE
   (session 80): they decode clean.**  Splitting each dump at `data_rows*4`
   words gives 540 code that is exactly our own `state_block()` record
   (`tst;bra;doutd;tst;bra;doutd;doutu;halt`, and the empty stub's
   `0x07000345 0xaf000000`); `0x0800e103` is a legitimate secondary record.
   No divergence.  **Do not re-open this** unless a *specific* field is
   suspected.
2. **The VDM↔USE handshake — the state-load task is the only client code the
   TA runs before the TE, and it is the one thing left.**  The USE side is the
   only unit whose state the status dump cannot see.

**Method.**

1. **The newest, sharpest observation (session 80, trap 419): `IDXFIFO` wakes
   mid-hang and LATCHES.**  `CLKGATESTATUS` sampled 7× at 0.3 s intervals goes
   `0x001000a0` at t=0 then `0x001800f0` for t=1…6 — the wake set over idle is
   **{MTE b4, VDM b6, IDXFIFO b19}**, TE never wakes.  **IDXFIFO is the index
   FIFO, the VDM's first read of a draw, and the SGX545 header does not mention
   it at all** — so it is the one unit this whole 545-derived port was written
   without.  There is no index-fetch *control* register (only the clock gate,
   override, status and reset bits), so the experiment is the gate:
   `EUR_CR_CLKGATECTLOVR` b16-17 (`IDXFIFO_CLKO`) to pin it ON for a run, and
   `SOFT_RESET` b16 (`IDXFIFO_RESET`).  Both **named** (trap 369 satisfied).
2. **Read the USE-side counters mid-hang too** (no boot): `USE0_SERV_EVENT`
   (0x0b10) / `USE1_SERV_EVENT` (0x0b1c) are COUNT b0-15 / EMPTY b16 service
   queues, and `LOOPBACK` (0x0b04) — all already in the `status` dump.
   Session 80 measured USE0's count climbing through the hang and pipe 1 idle,
   i.e. the EDM is alive and the *render* is what is stuck.
3. **Do NOT** re-run: the stream-encoding experiments (trap 416), the `TA_TE`
   tail (closed), the state-load program *content* (done — decodes clean), the
   TA register tables (0a), the doorbell read-back (trap 407), `CACHE_CTRL`'s
   PARTDM fields (trap 411), or Mesa/compositor work.

**Interpretation:**
* Pinning `IDXFIFO` ON changes nothing → the fetch is not gate-limited; the
  stall is in what it fetched or the VDM's consumption of it.
* Pinning it ON *moves* the stall (TE starts, DPM allocates) → the auto-gate is
  the wall and the 540's `CLKGATECTL`/override handling is the bug.
* The TE starts / pixels appear → the whole stream axis was the wall; walk
  `do_3d` next (it has never run either) toward the frame loop.

**SETTLED (trap 417) — do not build an argument on `EVENT_STATUS2`.**  It reads
`0x10` (`TRIG_TA`) on an **idle** driver and through every path (null job, full
render, mid-hang), and nothing on this stack writes it — our image has zero
stores to word 70 and `srvinit` writes only the two `EVENT_HOST_ENABLE`
registers.  **Session 79's "the TA task STARTS" is not supported by that bit**;
it means "the host delivered the kick and the µkernel consumed it", which the
CCB counters show.  What *does* show the VDM running: `CLKGATESTATUS`
(VDM/MTE/UCACHEL2/USE0 awake), the VDM/TE register block, and
`VDM_CTRL_STREAM_BASE = 0x42000`.

## 0c. The media, and the way back

* **region 0 = `0x02C1DDC2`** — `boot/chainloader/kernel.img`, 10 450 520 B,
  sha256 `70474c01…`.  Keep the image FILE; it is not byte-reproducible.
* **region 1 = `20a5edd3…`** (64 MiB at raw `0x29000000`) — the BusyBox
  fallback root, not booted; re-hashed live at the session-79 wrap-up.
* **hd3 = the session-71 rebake** `5d3eac8f…`, flashed at loader block
  `0x4915`, grown to 62.5 G; live root `/dev/loop0` ext4 rw.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`; qconn `169.254.0.1:9090`, `PB_LINK=door tools/pbssh.sh`.
* **To stock TabletOS:** the 1917 IFS at `0x825` **and**
  `local/work/rcfs-1917.rcfs` at `0x8D5`, plus the vendor UFS at `0x4915` if
  hd3 is repurposed.
* **Flash loop:** `python3 tools/bbusb.py flash --component <img>
  --dest-block 0x4915 --from-bootrom --password 0000 --apply`; **start the
  flasher, then reset with `reboot -f`**.  A region-0 flash is block `0x825`.
* **A region-0 flash needs the initramfs rebuilt too.**  `make … zImage`
  after `rm -f $K/usr/initramfs_data.cpio*` — and if the zImage does not
  relink, the cpio is embedded from the *old* one.

## 1. Bench state at the session-80 close (read on the unit)

| item | value |
|---|---|
| OS | **Linux `7.3.3`**, `Nura edge` — not TabletOS.  **No restore required** |
| region 0 | **`0x02C1DDC2`** — no flash this session.  File `boot/chainloader/kernel.img`, 10 450 520 B, sha256 `70474c01…` |
| region 1 | sha256 **`20a5edd3…`** (unchanged this session; re-hash live if it matters) |
| hd3 | the session-71 rebake **`5d3eac8f…`** at `0x4915`, grown to 62.5 G; `/dev/loop0` ext4 rw |
| `BOOTCTR_FCTRY` | **`0x20`** on the deploy boot (`pbrpmb: topup: … = 20000000`, above threshold, no write).  Self-maintaining; this session cost 3 boots |
| CPU | `nproc` 2; PID 1 affinity **`0,1`** |
| GPU | `card1`/`renderD129` = **`powervr_sgx`**; `card0`/`renderD128` = `omapdrm`.  The null family passes (671 µs; 600 jobs/96 ms; `resets 0` between render attempts); the render stall is §0b's subject |
| firmware | `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`kick16tail`** (wrapped md5 `744b072d…`; kick10's flags + UK_WAITLOG + the session-79 fixes + **the pruned `TA_TE` tail**).  Preserved beside it: **`.kick15pds` `24322c2a…`** (the session-79 image — the immediate revert), `.kick14otpm` `f1b16422…`, `.kick13inv` `688bfb8f…`, `.kick12inv` `81d7a183…`, `.kick11wl` `e5f1db18…`, `.kick10live` `3fbbd11b…`, and **`.working` = kick10 `3fbbd11b…`** (the session-78 proven pair) |
| module | `powervr_sgx.ko` = **`1a2949ab…`** (the render wiring + the extended status dump) — **unchanged this session**.  Preserved: `.predumpregs` `85fe59a2…`, **`.pre-render` = `.working` = `10c6f435…`** (the session-78 proven pair = the revert) |
| desktop | **llvmpipe** for compositor *and* clients; **not exercised this session** |
| panel | LVDS `card0-LVDS-1` connected, `bl_power` **0**.  Gesture handle 0 height in `/etc/catacomb/initrc` — a **temporary guard**, DECISIONS §114 |
| WLAN | **`wlan0` DOWN at the session-80 close** — the GPU resets of this session's render attempts take it down, and it would not re-associate: the kernel logs `wlan0: regulatory prevented using AP config, downgraded` then `authentication … timed out`, which is the documented 5 GHz/"world domain" case whose fix is `iw reg set <CC>` — and **`iw` is not installed on this Nura root** (`find / -name iw` finds nothing; region 1 carries it, hd3 does not).  Not a regression from the GPU work; the previous session saw the same down/up pattern.  `usb0` `169.254.0.1/16` UP and SSH works regardless |
| SSH | `PB_LINK=usb tools/pbssh.sh` works; if it times out check the cable, the host VPN (trap 341) and ufw (trap 342) |
| device-side files | the firmware variants above, `/etc/catacomb/initrc`, **`/tmp/powervr_sgx_test`**, and **`/tmp/powervr_sgx_frame`** = **`1177fafd…`** (session 79's VDM-stream dump restored **plus** the state-load-program extension) — with `.old79` = `efaefc61…` (session 79's tool) and `.s80base` = `83c8aef1…` kept beside it.  `/tmp` is on the ext4 (trap 354): survives reboots, lost on a reflash |
| host | clone `local/work/powervr_sgx` (upstream `95f78ca` + our uncommitted patches — **nothing is in a patch file yet**, DECISIONS §110.  Session 80 touched `ukernel/cores/sgx540.py` (`TA_TE` pruned) and `tools/gen/sgxgen.c` (`dump_prog`/`dump_progs` extended, the word-5 divergence documented)); assembler `local/work/sgxasm/{sgx-asm,sgx-disasm}`; trees `local/kernel-tree-sgx` (compiles the module) and `local/kernel-tree` (the known-good closed-DDK tree); **GPL-2 DDK 1.8 reference at `/home/psyden/Pdrive/GFX_Linux_DDK-master` (outside the repo)** — note the microkernel binary's real path has an extra `eurasia/` segment: `…/eurasia/eurasiacon/binary2_540_omap4430_android_release/target/intermediates/ukernel/ukernel.bin` |

## 2. Working rules (unchanged, plus what this session cost)

* Read `docs/project/SAFETY.md` before any write.  One variable per boot.  An
  agent never writes RPMB; NVRAM read-only.
* Kernel printk goes to the panel; `console=ttyGS0` is inert.  Use `reboot -f`.
  **Ask the operator what is on the panel**; do not screenshot.
* **Do not restart `tinydm`** (trap 353), and **do not leave Kumo running or
  kill it** (trap 367) — reboot.
* **Do not call `sync`** (trap 300).  `/tmp` is on the ext4 (trap 354).
* **Register reads:** only from inside the driver now (trap 384).
* **Revert experiments and prove the revert.**  Look up every external fact.
  Write the durable fact into the repository in the same turn.
* **A uAPI "pass" is only evidence once the fence is understood** (trap 388):
  `powervr_sgx_frame` printed "render done" on timeout runs this session —
  check `resets` and the pixels, never the verdict alone.
* **Parse every ctlblk dump BY WORD INDEX** (trap 403/406); **seed every
  counter GPU-side** (trap 409); **count an instrument's store sites in the
  disassembly** (trap 410); **decode multi-bit constants field-by-field
  against the vendor's derivation** (trap 412).
* **NEW (trap 413/414 class): a done-bit mask names only bits whose request
  register the same path pulses; a header's field list is not a bitmask to
  sum; and `EVENT_STATUS2` is a different register on the 540** — check
  every wait/mask against sgx540defs.h, not the 545's.
* **Firmware builds** must use explicit output paths, `build.py`'s output is
  raw and must be wrapped by `mkukfw --manifest`, and the SAME `UK_*` flags
  must stay set for `test_emu.py` (it REBUILDS from the environment).  Build
  both driver copies (`local/work/powervr_sgx` is the source of truth,
  `local/kernel-tree-sgx` compiles).  **A module change costs a reboot**;
  never unbind pvrsrvkm live (trap 383).  **GPU resets take `wlan0` down**.
* **Reboot before measuring anything if the boot's `open microkernel` line
  has wrapped out of dmesg** (trap 390).

## 3. The build and deploy recipe, exactly (all of it was run this session)

```bash
cd local/work/powervr_sgx
# 0. the assembler path, once per shell
export SGX_ASM=$PWD/../sgxasm/sgx-asm
# 1. firmware: raw image + manifest, EXPLICIT output paths; the SAME UK_*
#    flags must stay set for the gate in step 2
UK_TICKLOG=1 UK_ENTRYFIRST=1 UK_NO_PHAS=1 UK_TIMERDIV=100 UK_WAITLOG=1 \
  UK_BUILD_ID=<id> python3 ukernel/build.py sgx540 /tmp/<id>.bin /tmp/<id>.json
# 2. gates: both must pass before deploying, with the SAME environment
SGX_CORE=sgx540 UK_TICKLOG=1 UK_ENTRYFIRST=1 UK_NO_PHAS=1 UK_TIMERDIV=100 \
  UK_WAITLOG=1 python3 ukernel/test_emu.py                        # all OK
python3 ukernel/golden/check.py                    # SGX545 golden 83b5b0a7…
# 3. wrap it -- build.py's output is raw and the driver will reject it
python3 drm/tools/mkukfw.py /tmp/<id>.bin -o /tmp/<id>w.bin --manifest /tmp/<id>.json
python3 drm/tools/mkukfw.py --check /tmp/<id>w.bin                            # ok
# 4. push (hd3 has NO scp/sftp-server -- trap 391); preserve the old file first
cat /tmp/<id>w.bin | tools/pbssh.sh 'cat > /lib/firmware/powervr_sgx/<name>.new; md5sum …'
# 5. module: build BOTH source copies (env.sh lives at the REPO root)
. ../../tools/env.sh
cp drm/powervr_sgx/*.c drm/powervr_sgx/*.h \
   ../../kernel-tree-sgx/drivers/gpu/drm/powervr_sgx/
cd ../../kernel-tree-sgx && make ARCH=arm CROSS_COMPILE=$CROSS_COMPILE \
  M=drivers/gpu/drm/powervr_sgx modules
# 6. the frame tool (client-side; no reboot needed for a tool change)
python3 tools/gen/mkusse.py > tools/gen/usse_progs.h     # needs SGX_ASM
arm-buildroot-linux-gnueabihf-gcc -static -O2 -Wall -DSGXGEN_DRM \
  -Idocs/uapi -Itools/gen \
  -I$TC/arm-buildroot-linux-gnueabihf/sysroot/usr/include/drm \
  -o /tmp/pb-frame drm/tests/powervr_sgx_frame.c tools/gen/sgxgen.c -lm
#    ($TC = local/toolchains/armv7-eabihf; the warning about `s` in main is
#    pre-existing upstream).  Push as /tmp/powervr_sgx_frame.
# 7. reboot -f; confirm `open microkernel <id>` in dmesg (trap 390); the null
#    family FIRST (submit-null-job + submit-null-job-many), then the render.
```

## 4. Useful local extracts

* `docs/captures/sgx540-first-render-2026-11-27.txt` — **the whole campaign,
  now nine boots**: session 79's seven (the wiring, the first consumption, the
  four wait-site finds and fixes, the vendor write-set diff, the mid-hang
  TA/VDM/DPM state, the vendor field-for-field match, the stream dump) **and
  session 80's "SESSION 80 CONTINUATION"** — the state-load program dump, the
  three per-core record fields with their citations and word-level diffs, the
  tail measurement, the identical timeout lines.  Read it before quoting
  anything from either session.
* `docs/reports/282-*.md` — the numbered report for session 79 (session 80 has
  no numbered report; the capture continuation and `docs/sessions/session-80.md`
  are the record).
* `docs/reference/sgx540-factsheet.md` and `docs/reference/
  sgx540-ddk18-facts.md` — the C/K facts, including the errata list (~50
  BRNs at rev 120; 23055/31474/33181 are "workaround in uKernel") and the
  ta3dctl layout derivation.
* `local/work/powervr_sgx/ukernel/` — the microkernel; `build.py`,
  `test_emu.py`, `golden/check.py`, `cores/{sgx540,sgx545}.py` (the register
  tables and per-core constants — session 79's fixes and session 80's `TA_TE`
  pruning landed in `cores/sgx540.py`).  The vendor's `ta.asm`/
  `sgx_mkif_client.h` (in the DDK drop) are the TA-path ground truth.
* **The vendor's client-side VDM stream builder** — the thing session 80
  found and session 81 needs:
  `/home/psyden/Pdrive/GFX_Linux_DDK-master/eurasia/eurasiacon/opengles2/
  {validate,clear,scissor,accum}.c` and the record vocabulary in
  `…/eurasia/hwdefs/sgxdefs.h` (the `EURASIA_TAOBJTYPE_*`, `EURASIA_TAPDSSTATE_*`
  and `EURASIA_VDM_*` blocks, and every `#if defined(SGX545)` among them).
* `docs/contradictions/sgx540-cache-ctrl-breaks-dispatch.md` — the
  unresolved CACHE_CTRL tension (trap 411).  Do not write 0x0804's PARTDM
  fields.
* Reports 282 (session 79), 281 (session 78), 280 (session 77);
  `docs/sessions/session-80.md`.

## 5. What could not be verified

* **Why the VDM stalls** — the open question, now narrowed to the state-load
  programs or the VDM↔USE handshake (§0b).
* **Whether `EVENT_STATUS2`'s `TRIG_TA` latch is this kick's or the launch's**
  — needs the before-kick read nobody has taken (trap 417).
* **`CACHE_CTRL`'s PARTDM semantics and reset value** — unchanged (the
  contradiction file names the tests that would settle it).
* **The µkernel's per-kick `DMS_CTRL` vertex-partition update** (the vendor
  does it; ours relies on the reset value) — irrelevant at 256x256, untested
  with more geometry.
* **The panel**: not confirmed visually by the operator this session;
  `bl_power` 0, the desktop untouched.
* Unchanged: `0xb14`'s write (trap 387), `sgx540_init_regs`, the microkernel
  register tables' 3D half (`D3_RD` — never executed), the owed patch-series
  export for the session-77/78/79/80 driver+firmware changes — nothing is
  upstream (DECISIONS §110).
* **The 545-only register existence question is now moot for the tail** — it
  is pruned rather than proved absent; a write/read-back sweep would still
  settle whether the 540 implements them silently (trap 397's method).

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 81 (absolute; Linux-era 75)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a, KNOWN-ISSUES.md §2 (traps 398-417) and §3,
DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 112-117, docs/reports/INDEX.md,
report 282, docs/reference/sgx540-factsheet.md AND
docs/reference/sgx540-ddk18-facts.md,
docs/captures/sgx540-first-render-2026-11-27.txt (INCLUDING its "SESSION 80
CONTINUATION" section), docs/contradictions/sgx540-cache-ctrl-breaks-dispatch.md,
docs/sessions/session-80.md and session-79.md.

WHERE IT STANDS: Linux 7.3.3 on hd3 (Nura).  region 0 0x02C1DDC2 (keep the
image FILE), region 1 20a5edd3..., hd3 = 5d3eac8f grown to 62.5 G;
BOOTCTR_FCTRY 0x20 on the deploy boot (the Linux /init tops up below 0x1e);
card1/renderD129 = powervr_sgx running the open microkernel kick16tail
(744b072d).  THE FIRST RENDER JOB IS CONSUMED END TO END AND THE STALL IS
INSIDE THE VDM'S CONTROL-STREAM EXECUTION.  Excluded BY MEASUREMENT, not
argument: the host-kick path, the TA register block (the vendor's
SGXMKIF_TAREGISTERS matches ours field-for-field), the invalidate/OTPM/DPM-load
protocol, the VDM stream's RECORD FIELD LAYOUT (session 80: three per-core
fields found in the vendor's own client builder and emitted for real renders --
identical timeout both times), and the TA_TE tail (pruned in session 80, the
mid-hang state unchanged).  What still happens: the TA task is kicked, VDM/MTE/
UCACHEL2/USE0 are clocked awake, the TE never starts, the DPM allocates ZERO
pages, no BIF fault, and the job times out at the driver's 2s with
EVENT_STATUS2 TRIG_TA and EVENT_STATUS 0 (no TA_FINISHED).

THE ONE EXPERIMENT: the two things never checked on the 540, in order.
(1) THE CLIENT STATE-LOAD PDS/USE PROGRAMS the five STATE records name -- now
dumped (vdm_state[0/8/10/18/23] in the capture; re-dump with
SGXGEN_DUMP_PROGS=1 /tmp/powervr_sgx_frame -n 1), so this is STATIC and costs
no boot: disassemble each with sgx-disasm -c sgx540 and check every instruction
against the 540's USSE/PDS encoding (all were assembled from 545-derived
macros).  (2) THE VDM<->USE STATE-LOAD HANDSHAKE -- the state-load task is the
only client code the TA runs before the TE.  Read the USE0/USE1_SERV_EVENT and
LOOPBACK counters mid-hang, and add whatever per-unit register the 540 header
NAMES (trap 369).  Outcomes: a program that does not decode as 540 code or that
touches an unnamed register -> that is the fault, fix per-core with the SGX545
golden byte-identical and submit one render; the USE unit admits and never
retires the task -> the handshake, not the program; the TE starts or pixels
appear -> walk do_3d next (never run either) toward the frame loop.

DO NOT re-run: the stream's record-encoding experiments (trap 416 -- three
fields, measured, no change), the TA_TE tail (closed), the TA register tables,
the doorbell read-back (trap 407), CACHE_CTRL's PARTDM fields (trap 411 + the
contradiction file), or Mesa/compositor work.

THE METHOD THAT WORKED: (1) UK_WAITLOG names the stuck wait site at ctlblk
word 0x35 (the build manifest maps ids to labels); (2) the driver's status
node dumps the TA/VDM/DPM registers -- read it MID-HANG (background the frame
tool, sleep ~1s, cat status) and follow CLKGATESTATUS's awake units; (3)
disassemble the vendor 540 binary with sgx-disasm -c sgx540 and decode every
str #imm site -- it settles register/mask/value questions for this silicon
without a boot.  NOTE THE REAL PATH has an extra eurasia/ segment:
/home/psyden/Pdrive/GFX_Linux_DDK-master/eurasia/eurasiacon/
binary2_540_omap4430_android_release/target/intermediates/ukernel/ukernel.bin
(ff69e400..., md5).  (4) the vendor's ta.asm / sgx_mkif_client.h are the
TA-path ground truth (GPL-2), and its CLIENT-side VDM stream builder is
eurasiacon/opengles2/{validate,clear,scissor,accum}.c + hwdefs/sgxdefs.h.

TRAPS THAT COST TIME (416/417, new): the VDM stream's record encodings ARE
per-core in the vendor's source (index-list b24 IDXPRES45, the state record's
PARTITIONS -- 1 on the 540, 2 on the 545 -- and the draw's word 5 bit
positions), our generator emits the 545 values, and emitting the 540 values
changed NOTHING; and EVENT_STATUS2 = 0x10 at the timeout is read as TRIG_TA
latching but nobody has taken the cheap before-kick control, so "the TA starts"
is weaker than it reads (trap 417).  A git checkout of the clone took session
79's UNCOMMITTED stream dump with it -- commit an instrument before
experimenting on the file that holds it.

TRAPS THAT STILL APPLY: a uAPI "pass" is only evidence once the fence is
understood (trap 388 -- powervr_sgx_frame printed "render done" on timeout
runs; check resets and pixels); "consumed" after a reset equals "issued"
(trap 389); dmesg's "open microkernel <build>" is the only truth about which
firmware ran (trap 390) -- reboot before measuring if it has wrapped out;
hd3 has no scp/sftp-server -- push with cat FILE | tools/pbssh.sh 'cat >
/path' and md5sum first (trap 391); /dev/mem cannot reach the GPU (trap 384);
edm_init=1 is required and supported (trap 385); a module change costs a
reboot; never unbind pvrsrvkm live (trap 383); GPU resets can take wlan0
down; parse every ctlblk dump BY WORD INDEX; seed counters GPU-side.

BENCH: region 0 0x02C1DDC2; region 1 20a5edd3...; hd3 live /dev/loop0 ext4
rw; BOOTCTR 0x20 at the close; nproc 2, PID 1 affinity 0,1; battery Full;
panel bl_power 0; usb0 169.254.0.1/16 (PB_LINK=usb tools/pbssh.sh works; if
it times out check the cable, the host VPN (trap 341) and ufw (trap 342)
first).  Firmware on hd3: sgx540_ukernel.bin = kick16tail (744b072d, the
pruned TA_TE), preserved .kick15pds (24322c2a), .kick14otpm/.kick13inv/
.kick12inv/.kick11wl/.kick10live and .working = kick10 (3fbbd11b); module
powervr_sgx.ko = 1a2949ab (unchanged), preserved .predumpregs (85fe59a2) and
.pre-render = .working (10c6f435).  Desktop llvmpipe -- the hd3 flasher image
still lacks it, so a reflash loses it.  The uAPI test binary is
/tmp/powervr_sgx_test and the frame tool is /tmp/powervr_sgx_frame (1177fafd,
the stream dump + the state-load-program extension; .old79 = efaefc61) -- both
on the ext4.  The full build/deploy recipe is HANDOFF section 3.

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
