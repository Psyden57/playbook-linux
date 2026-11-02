# Session 8 (absolute 8, Linux-era "session 2") — capture v2, and the boot "walls" before the WDT2 deadline was understood

> Numbering: absolute **8** of 12.  In the reports and handoffs this is
> **"session 2"** — its output is `docs/reports/104-clean-kernel-capture-v2-2026-10-26.md`
> and `docs/reports/HANDOFF-2026-10-26-clean-kernel-v2.md`.  See `NUMBERING.md`.
> Sessions 9–12 are newer; their notes and `105`/`109` correct this session's
> central MMC2 conclusion — read *Superseded* before reusing anything below.

Date: 2026-10-26 (device logs are ART = UTC−3).
Unit state at the end: TabletOS from the sealed IFS (sha256 `c72b0530…`, verified
by restore + hash), door up, `BOOTCTR_FCTRY` read `0x32`.  Media restored; last
image on disk `chainloader/kernel.img` CRC **`0x51A36ED9`**, not on media.  At
this session's close the source patch was **20 files / 1 247 lines** (the tree
grew to 29/2 922 by session 12).

## Goal of this session

Start from `103-clean-kernel-crc-bringup-2026-10-26.md` and its handoff.  The
state they described: a clean 6.15.11 boots through the CRC/chainloader path,
runs ~1.33 s of driver init, then "hangs at/after `gpiochip1`" with
`ti-sysc 4a318000.target-module: probe with driver ti-sysc failed with error
-16`; the ring capture was ambiguous.  The ordered next steps were:

1. make the DRAM ring capture unambiguous (it wraps at 3 840 chars and
   interleaves);
2. isolate the GPIO2+/PER-domain/PRCM hang (disable `CONFIG_GPIO_OMAP` or the
   higher banks; then fix the PER-domain access);
3. continue init (i2c/PMIC, MMC, display, USB).

Operator rules kept: all writes and the live device in the main session,
subagents read-only; the MMC probe read-only (no mounts, no `dd`); RPMB off;
single owner of the USB link.

"Done" was to be: a trustworthy capture, the exact stop point, and the boot past
it.  What happened: the capture became trustworthy, the stop point was found
repeatedly, but **the stops were interpreted as per-driver hangs when they were
the bootloader-armed WDT2 deadline** (see *Superseded*).  Session 9 supplied the
event log and the answer; session 10 supplied the WDT2 arithmetic and the eMMC
fix.

## Confirmed (measured, read in a file, or seen on the bench)

### 1. The old capture was the reason the `gpiochip1` stop looked real

* The old ring is 3 840 chars at PA `0x90000100`, count at `+0x80`, index at
  `+0x84`, written through the **cached linear alias** (`VA = PA + 0x40000000`),
  and flushed only for ring1 and only `+0x80..+0x4FF` every 64 chars by a PL310
  clean+invalidate by PA (`103` §3.3; old `omap4bc.S`).  At the end of the old
  session the ring header read `count = 0x30b7` (12 471), `index = 0x3b7` —
  i.e. it had wrapped ~3×.
* I re-read the live ring after the OS was up and decoded the old captures:
  `docs/captures/playbook-live-spare-2026-10-26-clean-{cleanlog,econ-ring,
  noidle-ring,dm-ring,ck-ring}.txt`.  They contain real boot text but the tail
  is a mosaic (old evictions, `0xAA` fill, interleaved fragments), not the end
  of the boot.  The "1.33 s / `gpiochip1`" last line was therefore **not** the
  kernel's last output.
* The first capture-v2 attempt (cacheable ring + a DCCMVAC then PL310 CIPA
  every 8 chars) reproduced a new, real wall at **L2C init**: the log ended in a
  `[[    0.0` storm after `L2C-310 enabling early BRESP for Cortex-A9`.  That is
  the non-secure PL310 maintenance aborting once the L2C driver owns the PL310
  through secure SMC writes (`104` §1 item 3; the `kernel-clean` patch's
  `omap4bc.S` comment).
