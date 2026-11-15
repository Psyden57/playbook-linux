# 245 — SGX540 userspace: EGL/GLES 2.0 runs on the PowerVR SGX 540; catacomb needs one extension it lacks

Date: 2026-11-15 (session 55, Linux-era 49).
Bench: the unit is on the SGX kernel (`7.3.3 #68`, `pvrsrvkm` bound).  The
userspace stack was installed into the **tmpfs overlay** for the test (volatile;
a reboot restores the base root).  catacomb currently panics at renderer
creation, so the DE is down after the test.

## 0. Headline

With the kernel driver bound and the pvrports userspace installed, a headless
EGL/GLES 2.0 context comes up on the real GPU:

```
PVR_K: UM DDK-(4948957) and KM DDK-(4948957) match. [ OK ]      (pvrsrvinit)
EGL vendor  : Mesa Project (Amber)
EGL version : 1.4 (1.4)
GL_VENDOR   : Imagination Technologies
GL_RENDERER : PowerVR SGX 540
GL_VERSION  : OpenGL ES 2.0 build 1.17@4948957
OK
```

That is ROADMAP item 7a's "a trivial EGL/GLES 2.0 program runs on SGX540".
The remaining DE problem is separate: **catacomb requires
`GL_EXT_unpack_subimage`, which the SGX540 GLES2 driver does not expose**, and
panics at `create renderer` (`src/udev.rs:400`).  pvrports only claims Phosh
(with a patched phoc) and Plasma, not catacomb.

## 1. What was installed

The pvrports prebuilt repository has exactly the stack for `v23.12/armv7`, and
those packages install cleanly on this (Nura edge, musl 1.2.6) root:

| package | version | provides |
|---|---|---|
| `libglvnd` | 1.6.0-r2 | `libEGL.so.1`, `libGLESv2.so.2`, `libGLdispatch`, `libGLX` |
| `mesa-pvr-dri-classic` | 21.3.9-r1 | `libEGL_amber.so.0`, `libgbm.so.1`, `/usr/lib/xorg/modules/dri/pvr_dri.so`, `/usr/share/glvnd/egl_vendor.d/50_amber.json` |
| `sgx-ddk-um` | 1.17.4948957-r1 | `pvrsrvinit`, `libsrv_init` |
| `sgx-ddk-um-ti443x` | 1.17.4948957-r1 | `libsrv_um`, `libusc`, `libGLESv1/2_PVR_MESA`, `libpvr2d`, `libpvr_dri_support`, `libglslcompiler`, `pvrsrvctl` |
| `sgx-ddk-um-openrc` | 1.17.4948957-r1 | the `sgx-ddk-um` init service |

Extra Alpine packages: **`gcompat` 1.1.0-r4** (+ `musl-obstack` 1.2.3-r2,
`libucontext` 1.5.2-r0) for the glibc DDK blobs, and **`wayland-libs-server`
1.26.0-r0** (mesa-pvr's EGL needs `libwayland-server.so.0`, which the root did
not have).

`apk` resolves the replacement coherently: it purges `mesa` 26.2.6,
`mesa-{egl,gl,gles,dri-gallium,gbm}`, `llvm23-libs` and `spirv-tools`, and
installs the pvr stack.  The root already had every other soname (libX11/xcb,
libdrm, libexpat, libgcc_s, libstdc++, musl).

**The same packages also build from source on this tree.**  `pmbootstrap build
--arch armv7 mesa-pvr-dri-classic` (and `libglvnd`) succeeded in the edge chroot
(`local/work/nura`, after `pkexec` for root); the resulting APKs are in
`work/packages/edge/armv7/`.  The prebuilt v23.12 ones were what ran the test.

## 2. The GPU handshake

`pvrsrvinit` returned 0 and the kernel logged the DDK agreement:

```
PVR_K: UM DDK-(4948957) and KM DDK-(4948957) match. [ OK ]
```

The EGL/GLES probe is `local/work/egltest.py` (Python `ctypes`, no compiler on
the host side): it takes the **surfaceless** EGL platform
(`EGL_PLATFORM_SURFACELESS_MESA = 0x31DD`), creates a 16×16 pbuffer context and
reads `GL_VENDOR`/`GL_RENDERER`/`GL_VERSION`.  Run on the unit with
`MESA_LOADER_DRIVER_OVERRIDE=pvr python3 /tmp/egltest.py`.  The full chain is
libglvnd → `libEGL_amber` (the vendor JSON) → `pvr_dri.so` (forced by
`MESA_LOADER_DRIVER_OVERRIDE=pvr`) → `libsrv_um`/`libusc` → `/dev/dri/card1`.

