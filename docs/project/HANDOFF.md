# Handoff — session 71 (absolute) → session 72

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 72 = Linux-era 66**, written at the
session-71 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 71 RESULT: the hd3 rebake is flashed and validated, and WPE WebKit
> 2.54.3 + Kumo 1.10.0 are built and installed.**  The GPU result is now
> durable; the browser stack exists on the unit.  [reports 272, 273, 274;
> DECISIONS §105/§106; traps 356–364]
>
> **The one experiment for session 72 is the Kumo GUI on the panel** — the
> engine and Kumo are installed but have never drawn a page.  §0a is exact.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**, with
a GPU-accelerated desktop (compositor **and** clients), and now a modern browser.

**Done.**  The SoC, display, hd3/Nura root, dual-core ARMv7 at 1008 MHz, SGX540
with the desktop and its clients, the pitch fix, **the rebaked/flashed hd3 image**,
and **WPE WebKit 2.54.3 (fork) + Kumo 1.10.0 built and installed** on the live
root.  **Next:** run Kumo on the panel.

## 0a. SESSION 72's ONE EXPERIMENT — Kumo on the panel

Everything is installed: `wpewebkit-2.54.3-r1000` and `kumo-1.10.0-r1000` are
on the live root.  The unknown is whether the engine's EGL/GLES2 **renders on
the SGX** through Kumo's own WPEPlatform display, and whether it is usable.

**First, reboot (`reboot -f`).**  The desktop has been up 5 h and the GEM tree
has grown to 84 objects / 166 MiB total / **25.6 MiB pinned** (the fresh boot was
8 objects / 14.7 MiB); a clean boot isolates the measurement.  Also note the
single **transient DSI error at t=1911 s** on that boot (it recovered — null
packets sent after 1 poll — and is **not** the `NO PACKET_SENT` wedge).

**Then launch Kumo the way the session will.**  The compositor and its clients
run as `playbook`; a launched client must get the session environment, not a
bare `su`:

```sh
PID=$(pgrep -x catacomb | head -1)
# copy the session env the README-permissions recommends
su playbook -c '
  export XDG_RUNTIME_DIR=/run/user/10000
  export WAYLAND_DISPLAY=wayland-1
  export MESA_LOADER_DRIVER_OVERRIDE=pvr
  export DBMFIX=1
  export LD_PRELOAD=/usr/local/lib/dbmfix.so
  export XDG_SESSION_TYPE=wayland
  nohup kumo https://example.com >/tmp/kumo.log 2>&1 </dev/null &
'
```

Then read, in this order:

1. `/tmp/kumo.log` — did it create the display/view, and what is the first
   error?  `WAYLAND_DEBUG=1` only if needed.
2. `ps` for `kumo` plus the WebKit helpers
   (`/usr/libexec/wpe-webkit-2.0/{WPEWebProcess,WPENetworkProcess,WPEGPUProcess}`).
3. `grep -E 'pvr_dri|swrast|libWPEWebKit' /proc/<pid>/maps` for **each** of
   those processes.  The compositor/clients on the SGX map `pvr_dri`; a WebKit
   process on `swrast` is the failure to explain.
4. `/sys/kernel/debug/dri/omapdrm.0/gem` (pinned sum, object sizes) and
   `/proc/<pid>/status` `VmRSS`.
5. `dmesg` unfiltered (PVR errors, OOM).
6. **Ask the operator what is on the panel** (do not screenshot).

**What each result means.**

| result | meaning | next action |
|---|---|---|
| Kumo draws a page, `pvr_dri` mapped in its WebKit GPU process, panel correct, RSS/first-render measured | **the browser works on the SGX** | record RSS + first render; then touch zoom and the touch context menu; then the daily-browser question |
| Kumo starts but the view is black / a WebKit process maps `swrast` | the engine's EGL is not on the pvr node; the pvr driver may be loaded on the **omapdrm** node Kumo passes | read `MESA-LOADER`/`PVR_K` logs; check which DRM node Kumo hands WebKit (`drm_device(feedback.main_device())` in `src/engine/webkit/platform.rs`); the likely fix is to give the engine the pvr render node while presenting through the compositor's dmabuf feedback |
| Kumo aborts at display/view creation | Kumo's Rust `WPEDisplay`/`WPEView` is not accepted by the engine, or the platform ABI differs | `WAYLAND_DEBUG=1`, the engine version, and Kumo's `DisplayImpl`/`ViewImpl`; the static API gate passed, so this would be runtime/ABI detail |
| a helper process is missing or cannot exec | the engine's `libexec` path/permissions | `ls -l /usr/libexec/wpe-webkit-2.0/`; the path is compiled into the library |
| renderer is `softpipe`/very slow, no pvr | Mesa's driver selection again | the `MESA_LOADER_DRIVER_OVERRIDE`/DRM-node check; the engine's GLES2 path |

