# Handoff — session 83 (absolute) → session 84

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 84 = Linux-era 78**, written at the
session-83 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 83 RESULT (2): a REAL 545-shaped port bug was found in the vendor
> source and fixed, and the render's failure MOVED.**  `EUR_CR_DMS_CTRL`'s
> `MAX_NUM_VERTEX_PARTITIONS` is per-core — shift 27 / 5 bits on the SGX545,
> shift **25** / **3** bits on the SGX540 — and the port sent `10 << 27`
> (`0x50000000`) for every core, i.e. **zero vertex partitions** here.  The pixel
> half was missing too (render details +0x6c, which the µkernel ORs in at the
> 3D/SPM, was inside a private pad).  Both are now per-core fields (SGX540
> `4<<25`/`6<<22`; SGX545 bytes unchanged).  **The failure changed from a silent
> 2 s `job timed out` to, 3/3, `job page fault at 0x00401000 (BIF status
> 0x0000e000, requestor 0x2000, write)`** — a WRITE at 4 MiB + 4 KiB, the shape
> of a relative address used absolutely, in no mapped heap.  The frame still
> fails.  Trap 431; DECISIONS §124; capture "SESSION 83 CONTINUATION";
> module `21833840…` (revert `.pre-dms540` `92e14ad4…`).

