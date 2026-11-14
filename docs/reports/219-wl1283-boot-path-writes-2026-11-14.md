# 219 — What the reference's BOOT path does to power/clock/reset before `wifi.sh`

Date 2026-11-14. **Static, file-only** (no bench, no USB, no network); nothing under `tools/` run.
Tools: `$QNX_HOST/usr/bin/ntoarmv7-objdump` on `objdump -D -b binary -m arm`, `readelf`, `dd`,
`python3`; register names harvested from `hardware/swpu231ap.pdf.txt` (TRM SWPU231AP, Tables
3-360/3-1130 + per-register summaries). Read: `local/work/1917/ifs/proc/boot/*` (incl.
`proc/boot/.script`, the compiled IFS boot script), `local/work/1917/base/scripts/*`,
`local/work/1917/ifs/etc/system/config/*`, `local/work/ifs-shim-sealed.ifs`.

## 0. Headline

1. **A TWL6030 writer nobody had read: `proc/boot/pmic_twl6030_cfg`.** The IFS boot script runs it
   *before* the PMIC driver under the banner `Configure power management chip`. It writes **38
   single-byte TWL6030 registers** on I²C-0 slaves `0x48`/`0x49` (plus 5 on slave `0x38`), incl.
   `0x84`–`0x8F`, `0x98`–`0x9B`, `0xAD`–`0xB2`, `0xBD`/`0xBE`, `0xA1`–`0xA3`, `0xE5`, `0xF5`,
   `0xFA` (§3). **Linux never runs it.** The working oracle does: `strings
   local/work/ifs-shim-sealed.ifs` → `proc/boot/pmic_twl6030_cfg`, `SETUP-CORE-INACTIVE`,
   `Configure power management chip`.
2. **`pmic-winchester-twl6030` switches REGEN1/REGEN2 off and detaches them from all groups on
   DVT3+/MBID≥5** (`Detected MBID=5+, regen1 and regen2 GPIO controlled` …), reproducing the live
   QNX `0xAD..0xAF = 00/01/00`, `0xB0..0xB2 = 00/01/00` and explaining the QNX-vs-Linux `07/05/55`
   **by name** (§4.4). Report 192's "`0x8C`–`0x8E` is CLK32KG" is corrected: in the QNX TWL6030
   table `0x8C` is **VAUX3** (§4.2).
3. **`startup-playbook` writes `CM_L4PER_MMCSD5_CLKCTRL` (`0x4A009560`) = `0x2`** in a full
   module-clock sweep, and `CM_L4PER_UART2_CLKCTRL` (`0x4A009548`) (§2.1). It writes **no** PRM
   L3INIT (`0x4A307300`) and **no** PRM L4PER (`0x4A307400`–`0x4A3075FF`), so **no**
   `RM_L4PER_MMCSD5_CONTEXT (0x4A307564)`, no `PM_L4PER_MMCSD5_WKDEP`. It *does* drive I²C-1
   (`0x48070000` @0x80100aac/0x80100abc, `CONTROL_I2C_0 = 0x11111111`) with a few TWL writes (§2.4:
   `0x48:2C=7F, 80=00, 82=01, 25=07`, `0x4A:07=11/08=F0`) — report 192's "no TWL writes in
   startup-playbook" needs narrowing (still: no `CFG_GRP`, no `0x8C`–`0x8F`, no `0xBC`–`0xBE`, no
   `pmic-twl6030-ctl` call).
4. **Order** (§6): IFS binaries → `/base/scripts/startup.sh` run level 0 → `net` → `net.sh` →
   `wifi.sh` → `mount -Tio-pkt … devnp-ti1283.so`. `pmic` (the only thing that finally registers
   powerman clients for `regen1,regen2,vaux3`) is run level **1**, i.e. **after** `wifi.sh`.
5. **Verdict** (§7): one class of boot-path action exists in the reference with no Linux
   counterpart — the I²C PMIC configuration (`pmic_twl6030_cfg` §3 + the four `startup-playbook`
   writes §2.4 + REGEN1/2 off §4.4) — plus the PRCM-HAL voltage setup (§5). Both are replayable
   from Linux with `i2cset`/`devmem`, no kernel change.

## 1. The IFS boot script (`ifs/proc/boot/.script`) — order

Records are `u16 len, u16 type` (0 = exec, 1 = waitfor, 3 = print), decoded in file order:

