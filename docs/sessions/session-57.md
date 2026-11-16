# Session 57 (absolute) — apps and background, the touch input path, and the audio log cleanup

> Numbering: absolute `57` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 51** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-16.
Unit state at the end: **on the read-only hd3 + overlayfs Nura root**, region 0
**`0xB9DD91E3`** (kernel `7.3.3 #73`, the touch poll-on-touch fix) and hd3
**`f7858110…`** (apps + audio modules + `gsettings-desktop-schemas` + the
no-suspend initrc).  `catacomb` + `tabula` + `squeekboard` run; `BOOTCTR_FCTRY`
= `0x25`; console `playbook:~#` on the ACM; `usb0` up by hand; no sshd.

## Goal of this session

The handoff's active item: apps and background for real testing, then profile
the upload path.  The session spent its second half on the keyboard repeat/
jank, which turned out to be a touch-input bug, not the compositor.

## Confirmed (measured, read in a file, or seen on the bench)

* **The app set installs and the wallpaper renders.**  `tabula` 1.0.2,
  `epitaph` 1.4.0, `tzompantli` 1.2.0, `gorm` 1.2.1, `swayidle` 1.9.1, `foot`
  1.28.0, `grim` 1.5.0, `slurp` 1.5.0, `tremor` 1.1.0, `netsurf` 3.11 and
  `xournalpp` 1.3.8 are in the image (`apk --root`, `e2fsck -fn` clean);
  `pgrep` showed `catacomb`, `tabula` and `squeekboard` running, and the
  operator saw the wallpaper, a terminal and xournalpp.  A half-scale `grim`
  capture showed the postmarketOS wallpaper and catacomb's bar.  [report 247
  §4; image `f7858110…`]
* **`epitaph` aborts.**  It logs `Started Epitaph`, a zbus
  `GetAll … Object does not exist at path "/"` warning, then `Segmentation
  fault` (rc 139), reproduced from the session environment.  [trap 292]
* **The globe icon kills squeekboard.**  `GLib-GIO-ERROR: Settings schema
  'org.gnome.desktop.input-sources' is not installed` at 00:08:43 in
  `tinydm.log`; the package is missing from the `--no-recommends` root.
  `gsettings-desktop-schemas` 51.0-r0 and a recompiled `gschemas.compiled`
  (contains `input-sources`) are now in the image, with the root's glib
  2.90.0 and the host compiler 2.90.1.  [report 247 §4]
* **The idle suspend resets the unit.**  `/sys/power/state = "freeze mem"`;
  `swayidle timeout 210 "loginctl suspend"` blanks the panel, drops USB and
  WDT2-resets the unit about a minute later; `BOOTCTR_FCTRY` went `0x2b → 0x2a`
  across one such reset.  The initrc's idle block is now
  `timeout 180 "catacomb msg dpms off"` + `resume "catacomb msg dpms on"`, no
  suspend.  [trap 295; `/etc/catacomb/initrc` on `f7858110…`]
* **The audio log flood is the drivers' diagnostics.**  `omap_mcbsp_dump_reg()`
  was 14 `dev_info` lines per stream start (upstream `dev_dbg`, promoted in
  session 45 for the bench), plus the machine driver's per-`hw_params`
  `rate … AIF1CLK` and `repair:` lines.  After the bake, `dmesg | grep -cE
  "McBSP255|WM8994 clocks"` is **0** (it was 14+ per stream open).  The two
  rebuilt modules load with vermagic `7.3.3 mod_unload modversions ARMv6
  p2v8`.  [report 247 §3; trap 293]
* **A failed `hw_params` tainted the kernel.**  `xournalpp` at 48 kHz:
  `omap_mcbsp_dai_hw_params: not enough bandwidth` (`-EINVAL`), then
  `omap_mcbsp_dai_shutdown()` removed an inactive PM-QoS request →
  `WARNING: kernel/power/qos.c:327 … Unknown object`, taint `W`.  Guarded with
  `cpu_latency_qos_request_active()`.  [trap 294]
