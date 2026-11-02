# 105 — Clean 6.15.11: the ~1.5 s global stall (2026-10-26, session 3)

Status: the MMC2 driver path is **not** the wall. There is a global,
code-independent CPU halt at **~1.40–1.53 s** that also kills a bare busy
spin. The QNX/TabletOS IFS boot script shows the pre-Linux setup Linux is
missing, and gives the next RE targets.

> **Session 4 corrections (`109-epoch-test-2026-10-27.md`).** There is **no
> hardware wall**: with the console on, the boot ends in
> `Kernel panic - not syncing: VFS: Unable to mount root fs` — there is no root
> filesystem, and `panic()` disables IRQs so the tick stops.  Each "halt" time
> below is simply where the boot was when `prepare_namespace()` failed (2250 ms
> silent, 2440 ms with the console).  The 1.38–1.53 s numbers are the **WDT2**
> (7.81 s after arming on this path, not 58.6 s) arriving before
> `prepare_namespace` — so the busy-spin "deaths" below were watchdog kills, and
> the "the wall is time-based, not device-based" reading was an artifact of
> `loglevel=0` hiding the panic.  The pre-kernel delay measurement stands (a
> 1000 ms delay moved the halt by 0 ms), as does the MPU-domain observation.
> Next step: an initramfs, not more RE.

All numbers below are kernel time (`local_clock`/printk timestamps), HZ=100.

---

## 0. TL;DR

- **The eMMC card init actually runs** once the console is silenced
  (`loglevel=0`): CMD0, CMD1 ×3 with real completion IRQs, then power-off,
  then the rescan completes, then the MMC runtime autosuspend *and* resume
  complete cleanly.
- **The stall is global and code-independent.** A 30 s busy-spin
  (`cpu_relax`, IRQs enabled, no device I/O, no idle) dies ~60 ms in. The
  periodic tick (TWD IRQ 29 / GPT IRQ 37) stops with it.
- **Not the cause** (each tested and ruled out): printk/console load,
  cpuidle/NO_HZ (`nohz=off idle=poll cpuidle.off=1`), SmartReflex/AVS
  (`CONFIG_POWER_AVS_OMAP=n`), MMC2 clocks/target idle (readback ideal),
  the RPM callback path (completes), the TWL6030 PMIC watchdog reload.
- The always-on WDT2 still resets the SoC at **~55 s**, so the SoC is alive
  while the CPU is halted.
- **Prime remaining suspects**: the pre-Linux PRCM/VC setup
  (`setup-core-inactive`), the secure world (`trustzone-omap4`), and CPU1
  (QNX's first action is `setup-core-inactive full`; Linux leaves CPU1 as
  the bootloader parked it).

---

## 1. New instrumentation (this session)

A printk-free event log in the DEBUG_LL ring page's breadcrumb area
(`+0x00..+0x7C`, reachable through the permanent non-cacheable alias
`0xFE800000`).  Implemented in `include/linux/pb_event.h`:

| offset | meaning |
|---|---|
| `+0x00` | heartbeat: `(0x3700 << 16) \| (jiffies & 0xffff)`, one per TWD/GPT timer IRQ |
| `+0x04` | scratch (last initcall fn pointer, or the async func, or the test counter) |
| `+0x08` | magic `0x50424556` ("PBEV"), set at GIC init |
| `+0x0C` | event counter |
| `+0x10` | 28 × u32, slot = counter % 28; each word = `(code16 << 16) \| (jiffies & 0xffff)` |

- Uses `__raw_writel`/`__raw_readl` (no barrier) so a stuck posted peripheral
  write cannot stall the log.  **A heartbeat that advances is proof the CPU
  is alive; one that stops is not proof it is dead.**
- `jiffies` starts at `INITIAL_JIFFIES` (`-300*HZ`); the decoder subtracts
  `0x8AD0` (low 16 bits) to get ms since boot.
