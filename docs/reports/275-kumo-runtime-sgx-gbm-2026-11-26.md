# Report 275 — Kumo/WPE WebKit does not render on the SGX: the pvr GBM cannot export dmabufs

Date: 2026-11-26 (session 72, Linux-era 66).  Bench + host.  Answers HANDOFF-71
§0a, session 72's one experiment: **Kumo on the panel**.

## 0. Headline

**Kumo runs and loads the page, the WebKit engine is on the SGX, but the page
never composes — and the root cause is a hard driver-capability gap, proven by a
minimal probe: the PowerVR GBM backend cannot export a dmabuf.**

* Kumo opens its window, draws its URL bar, fetches the page, and spawns
  `WPEWebProcess`/`WPENetworkProcess`; **both `kumo` and `WPEWebProcess` map
  `pvr_dri.so` + `libGLESv2_PVR_MESA`** — the engine really is on the SGX, not
  `swrast`.  The failure is **not** the missing-helper, `swrast`, or
  display/view-creation branches of the handoff.
* The web content area stays **gray** because WebKit's accelerated surface makes
  a GBM buffer and then cannot get its dmabuf attributes:
  `AcceleratedSurface::RenderTargetEGLImage::create()` →
  `DMABufBufferAttributes::fromGBMBufferObject()` →
  `gbm_bo_get_fd_for_plane()` returns **-1** → `Failed to extract DMA-buf
  attributes from GBM buffer of size 1024x515` →
  `Failed to create GBM buffer`.
* **Proven directly** with `local/work/gbmplane.py` (a ctypes probe that calls
  the exact functions): on `/dev/dri/renderD129` (pvr), `gbm_bo_create(AR24,
  …, GBM_BO_USE_RENDERING)` succeeds with `plane_count=1`, but **both
  `gbm_bo_get_fd_for_plane(bo,0)` and `gbm_bo_get_fd(bo)` return -1**; on
  `/dev/dri/card1` (pvr primary) `gbm_bo_create()` itself **fails**
  (`PVRDRIPlaneCreate: Cannot create display buffer without a display device`).
  The omapdrm nodes (`card0`, `renderD128`) export fine (`fd_for_plane0=6/7`).
* **The desktop works around this**: libdbm / the pvr clients allocate DRM dumb
  buffers and export them with **PRIME handles** (`drmPrimeHandleToFD`), not with
  the GBM plane API, so `epitaph`/`tabula`/`catacomb` never touch the broken
  path.  WPE WebKit 2.54's `AcceleratedSurface` **requires** the GBM plane API.

**No `WPE_*` environment variable fixes it.**  `WPE_DRM_DEVICE` changes which
node is used (and the pvr node makes it worse — garbage handles);
`WPE_BUFFER_FORMAT` can force a linear `AR24` buffer (which does remove the pvr
handle errors) but the extraction still calls `gbm_bo_get_plane_count()` /
`gbm_bo_get_fd_for_plane()`, which the pvr backend cannot satisfy.

## 1. What was measured

Bench: Linux `7.3.3 #109 SMP`, hd3 root, catacomb `r10003`, the two packages
installed (`wpewebkit-2.54.3-r1000`, `kumo-1.10.0-r1000`).  Rebooted first as the
handoff says (the 5 h GEM was 84 objects / 174 493 696 B / 25.6 MiB pinned); a
fresh boot is **6 objects / 9 932 800 B**.

### 1.1 Launch (run 1, the handoff's command)

```sh
su playbook -c '
  export XDG_RUNTIME_DIR=/run/user/10000 WAYLAND_DISPLAY=wayland-1
  export MESA_LOADER_DRIVER_OVERRIDE=pvr DBMFIX=1
  export LD_PRELOAD=/usr/local/lib/dbmfix.so
  export XDG_SESSION_TYPE=wayland
  export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/10000/bus
  export HOME=/home/playbook; cd /home/playbook
  nohup kumo https://example.com >/tmp/kumo.log 2>&1 </dev/null &'
```

