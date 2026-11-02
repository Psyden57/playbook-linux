# 129 — Session 14: the bridge writes do not land, the "partial live image" was not ours, and the DSI block was never fully compared

**Session 14 (absolute), Linux-era session 8.  2026-10-28.**  This report records
the first bench cycle of the session and three corrections that change the
display model.  Image under test: `kernel-0x5DC2711F-vc1-selftest.img`
(CRC `0x5DC2711F`, verified before flashing: `zlib.crc32(image[0:-4])` equals the
stored trailer).

---

## 1. The GPIO36 delivery test: the writes do not land

Build `0x5DC2711F` contains the diagnostic whose log string is
`pb selftest 0x0524=%u GPIO36=%u` (that source is no longer in the tree; the
string was confirmed in the extracted `vmlinux` **before** flashing, by
decompressing the zImage at image offset `0x2C0000` and searching it — so no boot
was spent on an image without the test).

```
omapdss_dsi 58004000.encoder: pb selftest 0x0524=1 GPIO36=1
omapdss_dsi 58004000.encoder: pb selftest 0x0524=4 GPIO36=1
omapdss_dsi 58004000.encoder: pb selftest 0x0524=5 GPIO36=1
omapdss_dsi 58004000.encoder: pb selftest 0x0524=0 GPIO36=1
```

Required (report 126 §3): `1 → 1`, `4 → 0`, `5 → 1`, `0 → 0`.  **GPIO36 never
follows bit 0.**  The first pattern "passes" only because the init table's final
`0x0524 = 5` had already left the pin high.

The read is valid — checked directly on the same boot:

| item | value | meaning |
|---|---|---|
| `CTRL_CORE_PAD 0x4A100058` | `0x0108011B` | MUXMODE 3 = GPIO, INPUTENABLE set |
| `GPIO2_OE 0x48055134` | `0xCEFECDFF` (bit 4 = 1) | GPIO2_4 is an **input** |
| `GPIO2_DATAOUT 0x4805513C` | `0x31013200` | bit 4 not driven by us |
| `GPIO2_DATAIN 0x48055138` | `0x01000010` | bit 4 = **1** with `0x0524 = 0` |
| `CTRL_CORE_PAD 0x4A10017C` | `0x010F0103` | GPIO6_11 muxed to GPIO (mux 3) |
| `GPIO6_DATAIN 0x4805D138` | `0x00000000` | **not usable**: no consumer enables GPIO6, so the module is not clocked |

So the pin really is high while the bridge should be driving it low.

## 2. The OMAP is transmitting; the bridge is not answering

Read from the running image:

| register | value | decode |
|---|---|---|
| `DSI_IRQSTATUS 0x58004018` | `0x00000080` | **PLL_LOCK only** — no `HS_TX_TIMEOUT`, no `LP_RX_TIMEOUT`, no `TA_TIMEOUT`, no `COMPLEXIO_ERR` |
| `DSI_VC_IRQSTATUS(1) 0x58004138` | `0x00000004` | `PACKET_SENT`; no `FIFO_TX_OVF`, no `FIFO_TX_UDF`, no `CS` error |
| `DSI_VC_CTRL(1) 0x58004120` | `0x20009D81` | bit 5 = 0 → TX FIFO empty; our written value holds |
| `DSI_VC_LONG_PACKET_HEADER(1)` | `0x00000000` | consumed by the protocol engine |

So the packets are queued, transmitted and drained with no error.  `PACKET_SENT`
is an internal completion: it does **not** prove the bridge received anything.

Correction to the earlier reading of `-5`: `dsi_vc_write_common()`
(`dsi.c:2226`) calls **`dsi_vc_send_bta_sync()` after every write** and returns
`-EIO` when the bridge does not answer the bus turnaround.  That is exactly the
`write 0x013c <- 0x4 failed: -5` line from report 125.  The earlier
`mipi_dsi_generic_write()` attempts were therefore *transmitted* and failed only
on the missing ack; our hand-rolled `pb_bridge_write()` skips the BTA and so
reports success unconditionally.  **The bridge is not acknowledging**, which is
consistent with §1.

The TC358764 *does* answer DSI reads on real hardware: the Qualcomm MSM driver
`mipi_tc358764_dsi2lvds.c` reads `IDREG 0x0580` and requires
`0x00006500` before it programs anything.  A failed ack here is therefore
evidence about *reception*, not a chip limitation.

