# Kernel patches — PlayBook "winchester" (7.3.3 current; 6.15.11 reference)

## Current base: 7.3.3 (session 30)

`0001-playbook-winchester-7.3.3.patch` is the patch the project builds today:
**9 136 lines / 67 files** against pristine **linux-7.3.3**
(sha256 `d19adff8…`, session-67 export).  It was verified by applying it to
pristine 7.3.3 targets and comparing all 67 files byte-for-byte with the
intended contents — 0 rejects, 67/67 identical.  **Session 67** re-exports it:
`drivers/usb/musb/omap2430.c` now keeps the `set_mailbox_lock` mutex **and runs
the stock `omap_musb_set_mailbox()` call** (the winchester device-mode-only
early return is gone), and two files are added — `drivers/usb/phy/phy-twl6030-usb.c`
(the TWL6030 comparator is registered only after the probe can still fail; a
devm action clears it on probe failure/remove, and a deferred `vusb` regulator
now propagates `-EPROBE_DEFER` instead of failing the probe) and
`drivers/phy/ti/phy-omap-usb2.c` (`omap_usb2_set_comparator()` drops its
`usb_get_phy()` reference).  That is the report-264 fault: a failed
`twl6030_usb_probe()` freed the devm `twl` and left `phy->comparator` dangling
(report 265).  The board DTS also moves to `panic=0` for the bring-up (trap 335;
restore `panic=10` once deliberate-fault images are done).  It keeps the
DSI-init TCON hook in `drivers/gpu/drm/omapdrm/dss/dsi.c` and the board-DTS
`maxcpus=1` removal and inert `console=ttyGS0,115200`.  **Session 65** exported
the previous 65-file version after the dual-core work.  The **session-62/63/64 diagnostics
are not in the patch**: the `PBDBG` markers in `init/main.c`, the
`PBCPU`/`PBIPI1`/`PBSMF` markers in `arch/arm/kernel/smp.c`,
`arch/arm/mach-omap2/omap-smp.c` and `kernel/smp.c`, the `head.S` `CRR` stamp,
the `MINI4x6` font link, and the reverted `otg.h` text guard.  **Session 58**
fixes the Munin touch position ghost: `playbook-munin.c` keeps the 10 ms poll armed while
the controller asserts its IRQ line (and for a 200 ms grace after the last
contact), and holds a new down back one sample when it repeats the slot's
previous coordinates within 300 ms; the board DTS gains `irq-gpios` on the
touch node (report 248, traps 296/298/299).  **Session 57** changed the same
driver (the parity re-read, the 10 ms poll-on-touch watchdog, the mutex and the
`trace` parameter — traps 296/297, report 247) and the audio print levels
(`omap-mcbsp.c` back to `dev_dbg`, the machine driver's per-stream prints to
`dev_dbg`, the PM-QoS remove guarded — traps 293/294).  The walk excludes the
**vendored SGX driver** (`drivers/gpu/drm/pvrsgx/`, 362 files) and `scripts/`
(host tooling); without those exclusions the same walk reports 337 files,
including `drivers/gpu/drm/pvrsgx/` (271 files) and two generated
`scripts/mod/*.h`.  Session 52 adds two files,
both in the USB gadget path: `drivers/usb/musb/musb_gadget.c` sets
`musb->g.quirk_avoids_skb_reserve = 1` and
`drivers/usb/gadget/function/u_ether.c` realigns misaligned TX frames — MUSB's
HSDMA (RTL 2.00) refuses buffers that are not 4-byte aligned and silently fell
back to PIO, which capped the RNDIS link at 3.3 MB/s; with the fix it is
RX 6.3 / TX 4.1 MB/s (report 236 §7, trap 246).  The 40 MHz experiment in
`wl12xx/main.c` was reverted and is **not** in the patch (the module is again
byte-identical to the baseline, trap 247).  Session 51 adds two files, both
`drivers/net/wireless/ti/wl12xx/`: `main.c` with
`wl12xx_default_conf.conn.sta_sleep_auth = WL1271_PSM_CAM` (was
`WL1271_PSM_ILLEGAL`, which selects ELP — the WL1283's `-110` recovery loop was
the chip's ELP sleep, report 232/trap 239) **and**
`WL128X_FW_NAME_SINGLE = "ti-connectivity/wl128x-fw-4-sr.bin"`, and `wl12xx.h`
with the SR firmware floor lowered to `7.3.5.0` (`WL128X_MAJOR_SR_VER 5`,
`WL128X_MINOR_SR_VER 0`) because the newer **5-sr** service pack boots but
**panics on the first scan**, while **4-sr** scans (report 235, trap 243).  The
diagnostic `dev_err` used to find the failing transfer was in `wlcore/sdio.c`
and is **not** in the patch: that file is `cmp`-identical to pristine.
Session 50 adds
`drivers/clk/clk-twl.c` (the CLK32KG enable) and the board-DTS WL1283 changes
(the `ti,wl1283` child, `cap-power-off-card`, the WL_EN pwrseq).
**Session 48 retires the session-45 diagnostics** (the machine driver's `pcm:`
DMA-ring readback and `omap-mcbsp`'s eight-sample `live N:` dump, with the
`steady_dump` work item and the `substream` member they needed), and **adds the
one file every export since session 41 had silently omitted**:
`sound/soc/ti/omap-mcbsp-priv.h`.  That omission was a real defect — the patch
added `active_rate`/`active_width` *uses* in `omap-mcbsp.c` while leaving the
fields undeclared, so a build from this patch alone could not compile.  Found
by a full tree walk (the 89 other diffs it reports are the documented build
artifacts, trap 139).  Session 48 is module-only, so the region-0 image and its
CRC `0xAD85DF0F` are unchanged; region 1 was rebuilt and flashed.
**Session 47** adds the speaker path: the repair follows the jack and uses the
codec's **line outputs** for the speakers (PM3 `0x3cf0`, `R0x01e`, `R0x034/035`,
`R0x038`, `R0x039`, charge pump off), the `spk` line (GPIO2_12, active low) and
the GPIO1_15 `audio_boost` enable are owned by the machine driver
(`spk-gpios`/`boost-gpios`), and the `disp-en-12-hog` is removed from the board
DTS.  Report 215; both speakers play.
**Session 46** adds the working audio path: the WM8994's AIF1 is on **McBSP3**
(the oracle's routing log `AIF 0 Downlink: …->MCBSP3` and the live McBSP3
registers), so the card's second link is `40126000.mcbsp` + `wm8994-aif2`
with the `mcbsp3_pins` group (the PDM pads in mode 1, the oracle's live
values), `&mcbsp1` is disabled (its DX is the other route), and
`omap-mcbsp`'s `hw_params` guard also compares the channel layout
(`XPHASE`), so the OSS mono-first negotiation cannot leave the right slot
silent.  Report 214; the tone is audible in both ears at 48 kHz.
**Session 45** adds E5: the machine driver goes back to `SND_SOC_DAIFMT_CBC_CFC`
with the CPU DAI as bit/frame provider (`set_sysclk(CLKSM=1)` +
`set_clkdiv(CLKGDV, 49.152 MHz / BCLK)`, FLL1 referenced from BCLK,
`PB_MCBSP_FCLK_HZ = 49152000`), the board DTS `mcbsp2_pins` `clkx`/`fsx` are
outputs again, and two diagnostics were added (the machine driver's `pcm:`
DMA-ring readback and `omap-mcbsp`'s eight-sample `live N:` FIFO dump) — retired
in session 48.  Report 212 §10–11.  The DTS also carries the (disabled) `&mcbsp1` node and the `mcbsp1_pins` group — the codec's DATA ball is `abe_mcbsp1_dx` — as the groundwork for the two-engine design.
**Session 44 (later)** adds the reference codec-GPIO init: the boot-on
`audio-ldo` regulator on GPIO4_31, `ldo-supply`/`hp-gpios` on the sound node,
and `playbook_wm8994_board_enables()` in the machine driver (report 211).
**Session 44** adds the `&l4_abe` ABE-refclk assignment
(`assigned-clocks = <&abe_dpll_refclk_mux_ck>, <&dpll_abe_ck>`,
`assigned-clock-parents = <&sys_clkin_ck>`,
`assigned-clock-rates = <0>, <98304000>`), so `CM_ABE_PLL_REF_CLKSEL` is SYS_CLK
and DPLL_ABE locks at 98.304 MHz (report 209).
**Session 42** picks up the E1 clock-role changes: the board DTS makes
`clkx`/`fsx` inputs (codec AIF1 master, McBSP consumer) and the machine driver
uses `CBP_CFP` with FLL1 from MCLK1 and no CPU-side SRG calls.
**Session 41** adds
`sound/soc/ti/omap-mcbsp.c` (missing from the 53-file export: the rate/width
re-program fix, the `configured` guard, the info-level register dump and the
steady-state dump), the AUXCLK1 `assigned-clock-rates` in the board DTS and the
machine driver's deferred-repair chain (PM3, charge pump, DAC routing,
`regcache_mark_dirty`).  That session also changed
`sound/soc/ti/omap-mcbsp-priv.h` (the `active_rate`/`active_width` fields the
guard reads) **and did not add it to the patch**; session 48 found it with a
tree walk and the export is now 55 files.  **Session 39** adds the board
machine driver (`sound/soc/ti/playbook-wm8994.c` + Kconfig/Makefile), the
`wm8994-core.c` runtime-PM board quirk and the board `sound` node/codec clocks,
and it restores `drivers/input/touchscreen/{Kconfig,Makefile}`, which the
48-file export had silently omitted (trap 154).  Session 36 added the
`i2c2_pins` pinctrl group for the touch bus (report 193).  **Session 37 moved
the WL1283 (MMC5) instrumentation out** into
`kernel/archive/wl1283/wl1283-instrumentation-7.3.3.patch` (DECISIONS §46):
`drivers/mmc/core/sdio.c` and `include/linux/mmc/host.h` are no longer
modified (they are pristine again), and the WL1283 hunks in
`omap_hsmmc.c`/`core.c`/`mmc_ops.c` and the board DTS are gone.  The base is
now chainloader, display, sensors, charger, touch, WDT2, eMMC and the WM8994
machine driver.

