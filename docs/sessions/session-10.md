# Session 10 (absolute 10, Linux-era "session 4") — the epoch test, the panic behind the "wall", WDT2, eMMC, USB and the console

> Numbering: absolute **10** of 12.  The reports call this **"session 4"** — its
> output handoff is `HANDOFF-2026-10-27-session5.md` ("session 4 → session 5") and
> the reports it wrote are `109`…`116`.  See `NUMBERING.md`.

Span: 2026-10-26 late → 2026-10-27 (device logs are ART = UTC−3).  The host was
powered off overnight with this session still open; when the operator resumed it
on 2026-10-28 the session also repaired the door tooling and the docs (§7).

Unit state at the end: TabletOS from the sealed IFS (sha256 `c72b0530…` verified),
door up, `BOOTCTR_FCTRY` read `0x32`.  See *Superseded* — the top-up script was
later found to be broken, so that reading is **not** evidence of a repair.

## Goal of this session

1. Run report 108 §6.3's **epoch test** — insert a calibrated delay **before any
   kernel timer** and compare the halt time in kernel time.  `t_halt(D) ≈
   t_halt(0) − D` ⇒ a hardware epoch; unchanged ⇒ anchored at kernel entry.  This
   one experiment was to decide between the PRCM/VC family (report 106) and the
   secure-timer family (report 107) for the "1.4 s global stall".
2. Then, per the operator: continue the bring-up — card init first, USB/display
   "probably soon but maybe not today".

"Done" meant the epoch question answered *from measurements*, and whatever was
still blocking the bring-up identified or removed.

## Confirmed (measured, read in a file, or seen on the bench)

### 1. The epoch test: not a hardware epoch

The delay was built into the chainloader (`EP_TICKS`, in WDT2 `CRR` ticks at
32.768 kHz) and published its own state in an "epoch block" at ring1 `+0x88`
(`EPCH` magic, target, start/last `CRR`, elapsed, iterations) — re-published every
iteration, so a halt *inside* the delay would have been recorded too.
(`docs/reports/109-epoch-test-2026-10-27.md` §1; `boot/chainloader/`, still
present with `EP_TICKS = 0`.)

| run | D (ms) | kernel entry, ms after WDT2 arming | last heartbeat (kernel ms) |
|---|---|---|---|
| 4 | 0 | 2771 | **2240** |
| 5 | 0 | 2780 | **2240** |
| 6 | 0 | 2771 | **2240** |
| 3 | **1000** | **3771** (exactly +1000) | **2240** |

* `t_halt(D) − t_halt(0) = 0`, not −1000 ⇒ **not a hardware epoch** (109 §2.1).
* D=0 jitter was **≤ 10 ms** (three boots, identical heartbeat); D=1000 changed
  entry by exactly 1000 ms and the halt by 0.
* The delay is exactly calibrated, not nominal: the block records
  `elapsed = 32768` ticks (1000 ms, 102 492 iterations) for D=1000 and
  `131072` (4000 ms) for D=4000 (109 §2.1).
* Independent corroboration from the 32 kHz counter: runs 5 and 6 had absolute
  entries 299 ms apart (`32k = 30057` vs `29758` at entry) and absolute halts
  exactly 299 ms apart, while their kernel-time halts were identical (109 §2.1).
* **D=3000 was not usable** and never run: the watchdog window is 7.8 s from
  arming, and the loader (2.76 s) plus the decompressor/early boot (1.52 s) is
  already 4.3 s, so a 3 s delay would have put kernel entry at ~7.3 s after
  arming — about 0.5 s before the WDT2 overflow, far too little to observe the
  2.24 s wall.  D=2000 and D=4000 were run instead; both ended in the watchdog
  (109 §2.2, and run 8's ring2 shows the kernel started and was cut off inside
  early `start_kernel`).

### 2. The "wall" was the kernel's own VFS panic

* With the console on (`ignore_loglevel`) the boot ended at
  `[2.440490] Kernel panic - not syncing: VFS: Unable to mount root fs on
  unknown-block(0,0)` — **there was never a root filesystem** (109 §0).
* `panic()` calls `local_irq_disable()` (`kernel/panic.c:312` in 6.15.11), so the
  tick stops and the DRAM log ends exactly like a frozen CPU.  `loglevel=0`
  suppresses even KERN_EMERG, which is why this was invisible for two sessions.
