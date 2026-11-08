# 193 — The touch is on I²C2 (0x48072000), and Linux never muxed its pads

**Session 36 (absolute), Linux-era 30.  2026-11-08.**
Status: **bench + static RE; one region-0 image flash.**  The unit started on
the known-good Linux 7.3.3 image `0xE36C679A`; the test image `0x307615B3`
(DTB-only delta) was flashed to region 0 and booted once.  No RPMB, NVRAM or
MMC write.  This is the first work on ROADMAP item 9 step 5 (WM8994 audio +
CY8CTMA3 touch, report 176).

---

## 0. Headline

* **The Cypress touch is on OMAP I²C2 `0x48072000`** — QNX `/dev/i2c1`, Linux
  **`i2c-1`** — at slave **`0x65`**.  The `libmtouch-pegasus` `/dev/i2c2`
  string was a red herring: the board config selects **`driver = munin`**, and
  `libmtouch-munin` opens `/dev/i2c1`.  The open contradiction
  `touch-bus-i2c2-vs-i2c3` is **resolved** (report 176's live `0x65` ACK on
  `/dev/i2c1` was the touch).
* **The bus did not work under Linux because its pads were never muxed.**
  Read live, `0x4A100126` (i2c2_scl) and `0x4A100128` (i2c2_sda) were
  **`0x011F` = safe mode (mux 7)**, while the working I²C1 pads are `0x0110`
  (mux 0, input, pull-up).  Every i2c-1 transaction timed out (`-110`), which
  is why `i2cdetect` hung and `0x65` never answered.
* **Writing the two pads to `0x0110` fixed it immediately:** `0x65` ACKed a
  read-byte probe and a register read returned data.  No reset pulse was
  needed — both control lines already read output-high.
* **The source is QNX's `startup-playbook` again:** it muxes the board pads;
  our chainloader never runs it.  This is the same class of difference as the
  WL1283 QNX-only GPIO outputs (report 192, trap 135), now on the touch bus.
* The fix is baked into `omap4-winchester.dts` as an `i2c2_pins` group
  (`OMAP4_IOPAD(0x126/0x128, 0x0110)`) and `pinctrl-0` on `&i2c2`; the DTB
  compiles.  The test image **`0x307615B3`** (the existing zImage re-wrapped
  with the new DTB, `0x91A300` bytes) was flashed to **region 0 only** and
  booted; on that plain boot, with no `devmem`, the pads read `0x0110` and
  `0x65` ACKs (§5a).

---

## 1. Which driver, and therefore which bus

`/base/usr/lib/graphics/omap4430/graphics.conf` (1917 RCFS) selects the
transport explicitly:

```
begin mtouch
    driver = munin
    filter = ballistic,kalman
    options = i2c_lib=i2c-resmgr-client,tp_intr=1035,i2c_slave=0x65
end mtouch
```

So the driver is **munin**, not pegasus.  Its library,
`/base/lib/libmtouch-munin.so`, contains the string **`/dev/i2c1`**
(`libmtouch-pegasus.so` is the one with `/dev/i2c2`), plus
`winchester_tp_init`/`winchester_tp_reset`, `munin_existence_check`, and both
`i2c-omap4` and `i2c-resmgr-client` back ends (`i2c_lib`, `i2c_base`,
`i2c_slave` are option names).  The board selects the **resmgr client**, so
the bus is the QNX device node `/dev/i2c1`.

Report 176 recorded QNX's own `pidin ar` arguments:

| QNX node | OMAP controller |
|---|---|
| `/dev/i2c0` | `0x48070000` (I²C1) |
| `/dev/i2c1` | **`0x48072000` (I²C2)** |
| `/dev/i2c3` | `0x48350000` (I²C4) |

and its `pbi2c scan` found **`0x65` on `/dev/i2c1`**.  The Munin driver and the
ACK therefore name the same controller: **I²C2 `0x48072000`**.

On the live Linux image the adapters map as (from each
`/sys/bus/i2c/devices/i2c-N/of_node`):

| Linux | controller | note |
|---|---|---|
| `i2c-0` | `0x48070000` | TWL / ISL9519 / LM3532 / TMP103 / BQ27541 |
| **`i2c-1`** | **`0x48072000`** | **touch — the target** |
| `i2c-2` | `0x48060000` | I²C3, empty (QNX never instantiates it) |
| `i2c-3` | `0x48350000` | sensor bus |

