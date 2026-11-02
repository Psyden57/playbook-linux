# Session 14 (absolute) — the bridge starts answering; HS video still does not arrive

> Numbering: absolute 14.  The reports call this Linux-era **"session 8"**
> (`NN-6`).  This note is written by the session itself.

Date / span: 2026-10-28 (late), into 2026-10-29 01:00.
Unit state at the end: **TabletOS from the sealed IFS**, `0fca:8010`, door up,
`BOOTCTR_FCTRY = 0x32`, IFS sha256 `c72b0530…` verified, **no test image on
media**.  See §Bench state.

## Goal of this session

Execute the session-13 handoff: (1) confirm the bridge writes land by flashing
`0x5DC2711F` and reading `kmsg pb selftest`; (2) settle the Hitachi TCON byte
order; (3) if both are clean, fix the horizontal timing of the half-frame.

"Done" was reshaped three times.  Step 1 came back **negative**, which
invalidated steps 2 and 3 and turned the session into a delivery
investigation.  That investigation **succeeded** — the bridge now receives and
answers.  The panel is still black, for a new and narrower reason.

## Bench state (verified at the end of the session, re-read, not remembered)

| item | value |
|---|---|
| OS on the unit | TabletOS, `QNX localhost 6.6.0 2011/11/11-16:00:16EST OMAP4430_Winchester_ES2.2_HS_PVT4_Rev:07 armle` |
| USB | `0fca:8010` (media running), door up, `tools/pbssh.sh` works |
| `BOOTCTR_FCTRY` | `0x32` (`32 00 00 00`) |
| media contents | the **sealed IFS**, `dd if=/dev/hd1 bs=512 skip=2048 count=5632` → 0x2BFE10 bytes, os_type byte at 0x100 = 6 |
| media sha256 | `c72b053027769c4d49270e9ee208fd2b0afcf69ce4fb3cf0c0be3c938ee05299` **MATCH** |
| image CRC on media | none — no chainloader/kernel image is on media |
| device-side files deployed | `/tmp/pbi2c` (QNX I²C tool), **gone** — it was wiped by the next reboot; `ls /tmp/pbi2c` → `No such file or directory` |
| host processes | `door-hold.sh` + `BerryShell-V4.py hold` running since 28 Oct 10:22 |

The unit was cold-booted once (operator battery pull) during the session; that
cleared a persistent left-half panel artefact that no warm reset clears.

## Confirmed (measured, read in a file, or seen on the bench)

**Delivery (the session's result)**

* **The bridge answers DSI reads now.**  `IDREG 0x0580` returns
  `0x00006500` (`TC358764XBG_ID`) instead of `-EIO`.  It was `-5` on every
  earlier build, including session 13's.  [image `0x49A770D3`, log
  `pb probe after-seq: IDREG r=0 0x00006500`; report 131 §2]
* **The reference's own GPIO36 delivery test passes.**  `0x0524` = 1/4/5/0 →
  GPIO36 = 1/0/1/0, all four correct.  [same image, `pb gpio test` lines;
  report 131 §2]
* **The missing step is a null packet on VC0 with `MODE_SPEED = 1`.**  VC0 must
  be in **command** mode (bit 4 = 0) with HS selected (bit 9) and VC_EN set;
  writing `SPH(0) = 0` then completes (`PACKET_SENT`, 1 poll).  This is the
  reference's `L1DSS_DSI1_lp_mode_start_DSI_QNX` (report 128 §1.1).  [image
  `0x49A770D3`]
* **Every programmed bridge register reads back correctly**: `LV_CFG 0x049C = 1`,
  `LV_PHY0 0x04A0 = 0x0004C02D`, `VP_CTRL 0x0450 = 0x03F00120`,
  `VP_VTIM1 = 0x000A0001`, `VP_VTIM2 = 0x000C0258`, `VP_VFUEN = 1`,
  `GPIOC/GPOIO 0x0520/0x0524 = 5`, `DSI_LANEENABLE 0x0210 = 0x1F`.  [image
  `0x2DF88C1A`, `pb bridge dump`; report 131 §3]
