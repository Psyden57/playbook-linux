# 266 — every task was pinned to CPU 0 (PID 1's mask never widened); cpufreq-dt works but has no supply

Date: 2026-11-23 (session 67 continued, Linux-era 61).  Bench.

## 0. Headline

* **The second core was doing no userspace work at all.**  On the
  session-66/67 image (`0x3547D613`, `#109 SMP`) `online = 0-1` and
  `SMP: Brought up 1 node, 2 CPUs`, but **PID 1's affinity mask was `{0}`**, so
  every userspace process (init, OpenRC, dropbear, catacomb, the console shell)
  inherited `{0}` and `nproc` reported **1**.  Only kernel threads
  (`kthreadd`, `kworker`, `ksoftirqd`, per-CPU kthreads) had `0-1`.  This is a
  deterministic, reproducible state: **3/3 fresh boots** showed
  `taskset -p 1` → `1`, `nproc` → `1`.
* **The fix is a one-line inittab entry**, and it is verified: after
  `::sysinit:/usr/sbin/taskset -p 3 1` runs as PID 1's own child, a respawned
  shell has `Cpus_allowed_list: 0-1`, `nproc` = **2**, and a two-thread busy
  loop spreads load evenly (`cpu0` 2643 / `cpu1` 2622 user ticks).  Reverted →
  reproduced the pin; restored → reproduced the fix.  **2 boots each way.**
* **This supersedes the "no `cpufreq` policy exists" line in HANDOFF/STATE.**
  `cpufreq-dt` is bound (`scaling_driver = cpufreq-dt`), all four OPPs are
  present (`300000 600000 800000 1008000`), and the governor (`ondemand`) *does*
  scale: setting `scaling_max_freq = 300000` drops `scaling_cur_freq` to 300000
  and restoring it returns 1008000.  What is **missing** is the CPU supply: the
  OPP debug dirs have **no `supply-*` entries**, so no voltage is programmed per
  OPP.
* **The `twl6030_uv_to_vsel: OUT OF RANGE` lines are startup-time VC
  initialisation, not a runtime cpufreq fault.**  `omap4_mpu_vc_data.on =
  1375000 uV` (and `OMAP4_VP_MPU_VLIMITTO_VDDMAX = 1410000`) exceed what
  `twl6030_uv_to_vsel()` can encode with this TWL6030's `SMPS_OFFSET`
  (`twl6030_vsel_to_uv(0x39)` = 1316660); the function logs and falls back to
  VSEL `0x3A` (1.35 V).  So the rail is set to 1.35 V — matching the
  chainloader's own 1.35 V command (`PRM_VC_VAL_BYPASS` value `0x003A5512`).
  The frequency is therefore correct (1008 MHz) with 1.35 V, which is the
  vendor's OPP-Nitrogen point; only the *table-driven* voltage path is unbuilt.

## 1. The CPU-affinity defect

### What was measured

| check | before the fix | after the fix |
|---|---|---|
| `taskset -p 1` | `1` (`{0}`) | `3` (`{0,1}`) |
| `nproc` | 1 | 2 |
| `cat /sys/devices/system/cpu/online` | 0-1 | 0-1 |
| console shell `Cpus_allowed_list` | 0 | 0-1 |
| `kworker/*` `Cpus_allowed_list` | 0-1 | 0-1 |
| two-thread busy loop, `/proc/stat` user ticks | n/a (single CPU) | `cpu0 2643` / `cpu1 2622` |

`online` and `present`/`possible` were `0-1` in both states, and
`/sys/devices/system/cpu/cpu1/online` was `1`.  `taskset -c 1 <work>` always
worked, so CPU 1 was fully functional — it simply never received userspace
tasks.

### The mechanism

`PID 1` is `kernel_init`, created by `rest_init()` and deliberately pinned to
the boot CPU until the scheduler is up:

```c
/* init/main.c, rest_init(): upstream */
tsk->flags |= PF_NO_SETAFFINITY;
set_cpus_allowed_ptr(tsk, cpumask_of(smp_processor_id()));
```

`sched_init_smp()` is then supposed to widen it:

```c
/* kernel/sched/core.c, sched_init_smp(): upstream */
if (set_cpus_allowed_ptr(current, housekeeping_cpumask(HK_TYPE_DOMAIN)) < 0)
	BUG();
current->flags &= ~PF_NO_SETAFFINITY;
```

`housekeeping_cpumask()` returns `cpu_possible_mask` unless
`housekeeping_overridden` is enabled (i.e. unless `isolcpus=`/`nohz_full=` set
it).  There is **no** such bootarg here (`/proc/cmdline` has neither), and
`local/kernel-tree/kernel/sched/isolation.c` and `.../sched/core.c` are **byte-identical to
pristine 7.3.3**.  So in principle PID 1 should end up `{0,1}`.

Empirically it does not: PID 1 is `{0}` after every boot until something
explicitly widens it, and children inherit that.  Two candidate readings:

1. **The widening does not take effect** (the set is rejected/no-op, or
   `cpu_active_mask` is still `{0}` when it runs, making
   `cpumask_any_and_distribute(cpu_valid_mask, new_mask)` resolve to CPU 0 and
   `do_set_cpus_allowed()` store an effective `{0}`), or
2. the widening happens and something later **re-narrows** it.

The decisive measurements are consistent with (1): `PF_NO_SETAFFINITY` is
cleared after the call, so a failure would be silent, and widening PID 1 by
hand immediately fixes every descendant — i.e. nothing re-narrows it later.
`cpu_active_mask` is only extended by `sched_cpu_activate()` via the
`CPUHP_AP_ACTIVE` hotplug state, which runs during secondary bring-up; if the
`smp_init()` → `sched_init_smp()` ordering completes before CPU 1 reaches that
state, the widening is clamped to the active set.

This is not fully pinned down statically — it is a boot-ordering question and
the answer would come from one instrumented boot printing
`cpu_active_mask`/`cpu_online_mask` at the top of `sched_init_smp()` and PID 1's
`cpus_mask` right after the call (the unexported `PBCPU`/`PBDBG` markers are
already in the working tree).

### Why it mattered

Report 263 §4b measured "two cores in parallel: 0.38 GB/s aggregate — no
scaling" and 376 s soaks "on two cores".  The cores were *online* and taking
interrupts, but **no userspace thread ever ran on CPU 1**, so every userspace
benchmark, the compositor, and the compiler-style workloads were effectively
single-core.  The UI-performance and memory-throughput items should be
re-measured now that both cores actually take userspace load.

### The fix (device-side, durable, no kernel change)

`/etc/inittab` (on the hd3 root), one line **before** the OpenRC entries so it
runs while PID 1 is still the affinity holder:

```
::sysinit:/usr/sbin/taskset -p 3 1
::sysinit:/sbin/openrc sysinit
::sysinit:/sbin/openrc boot
```

`busybox init` runs each `::sysinit:` line as its own child of PID 1; that child
has the same UID (root) and can widen its parent's mask, which every later
process then inherits.  Backups on the unit: `/etc/inittab.session67-orig`
(the OpenRC default) and `/etc/inittab.fixed`.  This is a bench-root change on
hd3, **not** a kernel change, so the region-0 image and its CRC are unaffected.

**A kernel-side fix is the proper one** (make `sched_init_smp()`'s widening
stick) and should replace the inittab line once understood; the inittab line is
the verified workaround until then.

## 2. cpufreq / OPP — corrected status

