# 118 — Live QNX display register capture (2026-10-27, session 5)

**This supersedes the *inferred* half of 111.**  Instead of reverse-engineering
`winch_lcdctl+0x1cce0` for the H/V assignment, the DSI PLL values and the PHY
timings, the reference OS was made to run its own display bring-up and every
register it programmed was read back **from the live device**.  Raw capture:
`playbook-live-spare-2026-10-26-clean/qnx-display-live-2026-10-27.txt`.

Nothing was flashed, nothing mounted, nothing written to media.  The only
device-side write was to PRCM (see §1) plus the stock
`/proc/boot/winch_lcdctl -s splash`, which is what TabletOS itself runs at
every boot.

---

## 0. TL;DR

| question (111 R1) | answer | how |
|---|---|---|
| H/V assignment of the two blanking triples | **H = {137, 161, 19} → HBP/HFP/HSW (register values, i.e. n−1); V = {12, 12, 0} → VBP/VFP/VSW.**  Actual porches: **HBP 138, HFP 162, HSW 20; VBP 12, VFP 12, VSW 1** | `DISPC_TIMING_H=0x0890A113`, `DISPC_TIMING_V=0x00C00C00`, decoded with mainline's exact `_dispc_mgr_set_lcd_timings()` encoding |
| Timings / pixel clock | **1024×600, Htotal 1344, Vtotal 625 ⇒ 50.400 MHz at 60 Hz** | arithmetic on the read-back porches |
| DSI PLL words | **CFG1 `0x14A5E8BE` (REGM 756, REGN 95, M4 6, M5 6), CFG2 `0x00056000`, STATUS `0x00000383`** | live read; decode per TRM Tables 10-620/10-622 + mainline `dss_pll` encoding |
| DSI PHY words | **CFG0 `0x0B190D13`, CFG1 `0x42040A22`, CFG2 `0xB8800009`, CFG3/4 `0x625D21A0`, CFG5 `0xFF000000`, CFG6 `0x00010000`, CFG8 `0x00000410`, CFG9 `0x00020001`, CFG11 `0x7D20A59A`, CFG12 `0x04320000`, CFG14 `0x3E080000`, CFG15 `0x01400003`** | live read — **these were previously unreadable** (the page aborts until the PLL is up) |
| Polarities | **`DISPC_POL_FREQ = 0x00030000`** → ONOFF=1, RF(sync posedge)=1, everything else 0: HSYNC/VSYNC/DE **not inverted**, pixel data on **posedge**, `ALIGN` **off** | live read + mainline field map |
| DISPC channel | **`DISPC_CONTROL = 0x02000309`**: `LCDENABLE=1`, `TVENABLE=0`, `GOTV=0`, `STNTFT=1` (TFT), `TFTDATALINES=3` (24-bit).  ⇒ **DSI1 is fed by the LCD manager, not the TV/DIGITAL manager** | live read (corrects 111 §3.1) |
| Panel | **Hitachi** (`sloginfo` says `LCD is Hitachi` on every boot and every `screen` wake; one cold run of `winch_lcdctl` said `LCD is LG` — see §6.2).  Use the **9-word** SPI table | `sloginfo` |

Two facts that were not in any report and change the plan:

1. **The DSS power domain is a separate switch** (`PM_DSS_PWRSTCTRL =
   0x4A307100`, `POWERSTATE[1:0]`).  With it at 0 the module cannot wake no
   matter what you write to `CM_DSS_DSS_CLKCTRL` — `MODULEMODE` latches but
   `IDLEST` stays 3 (**disabled and cannot be accessed**), and every DISPC/DSI
   access SIGBUSes.  This is the real answer to "why does `0x48044200` abort".
2. **`winch_lcdctl` does not enable its own clocks.**  Run it with the domain
   off and it dies with SIGBUS at `_btext+0xde1c`.  Enable the domain and the
   module first and the identical binary runs to completion.  111 §5.3 guessed
   this; it is now measured.

---

## 1. The R4 recipe, verified on hardware

TabletOS was fully booted but had powered the display down (typical idle
blanking).  Initial state:

```
4a009100 CM_DSS_CLKSTCTRL   00000003   CLKTRCTRL=HW_AUTO
4a009120 CM_DSS_DSS_CLKCTRL 00070000   MODULEMODE=0 (DISABLED), IDLEST=3
4a307100 PM_DSS_PWRSTCTRL   00030000   POWERSTATE=0 (OFF)
4a307104 PM_DSS_PWRSTST     00000000   state OFF
```

Writing only `MODULEMODE=2` did **not** work (`IDLEST` stayed 3).  The working
order is power domain → clock domain → module:

| step | register | write (mask) | readback | decode |
|---|---|---|---|---|
| 1 | `PM_DSS_PWRSTCTRL` `0x4A307100` | `3` (mask `3`) | `00030003` | POWERSTATE=ON |
| — | `PM_DSS_PWRSTST` `0x4A307104` | — | `00000037` | ON, LOGICSTATEST=1, MEMSTATEST=3 |
| 2 | `CM_DSS_CLKSTCTRL` `0x4A009100` | `2` (mask `3`) | `00000F02` | CLKTRCTRL=2 SW_WAKEUP + all four CLKACTIVITY bits |
| 3 | `CM_DSS_DSS_CLKCTRL` `0x4A009120` | `0xF02` (mask `0xF03`) | `00040F02` | MODULEMODE=2, IDLEST=0, STBYST=0, OPTFCLKEN_DSSCLK/48MHZ/SYS/TV all 1 |

`0x00040F02` and `0x00000F02` are **exactly** what 112 read from the kernel on
the Linux path, i.e. this reproduces the state the bootloader hands to Linux.
For the port: `&dss { status = "okay"; }` plus `power-domains = <&prm_dss>` (the
node already exists in `omap4.dtsi`) is what makes mainline do steps 1–3; the
raw pokes above are the bring-up shortcut.

`memw32` (workspace `rea-work/memw32`, QNX ARM binary) was the write tool.  It
is now deployed at `/accounts/devuser/memw32`.  **This is new device state:**

> `/accounts/devuser/memw32` exists on the unit (8 121 B, mode 755).  It is a
> register **write** tool.  It is only for PRCM/CLOCK pokes; never point it at
> RPMB, the eMMC, or BOOTCTR.

---

## 2. DISPC — programmed values and decode

Live, while `winch_lcdctl -s splash` was running:

```
48041000 DISPC_REVISION      00000040
48041010 DISPC_SYSCONFIG     00001008
48041014 DISPC_SYSSTATUS     00000001   RESETDONE
48041018 DISPC_IRQSTATUS     000000A2   VSYNC1(1) | PROGRAMMEDLINENUMBER(5) | GFXEND(7)
4804101c DISPC_IRQENABLE     00000000
48041040 DISPC_CONTROL       02000309
48041044 DISPC_CONFIG        00000004
4804105c DISPC_LINE_STATUS   000000E5 -> 0000016C (1 s apart; scanning)
48041064 DISPC_TIMING_H      0890A113
48041068 DISPC_TIMING_V      00C00C00
4804106c DISPC_POL_FREQ      00030000
48041070 DISPC_DIVISOR       00010002
48041078 DISPC_SIZE_DIGIT    00000000   (TV/DIGIT manager unused)
4804107c DISPC_SIZE_LCD      025703FF
48041088 DISPC_GFX_POSITION  010C0109
48041080 DISPC_GFX_BA0       AFEF0000
48041084 DISPC_GFX_BA1       AFEF0000
4804108c DISPC_GFX_SIZE      003F01ED
480410a4 DISPC_GFX_ATTRIBUTES 04000200
480410b0 DISPC_GFX_ROW_INC    00000001
480410b4 DISPC_GFX_PIXEL_INC  00000000
48041238 DISPC_CONTROL2      00000000
```

### 2.1 Timings — the R1 answer

Mainline `_dispc_mgr_set_lcd_timings()` (`dispc.c:3021`) is the authority on
the encoding:

