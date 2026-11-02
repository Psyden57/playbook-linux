# Session 9 (absolute 9, Linux-era "session 3") — MMC2 held awake, a printk-free event log, and the "1.5 s wall" as it looked before it was explained

> Numbering: absolute **9** of 12 = Linux-era **"session 3"** in the reports.  This
> session wrote `105-global-1.5s-stall-2026-10-26.md`, the three RE reports
> `106`/`107`/`108`, `HANDOFF-2026-10-26-global-stall.md`, and the first full
> version of `NEXT-SESSION-BOOTSTRAP-2026-10-26.md`.  Sessions 10–12 are newer;
> their notes and `109` correct this session's central conclusion, so read the
> *Superseded* section before reusing anything below.

Date: 2026-10-26 (device logs are ART = UTC−3).
Unit state at the end: TabletOS from the sealed IFS (sha256 `c72b0530…`, restored
and verified), door up, `BOOTCTR_FCTRY` **read** `0x32`; media restored; last test
image on disk `chainloader/kernel.img` CRC `0x0BEB602A`, not on media.  The
`0x32` is a reading only — later sessions showed the device-side top-up was
writing zero and being refused (see `session-10.md`, *Superseded*, and the
`116` addenda).

## Goal of this session

Start from `104-clean-kernel-capture-v2-2026-10-26.md` and
`HANDOFF-2026-10-26-clean-kernel-v2.md`.  The state they described: a clean
6.15.11 booted from the CRC path, capture v2 reliable, a standstill at ~1.51 s
that was believed to be **MMC2 eMMC card init** (host probe, power-up, CMD0 and
`mmc_add_host` all worked; register accesses in `mmc_rescan` appeared to stall).
The ordered next steps were: (1) hold the MMC2 target/parent clocked
(`ti,no-idle`, clkdm deny-idle, child runtime-PM parent hold), (2) verify the
functional clock, (3) instrument CMD1/CMD2/CMD3, (4) then initcalls, display/DSS,
USB; use QNX/TabletOS RE for the TWL6030 voltage tables, USB PHY/gadget and
display panel/backlight.

Guardrails, restated by the operator and kept: **MMC probe read-only — no mounts,
no `dd`; RPMB off** (never open the RPMB chardev).  Single owner of the USB link
at all times.

"Done" was to be: the rescan completes, or the real blocker is named.  What
happened: the rescan *did* complete once the console was silenced, the blocker
was misnamed, and the durable output is the instrumentation, the QNX pre-Linux
picture, and the measurement discipline that let session 10 solve it in one run.

## Confirmed (measured, read in a file, or seen on the bench)

### 1. MMC2's interconnect target was awake — and measurably so

* The DT change was kept: `&mmc2_target { ti,no-idle; ti,sysc-sidle =
  <SYSC_IDLE_NO>; ti,sysc-midle = <SYSC_IDLE_NO>; };`
  (`kernel/source/omap4-winchester.dts:223-240`), plus a `DL_FLAG_PM_RUNTIME |
  DL_FLAG_AUTOREMOVE_CONSUMER` link from the MMC child to its parent
  (`kernel/clean-kernel-6.15.11-changes.patch:2717-2720`;
  `local/kernel-tree/drivers/mmc/host/omap_hsmmc.c`, `PBM:` print).
* Readback at probe, quoted verbatim from a console-on boot
  (`docs/captures/playbook-live-spare-2026-10-26-clean-mmc2-noidle-ring2.txt`):

  ```
  [1.458160] PBM: 480b4000.mmc parent=480b4000.target-module link=1
  [1.458648] PBX: mmc1 probe-pm fck=96000000 pclk=96000000 mm=2 idlest=0 stby=0 sel=1 clkst=2
  [1.458923] PBX: mmc1 probe-pm sysc=00000014 midle=1 sidle=1
  [1.461578] PBX: mmc1 ios-in  fck=96000000 pclk=96000000 mm=2 idlest=0 stby=0 sel=1 clkst=2
  [1.487243] PBX: mmc1 ios-in  ... (same)
  ```

  Decoded: functional and interface clocks **96 MHz**, `MODULEMODE = 2`
  (explicitly enabled), `IDLEST = 0` (fully functional), `STBYST = 0`,
  `CLKSEL = 1`, `CLKTRCTRL = 2`, `SYSCONFIG = 0x14` (`MIDLE = SIDLE = NO`).
