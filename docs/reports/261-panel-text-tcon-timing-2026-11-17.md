# 261 — why the panel is black on a wedge, and why an early TCON initcall was not enough

Date: 2026-11-17 (session 64, Linux-era 58).  Bench + static RE.

## 0. Headline

* The **blank screen with a blinking cursor** on the current `loglevel=0` images
  is explained by the bootarg, not by hardware: `loglevel=0` suppresses kernel
  text, and `fbcon.c:1113` suppresses the Tux logo whenever
  `console_loglevel <= CONFIG_CONSOLE_LOGLEVEL_QUIET` (this tree: **4**).  fbcon
  is still alive (`Console: switching to colour frame buffer device 128x37`),
  hence the cursor.
* A **loud** dual-core image (`earlyprintk keep_bootcon ignore_loglevel`, no
  `nosmp`) still showed a black panel when it wedged.  The reason is that
  `panel_tcon_init()` lives in `/init` (`initramfs/init.c:770`); a wedge at
  `execve("/init")` means `/init` never runs, the panel's Hitachi TCON is never
  programmed, and the panel stays dark even though the kernel has already
  printed text into the framebuffer.  The backlight is lit throughout
  (operator-confirmed), so it is not the cause.
* A **loud single-core** image (same kernel + `nosmp`) booted and the operator
  saw the console text and Tux on the panel — confirming both points above.
* Moving the TCON send into a `late_initcall` was **not sufficient**: with the
  corrected device name (`spi3.0`, modalias `spi:dh2228fv`) it still wedged with
  a black panel (0/1 + later 0/3).  The TCON table must be sent **after** the
  DSI/bridge is programmed and the HS clock is running — the reference sends it
  at `winch_lcdctl 0x193c4`, immediately after bridge programming (`0x1938c`).
  At `late_initcall` (~0.42 s) the DSI/bridge is not up yet, so the panel has no
  pixel data even with a programmed TCON.

## 1. The loglevel/logo mechanism (verified)

* `drivers/video/fbdev/core/fbcon.c:1113`:
  ```c
  if (logo_shown < 0 && console_loglevel <= CONSOLE_LOGLEVEL_QUIET)
          logo_shown = FBCON_LOGO_DONTSHOW;
  ```
* This tree has `CONFIG_CONSOLE_LOGLEVEL_QUIET=4`, `CONFIG_LOGO=y` and all three
  Linux logo variants in every config (old and new) — so the logo difference is
  **not** a config change.
* Current SMP images: DTB bootargs `loglevel=0 ...` → `console_loglevel = 0`,
  which is `<= 4` → logo suppressed, and no console text.
  Read-back image: `keep_bootcon ignore_loglevel ...` → loglevel 7 (> 4) → logo
  and text shown.  Pre-session-60 images: `earlyprintk ... ignore_loglevel ...`
  → same.
* So the correlation with SMP images is really with the **quiet bootarg**, which
  was introduced in session 62 to fit the loader's 7.812 s WDT2 window when
  decompression took 6.6 s (report 255 §4b/trap 325).  At 1008 MHz kernel entry
  is 3.276 s with ~4.5 s spare (report 257), so the quiet boot is no longer
  required — hence this experiment.

## 2. The images built and what they showed

All use the **known-good** IPI-marker zImage carved from
`kernel-ipidbg-cpu1.img` (`0xF8264016`) — i.e. without the failed SMC-0x108
experiment (report 260) — plus the fast chainloader.

| image | CRC | bootargs | outcome |
|---|---|---|---|
| `kernel-ipidloud-cpu1.img` | `0xC39AB4D1` | `earlyprintk keep_bootcon ignore_loglevel …` (no `nosmp`) | dual-core; **wedged**, panel black |
| `kernel-ipidloud-nosmp.img` | `0xC64F5898` | same + `nosmp` | booted to OS; operator saw **text + Tux** |
| `kernel-earlytcon-cpu1.img` (1st) | `0xA8518DC9` | loud, no `nosmp`; TCON from `late_initcall` looking up `spidev3.0` | booted once (`online=0-1`); dmesg `PBTCON: spidev3.0 not found` at 0.422 s → TCON **not** sent |
| `kernel-earlytcon-cpu1.img` (2nd) | `0xE6355C8F` | same, lookup `spi3.0` | **0/3**, panel black on each wedge |

