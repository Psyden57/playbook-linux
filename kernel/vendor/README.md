# Vendored third-party sources

## `pvrsgx-1.17.4948957-101c687a.tar.xz` — the SGX540 kernel driver

The PlayBook's GPU (PowerVR SGX 540, DDK **1.17.4948957**) has no mainline
driver.  The driver used by this project is the **openpvrsgx-devgroup**
kernel driver, vendored here as a pinned archive because it is a third-party
GPL-2 source tree of **362 files / 6.5 MB** (mostly the DDK's `eurasia_km`).
It is kept out of `kernel/patches/0001-playbook-winchester-7.3.3.patch` so
the board patch stays reviewable.

| field | value |
|---|---|
| upstream | `https://github.com/openpvrsgx-devgroup/linux_openpvrsgx` |
| branch | `pvrsrvkm-7.3` |
| commit | `101c687ac73c384e1ae1a0c50cc40efd6e1f6ba9` (2026-10-19) |
| DDK | `1.17.4948957` |
| archive | `pvrsgx-1.17.4948957-101c687a.tar.xz` (606 964 B) |
| sha256 | `a9d04d3cc9219d8dfb465156afeb63e7f12fd4e22a786920546cac5f291fea1e` |
| licence | GPL-2.0-only (the driver's module code; the archive carries the DDK's `GPL-COPYING`/`MIT-COPYING`) |

The archive contains only `pvrsgx/Kconfig`, `pvrsgx/Makefile`,
`pvrsgx/pvr-drv.{c,h}` and `pvrsgx/1.17.4948957/` — the subset this board
builds.  The upstream checkout also carries every other DDK version under
`pvrsgx/`; they are not included.

### How to use it

The main patch expects `drivers/gpu/drm/pvrsgx/` to exist.  Extract the
archive into the kernel tree **after** applying
`kernel/patches/0001-playbook-winchester-7.3.3.patch` and **before**
building:

```bash
K=local/kernel-tree
patch -d "$K" -p1 < kernel/patches/0001-playbook-winchester-7.3.3.patch
tar -xJf kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz \
    -C "$K/drivers/gpu/drm"
cp kernel/winchester-7.3.3.config "$K/.config"
make -C "$K" ARCH=arm CROSS_COMPILE=arm-linux- olddefconfig
make -C "$K" ARCH=arm CROSS_COMPILE=arm-linux- -j"$(nproc)" zImage modules
```

The integration itself lives in the main patch and the config:
`drivers/gpu/drm/Kconfig` sources `pvrsgx/Kconfig`,
`drivers/gpu/drm/Makefile` adds `obj-$(CONFIG_SGX) += pvrsgx/`,
`mm/vmalloc.c` exports `__vmalloc_node_range_noprof` (the driver calls it and
modpost fails without the GPL export), the board DTS enables `&sgx_module`,
and `kernel/winchester-7.3.3.config` selects `CONFIG_SGX=y`,
`CONFIG_PVRSGX_1_17_4948957=y` (+ABI), `CONFIG_SGX_DRM=y`, `CONFIG_SGX_OMAP=m`.
The pvrsgx Makefile builds nine variants; only
`pvrsrvkm_omap4_sgx540_120.ko` is packaged.

Reference: `docs/reports/244-sgx540-kernel-bringup-2026-11-15.md`,
`DECISIONS.md` §77, traps 275/276.
