# Session 69 (absolute) — the Wayland GPU client path on the SGX: two root causes and a working desktop

> Numbering: absolute `69` (the project's chronological scheme — see
> `NUMBERING.md`).  The reports call this **Linux-era session 63**
> (`absolute = Linux-era + 6`).  **This is the continuation of session 68**,
> which solved the selection gate (`wl_drm`, report 269 §1–§4f, catacomb
> r10002) and stopped with the client surfaces black; report 269's §4g–§4i are
> this session.

Date: 2026-11-24 (continued from the 2026-11-23 session-68 bench).  Unit at the end: **Linux/Nura on hd3**, region 0
**`0x3547D613`** (`#109 SMP`, `panic=0`; **no region-0 change this session**),
region 1 **`20a5edd3…`** (64 MiB at raw `0x29000000`, verified this session, not
booted), hd3 the Nura ext4 (`07555bde…` base, **15,273,984 blocks × 4 KiB**,
ext4 magic `0xEF53` at `0x49000438`), `BOOTCTR_FCTRY = 0x22`, SSH up
(`PB_LINK=usb`), `nproc` 2, PID 1 mask `3`.
**The desktop runs on the SGX end-to-end:** catacomb **`1.5.0-r10003`** and
epitaph **`1.4.0-r10008`** are installed on the live root (session 68 left
r10002/r10005),
`/etc/catacomb/initrc` is the pvr variant (no `unset`), and the operator
confirms wallpaper, keyboard and the bar's text/icons render and the UI is
responsive.

## Goal of this session

Finish session 68's GPU work: its `wl_drm` shim made the clients *select* the
SGX, but their surfaces were black.  The operator's directive is to make the
clients actually present on the SGX, keeping the desktop lightweight (1 GB
RAM).  "Done" meant the whole desktop — clients included — rendering on the
SGX.

## Confirmed (measured, read in a file, or seen on the bench)

### The selection gate: `wl_drm` (report 269 §1–§4d)

* **catacomb serves no `wl_drm`** (before this session).  `WAYLAND_DEBUG=1` on a
  client lists its globals (`wl_shm`, `zwp_linux_dmabuf_v1`, layer-shell, …) and
  no `wl_drm`.  [report 269 §1a]
* **Mesa 21.3.9's Wayland EGL platform needs it.**  `dri2_initialize_wayland_drm()`
  bails at `dri2_dpy->wl_drm == NULL` (`platform_wayland.c:1567`);
  `eglInitialize()` retries with `ForceSoftware = TRUE` (`eglapi.c:640`) and
  loads `swrast_dri`, so `MESA_LOADER_DRIVER_OVERRIDE` is never consulted (read
  only by `loader_get_driver_for_fd()`, `loader.c:478`).  Measured: the first
  EGL debug line is `MESA-LOADER: dlopen(…/swrast_dri.so)`, `epitaph` reports
  `softpipe`.  [report 269 §1b–§2]
* **The GBM/surfaceless path works:** `gbmtest2.py` on `renderD129` → EGL 1.4,
  12 window configs, `PowerVR SGX 540`, `EGL_MESA_image_dma_buf_export`.  [report
  269 §1c]
* **Smithay has no `wl_drm` implementation** (source tree checked), so catacomb
  could not enable one by configuration.  [report 269 §3]
* **wlroots/phoc does, and the same client presents under it.**  pvrports'
  phoc 0.52.rc1 advertises `wl_drm`; our presenting client (`pb-egltest`) runs
  as `pvr_dri`/`PowerVR SGX 540` under it and the operator **saw its colours on
  the panel** (red/green flashes).  [report 269 §4b, §4g; operator observation]
