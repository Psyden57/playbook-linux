# GPU client probes (session 69)

Small armv7/musl programs used to localise the black-client bug (report 269
§4g).  They are built on the host with a musl cross toolchain and pushed to
the unit; none needs a device-side compiler.  They were written in session 69
to root-cause the black client surfaces.

## Build

Requires the armv7 musl cross toolchain and the unit's own shared libraries for
linking (the chroot's Mesa is newer):

```sh
CC=<musl-cross>/bin/arm-linux-musleabihf-gcc
# headers: Alpine edge armv7 wayland-dev + mesa-dev, wayland-protocols XML
# libs:   pulled from the unit's /usr/lib (libEGL.so.1, libGLESv2.so.2,
#         libwayland-client.so.0, libwayland-egl.so.1, libgbm.so.1)
$CC -O2 -o pb-egltest pb-egltest.c xdg-shell-protocol.c \
    -I<hdr> -L<unitlib> -Wl,--allow-shlib-undefined \
    -l:libwayland-client.so.0 -l:libwayland-egl.so.1 -l:libEGL.so.1 -l:libGLESv2.so.2
```

`xdg-shell-protocol.c`, `wlr-layer-shell-protocol.c` and
`wayland-drm-protocol.c` come from `wayland-scanner` on the matching XML
(`wayland-protocols`, `wlr-protocols`, and catacomb's
`protocols/wayland-drm.xml`).

**Link EGL/GLES with `dlopen`, not at load time.**  A binary that links
`libEGL.so.1` directly gets `EGL_BAD_MATCH` (or a segfault) on a GBM-platform
surfaceless `eglMakeCurrent`; the compositors `dlopen` it, and so must a probe
that wants to reproduce their path (`pb-gbmimport.c`, `pb-gbmsample.c`).

## The probes

| file | what it does |
|---|---|
| `pb-egltest.c` | minimal presenting client: xdg toplevel + EGL window surface on the **wayland** platform, draws and swaps, and **reads its own drawable back before the swap** (`glReadPixels`) |
| `pb-layerdrm.c` | layer-shell surface carrying a `wl_drm` PRIME buffer we fill ourselves (omapdrm dumb buffer); `shm` argument uses the same pixels via `wl_shm` as the control |
| `pb-gbmsample.c` | **the decisive one**: reproduces catacomb's renderer (GBM display on card0, config+context, surfaceless current via `dlopen`) and samples an imported dma-buf in a shader — `2d` vs `ext`, and with/without `CLAMP_TO_EDGE` |
| `pb-gbmimport.c` | same setup, imports the buffer and reads it back directly |
| `pb-dlopen.c` | proves the dlopen'd EGL/GBM/surfaceless path works where the linked path does not |
| `pb-min.c` | the same test linked directly (kept as the negative control) |

## Result (report 269 §4g)

`pb-gbmsample` with the default `REPEAT` wrap samples a non-power-of-two
dma-buf as **opaque black**; with `GL_TEXTURE_WRAP_S/T = CLAMP_TO_EDGE` it
samples the buffer's actual pixels.  Client buffers are NPOT (1024x20,
1024x585, 640x200, …), and Smithay's `import_egl_image()` does not set the wrap
mode, so every client texture was an incomplete GLES2 texture.  wlroots sets
`CLAMP_TO_EDGE`, which is why the same client presents under phoc.
