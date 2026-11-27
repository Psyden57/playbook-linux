# Handoff — session 79 (absolute) → session 80

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 80 = Linux-era 74**, written at the
session-79 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 79 RESULT: the first render job went through the open microkernel
> end to end, and its failure point is measured.**  The render job type is
> wired (module `1a2949ab…`: the sgx545 command tables verbatim on the
> sgx540 entry, `pds_row_bytes = 16`; job_types advertises RENDER + TRANSFER;
> the suite reads 31/50; the null family intact).  Five µkernel fixes came
> out of the campaign (traps 413/414 + the vendor write-set diff), each found
> with UK_WAITLOG's wait-site id or the vendor 540 binary's register write
> set.  On `kick15pds` NO wait spins: the µkernel consumes the kick, completes
> every invalidate and DPM/OTPM load, and kicks the TA — the TA task STARTS
> (`EVENT_STATUS2 TRIG_TA` latches at the driver's timeout) and never
> finishes; the VDM/MTE/UCACHEL2/USE0 are clocked awake, the TE never starts,
> the DPM allocates zero pages, no BIF fault.  **The stall is inside the
> VDM's control-stream execution.**  Report **282**; the campaign (7 boots)
> is in `docs/captures/sgx540-first-render-2026-11-27.txt`; DECISIONS
> **§117**; traps **413/414**.

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

## 0b. THE ONE EXPERIMENT FOR SESSION 80 — the VDM control stream

**Where it stands.**  The TA task starts and the VDM is clocked awake with a
correct-looking setup, but the TE never starts, the DPM allocates zero pages,
nothing faults, and nothing is written.  Everything the µkernel programs has
been verified against the vendor.  What has NOT been checked against the 540
is one level down: **the encodings inside the client's VDM control stream and
the client state-load programs it names** — both derived from SGX545
captures, and three separate 545-shaped bugs fell to exactly this class of
check in session 79.

**Method — the 0xb14 method on the stream, host-side first.**

1. **Dump the stream and the programs it names** (no boot): run the frame
   tool with `SGXGEN_DUMP_PROGS=1` — the VDM stream dumps (28 words at
   0x42000 on the session-79 runs); extend `dump_progs()` in
   `tools/gen/sgxgen.c` to also dump the `c->pds_vtx` ring (the state-load
   PDS programs the STATE_LOAD records point at, 0x0e801200+).  Push the
   rebuilt frame tool with `cat FILE | tools/pbssh.sh 'cat > …'` (trap 391).
2. **Check each record type against sgx540defs.h** — the STATE_LOAD record's
   PDS-address field width and shift (the 540 is 28-bit; the encodings came
   from 545 captures), the PRIM record's fields, the terminate words.  The
   540 header names the VDM registers (0x300-0x378 block:
   `VDM_CONTEXT_STORE_*`, `VDM_MTE`); the stream *record* formats are NOT in
   the header — cross-check against the vendor's client-side stream builder
   (`libsrv_um` disassembly, or the `SGXMKIF` structures' producers) and
   against the vendor µkernel's USSE EventHandler (it consumes the same
   records for its own paths).
3. **Check the client state-load PDS/USE programs against the 540's
   USSE/PDS encodings** (`sgx-asm`/`sgx-disasm -c sgx540` will refuse or
   garble instructions that the 540 does not decode — run the disassembler
   over sgxgen's assembled programs and read every instruction back).
4. **Then the µkernel tail hygiene in the same boot** (cheap, header-backed):
   prune `TA_TE`'s SGX545-only writes (0x2a8, 0x2b0/0x2b4/0x2d0/0x2d4,
   0x324, 0x364/0x368/0x370/0x374, 0x520, 0x74c — the vendor's 540 build
   writes none), and fix the bogus mapping `(0x230, cmd+0x2a4)` (rt_flags →
   `MTE_OTPM_CSM_FLUSH_BASE`; the vendor uses 0x230/0x234 as addresses for
   its OTPM CSM flush/load).  Rebuild, gate, deploy (§3), null family first.
5. **Submit ONE render and read the mid-hang state** (the extended status
   dump): if the TE starts or the DPM allocates, the stream was the wall;
   if CLKGATESTATUS changes shape, follow the newly-awake unit.

**Interpretation:**
* **The TE starts / pixels appear** → the stream encodings were the wall;
  walk the 3D next (`do_3d` has never run either — the same 545-derived
  tables) toward the frame loop.
* **The stall moves to a different unit** (ISP/TSP awake) → the stream
  parsed further; follow the new CLKGATESTATUS shape and the DPM counters.
* **No change at all** → the stream is fine and the fault is in the VDM↔USE
  state-load handshake; instrument the USE side (the state-load task is the
  only client code the TA runs before the TE).

**Do not**: touch `CACHE_CTRL`'s PARTDM fields (trap 411 + the contradiction
file), re-audit the TA register tables (0a), or start Mesa/compositor work.

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