- Reader/decoder: `analysis/pb_events.py` (`--raw FILE` to decode offline).
- Hooks: GIC enter/done for every peripheral IRQ (29/37 become the
  heartbeat), initcall entry (`0x8000`) + level start/end, MMC
  request/IRQ/set_ios/init_stream/rescan/attach/power_off, RPM
  callback/suspend, ti-sysc suspend/resume, prepare_namespace,
  wait_for_device_probe, async_synchronize_full.
- `loglevel=0` in the DTS was used for the decisive runs (only KERN_EMERG
  reaches the console, so the ring stays empty and the boot is much faster).

Artifacts in `playbook-live-spare-2026-10-26-clean/`:
`silent-{ring,events}.txt`, `poll-{ring,events}.txt`,
`fine-{ring,events}.txt`, `rpmcore-{ring,events}.txt`,
`gptspin-{ring,events}.txt`, `novc-{ring,events}.txt`,
`wdtkick2-{ring,events}.txt`.

---

## 2. What the event log proved

### 2.1 The MMC path is healthy

With `loglevel=0` the log shows (1350–1480 ms):

```
set_ios pm=2 → set_clock after CEN → set_ios done
request: CMD1 → GIC 118 → mmc_irq entry → STAT read → done   (×3)
mmc_attach_mmc: after send_op_cond (err)
mmc_power_off → set_ios pm=0 → done
mmc_rescan: after freq loop / after release_host / out
… 130 ms later …
hsmmc rpm_suspend: entry → after context_save → before spin_lock
   → lock acquired → before unlock → unlocked
rpm_callback: after cb → before lock_irq → lock_irq done
rpm_suspend: callback returned → after rpm_idle(parent)
   → before/after suspend_suppliers → out
```

Every step completes.  The CPU halts immediately after, with the tick
stopping at the same moment.  This is **not** an MMC driver bug.

### 2.2 The console was changing which code hit the wall

The wall is time-based, so the console's per-character ring writes simply
changed what was executing when the wall arrived.  Silencing the console let
the boot reach the eMMC card init; restoring it moved the last line back to
`mmc_of_parse`/`set_ios`.

### 2.3 The spin test (decisive)

A `late_initcall_sync` ran `while (time_before(jiffies, end)) cpu_relax();`
for 30 s with interrupts enabled and no device access.  It started at
1350 ms and the heartbeat stopped at **1410 ms** — 60 ms into a pure CPU
loop.  Re-run without AVS: spin start 1250 ms, death 1500 ms.  The wall is
therefore not caused by any driver or by the MMC.

### 2.4 MMC2 target is provably awake

DT (kept): `&mmc2_target { ti,no-idle; ti,sysc-sidle = <SYSC_IDLE_NO>;
ti,sysc-midle = <SYSC_IDLE_NO>; };` plus a `DL_FLAG_PM_RUNTIME` device link
from the MMC child to the ti-sysc parent.

Readback at probe (PBX lines):
`fck=96000000 pclk=96000000 mm=2 idlest=0 stby=0 sel=1 clkst=2`,
`sysc=00000014` (midle=1 NO, sidle=1 NO).  MODULEMODE=ENABLED, IDLEST=0,
STBYST=0, CLKTRCTRL=SW_WKUP (2 = SW_WKUP on this CM instance), functional
clock 96 MHz.  The simple "MMC2 gated/idle" theory is dead.

---

## 3. QNX/TabletOS IFS: the pre-Linux setup Linux is missing

From `playbook-dev/dumped4869ifs/proc/boot/.script` (raw boot script):

```
SETUP-CORE-INACTIVE
setup-core-inactive full
resource_seed dma=0,31
devc-seromap … 0x48020000
slogger, pipe
starting trustzone-omap4
trustzone-omap4 -U99:102
starting I2C driver...
i2c-omap35xx-omap4 -p 0x48070000 …
i2c-omap35xx-omap4 -p 0x48072000 …
i2c-omap35xx-omap4 -p 0x48350000 …
starting Watchdog
omap4430-wdtkick -t 15000 -P -U 99:99
Configure power management chip
pmic_twl6030_cfg
Start PMIC driver...
pmic-winchester-twl6030 -U 99:99
sh /proc/boot/pmic-twl6030-ctl_script
Starting MMC/SD driver...
devb-mmcsd-winchester …
```

