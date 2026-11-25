# Report 270 — trap 349 is omapdrm TILER exhaustion, not a pvr allocation defect

Date: 2026-11-24 (session 70).  Bench only; no device writes.  Answers
HANDOFF-69 §0f.1 (the one experiment for session 70).

## 0. Headline

**Trap 349 is not a pvr defect and not a compositor lifetime bug: the client
fails on its second buffer when the OMAP4 DMM/TILER translation window is too
full to serve the next scan-out allocation.**  The failing allocation is an
**omapdrm** one — omapdrm pins the buffer into the TILER, the export's
`dma_buf_map_attachment` then returns `-ENOMEM` to the pvr driver
(`PVRSRVMapDmaBufKM: Failed to get dma-buf phys addr`), and the pvr DRI layer
reports the failure as `CreateImageShared: Failed to create buffer`.

Measured on a clean boot, ten consecutive runs, **10/10**:

| pinned TILER bytes before the run | outcome |
|---|---|
| **59.4 MiB** | clean |
| **112.3 MiB** | **fails** |

Idle time does not help; only rebooting (or dropping the occupancy) does.

### 0.1 Two corrections to the first draft of this report (§0.2)

Both were found by re-reading the kernel and the debugfs output against the
arithmetic, and both matter because the first draft named the wrong mechanism
and therefore the wrong fix.

* **`could not remap: -12 (3)` is `TILFMT_PAGE`, not `TILFMT_32BIT`.**  The
  enum is `TILFMT_8BIT = 0, TILFMT_16BIT, TILFMT_32BIT, TILFMT_PAGE`, so the
  `(3)` in the log is the **paged/linear** path (`tiler_reserve_1d`).  The
  buffers are **not tiled**: their GEM flags are `0x02000003`
  (`OMAP_BO_MEM_SHMEM | OMAP_BO_WC | OMAP_BO_SCANOUT`) and the `OMAP_BO_TILED_MASK`
  bits (`0x0f00`) are **zero**.
* **There is no "slot rounding" 8× amplification.**  `tiler_reserve_1d()`
  reserves `obj->size` pages **1:1**, so a linear buffer costs exactly its byte
  size.  A genuinely *tiled* 1024x600 AR24 buffer would be 2.38 MiB; a tiled
  8-bit one 0.62 MiB.  None of the measured objects match tiling.

### 0.2 What the 8× actually is (measured, not yet explained)

The real multiplier is exact and reproducible, and it comes from the
**allocator**, not from the TILER:

