# Handoff — session 82 (absolute) → session 83

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 83 = Linux-era 77**, written at the
session-82 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 82 RESULT: the render wall is the MTE state EMIT, and the CLIENT
> STREAM IS NOW PROVEN — so the wall is the SGX540 µKERNEL.**  The DDK reference
> builder gave one structural divergence not previously varied (the VDM stream's
> PDS addresses are `PDSExecBase`-relative; ours absolute) — emitting it changed
> the stream byte-for-byte and moved **nothing** (trap 427).  A one-variable
> bisection then pinned the wall to **`MTE_EMIT` (b17)**: clearing it wakes the
> TE (`CLKGATESTATUS 0x001800f8`), `SD`/`USEPIPE` do not (trap 426).  The DDK's
> own 540 capture (`43040e00 0e022403`) is byte-identical to our clear record,
> our `emit.t1` equals the vendor's `BuildEMITMTE` (`f3_20` = `EMIT_FREEP`), the
> kernel builds the same TA command, and the upstream frame tool renders on
> CE5310 — the client is correct.  Excluded by measurement: the state-copy
> `DOUTD`, the USE pipe (0/1/all), the service counters.  Two real config bugs
> fixed to the vendor's value and **neither moves it**: `USE_CTRL` (0x0a00 =
> `0x7c000`, was unset) and `DMS_CTRL` EDM 63→32 (BRN-23632 is SGX530-only, trap
> 401 corrected — trap 428).  New traps 426–429; report 283 §7/§8;
> `docs/sessions/session-82.md`.

> **SESSION 81 RESULT (context): `LASTKICK` and `IDXFIFO` are excluded, and the
> VDM stall is localized to the STATE_LOAD records.**  Traps 419/420/422–425.

> **SESSIONS 78–80 (context): the dispatch pipeline is alive (USE_PIPELINE_0);
> the first render job is consumed end to end; the stream's record layout and the
> `TA_TE` tail are walked and excluded.**  Reports 281/282; traps 413–417.

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
* **`LASTKICK` is excluded** (session 81): the µkernel sees it on **kick 2**,
  unreachable while kick 1 stalls.  **Do not add the `ta_finished` branch.**
* **The VDM stall is in the STATE_LOAD records** (session 81, trap 425); the
  draw/index path is not the wall.
* **The record's fields are excluded** (session 81): `USEPIPE`, `PARTITIONS`,
  `PDSDMADEPENDENCY`, the segment size, `EMITMTE src1`, the 545-only
  `emit.t3`/`op0xd6` pair.  (Correction: the vendor's `USEPIPE` is
  `(USEPIPE_1+pipe)` = `0x2000|pipe`, which our production matched.)
* **The VDM stream's PDS addresses are `PDSExecBase`-relative** (session 82, trap
  427) — real divergence, **measured not the wall**; production stays absolute
  (`SGXGEN_REL_PDS` opts in).
* **`MTE_EMIT` (b17) is the wall** (session 82, trap 426): clearing it wakes the
  TE; the VDM waits for the MTE state emit from the state-copy task.
* **The client stream is PROVEN against the DDK 540 capture** (session 82): the
  clear record `43040e00 0e022403` is byte-identical to ours, our `emit.t1`
  (`a0200600 fb2f4000`) equals the vendor's `BuildEMITMTE`, `f3_20` =
  `EMIT_FREEP`, the kernel's TA command is the same, and the upstream frame tool
  renders on CE5310.  **So the wall is µkernel-side.**
* **Excluded by measurement (session 82):** the state-copy `DOUTD`
  (`SGXGEN_STATE_NO_DOUTD` still stalls), the USE pipe (`USEPIPE` 0/1/all),
  `IDXFIFO` (trap 419), the launch stub, `CACHE_CTRL`'s PARTDM (trap 411).  The
  per-pipe `USE0/1_SERV_VERTEX` counters read empty even under `NO_STATE`, so
  they do **not** track VDM TA tasks (not evidence either way).
* **Two config bugs fixed to the vendor's value, neither moving the stall**
  (session 82, trap 428): `EUR_CR_USE_CTRL` (0x0a00) = `0x7c000`; `DMS_CTRL` EDM
  = 32 (`0x00208200`).
