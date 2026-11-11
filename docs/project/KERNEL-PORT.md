# Kernel base — 7.3.3 (port complete)

The port is **done**: `local/kernel-tree` is upstream **Linux 7.3.3**, the unit
boots it to the region-1 BusyBox userland with a clean panel and working eMMC,
and the exported patch reproduces the tree from pristine 7.3.3.

* Base: **upstream 7.3.3** (not the pmOS package, which is still `!armhf`).
* Patch: `kernel/patches/0001-playbook-winchester-7.3.3.patch`, **53 files /
  7,773 lines**, sha256 `434d06c3…`, verified 0 rejects / 53/53 identical —
  **this is the session-39 export and is now STALE.**  Session 41 changed
  `arch/arm/boot/dts/ti/omap/omap4-winchester.dts`,
  `sound/soc/ti/playbook-wm8994.c` and `sound/soc/ti/omap-mcbsp.c` in
  `local/kernel-tree` without re-exporting, so the file count and line count no
  longer describe the tree.  Regenerate it (tree walk in
  `kernel/patches/README.md`, **`make clean` first** — trap 139) before
  publishing or hand-on.  Trap 161.
* Config: `kernel/winchester-7.3.3.config` (the canonical copy).
* 6.15.11 reference: `local/pristine/linux-6.15.11.tar.xz`
  `local/kernel-tree-6.15.11`, and
  `kernel/patches/0001-playbook-winchester-6.15.11.patch` (incomplete by
  history; reference only).  The tarball is authoritative; extract it with
  `tools/get_pristine.sh 6.15.11`.
* The full target analysis, per-file risk and checkpoint log are archived in
  [`docs/history/KERNEL-PORT.md`](../history/KERNEL-PORT.md).

Build and flash loop: `docs/project/DEVELOPMENT.md` §3.
