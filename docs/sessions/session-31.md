# Session 31 (absolute) — the charger is an ISL9519 and now works; WDT2 matches the reference

> Numbering: absolute `31`; the reports call this Linux-era **session 25**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-08.
Unit state at the end: **our Linux image `0xE36C679A` in region 0** (the
ISL9519 charger driver plus the WDT2 58.6 s/10 s policy) and the 7.3.3 charger
rootfs in region 1; the **pack charges at ~0.4 A** from host USB (13 % and
rising).  The 1917 IFS is not on media.  `BOOTCTR_FCTRY` read **`0x2D`** on
the final boot (`0x28` → TabletOS top-up to `0x32` → five boots spent).

## Goal of this session

`HANDOFF.md` §1.1 / ROADMAP item 9 step 0: **confirm the prepared TWL6030 BCI
charger** (image `0x91AF34DF` + rootfs `ba275807…`), then proceed to the WL1283.
The charger was found at session-30 end to be the reason the pack ran flat:
there was no `charger` node, so the BCI never probed.

## Confirmed (measured, read in a file, or seen on the bench)

### Bench at session start

* Unit up on **our Linux 7.3.3 image `0x0F6AC47F`** (15-module rootfs),
  `uname -r` = 7.3.3, uptime 60 s.
* **Pack: 3.358 V, capacity 2 %, `Low`, `HEALTH=Dead`, −32 mA** (BQ27541
  `uevent`).  `HEALTH=Dead` is the gauge's SOC-final flag at 2 %, not a
  controller fault.
* **`BOOTCTR_FCTRY = 0x2A`** at that boot (`dmesg`: `pbrpmb: topup:
  BOOTCTR_FCTRY at block 0x3a record@196 = 2a000000`; `0x2a >= threshold 0x1e,
  no write`).  Three boots had happened since session 30's `0x2D`.
* Prepared charger artefacts verified statically before flashing:
  `local/images/kernel-0x91AF34DF-charger.img` (sha256 `7d4b9187…`) contains
  a DTB at `0x903AF8` with `twl6030-charger`, and
  `local/images/rootfs-region1-7.3.3-charger.ext4` (sha256 `ba275807…`) has
  the 17-module closure (`modules.dep`) and `rcS` modprobes
  `twl6030-gpadc`/`twl6030_charger`.

### The prepared charger image probes and fails

* Both regions flashed in one bootrom session
  (`pack -l /tmp/charger.lst -v 1,2 -s -f`, `flash --signed`: blocks
  `0x825..0x8B6` and `0x2915..0x2D14`, `COMPLETE 0x40C0 -> cmd 0x4006`).
* Boot on `0x91AF34DF`: the charger node and both modules are present, but
  **`twl6030_charger` probe fails**:
  `power_supply twl6030_usb: is 0/5/13/39 writeable?` then
  `error -ENODEV: Failed to get battery info`; **no `twl6030_usb` supply**
  appears.  (`ls /sys/class/power_supply` = only `bq27541-0`.)
* Root cause, read in the tree: `power_supply_get_battery_info()` resolves the
  charger's `monitored-battery` phandle and **requires the target node's
  `compatible` to be `simple-battery`** (or a static Samsung battery), else it
  returns `-ENODEV`
  (`local/kernel-tree/drivers/power/supply/power_supply_core.c` §728–766).
  The prepared DTS pointed `monitored-battery` at the **bq27541 gauge node**
  (`compatible = "ti,bq27541"`), which can never pass that check.

### The battery-profile fix works; charging is still not demonstrated

* DTS change: the `battery` label moved off `battery@55` onto a separate
  `battery { compatible = "simple-battery"; }` node (no properties).  Driver
  then falls back to `CHARGERUSB_CTRLLIMIT1/2`.
* New image **`0x306E3DA5`** (`local/images/kernel-0x306E3DA5-battery-profile.img`,
  sha256 `b42f9272…`, 0x91AC14 bytes), flashed region 0 only (the rootfs was
  already the charger rootfs).
