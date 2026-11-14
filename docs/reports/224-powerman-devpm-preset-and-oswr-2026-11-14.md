# 224 — powerman (Winchester2), device model, devpm writes, OSWR pads

Static file-only RE; no bench/USB/network/sudo, no `tools/`. `local/work/1917/base/bin/powerman` (ELF32 ARM DYN, 1 109 116 B), `lib/dll/devpm-omap4.so` (239 040 B), `devpm-omap4-4460.so` (245 980 B), `etc/system/config/winchester.pm`. **Corrects reports 187/189/190.**

## 0. Headline
1. **The board loads `winchester2.pm`, not `winchester.pm`.** `startup.sh:1494` picks it for `*_Winchester2_*`/non-4460; `get_env` (`0x1029c`, branch `0x1036c`) maps it to `winchester2_environment` (`0x10ef38`) → `winc2_auths`/`winc2_edges`/`winc2_dep_groups`/`winc2_initial_states` (relocs `0x10ef38..44`). 187/189/190 read `winchester_environment` (`0xbd804`, `winc_*`) — the `*_ES2.1_*`/fallback path (`startup.sh:1501/1504`).
2. **"6 dependency groups" is block 0 only.** Both arrays are *block arrays*: 28 (`winc_`), 32 (`winc2`), stride `0x2270` = 19 slots, each block ended by its own `type==3` group.
3. **Both presets have a WLAN block** (`winc_` `0x7956c`, `winc2` `0xc065c`): `air_needs_wifi`+`user_standby`+`battery_nominal` → 5 callouts to a device named **`wifi_driver`** publishing `msg::wifi_power` to `/pps/services/wifi/control` (`dat::off|graceful_shutdown|low_power|on`). Not a rail — it asks a peer to change its own mode.
4. **Groups DO command regulator devices**: `ldo_regen1`, `ldo_regen2` (`winc2` b11/12/13/18), TWL6030 REGEN rails (panel/audio, TWL GPIO2/15) registered by `pmic-twl6030-ctl -p regen1,regen2,vaux3` (`startup.sh:1456`). **No `ldo_vaux*` group, no WLAN rail, no clock, no power domain, no GPIO, no I2C.**
5. devpm: 8 `mmap_device_io`; exactly the 4 `CONTROL_CORE_PAD*` ones are stored-never-read (**verified**). One powerman device (`iva_opp`). No MMCHS5/`CM_L4PER_HSMMC5` write.
6. OSWR table in force = Winchester2's (`0x3568c`/`0x356c0`); it contains the **WL_IRQ pad `0x078`** (high half gpio_53). WL_EN (`0x07C`) is **not** in it.
7. **Verdict: nothing here can hold the radio down at probe time.**

## 1. The preset, decoded completely
**1.1 The .pm files.** `winchester.pm` (109 lines): `:4 use_preset_config winchester`; `:10 predicate device/dummy/dummy:[0..1]`; `:15-26` 12 authorities `needs device/dummy/dummy:1`; `:32-70` 25 `activity/*`/`powerstate*/*` authorities, same dummy form (`activity/wifi_driver/active|idle` `:38,39`); `:74-88` 8 `cpu_*` modes + sleep values `0x11/0x13/0x14/0x20`; `:91-95` `device/cpu/*` predicates; `:100-108` `predinfo device/cpu/mode` (8 OPP lines). `:5-6,13-14` say the `needs` clauses are dummies; real dependencies are compiled in. **Loaded instead:** `winchester2.pm` (121), `:3 use_preset_config winchester2`, 40 authorities (adds `wifi_tx_on`, `wlan_needs_DMA`, `human_near_*`, `powerstate/cellular_TX/*`, `activity/ulpi_phy/*`, `activity/cellular_TX/*`), same CPU block `:86-121`. `get_env` `strcmp` chain → `kg_environmentp` (BSS `0x10f008`): richmond(abort 0x102e8), cyclone(0x10314), winchester(0x10340), winchester2(0x1036c), colt(0x10398), 8660(0x103c4), 8960(0x103f0).

