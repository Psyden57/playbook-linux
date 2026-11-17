# 256 — the "SMP-only ext4 mount failure" was a rootfs-offset artefact; cpu1 bring-up hangs

Date: 2026-11-16 (session 63, Linux-era 57).

Bench at the end: region 0 = `local/work/kernel-smp-nosmp.img` (CRC `0x6D6F52C0`),
cmdline `loglevel=0 nosmp clk_ignore_unused panic=10 fw_devlink=permissive`,
Linux `7.3.3 #78 SMP` with `possible = 0-1`, `online = 0`.  Root `/dev/loop0`
ext4 **on hd3** (the region-1 busybox root `49e318bf…` flashed at loader block
`0x4915`, 58.7 M, 42 % used).  `pb> ` on the ACM; dropbear on `169.254.0.1:22`
after an entropy seed.  `BOOTCTR_FCTRY = 0x22` (the cpu1 reset loop drained it
from `0x26`).

## 0. Headline

* **Trap 326 is withdrawn — there was no SMP-only mount bug.**  The SMP image
  (`kernel-smp-quiet.img`, `#78 SMP`) carries an initramfs with
  `ROOTFS_RAW_OFF = 0x49000000` (hd3); the UP read-back image (`#79`) uses
  `0x29000000` (region 1).  At the time, hd3 held the **1917 vendor UFS
  (QNX6)**, so `mount -t ext4` failed correctly: there was no ext4 there.  With
  a real ext4 on hd3, the same SMP kernel mounts it and boots to a full
  userland, no retry, no delay, no kernel change.  The prior "loop/ext4 bring-up
  differs under SMP" inference was wrong.
* **`nosmp` is not a "one possible CPU" discriminator.**  It leaves
  `nr_cpu_ids = 2` (`SLUB: CPUs=2`, `percpu: [0] 0 [0] 1`) and only caps online
  CPUs (already 1).  The real test needs `CONFIG_NR_CPUS=1` or dropping `cpu@1`
  from the DTB.
* **cpu1 proper hangs in `smp_init()` before the USB gadget**, producing a
  reset loop that drains `BOOTCTR_FCTRY` (no `/init`, no top-up).  Recovered by
  reflashing the known-good `nosmp` image.

## 1. The mount failure, re-diagnosed

Session 62's first SMP boot to userspace fell back to the initramfs console with:

```
loop0: detected capacity change from 0 to 124583936
loop0: detected capacity change from 124583936 to 122191872
EXT4-fs (loop0): VFS: Can't find ext4 filesystem
```

The UP read-back image mounted "the same" filesystem minutes later as
`/dev/loop0 on / type ext4 (rw,relatime,nobarrier)`, 58.7 M, 42 % used.  The
two images are not the same, though: `initramfs/init.c:452` defines
`ROOTFS_RAW_OFF = 0x49000000ULL`, and `rootfs_boot()` sets
`li.lo_offset = ROOTFS_RAW_OFF` then `mount(loopdev, NEW_ROOT, "ext4", 0,
"barrier=0")`.  The failing boot's capacity drop is
`124583936 − 122191872 = 2392064` sectors × 512 = **`0x49000000` bytes** — the
loop's backing offset, hd3.  The UP image was built with `0x29000000` (region 1).

Three checks settled it (all read-only or recoverable):

* `qnx6 49000000` on the console mounts hd3 read-only as **QNX6** — the 1917
  vendor UFS, not ext4.
* `local/images/rootfs-region1-wl1283-bt.ext4` (64 MiB, sha256 `49e318bf…`) is
  the real ext4 region-1 root, with dropbear/usb0 in its `/etc/init.d/rcS`.
* `local/work/ufs-1917-devuser.ufs` (`482e0473…`, packed `371b09a1…`) and the
  sealed IFS/RCFS are the way back, so repurposing hd3 is recoverable.

Flash that ext4 to hd3 at loader block `0x4915` (the documented FS-partition
destination, report 241), keep the same SMP image on region 0, reboot:

```
EXT4-fs (loop0): mounted filesystem 5b10c0de-0000-4b00-8000-000000000018 r/w without journal. Quota mode: none.
/dev/loop0 on / type ext4 (rw,relatime,nobarrier)   58.7 M, 42 % used
```