* The fix that worked: a **dedicated non-cacheable ring alias**, no cache
  maintenance at all (`104` §1; `kernel/source/omap4bc.S`; tree changes below).

### 2. Capture v2 as built this session

* Ring geometry: 32 KB per ring, `+0x80` monotonic count, `+0x84` wrapping index
  (wrap `0x7F00`), characters `+0x100..+0x7FFF`; the checker
  `analysis/pb_ring.py` (now `tools/pb_ring.py`) rotates by the index and
  truncates by `count`.
* MMU-on debug writes use `0xFE800000 / 0xFE900000 / 0xFEA00000` for PA
  `0x88000000 / 0x90000000 / 0x94000000`; MMU-off uses the PAs.  The aliases are
  `MT_MEMORY_RWX_NONCACHED` in `omap44xx_io_desc` (`arch/arm/mach-omap2/io.c`)
  and normal non-cacheable sections in `arch/arm/kernel/head.S`; the ring pages
  are `no-map` in the board DTS, so there is no cacheable linear alias.
* `stdout-path` is removed from `chosen`, so the 8250 driver does not register
  `ttyS2` as a second console; earlycon is the only console.
* The ring page proved **stable for minutes** after the OS boot: two dumps of
  `0x88000000` were byte-identical (`docs/captures/...-nosgx-ring.txt` vs a
  re-dump; also `104` §1).
* The kernel's own printk ring also survives and was reconstructed:
  `__log_buf` at PA `0x81027ad0` (64 KB), descriptor array at PA `0x80f523c8`,
  stride 12, lpos `0xFFFF0000 + offset`; it starts at `Booting Linux` but its
  tail is unreliable.  Files: `docs/captures/...-full-boot-2026-10-26.txt` (the
  clean DRAM-ring tail) and `...-full-boot-from-printk-ring.txt` (early boot;
  a 2-byte duplicated tail on each record was trimmed heuristically).

### 3. The stop points this session saw (as measured)

All stops are in **kernel time** (printk timestamps; HZ=100).  The last line
moved with the code; the time clustered at ~1.49–1.51 s.

| last line | capture / evidence | what I changed |
|---|---|---|
| `PB: post-abt` … normal init | old ring, `104` §1 | capture v2 |
| L2C init `[[    0.0` | first v2 attempt, `104` §1 | non-cacheable ring |
| `PBS: 480a5030.target-module rev read` then `L3 Custom Error: MASTER MPU TARGET L4PER2 (Read): Data Access in User mode during Functional access`, `Unhandled fault: imprecise external abort (0x1406)`, `Internal error: : 1406 [#1] ARM` | `104` §2 (log text) | disable aes1/aes2/sham/rng/des |
| `PBS: 48098000 … idle` (MCSPI1) | `...-noabe-ring.txt`; `104` §2 | board-wide `NO_IDLE_ON_INIT`; later disable mcspi1–4 |
| `PBS: 40130000.target-module rev=502a0500` (ABE WD_TIMER3) | `104` §2 | `&l4_abe` disabled |
| `PBS: 4d000000.target-module reset` then `PB` (EMIF2) | `...-mmc-ring.txt`; `104` §2 | EMIF1/2 target modules disabled |
| `PBS: 58000000 …` / `5600fe00` (DSS / SGX) | `...-mmc-core-ring.txt` (DSS) / `...-noemif-ring.txt` (SGX); `104` §2 | DSS `ti,no-reset-on-init`; SGX/IVA-HD disabled |
| `PBSIO: mmc1 done` after `mmc_go_idle cs_high` (1 ms `mmc_delay`) | `mmc-goidle`, `mmc-delay1` captures; `104` §2 | MMC2 `no-sdio; no-sd;`; stray-IRQ ack |
| `PBINI: mmc1 cmd sent` without `cc=1` in the `mdelay` build | `mmc-mdelay` capture; `104` §2 | reverted the experiment |