**1.2 Authorities** — `winc2_auths` @0xbe03c, stride 8, 117 + NUL (`winc_auths` @0x77004: 94 + NUL); VA = 0xbe03c+8i. All 117 in table order (name=init):
```
user_standby=0 · wakestandby_by_user_action=0 · battery_nominal=1 · lcd_dim=0 · powerstate/ldo_regen1/0=0 · powerstate/ldo_regen1/1=0 · powerstate/ldo_regen1/2=1 · powerstate/ldo_regen2/0=0 · powerstate/ldo_regen2/1=0
powerstate/ldo_regen2/2=1 · activity/audio_headphones/active=0 · activity/audio_headphones/idle=1 · powerstate/audio_headphones/0=1 · powerstate/audio_headphones/1=0 · activity/audio_speaker/active=0 · activity/audio_speaker/idle=1 · powerstate/audio_speaker/0=1 · powerstate/audio_speaker/1=0
activity/audio_boost/active=0 · activity/audio_boost/idle=1 · powerstate/audio_boost/0=1 · powerstate/audio_boost/1=0 · activity/battery_temp_sensor/active=0 · activity/battery_temp_sensor/idle=1 · powerstate/battery_temp_sensor/0=1 · powerstate/battery_temp_sensor/1=0 · activity/audio_hdmi/active=0
activity/audio_hdmi/idle=1 · powerstate/audio_hdmi/0=0 · powerstate/audio_hdmi/1=1 · activity/hdmi_display/active=1 · activity/hdmi_display/idle=0 · powerstate/hdmi_display/0=0 · powerstate/hdmi_display/1=1 · activity/wifi_driver/active=0 · activity/wifi_driver/idle=1
activity/lcd_display/active=1 · activity/lcd_display/idle=0 · powerstate/lcd_display/0=0 · powerstate/lcd_display/1=1 · activity/usb_phy/active=0 · activity/usb_phy/idle=1 · activity/charger_phy/active=0 · activity/charger_phy/idle=1 · activity/ulpi_phy/idle=1
activity/ulpi_phy/active=0 · activity/audio_driver/active=0 · activity/audio_driver/idle=1 · activity/magnetometer/active=0 · activity/magnetometer/idle=1 · activity/gyroscope/active=0 · activity/gyroscope/idle=0 · activity/accelerometer/active=0 · activity/accelerometer/idle=0
powerstate/accelerometer/0=0 · powerstate/accelerometer/1=1 · activity/light_sensor/active=0 · activity/light_sensor/idle=0 · SYSLINK_NEEDS_CORE_ACTIVE=0 · SYSLINK_NEEDS_PREVENT_OSWR=0 · WLAN_NEEDS_PREVENT_OSWR=0 · PREVENT_OSWR=0 · powerstate_req/syslink_manager/0=1
powerstate_req/syslink_manager/1=0 · powerstate_req/syslink_manager/2=0 · powerstate_req/syslink_manager/3=0 · powerstate/cellular_TX/0=0 · powerstate/cellular_TX/1=0 · powerstate/cellular_TX/2=0 · powerstate/cellular_TX/3=1 · powerstate/camera/0=0 · powerstate/camera/1=1
powerstate/lcd_backlight/0=0 · powerstate/lcd_backlight/1=0 · powerstate/lcd_backlight/2=1 · activity/cellular_radio/idle=0 · activity/cellular_radio/active=1 · powerstate/cellular_radio/0=0 · powerstate/cellular_radio/1=0 · air_needs_wifi=0 · lcd_needs_DDR=1
gpu_active=1 · usb_needs_DMA=0 · eMMC_needs_DMA=0 · cpu_temperature_nominal=1 · cpu_temperature_elevated=0 · cpu_temperature_above_extreme=0 · charger_temperature_nominal=1 · charger_temperature_elevated=0 · charger_temperature_extreme=0
backlight_temperature_elevated=0 · usb_temperature_elevated=0 · cellular_temp_nominal=1 · cellular_temp_elevated=0 · cellular_temp_above_dangerous=0 · cellular_temp_above_extreme=0 · wifi_temp_nominal=1 · wifi_temp_elevated=0 · wifi_temp_above_dangerous=0
wifi_temp_above_extreme=0 · battery_below_dangerous=0 · battery_below_critical=0 · lock_cpu_to_OPP50=0 · lock_cpu_to_OPP100=0 · lock_cpu_to_OPPTURBO=0 · lock_cpu_to_OPPNITRO=0 · prevent_cpu_sleep=0 · human_near_cellular_TX=0
human_near_volume_control=0 · human_near_WIFI24_TX=0 · human_near_WIFI5_TX=0 · wifi_tx_on=0 · BBP_needs_location_services=0 · wlan_needs_DMA=0 · limit_cpu_to_OPP50=0 · limit_cpu_to_OPP100=0 · limit_cpu_to_OPPTurbo=0
```
WLAN VAs: `activity/wifi_driver/idle` 0xbe154(1), `air_needs_wifi` 0xbe2b4(0), `wlan_needs_DMA` 0xbe3c4(0), `WLAN_NEEDS_PREVENT_OSWR` 0xbe21c(0), `wifi_tx_on` 0xbe3b4(0); `0xbe3e4` = NUL.

