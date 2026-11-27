# Roadmap

Open work only; closed and parked item history is archived in
[`docs/history/ROADMAP-CLOSED.md`](../history/ROADMAP-CLOSED.md).  Each open
item states the evidence that would close it.  Completed *steps* are kept inline
only where an open item's sequence needs them (item 7a), marked "Done (session N)".

Current focus: **item 7a — put the UI on the GPU.  DONE (sessions 68–70).**
The clients run on the SGX and the desktop is responsive; the buffer waste that
made it fragile is fixed.  Three defects were root-caused and fixed: the
compositor bound imported dma-bufs as incomplete GLES2 NPOT textures (catacomb
`smithay-eglimage-clamp.patch`, r10003); the panel client hard-coded GL
attribute/uniform indexes the PowerVR linker assigns differently (epitaph
`bind-attribs.patch`, r10008); and **the vendor allocator asked omapdrm for a
1-pixel-wide buffer**, so omapdrm's 8-byte pitch alignment inflated every
allocation 8× (report 271, deployed as an `LD_PRELOAD` shim — idle desktop
59.4 → 9.5 MiB pinned, every GEM object 1.00×, trap 349 gone).
**The durability rebake is built, flashed and validated (session 71):**
`local/work/nura/images/blackberry-playbook-root-gpu-20261125.img` (sha256
`5d3eac8f…`) applies catacomb r10003, epitaph r10008, the inittab affinity line,
the pitch shim, **the live ARMv7 module tree** (the base carried ARMv6 — trap
356) and **the permissions set** (trap 358), leaving the hand-tuned
`/etc/catacomb/initrc` alone (trap 357).  The first GPU-only rebake is
`…-preperm.img` (`9b656d36…`).  `tools/rebake-hd3.sh`/`tools/rebake_hd3.py`
reproduce it, as root (loop mount) or a normal user (debugfs).
**The browser stack is built and installed on the live root (session 71):**
WPE WebKit 2.54.3 from the fork (official WPE tarball + six patches) and Kumo
1.10.0 (reports 273/274).  Next: the **Kumo GUI experiment on the panel** (RSS,
first render, touch zoom, touch context menu), then include both packages in the
next hd3 rebake.  After that: a heavy-redraw measurement, trap 350 (cosmetic
glitches), and packaging the shim as a `pvr-dbmfix` APK.
The SGX540 works (kernel driver + pvrports userspace; EGL/GLES 2.0 on
`PowerVR SGX 540`), catacomb serves a **PRIME-only `wl_drm`**, and the clients
run on the SGX.  The two presentation defects are fixed and installed on the
live root: the imported dma-buf is bound with `CLAMP_TO_EDGE`
(`rootfs/catacomb/smithay-eglimage-clamp.patch`, catacomb `1.5.0-r10003`,
trap 347, report 269 §4g) and epitaph binds its GL locations explicitly
(`rootfs/epitaph/bind-attribs.patch`, r10008, trap 348, report 269 §4h).
**Long-term route:** keep the fully open `powervr_sgx` stack in view.  The unit runs
**dual-core ARMv7 at 1008 MHz** with `cpufreq-dt` bound (OPPs 300/600/800/1008
MHz; the only gap is the unattached CPU supply), on region 0 `0x3547D613`; hd3
holds the Nura root, **writable, grown to 62.5 G**, with **SSH working**
(`PB_LINK=usb tools/pbssh.sh`).  Session 67 fixed the **MUSB mailbox fault** (a
freed TWL6030 `phy_companion` — report 265) and found that the **second core
was taking no userspace work** (PID 1 affinity-masked to CPU0 — report 266).
Closed by earlier sessions: the eMMC cache-flush item (no cache, trap 300), the
OSK repeat (wvkbd), touch-to-wake DPMS, and the `epitaph` panel (r10005).  The
region-1 BusyBox root stays the fallback.
The audio goal is closed (reports 214/215), and the **WL1283's WLAN associates
and passes traffic**.  On the same chip, **BT's core works** (real inquiry;
`hci0` registers with mgmt) with the controller-open blocked on a stray byte,
then FM and GPS.

## 7. Packaging (postmarketOS-style)

The intended end state: a reproducible artefact that builds the kernel from the
patch + config, produces the chainloader image, and installs onto the device —
the shape postmarketOS uses (a device package + a kernel package + an installer).
Nothing about tooling, licence or distribution has been decided
(`DECISIONS.md` §Open policy decisions); this item records the target, not a plan.

**Closes when:** a clean checkout plus documented host prerequisites reproduces
the image byte-for-byte (same CRC) and installs it on a unit without manual
steps.

## 7a. A real root on hd3, then SGX540 — **active (operator, session 53)**

**Goal, in the operator's order:** move off the 64 MiB BusyBox root in region 1
onto **hd3 (62.86 GiB)**, then get **SGX540 / OpenGL ES 2.0** hardware
acceleration working so a real DE (Catacomb or Sxmo-dwm) can run.  This is item
7's intent made concrete, and it reopens what `## Out of scope` used to list.

**Established by measurement (session 53):**

* **No large transfer is needed.**  hd3 is MCT type `0x1C`, blocks
  `0x4905–0xFFFFF` = **62.86 GiB**; a 2 GiB seed image is 3.18 % of it.  The
  pre-OS loader's allow-list already includes `0x1C`, and `bbusb.py` encodes the
  extent (`'0x1C FS Fixed': (0x4905, 0xFFFFF)`), so both routes below use paths
  the project already trusts.
* **hd3's contents, read live:** a DOS MBR at `0x49000000` carrying a QNX v1.2b
  boot sector, and the QNX6 superblock at **`0x49002000`** — matching report
  138's corrected offset.
* **In-place filesystem creation works on the device now.**  Rehearsed
  non-destructively: `losetup -f -o OFS` → `mkfs.ext2 -F -b 4096` → mount →
  write → unmount.  `losetup -o` exists, and `initramfs/init.c` already mounts
  region 1 through `LOOP_SET_STATUS64` + `lo_offset`.
* **Device tool gap:** busybox has `mke2fs` but **no `offset=`** and **no
  `resize2fs`**.  The first is covered by `losetup -o`; the second is why the
  image is built on the host rather than grown on the device.

**Established by measurement (session 54, report 240):**

* **pmbootstrap builds the rootfs without owning the kernel, but its install
  pipeline is not kernel-agnostic.**  A `device-blackberry-playbook` port with
  `flash_method="none"` and no kernel dependency installs `postmarketos-base`
  and the device package, then `create_device_rootfs()` runs `mkinitfs`, which
  requires exactly one `/usr/share/kernel/*/kernel.release`; with none it fails
  with `only one kernel release/flavor is supported, found: []`.
