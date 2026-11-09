# 198 — Touch calibration and the GPIO2_3 interrupt

Date: 2026-11-09 (session 38, Linux-era 32).  Item 9 step 5 polish: the
coordinate mapping and the IRQ pad that report 194 left open.  Read-only
device access until the region-0 test flash; the touch itself was written
(app boot / mode registers) exactly as in report 194.

## 0. Headline

* **The calibration is the 1917 vendor `scaling.conf` four-point line**, not a
  guess: raw `x 172→1108` maps to display `102→922`, raw `y 110→658` maps to
  display `60→540`.  Axis-aligned, no swap, no inversion; it extrapolates to
  the full 1024×600 panel and clamps at the edges.
* **The touch IRQ is gpio 35 = GPIO2_3, CORE pad `0x056` (`gpmc_ad11`).**  The
  driver requests it as `IRQ_TYPE_EDGE_FALLING`; the pad is mux 3, input, pull,
  edge-falling (`0x410B`).  It works: `Munin touchscreen ready (irq 110)` and
  `/proc/interrupts` shows `110: … 48055000.gpio 3 Edge 1-0065`.
* **The ~330–380/s interrupt rate with no finger is the controller's active
  report rate, not noise.**  Unbinding the driver freezes the count; rebinding
  resumes it.  The vendor library also attaches an ISR (`tp_intr_attach`).

Image: region-0 **`0x82FC0D35`** (`0x91A9C4` bytes, kernel `7.3.3 #26`).

## 1. The calibration, decoded

The 1917 `/base` carries
`etc/system/config/scaling.conf`.  Entries are tried in order; the first valid
`1024x600` entry is the calibration:

```
1024x600:mode=calib,disp_x=[102:922:922:102],disp_y=[60:60:540:540],
         mtouch_x=[172:1108:1108:172],mtouch_y=[110:110:658:658]
```

The four point pairs are therefore:

| point | raw (x,y) | display (x,y) |
|---|---|---|
| 1 | 172, 110 | 102, 60 |
| 2 | 1108, 110 | 922, 60 |
| 3 | 1108, 658 | 922, 540 |
| 4 | 172, 658 | 102, 540 |

Both axes are independent and increasing, so the map is:

```
x_disp = 102 + (x_raw - 172) * 820 / 936
y_disp = 60  + (y_raw - 110) * 480 / 548
```

clamped to `0..1023` / `0..599`.  The raw endpoints are calibration targets
inside the display, not panel edges: display 0/1023 corresponds to raw
≈ 56/1224 (x) and ≈ 42/726 (y), which is why the map must use the `disp_*`
values rather than stretching the raw range across the panel.

The driver takes the four endpoints per axis from
`rim,x-calibration = <172 1108 102 922>` and
`rim,y-calibration = <110 658 60 540>` (the values are also the driver
defaults), reads `touchscreen-size-x/y = <1024 600>`, and sets the ABS maxima
to `1023`/`599`.  `munin_map()` uses `div_s64()` (a plain 64-bit divide pulled
in `__aeabi_ldivmod` and failed to link).

## 2. The IRQ line

`graphics.conf` selects `driver = munin` with `tp_intr=1035`.  QNX numbers GPIO
interrupts as `1000 + gpio`, so this is gpio 35 = **GPIO2_3**.  The 1917
`hwio-config-winchester.so` `gpio_config` record for gpio 35 is
`dir=0 (input), edge=2 (falling), pull=1, signal=0x16` — the same class as
`WL_IRQ` (gpio 53).  The board `pad_config` record for signal `0x16` is pad id
`0x23`; the OMAP4 CORE pad for gpio 35 is `gpmc_ad11` at offset **`0x056`**
(cross-checked against mainline: `0x054` = `gpmc_ad10` = gpio_34 and `0x058` =
`gpmc_ad12` = gpio_36 in `omap4-sdp.dts` / `omap4-samsung-espresso-common.dtsi`).

The DTS therefore adds:

```dts
touch_irq_pins: touch-irq-pins {
	pinctrl-single,pins = <
		OMAP4_IOPAD(0x056, 0x410b)	/* gpmc_ad11 = gpio_35: input/pull/falling */
	>;
};

touchscreen@65 {
	interrupt-parent = <&gpio2>;
	interrupts = <3 IRQ_TYPE_EDGE_FALLING>;
	pinctrl-names = "default";
	pinctrl-0 = <&touch_irq_pins>;
	...
};
```

`0x410B` = `WAKEUP_EN | INPUT_EN | PULL_ENA | MUX3`, the reference's WL_IRQ
shape.  `0x4805514C` (FALLINGDETECT) reads `0x8`, `0x48055148` (RISINGDETECT)
and both LEVELDETECT registers read `0`: edge-only, exactly as requested.

### 2.1 Measured

```
[    2.585144] PBR: 1-0065 <- playbook-munin
[    2.585937] input: PlayBook Munin Touch as /devices/.../1-0065/input/input0
[    3.227661] playbook-munin 1-0065: Munin bootloader id 0xb0 ver 0x05 app 0x020a build 0x3b
[    3.453460] playbook-munin 1-0065: Munin touchscreen ready (irq 110)
```

```
devmem 0x4A100056 16        -> 0x410B
/proc/interrupts            -> 110:  ...  48055000.gpio   3  Edge  1-0065
```

Unbind/rebind, with no finger on the panel:

```
bound   65169 interrupts (170.7 s uptime)
UNBIND  65169 (1.7 s later)  -> frozen
UNBIND  65169 (3.0 s later)  -> still frozen
BIND    65636 (2 s later)    -> resumed
```

So the line is quiet unless the touch driver is talking to the chip.  The rate
is set by the controller: the driver reads the 18-byte report on every edge,
which releases the line, and the next report pulls it down again.  This is the
vendor's own ISR model (`tp_intr_attach` → `InterruptAttach`), not an electrical
fault.  The driver keeps the 20 ms poll as a fallback if the IRQ cannot be
requested (no `interrupts`, or `request_threaded_irq()` fails).

## 3. Coordinate check on the bench

Filtered to `ABS_MT_POSITION_X`/`Y` (type 3, codes 53/54), during a left-edge
and then a top-edge drag:

* left edge: `X = 0..7` while `Y` swept `43 → 29` — vertical motion changes Y,
  not X;
* top edge: `X` swept `86 → 957` monotonically while `Y = 0..27` — X increases
  left-to-right and Y = 0 is the top;
* every value is inside `0..1023 × 0..599`.

That confirms the map's orientation and range.  The absolute offset is the
vendor's own calibration (the inset targets are 102/60 and 922/540), so a
finger placed on a calibration target should read that display point.

## 4. What this does not settle

* **The precise corner calibration on this panel.**  The vendor calibration is
  authoritative, but the bench check is a drag, not four held targets; a
  repeatable four-corner test with a printed overlay would quantify the
  residual offset.
* **The report rate.**  ~330 Hz with no contact is normal for the controller's
  active mode, but the driver's `input_sync()` runs on every frame; a real
  userspace may prefer a coalesced report.  Not changed here.
* **The power/reset lines are still not driven by the driver.**  GPIO1_14
  (power) and GPIO4_8 (reset) read output-high after boot and the part answers
  without a pulse (report 193 §4); the driver still relies on that.

## 5. Sources

* `local/work/1917/base/etc/system/config/scaling.conf` (the calib line).
* `local/work/1917/base/usr/lib/graphics/omap4430/graphics.conf` (`tp_intr`).
* `local/work/1917/base/lib/dll/hwio-config-winchester.so`: `gpio_config`
  record 6 (gpio 35: dir 0, edge 2, pull 1, signal 0x16), `pad_config` record
  for signal 0x16 (pad id 0x23).
* Mainline OMAP4 pad names: `omap4-sdp.dts` (`0x054` = `gpmc_ad10` = gpio_34),
  `omap4-samsung-espresso-common.dtsi` (`0x05C` = `gpmc_ad14` = gpio_38).
* Live Linux: `devmem`, `/proc/interrupts`, `dmesg`, the `event0` capture.
* Reports 176, 193, 194; `DECISIONS.md` §45/§47.
