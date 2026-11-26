# Handoff — session 72 (absolute) → session 73

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 73 = Linux-era 67**, written at the
session-72 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 72 RESULT: the Kumo/WPE WebKit experiment ran and is a proven
> negative, root-caused to the closed GPU driver — not Kumo, not WebKit.**  The
> engine runs on the SGX (`kumo` and `WPEWebProcess` map `pvr_dri`) but the web
> content never composes, because the PowerVR GBM backend cannot export a dmabuf
> for the buffers it allocates.  Reports **275** (the experiment + the probes) and
> **276** (the open-stack scope); DECISIONS **§107/§108**; traps **365–367**.
> The browser is **blocked on the DDK**, and the durable route is the open
> `powervr_sgx` SGX540 port (report 276, and the drafted fact sheet in
> `docs/reference/sgx540-factsheet.md`).

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**, with
a GPU-accelerated desktop (compositor **and** clients), and a modern browser.

**Done.**  Everything through session 71: the SoC, display, hd3/Nura root,
dual-core 1008 MHz, SGX540 with the desktop and its clients, the pitch fix, the
rebaked/flashed hd3 image, and WPE WebKit 2.54.3 (fork) + Kumo 1.10.0 built and
installed on the live root.

**Session 72** ran the browser experiment (§0a): **Kumo launches, loads a page and
the engine renders on the SGX, but the page never presents** — the closed pvr GBM
cannot export the dmabuf WebKit's `AcceleratedSurface` needs.  Proven with two
probes (`tools/bench/gpu/gbmplane.py`, `gbmprime.c`, `gbmprime2.c` + `ioctllog.c`)
and documented.  No media write; the unit is left clean on the desktop.

## 0a. Session 72's result in brief (do not re-derive)

* Kumo starts, draws its window/URL bar, and **fetches the page**; `kumo` **and**
  `WPEWebProcess` map `pvr_dri` + `libGLESv2_PVR_MESA` — the engine is on the
  SGX.  The failure is **not** a missing helper, `swrast`, or a display/view
  abort.  [report 275 §1]
* WebKit's `AcceleratedSurface::RenderTargetEGLImage::create()` makes a GBM
  buffer and `DMABufBufferAttributes::fromGBMBufferObject()` cannot get its
  dmabuf attributes: `gbm_bo_get_fd_for_plane` = **-1** → `Failed to extract
  DMA-buf attributes` → the page stays gray.  [report 275 §2]
* **Why:** the pvr GBM buffer is a real DRI image (no `CREATE_DUMB`) but the DDK
  gives it **no GEM handle** (`gbm_bo_get_handle_for_plane = 0xFFFFFFFF`), so
  `PRIME_HANDLE_TO_FD` fails.  The **DDK, not Mesa, is the wall** — a Mesa-only
  or WebKit-only fix cannot invent the handle.  [report 275 §2.2]
* `WPE_DRM_DEVICE`/`WPE_BUFFER_FORMAT` do **not** help.  Software is blocked
  twice: Kumo panics without a DRM node path (`main.rs:385`), and swrast cannot
  bind over this GBM (`DRI_DRI2` missing).  [traps 365/366]
* **Do not leave Kumo running or kill it**: stopping it wedges the pvr driver
  (`FreeResourceByPtr`) and freezes the panel with **0 DSI errors**; it also
  drives GEM toward the 128 MiB TILER.  Use `reboot -f`; a DSI `NO_PACKET_SENT`
  wedge can follow a GPU wedge plus the power button and needs a **second**
  reboot.  [trap 367]
* The desktop is unaffected (libdbm/pvr clients export PRIME handles, not GBM).

## 0b. The two tracks for session 73 (operator's choice)

**Track A — the durable graphics route: the open `powervr_sgx` SGX540 port.**
This is the structural fix: a real Mesa 26.x GBM that exports dmabufs and the
dmabuf-feedback Wayland path (which also removes the `wl_drm` requirement and the
closed DDK).  **Yes — start from the existing upstream repo**
(`github.com/The1TrueJoe/powervr_sgx`), which already supports the **SGX545**
across one kernel driver, one Mesa driver and one open microkernel; an SGX540 is
added **to that tree** (branch `dev`), not as a fork.  It is "data, not code" per
the project's porting guide, **with** the non-per-core work our core needs:

* Our SGX540 is **USSE1, 4 pipes, multi-event-kick**, but **28-bit, single
  memory context, no `USE_CODE_BASE`** in the DDK's feature block — it fails the
  open driver's core check (≥32 bits, `MULTIPLE_MEM_CONTEXTS`, ≥2 dir lists), so
  it needs the **SGX530-class** 28-bit heap, single-context BIF and code-base
  work, plus OMAP4 platform glue.  [report 276]
