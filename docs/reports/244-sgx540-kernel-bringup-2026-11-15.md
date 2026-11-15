# 244 — SGX540 kernel bring-up: the pvrsgx driver builds on 7.3.3 and binds the GPU

Date: 2026-11-15 (session 55, Linux-era 49).
Bench: the unit boots `7.3.3 #68` (region 0 `0xA8F0F30C`) on the read-only hd3
overlay; the SGX module is loaded with `insmod` and the driver probes.  The
userspace stack is **not yet installed** (the DE is unchanged).

## 0. Headline

The SGX540 driver that mainline lacks now **builds against our 7.3.3 tree and
binds the board's GPU**:

```
pvrsrvkm 56000000.gpu: Enabling quirks 00000001
[drm] Initialized pvr 1.17.4948957 for 56000000.gpu on minor 1
```

`/dev/dri/card1` and `/dev/dri/renderD129` exist, and
`/sys/class/drm/card1/device/uevent` reports `DRIVER=pvrsrvkm`,
`OF_FULLNAME=/ocp/target-module@56000000/gpu@0`,
`OF_COMPATIBLE_0=ti,omap4430-gpu`, `OF_COMPATIBLE_1=img,powervr-sgx540`.  The
system stayed healthy (catacomb still running, the overlay root intact).
The remaining half is userspace: the DDK blobs are glibc/armhf, the root is
musl, and the Mesa that talks to them is a patched build.

## 1. The source

`https://github.com/openpvrsgx-devgroup/linux_openpvrsgx`, branch
**`pvrsrvkm-7.3`**, HEAD **`101c687ac73c384e1ae1a0c50cc40efd6e1f6ba9`** — the
active project's 7.3 branch, which already carries DDK **1.17.4948957** (the
version the reference and pvrports name).  The old `tmlind/linux_openpvrsgx`
that the pvrports `linux-openpvrsgx` APKBUILD pins (6.1) was **not** used.

The driver is `drivers/gpu/drm/pvrsgx`: `Kconfig`, `Makefile`, `pvr-drv.c/h`
(the DRM glue and the OF match table) and `1.17.4948957/eurasia_km` (**358
files, 6.3 MB**).  `pvr-drv.c`'s match table enables
`{ .compatible = "ti,omap4430-gpu" }` under `-Dti_omap4_sgx540_120`, which the
pvrsgx `Makefile` sets for `SOC=omap4 SGX=sgx540 SGX_REV=120`.

## 2. Integration

* **the tree:** the driver is copied to
  `local/kernel-tree/drivers/gpu/drm/pvrsgx/` (only `1.17.4948957`, not the
  other DDK versions);
* **`drivers/gpu/drm/Kconfig`** gains `source "drivers/gpu/drm/pvrsgx/Kconfig"`
  inside the `if DRM` block, and **`drivers/gpu/drm/Makefile`** gains
  `obj-$(CONFIG_SGX) += pvrsgx/`;
* **config:** `CONFIG_SGX=y`, `CONFIG_PVRSGX_1_17_4948957=y` (and its ABI),
  `CONFIG_SGX_DRM=y`, `CONFIG_SGX_OMAP=m`; `olddefconfig` keeps
  `SGX_HW_PERF`, `SGX_PB_PER_CONTEXT`, `SGX_HW_RECOVERY`;
* **one kernel export:** the driver calls
  `__vmalloc_node_range_noprof()` and the devgroup branch exports it; our tree
  gets the same one line in `mm/vmalloc.c`
  (`EXPORT_SYMBOL(__vmalloc_node_range_noprof);` after the function).  Without
  it modpost fails for every variant;
* **the board DTS:** `&sgx_module` is `status = "okay"` (the `omap4.dtsi` target
  module already supplies `gpu@0` with `ti,omap4430-gpu` /
  `img,powervr-sgx540`, IRQ `GIC_SPI 21`, a 32 MB range, `prm_gfx` and the
  L3_GFX clock);
* **the module:** `pvrsrvkm_omap4_sgx540_120.ko`, stripped with `--strip-debug`
  to **347 KB**, vermagic `7.3.3 mod_unload modversions ARMv6 p2v8`; written
  flat into the hd3 image's `/lib/modules/7.3.3/kernel/`.

The pvrsgx Makefile builds **nine** SoC variants (omap3/3630, am3517/3352/am4,
omap4 sgx540, omap4 sgx544, omap5, dra7), 4 MB each; only
`pvrsrvkm_omap4_sgx540_120.ko` was packaged.  Trimming the Makefile to the board
is a later cleanup.

