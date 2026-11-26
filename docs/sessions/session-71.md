# Session 71 (absolute) — the hd3 rebake is flashed, and WPE WebKit + Kumo are built

> Numbering: absolute `71` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era session 65**
> (`absolute = Linux-era + 6`).  Continues session 70.

Date: 2026-11-25 (long session).  Unit at the end: **Linux 7.3.3 `#109 SMP`** on
hd3, the **session-71 rebaked image** flashed (sha256 `5d3eac8f…`), `BOOTCTR_FCTRY =
0x2f`, the **wpewebkit 2.54.3** and **kumo 1.10.0** packages installed on the
live root, Wi-Fi associated (`192.168.0.231`), desktop up.

## Goal of this session

Two things: (1) the session's one experiment, **the hd3 rebake** (HANDOFF-70
§0f.1) so the session-69/70 GPU result survives a reflash; (2) the operator's
next goal, **Kumo on WPE WebKit from chrisduerr's fork** (HANDOFF-70 §0f.2),
starting with the API gate and the engine build.

Both are done: the rebake is flashed and validated, and the engine and Kumo are
built, packaged and installed.  The Kumo **runtime** experiment (rendering on
the panel, RSS, touch) is deliberately left to the next session.

## Confirmed (measured)

### The rebake (report 272)

* **The recorded delta was incomplete in three ways**, all read from the image:
  a flat **44-module ARMv6** set (vermagic `ARMv6 p2v8`) while the kernel is
  ARMv7; the catacomb APK's `/etc/catacomb/initrc` is the upstream default,
  owned by `postmarketos-ui-catacomb`, not the bench pvr file; and the
  session-70 permissions set was absent.  [traps 356/357/358]
* **The built image** `local/work/nura/images/blackberry-playbook-root-gpu-20261125.img`
  sha256 **`5d3eac8f…`**, 625,999,872 B, `e2fsck -fn` clean; base `07555bde…`
  untouched; the GPU-only first rebake kept as `…-preperm.img` (`9b656d36…`).
* **The delta, verified from the image**: catacomb r10003 (`cb3dcc30…`), epitaph
  r10008 (`84a52649…`), inittab `::sysinit:/usr/sbin/taskset -p 3 1`, the pitch
  shim (`cc302b42…`) + wrapper + `.desktop`, the live **842-module ARMv7** tree
  (pvrsrvkm `afdca89d…`, vermagic `7.3.3 SMP … ARMv7 p2v8`), the polkit rule
  (`root:polkitd` 0640), the backlight udev rule, `/etc/deviceinfo`, and
  `playbook` in `video`.  [report 272 §1–§3]
* **Flashed** at block `0x4915` (`COMPLETE`, 626,065,440 B, ~4 min) after a
  `reboot -f`; the flasher waits up to 60 s for the bootrom, and a warm reset is
  enough — **no physical power cycle is required**.  [report 272 §4]
* **First boot wedged the DSI** at boot+37 s (`Failed to set dsi_if_enable to
  0`, `NO PACKET_SENT`); the compositor stayed alive and touch was healthy
  (IRQ 118: 20,126 events).  The likely trigger is the power-button **release**
  binding toggling DPMS (trap-353 class); `reboot -f` cleared it.  **The second
  boot had 0 DSI errors.**  [report 272 §4; capture
  `docs/captures/rebake-2026-11-25-boot1-evidence.txt`]
* **Acceptance on the flashed image**: `apk list -I` r10003/r10008; inittab
  line; initrc md5 `00fdff84…`; ARMv7 `pvrsrvkm`/`wlcore`; `nproc` 2, PID 1
  mask 3; root 56.5 G; panel `connected`, 0 DSI errors; catacomb/epitaph/tabula
  map `pvr_dri` **and** `dbmfix.so`; largest GEM object **2,457,600 B**; **8/8**
  re-rendering pvr clients clean, pinned TILER flat **20.66 MiB**; `BOOTCTR`
  `0x31 → 0x30`.  **Trap 349 does not reproduce.**  [report 272 §5]
