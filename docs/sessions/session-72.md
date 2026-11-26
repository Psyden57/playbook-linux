# Session 72 (absolute) — Kumo/WPE WebKit runs on the SGX but cannot present: the pvr GBM cannot export dmabufs

> Numbering: absolute `72` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era session 66**
> (`absolute = Linux-era + 6`).  Continues session 71, which built and installed
> the browser stack and left this as the one experiment.

Date: 2026-11-26.  Unit at the end: **Linux 7.3.3 `#109 SMP`** on hd3, region 0
**`0x3547D613`** (media trailer measured; **no media write this session**) and
region 1 sha256 **`20a5edd3a791…7422`** (re-measured this session),
`BOOTCTR_FCTRY` **`0x28`**, `nproc` 2 / PID 1 mask 3, the desktop up on
`pvr_dri`, **0 DSI errors**, GEM 9 objects / 17,121,280 B, `wlan0` **up**
(`192.168.0.232`), sshd up, **Kumo not running**.

## Goal of this session

HANDOFF-71 §0a: the one experiment — **Kumo on the panel**.  Reboot (clear the
5 h GEM), launch Kumo as `playbook` under catacomb with the session env, read the
logs/helpers/maps/RSS/GEM/dmesg, and ask the operator what is on the panel.

## Confirmed (measured)

### Kumo starts and the engine is on the SGX — but the page is gray

* **Kumo launches, draws its window and URL bar, and the page is fetched**
  (the host server logged `GET /` twice and a `favicon.ico` 404).  So the
  packages, `/usr/libexec/wpe-webkit-2.0/`, the WPEPlatform ABI and
  display/view creation are **all fine** — none of the handoff's failure
  branches (missing helper, `swrast`, display abort) applies.  [report 275 §1]
* **`kumo` and `WPEWebProcess` map `pvr_dri.so` + `libGLESv2_PVR_MESA`** — the
  engine *is* on the SGX; `WPEWebProcess` holds fds to `/dev/dri/renderD129`
  (pvr) **and** `/dev/dri/card0` (omapdrm).  [report 275 §1.1]
* **The page area stays gray** (operator).  dmesg at the first composite:
  `PVR_K: Handle not allocated (index: 545)` / `lookup handle (150)`, client
  `SGXKickSubmit: failed call to SGXDoKick (150)`.

### Root cause, proven: the pvr GBM cannot export dmabufs

* **`local/work/gbmplane.py`** (ctypes; calls the same functions as
  `DMABufBufferAttributes::fromGBMBufferObject()`):
  on `/dev/dri/renderD129` (pvr) `gbm_bo_create(AR24, …, GBM_BO_USE_RENDERING)`
  succeeds with `plane_count=1`, but **`gbm_bo_get_fd_for_plane(bo,0)` and
  `gbm_bo_get_fd(bo)` are both `-1`**; on `/dev/dri/card1` `gbm_bo_create()`
  itself fails (`PVRDRIPlaneCreate: Cannot create display buffer without a
  display device`); the omapdrm nodes export fine (`fd_for_plane0 = 6/7`).
  [report 275 §2]
* WebKit's `AcceleratedSurface::RenderTargetEGLImage::create()`
  (`Source/WebKit/WebProcess/WebPage/CoordinatedGraphics/AcceleratedSurface.cpp:306`)
  needs a non-negative plane fd, so `fromGBMBufferObject()` returns `nullopt`
  and it logs `Failed to extract DMA-buf attributes from GBM buffer of size
  1024x515` → no page surface.  [report 275 §2]
* **The desktop is unaffected** because libdbm/pvr clients allocate DRM dumb
  buffers and export **PRIME handles** (`drmPrimeHandleToFD`), never the GBM
  plane API.  [report 275 §2.1]

### The `WPE_*` levers tried, and why none fixes it

* `WPE_DRM_DEVICE=/dev/dri/renderD129` → garbage pvr handles
  (`6255103 >= 768`, `0xFFFFFFFF`) and a wedge.  [report 275 §1.2]
* A per-process `LD_PRELOAD` shim (`local/work/softshim.c`) forcing the `WPE*`
  helpers to software GL → `gbm: did not find extension DRI_DRI2` →
  `failed to load driver: swrast`; the pvr import still failed
  (`PVRRenderBufferInitFromFdNoOffset … BAD_MAPPING`); GEM hit 109–113 MiB.
  [report 275 §1.3]
* `WPE_DRM_DEVICE=/dev/dri/card1 WPE_BUFFER_FORMAT=AR24:0:rendering` → **the pvr
  render errors vanished** (only the DDK `[ OK ]` line; GEM 10 MB) but
  `fromGBMBufferObject()` still calls `gbm_bo_get_fd_for_plane()` → still gray,
  plus `GLib-GObject-CRITICAL g_object_unref` in `kumo`.  [report 275 §1.4]