> **SESSION 83 RESULT (1): the GPU-side marker was built and submitted, and it is
> NOT VALIDATED — its zeros are not evidence.  The render wall is therefore
> UNCHANGED (session 82's MTE state emit).**  Four marker programs, one
> `H_GENERAL` BO, a `limm` patched at load, cache clean+invalidate on the readback,
> all behind `SGXGEN_STATE_MARK`.  **Every marker read 0, and so did all four
> controls** (state-emit task, the clear's vertex task, the pixel-event ptoff
> task, a `sa0`-base store in the vertex-secondary task), with and without
> `NO_STATE`.  The measured reason: the **vertex pipe services nothing** on this
> stack (`USE0/1_SERV_VERTEX`, `USE0_SERV_PIXEL` EMPTY at idle/mid-hang/after),
> the state-copy task runs on the vertex heap, and the only pipe that services
> tasks (`USE0_SERV_EVENT`) is the **microkernel's** event handler — the client
> pixel-event program runs per 3D tile, which never starts.  **So no client task
> class can positively control the marker in a stalling frame.**  Five instrument
> defects found and fixed (`st.a.d` writes base+4; the `limm` patch must be
> read-modify-write or it drops `skipinv`; the shmem BO is cacheable; the marker
> must follow the `phas`; a `r`-using program needs a nonzero DOUTU temp count).
> Trap 430; `docs/sessions/session-83.md`.

> **SESSION 82 RESULT (context): the render wall is the MTE state EMIT, and the
> CLIENT STREAM IS PROVEN — so the wall is the SGX540 µKERNEL.**  `MTE_EMIT`
> (b17) is what the VDM waits on; the state-copy USSE task never delivers the
> emit.  The DDK's own 540 capture is byte-identical to our clear record, our
> `emit.t1` equals `BuildEMITMTE`, the kernel builds the same TA command, and the
> upstream frame tool renders on CE5310.  Excluded by measurement: record fields
> (416/425), PDS address encoding (427), state-copy `DOUTD`, USE pipe (0/1/all),
> `USE_CTRL`, EDM-32 (426/428), `LASTKICK` (420), `IDXFIFO` (419).

> **SESSIONS 78–81 (context): the dispatch pipeline is alive; the first render job
> is consumed end to end; the stream's record layout and the `TA_TE` tail are
> walked and excluded; the VDM stall is in the STATE_LOAD records.**

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**,
with a GPU-accelerated desktop (compositor **and** clients), and a modern
browser.  The long-term graphics route is the open `powervr_sgx` stack
(DECISIONS §107/§108) — this is that port, driving toward a rendered frame.

## 0a. What sessions 78–83 established (do not re-derive)

* **The dispatch pipeline is alive and jobs complete** (session 78):
  `EVENT_PDS_INFO` 0x0a70 = `0x00013c01` (`USE_PIPELINE_0`).  Keep it.
* **The µkernel's host-kick path is exhausted as a suspect** (session 79).
* **`LASTKICK` is excluded** (session 81, trap 420): it sits downstream of the
  stall; **do not** add a `ta_finished` branch.
* **The VDM stall is in the STATE_LOAD records** (session 81, trap 425).
* **The record's fields are excluded** (traps 416/425), and so is the
  `PDSExecBase`-relative address form (trap 427, measured not the wall).
* **`MTE_EMIT` (b17) is the wall** (session 82, trap 426): clearing it wakes the
  TE; the state-copy task never delivers the emit.
* **The client stream is PROVEN against the DDK 540 capture** (session 82).
* **Two config bugs fixed, neither moves the stall** (trap 428): `USE_CTRL`
  `0x7c000`; `DMS_CTRL` EDM 32 (`0x00208200`).
* **The GPU-side marker is BUILT but UNVALIDATED** (session 83, trap 430): on
  this stack every client task class that could positively control it is
  downstream of the same stall, so its zeros say nothing.  **The session-82
  question — does the state-copy USSE task RUN? — is still open.**
* **The desktop is llvmpipe** (DECISIONS §119); the closed pvr stack is blocked
  at modprobe.
* **A render can (intermittently) storm IRQ 147 and wedge the DMM/TILER**
  (traps 423/424).

## 0b. Where the next session picks up — one experiment: bound the faulting writer

**State.**  The DMS_CTRL fix (trap 431) is in and on the wire; the render's
failure is now a **page fault** that is proven to be a *side effect*: mapping the
general-heap range at `0x400000` removes it and the job reverts to the session-82
timeout.  The kernel-module source (`eurasia_km/`) was found this session and
three leads were closed with it.  The render wall is unchanged.

**THE EXPERIMENT (one boot, no rebuild — this is the whole next step).**
Bound the writer's high-water mark with the existing tool knob, on a **fresh
boot** (`resets` must be 0; a degraded GPU gives false readings — see trap
423/424):

```bash
# on the device, after a clean boot:
for sz in 0x001000 0x010000 0x040000 0x100000 0x400000; do
  B=$(dmesg | wc -l)
  timeout 20 env SGXGEN_MAP_AT=0x400000 SGXGEN_MAP_SIZE=$sz \
        /tmp/frame-mapto -n 1 >/dev/null 2>&1
  echo "== size $sz"; dmesg | tail -n +$((B+1)) | grep -E 'page fault|timed out' | tail -1
done
```

Inputs: tool `/tmp/frame-mapto` `775889e7…` (`SGXGEN_MAP_AT`/`SGXGEN_MAP_SIZE`,
general heap only), firmware `kick18ccb` `d4087521…`, module `96368b07…`
(it prints the fault's requestor, direction, `DIR_LIST_BASE0` vs kernel PD,
`*_REQ_BASE` and `MEM_REQ_STAT`).

**What each result means.**
* **The fault disappears at some size S and the job reports `job timed out`** —
  S is the buffer the writer touches.  Compare S with the client-side sizes we
  hand the GPU (`pages`, `tail_pointers_size`, `mt_bytes`, `MTRegionArraySize`,
  region sizes, strides in `TA_PB`/`TE`/`D3_RD`) and the matching field is the
  defect.  A multiple of 4 KiB points at a page count used as an address.
* **The fault keeps moving past every size** — the writer walks the whole heap;
  then it is a stride/base computation, and the next step is to log the `*_REQ_BASE`
  at the fault for each step (the module already prints them) and see which one
  tracks the fault address.
* **The fault stays at `0x00401000` regardless of size** — the mapping did not
  take (check the tool's `-v` heap line shows `0x00400000` for the enlarged BO),
  and the run is invalid rather than informative.

Then, and only then: name requestor bit 13 vs bit 1 (the drop gives the mask
`0x3FFF` with no per-bit names; a per-bit test or a block-by-block disable is the
only way), and decide with the operator whether an out-of-allocation write is in
scope before the render wall.

**Do NOT re-open** (settled this session): `TA_CTL`'s provenance (trap 431),
`TE_STATE = 0`, `TE_PSG` b24, the `0x618` page-table base, the `0x234` OTPM base
(written up in the capture).  **Do NOT** read a marker zero as evidence (trap
430) — the marker is still unvalidated.

**Still open, unchanged:** `FIX_HW_BRN_33181` (`IDLETA`/`RESUME_TA`,
`usedefs.h:687`) unimplemented; the frame tool's two-kick layout; the owed
patch-series export (§110).

**Reference inputs (all present).**
* The DDK drop at `/home/psyden/Pdrive/GFX_Linux_DDK-master` (GPL-2; DECISIONS
  §118).  **Both trees:** `eurasia/` (client + microkernel asm + hwdefs) and
  **`eurasia_km/services4/srvkm/devices/sgx/`** (the kernel module:
  `sgxkick.c`, `sgxutils.c`, `sgxinit.c`, `mmu.c`, `sgxreset.c`).
* `local/work/powervr_sgx/docs/log/entries/` — the upstream's own bring-up logs.
* `docs/STATE.md` §2 — the DDK's own 540 capture words (the client reference).
* The frame tool: `tools/gen/sgxgen.c`, `tools/gen/mkusse.py`,
  `drm/tests/powervr_sgx_frame.c`.

**Do NOT re-run:** the excluded record fields (traps 416/425), the PDS address
(trap 427), the state-copy `DOUTD`, the USE pipe (0/1/all), `USE_CTRL`/EDM-32,
`LASTKICK` (420), `IDXFIFO` (419), the TA register tables, the doorbell read-back
(trap 407), `CACHE_CTRL`'s PARTDM (trap 411), the vendor-image load (§5), Mesa
work, **and do not add more marker variants without a positive control** (trap
430: four controls already all read zero).

**SETTLED (trap 417): do not build an argument on `EVENT_STATUS2`.**

## 0c. The media, and the way back

* **region 0 = `0x02C1DDC2`** — `boot/chainloader/kernel.img`, 10 450 520 B,
  sha256 `70474c01…`, magic `0xD7C82D1F`.  Keep the image FILE.
* **region 1 = `20a5edd3…`** (64 MiB at raw `0x29000000`).
* **hd3 = the session-71 rebake** `5d3eac8f…`, block `0x4915`, grown to 62.5 G.
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

## 1. Bench state at the session-83 close (read on the unit)

| item | value |
|---|---|
| OS | **Linux `7.3.3`**, `Nura edge` — not TabletOS.  **No restore required** |
| region 0 | **`0x02C1DDC2`** — no flash.  sha256 `70474c01…`, magic `0xD7C82D1F`, 10 450 520 B |
| region 1 | sha256 **`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`** (live) |
| hd3 | the session-71 rebake **`5d3eac8f…`** at `0x4915`, 62.5 G, `/dev/loop0` ext4 rw |
| `BOOTCTR_FCTRY` | **`0x22`** (`pbrpmb: topup: … = 22000000`, above threshold, no write) |
| CPU | `nproc` 2; PID 1 affinity `0,1` |
| GPU | `card1`/`renderD129` = `powervr_sgx`; `card0`/`renderD128` = `omapdrm`.  Close: `resets 1`, one per render (each render faults and resets), `BIF_FAULT 0` |
| firmware | `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`kick18ccb`** (`d4087521…`, 14 760 B).  Preserved `.kick17ccbtrace` `79bffc34…` (invalid), `.kick16tail` `744b072d…`, …, `.working` = kick10 `3fbbd11b…` |
| module | **`96368b07…`** (per-core `DMS_CTRL` partitions + the fault message: requestor, direction, `DIR_LIST_BASE0` vs kernel PD, `*_REQ_BASE`, `MEM_REQ_STAT`).  Reverts: `.pre-dms540` `92e14ad4…`, `.pre-reqlog` `91729a08…`, `.pre-pdlog` `21833840…`, `.pre-reqbase` `2c191e1a…`, plus `.pre-edm32` `6ca3b4bf…`, `.pre-usectrl` `f54af284…`, `.pre-serv` `4a928e15…`, `.pre-render` = `.working` `10c6f435…` |
| desktop | **llvmpipe** (DECISIONS §119).  Do **not** conclude "on the SGX" from `map_files` |
| panel | LVDS `card0-LVDS-1`, `bl_power` 0, dpms On; gesture handle 0 height (temporary, DECISIONS §114) |
| WLAN | **`wlan0` UP** (`192.168.0.231/24`); `usb0` `169.254.0.1/16` UP, SSH works.  **`iw` is not installed** |
| device-side tools | `/tmp/powervr_sgx_frame` `1177fafd…` (baseline), **`.loc` `68bc15af…`** (production default + the `SGXGEN_*` knobs), **`.s83` `3da50fab…`** (the marker build; production default + `SGXGEN_STATE_MARK`), **`/tmp/frame-mapto` `775889e7…`** (`SGXGEN_MAP_AT`/`SGXGEN_MAP_SIZE`, general heap only), `.relpds` `ebf463f3…`, `.vend` `a0d48593…` (**wrong form — do not cite**), `.doutd` `d285cd17…`, `.pipe` `c7ef9997…`, `.v540` `ef49eb03…`; `/tmp/powervr_sgx_test` `16545fd5…`.  `/tmp` is on the ext4 (trap 354) |
| host | clone `local/work/powervr_sgx` (upstream `95f78ca` + uncommitted patches; DECISIONS §110).  Session 83 touched `tools/gen/{mkusse.py,sgxgen.c,sgxgen.h}`, `drm/tests/powervr_sgx_frame.c` — **none in a patch**, and `tools/gen/usse_progs.h` carries the four added marker programs (production programs byte-identical).  Trees `local/kernel-tree-sgx` (compiles) and `local/kernel-tree`; DDK 1.8 at `/home/psyden/Pdrive/GFX_Linux_DDK-master` |

## 2. Working rules (unchanged, plus what this session cost)

* Read `docs/project/SAFETY.md` before any write.  One variable per boot.  An
  agent never writes RPMB; NVRAM read-only.
* Kernel printk goes to the panel; `console=ttyGS0` is inert.  Use `reboot -f`.
  **Ask the operator what is on the panel**; do not screenshot.
* **Do not restart `tinydm`** (trap 353), **do not leave Kumo running or kill it**
  (trap 367) — reboot.
* **Do not call `sync`** (trap 300).  `/tmp` is on the ext4 (trap 354).
* **Register reads:** only from inside the driver (trap 384).
* **Revert experiments and prove the revert.**  Look up every external fact.
  Write the durable fact into the repository in the same turn.
* **A uAPI "pass" is only evidence once the fence is understood** (trap 388).
* **Parse every ctlblk dump BY WORD INDEX**; **one word, one instrument** (trap
  422); **a word is free only if no store anywhere touches it** (trap 403).
* **New this session (trap 430):** a GPU-side marker's zero is not evidence
  until a task of the **same class and pipe** has been seen to write.  On this
  stack **no client task class can be that control in a stalling frame** — the
  vertex pipe services nothing, and the event pipe that runs is the µkernel's.
  Instrument rules that cost a defect each: `st.a.d base,#0` writes `base+4`
  (ISA.md:1485); patch a `limm` with read-modify-write or you drop `skipinv`;
  a shmem BO is **cacheable** (`map_wc` unset) so clean+invalidate before
  reading; put the marker **after** the `phas`; a program that uses `r` needs a
  nonzero DOUTU temp count (PDS.md:301-305).
* **Firmware builds**: explicit output paths; `build.py`'s output is raw and must
  be wrapped by `mkukfw --manifest`; the SAME `UK_*` flags for `test_emu.py`.
  **A module change costs a reboot**; never unbind pvrsrvkm live (trap 383).
  **GPU resets take `wlan0` down.**
* **Reboot before measuring anything if the boot's `open microkernel` line has
  wrapped out of dmesg** (trap 390) — or confirm the firmware file's md5.
* **A render can (intermittently) wedge the DMM and storm IRQ 147** (traps
  423/424): reboot, or read the GPU's registers mid-hang.
* **The client is proven (session 82)** — match the DDK capture/encoder before
  touching the µkernel; the wall is there.
* **Do not trust the SGX545's init-register offsets for the 540** (trap 428).

## 3. The build and deploy recipe, exactly

```bash
cd local/work/powervr_sgx
ROOT=$(cd ../../.. && pwd); export PATH=$ROOT/local/toolchains/armv7-eabihf/bin:$PATH
export SGX_ASM=$PWD/../sgxasm/sgx-asm
# 1. firmware (only if the µkernel changes): explicit output paths; the SAME
#    UK_* flags for the gate, and a CCBTRACE build must NOT also set UK_WAITLOG.
UK_TICKLOG=1 UK_ENTRYFIRST=1 UK_NO_PHAS=1 UK_TIMERDIV=100 UK_WAITLOG=1 \
  UK_BUILD_ID=<id> python3 ukernel/build.py sgx540 /tmp/<id>.bin /tmp/<id>.json
SGX_CORE=sgx540 UK_TICKLOG=1 UK_ENTRYFIRST=1 UK_NO_PHAS=1 UK_TIMERDIV=100 \
  UK_WAITLOG=1 python3 ukernel/test_emu.py                        # all OK
python3 ukernel/golden/check.py                    # SGX545 golden 83b5b0a7…
python3 drm/tools/mkukfw.py /tmp/<id>.bin -o /tmp/<id>w.bin --manifest /tmp/<id>.json
python3 drm/tools/mkukfw.py --check /tmp/<id>w.bin                            # ok
# push (hd3 has NO scp/sftp-server — trap 391); preserve the old file first:
cat /tmp/<id>w.bin | ../../../tools/pbssh.sh 'cat > /lib/firmware/powervr_sgx/<name>.new; md5sum …'
# 2. module: build BOTH source copies (env.sh at the REPO root)
. $ROOT/tools/env.sh
cp drm/powervr_sgx/*.c drm/powervr_sgx/*.h $ROOT/local/kernel-tree-sgx/drivers/gpu/drm/powervr_sgx/
cd $ROOT/local/kernel-tree-sgx && make ARCH=arm CROSS_COMPILE=$CROSS_COMPILE \
  M=drivers/gpu/drm/powervr_sgx modules
#    deploy to /lib/modules/7.3.3/kernel/drivers/gpu/drm/powervr_sgx.ko, keeping
#    the old as .pre-<change>; then reboot -f (a module change costs a boot).
# 3. the frame tool (client-side; no reboot).  Plain usse_progs.h = production.
python3 tools/gen/mkusse.py > tools/gen/usse_progs.h     # needs SGX_ASM
arm-buildroot-linux-gnueabihf-gcc -static -O2 -Wall -DSGXGEN_DRM \
  -Idocs/uapi -Itools/gen \
  -I$ROOT/local/toolchains/armv7-eabihf/arm-buildroot-linux-gnueabihf/sysroot/usr/include/drm \
  -o /tmp/pb-frame drm/tests/powervr_sgx_frame.c tools/gen/sgxgen.c -lm
#    push as /tmp/powervr_sgx_frame.* and chmod +x (an upload loses the exec bit).
# 4. confirm `open microkernel <id>` in dmesg; null family FIRST; then background
#    the frame tool and sample /sys/kernel/debug/dri/129/status at ~0.5/1.4 s:
#      S=/sys/kernel/debug/dri/129/status
#      /tmp/powervr_sgx_frame.loc -n 1 >/tmp/r.txt 2>&1 & P=$!
#      sleep 0.5; grep -E 'CLKGATESTATUS|TE_STATE|DPM_PAGE_STATUS|DMS_CTRL|USE_CTRL' $S
#      sleep 0.9; grep CLKGATESTATUS $S; wait $P; grep -E 'frame 0|frames right' /tmp/r.txt
```

## 4. Useful local extracts

* `docs/captures/sgx540-first-render-2026-11-27.txt` — **the whole campaign,
  sessions 79–83**; read "SESSION 83" first (the marker result), then "SESSION
  82" and "SESSION 82 CONTINUATION", before quoting anything.
* `docs/reports/283-*.md` — the session-82 report (§7/§8 are the continuation:
  the client proof, the two fixes, the `a74` anomaly).
* `docs/sessions/session-83.md`, `session-82.md` — the notes (dead ends
  included).
* `docs/STATE.md` §2 — the DDK's own 540 capture words (the client reference).
* `docs/reference/{sgx540-factsheet,sgx540-ddk18-facts}.md` — the C/K facts.
* **The DDK drop**: `services4/srvinit/devices/sgx/{usedefs.h,ta.asm,
  sgx_utils.asm,sgxinit.c}`, `hwdefs/{usecodegen.h,sgxerrata.h,sgx540defs.h}`,
  `codegen/{usegen,pds}`, `unittests/sgx_render_flip_test/`.
* `local/work/powervr_sgx/{ukernel,docs/log/entries/}` — the µkernel build and
  the upstream's own bring-up logs (the `2026-11-03-0925` entry is the
  TA-completion fix to diff against — §0b step 1).

## 5. What could not be verified

* **Whether the state-copy USSE task runs at all** — §0b step 1.  The marker that
  would answer it is built but **unvalidated** (trap 430): on this stack every
  client task class that could positively control it is downstream of the same
  stall.
* **Why the state-copy USSE task never emits to the MTE** — §0b's subject.  All
  client-side and config causes are excluded.
* **Why `do_ta`'s `cmd+0x14` → `0xa74` RMW does not land** (trap 429).
* **Whether `FIX_HW_BRN_33181` (`IDLETA`/`RESUME_TA`) matters** — not implemented.
* **Whether the frame tool's two-kick layout matters** — not changed.
* **The intermittent DMM wedge / IRQ-147 storm** — measured on some boots, not
  others; the trigger is not identified.
* **`TE_PSG` (0x218) reads `0x01400000`** though the vendor 540 µkernel only ORs
  those on the terminate/context-switch paths; a write/read-back would settle it.
* **Why the client vertex pipe services nothing** — `USE0/1_SERV_VERTEX` EMPTY
  at idle, mid-hang and after (session 83).  Whether that counter is simply not
  a VDM-TA observable, or no vertex-pipe task ever runs, is not established; it
  is why the marker has no control.
* **The owed patch-series export** for the session-77…83 driver, µkernel and tool
  changes — **nothing is in a patch** (DECISIONS §110), and this session added
  `tools/gen/{mkusse.py,sgxgen.c,sgxgen.h}` and
  `drm/tests/powervr_sgx_frame.c`.

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 84 (absolute; Linux-era 78)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a (a4f then a4e), KNOWN-ISSUES.md §2 (traps 416-430) and
§3 (3.36), DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 112-123,
docs/reports/INDEX.md, report 283 AND 282,
docs/reference/sgx540-factsheet.md AND docs/reference/sgx540-ddk18-facts.md,
docs/captures/sgx540-first-render-2026-11-27.txt (ALL session-81, session-82 and
session-83 sections incl. CONTINUATION),
docs/contradictions/sgx540-cache-ctrl-breaks-dispatch.md,
docs/sessions/session-83.md, session-82.md, session-81.md and session-80.md.

WHERE IT STANDS: Linux 7.3.3 on hd3 (Nura).  region 0 0x02C1DDC2 (no flash),
region 1 20a5edd3..., hd3 5d3eac8f grown to 62.5 G, BOOTCTR_FCTRY 0x2a.
card1/renderD129 = powervr_sgx running the open microkernel kick18ccb
(d4087521); module 92e14ad4 (EDM 32 + USE_CTRL + service counters).  wlan0 DOWN
(GPU resets), usb0 up, desktop LLVMPIPE -- measured.  Do NOT conclude "on the
SGX" from /proc/PID/map_files -- ask GL (DECISIONS 119).

THE RENDER STALL: the VDM waits for the MTE state EMIT (MTE_EMIT b17), and the
state-copy USSE task never emits.  THE CLIENT IS PROVEN (session 82): the DDK's
own 540 capture (43040e00 0e022403) is byte-identical to our clear record, our
emit.t1 equals the vendor's BuildEMITMTE, the kernel builds the same TA command,
and the upstream frame tool renders on CE5310.  Excluded BY MEASUREMENT: the
record fields (416/425), the PDS address (427), the state-copy DOUTD, the USE
pipe (0/1/all), USE_CTRL and EDM-32 (426/428), LASTKICK (420), IDXFIFO (419).

SESSION 83, TWO RESULTS.  (1) The GPU-side marker was built and run and is NOT
VALIDATED (trap 430): every marker read 0 AND SO DID ALL FOUR CONTROLS, because
on this stack the vertex pipe services nothing (USE0/1_SERV_VERTEX EMPTY at
idle/mid-hang/after) and the only pipe that runs is the microkernel's event
handler.  Do not read a marker zero as evidence.  (2) Reading the vendor DDK 1.8
source found a REAL 545-shaped port bug and fixed it: EUR_CR_DMS_CTRL's
MAX_NUM_VERTEX_PARTITIONS is shift 27 / 5 bits on the SGX545 but shift 25 / 3
bits on the SGX540, and the port sent 10<<27 (0x50000000) for every core -- i.e.
ZERO vertex partitions here.  The pixel half was missing too (render details
+0x6c, OR'd into the same register by the uKernel at the 3D/SPM, sat in a
private pad).  Both are now per-core (SGX540 4<<25 / 6<<22; SGX545 bytes
unchanged) and the live command reads cmd+0x14/+0x18 = 08000000.

THE STALL MOVED: from a silent 2 s "job timed out" (the signature sessions
79-82 chased) to, reproducibly 3/3, "job page fault at 0x00401000 (BIF status
0x0000e000, requestor 0x2000, write)" -- a WRITE at 4 MiB + 4 KiB, the shape of
an address used relative to a base when it should be absolute, in no mapped
heap.  The frame still fails (0/1).

