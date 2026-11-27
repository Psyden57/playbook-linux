# Current state

## Current state — 2026-11-27 (session 78)

**THE OPEN MICROKERNEL TAKES AND COMPLETES JOBS.**  `null job signalled in
946 µs`, `600 null jobs in 91 ms`, **zero GPU resets** for the whole uAPI
suite, the CCB ring draining (`issued 615 consumed 615`), host interrupts
delivered, the tick counter and entry counters climbing continuously.  **The
fix was ONE BIT: `EVENT_PDS_INFO` (0x0a70) had `USE_PIPELINE = TOGGLE`
(round-robin between the two USE pipes); the vendor's derivation is
`USE_PIPELINE_0`.**  Under round-robin the event tasks alternated pipes and a
stuck pipe blocked the toggle forever — the one-dispatch-then-stuck
signature.  Report **281**; the full campaign (16 boots) is in the capture
`sgx540-kick-entry-counter-2026-11-27.txt`.

**How the one bit was found** (every instrument proven first — DECISIONS
§116): the tick counter needed a **GPU-side seed** (the GPU's initial view of
the control-block tail is not the CPU's zeros — the old counter read
0xffffffff and wrapped to 0; trap 409); "4 fetches" was **double counting**
(trap 410); the negatives were re-run correctly (DMS_AGE no effect; the emit
with the vendor's operand derivation — session 76's negative was garbage
operands; `UK_NO_PHAS` — the vendor's image has no `phas`; the DOUTU
task-control words fixed to `MODE_PERINSTANCE` — kept, no effect alone); the
launch-script diff was completed and **`CACHE_CTRL = 0x0000ffff` — the
vendor's own value — breaks event dispatch entirely** (bisected; trap 411;
not written).

**What still fails**: 30/50 on the uAPI suite — the failures are SGX545-shaped
test expectations (core_id 0x0115, va_bits 32, heap base 0x3e000000; our core
is 0x0114 / 28-bit), so `vm_map` at 32-bit addresses correctly refuses.  The
suite needs its addresses parametrized by `va_bits`.  No render/transfer job
submitted yet — the TA/3D register tables (hypothesis) are the next mountain
toward an actual frame.

**Bench**: region 0 `0x02C1DDC2` (no flash all session), region 1
`20a5edd3…`, hd3 the session-71 rebake.  `BOOTCTR_FCTRY 0x28` (the top-up
fired during the heavy rebooting — self-maintaining).  **The working pair is
preserved AND live**: firmware `sgx540_ukernel.bin.working` = `kick10`
(`3fbbd11b…`), module `powervr_sgx.ko.working` = `10c6f435…`.  Preserved
beside them: `.kick9w/.kick8w/.kick2w/.fix1w/.tick2/.before-hostirq` firmware
variants and module `.pre-doutu 0e0fea3b / .prelaunch b84dd8c0 /
.pre-loopback 127a5fad / .dmsage be3d4b35 / .predump 801d08a5 /
.pre-doorbell 94eaea61`.  `card1` = `powervr_sgx` (`open microkernel
kick10`), desktop llvmpipe, `wlan0` down (the earlier GPU resets), usb0 up.

## Previous state — 2026-11-27 (session 78, mid-session, before the USE_PIPELINE fix)

The pre-fix snapshot: the dispatch happened exactly once per microkernel life
(the launch's init pass, then the FIRST timer edge); the tick counter had just
been fixed with the GPU-side seed; the launch-script diff was complete with
CACHE_CTRL identified as load-bearing.  Full detail in the capture
`docs/captures/sgx540-kick-entry-counter-2026-11-27.txt` (boots 1-13) and
report 281.  Superseded by the current state above.

## Previous state — 2026-11-27 (session 77)

**HANDOFF §0b is answered, and broader than it asked: the kick entry is never
entered, and NO dispatch source works after the launch — the EDM fetched the
event program twice during launch and never again, for any trigger.**  The
re-instrumented kick counter (control-block word 0x3f, the `kick` entry's first
instruction, mechanically verified free) reads **0 across eight job submits
that each cost a GPU reset**; the timer's b29 stays latched while the entry
stays 2 and the tick counter stays 0; and the session-76 wrap-up's `relaunch`
node already showed re-writing `EVENT_KICK2` itself does not re-fetch.  Report
**280**; traps **406–408**; capture
`docs/captures/sgx540-kick-entry-counter-2026-11-27.txt`.

**Two instrument corrections fell out, both of the trap-403 family:**

* The `UK_ENTRYLOG` STATUS-snapshot store has **never worked** (in `fix1`
  too): it runs `limm r5, #0x39` — bytes — where its own comment says
  "word 0x39 → 0xe4", so the store lands misaligned and word 0x39 has read 0
  in every dump of every build (trap 406; found by disassembling the image).
* **"One init pass and one dispatch pass, and the dispatch pass is the
  timer's" is withdrawn in its second half — there is no dispatch pass.**  The
  `tick(0x2c) = 1` readings of sessions 75–76 are misparse candidates:
  control-block **word 0x2a reads 1** on every boot (writer still unidentified
  — nothing in the image stores there, the driver memsets the block), 0x2a is
  the 3rd word of dump row 6 while 0x2c is the 5th, and a word-index parse
  reads tick = 0 on every session-77 boot.

**The doorbell read-back is not evidence on this silicon (trap 407):** the new
`ccb_write()` sampling (8× after the write) and the launch-kick control both
read all-zero — even the provably-landing launch kick never shows `EVENT_KICK`
NOW in a read-back.  A self-clearing register needs a landing control.

**The complete dispatch-register dump (now in `status`) measured the never-
written ones:** `DMS_AGE = 0` (the vendor's launch writes
`ENABLE | PVRSRV_SGX_MAX_DMS_AGE`; the value is underivable from the DDK 1.8
drop), the **PIXEL event-PDS pair entirely 0** (`0x0a5c/0x0a60/0x0a64`),
`EVENT_PDS_ENABLE2 = 0`, `EVENT_KICK1/KICK3 = 0`.  The next candidates are the
launch-script registers the driver never writes — **`DMS_AGE` first** (a stuck,
never-completed launch task with aging disabled would block every later
dispatch, which is exactly the observed shape), then `PDS_DMS_CTRL2`/
`USE_G0/G1`/`USE_CTRL`/`USE_MOE`/`USE_CODE_BASE`.

**Bench at the close (boot 3, read on the unit):** region 0 **`0x02C1DDC2`**
(no flash this session), region 1 **`20a5edd3…`**, hd3 = the session-71 rebake
`5d3eac8f…` live as `/dev/loop0` ext4 rw.  **`BOOTCTR_FCTRY 0x23`** read fresh
on boot 1; boots 2–3 cost one unit each (0x22/0x21 expected — **not yet
re-read**; read `dmesg | grep pbrpmb` on the next boot).  Kernel `7.3.3`.
`card1`/`renderD129` = `powervr_sgx` running **`open microkernel kick2`**
(`12847879…`, the kick-entry instrument), `EDM init: event status
0x20000000, BIF int 0x8000`, `microkernel started`, `BIF_FAULT 0`;
`card0`/`renderD128` = `omapdrm`.  Module `powervr_sgx.ko` = **`ea10d582…`**
(the kick instrument ride-along: the `doorbell` node, `ccb_write`/launch-kick
sampling, and the six new `status` registers), with `.pre-doorbell`
(`94eaea61…`) and `.predump` (`801d08a5…`) preserved; firmware variants beside
the live file: `.fix1w` `f468a71d…`, `.kick2w` `12847879…`, `.tick2`
`7b44b33a…`, `.before-hostirq` `12f12abb…`.  Desktop llvmpipe; panel
`bl_power` 0; **`wlan0` DOWN** (the GPU resets take it over — the session-76
caution); `usb0` `169.254.0.1/16` up; `/tmp/powervr_sgx_test` on the ext4.

## Previous state — 2026-11-26 (session 76)

**The SGX interrupt reaches the CPU; five defects fixed; the entry counter**
**corrected** (word 0x1c was `CLEANUP_DEFER_CT`; on word 0x07 the program is**
**fetched twice).  Full snapshot moved to
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md); its
"dispatch dead after the launch" conclusion was superseded by report 281
(`EVENT_PDS_INFO`'s `USE_PIPELINE` bit).

## Previous state — 2026-11-26 (session 74)

**The open SGX540 microkernel started and a NULL job printed `1/1 passed`; a
running EDM was the open question.**  Superseded by session 75, which showed that
the `1/1 passed` was the driver's own 2 s timeout, and then by session 76, which
found that the interrupt had never reached the CPU and **corrected session 75's
"entered once and never again"** — the corrected counter reads two fetches.  Full
snapshots:
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
(`## Previous state — 2026-11-26 (session 74)` and `… (session 75)`); its report
is 277, with two figures withdrawn by 278 and one more by 279.

## Previous state — 2026-11-26 (session 73)

**The open SGX540 port reached hardware: the driver binds and identifies the
core, and the first microkernel builds.  The next blocker is the microkernel's
list-0 / PD-table path.**  Superseded by session 74, which gave it that path;
the full snapshot is in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
(`## Previous state — 2026-11-26 (session 73)`).

## Previous state — 2026-11-26 (session 72)

**The Kumo/WPE WebKit experiment ran and is a proven negative: the engine is on
the SGX, but the PowerVR GBM backend cannot export dmabufs, so the page never
composes.**  Full snapshot in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
(`## Previous state — 2026-11-26 (session 72)`).

## Previous state — 2026-11-25 (session 71)

Superseded; the full snapshot is in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
(`## Previous state — 2026-11-25 (session 71)`).

## Previous state — 2026-11-24 (session 70)

**The GPU buffer waste is fixed and deployed, and trap 349 is closed.**  Two
findings, in order.  (1) The "second buffer fails" symptom was **omapdrm
scan-out capacity**, not a pvr or compositor defect: rendering is the SGX540's
job (`renderD129`) and scan-out is omapdrm's (`renderD128`), which streams
through a fixed **128 MiB TILER aperture** (`0x60000000`, `2^14 × 2^13`) and
pins each buffer into it; when the next does not fit, `tiler_reserve_1d()`
returns `-ENOMEM` → `could not remap: -12 (3)` → `dma_buf_map_attachment
failed: -12` → the client's `CreateImageShared: Failed to create buffer`.
(2) **The capacity was being wasted 8×** by a pitch-alignment bug: `libdbm`
describes a linear buffer as `width = 1, height = <total bytes>, bpp = 8`, and
`omap_gem_dumb_create()` calls `drm_mode_size_dumb(dev, args, SZ_8, 0)`, which
aligns the **pitch to 8 bytes** — a 1-byte row becomes an 8-byte pitch.
[reports 270, 271; trap 349; DECISIONS §104]

* **The fix is deployed on the live root**: `tools/bench/gpu/dbmfix.c`
  (an `LD_PRELOAD` wrapper on `ioctl` that re-expresses the bytes as
  `width=<bytes>, height=1`) installed as `/usr/local/lib/dbmfix.so`, enabled by
  `/usr/local/bin/catacomb-dbmfix`, with `catacomb.desktop` `Exec=` repointed
  (backup `catacomb.desktop.orig`).  No closed binary is modified — `libdbm`
  reaches the kernel through `drmIoctl`'s PLT.
* **Measured after the fix**: idle desktop pinned TILER **59.4 → 14.0 MiB**
  (4 h uptime) and **9.5 MiB** on a fresh boot; **every** GEM object **1.00×**
  (largest 2,457,600 B = one full frame, was 19,660,800); **8/8** re-rendering
  pvr clients clean with occupancy **flat at 20.7 MiB** (before: 3/5 failed,
  climbing 59.4 → 112.3 MiB); **panel operator-confirmed correct twice**.
  **Trap 349 no longer reproduces.**
* **The 10/10 threshold before the fix** (pinned 59.4 → clean, 112.3 → fail) and
  the phoc-vs-catacomb comparison (39.8 vs 59.4 MiB idle; both release client
  buffers 15–17 per run; both advertise `wl_drm.device("/dev/dri/renderD128")`,
  i.e. omapdrm) are in report 270 — they established the mechanism before the
  pitch bug explained the magnitude.
* **Retracted this session** (all four kept in the reports): `-12 (3)` means
  `TILFMT_PAGE`, **not** 32-bit tiling; the 8× is **not** slot rounding (linear
  buffers are reserved 1:1); "prefer LINEAR buffers" therefore fixes nothing
  (they are already linear); and "the DDK asks for height×8 rows" was the right
  answer to the wrong question — the height was **bytes**, not rows (killed by
  logging the ioctl arguments instead of inferring from the size).  Also
  retracted: "catacomb sends zero `wl_buffer.release`" (trap 351).
* **Bench at the close (fresh boot, re-measured)**: region 0 media trailer
  `1f 2d c8 d7 13 d6 47 35` = CRC **`0x3547D613`** (no write); region 1 sha256
  **`20a5edd3…`**; hd3 ext4 magic `0xEF53`, **15 273 984** × 4 KiB blocks, root
  `/dev/loop0` rw; `BOOTCTR_FCTRY` **`0x1e`**; `nproc` 2, PID 1 mask `3`, zero
  DSI errors, panel `connected`, backlight 255, battery Full 4.148 V.
  **No region-0, region-1 or hd3 image write; no patch change.**
* **Owed — the hd3 rebake, which must now also carry the pitch fix.**  The baked
  image is the session-59 state, so a reflash loses the session-69 GPU result
  *and* the shipped-behaviour buffer fix.  `tools/rebake-hd3.sh` applies the
  measured delta (catacomb r10003, epitaph r10008, the inittab affinity line)
  and takes `DBMFIX_SO=` to add the shim + wrapper + `.desktop` edit.  It is
  **verified by dry run and not yet run** (needs root for the loop mount).

## Previous state — 2026-11-24 (session 69)

**The GPU client path is fixed and validated on the panel.**  Session 68 made
Wayland clients *select* the SGX (the `wl_drm` shim); this session found and
fixed the two defects that stopped them from *presenting*, and the desktop now
runs with the clients on the SGX.

* **Defect 1 — catacomb bound imported dma-bufs as incomplete GLES2 NPOT
  textures.**  Smithay's `import_egl_image()` never set `GL_TEXTURE_WRAP_S/T`;
  the default `REPEAT` makes a non-power-of-two texture incomplete, and it
  samples opaque black with no GL error.  Client buffers are NPOT (1024x20,
  1024x585).  wlroots sets `CLAMP_TO_EDGE`, which is why phoc worked.  Fixed in
  `rootfs/catacomb/smithay-eglimage-clamp.patch`, catacomb **r10003**.  Measured
  A/B in `tools/bench/egl/pb-gbmsample.c`: no clamp → `first=(0,0,0,255) nz=0`;
  with clamp → `first=(0,255,0,255) nz=64/64`.  [report 269 §4g, trap 347,
  DECISIONS §102]
* **Defect 2 — epitaph hard-coded GL attribute/uniform indices.**  It passed
  `0/1/2` to `glVertexAttribPointer` and wrote its projection with
  `glUniform4f(0, …)`; the PowerVR linker assigns `a_Position=2, a_UV=1,
  a_Flags=0, u_Projection=1, u_Texture=2`, so the projection was never set
  (glyphs collapsed) and the write to location 0 hit the sampler
  (`GL_INVALID_OPERATION`).  A one-shot framebuffer histogram inside the client
  showed the whole bar as one colour `(24,24,24)`.  Fixed in
  `rootfs/epitaph/bind-attribs.patch`, epitaph **r10008**.  [report 269 §4h,
  trap 348, DECISIONS §103]
* **Validated on the panel (operator):** wallpaper, keyboard and the bar's text
  and icons all render on the SGX, and the UI is responsive — steady-state bar
  draw **1.5–30 ms** (softpipe was 1.6–2.3 s), 1024x585 drawer 97 ms.
  **The desktop now runs with the clients on the SGX**; `/etc/catacomb/initrc`
  is the pvr variant (no `unset`) and the `wl_shm` safe state is retired.
  [report 269 §4i]
* **Bench verified at the close:** region 0 media trailer
  `1f 2d c8 d7 13 d6 47 35` = CRC **`0x3547D613`** (matches
  `boot/chainloader/kernel.img`, 10,448,868 B, sha256 `6ecd068d…`); region 1
  sha256 **`20a5edd3…`** (read fresh, 64 MiB at `0x29000000`); hd3 ext4 magic
  **`0xEF53`**, 15,273,984 blocks; root `/dev/loop0` rw; `BOOTCTR_FCTRY =
  0x22`; `nproc` 2, PID 1 mask 3; SSH up.  **No region-0 change this session.**
* **Installed on the live root:** catacomb `1.5.0-r10003`, epitaph
  `1.4.0-r10008`, `/etc/catacomb/initrc` (pvr, 3168 B), and
  `/usr/share/wayland-sessions/phoc-pb.desktop` (the comparison session).
  **The hd3 baked image still has catacomb r10000, epitaph r10005 and no pvr
  initrc** — a reflash reverts the whole result (trap-343 class); the rebake is
  owed.
* **Open:** trap **349** (a re-rendering pvr client fails on its second buffer
  in 3/5 runs under catacomb, never under phoc — a pvr allocation defect), trap
  **350** (cosmetic pixel glitches on shm/dmabuf clients, prime suspect the
  bring-up-era upload/damage path), a heavy-redraw measurement, and the hd3
  rebake.

## Previous state — 2026-11-23 (session 68)

> **Superseded by session 69** (the block above): session 68 solved the
> *selection* gate and left the clients black; session 69 fixed the two
> presentation defects.  The text below is that session's own snapshot.

**The Wayland-client GPU wall is identified AND broken: it was the missing
`wl_drm` global, and with pvrports' `phoc` a Wayland client gets the SGX.**
Report 269.

* **Why clients were software (measured + source):** Mesa 21.3.9's Wayland EGL
  platform gets its DRM fd **only** through the legacy `wl_drm` protocol
  (`dri2_initialize_wayland_drm()` bails at `dri2_dpy->wl_drm == NULL`,
  `platform_wayland.c:1567`).  **catacomb (Smithay) advertises no `wl_drm`** —
  `WAYLAND_DEBUG=1` shows `wl_shm` and `zwp_linux_dmabuf_v1` but no `wl_drm` —
  so `eglInitialize()` silently retries with `ForceSoftware = TRUE`
  (`eglapi.c:640`) and loads `swrast_dri`; `MESA_LOADER_DRIVER_OVERRIDE` is only
  read by `loader_get_driver_for_fd()`, which that path never reaches.  First
  EGL debug line is `MESA-LOADER: dlopen(…/swrast_dri.so)`, with or without the
  override.  **Smithay has no `wl_drm` implementation at all** (source tree
  checked), so catacomb cannot be fixed by a setting.
* **The GBM/surfaceless path re-confirmed:** `gbmtest2.py` on `renderD129` →
  `EGL 1.4`, 12 window configs, `PowerVR SGX 540`,
  `EGL_MESA_image_dma_buf_export`; surfaceless the same.
* **Stock phoc 0.59 proved the wlroots path**: its DRM backend trained on
  `card0` (omapdrm) and its GLES2 renderer on the SGX (`GL renderer: PowerVR
  SGX 540`), then died at `GL_EXT_unpack_subimage not supported` — the exact
  thing pvrports patches.
* **pvrports' phoc (0.52.rc1, `pkgrel=9992`) built and ran**: it advertises
  `wl_drm`, modesets LVDS-1 at 1024x600, and a **Wayland-EGL client under it
  with `MESA_LOADER_DRIVER_OVERRIDE=pvr` gets `GL_RENDERER = PowerVR SGX 540`
  and maps `pvr_dri`** (12 ES2 configs, dmabuf export); without the override it
  falls back to `swrast`/`softpipe`.  Proof: `local/work/waylandtest.py`.
* **catacomb now serves a PRIME-only `wl_drm`; clients SELECT the SGX but their
  frames do NOT present (open).**  The operator rejected a Phosh desktop (too
  heavy for 1 GB), so the fix went into catacomb: a **PRIME-only `wl_drm`
  global** (`src/protocols/wl_drm.rs`), shipped as catacomb **1.5.0-r10002**
  (`wl-drm-prime.patch`, 432 lines).  The selection works: `global(32,
  "wl_drm", 2)`; `waylandtest.py` → 12 ES2 configs, `PowerVR SGX 540`,
  `pvr_dri`; `epitaph` logs `GL renderer "PowerVR SGX 540"`; the clients'
  linear `AR24` dmabufs (stride 4096, `modifier: Invalid`) import without
  error.  **But a `grim` capture shows only the compositor's chrome — the
  client surfaces are black**, while a `wl_shm` client displays.  Cause not yet
  separated (the client's pvr render not landing in the shared buffer, or a
  black pvr texture sample).  Without the override a client falls back to
  `swrast`.  [report 269 §4c–§4f, DECISIONS §101]
* **Safe operational state:** the compositor (`catacomb`) keeps the SGX;
  `/etc/catacomb/initrc` **unsets `MESA_LOADER_DRIVER_OVERRIDE` for its
  children**, so the clients use `wl_shm`/softpipe and the desktop **displays**
  (wallpaper, status bar, keyboard; verified with `grim`).  Backup
  `/etc/catacomb/initrc.session68-pvr`; the `wl_drm` global stays advertised
  but unused.  The pvr-node bind-mount experiment was reverted (it made the
  clients exit) and the trace env file was removed.
* **Bench:** catacomb `1.5.0-r10002` running on hd3 (the live root; the baked
  image still has r10000 — **a reflash reverts the patch and the initrc**,
  trap-343 class).  pvrports' phoc 0.52.rc1 is installed but unused (its
  `phoc-test.desktop` was removed); tinydm points at `catacomb.desktop`.  A
  host HTTP proxy (`local/work/httpproxy.py`, `169.254.0.2:8080`) served the
  unit's plain-HTTP repositories without host root; WLAN credentials are in the
  gitignored `local/work/wifi.conf`.  The clean reboot read `BOOTCTR_FCTRY =
  0x23`, `nproc` 2, PID 1 mask `3`, battery Full ~4.1 V.