* Register facts that make that decode safe to reuse (TRM Table 3-1328; the
  `CLKTRCTRL` encoding is also tabulated in
  `docs/reports/110-re-usb-2026-10-27.md`'s `CM_L3INIT_CLKSTCTRL` row):
  `CM_L3INIT_HSMMC2_CLKCTRL = 0x4A009330`, with `MODULEMODE[1:0]`
  (0 off, 1 enabled, 2 *explicitly* enabled = "functional clock may not be
  gated"), `IDLEST[17:16]` (0 functional, 2 interface-idle-only, 3 disabled and
  inaccessible), `STBYST[18]`, `CLKSEL[24]`;
  `CM_L3INIT_CLKSTCTRL = 0x4A009300`, with `CLKTRCTRL[1:0]`:
  **0 = NO_SLEEP, 1 = SW_SLEEP, 2 = SW_WKUP, 3 = HW_AUTO**.  Note the trap: on
  this CM instance **2 is awake, not asleep**.  (Report `118` §1 uses the same
  encoding for the DSS clock domain.)
* Code mechanics, verified in `local/kernel-tree/drivers/bus/ti-sysc.c`:
  * `SYSC_QUIRK_NO_IDLE_ON_INIT` (set board-wide by `sysc_probe()`, patch
    `:1449-1450`) keeps the target clocked through probe; a delayed work
    (`ti_sysc_idle()`, `:2873-2902`) performs the one-time clock disable and
    then drops the PM usage count.  It is scheduled with
    `schedule_delayed_work(&ddata->idle_work, 3000)` (`:3258`) — **3000
    jiffies**, which at HZ=100 is **30 s**, not the "3 s" the patch comment
    says.
  * `SYSC_QUIRK_NO_IDLE` (`ti,no-idle`) makes `sysc_runtime_idle()` return
    without suspending (`:1424-1436`) and makes `ti_sysc_idle()` **keep** the
    usage count instead of dropping it (`:2892-2894`).
  * `sysc_runtime_resume()` ends with `sysc_clkdm_allow_idle()` (`:1343`), so
    this is a *module-clock* hold, **not** a clkdm deny-idle.  What actually
    stops the module asking to idle is the SIDLE/MIDLE=NO property:
    `sysc_enable_module()` picks `best_mode = fls(sidlemodes) - 1` (`:1069`,
    `:1091`), i.e. the *highest* mode listed.  The upstream four-mode list
    therefore selects SMART_WKUP by default; overriding the DT to a single
    `SYSC_IDLE_NO` is what changes it.
* Consequence worth keeping: because the board-wide quirk holds every target for
  the first **30 s** (3000 jiffies at HZ=100) and the MMC2 probe runs at
  ~1.45 s, the target was awake *by construction* in every run of this session.
  The "first access after a short idle window stalls" premise in the DT comment
  was never demonstrated; the actual rescan failure was `ocr_avail = 0` (see
  *Superseded*).  The DT/link change is defensive, not the fix.

### 2. An MMC init-stream IRQ-status storm, and the ack added for it

* Upstream `omap_hsmmc_irq()` loops `while (status & (INT_EN_MASK | CIRQ_EN))`
  and only calls `omap_hsmmc_do_irq()` when `host->req_in_progress` is set.  The
  init-stream (CMD0) completion can arrive after `send_init_stream()` cleared
  STAT and re-enabled the IRQ.  This session added an `else` branch that **acks
  the status it finds** (`OMAP_HSMMC_WRITE(host->base, STAT, status)`) and logs
  `PBIRQ: ... stray`, with a comment naming the init-stream race
  (`local/kernel-tree/drivers/mmc/host/omap_hsmmc.c:1138-1152`; patch
  `:2604-2619`).
* The storm is captured before the ack:
  `docs/captures/playbook-live-spare-2026-10-26-clean-console-boot1-ring.txt`
  shows repeated `PBIRQ: mmc1 enter` / `stat=00000001 req=1` from 1.700 s
  (1.700, 1.734, 1.813, 1.835, 1.883, 1.900, 1.923, 1.938, 1.955, 1.970,
  1.994, 2.004 s) — the same status word, never cleared, each entry a fresh
  level-triggered IRQ.  After the change the probe/rescan proceeds:
  `...-mmc-irqfix-ring.txt` runs to `PBSIO: mmc1 init_stream done` at 1.500 s.
* Caveat (unresolved): the storm lines read `req=1`, so the *exact* path that
  made the status stick is not fully explained by the "no request in progress"
  comment.  What is confirmed is the added ack and the two captures; the
  causal chain from the `else` branch to the quiet post-change boot is an
  inference.
* It is a genuine IRQ-level hazard at the MMC2 rescan, so it was a plausible
  "wall" candidate — but the standstill remained after the change, so it was
  not the wall.

### 3. The MMC driver path completes (silent boot, event log)

With the console silenced (`loglevel=0`) the event log shows, between 1.23 s and
1.54 s (`...-silent-events.txt`, `...-poll-events.txt`, `...-fine-events.txt`,
`...-rpmcore-events.txt`):

```
set_ios pm=2 → set_clock after CEN → set_ios done
request: CMD1 → GIC enter IRQ 118 → mmc_irq entry → STAT read (stat>>16=0x00)
   → GIC done                      (three times, 20–40 ms apart)
mmc_attach_mmc: after send_op_cond
mmc_power_off → set_ios pm=0 → set_ios done
mmc_rescan: after freq loop → after release_host → out
… 130–140 ms later …
rpm_suspend: out / rpm_callback: before cb → hsmmc rpm_suspend: entry
   → after context_save → before spin_lock → lock acquired → before unlock
   → unlocked → after cb → before lock_irq → lock_irq done
   → callback returned → after rpm_idle(parent) → after suspend_suppliers → out
```

Every step completes; there is no MMC driver hang.  What this session did **not**
do is decode the CMD1 response: only `stat>>16` was logged, and the response was
logged as `stat=0x00`.  Session 10's `PBOCR`/`PBVOLT` prints are what showed the
card answering `0xc0ff8080` and the host masking it to zero.

### 4. `t_halt` is the last heartbeat, not the last event — the definition `109` cites

The event log is the printk-free record in the DEBUG_LL breadcrumb area
(`+0x00..+0x7C`, reached through the permanent non-cacheable alias).  Layout and
hooks are in `105` §1 and bootstrap §3.3; the definition that matters:

* `pb_tick()` is called from `gic_handle_irq()` at IRQ **entry** for INTID 29
  and 37, and writes `(0x3700 << 16) | (jiffies & 0xffff)`.  The tick's own
  `jiffies` increment happens later, so **the heartbeat is one tick behind**
  and has 10 ms granularity.  `t_halt` = that last value, i.e. a lower bound.
* The last *event* is written independently (initcalls, MMC, RPM, GIC), so it
  can be earlier or later than the last heartbeat.  Measured on my runs:

  | capture | bootargs | last event | last heartbeat |
  |---|---|---|---|
  | `silent` | `… loglevel=0 …` | 1430 `set_ios done` | 1500 |
  | `async` | `… loglevel=0 …` | 1390 `set_ios done` | 1460 |
  | `poll` | `… loglevel=0 nohz=off idle=poll cpuidle.off=1` | 1350 `rescan out` | 1490 |
  | `fine` | same | 1530 `hsmmc rpm_suspend: unlocked` | 1520 |
  | `rpmcore` | same | 1480 `rpm_suspend: out` | 1480 |
  | `initcall` | same | 1330 `initcall` | 1480 |
  | `noavs` | same + `CONFIG_POWER_AVS_OMAP=n` | 1290 `rpm_suspend: out` | 1500 |
  | `spin` | same + 30 s busy spin | 1350 `0xc001` (spin entry) | 1410 |
  | `gptspin` | same | 1330 `0xc001` | 1390 |
  | `wdtkick2` | same + TWL `0x2C=127` | 1320 `0xc001` | 1380 |
  | `novc`, `wdtkick` | VC/VP disabled | 370 `initcall` | 360 |

  So the heartbeat can lag the last event by up to ~140 ms (`poll`) and once
  preceded it (`fine`, −10 ms).  In the older heartbeat-only capture
  `...-mmc2-hb-events.txt` the last event (`mmc probe: entry`) is at 1500 ms
  and the heartbeat at 1490 ms — the event outlives the heartbeat by one tick,
  the same one-tick lag seen from the other side.  That file predates the
  `INITIAL_JIFFIES` fix: its printed "ms" are `jiffies*10`, not boot-relative
  (subtract `0x8ad0` from the jiffies value first).
* A stopped heartbeat does **not** prove a dead CPU: a writer can be preempted,
  and a stuck posted peripheral write can stop the console while the CPU lives
  (the reason `pb_event` uses `__raw_writel` with no barrier — bootstrap §3.3).

### 5. The printk ring is cache-stale; the DRAM ring is the only truth

Measured this session (`105` §6, bootstrap §3.2): `__log_buf` at PA
`0x81027ad0` (64 KB), descriptors at `0x80f523c8`, and `last_seq` in
`printk_rb_static` at `0x80f26380 + 0x14`.  On the silent runs it stopped at
**seq 894 / timestamp 1.415 s** while the DRAM ring ran to 1.49 s; the text
recovered from it is a shuffled mosaic (records reordered/duplicated), not a
missing tail in sequence.  The reason is cacheability, not overwrite.
Separately, `loglevel=0` silences the console, and the DRAM ring is written by
the debug console's `putc`, so `pb_ring.py dump` reads count 0 on those boots
(`109` trap 5) — the printk ring is the only text and it is exactly the one not
to trust.

### 6. IRQ 29 / 37 are the tick: the DT PPI numbering

`gic_handle_irq()` sees INTIDs, so the heartbeat needed the numbering resolved:

* DT `GIC_PPI n` = INTID `16 + n`.  IRQ **29** is `GIC_PPI 13` = the A9
  **local timer** (`local-timer@48240600`, `omap4.dtsi:86-92`); IRQ **37** is
  `GIC_SPI 37` = **GPTIMER1** (`timer1: timer@0`, `0x4A318000`,
  `ti,timer-alwon`, `omap4-l4.dtsi:1152-1182`).  Watching both covers a
  clockevent switch.
* This also disposes of `107`'s "always-on secure timer" candidate: the
  `0x4A318000` block it flagged *is* GPTIMER1, the kernel's own always-on
  timer — report `109` §8.1 reached the same conclusion from `REV = 0x15`.
* The PMU is not a candidate: the boot log has `hw perfevents: no irqs for PMU`,
  so PPI 29 is not a PMU overflow.

### 7. QNX reference: the pre-Linux script, and WDT2 counts up

* Boot order read from `local/proprietary/dumped4869ifs/proc/boot/.script`
  (full text in `105` §3): `setup-core-inactive full`, `resource_seed`,
  `devc-seromap`, `trustzone-omap4`, the three I²C controllers, then
  `omap4430-wdtkick -t 15000 -P -U 99:99`, `pmic_twl6030_cfg`, the PMIC driver
  and `devb-mmcsd-winchester`.  The kicker's usage text: `-a` base, `-t` kick
  time, `-P` also kick the TWL6030 PMIC primary watchdog, `-T` its timeout
  (default 127 s).
* Live WDT2 read on QNX (`105` §3.2):
  `0x4A314010 = 0x20`, `0x14 = 1`, `0x24 = 0`, `0x28 CRR = 0xffe5915e`,
  `0x2C LDR = 0xffe2b400`, `0x30 TGR = 0xffffd47b`, `0x34 WPS = 0`.
* The up-counter derivation (this is the correction later credited as "session
  3" in `109` §7 trap 4): `(0xffffffff − LDR)/32768 = 58.59 s` is the window;
  `CRR > LDR` by `0x2DD5E` ticks = **5.73 s elapsed**, leaving 52.86 s.  So the
  register counts **up** from LDR to `0xffffffff`, it is not a down-counter.
  QNX's own arming is the 58.6 s window; ours is different (see *Superseded*).
* The three RE reports (`106`, `107`, `108`), condensed:
  * `106` — `setup-core-inactive` writes `PRM_VC_VAL_BYPASS (0x4A307BA0)` with
    `0x013A5612` (TWL6030 slave 0x12, register 0x56 = `VDD_MPU SR_CMD`) and
    `0x01405512` (`SR_VOLT`), each followed by an **unbounded VALID poll**;
    "full" also idles ~58 modules, writes `PM_MPU_PWRSTST`, `CM_SDMA_STATICDEP`,
    some `CLKSTCTRL = SW_SLEEP`, and the A9-private `0x48243400/0x48243800`.
  * `107` — `trustzone-omap4` has no SMC at startup, no timers, no GIC/FIQ, no
    periodic work; it sets `CM_MPU_STATICDEP[14]` and `CM_L4SEC_CLKSTCTRL =
    HW_AUTO`.  The SMC thunk it installs does `push {r4-r12,lr}`.
  * `108` — the kicker reads/inverts/writes WDT2 `TGR` (`+0x30`) and writes
    `{0x2C, seconds}` to TWL6030 `0x48`; `0x2C` is `PRIMARY_WATCHDOG_CFG`,
    enabled by default on the TWL6030B1, expiry = PMIC WAIT-ON/OFF (rails
    down).  The PMIC watchdog is **not** the 1.4 s killer; `108` §6.2 has the
    neutralisation/kick recipe.

### 8. The negative experiments (each measured, none moved the stop)

* `CONFIG_POWER_AVS_OMAP(_CLASS3)=n`: last heartbeat 1.50 s, no change.
* `nohz=off idle=poll cpuidle.off=1`: 1.49 s, no change (not idle/NO_HZ).
* TWL watchdog reload at the TWL probe (`0x2C = 127`): 1.38 s, no change — and
  the write was **gated on a successful read and never verified**
  (`local/kernel-tree/drivers/mfd/twl-core.c`), so it may never have landed.
* VC/VP disabled (`vp_param/vc_param = NULL`): an **early** stop at 0.37 s in
  `__omap2_common_pm_late_init` (`...-novc-events.txt`, `...-wdtkick-events.txt`
  both end with `ptr = __omap2_common_pm_late_init+0x0`).  This is a
  self-inflicted software hang, not a wall, and it confounded the first TWL
  test (see *Dead ends*).
* MMC2 target readback (above): the target was already awake; the idle theory
  had no support in the window that was actually observed.

### 9. Device / DT facts that cost time to learn

* **The mmc label/alias inversion.** In `omap4.dtsi`, `aliases { mmc0 = &mmc1;
  mmc1 = &mmc2; … }`.  So the board's `&mmc2` (controller at `0x480b4000`,
  the eMMC) is **Linux `mmc1` / `mmcblk1`**, and `&mmc1` (`0x480ad000`) is the
  unused one.  The board file disables `&mmc1`, which is correct, but a future
  edit "by label number" can disable the wrong controller.  The PBX print says
  `mmc1` for the eMMC for exactly this reason
  (`kernel/source/omap4-winchester.dts:242-292`, capture §1 above).
* `omap_hsmmc` is probed asynchronously
  (`local/kernel-tree/drivers/mmc/host/omap_hsmmc.c:2264`,
  `.probe_type = PROBE_PREFER_ASYNCHRONOUS`), so its PBR/PBM block appears late
  and interleaves with other initcalls; "the last device printed" is not a
  boot-order step.  The `PBR:`-without-follow-up rule (bootstrap §6 lesson 2)
  is the reliable test.
* Print prefixes in the captures, so the ring text can be read without the
  patch open: `PBR:` driver core binding (before probe), `PBS:` ti-sysc target
  probe steps, `PBM:` omap_hsmmc probe steps, `PBX:` the CLKCTRL/SYSCONFIG
  state dump, `PBC:` mmc core (device_add/start_host/power_up/rescan),
  `PBSIO:`/`PBCLK:`/`PBINI:`/`PBIRQ:` the set_ios/clock/init-stream/IRQ
  stages, `PBG:` GPIO probe, `PBEV` the event-log magic.
* The chainloader's `bc_mirrors` copies `0x40` words (`+0x00..+0xFC`) before
  `clear_ring`, and `clear_ring` covers `+0x80..+0x107F`.  So the kernel must
  initialise its own event magic/count; on the first instrumented boot the
  magic still read `KERN` until `pb_event_init()` was moved into
  `gic_of_init()` (it had been in the MMC probe, which a boot might never
  reach).
* The event ring's first of the 28 entries can be out of order (a lost counter
  increment from the unlocked read-modify-write, or a stale slot).  It shows up
  as a lone late timestamp above the following entries (e.g. `spin`/`gptspin`/
  `wdtkick2` print a ~1.4 s `rpm_suspend: out` first, then 1.32–1.35 s events).
  Trust the tail and the heartbeat; do not build a story on the boundary slot.

