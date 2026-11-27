# Handoff — session 82 (absolute) → session 83

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 83 = Linux-era 77**, written at the
session-82 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 82 RESULT: the wall is the MTE state EMIT.**  The DDK's own
> reference frame builder gave one structural difference not previously varied —
> every PDS address in the VDM stream is `PDSExecBase`-relative, and ours were
> absolute — and emitting it changed the stream byte-for-byte but not the stall,
> so that is a real divergence and **not** the wall.  A one-variable bisection of
> the primary state record then localized it: **clearing `MTE_EMIT` (b17) wakes
> the TE** (`CLKGATESTATUS 0x001800f8`), while clearing `SD` or setting
> `USEPIPE_ALL` does not.  The VDM waits for the MTE state emit from the
> state-copy task and it never arrives; **the vendor's exact 540 state-emit USE
> program form still stalls**, so the task never reaches its emit — the launch
> (`DOUTD`/`DOUTU`) is the next suspect.  New traps 426/427; report 283;
> `docs/sessions/session-82.md`.

> **SESSION 81 RESULT (context): `LASTKICK` and `IDXFIFO` are excluded, and the
> VDM stall is localized to the STATE_LOAD records.**  Traps 419/420/422–425.

> **SESSION 80 RESULT (context): the stream's record layout is walked and
> excluded; the `TA_TE` tail is pruned.**  Traps 416/417.

> **SESSION 79 RESULT (context): the first render job went through the open
> microkernel end to end, and its failure point is measured.**  Report 282.

> **SESSION 78 RESULT (context): the dispatch pipeline is ALIVE** — the
> USE_PIPELINE_0 fix.  Report 281.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**,
with a GPU-accelerated desktop (compositor **and** clients), and a modern
browser.  The long-term graphics route is the open `powervr_sgx` stack
(DECISIONS §107/§108) — this is that port, driving toward a rendered frame.

## 0a. What sessions 78–82 established (do not re-derive)

* **The dispatch pipeline is alive and jobs complete** (session 78):
  `EVENT_PDS_INFO` 0x0a70 = `0x00013c01` (`USE_PIPELINE_0`).  Keep it.
* **The µkernel's host-kick path is exhausted as a suspect** (session 79): the
  kick is consumed, the invalidate/OTPM/DPM loads complete, the TA register
  block matches the vendor's `SGXMKIF_TAREGISTERS` field-for-field.  **Do not
  re-audit the TA register tables.**
* **`LASTKICK` is excluded by measurement** (session 81): the µkernel sees it on
  **kick 2**, unreachable while kick 1 stalls.  **Do not add the
  `ta_finished` branch.**
* **The VDM stall is in the STATE_LOAD records** (session 81, trap 425):
  removing them wakes the TE; removing the PRIMs changes nothing.
* **The record's fields are excluded** (session 81): `USEPIPE`, `PARTITIONS`,
  `PDSDMADEPENDENCY`, the data-segment size, `EMITMTE src1`, the 545-only
  `emit.t3`/`op0xd6` pair — each stalls identically.  (Correction, session 82:
  the vendor's `USEPIPE` is `(USEPIPE_1 + pipe)` = `0x2000|pipe`, which our
  production `counter+0x2000` already matched; the session-81 `(1+pipe)<<13`
  was a double shift.  It changes nothing — the first block is pipe 0.)
* **The VDM stream's PDS addresses are `PDSExecBase`-relative on the 540, and
  ours were absolute** (session 82, trap 427) — a real divergence, **measured
  not the wall**.  Production stays absolute (`SGXGEN_REL_PDS` opts in).  Our
  tool already subtracts it for the pixel PDS words inside the state block
  (`pds_word()`).
* **`MTE_EMIT` (b17) is the wall** (session 82, trap 426): clearing it wakes the
  TE (`0x001800f8`); `SD` and `USEPIPE` do not.  The VDM waits for the MTE state
  emit from the state-copy task; it never arrives.
* **The vendor's exact 540 state-emit USE program form still stalls** (session
  82): `MKUSSE_VENDOR_STATE` (no `phas`/prologue/VCB, `EMITMTE` flags `END`,
  `src1 = numWrites`) → TE asleep.  The emit *instruction* is not the wall.