* **A Nura `linux-*` package can wrap our 7.3.3 tree.**  The artifact package
  `linux-blackberry-playbook` installs `/boot/vmlinuz`, the DTB,
  `/usr/share/kernel/winchester/kernel.release`, and `/lib/modules/7.3.3`
  (43 modules + `modules.dep`); `mkinitfs`/`boot-deploy` then complete.  A
  production port would use `--envkernel`/the pristine tarball; the layout is
  the same.
* **`--split` produces the root image we need:** a 597 MiB bare ext4
  (`pmOS_root`), Nura catacomb, **464 packages**, `/sbin/init` → BusyBox init →
  OpenRC, `tinydm` → `catacomb.desktop`, 7.3.3 modules.  The 512 MiB boot image
  is unused by our chainloader.
* **Two host-side fixes are required:** remove the `/etc/fstab` `/boot` UUID
  line (`tinydm` needs `localmount`, and there is no boot partition) and add
  `ttyGS0::respawn:/bin/sh -l` for the ACM console.
* **The hd3 flash plan passes the gate:** loader blocks `0x4905..0x6E54` inside
  MCT `0x1C`.  The region-0 image with `ROOTFS_RAW_OFF = 0x49000000` is
  `0x939D5C` B, CRC `0x8B709950`; the kernel patch is unchanged.

**Established by measurement (session 54, report 241):**

* **hd3's filesystem starts at loader block `0x4915`** (the MCT entry `0x4905`
  has a 1 MiB prefix); the loader gate rejects `0x4905` (`0xF7 -> cmd 0x15
  data 03`, nothing written).  The flash wrote 626 065 440 B and verified sha256
  `076bafcf…` byte-exact.
* **Nura boots from hd3** (7.3.3, Nura edge, OpenRC, the display and the Munin
  touch) once the pre-chroot check uses `lstat` (the absolute `/sbin/init ->
  /bin/busybox` symlink); image `0x060B25F2`.
* **The first ext4 journal/udev write stalls the eMMC** (`mmc1: Card stuck being
  busy! __mmc_poll_for_busy`), aborts the journal and remounts read-only;
  `tinydm`/`catacomb` cannot get a libseat session.  The region-1 no-journal
  root is stable; the read-only hd3 + overlayfs is the workaround.
* **The Nura root lacks the WLAN firmware** (`wl128x-fw-4-sr.bin` `-2`); add
  `local/work/firmware-nvs`.
* **The `PB*` prints are now `pr_debug`** (dyndbg), `PBREQ`/`PBIRQ` behind
  `pb_trace`; `CONFIG_DYNAMIC_DEBUG=y`, `CONFIG_OVERLAY_FS=y`; patch 63 files /
  8 998 lines (sha256 `48c14a41…`), image `0xE84693A1` (built, not flashed).

**The design decision (operator, session 53): hd3 holds a flasher-restorable
ext4 image, not an in-place mkfs.**  A known-good Linux image that the bootrom
flasher can put back is worth more than an in-place filesystem, because it is
what makes a broken root recoverable without needing a working Linux on the
device.  So the primary route is **(B) build the image on the host, flash it
with `bbusb.py`** — the same loop the project already trusts — and treat
image size as a policy choice rather than a constraint.

**Size is a policy choice, and the transfer is what sets it.**  `analyze_blocks()`
skips runs of **`0xFF`**, not `0x00`, so a sparse (zero-filled) ext4 does **not**
have its holes skipped: a fully expanded 62.86 GiB image would send all 62.86 GiB
over the bootrom link.  Practical sizes therefore:

| seed | flasher sends | covers |
|---|---|---|
| 1 GiB | 1 GiB | 1.59 % of hd3 |
| 2 GiB | 2 GiB | 3.18 % |
| 4 GiB | 4 GiB | 6.36 % |

Grow by building a larger image and flashing it over the seed (the gate accepts
any run inside `0x1C`), rather than by `resize2fs` — which busybox does not have.
The invariant is that hd3 **always** holds a flasher-restorable ext4 image.

The in-place mkfs route (A) is **not** preferred for that reason, though it is
proven to work on the device (`losetup -f -o OFS` → `mkfs.ext2` → mount) and
remains useful for the *populate* step, and as a way to test a filesystem
without touching the media.  There is no `resize2fs` on the device, which is the
other reason to prefer building images on the host.

**Mechanism change is one constant.**  `initramfs/init.c`'s
`ROOTFS_RAW_OFF` moves from `0x29000000` (region 1) to `0x49000000` (hd3); the
initramfs already falls back to the bring-up console when the root will not
mount, so a bad hd3 costs a boot, not a brick.  **Region 1 stays intact as the
fallback** through every step.

**SGX.**  Mainline 7.3.3 has **no** SGX/PVR driver (`ti,omap4430-gpu` /
`img,powervr-sgx540` are bound by nothing), though the DTS target-module, the
GFX power domain and the L3_GFX clock all exist upstream.  The route is
`gitlab.com/pvrports/pvrports`: `linux-openpvrsgx` (kernel driver, its
`config-openpvrsgx.armv7` sets `CONFIG_SGX=y`, `CONFIG_SGX_OMAP=m`,
`CONFIG_SGX_DRM=y`) plus `non-free/sgx-ddk-um` (TI userspace,
**licence=proprietary**).  Its reference devices are the Samsung espresso
(OMAP4430, TWL6030) — the same SoC family as this unit.  Its driver targets
6.1, so a rebase to 7.3.3 is expected work.  **Blobs stay gitignored in
`local/`**, like the QNX dumps and the TI TRM.

**Safety.**  The operator has the UFS images, so **no hd3 backup is required** —
a reflash restores TabletOS.  Writing hd3 destroys the QNX userland on media
until then; region 1 and the sealed IFS are the way back, and the MCT/NVRAM/RPMB
are untouched throughout (`SAFETY.md` §6).

**Open decision that gates the writes:** which userspace goes on hd3 —
**Nura (formerly postmarketOS, renamed 2026-09-27)** or **our own minimal root,
grown**?

**Decided (operator, session 53): Nura, with Catacomb or Sxmo-dwm as the DE.**
Research notes, all from primary sources (`nura.eco`, `docs.nura.eco`,
`wiki.nura.eco`) — the rename is seven weeks old, so **search "Nura", not
"postmarketOS"**, and note that most of the ecosystem's URLs and package names
still say `postmarketos`/`pmaports`:

* **Both interfaces exist and fit.**  Catacomb is described as a *lightweight
  interface optimised for low-end hardware with limited battery*; Sxmo is
  selectable as `sxmo-de-dwm` (`pmbootstrap config ui sxmo-de-dwm`).  Either is a
  sane target for 1 GB RAM / SGX540.