### 3.1 `setup-core-inactive` (runs first)

Strings from the binary:

```
FULL core inactive setup
PARTIAL core inactive setup
cpupm_init: prcm_vc_global_init(0x%x,1) returned %d
cpupm_init: prcm_configure_volcon() returned %d
cpupm_init: prcm_device_init() returned %d
cpupm_init: prcm_reinit_dmnstates() returned %d
cpupm_init: prcm_sr_interface_config_domain() returned %d
cpupm_init: prcm_vc_bypass_write 0x3a returned %d
cpupm_init: prcm_vc_bypass_write 0x40 returned %d
Core voltage is 1225
Core voltage is 1250
```

This is the board's real PRCM/VC/VP setup: VC **bypass** writes of `0x3A`
and `0x40`, and core voltages 1225/1250 mV.  It is the reference for the
TWL6030 voltage-table task in report §4a, and a strong candidate for the
pre-Linux state the kernel now lacks.

### 3.2 `omap4430-wdtkick` (two watchdogs)

Usage text (`QNX_usage` section):

```
-a addr   WDT register physical base
-t time   Set kick time
-e        Terminate the Watchdog Timer Module
-P        kick TWL6030 PMIC primary watchdog (in addition to 4430 WDT)
-d        TWL6030 I2C device name (default: /dev/i2c0)
-T        TWL6030 watchdog timeout (default: 127s)
```

- QNX runs `-t 15000 -P` → kicks the OMAP WDT2 **and** the TWL6030 PMIC
  primary watchdog, TWL timeout default 127 s.
- OMAP WDT2 kick (disassembly): read `TGR` (0x30), invert, write back
  (the TRM: writing a different value reloads the counter).
- TWL6030 kick (disassembly): i2c write to 0x48, register **0x2C = timeout
  in seconds**.  In the kernel map that is `TWL_MODULE_PM_MASTER`
  (`BASEADD_PM_MASTER = 0x1F`) register `0x0D`.
- Live QNX read of WDT2 (driver map: base+0x10 SYS_CONFIG, 0x14 STATUS,
  0x24 CNTRL, 0x28 CRR, 0x2C LDR, 0x30 TGR):

```
4a314010: 00000020   4a314014: 00000001   4a314024: 00000000
4a314028: ffe5915e (CRR)   4a31402c: ffe2b400 (LDR ≈ 58.6 s)
4a314030: ffffd47b (TGR)   4a314034: 00000000
```

So the OMAP WDT2 is the ~58 s recovery watchdog (matches the observed
~55 s reset); it is not the 1.4 s killer.

### 3.3 PMIC config

`pmic-twl6030.cfg` turns off unused LDOs (`vmmc`, `vpp`, `vusim`), sets
`vaux1/2/3`, `vusb` transitions, keeps `regen1/2` on, unmasks power
transitions.  No watchdog entry (the kicker handles it).

---

## 4. Experiment matrix

| change | result |
|---|---|
| `loglevel=0` | boot reaches eMMC CMD1; halt still ~1.42–1.50 s |
| `nohz=off idle=poll cpuidle.off=1` | halt ~1.49 s (not idle/NO_HZ) |
| `CONFIG_POWER_AVS_OMAP(_CLASS3)=n` | halt ~1.50 s (not SmartReflex) |
| VC/VP `vp_param/vc_param = NULL` | **different, early** halt at 0.37 s inside `__omap2_common_pm_late_init` (confounded; PM init depends on the voltdm) |
| TWL watchdog reload at TWL probe (`reg 0x2C = 127`) | wall unchanged (~1.38 s). Needs confirmation that the write landed (the code gated the write on a successful read); try unconditional + repeated kicks |
| 30 s busy-spin, IRQs on | dies ~60 ms in (code-independent) |

