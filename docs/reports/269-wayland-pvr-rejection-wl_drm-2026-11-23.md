# Report 269 — why Mesa's wayland platform refuses pvr: catacomb does not advertise `wl_drm`

Date: 2026-11-23 (session 68); **§4g–§4i were added in session 69**, which
continued the work on the same bench.  Bench + host; no device writes.  Answer to
report 268 §3 step 1 (the one diagnostic that had to precede any Mesa work).

## 0. Headline

**The Wayland-client path does not select the SGX because catacomb (Smithay)
does not implement the legacy `wl_drm` protocol.**  Mesa 21.3.9's Wayland EGL
platform obtains the DRM device fd *only* through `wl_drm`; without that global
`dri2_initialize_wayland_drm()` fails, and `eglInitialize()` then retries with
`ForceSoftware = TRUE`, which loads `swrast_dri.so` unconditionally.  The
`MESA_LOADER_DRIVER_OVERRIDE` variable is consulted on the **fd** path
(`loader_get_driver_for_fd()`), which the software retry never reaches — so the
override being "ignored" is expected, not a bug.

Measured, both ways, on the unit:

* `epitaph` on `wayland-1`, with and without the override, logs
  `MESA-LOADER: dlopen(/usr/lib/xorg/modules/dri/swrast_dri.so)` as its **first**
  driver action and never tries `pvr_dri`; it reports `softpipe`.
* `WAYLAND_DEBUG=1` on the same client shows catacomb's globals, and `wl_drm` is
  absent (the list is in §1).
* The same `libEGL_amber` on **GBM** (`renderD129`) and on **surfaceless** gives
  `GL_RENDERER = PowerVR SGX 540`, 12 window configs and
  `EGL_MESA_image_dma_buf_export` — re-confirmed this session.

This corrects the framing in report 268 §0: the missing piece is a **protocol
the compositor does not implement**, not a DRI extension the driver lacks.  It
also explains why pvrports supports only compositors that *do* implement
`wl_drm` (wlroots/phoc, KWin, Mutter) and not catacomb.

## 1. The measurement

All on the unit, `7.3.3 #109 SMP`, hd3 root, catacomb running as `playbook`
(`/run/user/10000`, `wayland-1`).

### 1a. The Wayland globals catacomb advertises

`WAYLAND_DEBUG=1` on a client, then the `wl_registry.global` events, parsed
(`/tmp/wl-debug.log`):

```
wl_compositor  wl_subcompositor  xdg_wm_base  zwlr_layer_shell_v1  wl_shm
zxdg_output_manager_v1  wp_fractional_scale_manager_v1  wp_viewporter
zxdg_decoration_manager_v1  org_kde_kwin_server_decoration_manager
zwlr_screencopy_manager_v1  ext_foreign_toplevel_image_capture_source…_v1
ext_output_image_capture_source_manager_v1  ext_image_copy_capture_manager_v1
wp_presentation  wp_single_pixel_buffer_manager_v1  zwp_idle_inhibit_manager_v1
ext_idle_notifier_v1  ext_session_lock_manager_v1  xdg_activation_v1
ext_foreign_toplevel_list_v1  zwp_primary_selection_device_manager_v1
zwp_keyboard_shortcuts_inhibit_manager_v1  wl_seat  zwp_input_method_manager_v2
zwp_text_input_manager_v3  zwp_virtual_keyboard_manager_v1  wl_data_device_manager
zwlr_data_control_manager_v1  zwp_linux_dmabuf_v1  wl_output
```

`wl_drm` is **not** in the list.  The client (epitaph) binds
`zwp_linux_dmabuf_v1`, immediately destroys the probe, then binds `wl_shm`,
creates a pool and a `wl_buffer`, i.e. it presents through `wl_shm`.

### 1b. The EGL loader's decision

`EGL_LOG_LEVEL=debug MESA_DEBUG=1`, `timeout 12 epitaph`:

| run | env | first driver line | renderer |
|---|---|---|---|
| A | `MESA_LOADER_DRIVER_OVERRIDE=pvr` | `MESA-LOADER: dlopen(…/swrast_dri.so)` | `softpipe` |
| B | `env -u MESA_LOADER_DRIVER_OVERRIDE` | `MESA-LOADER: dlopen(…/swrast_dri.so)` | `softpipe` |