The first early-TCON image's one success is consistent with the 1/3 baseline
(timing noise), not a fix.  The "spi3.0 not found" line proves the first lookup
name was wrong; the second run's three wedges with a black panel show that even
with the right name the timing is wrong (see §3).

## 3. Why the early TCON initcall failed — the correct hook

* The panel needs **three** things: backlight (lit from the bootloader's LM3532
  state — `panel_vdd` is `regulator-always-on`/`boot-on`), the TC358764 bridge
  programmed over DSI, and the Hitachi TCON table over MCSPI4.
* In our kernel the bridge is programmed by `pb_bridge_program()` in
  `drivers/gpu/drm/omapdrm/dss/dsi.c:4212`, inside `dsi_init_dsi()` — after the
  DSI PHY/PLL and HS clock, before the VC/video setup.  That is the exact
  reference point (`winch_lcdctl 0x1938c` then the TCON table at `0x193c4`).
  `tc358764_pre_enable()` deliberately does nothing on winchester
  (`of_machine_is_compatible("blackberry,omap4-winchester")` → `ctx->inited =
  true; return`) because the DSI host programs the bridge.
* A generic `late_initcall` at 0.42 s runs before that DSI init, so the panel has
  no pixel data; the TCON table alone cannot make text appear.  Sending it from
  `dsi.c` immediately after `pb_bridge_program()` is the right idea, but the
  SPI client device (`spi3.0`) was not registered at 0.42 s — whether it is
  registered by the time DSI init runs needs checking.  If not, the options are
  a deferred probe/work coordinated with the display enable, or raw McSPI4
  register writes at that point.
* This is a **diagnostic aid only** — the wedge itself is still localised by
  the ring to the post-IPI `execve` path (report 258).

## 3a. The DSI-init hook works — the wedge is a visible kernel panic

A third approach succeeded.  `dsi.c` now schedules a short retrying delayed
work (`pb_tcon_work`) from the `pb_bridge_program()` call site (after the DSI
PHY/PLL, bridge programming and HS clock) which waits for the `spi3.0` client
(~1.97 s, versus DSI init ~1.7 s) and then sends the 9-word table.  Built into
`kernel-dsitcon-cpu1.img` (CRC **`0x445D5D60`**, loud dual-core bootargs).

**Result: the operator saw the console on the panel and a kernel panic** on the
wedged boots (three wedges, no `0fca:8010`).  So the panel-diagnostic goal is
achieved, and the earlier "clean `execve` hang" reading (report 258) needs
revising — there is an actual panic, with a visible message/stack.  The exact
panic text is pending the operator's screenshots / a ring capture.

The experiment is retained in the gitignored tree (`dsi.c` `pb_tcon_*`, report
261 marker) because it is a useful diagnostic instrument; it must be removed
before any kernel-patch re-export like the other `PB*` instruments.

## 4. Bench

Region 0 restored to `kernel-fastcl-nosmp.img` (`0x8B83421F`) after the
experiments.  The experimental `playbook-tcon-early.c` and its Makefile line
were removed from the (gitignored) kernel tree; the carved DTBs and built images
remain under `local/work/` as artefacts.

## 5. Evidence / limits

* fbcon logic and config values: `local/kernel-tree/drivers/video/fbdev/core/
  fbcon.c:1113`, `include/linux/printk.h:63`, `local/kernel-tree/.config`.
* TCON source: `initramfs/init.c:371-426` (`tcon_hitachi[]`, `panel_tcon_init`)
  and `panel_tcon_init()` call at line 770.
* Bridge/DSI: `local/kernel-tree/drivers/gpu/drm/bridge/tc358764.c:402-460`,
  `local/kernel-tree/drivers/gpu/drm/omapdrm/dss/dsi.c:4200-4218`.
* Reports 133 (display fixes), 255 §4b (quiet boot), 257 (1008 MHz margin),
  258 (wedge markers), 260 (SMC 0x108 experiment).
* The operator's visual observations (text/Tux on the loud single-core boot;
  black panel on the wedges) are the primary evidence for §2.
