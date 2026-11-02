# 121 — Panel TCON init over SPI (MCSPI4): built, byte-exact, still black

**Session 6, 2026-10-27.**  This report records the SPI/TCON work done *after*
report 120 solved the backlight.  It is a state report, not a conclusion: the
panel is backlit and the framebuffer is being scanned, but the panel still shows
**nothing at all**, including with a solid-colour framebuffer.  §6 is the open
question that was handed to the previous session's agent through the operator.

---

## 1. What was added to the tree

### 1.1 MCSPI4 (`0x480BA100`, QNX's `/dev/spi3`)

```dts
&omap4_pmx_core {
	mcspi4_pins: mcspi4-pins {
		pinctrl-single,pins = <
			OMAP4_IOPAD(0x154, 0x1700)	/* mcspi4_clk  (gpio_151) */
			OMAP4_IOPAD(0x156, 0x1700)	/* mcspi4_simo (gpio_152) */
			OMAP4_IOPAD(0x15a, 0x1708)	/* mcspi4_cs0  (gpio_154) */
		>;
	};
};

&mcspi4 {
	status = "okay";
	pinctrl-0 = <&mcspi4_pins>;
	tcon@0 { compatible = "rohm,dh2228fv"; reg = <0>; spi-max-frequency = <450000>; };
};

/ { aliases { spi3 = &mcspi4; }; };	/* /dev/spidev3.0, like QNX's /dev/spi3 */
```

`CONFIG_SPI_SPIDEV=y` (was unset; `SPI_OMAP24XX=y` already).

The four MCSPI4 signals are **Type 2 (SMART3IO) pads** on
`CONTROL_CORE_PAD0_M`, and the reference muxes only three — `somi` is left in
safe mode because the TCON is write-only:

| pad | function | reference value |
|---|---|---|
| `0x4A100154[15:0]` | `mcspi4_clk` (gpio_151) | `0x1700` |
| `0x4A100154[31:16]` | `mcspi4_simo` (gpio_152) | `0x1700` |
| `0x4A100158[15:0]` | `mcspi4_somi` (gpio_153) | `0x000f` (safe mode) |
| `0x4A100158[31:16]` | `mcspi4_cs0` (gpio_154) | `0x1708` |

Verified live on Linux after the pinctrl state applied: `0x4A100154 =
0x17001700`, `0x4A100158 = 0x1708011f` (low half safe mode, as the reference),
and — crucially — **`CONTROL_SMART3IO_PADCONF_0 = 0xBD7F5501`,
`_2 = 0x67000510`, byte-identical to the reference**, so the Type-2 mux selects
MCSPI4 and the bytes really do reach the panel.

### 1.2 Console additions (`analysis/initramfs/console.c`)

* `spi tcon` — send the 9-word Hitachi table (111 §3.5) at 450 kHz, 8-bit,
  each u16 as two bytes low-first, one `SPI_IOC_MESSAGE` per word, with 100 ms
  after word 0, word 2 and the last word.
* `spi w <hexword> …` — raw words, for byte-order/mode experiments.
* `fb <rrggbb>` — mmap `/dev/fb0` and fill it.  **This is the only way to tell
  "no image" from "a black image"**: omapdrm's primary plane covers the whole
  screen, so `DISPC_DEFAULT_COLOR` is invisible (`disp col 0000ff` changed
  nothing on the panel, correctly).

Both run in a forked worker with a bounded wait, like `i2c`/`disp`.

Result on the bench: all nine words report `ok`, in **both** byte orders
(`29 00 25 00 40 08 …` and `00 29 00 25 08 40 …`).  The panel does not react to
either.

---

## 2. GPIO2: the reference drives six lines, and we now drive all six

`GPIO2_DATAOUT = 0x31013200` on the reference — bits **9, 12, 13, 16, 24, 28,
29** (`0x31013200` = `0x30000000 | 0x01000000 | 0x00013200`).  Report 118 §10.1
listed 9/12/13/16/28/29 and **missed bit 24** (`0x01000000`, `gpio_56`).