* The two clusters map onto that: the silent runs stopped right after
  `prepare_namespace: after wait_for_device_probe` (2250 ms) and panicked there;
  the console-on run reached the same point later (2440 ms) because the console
  slows the boot.  So the "wall time" is *where the boot was when
  `prepare_namespace()` failed*, not a clock.
* Consequence: sessions 3's busy-spin "deaths" (~1.4–1.5 s) were the watchdog
  arriving before `prepare_namespace` (§3), and nothing in PRCM/VC/VP, the secure
  world or CPU1 was ever implicated.

### 3. WDT2 — the real numbers, and the only lever

* The bootloader arms it with **`LDR = 0xfffc1800` ⇒ 7.812 s**, not the 58.6 s
  seen on live QNX; it is (re)armed **7 ms before the chainloader starts**
  (`cl-entry = LDR + 7 ms`), so it is a per-boot clock (109 §1.1).
* `CRR` counts **up** from `LDR` towards `0xffffffff`; overflow resets.
* **Writes to `LDR` are dropped.**  A `TGR` kick *does* reload `CRR`: the kernel
  probe measured `crr fffd0e91 -> fffc1803` with `wps = 0`.  QNX's own kicker is
  exactly "read `TGR`, invert, write".
* `CM_WKUP_WDTIMER2_CLKCTRL = 0x4A307830` and it already reads
  `0x00000002` (MODULEMODE = HW_AUTO, IDLEST = 0) — the module clock was never
  the problem.  `0x4A31C030` (an earlier guess) is the **keypad** target-module;
  writing it wedged the loader.
* The kick must come from a **`timer_list`, not the tick**: an idle kernel is
  tickless, the sampler ran only 315 times in 8.6 s, a `jiffies % 200` kick
  missed its slot and the board still reset at 8.64 s.  With a 2 s timer the
  heartbeat reached 14 510 ms and `/init` printed "STILL ALIVE at 12 s".
* While a test image is on media the unit therefore **reset-loops every ~7.8 s**;
  that is what the restore flasher's `--delay` was always "catching", and it is
  why session 3's "~55 s reset" and "not a reset loop" were both wrong.
* `0x4A318000` is **not** a watchdog on this unit (`REV = 0x15`, and its "counter"
  words move like unrelated data, not a 32 kHz countdown), so report 107's
  "always-on secure timer" candidate is a misidentification (109 §8.1).

### 4. eMMC: the card always answered; the host was misconfigured

* CMD1 was returning sane data all along — logged every poll
  (`PBOCR: arg=00000000 resp=40ff8080 host_avail=00000000 busy`, then
  `resp=c0ff8080`, i.e. ready | high-capacity | 2.7–3.6 V) (109 §9).
* The failure was `-EINVAL`, not a timeout: `host->ocr_avail = 0` because `&mmc2`
  declared no supply, so `mmc_select_voltage()` masked the card's OCR to zero and
  `mmc_attach_mmc()` returned `-EINVAL` ("no support for card's volts",
  `drivers/mmc/core/core.c:1136-1138` in 6.15.11).
* Mechanism worth remembering: `mmc_of_parse_voltage()` parses `voltage-ranges`
  but has **no in-tree caller**; `omap_hsmmc` takes `ocr_mask` only from legacy
  platform data (`omap_hsmmc.c:2059`), so on a DT board the OCR mask comes from
  the **`vmmc-supply` regulator's constraints** (`mmc_regulator_get_supply()`).
* Fix: a `regulator-fixed` 3.3 V rail (`vemmc`, always-on) + `vmmc-supply`;
  `ocr_avail` then reads `0x00300000` and the card binds:
  `mmc1: new high speed MMC card at address 0001`,
  `mmcblk1 = MCGAFA 59.4 GiB`, `boot0`/`boot1` 512 KiB, `rpmb` 128 KiB (chardev
  only).  Nothing was mounted or written (109 §9).

### 5. USB gadget enumerates (Linux side)

Four separate fixes were needed, two of them non-obvious (114 §1):

1. the whole USB stack was `=m` and an initramfs kernel loads no modules → build
   it in;
2. the OMAP musb glue parses the **legacy u32 `mode`** property, not `dr_mode`
   (`drivers/usb/musb/omap2430.c:362`) — without it
   `musb-hdrc.0.auto: unsupported port mode 0` and **no UDC**; `mode = <2>`;
