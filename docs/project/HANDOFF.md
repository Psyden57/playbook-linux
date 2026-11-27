# Handoff — session 81 (absolute) → session 82

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 82 = Linux-era 76**, written at the
session-81 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 81 RESULT: `LASTKICK` and `IDXFIFO` are excluded, and the VDM stall
> is localized to the STATE_LOAD records.**  The `UK_CCBTRACE` measurement shows
> the µkernel reads `ta_flags` and **sees LASTKICK when a kick carries it**
> (`0x73` with `-k 1`), but the default two-kick render puts LASTKICK on **kick
> 2**, which is never serviced because kick 1's TA never finishes — so the
> missing `ta_finished` branch is downstream and cannot be the fix.  Forcing
> `IDXFIFO`'s clock gate ON moves nothing.  A one-boot stream bisection then
> shows: **removing the PRIM records changes nothing; removing the STATE_LOAD
> records wakes the TE** (`CLKGATESTATUS 0x001800f8`, b3), so the VDM stops in
> the state loads, before the draw.  Three real 540 record divergences, the
> data-segment size, and two USE-program variables were corrected in variants —
> **none moves the stall**.  New traps 422–425; `docs/sessions/session-81.md`.

> **SESSION 80 RESULT (context): the VDM control stream's RECORD LAYOUT is
> walked and excluded** — three per-core fields, emitted for real renders,
> identical timeout.  Also the `TA_TE` tail is pruned (`kick16tail`).  Traps
> 416/417.

> **SESSION 79 RESULT (context): the first render job went through the open
> microkernel end to end, and its failure point is measured** — five µkernel
> fixes (traps 413/414); on `kick15pds` no wait spins, the TA is kicked, the TE
> never starts, DPM allocates zero pages, no BIF fault.  Report 282.

> **SESSION 78 RESULT (context): the dispatch pipeline is ALIVE** — the
> USE_PIPELINE_0 fix, the null family passing, zero resets.  Report 281.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**,
with a GPU-accelerated desktop (compositor **and** clients), and a modern
browser.  The long-term graphics route is the open `powervr_sgx` stack
(DECISIONS §107/§108) — this is that port, driving toward a rendered frame.

## 0a. What sessions 78–81 established (do not re-derive)

* **The dispatch pipeline is alive and jobs complete** (session 78):
  `EVENT_PDS_INFO` 0x0a70 = `0x00013c01` (`USE_PIPELINE_0`).  Keep it.  The
  null family passes (`600 jobs` in ~90 ms).
* **The µkernel's host-kick path is exhausted as a suspect** (session 79): the
  kick is consumed, both TA commands' worth of protocol run, every
  invalidate/OTPM/DPM load completes, and the per-scene TA register block
  matches the vendor's `SGXMKIF_TAREGISTERS` field-for-field.  **Do not re-audit
  the TA register tables.**
* **Five SGX545-shaped µkernel bugs fixed** (traps 413/414): the 0x138
  invalidate masks (per-request 0x03/0x05), the OTPM OP bit 3 (540 writes 1),
  the STATUS2 b12 wait (545-only), the DPM PDS-page threshold (**0x614**), and
  no free-list mode selector.
* **`LASTKICK` is excluded by measurement** (session 81): the µkernel reads
  `ta_flags` (`0x71` on kick 1, `0x73` with `-k 1`; `cmd+0x58` ready = 1) and
  the flag is on **kick 2**, which is unreachable while kick 1 stalls.  **Do
  not add the `ta_finished` branch.**  [trap 420]
* **The instrument collision is known** (trap 422): `UK_CCBTRACE` and
  `UK_WAITLOG` both write word 0x38; build the trace **without** `UK_WAITLOG`.
* **`IDXFIFO` pinned ON is a non-fixer** (session 81, module `4a928e15…`); the
  gate is already on through the hang.  [trap 419]