* **Next:** establish whether the pvr dmabuf **client** render lands in the
  shared buffer (a presenting pvr client under a wlroots compositor) or the
  compositor's pvr texture sample is black; then try the wl_drm GEM-name path
  or a compositor-owned copy.  After that: the hd3 rebake with catacomb r10002
  + the initrc change, a heavy GPU workload/latency, then the owed
  CPU/throughput/USB items.  The fully open `powervr_sgx` stack stays on the
  roadmap as the long-term fix (with a current Mesa it needs no `wl_drm`).
* **No region-0 change, no patch change, no kernel/flash work this session.**

## Previous state — 2026-11-23 (session 67)

**The second core was doing no userspace work at all — fixed; the mailbox fault
is fixed too.**  Two findings this session, in order.  First, the mailbox fault
was a TWL6030 comparator lifetime bug: `twl6030_usb_probe()` stored
`&twl->comparator` in the PHY before `ldo_init()`, a deferred `vusb` was
collapsed to `-ENODEV`, the probe failed permanently, devres freed the `twl`,
and `phy->comparator` dangled (report 265).  Then, looking at the live unit,
**PID 1 was affinity-masked to `{0}`**, so every userspace process inherited
`{0}` and `nproc` reported **1** even though `online = 0-1` and
`SMP: Brought up 1 node, 2 CPUs` (report 266).  Only kernel threads had `0-1`.
This is why report 263 §4b's "two cores in parallel" scaled flat — CPU 1 was
never given userspace work.

* **Region 0 = `boot/chainloader/kernel.img`, CRC `0x3547D613`** (10,448,868 B,
  sha256 `6ecd068d…`) — kernel `7.3.3 #109 SMP`, `-march=armv7-a`, no `maxcpus`,
  **stock MUSB mailbox + comparator fix**, DSI-init TCON hook, inert
  `console=ttyGS0`, `panic=0` (trap 335).  Flashed at block `0x825`
  (`COMPLETE 0x40C0 -> cmd 0x4006`, `done`).  The previous `0xC9FEE2D1`
  (`#105`, device-mode-only workaround, `panic=10`) is the known-good fallback
  on the host.
* **The CPU-affinity workaround is on hd3's root, not in the image.**
  `/etc/inittab` carries `::sysinit:/usr/sbin/taskset -p 3 1` **before** the
  OpenRC entries; PID 1 then runs `{0,1}`, `nproc` = 2, and a two-thread busy
  loop spreads load evenly (`cpu0` 2643 / `cpu1` 2622 user ticks).  Backups:
  `/etc/inittab.session67-orig` (OpenRC default) and `/etc/inittab.fixed`.
  Verified 2 boots with it and 3 without.  **Still owed: a kernel-side fix** for
  `sched_init_smp()`'s widening (see report 266 §1).
* **`cpufreq-dt` is bound and scales** (`300000 600000 800000 1008000`,
  `ondemand`); the handoff's "no `cpufreq` policy" was stale.  The CPU **supply**
  is still unattached (no `supply-*` in the OPP debug dirs), and the
  `twl6030_uv_to_vsel: OUT OF RANGE` lines are startup VC init clamped to VSEL
  `0x3A` = 1.35 V — the same voltage the chainloader commands.  SmartReflex MPU
  is present but idle (`autocomp = 0`).  [report 266 §2/§3]
* **Region 1 and hd3 were read back this session, not assumed.**  Region 1
  (64 MiB at raw `0x29000000`) now hashes **sha256 `20a5edd3…`** — drifted from
  the recorded `6f5b8ca7…` (which itself drifted from `49e318bf…`), consistent
  with read-back boots having mounted it rw; it is not booted.  hd3 is still the
  Nura ext4: ext4 magic `0x53EF` at `0x49000438`, `Block count 15273984 × 4096`
  = **62.56 GB** (`df`'s 56.5 G is usable blocks in GiB), mounted
  `/dev/loop0 on / type ext4 (rw,relatime,nobarrier)`.  No 626 MB write this
  session; no restore needed.
* **Region 0 was re-verified on media this session**: its trailer read back from
  `/dev/mmcblk1` at raw `0x8100000 + 10448868 − 8` is `1f 2d c8 d7 13 d6 47 35`
  = CRC **`0x3547D613`**, byte-identical to `boot/chainloader/kernel.img`.
* **`BOOTCTR_FCTRY = 0x24`** at the close (the mailbox validation boots read
  `0x2a → 0x29 → 0x28`; the affinity half's reboots took it to `0x24`, still
  above the `0x1e` top-up threshold).  `/init` tops up below `0x1e`.
* **Patch = 67 files / 9 136 lines**, sha256 `d19adff8…`, 0 rejects / 67/67
  identical; adds `phy-twl6030-usb.c` + `phy-omap-usb2.c`, and `omap2430.c` now
  runs the stock call (mutex kept).  Tree walk (trap 211) clean outside the
  known excluded diagnostics.  `kernel/source/` mirrors refreshed.
* **Bench:** Linux/Nura on hd3; **`PB_LINK=usb tools/pbssh.sh` works** (ping
  0.5 ms).  It appeared broken for most of the session because the **host's
  WireGuard tunnel** blackholed `169.254.0.1` (`ip rule ... lookup 51820` with a
  `default dev utun420` before `lookup main`); with the VPN off it is fine, and
  `ufw allow in/out on enp8s0f3u1i2` is needed too.  [report 267 §1, traps
  341/342]  The VUSB teardown (trap 337) is unchanged and open.
* **hd3's `epitaph` was the broken `r0`** (segfault ~120 ms in, trap 292's
  symptom); `epitaph-1.4.0-r10005` is now installed (trap 343), and it reports
  `softpipe` — the session-60 llvmpipe `swrast_dri.so` is still absent from hd3.
* **The SGX works on the GBM/DRM EGL platform; only the Wayland-client path does
  not select it** (report 267 §3, corrected): `eglGetPlatformDisplay(
  EGL_PLATFORM_GBM_MESA, <renderD129>)` gives `EGL 1.4`, **12 window configs**,
  `GL_RENDERER = PowerVR SGX 540`, and `EGL_MESA_image_dma_buf_export` present.
  A Wayland client with `MESA_LOADER_DRIVER_OVERRIDE=pvr` still gets `swrast`;
  the reason is **not yet established** (an earlier "libEGL only supports
  wayland" claim was a bad grep and is withdrawn — it supports
  wayland/x11/gbm/surfaceless/device).  The kernel side is healthy: `pvrsrvkm`
  is loaded and `catacomb` maps `pvr_dri` + `gbm`.  **Session 68 is dedicated to
  this**; the plan, the seven outside sources and their value are in
  **report 268**.

## Previous state — 2026-11-19 (session 66)

**The mailbox fault's bad indirect transfer is identified; the working image is
back on region 0.**  HANDOFF-65 §0d's one experiment was run: an instrumented
stock-mailbox kernel (winchester early return removed, mutex kept, `panic=0`)
with `PBMBX` `printascii` markers was booted twice on dual-core, and the hung
boot's DRAM ring was recovered through a warm reset + the quiet read-back
image.  **Entry pointers are valid** — `status=4` (`MUSB_VBUS_OFF`), valid
`xceiv`/`otg`, and `otg->set_vbus = 0xc0729b94` = `omap_usb_set_vbus` kernel
text — so the "stale `usb_phy` at entry" branch is ruled out.  Branch markers
show `sv` still valid right before `otg_set_vbus()`, then the Oops:
`PC = 0xc2c05800`, a `kmalloc-1k` slab address, `LR =
omap_musb_set_mailbox+0x1e0`; no `PBMBX prechain`, so it is not the notifier
chain.  Disassembly pins the fault to `omap_usb_set_vbus()`'s tail-call
`phy->comparator->set_vbus`: the TWL6030 `phy_companion` registered into the
`omap-usb2` PHY is stale/corrupt (a PHY-lifecycle fault, one level below
`musb->xceiv`).  [report 264; session-66 note; capture
`docs/captures/musb-mailbox-fault-2026-11-19.txt`]