* **catacomb now serves a PRIME-only `wl_drm`** (`src/protocols/wl_drm.rs`,
  generated from `protocols/wayland-drm.xml`): it sends the render node,
  ARGB8888/XRGB8888, `capabilities(PRIME)`; `create_prime_buffer` builds a
  Smithay `Dmabuf` as the `wl_buffer`'s user data; GEM-name requests are
  rejected.  Shipped as catacomb r10002 (`wl-drm-prime.patch`, 432 lines).
  Measured: `global(32, "wl_drm", 2)`; `waylandtest.py` → 12 ES2 configs,
  `PowerVR SGX 540`, `pvr_dri`.  [report 269 §4c–§4e]

### Root cause 1: the imported texture was an incomplete GLES2 NPOT texture (report 269 §4g, trap 347)

* **The clients rendered; the compositor sampled black.**  Smithay's
  `GlesRenderer::import_egl_image()` binds the EGLImage without setting
  `GL_TEXTURE_WRAP_S/T`; the default `REPEAT` makes a **non-power-of-two**
  texture incomplete (GLES2 rule), so it samples `(0,0,0,1)` with **no GL
  error**.  Client buffers are NPOT (1024x20, 1024x585).  wlroots sets
  `CLAMP_TO_EDGE` (`render/gles2/texture.c`), which is why phoc works.
* Measured four ways: (1) our client's **pre-swap `glReadPixels` of its own
  drawable is magenta** (`nz=64/64`) yet black on screen under catacomb; (2) a
  CPU-filled `wl_drm` dumb buffer is black under catacomb while the same pixels
  via `wl_shm` display; (3) a standalone repro of catacomb's renderer
  (`tools/bench/egl/pb-gbmsample.c`: dlopen'd EGL, GBM on card0, config+context,
  surfaceless) imports with no error and samples `first=(0,0,0,255) nz=0`;
  (4) adding only `GL_TEXTURE_WRAP_S/T = CLAMP_TO_EDGE` samples
  `first=(0,255,0,255) nz=64/64`.  [report 269 §4g]
* **Fix:** `rootfs/catacomb/smithay-eglimage-clamp.patch`, catacomb
  `pkgrel=10003`, applied after the two existing Smithay patches (verified in
  that order against pinned revision `dba255c`).  **Installed and confirmed on
  the panel**: wallpaper, keyboard and the bar's background render on the SGX
  (operator).

### Root cause 2: the panel client hard-codes GL attribute/uniform indices (report 269 §4h, trap 348)