```bash
# pristine 7.3.3, then:
patch -p1 < 0001-playbook-winchester-7.3.3.patch
cp winchester-7.3.3.config .config
make ARCH=arm CROSS_COMPILE=<arm-linux-> -j$(nproc) zImage dtbs modules
# then wrap it: python3 tools/build_kernel_image.py
```

The canonical config is `kernel/winchester-7.3.3.config` (sha256
`50d1f60a…`).  The stale copies that used to sit in this directory are archived
under `kernel/archive/configs/`; do not build with them.
The base had to be **upstream 7.3.3**, not the pmOS package: current pmaports
still declares `linux-postmarketos-stable` as `arch="all !armhf !s390x"`.
The kernel is **XZ-compressed** so the image fits the region-0 IFS slot
(`0xB00000`): zImage 6 568 696 B, image 0x91AB20, CRC `0x0F6AC47F`.

The charger preparation adds one modified file, `arch/arm/boot/dts/ti/omap/
twl6030.dtsi` (the `gpadc` label), plus the `charger` node and the `battery`
label in the board DTS.  Session 31 moved that label onto a separate
`compatible = "simple-battery"` profile node: `power_supply_get_battery_info()`
rejects the bq27541 gauge node with `-ENODEV`, so the first prepared image
probed the BCI and failed.  The profile has no properties on purpose — the
driver then falls back to the bootloader's `CHARGERUSB_CTRLLIMIT1/2`
(measured `0x21` → 4.16 V, `0x08` → 900 mA).  **The BCI turned out not to be
the board's charger** (report 183): session 31 adds the real one,
`drivers/power/supply/isl9519.c`, an Intersil ISL9519 at I²C1 0x09, and its
`charger@9` node in the board DTS.  The BCI node/modules stay but are inert.

