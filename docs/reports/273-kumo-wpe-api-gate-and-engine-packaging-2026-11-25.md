# Report 273 — Kumo/WPE: the API gate passes and the engine is packaged for the chroot

Date: 2026-11-25 (session 71).  Host only; no device writes.  Follows
HANDOFF-70 §0f.2.

## 0. Headline

**The crate/engine API gate passes, statically.**  Kumo vendors its own
`wpe-platform{,-sys}` and `wpe-webkit{,-sys}` bindings, and they were generated
against the fork: at `e4f4ac71568a26f27f85d52eee42c4219dc84ae4`,
`Source/WebKit/WPEPlatform/wpe/WPEGestureController.h` and `WPEView.h` declare
exactly the symbols Kumo binds —

```
wpe_view_get_gesture_controller / wpe_view_set_gesture_controller
wpe_gesture_controller_get_type / _handle_event / _cancel / _get_gesture
wpe_gesture_controller_get_gesture_position / _get_gesture_delta
wpe_gesture_controller_is_drag_begin / _get_zoom_delta
```

— so no host build was needed to answer HANDOFF §0f.2's first check.

**The engine is packaged for the pmaports chroot.**  `main/wpewebkit/APKBUILD`
builds **WPE WebKit 2.54.3 from the official WPE tarball plus the fork's six
commits**, all of which apply cleanly (verified).  It is the WPE port with
EGL/GLES2, `ENABLE_WPE_PLATFORM=ON`, and the legacy libwpe API and the built-in
DRM/headless/Wayland platform backends **off** — because Kumo implements its own
`WPEDisplay`/`WPEView` in Rust (it passes a DRM node and dmabuf feedback).

The build itself is the long pole and must run as root in the pmbootstrap
chroot (the chroot is root-owned); `local/work/nura/run-build-wpewebkit.sh` is
the wrapper.

## 1. The API gate (measured, no guess)

Kumo's `web-platform-sys` declares the symbols above
(`wpe-platform-sys/src/lib.rs:4269-4338`), and its `GestureController` wrapper
is `wpe-platform/src/auto/gesture_controller.rs`.  A static compare against the
fork's headers at the pinned rev (fetched from GitHub raw at
`e4f4ac71568a…`) shows the same set, including the view accessors in
`WPEView.h`.  The crate was generated from this fork's GIR, so the binding side
matches by construction; what remains is the engine ABI at build time.

**The earlier host `cargo check` was abandoned.**  `cargo check
--no-default-features --features webkit` on the host resolved and fetched the
**optional** servo/stylo git dependencies (3.1 GB in `~/.cargo/git`) and
compiled ~200 crates before it was stopped; it was network/IO-bound, not a real
build.  That cache was removed (the root filesystem had fallen to 4.8 GB free).
The real build belongs in the chroot, and the static gate above is cheaper and
answers the same question.

## 2. The source: exact, and small

The fork's six own commits sit **directly on the 2.54.3 release commit**
(`c01fbd90`), so the official WPE tarball plus those patches is the fork's tree:

| source | sha512 |
|---|---|
| `https://wpewebkit.org/releases/wpewebkit-2.54.3.tar.xz` (49,591,296 B) | `0c5d059b…` |
| `0001-wpe-context-menu.patch` (`b86110562d`) | `6752dcaa…` |
| `0002-touch-context-menu.patch` (`dd88b3eb8d`) | `201f9fb9…` |
| `0003-download-cancel.patch` (`5ef6e19e41`) | `380279cc…` |
| `0004-touch-page-zoom.patch` (`41dbc7cc99`) | `2a239d65…` |
| `0005-text-search-colors.patch` (`8e81edd149`) | `6cf6f6b7…` |
| `0006-preedit-segfault.patch` (`e4f4ac7156`) | `bb420ef2…` |

The patches apply **cleanly in that order** to the 2.54.3 WPE tree (`patch -Np1`,
no rejects, no `.orig`).  This avoids the 6.4 GB git checkout (which is mostly
LayoutTests); the release tarball excludes tests.  The GTK tarball
(`webkitgtk-2.54.3.tar.xz`) **does not contain the WPE port** and must not be
used.

