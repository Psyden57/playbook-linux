# 127 — Display breakthrough: the panel is Hitachi, QNX's bridge writes are HS, and the first partial image

**Session 13 (continued), 2026-10-28.**  This report records the display work
after report 125: the definitive panel identification, the first proof that the
TC358764 *does* receive register writes (on QNX), the command-VC transport
difference, the byte-identical PLL, and the first partial live image on the
panel.  The panel is not yet showing a stable live framebuffer; the remaining
problem is now the TCON/VP timing, not "nothing works".

---

## 1. The panel is a Hitachi (definitive)

Read on TabletOS with the display up and GPIO6 clocked:

```
memdump3 4805d138 1   ->  00010800      bit 16 = 1  => Hitachi
sloginfo | grep "LCD is" ->  LCD is Hitachi
```

The earlier Linux `GPIO6_DATAIN = 0` was from an unclocked GPIO6 and is not
evidence.  The LG-table result in §4 was an artefact of the host being in a
different state; **use the 9-word Hitachi table**.

## 2. Proof that the reference's bridge writes land

QNX's `-t/-T` self-test (`winch_lcdctl 0x196dc`, report 126) programs the
bridge's GPIO block over DSI and reads the OMAP pins.  Run on the spare as:

```
/proc/boot/winch_lcdctl power_off
/proc/boot/winch_lcdctl -t 500000 power_on &
memdump3 48055138 1        # repeatedly
```

`GPIO2_DATAIN` (GPIO2_4 = GPIO36) toggles with `0x0524`:

```
08000008   (bit4 = 0)
08000018   (bit4 = 1)
0800001c   (bit4 = 1, other pins)
```

So the bridge receives the writes under QNX, the GPIO36 indicator is valid, and
our Linux self-test reading a constant `1` means **our writes were not landing**
(after the fixes in §3 that may change; the diagnostic build is
`0x5DC2711F`, saved but not yet flashed).

The same test visibly leaves the panel in a **grey vertical-line test pattern**
(observed on QNX), so the grey/lines seen under Linux on the LG table were a
test state, not live video.

## 3. The command-VC transport difference (measured)

Captured on QNX while the self-test loop was running:

| register | QNX | Linux (before this work) |
|---|---|---|
| `DSI_VC_CTRL(1)` | **`0x20009D81`** | `0x20808D81` |

Decoded with TRM Table 10-584: QNX runs the command VC in **HS mode (bit 9 = 1)**,
with bit 12 set and `DMA_TX_REQ_NB = 0`; mainline leaves it in **LP**, bit 12
clear, and the no-DMA field set.  The bridge writes are sent on this VC.

Implemented: `pb_bridge_program()` now writes `DSI_VC_CTRL(VC_CMD) =
0x20009D81` before the sequence, enables the interface, and waits for the TX
FIFO to drain between packets.  It runs inside `dsi_init_dsi()`, after
`dsi_proto_config()` and before `dsi_setup_dsi_vcs()` — the reference's point —
with the RESX pulse there too (`tc358764_playbook_reset()`).

## 4. The PLL is now byte-identical to QNX

Forced REGM = 756, REGN = 96 (n), mX = 6 for both HSDIVs, and:
* `PLL_STOPMODE` (CFG1 bit 0) left **clear** (QNX; mainline sets it — it stops
  the output while unlocked, consistent with one-frame-then-black);
* `PLL_CONFIGURATION2` bits 3:1 cleared.

Measured after boot:

```
DSI PLL STATUS 00000383
DSI PLL CFG1   14a5e8be     (QNX 14a5e8be)  EXACT
DSI PLL CFG2   00056000     (QNX 00056000)  EXACT
pb PLL forced: clkin=38400000 fint=400000 clkdco=604800000
```

`DSI_CLK_CTRL = 0xA0346005`, `VM_TIMING1 = 0x01079074`, `VM_TIMING3 =
0x03F00258`, `DSI_CTRL = 0x00EAEF9D`, PHY `0B190D13/42040A22/B8800009` — all
still equal to the reference.