WORK NEXT (HANDOFF section 0b) -- ONE experiment, one boot, no rebuild: bound the
page fault's writer by mapping growing general-heap ranges at 0x400000 with
/tmp/frame-mapto (SGXGEN_MAP_AT / SGXGEN_MAP_SIZE) on a FRESH boot, and read the
fault message (module 96368b07 prints requestor, direction, DIR_LIST_BASE0 vs
kernel PD, the *_REQ_BASE registers and MEM_REQ_STAT).  A size at which the fault
stops and the job reverts to "job timed out" is the buffer the writer touches --
match it against pages / tail_pointers_size / mt_bytes / MTRegionArraySize /
region sizes / strides and the field is the defect.  The exact recipe and the
meaning of each outcome are in HANDOFF section 0b.  Then decide with the operator
whether an out-of-allocation write is in scope before the render wall.

SETTLED THIS SESSION -- do not re-open: TA_CTL's provenance (trap 431), TE_STATE=0,
TE_PSG b24, the 0x618 page-table base, the 0x234 OTPM base.  Do NOT read a marker
zero as evidence (trap 430): the instrument is still unvalidated.

READ THE DDK DROP, IT IS ALL EVIDENCE (DECISIONS 118).  DECISIONS 115 stands: the
microkernel we RUN is ours; the vendor binary is evidence, never a deployment
target.

