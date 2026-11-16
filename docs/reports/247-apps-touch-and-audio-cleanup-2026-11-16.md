# 247 — apps and background, the touch input path, and the audio log cleanup

Date: 2026-11-16 (session 57, Linux-era 51).
Bench: the unit boots the Nura overlay root on **region 0 `0xB9DD91E3`**
(kernel `7.3.3 #73`) and **hd3 `f7858110…`**; `BOOTCTR_FCTRY = 0x25`.
`catacomb` runs on the SGX540, `tabula` draws the wallpaper, `foot`,
`netsurf`, `xournalpp`, `squeekboard` and `swayidle` are installed.  The touch
merge bug is fixed; a position ghost remains.  The audio bring-up diagnostics
are gated and the idle suspend is removed.

## 0. Headline

* **Apps and background exist.**  The `--no-recommends` image had no Catacomb
  companions.  Baked in (offline `apk --root` on a copy of the hd3 image):
  `tabula` 1.0.2, `epitaph` 1.4.0, `tzompantli` 1.2.0, `gorm` 1.2.1,
  `swayidle` 1.9.1, `foot` 1.28.0, `grim` 1.5.0, `slurp` 1.5.0, `tremor`
  1.1.0, `netsurf` 3.11, `xournalpp` 1.3.8.  The wallpaper renders and `foot`
  brings up the OSK (operator saw both; a half-scale `grim` capture showed the
  wallpaper and catacomb's bar).
* **`epitaph` is broken here** — it aborts (rc 139) after UPower's `GetAll` on
  `/` fails, so the panel is absent.  `tabula`/`swayidle`/`foot`/`netsurf`/
  `xournalpp` run.
* **The globe icon killed squeekboard**: tapping it raises `GLib-GIO-ERROR:
  Settings schema 'org.gnome.desktop.input-sources' is not installed`.
  `gsettings-desktop-schemas` 51.0-r0 is now in the image with a recompiled
  `gschemas.compiled`.
* **Idle suspend was resetting the unit.**  The stock initrc runs
  `swayidle timeout 210 "$systemctl suspend"`; `/sys/power/state = "freeze
  mem"`, the panel blanks, USB drops and WDT2 resets the unit ~1 min later
  (`BOOTCTR_FCTRY 0x2b → 0x2a`).  The initrc now has DPMS off + resume and no
  suspend.
* **The audio log flood is gone.**  `omap_mcbsp_dump_reg()` (14 `dev_info`
  lines per stream start) is `dev_dbg` again, the machine driver's per-stream
  `rate … AIF1CLK`/`repair:` lines are `dev_dbg`, and the PM-QoS remove is
  guarded.  After the bake, `dmesg` has 0 matching lines where it had 14+ per
  stream open.
* **The touch merge bug is fixed** (falling edge + poll-on-touch), but a
  **position ghost** remains: later taps are reported at the previous contact's
  coordinates.

## 1. The repeated-key symptom

A single tap repeated the key 5–13 times.  The first evdev capture was
decisive: **one press, one release, 2.79 s apart** — the key was logically
held, so squeekboard's autorepeat did the rest.  Contacts were 250–500 ms even
for "quick" taps.

### 1.1 The controller reports the lifts

With the driver **unbound** and the report register polled every 5 ms
(`muninraw`), five fast double-taps produced **14 contact intervals with
76–118 ms lift gaps** (capture §2), including a triple tap; no multi-slot
samples and no coordinate jumps.  So the chip was not merging anything.

### 1.2 The IRQ only signals the touchdown

The driver's `PBTS` trace (kernel `#71`) shows why: a contact from 69.922 to
70.402 s (480 ms) performed **2 reads** while the chip's report counter
advanced 40 samples (capture §3).  The controller asserts its line for the whole
contact; the lift is a rising edge, and the driver requested falling-edge only.
A first attempt to request both edges made the OMAP GPIO counter stay at 0
(trap 297) — no interrupts at all.

**Fix (kernel `#73`):** keep falling-edge, re-read until the counter parity
matches (4 tries, `usleep_range(200,400)`), and while any contact is down
re-arm the driver's 20 ms poll work with a 10 ms delay.  Measured after:
contacts are 36–158 ms and separate (14 for the burst).

## 2. The remaining position ghost

With `hello` typed slowly, every contact and character is correct:
(615,452)→h, (245,402)→e, (934,451)→l, (937,449)→l, (890,381)→o, with 8–33 ms
latency (capture §4).  A fast burst produced `h e h h e` instead of
`hellohellohello`: the 3rd/4th contacts report the **first key's** position
(616,436)/(632,444), and the 4th moves from `h` to `e` *inside* one contact
(capture §5).

The driver indexes contacts by slot; the chip's `b3` byte alternates `01/02`
even when idle and is not currently used as an identity.  A later fix needs
contact-id tracking (or stale-slot filtering) in `playbook-munin.c`.  This is
the next session's single experiment.

## 3. The audio log flood and the QoS taint

`dmesg` after the app bake was dominated by:

* `omap_mcbsp_dump_reg()`: 14 `dev_info` lines on every stream start.  Upstream
  is `dev_dbg`; session 45 promoted it on purpose (L4_ABE reads fault in user
  mode, trap 158), and it was never gated after the audio work.
* the machine driver's per-`hw_params` `rate … AIF1CLK …` line and the
  `repair: …` lines, also `dev_info`.
* `RX Buffer Underflow!` (`dev_err`, upstream) on playback-only starts.

All the informational ones are `dev_dbg` now, so they are available through
dynamic debug on the bench.  The RX-underflow line stays (one line per stream
start; it is a real condition, not a fault).

`xournalpp` opening the card at 48 kHz also found a real bug: `hw_params`
returns `-EINVAL` (`not enough bandwidth`) **after** recording
`mcbsp->latency[stream]`, so `omap_mcbsp_dai_shutdown()` removes a PM-QoS
request that `prepare()` never added → `WARNING: kernel/power/qos.c:327 …
called for unknown object`, taint `W`.  The remove is now guarded with
`cpu_latency_qos_request_active()`.  The 48 kHz rejection itself is open (the
card is stereo S16; the request that failed was wider, and the clock-divider
state at that point is not yet measured).

## 4. Apps, the schema, and the image

The app set went into a copy of the flashed image with the offline `apk --root`
method (traps 283): the packages and their dependencies, then `e2fsck -fn`
clean.  The audio bake replaced
`/lib/modules/7.3.3/kernel/{snd-soc-omap-mcbsp,snd-soc-playbook-wm8994}.ko`
with the rebuilt, `--strip-debug` modules (vermagic `7.3.3 mod_unload
modversions ARMv6 p2v8`), added `gsettings-desktop-schemas` and recompiled the
GLib schema database with the host's `glib-compile-schemas` 2.90.1 (the root's
glib is 2.90.0), and installed the edited `/etc/catacomb/initrc`.

