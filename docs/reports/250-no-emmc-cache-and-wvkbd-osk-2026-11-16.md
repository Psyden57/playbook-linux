# 250 — the eMMC has no cache (the active item was a no-op), and the OSK repeat is fixed by installing wvkbd on catacomb 1.5.0

Date: 2026-11-16 (session 59, Linux-era 53).  Bench: the unit boots the Nura
root **read-write** from hd3, region 0 **`0x294F8E36`** (kernel `7.3.3 #77`),
grown to 56.5 G with SSH.  No kernel or catacomb rebuild was needed for either
result in this report.

## 0. Headline

* **The eMMC card has no cache.**  `EXT_CSD.CACHE_SIZE = 0` and
  `EXT_CSD.CACHE_CTRL = 0`, read both directly (CMD8 via `MMC_IOC_CMD`) and
  from the kernel's own cached EXT_CSD in debugfs.  The MMC core therefore
  never issues `FLUSH_CACHE`: `_mmc_cache_enabled()` requires
  `cache_size > 0`.  The handoff's "CACHE_SIZE = 249" was the EXT_CSD **byte
  index** (249), not the value.  The handoff's active item — clear the cache
  capability so no FLUSH is issued — is satisfied already; no change is
  needed.
* **The recorded stall is not a flush.**  The session-58 error is
  `I/O error, dev mmcblk1, sector 0 op 0x1:(WRITE) flags 0x900000` — a plain
  WRITE (`REQ_BACKGROUND|REQ_ALLOC_CACHE`; no `REQ_PREFLUSH`, no `REQ_FUA`,
  `REQ_OP_FLUSH` would be `0x2`).  A controlled `sync` with the MMC request
  trace on completed cleanly with no CMD6 in the trace.  The intermittent
  busy-after-WRITE remains an unexplained card/host behaviour; the existing
  `barrier=0` + write-through mount keeps the FS usable.
* **The backspace/enter repeat is fixed by installing wvkbd** alongside the
  existing catacomb 1.5.0.  Catacomb 1.5.0 already initialises
  `VirtualKeyboardManagerState` (`zwp_virtual_keyboard_v1`, `src/catacomb.rs:303`),
  which is the protocol wvkbd speaks.  One physical BackSpace tap now delivers
  **one** `0x7f` (was three with squeekboard).  No catacomb 1.7.x move, no
  GLES2-shim rebase, no Rust build and no region-0 flash.

## 1. The cache measurement

The card is a Samsung **MCGAFA**, manfid `0x15` (`CID_MANFID_SAMSUNG`),
oemid `0x0100`, product rev `0x10`, serial `0xa81de12b`, 59.4 GiB; its single
quirk is `0x400` = `MMC_QUIRK_SEC_ERASE_TRIM_BROKEN`, **not**
`MMC_QUIRK_BROKEN_CACHE_FLUSH`.

Two independent reads agree:

1. A small `MMC_IOC_CMD` tool (`tools/bench/mmccache.c`, pushed paced to
   `/tmp`) reads EXT_CSD with CMD8 (512 B, ADTC, R1):

   ```
   EXT_CSD CACHE_SIZE   = 0 (bytes 249..252)
   EXT_CSD CACHE_CTRL   = 0x00 (byte 33, volatile)
   EXT_CSD CACHE_FLUSH  = 0x00 (byte 170)
   EXT_CSD REV          = 0x05 (byte 192)
   ```

   The full 512-byte dump has valid data in the eMMC-5 property region and
   zero CACHE_SIZE bytes, so this is not a failed transfer.
2. The kernel's cached copy at
   `/sys/kernel/debug/mmc1/mmc1:0001/ext_csd` gives the same values:
   CACHE_SIZE 0, CACHE_CTRL 0, REV 0x5.

`drivers/mmc/core/mmc.c` gates cache enable on `cache_size > 0` (it writes
CACHE_CTRL=1 at init only then), and `_mmc_cache_enabled()` — the sole gate on
`_mmc_flush_cache()` — is `cache_size > 0 && cache_ctrl & 1`.  With both zero,
no flush command is ever sent.  `mmc_blk_issue_flush()` would call
`mmc_flush_cache()` → the no-op.  Adding a `cache_size = 0` quirk would change
nothing.

**The handoff's premise.**  HANDOFF §0a said "CACHE_SIZE = 249 (read it;
non-zero means the kernel will flush) and CACHE_CTRL = 33 (0 = cache off,
1 = on)".  Those are the EXT_CSD **field indices**: CACHE_SIZE is byte 249,
CACHE_CTRL is byte 33.  mmc-utils was never installed and the read was never
performed; the value 249 was taken for the value.  The measured value is 0.

