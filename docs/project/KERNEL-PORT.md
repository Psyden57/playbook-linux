# Kernel base — 7.3.3 (port complete; the SGX driver vendored)

The port is **done**: `local/kernel-tree` is upstream **Linux 7.3.3**, the unit
boots it to the hd3 Nura root (read-only + overlayfs) and to the region-1
BusyBox userland.

* Base: **upstream 7.3.3** (not the pmOS package, which is still `!armhf`).
* Patch: `kernel/patches/0001-playbook-winchester-7.3.3.patch`, **64 files /
  8 800 lines**, sha256 `014eb8a8…`, verified 0 rejects / 64/64 byte-identical
  against pristine 7.3.3 targets (session 56).  It includes the SGX integration
  (`drivers/gpu/drm/{Kconfig,Makefile}`, `mm/vmalloc.c`, the DTS, the config)
  and the session-56 Munin Y fix; the **362-file SGX driver itself is vendored**
  at `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` (sha256 `a9d04d3c…`;
  extract it into `drivers/gpu/drm/` after applying the patch — see
  `kernel/vendor/README.md`).  A build from the patch alone is missing the
  driver until that copy step.
* **The SGX delta (exported in session 56):**
  * `drivers/gpu/drm/pvrsgx/` — the `openpvrsgx-devgroup/linux_openpvrsgx`
    branch `pvrsrvkm-7.3` (HEAD `101c687a…`) driver: `Kconfig`, `Makefile`,
    `pvr-drv.c/h`, and `1.17.4948957/eurasia_km` (**362 files, 6.5 MB**, vendored);
  * `drivers/gpu/drm/Kconfig` sources `drivers/gpu/drm/pvrsgx/Kconfig`;
    `drivers/gpu/drm/Makefile` has `obj-$(CONFIG_SGX) += pvrsgx/`;
  * `mm/vmalloc.c` gains `EXPORT_SYMBOL(__vmalloc_node_range_noprof);` (the
    driver calls it; the devgroup branch has the same line);
  * the board DTS `&sgx_module` is `status = "okay"` (the `omap4.dtsi` node
    supplies `gpu@0` with `ti,omap4430-gpu` / `img,powervr-sgx540`).
* Config: `kernel/winchester-7.3.3.config` (the canonical copy).  Session 55
  adds the SGX block (`CONFIG_SGX=y`, `CONFIG_PVRSGX_1_17_4948957=y` +
  `_ABI`, `CONFIG_SGX_DRM=y`, `CONFIG_SGX_OMAP=m`, the HW-perf/recovery
  defaults) and the FB helpers SGX_DRM selects (`FB_CFB_FILLRECT/COPYAREA/
  IMAGEBLIT`, `FB_IOMEM_FOPS/HELPERS`); session 54 added
  `CONFIG_DYNAMIC_DEBUG=y` and `CONFIG_OVERLAY_FS=y`.
* `initramfs/init.c`: `ROOTFS_RAW_OFF = 0x49000000` (hd3 at loader block
  `0x4915`), the `lstat()` hand-off, and the read-only root: the loop
  `LO_FLAGS_READ_ONLY`, the ext4 `MS_RDONLY`+`noload`, a 256 MiB tmpfs
  upper/work and overlayfs on `/newroot`.  Unchanged in session 55; the region-0
  image is `local/images/kernel-0xAB899C49-touch.img` (`0x9676A0` B, CRC
  `0xAB899C49`, sha256 `c37ee2fe…`, with `&sgx_module` enabled and the session-56
  Munin Y fix).
* Session 54's instrumentation change: the general MMC/GPIO/sysc/probe prints
  are `pr_debug` (`PBSIO`/`PBINI`/`PBX`/`PBM` in `omap_hsmmc.c`, `PBC` in
  `mmc/core/{core,host,mmc_ops}.c`, `PBR` in `drivers/base/dd.c`, `PBG` in
  `gpio-omap.c`, `PBS` in `ti-sysc.c`, `PB:` in `arch/arm/mm/mmu.c`), with
  `PBREQ`/`PBIRQ` behind the `pb_trace` module parameter.
* Session 56 adds the Munin Y fix: the controller's Y axis is inverted
  relative to the display (physical top → evdev 577–591), so
  `playbook-munin.c` reports `panel_h - 1 - munin_map(...)`.
* Earlier session deltas already in the patch: the MUSb HSDMA fix
  (`musb_gadget.c`, `u_ether.c`), the UART2 CTS/RTS pads, the WL1283 CAM/4-sr
  (`wl12xx`), the CLK32KG (`clk-twl.c`), the audio path (McBSP3, the speaker
  line-out route, `omap-mcbsp-priv.h`), the charger/display/sensor/touch set.
* 6.15.11 reference: `local/pristine/linux-6.15.11.tar.xz`
  `local/kernel-tree-6.15.11`, and
  `kernel/patches/0001-playbook-winchester-6.15.11.patch` (incomplete by
  history; reference only).  The tarball is authoritative; extract it with
  `tools/get_pristine.sh 6.15.11`.
* The full target analysis, per-file risk and checkpoint log are archived in
  [`docs/history/KERNEL-PORT.md`](../history/KERNEL-PORT.md).

Build and flash loop: `docs/project/DEVELOPMENT.md` §3 (apply the patch,
extract `kernel/vendor/pvrsgx-…tar.xz` into `drivers/gpu/drm/`, then build).