* Boot result: **`twl6030_usb` is registered** —
  `OF_NAME=charger`, `ONLINE=1`, `VOLTAGE_NOW` 4.811–4.912 V,
  `INPUT_CURRENT_LIMIT=500000`; the driver logs
  `battery with vmax -22 imax: -22` (the `-EINVAL` printed *before* its
  fallback) and then configures the BCI.
* BCI register state after probe (`0x49`, base `0xE0`):
  `CONTROLLER_CTRL1=0x10` (EN_CHARGER), `CHARGERUSB_VOREG=0x21` (4.16 V),
  `CHARGERUSB_VICHRG=0x08` (900 mA), `CHARGERUSB_CINLIMIT=0x09` (500 mA),
  `CHARGERUSB_CTRL1=0x10` (TERM).  Before probe the bootloader had left
  `CTRLLIMIT1=0x21`, `CTRLLIMIT2=0x08`, `CINLIMIT=0x01` (100 mA) and
  `CONTROLLER_CTRL1=0x00`.
* **`BQ27541 CURRENT_NOW` went from −175 mA (before) to 0** and the voltage
  crept 3.305 → 3.339 V; with the backlight off the current stayed **0** and
  the voltage recovered 3.286 → 3.332 V.  With the backlight back on the gauge
  read **−132 mA, `Discharging`** and `twl6030_usb` flipped to `Not charging`.
  **No positive battery current was ever observed.**
* **The "500 mA limit is consumed by the system" explanation is weak:** before
  the BCI was enabled the pack supplied the *whole* system at **−175 mA**, so
  the load is ~175 mA, not 500 mA — a working charger would have ~325 mA of
  headroom at the 500 mA input limit.  The charge path is therefore not
  delivering current, and the cause is not simple input starvation.
* **Vendor-driver cross-check (primary source, 2011 TI `twl6030_bci_battery.c`,
  `https://github.com/rychly/rk3026-linux-sources/blob/master/drivers/power/twl6030_bci_battery.c`):**
  the USB start sequence is exactly what the mainline driver does —
  `VICHRG`, `CINLIMIT`, `VOREG`, `ITERM`, then
  `CONTROLLER_CTRL1 = EN_CHARGER`.  `SEL_CHARGER` is used only for the **AC
  (VAC)** path, so it is not the missing USB bit.  The vendor driver's USB
  input limits are 500 mA for SDP and 1800 mA for a dedicated charger (DCP).
  It also gates charging on a battery-ID resistance check
  (`is_battery_present()`, GPADC channel 7) that the mainline driver does not
  implement — relevant if the pack's ID/thermistor path is damaged.

### TabletOS cross-check — the charger is an ISL9519

* The 1917 IFS (`local/work/ifs-shim-sealed.ifs`, 489 frames) was flashed to
  region 0; the unit then showed the **flashing yellow low-battery LED** and
  did not boot while docked on the rapid charger — the loader holding the boot
  until the pack was charged enough.  It later booted 1917 when undocked.
* **The pack charged on the dock:** QNX now reports **`StateOfCharge 7`**,
  **`Voltage 3683` mV**, **`StateOfHealth 90`**, **`StateOfBattery::OK`**,
  `Present::YES` — against the Linux reading of 2 % / 3.33 V.  So the pack is
  healthy and accepts charge; Linux's `HEALTH=Dead` was the gauge's SOC-final
  flag at 2 %.