**1.3 Dependency groups.** Layout (`auth_group_enabledP` 0xffec, `process_dep_group` 0x10e84, `handle_dependency_groups` 0x10f18): `+0` type (0=AND,1=OR,2=uncond,3=end), `+4` up to 14 `{char *name,u32 value}` (null name ends), `+0x3c` cmd block (`u32 state; {u32 type,a1,a2,a3,a4}…` until type 0), `+0x50` next-list ptr. Group stride `0x1d0`; **array stride `0x2270` = 19 slots** (`movw r4,#0x2270`; `add r4,r4,#0x2240; add r4,r4,#0x30`). Cmd types: **1** device `a1`=mode `a2` (a3 aux), **2** callout (a2=type), **3** funcstatus (`/pps/%s/funcstatus/%s?nopersist`), **4** log only.
```
Block 0 (winc_ 0x772fc, winc2 0xbe3ec) — identical in both:
g0 OR   prevent_cpu_sleep                                    -> cpu=sleep
g1 OR   SYSLINK_NEEDS_CORE_ACTIVE, powerstate/camera/1       -> cpu=sleep
g2 OR   PREVENT_OSWR                                         -> cpu=sleep
g3 AND  user_standby, activity/wifi_driver/idle, SYSLINK_NEEDS_PREVENT_OSWR -> cpu=sleep
g4 AND  user_standby, activity/wifi_driver/idle, WLAN_NEEDS_PREVENT_OSWR    -> cpu=sleep
g5 AND  user_standby, activity/wifi_driver/idle              -> cpu=sleep
g6 UNCOND                                                    -> cpu=sleep
Raw entry (0x77338/0xbe428) = type=1,"cpu","sleep" — a CPU-policy mode request, NOT the shutdown message.

Block 1 = the WLAN block (winc_ 0x7956c, winc2 0xc065c; the number is the callout type):
g0 AND air_needs_wifi, user_standby, battery_nominal -> callout wifi_driver {9|15}
g1 AND user_standby, battery_nominal                 -> callout wifi_driver {8|14}
g2 AND air_needs_wifi, battery_nominal               -> callout wifi_driver {10|16}
g3 AND battery_nominal                               -> callout wifi_driver {9|15}
g4 UNCOND                                            -> callout wifi_driver {7|13}
call_callout (0x11890, jump table 0x118c0, 34 entries): 1..3 /pps/services/power/shutdown/control
+ dat::{shutdownType:0|1|1,reason:"power_management"}; 4 toggle_the_sleep_state(0x117e8);
5,6 internal flag 0/1; 7,13 wifi dat::off; 8,14 dat::graceful_shutdown; 9,15 dat::low_power;
10,16 dat::on; 11,17 BT command::radio_shutdown; 12,18 BT command::radio_init;
19..30 repeat 7..12/13..18; 31,32 set_auth_enable(1|0); 33 recursive block.
Wifi payload = /pps/services/wifi/control + msg::wifi_power\nid::from_powerman.

Blocks 2–18 (winc2) — device-mode blocks, several to regulators:
b2 audio_hdmi=on  b3 audio_hdmi=on  b4 core_opp=throughput(1),hdmi_display=on
b5 hdmi_display=on  b6 location_services=power  b7 phy_power=on
b8 phy_power=on,usb_phy=on  b9 phy_power=on,charger_phy=on  b10 audio_driver=on
b11 ldo_regen1=power(1),core_opp,lcd_display,lcd_backlight [5 groups]
b12 ldo_regen1=power(0) [6]  b13 ldo_regen2=power(0) [3]
b14 audio_speaker=on  b15 audio_headphones=on  b16 audio_boost=on
b17 battery_temp_sensor=on  b18 ldo_regen1=power(1),ulpi_phy=on
Blocks 19–30: CPU OPP/thermal (cpu=throughput), iva_opp+syslink_manager, cellular_TX,
cellular_radio, cap_sensor, light_sensor, charger_power_driver, usb_power_driver,
magnetometer, gyroscope, accelerometer, core_opp (both displays off).
```
`winc_` = 27 blocks/90 group instances; `winc2` = 31/100. Only one group is in `winc_` but not `winc2` (an `ldo_regen1` variant without `activity/ulpi_phy/idle`); `winc2` adds 9 cellular/ulpi/cap-sensor groups.

