# 125 — The reference DOES program the TC358764 (the premise of 121 §12/122 was wrong)

**Session 13 (Linux-era session 7), 2026-10-28.**  This report records a
correction that changes the display bring-up: `winch_lcdctl` in the IFS
**does** program the TC358764 on the splash path, with a full register
sequence.  Reports 121 §12 and 122 concluded the opposite ("nothing programs
it, it needs no programming") and the session-12 handoff is built on that
conclusion.  The call was already present in report 122's own decoded splash
sequence (`1938c  0x13e50`) but was not identified as a bridge programmer.

The exact sequence has been recovered instruction by instruction and
implemented in `tc358764.c`.  The panel is still black after it — so this is a
correction and a new starting point, not yet a fix (see §6).

---

## 1. The call site

`winch_lcdctl` display sequencer (`0x19020`), splash path (`mode == 1`),
addresses as in report 122 §7.1, with the branch context now decoded:

```
19330  GPIO2_CLEARDATAOUT = 0x01000000      ; reset asserted
1933c  usleep(20000)
19340  usleep(20000)                        ; reset released at 0x19338
19344  bl 0x17094                           ; DSI second pass
19350  r0 = <display config flag>
1935c  cmp r0,#0 ; beq 0x1938c
19364  bl 0x196dc                           ; bridge/panel probe
19368  cmp r0,#0 ; bne 0x1938c
19370  r3 = <another flag>; cmp r3,#0 ; beq 0x1938c
19388  bl 0x4330
1938c  bl 0x13e50                           ; <-- PROGRAM THE BRIDGE
19390  r0 = <config>; bl 0x16bd8             ; DSI VC enable
```

Every path reaches `0x1938c`; the call is unconditional.  It runs **after**
the DSI PHY/PLL programming and **before** the DSI VC enable — exactly the
ordering a bridge programmer needs.

## 2. The sequence at `0x13e50`

The generic-write helper `0x13ce0(addr, val)` builds a 6-byte DSI generic long
write payload `{addr_lo, addr_hi, v0, v1, v2, v3}` (data type `0x29`) and sends
it on **OMAP DSI VC 1** with packet VC ID 0 — the same transport mainline's
`omap_dsi_host_transfer()` uses (`VC_CMD = 1`, `msg->channel` = 0).  The
payload layout and data type were checked against `tc358764_write()` and match
byte for byte.

The four PPI clear-SIPO values are read from a runtime config word; the object
is at vaddr `0x2e55c` and its first word is **4** (so all four are 4, not the
upstream 5).

| register | value | note |
|---|---|---|
| `PPI_TX_RX_TA` `0x013C` | `4` | upstream writes `0x20003` |
| `PPI_LPTXTIMECNT` `0x0114` | `4` | upstream writes `2` |
| `PPI_D0S_CLRSIPOCOUNT` `0x0164` | `4` | upstream writes `5` |
| `PPI_D1S_CLRSIPOCOUNT` `0x0168` | `4` | |
| `PPI_D2S_CLRSIPOCOUNT` `0x016C` | `4` | |
| `PPI_D3S_CLRSIPOCOUNT` `0x0170` | `4` | |
| `DSI_LANEENABLE` `0x0210` | `0x1F` | 4 data + clock |
| `PPI_LANEENABLE` `0x0134` | `0x1F` | |
| `PPI_STARTPPI` `0x0104` | `1` | |
| `DSI_STARTDSI` `0x0204` | `1` | |
| `0x04A4` | `0` | |
| `LV_PHY0` `0x04A0` | `0x0044C02D` | PHY reset (bit 22) asserted, `PRBS_ON=4`, `IS=3`, `ND=13` |
| `LV_MX0003` `0x0480` | `0x03020100` | VESA-24 mapping, identical to upstream |
| `LV_MX0407` `0x0484` | `0x08050704` | |
| `LV_MX0811` `0x0488` | `0x0F0E0A09` | |
| `LV_MX1215` `0x048C` | `0x100D0C0B` | |
| `LV_MX1619` `0x0490` | `0x12111716` | |
| `LV_MX2023` `0x0494` | `0x1B151413` | |
| `LV_MX2427` `0x0498` | `0x061A1819` | |
| `VP_CTRL` `0x0450` | `0x03F00120` | `VSDELAY=1008`, `RGB888`, `EVTMODE`; no HSPOL/VSPOL |
| `VP_HTIM1` `0x0454` | `0x78000001` | |
| `VP_HTIM2` `0x0458` | `0x76000400` | `HACT=1024` |
| `VP_VTIM1` `0x045C` | `0x000A0001` | `VBP=10`, `VSYNC=1` |
| `VP_VTIM2` `0x0460` | `0x000C0258` | `VFP=12`, `VACT=600` |
| — `usleep(100 ms)` — | | PHY reset settle |
| `LV_PHY0` `0x04A0` | `0x0004C02D` | reset released, same range |
| `LV_CFG` `0x049C` | `0x00000001` | **`LVEN`** only; upstream writes `0xD` (adds `CLKPOL1|CLKPOL2`) |
| `0x0520` | `5` | |
| `0x0524` | `5` | |
| `SYS_RST` `0x0504` | `4` | `SYS_RST_LCD`; **never `0x1f`** |
| `VP_VFUEN` `0x0464` | `1` | frame-timing update enable |

Two differences matter most, because they are the ones upstream gets wrong for
this panel: **`LV_CFG = 1`** (no LVDS clock-polarity inversion) and
**`LV_PHY0.IS/ND = 3/13`** (the LVDS PHY frequency range).  Upstream uses
`0xD` and `IS(2)/ND(6)`.

The datasheet (`/home/psyden/rea-work/TC358764XBG.PDF`, §Features, "Power
Consumption") independently names the two control bits this depends on:
`LV_CFG` `0x049C[0]` is the LVDS enable, and `0x04A0[8]` is the PLL
power-down.  The mapping registers and `SYS_RST_LCD` are the same as upstream.

## 3. The implementation

`tc358764_init()` had been replaced by a "do not program the bridge" stub.  It
now writes the table above through `tc358764_write()` (which logs the first
`-EIO` once and keeps going — the packet is transmitted, only the BTA ack
fails).  It runs from `pre_enable`, after the 20 ms `GPIO2_24` pulse.  The
driver keeps `pre_enable_prev_first = true`, so this happens before the DSI
host's `.enable` and before the video VC is switched to video mode — the same
phase as QNX.

Images:

| image | crc | contents |
|---|---|---|
| bridge init | `0x2964FC3F` | exact sequence, no read-back |
| bridge init + probe | `0x070E35EE` | above + `SYS_ID` read after programming + console `spi tconbe` |

Boot log (0x070E35EE):

```
tc358764 …: write 0x013c <- 0x00000004 failed: -5 (continuing)
tc358764 …: SYS_ID after programming: 0x00000000
tc358764 …: programmed the bridge with the reference sequence
```

`SYS_ID` reading 0 is **not** evidence that the sequence failed: a read needs
the bridge to answer BTA, and the host still gets no BTA ack (report 121 §7.5
showed the same with every earlier attempt).  A non-zero value would have been
positive proof; zero is inconclusive.

## 4. SPI / TCON, from the same static sweep (report 123)

The QNX TCON link is **SPI mode 0**, 8 bits/word, MSB-first, **high byte
first**, requested 450 kHz / actual 375 kHz, MCSPI4 CS0 active low, CS per
word, 100 ms after word 0, word 2 and the last word.  The Linux console had
been sending the low byte first; the earlier "both byte orders" test was made
while the LVDS output was still disabled, so it could not have shown an image.
The console now has `spi tconbe` (high byte first, reference gaps) and its
argument limit was raised so a nine-word `spi w` is not truncated to six.

Bench: `spi tconbe` reports all nine words `ok`; the panel is still black.

## 5. What the negative result means

The two experiments the session was ordered to run (the four-register timing
set and the QNX PHY words) were both negative; see report 124.  With this
report the bridge is no longer in the "untouched by design" category: the
reference programs it, and our driver now sends the same sequence.  The panel
is still black, so at least one of these is true:

1. **The writes are not being received by the bridge.**  The packet format and
   VC match QNX, but the bridge's DSI RX may need its RESX released correctly,
   or the OMAP command-VC path may deliver while the video VC is already
   active in a way that differs from QNX's pre-VC-enable window.  `SYS_ID = 0`
   is consistent with this but does not prove it.
2. **`GPIO2_24` is not the bridge's RESX.**  Report 122 rated this "medium"
   confidence; the datasheet says RESX is a dedicated pin.  If the bridge is
   held in reset by a rail/POR the bootloader releases on the reference and
   our path skips, nothing we write will land.  `GPIO36/171` reading high is
   necessary for QNX's self-test but is not a reset-state proof (the balls can
   be pulled up).
3. **The LVDS output is on but the panel still needs something else** (the
   TCON sequence, its rail, or a different mapping).  The mapping is not it:
   QNX writes the same `LV_MX` words upstream does, so "VESA-24 vs JEIDA" is
   not the difference between the reference and us.

## 6. Next experiments, in order

1. **Prove write delivery without the panel.**  QNX's self-test at `0x196dc`
   programs the bridge's own GPIOs over DSI and reads them on the OMAP side
   (`GPIO36` = `GPIO2_DATAIN` bit 4, `GPIO171` = `GPIO6_DATAIN` bit 11).  Run
   that sequence from Linux and watch `GPIO2_DATAIN` change.  If it does not
   change, the problem is delivery/RESX, not the video stream.  This is the
   single most informative next test.
2. If delivery works, compare the **video-mode packet type / VC state** at the
   moment the bridge is programmed: QNX programs before `VC_EN` and leaves
   `VC_CTRL(0)` in command mode until step 9; ours may already have the video
   VC enabled by the `dsi_enable()` triggered by the first generic write.
3. Re-examine the **RESX pin** (datasheet pin D4) against the board schematic
   if one can be obtained; `GPIO2_24` is a 20 ms pulse but its net is not
   named by anything in the boot set.
4. Only then revisit the LVDS mapping / panel rail.

## 7. Corrections to earlier documents

* Report 121 §12 and report 122's conclusion "nothing programs the bridge / it
  needs no programming" are **wrong**.  What the RE found was that the bridge
  is not programmed *by the boot ROM or the second-stage loader* and that it
  is not programmed over I²C; the IFS programmer is at `0x13e50` and is called
  unconditionally by the splash sequencer.
* The 2026-11-15 relay's "the IFS splash proves it works with the bridge
  untouched" is also wrong: the splash *is* the bridge programmer.  The
  observation that the panel lights on the reference is explained by that
  call, not by a power-on default.
* `docs/contradictions/111-vs-122-bridge-programming.md` has been updated to
  record this.

## 8. Bench state left behind

The sealed IFS was restored and verified
(`c72b0530…`, `BOOTCTR_FCTRY = 0x32`).  The test images are in
`local/images/`: `kernel-0x070E35EE-bridge-init-diag.img` and the earlier
`kernel-0x2964FC3F` build was overwritten by it.
