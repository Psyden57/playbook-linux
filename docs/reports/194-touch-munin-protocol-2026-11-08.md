# 194 — The Munin touch protocol: bootloader, app start, and the report format

**Session 36 (absolute), Linux-era 30.  2026-11-08.**
Status: **static RE + live, read-only/write-to-the-touch-only.**  The unit is on
the session-36 test image `0x307615B3` (touch pads muxed).  The touch at I²C2
`0x65` was put into app mode with the vendor's own two-byte command
(`reg0 = 0x80`) and read; no flash, RPMB, NVRAM or MMC write.  This continues
report 193 (the bus) and decides the driver approach for item 9 step 5.

---

## 0. Headline

* **The CY8CTMA3 on this board is a Cypress TrueTouch Gen3 with RIM's "Munin"
  bootloader, not a mainline part.**  The chip answers with **1-byte register
  addressing** at `0x65`, and its register 0 is a **command register**;
  register 1 is the **status/data** register.
* **On a cold boot the chip is in bootloader mode**, not app mode.  Live status
  `0x09` = bit3 (bootloader) + bit0 (just reset).  The vendor driver resets it
  (`reg0 = 0x81`), reads a 21-byte bootloader block from `reg1`, checks the
  firmware version, and starts the resident app with **`reg0 = 0x80`**.
* **The app firmware is resident and current**, so a Linux driver does **not**
  need the firmware upload path on this unit: the live 21-byte block is
  `bootloader ID = 0xB0` (Pegasus), `app start = 0x900`, `app version = 0x020A`,
  `app checksum = 0xB72F`, `silicon ID = 0x0586`, `build ID = 0x3B` — exactly
  the header of `Pegasus_50.hex` (`Pegasus_50.hex` and `Pegasus2_50.hex` are
  byte-identical; `Pegasus_D4_A.hex` is the older build `0x35`).
* **The app protocol is small:** write `reg0 = 0x02` (config complete), poll
  `reg1` bit0, write `reg 0x1D = 1` (4-finger mode) and `reg 0x1F = 1`
  (report threshold), then read **18 bytes at reg1** on the IRQ: a status byte
  followed by **four 4-byte contacts** with 12-bit packed X/Y.
* **This is not the mainline `cyttsp_core` (Gen2) or `cyttsp5` (Gen5)
  protocol**, so the decision is a **new small driver** (input subsystem), not
  an extension of either.  It is a contained driver: reset/power, a two-command
  boot, a fixed 18-byte report, and a GPIO IRQ.

---

## 1. The live bootloader block

After a plain boot (the pads muxed by the DTS) the chip answers:

```
i2ctransfer -y 1 w1@0x65 0x01 r21
0x09 0xb0 0x05 0x09 0x00 0xb7 0x2f 0x02 0x0a 0x05 0x86 0x40 0x6d 0x03 0x40
0x27 0x30 0x00 0x00 0x00 0x3b
```

`tp_reset` copies these 21 bytes to `dev+0x94`; the field layout (from the
Ghidra decompile of `tp_reset`, `0x15ec4`) is:

| offset | field | live value |
|---|---|---|
| `+0x94` | status | `0x09` = bootloader (bit3) \| just-reset (bit0) |
| `+0x95` | bootloader ID | **`0xB0`** = `'Pegasus'` (`0xB1` Pulsar, `0xB2` Quimby) |
| `+0x96` | bootloader version | `0x05` |
| `+0x97` | app start (BE16) | **`0x0900`** |
| `+0x99` | app checksum (BE16) | `0xB72F` |
| `+0x9B` | app version (BE16) | **`0x020A`** |
| `+0x9D` | silicon ID (BE16) | `0x0586` |
| `+0xA5` | build ID (BE32) | **`0x3B`** |

The chip was in bootloader mode on every cold boot; that is why a bare read of
register 0/1 gave `0x00 0x09` in report 193.

## 2. The vendor sequence (`tp_reset`)

From the decompiled `tp_reset` (`0x15ec4`) and `munin_platform_init`
(`0x17530`):

1. Platform GPIO reset (below).
2. **Write `reg0 = 0x81`** (enter/reset bootloader); sleep 10 ms.
3. **Read 21 bytes at `reg1`**; require status bit3 (bootloader) and bit0
   (just reset), else fail.