```c
timing_h = (hsync_len-1)@0 | (hfront_porch-1)@8  | (hback_porch-1)@20;
timing_v = (vsync_len-1)@0 | (vfront_porch)@8    | (vback_porch)@20;
```

so (H/V field positions for OMAP4 are `sw_start=0`, `fp_start=8`, `bp_start=20`):

| register | field | raw | actual porch |
|---|---|---|---|
| TIMING_H | HSW [5:0] | 19 | **20** |
| TIMING_H | HFP [15:8] | 161 | **162** |
| TIMING_H | HBP [27:20] | 137 | **138** |
| TIMING_V | VSW [5:0] | 0 | **1** |
| TIMING_V | VFP [15:8] | 12 | **12** |
| TIMING_V | VBP [27:20] | 12 | **12** |

and these are exactly the mode-record fields the translator (`0x162ec`) had
already been shown to consume — the record stores the *actual* porches, and the
translator's three `-1`s are the H-field `n−1` encoding:

| record offset | value | becomes |
|---|---|---|
| `+0x3c` | 138 | HBP |
| `+0x34` | 162 | HFP |
| `+0x38` | 20 | HSW |
| `+0x30` | 12 | VBP |
| `+0x28` | 12 | VFP |
| `+0x2c` | 1 | VSW |

`SIZE_LCD = 0x025703FF` → 1024 × 600 (bits[10:0]=1023 width, bits[26:16]=599
height).  `GFX_SIZE = 0x003F01ED` (494×64) at `GFX_POSITION = 0x010C0109`
(x=265, y=268) is the centred RIM splash logo, not the scan size.

```
Htotal = 1024 + 138 + 162 + 20 = 1344
Vtotal =  600 +  12 +  12 +  1 =  625
pixel clock @60 Hz = 1344 × 625 × 60 = 50 400 000 Hz exactly
```

Cross-check: `DISPC_DIVISOR = 0x00010002` → pck_div = 2, lck_div = 1, so the
DSS functional clock is 100.8 MHz = 2 × pixel clock, a clean number.

### 2.2 Polarities

`DISPC_POL_FREQ = 0x00030000` against mainline's map (`dispc.c:3038`):

| bit | field | value | meaning |
|---|---|---|---|
| 17 | ONOFF | 1 | "always use the `rf` setting" (mainline hardcodes 1) |
| 16 | RF / SYNC_POSEDGE | 1 | syncs sampled on the **positive** edge |
| 15 | DE (DE_LOW) | 0 | data-enable **active high** |
| 14 | IPC (PIXDATA_NEGEDGE) | 0 | pixel data on the **positive** edge |
| 13 | HS (HSYNC_LOW) | 0 | HSYNC **active high** |
| 12 | VS (VSYNC_LOW) | 0 | VSYNC **active high** |
| 18 | ALIGN | 0 | not used |

111 §3.4 guessed "all syncs active-low"; the hardware says the opposite.  Note
the DSI video-mode packets carry the syncs, so for the LVDS bridge what matters
is that the DISPC timing generator and the DSI VM timings agree — both were
programmed together.

### 2.3 The channel question (corrects 111 §3.1)

`DISPC_CONTROL = 0x02000309`:

| bits | field | value |
|---|---|---|
| 0 | LCDENABLE | **1** |
| 1 | TVENABLE | **0** |
| 3 | STNTFT | 1 (Active/TFT — TRM Table 10-147: "1 = Active or TFT") |
| 9:8 | TFTDATALINES | 3 = 24-bit (RGB888) |
| 26:25 | TDMUNUSEDBITS | 2 = "unchanged" (TDM only, inert) |
| 6 | GOTV | 0 |
| 5 | GOLCD | 0 (self-clearing, completed) |

So the primary **LCD** manager drives the panel, `DISPC_SIZE_DIGIT` is 0 and the
TV/DIGITAL manager is never enabled.  111 §3.1 read the CRED accessor at
`0x52dc` as an RMW with mask `0x42` (bit1 `TVENABLE` + bit6 `GOTV`) and
concluded the DIGIT path was used — that must be an unused (TV/HDMI) code path
in the same HAL.  This matches mainline, where OMAP4 `OMAP_DSS_CHANNEL_LCD`
supports `OMAP_DSS_OUTPUT_DSI1` (`dss.c`, `omap4_dss_supported_outputs`).

---

## 3. DSI1 — programmed values and decode

```
48044000 DSI_REVISION            00000030
48044010 DSI_SYSCONFIG           00000010
48044014 DSI_SYSSTATUS           00000001   RESET_DONE
48044018 DSI_IRQSTATUS           00000080   PLL_LOCK_IRQ (bit 7)
48044040 DSI_CTRL                00EAEF9D
48044048 DSI_COMPLEXIO_CFG1      2A054321
4804404c DSI_COMPLEXIO_IRQ_STATUS 00000000  (no COMPLEXIO/HS error)
48044054 DSI_CLK_CTRL            A0346005
48044058 DSI_TIMING1             79190999
4804405c DSI_TIMING2             7FFF7FFF
48044060 DSI_VM_TIMING1          01079074
48044064 DSI_VM_TIMING2          04010C0C
48044068 DSI_VM_TIMING3          03F00258
4804406c DSI_CLK_TIMING          00000E0B
48044070 DSI_TX_FIFO_VC_SIZE     00002000
48044074 DSI_RX_FIFO_VC_SIZE     00002000
48044078 DSI_COMPLEXIO_CFG2     00030000   (NOT DSI_GNQ — that is at 0x44)
48044080 DSI_VM_TIMING4          00000000
48044084 DSI_VM_TIMING5          00003F00
48044088 DSI_VM_TIMING6          00000000
4804408c DSI_VM_TIMING7          000000C8
48044090 DSI_VM_TIMING?          000B0009
48044094 DSI_STOPCLK_TIMING      00000080
48044100 DSI_VC_CTRL(0)          20808791
48044118 DSI_VC_IRQSTATUS(0)     00000004   PACKET_SENT (bit 2)
```

### 3.1 VM timings — two more "unknown" record fields identified

| register | field | raw | value |
|---|---|---|---|
| VM_TIMING1 | HBP [11:0] | 0x074 | **116** |
| VM_TIMING1 | HFP [23:12] | 0x079 | **121** |
| VM_TIMING1 | HSA [31:24] | 0x01 | **1** |
| VM_TIMING2 | VBP [7:0] | 0x0C | **12** |
| VM_TIMING2 | VFP [15:8] | 0x0C | **12** |
| VM_TIMING2 | VSA [23:16] | 0x01 | **1** |
| VM_TIMING2 | WINDOW_SYNC [27:24] | 0x4 | 4 |
| VM_TIMING3 | VACT [14:0] | 0x258 | **600** |
| VM_TIMING3 | TL [31:16] | 0x3F0 | **1008** |

`HBP = 116` and `HFP = 121` are exactly mode-record fields `+0x5c` (116) and
`+0x54` (121), which 111 §3.2 listed as "unknown".  So:

* record `+0x54` → `DSI_VM_TIMING1.HFP`
* record `+0x5c` → `DSI_VM_TIMING1.HBP`
* record `+0x40` (1008) → `DSI_VM_TIMING3.TL`

They are **not** the DISPC porches: the DSI VM fields are in TXBYTECLKHS units,
the DISPC fields in pixels.  `TL = 1008` and the 625-line total give
`byteclk = 1008 × 625 × 60 = 37.8 MHz`, which is `clkdco / 16` — self-consistent
with the PLL below and with a 60 Hz frame rate.

### 3.2 DSI PLL — the last unknown, read out

```
48044300 DSI_PLL_CONTROL         00000000
48044304 DSI_PLL_STATUS          00000383
48044308 DSI_PLL_GO              00000000
4804430c DSI_PLL_CONFIGURATION1  14A5E8BE
48044310 DSI_PLL_CONFIGURATION2  00056000
```

**CONFIGURATION1 = 0x14A5E8BE** decoded with mainline `dss_pll`'s encoding
(`pll.c:405`):