* The DES event is a **real abort**, not a truncation: full call trace, L3 NOC
  error, `Internal error: : 1406 [#1] ARM`, and `note: kworker/u4:3[26] exited
  with irqs disabled`.  The address `0x480a5030` is the **DES** target's
  revision register (`omap4-l4.dtsi`, `des_target`), in `l4_secure_clkdm`.
* The EMIF addresses are `0x4c000000` (EMIF1) and `0x4d000000` (EMIF2), the two
  DRAM controllers; both read `rev = 0x40441403`.  EMIF1's `sysc_reset`
  completed; EMIF2's `reset` was the last full line.  I treated this as
  "never reset a live DRAM controller" and disabled both.
* The DSS target is `0x58000000` (its child `dss@0` is disabled in the DT at
  this point); SGX is `0x5600fe00`; IVA-HD is `0x5a000000`.
* The MMC2 host probe did reach the end of the driver path:
  `PBM: 480b4000.mmc probe → of_parse → fclk → pm_get → ctx_save → bus_power →
  rx/tx dma → reg_get → add_host`; `PBC: mmc1 device_add → start_host →
  power_up`; `PBSIO` (pm=1, 0 Hz; pm=2, 400 kHz); `PBINI` (CMD0 init stream,
  `cc=1`); `set_bus_mode`; `PBC: power_up done → detect_change → start_host
  done → PBM: 480b4000.mmc add_host done → mmc1 rescan`.  The MMC2 node was marked
  **eMMC-only** (`no-sdio; no-sd;`) after `sdio_reset`'s CMD52 was the last
  line in one run (`104` §2).
* The MMC `mmc_delay(1)`/`mdelay(1)` experiment: with `mdelay` the stop moved
  *earlier*, into `send_init_stream`'s `STAT` poll (`PBINI cmd sent`, no
  `cc=1`).  That is the measured fact; the interpretation is under *Dead ends*.

### 4. Recovery after a restore flash — the exact sequence

Used unchanged for every cycle (a dozen-plus image boots and restores this
session):

1. Start `pre_os_ifs_flash.py --template-ifs <sealed IFS> --delay 40` **before**
   the unit resets.  It sleeps 40 s, then polls for the bootrom; it caught the
   WDT self-reset every time.
2. Wait for the tool to exit.  It uploads the ramloader, `PreFlash 0x20EE`
   (`→ 0x20DE`), 181 frames, `0x40F9` (signature), `0x40C0`, then calls
   `bb.reboot_loader()` and closes.
3. Run `force_reset_loader.py` **once**.  It opens USB `0x8001` and sends
   `0x80EF` first: `0x80EF -> 0x0`, then `0xEF`/`0x80EF` fail with
   `[Errno 19] No such device` (the disconnect is success); it then tries to
   reopen, prints `no ramloader enumerated`, and exits 0.  No power-cycle.
   If it prints `no ramloader enumerated` on the first open, the loader is
   already gone — just wait.
4. Poll SSH (`timeout 8`, every 3 s) until the door reconnects; 17–25 tries,
   ~1–2 min.  Then read media/counter.

I did **not** stop or change `bb.reboot_loader()`; the tool still calls it at
the end of both the kernel and restore flashes.  The explicit `0x80EF` is the
reliable final step because the tool's own reboot often leaves the unit at
`0fca:8001`.  `0xEF` alone was never reliable.  (Later docs state the same
sequence: `HANDOFF-2026-10-27-session6.md` §4.5, bootstrap §2, and the
`force_reset_loader` entry in `SETUP.md` §8.)

### 5. PMIC message: `twl6030_uv_to_vsel: OUT OF RANGE`

* During every boot, at ~0.38 s, the log has six `non mapped vsel for 1375000 Vs
  max 1316660` and one `for 1410000`, then `Power Management for TI OMAP4+
  devices` and `SmartReflex Class3 initialized` (`104` §4a; the `full-boot-
  from-printk-ring.txt` capture, lines ~171–182).
