# catacomb + a PRIME-only `wl_drm` and the texture-wrap fix (GPU client path, sessions 68–69)

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
rejected.  Report 269 §4c–§4e has the analysis and the bench results (session 68); §4g–§4i is the session-69 root cause and validation.

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

## The NPOT texture fix (`smithay-eglimage-clamp.patch`, pkgrel 10003)

With `wl_drm` in place the clients selected the SGX but their surfaces were
still black.  The cause is GLES2 texture completeness: client buffers are
non-power-of-two (1024x20, 1024x585, …), and Smithay's
`GlesRenderer::import_egl_image()` binds the imported dma-buf without setting
`GL_TEXTURE_WRAP_S/T`, so the default `REPEAT` makes the texture incomplete and
sampling returns opaque black — with no GL error anywhere.  wlroots sets
`CLAMP_TO_EDGE` (`render/gles2/texture.c`), which is why phoc presents the same
client.  The patch adds the two `TexParameteri` calls to `import_egl_image()`
(the shm/surface paths in the same file already do this).

It is applied to the fetched Smithay checkout **after** the two existing
Smithay patches (that order is verified against the pinned revision `dba255c`),
and the APKBUILD is `pkgrel=10003`.  Measured A/B: `tools/bench/egl/pb-gbmsample.c`
samples an imported buffer as `nz=0` without the clamp and `nz=64/64` with it.
Report 269 §4g, trap 347, DECISIONS §102.

## Not baked

The `wl_drm` patch and r10002 live on the **live root**; r10003 (with the clamp)
needs the root-owned build (`local/work/nura/run-build-catacomb.sh`).  The
flasher-restorable hd3 image still carries catacomb r10000.  A reflash reverts
all of it (the trap-343 class) — include it in the next rebake.
