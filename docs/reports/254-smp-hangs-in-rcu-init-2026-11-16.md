# 254 — CONFIG_SMP: the kernel boots and hangs inside `rcu_init()`; and how to read the ring at all

Date: 2026-11-16 (session 61, Linux-era 55).
Bench at the end of the investigation: the unit runs **our region-1 image**
(`local/work/kernel-region1.img`, CRC `0x433232F1`): the non-SMP kernel with the
initramfs rooted at **region 1** (`0x29000000`), so `pb> ` on the ACM and
dropbear on `169.254.0.1:22` (`PB_LINK=usb tools/pbssh.sh`).  Region 0 = that
image; hd3 = the **1917 vendor UFS** (the Nura root is gone, see §7).

## 0. Headline

* **The SMP kernel is not dead at entry — it boots and stops at `~0.000 s` right
  after `rcu_init_geometry()` printed
  `RCU calculated value of scheduler-enlistment delay is 10 jiffies.`**  No oops
  text, no further printk, then the loader's WDT2 resets the unit: a silent hang,
  not a fault.  **The exact statement is not established** — the finer
  instrument runs were invalid or inconsistent (§2), and the `+0x1000` printk
  window's writer must be identified before any marker run can be trusted.
* **Nothing in the platform SMP path is blocked by the hardware**: the SCU is
  readable from non-secure Linux (`SCU_CONFIG = 0x1` = 2 cores,
  `SCU_CTRL = 0x531` = enabled), and so are `cpu1_rstctrl` (`0x4824380c` = 0),
  both WUGEN `AUX_CORE_BOOT` registers (`0x48281800/4` = 0), the PL310 ID and
  `CM_CLKSEL/MODE_DPLL_MPU`.  The SCU/firewall hypothesis is dead.
* **The reference is dual-core at 1.008 GHz** — measured on the live oracle,
  `Processor1` and `Processor2`, both `Cortex A9 MPCore 1008MHz`, on
  `/procnto-smp-instr`.  This is the MPU OPP target for handoff §0b.