**Pass criterion:** Kumo renders a real page on the SGX — `pvr_dri` mapped,
`GL_RENDERER = PowerVR SGX 540` in the WebKit process — with a measured RSS and
first-render time, and touch scrolling, zoom and the context menu working.

## 0b. The owed rebake — now includes the browser packages

The flashed image is `local/work/nura/images/blackberry-playbook-root-gpu-20261125.img`
(sha256 `5d3eac8f…`): catacomb r10003, epitaph r10008, the inittab affinity
line, the pitch shim, the live **842-module ARMv7** tree, and the permissions
set.  **It does not contain `wpewebkit` or `kumo`.**  The next rebake must add
the two runtime packages (not the dev packages the engine install pulled on the
unit).  `tools/rebake_hd3.py` (called by `tools/rebake-hd3.sh`) applies a
file-level delta; add them as another delta (extract the two APKs, splice the DB
records, or extend the tool).  The base `07555bde…` is untouched; the first
GPU-only rebake is `…-preperm.img` (`9b656d36…`).

## 0c. The media, and the way back

* **region 0 = `0x3547D613`** — `boot/chainloader/kernel.img` (10,448,868 B,
  sha256 `6ecd068d…`), kernel `7.3.3 #109 SMP`, `-march=armv7-a`, fast
  chainloader, loud DTB, stock MUSB mailbox + comparator fix, `panic=0`.
  **Measured this session** from `/dev/mmcblk1` at `0x8100000 + 10 448 868 − 8`:
  `1f 2d c8 d7 13 d6 47 35`.  Not written this session.
* **region 1 = `20a5edd3…`** (64 MiB at raw `0x29000000`, measured this session
  with `dd bs=1M skip=656 count=64 | sha256sum`).  The BusyBox fallback root,
  not booted.
* **hd3 = the session-71 rebaked image** (`5d3eac8f…`, 625,999,872 B), flashed
  at loader block `0x4915`, grown to 62.5 G; live root `/dev/loop0` ext4
  (superblock magic `53 ef`).  The live root now also carries the engine/Kumo
  packages and the dev packages the engine install pulled.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`, then reboot; qconn `169.254.0.1:9090`, `PB_LINK=door tools/pbssh.sh`.
* **To stock TabletOS:** the 1917 IFS at `0x825` **and**
  `local/work/rcfs-1917.rcfs` at `0x8D5`, plus the vendor UFS at `0x4915` if
  hd3 is repurposed.  No restore is required at this wrap-up.
* **Flash loop:** `python3 tools/bbusb.py flash --component <img>
  --dest-block 0x4915 --from-bootrom --password 0000 --apply`; **start the
  flasher, then reset with `reboot -f`** (a warm reset enters the bootrom; no
  physical power cycle).  Region 0 at `0x825`; both regions in one bootrom
  session only when both changed.

## 0d. The queue behind the Kumo experiment

1. **The GEM growth** — 84 objects / 166 MiB total / 25.6 MiB pinned after 5 h;
   six unpinned 9,707,520 B objects (`1024×2370×4`).  Source unknown; walk the
   objects with the clients stopped and compare a fresh boot.  Reboot before the
   Kumo test.
2. **The power-button DPMS binding** — the initrc binds the release to a DPMS
   toggle, and a DPMS off wedges the DSI (the first post-flash boot's black
   panel; trap-353 class).  Decide whether to neutralize it in the next image.
3. **trap 350** (cosmetic glitches) and a **heavy-redraw/frame-latency**
   measurement — both now unblocked (the TILER pressure is gone).
4. **Package the pitch fix** as `pvr-dbmfix` (today `/usr/local` files).
5. The **CPU** items (§0e of the old handoff, still current): the voltage
   decision, the kernel-side PID-1 affinity fix, the memory-throughput re-measure.
6. The **USB teardown** (trap 337) — capture before the drop.

## 0e. What is proven about the build (do not re-derive)

* **The API gate is a static header compare**, not a host `cargo check` (which
  fetched 3.1 GB of optional servo/stylo).  The fork's
  `WPEGestureController.h`/`WPEView.h` at `e4f4ac71` declare exactly the symbols
  `wpe-platform-sys` binds.
* **The engine source is the official WPE 2.54.3 tarball + the fork's six
  commits as patches**, all applying cleanly.  The GTK tarball has no WPE port.
* **Build fixes that are permanent:** `samurai`/`lld`/`llvm`;
  `-DENABLE_JOURNALD_LOG=OFF`; **`-lintl` in `CMAKE_C/CXX_STANDARD_LIBRARIES`**;
  `clang18-libclang` + `LIBCLANG_PATH=/usr/lib` for bindgen; `--no-cross` for
  Kumo (crossdirect cannot install the armv7 `wpewebkit` dep natively);
  `sccache` in makedepends plus the **local pmbootstrap patch**
  (`pmb/chroot/shutdown.py`, `kill_sccache`); `CARGO_TARGET_DIR` on the
  persistent `/mnt/pmbootstrap/rust` (the `cache_rust` mount must be owned by
  12345); Kumo `0001-drop-optional-servo` and `0002-webkit-only-cfg` (Kumo
  1.10.0 is **not** clean without servo).
* **Packages:** `wpewebkit-2.54.3-r1000.apk` (sha256 `032e5121…`),
  `kumo-1.10.0-r1000.apk` (sha256 `b312ecfb…`), in
  `local/work/nura/work/packages/edge/armv7/`.  `pkg-config --modversion
  wpe-webkit-2.0` = 2.54.3; `kumo --version` = 1.10.0.

## 1. Bench state at the session-71 close (all re-measured)

| item | value |
|---|---|
| OS | **Linux 7.3.3 `#109 SMP`**, ARMv7, dual-core 1008 MHz — not TabletOS |
| region 0 | CRC **`0x3547D613`** (media trailer `1f 2d c8 d7 13 d6 47 35`, measured) |
| region 1 | sha256 **`20a5edd3…`** (measured) |
| hd3 | the session-71 rebaked image **`5d3eac8f…`**, flashed at `0x4915`, grown to 62.5 G; live root `/dev/loop0` ext4 |
| `BOOTCTR_FCTRY` | **`0x2f`** (`dmesg \| grep pbrpmb`; `/init` tops up below `0x1e`) |
| CPU | `nproc` 2, `taskset -p 1` → **3** |
| desktop | catacomb r10003 + epitaph r10008, all clients on **`pvr_dri`**, pitch shim loaded |
| panel | `connected`, backlight 255; **1 transient DSI error** in the 5 h boot (recovered); fresh boot 0 |
| GEM | 84 objects, 166 MiB total, **25.6 MiB pinned**, largest 9.7 MB |
| browser | `wpewebkit 2.54.3-r1000`, `kumo 1.10.0-r1000` installed (`kumo --version`) |
| WLAN | `wlan0 connected`, 192.168.0.231, gateway/1.1.1.1/DNS work |
| battery | Full, 100 % |
| SSH | `PB_LINK=usb tools/pbssh.sh` works; if it times out check the cable, the host VPN (trap 341), `ufw` (trap 342), then trap 337 |

