# Session 13 (absolute) — QNX's bridge programmer recovered; the panel shows its first partial image

> Numbering: absolute 13.  The reports and earlier handoffs call this Linux-era
> "session 7" (`NN-6`).  This note is written by the session itself.

Date / span: 2026-10-28
Unit state at the end: **TabletOS from the sealed IFS**, door up,
`BOOTCTR_FCTRY = 0x32`, IFS sha256 `c72b0530…` verified.  No new device-side
files (see §Bench state at the end).

## Goal of this session

Execute the session-12 handoff: force the complete QNX timing set as one
experiment, then the QNX DSI PHY words, then question the LVDS mapping — with
the goal of an image on the DSI → TC358764 → LVDS panel.

Done was reshaped twice during the session: first "the timing set is the
blocker" (falsified), then "the bridge receives no writes" (falsified on QNX),
and finally "the panel shows its first partial live image".

## Confirmed (measured, read in a file, or seen on the bench)

* **The timing-encoding hypothesis is false.**  Forcing the four words together
  (`DISPC_TIMING_H = 0x0890A113`, `DISPC_DIVISOR = 0x00010002`,
  `DSI_VM_TIMING1 = 0x01079074`, `DSI_CLK_CTRL = 0xA0346005`) reads back exactly
  and leaves the panel black.  The divisor change required the QNX HSDIV mX = 6
  to stay coherent (`docs/reports/124-*.md` §1).
* **The PHY-word hypothesis is false.**  QNX's `DSI_DSIPHY_CFG0/1/2` =
  `0B190D13/42040A22/B8800009` read back exactly; panel black (124 §2).
* **The overlay is enabled and scanning the painted buffer.**
  `DISPC_GFX_ATTRIBUTES` at `0xA0` = `0x120000B1`, `GFX_BA0 = 0x7FDA8000`,
  `GFX_SIZE = 0x025703FF` (124 §3.3).
* **`DISPC_CONTROL` GPOUT bits 15/16 are not the blocker.**  A live
  `poke 48041040 02000309` matched QNX and changed nothing (124 §3.2).
* **The panel is a Hitachi.**  On TabletOS with the display up:
  `memdump3 4805d138` = `00010800` (bit 16 = 1) and `sloginfo | grep "LCD is"`
  = `LCD is Hitachi` (127 §1).  The earlier Linux `GPIO6_DATAIN = 0` was an
  unclocked-GPIO artefact.
* **The reference programs the TC358764.**  `winch_lcdctl` calls the function
  at vaddr `0x13e50` from `0x1938c` in the splash sequencer, unconditionally,
  between the DSI PHY/PLL programming and the DSI VC enable (125 §1–2).  The
  full register sequence is in 125 §2.
* **The QNX self-test proves the reference's bridge writes land.**  With the
  display up: `winch_lcdctl power_off` then
  `winch_lcdctl -t 500000 power_on &`; `GPIO2_DATAIN` bit 4 toggles
  `0x08000008` / `0x08000018` / `0x0800001c` (127 §2).  The tool leaves the
  panel in a grey vertical-line test pattern (operator saw it on QNX).
* **The command-VC transport differs.**  On QNX during the self-test,
  `DSI_VC_CTRL(1) = 0x20009D81` (HS bit 9, bit 12 set, DMA_TX_REQ_NB 0); Linux
  had `0x20808D81` (LP, bit 12 clear, no-DMA).  127 §3.
* **The PLL is byte-identical to QNX.**  REGM 756 / REGN 96 / mX 6,
  `PLL_STOPMODE` (CFG1 bit 0) and CFG2 bits 3:1 clear; measured
  `CFG1 = 0x14A5E8BE`, `CFG2 = 0x00056000`, `clkin=38400000`,
  `fint=400000`, `clkdco=604800000` (127 §4).
* **First partial live image.**  Build `0x2F128BCE` (rebuilt during the wrap-up
  as `0x29D61DB6`) plus `spi tconbe` (high byte first) and a white framebuffer:
  operator saw the **left half white/grey and the right half black with a few
  white lines**, briefly (127 §5).
* **TCON byte-order behaviour.**  `spi tcon` (low byte first) then white: the
  panel **held a full white field**, but did not follow the framebuffer
  (painting black or cycling colours left it white).  `spi tconbe`
  (high byte first): the partial image/black.  127 §5.
* **The flasher now resets the ramloader itself.**  `leave_loader()` retries
  `0x80EF/0xEF/0x80EF` on a fresh `0x8001` handle; tested with an idempotent
  sealed-IFS restore (commit `dd044fc`).

## Inferred (reasoning from evidence, not directly measured)