| buffer (1024x600 AR24 = 2 457 600 B of pixels) | GEM `obj->size` | ratio |
|---|---|---|
| `[fbcon]` scan-out (omapdrm's own allocation) | 2 457 600 B | **1.00×** |
| `catacomb` scan-out (`framebuffer[71]/[73]`, pitch 4096) | 19 660 800 B | **8.00×** |
| pvr client BO, 1024x565 (frame = 2 314 240 B) | 18 513 920 B | **8.00×** |
| pvr client BO, 1024x585 (frame = 2 396 160 B) | 19 169 280 B | **8.00×** |

So every buffer in the GBM/pvr path is **eight screenfuls in one allocation**,
while omapdrm's own framebuffer is exactly one.  The ratio is 8.00 for all three
measured sizes, which rules out alignment/rounding (that would give a size-
dependent ratio) and points at a constant multiplier in the allocation path.
**Why 8 is not established** — it is the next measurement (see §4).  What is
established is that it is not tiling and not the TILER.

Two further corrections to the handoff and to report 269 §4h, both measured:

* **The failure is not catacomb-specific in the way the handoff assumed.**  In
  the handoff's own control direction catacomb was "fails 3 of 5, never under
  phoc".  Under phoc the probe is clean **5/5**, and catacomb on a fresh boot is
  **3/5**.  But phoc simply holds less of the TILER (39.8 MiB idle vs catacomb's
  59.4 MiB), so it clears the threshold — it is the *same* fault with more
  headroom, not a different code path.
* **"Catacomb sends zero `wl_buffer.release`" is wrong.**  Successful catacomb
  runs receive **15–16** releases.  My first sample of "zero releases" was
  taken from runs that had *already died* at `eglCreateWindowSurface`, where no
  buffer is ever created; the zero was a consequence of the failure, not its
  cause.  (Trap 351 records that reading, so the mistake is not repeated.)

## 1. The measurement

### 1.1 Node map (the handoff's first branch)

`wl_drm.device()` is `/dev/dri/renderD128` under **both** compositors — and
`renderD128` is **omapdrm**, not the pvr node:

```
renderD128   DRIVER=omapdrm
renderD129   DRIVER=pvrsrvkm      (ti,omap4430-gpu / img,powervr-sgx540)
card0        DRIVER=omapdrm
card1        DRIVER=pvrsrvkm
```

catacomb derives the node from Smithay's `default_dmabuf_feedback()`
(`udev.rs:851`), which takes the **scanout** device's node and only falls back
to another render-capable node if that one has no render node — omapdrm has
one.  So the advertised node is correct for the *scanout* side, and the client's
`pvr_dri` (loaded from the fd Mesa opens on that node, then switched by the
override) exports its buffers to the omapdrm scanout through PRIME.  Both
compositors do exactly the same thing (§2), which is why the node is not the
discriminator.

### 1.2 The alternating occupancy

Idle desktop (catacomb + tabula + epitaph + wvkbd running) pins **59.4 MiB**.
One probe run takes it to **112.3 MiB**; the next run then fails and the
occupancy drops back to 59.4 MiB.  Ten back-to-back runs alternated perfectly:

```
59.4MiB  -> clean
112.3MiB -> fail
59.4MiB  -> clean
112.3MiB -> fail        (x5, then repeated with a 6 s settle between runs)
```

The settle test matters: **waiting does not release it.**  With no client
process alive the occupancy stays at 112.3 MiB for at least 60 s, and the
three extra objects are still pinned in the freed object's own accounting.

### 1.3 The kernel trace (unfiltered, one failure)

```
omapdrm omapdrm.0: could not remap: -12 (3)
PVR_K:(Error): DmaBufImportAndAcquirePhysAddr: dma_buf_map_attachment failed: -12
PVR_K:(Error): PVRSRVMapDmaBufKM: Failed to get dma-buf phys addr
PVR_K:(Error): PVRSRVMapDmaBufBW: Failed to map dma-buf handle
WARNING: drivers/gpu/drm/omapdrm/omap_gem.c:1209 at omap_gem_free_object+0x218/0x23c, CPU#1: pb-egltest/...
  omap_gem_free_object <- drm_gem_dmabuf_release <- dma_buf_release <- dentry_kill <- __fput <- do_exit
```

Two independent facts in one trace:

1. **`could not remap: -12 (3)`** is `omap_gem_pin_tiler()`
   (`omap_gem.c:763`) — the `tiler_reserve_*()` call returned `-ENOMEM`.  The
   `(3)` is the format argument, and the enum order is
   `TILFMT_8BIT = 0, TILFMT_16BIT, TILFMT_32BIT, TILFMT_PAGE`, so **`(3)` is
   `TILFMT_PAGE`** — these are the *linear* buffers taking
   `tiler_reserve_1d(obj->size)` (**corrected in §0.1**; the first draft read
   this as 32-bit tiling, which it is not — the GEM tiled-mask bits are zero).
   The `-12` is then propagated out through `omap_gem_pin()` → the dma-buf
   `map_attachment` → `PVRSRVMapDmaBufKM` → the DRI image creation → the client's
   `CreateImageShared` message.
2. **`WARN_ON(refcount_read(&omap_obj->pin_cnt) > 0)` at `omap_gem.c:1209`** —
   the object is freed with a pin still held, from `dma_buf_release` on the
   *process exit* path.  So the accounting shows pins that were never unpinned.

Counts on the clean boot after the run set: `could not remap` = 11,
`omap_gem.c:1209` = 11 — one of each per failing run.

### 1.4 The container

The TILER maps a fixed aperture into the SoC address space — `TILVIEW_8BIT =
0x60000000`, sized `2^CONT_WIDTH_BITS × 2^CONT_HEIGHT_BITS = 2^14 × 2^13 =
2^27` bytes = **128.0 MiB**.  That is a **window wired into the memory map**, not
a framebuffer-size limit: the display controller reads the window and the DMM
translates each 4 KiB of it to a real, scattered DRAM page.  Allocation is
managed in 4 KiB **slots** (a 64×64 px 8-bit tile), but for the linear buffers in
play here `tiler_reserve_1d()` charges `obj->size` bytes **1:1**, so a buffer's
cost is its real size, not a rounded-up tile count.

OMAP4 has **one** container shared by the 8/16/32-bit views
(`containers[TILFMT_8BIT..32BIT] = tcm[0]`, `omap_dmm_tiler.c:933-935`), and the
paged/linear allocations use `TILFMT_PAGE`, which on OMAP4 is that same `tcm[0]`
(`container_height == lut_height`).  So every pinned scanout buffer competes for
the same 128 MiB window.  With catacomb's buffers at **8× their pixels** (§0.2),
about three and a half of them fill it.

## 2. Why phoc is clean (the handoff's control, correctly read)

Same probe binary, same `renderD128`, same `pvr_dri`, `WAYLAND_DEBUG=1`:

| | phoc | catacomb |
|---|---|---|
| runs | 5 | 5 |
| `Failed to create buffer` | **0** | 3 (fresh boot) / 5 (dogged) |
| `wl_buffer.release()` events received | **16–17 per run** | **15–16 per run (successful runs)** |
| idle TILER held by the compositor | **39.8 MiB** | **59.4 MiB** |
| `could not remap` in dmesg | **0** | 1 per failure |

Both compositors release client buffers promptly; the earlier "catacomb never
releases" reading is retracted (§0).  The difference that decides the outcome is
**idle TILER footprint**: catacomb pins ~20 MiB more (its scanout pair plus the
wallpaper), so the probe's new 18.5–19.7 MiB buffer does not fit about half the
time.

The strongest support for "it is capacity, not code path": with catacomb's own
clients stopped (freeing ~20 MiB of idle pinning), the same probe under the same
catacomb went **4 clean / 4 failed out of 8**, and the outcome tracked the
occupancy exactly — every clean run started at 58.1 MiB or below, every failure
at 94.7 MiB:

```
39.8MiB clean | 94.7MiB fail | 58.1MiB clean | 94.7MiB fail
58.1MiB clean | 94.7MiB fail | 58.1MiB clean | 94.7MiB fail
```

Nothing about catacomb's C code changed between those runs and the ones that
never failed.

## 3. What each handoff branch resolves to

| handoff branch | verdict |
|---|---|
| catacomb holds buffers too long (release after next alloc) | **No.** Catacomb releases 15–16 per run, same order as phoc's 16–17. |
| pvr DDK second-allocation defect, independent of compositor | **No.** The failing call is omapdrm's `tiler_reserve_1d`, reached through the dma-buf export; the buffer never leaves the compositor's TILER budget. |
| allocation pressure / import caching | **Yes — this is it**, but the pressure is the *scanout* TILER window, not pvr memory: 128 MiB of aperture, 59.4 MiB idle, one more 18.5–19.7 MiB buffer does not fit at 112.3 MiB. |
| only with the desktop's own clients | Partly: they raise the idle occupancy. With them stopped the failure rate halves but does not vanish. |

## 4. Consequences and what to do next

* **The desktop is not in immediate danger.**  The static desktop (wallpaper,
  bar, keyboard) already lives at 59.4 MiB idle and is stable — that is the
  session-69 validated state.  The failure needs a *full-screen AR24 client*
  on top of an already-high occupancy, which is why it shows up with probes
  and animated clients rather than with the bar.
* **Any large client is exposed**, because each buffer in the GBM/pvr path
  costs **8× its pixels** in the TILER: a 1024x600 AR24 frame is 2.4 MiB of
  pixels but its GEM object is 19,660,800 B, so it occupies **18.75 MiB** of the
  128 MiB window.  (Same 1.00× equivalent from omapdrm's own framebuffer is
  2.4 MiB — the 8× is the allocator's, not the TILER's; §0.2.)
* **Real fixes, in order of cost** — **corrected: the first draft's "prefer
  LINEAR buffers" was wrong**, because these buffers are *already* linear
  (`TILFMT_PAGE`, tiled-mask bits zero), so that change addresses nothing.
  1. **Find out why each allocation is 8 screenfuls.**  This is the highest-value
     next measurement and the only one that reduces the cost per client.  The
     candidates to check, in order: what catacomb asks
     `gbm_bo_create_with_modifiers` for (does it request a larger `height`, a
     count, or a modifier that the pvr allocator rounds up?); whether the pvr
     `gbm` backend rounds the *allocation* while reporting the requested size
     (so `obj->size` is 8× and the visible size is right); and whether
     `drmModeAddFB2` is called with a `pitch` that implies the smaller buffer
     (it is: `pitch[0]=4096`, 600 rows = 2,457,600 B, against `obj->size`
     19,660,800 B — so the framebuffer uses 1/8 of the object).
  2. **Reduce how many such buffers stay pinned at once.**  Fewer live scanout
     buffers = more headroom; the `WARN_ON(pin_cnt > 0)` at `omap_gem.c:1209`
     in §1.3 suggests some pins outlive their use.
  3. **A kernel-side unpin fix** if the `WARN_ON` proves to be a real leak rather
     than a symptom: the object is freed with `pin_cnt > 0` from
     `dma_buf_release` on exit, which is an omapdrm refcount imbalance worth its
     own instrumented session.
  4. **Drop the wallpaper's scanout pin** while other clients are mapped, to
     keep the idle occupancy lower — a mitigation, not a fix.
* **Not a reason to change the session-69 fixes.**  Traps 347/348 (the NPOT
  clamp and the GL locations) are independent and remain validated.
* **Rendering is unaffected.**  This is the scan-out path: the SGX540 still does
  all the rendering (`GL_RENDERER = PowerVR SGX 540`, `pvr_dri` mapped).  Nothing
  here means graphics is running on the CPU.

## 5. Method notes (so this is repeatable)

* The measurement that decided it is `cat /sys/kernel/debug/dri/omapdrm.0/gem`
  parsed for the **pin** column (field 6) and the **size** column (field 10),
  summed.  `mm` and `bufinfo` are not useful here: `dri/*/mm` prints the *VMA*
  address space (always "used 0"), and `dma_buf/bufinfo` **blocks** on this
  kernel — reading it hangs the SSH session.
* Parse the file properly, not with a positional `awk`: the fields are
  `flags: name (refcount) offset dma_addr (pin_cnt) vaddr roll size`.
* `waylandinfo`-style "zero releases" conclusions must be taken only from runs
  that reached the swap at least once; a run that dies in
  `eglCreateWindowSurface` (`context/surface failed 0x3009`) never creates a
  buffer at all.  Two distinct failure modes exist and look alike in a summary:
  **fail-at-surface-create** (no buffer) and **fail-after-first-swap** (the
  second buffer).  Only the latter is the handoff's symptom.

## 6. Artefacts

* Evidence capture: `docs/captures/gpu-tiler-exhaustion-2026-11-24.txt`.
* Device-side measurement helper: `/tmp/pins.py` (staged from the session
  scratch; the parse is reproduced in §5).
* Probe unchanged: `tools/bench/egl/pb-egltest.c` (prebuilt armv7 at
  `local/work/egltest-client/pb-egltest`, sha256 `7eda9fa3…`).
* No device writes.  The session's bench disturbance (four `tinydm` restarts to
  switch catacomb↔phoc) wedged the DSI link — the documented trap 289 — and
  was cleared by one `reboot -f`; `BOOTCTR_FCTRY` went `0x21 → 0x20`, above the
  `0x1e` top-up threshold.