* **First steps (zero risk):** read `CORE_ID`/`CORE_REVISION` (0x0020/0x0024,
  expect `0x00010200` from "Rev 120") on the unit; finalise
  `docs/reference/sgx540-factsheet.md` into the upstream
  `docs/porting/cores/sgx540.md`; open a "New SGX core" issue with the hardware
  and what we can run.  Then the platform driver, the 28-bit/code-base code, the
  microkernel, and Mesa, in the guide's order.
* Cleanroom: no proprietary source/binary/disassembly in the open tree; our DDK
  headers are GPL (usable), the closed userspace/blobs and QNX material are
  black-box evidence only.  [report 276 §5]

**Track B — the queue (smaller, durable items).**
1. **The next hd3 rebake must add `wpewebkit` + `kumo`** (they are on the live
   root only, not in the flashed image) — and the `pvr-dbmfix` shim if not yet an
   APK.  Kumo will stay gray until the DDK/GBM gap is closed, but the packages
   belong in the image.
2. **`pvr-dbmfix` APK** — package the pitch shim (today `/usr/local` files).
3. **Power-button DPMS binding** — the initrc binds the release to a DPMS toggle,
   the likely black-panel/DSI-wedge trigger; neutralise it in the next image.
4. **The GEM growth** — the idle desktop reaches ~25.6 MiB pinned / 166 MiB total
   over hours; the 9.7 MB objects are unexplained (a per-object walk, clients
   stopped).
5. **trap 350** (cosmetic glitches) and a **heavy-redraw/frame-latency**
   measurement (now unblocked — the TILER pressure is gone).
6. The **CPU** items: 1.35 V-for-all-OPPs vs a real supply, the kernel-side PID-1
   affinity fix, the memory-throughput re-measure.
7. The **USB teardown** (trap 337) capture.

## 0c. The media, and the way back

* **region 0 = `0x3547D613`** — `boot/chainloader/kernel.img` (10,448,868 B,
  sha256 `6ecd068d…`), kernel `7.3.3 #109 SMP`, ARMv7, dual-core 1008 MHz, no
  `maxcpus`, stock MUSB mailbox + comparator fix, `panic=0`.  Media trailer
  `1f 2d c8 d7 13 d6 47 35` — **not written in session 72**.
* **region 1 = `20a5edd3…`** (64 MiB at raw `0x29000000`) — the BusyBox fallback
  root, not booted.
* **hd3 = the session-71 rebaked image** `5d3eac8f…` (625,999,872 B), flashed at
  loader block `0x4915`, grown to 62.5 G; live root `/dev/loop0` ext4.  It
  carries catacomb r10003, epitaph r10008, the pitch shim, the 842-module ARMv7
  tree and the permissions; **it does not carry `wpewebkit`/`kumo`**.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`; qconn `169.254.0.1:9090`, `PB_LINK=door tools/pbssh.sh`.
* **To stock TabletOS:** the 1917 IFS at `0x825` **and**
  `local/work/rcfs-1917.rcfs` at `0x8D5`, plus the vendor UFS at `0x4915` if hd3
  is repurposed.  No restore is required at this wrap-up.
* **Flash loop:** `python3 tools/bbusb.py flash --component <img>
  --dest-block 0x4915 --from-bootrom --password 0000 --apply`; **start the
  flasher, then reset with `reboot -f`** (a warm reset enters the bootrom).  A
  region-0 flash is block `0x825`; both regions in one bootrom session only when
  both changed.

## 1. Bench state at the session-72 close (re-measured)

| item | value |
|---|---|
| OS | **Linux 7.3.3 `#109 SMP`**, ARMv7, dual-core 1008 MHz — not TabletOS |
| region 0 | CRC **`0x3547D613`** (media trailer `1f 2d c8 d7 13 d6 47 35`, read) |
| region 1 | sha256 **`20a5edd3…`** (recorded; not re-hashed) |
| hd3 | the session-71 rebaked image **`5d3eac8f…`**, flashed at `0x4915`, grown to 62.5 G; live root `/dev/loop0` ext4 |
| `BOOTCTR_FCTRY` | **`0x28`** (`dmesg \| grep pbrpmb`; `/init` tops up below `0x1e`) |
| CPU | `nproc` 2, `taskset -p 1` → **3** |
| desktop | catacomb r10003 + epitaph r10008, clients on **`pvr_dri`**, pitch shim loaded |
| panel | `connected`, backlight 255, **0 DSI errors** on the final boot |
| GEM | fresh (`3` objects early, ~9.9 MB settled) |
| browser | `wpewebkit 2.54.3-r1000` + `kumo 1.10.0-r1000` installed on the **live root**; **Kumo cannot present** (report 275) |
| WLAN | **down** at the close (the 5 GHz SSID was not in the scan; `gorm` is the path) |
| SSH | `PB_LINK=usb tools/pbssh.sh` works; if it times out check the cable, the host VPN (trap 341) and ufw (trap 342) |