The old bench plan ("`i2cdetect -y -r 2` and `-r 3`") was aimed at the wrong
adapters; the correct one is **`i2c-1`**.

## 2. The Munin wiring, from the library

`winchester_tp_init` maps two GPIO banks:

```
mmap_device_io(0x1000, 0x4A310000)  -> GPIO1   (stored at [dev+0xf8])
mmap_device_io(0x1000, 0x48059000)  -> GPIO4   (stored at [dev+0xf8]+4)
```

`winchester_tp_reset` then runs (offsets are OMAP GPIO: CLEARDATAOUT `+0x190`,
SETDATAOUT `+0x194`):

1. GPIO4 `+0x190 = 0x00000100` → **GPIO4_8 low** (reset asserted); 20 ms;
2. GPIO1 `+0x190 = 0x00004000` → **GPIO1_14 low** (power off); 500 ms;
3. GPIO1 `+0x194 = 0x00004000` → GPIO1_14 high; 500 ms;
4. GPIO4 `+0x194 = 0x00000100` → GPIO4_8 high (reset released); 15 ms.

So **reset = GPIO4_8 (active low)**, **power/enable = GPIO1_14**.  The
interrupt is `tp_intr=1035`; QNX numbers GPIO interrupts as `1000 + gpio`
(report 180 used `0x41d = 1053 = 1000 + 53`), so 1035 = gpio 35 = **GPIO2_3**
(an inference from the convention, not a separate measurement).
`mtouch_driver_init` also defaults the address to `0x65` and the panel to
1280×768.

## 3. Why the Linux bus was dead — the pad mux

Live on `0xE36C679A` (`devmem`, 16-bit reads; the OMAP4 padconf is a 16-bit
`pinctrl-single` register at `0x4A100000 + offset`):

| pad | offset | live value | meaning |
|---|---|---|---|
| i2c1_scl | `0x122` | `0x0110` | mux 0, input, pull-up — working PMIC bus |
| i2c1_sda | `0x124` | `0x0110` | same |
| **i2c2_scl** | **`0x126`** | **`0x011F`** | **mux 7 = safe mode** |
| **i2c2_sda** | **`0x128`** | **`0x011F`** | **mux 7 = safe mode** |
| i2c3_scl | `0x12a` | `0x011F` | safe mode |
| i2c3_sda | `0x12c` | `0x011F` | safe mode |
| i2c4_scl | `0x12e` | `0x0118` | mux 0 (sensor bus works) |
| i2c4_sda | `0x130` | `0x0118` | mux 0 |

(`0x0110` = MUXMODE 0 | PULL_UP | INPUT_EN `/ 0x0118` = the kernel's
`PIN_INPUT_PULLUP` encoding; the working I²C1 pads are `0x0110`, which is what
this change copies.)

The board DTS had `&i2c2`/`&i2c3` `status = "okay"` with **no `pinctrl-0`**, so
the controllers probed but their lines were not connected to the pads.  The
symptom was distinctive:

* an absent address on a working bus (i2c-3 `0x40`) → `--` and
  `Remote I/O error`;
* `0x65` on i2c-1 → `--` and **`Connection timed out`**; a whole-bus
  `i2cdetect -y -r 1` blocked the console for over a minute.

That is a bus-timeout signature, not a NAK, and it sent the investigation to
the pads.

## 4. The one-variable runtime test

```
devmem 0x4A100126 16 0x0110     # i2c2_scl: mux 0, input, pull-up
devmem 0x4A100128 16 0x0110     # i2c2_sda
```

Immediately afterwards, on `i2c-1`:

```
i2cdetect -y -r 1 0x65 0x65   ->  60: 65        (ACK)
i2ctransfer -y 1 w1@0x65 0x00 r1 -> 0x00
```

A read-only register sweep (`w1@0x65 reg r1`, 1-byte register addressing)
returned

```
reg 0x00..0x07 = 00 09 b0 05 09 00 b7 2f
```

so the part is alive and answers; the exact identity/protocol decode is the
next task.  (A 2-byte register write returns the same bytes, so the part
appears to use 1-byte register addressing at this level.)

The GPIO control lines were **already released** on this boot:
`GPIO1_OE = 0xFFFF9FBB` (bit14 output), `GPIO1_DATAOUT = 0x00006044`
(bit14 high); `GPIO4_OE = 0x6FFFFEDF` (bit8 output),
`GPIO4_DATAOUT = 0x90000120` (bit8 high).  The decoded reset/power pulse was
run as a control and changed nothing — expected, because the bus was still
unmuxed at that point.  The pad mux alone made the part answer, so **the host
was the blocker, not the chip**.