* Kumo itself needs a **DRM node path** (`src/main.rs:385`) and only the pvr
  driver yields one; software GL (no override / `swrast` /
  `LIBGL_ALWAYS_SOFTWARE=1`) panics `DRM node has no path`.  [trap 365]

### Operational hazards (measured)

* **Stopping a running Kumo/WPE wedges the pvr driver**: `PVR_K:
  FreeResourceByPtr: ERROR calling FreeResource function`, and the panel
  freezes while the compositor/touch stay alive (**0 DSI errors**, panel
  `connected`, backlight 255).  A `reboot -f` cleared it.  [trap 367]
* A boot then showed the DSI `NO PACKET_SENT` wedge (`Failed to set
  dsi_if_enable to 0`; VC0/HS and VC1/LP) at t=24.7 s — likely the power-button
  release/DPMS binding; a **second** `reboot -f` cleared it and the next boot
  had 0 DSI errors.  [report 275 §4]
* Kumo drives GEM toward the **128 MiB TILER** (109–113 MiB observed); clear it
  with a reboot, don't let it exhaust.

## Alternatives surveyed (the operator asked)

Report 268's sources, checked against current upstream state (report 275 §3):

* **`The1TrueJoe/powervr_sgx`** — the fully open stack (DRM driver + open
  microkernel + Mesa gallium on **Mesa 26.2.5**, EGL/GBM/GLX/DRI3).  **SGX545 is
  supported** (Cedarview/CE5300/Clovertrail), **SGX530 is in bring-up**, and
  **SGX540 is "Wanted"** — not supported yet.  It is the structural fix (a real
  Mesa GBM backend + dmabuf feedback, no `wl_drm`), but SGX540 is a porting
  project.
* **`pvrports/pvrports`** — the source of our closed DDK 1.17 + amber Mesa
  21.3.9; supported UIs are Phosh/phoc and Plasma-Mobile, and its GBM backend is
  the broken one.  Only worth continuing if a newer pvrports Mesa exports
  dmabufs.
* **`zsavajji/pvr_sgx_rm696`** — the N9 SGX530 forward-port; a method reference
  (DT probe, runtime PM, the `vm_pgoff`/PFN fix) that keeps the closed
  userspace and exports via omapfb/PRIME.  Not a drop-in.

## The open stack route (scoped, report 276)

The operator asked whether a more complete stack would fix WebKit.  Scoped the
open `powervr_sgx` SGX540 port against the project's own porting guide, using
our vendored GPL DDK 1.17 headers (the "K" source):