* **IDXFIFO's gate, the launch stub, and `CACHE_CTRL`** are closed/non-fixers
  (traps 419, 411, and the session-81 launch-stub reading).
* **The desktop is llvmpipe** (DECISIONS §119), and the closed pvr stack is
  blocked at modprobe.
* **A render can (intermittently) storm IRQ 147 and wedge the DMM/TILER**
  (traps 423/424).  Session 82 saw the storm on one boot and not on another
  (8 resets, `irq 147: 0`), and no DMM wedge.  Do not assume either way.

## 0b. THE ONE EXPERIMENT FOR SESSION 83 — is the state-copy task launched at all?

**Why.**  The stall is now pinned to the MTE state emit (`MTE_EMIT` b17), and
the vendor's exact emit form does not deliver it.  Two possibilities remain: the
PDS state-copy task is never **launched** (its `DOUTU`/`DOUTD` is wrong), or it
launches and hangs before its `emit.t1`.  One experiment separates them.

**Do both halves, in this order.**

1. **GPU-side marker (does the task run?).**  Put a store of a fixed constant at
   the **top** of the state-emit USE program (the first instruction, before the
   `mov o,pa` copies), writing to a client-visible PDS location — the state
   program's own data segment, or a dedicated word the tool can read back.  Then
   submit one render and read that word from the **PDS-vertex ring** after the
   render (the frame tool maps the rings; `SGXGEN_DUMP_PROGS=1` already dumps
   them, and a second dump after the frame works because the ring memory is not
   zeroed by the rewind).  A marker present ⇒ the task started; absent ⇒ it was
   never launched, and the PDS `DOUTU` (task start) is the wall.  This is the
   `docs/ISA.md` `movs`/`doutd` path — the clone's `mkusse.py` builds these
   programs from text, so it is a source change, not a bit-hack.
2. **The vendor's compiled state-copy PDS program.**  Our state-copy program is
   a flat `TST;BRA;DOUTD ×2;DOUTU` with `DOUTD` `src2 = 0`.  The vendor's own
   compiled program
   (`eurasia/eurasiacon/binary2_540_omap4430_android_release/target/intermediates/pds_mte_state_copy/pds_mte_state_copy.h`,
   23 words, `PDS_MTESTATECOPY_DATA_SEGMENT_SIZE = 64`) is a **loop**
   (`17000CE0`) whose `DOUTD` uses `src2` row **0x18** (`07030203`).  Emit it
   with our DMA source (DS0 word 0) and our USE address patched in, and submit
   one render.  If the TE wakes, the DOUTD operand was the wall; if not, the
   marker half has already told you whether the task ran.

**Inputs (all already present).**

* The DDK drop at `/home/psyden/Pdrive/GFX_Linux_DDK-master` (GPL-2; DECISIONS
  §118 clears all of it).
  * `eurasia/codegen/usegen/usegen.c:592-700` — `USEGenWriteStateEmitProgram`
    (the emit form; already matched, negative).
  * `…/intermediates/pds_mte_state_copy/pds_mte_state_copy.{h,sizeof.h}` — the
    23-word PDS state-copy program and its DMA/USE patch locations
    (`DMA0/1/2/3` at words 0/1/4/5, `USE0/1/2` at 8/9/12).
  * `eurasia/eurasiacon/opengles2/validate.c:3033-3090` — `SetupStateUpdate`
    (the per-block record; the field layout we already match).
  * `eurasia/unittests/sgx_render_flip_test/sgx_render_flip_test.c` — the
    reference builder (state `:2498`, index list `:3033`, terminate `:3152`,
    `srft_WriteMTEStateCode:2430` calls `USEGenWriteStateEmitProgram` and
    `PDSGenerateStateCopyProgram`).
* Our stream tool: `tools/gen/sgxgen.c` (`state_block`, `pds_secondary`,
  `pds_pixel`, `vdm_draw`, `vdm_end`) and `tools/gen/mkusse.py` (the USE
  programs — `state_emit`).  `/tmp/powervr_sgx_frame.loc` (`68bc15af…`) is the
  production default with the session-82 knobs; regenerate from source for a new
  program.
* The DDK's `PDSGenerateStateCopyProgram` (codegen/pds) — if it is more reliable
  than transcribing the compiled words, read it and emit its output.

