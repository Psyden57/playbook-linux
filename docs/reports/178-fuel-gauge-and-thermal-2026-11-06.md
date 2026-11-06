# 178 — The BQ27541 fuel gauge and the TMP103 thermal sensor; the CM3217 is still open

**Session 28 (absolute), Linux-era 22.  2026-11-06.**
Status: **read-only on RPMB.**  Step 2 of ROADMAP item 9 (report 176), after
the motion sensors (report 177).

---

## 0. Headline

* **BQ27541 fuel gauge works**: `/sys/class/power_supply/bq27541-0` reports
  capacity 100 %, 4.093–4.094 V, 28.8 °C, `Full`, 5.307 Ah full / 5.400 Ah
  design, cycle count 5, Li-ion, manufacturer "Texas Instruments".  The
  `bq27xxx_battery_i2c` driver binds `0-0055` from the DTS.
* **TMP103 works**: `hwmon0` name `tmp103`, `temp1_input` = **37000**
  (37.0 °C).  It needs the `hwmon` module loaded first (trap 94).
* **CM3217 remains open.**  Both 0x10 and 0x11 on I²C4 ACK but every register
  reads `0x00`; the mainline `cm32181` driver accepts only chip IDs
  `0x18`/`0x81`/`0x82`, and the QNX driver is the CM3217-specific
  `ls-cm3217.so` (address **0x10**, 400 kHz, `devctl 0xC0100506` reads,
  a CMD/IT/calibration config).  This is RE work, not a DTS change.

---

## 1. The change

**Device tree** (`&i2c1`, the PMIC/backlight bus, `0x48070000`):

```
temperature-sensor@77 { compatible = "ti,tmp103";  reg = <0x77>; };
battery@55            { compatible = "ti,bq27541"; reg = <0x55>; };
```

Both bind by compatible: the I²C core names the client from the part after the
comma (`tmp103`, `bq27541`), which matches each driver's `i2c_device_id`.

**Config:** `CONFIG_SENSORS_TMP103=m`; `CONFIG_BATTERY_BQ27XXX_I2C=m` (and the
core) were already set.

**Rootfs:** add the module closure — `tmp103.ko` needs `hwmon.ko` (because
`CONFIG_HWMON=m` in this config), `bq27xxx_battery_i2c.ko` needs
`bq27xxx_battery.ko` — and rcS modprobes `hwmon tmp103 bq27xxx_battery_i2c`.

---

## 2. Measured

```
PBR: 0-0055 <- bq27xxx-battery
PBR: 0-0077 <- tmp103
hwmon hwmon0: temp1_input not attached to any thermal zone   (benign)

$ cat /sys/class/power_supply/bq27541-0/uevent
POWER_SUPPLY_VOLTAGE_NOW=4093000
POWER_SUPPLY_CURRENT_NOW=-130000
POWER_SUPPLY_POWER_AVG=-5320000
POWER_SUPPLY_CHARGE_FULL=5307000
POWER_SUPPLY_CHARGE_FULL_DESIGN=5400000
POWER_SUPPLY_CAPACITY=100
POWER_SUPPLY_CYCLE_COUNT=5
POWER_SUPPLY_TEMP=288
POWER_SUPPLY_STATUS=Full
POWER_SUPPLY_TECHNOLOGY=Li-ion
POWER_SUPPLY_MANUFACTURER=Texas Instruments

$ cat /sys/class/hwmon/hwmon0/temp1_input
37000
```

The TMP103's "not attached to any thermal zone" is only a missing
`thermal-zones` link; the value is live.  Adding a thermal zone is optional.

---

## 3. The CM3217 (open)

RIM's `winchester-lc` loads `ls-cm3217.so` (Capella CM3217 ambient light).
Disassembly of `initCM3217` gives the parameters it uses:

* I²C address byte at struct `+0x2c` = **0x10**, bus speed `0x00061a80` =
  **400 kHz**, and it opens a `/dev/i2cN` path built with `snprintf` (QNX names
  the sensor bus `/dev/i2c3`, OMAP I²C4).
* `cm3217_read_reg` issues `devctl(fd, 0xC0100506, ...)`;
  `readLightRegisters` reads two registers and combines them
  (`(hi << 8) | lo`) before `cm3217_adc_to_lux`.
* `cm3217_parse_config_file` understands `CMD` labels, an integration-time
  delay and a `cal_table` of LUX/ADC pairs — i.e. the driver is calibrated by a
  config file, not just a chip ID.

On the live bus, `pbi2c rd 3 0x10 <reg>` and `0x11` return `0x00` for every
register 0x00–0x10.  The devices ACK, so something is there; either the part is
held in shutdown until a CMD write, or it is a different die (the mainline
`cm32181` probe would reject it anyway: IDs `0x18`/`0x81`/`0x82` only).

From Linux (`busybox i2cget`/`i2cset`), the picture is **not** CM32181-shaped:

```
i2cget -y 3 0x10 0x04        -> 0x4d once, then 0x00   (address is live)
i2cget -y 3 0x10 0x04 w      -> 0xff00                (word read: second byte 0xff)
i2cset -y 3 0x10 0x00 0x0800 w -> ENXIO ("Remote I/O error")
```

So the CM32181 command sequence (16-bit CMD `0x0800`) is refused, and the
byte reads do not settle.  The QNX `ls-cm3217` `ls_startup` writes the CMD
value from a **config byte at `+0x40`** (and `ls_shutdown` ORs bit 0 into it);
`readLightRegisters` reads a pair of registers and combines them.  The config
value and the register pair are the RE targets.

**Do not write blind** to 0x10/0x11 beyond the single refused CMD attempt:
the address is confirmed for the QNX CM3217, but the protocol is not the
CM32181's.  The next step is a static RE of `ls-cm3217.so` (the `+0x40` CMD
byte, the ALS register addresses, the `cal_table`) followed by a small driver.

---

## 4. Images and next

* Region 0: `0x1D9159E0` (`boot/chainloader/kernel.img`, the TMP103/BQ DTB).
* Region 1: `local/images/rootfs-region1.ext4`, sha256 `c689ca9e…` (14 modules).
* Next: the CM3217 RE, then step 3 (FAN5702 status LED), then the WL1283 combo.