## 3. Correction: QNX is **not** running the command VC in HS

Report 127 §3 decoded QNX's `DSI_VC_CTRL(1) = 0x20009D81` as "HS mode (bit 9 =
1)".  Per TRM Table 10-530, bit 9 is `MODE_SPEED` and **`0x0` = low power**.
`0x20009D81` has bit 9 **clear**, so QNX's command VC is in **LP**, exactly as
ours is.  The real differences between QNX and mainline are:

| bit | field | mainline `0x20808D81` | QNX `0x20009D81` |
|---|---|---|---|
| 23:21 | `DMA_TX_REQ_NB` | 4 (`no dma`) | 0 |
| 12 | `RGB565_ORDER` | 0 | 1 |
| 9 | `MODE_SPEED` | 0 (LP) | 0 (LP) |

So "match QNX's VC1 to get HS delivery" is not a supported reading, and the
`MIPI_DSI_MODE_LPM` story in `tc358764.c` rests on the same mistake.  Our
transport now equals QNX's byte for byte and the bridge still ignores it.

## 4. New: the DSI protocol block was never fully compared

Reports 118/121/127 compared `DSI_CTRL`, `DSI_CLK_CTRL`, `VM_TIMING1/2/3`, the
PHY words and the PLL.  The rest of the block was not.  Dumped side by side
(QNX from report 118 §3, ours live this session):

| offset | register | QNX | ours | note |
|---|---|---|---|---|
| `0x48` | `COMPLEXIO_CFG1` | `2A054321` | `2A054321` | match |
| `0x54` | `CLK_CTRL` | `A0346005` | `A0346005` | match |
| `0x58` | `TIMING1` | `79190999` | **`FFFF1000`** | TA_TO/STOP_STATE differ |
| `0x5C` | `TIMING2` | `7FFF7FFF` | **`FFFFFFFF`** | HS_TX_TO / LP_RX_TO enables differ |
| `0x60/64/68` | `VM_TIMING1/2/3` | `01079074` / `04010C0C` / `03F00258` | same | match |
| `0x6C` | `CLK_TIMING` | `00000E0B` | **`0000120E`** | DDR_CLK_PRE/POST differ |
| `0x70` | `TX_FIFO_VC_SIZE` | `00002000` (VC1 = 64 entries) | **`13121110`** (4× 32) | both valid; QNX gives the FIFO to VC1 only |
| `0x74` | `RX_FIFO_VC_SIZE` | `00002000` | **`13121110`** | ditto |
| `0x78` | `COMPLEXIO_CFG2` | `00030000` | `00030000` | match |
| `0x84` | `TX_FIFO_VC_EMPTINESS` | `00003F00` | **`00001F00`** | follows the allocation |
| `0x8C` | `VM_TIMING7` | `000000C8` | **`01E6001A`** | unexplored |
| `0x90` | `VM_TIMING8` | `000B0009` | `000B000A` | one bit differs |
| `0x94` | `STOPCLK_TIMING` | `00000080` | `00000080` | match |
| `0x100` | `VC_CTRL(0)` | `20808791` | **`20818FB3`** | `OCP_DATA_BUS_WIDTH` 24-bit vs 32-bit, plus bits 16/11/1 |

None of these is obviously fatal for an LP command packet, but they are real
differences between "matches the reference" and "matches the words we happened
to look at".  They are the next single-variable set to test (`DSI_TIMING1/2`
and `CLK_TIMING` first: they are the LP/HS transition parameters).

`TX_FIFO_VC_SIZE` was checked against the driver's encoder
(`dsi_config_tx_fifo()`): `0x13121110` is size 1 at address 0/1/2/3 for VC0/1/2/3
— 4 × 32 entries, the full 128, valid.  QNX's `0x2000` is VC1 = size 2 (64
entries) with every other VC at 0.  Not a bug on our side, just a difference.

## 5. Correction: the "first partial live image" was not our framebuffer

On this boot, with the host streaming, the framebuffer painted **magenta**
(`fb ff00ff`, `fb: 1024x600 32bpp, 2457600 bytes <- 0xff00ff`) and the Hitachi
table sent high-first (`spi tconbe`, all nine words `ok`), the operator saw a
**brief grey flash on half the screen, then black**.  Grey, not magenta.