* **The PlayBook is NOT a supported device** — a search for a PlayBook/winchester
  port finds only this project — so this is a **new device port**.
* **The OMAP4430 reference port is `device-samsung-espresso7`** (Galaxy Tab 2,
  same SoC family and TWL6030), and it is the model to copy.  Its
  `deviceinfo` shows the shape: `deviceinfo_arch="armv7"`,
  `deviceinfo_dtb="omap4-samsung-espresso7"`, `deviceinfo_append_dtb="true"`,
  `deviceinfo_drm="true"`, `deviceinfo_chassis="tablet"`, and
  `depends="postmarketos-base linux-openpvrsgx mkbootimg"`.
* **The one structural difference is how the kernel gets on the device.**  The
  espresso port uses `deviceinfo_flash_method="heimdall-bootimg"` (a Samsung
  download-mode boot image at fixed offsets).  The PlayBook has **no such
  path**: its kernel must be the **CRC-sealed chainloader image** that
  `tools/build_kernel_image.py` produces and that the pre-OS `0xF7` flasher
  writes to region 0, block `0x825`.  So the Nura **kernel package** here is not
  what flashes — our chainloader is — and the device port must be built around
  that.  That is the single biggest piece of integration work, and it is
  **already solved**: the chainloader boots a zImage + DTB and the initramfs
  hands PID 1 to a rootfs.  What is missing is only that the *rootfs* comes from
  Nura instead of our BusyBox tree.
* **The rootfs can be produced without flashing.**  `pmbootstrap install` builds
  through `pmb.install.blockdevice.create(sector_size, size_boot, size_root,
  split)`, which creates image files (with `split` for separate boot/root) rather
  than requiring a device.  So the flow is: `pmbootstrap install` → take the
  **root** image → flash it to hd3 with `bbusb.py` → the chainloader's initramfs
  mounts hd3 as `/`.  That keeps the operator's "always flashable" property and
  needs **no** bootloader cooperation.
* **Kernel packaging.**  Nura expects a `linux-*` package providing the kernel;
  ours is the 7.3.3 tree + `kernel/patches/` + `winchester-7.3.3.config`.  The
  package can wrap exactly that (`envkernel.sh` exists for out-of-tree kernel
  work) — the image it produces must then be wrapped by
  `build_kernel_image.py` rather than used as a boot.img.  Worth confirming
  against a real `pmbootstrap init` run before committing to the packaging.

**Sequence** — each step independently verifiable, and hd3 stays in a state the
flasher can replace at every point:

1. **Scaffold the Nura device port** with `pmbootstrap init` + `pmbootstrap
   aportgen`, modelled on `device-samsung-espresso7`: codename
   `blackberry-playbook`, `arch=armv7`, `dtb=omap4-winchester` (ours),
   `chassis=tablet`, `drm=true`.  Determine the kernel-package story (§above)
   against a real init run.  *No device writes.*  **Done (session 54):** the
   scaffold is `device/community/device-blackberry-playbook`.
2. **`pmbootstrap install`** with `ui=catacomb` (or `sxmo-de-dwm`), configured to
   produce **image files** (`split`), not a flash.  Verify by loop-mounting the
   root image on the host and checking `/sbin/init`, the DE and the kernel
   modules match 7.3.3.  **Done (session 54, report 240):** the root image is
   597 MiB, Nura catacomb, and the kernel is the `linux-blackberry-playbook`
   artifact package.
3. **Done (session 54, report 241) — flash the root image to hd3 at `0x4915`**
   (not `0x4905`: the MCT entry has a 1 MiB prefix; the loader gate rejected
   `0x4905`).  The write was 626 065 440 B and the device-side sha256 was
   `076bafcf…`, equal to the host image.
4. **Done (session 54) — re-point `ROOTFS_RAW_OFF` to `0x49000000`, flash region
   0, boot.**  The `lstat` hand-off fix is needed (the Nura `/sbin/init` is an
   absolute symlink); image `0x060B25F2` boots Nura from hd3.
4a. **Done (session 55, report 243) — the read-only hd3 + overlayfs root is
   flashed and booting.**  The packed container was written in one bootrom
   session (`COMPLETE 0x40C0 -> cmd 0x4006`, `done`) and the unit booted
   `7.3.3 #67` on `overlay / … lowerdir=/rootfs,upperdir=/overlay/upper` with
   the lower `ro without journal`: **no eMMC stall, no journal abort**, and the
   image's top-up took `BOOTCTR_FCTRY` `0x1d -> 0x32`.  `elogind`, `tinydm` and
   `catacomb` start, `/run/user/10000` and the catacomb Wayland socket exist,
   the touch is ready, the backlight is on, and `wlcore` boots the WLAN firmware.
   The DE draws only its empty bar because the GL renderer fails
   (`ZINK: failed to load libvulkan.so.1`) — the remaining blocker is the GPU.
   A writable eMMC root is a separate experiment; the region-1 root is the
   fallback.
