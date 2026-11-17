# Session 62 (absolute) — the ring was lying, and the loader's watchdog was killing the SMP boot

> Numbering: absolute `62` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 56** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-16.
Unit state at the end (read off the media through `/dev/mmcblk1` on the unit):
region 0 = the **read-back image `0x5BBCD22E`** — its 10 355 580 bytes at raw
`0x8100000` hash to
`6412b6e7af547915e385f3edfec254128cae2a5213885d741954589c6953fb35`, equal to
`local/work/kernel-readback-quiet.img`, with `LoadEnd = 0x80ADFB7C` at image
`+0x08` and the trailer `1f2dc8d7 2ed2bc5b` = magic `0xD7C82D1F` + CRC
`0x5BBCD22E` at that offset.  Region 1 = the busybox root at raw `0x29000000`,
64 MiB, now `ae2b1d6991f3067e0b56f5177155dacfe568af0ab4a66880e7b0008e567ab0c1`
(mounted rw, so it has drifted from the baked `49e318bf…`).  The unit runs
**Linux `7.3.3 #79` (UP)**, root `/dev/loop0` on region 1
(`ext4 rw,relatime,nobarrier`, 58.7 M, 42 % used), `pb> ` on the ACM and
dropbear on `169.254.0.1:22` (`PB_LINK=usb tools/pbssh.sh`).
**`BOOTCTR_FCTRY = 0x28`** (`28000000`, above the `0x1e` threshold, no write).
No device-side files are deployed (`/tmp` is tmpfs and empty).  hd3 = the 1917
vendor UFS.

## Goal of this session

The handoff's active item: identify the writer of the ring's `+0x1000` "printk
window" (trap 317a), then use a trustworthy ring to localise the SMP failure.

## Confirmed (measured, read in a file, or seen on the bench)

* **There is no `+0x1000` writer.**  `+0x1000` is the ring's character array at
  **character #3840**, exactly the first byte past the old `clear_ring`
  (`+0x80..+0xFFF`); the only writer of the page is `ring_put` in
  `arch/arm/include/debug/omap4bc.S`, which is also the kernel's `earlyprintk`
  path.  [report 255 §1; live ring: text flows across `+0x1000` mid-line,
  `index == count mod 0x7F00`; offsets 3822/3959/4294 explain both captures]
* **A failed boot's log was being erased, not misread**: an early death prints
  < 3840 characters, so nothing reached `+0x1000`, and the read-back boot's
  chainloader zeroed `+0x80..+0xFFF`.  [report 255 §2]
