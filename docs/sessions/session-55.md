# Session 55 (absolute) — the overlay root flashed and booted: eMMC out of the boot path, the Nura DE starts, the GPU is the last wall

> Numbering: absolute `55` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 49** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-15.
Unit state at the end: **on the read-only hd3 + overlayfs Nura root**, region 0
**`0xA8F0F30C`** (kernel `7.3.3 #68`, `&sgx_module` enabled) and hd3
**`1fcf0cb0…`** (the overlay image + `pvrsrvkm_omap4_sgx540_120.ko`), both
written in one bootrom session.  The lower is mounted `MS_RDONLY`+`noload` on a
`LO_FLAGS_READ_ONLY` loop with a 256 MiB tmpfs upper; **no eMMC stall, no
journal abort**.  `elogind`, `tinydm` and `catacomb` run; `/run/user/10000` and
the catacomb Wayland socket exist; the Munin touch is ready; the backlight is
255; `wlcore` boots the WLAN firmware.  The **SGX driver is loaded and bound**
(`pvrsrvkm 56000000.gpu`, `card1`/`renderD129`); the userspace is not installed.
`BOOTCTR_FCTRY` is `0x31`.  Console `playbook:~#` on the ACM (`/dev/ttyACM0`),
no sshd; `usb0` DOWN.  Region 1 `49e318bf…` untouched.

## Goal of this session

The handoff's one experiment (ROADMAP item 7a, report 242): flash the packed
both-halves container in one bootrom session, boot the overlay root, and — the
operator's next choice — bring up **SGX540**.

## Confirmed (measured, read in a file, or seen on the bench)

* **The packed container parsed before flashing.**  `bbusb.py parse` shows the
  V1 runs at **16** (151 blocks, region 0) and **16640** (9552 blocks, hd3), the
  V2 at `0xA0`, and `signature: crc 0x493FA440 OK`; 635 961 936 B.
* **The flash succeeded in one bootrom session.**  From the booted Nura root
  with `bbusb.py flash --signed … --from-bootrom --password 0000 --apply`, then
  `reboot -f`: `Connected in bootrom`, `Model ID: 0x06001A06`, the ramloader
  upload to `0x80100000`, both runs, `COMPLETE 0x40C0 -> cmd 0x4006`, `done`,
  `loader exit: device released (attempt 1)`.  The unit re-enumerated
  `0fca:8010` and booted.
* **The overlay root booted.**  `uname` `Linux playbook 7.3.3 #67`;
  `overlay / overlay rw,relatime,lowerdir=/rootfs,upperdir=/overlay/upper,workdir=/overlay/work,uuid=on`;
  `EXT4-fs (loop0): mounted filesystem aed244ce-… ro without journal`.
* **No eMMC stall.**  `dmesg` has no `Card stuck being busy!`, no `Aborting
  journal`, no `Remounting filesystem read-only`; the writes went to the tmpfs
  upper.