* **Region 0 = `local/work/kernel-armv7-console-cpu1.img`, CRC `0xC9FEE2D1`**
  (10,450,108 B) — **restored** after the experiment (marker images
  `0xF1AA4568` / `0x405E8997` and the read-back image `0x5BBCD22E` were
  temporary).  The same session-65 ARMv7 `#105 SMP` dual-core image: fast
  chainloader, loud DTB **without `maxcpus`**, DSI-init TCON hook,
  device-mode-only mailbox workaround, inert `console=ttyGS0`.  The operator
  saw the Nura desktop boot cleanly after a few reset cycles; the USB link then
  dropped at ~60 s (the intermittent VUSB teardown, trap 337) while the unit
  kept running.  `boot/chainloader/kernel.img` was reset to this saved file.
* **Region 1 = the BusyBox ext4 `6f5b8ca7…`** at `0x29000000` (unchanged,
  fallback, not booted).  **hd3 = the Nura ext4 `07555bde…`**, grown to 62.5 G
  with the ARMv7 modules (unchanged — no hd3 write this session).
* **`BOOTCTR_FCTRY` not re-read this session** (the USB link was down at the
  close); last known `0x26`.  `panic=0` prevented a trap-335 loop, so no
  counter drain occurred.  Read `dmesg | grep pbrpmb` on the next SSH-capable
  boot as a health check; `/init` tops up below `0x1e`.
* **Bench:** Linux/Nura on hd3, not TabletOS.  dropbear on `169.254.0.1:22`
  when the gadget is up (`PB_LINK=usb tools/pbssh.sh`); at the close the USB
  link had dropped (~60 s after boot) and the panel console is the recovery
  path — a cable replug does not re-enumerate (trap 337).  No persistent
  device-side files were deployed this session (`/tmp/ringdump` and the ring
  dumps were on the temporary read-back image and are gone).
* **Build tree reverted and proven:** `omap2430.c` and the board DTS match
  their pre-experiment backups byte-for-byte, the rebuilt `vmlinux` has zero
  `PBMBX` strings and the DTB bootargs are back to `panic=10`.  No tracked
  driver/DTS change; the export stays **65 files / 9 110 lines**,
  sha256 `6f9bef24…`.  **No restore to TabletOS required.**

## Previous state — 2026-11-18 (session 65)

**The mailbox mutex is not the fix, the kernel is now ARMv7, and hd3 runs the
Nura root.**  Serializing the two `omap_musb_set_mailbox()` callers with a glue
mutex and restoring the stock bind-path call **still panics** on dual-core: the
first dual-core boot of `0x19E24D8F` faulted in `omap_musb_set_mailbox` and
`panic=10` rebooted it ~6 times, draining `BOOTCTR_FCTRY` `0x22 → 0x1c`.  It was
recovered by reflashing `kernel-usbdev-cpu1.img` (`0xC675E206`), whose `/init`
topped `0x1c → 0x32` (`PASS`).  The device-mode-only workaround stays until the
real cause is known (the surviving overflow points at corruption of the
`usb_phy`/`usb_otg`, not just the two-caller race).  [report 263; session-65 note]

* **Region 0 = `local/work/kernel-armv7-console-cpu1.img`, CRC `0xC9FEE2D1`**
  (10,450,108 B) — `#105 SMP`, **`-march=armv7-a`**, fast chainloader, loud DTB
  **without `maxcpus`**, DSI-init TCON hook, device-mode-only mailbox
  workaround.  The only change over `0x158D8A85` is an inert
  `console=ttyGS0,115200` (the gadget tty has no kernel-console path; no
  `console [ttyGS0] enabled`).  Dual-core `online = 0-1`, 1008 MHz; the 6/6
  reboot set was run on `0x158D8A85`, and `0xC9FEE2D1` booted once for the
  **376 s soak with the gadget link alive**.
* **The kernel had been ARMv6K**, not ARMv7: `CONFIG_ARCH_MULTI_V6=y` selected
  `CPU_ARM1176 → CPU_V6K → CPU_32v6`, and `arch/arm/Makefile` lets the v6K
  `-march=armv6k` / `__LINUX_ARM_ARCH__=6` override v7 while `CPU_32v6` is set.
  Disabling `ARCH_MULTI_V6` (OMAP4430 only) gives `-march=armv7-a` and module
  vermagic `ARMv7 p2v8`.  The tracked `kernel/winchester-7.3.3.config` now
  matches the working tree (`CONFIG_SMP=y`, `CONFIG_NR_CPUS=2`, no
  `ARCH_MULTI_V6`).
* **hd3 = the Nura root.**  The BusyBox ext4 that had been flashed there was
  **damaged by power cycles** (`EXT4-fs: This should not happen!! Data will be
  lost`, corrupt inode bitmap) — the rw root is mounted `nobarrier`.  The baked
  Nura image (`local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img`,
  sha256 `07555bde…`, 625,999,872 B) was written at loader block `0x4915`
  (626,065,440 B, `FLASH_OK`).  First boot grew it to 15,273,984 blocks
  (**62.5 GB**), installed dropbear (crng init at 13.1 s, no seed needed) and
  the ttyGS0 root shell.  Its pre-SMP modules were replaced with the ARMv7 set
  (29 MB, `depmod` OK); after that `pvrsrvkm` binds (`Enabling quirks`),
  `catacomb` runs, panel `connected`, backlight 255, `online=0-1`.
* **Region 1 = the BusyBox ext4 `6f5b8ca7…`** at `0x29000000` — the fallback
  root; not booted.  hd3 no longer holds a BusyBox root.
* **`BOOTCTR_FCTRY` = `0x26`** (last boot, `0x26 ≥ 0x1e`, no write).  The panic
  loop drained it `0x22 → 0x1c` and the recovery top-up restored `0x32`.
  Healthy; `/init` tops up below `0x1e`.
* **The USB teardown is intermittent, not an idle timer.**  It killed the link
  at ~30 s on the BusyBox root (with a command every 15 s) and at ~90 s on Nura
  (SSH traffic), but did **not** occur on the final boot: 240 s of continuous
  ACM reads (`cat /dev/kmsg` streamed to the host) followed by 90 s with the
  port closed ended at `uptime 376 s`, link alive, no VUSB message.
  `audio-ldo: disabling` at ~31.8 s is a separate regulator event.  Only
  `twl6030_usb_irq()` disables `usb3v3`; the trigger is still unproven.
* **Bench:** SSH works on Nura — `PB_LINK=usb tools/pbssh.sh 'cmd'` (root,
  key-only, `169.254.0.1:22`); the ttyGS0 shell is `playbook:~#` (not `pb> `,
  trap 274 still applies to `pbcon.py`).  At wrap-up the unit is **up and on
  USB** (uptime 376 s), running the desktop.  Flash: start the flasher, then
  reset/power on; the power-on bootrom is caught reliably.
* **No restore required.**  The way back is the 1917 IFS at block `0x825` plus
  the vendor UFS at `0x4915`; region 1 is intact.

## Previous state — 2026-11-17 (session 64)

**The dual-core wedge is the USB gadget bind, and a targeted MUSB fix makes
dual-core boot reliably.**  The IPI markers (HANDOFF-63 §0a) showed the
TLB-flush IPI is sent, received and serviced — so the "IPI race" reading is
superseded.  Panel diagnostics (a DSI-init TCON hook, report 261) exposed a
real kernel panic in `omap_musb_set_mailbox` from the gadget bind performed by
`/init`; a second mode was a `pbrpmb` hang.  Disabling the gadget entirely gave
no wedge (>100 s), proving the trigger.  The targeted fix — call only
`omap_control_usb_set_mode(USB_MODE_DEVICE)` from `omap2430_musb_enable()` on
winchester, skipping the faulting mailbox — gave **3/3 clean dual-core boots**
(`kernel-usbdev-cpu1.img`, `0xC675E206`).  [report 262; session-64 note]

* **Region 0 = `local/work/kernel-usbdev-cpu1.img`, CRC `0xC675E206`** — `#102
  SMP` zImage, fast chainloader, no-`nosmp` loud DTB, DSI-init TCON hook, MUSB
  device-mode fix.  Dual-core `online = 0-1`, 1008 MHz, root ext4 on hd3.
  Region 1 = the busybox ext4 `6f5b8ca7…` at `0x29000000` (inherited, not
  re-hashed this session); hd3 = the same ext4 at `0x4915`, mounted rw.
* **`BOOTCTR_FCTRY = 0x2c`** at the last boot (`2c000000`, above the `0x1e`
  threshold, no write) — healthy after the session's wedge cycles (read at
  `0x31`, drained to `0x18`, topped to `0x32`, then `0x29`/`0x2d`/`0x2c`).
  Linux `/init` tops it up; no manual write was needed.
* **OS: Linux**, not TabletOS.  dropbear on `169.254.0.1:22` after a
  `pbconsole entropy <hex>` seed (trap 328); ACM `pb> ` is the fallback.  At
  wrap-up the unit is **off USB** after ~32 s idle (`VUSB: disabling`, gadget
  deactivated — idle runtime PM, not a wedge; the unit keeps running with its
  panel console).  Cable replug did not re-enumerate; separate PM matter.
* Device-side files: `/tmp/ringdump` (tmpfs, ephemeral).  **No restore required**;
  the way back to TabletOS is the 1917 IFS at block `0x825`.
* **Bench gotcha:** the ACM wraps at ~80 columns and `echo <hex> > /dev/random`
  credits only ~21 bits per short write; seed with `/bin/pbconsole`'s
  `entropy <hex>` driven via `tools/pbraw.py` (trap 328).  On the read-back
  image the ACM is the bring-up console, so `entropy <hex>` works directly.
* **Ghidra MCP**: the server lacked `GHIDRA_INSTALL_DIR`; `rea doctor` is
  healthy with it set.  This session used the REA CLI + bundled `pyghidra`.

## Current state — 2026-11-17 (session 63)

**TRAP 326 IS A MISDIAGNOSIS — the "SMP-only ext4 mount failure" was a rootfs-
offset difference between two images, not an SMP kernel bug.**  The SMP image
(`kernel-smp-quiet.img`, `#78 SMP`) carries an initramfs with
`ROOTFS_RAW_OFF = 0x49000000` (hd3); the UP read-back image (`#79`) uses
`0x29000000` (region 1).  hd3 held the 1917 vendor UFS (QNX6), so `mount -t ext4`
failed correctly.  Proof: the failing boot's loop capacity changed
`0 → 124583936 → 122191872` sectors — a drop of exactly `0x49000000` bytes;
`qnx6 49000000` mounted hd3 read-only as QNX6; `initramfs/init.c:452` has
`ROOTFS_RAW_OFF = 0x49000000`.  After flashing the region-1 ext4 (`49e318bf…`)
to hd3 at `0x4915`, the **same SMP kernel** mounts `/dev/loop0 on / type ext4`
and boots a full userland.  [report 256; trap 326 corrected]

* **The MPU runs at 1008 MHz via the fast chainloader.**  `boot/chainloader/
  chainloader_kernel.S.in` now commands VDD_MPU = 1.35 V (VC bypass) and programs
  the MPU DPLL to 1008 MHz at entry, before the copy/XZ decompress.  Kernel entry
  is **3.276 s of 7.812 s** (decompression 2.959 s, down from 6.6 s), and the
  clock persists through boot (`dpll_mpu_ck = 1008000000`,
  `CM_CLKSEL = 0x0080D207`).  A runtime `clk_set_rate` measured a **2.54×**
  CPU speedup (13.63 s vs 34.66 s); **1.2 GHz crashes the unit**.  Single-core
  boot is now reliable (3/3 clean reboots).  [report 257 §1–§3; DECISIONS §91]
* **CPU1 works; dual-core boot is flaky, not a cpu1 bug.**  `PBCPU1:` markers
  through `post complete`, `online cpus=02`, `nproc=2` and `twd` interrupts on
  both cores were measured; dual-core 1008 MHz booted cleanly once (`#91`,
  stable >2 min) but a 3-boot set was 1 success / 2 wedges.  The wedge is a
  clean `execve("/init")` hang — a timing/race in the IPI/TLB-flush path
  (traps 329 corrected, 330).  Power is ruled out: MPU rail `0x3A` = 1.35 V,
  battery 97 % / 3.819 V (trap 331).  [report 257 §4–§5]
* **`nosmp` is not a `nr_cpu_ids=1` test** (trap 327): `nr_cpu_ids` stays 2
  (`SLUB: CPUs=2`); a real test needs `CONFIG_NR_CPUS=1` or a DTB with `cpu@1`
  dropped.  **SSH needs an entropy seed** each fresh boot (trap 328): dropbear
  blocks in `wait_for_random_bytes`; `pbconsole entropy <hex>` fixes it.
* **Bench at the wrap-up (verified off the media)**: region 0 =
  `local/work/kernel-fastcl-nosmp.img`, CRC **`0x8B83421F`** (media trailer at
  raw `0x8B10198` = `1f2dc8d7 1f42838b`), cmdline `loglevel=0 nosmp
  clk_ignore_unused panic=10 fw_devlink=permissive`, `possible = 0-1`,
  `online = 0`, 1008 MHz; root `/dev/loop0` ext4 **on hd3** (58.7 M, 42 % used).
  Region 1 = the busybox ext4, sha256 `6f5b8ca7…` (drifted from `49e318bf…`
  from read-back boots mounting it rw).  dropbear on `169.254.0.1:22` after an
  entropy seed; `pb> ` on the ACM; `BOOTCTR_FCTRY = 0x20`.  `/tmp` empty (tmpfs,
  nothing deployed).  No restore required; the way back to TabletOS is the 1917
  IFS at `0x825` + `local/work/ufs-1917-devuser.ufs` (`482e0473…`) at `0x4915`.

## Current state — 2026-11-16 (session 62)

**THE SMP KERNEL NOW BOOTS TO USERSPACE (`possible = 0-1`, `online = 0`) — the
blocker was the loader's 7.8125 s watchdog window, not a hang.**
`+0x1000` never had a writer of its own: it is the ring's own character array at
**character #3840**, the first byte past the old chainloader clear, so every
early-death log was erased by the read-back boot.  The chainloader now **hands
the ring off** (append-only, last two entry headers + timing saved, trap 321) and
`tools/pb_ring.py` decodes it.  With that read: **`rcu_init()` completes** (every
`PBDBG` marker through `rcu_init done`), the earlier stop points (3208/3943/4211
chars) were the watchdog, and the fix was measured and applied:
**kernel entry happened at 7.450 s of 7.812 s (362 ms left)** — chainloader copy
0.825 s + XZ decompression **6.619 s** — while the kernel needs ~0.74 s to reach
its own WDT2 re-program.  A **block copy** in the chainloader (0.83 s → a few
hundred ms) plus a **quiet boot** (`loglevel=0`) buys that time, and the SMP
kernel then boots.  [reports **255**; traps 312 corrected, 315 corrected, 317a
closed, 321–326 added]

* **The remaining SMP blocker is the root mount** (trap 326): the first SMP boot
  fell back to the initramfs console with
  `EXT4-fs (loop0): VFS: Can't find ext4 filesystem`, while the **same**
  region-1 filesystem mounts cleanly on the UP image minutes later
  (`/dev/loop0 on / type ext4 (rw,relatime,nobarrier)`, 58.7 M, 42 % used).  The
  FS is intact; the loop/ext4 bring-up differs under SMP.  Next: `SMP=y` +
  `NR_CPUS=1` and `nosmp`, and a retry/delay in the initramfs loop setup.