## 5. The first partial live image

Build `0x2F128BCE` (QNX VC1 state, RESX in the DSI hook, byte-identical PLL,
**no boot-time GPIO self-test**).  With the host streaming (`VC0 = 0x20818FB3`,
PLL on, `LINE_STATUS 0x96 -> 0x1F1`) and the framebuffer painted:

* `spi tconbe` (Hitachi, **high byte first**) then white: a brief flash with
  **the left half white/grey and the right half black with a few white lines**.
  This is the first evidence of our own framebuffer content on the panel.
* `spi tcon` (Hitachi, **low byte first**) then white: the panel **held a full
  white field** — but it did not follow the framebuffer (painting black or
  cycling colours left it white).  That is the TCON's static/test state, not
  live video.
* `spi tconbe` + red then produced a black/glitching transition and the unit
  rebooted (`/init`).

So the video path now reaches the panel (half-frame), the bridge is programmed,
and the TCON reacts to the table; what remains is the **horizontal timing /
byte order / line-length** that turns a half-frame into a stable full image.

## 6. Images

| image | crc | contents |
|---|---|---|
| QNX VC1 + self-test | `0x54FB4D9A` | VC1 = 0x20009d81, boot self-test left in (faint test pattern).  **Bytes not preserved.** |
| QNX VC1, no self-test | **`0x2F128BCE`** | the build that produced the half-frame; TCON reacts.  **Bytes not preserved.** |
| VC1, no self-test, rebuilt | **`0x29D61DB6`** | the same source state as `0x2F128BCE`, rebuilt during the wrap-up; the usable best-display image |
| VC1 + delivery diagnostic | `0x5DC2711F` | re-adds the GPIO36 self-test at boot to confirm delivery; **not flashed** |

Saved in `local/images/` as `kernel-0x29D61DB6-vc1-noselftest.img` and
`kernel-0x5DC2711F-vc1-selftest.img`.  The `0x54FB4D9A`/`0x2F128BCE` bytes were
overwritten before this was noticed; the rebuild is code-equivalent but not
byte-identical (different CRC).

## 7. What is still open

1. **Confirm delivery with `0x5DC2711F`.**  Flash it, read `kmsg pb selftest`;
   GPIO36 must toggle with `0x0524`.  If it does, the VC1 fix made the writes
   land and the remaining work is timing only.  If not, the transport still
   differs (compare the QNX VC1 state again; the FIFO/drain may still differ).
2. **TCON byte order — a genuine contradiction.**  Report 111 recorded the
   Hitachi wire order low-first; report 123's static RE concluded high-first.
   The bench now distinguishes them: low-first → static white field, high-first
   → partial live image / black.  This needs a pad-level capture or a careful
   re-read of `winch_lcdctl`'s SPI send, and the contradiction must be recorded
   rather than resolved by preference.
3. **Horizontal timing.**  A half-frame (left white, right black) points at the
   line length / blanking / DE, i.e. the bridge's `VP_HTIM*` words, the DSI
   `TL`, or the TCON table's timing words.  All currently copy QNX; the next
   step is to vary one at a time once delivery is confirmed.
4. **Automate the TCON send.**  The live `spi tcon` is a console command; the
   reference sends the table as part of display bring-up.  The clean fix is an
   in-kernel send at the bridge-programming point (before the video VC), which
   also removes the live-order variable.

## 8. Corrections / notes for the docs

* The panel is **Hitachi**; the LG table is not for this unit.
* The QNX self-test is a diagnostic and leaves the display in its grey test
  pattern; it must not run in the normal boot path (build `0x2F128BCE` removes
  it).
* `PLL_STOPMODE` and CFG2 bits 3:1 are part of the reference's PLL state and
  are now matched.
* The host is streaming while the panel is black/partial, so a black panel is
  no longer confusable with a dead host link.