* **The bridge programming now reaches the bridge at least momentarily.**  The
  partial image (left half) and the TCON's reaction to the table imply the
  video path and LVDS are alive; strength: medium-strong (operator observation,
  not a register read).
* **The half-frame is a horizontal timing / line-length / DE problem**, not a
  dead link or a dead bridge.  Strength: medium (the left half is our 
  framebuffer; the right half is black/lines).
* **The held white is a TCON test/default state**, not live video.  Strength:
  medium (it ignored `fb 000000` and the colour cycle).
* **The boot-time GPIO self-test was corrupting the display state.**  Removing
  it changed the symptom from a faint test pattern to the partial live image
  (`0x54FB4D9A` → `0x2F128BCE`).  Strength: medium.
* **The QNX self-test needs `power_on` to run.**  `-T 1` alone printed only
  `Platform Winchester` and returned immediately; `power_off` + `-t N power_on`
  ran long enough to poll GPIO36.  Strength: medium (behavioural).

## Dead ends and wrong assumptions (the valuable part)

* **"Nothing programs the TC358764 / it needs no programming"** (reports 121
  §12, 122, and the 2026-11-15 relay) is **wrong**.  The call was in report
  122's own decoded sequence (`1938c  0x13e50`); the programmer is there.  The
  relay's "the IFS splash proves it works untouched" is also wrong — the splash
  *is* the programmer.  Disproved by disassembling `0x13e50` (125).
* **"The LG table works."**  Confounded: the LG-table runs that produced frames
  were live with the host streaming, while the Hitachi runs were made with the
  host disabled by `unbind`/`bind` or the operator missed the transient.  The
  panel is Hitachi by GPIO176 and slog; the LG table is not for this unit.
  This cost several boots.
* **"GPIO36 constant ⇒ our writes don't land."**  The first QNX reading was
  also constant, so the indicator looked invalid; only `power_off` +
  `-t 500000 power_on` made it toggle.  The Linux constant reading before the
  VC1 fix was real, but the conclusion needed the proper QNX control.
* **"MIPI_DSI_MODE_LPM / HS is the transport difference."**  Removing LPM
  alone (`0xC4E2A542`) did not fix delivery.  The real difference was the VC1
  register word `0x20009D81` (HS, bit 12, DMA field 0), not the mode-flag
  helper (127 §3).
* **"The reset ordering is the problem."**  A throwaway write to enable the DSI
  before the RESX pulse, and moving the reset into the DSI hook, did not fix it
  by themselves.  They are still correct relative to QNX, but were not the
  missing variable.
* **"The PLL ratio / STOPMODE is the problem."**  Forcing 756/96 and clearing
  STOPMODE made the PLL exact but did not produce a held image by itself.
* **"The `unbind`/`bind` route stops only the stream."**  It actually disabled
  the whole display output: after the bind, `DSI_CLK_CTRL = 0x00340005`
  (PLL off), `VC_CTRL(0)` lost `VC_EN`, and `LINE_STATUS` stopped.  The
  transient frames seen then were the output restarting, not a TCON test.
* **"A black panel means the host stopped."**  Several times the host was
  demonstrably streaming (`VC0 = 0x20818FB3`, PLL on, `LINE_STATUS 0x96 →
  0x1F1`) while the panel was black.  This is now a known trap.
* **`winch_lcdctl -T 1` alone does not run the self-test** (or returns before
  it); the display must be power-cycled and the command given with `power_on`.
* **`/init`'s 180 s auto-reboot** repeatedly reset the display state and made
  observations easy to miss; several "still black" answers were taken after a
  state change or just before a reboot.  The operator later confirmed the
  one-off flashes.
* **The LG table bytes themselves were correct** (verified against the binary
  at `0x2e58c`); the table was simply the wrong panel family/state.

## Unknown / open questions

* **Do our bridge writes land after the VC1 fix?**  `0x5DC2711F` (VC1 +
  GPIO36 self-test) is built but **not flashed**.  Settle by flashing it and
  reading `kmsg pb selftest`: GPIO36 must follow `0x0524`.
* **Which TCON byte order is correct?**  Report 111 says low-first; report 123's
  static RE says high-first.  The bench distinguishes them (low-first → static
  white, high-first → partial image/black), but neither is yet a stable full
  image.  Settle with a pad-level SPI capture or a buffer-level re-read; see
  `docs/contradictions/111-vs-123-tcon-byte-order.md`.
* **What exactly causes the half-frame?**  Candidates: bridge `VP_HTIM*`, DSI
  `TL`, TCON timing words.  Settle by varying one field at a time with a
  contrasting framebuffer colour.
* **Does the TCON table need to be sent before the video VC enable?**  Live
  sends produce transients; the reference sends it as part of display bring-up.
  Settle with an in-kernel SPI send at the bridge-programming point.