## 2. Working rules (unchanged)

* Read `docs/project/SAFETY.md` before any write.  One variable per boot.  An
  agent never writes RPMB; NVRAM read-only; never program WDT2 from the
  chainloader (trap 322).
* Kernel printk goes to the panel; `console=ttyGS0` is inert.  Use `reboot -f`.
  **Ask the operator what is on the panel**; do not screenshot.
* **Do not restart `tinydm`** to switch compositors (trap 353), and **do not
  leave Kumo running or kill it** (trap 367) — reboot.
* The eMMC can log `Card stuck being busy`; a power cut can damage the rw ext4
  (trap 336).  **Do not call `sync`** (the FLUSH hangs the card, trap 300).
  `/tmp` is on the ext4, not tmpfs (trap 354).
* **Revert experiments and prove the revert.**  Look up every external fact.
  Time-box subagents (trap 109).  Write the durable fact into the repository in
  the same turn.

## 3. Useful local extracts

* `docs/reference/sgx540-factsheet.md` — the SGX540 facts (K from our DDK 1.17,
  L from mainline OMAP4) for the open port; report 276 is the scope.
* `tools/bench/gpu/` — `gbmplane.py`, `gbmprime.c`, `gbmprime2.c`, `ioctllog.c`
  (the session-72 probes) and `dbmfix.c` (the pitch shim).
* `local/work/mesa-pvr-src/` — the amber Mesa 21.3.9 source (the pvr DRI/GBM);
  `local/work/pvrports/` — the pvrports pmaports; `local/work/pvrdri/` — the
  pulled `pvr_dri.so`/`libEGL_amber`/`swrast_dri`.
* `local/work/nura/pmaports/main/wpewebkit/`, `.../kumo/` — the two browser
  packages' APKBUILDs + patches; `local/work/nura/work/packages/edge/armv7/` —
  the APKs (`wpewebkit-2.54.3-r1000.apk`, `kumo-1.10.0-r1000.apk`).
* `local/work/nura/images/blackberry-playbook-root-gpu-20261125.img` — the
  flashed hd3 image (`5d3eac8f…`); `tools/rebake_hd3.py` / `rebake-hd3.sh`.
* `local/kernel-tree/` — the 7.3.3 build tree; `local/pristine/linux-7.3.3` for a
  clean diff.  Kernel build/flash: `docs/project/DEVELOPMENT.md` §2.
* `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` — the vendored SGX540 DDK
  (GPL-2 headers: `sgx540defs.h`, `sgxfeaturedefs.h`, `sgxerrata.h`, `sgxmmu.h`).
* `local/work/ifs-shim-sealed.ifs` — the way back to TabletOS (block `0x825`);
  `local/work/rcfs-1917.rcfs` (0x8D5); `local/work/ufs-1917-devuser.ufs` (0x4915).
* `tools/pbssh.sh`, `tools/pbraw.py`, `local/work/kmsgcap.py` — the console/SSH
  and capture paths.
* Reports **276** (open-stack scope), **275** (the Kumo runtime negative), 274
  (browser build), 272 (rebake), 271 (pitch fix), 270 (trap 349).

## 4. The one thing to start with (track A)

**Read `CORE_ID`/`CORE_REVISION` (0x0020/0x0024) on the unit** while the vendor
DDK clocks the GPU (it already does: `pvrsrvkm 56000000.gpu`), confirm the
expected `0x00010200`, and turn `docs/reference/sgx540-factsheet.md` into the
upstream `docs/porting/cores/sgx540.md` with the first **C** facts.  That is the
zero-risk first step of the only durable route to a browser on this device; the
closed stack stays in service meanwhile.

## 5. What could not be verified

* Whether the SGX540 GBM buffer can be exported at all (the DDK gives it no GEM
  handle; a display-node/master path was untestable — `card1` is already
  DRM-master, `drmSetMaster` → `EBUSY`).
* The SGX540 `CORE_REVISION` and every §Gaps item in the fact sheet (no GPU
  register read yet).
