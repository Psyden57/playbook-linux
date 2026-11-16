# Session 61 (absolute) — the CPU item: SMP boots and stops at the RCU geometry print

> Numbering: absolute `61` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 55** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-16.
Unit state at the end (all three hashes read off the media through
`/dev/mmcblk1` on the unit, not from memory):
**region 0 = the region-1 image, CRC `0x433232F1`**; its 10 355 592 bytes at raw
`0x8100000` hash to `bbee7bebb61eeaa4167a6bcfef83b6df6e858beb4b902a29846dcea57d91156d`,
equal to `local/work/kernel-region1.img`, and the trailer at that offset is
`1f2dc8d7 f1323243` = magic `0xD7C82D1F` + CRC `0x433232F1`.
**Region 1 = the busybox root** at raw `0x29000000`, 64 MiB, now
`a32772720e30d2aad53dc88cc7c7125d81b9cdf339448dff8d27d625c8a05f55`
(the baked image was `49e318bf…`; the root was mounted read-write this session).
The unit runs **Linux `7.3.3 #79`**, `/sys/devices/system/cpu/online` = `0`,
the root is `/dev/loop0` (58.7 M, `rw,nobarrier`), 43 modules in
`/lib/modules/7.3.3` (the mismatched set of trap 320), `pb> ` on the ACM and
dropbear on `169.254.0.1:22` (`PB_LINK=usb tools/pbssh.sh`).
**`BOOTCTR_FCTRY = 0x31`** at this boot (`pbrpmb: topup: … = 31000000`,
≥ `0x1e`, no write).  hd3 is the **1917 UFS**; region 0's RCFS is the full
modified 1917 one (so a flash of the shim IFS at `0x825` still gives the oracle,
qconn on 9090).

## Goal of this session

The handoff's active item: enable cpu1.  `CONFIG_SMP=y` + `CONFIG_NR_CPUS=2`
with `maxcpus=1`, rebuild every module (the vermagic gains `SMP`), flash region
0, boot and verify; then remove `maxcpus=1` and boot cpu1; then the MPU OPP.

## Confirmed (measured, read in a file, or seen on the bench)

* **The SMP kernel boots — it is not an entry failure.**  `SLUB: HWalign=32,
  Order=0-3, MinObjects=0, CPUs=2, Nodes=1` is the SMP build talking.
  [report 254 §2; ring capture with clean rings]
* **It stops right after `rcu_init_geometry()` printed** `rcu: RCU calculated
  value of scheduler-enlistment delay is 10 jiffies.`
  (`adjust_jiffies_till_sched_qs()`, `kernel/rcu/tree.c:474`), with no oops
  text; the loader's WDT2 then resets the unit (~7.8 s loop).  [report 254]
* **Nothing in the platform SMP path is blocked by hardware.**  From the
  region-1 boot, busybox `devmem`: `SCU_CONFIG 0x48240000 = 0x00000001` (2
  cores), `SCU_CTRL 0x48240004 = 0x00000531` (SCU enabled),
  `0x4824380c = 0`, WUGEN `0x48281800/4 = 0`, PL310 ID `0x48242000 = 0x410000C4`,
  SAR `0x4a326000 = 0xC80E2E93`, `CM_CLKSEL_DPLL_MPU 0x4a00416c = 0x00800000`,
  `CM_CLKMODE_DPLL_MPU 0x4a004160 = 0x5` (locked).  [report 254 §3]
* **The reference is dual-core at 1008 MHz.**  `pidin info` on the live oracle
  (captured through the qconn shell to a file, then `QCL.py get`):
  `Processor1` **and** `Processor2`, both `Cortex A9 MPCore 1008MHz`, on
  `/procnto-smp-instr`.  [report 254 §4]
* **`SMC` from QNX user space is `SIGILL` for every service, including `0x102`**
  (`tools/bench/smcprobe.c`) — the privileged-instruction trap, not evidence
  about the ROM, since our own non-SMP kernel drives the PL310 through
  `0x102/0x109/0x113`.  [trap 319]
