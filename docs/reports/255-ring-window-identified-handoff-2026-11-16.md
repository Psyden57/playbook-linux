# 255 — the `+0x1000` "printk window" is the ring itself, and the loader's watchdog was killing the SMP boot

Date: 2026-11-16 (session 62, Linux-era 56).
Bench at the end: the unit runs the **read-back image** (`0x5BBCD22E`: the
region-1 kernel, `earlyprintk` removed from the DT `bootargs`, the hand-off +
block-copy chainloader), Linux `7.3.3 #79`, UP, `pb> ` on the ACM, dropbear on
`169.254.0.1:22`.  `BOOTCTR_FCTRY = 0x28` (`28000000`, above the `0x1e`
threshold, no write).

## 0. Headline

* **Trap 317a is closed: there is no `+0x1000` printk window and no second
  writer.**  `+0x1000` is the ring's own character array at **character
  #3840** — the first byte past what the old chainloader cleared
  (`+0x80..+0xFFF`, `0xF80` bytes).  One buffer, one writer: `ring_put` in
  `arch/arm/include/debug/omap4bc.S`, which is also the kernel's
  `earlyprintk` path (`arch/arm/kernel/early_printk.c` → `printascii` →
  `busyuart`).  Measured on a live ring: the text flows straight across
  `+0x1000` mid-line and `index == count mod 0x7F00` exactly.
* **That is why marker runs were untrustworthy.**  A kernel that dies early
  prints ~4 KB, so it stops *below* `+0x1000` and never writes there; the
  read-back boot's chainloader then erased everything below it, and the
  "window" held **older** text from an earlier, longer boot.  The
  "impossible shorter trace" of session 61 is exactly that.
* **The chainloader now hands the ring off instead of clearing it** (report
  §2): the stream is append-only across boots and the last two entry headers
  are recorded, so a failing boot's log survives the read-back boot intact.
  Verified byte-exactly on the bench.
* **`rcu_init()` completes.**  With a trustworthy read, the `rcu2` marker
  build (`0x4F6CC563`, `#85 SMP`) prints every marker inside `rcu_init()`
  through `PBDBG: rcu_init done`, and continues through `kvfree_rcu_init`,
  `trace_init`, `context_tracking_init` and the rest.  Trap 315's "stops
  inside `rcu_init()` after the geometry print" is **withdrawn**.
* **The stop point is nondeterministic on one and the same image.**  The
  plain SMP image (`0x202E9F38`, `#78 SMP`) was booted 7 times: it stopped at
  **3208**, **3943** (×5) and **4211** characters — the last one *inside* the
  `RCU calculated value…` line.  So this is not a statement-level hang and a
  marker bracket will give a distribution, not a single answer.
* **The loader's 7.8125 s watchdog is not the cause** (§4), and the
  chainloader must never program WDT2 (§4a — it wedged the unit and left the
  timer stopped).

## 1. The identification, with the evidence

The ring page layout (unchanged): breadcrumbs `+0x00..+0x7C`, `count` at
`+0x80`, wrapping `index` at `+0x84`, characters `+0x100..+0x7FFF` for a
`0x7F00`-character ring, plus the chainloader's epoch block at
`+0x88..+0xB3`.

Three measurements settle it:

1. **`+0x1000` is inside the character array.**  `+0x1000 - 0x100 = 0xF00 =`
   character 3840, and the old `clear_ring` macro zeroed `(0x1000-0x80)/4 =
   0x3E0` words from `+0x80`, i.e. `+0x80..+0xFFF` — stopping exactly one
   byte short of `+0x1000`.  Nothing else in the tree writes those pages:
   `grep` over the build tree finds only `omap4bc.S` (`ring_put`), the
   `omap44xx_io_desc` aliases and `irq-gic.c`'s `pb_event` block at ring1
   `+0x00..`, and `arch/arm/kernel/early_printk.c` has no ring hook of its own.
2. **The live ring is one continuous stream across `+0x1000`.**  On the bench
   image (`count=71103`, `index=6079 = 71103 mod 0x7F00`) the bytes at
   `+0x1000` read `ime\r\n[ 9.628753] module kpp: …` — the middle of the
   module-load text, continuing the text before it (`[ 9.624053]`) and after
   it (`[ 9.633453]`).  A separate writer would not continue a line.
3. **The old window content belongs to a longer log, not to the boot under
   test.**  Offset of the `mobility grouping` line is **3822** in every SMP
   log (so `+0x1000` starts mid-`grouping on.  Total pages:` — exactly the
   `on.  Total pages: 261632` the session-61 capture shows), while `SLUB:`
   sits at 3959 and `RCU calculated` at 4294.  A 3943-character boot therefore
   leaves `mem auto-init…` as the last text at `+0x1000`, and a 4986-character
   boot leaves the SLUB + RCU lines — the two observations that looked
   contradictory.

## 2. The new chainloader contract (ring hand-off)