**What each result means.**  Marker present + vendor program wakes the TE ⇒ the
DOUTD operand was the wall; adopt the vendor's program.  Marker present + still
stalls ⇒ the task runs but its emit is not accepted — go to the emit's MTE
control/`emit.t1` fields.  Marker absent ⇒ the PDS `DOUTU` never starts the
task; check the `doutu_w0` code-base/offset for `state_emit` (it works for
`empty_v`, so the difference is the program's size/address).

**Do NOT re-run:** the record-field variants (USEPIPE/PARTITIONS/DEP/SEG4/attr —
traps 416/425), the PDS address encoding (trap 427), `LASTKICK` (trap 420),
`IDXFIFO` (trap 419), the TA register tables, the doorbell read-back (trap 407),
`CACHE_CTRL`'s PARTDM fields (trap 411), the vendor-image load (a driver port —
§5), or Mesa work.

**SETTLED (trap 417): do not build an argument on `EVENT_STATUS2`.**  It reads
`0x10` on an idle driver.  What supports "the VDM runs": `CLKGATESTATUS`, the
VDM/TE register block, `VDM_CTRL_STREAM_BASE`.

## 0c. The media, and the way back

* **region 0 = `0x02C1DDC2`** — `boot/chainloader/kernel.img`, 10 450 520 B,
  sha256 `70474c01…`, magic `0xD7C82D1F`.  Keep the image FILE.
* **region 1 = `20a5edd3…`** (64 MiB at raw `0x29000000`) — the BusyBox
  fallback root, not booted.
* **hd3 = the session-71 rebake** `5d3eac8f…`, block `0x4915`, grown to 62.5 G;
  live root `/dev/loop0` ext4 rw.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`; qconn `169.254.0.1:9090`, `PB_LINK=door tools/pbssh.sh`.
* **To stock TabletOS:** the 1917 IFS at `0x825` **and**
  `local/work/rcfs-1917.rcfs` at `0x8D5`, plus the vendor UFS at `0x4915` if
  hd3 is repurposed.
* **Flash loop:** `python3 tools/bbusb.py flash --component <img>
  --dest-block 0x4915 --from-bootrom --password 0000 --apply`; **start the
  flasher, then reset with `reboot -f`**.  A region-0 flash is block `0x825`.
* **A region-0 flash needs the initramfs rebuilt too** — `make … zImage` after
  `rm -f $K/usr/initramfs_data.cpio*`.

## 1. Bench state at the session-82 close (read on the unit)

| item | value |
|---|---|
| OS | **Linux `7.3.3`**, `Nura edge` — not TabletOS.  **No restore required** |
| region 0 | **`0x02C1DDC2`** — no flash this session.  `boot/chainloader/kernel.img`, 10 450 520 B, sha256 `70474c01…`, magic `0xD7C82D1F` |
| region 1 | sha256 **`20a5edd3…`** |
| hd3 | the session-71 rebake **`5d3eac8f…`** at `0x4915`, grown to 62.5 G; `/dev/loop0` ext4 rw |
| `BOOTCTR_FCTRY` | **`0x2e`** on the wrap-up boot (`pbrpmb: topup: … = 2e000000`, above threshold, no write) |
| CPU | `nproc` 2; PID 1 affinity **`0,1`** |
| GPU | `card1`/`renderD129` = **`powervr_sgx`**; `card0`/`renderD128` = `omapdrm`.  Close: `firmware running: 1 lost: 0 resets: 8 irqs: 0`, `BIF_FAULT 0` |
| firmware | `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`kick18ccb`** (md5 `d4087521…`; `UK_CCBTRACE` without `UK_WAITLOG`).  Preserved beside it `.kick17ccbtrace` `79bffc34…` (invalid), `.kick16tail` `744b072d…`, `.kick15pds`, …, `.working` = kick10 `3fbbd11b…` |
| module | `powervr_sgx.ko` = **`4a928e15…`** (`idx_clk_on` default off, option reverted).  Preserved `.pre-idxfifo` `1a2949ab…`, `.pre-render` = `.working` `10c6f435…` |
| desktop | **llvmpipe** (DECISIONS §119).  Do **not** conclude "on the SGX" from `map_files` |
| panel | LVDS `card0-LVDS-1`, `bl_power` 0, dpms On; gesture handle 0 height (temporary, DECISIONS §114) |
| WLAN | `wlan0` UP (`192.168.0.232/24`); `usb0` `169.254.0.1/16` UP, SSH works.  **`iw` is not installed** |
| device-side tools | `/tmp/powervr_sgx_frame` `1177fafd…` (baseline), **`.loc` `68bc15af…` (production default + the `SGXGEN_STATE_NO_MTEEMIT/NO_SD/PIPE_ALL` and `SGXGEN_REL_PDS` knobs)**, `.relpds` `ebf463f3…` (relative PDS — negative), `.vend` `a0d48593…` (vendor emit form — negative), `.bisect` `b3ee9342…`, `pb-emitn` `411088d8…`, `pb-emitn-novcb` `18d2c85c…`, `/tmp/powervr_sgx_test`.  `/tmp` is on the ext4 (trap 354) |
| host | clone `local/work/powervr_sgx` (upstream `95f78ca` + uncommitted patches; DECISIONS §110).  Session 82 touched `tools/gen/sgxgen.c` (`pds_record_addr`, the six record sites, `dump_progs`, the state-record knobs) and `tools/gen/mkusse.py` (`MKUSSE_VENDOR_STATE/_NOF3/_NO_PHA/_NO_PROLOGUE`); **`tools/gen/usse_progs.h` regenerated plain**.  Assembler `local/work/sgxasm/{sgx-asm,sgx-disasm}`; trees `local/kernel-tree-sgx` and `local/kernel-tree`; DDK 1.8 at `/home/psyden/Pdrive/GFX_Linux_DDK-master` |

