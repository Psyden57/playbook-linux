# Porting the PlayBook patches from 6.15.11 to linux-postmarketos-stable 7.3.3-r0

**Session 30's first focus (operator decision, 2026-11-07).**
Status: **DONE — 7.3.3 boots the unit to the region-1 userland with a clean panel
and working eMMC; the patch exports cleanly (42 files, 6 648 lines).  Details in
§7.**

The operator's instruction: target **`linux-postmarketos-stable-7.3.3-r0`**,
keeping the **6.15.11** tree as the backup/reference.

---

## 0. The target, verified

* `https://www.kernel.org/releases.json` on 2026-11-07: **`stable` = `7.3.3`**,
  released 2026-11-06 (mainline is `7.4-rc1`).  So 7.3.3 is real and current; an
  earlier assumption in this session that it "doesn't match the 6.x series" was
  wrong.
* The pmaports tree the operator supplied
  (`pmaports-main.zip`, 2026-09-01) carries `device/main/linux-postmarketos-stable`
  at **`pkgver=7.2.2`**.  So the 7.3.3-r0 package is newer than that snapshot and
  must come from current pmaports (or `pmbootstrap`), not from this zip.
* Its `arch="all !armhf !s390x"` — **the package does not build armv7/armhf at
  all.**  It ships `config-stable.aarch64|loongarch64|ppc64le|riscv64|x86`, and
  the APKBUILD comment says "pmbootstrap cannot dynamically choose a defconfig"
  for armhf.  **Our target is 32-bit ARM (OMAP4430, `armv7`/`arm`), so this
  package as shipped is not buildable for our SoC** — see §1.
  * For a *starting config* that is still the best thing available:
    `config-stable.armv7` **does** exist in the zip and is genuinely armv7.  It
    already has `CONFIG_ARCH_OMAP4=y`, `CONFIG_ARCH_OMAP2PLUS=y`,
    `CONFIG_MMC_OMAP_HS=y`, `CONFIG_TI_SYSC=y`, `CONFIG_OMAP_WATCHDOG=y`,
    `CONFIG_DRM_TOSHIBA_TC358764=m`, `CONFIG_DRM_OMAP=m` — but
    **`# CONFIG_OMAP2_DSS_DSI is not set`**, which we require (our whole panel
    chain is DSI), and `CONFIG_DRM_OMAP=m` where we need it built in (there is no
    module loading before the rootfs).

## 0a. Settled in session 30 (2026-11-07): current pmaports, case (b)

The pmaports repository moved from `gitlab.com/postmarketOS/pmaports` to
`gitlab.postmarketos.org` (the gitlab.com project says "Moved"; its last
activity is 2026-03).  The new host sits behind an Anubis anti-bot challenge for
HTTP browsers, but the **git smart protocol works**, so the source of truth was
read from a blobless shallow clone:

```bash
git clone --filter=blob:none --no-checkout --depth=1 \
    https://gitlab.postmarketos.org/postMarketOS/pmaports.git
git -C pmaports show HEAD:device/main/linux-postmarketos-stable/APKBUILD
git -C pmaports log -1   # d58ca1a2, 2026-11-07
```

* **`pkgver=7.3.3`, `pkgrel=0`** — the package the operator named exists.
* **`arch="all !armhf !s390x"` — unchanged.**  So the answer to §1 is **(b)**:
  take **upstream 7.3.3**, seed the config from `config-stable.armv7`, merge
  `winchester.config`; do not build the pmOS package.
* `config-stable.armv7` (12 234 lines, sha256 `dd07f6ad…`) still has
  `# CONFIG_OMAP2_DSS_DSI is not set`, `CONFIG_DRM_OMAP=m`, `CONFIG_QNX6FS_FS`
  off and the sensor drivers off — exactly the traps §5 predicted.  A staged
  copy lives at `local/work/pmaports-7.3.3/` (gitignored).
* **Packaging nuance:** the package builds `linux-7.3.tar.xz` + the
  `patch-7.3.3.patch.xz` stable patch; we downloaded kernel.org's rolled-up
  **`linux-7.3.3.tar.xz`** (sha256 `79c66dde…`, fetched from `cdn.kernel.org`,
  2026-11-07).  Same upstream content, two packagings.
  `local/pristine/linux-7.3.3` is extracted from it; `local/pristine/linux-6.15.11`
  is untouched.