* **`VP_HTIM1/2` store only their low half**: writing `0x78000001`/`0x76000400`
  reads back `0x00000001`/`0x00000400`.  QNX writes the same values.  [same dump]
* **`PPI_STARTPPI 0x0104`, `DSI_STARTDSI 0x0204` and `SYS_RST 0x0504` read 0**
  after a write of 1 — consistent with self-clearing *start* bits.  [same dump]

**Corrections to earlier documents**

* **`MODE_SPEED` is `DSI_VC_CTRL[9]` and `0x0` means LP.**  QNX's measured
  `0x20009D81` has bit 9 **clear**; report 127 §3's "QNX runs the command VC in
  HS" is wrong.  The real deltas from mainline's `0x20808D81` are bit 23
  (`DMA_TX_REQ_NB = 4` vs 0) and bit 12 (`RGB565_ORDER`).  [TRM Table 10-530,
  quoted in report 129 §3]
* **The `-5` from `mipi_dsi_generic_write()` was a BTA timeout, not a transmit
  failure.**  `dsi_vc_write_common()` calls `dsi_vc_send_bta_sync()` after every
  write (`dsi.c:2247`), so a bridge that does not answer returns `-EIO` even
  though the packet left the FIFO.  [report 129 §2]
* **The session-13 "first partial live image" is not ours.**  Framebuffer
  painted magenta (`fb ff00ff`), TCON sent, and the operator saw a **grey**
  flash.  [operator observation; report 129 §5]

**Reference-vs-Linux comparisons made this session (all read on both sides)**

* Full DSI PHY block captured on the reference **during its self-test**, filling
  the gap the previous session named: `CFG0/1/2 = 0B190D13/42040A22/B8800009`,
  `CFG5 = 0xFF000000`, **`CFG10 (0x4228) = 0x00000000`** — all equal to ours.
  [report 129 §7d]
* `CONTROL_DSIPHY` (`0x4A100618`) = **`0x1FF80000` on both**.  [report 131 §4c]
* PMIC rails: reference has **`VDAC` ON at 1800 mV and `VANA` ON at 2100 mV**;
  Linux had both **disabled**.  With them marked `regulator-always-on` (image
  `0xE4D4DEC2`) every rail agrees and the symptom is unchanged.  [report 129 §7d]
* Pads: the display group `0x4A100060…0x4A100090`; the only real mismatch is
  `0x4A100078` — reference `0x410B4103`, ours `0x011F011F`.  Those are
  `gpmc_ncs2/ncs3` = **gpio_52/gpio_53 (GPIO2_20/21)**, held as GPIO with no
  pull by the reference, safe-mode with pull-up by us.  Fixed in the DTS; reads
  `0x410B4103` now.  Neutral for the panel.  [report 131 §4d]
* DSS clock module: `CM_DSS_CLKSTCTRL` reference `0x00000F02` vs ours
  `0x00000702`; `CM_DSS_DSS_CLKCTRL` reference `0x00000F02` vs ours
  `0x00040502`.  The deltas are `OPTFCLKEN_TV_FCLK` and the read-only
  `STBYST`/`IDLEST` status — nothing that gates the DSI.  [report 131 §4f]
* `DSI_PLL_CONFIGURATION2 = 0x00056000` on both; `PLL_STATUS = 0x383` with
  `M4_CLOCK_ACK`, `M5_CLOCK_ACK` and `BYPASSACKZ` set.  [report 131 §4f]

**The failure is now characterised**

* **No lane is ever in HS RX mode**: `DSI_LANESTATUS0 (0x0214) = 0` over 24
  samples plus retries; `LANESTATUS1 (0x0218) = 0x1D` (ULPS/STOP).  [image
  `0xF9BE8587`, `pb late lanes`; report 131 §4a]
* **The host really is streaming**: clearing `VC_IRQSTATUS(0)` and re-reading
  shows `PACKET_SENT` set again within 5 ms, twice.  [image `0x59E6F33B`,
  `pb reg: video irq … PACKET_SENT 1 1`]