* **Permissions**: backlight `root:video 0664`, `playbook` writes brightness,
  `nmcli radio wifi off/on` succeeds, zram `1489 MB lzo-rle`, and the operator
  joined `TeleCentro-AW-5G` (`192.168.0.231`, gateway/1.1.1.1/DNS).  [report 272
  §6; `rootfs/README-permissions.md`]

### The WPE WebKit + Kumo build (reports 273/274)

* **The API gate passes statically**: at the fork's `e4f4ac71568a…`,
  `WPEGestureController.h`/`WPEView.h` declare exactly the symbols Kumo's
  vendored `wpe-platform-sys` binds (`wpe_view_get/set_gesture_controller`, the
  full `wpe_gesture_controller_*` set).  [report 273 §1]
* **The engine source is exact and small**: the official **WPE 2.54.3** tarball
  (49,591,296 B, sha512 `0c5d059b…`) plus the fork's six commits as patches
  (`b86110562d`, `dd88b3eb8d`, `5ef6e19e41`, `41dbc7cc99`, `8e81edd149`,
  `e4f4ac7156`), all applying cleanly in that order.  The **GTK** tarball
  contains no WPE port.  [report 273 §2]
* **The engine built** in the pmbootstrap chroot (crossdirect, then a full
  rebuild on the final `-lintl` fix): `wpewebkit-2.54.3-r1000.apk` (34 MB,
  sha256 `032e5121…`), installs `libWPEWebKit-2.0.so.1.11.6`, the
  `wpe-webkit-2.0`/`wpe-platform-2.0` pc files, headers, and
  `/usr/libexec/wpe-webkit-2.0/{WPEWebProcess,WPENetworkProcess,WPEGPUProcess}`.
  [report 274]
* **Kumo built**: `kumo-1.10.0-r1000.apk` (2.6 MB, sha256 `b312ecfb…`) and runs
  on the unit (`kumo 1.10.0`, `--help`).  [report 274]
* **Both are installed on the live root**: `apk list -I` shows `wpewebkit` and
  `kumo`; `pkg-config --modversion wpe-webkit-2.0` = 2.54.3.  **They are not in
  the rebaked hd3 image** — the next rebake must include them.

### Bench at the close (measured this session)

* Region 0 trailer read from `/dev/mmcblk1` at raw `0x8100000 + 10 448 868 − 8`:
  `1f 2d c8 d7 13 d6 47 35` = CRC **`0x3547D613`**.  No region-0 write.
* Region 1 (64 MiB at raw `0x29000000`, `bs=1M skip=656 count=64`): sha256
  **`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`**.  No write.
* hd3: the live root is ext4 (`/dev/loop0` superblock read: magic `53 ef`), 56.5 G
  (`df`), the flashed image `5d3eac8f…`; `/etc/pb-grown` present (the first boot
  grew it).
* `BOOTCTR_FCTRY` **`0x2f`** (`dmesg | grep pbrpmb`, `≥ 0x1e, no write`).
* `nproc` 2, `taskset -p 1` = **3**; `pvrsrvkm` loaded; panel `connected`,
  backlight 255; **1 DSI error** in the 5 h boot (a transient at t=1911 s:
  `Failed to set dsi_if_enable to 0`, then null packets sent after 1 poll — not
  the session-70 `NO PACKET_SENT` wedge); desktop processes from the boot.
* GEM tree: **84 objects, 174,493,696 B total, 26,861,568 B pinned (25.6 MiB)**,
  largest 9,707,520 B — up from the fresh-boot 8 objects/14.7 MB.  The pinned sum
  is below the 128 MiB TILER but higher than the 9.5 MiB fresh baseline; the
  9.7 MB objects are unpinned.  **Reboot before the Kumo test.**
* Wi-Fi `wlan0 connected` 192.168.0.231; battery Full 100 %; SSH over USB works.

## Inferred