* **QNX's power PPS:** `ChargingState::CC`, `ChargeCurrent 640`,
  `ChargerMaxCurrent 500`, `InputCurrentLimit 384`, `InputCurrentMonitor 430`,
  `MaxSystemVoltage 4208`, `MinSystemVoltage 3328`, `SystemVoltage 3662`,
  `ConnectedChargerSource::USB HOST`; gauge `AverageCurrent -16` mA.  The
  host-port input limit is **below the system load**, so the net is slightly
  negative.  The operator then **confirmed that 1917 does charge on a PC port,
  only slowly — 7 % → 8 % with the screen off** — which is exactly what a
  384 mA input limit against a ~430 mA load predicts; it is a policy
  difference from 2.0.0.4869, not a hardware fault.
* **The charger is an Intersil ISL9519 at I²C1 `0x48070000`, slave `0x09`**
  (the unidentified `0x09` in report 176).  Measured on the live unit:
  VendorID `0xFE = 0x0049` ('I'), DeviceID `0xFF = 0x0003`;
  `0x14 = 0x0280` (640 mA charge current), `0x15 = 0x1070` (4208 mV max
  system voltage), `0x3F = 0x0180` (384 mA input limit), `0x44 = 0x0000`
  (control).  These match the PPS values exactly.  RIM's `/base/bin/battery_win`
  contains `isl_9519_init`/`isl_9519_i2c_dev_p` and the CC/CV state machine.
* **The TWL6030 BCI is NOT used by the stock OS:** on the running TabletOS the
  MAIN_CHARGE block still holds the bootloader's reset values
  (`CONTROLLER_CTRL1 = 0x00`, `VOREG = 0x02`, `VICHRG = 0x04`,
  `CINLIMIT = 0x01`); QNX only writes `CONTROLLER_INT_MASK = 0xDC` for
  VBUS/VAC detection.
* **Vendor rule (Intersil AN1570):** the ISL9519's output is disabled while
  the charge-current and max-system-voltage registers are `0x0000`; non-zero
  values to both enable it; the part shuts down unless refreshed at least
  every **175 s**; words are LO-first.  The vendor's own tool writes
  registers `0x14`, `0x15`, `0x3F` and `0x44`.
* **Static RE of `battery_win` (subagent, file-only):** the register values
  are **masked engineering fields**, not 1 mA/mV codes — `0x14`/`0x3F` bits
  12:7 at 128 mA/LSB, `0x15` bits 13:4 at 16 mV/LSB, `0x3E` bits 14:8 at
  256 mV/LSB.  RIM writes **`0x3E`, `0x15`, `0x14`, `0x3F`** in that order,
  reads them back, and repeats every **~1.07 s**; that rewrite is the watchdog
  kick.  It reads `0x3D` as a liveness probe and **never writes `0x44`**.
  (`isl_9519_init` @ 0xBB18, `isl_9519_update_regs` @ 0xB1B8, worker @ 0x4788.)
* Counter on the last Linux boot: **`0x28`** (two boots spent: `0x2A → 0x28`).

### WDT2: the reference's 58.6 s window is reproducible

* **Question:** TabletOS kicks WDT2 only every ~60 s while our kernel must pet it
  every 2 s.  **Answer: the load value differs.**  Live on our boot
  `WLDR = 0xFFFC1800` → `(0xFFFFFFFF − WLDR + 1)/32768` = **7.8125 s**;
  `WCLR = 0x20` (PTV 0, PRE 1, 32.768 kHz).  QNX's live value (report 109 §8.1)
  is `0xFFE2B400` → **58.6 s**.  The 60 s kick is a consequence of the window.
* **Why our `LDR` writes were "dropped":** only while the timer runs.  TRM
  §22.3.4.9 requires stopping it first via `WSPR`; session 31 did exactly that
  live: `WSPR` `0xAAAA`→`0x5555` (stop), `WLDR = 0xFFE2B400`, `WSPR`
  `0xBBBB`→`0x4444` (start), then a `TGR` kick.  **`WLDR` read back
  `0xFFE2B400` and `WCRR` reloaded to it**, with the unit alive and charging
  afterwards.  The kick is still `TGR` read→invert→write; QNX's `wdtkick`
  never disables the timer.