## 3. catacomb: `GL_EXT_unpack_subimage`

Restarting tinydm with `MESA_LOADER_DRIVER_OVERRIDE=pvr` (a new
`/usr/share/tinydm/env-wayland.d/60-pvr.sh`) makes catacomb reach the pvr
renderer and then **panic**:

```
thread 'main' (3704) panicked at src/udev.rs:400:56:
create renderer: GLExtensionNotSupported(["GL_EXT_unpack_subimage"])
```

So the compositor, not the GPU, is the blocker.  pvrports' own TODO lists
"Add support for EXT_read_format_bgra and others", and its supported UIs are
**Phosh** (needs their patched `phoc`) and **Plasma-Mobile** — catacomb is not
among them.  Options for the DE: a wlroots compositor (phoc/sway/Sxmo-dwm) or
adding the missing path to catacomb/smithay.

## 4. State and next

* The install was in the **tmpfs upper**: a reboot returns to the base hd3 root
  (mesa 26.2.6, no pvr userspace), and the SGX module must be `insmod`ed again.
* **Next:** (a) bake the stack into the hd3 image (the nine APKs + the module
  autoload + the service) so the GPU survives a boot; (b) choose and bring up a
  compositor that works on this GLES2 driver (Phosh/phoc or a wlroots DE), or
  extend catacomb; (c) the kernel patch re-export (358 files) still needs its
  packaging decision.

## 5. Artefacts

* `local/work/egltest.py` — the headless EGL/GLES probe.
* `local/work/apks-pvr/` — the prebuilt pvrports APKs + `gcompat` chain +
  `wayland-libs-server` + `mesa-demos` (`es2_info`) + the built packages, and
  the local `repo/armv7` used for the offline install.
* `local/work/nura/work/packages/edge/armv7/` — the source-built
  `libglvnd`/`mesa-pvr-dri-classic` APKs.
* Traps 280–284; DECISIONS §78.

## 6. Baked into hd3 (same session) — the stack survives a boot

The test install was in the tmpfs upper, so it was baked into the hd3 image:

1. `blackberry-playbook-root-overlay-sgx.img` was copied to
   `blackberry-playbook-root-pvr.img` and loop-mounted rw (as root, via
   `pkexec`).
2. The nine APKs were installed **offline** with the x86_64 static `apk` from
   the pmbootstrap work dir:
   `apk --root /mnt/pbh3 --arch armv7 --allow-untrusted --no-network
   --no-scripts --repositories-file /dev/null --repository <local repo> add
   musl-obstack gcompat wayland-libs-server libglvnd mesa-pvr-dri-classic
   sgx-ddk-um sgx-ddk-um-ti443x sgx-ddk-um-openrc`.  The resolver made the same
   coherent replacement (purged mesa 26.2.6/llvm/spirv, installed the pvr
   stack).
3. The boot wiring: `pvrsrvkm_omap4_sgx540_120` appended to `/etc/modules`,
   `depmod -b /mnt/pbh3 7.3.3` (the text `modules.dep` now lists the module),
   `/etc/runlevels/default/sgx-ddk-um` linked to `/etc/init.d/sgx-ddk-um`, and
   `/usr/share/tinydm/env-wayland.d/60-pvr.sh` with
   `MESA_LOADER_DRIVER_OVERRIDE=pvr`.
4. `e2fsck -fn` exit 0; the image is `85b44deb…` (625 999 872 B).  Because only
   hd3 changed, it was flashed **alone** with
   `bbusb.py flash --component … --dest-block 0x4915 --from-bootrom
   --password 0000 --apply` (626 065 440 B, `COMPLETE 0x40C0 -> cmd 0x4006`,
   `done`); region 0 (`0xA8F0F30C`) and region 1 are unchanged.

**Verified on the fresh boot:** the module autoloads (`pvrsrvkm_omap4_sgx540_120`
in `lsmod`), `sgx-ddk-um` starts (`rc-status`), dmesg has the probe and
`PVR_K: UM DDK-(4948957) and KM DDK-(4948957) match. [ OK ]`, `/dev/dri/card1` +
`renderD129` exist, and the same EGL probe still reports `PowerVR SGX 540`.
The counter read `0x30` and correctly did not top up (`0x30 >= 0x1e`).

**Consequence:** catacomb is the only compositor installed and it panics on
`GL_EXT_unpack_subimage`, so the panel is black with the backlight on — the
display and the GPU are healthy, the session is not.  Installing a compatible
compositor (Phosh/phoc, Plasma, or a wlroots DE) is the next step.