| field | bits | raw | value |
|---|---|---|---|
| M5_CLOCK_DIV | 30:26 | 5 | mX[1] = **6** |
| M4_CLOCK_DIV | 25:21 | 5 | mX[0] = **6** |
| PLL_REGM | 20:9 | 0x2F4 = 756 | m = **756** |
| PLL_REGN | 8:1 | 0x5F = 95 | n = **96** |
| PLL_STOPMODE | 0 | 0 | off |

`REGM = 756` is **exactly** mode-record `+0x80` (the "12.6 × fps" field), and
`REGN = 95` is record `+0x7c`.  That resolves two more unknowns:

* record `+0x80` → `PLL_REGM` (m, not m−1)
* record `+0x7c` → `PLL_REGN` (n−1)
* record `+0x74`, `+0x78` (both 5) → `M4_CLOCK_DIV`, `M5_CLOCK_DIV` (mX−1)

**CONFIGURATION2 = 0x00056000**:

| bit | field | value |
|---|---|---|
| 18 | M5_CLOCK_EN | 1 |
| 17 | M5_CLOCK_PWDN | 0 |
| 16 | M4_CLOCK_EN | 1 |
| 15 | BYPASSEN | 0 |
| 14 | PLL_REFEN | 1 |
| 13 | PHY_CLKINEN | 1 |
| 12 | PLL_DRIFTGUARDEN | 0 |
| 6 | PLL_LOWCURRSTBY | 0 |
| 0 | PLL_IDLE | 0 |

**STATUS = 0x383** = bits 0,1,7,8,9; bit1 is `PLL_LOCK` (TRM §10.3.4.5.5) — the
PLL is locked.

**Derived:** `CLKDCOLDO = fint × 2 × m` with `fint = CLKINP / n`.  For a
38.4 MHz reference (`CLKINP` ≤ 52 MHz per TRM, and 38.4 gives the round number
below): `CLKDCOLDO = 38.4 × 2 × 756 / 96 = 604.8 MHz`, `CLKOUT4 = CLKOUT5 =
604.8 / 6 = 100.8 MHz`, `txbyteclkhs = 604.8/16 = 37.8 MHz`.

> **Correction to 111 §3.4:** the estimated "≈150 Mbps aggregate / 75 Mbps per
> lane" is wrong.  The measured PLL is `REGM 756 / REGN 95`, not the guessed
> `{60, 90, 300, 5, 5}`.  The DCO is ~604.8 MHz and the link carries ~604.8 Mbps
> aggregate at `txbyteclkhs = 37.8 MHz`.  The record constants
> `+0x68/+0x6c/+0x70 = {60, 90, 300}` are still unassigned.
>
> (`clkdco` in the TRM's sense is `CLKIN4DDR`; whether the per-lane bit rate is
> `1×` or `2×` that number depends on which side of the PHY you count.  What is
> certain and useful: **m=756, n=96, mX(M4)=mX(M5)=6, fint=0.4 MHz,
> CLKDCOLDO=604.8 MHz**, and those are the numbers the driver has to reproduce
> if it programs the PLL by hand.)

### 3.3 DSI PHY — previously unreadable, now captured

```
48044200 DSI_DSIPHY_CFG0   0B190D13
48044204 DSI_DSIPHY_CFG1   42040A22
48044208 DSI_DSIPHY_CFG2   B8800009
4804420c DSI_DSIPHY_CFG3   625D21A0
48044210 DSI_DSIPHY_CFG4   625D21A0
48044214 DSI_DSIPHY_CFG5   FF000000
48044218 DSI_DSIPHY_CFG6   00010000
48044220 DSI_DSIPHY_CFG8   00000410
48044224 DSI_DSIPHY_CFG9   00020001
4804422c DSI_DSIPHY_CFG11  7D20A59A
48044230 DSI_DSIPHY_CFG12  04320000
48044238 DSI_DSIPHY_CFG14  3E080000
4804423c DSI_DSIPHY_CFG15  01400003
```

These were the blocker in 112 ("probe child died on signal 7").  They are pure
`DSI_DDRTiming`/`DSI_HsLpTiming` outputs of the 13 refresh-dependent record
numbers at `+0x84…+0xb4`; e.g. `CFG0`'s four bytes are
`{19, 13, 25, 11}` = record `[3],[2],[1],[0]`.  A driver that lets mainline
compute them will produce different (but equivalent) numbers; if we ever need
to reproduce QNX exactly, this is the table.

### 3.4 Clock / lane structure

`DSI_CLK_CTRL = 0xA0346005`:

| bits | field | value |
|---|---|---|
| 31:30 | PLL_PWR_CMD | 2 = ON_ALL |
| 29:28 | PLL_PWR_STATUS | 2 = ON_ALL |
| 21 | LP_RX_SYNCHRO_ENABLE | 1 |
| 14 | CIO_CLK_ICG | 1 |
| 12:0 | LP_CLK_DIVISOR | 5 |

`DSI_COMPLEXIO_CFG1 = 0x2A054321` — lane positions/order (CLK + 2 data lanes →
`nibbles 1,2,3,4` pattern) plus the power/ULPS fields in the upper half.  No
COMPLEXIO error is latched (`0x4804404c = 0`), so the link is electrically
clean.

---

## 4. What this means for the port

### 4.1 display-timings node — paste-ready

```dts
display-timings {
        native-mode = <&timing0>;

        timing0: timing0 {
                clock-frequency = <50400000>;   /* 1344 x 625 x 60 */
                hactive = <1024>;
                vactive = <600>;
                hback-porch = <138>;
                hfront-porch = <162>;
                hsync-len = <20>;
                vback-porch = <12>;
                vfront-porch = <12>;
                vsync-len = <1>;
                hsync-active = <1>;             /* POL_FREQ IHS = 0 */
                vsync-active = <1>;             /* POL_FREQ IVS = 0 */
                de-active = <1>;                /* POL_FREQ IEO = 0 */
                pixelclk-active = <1>;          /* POL_FREQ IPC = 0 (posedge) */
        };
};
```

The `*_active = <1>` values are the "not inverted" form; if the picture is
offset or torn, flip them together with `DISPC_POL_FREQ` bit 13/12.

### 4.2 Driver checklist, in the order that worked

1. **Power domain first** — `power-domains = <&prm_dss>` on `dss_target`, or a
   bring-up poke of `PM_DSS_PWRSTCTRL = 3`.  Without it, every DSS/DSI access
   aborts and the failure looks like a firewall.
2. `CM_DSS_CLKSTCTRL = 2`, `CM_DSS_DSS_CLKCTRL = 0xF02` (MODULEMODE=2 + the
   four `OPTFCLKEN`s).  `0x00040F02` is the value QNX and the bootloader use.
3. DISPC: timings/size/pol/divisor as §2, `CONTROL |= LCDENABLE | STNTFT |
   TFTDATALINES=3`, then `GOLCD`.  Success = `DISPC_LINE_STATUS` advancing and
   `VSYNC1` + `GFXEND` in `DISPC_IRQSTATUS`.
4. DSI: reset → complex-IO lanes → PLL (`REGM/REGN/M4/M5`, `PLL_GO`, poll
   `STATUS[1]`) → `CLK_CTRL`/`CLK_TIMING` → PHY DDR timing → `VM_TIMING1..3` →
   `VC_CTRL` → null packet → video mode.
5. **Panel is Hitachi**: the 9-word SPI4 table (QNX calls it `/dev/spi3`; it is
   **OMAP MCSPI4**, registers at `0x480BA100`) —
   `0029 0025 0840 00AD 0430 385F 3CA4 3409 4041`, byte-swapped on the wire,
   100 ms after word 0, after word 2 and after the last word (111 §3.5).
   See §6.2 — do **not** trust a cold GPIO176 read.
6. **LM3532 over I²C 0x38** on `0x48070000` (= `&i2c1`) (`0x70←0xFF, 0x17←0x19,
   0x13←0x06, 0x1D←1`, `0x12←level|level<<3`) — nothing else lights the panel
   (111 §4.3).  Verified necessary in §6.1: DSS/DISPC/DSI alone left the panel
   dark.