* **A DEBUG_LL ring is only readable by the boot that follows the failure, and
  three different things write it**: the chainloader (`clear_ring 0x88000000`
  + breadcrumbs), the decompressor (`busyuart` mirrors into all three rings via
  `ring_put`), and an **unidentified** writer of a small printk window at
  `+0x1000`.  `dd if=/dev/mem` cannot read the `no-map` pages (`Bad address`);
  `mmap` can (`tools/bench/ringdump.c`).  [report 254 §2; traps 316/317/317a]
* **QNX fills free RAM with `0xAA`**, so booting the oracle destroys the ring:
  `0x88000000`, `0x90000000` and the pbmon shadow `0x90200000` all read `0xAA`
  while the IFS region `0x80000000` keeps real data.  (The operator recalls
  earlier sessions reading full rings through the door, so this is recorded as
  an **open** mechanism, not a law.)  [trap 317]
* **The SMP module set is consistent**: all 44 modules, including the vendored
  `pvrsrvkm_omap4_sgx540_120.ko`, rebuilt with vermagic
  `7.3.3 SMP mod_unload modversions ARMv6 p2v8`, stripped `--strip-debug`, and
  installed into the live root before the flash.  [report 254 §1]
* **The region-1 root's modules no longer match the config** —
  `.gnu.linkonce.this_module section size must match the kernel's built struct
  module size` on every `insmod`.  [trap 320]
* **The host's RNDIS interface name flaps** (`enp8s0f3u1` ↔ `enp8s0f3u1i2`), so
  NM's `pb-rndis` profile (bound by name) silently stops activating; bind by MAC
  `02:1a:11:00:00:02`.  [trap 318]
* **The full modified 1917 RCFS boots with USB + qconn; the minimal ones did
  not.**  With the 1917 UFS in place: the shim IFS alone → setup wizard, no
  network; the patched minimal RCFS (`9b67cbdb…`) → splash, no USB; the
  session-43 `setB.signed` (`d22eab3b…`) → no USB; the **full modified 1917
  RCFS** (`rc4ab6e34…`, packed `rcfs1917-flash.signed`) → `0fca:8012`, qconn on
  9090 and `sshd` once.  [report 254 §7]

## Inferred (reasoning from evidence, not directly measured)

* **The hang is a silent spin, not a fault**: no oops text reaches the ring and
  the console never comes up; a data abort at that point would print.
* **The likely class is "`nr_cpu_ids = 2` with one CPU online"** inside
  `rcu_init()` — per-CPU workqueue/RCU init for a second possible CPU — but this
  is a hypothesis, not a measurement.
* **`hd3` no longer holds the Nura root** and the RCFS "reseeded" it before the
  UFS flash (operator); the QNX setup wizard and the operator's UFS repack are
  consistent with that.

## Dead ends and wrong assumptions

* **"SMP dies at entry"** — no: it boots and prints.
* **"The SCU is firewalled to non-secure"** — no: `SCU_CONFIG`/`SCU_CTRL` read
  cleanly from Linux user space, as do the WUGEN/PRM/CM registers.
* **"The ROM public API may be missing"** — not testable from QNX user space,
  and our own kernel already uses it for the PL310.
* **"Boot QNX and read the ring"** — QNX's fill erases it (measured), and the
  operator's recollection of earlier successes is recorded as an open question.
* **"The `PBDBG` markers prove the hang is inside `rcu_init()`"** — invalid:
  those three images were built while the tree DTS had `earlyprintk` removed,
  so they were silent and the trace was stale (trap 315).  The one marker image
  with `earlyprintk` (`0x4F6CC563`) left a *shorter* trace than the plain SMP
  image, so the `+0x1000` window's writer must be identified first (trap 317a).