| off | record |
|---|---|
| 0x3c | msg `Welcome to QNX Neutrino Trunk on the … OMAP4430 … Winchester Board` |
| 0xac / 0xc0 | msg `SETUP-CORE-INACTIVE` + **exec `setup-core-inactive full`** (§5) |
| — | `bmetrics`, `resource_seed dma=0,31`, `devc-seromap`, `slogger`, `pipe`, `trustzone-omap4`, `shm_lock_init` |
| 0x32c | `starting I2C driver...` + `i2c-omap35xx-omap4 -p 0x48070000 -i 88 …` → waitfor `/dev/i2c0` (also `0x48072000`→`/dev/i2c1`, `0x48350000`→`/dev/i2c3`) |
| 0x49c | `starting Watchdog` + `omap4430-wdtkick -t 15000 -P -U 99:99` |
| **0x4f0 / 0x51a** | **msg `Configure power management chip` + exec `pmic_twl6030_cfg`** (§3) |
| 0x549 | msg `Take touch pad out of reset` (message only, no command follows) |
| 0x56a / 0x587 / 0x5d0 | msg `Start PMIC driver...` + `pmic-winchester-twl6030 -U 99:99` + waitfor `/dev/pmic/regulator` (§4) |
| **0x5e8 / 0x604** | **msg `Initialize regulators` + exec `/proc/boot/pmic-twl6030-ctl_script`** (§4.3) |
| — | `random`(entropy.so), `devb-mmcsd-winchester` (MMC1/2 only, report 189), `spi-master … base=0x480ba100,irq=80,sdma=0` |
| 0x7fc | `starting SPI driver` + **`/proc/boot/splash_script`** |
| — | `backlight_win`, `rpmb-omap4`, `nvram_resource_manager -U 99:0 /dev/hd0`, `sanityChecking test /dev/hd1 /dev/hd2 …` |
| 0x5078 | exec `/base/scripts/startup.sh` (→ §6) |

**There is no `/proc/boot/startup` script** — the order lives entirely in `.script`; the image's
`startup` slot is `startup-playbook` (imagefs record `908 44008 startup.*`, §2).
`splash_script` in full: `MIN_SYS_VOLTAGE=3328` (`3455` on Winchester2) then
`on -d -p 20 winchester-bootreason -m${MIN_SYS_VOLTAGE} -d3328 -U 99:0 -k -vvvv` — that program only
*reads* the TWL (`Fail to read TWL6030_CTRL_STAT1 register`, charger/USB strings).
`pmic-twl6030-ctl_script` in full: `pmic-twl6030-ctl -c /etc/system/config/pmic-twl6030.cfg`
(`…_winchester2.cfg` on Winchester2). Our board is `…_Winchester_…` → **`pmic-twl6030.cfg`** (§4.3).

## 2. `startup-playbook` — MMIO/TWL **write** inventory

`ifs/proc/boot/startup-playbook`, `0x44008` B @ imagefs `0x908`, raw ARM, base `0x80100000`
(sha per report 192: `bc7d3a41…`). Method: full ARM disassembly, then (a) scan of every 32-bit
literal for MMIO values, (b) scan for `movw`/`movt` MMIO pairs, (c) a register-tracking pass
resolving `base+offset` of every `str/strb/strh`. **144 stores resolve.**

### 2.1 CM2 — the page that matters for the WL1283

`0x4A009000`–`0x4A0095FF` is three blocks (TRM Table 3-1130): `CAM_CM2 0x4A009000`,
`L3INIT_CM2 0x4A009300`, `L4PER_CM2 0x4A009400` (512 B). MMCSD5's clock control is in L4PER_CM2.