* **The touch releases were lost, and the controller was not at fault.**  With
  the driver unbound and the report register polled every 5 ms, five fast
  double-taps gave **14 contacts with 76–118 ms lift gaps**, 0 multi-slot
  samples, 0 coordinate jumps.  [capture
  `touch-munin-2026-11-16.txt` §2]
* **The IRQ only signalled the touchdown.**  The `PBTS` trace (kernel `#71`)
  shows a 480 ms contact with **2 reads** while the chip's counter advanced 40
  samples; the line is asserted for the whole contact and the lift is a rising
  edge.  [capture §3; report 247 §1.2]
* **Both-edge IRQ is a dead end here.**  `IRQF_TRIGGER_FALLING |
  IRQF_TRIGGER_RISING` left `/proc/interrupts` for `1-0065` at **0** while the
  driver still logged "Munin touchscreen ready"; touch died completely.
  [trap 297]
* **The merge is fixed by falling edge + poll-on-touch.**  Contacts are now
  36–158 ms and separate (14 for the burst); a slow `hello` produced
  h,e,l,l,o with 8–33 ms end-to-end latency (contact down → byte at the app).
  [capture §4; kernel `#73`]
* **A position ghost remains.**  A fast `hello` produced h,e,h,h(+e): the
  3rd/4th contacts report the first key's coordinates, and the 4th moves from h
  to e inside one contact.  [capture §5; trap 296]
* **The kernel export.**  64 files / 8 862 lines, sha256 `be2ca8ba…`, applied
  to pristine 7.3.3 with 0 rejects and compared 64/64 byte-identical.  The SGX
  driver is excluded as vendored.  [kernel/patches/README.md]
* **Kumo is not a package.**  Alpine/Nura carry `libwpe`/`wpebackend-fdo` but no
  `wpewebkit`; Kumo's README requires the `chrisduerr/WebKit` fork.
  [report 247 §4]

## Inferred (reasoning from evidence, not directly measured)

* **The position ghost is stale slot data in the controller's four-contact
  report.**  Strong: the same positions recur for later taps, one contact moves
  between two known key positions, and the driver indexes contacts by slot
  while `b3` alternates 01/02 even idle.  Not proven: we have not decoded `b3`
  as a contact id yet.
* **The glitchy top-left cursor is the upload/partial-damage path.**  Moderate:
  it is a visual redraw artefact, and input latency is 8–33 ms; the `PBGL`
  instrumentation is staged to measure it.
* **The 48 kHz `hw_params` failure is a format/divider issue, not a broken
  path.**  Moderate: the machine driver printed 48 kHz/1.536 MHz BCLK just
  before the CPU DAI rejected the request, and `pbplay`'s own 48 kHz path still
  works.
* **The idle ~174 IRQ/s storm is the chip reporting with no contact.**
  Observed; its CPU cost was not measured.

## Dead ends and wrong assumptions

* **"Both edges will catch the lift."**  The lift is a rising edge, but the OMAP
  GPIO irqchip delivered *neither* edge for a both-edge request; the IRQ counter
  stayed at 0 and touch died (trap 297).  The measurement is the counter, not
  the request's return code.
* **"Zero idle evdev events means the driver is dropping every read."**  Wrong:
  the input core suppresses duplicate `SYN_REPORT`s, so a driver that reads
  every sample but reports no change emits nothing.  This nearly sent the
  session down a parity-only fix.
* **"The parity re-read alone fixes the merge."**  It did not: kernel `#70` kept
  merging; the trace showed the missed release edge, not torn reads, as the
  cause.  The parity re-read is still useful (6.3 % of polled reads are torn)
  and stayed in.
* **"`epitaph` just needs the session environment."**  It segfaults with the
  correct `XDG_RUNTIME_DIR`/`WAYLAND_DISPLAY` too.
* **"Kumo can be installed from the Alpine/Nura repositories."**  There is no
  `wpewebkit` package for armv7; Kumo needs the WebKit fork built.
