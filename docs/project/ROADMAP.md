# Roadmap

Open work only.  Closed and parked item history is archived in
[`docs/history/ROADMAP-CLOSED.md`](../history/ROADMAP-CLOSED.md).  Each open
item states the evidence that would close it.

Current focus: **item 7a** — the SGX540 is **working** (kernel driver + pvrports
userspace; EGL/GLES 2.0 on `PowerVR SGX 540`, sessions 55, reports 243–245).
What remains is to **bake the stack into hd3** (it was tested from the tmpfs
overlay) and **pick a compositor that runs on this GLES2 driver** — catacomb
needs `GL_EXT_unpack_subimage`, which the driver lacks.  The eMMC is out of the
boot path and `elogind`/`tinydm`/`catacomb` start and get a seat.  hd3 holds the
Nura root and it boots unattended; the region-1 BusyBox root stays the fallback.  The audio goal is
closed (reports 214/215), the bench has **ssh over USB** (RNDIS + dropbear) with
the gadget's DMA now working (session 52), and the **WL1283's WLAN associates and
passes traffic** (on the region-1 root; the Nura root now carries its firmware
and `wlcore` boots it).  On the same chip, **BT's core works** (real inquiry;
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
   hd3 (`f7858110…`); region 0 is `0xB9DD91E3` (kernel `#73`).  **Next:** (a)
   fix the touch position ghost (report 247 §2, HANDOFF §0a); (b) profile the
   upload path — the `PBGL_PROFILE` catacomb build is staged
   (`local/work/nura/pmaports/main/catacomb/`, `pkgrel=10001`).  The measured
   input latency is 8–33 ms, so the jank is redraw/upload.  `epitaph` aborts
   (trap 292), so there is no panel; Kumo needs a WPEWebKit build (report 247
   §4).

Step 3 is reversible at any time by re-flashing the same block range — which is
the whole reason it beats an in-place mkfs.

**Closes when:** the unit boots a real rootfs from hd3 unattended, and a trivial
EGL/GLES 2.0 program runs on SGX540.  **Both are met** — Nura boots from the
read-only hd3 (sessions 54/55), the EGL/GLES2 probe reports `PowerVR SGX 540`
(session 55, report 245), and **catacomb now composites on it** (session 56,
report 246).  The remaining work (apps/content and the upload-path performance)
is tracked under step 5 and does not reopen the GPU result.

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
* **Touch.**  The release/merge bug is fixed (kernel `#73`: falling edge plus a
  10 ms poll while a contact is down); contacts are 36–158 ms and separate.
  **Open:** a fast tap can be reported at the previous contact's coordinates
  (the position ghost — report 247 §2, traps 296/297).  The remaining nicety is
  driving power/reset (GPIO1_14 / GPIO4_8) for cold boots — a warm reset keeps
  the controller up (session 42).

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
