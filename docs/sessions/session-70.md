# Session 70 (absolute) — the 8× buffer waste: root-caused as a pitch-alignment bug, fixed and deployed

> Numbering: absolute `70` (the project's chronological scheme — see
> `NUMBERING.md`).  The reports call this **Linux-era session 64**
> (`absolute = Linux-era + 6`).  **Continues session 69**, which finished the
> GPU client path (report 269 §4g–§4i) and left trap 349 open with this
> experiment specified.

Date: 2026-11-24.  Unit at the end: **Linux/Nura on hd3**, region 0
**`0x3547D613`** (unchanged — **no region-0 or region-1 write this session**),
hd3 the Nura ext4 (`0xEF53`, 15 273 984 × 4 KiB blocks), `BOOTCTR_FCTRY` =
**`0x1e`**, SSH up (`PB_LINK=usb`), `nproc` 2, PID 1 mask `3`,
catacomb `1.5.0-r10003` + epitaph `1.4.0-r10008` installed, all four desktop
processes on `pvr_dri`, **the pitch-fix shim deployed and loaded**, idle desktop
**9.5 MiB** pinned.

## Goal of this session

HANDOFF-69 §0f.1: settle why a re-rendering pvr client fails on its **second**
buffer under catacomb (3 of 5 runs) and never under phoc.  The handoff listed
four branches and asked for the `WAYLAND_DEBUG=1` event ordering under both
compositors plus the pvr allocation trace.

**Done, and further than that**: the mechanism was found (TILER capacity →
report 270), the reason behind the capacity number was found (an 8× allocation
waste → report 271), and the waste was **fixed, deployed and validated on the
panel**.

## Confirmed (measured)

Each claim with the artefact that proves it.

### Trap 349 is scan-out capacity, not a renderer defect (report 270)

