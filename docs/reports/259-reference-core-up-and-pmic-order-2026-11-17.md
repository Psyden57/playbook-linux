# 259 — how the reference brings up both cores and the PMIC, from the IFS startup path

Date: 2026-11-17 (session 64, Linux-era 58).  Static RE only; no bench writes.

**Question (operator, session 64):** does the reference enable both cores right
at boot in the IFS, one after the other, and does it set up the PMIC first?

## 0. Headline

* **The reference brings both cores up at boot, before any IFS user program
  runs.**  `procnto-smp-instr` is the SMP kernel; the secondary-CPU start path
  (`SCU_CONFIG`, `cpu_startnext`, `send_ipi`, `CPU %d start failure`) is in
  **`startup-playbook`**, which runs *before* procnto and publishes the callback
  in the syspage.  There is no "start cpu1" command in the IFS `.script` — procnto
  starts it during kernel init.
* **The PMIC is NOT configured first.**  `startup-playbook` sets voltage through
  the OMAP **PRM voltage controller** (VC bypass, I²C-1 driver for the TWL), then
  procnto starts and the IFS `.script` runs **`setup-core-inactive full` as its
  very first program** — the TI PRCM-HAL (`cpupm_init`: VC/VP/DPLL/SmartReflex/
  OPP), still before any I²C driver.  Only *after* three `i2c-omap35xx` drivers
  and the watchdog does `.script` run `pmic_twl6030_cfg` (38 TWL writes), the
  `pmic-winchester-twl6030` resmgr and `pmic-twl6030-ctl_script`.
* **`setup-core-inactive` writes VSEL `0x3A`** via `prcm_vc_bypass_write` — the
  same 1.35 V MPU OPP our fast chainloader commands (`PRM_VC_VAL_BYPASS =
  0x003A5512`, DECISIONS §91).  It also writes `0x40` (a second bypass value,
  domain not established).  This is the first direct reference-side confirmation
  of the chainloader's voltage choice.

## 1. The IFS startup script (decoded, in order)

`local/work/1917/ifs/proc/boot/.script` (5664 B, a QNX `mkifs` binary script)
decodes to this program sequence.  Verbatim ordering, with the PMIC-relevant
steps marked:

```
startup-playbook            (pre-procnto: syspage, pads, GPIO, WDT2, CM/PRM/SCRM,
                             PRM VC config, AUXCLK1, a few TWL writes)   [reports 192/219]
procnto-smp-instr           (SMP kernel — starts both cores during its init)
-- .script begins --
1  setup-core-inactive full          <-- PRCM HAL: VP/VC/DPLL/SR/OPP, VSEL 0x3A/0x40
2  bmetrics / bmetrics_cli start / service_start ifs / resource_seed
3  devc-seromap, slogger, pipe
4  trustzone-omap4, shm_lock_init
5  i2c-omap35xx-omap4 x3  -> /dev/i2c0, /dev/i2c1, /dev/i2c3
6  omap4430-wdtkick -t 15000 -P
7  pmic_twl6030_cfg                 <-- 38 TWL writes over I2C-0 @400 kHz  [report 219 §3]
8  (Take touch pad out of reset)
9  pmic-winchester-twl6030          <-- regulator resmgr (REGEN1/2 off on DVT3+) [219 §4.4]
10 sh /proc/boot/pmic-twl6030-ctl_script   <-- regulator states [219 §4.3]
11 random, devb-mmcsd-winchester, spi-master
12 sh /proc/boot/splash_script -> winchester-bootreason, backlight_win
13 rpmb-omap4, nvram_resource_manager, sanityChecking
14 ksh /base/scripts/startup.sh      (run levels 0/1: … powerman … pmic …)
```

So the answer to "does it first set up the PMIC in a specific way?" is **yes, but
in two stages**: a *voltage-controller/PRCM* stage before procnto and before the
I²C drivers (`startup-playbook` PRM VC config, then `setup-core-inactive full`
with `prcm_vc_bypass_write 0x3a`), and a *TWL register* stage after the I²C
drivers (`pmic_twl6030_cfg` and the ctl script).  Core-up sits between them:
procnto starts CPU1 before `.script` user programs execute.

## 2. `startup-playbook` — the secondary-CPU machinery

Target: `local/work/s74/startup-playbook.bin` (278536 B), raw ARM at
`0x80100000`; `startup_vaddr = 0x80100cc0` (report 192 §1).  REA's deep providers
were unavailable this session, so this used the shipped linear disassembly
`local/work/s74/startup-playbook.dis`, `strings`, and reports 192/219.

Strings confirming the SMP role: `SCU_CONFIG = %x, %d cpus`,
`CPU %d start failure`, `cpu_startnext: cpu%d -> %x`, `send_ipi:%l cpu:%l`,
`cpu_intr_base:%l, cpu_intr_stride:%d, flags:%w`, `Header size=0x%x, Total Size=0x%x,
#Cpu=%d, Type=%d`, `Fatal Error: MPU DPLL would not lock.`,
`error: unable to set voltage. i2c_data=0x%x, err=0x%x`.

Decoded code (corroborating report 219 §2.2/§2.4):

* **SMC wrapper** `0x80107804`: `dsb sy; smc #0` with `r12` = service, `r0/r1`
  = args — the same secure-monitor ABI our kernel uses.  Nine call sites.
  Services seen include `0x102` (PL310/L2 cache control), `0x104`, `0x105`,
  `0x108`, all within the ROM's documented `0x100`–`0x113` range (report 62 §1).
