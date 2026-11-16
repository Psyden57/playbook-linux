# Session 56 (absolute) — catacomb on the SGX540, the touch Y fix, and the vendored SGX driver

> Numbering: absolute `56` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 50** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-15.
Unit state at the end: **on the read-only hd3 + overlayfs Nura root**, region 0
**`0xAB899C49`** (kernel `7.3.3 #69`, the Munin Y fix) and hd3 **`89399744…`**
(the SGX-compositor image: catacomb 1.5.0-r10000 with the smithay GLES2
fallback).  **`catacomb` runs on the PowerVR SGX 540**; `squeekboard` is
composited; the touch cursor tracks the finger.  `BOOTCTR_FCTRY` = `0x2c`;
console `playbook:~#` on the ACM; `usb0` down (bring it up by hand); no sshd.

## Goal of this session

The handoff's active item: a compositor that works on the SGX540 GLES2 driver.
The operator chose **catacomb** (the Nura default, lightest) and to patch
smithay rather than switch to Phosh/Sxmo.

## Confirmed (measured, read in a file, or seen on the bench)

* **The blocker is smithay, not the GPU.**  Catacomb 1.5.0 pins smithay 0.7.0
  (`dba255cd…`); `GlesRenderer::new()` rejects GLES2 without
  `GL_EXT_unpack_subimage`, which the SGX540 lacks, so catacomb panics at
  `src/udev.rs:400`.  `libGLESv2_PVR_MESA`, `libsrv_um`, `libusc`, `pvr_dri.so`
  are all mapped; there is no `swrast`.
* **The smithay fallback works.**  Relax the check and implement the upload
  fallbacks: full upload tight → one `TexImage2D`; padded → per-row; partial
  damage tight → whole damaged rows (one call per rect); padded → per-row;
  `update_memory` → full-texture-layout rows.  The tinydm log shows
  `GL_EXT_unpack_subimage not supported, using per-row uploads`, and catacomb
  composites the OSK and the touch cursor.
* **The touch events are real and reach catacomb.**  `/dev/input/event0`:
  `ABS_MT_POSITION_X/Y` (type 3, codes 53/54), `ABS_MT_TRACKING_ID`, `BTN_TOUCH`;
  udev tags `ID_INPUT_TOUCHSCREEN=1`; catacomb has `event0` open; elogind is a
  non-exclusive second reader.  The earlier "touch does nothing" was the OSK
  not being hit by the **mirrored** coordinate.
* **The Y axis is inverted.**  A controlled top/bottom edge capture:
  physical top → evdev `Y 577–591`, physical bottom → `Y 35–51`.  The driver
  now reports `panel_h - 1 - munin_map(cal_y, yr, panel_h - 1)`; the bench
  cursor follows the finger exactly (top→top, bottom→bottom).
* **The build and flashes.**  Catacomb is built in the pmbootstrap edge chroot
  (Alpine `testing/catacomb` package + the smithay patch in `prepare()`), with
  `--no-cross --no-ccache` (native rustc 1.100.0 vs armv7 1.99.1; no sccache in
  the QEMU buildroot), `pkgrel=10000`, `!check`.  Region 0 + hd3 were written
  in one bootrom session (`COMPLETE 0x40C0 -> cmd 0x4006`, `done`); the
  optimized v2 was a hd3-only flash.  Boot `7.3.3 #69`; catacomb md5
  `7418d9ea…`; the DSI HS-clock packets `irqstatus=0x4 polls=1`, no VC errors
  on a clean boot.
* **The export is re-done.**  `0001-playbook-winchester-7.3.3.patch` is
  **64 files / 8 800 lines**, sha256 `014eb8a8…`, 0 rejects / 64/64 identical;
  the **362-file SGX driver is vendored** as
  `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` (sha256 `a9d04d3c…`,
  362/362 identical after extraction).  The old patch's two generated
  `scripts/mod/*.h` are removed.

## Inferred (reasoning from evidence, not directly measured)

* **The key-press jank is the OSK's redraw path, not the GPU failing.**
  Strong: the compositor renders the cursor smoothly enough, and the jank
  appears only on key taps; the v2 full-row optimization did not remove it, so
  the damage/upload is probably padded-stride or dominated by the per-upload
  `glFinish` (no `GL_OES_EGL_sync`).  Needs a stride/damage instrument.
* **Restarting the compositor on a wedged DSI is not recoverable from
  userspace.**  Strong: stopping tinydm left the screen black; the restart
  produced `VC busy`/`if_enable` errors; a clean boot works.  Treat a display
  restart as a reboot.

## Dead ends and wrong assumptions

* **"Patch phoc/Phosh instead."**  pvrports' phoc is 0.52; current edge is
  0.58 with greetd and a different wlroots (0.20.x).  Pairing them is a
  version mismatch; the operator chose catacomb, and the smithay patch is
  smaller and version-matched.
* **"Report 198's Y orientation still holds."**  It recorded `Y=0` at the top;
  on this display pipeline the hardware is inverted.  The bench measurement is
  authoritative.
* **"The host can serve the APK over the RNDIS link."**  The host UFW drops
  inbound TCP; the transfer was done in the other direction (device `nc`
  listener, host push) at ~10 MB/s.
* **"A compositor restart after a failed session is safe."**  It wedged the DSI
  (`NO PACKET_SENT`, `VC(1) error`); a reboot clears it.
* **"The old 63-file export was clean."**  The tree walk found two generated
  `scripts/mod/*.h` in it and the three SGX integration files missing.

## Unknown / open questions

* **Where does the OSK key-press time go?**  Would settle it: log the shm
  buffer stride and the damage rects in the fallback; check whether
  `EGL_KHR_fence_sync` is available so `glFinish` can go.
* **Do apps/background change the picture?**  The UI is empty (`tabula` and
  the other recommends are missing); a paint app/browser is the next test.

## Superseded / uncertain

* **The session-55 "catacomb cannot use it (GL_EXT_unpack_subimage)"** — with
  the smithay patch catacomb does use it.
* **Report 198 §1's "no inversion"** — see above; the current hardware/display
  needs the Y inversion.
* **The session-55 handoff's "the patch re-export is pending"** — it is done
  this session (64 files, vendored driver).

## Artefacts produced

* `docs/reports/246-sgx540-catacomb-and-touch-2026-11-15.md`.
* Region 0 `local/images/kernel-0xAB899C49-touch.img` (sha256 `c37ee2fe…`);
  hd3 `local/work/nura/images/blackberry-playbook-root-compositor2.img`
  (`89399744…`); container `both-0xAB899C49-compositor.signed` (`426ed972…`).
* `kernel/patches/0001-playbook-winchester-7.3.3.patch` (64 files / 8 800
  lines, sha256 `014eb8a8…`); `kernel/vendor/pvrsgx-…tar.xz` (`a9d04d3c…`);
  `kernel/vendor/README.md`.
* `local/work/nura/pmaports/main/catacomb/` (APKBUILD + smithay patch);
  `local/work/nura/work/packages/edge/armv7/catacomb-1.5.0-r10000.apk`.
* Traps 285–291; `DECISIONS.md` §79/§80; `kernel/patches/README.md`.