| store VA | target(s) | value | register (TRM) |
|---|---|---|---|
| 0x801017b4/b8/bc | 0x4A009000 / 020 / 028 | 2 / 0x102 / 2 | `CM_CAM_CLKSTCTRL`, `CM_CAM_ISS_CLKCTRL`, `CM_CAM_FDIF_CLKCTRL` |
| 0x80101754, 0x80101758 | 0x4A009328, 0x4A009330 | 0x01000002 | `CM_L3INIT_HSMMC1/2_CLKCTRL` |
| 0x801028bc, 0x801028f4 | 0x4A009328 | read \| 0x01000000 | `CM_L3INIT_HSMMC1_CLKCTRL` |
| 0x80103628 / 0x80107e20 / 0x80107e14 | 0x4A009358 / 360 / 3E0 | read\|0x03000000 / — / — | `CM_L3INIT_HSUSBHOST/OTG/USBPHY_CLKCTRL` |
| 0x8010169c | **0x4A009400** | **2** | `CM_L4PER_CLKSTCTRL` |
| 0x801016a4–b8 | 0x4A009428/430/438/440/448/450 | 2 | `CM_L4PER_GPTIMER10/11/2/3/4/9_CLKCTRL` |
| 0x801016bc–cc | 0x4A009460/468/470/478/480 | 1 | `CM_L4PER_GPIO2/3/4/5/6_CLKCTRL` |
| 0x80101728 / 0x80101740 | 0x4A009488 / 0x4A0094E0 | 2 | `CM_L4PER_HDQ1W / MCBSP4_CLKCTRL` |
| 0x80101730–3c | 0x4A0094A0/A8/B0/B8 | 2 | `CM_L4PER_I2C1/2/3/4_CLKCTRL` |
| 0x80101744–50 | 0x4A0094F0/F8/500/508 | 2 | `CM_L4PER_MCSPI1/2/3/4_CLKCTRL` |
| 0x8010175c, 0x80101760 | 0x4A009520, 0x4A009528 | 2 | `CM_L4PER_MMCSD3/4_CLKCTRL` |
| **0x80101764** `e5832560 str r2,[r3,#1376] ; 0x560` | **0x4A009560** | **2** | **`CM_L4PER_MMCSD5_CLKCTRL`** |
| 0x80101768/6c/70/74 | 0x4A009540/548/550/558 | 2 | `CM_L4PER_UART1/**UART2**/UART3/UART4_CLKCTRL` |

`r2=2` (`mov r2,#2` @0x80101624), `r1=1` (@0x80101628), `r6=0x01000002` (`movw r6,#2` @0x80101698 +
`movt r6,#0x100` @0x801016a0), `r3=0x4A009000` (`movw r3,#0x9000` @0x801015fc + `movt r3,#0x4a00`
@0x80101604). `MODULEMODE=0b10` = explicitly **enabled** (not `HW_AUTO`).

### 2.2 Everything else (by target block)

