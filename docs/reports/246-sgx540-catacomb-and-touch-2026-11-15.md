# 246 — catacomb on the SGX540 (smithay GLES2 fallback), the touch Y fix, and the vendored SGX driver

Date: 2026-11-15 (session 56, Linux-era 50).
Bench: the unit boots the Nura overlay root on **region 0 `0xAB899C49`**
(kernel `7.3.3 #69`) and **hd3 `89399744…`** (the compositor image).  `catacomb`
1.5.0-r10000 runs on the pvr renderer, `squeekboard` is composited, the touch
cursor tracks the finger, and `BOOTCTR_FCTRY` reads `0x2c`.

## 0. Headline

* **catacomb runs on the PowerVR SGX 540.**  The blocker was never the GPU: it
  is smithay's *hard requirement* for `GL_EXT_unpack_subimage` at renderer
  creation.  A small smithay patch relaxes that check and implements the
  per-row/full-row upload fallbacks, and `catacomb` now composites on
  `libGLESv2_PVR_MESA`/`libsrv_um`/`libusc`/`pvr_dri.so` (verified in
  `/proc/<pid>/maps` — no `swrast`).
* **The touch Y axis was inverted.**  Measured: physical top edge → evdev
  `Y ≈ 577–591`, bottom edge → `Y ≈ 35–51`.  The Munin driver now inverts the
  mapped Y; the cursor tracks exactly.
* **Both changes are baked and flashed:** region 0 `0xAB899C49` (`#69`, the
  touch fix) and hd3 `89399744…` (catacomb r10000).  One bootrom session wrote
  region 0 + hd3 v1 (`both-0xAB899C49-compositor.signed`, `COMPLETE 0x40C0 ->
  cmd 0x4006`); a second hd3-only flash wrote the optimized v2.
* **The export is re-done:** `0001-playbook-winchester-7.3.3.patch` is now
  **64 files / 8 800 lines**, sha256 `014eb8a8…`, verified 0 rejects / 64/64
  files byte-identical; the **366-file SGX driver is vendored** as
  `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` (606 964 B, sha256
  `a9d04d3c…`, 362 files, extraction verified 362/362).
* **Performance is still open.**  Tapping OSK keys drops frames badly; dragging
  over the black area is better but still hitchy.  The full-row partial-upload
  optimization (v2) did not remove the key-press cost.  Next session: apps,
  background, and a proper profile (stride/damage, `glFinish`/fencing).

## 1. Why catacomb panicked (the diagnosis)

Catacomb 1.5.0 pins smithay **0.7.0** (git `dba255cd…`).  In smithay's GLES2
renderer (`src/backend/renderer/gles/mod.rs`):

```rust
if gl_version < GLES_3_0 && !exts.iter().any(|ext| ext == "GL_EXT_unpack_subimage") {
    return Err(GlesError::GLExtensionNotSupported(&["GL_EXT_unpack_subimage"]));
}
```

The SGX540 is a GLES **2.0** part and does not expose that extension, so
`GlesRenderer::new()` fails and catacomb panics at `src/udev.rs:400`
(`create renderer: GLExtensionNotSupported`).  The extension only adds
`UNPACK_ROW_LENGTH`/`SKIP_PIXELS`/`SKIP_ROWS`; the Khronos spec explicitly
allows the `TexSubImage2D`-one-line-at-a-time fallback.  The same class of fix
is what pvrports carries for wlroots (`phoc`).

## 2. The smithay patch

Applied to the fetched cargo checkout in the package's `prepare()` (the pinned
git rev, `smithay-312425d…/dba255c`).  It touches only
`src/backend/renderer/gles/mod.rs`:

* **Renderer creation:** the hard error is replaced by a warning; the support
  flag is computed from `gl_version`/`extensions` wherever it is used.
* **`import_shm_buffer` full upload:** tight stride → one `TexImage2D`;
  padded stride → allocate (NULL `TexImage2D`) then one `TexSubImage2D` per
  row.