* The old, abandoned `playbook-dev` tree corroborates the 58.6 s value and the
  same `WSPR` sequences (its `wdtkick` notes).
* **The kernel now adopts it** (`drivers/irqchip/irq-gic.c`): at the late
  initcall it stops WDT2 (`WSPR` `0xAAAA`/`0x5555`), writes `WLDR =
  0xFFE2B400`, restarts (`0xBBBB`/`0x4444`) and pets every **10 s**, falling
  back to 2 s if the readback fails.  **Successive WDT writes must poll
  `WWPS`** — with only `udelay(200)` the sequence stopped at `WSPR=0xBBBB`
  and `WLDR` stayed `0xFFFC1800`.  The reprogram had to move from the
  inlined GIC probe (which did not execute on 7.3.3) to the late initcall.
  Verified on image **`0xE36C679A`**: `WLDR=0xFFE2B400`, and `WCRR` sampled
  every second reloads every ~10 s; the unit is alive past 275 s.  The
  expected `PLAYBOOK WDT` dmesg line does not appear on 7.3.3 (unexplained
  printk anomaly) — verify by registers.

### WL1283 FREF test — negative, and the divider is the DSS clock

* With the pad muxed (`0x4A10019C = 0x41000000`) and `CM_DIV_M5_DPLL_PER`
  forced to the reference's `0x329`, WL_EN verified high (`GPIO2_DATAOUT =
  0x31413200`, and on OMAP `GPIO_OE` 1 = input, 0 = output, so bit 22 = 0 is
  correct for the enable), an MMC5 unbind/bind still failed
  (`Failed to initialize a non-removable card`) and `/bin/pbhci /dev/ttyS1`
  still got no HCI response.  The FREF route is closed (report 180 §8).
* **The panel glitched during this test.**  It was isolated one command at a
  time after a clean boot: GPIO2_22 set/clear, the FREF pad mux and the WDT2
  `WSPR` sequence each left the panel clean; `CM_DIV_M5 = 0x329` glitched it
  immediately, and writing `0x204` back **recovered it without a reboot**.
  Cause: `dpll_per_m5x2_ck` is the **DSS functional-clock parent**
  (`omap4_dss_dss_clk_parents[]`), so the reference's M5 divider changes the
  display clock.  Trap 116.
* The shell hung after the aborted command (echo only, no prompts); the
  operator power-cycled the unit, and a clean boot restored `CM_DIV_M5 =
  0x204`, the safe-mode FREF pad, `WLDR = 0xFFFC1800` and DSS `fck 96 MHz`.

### The ISL9519 Linux driver works

* A minimal `drivers/power/supply/isl9519.c` (built-in,
  `CONFIG_CHARGER_ISL9519=y`, DTS `charger@9` under `&i2c1`) probes the part,
  verifies VendorID/DeviceID, writes the four registers in RIM's order with
  the field steps above, and refreshes them every 5 s.  Image
  **`0xA0B75C43`** (`local/images/kernel-0xA0B75C43-isl9519.img`), region-0
  only.
* Measured: `isl9519 0-0009: ISL9519 charger: 640 mA charge, 3328/4208 mV
  system, 384 mA input`; `isl9519` supply `STATUS=Charging`, `ONLINE=1`;
  **BQ27541 `CURRENT_NOW = +358 000 → +397 000 µA`**, `VOLTAGE_NOW` 3.775 →
  3.789 V, `STATUS=Charging`, capacity 8 %.  Register read-back: `0x14 =
  0x0280`, `0x15 = 0x1070`, `0x3E = 0x0D00`, `0x3F = 0x0180`, IDs
  `0x0049`/`0x0003`.
* The TWL6030 BCI node/modules are left in place but inert.
* **The charger test is closed: the pack charges under Linux.**

## Inferred (reasoning from evidence, not directly measured)

* **The BCI probe is fixed** (the simple-battery node is the documented,
  upstream shape; upstream `omap4-epson-embt2ws.dts` uses the same pattern),
  but **the TWL BCI is not this board's charger** — the stock OS leaves it
  disabled and charges through the ISL9519.  *Strength: strong.*
* **The pack is healthy and accepts charge.**  QNX reports SoC 7 %,
  3.683 V, health 90/OK; the pack rose from 2 % / 3.33 V on the dock.  Linux's
  `HEALTH=Dead` was the gauge's SOC-final flag, not a pack fault.  *Strength:
  strong.*
* **The 1917 host-port charge policy is why the pack appears not to charge on
  a PC port.**  QNX limits input to **384 mA** while the system draws
  **430 mA** (`InputCurrentMonitor`), so the gauge sits at −16 mA.  On the
  dock the limit is higher and it charges.  The operator remembers 2.0.0.4869
  charging on a PC port; that would be a different input-limit policy (or a
  different charger configuration) in the older OS.  *Strength: strong for
  1917; moderate for the 4869 difference.*
* **The TWL BCI work cannot charge this pack, but a four-register ISL9519
  driver can.**  The driver enables the part and the pack takes ~0.4 A from a
  host port; the ISL9519 was the missing piece, not the BCI.  *Strength:
  strong (measured).*

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The prepared charger node will just work."**  It probes and fails with
  `-ENODEV` because `monitored-battery` pointed at the gauge.  The binding
  name (`monitored-battery`) suggests the gauge; the code wants a
  **`simple-battery` profile**.
* **"`twl6030_usb` appearing means the pack is charging."**  It does not: the
  supply was `Charging` at first but the gauge showed 0 mA, and with the panel
  on the pack went back to −132 mA.  Measure the gauge, not the supply status.
* **"The driver prints the negotiated limits."**  `battery with vmax -22
  imax: -22` is printed before the fallback; `-22` is `-EINVAL`, not a value.
  The real limits are `0x21`/`0x08` in `CHARGERUSB_CTRLLIMIT1/2`.
* **"The TWL6030 BCI is the board's charger."**  The stock OS leaves the BCI
  at its bootloader reset values and charges through the **ISL9519** at
  I²C1 0x09.  The session-30 premise ("the board had no charger node") was
  true but irrelevant: adding the node and fixing the probe cannot charge
  this pack.  (Report 183.)
* **"Matching the reference's `CM_DIV_M5 = 0x329` enables the FREF clock
  safely."**  It also changes the **DSS clock** (the same `dpll_per_m5x2_ck`
  is the DSS parent) and glitches the panel; `0x204` restores it.  The FREF
  experiment as specified in the session-30 handoff is not a single variable.
  (Trap 116, report 180 §8.)
* **"The 500 mA input limit is consumed by the system, so the pack cannot
  charge."**  Before the BCI was enabled the pack supplied the whole system at
  −175 mA, so 500 mA leaves ~325 mA of headroom.  The BCI was still not
  delivering charge — because it is not the charger at all (the ISL9519 is).
* **"WDT2's `LDR` writes are dropped, so the window cannot be changed."**
  That was report 109 §8.1's conclusion and it is incomplete: the load is
  writable while the timer is stopped, via `WSPR`.  The real trap is that the
  posted writes need `WWPS` polling — with fixed delays the sequence silently
  stopped at `WSPR=0xBBBB` and `WLDR` stayed `0xFFFC1800` (trap 115).

## Unknown / open questions

* **Does the 5 s refresh keep the part alive beyond the 175 s watchdog?**
  **Yes, measured:** at 197 s uptime the pack was still charging at +396 mA
  with `0x14` still `0x0280`; the driver's refresh timer holds the part on.
* **What does register `0x44` do?**  QNX never writes it, so it is not needed
  for charging on this board; the vendor eval tool writes it but the guide
  does not give the value.  *Would settle it:* the full ISL9519 datasheet
  (request-only from Renesas), only if a future problem points at it.
* **What did 2.0.0.4869 do differently on a PC USB port?**  It reportedly kept
  the pack charged.  *Would settle it:* the 4869 `.ifs`/`.rcfs` battery
  binaries, or an input-limit comparison.
* **Does the loader configure the ISL9519 while it charges the pack off the
  dock?**  The yellow low-battery indication implies a charge path in the
  loader.  *Would settle it:* read the ISL registers while docked (needs a
  non-USB link) or inspect the loader/IFS battery code.
* **Is the pack healthy long-term?**  QNX says SoH 90/OK, but session 30 saw
  71 % → 0 % in twelve minutes.  *Would settle it:* a controlled
  charge/discharge with a known load.

## Superseded / uncertain

* **"The TWL6030 BCI is the board's charger; the pack ran flat because it was
  never instantiated."**  Withdrawn: the BCI is not the charger.  The stock OS
  leaves it disabled and charges through the **ISL9519** (I²C1 0x09).
  The BCI probe fix is correct work but cannot charge this pack.
* **"The pack is end-of-life (`HEALTH=Dead`)."**  Withdrawn for now: QNX
  reports `StateOfHealth 90`, `StateOfBattery::OK`; the Linux health value was
  the gauge's SOC-final flag at 2 %.
* **"The pack was charged by the dock overnight."**  It was not: 2 % at
  session start after hours docked.  The handoff's "charge first" therefore
  could not be satisfied; the operator approved proceeding at 3.358 V.
* **"Flashing both regions is always required to return to Linux."**  Operator
  correction: if only region 0 was modified (the TabletOS IFS), restoring the
  Linux chainloader image in region 0 is enough; flash both when both regions
  changed.

## Artefacts produced

* DTS: `arch/arm/boot/dts/ti/omap/omap4-winchester.dts` (battery-profile node;
  gauge label removed), copied to `kernel/source/omap4-winchester.dts`.
* Images: `local/images/kernel-0x306E3DA5-battery-profile.img`
  (sha256 `b42f9272…`) — the image the BCI probes on;
  `local/images/kernel-0xA0B75C43-isl9519.img` (sha256 `a608a579…`) — the
  ISL9519 driver; `local/images/kernel-0x010F8C5F-wdt58s.img` and
  `local/images/kernel-0x928D4DFE-wdt58s.img` — the two WDT2 intermediate
  builds (GIC-probe path, then late-init path without `WWPS`; both superseded);
  **`local/images/kernel-0xE36C679A-wdt58s.img`** (sha256 `bdbaf5fc…`) — the
  current image (ISL9519 + WDT2 58.6 s/10 s, with `WWPS` polling).
* Drivers: `local/kernel-tree/drivers/power/supply/isl9519.c` (+ `kernel/source/
  isl9519.c`), `CONFIG_CHARGER_ISL9519=y`, DTS `charger@9` under `&i2c1`; and
  `drivers/irqchip/irq-gic.c` (WDT2 `WSPR` reprogram + `WWPS` polling + 10 s
  pet, + `kernel/source/irq-gic.c`).  `tools/qnx-i2c.c` gained `rdw`/`dumpw`
  and `tools/pbi2c` was rebuilt.
* Patch: `kernel/patches/0001-playbook-winchester-7.3.3.patch` re-exported
  with the charger and WDT changes, **7 103 lines / 45 files**,
  sha256 `26babb10…`, verified 0 rejects / 0 mismatches against pristine
  7.3.3.
* Report **183** — the ISL9519 discovery, the register fields and the driver.
* Boot-time state: `BOOTCTR_FCTRY = 0x2D` on the current boot; the unit was
  left **on image `0xE36C679A` and charging** (operator decision).
* **Item 9 step 5 (WM8994 audio, CY8CTMA3 touch) was not started**; it is the
  next task.  The WL1283 (step 4) is blocked on external evidence after the
  FREF route was closed.