`tabula` is launched by the initrc with
`-i /usr/share/wallpapers/postmarketos.jpg`; the wallpaper is present in the
image.  A half-scale `grim` capture showed the wallpaper and the empty bar.

Kumo (the operator's preferred browser) is **not packaged**: it links
`libWPEWebKit-2.0` and its README requires the `chrisduerr/WebKit` fork.
Alpine/Nura carry `libwpe`/`wpebackend-fdo` but no `wpewebkit`, so it is a
WebKit-fork build, not an install.  `netsurf` was baked as a light interim
browser; `badwolf` (WebKitGTK 4.1) is the packaged WebKit alternative.

## 5. Suspend

Two resets in the session were the idle action: `<panel dark> → USB gone →
WDT2 reset`.  `/sys/power/state` advertises `freeze mem`, so elogind tries
suspend-to-RAM and the platform does not resume.  `BOOTCTR_FCTRY` `0x2b → 0x2a`
was measured across one of them.  The initrc change removes the suspend and
keeps `timeout 180 "catacomb msg dpms off"`, `resume "catacomb msg dpms on"`.
DPMS off still happens after 3 idle minutes (any touch restores it), so a long
idle bench still blanks the panel but no longer resets the unit.

One reset occurred while `/tmp` held the capture tools; the same edit prevented
a repeat.  On the final boot the counter is `0x25` (started `0x2c`; the
difference is the flash boots plus the two suspend resets).

## 6. Kernel and export

The session produced four region-0 images:

| CRC | size | what |
|---|---|---|
| `0xA544A731` | `0x967CB0` | `#70`, parity re-read (first fix) |
| `0x373674A4` | `0x967AA8` | `#71`, plus the `trace` parameter |
| `0x23D88CBD` | `0x967800` | `#72`, both-edge IRQ (dead end, trap 297) |
| **`0xB9DD91E3`** | **`0x967398`** | **`#73`, poll-on-touch — current** |

The exported patch is now **64 files / 8 862 lines**, sha256 `be2ca8ba…`,
verified by applying to pristine 7.3.3 targets (0 rejects) and comparing
64/64 byte-identical.  The SGX driver remains vendored and is excluded from the
walk (`drivers/gpu/drm/pvrsgx`, 362 files); `scripts/` is excluded as host
tooling.

## 7. Next

1. **The touch position ghost**: enable `trace=1`, type `hello` with the raw
   report capture running, and decode `b3`/slot ownership; then either track
   contacts by id or ignore slots whose coordinates are known stale.
2. **Profile the upload path** (`PBGL_PROFILE` catacomb build is staged): the
   OSK key-press jank and the glitchy top-left cursor are the upload/partial-
   damage path, not input latency (which was measured at 8–34 ms).
3. Kumo/WPEWebKit scope, if the operator wants that browser.
