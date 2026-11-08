# 187 — The QNX `winchester` powerman preset has no WLAN rail or clock

**Session 33 (absolute), Linux-era 27.  2026-11-08.**
Status: **static RE only** (`local/work/rcfs-1917-mnt/bin/powerman`,
`local/work/rcfs-1917-mnt/lib/dll/devpm-omap4.so`,
`local/work/rcfs-1917-mnt/etc/system/config/winchester.pm`,
`local/work/rcfs-1917-mnt/scripts/startup.sh`, the 1917 IFS tree).
No device access, no writes.

This is the last software layer report 184 §5 / report 185 §5 left open: the
QNX power manager.  The answer is negative — the `winchester` preset does not
switch a WLAN rail or clock.  Its WLAN authorities only gate the
standby/shutdown and OSWR-prevention decisions.

---

## 0. Headline

* **`winchester_environment` resolves to four tables in `powerman`:** 94
  authorities (`winc_auths`), 47 edges (`winc_edges`), 6 dependency groups
  (`winc_dep_groups`) and 35 initial states (`winc_initial_states`).  The
  preset is compiled in; `winchester.pm` only names it
  (`use_preset_config winchester`) and defines dummy authorities.
* **The WLAN-related authorities are `activity/wifi_driver/active|idle`,
  `air_needs_wifi`, `wlan_needs_DMA` and `WLAN_NEEDS_PREVENT_OSWR`.**  Only two
  of them appear in a dependency group:
  * `activity/wifi_driver/idle` (with `user_standby`) → the group publishes the
    **shutdown** message (`/pps/services/power/shutdown/control`,
    `reason:"power_management"`);
  * `WLAN_NEEDS_PREVENT_OSWR` replaces `SYSLINK_NEEDS_PREVENT_OSWR` in the same
    group, i.e. it prevents that shutdown while the WLAN driver is active.
  `air_needs_wifi` and `wlan_needs_DMA` are published authorities only; no
  dependency group, edge or initial state references them.
* **`ldo_vaux1`/`ldo_vaux3` are not WLAN rails.**  They are initial PPS
  commands (`ldo_vaux3 = power`, `ldo_vaux1 = power`) consumed by
  `pmic-twl6030-ctl`, and `startup.sh` registers only `regen1,regen2,vaux3`
  with it.  VAUX3 is the **LM3532 backlight** rail and REGEN1/2 are the TWL
  GPIO display rails (report 118 §9); VAUX1 is not registered and is inert.
* **`devpm-omap4.so` is the OMAP4 PRCM/voltage/DVFS plugin, not a WLAN power
  switch.**  It carries ABB-LDO, VP/VC voltage, DPLL and OSWR-wakeup code; its
  only WLAN-adjacent strings are the `sdmmc5_*` pad wakeup names (OSWR wake,
  not enumeration) and `ATTILA_L4PER_TA_HSMMC5`.  `/dev/i2c0` is its PMIC
  voltage path (TWL6030), not a board rail for the WL1283.
* **Consequence:** by the handoff's decision table this **closes the powerman
  lead**.  The remaining candidate is the chip side — the 26 MHz TCXO, VIO,
  VBAT and reset — i.e. the multimeter checks of report 184 §5 / report 185 §5.

---

## 1. How the preset is selected

`/base/scripts/startup.sh` (SERVICE_powerman) runs, on the OMAP4430 ES2.x
`Winchester` board:

```
on -d powerman -c devpm-omap4.so -o1 -l5000 -d /etc/system/config/winchester.pm
```

`winchester.pm` says

```
use_preset_config winchester
```

and the header comment states the dependencies live **in the powerman source
code**, not in the `.pm`; the file only defines the authority names so the
preset can respond to them.  `get_env()` maps the name to
`winchester_environment`; the strings in the binary include `winchester`,
`winchester2`, `colt`, `cyclone`, `8660` and `8960`, and the error text
"unrecognized board name >%s< in use_preset_config statement".