* **The first-boot black panel was a DPMS-off wedging the DSI**, not a rebake
  defect: the initrc binds the power-button *release* to a DPMS toggle, and the
  session-59/70 measurements already showed DPMS resume wedges the link.  The
  clean second boot and the transient 1-poll DSI event support this (strong, but
  the exact button press was not instrumented).
* **Kumo implements its own `WPEDisplay`/`WPEView`/`WPEToplevel`** in Rust
  (`src/engine/webkit/platform.rs`: `DisplayImpl`, `ViewImpl`, `ToplevelImpl`),
  so the engine needs `ENABLE_WPE_PLATFORM=ON` with its built-in DRM/headless/
  Wayland backends off and the legacy libwpe API off.  The configure summary
  (`ENABLE_WPE_PLATFORM ON`, `WPE_PLATFORM_* OFF`, `USE_GBM/LIBDRM ON`) matches.
* **The GEM growth over 5 h is a slow buffer accumulation**, not the 8× pitch
  bug: every measured object is at 1.00× sizes, and the pinned sum is 25.6 MiB.
  The 9.7 MB objects (`1024×2370×4`) are unpinned and need a per-object source;
  the engine/Kumo installs are not GPU clients.

## Dead ends and wrong assumptions (valuable — do not skip)

* **The handoff's "the delta is four items" was wrong.**  Reading the image
  found the ARMv6 module tree, the APK initrc conflict, and the missing
  permissions set.  [traps 356/357]
* **The handoff's "the udev/polkit rules are empty on the live root, so not
  needed" was wrong for the goal**: the operator's brightness/Wi-Fi failures
  were exactly those missing files.  [trap 358 for the `udevadm trigger` action]
* **A host-side `cargo check` is not the cheap API gate here.**  It fetched the
  optional servo/stylo git trees (3.1 GB in `~/.cargo`) and compiled ~200
  crates before it was stopped; the root filesystem fell to 4.8 GB free.  The
  static header comparison answers it in seconds.  [report 273 §1]
* **The GTK tarball (`webkitgtk-2.54.3.tar.xz`) contains no WPE port.**  The
  first patch test failed on `Source/WebKit/UIProcess/wpe/…`; the WPE release
  tarball is `wpewebkit-2.54.3.tar.xz` on `wpewebkit.org`.  [report 273 §2]
* **crossdirect cannot install an armv7-only makedepend in the native chroot.**
  Kumo's `wpewebkit` dependency aborted the native `apk`; the Rust package must
  use `--no-cross` (qemu) so the dependency goes into the armv7 buildroot.
* **`clang23` in the buildroot has `libclang-cpp`, not the C API `libclang.so`.**
  `xkbcommon-sys`'s bindgen failed twice; `clang18-libclang` +
  `LIBCLANG_PATH=/usr/lib` fixed it.
* **pmbootstrap's `kill_sccache()` is fatal under `--no-cross`**: it runs
  `sccache --stop-server` in the native chroot, where sccache is not installed.
  A local pmbootstrap patch tries the build chroot and never makes cleanup
  fatal.  [report 274]
* **`CARGO_TARGET_DIR=/home/pmos/cache` is inside the chroot and is lost on
  every zap** (the catacomb comment was misleading); Kumo now uses the
  persistent `/mnt/pmbootstrap/rust` mount.
* **Kumo 1.10.0 is not clean with the servo feature off**, despite the handoff's
  "properly cfg-gated": `config.rs` imports `ServoState` unconditionally,
  `window.rs` imports `tracing::error` only with both features, and
  `engine/mod.rs` gates `AtomicU32` on servo.  Patch `0002-webkit-only-cfg.patch`
  fixes the three.
* **The WPE port's link line lacks `-lintl`** (the GTK port links it), so the
  final `libWPEWebKit` link failed on `libintl_bindtextdomain` /
  `libintl_bind_textdomain_codeset`; `CMAKE_C/CXX_STANDARD_LIBRARIES=-lintl`
  fixes it.  `-DENABLE_JOURNALD_LOG=OFF` is also required (no libsystemd/
  elogind).