3. OMAP4430 uses the internal **UTMI+** PHY: `interface-type = <1>`
   (cf. `omap4-panda-common.dtsi:540`);
4. **VBUS/ID sensing lives in the TWL6030**, not in the OMAP PHY:
   `&twl_usb_comparator { usb-supply = <&vusb>; }`.  Without it the UDC binds but
   `/sys/class/udc/musb-hdrc.0.auto/state` stays **`not attached`** forever.

Result: host sees `0fca:8010`, product **"PlayBook (Linux bring-up)"**,
`/dev/ttyACM0`; the device logs `activate acm ttyGS0` and then `req21.20`
(the host's `SET_LINE_CODING`) — a real conversation, not just descriptors
(114 §0).

Userspace probe before the driver bound (112 §2): `MUSB_DEVCTL = 0x00000080`
(BSESSVLD, SESSEND = 0, IDDIG = 0 = device), `USBOTGHS_CONTROL = 0x15` already,
`DPLL_USB` mode `0x7` (locked), `L3INIT_USBOTGHS = 0x02`,
`L3INIT_USBHOSTHS = 0x0101`, `L3INIT_USBTLL = 0x01`.

### 6. Userspace and the console

* A static `/init` as PID 1 avoids `prepare_namespace()` entirely — the VFS panic
  disappears and `PLAYBOOK: /init running as PID 1` appears at 2.29 s (109 §8).
* `/console` is a REPL on `/dev/ttyGS0` (`help ls cat peek poke kick reboot`);
  every `/dev/mem` access runs in a **forked worker with a bounded alarm**, so a
  gated target's L4 abort is a SIGBUS in the child, not a dead unit (115).
* `/init` reboots **180 s** after start and immediately on the console's
  `reboot` request (pipe), which is what makes the boot rom reachable for the
  restore without a power cycle (109 §8, 115).
* Door timings measured while testing the supervisor: **cold start 9 s**,
  **supervisor respawn 12 s**; a dead door presents as
  `ssh: … port 22: Connection refused` (that *is* the door) — later folded into
  bootstrap §1.1.
* **`/dev/kmsg` from userspace is not a reliable log near a faulting device
  access**: records written around the fault vanished while PID 1 kept running.
  The fix used for every probe afterwards was to write the DRAM ring page
  directly (`/dev/mem` at PA `0x88000000`, same count/index protocol as the
  kernel's `ring_put`), which also survives the reset (112 §4, 115 §6,
  `HANDOFF-2026-10-27-session5.md` §7 trap 2).
* The **DRAM ring wraps at 32 KB**: a run that printed ~90 KB kept only the tail,
  so the early boot lines (including a probe at 0.8 s) were gone.  Keep runs
  short or read twice (`HANDOFF-2026-10-27-session5.md` §7 trap 3).

### 7. Display reachability (report 111's R3) — and what it did *not* prove

From userspace, read-only (`peek`): `DSS_REVISION = 0x00000040`,
`DISPC_REVISION = 0x00000040`, `DISPC_SYSSTATUS = 0x00000001` (RESETDONE),
`DISPC_CONTROL = 0`, `DSI1_REVISION = 0x00000030`,
`DSI1_SYSSTATUS = 0x00000001`, `CM_DSS_CLKSTCTRL = 0x00000f02`,
`CM_DSS_CLKCTRL = 0x00040f02` (112 §1).  A read of the **DSI PHY page
`0x48044200`** aborted (SIGBUS in the forked worker) — so "reachable" was proven
for DSS/DISPC/DSI-core and *not* for the PHY, and I attributed the abort to the
PLL being off (see *Superseded* for the sharper cause).

### 8. Loader and image facts

* The chainloader's LED blink phase was trimmed and then **removed**
  (`blink_n`/`delay`/`led_on` deleted): pre-kernel went **2764 → 840 → 781 ms**;
  the loader is 828 B (109 §8, 115).
* The epoch instrument is still in the tree and repeatable:
  `boot/chainloader/chainloader_kernel.S` carries `.equ EP_TICKS, 0x00000000`
  and the `EPCH` block; `EP_TICKS=<ticks> … tools/build_kernel_image.py` enables a
  delay of that many 32.768 kHz ticks (needs a WDT2 window that accommodates it).
* Images from this session are preserved in `local/images/`:
  `kernel-d{0,1000,2000,3000,4000}.img`, `kernel-clean-blinkless.img`,
  `kernel-console-base.img`, `kernel-console-guarded.img`.

## Inferred (reasoning from evidence, not directly measured)

* **Kernel time 0 is 1519 ms after the loader's jump** (32k counter at first tick
  `31345 ms` vs `29826 ms` at the pre-jump sample — capture
  `docs/captures/playbook-live-spare-2026-10-26-clean-mpustate-run1.txt`, quoted
  in 109 §5.2): decompressor + early boot.  Strength: good for that run, never
  re-measured per boot, and it is the number that converts between the two
  clocks.