* **The chainloader now hands the ring off** (append-only stream, `RHND` record
  with the last two entry headers and the previous boot's timing words),
  verified byte-exactly: after a boot the delimited logs are the two most recent
  boots and `index == (prev_index + length) mod 0x7F00`.  [trap 321,
  DECISIONS §89]
* **`rcu_init()` completes.**  `kernel-smp-rcu2.img` (`0x4F6CC563`, `#85 SMP`)
  prints every `PBDBG` marker inside it through `rcu_init done`, then
  `kvfree_rcu_init`/`trace_init`.  [report 255 §3;
  `docs/captures/ring-handoff-2026-11-16.txt`]
* **The stop point varies on one image**: plain SMP `#78`, seven boots, stopped
  at 3208 (×2), 3943 (×3) and 4211 characters (the last truncated mid-line
  inside `RCU calculated value…`).  [report 255 §3; same capture]
* **The loader's 7.8125 s WDT2 window is the cutter.**  A WDT2 `CRR` sample in
  `head.S` at kernel entry (ring2 `+0x18`, saved by the chainloader to `+0xd0`)
  gives: chainloader entry → jump **0.832 s**, jump → kernel entry (XZ
  decompression) **6.619 s**, i.e. **7.450 s of 7.812 s — 362 ms left**, while
  the kernel needs ~0.74 s to reach its own re-program.  [report 255 §4;
  trap 325]
* **The fix works: the SMP kernel boots to userspace.**  Eight-word
  `LDMIA`/`STMIA` copy in the chainloader **plus** `loglevel=0`:
  `kernel-smp-quiet.img` (`0xA2FA490E`) comes up with
  `possible = 0-1`, `online = 0`, cmdline `loglevel=0 maxcpus=1 …` (read over
  `pbcon.py`).  [trap 325]
* **The next blocker is SMP-only**: the initramfs fell back to its console with
  `EXT4-fs (loop0): VFS: Can't find ext4 filesystem`, while the same region-1
  filesystem mounts cleanly on the UP image minutes later.  [trap 326]
* **The OPP table already exists** in `omap443x.dtsi` (300/600/800/1008 MHz,
  1.025–1.375 V) and `CONFIG_CPUFREQ_DT=m` never loads; live clocks are
  `dpll_mpu_ck = dpll_mpu_m2_ck = div_mpu_hs_clk = 400000000`.  [trap 312
  corrected]
* **The bootrom can hold ~20 s** before it runs the chainloader (operator), and
  a **power-off wipes the ring** (DRAM) — both cost boots this session.
  [traps 323, 321]

## Inferred (reasoning from evidence, not directly measured)

* The `NR_CPUS=2` kernel is affected *more* than the UP one simply because its
  zImage is 196 KB larger, so its decompression overruns a window the UP image
  just fits — moderate confidence, consistent with the measured 6.619 s.
* The SMP root-mount failure is a timing/geometry effect in the loop/ext4
  bring-up rather than corruption: the loop capacity-change messages precede it
  and the FS mounts on UP immediately after — moderate confidence.
* CPU1 is parked, not running: session 61 read `AUX_CORE_BOOT_0 = 0` (the GP
  release bit clear) and `AUX_CORE_BOOT_1 = 0`, which is what upstream's
  `omap4_smp_maybe_reset_cpu1()` treats as "not running" — so the
  "CPU1 corrupts DRAM" hypothesis is unlikely, though not excluded.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The `+0x1000` window has its own writer (pbmon or similar)."**  Killed by
  the layout arithmetic (`+0x1000 - 0x100 = 3840`; the old clear stopped at
  `+0xFFF`), by a tree-wide grep (only `ring_put`, the io aliases and
  `pb_event`), and by the live ring's mid-line continuation across `+0x1000`.
* **"The watchdog cannot be the cutter."**  My own first analysis: I bounded the
  decompression at < 6.27 s from a working boot's re-program timestamp and
  concluded a time cut was implausible.  Killed by the direct `CRR` measurement
  (6.619 s, 362 ms left) — corrected in the same session (report 255 §4).
* **"Only one core online is the bug."**  Killed by the source order: even a
  working SMP kernel has one online CPU at that stage (`smp_prepare_cpus()` and
  `smp_init()` run from `kernel_init`, after `start_kernel()` returns, which the
  failing kernel never reached).
* **"The chainloader is the problem."**  The same chainloader boots the UP kernel
  of the same tree to a full userland, and the failing logs show the SMP kernel
  reading its DTB, reserved-memory map and cmdline correctly.
* **"Extend WDT2 from the chainloader."**  It wedged before the kernel ran and
  left the timer stopped: dark unit, no USB, no periodic reset for minutes; only
  a power-button power cycle recovered it (twice reflashed).
  [trap 322; `local/work/kernel-smp-wdt.img` is that bad image]
* **"Arm the read-back flash 10 s after the test flash."**  The bootrom can hold
  ~20 s, so the image under test had not even run — the ring showed no log for
  that boot and it looked like "printed nothing".  [trap 323]
* **"`marks`/`marks2` carry usable markers."**  Their DTBs have no
  `earlyprintk`, so they are silent by construction.  [trap 324]

## Unknown / open questions

* **Why does the region-1 ext4 loop mount fail under SMP but not UP?**  Settle
  with `SMP=y` + `NR_CPUS=1` and `nosmp`, and with a retry/delay in the
  initramfs loop setup (trap 326).
* **Can CPU1 be started at all?**  Settle by removing `maxcpus=1` now that the
  kernel can reach its own watchdog re-program.
* **Why does the loop geometry change twice** (`0 → 124583936 → 122191872`)?
  Settle by instrumenting `rootfs_boot()`'s loop setup.
* **The MPU voltage/ABB for 1008 MHz.**  The rate is measured; the voltage is
  not — read `PRM_VC_*` (`0x4a307ba0`) from a *Linux* `devmem`.
* **Does a QNX boot still wipe the ring?**  Unchanged from session 61; the
  contradiction file `docs/contradictions/ring-survives-a-qnx-boot.md` stays
  open (the hand-off sanitises the case).

## Superseded / uncertain

* **Session 61's "the SMP kernel stops right after the RCU geometry print"
  (trap 315 as first written, report 254).**  Withdrawn: the ring was showing
  another boot's text.  Report 255 is current.
* **Report 254's "the SCU is fine but the finer localization is not
  established".**  The localization now is: the loader's watchdog, not a
  statement.
* **This session's own first conclusion that "the watchdog is not the
  cutter".**  Withdrawn by the `CRR` measurement (report 255 §4).
* **Trap 312's "the DTS has no `operating-points`".**  Wrong: the table comes
  from `omap443x.dtsi`.
* **"Region 1 is a usable bench root."**  It mounts on UP, but its modules still
  do not match the config (trap 320).

## Artefacts produced

* `boot/chainloader/chainloader_kernel.S.in` — `ring_handoff` (sanitise +
  append-only + two entry headers + timing words) and a block `copy_words`;
  chainloader 852 B (was 828).
* `tools/pb_ring.py` — hand-off-aware decode (`--boot prev|cur|all`) and the
  previous boot's timing line.
* Report **255** (`docs/reports/255-ring-window-identified-handoff-2026-11-16.md`);
  capture `docs/captures/ring-handoff-2026-11-16.txt`.
* Traps: 312 and 315 corrected, 317a closed, 316 revised, 321–326 added;
  DECISIONS §89; STATE, HANDOFF, ROADMAP, reports/INDEX, NUMBERING.
* Images (host, gitignored): `kernel-readback-quiet.img` (`0x5BBCD22E`, on
  media), `kernel-smp-quiet.img` (`0xA2FA490E`, boots to userspace),
  `kernel-smp-loud.img` (`0x3BD64F22`), `kernel-smp-mark62.img` (`0x0A70D405`),
  `kernel-smp-wdt.img` (**bad** — the rejected WDT step), and
  `local/work/carve/` (carved `zImage`/DTB for five images, `carve.py`,
  `main.c.session62-orig`, `region1-quiet.dtb`, `smp-quiet.dtb`).
* Ring captures (host, gitignored): `local/work/r2{b,c,e,f,g,h}.bin`; recovered
  logs `local/work/{rcu2-log.txt,smp-plain-log.txt,mark62-log.txt}`.
* Tree instruments (gitignored, **remove before a patch re-export**): the
  `PBDBG` set in `local/kernel-tree/init/main.c` and the WDT2 `CRR` stamp in
  `local/kernel-tree/arch/arm/kernel/head.S`; `local/kernel-tree/.config` is now
  `CONFIG_SMP=y`, `CONFIG_NR_CPUS=2`.