There is no attempt to dlopen `pvr_dri.so` in either run; the override changes
nothing.  Logs: `/tmp/wp-pvr.log`, `/tmp/wp-ctrl.log` (tmpfs, session-local).

### 1c. The working platforms, same libraries

| probe | platform | result |
|---|---|---|
| `gbmtest2.py /dev/dri/renderD129` | `EGL_PLATFORM_GBM_MESA` | `gbm backend : drm`, `EGL 1.4`, **12 window configs**, context current, **`PowerVR SGX 540`**, `EGL_MESA_image_dma_buf_export` true — with **and** without the override |
| `gbmtest2.py /dev/dri/card1` | GBM | same, **15** window configs |
| `egltest.py` | surfaceless | `PowerVR SGX 540`, `OpenGL ES 2.0 build 1.17@4948957` |

So the driver and the kernel side are healthy; the platform is the only
difference.

## 2. The code path (Mesa 21.3.9, the exact Amber version on the unit)

Fetched from `archive.mesa3d.org/older-versions/21.x/mesa-21.3.9.tar.xz` and read
(this sidesteps the freedesktop GitLab bot wall).

`src/egl/drivers/dri2/platform_wayland.c`:

```c
EGLBoolean
dri2_initialize_wayland(_EGLDisplay *disp)          /* line 2152 */
{
   if (disp->Options.ForceSoftware)
      return dri2_initialize_wayland_swrast(disp);
   else
      return dri2_initialize_wayland_drm(disp);
}
```

```c
   wl_registry_add_listener(dri2_dpy->wl_registry,
                            &registry_listener_drm, dri2_dpy);
   if (roundtrip(dri2_dpy) < 0 || dri2_dpy->wl_drm == NULL)   /* line 1567 */
      goto cleanup;
   ...
   dri2_dpy->driver_name = loader_get_driver_for_fd(dri2_dpy->fd);  /* 1603 */
```

The registry listener binds `wl_drm` only if the compositor advertises it
(`strcmp(interface, "wl_drm") == 0`, line ~1345) and gets the DRM device path
from its `device` event.  With no `wl_drm`, the init fails.  `ForceSoftware`
cannot come from the environment (`LIBGL_ALWAYS_SOFTWARE` was unset) — it comes
from the retry in `src/egl/main/eglapi.c`:

```c
      disp->Options.ForceSoftware =
         env_var_as_boolean("LIBGL_ALWAYS_SOFTWARE", false);   /* line 627 */
      ...
      if (!_eglDriver.Initialize(disp)) {
         if (disp->Options.ForceSoftware)
            RETURN_EGL_ERROR(disp, EGL_NOT_INITIALIZED, EGL_FALSE);
         else {
            disp->Options.ForceSoftware = EGL_TRUE;             /* ~line 640 */
            if (!_eglDriver.Initialize(disp))
               RETURN_EGL_ERROR(disp, EGL_NOT_INITIALIZED, EGL_FALSE);
         }
      }
```

The swrast initializer hard-codes the driver:

```c
   dri2_dpy->driver_name = strdup("swrast");                    /* line 2117 */
   if (!dri2_load_driver_swrast(disp))
```

and `dri2_load_driver_swrast()` calls `dri2_load_driver_common()`, which dlopens
`swrast_dri.so`.  That is the line the log shows first.

Finally, `src/loader/loader.c` proves the override *would* work on the fd path:

```c
char *
loader_get_driver_for_fd(int fd)
{
   ...
   if (geteuid() == getuid()) {
      driver = getenv("MESA_LOADER_DRIVER_OVERRIDE");   /* line 478 */
      if (driver)
         return strdup(driver);
   }
   ...
```

So once `wl_drm` exists, `dri2_initialize_wayland_drm()` opens the node, and
`loader_get_driver_for_fd()` returns `"pvr"` verbatim (or, with the override
unset, the DRM-name mapping).

## 3. Which compositors can satisfy this

| compositor | stack | serves `wl_drm`? | pvrports support |
|---|---|---|---|
| **catacomb** (ours) | Smithay | **no** (measured, §1a) | no — not a supported UI |
| **phoc / Phosh** | wlroots 0.19 (`types/wlr_drm.c`) | **yes** — `wl_global_create(…, &wl_drm_interface, …)`, `drm_handle_device`, `create_buffer`, `create_prime_buffer`, `authenticate`, `WL_DRM_CAPABILITY_PRIME` | **yes, the supported config** |
| KWin / Plasma-Mobile | KWin | yes (Wayland DRM path) | yes (README: "no extra packages") |
| Mutter / GNOME-Mobile | Mutter | yes | yes (README: "no extra packages") |