* **"A WDT reset is the only way the unit drops USB."**  The idle suspend looks
  identical from the host (USB gone, panel dark) and is the actual cause twice
  in this session; check `/sys/power/state` and the initrc before blaming WDT.
* **"The physical taps were fast, so the 250 ms contacts are the driver's."**
  The operator's deliberate taps measured ~250 ms; when asked for fast taps the
  driver did report 36–158 ms contacts after the fix.  Do not infer physical
  timing from the contact length without a control.

## Unknown / open questions

* **What is `b3`, and how does the controller assign slots?**  Settle by
  enabling `trace=1`, recording the raw reports for a slow h,e,l,l,o, and
  correlating `b3`/slot/coordinates with the intended key; then implement id
  tracking or stale-slot filtering.
* **Why was the OSK's second `hello` only e,l?**  The capture has two ~250 ms
  contacts 7 ms apart at e and l; the physical taps cannot be 7 ms apart, so
  one of them is a ghost.  Same investigation as above.
* **What request does `xournalpp` make at 48 kHz?**  Settle by logging the
  `hw_params` format/channels and the DAI's `in_freq`/`clk_div` at the failure.
* **Does the TX upload path dominate the key-press redraw?**  Build the
  `PBGL_PROFILE` catacomb and log stride/damage/bytes per import.
* **Can this platform suspend/resume at all?**  `mem` is advertised but does
  not resume; a suspend implementation is a separate project.
* **Kumo/WPEWebKit build scope** (WebKit fork, hours, 32-bit ARM).

## Superseded / uncertain

* **Report 246 §7's "check `EGL_KHR_fence_sync` vs the per-upload `glFinish`".**
  Measured this session: `EGL_KHR_fence_sync` **is** present, `GL_OES_EGL_sync`
  is not, and `EGLContext::new()` is **not shared**, so the upload path does
  not call `glFinish` at all.  smithay's `Capability::Fencing` uses
  `glFenceSync` (GL_OES_EGL_sync), so it cannot be enabled on this driver; the
  upload cost is the `TexSubImage2D` calls themselves.
* **The session-56 handoff's "OSK key presses jank" as an upload-only
  problem.**  The upload/redraw is still suspect for the visual jank, but the
  repeated keys were the lost release (this session); the input latency itself
  is 8–33 ms.
* **The session-56 handoff's "region 0 `0xAB899C49` / hd3 `89399744…`".**
  Current is `0xB9DD91E3` / `f7858110…`.

## Artefacts produced

* `docs/reports/247-apps-touch-and-audio-cleanup-2026-11-16.md`.
* `docs/captures/touch-munin-2026-11-16.txt` (evdev/raw/trace evidence).
* `docs/sessions/session-57.md` (this file).
* Region 0 images: `#70` `0xA544A731`, `#71` `0x373674A4`, `#72` `0x23D88CBD`,
  **`#73` `0xB9DD91E3`** (`0x967398` B, `boot/chainloader/kernel.img` sha256
  `5ac47496…`); the flashed image is `#73`.
* hd3 `local/work/nura/images/blackberry-playbook-root-apps-audio.img`
  (625 999 872 B, sha256 `f7858110…`); its base apps image is
  `blackberry-playbook-root-apps.img` (`711cd5ec…`).
* `kernel/patches/0001-playbook-winchester-7.3.3.patch` (64 files / 8 862
  lines, sha256 `be2ca8ba…`).
* `local/work/modules-7.3.3/{snd-soc-omap-mcbsp,snd-soc-playbook-wm8994}.ko`
  (rebuilt, `--strip-debug`; 37 980 / 16 228 B).
* `local/work/nura/pmaports/main/postmarketos-ui-catacomb/initrc` (no suspend,
  `pkgrel=1`), `local/work/nura/pmaports/main/catacomb/smithay-gles2-profile.patch`
  and the APKBUILD `pkgrel=10001` (the `PBGL_PROFILE` build is staged, not
  built).
* Traps 292–297; decisions §81/§82.