THE METHOD THAT WORKED: read the driver's status node MID-HANG (background the
tool, sample at ~0.5/1.4 s) and follow CLKGATESTATUS/DMS_CTRL/USE_CTRL; advance
one VARIABLE per boot; match the DDK capture/encoder before probing the uKERNEL.
AND (session 83): a GPU-side instrument needs a positive control of the SAME task
class and pipe -- on this stack none exists in a stalling frame (trap 430).

TRAPS THAT STILL APPLY: a uAPI "pass" is only evidence once the fence is
understood (388); "consumed" after a reset equals "issued" (389); dmesg's "open
microkernel <build>" is the only truth about which firmware ran (390) --
/var/log/dmesg survives when the ring wraps; hd3 has no scp/sftp-server -- push
with cat FILE | tools/pbssh.sh 'cat > /path' and md5sum first (391), chmod +x
after; /dev/mem cannot reach the GPU (384); edm_init=1 is required (385); a
module change costs a reboot; never unbind pvrsrvkm live (383); GPU resets take
wlan0 down; parse every ctlblk dump BY WORD INDEX; one word, one instrument
(422); do not build an argument on EVENT_STATUS2 (417); a render can storm IRQ
147 and wedge the DMM/TILER, INTERMITTENTLY (423/424); the SGX545 init offsets
are NOT the 540's (428); a GPU marker's zero needs a same-pipe control (430).