| item | measured |
|---|---|
| driver | `cpufreq-dt` (`/sys/.../cpufreq/scaling_driver`) |
| OPPs | `300000 600000 800000 1008000` (from `omap443x.dtsi`, unchanged upstream values) |
| governor | `ondemand`; `performance`/`powersave`/`userspace`/`conservative` also available |
| scaling | **works**: max 300000 → cur 300000; restore → 1008000 |
| OPP supply | **absent** — no `supply-*` in `/sys/kernel/debug/opp/cpu0/opp:*/` |
| MPU rail | set to VSEL `0x3A` = **1.35 V** (see §3) |
| SmartReflex MPU | present (`smartreflex_mpu`), `autocomp = 0` — Class3 init only, not running |
| thermal | `thermal_zone0` = 67.0 °C under the bench load |

So the "MPU OPP" item is **not** "there is no cpufreq policy" (that was true
before the ARMv7/SMP image; it is stale now).  What remains is:
1. attach a CPU supply so the OPP table's voltages are actually applied, and
2. decide the voltage question in §3 (1.35 V at 1008 MHz is what the
   chainloader already commands and what the part runs at today).

## 3. The `OUT OF RANGE` warning, explained

`arch/arm/mach-omap2/omap_twl.c`:

```c
if (uv > twl6030_vsel_to_uv(0x39)) {          /* 1316660 uV with this efuse */
	if (uv == 1350000) return 0x3A;       /* hardcoded 1.35 V */
	pr_err("... OUT OF RANGE! non mapped vsel for %ld Vs max %ld\n", ...);
	return 0x3A;                          /* clamps to 1.35 V anyway */
}
```

`omap4_mpu_vc_data.on` is **1375000** (the OPP100 voltage from
`omap443x.dtsi`) and `OMAP4_VP_MPU_VLIMITTO_VDDMAX` is **1410000**; both exceed
1316660, which is why the line prints six times for 1375000 and once for
1410000.  The value actually programmed is VSEL `0x3A` = 1.35 V, i.e. the
function's documented fallback and exactly the chainloader's
`PRM_VC_VAL_BYPASS = 0x003A5512` (`vsel<<16 | reg<<8 | slave`, `vsel = 0x3A`,
`reg = 0x55` = `OMAP4_VDD_MPU_SR_VOLT_REG`, slave `0x12`).  So:

* the **frequency and voltage in force are consistent** (1008 MHz @ 1.35 V);
* the warning is cosmetic on this TWL6030 revision (`SMPS_OFFSET` gives a
  0.607–1.317 V standard range; only the hardcoded 1.35 V is representable
  above it);
* a "fix" must decide between (a) leaving 1.35 V and silencing/skipping the
  out-of-range OPP entry, or (b) sourcing a higher range, which this
  TWL6030 revision cannot express.  **1.2 GHz already crashes the unit
  (report 257), so option (a) is the sane one.**

## 4. Where this leaves the item

* **DONE:** the CPU-affinity defect (both cores now take userspace work);
  `cpufreq-dt` scaling confirmed working; the `OUT OF RANGE` message explained.
* **OPEN (next):** attach the CPU supply so OPP voltages apply (or confirm
  1.35 V is intended for all OPPs on this board); a kernel-side fix for the
  PID-1 affinity instead of the inittab workaround; re-measure the memory
  throughput and UI latency with two real cores; SmartReflex (`autocomp=0`) is
  the reference's next lever (report 219 §5, report 257 §6).
* **Also still open from session 67:** the intermittent VUSB teardown
  (trap 337) and the hd3 rebake.

## 5. Evidence

* `local/work/session67-cpuaffinity.txt` (mask/nproc/`/proc/stat` before and
  after), `local/work/session67-state.txt` (cpufreq driver/frequencies/OPP,
  inittab files on the unit).
* Bench transcripts via `local/work/pbcmd.py` (ACM console; SSH/RNDIS did not
  answer on the Nura root).
* Device files: `/etc/inittab`, `/etc/inittab.session67-orig`,
  `/etc/inittab.fixed` on hd3.
* Reproducibility: 3/3 fresh boots pinned; 2 boots with the inittab fix →
  `nproc` 2, `cpu0`/`cpu1` load balanced; 1 revert boot → pinned again;
  1 restore boot → fixed again.