## 2. Working rules (unchanged, plus what this session cost)

* Read `docs/project/SAFETY.md` before any write.  One variable per boot.  An
  agent never writes RPMB; NVRAM read-only.
* Kernel printk goes to the panel; `console=ttyGS0` is inert.  Use `reboot -f`.
  **Ask the operator what is on the panel**; do not screenshot.
* **Do not restart `tinydm`** (trap 353), and **do not leave Kumo running or
  kill it** (trap 367) — reboot.
* **Do not call `sync`** (trap 300).  `/tmp` is on the ext4 (trap 354).
* **Register reads:** only from inside the driver (trap 384).
* **Revert experiments and prove the revert.**  Look up every external fact.
  Write the durable fact into the repository in the same turn.
* **A uAPI "pass" is only evidence once the fence is understood** (trap 388).
* **Parse every ctlblk dump BY WORD INDEX**; **seed every counter GPU-side**
  (trap 409); **one word, one instrument** (trap 422).
* **Firmware builds**: explicit output paths, `build.py`'s output is raw and
  must be wrapped by `mkukfw --manifest`, the SAME `UK_*` flags for
  `test_emu.py`.  Build both driver copies.  **A module change costs a reboot**;
  never unbind pvrsrvkm live (trap 383).  **GPU resets take `wlan0` down.**
* **Reboot before measuring anything if the boot's `open microkernel` line has
  wrapped out of dmesg** (trap 390) — or confirm the firmware file's md5.
* **A render can (intermittently) wedge the DMM and storm IRQ 147** (traps
  423/424): if it happens the panel is not a witness and a post-storm timeout is
  not pure GPU evidence — reboot, or read the GPU's registers mid-hang.  Session
  82 measured one boot with the storm and one with 8 clean resets, same day.
* **`MTE_EMIT` is the wall** (trap 426) — do not re-run the record-field
  variants, and do not expect the emit *instruction* to be the fix.

## 3. The build and deploy recipe, exactly