### 4.3 What is still unknown

* The DSI PLL reference `CLKINP` is inferred to be 38.4 MHz from the round
  numbers; it was not read (there is no CM register to read it from — it is the
  DSS `sys_clk`).
* Record `+0x68/+0x6c/+0x70 = {60, 90, 300}` remain unassigned.
* Whether `GFX_ATTRIBUTES = 0x04000200` has `GFXENABLE` set is unclear (bit 0 is
  clear, yet `GFXEND` fires) — the bit position in OMAP4's
  `DISPC_GFX_ATTRIBUTES` was not looked up.  Irrelevant for us: omapdrm programs
  its own overlay.
* The TC358764 was never programmed by `winch_lcdctl`; the RIM bootloader must
  configure it.  **Correction (2026-10-27, report 121 §9): its control port is
  MIPI DSI *generic packets* (16-bit register address + 32-bit data), not I²C —
  so "no 16-bit-address I²C table" is the expected result and is not evidence of
  absence.**  Registers: `SYS_ID 0x0580`, `SYS_RST 0x0504` (I2CS 0, I2CM 1,
  LCD 2, BM 3, DSIRX 4), `PPI_STARTPPI 0x0104`, `DSI_STARTDSI 0x0204`,
  `VP_CTRL 0x0450`, `LV_CFG 0x049C`.  The `reg = <0>` in a DT bridge node is
  the DSI virtual channel, not an address.  The chip's own I²C controllers
  (`SYS_RST_I2CS/I2CM`) are a function of the bridge — it can master a
  downstream panel — not its register port.  Consequence for our port: the
  bridge has to be programmed over DSI, and on our boot path nobody does it.

---

## 5. Device state left behind

* `winch_lcdctl -s splash` was **run** on TabletOS; it was killed again with
  `slay -f winch_lcdctl` after the capture.  Nothing is written to media.
* `/accounts/devuser/memw32` deployed (§1).
* DSS was left powered ON (it was OFF before).  `powerman` re-idles it.
* Media untouched: sealed IFS sha256 `c72b0530…`, `BOOTCTR_FCTRY = 0x32`
  (read before and after; no RPMB access).

---

## 6. Post-wake validation (same session, after the panel was woken)

The panel did **not** light from the `winch_lcdctl` run alone — it stayed dark
until the operator touched the screen, at which point QNX's `screen` service
left idle, re-initialised the display and showed the login screen.  Two useful
things fall out.

### 6.1 The timing/PLL/PHY configuration is canonical, not `winch_lcdctl`-specific

Re-read after `screen` owned the display (capture:
`playbook-live-spare-2026-10-26-clean/qnx-display-after-wake-2026-10-27.txt`):