* **The launch-stub comparison is closed** (static): the vendor's two-program
  `emitpds` launch vs our driver-init + one `kick` is the known structural
  difference (DECISIONS §115); `KickTA`'s per-kick path matches ours.
* **The VDM stall is inside the STATE_LOAD records** (session 81, trap 425):
  remove the STATE_LOADs and the TE clock wakes; remove the PRIMs and nothing
  changes.  `IDXFIFO` b19 is therefore not the draw's index fetch.
* **The state record's fields/segment and two USE-program variables are
  excluded** (session 81): `USEPIPE = (1+pipe)<<13`, `PARTITIONS = 1` (b9),
  `PDSDMADEPENDENCY` (b21), the 4-row/64-byte segment, `EMITMTE src1 = n`, and
  dropping the 545-only `emit.t3`/`op0xd6` pair — each stalls identically.
* **The desktop is llvmpipe** (DECISIONS §119), and the closed pvr stack is
  blocked at modprobe.  The fast-tap guard (`gesture-handle --height 0`) is
  temporary (DECISIONS §114).
* **A render can wedge the DMM/TILER and storm IRQ 147 — intermittently**
  (traps 423/424).  One bisection boot ran 13 resets with `irq 147: 0`, no DMM
  errors, panel stable.  Do not assume it either way.

## 0b. THE ONE EXPERIMENT FOR SESSION 82 — diff our stream against the DDK's reference builder

**Why.**  Every field-level and USE-program variable we can vary has been
excluded, and the single robust fact is that the VDM stops **in the state-load
records** before the draw.  The DDK drop contains a complete, known-good
**reference frame builder**: `eurasia/unittests/sgx_render_flip_test/
sgx_render_flip_test.c` (it calls `USEGenWriteStateEmitProgram` and the codegen
directly, and its build product is the same DDK).  One word-level diff of its
stream against ours will either show the structural difference we have been
unable to guess, or show that the stream is not the difference.

**Inputs (all already present).**

* The DDK drop at `/home/psyden/Pdrive/GFX_Linux_DDK-master` (GPL-2; DECISIONS
  §118 clears all of it, PDFs included).
  * `eurasia/unittests/sgx_render_flip_test/sgx_render_flip_test.c`
    (the reference stream builder; it also calls
    `USEGenWriteStateEmitProgram` at `:2449`/`:3099`).
  * `eurasia/codegen/{pds,use,vdm}` — the record and program generators
    (`USEGenWriteStateEmitProgram` at `usegen/usegen.c:592`).
  * `eurasia/eurasiacon/opengles2/validate.c:3033-3090` — the per-block state
    record (`SetupStateUpdate`), the field layout we already matched.
  * `…/target/intermediates/pds_mte_state_copy/pds_mte_state_copy.h` — the
    23-word copy program.
* Our stream: the frame tool can dump it (`SGXGEN_DUMP_PROGS=1`, or the
  capture's 28 words `40e80120 0e022403 80c00003 …`), and `tools/gen/sgxgen.c`
  is the builder (`state_block`, `vdm_draw`, `vdm_end`, `pds_secondary`,
  `pds_pixel`).
* The env-gated knobs in `tools/gen/sgxgen.c`/`mkusse.py` let us emit any
  variant in one tool build (see §3).

**What each result means.**

1. **The reference stream has a record/ordering/structure we lack** (e.g. a
   different record class before the state block, a different state block form,
   a word we do not write): change the generator to match, and submit one
   render.  That is the fix candidate.  Verify with the TE-wake check
   (`CLKGATESTATUS` b3) and the pixels.
2. **The reference stream is structurally identical to ours** (field values
   aside): then the stream is not the difference.  The next suspect is the
   **state task's completion path** — instrument it GPU-side (e.g. have the
   state-emit USE program write a known marker to a client-visible PDS
   location, or use a per-program counter) and read it mid-hang.  Trap 425 has
   the pieces.