```bash
cd local/work/powervr_sgx
export SGX_ASM=$PWD/../sgxasm/sgx-asm
ROOT=$(cd ../../.. && pwd)
export PATH=$ROOT/local/toolchains/armv7-eabihf/bin:$PATH
# 1. firmware (only if the µkernel changes): explicit output paths; the SAME
#    UK_* flags must stay set for the gate in step 2, and a CCBTRACE build must
#    NOT also set UK_WAITLOG (trap 422).
UK_TICKLOG=1 UK_ENTRYFIRST=1 UK_NO_PHAS=1 UK_TIMERDIV=100 UK_WAITLOG=1 \
  UK_BUILD_ID=<id> python3 ukernel/build.py sgx540 /tmp/<id>.bin /tmp/<id>.json
SGX_CORE=sgx540 UK_TICKLOG=1 UK_ENTRYFIRST=1 UK_NO_PHAS=1 UK_TIMERDIV=100 \
  UK_WAITLOG=1 python3 ukernel/test_emu.py                        # all OK
python3 ukernel/golden/check.py                    # SGX545 golden 83b5b0a7…
python3 drm/tools/mkukfw.py /tmp/<id>.bin -o /tmp/<id>w.bin --manifest /tmp/<id>.json
python3 drm/tools/mkukfw.py --check /tmp/<id>w.bin                            # ok
# push (hd3 has NO scp/sftp-server — trap 391), preserve the old file first:
cat /tmp/<id>w.bin | ../../../tools/pbssh.sh 'cat > /lib/firmware/powervr_sgx/<name>.new; md5sum …'
# 2. module: build BOTH source copies (env.sh at the REPO root)
. ../../../tools/env.sh
cp drm/powervr_sgx/*.c drm/powervr_sgx/*.h \
   ../../../kernel-tree-sgx/drivers/gpu/drm/powervr_sgx/
cd ../../../kernel-tree-sgx && make ARCH=arm CROSS_COMPILE=$CROSS_COMPILE \
  M=drivers/gpu/drm/powervr_sgx modules
# 3. the frame tool (client-side; no reboot needed for a tool change).
#    Plain usse_progs.h = production.  The session-82 knobs:
#      MKUSSE_VENDOR_STATE / _NOF3 / _NO_PHA / _NO_PROLOGUE   (USE programs)
#      SGXGEN_REL_PDS, SGXGEN_STATE_NO_MTEEMIT/NO_SD/PIPE_ALL  (runtime)
python3 tools/gen/mkusse.py > tools/gen/usse_progs.h     # needs SGX_ASM
TC=$ROOT/local/toolchains/armv7-eabihf
arm-buildroot-linux-gnueabihf-gcc -static -O2 -Wall -DSGXGEN_DRM \
  -Idocs/uapi -Itools/gen \
  -I$TC/arm-buildroot-linux-gnueabihf/sysroot/usr/include/drm \
  -o /tmp/pb-frame drm/tests/powervr_sgx_frame.c tools/gen/sgxgen.c -lm
#    (the `s` warning in main is pre-existing).  Push as /tmp/powervr_sgx_frame.*
#    and chmod +x (an upload through `cat >` loses the exec bit).
# 4. reboot -f only for a firmware/module change; a tool change needs none.
#    Confirm `open microkernel <id>` in dmesg; run the null family FIRST; then
#    background the frame tool and sample the status node at ~0.5/1.4 s:
#      S=/sys/kernel/debug/dri/129/status
#      SGXGEN_DUMP_PROGS=1 /tmp/powervr_sgx_frame.loc -n 1 >/tmp/r.txt 2>&1 & P=$!
#      sleep 0.5; grep -E 'CLKGATESTATUS|TE_STATE|DPM_PAGE_STATUS' $S; \
#      sleep 0.9; grep CLKGATESTATUS $S; wait $P; grep -E 'frame 0|frames right' /tmp/r.txt
```

## 4. Useful local extracts

* `docs/captures/sgx540-first-render-2026-11-27.txt` — **the whole campaign,
  sessions 79–82**; read the **"SESSION 82"** section before quoting anything.
* `docs/reports/283-*.md` — the session-82 report (the stream diff and
  `MTE_EMIT`); `282` is session 79.
* `docs/sessions/session-82.md` — the session note (dead ends included).
* `docs/reference/{sgx540-factsheet,sgx540-ddk18-facts}.md` — the C/K facts.
* `local/work/powervr_sgx/ukernel/` — `build.py`, `test_emu.py`,
  `golden/check.py`, `cores/{sgx540,sgx545}.py`.
* **The DDK reference builder and the vendor state-copy program:**
  `eurasia/unittests/sgx_render_flip_test/sgx_render_flip_test.c`,
  `eurasia/codegen/usegen/usegen.c:592-700`,
  `…/target/intermediates/pds_mte_state_copy/pds_mte_state_copy.h`.
