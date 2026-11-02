# 124 — The complete QNX timing set and the QNX PHY words: both forced, still black

**Session 13 (Linux-era session 7), 2026-10-28.**  This report records the two
experiments the session-12 handoff ordered, in the order it ordered them, and
their result.  Both are **negative**: the panel stayed completely black after
the host-side stream became byte-identical to the reference in every field that
was measured.  It also records the additional differences found in a full DSI
register dump and the two live register writes tried after the two builds.

---

## 0. TL;DR

| experiment | change | result |
|---|---|---|
| 1 (handoff §1, ROADMAP 1) | force the complete QNX timing set: `DISPC_TIMING_H`, `DISPC_DIVISOR`, `DSI_VM_TIMING1`, `DSI_CLK_CTRL` | all four read back **exactly** QNX; panel **still black** |
| 2 (ROADMAP 2) | substitute the reference `DSI_PHY_CFG0/1/2` words | all three read back **exactly** QNX; panel **still black** |
| live extra | clear `DISPC_CONTROL` GPOUT0/1 (bits 15/16) to match QNX `0x02000309` | no change |
| live extra | re-send the Hitachi TCON table (9 words) | no change (all words `ok`) |
| live extra | send the alternate LG TCON table (11 words) | no change |

**Conclusion:** the "timing encoding" and "PHY words" hypotheses are
**falsified**.  The host stream now matches the reference in every measurable
field; the difference is downstream of the DSI protocol engine, or in a
parameter that is not a register word (SPI framing of the TCON init, or the
bridge's inherited state).

---

## 1. Experiment 1 — the complete QNX timing set, one build

### 1.1 Why the clocks had to change with the divisor

`DISPC_DIVISOR = 0x00010002` (LCD divisor 2) is only correct with a 100.8 MHz
DISPC/LCD clock.  Mainline's search picks HSDIV m = 4 (151.2 MHz) and divisor
3; forcing divisor 2 against 151.2 MHz would give a 75.6 MHz pixel clock — the
genuine "2× error" the relay warned about.  So the experiment also selects the
reference's **HSDIV post-dividers** (`mX = 6` for both the DISPC and DSI PLL
outputs) while keeping our own REGM/REGN ratio (126/16).  That is a clock
*distribution* change, not forcing QNX's PLL ratio words.

### 1.2 Code changes (all guarded by `of_machine_is_compatible("blackberry,omap4-winchester")`)

`drivers/gpu/drm/omapdrm/dss/dsi.c`:

* `dsi_vm_calc_blanking()`: DSI HBP/HFP/HSA forced to 116/121/1 byte clocks,
  DISPC HBP/HFP/HSW forced to the requested 138/162/20 (register 137/161/19).
  Totals unchanged (238 byte clocks, 320 pixels), so `TL` stays 1008.
* `dsi_proto_timings()`: `DSI_VM_TIMING1.HSA` is written from the stored value
  instead of forced to 0 in burst mode, so the word is `0x01079074`.
* `dsi_vm_calc_hsdiv_cb()`: only accept m = 6 for the DISPC HSDIV.
* `dsi_pll_calc_dsi_fck()`: use mX = 6 for the DSI HSDIV.
* `dsi_set_lp_clk_divisor()`: set `HS_AUTO_STOP_ENABLE` (bit 18).

`arch/arm/boot/dts/ti/omap/omap4-winchester.dts`: `lp-rate = <10080000>` so the
LP divisor computes to 5 (100.8 / 2 / 5 = 10.08 MHz), matching QNX.

Image: `chainloader/kernel.img` crc **`0x286C232F`**.

### 1.3 Measured after boot (`disp stat`, solid framebuffer)

```
DISPC TIM_H 0890a113 TIM_V 00c00c00 POL 00060000 DIV 00010002
DSI CTRL 00eaef9d CLK_CTRL a0346005 VM1 01079074 VM2 04010c0c VM3 03f00258
DSI PLL STATUS 00000383 CFG1 14a0fc1f CFG2 00056008
DSI IRQSTATUS 00000080   COMPLEXIO_IRQ_STATUS 00000000   VC_IRQSTATUS(0) 00000004
```