`boot/chainloader/chainloader_kernel.S.in`, at `_start`, before anything can
write a character:

* **`index ≥ 0x7F00` is sanitised to `count = index = 0`.**  A QNX boot fills
  free RAM with `0xAA` (index `0xAAAAAAAA`); without this, `ring_put`'s
  `add #0x80` would address outside the page.  Measured after the power cycle
  in this session: the header came back sanitised and the ring restarted at 0.
* **Two entry headers are kept, shifted every boot** (all in ring2 =
  `0x90000000` = `BC_PRIMARY`):
  `+0xb8` magic `RHND` (`0x444E4852`), `+0xbc`/`+0xc0` prev count/index,
  `+0xc4`/`+0xc8` prevprev count/index.
  * previous boot's log = characters from `prevprev_index`, `prev_count -
    prevprev_count` long;
  * this boot's log = characters from `prev_index`, `count - prev_count` long.
* **The character area is not cleared.**  A read-back boot appends its own
  ~1.6 KB after the failed log instead of erasing it.  The read-back image
  still has to be quiet (`earlyprintk` removed from its DT `bootargs`;
  `make dtb` only, no kernel rebuild) — otherwise its multi-10-KB log wraps
  the ring and overwrites the failure.

Everything is verified on the bench: after a quiet boot the delimited logs are
`Uncompressing Linux... done, booting the kernel.\r\n` for both boots and
`index == (prev_index + this_length) mod 0x7F00`.

## 3. What the recovered logs say

`rcu2` (`0x4F6CC563`, `#85 SMP`, markers inside `rcu/tree.c`), 4986 characters:

```
[    0.000000] SLUB: HWalign=32, Order=0-3, MinObjects=0, CPUs=2, Nodes=1
[    0.000000] rcu: PBDBG: rcu_init enter
[    0.000000] rcu: PBDBG: after early_boot_tests
[    0.000000] rcu: Hierarchical RCU implementation.
[    0.000000] rcu: RCU event tracing is enabled.
[    0.000000] rcu: PBDBG: after bootup_announce/prio
[    0.000000] rcu: RCU calculated value of scheduler-enlistment delay is 10 jiffies.
[    0.000000] rcu: PBDBG: after init_geometry
[    0.000000] rcu: PBDBG: after init_one
[    0.000000] rcu: PBDBG: after open_softirq
[    0.000000] rcu: PBDBG: after pm_notifier
[    0.000000] rcu: PBDBG: after rcutree_prepare_cpu
[    0.000000] rcu: PBDBG: after rcutree_report_cpu_starting
[    0.000000] rcu: PBDBG: after rcutree_online_cpu
[    0.000000] rcu: PBDBG: after rcu_gp_wq
[    0.000000] rcu: PBDBG: after sync_wq
[    0.000000] rcu: PBDBG: after qovld
[    0.000000] rcu: PBDBG: after start_poll_expedited
[    0.000000] rcu: PBDBG: after test_sync_prims
[    0.000000] rcu: PBDBG: rcu_init done
```

So every statement the session-61 plan wanted to bracket has run.  The
`rcutree_prepare_cpu` / `report_cpu_starting` / `online_cpu` group — the part
the "two possible CPUs, one online" hypothesis was aimed at — is complete.

The plain image (`0x202E9F38`, `#78 SMP`) booted 7 times gives:

| characters | last line | boots |
|---|---|---|
| 3208 | `On node 0, zone HighMem: 512 pages in unavailable ranges` | 2 |
| 3943 | `mem auto-init: stack:all(zero), heap alloc:off, heap free:off` | 3 |
| 4211 | `…RCU calculated value of scheduler-enlistment delay is 10 jif` (truncated mid-line) | 1 |

(i.e. it dies somewhere between `mm_core_init()` and `rcu_init()` — sometimes
before SLUB, sometimes after the RCU geometry print.)  §4 shows this is the
loader's watchdog expiring, not a hang.

## 4. CORRECTED in the same session: the loader's window IS the limit

The first draft of this section argued the watchdog could not be the cutter,
from `D < 6.27 s` (a working boot re-programs WDT2 at kernel time 0.741 s) and
the ~6.9 s a cut at kernel time ~0.1 s would need.  **That inference was wrong,
and it was measured wrong.**  Sampling WDT2 `CRR` in `head.S` at kernel entry
(§4b) gives the number directly:

```
chainloader entry -> jump (the 11 MB copy) : 0.832 s
jump -> kernel entry (XZ decompression)    : 6.619 s
elapsed since arming at kernel entry       : 7.450 s  of 7.812 s
```

So the kernel starts with **362 ms** of watchdog budget, and the early log is
~4 KB of printk through the uncached ring.  The varying stop points are simply
where the axe falls, and the "nondeterministic hang" of trap 315 is the
**loader's 7.8125 s window expiring mid-init** — the kernel needs ~0.74 s of
its own time to reach the late initcall that reprograms WDT2, and it never has
it.  The bench (non-SMP) image survives because its decompression is shorter
(it reaches the reprogram in time).

