# Handoff — session 62 (absolute) → session 63

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**, with
a GPU-accelerated compositor.

**THE ACTIVE ITEM: the CPU (second core, then the MPU OPP).**  The unit runs one
core at 400 MHz — but **the SMP kernel now boots to userspace**
(`possible = 0-1`, `online = 0`).

* **The blocker was the loader's 7.8125 s watchdog window, not a hang.**  A WDT2
  `CRR` sample in `head.S` (ring2 `+0x18`, saved by the chainloader into `+0xd0`)
  measured kernel entry at **7.450 s of 7.812 s — 362 ms left**: chainloader copy
  **0.825 s** + XZ decompression **6.619 s**.  The kernel needs ~0.74 s to reach
  its own late-initcall re-program, so it was killed mid-init — which is what the
  varying 3208/3943/4211-character stop points were.  The fix, verified together:
  a **block copy** in the chainloader (`LDMIA`/`STMIA` ×8) **and** a quiet boot
  (`loglevel=0`).  Then `kernel-smp-quiet.img` (`0xA2FA490E`) boots.
* **`rcu_init()` completes** — with the fixed ring, `rcu2` prints every `PBDBG`
  marker through `rcu_init done` and on into `kvfree_rcu_init`/`trace_init`.
  Session 61's "stops inside `rcu_init()`" is withdrawn (ring artefact).
