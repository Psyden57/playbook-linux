# Report 274 — WPE WebKit 2.54.3 (fork) and Kumo 1.10.0 are built and installed

Date: 2026-11-25 (session 71).  Chroot builds on the host; packages installed on
the live hd3 root.  Follows report 273 (the API gate and the APKBUILD).

## 0. Headline

Both packages now exist and are installed on the unit:

| package | size | sha256 | contents |
|---|---|---|---|
| `wpewebkit-2.54.3-r1000.apk` | 34 MB (72 MB installed) | `032e5121b6e6b274f021c808c2d12f0e03e475dd7d78788699625a6f33423d23` | `libWPEWebKit-2.0.so.1.11.6`, `wpe-webkit-2.0`/`wpe-platform-2.0` pc files, headers, `/usr/libexec/wpe-webkit-2.0/{WPEWebProcess,WPENetworkProcess,WPEGPUProcess}` |
| `kumo-1.10.0-r1000.apk` | 2.6 MB (13 MB installed) | `b312ecfbd8b2366b15fab314092af2b6b8ff4a586d652e0c1ed934d61d81f220` | `/usr/bin/kumo`, `Kumo.desktop` |

On the unit: `apk list -I wpewebkit kumo` → `wpewebkit-2.54.3-r1000`,
`kumo-1.10.0-r1000`; `pkg-config --modversion wpe-webkit-2.0` → 2.54.3;
`kumo --version` → `kumo 1.10.0`.  **The Kumo GUI experiment is not run.**

## 1. The engine build

`main/wpewebkit/APKBUILD`: official **WPE 2.54.3** tarball + the fork's six
commits as patches (report 273 §2).  `-DPORT=WPE`, `ENABLE_WPE_PLATFORM=ON`, the
legacy libwpe API and the built-in DRM/headless/Wayland platform backends OFF
(Kumo implements its own display), no introspection/docs/webdriver/cog,
`USE_SYSTEM_MALLOC=ON`, `USE_GBM/LIBDRM=ON`, clang/lld.

The build failed five times before succeeding; each fix is permanent in the
APKBUILD:

| failure | fix |
|---|---|
| `CMake Error: unable to find a build program for Ninja` | `samurai` (provides `ninja`), `lld`, `llvm` in makedepends |
| `libsystemd or libelogind are needed for ENABLE_JOURNALD_LOG` | `-DENABLE_JOURNALD_LOG=OFF` |
| `ld.lld: undefined symbol: libintl_bindtextdomain` at the final link | `-DCMAKE_C_STANDARD_LIBRARIES=-lintl -DCMAKE_CXX_STANDARD_LIBRARIES=-lintl` |
| abuild rejected the maintainer line (no email) | `# Maintainer: … <bench@example.com>` |

The successful build ran under **crossdirect** (~2 h, then a full rebuild on the
`-lintl` fix).  The ccache is warm (`work/cache_ccache_armv7`, 148 MB); note
that `/home/pmos/cache` is inside the chroot and is lost on a zap.

## 2. The Kumo build

`main/kumo/APKBUILD`, `--no-default-features --features webkit`, with two
source patches:

* **`0001-drop-optional-servo.patch`** — removes the `servo` feature and the
  optional servo/stylo/keyboard-types git dependencies.  Cargo otherwise fetches
  ~3 GB even with the feature off (proven on the host).  The code paths are
  `cfg`-gated off.
* **`0002-webkit-only-cfg.patch`** — Kumo 1.10.0 is **not** clean without servo:
  `src/config.rs` imports `crate::engine::servo::ServoState` unconditionally,
  `src/window.rs` imports `tracing::error` only under
  `cfg(all(servo, webkit))`, and `src/engine/mod.rs` gates `AtomicU32` on servo.
  The patch makes `AtomicU32`/`error` unconditional and gates the ServoState
  import/call.  (The remaining `unexpected cfg servo` and unused-`config`
  warnings are lints.)

Build path and fixes:

* **`--no-cross` (qemu-only).**  crossdirect tries to install the armv7-only
  `wpewebkit` dependency in the native x86_64 chroot and aborts; qemu installs it
  in the armv7 buildroot.  The Rust toolchain (1.100.0) runs under qemu.
* **`sccache`** is set as `RUSTC_WRAPPER` by pmbootstrap; it must be in
  makedepends.  pmbootstrap's `kill_sccache()` then calls
  `sccache --stop-server` in the **native** chroot (no binary under
  `--no-cross`) and aborts the whole run with 127; a **local pmbootstrap patch**
  (`pmb/chroot/shutdown.py`) tries the native and build chroots and never makes
  cleanup fatal.
* **libclang**: `xkbcommon-sys`'s bindgen needs `libclang.so`; Alpine's
  `clang23` ships only `libclang-cpp`.  `clang18-libclang` +
  `LIBCLANG_PATH=/usr/lib` supplies `libclang.so.18.1`.
* **`CARGO_TARGET_DIR=/mnt/pmbootstrap/rust/cargo-target-kumo`** — the
  persistent host mount.  `/home/pmos/cache` is inside the zapped chroot; the
  earlier path lost every compiled crate on each retry.  The mount must be
  writable by the build user (`chown 12345:12345 work/cache_rust`).

The successful build took ~16 min (dependency crates already cached), with
`kumo` itself recompiled after the cfg patch.

## 3. Install and smoke test

`apk add --allow-untrusted /tmp/wpewebkit.apk` pulled 58 packages (libsoup3,
gstreamer, gst plugins, the dev packages the pc files depend on) — 470 MB
installed.  `apk add --allow-untrusted /tmp/kumo.apk` installed cleanly.
`pkg-config` resolves both 2.54.3 pc files; `/usr/libexec/wpe-webkit-2.0/` has
the three helper processes; `kumo --version` and `--help` run on the unit.

**Both are on the live root only**; the flashed hd3 image (`5d3eac8f…`) does not
contain them.  The next rebake must include the two runtime packages.

## 4. What is next (the runtime experiment)

Reboot first (the idle GEM has grown to 84 objects / 166 MB total / 25.6 MiB
pinned after 5 h).  Then run Kumo under catacomb with the session environment
(`XDG_RUNTIME_DIR=/run/user/10000`, `WAYLAND_DISPLAY=wayland-1`,
`MESA_LOADER_DRIVER_OVERRIDE=pvr`, `DBMFIX=1`,
`LD_PRELOAD=/usr/local/lib/dbmfix.so`), open a page, and record: RSS,
first-render time, `pvr_dri`/`GL_RENDERER`, and whether touch zoom and the touch
context menu work.  Expected first failure modes: no `WPEDisplay` accepted by
WebKit (Kumo's Rust display), or the WebKit helper process paths/permissions.

## 5. Artefacts

* `local/work/nura/pmaports/main/wpewebkit/` — APKBUILD + six patches.
* `local/work/nura/pmaports/main/kumo/` — APKBUILD + two patches.
* `local/work/nura/run-build-wpewebkit.sh`, `run-build-kumo.sh` (root).
* Packages in `local/work/nura/work/packages/edge/armv7/` (sha256 above).
* Local pmbootstrap patch: `pmb/chroot/shutdown.py` (`kill_sccache`).
