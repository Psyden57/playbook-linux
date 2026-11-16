# Session 60 (absolute) — the `epitaph` panel fixed: atlas context order + dual-source text draw

> Numbering: absolute `60` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 54** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-16.
Unit state at the end: the Nura root **read-write** from hd3, region 0
**`0x294F8E36`** (`0x9E0980` B, `boot/chainloader/kernel.img` sha256
`f0738625…`, kernel `7.3.3 #77`), grown to 56.5 G with SSH.  Region 1
`49e318bf…` (fallback, not booted).  `PB_LINK=usb tools/pbssh.sh` is the
shell; the ttyGS0 getty is the recovery console.  `BOOTCTR_FCTRY` = `0x27`
(this boot; no reboot this session).  `epitaph` is installed as
`1.4.0-r10006` (draw-phase timing), the panel renders, llvmpipe is in place
for software clients, and the `catacomb` group + udev/polkit rules are
installed.  The CPU is **one core at 400 MHz** (`maxcpus=1`, no OPP/cpufreq).

## Goal of this session

The handoff's active item: fix the `epitaph` panel segfault on the PVR/EGL
stack, then optionally flash the baked hd3 image and build the upload-path
profile.

## Confirmed (measured, read in a file, or seen on the bench)

* **The app receives softpipe, not the SGX.**  The patched binary logs
  `GL renderer "softpipe", version "OpenGL ES 3.1 Mesa 21.3.9 Amber"`.
  `swrast_dri.so` is where the earlier tracer put the fault.  [report 251 §1]
* **GL calls made without a context are no-ops.**  A ctypes probe created a
  texture before `eglMakeCurrent` (name unchanged, no error), then after it got
  `TexSubImage2D → 0x502` — the exact `invalid texture level 0` the app
  logged.  [report 251 §2]
* **The atlas is built before the context.**  Upstream `5696bdf` moved the
  context into the lazy `SizedRenderer`, but `Renderer::new` still constructs
  `GlRasterizer` (and `Atlas::default` → `Texture::new(4096, 4096)`) first.
  [source `src/renderer.rs`, `src/text.rs`; report 251 §2]
* **The SGX540's `GL_MAX_TEXTURE_SIZE` is 2048**, softpipe's is 16384; a
  4096×4096 `TexImage2D` fails on the SGX540.  [report 251 §1]
* **Softpipe advertises `GL_EXT_blend_func_extended` and compiles the text
  shader, but drawing with it segfaults.**  r10004 (atlas fix, original
  shader) crashed; `MESA_EXTENSION_OVERRIDE=-GL_EXT_blend_func_extended` made
  the shader fail to link and the app survive (`RC=143`).  [report 251 §3]
* **The fix works.**  r10005: `timeout 6 epitaph` → `RC=143`; the process stays
  up; `grim` shows `Thu. 1`, `03:20` and the signal/Wi-Fi/battery icons.
  [report 251 §5]
* **The permissions were missing.**  `playbook` was not in `catacomb`, the
  backlight was `0644 root:root`, and the polkit rule was absent, so the
  brightness slider and the Wi-Fi button did nothing (`PermissionDenied`).
  Installed the group, `66-epitaph.rules.udev` and `.polkit`; the backlight is
  now group-writable and `echo $v > brightness` works as `playbook`.
  [report 251 §6]
* **Touch reaches xdg toplevels.**  `uinput` is not in the root but the kernel
  has it as a module; built/loaded it and wrote `tools/bench/uitap.py`.  With a
  fresh fullscreen tzompantli, an injected tap on the Foot icon launched `foot`
  (tzompantli exits on a successful spawn); gorm responds too.  The earlier
  dead taps were a stale/tiny window state, **not** the touch pipeline and not
  wvkbd — a visible wvkbd also accepted a tap in a clean state.  [report 252 §2;
  trap 306/307]
* **Wi-Fi was disabled in NetworkManager.**  `nmcli radio wifi` = `disabled`,
  `wlan0` "unavailable"; `nmcli radio wifi on` gives `disconnected` and
  `nmcli dev wifi list --rescan yes` returns ~10 APs (2.4/5 GHz) while
  `wlcore` boots `Rev 7.3.5.0.98`.  Gorm needs its own `10-gorm.rules`
  (scan/network-control/settings.modify.system/enable-disable-wifi for group
  `catacomb`); with it installed gorm lists the APs.  [report 252 §3; trap 308]