* **The failing allocation is omapdrm's, on the scan-out side.**  Unfiltered
  `dmesg` on a failure shows `omapdrm omapdrm.0: could not remap: -12 (3)` →
  `dma_buf_map_attachment failed: -12` → `PVRSRVMapDmaBufKM: Failed to get
  dma-buf phys addr` → the client's `CreateImageShared: Failed to create
  buffer`.  [report 270 §1.3; `docs/captures/gpu-tiler-exhaustion-2026-11-24.txt`]
* **Rendering was never the problem**: the clients map `pvr_dri` and report
  `GL_RENDERER = PowerVR SGX 540` in every run, failing or not.  The SGX540
  (`renderD129`) renders; omapdrm (`renderD128`) scans out.  [report 270 §1.1]
* **`(3)` in `could not remap: -12 (3)` is `TILFMT_PAGE`**, the linear path —
  enum is `8BIT=0, 16BIT=1, 32BIT=2, PAGE=3`; GEM flags `0x02000003` have the
  `OMAP_BO_TILED_MASK` bits zero.  [report 270 §0.1]
* **The TILER aperture is 128 MiB** (`TILVIEW_8BIT = 0x60000000`,
  `2^14 × 2^13 = 2^27`), shared by all allocation formats on OMAP4
  (`omap_dmm_tiler.c:933-935`).  [report 270 §1.4]
* **Threshold measured 10/10 on one boot**: pinned 59.4 MiB → clean,
  112.3 MiB → fail.  Held back-to-back and with a 6 s settle.  [report 270 §1.2]
* **The catacomb/phoc asymmetry is headroom, not a code path**: phoc idles at
  39.8 MiB, catacomb at 59.4 MiB; **both** release client buffers (15–17 per
  run) and **both** advertise `wl_drm.device("/dev/dri/renderD128")`, which is
  omapdrm.  [report 270 §2]

### The 8× is a pitch-alignment bug (report 271)

* **`libdbm` describes a linear buffer as `width = 1, height = <total bytes>,
  bpp = 8`** — captured by an `LD_PRELOAD` logging shim:
  `CREATE_DUMB 1x2457600 bpp=8 → pitch=8 size=19660800`.
  [`docs/captures/gpu-pitch-fix-2026-11-24.txt`]
* **omapdrm aligns the scanline pitch to 8 bytes**:
  `omap_gem_dumb_create()` → `drm_mode_size_dumb(dev, args, SZ_8, 0)`
  (`local/kernel-tree/drivers/gpu/drm/omapdrm/omap_gem.c:589`).  A 1-byte-wide
  row therefore becomes an 8-byte pitch → 8× the bytes.
* **The kernel is faithful, not rounding**: a raw `DRM_IOCTL_MODE_CREATE_DUMB`
  gives 1.00× for the same geometry, and the kernel **recomputes**
  `size = pitch × height`, ignoring any size passed.  [report 271 §1]
* **The multiplier is exactly 8.00 at every size and pixel format** (bpp 8/16/32
  × 5 resolutions), so it is not geometry.  [report 271 §1]

### The fix works and is deployed

* **`tools/bench/gpu/dbmfix.c` re-expresses the bytes as `width=<bytes>,
  height=1`** → `pitch=2457600 size=2457600`, ratio 1.00.
* **Deployed for the whole session**: `/usr/local/lib/dbmfix.so` +
  `/usr/local/bin/catacomb-dbmfix` + `catacomb.desktop` `Exec=` repointed
  (backup `.orig`), applied by `reboot -f`; the shim is loaded in catacomb,
  epitaph, tabula **and** wvkbd (3 mappings each).
  [`docs/captures/gpu-pitch-fix-desktop-2026-11-24.txt`]
* **Measured after the fix**:
  * idle desktop pinned TILER **59.4 → 14.0 MiB** (4 h uptime) and **9.5 MiB**
    on a fresh boot (6 objects: 2 × 2,457,600, 1 × 2,396,160, 2 × 81,920, fbcon);
  * **every** GEM object is 1.00× — the largest is 2,457,600 B = one full frame,
    where it used to be 19,660,800;
  * **8/8** re-rendering pvr clients clean, occupancy **flat at 20.7 MiB**
    (before: 3/5 failed, climbing 59.4 → 112.3 MiB);
  * **panel operator-confirmed correct twice** — once after the deployment
    reboot and once after a later clean reboot.
* **Trap 349 no longer reproduces.**  [report 271 §5]

### Bench measured at the close (fresh boot, verified this session)

* **Region 0** trailer at raw `0x8100000 + 10 448 868 − 8` =
  `1f 2d c8 d7 13 d6 47 35` → CRC **`0x3547D613`**; unchanged, no write.
* **Region 1** (64 MiB at `0x29000000`) sha256
  **`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`** —
  re-read this session, identical to sessions 67/69.
* **hd3**: ext4 magic **`0xEF53`**, `s_blocks_count_lo` **15 273 984**,
  `s_log_block_size` 2 (4 KiB) → 62.56 GB raw; root `/dev/loop0` ext4 rw.
* **`BOOTCTR_FCTRY = 0x1e`** at the close (3 boots this session: `0x20 → 0x1f →
  0x1e`), each read `≥ 0x1e, no write`.  `/init` tops up below `0x1e`, so this
  is the last value before a top-up — worth a look next session.
* `nproc` 2, `taskset -p 1` → 3, zero DSI errors, panel `connected`,
  backlight 255, battery `Full` 4.148 V / 100 %.

## Inferred

* **The omapdrm `WARN_ON(pin_cnt > 0)` at `omap_gem.c:1209` is a real refcount
  imbalance or a symptom of the failed-pin path.**  The object is freed from
  `dma_buf_release` on process exit with a pin still held.  Moderate confidence —
  it is now much less urgent, since the pitch fix removed the pressure that
  triggered it.  Would be settled by instrumenting
  `omap_gem_pin`/`omap_gem_unpin` against `omap_framebuffer_pin/unpin` and
  `drm_gem_fb_destroy`.  [report 270 §4]
* **phoc benefits identically from the shim.**  It uses the same `libdbm`, so
  the pitch bug and its fix are compositor-independent — but this was **not
  measured** (only catacomb was run under the shim).  Strong inference.
* **The shim is safe for wvkbd too.**  It inherits the `LD_PRELOAD` and loads
  it (3 mappings) and still renders — but it is a `wl_shm` client, so this is
  evidence of "no harm", not of benefit.

## Dead ends and wrong assumptions (valuable)

Four wrong beliefs were killed by measurement this session.  They are kept
because each one cost real time.

* **"`-12 (3)` means 32-bit tiling."**  I read the `(3)` as `TILFMT_32BIT`.
  **Killed by**: recomputing the sizes — a genuinely tiled 1024x600 AR24 buffer
  would be 2.38 MiB, and the measured objects were 18.5–19.7 MiB, which matches
  no tiled layout.  The enum is `8BIT=0, 16BIT=1, 32BIT=2, PAGE=3`, so it is
  `TILFMT_PAGE`.  [report 270 §0.1]
* **"The 8× is slot rounding / tiled-layout amplification."**  **Killed by**:
  `tiler_reserve_1d()` reserves `obj->size` **1:1**, and the ratio was exactly
  8.00 at three different sizes and three pixel formats — a geometry effect
  would vary with size.  The lesson recorded: an exact integer ratio across
  several sizes is an allocator constant, not geometry.  [report 271 §1]
* **"Prefer `LINEAR`/non-tiled buffers" (my proposed fix).**  **Killed by**:
  the buffers were **already** linear (`TILFMT_PAGE`, tiled-mask bits zero), so
  the change addresses nothing.  Retracted in report 270 §4, DECISION §104 and
  trap 349.
* **"The DDK asks for `height × 8` rows."**  Consistent with *every* size
  measurement, and a `/8` height clamp appeared to work — but only because 8
  happened to be the pitch alignment.  **Killed by**: an `LD_PRELOAD` shim that
  *logged* the ioctl arguments instead of guessing from the resulting size.  The
  height was **bytes**, not rows: `width=1, height=2457600`.  This is the
  session's most useful lesson — **log the arguments, not just the result**;
  every size-only probe I wrote could not distinguish "8× height" from "8 bytes
  per row".  [report 271 §1]
* **"catacomb never sends `wl_buffer.release`."**  I sampled five runs and
  counted zero releases under catacomb vs 16–17 under phoc, which pointed
  straight at the handoff's compositor-lifetime branch.  **Killed by**: those
  runs had died at `eglCreateWindowSurface` (`context/surface failed 0x3009`)
  and **never created a buffer**.  Re-measured on runs that reached `swap=1`:
  catacomb sends 15–16.  Trap 351 records the rule.
* **"trap 349 is a pvr DDK second-allocation defect"** (the handoff's leading
  hypothesis and session 69's inference).  **Killed by**: the failure chain is
  omapdrm's `tiler_reserve_1d()` returning `-ENOMEM`; the DDK is handed the
  error last, and its `bad_mapping` message is downstream.
* **"`/sys/kernel/debug/dma_buf/bufinfo` gives the buffer accounting."**
  **Killed by**: it **blocked** a whole SSH command batch until timeout.  And
  `dri/*/mm` is the VMA address space, always `used 0`.  Trap 352.
* **"`18,513,920 B` objects left behind after a test batch = a leak."**
  **Killed by**: those were runs launched **without** the `LD_PRELOAD` (the env
  was being passed per-command), so they allocated the old 8× way.  The
  **unfixed** runs are the ones that leave debris — check object *sizes*, not
  just the total.
* **"Restarting `tinydm` to switch compositors is fine."**  It wedged the DSI
  link (trap 289/353): `DSI error: Failed to set dsi_if_enable to 0` plus
  `NO PACKET_SENT` on VC0 **and** VC1, black panel with backlight lit, DISPC
  still scanning, catacomb's IPC still answering `dpms on`.  Only `reboot -f`
  cleared it.  For the deployment I used a reboot instead, which is also the
  form we would package.  The operator saw the wedge and both recoveries.
* **"The SSH timeout mid-wrap-up was trap 337 (the intermittent VUSB teardown)."**
  It was not — **the cable was simply unplugged** (operator, at wrap-up).  The
  host checks came back clean (`ip rule` had no VPN rule, `enp8s0f3u1i2` held
  `169.254.0.2`), and the link returned as soon as it was plugged back in.  Trap
  337 remains **open and unmeasured this session**; do not treat a timeout as
  evidence for it without checking the cable.  The long uptime I first read
  (8 h) was the operator pausing, not a survivor of a drop.  [the
  `docs/contradictions/usb-vusb-drop-idle-vs-intermittent.md` question is
  unchanged]

## Unknown / open questions

* **Is the omapdrm pin `WARN_ON` a real leak?**  Now low priority (the pressure
  that exposed it is gone).  Settle by instrumenting the pin/unpin callers.
* **Does the shim work for phoc?**  Almost certainly, same `libdbm`; unmeasured.
* **Does the pitch fix survive a heavy-redraw measurement?**  Still unmeasured —
  and now the interesting question, because the TILER pressure that used to
  confound it is gone.
* **Is `9.5 MiB` the true floor, or is there more waste?**  Six objects at 1.00×
  for a 1024x600 desktop looks close to minimal, but 2 full frames + 2,396,160 +
  2 × 81,920 has not been justified object-by-object.
* **The hd3 rebake is still owed** — and it now must also carry the **pitch
  fix** (`tools/rebake-hd3.sh` takes `DBMFIX_SO=…`).  The baked image is the
  session-59 state, so a reflash loses the whole session-69/70 result.

## Superseded / uncertain

* **Session 69's and the handoff's "the pvr second-buffer failure is a pvr
  allocation defect"** — superseded by report 270 (it is omapdrm TILER
  capacity) and then report 271 (the capacity number was itself an 8× waste).
  The current statements are report 271 and trap 349.
* **This session's own first two mechanism claims** (32-bit tiling; slot
  rounding) — superseded within the same session by report 270 §0.1/§0.2, then
  by report 271.  Kept in the reports so the correction is visible.
* **The earlier handoff's claim that the baked hd3 image lacks the pvr initrc,
  the pvr `mesa.sh` and the pvrports stack, and needs llvmpipe + udev/polkit
  rules** — superseded by measurement in this session (report/session note):
  the image already has all of those; `libLLVM-13`/llvmpipe is absent on the
  live root too, and both rule directories are empty.  The rebake delta is
  three things plus the pitch fix.
* **"`/tmp` is tmpfs on this root"** — superseded by trap 354 (it is on the
  62.5 G ext4; probes survive a reboot).

## Artefacts produced

* **Report 270** — `docs/reports/270-trap349-omapdrm-tiler-exhaustion-2026-11-24.md`
  (trap 349 root-caused as TILER capacity; two mechanism corrections in-place).
* **Report 271** — `docs/reports/271-8x-buffer-multiplier-2026-11-24.md`
  (the pitch-alignment root cause, the fix, and the deployment).
* **Captures**: `docs/captures/gpu-tiler-exhaustion-2026-11-24.txt`,
  `docs/captures/gpu-8x-allocation-multiplier-2026-11-24.txt`,
  `docs/captures/gpu-pitch-fix-2026-11-24.txt`,
  `docs/captures/gpu-pitch-fix-desktop-2026-11-24.txt`.
* **`tools/bench/gpu/`** — `dbmfix.c` (the fix) plus nine Python `ctypes`
  probes (`dumbchk`, `sizein`, `pitch`, `bochk`, `bochk4`, `final`, `heightspy`,
  `bpp`, `stride`) and a README.  No compiler needed on the unit.
* **`tools/rebake-hd3.sh`** — the hd3 rebake, scoped by measurement; takes
  `DBMFIX_SO=` to bake in the pitch fix.
* **Device-side (hd3, persistent)**: `/usr/local/lib/dbmfix.so` (sha256
  `cc302b42…`), `/usr/local/bin/catacomb-dbmfix`,
  `/usr/share/wayland-sessions/catacomb.desktop` (repointed) and
  `catacomb.desktop.orig` (backup).  **A reflash of the session-59 image loses
  all four.**
* **Host-side toolchain**: `local/work/arm-linux-musleabihf-cross/` (from
  `https://musl.cc/arm-linux-musleabihf-cross.tgz`, 98 MB, sha256 `11e155a0…`)
  — needed because the pmbootstrap build chroot is deleted after each build and
  `local/toolchains/armv7-eabihf` is glibc.
* **Docs**: STATE, HANDOFF, ROADMAP 7a, KNOWN-ISSUES (trap 349 rewritten;
  351–355 added), DECISIONS **§104**, reports INDEX, session NUMBERING, this note.
* **No region-0 / region-1 / hd3 image write; no patch change; no restore
  required.**

## Note on the `.orig` backups and reverting

Revert the pitch fix by restoring
`/usr/share/wayland-sessions/catacomb.desktop.orig` over `catacomb.desktop` and
rebooting.  The shim is inert unless `DBMFIX=1`, so leaving the files in place is
harmless.