| target | registers written (values where statically known) |
|---|---|
| `CM1 0x4A004000` | `CM1_ABE_CLKSTCTRL 0x500=3`; `AESS 0x528, PDM 530, DMIC 538, MCASP 540, GPTIMER5/6/7/8 568/570/578/580, WDTIMER3 588 = 2`; `MCBSP1/2/3 548/550/558 = 0`; **`ABE_SLIMBUS 560 = 0xF02`**; `CM_DSP_CLKSTCTRL 400 / DSP_CLKCTRL 420` |
| `CM2 0x4A008000` | `CM_MPU_M3_CLKSTCTRL 900 / CLKCTRL 920`; `CM_IVAHD_CLKSTCTRL F00 / IVAHD F20 / SL2 F28`; `CM_SDMA_CLKSTCTRL A00`; `CM_MEMIF_CLKSTCTRL B00 / EMIF_1 B30 / EMIF_2 B38`; `C00=3`; `CM_L3_2_GPMC 828`; `CM_L3INSTR_L3_3 E20 / OCP_WP1 E28`; `CM_ALWON_USBPHY E40` |
| `0x4A306000/0x4A306100` | **`CM_SYS_CLKSEL 0x4A306110 = 5`** (@0x80100884); `CM_ABE_PLL_REF_CLKSEL 0x4A30610C = 0` (@0x8010161c); `PRM_IRQSTATUS_MPU_A9 0x4A306010` |
| `0x4A307000` | `CM_WKUP_CLKSTCTRL 800 = 3` (@0x80101778); `CM_WKUP_WDTIMER2_CLKCTRL 830`, `CM_WKUP_GPIO1_CLKCTRL 838 = 1`, `CM_WKUP_GPTIMER1_CLKCTRL 840 = 0x01000002`, `CM_WKUP_KEYBOARD_CLKCTRL 878 = 0`; **`PRM_VC_VAL_BYPASS BA0`, `PRM_VC_CFG_I2C_MODE BA8`, `PRM_VC_CFG_I2C_CLK BAC = 0x6026 / 0x1E1A`, `PRM_VC_ERRST BF8`**; `PRM_SRAM_WKUP_SETUP BB4`, `PRM_LDO_SRAM_CORE/MPU/IVA_SETUP BB8/BC0/BC8` |
| `SCRM 0x4A30A000` | `PMICSETUPTIME 104`; **`AUXCLK1 314 = 0x00010000`** (@0x80100914), later `AUXCLK1 314`, `AUXCLK3 31C`, `AUXCLK4 320` |
| `0x4A002000` | `CONTROL_DEV_CONF 0x4A002300`, `CONTROL_DPLL_NWELL_TRIM_0 0x4A002330` |
| `WDT2 0x4A314000` | `WDT_WSPR 0x4A314048 = 0xAAAA` ×2 (@0x801036b4/0x801036f8) — agrees with report 192 §5 (`WSPR=0xAAAA`, `WLDR=0xFFE2B400`, start) |
| GPIO banks | `0x4A310010`, `0x48055010`, `0x48057010`, `0x48059010`: read-modify-`orr #1` |
| padconf (table) | base `0x4A100000` or `+0x0021E000` (`movw ip,#0xe000; movt ip,#0x21` @0x8010cd1c–24 = WKUP `0x4A31E000`), id from 20-byte records |
| GPIO (table) | `+0x134 GPIO_OE`, `+0x190 CLEARDATAOUT` / `+0x194 SETDATAOUT`, `+0x148/0x14C` edge detect, `+0x150 DEBOUNCENABLE`, `+0x154` — re-confirmed at 0x8010cbe4–0x8010cce8 |
| `CONTROL 0x4A100600+` | `CONTROL_PBIASLITE 600 \|= 0x14400000` ×3, **`CONTROL_MMC1 628 \|= 0x0E000000`** ×2, `CONTROL_I2C_0 604 = 0x11111111` |
| USB / SDMA / SCM / SPI | `UHH_SYSCONFIG 0x4A064010`, `UHH_HOSTCONFIG 0x4A064040`, `OTG_SYSCONFIG 0x4A0AB404 = 0x100D`, `OTG_INTERFSEL 0x4A0AB40C`; `DMA4_GCR 0x4A056078`, `DMA4_OCP_SYSCONF 0x4A05602C = 0x2011`; `WKG_ENB_A_0/1 0x48281010/0x48281410`; `0x4824277C = 0xFFFF`; `0x48098110 = 0x30A` (MCSPI1 `MCSPI_SYSCONFIG`), `0x480B8000`/`0x480BA000` (MCSPI3/4 = `spi-master base=0x480ba100`) |
| **TWL6030 (I²C-1)** | **§2.4** |

### 2.3 The three specific questions

* **CM2 L3INIT / L4PER?** **Yes**, module clocks only: `CM_L4PER_MMCSD5_CLKCTRL 0x4A009560 = 2`
  @0x80101764 (+ §2.1's MMCSD3/4, UART1-4, MCSPI1-4, I2C1-4, GPIO2-6, GPTIMERs,
  `CM_L4PER_CLKSTCTRL=2`, `CM_L3INIT_HSMMC1/2=0x01000002`, `CM_CAM_*`).
* **PRM L3INIT (`0x4A307300`) / PRM L4PER (`0x4A307400`–`0x4A3075FF`)?** **No.** No `str` in the
  image targets `0x4A3073xx/74xx/75xx/76xx` (the only writes in that page are `7700`/`7800`/`7B00`+,
  §2.2). So **`RM_L4PER_MMCSD5_CONTEXT (0x4A307564)`, `PM_L4PER_MMCSD5_WKDEP` and
  `PM_L3INIT_PWRSTCTRL` are never written by the boot path** — only by `devnp-ti1283.so` itself
  (`0x4A307564 = 1`, report 180 §5), which was already replayed on Linux (report 217 §4).
* **Regulator group / `pmic-twl6030-ctl`?** No `pmic-twl6030-ctl` string, no `set_groups`, no
  `CFG_GRP`, no `0x8C`–`0x8F`, no `0xBC`–`0xBE`. But (correction to report 192 §6) the I²C-1 path
  **is live code**: `movw r0,#0`/`movt r0,#0x4807` @0x80100aac/0x80100abc → `0x48070000`,
  `CONTROL_I2C_0 = 0x11111111` @0x80100a88, then §2.4. The DPLL/OPP voltage path still goes
  through the OMAP **PRM VC** (§2.2).