* **The instrument** (verified on the bench, byte-exactly):
  `boot/chainloader/chainloader_kernel.S.in` no longer clears the ring; it
  sanitises a garbage header (`index >= 0x7F00` → 0), records `RHND` at `+0xb8`
  with the last two entry headers (`+0xbc..+0xc8`) and the previous boot's
  timing (`+0xcc` pre-jump CRR, `+0xd0` kernel-entry CRR, `+0xd4` entry stamp),
  and copies in eight-word `LDMIA`/`STMIA` bursts.  Chainloader is 852 B.
* **Images this session** (host, gitignored; media holds the read-back):
  * region 0 now = **`local/work/kernel-readback-quiet.img`, CRC `0x5BBCD22E`**
    (10 355 580 B): region-1 kernel `#79`, `earlyprintk` removed from its DT
    `bootargs`, the hand-off + block-copy chainloader.  Boots to a shell.
  * `local/work/kernel-smp-quiet.img` (`0xA2FA490E`) — **the SMP image that
    boots** (plain SMP `#78` zImage + `loglevel=0` DTB + chainloader).
  * `local/work/kernel-smp-loud.img` (`0x3BD64F22`) — same with
    `earlyprintk`/`ignore_loglevel`: expected to work now that the kernel owns
    the watchdog, **not yet booted**.
  * `local/work/kernel-smp-mark62.img` (`0x0A70D405`) — `#86 SMP` with the dense
    `PBDBG` set and the `head.S` CRR stamp; its boot gave the timing above.
  * `local/work/kernel-smp-wdt.img` — **bad**, contains the rejected
    chainloader WDT step (trap 322); do not flash.
  * Re-wrapping a carved `zImage`+DTB reproduces `0x202E9F38`, `0x4F6CC563` and
    the region-1 image **byte-identically**, so chainloader-only changes need no
    kernel build (`local/work/carve/*`, `carve.py`).
* **Do NOT program WDT2 from the chainloader** (trap 322): it wedged the boot
  and left the timer stopped — no USB, no reset, dark panel; recovery was a
  power-button power cycle plus a reflash through the power-on bootrom.
* **Bench at the wrap-up (read on the unit):** the unit runs the read-back image
  (`7.3.3 #79`, UP), root `/dev/loop0` (58.7 M, 42 % used), `pb> ` on the ACM,
  dropbear on `169.254.0.1:22` (`PB_LINK=usb tools/pbssh.sh`); `/tmp` empty
  (tmpfs — re-push the tools).  **`BOOTCTR_FCTRY = 0x28`** (the session's failing
  boots drained it from `0x32`; one normal boot read `0x1d` and wrote `0x32`
  earlier, `PASS`).  Region 1 = the busybox root (intact, mounts, 43 modules
  that still do not match the config — trap 320).  hd3 = the 1917 vendor UFS.
* **Bench laws that cost time this session:** the bootrom can hold **~20 s**
  before the chainloader runs, so a read-back flash armed 10 s after the test
  flash catches the bootrom *before the image under test has run* (trap 323) —
  wait ≥25 s or poll `lsusb`; a **power-off wipes the ring**; and
  `kernel-smp-marks{,2}` are **silent by construction** (trap 324).
* **The tree** carries this session's instruments (they are gitignored and must
  be removed before any patch re-export): the `PBDBG` marker set in
  `local/kernel-tree/init/main.c` (pristine copy at
  `local/work/carve/main.c.session62-orig`) and the WDT2 `CRR` stamp in
  `local/kernel-tree/arch/arm/kernel/head.S`.  `.config` is now
  **`CONFIG_SMP=y`, `CONFIG_NR_CPUS=2`**; the tracked
  `kernel/winchester-7.3.3.config` is still non-SMP (the change is an
  experiment until cpu1 works).

## Previous state — 2026-11-16 (session 61)

The ring method was built and the SMP kernel was found to boot and die early;
**its conclusion ("stops inside `rcu_init()` after the RCU geometry print") is
withdrawn this session** — that was the ring artefact (trap 315/317a).  Also:
the oracle measured **dual-core at 1008 MHz** (`pidin info`), `SMC` from QNX
user space is merely `SIGILL` (privileged instruction, trap 319), the SCU and
every register the SMP path touches read cleanly from non-secure Linux, and
hd3 was given up to the 1917 vendor UFS to get the oracle.  Full snapshot:
`docs/sessions/session-61.md`, report 254.

## Previous states

* **session 60** (2026-11-16) — the `epitaph` panel fixed (r10005), the drawer,
  the llvmpipe swap latency, and the one-core 400 MHz state.  The full snapshot
  is in [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
  (`## Previous state — 2026-11-16 (session 60)`).
* **session 59** (2026-11-16) — the root became writable and grew to 56.5 G,
  SSH came back (kernel `#77`, region 0 `0x294F8E36`), the touch position ghost
  was fixed (kernel `#74`), the eMMC cache-flush item was closed by measurement,
  and wvkbd fixed the OSK repeat.  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
  (`## Previous state — 2026-11-16 (session 59)`).
* **session 58** (2026-11-16) — the root became writable and grew to 56.5 G,
  SSH came back (kernel `#77`, region 0 `0x294F8E36`), the touch position ghost
  was fixed (kernel `#74`); eMMC flush first isolated.  Full snapshot at git
  commit `00228e4` and `docs/sessions/session-58.md`.
* **session 57** (2026-11-16) — apps/background, the touch release fix (kernel
  `#73`), the audio diagnostics gated and the idle suspend removed; a position
  ghost remained.  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
  (`## Previous state — 2026-11-16 (session 57)`).
* **session 56** (2026-11-15) — catacomb runs on the SGX540 (the smithay GLES2
  fallback), the Munin Y axis is fixed, and the SGX driver is vendored; region
  0 `0xAB899C49`, hd3 `89399744…`.  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
  (`## Session 56 snapshot`).
* **session 55** (2026-11-15) — the read-only hd3 + overlayfs
  Nura root booted (region 0 `0xA8F0F30C`, hd3 `1fcf0cb0…`), the eMMC stall
  left the boot path, and the SGX540 kernel driver bound
  (`pvrsrvkm 56000000.gpu`) with the pvrports userspace running EGL/GLES 2.0
  (`PowerVR SGX 540`).  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md).
* **session 54** (2026-11-15) — pmbootstrap built the Nura root, hd3 was flashed
  at `0x4915` and Nura booted, but the eMMC write stall remounted the root
  read-only; the read-only overlay root was implemented and built (reports
  240–242).  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md).
* **session 53** (2026-11-15) — the WL1283 BT core works (a full BR/EDR
  inquiry; the reference's `WL1283.bts` replays 315/315 and makes `hci0`
  register with mgmt), and item 7a (hd3/Nura/SGX) was scoped.  The full
  snapshot is in [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
  (`## Session 53 snapshot`).

The per-session snapshots are in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md):

* **session 52** (2026-11-15) — the WL1283 WLAN associates (the `ccm`/`ctr`
  modules), the USB link gets its DMA, and the BT core answers HCI (the UART2
  CTS/RTS pads); reports 236/237.
* **session 51** (2026-11-14) — the WL1283 WLAN works: the `-110` loop was the
  chip's ELP sleep (now CAM) and the never-completing scan was the 5-sr
  firmware (4-sr scans); reports 232–235.
* **session 50** (2026-11-14) — the WL1283 enumerates (`CLK32KG`) and firmware
  boots; the interface recovery-looped (reports 230/231).
* **session 49** (2026-11-14) — the blocker measured from both sides, the host
  program excluded, the "silent card" instrument corrected (reports 216–227).
* **session 48 and older** — see the same file.

## Per-subsystem state