They are now all in the board DTS as **hogs** (bit 16 is the LM3532 gate and was
already one, report 120; the rest match the reference), together with the pad
muxes for `0x4A100060`, `0x068`, `0x070` and `0x088` taken from the reference's
own 16-bit readbacks.  Live readback after the change:
`GPIO2_OE = 0xcefecdff`, `GPIO2_DATAOUT = 0x31013200` — the reference's output
map, exactly.

**Do not drive these lines by poking `GPIO2_OE`/`DATAOUT` from the console.**
The `omap_gpio` driver restores the bank from its own cached context on runtime
PM transitions, so a direct poke is silently reverted (observed twice).  Use
sysfs, or better, a hog.

*Trap:* `GPIO2` output bits are easy to misread by hand.  `0x30013200` and
`0x31013200` differ by bit 24, not by bit 16 — recompute with a tool, not in
your head, before concluding a line is "missing".

---

## 3. What the display is doing

All of the following measured with a solid blue framebuffer (`fb 0000ff`,
`fb: 1024x600 32bpp, 2457600 bytes`):

```
DISPC CONTROL 00018309   LCDENABLE=1, STNTFT=1, 24-bit
DISPC LINE    000001c7   raster advancing
DISPC TIM_H   0890b400   (QNX 0890a113)
DISPC TIM_V   00c00c00   (QNX 00c00c00)  ← same
DISPC DIVISOR 00010003   (QNX 00010002)
DSI CTRL      00eaee9f   (QNX 00eaef9d)
DSI VC_CTRL   20818fb3   (QNX 20808791)
DSI CLK_CTRL  a0306008   (QNX a0346005)
DSI VM1       0010c0cf   (QNX 01079074)
DSI VM2       04010c0c   (QNX 04010c0c)  ← same
DSI VM3       07e00258   TL 2016 (QNX 03f00258, TL 1008)
DSI PLL STAT  00000383   locked
DSI PLL CFG1  1ce1f81f   (QNX 14a5e8be: m=756, n=96, mX=6/6)
DSI PLL CFG2  00056008   (QNX 00056000)
```

`58004000.encoder.0` is bound to `tc358764`, and the TC358764 is the only
`mipi-dsi` device — so the bridge driver *is* running (118 §8.1's concern about
DTS placement was unfounded: `tc358764.c` is a `mipi_dsi_driver` here).

Mainline's `dsi_vm_calc_blanking()` redistributes the horizontal blanking to fit
the byte clock, which is why `TIM_H`, `VM1`, `VM3.TL` and the PLL differ from
QNX; 118 §8.2 called that benign, **but it has never been validated against a
visible image.**  The DSI link runs at roughly **twice** the reference's byte
clock (TL 2016 vs 1008).

---

## 4. New tools and traps

* `spi tcon`, `spi w`, `fb` in the console (§1.2).
* Always paint the framebuffer before judging the panel — a black framebuffer
  and a dead panel look identical.
* `omap_gpio` context restore reverts direct register pokes (§2).
* Read GPIO bit maps with a calculator (§2).

---

## 5. State of the tree / images

| item | state |
|---|---|
| DTS | MCSPI4 okay + spidev + `spi3` alias; six GPIO2 hogs + pad muxes; panel `backlight` link; `panel_vdd`; `lm3532` with `vin-supply` |
| `.config` | + `SPI_SPIDEV=y` (`PWM_TWL*` =y from report 120) |
| console | + `spi`, `fb` |
| image | `chainloader/kernel.img` crc **`0xFDE2DEBB`** (six hogs, SPI, `fb`/`spi` commands) |
| unit | closed clean after the previous session segment; this one ended on the test image with the sealed IFS **not** on media — **restore before closing** |

---

## 6. The open question (relayed to the previous session's agent)

> Is the ~2× DSI link-rate difference (mainline `m=252/n=16/mX=8/8`, TL 2016
> vs QNX `m=756/n=96/mX=6/6`, TL 1008) a plausible cause of a **completely
> black** panel — i.e. does the panel's LVDS clock come from the bridge's
> DSI-derived PLL, so a 2× byte clock gives an out-of-spec LVDS clock?  And if
> so, should we force the QNX PLL/VM words, or give the bridge a different
> `hs-rate`?