* **How to read the ring at all** (§2): zero it, boot the failing image, then
  boot a *silent* read-back kernel (`earlyprintk` removed, `CONFIG_DEBUG_LL=n`,
  `/init`'s `rlog` disabled, region-1 root) and read DRAM with `mmap` — `dd` on
  `/dev/mem` cannot read those pages.

## 1. What was built and flashed

| image | what | CRC |
|---|---|---|
| `local/images/kernel-0x202E9F38-smp.img` | `CONFIG_SMP=y`, `CONFIG_NR_CPUS=2`, `maxcpus=1` still in the DTS | `0x202E9F38` |
| `local/work/kernel-region1.img` | non-SMP, `ROOTFS_RAW_OFF = 0x29000000` (region-1 root) | `0x433232F1` |
| `local/work/kernel-dump.img` | as region1, `earlyprintk` removed from the bootargs | `0xCB9D6DD7` |
| `local/work/kernel-dump2.img` | as dump, `/init` `rlog` disabled | `0x12B2BA9E` |
| `local/work/kernel-dump3.img` | as dump2, `CONFIG_DEBUG_LL=n` (silent decompressor too) | `0x11D29723` |
| `local/work/kernel-smp-marks*.img` | SMP + `PBDBG` markers in `start_kernel` | `0x37357AF0`, `0x867376A9` |

The SMP build's vermagic is `7.3.3 SMP mod_unload modversions ARMv6 p2v8`; **all
44 modules** (including the vendored `pvrsrvkm_omap4_sgx540_120.ko`) were rebuilt
and installed into the live root before any flash.

## 2. Reading the ring post-mortem (the method, and why the obvious route fails)

Three things write to `0x88000000` (`ring1`) and its siblings:

1. **the chainloader** — `clear_ring 0x88000000` on every boot, then breadcrumb
   words at `+0x00..+0x7c`, `+0x88..+0xa0` (`BC_PRIMARY = 0x90000000`);
2. **the decompressor** — `busyuart` mirrors every debug char into **all three**
   rings through `ring_put` (count `+0x80`, index `+0x84`, chars `+0x100`), so
   ring2 carries `Uncompressing Linux... done, booting the kernel.` (50 chars);
3. **pbmon** — mirrors printk into a small window at **`+0x1000`** without
   touching count/index.  That window is what carries a failed boot's tail.

**A QNX boot cannot read it.**  On the oracle, `0x88000000`, `0x90000000` and the
pbmon shadow `0x90200000` all read back as `0xAA` fill with no printable run in
64 KiB, while the IFS load address `0x80000000` still holds real data — so
procnto fills the RAM it considers *free*, and the ring pages are free RAM to it.
(The operator recalls earlier sessions reading full rings this way; the open
question is which image/session that was — see trap 320.)

**The route that works** (used here):

1. from a running image, zero the three rings (`tools/bench/memzero.c`);
2. boot the image under test and let it hang;
3. boot the silent read-back image and dump the rings
   (`tools/bench/ringdump.c`, `mmap` of `/dev/mem`), then read the printk window.

`dd if=/dev/mem` **fails** for those pages (`Bad address`): they are `no-map`
reserved, so `pfn_valid()` is false and the `read()` path rejects them.  `mmap`
works, and `devmem` works one word at a time.  Also: `mmap` offsets must be
page-aligned (`0x88000080` as an offset is `EINVAL`).

With that, the SMP boot's own trace is:

```
init: stack:all(zero), heap alloc:off, heap free:off
[    0.000000] SLUB: HWalign=32, Order=0-3, MinObjects=0, CPUs=2, Nodes=1
[    0.000000] rcu: Hierarchical RCU implementation.
[    0.000000] rcu: RCU event tracing is enabled.
[    0.000000] rcu: RCU calculated value of scheduler-enlistment delay is 10 jiffies.
```

`CPUs=2` is the SMP kernel talking.  The RCU line is printed by
`adjust_jiffies_till_sched_qs()` from `rcu_init_geometry()` *inside*
`rcu_init()` (`kernel/rcu/tree.c:474`).

**Two methodological corrections, both measured the hard way:**

* **A marker build must be checked for `earlyprintk`.**  Three marker builds
  (`kernel-smp-marks.img`, `marks2`, `rcu`) were produced while the tree DTS had
  `earlyprintk` removed for the read-back image, so those kernels were *silent* —
  and the trace read after them was a *stale* one from the 17:05 image.  Always
  `grep -ac earlyprintk <image>` before drawing a conclusion from a marker run
  (`kernel-0x202E9F38-smp.img` = 1, `kernel-smp-marks*`/`-rcu` = 0,
  `kernel-smp-rcu2.img` = 1).
* **The writer of the `+0x1000` printk window is not identified.**  It is *not*
  `ring_put` (count/index stay at the decompressor's `0x32`), and `drivers/misc/
  pbmon.c` has no printk hook, yet a small printk window appears there and its
  content changes per boot (the 17:05 SMP image left SLUB+RCU; the marked
  `rcu2` image left only `mem auto-init`, which is *earlier* than the 17:05
  run — impossible for a linear boot log unless the window is filled by a
  *different* boot than the one being studied).  Until that writer is known, a
  marker run cannot be trusted to describe the boot under test.

So: the **17:05 SMP image** (`0x202E9F38`, `earlyprintk`, clean rings) is the
one measurement that stands — it reaches the RCU geometry line and stops.  The
finer localization (which statement of `rcu_init()`) is **not established**: the
marker runs were invalid (first three) or inconsistent (`rcu2`), and the next
step is to identify the `+0x1000` writer before instrumenting anything else.


## 3. Hardware access from non-secure Linux (all reads, no faults)

Read on the region-1 boot with busybox `devmem`:

| address | what | value |
|---|---|---|
| `0x48240000` | SCU_CONFIG | `0x00000001` → 2 cores |
| `0x48240004` | SCU_CTRL | `0x00000531` → SCU enabled |
| `0x4824380c` | `cpu1_rstctrl` | `0x00000000` |
| `0x48281800` | WUGEN `AUX_CORE_BOOT_0` | `0x00000000` |
| `0x48281804` | WUGEN `AUX_CORE_BOOT_1` | `0x00000000` |
| `0x48242000` | PL310 cache ID | `0x410000C4` |
| `0x4a326000` | SAR RAM | `0xC80E2E93` |
| `0x4a00416c` | `CM_CLKSEL_DPLL_MPU` | `0x00800000` |
| `0x4a004160` | `CM_CLKMODE_DPLL_MPU` | `0x00000005` (locked) |

`cfg.scu_base = OMAP2_L4_IO_ADDRESS(scu_a9_get_base())` = `0xFA240000`, inside
the 4 MiB `L4_PER_44XX` iotable window (`0xFA000000`), so the kernel's own SCU
access is mapped.

## 4. The reference, live (oracle)

`pidin info` captured through the qconn shell to a file, then `QCL.py get`:

```
CPU:ARM Release:6.6.0  FreeMem:617Mb/1024Mb BootTime:Nov 16 21:39:15 GMT 2026
Processes: 154, Threads: 588
Processor1: 1092599954 Cortex A9 MPCore 1008MHz FPU
Processor2: 1092599954 Cortex A9 MPCore 1008MHz FPU
```

A user-space `SMC` probe (`tools/bench/smcprobe.c`, services `0x102`–`0x105`)
gets `SIGILL` for every service, **including `0x102`** — which our own non-SMP
kernel uses successfully to program the PL310.  That is the privileged-instruction
trap, not evidence about the ROM; the public API demonstrably exists here.

The SCU/CM/PRM ranges are not readable from a QNX *user* process
(`mmap_device_memory` → the process dies, while SAR RAM and part of `PRM_VC`
read), so the oracle cannot settle the SMP question by itself.

## 5. The region-1 root: modules no longer match

Every `insmod` from the region-1 root now fails with

```
module kpp: .gnu.linkonce.this_module section size must match the kernel's built struct module size at run time
```

— the region-1 module set predates the current config (the `struct module`
layout changed).  Harmless for reading `/dev/mem`, but the region-1 image needs
its modules rebuilt before it is a useful bench root again.

## 6. Host side (trap 318)

The PlayBook's RNDIS interface name flaps between `enp8s0f3u1` and
`enp8s0f3u1i2` depending on the USB interface numbering, so a NetworkManager
profile bound by `connection.interface-name` silently stops activating and the
link has no address.  Bind it by MAC instead (`02:1a:11:00:00:02`) or retarget
the name after each enumeration.

## 7. Media state, and how it got there

The session's goal was to read the ring; that required a live QNX, which
required a UFS on hd3, and hd3 held the Nura root.  Sequence actually executed
(all region-0/hd3 writes are recoverable):

* the shim IFS at `0x825` (`local/work/ifs-shim-sealed.ifs`, `b52195bb…`);
* a patched minimal RCFS (`minimal/rcfs-noufs.rcfs`, `9b67cbdb…`: the
  `rootfs.sh || Fatal` made a `Warning`, an early `qconn port=9090`) — this
  **did not** bring the network up;
* the session-43 `setB.signed` (`d22eab3b…`) — also no network;
* the full modified 1917 RCFS (`local/work/rcfs-1917.rcfs`, `c4ab6e34…`, packed
  as `local/work/rcfs1917-flash.signed`) — **this one boots with USB + qconn**;
* the 1917 vendor UFS with the devuser injection
  (`local/work/ufs-1917-devuser.ufs`, `482e0473…`, packed `371b09a1…`) and then
  the operator's own modified UFS.

**hd3 is now a 1917 UFS; the Nura root is gone.**  The way back to the session's
starting state is the flasher-restorable base
`local/work/nura/images/blackberry-playbook-root-apps-audio.img`
(`f7858110…`, 625 999 872 B) at `0x4915`; `/init` regrows it and reinstalls the
SSH kit, but the session-59/60 live fixes (llvmpipe, `epitaph` r10006, the
udev/polkit/gorm rules) must be re-applied from the host payloads
(`local/work/llvmpipe/`, `local/work/nura/pmaports/main/{epitaph,catacomb}/`).

The oracle currently on media is the shim IFS + the full modified 1917 RCFS, and
it answers `qconn` on `169.254.0.1:9090`.

## 8. Next

0. **Identify the writer of the `+0x1000` printk window** (§2) — until then no
   marker run means anything.  Candidates: a `printk` hook this tree adds
   outside `drivers/misc/pbmon.c`, or a userspace writer in the read-back image.
   Cheap test: give the read-back kernel a *unique* boot-time printk and see
   whether it lands in the window.
1. **Then bracket `rcu_init()`'s statements** (`rcu_init_one`, the three
   `rcutree_*_cpu` calls, the two `alloc_workqueue`s,
   `start_poll_synchronize_rcu_expedited`) with `PBDBG` markers and re-run the
   §2 sequence: one build, two flashes.  The hang is in that function and
   nowhere else.
2. If it is an RCU/workqueue/percpu issue, the likely class is "second possible
   CPU exists but is offline" (`nr_cpu_ids = 2`, `num_online_cpus() == 1`) — try
   `CONFIG_NR_CPUS=2` with `maxcpus=1` vs. `nosmp`, and compare with a build
   where `NR_CPUS=1` is forced but `CONFIG_SMP=y` (that kernel cannot boot cpu1
   but exercises the same code paths with one possible CPU).
3. Then the MPU OPP: the reference's own value is **1008 MHz** (§4).