**1.4 Edges** — `winc2_edges` @0x1031ec, stride 0x1dc, 57 + NUL (`winc_edges` @0xb373c: 47 + NUL). `+0x04/+0x08` = `{name,value}` condition pairs; `+0x48` = cmd block.
```
e0..15 THERMAL_MITIGATION_DDR_{1,2}_TEMP_STEP_0..7                     [no cond]
e16 short_input_timeout {lcd_dim=1}
e17 declare_WIFI_TX_ON {wifi_tx_on=1}  e18 declare_WIFI_TX_OFF {wifi_tx_on=0} [empty cmd]
e19 be_asleep_by_inactivity {lcd_dim=0,lcd_display/idle=1,lcd_display/active=0,user_standby=1,wakestandby_by_user_action=0}
e20 be_asleep_by_button / e21 be_asleep_and_locked_by_button / e22 be_asleep_by_gesture
    {lcd_dim=0,lcd_display/idle=1,lcd_display/active=0,user_standby=1,wakestandby_by_user_action=1}
e23 be_awake {lcd_dim=0,lcd_display/idle=0,lcd_display/active=1,hdmi_display/idle=0,hdmi_display/active=1,wakestandby_by_user_action=1,user_standby=0}
e24 toggle_sleep_state  e25 die  e26 do_restart / e27 do_shutdown {lcd_display/idle=0,lcd_display/active=1}
e28 Temperature_NOMINAL {cpu_temperature_above_extreme=0,elevated=0,nominal=1}
e29 Temperature_ELEVATED {cpu_temperature_above_extreme=0,elevated=1,nominal=0}
e41 Temperature_ABOVE_EXTREME {cpu_temperature_above_extreme=1,elevated=0,nominal=0}
e30..33 cellular_Temperature_{NOMINAL,ELEVATED,ABOVE_DANGEROUS,ABOVE_EXTREME}
e34..36 THERMAL_MITIGATION_CPU_{NOMINAL,ELEVATED,EXTREME}
e37..40 WIFI_Temperature_{NOMINAL,ELEVATED,ABOVE_DANGEROUS,ABOVE_EXTREME}
e42 Battery_BELOW_CRITICAL {battery_nominal=0,below_dangerous=0,below_critical=1}
e43 Battery_BELOW_DANGEROUS {battery_nominal=0,below_dangerous=1,below_critical=0}
e44 Battery_NOMINAL {below_dangerous=0,below_critical=0,battery_nominal=1}
e45..47 THERMAL_MITIGATION_CHARGER_{NOMINAL,ELEVATED,EXTREME}
e48/49 THERMAL_MITIGATION_LCD_BACKLIGHT_{NOMINAL,ELEVATED} {backlight_temperature_elevated=0|1}
e50/51 THERMAL_MITIGATION_USB_INPUT_{NOMINAL,ELEVATED} {usb_temperature_elevated=0|1}
e52..56 THERMAL_MITIGATION_CPU_STEP_0..4 {limit_cpu_to_OPP50/OPP100/OPPTurbo triples}
```
The `cellular_/WIFI_Temperature_*` sets are the same 4-term exclusive sets as `cpu_temperature_*`. Payloads are only device-mode requests (`ldo_vaux3=power`, `BT=<n>`, `wifi_driver=<n>`, `mtouch`, `camera`, `lcd_backlight`, …), a callout (`die`/`do_restart`/`do_shutdown` → `shutdownType:*`), funcstatus (`user_activity`), or a log. `declare_WIFI_TX_*` have **empty** command blocks (`0xdeadead`). **No edge touches a rail electrically, a clock, a GPIO or an I2C device.** `kg_handle_edge` (0x11740) gid-checks (0x19908) before `apply_edge` (0x11704).

