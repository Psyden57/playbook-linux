# Report 268 — GPU acceleration: what we know, the sources surveyed, and the next session's plan

Date: 2026-11-23 (session 67).  Static + bench; no device writes from this report.

> **Purpose.**  Session 68 is dedicated to getting the UI on the SGX540.  This
> is the survey of the outside work the operator provided plus our own measured
> state, turned into an ordered plan.  Read `267` first for the host/VPN and
> driver-selection facts.

## 0. Where we actually stand (measured)

| fact | value | source |
|---|---|---|
| kernel driver bound | `pvrsrvkm_omap4_sgx540_120`, DDK **1.17.4948957** (UM = KM) | report 246 |
| nodes | `/dev/dri/card1`, `renderD129`; `/dev/pvrsrvkm` | report 246 |
| compositor | `catacomb` maps **`pvr_dri` + `gbm`** — the compositor **is** on the GPU | report 267 |
| client (Wayland) | `epitaph` maps **`swrast_dri`**, reports `softpipe` | report 267 |
| `libEGL_amber` platforms | wayland, x11, gbm, surfaceless, device (+ `EGL_WL_bind_wayland_display`) | report 267 (corrected) |
| **SGX on GBM** | **works**: 12 window configs, `GL_RENDERER = PowerVR SGX 540`, `EGL_MESA_image_dma_buf_export` present | report 267 |
| Wayland client + `MESA_LOADER_DRIVER_OVERRIDE=pvr` | still `swrast` — override ignored | report 267 |
| `pvr_dri` vs `swrast_dri` DRI extensions | near-identical (`DRI_IMAGE`, `DRI_IMAGE_DRIVER/LOADER/LOOKUP`, `DRI_DRI2`); pvr lacks `DRI_MutableRenderBufferDriver`, `DRI_ConfigOptions`, `DRI_RENDERER_QUERY`, `DRI_NO_MSAA` | report 267 |

**The one-line summary:** the SGX works for a GBM/DRM consumer and exports
dmabufs; it is the *Wayland-client* path that does not pick it.  So the cheapest
win is probably not "make Wayland clients use pvr" but "make everything a GBM
consumer" (option B below).

## 1. The sources the operator supplied, and what each is worth to us

The seven links, exactly as supplied (the letters match the subsections below):

* **A.** https://github.com/zsavajji/pvr_sgx_rm696
* **B.** https://github.com/zsavajji/linux_mainline_rm696
* **C.** https://gitlab.com/pvrports/pvrports
* **D.** https://github.com/The1TrueJoe/powervr_sgx
* **E.** https://github.com/openpvrsgx-devgroup/linux_openpvrsgx
* **F.** https://codeberg.org/Garnet/sgx540-reversing
* **G.** https://e2e.ti.com/support/processors-group/processors/f/processors-forum/287992/powervr-sgx540-linux-drivers

### A. `zsavajji/pvr_sgx_rm696` — Nokia N9 (SGX530) on mainline 6.18 ★★★★★

**The single most useful reference.**  Nokia's in-tree `drivers/gpu/pvr`
forward-ported from 5.0 to 6.18, keeping the closed DDK **1.4.14.2514**
userspace ABI (`*_r125.so`, `libsrv_um`) so Harmattan's own binaries run
unmodified.

Why it matters to us:

* **It is the same shape of job we have**: a vendor's old SGX kernel driver
  forward-ported to a modern kernel, with the *closed* userspace kept.
* Its README lists exactly the classes of bug we will hit, and each is a
  checklist item for us:
  * probe from the **device tree** node instead of a self-registered platform
    device; `platform_get_irq` instead of `NR_IRQS + n`;
  * power through **runtime PM** with the SGX sitting in a `ti-sysc` target
    module that owns its clocks (we already have `ti-sysc`; our `pvrsrvkm`
    bind is at `56000000.gpu`);
  * **flip events**: the 5.0 port never set the omapfb overlay notify hook, so
    every flip/update wait returned `-ENOSYS`;
  * event timestamps `tv_usec = tv_nsec * 1000` (should divide);
  * the 6.18 API sweep (`proc_ops`, `vm_flags_set`, `get_user_pages` without
    vmas, `follow_pfnmap_start`, a vmap-based `vmalloc`, `timer_delete`, …);
  * **wrapped PFN mappings**: memory without a `struct page` (omapfb VRAM,
    reserved above lowmem) took its physical address from `vm_pgoff`, which
    6.18's `remap_pfn_range` only sets for private mappings → EGL images of
    framebuffer memory pointed the SGX at **physical 0** and produced L3
    in-band errors and a BUG.  **This is the same class as our reserved-memory
    ring pages**, and we should check for it deliberately;
  * a CPU floor while the GPU is busy (module param `cpu_min_khz`).