3. **The reference builder will not run as-is on our port** (it uses the DDK's
   services path, not our DRM uAPI): extract its *stream words* (or a
   normalised trace of them) rather than trying to run it; the diff is the
   deliverable.

**Do NOT re-run:** the stream's record field layout (trap 416/425), the
`TA_TE` tail, the TA register tables, the doorbell read-back (trap 407),
`CACHE_CTRL`'s PARTDM fields (trap 411), the vendor-image load (a driver port
— see §5), the `EMITMTE`/VCB/segment variants (trap 425), or Mesa work.

**Correct, but do not deploy blind:** the three known-wrong generator fields
(`USEPIPE = (1+pipe)<<13`, `PARTITIONS = 1` at b9, `PDSDMADEPENDENCY` b21).
They are real 540 divergences; fix them in the production generator once the
stall is understood (they are currently env-gated variants, so the production
stream is unchanged).

**SETTLED (trap 417): do not build an argument on `EVENT_STATUS2`.**  It reads
`0x10` on an idle driver and through every path.  What still supports "the VDM
runs": `CLKGATESTATUS`, the VDM/TE register block, `VDM_CTRL_STREAM_BASE`.

## 0c. The media, and the way back

* **region 0 = `0x02C1DDC2`** — `boot/chainloader/kernel.img`, 10 450 520 B,
  sha256 `70474c01…`, magic `0xD7C82D1F`.  Keep the image FILE; it is not
  byte-reproducible.
* **region 1 = `20a5edd3…`** (64 MiB at raw `0x29000000`) — the BusyBox
  fallback root, not booted; **re-hashed live at the session-81 wrap-up**.
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

## 1. Bench state at the session-81 close (read on the unit)