5. **SGX540 — THE COMPOSITOR WORKS; apps and background are in (sessions
   56/57, reports 246/247); performance and the touch ghost are the
   remainder.**  catacomb 1.5.0-r10000 runs on the SGX540 with a smithay 0.7.0
   GLES2 fallback and maps `libGLESv2_PVR_MESA`/`libsrv_um`/`libusc`/
   `pvr_dri.so`; `squeekboard` is composited.  The app set (`tabula` +
   wallpaper, `foot`, `netsurf`, `xournalpp`, `tzompantli`, `gorm`, `grim`,
   `slurp`, `tremor`, `swayidle`) and `gsettings-desktop-schemas` are baked into
   hd3 (`f7858110…`); region 0 is `0x294F8E36` (kernel `#77`).  **Done:** the
   touch position ghost is fixed (report 248), the root is **writable, grown to
   56.5 G, with SSH back** (report 249), and session 59 closed the eMMC-cache
   item by measurement (no cache → no flush), fixed the OSK repeat by installing
   wvkbd on catacomb 1.5.0 (no UI upgrade), and made touch wake a DPMS-blanked
   panel (report 250).  **Session 60 fixed the `epitaph` panel** (report 251):
   the lazy renderer built the glyph atlas before the EGL context, and
   softpipe's advertised dual-source text draw segfaults; r10005 creates the
   rasterizer with the context, caps the atlas, and uses a single-output
   premultiplied shader — the bar renders its text and icons, and the panel's
   udev/polkit rules + `catacomb` group are installed.  **The ~2 s interaction
   latency was the client's softpipe rasterizer** (a 2 s drag: epitaph 3.16 s
   CPU vs catacomb 0.16 s).  Replacing `swrast_dri.so` with Alpine's Mesa
   21.3.9 llvmpipe build (+ `libLLVM-13`, libdrm sub-libs) drops the drawer
   `render` to 2.7–6 ms (report 253); the remaining 420–550 ms is llvmpipe's
   deferred `swap` and its first-draw JIT.  **At the time (session 60) the unit
   ran one core at 400 MHz** (`maxcpus=1` in the DTS, `dpll_mpu_ck = 400 MHz`,
   no cpufreq/OPP), while the reference programs `DPLL_MPU`/`PRM_VC` for 1.0 GHz
   dual-core (report 223) — then the biggest lever, since addressed (dual-core
   1008 MHz, `cpufreq-dt`; see the end of this item).  **Session 61 built (a); session
   62 fixed the instrument, found the real blocker and got the SMP kernel to
   userspace:** the stop points that looked like a nondeterministic hang
   (3208/3943/4211 characters) were the **loader's 7.8125 s WDT2 window**
   expiring mid-init — measured with a `CRR` sample in `head.S`: kernel entry at
   **7.450 s of 7.812 s (362 ms left)**, chainloader copy 0.825 s + XZ
   decompression **6.619 s**, while the kernel needs ~0.74 s to reach its own
   WDT2 re-program.  A **block copy** in the chainloader (`LDMIA`/`STMIA` ×8)
   plus a quiet boot (`loglevel=0`) buys that time and the SMP kernel **boots to
   userspace** (`possible = 0-1`, `online = 0`, image `0xA2FA490E`).  Along the
   way the ring became trustworthy: `+0x1000` was never a printk window (it is
   the ring's character array at #3840, the first byte past the old chainloader
   clear) and the chainloader now **hands the ring off** (append-only,
   prev/prevprev headers + timing, trap 321; report 255); with that read
   **`rcu_init()` completes**.  The SCU and every register the SMP path touches
   are accessible from non-secure Linux (`SCU_CONFIG = 1` = 2 cores,
   `SCU_CTRL = 0x531`), SMC is merely privileged, and the reference is measured
   **dual-core at 1008 MHz**.  **Session 63 withdrew trap 326:** the "SMP-only
   root-mount failure" was a rootfs-offset artefact — the SMP image's initramfs
   pointed at hd3 (`0x49000000`, then the QNX6 UFS), the UP one at region 1
   (`0x29000000`, ext4).  Flashing the region-1 ext4 (`49e318bf…`) to hd3 at
   `0x4915` makes the same SMP kernel mount `/dev/loop0 on / type ext4` and boot
   a full userland with dropbear (after an entropy seed, trap 328); hd3 is now
   the ext4 bench root, with region 1 + the sealed IFS/RCFS + the UFS image the
   way back.  `nosmp` is not a `nr_cpu_ids=1` test (trap 327).  **Session 63
   continued:** the fast chainloader (VC 1.35 V + MPU DPLL 1008 MHz at entry)
   makes single-core 1008 MHz reliable (kernel entry 3.276 s of 7.812 s, 2.54×
   CPU speedup; 1.2 GHz crashes — rejected, report 257).  CPU1 brings up fine
   (`online cpus=02`, `twd` on both cores) and dual-core 1008 MHz booted once
   cleanly, but is ~1 success / 2 wedges — a clean `execve` hang from an
   IPI/TLB-flush race (trap 330), not power (VSEL 0x3A, 97 % battery, trap 331)
   nor the watchdog; trap 329 corrected.  **Session 64 resolved the dual-core wedge:** it is the USB gadget bind from
   initramfs `/init` (`musb_start` → `omap2430_musb_enable` →
   `omap_musb_set_mailbox`), not an IPI race (reports 258–262; trap 333).  The
   reference core-up/PMIC order and `cpu_startnext` were decompiled (reports
   259/260) and an SMC-0x108 replica was tested and rejected.  Panel diagnostics
   (DSI-init TCON hook, report 261) exposed a real kernel panic.  A targeted
   MUSB device-mode fix (`kernel-usbdev-cpu1.img`, `0xC675E206`, report 262)
   gave 3/3 clean dual-core boots with the gadget up and `pbrpmb` OK.
    **Session 65:** the dual-core soak passed — 6/6 clean boots and a 376 s run
    with the gadget link alive on the **ARMv7** image — and hd3 is the Nura root
    again.  Restoring the stock mailbox with both callers serialized by a mutex
    **still panics**, so the device-mode-only workaround stays.  **Session 66
    identified the bad indirect transfer (report 264, trap 338):** with `PBMBX`
    markers and `panic=0`, the ring shows valid entry pointers (`status=4`,
    `otg->set_vbus = 0xc0729b94` = `omap_usb_set_vbus` text) and the Oops at
    `PC = 0xc2c05800` (a slab), `LR = omap_musb_set_mailbox+0x1e0` — the fault is
    `omap_usb_set_vbus()`'s tail call `phy->comparator->set_vbus`, a stale
    TWL6030 `phy_companion`; not the notifier chain and not a wild pointer at
    entry.  **Session 67 closed it (report 265, trap 339):** the comparator was
    a freed `devm` TWL6030 `twl` — `twl6030_usb_probe()` registered it in the
    PHY before `twl6030_usb_ldo_init()`, and a deferred `vusb` regulator was
    collapsed to `-ENODEV`, so the probe failed permanently and devres freed the
    object while `phy->comparator` dangled.  The fix (propagate `PTR_ERR()`,
    clear the comparator with a devm action, `usb_put_phy()`) and the restored
    stock call gave **3/3 clean dual-core boots** on `0x3547D613`.  **Later in
    session 67 the second core turned out to be doing no userspace work at all**
    (report 266, trap 340): PID 1 was affinity-masked to `{0}`, so every
    userspace process inherited `{0}` and `nproc` was 1 even with `online=0-1`.
    The inittab workaround (`::sysinit:/usr/sbin/taskset -p 3 1`) fixes it
    (`nproc` 2, load spread evenly, 2 boots with it and 3 without); a kernel-side
    fix in `sched_init_smp()` is owed.  `cpufreq-dt` **does** exist and scale, so
    the old "no cpufreq policy" note was stale.  **Session 67 opened the GPU
    question and sessions 68–69 own it (report 268 is the plan, report 267 the
    measurement).  **Sessions 68–69 result:** the selection gate was the missing
    `wl_drm` (catacomb now serves a PRIME-only one, report 269 §4c–§4e); the
    presentation defect was **GLES2 NPOT texture completeness** — the imported
    dma-buf texture kept the default `REPEAT` wrap, so it sampled opaque black
    (report 269 §4g, trap 347, DECISIONS §102).  The fix
    (`rootfs/catacomb/smithay-eglimage-clamp.patch`, catacomb `pkgrel=10003`)
    is written and verified in build order; it needs the root-owned
    pmbootstrap build.**  Measured: a Wayland client gets `swrast_dri`/`softpipe`
    even with `MESA_LOADER_DRIVER_OVERRIDE=pvr`, while the **GBM/DRM** platform
    *does* reach the SGX (12 window configs, `PowerVR SGX 540`,
    `EGL_MESA_image_dma_buf_export`); the compositor (`catacomb`) already uses
    that path.  **`libEGL_amber` is not wayland-only** — it advertises wayland,
    x11, gbm, surfaceless and device (an earlier revision of this item said
    otherwise; that came from a bad anchored `grep`, trap 344).  **Why** Mesa's
    wayland platform refuses `pvr_dri` is the open question, and the first thing
    session 68 measures.  Order: (1) diagnose that rejection
    (`EGL_LOG_LEVEL=debug`, no Mesa patch before it); (2) prove the
    vendor-supported GBM path with pvrports' `phoc` (its supported UIs are Phosh
    and Plasma-Mobile, **not** catacomb); (3) decide with data; (4) only then
    weigh the open-stack SGX540 port (`The1TrueJoe/powervr_sgx` lists SGX540 as
    wanted; `Garnet/sgx540-reversing` has the ISA/PDS work).  **Not** more
    software-rendering tuning.  **Done (session 70): the GPU work is closed** —
    trap 349 was the OMAP4 TILER scan-out aperture filling (report 270), and the
    8× that filled it was a **pitch-alignment bug in the vendor allocator**
    (report 271), now fixed and deployed.  See "Current focus".  **Next:**
    (a) **the hd3 rebake — built, flashed and validated (session 71)** — it
    carries catacomb r10003, epitaph r10008, the inittab affinity line, the pitch
    fix, **the live ARMv7 module tree** and **the permissions set**
    (`tools/rebake-hd3.sh`, `tools/rebake_hd3.py`; traps 356–358); (a0) **the
    Kumo GUI experiment — RUN, NEGATIVE (session 72)** — WPE WebKit 2.54.3
    (fork) and Kumo 1.10.0 are installed, and Kumo launches, draws its window
    and loads the page with the engine on the SGX (`pvr_dri` mapped), but the
    web content never composes: the **PowerVR GBM backend cannot export dmabufs**
    (`gbm_bo_get_fd_for_plane` = `-1`; `gbm_bo_create` SCANOUT on the pvr node
    fails), so WebKit's `AcceleratedSurface` cannot create the page buffer
    (`Failed to extract DMA-buf attributes`).  This is a capability gap, not a
    setting — `WPE_DRM_DEVICE`/`WPE_BUFFER_FORMAT` do not help, and a WebKit
    PRIME fallback is proven impossible (`tools/bench/gpu/gbmprime.c`: the pvr
    GBM exposes neither a fd nor a GEM handle).  Fix direction: a Mesa/GBM pvr
    backend that exports dmabufs, or the open `powervr_sgx` (SGX540 still
    wanted).  Report **275**, DECISIONS §107, traps 365–367; the packages must
    still be added to the next rebake; the **open route is scoped in report 276**
    — the SGX540 port needs the 28-bit / single-context / no-`USE_CODE_BASE`
    work plus OMAP4 glue, not just a table entry; start with the fact sheet and
    a "New SGX core" issue;
    (a1) **the SGX540 identity is measured (session 73) and the open tree already
    carries the 28-bit core class** — `CORE_ID` id **0x0114**, `CORE_REVISION`
    **0x00010200** (Rev 120), module `rev` **0x40000000**,
    `tools/bench/gpu/sgxcoreid.py` + `docs/captures/sgx540-identity-2026-11-26.txt`,
    **C** facts in `docs/reference/sgx540-factsheet.md`, DECISIONS §109, the
    session-73 addendum to report 276.  Three corrections to report 276's scoping:
    the upstream core check accepts **28 bits** and **one dir list** (not ≥32/≥2);
    the SGX530 is a **complete** 28-bit family entry with `allow_unisolated`; and
    the 540's `USE_CODE_BASE` is **24-bit/DM-at-24** (not the 545's 25-bit/25).
    The closed stack stays in service throughout;
    (a1b) **the entry is now written and unit-tested, and nothing is published**
    (session 73; DECISIONS §110 — no issue, no PR until the core runs on the
    unit).  `rootfs/powervr-sgx/0001-sgx540-core-entry.patch` carries
    `sgx540_regs`, a 28-bit `sgx540_heaps` taken from the DDK's own
    `sgxconfig.h` 28-bit block, `sgx540_init_regs`, `pvrsgx_cmd_core_sgx540`, the
    `cores[]` entry and the `of_device_id` row; it compiles 23/23 objects on the
    real 7.3.3 tree with zero warnings, the driver's `powervr_sgx_core` KUnit
    suite passes **8/8** (with a verified negative control) and the heaps pass
    `tools/bench/gpu/sgx540-heaps-check.py`.  Two header differences are
    load-bearing: the 540 gates **IDXFIFO and TA** too (`GENMASK(19,0)`) and its
    `BIF_INT_STAT` has no fault-type field.
    (a1c) **the driver BINDS and IDENTIFIES the core on hardware (session 73)** —
    it logs `SGX core id 0x01140000 revision 0x00010200: TI OMAP4430 (SGX540
    1.2.0)`, so the entry is validated on silicon, and the `allow_unisolated` guard
    behaves as designed.  Route: a **copy** of `local/kernel-tree`
    (`local/kernel-tree-sgx`, `mrproper` first) with the closed DDK off
    (DECISIONS §111); the GPU firmware goes in the **initramfs** (trap 380); the
    closed DDK is blocked with a modprobe.d `install … /bin/false` (trap 381), and
    never by a live unbind (it oopses).
    (a1d) **the SGX540 microkernel builds but is a `srvinit` image** — `3-byte`
    delta from the SGX545 image, all 7 offline tests pass, SGX545 golden hash
    intact; the driver rejects it because a one-context core needs a **`pdtable`
    patch and a list-0 load**, and the image routes TA/3D to **list 1** (trap 382).
    **Resolved in session 74** — see (a1e).
    (a1e) **the SGX540 microkernel STARTS on silicon and completes a NULL job
    (session 74).**  `open microkernel invfix, interface 1.0, features 0x8` →
    `EDM init: event status 0xa0000000, BIF int 0x8000` → `microkernel started`,
    with the driver's debugfs agreeing (`firmware running: 1`, `uk-lockups 0`,
    `BIF_FAULT 0`); `submit-null-job` went from a universal `consumed 0` timeout
    to **`1/1 passed`**.  Four SGX545 assumptions were corrected
    (`rootfs/powervr-sgx/0002-sgx540-one-context-list0.patch`), each gated on the
    per-core `SINGLE_CONTEXT` flag with the SGX545 serial image kept
    byte-identical (DECISIONS §112), and the driver needs `edm_init=1` (its §9
    single-context launch).  Then a kick, then
    the register tables (hypothesis), `sgx540_init_regs`, `USE_CODE_BASE`
    existence (the unit is settled: shift 8, DM at 24), and Mesa;
    (a1f) **HANDOFF-74 §0b is ANSWERED: the EDM is never re-dispatched, and the
    "passing" job was the driver's own timeout (session 75).**
    `rootfs/powervr-sgx/0003-sgx540-kick-counter.patch` counts `kick` entries and
    empty-CCB exits in control-block words 0x3d/0x3e; over a whole job window
    **both stayed 0**, the kernel ring held the command (`ccb wo 1 ro 0`) while
    the microkernel read `0/0`, and six back-to-back jobs cost **six** resets.
    The two `1/1 passed` runs read **2016806** and **2020499** µs against
    `job_timeout_ms = 2000`: `pvrsgx_timedout_job()` signals the fences with
    `-ETIME`, but the test's syncobj is bound to `drm_sched`'s
    `s_fence->finished`, which `drm_sched_start(sched, 0)` finishes **without**
    an error, and `drm_syncobj_array_wait_timeout()` never inspects the error —
    so the wait returns 0 at the 2 s expiry.  Two handoff figures are corrected:
    **`consumed` is an artefact after a reset** (`pvrsgx_ccb_reset()` sets
    `reset_seq = issued`, so `consumed == issued`; the idle boot reads
    `issued 1 consumed 1`), and the boot ran **`ccb2`**, not `invfix`.
    [capture `docs/captures/sgx540-ukernel-kick-counter-2026-11-26.txt`,
    report 278]  **The next session's unit is the doorbell
    (`EUR_CR_EVENT_KICK` 0xac8 / `EVENT_KICKER` 0xac4, 28-bit) and
    `EVENT_PDS_ENABLE` (`0x2204f00f`), read from inside the driver** (trap 384).
    Two cheaper explanations were cleared statically and must not be re-run: the
    differing init/job kick offsets are the vendor's own `MULTI_EVENT_KICK`
    behaviour (trap 392), and the event program's address matches `srvinit`'s
    derivation (trap 393).  **Then measured, three boots: the timer fires,
    nothing consumes it, and one PDS bit does not stick.**  A dynamic counter
    (`UK_TICKLOG`; a static `ctlblk` word can never show a 1 kHz event, trap 394)
    read 0 over 12 s, yet `EUR_CR_EVENT_TIMER` is alive — `0x010228fe`, `ENABLE`
    set, cycling inside its period — so the driver's period is right and my
    "1000×" claim is retracted (trap 395).  `EVENT_STATUS` b29 stays latched and
    IRQ 147 never fires.  Of the whole dispatch configuration exactly one field is
    not what the driver wrote: **`EVENT_PDS_ENABLE` reads `0x2204b00f` where it
    writes `0x2204f00f`** — bit 14 `SW_EVENT`, stable across reads.  **A per-bit
    sweep then settled it: bit 14 is absent from that register on this silicon**
    (every other bit sticks; bit 14 returns 0 when written alone), so session
    74's `SGX540_CR_EVENT_PDS_ENABLE_ALL 0x2204f00f` was a **no-op** — the one
    session-74 "correction" that measured nothing, and the fact sheet's first
    **C** fact to contradict a **K** one (trap 397).  The other three stand.  The
    init task *does* run (`SC_VERSION`, `EVENT_HOST_ENABLE` b29 — not the
    "microkernel started" line, which the host writes under `edm_init=1`, trap
    396).  Next: whether `EVENT_PDS_EXEC` (`0x00804090`, 22-bit/64 MiB field) points at
    code this core executes, then the doorbell.
    Then one interaction to make the EDM dispatch at all, and the aim after that
    is a frame.
    (a3) **The vendor's SGX540 microkernel is in the DDK 1.8 drop as source AND
    as a prebuilt binary — it is REFERENCE, we still write our own (operator,
    session 76).**  Operator direction: the open driver compiles its own
    microkernel, because ours can be cleaner and the vendor's 2014 blob predates
    what modern Mesa needs; reading their source is explicitly fine.  So the
    vendor image is evidence, not a deployment target.  `services4/srvinit/
    devices/sgx/eventhandler.pds.asm` is the program `EVENT_PDS_EXEC` points at,
    and it is a
    **PDS dispatcher**: `movs douta, ir0, INPUT_IR0_PA_DEST`, branch on the bits,
    `movs doutu` into the USSE EventHandler.  Its event bit map is **not**
    `EUR_CR_EVENT_STATUS`: `EURASIA_PDS_IR0_EDM_EVENT_TIMER` is **bit 1**
    (`hwdefs/sgxdefs.h:3270`) against this port's bit-29 poll.  The constants are
    patched by the host at load (`sgxinit.c:2507-2510`); the generated
    `eventhandler_pds.h` names the words (IR0 `[17]`, IR1 `[20]`, timestamp
    `[16]`, DOUTU1 `[1]/[5]/[9]/[13]`, DOUTU2 `[29]`, generic DOUTU0 `[12]`,
    3D `[0]`, TA `[4]`, SPM `[8]`; segment 144 B) — and **our loader patches
    only `ctlblk` and `pdtable`**, so no PDS-input routing exists.  The drop also
    carries a **finished vendor image**,
    `binary2_540_omap4430_android_{debug,release}/…/ukernel/ukernel.bin`
    (22 192 B `cfc577aa…` / 19 984 B `ff69e400…`), with `ukernel_labels.h`'s
    per-command entry table (`EVENTHANDLER 0x50`, `TIMER 0x2AD0`,
    `GETMISCINFO 0xEE0`) and 2D/3D/SPM/TA splits; it disassembles as SGX540 code
    with `local/work/sgxasm/sgx-disasm -c sgx540`.  It is **not** to be deployed
    — but it is the cross-check our own image is measured against (and it
    already cleared one suspicion: rev 120 defines no `FIX_HW_BRN_29104`, so the
    vendor also ends a task with a bare `mov.end` and no `phas`, as ours does).
    Extract: `docs/captures/sgx540-vendor-ukernel-source-2026-11-26.txt`;
    trap 399; DECISIONS §115.
    (a4) **Does the kick path reach the microkernel at all?  This now blocks
    everything upstream of it (session 76).**  With the interrupt reaching the
    CPU, the PDS launch defects fixed and `DMS_CTRL` corrected, the corrected
    entry counter reads `entry = 2, tick = 1`, unchanged over 94 s and across
    eight host kicks — and a PDS-task emit, correctly placed on `tmr_tick_end`
    and provably firing (the one-shot flag goes 1 → 0), produces **no** re-fetch
    (traps 402/404).  `REARM flag` is still 1 after eight kicks, so **no task end
    runs at all** in that configuration: `init_set`'s is the only one, and an
    idle boot apparently never reaches a second.  The next unit is therefore not
    the re-emit but whether a job reaches the microkernel: re-instrument the
    `kick` entry on a control-block word **verified** free (trap 403 — 0x07 is,
    0x1c is `CLEANUP_DEFER_CT`), read `ctlblk` before and after a submit, and
    follow HANDOFF §0b's four outcomes.  **Owes also, and blocked on this:**
    getting one job to complete, which is session 76's original goal and the
    precondition for measuring any re-emit at all.
    (a2) a
    heavy-redraw/frame-latency measurement (now meaningful — the TILER pressure
    is gone) and trap 350; (b) attach the CPU supply or confirm 1.35 V for all OPPs and settle the
    `twl6030_uv_to_vsel` clamp (report 266 §2/§3); (c) a kernel-side PID-1
    affinity fix to replace the inittab line; (d) the intermittent USB teardown
    (trap 337) — capture before the drop;
    (f) re-measure the
    memory throughput (report 263 §4b) — the old figure is single-core.