The `-c` argument loads `lib/dll/devpm-omap4.so` as the board power plugin
(powerman's own string `devpm-%s.so`).

## 2. The winchester preset tables

`winchester_environment` is at VMA `0xbd804` and is filled by relocations:

| table | address | entries |
|---|---|---|
| `winc_auths` | `0x77004` | 94 authorities (8-byte `{name, initial}` records) |
| `winc_edges` | `0xb373c` | 47 edges (stride `0x1dc`) |
| `winc_dep_groups` | `0x772fc` | 6 dependency groups + terminator (stride `0x1d0`) |
| `winc_initial_states` | `0xb907c` | 35 initial states (stride `0x1fc`) |

`powerman` is stripped but its `.dynsym` survives; the tables above were found
from the relocation entries for `winchester_environment` and decoded from the
functions `get_env` (`0x1029c`), `auth_group_enabledP` (`0xffec`),
`process_dep_group` (`0x10e84`), `handle_dependency_groups` (`0x10f18`),
`apply_cmd_block` (`0x10b40`), `call_callout` (`0x11890`) and
`kg_set_initial_state` (`0x11b80`).

### 2.1 The WLAN authorities

```
 34 activity/wifi_driver/active   initial 0
 35 activity/wifi_driver/idle     initial 1
 66 SYSLINK_NEEDS_PREVENT_OSWR    initial 0
 67 WLAN_NEEDS_PREVENT_OSWR       initial 0
 68 PREVENT_OSWR                  initial 0
 69 air_needs_wifi                initial 0
 72 usb_needs_DMA                 initial 0
 73 eMMC_needs_DMA                initial 0
 90 wlan_needs_DMA                initial 0
```

They are toggled from a device-activity message:
`kg_handle_activity()` formats `activity/%s/active` and `activity/%s/idle`,
flips the pair and re-runs `handle_dependency_groups()`.  `wifi_driver` is the
device name (the strings `/pps/services/wifi/control` and `msg::wifi_power`
are in the same binary), i.e. the WLAN power service raises/lowers these
authorities when it powers the driver.

### 2.2 The dependency groups

Each group is `{type, auth-list..., cmd-block at +0x3c}`; type 1 = OR of the
listed auths, type 0 = AND, type 2 = unconditional.

| # | type | condition | command |
|---|---|---|---|
| 0 | OR | `prevent_cpu_sleep` or `powerstate/camera/1` | `cpu = sleep` (PPS) |
| 1 | OR | `SYSLINK_NEEDS_CORE_ACTIVE` or `powerstate/camera/1` | `cpu = sleep` |
| 2 | OR | `PREVENT_OSWR` | `cpu = sleep` |
| 3 | AND | `user_standby` + `activity/wifi_driver/idle` + `SYSLINK_NEEDS_PREVENT_OSWR` | **shutdown** |
| 4 | AND | `user_standby` + `activity/wifi_driver/idle` + `WLAN_NEEDS_PREVENT_OSWR` | **shutdown** |
| 5 | AND | `user_standby` + `activity/wifi_driver/idle` | **shutdown** |
| 6 | unconditional | — | `cpu = sleep` |

The "shutdown" command is command type 1, which goes through
`apply_cmd_to_pop()` → `to_pps()`, and its two strings are
`/pps/services/power/shutdown/control` and
`msg::shutdown\ndat::{shutdownType:0,reason:"power_management"}\n`.
**No group touches a regulator, an I²C device or a clock.**

Semantics: `user_standby` plus a WLAN driver that is *idle* allows the
standby shutdown; `WLAN_NEEDS_PREVENT_OSWR` (when raised by the WLAN stack)
blocks it while the WLAN needs the core awake.  That is the whole WLAN role.

### 2.3 The edges

47 edges.  Their command blocks are PPS publications and sleep/restart
callouts, not hardware writes:

* 16 thermal-mitigation edges (`THERMAL_MITIGATION_DDR_*`, PPS commands);
* 17 edges with no command;
* the sleep/button/thermal/power edges (`be_asleep_by_inactivity`,
  `be_asleep_by_button`, `be_awake`, `die`, `do_restart`,
  `toggle_sleep_state`, `Battery_BELOW_DANGEROUS`, `Battery_NOMINAL`,
  `Temperature_ABOVE_EXTREME`) using the same PPS/`call_callout` types.

`call_callout`'s types are PPS writes (`cpu`, `display`, …), `exit`, sleep-state
toggle, `set_auth_enable`, and one recursive command block.  There is **no
PMIC/I²C/clock callout** anywhere in this preset.

### 2.4 The initial states and `ldo_vaux1`/`ldo_vaux3`

The 35 initial states are `{object, value}` PPS commands: `ldo_vaux1 = power`,
`ldo_vaux3 = power`, `ldo_regen1 = power`, `phy_power = on`,
`lcd_display = on`, `mtouch = on`, `devb-mmcsd = power`, `BT =`, … .  The
`ldo_*`/`regen*` names are the authorities of the **`pmic-twl6030-ctl`**
resmgr: `-p list` starts "a powerman client for the comma separated list of
LDOs" (`pmic-twl6030-ctl` usage string), and
`startup.sh` starts it as

```
pmic-twl6030-ctl -U 99:99 -p regen1,regen2,vaux3
```

so only **REGEN1, REGEN2 and VAUX3** are wired to powerman on this board.
Report 118 §9 measured what those are: VAUX3 powers the **LM3532 backlight**
(and its absence wedges the I²C bus), REGEN1/REGEN2 are the panel rails
(TWL GPIO2/GPIO15).  `ldo_vaux1` is published but has no client, so it is
inert.  None of them is a WLAN rail, and none is a clock.

### 2.5 `devpm-omap4.so`

The plugin is the OMAP4 PRCM/power engine: `prcm_*`, ABB LDO, VP/VC voltage,
DPLL configuration tables, DVFS/OPP, and the OSWR/wakeup pad tables.  Its WLAN
adjacent strings are only:

```
sdmmc5_clk_sdmmc5_cmd
sdmmc5_dat1_sdmmc5_dat0
sdmmc5_dat3_sdmmc5_dat2
gpmc_clk_gpmc_nwp
ATTILA_L4PER_TA_HSMMC5
```

i.e. the SDMMC5/`gpmc_nwp` pads in the **OSWR wakeup** table.  `/dev/i2c0`
(TWL6030) is used for voltage control of the MPU/IVA/CORE domains, not for a
board rail.  There is no `wifi`/`wlan`/`vaux` device in the plugin.

## 3. What this settles, and what it does not

* **Settled:** the `winchester` preset does not enable a WLAN regulator or
  clock.  Its WLAN authorities are standby/OSWR gates.  The `ldo_vaux*` strings
  that motivated the RE belong to the display rails (`vaux3` → LM3532,
  `regen1/2` → panel), not to the WL1283.
* **Settled elsewhere:** the WL1283 driver itself (`devnp-ti1283.so`) touches
  no PMIC/CM/DPLL register; it drives GPIO2_22 (`WL_EN`) and GPIO2_21 (`IRQ`)
  and enables `CM_L4PER_HSMMC5`/PRM only.  The BT driver's only GPIO line is
  BT_EN (GPIO2_23), low in both captures.  Reports 180 §2, 185 §3–§4.
* **Not settled:** the chip's own power/clock.  The 26 MHz TCXO, VIO, VBAT and
  the reset/enable levels at the chip still need the multimeter checks.  A
  PMIC register read proves programming, not the voltage at the chip.

## 4. Sources

* `local/work/rcfs-1917-mnt/bin/powerman` (QNX ARM, ELF32 DYN, 1 109 116 B;
  `.dynsym` present, `.text` at `0xb660`); tables and functions as cited above.
* `local/work/rcfs-1917-mnt/lib/dll/devpm-omap4.so` (239 040 B, `.dynsym`
  present; PRCM/ABB/DPLL/OSWR strings).
* `local/work/rcfs-1917-mnt/etc/system/config/winchester.pm`,
  `scripts/startup.sh` (lines 1456 and 1504), and
  `local/work/ifs1917-tree/proc/boot/pmic-twl6030-ctl` (usage strings and
  `pmic-twl6030.cfg`).
* Report 118 §9 (VAUX3/REGEN1/REGEN2 are the display rails), report 180 §2
  (the WLAN driver's register set), reports 184/185 (the powerman question and
  its stop condition).

## 5. Bench addendum — the first direct measurements at the bare board

The operator removed the main board from the device (no screen, no antennas;
the pack stays connected because the board does not start from USB alone).
The unit booted our 7.3.3 image `0xAF4FC103`; the pack was Full (4.14 V,
+167 mA) and `WL_EN`/`GPIO2_22` was toggled from the shell with
`devmem 0x48055190/0x48055194 32 0x00400000` (`DATAOUT` reads back
`0x31413200` high, `0x31013200` low).

* **The accessible points show 1.8 V, not 3.3 V.**  With WL_EN high, one front
  pad and about four pads on the back directly behind the chip measure
  **≈1.8 V**; the rest measure **0 V**; one resistor measures **≈4.7 V**
  (VBUS through the input path, not a chip rail).
* **WL_EN is not brought out.**  The SoC pad is confirmed (`CONTROL_CORE_PAD0_
  GPMC_NWP` `0x4A10007C`, mux 3 = `gpio_54` = GPIO2_22, TRM §18.6.13 table
  18-358/§18.6.13.2) and the pin was toggled through a 90 s low, a 5 s/5 s
  square wave and a 2 s/2 s square wave.  **No accessible point followed it**;
  the 1.8 V points stayed 1.8 V and the 0 V points stayed 0 V.
* **The chip still does not drive WL_IRQ.**  `GPIO2_DATAIN` (0x48055138) reads
  `0x01020010` with WL_EN high; bit 21 (WL_IRQ) is **0**.  The reference reads
  `0x08201018` (bit 21 = 1) with WLAN up (report 185 §1).
* **The TWL rails on Linux are all in their expected states** (`/sys/class/
  regulator`): V1V8 1.8 V, VCXIO 1.8 V, VDAC 1.8 V, VANA 2.1 V, V2V1 2.1 V,
  VUSB 3.3 V, VAUX1/2/3 2.8 V, VMMC/VPP/VUSIM off.  So a 1.8 V VIO is present;
  the 3.3 V VBAT is **not visible** at any probed point, but the board's 3.3 V
  rails (VUSB, `vemmc`) are on in software.

The next useful comparison is the same labelled points under the working
reference (boot the 1917 IFS on the same bare board and measure them with WLAN
up): a point that is 3.3 V there and 0 V under Linux is the missing rail.  If
every point matches, the difference is inside the chip (TCXO/reset/strap), not
on a board rail.
