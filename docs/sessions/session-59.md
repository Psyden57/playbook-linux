# Session 59 (absolute) — the eMMC cache item closed by measurement; the OSK repeat fixed by installing wvkbd

> Numbering: absolute `59` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 53** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-16.
Unit state at the end: the Nura root **read-write** from hd3, region 0
**`0x294F8E36`** (`0x9E0980` B, `boot/chainloader/kernel.img` sha256
`f0738625…`, kernel `7.3.3 #77`), grown to 56.5 G with SSH.  Region 1
`49e318bf…` (fallback, not booted).  `PB_LINK=usb tools/pbssh.sh` is the
shell; the ttyGS0 getty is the recovery console.  `BOOTCTR_FCTRY` = `0x2d` at
the last boot (top-up threshold `0x1e`, no write needed).  The root now has
wvkbd 0.21 installed and catacomb's initrc starts it instead of squeekboard.

## Goal of this session

The handoff's active item: clear the eMMC cache capability so no FLUSH is
issued.  After measuring that the card has no cache, the operator redirected
to the next item: fix the OSK repeat by installing wvkbd rather than moving
the whole UI to catacomb 1.7.x.

## Confirmed (measured, read in a file, or seen on the bench)

* **The eMMC has no cache.**  `CACHE_SIZE = 0`, `CACHE_CTRL = 0`, read by a
  new `MMC_IOC_CMD` tool and from `/sys/kernel/debug/mmc1/mmc1:0001/ext_csd`.
  Card: Samsung MCGAFA, manfid `0x15`, quirk `0x400`
  (`SEC_ERASE_TRIM_BROKEN`), not `BROKEN_CACHE_FLUSH`.  The handoff's "CACHE_SIZE
  = 249" was the byte **index**, not the value.  [report 250 §1]
* **The stall is a plain WRITE, not a flush.**  `flags 0x900000` =
  `REQ_BACKGROUND|REQ_ALLOC_CACHE`; no `REQ_PREFLUSH`/`REQ_FUA`, op `0x1` not
  `0x2`.  A controlled `sync` with `omap_hsmmc` trace on showed only CMD25/CMD13
  and no CMD6, and did not stall.  [report 250 §1]
* **catacomb 1.5.0 already speaks wvkbd's protocol.**
  `VirtualKeyboardManagerState::new()` at `src/catacomb.rs:303` is
  `zwp_virtual_keyboard_v1`; its OSK toggle just runs a program (`VkActions`).
  The initrc's `sm.puri.OSK0` D-Bus calls were squeekboard-specific, not
  catacomb requirements.  [cached `catacomb-v1.5.0.tar.gz`; report 250 §2]
* **wvkbd 0.21 is in Alpine edge/community armv7** and all four runtime
  libraries were already present; `apk add --no-network` worked offline.
  [report 250 §2]
* **One BackSpace tap now delivers one `0x7f`.**  With the new keyboard visible
  (operator confirmed on the panel) and `keylog.py` on a foot terminal's stdin,
  a single tap produced one `0x7f` at +17.28 s and no repeats in the remaining
  ~33 s (squeekboard produced three: +0/+194/+234 ms).  [report 250 §2]
* **The fix survives a reboot.**  After `reboot -f`, wvkbd (PID 1748) started
  from the edited `/etc/catacomb/initrc` and squeekboard did not start.
  [report 250 §2]
* **Missing OSK glyphs were a font problem.**  The image had no DejaVu font, so
  wvkbd's default `"Sans 14"` fell back to Droid Sans without `⌫`/`⌨`/`⇧`/`↵`.
  DejaVuSans.ttf (+ Bold) were installed and `--fn "DejaVu Sans 16"` set; the
  operator confirmed the backspace and layout-switch icons now render (one
  combining-mark label is still cosmetic).  [report 250 §2a]
* **Touch wakes the DPMS-off panel now.**  `pb-touchwake.py` watches
  `/dev/input/event0` for `BTN_TOUCH` down and calls `catacomb msg dpms on`
  when `bl_power != 0`; the session user joined the `input` group.  Forced
  `bl_power=4` + one tap returned it to `0`.  Installed at
  `/usr/bin/pb-touchwake`, started by the initrc.  [report 250 §2a]
* **The whole stack survives a second reboot.**  wvkbd (DejaVu), pb-touchwake,
  swayidle and the `input` group all came back.  [report 250 §2a]