### 2.4 TWL6030 I²C accesses in `startup-playbook`
(helpers: `0x80113aac` set address, `0x80113e98` write `reg=val`, `0x80113fac` read, `0x80113fbc`
read reg, `0x80113838` open, `0x801134e0` close)

* 0x80100af4–0x80100b24 — slave **0x4A**, 3-byte write from reg `0x07`: `0x07←0x11`, `0x08←0xF0`.
* 0x80100d7c–0x80100d90 and 0x80107ad4–0x80107ae8 — slave **0x48**, `0x2C ← 0x7F`.
* 0x80107afc–0x80107b04 — slave **0x55**, `read 8` → u16 vs **3328 mV** (fuel gauge, `fg_mod`);
  if low it drops `GPIO1` bit 6 (`GPIO_OE 0x134` bic `0x40`, `SETDATAOUT 0x194 = 0x40`)
  @0x80107b38–4c.
* 0x80107bac / 0x80107bbc — slave **0x48**, `0x80 ← 0x00`, `0x82 ← 0x01` (VANA base / base+2).
* 0x80107bd0 — slave **0x49**, read reg `0xE3`; 0x80107dd8 — slave **0x48**, `0x25 ← 0x07`.

These are the only TWL accesses in `startup-playbook`; none is on the `pmic_twl6030_cfg`/resmgr
regulator set except the `0x80`/`0x82` pair, and **report 192's "no `CLK32KG` / no `0xBC`–`0xBE`
write" survives**. Each sits behind an `open()` (and, for the fuel gauge, return-value) test, so
per-boot execution is not proven statically.

## 3. `pmic_twl6030_cfg` — the pre-driver TWL writer (NEW)

ELF DYN, `LOAD off 0 → paddr 0x8062b000` (0xFBC), entry `0x860` → `0x8062b860`. Strings:
`/dev/i2c0`, `twl6030_i2c_write fail`, `twl6030_i2c_read fail`, `TWL6030 ES1.0/ES2.0/ES2.1`,
`Winchester`, `DESCRIPTION=PMIC TW4030 POWER MANAGEMENT chip Configuration Utility`.
`main` @0x8062bae0: `open("/dev/i2c0",2)` → `devctl(fd,0x80040502,&400000,4)` (**400 kHz**) →
devctl read of the ID page (`0xC0140507`) + print `TWL6030 ESx.y` (the revision gates **no** write)
→ 6 unconditional writes → `confstr(5)+strstr("Winchester")` (0x8062bc8c–9c) → 18-write Winchester
block → 5 writes to slave `0x38` → 6 to `0x48` → 3 to `0x49` → `close`, `exit(0)`.
Write helper `0x8062ba30(slave,reg,value)`: 3-message I²C send, `devctlv(fd,0x80100505,3,…)`.

### 3.1 Exact write list (program order; all single-byte)

```
0x48: BD=05 BE=E1 98=01 99=03 9A=21 9B=15      ; unconditional   (@0x8062bc1c..bc78)
0x48: 84=01 85=03 86=21 87=13                  ; VAUX1  block    (@0x8062bca8..bce4)
0x48: 88=01 89=03 8A=21 8B=13                  ; VAUX2  block    (@0x8062bcf8..bd24)
0x48: 8C=01 8D=03 8E=21 8F=13                  ; VAUX3  block    (@0x8062bd28..bd64)
0x48: AD=01 AE=03 AF=21                        ; REGEN1          (@0x8062bd68..bd94)
0x48: B0=01 B1=03 B2=21                        ; REGEN2          (@0x8062bd98..bdc4)
0x38: 10=00 16=03 17=FF 1D=01 70=00            ; other i2c0 dev  (@0x8062bdd8..be14)
0x48: F5=01 E5=10 A3=18 A1=07 A2=E1 FA=01      ;                 (@0x8062be1c..be74)
0x49: 07=FF 06=05 04=04                        ; slave 0x49      (@0x8062be7c..bea4)
```

### 3.2 Overlap with the 12 "unexplained" TWL differences of reports 190/192
`{0x00,0x01,0x20,0x2d,0xb0,0xb1,0xb2,0xbd,0xbe,0xe4,0xf4,0xfa}` → `0xB0`,`0xB1`,`0xB2`,`0xBD`,
`0xBE`,`0xFA` are written *only* by this program on the reference side: six "unexplained
differences" become "a known reference boot action with no Linux counterpart".