| register | after `winch_lcdctl` | after `screen` wake |
|---|---|---|
| `DISPC_TIMING_H` | `0890A113` | `0890A113` (identical) |
| `DISPC_TIMING_V` | `00C00C00` | `00C00C00` (identical) |
| `DISPC_POL_FREQ` | `00030000` | `00030000` (identical) |
| `DISPC_DIVISOR` | `00010002` | `00010002` (identical) |
| `DISPC_SIZE_LCD` | `025703FF` | `025703FF` (identical) |
| `DSI_PLL_CONFIGURATION1` | `14A5E8BE` | `14A5E8BE` (identical) |
| `DSI_PLL_CONFIGURATION2` | `00056000` | `00056000` (identical) |
| `DSI_VM_TIMING1/2/3` | `01079074`/`04010C0C`/`03F00258` | identical |
| `DSI_DSIPHY_CFG0/1` | `0B190D13`/`42040A22` | identical |
| `DISPC_GFX_BA0` | `AFEF0000` | `BD100000` (screen's framebuffer) |
| `DISPC_GFX_POSITION` | `010C0109` (265,268) | `00000000` |
| `DISPC_GFX_SIZE` | `003F01ED` (494×64) | `025703FF` (full screen) |

So the whole mode (timings, polarity, PLL, PHY, VM timings) is **the same
configuration whichever driver programs it** — two independent implementations
agree byte-for-byte.  That is the strongest validation this table can get, and
it means the values in §2/§3 are "the panel's mode", not an artefact of one
tool.  Only the framebuffer pointer and the GFX window differ.

### 6.2 Trap: the panel-ID strap reads 0 until the panel rail is up

`winch_lcdctl` said `LCD is LG` on the cold run; every other occurrence in
`sloginfo` (boot splash, and the `screen` wake) says `LCD is Hitachi`:

```
Dec 31 21:00:00   LCD is Hitachi     (boot)
Oct 27 04:47:06   LCD is Hitachi
Oct 27 06:25:28   LCD is Hitachi
Oct 27 06:45:08   LCD is LG          <-- the cold run in this session
Oct 27 06:50:33   LCD is Hitachi     (screen wake)
```

`GPIO6_DATAIN` (`0x4805D138`) followed the same pattern: `0x00000800`
(bit 16 = 0) during the cold run, `0x00010800` (bit 16 = 1) once `screen` had
the display running.  So GPIO176 is **not a static board strap** — it only
reflects the panel when the panel's rail/TCON is powered.

⇒ This unit is **Hitachi** (111 §0's "1 ⇒ Hitachi" mapping is right; the
value is just unreadable cold).  A Linux driver must either power the panel
rail before sampling GPIO176, or hardcode Hitachi for `winchester`.

The cold run also explains why the panel stayed dark: `winch_lcdctl` selected
the **LG** table, so it sent the wrong TCON unlock sequence — the DSS/DSI side
was scanning correctly (`DISPC_LINE_STATUS` advancing, `VSYNC`/`GFXEND` in
`DISPC_IRQSTATUS`, `PLL_LOCK`, `PACKET_SENT`), but nothing was lit.

### 6.3 Reconciliation with report 117 (the static RE of `0x1cce0`)

117 independently derived the register values from the binary.  Agreement and
disagreement with the live capture:

| item | 117 | hardware | verdict |
|---|---|---|---|
| `DISPC_TIMING_H` | `0890A113` | `0890A113` | **agree** |
| `{137,161,19}` → H | HBP/HFP/HSW | same | **agree** |
| `DISPC_TIMING_V` | `00C00C00` | `00C00C00` | **agree** (register word) |
| V porch decode | "VBP 13, VFP 13, VSW 1 ⇒ Vtotal 627 ⇒ 50.56 MHz" | **VBP 12, VFP 12, VSW 1 ⇒ Vtotal 625 ⇒ 50.40 MHz** | **117 wrong** — see below |
| `DISPC_POL_FREQ` | `00030000` | `00030000` | **agree** |
| `DISPC_DIVISOR` | `00010002` | `00010002` | **agree** |
| `DSI_PLL_CONFIGURATION1` | `14A5E8BE` (derived from `rec[0x74/0x78/0x7c/0x80]`) | `14A5E8BE` | **agree** — two independent derivations |
| `DSI_VM_TIMING1/2/3` | `01079074`/`04010C0C`/`03F00258` | same | **agree** |
| `DSI_DSIPHY_CFG0` | `130B190D` | `0B190D13` | 117's *formula* is right, its evaluated word is byte-scrambled; hardware wins |
| `DSI_DSIPHY_CFG1/2/5` | "never written" | non-zero | 117 traced only the video-mode path; something else (screen's driver, or the DDR/HS-LP HAL steps 111 §2 lists) writes them |

**The V-field correction is not cosmetic.**  TRM Table 10-21 ("Programmable
Fields in Bypass Mode") is explicit:

```
HBP  DISPC_TIMING_Ho[31:20]  "HBP bit field value + 1"
HFP  DISPC_TIMING_Ho[19:8]   "HFP bit field value + 1"
HSW  DISPC_TIMING_Ho[7:0]    "HSW bit field value + 1"
VBP  DISPC_TIMING_Vo[31:20]  "VBP bit field value"        <-- no +1
VFP  DISPC_TIMING_Vo[19:8]   "VFP bit field value"        <-- no +1
VSW  DISPC_TIMING_Vo[7:0]    "VSW bit field value + 1"
```

which is exactly what mainline does (`dispc.c:3028`: H fields written `-1`,
`vfront_porch`/`vback_porch` written as-is).  So VBP = 12, VFP = 12, VSW = 1.

Two useful by-products of 117 that this report should keep:

* **`0x1cce0` is not the register programmer** — it is the HAL parameter-block
  defaults initialiser (38 stack pointers into `0x2e5c4`, only `memset`/constant
  stores, no CRED access).  The real writers are `0x18a7c` (DISPC, CRED[52]),
  `0x179dc` (DSI, CRED[55/56/57]) and `0x16610` (`DSS_CONTROL`).
* **`DSI_PLL_CONFIGURATION1` is built entirely from record bytes**
  `rec[0x74]`→M4, `rec[0x78]`→M5, `rec[0x7c]`→REGN, `rec[0x80]`→REGM, so the
  record *is* the PLL table — no extra HAL constants are involved.

---

## 7. The same sequence from Linux: it does NOT work, and why (bench, session 5)

A userspace prover was added to the initramfs console (`analysis/initramfs/console.c`,
commands `disp init|stat|mux|col|off` and `bl init|dump|rd|wr`) that replays
§1/§2/§3 over `/dev/mem` and drives the LM3532 through `/dev/i2c-*`.  Image:
`chainloader/kernel.img` crc `0x2AC833B8`.  It **fails on this kernel**, and the
reason is a real porting blocker rather than a mistake in the values.

### 7.1 The inherited state is "enabled but in standby"

```
PM_DSS_PWRSTCTRL  00030003   POWERSTATE = ON
PM_DSS_PWRSTST    00000037   ON
CM_DSS_CLKSTCTRL  00000f02   CLKTRCTRL = 2 (SW_WAKEUP)
CM_DSS_DSS_CLKCTRL 00040f02  MODULEMODE = 2, IDLEST = 0, **STBYST (bit 18) = 1**
```

Every kernel boots with the DSS module in **module standby**.  In that state:

| access | result |
|---|---|
| `DSS_CONTROL` (0x48040040) read | SIGBUS |
| `DSS_CONTROL` (0x48040040) **write** | **never completes** — the interconnect hangs |
| `DISPC_CONTROL` (0x48041040) write | SIGBUS |
| `DISPC_REVISION` (0x48041000) read | SIGBUS *after* a 0→2 module cycle |
| CM/PM registers | always fine |

The hung write is the dangerous one: it left the console child in an
uninterruptible bus cycle, and **PID 1's `reboot()` hung too**, so the unit
needed a physical power cycle.  (The 180 s auto-reboot worked normally on the
next boot, when no bus cycle was wedged.)  Treat a write to any DSS target in
standby as a bench-losing move.

### 7.2 Writing MODULEMODE is not enough — the module is clock-starved

* `CM_DSS_DSS_CLKCTRL` already reads MODULEMODE=2, and re-writing 0xF02 changes
  nothing (STBYST stays 1).
* A real **0 → 2** transition (`*cm = 0`, poll IDLEST=3, `*cm = 0xF02`, poll
  IDLEST=0) also leaves STBYST=1 — and afterwards DISPC reads abort too, i.e.
  the cycle made it worse.
* QNX only got STBYST=0 as a side effect of enabling a module that started at
  MODULEMODE=0 **with its functional clock already running** (`setup-core-inactive`
  had cleared the `DPLL_PER`/`DPLL_CORE` output gates, report 106 Phase A).

DPLL_PER dividers, same board, measured on both sides:

| register | QNX (display running) | Linux (DSS standby) |
|---|---|---|
| `CM_DIV_M2_DPLL_PER` | `00000808` | `00000822` |
| `CM_DIV_M3_DPLL_PER` | `0000000d` | `00000023` |
| `CM_DIV_M4_DPLL_PER` | `0000002c` | `00000023` |
| `CM_DIV_M5_DPLL_PER` | `00000224` | `00000028` |
| `CM_DIV_M6_DPLL_PER` | `00000024` | `00000024` |
| `CM_DIV_M7_DPLL_PER` | `00000025` | `00000023` |

So the DSS functional clock is a *different clock tree* on the Linux boot path,
and with no consumer the clock framework has it off.  `clk_ignore_unused` keeps
the gate from being closed at `late_initcall`, but it does not make the DSS
functional clock run.

### 7.3 Consequence for the plan

Userspace `/dev/mem` poking **cannot** bring up DSS here; the blocker is the
clock, not the register values.  Two ways forward:

1. **Let the kernel claim the DSS clocks** — enable `&dss`/`&dispc`/`&dsi1` so
   `omapdrm`/`ti-sysc`/the clock framework take `dss_clk` (and, for OMAP4,
   `dss_sys_clk`) references.  This is the driver route, and it is the only one
   the clock framework will be happy with.  The `&dss_target {
   ti,no-reset-on-init; }` hack in the DTS exists because the target's reset
   poll stalls while the *power domain* is off — on the Linux path the domain is
   already ON, so that may no longer be needed.
2. **Replicate QNX's PRCM state** — program `DPLL_PER` (M values, gate control)
   exactly as `setup-core-inactive` leaves it (report 106) and then poke.  This
   is more faithful but keeps the kernel's clock framework out of the loop, and
   it will break as soon as something else reprograms DPLL_PER.

Either way, do not write to the DSS top target until `CM_DSS_DSS_CLKCTRL` bit 18
reads 0.

### 7.4 What was proven on the way

* `PM_DSS_PWRSTCTRL=3`, `CM_DSS_CLKSTCTRL=2`, `CM_DSS_DSS_CLKCTRL=0xF02` all
  take effect from Linux over `/dev/mem` (readback exact).
* The LM3532 path was never reached (the DISPC step aborts first); `/dev/i2c-*`
  is available (`CONFIG_I2C_CHARDEV=y`) and `&i2c1` (0x48070000) is the bus the
  backlight is on.
* The unit returns to the boot ROM on its own 180 s after `/init` starts, so a
  failed experiment costs one restore cycle, **except** when a bus write hangs
  (then it needs a power cycle).

---

## 8. Route (b): omapdrm drives the panel — the pipeline is ALIVE

Built and booted with the in-tree DRM stack.  Image `chainloader/kernel.img`
crc **0x33BF3664** (`0xA8FF00` bytes).

### 8.1 What was changed

`.config` (was `CONFIG_DRM=m`, i.e. no DRM at all in the initramfs kernel):

```
CONFIG_DRM=y  CONFIG_DRM_KMS_HELPER=y  CONFIG_DRM_OMAP=y
CONFIG_DRM_PANEL=y  CONFIG_DRM_PANEL_LVDS=y  CONFIG_DRM_TOSHIBA_TC358764=y
CONFIG_DRM_FBDEV_EMULATION=y
CONFIG_LEDS_CLASS=y  CONFIG_LEDS_LM3532=y  CONFIG_BACKLIGHT_LED=y
```

`omap4-winchester.dts`: `&dss { status = "okay"; }`, `&dsi1 { status = "okay"; }`
with `port { endpoint { lanes = <0 1 2 3 4 5>; } }` (3 pairs = clock + DATA1 +
DATA2, the same shape `motorola-mapphone-xt8xx.dtsi` uses for a 2-lane OMAP4
panel), a `bridge@0` child with `compatible = "toshiba,tc358764"`, and a
top-level `panel-lvds` with the §4.1 timing.  The bridge node also carries
`lanes = <2>`, `hs-rate = <604800000>`, `lp-rate = <10000000>`.

`drivers/gpu/drm/bridge/tc358764.c` — three board-bring-up changes, all guarded
so other users are unaffected:

1. `reset-gpios` is now **optional** (`devm_gpiod_get_optional`, and
   `tc358764_reset()` returns early when absent).  The PlayBook's bridge reset
   line is not known yet.
2. `dsi->lanes` / `hs_rate` / `lp_rate` can come from DT (`lanes`, `hs-rate`,
   `lp-rate`).  The driver used to hardcode 4 lanes (this panel has 2) and
   leave `hs_rate` at **0** — and the OMAP host derives its PLL search range
   from `hs_clk_max = client->hs_rate`, so with 0 the mode is rejected outright.
3. `#include <linux/of.h>`.

> The OMAP host takes the *link* lane count from the **DT `lanes` list on the
> host endpoint**, not from `mipi_dsi_device.lanes` (`dsi_configure_pins()`),
> and the DSI clock budget from `client->hs_rate` (`omap_dsi_host_attach()`).
> Both matter; that is the whole reason a plain `panel-simple`/`panel-lvds` on
> the DSI port cannot work (there would be no `mipi_dsi_device` at all, so the
> host never attaches).

### 8.2 Result — the driver wakes the DSS and scans

```
/dev/dri/                      exists
/dev/fb0                       exists      ("0 omapdrmdrmfb" in /proc/fb)
/dev/i2c-0 .. i2c-3            exist       (LM3532 bus reachable)
```

Live registers, read from the kernel console **with the DRM driver running**:

```
PM_DSS_PWRSTCTRL   00030003      ON
CM_DSS_CLKSTCTRL   00000f02      SW_WAKEUP
CM_DSS_DSS_CLKCTRL 00040f02      (bit18 standby clear in effect: reads work)
DSS_CONTROL        0000001b      <- READABLE now (was SIGBUS/hang in §7)
DISPC_CONTROL      00018309      LCDENABLE(0) | STNTFT(3) | 24-bit(9:8) | b15,b16
DISPC_CONFIG       0000020c
DISPC_LINE_STATUS  000001a8      raster advancing
DISPC_IRQSTATUS    00000022      VSYNC1 | PROGRAMMEDLINENUMBER
DISPC_TIMING_H     0890b400      HBP 137 (=138), HFP 180 (=181), HSW 0 (=1)
DISPC_TIMING_V     00c00c00      VBP 12, VFP 12, VSW 0 (=1)   <- identical to QNX
DISPC_DIVISOR      00010003      PCD 1, LCD 3   (QNX used LCD 2)
DISPC_SIZE_LCD     025703ff      1024 x 600
DSI_SYSSTATUS      00000001      RESET_DONE
DSI_CTRL           00eaee9f
DSI_CLK_CTRL       a0306008
DSI_VM_TIMING1     0010c0cf      HBP 207, HFP 16, HSA 0
DSI_VM_TIMING2     04010c0c      identical to QNX
DSI_VM_TIMING3     07e00258      TL 2016, VACT 600
DSI_PLL_STATUS     00000383      **PLL LOCKED**
DSI_PLL_CONFIG1    1ce1f81f      mainline's own M/N (not QNX's 14A5E8BE)
DSI_PLL_CONFIG2    00056008
DSI_PHY_CFG0       1837182c      mainline's own DDR/HS-LP values
```

One caveat on the two DSS **clock** registers above: the optional-clock bits move
between boots as the clock framework drops clocks nobody holds.  The first DRM
boot read `CM_DSS_CLKSTCTRL=0x00000f02` / `DSS_CLKCTRL=0x00040f02`; a later one
read `0x00000702` / `0x00000502`.  What matters is constant: `CLKTRCTRL=2`,
`MODULEMODE=2`, `IDLEST=0`, **`STBYST=0`** — i.e. "awake", which is the thing
§7's userspace route could never achieve.

So: **the DSS module is awake, DISPC is generating a 1024x600 raster, and the
DSI PLL is locked.**  That is 111's R5 table green under the real driver, and it
closes §7's blocker — the clock framework is happy once a driver owns the DSS.

**CORRECTION (2026-11-15, report 121 §11): the `TL` 1008 → 2016 divergence was
NOT benign and not "mainline redistributing blanking" — it was a lane-count
mistake.**  This board's reference runs **four** DSI data lanes, not two:

```
DSI_COMPLEXIO_CFG1   QNX 0x2A054321   (CLK=1 DATA1=2 DATA2=3 DATA3=4 DATA4=5)
                     our 2-lane build 0x2A000321   (DATA3/DATA4 unused)
```

1024x600 at 60 Hz RGB888 = 50.4 Mpx/s x 24 bpp = **1.2096 Gbps**; over 4 lanes
that is 302.4 Mbps/lane = a **37.8 MHz byte clock**, i.e. exactly QNX's
`TL = 1008` (1008 x 625 x 60 = 37.8 MHz).  Two lanes force 75.6 MHz, which is
what mainline computed — the whole "2x" was the missing pair of lanes.  With
`lanes = <0 1 2 3 4 5 6 7 8 9>` (5 pairs) and `lanes = <4>` on the bridge,
`DSI_COMPLEXIO_CFG1` and `DSI_VM_TIMING3` (`0x03F00258`) become byte-identical to
QNX.  `DSI_GNQ[11:9]` = NB_DATA_LANES reset `0x4` ⇒ 1+4 = 5 lanes supported, which
is why a 5-pair lane list is legal.

`DISPC_DIVISOR` (LCD 3 vs 2) and `TIMING_H` field distribution still differ, but
both total 320 blanking + 1024 ⇒ the same 50.4 MHz pixel clock.  `DSI_CTRL` also
differed in two bits, decoded from TRM Table 10-530: `CS_RX_EN` (bit 1, RX only)
and `VP_CLK_POL` (bit 8); matching the reference gives `0x00EAEF9D`.

### 8.3 What is still missing for a lit panel

1. **LM3532 backlight.**  `bl dump`/`bl init` in the console use `/dev/i2c-*`,
   but the first attempt **wedged the console inside an i2c ioctl** (no reply,
   no prompt) — the OMAP i2c driver did not return.  Next time: probe
   `/dev/i2c-1` (0x48070000) explicitly, and do it in a forked worker with an
   alarm like `disp` does.  The mainline `leds-lm3532` driver is built in but
   has no DT node yet.
2. **Panel TCON init over SPI (MCSPI4).**  Still no driver.  `&mcspi4` is
   `status = "disabled"` because its probe stalled in an earlier session; the
   Hitachi table is 9 words (111 §3.5).
3. Nothing else: the video path itself is proven.

---

## 9. The backlight, attempt 2: binding the driver wedges the bus (2026-10-27)

With §8 working, the LM3532 was added the proper way — a `ti,lm3532` node at
`0x38` on `&i2c1`, a `led@0` child (`led-sources = <0>`, `ti,led-mode = <0>`,
`led-max-microamp = <20000>`), an `led-backlight` wrapper with
`default-brightness-level = <255>`, and `backlight = <&backlight>` on the panel.
`&i2c2/3/4` were disabled at the same time (only I2C1 is known clocked).
Image crc `0x3B04298E`.

**Result: the kernel booted, the gadget and console came up (~20 s), and then the
unit reset into the boot ROM.**  No `/sys/class/backlight` and no
`/sys/class/leds` entries appeared, and the wedged state was the same one as
§7.4's userspace probe: **any I2C transfer to 0x38 wedges the bus** — it takes
the machine down (the WDT2 caught it this time, which is why the unit landed in
the bootrom instead of sitting dead).

So both access paths agree, which makes the *device/bus condition* the suspect,
not the driver:

* the LM3532 is at 7-bit `0x38` on `0x48070000` (`&i2c1` = `/dev/i2c-0` by the
  DT aliases in `omap4.dtsi`), and the TWL6030 at `0x48` on the *same* bus talks
  fine — so the controller, its clock and the bus itself are healthy;
* a slave that is **unpowered** holds SDA/SCL through its ESD diodes, which
  hangs the controller with no timeout to recover from.  That fits the evidence
  much better than a driver bug.

### 9.1 Where the rail is likely to come from

`playbook-dev/dumped4869ifs/etc/system/config/pmic-twl6030.cfg` (the file QNX's
`pmic-twl6030-ctl -c …` loads) says:

```
set_state regen1 on app
# On DVT3+ (ie: DVT4+ on the sticker), this goes to GPIOs 2 and 15.
```

and `pidin` on the live unit shows `pmic-twl6030-ctl -U 99:99 -p regen1,vaux3`
running with `-p regen1,vaux3`.  This unit reports `PVT4`, i.e. **DVT3+**, so
`regen1`/`regen2` are remapped to **GPIO 2 and GPIO 15** and only their states
are controllable.  `vaux3` is also explicitly named.

⇒ The next backlight step is **not** another probe.  It is: work out which of
`REGEN1` (TWL GPIO2), `REGEN2` (TWL GPIO15) and `VAUX3` powers the LM3532 and
the panel, get it up from the kernel (TWL regulator / GPIO), and only then bind
`ti,lm3532`.

### 9.2 The rail was the problem — VAUX3 fixes the bus hang

Reference states read from the live reference OS (`/proc/boot/pmic-twl6030-ctl`
is in the IFS and its grammar includes `get_state`):

```
$ pmic-twl6030-ctl get_state vaux3     ->  APP: ON   CON: OFF  MOD: OFF  LDO: ON
$ pmic-twl6030-ctl get_state regen1    ->  APP: ON   CON: OFF  MOD: OFF  LDO: ON
$ pmic-twl6030-ctl get_state regen2    ->  APP: OFF  CON: OFF  MOD: OFF  LDO: OFF
```

Linux exposed `VAUX3_6030` as `regulator.5` with **no `microvolts`** at all,
while VUSB (`regulator.12`) reports 3 300 000 and VANA 2 100 000 — i.e. VAUX3
was genuinely unset.  (`state` reads `unknown` for all of them; that is just
how `twl-regulator` reports, it is not a fault.)

Forcing it on in the board DTS:

```dts
&vaux3 {          /* must come AFTER #include "twl6030.dtsi" — dtc has no
                     forward references to labels defined later */
        regulator-always-on;
        regulator-boot-on;
};
```

and re-enabling `ti,lm3532` **changes the failure completely**:

| before VAUX3 | after VAUX3 |
|---|---|
| any transfer to 0x38 hangs the bus, unit resets/power-cycles | the client enumerates as `0-0038` and simply **defers** (`waiting_for_supplier`); no hang, machine stays up |

So §9's guess was right: the slave was unpowered and holding the bus.  Image
crc `0x6A040070` (LM3532 + `led-backlight` enabled, panel **not** linked).

`0-0038` never binds, though: no `/sys/class/leds`, no
`/sys/class/backlight`, and the device carries the `waiting_for_supplier`
attribute.  The driver itself has nothing that should defer
(`leds-lm3532.c`: optional `enable-gpios`, `devm_regulator_get(dev, "vin")`
which falls back to a dummy, regmap-i2c).  The most likely culprit is
`fw_devlink` + the `leds = <&bl_led>` phandle: `led@0` has `reg` but no
`compatible`, so it is never a device, and the consumer chain never resolves.
**Next thing to try: drop the `led-backlight` wrapper** and see whether the
bare `ti,lm3532` + `led@0` binds (then drive brightness through the LED class),
or add `fw_devlink=permissive` to the command line for the experiment.

### 9.3 State of the tree

* `&vaux3 { regulator-always-on; regulator-boot-on; }` — **on**, and it is what
  removed the bus hang.
* `ti,lm3532` and `led-backlight` are **`status = "okay"`** for probing; the
  panel's `backlight = <&backlight>` is still commented out, so a failing
  backlight cannot make `panel-lvds` defer and take the working pipeline down.
* `chainloader/kernel.img` = crc **`0x6A040070`** (the probe image, safe in
  practice: it boots and the display works).
* `chainloader/kernel-safe.img` = crc `0x7777931C` (VAUX3 on, backlight nodes
  disabled) — the image to fall back to.
* `kernel-drm-ok.img` = crc `0x33BF3664` (first working DRM image).
* Never flash **`0x3B04298E`** (the LM3532 build without VAUX3 — it wedges).

### 9.4 VAUX3 fixes the bus, but 0x38 still does not answer

Two more facts, both measured (image crc `0x2048684E`):

1. **`fw_devlink=permissive` clears `waiting_for_supplier`** — with it on the
   command line, `0-0038` no longer carries that attribute.  It does not make
   the driver bind, and the flag was a red herring: our node has no phandle
   suppliers at all.
2. **The device is not there.**  Now that the bus is healthy, a userspace probe
   returns immediately instead of hanging:

   ```
   bl dump -> bl: no LM3532 at 0x38 on i2c-0
   ```

   and `lm3532-led` *is* registered as a driver with a matching modalias
   (`of:Nled-controllerT(null)Cti,lm3532`), so the kernel probe ran and got a
   NACK.  So VAUX3 only removed the *electrical* fault; 0x38 still does not
   acknowledge.  Either it is on another rail, or it is not at 0x38.

**REGEN1 is the obvious next candidate** — QNX powers both (`-p regen1,vaux3`),
and on DVT3+/PVT4 `regen1` is driven through TWL GPIO2.  It cannot be done with
the in-tree driver: `gpio-twl4030.c` matches only `ti,twl4030-gpio` and handles
TWL4030/TPS659x0 — **no TWL6030 GPIO support**.  The options are

* a small patch that writes the TWL6030 REGEN/GPIO register through the MFD
  (`twl_i2c_write_u8(TWL6030_MODULE_*, …)`), which needs the register numbers —
  a static-RE job on `playbook-dev/dumped4869ifs/proc/boot/pmic-twl6030-ctl`,
  tracing what `set_state regen1 on` writes (zero risk, no bench);
* or a **bus scan** of i2c-0 to see whether anything answers at all and at which
  addresses — the console has no scan command yet, so that needs one small
  addition (`bl scan`) plus a build.

Do that before spending more bench cycles on guesses: one of those two answers
tells us whether the target is "power this rail" or "look at another address".

### 9.5 Bus scan: 0x38 is simply absent (image crc `0x6017A08E`)

The console gained `bl scan [bus]`, `i2c r|w <bus> <addr> <reg> [val]` and
`write <path> <text>`, and the scan is forked with a bounded wait (trap 16).
On the live kernel, i2c-0:

```
scan: ACK at 0x09
scan: ACK at 0x48      <- TWL6030
scan: ACK at 0x49      <- TWL6030 (leds/pwm block)
scan: ACK at 0x4a      <- TWL6030
scan: ACK at 0x55
scan: ACK at 0x77
scan: i2c-0 done, 6 device(s)
```

**No `0x38`.**  So the LM3532 is not at a different address — it is not
answering at all, i.e. unpowered.  (`0x09`, `0x55`, `0x77` are unidentified;
they may be other board devices or false ACKs from a device that answers any
address.  Worth a note only.)

That makes **REGEN1 the one remaining candidate**, and with the generic
`i2c w` command we can now poke the TWL from the console the moment the
register is known — no rebuild needed.

### 9.6 Rails, GPIOs and buses: what the backlight is NOT (2026-10-27, later)

Reference values, read live on TabletOS (`pmic-twl6030-ctl get_state/get_vout`):

| rail | state | Vout |
|---|---|---|
| vaux1 | APP ON, LDO ON | 2800 mV |
| vaux2 | APP ON, LDO ON | 2800 mV |
| vaux3 | APP ON, LDO ON | 2800 mV |
| vmmc | OFF | 3000 mV |
| regen1 | APP ON, LDO ON | — |
| regen2 | OFF | — |

Linux had **VAUX1/2/3 enabled but with no voltage at all** (VSEL never written).
Fixed with a `min == max` constraint, which makes the regulator core apply it
(`apply_uV`) — see §9.6.1 — and all three now read back **2 800 000 µV**.  That
did **not** make `0x38` appear.

**Bus:** `backlight_win` on the live reference has **`/dev/i2c0`** open
(`pidin -p 49166 fds`), and QNX's three `i2c-omap35xx-omap4` instances are
`0x48070000` (IRQ 88), `0x48072000` (IRQ 89) and `0x48350000` (IRQ 94), so
`/dev/i2c0 = 0x48070000` — the TWL's bus, the one we scan.  Re-enabled
`&i2c2` and `&i2c3` to scan the others too: buses 1 and 2 *hang* on the first
access (their controllers are not clocked on this board), so there is nothing
to find there either.

**GPIOs** (`winch_lcdctl` sets them, report 117 row 35 / report 119):

| signal | register | reference `OE` | `DATAOUT` | `DATAIN` |
|---|---|---|---|---|
| GPIO1_2 (`regen1` on MBID ≥5) | `0x4A310134/13C/138` | bit2 = 0 (output) | bit2 = **1** | bit2 = **0** |
| GPIO2_13 (`winch_lcdctl` display setup) | `0x48055134/13C/138` | bit13 = 0 (output) | bit13 = **1** | bit13 = **0** |

Both are "output, driven high" in the software state, yet **both pads read low**
— i.e. they are **not actually muxed to GPIO** on this board (or something
external holds them low).  That matches report 119's warning that the cfg's
"GPIO 2"/"GPIO 15" mapping is not proven against the pad configuration.

Bench result worth keeping: **driving GPIO1_2 high from Linux wedged the i2c
bus** (a scan hung and had to be abandoned; the bus recovered on the next
boot).  So whatever that ball really is, it sits on the i2c0 segment's
power/enable path — it is not a harmless spare.  Do not poke it blindly again.

**Where this leaves the backlight:** with all three AUX rails at the reference
voltage, the TWL responding on i2c0, and the LM3532 still not acknowledging at
0x38, the remaining explanations are (a) the LM3532's enable/HWEN is a ball we
have not identified and are not driving, (b) it is on a pad-muxed gate on the
i2c segment, or (c) the chip is not fitted/populated and QNX's light comes from
somewhere else — which is testable: kill `backlight_win` on the reference and
see whether the panel goes dark.

#### 9.6.1 The `min == max` trick (useful beyond this board)

`drivers/regulator/of_regulator.c` sets `constraints->apply_uV = true` whenever
both `regulator-min-microvolt` and `regulator-max-microvolt` are present, and
`machine_constraints_voltage()` then programs that voltage at registration
(including the `-ENOTRECOVERABLE` case, which is what an unreadable VSEL gives).
That is how a *board* can force a rail up and programmed without writing a
consumer driver:

```dts
&vaux3 {
        regulator-min-microvolt = <2800000>;
        regulator-max-microvolt = <2800000>;
        regulator-always-on;
        regulator-boot-on;
};
```









---

## 10. Backlight, the decisive observations (2026-10-27, end of session)

The single most useful datum came from the bench operator, not a register:
**stopping `backlight_win` left the panel lit (at its old, dim level), and
restarting it took the panel to full brightness.**  So

* the LM3532 **is** alive and writable on QNX's `/dev/i2c0` at run time, and
* the chip **latches** its brightness when the resmgr dies — which is why
  "kill the resmgr and see if the screen goes dark" is *not* a valid test.

And yet from Linux, with every AUX rail at the reference 2.8 V:

```
i2c r 0 38 70   -> Remote I/O error   (NAK: nothing at 0x38 on 0x48070000)
i2c r 1 38 70   -> Connection timed out  (controller unusable)
i2c r 2 38 70   -> Connection timed out  (controller unusable)
bl scan 0       -> 0x09 0x48 0x49 0x4a 0x55 0x77   (no 0x38)
```

So on this board something *enables* the LM3532 that we are not driving.

### 10.1 Reference GPIO output map (the candidate list)

All six banks on the live reference, `OE` / `DATAIN` / `DATAOUT`
(`base+0x134/0x138/0x13C`), read with `memdump3`:

| bank | base | OE | DATAIN | DATAOUT |
|---|---|---|---|---|
| GPIO1 | `0x4A310000` | `6ffb1faa` | `20700600` | `00006044` |
| GPIO2 | `0x48055000` | `ce3ecd5f` | `08000018` | `31013200` |
| GPIO3 | `0x48057000` | `fff7ffff` | `00000000` | `00000000` |
| GPIO4 | `0x48059000` | `6ffffe1c` | `10000010` | `90000120` |
| GPIO5 | `0x4805B000` | `ffffffff` | `00000400` | `00000000` |
| GPIO6 | `0x4805D000` | `ffffffff` | `00010800` | `00000000` |

Driven **high** (`DATAOUT` bit 1 with `OE` bit 0) in the reference:

* GPIO1: **2, 6, 13, 14**
* GPIO2: **13** (and others in `0x31013200`)
* GPIO4: **5, 8, 28, 31**

GPIO1_2 (`regen1`) and GPIO2_13 are both in that list and were tested from
Linux — neither produced a responsive 0x38, and GPIO2_13 changed nothing
visibly.  **The untested candidates are GPIO1_6/13/14 and GPIO4_5/8/28/31.**

### 10.2 How to test them safely

* Do the TWL regen1 detach first (`i2c w 0 48 af e0`, `i2c w 0 48 ad 00`).
  Without it, driving GPIO1_2 fights the PMIC's REGEN FSM and **wedges the i2c
  bus** (measured).  With it, the same write is clean.
* Drive one pin at a time through sysfs (`export` + `direction out` + `value 1`),
  then a single `i2c r 0 38 70` — *not* a full scan, so a stuck address cannot
  eat the alarm budget.
* GPIO1 = `gpiochip512` (lines 0-31), GPIO2 = `gpiochip544`, GPIO4 = `gpiochip608`,
  so the globals are `512+n`, `544+n`, `608+n`.
* Reading GPIO3-6 from our kernel needs their controllers clocked (no driver
  claims them, so the registers `SIGBUS`); GPIO1/2 are fine because exported
  lines keep them alive.

---

## 11. "Disabled in effect" audit — the real candidate (2026-10-27)

Prompted by a good question from the operator: *is something on the backlight path
disabled in the tree?*  The answer is **yes, and it is specific**.  Our DT carries
TWL6030 sub-blocks whose drivers are built as **modules**, and this kernel has
**no module loading at all** (initramfs only), so they silently never bind:

| DT node (from `twl6030.dtsi`) | config | state |
|---|---|---|
| `ti,twl6030-pwm` | `CONFIG_PWM_TWL=m` | **present in DT, driver never loads** |
| `ti,twl6030-pwmled` | `CONFIG_PWM_TWL_LED=m` | **present in DT, driver never loads** |
| `ti,twl6030-gpadc` | `CONFIG_TWL6030_GPADC=m` | present in DT, never loads |
| (TWL GPIO) | `CONFIG_GPIO_TWL4030=y` but the driver is **TWL4030/TPS659x0 only**, and the board DT has no `gpio` child | **TWL GPIOs unreachable from Linux** |

That last row matters because the reference drives `regen1`/`regen2` through the
PMIC on DVT3+ (report 119).  And the second row matters for a concrete reason:

> `winch_lcdctl`'s very first backlight action is a write to **TWL slave 0x49,
  register 0x92 ← 0x30**, which its own log text calls *"PMIC PWM
  initialization"* (report 111 §4.2).  The TWL6030's LED/PWM block is the
  **boost** that feeds the backlight.  With `PWM_TWL_LED` not bound, that block
  is left in its reset state, so the LM3532 may simply have **no supply** — which
  is exactly what "not present on the I2C bus at all" looks like.

**Top next experiment** (cheap, no new hardware work): build
`CONFIG_PWM_TWL=y`, `CONFIG_PWM_TWL_LED=y` and `CONFIG_TWL6030_GPADC=y`
(and consider `TWL4030_MADC`, `TWL4030_WATCHDOG`, `I2C_MUX` the same way, they are
all `=m` with DT nodes), rebuild, boot, and see whether `i2c r 0 38 70` starts
answering.  If it does, the backlight boost was the missing rail all along.

Signals that support this: the reference keeps VAUX1/2/3 at 2.8 V *and* runs the
TWL PWM block; we now have the rails but not the PWM block.  Signals against:
a boost normally feeds the LED string, not the driver's own VDD — so treat it as
the leading hypothesis, not a certainty.