## 5. The durable fix

`kernel/source/omap4-winchester.dts` (copied to `local/kernel-tree`):

```dts
&omap4_pmx_core {
	i2c2_pins: i2c2-pins {
		pinctrl-single,pins = <
			OMAP4_IOPAD(0x126, 0x0110)	/* i2c2_scl */
			OMAP4_IOPAD(0x128, 0x0110)	/* i2c2_sda */
		>;
	};
};

&i2c2 {
	pinctrl-names = "default";
	pinctrl-0 = <&i2c2_pins>;
	clock-frequency = <400000>;
	status = "okay";
};
```

The DTB builds (`make … ti/omap/omap4-winchester.dtb`).  The image was built by
re-wrapping the existing zImage with the new DTB and flashed region-0 only:
**`0x307615B3`**, `0x91A300` bytes.  `&i2c3` is left as it was (enabled,
unmuxed, empty) so the change stays one variable.

### 5a. Plain-boot proof

`tools/bbusb.py flash --component boot/chainloader/kernel.img --dest-block
0x825 --from-bootrom --password 0000 --apply` reported
`COMPLETE 0x40C0 -> cmd 0x4006` (~588 frames); the unit enumerated as
`0fca:8010` after 20 s.  On that boot, with **no `devmem`**:

```
uname -a                      -> Linux playbook 7.3.3 #19 …
devmem 0x4A100126 16          -> 0x0110      (DTS mux, not 0x011F)
devmem 0x4A100128 16          -> 0x0110
i2cdetect -y -r 1 0x65 0x65   -> 60: 65      (ACK)
i2ctransfer -y 1 w1@0x65 0x00 r1 -> 0x00
i2ctransfer -y 1 w1@0x65 0x01 r1 -> 0x09
```

`pbrpmb: topup: … = 2e000000`, `0x2e >= threshold 0x1e, no write`; the pack
read Full 4.155 V.  This is the durable result: the touch bus works on a stock
boot with no runtime poking.

## 6. What this does not decide

* **The touch driver.**  Mainline has Gen2 (`cyttsp_core`) and Gen5
  (`cyttsp5`), not the CY8CTMA3 Gen3; RIM's stack is the Munin/Pegasus
  protocol plus `muninfw` and the `.hex` firmware.  Whether to extend
  `cyttsp_core` or write a small Gen3 driver is the next design question, and
  it needs the register/protocol decode (the sweep above is the start).
* **The exact ball for GPIO4_8** was not mapped (the reset pulse may have been
  a no-op even if needed; the part answered without it).
* **`tp_intr = 1035 → GPIO2_3`** is inferred from the `1000 + gpio` convention,
  not read from a second source.

## 7. Sources

* 1917 `/base`: `usr/lib/graphics/omap4430/graphics.conf` (the `mtouch`
  blocks); `lib/libmtouch-munin.so` (dynamic symbols and `/dev/i2c1`);
  `lib/libmtouch-pegasus.so` (`/dev/i2c2`, not selected);
  `usr/bin/muninfw` (`i2c_slave_addr`, `Munin FW version`).
* `ntoarmv7-objdump -d libmtouch-munin.so`: `winchester_tp_init` at
  `0x9120`, `winchester_tp_reset` at `0x90a0`, `mtouch_driver_init` at
  `0x57d8`, `munin_existence_check` at `0x56f4`.
* Report 176 §1 (QNX `pidin ar` and the live `0x65` on `/dev/i2c1`).
* TRM `hardware/swpu231ap.pdf.txt` §18: `CONTROL_CORE_PAD0_I2C1_SDA_PAD1_I2C2_SCL`
  (0x124/0x126) and `…I2C2_SDA_PAD1_I2C3_SCL` (0x128/0x12a); pad offsets
  `i2c2_scl = 0x126`, `i2c2_sda = 0x128`.
* Live Linux on `0xE36C679A`: `/sys/bus/i2c/devices/i2c-*/of_node`, `devmem`
  pad/GPIO reads, `i2cdetect`/`i2ctransfer` probes.
* `kernel/source/omap4-winchester.dts` and
  `local/kernel-tree/…/omap4-winchester.dts` (the `i2c2_pins` change).