| subsystem | state | detail / evidence |
|---|---|---|
| Chainloader image (CRC path) | **working** | 828 B loader at image `+0x800` = `0x80100000`; copies zImage/DTB high, checks the DTB magic `0xEDFE0DD0`, jumps (`r0=0`, `r1=~0`, `r2=DTB`). Report 109 §8.1, 103 §5.2 |
| DRAM / memory map | **working** | 1 GB at `0x80000000`; kernel runs from `0x84000000`-loaded zImage, decompressed to `0x80008000`; DTS comment |
| Kernel self-capture | **working** | DRAM ring at PA `0x88000000` (32 KB) + breadcrumb/event log; `tools/pb_ring.py`, `tools/pb_events.py`. Bootstrap §3 |
| WDT2 | **working — 58.6 s window, 10 s pet (session 31)** | The loader arms `WLDR = 0xFFFC1800` (7.8125 s); the kernel reprograms it to the reference's `0xFFE2B400` (58.6 s) at the late initcall via `WSPR` (`0xAAAA`→`0x5555` stop, write `WLDR`, `0xBBBB`→`0x4444` start), **polling `WWPS`** after each write, and pets every 10 s (`TGR` read→invert→write); fallback 2 s if the readback fails.  Verified on `0xE36C679A` by `WCRR` reload timing (~10 s) and uptime >275 s.  The expected dmesg line is missing on 7.3.3 (printk anomaly).  Report 109 §8.1 + correction; trap 115; decision §38 |
| eMMC (MMC2) | **reads proven; writes healthy — but writers must use large blocks** | `mmcblk1` = MCGAFA 59.4 GiB, `boot0`/`boot1`/`rpmb`; needs the `vemmc` fixed supply for `ocr_avail` (report 109 §9).  Session 16 read a freshly flashed 11 152 064-byte image back **byte-exactly** and matched a pre-flash TabletOS capture (report 134); the MMC2 functional clock is 96 MHz.  Session 17 measured the write path (report 136): the driver has no request-size limit — an O_DIRECT write gives **256 KiB `CMD25`s, 5.5–7.0 MB/s** (4 KiB chunks: 0.93 MB/s), one `CMD13` per request (normal `mmc_blk_card_busy`).  The old 17–32 KB/s was `recv`'s tiny buffered writes plus 4–7 unthrottled `printk`s per request to **fbcon**.  Use ≥64 KiB (ideally 256 KiB–1 MiB) writes with the MMC trace off.  **Session 18: region 1 now holds a bootable Linux root** (32 MiB ext4 at raw `0x29000000`), written through the pre-OS `0xF7` loop (2055 frames) and read back byte-exactly (QNX `dd` and Linux `blk sumb`); it mounts read-only via a loop and its `/init` runs (report 140).  **Session 54 (report 241): the hd3 writable root stalls this path.**  The first ext4 journal/udev write after the 597 MiB bootrom image gives `mmc1: Card stuck being busy! __mmc_poll_for_busy`; the loop write fails, the journal aborts, and ext4 remounts `emergency_ro`.  A later 4 KiB write to scratch hd3 returns `I/O error`; reads are fine.  The region-1 root is **no journal** and stable.  Workaround: read-only hd3 + overlayfs (`CONFIG_OVERLAY_FS=y`); **session 55 (report 243) confirmed it: the overlay boot has no `Card stuck`, no journal abort, the lower `ro without journal` and all writes in the tmpfs upper.**  The eMMC write-latency/cache fix is a separate experiment. |
| USB device + gadget | **working — DMA now actually used (session 52)** | enumerates `0fca:8010`: CDC-ACM (console) + **RNDIS** (network) in one configuration; High Speed (480 Mbps).  **Session 52 (report 236 §7): the RNDIS path had been running MUSB's PIO fallback** — `dma_channel_program()` refuses buffers that are not 4-byte aligned on RTL 2.00 (`MUSB_HWVERS = 0x0800`), and u_ether hands it 2-mod-4 addresses.  Fixed with `musb->g.quirk_avoids_skb_reserve = 1` (RX) and a TX realign in `gether_start_xmit()`: **RX 3.3 → 6.3 MB/s, TX 3.3 → 4.1 MB/s** (raw `nc`).  The RNDIS function's one-frame-per-transfer structure is now the limit; CDC-NCM is the untested next step.  The ACM console is still only ~134 KB/s and **drops bytes when the host blasts** (report 136 §3.3) — do not send multi-MB images through it.  Reports 114, 136, 236 |
| **WL1283 (MMC5)** | **WORKING — associates, leases and passes traffic (session 52); scans (session 51)** | **Session 52 (report 236): the association blocker was the missing `ccm`/`ctr` crypto modules.**  `ieee80211_key_alloc()` allocates a `ccm(aes)` AEAD for every CCMP key; without it the PTK install returns `-ENOENT` and wpa_supplicant mislabels it `WRONG_KEY` (the handshake itself is fine — message 3 decrypts).  `ccm.ko`/`ctr.ko` ship in region 1 and `rcS` modprobes them before the stack: `wpa_state=COMPLETED`, WPA2-CCMP on 5785 MHz at −41 dBm, MCS7/65 Mbit/s, DHCP **192.168.0.212**, gateway and 1.1.1.1 reachable.  A 5 GHz AP needs `iw reg set <CC>` (the world domain marks 5735–5835 MHz `PASSIVE-SCAN`).  The **per-unit NVS** is shipped (`wl128x-nvs-perunit.bin`).  **Session 51 (report 232): the recovery loop was the firmware's ELP sleep.**  The failing transfer is the **FW-status read** — CMD53 to `0x1efc0`, 84 bytes, byte mode, 256-byte block — from the WL_IRQ thread right after `firmware booted`; it fails only once the chip has slept, and the identical read succeeds when it is awake.  Fix: `wl12xx_default_conf.conn.sta_sleep_auth = WL1271_PSM_CAM` (was `ILLEGAL` → ELP).  Verified after a real region-1 flash + reboot: one firmware boot, zero failures, `wlan0` `UP`, debugfs `sleep_auth` 0.  `wl128x-fw-4-sr.bin` is the **shipped** SR firmware (report 235): 5-sr (7.3.10.0.142) boots but panics on the first scan, so `wl12xx`'s floor is lowered to 7.3.5.0 and `WL128X_FW_NAME_SINGLE` names the 4-sr file; both files stay installed.  **WLAN (report 231):** `wlcore`/`wl12xx`/`mac80211`/`cfg80211` + `wl128x-nvs.bin`, the `ti,wl1283` child (`reg = <2>`, WL_IRQ gpio 53), `cap-power-off-card`, and an `mmc-pwrseq-simple` pulsing `WL_EN` (low → 50 ms → high → 120 ms) → firmware `Rev 7.3.5.0.98`, MAC `14:74:11:8f:9c:c3` (the assigned address, patched into the generic NVS; the radio calibration is still default).  **Enumeration (report 230):** the TWL6030 `CLK32KG` 32.768 kHz SLOW_CLK is physical `0xBC`–`0xBE` and was OFF; `CONFIG_CLK_TWL=y` + an enable in `clk-twl`'s probe (~1.9 s, before MMC5 at ~3.0 s) fixes it.  **Remaining (report 235):** the WLAN **associates** and passes traffic (session 52); open and not blocking use are *why* the 5-sr firmware panics on scan (235 §3) and *why* the ELP wake is not honoured (232 §6).  **Session-49/50 negatives (do not repeat):** the reference's MMCHS5 program and WL_EN ordering replayed, the stock-TabletOS diff, the boot-path RE, the clock sweep (reports 217/218/221–229).  The BT half's enable is `BT_EN` = gpio 55 = `GPIO2_23`, held low while the WLAN works (report 216). |
| **WL1283 BT** | **the CORE WORKS — real inquiry; `hci0` now registers with mgmt (session 53)** | UART2 `0x4806C000` = `/dev/ttyS1`, H4 at 115200 8N1; `BT_EN` = gpio 55 = `GPIO2_23` (pad `0x4A10007C` mux 3), pulsed low → 50 ms → high.  **Measured (report 238):** the core answers `HCI_Reset`, reports `hci ver 6` / `lmp ver 6` / `mfr 0x002b` (TI, BT 4.0), and **runs a full BR/EDR inquiry that finds a peer** (`f0:a3:b2:bb:d1:00`, class `0x011c00`), reproducibly.  **The reference's `WL1283.bts` loads 315/315** and moves LMP subversion `0x2b0f` → `0x2b33`; loaded **before** the attach it makes `hci0` **register with mgmt** (0 → 1 controller, negative control both ways) by fixing the `0x201c` reply length.  **Left:** opening the controller — a stray `0x30` byte arrives ~200 ms after the last init reply and the kernel's H4 receiver correctly rejects it as not-an-H4-type (`-EILSEQ` → `Frame reassembly failed (-84)` → `SET_POWERED` `0x05`); the instrumented `h4_recv()` shows it in 6 of 6 runs, but **which side emits it is open** (it never appears from userspace).  Then ship `l2cap`/`sco`/`rfcomm`/`hidp` (not in region 1 yet) and a stack.  **LE is refused by the controller**, though the chip reports BT 4.0 with the LE-supported bit — no known sequence, and impossibility not demonstrated.  The pack survives `HCI_Reset` but not a `BT_EN` power cycle.  Tools in `rootfs/bt/`.  Reports 216/237/238, traps 248–252, DECISIONS §72/§73 |
| **GPIO1 pads / board lines** | **fixed (session 49): three pads were driving GPMC alternate functions** | `0x4A100050` (`gpmc_ad0/1`) and `0x4A100074` (`gpmc_ncs0/1`) were left at mode 0, so pad logic drove GPMC bus lines on a board with no GPMC device while gpiolib could not touch them (trap 228); `0x4A31E058` (GPIO1_31) was unmuxed with its input buffer off.  The DTS now sets mode 3 + pull-down and hogs GPIO1 0/18/28/31 low, so `GPIO1_OE = 0x6FFB1FBA` against the reference's `0x6FFB1FAA` (the missing bit 4 is pad `0x058`'s low half = GPIO2_4, owned by `bridge_pins`, trap 229) and `GPIO1_DATAOUT` is identical.  Image `0x0114FFD3`.  **Not** the WL1283 blocker (card still silent).  Report 220 |
| **Munin touch (CY8CTMA3)** | **FIXED — the position ghost and the lost burst taps are gone (session 58, kernel `#74`)** | I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` boots the app and maps the 12-bit report through the 1917 `scaling.conf` calib (raw x 172→1108 → 102→922, y 110→658 → 60→540) to 1024×600; IRQ gpio 35 = GPIO2_3 pad `0x056` = `0x410B`, edge-falling, `irq 110` (~174/s idle).  The controller asserts its line for the whole gesture and reuses slots, so the driver re-reads a torn report until the counter parity matches, **keeps polling while the line is asserted (and for 200 ms after the last contact)**, and **holds a new down back one sample when it repeats the slot's previous coordinates within 300 ms**; `irq-gpios` gives it the line level (traps 296/298).  Contacts are separate and fast typing is correct (report 248).  The backspace/enter repeat was squeekboard's, now fixed by wvkbd (trap 299); power/reset GPIO1_14/GPIO4_8 are not driven.  `pb-touchwake.py` wakes a manually blanked panel; auto-blank removed (session 59 — DPMS resume wedges DSI).  Reports 198/247/248/250, DECISIONS §47/§82/§83/§85 |
| **WM8994 audio** | **WORKING — stereo 440 Hz on headphones AND speakers at 48 kHz; diagnostics `dev_dbg` (session 57)** | I²C4 `0x1a` (`0x8994`, revision D); the board machine driver makes `0 [WM8994]: PlayBook_WM8994`.  **Data path (session 46):** the codec's AIF1 is on McBSP3 (the oracle's routing log `AIF 0 Downlink: MM_DL_PORT->MM_EXT_OUT_PORT->MCBSP3`, the live `PCR0 = 0x3`, the PDM pads `0x106/0x108/0x10a/0x10c` = `0x0109/0x0009/0x0109/0x0109`); the card is two ASoC links (`40124000.mcbsp + wm8994-aif1` clocks, `CBC_CFC`; `40126000.mcbsp + wm8994-aif2` data, `CBP_CFP`); the codec is the AIF1 slave, FLL1 locks from our BCLK (`R0x302 = 0x0000`, `R0x732 = 0x6020`, `R0x200 = 0x0011`), no MCLK1; the `omap-mcbsp` channel-layout guard keeps `XCR2 = 0x8041`.  **Speaker (session 47, report 215):** the speakers are on the codec's **line outputs**, so the output stage follows the jack: headphones → HPOUT1 + charge pump (`R0x060 = 0xcc`); jack out → LINEOUT1P/2P (PM3 `0x3cf0`, `R0x01e = 0`, `R0x034/0x035 = 1`, `R0x036 = 0`, `R0x038 = 0x80`, `R0x04c = 0x1f25`, `R0x060 = 0`) with **`spk` (GPIO2_12) driven low** and **GPIO1_15 (the `audio_boost` enable) high**.  **Session 48** removed the `pcm:` ring readback and the `live N:` FIFO dump and re-verified both outputs at DAC1 volume 40: `jack out (speaker)` / `jack in (headphone)`, "done, FLL1 locked", both PCMs `RUNNING`, the full byte count written, `spk` low during the speaker route and high on the headphone route, `boost` high.  Reports 214/215, DECISIONS §59/§60.  **Session 57:** the remaining info-level diagnostics (`omap_mcbsp_dump_reg`, the per-stream `rate/AIF1CLK` and `repair:` lines) are `dev_dbg`, and the PM-QoS remove is guarded, so a failed 48 kHz `hw_params` no longer taints the kernel (trap 294); the 48 kHz rejection itself is open. |
| Userspace (initramfs) | **working, minimal** | `/init` = PID 1, mounts, creates the configfs gadget (**ACM console + RNDIS**, session 48), forks `/console` in the fallback path.  **No auto-reboot since session 15** — PID 1 reboots only on request (the timer spent a `BOOTCTR_FCTRY` unit per iteration).  **Session 28:** after the gadget and the boot-time top-up, `/init` mounts the region-1 ext4 root **read-write**, bind-mounts /dev//proc//sys, chroots and execs `/sbin/init`; with no `/sbin/init` it falls back to the console.  Report 176, session-28 note |
| **USB network + ssh** | **working — commands and interactive sessions (session 48); 2.3 MB/s is the ssh bound, not the USB one** | The gadget carries an **RNDIS** function next to ACM, so the host sees four interfaces and a net device (`enp8s0f3u1i2`, MAC `02:1a:11:00:00:02`); the region-1 `rcS` brings `usb0` up at **`169.254.0.1/16`** and starts **dropbear** listening on `169.254.0.1:22` only.  Key-only auth, host keys shipped, password/PAM/shadow off.  **`tools/pbssh.sh 'cmd'` works**; measured before session 52 at 2.3 MB/s, which is dropbear's cost — the raw path is **6.3 MB/s (host→device) / 4.1 (device→host)** after the session-52 DMA fix, so use raw `nc`/`socat` for bulk copies and ssh for command access.  An **interactive** session works under a real terminal (`script -qec` from this side); an agent's own shell has no tty, so an apparent "closes with no output" is a harness artefact (trap 216).  DECISIONS §62. |
| Userspace (region-1 root) | **working — a real BusyBox userland with ssh, the WLAN stack and the BT stack** | 64 MiB ext4 at raw `0x29000000`; BusyBox 1.36.1 + init + 401 applet links + `pbconsole`/`pbrpmb`/`pbplay`/`pbmix` + `/lib/modules/7.3.3`; boots to `sh -l` on `/dev/ttyGS0` and serves **dropbear on `usb0`**; `insmod`/`modprobe` proven.  Current image: `local/images/rootfs-region1-wl1283-bt.ext4` (sha256 `49e318bf…`, **43 modules** — the sensor/charger/audio set, the wireless stack with **`ccm`/`ctr`**, the seven Bluetooth modules, both wl128x SR firmware files, the per-unit NVS, `iw`, `wpa_*` and the signed `regulatory.db`, session 52).  Earlier: `…-wl1283-final.ext4` (`877c42c3…`, 36 modules), `…-wl1283-assoc.ext4` (`6c2cfb30…`), `…-wl1283-scan.ext4` (`f159a221…`), `…-rndis-ssh.ext4` (`a4e76b18…`) and the plain userland `0xAEDF68D0`. |
| Console | **working** | `help ls cat peek poke kick disp bl i2c i2c16 spi fb kmsg grep blk recv sink blkwr qnx6 rootfs write reboot`; `spi tcon` (low-first) and `spi tconbe` (high-first); `blk info/sum/sumb/rd` is read-only FNV-1a64 over `/dev/mmcblk1` (**`blk info` prints size 0 B — `fstat` bug, see KNOWN-ISSUES**), `grep <substr> <path>` scans any-size text (for `clk_summary`); `sink <len>` measures the link, `blkwr <off> <len> [chunk]` is a timed O_DIRECT write with a deterministic pattern (report 136); `qnx6 [off-hex]` mounts hd3 read-only (report 138); `rootfs [off-hex]` mounts the region-1 ext4 root read-only on `/rootfs` (report 140); argument limit 24.  `pb_reg` op set is now `w a:v` / `r a` / `t a` (16 reads, re-arm) / `u a` (16 reads, no re-arm) / `d from:to` (range dump) / `v` (video liveness).  Every hardware access forks with a bounded wait |
| QNX6 read access (hd3) | **working, read-only** | `CONFIG_QNX6FS_FS=y` + console `qnx6 [off-hex]`: a read-only loop over `/dev/mmcblk1` at raw **`0x49000000`** (superblock `0x49002000`) mounted qnx6 on `/mnt`.  Verified on `0xFC77CD47`; the old `0x48F00000` offset gives `wrong signature (magic)` from the driver.  Report 138 |
| **SGX540 / PowerVR** | **WORKING — catacomb composites on the GPU (session 56)** | The `openpvrsgx-devgroup` 7.3 driver (DDK 1.17.4948957) is in-tree and bound (`pvrsrvkm 56000000.gpu`); `catacomb` 1.5.0 runs with a smithay 0.7.0 GLES2 fallback (`GL_EXT_unpack_subimage` absent → per-row/full-row uploads) and maps `libGLESv2_PVR_MESA`/`libsrv_um`/`libusc`/`pvr_dri.so` (no `swrast`); `squeekboard` composites.  The OSK key-press path still janks (open).  The driver is vendored at `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz`.  Report 246. |
| DSS / DISPC via omapdrm | **working — and the core clock is now right** | `/dev/dri`, `/dev/fb0`, `card0-LVDS-1` connected+enabled, `LINE_STATUS` advancing (37 lines/ms), GFX overlay scanning the painted buffer.  The DSS functional clock, which is the DISPC's **core** clock, was left at 48 MHz by the boot chain while the pixel clock is 50.4 MHz — that was the last display defect.  `dss_probe()` now asks the PER PLL for the reference's rate and gets **96 MHz**.  **Session 18:** the boot-time `pb` bridge-read probes are behind `pb_probe` (default **off**), so a normal boot has **no DSI CIO errors**; the DSS rate-mismatch WARN is gone and the real rate is recorded (report 142).  Report 133 §2/§3 |
| DSI1 link | **byte-identical to the reference, PLL included** | `DSI_CTRL = 0x00EAEF9D`, `CLK_CTRL = 0xA0346005`, `TIMING1 = 0x79190999`, `TIMING2 = 0x7FFF7FFF`, `VM_TIMING1/2/3`, `CLK_TIMING = 0x00000E0B`, `VM_TIMING7/8`, PHY, PLL `CFG1 = 0x14A5E8BE` / `CFG2 = 0x00056000`.  All re-read live in session 15. Reports 127 §4, 132 §6 |
| TC358764 bridge | **programmed; configuration now identical to a working system** | The whole 353-register map was read from **TabletOS with its display working** (`tools/pbdsi`) and diffed against ours.  After fixing `VP_HTIM1/2` (§ below) only four registers differ, three of them live status/counters.  `IDREG = 0x00006500`, `LV_CFG = 1`, `LV_PHY0 = 0x0004C02D`, `VP_HTIM1 = 0x00780001`, `VP_HTIM2 = 0x00760400`, `VP_VTIM1/2`, `VFUEN` self-clears. Report 132 §2/§5/§6 |
| Bridge **reads** | **reliable — with `PPI_TX_RX_TA = 0x00040006`** | The value is latched by `PPI_STARTPPI (0x0104)`, so it must be right **in `pb_bridge_seq`**.  Boot with `4` → 4/8 at boot, 8/16 sampled; boot with `0x00040006` → 8/8 and 16/16.  Live writes after boot change nothing.  The VC re-arm does not affect the rate. Report 132 §3 |
| Backlight (LM3532 @ `0x38`) | **working** | binds, `bl_power = 0`, brightness 255; gate is `GPIO2_16`. Report 120 |
| Panel TCON (MCSPI4) | **working — sent automatically at bring-up** | The 9-word Hitachi table is sent by `/init` right after the display comes up (high byte first, 450 kHz, 100 ms after words 0/2/last), matching the reference's own order (`winch_lcdctl 0x193c4`).  Without it the panel stays black even with the bridge programmed. Report 133 §3/§7 |
| Panel identity | **Hitachi (definitive)** | on TabletOS: `GPIO6_DATAIN = 0x00010800` (bit 16 = 1), slog `LCD is Hitachi`.  Report 127 §1 |
| **Image on the panel** | **WORKING** | With `0x8C146805` (and the same source as `0x068C867F`) the panel shows a clean, stable image: solid white, solid red, and the kernel console as legible text.  The three necessary fixes were the bridge delivery (VC0/HS null packet), the LVDS porches + TCON table at bring-up, and the DISPC core clock. Report 133 |
| QNX oracle tool | **`tools/qnx-dsiread.c` → `tools/pbdsi`** | Reads any TC358764 register over DSI from a TabletOS user process (generic read request `0x24` on hw VC1, BTA, RX FIFO pop; response is a generic long read, DT `0x1A`).  Read-only.  This is how the reference values in this document were obtained. Report 132 §2 |
| Kernel patch + config | **7.3.3 (session 67 export)** | `kernel/patches/0001-playbook-winchester-7.3.3.patch` is **9 136 lines / 67 files**, sha256 `d19adff8…`, 0 rejects / 67/67 identical against pristine 7.3.3.  Session 67 adds `drivers/usb/phy/phy-twl6030-usb.c` + `drivers/phy/ti/phy-omap-usb2.c` (the comparator lifetime fix) and restores the **stock `omap_musb_set_mailbox()` call** in `omap2430.c` (mutex kept); the board DTS moves to `panic=0`.  Session 65 added `drivers/usb/musb/omap2430.c` (device-mode-only enable + `set_mailbox_lock`), the DSI-init TCON hook in `dsi.c`, and the board DTS without `maxcpus`; the session-62/63/64 markers, the `head.S` `CRR` stamp and the `MINI4x6` font link are not in it.  Session 58 fixes the Munin touch position ghost (`playbook-munin.c`: poll while the line is asserted + the fresh-down confirm; the board DTS gains `irq-gpios`; report 248).  Session 54 makes the `PB*` prints `pr_debug` (config `CONFIG_DYNAMIC_DEBUG=y`, `CONFIG_OVERLAY_FS=y`); the rebuilt image is `0xE84693A1` (built, not flashed).  Session 52 adds `drivers/usb/musb/musb_gadget.c` (the `quirk_avoids_skb_reserve` flag) and `drivers/usb/gadget/function/u_ether.c` (the TX realign); the 40 MHz experiment in `wl12xx/main.c` was reverted and is not in the patch.  Session 51 added `wl12xx/main.c` (CAM + the 4-sr firmware name) and `wl12xx.h` (the SR floor 7.3.5.0).  Session 49 added the report-220 board-DTS GPIO1 pad fix and `arch/arm/Kconfig.debug` (missing from every export since session 30 — the tree walk found it, trap 211 class).  Session 48 retires the two diagnostic blocks and adds `sound/soc/ti/omap-mcbsp-priv.h`, which every export since session 41 had omitted although `omap-mcbsp.c` reads the fields it declares (trap 211 — the patch could not compile from pristine until now).  Session 47 added the speaker path (line-output route, `spk`/`boost` GPIOs, the `disp-en-12-hog` removal); session 46 the `&l4_abe` ABE-refclk assignment, the E5 clock-role change, the McBSP3 two-link card and the `omap-mcbsp` channel-layout guard; session 39 the board machine driver (`sound/soc/ti/playbook-wm8994.c`, Kconfig/Makefile), the `wm8994-core.c` runtime-PM quirk and the board `sound` node/codec clocks, and it restored `drivers/input/touchscreen/{Kconfig,Makefile}`, which the 48-file export had silently omitted (trap 154).  Config: `kernel/winchester-7.3.3.config` (SMP, `CONFIG_NR_CPUS=2`, no `ARCH_MULTI_V6`; includes `CONFIG_CLK_TWL=y`, `CONFIG_CHARGER_ISL9519=y`, `CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`, `CONFIG_KERNEL_XZ=y`, `CONFIG_SND_PCM_OSS=m`, `CONFIG_SND_MIXER_OSS=m`).  The 6.15.11 patch (`5 703 lines / 34 files`) stays as the bisect reference; it is **incomplete** — it omits `cm3217.c`, `leds-fan5702.c`, `pbmon-pair.h`, `pbmon-page.h` and their Kconfig/Makefile lines (trap 106) |
| **ISL9519 charger** | **WORKING under Linux (session 31)** | I²C1 `0x48070000`, slave `0x09`; VendorID `0x0049`, DeviceID `0x0003`.  `isl9519.c` (built-in, DTS `charger@9`) sets the four registers in RIM's order with the masked fields and refreshes every 5 s.  Measured **+358 → +397 mA**, 3.775 → 3.794 V, capacity 8 → 9 %, `Charging`; still on at 197 s (watchdog).  Registers read back `0x14=0x0280`, `0x15=0x1070`, `0x3E=0x0D00`, `0x3F=0x0180`.  Report 183 |
| TWL6030 BCI | **probes after the session-31 fix; NOT the board's charger** | `twl6030_charger` probes with the `simple-battery` profile and sets `CONTROLLER_CTRL1=0x10`, `VOREG=0x21`, `VICHRG=0x08`, `CINLIMIT=0x09`, but the stock OS leaves the whole block at bootloader reset values — it charges through the ISL9519.  The BCI node/modules are inert; `twl6030_usb` reports `ONLINE=1`, VBUS 4.81–4.91 V |

## Current images

The unit table below is authoritative for what is on media.  The durable record
is the CRC and what it demonstrated, **not** the file.  **Retention rule
(operator, session 70):** keep the **latest dual-core image**, the **last working
single-core image**, the **read-back instrument**, and the **freshest hd3
filesystem** — everything else is rebuildable from the recorded CRC and is
deleted rather than accumulated.  All QNX assets (`*.ifs`, `*.rcfs`, `*.ufs`) are
kept regardless: they are the way back.  `LOCAL-PAYLOAD-INVENTORY.md` records
what was deleted and what remains.

**Only five kernel images are retained** (session-70 cleanup): the current
dual-core `0x3547D613` (in `boot/`, tracked by name), the single-core
`0x8B83421F`, the read-back instrument `0x5BBCD22E`, and the two fallbacks
`0xC9FEE2D1` / `0xC675E206`.  **The table
below is a historical record of what each image demonstrated — most of those
files are no longer on disk.**  A CRC is the durable artefact; the file was
rebuildable and was deleted (`LOCAL-PAYLOAD-INVENTORY.md` §7).

| image | base | role |
|---|---|---|
| `0x3547D613` | 7.3.3 | **current region 0 — ARMv7 dual-core, stock MUSB mailbox** (session 67: 10,448,868 B, `#109 SMP`, `-march=armv7-a`, no `maxcpus`, inert `console=ttyGS0`, **comparator lifetime fix**, `panic=0`; 3/3 clean dual-core boots, gadget bound) |
| `0xC9FEE2D1` | 7.3.3 | session-65 region 0 — `0x9F74BC` B, `#105 SMP`, device-mode-only mailbox workaround, `panic=10` (known-good fallback; 6/6 boots, 376 s soak) |
| `0x158D8A85` | 7.3.3 | session-65 ARMv7 base (`0x9F74A8` B, same kernel/DTB without the console bootarg; 6/6 clean boots) |
| `0x19E24D8F` | 7.3.3 | **rejected** session-65 mutex experiment — stock `omap_musb_set_mailbox` with a glue mutex still panics on dual-core (`panic=10` loop) |
| `0xBEF06B78` | 7.3.3 | session-65 mutex build that booted single-core (DTB still had `maxcpus=1`) |
| `0xC675E206` | 7.3.3 | session-64 dual-core image — the device-mode-only mailbox fix (known-good fallback; used to recover the panic loop) |
| `0x294F8E36` | 7.3.3 | earlier region 0 — read-write root + SSH + grown FS (session 58: `0x9E0980` B, `boot/chainloader/kernel.img` sha256 `f0738625…`, kernel `#77`: mounts hd3 rw, installs dropbear, `barrier=0` + write-through, one-shot `resize2fs`) |
| `0xED58EF36` | 7.3.3 | previous region 0 — the touch position-ghost fix (session 58: `0x967CE0` B, sha256 `c67b75c5…`, kernel `#74`) |
| `0xB9DD91E3` | 7.3.3 | previous region 0 — the touch poll-on-touch fix (session 57: `0x967398` B, `boot/chainloader/kernel.img` sha256 `5ac47496…`, kernel `#73`) |
| `0x23D88CBD` | 7.3.3 | session-57 experiment — both-edge IRQ (no interrupts, touch dead; trap 297; `0x967800` B) |
| `0x373674A4` | 7.3.3 | session-57 experiment — the `PBTS` trace kernel (`0x967AA8` B) |
| `0xA544A731` | 7.3.3 | session-57 first touch fix — the parity re-read (`0x967CB0` B) |
| `0xAB899C49` | 7.3.3 | previous region 0 — the overlay init with `&sgx_module` and the Munin Y fix (session 56: `0x9676A0` B, sha256 `c37ee2fe…`, kernel `#69`; `local/images/kernel-0xAB899C49-touch.img`) |
| `0xA8F0F30C` | 7.3.3 | previous region 0 — the overlay init with `&sgx_module` (session 55: `0x967880` B, sha256 `fb9905f3…`, kernel `#68`; `local/images/kernel-0xA8F0F30C-sgx.img`) |
| `0xE85217E5` | 7.3.3 | previous region 0 — the overlay init without SGX (session 55: `0x9662A4` B, sha256 `fbeb6df0…`; kernel `#67`; `local/images/kernel-0xE85217E5-overlay.img`) |
| `0xE84693A1` | 7.3.3 | **built, NOT flashed** (session 54: the PB prints `pr_debug`/dyndbg, `PBREQ`/`PBIRQ` behind `pb_trace`; `CONFIG_DYNAMIC_DEBUG=y`, `CONFIG_OVERLAY_FS=y`; `0x967A94` B, sha256 `663348d0…`; `local/images/kernel-0xE84693A1-quietpb.img`) |
| `0x060B25F2` | 7.3.3 | previous region 0 (session 54: `ROOTFS_RAW_OFF=0x49000000`, the `lstat` hand-off, read-write root; `0x939D04` B, sha256 `b548ec6c…`) |
| `0x8B709950` | 7.3.3 | session-54 intermediate (`ROOTFS_RAW_OFF=0x49000000` with the old `access()` hand-off; `0x939D5C` B, sha256 `835fa863…`) |
| `0xDD66D8C3` (readback) | 7.3.3 | the session-52 image, read back before the hd3 switch and CRC-verified (`local/images/kernel-0xDD66D8C3-readback.img`, sha256 `06e61587…`) |
| `0xDD66D8C3` | 7.3.3 | session 52 (the UART2 **CTS/RTS pads** — the BT core answers HCI; `0x939E4C` B) — no longer current region 0 |
| `0x50CD2A48` | 7.3.3 | session-52 (the musb HSDMA fix — `quirk_avoids_skb_reserve` + the u_ether TX realign; `0x939E3C` B) |
| `0x5C37F181` | 7.3.3 | session-52 intermediate (the TX realign, with the one-shot `PBDMA` diagnostic still in) |
| `0x5A7F15D8` | 7.3.3 | session-52 intermediate (the RX quirk + the diagnostic) |
| `0x9712A086` | 7.3.3 | previous region 0 (session 50: the CLK32KG fix + the `ti,wl1283` child, `cap-power-off-card` and the pwrseq; kernel `#60`) |
| `0xF64A680D` | 7.3.3 | session-50 intermediate (`+cap-power-off-card`; the second firmware boot still failed) |
| `0x53F7F97B` | 7.3.3 | session-50 intermediate (`+ti,wl1283` child) |
| `0x69CDAC94` | 7.3.3 | session-50 first fix (`CONFIG_CLK_TWL=y` + `clk32kg` enable; the card enumerates) |
| `0x17C2E119` | 7.3.3 | previous region 0 (session 50: the session-49 tree rebuilt after the reverted experiments; kernel `#59`) |
| `0x91E22899` | 7.3.3 | session-50 experiment (the slow-identification-clock test: `f_min=100000` + the archived `PBX`/`PBIRQ`/`PBCMD` trace; kernel `#58`) |
| `0xBAFD9B3B` | 7.3.3 | previous region 0 (session 49: the session-48 tree + report 220's GPIO1 pad fix; kernel `#57`) |
| `0x0114FFD3` | 7.3.3 | session-49 experiment (the GPIO1 pad fix, kernel `#51`) |
| `0x71A79419` | 7.3.3 | session-49 intermediate (the session-48 tree rebuilt after the WL1283 experiments were reverted; kernel `#50`) |
| `0x12C8C320` | 7.3.3 | previous region 0 (session 48: the RNDIS + dropbear initramfs; kernel `#45`) |
| `0xAD85DF0F` | 7.3.3 | previous region 0 (session 47: the speaker path — line outputs, `spk`/`boost` GPIOs, `disp-en-12-hog` removed; kernel `#44`) |
| `0xF8BADFF3` | 7.3.3 | previous region 0 (session 46: McBSP3 two-link card, kernel `#42`) |
| `0xE5E2AEAF` | 7.3.3 | session-47 intermediate (jack-aware speaker state; `spk` still a hog) |
| `0x47ED0450` | 7.3.3 | session-46 experiment: mcbsp1 as the data engine (still stalled; pads input-enabled) |
| `0x58F85337` | 7.3.3 | previous region 0 (session 45: the committed E5 tree; `#40`) |
| `0x5A04ACD9` | 7.3.3 | session-45 E5 restore (`#35`) |
| `0xE5A13DE7`, `0x6AD9E204` | 7.3.3 | session-45 E9 two-link experiments (data port stalls — §14) |
| `0x92458D00` | 7.3.3 | session-45 E5 + the disabled `&mcbsp1` node (experiment, not on media) |
| `0x3DC4508D` | 7.3.3 | session-45 E6 experiment (McBSP1 as the port; codec clock lost) |
| `0xA66B40EF` | 7.3.3 | session-45 E7/E8 (two PCMs; raw clock pokes crashed the SoC — see trap 193) |
| `0x7A5DD941` | 7.3.3 | session-45 E5 as first built (McBSP bit/frame provider, pads back to outputs) |
| `0xB43F169B` | 7.3.3 | previous region 0 (session 44: reference codec-GPIO init — boot-on `audio-ldo` + pulse, hp input) |
| `0x7C56BDC0` | 7.3.3 | previous region 0 (session 44: `CM_ABE_PLL_REF_CLKSEL` = SYS_CLK + DPLL_ABE 98.304 MHz; E1 clock roles) |
| `0x36597040` | 7.3.3 | previous region 0 (session 42: E1 clock roles — codec AIF1 master, McBSP consumer, input pads) |
| `0xB8830701` | 7.3.3 | previous region 0 (session 41: AUXCLK1 DTS fix, McBSP dump) |
| `0xE36C679A` | 7.3.3 | known-good region 0 (WDT2 + ISL9519 charger) |
| `0x0F6AC47F` | 7.3.3 | last full-regression image (sensors, CM3217) |
| `0x0D1DA0D3` | 6.15.11 | clean pre-userland fallback |
| `0x8C146805` | 6.15.11 | first image with a correct display |

Full image history and the older per-image notes are in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md).