Step 3 is reversible at any time by re-flashing the same block range — which is
the whole reason it beats an in-place mkfs.

**Closes when:** the unit boots a real rootfs from hd3 unattended, and a trivial
EGL/GLES 2.0 program runs on SGX540.  **Both are met** — Nura boots from the
read-only hd3 (sessions 54/55), the EGL/GLES2 probe reports `PowerVR SGX 540`
(session 55, report 245), and **catacomb now composites on it** (session 56,
report 246).  Session 59 added a persistent, writable root with SSH (report
249), fixed the touch position ghost (report 248), closed the eMMC-cache item
by measurement and fixed the OSK repeat with wvkbd + touch-wake (report 250).
The remaining work (upload-path performance) is tracked under step 5 and does
not reopen the GPU result.

## 8. A BB10 image on the PlayBook — **PARKED (operator, session 25)**

The full analysis and the built set are in
[`docs/parked/BB10-PORT.md`](../parked/BB10-PORT.md) and
[`docs/parked/BB10-IMAGE-GUIDE.md`](../parked/BB10-IMAGE-GUIDE.md); the
session-by-session record is in
[`docs/history/ROADMAP-CLOSED.md`](../history/ROADMAP-CLOSED.md).  Reopen only
with the operator.

## 9. The device's own sensors and peripherals — **active**