* `USE_CODE_BASE` on the SGX540 (the DDK header defines it; the upstream guide
  says 540 has none — a hardware read settles it).
* Kumo's RSS/first-render/touch behaviour (it never presents).

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 73 (absolute; Linux-era 67)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a, KNOWN-ISSUES.md §2 (traps 356-367, especially 365-367)
and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 91-108,
docs/reports/INDEX.md, reports 276 (current for the graphics route), 275 (current
for the Kumo runtime result), 274, 273, 272, 271, 270, 269, 268, 267, 266, 265,
264, 263, 257, docs/reference/sgx540-factsheet.md, and docs/sessions/session-72.md
and session-71.md.

WHERE IT STANDS: Linux 7.3.3 #109 SMP ARMv7, dual-core 1008 MHz; region 0
0x3547D613, region 1 20a5edd3, hd3 = the session-71 rebaked image 5d3eac8f
flashed at 0x4915 and grown to 62.5 G; BOOTCTR_FCTRY 0x28.  THE BROWSER IS
BLOCKED ON THE CLOSED GPU DRIVER (session 72): Kumo launches, loads a page, and
kumo + WPEWebProcess map pvr_dri (the engine is on the SGX), but the page never
composes because WebKit's AcceleratedSurface needs a GBM dmabuf and the pvr GBM
gives the buffer no GEM handle (gbm_bo_get_fd_for_plane -1; PRIME_HANDLE_TO_FD
-1).  wpewebkit 2.54.3 + kumo 1.10.0 are installed on the live root but NOT in
the flashed image.  Do NOT leave Kumo running or kill it (it wedges the pvr and
freezes the panel with 0 DSI errors) — use reboot -f; a DSI NO_PACKET_SENT wedge
can follow and needs a second reboot.

THE NEXT WORK — two tracks, operator's choice.
TRACK A (durable, recommended): the open powervr_sgx SGX540 port.  START FROM THE
EXISTING UPSTREAM REPO (github.com/The1TrueJoe/powervr_sgx), which already
supports the SGX545 across one kernel driver + one Mesa driver + one open
microkernel; add SGX540 to that tree (branch dev), not a fork.  Our SGX540 is
USSE1 / 4 pipes / multi-event-kick but 28-bit / single-memory-context / no
USE_CODE_BASE (per the vendored GPL DDK), so it needs the SGX530-class
28-bit/single-context/code-base work plus OMAP4 platform glue (report 276).  First
zero-risk step: read CORE_ID/CORE_REVISION (0x0020/0x0024, expect 0x00010200) on
the unit with the vendor DDK clocking the GPU; finalise
docs/reference/sgx540-factsheet.md into upstream docs/porting/cores/sgx540.md;
open a "New SGX core" issue.  Cleanroom: no proprietary source/binaries in the
open tree.
TRACK B (queue): the next hd3 rebake must add wpewebkit + kumo (+ pvr-dbmfix);
package pvr-dbmfix; neutralise the power-button DPMS binding; the GEM-growth walk;
trap 350 + a heavy-redraw measurement; the CPU items; the USB teardown.

BENCH: region 0 trailer 1f 2d c8 d7 13 d6 47 35 -> 0x3547D613; region 1
20a5edd3...; hd3 live /dev/loop0 ext4 (53 ef), 56.5 G; BOOTCTR 0x28; nproc 2,
PID 1 mask 3; panel connected, backlight 255, 0 DSI errors; WLAN down (5 GHz SSID
not in scan; gorm is the path).  SSH: PB_LINK=usb tools/pbssh.sh works; if it
times out check the cable, the host VPN (trap 341) and ufw (trap 342) first.
Kernel printk goes to the panel; use reboot -f; ask the operator for visual
checks; do not screenshot; do not restart tinydm (trap 353) and do not kill Kumo
(trap 367).  An agent never writes RPMB; NVRAM read-only.  Revert experiments and
prove the revert; look up every external fact; the eMMC can log Card stuck being
busy and do not call sync (traps 336/300); /tmp is on the ext4 (trap 354).

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
Start the flasher, then reset with reboot -f; the bootrom holds ~20 s.  The hard
hazards (BOOTCTR_FCTRY 0 / >0x32 / failed write / flat pack) remain.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  WRAP-UP: only the operator
decides when the session stops; then record the bench state, write the session
note, update the canonical docs, verify with tools/docs_audit.py and
tools/tests/test_offline.py, commit privately and publish the snapshot
(docs/project/REPO-MANAGEMENT.md).  No restore is required.
```
