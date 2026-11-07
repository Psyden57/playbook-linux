# Kernel patches — mainline 6.15.11 → PlayBook "winchester"

This directory contains the complete diff between **mainline Linux
6.15.11** and the kernel tree the project actually builds and boots
(`arch/arm` omap2plus, non-LPAE). Apply with:

```bash
git checkout v6.15.11            # or unpack pristine 6.15.11
git apply --check 0001-playbook-winchester-6.15.11.patch   # dry run
git apply 0001-playbook-winchester-6.15.11.patch
cp winchester.config .config     # the exact config the project boots
make ARCH=arm CROSS_COMPILE=<arm-linux-> -j$(nproc) zImage dtbs
# then wrap it: python3 tools/build_kernel_image.py
```

`winchester.config` is the shipped config (note `CONFIG_IKCONFIG=y`: the
config is also baked into every built kernel and recoverable via
`scripts/extract-ikconfig`).

## What the patch contains, file by file

### Load-bearing (removing these breaks the boot)

| File | Purpose |
|------|---------|
| `arch/arm/kernel/head.S` (+247) | The PlayBook prelude: breadcrumb ladder (pbmark/pbmark3/pbmarkv), DEBUG_LL smoke test, **r1/r2 save/restore around the UART diagnostics**, early ring/PL310/UART section maps, the ring-map `lsl #12` fix (the M=1 unlock), TTBR0 walk-attribute strip (`bic r4, r4, #0x6A` — standing, necessity untested, see KNOWN_ISSUES) |
| `arch/arm/include/debug/omap4bc.S` (new, +116) | DEBUG_LL: UART3 @0xFED20000 **+ DRAM console ring capture** (VA 0xD4000080 when MMU-on / raw PA when off; MMU state captured once at busyuart entry) |
| `arch/arm/mach-omap2/omap4-common.c` | `omap4_l2c310_write_sec` — routes L2X0 CTRL/AUX/PREFETCH writes through the secure monitor SMCs (0x102/0x109/0x113); latency writes hit the default WARN+skip |
| `arch/arm/mm/dma-mapping.c` | `PB-CMA` diagnostic print in `dma_contiguous_remap` (base/size/va/`__pv_offset` — the discriminator for the pv-stub defect) |
| `arch/arm/mm/init.c` | PB-MEM/PB-ADJ prints + the memblock limit fix (`adjust_lowmem_bounds` computed 0 on this layout; forced to `memblock_end_of_DRAM`) |
| `arch/arm/mm/mmu.c` | `adjust_lowmem_bounds` instrumentation (vmalloc_limit/lowmem_limit prints) |
| `arch/arm/Kconfig.debug` (+12) | `DEBUG_PLAYBOOK_BC` choice → `DEBUG_LL_INCLUDE="debug/omap4bc.S"` |
| `arch/arm/boot/dts/ti/omap/Makefile` + `omap4-winchester.dts` (new) | The device tree: memory bank, pl310 node, reserved-memory bc/ring pages, model string |
| `arch/arm/kernel/early_printk.c` | early console plumbing compatible with omap4bc |

### Diagnostic instrumentation (as-shipped; safe to strip, but they are in every log)

| File | Purpose |
|------|---------|
| `init/main.c` (+183) | bc probes through `parse_early_param` (cmdline head/len), `setup_arch`, and the initcall levels — the session-5/6-era bisect scaffolding |
| `arch/arm/kernel/setup.c` (+49) | PB-ADJ/PB-MEM-era setup probes |
| `arch/arm/kernel/head-common.S` | bc probes in `__mmap_switched` (bss bounds) |
| `kernel/cgroup/cgroup.c`, `kernel/taskstats.c`, `mm/slab_common.c` | The 171-wall bisect markers (fine-grained breadcrumbs inside cgroup_init_subsys / taskstats_init_early / kmem_cache_create) |
| `arch/arm/mm/mmu.c`, `arch/arm/mm/init.c` | (also carry the instrumentation noted above) |

No functional deletions: the only removed lines are a `mov r3, r3` NOP in
head.S, the two `omap4-sdp*.dtb` list entries in the dts Makefile, and
guard lines in `parse_early_param`/initcall loops that were rewritten to
carry probes.

## Caveats