**What the stall actually is.**  `block/blk-mq.c:883` prints
`flags = req->cmd_flags & ~REQ_OP_MASK`.  In this kernel
`REQ_BACKGROUND = 1<<20` and `REQ_ALLOC_CACHE = 1<<23`, so `0x900000` is a
plain buffered write; `REQ_PREFLUSH` is `1<<18` and `REQ_FUA` `1<<17`
(neither set), and a flush op is `REQ_OP_FLUSH = 0x2`, not `0x1`.  The
session-58 journal abort (`0x800800` = `REQ_SYNC|REQ_ALLOC_CACHE`) is likewise
a write.  With the `omap_hsmmc` `trace` parameter on (`PBREQ` lines), a
controlled `printf … > /root/mmctrace.test; timeout 12 sync` produced only
CMD25 writes (to hd3 sectors) and CMD13 busy polls — no CMD6, no stall.

## 2. The OSK fix: wvkbd on catacomb 1.5.0 (no rebuild)

The operator observed that catacomb 1.5.0 and 1.7.0 differ little in their
changelogs and asked whether wvkbd could simply be installed instead of
moving the whole UI to 1.7.x.  It can.  Primary-source checks:

* **catacomb 1.5.0 already supports wvkbd.**  The cached
  `catacomb-v1.5.0.tar.gz` has `VirtualKeyboardManagerState::new()` at
  `src/catacomb.rs:303` — the standard Wayland `zwp_virtual_keyboard_v1`
  protocol.  Its OSK state control (`VkActions`, `src/catacomb.rs:591/1252`)
  merely executes a program bound to the
  `EnableVirtualKeyboard`/`DisableVirtualKeyboard`/`AutoVirtualKeyboard`
  keysyms; it does not hard-code squeekboard's D-Bus.  The image's initrc used
  `sm.puri.OSK0` D-Bus calls only because that is squeekboard's interface.
* **wvkbd is packaged for this image.**  Alpine edge/community armv7 has
  `wvkbd-0.21-r0` and `wvkbd-mobintl-0.21-r0` (99706 B, sha256
  `7a9be1a3…`, `cmd:wvkbd-mobintl`).  All four runtime libraries
  (`libcairo.so.2`, `libpango-1.0.so.0`, `libpangocairo-1.0.so.0`,
  `libwayland-client.so.0`) were already on the root, so
  `apk add --no-network /tmp/wvkbd.apk` sufficed.  wvkbd shows/hides on
  SIGUSR2/SIGUSR1 and toggles on SIGRTMIN.
* wvkbd's repeat is the compositor's ordinary held-key repeat; it has no
  squeekboard action-key repeat.