The new files are the ones the old export recipe silently dropped, the
session-31 charger driver, the session-36 touch driver, and the session-39
machine driver:
`arch/arm/boot/dts/ti/omap/omap4-winchester.dts`,
`arch/arm/include/debug/omap4bc.S`, `drivers/iio/light/cm3217.c`,
`drivers/leds/leds-fan5702.c`, `drivers/misc/pbmon.c`, `pbmon-pair.h`,
`pbmon-page.h`, `include/linux/pb_event.h`,
`drivers/power/supply/isl9519.c`, `drivers/input/touchscreen/
playbook-munin.c` and `sound/soc/ti/playbook-wm8994.c`.  `patch -p1 --dry-run`
alone does not prove an export: it must be applied and the result compared to
the tree.  The touchscreen Kconfig/Makefile and the machine driver's
Kconfig/Makefile are modifications, not new files.

Session 32 added two modified files — `drivers/mmc/core/sdio.c` (the CMD5
probe-OCR quirk and `PBC: sdio` diagnostics) and `include/linux/mmc/host.h`
(`MMC_CAP2_SDIO_OCR_PROBE_FFFF00`) — and extends `drivers/mmc/host/
omap_hsmmc.c` (the quirk for `480d5000`), `arch/arm/mach-omap2/omap4-common.c`
(`playbook_fref_clk2_init`: SCRM `AUXCLK1` = `0x00020100`, 12.8 MHz) and the
board DTS (`fref_clk2_out_pins`, `vwl1283` 3.3 V).  The SDIO quirk and the
`PBC: sdio` prints are bring-up instrumentation.  **Session 33 removes the two
hardware experiments from the calls:** `pb_enable_fref_clk2()` and
`pb_wl1283_reset_pulse()` are both negative for CMD5 (report 185), and the
WL_EN reset pulse also broke USB warm-reset enumeration (trap 130).  The
functions remain in `omap_hsmmc.c` as `__maybe_unused` until the next cleanup;
the session-33 MMC test knobs (`post-power-on-delay-ms`, CMD52 skip) are not
in this patch.