A panel transient that is grey while the framebuffer is magenta means the LVDS
input is not carrying our content.  Report 127 §5's "left half white, right half
black — the first evidence of our own framebuffer content on the panel" is
therefore **not supported by the observation**: it is compatible with the panel's
own reaction to a TCON write (the same grey/line state QNX's self-test leaves).

Standing observation chain, now consistent: the panel reacts to SPI (TCON words
reach it), the host streams, the bridge is programmed, and **nothing of ours is
visible**.  The half-screen flash is the panel's transient, not a half frame.

## 6. What the bridge really is: register map from a working driver

Source: `https://android.googlesource.com/kernel/msm/+/android-msm-hammerhead-3.4-kk-r1/drivers/video/msm/mipi_tc358764_dsi2lvds.c`
(fetched 2026-10-28; it drives the same chip from DSI generic long writes).

Facts it settles:

* **`0x0520` = `GPIOC` (GPIO Control)**, **`0x0524` = `GPIOO` (GPIO Output)**,
  `0x0528` = `GPIOI` (GPIO Input).  Report 126's behavioural decode was right;
  the names are now confirmed.
* **`0x0580` = `IDREG`, expected `0x00006500`**, read over DSI *before* any
  programming.  So the chip answers generic reads from a cold state.
* **`LV_CFG 0x049C`**: bit 0 = `LVEN`; `0x0003` = "PCLK = DCLK/3, **Dual Link**,
  LVEN"; `0x0103` = "PCLK = DCLK/4, Dual Link, LVEN"; `0x01` = "LVEN" (single
  link).  QNX writes `0x1`.
* **`VP_HTIM1 = (HBP<<16) | HPW`, `VP_HTIM2 = (HFP<<16) | HACT`,
  `VP_VTIM1 = (VBP<<16) | VPW`, `VP_VTIM2 = (VFP<<16) | VACT`** — consistent with
  the field widths in `tc358764.c` for these magnitudes.
* The working driver's order is: **`SYS_RST ← 0xFF`, `msleep(30)`,** then LV mux,
  PPI/DSI, `VPCTRL`, timings, `VFUEN`, and **`LV_CFG` last**.
  QNX's order (which we copy) writes `SYS_RST ← 4` near the end instead.
* `VPCTRL = 0x01000120` in event mode; QNX's `0x03F00120` differs only in
  `VSDELAY` and in bit 16.

Do **not** experiment with `SYS_RST` on the reference (trap 28); a `0xFF` write
on the spare is a last resort, not a first test.

## 7. Where this leaves the display problem

The frame that had been ruling the work — "the panel shows a half frame, so fix
the horizontal timing" — does not survive §1, §2 and §5.  In order of evidence:

1. **Our DSI writes do not reach the bridge** (GPIO36 stuck high; BTA never
   answered).  Every downstream question — TCON byte order, horizontal timing,
   LVDS mapping — is unanswerable until this is fixed.
2. The OMAP side of the link is configured, clocked, locked and error-free, and
   it transmits.
3. Therefore the failure is on the far side of the link: the bridge is not
   listening (power, reset, or PPI/DSI-RX state), or the LP signalling does not
   arrive.

Next experiments, in order — **updated after the results in §7a**:

1. ~~Read `IDREG` over DSI.~~ **Done: `-5` in four different configurations** (§7a).
2. ~~Try the HS transport.~~ **Done: identical to LP.**
3. ~~Start the HS clock first (QNX's null packet), and run the VC setup before
   the bridge writes.~~ **Done: identical.** The reference's ordering is now
   matched as far as the host side can be matched, and the bridge still does
   nothing.
4. **The host's transmit side is now the prime suspect.** Everything measured is
   a *protocol-engine* state (`PACKET_SENT`, FIFO empty, no error bits); none of
   it proves the PHY drove a lane. The open candidates are the PHY's own supply
   (`VDDS_DSI` — our `vdds_dsi` is a dummy regulator), the LP transmitters, and
   `DSI_PHY_CFG5/CFG10` (`0xFF000000` / `0x00000000` here, never compared with
   the reference). This is the question put to the relay channel.
5. Only after the bridge reacts: re-open the TCON byte order and the horizontal
   timing. Both are unanswerable while the bridge receives nothing.

## 7a. Bench results after §7 was written (same session)

Four further images were built and booted.  All of them produced **identical**
results, which is itself the finding.

| image | crc | change | IDREG read | GPIO36 test |
|---|---|---|---|---|
| probe | `0x9FF603AD` | read `IDREG`/`GPIOO`/`SYSSTAT` + full-value GPIO test; HS retry ladder | `-5` ×2, LP and HS | all four patterns read 1 |
| no-RESX probe | `0x55D4488F` | RESX pulse removed | not booted (superseded) | — |
| hs-clock probe | `0x1DA714C9` | `VC_SHORT_PACKET_HEADER(1) = 0` (QNX 0x1788C's null packet) + probe | `-5` | all four patterns read 1 |
| vc-first probe | `0xA1EF5F02` | `dsi_setup_dsi_vcs()` moved **before** the bridge writes, so mainline's DDR-clock-start null packet has run first (QNX's 0x17094 → 0x13e50 order) | `-5` | all four patterns read 1 |

Two things were learned from failures of the experiments themselves:

* A failed BTA **clears `VC_EN` behind the driver's back**: `VC_CTRL(1)` reads back
  as `0x20001DC0` (bit 0 = 0, bit 6 = 1) after one failed read.  The probe now
  re-runs `pb_vc_setup_lp()` before any write, otherwise the probe invalidates the
  experiment that follows it.  (The first probe image did exactly that, which is
  why its LP result is not evidence; the later images fixed it.)
* The null packet on VC1 does complete — `VC_IRQSTATUS(1) = 0x4`, one poll — so
  the earlier "the packet is queued but nothing happens" reading is not about the
  host-side handshake.

One more negative, and it is important: on the `0x1DA714C9` boot the unit had
been power-cycled, and **TabletOS never ran**, so the bridge was at POR.  The
`0xA1EF5F02` boot was made straight from a working TabletOS, with **no RESX pulse
and no power cycle**, and the panel went **black** — so the boot chain clears the
bridge's state on the way into Linux, and "the bridge was already working" is not
a state we can inherit.  (It also means report 127's grey test pattern surviving
into Linux was either a panel-side artefact or did not reproduce.)

## 7b. What the reference actually does first (report 128)

Static RE of `winch_lcdctl` (`docs/reports/128-…`) identifies the reference's
DSI bring-up as three named functions:

| vaddr | name | role |
|---|---|---|
| `0x179dc` | `L1DSS_DSI1_setup_config_QNX` | clocks, timings, PHY, PLL |
| `0x17094` | `L1DSS_DSI1_lp_mode_start_DSI_QNX` | `VC_CTRL(1)` (LP, `VC_EN`), `VC_CTRL(0)` (**HS**, `VC_EN`), FIFO sizes, `DSI_CTRL` (IF_EN 0→1), `COMPLEXIO_CFG1`, then **`SPH(1) = 0` and `SPH(0) = 0` null packets**, each polled for `PACKET_SENT` |
| `0x13e50` | bridge programmer | called at `0x1938c` |
| `0x16bd8` | `L1DSS_DSI1_hs_mode_start_DSI_QNX` | `VC_CTRL(0)` ← `0x20800791` (HS, video), after the bridge |

Its error string is literal: **"Could not send inital null packet to start HS
clock"**.  Two corrections come with it: every write to `VC_CTRL(1)` in the
image has `MODE_SPEED = 0` (LP) — §3 above is confirmed from the code as well as
the TRM — and the bridge is **never** programmed over I²C (the image's only I²C
client talks to `0x4A` on `/dev/i2c0`).

## 7c. What the reference does *not* do (report 130)

The per-panel table is keyed by platform + refresh rate, not by panel identity;
the Hitachi/LG branch selects **only** the 9- vs 11-word SPI table.  `LV_CFG
0x049C` is written exactly once, with **1**, hard-coded: single-link LVDS,
1024x600, identical for both panels.  So the "dual link" explanation for a
half-width image is ruled out by the reference's own code.

## 7d. Two more negative tests, and one like-for-like reference capture

**The rails were the last measured difference between the two systems, and they
are not the cause.**  Read live on both:

| rail | reference (`pmic-twl6030-ctl get_state/get_vout`) | Linux (`/sys/class/regulator`) |
|---|---|---|
| `VAUX1` / `VAUX2` / `VAUX3` | ON, 2800 mV | enabled |
| `VCXIO` | ON, 1800 mV | enabled |
| **`VDAC`** | **ON, 1800 mV** | **disabled** |
| **`VANA`** | **ON, 2100 mV** | **disabled** |
| `VUSB` | ON, 3300 mV | enabled |
| `VMMC` / `VPP` / `VUSIM` | OFF | disabled |

Image `0xE4D4DEC2` marks `VDAC` and `VANA` `regulator-always-on`; both then read
`enabled`, so the two systems now agree on every rail — and the probe result is
**identical**.  (The DTS change is kept: it makes the board match the reference
and costs nothing.)

**The three remaining DSI timing words are not the cause either.**  Image
`0xEC932C5B` forces `TIMING1 = 0x79190999`, `TIMING2 = 0x7FFF7FFF`,
`CLK_TIMING = 0x00000E0B` before the interface is enabled.  The write works —
`VC_CTRL(1)` no longer loses `VC_EN` after a failed BTA, because QNX's `TIMING2`
disables the turn-around timer — and delivery is still `-5`.

**The PHY block now has a reference value during the self-test** (the gap the
previous session flagged).  Captured on TabletOS with `-t 500000 power_on`
running:

```
48044200 0b190d13   48044204 42040a22   48044208 b8800009   4804420c 625d21a0
48044210 625d21a0   48044214 ff000000   48044218 00010000   4804421c 00000000
48044220 00000410   48044224 00020001   48044228 00000000   4804422c 7d20a59a
48044230 04320000   48044234 00000000   48044238 3e080000   4804423c 01400003
```

`CFG0/1/2`, `CFG5 = 0xFF000000` and **`CFG10 = 0x00000000`** are all identical to
ours, so the PHY configuration is not the difference.

**The bridge does not answer on I²C either.**  Its slave address is `0x0F`
(7-bit; the msm driver's header says read/write `0x1F`/`0x1E`).  Read-only
probes: bus 0 → `Remote I/O error` (NAK), bus 1 and bus 2 → `Connection timed
out`.  No positive evidence of a second way in.

**Pad-level comparison** (low half = the pad we care about, high half = its
paired pad in the same 32-bit padconf word):

| pad | reference | ours | verdict |
|---|---|---|---|
| `0x4A100058` gpio_36 | `0x0003_011B` | `0x0108_011B` | low half identical (mux 3, input on) |
| `0x4A100080` RESX? | `0x0119_4003` | `0x0101_4003` | low half identical |
| `0x4A10017C` gpio_171 | `0x4103_0103` | `0x010F_0103` | low half identical |

`GPIO2_DATAIN` is the one GPIO state that still disagrees: reference
`0x08000018` (bits 27, 4, 3), ours `0x01000010` (bits 24, 4).  Bit 24 = GPIO2_24,
which report 122 called RESX; the reference reads it **low** during a working
display, and the datasheet says RESX is "hardware reset, **low active**" (pin
B3).  Both facts cannot hold with GPIO2_24 = RESX, so **report 122's pin
assignment should be treated as unproven again** (this is the open question in
report 125 §5 item 2), and bits 27/3 — inputs on both sides, high only on the
reference — are unexplained.

## 7e. What is left, stated so the next session does not repeat this

Every host-side value that can be compared now matches the reference:
`DSI_CTRL`, `CLK_CTRL`, `COMPLEXIO_CFG1`, `TIMING1/2`, `CLK_TIMING`, the VM
timings, `VC_CTRL(1)`, the PHY block, the PLL, the packet bytes, the VC setup
order, the HS-clock null packet, and every PMIC rail.  Two things do **not**
match and have never been explained:

1. **`clk rate mismatch: 48000000 != 128000000`** from `dss_setup_default_clock()`
   — the DSS functional clock is 48 MHz where 128 MHz was asked for, and the
   reference's DSI domain is 100.8 MHz.  A functional clock below the 37.8 MHz
   byte clock is a plausible way to get "PACKET_SENT, no error, malformed
   waveform", and it is the only *rate* left that is wrong.
2. **`GPIO2_DATAIN` bits 27 and 3** read high on the reference and low here, and
   bit 24 reads the other way round.

Both are worth one session each, and neither is a packet variant.

## 8. Bench state left behind

* Image flashed: `0x5DC2711F` (self-test build).  It is a diagnostic image and
  leaves `0x0524 = 0`; do not treat its panel state as meaningful.
* The sealed IFS was **not** restored in this report's cycle and must be
  restored at session end (`local/images/ifs0.sealed.bin`, sha256 `c72b0530…`).
* `BOOTCTR_FCTRY` read `0x32` (50) immediately before the flash.  `/init`
  auto-reboots every 180 s and each reboot spends a unit (trap 33); re-read the
  counter on the next TabletOS boot.
* The framebuffer was left magenta and the TCON table sent high-first.