* **The ~2 s interaction latency is the client's softpipe rasterizer, and
  llvmpipe fixes most of it.**  During a 2 s drag epitaph burned **3.16 s CPU**
  vs catacomb's **0.16 s**; the 1024×585 drawer draw measured `render=1.6–2.3 s`
  (compositor import 28–90 ms, swap 66–77 ms).  The root's amber Mesa has no
  llvmpipe; installing Alpine 3.16's Mesa 21.3.9 `swrast_dri.so` (llvmpipe) +
  `libLLVM-13` + the libdrm sub-libraries drops `render` to **2.7–6 ms** and the
  drag CPU to 1.65 s (~4×), with the remaining 420–550 ms in llvmpipe's
  deferred `swap` (and a first-draw LLVM JIT of 1.75 s panel / 4.15 s drawer).
  The compositor still maps `pvr_dri`.  [report 253; traps 309–311]
* **The CPU is one core at 400 MHz, with no cpufreq/OPP.**  The kernel is built
  **`# CONFIG_SMP is not set`** (so `maxcpus=1` is an unknown parameter and the
  DT's two CPUs are capped), and the DTS bootargs also set `maxcpus=1`
  (`kernel/source/omap4-winchester.dts:88`):
  `/sys/devices/system/cpu/online` is `0`, `nproc` is 1; the debugfs
  `clk_summary` shows `dpll_mpu_ck = 400000000` (BogoMIPS 795.44).  There is no
  `/sys/devices/system/cpu/cpu0/cpufreq/` and no `operating-points`/`cpu-supply`
  in the DTS (though `CONFIG_CPU_FREQ=y` and `CONFIG_PM_OPP=y`).  [measured this
  session]
* **Thermal:** `thermal_zone0` `cpu_thermal` 65.0 °C and `thermal_zone1`
  `bq27541-0` 34.5 °C; the governors are registered, but there is no cooling
  device (no cpufreq).  [measured]
* **The reference programs the MPU OPP and voltage.**  Report 223 Q6/§9 decoded
  `CM_CLKMODE_DPLL_MPU (0x4A004160)` idle-bypass→lock,
  `CM_CLKSEL_DPLL_MPU (0x4A00416C)`, and `PRM_VC_* (0x4A307BA0–0xBF8)` (no
  `PRM_VP_*`).  Our boot chain leaves the MPU at 400 MHz.  [report 223; the
  reference's exact rate/voltage was not read live this session]
* **GL clients are software; only the compositor is hardware.**
  `/proc/<catacomb>/maps` has `pvr_dri`; `epitaph`, `tzompantli` and `gorm` all
  depend on `glutin` (EGL/Wayland) and map `swrast_dri` (plus `libLLVM-13`
  after the replacement); `wvkbd` maps cairo/pixman and is responsive.  The
  amber Mesa's Wayland platform cannot select `pvr_dri`, and the
  `sgx-ddk-um-ti443x` package ships `libGLESv2_PVR_MESA` but **no `libEGL`**.
  [Cargo.toml files, package contents, `/proc` maps]
* **The r10003 shader experiment failed on a GLSL ES rule, not on logic:**
  `vec4 tex = texture2D(...)` needs a default precision
  (`No precision specified … vec4`).  Adding `precision mediump float;` makes
  the single-output shader compile and link on softpipe.  [report 251 §4]
* **The build is a qemu-user armv7 build.**  `--no-cross` (crossdirect is
  blocked by the host/chroot rustc mismatch), abuild wipes `$srcdir`, so all
  cargo dependencies are rebuilt each time; zbus/zvariant_derive and resvg
  dominate.  A persistent `CARGO_TARGET_DIR` was added to the local APKBUILD.
  [report 251 §7]

## Inferred (reasoning from evidence, not directly measured)

* **Sampling the undefined texture 0 is the null dereference** at
  `swrast_dri.so+0x3da8d0` (`fault_addr=0`): with no level 0 the image pointer
  is null.  The app's trace agrees (texture name 0, invalid level 0).
* **The dual-source *blend* is unimplemented or broken in softpipe**, although
  the extension is advertised; the shader compiles, but the draw crashes.
  (Strong: disabling the extension changes the outcome; the crash site is
  unchanged.)
* **The next performance lever is the MPU OPP and the second core.**  The
  measured 400 MHz on one core is ~2.5× below the OMAP4430's nominal 1.0 GHz
  and half the cores; the reference's startup writes `DPLL_MPU` and the PRM
  voltage controller (report 223), so the path exists.  (Strong on the 400 MHz
  and one-core measurements; the reference's exact rate/voltage is unmeasured.)
* **llvmpipe's `swap` is its deferred rasterization of the full-surface
  clears.**  The drawer clears ~1.2 M pixels per frame; the panel's 20 k-pixel
  swap is 2–15 ms and the drawer's scales linearly with pixels.
  (Moderate-strong.)

## Dead ends and wrong assumptions

* **"The texture-size mismatch is the crash."**  4096 > the SGX540's 2048 is
  real (and capped now), but this client runs on softpipe (max 16384), so it
  does not explain the segfault.  The context order does.
* **"Replacing the shader fixes it outright."**  The first single-output
  shader omitted `precision mediump float;` and failed to compile; the app
  survived but had no text/icons.  Fixed by adding the precision statement.
* **"Dual-source is supported because the extension is advertised."**
  Advertised and compilable, but the draw crashes.  Confirm by disabling it.
* **"Changing the mouse/keyboard repeat is a driver job."**  Not this session;
  unchanged from trap 299 (wvkbd fixed it).
* **"`maxcpus=1` means 'use both cores'."**  Measured: `/sys/.../online` is
  `0`, `nproc` is 1, and the kernel is built `CONFIG_SMP=n` (the parameter is
  unknown to it); removing the bootarg alone cannot bring up cpu1.
* **"Removing `maxcpus=1` enables the second core."**  It is necessary but not
  sufficient: the kernel needs `CONFIG_SMP=y`, which also changes the module
  vermagic, so every module must be rebuilt.
* **"llvmpipe fixes the sluggishness."**  It fixed the 1.6–2.3 s client draw
  (2.7–6 ms), but the 400 MHz/one-core CPU and the full-surface clears still
  cost ~0.5 s per frame, and GL clients remain software.
* **"The SGX/GPU driver is slow or faulty."**  The compositor composites on the
  SGX (`pvr_dri`); the clients never reach it (the Mesa Wayland platform gives
  them `swrast`), and the reference's native WPE/GTA use the SGX directly.
  The slowness is the software client path plus the low CPU clock.

## Unknown / open questions

* **Does the Wi-Fi/cellular toggle now work through the new polkit rule?**
  The rule is installed and polkitd HUP'd; needs a tap to confirm.
* **What is the reference's MPU OPP and voltage?**  Boot the oracle and read
  `CM_CLKSEL_DPLL_MPU`/`CM_CLKMODE_DPLL_MPU` (0x4A00416C/0x4A004160) and the
  `PRM_VC_*` values at 0x4A307BA0–0xBF8, or use the decoded startup-playbook
  values (report 223).  Settles the target rate/voltage for 1.0 GHz.
* **Does cpu1 come up and stay stable?**  Build `CONFIG_SMP=y`
  (`CONFIG_NR_CPUS=2`), keep `maxcpus=1` for the first boot (proves the SMP
  kernel on one CPU), then remove `maxcpus=1`, build/flash region 0 and boot.
  `CONFIG_SMP` changes the module vermagic, so rebuild and install all modules
  (including the vendored `pvrsrvkm`) before the flash.  Check `online`, dmesg,
  WDT, thermal and a llvmpipe drag; if cpu1 hangs, capture dmesg and revert.
* **Can the MPU DPLL/VC be raised safely under Linux?**  The reference writes
  `PRM_VC_*` and `DPLL_MPU`; a proper OPP table + `cpu-supply` (vdd_mpu) + ABB,
  or a boot-time write with the right VCORE/ABB values.  Needs the reference's
  voltage and a thermal check at 1.0 GHz.
* **Can a Wayland client use the SGX?**  The amber Mesa Wayland platform gives
  `swrast`; the DDK has no `libEGL`.  Candidates: a pvr Wayland EGL platform
  (`wl_drm`/linux-dmabuf), a client that renders via GBM+dmabuf, or an X/Wayland
  bridge.  Not started.
* **Flash the baked hd3 image?**  It still has the old `epitaph` and
  `catacomb`'s initrc starts it; a reflash would lose the live fixes unless it
  is rebaked.  Not done.
* **The panel is `Layer::Bottom`**: fullscreen toplevels cover it.  Not a bug,
  but the reason it is only visible over the wallpaper.

## Superseded / uncertain

* Trap 292 ("epitaph segfaults; fix needs a source patch and Rust rebuild") —
  fixed; the cause and the patch are report 251.
* The report-250 §2b claim that the crash is in the VAO/`glDrawElements` path —
  the atlas and the dual-source draw are the measured causes; the VAO path may
  still be involved but is not what the evidence shows.
* The handoff's "the upload-path profile is the next item" — still true, but
  the panel now renders, which is what makes the drawer usable enough to judge
  it.
* The session-59/60 inference that the ~2 s is the "redraw/upload path" — it
  is the client's **softpipe** rasterizer and the 400 MHz/one-core CPU
  (report 253); the compositor's import is 28–90 ms.
* The implicit "GL clients use the SGX" — only the compositor does; the
  clients are on `swrast`/llvmpipe (trap 314).
* The operator's reading of `maxcpus=1` as "both cores" — measured one core
  (`online=0`, `nproc=1`; trap 313).
* "Removing `maxcpus=1` alone enables cpu1" — the kernel is `CONFIG_SMP=n`; the
  second core needs a kernel rebuild (and the vermagic forces a module rebuild;
  trap 313).

## Artefacts produced

* Report `docs/reports/251-epitaph-panel-fixed-2026-11-16.md`,
  `docs/reports/252-app-layer-touch-tzompantli-gorm-2026-11-16.md` and
  `docs/reports/253-latency-softpipe-llvmpipe-2026-11-16.md`.
* Local pmaports: `main/epitaph/` (APKBUILD `pkgrel=10006`,
  `fix-renderer-init.patch`, sha512 `b149772a…`; APK `epitaph-1.4.0-r10006.apk`,
  sha256 `857cb5b8…`) and `main/catacomb/` (`pkgrel=10001`; profile patch
  `smithay-gles2-profile.patch`, sha512 `c924aa90…`; APK
  `catacomb-1.5.0-r10001.apk`, sha256 `b0097340…`).  `local/work/llvmpipe/`
  holds the Mesa 21.3.9 `mesa-dri-gallium`, `llvm13-libs`, the edge `libdrm` and
  `payload.tar.gz` (sha256 `820d7ece…`) pushed to the unit.
* `uinput.ko` built from `local/kernel-tree` (sha256 `7c99e8bf…`); loaded only,
  not part of the image.  The DTS `maxcpus=1` (`omap4-winchester.dts:88`) is
  unchanged — it is the next experiment.  No region-0/1 flash and no kernel
  patch change this session.
* Probe scripts under `local/work/src/`: `glprobe.py` (EGL/GL limits and the
  pre-context texture test), `glprobe2.py`, `glshader.py`, `glshader2.py`,
  `wlprobe.py` (the wl_surface attempt; its own musl crash is a ctypes issue,
  not a device result).  `tools/bench/uitap.py` (tracked) — a `uinput`
  touchscreen tap.
* The live root: `epitaph` r10006, group `catacomb` (gid 111) with `playbook`,
  `/etc/udev/rules.d/66-epitaph.rules`,
  `/etc/polkit-1/rules.d/66-epitaph.rules`, `10-gorm.rules`, backlight
  `root:catacomb` `-rw-rw-r--`, `uinput.ko` loaded (taints the kernel), and the
  llvmpipe install (`/usr/lib/xorg/modules/dri/swrast_dri.so` from Alpine
  21.3.9 gallium, `swrast_dri.so.amber` backup, `/usr/lib/libLLVM-13.so`,
  `libdrm_amdgpu.so.1`/`libdrm_nouveau.so.2`).  `catacomb` is 1.5.0-r10001
  (the env-gated `PBGL_PROFILE` build) and `PBGL_PROFILE=1` is staged in
  `/usr/share/tinydm/env-wayland.d/61-pbgl-profile.sh` (remove it for normal
  use).
* NetworkManager: `nmcli radio wifi on`; `wlan0` scans (not connected).  Gorm
  and tzompantli were launched from SSH; no initrc change yet.
* Traps: 292 fixed; 303 (softpipe advertises but crashes on dual-source
  drawing), 304 (GL before a context is a silent no-op), 305 (epitaph's
  udev/polkit rules), 306 (uinput build/load and the `UI_SET_*` value form),
  307 (stale/tiny xdg window state), 308 (NetworkManager radio off; gorm's
  own rule), 309 (no llvmpipe; the Alpine 21.3.9 replacement), 310 (llvmpipe's
  JIT warm-up and deferred swap) and 311 (the profile's `KiB/s` wrap) added.
* No kernel, DTB or initramfs change → no export, no tree walk, no region-0
  flash.  (`uinput.ko` is a module build only, not part of the image.)
