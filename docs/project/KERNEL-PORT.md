# Kernel base — 7.3.3 (port complete)

The port is **done**: `local/kernel-tree` is upstream **Linux 7.3.3**, the unit
boots it to the region-1 BusyBox userland with a clean panel and working eMMC,
and the exported patch reproduces the tree from pristine 7.3.3.

* Base: **upstream 7.3.3** (not the pmOS package, which is still `!armhf`).
* Patch: `kernel/patches/0001-playbook-winchester-7.3.3.patch`, **63 files /
  8 998 lines**, sha256 `48c14a41…`, verified 0 rejects / 0 mismatches against
  pristine 7.3.3 targets (session 54).  Session 54 makes the general MMC/GPIO/
  syscatter/probe instrumentation `pr_debug` instead of unconditional
  `pr_info` (`PBSIO`/`PBINI`/`PBX`/`PBM` in `omap_hsmmc.c`, `PBC` in
  `mmc/core/{core,host,mmc_ops}.c`, `PBR` in `drivers/base/dd.c`, `PBG` in
  `gpio-omap.c`, `PBS` in `ti-sysc.c`, `PB:` in `arch/arm/mm/mmu.c`), with
  `PBREQ`/`PBIRQ` still behind the `pb_trace` module parameter; it enables
  `CONFIG_DYNAMIC_DEBUG` (the runtime flag) and `CONFIG_OVERLAY_FS` (the
  read-only hd3 root).  The rebuilt image is `0x967A94` B, CRC `0xE84693A1`
  (built, not flashed).  Session 52 added
  `drivers/usb/musb/musb_gadget.c` (`quirk_avoids_skb_reserve`, so u_ether's RX
  buffers stay 4-byte aligned and MUSB's HSDMA is actually used) and
  `drivers/usb/gadget/function/u_ether.c` (the TX realign for the same reason),
  and the board DTS `uart2_pins` gains the **CTS/RTS** pair
  (`0x118`/`0x11A`) without which the BT core never answers.  It carries the E5 clock roles (`CBC_CFC`, CPU `CLKSM = 1`/`CLKGDV = 32`, FLL1
  from BCLK, `mcbsp2_pins` `clkx`/`fsx` as outputs), the working audio card
  (`mcbsp3_pins` on the PDM pads, `&mcbsp1` disabled, the second
  `40126000.mcbsp + wm8994-aif2` link and the `omap-mcbsp` `XPHASE`
  channel-layout guard with its `omap-mcbsp-priv.h` fields), the speaker path
  (jack-dependent output stage, the line-out route, `spk-gpios`/`boost-gpios`,
  the `disp-en-12-hog` removed), the `&l4_abe` ABE-refclk assignment and the
  codec-GPIO init (reports 206/209/211/212/214/215).  Session 48 removed the
  two session-45 diagnostics and added `sound/soc/ti/omap-mcbsp-priv.h`, which
  every earlier export had omitted (trap 211).  Session 50 added the WL1283
  chain: `drivers/clk/clk-twl.c` (`CLK32KG`) and the board DTS (the
  `ti,wl1283` child, `cap-power-off-card`, the WL_EN pwrseq).  Session 51 adds
  `drivers/net/wireless/ti/wl12xx/{main.c,wl12xx.h}`: the CAM default
  (`sta_sleep_auth`) and the 4-sr firmware choice with the SR floor at 7.3.5.0
  (reports 232/235, traps 239/243).  Trap 161.
* Config: `kernel/winchester-7.3.3.config` (the canonical copy; session 54 adds
  `CONFIG_DYNAMIC_DEBUG=y` and `CONFIG_OVERLAY_FS=y` with their resolved deps).
* `initramfs/init.c`: `ROOTFS_RAW_OFF = 0x49000000` (hd3 at loader block
  `0x4915`), the `lstat()` hand-off, and the session-54 read-only root: the
  loop `LO_FLAGS_READ_ONLY`, the ext4 `MS_RDONLY`+`noload`, a 256 MiB tmpfs
  upper/work and overlayfs on `/newroot`.  The image is
  `local/images/kernel-0xE85217E5-overlay.img` (`0x9662A4` B, CRC
  `0xE85217E5`, sha256 `fbeb6df0…`), built and not flashed.
* 6.15.11 reference: `local/pristine/linux-6.15.11.tar.xz`
  `local/kernel-tree-6.15.11`, and
  `kernel/patches/0001-playbook-winchester-6.15.11.patch` (incomplete by
  history; reference only).  The tarball is authoritative; extract it with
  `tools/get_pristine.sh 6.15.11`.
* The full target analysis, per-file risk and checkpoint log are archived in
  [`docs/history/KERNEL-PORT.md`](../history/KERNEL-PORT.md).

Build and flash loop: `docs/project/DEVELOPMENT.md` §3.