wlroots 0.19.3's `types/wlr_drm.c` (Debian source tree,
`wlroots/0.19.3-1/types/wlr_drm.c`) registers the global and sends the DRM node
name and PRIME capability.  That matches pvrports' statement that its supported
compositors work and catacomb is not among them.

## 4. What this changes

* **Option A ("make catacomb use pvr") has a precise, non-trivial requirement:**
  serve `wl_drm` (device + authenticate + `create_buffer` + `create_prime_buffer`)
  from Smithay, or provide a pvr-specific Wayland EGL platform.  It is code, and
  it is the largest of the routes.  No Mesa patch is needed — the rejection is
  above Mesa's driver selection.
* **Option B (phoc/wlroots) is more valuable than "compositor only".**  Because
  phoc advertises `wl_drm`, Mesa's Wayland EGL platform should now load
  `pvr_dri` for **ordinary Wayland clients** too.  The prediction is testable in
  one boot: launch `epitaph` under phoc and read its renderer / `maps`.
* **The GBM path remains proven** (report 267 and §1c), so a GBM compositor is
  safe regardless.

**Predicted pass criteria for the phoc test:** phoc starts on `renderD129`;
its own renderer reports `pvr_dri`/`PowerVR SGX 540`; a client launched under it
(epitaph) reports `PowerVR SGX 540` and maps `pvr_dri.so`; the panel shows the
frame; no GPU hang in `dmesg`.  If the client still maps `swrast_dri`, capture
`WAYLAND_DEBUG=1` and confirm `wl_drm` is now present before drawing further
conclusions.

## 4a. Step-2 control: stock phoc (wlroots) reaches the SGX, then needs the GLES2 patches

To test the prediction early, **stock phoc 0.59** from the pmOS/alpine repo was
installed on hd3 (7 packages, no core upgrades) and run through tinydm as the
session (`/usr/share/wayland-sessions/phoc-test.desktop`, `phoc --no-xwayland
--socket wayland-phoc -v`).  It was then reverted to catacomb.

What its log shows (`/home/playbook/.local/state/tinydm.log`):

```
[libseat] Seat opened with backend 'logind'
[backend/backend.c] Found 1 GPUs
[backend/drm/backend.c] Initializing DRM backend for /dev/dri/card0 (omapdrm)
[render/egl.c] EGL vendor: Mesa Project (Amber)
[render/gles2/renderer.c] Creating GLES2 renderer
[render/gles2/renderer.c] Using OpenGL ES 2.0 build 1.17@4948957
[render/gles2/renderer.c] GL vendor: Imagination Technologies
[render/gles2/renderer.c] GL renderer: PowerVR SGX 540
CRITICAL: GL_EXT_unpack_subimage not supported
CRITICAL: Failed to create GLES2 renderer
...
phoc-server-CRITICAL: Failed to create server: Could not create renderer
```

So a **real wlroots compositor splits the job correctly on this device**: KMS
on `card0` (omapdrm) and GLES2 rendering on the SGX (`PowerVR SGX 540`).  That
is the GBM/DRM path working end-to-end at the compositor.  Stock phoc then dies
because its GLES2 renderer *requires* `GL_EXT_unpack_subimage`, which the SGX
does not expose — precisely what pvrports'
`render-gles2-Properly-handle-GL_EXT_unpack_subimage.patch` (and the
`EXT_read_format_bgra` quirk) fixes.  It exited before creating its globals, so
this run does **not** yet test the client `wl_drm` path; the pvrports-patched
phoc is required for that.

This is also the clearest independent confirmation of report 268 §2's claim that
pvrports' supported configuration is the right one: the compositor path is
sound and the missing piece is a patched renderer, not the driver.

## 4b. Step-2 result: with pvrports' phoc, a Wayland client gets the SGX

The pvrports-patched phoc was built from `local/work/nura/pmaports/main/phoc`
(`pkgrel=9992`, phoc 0.52.rc1 with wlroots 0.19 embedded) with the local
pmbootstrap, installed over the stock package, and run through tinydm.  Its own
renderer is the SGX:

```
[render/gles2/renderer.c] GL vendor: Imagination Technologies
[render/gles2/renderer.c] GL renderer: PowerVR SGX 540
[render/gles2/renderer.c] Using OpenGL ES 2.0 build 1.17@4948957
Running compositor on wayland display 'wayland-phoc'
[backend/drm/drm.c] 'LVDS-1' connected · 1024x600 @ 60.000 Hz
```

**It advertises `wl_drm`** (the thing catacomb lacks):

```
wl_registry#2.global(2, "wl_drm", 2)
```

A minimal Wayland-EGL probe (`local/work/waylandtest.py`: `wl_display_connect`,
`eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, <wl_display>)`, a surfaceless
context) then gives:

| env | `ES2 configs` | `GL_RENDERER` | mapped DRI |
|---|---|---|---|
| `MESA_LOADER_DRIVER_OVERRIDE=pvr` | **12** | **`PowerVR SGX 540`** (`GL_VERSION` build 1.17@4948957) | **`pvr_dri`** |
| override unset | 15 | `softpipe` (`Mesa/X.org`) | `swrast_dri` |

So the chain is proven end-to-end: **`wl_drm` → `loader_get_driver_for_fd()` →
`pvr` → `pvr_dri` → SGX**, on the **wayland** platform, for an ordinary client.
The override is required (the DRM-name heuristic does not pick `pvr` on its
own), which is already what `/usr/share/tinydm/env-wayland.d/60-pvr.sh` and
`/etc/profile.d/mesa.sh` export.  `EGL_MESA_image_dma_buf_export` is present on
the pvr path.

**Consequences.**

* **The compositor is no longer the only GPU consumer; clients can be too.**
  This is the operator's goal (report 268) demonstrated at the client level.
* **The desktop must be a `wl_drm`-serving compositor** for this to hold —
  pvrports' supported config is Phosh/phoc (or Plasma-Mobile/KWin, GNOME/Mutter).
  catacomb cannot deliver it without implementing `wl_drm` in Smithay.