Do not boot `0x5E40C5C1` or `0xB9A1BC4F` (two lanes), or `0x3B04298E`
(LM3532 without VAUX3).

## The unit

| item | value |
|---|---|
| working units | **one: the reference (`0x50175127`)**, armed AND sealed.  The spare (`0x50176EC1`, BSN `1063043093`) is **down** with the 1-2-2 blink = error `0x5B` (report 153) and presents no USB.  Two 32 GB units are bricked and parked |
| region-0 IFS on media | **the ARMv7 dual-core image `boot/chainloader/kernel.img`, CRC `0x3547D613`** (10,448,868 B, kernel `7.3.3 #109 SMP`, `-march=armv7-a`, the **stock MUSB mailbox + comparator lifetime fix**, `panic=0`, the DSI-init TCON hook), region 0.  The previous `0xC9FEE2D1` (`#105`, device-mode-only workaround, `panic=10`) is the known-good fallback; the session-65 ARMv7 base is `0x158D8A85`; the session-64 known-good dual-core image is `0xC675E206`; the session-58 read-write/SSH image `0x294F8E36` (`0x9E0980` B, kernel `#77`) remains on the host.  The way back is the 1917 IFS (`local/work/ifs-shim-sealed.ifs`, 7,900,548 B, sha256 `b52195bb…`); the compressed `local/work/ifs1917-compressed.ifs` (sha256 `f14db207…`) is the alternative.  Earlier images (the `0xED58EF36`/`0xB9DD91E3` touch, `0xAB899C49` compositor, `0xE85217E5` overlay, `0x060B25F2`, `0xDD66D8C3`, …) remain on the host.  Earlier Linux images (region 0 only, block `0x825`): `0x23D88CBD`, `0x373674A4`, `0xA544A731`, `0xAB899C49`, `0xA8F0F30C`, `0xE85217E5`, `0xE84693A1`, `0x8B709950`, `0x50CD2A48`, `0x5C37F181`, `0x5A7F15D8`, `0x9712A086`, `0xF64A680D`, `0x53F7F97B`, `0x69CDAC94`, `0x17C2E119`, `0xBAFD9B3B`, `0x91E22899`, `0x12C8C320`, `0xAD85DF0F`, `0xF8BADFF3`, `0x36597040`, `0xB43F169B`, `0xB8830701`, `0xF97CD9C7`, `0x3B2ECFED`, `0x50842767`, `0x82FC0D35`, the Task-3 `0x0DC36A11`, `0x05819994`, the known-good `0xE36C679A`, and the touch `0x04AF1433` |
| region-0 RCFS (`/base`) | **now the full 1917 RCFS at `0xC00000`** (`local/work/rcfs-1917.rcfs`, the modified one with the `/accounts/devuser/startup.sh` hook, sha256 `c4ab6e34…`): session 49 wrote it to recover root on a stock-TabletOS boot, and it sits **outside** the 7.3.3 test image (which ends at `0x9E0980`).  The pruned oracle RCFS is no longer on media — re-flash `local/work/minimal/setB.signed` for the minimal oracle (trap 227).  Not readable as `/base` without the 1917 IFS |
| UFS (`/`) | **not on media** — hd3 has held Linux filesystems since session 63 and now the Nura ext4; the 1917 vendor `.5.ufs` with `autorun::0` (the session-27 set, `local/work/ufs-1917-devuser.ufs`, sha256 `482e0473…`) is on the host as the way back.  The BB10 `.0.4.ufs` is not on media.  (Not re-hashed this session) |
| `/accounts/devuser/startup.sh` | the 1917 UFS carries the operator's script; it last ran during the session-35 S3/S2 TabletOS bench and topped `BOOTCTR_FCTRY` back to `0x32` |
| `BOOTCTR_FCTRY` | **`0x24`** at the session-67 wrap-up (measured on the unit).  The three mailbox validation boots read `0x2a → 0x29 → 0x28`, then the affinity half's four reboots took it to `0x24`; every read was `>= 0x1e, no write`.  (The session-64 panic loop had drained it to `0x1c`; the recovery image topped it to `0x32`.)  Block `0x3a` record@196.  **Read it on Linux with `dmesg \| grep pbrpmb`** (the top-up line carries the 4-byte LE record).  Self-replenishing: `/init` tops up at <`0x1e`, a TabletOS boot writes `0x32` (`SAFETY.md` §2, `DEVELOPMENT.md` §5) |
| region-1 Linux root | **present** at raw `0x29000000` (region-1 +16 MiB, block `0x2915`): 64 MiB ext4, no journal, on-media sha256 **`20a5edd3…`** (re-hashed at the session-67 close; earlier readings `6f5b8ca7…`, then the staged file `49e318bf…` = `local/images/rootfs-region1-wl1283-bt.ext4`, session 52: the WLAN fixes, **`ccm`/`ctr`**, the per-unit NVS, the CAM+4-sr `wl12xx.ko` and the seven Bluetooth modules), **43 modules under `/lib/modules/7.3.3`** (the sensor/charger/audio set plus **`cfg80211`, `mac80211`, `wlcore`, `wlcore_sdio`, `wl12xx`, `libarc4`, `crc7`** and **`bluetooth`, `hci_uart`, `btbcm`, `crc-ccitt`, `ecdh_generic`, `ecc`, `kpp`**), `pbplay`, `pbmix`, **dropbear**, **`/usr/bin/iw`**, **`/usr/sbin/wpa_{supplicant,cli,passphrase}`**, and **`/lib/firmware/ti-connectivity/{wl128x-fw-4-sr.bin,wl128x-fw-5-sr.bin,wl128x-nvs.bin}`** plus the signed **`/lib/firmware/regulatory.db`/`.p7s`**; `rcS` loads the wireless stack and `bluetooth hci_uart`.  Earlier images: `…-wl1283-final.ext4` (`877c42c3…`), `…-wl1283-assoc.ext4` (`6c2cfb30…`), `a148c362…`, `7e067560…`, `88084b78…`, `a4e76b18…` and `0342ab06…`.  The modules in any of them can be checked by extracting them with `debugfs` (trap 163).  **Session 55: not booted.**  The region-0 image is the hd3-pointing `0xA8F0F30C`; it mounts hd3 (Nura) and never touches region 1.  This root remains the fallback; its `rcS`/dropbear/WLAN/BT are unchanged. |
| TabletOS / OS | **NOT running** — the unit is on the **Nura root from hd3** (image `0x3547D613`, `#109 SMP` dual-core ARMv7, `0fca:8010`, console `playbook:~#` on the USB ACM, **sshd: dropbear on `169.254.0.1:22`**, `PB_LINK=usb tools/pbssh.sh` — **this works** (verified at the session-67 close, ping ~0.4 ms); if it times out the cause is the **host** (WireGuard VPN, trap 341; `ufw`, trap 342), not the device; the root is the hd3 ext4 mounted **read-write**, grown to **62.5 G**, with the ARMv7 module set installed).  TabletOS 2.1.0.1917 returns by flashing `local/work/ifs-shim-sealed.ifs` at block `0x825`; its door is also `169.254.0.1`, so use `PB_LINK=door` for `tools/pbssh.sh` there |
| hd3 (MCT `0x1C`, FS at loader block `0x4915` / raw `0x49000000`) | **the media was re-created on 2026-11-18 (session 65)** by flashing the baked Nura image `local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img` (`07555bde…`, 625,999,872 B) at block `0x4915` (626,065,440 B, `FLASH_OK`), because the BusyBox ext4 that had been flashed there in session 63 was **damaged by power cycles** (`EXT4-fs: This should not happen!! Data will be lost`, corrupt inode bitmap).  First boot grew it to **62.5 G** (15,273,984 blocks) and installed dropbear; the ARMv7 module set was then installed over SSH and `depmod` run.  It is a rw ext4 with `barrier=0` + write-through, so power cuts can still damage it — prefer a clean `reboot`.  The flasher-restorable image is now the **session-71 rebake** `local/work/nura/images/blackberry-playbook-root-gpu-20261125.img` (sha256 `5d3eac8f…`, 625,999,872 B), which carries the session-69/70 GPU result, the ARMv7 modules and the permissions; the session-59 base (`07555bde…`) is untouched.  Its content is the session-59 state: `tabula` 1.0.2, `epitaph` 1.4.0, `tzompantli` 1.2.0, `gorm` 1.2.1, `swayidle` 1.9.1, `foot` 1.28.0, `grim` 1.5.0, `slurp` 1.5.0, `tremor` 1.1.0, `netsurf` 3.11, `xournalpp` 1.3.8, the pvrports stack (`libglvnd`, `mesa-pvr-dri-classic`, `sgx-ddk-um{,-ti443x,-openrc}`, `gcompat`, `wayland-libs-server`), `MESA_LOADER_DRIVER_OVERRIDE=pvr`, the pvrsrvkm module in `/etc/modules`, wvkbd/DejaVu/pb-touchwake, the icon-theme fix and the no-suspend `/etc/catacomb/initrc`.  **The session-71 rebake** (report 272, DECISIONS §105) applies catacomb `r10003`, epitaph `r10008`, the inittab affinity line, the pitch fix, **the live 842-module ARMv7 tree** (the base had ARMv6 — trap 356) and **the permissions set** (polkit, backlight udev, `/etc/deviceinfo` lzo-rle, `playbook` in `video`).  It leaves the bench pvr `/etc/catacomb/initrc` (md5 `00fdff8481e7c4dde730221ef55d53da`) alone, because the APK's copy is the upstream default (trap 357).  `tools/rebake_hd3.py` does this file by file (root loop mount or `debugfs`) and splices the package DB/world from the live root; `tools/rebake-hd3.sh` is the wrapper.  The llvmpipe `swrast_dri.so`/`libLLVM-13` are still absent and are not part of the working state (the desktop runs on `pvr_dri`).  The pre-pvr images (`1fcf0cb0…`, `1f14915f…`), the pristine `076bafcf…` and the older `f7858110…` remain on the host.  See reports 249/250, 263 §3, 270 and **272**. |
| device-side files | **persistent on hd3 (session 58: the root is writable and grown):** the initramfs installs `/usr/sbin/dropbear`, `/etc/dropbear/*`, `/root/.ssh/authorized_keys`, `/etc/local.d/pb-ssh.start` and `/etc/local.d/pb-grow.start` on first boot, and `/etc/pb-grown` marks the grow.  `/var`, `/home` and `/tmp` are now real directories on the 56.5 G FS, so the DE state and the SSH kit survive a reboot (the state files written before the switch are on the grown FS).  `memdump3`/`memw32`/`pbi2c` are outside the tree: push them (paced) after a reflash (trap 177).  The first-Nura-boot log is `docs/captures/playbook-live-2026-11-15-hd3-nura-boot.txt`.  Session 57's touch tools (`evcap.py`, `keylog.py`, `muninraw`) lived only in `/tmp`; the measured evidence is preserved in the captures.  **Session 59 deployed:** `wvkbd-mobintl` 0.21 (`/usr/sbin/wvkbd-mobintl`), `/usr/share/fonts/dejavu/DejaVuSans{, -Bold}.ttf`, `/usr/bin/pb-touchwake`, `/usr/share/icons/default/index.theme` (`Inherits=Adwaita,hicolor`), `adwaita-icon-theme` + `glycin-svg`, a hicolor netsurf PNG, the edited `/etc/catacomb/initrc` (no swayidle, wvkbd + pb-touchwake), and `playbook` in the `input` group (gid 23).  Tools/daemon sources are tracked in `tools/bench/`; the pmaport edits are under gitignored `local/work/nura/pmaports/`. |