* **The desktop is llvmpipe** (DECISIONS §119); the closed pvr stack is blocked
  at modprobe.
* **A render can (intermittently) storm IRQ 147 and wedge the DMM/TILER**
  (traps 423/424) — observed on some boots, not others.

## 0b. THE ONE EXPERIMENT FOR SESSION 83 — the µkernel's TA/USE dispatch

**Why.**  The client stream, the kernel command and every client-side encoding
are proven against the DDK; `MTE_EMIT` (b17) is the wait; the state-copy USSE
task never emits.  The wall is in our SGX540 µkernel.  Three concrete,
untested µkernel-side differences remain, in cost order (do 1 and 3 first — they
are cheapest and most likely).

1. **The frame tool's two-kick layout (client-only, no reboot).**  The working
   CE5310 form is two kicks: `[the scene's VDM stream]` then `[the terminate
   words]` (`docs/log/entries/2026-11-03-0210-drm-first-frame.md`, "Why two
   kicks").  Our `sgx_flush`/`sgx_render_job` (`tools/gen/sgxgen.c`) call
   `vdm_end(c)` **twice**, so kick 0 = scene **+ a full terminate** and kick 1 =
   another terminate.  Change it to **one** `vdm_end` (kick 0 = scene,
   `k0 = vdm_start`; kick 1 = the terminate, `k1 = vdm_end()`), rebuild the frame
   tool, one render.  If `CLKGATESTATUS` b3 (TE) wakes or pixels appear, the
   extra terminate in a non-last kick was suppressing the scene.
2. **A firmware trace of the `do_ta` `cmd+0x14`/`a74` path (trap 429).**  The
   µkernel RMWs `0xa74` b27-31 with `cmd+0x14` (`ta_ctl[0] = 0x50000000`, built
   by the kernel at `pvrsgx_render.c:880/642`), but `DMS_CTRL` reads `0x00208200`
   idle and mid-hang — no TA field.  Add a `UK_*`-gated store of `cmd+0x14` (and
   the post-store `0xa74`) to **free** ctlblk words (trap 403), rebuild, read
   after a render.  A zero `cmd+0x14` read means the µkernel reads the wrong
   command word; a non-zero read that does not reach `0xa74` means the write is
   masked.  (On the 540 `0xa74` is `DMS_CTRL`, not a 545 TA-flag register, so
   the RMW may not belong at all — but its doing nothing is a signal.)
3. **Implement `FIX_HW_BRN_33181` (`IDLETA`/`RESUME_TA`) for the 540 (one
   firmware build).**  This is one of the three rev-120 "workaround in uKernel"
   errata (with 23055 and 31474 — 31474 is done, 23055 is the SPM/MTE page
   deadlock); `usedefs.h:687` defines `IDLETA()`/`RESUME_TA()` to **idle and
   resume the TA USE core** (`USE_IDLECORE_TA_REQ`, `PVRSRV_SGXUTILS_CALL(IdleCore)`).
   Our µkernel does not implement it.  Read where `ta.asm` uses `IDLETA`/
   `RESUME_TA` (the TA handler's enter/exit), port it to `ukernel/cores/sgx540.py`
   + `ukernel.py` gated on the 540, build, one boot.

**Inputs (all present).**

* The DDK drop at `/home/psyden/Pdrive/GFX_Linux_DDK-master` (GPL-2; DECISIONS
  §118).  `services4/srvinit/devices/sgx/usedefs.h:687` (the 33181 macros),
  `ta.asm` (where `IDLETA`/`RESUME_TA` are used), `sgx_utils.asm`
  (`IdleCore`/`Resume`), `sgxinit.c:4124` (`USE_CTRL`), `sgxerrata.h` (the
  SGX530 vs SGX540 blocks).
* The upstream's own bring-up log `docs/log/entries/2026-11-03-{0700,0925}-*.md`
  (DDK renders, their open µkernel stalled, fixed by dir-list routing + `a74`)
  and `…-0210-drm-first-frame.md` (the two-kick form).
* `docs/STATE.md` §2 — the DDK's own 540 capture words (the client reference).
* The frame tool: `tools/gen/sgxgen.c` (`sgx_flush`, `sgx_render_job`,
  `state_block`, `vdm_end`) and `tools/gen/mkusse.py`.

**What each result means.**  Two-kick fix wakes the TE / renders ⇒ the layout was
the wall; keep it.  `cmd+0x14` reads 0 ⇒ the µkernel command read is broken (fix
`SC_CMD`/the offset); it reads `0x50000000` but `0xa74` stays `0x00208200` ⇒ the
write is masked or overwritten (chase the write).  `IDLETA`/`RESUME_TA` wakes the
TE ⇒ BRN 33181 was needed.  If all three stall, instrument the state-emit USSE
task directly (a USSE store to client memory; `docs/ISA.md`) to learn whether it
runs.

**Do NOT re-run:** the excluded record fields (traps 416/425), the PDS address
(trap 427), the state-copy `DOUTD`, the USE pipe (0/1/all), `USE_CTRL`/EDM-32
(all measured, none moves it — traps 426/428), `LASTKICK` (trap 420), `IDXFIFO`
(trap 419), the TA register tables, the doorbell read-back (trap 407),
`CACHE_CTRL`'s PARTDM (trap 411), the vendor-image load (a driver port — §5), or
Mesa work.

**SETTLED (trap 417): do not build an argument on `EVENT_STATUS2`.**  What
supports "the VDM runs": `CLKGATESTATUS`, the VDM/TE register block,
`VDM_CTRL_STREAM_BASE`.

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

## 1. Bench state at the session-82 close (read on the unit)

| item | value |
|---|---|
| OS | **Linux `7.3.3`**, `Nura edge` — not TabletOS.  **No restore required** |
| region 0 | **`0x02C1DDC2`** — no flash.  sha256 `70474c01…`, magic `0xD7C82D1F`, 10 450 520 B |
| region 1 | sha256 **`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`** (live) |
| hd3 | the session-71 rebake **`5d3eac8f…`** at `0x4915`, 62.5 G, `/dev/loop0` ext4 rw (957.6 M used) |
| `BOOTCTR_FCTRY` | **`0x2a`** (`pbrpmb: topup: … = 2a000000`, above threshold, no write) |
| CPU | `nproc` 2; PID 1 affinity `0,1` |
| GPU | `card1`/`renderD129` = `powervr_sgx`; `card0`/`renderD128` = `omapdrm`.  Close: `resets 1`, `BIF_FAULT 0` |
| firmware | `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`kick18ccb`** (`d4087521…`; rebuilt byte-identical from source).  Preserved `.kick17ccbtrace` `79bffc34…` (invalid), `.kick16tail` `744b072d…`, …, `.working` = kick10 `3fbbd11b…` |
| module | **`92e14ad4…`** (EDM-32 + `USE_CTRL` + service counters + status).  Backups `.pre-edm32` `6ca3b4bf…`, `.pre-usectrl` `f54af284…`, `.pre-serv` `4a928e15…` (session-81 module), `.pre-render` = `.working` `10c6f435…` |
| desktop | **llvmpipe** (DECISIONS §119).  Do **not** conclude "on the SGX" from `map_files` |
| panel | LVDS `card0-LVDS-1`, `bl_power` 0, dpms On; gesture handle 0 height (temporary, DECISIONS §114) |
| WLAN | **`wlan0` DOWN** (the GPU resets took it down; re-associates slowly).  `usb0` `169.254.0.1/16` UP, SSH works.  **`iw` is not installed** |
| device-side tools | `/tmp/powervr_sgx_frame` `1177fafd…` (baseline), **`.loc` `68bc15af…`** (production default + `SGXGEN_STATE_NO_MTEEMIT/NO_SD/PIPE_ALL/PIPE0/NO_DOUTD` and `SGXGEN_REL_PDS`), `.relpds` `ebf463f3…`, `.vend` `a0d48593…` (**wrong form — do not cite**), `.doutd` `d285cd17…`, `.pipe` `c7ef9997…`, `.v540` `ef49eb03…`; `/tmp/powervr_sgx_test` `16545fd5…`.  `/tmp` is on the ext4 (trap 354) |
| host | clone `local/work/powervr_sgx` (upstream `95f78ca` + uncommitted patches; DECISIONS §110).  Session 82 touched `drm/powervr_sgx/{pvrsgx_core.c,pvrsgx_fw.c,pvrsgx_dump.c}` (USE_CTRL, EDM 32, service counters/status) and `tools/gen/{sgxgen.c,mkusse.py}` — **none in a patch**.  `tools/gen/usse_progs.h` is **plain** (production).  Trees `local/kernel-tree-sgx` (compiles) and `local/kernel-tree`; DDK 1.8 at `/home/psyden/Pdrive/GFX_Linux_DDK-master` |

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
* **Firmware builds**: explicit output paths; `build.py`'s output is raw and must
  be wrapped by `mkukfw --manifest`; the SAME `UK_*` flags for `test_emu.py`.
  **A module change costs a reboot**; never unbind pvrsrvkm live (trap 383).
  **GPU resets take `wlan0` down.**
* **Reboot before measuring anything if the boot's `open microkernel` line has
  wrapped out of dmesg** (trap 390) — or confirm the firmware file's md5.
* **A render can (intermittently) wedge the DMM and storm IRQ 147** (traps
  423/424): if it happens the panel is not a witness and a post-storm timeout is
  not pure GPU evidence — reboot, or read the GPU's registers mid-hang.
* **The client is proven (session 82)** — match the DDK capture/encoder before
  touching the µkernel; the wall is there.
* **Do not trust the SGX545's init-register offsets for the 540** — the 545's
  `0x818/0x810/0x814/0x4e8/0x800/0xaac/0xa78/0xa7c/0xa80` are not named in the
  540 header; only `USE_CTRL` (0x0a00) was a real gap (trap 428).

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
  sessions 79–82**; read "SESSION 82" and "SESSION 82 CONTINUATION" before
  quoting anything.
* `docs/reports/283-*.md` — the session-82 report (§7/§8 are the continuation:
  the client proof, the two fixes, the `a74` anomaly).
* `docs/sessions/session-82.md` — the note (dead ends included).
* `docs/STATE.md` §2 — the DDK's own 540 capture words (the client reference).
* `docs/reference/{sgx540-factsheet,sgx540-ddk18-facts}.md` — the C/K facts.
* **The DDK drop**: `services4/srvinit/devices/sgx/{usedefs.h,ta.asm,
  sgx_utils.asm,sgxinit.c}`, `hwdefs/{usecodegen.h,sgxerrata.h,sgx540defs.h}`,
  `codegen/{usegen,pds}`, `unittests/sgx_render_flip_test/`.
* `local/work/powervr_sgx/{ukernel,docs/log/entries/}` — the µkernel build and
  the upstream's own bring-up logs.

## 5. What could not be verified

* **Why the state-copy USSE task never emits to the MTE** — §0b's subject.  All
  client-side and config causes are excluded.
* **Why `do_ta`'s `cmd+0x14` → `0xa74` RMW does not land** (trap 429).
* **Whether `FIX_HW_BRN_33181` (`IDLETA`/`RESUME_TA`) matters** — not implemented.
* **Whether the frame tool's two-kick layout matters** — not changed.
* **The intermittent DMM wedge / IRQ-147 storm** — measured on some boots, not
  others; the trigger is not identified.
* **`TE_PSG` (0x218) reads `0x01400000`** though the vendor 540 µkernel only ORs
  those on the terminate/context-switch paths; a write/read-back would settle it.
* **The owed patch-series export** for the session-77…82 driver and µkernel
  changes — **nothing is in a patch** (DECISIONS §110), and this session added
  driver changes (`pvrsgx_core.c`, `pvrsgx_fw.c`, `pvrsgx_dump.c`).

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 83 (absolute; Linux-era 77)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a (a4e), KNOWN-ISSUES.md §2 (traps 416-429) and §3
(3.36), DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 112-122, docs/reports/INDEX.md,
report 283 AND 282, docs/reference/sgx540-factsheet.md AND
docs/reference/sgx540-ddk18-facts.md,
docs/captures/sgx540-first-render-2026-11-27.txt (ALL session-81 and session-82
sections incl. CONTINUATION), docs/contradictions/sgx540-cache-ctrl-breaks-dispatch.md,
docs/sessions/session-82.md, session-81.md and session-80.md.

WHERE IT STANDS: Linux 7.3.3 on hd3 (Nura).  region 0 0x02C1DDC2 (no flash),
region 1 20a5edd3..., hd3 5d3eac8f grown to 62.5 G, BOOTCTR_FCTRY 0x2a.
card1/renderD129 = powervr_sgx running the open microkernel kick18ccb
(d4087521); module 92e14ad4 (EDM 32 + USE_CTRL + service counters).  wlan0 DOWN
(GPU resets), usb0 up, desktop LLVMPIPE -- measured.  Do NOT conclude "on the
SGX" from /proc/PID/map_files -- ask GL (DECISIONS 119).

THE RENDER STALL: the VDM waits for the MTE state EMIT (MTE_EMIT b17), and the
state-copy USSE task never emits.  THE CLIENT IS PROVEN: the DDK's own 540
capture (43040e00 0e022403) is byte-identical to our clear record, our emit.t1
equals the vendor's BuildEMITMTE (f3_20 = EMIT_FREEP), the kernel builds the same
TA command, and the upstream frame tool renders on CE5310.  So the wall is the
SGX540 uKERNEL.  Excluded BY MEASUREMENT: the record fields (traps 416/425), the
PDS address encoding (trap 427), the state-copy DOUTD, the USE pipe (0/1/all),
USE_CTRL and EDM-32 (traps 426/428), LASTKICK (420), IDXFIFO (419).

THE ONE EXPERIMENT (HANDOFF section 0b), cheapest first: (1) the frame tool's
TWO-KICK layout -- our sgx_flush calls vdm_end TWICE so kick 0 carries a full
terminate; the CE5310 working form is [scene]+[terminate]; make it one vdm_end,
rebuild the tool, one render; (2) a firmware trace of do_ta's cmd+0x14 -> a74
(DMS_CTRL never gets 0x50000000; trap 429) -- is cmd+0x14 read 0, or is the write
masked?; (3) implement FIX_HW_BRN_33181 (IDLETA/RESUME_TA, idle the TA USE core;
usedefs.h:687) for the 540.  If all stall, instrument the state-emit USSE task
with a USSE store to client memory.  Do NOT re-run the excluded variants.

READ THE DDK DROP, IT IS ALL EVIDENCE (DECISIONS 118).  DECISIONS 115 stands: the
microkernel we RUN is ours; the vendor binary is evidence, never a deployment
target.

THE METHOD THAT WORKED: read the driver's status node MID-HANG (background the
tool, sample at ~0.5/1.4 s) and follow CLKGATESTATUS/DMS_CTRL/USE_CTRL; advance
one VARIABLE per boot; match the DDK capture/encoder before probing the uKERNEL.

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
are NOT the 540's (428).

BENCH: region 0 0x02C1DDC2; region 1 20a5edd3...; hd3 live /dev/loop0 ext4 rw
56.5 G; BOOTCTR 0x2a; nproc 2, PID 1 affinity 0,1; battery Full; panel
bl_power 0, dpms On; usb0 169.254.0.1/16 (PB_LINK=usb tools/pbssh.sh works; if
it times out check the cable, the host VPN (trap 341) and ufw (trap 342) first).
iw is NOT installed.  Firmware kick18ccb (d4087521), .working = kick10
(3fbbd11b); module 92e14ad4, backups .pre-edm32 (6ca3b4bf), .pre-usectrl
(f54af284), .pre-serv (4a928e15).  Tools: /tmp/powervr_sgx_frame (1177fafd),
.loc (68bc15af, production default + SGXGEN_* knobs), .doutd (d285cd17), .pipe
(c7ef9997), .relpds (ebf463f3), .vend (a0d48593, wrong form -- do not cite).
The clone's tools/gen/usse_progs.h is PLAIN.  Full recipe: HANDOFF section 3.

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
