# 199 — WM8994 audio: the codec is alive; the machine link is the work

Date: 2026-11-09 (session 38, Linux-era 32), item 9 step 5's audio half.
Read-only throughout: the codec ID was read over I²C; nothing was written to
the codec, the media, NVRAM or RPMB.

## 0. Headline

* **The codec answers.**  `i2ctransfer -y 3 w2@0x1a 0x00 0x00 r2` on the live
  Linux image returns **`0x89 0x94` = WM8994**, at I²C4 `0x48350000`
  (Linux `i2c-3`), 7-bit `0x1a`, 16-bit register addressing.
* **There is no mainline OMAP4+WM8994 machine driver.**  The codec/MFD drivers
  are mainline (`CONFIG_MFD_WM8994`, `CONFIG_SND_SOC_WM8994`), but
  `SND_SOC_WM8994` has **no Kconfig prompt** — a machine driver must select
  it.  The OMAP4 ASoC machine drivers in 7.3.3 are only the ABE/twl6040 and
  OMAP3 ones.
* **The 1917 stack is ABE + McBSP:** `/base/scripts/startup.sh` runs
  `io-audio -d omap4abe-wm8994 aif=0,secure_capture -o enable_buffering`;
  the driver is `deva-ctrl-omap4abe-wm8994.so` (libabe + McBSP + WM8994).
* **The gating hazard is the ABE segment.**  McBSP1/2/3 are inside
  `l4_abe` (`0x40122000`/`0x40124000`/`0x40126000`), and the board disables
  the whole segment because the **WD_TIMER3 target (0x40130000) hangs
  ti-sysc** after its revision read.  The path is to enable `l4_abe` with the
  WD_TIMER3 target disabled.

## 1. The codec

```
i2ctransfer -y 3 w2@0x1a 0x00 0x00 r2   ->  0x89 0x94
i2cdetect -y -r 3 0x1a 0x1a             ->  1a
```

The ID read already proves DBVDD/DCVDD are up and the part is in a sane state.
The MFD driver (`wm8994-core.c`) requires **seven** supplies via
`regulator_bulk_get()`:

```
DBVDD, DCVDD, AVDD1, AVDD2, CPVDD, SPKVDD1, SPKVDD2
```

The board's always-on TWL rails are the likely mapping: 1.8 V (`v1v8`) for
DBVDD/DCVDD/AVDD1, 2.1 V (`v2v1`) for AVDD2/CPVDD.  **SPKVDD is provisional**
(the speaker rail is not identified); it is fine for a probe, not for the
audio path.  VDAC (1.8 V) and VANA (2.1 V) are already `regulator-always-on`
in the board DTS.

## 2. The 1917 audio stack

`/base/scripts/startup.sh` (the `SERVICE_audio_winchester` handler):

```
io-audio -d omap4abe-wm8994 aif=0,secure_capture -o enable_buffering
WAITFOR "mixer" /dev/snd/mixerC0D0
mix_ctl group "Input Gain" volume=90%
```

The driver's own option help (from the 1917 `.so`) says:

```
aif=[0..3]                Interface index.
i2c_addr=xx               I2C slave address of Codec.
mcbsp=[0..4]              McBSP to attach to (0 == interface disabled).
protocol=[i2s|pcm|tdm]    Audio Protocol. (default: i2s).
clk_mode=[0|1]            Set McBSP as clock master(1) or clk slave(0).
rate=min[:max]            Set sample rate min and max values (default: 48kHz).
```

It is an **ABE** driver: it calls `libabe` (`abe_initialize`, `abe_poweron`,
`abe_configure_port`, `abe_connect_serial_port`, …) and drives McBSP1/2/3.
`aif=0` selects codec AIF1.  The exact default McBSP is not in the strings;
the QNX driver's `omap4_parse_commandline` sets it, and `mcbsp=[0..4]` means it
must be named or defaulted — determine it from the ABE port map before the
mainline link, or try McBSP2 first.

## 3. The ABE segment and the WD_TIMER3 hazard

`arch/arm/boot/dts/ti/omap/omap4-l4-abe.dtsi` puts the audio peripherals in
`l4_abe`: `mcbsp1@40122000`, `mcbsp2@40124000`, `mcbsp3@40126000`, `mcasp`,
`dmic`, `mcpdm`, the ABE timers and **`target-module@30000` = WD_TIMER3
(0x40130000)**.  The board DTS currently has:

```dts
&l4_abe {
	status = "disabled";
};
```

because, once the segment populates, the WD_TIMER3 target hangs in the
ti-sysc enable/softreset path right after its revision read.  The WD target has
**no label** (`wdt3: watchdog@0` is its child), so the plan is:

1. add a label to `target-module@30000` (`wdt3_target:`), and
2. in the board DTS, set `&l4_abe { status = "okay"; }` and
   `&wdt3_target { status = "disabled"; }`.

Then the McBSP targets can probe without the WD_TIMER3 init.  Other non-audio
targets (the ABE timers, MCASP, DMIC, McPDM) may also need disabling if they
hang — one at a time.

**Bench risk.**  The loader arms WDT2 at 7.8125 s; the kernel reprograms it to
58.6 s at a late initcall.  A hang in an early device probe therefore resets
the unit before the 58.6 s window is programmed, and a deterministic hang can
reset-loop.  The counter self-replenishes only after a successful boot, so a
loop can drain it.  Keep the known-good image (`0x82FC0D35` or `0xE36C679A`)
ready to reflash, and do this as one variable per boot.