* **SGX540 is USSE** (the supported SGX545's ISA family) and is "Wanted", but the
  DDK's `#if defined(SGX540)` block says **28-bit address space, single memory
  context, `MULTI_EVENT_KICK`, 4 USE pipes, no `USE_CODE_BASE`**; `CORE_ID`
  0x20 / `CORE_REVISION` 0x24.
* The open driver's core check requires **≥32 address bits, multiple memory
  contexts and ≥2 directory lists**, so SGX540 needs the **SGX530-class
  28-bit / single-context / code-base** work plus **OMAP4 platform glue** —
  a multi-session project, not a table row.  Its multi-kick and 4 pipes are
  easier than the SGX530.
* We can shorten it: a working SGX540 to capture the init script/state, the GPL
  headers, the QNX oracle, `Garnet/sgx540-reversing`, and our ti-sysc DT.  The
  strict cleanroom applies (no proprietary material in the open tree).
* **Recommendation (DECISIONS §108):** pursue it as the long-term route; keep
  the closed stack in service meanwhile; start with the fact sheet and a
  "New SGX core" issue.  It would give a modern Mesa (GBM export + dmabuf
  feedback), i.e. the structural fix for report 275.

## Inferred

* **A WebKit PRIME fallback cannot work — measured, not inferred.**
  `tools/bench/gpu/gbmprime.c` shows the pvr GBM exposes neither a fd
  (`gbm_bo_get_fd = -1`) nor a GEM handle (`gbm_bo_get_handle_for_plane =
  0xFFFFFFFF`, `drmPrimeHandleToFD = -1`), so there is no export path for a
  WebKit patch to fall back to.  The gap is inside the GBM/Mesa pvr backend.
* **The fix is a Mesa/GBM pvr backend that exports dmabufs** (patch the amber
  Mesa or build a newer pvrports Mesa), or the durable open `powervr_sgx` stack
  with SGX540 support.

## Dead ends and wrong assumptions (valuable — do not re-run)

* **"The client buffers don't present" was not the failure this time**, unlike
  sessions 68–70: here the page buffer is never created (the engine's
  `AcceleratedSurface` fails), so catacomb is not involved in the failure.
* **`WPE_DRM_DEVICE` is not "give the engine the pvr render node" as the handoff
  hoped**: it moves the node but the pvr node's GBM is the one that cannot
  export, and it produces garbage handle indices.
* **`WPE_BUFFER_FORMAT` (linear) removes the pvr handle errors but cannot
  bypass the plane-API extraction** — the modifier flag only changes the
  modifier value, not the `gbm_bo_get_plane_count`/`gbm_bo_get_fd_for_plane`
  calls.
* **A software proof is not reachable via env**: Kumo panics without a DRM node
  path, and swrast cannot bind over this GBM (`DRI_DRI2` missing).  A Kumo patch
  (tolerate a missing node) is required, and it would still need a working
  software GBM.
* **Killing Kumo to stop it wedges the pvr driver** and freezes the panel with
  0 DSI errors; use `reboot -f`.

* **"The SGX works on GBM / `EGL_MESA_image_dma_buf_export` is present" (reports
  267/268) does not mean WebKit's GBM dmabuf export works.**  That is the
  EGL/libdbm path, which exports **PRIME handles from a dumb buffer**; WebKit
  allocates through the DDK image API, which gives the buffer **no GEM handle**.
  Killed by `gbmprime.c` (`gbm_bo_get_fd` = -1, `handle_for_plane` =
  `0xffffffff`).  [report 275 §2.2]
* **"A WebKit PRIME fallback could fix it."**  Killed by the same probe: there is
  no handle to fall back to.  [report 275 §3, DECISIONS §107]

## Unknown / open questions

* Does the pvr GBM export a dmabuf when the device is a **display-capable**
  (authenticated) node, or does it never implement the fd export?  The probe
  suggests the latter (`gbm_bo_get_fd` = `-1` even for a rendering buffer), but
  the barest device was used.
* Whether a newer pvrports Mesa (if one exists) implements the plane fd export.
* powervr_sgx's SGX540 status moves — re-check before starting a port.

## Superseded / uncertain

* **The handoff's hope that "give the engine the pvr render node" would fix it**
  — superseded: `WPE_DRM_DEVICE` makes it worse.
* **The handoff's software-fallback suggestion** — superseded: Kumo panics and
  swrast cannot bind.

## Artefacts produced

* `docs/reports/275-kumo-runtime-sgx-gbm-2026-11-26.md` — the runs, the probes,
  the alternatives and the boot incidents.
* `docs/reports/276-sgx540-open-stack-scoping-2026-11-26.md` — the scoping of an
  SGX540 port to `powervr_sgx` (the long-term graphics route; DECISIONS §108).
* `docs/reference/sgx540-factsheet.md` — the drafted SGX540 fact sheet (upstream
  format: identity, register map, clock/reset/events, 28-bit BIF/MMU, USSE/code
  base, OMAP glue, gaps).
* `docs/captures/kumo-gbm-export-2026-11-26.txt` — the live probe re-run and the
  Kumo logs (the evidence behind report 275).
* `docs/contradictions/sgx540-use-code-base.md` — the `USE_CODE_BASE` conflict
  (DDK header vs the upstream guide), open.
* `tools/bench/gpu/gbmplane.py` — the GBM plane-API probe (the decisive
  measurement; working copy `local/work/gbmplane.py`).
* `tools/bench/gpu/gbmprime.c` — the PRIME-handle probe that ruled out a WebKit
  export fallback.
* `tools/bench/gpu/gbmprime2.c` + `tools/bench/gpu/ioctllog.c` — the bounded
  DDK/GBM test (no `CREATE_DUMB`; the DDK gives no GEM handle).
* `tools/bench/gpu/softshim.c` — the per-process software-GL shim.
* Live `/tmp` logs on the unit (`/tmp/kumo.log`, `kumo2.log`, `kumo_shim.log`,
  `kumo_shim2.log`, `kumo_fmt.log`; `/tmp` is on the ext4, trap 354); copies in
  the scratchpad (`kumo-evidence.txt`, `kumo-shim2-evidence.txt`,
  `kumo-gbm-rootcause.txt`).
* Docs: KNOWN-ISSUES traps **365–367**, DECISIONS **§107**, ROADMAP 7a (a0),
  STATE, reports INDEX, this note.
* **No region-0/region-1/hd3 write; no patch change; no restore required.**

## Note on reverting

Nothing persistent was changed on the live root this session (the Kumo runs were
launches; the shim and probe were in `/tmp`, and `/tmp` persists, so remove
`/tmp/softshim.so`, `/tmp/gbmplane.py`, `/tmp/kumo*.log` if wanted).  The
desktop is on the pvr clients as before.

## Wrap-up (session 72 close)

The session is closed by the operator.  Recorded and committed privately:
reports **275** (the Kumo runtime negative and the probes) and **276** (the
open-stack scope), the drafted **`docs/reference/sgx540-factsheet.md`**, traps
**365–367**, DECISIONS **§107/§108**, ROADMAP 7a, STATE, HANDOFF (72→73), reports
INDEX, NUMBERING, and this note.  `docs_audit` and the offline tests are green;
the public snapshot is published per `REPO-MANAGEMENT.md`.

**The next session starts from the existing upstream `powervr_sgx` repo** (it
already runs the SGX545) for route A — adding SGX540 to that tree — or takes the
queue (route B).  The unit is left clean on the desktop; **Kumo is not running**.