## 4. `pmic-winchester-twl6030` (resmgr) and `pmic-twl6030-ctl` (+cfg)

### 4.1 The regulator table (resmgr file offset 0x8170, 19 × 32 B)
`{char* name; u16 base|variant; u16 pd_bit; u16 volts_mV[..]; u32 id; u32 flags}`; the low byte of
field 1 is the resource base (**GRP = base, TRANS = base+1, STATE = base+2** — the convention
report 192 derived independently and which this table reproduces exactly for regen1/regen2):
`vana 0x80 · vaux1 0x84 · vaux2 0x88 · vaux3 0x8C · vcxio 0x90 · vdac 0x94 · vmmc 0x98 · vpp 0x9C ·
vusb 0xA0 · vusim 0xA4 · regen1 0xAD · regen2 0xB0 · vrtc 0xC2` (+ TWL6032-only names
v1v29/v1v8/v2v1/vmem/vcore1/vcore2 at 0x40/0x46/0x4C/0x64/0x52/0x58). Inline mV lists identify the
entries (`vana → 0x0834` = 2100 mV; `vaux1 → 1000/1300/1800/2500/2800/2900/3000 mV`).

### 4.2 Correction to report 192 §7
Report 192 read the differing triple `0x8C/0x8D/0x8E` as **CLK32KG**, from Linux's
`drivers/clk/clk-twl.c` (`clk32kg base 0x8C`, `VREG_GRP 0 / TRANS 1 / STATE 2`). That table is
explicitly the **TWL6032** one (`static const struct twl_clks_data twl6032_clks[] {
.name="clk32kg", .base=0x8C }`). In the QNX **TWL6030** table `0x8C` is **VAUX3** and `0x8F`
(Linux's `clk32kaudio`) is its 4th byte. So the measured difference (QNX `01/05/05`, Linux
`07/05/55`) is a **rail's group/state word (VAUX3), not a 32 kHz clock**: both have APP ON
(`0x05 & 0x0C`, `0x55 & 0x0C` both = 0x04); only group membership (sleep behaviour) differs. The
32 kHz lead stays closed; re-label that bullet **VAUX3**.

### 4.3 What the cfg produces (`pmic-twl6030.cfg`; `on app`=0x05, `on app con mod`=0x55,
`off app con mod`=0x00, `sleep=ams act=ams` TRANS=0x05, per the live captures)

`set_pd vpp off` → VPP PD bit cleared · `set_state vpp|vmmc|vusim off app con mod` → `9E←00`,
`9A←00`, `A6←00` · `set_trans vaux1|vaux2|vaux3|vusb sleep=ams act=ams` → `85←05`, `89←05`,
`8D←05`, `A1←05` · `set_groups vmmc|vpp|vusim` (empty) → `98/9C/A4←00` · `MACHINE Winchester:3-`
`set_groups regen1 app`, `set_groups regen2 app` → `AD←01`, `B0←01` · `set_state regen1|regen2 on
app` → `AF←05`, `B2←05` · `unmask_preq 1` → PREQ-mask write (register not localized).
**Nothing enables a rail Linux leaves off**: `vmmc/vpp/vusim` are turned *off* (Linux also off,
report 190 §7.3); `regen1/2` are only transiently on.

### 4.4 The action report 192 did not identify: REGEN1/REGEN2 off + detached
Resmgr strings (file 0x7700–0x7960): `Unable to determine board revision so setting regen1 and
regen2 GPIO controlled`, `Detected MBID=5+, regen1 and regen2 GPIO controlled`, `Failed to disable
regen1`, `Failed to detach regen1 from all groups`, `Failed to disable regen2`, `Failed to detach
regen2 from all groups`, `failed to map PRM registers`. On DVT3+ (MBID ≥ 5) it writes
`0xAD/0xAE/0xAF = 00/01/00` and `0xB0/0xB1/0xB2 = 00/01/00` — **exactly** the live QNX values in
report 192 §7 vs Linux's `07/05/55`. So the `0xB0`–`0xB2` "difference" is explained *by name*:
**QNX switches REGEN1/REGEN2 off and detaches them from all power groups at boot; Linux leaves both
ON in APP|CON|MOD.** (On DVT3+ they are routed to TWL GPIO2/GPIO15 per the cfg comment; report 187
classed them as display/panel rails.)