* **The DISPC pixel rate is correct**: `LINE_STATUS` sampled at 1 ms steps read
  `162 → 199 → 237`, i.e. **37.5 lines/ms ≈ 37.4 kHz**, the reference's rate.
  [same image, `pb late scan`]
* **The panel does not react to the bridge's LVDS enable at all**: `LV_CFG`
  toggled 1→0→1 three times, 2 s apart, with a bright green framebuffer; the
  operator saw no change.  The same panel *does* react to SPI.  [operator
  observation, report 131 §4a]
* **The session-13 white is not reproducible with the bridge programmed**:
  magenta painted first, then low-first `spi tcon` → panel **stayed black**
  (neither magenta nor white).  [operator observation, report 131 §4e]

**QNX-side RE done this session**

* `winch_lcdctl 0x17094` is `L1DSS_DSI1_lp_mode_start_DSI_QNX`; it enables VC1
  (LP) and VC0 (**HS**), sets the FIFO sizes, `DSI_CTRL`, `COMPLEXIO_CFG1`, then
  sends the two null packets.  Its failure string is literally *"Could not send
  inital null packet to start HS clock"*.  [report 128]
* `0x16bd8` is `L1DSS_DSI1_hs_mode_start_DSI_QNX` and writes
  `VC_CTRL(0) = 0x20800791` (bit 4 = 1, video mode) — **after** the bridge.  Using
  that word for the null packet makes `PACKET_SENT` never complete.
  [report 128 §2, confirmed on the bench with image `0xF5329FDD`]
* The bridge is **never** programmed over I²C by the QNX set.  [report 128 §4]
* The per-panel table is keyed by platform + refresh rate, not panel identity;
  the Hitachi/LG branch selects **only** the 9- vs 11-word SPI table, and
  `LV_CFG` is hard-coded to **1** (single link).  [report 130]
* `pbi2c` rebuilt against the QNX 6.6 toolchain and run on TabletOS: the bridge
  **NAKs at `0x0F` on `/dev/i2c0`, `/dev/i2c1` and `/dev/i2c3`** — its I²C slave
  is not wired.  [report 131 §4c]
* `QNX /dev/i2c3` is at `0x48350000`, which our Linux DTS does **not** enable;
  our third bus is `0x48060000`.  [same]

**Tooling**

* `get_mainline`-style register pokes are no longer needed for the bridge: a
  sysfs hook in `dsi.c` reads and writes any bridge register from the console.
  `write /sys/bus/platform/devices/58004000.encoder/pb_reg w0524:0` dropped
  GPIO36 from 1 to 0 in one command.  [image `0xBD7F65A6`, report 131 §4a]
* The kernel patch was regenerated **twice**: finally 4 003 lines / 31 files,
  verified by applying to a fresh copy of pristine and comparing all 31 files
  byte-identically.  [`kernel/patches/0001-playbook-winchester-6.15.11.patch`]

## Inferred (reasoning from evidence, not directly measured)

* **The data lanes and the bridge's LP receivers are electrically fine.**
  LP command packets reach the bridge (it answers reads and writes), and LP
  travels the same four data lanes as HS.  Only the **clock lane** at ~151 MHz
  and the receiver PLL locking to it are additional for HS.  Strength: strong
  for "the data lanes work"; medium for "the clock lane is the fault" — it is
  the remaining candidate, not a proven one.
* **The panel is probably fine.**  It responds to SPI (grey flash in the
  unprogrammed state) and stands on its own TCON.  Strength: medium (no direct
  measurement of the panel's power rails).
* **The session-13 white was the TCON's own state.**  It happened with the
  bridge unprogrammed, i.e. with no LVDS clock, and does not reproduce once the
  bridge is programmed.  Strength: medium.
* **The `0x078/0x07A` pads are parked, not functional.**  Matching the reference
  changed nothing.  Strength: medium.

## Dead ends and wrong assumptions (the valuable part)

* **"The bridge writes don't land, so the transport is wrong."**  The transport
  was not the problem: the **HS clock was never started**.  Every transport
  variant (LP, HS, VC1 null packet, VC setup reordering) failed for that one
  reason and all looked identical, which is why four images produced the same
  log.  Killed by report 128's `0x17094` analysis.
* **"QNX runs the command VC in HS."**  Wrong (report 127 §3); `MODE_SPEED` is
  bit 9 and QNX's word has it clear.  This wasted a build (`0xC4E2A542` in
  session 13, and the HS retry ladder here).
