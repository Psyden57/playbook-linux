# Session 63 (absolute) — the "SMP-only mount failure" was a rootfs-offset artefact; CPU1 and the 1008 MHz OPP work, but dual-core boot is flaky

> Numbering: absolute `63` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 57** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-16 → 2026-11-17 (two calendar days, one session).

**Unit state at the end (verified off the media):**
* **Region 0 = `local/work/kernel-fastcl-nosmp.img`, CRC `0x8B83421F`**, read off
  media this session: region-0 trailer at raw `0x8B10198` =
  `1f 2d c8 d7 1f 42 83 8b` → magic `0xD7C82D1F`, CRC **`0x8B83421F`**.  This is
  the `#78` SMP zImage with the **fast chainloader** (VC 1.35 V + MPU DPLL
  1008 MHz before the copy) and a `loglevel=0 nosmp` DTB.  It boots to a full
  userland, `possible = 0-1`, `online = 0`, `dpll_mpu_ck = 1008000000`.
* **Region 1 = the busybox ext4**, raw `0x29000000`, 64 MiB (byte count
  `67108864` confirmed), sha256
  `6f5b8ca7b41bb0ac6936fb1e422f63032c68b0ead4c9226c0baca824f2e89325` — drifted
  from the baked `49e318bf…` because the read-back boots mount region 1 rw.
  Modules still do not match the config (trap 320).
* **hd3** = the region-1 ext4 image `49e318bf…` flashed at loader block `0x4915`
  this session; it is the mounted root (`/dev/loop0 on / type ext4,
  rw,relatime,nobarrier`, 58.7 M, 42 % used).
