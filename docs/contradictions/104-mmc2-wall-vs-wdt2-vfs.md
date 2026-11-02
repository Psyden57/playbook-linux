# The ~1.5 s MMC2 "wall": driver register stalls vs the WDT2 deadline

**Status: resolved — the current reading is B.  Report 104 §2's "current wall is
MMC2 card init" is superseded; the ~1.5 s cluster is the bootloader-armed WDT2
arriving before `prepare_namespace()`, and the eMMC blocker was `ocr_avail = 0`,
not a register stall.**

## The claims

**Claim A (report 104 §2, session 8).**  The clean kernel reaches ~1.51 s and
then stops inside MMC2 card init.  The host probe, power-up and CMD0 init stream
complete (`PBM/PBC/PBSIO/PBINI` up to `add_host`); `mmc_rescan` then stalls at
MMC2 register accesses in a **timing-dependent** way.  One run stopped in
`mmc_go_idle`'s 1 ms `mmc_delay` after `cs_high`; replacing it with `mdelay(1)`
moved the stop *earlier*, into `send_init_stream`'s `STAT` poll (`PBINI cmd
sent`, no `cc=1`).  The suggested cause was the MMC2 module clock / `l4_per`
clock-domain / the MMC child not holding the ti-sysc parent, with `ti,no-idle`
and a clkdm deny-idle as the next experiments.

**Claim B (report 105 §2, `109` §2/§5.2/§9, sessions 9 and 10).**
* With the console silenced (`loglevel=0`) the event log shows the **whole MMC
  path completes**: `set_ios`/`set_clock`, CMD1 ×3 with real completion IRQs,
  `mmc_attach_mmc` returning (with `-EINVAL`), `mmc_power_off`, `mmc_rescan:
  out`, then hsmmc RPM autosuspend/resume.  There is no MMC driver hang
  (`105` §2.1).
* The MMC2 interconnect target is **provably awake** at probe and at every
  `set_ios`: `fck = 96 MHz`, `CM_L3INIT_HSMMC2_CLKCTRL` `MODULEMODE = 2`,
  `IDLEST = 0`, `STBYST = 0`, `CLKTRCTRL = SW_WKUP`, `SYSCONFIG = 0x14`
  (`105` §2.4, session 9 §1).
* The ~1.5 s cluster is the **WDT2**: on the Linux/chainloader path `LDR =
  0xfffc1800` → **7.812 s**, re-armed 7 ms before the chainloader, and it counts
  up.  With the untrimmed chainloader (~4.8 s) plus the **1 520 ms**
  decompressor/early-boot offset, the 7.812 s deadline lands at ~1.5 s of
  *kernel* time.  The log then ends wherever the boot was
  (`109` §2.2/§5.2; session 9 §Superseded).
* The real end of a WDT-petted boot without an initramfs is the **VFS panic**
  (`prepare_namespace()`, no root filesystem) at 2.24 s silent / 2.44 s with the
  console; `panic()` disables IRQs, so the tick stops and it looks like a
  freeze (`109` headline, `105` banner).
* The eMMC failure was `-EINVAL`, not a timeout: CMD1 returned
  `resp = 0xc0ff8080`, but `host->ocr_avail = 0` because `&mmc2` had no supply;
  `mmc_select_voltage()` masked the OCR to zero.  Fix: the `vemmc` fixed 3.3 V
  regulator + `vmmc-supply = <&vemmc>`, then `mmc1: new high speed MMC card …
  MCGAFA 59.4 GiB` (`109` §9).

## Evidence

* Claim A's measurements are real: the DRAM ring stops at 1.49–1.51 s with
  different last lines (`104` §2; `docs/captures/...-mmc-goidle-ring.txt`,
  `...-mmc-delay1-ring.txt`, `...-mmc-mdelay-ring.txt`), and the `mdelay` run
  moved the last line earlier.  What was missing was a way to tell a driver
  stall from the watchdog deadline: there was no event log, no epoch block, and
  WDT2 was not petted.
* Claim B's event log is a direct measurement of the same MMC path completing
  with the console silenced (`105` §2.1; `docs/captures/...-silent-events.txt`,
  `...-poll-events.txt`, `...-fine-events.txt`).
* The WDT2 arithmetic is measured: `LDR = 0xfffc1800`, `CRR` counts up,
  `TGR` kicks reload it, and `LDR` writes are dropped (`109` §8.1); the epoch
  test placed kernel time 0 at 1 519 ms after the jump (`109` §5.2).
* The OCR prints are measured: `PBOCR: resp=c0ff8080 host_avail=00000000` then
  `no support for card's volts`, and after `vemmc` `host_avail=00300000`
  (`109` §9).

## Resolution

Claim B.  The 104 "MMC2 register stalls" were the **WDT2 deadline** changing
which printk was in flight, not a hardware/clock stall.  The MMC host path was
sound; the eMMC blocker was the empty `ocr_avail`, fixed by `vemmc`.

## Still uncertain

* Which of session 8's other stops (ABE `0x40130000`, EMIF1/2 `0x4c/0x4d000000`,
  DSS `0x58000000`, SGX `0x5600fe00`, MCSPI4 `0x480ba000`) are genuine target
  hangs when reached on a **WDT-petted** build.  Session 8 only saw them at the
  ~1.5 s deadline.  Later sessions kept ABE/EMIF/SGX/IVA disabled, re-enabled
  MCSPI4 (panel TCON) and brought DSS up with the power-domain recipe, but did
  not systematically re-test each target with the watchdog neutralised.
* The DES `0x480a5030` abort is a **real** L3-firewall/imprecise abort
  (`Internal error: : 1406`), independent of the deadline; the crypto disables
  are not explained away by claim B.

## What would settle the remainder

A WDT-petted build (`pb_wdt_kick()` on a 2 s `timer_list`, as in `109` §8.1)
with the printk-free event log, re-enabling ABE, EMIF1/2, DSS, SGX/IVA and MCSPI
**one target at a time**: each target either probes to completion (no hang) or
stops with an event-log entry and no heartbeat, and the two are now
distinguishable because the deadline no longer moves.

## What this cost, and the lesson

Session 8 spent a long cycle sequence disabling one target after another and
watching the stop move, which is exactly what a fixed wall-clock deadline looks
like when the console's cost changes which code is running at the deadline.
The generalisable lesson: **"last printed line" is not the time or cause of
death**; without a WDT-neutralised boot and a printk-free event log, a driver
that happens to be running at the deadline will be blamed.  Session 9's event
log and session 10's WDT2 arithmetic are what closed it.