* **The ring artefact is fixed** (that was the session's first half): there never
  was a `+0x1000` printk window — `+0x1000` is the ring's character array at
  **character #3840**, the first byte past the old chainloader clear, so a short
  log left older text there while the read-back chainloader erased the real log.
  The chainloader now hands the ring off (report 255, trap 321).
* **The remaining SMP blocker is the root mount** (trap 326): the first SMP boot
  fell back to the initramfs console with
  `EXT4-fs (loop0): VFS: Can't find ext4 filesystem`, while the **same** region-1
  filesystem mounts fine on the UP image (`/dev/loop0 on / type ext4`, 58.7 M,
  42 % used).  The FS is intact; loop/ext4 bring-up differs under SMP.
* **No hardware blocks the platform SMP path** (unchanged): `SCU_CONFIG = 0x1`
  (2 cores), `SCU_CTRL = 0x531`, `0x4824380c = 0`, WUGEN `0x48281800/4 = 0`,
  PL310 ID `0x410000C4`; `SMC` is merely privileged (trap 319).  The reference is
  measured **dual-core at 1008 MHz** — the MPU OPP target (trap 312).

## 0a. THE NEXT EXPERIMENT — separate "SMP kernel" from "two possible CPUs", then the mount

1. **`CONFIG_SMP=y` with `CONFIG_NR_CPUS=1`** (same SMP code paths, one possible
   CPU) and a plain SMP build with **`nosmp`** on the cmdline.  Boot each with
   `loglevel=0` (keep the block-copy chainloader): does the region-1 ext4 mount
   succeed?  If `NR_CPUS=1` mounts and `NR_CPUS=2` does not, the trigger is
   `nr_cpu_ids = 2` with one online CPU.
2. **Retry/delay in the initramfs loop setup** (`initramfs/init.c`,
   `rootfs_boot()`): the loop geometry change messages
   (`0 -> 124583936 -> 122191872`) come *before* the ext4 failure, so a retry is
   the cheap way to separate "geometry" from "timing".  A change here needs the
   userspace binary rebuilt (`arm-linux-gcc -static -O2`) **and** the cpio
   removed (`rm -f $K/usr/initramfs_data.cpio*`) — i.e. a kernel relink.
3. **Then cpu1 proper**: drop `maxcpus=1` from the DTS `bootargs` and let the
   kernel bring CPU1 up (`omap4_smp_prepare_cpus()` → `scu_enable()` →
   `AUX_CORE_BOOT_1`).  Keep `loglevel=0` for the first attempt; once the kernel
   owns the 58.6 s watchdog, `earlyprintk`/`ignore_loglevel` can come back for
   measurement (that is `kernel-smp-loud.img`, `0x3BD64F22`, **not yet booted**).
4. **Then the MPU OPP**: the OPP table already exists in `omap443x.dtsi`
   (300/600/800/**1008** MHz with 1.025/1.2/1.313/1.375 V, trap 312); what is
   missing is `cpu-supply`/`vdd_mpu` and the `cpufreq-dt` binding — or a
   boot-time DPLL/VC write measured against the reference's own values.

## 0b. Reading the ring (works; reuse it)

* **Ring2 = `0x90000000`** is the canonical page (the chainloader's
  `BC_PRIMARY`); only its hand-off record is trusted.
* Layout: `+0x00..+0x7C` breadcrumbs, `+0x80` count, `+0x84` index,
  `+0x88..+0xB3` epoch block, `+0xB8` magic `RHND`, `+0xBC/+0xC0` prev
  count/index (= this boot's entry = the previous boot's end), `+0xC4/+0xC8`
  prevprev count/index (= the previous boot's start), `+0x100..+0x7FFF`
  characters (append-only across boots).
* **Previous boot's log** = chars from `prevprev_index`,
  `prev_count - prevprev_count` long.  `tools/pb_ring.py decode <memdump>
  [--boot prev|cur|all]` does it, and `--boot all` prints both delimited boots.
* **Read-back image:** `local/work/kernel-readback-quiet.img`
  (`0xBBAC2B82`) — the region-1 kernel (`#79`) with `earlyprintk` removed from
  its DT `bootargs` plus the hand-off chainloader.  It boots to a shell and
  appends only ~1.6 KB, so the failed log survives.
* `tools/bench/{ringdump,memzero}.c` are the instruments (`mmap`; `dd` on
  `/dev/mem` fails on the `no-map` pages).  Push them to `/tmp` (`/tmp` is
  tmpfs); `tools/pbssh.sh 'cat > /tmp/ringdump' < local/work/ringdump` is
  byte-exact.
* **A power-off wipes the ring** (DRAM); capture before any power cycle.

## 0c. Then, in order

1. **The "one possible CPU" class** — `SMP=y` with `CONFIG_NR_CPUS=1`, and a
   plain SMP build with `nosmp` on the cmdline.  If either boots, the trigger is
   `nr_cpu_ids = 2` with one online CPU and the fix is in the per-CPU/percpu
   init path, not in mach-omap2.
2. **cpu1 proper** (remove `maxcpus=1` from the DTS) — the SCU, the WUGEN
   `AUX_CORE_BOOT` registers and the ROM API are all demonstrably usable.
3. **Then the MPU OPP**: the reference's measured rate is **1008 MHz**; the
   voltage/ABB are still unmeasured — read `PRM_VC_*` (`0x4a307ba0`) from a
   *Linux* `devmem` (they read there; they die in QNX user space).

## 0d. The media, and the way back

* **Region 0 = the read-back image `0x5BBCD22E`** (`local/work/kernel-readback-quiet.img`).
  Rebuilding it needs no kernel compile: carve `zImage`/DTB out of an existing
  image (`zoff 0x2C0000`, `zlen = u32 @ +0x2C`, `dtoff = align(zoff+zlen, 8)`,
  DTB totalsize at `+4`), then `ZIMAGE=… DTB=… python3 tools/build_kernel_image.py`.
  Re-wrapping the stock images reproduces their CRCs byte-identically.
  `fdtget/fdtput` on a carved DTB is how the quiet (`loglevel=0`) variants are
  made — no DTS edit, no `make dtb` needed.
* **Region 1 = the busybox root** at raw `0x29000000`, 64 MiB (mounted rw by
  `/init`).  **Its modules still do not match the config** (trap 320) — rebuild
  them before using region 1 as a real bench root.
* **hd3 = the 1917 vendor UFS** (the Nura root is gone; way back:
  `local/work/nura/images/blackberry-playbook-root-apps-audio.img`
  (`f7858110…`, 625 999 872 B) at `0x4915`, then re-apply the session-59/60 live
  fixes).
* **The oracle** is one region-0 flash away: `local/work/ifs-shim-sealed.ifs`
  (`b52195bb…`) at `0x825` with the full modified 1917 RCFS at `0x8D5`; qconn on
  `169.254.0.1:9090` (`python3 tools/QCL.py …`).
* **Do NOT restore** `local/images/ifs0.sealed.bin` (trap 84).

## 0e. What this session changed (do not redo)

* **`boot/chainloader/chainloader_kernel.S.in`** (852 B, was 828):
  * `clear_ring` → `ring_handoff` (sanitise a garbage header + append-only +
    last two entry headers + the previous boot's timing words, traps 321/325);
  * `copy_words` → eight-word `LDMIA`/`STMIA` bursts (saves `r4-r10`; the word
    loop cost 0.825 s of the 7.812 s window). **This is what let the SMP kernel
    boot** (trap 325).
* **`tools/pb_ring.py`** now speaks the hand-off contract (`--boot prev|cur|all`,
  and it prints the previous boot's timing when the `head.S` stamp is present).
* **Builds:** `kernel-readback-quiet.img` (`0x5BBCD22E`),
  `kernel-smp-quiet.img` (`0xA2FA490E`, **boots to userspace**),
  `kernel-smp-loud.img` (`0x3BD64F22`, not booted),
  `kernel-smp-mark62.img` (`0x0A70D405`, `#86 SMP` with the dense markers and
  the CRR stamp — it produced the 7.450 s measurement).
* **Tried and rejected:** extending WDT2 from the chainloader.  It wedged the
  boot and left the timer stopped (no USB, no reset, dark panel) — recovered by
  a power-button power cycle and a reflash through the power-on bootrom.  The
  code is removed; trap 322 records it.  `local/work/kernel-smp-wdt.img` is that
  bad image — never flash it.
* **The tree** carries two instruments (gitignored; remove before a patch
  re-export): the `PBDBG` set in `local/kernel-tree/init/main.c` (pristine copy
  `local/work/carve/main.c.session62-orig`) and the WDT2 `CRR` stamp in
  `local/kernel-tree/arch/arm/kernel/head.S`.  `.config` is now
  **`CONFIG_SMP=y`, `CONFIG_NR_CPUS=2`**; the tracked
  `kernel/winchester-7.3.3.config` is still non-SMP.
* The chainloader is not part of the kernel patch, so no re-export was needed
  for it; the `init/main.c`/`head.S`/config edits are experiments.

## 0f. Traps added/corrected this session (full list 14–326 in `KNOWN-ISSUES.md` §2)

312 **corrected** (the OPP table comes from `omap443x.dtsi`; what is missing is
`cpu-supply` + the binding); 315 **corrected twice** (`rcu_init()` completes;
the stop points were the watchdog expiring); 317a **closed** (`+0x1000` =
character #3840, no second writer); 316 revised (two writers, no clear); 321 the
ring hand-off contract; 322 never program WDT2 from the chainloader; 323 the
bootrom can hold ~20 s (poll, do not sleep 10 s); 324 `marks`/`marks2` are silent
by construction; **325 the watchdog window is the real budget (and the copy ate
it)**; **326 the region-1 ext4 mount fails under SMP but not UP**.

## 1. Useful local extracts

* `local/work/kernel-readback-quiet.img` (`0x5BBCD22E`) — **the bench image**;
  `kernel-smp-quiet.img` (`0xA2FA490E`) — **the SMP image that boots**;
  `kernel-smp-loud.img` (`0x3BD64F22`); `kernel-smp-mark62.img` (`0x0A70D405`);
  `kernel-smp-wdt.img` (**bad** — the rejected WDT step).
* `local/work/carve/` — carved `zImage`/DTB for `region1`, `smp`, `rcu2`,
  `marks2`, `marks`; `region1-quiet.dtb`, `smp-quiet.dtb` (`loglevel=0`);
  `main.c.session62-orig`; `carve.py`.
* `local/work/{ringdump,memzero}` (static ARM builds); `tools/bench/*.c`.
* `local/work/{flash825.sh,flash825-long.sh,run-smp-then-readback.sh}` —
  the flash/measurement wrappers.
* Captured ring from this session: `local/work/r2{b,c,e,f,g,h}.bin` and the
  recovered logs `local/work/{rcu2-log.txt,smp-plain-log.txt,mark62-log.txt}`.
* Reports **255** (current) and 254; sessions `docs/sessions/session-62.md`.

## 2. Build and flash (7.3.3)

`REPO=$PWD; . tools/env.sh`; `K=local/kernel-tree`.  Kernel:
`rm -f $K/usr/initramfs_data.cpio*; make -C $K ARCH=arm CROSS_COMPILE=arm-linux-
-j12 zImage; make -C $K … ti/omap/omap4-winchester.dtb`; wrap with
`ZIMAGE=… DTB=… python3 tools/build_kernel_image.py` (prints the CRC; must stay
under `0xB00000`).  Modules **in-tree**, strip `--strip-debug`.  **A config
change (e.g. `CONFIG_SMP`) changes the module vermagic — rebuild and install
every module before the region-0 flash.**  A `-j12` kernel build is ~7 min here;
hand long builds to the operator.

## 3. Bench

* SSH is the primary shell: `PB_LINK=usb tools/pbssh.sh`.  The ttyGS0 prompt is
  `pb> ` (region-1 root) or the initramfs console; `pbcon.py` drives only the
  former (trap 274).
* **Host link:** the RNDIS interface flaps (`enp8s0f3u1` / `enp8s0f3u1i2`); NM's
  `pb-rndis` profile must be bound by MAC (`02:1a:11:00:00:02`), not by name
  (trap 318).
* Reboot: `reboot -f` over ssh.  **The flash: start the flasher first, then
  reset**; one bootrom session; 60 s window.  **The bootrom can hold ~20 s
  before it runs the chainloader** (trap 323) — wait ≥25 s before a read-back
  flash, or poll `lsusb`.
* The oracle: `python3 tools/QCL.py info | exec | put | get | shell`.

## 4. Working rules

* One variable per boot; a test boot is not a budget concern, but a boot-loop is
  — stop it, and read the counter at both ends of a cycle
  (`dmesg | grep pbrpmb`).  A failing image never runs `/init`, so its loops
  drain the counter with no top-up: keep the window short.
* Read dmesg **unfiltered** after a boot or structural change; do not run two
  console commands in parallel.
* Never present a guess as a fact; prefer a fresh read.  Look up external facts
  from the primary source.
* **Check a marker/read-back build for `earlyprintk` before believing a trace**
  (traps 315/324).
* Revert experiments and prove the revert; re-export the kernel patch when the
  DTS/config/driver change persists (trap 161), then apply-and-compare and tree
  walk (trap 211).
* Do not take screenshots; ask the operator to look.  Only the operator decides
  when to stop / wrap up.

## 5. Paste-ready kickoff prompt (session 63)

```
PlayBook (OMAP4430 "winchester") — session 63 (absolute; Linux-era 57)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a, KNOWN-ISSUES.md §2 (traps 14-326, especially 312-326)
and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md (84-89), reports 255, 254,
253 and 249-252, docs/sessions/session-62.md.

WHERE IT STANDS: THE ACTIVE ITEM IS THE CPU, and it moved a long way: with
CONFIG_SMP=y + CONFIG_NR_CPUS=2 + maxcpus=1 the kernel NOW BOOTS TO USERSPACE
(`possible = 0-1`, `online = 0`).  The blocker was never a hang: the loader arms
WDT2 with 7.8125 s and kernel entry happened at 7.450 s of it (chainloader copy
0.825 s + XZ decompression 6.619 s), so the kernel was being reset mid-init; the
fix was a block copy in the chainloader plus a quiet boot (loglevel=0).  The
remaining SMP problem is the ROOT MOUNT: the same region-1 ext4 that mounts on
the UP image fails under SMP with "EXT4-fs (loop0): VFS: Can't find ext4
filesystem" (trap 326).  The ring is trustworthy now too: +0x1000 was never a
printk window (it is the ring's character array at #3840, the first byte past
the old chainloader clear); the chainloader hands the ring off (append-only,
prev/prevprev headers + timing, trap 321) and tools/pb_ring.py decodes it.
rcu_init() COMPLETES (session 61's "stops inside rcu_init" is withdrawn).

THE NEXT EXPERIMENT (HANDOFF §0a): (1) CONFIG_SMP=y with CONFIG_NR_CPUS=1, and a
plain SMP build with nosmp, both with loglevel=0 — does the region-1 ext4 mount
succeed?  (2) a retry/delay in the initramfs loop setup (initramfs/init.c
rootfs_boot(); rebuild the binary AND remove the cpio).  Then cpu1 proper
(drop maxcpus=1) and then the MPU OPP (the OPP table already exists in
omap443x.dtsi: 300/600/800/1008 MHz — trap 312).

THE FACTS: region 0 = local/work/kernel-readback-quiet.img, CRC 0x5BBCD22E (the
region-1 kernel #79, earlyprintk removed from its DT bootargs, the hand-off +
block-copy chainloader) — the bench/read-back image.  local/work/
kernel-smp-quiet.img (0xA2FA490E) is the SMP image that boots.  Region 1 = the
busybox root at 0x29000000, 64 MiB, mounts on UP (58.7 M, 42% used), 43 modules
that NO LONGER MATCH the config (trap 320).  hd3 = the 1917 vendor UFS (way
back: local/work/nura/images/blackberry-playbook-root-apps-audio.img,
f7858110..., at 0x4915).  BOOTCTR_FCTRY = 0x28 at the wrap-up.  The oracle is one
IFS flash away (ifs-shim-sealed.ifs at 0x825), qconn on 169.254.0.1:9090.

PHYSICAL STATE: the unit runs the read-back image, SSH dropbear on
169.254.0.1:22 (PB_LINK=usb tools/pbssh.sh); the ACM is the fallback console
(pb> ).  The host's RNDIS interface name flaps (trap 318) — bind the NM pb-rndis
profile by MAC 02:1a:11:00:00:02.  /tmp is tmpfs: re-push ringdump/memzero.
Pace bulk pushes (8 KiB/5 ms; trap 302).  A power-off WIPES the ring (DRAM):
capture before any power cycle.

BENCH: SSH is the primary shell; `reboot -f` to reset.  THE FLASH: start the
flasher first, then the reset; one bootrom session; 60 s window.  The bootrom can
hold ~20 s before the chainloader runs (trap 323) — wait >=25 s before a
read-back flash, or poll lsusb.  An agent NEVER writes RPMB; NVRAM read-only.
Read dmesg UNFILTERED.  One command at a time.  A boot-looping image drains
BOOTCTR_FCTRY with no top-up: stop it and read `dmesg | grep pbrpmb`.  NEVER
program WDT2 from the chainloader (trap 322: it wedges and stops the timer; the
recovery is a power cycle).

RING WORK: ring2 0x90000000 is canonical.  +0x80 count, +0x84 index, +0x88..+0xb3
epoch block, +0xb8 magic "RHND", +0xbc/+0xc0 prev, +0xc4/+0xc8 prevprev,
+0xcc prev pre-jump CRR, +0xd0 prev kernel-entry CRR, +0xd4 prev entry stamp,
chars +0x100.. append-only.  Previous boot's log = chars from prevprev_index,
prev_count-prevprev_count long; tools/pb_ring.py --boot prev|cur|all also prints
the timing.  dd on /dev/mem FAILS on the no-map pages — use tools/bench/ringdump
(mmap) and memzero.  A QNX boot leaves 0xAA fill (the hand-off sanitises it).

BUILDS: modules IN-TREE, strip --strip-debug; a config change (CONFIG_SMP)
changes the module vermagic — rebuild every module before the flash.  A -j12
kernel build is ~7 min; hand long builds to the operator and stop the turn.
Carving zImage+DTB out of an existing image and re-wrapping it reproduces its
CRC byte-identically, so chainloader- or bootargs-only changes need no kernel
build (fdtget/fdtput for bootargs).  The tree currently carries two experiment
instruments (init/main.c PBDBG markers, head.S WDT2 CRR stamp) and CONFIG_SMP=y;
remove them before any kernel-patch re-export.

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