| item | value |
|---|---|
| OS | **Linux `7.3.3`**, `Nura edge` — not TabletOS.  **No restore required** |
| region 0 | **`0x02C1DDC2`** — no flash this session.  File `boot/chainloader/kernel.img`, 10 450 520 B, sha256 `70474c01…`, magic `0xD7C82D1F` |
| region 1 | sha256 **`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`** — re-hashed **live** this session |
| hd3 | the session-71 rebake **`5d3eac8f…`** at `0x4915`, grown to 62.5 G; `/dev/loop0` ext4 rw, 941.7 M used |
| `BOOTCTR_FCTRY` | **`0x30`** on the wrap-up boot (`pbrpmb: topup: … = 30000000`, above threshold, no write).  On the first deploy boot it read `0x1d` and the top-up fired (`0x1d -> 0x32`, `PASS`).  Self-maintaining; this session cost ~7 boots |
| CPU | `nproc` 2; PID 1 affinity **`0,1`** |
| GPU | `card1`/`renderD129` = **`powervr_sgx`**; `card0`/`renderD128` = `omapdrm`.  Status at the close: `firmware running: 1 lost: 0 resets: 13 irqs: 0` (13 resets from the bisection; no IRQ storm that boot) |
| firmware | `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`kick18ccb`** (wrapped md5 `d4087521…`; kick10 flags + UK_ENTRYFIRST/UK_TICKLOG/UK_NO_PHAS/UK_TIMERDIV + **`UK_CCBTRACE` without `UK_WAITLOG`**; the session-79/80 fixes and the pruned `TA_TE`).  Preserved beside it: **`.kick17ccbtrace` `79bffc34…`** (the invalid CCBTRACE+WAITLOG build — do not cite its 0x38 value), `.kick16tail` `744b072d…` (session 80), `.kick15pds` `24322c2a…`, `.kick14otpm`, `.kick13inv`, `.kick12inv`, `.kick11wl`, `.kick10live`, and **`.working` = kick10 `3fbbd11b…`** |
| module | `powervr_sgx.ko` = **`4a928e15…`** — adds `clkgatectl_idxfifo` in `pvrsgx_core_regs` (SGX540 `GENMASK(17,16)`) and a default-off `idx_clk_on` gate override in `pvrsgx_fw_start()`; **the option is reverted** (`/etc/modprobe.d/powervr-sgx-opts.conf` = `options powervr_sgx edm_init=1`).  Preserved: **`.pre-idxfifo` = `1a2949ab…`** (the session-79/80 module), `.predumpregs` `85fe59a2…`, **`.pre-render` = `.working` = `10c6f435…`** (the session-78 proven pair = the revert) |
| desktop | **llvmpipe** (measured: catacomb holds only `/dev/dri/card0`/omapdrm and maps `kms_swrast_dri.so` + `libLLVM-13`).  Do **not** conclude "on the SGX" from `map_files` (DECISIONS §119) |
| panel | LVDS `card0-LVDS-1` connected, `bl_power` **0**, dpms `On` at the close.  Gesture handle 0 height in `/etc/catacomb/initrc` — a **temporary guard**, DECISIONS §114 |
| WLAN | **`wlan0` UP** with a lease (`192.168.0.232/24`); it goes down under GPU resets and re-associates slowly.  `usb0` `169.254.0.1/16` UP and SSH works.  **`iw` is not installed on the Nura root** |
| SSH | `PB_LINK=usb tools/pbssh.sh` works; if it times out check the cable, the host VPN (trap 341) and ufw (trap 342) |
| device-side files | the firmware variants above; `/etc/catacomb/initrc`; `/etc/modprobe.d/powervr-sgx-opts.conf`; **`/tmp/powervr_sgx_test`**; **`/tmp/powervr_sgx_frame`** = `1177fafd…` (session 79's dump + the state-program extension); **`/tmp/powervr_sgx_frame.bisect`** = `b3ee9342…` (the stream-omit/stub/segment knobs); **`/tmp/pb-emitn`** `411088d8…` and **`/tmp/pb-emitn-novcb`** `18d2c85c…` (the two USE-program variants).  `/tmp` is on the ext4 (trap 354): survives reboots, lost on a reflash |
| host | clone `local/work/powervr_sgx` (upstream `95f78ca` + our uncommitted patches — **nothing is in a patch file yet**, DECISIONS §110).  Session 81 touched `drm/powervr_sgx/{pvrsgx_core.c,pvrsgx_core.h,pvrsgx_fw.c}` (the IDXFIFO gate) and `tools/gen/{sgxgen.c,mkusse.py}` (the env knobs).  **`tools/gen/usse_progs.h` currently holds the `EMIT_N + NO_VCB` build** — regenerate with plain `python3 tools/gen/mkusse.py` for production.  Assembler `local/work/sgxasm/{sgx-asm,sgx-disasm}`; trees `local/kernel-tree-sgx` (compiles the module) and `local/kernel-tree`; **GPL-2 DDK 1.8 at `/home/psyden/Pdrive/GFX_Linux_DDK-master`** (note the extra `eurasia/` segment in the binary path) |

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
  check `resets` and the pixels, never the verdict alone.
* **Parse every ctlblk dump BY WORD INDEX** (traps 403/406); **seed every
  counter GPU-side** (trap 409); **count an instrument's store sites** (trap
  410); **decode multi-bit constants field-by-field** (trap 412); **one word,
  one instrument** (trap 422 — WAITLOG and CCBTRACE collide on 0x38).
* **Firmware builds** must use explicit output paths, `build.py`'s output is
  raw and must be wrapped by `mkukfw --manifest`, and the SAME `UK_*` flags
  must stay set for `test_emu.py` (it REBUILDS from the environment).  Build
  both driver copies (`local/work/powervr_sgx` is the source of truth,
  `local/kernel-tree-sgx` compiles).  **A module change costs a reboot**;
  never unbind pvrsrvkm live (trap 383).  **GPU resets take `wlan0` down**.
* **Reboot before measuring anything if the boot's `open microkernel` line has
  wrapped out of dmesg** (trap 390).
* **A render can (intermittently) wedge the DMM and storm IRQ 147** (traps
  423/424): if it happens, the desktop/panel is not a witness and a later job
  timeout is not pure GPU evidence — reboot, or read the GPU's registers
  mid-hang instead.

## 3. The build and deploy recipe, exactly

```bash
cd local/work/powervr_sgx
# 0. the assembler path, once per shell
export SGX_ASM=$PWD/../sgxasm/sgx-asm
# 1. firmware: raw image + manifest, EXPLICIT output paths; the SAME UK_*
#    flags must stay set for the gate in step 2
UK_TICKLOG=1 UK_ENTRYFIRST=1 UK_NO_PHAS=1 UK_TIMERDIV=100 UK_WAITLOG=1 \
  UK_BUILD_ID=<id> python3 ukernel/build.py sgx540 /tmp/<id>.bin /tmp/<id>.json