The factory boot budget is **self-maintaining in bench use**: every successful
factory/CRC boot spends one unit, and the booted OS puts it back — our Linux
`/init` runs `pbrpmb topup 0x1E`, and every TabletOS boot tops the record to
`0x32`.  **Since session 15 our images no longer reboot themselves**, so a test
boot costs one unit, is replenished at boot, and stays up until asked; it is not
a reason to avoid an experiment.  The hard hazards remain: `0` and `> 0x32` do
not fall back to the product path — they hard-lock the unit (`SAFETY.md` §2) —
and a failed/half-formed write or a flat pack is still a brick risk.

## Blockers in play

**Session 67 — the open blockers are:** (1) the **CPU voltage/Opp** — `cpufreq-dt`
exists and scales, but the CPU supply is unattached and the VC init clamps to
1.35 V (report 266 §2/§3); (2) the remaining **UI performance** — was measured
with CPU 1 starved of userspace (see the closed item), so it needs re-measuring
before it can be worked; llvmpipe's deferred `swap` (420–550 ms per full-screen
drawer frame) and its first-draw JIT are the known costs; (3) **Kumo** needs a
WPEWebKit/WebKit-fork build (not
packaged, report 247 §4); (4) the **intermittent WRITE-busy stall** — not a
cache flush (report 250 §1), mitigated by `barrier=0` + write-through, but it
still fires during write bursts and can damage the rw ext4 if power is cut
(trap 336); (5) the **intermittent USB teardown** (`VUSB` disabled by the TWL
comparator's IRQ; 30 s, 90 s or not in 6 min — trap 337, contradiction file);
(6) the **memory read throughput** — the EMIF is correctly configured
(400 MHz LPDDR2-800, 32-bit, CL 6, power-down off, L3 200 MHz) and the CPU is
at 1008 MHz, but a userspace benchmark reads only ~0.4–0.6 GB/s — **that was
measured single-core, so it must be redone** (report 263 §4b; open).
**Closed in session 67:** the **mailbox fault** — the bad indirect transfer was
a freed TWL6030 `phy_companion` (a failed `twl6030_usb_probe()` on a deferred
`vusb`), fixed by the comparator lifetime change; the stock
`omap_musb_set_mailbox()` call now runs on dual-core (3/3 clean boots; report
265, trap 339), so OTG host mode is reachable again.  Also **the second core
now takes userspace work** — PID 1 was affinity-masked to `{0}` (report 266,
trap 340); the inittab workaround is in place and verified, with a kernel-side
fix still owed.
**Closed since session 60:** the CPU's second core (dual-core stable with the
workaround), the ARMv6 codegen (now `-march=armv7-a`, trap 334), the
**`epitaph` panel** (r10005 — report 251; trap 292); the panel's
**permissions**; the touch position ghost and lost burst taps (kernel `#74`,
report 248); the backspace/enter repeat (wvkbd, report 250 §2); the app-drawer
icons; the eMMC cache item (closed by measurement, report 250 §1).  **Bench
note:** DPMS wake-by-touch works with pb-touchwake, but repeated DPMS resume
wedges the DSI link (black panel — only a reboot clears it), so auto-blank was
removed and the panel stays lit (report 250 §2a/§2b, traps 289/298/301).

**The audio blocker is closed, speakers included, and the diagnostics are
retired (sessions 47/48, reports 214/215).**
`pbplay`'s 440 Hz tone is audible in both ears on headphones and in both
speakers at 48 kHz ±1 % on the working image (`0xBAFD9B3B` + rootfs
`a4e76b18…`), re-verified after the cleanup at DAC1 volume 40.  The data path
is session 46's (AIF1 on **McBSP3**; two links `mcbsp2 + wm8994-aif1` clocks,
`mcbsp3 + wm8994-aif2` data; the `omap-mcbsp` channel-layout guard fixed the
right channel; the ABE is not needed).  The speakers needed the codec's **line
outputs** (not class-D), `spk` driven **low**, and GPIO1_15 (the `audio_boost`
enable) **high** — all three in the driver/DTS.  **Remaining audio items: none
required** — the optional **MCLK1 probe** (trap 165) stays optional, and the
`audio_boost` net is behaviourally confirmed but not schematic-confirmed.