* The **2240 ms wall is a position, not a duration**: it tracks how fast the boot
  reaches `prepare_namespace()` (console on moved it to 2440 ms).  Strength:
  strong — it is the direct reading of the panic (§2).
* The chainloader phase varies **1.77–4.78 s** across boots because the blink
  loops were uncached and the bootloader's MPU clock differs.  Strength: the
  durations are measured (several runs); the cause is an inference.  An early
  attempt to correlate the variation with the two halt clusters
  (2.24/1.45 ≈ 2.77/1.77) did not survive more data.
* The DSI PHY read abort was "expected while the PLL is off" — the correct
  generalisation (now proven by session 11) is that the **DSS power domain** was
  off, and any access aborts until `PM_DSS_PWRSTCTRL` is on (see *Superseded*).
* `select_kek` returning `-1` at every probe point means the hardware-KEK path is
  unavailable on this unit, which is why QNX's RPMB driver is running on its
  hardcoded fallback key (116 §3).  Strength: the return value is measured; the
  consequence is inferred.

## Dead ends and wrong assumptions (the expensive part)

1. **I expected a hardware epoch, and sized D for it.**  D=3000 was designed to
   make the shift unmistakable, but the 7.8 s watchdog window (unknown when the
   plan was written — the docs said 58.6 s) makes any pre-kernel delay over ~2 s
   land in the watchdog.  Recovering meant running D=1000 first, which then
   decided the question anyway.
2. **My own "~6.26 s second hazard"** in 109 §2.2 was an arithmetic error: I
   compared entries measured from the WDT2 arming against halts measured in
   kernel time and omitted the 1519 ms decompressor offset.  With it, those runs
   are the WDT2 overflow at 7812 ms and there is one wall, not two (109 §5.2 —
   corrected in-session, not by a later session).
3. **"The SMC never returns."**  The first two SMC probes looked exactly like a
   hung monitor: heartbeat stopped at 300 ms, no return value, mask bit unset.
   It was a **kernel NULL write at `gic_handle_irq+0x324`** (`str r0, [r8]`,
   `r8 = 0`): the masked monitor **clobbers r4–r12**, and the compiler had live
   values (the ring pointer) there.  QNX's own thunk does `push {r4-r12,lr}` for
   exactly this reason.  With that fixed the monitor returns `-1` instantly at
   300/800/1300/2000 ms (109 §6.1).
4. **`0x4A31C030` is not CM_WKUP.**  Writing `MODULEMODE` there wedged the
   loader (no epoch block, kernel never started); the register is the keypad's
   target-module.  The TRM's WKUP_CM is `0x4A307800`, so the WDT2 clock control is
   `0x4A307830` (109 §8.1).
5. **Extending the watchdog window by writing `LDR` does not work** (writes are
   dropped), so the whole "reload value = 58.6 s like QNX" plan died; kicking is
   the only lever.
6. **A kernel-side WDT2 probe at gic init killed the boot** when it touched the
   wrong CM base: the abort happened before the first `pr_emerg`, so there was no
   trace at all — the event counter read 0 and the `EPCH` block was absent.  The
   probe had to move to a forked userspace worker (§6) before it could be
   debugged at all.
7. **The loader-side WDT2 write diagnostic wrote to the wrong address** (`PWDT`
   ended up at `0x90000004`, not ring3 `+0xb0`) and wedged the loader; abandoned
   in favour of userspace probing, which is also where it belongs.
8. **`system()` does not work in this initramfs** (no shell): configfs objects
   have to be built with `mkdir()`/`symlink()` directly.
9. **`pkill -f 'door-hold\.sh'` killed my own shell** (exit 143) because the
   command line contained the path; the test aborted mid-way.  Kill by explicit
   PID, and use a character class in the pattern (`'BerryShell-V4[.]py'`) so it
   cannot match your own argv.