All four target words match the reference.  `DSI_PLL_CONFIGURATION1` is
`0x14A0FC1F` — QNX's structure (mX = 6/6, DCO 604.8 MHz, txbyteclkhs 37.8 MHz)
with our equivalent 126/16 ratio, which is why it is not QNX's `0x14A5E8BE`
byte-for-byte.

`DISPC_TIMING_H`, `DISPC_DIVISOR`, `DSI_VM_TIMING1` and `DSI_CLK_CTRL` were
read back from the console, not inferred.  **The panel was still completely
black** (operator-confirmed, framebuffer painted `fb ffffff`).

---

## 2. Experiment 2 — the QNX DSI PHY words

Before changing anything, the full `DSI_DSIPHY_CFG*` set was read live and
compared with report 118 §3.3.  Only three words differ:

| register | ours (before) | QNX | 
|---|---|---|
| `DSI_DSIPHY_CFG0` | `0D1D0F16` | `0B190D13` |
| `DSI_DSIPHY_CFG1` | `42040C28` | `42040A22` |
| `DSI_DSIPHY_CFG2` | `B800000A` | `B8800009` |
| `CFG3`/`CFG4` | `625D21A0` | `625D21A0` |
| `CFG5` | `FF000000` | `FF000000` |
| `CFG6` | `00010000` | `00010000` |
| `CFG8` | `00000410` | `00000410` |
| `CFG9` | `00020001` | `00020001` |
| `CFG11` | `7D20A59A` | `7D20A59A` |
| `CFG12` | `04320000` | `04320000` |
| `CFG14` | `3E080000` | `3E080000` |
| `CFG15` | `01400003` | `01400003` |

`dsi_cio_timings()` was extended to write the three reference words after its
own calculation (same board guard).  Image crc **`0xEAAAA372`**.  Read back:

```
DSI PHY CFG0 0b190d13 CFG1 42040a22 CFG2 b8800009
```

byte-identical to the reference.  **Panel still completely black.**

---

## 3. What else a full DSI dump shows, and why it is not the answer

The four-register set did not cover the whole block.  A full read on the
4-lane build gives these differences from report 118 §3 (QNX live):

| register | ours | QNX | assessment |
|---|---|---|---|
| `DSI_SYSCONFIG` | `00000015` | `00000010` | idle-mode bits; not stream content |
| `DSI_TIMING1` | `FFFF1000` | `79190999` | protocol timeouts |
| `DSI_TIMING2` | `FFFFFFFF` | `7FFF7FFF` | protocol timeouts |
| `DSI_CLK_TIMING` | `0000140E` | `00000E0B` | DDR clock pre/post; derived from the PHY constants |
| `DSI_VM_TIMING5` | `00001F00` | `00003F00` | command-mode interleaving (inert in pure video) |
| `DSI_VM_TIMING7` | `01E4001A` | `000000C8` | HS entry/exit latency (interleaving) |
| `DSI_TX/RX_FIFO_VC_SIZE` | `13121110` | `00002000` | FIFO allocation; both VCs work |
| `DSI_VC_CTRL(0)` | `20818FB3` | `20808791` | see below |
| `DISPC_CONTROL` | `00018309` | `02000309` | bits 15/16 GPOUT, bit 25 TDM (see below) |

### 3.1 `DSI_VC_CTRL(0)`

Decoded against TRM Table 10-584, both words have `VC_EN=1`, `MODE=video`,
`CS_TX_EN=1`, `ECC_TX_EN=1` and no DMA requests.  The only writable field that
differs is **`OCP_DATA_BUS_WIDTH` (bits 11:10)**: ours 3 (32-bit), QNX 1
(24-bit).  The `SOURCE` bit (1) also differs (ours 1, QNX 0) but the TRM says
SOURCE is ignored when `MODE=video`.