#    (for a CCBTRACE build: UK_CCBTRACE=1 and NO UK_WAITLOG — trap 422)
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
# 6. the frame tool (client-side; no reboot needed for a tool change).
#    Plain = production programs; MKUSSE_EMIT_N / MKUSSE_NO_VCB change the
#    state-emit USE program; SGXGEN_* env knobs change the stream at run time
#    (see tools/gen/sgxgen.c "VDM-stream bisection").
python3 tools/gen/mkusse.py > tools/gen/usse_progs.h     # needs SGX_ASM
arm-buildroot-linux-gnueabihf-gcc -static -O2 -Wall -DSGXGEN_DRM \
  -Idocs/uapi -Itools/gen \
  -I$TC/arm-buildroot-linux-gnueabihf/sysroot/usr/include/drm \
  -o /tmp/pb-frame drm/tests/powervr_sgx_frame.c tools/gen/sgxgen.c -lm
#    ($TC = local/toolchains/armv7-eabihf; the `s` warning in main is
#    pre-existing).  Push as /tmp/powervr_sgx_frame.* and chmod +x (an upload
#    through `cat >` loses the exec bit).
# 7. reboot -f only for a firmware/module change; a tool change needs none.
#    Confirm `open microkernel <id>` in dmesg (trap 390); run the null family
#    FIRST, then the render, reading the GPU's registers mid-hang:
#      background the tool; sleep 0.5; grep the status node; sleep 0.9; grep
#    (samples at ~0.5/1.4 s catch the state before the 2 s reset)
```

## 4. Useful local extracts

* `docs/captures/sgx540-first-render-2026-11-27.txt` — **the whole campaign,
  now ten boots**: sessions 79/80 and **five session-81 sections** (the
  `UK_CCBTRACE` measurement, IDXFIFO, the panel/DMM mechanism, the static DDK
  reading, and the **VDM-stream bisection**).  Read it before quoting anything.
* `docs/sessions/session-81.md` — the session note (what is *not* in the docs
  above, especially the dead ends).
* `docs/reference/{sgx540-factsheet,sgx540-ddk18-facts}.md` — the C/K facts,
  the ~50-BRN rev-120 errata list, the `ta3dctl` derivation.
* `local/work/powervr_sgx/ukernel/` — `build.py`, `test_emu.py`,
  `golden/check.py`, `cores/{sgx540,sgx545}.py`.
* **The DDK's reference frame builder** (session 82's subject):
  `eurasia/unittests/sgx_render_flip_test/sgx_render_flip_test.c`, plus
  `eurasia/codegen/{pds,use,vdm}` and
  `eurasia/eurasiacon/opengles2/validate.c` (`SetupStateUpdate` at
  :3033-3090).
* `docs/contradictions/sgx540-cache-ctrl-breaks-dispatch.md` — the unresolved
  CACHE_CTRL tension (trap 411).  Do not write 0x0804's PARTDM fields.
* Reports 282 (session 79), 281, 280; `docs/sessions/session-80.md`.

## 5. What could not be verified

* **What in the STATE_LOAD record/program the VDM waits for** — §0b's subject.
  Not settled by the field/segment/USE variants (all excluded).
* **Whether the state-copy task ever runs to completion.**  A HALT-only program
  also stalls; a GPU-side marker from the state-emit USE program would settle
  it (not yet built).
* **The exact source of the intermittent DMM wedge / IRQ-147 storm.**  Measured
  on two boots, absent on a third; the trigger is not identified.
* **The three known-wrong generator fields** (`USEPIPE` rotation,
  `PARTITIONS = 1`, `PDSDMADEPENDENCY`) are real divergences and are **not**
  deployed (env-gated variants only).  Correct them once the stall is
  understood.
* **`TE_PSG` (0x218) reads `0x01400000`** (b22 + b24) though the vendor 540
  µkernel only ORs those on the terminate/context-switch paths; it is a
  signature register by its neighbours.  A write/read-back would settle it.
* **"Load the vendor microkernel as a control" is a driver port, not a file
  swap** — 9 `limm` sites match our magic but all carry the sentinel *encoded*
  (`0xfc237150`), which our `fw_limm()` guard rejects; 54 per-command entry
  points vs our 5; a three-program `emitpds` launch; no `pdtable` site.
  (DECISIONS §115; do not re-derive.)
* **`CACHE_CTRL`'s PARTDM semantics and reset value** — unchanged (the
  contradiction file names the tests).
* Unchanged: the owed patch-series export for the session-77…81 driver and
  microkernel changes — nothing is upstream (DECISIONS §110).

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 82 (absolute; Linux-era 76)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a, KNOWN-ISSUES.md §2 (traps 398-425) and §3,
DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 112-121, docs/reports/INDEX.md,
report 282, docs/reference/sgx540-factsheet.md AND
docs/reference/sgx540-ddk18-facts.md,
docs/captures/sgx540-first-render-2026-11-27.txt (ALL session-80 and session-81
sections), docs/contradictions/sgx540-cache-ctrl-breaks-dispatch.md,
docs/sessions/session-81.md, session-80.md and session-79.md.

WHERE IT STANDS: Linux 7.3.3 on hd3 (Nura).  region 0 0x02C1DDC2 (no flash;
re-verified from the tracked file: CRC 0x02C1DDC2, magic 0xD7C82D1F, sha256
70474c01..., 10 450 520 B), region 1 20a5edd3... (re-hashed live this session),
hd3 = 5d3eac8f grown to 62.5 G, BOOTCTR_FCTRY 0x30.  card1/renderD129 =
powervr_sgx running the open microkernel kick18ccb (d4087521, UK_CCBTRACE
without UK_WAITLOG); module 4a928e15 (idx_clk_on default off, option reverted).
wlan0 UP (192.168.0.232); usb0 up; desktop LLVMPIPE -- measured, and the closed
pvr stack is blocked at modprobe.  Do NOT conclude "on the SGX" from
/proc/PID/map_files -- ask GL (DECISIONS 119).

THE RENDER STALL, NOW LOCALIZED: the VDM stops inside the STATE_LOAD records.
Excluded BY MEASUREMENT: the host-kick path, the TA register block, the
invalidate/OTPM/DPM-load protocol, the stream's RECORD FIELD LAYOUT (trap 416),
the TA_TE tail, LASTKICK (the µkernel sees it on kick 2, which is unreachable
while kick 1 stalls -- trap 420), IDXFIFO's gate (trap 419), the three 540
record fields, the data-segment size, and two USE-program variables (trap 425).
The bisection: removing the PRIMs changes nothing; removing the STATE_LOADs
wakes the TE (CLKGATESTATUS 0x001800f8, b3).

THE ONE EXPERIMENT (HANDOFF section 0b): a word-level diff of our 28-word VDM
stream against the DDK's own reference frame builder,
eurasia/unittests/sgx_render_flip_test/sgx_render_flip_test.c (it calls
USEGenWriteStateEmitProgram and the codegen directly on this same DDK).  Read
it, extract its state records and draw words for a small clear, and compare.
If it has a record/ordering/structure we lack -> that is the fix candidate;
submit one render and check the TE wake and pixels.  If it is structurally
identical -> the stream is not the difference; instrument the state-copy task's
completion GPU-side (a marker from the state-emit USE program) and read it
mid-hang.  Do NOT re-run the excluded variants.

READ THE DDK DROP, IT IS ALL EVIDENCE (DECISIONS 118): the headers, srvinit C,
the microkernel assembly (services4/srvinit/devices/sgx/*.asm -- ta.asm exports
TAEventHandler/KickTA/TAFinished) and the 30 spec PDFs (pdftotext -layout
works).  DECISIONS 115 still stands: the microkernel we RUN is ours; the vendor
binary is evidence, never a deployment target.

THE METHOD THAT WORKED: read the driver's status node MID-HANG (background the
tool, sample at ~0.5/1.4 s) and follow CLKGATESTATUS; UK_WAITLOG names a stuck
wait at ctlblk word 0x35 (but never with UK_CCBTRACE -- trap 422); patch tables
in one tool build with env knobs and batch variants per boot; read the DDK's
own builder before inferring a record.

TRAPS THAT COST TIME (422-425, new): one control-block word can hold only one
instrument (CCBTRACE and WAITLOG collide on 0x38); a render can storm IRQ 147
to 100001 and wedge the DMM/TILER, but INTERMITTENTLY -- a third boot ran 13
resets with irq 147:0 and no DMM errors, so neither a healthy panel nor a
timeout is proof on its own; LASTKICK is on kick 2, not kick 1; the state
record's fields and USE-program variables are excluded and the stall is in the
state load.

TRAPS THAT STILL APPLY: a uAPI "pass" is only evidence once the fence is
understood (trap 388); "consumed" after a reset equals "issued" (trap 389);
dmesg's "open microkernel <build>" is the only truth about which firmware ran
(trap 390) -- /var/log/dmesg survives when the ring wraps; hd3 has no
scp/sftp-server -- push with cat FILE | tools/pbssh.sh 'cat > /path' and md5sum
first (trap 391), and chmod +x after an upload; /dev/mem cannot reach the GPU
(trap 384); edm_init=1 is required (trap 385); a module change costs a reboot;
never unbind pvrsrvkm live (trap 383); GPU resets take wlan0 down; parse every
ctlblk dump BY WORD INDEX; seed counters GPU-side; measure a renderer by asking
GL (DECISIONS 119); do not build an argument on EVENT_STATUS2 (trap 417).

BENCH: region 0 0x02C1DDC2; region 1 20a5edd3...; hd3 live /dev/loop0 ext4 rw
56.5 G; BOOTCTR 0x30; nproc 2, PID 1 affinity 0,1; battery Full; panel
bl_power 0, dpms On; usb0 169.254.0.1/16 (PB_LINK=usb tools/pbssh.sh works; if
it times out check the cable, the host VPN (trap 341) and ufw (trap 342)
first).  iw is NOT installed on the Nura root.  Firmware on hd3:
sgx540_ukernel.bin = kick18ccb (d4087521), preserved .kick17ccbtrace
(79bffc34, invalid -- do not cite), .kick16tail (744b072d) and the earlier
variants, .working = kick10 (3fbbd11b); module powervr_sgx.ko = 4a928e15,
.pre-idxfifo (1a2949ab), .pre-render = .working (10c6f435).  Tools on the
ext4: /tmp/powervr_sgx_test, /tmp/powervr_sgx_frame (1177fafd),
/tmp/powervr_sgx_frame.bisect (b3ee9342, the env-knob build), /tmp/pb-emitn
(411088d8), /tmp/pb-emitn-novcb (18d2c85c).  The clone's
tools/gen/usse_progs.h is the EMIT_N+NO_VCB build -- regenerate plain for
production.  Full recipe: HANDOFF section 3.

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