**Live test (before baking).**  The operator confirmed the keyboard appeared
on the panel (`Resize 1024x260 1.000000, 55 layouts` in wvkbd's log).  A foot
terminal ran `keylog.py` on its stdin while the operator tapped BackSpace
once:

```
# t0=1662.962150 secs=50.0
2.9148 6c      <- 'l'
3.7327 61      <- 'a'
17.2816 7f     <- ONE BackSpace (was 7f / +194 ms / +234 ms with squeekboard)
```

No further bytes arrived in the remaining ~33 s of the capture.  One tap is
one key.

**Made persistent.**  `postmarketos-ui-catacomb`'s `initrc` now spawns
`wvkbd-mobintl -L 260` instead of `squeekboard`, rebinds the OSK keys to
`pkill -USR2/-USR1 -x wvkbd-mobintl`, and drops the GNOME-only
`gsettings screen-keyboard-enabled` line.  The local pmaport's APKBUILD now
depends on `wvkbd-mobintl` (not `squeekboard`), `pkgrel=2`, with the new initrc
sha512.  After `reboot -f`, wvkbd (PID 1748) came up from the initrc and
squeekboard did not start.

The root is mounted read-write and persistent (DECISIONS §84), so the live
install + initrc edit survive a reboot without an hd3 reflash.  The
flasher-restorable base `f7858110…` still holds squeekboard; reflashing it
would reintroduce the repeat until wvkbd is reinstalled.

## 2a. Two follow-up refinements on the live root

* **Missing glyphs — a font problem.**  The image shipped only Terminus and
  Droid fonts; wvkbd's default `"Sans 14"` resolved to Droid Sans, which lacks
  the OSK's symbols (`⌫` U+232B, `⌨` U+2328, `⇧` U+21E7, `↵` U+21B5, …).
  `font-dejavu` 2.37's `encodings`/`mkfontscale` dependencies are not installed
  and could not be resolved offline, so `DejaVuSans.ttf` + `DejaVuSans-Bold.ttf`
  were copied into `/usr/share/fonts/dejavu/` and `fc-cache -f` run.  wvkbd now
  starts with `--fn "DejaVu Sans 16"`; the operator confirmed the backspace and
  layout/symbol switch icons render.  One combining-mark label (`⌨͔`/`⌨͕`,
  U+0354/U+0355) still renders oddly — cosmetic.  The pmaport APKBUILD now
  depends on `font-dejavu`.
* **Touch-to-wake DPMS (trap 298) — daemon works, but auto-blank is unsafe.**
  `tools/bench/pb-touchwake.py` watches `/dev/input/event0` for `BTN_TOUCH`
  down and runs `catacomb msg dpms on` when `bl_power != 0`; the session user
  was added to the `input` group (gid 23).  A single forced DPMS-off + tap woke
  the panel cleanly.  But a later auto-blank cycle left a **black screen with
  the backlight on**: the DSI link wedged (`Failed to set dsi_if_enable`,
  `NO PACKET_SENT`, `VC(1) error`); catacomb/tabula/wvkbd were all alive, so
  this is the DSI link, not a dead DE — a reboot clears it (traps 289/301).
  Controlled retest: with swayidle's `resume` removed so pb-touchwake was the
  sole DPMS-on caller, cycles 1 and 2 (one forced, one auto-blank) were clean,
  but cycle 3 wedged again.  So both a double-call race *and* repeated DPMS
  resume on this kernel are implicated; the reference OS's robust DSI resume
  is unported.  **Policy: no auto-blank** — swayidle is removed from the
  initrc so the panel stays lit; pb-touchwake remains for a manual
  `catacomb msg dpms off`.  Only a reboot recovers a wedged link.

Both were verified across a second `reboot -f`: wvkbd (with the DejaVu font),
pb-touchwake, swayidle and the `input` group membership all came back.

## 2b. App-drawer icons, the baked image, and the remaining panel crash

* **App drawer (tzompantli) icons were all "NotFound".**  tzompantli starts its
  icon-theme chain at `default` and follows `Inherits`, but
  `/usr/share/icons/default/index.theme` was missing, so it never reached
  `hicolor` where the app icons live.  Created it (`Inherits=Adwaita,hicolor`),
  installed `adwaita-icon-theme` 51.0-r0 + `glycin-svg` 2.2.5-r1, and converted
  netsurf's `/usr/share/pixmaps/netsurf.xpm` to a 128×128 PNG in hicolor.
  `tzompantli` now renders every app icon; the drawer is launched on boot for
  the bench (normally it is bound to a power-button long-press).
* **`epitaph` (the panel) still segfaults** ~0.3 s after start.  It is *not*
  the UPower warning in trap 292 (the zbus `GetAll` on `/` is a `NetworkManager
  AccessPoint` proxy with a null path — a WARN); with `EGL_LOG_LEVEL=debug` the
  crash follows font loading and EGL config selection on the PVR GLES2 driver.
  A fix likely needs a patched Rust rebuild of epitaph (long build) or a
  driver-level change; neither was done this session.  The missing panel is
  why the boot shows only the wallpaper, the drawer on demand, and the OSK.
* **`calls`/`chatty` are not installed** (heavy GTK4/GStreamer recommends
  skipped with `--no-recommends`); their initrc lines only print "not found".
* **A flasher-restorable hd3 image was baked** with all of the above:
  `local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img`
  (625 999 872 B, sha256 `07555bde…`, `e2fsck -fn` clean).  It adds wvkbd +
  DejaVu, pb-touchwake, the icon fixes and the swayidle-free initrc on the
  `f7858110…` base.  It was **not flashed** — the live rw root already has the
  fixes, so the flash is left to the operator; hd3-only at `0x4915`.

## 3. State and counter

Region 0 is unchanged (`0x294F8E36`, kernel `#77`) — no kernel rebuilt, so no
patch re-export and no tree walk were needed.  `BOOTCTR_FCTRY` read `0x2d`
this boot (`pbrpmb: topup: … = 2d000000`, above the `0x1e` threshold, no
write).  `swayidle` was killed after both the pre- and post-reboot boots
(trap 298).

## 4. Next

1. Flash the baked hd3 image (`07555bde…`) at `0x4915` if the operator wants
   media to match the live root (hd3-only; initramfs re-installs SSH and
   re-grows).  Not done this session.
2. Fix `epitaph` (patched Rust rebuild or a working panel) — the reason the
   boot UI is bare; and optionally install `calls`/`chatty`.
3. The upload-path profile (`PBGL_PROFILE` catacomb build) — still the
   remaining performance item; port the reference OS's DSI resume if DPMS is
   wanted back.
4. Keep watching the intermittent WRITE-busy stall; `barrier=0` +
   write-through remain the mitigation.