* **"The partial live image is our framebuffer."**  Wrong — grey flash with a
  magenta framebuffer.  Killed by the operator's colour observation.
* **"`0x20800791` is the word for the clock-start null packet."**  Wrong: bit 4 =
  1 selects *video* mode, and `PACKET_SENT` never completes in video mode.  The
  bench showed `polls=2000 (NO PACKET_SENT)` with image `0xF5329FDD`.
* **"A whole-word store to `VC_CTRL(0)` is fine."**  Wrong: it destroys the DMA
  and `OCP_DATA_BUS_WIDTH` fields set by `dsi_vc_initial_config()`, and it
  silently overwrote the OCP fix in image `0xA3A24D73`.  Must be
  read-modify-write.
* **"Moving `dsi_setup_dsi_vcs()` ahead of the bridge programming is safe."**
  It stopped the DISPC scanout (`LINE_STATUS` stuck at 0).  Reverted.
* **"A failed BTA is harmless."**  It clears `VC_EN` behind the driver's back
  (`VC_CTRL(1)` reads `0x20001DC0`), which invalidated the first probe image's
  LP result.  The probe must re-establish the VC before measuring.
* **"The rails are the difference."**  `VDAC`/`VANA` were genuinely off in Linux
  and on in the reference, and enabling them changed nothing.
* **"The D-PHY RX block needs programming."**  It is a sane POR state, and
  mainline's working gma500 `tc35876x` driver for this chip family writes none
  of it.
* **"The RESX pulse is required."**  Restoring the reference's pulse
  (`0xDED99C5E`) changed nothing; the boot chain resets the bridge anyway
  (`IDREG` is `-5` before the sequence on every boot).
* **"The I²C slave is a second way in."**  The bridge NAKs at `0x0F` on all
  three QNX buses.
* **Build traps hit this session:** `udelay(5000)` fails to link
  (`__bad_udelay`, max 2 ms) and the build leaves the **previous** image in
  place, so a "new" CRC that matches the old one means the link failed — check
  the build output.  The console's `write` passes only the **first token**, so a
  sysfs interface must take a space-free argument (`w0524:0`, `r0214`).
  `pkexec` also started timing out mid-session and needed the operator to
  approve the prompt.

## Unknown / open questions

* **Why does HS video never reach the bridge?**  Every readable register on both
  sides matches.  Resolve with a scope on the DSI clock pair at the bridge
  (~151 MHz expected during video); there is none on this bench and no known
  test point.  Alternatively the full TC358764XBG register manual (V1.19), which
  is not publicly available.
* **What do `DSI_LANESTATUS0/1`, `SYSSTAT (0x0500)` and
  `DSI_INTSTATUS = 0x90400883` actually mean?**  Undecodable without the
  register map.
* **Is the clock lane physically switching?**  Not measurable without a probe.
* **Are our bridge reads trustworthy, or marginal?**  `0x0214`, `0x0300`,
  `0x0500` and `0x0110` each returned `-5` on some attempts and values on others
  in the same boot, while `0x0214` read 24 times in a row in one sampling loop.
  Our `PPI_TX_RX_TA (0x013C) = 4` (`TXTAGOCNT = 0`) is far below mainline's
  `0x00020003`, the msm driver's `0x00040004` and gma500's `0x00040006`, and that
  register is the bus **turn-around** timing.  Raise it and re-read; if the
  failures stop, `LANESTATUS0 = 0` becomes a measurement rather than a
  best-of-N.  [report 131 §4g; upstream driver fetched 2026-10-29]
* **Which TCON byte order is right?**  Still open, and now weaker: neither order
  produces an image with the bridge programmed
  (`docs/contradictions/111-vs-123-tcon-byte-order.md`).