So the root mount under SMP was never broken; only the pointer was.

## 2. The `nosmp` non-test

`kernel-smp-nosmp.img` (CRC `0x6D6F52C0`) = the `#78` SMP zImage with a DTB
whose bootargs are `loglevel=0 nosmp clk_ignore_unused panic=10
fw_devlink=permissive`.  It produced `SLUB: … CPUs=2`, `percpu: [0] 0 [0] 1`
and `smp: Brought up 1 node, 1 CPU`: `nr_cpu_ids` stayed 2, only the number of
online CPUs was capped (and that was already 1 with `maxcpus=1`).  It failed the
mount for the same hd3-is-QNX6 reason as the original SMP image.  No new
information about "SMP kernel vs two possible CPUs" came out of it.

## 3. SSH, entropy and dropbear

With the ext4 root mounted, dropbear (PID 138) listened on `169.254.0.1:22`
and ping worked both ways, but SSH returned "Connection timed out during
banner exchange": the client sent its version string and got no server banner.
`cat /proc/138/wchan` = `wait_for_random_bytes`; the CRNG was unseeded because
the SoC RNG is firewalled (trap 87).  `/bin/pbconsole`'s (built from
`initramfs/console.c`) `entropy <64 hex digits>` credited 256 bits,
`random: crng init done` appeared, dropbear's `wchan` moved to
`poll_schedule_timeout`, and SSH worked.  This must be repeated after every
boot when the region-1 root serves dropbear.

## 4. cpu1 proper: hang and recovery

`kernel-smp-cpu1.img` (CRC `0x182B0E00`) = the same `#78` SMP zImage with a DTB
whose bootargs are `loglevel=0 clk_ignore_unused panic=10 fw_devlink=permissive`
(no `nosmp`, no `maxcpus`).  Flashed to region 0:

* no PlayBook USB for ~70 s; `0fca:0001` bootrom appeared briefly at t+80 s then
  vanished — a **reset loop**;
* the hang precedes `/init`, so no RPMB top-up ran and `BOOTCTR_FCTRY` drained
  (`0x26 → 0x22` over the loop);
* the kernel owns WDT2 by then (late-initcall reprogram), so a hung cpu1 can
  persist with no reset until the watchdog/bootrom catches it;
* recovered by reflashing `kernel-smp-nosmp.img` (`0x6D6F52C0`) — the unit came
  back on `0fca:8010`, `possible = 0-1`, `online = 0`.

The SCU and WUGEN `AUX_CORE_BOOT` registers read clean from non-secure Linux
(report 254 §3), so register access is not the leading suspect.  The hang is in
`omap4_smp_prepare_cpus()` → `scu_enable()` → the ROM API path that starts CPU1.

## 5. Next

1. **Localise the cpu1 hang with the ring** — a loud cpu1 image
   (`earlyprintk`/`ignore_loglevel`, no `nosmp`) plus the quiet read-back, using
   `tools/pb_ring.py --boot prev`.  Keep the reset window short (no top-up on a
   hanging image).  `kernel-smp-loud.img` (`0x3BD64F22`) is the existing loud
   SMP build and needs a DTB re-cut for the no-`nosmp` bootargs.
2. **Then the real "one possible CPU" test** — `CONFIG_NR_CPUS=1` (rebuild) or
   drop `cpu@1` from the DTB — if the hang localisation points at per-CPU init.
3. **Then the MPU OPP** (report 254 §4: the reference is dual-core at 1008 MHz;
   the OPP table is in `omap443x.dtsi`, trap 312).

## 6. Evidence

* `local/work/kmsg-nosmp.txt` (974 lines, the failing `nosmp` boot: loop
  capacity math and `Can't find ext4 filesystem`).
* `local/work/dmesg-smp-nosmp-hd3-root.txt` (692 lines, the successful SMP boot
  with hd3 ext4: `EXT4-fs (loop0): mounted … r/w without journal`).
* `local/work/{r2-session63-start.bin,.words}` (ring capture at session start).
* Flash logs `local/work/flash-{smpnosmp,hd3,cpu1,recovery}.log`.
* Traps 326 corrected, 327–329 added; DECISIONS §90.