* The patched phoc is a *compositor*: with no shell attached it modesets a
  blank output.  The usable UI needs Phosh (pvrports' supported shell) or
  another shell; that is the next decision, not part of this proof.

## 4c. The chosen fix: a PRIME-only `wl_drm` in catacomb (implementation)

Chosen over a desktop migration (the operator rejected Phosh on a 1 GB
OMAP4430).  It keeps catacomb's lightweight DE and makes **every unmodified
Mesa client** — Qt, GTK, WPE WebKit — a GPU client, because they all go through
Mesa's Wayland EGL platform.

**What the patch adds** (`rootfs/catacomb/wl-drm-prime.patch`, tracked, with a
README; the pmaports copy is `local/work/nura/pmaports/main/catacomb/wl-drm-prime.patch`;
432 lines, applies cleanly after `dont-depend-on-systemd.patch`):

* `protocols/wayland-drm.xml` — the legacy protocol (from Mesa 21.3.9);
  `src/protocols/wl_drm.rs` generates the server bindings with
  `wayland_scanner::generate_server_code!`.
* `Cargo.toml`/`Cargo.lock` gain `wayland-server`/`wayland-backend`/
  `wayland-scanner` (same versions Smithay already resolves).
* `src/udev.rs` registers the global right after `dmabuf_node` is known.
* The global sends `device(<render node>)`, `format(ARGB8888)`,
  `format(XRGB8888)`, `capabilities(PRIME)`; `authenticate` is acknowledged
  (Mesa skips it for a render node and marks itself authenticated);
  `create_prime_buffer` builds a `Dmabuf` and `data_init.init`s a `wl_buffer`
  with that `Dmabuf` as user data.  `import_buffer()`/`buffer_type()` already
  treat that exactly like a `zwp_linux_dmabuf_v1` buffer, so the existing
  rendering path imports it.  The legacy GEM-name requests are rejected
  (`invalid_name`) — Mesa uses PRIME because the capability is advertised.
* No Smithay change: the buffer import machinery is reused as-is.

The recipe is `catacomb` `pkgrel=10002`; the APKBUILD applies the patch before
`cargo fetch --locked`.  It compiles natively (`cargo check`, clean) and the
patch is verified against pristine 1.5.0 in the real build order.  **Bench
validation in §4e.**

## 4d. The long-term open stack removes the requirement; the closed one does not

Checked against a current Mesa (`mesa-26.2.5`, the version the
`The1TrueJoe/powervr_sgx` stack patches): `dri2_initialize_wayland_drm_extensions()`
first asks `zwp_linux_dmabuf_v1` for **default dmabuf feedback**, and its
`main_device` handler maps the `dev_t` to a render node with
`loader_get_render_node()`, opens it, and sets `fd_render_gpu` +
`authenticated = true`
(`src/egl/drivers/dri2/platform_wayland.c:2167` and `:2629`).  `wl_drm` is only
the **fallback** when no render node came back (`:2655`).

So:

* **Closed stack (Mesa Amber 21.3 + `pvr_dri`):** the Wayland EGL platform has
  no dmabuf-feedback path; `wl_drm` is mandatory.  A Qt Wayland *client* under
  catacomb is software, and a Qt Wayland *compositor* does not serve `wl_drm`
  either, so its clients would be software too.  This is above the DRI driver —
  changing compositor or toolkit does not avoid it.
* **Open stack (`powervr_sgx` + a current Mesa):** the render node arrives via
  dmabuf feedback, so **catacomb's existing `zwp_linux_dmabuf_v1` (v5) could
  satisfy Mesa without `wl_drm`** — *if* Smithay's default feedback carries the
  pvr render node's `dev_t`.  That makes the open stack the structural fix; it
  needs the SGX540 core port (source D, a project).

For the current stack the practical lightweight choices are therefore: serve
`wl_drm` ourselves (in catacomb/Smithay) or use a small wlroots compositor
(which serves it) — not a different toolkit.

## 4e. Bench — the driver selection works; the client buffers do NOT present

`catacomb` **1.5.0-r10002** (APK sha256 `5fdf7a1c…`, 2,079,579 B) was built
with the patch, installed over the live root (upgrade from r10000) and the
session restarted (`rc-service tinydm restart`).

> **Correction (same session, after a visual check).**  The measurements below
> are real, but the clients are **not visible**: a `grim` capture shows only
> the compositor's own chrome (the gesture bar), and the operator saw a black
> screen.  The clients *select* the SGX; their **buffers do not present** under
> catacomb.  The safe, displaying state is the compositor on the SGX with the
> clients on `wl_shm` (§4f).  Do not read the renderer/`maps` lines below as
> "the UI is on the GPU".

* The session logs `Wayland socket: wayland-1`, and `WAYLAND_DEBUG=1` shows
  **`global(32, "wl_drm", 2)`** — catacomb now serves it.
* `waylandtest.py` under catacomb (`WAYLAND_DISPLAY=wayland-1`):

  | env | ES2 configs | `GL_RENDERER` | DRI mapped |
  |---|---|---|---|
  | `MESA_LOADER_DRIVER_OVERRIDE=pvr` | 12 | **`PowerVR SGX 540`** build 1.17@4948957 | **`pvr_dri`** |
  | override unset | 10 | `softpipe` | `swrast_dri` |

  The override is still needed (the DRM-name heuristic does not map
  `pvrsrvkm` to `pvr`); the session exports it from
  `/usr/share/tinydm/env-wayland.d/60-pvr.sh`, so desktop-launched clients
  inherit it.
* **`epitaph` (unmodified) logs `GL renderer "PowerVR SGX 540"`** — the panel
  client that used to report `softpipe` is now on the GPU.
* Per-process DRI maps after the restart: **`catacomb` → `pvr_dri`,
  `tabula` → `pvr_dri`, `epitaph` → `pvr_dri`**, `wvkbd` → none (cairo/shm, as
  expected).  No `Buffer import failed`/render errors in the session log, and
  no new GPU messages in `dmesg`.

## 4f. Why the clients are still invisible (trace + safe state)

Restarting the session with `RUST_LOG=smithay=trace` shows the client buffers
and the import:

* The EGL display is `native="/dev/dri/card0"`, `platform=PLATFORM_GBM_KHR`,
  version 1.4; its dmabuf **import/render** formats are AB30/AR24/AB24/XR24/…
  all with `modifier: Invalid`.
* The client buffers are `DrmFourcc(AR24)`, 1024x20 (bar) and 1024x585
  (wallpaper), **`stride: 4096` (linear)**, `modifier: Invalid`,
  `node: Mutex(None)`.
* `import_dmabuf` returns success and **reuses** a texture (no error); the
  compositor's log has no `Buffer import failed`/`unable to import buffer`.

So the texture is created, yet the capture is black.  A `wl_shm` client (the
override unset) **does** display, which rules out the layer/surface placement
and the compositor's own rendering.  The remaining possibilities are that the
**client's GPU render does not land in the shared linear buffer** (the pvr
driver renders elsewhere/into a shadow) or that the **compositor's pvr texture
sample of it is black**.  Not yet separated; the next experiments are:

1. a **presenting pvr client under a wlroots compositor** (to decide
   client-side vs compositor-side);
2. the **wl_drm `create_buffer` (GEM name)** path (we currently advertise PRIME
   only and reject GEM);
3. a compositor-owned **copy** of the client buffer (like the shm path).

**Safe state now (operational):** `catacomb` keeps the SGX (it maps `pvr_dri`),
and `/etc/catacomb/initrc` **unsets `MESA_LOADER_DRIVER_OVERRIDE` for its
children**, so the clients use `wl_shm`/softpipe and the desktop **displays**
(wallpaper, status bar, keyboard; verified with `grim`).  Backup:
`/etc/catacomb/initrc.session68-pvr`.  The `wl_drm` global stays advertised;
it is simply not used by the clients in this state.

**Not yet exercised:** a heavy compositing workload and frame latency (they
presuppose a working client buffer path).

## 4g. ROOT CAUSE (session 69): the imported texture has `REPEAT` wrap, and the buffers are NPOT

**One sentence.** Smithay's `GlesRenderer::import_egl_image()` binds the
imported dma-buf to a GL texture and never sets `GL_TEXTURE_WRAP_S/T`; the
default is `GL_REPEAT`, and in **GLES 2.0 a non-power-of-two texture with
`REPEAT` is incomplete — sampling it returns opaque black**.  Client buffers
are NPOT (1024x20, 1024x585, 640x200, …).  wlroots sets `CLAMP_TO_EDGE` on the
same textures (`render/gles2/texture.c`), which is why the identical client
presents under phoc.

### The measurements that pin it

All on the unit, `7.3.3 #109 SMP`, pvr amber Mesa 21.3.9, SGX540.

1. **A presenting client of our own** (`pb-egltest`, `tools/bench/egl/`):
   under catacomb it runs as `PowerVR SGX 540` (`pvr_dri`) and its
   **pre-swap `glReadPixels` of its own drawable is non-black**
   (`first=(255,0,255,255)`, `nz=64/64`), i.e. the client renders into exactly
   the image Mesa hands over; the surface on screen is black.
2. **A CPU-filled `wl_drm` buffer** (`pb-layerdrm`, an omapdrm dumb buffer
   filled with a red/green checkerboard and attached through
   `wl_drm.create_prime_buffer`) is black under catacomb; **the same pixels via
   `wl_shm` display correctly** in the same layer position.  So the layer
   geometry, crop and compositor draw are all fine; the dma-buf texture is not.
3. **Reproduced outside the compositor** (`pb-gbmimport`/`pb-gbmsample`,
   `tools/bench/egl/`): a program that recreates catacomb's renderer exactly —
   `dlopen`'d `libEGL.so.1`, a **GBM display on `/dev/dri/card0`**, a config and
   context, surfaceless `eglMakeCurrent` — imports the dumb buffer with
   `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)`, binds it with
   `glEGLImageTargetTexture2DOES` and samples it in a shader.  The import
   returns an image and reports **no GL error**; the FBO is complete; the draw
   reports no error; and the sampled row is
   **`first=(0,0,0,255) nz=0` — opaque black**.
4. **The A/B that names the parameter.**  The same program with only
   `GL_TEXTURE_WRAP_S/T = GL_CLAMP_TO_EDGE` added samples the buffer's real
   pixels (`first=(0,255,0,255) nz=64/64`).  External-target sampling
   (`GL_TEXTURE_EXTERNAL_OES`) behaves the same way.  Nothing else changed.
5. **The contrast on the panel.**  The operator saw the `pb-egltest` colours
   flash on the panel **under phoc** (wlroots) — the same client, the same
   buffer class, the same driver and the same forced `MESA_LOADER_DRIVER_OVERRIDE=pvr`.