* `check-abi.py` is a reusable idea: compare **our** `pvrsrvkm.ko` dispatch
  ioctls against a reference module (here Harmattan's) without a device.  We
  have the reference's own SGX libraries from the QNX dumps, so the analogous
  check is possible for us.

**Caveat:** SGX530, DDK 1.4.x, OMAP3.  Our SGX540 is DDK 1.17 and OMAP4, so it
is a *method* reference, not a drop-in.

### B. `zsavajji/linux_mainline_rm696` — the matching kernel tree ★★★☆☆

Only useful as the example of how the driver is wired into a DT kernel
(`ti,omap3630-gpu` node, `ti-sysc` parenting, the DSS notifier).  We already
have `&sgx_module` in our DTS and a working `pvr_dri`/`pvrsrvkm` bind, so we
mostly need the *method*, not the tree.

### C. `pvrports/pvrports` — our current stack's origin ★★★★★

The unofficial pmaports repo we already build from: `linux_openpvrsgx` (kernel)
plus the **non-free SGX540 blobs**, "some tweaks to support UI that require
hardware acceleration and OpenGL ES 2.0 on PowerVR 5th generation devices".
Our DDK 1.17.4948957 stack and the `libEGL_amber`/`pvr_dri` pair come from here.

**Concretely actionable:** it states which UIs it supports.  Report 245 already
recorded "Phosh (needs their patched `phoc`) and Plasma-Mobile — catacomb is not
among them".  So the *supported* configuration is a GBM-based compositor, which
is exactly what our GBM test just proved works.  **This is option B below, and
it is the vendor-sanctioned path.**

### D. `The1TrueJoe/powervr_sgx` — fully open SGX stack ★★★★★ (as a map)

An out-of-tree **completely open** stack: a new DRM kernel driver
(`powervr_sgx`), an **open microkernel** (replacing Imagination's), and a **Mesa
gallium driver with a NIR→USSE compiler**.  SGX545 (Cedarview) is *supported*
with OpenGL 2.1 and GLES 2.0, EGL, **GBM**, GLX and DRI3.

Why it matters even though it does not do SGX540 yet:

* **SGX540 (TI OMAP4) is explicitly on its "Wanted" list**, and the porting guide
  says the whole SGX family is "one kernel driver, one Mesa driver and one
  microkernel source; a new core is added as **data**, not as a copy of the
  code."
* Its own porting guide (**in that repository, not this one**) is a step-by-step bring-up method with pass criteria —
  the best available checklist for "is my GPU actually working", independent of
  driver.
* Its **`drm/` driver exposes EGL/GBM/GLX/DRI3 to ordinary clients**, i.e. it
  solves our exact problem (clients, not just a compositor) — but for a
  different core.
* Its SGX545 core sheet is USSE, the same instruction set family as the 540;
  only the 543/544/554 need USSE2.

**Judgement:** this is the strategic path if we want *fully open* graphics, and
it is where a long-term contribution would go (an SGX540 fact sheet + platform
glue).  It is not a next-session win — it needs an open microkernel running on
our GPU first.

### E. `openpvrsgx-devgroup/linux_openpvrsgx` — what we already run ★★★★☆

The GPL kernel drivers we vendor (`drivers/gpu/drm/pvrsgx`, branch
`pvrsrvkm-7.3`, HEAD `101c687a`).  Its stated scope is important and matches our
constraint: *kernel drivers only; explicitly **not** userspace, firmware or
reverse engineering.*  So it will never fix a client-side problem — that must
come from the blobs (pvrports) or from D.

### F. `Garnet/sgx540-reversing` — SGX540 (Galaxy Tab 2) ★★★☆☆

Notes and tooling from RE of **our exact core**: a disassembler/assembler for
the ISA, shader dumping, an armv7 debugger backend for analysing GPU init and
shaders under postmarketOS, plus `ukernel/`, `info/`, `Android/`.  Its checklist
(define ISA, reverse PDS, reverse the microkernel, render independently of the
vendor libEGL) is the same road as D, starting from binaries.

**Value:** the ISA and PDS material is directly useful *if* we go the open-stack
route (D), and the "analyse GPU init on postmarketOS" tooling is a model for
instrumenting our own init.  Not needed for option B.

### G. The TI E2E thread ★★☆☆☆

Historical only: "pvr-omap4 is part of the GLSDK" and the OMAP4 GLSDK downloads
are gone; the wiki is dead.  It confirms the **DDK lineage** (TI's
`pvr-omap4`, DDK 1.17 family) that our blobs come from, and that no newer
official SGX540 Linux driver exists.  Do not chase it for binaries.

## 2. Options, with honest cost

| # | route | gets clients on the GPU? | cost | notes |
|---|---|---|---|---|
| **B** | **GBM-based compositor** (pvrports' supported config: their patched `phoc` + Phosh, or another GBM/wlroots compositor) | **Yes** — clients render into GBM buffers the compositor imports on the SGX | **smallest** | proven path: the GBM EGL platform already gives `PowerVR SGX 540` with dmabuf export; replaces the catacomb desktop |
| **A** | Make Mesa's **wayland** platform select `pvr_dri` (find and fix the rejection) | Yes, keeps catacomb | medium, unknown | first step is diagnostic (`EGL_LOG_LEVEL=debug`); the DRI extension sets are near-identical, so it is *not* a trivial gate; if it is a missing loader extension we may not be able to fix it without the blob's cooperation |
| **C** | **dmabuf bridge**: clients render to linear buffers, compositor uploads via `EGL_EXT_image_dma_buf_import` | Partly (upload + composite on GPU, clients still software) | medium | `EGL_MESA_image_dma_buf_export` exists; import support unverified |
| **D** | **Open stack** (The1TrueJoe) with an SGX540 core port | Yes, fully open | large | needs the open microkernel on our GPU; a contribution-scale project, the right long-term home |

**Recommendation:** do **A as a one-session diagnostic** (it is cheap and it
tells us whether catacomb can be saved), and in parallel prove **B** by
bringing up pvrports' `phoc` — because B is vendor-supported and our own GBM
test already showed the GPU works on that path.  Treat D as the long-term
route, and C as a fallback.

## 3. Session 68 plan (ordered, one variable at a time)

**Step 1 — find out why Mesa's wayland platform rejects pvr (diagnostic, ~1 boot).**
```
EGL_LOG_LEVEL=debug MESA_DEBUG=1 MESA_LOADER_DRIVER_OVERRIDE=pvr \
  WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/run/user/10000 epitaph
```
and the same with `LIBGL_DEBUG=verbose`.  Capture the loader's stated reason.
Deliverable: the exact rejection (missing extension / config filter / different
code path) recorded in a report.  **Do not** start patching Mesa before this.

**Step 2 — prove the GBM path with a real compositor (option B).**
Bring up pvrports' `phoc` (`phoc`/`phosh` APKs are in
`local/work/nura/work/packages/edge/armv7/` or buildable from
`local/work/nura/pmaports/`), run it as the session instead of catacomb, and
check that a GBM client's `maps` shows `pvr_dri` and that a frame reaches the
panel.  This is the vendor-supported configuration and the fastest route to
"the UI is on the GPU".
Pass criteria: compositor + at least one client on `renderD129`, panel alive,
no GPU hang in `dmesg`, and a before/after measure of a full-screen redraw.

**Step 3 — decide, with data.**  If step 2 works, the desktop becomes
`phoc`+Phosh (or we port catacomb's smithay path onto GBM).  If step 2 fails,
fall back to step 1's finding (fix the wayland rejection) or option C.

**Step 4 (only if 2 and 3 stall) — the long route.**  Read
the `The1TrueJoe/powervr_sgx` porting guide (external) and scope an SGX540 core
port (fact sheet from `Garnet/sgx540-reversing`'s ISA work + our live register
dumps).  This is a multi-session project, not a session task.

**Constraints carried from session 67:** `panic=0` is on the image; keep it for
any deliberate-fault experiment.  The hd3 root is rw with `barrier=0` — prefer
`reboot -f`.  The inittab affinity line and `epitaph r10005` are already
installed; a reflash reverts both (trap 343).  Region 0 is `0x3547D613`; a
kernel-side change means a re-export (trap 211) and a region-0 flash.

## 4. Greps already done (session 67), and what they found

**Hit on the first one.**  `services4/srvkm/env/linux/mmap.c` uses exactly the
pattern source A warns about:

```c
if (PFNIsPhysical(ps_vma->vm_pgoff))
{
    PVR_ASSERT(LinuxMemAreaToCpuPFN(psLinuxMemArea, uiByteOffset) == ps_vma->vm_pgoff);
    result = IO_REMAP_PFN_RANGE(ps_vma, ps_vma->vm_start,
                                ps_vma->vm_pgoff, uiByteSize, ps_vma->vm_page_prot);
```

i.e. the driver treats **`vm_pgoff` as the physical PFN** of a contiguous
range (the comment at line 251-254 says so explicitly: "vm_pgoff is the PFN …
the second page is assumed to have PFN (vm_pgoff + 1)").  Source A's fix was
that on modern kernels `remap_pfn_range()` only sets `vm_pgoff` for **private**
mappings, so a non-private mapping of memory without a `struct page` (omapfb
VRAM, reserved-memory rings) yields PFN 0 → **the SGX is pointed at physical
0**, giving L3 in-band errors and a BUG.

**But it does not apply to us.**  `mmap.h` sets `PVR_MAKE_ALL_PFNS_SPECIAL`
whenever `VM_MIXEDMAP` is defined (and it is, on 7.3.3):

```c
#if defined(VM_MIXEDMAP)
/* Mixed maps allow us to avoid using raw PFN mappings (VM_PFNMAP) for
 * pages without pages structures ("struct page") ... */
#define PVR_MAKE_ALL_PFNS_SPECIAL
#endif
```

With that defined, every block above is `#if !defined(PVR_MAKE_ALL_PFNS_SPECIAL)`
and is compiled **out** — the driver uses `vmf_insert_mixed()` instead of
`remap_pfn_range`, so the `vm_pgoff`-as-PFN path source A had to fix is not in
our build.  **Hypothesis eliminated** (report 268 §4): this is not our bug, and
the next session should not spend time on it.

Other §4 checks (flip/overlay notify, `tv_usec`, runtime PM) found **no**
`omaplfb` sources in the tree — our display class is the DRM path
(`SGX_DRM`), not Nokia's `omaplfb`, so source A's omapfb-specific fixes do not
map onto us directly.  That narrows the useful part of source A to the PFN
mapping, the DT/runtime-PM method, and `check-abi.py`'s idea.

## 5. Specific things still to check against our tree (from source A)

These are cheap greps against `local/kernel-tree/drivers/gpu/drm/pvrsgx` and
worth doing before blaming the blobs:

1. **`remap_pfn_range` / `vm_pgoff` for PFN mappings** — do we derive a physical
   address from `vm_pgoff` for memory that has no `struct page` (our
   `reserved-memory` rings, omapfb VRAM)?  If yes, that is source A's
   "pointed the SGX at physical 0" bug class, and it would show as L3 in-band
   errors.
2. **Flip/overlay notify hook** — does our display class set the omapfb overlay
   notify callback?  If not, every flip wait returns `-ENOSYS` (source A's fix).
3. **Event timestamps** — check for `tv_usec = ... * 1000` instead of a divide.
4. **Runtime PM vs our manual clock enable** — source A moved the SGX to
   `ti-sysc`-owned clocks and runtime PM; verify ours does not fight the
   `ti-sysc` module.
5. **DDK version pairing** — `UM DDK-(4948957) and KM DDK-(4948957) match` is
   already logged, so the kernel/userspace split is consistent; do not change
   one without the other.

## 6. Sources

* `zsavajji/pvr_sgx_rm696` — SGX530 kernel driver on Linux 6.18, DDK 1.4.14.2514
  userspace kept, forward-port bug list and `check-abi.py`.
* `zsavajji/linux_mainline_rm696` — the DT kernel it links against.
* `pvrports/pvrports` — the SGX540 kernel drivers + non-free blobs we run;
  supported UIs are Phosh (their `phoc`) and Plasma-Mobile.
* `The1TrueJoe/powervr_sgx` — fully open SGX stack (DRM driver, open
  microkernel, Mesa gallium + NIR→USSE); SGX545 supported, **SGX540 wanted**;
  porting guide is the bring-up method.
* `openpvrsgx-devgroup/linux_openpvrsgx` — the GPL kernel drivers we vendor;
  kernel-only by charter.
* `Garnet/sgx540-reversing` — SGX540 ISA/PDS/microkernel RE tooling.
* TI E2E 287992 — historical: OMAP4 `pvr-omap4` came with the (now gone) GLSDK.

## 7. Artefacts

* `local/work/pvrdri/` — `pvr_dri.so`, `swrast_dri.so`, `libEGL_amber.so.0.0.0`
  pulled from the unit for host-side analysis (sha256 in the session note).
* `/tmp/gbmtest2.py` on the unit — the GBM/EGL probe that proved the SGX works
  on the GBM platform (source of the 12-config / dmabuf results).