**1.5 Initial states** — `winc2_initial_states` @0x109dc4, stride 0x1fc, 39 + NUL:
```
ldo_vaux1<-power · ldo_vaux3<-power · phy_power<-on · ldo_regen1<-power · cpu<-sleep · cpu<-throughput · lcd_display<-on · mtouch<-on · hdmi_display<-on
lcd_backlight<-power · camera<-on · BT<-18 · location_services<-power · audio_hdmi<-on · light_sensor_driver<-on · button_handler<-33 · usb_phy<-on · charger_phy<-on
devb-mmcsd<-power · ulpi_phy<-on · iva_opp<-throughput · syslink_manager<-id · cellular_radio<-on · cellular_TX<-id · core_opp<-throughput aux=1 · audio_driver<-on · cap_sensor<-on
ddr1_temp_driver<-id · ddr2_temp_driver<-id · light_sensor<-on · usb_power_driver<-id · charger_power_driver<-id · cellular_radio<-on · gyroscope<-on · magnetometer<-on · accelerometer<-on
audio_speaker<-on · audio_headphones<-on · audio_boost<-on · battery_temp_sensor<-on   [+bulk auth pairs on audio_hdmi/ulpi_phy]
```
`winc_initial_states` @0xb907c: 35 + NUL (no ulpi/cellular/cap_sensor; `BT`=0xc, `button_handler`=0x21 vs `winc2`'s 18/33). `ldo_vaux1`/`ldo_vaux3` appear **only here**; no group/edge re-requests them; only `vaux3` is registered with the pmic client (`ldo_vaux1` inert). Neither is a WLAN rail (118 §9: VAUX3 = LM3532 backlight; REGEN1/2 = panel/TWL GPIO2/15).

**1.6 Any edge/group on a regulator, power domain, clock, GPIO or I2C?** **Regulator YES** — `ldo_regen1`/`ldo_regen2` (b11/12/13/18; REGEN = panel/audio, TWL GPIO2/15). **Power domain NO** — `cpu=sleep` is a policy request; the domain state is devpm's. **Clock NO** — no dpll/cm_/divider name in either preset. **GPIO NO** in powerman (REGEN1/2 are TWL GPIOs 2/15 *inside the pmic client*). **I2C NO** — powerman opens no `/dev/i2c*`; only PPS file I/O.

## 2. powerman's device model
**No devctl, no resmgr — a PPS server.** Strings: `system/power/mgr` 0x1be8c, `system/power/dev` 0x1bb80, `system/power/dev/` 0x1bb00, `system/power/auth` 0x1bc20, `system/power/auth/` 0x1baec, `%s/%s/%s/%s` 0x1bb74, `%s?delta,nopersist` 0x1bb98, `%s/%s?wait,delta` 0x1bff8, `%s?nopersist,delta,critical,f=REQUESTED_MODE` 0x1bf8c. Client API is compiled into each client (**no `libpowman` in the tree**): `powman_create` 0x15770 (`calloc(1,4*(0x1e+n_modes))`, copies the mode table, `strdup`s class/name at 0x15804/0x15818/0x1582c); `powman_connect` 0x15410 (builds `/pps`+`system/power/mgr`, opens it, sends `CONNECT::<n>,<class>,<name>` 0x1bb5c/0x1bf64, then `_powman_start_backchannel` 0x15f78). `powman_alive`/`powman_idle` are client helpers; the manager sees only the attribute strings and keeps `powman_alive_count` (BSS 0x10f480) / `powman_idle_count` (0x10f448). `powman_edge` (0x1bbd0) is the named-edge trigger. Wire format, parsed by the device-request parser at **0x13b14** (dispatch on first char; table 0x13bd0: `*`→0x13c60, `A`→0x140c0, `C`→0x13fac, `G`→0x14010, `I`→0x1405c, `M`→0x13c70):

| attr | VA | effect |
|---|---|---|
| `ALIVE::` | 0x1bd6c | alive refcount — what `powman_alive("name")` writes |
| `IDLE::` | 0x1bd64 | release the alive claim |
| `CURRENT_MODE::` | 0x1bd48 | current mode index |
| `GENERIC::` | 0x1bd58 | characteristic/annotation |
| `MODE_<n>::` | 0x1bc98 | `MODE_<n>::<c>[=<v>][,<c>[=<v>]]` + cost `%u/%u/%u` |
| `REQUESTED_MODE` | 0x1ba90 | manager→client allowed mode |
| `RESPONSE::` | 0x1bab4 | client ack |

So **`powman_alive("name")` is not an action** — it bumps a PPS refcount. A device's hardware effect comes solely from its `MODE_*` declaration plus the `[mode]`/`callout` commands other groups point at it. **Resource names** addressed by `winc2` devices: `BT button_handler overall_playbook user_activity cpu core_opp iva_opp syslink_manager cellular_TX cellular_radio cap_sensor light_sensor light_sensor_driver magnetometer gyroscope accelerometer camera navigator mtouch lcd_display lcd_backlight hdmi_display audio_hdmi audio_driver audio_speaker audio_headphones audio_boost battery_temp_sensor usb_phy charger_phy ulpi_phy phy_power location_services devb-mmcsd charger_power_driver usb_power_driver ddr1_temp_driver ddr2_temp_driver ldo_vaux1 ldo_vaux3 ldo_regen1 ldo_regen2 wifi_driver`. `cpu` is bound to the plug-in: `cpu_init` 0xe934 `dlopen`s `devpm-%s.so` (0x192d8), `dlsym`s `cpupm_init` (`"Missing 'cpupm_init' symbol"`), registers `device/cpu/mode` via `cpu_set_power` 0xedc8 (`'/cpu.'`,`'sleep'`,`'throughput'`). **`wifi_driver` is addressed but registered by nobody**: `grep -rla wifi_driver` over the tree hits only `powerman` and four `.pm` files. `devnp-ti1283.so` has `powman_edge` 0xce7e4 + `_powman_pps_root` and calls `powman_edge("declare_WIFI_TX_ON"|"…OFF")` (sites 0x27ad0/0x27c18, literals 0xd07fc/0xd0824) — empty command blocks; it never publishes `activity/wifi_driver/*`. Those come from `toggle_activity_auths` 0x1111c via `kg_handle_activity` 0x11258, driven by a PPS handler in a service absent from the tree.

## 3. devpm-omap4.so — every MMIO write and I2C request
Byte-level `BL` scan of `.text` (0xca2c…0x26be0, 26 733 insns) vs `.rel.plt` (63 slots): **8 `mmap_device_io` sites** — 0xdf30 `omap4_cpu_offline_prep` 0xdec4 → `0x48240100`/0x80 (SAR/secure RAM); 0x102e8, 0x10308, 0x10328, 0x10348 `cpupm_init` 0xfcb4 → `0x4A10007C`, `0x4A100148`, `0x4A10014C`, `0x4A100150`, 4 B each, **stored-never-read**; 0x10508 `event_receive_thread` 0x104b0 → `0x4A100054` (GPIO2 IRQSTATUS, OSWR wake); 0x10520 same → `0x4A100144` (GPIO6 IRQSTATUS); 0x12f14 `core_oswr_gpio_init` 0x12e14 → 13-pad table (§4). **Dead-globals check:** the four handles land in GOT slots 0x353f4/0x35474/0x3524c/0x35320 (offsets 0x3f4/0x474/0x24c/0x320, literals 0x103d8/0x103dc/0x103e0/0x103e4, stores 0x102f4/0x10314/0x10334/0x10354). A full byte scan for `ldr Rt,[pc,#imm]` (1 820 sites) shows the **only** loaders of those four literal slots are those four stores. Confirms 190 §3.3.

All other writes go through the instance struct (`+0x3d4` PRM, `+0x2dc` CM, `+0x4c0` SmartReflex, `+0x478` GPIO, `+0x2dc`+0x320 `CM_SYS_CLKSEL`):
* **PRCM voltage/ABB/VP-VC/DVFS** — `cpupm_init` 0x10074..0x10190 reads `CM_SYS_CLKSEL`+5 siblings then picks an OPP; `prcm_vc_bypass_write` 0x1accc called 8× with (domain,reg,val) triples (`0x12/0x56/0x36`, `0x12/0x55/0x40`, `0x12/0x62/0x29`, `0x12/0x61/0x40`, `0x12/0x5c/0x28`, `0x12/0x5b/0x40`). `omap4_vfs` 0xf7a4, `omap4_init_vfs_mode` 0xfa00, `omap4_enable_smartreflex` 0xf71c, `prcm_sr_*`. **Only rail names:** `prcm_slowopp_ldoconf` 0x3aa94, `prcm_bypassopp_ldoconf` 0x3aa78, `prcm_nominalopp_ldoconf` 0x3aab0, `prcm_fastopp_ldoconf` 0x3aa5c (ABB-LDO trim per OPP), plus `prcm_sr_voltages` 0x3a9f0, `sr_sensor_params` 0x3a9c0, `vps` 0x3a9e4.
* **Power domains** — `prcm_set_module_mode` 0x145d4, `prcm_enable_module` 0x1fd28, `prcm_disable_module` 0x205e4, `prcm_set_clkdomain_state` 0x163f0; callers `omap4_state_set` 0xe8ec, `omap4_core_state_set` 0x11af0, `omap4_core_state_restore` 0x10924, `omap4_idle_s2_prep` 0xe3c8, `prcm_cd_*`. Names are CRED strings (`prcm_mmc1`, `prcm_mmc2`, `prcm_mmc_sd3/4/5`, `prcm_i2c1..4`, `prcm_usb_*`, `prcm_gpio2..6`). `MMC_SD5` exists in CRED (0x33640: base 0x480d5000, size 0x300, name 0x27e94) but **no code selects it**: nothing in `.text` builds an address in `0x4A0095xx`, and the only MMC clock names used are `prcm_clk_hsmmc1_fclk`/`prcm_clk_hsmmc2_fclk`. **No MMCHS5 write** (190 §3.3).
* **OPP/DVFS tables** — `prcm_4430_instance_by_irq` 0x3aacc, `prcm_opp_div`; the SR monitor writes 0x4A05EA20, 0x4A05EA70, 0x4A05EB20, 0x4A05EB60, 0x4A05EBB0, 0x4A05E030 (SmartReflex AVGWEIGHT/…) and reads 0x4A326000..0x4A326B20 (PRM VC/VP).
* **GPIO wake** — `event_receive_thread` maps 0x4A100054/0x4A100144, masks `+0x10` (IRQENABLE), enables IRQs 0x2b/0x2, journals status back; OSWR only.
* **I2C/regulator request** — exactly **one** `open("/dev/i2c0",O_RDWR)` in `omap4_phoenix_init` (0xf394, string 0x26da4) and **one** `devctlv` (0x0f51c) issuing the TWL6030 `phoenix-sleep-kick` (0x26db0), i2c_msg at 0xf3b4..0xf3f4. One device, one request, no rail selection, no VAUX1/2, no WLAN rail.
* **The one powerman device** — `power_init` 0xd60c → `powman_create` 0xd638: `iva_opp` (0x26c28), class `driver` (0x26c30), modes `opp_50`/`opp_100`/`opp_turbo` (0x26cb8/0x26ccc/0x26cd4, `+throughput` 0x26cc0); handler `do_what_powerman_says` 0xd52c → `prcm_change_ivahd_opp`. Nothing MMC/WLAN.

Board branching: `machine_name` vs `"Winchester"` 0x26ce0, `"Winchester2"` 0x26cec, `"Colt"` 0x26d00, `"London"` 0x26f90; overrides `DEVPM_32K_THRESH`, `DEVPM_NO_PHOENIX_KICK`, `DISABLE_DPLL`, `DISABLE_QUIESCE`. `devpm-omap4-4460.so` adds only 4460 material (`A2OCMURR`, `A2OMISC`, `A2ORPT`, extra ABE/IPU clocks); its OSWR pad tables are identical (same 31 `0x4A1000xx` words at 0x3a5d4..0x3a6d4) and it too registers only `iva_opp`; selected only for `*OMAP4460*`.

## 4. The OSWR wake tables
`core_oswr_gpio_init` (0x12e14) `strcmp`s `machine_name` against `"Colt"` (0x26d00, also `"London"` 0x26f90), `"Winchester2"` (0x26cec), else default; stores `{cfg,addr}` into BSS 0x3ad24 and 0x3ad24+0x358 (via GOT 0x5d24), then walks 13 entries (`cmp r4,#0x34`) doing `mmap_device_io(4,addr)`, storing each handle at 0x3ad24+4i, skipping `0xffffffff`. Tables: Colt/London cfg `0x35624`/addr `0x35658`; **Winchester2 cfg `0x3568c`/addr `0x356c0`**; default cfg `0x356f4`/addr `0x35728`. Winchester2 complete:
```
cfg  : 00010915 00000816 00000814 00010815 00010103 0001031c ffffffff ffffffff 00010d1e 00010819 0000081a 0000010e 0000091f
addr : 4a100078 4a1001c4 4a1001c0 4a1001c0 4a100054 4a100118 ffffffff ffffffff 4a1001d0 4a1001c8 4a1001cc 4a10006c 4a100098
```
| # | pad reg | bank/bit | ident |
|---|---|---|---|
| 0 | 0x4A100078 | GPIO2 bit21 | **pad 0x07A = gpio_53 = WL_IRQ** (low half 0x078 = gpio_52) |
| 1 | 0x4A1001C4 | GPIO1 bit22 | board pad |
| 2 | 0x4A1001C0 | GPIO1 bit20 | board pad |
| 3 | 0x4A1001C0 | GPIO1 bit21 | board pad (2nd half) |
| 4 | 0x4A100054 | GPIO2 bit3 | pad offset **0x054** = gpmc_nwp first half = GPIO2_3 — *not* WL_EN |
| 5 | 0x4A100118 | GPIO4 bit28 | board pad |
| 8 | 0x4A1001D0 | GPIO6 bit30 | board pad |
| 9 | 0x4A1001C8 | GPIO1 bit25 | board pad |
| 10 | 0x4A1001CC | GPIO1 bit26 | board pad |
| 11 | 0x4A10006C | GPIO2 bit14 | board pad |
| 12 | 0x4A100098 | GPIO2 bit31 | board pad |

Report 190 §3.3 is right that **WL_IRQ (0x07A, GPIO2_21) is in the Winchester2 table** — the table is keyed by pad register `0x4A100078`, whose halves are gpio_52/gpio_53 — but **WL_EN (0x07C/gpio_54) is not**; entry 4's `0x054` is a different pad (`gpmc_nwp` low half, GPIO2_3). Report 187 §2.5's claim that the four `cpupm_init` names *are* wake-table entries is wrong — those are the dead globals of §3. Colt/London differs: entry 0 is 0x4A10016C/GPIO1 bit27. **Entry/exit:** `core_oswr_gpio_init` is called only from `omap4_idle_s2_prep` (0xe3c8) on *entry* to OSWR (Sleep2), and only maps handles. The pad-programming consumer is 0x10684, called only from `omap4_core_state_restore` (0x10924), which `omap4_state_set` (0xe8ec) calls on the way back: it walks the addr table, tests each GPIO IRQSTATUS, ORs the wake-enable, sets `+0x28 |= 0x400`; restore clears it. **Nothing runs at boot, nothing at SDIO probe**; `omap4_idle_s1_*` (retention) never touches these pads.

## 5. The WLAN authorities
| authority | references | effect |
|---|---|---|
| `activity/wifi_driver/active` 0xbe14c | auths[34] | **nothing** |
| `activity/wifi_driver/idle` 0xbe154 | auths[35]; b0 g3/g4/g5 | gate `cpu=sleep` (allows OSWR) |
| `air_needs_wifi` 0xbe2b4 | auths[79]; b1 g0/g2 | `wifi_driver` callout `dat::on`/`dat::low_power` |
| `wlan_needs_DMA` 0xbe3c4 | auths[113] | **nothing** |
| `WLAN_NEEDS_PREVENT_OSWR` 0xbe21c | auths[60]; b0 g4 | gate `cpu=sleep` (forbids OSWR) |
| `wifi_tx_on` 0xbe3b4 | auths[111]; edges 17/18 | empty command blocks (veto only) |
| `wifi_temp_*` 0xbe33c..0xbe354 | auths[96..99]; edges 37–40 | thermal mitigation/shutdown |

**No path where a missing client leaves a rail or clock off.** `activity/wifi_driver/idle` inits to **1** (0xbe154) ⇒ with no client the idle claim is already set, so b0 g3/g4/g5 can fire with `user_standby`; their only command is `cpu=sleep` — a sleep *permission*, never an enable. `WLAN_NEEDS_PREVENT_OSWR` inits to **0** ⇒ unraised, g4 never blocks OSWR; only a sleep gate. `air_needs_wifi` inits to **0** ⇒ b1 g0/g2 never fire; g1/g3/g4 emit `[callout] wifi_driver <n>` which `apply_cmd_to_pop` resolves **by device name**. No binary registers a `wifi_driver` device, so the callout has no target. Even if it did, the payload is a PPS write (`dat::off|graceful_shutdown|low_power|on` to `/pps/services/wifi/control`), a peer's own power policy, not a rail. No authority, edge or group ever names a rail, clock, GPIO or I2C device as a consequence of a WLAN authority.

## 6. Verdict
**Nothing in the loaded preset (`winc2`) or in `devpm-omap4.so` can hold the WL1283 down at probe time.** The preset's only electrical actors are `ldo_regen1`/`ldo_regen2` (TWL REGEN → GPIO2/15, panel/audio) plus the initial-state-only `ldo_vaux1`/`ldo_vaux3` (VAUX3 = LM3532 backlight, VAUX1 inert). devpm writes PRCM/ABB/VC-VP/SR/DPLL for MPU/IVA/CORE, generic power-domain state, two GPIO IRQSTATUS windows, one TWL6030 I2C sleep-kick, and 13 OSWR pad wake-enables — never MMCHS5, never a TWL rail; its four `CONTROL_CORE_PAD` mappings are dead stores. The OSWR table lists the WL_IRQ pad and is programmed only on OSWR entry/exit. The WLAN authorities are PPS mode gates (`cpu=sleep` permission plus `msg::wifi_power` publishes to an unregistered device); `powman_alive` is a PPS refcount, not a switch. **What the reference does that Linux's runtime-PM/clock framework does not: nothing for the WLAN.** Remaining candidates are chip-side (VBAT/TCXO/reset) or OMAP HSMMC5 sampling (reports 184 §5, 185 §5, 188 §4).

## 7. Corrections to earlier reports
1. 187/189/190 decoded `winc_*`; this board loads `winc2_*` (`startup.sh:1494`); `winc_*` is the `*_ES2.1_*`/fallback path.
2. "6 dependency groups" = block 0. Both arrays are 28/32-block arrays of 1–9 groups (stride `0x2270`, per-block `type==3` terminator).
3. A WLAN block exists in **both** presets (`winc_` 0x7956c, `winc2` 0xc065c) publishing `msg::wifi_power` for a device `wifi_driver`; 187 §2.2's "`air_needs_wifi` … no dependency group references them" is wrong (b1 g0/g2 reference it at 0x79570/0x79910 in `winc_`).
4. Groups **do** command a regulator (`ldo_regen1`/`ldo_regen2`); 187 §2.2's "no group touches a regulator" held only for block 0.
5. Group commands publish **`cpu=sleep`**, not the shutdown message; 187 §2.2 attributed `/pps/services/power/shutdown/control` + `shutdownType:1` to them. Those payloads are `call_callout` types 1–3, used by edges.
6. `winc2`: 57 edges (not 47), 118 authorities (not 94).
7. OSWR: 190 §3.3's WL_IRQ 0x07A/GPIO2_21 is correct; WL_EN 0x07C is **absent** from the table, and 187 §2.5 misidentified the dead `cpupm_init` globals as wake-table entries.
8. Confirmed: four mmap globals stored-never-read; no MMCHS5/`CM_L4PER_HSMMC5` write; one powerman device (`iva_opp`).
9. `winchester.pm` (the task's file) is real but **not the loaded preset** here.

## 8. Sources
powerman `.dynsym` 357 / `.rel.dyn` 3832 / `.rel.plt` 117. `get_env` 0x1029c, `auth_group_enabledP` 0xffec, `process_dep_group` 0x10e84, `handle_dependency_groups` 0x10f18, `apply_cmd_block` 0x10b40, `apply_cmd_to_pop` 0x104a8, `call_callout` 0x11890 (table 0x118c0), `kg_set_initial_state` 0x11b80, `kg_handle_edge` 0x11740, `apply_edge` 0x11704, `toggle_activity_auths` 0x1111c, `kg_handle_activity` 0x11258, `apply_bulk_auths` 0x11464, `flip_auth` 0x10464, `set_auth_enable` 0x1152c, `powman_create` 0x15770, `powman_connect` 0x15410, `_powman_start_backchannel` 0x15f78, parser 0x13b14 (table 0x13bd0), `cpu_init` 0xe934, `cpu_set_power` 0xedc8; tables `winc2_*` 0xbe03c/0x1031ec/0xbe3ec/0x109dc4, `winc_*` 0x77004/0xb373c/0x772fc/0xb907c. devpm `cpupm_init` 0xfcb4, `power_init` 0xd60c, `core_oswr_gpio_init` 0x12e14 (consumer 0x10684), `omap4_core_state_set` 0x11af0, `omap4_core_state_restore` 0x10924, `omap4_state_set` 0xe8ec, `omap4_idle_s2_prep` 0xe3c8, `event_receive_thread` 0x104b0, `omap4_phoenix_init` 0xf334, `omap4_cpu_offline_prep` 0xdec4, `do_what_powerman_says` 0xd52c; OSWR tables 0x35624/0x3568c/0x356f4 (+addr 0x35658/0x356c0/0x35728); CRED `MMC_SD5` 0x33640; `.rel.plt` 63. Presets `winchester.pm` (109)/`winchester2.pm` (121); `startup.sh` 1456, 1491–1504; `proc/boot/pmic-twl6030-ctl` (`ldo_%s` 0x580c); `pmic-twl6030_winchester2.cfg`. Driver `devnp-ti1283.so`: `powman_edge` 0xce7e4, sites 0x27ad0/0x27c18, names 0xd07fc/0xd0824, `system/power/mgr` 0x106424. Reports 118 §9, 180 §2, 184/185, 187, 189 §2–4, 190 §3.3/§6.