(For later runs a host HTTP server on `169.254.0.2:8000` served a deterministic
page over the USB RNDIS link when Wi-Fi was down.)

* **Kumo starts**, and the page is **fetched**: the host server logged
  `GET /` twice plus a `favicon.ico` 404, and the window shows the URL bar.  So
  the packages, the libexec helpers (`/usr/libexec/wpe-webkit-2.0/`), the
  WPEPlatform ABI and the display/view creation are all **fine**.
* Processes: `kumo` (81 MiB RSS), `WPENetworkProcess` (31 MiB),
  `WPEWebProcess` (72 MiB).  `kumo` **and** `WPEWebProcess` map
  `pvr_dri.so`, `libGLESv2_PVR_MESA`, `libpvr_dri_support` — **the engine is on
  the SGX**.  `WPEWebProcess` holds fds to **both** `/dev/dri/renderD129` (pvr)
  and `/dev/dri/card0` (omapdrm).
* **The page area is gray** (operator).  dmesg at the first composite:
  `PVR_K:(Error): GetHandleStructure: Handle not allocated (index: 545)` +
  `PVRSRVLookupHandle: Error looking up handle (150)`, and the client logs
  `PVR:(Error): SGXKickSubmit: failed call to SGXDoKick (150)`.

### 1.2 Giving the engine the pvr node (run 2)

`WPE_DRM_DEVICE=/dev/dri/renderD129` (the WPE platform's device override, which
also sets the node handed to the web process) made it **worse**:

* two `WPEWebProcess`;
* `PVR_K:(Error): GetHandleStructure: Handle index out of range (6255103 >= 768)`
  and `(4294967295 >= 768)` — **garbage handle indices** (the kernel's handle
  table has 768 slots);
* `Failed to extract DMA-buf attributes from GBM buffer of size 360x590`;
* the panel **wedged** (compositor froze; 0 DSI errors), recovered by reboot.

### 1.3 Software in the helpers only (run 3, `local/work/softshim.c`)

A per-process `LD_PRELOAD` shim set `LIBGL_ALWAYS_SOFTWARE=1` (and later
`MESA_LOADER_DRIVER_OVERRIDE=swrast`) **only in the `WPE*` helpers**, so Kumo's
UI process kept pvr (and its DRM node path — see trap 365) while the web process
rendered in software.  The helpers logged `[softshim] WPEWebProcess -> software
GL`, but:

* `gbm: did not find extension DRI_DRI2 version 1` → `failed to bind extensions`
  → `failed to load driver: swrast` — **swrast cannot bind over GBM** on this
  Mesa;
* the pvr import still ran: `PVR:(Error): PVRRenderBufferInitFromFdNoOffset:
  Failed to import device memory for render buffer (PVRSRV_ERROR_BAD_MAPPING)` →
  `CreateImageShared: Failed to create buffer` →
  `Failed to create GBM buffer of size 1024x515`;
* GEM climbed to **109–113 MiB** (near the 128 MiB TILER aperture) — the run was
  cleared with a reboot before it exhausted the TILER.

### 1.4 Forced linear format on the pvr device (run 4)

`WPE_DRM_DEVICE=/dev/dri/card1 WPE_BUFFER_FORMAT=AR24:0:rendering`:

* **the pvr handle errors vanished** (only `PVR_K: UM DDK-(4948957) and KM
  DDK-(4948957) match. [ OK ]`), and GEM stayed at **8 objects / 10 342 400 B**;
* but the buffer handoff still failed:
  `Failed to extract DMA-buf attributes from GBM buffer of size 1024x255` (and
  `360x590`), plus `GLib-GObject-CRITICAL g_object_unref: assertion 'G_IS_OBJECT
  (object)' failed` in the `kumo` process;
* the page stayed **gray**.

So the linear format removes the pvr *render* errors but not the *export* failure
— which is exactly what the probe shows.

## 2. The root cause, proven with a probe

`local/work/gbmplane.py` (a ctypes probe; run as `playbook` with `python3`)
calls the same GBM functions as `DMABufBufferAttributes::fromGBMBufferObject()`
(`Source/WebCore/platform/graphics/DMABufBuffer.cpp:78`):

