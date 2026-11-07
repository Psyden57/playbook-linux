# 182 — The CM3217 works under Linux: 191 lux with the lights on, 32 off

**Session 29 (absolute), Linux-era 23.  2026-11-07.**
Status: **CLOSED — working.**  This report supersedes its own first two drafts
("the part is dead" and "the protocol is not settled"); both were wrong, and §4
records why so the mistake is not repeated.

ROADMAP item 9 **step 2 is now complete**: CM3217 + TMP103 + BQ27541 all report
data under Linux.

---

## 0. Result

```
# cat /sys/bus/iio/devices/iio:device2/name            -> cm3217
# cat .../in_illuminance_input   (room lights OFF)     -> 32 32 32 32 37
# cat .../in_illuminance_input   (room lights ON)      -> 191 191 191 191 191 191
```

Stable, repeatable, and it tracks the room light by ~6x.  Driver:
`drivers/iio/light/cm3217.c`, module `cm3217.ko`, DTS node `light-sensor@10`
(`capella,cm3217`), `CONFIG_CM3217=m`, loaded by `rcS`.

---

## 1. The protocol, as it actually is

Report 181's static RE was right about the *addressing* and wrong about the
*timing*.  What the bench established:

| element | value |
|---|---|
| addresses | **0x10** = ALS MSB + command, **0x11** = ALS LSB + config |
| count | register **0x00 of each address**: `(byte(0x10)<<8) | byte(0x11)` |
| enable | write `0x26` to 0x10 |
| **settle** | **none — read immediately.**  Data is present right after the enable |
| **read shape** | **poll for a nonzero pair**; a single read can land in a conversion gap and return 0 |
| idle | write `0x60` to 0x11, then `0x27` to 0x10 (ALS off) |
| ID | never read |
| calibration | the vendor's 19-point `cal_table_IT200MS`, `lux = lux0 * raw / adc0` |

The driver enables, then polls up to 25 x 10 ms for the first nonzero `(hi,lo)`
pair, converts and shuts the ALS down again.

**The settle time was the bug.**  Report 181's disassembly showed
integration-time machinery and I assumed a 200 ms integration wait; measured,
every delay from 1 ms to 200 ms reads **zero**, and only an immediate read
returns data.  A 200 ms sleep was the one thing between a correct protocol and a
working driver.

## 2. Two behaviours that matter later

* **Leaving the part enabled wedges its own automatic-brightness path.**  The
  operator watched the panel stop responding to light while a probe left the ALS
  on, and resume as soon as the vendor idle state (`0x60` -> 0x11, `0x27` -> 0x10)
  was written back.  Our driver always restores the idle state, so polling
  `in_illuminance_input` is safe — but any future tool that pokes this part
  **must** put it back, or it breaks the OS's own use of the sensor.
* **On TabletOS, external I2C accesses race RIM's driver.**  Every QNX-side read
  or write we did interleaved with `ls-cm3217`'s own sequence and broke it; that
  is why `/pps/services/light_controller` showed `Lux::0000` and the panel froze
  *while we were probing*.  The values collected there (`0x6a` plateaus, the
  `0x00` gaps, the "covered vs uncovered" numbers) were artefacts of that race,
  not properties of the part.  **Do not use TabletOS to characterise a device
  another QNX driver owns** — characterise it from Linux, where we are the only
  client.

## 3. Boot log

`local/logs/boot-20261107-095720.log` (1041 lines) — the first full console dump
of this image.  It is clean: no WARN/Oops/backtrace, no L3 errors, and no taint
except the expected `loading out-of-tree module taints kernel` from `cm3217`.  The
three lines worth knowing:

* `mmc4: Failed to initialize a non-removable card` — the known WL1283 SDIO
  failure (report 180).
* `Warning: unable to open an initial console.` — benign and expected: the
  initramfs has no `/dev/console` *node*; the real console is the CDC-ACM
  `ttyGS0`, created a moment later and opened by `sh -l`.
* `random: pbrpmb: uninitialized urandom read` — trap 87, unchanged.

## 4. The three wrong turns, recorded

1. **"The part is dead."**  Wrong.  Derived from the reference showing lux 0 —
   which was itself a race artefact (§2) — and it contradicted the operator's
   direct observation that automatic brightness works on this unit.  **A bench
   measurement that contradicts what the operator can see is wrong until proven
   otherwise.**
2. **"The protocol is not settled."**  Half right — the addressing was right and
   the timing was wrong — but the delay sweep had already shown data only at
   delay 0, and I concluded "unknown" instead of changing the driver.
3. **An invalid cover/uncover test** (two unsynchronised 48 s loops; trap 102).
   Discarded.  The test that settled it was one bounded run per state on Linux,
   with the operator confirming each state first.

## 5. Sources

* Bench, Linux: `iio:device2` = `cm3217`, `in_illuminance_input` 32 -> 191.
* `local/logs/boot-20261107-095720.log`.
* `tools/qnx-cm3217.c` (bare-byte probe + delay sweep — the sweep is what showed
  delay 0 works).
* Reports 176, 178, 181; traps 95, 102.