**The WL1283 WLAN ASSOCIATES and passes traffic (session 52, report 236).**  The
session-51 stability fixes hold (CAM, 4-sr) and the association blocker is gone:
**`ccm.ko`/`ctr.ko` were missing**, so `ieee80211_key_alloc()` could not allocate
the `ccm(aes)` AEAD and the PTK install failed with `-ENOENT` — which
wpa_supplicant reports as `WRONG_KEY`.  They are now in region 1 and loaded by
`rcS`.  Measured: `wpa_state=COMPLETED`, WPA2-PSK/CCMP, 5785 MHz, −41 dBm,
MCS7/65 Mbit/s, DHCP lease 192.168.0.212, gateway and 1.1.1.1 reachable.  The
per-unit NVS ships; the country must be set (`iw reg set <CC>`) for 5 GHz APs
the world domain marks no-IR (trap 245).  **Wi-Fi is at its hardware ceiling**
(1×1 HT20, 72 Mbit/s; TX 2.9 / RX 4.4 MB/s) — 40 MHz is measured harmful
(trap 247).  **Open (not blocking use):** *why* 5-sr panics on scan (report 235
§3), *why* the ELP wake is not honoured (report 232 §6), and the occasional
first-auth timeout that needs a `wpa_cli scan` nudge.  **Next for the combo
chip:** BT (`BT_EN` = gpio 55, UART2), then FM/GPS (§"What is next").

**The per-unit WLAN calibration is in the QNX `hd5`
partition** (report 197, session 50) — a TI WL128x radio-parameters INI + a
binary blob, and the assigned MAC is in NVRAM `deviceinfo`
(`14:74:11:8f:9c:c3`, patched into the generic NVS).  It is the source for
`wl128x-nvs.bin` **after** enumeration; it was never the `-110`.

**The touch is fixed (session 58).**  I²C2 `0x48072000` = `i2c-1` at
`0x65`, `playbook-munin.c`, `input0`/`event0`; the 1917 `scaling.conf`
calibration maps to 1024×600, the GPIO2_3 edge IRQ is `irq 110`, and the Y
inversion is in (kernel `#69`).  The driver re-reads a torn report until the
counter parity matches, keeps sampling while the controller asserts its IRQ
line (plus a 200 ms grace period) and, on a new down that repeats its slot's
previous coordinates within 300 ms, waits one sample and reports the fresh
position (kernel `#74`, report 248).  Contacts are separate and the fast-tap
ghost is gone.  The remaining backspace/enter repeat is squeekboard's, not the
driver (trap 299).  The power/reset lines are still not driven by the driver;
only cold boots need the GPIO pulse.

**Media state: region 0 = `0x3547D613`** (kernel `7.3.3 #109 SMP`,
`-march=armv7-a`, 10,448,868 B, no `maxcpus`, the **stock MUSB mailbox** +
comparator lifetime fix, `panic=0`, the DSI-init TCON hook); region 1 = the
WLAN+BT rootfs, **on-media sha256 `20a5edd3…`** (the staged file
`49e318bf…` is 67,108,864 B: **43 modules** + both `wl128x` SR firmware files
+ the CAM/4-sr `wl12xx.ko` + the per-unit NVS +
`iw`/`wpa_supplicant`/`wpa_cli`/`passphrase` + the signed `regulatory.db`;
`local/images/rootfs-region1-wl1283-bt.ext4`); hd3 = the **Nura** ext4 from
`local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img` (`07555bde…`,
625,999,872 B), mounted read-write and grown to 62.5 G, with the ARMv7 module
set.  The unit is on Linux, not TabletOS.  The way back to TabletOS is the 1917
IFS (`local/work/ifs-shim-sealed.ifs`, block `0x825`); the previous Linux images
`0x158D8A85`, `0xC675E206`, `0x19E24D8F`, `0xBEF06B78`, `0x294F8E36`,
`0xED58EF36`, `0xB9DD91E3`, `0xA544A731`, `0x373674A4`, `0x23D88CBD`,
`0xAB899C49`, `0xA8F0F30C`, `0xF64A680D`, `0x53F7F97B`, `0x69CDAC94`,
`0x17C2E119`, `0xBAFD9B3B`, `0x91E22899`, `0xAD85DF0F`, `0xF8BADFF3`,
`0xE5E2AEAF`, `0x58F85337`, `0x47ED0450`, `0x5A04ACD9`, `0x92458D00`,
`0x7A5DD941`, `0x36597040`, `0xB8830701`, `0xF97CD9C7`, `0x3B2ECFED`,
`0x0DC36A11`, `0xE36C679A` and `0x04AF1433` remain.
NVRAM and the MCT remain unwritable, and an agent never writes RPMB
(`SAFETY.md` §6).

## What is next

0. **THE ACTIVE ITEM FOR SESSION 68 — put the UI on the GPU (operator
   directive; report 268 is the plan).**  Measured: the SGX works on libEGL's
   **GBM/DRM** platform (12 window configs, `PowerVR SGX 540`, dmabuf export),
   and only the **Wayland client** path refuses it.  Session 68: (1) diagnose the
   wayland rejection with `EGL_LOG_LEVEL=debug` (do not patch Mesa before that),
   (2) prove the vendor-supported GBM path with pvrports' `phoc`, (3) decide with
   data, (4) only then weigh the open-stack SGX540 port
   (`The1TrueJoe/powervr_sgx`).
   * **Owed after the GPU — the CPU (session 67, reports 265/266, ROADMAP 7a).**
     Both cores now take userspace work, and the **stock
     `omap_musb_set_mailbox()` call is restored** (the mailbox fault was a freed
     TWL6030 `phy_companion`, report 265).  PID 1 was affinity-masked to `{0}`,
     so all userspace ran on one core (`nproc` = 1); the inittab workaround
     `::sysinit:/usr/sbin/taskset -p 3 1` fixes it (`nproc` = 2, load balanced) —
     a **kernel-side fix in `sched_init_smp()` is still owed** (report 266 §1).
     `cpufreq-dt` **does** exist and scale; what is missing is the CPU
     **supply**, and the `twl6030_uv_to_vsel` warnings are startup VC init
     clamped to 1.35 V (report 266 §2/§3).
   * **Then the MPU OPP:** `cpufreq-dt` is bound and scales
     (`300000 600000 800000 1008000`, `ondemand`), so the old "no cpufreq
     policy" note is stale.  What remains is the CPU **supply** (no
     `supply-*` under `/sys/kernel/debug/opp/cpu0/`, so OPP voltages are not
     applied) and the `twl6030_uv_to_vsel: OUT OF RANGE` lines, which are
     startup VC init (`omap4_mpu_vc_data.on` 1.375 V vs the TWL's 1.31666 V
     ceiling) clamped to VSEL `0x3A` = 1.35 V — the chainloader's own value.
     Decide 1.35 V-for-all-OPPs vs a supply binding; SmartReflex MPU is present
     but idle (`autocomp = 0`).  Thermal-check (trap 312; report 266 §2/§3).
   * **Then re-measure:** the memory-throughput numbers (report 263 §4b) and the
     UI latency were taken with CPU 1 starved of userspace; redo them now that
     both cores take load, then shrink/pre-warm the drawer (the swap) and pursue
     a hardware GL path for Wayland clients (trap 314).  Remove the r10006 draw
     timing and the `PBGL_PROFILE` env before the final image.
   * **Durable (corrected in session 70 by measurement):** the flasher-restorable
     hd3 image needs exactly **three** changes, not the list this item used to
     carry — `catacomb` r10003, `epitaph` r10008, and the inittab affinity line.
     The baked image already has the pvr `/etc/catacomb/initrc`
     (md5 `00fdff8481e7c4dde730221ef55d53da`), the pvr `mesa.sh`, the module, and
     the pvrports stack; **no llvmpipe `swrast_dri.so` or `libLLVM-13` is in the
     shipped state at all** (the desktop runs on `pvr_dri`), and the udev/polkit
     rule directories are **empty**.  `tools/rebake-hd3.sh` applies the three to a
     copy of the base image; it has not been run (needs root).
   * **Apps / Wi-Fi are up:** `nmcli radio wifi on` (the radio was disabled)
     gives `wlan0` a scan; `gorm` with `10-gorm.rules` lists ~10 APs (joining
     an AP is untried), and a cleanly launched `tzompantli` runs apps (an
     injected tap on Foot ran `foot`).  Traps 306–308.
   * the **export is current**: 67 files / 9 136 lines (`d19adff8…`), verified
     0 rejects / 67/67 against pristine 7.3.3 in session 67, with the pvr
     driver vendored; the `local/kernel-tree` still carries the session-62/63/64
     diagnostics (not in the patch).  Re-export after any further kernel change.
   * **Build speed:** the armv7 package builds under qemu-user (`--no-cross`)
     and abuild wipes `$srcdir`; the local APKBUILD now sets a persistent
     `CARGO_TARGET_DIR`.  The native `--cross` path is blocked by a
     host/chroot rustc version mismatch — matching them is the real speedup.
1. **WL1283 — the WLAN ASSOCIATES (session 52, report 236).**  Session 51 fixed
   stability (CAM) and the scan (4-sr); session 52 found the association
   blocker — the **missing `ccm`/`ctr` modules** — and the link now reaches
   `COMPLETED`, takes a DHCP lease and passes traffic.  The per-unit NVS is
   shipped.  **Open, none blocking use:** *why* 5-sr panics on scan (235 §3),
   *why* the ELP wake is not honoured (232 §6), and the occasional first-auth
   timeout (restart the supplicant or nudge a scan).
   **Do not** re-run the session-49/50 negatives, do **not** use
   `IRQ_TYPE_LEVEL_LOW` for WL_IRQ (trap 238), do **not** use
   `wlcore_sdio dump=1` to measure a timing fault (trap 239), do **not**
   switch back to 5-sr (trap 243), and do **not** advertise HT40 (trap 247).
1a. **WL1283 BT — the core WORKS; the controller open is parked (session 53,
    report 238).**  Parked by the operator to move to item 7a; the blocker is
    located, so this is resumable in one experiment.
    (a) **What is proven:** the core answers `HCI_Reset`, reports itself
    (`hci ver 6` / `lmp ver 6` / `mfr 0x002b` = TI, BT 4.0) and **runs a full
    BR/EDR inquiry that finds a peer**, reproducibly.  The reference's
    `WL1283.bts` loads **315/315** and moves LMP subversion `0x2b0f` → `0x2b33`;
    loaded **before** the attach it makes `hci0` **register with mgmt** (0 → 1
    controller, negative control both ways).
    (b) **The one experiment when resumed:** find who emits the stray `0x30` that
    breaks the kernel's H4 receiver.  `MGMT_OP_SET_POWERED` returns `0x05`
    (`-ETIMEDOUT`) after `Frame reassembly failed (-84)` (`-EILSEQ`); an
    instrumented `h4_recv()` shows a single `0x30` arriving ~200 ms after the
    last complete init reply, in 6 of 6 runs.  It never appears from userspace.
    Swap the instrument in with `rmmod btbcm` **then** `hci_uart` (trap 251) and
    log the driver's *writes* too, so a host-side echo is visible.
    Excluded by measurement: alignment/padding, chip idle-sleep, leftover bytes,
    UART line errors (`LSR = 0x60`), truncated long replies, and hardware flow
    control (identical A/B on a clean boot).
    (c) **Then** power, an inquiry through the stack, and HID.  **Ship
    `l2cap`/`sco`/`rfcomm`/`hidp` first** — they are modules and none are in
    region 1 yet.
    (d) **LE is refused by the controller** (`0x0c`, `Write_LE_Host_Support` =
    `0x11`, LE feature page zero), but the chip reports BT 4.0 with the
    LE-supported bit and the reference stack being BR/EDR-only reflects the
    vendor's choices, not the silicon — so no enable sequence is known, and
    impossibility is **not** demonstrated.  **The `wl128x-fw-4-sr/5-sr.bin` files
    are WLAN firmware, not BT** — do not swap them expecting a BT change.
    `BT_EN` = gpio 55, UART2 = `/dev/ttyS1`; tools in `rootfs/bt/`.
    **Do not** re-run the "BT is silent" probes — answered.
    **FM:** mainline had `drivers/media/radio/wl128x` from 2.6.39 to 6.13 and
    removed it; bringing it back means porting that driver forward and whatever
    transport it needs.
    **GPS:** no mainline driver; the operator has a 2024 LKML patch to
    evaluate, and the reference's own GPS path is the fallback.
2. **The WM8994 audio — DONE and the diagnostics retired (sessions 47/48,
   reports 214/215).**  Stereo 440 Hz at 48 kHz on headphones and speakers,
   re-verified at DAC1 volume 40 after the cleanup.  The only items left are
   optional: the **MCLK1 probe** (trap 165) and identifying what the GPIO1_15
   `audio_boost` enable supplies if a schematic appears.  The ABE is not needed
   (DECISIONS §58/§59).
3. **Touch: DONE (session 38, report 198) but down after a cold boot.**  The
   calibration and the IRQ are in the tree and proven; after a cold boot the
   controller does not boot (`0x65` NAKs) until the power/reset GPIOs
   (GPIO1_14/GPIO4_8) are pulsed, which the driver still does not drive.  A
   warm reset keeps it up (session 42).
4. **The `wlcore@2` child is in the DTS** (`ti,wl1283`, `reg = <2>`, WL_IRQ,
   26 MHz ref/TCXO), the stack probes, and the firmware stays up; the only
   remaining node-level item is the **per-unit `wl128x-nvs.bin`** from the `hd5`
   calibration (report 197), to replace the generic NVS.
5. **The unit is on the Nura read-write ext4 root on hd3** (region 0
   `0x3547D613`), grown to 62.5 G — not the old session-58 overlay.  **SSH
   works:** `PB_LINK=usb tools/pbssh.sh` (`169.254.0.1:22`, key-only; the host
   is `169.254.0.2/16` on `enp8s0f3u1i2`).  If it times out the cause is the
   **host** — WireGuard VPN (trap 341) or `ufw` (trap 342) — not the device.
   The `ttyGS0` shell on the USB ACM (`/dev/ttyACM0`, `playbook:~#`) remains the
   fallback; note `tools/pbcon.py` expects `pb> ` and does not drive it
   (trap 274).  To return to TabletOS, flash the 1917 IFS at block `0x825`.
6. **The QNX door** uses `PB_LINK=door tools/pbssh.sh` (qconn on
   `169.254.0.1:9090`, root ssh); the minimal oracle is
   `local/work/minimal/setB.signed`.

## Workspace location

The workspace moved at the end of session 15 from the old checkout (the root
filesystem) to this repository.  Start sessions in the repository root.
Absolute paths were retargeted in `tools/` (`pbssh.sh`, `door-hold.sh`,
`pb_ring.py`, `pb_events.py`), the shipped kernel configs
(`CONFIG_INITRAMFS_SOURCE`) and the canonical docs; historical reports keep their
original wording.

A **fallback copy of the old tree is deliberately kept at `$HOME/playbook-linux`**
until the operator is happy with the move.  It is stale from the moment of the
move — do not work in it, and do not let anything (`CONFIG_INITRAMFS_SOURCE`,
the door scripts) point at it.

**Public snapshot:** `$HOME/SSDdrive/playbook-linux-public` (remote
`git@github.com:Psyden57/playbook-linux.git`).  The private tree is the source
of truth; publish with `tools/publish_public.sh` (see `REPO-MANAGEMENT.md`).

**Both cross-toolchains are inside the workspace** in the gitignored payload:
`local/toolchains/armv7-eabihf` (Buildroot gcc 14.3.0) and
`local/toolchains/qnx660-master` (QNX SDP 6.6).  `. tools/env.sh` exports
`PATH`/`CROSS_COMPILE`/`QNX660`/`QNX_HOST`/`QNX_TARGET`; `tools/env.sh --check`
validates them and prints the versions.

---