**Goal.**  Bring up the board's sensors and peripherals under Linux before any
`hd3` repurpose.  Report 176 is the inventory and the bus map.

**Done.**  I²C4 motion sensors (BMA150/HMC5883L/MPU3050), TMP103, BQ27541,
FAN5702 and CM3217; the Munin touch is calibrated and has its GPIO2_3 edge IRQ
(report 198); the ISL9519 charger and the 58.6 s WDT2 policy are in the
known-good image; **the WM8994 audio plays stereo at 48 kHz on headphones and
both speakers** (sessions 46/47, reports 214/215 — the codec is on McBSP3, the
card is two ASoC links, the ABE detour is closed, and the speakers are the
codec's line outputs with `spk` active-low and GPIO1_15 as the `audio_boost`
enable; history in
[`docs/history/ROADMAP-CLOSED.md`](../history/ROADMAP-CLOSED.md)).  Session 48
finished the audio: the `pcm:`/`live N:` diagnostics are out of the machine
driver and `omap-mcbsp`, the modules were rebuilt, region 1 was flashed on its
own (the region-0 CRC is unchanged — trap 190), and both outputs were
re-verified at DAC1 volume 40 (speakers: `jack out`, `spk` low, `boost` high;
headphones: `jack in`, `spk` high; both PCMs `RUNNING` and the full byte count
written).  The re-export is **55 files / 8 500 lines** and repairs a patch
defect as well — `sound/soc/ti/omap-mcbsp-priv.h` had been missing from every
export since session 41 (trap 211).