* **Voltage path**: `FUN_80107748`-adjacent code (report 192 §6) touches
  `PRM_VC_ERRST 0x4A307BF8` and `PRM_VC_VAL_BYPASS 0x4A307BA0`; the I²C-1 driver
  (`i2c_omap_init/reset/set_reg_addr_len/write_reg/write`, base `0x48070000`)
  is present.  This is the "unable to set voltage" string's path; report 192
  established it is PRM VC, not direct TWL register writes.
* **SCU/GIC/WUGEN setup** at `0x801024xx`–`0x801027ac`:
  `movw/movt rX,#0x2000,#0x4824` → `0x48242000` (GIC CPU interface); writes
  `0x4824277C = 0xFFFF` (report 219 §2.2); loops zeroing `WKG_ENB_A_0/1`
  `0x48281010`/`0x48281410` (report 219 §2.2); reads SCU/GIC state.
  **No direct write to `AUX_CORE_BOOT_0/1` (`0x48281800/4`) and no SMC service
  `0x103`/`0x107` (the ROM's CPU1-wake services, report 62 §2) was found in
  `startup-playbook`.**  So the actual CPU1 release is done by procnto (or the
  ROM wake path), using the `cpu_startnext` callback startup publishes —
  *not* by a write in startup itself.  Confidence: moderate (bounded string/
  immediate search, no decompiler this session).
* Clock/PRM sweep and `CM_SYS_CLKSEL=5`, `PRM_VC_VAL_BYPASS`,
  `PRM_VC_CFG_I2C_MODE/CLK`, `SCRM_AUXCLK1=0x10000`, pad/GPIO tables, WDT2:
  report 219 §2.2, re-confirmed at the addresses quoted there.

## 3. `setup-core-inactive` — the PRCM HAL, first in `.script`

Target: `local/proprietary/device-binaries/setup-core-inactive` (173116 B, ELF
DYN, stripped; a 173999-B copy is in the 1917 IFS).  Invoked as
`setup-core-inactive full` (`.script` item 1).  Strings:

```
cpupm_init: prcm_vc_global_init(0x%x,1) returned %d
cpupm_init: prcm_configure_volcon() returned %d
cpupm_init: prcm_device_init() returned %d
cpupm_init: prcm_reinit_dmnstates() returned %d
cpupm_init: prcm_sr_interface_config_domain() returned %d
cpupm_init: prcm_vc_bypass_write 0x3a returned %d
cpupm_init: prcm_vc_bypass_write 0x40 returned %d
```

plus the PRCM-HAL database (`prcm_hal_vc_configured`, `prcm_sr_voltages`,
`dpll_{mpu,core,iva,…}_cfg_opp*`, `vp_{mpu,iva,core}_conf_*`, `vc_cfg`,
`volt_domains`) already catalogued in report 219 §5.  No CPU-start strings.
Its whole purpose is **voltage/clock/OPP setup via PRM VC/VP** before the I²C
drivers exist; the `0x3a` bypass write is the reference's 1.35 V MPU OPP,
matching our chainloader (report 257 §1, DECISIONS §91).

## 4. What this means for our dual-core wedge

1. **Our ordering is already close to the reference's.**  The fast chainloader
   commands VSEL `0x3A` and programs `DPLL_MPU = 1008 MHz` at entry, before the
   kernel — the reference does the same class of VC/DPLL setup before procnto
   (`startup-playbook`) and again first in `.script` (`setup-core-inactive`).
   There is no evidence the reference waits for an I²C PMIC driver before
   putting both cores at 1 GHz.
2. **CPU1 start is a kernel/ROM action, not an IFS-script action.**  A fix for
   the wedge (report 258: IPI sent/serviced, hang downstream) should therefore
   be sought in our kernel's `flush_old_exec`/`mmput` path, not in adding a
   startup delay — the reference has no such delay in the IFS script.
3. **Open, not established:** the exact `cpu_startnext` body (does it write
   AUX_CORE_BOOT directly, or call ROM service `0x103`/`0x107` from procnto?),
   the domain of the `0x40` bypass write, and whether `setup-core-inactive`
   re-locks the MPU DPLL after our chainloader already set it.  A decompiler run
   (Ghidra 12.1.4 per DEVELOPMENT §7a, currently unavailable through REA) would
   settle these.

## 5. Evidence / limits

* IFS `.script` and binaries:
  `local/work/1917/ifs/proc/boot/.script`,
  `local/work/s74/startup-playbook.bin` + `.dis`,
  `local/proprietary/device-binaries/setup-core-inactive`,
  `local/work/1917/ifs/proc/boot/procnto-smp-instr`.
* Prior decode reused: reports 192 §6, 219 §2.2/§2.4/§5/§6, 62 §1–§2, 257 §1,
  DECISIONS §91, report 258 (the wedge).
* REA native providers (Ghidra/IDA/Hopper/Kuna) were **unavailable** in this
  session; conclusions are from shipped disassembly, strings, and prior
  decompilations, not fresh pseudocode.  Negative searches (no AUX_CORE_BOOT
  write, no SMC 0x103/0x107 in `startup-playbook`) are bounded to those
  artifacts and do not prove absence from procnto or the ROM wake path.