* **"A region-1 root makes a perfect read-back image"** — it needs its modules
  rebuilt (trap 320); fine for `/dev/mem` work.

## Unknown / open questions

* **Who writes the `+0x1000` printk window?**  Not `ring_put` (count/index stay
  at 50) and not `pbmon` (no printk hook).  Cheap probe: a unique boot-time
  printk in the read-back kernel, then look for it there.
* **Which statement of `rcu_init()` hangs?**  Only after the above.
* **Why did earlier sessions' post-QNX ring reads work?**  Which image, which
  boot, which offset.
* **Is `nr_cpu_ids = 2` + one online CPU the trigger?**  A `CONFIG_NR_CPUS=1`
  SMP build exercises the same paths with one possible CPU.
* **The MPU OPP**: the reference's 1008 MHz is measured; the voltage/ABB and the
  safe step are not.

## Artefacts produced

* Report **254** (`docs/reports/254-smp-hangs-in-rcu-init-2026-11-16.md`),
  capture `docs/captures/oracle-live-2026-11-16-ring-and-cpu.txt`.
* New tracked tools: `tools/bench/ringdump.c` (mmap `/dev/mem` reader),
  `tools/bench/memzero.c` (ring zeroing), `tools/bench/smcprobe.c` (SMC probe).
* Images (host, gitignored): `local/images/kernel-0x202E9F38-smp.img` (**the**
  SMP build), `local/work/kernel-region1.img` (`0x433232F1`, the canonical
  bench image), `kernel-dump{,-2,-3}.img` (read-back variants),
  `kernel-smp-marks{,2}.img`, `kernel-smp-rcu{,2}.img`.
* UFS/RCFS work: `local/work/ufs-1917-devuser.ufs` (`482e0473…`) +
  `.signed` (`371b09a1…`), `local/work/rcfs1917-flash.signed`, the patched
  `local/work/minimal/rcfs-noufs.rcfs` (`9b67cbdb…`).
* Retry wrappers: `local/work/{oracle,noufs,ufs,setb,rcfs1917,smp,r1}-flash-retry.sh`,
  `local/work/ring{clean,clean3,marks,marks2,rcu,rcu2}.sh`.
* Docs: traps 315–320 (+317a), STATE, INDEX, this note.

## Superseded / uncertain

* **"The SMP kernel dies at entry / the SCU is firewalled / the ROM lacks the
  services."**  This session believed all three early on.  Superseded by the
  measurements in report 254 §2–§4 (it boots and prints `CPUs=2`; the SCU and
  every register in the SMP path read cleanly; SMC is merely privileged).
* **"`PBDBG` markers show the hang is inside `rcu_init()`."**  Withdrawn: three
  marker builds were made while the tree DTS had `earlyprintk` removed (silent
  kernels), and the marker build that did carry `earlyprintk` left a *shorter*
  window than the plain SMP image, so the `+0x1000` window's writer must be
  identified first (trap 317a).  The current statement is only "stops right after
  the RCU geometry print".
* **Session 60's "the ring can be read after booting the reference OS"** is not
  contradicted outright — the operator recalls it working — but it did **not**
  work this session (trap 317, and the contradiction file
  `docs/contradictions/ring-survives-a-qnx-boot.md`).  The current, measured
  statement is: a QNX boot leaves `0xAA` in the ring pages here.
* **"`region 1` is the fallback root and can be used as-is."**  It boots, but its
  module set no longer matches the kernel config (trap 320).

## Next

1. Identify the `+0x1000` window's writer, then bracket `rcu_init()`'s
   statements with markers (each build must be checked with
   `grep -ac earlyprintk`).
2. Then the "one possible CPU" experiments (`NR_CPUS=1` with `SMP=y`; `nosmp`).
3. Then cpu1 proper, and the MPU OPP (1008 MHz is the reference's value).
4. Bench upkeep: rebuild the region-1 modules (trap 320); the way back to the
   Nura root is `f7858110…` at `0x4915`.
