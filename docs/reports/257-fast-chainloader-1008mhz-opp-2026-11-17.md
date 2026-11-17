# 257 — fast chainloader (VC + DPLL) gives 1008 MHz and a wide watchdog margin; dual-core boot is flaky

Date: 2026-11-17 (session 63, Linux-era 57; continues report 256).

Bench at the end: region 0 = `local/work/kernel-fastcl-nosmp.img`, CRC
`0x8B83421F` (verified off the media trailer `1f2dc8d7 1f42838b`), the `#78`
SMP zImage with a VC 1.35 V + MPU DPLL 1008 MHz chainloader and a
`loglevel=0 nosmp` DTB.  Linux `7.3.3 #78 SMP`, `online = 0`, `dpll_mpu_ck =
1008000000`, root `/dev/loop0` ext4 on hd3, dropbear on `169.254.0.1:22` after
an entropy seed.  `BOOTCTR_FCTRY = 0x20`.

## 0. Headline

* A **fast chainloader** that commands VDD_MPU = 1.35 V and programs the MPU
  DPLL to **1008 MHz at entry** (before the copy/decompress) cuts kernel entry
  from ~7.0 s to **3.276 s of the loader's 7.812 s window** — ~4.5 s spare —
  and the 1008 MHz clock persists through boot.  Single-core boot is now
  reliable (3/3 clean reboots).
* `clk_set_rate(dpll_mpu_ck, 1008000000)` from a loadable module gives a
  measured **2.54× CPU speedup** (13.63 s vs 34.66 s on a fixed `awk` loop).
  **1.2 GHz crashes** the unit.
* The MPU rail is correctly commanded to the 1 GHz OPP voltage (VSEL `0x3A` =
  1.35 V) and the battery is healthy (97 %, 3.819 V) — **the dual-core wedge is
  not a power problem**.  Dual-core 1008 MHz boots successfully at least
  sometimes, but nondeterministically (1 success / 2 wedges); the remaining
  fault is a timing/race in the IPI/TLB-flush path.

## 1. The chainloader DPLL/VC program

`boot/chainloader/chainloader_kernel.S.in` (tracked; grew 852 → 1000 B) gains,
at `_start` before the copy:

* **VC bypass**: write `0x003A5512` to `PRM_VC_VAL_BYPASS` (`0x4A307BA0`), then
  set VALID (bit 24), poll for the SMPS ack.  `(vsel<<16)|(reg<<8)|slave` with
  VSEL `0x3A` (1.35 V), MPU volt reg `0x55`, slave `0x12` — the kernel's own
  `omap_twl.c` / `vc44xx_data.c` constants.
* **MPU DPLL**: `CM_CLKMODE = 5` (low-power bypass) → poll `CM_IDLEST` bit0 == 0;
  `CM_CLKSEL = 0x0080D207` (M = 210, N = 8, 38.4 MHz × 210 / 8 = 1008 MHz);
  `CM_CLKMODE = 7` (locked) → poll IDLEST bit0 == 1.  Each poll bounded
  (~2^20 loops); WDT2 is never written (trap 322).

Verified on media after boot: `PRM_VC_VAL_BYPASS = 0x003A5512`,
`CM_CLKSEL_DPLL_MPU = 0x0080D207`, `CM_CLKMODE = 0x7`, `clk_summary
dpll_mpu_ck = 1008000000`.  The pre-change template copy is
`local/work/carve/chainloader_kernel.S.in.session63-orig`.

## 2. The 1008 MHz OPP measurement

`local/work/src/pbopp.c` (module, vermagic `7.3.3 SMP`) calls
`clk_get(NULL, "dpll_mpu_ck")` then `clk_set_rate()`:

| clock | `awk` 3,000,000-iteration loop (real) |
|---|---|
| 400 MHz | 34.66 s (user 33.21 s) |
| 1008 MHz | 13.63 s (user 13.22 s) |

2.54× ≈ 1008/400.  The TI clock driver reprogrammed the hardware directly
(`omap3_noncore_dpll_program`: bypass → M/N → lock).  Reverting with
`rate=400000000` restored 400 MHz.  **1.2 GHz** (`rate=1200000000`) produced no
output; the device re-enumerated (`0fca:0001`, new USB device number) and SSH
died — an immediate crash/reset.  The clock driver is not bounded by the DTB
OPP table (which tops out at 1008 MHz); the hardware simply will not sustain
1.2 GHz at this voltage.

## 3. Watchdog margin and boot reliability