**Session 37 archives the above (DECISIONS §46).**  The WL1283 SDIO quirk
(`sdio.c`, `host.h`), `playbook_fref_clk2_init` (not present in the current
`omap4-common.c` any more), `pb_enable_fref_clk2`, `pb_wl1283_reset_pulse`,
`pb_sdio_ocr_quirk`, `pb_init_hz`, the `PBCMD`/`PBCLK`/`PBD`/`PBOCR`/`PBVOLT`
prints, the `480d5000` PBGPIO dump and the DTS `vwl1283`/`fref*`/`sdmmc5_pins`/
`wl1283_ctrl_pins`/`wl1283-en-hog`/`&mmc5` nodes are in
`kernel/archive/wl1283/wl1283-instrumentation-7.3.3.patch`.  The board DTS now
sets `&mmc5 { status = "disabled"; }`.  The general eMMC bring-up prints
(`PBM`/`PBINI`/`PBSIO`/`PBX`/`PBREQ`/`PBIRQ` and `pb_event`) remain.

**Session 38 updates the touch only (report 198).**  `playbook-munin.c` gains
the 1917 `scaling.conf` coordinate map (`rim,x-calibration`/`rim,y-calibration`,
`div_s64`, clamped to `touchscreen-size-x/y`) and the threaded
`IRQ_TYPE_EDGE_FALLING` handler with the poll fallback; the board DTS gains
`touch_irq_pins` (`0x056` = gpio 35) and the `interrupts`/calibration
properties.  Session 38 also enables the ABE segment: `omap4-l4-abe.dtsi`
gains the `wdt3_target` label and the board DTS sets `&l4_abe { status =
"okay"; }` + `&wdt3_target { status = "disabled"; }` (report 199), and adds the
WM8994 codec + McBSP2 + `simple-audio-card` nodes plus the
`SND_SOC_PLAYBOOK_WM8994` board Kconfig.  The export is **7 530 lines / 48
files**, sha256 `9089ef49…`, verified 0 rejects / 48/48 byte-identical.