* **DPMS retest corrected the policy: no auto-blank.**  A forced blank + tap
  woke cleanly, but a later auto-blank left a black panel with the backlight on
  (DSI link wedged — `NO PACKET_SENT`/`VC error`; catacomb/tabula/wvkbd alive,
  only `reboot -f` clears it).  With swayidle's `resume` removed (single
  pb-touchwake caller), cycles 1 and 2 were clean but cycle 3 wedged again.
  Swayidle was removed from the initrc so the panel stays lit.  [report 250 §2a]
* **App-drawer icons fixed.**  `/usr/share/icons/default/index.theme` was
  missing, so tzompantli never reached `hicolor`; created it, installed
  `adwaita-icon-theme` + `glycin-svg`, converted netsurf's XPM.  All app icons
  render now.  [report 250 §2b]
* **`epitaph` still segfaults** ~0.3 s after start, after font/EGL setup on the
  PVR GLES2 driver (the zbus `GetAll` warning is benign).  Fix needs a Rust
  rebuild or a working panel.  `calls`/`chatty` not installed (heavy recommends).
  [report 250 §2b]
* **A flasher-restorable hd3 image was baked** with all fixes:
  `blackberry-playbook-root-osk-wvkbd.img` (`07555bde…`, 625 999 872 B,
  e2fsck clean) — not flashed.  [report 250 §2b]
* Region 0, the kernel patch and the DTB are **unchanged** — no export needed.

## Inferred (reasoning from evidence, not directly measured)

* **The intermittent stall is a card/host busy-after-WRITE behaviour**, not a
  cache flush.  (Strong for "not a flush"; the busy cause is open.)
* **wvkbd's ordinary held-key repeat will follow catacomb's 200 ms/25 ms seat
  repeat**, but the squeekboard action-key repeat was a different mechanism;
  the one-tap test settles the observed bug.  (Moderate.)

## Dead ends and wrong assumptions

* **"CACHE_SIZE = 249; clear the cache to stop the flush."**  The number was
  the EXT_CSD byte index; the value is 0.  Measurement: two independent
  EXT_CSD reads.
* **"The sector-0 error is a flush because a flush has no data."**  It is op
  `0x1` (WRITE) with no PREFLUSH/FUA flags; the revised block error print
  proves it.  Measurement: `block/blk-mq.c:883` + `blk_types.h`.
* **"Moving the OSK to wvkbd requires catacomb 1.7.x and a GLES2-shim rebase."**
  False: catacomb 1.5.0 already implements `zwp_virtual_keyboard_v1`.
  Measurement: the cached 1.5.0 source, plus the live install and tap test.

## Unknown / open questions

* **What makes the card busy after an intermittent WRITE?**  Not measured;
  mitigated by `barrier=0` + write-through.
* **Does wvkbd's `--auto` work with catacomb's input-method implementation?**
  Not tried; the image starts wvkbd always-visible like the old squeekboard.
* **The upload-path profile** (`PBGL_PROFILE`) is still unbuilt.
* **Flash the baked hd3 image?**  Baked as `07555bde…` but not flashed; the
  live persistent root already has the fixes.
* **Fix `epitaph`'s segfault** — a patched Rust rebuild (long build) or a
  replacement panel; the reason the boot UI is bare.
* **Port the reference OS's DSI resume** if DPMS auto-blank is ever wanted back.

## Superseded / uncertain

* The handoff's "clear the eMMC cache" as the active item — closed by
  measurement, no change needed.
* The plan to move to catacomb 1.7.x for the OSK — replaced by the wvkbd
  install on 1.5.0.
* The session-58 claim that the stall is a `FLUSH_CACHE` (report 249 §3,
  trap 300) — corrected by report 250 §1.

## Artefacts produced

* Report `docs/reports/250-no-emmc-cache-and-wvkbd-osk-2026-11-16.md`.
* `tools/bench/mmccache.c` — the device-side `MMC_IOC_CMD` EXT_CSD reader.
* Edits to `local/work/nura/pmaports/main/postmarketos-ui-catacomb/{initrc,APKBUILD}`
  (wvkbd instead of squeekboard; `pkgrel=2`; new sha512) — gitignored `local/`.
* The live root has `wvkbd-mobintl` 0.21 installed and the edited initrc.
* Traps: 300 corrected, 299 fixed by wvkbd.  DECISIONS §85.
