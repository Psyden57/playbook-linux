# Kernel base — 7.3.3 (port complete; the SGX driver vendored)

> **Session 76: the kernel base and patch are UNCHANGED** — no kernel-source
> edit, no re-export, no region-0 flash.  That session's work was entirely in the
> **vendored open SGX driver** (`pvrsgx_*`, a module, in the gitignored
> `local/work/powervr_sgx` and `local/kernel-tree-sgx`) and in the microkernel.
> The patch below is the session-67 export and is still current.

The port is **done**: `local/kernel-tree` is upstream **Linux 7.3.3**, the unit
boots it to the hd3 Nura root (now read-write and grown; report 249) and to the
region-1 BusyBox userland.

* Base: **upstream 7.3.3** (not the pmOS package, which is still `!armhf`).
* Patch: `kernel/patches/0001-playbook-winchester-7.3.3.patch`, **67 files /
  9 136 lines**, sha256 `d19adff8…`, verified 0 rejects / 67/67 byte-identical
  against pristine 7.3.3 targets (session 67 export).  Session 67 restores the
  **stock `omap_musb_set_mailbox()` call** in `drivers/usb/musb/omap2430.c`
  (the winchester early return is gone; the `set_mailbox_lock` mutex stays) and
  adds `drivers/usb/phy/phy-twl6030-usb.c` + `drivers/phy/ti/phy-omap-usb2.c`
  for the TWL6030 comparator lifetime (a failed probe used to free the devm
  `twl` and leave `phy->comparator` dangling — report 265); the board DTS is
  `panic=0` for the bring-up.  Session 65 added the device-mode-only enable
  path, the DSI-init TCON hook in `dsi.c`, and the board DTS without `maxcpus`;
  the session-62/63/64 markers, the `head.S` `CRR` stamp and the `MINI4x6`
  font link are deliberately **not** in it.  It includes the
  SGX integration (`drivers/gpu/drm/{Kconfig,Makefile}`, `mm/vmalloc.c`, the DTS, the config),
  the session-56 Munin Y fix, the session-57 touch input model and audio
  print levels, and the session-58 position-ghost fix (`playbook-munin.c` +
  `irq-gpios` in the board DTS); the **362-file SGX driver itself is vendored**
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
* Config: `kernel/winchester-7.3.3.config` (the canonical copy).  Session 65
  aligns it with the bench: `CONFIG_SMP=y`, `CONFIG_NR_CPUS=2` and **no
  `ARCH_MULTI_V6`** (so the build is `-march=armv7-a`; module vermagic `ARMv7`).
  Session 55 adds the SGX block (`CONFIG_SGX=y`, `CONFIG_PVRSGX_1_17_4948957=y`
  + `_ABI`, `CONFIG_SGX_DRM=y`, `CONFIG_SGX_OMAP=m`, the HW-perf/recovery
  defaults) and the FB helpers SGX_DRM selects (`FB_CFB_FILLRECT/COPYAREA/
  IMAGEBLIT`, `FB_IOMEM_FOPS/HELPERS`); session 54 added
  `CONFIG_DYNAMIC_DEBUG=y` and `CONFIG_OVERLAY_FS=y`.
* `initramfs/init.c`: `ROOTFS_RAW_OFF = 0x49000000` (hd3 at loader block
  `0x4915`), the `lstat()` hand-off for the absolute `/sbin/init` symlink, and
  a **read-write** root: the loop/queue are set to `write through`, ext4 is
  mounted `rw` with `barrier=0`, a one-shot `resize2fs` grows it (`/etc/pb-grown`
  marks it), and the first boot installs the dropbear/SSH kit and the
  `/etc/local.d` scripts.  The overlay-root variant is historical (session 55).
  The current region-0 image is `boot/chainloader/kernel.img` (`0x3547D613`,
  10,448,868 B, kernel `7.3.3 #109 SMP`, `-march=armv7-a`, no `maxcpus`, the
  **stock MUSB mailbox** + comparator lifetime fix, `panic=0`, the DSI-init
  TCON hook).  The previous `0xC9FEE2D1` (`#105`, device-mode-only workaround,
  `panic=10`) is kept as a known-good fallback on the host.
* Session 54's instrumentation change: the general MMC/GPIO/sysc/probe prints
  are `pr_debug` (`PBSIO`/`PBINI`/`PBX`/`PBM` in `omap_hsmmc.c`, `PBC` in
  `mmc/core/{core,host,mmc_ops}.c`, `PBR` in `drivers/base/dd.c`, `PBG` in
  `gpio-omap.c`, `PBS` in `ti-sysc.c`, `PB:` in `arch/arm/mm/mmu.c`), with
  `PBREQ`/`PBIRQ` behind the `pb_trace` module parameter.
* Session 56 adds the Munin Y fix: the controller's Y axis is inverted
  relative to the display (physical top → evdev 577–591), so
  `playbook-munin.c` reports `panel_h - 1 - munin_map(...)`.
* Session 57 adds the touch input model to `playbook-munin.c`: the parity
  re-read (up to 4 tries, `usleep_range(200,400)`), the 10 ms poll-on-touch
  watchdog, the per-contact `trace` module parameter, and the mutex that
  serialises the IRQ thread and the poll work; it also puts
  `omap_mcbsp_dump_reg()` and the machine driver's per-stream prints back at
  `dev_dbg` and guards the PM-QoS remove (`omap-mcbsp.c`,
  `playbook-wm8994.c`).
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
