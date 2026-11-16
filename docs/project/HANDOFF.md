# Handoff — session 61 (absolute) → session 62

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**, with
a GPU-accelerated compositor.

**THE ACTIVE ITEM: the CPU (second core, then the MPU OPP).**  The unit runs one
core at 400 MHz.  This session built the SMP kernel and found where it stops:

* **`CONFIG_SMP=y` + `CONFIG_NR_CPUS=2` + `maxcpus=1` boots** — the trace proves
  it (`SLUB: … CPUs=2`) — and **stops right after `rcu_init_geometry()` printed
  `rcu: RCU calculated value of scheduler-enlistment delay is 10 jiffies.`**
  (`adjust_jiffies_till_sched_qs()`, `kernel/rcu/tree.c:474`), with no oops text;
  the loader's WDT2 then resets it.  Report **254**, trap **315**.
* **No hardware blocks the platform SMP path.**  From non-secure Linux:
  `SCU_CONFIG 0x48240000 = 0x00000001` (2 cores), `SCU_CTRL 0x48240004 = 0x00000531`
  (enabled), `0x4824380c = 0`, WUGEN `0x48281800/4 = 0`, PL310 ID `0x410000C4`,
  SAR `0x4a326000 = 0xC80E2E93`, `CM_CLKSEL_DPLL_MPU = 0x00800000`,
  `CM_CLKMODE_DPLL_MPU = 0x5` (locked).  `SMC` is merely privileged (`SIGILL` in
  a QNX user process even for `0x102`, which our kernel uses).  Report 254 §3–§4,
  trap 319.
* **The reference is dual-core at 1008 MHz** — `pidin info` on the live oracle,
  `Processor1` *and* `Processor2`.  That is the MPU OPP target (trap 312).

