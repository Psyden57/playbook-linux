# Kernel base — 7.3.3 (port complete)

The port is **done**: `local/kernel-tree` is upstream **Linux 7.3.3**, the unit
boots it to the region-1 BusyBox userland with a clean panel and working eMMC,
and the exported patch reproduces the tree from pristine 7.3.3.

* Base: **upstream 7.3.3** (not the pmOS package, which is still `!armhf`).
* Patch: `kernel/patches/0001-playbook-winchester-7.3.3.patch`, **54 files /
  8 214 lines**, sha256 `90391b96…`, verified 0 rejects / 54/54
  byte-identical against pristine 7.3.3 targets (session 44 wrap-up).  It
  carries the E1 clock roles, the `&l4_abe` ABE-refclk assignment and the
  codec-GPIO init (reports 206/209/211).  Trap 161.
* Config: `kernel/winchester-7.3.3.config` (the canonical copy).
* 6.15.11 reference: `local/pristine/linux-6.15.11.tar.xz`
  `local/kernel-tree-6.15.11`, and
  `kernel/patches/0001-playbook-winchester-6.15.11.patch` (incomplete by
  history; reference only).  The tarball is authoritative; extract it with
  `tools/get_pristine.sh 6.15.11`.
* The full target analysis, per-file risk and checkpoint log are archived in
  [`docs/history/KERNEL-PORT.md`](../history/KERNEL-PORT.md).

Build and flash loop: `docs/project/DEVELOPMENT.md` §3.