An earlier hypothesis — that the pvr driver cannot import a foreign dma-buf —
is **withdrawn**: the import works; the only defect is the texture wrap mode.
(A direct-linked-EGL test binary also segfaulted on `eglMakeCurrent` and read
back black; that was an artefact of linking `libEGL.so.1` at load time instead
of `dlopen`ing it as the compositors do — see the `tools/bench/egl` README.)

### The fix

Set `GL_TEXTURE_WRAP_S/T = GL_CLAMP_TO_EDGE` in `import_egl_image()` — the
shm and surface import paths in the same file already do this
(`gles/mod.rs:940/1059/1132`); only the EGLImage path omitted it.

* Tracked patch: **`rootfs/catacomb/smithay-eglimage-clamp.patch`** (adds the
  two `TexParameteri` calls with a comment explaining the GLES2 rule).
* Packaged as catacomb **`pkgrel=10003`**: the APKBUILD applies it to the
  fetched Smithay checkout after the two existing Smithay patches (verified in
  that order against the pinned revision `dba255c`, hunk applies at +266 lines).
* This is a Smithay bug, not a pvr one: any GLES2 renderer that imports NPOT
  dma-bufs and leaves the wrap at `REPEAT` hits it.  Worth upstreaming to
  Smithay (the same two lines).

### What is still open after the fix

The build is the remaining step (it needs the root-owned pmbootstrap chroot);
once installed, the acceptance test is: `epitaph`/`tabula` render on the SGX
**and** a heavy redraw is visible on the panel.  If the frames still do not
appear, the next instrument is the one-shot FBO readback in
`DmabufHandler::dmabuf_imported` described in HANDOFF §0f — but the NPOT wrap
result explains the observed black exactly, so it should not be needed.

## 4h. After the clamp fix: the panel's modules were still missing — the client hard-codes GL locations

With the clamp fix installed (r10003) the **wallpaper, keyboard and the bar's
background** all appear on the SGX.  The bar's modules (clock text, icons) did
not.  A one-shot framebuffer summary inside the client (`debug-framebuffer.patch`,
r10007) reported the whole 1024x20 bar as **one colour**, `(24,24,24)`, with
`glerr = 1282` (`GL_INVALID_OPERATION`) — so the client itself drew nothing but
its background.

**Cause: epitaph assumed the linker's attribute and uniform indices.**
`src/renderer.rs` passes `0/1/2` to `glVertexAttribPointer`/`EnableVertexAttribArray`
and writes the projection with `glUniform4f(0, …)`.  GLES2 leaves those
indices to the linker, and the PowerVR compiler picks different ones from Mesa's
software rasterizer.  Measured on the unit by linking epitaph's own text shaders:

| symbol | pvr (SGX540) |
|---|---|
| `a_Position` | **2** |
| `a_UV` | **1** |
| `a_Flags` | **0** |
| `u_Projection` | **1** |
| `u_Texture` | **2** |

So under `pvr_dri` the glyph positions were fed from the wrong attribute, the
projection uniform was never written, and `glUniform4f(0, …)` (the sampler's
location) returned `GL_INVALID_OPERATION` — exactly the code the client logged.
Under softpipe the linker happened to assign `0/1/2` in declaration order, which
is why the panel worked before and why the client was never fixed upstream.

**Fix (`bind-attribs.patch`, epitaph `pkgrel=10008`):** `glBindAttribLocation`
for `a_Position`/`a_UV`/`a_Flags` (text) and `aPos`/`aColor` (rect) before
`glLinkProgram`, and `glGetUniformLocation(…, "u_Projection")` instead of the
literal `0`.  This is a general portability defect, not an SGX one — worth
upstreaming.

**Also measured (unchanged, and separate):** a client that keeps re-rendering
fails **intermittently** on its second buffer with
`CreateImageShared: Failed to create buffer` /
`PVRDRIBufferCreate: Failed to create plane` (3 of 5 runs).  Under phoc the same
client swaps indefinitely.  Cause not established; it is a pvr allocation
defect in `e[gl]SwapBuffers` after the first frame, and it affects any client
that animates.  Recorded for the next session.

## 4i. VALIDATED ON THE PANEL (operator, session 69)

With catacomb r10003 (clamp) and epitaph r10008 (bound locations) installed and
the session in the pvr client state, the operator confirms on the unit:

* **the wallpaper renders** (a pvr-rendered, textured 1024x585 client),
* **the keyboard renders**,
* **the top bar renders with its text and icons**, and
* **the bar toggles and the UI is responsive** — "it's finally responsive, like
  it should be".

Measured at the same time (epitaph's own draw timing, `render=`):

| frame | softpipe (session 60) | SGX (session 69) |
|---|---|---|
| 1024x20 bar, first draw | ~1.6–2.3 s for the drawer | 170–280 ms |
| 1024x20 bar, steady state | — | **1.5–30 ms** |
| 1024x585 drawer | 420–550 ms (llvmpipe swap) | 97 ms |

So the client path is on the GPU and the interactive latency that llvmpipe
could not fix (the deferred `swap`) is gone.  The safe `wl_shm` state is no
longer needed: **the desktop runs with the clients on the SGX.**

Open, found on the way and unrelated to the two fixes: a client that keeps
re-animating fails on its **second** buffer in 3 of 5 runs
(`CreateImageShared: Failed to create buffer`,
`PVRDRIBufferCreate: Failed to create plane`), never under phoc — trap 349.
It is worth fixing before any animated client is judged, but it does not
affect the static desktop.

## 5. Not yet proven / caveats

* **Driver selection works; presentation was black — root-caused and fixed in
  source (§4g).**  The clients map `pvr_dri`, the dmabuf import succeeds, but
  the imported texture sampled opaque black: it was bound without
  `GL_TEXTURE_WRAP_S/T = CLAMP_TO_EDGE`, and the NPOT client buffers are
  incomplete GLES2 textures with the default `REPEAT`.  The fix is
  `rootfs/catacomb/smithay-eglimage-clamp.patch` (catacomb `pkgrel=10003`);
  the remaining step is the root-owned build.  Until it is installed, the safe
  operational state keeps the compositor on the SGX and the clients on
  `wl_shm`.
* **The override is required.**  `MESA_LOADER_DRIVER_OVERRIDE=pvr` is what makes
  `loader_get_driver_for_fd()` return `pvr`; the session
  (`/usr/share/tinydm/env-wayland.d/60-pvr.sh`) and login shells
  (`/etc/profile.d/mesa.sh`) export it.  A client started without it falls back
  to swrast.  A durable alternative would be a Mesa dri-config mapping or the
  open stack.
* **The live root, not the baked image, carries the fix.**  catacomb
  `1.5.0-r10002` was installed by hand; the hd3 base image still has r10000, and
  a reflash reverts it (the trap-343 class).  The next hd3 rebake must include
  it.
* **The wlroots/phoc route is proven but not chosen.**  pvrports' phoc runs on
  the SGX and serves `wl_drm`; Phosh is too heavy for 1 GB (operator decision).
  The patched phoc 0.52.rc1 and its `phoc-test.desktop` are installed but
  unused.
* **Smithay itself implements no `wl_drm`**; the global lives in catacomb
  (`src/protocols/wl_drm.rs`).  Upstreaming it would mean a small Smithay
  module reusing the same dmabuf machinery.
* **Long term:** the fully open `powervr_sgx` stack with a current Mesa needs no
  `wl_drm` (dmabuf feedback), so catacomb would work unchanged; SGX540 support
  is the project (report 268 source D, ROADMAP item 7a).

## 6. Artefacts

* Probes, now tracked: **`tools/bench/waylandtest.py`** (Wayland-EGL; the
  decisive one), **`tools/bench/gbmtest2.py`** (GBM) and
  `tools/bench/egltest.py` (surfaceless).  Copies were pushed to `/tmp` on the
  unit.
* The catacomb patch: **`rootfs/catacomb/wl-drm-prime.patch`** (+ its README),
  432 lines, applied by `pkgrel=10002`; the source used for the native compile
  is `local/work/src/catacomb-150/`.
* Built package: `local/work/nura/work/packages/edge/armv7/catacomb-1.5.0-r10002.apk`
  (sha256 `5fdf7a1c…`).
* `/tmp/wp-pvr.log`, `/tmp/wp-ctrl.log`, `/tmp/wl-debug.log` on the unit (tmpfs).
* Note: `/etc/profile.d/mesa.sh` exports `MESA_LOADER_DRIVER_OVERRIDE=pvr` into
  login shells; a plain SSH `sh -c` does not inherit it.
* The complete globals list and the EGL debug lines are quoted in §1.
