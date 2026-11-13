# 211 — The reference codec-GPIO init, implemented in the machine driver: negative for MCLK1, positive for codec probe reliability (session 44)

Date: 2026-11-12 (session 44).  Image `0xB43F169B` (0x93A3E0), region 1
rootfs sha256 `3df8056c…`.  Built, flashed (both regions) and booted.

## 0. Headline

The reference's `codec_gpio_init` sequence (report 210 §3) is now in
`playbook-wm8994.c`: the **ldo** line (GPIO4_31) is a boot-on fixed regulator
that the driver force-disables for 100 ms and re-enables; the **spk** line
(GPIO2_12) is already held high by the board's gpio2 hog; **hp** (GPIO2_17) is
requested as an input.  Results:

* **MCLK1 is still absent** — after `pbplay`, the driver itself reports
  `repair: done, FLL1 NOT locked`, so this sequence is **not** the missing gate.
* **The WM8994 probe became reliable.**  On the two builds where the ldo line
  was left as an input until the machine driver loaded, the codec's ID read
  failed (`Failed to read ID register`, -121) and no card was registered.  With
  the boot-on regulator (line high from `late_initcall`, before the rcS module
  load) it probes on every boot and `0 [WM8994]` registers.  This is a real
  board/chain fact: the codec needs that line high before its driver probes.

## 1. What was changed

### Device tree (`arch/arm/boot/dts/ti/omap/omap4-winchester.dts`)

```
	audio_ldo: regulator-audio-ldo {
		compatible = "regulator-fixed";
		regulator-name = "audio-ldo";
		regulator-min-microvolt = <1800000>;
		regulator-max-microvolt = <1800000>;
		gpio = <&gpio4 31 GPIO_ACTIVE_HIGH>;
		enable-active-high;
		regulator-boot-on;
	};

	sound {
		...
		ldo-supply = <&audio_ldo>;
		hp-gpios = <&gpio2 17 GPIO_ACTIVE_HIGH>;
	};
```

The former `ref-hog-31` on GPIO4_31 is removed (the regulator owns the line and
leaves it high).  There is deliberately **no pinctrl group** for the three
pads: the pads are already in GPIO mode at boot, and a pinctrl group collides
with the gpio2 hog on pad 0x068 (`pin PIN20 already requested by 48055000.gpio`).

### Machine driver (`sound/soc/ti/playbook-wm8994.c`)

`playbook_wm8994_board_enables()` runs at probe, before the card is registered
and therefore before any `hw_params`/FLL work — the same point in the chain as
the reference's `codec_gpio_init`:

```
ldo = devm_regulator_get_optional(dev, "ldo");
regulator_force_disable(ldo);      /* regulator_disable() returns -EIO on a
                                      boot-on regulator with use_count 0 */
msleep(100);
regulator_enable(ldo);
hp = devm_gpiod_get_optional(dev, "hp", GPIOD_IN);
```

`force_disable` is the important detail: a `regulator-boot-on` regulator has no
consumer enable count, so the normal `regulator_disable()` warns
"unbalanced disables" and returns `-EIO` without gating the line.

## 2. The test

`pbplay 8 440` on the booted image, then the driver's own report:

```
wm8994-codec wm8994-codec: WM8994 revision D CUST_ID 00
playbook-wm8994 sound: audio ldo pulsed, now on
playbook-wm8994 sound: audio hp detect on gpio 529 (input)
0 [WM8994]: PlayBook_WM8994 - PlayBook WM8994
...
wm8994-codec wm8994-codec: repair: done, FLL1 NOT locked
omap-mcbsp 40124000.mcbsp: DXR 0x00000000 / SPCR2 0x0235 / PCR0 0x000f ...
```

(The McBSP dump is the E1 consumer configuration; the stream stalls at the
transmit path exactly as with no MCLK1.)

## 3. A trap found on the way: Linux gpio numbers are not OMAP gpio numbers

`/sys/class/gpio` assigns bases **in probe order**, and the probe order is not
the bank order:

| OMAP bank | device | class base | OMAP lines |
|---|---|---|---|
| GPIO2 | `48055000.gpio` | 512 | 32–63 |
| GPIO3 | `48057000.gpio` | 544 | 64–95 |
| GPIO4 | `48059000.gpio` | 576 | 96–127 |
| GPIO5 | `4805b000.gpio` | 608 | 128–159 |
| GPIO6 | `4805d000.gpio` | 640 | 160–191 |
| GPIO1 | `4a310000.gpio` | 676 | 0–31 |

So OMAP `gpio_127` = **global 607** (`&gpio4 31`), not 639; `gpio_44` = 524 and
`gpio_49` = 529.  The session-44 sysfs replay in report 210 §4 used 639/556/561
— it drove GPIO5_31 / GPIO3_12 / GPIO3_17, i.e. **the wrong pins**, which is a
second, independent reason that replay was invalid.  Always resolve a line by
its DT phandle and the bank's `base`, never by "512 + bank*32 + bit".

## 4. Why the pulse's electrical effect is not observable

GPIO4_OE bit 31 reads 0 even after the regulator requests the line output, and
pad `0x120`'s INPUTENABLE bit (bit 5) does not stick (writing `0x0023` reads
back `0x0003`), so `GPIO4_DATAIN` cannot show the pin level.  The reference's
own `GPIO4_OE` also has bit 31 clear in its boot state, so "input + DATAOUT=1"
is normal here; only the driver's own request differs.  Whether the fixed
regulator's `gpiod_set_value` actually toggles the pad is therefore not
measurable from software on this board — but the code path is now the
reference's, and it is testable by its own observable (the codec probe and the
FLL lock).

## 5. Where this leaves the audio work

* The reference's board-enable sequence is **implemented and exercised**; it
  does not restore MCLK1.
* The **codec-probe reliability fix** is real and should stay: the ldo line must
  be high before `wm8994` probes.
* The MCLK1 fault remains below the readable registers; the next software-only
  option is the McBSP-master/SRG design, and the RE chain continues (report 210
  §5: `MixerInitAvp`, the AUXCLK1 writer, the `audio_boost` powerman modes).