* Source: `arch/arm/mach-omap2/omap_twl.c:88-125`, `twl6030_uv_to_vsel()`.  It
  reads the TWL6030 `SMPS_OFFSET` efuse; with `SMPS_OFFSET = 0` the SMPS range
  tops out at `twl6030_vsel_to_uv(0x39) = 1 316 660 µV`.  The OMAP4 VC/VP data
  asks for ON/ONLP `OMAP4_ON_VOLTAGE_UV = 1 375 000`
  (`arch/arm/mach-omap2/vc44xx_data.c:91`) and VP `vddmax =
  OMAP4_VP_MPU_VLIMITTO_VDDMAX = 1 410 000`
  (`arch/arm/mach-omap2/voltage.h:102`), so those values take the `pr_err`
  branch and are **clamped to `0x3A`** (the function returns, it does not fail).
* This is a VDD_MPU/IVA/CORE VC/VP issue, not the eMMC (MMC2 has no
  `vmmc`/`vqmmc` in the DT at this point), and it did not stop the boot.  The
  open risk is DVFS/SmartReflex at high OPP (see *Unknown*).

### 6. Device / tree facts

* `no-sdio; no-sd;` on `&mmc2` survived into the consolidated tree
  (`kernel/source/omap4-winchester.dts:242-251`).  `&mmc2_target` and
  `vmmc-supply = <&vemmc>` are **later** sessions' work (9 and 10).
* `mmc1` in the logs is `&mmc2` (the eMMC at `0x480b4000`), because
  `omap4.dtsi` aliases `mmc1 = &mmc2` (session 9 §9).  My logs say
  `PBM: 480b4000.mmc` / `PBC: mmc1 …` for the same device; do not read the
  label as the controller number.
* The current patch's crypto/ABE/SPI1–3/EMIF/SGX/IVA comments and disables are
  the ones written this session (`kernel/source/omap4-winchester.dts:311-438`);
  MCSPI4 was **re-enabled** later (session 12) for the panel TCON, and DSS was
  brought up with the power-domain recipe (sessions 11–12).
* The `PBR: <dev> <- <driver>` print is `drivers/base/dd.c` immediately before
  the probe (bootstrap §6 trap 2).  A `PBR:` line with no driver follow-up is
  not proof of a probe hang at a WDT deadline (see *Dead ends*).
* Media/counter at the end: hash `c72b0530…`; `BOOTCTR_FCTRY = 50` (read with
  `dd … | od -An -tu4`, no `od` on QNX).

## Inferred (reasoning from evidence, not directly measured)

* **Why the old capture was a mosaic.**  The ring VAs are write-back cached;
  a PL310 clean-by-PA alone does not clean the dirty L1 lines, and only the
  first `0x480` bytes were ever flushed.  Combined with the decompressor's
  non-cached 1 KB wrap (`uncompress.h`) and the kernel's cached 3.8 KB wrap,
  reading ring2 mixed two different windows.  Strength: strong for the cache
  part (the same L1 behavior was reproduced by the L2C abort/storm and fixed by
  the non-cached alias); the decompressor-window mismatch is a code reading.
* **The board-wide `NO_IDLE_ON_INIT` was an idle-race workaround.**  I inferred
  a race between a ti-sysc target's probe epilogue (clock disable +
  `sysc_clkdm_allow_idle`) and the child driver's first register access.  The
  later session 9 showed `sysc_runtime_resume()` still allows the clkdm idle and
  that the MMC2 target was awake; the workaround is defensive, not proven
  causal for the 1.5 s stops.
* **The DES L3 error is a firewall denial.**  The error names `MASTER MPU`,
  `TARGET L4PER2`, read, "User mode during Functional access"; the target is in
  `l4_secure_clkdm`.  Strength: strong (the abort and the secure clock domain
  agree), though the exact firewall register was not read.