* **With the clamp in, the bar drew only its background.**  A one-shot
  `glReadPixels` histogram inside epitaph (`debug-framebuffer.patch`, r10007)
  reported the whole 1024x20 bar as **one colour** `(24,24,24)` with
  `glerr = 1282` (`GL_INVALID_OPERATION`).  [operator: "dark grey, definitely
  just epitaph's background"]
* **Cause:** epitaph passes literal `0/1/2` to `glVertexAttribPointer` and
  writes its projection with `glUniform4f(0, …)`.  GLES2 leaves those indices
  to the linker, and the PowerVR compiler assigns
  **`a_Position=2, a_UV=1, a_Flags=0, u_Projection=1, u_Texture=2`** (measured
  on the unit by linking epitaph's own shaders and querying the locations).
  So the projection was never set (glyphs collapse) and the write to location 0
  hit the sampler — the `1282`.  Softpipe used declaration order, which is why
  it worked before.  [report 269 §4h]
* **Fix:** `rootfs/epitaph/bind-attribs.patch` — `glBindAttribLocation` before
  linking (text and rect programs) and `glGetUniformLocation("u_Projection")`.
  epitaph `pkgrel=10008`.  **Installed and confirmed on the panel**: the bar
  shows its clock text and icons and the UI is responsive (operator).
* **End-to-end result:** bar draw **1.5–30 ms** steady state (softpipe was
  1.6–2.3 s), 1024x585 drawer 97 ms; the llvmpipe deferred-`swap` cost is gone.
  The clients now run on the SGX by default; the `wl_shm` safe state is
  retired.  [report 269 §4i; operator]
* **Operator preference recorded:** ask what is on the panel for a visual
  check; do not take screenshots.

### Bench state measured at the wrap-up

* **Region 0** media trailer at raw `0x8100000 + 10,448,868 − 8` reads
  `1f 2d c8 d7 13 d6 47 35` → CRC **`0x3547D613`**, equal to
  `boot/chainloader/kernel.img` (10,448,868 B, sha256 `6ecd068d…`).
  **No region-0 change this session.**
* **Region 1** (64 MiB at `0x29000000`) sha256
  **`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`** —
  read fresh this wrap-up; identical to session 67.
* **hd3**: ext4 magic **`0xEF53`** at `0x49000438`, **15,273,984 blocks**
  (62.56 GB raw; `df` 56.5 G usable); root `/dev/loop0` rw; packages changed
  this session (catacomb r10003, epitaph r10008, pvr initrc).
* **`BOOTCTR_FCTRY = 0x22`** (`22000000 ≥ 0x1e`, no write) after the
  mid-session clean `reboot -f`; `nproc` **2**, `taskset -p 1` → **3**;
  uptime 1:26 at the close.
* **Device-side files** (persistent on hd3): `/usr/bin/catacomb` (r10003),
  `/usr/bin/epitaph` (r10008), `/etc/catacomb/initrc` (pvr variant, 3168 B,
  identical to `initrc.session68-pvr`), `/usr/share/wayland-sessions/phoc-pb.desktop`
  (the comparison session).  `/tmp` is tmpfs (the probes were wiped by the
  reboot and are re-pushed as needed).

## Inferred (reasoning from evidence, not directly measured)

* **The open stack would remove the `wl_drm` requirement.**  Mesa 26.2.5's
  Wayland platform asks `zwp_linux_dmabuf_v1` for default dmabuf feedback and
  maps `main_device` to a render node (`platform_wayland.c:2167`, `:2629`);
  `wl_drm` is only the fallback, and catacomb already advertises
  `zwp_linux_dmabuf_v1` v5.  Strong source-read; not bench-tested.  [report 269
  §4d]
* **Trap 349 is a pvr allocation defect, not a protocol or compositor bug.**
  The client fails on its second buffer (`PVRDRIBufferCreate: Failed to create
  plane`) in 3 of 5 runs under catacomb and never under phoc; both compositors
  send `wl_buffer.release` (8–9 releases over 5 swaps measured on each).
  Moderate.  [report 269 §4h]
* **Trap 350 (cosmetic glitches) is pre-session-68 code.**  The clamp fix
  cannot alter drawn pixels, and both affected clients use different buffer
  types; the common code is the bring-up-era upload/damage path.  Moderate;
  not measured.  [trap 350]

## Dead ends and wrong assumptions (valuable — do not skip)

* **"Install Phosh" was the wrong first move.**  pvrports' supported UI, but
  358 packages / +323 MiB is wrong for 1 GB (operator).  [report 269 §4e]
* **"A custom Qt client sidesteps it" is false** for the closed stack: Qt
  clients use Mesa's wayland platform, and a Qt compositor serves no `wl_drm`.
  [report 269 §4d]
* **"The override is enough"** — no: the swrast fallback never reads it.
  Necessary, but only after `wl_drm` exists.  [report 269 §2]
* **"The clients are on the GPU" — wrong as first written.**  Inferred from
  `GL_RENDERER`/`maps`; the capture and the operator showed black surfaces.
  Selection ≠ presentation.  Corrected in place; trap 346.  [report 269 §4e]
* **"The pvr render-node bind-mount is not a fix":** `renderD129` over
  `renderD128` made `tabula`/`epitaph` exit.  [report 269 §4f]
* **"The pvr driver cannot import a foreign dma-buf" — withdrawn.**  A test
  that linked `libEGL.so.1` at load time segfaulted on a GBM-platform
  surfaceless `eglMakeCurrent` and read back black; with `dlopen`'d EGL (as the
  compositors do) the import works, and the only defect was the wrap mode.
  Harness lesson: **reproduce the compositor's EGL path with `dlopen`.**  [trap
  347; `tools/bench/egl/README.md`]
