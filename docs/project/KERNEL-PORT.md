# Kernel base — 7.3.3 (port complete)

The port is **done**: `local/kernel-tree` is upstream **Linux 7.3.3**, the unit
boots it to the region-1 BusyBox userland with a clean panel and working eMMC,
and the exported patch reproduces the tree from pristine 7.3.3.

* Base: **upstream 7.3.3** (not the pmOS package, which is still `!armhf`).
* Patch: `kernel/patches/0001-playbook-winchester-7.3.3.patch`, **55 files /
  8 500 lines**, sha256 `f5856a8b…`, verified 0 rejects / 55/55
  byte-identical against pristine 7.3.3 targets (session 48 wrap-up).  It
  carries the E5 clock roles (`CBC_CFC`, CPU `CLKSM = 1`/`CLKGDV = 32`, FLL1
  from BCLK, `mcbsp2_pins` `clkx`/`fsx` as outputs), the working audio card
  (`mcbsp3_pins` on the PDM pads, `&mcbsp1` disabled, the second
  `40126000.mcbsp + wm8994-aif2` link and the `omap-mcbsp` `XPHASE`
  channel-layout guard with its `omap-mcbsp-priv.h` fields), the speaker path
  (jack-dependent output stage, the line-out route, `spk-gpios`/`boost-gpios`,
  the `disp-en-12-hog` removed), the `&l4_abe` ABE-refclk assignment and the
  codec-GPIO init (reports 206/209/211/212/214/215).  Session 48 removed the
  two session-45 diagnostics and added `sound/soc/ti/omap-mcbsp-priv.h`, which
  every earlier export had omitted (trap 211).  Trap 161.
* Config: `kernel/winchester-7.3.3.config` (the canonical copy).
* 6.15.11 reference: `local/pristine/linux-6.15.11.tar.xz`
  `local/kernel-tree-6.15.11`, and
  `kernel/patches/0001-playbook-winchester-6.15.11.patch` (incomplete by
  history; reference only).  The tarball is authoritative; extract it with
  `tools/get_pristine.sh 6.15.11`.
* The full target analysis, per-file risk and checkpoint log are archived in
  [`docs/history/KERNEL-PORT.md`](../history/KERNEL-PORT.md).

Build and flash loop: `docs/project/DEVELOPMENT.md` §3.