```
/dev/dri/renderD129 [rend|scan] -> gbm_bo_create FAILED
/dev/dri/renderD129 [rend] plane_count=1 fd_for_plane0=-1 gbm_bo_get_fd=-1 stride=256 ... modifier=0xffffffffffffff
/dev/dri/card1 [rend|scan] -> gbm_bo_create FAILED
/dev/dri/card1 [rend] -> gbm_bo_create FAILED
/dev/dri/card0 [rend|scan] plane_count=1 fd_for_plane0=6 gbm_bo_get_fd=7 stride=256 ...
/dev/dri/card0 [rend] plane_count=1 fd_for_plane0=7 gbm_bo_get_fd=8 stride=256 ...
/dev/dri/renderD128 [rend|scan] plane_count=1 fd_for_plane0=7 gbm_bo_get_fd=8 stride=256 ...
```

and the driver's own errors:

```
PVR:(Error): PVRDRIPlaneCreate: Cannot create display buffer without a display device [0, ]
PVR:(Error): PVRDRIBufferCreate: Failed to create plane [0, ]
```

`fromGBMBufferObject()` requires `gbm_bo_get_plane_count(bo) > 0` **and**
`gbm_bo_get_fd_for_plane(bo, i) >= 0` for every plane; the pvr backend satisfies
the first and **fails the second** (`-1`), so the function returns `nullopt` and
the surface is never created.  The display-buffer (`SCANOUT`) allocation on the
pvr node fails even earlier.

**The PRIME fallback is also impossible (measured).**  `tools/bench/gpu/gbmprime.c`
(`dlopen`, correct union handling) calls `gbm_bo_get_handle_for_plane()` +
`drmPrimeHandleToFD()` on the same nodes: on `renderD129` `gbm_bo_get_fd=-1`,
`gbm_bo_get_handle_for_plane(0)=0xFFFFFFFF`, and `drmPrimeHandleToFD` returns
`-1`; on `card1` `gbm_bo_create` returns NULL; `card0` works (`get_fd=6`,
`handle=1`, `prime=7`).  So the pvr GBM exposes **neither a fd nor a GEM
handle**, and a WebKit patch that fell back to PRIME could not help either — the
gap is inside the GBM/Mesa backend, not in WebKit.

### 2.1 Why the rest of the desktop works

libdbm (the pvr buffer allocator) describes a linear buffer with
`DRM_IOCTL_MODE_CREATE_DUMB` and exports it with **`drmPrimeHandleToFD`** (PRIME
handles), which the pvr kernel driver implements.  `catacomb`, `epitaph` and
`tabula` therefore never call the GBM plane API and display normally (report
269).  WPE WebKit 2.54's coordinated *GBM* accelerated surface does, and that is
the incompatibility.

### 2.2 The bounded DDK/GBM test (session 72, second pass)

`tools/bench/gpu/gbmprime2.c` (a `dlopen` C probe with `drmSetMaster`) and
`tools/bench/gpu/ioctllog.c` (an `ioctl` logger) answer *why* the export fails:

* on `renderD129` (RENDERING) the GBM BO is a **real DRI image** — the ioctl log
  has **no `CREATE_DUMB`** (`0xc02064b2`), so it is **not** the dumb fallback —
  and `plane_count = 1`.
* the pvr DDK returns **no GEM handle**: `gbm_bo_get_handle_for_plane(0) =
  0xFFFFFFFF`, and the standard `PRIME_HANDLE_TO_FD` ioctl (`0xc00c642d`) that
  the export path issues **fails** on it → `gbm_bo_get_fd = -1`.
* the pvr primary `card1` is **already DRM master** (`drmSetMaster` →
  `EBUSY`), so a display-node allocation could not be tested; the omapdrm
  control exports normally (`fd=6`, `handle=1`, `drmPrimeHandleToFD=8`).