## Inferred (reasoning from evidence, not directly measured)

* **The stop was an epoch set at kernel entry** — the session's working
  conclusion, from: the spread was independent of the code running (console
  on/off, AVS on/off, idle on/off, MMC vs spin); the tick and the code stopped
  together; a 30 s `cpu_relax` loop appeared to die ~60 ms in.  Strength: the
  measurements are real; the *interpretation* is superseded (`109`).
* **The missing pre-Linux state was the cause** — the `setup-core-inactive`
  PRCM/VC state and/or the secure world; this is why `106`/`107`/`108` were
  commissioned.  Strength: plausible at the time; never needed (`109`).
* **QNX's 58.6 s WDT2 window was also ours**, and the ~55 s reset seen from the
  flasher timestamps was one watchdog expiry.  Strength: the live QNX read is
  real; the transfer to our path is wrong (see *Superseded*).
* **The eMMC was not answering.**  I saw `mmc_attach_mmc: after send_op_cond`
  followed by a power-off and `rescan: out`, and read the silence of the CMD1
  path as "the card does not answer".  Strength: the events are real; the
  conclusion is wrong (see *Superseded*).
* **The busy spin proved a CPU hold.**  Strength: weak; the raw capture does
  not support it (see *Dead ends*).
* **The chainloader's uncached `blink_n` loops were the large, uncontrolled
  pre-kernel variable.**  Strength: the code is 12 × `0x04000000`-iteration
  loops with caches off; the cost was not measured this session (the epoch
  block came later; session 10 measured 1.77–4.78 s).