10. **`/tmp/pbssh.sh` was the only way to reach the unit** and it vanished when
    the operator powered the host off — the next session started with no way in.
    Fixed in this session by checking in `tools/pbssh.sh` and writing bootstrap
    §1.1 (cold start + the `lsusb` product-ID table, including "nothing at all =
    cable/power").
11. **Believing the top-up script's log.**  I read
    `/accounts/devuser/startup.sh`, quoted its comment, and reported that it
    "writes `0x32` and verifies by readback; `.bootctr.log` shows success on every
    boot" — and used that to explain why the counter read 50 all session.  The
    log was a false positive (§Superseded).
12. **"Nothing on our path consumes the counter, so maybe no write is needed."**
    I argued this from a dozen-plus readings of `0x32` across ~25–30 cycles, then
    (after the operator's correction) that the script repairs it every boot.  Both
    are wrong: the counter does drain, and the repair was a no-op.  See the
    contradictions file.

## Unknown / open questions

* **`select_kek` (`0x31`) returns `-1`: no usable hardware KEK, or a wrong
  descriptor / missing L4SEC setup?**  Resolve by re-running the probe with the
  descriptor and function arguments varied, and by reading the two L4_WKUP AP
  words report 107 §5.2 uses (`0x4A300100` bit 4, `0x4A300104[10:0] == 0x7FF`).
  It matters because the KEK path is the "clean" source of the RPMB key.
* **What `0x800149C2` does to `BOOTCTR_FCTRY`** (consume / zero / invalidate) —
  inherited from report 75, still unresolved, and it is the mechanism that makes
  an out-of-range value a hard lock.
* **Why the counter read `0x32` throughout this session while the repair was
  broken** — recorded as a contradiction rather than resolved
  (`docs/contradictions/bootctr-drain-vs-session10-readings.md`).
* **Is a kernel-side CRR sample safe?**  This session skipped it (a bad read at
  gic init had already killed one boot) and used the loader's sample plus the
  32 kHz counter instead.  A guarded version (forked/alarmed from userspace, or
  `panic_on_oops=0`) was never tried.
* **Why a `peek 48040000` that returned `0x40` at 2.8 s aborted at ~50 s** — the
  DSS module had gone to standby in between.  Session 11 explained the state
  (`STBYST = 1`, no consumer of `dss_clk`, report 118 §7.1); the *timing* of the
  transition was never measured.
* Documentation defect left by this session: the shipped patch still carries the
  two temporary MMC voltage prints (`PBVOLT` in `drivers/mmc/core/core.c`,
  `PBOCR` in `drivers/mmc/core/mmc_ops.c`), and the `PB_SMC_PROBES 0` machinery is
  still compiled into `irq-gic.c` — both intended to be removed once the card and
  the secure-world question were settled.

## Superseded / uncertain

* **Report 116 §2's account of `startup.sh` is wrong.**  I described it as
  writing `0x32` and verifying by readback, with `.bootctr.log` proving it ran
  every boot.  Session 5's addenda (in `116-rpmb-bootctr-plan-2026-10-27.md`,
  "Session 5 addendum" and "Addendum 2") show `printf` **does not exist on
  TabletOS**, so the script was writing `0x00000000` six times per boot (refused
  as out-of-range — the only reason the unit survived), and the readback check
  cannot distinguish "our write landed" from "nothing changed".  The current
  implementation is `bench/device-startup.sh`.  My evidence at the time was the
  script text plus ~25 `bootctr -> 0x32 ok` log lines; that evidence was
  worthless, which is the lesson.
* **My claim that a static counter reading was explained by a working repair** is
  superseded by the same addenda; the counter really drains (they measured
  `0x32 → 0x28` over ~10 boots).  See the contradiction file for the part that
  still does not add up.
* **112 §1's explanation of the DSI PHY abort** ("expected while its PLL is off")
  is superseded: the cause is the **DSS power domain being off**
  (`PM_DSS_PWRSTCTRL = 0`, `CM_DSS_DSS_CLKCTRL = MODULEMODE 0`, `IDLEST = 3`), and
  writing `MODULEMODE = 2` alone does not help — reads still abort (report 118 §1,
  session 11).  My conclusion "not a firewall" was right; the mechanism was
  wrong.
* **Report 111 (commissioned from a subagent this session) is superseded in two
  places**: the reference runs **four** DSI data lanes, not two (report 121 §11,
  `DSI_COMPLEXIO_CFG1 = 0x2A054321`; `docs/contradictions/118-vs-121-dsi-link-rate.md`),
  and the TC358764 control port is
  **MIPI DSI generic packets**, with nothing on this board programming the bridge
  (reports 121 §9, 122; `docs/contradictions/111-vs-122-bridge-programming.md`).
  Its SPI TCON byte table (111 §3.5) still stands.
* **Report 113 (my subagent's RE of TabletOS's `devu-omap4430-musb.so`) is
  unaudited** — it wrote ~48 KB but its tail ends mid-work.  Nothing in the
  working USB path depends on it.
* **109 §2.2's "second freeze at ~6.26 s"** is superseded by 109 §5.2, written
  later in the same session: the offset was missing, not the hazard.
* **The epoch test's answer needs a caveat in hindsight.**  "Kernel-entry
  anchored" was correct and it did rule out a hardware epoch, but the reason is
  mundane: the halt is where `prepare_namespace()` ran.  So the result does not
  implicate the PRCM/VC state the kernel sets at entry — that family was never
  tested by this experiment, it simply was not needed.
* **Image CRC bookkeeping**: `HANDOFF-2026-10-27-session5.md` §3 and report 115
  both quote `0xA245C8 bytes / crc 0x642BDFBE`, but 115 also records an earlier
  console build `0x185F45A7`; the two size/CRC pairs cannot both be the same
  binary.  Treat 115's *table* as the console record and the handoff's row as a
  transcription to verify against `local/images/kernel-console-*.img`.

## Artefacts produced

* `docs/reports/109-epoch-test-2026-10-27.md` — the epoch test, the VFS-panic
  discovery, WDT2 §8.1, eMMC §9 (the session's main report).
* `docs/reports/110-re-usb-2026-10-27.md`, `111-re-display-2026-10-27.md`,
  `113-re-usb-device-driver-2026-10-27.md` — subagent RE (111 and 113 later
  corrected/unaudited, see above).
* `docs/reports/112-bench-r3-usb-m3-2026-10-27.md` — userspace register probes.
* `docs/reports/114-usb-gadget-works-2026-10-27.md` — the four USB fixes.
* `docs/reports/115-usb-console-2026-10-27.md` — the console, its verification
  and the `/dev/kmsg`-vs-DRAM-ring lesson.
* `docs/reports/116-rpmb-bootctr-plan-2026-10-27.md` — the RPMB/`BOOTCTR` plan, the
  recovered 32-byte key literal, the read-only-first ladder (addenda 1–2 added by
  session 5).
* `HANDOFF-2026-10-27-session5.md` — the handoff to session 11.
* Chainloader: `EP_TICKS` delay + `EPCH` epoch block; blink loops removed.
* Kernel (`kernel/patches/…`, present in both patch styles): WDT2 policy
  (`pb_wdt_kick()`, `PB_WDT_KICK_HZ = 2`, `timer_list`), MPU/32 kHz sampler
  (`pb_state_tick`), the SMC liveness probe + GIC census (gated by
  `PB_SMC_PROBES 0`), the `vemmc` fixed regulator and `&mmc2` supply, `&usb_otg_hs`
  (`mode`, `interface-type`, `power`), `&twl_usb_comparator`, and the USB stack /
  `CONFIGFS_FS` / `OMAP_USB2` / `TWL6030_USB` config changes.
* `initramfs/init.c` — PID 1: mounts, configfs ACM gadget (`0fca:8010`, product
  "PlayBook (Linux bring-up)"), forked `/dev/mem` probes, DRAM-ring logger,
  180 s bounded lifetime, reboot-by-request.
* `initramfs/console.c` — the first version of the REPL.
* `tools/pbssh.sh` — durable SSH wrapper (the `/tmp` one had vanished), plus
  bootstrap §1.1's cold-start procedure and the `lsusb` product-ID table; traps
  11–13 in the bootstrap.
* Images: `local/images/kernel-d{0,1000,2000,3000,4000}.img`,
  `kernel-clean-blinkless.img`, `kernel-console-base.img`,
  `kernel-console-guarded.img`.  Report 115's own table names two console CRCs —
  `0x185F45A7` (banner-at-open) and `0x642BDFBE` (silent-until-input, the one
  verified end to end); hashing those two files is the way to confirm which is
  which (see the CRC caveat above).
* `docs/contradictions/bootctr-drain-vs-session10-readings.md` — written with this
  note.