### 4a. Do not program WDT2 from the chainloader

Session 62 tried to extend the window from the chainloader with the kernel's
own `WSPR` sequence (stop `0xAAAA`/`0x5555`, `WLDR`, start `0xBBBB`/`0x4444`,
polling `WWPS`).  It **wedged before the kernel ran and left WDT2 stopped**:
the unit sat dark with no USB and no periodic reset for minutes, and only a
power-button power cycle recovered it.  Two boots of that image were written
to media before this was understood, and the second one had to be reflashed
through the power-on bootrom window.  The step has been removed and replaced
by a comment.  `report 109 §8.1`'s "loader-side WDT writes are dropped / wedge
the loader" stands; the kernel's late-initcall reprogram (trap 115) is the
supported path.

### 4b. The fix that worked: a block copy in the chainloader

`copy_words` in the chainloader copied the 7.5 MB zImage one word at a time with
the MMU **and caches off**, i.e. 1.9 M non-cached load/store pairs plus loop
overhead — 0.83 s of the window.  Replacing it with `LDMIA`/`STMIA` in
eight-word bursts (saving `r4-r10`, because the caller keeps `BC_PRIMARY` in
`r4` and the CRR sample in `r11`) cuts that substantially, and **the SMP kernel
then reaches its own WDT2 reprogram and boots to userspace**:

```
$ pbcon.py 'cat /proc/cmdline'
loglevel=0 maxcpus=1 clk_ignore_unused panic=10 fw_devlink=permissive
$ cat /sys/devices/system/cpu/possible   ->  0-1
$ cat /sys/devices/system/cpu/online     ->  0
```

It needs **both** changes to fit: the block copy, and `loglevel=0` (≈4 KB of
printk through the slow ring costs a large part of the 362 ms).  With the
kernel owning a 58.6 s window after its late initcall, logging can be turned
back on for later boots.

## 5. The next blocker, measured (SMP only)

That first SMP boot reached the initramfs fallback console, and the reason is
specific:

```
loop0: detected capacity change from 0 to 124583936
loop0: detected capacity change from 124583936 to 122191872
EXT4-fs (loop0): VFS: Can't find ext4 filesystem
```

The **same** region-1 filesystem mounts cleanly on the UP image minutes later
(`/dev/loop0 on / type ext4 (rw,relatime,nobarrier)`, 58.7 M, 42 % used, with
only `warning: mounting unchecked fs`), so the filesystem is intact and the
loop/ext4 bring-up is what differs under SMP.  That is the next experiment:
`CONFIG_SMP=y` with `NR_CPUS=1` (same SMP code paths, one possible CPU) will
separate "SMP kernel" from "two possible CPUs", and a retry/delay in the
initramfs loop setup will separate "geometry" from "timing".


## 5. Bench notes worth keeping

* **The bootrom can take ~20 s** before it hands over to the chainloader
  (operator, this session).  A read-back flash armed 10 s after a test flash
  therefore sometimes catches the bootrom *before the image under test has
  run at all* — which is what happened on the first plain-SMP attempt (the
  ring showed no log for that boot).  Wait ≥25 s, or poll `lsusb` for the
  bootrom's disappearance and reappearance rather than sleeping.
* **A power-off wipes the ring** (DRAM loses its content): after the power
  cycle the ring header came back as fill and was sanitised to 0.  Capture
  the ring *before* any power cycle.
* The chainloader grew from **828 to 852 bytes** (the `clear_ring` loop is
  gone — four macro expansions became one hand-off — and `copy_words` became a
  block copy).

## 6. Next

1. **The first SMP blocker is gone; the new one is the root mount.**  Boot the
   SMP kernel with `NR_CPUS=1` (same SMP code paths, one possible CPU) and with
   `nosmp`, and see whether the region-1 ext4 mount succeeds: that separates
   "SMP kernel" from "two possible CPUs", and a retry in the initramfs loop
   setup separates "geometry" from "timing" (§5).
2. **Then cpu1 proper**: remove `maxcpus=1` and let the kernel bring CPU1 up.
   The SCU, the WUGEN `AUX_CORE_BOOT` registers and the ROM API are all
   demonstrably usable (report 254 §3–§4).  Keep the block-copy chainloader and
   the quiet boot until the kernel owns the watchdog, then re-enable
   `earlyprintk`/`ignore_loglevel` for measurement.
3. **Then the MPU OPP**: the reference measured 1008 MHz; the DTB already
   carries the OMAP4430 OPP table (trap 312), so what is missing is a
   `cpu-supply`/`vdd_mpu` and the `cpufreq-dt` binding — or a boot-time DPLL/VC
   write.
4. Keep the marker set in `init/main.c` (`PBDBG:`) and the `head.S` CRR stamp
   as the instruments for any further early-boot work; both are gitignored tree
   edits and must be removed before a patch re-export.