## 2. Working rules (unchanged)

* Read `docs/project/SAFETY.md` before any write.  One variable per boot.  An
  agent never writes RPMB; NVRAM read-only; never program WDT2 from the
  chainloader (trap 322).
* Kernel printk goes to the panel; `console=ttyGS0` is inert.  Use `reboot -f`
  (plain reboot is slow).  **Ask the operator what is on the panel** for a
  visual check; do not screenshot.
* **Do not restart `tinydm` to switch compositors** — it wedges the DSI (trap
  353).  A DPMS off can also wedge it (see the power-button binding above).
* Write the durable fact into the repository in the same turn.  Never present a
  guess as a fact.

## 3. Useful local extracts

* `local/work/nura/pmaports/main/wpewebkit/` — engine APKBUILD + six patches.
* `local/work/nura/pmaports/main/kumo/` — Kumo APKBUILD + two patches.
* `local/work/nura/run-build-wpewebkit.sh`, `run-build-kumo.sh` (root).
* `local/work/nura/work/packages/edge/armv7/` — the two APKs.
* `local/work/nura/images/blackberry-playbook-root-gpu-20261125.img` — the
  flashed hd3 image (`5d3eac8f…`); `…-preperm.img`; base `07555bde…`.
* `tools/rebake_hd3.py`, `tools/rebake-hd3.sh` — the rebake.
* `local/work/src/kumo/` — Kumo 1.10.0 checkout (the source of the API
  symbols); `local/work/src/wpe-src/` — the WPE tarball and patches.
* Reports **274** (current for the build), 273 (API gate/APKBUILD), **272**
  (rebake), 271 (pitch fix), 270 (trap 349).

## 4. The one experiment, in one line

**Reboot; launch Kumo as `playbook` under catacomb with the session env; ask the
operator whether a page is on the panel; measure RSS and first render; check
`pvr_dri` in the WebKit GPU process; then the touch zoom and context menu.**

## 5. What could not be verified this session

* Kumo has never drawn a page; its RSS, first render and touch behaviour are
  unmeasured.  The engine's data path on the SGX (the DRM node Kumo passes vs
  the pvr render node) is the likely first failure.
* The GEM growth and its six 9.7 MB unpinned objects are unexplained.
* The single transient DSI event at t=1911 s was not seen by the operator and
  did not cost the panel; it is not a full wedge.