4. `munin_panel_init` matches the bootloader ID to a panel:
   `0xB0` Pegasus, `0xB1` Pulsar, `0xB2` Quimby.  Pegasus parameters:
   report register **`0x01`**, 4 contacts, interrupt table 0x80 bytes at
   block 17, app at block 18, `reg 0x1D` = mode, `reg 0x1F` = threshold.
5. `tp_firmware_check` compares the 21-byte app version/build with the `.hex`
   header; **equal here, so no upload**.
6. **Write `reg0 = 0x80`** (start app); sleep 50 ms.
7. Read `reg1`: bit3 must be clear (app mode).
8. **Write `reg0 = 0x02`** (config complete); poll `reg1` bit0 until clear
   (10 × 100 ms).
9. **Write `reg 0x1D = 1`** (4-finger mode), **`reg 0x1F = 1`** (report
   threshold), then the power state (`1` = active threshold 1, `2` = sleep
   threshold `0xFC`).

The `0x81`/`0x80`/`0x02` writes are the bootloader/app command set.  A Linux
driver needs exactly that sequence; the firmware-upload path (`reg0` `0x84`
write-data, `0x83` flash-block, 0x80-byte blocks, Intel HEX) is only needed if
the version check fails.

## 3. The report format

`munin_sample_xy_data` (`0x150b0`) reads `contacts * 4 + 2` bytes at the report
register (Pegasus: **1**, length **18**).  The first byte is a status/header;
then four 4-byte contact records.  `get_coords` (`0x4df0`) unpacks each record:

```
X = byte0 | ((byte2 & 0x0F) << 8)
Y = byte1 | ((byte2 & 0xF0) << 4)
down = byte3 != 0 and (Y != 0xFFF)
```

* 12-bit coordinates; `0xFFF` is the "no contact" sentinel.
* `byte3` is the contact ID / down flag.
* The driver accepts a read when `byte17 & 1 == (status & 0x10) >> 4`
  (a counter/status parity check); otherwise it re-reads up to 10 times.

Live after the app start + init:

```
status reg1          : 0x10        (bit3 clear = app; bit4 = counter)
report (18 bytes)    : 00 ff ff ff 00 ff ff ff 00 ff ff ff 00 ff ff ff 00 86
```

i.e. four sentinel contacts and `byte17 = 0x86` (`byte17 & 1 = 0`,
`status & 0x10 = 0x10 >> 4 = 1`? — the live status byte inside the report is
`0x00`, so the parity is `0 == 0`; the single-byte `reg1` read shows the
counter bit).  The report is structurally valid: four `0xFFF` contacts = no
finger.

## 4. The firmware files

`/base/lib/firmware/munin/` holds three Intel-HEX files, all 36 301 bytes:

| file | sha256 (12) | header version | build ID |
|---|---|---|---|
| `Pegasus_50.hex` | `3aae7ef47107` | `0x020A` (app 0x7A) | `0x3B` |
| `Pegasus2_50.hex` | `3aae7ef47107` | `0x020A` | `0x3B` (byte-identical to `Pegasus_50`) |
| `Pegasus_D4_A.hex` | `c6c604a423b1` | `0x020A` | `0x35` (older) |

Segment 0 is 0x4000 bytes; the app size at segment `0x7C` is **`0x2700`**, the
app version at `0x7A` is `0x020A`, and the checksum field at `0x7E` is the
`0x3030` placeholder the upload path recomputes.  The live chip's
`bootloader ID 0xB0` / `app version 0x020A` / `build ID 0x3B` match
`Pegasus_50.hex`, so no upload is needed.  (`get_fw_path` would choose
`Pegasus_D4_A.hex` only from the fallback path; the manufacturing data at
`0x3F00` normally names the 50 variant.)

## 5. Driver decision

**Write a new, small input driver — not `cyttsp_core` and not `cyttsp5`.**

* Mainline `cyttsp_core` speaks the Gen2 protocol (`cyttsp_i2c`), and
  `cyttsp5` the Gen5 register map; this part speaks RIM's Munin bootloader +
  a 4-contact 12-bit report.  There is no compatible mainline glue.
* The driver is contained: `regmap` over I²C (1-byte registers), the
  reset/power GPIOs (GPIO1_14 power, GPIO4_8 reset), the IRQ (GPIO2_3,
  inferred from `tp_intr=1035`), the `0x81 → 21-byte read → 0x80 → 0x02 →
  0x1D/0x1F` init, and a 18-byte report parser producing `input_mt` slots.