The TRM (`swpu231ap.pdf.txt:192384-192425`) defines `OCP_DATA_BUS_WIDTH` as the
width of the **L3 interconnect slave port** for write access, and states
explicitly that in video mode *"only the DSI_VC_LONG_PACKET_HEADER_i register is
used. The video port pixels are used for the payload."*  So the field is inert
for a video-port stream and is **not** the cause.  It is recorded because it is
the only decoded VC_CTRL difference and the earlier reports quoted the words
without decoding them.

### 3.2 `DISPC_CONTROL` GPOUT bits

Mainline sets bits 15/16 (GPOUT0/1) to 1,1 for `DSS_IO_PAD_MODE_BYPASS`; QNX
has both clear and also sets bit 25 (`TDMUNUSEDBITS`, TV/DIGIT only).  The live
write `poke 48041040 02000309` matched QNX exactly and changed nothing, so the
I/O-pad mode is not the blocker.

### 3.3 The overlay really is scanning our buffer

An earlier reading at `0x480410A4` was the wrong register.  The TRM places
`DISPC_GFX_ATTRIBUTES` at `0xA0`; the driver's `DISPC_ATTR_OFFSET(GFX)` is
0x20 over base 0x80, i.e. `0x480410A0`.  Live:

```
0x480410A0 = 0x120000B1   ENABLE (bit 0) = 1
0x48041080 = 0x7FDA8000   the framebuffer the console paints
0x48041088 = 0x00000000
0x4804108C = 0x025703FF   1024 x 600
```

So the DISPC is enabled, the GFX overlay is enabled, the raster is advancing,
and the buffer being scanned is the one painted white.  This rules out "the
framebuffer is black / the plane is off" as the explanation for the black
panel.

---

## 4. TCON table, both panel families

Re-sent on the bench with the framebuffer painted white:

* Hitachi 9-word table (`spi tcon`): all nine words `ok`; no change.
* LG 11-word table (`spi w 0029 0025 0430 0840 385f 3ca4 3400 0843 2828 4041 00ad`):
  all words `ok`; no change.  (`GPIO6_DATAIN` read 0, but that register is not
  clocked by any Linux driver and report 118 §6.2 already showed it can read 0
  on a cold panel rail, so it does not prove the unit is an LG panel.)

The console has only ever sent these tables in **SPI mode 0**, 8 bits/word.
SPI mode is the one framing parameter never varied; it is being determined from
`winch_lcdctl` statically (report 123) rather than guessed.

---

## 5. Where this leaves the bring-up

The host stream matches the reference in every field that can be measured:

* 4 lanes, `COMPLEXIO_CFG1 = 0x2A054321`, 37.8 MHz byte clock;
* `DSI_CTRL = 0x00EAEF9D`, `CLK_CTRL = 0xA0346005`, `VM_TIMING1/2/3` equal;
* `DSI_PHY_CFG*` equal;
* DISPC timings, divisor and `CONTROL` equal (after the live poke);
* PLL locked, no COMPLEXIO error, `PACKET_SENT`;
* overlay enabled and scanning the painted buffer;
* backlight on and the panel lit.

The remaining suspects, in order:

1. **The TCON init never reaches the panel** — SPI framing (mode) not yet
   matched to QNX.  Report 123 answers exactly this.
2. **The bridge's inherited state on the factory/CRC path.**  Report 122 proved
   the *IFS* does not program it and the second-stage loader `hd0head.bin` is
   exactly 2 MB with no DSI/DISPC/TC358764 constants; but our path skips the
   loader entirely.
3. **The LVDS mapping** — but see the contradiction note: neither the reference
   nor our kernel programs the bridge, so both use its strap default.  The
   driver's `vesa-24` `data-mapping` is a *no-op* while `tc358764_init()` does
   nothing, so "the driver writes VESA-24 while the panel wants JEIDA" cannot
   be the difference between the reference and us.

Nothing here shows a link error.  A link error can no longer be mistaken for a
panel problem: the DSI health registers are clean on every boot recorded above.
