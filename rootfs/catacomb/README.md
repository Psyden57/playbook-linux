# catacomb + a PRIME-only `wl_drm` (the GPU client fix, session 68)

catacomb is a Smithay compositor and serves **no `wl_drm`**.  Mesa 21.3.9's
Wayland EGL platform obtains its DRM fd only through that legacy protocol
(`dri2_initialize_wayland_drm()`, `platform_wayland.c:1567`); without it
`eglInitialize()` silently retries with `ForceSoftware = TRUE` and loads
`swrast_dri`, so `MESA_LOADER_DRIVER_OVERRIDE=pvr` is never consulted and every
Wayland client is software-rendered.

`wl-drm-prime.patch` adds a **PRIME-only `wl_drm` global** that reuses
Smithay's existing dmabuf import.  It advertises the render node,
ARGB8888/XRGB8888 and `capabilities(PRIME)`; `create_prime_buffer` becomes a
`wl_buffer` carrying a `Dmabuf`, which `buffer_type()`/`import_buffer()` treat
exactly like a `zwp_linux_dmabuf_v1` buffer.  The legacy GEM-name requests are
rejected.  Report 269 §4c–§4e has the analysis and the bench results.

## Applying it

The patch is against catacomb **v1.5.0** and is applied in the pmaports
`main/catacomb` APKBUILD **before** `cargo fetch --locked` (the patch also
updates `Cargo.lock` with the `wayland-server`/`wayland-backend`/
`wayland-scanner` direct dependencies):

```sh
source="
	$pkgname-v$pkgver.tar.gz::https://git.sr.ht/~undeadleech/catacomb/archive/v$pkgver.tar.gz
	dont-depend-on-systemd.patch
	wl-drm-prime.patch
	smithay-gles2-fallback.patch
	smithay-gles2-profile.patch
	"
...
prepare() {
	cd "$builddir"
	patch -Np1 < "$srcdir"/dont-depend-on-systemd.patch
	patch -Np1 < "$srcdir"/wl-drm-prime.patch
	cargo fetch --target="$CHOST" --locked
	...
}
```

Bump `pkgrel` (the shipped build was **10002**).  The built package is
`catacomb-1.5.0-r10002.apk`; it was installed over the live hd3 root and
validated as described in report 269 §4e.

## Requirements at runtime

`MESA_LOADER_DRIVER_OVERRIDE=pvr` must be in the session environment — the DRM
name (`pvrsrvkm`) does not map to `pvr` on its own.  The Nura root exports it
from `/usr/share/tinydm/env-wayland.d/60-pvr.sh` (session) and
`/etc/profile.d/mesa.sh` (login shells); the desktop-launched clients inherit
it.

## Not baked

This patch plus `catacomb-1.5.0-r10002` live on the **live root** only; the
flasher-restorable hd3 image still carries catacomb r10000.  A reflash reverts
the fix (the trap-343 class) — include it in the next rebake.
