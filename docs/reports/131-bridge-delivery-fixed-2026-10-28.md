# 131 — The bridge receives our writes: the missing step was the VC0 HS null packet

**Session 14, 2026-10-28.**  This report records the fix for the delivery problem
that blocked reports 125–130, and the state of the *next* problem.

Image: `kernel-0xD10FD1C7-ocp24b.img` (CRC `0xD10FD1C7`).

---

## 1. The fix

The TC358764 accepts nothing — no generic write, no generic read, no BTA — until
the **DSI HS clock is running**.  The reference starts it, before it programs the
bridge, with two MII null packets at the end of
`L1DSS_DSI1_lp_mode_start_DSI_QNX` (`winch_lcdctl 0x17094`, report 128 §1.1):

```
17884  add r3,r3,#0x130            ; VC_SHORT_PACKET_HEADER(1)
1788c  str r2,[r3]                 ; SPH(1) = 0   (VC1, LP)
       TAAL_CheckPktSent(55, 1, 0x1E8480)
1790c  add r3,r3,#0x110            ; VC_SHORT_PACKET_HEADER(0)
17914  str r1,[r3]                 ; SPH(0) = 0   (VC0, HS)
       TAAL_CheckPktSent(55, 0, 0x1E8480)
```

and the string for failure is literally *"Could not send inital null packet to
start HS clock"*.

The packet that matters is the **VC0 one**, and VC0 must be in **command mode
with `MODE_SPEED = 1` (HS)** when it is written.  The VC1/LP null packet that
mainline already sends (`dsi_setup_dsi_vcs()`) cannot start an HS clock.

Implemented in `pb_start_hs_clock()`:

```c
r = dsi_read_reg(dsi, DSI_VC_CTRL(VC_VIDEO));
r &= ~BIT(4);            /* command mode — NOT video mode */
r |=  BIT(9) | BIT(0);   /* MODE_SPEED = HS, VC_EN */
dsi_write_reg(dsi, DSI_VC_CTRL(VC_VIDEO), r);
dsi_if_enable(dsi, 1);
dsi_write_reg(dsi, DSI_VC_IRQSTATUS(VC_VIDEO), DSI_VC_IRQ_PACKET_SENT);
dsi_write_reg(dsi, DSI_VC_SHORT_PACKET_HEADER(VC_VIDEO), 0);
/* poll PACKET_SENT */
```

The read-modify-write matters: a whole-word store of the command-VC value
(`0x20009F81`) clobbers VC0's DMA/no-DMA fields (bits 29:27 and 23:21) and its
`OCP_DATA_BUS_WIDTH` (bits 11:10), and that broke the video VC.  An earlier
attempt also used `0x20800791` — the *later* `hs_mode_start` word, whose bit 4
= 1 selects video mode, in which a short-packet write never completes.

## 2. The result

```
pb hs-clock: null packet on VC0/HS -> irqstatus=0x00000004 polls=1
pb hs-clock: null packet on VC1/LP -> irqstatus=0x00000004 polls=1
pb probe before-seq:  IDREG r=-5 0x00000000        (bridge was reset by boot)
pb probe after-seq:   IDREG r=0 0x00006500         <- TC358764 chip ID
                      GPIOO r=0 0x00000005
pb gpio test 0x0524=1 -> GPIO36=1 want 1
pb gpio test 0x0524=4 -> GPIO36=0 want 0
pb gpio test 0x0524=5 -> GPIO36=1 want 1
pb gpio test 0x0524=0 -> GPIO36=0 want 0
```

`IDREG = 0x00006500` is the chip's own ID (`TC358764XBG_ID` in the msm driver)
and it is read back **over DSI**, so the bridge's register interface, the BTA
path and the whole command transport work.  The GPIO36 test then passes all four
patterns, which is the reference's own delivery check (report 126).
**Delivery is fixed.  This closes the blocker of report 129 §7.**

Before the fix, `IDREG` returned `-5` (no BTA) in every configuration; after it,
the read succeeds on the first attempt.

## 3. The bridge's configuration now reads back correctly

Read back over DSI after programming (image `0x2DF88C1A`):