- **Re-exported in session 20** against the real pristine tree, taken from
  **this repository's own payload** —
  `local/pristine/linux-6.15.11.tar.xz` (sha256 `89ab469f…65d93`), extracted
  with `tools/get_pristine.sh`.  The project must not depend on a pristine copy
  outside the workspace; the old recipe's
  `/home/psyden/SSDdrive/…/wsl-migration/home/psyden/kernel/pristine` is not
  used any more, and `/home/psyden/rea-work/playbook-dev/kernel-clean` was
  never pristine (it is a copy of our own tree from session 5, which is why
  that recipe produced a patch three sessions out of date).
  The right side of the diff is `local/kernel-tree`, our **current modified
  tree**, the one the zImage is built from.
  The file is **5 703 lines / 34 files** at the session-20 export; it was
  verified then by extracting the tarball fresh, applying the patch
  (`patch -p1 --forward`, rc=0, no rejects) and then comparing the result
  against `local/kernel-tree` — **all 34 files compare byte-identical**.  Sessions 18–20 added the `pb_probe` gating, the `pb_reg`
  driver group and the DSS clock fix (session 18), `drivers/misc/pbmon.c` +
  `pbmon-pair.h` and the `drivers/misc/Makefile` line (session 19), and the
  identity-page/`CM_MPU_STATICDEP` changes plus three `no-map` DTS oracle pages
  (session 20).  Session 17's delta over session 15's is `omap_hsmmc.c`:
  `PBREQ`/`PBIRQ` moved behind the `trace` module parameter (default off) and
  `PBREQ` now prints `R/W blksz blocks bytes sg_len dt` (report 136).  Session
  15's delta over session 14's is `dsi.c`,
  `tc358764.c` and `dss.c`: `PPI_TX_RX_TA = 0x00040006`, the corrected
  `VP_HTIM1/2` porches (`0x00780001`/`0x00760400`), failure-counted boot reads,
  the `t`/`u`/`d`/`v` console ops, and **the board-gated PER-PLL rate request in
  `dss_probe()` that fixed the display** (report 133).  `winchester.config` also
  gains `CONFIG_OMAP2_DSS_DEBUGFS=y`.
- The previous snapshot (session 14) was 4 003 lines / 31 files.
- The previous snapshot (session 5, 2 922 lines / 29 files) was stale in two
  important ways: it contained **no `pb_bridge_program` at all**, i.e. none of
  the session-13/14 display work (the TC358764 sequence, the DSI VC0/HS
  null-packet clock start, the forced DSI timing words, the probe and the
  `pb_reg` hook); and it **did not create `include/linux/pb_event.h`** although
  eleven places in its own hunks include that header, so it could not build.
  The new delta adds `include/linux/pb_event.h` and
  `drivers/gpu/drm/omapdrm/dss/pll.c`.
- The pristine tree is taken from the repository payload,
  `local/pristine/linux-6.15.11.tar.xz` (sha256 `89ab469f…65d93`), extracted with
  `tools/get_pristine.sh`.  Nothing outside the workspace is required.
  `…/kernel/linux` (the wsl-migration area) is the **modified** kernel tree from
  the earlier kexec project — neither pristine nor the project tree; do not diff
  against it.
- `head.S`/`omap4bc.S` are heavily comment-marked `PlayBook` — grep that
  for the full change inventory.
- The bc/ring section VAs (0xC88/0xD00/0xD40) are load-bearing *and*
  deliberately identical to the linear-map VAs those pages later get, so
  `paging_init`'s re-map is seamless.
- Regenerating the diff: `diff -ruN` of the two trees restricted to tracked
  source files (build artifacts excluded). The **exact** recipe, including the
  traps (`diff -rq` follows the `scripts/dtc/include-prefixes` symlinks so
  `arch/arm/boot/dts/**` shows up three times; `-x` matches basenames only; new
  files must be added by hand because they appear as "Only in"), is in
  `bootdumps-2026-09-11/analysis/NEXT-SESSION-BOOTSTRAP-2026-10-26.md`
  §5, "Source patch".

  **Use these two paths** (`C` must be the live build tree, not a stale copy —
  the session-5 recipe's `C` was a copy of our own tree and produced a patch
  three sessions out of date; `K` must come from this repository's payload, not
  from a copy outside the workspace):

  ```bash
  tools/get_pristine.sh                    # -> /tmp/linux-6.15.11 (verifies sha256)
  K=/tmp/linux-6.15.11                     # UNMODIFIED 6.15.11
  C=$PWD/local/kernel-tree                 # our build tree
  ```

  `/tmp` is a tmpfs on the bench host (7.8 GB), so the ~1.3 GB extraction costs
  RAM unless you pass a disk path:
  `tools/get_pristine.sh local/pristine/linux-6.15.11` (that location is
  gitignored).  Delete the extraction when finished.

  Then verify by applying the result to a fresh copy of `$K` and comparing it
  against `$C` file by file; a clean `patch -p1 --dry-run` alone does not prove
  the patch reproduces the tree.

## Added after the session-7 snapshot (2026-10-27)

Beyond the instrumentation listed above, the delta now carries everything from
sessions 3–5, and these are the ones that actually matter for a rebuild:

| Area | Files |
|---|---|
| **Display bring-up** | `arch/arm/boot/dts/ti/omap/omap4-winchester.dts` (`&dss`/`&dsi1` + TC358764 bridge + `panel-lvds` + the measured 1024×600@60 timing, `&vaux1/2/3` at 2800 mV via fixed `min == max`, board device enables/disables), `drivers/gpu/drm/bridge/tc358764.c` (optional `reset-gpios`; `lanes`/`hs-rate`/`lp-rate` from DT — it used to hardcode 4 lanes and leave `hs_rate = 0`, which makes the OMAP host reject every mode) |
| **WDT2 / ti-sysc** | `arch/arm/boot/dts/ti/omap/omap4-l4.dtsi` (WDT2 `ti,no-idle; ti,no-reset-on-init`, `mmc2_target` label), `drivers/bus/ti-sysc.c`, `arch/arm/mach-omap2/*` |
| **eMMC** | `drivers/mmc/host/omap_hsmmc.c`, `drivers/mmc/core/{core,host,mmc,mmc_ops}.c` (`PBOCR:`/`PBVOLT:` voltage logging + the rescan/attach events) |
| **Event log / capture** | `drivers/irqchip/irq-gic.c` (event log, heartbeat, WDT2 pet from a 2 s `timer_list`, MPU sampler), `init/main.c`, `init/do_mounts.c`, `kernel/async.c`, `drivers/base/{dd.c,power/runtime.c}`, `drivers/mfd/twl-core.c`, `drivers/gpio/gpio-omap.c` |
| **DT labels** | `arch/arm/boot/dts/ti/omap/omap4.dtsi` — `emif1_target`, `emif2_target`, `dss_target` labels only (no behaviour change) |

The board DTS disables 19 nodes on purpose (crypto engines behind the L3
firewall, ABE, MCSPI, unused MMC instances, i2c4); each carries a comment saying
what it cost to learn. See the board file itself and report 118.


## Session 19 addition — `drivers/misc/pbmon.c`

`pbmon` is a built-in misc driver that calls the OMAP4430 secure monitor's RPMB
block seal/unseal services (`0x33` key load + `0x2F`, op 0, dir 1/0), with the
six CM2 L4SEC crypto clocks enabled around the call and one coherent page for
the descriptor/command/data buffers.  It exposes `/dev/pbseal` and
`/dev/pbunseal` (224-byte in, 224-byte out) and an **off-by-default**
`pbmon.selftest` that compares the seal of block 0x43 with the media bytes.

The call is a faithful transcription of `rpmb-omap4`.  Session 20 pinned the
address convention (identity physical addresses, every monitor page below
`0xB0000000`; report 147) and the ABI is accepted end to end, but the transform
does not yet reproduce the device's block-0x43 pair (report 148).  Do not enable
`pbmon.selftest` at probe and do not issue an RPMB write until that is closed.
`pbmon-pair.h` carries the known block-0x43 pair.

## Regenerating this patch (do it whenever `local/kernel-tree` changes)

There is no generator script; the patch is a **per-file** diff against the
pristine tree, using the file list the patch already carries.  That keeps build
output (`*.o`, `.config`, `usr/initramfs_data.cpio*`) out of it:

```bash
cd /home/psyden/SSDdrive/playbook-linux
python3 - <<'EOF'
import subprocess, os
old = 'kernel/patches/0001-playbook-winchester-6.15.11.patch'
pris, tree = 'local/pristine/linux-6.15.11', 'local/kernel-tree'
files = [l[6:].strip() for l in open(old) if l.startswith('+++ b/')]
out = []
for f in files:
    a, b = os.path.join(pris, f), os.path.join(tree, f)
    if not os.path.exists(b):
        continue
    r = subprocess.run(['diff', '-u', '-N', '--label', 'a/' + f, '--label', 'b/' + f,
                        a if os.path.exists(a) else '/dev/null', b],
                       capture_output=True, text=True)
    out.append(r.stdout)                     # returncode 1 = differences, 0 = none
open('/tmp/new.patch', 'w').write(''.join(out))
EOF

# verify it applies to pristine (copy just the targets, dry-run)
rm -rf /tmp/pcheck && mkdir -p /tmp/pcheck
python3 - <<'EOF'
import os, shutil
for f in [l[6:].strip() for l in open('/tmp/new.patch') if l.startswith('+++ b/')]:
    src = os.path.join('local/pristine/linux-6.15.11', f)
    if os.path.exists(src):
        dst = os.path.join('/tmp/pcheck', f)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
EOF
(cd /tmp/pcheck && patch -p1 --dry-run < /tmp/new.patch)    # every file "checking ...", no rejects
cp /tmp/new.patch kernel/patches/0001-playbook-winchester-6.15.11.patch
wc -l kernel/patches/0001-playbook-winchester-6.15.11.patch   # update this README and STATE.md
```

Two gotchas that cost time in session 20: the `+++ b/` lines carry the trailing
newline (`l[6:].strip()`), and `diff -N` needs `/dev/null` for a file that exists
only in the kernel tree (new drivers), which is what produces the
`@@ -0,0 +1,N @@` creation hunks `git apply` expects.