## 1. Bench state at the session-79 close (read on the unit)

| item | value |
|---|---|
| OS | **Linux `7.3.3`**, `Nura edge` — not TabletOS.  **No restore required** |
| region 0 | **`0x02C1DDC2`** — no flash this session.  File `boot/chainloader/kernel.img`, 10 450 520 B, sha256 `70474c01…` |
| region 1 | sha256 **`20a5edd3…`** — re-hashed live at the wrap-up (`dd` 64 MiB at `0x29000000`) |
| hd3 | the session-71 rebake **`5d3eac8f…`** at `0x4915`, grown to 62.5 G; `/dev/loop0` ext4 rw |
| `BOOTCTR_FCTRY` | **`0x21`** read fresh at the wrap-up (`pbrpmb: topup: … = 21000000`, above threshold, no write).  The session's ~10 boots each spent one unit; the top-up fired during the campaign (a boot landed below 0x1e and wrote 0x32).  Self-maintaining as designed |
| CPU | `nproc` 2; PID 1 affinity **`0,1`** |
| GPU | `card1`/`renderD129` = **`powervr_sgx`** running **`open microkernel kick15pds`**; `card0`/`renderD128` = `omapdrm`.  The null family passes (640 µs; 600 jobs/120 ms; `resets 0` between render attempts); the render stall is §0b's subject |
| firmware | `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`kick15pds`** (wrapped md5 `24322c2a…`; kick10's flags + UK_WAITLOG + the four fixes).  Preserved beside it: `.kick14otpm` `f1b16422…`, `.kick13inv` `688bfb8f…`, `.kick12inv` `81d7a183…`, `.kick11wl` `e5f1db18…`, `.kick10live` `3fbbd11b…`, and **`.working` = kick10 `3fbbd11b…`** (the session-78 proven pair), plus the session-76/77 variants |
| module | `powervr_sgx.ko` = **`1a2949ab…`** (the render wiring + the extended status dump).  Preserved: `.predumpregs` `85fe59a2…` (the render wiring before the dump extension), **`.pre-render` = `.working` = `10c6f435…`** (the session-78 proven pair = the revert), and the session-76/77 variants |
| desktop | **llvmpipe** for compositor *and* clients; **not exercised this session** (the GPU work never touched tinydm/catacomb) |
| panel | LVDS `card0-LVDS-1` connected, `bl_power` **0**.  Gesture handle 0 height in `/etc/catacomb/initrc` — a **temporary guard**, DECISIONS §114 |
| WLAN | **`wlan0` UP** (re-associated between the GPU-reset boots; the resets take it down).  `usb0` `169.254.0.1/16` UP |
| SSH | `PB_LINK=usb tools/pbssh.sh` works; if it times out check the cable, the host VPN (trap 341) and ufw (trap 342) |
| device-side files | the firmware/module variants above, `/etc/catacomb/initrc`, **`/tmp/powervr_sgx_test`** and **`/tmp/powervr_sgx_frame`** (`efaefc61…`, with the VDM-stream dump) — `/tmp` is on the ext4 (trap 354), so they survive reboots but a reflash loses them |
| host | clone `local/work/powervr_sgx` (upstream `95f78ca` + our uncommitted patches — **the session-77/79 module+firmware changes are NOT yet in a patch file**, DECISIONS §110); assembler `local/work/sgxasm/{sgx-asm,sgx-disasm}`; trees `local/kernel-tree-sgx` (compiles the module) and `local/kernel-tree` (the known-good closed-DDK tree); **GPL-2 DDK 1.8 reference at `/home/psyden/Pdrive/GFX_Linux_DDK-master` (outside the repo)** — its `binary2_540_omap4430_android_release/target/intermediates/ukernel/ukernel.bin` is the vendor-binary diff source |

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

* `docs/captures/sgx540-first-render-2026-11-27.txt` — **the whole first-
  render campaign, seven boots in order**: the wiring, the first consumption,
  the four wait-site finds and fixes, the vendor write-set diff, the
  mid-hang TA/VDM/DPM state, the vendor field-for-field match, the stream
  dump.  Read it before quoting anything from session 79.
* `docs/reports/282-*.md` — the numbered report for the campaign.
* `docs/reference/sgx540-factsheet.md` and `docs/reference/
  sgx540-ddk18-facts.md` — the C/K facts, including the errata list (~50
  BRNs at rev 120; 23055/31474/33181 are "workaround in uKernel") and the
  ta3dctl layout derivation.
* `local/work/powervr_sgx/ukernel/` — the microkernel; `build.py`,
  `test_emu.py`, `golden/check.py`, `cores/{sgx540,sgx545}.py` (the register
  tables and per-core constants — every fix this session landed in
  `cores/sgx540.py`).  The vendor's `ta.asm`/`sgx_mkif_client.h` (in the DDK
  drop) are the TA-path ground truth.
* `docs/contradictions/sgx540-cache-ctrl-breaks-dispatch.md` — the
  unresolved CACHE_CTRL tension (trap 411).  Do not write 0x0804's PARTDM
  fields.
* Reports 282 (session 79), 281 (session 78), 280 (session 77).

## 5. What could not be verified

* **Why the VDM stalls** — the session's central open question (§0b).
* **`CACHE_CTRL`'s PARTDM semantics and reset value** — unchanged (the
  contradiction file names the tests that would settle it).
* **The µkernel's per-kick `DMS_CTRL` vertex-partition update** (the vendor
  does it; ours relies on the reset value) — irrelevant at 256x256, untested
  with more geometry.
* **Whether the 545-only registers our TA_TE tail writes (0x2a8, 0x2b0-0x2d4,
  0x324, 0x364-0x374, 0x520, 0x74c) are truly absent on the 540 or
  implemented-but-unnamed** — would be settled by a per-bit write/read-back
  sweep on a working system (trap 397's method).
* **The panel**: not confirmed visually by the operator this session;
  `bl_power` 0, the desktop untouched.
* Unchanged: `0xb14`'s write (trap 387), `sgx540_init_regs`, the microkernel
  register tables' 3D half (`D3_RD` — never executed), the owed patch-series
  export for all the session-77/78/79 driver+firmware changes — nothing is
  upstream (DECISIONS §110).

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 80 (absolute; Linux-era 74)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a, KNOWN-ISSUES.md §2 (traps 398-414) and §3,
DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 112-117, docs/reports/INDEX.md,
report 282 (this session's predecessor) and 281,
docs/reference/sgx540-factsheet.md AND docs/reference/sgx540-ddk18-facts.md,
docs/captures/sgx540-first-render-2026-11-27.txt,
docs/contradictions/sgx540-cache-ctrl-breaks-dispatch.md,
docs/sessions/session-79.md.

WHERE IT STANDS: Linux 7.3.3 on hd3 (Nura).  region 0 0x02C1DDC2 (keep the
image FILE), region 1 20a5edd3..., hd3 = 5d3eac8f grown to 62.5 G;
BOOTCTR_FCTRY 0x21 at the wrap-up (the Linux /init tops up below 0x1e);
card1/renderD129 = powervr_sgx running the open microkernel kick15pds
(24322c2a).  THE FIRST RENDER JOB WENT THROUGH THE MICROKERNEL END TO END:
the render job type is wired (job_types advertises RENDER+TRANSFER, suite
31/50, the null family intact at 640us / 600 jobs in 120ms), five SGX545-
shaped microkernel bugs were fixed (traps 413/414: the 0x138 invalidate-done
masks 0x03/0x05 per request register; the OTPM OP bit 3; the STATUS2 b12
wait; the DPM PDS-page threshold 0x614; no free-list mode selector), and the
failure point is measured: the microkernel consumes the kick, completes every
invalidate/load, kicks the TA -- the TA task STARTS (EVENT_STATUS2 TRIG_TA
latches at the driver's 2s timeout) and never finishes.  VDM/MTE/UCACHEL2/
USE0 clocked awake, the TE never starts, the DPM allocates zero pages, no BIF
fault, and every programmed register verified sane mid-hang (the vendor's
SGXMKIF_TAREGISTERS block matches ours field-for-field).  THE STALL IS
INSIDE THE VDM'S CONTROL-STREAM EXECUTION.  wlan0 UP (re-associated between
GPU-reset boots); usb0 up; desktop llvmpipe (untouched).

THE METHOD THAT WORKED: (1) UK_WAITLOG names the stuck wait site at ctlblk
word 0x35 (the build manifest maps ids to labels); (2) the driver's status
node now dumps the TA/VDM/DPM registers -- read it MID-HANG (background the
frame tool, sleep ~1s, cat status) and follow CLKGATESTATUS's awake units;
(3) disassemble the vendor 540 binary
(/home/psyden/Pdrive/GFX_Linux_DDK-master/eurasia/eurasiacon/
binary2_540_omap4430_android_release/target/intermediates/ukernel/ukernel.bin)
with sgx-disasm -c sgx540 and decode every str #imm site -- it settles
register/mask/value questions for this silicon without a boot; (4) the
vendor's ta.asm / sgx_mkif_client.h are the TA-path ground truth (GPL-2).

THE ONE EXPERIMENT (HANDOFF-79 section 0b, continued): the 0xb14 method on
the VDM CONTROL STREAM.  Everything the microkernel programs is verified;
what has never been checked against the 540 is one level down -- the stream
record encodings and the client state-load programs, both derived from 545
captures.  Dump the stream (SGXGEN_DUMP_PROGS=1 /tmp/powervr_sgx_frame -n 1;
28 words at 0x42000: STATE_LOADs into the PDS-vertex heap, two 3-vertex PRIMs,
terminates), extend dump_progs() in tools/gen/sgxgen.c to dump the c->pds_vtx
ring too, then check each record type and program against sgx540defs.h and
the vendor's stream builder.  In the same boot: prune TA_TE's SGX545-only
tail (0x2a8, 0x2b0-0x2d4, 0x324, 0x364-0x374, 0x520, 0x74c -- the vendor's
540 build writes none) and fix the bogus (0x230, cmd+0x2a4) mapping
(rt_flags -> MTE_OTPM_CSM_FLUSH_BASE).  Keep the .working pair as the revert,
verify the null family first, then submit ONE render and read the mid-hang
state.  Outcomes: the TE starts / pixels appear -> the stream was the wall,
walk do_3d next toward the frame loop; the stall moves to another unit ->
follow the new CLKGATESTATUS shape and the DPM counters; no change -> the
fault is in the VDM<->USE state-load handshake, instrument the USE side.

TRAPS THAT COST TIME (413/414, new): a PDS-cache done-bit mask names only
bits whose request register the same path pulses (b1=0xad0, b2=0xad4,
b4=0xadc, b0=0xae0) -- a header's field list is not a bitmask to sum; and
EVENT_STATUS2 is a different register on the 540 (only bits 0-4 exist; the
OTPM done bits live in EVENT_STATUS b19-21; TRIG_TA/3D/DL at b4/b3/b2).

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
rw; BOOTCTR 0x21 at the wrap-up; nproc 2, PID 1 affinity 0,1; battery Full;
panel bl_power 0; usb0 169.254.0.1/16 (PB_LINK=usb tools/pbssh.sh works; if
it times out check the cable, the host VPN (trap 341) and ufw (trap 342)
first).  Firmware on hd3: sgx540_ukernel.bin = kick15pds (24322c2a),
preserved variants .kick14otpm/.kick13inv/.kick12inv/.kick11wl/.kick10live
and .working = kick10 (3fbbd11b); module powervr_sgx.ko = 1a2949ab, preserved
.predumpregs (85fe59a2) and .pre-render = .working (10c6f435).  Desktop
llvmpipe -- the hd3 flasher image still lacks it, so a reflash loses it.  The
uAPI test binary is /tmp/powervr_sgx_test and the frame tool is
/tmp/powervr_sgx_frame (efaefc61, with the VDM-stream dump) -- both on the
ext4.  The full build/deploy recipe is HANDOFF section 3.

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