**The next experiment is a measurement-method fix, not a kernel experiment.**
Before any marker run can be believed, the writer of the ring's `+0x1000` printk
window must be identified (trap **317a**): it is not `ring_put` (count/index stay
at the decompressor's `0x32`) and `drivers/misc/pbmon.c` has no printk hook, yet
its content changes per boot and a *more* chatty kernel left a *shorter* window.

## 0a. THE NEXT EXPERIMENT — identify the `+0x1000` window's writer

**Why first.**  Three `PBDBG` marker builds this session were silent because they
were made while the tree DTS had `earlyprintk` removed, and the one that did carry
`earlyprintk` produced an impossible (shorter) trace.  Until the window's writer
is known, marker work cannot localise the hang.

**Inputs (nothing to build).**  The unit is already on
`local/work/kernel-region1.img` (CRC `0x433232F1`) with `PB_LINK=usb tools/pbssh.sh`
and the diagnostic loop of §0b below.

**The one experiment, exactly:**

1. Put a **unique, unmistakable** boot-time line in the read-back kernel — e.g.
   `pr_info("PBWINPROBE readback 0x5a5a0001\n")` as the *first* statement of
   `start_kernel()` in `init/main.c` — rebuild that image **and check it**:
   `grep -ac earlyprintk <image>` must be `0` (this is a read-back image), and
   the new string must be findable in the zImage.
2. Run the §0b sequence with (a) the plain SMP image `kernel-0x202E9F38-smp.img`
   and then (b) this read-back image, and dump `0x90000000` with
   `local/work/ringdump` + `xxd -p`.
3. **Read the result:**
   * the string is in the window **and** the SMP trace survived → the window is
     written by the *read-back* boot, so it is a live printk mirror and the
     `<n> chars` window belongs to the newest boot; marker runs are then valid
     and §0c is next;
   * the string is in the window **and** the SMP trace is gone → the window is
     the read-back boot's own mirror (i.e. it is always the newest boot's) and
     the 17:05 trace we read was the *read-back* image's, not the SMP image's —
     so the whole "stops after the RCU line" attribution must be re-measured with
     the SMP image as the *last* boot before the read (which is impossible) or
     with another instrument (see §0c's alternatives);
   * the string is **not** in the window → the window is written by neither the
     read-back kernel nor `ring_put`; search `startup`/`procnto`-style writers in
     our own tree (`grep -rn "0x1000" drivers/misc/ arch/arm/kernel/`) and in the
     QNX side, and record the answer in trap 317a.

## 0b. The ring-capture procedure (works; reuse it)

The unit must be on the region-1 image (`local/work/kernel-region1.img`,
`0x433232F1`) for the zeroing and the read-back shell.  `local/work/ringclean3.sh`
implements the whole loop; the pieces:

1. build the two instruments static (`arm-linux-gcc -static -O2 -Wall -o
   local/work/memzero tools/bench/memzero.c`, same for `ringdump`) and push them
   to `/tmp` on the unit;
2. arm the flasher for the image under test
   (`tools/bbusb.py flash --component <img> --dest-block 0x825 --from-bootrom
   --password 0000 --apply`, in a retry loop — `local/work/*-flash-retry.sh`);
3. `ssh '… /tmp/memzero 88000000 8000; /tmp/memzero 90000000 8000;
   /tmp/memzero 94000000 8000; sync; reboot -f'` — this zeroes the rings and
   resets into the bootrom so the flasher catches it;
4. let the image under test run (25 s is enough for an early hang);
5. flash the **read-back image** (`local/work/kernel-dump3.img`, `0x11D29723`:
   non-SMP, `earlyprintk` removed, `CONFIG_DEBUG_LL=n`, `/init`'s `rlog`
   disabled, region-1 root) and boot it;
6. `/tmp/ringdump 88000000 8000 /tmp/r1.bin; /tmp/ringdump 90000000 8000
   /tmp/r2.bin; xxd -p /tmp/r1.bin` → decode on the host.

**Facts that make this necessary** (traps 316/317): the chainloader
`clear_ring 0x88000000` on every boot; `busyuart` mirrors every debug char into
all three rings; **`dd if=/dev/mem` cannot read those pages** (`Bad address`,
`no-map`) — `mmap` can; a QNX boot leaves `0xAA` fill (contradiction file
`docs/contradictions/ring-survives-a-qnx-boot.md`).

## 0c. Then, in order

1. **Bracket `rcu_init()`** — but only after §0a.  Markers before/after
   `rcu_init_one`, `rcutree_prepare_cpu`, `rcutree_report_cpu_starting`,
   `rcutree_online_cpu`, the two `alloc_workqueue()`s,
   `start_poll_synchronize_rcu_expedited`.  **Check every marker image with
   `grep -ac earlyprintk` first** (trap 315).
2. **Test the "one possible CPU" class**: a build with `CONFIG_SMP=y` and
   `CONFIG_NR_CPUS=1` (cannot boot cpu1, but keeps every SMP code path with one
   possible CPU), and a plain SMP build with `nosmp` on the cmdline.  If either
   boots, the trigger is `nr_cpu_ids = 2` with one online CPU, and the fix is
   likely in the per-CPU/percpu init path rather than in mach-omap2.
3. **Then cpu1 proper** (remove `maxcpus=1` from the DTS) — the SCU, the WUGEN
   `AUX_CORE_BOOT` registers and the ROM API are all demonstrably usable.
4. **Then the MPU OPP**: the reference's measured rate is **1008 MHz**; the
   voltage/ABB are still unmeasured (`PRM_VC_*` reads die in QNX user space — read
   them from a *Linux* `devmem` instead, they are accessible: `0x4a307ba0` area).

## 0d. The media, and the way back

* **Region 0 = the region-1 image, CRC `0x433232F1`** (verified on media:
  `bbee7beb…`, trailer `1f2dc8d7 f1323243`).
* **Region 1 = the busybox root**, raw `0x29000000`, 64 MiB, now `a3277272…`
  (baked `49e318bf…`; mounted rw).  **Its modules no longer match the config**
  (trap 320) — rebuild them before using it as a real bench root.
* **hd3 = the 1917 UFS** (the Nura root was written away this session).
* **The oracle** is one region-0 flash away: `local/work/ifs-shim-sealed.ifs`
  (`b52195bb…`) at `0x825`, with the full modified 1917 RCFS
  (`local/work/rcfs-1917.rcfs`, `c4ab6e34…`, packed
  `local/work/rcfs1917-flash.signed`) at `0x8D5` — that combination gave
  `0fca:8012`, `qconn` on `169.254.0.1:9090` (`python3 tools/QCL.py …`) and
  `sshd` once.  The patched minimal RCFS (`minimal/rcfs-noufs.rcfs`) and
  `setB.signed` did **not** bring the network up.
* **The way back to the Nura root:** flash
  `local/work/nura/images/blackberry-playbook-root-apps-audio.img`
  (`f7858110…`, 625 999 872 B) at `0x4915`; `/init` regrows it and reinstalls the
  SSH kit, but the session-59/60 live fixes must be re-applied from the host
  payloads (`local/work/llvmpipe/`,
  `local/work/nura/pmaports/main/{epitaph,catacomb}/`, the udev/polkit rules).
* **Do NOT restore** `local/images/ifs0.sealed.bin` (trap 84).

## 0e. What this session changed (do not redo)

* SMP kernel built (`0x202E9F38`): `CONFIG_SMP=y`, `CONFIG_NR_CPUS=2`,
  `maxcpus=1` kept; all 44 modules rebuilt (vermagic gains `SMP`) and installed;
  vermagic verified.  The tracked config is **unchanged** (still non-SMP) —
  re-apply `scripts/config -e SMP --set-val NR_CPUS 2` + `olddefconfig` when
  continuing.
* The whole ring-capture method and three tools
  (`tools/bench/{ringdump,memzero,smcprobe}.c`).
* The oracle measurements (dual-core 1008 MHz; register accessibility; SMC is
  privileged) — report 254 §4, capture
  `docs/captures/oracle-live-2026-11-16-ring-and-cpu.txt`.
* The UFS/RCFS work that produced a bootable oracle (`ufs-1917-devuser.*`,
  `rcfs1917-flash.signed`) — **and the loss of the Nura root** (operator's call).
* No kernel/DTS/config change persisted → **no kernel-patch re-export and no tree
  walk were needed** (the tree is back to canonical: no `PBDBG`, non-SMP config,
  `ROOTFS_RAW_OFF 0x49000000`, `earlyprintk` restored).

## 0f. Traps added this session (full list 14–320 in `KNOWN-ISSUES.md` §2)

315 SMP stops after the RCU geometry print (and a marker build must carry
`earlyprintk`); 316 ring writers, the `no-map` `dd` failure, the read-back
recipe; 317 QNX's `0xAA` fill (open — contradiction file); 317a the `+0x1000`
window's unknown writer; 318 the RNDIS interface-name flap vs the NM profile;
319 SMC is privileged (not evidence about the ROM); 320 the region-1 module
mismatch.

## 1. Useful local extracts

* `local/work/kernel-region1.img` (`0x433232F1`) — **the bench image**;
  `local/images/kernel-0x202E9F38-smp.img` — the SMP build;
  `local/work/kernel-dump{,2,3}.img` — the read-back variants.
* `local/work/ringclean3.sh`, `local/work/*-flash-retry.sh` — the capture loop.
* `tools/bench/{ringdump,memzero,smcprobe}.c` — the new instruments (sources;
  the built static binaries are `local/work/{ringdump,memzero}`).
* `local/work/minimal/` — the pruned/stage-A RCFS, `setB.signed`, `memdump3`.
* `local/work/{rcfs-1917.rcfs, rcfs1917-flash.signed, ufs-1917-devuser.ufs}`.
* `kernel/patches/0001-playbook-winchester-7.3.3.patch` — unchanged this session.
* Reports **254** (current), 253–249; sessions `docs/sessions/session-61.md`.

## 2. Build and flash (7.3.3)

`REPO=$PWD; . tools/env.sh`; `K=local/kernel-tree`.  Kernel:
`rm -f $K/usr/initramfs_data.cpio*; make -C $K ARCH=arm CROSS_COMPILE=arm-linux-
-j12 zImage; make -C $K … ti/omap/omap4-winchester.dtb`; wrap with
`ZIMAGE=… DTB=… python3 tools/build_kernel_image.py` (prints the CRC; must stay
under `0xB00000`).  Modules **in-tree**, strip `--strip-debug`.  **A config
change (e.g. `CONFIG_SMP`) changes the module vermagic — rebuild and install every
module before the region-0 flash.**  A `-j12` kernel build is ~7 min here; hand
long builds to the operator.

## 3. Bench

* SSH is the primary shell on the region-1 image: `PB_LINK=usb tools/pbssh.sh`.
  The ttyGS0 prompt is `pb> ` (region-1 root) or the initramfs console (when the
  root does not mount) — `pbcon.py` drives only the former (trap 274).
* **Host link:** the PlayBook's RNDIS interface flaps between `enp8s0f3u1` and
  `enp8s0f3u1i2`; NM's `pb-rndis` profile must be bound by MAC
  (`02:1a:11:00:00:02`), not by name (trap 318).  Do not add the address by hand.
* Reboot: `reboot -f` over ssh.  The flash: start the flasher first, then reset;
  one bootrom session; `--from-bootrom` needs the unit booted (trap 266) unless it
  is already in the bootrom after a WDT reset — a boot-looping image therefore
  feeds the flasher by itself.
* The oracle: `python3 tools/QCL.py info | exec | put | get | shell`
  (`QCL.py exec` truncates long replies and needs `--` before `-flags`; compound
  commands want `QCL.py shell`).  `PB_LINK=door tools/pbssh.sh` works only while
  the oracle's `sshd` is alive (it died after one connection here).

## 4. Working rules

* One variable per boot; a test boot is not a budget concern, but a boot-loop is —
  stop it, and read the counter at both ends of a cycle (`dmesg | grep pbrpmb`).
* Read dmesg **unfiltered** after a boot or structural change; do not run two
  console commands in parallel.
* Never present a guess as a fact; prefer a fresh read.  Look up external facts
  from the primary source.
* **Check a marker/read-back build for `earlyprintk` before believing a trace**
  (trap 315) — this session lost time to exactly that.
* Revert experiments and prove the revert; re-export the kernel patch when the
  DTS/config/driver changes persist (trap 161), then apply-and-compare and tree
  walk (trap 211).
* Do not take screenshots; ask the operator to look.  Only the operator decides
  when to stop / wrap up.

## 5. Paste-ready kickoff prompt (session 62)

```
PlayBook (OMAP4430 "winchester") — session 62 (absolute; Linux-era 56)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md items 7a and 9, KNOWN-ISSUES.md §2 (traps 14-320, especially
312-320) and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md (84-88), reports
254, 253 and 249-252, docs/captures/oracle-live-2026-11-16-ring-and-cpu.txt,
docs/contradictions/ring-survives-a-qnx-boot.md and sessions/session-61.md.

WHERE IT STANDS: THE ACTIVE ITEM IS THE CPU.  The unit runs one core at 400 MHz.
Session 61 built CONFIG_SMP=y + CONFIG_NR_CPUS=2 with maxcpus=1: the kernel
BOOTS (SLUB: ... CPUs=2) and STOPS right after rcu_init_geometry() printed
"rcu: RCU calculated value of scheduler-enlistment delay is 10 jiffies."
(kernel/rcu/tree.c:474) — no oops text, then WDT2 resets it (report 254, trap
315).  The SCU and every register the SMP path touches are accessible from
non-secure Linux (SCU_CONFIG=1 = 2 cores, SCU_CTRL=0x531, 0x4824380c=0,
0x48281800/4=0, PL310 0x410000C4), and SMC is merely privileged — so this is not
a firewall/ROM problem.  The reference is measured dual-core at 1008 MHz (the
MPU OPP target).  The finer localization is NOT established: the PBDBG marker
runs were invalid (builds made without earlyprintk) or inconsistent, because the
ring's +0x1000 printk window has an unidentified writer (trap 317a).

THE NEXT EXPERIMENT (HANDOFF §0a, no kernel build needed): identify that writer.
Put a unique printk as the first statement of start_kernel() in a read-back
image, run the capture loop (HANDOFF §0b: zero the rings, boot the SMP image,
boot the read-back image kernel-dump3.img, dump 0x90000000 with
tools/bench/ringdump), and interpret per HANDOFF §0a.  Then bracket rcu_init()
(§0c) — checking every marker image with `grep -ac earlyprintk` first.

THE FACTS: region 0 = local/work/kernel-region1.img, CRC 0x433232F1, verified on
media (sha256 bbee7beb... at raw 0x8100000, trailer 1f2dc8d7 f1323243); it is the
non-SMP kernel with the initramfs rooted at region 1.  Region 1 = the busybox
root at 0x29000000, 64 MiB, sha256 a3277272... (baked was 49e318bf...), 43
modules that NO LONGER MATCH the config (trap 320).  hd3 = the 1917 vendor UFS
(the Nura root is gone; way back: local/work/nura/images/
blackberry-playbook-root-apps-audio.img, f7858110..., at 0x4915).  BOOTCTR_FCTRY
= 0x31 at the last boot (self-maintaining, threshold 0x1e).  Region 0's RCFS is
the full modified 1917 one, so the oracle is one IFS flash away
(ifs-shim-sealed.ifs at 0x825) and answers qconn on 169.254.0.1:9090.

PHYSICAL STATE: the unit runs the region-1 busybox root, SSH dropbear on
169.254.0.1:22 (PB_LINK=usb tools/pbssh.sh); the ACM is the fallback console.
The host's RNDIS interface name flaps (enp8s0f3u1 / enp8s0f3u1i2) — bind the NM
pb-rndis profile by MAC 02:1a:11:00:00:02 (trap 318), never add the address by
hand.  No device-side files are deployed (/tmp is tmpfs).  Pace bulk pushes
(8 KiB/5 ms; trap 302).  To boot the ORACLE: ifs-shim-sealed.ifs at 0x825;
qconn/QCL.py for a shell.  Do NOT restore local/images/ifs0.sealed.bin (trap 84).

BENCH: SSH is the primary shell; `reboot -f` to reset.  THE FLASH: start the
flasher first, then the reset; one bootrom session; 60 s window; --from-bootrom
uploads the ramloader so the unit must be booted (trap 266) unless it is in the
bootrom after a WDT reset.  An agent NEVER writes RPMB; NVRAM read-only.  Read
dmesg UNFILTERED.  One command at a time.  A boot-looping image drains
BOOTCTR_FCTRY: stop it and read the counter with `dmesg | grep pbrpmb`.

RING WORK: only the boot that immediately follows the failure can be read.  The
chainloader clears ring1 on every boot; busyuart mirrors chars into all three
rings; dd on /dev/mem FAILS on the no-map pages — use tools/bench/ringdump (mmap)
and tools/bench/memzero; a QNX boot leaves 0xAA fill.  ALWAYS check a
read-back/marker image with `grep -ac earlyprintk` before trusting its trace.

BUILDS: modules IN-TREE, strip --strip-debug; a config change (CONFIG_SMP)
changes the module vermagic — rebuild every module before the flash.  A -j12
kernel build is ~7 min; hand long builds to the operator and stop the turn.  The
initramfs binaries are built separately (arm-linux-gcc -static -O2) and the cpio
must be removed.  The kernel export only changes if the DTS/config/driver change
persists (trap 161), then apply-and-compare + tree walk (trap 211).

SAFETY: read SAFETY.md first.  The hard hazards (BOOTCTR_FCTRY 0 / >0x32 / failed
write / flat pack) remain; a test boot is not a budget concern but a loop is.
Flash region 0 only when only it changed; both in one bootrom session only when
both changed.  Operator's call when to stop / wrap up.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.

WRAP-UP: only the operator decides when the session stops.  Record the bench
state (verified off the media), write the session note, update the canonical
docs, run tools/docs_audit.py and tools/tests/test_offline.py, then commit
privately and publish the snapshot (docs/project/REPO-MANAGEMENT.md).
```