## 5. `setup-core-inactive` — the TI PRCM HAL, run first

`ifs/proc/boot/setup-core-inactive` (173 999 B, ELF DYN), invoked `setup-core-inactive full`
(`.script` @0xc0). Its rodata is a PRCM-HAL database: `prcm_hal_vc_configured`,
`prcm_hal_sr_init_executed`, `prcm_sr_voltages`, `prcm_sr_smart_reflex_enabled`,
`prcm_sysclk_value`, `prcm_clk_domain`, `prcm_power_domain`,
`dpll_{mpu,core,iva,ivamid,per,abe,tesla,unipro}_cfg_opp*` (25/50/80/100/119/132/166 MHz ×
12/26/192 MHz sysclk), `vp_{mpu,iva,core}_conf_{phoenix,rocket}`, `vc_cfg`, `volt_domains`,
`prcm_slimbus_*` — the OMAP4 **DPLL / VP / VC / SmartReflex / OPP** initialiser. It runs *before*
the I²C drivers, so it can only reach the PMIC through **PRM VP/VC**. It is the one pre-`startup.sh`
actor whose whole purpose is to set voltages; Linux's equivalent (`omap vc`/`vp` + `opp4xxx_data`)
runs at a different time from a different state.

## 6. Reconstructed order, and what precedes `wifi.sh`

```
RESET → startup-playbook : syspage; pad table (0x4A100000 / 0x4A31E000); GPIO table
        (gpio54 WL_EN, 53 WL_IRQ, 55 BT_EN + the 8 QNX-only outputs); WDT2;
        CM1/CM2/PRM/SCRM sweep incl. CM_L4PER_MMCSD5_CLKCTRL=2, CM_CAM_*, CM_L3INIT_HSMMC1/2;
        PRM VC config; AUXCLK1=0x10000; a few TWL writes (§2.4)
     → procnto → .script:
        setup-core-inactive full      (PRCM HAL: DPLL/VP/VC/SR/OPP)
        bmetrics, resource_seed, devc-seromap, slogger, pipe, trustzone-omap4, shm_lock_init
        i2c-omap35xx-omap4 ×3 → /dev/i2c0,1,3 ;  omap4430-wdtkick
        pmic_twl6030_cfg              (38 TWL writes, §3)   ← I²C-0 @400 kHz
        pmic-winchester-twl6030       (REGEN1/2 off + detach on DVT3+, §4.4)
        pmic-twl6030-ctl_script       (regulator cfg, §4.3)
        random, devb-mmcsd-winchester, spi-master, splash_script→winchester-bootreason,
        backlight_win, rpmb-omap4, nvram_resource_manager, sanityChecking
     → /base/scripts/startup.sh run level 0:
        bootinfo filesystem logging pps simconfig powerman lpddrmon thermal phyctrl battery
        bootreason splashscreen rtc dumper ves hid capsensor screen drmclock authman random
        coreservices perimeter audio pty dtminit NET certmgr vpn wpa usbmgr rimusb nfc
        net → net.sh → io-pkt-v6-hc → wifi.sh → mount -Tio-pkt -o nvram,… devnp-ti1283.so
     → run level 1 (AFTER the mount): … bluetooth … pmic (pmic-twl6030-ctl -p regen1,regen2,vaux3)
```
(`base/scripts/startup.sh` 2011-2014: `RUN_LEVEL_0_SERVICES=" bootinfo filesystem logging pps
simconfig powerman lpddrmon thermal phyctrl battery bootreason splashscreen rtc dumper ves hid
capsensor screen drmclock authman random coreservices perimeter audio pty dtminit net certmgr vpn
wpa usbmgr rimusb nfc"`, `RUN_LEVEL_1_SERVICES=" … pmic …"`; `net.sh` runs
`${BASEFS}/scripts/wifi.sh` for `*_Winchester_*`.)

**Skipped by a minimal image (`filesystem pps random pty net powerman phyctrl usbmgr rimusb`)**
before `wifi.sh`: `lpddrmon`, `thermal`, `battery`, `bootreason`, `splashscreen`, `rtc`, `dumper`,
`hid`, `capsensor` (i2c1), `screen`+`displayctl`, `drmclock`, `authman`, `coreservices`,
`perimeter`, `audio`, `dtminit`, `certmgr`, `vpn`, `wpa`, `nfc` (+ level-1 services if `/base` is
pruned). **No IFS-time item is skipped**: `local/work/ifs-shim-sealed.ifs` (the working oracle)
contains `proc/boot/pmic_twl6030_cfg`, `SETUP-CORE-INACTIVE`, `Configure power management chip`, so
those ran on the reference boot that brought `ti0` up (report 217 §2).