| register | written | read back | verdict |
|---|---|---|---|
| `LV_CFG 0x049C` | `1` | `00000001` | LVDS enabled |
| `LV_PHY0 0x04A0` | `0x0004C02D` | `0004c02d` | ok |
| `VP_CTRL 0x0450` | `0x03F00120` | `03f00120` | ok |
| `VP_VTIM1 0x045C` | `0x000A0001` | `000a0001` | ok |
| `VP_VTIM2 0x0460` | `0x000C0258` | `000c0258` | ok |
| `VP_VFUEN 0x0464` | `1` | `00000001` | ok |
| `GPIOC 0x0520` | `5` | `00000005` | ok |
| `GPIOO 0x0524` | `5` | `00000005` | ok |
| `DSI_LANEENABLE 0x0210` | `0x1F` | `0000001f` | ok |
| `IDREG 0x0580` | — | `00006500` | alive |
| `VP_HTIM1 0x0454` | `0x78000001` | `00000001` | **upper half not stored** |
| `VP_HTIM2 0x0458` | `0x76000400` | `00000400` | **upper half not stored** |
| `PPI_STARTPPI 0x0104` | `1` | `00000000` | self-clearing or not taken |
| `DSI_STARTDSI 0x0204` | `1` | `00000000` | self-clearing or not taken |
| `SYS_RST 0x0504` | `4` | `00000000` | self-clearing |

The last four rows are the same values QNX's sequence writes, so they are
probably write-only/self-clearing *start* bits and a 16-bit register pair; they
are recorded as observation, not as a fault.

## 4. What is still not working: HS video does not reach the bridge

With the bridge programmed and the host streaming (DISPC `LINE` advancing,
`VC_CTRL(0) = 0x208087B3` against the reference's `0x20808791` — only the
ignored `SOURCE` bit and the FIFO-status bit differ), the panel is still black
and the bridge reports:

```
pb late bridge: 0214=00000000 0218=00000000! 0220=90400883 0300=00000000! 0500=00000000 049c=00000000! 0230=ffffffff
```

* `DSI_LANESTATUS0 (0x0214) = 0` — **no lane is in HS RX mode.**  LP command
  packets reach the bridge; HS video does not.
* The reads marked `!` failed (BTA timeouts).  During video mode the command VC's
  packets are only sent in the blanking windows `DSI_CTRL[23:21]` allow, so a
  late check is unreliable — that part of the measurement needs care.

Two image builds were made to chase this and both are negative:

* `0xA3A24D73` — `OCP_DATA_BUS_WIDTH` set to 24-bit (the reference's value) in
  the timing block: **overwritten** by `pb_start_hs_clock`'s whole-word store.
* `0xD10FD1C7` — the same fix applied *after* `dsi_setup_dsi_vcs()`, with the
  read-modify-write above: `VC_CTRL(0)` then reads `0x208087B3`, matching the
  reference, and the panel is still black.

So the video VC now matches the reference and HS still does not arrive.  The
next suspects, in order:

1. **The LVDS side of the bridge** — `SYSSTAT 0x0500` reads 0; if that carries
   the LVDS PLL lock, the bridge may be receiving video and failing to drive
   LVDS.  The TC358764 datasheet we have has no register map; this needs the
   full one (see §6).
2. **The clock lane in HS.**  `DDR_CLK_ALWAYS_ON` is set and `COMPLEXIO_CFG1`
   matches the reference, but "the DDR clock is on" is an internal signal; the
   lane-level HS driver enable is not visible in any register we have.
3. **The DISPC→DSI video path**, if `LANESTATUS0` is being read during a
   window in which no HS burst is in flight.

## 4a. Measurements taken with the live bridge hook

Image `0x0E55D92E` adds a sysfs hook so the bridge can be driven from the
console without a boot per hypothesis (the console's `write` passes only its
first token, so the format has no spaces):

```
write /sys/bus/platform/devices/58004000.encoder/pb_reg w0524:0     # write
write /sys/bus/platform/devices/58004000.encoder/pb_reg r0214       # read
kmsg pb reg
```

Results on a cold-booted unit (bridge reset by the boot chain, delivery then
working: `IDREG -5 -> 0x00006500`):

| measurement | value | meaning |
|---|---|---|
| live write `0x0524 = 0` | `GPIO2_DATAIN` 1 -> 0 | the hook and the write path work interactively |
| `DISPC LINE_STATUS` sampled at 1 ms | `162 -> 199 -> 237` | **37.5 lines/ms**, i.e. ~37.4 kHz — the 50.4 MHz pixel clock is correct, so the DISPC is *not* running from the 48 MHz FCK |
| `LV_CFG 0x049C` | `1` | LVDS enable is set |
| `SYSSTAT 0x0500` | `0` (5 reads) | constant |
| `DSI_LANESTATUS0 0x0214` | `0` (24 samples + repeats) | no lane in HS RX mode |
| `DSI_LANESTATUS1 0x0218` | `0x1D` | lanes in ULPS/STOP |
| `DSI_INTSTATUS 0x0220` | `0x90400883` | undecoded, no register map |
| `CLW_DPHYCONTRX 0x0020` | `0xA002` | the only non-zero D-PHY RX word |
| `COM_DPHYCONTRX 0x0038`, `D0W_CNTRL 0x0044` | `0` | POR defaults |

**The panel does not react to LVEN at all.**  With the framebuffer bright green,
`LV_CFG` was toggled `1 -> 0 -> 1` three times, 2 s apart.  The operator
reported no change whatever.  The same panel *does* react to SPI (the TCON table
in the unprogrammed state gave the grey flash), so the panel itself is alive and
listening on its own bus.

That is consistent with one story: the bridge's DSI receiver never locks to the
HS stream (`LANESTATUS0 = 0`), the LVDS PLL is fed from that stream, so the LVDS
clock never runs, and toggling `LVEN` changes nothing the panel can see.

## 4b. Where the remaining fault sits

Since `LINE_STATUS` shows a correct pixel rate, the DISPC and the DSI packet rate
are right; the loss is between the OMAP PHY and the bridge's receiver.  The
candidates, with what they need:

1. **The bridge's D-PHY RX configuration** — `CLW_DPHYCONTRX 0x0020 = 0xA002` is
   the only non-default word in the 0x0004–0x0050 block.  The reference's own
   sequence (report 125) never writes these either, so on the reference they are
   in whatever state the **bootloader** left them.  Our bridge is reset on every
   boot, so it runs on POR defaults here.  This is the leading suspect and it
   needs the register map.
2. **The bridge's LVDS-side supplies** (`VDD_LVDS*_33`, `VDD_LVDS*_12`) are dummy
   regulators in the DTS; nothing in the kernel enables or checks them.
3. The panel's own power/enable chain, which we have never identified.

## 4c. Everything else that was tried, and what it ruled out

All of these left the symptom unchanged (`LANESTATUS0` never shows HS RX, panel
black).  They are recorded so the next session does not repeat them.

| change | image | result |
|---|---|---|
| RESX pulse restored (the reference pulses it before programming) | `0xDED99C5E` | no change; delivery still `IDREG 0x00006500` |
| pads `0x078`/`0x07A` set as GPIO outputs like the reference (see §4d) | `0x5BCC1863` | pad word now reads `0x410B4103`, matching the reference; no change |
| `VM_TIMING6` forced to `0x000000C8` (reference: `BL_HS_INTERLEAVING` 0, `BL_LP_INTERLEAVING` 200; mainline computes 486/26) and `VM_TIMING7` to `0x000B0009` | `0x48048809` | register now matches; no change |
| OMAP DSI HDVPSS `CONTROL_DSIPHY` (`0x4A100618`) | — | **matches the reference exactly**: `0x1FF80000` on both |

Confirmed along the way:

* the host really is streaming: clearing `VC_IRQSTATUS(0)` (`PACKET_SENT`) and
  re-reading shows it set again within 5 ms, twice;
* the bridge's D-PHY block is a sane POR state, not zeroed —
  `CLW_DPHYCONTRX 0x0020 = 0xA002`, `D0W/D1W 0xA002`, `PPI_LINEINITCNT 0x0110 =
  0x208E`, and mainline's working gma500 `tc35876x` driver writes none of them
  either, so they are not expected to need programming;
* `PPI_LANEENABLE 0x0134 = 0x1F` and `PPI_TX_RX_TA 0x013C = 4` hold what we wrote;
* `DSI_LANEENABLE` and `PPI_STARTPPI`/`DSI_STARTDSI` read back 0 after a write of
  1 — consistent with self-clearing *start* bits.

**The I²C oracle is closed.** `tools/qnx-i2c.c` was rebuilt against the QNX 6.6
toolchain and run on TabletOS: `pbi2c probe <bus> 0x0f` returns **NAK on
/dev/i2c0, /dev/i2c1 and /dev/i2c3** — the bridge's I²C slave is not wired (the
msm header notes it is optional).  Note also that QNX's third bus is at
`0x48350000`, which our Linux DTS does **not** enable at all (our third is
`0x48060000`), so an I²C-only device on that bus would be invisible to Linux
regardless.