## 4. Plan

1. **ABE segment only** (one boot): label `wdt3_target`, enable `l4_abe`,
   disable `wdt3_target`.  Verify the segment and the three McBSP targets probe
   and the unit does not hang/reset.  No codec/machine changes yet.
2. **Codec + machine link:** enable `CONFIG_MFD_WM8994` and a board Kconfig
   entry that `select SND_SOC_WM8994` (and `SND_SOC_OMAP_MCBSP`), add the
   `wlf,wm8994` node on `&i2c4` with the supplies, and add the DAI link —
   either `simple-audio-card` (`SND_SIMPLE_CARD`) or a small machine driver.
   The codec DAI is `wm8994-aif1`; the CPU DAI is `omap-mcbsp.<n>`.
3. **Audio:** a playback test (`aplay`/`speaker-test`, or a tiny raw write) and
   the mixer path (`mix_ctl` equivalent).

## 5. Executed — the ABE segment (session 38)

**Image `0x50842767`** (`0x91A9D8`, kernel 7.3.3) with:

* `omap4-l4-abe.dtsi`: `wdt3_target: target-module@30000`;
* board DTS: `&l4_abe { status = "okay"; }` + `&wdt3_target { status = "disabled"; }`.

**It booted and the segment probed.**  From the boot log (the `PBS:` trace):

```
4012208c.target-module rev=00000000  enable done  reset done  pm ok   (McBSP1)
4012408c.target-module rev=00000000  enable done  reset done  pm ok   (McBSP2)
4012608c.target-module rev=00000000  enable done  reset done  pm ok   (McBSP3)
...
```

`40130000` (WD_TIMER3) is **not** in the probe list — the disabled target was
skipped.  The other ABE targets (MCASP `40128000`, DMIC `4012e000`, McPDM
`40132000`-range, the ABE timers `40138000`–`4013e000`) also probed.  The unit
enumerated `0fca:8010`, reached a shell, `/proc/uptime` = 37 s, and the
boot-time top-up read `BOOTCTR_FCTRY = 0x30` (`>= 0x1e`, no write).  No hang,
no reset loop.

Remaining for audio: the machine link (step 2 of §4).

## 6. Executed — the machine link, and the USB regression it caused

The first machine-link build put the whole sound stack **in the kernel**:
`wm8994`, `snd-soc-wm8994`, `omap-mcbsp` and `simple-card` all probed at boot,
and `asoc-simple-card sound` registered the card from **3.35 s to 4.63 s** — a
~1.3 s delay.  The USB gadget bound at 4.77 s but the host configured it only
after a physical replug (48 s): the same warm-reset regression as traps
130/141.

**Fix:** the sound stack is **modules** (`MFD_WM8994=m`, `SND_SOC_WM8994=m`,
`SND_SOC_OMAP_MCBSP=m`, `SND_SIMPLE_CARD=m`, selected by a new
`SND_SOC_PLAYBOOK_WM8994` board Kconfig).  The region-1 `rcS` loads them
**after** the initramfs has created the USB gadget:

```
wm8994 snd-soc-wm-hubs snd-soc-wm8994 snd-soc-ti-sdma
snd-soc-omap-mcbsp snd-soc-simple-card-utils snd-soc-simple-card
```

Result on image **`0x3B2ECFED`** + rootfs **`07d4e28b…`** (24 modules):

* the gadget is configured at **5.33 s**; the audio modules load at
  **6.8–7.0 s** — after the USB connect;
* the unit enumerates **by itself** (~22 s), no replug;
* `dmesg | grep -ci "unknown symbol"` = **0** (the first rootfs lacked
  `snd-soc-simple-card-utils`, which produced a page of those);
* `/proc/asound/cards` = `0 [WM8994]: simple-card - PlayBook WM8994`;
* `/proc/asound/pcm` =
  `40124000.mcbsp-wm8994-aif1 … playback 1 : capture 1`.

The kernel's `ALSA device list: No soundcards found.` at 4.5 s is expected: the
card is a module registered later.  The four
`wm8994-codec: Failed to request Mic1/Mic2 detect IRQ: -22` lines are the
codec's jack-detect IRQs; the codec node has no `interrupts` (the board's codec
IRQ line is not identified), so the requests fail harmlessly.

## 7. Sources

* Live Linux: the I²C transfer/detect above; `/proc/bus/input/devices` for the
  touch (unrelated); the boot log.
* 1917 `/base`: `scripts/startup.sh` (`SERVICE_audio_winchester`),
  `lib/dll/deva-ctrl-omap4abe-wm8994.so` (option strings and ABE symbols),
  `etc/system/enum/devices/audio`.
* 7.3.3 tree: `drivers/mfd/wm8994-core.c` (the seven supplies),
  `sound/soc/codecs/Kconfig` (`SND_SOC_WM8994` hidden),
  `sound/soc/ti/Kconfig` (no OMAP4+WM8994 machine),
  `arch/arm/boot/dts/ti/omap/omap4-l4-abe.dtsi` (McBSP/WD_TIMER3),
  the board DTS (`&l4_abe` disabled, VDAC/VANA always-on).
* Report 176 §3 (the inventory), DECISIONS §47 (touch, the same session).