**Tree layout after step 0.**  `local/kernel-tree` is now a fresh 7.3.3 tree;
`local/kernel-tree-6.15.11` is a byte-for-byte copy of the previous working tree
(`diff -rq`: 0 differences); `local/pristine/linux-6.15.11` and
`kernel/patches/0001-playbook-winchester-6.15.11.patch` are untouched.

## 0b. What pmOS/nura actually patches at these versions (checked at HEAD)

The base is **plain upstream 7.3.3 + our own 42-file patch**; no pmOS/nura
kernel patch is applied.  That is not a shortcut — the packages were checked:

* **`linux-postmarketos-stable` 7.3.3-r0** contains **only two powerpc build
  patches** (`0001-WIP-arch-powerpc-boot-Support-LLVM-1-in-wrapper-scri.patch`,
  `0002-WIP-arch-powerpc-boot-Add-z-norelro.patch`) plus the configs.  There is
  **no mobile/WL1283 patch set** in it; its value is `config-stable.armv7`
  (which we use as the config seed) and the package plumbing.
* **`linux-postmarketos-omap`** is the **armv7** OMAP kernel
  (`device/community/linux-postmarketos-omap`, `pkgver=7.1.5`, `pkgrel=1`,
  `arch="armv7"`).  It carries 11 patches, mostly for other boards:
  `iio-rescale` logic revert, HSI DMA, **TWL6030 power button**, Barnes & Noble
  Encore DTS + panel, OMAP4 CFI, Samsung P30 extcon + driver, musb omap2430
  initial-ID, **TWL6030 USB-PHY extcon/external VBUS**, and Espresso USB OTG.
  Relevance to the PlayBook: the TWL6030 pwrbutton **driver** is already in
  7.3.3 (only a DTS node is board-specific); the TWL6030 USB-PHY extcon/VBUS
  patch is **not** in 7.3.3 but is aimed at host-mode ID/extcon, not our
  device-mode charger; the rest are device-specific or unused.
* **Consequence for the nura goal.**  What a future nura/pmOS port needs is not
  a patch set to inherit but **a PlayBook device package plus an
  armv7-buildable kernel package** — the stable package refuses armhf by design.
  Our 42-file patch + `winchester-7.3.3.config` is already in that shape, so the
  kernel can be packaged on top of a nura tree without re-porting.  Do not
  substitute `linux-postmarketos-omap` 7.1.5 wholesale: it is an older base and
  none of its patches targets this board.

Fetched from the same blobless pmaports clone (§0a), 2026-11-07:
`git ls-tree HEAD:device/main/linux-postmarketos-stable` and
`git ls-tree HEAD:device/community/linux-postmarketos-omap`.

## 1. The target question — ANSWERED: case (b)

**Is `linux-postmarketos-stable` packaged for armv7/armhf at 7.3.3-r0?  No.**
The current pmaports APKBUILD (§0a) still declares `arch="all !armhf !s390x"`.

| answer | consequence |
|---|---|
| (a) 7.3.3-r0 added `armhf` | we use the package's config + patches directly, and the port is mostly our own delta |
| **(b) still `!armhf` — the answer** | we do **not** use the package; we take **upstream 7.3.3** as the base, start the config from `config-stable.armv7` and merge our `winchester.config`, and keep pmOS's patches only where they matter |
| (c) the operator means the *package name* as a kernel version string only | same as (b) |

## 2. What our patch is, and what will hurt

34 files, **5 703** lines.  The churn per file, from
`kernel/patches/0001-playbook-winchester-6.15.11.patch`:

| lines | file | port risk |
|---|---|---|
| 765 | `drivers/gpu/drm/omapdrm/dss/dsi.c` | **highest** — display is our most delicate subsystem and this file is instrumented *and* functionally patched |
| 306 | `drivers/irqchip/irq-gic.c` | **highest** — irqchip changes are invasive and 6→7 may rework the GIC driver |
| 272 | `drivers/gpu/drm/bridge/tc358764.c` | high — upstream bridge driver has changed since 6.15 |
| 215 | `drivers/mmc/host/omap_hsmmc.c` | high — this is also where our `PB*` instrumentation lives; much of it should be **dropped**, not ported |
| 42 / 24 | `arch/arm/kernel/head.S`, `arch/arm/boot/compressed/head.S` | **load-bearing** (the PlayBook prelude + DEBUG_LL); must port exactly |
| 34 / 15 | `dss.c`, `pll.c` | high (DISPC core clock fix lives here) |
| 28 | `arch/arm/mach-omap2/io.c` | medium |
| 26 | `drivers/bus/ti-sysc.c` | medium |
| 20 / 14 / 6 | `kernel/async.c`, `drivers/base/power/runtime.c`, `drivers/base/dd.c` | **suspect** — these are almost certainly bring-up probes, not requirements; drop them and see |
| 13 / 9 / 7 | `omap4-l4.dtsi`, `omap4-common.c`, `gpio-omap.c` | medium |
| — | `drivers/misc/pbmon.c`, `pb_event.h`, `omap4bc.S`, `omap4-winchester.dts`, `cm3217.c` | ours; carry over as-is |

**The split that matters.**  Roughly half of the 5 703 lines is
**instrumentation** (`PBM:/PBS:/PBC:/PBSIO:/PBCLK:/PBD:/PBGPIO:/PBX:` prints,
`pb_event` breadcrumbs, `pb_reg`, the `trace`/`init_hz` knobs).  Porting it all
is the wrong move: port the **functional** delta first, boot, and only re-add the
prints needed to debug a specific failure.  A clean 7.3.3 tree is also the point
of the exercise.

## 3. The port plan (each step is a checkpoint)

**Step 0 — prerequisites, no bench.**
1. Fetch **upstream `linux-7.3.3.tar.xz`** (`cdn.kernel.org/pub/linux/kernel/v7.x/`)
   and keep it as `local/pristine/linux-7.3.3` **alongside** the existing
   `local/pristine/linux-6.15.11`.  Do not remove the 6.15.11 tree, tarball or
   `local/kernel-tree` — they are the backup and the reference.
2. Settle §1 (the `arch=` question) from current pmaports.
3. Start the new tree: copy `local/kernel-tree` aside as
   `local/kernel-tree-6.15.11` (the working reference), then build
   `local/kernel-tree` fresh from 7.3.3.