## 4d. A real board difference that was missed: pads 0x078/0x07A

The earlier pad comparison used `0x4A100060/0x068/0x070/0x088` only.  The full
display group, read on TabletOS vs this kernel:

| word | reference | ours (before) |
|---|---|---|
| `0x4A100060` | `0x0003410B` | same |
| `0x4A100064` | `0x000F000F` | same |
| `0x4A100068` | `0x00030003` | same |
| `0x4A10006C` | `0x000F000F` | same |
| `0x4A100070` | `0x41030003` | same |
| `0x4A100074` | `0x41030103` | `0x41034103`? (unverified) |
| **`0x4A100078`** | **`0x410B4103`** | **`0x011F011F`** |
| `0x4A10007C` | `0x00030003` | same |
| `0x4A100080` | `0x01194003` | `0x01014003` (paired pad only) |
| `0x4A100084` | `0x011B0119` | — |
| `0x4A100088` | `0x00030003` | same |

Pad `0x078` = `gpmc_ncs2` = **gpio_52 (GPIO2_20)** and `0x07A` = `gpmc_ncs3` =
**gpio_53 (GPIO2_21)** (TRM CORE pad table).  The reference configures both as
GPIO (`mux 3`, no pull, input disabled) with `GPIO2_OE` bits 20/21 = 1, i.e.
high-Z; we left them in **safe mode with the pull-up enabled**, i.e. pulled
high.  The board DTS now sets the pads to the reference's values.

Whether this matters is unresolved: driving them low (`0x5BCC1863`) and leaving
them as the reference does both gave a black panel, so it is likely a
parked/unused ball.  It is recorded because it is a genuine mismatch that the
earlier comparison could not have seen.

## 4e. The session-13 "full white" discriminator, run

Report 129 left one loose end worth closing: session 13 got a full-white screen
with `spi tcon` (low-first) and the framebuffer painted white.  The previous
session's agent proposed the decisive variant: paint **magenta first**, then send
the **low-first** table, and see which colour survives.

Result: **the panel stayed black** — neither magenta nor white.  That is a third
outcome, not one of the two the test was designed to separate:

* it is **not** magenta, so the bridge did not push a frame of our content — the
  "writes-don't-land" conclusion stands (and delivery now works, so this is
  about the video path, not the command path);
* it is **not** the session-13 white either, and the one thing that changed
  between the two is that **the bridge is now actually programmed** (`LV_CFG =
  1`, LVDS PHY released).  White with the bridge unprogrammed and black with it
  programmed is consistent with: unprogrammed → no LVDS clock → the TCON holds
  its white state; programmed → an LVDS clock is present but the data/timing is
  not usable → the panel blanks.

This still does not settle the low-first/high-first question
(`docs/contradictions/111-vs-123-tcon-byte-order.md`): the panel reacted to
neither order in the programmed state.

## 4f. Why the fault looks like the clock lane

Worth stating because it narrows the next attempt:
**LP command packets reach the bridge, and LP travels the same four data lanes.**
So the data lanes are connected and the bridge's LP receivers work.  What only
HS needs, and LP does not, is the **clock lane** at the HS rate (~151 MHz for
302 Mb/s per lane) and the receiver's PLL locking to it.  A dead or
non-switching clock lane would produce exactly what we see: LP commands fine,
`LANESTATUS0` never reporting HS RX, no LVDS clock, black panel.

Supporting state, all read on both sides:

* OMAP: `COMPLEXIO_CFG1 = 0x2A054321`, `DSI_PHY_CFG0/1/2` and
  `PLL CFG1/CFG2 = 0x14A5E8BE/0x00056000` identical to the reference;
  `PLL_STATUS = 0x383` with `M4_CLOCK_ACK` (PHY clock) and `M5_CLOCK_ACK`
  (protocol engine clock) both set, `BYPASSACKZ = 1` (PLL outputs in use).