* **The per-driver stops were driver hangs.**  This was the working assumption
  at the time (the last line was inside the driver; removing the driver moved
  the last line).  It is now known to be wrong as an interpretation — see
  *Superseded*.
* **The PMIC clamp may lower the CPU voltage.**  `_vp_set_init_voltage()` writes
  the clamped VSEL and triggers `initVDD`; whether it actually changes the
  bootloader-set rail was not measured.  Strength: the code path is read; the
  rail effect is not.
* **GIC PPI 29 is the local timer.**  I did not trace GIC; this was general
  Cortex-A9 knowledge offered to the next session.  The precise mapping measured
  later is INTID 29 = `GIC_PPI 13` = the A9 local timer (session 9 §6;
  `omap4.dtsi` `GIC_PPI n = INTID 16 + n`).

## Dead ends and wrong assumptions (the expensive part)

1. **"The `gpiochip1` hang is real."**  It was the old capture's mosaic
   (`104` §1).  The clean ring showed the boot running past GPIO2–6 and i2c1–4.
2. **"Each stop at ~1.5 s is a driver hang."**  This is the central error.  The
   stops moved *through* DES → MCSPI → ABE → EMIF → DSS/SGX → MMC as I disabled
   the previous one, but the kernel time stayed ~1.49–1.51 s and the last lines
   were often mid-print.  Session 9/10 showed the ~1.5 s cluster is the
   **bootloader-armed WDT2** (7.812 s after arming; `LDR = 0xfffc1800`) arriving
   before `prepare_namespace()`, with the untrimmed ~4.8 s chainloader putting
   kernel entry late enough that 7.812 s lands at ~1.5 s of kernel time
   (`109` §2.2/§5.2; session 9 §Superseded).  Removing a driver did not "fix a
   hang"; it let the CPU reach the next driver before the same wall-clock
   deadline.  The partial lines (e.g. `PB`) are the reset cutting a printk.
3. **"MMC2 register accesses stall; the clock/parent PM is the wall."**  Session
   9's PBX readback showed `MODULEMODE=ENABLED`, `IDLEST=0`, `STBYST=0`,
   `CLKTRCTRL=SW_WKUP`, `SYSCONFIG=0x14`, `fck=96 MHz` at probe and at every
   `set_ios`; with the console silenced the event log showed the whole MMC path
   completing (CMD1 ×3, attach, power-off, rescan out; session 9 §1/§3,
   `105` §2.1).  The actual eMMC failure was `host->ocr_avail = 0`
   (`mmc_select_voltage()` masked the card's `0xc0ff8080` OCR to 0 and
   `mmc_attach_mmc()` returned `-EINVAL`), fixed by the `vemmc` fixed 3.3 V rail
   + `vmmc-supply` (session 10, `109` §9).  My MMC2 `no-sdio/no-sd` change and
   the stray-IRQ ack were kept, but they were not the wall.