If the answer is "yes, likely": force `DSI_PLL_CONFIGURATION1/2` and
`DSI_VM_TIMING1/3` to the measured QNX values (a board quirk or a patched
`dsi_vm_calc_blanking`) and re-test.

If "no, harmless": stop looking at the link rate and instrument the TC358764
itself — it has status/lock registers readable over DSI generic reads, which
would tell us whether it produces LVDS at all.  That needs a small driver
addition, since mainline's `tc358764.c` only writes.

---

## 7. The relay answer, and what the kernel log then showed

**The relay settled §6: the 2× is a register-encoding difference, not a wire
difference.**  `fint = 38.4/n`, `clkdco = fint·2·m`, `txbyteclkhs = clkdco/16`:
QNX `96/756` gives 0.4 MHz → 604.8 MHz → 37.8 MHz with `TL=1008` (26.67 µs/line);
mainline `16/252` gives 2.4 MHz → 1209.6 MHz → 75.6 MHz with `TL=2016`
(26.67 µs/line).  Same line time, same frame rate, same byte rate per pixel.
Likewise `TIMING_H` 137/180/0 vs 138/162/20: both total 320 blanking + 1024 =
1344, so the pixel clock is the same 50.4 MHz — mainline only moved pixels
between the fields.  **Do not force the PLL words** (and never mix: QNX PLL with
mainline `TL`, or the reverse, *is* a real 2× error).

The relay's other points, in the order it ranked them:

1. *Is the backlight really on?* — **closed**: report 120 lit the panel, and the
   operator confirmed it visually.
2. **`tc358764_write()` silently drops everything after the first error** — this
   turned out to be the real finding.
