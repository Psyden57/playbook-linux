# 176 — The PlayBook's sensors and peripherals: what is on the board, and what mainline already drives

**Session 28 (absolute), Linux-era 22.  2026-11-06.**
Status: **read-only on the unit.**  The TabletOS oracle was queried over the
door (`pidin`, `ls /dev`, `pbi2c scan`/`rd`, `/base` config and driver files);
its `Winchester` sensor configuration and the RIM driver modules were read.
No RPMB, NVRAM or media write.  The unit was left on PlayBook OS 2.1.0.1917 with
`BOOTCTR_FCTRY = 0x32` (the session also closed ROADMAP item 5; see the session
note).

The aim is the one the operator set after item 5: **get the device's own
sensors and peripherals working under Linux first** — accelerometer,
magnetometer, gyroscope, touchscreen, the FAN5702 status LED, audio, WiFi,
GPS/AGPS and the battery/thermal parts — before any `hd3` repurpose.

---

## 0. Headline

* **The whole sensor cluster is on one I²C bus, OMAP4 I²C4 at `0x48350000`**
  (QNX names it `/dev/i2c3`), and our board DTS currently **disables** that
  controller while enabling the wrong one (`&i2c3` = `0x48060000`, which QNX
  never instantiates).  This is the single most important correction.
* **The cluster is identified and matches the teardown**: BMA150 accelerometer
  (`0x38`), HMC5883L magnetometer (`0x1E`), MPU-3050 gyroscope (`0x68`),
  CM3217 ambient light (`0x10`/`0x11`), FAN5702 LED driver (`0x36`).  All five
  answer on the live bus and their chip IDs were read.
* **Most of these already have mainline drivers** — BMA150, HMC5843, MPU3050,
  CM32181, TMP103, BQ27xxx — but they are **not enabled** in
  `winchester.config` and **no device nodes exist** in the DTS.  Enabling the
  bus and the drivers is host-side work, not RE.
* **Three parts need new code**: the **FAN5702** and **NCP5623** LED drivers
  (no mainline driver), the **Cypress CY8CTMA3** TrueTouch Gen3 (mainline has
  Gen2/Gen5 only; QNX uses a RIM "Munin/Pegasus" protocol and firmware), and
  the **WM8994** audio machine glue (the codec is mainline, the OMAP4+WM8994
  machine driver is not).
* **WiFi/BT/GPS are one TI WL1283 combo** on SDIO + UART: `devnp-ti1283`,
  `devc-seromap-winbt`, `devlp-ti128x`, `btp-gps`.  Mainline `wl12xx` covers the
  WiLink, but `mmc1` (SDIO) is disabled and the firmware/NVS are QNX blobs.
  **GPS/AGPS is real** (the WL128x has a GPS core; QNX has RRLP aiding), and is
  the same combo — so "AGPS" is not a separate chip.

---

## 1. The bus topology, measured

QNX runs three OMAP I²C controllers (`pidin ar`):

```
i2c-omap35xx-omap4 -p 0x48070000 -i 88 -l 420 -f   -> /dev/i2c0
i2c-omap35xx-omap4 -p 0x48072000 -i 89 -l 420 -f   -> /dev/i2c1
i2c-omap35xx-omap4 -p 0x48350000 -i 94 -l 420 -f   -> /dev/i2c3
```

`/dev/i2c2` (OMAP I²C3, `0x48060000`) is **not instantiated**.  A full scan with
`pbi2c scan <bus>` (read-only probing) gave:

| QNX bus | OMAP controller | devices that ACK (7-bit) |
|---|---|---|
| `/dev/i2c0` | I²C1 `0x48070000` | `0x09`, `0x38` (LM3532), `0x48`/`0x49`/`0x4a` (TWL6030), `0x55`, `0x77` |
| `/dev/i2c1` | I²C2 `0x48072000` | `0x65` |
| `/dev/i2c3` | **I²C4 `0x48350000`** | `0x10`, `0x11`, `0x1a` (WM8994), `0x1e` (HMC5883L), `0x36` (FAN5702), `0x38` (BMA150), `0x68` (MPU3050) |

### 1.1 The sensors, from RIM's own configuration

`/base/etc/system/config/sensor_config.xml`, board **`Winchester` rev `0`**
(this unit), lists exactly the three motion sensors and their interrupts:

| type | part | I²C | address | `gpio_num_1` |
|---|---|---|---|---|
| `accel` | `bma150` | `/dev/i2c3` | `0x38` | 34 |
| `gyro` | `mpu3050` | `/dev/i2c3` | `0x68` | 50 |
| `mag` | `hmc5883l` | `/dev/i2c3` | `0x1E` | 59 |