* **The counter self-replenished.**  `pbrpmb: topup: BOOTCTR_FCTRY … = 1d000000`,
  `writing 0x1d -> 0x32 (RPMB counter 11995)`, `PASS - BOOTCTR_FCTRY = 0x32`
  (the image's own boot-time top-up, `SAFETY.md` §2 — not an agent action).
* **The session stack works.**  `rc-status`: `dbus`, `elogind`, `tinydm`,
  `udev-postmount`, `networkmanager`, `wpa_supplicant`, `bluetooth`,
  `iio-sensor-proxy`, `sleep-inhibitor`, `localmount`, `root`, `fsck` all
  started.  `/run/user/10000` holds `bus`, `catacomb-wayland-1.sock`,
  `wayland-1`, `openrc`; `pgrep` finds `catacomb` (1588), `elogind` (1272) and
  `squeekboard` (1684).
* **The peripherals came up.**  `playbook-munin … Munin touchscreen ready (irq
  110)`, `/dev/input/event0`; `/sys/class/backlight/backlight/brightness` =
  255; `wlcore: firmware booted (Rev 7.3.5.0.98)` (twice), `wlan0` UP; 33
  modules loaded of the 43.
* **The DE's limiter is the renderer.**  `~/.local/state/tinydm.log`:
  `Wayland socket: wayland-1`; `Using output scale: 1 (PPI 169.83)`;
  `Loading global config at "/etc/catacomb/initrc"`;
  `smithay::backend::drm::device::fd: Unable to become drm master, assuming
  unprivileged mode`; **`MESA: error: ZINK: failed to load libvulkan.so.1`**;
  `failed to create signaled syncobj … ENOTSUP`; `swayidle`/`tabula`/`epitaph`/
  `chatty` not found.  `/usr/lib/dri` has `swrast_dri.so`, `kms_swrast_dri.so`,
  `zink_dri.so` and no GPU driver; `/dev/dri/{card0,renderD128}` exist;
  `card0-LVDS-1` is `connected` at `1024x600`; nothing binds the GPU.
* **`tools/pbcon.py` does not drive the Nura shell.**  Its prompt marker is
  hardcoded `pb> ` and the Nura prompt is `playbook:~#`; it reported "no prompt"
  and sent nothing.  A one-line raw send/read helper drove the shell (and the
  `reboot -f`).
* **No source change.**  The kernel patch, config, initramfs and images are
  session 54's; `boot/chainloader/kernel.img` is the `0xE85217E5` image.

## Inferred (reasoning from evidence, not directly measured)

* **The overlay root removes the eMMC from the boot path for good on this
  design.**  Strong: the lower is read-only at the loop and the fs and the
  dmesg has no stall; the same media previously stalled on the first journal
  write.
* **catacomb would draw if it had a GL renderer.**  Medium-strong: the
  compositor gets the output, loads its config and starts the keyboard; only
  the `eglInitialize`/Zink path fails.  The black screen + bar is the empty
  panel, not a crashed session.
* **A software renderer is a cheap way to a drawing DE before SGX.**  Medium:
  the overlay root is writable (tmpfs upper), Mesa is installed, only
  `libvulkan`/lavapipe (for Zink) or a `kms_swrast`/llvmpipe override is
  missing.  Untested.

## Dead ends and wrong assumptions

* **"`tools/pbcon.py` works on the Nura shell."**  It does not: the marker is
  `pb> `.  The session-54 handoff repeated this; trap 274 corrects it.
* **"The prompt is `playbook:~#` in the ttyGS0 shell."**  It is, but the
  session-54 text also implied `pbcon`; the tool mismatch is the real issue.
* **"A black screen means the root/seat failed."**  No: `/run/user/10000` and
  catacomb exist; the renderer is the failure.
* **"`--from-bootrom` needs the unit in the ramloader."**  It uploads the
  ramloader and needs the unit booted (trap 266); confirmed again.

## Unknown / open questions

* **Does SGX540 give catacomb a usable renderer on 7.3.3?**  Would settle it:
  rebase `linux-openpvrsgx` to 7.3.3, enable `CONFIG_SGX`/`SGX_OMAP`/`SGX_DRM`,
  install `sgx-ddk-um` `ti443x`, and run a trivial EGL/GLES 2.0 program before
  any compositor.
* **Does a software renderer (lavapipe/Zink or `kms_swrast`) draw the DE?**
  Would settle it: add `libvulkan`/lavapipe or set a Mesa loader override on
  the overlay and restart `tinydm`.
* **Is the `Unable to become drm master` warning material?**  catacomb still
  sets the mode; check whether it is a consequence of the unprivileged session
  or a second DRM master.
* **WLAN association on the Nura root.**  The firmware boots and `wlan0` is UP
  but unconfigured; NetworkManager is running — a separate test.

## Superseded / uncertain

* **The session-54 STATE/HANDOFF/report 242 "the flash did not run / next is the
  flash".**  It ran and succeeded this session; report 243, STATE, ROADMAP and
  HANDOFF are current.
* **Report 241 §4's "tinydm fails on the read-only root, no /run/user/10000".**
  The overlay root fixes it: the seat exists and `catacomb` runs.
* **The session-54 note's "pbcon.py works at a prompt".**  Corrected by trap
  274 (the Nura prompt is not `pb>`).

## SGX540 — the kernel half (same session, after the overlay result)

The operator chose SGX540 next.  **The driver builds and binds the GPU; the
userspace is the remaining piece.**  [report 244]

### Confirmed

* **The source is the active project, not the old fork.**  The driver is
  `openpvrsgx-devgroup/linux_openpvrsgx`, branch **`pvrsrvkm-7.3`**, HEAD
  `101c687a…`, which already carries DDK **1.17.4948957** and selects
  `SOC=omap4 SGX=sgx540 SGX_REV=120`.  The pvrports APKBUILD's
  `tmlind/linux_openpvrsgx` (6.1) was not used.
* **The driver builds against 7.3.3.**  `drivers/gpu/drm/pvrsgx/1.17.4948957`
  (358 files, 6.3 MB) plus `pvr-drv.c/h`, its Kconfig sourced from
  `drivers/gpu/drm/Kconfig`, `obj-$(CONFIG_SGX) += pvrsgx/`; config
  `SGX=y`, `PVRSGX_1_17_4948957=y`, `SGX_DRM=y`, `SGX_OMAP=m`.  One kernel
  export was required: `EXPORT_SYMBOL(__vmalloc_node_range_noprof);` in
  `mm/vmalloc.c` (modpost otherwise fails for every variant).
* **The DTB has the GPU enabled.**  `&sgx_module { status = "okay"; }`; the
  `omap4.dtsi` node supplies `gpu@0` (`ti,omap4430-gpu` /
  `img,powervr-sgx540`, IRQ `GIC_SPI 21`, 32 MB).
* **The image and the module.**  zImage `6 880 640` B, DTB `96 461` B, image
  `0x967880` B, CRC **`0xA8F0F30C`**.  `pvrsrvkm_omap4_sgx540_120.ko`, stripped
  `--strip-debug` to 347 KB, vermagic `7.3.3 mod_unload modversions ARMv6 p2v8`,
  written flat into the hd3 image.
* **The flash and the probe.**  The both-halves container `3be2c73a…`
  (CRC `0x9CD674DA`) flashed in one bootrom session (`COMPLETE`, `done`); boot
  `7.3.3 #68`.  `insmod` gives:
  ```
  pvrsrvkm 56000000.gpu: Enabling quirks 00000001
  [drm] Initialized pvr 1.17.4948957 for 56000000.gpu on minor 1
  ```
  `/dev/dri/card1` + `renderD129` exist; `uevent` → `DRIVER=pvrsrvkm`,
  `/ocp/target-module@56000000/gpu@0`.  catacomb stayed up.
* **The counter is `0x31`.**  The second boot read `0x31` and `/init` correctly
  wrote nothing (`0x31 >= 0x1e`); trap 278.

### Inferred

* **The kernel↔GPU handshake is ready for userspace.**  Medium-strong: the
  driver mapped the SGX registers, powered the domain and registered a DRM
  device; only the DDK's `SrvInit` (and then the Mesa pvr backend) is missing.
* **The userspace is the whole remaining blocker.**  Strong: the root has no
  `gcompat`, no glibc loader and no `pvr_dri.so`; the DDK blobs are glibc/armhf.

### Dead ends and wrong assumptions

* **"The pvrports `linux-openpvrsgx` APKBUILD is the route."**  It pins the old
  `tmlind` 6.1 fork; the devgroup has a current 7.3 branch.  (Operator's
  correction.)
* **"The driver will compile unchanged."**  It compiles, but modpost rejects
  `__vmalloc_node_range_noprof`; the devgroup branch also adds the export.
* **"`BOOTCTR_FCTRY` is 0x32 after every boot."**  It tops up only below `0x1e`;
  the second boot left `0x31`.
* **"The vendor `pvrsrvctl` can be hand-staged under gcompat."**  It cannot:
  with `gcompat` 1.1.0 (+ `musl-obstack`, `libucontext`) installed and the
  `ti443x` libs copied in, the vendor glibc binary **segfaults even on
  `--help`** (`rc=139`), before any service init.  The userland changes were in
  the tmpfs upper (volatile); the userspace must be built the pvrports way.

### Unknown

* **Does the DDK userspace initialize the GPU on musl with gcompat?**  Settle
  by staging `gcompat` + the blobs + `pvrsrvinit`.
* **Does `mesa-pvr-dri-classic` build in the Nura pmaports chroot?**  Settle by
  building it; then a trivial EGL/GLES 2.0 test.
* **Kernel patch packaging** — the driver adds 358 files to a 63-file patch.

### Artefacts

* `docs/reports/244-sgx540-kernel-bringup-2026-11-15.md`.
* `local/kernel-tree/drivers/gpu/drm/pvrsgx/` (the driver);
  `drivers/gpu/drm/{Kconfig,Makefile}`, `mm/vmalloc.c`, the board DTS,
  `kernel/winchester-7.3.3.config` (the SGX block + the FB helpers).
* `local/images/kernel-0xA8F0F30C-sgx.img`;
  `local/work/nura/images/blackberry-playbook-root-overlay-sgx.img` (`1fcf0cb0…`);
  `both-0xA8F0F30C-sgx.signed` (`3be2c73a…`).
* Traps 275–279; DECISIONS §77.

## SGX540 userspace — the GPU works (same session)

**The pvrports stack on the unit initializes the SGX540 and runs EGL/GLES 2.0.**
[report 245]

### Confirmed

* **The prebuilt stack installs on the edge root.**  From
  `https://pvrports.antonialoytorrens.com/pvrports/v23.12/armv7/`:
  `libglvnd` 1.6.0-r2, `mesa-pvr-dri-classic` 21.3.9-r1, `sgx-ddk-um`
  1.17.4948957-r1 (+ `-ti443x`, `-openrc`).  Plus Alpine `gcompat` 1.1.0-r4
  (+ `musl-obstack`, `libucontext`) and `wayland-libs-server` 1.26.0-r0.
  `apk` replaces mainline mesa 26.2.6 coherently (it purges `mesa`,
  `mesa-{egl,gl,gles,dri-gallium,gbm}`, `llvm23-libs`, `spirv-tools`).
* **The source build also works.**  `pmbootstrap build --arch armv7
  mesa-pvr-dri-classic` (and `libglvnd`) succeeded in the edge chroot — run
  under **`pkexec`**, since the host has no passwordless sudo; the packages are
  in `local/work/nura/work/packages/edge/armv7/`.
* **The DDK handshake.**  `pvrsrvinit` returns 0 and dmesg has
  `PVR_K: UM DDK-(4948957) and KM DDK-(4948957) match. [ OK ]`.
* **EGL/GLES 2.0 on the GPU.**  `local/work/egltest.py` (Python ctypes,
  surfaceless platform `0x31DD`, 16×16 pbuffer) with
  `MESA_LOADER_DRIVER_OVERRIDE=pvr` prints
  `EGL vendor: Mesa Project (Amber)`, `GL_VENDOR: Imagination Technologies`,
  **`GL_RENDERER: PowerVR SGX 540`**, `GL_VERSION: OpenGL ES 2.0 build
  1.17@4948957`.  This meets ROADMAP 7a's EGL/GLES test.
* **catacomb cannot use it.**  With the pvr override in
  `/usr/share/tinydm/env-wayland.d/60-pvr.sh` and `rc-service tinydm restart`,
  catacomb reaches the pvr renderer and panics:
  `create renderer: GLExtensionNotSupported(["GL_EXT_unpack_subimage"])`
  (`src/udev.rs:400`).  pvrports supports Phosh (patched phoc) and Plasma.
* **The stack is baked into hd3.**  The nine APKs were installed **offline**
  into the loop-mounted image with the x86_64 static `apk`
  (`--root … --arch armv7 --allow-untrusted --no-network --no-scripts
  --repositories-file /dev/null --repository <local repo>`), then
  `depmod -b`, `/etc/modules`, the runlevel link and the tinydm env; e2fsck 0;
  image **`85b44deb…`**.  hd3-only flash (`--component --dest-block 0x4915`,
  626 065 440 B, `COMPLETE`/`done`).  On a fresh boot the module autoloads,
  `sgx-ddk-um` starts, dmesg shows `UM/KM DDK match`, and the EGL probe still
  reports `PowerVR SGX 540`.

### Inferred

* **The GPU is done; the DE is a compositor-choice problem.**  Strong: EGL/GLES
  works offscreen; the only failure is an extension check in smithay.
* **Baking the stack into hd3 will survive a boot.**  The nine APKs are
  self-contained and the module/service wiring is known; untested.

### Dead ends and wrong assumptions

* **"Hand-stage gcompat + the blobs."**  `pvrsrvctl` segfaults on `--help`
  (trap 279); the packages are the route.
* **"es2_info is a quick headless test."**  It insists on an X display; use the
  ctypes surfaceless probe.
* **"catacomb will draw once the GPU works."**  It requires
  `GL_EXT_unpack_subimage`, which this GLES2 driver lacks.

### Unknown

* **Which compositor?**  Phosh/phoc (pvrports-proven), Plasma, or a wlroots DE.
* **Kernel patch packaging** (358 driver files).

### Artefacts

* `docs/reports/245-sgx540-userspace-egl-2026-11-15.md`.
* `local/work/egltest.py`; `local/work/apks-pvr/` (and its `repo/armv7`).
* `local/work/nura/images/blackberry-playbook-root-pvr.img` (`85b44deb…`) —
  the baked image now on hd3.
* Traps 280–284; DECISIONS §78.

## Artefacts produced

* `docs/reports/243-overlay-boot-nura-de-2026-11-15.md` — the overlay flash log,
  the boot verification and the renderer finding.
* `docs/reports/244-sgx540-kernel-bringup-2026-11-15.md` — the SGX driver
  integration and the probe.
* `docs/reports/245-sgx540-userspace-egl-2026-11-15.md` — the pvr userspace,
  the EGL/GLES 2.0 result and the catacomb limitation.
* `docs/project/STATE.md`, `HANDOFF.md`, `ROADMAP.md`, `KNOWN-ISSUES.md`
  (traps 272–284), `DECISIONS.md` (§76 executed, §77, §78),
  `docs/reports/INDEX.md`, `docs/sessions/NUMBERING.md`.
* `/tmp/f.log`, `/tmp/f2.log`, `/tmp/f3.log` (host, volatile) — the three
  flasher logs (overlay, SGX, hd3-only bake).
* Region 0 `local/images/kernel-0xA8F0F30C-sgx.img`; hd3
  `local/work/nura/images/blackberry-playbook-root-pvr.img` (`85b44deb…`, the
  baked pvr image now on media); the pre-pvr images remain on the host.