4. **The `mdelay(1)` test.**  I replaced `mmc_delay(1)` in `mmc_go_idle` with
   `mdelay(1)` to test the sleep path; it did not fix anything and the stop
   moved earlier (to `send_init_stream`'s STAT poll).  That looked like a
   "timing-dependent register stall"; it is better explained by the WDT deadline
   changing which printk was in flight.  The change was reverted.
5. **The `NO_IDLE_ON_INIT` comment says "3 s".**  The code schedules
   `schedule_delayed_work(&ddata->idle_work, 3000)` — **3 000 jiffies = 30 s at
   HZ=100**, not 3 s.  Session 9 §1 caught it; the comment in the tree is still
   wrong.
6. **I did not test a heartbeat past the stop.**  The next session asked; my
   answer was "not done".  Without it, the tick's survival and the CPU's
   survival could not be separated.  Session 9 built the event log for exactly
   this.
7. **I did not trace GIC PPIs, read the target at the stop, or try a clkdm-level
   deny-idle.**  The next session's questions covered all three; my answers were
   "not done" and "the target readback (session 9) kills the idle theory".
8. **The printk ring stop at seq 894 / ts 1.415 s is not a death time.**  It is
   the cacheable ring's stale tail; the non-cacheable DRAM ring ran to 1.49 s
   (session 9 §5; `104` §1).
9. **The `PBR:` print can mislead at a deadline.**  I added it so a
   `PBR`-without-follow-up means a probe hang; at the WDT2 deadline the
   `PBR` victim is whichever probe was running, not a hung probe.

## Unknown / open questions

* **Which of the ABE/EMIF/DSS/MCSPI probes actually hang when reached with the
  WDT2 petted?**  This session only saw them at the ~1.5 s deadline.  Later
  sessions kept ABE/EMIF/SGX/IVA disabled, re-enabled MCSPI4, and brought DSS up
  with the power-domain recipe, but did not systematically re-test every target
  with the watchdog neutralised.  Resolve by re-enabling one target at a time on
  a WDT-petted build and using the event log.
* **Did VC/VP init actually lower the CPU rail to the clamped `0x3A`?**  The
  code path is read; the PMIC/rail effect was not measured.  Resolve with
  `pmic-twl6030-ctl get_vout`/`get_state` on the reference and a Linux
  `regulator` read, or by reading the TWL registers from QNX.
* **The exact loader phase in these runs.**  I had no epoch block; session 10
  measured 1.77–4.78 s and the 1 520 ms decompressor offset (`109` §2/§5.2).
  That is what converts my kernel-time ~1.5 s to the WDT2 deadline.
* **The GIC PPI numbering.**  Later resolved: INTID 29 = `GIC_PPI 13` = A9
  local timer; INTID 37 = `GIC_SPI 37` = GPTIMER1 (session 9 §6).  My session
  did not measure it.
* **The event-ring ordering anomaly and the `PB*` cleanup debt.**  Session 9
  raised both; the debug prints (`PBG/PBS/PBM/PBC/PBSIO/PBCLK/PBINI/PBREQ/
  PBIRQ/PBR` and the later `PBX`/`PBEV`) are still in the patch at the time of
  writing.

## Superseded / uncertain

* **Report 104 §2's "current wall is MMC2 card init".**  The current reading is
  session 9/10/`109`: the ~1.5 s cluster is the WDT2 deadline; the MMC path
  completes; the eMMC card answered and the host's `ocr_avail` was 0.  See
  `docs/contradictions/104-mmc2-wall-vs-wdt2-vfs.md`.  My evidence (the
  timing-dependent last lines at ~1.49–1.51 s, the `mdelay` run, the partial
  `PB`) is preserved in `104` §2 and the `mmc-*` captures; the interpretation is
  not current.
* **"The MMC2 target is gated / the parent is not held."**  Session 9 measured
  the target awake (`fck=96 MHz`, `MODULEMODE=2`, `IDLEST=0`, `STBYST=0`,
  `CLKTRCTRL=SW_WKUP`, `SYSCONFIG=0x14`) and showed the rescan completing; the
  `&mmc2_target { ti,no-idle; sidle/midle NO; }` + device link are session 9's
  defensive work, not a proven fix (`105` §2.4, session 9 §1).
* **The `NO_IDLE_ON_INIT` "3 s" comment.**  Superseded by 3 000 jiffies = 30 s
  (session 9 §1; `ti_sysc_idle()`).
* **Stray-status ack attribution is uncertain.**  Report 104 §0 says I added the
  `omap_hsmmc_irq()` stray-status ack; session 9 §2 says it added the `else`
  branch and the comment naming the init-stream race.  Both accounts describe an
  `else` ack in the same handler; the exact final authorship of the hunk is not
  recoverable from the captures.  What is measured is the pre-ack
  `PBIRQ … stat=00000001 req=1` storm (session 9's `console-boot1-ring.txt`) and
  the quiet post-change runs.  Treat the ack as a shared/refined change.
* **The `mmc_delay`/`mdelay` "timing-dependent register stall" reading.**  The
  measurements are real; the "register access stalls" interpretation is
  superseded by the WDT deadline.
* **The `mdelay` experiment was reverted**; the tree has `mmc_delay(1)` again
  (`drivers/mmc/core/mmc_ops.c`).
* **The PMIC clamp note stands as an analysis, not a measurement.**  Later
  sessions measured the rails on the reference (VAUX1/2/3 at 2.8 V, etc.) but
  did not re-measure this VC/VP path; the clamp behavior is still the source
  reading plus the log.
* **My PPI 29 answer is imprecise.**  INTID 29 = `GIC_PPI 13` = local timer
  (session 9 §6); "PPI 29" as an INTID is not the same as `GIC_PPI 29`.
* **The image CRC.**  `0x51A36ED9` is this session's source-matching image; the
  current best at handover is `0xE1F79294` (`STATE.md`, `HANDOFF` §0).  Do not
  confuse them.
* **The recovery sequence is not superseded.**  The later docs (`HANDOFF` §4.5,
  bootstrap §2) state the same `--delay 40` + one `force_reset_loader` (0x80EF
  first) + wait-for-door sequence.  One invocation per phase, no power-cycle.

## Artefacts produced

* `docs/reports/104-clean-kernel-capture-v2-2026-10-26.md` — the session report:
  capture v2, the wall table, the MMC2 detail, the PMIC §4a, the gotchas.
* `docs/reports/HANDOFF-2026-10-26-clean-kernel-v2.md` — the handoff to session
  9 (superseded by `HANDOFF-2026-10-27-session6.md` for current state, but the
  session-8 command/state record).
* `analysis/pb_ring.py` (now `tools/pb_ring.py` per the tree) — the 32 KB ring
  dumper/decoder; count/index rotation, `--ring`, `--out`.
* Capture v2: `arch/arm/include/debug/omap4bc.S` (32 KB, wrap `0x7F00`,
  non-cacheable VA/PA pairs, `dsb` every 8 chars), `arch/arm/kernel/head.S`
  (early non-cacheable section maps), `arch/arm/mach-omap2/io.c`
  (`MT_MEMORY_RWX_NONCACHED` aliases), `arch/arm/boot/dts/ti/omap/
  omap4-winchester.dts` (`no-map` ring reservations; no `stdout-path`),
  `arch/arm/include/debug/uncompress.h` (decompressor ring consistency).
* Boot-wall work: board-wide `SYSC_QUIRK_NO_IDLE_ON_INIT` in
  `drivers/bus/ti-sysc.c`; board DTS crypto/ABE/MCSPI1–3/EMIF1–2/SGX/IVA-HD
  disables and DSS `ti,no-reset-on-init`; `&mmc2 { no-sdio; no-sd; }`;
  `omap_hsmmc_irq()` stray-status ack; `PBG/PBS/PBM/PBC/PBSIO/PBCLK/PBINI/
  PBREQ/PBIRQ` checkpoints; `PBR:` in `drivers/base/dd.c`.
* Captures in `docs/captures/` (selected): `...-nocrypto-ring.txt`,
  `...-noabe-ring.txt`, `...-noemif-ring.txt`, `...-nosgx-ring.txt`,
  `...-pbr-ring.txt`, `...-mmc-{goidle,delay1,mdelay,rescan,irqfix}.txt`,
  `...-full-boot-2026-10-26.txt`, `...-full-boot-from-printk-ring.txt`.
* Images: `chainloader/kernel.img` CRC `0x51A36ED9` (source-matching at session
  close; not on media).  The patch at session close: 20 files / 1 247 lines;
  `CONFIG_MMC_BLOCK` was toggled to `n` for the MMC probe builds and restored to
  `y`; `CONFIG_MMC_RPMB` off; no initramfs/userspace.