**Bench access — done (session 48).**  The ACM console is a debug channel at
best: 134 KB/s and it drops bytes under load (trap 51).  The gadget now carries
an **RNDIS** function beside ACM, the unit serves **dropbear** on `usb0`
(`169.254.0.1:22`, key-only), and `tools/pbssh.sh` reaches it at a measured
**2.3 MB/s** — commands *and* interactive sessions, with the console staying
bound as the recovery path.  RNDIS rather than CDC-ECM (Windows in-box support,
and it is what the reference presents); dropbear rather than OpenSSH (fit on this
BusyBox root).  An agent's shell has no tty, so it cannot exercise an
interactive session itself — test those under `script -qec` or from a real
terminal (trap 216).

**Open.**

* **The eMMC WRITE-busy stall (the "cache flush" item, closed as a flush).**
  Session 59 measured `CACHE_SIZE = 0` and `CACHE_CTRL = 0` (CMD8 read + kernel
  debugfs), so the MMC core never issues `FLUSH_CACHE` — no change is needed.
  The session-58 error is a plain WRITE (`flags 0x900000` =
  `REQ_BACKGROUND|REQ_ALLOC_CACHE`, op `0x1`, no `PREFLUSH`/`FUA`), not a flush;
  a traced `sync` showed no CMD6 and did not stall.  The intermittent
  busy-after-WRITE cause is open; `barrier=0` + write-through keep the root
  usable.  [report 250 §1, trap 300 corrected]
* **The WM8994 audio has nothing required left**, only two optional items: the
  **MCLK1 probe** (trap 165 — the working design does not need it) and
  identifying the GPIO1_15 `audio_boost` net if a schematic appears.

* **The minimal 1917 oracle is built and has been used** (session 43/44, reports
  207/208/209).  A debloated 2.1.0.1917 IFS+RCFS — services reduced to
  `filesystem pps random pty net powerman phyctrl usbmgr rimusb`, GUI/app/font/
  browser/DRM subtrees pruned (live `/base` 90.4 MiB) — boots in ~30 s with
  `qconn` on `169.254.0.1:9090` and root `sshd`; built by
  `tools/build_minimal_qnx.py`.  **It settled the MCLK1 model:** AUXCLK1 IS the
  codec's MCLK1 (gate off + codec reset + FLL power-up → no lock; restore →
  lock), and on Linux MCLK1 is measurably **absent** with identical
  AUXCLK1/pad/stream-time FLL file (codec freeze detector, validated on the
  reference).  **The power-domain/CM comparison is done and negative** (report
  209): every power *state* matches, the only difference is the
  `LASTPOWERSTATEENTERED` debug latch, and forcing Linux's CM policies on the
  working oracle leaves MCLK1 present (report 209).  **ABE path fixed:**
  `CM_ABE_PLL_REF_CLKSEL` is SYS_CLK with DPLL_ABE locked at 98.304 MHz.  **The
  reference's codec-GPIO init is implemented** (report 211): a boot-on
  `audio-ldo` on GPIO4_31 pulsed 100 ms at probe, spk high, hp input.  It does
  **not** restore MCLK1 (`FLL1 NOT locked`) but it fixes the WM8994 probe (-121
  without it).  **Session 47** used the oracle again for the speaker:
  `Headphone Select` = false routes it to the line outputs and the speaker
  sounds, which is how the missing board lines were found (report 215).