**Session 39 replaces the simple-audio-card link with a board machine driver
(report 200).**  `sound/soc/ti/playbook-wm8994.c` (new) builds the DAI link,
the BCLK-fed FLL1 clocking and the `Headphone`/`Speaker` DAPM routes;
`sound/soc/ti/{Kconfig,Makefile}` build it; `drivers/mfd/wm8994-core.c` gets a
board `pm_runtime_forbid` quirk; the board DTS gains the
`blackberry,playbook-wm8994` `sound` node, `codec_mclk_pins` and the codec
`clocks`/`clock-names`.  A clean tree walk also found that the 48-file export
had omitted `drivers/input/touchscreen/{Kconfig,Makefile}` (trap 154) and the
`drivers/mfd/wm8994-core.c` modification.  The export is **7 773 lines / 53
files**, sha256 `434d06c3…`, verified 0 rejects / 53/53 byte-identical.  Run
`make clean` in the tree before the walk (trap 139).  **Superseded by the
session-41 export above (54 files / 8 113 lines)** — kept because it records
which file the following session had to add (`omap-mcbsp.c`).

## Reference base: 6.15.11

This section is the **backup/reference** delta.  The project booted it until
session 30 and keeps it for a bisect back; do not delete it.

This directory contains the complete diff between **mainline Linux
6.15.11** and the kernel tree the project actually builds and boots
(`arch/arm` omap2plus, non-LPAE). Apply with:

```bash
git checkout v6.15.11            # or unpack pristine 6.15.11
git apply --check 0001-playbook-winchester-6.15.11.patch   # dry run
git apply 0001-playbook-winchester-6.15.11.patch
cp ../winchester.config .config  # canonical 6.15.11 config (kernel/winchester.config)
make ARCH=arm CROSS_COMPILE=<arm-linux-> -j$(nproc) zImage dtbs
# then wrap it: python3 tools/build_kernel_image.py
```