So the DDK does not expose a GEM handle/fd for the buffers it allocates for
Mesa's GBM image path; only its **dumb/PRIME** path (which libdbm uses for the
compositor's own buffers) exports.  The `PVRDRIValidateUsage` render-node policy
is **not** the cause (image creation succeeded), and a Mesa-only patch cannot
invent a handle the DDK does not have.  The remaining Mesa-side option would be
to re-route the pvr GBM image allocation through the exportable dumb/PRIME path,
which is a substantial pvr-classic-driver change, not a one-liner.

## 3. Alternatives surveyed (for the graphics stack)

Checked against the current upstream state (not just report 268's snapshot):

| route | state | what it would fix |
|---|---|---|
| **`The1TrueJoe/powervr_sgx`** (fully open: DRM driver + open microkernel + Mesa gallium on **Mesa 26.2.5**, with EGL/GBM/GLX/DRI3) | **SGX545 supported** (Cedarview/CE5300/Clovertrail); **SGX530 in bring-up**; **SGX540 "Wanted"** — not supported yet | A real Mesa (26.x) GBM backend that exports dmabufs, plus the dmabuf-feedback Wayland path (no `wl_drm` needed, report 269 §4d).  The structural fix, but SGX540 is a porting project (their guide: a new core is "data, not code", still real work). |
| **`pvrports/pvrports`** (our closed DDK 1.17 + amber Mesa 21.3.9) | supported UIs are Phosh (their `phoc`) and Plasma-Mobile; the GBM backend here is the broken one | only if it (and the DDK) give the allocated buffer a GEM handle — the bounded test (§2.2) shows the DDK returns none on the render node |
| **`zsavajji/pvr_sgx_rm696`** (Nokia N9 SGX530, DDK 1.4.14.2514, Linux 6.18) | method reference; keeps the **closed** userspace, exports via omapfb/PRIME | nothing directly — same class of closed-DDK/GBM limitation, and SGX530 not SGX540 |
| WebKit **engine patch** to fall back to PRIME / `gbm_bo_get_fd` + `gbm_bo_get_stride`/offset(0) for single-plane buffers when `gbm_bo_get_fd_for_plane` fails | **ruled out by measurement** (`gbmprime.c`): the pvr GBM exposes neither a fd (`gbm_bo_get_fd=-1`) nor a GEM handle (`gbm_bo_get_handle_for_plane=0xFFFFFFFF`, `drmPrimeHandleToFD=-1`), so no WebKit-side export fallback can work | nothing — do not build this |

**Recommended order.**  The bounded test (§2.2) shows the **DDK is the wall**:
the allocated buffer has **no GEM handle**, so a Mesa-only "implement the fd
export" patch has nothing to export.  The realistic options are:

1. **Route the pvr GBM image allocation through the exportable dumb/PRIME path**
   (as libdbm does for the compositor's own buffers) — a substantial pvr
   classic-driver change, and it may not support WebKit's required formats.
   Worth a feasibility look, not a quick patch.
2. **`powervr_sgx`'s SGX540 port** (a real Mesa 26.x GBM) — the durable fix, but
   SGX540 is still "wanted" (SGX545 works, SGX530 bring-up), so a multi-session
   port.

The "cheapest WebKit patch" idea is **withdrawn** — there is no export path to
fall back to.

## 4. Boot incidents (and what they teach)

* **Stopping a running Kumo/WPE wedges the pvr driver.**  After killing Kumo
  mid-flight, dmesg showed `PVR_K:(Error): FreeResourceByPtr: ERROR calling
  FreeResource function`; the compositor stayed alive (all processes `S`,
  `wchan LinuxEventObjectWait`, touch IRQ 118 still ~160/s, **0 DSI errors**,
  panel `connected`, backlight 255) but the panel **froze** — new clients (e.g.
  `gorm`) never appeared.  A `reboot -f` cleared it.  **Do not leave Kumo running
  or kill it mid-render**; reboot instead.
* **The DSI `NO PACKET_SENT` wedge can follow** a GPU wedge plus a power-button
  press (the initrc binds the release to a DPMS toggle): a boot showed
  `Failed to set dsi_if_enable to 0` + `NO PACKET_SENT` on VC0/HS and VC1/LP at
  t=24.7 s (trap-353 class).  A **second** `reboot -f` cleared it; the following
  boot had 0 DSI errors.
* Kumo drives **GEM toward the TILER budget** (109–113 MiB of 128 MiB) on the
  runs that allocate many buffers — clear it with a reboot rather than letting
  it exhaust the window.

## 5. Kumo's own constraint (trap 365)

Kumo calls `egl_display.device().drm_device_node_path().expect(...)`
(`src/main.rs:385`) and requires a DRM node path.  On this stack **only the pvr
driver yields one**:

* no `MESA_LOADER_DRIVER_OVERRIDE` → Mesa resolves `omapdrm_dri.so` (absent) →
  EGL init fails → the device has no path → `panicked … DRM node has no path`;
* `LIBGL_ALWAYS_SOFTWARE=1` or `MESA_LOADER_DRIVER_OVERRIDE=swrast` → software
  EGL device, no DRM node → the same panic;
* `pvr` → Kumo starts (which is why every run used pvr).

So a software render needs a Kumo change (accept a missing/dummy node) **and** a
working software GBM (swrast lacks `DRI_DRI2` here) — two more reasons the current
stack cannot run Kumo either way.

## 6. What changes

* **Trap 365** (Kumo needs a DRM node path; software GL panics), **366** (the pvr
  GBM cannot export dmabufs; WebKit needs it), **367** (stopping Kumo wedges the
  pvr driver; reboot clears).
* **DECISIONS §107** records the finding and the fix direction.
* **ROADMAP 7a** (a0) records the result and the new blocker.
* The two packages are still on the **live root only**; the next hd3 rebake must
  add them (unchanged requirement) — but Kumo will not render until the GBM/PRIME
  gap is addressed.

## 7. Artefacts

* `tools/bench/gpu/gbmplane.py` — the ctypes probe (the decisive measurement;
  a working copy was `/tmp/gbmplane.py` on the unit and `local/work/gbmplane.py`
  on the host).
* `tools/bench/gpu/gbmprime.c` — the dlopen/C PRIME-handle probe that ruled out
  a WebKit-side export fallback.
* `tools/bench/gpu/gbmprime2.c` + `tools/bench/gpu/ioctllog.c` — the bounded
  DDK/GBM test: image-vs-dumb (no `CREATE_DUMB`) and the missing GEM handle.
* `tools/bench/gpu/softshim.c` — the per-process software-GL shim (helpers only;
  working copy `local/work/softshim.c`).
* Logs: `/tmp/kumo.log`, `/tmp/kumo2.log`, `/tmp/kumo_shim.log`,
  `/tmp/kumo_shim2.log`, `/tmp/kumo_fmt.log` on the unit (`/tmp` is on the ext4,
  trap 354); copies in the session scratchpad (`kumo-evidence.txt`,
  `kumo-shim2-evidence.txt`, `kumo-gbm-rootcause.txt`).
* Engine source read: `AcceleratedSurface.cpp:306` (the message),
  `DMABufBuffer.cpp:78` (`fromGBMBufferObject`), `WPEToplevel.cpp:622`
  (`WPE_BUFFER_FORMAT`), `WPEDisplay.cpp:690` (`WPE_DRM_DEVICE`).
* `docs/captures/` — no capture file added; the evidence is the logs above plus
  the probe output.

## 8. Bench at the close

Region 0 trailer `1f 2d c8 d7 13 d6 47 35` = **`0x3547D613`** (read this session;
**no media write**).  `BOOTCTR_FCTRY` **`0x28`** (`0x2f` at the start; the
experiment's reboots spent the rest, each read `≥ 0x1e, no write`).  `nproc` 2,
PID 1 mask 3; panel `connected`, backlight 255, **0 DSI errors** on the final
boot; Wi-Fi down (the 5 GHz SSID was not in the scan; `gorm` is the path).