## 3. The image and the flash

```
zImage 6 880 640 B; DTB 96 461 B
image  : 0x967880 bytes; crc 0xA8F0F30C
```

`local/images/kernel-0xA8F0F30C-sgx.img`, sha256 `fb9905f3…`.  The hd3 image
copy with the module (`blackberry-playbook-root-overlay-sgx.img`, sha256
`1fcf0cb0…`) and the both-halves container
`both-0xA8F0F30C-sgx.signed` (sha256 `3be2c73a…`, parse CRC `0x9CD674DA OK`)
were flashed in one bootrom session: `COMPLETE 0x40C0 -> cmd 0x4006`, `done`.
The unit booted `7.3.3 #68`; the overlay and the DE are unchanged.

The DTB was checked with `dtc -I dtb -O dts`: the target module has
`status = "okay"`, the `gpu@0` child with the two compatibles, and
`assigned-clock-rates = <0 0x124f8000>` (19.2 MHz).

## 4. What is left — userspace

The kernel side is done; the userspace stack is the pvrports one:

* **`sgx-ddk-um` 1.17.4948957** (TI DDK userspace): `libsrv_um`, `libusc`,
  `libsrv_init`, `libglslcompiler`, `libGLESv1/2_PVR_MESA`, `libpvr2d`,
  `libpvr_dri_support`, `libPVRScopeServices`, `pvrsrvctl`, and `pvrsrvinit`
  (a tiny `dlopen("libsrv_init.so.1")` + `SrvInit()`).  2.0 MB of blobs.
* **glibc vs musl:** the blobs `NEEDED` `libc.so.6` and use
  `/lib/ld-linux-armhf.so.3`; the Nura root is **musl 1.2.6** with neither
  `gcompat` nor a glibc loader.  The pvrports package depends on `gcompat`.
* **`mesa-pvr-dri-classic` 21.3.9** (the `amber-pvr` tree): the Mesa DRI/EGL/GBM
  that loads `pvr_dri.so` and talks to `libsrv_um`; **`libglvnd`** is its
  companion.  `MESA_LOADER_DRIVER_OVERRIDE=pvr` selects it
  (`etc/profile.d/mesa.sh`).
* The root already has `libdrm`, `mesa`, `mesa-egl`, `mesa-gles`; the pvr stack
  replaces them.

Building the Mesa package needs the pmbootstrap/abuild chroot (present at
`local/work/nura`); the DDK blobs are a prebuilt tarball
(`MightyM17/sgx-ddk-um` at `c7dbb0a0…`).

**The live shortcut was tried and failed.**  `gcompat` 1.1.0 (+ `musl-obstack`,
`libucontext`) was installed on the running overlay from staged apks, the
`ti443x` libs were copied to `/usr/lib` and `pvrsrvctl` to `/usr/bin` (over the
RNDIS link, `usb0` brought up by hand).  `pvrsrvctl` **segfaults even on
`--help`** (`rc=139`), i.e. before any service init: the vendor glibc binary does
not run under that hand-installed loader.  This is not evidence about the kernel
or the blobs — the virtual filesystem changes were in the tmpfs upper and are
volatile — but it means the userspace must be built/installed the pvrports way
(the Alpine/pmbootstrap chroot), not hand-staged on the device.

## 5. Next

1. Build/install the pvr userspace (`sgx-ddk-um` + `mesa-pvr-dri-classic` +
   `libglvnd`, + `gcompat`) into the hd3 image; `pvrsrvinit` at boot.
2. Test with a trivial EGL/GLES 2.0 program; then restart `tinydm`/catacomb and
   check it draws.
3. Re-export the kernel patch (the driver adds 358 files) and decide its
   packaging (main patch vs a vendored archive).

## 6. Artefacts

* `local/kernel-tree/drivers/gpu/drm/pvrsgx/` (the driver, 358 files);
  `drivers/gpu/drm/{Kconfig,Makefile}`, `mm/vmalloc.c`, the board DTS and
  `.config` changed.
* `local/images/kernel-0xA8F0F30C-sgx.img` (sha256 `fb9905f3…`).
* `local/work/nura/images/blackberry-playbook-root-overlay-sgx.img`
  (sha256 `1fcf0cb0…`) and `both-0xA8F0F30C-sgx.signed`
  (sha256 `3be2c73a…`, CRC `0x9CD674DA`).
* Traps 275–279; DECISIONS §77.