* The `wpewebkit`/`kumo` packages are not in the flashed image.

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 72 (absolute; Linux-era 66)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a, KNOWN-ISSUES.md §2 (traps 334-364, especially
356-364) and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 91-106,
docs/reports/INDEX.md, reports 274 (current), 273, 272, 271, 270, 269, 268,
267, 266, 265, 264, 263, 257, and docs/sessions/session-71.md and session-70.md.

WHERE IT STANDS: Linux 7.3.3 #109 SMP ARMv7, dual-core 1008 MHz; region 0
0x3547D613, region 1 20a5edd3, hd3 = the session-71 rebaked image 5d3eac8f
flashed at 0x4915 and grown to 62.5 G; BOOTCTR_FCTRY 0x2f.  THE GPU WORK IS
DONE (reports 269-271) AND THE hd3 REBAKE IS FLASHED AND VALIDATED (report 272):
catacomb r10003, epitaph r10008, the inittab affinity line, the pitch shim, the
842-module ARMv7 tree, and the permissions set; 8/8 pvr clients clean, 0 DSI
errors on the clean boot, brightness/Wi-Fi/zram working.

THE BROWSER STACK IS BUILT AND INSTALLED (reports 273/274): WPE WebKit 2.54.3
from github.com/chrisduerr/WebKit @ e4f4ac71 (official WPE tarball + six
patches, -DPORT=WPE, ENABLE_WPE_PLATFORM=ON, legacy libwpe and the built-in
backends OFF because Kumo implements its own WPEDisplay) as
wpewebkit-2.54.3-r1000.apk (032e5121...), and Kumo 1.10.0 as
kumo-1.10.0-r1000.apk (b312ecfb...); both installed on the live root and
kumo --version runs.

THE ONE EXPERIMENT (session 72): KUMO ON THE PANEL.  Reboot first (the 5 h GEM
is 84 objects / 166 MiB total / 25.6 MiB pinned).  Launch Kumo as playbook under
catacomb with XDG_RUNTIME_DIR=/run/user/10000 WAYLAND_DISPLAY=wayland-1
MESA_LOADER_DRIVER_OVERRIDE=pvr DBMFIX=1 LD_PRELOAD=/usr/local/lib/dbmfix.so,
nohup kumo https://example.com; read /tmp/kumo.log, the WebKit helper processes
(/usr/libexec/wpe-webkit-2.0/), their pvr_dri/swrast maps, /proc/<pid>/status
VmRSS, the omapdrm gem tree and unfiltered dmesg; ASK THE OPERATOR what is on the
panel.  Pass: a real page on the SGX (pvr_dri mapped, GL_RENDERER=PowerVR SGX
540), measured RSS and first render, and touch scrolling/zoom/context menu.

IF IT FAILS: the likely first failure is the DRM node Kumo passes to WebKit
(the compositor's dmabuf feedback main device is omapdrm renderD128, while the
pvr driver needs renderD129); read MESA-LOADER/PVR_K, and consider giving the
engine the pvr render node while presenting through the compositor's feedback.
A display/view creation abort would be the WPEPlatform ABI; a missing helper is
the libexec path.

AFTER THE EXPERIMENT: the next hd3 rebake must add wpewebkit + kumo (runtime
only; they are not in the flashed image).  Then the GEM growth, the power-button
DPMS binding, trap 350 and a heavy-redraw measurement, the pvr-dbmfix APK, the
CPU items, and the USB teardown.

BENCH: region 0 trailer 1f 2d c8 d7 13 d6 47 35 -> 0x3547D613; region 1
20a5edd3...; hd3 live /dev/loop0 ext4 (53 ef), 56.5 G; BOOTCTR 0x2f; nproc 2,
PID 1 mask 3; panel connected, backlight 255, 1 transient DSI error in the 5 h
boot; WLAN wlan0 connected 192.168.0.231; battery Full.  SSH:
PB_LINK=usb tools/pbssh.sh works; if it times out check the cable, the host VPN
(trap 341) and ufw (trap 342) first.  Kernel printk goes to the panel; use
reboot -f; ask the operator for visual checks; do not screenshot; do not restart
tinydm (trap 353).  An agent never writes RPMB; NVRAM read-only.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
Start the flasher, then reset with reboot -f; the bootrom holds ~20 s.  The
hard hazards (BOOTCTR_FCTRY 0 / >0x32 / failed write / flat pack) remain.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  WRAP-UP: only the operator
decides when the session stops; then record the bench state, write the session
note, update the canonical docs, verify with tools/docs_audit.py and
tools/tests/test_offline.py, commit privately and publish the snapshot
(docs/project/REPO-MANAGEMENT.md).  No restore is required.
```
