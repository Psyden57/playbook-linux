# 258 — the dual-core wedge: the TLB-flush IPI is sent, received and serviced; the hang is downstream

Date: 2026-11-17 (session 64, Linux-era 58; continues report 257).

Bench at the end: region 0 restored to `local/work/kernel-fastcl-nosmp.img`,
CRC **`0x8B83421F`** (verified off the media trailer
`1f2dc8d7 1f42838b` at raw `0x8B10198`), the `#78` SMP zImage with the fast
chainloader and a `loglevel=0 nosmp` DTB.  Linux `7.3.3 #78 SMP`, `online = 0`,
root ext4 on hd3, dropbear on `169.254.0.1:22` after an entropy seed.
`BOOTCTR_FCTRY = 0x31` (self-topped up after the session's wedge drains).

## 0. Headline

The first experiment of HANDOFF §0a is complete.  A wedged dual-core boot of
`kernel-ipidbg-cpu1.img` (`0xF8264016`) was captured through the quiet read-back
image, and its `PBSMF`/`PBIPI1` markers read.  **CPU1 receives and completes
call-function IPIs right up to the point of the hang — including four clean
`PBIPI1: CALL_FUNC enter`/`done` pairs *after* `PBCPU: pre run_init(/init)`.**

That eliminates three of the four outcomes the handoff listed:

* not "IPI sent but CPU1 never receives it" (`PBIPI1: enter` fires);
* not "CPU1 receives it but stalls in the service routine" (`PBIPI1: done` fires);
* not "csd never completed/acked" on the available evidence (a `PBSMF: pre` /
  `PBSMF: post` pair is present, and no IPI is left mid-service).

The remaining branch is the handoff's fourth: **the hang is later than the IPI
service**.  The next instrument belongs in `flush_old_exec()` / `mmput()`, not
in the IPI path.  Caveat: the two CPUs share one `printascii` ring with no lock,
so a CPU0 marker can be garbled or overwritten by a concurrent CPU1 marker (one
earlier `PBSMF` pair is visibly interleaved with `PBIPI1` text).  The final
`execve` shows no `PBSMF` line at all; that absence is *not* strong evidence for
a csd ack failure and is the reason to make the next markers CPU0-only.

## 1. What was run

1. Flash `local/work/kernel-ipidbg-cpu1.img` (CRC `0xF8264016`, verified from its
   trailer `1f2dc8d7 164026f8`) to region 0 with `flash825-long.sh`; reboot via
   the ACM console (`pbcon.py 'reboot -f'`), not SSH.
2. **Boot 1 was a dual-core success**: `7.3.3 #91 SMP`, `online = 0-1`.  Rebooted.
   The operator observed **boots 2 and 3 wedge**.
3. USB monitor (`local/work/boot2-usb.log`): bootrom `0fca:0001` at 11:24:59,
   then **no USB for ~45 s** (11:25:05–11:25:47), bootrom again 11:25:50–11:26:02
   (the kernel-owned WDT2 reset), no USB again 11:26:05–11:26:18, bootrom
   11:26:21, ramloader `0fca:8001` at 11:26:27.  A wedge shows no `0fca:8010` at
   all, because the failure is in the kernel's `run_init_process("/init")` —
   before the USB gadget is created.
4. **After** the wedge, armed the read-back flash
   (`flash825-long.sh kernel-readback-quiet.img`, CRC `0x5BBCD22E`); it caught a
   bootrom window and reported `FLASH_OK` on attempt 1.  The read-back image came
   up at 11:27:01 as `#79`.
5. Seeded entropy with `entropy <64 hex>` on the bring-up console (trap 328),
   pushed the cross-built `tools/bench/ringdump` binary to `/tmp` paced at
   8 KiB / 5 ms (trap 302), and dumped ring2:
   `/tmp/ringdump 90000000 8000 /tmp/r2-64.bin` → 32768 bytes, pulled to
   `local/work/r2-64.bin` (sha256
   `c011b7e348224bc8a087dd228166f4b06a39c7fc7719593ca83352c0b1fe4c90`).
6. Converted with `local/work/raw2words.py` (LE u32 words, base `0x90000000`)
   to `local/work/r2-64.words` (8192 lines), then
   `tools/pb_ring.py decode local/work/r2-64.words --boot prev`.
7. Restored region 0 to `kernel-fastcl-nosmp.img` (`0x8B83421F`); `FLASH_OK`,
   unit back as `#78`, trailer and counter re-read (see Bench).

## 2. The ring read

```
count=0x2593 (9619) index=0x2593 ring=0x90000000
handoff prev=(9569,9569) prevprev=(4057,4057)
prev boot timing: chainloader-entry->jump 0.316 s,
  jump->kernel-entry (copy/decompress) 2.983 s,
  elapsed since arming at kernel entry 3.299 s of 7.812 s
# ---- prev: 5512 chars ----
# ---- cur:  50 chars ----   (the read-back boot's "Uncompressing Linux...")
```

The 50-character `cur` log is the expected silent read-back.  The 5512-character
`prev` log is the wedged dual-core boot.  Its marker sequence:

* full CPU1 bring-up — `PBCPU: pre/post smp_prepare_cpus`, `pre/post scu_enable`,
  `boot_secondary enter`, `pre/post wakeup_ipi`, every `PBCPU1:` marker through
  `post set_cpu_online`;
* `PBCPU: post smp_init, online cpus=02`; `post sched_init_smp`; `post topology
  init`;
* one garbled `PBSMF: pre csd_lock_wait` / `PBSMF: post csd_lock_wait` pair
  (interleaved with a CPU1 `PBIPI1` line) just after topology init — so a
  synchronous `smp_call_function_many()` **completed once**;
* ~170 `PBIPI1: CALL_FUNC enter` / `done` pairs during `do_basic_setup()`;
* `PBCPU: post do_basic_setup`, `post console_on_rootfs`, `kernel_init_freeable
  done`/`returned`, `post async_synchronize_full`, `post rcu_end_inkernel_boot`;
* `PBCPU: pre run_init(/init)`;
* **four clean `PBIPI1: CALL_FUNC enter`/`done` pairs, then silence.**

## 3. Interpretation

`PBIPI1` is emitted in `do_handle_IPI()`'s `IPI_CALL_FUNC` case
(`arch/arm/kernel/smp.c`) on **CPU1**.  `PBSMF` is emitted in the wait branch of
`__smp_call_function_many...()` (`kernel/smp.c`, around the `csd_lock_wait()`
loop) on **CPU0**.  Therefore:

* the four final IPI pairs are CPU1 servicing call-function IPIs *after* the
  kernel announced it was about to exec `/init` — the TLB-flush window in which
  the wedge occurs;
* every one of them has a `done`, so CPU1 is not stuck in the handler and the
  csd work is being drained;
* a `PBSMF: post` exists, so `csd_lock_wait()` returned at least once.

This points past the IPI layer.  The wedge is consistent with CPU0 hanging
inside `flush_old_exec()`/`mmput()` (page-table teardown, `mmput_async`, the ASID
/ `flush_tlb_mm` sequence), or with a lost csd completion whose CPU0 marker was
clobbered by the shared ring.  Distinguishing those needs markers inside
`flush_old_exec`/`mmput`, written by CPU0 only, and ideally a per-CPU marker
stream so concurrent `printascii` cannot interleave.

The 3.299 s kernel-entry time and the normal boot progression also re-confirm
that this is a software race, not the loader watchdog (trap 325) or power
(trap 331).

## 4. Bench notes

* On `kernel-readback-quiet.img` the ACM is the **bring-up console** — its
  `entropy <hex>` command seeds the CRNG directly (trap 328).
* On the restored `kernel-fastcl-nosmp.img` the ACM is a **BusyBox shell**; `echo
  <hex> > /dev/random` credits only ~21 bits per short write (the ACM line wraps
  at ~80 columns, so a 64-hex seed splits).  To seed, launch `/bin/pbconsole`
  from the shell and feed `entropy <hex>` with `tools/pbraw.py` — `pbcon.py`
  cannot drive `pbconsole`'s prompt.  Re-seed every boot that serves dropbear.
* The ACM still drops bytes when blasted; push bulk files over RNDIS with paced
  writes (trap 302).  `dd if=/dev/mem` cannot read the `no-map` ring; use
  `ringdump` (mmap).  A power-off wipes the ring — capture before any power
  cycle.

## 5. Next

1. Add CPU0-only markers in `flush_old_exec()` and `mmput()` (and, if the tree
   supports it cheaply, a per-CPU marker page) to a new IPI-debug build; capture
   a wedge and read the last marker.  This is a kernel rebuild — vermagic is
   unchanged by marker-only edits, so modules need no rebuild.
2. If `flush_old_exec` is the last marker, bracket its sub-steps
   (`flush_thread`, `setup_new_exec`, the `mmput`/`exit_mmap`/`flush_tlb_mm`
   path).
3. Keep `kernel-fastcl-nosmp.img` (`0x8B83421F`) as the region-0 bench until a
   fix is measured.  The real one-possible-CPU test (`CONFIG_NR_CPUS=1`, trap
   327) and the MPU OPP soak (trap 312) remain after the race.

## 6. Evidence

* Ring: `local/work/r2-64.{bin,words}`, decode in `local/work/r2-64-prev.txt`
  and `local/work/r2-64-all.txt`; findings note
  `local/work/ipi-wedge-findings-64.txt`.
* Boot/flash logs: `local/work/boot2-usb.log`,
  `local/work/flash-{cpu1,readback,restore}-64.log`,
  `local/work/cpu1-boot-outcomes.txt`.
* Helpers: `local/work/raw2words.py`, `local/work/paced_push.py`,
  `local/work/monitor-boot-64.sh`; cross-built `ringdump` from
  `tools/bench/ringdump.c`.
* Traps 322/325/328/330/331; DECISIONS §91; report 257 is the companion.