3. Video-mode subtype mismatch (`MIPI_DSI_MODE_VIDEO_BURST` + `VP_CTRL_EVTMODE`
   vs QNX's `DSI_CTRL = 0x00EAEF9D`); open.
4. The bridge is never reset (`reset-gpios` unknown, `tc358764_reset()` returns
   early), and its supplies are dummies — open.
5. Confirm the link carries packets — done, see below.

### 7.1 What `kmsg` shows (this is the finding)

The console gained a `kmsg [substr]` command (reads `/dev/kmsg` non-blocking,
optional substring filter — the link is slow and the ring is long; scan at least
20 000 records or you will stop before the DRM probe).

```
tc358764 58004000.encoder.0: supply vddlvds not found, using dummy regulator
tc358764 58004000.encoder.0: ID: 0x0
DSI: omapdss DSI error: dsi_vc_generic_read(vc 1, reqlen 2) failed
DSI: omapdss DSI error: bta sync failed
tc358764 58004000.encoder.0: write 0x013c <- 0x00020003 failed: -5
tc358764 58004000.encoder.0: error initializing bridge (-5)
```

`0x013C` is `PPI_TX_RX_TA`, the **first** write of `tc358764_init()`.  `-5` is
`-EIO`, produced by `dsi_vc_send_bta_sync()` failing inside
`dsi_vc_write_common()`.  So:

* the DSI **read** of `SYS_ID` fails too (`ID: 0x0` — `tc358764_read()` never
  sets `ctx->error`, so init continued to the writes);
* the first write latches `ctx->error` and **every subsequent bridge register
  write becomes a no-op** — the TC358764 is never programmed at all, which is
  exactly "DISPC scans, DSI PLL locked, VC_IRQSTATUS shows PACKET_SENT, and the
  panel is black";
* the panel-side link is clean (`DSI_COMPLEXIO_IRQ_STATUS = 0`), and the DISPC
  really is fetching a blue frame (`DISPC_GFX_BA0 = 0x7FDA8000` reads
  `0x000000FF` after `fb 0000ff`).

### 7.2 Change tried, and its result

`tc358764_write()` now logs the first failure once and **keeps programming**
(no `ctx->error` latch), so the full ~20-register sequence is attempted even
without BTA acks.  Verified in the log: one `(continuing)` warning, and no
`error initializing bridge` line.  **The panel is still black.**

Also added in the same round: GPIO1 hogs (lines 2, 6, 14; pads `0x190` and
`0x1b4`) so the reference's `GPIO1_DATAOUT = 0x6044` is matched — live readback
`0x00004044` (bit 13 omitted: its pad `0x1b0` is safe-mode on the reference
too).  **No effect.**

### 7.3 Next hypothesis, in order

1. **The bridge is programmed while the DSI is in video mode.**  Both the
   generic *read* and the *write* fail at BTA, which smells host-side: OMAP4's
   `omap_dsi_host_transfer()` sends on `VC_CMD` (vc 1) and uses
   `dsi_vc_enable_hs()` + `dsi_vc_send_bta_sync()`.  `tc358764_init()` runs from
   `bridge->pre_enable`, i.e. *after* the DSI encoder has started video mode
   (`pre_enable_prev_first = true`).  Try calling `tc358764_init()` at
   **attach/probe** time instead (`omap_dsi_host_transfer()` then does
   `dsi_enable()` itself because `iface_enabled` is false) and see whether the
   BTA starts working.  Cheap test: `kmsg tc358764` — an `ID: 0x…` that is not
   `0x0` means the DSI read path works.
2. If BTA still fails with video mode off, the bridge is genuinely unreset /
   unpowered: identify RESETN and the `vddc`/`vddio`/`vddlvds` rails (all
   currently dummy).  `pmic-twl6030-ctl get_state` on the reference is safe;
   toggling rails is not (§8).
3. Then the video-mode subtype (`MIPI_DSI_MODE_VIDEO_BURST` + `VP_CTRL_EVTMODE`
   vs QNX's `DSI_CTRL` bit pattern) — decode `DSI_CTRL` bit 1 and bit 8 from the
   TRM before changing anything.

### 7.4 §7.3 item 1 was tried — it is worse, and the answer is not the ordering

`tc358764_init()` moved into `tc358764_attach()` (and left out of `pre_enable`
when it succeeds).  Measured:

```
tc358764 …: ID: 0x0
tc358764 …: bridge did not answer SYS_ID; programming anyway
tc358764 …: write 0x013c <- 0x00020003 failed: -22 (continuing)
tc358764 …: attach-time init failed (-19); will retry at pre_enable
```

`-22` is `-EINVAL`: the DSI host rejects transfers *before the link has been
configured*, so attach is too early.  At `pre_enable` the same write is `-EIO`
from BTA.  **Neither link state accepts the transfer**, so the ordering is not
the fix and the code was reverted (the note is kept in `tc358764_attach()` so
nobody repeats it).

Also ruled out in the same round:

* **the bridge is not on i2c0**: a new `i2c16` console command (16-bit register
  addressing over `I2C_RDWR`) shows the unidentified `0x09` device answers
  *8-bit*-register reads (`i2c r 0 09 00 = 0x00`) but NAKs 16-bit addressing
  ("read failed"), and `0x0F` (the TC358764's usual address) does not answer at
  all.  i2c-1 hangs on this board.
* **it is not one of the reference-driven GPIOs**: `GPIO2_24` (`gpio_56`,
  `0x01000000`) was missing from the hog set and has been added — its pad
  `0x4A100080` was already mux 3.  `GPIO2_DATAOUT` is now `0x31013200`,
  byte-identical to QNX, and `GPIO1_DATAOUT = 0x00004044` (bit 13 omitted: pad
  `0x1b0` is safe-mode on the reference too).  **Still black.**

**Open question handed over (§9).**  These three rounds of failures are all the
same statement: on this board the bridge is *not* reached by the mechanisms
mainline's driver uses.  Report 111 says the RIM bootloader programs it, and our
chainloader path never does.  Until we know how it is meant to be programmed
(bus/address/addressing, or DSI-generic; RESETN; whether it survives a reset),
more register guessing is wasted boots.

### 7.5 Full register-level reset + read-back verification — the bridge is silent, not mis-programmed

Per the relay's advice, `tc358764_init()` now (a) writes a **full** `SYS_RST`
(`0x0504 <- 0x1f` = DSIRX|BM|LCD|I2CM|I2CS) *before* any other register — there is
no reset pin, so this is the only reset we can give it — and (b) **verifies by
read-back** instead of assuming, because a `-EIO` only means the BTA ack timed
out:

```
tc358764 …: write 0x0504 <- 0x0000001f failed: -5 (continuing)
tc358764 …: ID after full reset: 0x0
tc358764 …: bridge did not answer SYS_ID; programming anyway
tc358764 …: verify: VP_CTRL=0x0 SYS_ID=0x0
tc358764 …: error initializing bridge (-19)
```

`VP_CTRL` was written (with `VP_CTRL_RGB888|EVTMODE|HSPOL|VSPOL|VSDELAY(15)`)
and reads back **0**.  So on this boot path the bridge has no working control
channel in either direction: it is not "programmed but with the wrong values".

Combined with §7.4 and §7.2, the bridge is therefore either held in reset or
unpowered — not mis-programmed.  Its three supplies are dummy regulators
(`vddc`, `vddio`, `vddlvds` all "not found") and `reset-gpios` is absent.

Note also that this is a *different starting point* from any QNX observation:
the reference *inherits* a bridge the RIM bootloader configured (and, per the
relay, most likely never loses that state across a warm reset), whereas our
boot ROM -> chainloader -> Linux path runs no bootloader at all, so the bridge
starts from power-on defaults every time.

That is why the next step became a static sweep for the reset/enable
(**report 122**) rather than more register work.

---

### 7.6 The reset was the missing input — and the bridge is alive

Report **122** (static sweep of the whole QNX boot set) found the reset: **`GPIO2_24`
(`gpio_56`, pad `0x4A100080`, GPMC_NADV_ALE)** is driven by `winch_lcdctl` as a
**20 ms active-low pulse** — `CLEARDATAOUT 0x01000000` at vaddr `0x192d4`,
`usleep(20000)`, `SETDATAOUT` at `0x19338`, `usleep(20000)` — taken after the DSI
programming and before the panel strap read.  We had it as a static level-high
hog, so the bridge never saw a reset edge.

Implemented: `reset-gpios = <&gpio2 24 GPIO_ACTIVE_LOW>` on the bridge node, a
20 ms pulse in `tc358764_reset()`, and pad muxes for `0x080` (the reset) plus
`0x058`/`0x17c` (the bridge's two status balls, `gpio_36`/`gpio_171`).  The QNX
boot set itself never writes a pad mux (report 122), so those pads are only
correct because we set them.

**Result — the bridge came alive as far as hardware status goes:**

```
GPIO2_DATAIN = 0x01000010     bit 4 = 1
```

Bit 4 is `GPIO36`, the bridge's own GPIO ball wired back into the OMAP, and QNX's
own self-test refuses to believe the link unless `GPIO36` and `GPIO171` both read
high (`"GPIO status good (GPIO36 and GPIO171 high)"`).  So the bridge is powered
and out of reset — the `-EIO` failures are **not** a dead bridge.

What still fails is the **control channel**:

```
tc358764 …: write 0x0504 <- 0x0000001f failed: -5 (continuing)
tc358764 …: ID after full reset: 0x0
tc358764 …: verify: VP_CTRL=0x0 SYS_ID=0x0
```

The OMAP host issues command transfers on `VC_CMD` (= 1; `VC_VIDEO` is 0), and
the BTA times out, so we cannot read `SYS_ID` or read back `VP_CTRL`.  Nothing
proves the bridge listens on vc 1 — trying **vc 0** is the obvious next probe.

Meanwhile the cheapest hypothesis is that the bridge is **already configured**
(the RIM bootloader does it; per the relay that state survives an OMAP reset, and
the reference never reprograms it at display time).  If so, the panel is black
because our *stream* differs from the configuration it was left in — start with
the video-mode subtype (`MIPI_DSI_MODE_VIDEO_BURST` + `VP_CTRL_EVTMODE` vs QNX's
`DSI_CTRL 0x00EAEF9D` against our `0x00EAEE9F`).

---

## 8. Bench notes from this stretch

* **A flash cycle's precondition is that the unit is running media, not the
  ramloader — this is the whole "flashes got slow" mystery.**  The previous
  flash leaves the unit in `0fca:8001`; in that state there is no
  `/dev/ttyACM0`, so `pkexec chmod` and `pbcon 'reboot'` fail **silently**, the
  SoC is never rebooted, and `pre_os_ifs_flash.py`'s `wait_bootrom()` (which
  polls for `0fca:0001` only) waits while the ramloader times out in ~90–120 s
  and falls back to the bootrom.  Hence "~2 minutes, green LED on, then it
  works", independent of frame count.  Fixes: end each flash with
  `force_reset_loader.py` (retry if it prints "no ramloader enumerated", which
  is just a race), and when already in the ramloader use
  `--reuse-loader`.  The flash itself took **13 s** here
  (`plan:` 16:14:13 → `done (181 frames)` 16:14:26).
* **`BOOTCTR_FCTRY` was `0x28` (40) at the end of this session**, not `0x32`:
  `/accounts/devuser/.bootctr.log` shows
  `REFUSING to write - temp byte '' != '2'` on the last TabletOS boot, i.e. the
  guard in `top_up_bootctr` correctly declined to write when its temp-file
  verification read back empty.  Re-running the same two `dd` steps by hand
  immediately afterwards produced `32 00 00 00` and `t=[2]`, so the failure was
  transient.  **Ten image boots this session account for 50 → 40.**  The
  operator repaired the counter manually; do not write RPMB from the agent.
* Never judge the panel without `fb <rrggbb>` first (report 121 §1.2), and read
  `kmsg` before theorising — two rounds of GPIO guessing were answered in one
  `kmsg tc358764`.


---

## 9. The question that was handed to the previous session's agent

> How is the TC358764 meant to be programmed on this board?  111 concluded
> `winch_lcdctl` never touches it and "the RIM bootloader must configure it" —
> but we boot via the bootrom + our chainloader, so nothing configures it.
>
> * **Control port:** I²C or DSI-generic?  Which bus, which address, and with
>   what register-address width?  On Linux, `0x09` on i2c0 answers
>   *8-bit*-register reads but NAKs 16-bit addressing, `0x0F` is absent, i2c-1
>   hangs.
> * **RESETN / power enable:** what is it?  It is not one of the reference-driven
>   GPIOs — we now hold `GPIO1` 2/6/14 and `GPIO2` 9/12/13/16/24/28/29 high and
>   `DATAOUT` matches QNX exactly.
> * **Persistence:** does the bridge's configuration survive an OMAP reset (like
>   the LM3532's registers do), or must it be re-programmed every boot?

What was planned either way:

* *I²C and survives resets* → reproduce the init over I²C (`i2c16` already
  exists in the console) and the panel is nearly done.
* *DSI-generic required* → the fix is host-side: make `omap_dsi_host_transfer()`
  accept the transfer in the link state we have, since both `attach` (-EINVAL)
  and `pre_enable` (-EIO/BTA) reject it today.
* *Must be re-programmed and is DSI-only* → a kernel driver that programs it
  before the video mode starts, which needs RESETN identified first.

## 10. Tools added in this stretch (all in `analysis/initramfs/console.c`)

| command | purpose |
|---|---|
| `spi tcon` / `spi w <hexword>…` | MCSPI4 transfers (450 kHz, 8-bit, per-word, 100 ms gaps) |
| `fb <rrggbb>` | paint `/dev/fb0` — **required** before judging the panel |
| `kmsg [substr]` | dump `/dev/kmsg` (non-blocking, filtered, 20 000-record scan) |
| `i2c16 r|w <bus> <addr> <reg16> …` | 16-bit-register I²C over `I2C_RDWR`, 1–4 byte writes |

`kmsg` is the one that earned its keep: it turned two rounds of GPIO guessing
into a two-line answer (§7.1).

---

## 11. The reference runs FOUR DSI data lanes, not two (2026-11-15)

This is a correction to 118 §8.1 and to everything built on it.  Reading
`DSI_COMPLEXIO_CFG1` from both systems settled it:

```
Linux (2-lane build): 0x2A000321   CLK=1 DATA1=2 DATA2=3 DATA3=0 DATA4=0
QNX (reference):      0x2A054321   CLK=1 DATA1=2 DATA2=3 DATA3=4 DATA4=5
```

So the reference drives **four** data lanes.  The arithmetic agrees: 1024x600 at
60 Hz, RGB888, is 50.4 MHz × 24 bpp = **1.2096 Gbps**; over 4 lanes that is
302.4 Mbps/lane = a **37.8 MHz byte clock**, which is exactly QNX's
`txbyteclkhs`.  The two-lane build had to run a 75.6 MHz byte clock — the "2×"
the relay explained as an encoding difference was really a *lane-count* mistake.

### 11.1 The fix and what it changed

```dts
dsi1_out: endpoint { lanes = <0 1 2 3 4 5 6 7 8 9>; };   /* clock + 4 data */
bridge@0 { lanes = <4>; };
```

Measured after the change:

| register | 2 lanes | 4 lanes | QNX |
|---|---|---|---|
| `DSI_COMPLEXIO_CFG1` | `0x2A000321` | **`0x2A054321`** | `0x2A054321` |
| `DSI_VM_TIMING3` (TL) | `0x07E00258` (2016) | **`0x03F00258`** (1008) | `0x03F00258` |
| `DSI_PLL_CONFIGURATION1` | `0x1CE1F81F` | `0x0C60FC1F` — m=126 n=16 mX=4 ⇒ clkdco 604.8 MHz, txbyteclkhs 37.8 MHz | `0x14A5E8BE` — m=756 n=96 ⇒ same clkdco, same 37.8 MHz |
| `DSI_IRQSTATUS` | `0` | **`0x80`** (PLL lock) | `0x80` |
| `DSI_COMPLEXIO_IRQ_STATUS` | `0` | `0` | `0` |
| `DSI_VC_IRQSTATUS(0)` | `0x4` | `0x4` (PACKET_SENT) | `0x4` |

### 11.2 The last stream difference: DSI_CTRL

`DSI_CTRL` differed in exactly two bits.  Decoded from TRM Table 10-530:

| bit | field | upstream (ours) | QNX |
|---|---|---|---|
| 1 | `CS_RX_EN` (RX payload checksum) | 1 | **0** |
| 8 | `VP_CLK_POL` (video-port sampling edge) | 0 | **1** |

`omap_dsi_configure_videomode()`-time code in `dsi.c` sets both; they were
changed to the reference's values (with a comment).  `DSI_CTRL` then reads
**`0x00EAEF9D`**, byte-identical to QNX.  `CS_RX_EN` is receive-only, so the
meaningful one is `VP_CLK_POL`.

### 11.3 Result: everything measurable now matches, and the panel is still black

With 4 lanes, matching `DSI_CTRL`, a locked PLL, no DSI errors, the DISPC
scanning a blue framebuffer (`fb 0000ff`, `GFX_BA0` reads `0x000000FF`), the
backlight on and the TCON table sent — the panel is still black.

Remaining measured differences from the reference (nothing else differs):

* `DISPC_TIMING_H` field distribution — `137/180/0` vs `137/161/19`.  Both total
  320 blanking + 1024 = 1344 ⇒ same 50.4 MHz.
* `DISPC_DIVISOR` — LCD 3 vs 2 (both give 50.4 MHz from different DSS FCK).
* `DSI_VM_TIMING1` porches — HBP/HFP/HSA `103/134/0` vs `116/121/1` (TXBYTECLKHS
  units; totals match).
* `DSI_CLK_CTRL` — `0xA0306008` vs `0xA0346005` (LP clock divisor 8 vs 5, and
  bit 18).
* the `DSI_PHY_CFG*` values (mainline computes its own DDR/HS-LP numbers).

**Prime remaining suspect: the bridge's LVDS/LCD block is not enabled.**  Its
control port only accepts DSI-generic packets, and that channel does not answer
us (`SYS_ID` 0, `VP_CTRL` reads back 0, BTA times out on `VC_CMD`).  Nothing in
the QNX boot set programs the bridge (report 122; confirmed by scanning the whole
sealed IFS for DSS/DISPC/DSI literals — the only hits are GPIO1/2/4 bases in
`winch_lcdctl`), so the reference must inherit its state from the **boot ROM**.
The open question for the relay: **does the boot ROM program the bridge on the
signed product path only, i.e. is it skipped on the CRC/factory path our
chainloader uses?**  If so, our path can never inherit a working bridge and we
must make the DSI command channel work (starting with `VC 0` instead of
`VC_CMD = 1`).

### 11.4 State / images

Image crc at the end of this round: `0xE1F79294` (4 lanes + `DSI_CTRL` matched +
bridge never programmed).  Watch out for the earlier ones that must **not** be
used as a reference for the DSI configuration: `0xB9A1BC4F` / `0x5E40C5C1`
(2 lanes; `0x5E40C5C1` also had the rejected sync-pulse mode).

---

## 12. Bridge retired as a variable; ordering and dpms tested (2026-11-15, later)

The relay's 2026-11-15 answer (relayed through the operator) settles the bridge:

* **Nothing programs it and it needs no programming.**  Boot chain is boot ROM →
  eMMC second-stage loader → IFS → RFS.  The ROM *cannot* (a DSI host with
  PHY/PLL/packet engine is required); the second-stage loader (`hd0head.bin`,
  first 2 MB checked) contains no DISPC/DSI/PHY/PLL constants and no
  display-related strings; the IFS has `winch_lcdctl` only, which report 111
  shows sends no bridge configuration.
* **The IFS splash proves it in one observation**: `winch_lcdctl -s splash` puts
  up the logo before the RFS is mounted — `GFX_SIZE = 0x003F01ED` (494x64) at
  `GFX_POSITION = 0x010C0109` (265,268), a centred 494x64 bitmap in a 1024x600
  field — so DISPC + DSI + the SPI TCON init + the LM3532 all work with the
  TC358764 untouched.  It runs from its power-on/strapped state.
* Also retracted: `loader_06001A06-00.bin` is a **ramloader** uploaded over USB by
  the host tools — never on media, never in the boot chain.
* And: the "2× was benign / an encoding artefact" line in §6/§7 is **wrong** —
  the 4-lane finding in §11 is the explanation.  118 §8.2 and 111 §8 item 4 have
  been corrected in place.

Consequence: our image deliberately leaves the bridge alone (and must never write
`SYS_RST <- 0x1f` — §7.6, trap 28).  A failed BTA only shows the bridge does not
answer *reads*; it does not show that writes did not land, so a non-zero `SYS_ID`
is not the only possible sign of life.

### 12.1 Backlight and TCON: both confirmed, neither is the gap

* LM3532 at `0x38` **ACKs**, `0x1d = 0xf9` (control bank A enabled),
  `0x74 = 0xff`, `/sys/class/backlight/backlight` with `bl_power = 0` and
  `actual_brightness = 255` — the panel is lit (report 120; operator-confirmed).
* The TCON table has now been sent in **both byte orders** and in **both
  orderings**: after the video stream started (the normal bench path), and with
  the stream down (bridge driver `unbind`ed via
  `/sys/bus/mipi-dsi/drivers/tc358764/unbind`, table sent, driver re`bind`ed), which
  reproduces QNX's order — its step 8 is the TCON init, step 9 is DSI/DISPC.
  **No effect either way.**

Note for the next session: `/sys/class/drm/card0-LVDS-1/dpms` rejects writes
(`off` → EACCES) even though it reads `On`, and this kernel's fbdev has no
`blank` attribute — the `unbind`/`bind` route is what works for stopping the
stream.

### 12.2 What is left

Only timing *encoding*, and the totals already agree (1344 x 625 ⇒ 50.4 MHz):

| register | ours | QNX |
|---|---|---|
| `DISPC_TIMING_H` | `0x0890B400` = 137/180/0 | `0x0890A113` = 137/161/19 |
| `DISPC_DIVISOR` | `0x00010003` (LCD 3) | `0x00010002` (LCD 2) |
| `DSI_VM_TIMING1` | `0x00086067` = HBP 103, HFP 134, HSA 0 | `0x01079074` = 116/121/1 |
| `DSI_CLK_CTRL` | `0xA0306008` (LP divisor 8) | `0xA0346005` (5, + bit 18) |

Force all four together (never a partial mix).  If that changes nothing, question
the `DSI_PHY_CFG*` values (QNX's are in 118 §3.3; mainline computes its own), and
then whether this TCON wants a different LVDS mapping than the driver's VESA-24
`LV_MX` table — which we cannot change, since the bridge cannot be programmed.