* **The `xkbcommon-dl` "unused dependency" warning and the 46 `cfg(servo)`
  warnings are lints**, not errors; the unused `config` local is the gated
  ServoState call.

## Unknown / open questions

* **Kumo on the panel**: does it render on the SGX under catacomb; RSS; first
  render; touch zoom and the touch context menu?  The engine/Kumo are installed,
  so this is the next session's one experiment (reboot first to clear the GEM).
* **The GEM accumulation**: what are the six 9,707,520 B unpinned objects, and
  why does the idle desktop reach 25.6 MiB pinned/166 MiB total after 5 h?  A
  per-object walk with the clients stopped, and a reboot-to-reboot comparison.
* **The single DSI event at t=1911 s**: a transient (null packets after 1 poll)
  or the start of a wedge?  It did not cost the panel this time; capture the
  next one with the operator's visual check.
* **The power-button DPMS binding**: should it be neutralized in the next image?
  It is the likely first-boot black-panel trigger and the only physical button.
* **The engine/Kumo packages are not in the hd3 image**: the next rebake must
  add them (runtime only; the dev packages the engine install pulled are not
  needed on the unit).

## Superseded / uncertain

* **"The hd3 delta is exactly four items"** (HANDOFF-70 §0f.1) — superseded by
  the image reading: it is six (modules, initrc ownership, permissions included).
  Report 272 and DECISIONS §105 are current.
* **"The fork is built against libwpe + libwpebackend-fdo"** (HANDOFF-70 §0f.2)
  — superseded: Kumo uses the new WPEPlatform API and implements the display; the
  legacy API is off.  Report 273/274.
* **"Kumo's two engines are properly cfg-gated"** (HANDOFF-70 §0f.2) —
  superseded by the four compile errors; the webkit-only patch is required.
* **"A host `cargo check` answers the API mismatch cheaply"** (HANDOFF-70) —
  superseded on this host; the static header compare is the cheap check.
* **The first rebake (`9b656d36…`) is not the final image** — it lacks the
  permissions; the final is `5d3eac8f…` (and both lack the engine/Kumo).

## Artefacts produced

* `docs/reports/272-hd3-rebake-2026-11-25.md` — the rebake, the three omissions,
  the flash, the acceptance and the permissions.
* `docs/reports/273-kumo-wpe-api-gate-and-engine-packaging-2026-11-25.md` — the
  API gate, the source, the APKBUILD and the build fixes.
* `docs/reports/274-wpewebkit-kumo-build-2026-11-25.md` — the successful engine
  and Kumo builds and the packages.
* `tools/rebake_hd3.py` (+ `tools/rebake-hd3.sh`) — the file-level hd3 bake,
  root or `debugfs`, with verification.
* `rootfs/polkit/50-playbook-desktop.rules`, `rootfs/udev/90-playbook-backlight.rules`,
  `rootfs/deviceinfo.d/bench-overrides`, `rootfs/README-permissions.md` — the
  permissions set, now baked by the rebake tool.
* `local/work/nura/pmaports/main/wpewebkit/APKBUILD` + six patches;
  `local/work/nura/pmaports/main/kumo/APKBUILD` + `0001-drop-optional-servo.patch`
  + `0002-webkit-only-cfg.patch`; `run-build-wpewebkit.sh`, `run-build-kumo.sh`
  (local, gitignored).
* Packages: `wpewebkit-2.54.3-r1000.apk` (sha256 `032e5121…`),
  `kumo-1.10.0-r1000.apk` (sha256 `b312ecfb…`).
* Images: hd3 `blackberry-playbook-root-gpu-20261125.img` (`5d3eac8f…`, flashed);
  `…-preperm.img` (`9b656d36…`); base `07555bde…` untouched.
* `docs/captures/rebake-2026-11-25-boot1-evidence.txt` — the first-boot DSI
  wedge, touch IRQ count, GEM tree and second-boot state.
* Docs: STATE, HANDOFF, ROADMAP, KNOWN-ISSUES (traps 356–364), DECISIONS §105/§106,
  reports INDEX, this note.