* **Is `GPIO2_24` really RESX?**  The reference reads it **low** during a working
  display while the datasheet says RESX is low-active; both cannot hold.  Needs
  a schematic or a relay question.

## Superseded / uncertain

* **Report 127 §3 ("QNX runs the command VC in HS")** — superseded by report 129
  §3 (LP; `MODE_SPEED` bit 9 = 0).  Old evidence: bit 9 was assumed set in
  `0x20009D81` without decoding.
* **Report 127 §5 ("first partial live image")** — withdrawn in report 129 §5;
  the flash was grey against a magenta framebuffer.
* **Report 122 ("GPIO2_24 = bridge RESX", medium confidence)** — unproven again;
  see report 129 §7e and the open question above.
* **Session 13's bench reading of the TCON orders** — both observations were
  made with the bridge unprogrammed; recorded as weakened in
  `docs/contradictions/111-vs-123-tcon-byte-order.md`.
* **The previous patch snapshot (2 922 lines / 29 files)** — stale and
  unbuildable: it had no `pb_bridge_program` and did not create
  `include/linux/pb_event.h` although its own hunks include it.  Current:
  4 003 lines / 31 files.

## Artefacts produced

**Reports**: `128-re-qnx-dsi-second-pass-2026-10-28.md` (RE, `0x17094`/`0x16bd8`),
`129-session14-delivery-and-timing-truth-2026-10-28.md`,
`130-re-qnx-panel-parameter-table-2026-10-28.md` (RE, per-panel table),
`131-bridge-delivery-fixed-2026-10-28.md` (the fix and everything ruled out).

**Source changes** (copies in `kernel/source/`): `dsi.c` —
`pb_start_hs_clock()` (the VC0/HS null packet — the fix), `pb_bridge_read()` /
`pb_bridge_probe()` / `pb_bridge_dump()` / `pb_bridge_gpio_test()`,
`pb_vc_setup_lp()`, the `pb_reg` sysfs hook (`w<addr>:<val>`, `r<addr>`, `v`),
forced `DSI_TIMING1/2/CLK_TIMING`, forced `VM_TIMING6/7`, `OCP_DATA_BUS_WIDTH`
24-bit, `dsi_setup_dsi_vcs()` ordering comment.  `omap4-winchester.dts` —
`VDAC`/`VANA` always-on, pads `0x078/0x07A` and the gpio2-20/21 hogs.

**Kernel patch**: `kernel/patches/0001-playbook-winchester-6.15.11.patch` and
`kernel/clean-kernel-6.15.11-changes.patch` regenerated (4 003 lines, 31 files);
`kernel/patches/README.md` and `docs/project/SETUP.md` corrected.

**Payload**: `local/pristine/linux-6.15.11.tar.xz` (151 MB, sha256
`89ab469fc35bd9cbc6c5bf4e5cb802581806d5cbc14fb47e5c9edcc477e65d93`), verified
byte-identical to the pre-extracted pristine tree.

**Images** (all in `local/images/`, CRC = `zlib.crc32(image[0:-4])`):
`0x9FF603AD` probe; `0x55D4488F` no-RESX (built, **not flashed**);
`0x1DA714C9` HS-clock probe; `0xA1EF5F02` VC-first probe; `0xE4D4DEC2` rails;
`0xEC932C5B` forced DSI timings; `0xF5329FDD` VC0/HS with the video-mode word
(never completes); **`0x49A770D3` the fix**; `0x4E44A894` bridge-live;
`0x2DF88C1A` bridge dump; `0x05AB723E` late check; `0xF9BE8587` lane sampling;
`0xBD7F65A6` sysfs hook; `0x0E55D92E` scan-rate; `0x59E6F33B` video activity;
`0x5BCC1863` pads; `0xDED99C5E` RESX; `0xA3A24D73` and `0xD10FD1C7` OCP-24;
`0x48048809` VM_TIMING6/7 (the last image flashed this session).

**Tooling built**: `/tmp/pbi2c` from `tools/qnx-i2c.c` with the QNX 6.6
toolchain (not kept on the unit — `/tmp` was wiped).