* **"tabula under phoc is inconclusive"** (earlier this session): it was
  inconclusive *then* because `tabula`/`epitaph` need catacomb's IPC.  A
  dedicated client (`pb-egltest`) settled it: the same client presents under
  phoc.  [report 269 §4g]
* **A direct-linked-EGL probe also produced a misleading `GL_INVALID_OPERATION`
  and black readbacks**; the dlopen repro is the trustworthy one.
* **The intermittent second-buffer failure is not fixed by a clean reboot** —
  it reproduced after one (3/5 runs).  So it is not leaked GPU memory from the
  crashed probes.  [trap 349]

## Unknown / open questions

* **Why does a re-rendering pvr client fail on its second buffer under
  catacomb (3/5) and never under phoc?**  Settle with: a minimal client that
  renders exactly N frames, run under both compositors with `WAYLAND_DEBUG=1`,
  plus the pvr allocation trace; compare the `wl_buffer` lifetimes and the
  import/release order.  A possible lead: catacomb caches the imported texture
  while phoc may not.  [trap 349]
* **What causes the cosmetic pixel glitches** on shm (keyboard) and, at times,
  dmabuf (bar) clients?  Settle with: force the slow per-row-only upload path
  (or run with the pre-session-68 `wl_shm` client state) and see whether the
  glitches persist; if they do, instrument catacomb's damage tracker.  [trap
  350]
* **Heavy-workload performance / frame latency** on the now-working path —
  the static desktop is measured; an animated workload is gated by trap 349.
* **The hd3 baked image is still the session-59 state** (catacomb r10000,
  epitaph r10005, no pvr initrc).  A reflash reverts the whole result.  The
  rebake is owed.  [trap 343 class]
* **Upstreaming** the `wl_drm` global to Smithay and the clamp to Smithay, and
  `bind-attribs` to epitaph — all small, all worth sending.

## Superseded / uncertain

* **Report 269 §4e's original claim** ("catacomb clients now use the SGX",
  from `maps`/`GL_RENDERER`) is superseded by its own correction: the clients
  selected the SGX but presented black.  The corrected text is current.
* **STATE/DECISIONS/ROADMAP/trap 345 as first written** ("the client path is
  fixed/validated") are superseded by "selection fixed, presentation open" and
  then by §4g–§4i.
* **The session title** ("selection solved, presentation open") was true for
  the first half; the session ended with the presentation fixed and validated.
* **"The pvr render node is the fix"** is not supported (the bind-mount made
  the clients exit).
* **The early view that the client's render might not land in the shared
  buffer** is withdrawn: its own pre-swap readback is non-black, and the same
  pixels render when handed to the compositor as `wl_shm`.

## Artefacts produced

* Report **269** (`docs/reports/269-wayland-pvr-rejection-wl_drm-2026-11-23.md`)
  — §1–§4i; §4g is the root cause, §4i the validated result.
* Patches, tracked: `rootfs/catacomb/wl-drm-prime.patch` (r10002),
  `rootfs/catacomb/smithay-eglimage-clamp.patch` (r10003),
  `rootfs/epitaph/bind-attribs.patch` (r10008),
  `rootfs/epitaph/debug-framebuffer.patch` (diagnostic only).
* Packages built: catacomb `1.5.0-r10003` and epitaph `1.4.0-r10008` (host
  `local/work/nura/work/packages/edge/armv7/`), both installed on the live
  root.
* Probes, tracked: `tools/bench/egl/` — `pb-egltest.c`, `pb-layerdrm.c`,
  `pb-gbmsample.c`, `pb-gbmimport.c`, `pb-dlopen.c`, `pb-min.c`, README.
* Host build helpers: `local/work/nura/run-build-catacomb.sh`,
  `run-build-epitaph.sh` (root-owned chroot; the one step an agent cannot do).
* Docs: STATE, HANDOFF, ROADMAP 7a, KNOWN-ISSUES traps **345–350**,
  DECISIONS **§101–§103**, reports INDEX, this note.
* No region-0/region-1 write; no restore required.
