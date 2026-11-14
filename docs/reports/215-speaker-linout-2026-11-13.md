# 215 — the speaker path: the codec's line outputs, an active-low `spk` enable, and GPIO1_15 (session 47)

Date: 2026-11-13/14 (session 47, Linux-era 41).
Bench: unit on our Linux image region 0 **`0xAD85DF0F`** (`0x939D3C` B, kernel
`7.3.3` **#44**) and region 1 rootfs **`0342ab06…`** (the speaker path in
`playbook-wm8994.c`).  The oracle (1917 minimal IFS at `0x825`) was booted for
the speaker capture and the unit returned to Linux.  No NVRAM/MCT/RPMB write;
the image's own `/init` top-up ran at each boot (`BOOTCTR_FCTRY` 0x20 → 0x2c,
read as a health check).  **Verdict (operator): both speakers play the 440 Hz
tone**, at DAC1 volume 96 (loud) and at 40 (clearly audible); nothing about the
working two-link path changed.

## 0. Headline

The speaker was silent because **three things outside the data path** were
wrong, and none of them is a codec data/clock issue:

1. **The speakers hang off the codec's line outputs, not its class-D
   amplifier.**  While the reference plays through the speakers its codec reads
   `PM1 = 0x0013` (no HPOUT1, no SPKOUT), `PM3 = 0x3cf0`
   (`LINEOUT1P/N` + `LINEOUT2P/N` + the output mixers), `R0x036 = 0` (the
   speaker mixer is *off*), `R0x060 = 0`, and the route is
   `DAC1 -> MIXOUT (R0x02d/0x02e) -> LINEOUT1P/LINEOUT2P (R0x034/R0x035 bit 0)`
   with `R0x01e = 0` (all four line-output mutes cleared) and
   `R0x038 = 0x0080` (`LINEOUT_VMID_BUF_ENA`).  Our repair had none of the
   line-out bits and kept the class-D route (PM3 `0x03f0`, `R0x036 = 0x0f`).
2. **`spk` is an active-low amplifier enable.**  The reference's
   `codec_gpio_init` leaves GPIO2_12 (`spk`, hwio signal 0x93) **high** (amp
   off) and `enable_codec_line_out` drives it **low for the whole time the
   speakers play**, raising it again at teardown.  Our board DTS held it high
   permanently as a "display enable" (`disp-en-12-hog`), so the amp was never
   enabled.  The machine driver now owns the line (`spk-gpios`), drives it low
   for the speaker route and high on `trigger(STOP)`.