The other boards in the file (`Colt`, `Winchester2`) use the alternate parts
(MMA8451Q at `0x1D`, AMI306R/MAG3110 at `0x0E`) with different orientations —
irrelevant for this unit but useful if a second board ever appears.

**Confirmed by reading the chips** (`pbi2c rd`):

| device | address | read | meaning |
|---|---|---|---|
| BMA150 | `0x38` | reg `0x00` = `0x02` | BMA150 chip ID |
| HMC5883L | `0x1E` | `0x0A/0x0B/0x0C` = `48 34 33` | ASCII `H43` = HMC5883L |
| MPU3050 | `0x68` | `0x00` = `0xDE` | MPU-3050 product ID |
| FAN5702 | `0x36` | reg `0x00` = `0xFF` | the LED driver's register bank |
| CM3217 | `0x10`, `0x11` | reg `0x00` = `0x00` | ambient light (CM3217/CM3218 pair) |
| WM8994 | `0x1a` | — | audio codec (16-bit register space; 8-bit reads return 0) |
| fuel gauge | `0x55` | `0x00`=`0x0b`, `0x01`=`0x60` | BQ27541-class gauge (`battery_win`) |
| TMP103 | `0x77` | `0x00`=`0x29` | TI TMP103 thermal sensor (`ti_tmp103`) |

`0x09` on I²C1 and `0x65` on I²C2 are not yet identified; nothing in the RIM
driver list obviously claims them (candidates: a board EEPROM/GPIO expander and
the NCP5623, whose address the QNX module does not print).  They are recorded,
not guessed.

### 1.2 The driver set RIM loads

`pidin ar` and `/base/lib/dll`:

```
sensor-services -d sensor-accel-bma150.so -d sensor-mag-hmc5883l.so -d sensor-gyro-mpu3050.so
winchester-lc  -d led-fan5702.so -d ls-cm3217.so
io-audio       -d omap4abe-wm8994 aif=0,secure_capture
wpa_pps        -d libpps_wlandriver_ti.so.1 -i ti0
devc-seromap-winbt ... 0x4806c000          (Bluetooth, WL1283 over UART)
io-locp        -p .../location/gps.xml      (GPS provider)
io-locp        -p .../location/wifi.xml     (WiFi positioning)
```

plus `led-ncp5623.so`, `ti_tmp103.so`, `Battery_temp.so`, `deva-ctrl-omap4abe-wm8994.so`,
`devnp-ti1283.so`, `devlp-ti128x.so`, `btp-gps.so` in `/base/lib/dll`.

The RIM modules are on the bench host now at `local/work/qnx-libs/` (gitignored)
for static RE.

---

## 2. Mainline support matrix

Kernel symbols are from `kernel/winchester.config`; driver files from
`local/kernel-tree`.

| function | part | mainline driver | symbol now | work |
|---|---|---|---|---|
| accelerometer | BMA150 | `drivers/input/misc/bma150.c` | `# CONFIG_INPUT_BMA150 is not set` | enable + DTS node |
| magnetometer | HMC5883L | `drivers/iio/magnetometer/hmc5843_i2c.c` | not set | enable + DTS node |
| gyroscope | MPU-3050 | `drivers/iio/imu/inv_mpu6050/` (MPU3050) | `CONFIG_MPU3050=m` | DTS node (driver builds) |
| ambient light | CM3217 | `drivers/iio/light/cm32181.c` | `# CONFIG_CM32181 is not set` | verify register compatibility; enable + DTS |
| thermal | TMP103 | `drivers/hwmon/tmp103.c` | `# CONFIG_SENSORS_TMP103 is not set` | enable + DTS node |
| battery gauge | BQ27541 | `drivers/power/supply/bq27xxx_battery_i2c.c` | `CONFIG_BATTERY_BQ27XXX_I2C=m` | DTS node |
| status LED | FAN5702 | **none** | — | new driver (small I²C LED) or RE `led-fan5702.so` |
| RGB/notify LED | NCP5623 | **none** | — | identify address; new driver or drop |
| touchscreen | Cypress CY8CTMA3 | `cyttsp_core`/`cyttsp5` only (Gen2/Gen5) | `# CONFIG_TOUCHSCREEN_CYTTSP_CORE is not set` | **RE/new Gen3 driver**; QNX firmware + protocol in hand |
| audio codec | WM8994 | `sound/soc/codecs/wm8994.c`, `drivers/mfd/wm8994-core.c` | `# CONFIG_MFD_WM8994 is not set` | enable MFD+codec, add an OMAP4 machine driver (simple-card or ABE) |
| WiFi/BT | TI WL1283 | `wl12xx` (WL128x), `hci_ll` | `CONFIG_WL12XX=m` | **enable mmc1 SDIO**, power/32k GPIOs, firmware+NVS |
| GPS/AGPS | WL1283 GPS | TI ST / WL128x GPS path | TBD | enable with the combo; AGPS needs userspace |
| camera | — | — | — | later |