* **OS**: our Linux `7.3.3 #78 SMP` (single core online), **not TabletOS**.
  dropbear on `169.254.0.1:22`, `PB_LINK=usb tools/pbssh.sh` — **after** a
  `pbconsole entropy <hex>` seed each boot (trap 328); ACM `pb> ` is the
  fallback console.  **`BOOTCTR_FCTRY = 0x20`** (`20000000`, above the `0x1e`
  threshold, no write — the Linux `/init` tops it up; it drained from `0x32`
  over the session's many test boots and rebounded via top-up).
* **Device-side files**: none persist — `/tmp` is tmpfs and empty at the end;
  the tools pushed this session (`ringdump`, `pbopp.ko`, `pbvc.ko`,
  `pbi2c2.ko`) were ephemeral and are wiped by reboot.  No restore is required;
  region 0 boots, so there is no need to reflash the 1917 IFS.

## Goal of this session

The handoff's active item: separate "SMP kernel" from "two possible CPUs" for
the root-mount failure (trap 326), then cpu1, then the MPU OPP.  "Done" was a
boot with both cores online and the MPU clocked above the bootloader's
400 MHz.

## Confirmed (measured, read in a file, or seen on the bench)

* **Trap 326 is a misdiagnosis — there was no SMP-only mount bug.**  The SMP
  image's initramfs has `ROOTFS_RAW_OFF = 0x49000000` (hd3, `initramfs/init.c:452`);
  the UP read-back image has `0x29000000` (region 1).  hd3 held the 1917 vendor
  UFS (QNX6): console `qnx6 49000000` mounted it read-only.  The failing loop's
  capacity drop `124583936 → 122191872` sectors is exactly `0x49000000` bytes.
  Flashing the region-1 ext4 (`49e318bf…`) to hd3 at `0x4915` made the **same SMP
  kernel** print `EXT4-fs (loop0): mounted filesystem … r/w without journal`
  and boot a full userland.  [report 256 §1; `local/work/kmsg-nosmp.txt`,
  `local/work/dmesg-smp-nosmp-hd3-root.txt`; media trailer read above]
* **`nosmp` is not a `nr_cpu_ids=1` test** (trap 327): with `nosmp` the kernel
  still printed `SLUB: CPUs=2`, `percpu [0] 0 [0] 1`; it only caps online CPUs
  (already 1).  [report 256 §2; `local/work/r2-cpu1mark.words`]
* **SSH opens the port but never sends a banner until entropy is seeded**
  (trap 328): `/proc/138/wchan` = `wait_for_random_bytes`; `pbconsole entropy
  <64 hex>` credited 256 bits, `random: crng init done`, dropbear's `wchan`
  moved to `poll_schedule_timeout`, SSH worked.  [report 256 §3]
* **CPU1 bring-up works.**  A `kernel-smp-cpu1-mark4.img` (`0x0A80BDC6`) boot
  produced `PBCPU: post smp_init, online cpus=02`, every `PBCPU1:` marker through
  `post complete`, and a running system with `nproc=2`, `online=0-1` and `twd`
  timer interrupts on **both** CPUs (4863 / 5770).  [ring
  `local/work/r2-cpu1ok.{bin,words}`; report 256 §4]
* **`clk_set_rate(dpll_mpu_ck, 1008000000)` works and is stable** via a
  purpose-built loadable module (`local/work/src/pbopp.c`): `clk_summary` showed
  `dpll_mpu_ck = 1008000000`, hardware `CM_CLKSEL_DPLL_MPU = 0x0080D207`,
  `CM_CLKMODE = 0x7` (locked).  A CPU-bound `awk` loop took **13.63 s at 1008 MHz
  vs 34.66 s at 400 MHz (2.54×)**.  **1.2 GHz was tried and crashed/reset the
  unit** — the device re-enumerated and SSH died.  [report 257 §2]
* **The fast chainloader widens the watchdog window dramatically.**  Adding a
  VC 1.35 V command and MPU DPLL 1008 MHz program at chainloader entry (physical
  writes, MMU/caches off) cut kernel entry to **3.276 s of 7.812 s** (chainloader
  copy 0.316 s + XZ decompression **2.959 s**, down from 6.6 s) — ~4.5 s spare.
  The 1008 MHz clock **persists** through boot (no kernel re-program down).
  Three consecutive reboots of the single-core fast-cl image all reached the OS.
  [report 257 §3; `local/work/r2-fastcl.words` timing words;
  `local/work/chainloader_kernel.S.in.session63-orig` is the pre-change copy]
* **VDD_MPU is correctly commanded to the 1 GHz OPP voltage.**  The VC bypass
  (`0x4a307ba0`) reads `0x003A5512` (VSEL `0x3A` = 1.35 V per
  `twl6030_uv_to_vsel()`), and the VC command register
  `PRM_VC_VAL_CMD_VDD_MPU_L` (`0x4a307b98`) reads `0x3A3A1400`.  The battery is
  healthy: `bq27541-0` voltage `3819000` µV, capacity `97`, status `Full`.
  [report 257 §5; register reads this session]
* **Dual-core 1008 MHz boots successfully at least sometimes** — `kernel-ipidbg-cpu1.img`
  (`0xF8264016`) came up as `#91 SMP` with `nproc=2`, `online=0-1`, both cores at
  1008 MHz, `twd` on both (10933 / 11536), stable for >2 min.  But a 3-boot set
  gave **1 success / 2 wedges** — the nondeterminism remains.  [report 257 §4;
  `tools/pbssh.sh` reads this session; operator observed the frozen-cursor
  wedge on the failed boots]

## Inferred (reasoning from evidence, not directly measured)

* The dual-core wedge is a **timing/race in the IPI / TLB-flush path** when two
  cores are active at 1008 MHz — the failure is a clean `execve("/init")` hang
  (all markers through `pre run_init`, or `post run_init FAILED`) rather than a
  reset; `execve` → `flush_old_exec` sends a TLB-flush IPI via
  `smp_call_function_many` → `csd_lock_wait`, and CPU1 may not service it.
  **Moderate confidence**: adding IPI markers did not remove the nondeterminism
  (1/3 success), so this is a race, not a single stuck statement.  Instrumented
  with `PBSMF: pre/post csd_lock_wait` (CPU0) and `PBIPI1: CALL_FUNC enter/done`
  (CPU1) in the tree; the next session should read those markers from a wedged
  boot.
* The fast chainloader helps reliability **by widening the watchdog margin**,
  not by changing anything in the kernel — the 2.5× faster decompression turns
  the marginal 0.8 s into ~4.5 s.  **High confidence** (measured kernel-entry
  times and 3/3 clean single-core reboots).
* Setting the clock in the chainloader before kernel clock-init is **not** by
  itself fatal — the successful dual-core boots used exactly that path.  **High
  confidence** (dual-core `#91` boot had the chainloader DPLL program).

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The ext4 mount differs under SMP" (trap 326 as first written).**  Killed by
  the loop-offset arithmetic (`0x49000000`), the `qnx6` mount of hd3, and the
  flashing experiment: the two images were mounting different filesystems.
* **"Use `nosmp` for a one-possible-CPU test."**  Killed by `SLUB: CPUs=2` —
  `nosmp` caps online CPUs, not `nr_cpu_ids`.
* **"CPU1 hangs in `smp_init()`."**  Killed by the full marker trace (all
  `PBCPU1:` markers fire, `online cpus=02`) and a successful dual-core boot with
  both CPUs servicing `twd` interrupts.
* **"SSH is broken when the port is open."**  Killed by `/proc/<dropbear>/wchan`
  = `wait_for_random_bytes` and the entropy-seed fix.
* **"1.2 GHz should be doable."**  Killed by an immediate crash/reset (device
  re-enumerated, SSH died) at the 1.35 V / 1.10 V OPP.
* **"The dual-core wedge is a power problem."**  Killed by the healthy battery
  (97 %, 3.819 V) and the correctly commanded 1.35 V MPU rail — dual-core still
  wedges with power proven good.
* **"The IPI markers will pin the stall / fix it."**  Killed by 1-success /
  2-wedge after adding them — they perturb timing but do not remove the race.

## Unknown / open questions

* **What exactly is the dual-core IPI race?**  Settle by booting the IPI-marker
  kernel (`kernel-ipidbg-cpu1.img`) until it wedges, capturing the ring via the
  read-back image, and reading the last `PBSMF`/`PBIPI1` markers — which shows
  whether the IPI is sent, received, or serviced.  The chainloader's VC/DPLL
  block is in `boot/chainloader/chainloader_kernel.S.in`.
* **Would a higher VSEL (0x3B/0x3C) help?**  Probably not (power is ruled out),
  but untested; would need a chainloader re-cut and a dual-core boot.
* **Is 1008 MHz stable long-term / thermally?**  Only minutes-long soaks were
  done.  `cpu_thermal` should be read over a sustained dual-core load.
* **What ABB/SmartReflex setup does the reference use at 1008 MHz?**  Report 223
  shows `PRM_VC_*` programmed but no voltage commanded in startup-playbook; the
  actual sequencing is in `setup-core-inactive` (report 219 §5), not read.
* **Does a QNX boot still wipe the ring?**  Unchanged; the contradiction file
  stays open (the hand-off sanitises the case).

## Superseded / uncertain

* **Trap 326 and report 255 §5's "SMP-only root-mount failure"** are withdrawn
  by report 256.  STATE/HANDOFF/ROADMAP/DECISIONS/reports-INDEX were updated.
* **Trap 329 as first written ("cpu1 hangs in `smp_init()`")** is corrected this
  session: CPU1 brings up fine and dual-core boots at least sometimes; the
  remaining problem is a nondeterministic IPI/TLB-flush race.
* **The initial plan to "test `SMP=y` + `NR_CPUS=1` and `nosmp`"** was based on
  the false premise that the mount failure was SMP-intrinsic.
* **This session's own "it's a voltage problem"** (after the first dual-core
  wedges at 1008 MHz) is superseded by the 1.35 V command and healthy battery
  measurements above.