BENCH: region 0 0x02C1DDC2; region 1 20a5edd3...; hd3 live /dev/loop0 ext4 rw;
BOOTCTR 0x22; nproc 2, PID 1 affinity 0,1; battery Full; panel
bl_power 0, dpms On; usb0 169.254.0.1/16 (PB_LINK=usb tools/pbssh.sh works; if
it times out check the cable, the host VPN (trap 341) and ufw (trap 342) first).
iw is NOT installed.  Firmware kick18ccb (d4087521), .working = kick10
(3fbbd11b); module 96368b07 (per-core DMS_CTRL + fault diagnostics), reverts
.pre-dms540 (92e14ad4), .pre-reqlog (91729a08), .pre-pdlog (21833840),
.pre-reqbase (2c191e1a).  Tools: /tmp/powervr_sgx_frame (1177fafd),
.loc (68bc15af, production default + SGXGEN_* knobs), .s83 (3da50fab, the marker
build), .doutd (d285cd17), .pipe (c7ef9997), .relpds (ebf463f3), .vend
(a0d48593, wrong form -- do not cite).  The clone's tools/gen/usse_progs.h is
production PLUS the four added marker programs.  Full recipe: HANDOFF section 3.

Kernel printk goes to the panel; use reboot -f; ask the operator for visual
checks; do not screenshot; do not restart tinydm (353) or kill Kumo (367).  An
agent never writes RPMB; NVRAM read-only.  Revert experiments and prove the
revert; look up every external fact; do not call sync (300); /tmp is on the ext4
(354).  There is NO RAM-boot path.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
Start the flasher, then reset with reboot -f; the bootrom holds ~20 s.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  WRAP-UP: only the operator
decides when the session stops; then record the bench state, write the session
note, update the canonical docs, verify with tools/docs_audit.py and
tools/tests/test_offline.py, commit privately and publish the snapshot
(docs/project/REPO-MANAGEMENT.md).

Read the document files in full, to be thorough and not miss anything
important.
```