## 3. The APKBUILD

`local/work/nura/pmaports/main/wpewebkit/APKBUILD`, `pkgver=2.54.3`,
`pkgrel=1000`, `arch="armv7"`, `options="!check net"`.  Key choices, from the
WPE port's own `OptionsWPE.cmake`:

* `-DPORT=WPE`, `-DENABLE_WPE_PLATFORM=ON` — the new platform API Kumo uses.
* `-DENABLE_WPE_LEGACY_API=OFF` — no libwpe/libwpebackend-fdo; Kumo does not use
  the legacy `wpe_view_backend`.
* `-DENABLE_WPE_PLATFORM_{DRM,HEADLESS,WAYLAND}=OFF` — Kumo implements the
  display itself, so the engine needs none of the built-in backends.
* `-DENABLE_INTROSPECTION=OFF`, `-DENABLE_DOCUMENTATION=OFF`,
  `-DENABLE_WEBDRIVER=OFF`, `-DENABLE_COG=OFF`, `-DENABLE_MINIBROWSER=OFF`.
* `-DENABLE_SPELLCHECK=OFF`, `-DENABLE_SPEECH_SYNTHESIS=OFF`,
  `-DUSE_ATK=OFF`, `-DUSE_FLITE=OFF`, `-DUSE_LIBHYPHEN=OFF`,
  `-DUSE_LIBBACKTRACE=OFF`, `-DUSE_SYSPROF_CAPTURE=OFF`,
  `-DUSE_AVIF=OFF -DUSE_LCMS=OFF -DUSE_JPEGXL=OFF -DUSE_WOFF2=OFF` — fewer
  dependencies and a smaller build.
* `-DUSE_SYSTEM_MALLOC=ON -DUSE_MIMALLOC=OFF` — mimalloc's initial-exec TLS
  breaks dlopen on musl (same reason as Alpine's armv7 build).
* `-DUSE_GBM=ON -DUSE_LIBDRM=ON` (the GPU process depends on GBM), and the
  compiler setup from Alpine's webkit2gtk: `CC=clang`, `CXX=clang++`,
  `-fuse-ld=lld`, `-g1`, `-U_FORTIFY_SOURCE`.

`local/work/nura/run-build-wpewebkit.sh` runs
`pmbootstrap build --arch armv7 --force wpewebkit` with
`WEBKIT_NINJA_COMPILE_MAX=4` (unified sources need ~2 GiB per job; the chroot is
qemu-emulated).  It must run as root.

## 4. What follows

1. **Build the engine** in the chroot (root).  Then inspect the package: the
   `wpe-platform-2.0.pc` / `wpe-webkit-2.0.pc` files and the installed
   `libWPEPlatform-2.0.so` / `libWPEWebKit-2.0.so` are what Kumo's
   `system-deps` build scripts need.
2. **Package Kumo** (`--no-default-features --features webkit`) as a second
   pmaports package.  Note: Kumo's `Cargo.lock` also pins the optional
   servo/stylo git trees, and `cargo fetch --locked` pulls them even when the
   feature is off; the Kumo APKBUILD should drop the servo dependency (a small
   source patch) or fetch selectively, or the chroot will download 3 GB.  Also
   match the chroot's rustc to the host's for a `--cross` build (the known
   speedup).
3. **Run it under catacomb** and report RSS, first-render time, touch zoom and
   the touch context menu (HANDOFF §0f.2's pass criterion).

**Escape hatch unchanged:** Kumo's Servo backend
(`--no-default-features --features servo`) needs no engine build; it is the
cheap attempt if the engine build proves impractical.  Skip cog.

## 5. Artefacts

* `local/work/nura/pmaports/main/wpewebkit/APKBUILD` + the six patches.
* `local/work/nura/run-build-wpewebkit.sh`.
* `local/work/src/wpe-src/wpewebkit-2.54.3.tar.xz` (the downloaded release) and
  the patches (`local/work/src/wpe-src/`).
* The Kumo checkout (host, unused): `local/work/src/kumo`; its
  `wpe-platform-sys` is the source of the API symbols above.