* **The 25–55 s "reset" was not a reset loop.**  Strength: none — it was wrong
  (`109` §8.1).

## Dead ends and wrong assumptions (the expensive part)

1. **The "global CPU halt at 1.4–1.5 s".**  This is the central error.
   * What I believed: a global, code-independent halt at a fixed time, with the
     SoC alive (the always-on WDT2 still reset it ~55 s later).
   * Why it looked real: with `loglevel=0` the console is silent; the DRAM ring
     and event log both stop with the tick; a pure `cpu_relax` loop appeared to
     stop 60 ms in; the stop time was stable across code changes.
   * What it actually was: the kernel's **normal VFS panic** (no root
     filesystem) with `panic()` disabling IRQs, and — for the runs whose late
     loader left little WDT2 window — the **7.812 s WDT2** arriving before
     `prepare_namespace()`.  See *Superseded* and `109`.
   * The cost: the whole session's experiment matrix (AVS, idle, TWL kick,
     VC/VP, PRCM RE) was aimed at a hardware epoch that did not exist.  The
     instrumentation it produced is what made the next session's answer cheap.
2. **`loglevel=0` "only KERN_EMERG reaches the console".**  I wrote that in `105`
   §1 and in the board DTS comment.  It is wrong: `console_loglevel = 0` drops
   *everything*, because a record prints only when `level < console_loglevel`
   (0 < 0 is false).  Use `loglevel=1` if EMERG must remain visible, or
   `ignore_loglevel` while diagnosing.  This mistake is the direct reason the
   panic was invisible for two sessions.