## 7. Verdict — candidates that could gate the WL1283, ranked

| rank | candidate | evidence | status |
|---|---|---|---|
| **1** | **`pmic_twl6030_cfg`'s 38 TWL writes** (§3.1), esp. `0xBD/0xBE`, `0xA1/0xA2/0xA3`, `0xE5`, `0xF5`, `0xFA`, `0x49:04/06/07` | `.script` 0x51a; code 0x8062bc1c–0x8062bea4; present in the oracle IFS | not devnp's writes, not pads/WL_EN, not a compared rail → **open**, testable with `i2cset` |
| **1b** | `startup-playbook`'s own TWL writes (`0x48:2C=7F, 80=00, 82=01, 25=07`; `0x4A:07=11/08=F0`; `0x49:E3` read) | §2.4; 0x80107ad4–0x80107dd8, 0x80100af4 | same class as 1, same test |
| **2** | `setup-core-inactive full` (VP/VC/DPLL/OPP) | `.script` 0xc0; §5 | open; sets VDD_CORE/MPU/IVA before any driver, via PRM VP/VC |
| **3** | REGEN1/REGEN2 off + detached by the resmgr | §4.4; matches live `00/01/00` | real QNX-only rail action; report 187 says display/panel → lower |
| **4** | `startup-playbook`'s CM2 module-clock sweep (`CM_L4PER_MMCSD5_CLKCTRL=2`, `CM_L4PER_CLKSTCTRL=2`, `CM_CAM_*`, `CM_L3INIT_HSMMC1/2=0x01000002`) | §2.1 | the MMCSD5 bit is duplicated by the driver (replayed, negative); the `0x01000000` HSMMC1/2 bits and CAM enables are **not** in the replay |
| **5** | `CM_SYS_CLKSEL=5`, `AUXCLK1=0x00010000`, `PMICSETUPTIME` | §2.2 | clock-tree side effects; AUXCLK1 already tested (reports 185/190) — negative |
| — | PRM L3INIT/L4PER, `RM_L4PER_MMCSD5_CONTEXT`, regulator groups from `startup-playbook` | §2.3 | **absent — closed** |
| — | pads, WL_EN, WL_IRQ, the compared TWL rails | 189/190/192/217 | closed |

**If there were no candidate I would say so; there is one class, and it is not in the WLAN driver**:
the I²C PMIC configuration the reference performs from its boot image (§3, §2.4, §4.4) plus the
PRCM-HAL voltage setup (§5). Everything else the boot path does to power, pads, GPIO and clock
before `wifi.sh` is either already matched on Linux or is duplicated by the driver program already
replayed (report 217).

**Cheapest decisive test** (no build, no `devnp` change): (1) boot the oracle, `pbi2c dump` the
TWL at the moment `ti0` comes up and diff `0x84–0x8F`, `0x98–0x9F`, `0xA1–0xA4`, `0xAD–0xB2`,
`0xBD/0xBE`, `0xE5`, `0xF5`, `0xFA` against Linux — shows how much of §3.1 survives the later
driver writes; (2) on Linux after the MMC5 probe fails, replay §3.1 with `i2cset -f -y 0 0x48 …`
(plus the §4.4 REGEN pair) and rescan `mmc4`.

## 8. What I did **not** establish

* The **meaning** of `0x21` written to `base+2` for VAUX1/2/3, REGEN1/2 and `0x98`–`0x9B` (it is
  *not* the APP-on value `0x05`); the TRM CFG_STATE field description is not machine-readable here
  (`hardware/twl6030.pdf` has no extractable text layer). The register used by
  `mask_preq`/`unmask_preq` is also unlocalized.
* A complete padconf/pad-table enumeration (tables are chosen at runtime, report 192 §3/§4a); only
  the mechanism and both bases were re-confirmed.
* Whether each of `startup-playbook`'s TWL accesses (§2.4) runs on every boot, and whether the
  pruned oracle `/base` still runs `pmic` (run level 1, after `wifi.sh` — irrelevant for the
  ordering, but it decides whether §4.3/§4.4 ran on the oracle).