* **WL1283 WLAN — WORKING: it associates, leases and passes traffic (session
  52, report 236).**  The association blocker was the **missing `ccm`/`ctr`
  crypto modules**: `ieee80211_key_alloc()` allocates a `ccm(aes)` AEAD for
  every CCMP key, so without them the PTK install returned `-ENOENT` — which
  wpa_supplicant reports as `WRONG_KEY`, even though the 4-way handshake was
  completing all along (message 3 decrypts).  They ship in region 1 and `rcS`
  loads them before the stack.  Measured: `wpa_state=COMPLETED`, WPA2-PSK/CCMP,
  5785 MHz, −41 dBm, MCS7/65 Mbit/s, DHCP **192.168.0.212**, traffic to the
  gateway and to 1.1.1.1.  A 5 GHz AP needs `iw reg set <CC>` (the world domain
  marks 5735–5835 MHz `PASSIVE-SCAN`), and `disable_scan_offload=1` plus a
  `wpa_cli scan` nudge makes the bring-up reliable.  The **per-unit NVS** is
  shipped.  Session 51 had already fixed stability (CAM, report 232) and the
  scan (the 4-sr firmware, report 235).  **Wi-Fi is at its hardware ceiling**
  (1×1 HT20, 72 Mbit/s; TX 2.9 / RX 4.4 MB/s) — advertising HT40 makes it
  collapse to MCS0, so it stays off (trap 247).  **Open, none blocking use:**
  *why* 5-sr panics on scan (235 §3), *why* the ELP wake is not honoured
  (232 §6), and the occasional first-auth timeout.
* **WL1283 BT — the CORE WORKS; the kernel's controller open is the next step
  (session 53, report 238).**  Over a plain H4 tty the core answers
  `HCI_Reset`, reports `hci ver 6` / `lmp ver 6` / `mfr 0x002b` (TI, BT 4.0) and
  **runs a full BR/EDR inquiry that finds a peer**
  (`f0:a3:b2:bb:d1:00`, class `0x011c00`), reproducibly.  The reference's
  `WL1283.bts` replays **315/315** and moves the LMP subversion
  `0x2b0f` → `0x2b33`.  Loaded **before** the H4 attach it makes `hci0`
  **register with mgmt for the first time** (0 → 1 controller, with the
  no-pack case as the negative control), by fixing the
  `LE_Read_Supported_States` (`0x201c`) reply length.
  **Open:** opening the controller.  A stray `0x30` byte arrives ~200 ms after
  the last init reply (found with an instrumented `h4_recv()`, 6 of 6 runs) and
  the kernel's H4 receiver correctly rejects it as not-an-H4-type (`-EILSEQ`,
  `Frame reassembly failed (-84)`), so `SET_POWERED` returns `0x05`
  (`-ETIMEDOUT`).  It never appears from userspace, so **which side emits it** is
  the next experiment (trap 250).  Then ship
  `l2cap`/`sco`/`rfcomm`/`hidp` (modules; none in region 1 yet) and a stack.
  **LE is refused by the controller**, but the chip reports BT 4.0 and the
  LE-supported feature bit, so an enable sequence may exist even though the
  reference stack never had one.
  Tools in `rootfs/bt/`.  **Closes when:** `hci0` powers up and a scan through
  the stack finds a device; the operator's fuller goal (LE, HID) is recorded in
  report 238 §7 and is a separate question.
  **FM:** mainline carried `drivers/media/radio/wl128x` from 2.6.39 to 6.13 and
  removed it; bringing it back is a forward port.  **GPS:** no mainline driver;
  a 2024 LKML patch is the candidate, the reference's own path the fallback.
* **Touch — closed (session 58).**  The release, the fast-tap merge and the
  position ghost are fixed (kernels `#70`/`#73`/`#74`); typing is correct.  The
  remaining backspace/enter repeat is squeekboard's action-key repeat (trap
  299), not the touch driver.  The history is in
  [`docs/history/ROADMAP-CLOSED.md`](../history/ROADMAP-CLOSED.md),
  `## Touch — closed session 58`.  The one remaining nicety is driving
  power/reset (GPIO1_14 / GPIO4_8) for cold boots — a warm reset keeps the
  controller up (session 42).

## Out of scope for now

IVA-HD, camera and the secure-world (TrustZone) path are still disabled or
untouched in the board DTS.  They are not blocked on the panel.  Audio, touch,
the sensors, WiFi/BT/GPS and **SGX/GPU** are **no longer out of scope**: the
peripherals are item 9 (report 176), and the GPU is item 7a (operator, session
53, with the hd3 rootfs).

## Working rules that apply to every item

* One variable per boot; never mix a timing experiment with a structural change.
* `fb <rrggbb>` before judging the panel; `kmsg` before theorising.
* Read `BOOTCTR_FCTRY` as a health check; the booted OS tops it up (Linux
  `/init`, TabletOS `startup.sh`).  Leave the unit on the image the operator
  wants; no restore is required.
* **Everything that writes** — RPMB, NVRAM, the eMMC, the media region, the panel
  chain — is governed by `SAFETY.md`.  Short version: writable eMMC for **our
  own** data is allowed (session 18); NVRAM and the MCT are never written; RPMB
  is the **operator's** `0x32` top-up only (an agent never writes it); never
  `SYS_RST`, never `VAUX1`.
* **Look up every external fact; do not trust recalled knowledge.**  The
  assistant's built-in knowledge is frozen at its training date and this project
  spans months of upstream movement: kernel versions, pmaports package contents,
  `arch=` lists and driver internals all drift.  On 2026-11-07 a session
  asserted that `7.3.3` "doesn't match the 6.x series" and was wrong —
  `kernel.org/releases.json` showed 7.3.3 as the current *stable*, with mainline
  at 7.4-rc1.  **Fetch the authoritative source** (`kernel.org/releases.json`,
  the actual pmaports `APKBUILD`, the tree you are about to build) and cite it,
  rather than reasoning from memory.  The same rule applies to register maps:
  prefer a live read or the local TRM over recollection.