3. **The busy-spin test, called "decisive" in `105` §2.3, was not.**  Two
   independent reasons: (a) the `noavs` capture shows an `rpm_suspend: out`
   event *after* the nominal spin start (1290 ms vs 1250 ms) and in the
   `spin`/`noavs` captures `+0x04` still held the `pb_spin_init` pointer rather
   than the iteration counter — so either the spin never blocked or the event
   ring's ordering is off; its blocking is not established either way; (b) even
   a perfect spin cannot test liveness when the absolute WDT2 deadline is
   ~1.4 s of kernel time away.  The run should be treated as inconclusive, not
   as evidence.
4. **The TWL watchdog test was run twice, and the first run was invalid.**
   I had reverted `.config` (AVS) but not the `#if 0` source that disabled
   `vp_param/vc_param`, so the first `wdtkick` build died at 0.36 s inside
   `__omap2_common_pm_late_init` — a software hang, nothing to do with the
   PMIC.  The second build (`wdtkick2`) restored VC/VP and measured
   `t_halt = 1380 ms`, no movement; but the `0x2C = 127` write was gated on a
   read and never verified.  Lesson: when reverting a source experiment, revert
   the *source*, not just the config, and verify the write, not just the code.
5. **The 58.6 s WDT2 / "not a reset loop" reading.**  The live QNX register read
   is real, but it is QNX's own arming.  I then used the restore-flasher
   timestamps to argue a single ~55 s expiry, which made the 7.8 s reset loop
   invisible (the flasher's `--delay` was catching a later iteration).
6. **"The card does not answer."**  The CMD1 events were there; the error was
   `-EINVAL` from `mmc_select_voltage()` with `host->ocr_avail = 0`.  The card
   had answered OCR `0xc0ff8080` all along (session 10's `PBOCR`).
7. **`WDTIMER1 (0x4A318000)` as an always-on secure watchdog.**  I proposed it
   as the hardware-epoch candidate in `105` §5 / `107`; it is not a watchdog
   (`REV = 0x15`, no 32 kHz countdown — `109` §8.1).
8. **"Target awake at the halt" was never measured *through* the halt.**  The
   PBX readback is at probe and at each `set_ios`; the last sample is 1.487 s in
   a console-on boot, while the silent runs' heartbeats stop at 1.48–1.52 s.
   Those are different boots and different instruments: "awake at the last MMC
   activity I captured", not a sample taken at the stop.  Do not over-read it.
9. **`pb_event_init()` in the MMC probe.**  If a boot never reaches the MMC
   probe, the event area keeps the loader's mirrored words and the magic reads
   `KERN`.  Moving the init to `gic_of_init()` fixed it; that in turn is why
   the log's magic/count are set before any driver runs.
10. **The first sealed-IFS hash looked wrong.**  The IFS starts at media
    `0x100000`; a hash taken from offset 0 does not match the sealed value, so
    the read needs `skip=2048` (bootstrap §1.3).  The media was fine; the read
    was not.
11. **The "first MMC2 access after an idle window stalls" premise** (the board
    DTS comment) was never demonstrated.  The target is held clocked for the
    first 30 s by the board-wide quirk and the probe runs at 1.45 s; the rescan
    failure was the OCR.  The change is harmless and defensive, not causal.

## Unknown / open questions

* **CPU1.**  QNX's first action is `setup-core-inactive full`; Linux leaves
  CPU1 as the bootloader parked it (`maxcpus=1` is not a valid parameter;
  NR_CPUS=1).  This session raised it as a suspect and never tested it.  It is
  moot for the "wall" (`109`), but still untested for general bring-up.
* **Is the MMC2 `ti,no-idle` + device link still earning its keep?**  With the
  `vemmc` supply the card binds; the target is held clocked for the first 30 s
  by the board-wide quirk anyway.  Keep or retire is a decision nobody has
  taken.
* **Did the clkdm ever actually gate the MMC2 functional clock between
  accesses?**  `sysc_runtime_resume()` ends with `sysc_clkdm_allow_idle()`, so
  in principle yes; the PBX samples show `fck = 96 MHz` at every `set_ios`, but
  there is no sample at the stop.  Unresolved.
* **The per-boot chainloader time on the session-3 runs.**  Not measured then
  (the epoch block came in session 10), so the `t_halt` → arming-relative
  arithmetic below is inferred, not measured per boot.
* **The event-ring ordering anomaly.**  The unlocked counter read-modify-write
  can lose an increment and surface a stale/racy slot; the decoder has no
  protection.  A fixed slot layout or an IRQ-safe counter would settle it.
* **The TWL6030 watchdog period actually armed on this unit pre-Linux.**  Never
  read; `108`'s 127 s default is from the datasheet/RE, not a measurement here.
* **Cleanup debt left in the tree.**  The `PB*` prints (`PBR`/`PBS`/`PBM`/`PBX`/
  `PBC`/`PBSIO`/`PBCLK`/`PBINI`/`PBIRQ`/`PBG`) and the `pb_event` hooks are still
  in the shipped patch; `session-10.md` separately flags the `PBVOLT`/`PBOCR`
  prints and `PB_SMC_PROBES`.  The board DTS comments still say
  "`loglevel=0`: only KERN_EMERG reaches the console", "WDT2 … ~58 s", and
  "the ~1.5 s stall" — all stale; and `105` §5 item 6 still says the card does
  not answer.  These are documentation defects for the reconciliation pass, not
  behavioural bugs.

## Superseded / uncertain

* **The global halt.**  `109`'s headline (and `session-10.md`) is current: with
  the console on, the boot ends in
  `Kernel panic - not syncing: VFS: Unable to mount root fs on
  unknown-block(0,0)`; `panic()` calls `local_irq_disable()`, so the tick stops
  and the log ends like a frozen CPU.  Each "halt" is where
  `prepare_namespace()` failed (2250 ms silent, 2440 ms console-on); the
  1.38–1.53 s cluster is the WDT2.  My evidence (the stable stop, the spin,
  the ruled-out drivers) is in `105`; the interpretation is not.
* **The 58.6 s WDT2 window.**  That is QNX's own arming (`LDR = 0xffe2b400`).
  On the Linux/chainloader path `LDR = 0xfffc1800` → **7.812 s**, re-armed 7 ms
  before the chainloader (`109` §1.1, §3).  The observed "~55 s" is the restore
  flasher catching a bootrom after several 7.8 s loop iterations; the unit does
  reset-loop while a test image is on media.
* **"The unit does not reset-loop."**  Wrong — the opposite (`109` §8.1,
  bootstrap trap 15).
* **The busy-spin deaths.**  Session 10 reads them as watchdog kills.  With the
  missing 1520 ms decompressor offset (`109` §5.2) the arithmetic closes:
  `halt_arming = entry + 1520 + halt_kernel`; for my heartbeats (1380–1520 ms)
  and the 7812 ms deadline, entry = 4772–4912 ms after arming — i.e. the
  untrimmed loader (~4.8 s) plus the 7 ms hand-over, at the top of the
  1.77–4.78 s range session 10 measured.  So the *death* was the WDT2; the
  spin's own blocking is still unproven (see Dead ends 3), so the run remains
  inconclusive rather than confirming.
* **The eMMC "does not answer".**  The card answered OCR `0xc0ff8080`; the host
  masked it to zero (`ocr_avail = 0`), `mmc_select_voltage()` returned 0 and
  `mmc_attach_mmc()` returned `-EINVAL` — "no support for card's volts", not a
  timeout.  Fixed by the `vemmc` fixed 3.3 V regulator + `vmmc-supply`
  (`109` §9).  `105` §5 item 6 is stale.  The whole cluster (this note's "global
  halt", the MMC "wall" and the register-stall reading) is one contradiction:
  `docs/contradictions/104-mmc2-wall-vs-wdt2-vfs.md`.
* **`loglevel=0` "only EMERG".**  Wrong (Dead ends 2); `109` supplies the
  corrected bootargs (`loglevel=0` for silence, `ignore_loglevel` for
  diagnosis) and `109` §8 uses `loglevel=1`.
* **WDTIMER1 as a secure watchdog.**  Superseded by `109` §8.1.
* **The `setup-core-inactive` / PRCM-VC-VP / secure-world / CPU1 families.**
  None of them was needed: the "wall" was the panic, and the epoch test's
  kernel-entry anchoring does not implicate the PRCM state (the test's answer
  was correct for a mundane reason).  `106`–`108` remain useful *hardware*
  documentation (VC bypass payloads, WDT kick semantics, the TWL PMIC
  watchdog), not a fix list.
* **"The wall is time-based, not device-based."**  `105` §2.2/§2.3 states this
  as a conclusion.  It was an artifact of `loglevel=0` plus the absolute WDT2
  deadline: the console changed *which code* was running at the deadline, not
  the deadline.  Treat it as a warning about the console's cost, not a fact
  about the kernel.
* **The board DTS comments** (`kernel/source/omap4-winchester.dts:48-51`,
  `:294-300`) are stale for the same reasons: `loglevel=0` behaviour and the
  "~58 s" WDT2 period.  The code they document (`ignore_loglevel`,
  `clk_ignore_unused`, `vemmc`) is current.
* **Uncertain:** the exact per-boot loader time for the `t_halt` table is
  inferred from session 10's 1520 ms offset; if a future capture re-measures
  the loader on an old image, the entry column can be checked.  Until then the
  WDT2 explanation is consistent, not proven per boot.

## Artefacts produced

* `docs/reports/105-global-1.5s-stall-2026-10-26.md` — the session's main
  report: MMC2 readback, the event log, the MMC path, the experiment matrix,
  the QNX script (a correction banner from Linux-era session 4 = absolute
  `session-10.md` was inserted later).
* `docs/reports/106-re-setup-core-inactive.md`,
  `docs/reports/107-re-trustzone-omap4.md`,
  `docs/reports/108-re-wdtkick-pmic.md` — the three subagent RE reports.
* `docs/reports/HANDOFF-2026-10-26-global-stall.md` — the handoff (now
  superseded by `109`).
* `docs/reports/NEXT-SESSION-BOOTSTRAP-2026-10-26.md` — the first full version
  of the manual (access, loop, capture formats, instrumentation index, traps);
  later sessions prepended update blocks.
* `tools/pb_events.py` — new decoder for the event log (`--raw`, pointer
  resolution against `System.map`, `INITIAL_JIFFIES` offset).
* `include/linux/pb_event.h` (new) — event log + `pb_event`/`pb_ptr`/`pb_tick`/
  `pb_event_init`; hooks in `drivers/irqchip/irq-gic.c` (init in
  `gic_of_init`, IRQ enter/done, heartbeat on 29/37), `init/main.c` (initcalls),
  `init/do_mounts.c`, `kernel/async.c`, `drivers/base/dd.c`,
  `drivers/base/power/runtime.c`, `drivers/bus/ti-sysc.c`,
  `drivers/mmc/host/omap_hsmmc.c`, `drivers/mmc/core/core.c`,
  `drivers/mmc/core/mmc.c`, `drivers/mfd/twl-core.c`,
  `drivers/i2c/busses/i2c-omap.c`.
* `drivers/mmc/host/omap_hsmmc.c` — `PB*` checkpoints, the init-stream
  status ack, the device link, the `PBX` CLKCTRL/SYSCONFIG dump, context-save
  and RPM instrumentation.
* `drivers/bus/ti-sysc.c` — board-wide `SYSC_QUIRK_NO_IDLE_ON_INIT` (22 lines).
* `arch/arm/boot/dts/ti/omap/omap4-l4.dtsi` — `mmc2_target:` label;
  `arch/arm/boot/dts/ti/omap/omap4-winchester.dts` — the `&mmc2_target` block.
* `drivers/mfd/twl-core.c` — TWL6030 `0x2C = 127` reload (read-gated, later
  superseded by `108` §6.2's unconditional recipe).
* `.config` experiments: `CONFIG_POWER_AVS_OMAP(_CLASS3)` on/off,
  `CONFIG_MMC_BLOCK=y` restored at the end.
* Image: `chainloader/kernel.img` CRC **`0x0BEB602A`** (on disk, not on media).
* Captures (all in `docs/captures/`):
  `...-silent-{ring,events}.txt`, `...-async-*`, `...-poll-*`, `...-fine-*`,
  `...-rpm-*`, `...-rpmcore-*`, `...-initcall-*`, `...-noavs-*`,
  `...-spin-*`, `...-gptspin-*`, `...-novc-*`, `...-wdtkick-*`,
  `...-wdtkick2-*`, `...-mmc2-{hb,noidle}-*`, `...-mmc-{irqfix,reqirq}-*`,
  `...-console-boot1-ring.txt`, `...-full-boot-2026-10-26.txt`.