`kernel/winchester.config` is the shipped 6.15.11 config (note `CONFIG_IKCONFIG=y`: the
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
| `drivers/usb/musb/omap2430.c` (new, +23) | The winchester MUSB glue: `omap2430_musb_enable()` sets only `USB_MODE_DEVICE` and skips the bind-time `omap_musb_set_mailbox()` that panics on dual-core; a `set_mailbox_lock` mutex serializes the remaining callers (reports 262/263, DECISIONS §93/§95) |

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

## Regenerating the patch — derive the file list from the TREE, not the old patch

**This is the session-30 fix for trap 106.**  The old recipe (kept at the bottom
for history) took the file list from the previous patch's `+++ b/` lines, so
every file added after an export was silently omitted — that is how
`cm3217.c`, `leds-fan5702.c`, `pbmon-pair.h` and `pbmon-page.h` disappeared from
the 6.15.11 patch.  The list must come from a tree-vs-pristine walk.  **Run
`make clean` in `local/kernel-tree` first** (session 36 warning): on a built
tree the walk also returns generated sources (asn1 tables, the dtc/genksyms
lexers and parsers, the logo/ucs tables, `.tmp_vmlinux*.S`, …) — session 50 saw
90 entries instead of 56.

**Session-50 correction:** the filter must match `Kconfig.*`, not just the exact
name `Kconfig`.  `arch/arm/Kconfig.debug` is a real modified file (session 49
added it) and `b in ('Kconfig','Makefile')` silently drops it — the same
trap-211 class the walk exists to catch.  Use
`b == 'Makefile' or b.startswith('Kconfig') or rel.endswith(EXT)`.

```bash
cd /home/psyden/SSDdrive/playbook-linux
python3 - <<'EOF'
import os, subprocess
P='local/pristine/linux-7.3.3'; T='local/kernel-tree'
SKIP_DIRS=('include/generated','arch/arm/include/generated','usr','.tmp_versions','.git')
GEN={'drivers/tty/vt/defkeymap.c','drivers/tty/vt/consolemap_deftbl.c',
     'drivers/scsi/scsi_devinfo_tbl.c','drivers/memory/ti-emif-asm-offsets.h',
     'arch/arm/mach-omap2/pm-asm-offsets.h','lib/crypto/arm/sha256-core.S',
     'lib/crypto/arm/sha512-core.S','init/utsversion-tmp.h',
     'drivers/of/empty_root.dtb.S'}
NAME_SKIP={'.config','Module.symvers','System.map','vmlinux','vmlinux.o','.version'}
EXT=('.c','.h','.S','.dts','.dtsi','.yaml','.sh','.pl')
def wanted(rel):
    b=os.path.basename(rel)
    return (b == 'Makefile' or b.startswith('Kconfig') or rel.endswith(EXT)) and \
           b not in NAME_SKIP and rel not in GEN and not rel.endswith('.mod.c')
out=[]; n=0
for dp,dn,fn in os.walk(T):
    rd=os.path.relpath(dp,T)
    dn[:]=[d for d in dn if not any((os.path.join(rd,d) if rd!='.' else d)==s or
          (os.path.join(rd,d) if rd!='.' else d).startswith(s+'/') for s in SKIP_DIRS)]
    for f in fn:
        rel=os.path.normpath(os.path.join(rd,f)) if rd!='.' else f
        if not wanted(rel): continue
        a=os.path.join(P,rel); b=os.path.join(T,rel)
        if os.path.exists(a) and open(a,'rb').read()==open(b,'rb').read(): continue
        r=subprocess.run(['diff','-u','-N','--label','a/'+rel,'--label','b/'+rel,
                          a if os.path.exists(a) else '/dev/null', b],
                         capture_output=True, text=True)
        out.append(r.stdout); n+=1
open('/tmp/new.patch','w').write(''.join(out))
print('files', n)
EOF
```

Then **apply it to pristine targets and compare** — a `--dry-run` is not
enough:

```bash
rm -rf /tmp/pcheck && mkdir -p /tmp/pcheck
python3 - <<'EOF'
import os, shutil
P='local/pristine/linux-7.3.3'
for l in open('/tmp/new.patch'):
    if not l.startswith('+++ b/'): continue
    f=l[6:].strip(); src=os.path.join(P,f)
    if os.path.exists(src):
        dst=os.path.join('/tmp/pcheck',f); os.makedirs(os.path.dirname(dst),exist_ok=True)
        shutil.copy2(src,dst)
EOF
(cd /tmp/pcheck && patch -p1 --forward < /tmp/new.patch)   # 0 rejects
python3 - <<'EOF'
import os
for l in open('/tmp/new.patch'):
    if not l.startswith('+++ b/'): continue
    f=l[6:].strip()
    if open('local/kernel-tree/'+f,'rb').read()!=open('/tmp/pcheck/'+f,'rb').read():
        print('MISMATCH',f)
EOF
cp /tmp/new.patch kernel/patches/0001-playbook-winchester-7.3.3.patch
wc -l kernel/patches/0001-playbook-winchester-7.3.3.patch
```

**Session 48's export was 8 500 lines / 55 files** (sha256 `f5856a8b…`); the
**session-49** export is **8 583 lines / 56 files** (sha256 `455b45d8…`) and adds
the report-220 board-DTS GPIO1 pad fix plus `arch/arm/Kconfig.debug`, which the
tree walk showed had been missing from every export since session 30.  Session
48's export,
verified 0 rejects / 55/55 byte-identical.  It used the session-47 patch's
54-file list **plus `sound/soc/ti/omap-mcbsp-priv.h`**, which a full tree walk
identified as the only genuine source omission among 90 reported diffs (the
other 89 are the build artifacts listed in the warning below).  **The lesson
generalises trap 106:** driving an export from the previous patch's file list
cannot see a file the *earlier* export missed, so a walk is still needed to find
what has been missing all along — and the byte-compare cannot catch it either,
because a missing file is simply absent from both sides.  Check the build, or
walk.
**Session 42's export is 8 095 lines / 54 files** (sha256 `55035aac…`),
verified 0 rejects / 54/54 byte-identical; it was driven from the session-41
file list (no new sources — only `omap4-winchester.dts` and
`playbook-wm8994.c` changed).  **Session 41's export was 8 113 lines / 54
files** (sha256 `fc3b2e54…`); it was **not** produced with the
tree walk below: on a built tree that walk returned **90 files** because it
sweeps build artifacts (see the session-36 warning at the end).  Instead the
export was driven from the previous patch's 53-file list **plus**
`sound/soc/ti/omap-mcbsp.c`, which is the documented alternative when no other
new source exists — but the file list must still be checked file by file, since
that is exactly how `omap-mcbsp.c` came to be missing.

Session 32's export is **7 257 lines / 47 files** (sha256 `ad55511d…`), and
the apply-and-compare verification printed **0 rejects, 0 mismatches**.
**Session 37's export is 7 305 lines / 46 files** (sha256 `d0d00009…`), also
verified 0 rejects / 0 mismatches; it used the session-36 patch's file list as
the source set, dropped the two files that are now pristine (`sdio.c`,
`host.h`), and did not need a clean tree (nothing was added).
**Session 38's export is 7 530 lines / 48 files** (sha256 `9089ef49…`),
verified 0 rejects / 48/48 byte-identical; the touch driver + board DTS +
`omap4-l4-abe.dtsi` changed, and `sound/soc/ti/Kconfig` joined the set (the
`SND_SOC_PLAYBOOK_WM8994` select).
Update this README and `STATE.md` when a new export lands.

**Session 36 warning — the tree-vs-pristine walk above picks up BUILD
ARTIFACTS on a built tree.**  Run on `local/kernel-tree` after a build, the
script produced **85 files / 371 678 lines** because the tree contains
generated sources that the `GEN` set does not list: `.vmlinux.export.c`,
`.tmp_vmlinux*.kallsyms.S`, `scripts/{mod,dtc,genksyms,kconfig}/*` generated
files, `crypto/*.asn1.[ch]`, `lib/oid_registry_data.c`,
`lib/crc/crc32table.h`, `drivers/tty/vt/ucs_*`, `drivers/video/logo/*.c`,
`net/wireless/shipped-certs.c`, `*.dtbo.S`, and the build copies under
`arch/arm/boot/compressed/`.  Either `make clean` first or restrict the walk to
the known source paths (48 now).  The session-36 exports used the previous patch's
file list as the source set plus the new touch driver, and verified 48/48
byte-identical; do the same only when you know no new source
file exists, otherwise clean the tree and re-run the tree walk.

### The old (broken) recipe, for history

It used `files = [... for l in open(old) if l.startswith('+++ b/')]` and was
run whenever the tree changed; it cannot see new files.  Do not use it.  The
session-20 gotchas it records are still true: the `+++ b/` lines carry the
trailing newline (`l[6:].strip()`), and `diff -N` needs `/dev/null` for a new
file to produce the `@@ -0,0 +1,N @@` creation hunk.