* `docs/ISA.md` (the USSE encoding, for a marker instruction),
  `docs/PDS.md` (the PDS `DOUTD`/`DOUTU`).
* `docs/contradictions/sgx540-cache-ctrl-breaks-dispatch.md` — do not write
  0x0804's PARTDM fields.

## 5. What could not be verified

* **Whether the state-copy task is launched** — §0b's subject.  A GPU-side
  marker settles it; not yet built.
* **Whether adopting the vendor's `pds_mte_state_copy` moves the stall** — never
  submitted.
* **Why the relative vs absolute PDS address is behaviourally identical** —
  whether the VDM ignores the base or masks the field.  Not blocking.
* **The exact source of the intermittent DMM wedge / IRQ-147 storm** — measured
  on some boots, absent on others; the trigger is not identified.
* **`TE_PSG` (0x218) reads `0x01400000`** though the vendor 540 µkernel only ORs
  those on the terminate/context-switch paths; a write/read-back would settle it.
* **"Load the vendor microkernel as a control" is a driver port, not a file
  swap** — the `limm` sites are filled, 54 per-command entries, a three-program
  launch.  (DECISIONS §115; do not re-derive.)
* **`CACHE_CTRL`'s PARTDM semantics and reset value** — unchanged (the
  contradiction file names the tests).
* Unchanged: the owed patch-series export for the session-77…82 driver and
  microkernel changes — nothing is upstream (DECISIONS §110).

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 83 (absolute; Linux-era 77)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a (a4e), KNOWN-ISSUES.md §2 (traps 416-427) and §3,
DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 112-122, docs/reports/INDEX.md,
report 283 AND 282, docs/reference/sgx540-factsheet.md AND
docs/reference/sgx540-ddk18-facts.md,
docs/captures/sgx540-first-render-2026-11-27.txt (ALL session-81 and session-82
sections), docs/contradictions/sgx540-cache-ctrl-breaks-dispatch.md,
docs/sessions/session-82.md, session-81.md and session-80.md.

WHERE IT STANDS: Linux 7.3.3 on hd3 (Nura).  region 0 0x02C1DDC2 (no flash),
region 1 20a5edd3..., hd3 = 5d3eac8f grown to 62.5 G, BOOTCTR_FCTRY 0x2e.
card1/renderD129 = powervr_sgx running the open microkernel kick18ccb
(d4087521, UK_CCBTRACE without UK_WAITLOG); module 4a928e15.  wlan0 UP
(192.168.0.232); usb0 up; desktop LLVMPIPE -- measured, and the closed pvr stack
is blocked at modprobe.  Do NOT conclude "on the SGX" from /proc/PID/map_files
-- ask GL (DECISIONS 119).

THE RENDER STALL, NOW PINNED TO ONE FIELD: the VDM waits for the MTE state EMIT.
The primary state record (SD b25 | MTE_EMIT b17 | USEPIPE_1 | PARTITIONS) stalls;
the vertex-secondary record (SECONDARY b8 | USEPIPE_ALL, no MTE_EMIT) does not.
Clearing MTE_EMIT (b17) wakes the TE -- CLKGATESTATUS 0x001800f8 -- while
clearing SD or setting USEPIPE_ALL does not (reproduced twice each).  The
vendor's exact 540 state-emit USE program form still stalls, so the emit
instruction is not the wall: the state-copy task never reaches it.  The VDM
stream's PDS addresses are PDSExecBase-relative (ours were absolute -- measured
NOT the wall; production stays absolute, SGXGEN_REL_PDS opts in).  Excluded BY
MEASUREMENT: the host-kick path, the TA register block, the
invalidate/OTPM/DPM-load protocol, the record's field layout (traps 416/425),
LASTKICK (trap 420), IDXFIFO (trap 419), the PDS address encoding (trap 427).