Timing spread across runs: 1.38, 1.39, 1.41, 1.48, 1.49, 1.50, 1.52, 1.53 s.
The always-on WDT2 reset was ~55 s in every cycle (measured from the
restore-flasher timestamps), i.e. the unit does not reset-loop.

---

## 5. Next steps (in order)

The three RE reports were produced at the end of this session:
`106-re-setup-core-inactive.md`, `107-re-trustzone-omap4.md`,
`108-re-wdtkick-pmic.md` (condensed in `NEXT-SESSION-BOOTSTRAP-2026-10-26.md`
§7).

1. **Run the epoch test** (report 108 §6.3): a fixed delay D in the
   chainloader/decompressor before any kernel timer; boot D=0 twice (jitter)
   and D≈3 s; compare the halt time in kernel time.  `t_halt(D) ≈ t_halt(0)
   − D` ⇒ hardware epoch; unchanged ⇒ kernel entry.  This one experiment
   decides the family.
2. **Hardware epoch** ⇒ always-on/secure timers.  `WDTIMER1 0x4A318000` is
   the report-107 candidate (it is also Linux's always-on clockevent);
   `WDTIMER3 0x40130000` is in the disabled ABE.  WDT2 and the TWL PMIC
   watchdog are cleared (report 108: the PMIC expiry would drop the rails,
   but the 55 s WDT2 reset always happens).
3. **Kernel entry** ⇒ replicate `setup-core-inactive`'s PRCM/VC/VP state
   (report 106: `PRM_VC_VAL_BYPASS 0x4A307BA0` ← `0x013A5612` /
   `0x01405512`, `PM_MPU_PWRSTST`, `CM_SDMA_STATICDEP`, CLKSTCTRL) before
   the wall; check the MPU power domain/clkdm.
4. **CPU1.**  QNX's first action is `setup-core-inactive full`; Linux
   leaves CPU1 as the bootloader parked it (`maxcpus=1` is not even a valid
   param; NR_CPUS=1).  Try forcing all GIC SPIs to CPU0
   (`GICD_ITARGETSR`), and/or a safe CPU1 park, before the wall.
5. Implement the latent-bug fixes from report 108 §6.2: kick the TWL
   `0x48/0x2C=0x7F` every 30 s and WDT2 `TGR` every 15 s.
6. Only then continue the card init (the rescan already issues CMD1; the
   card does not answer yet), then initcalls, display/DSS, USB.

---

## 6. Tree / device state and gotchas

- Instrumentation in: `include/linux/pb_event.h`, `drivers/irqchip/irq-gic.c`,
  `init/main.c`, `init/do_mounts.c`, `kernel/async.c`, `drivers/base/dd.c`,
  `drivers/base/power/runtime.c`, `drivers/bus/ti-sysc.c`,
  `drivers/mmc/host/omap_hsmmc.c`, `drivers/mmc/core/core.c`,
  `drivers/mmc/core/mmc.c`, `drivers/mfd/twl-core.c`.
- The 30 s spin test has been removed; the DTS bootargs are back to the
  documented `earlyprintk keep_bootcon ignore_loglevel maxcpus=1
  clk_ignore_unused panic=10`; `CONFIG_MMC_BLOCK=y` restored.
  Latest image on disk: `analysis/chainloader/kernel.img` CRC `0x0BEB602A`
  (**not** on media).
- Device: TabletOS up, sealed IFS `sha256 c72b0530…`, `BOOTCTR_FCTRY = 50`.
- `drivers/base/dd.c:619` prints `PBR: dev <- driver` **before** the probe.
  A PBR line with no follow-up from the driver means the probe hung.
- The DRAM event area is readable only after TabletOS is back (the door);
  the event ring is circular (last 28 events).
- Printk ring (`__log_buf`) is cache-stale after a wedge; the DRAM ring and
  the event log are the only authoritative captures.
- Recovery loop used this session (works every time): flash the image,
  reboot the unit, `force_reset_loader.py` once to boot the kernel, start
  the sealed-IFS restore with `--delay 30`, and after it exits run
  `force_reset_loader.py` once more (0x80EF first) to boot TabletOS.