Ring timing from `local/work/r2-fastcl.words` (this boot's own words):

| phase | before (session 62/63) | with fast chainloader |
|---|---|---|
| loader arm → chainloader entry | 0.007 s | 0.007 s |
| chainloader entry → jump | 0.316–0.387 s | 0.316 s |
| jump → kernel entry (XZ) | 6.6 s | **2.959 s** |
| elapsed at kernel entry | ~7.0 s | **3.276 s of 7.812 s** |

The carved `#78` zImage lacks the `head.S` entry-CRR stamp, so the jump→entry
number comes from the hand-off timing of the following read-back boot.  Three
consecutive reboots of `kernel-fastcl-nosmp.img` each reached the OS (≈24 s),
versus the earlier flaky ~0.8 s margin.

## 4. Dual-core: works, but nondeterministically

* `kernel-smp-cpu1-mark4.img` (`0x0A80BDC6`) at 400 MHz produced a full dual-core
  boot: `PBCPU: post smp_init, online cpus=02`, all `PBCPU1:` markers through
  `post complete`, `nproc=2`, `twd` interrupts on both CPUs.
* With the fast chainloader at 1008 MHz, `kernel-ipidbg-cpu1.img` (`0xF8264016`)
  also booted cleanly once as `#91 SMP`, `nproc=2`, `online=0-1`, both cores at
  1008 MHz, `twd` on both (10933 / 11536), stable for >2 min (benchmark 13.71 s).
* But a 3-boot set gave **1 success / 2 wedges**.  The operator observed the
  wedge as a lit backlight with a frozen (or absent, blink-phase) cursor, then
  a reset.  The failure is a clean `execve("/init")` hang, not a power reset.
* Runtime CPU1 onlining was also tried (`maxcpus=1` DTB,
  `kernel-vcfast-maxcpus1.img`, `0x09DDA895`): `echo 1 >
  /sys/devices/system/cpu/cpu1/online` hung and `online` stayed `0`.

To localise the stall, markers were added to the IPI path in the tree:
`PBSMF: pre/post csd_lock_wait` in `kernel/smp.c` (CPU0, where `execve`'s
TLB-flush IPI blocks) and `PBIPI1: CALL_FUNC enter/done` in
`arch/arm/kernel/smp.c` (CPU1 service handler, `do_handle_IPI`).  They did not
remove the nondeterminism — a race, not a single stuck statement.  Reading the
last markers from a *wedged* boot is the next session's first experiment.

## 5. Power is ruled out

* Battery (`bq27541-0`): `voltage_now = 3819000`, `capacity = 97`,
  `status = Full`; the unit is on USB charging (`isl9519 online = 1`,
  `input_current_limit = 384000`).  No flat-pack brownout.
* MPU rail: `PRM_VC_VAL_BYPASS = 0x003A5512` (VSEL `0x3A`), and
  `PRM_VC_VAL_CMD_VDD_MPU_L` (`0x4a307b98`) = `0x3A3A1400`.  The kernel's
  VDD1 SMPS ops return `-ENODEV` (VC-hardware-managed), so Linux cannot read
  the rail through the regulator framework; the VC command registers are the
  authority, and they show the 1.35 V OPP value.  Note: the VC's `reg = 0x55`
  is its own SRI2C addressing (`slave 0x12`), not the Linux I²C register map
  (PM_RECEIVER base `0x30` + `VREG_VOLTAGE_SMPS` 4 = `0x34`) — reading `0x55`
  over Linux I²C 0x48 returned `0x00` and is **not** the MPU voltage.
* The failure is a software hang at `execve`, not an abrupt reset.  Therefore
  neither a hard power failure nor an undervolted rail explains it.

## 6. Next

1. Boot `kernel-ipidbg-cpu1.img` until it wedges, capture the ring via the quiet
   read-back image and `tools/pb_ring.py decode --boot prev`; the last
   `PBSMF`/`PBIPI1` markers show whether the IPI is sent, received, or serviced.
2. Keep the fast chainloader (it fixes single-core reliability and the watchdog
   margin) and consider a dedicated startup script that raises the clock to
   1008 MHz only after userspace is up, if boot-time SMP proves fragile.
3. Then the MPU OPP proper: long-soak thermals and the reference's ABB/
   SmartReflex setup (report 219 §5).

## 7. Evidence

* `local/work/{r2-cpu1mark,r2-cpu1ok,r2-fastcl,r2-fastmark4-wedge}.{bin,words}`.
* `local/work/{dmesg-smp-nosmp-hd3-root,kmsg-nosmp}.txt`; flash logs
  `local/work/flash-*.log`.
* Modules `local/work/src/{pbopp,pbvc,pbi2c2}.{c,ko}`; chainloader template
  `boot/chainloader/chainloader_kernel.S.in` (+ `.session63-orig` backup).
* Traps 327–331; DECISIONS §91; report 256 is the companion first half.