* **`import_shm_buffer` partial damage:** tight stride → **whole rows** for the
  damaged y-range, one call per damage rect (the extra pixels are unchanged in
  the client buffer); padded stride → one call per row.
* **`update_memory`:** tight path unchanged; fallback reads the full-texture
  layout row by row (`((y_region + y)*W + x_region)*bpp`).

No unsupported pixel-store parameter reaches the GLES2 driver in the fallback.
The patch is `local/work/nura/pmaports/main/catacomb/smithay-gles2-fallback.patch`
(sha512 `1c07cd03…` for v2).

## 3. Building catacomb for this root

The Alpine `testing/catacomb` APKBUILD was copied into
`local/work/nura/pmaports/main/catacomb/`, with `pkgrel=10000` and
`options="net !check"` (skip the duplicate `cargo test` compile).  The build
runs in the pmbootstrap edge chroot:

```bash
cd local/work/nura/pmbootstrap
pkexec env HOME=$HOME \
  XDG_CONFIG_HOME=$PWD/../config PYTHONPATH=$PWD \
  python3 pmbootstrap.py --as-root --no-cross --no-ccache \
  build --lax --arch armv7 catacomb
```

`--no-cross` is needed because crossdirect refuses a native rustc **1.100.0**
against the armv7 chroot's **1.99.1** (a transient Alpine lag), and `--no-ccache`
because `sccache` is not installed in the QEMU buildroot.  The build is
`cargo auditable build --release` (LTO) under QEMU — about 18–20 min with
`!check`.  Artifact: `local/work/nura/work/packages/edge/armv7/
catacomb-1.5.0-r10000.apk`.

## 4. The touch Y inversion

Report 198 decoded the vendor calibration as `raw y 110→658` → `display
60→540` and its edge drag read `Y=0` at the top.  On the current display
pipeline the hardware is the opposite: a repeated top/bottom edge capture
(`/dev/input/event0`, 16-byte `input_event`) gave:

| touch | evdev `ABS_MT_POSITION_Y` |
|---|---|
| physical top edge | 577–591 |
| physical bottom edge | 35–51 |

so the evdev Y axis points up.  The fix is in
`drivers/input/touchscreen/playbook-munin.c`: report
`panel_h - 1 - munin_map(cal_y, yr, panel_h - 1)`.  Kernel `#69`; the bench
cursor now tracks top→top, bottom→bottom.  (The X axis was always correct.)

## 5. Flashes and verification

* **both halves:** `local/work/nura/images/both-0xAB899C49-compositor.signed`
  (635 961 936 B, sha256 `426ed972…`; runs at 16 / 16640; signature CRC
  `0x69B41FF7 OK`) — written in one bootrom session, `COMPLETE 0x40C0 -> cmd
  0x4006`, `done`.
* **hd3 v2 only:** `--component blackberry-playbook-root-compositor2.img
  --dest-block 0x4915` (626 065 440 B payload, `COMPLETE`/`done`).
* Boot: `Linux playbook 7.3.3 #69`; `/usr/bin/catacomb` md5 `7418d9ea…`;
  `rc-status`/`tinydm` started; the DSI HS-clock null packets
  (`irqstatus=0x4 polls=1`) — no `NO PACKET_SENT`, no VC errors on a clean
  boot.
* `GL_EXT_unpack_subimage not supported, using per-row uploads` in the tinydm
  log; the compositor maps `libGLESv2_PVR_MESA`, `libsrv_um`, `libusc`,
  `pvr_dri.so` — **GPU**, not software.
* `BOOTCTR_FCTRY` = `0x2c` (`/init` correctly wrote nothing, `>= 0x1e`).

**A restart can wedge the DSI.**  Stopping tinydm after the earlier
panicked/restarted session did not restore the console, and restarting catacomb
over that state produced `DSI: Failed to set dsi_if_enable to 0`, `VC(0) busy`,
`NO PACKET_SENT`, `VC(1) error`.  A clean boot does one clean modeset and works.
Recover with a reboot; do not iterate compositor restarts on a wedged link.