## Artefacts produced

* Reports **256** (`docs/reports/256-smp-mount-misdiagnosis-and-cpu1-hang-2026-11-16.md`)
  and **257** (`docs/reports/257-fast-chainloader-1008mhz-opp-2026-11-17.md`).
* Traps 326 corrected, 327–331 added; DECISIONS §90/§91; STATE, HANDOFF, ROADMAP,
  reports/INDEX, NUMBERING, report 255 §0 headline fix.
* Images (host, gitignored): `kernel-smp-nosmp.img` (`0x6D6F52C0`),
  `kernel-smp-cpu1.img` (`0x182B0E00`, bad), the `*-mark{,2,3,4}` marker images,
  `kernel-fastcl-nosmp.img` (`0x8B83421F`, **on media**),
  `kernel-fastcl-cpu1.img` (`0xBFA05CAD`), `kernel-vcfast-cpu1.img`
  (`0x4C8AC77B`), `kernel-vcfast-maxcpus1.img` (`0x09DDA895`),
  `kernel-ipidbg-cpu1.img` (`0xF8264016`), plus carved DTBs in
  `local/work/carve/`.
* **Chainloader change (tracked)**: `boot/chainloader/chainloader_kernel.S.in`
  gains a VC 1.35 V command + MPU DPLL 1008 MHz program at entry (chainloader
  grew 852 → 1000 B); the pre-change copy is
  `local/work/carve/chainloader_kernel.S.in.session63-orig`.
* **Kernel tree instruments (gitignored; remove before any patch re-export)**:
  `PBCPU:`/`PBCPU1:`/`PBIPI1:`/`PBSMF:` markers in `init/main.c`,
  `arch/arm/kernel/smp.c`, `omap-smp.c` and `kernel/smp.c`, on top of the
  session-62 `PBDBG` set and `head.S` CRR stamp.  Pristine copies where saved:
  `local/work/carve/main.c.session63-orig`, `omap-smp.c.session63-orig`.
  `.config` remains `CONFIG_SMP=y`, `CONFIG_NR_CPUS=2`.
* Modules (host, gitignored): `local/work/src/{pbopp,pbvc,pbi2c2}.c/.ko` —
  the OPP/VC/I²C experiment modules; vermagic `7.3.3 SMP`.
* Captures (host, gitignored): `local/work/r2-cpu1mark*.{bin,words}`,
  `r2-cpu1ok.*`, `r2-fastcl.*`, `r2-fastmark4-wedge.*`,
  `dmesg-smp-nosmp-hd3-root.txt`, `kmsg-nosmp.txt`, flash logs.