* `CM_DSS_CLKSTCTRL` reference `0x0F02` vs ours `0x0702`, and
  `CM_DSS_DSS_CLKCTRL` reference `0x0F02` vs ours `0x040502` — the deltas are
  `OPTFCLKEN_TV_FCLK` (TV, unused here) and the read-only `STBYST`/`IDLEST`
  status.  Nothing that gates the DSI.
* two cheaper checks are ruled out: the host is streaming (`PACKET_SENT`
  returns within 5 ms of being cleared) and the DISPC pixel rate is correct
  (37.5 lines/ms).

**Measuring the clock lane is the next step that would settle it**, and it needs
hardware: a scope or a differential probe on the DSI clock pair.  There is none
on this bench, and the unit has no known test point.

## 4g. Upstream's `tc358764.c` disagrees with QNX on every PPI timing value

Mainline's own driver for this chip (`drivers/gpu/drm/bridge/tc358764.c`,
fetched 2026-10-29) writes the same registers as the reference but with
different values.  Four independent drivers:

| register | mainline | QNX (**ours**) | Qualcomm msm | gma500 `tc35876x` |
|---|---|---|---|---|
| `PPI_TX_RX_TA 0x013C` | `0x00020003` (`TTA_GET 0x20000 \| TTA_SURE 3`) | **`0x00000004`** | `0x00040004` | `0x00040006` |
| `PPI_LPTXTIMECNT 0x0114` | `LPX_PERIOD = 2` | **4** | 4 | 4 |
| `PPI_DnS_CLRSIPOCOUNT` `0x0164…0x0170` | 5 | **4** | 3 | 1 |
| `MIPI_DSI_MODE_LPM` in `mode_flags` | **set** | not set (removed in session 13) | n/a | n/a |

Three of these are "how many LP clocks does the peripheral need" numbers and the
reference demonstrably works with its own values, so they are not obviously
wrong.

**`PPI_TX_RX_TA` is different in kind: it is the bus turn-around timing**, and it
is the one value that should govern whether the bridge can answer a read.  Ours
(`4`, i.e. `TXTAGOCNT = 0`, `TXTASURECNT = 4`) is far smaller than all three other
drivers, and **our bridge reads are intermittent**: `0x0214`, `0x0300`, `0x0500`
and `0x0110` each returned `-5` on some attempts and values on others within the
same boot, and `0x0214` read successfully 24 times in a row in one sampling loop
and failed minutes later.  A too-tight turn-around window is a plausible cause.

**Worth testing next** (cheap, and it would make every future diagnostic
reliable): raise `PPI_TX_RX_TA` to `0x00040006` (gma500's value, which has both
`TXTAGOCNT = 4` and `TXTASURECNT = 6`) or mainline's `0x00020003`, then re-run the
lane/status reads and see whether the `-5` failures disappear.  This does not
touch the panel path — only the bridge's ability to answer us.

## 5. Method notes that cost real time

* A failed BTA **clears `VC_EN`** on that VC behind the driver's back
  (`VC_CTRL(1)` reads `0x20001DC0`).  Any probe must re-establish the VC before
  the writes it is measuring.
* Writing `SPH(vc)` in **video mode never completes** — `PACKET_SENT` times out
  after 2000 polls.  Use command mode for null packets.
* Moving `dsi_setup_dsi_vcs()` ahead of the bridge programming stopped the
  DPSC scanout (`LINE_STATUS` stuck at 0).  Keep the original order and start
  the HS clock explicitly instead.
* Whole-word stores to `VC_CTRL(0)` destroy fields set by
  `dsi_vc_initial_config()`; always read-modify-write.

## 6. Evidence needed from outside this workspace

* **The full TC358764XBG register map.**  The copy in `hardware/` is the 25-page
  summary — no register set.  Needed for: `SYSSTAT 0x0500` (does it carry the
  LVDS PLL lock?), `DSI_INTSTATUS 0x0220` (we read `0x90400883` and cannot
  decode it), `DSI_LANESTATUS0/1`, and the `VP_HTIM1/2` field layout.
* A working driver that reads them, if one exists.