Two structural facts shape the plan:

1. **The module path now works.**  Session 28's region-1 userland has
   `insmod`/`modprobe` and `/lib/modules/6.15.11`, so the many `=m` sensor
   drivers can be tried by loading a `.ko` instead of rebuilding the kernel.
2. **The bus is one DTS change.**  The sensor cluster needs `&i2c4`
   (`0x48350000`) enabled and the five nodes added; the interrupt GPIOs are
   QNX global numbers 34/50/59 (GPIO2 lines 2/18/27 under the Linux numbering,
   to be checked against the pad mux and the existing GPIO2 reset note).

---

## 3. The three parts that are real work

**FAN5702 (status LED).**  No mainline driver.  It is a small I²C LED driver;
the QNX module is 18 KB and its symbols (`led_write_reg`, `led_dimming_*`,
`fan5702_probe`) are a clean reference.  A minimal `leds-fan5702` for the RGB
notification LED is a contained driver task.

**Cypress CY8CTMA3 (touch).**  The teardown identifies `CY8CTMA3` TrueTouch;
mainline has the Gen2 `cyttsp_core` and Gen5 `cyttsp5`, not this part.  RIM's
stack is `/base/lib/firmware/munin/{Pegasus_50,Pegasus2_50,Pegasus_D4_A}.hex`
loaded by `muninfw` through `libmtouch-pegasus`/`libmtouch-munin`, and the
daemon `/base/usr/bin/touch`.  The hard-coded I²C path in `libmtouch-pegasus.so`
is `/dev/i2c2` — QNX's **uninstantiated** OMAP I²C3 (`0x48060000`), which our
DTS *already* enables, so the touch bus is probably I²C3 at the Cypress address
(still to confirm on the bus; the controller may not ACK until firmware is
loaded).  This is the largest single driver/RE task.

**WM8994 audio.**  The codec driver and MFD are mainline but disabled; the QNX
side is `deva-ctrl-omap4abe-wm8994.so` (StereoAudioCodec* API), i.e. the OMAP4
ABE + WM8994.  Mainline has no OMAP4+WM8994 machine driver, so this is a
`simple-audio-card`/new-machine + ABE firmware question.  Speakers, the 3.5 mm
jack (headset detect) and the microphones are all behind this one codec.

---

## 4. Proposed order

1. **The sensor bus + the three motion sensors** (one variable): enable
   `&i2c4`, add BMA150/HMC5883L/MPU3050 nodes, enable `INPUT_BMA150` and
   HMC5843, keep MPU3050 `=m`, load and read via `iio`/`evdev`.  This proves
   the corrected bus and the interrupt wiring.
2. **Add CM3217, TMP103 and the BQ27541** to the same bus (and I²C1 for the
   gauge/temp), enable their drivers.  These are low-risk, and the gauge is
   needed for any battery-aware userland.
3. **FAN5702** driver (small) — the status LED is a visible end-to-end I²C test.
4. **WiFi** (mmc1 SDIO + `wl12xx` + firmware/NVS), then **BT**, then **GPS**
   (the WL1283 combo; AGPS after the GPS core answers).
5. **Audio** (WM8994/ABE), then **touch** (the Gen3 driver/RE).

Every step is one variable per boot, and none of them needs a write to RPMB;
the `hd3` repurpose stays parked.

---

## 5. Sources

* Live QNX over the door: `pidin ar`, `ls /dev`, `ls /base/lib/dll`,
  `cat /base/etc/system/config/sensor_config.xml`, `find /base/lib/firmware`,
  `/tmp/pbi2c scan|rd`; the RIM modules copied to `local/work/qnx-libs/`.
* `kernel/winchester.config`, `local/kernel-tree/drivers/...`,
  `kernel/source/omap4-winchester.dts`.
* Teardown identification (BMA150, MPU-3050, CY8CTMA3): iFixit / EE Times
  PlayBook teardown (public).
* Reports 119/120 (PMIC/I²C), 138 (hd3), 175 (RPMB); session-28 note.