**Step 1 — config.**  Merge `kernel/winchester.config` onto 7.3.3's defaults,
starting from `config-stable.armv7`.  The non-negotiables are the ones the
project already knows: `CONFIG_ARCH_OMAP2PLUS`, `CONFIG_ARCH_OMAP4`,
`CONFIG_OMAP2_DSS_DSI=y` (**not** set in pmOS's armv7 config),
`CONFIG_DRM_OMAP=y` (built in, not `=m`), `CONFIG_MMC_OMAP_HS=y`,
`CONFIG_TI_SYSC=y`, `CONFIG_QNX6FS_FS=y`, `CONFIG_HWMON=m`/`CONFIG_IIO=m` where
the module closure needs it, `CONFIG_IKCONFIG=y`, and the `/dev/console`-free
initramfs settings.  Expect renames between 6.15 and 7.3.3; `make oldconfig`
will list them, and `scripts/diffconfig` (or `make savedefconfig`) is the way to
review the delta rather than trusting a hand-merge.

**Step 2 — the load-bearing core, one boot per checkpoint.**
`arch/arm/kernel/head.S`, `arch/arm/boot/compressed/head.S`,
`arch/arm/include/debug/omap4bc.S`, `arch/arm/Kconfig.debug`,
`arch/arm/mach-omap2/omap4-common.c` (the L2 SMC path).  **Goal: the DEBUG_LL
console prints and the kernel reaches early console.**  Nothing else until that
works.

**Step 3 — the board DTS.**  `omap4-winchester.dts` plus the
`omap4-l4.dtsi`/`omap4.dtsi` deltas and the `Makefile` line.  **Goal: DRAM
detected, `uname` on the console.**  The DTS is the one artefact that is
basically portable as-is — it is data, not code — so a failure here is a binding
rename, not a rewrite.

**Step 4 — the mmc/irq/bus core.**  `irq-gic.c`, `omap_hsmmc.c`, `ti-sysc.c`,
`gpio-omap.c`, `mmc/core/*`.  **Goal: eMMC reads, region-1 root mounts.**
Port only the functional hunks; leave every `PB*` print behind unless a failure
needs it.

**Step 5 — the display.**  `dsi.c`, `tc358764.c`, `dss.c`, `pll.c`.  **Goal: the
panel shows a clean image.**  This is the largest single risk (765 + 272 + 34 +
15 lines) and the one subsystem where the 6.15.11 code is a *tuned* result
(bridge delivery, `PPI_TX_RX_TA = 0x00040006`, LVDS porches, the DISPC core
clock).  Port them as a set — they were found as a set — and diff the resulting
register writes against the values in `STATE.md`'s per-subsystem table.

**Step 6 — the peripherals we just got working.**  `cm3217.c` (ours, carries
over), the FAN5702 driver, the sensor/I²C DTS nodes, and *then* the parked
WL1283 attempt — on a tree where the display and storage are known good, so a
failure is unambiguous.

**Step 7 — re-export and record.**  Regenerate
`kernel/patches/0001-playbook-winchester-<ver>.patch`, verify with
`patch -p1 --dry-run` against the new pristine tree, and add a
`kernel/patches/README.md` section for the new base.  **Keep the 6.15.11 patch
file** — it is the reference for a bisect back.

## 4. What "done" looks like for this focus

* A 7.3.3 tree in `local/kernel-tree` that boots the PlayBook to the region-1
  userland with a working panel and eMMC.
* `local/pristine/linux-6.15.11` + `local/kernel-tree-6.15.11` + the 6.15.11
  patch file all still present and untouched.
* A patch exported against 7.3.3 that applies cleanly to pristine 7.3.3.
* `STATE.md` naming which image is known-good on which base — the two must not
  be confused, because the image CRC alone will not tell you the kernel base.

## 5. Risks worth stating up front

* **The 6→7 series jump is not the 6.15→7.3 jump it looks like.**  A `.x` major
  bump in the 7 series carries whatever reworks happened in 7.0–7.3; assume the
  OMAP display and irqchip code *did* move and budget for rebasing, not
  cherry-picking.
* **Our patch mixes requirements with scaffolding.**  The single most useful
  discipline in this port is to refuse to carry anything that is not needed to
  boot.  Every print ported "just in case" is future debt in a publishable
  patch (an existing open item, `KNOWN-ISSUES.md` §"Still open").
* **`CONFIG_DRM_OMAP=m` and `OMAP2_DSS_DSI` off in the pmOS armv7 config** will
  silently give a black panel if inherited unexamined.  This is the most likely
  way to lose a whole boot cycle.

## 6. Sources

* `kernel/patches/0001-playbook-winchester-6.15.11.patch` (the 34-file/5 703-line
  delta and the per-file churn in §2).
* `kernel/winchester.config`, `local/kernel-tree/.config`.
* `https://www.kernel.org/releases.json` (2026-11-07): stable `7.3.3`, released
  2026-11-06.
* The operator's pmaports snapshot
  (`pmaports-main.zip`, 2026-09-01): `device/main/linux-postmarketos-stable`
  APKBUILD `pkgver=7.2.2`, `arch="all !armhf !s390x"`, and
  `config-stable.armv7`.

## 7. Session-30 checkpoint log — the port is DONE

**Status: all six §4 criteria met on 2026-11-07.**  The unit boots 7.3.3 to the
region-1 BusyBox userland, the operator confirmed a clean panel (console text →
solid white → console text), eMMC reads/writes and the ext4 root work, and the
exported patch reproduces the working tree from pristine 7.3.3 byte-for-byte.

### 7.1 What was actually done

1. **Target settled (case (b), §1).**  Current pmaports HEAD (d58ca1a2,
   2026-11-07) has `pkgver=7.3.3`, `pkgrel=0` and still
   `arch="all !armhf !s390x"`.  Upstream 7.3.3 is the base.
2. **Trees.**  `local/pristine/linux-7.3.3` extracted from kernel.org's
   `linux-7.3.3.tar.xz` (sha256 `79c66dde…`); `local/kernel-tree-6.15.11` is a
   byte-exact copy of the old working tree (`diff -rq`: 0 differences);
   `local/kernel-tree` replaced with the fresh 7.3.3 tree.
3. **Config.**  Seeded with pmOS `config-stable.armv7` (12 234 lines), then
   `winchester.config` merged over it and `olddefconfig`.  The merge matters:
   the pmOS base alone has `OMAP2_DSS_DSI` **off** and `DRM_OMAP=m`; both are
   forced to `=y`.  The final config is `kernel/winchester-7.3.3.config`
   (sha256 `7e0aa261…`).
4. **Patch application.**  The 6.15.11 patch applied to 7.3.3 with **7 rejects
   in 6 files**, all rebased mechanically:
   * `arch/arm/boot/dts/ti/omap/Makefile` — upstream inserted
     `omap4-samsung-espresso7/10.dtb` before `omap4-xyboard`; the new line goes
     before xyboard.
   * `drivers/gpu/drm/bridge/tc358764.c` h1 — `mod_devicetable.h` was removed
     upstream; the new includes are `linux/io.h` and `linux/of.h`.
   * `tc358764.c` h7 — `drm_bridge_attach(encoder, …)` replaced
     `bridge->encoder`.
   * `drivers/mmc/host/omap_hsmmc.c` h14 — upstream returns `ret` instead of
     `goto err1`.
   * `init/do_mounts.c` h3 — the `out:` label is gone (pivot_root rewrite); the
     two `pb_event` calls were placed inline.
   * `init/main.c` h3 — same shape, `do_initcall_level` unchanged otherwise.
   * `drivers/misc/Makefile` — appended `obj-y += pbmon.o`.
5. **Missing files (trap 106).**  The 6.15.11 patch's file list is derived from
   itself, so it omitted `cm3217.c`, `leds-fan5702.c`, `pbmon-pair.h`,
   `pbmon-page.h` and their Kconfig/Makefile lines.  All were copied from the
   6.15.11 tree; `pbmon-pair.h`/`pbmon-page.h` are the build-stopping ones.
6. **XZ compression.**  With gzip the image was **0xB8ABB4**, over the
   `0xB00000` IFS-slot bound (the base container is `ifs0.sealed.bin`,
   0x2BFE10, so the zImage budget is ~8.6 MB).  `CONFIG_KERNEL_XZ=y` cut the
   zImage from 9 124 816 B to **6 568 696 B**; final image **0x91AB20**
   (9 544 480 B, CRC **`0x0F6AC47F`**).  This is a packaging fix, not a
   functional one.
7. **Boots.**  Three boots on 7.3.3:
   * `0x83E1A570` — first 7.3.3 image: to the userland, panel clean, eMMC/root
     rw, but the DTS snapshot in the patch was older than the tree (no CM3217
     node; patch exported 07:24, node added 09:23).
   * `0x…` (rootfs-only flash) — validated the 7.3.3 module tree: 15 modules
     loaded, BMA150/HMC5843/MPU3050/TMP103/BQ27541/FAN5702 all bound; CM3217
     instantiated by hand read **32 lux**.
   * `0x0F6AC47F` — final image (CM3217 node + fixed driver) + rootfs
     `0b3b83e8…`: `3-0010` probes from DT, IIO `cm3217`, **26–30 lux**; the
     unbind/bind cycle that used to oops now survives (trap 107).
8. **Export.**  `kernel/patches/0001-playbook-winchester-7.3.3.patch` —
   **6 648 lines / 42 files** (34 modified + 8 new), generated from a
   tree-vs-pristine source walk, applied to pristine targets with **0 rejects**,
   and all **42 files compare byte-identical** to `local/kernel-tree`.  The
   export now includes the prepared (untested) TWL6030 charger node and the
   `twl6030.dtsi` gpadc label; the last tested image remains `0x0F6AC47F`.
9. **Backup intact.**  `local/pristine/linux-6.15.11`, the 6.15.11 tarball
   (sha256 `89ab469f…`) and `kernel/patches/0001-playbook-winchester-6.15.11.patch`
   are untouched; `local/kernel-tree-6.15.11` is the working reference.

### 7.2 What the port did *not* do

* It did not strip the `PB*` instrumentation.  The whole patch (including
  `init/main.c`, `kernel/async.c`, `drivers/base/{dd.c,power/runtime.c}` and the
  `PBD:`/`PBGPIO:` MMC dumps) is carried deliberately: the first 7.3.3 boot used
  it, and pruning is a separate, testable change.  The open item in
  `KNOWN-ISSUES.md` §4 still stands.
* It did not re-test the WL1283 (parked, report 180 §6) or the display-timing
  experiments.  The panel was verified by the operator on the ported image,
  which is the acceptance criterion; the register-level whole-map diff against
  `STATE.md` was not repeated.
* It did not upstream anything.  The 7.3.3 patch is still a bring-up patch.