* No firmware upload on this unit.  Keep the upload path out until a version
  mismatch is seen; the `.hex` files are preserved in the 1917 `/base`.

**Wiring recap:** power **GPIO1_14** (low 500 ms → high), reset **GPIO4_8**
(active low), IRQ **GPIO2_3** (`tp_intr=1035`; `1000 + gpio`, inferred), I²C
**I²C2 `0x48072000` = Linux `i2c-1`**, slave **`0x65`**, 1-byte register
addressing, 400 kHz.

**Open:** coordinate orientation/scaling (the 1917 `scaling.conf` has a
1024×600 calibration), the meaning of the status/counter bits, the 0x1D/0x1F
mode/threshold values beyond the active defaults, and the sleep/power states
(`0xFC` threshold).  The panel's native coordinates are 12-bit; the driver
needs the axis map for the 1024×600 panel.

## 6. Driver implementation and the bench result

A new **`drivers/input/touchscreen/playbook-munin.c`** implements this: input
multitouch, 1-byte register I2C, the boot sequence above, the 18-byte report
parser, and a 20 ms poll (the inferred IRQ pad is not wired yet).  The DTS node
is `touchscreen@65` under `&i2c2`, and `CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`.

The **probe must not block**: the controller is not ready while the i2c adapter
is still probing (the first read at ~2.7 s returns `-EREMOTEIO`), and a 40 ×
50 ms retry inside `probe()` delayed the boot enough that the host missed the
USB gadget connect (configured only after a replug).  The boot therefore runs in
a delayed work that retries every 500 ms; the probe returns immediately
(trap 141).

Three images were built:

| image | state | result |
|---|---|---|
| `0x86024669` | probe read fails at 2.68 s | driver does not bind; a manual `bind` works |
| `0x31AD9A77` | 2.6 s retry in probe | **USB warm-reset regression** — host configured only after a replug |
| **`0x04AF1433`** | non-blocking probe + boot work | **self-enumerates (26 s); driver binds** |

On `0x04AF1433`: `PBR: 1-0065 <- playbook-munin` at 2.379 s registers
`input0`; the boot work logs
`Munin bootloader id 0xb0 ver 0x05 app 0x020a build 0x3b` at 2.958 s and
`Munin touchscreen ready (poll 20 ms)` at 3.259 s.  Touching the panel
produces `event0` records: `ABS_MT_SLOT 0`, `ABS_MT_POSITION_X` (≈368–374),
`ABS_MT_POSITION_Y` (≈170–261), plus `ABS_X`/`ABS_Y` and SYN frames — the
touchscreen works under Linux.

`BOOTCTR_FCTRY = 0x2b` after the three test boots; the pack reads Full
4.156 V.  **Open:** the axis max/calibration (the report values look like the
vendor's 1280×768 input device, which its `scaling.conf` maps to the
1024×600 panel), the IRQ pad, and the sleep/power states.

## 7. Sources

* Live: `i2ctransfer` on `i2c-1` at `0x65` (the 21-byte block, the app-start
  sequence, the 18-byte report).  All reads/writes were to the touch only.
* `local/work/qnx-libs/libmtouch-munin.so` (sha256 `5d3c2269…`), Ghidra 12.1.4
  via REA: `tp_reset` `0x15ec4`, `munin_existence_check` `0x156f4`,
  `munin_panel_init` `0x1734c`, `munin_platform_init` `0x17530`,
  `munin_sample_xy_data` `0x150b0`, `get_coords` `0x4df0`, `is_contact_down`
  `0x4da0`, `read_version_info` `0x17ae0`, `tp_firmware_check` `0x18900`,
  `firmware_upload` `0x186b4`, `transfer_block` `0x18598`, `flash_block`
  `0x1846c`, `wait_for_complete` `0x17e94`, `verify_checksum` `0x1839c`.
  (Ghidra image base `0x10000`; the objdump addresses are base-relative.)
* `local/work/rcfs-1917-mnt/lib/firmware/munin/{Pegasus_50,Pegasus2_50,
  Pegasus_D4_A}.hex` and `usr/lib/graphics/omap4430/graphics.conf`
  (`driver = munin`, `tp_intr=1035`, `i2c_slave=0x65`).
* Report 193 (the bus and pad mux), trap 138.