3. **GPIO1_15 must be high.**  It is the reference's runtime-only board output
   (the startup table leaves it low, the QNX GPIO1 data word reads `0x0000a044`
   = bits 2/6/13/**15** while it plays), and with `spk` low but GPIO1_15 low the
   speaker is still silent.  It is the amplifier rail enable the QNX audio
   driver asks `powerman` for as the **`audio_boost`** resource (the driver has
   no register write for it — the request is PPS).  The machine driver now
   drives it high (`boost-gpios`).

The codec registers alone were *necessary but not sufficient*: with them
matched the speaker stayed silent until both board lines were right.  That is
why the register diff against the reference could not close the item.

## 1. What the reference does (measured)

The minimal 1917 oracle was booted, `io-audio -d omap4abe-wm8994
aif=0,secure_capture -o enable_buffering` started, and the driver's own
`Headphone Select` mixer control set to **false** (the driver's config file
`/var/etc/system/config/audio/OMAP4 @ 1a.io-ado-cfg` is generated at run time;
`false` = speaker/LINE_OUT, `true` = headphone).  With a 440 Hz wav playing the
operator confirmed the speakers sound (quietly).  The codec then read:

| register | value | meaning |
|---|---|---|
| `R0x001` | `0x0013` | VMID + MICB1 only; **no HPOUT1, no SPKOUT** |
| `R0x003` | `0x3cf0` | LINEOUT1P/N + LINEOUT2P/N (bits 12:10) + output mixers (7:4) |
| `R0x01e` | `0x0000` | all four line-output mutes cleared |
| `R0x034`/`R0x035` | `0x0001` | MIXOUTL→LINEOUT1P, MIXOUTR→LINEOUT2P |
| `R0x036` | `0x0000` | speaker mixer off (the class-D route is unused) |
| `R0x038` | `0x0080` | `LINEOUT_VMID_BUF_ENA` |
| `R0x039` | `0x01c8` | VMID ramp / discharge |
| `R0x04c` | `0x1f25` | charge pump **off** |
| `R0x060` | `0x0000` | analogue HP path off |

Board lines while it plays: GPIO2_12 (`spk`) **low**, GPIO1_15 **high**,
GPIO4_31 (`ldo`) high, GPIO2_17 (`hp`) low (jack out).  The jack-detect ISR
auto-routes to the speaker when `hp` reads 0.

## 2. What the reference's driver does (static RE)

`deva-ctrl-omap4abe-wm8994.so` (sha256 `2b346de3…`), `enable_codec_playback_device`
(VA `0x2c814`, branch `enable_codec_line_out`):

```
FUN_0002a008(ctx,1)                 ; powman_alive("audio_boost") -- a PPS request
gpio_set_output(spk, 0)             ; ASSERT the speaker amp (active low)
R0x39 |= 0x01ce ; R0x38 |= 0x80 ; PM3 |= 0x3cf0
R0x38 &= ~0x30 ; R0x39 &= ~0x06 ; R0x1e &= ~0x66
```

and `disable_codec_playback_device` (VA `0x2c5fc`) mirrors it: mutes back, PM3
line-outs off, `gpio_set_output(spk, 1)`, then `powman_idle("audio_boost")`.
`codec_gpio_init` (inlined in `codec_mixer`, VA `0x2e8c4`) is the only other
user: `ldo` low 100 ms → high, `spk` high, `hp` input.  The headphone branch
never touches `spk` and enables `R0x4c` bit 15 (charge pump), `PM1` bits 9:8,
`R0x60` and `R0x1c` instead.

The **`audio_boost`** request is a powerman PPS resource
(`powerstate/audio_boost/*`); powerman's device table also carries
`ldo_regen1`/`ldo_regen2` and `rocket_ldo0/3/10/12`.  On the bench, the reference
plays with `spk` low and GPIO1_15 high; on Linux, `spk` low alone is silent and
GPIO1_15 high makes it sound, so GPIO1_15 is the board line that request ends up
asserting on this unit.

## 3. The Linux change (module + DTS)

`sound/soc/ti/playbook-wm8994.c`:

* the repair now follows the jack (`gpiod_get_value_cansleep(hp)`):
  headphones → HPOUT1 + charge pump + `R0x60 = 0xcc`;
  jack out → HPOUT1/SPKOUT/charge-pump off, `R0x60 = 0`, `R0x36 = 0`,
  `R0x39 = 0x01c8`, and the line-out block:
  `R0x38 |= 0x0080`, `PM3 |= 0x3c00`, `R0x1e mutes = 0`, `R0x34/R0x35 = 1`.
* `spk-gpios` (GPIO2_12) is driven low for the speaker route and high on
  `trigger(STOP)`.
* `boost-gpios` (GPIO1_15) is driven high at probe.

`arch/arm/boot/dts/ti/omap/omap4-winchester.dts`: the `disp-en-12-hog` is
removed (GPIO2_12 was never a display enable — the reference calls it `spk`),
and the `sound` node gains `spk-gpios` and `boost-gpios`.

## 4. Measurement on Linux (final image)

`repair: jack out (speaker)` then `repair: done, FLL1 locked`; the codec reads
`PM1 = 0x0003`, `PM3 = 0x3cf0`, `R0x01e = 0`, `R0x034/0x035 = 1`,
`R0x036 = 0`, `R0x038 = 0x80`, `R0x039 = 0x1c8`, `R0x04c = 0x1f25`,
`R0x060 = 0`; `/sys/kernel/debug/gpio` shows `spk` out **lo** and `boost` out
**hi** during the stream, and `spk` back **hi** after it.  The operator heard
both speakers at DAC1 volume 96 and again, clearly, at 40.

`R0x01e` and the line-out route had to be written directly: the WM8994 codec
driver exposes no line-out mixer control, and DAPM never routes LINEOUT.

## 5. What this changes, and what is left

* **The speaker nicety is closed.**  Headphones and speakers both work, with
  the reference's jack-dependent routing.
* The **`pcm:` / `live N:` diagnostics** are still in the tree (ROADMAP next
  item (a)).
* The **`audio_boost`** resource could not be identified as a named PMIC rail;
  GPIO1_15 is what makes the difference on this unit.  If a schematic appears,
  check whether it is an external LDO enable.
* Nothing in the working two-link path changed: `mcbsp2 + wm8994-aif1` clocks,
  `mcbsp3 + wm8994-aif2` data, `XCR2 = 0x8041`.

## 6. Corrections

* **Handoff/STATE session 46:** "the likely gate is board-level: the spk line
  or the hp jack detect" — correct in spirit, but `spk` is **active low**, and
  the *codec's* output route (line out, not class-D) was also wrong.
* **Trap 204/205** stand; this report adds the output-stage facts only.
