# 177 — The I²C4 sensor bus and the three motion sensors under Linux

**Session 28 (absolute), Linux-era 22.  2026-11-06.**
Status: **read-only on RPMB.**  The unit was on the sensor-bus image (region 0
`0x3E479F6F`, region 1 `6aa322bd…`) and was restored to PlayBook OS 2.1.0.1917
afterwards.  No RPMB/NVRAM write.

This is the first step of ROADMAP item 9 / report 176: enable the corrected
sensor bus and the BMA150, HMC5883L and MPU-3050.

---

## 0. Headline

* **OMAP I²C4 (`0x48350000`, Linux `i2c-3`) is up** and the three clients are
  instantiated from the DTS: `3-0038` (BMA150), `3-001e` (HMC5883L), `3-0068`
  (MPU-3050).
* **BMA150 accelerometer:** bound as `input0`, polling mode (no IRQ), and
  `/dev/input/event0` carries live `EV_ABS` X/Y/Z events once `evdev` is loaded.
* **HMC5883L magnetometer:** IIO `hmc5883l`, raw `in_magn_x/y/z_raw` read
  (`-103 / -268 / -518` at rest).
* **MPU-3050 gyroscope:** IIO `mpu3050`, raw `in_anglvel_x/y/z_raw` and
  `in_temp_raw`; the driver read **part no 3, version 14, die ID `0E9E`**.
* **The `=m` IIO core is part of the rootfs now**: `industrialio`,
  `industrialio-triggered-buffer`, `kfifo_buf`, `i2c-mux` and `evdev` — without
  them the sensor modules fail with "unknown symbol".

---

## 1. The change (one variable)

**Device tree** (`kernel/source/omap4-winchester.dts`, copied to the build
tree): `&i2c4` (`0x48350000`) enabled and the three nodes added; `&i2c3`
(`0x48060000`) stays enabled but empty (the unconfirmed touch bus).  The bus is
the one QNX names `/dev/i2c3` (report 176, trap 91); `&i2c4` was `disabled`.

```
&i2c4 { clock-frequency = <400000>; status = "okay";
    accelerometer@38 { compatible = "bosch,bma150";       reg = <0x38>; };
    magnetometer@1e  { compatible = "honeywell,hmc5883l"; reg = <0x1e>; };
    gyroscope@68     { compatible = "invensense,mpu3050"; reg = <0x68>; };
};
```

The IRQs (QNX int gpio 34/59/50 = GPIO2 lines 2/27/18) are **not** described
yet: the BMA150 driver polls when `client->irq <= 0`, and the HMC5843/MPU3050
expose direct IIO reads with no trigger.  The pads therefore stay as QNX left
them and no GPIO2 fighting is introduced in this step.

**Config:** `CONFIG_INPUT_BMA150=m`, `CONFIG_SENSORS_HMC5843_I2C=m` (selects the
`SENSORS_HMC5843` core); `CONFIG_MPU3050=m` was already set.

**Driver:** `drivers/input/misc/bma150.c` predates DT and had no
`of_device_id`, so a node whose name is not `bma150` would not bind.  A
two-entry table (`bosch,bma150`, `bosch,smb380`) was added and wired to
`.of_match_table`.

**Rootfs:** `build_rootfs.py` now installs the module closure and rcS modprobes
`evdev bma150 hmc5843_i2c mpu3050`.

---

## 2. What the boot log says

```
PBR: 48350000.i2c <- omap_i2c
omap_i2c 48350000.i2c: bus 3 rev0.10 at 400 kHz
PBR: 3-0038 <- bma150
input: bma150 as /devices/.../i2c-3/3-0038/input/input0
PBR: 3-001e <- hmc5843
hmc5843 3-001e: mounting matrix not found: using identity...
PBR: 3-0068 <- mpu3050-i2c
mpu3050-i2c 3-0068: found MPU-3050 part no: 3, version: 14
mpu3050-i2c 3-0068: die ID: 0E9E, wafer ID: 07, ...
```

`lsmod`: `bma150`, `hmc5843_i2c`, `hmc5843_core`, `mpu3050`, `i2c_mux`,
`industrialio`, `industrialio_triggered_buffer`, `kfifo_buf`, `evdev`.

`ls /sys/bus/i2c/devices`: `i2c-0`…`i2c-3`, `0-0038` (LM3532), `0-0048/49/4a`
(TWL6030), `3-001e`, `3-0038`, `3-0068`.

**Values read at rest** (all live):

| device | access | value |
|---|---|---|
| BMA150 | `/dev/input/event0`, `EV_ABS` | X `-4`, Y `0..1`, Z `241..244` (≈1 g) |
| HMC5883L | `iio:device0/in_magn_*_raw` | `-103 / -268 / -518` |
| MPU-3050 | `iio:device1/in_anglvel_*_raw` | `255 / -15 / 0` (scale `0.001068131` rad/s) |
| MPU-3050 | `iio:device1/in_temp_raw` | `-12576` |

The BMA150 event stream is the proof end to end: I²C4 → BMA150 → input core →
evdev → `/dev/input/event0`.

---

## 3. The module closure (why the first boot showed only bma150)

The first attempt had only `bma150.ko`, `hmc5843*.ko`, `mpu3050.ko`; `lsmod`
showed `bma150` and the modprobes failed with "unknown symbol".  Because
`CONFIG_IIO=m`, the sensors' dependencies are **modules too**:

```
hmc5843_core  -> industrialio, industrialio-triggered-buffer
mpu3050       -> industrialio, i2c-mux, industrialio-triggered-buffer
triggered-buf -> kfifo_buf, industrialio
bma150        -> (none)
```

`evdev` is separate: the BMA150's input device exists without it, but
`/dev/input/event0` does not.  Install the closure and `modprobe evdev`.

---

## 4. Flashing: `bbusb.py`, not `pre_os_ifs_flash.py`

The operator's ruling: **`tools/bbusb.py` is the flasher**; the template tool is
older.  Both writes can be one bootrom session:

```bash
# both region 0 and region 1 in one session
printf 'boot/chainloader/kernel.img=16,0\nlocal/images/rootfs-region1.ext4=8448,0\n' \
  > /tmp/both.lst
python3 tools/bbusb.py pack -l /tmp/both.lst -v 1,2 -s -f -c /tmp/both.signed
python3 tools/bbusb.py flash --signed /tmp/both.signed --from-bootrom \
        --password 0000 --apply
# or one component
python3 tools/bbusb.py flash --component local/images/rootfs-region1.ext4 \
        --dest-block 0x2915 --from-bootrom --password 0000 --apply
```

`COMPLETE 0x40C0 -> cmd 0x4006` is the success status with this tool (the old
template flasher saw `0x4015`).  The bootrom/ramloader open window is now
**60 s** (was 10 s); a graceful busybox shutdown can take ~90 s on this unit, so
for flashing use `reboot -f` from the shell to reset immediately (trap 93).

---

## 5. Images and next

* Region 0: `0x3E479F6F` (`boot/chainloader/kernel.img`).
* Region 1: `local/images/rootfs-region1.ext4`, sha256 `6aa322bd…`.
* Next (report 176 order): CM3217 `0x10` (CM32181 compatibility), TMP103 `0x77`,
  BQ27541 `0x55`; then FAN5702 `0x36`; then the WL1283 combo; then the WM8994
  audio machine and the CY8CTMA3 touch.  The sensor IRQs (GPIO2 2/27/18) are
  the next variable for the motion sensors if triggered sampling is wanted.