THE ONE EXPERIMENT (HANDOFF section 0b), two halves: (1) a GPU-side MARKER -- a
fixed-constant store at the top of the state-emit USE task (and right after the
PDS DOUTD), read from the PDS-vertex ring after a render -- to say whether the
task runs; (2) the vendor's compiled state-copy PDS program
(intermediates/pds_mte_state_copy/pds_mte_state_copy.h: 23 words, 64-byte
segment, a loop at 17000CE0, DOUTD src2 row 0x18 = 07030203) with our DMA source
and USE address patched in.  Marker absent -> the PDS DOUTU never starts the
task; marker present + vendor program stalls -> the emit's MTE control.  Do NOT
re-run the excluded variants.

READ THE DDK DROP, IT IS ALL EVIDENCE (DECISIONS 118): the headers, srvinit C,
the microkernel assembly (services4/srvinit/devices/sgx/*.asm) and the 30 spec
PDFs (pdftotext -layout works).  DECISIONS 115 still stands: the microkernel we
RUN is ours; the vendor binary is evidence, never a deployment target.

THE METHOD THAT WORKED: read the driver's status node MID-HANG (background the
tool, sample at ~0.5/1.4 s) and follow CLKGATESTATUS; advance one VARIABLE per
boot (the env knobs in tools/gen/sgxgen.c and tools/gen/mkusse.py) and batch
variants per boot; read the DDK's own builder before inferring a record.

TRAPS THAT COST TIME (426-427, new): MTE_EMIT (b17) is the field the VDM waits
on -- clearing it wakes the TE, and the vendor's emit form does not fix it, so
the task's launch is the suspect; the VDM record PDS address is PDSExecBase-
relative and ours were absolute -- emitting it changes the stream but not the
stall.  Also: USEPIPE is (USEPIPE_1+pipe) = 0x2000|pipe, not (1+pipe)<<13.

TRAPS THAT STILL APPLY: a uAPI "pass" is only evidence once the fence is
understood (trap 388); "consumed" after a reset equals "issued" (trap 389);
dmesg's "open microkernel <build>" is the only truth about which firmware ran
(trap 390) -- /var/log/dmesg survives when the ring wraps; hd3 has no
scp/sftp-server -- push with cat FILE | tools/pbssh.sh 'cat > /path' and md5sum
first (trap 391), and chmod +x after an upload; /dev/mem cannot reach the GPU
(trap 384); edm_init=1 is required (trap 385); a module change costs a reboot;
never unbind pvrsrvkm live (trap 383); GPU resets take wlan0 down; parse every
ctlblk dump BY WORD INDEX; seed counters GPU-side; one word, one instrument
(trap 422); measure a renderer by asking GL (DECISIONS 119); do not build an
argument on EVENT_STATUS2 (trap 417); a render can storm IRQ 147 and wedge the
DMM/TILER, INTERMITTENTLY (traps 423/424).

BENCH: region 0 0x02C1DDC2; region 1 20a5edd3...; hd3 live /dev/loop0 ext4 rw
56.5 G; BOOTCTR 0x2e; nproc 2, PID 1 affinity 0,1; battery Full; panel
bl_power 0, dpms On; usb0 169.254.0.1/16 (PB_LINK=usb tools/pbssh.sh works; if
it times out check the cable, the host VPN (trap 341) and ufw (trap 342)
first).  iw is NOT installed on the Nura root.  Firmware on hd3:
sgx540_ukernel.bin = kick18ccb (d4087521), preserved .kick17ccbtrace
(79bffc34, invalid -- do not cite), .kick16tail (744b072d) and .working = kick10
(3fbbd11b); module powervr_sgx.ko = 4a928e15, .pre-idxfifo (1a2949ab),
.pre-render = .working (10c6f435).  Tools on the ext4: /tmp/powervr_sgx_frame
(1177fafd), /tmp/powervr_sgx_frame.loc (68bc15af, production default + the
SGXGEN_* knobs), .relpds (ebf463f3), .vend (a0d48593), .bisect (b3ee9342),
/tmp/pb-emitn (411088d8), /tmp/pb-emitn-novcb (18d2c85c).  The clone's
tools/gen/usse_progs.h is PLAIN (production).  Full recipe: HANDOFF section 3.

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
operator decides when the session stops; then record the bench state, write the
session note, update the canonical docs, verify with tools/docs_audit.py and
tools/tests/test_offline.py, commit privately and publish the snapshot
(docs/project/REPO-MANAGEMENT.md).

Read the document files in full, to be thorough and not miss anything
important.
```