## 6. Export and packaging (the choice)

The SGX driver is 362 source files / 6.5 MB of third-party GPL-2 code.  The
operator chose to **vendor** it rather than grow the board patch:

* `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` — 606 964 B, sha256
  `a9d04d3c…`, `pvrsgx/{Kconfig,Makefile,pvr-drv.c,pvr-drv.h,1.17.4948957/}`;
  `kernel/vendor/README.md` records the source URL, branch `pvrsrvkm-7.3`,
  commit `101c687a…` (2026-10-19), licence, the extraction step, and the
  integration (Kconfig/Makefile/`mm/vmalloc.c`/DTS/config).
* `0001-playbook-winchester-7.3.3.patch` — **64 files / 8 800 lines**, sha256
  `014eb8a8…`.  It now includes the three SGX integration files
  (`drivers/gpu/drm/Kconfig`, `drivers/gpu/drm/Makefile`, `mm/vmalloc.c`) and
  the Munin Y fix.  The two generated headers that the old 63-file export had
  wrongly carried (`scripts/mod/devicetable-offsets.h`, `scripts/mod/elfconfig.h`)
  are gone.
* **Verification:** a pristine 7.3.3 copy + the patch applies with **0
  rejects**; after extracting the vendor archive, **64/64** patched files and
  **362/362** driver files are byte-identical to `local/kernel-tree`.  The
  tree walk was run after `make clean`, excluding `scripts/` (host tools) and
  the usual artifacts; the `Kconfig.*` filter is in place (trap 211).

## 7. Performance notes (open)

* Tapping an OSK key drops the framerate badly; dragging the cursor over the
  black area is better but still hitches.  The v2 full-row partial upload did
  not remove the key-press cost.
* Candidates for next session: (a) instrument the OSK buffer stride and the
  damage rects — if the stride is padded, the full-row path is not taken;
  (b) pack damaged rows into one `TexSubImage2D`; (c) the no-fencing path calls
  `glFinish()` after each upload (`GL_OES_EGL_sync` absent) — check whether
  `EGL_KHR_fence_sync` is available and can replace it; (d) the OSK's own
  redraw cost and the missing wallpaper/apps.
* There is no wallpaper or app content yet (`tabula`, `epitaph`, `chatty`,
  `gnome-calls`, `swayidle` are not installed; the root was built with
  `--no-recommends`).  The white bar + black background is the empty catacomb
  UI, not a rendering failure.

## 8. Next

1. Apps/background for real testing: `tabula` + the installed
   `postmarketos-artwork-wallpapers`, a terminal/browser/paint client, and the
   missing catacomb initrc companions.  Then profile the frame path.
2. The performance items above.
3. Keep the vendor archive and the export procedure in sync on the next kernel
   change (copy step in `kernel/vendor/README.md`).

## 9. Artefacts

* `docs/reports/246-sgx540-catacomb-and-touch-2026-11-15.md` (this report).
* `local/images/kernel-0xAB899C49-touch.img` (9 860 768 B, sha256
  `c37ee2fe…`); `boot/chainloader/kernel.img` holds the same image.
* `local/work/nura/images/blackberry-playbook-root-compositor2.img`
  (625 999 872 B, sha256 `89399744…`); `both-0xAB899C49-compositor.signed`
  (sha256 `426ed972…`).
* `kernel/patches/0001-playbook-winchester-7.3.3.patch` (64 files / 8 800
  lines, sha256 `014eb8a8…`); `kernel/vendor/pvrsgx-…tar.xz` (sha256
  `a9d04d3c…`).
* `local/work/nura/pmaports/main/catacomb/` (the package + the smithay patch);
  `local/work/nura/work/packages/edge/armv7/catacomb-1.5.0-r10000.apk`.
* Traps 285–291; `DECISIONS.md` §79/§80.