* **Is the held white a test pattern or a valid image with no live data?**  It
  ignored `fb 000000`; a test-pattern explanation is more likely, but not
  proven.
* **Does the GPIO2_24 RESX pulse at QNX's exact point matter?**  It is now
  there; not isolated.

## Superseded / uncertain

* **121 §12 / 122: "nothing programs the TC358764, it needs no programming."**
  Superseded by report 125 (the reference calls `0x13e50`).  Evidence the old
  claim had: the IFS splash, and a static sweep that found no bridge programming
  in the second-stage loader/IFS — but the IFS's own `winch_lcdctl` was the
  programmer, and the call was already printed in 122 §7.1.
* **118 §8.2 / 111 §8 item 4** (two lanes; bridge programming over I²C) remain
  superseded as recorded (121/125).
* **The panel identity was recorded as Hitachi (118 §6.2) and briefly doubted**
  when the LG table produced frames under Linux.  The current, measured answer
  is Hitachi (`GPIO6_DATAIN = 0x00010800`, slog).  The LG result was a host-state
  artefact.
* **Session 12's "only timing encoding differs"** was the handoff's premise; it
  is falsified by 124.  The newer statement (the bridge programming and the
  TCON/line timing are the remaining variables) is current.
* **The 2026-11-15 relay's "the IFS splash proves the bridge untouched"** is
  superseded by 125: the splash programs the bridge.

## Artefacts produced

Reports: `123-re-qnx-spi-tcon-2026-10-28.md`,
`124-qnx-timing-and-phy-2026-10-28.md`,
`125-re-bridge-programmed-2026-10-28.md`,
`126-re-qnx-bridge-gpio-selftest-2026-10-28.md`,
`127-display-vc1-hs-tcon-2026-10-28.md`.

Contradiction: `docs/contradictions/111-vs-123-tcon-byte-order.md` (open).

Source changes (copies in `kernel/source/`): `dsi.c` (QNX timing set, HSDIV mX,
PLL override, bridge sequence at the DSI point, VC1 = 0x20009D81, RESX export
hook, FIFO drain, diagnostic self-test), `pll.c` (STOPMODE/CFG2 clear),
`tc358764.c` (reference init, RESX export), `omap4-winchester.dts`
(`lp-rate = <10080000>`).  Console: `spi tconbe`, `av[24]` (`initramfs/console.c`).
Tooling: `tools/pre_os_ifs_flash.py` `leave_loader()`.

Images (CRC): `0x286C232F` (four-word timing), `0xEAAAA372` (PHY),
`0x2964FC3F` and `0x070E35EE` (bridge init; the latter saved),
`0xD00262EC` (reset order + LP self-test), `0xC4E2A542` (HS via mode flag),
`0xED55C280` (bridge in DSI hook), `0x199ED304` (RESX in hook),
`0x23CDEE3E` (QNX PLL ratio), `0xE0E60430` (STOPMODE/CFG2),
`0x6FBF71DD` (FIFO drain), `0x54FB4D9A` (VC1 + self-test),
`0x2F128BCE` (VC1, no self-test — the build that produced the partial image),
`0x5DC2711F` (VC1 + diagnostic; built, **not flashed**), and `0x29D61DB6`
(VC1, no self-test, rebuilt during the wrap-up; code-equivalent to
`0x2F128BCE`).  Saved:
`kernel-0x070E35EE-bridge-init-diag.img`, `kernel-0x29D61DB6-vc1-noselftest.img`,
`kernel-0x5DC2711F-vc1-selftest.img` (plus `kernel-0x199ED304-reset-in-hook.img`,
`kernel-0x6FBF71DD-fifo-drain.img`, `kernel-0xED55C280-dsi-hook.img`).
**The `0x54FB4D9A` and `0x2F128BCE` bytes were not preserved** — they were
overwritten before the wrap-up noticed; the rebuild is not byte-identical.

## Bench state at the end (for the next session)

* Unit: spare `0x50176EC1`, on **TabletOS** (QNX 6.6) from the sealed IFS,
  `0fca:8010`, door up via `tools/pbssh.sh`.
* `BOOTCTR_FCTRY = 0x32` (read after the final restore; `32 00 00 00`).
* Sealed IFS on media: sha256
  `c72b053027769c4d49270e9ee208fd2b0afcf69ce4fb3cf0c0be3c938ee05299` (verified).
* Image on media: the sealed IFS, **not** a chainloader/kernel image.  The test
  images are in `local/images/`.
* Device-side files: `/accounts/devuser/memdump3` was already present from an
  earlier session (Sep 2012 timestamp); nothing new was deployed.  `/tmp` logs
  from this session are gone (fresh TabletOS boot).
