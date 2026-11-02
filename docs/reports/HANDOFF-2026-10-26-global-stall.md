# Handoff — 2026-10-26 session 3 (clean 6.15.11) → next session

> **SUPERSEDED — for current state read `HANDOFF-2026-10-27-session5.md`.**
> The "global stall" this file is about turned out to be the kernel's VFS
> panic (no root filesystem), not a hardware halt; see `109`.  Kept for the
> measurement history.

> **SESSION 4 (2026-10-27): there is no wall — see
> `109-epoch-test-2026-10-27.md`.**  With the console on, the boot ends in
> `Kernel panic - not syncing: VFS: Unable to mount root fs`; `panic()` disables
> IRQs, so the tick stops and the log ends like a frozen CPU.  `loglevel=0`
> hid it.  The 1.38–1.53 s numbers below are the WDT2 (7.81 s on this path).
> The secure world is fine (SMC probe returns instantly, `select_kek` → −1), the
> MPU domain never transitions, and a 1000 ms pre-kernel delay moved the halt by
> 0 ms.  Next step is an initramfs, not more RE.


**Read `105-global-1.5s-stall-2026-10-26.md` first** — full detail, the
instrumentation, the QNX IFS findings and the experiment matrix.  This file
is the short entry point.

## 0. TL;DR

- **The MMC2 driver is not the wall.** With `loglevel=0` the eMMC card init
  actually runs: CMD0, CMD1 ×3 with completion IRQs, power-off, rescan
  completes, MMC runtime autosuspend **and** resume complete cleanly.
- **There is a global CPU halt at ~1.40–1.53 s.** A 30 s busy-spin
  (`cpu_relax`, IRQs on, no device I/O) dies ~60 ms in; the periodic tick
  (TWD IRQ 29 / GPT IRQ 37) stops with it.  The always-on WDT2 resets the
  SoC ~55 s later.
- Ruled out: printk/console, cpuidle/NO_HZ, SmartReflex/AVS, MMC2
  clocks/target idle (readback ideal), the RPM callback path, the TWL6030
  watchdog reload.
- **Next:** RE the QNX pre-Linux setup — `setup-core-inactive` (PRCM/VC
  bypass 0x3A/0x40, core 1225/1250 mV), `trustzone-omap4`, and CPU1
  (`setup-core-inactive full` is QNX's first action).
- Spare is clean: TabletOS up, sealed IFS `sha256 c72b0530…`,
  `BOOTCTR_FCTRY = 50`, door held.  Latest image on disk
  `analysis/chainloader/kernel.img` CRC `0x0BEB602A` (not on media; the
  tree is back to `CONFIG_MMC_BLOCK=y`, documented bootargs, spin test
  removed).

## 1. New tooling

- `include/linux/pb_event.h` — printk-free event log in ring1
  `+0x00..+0x7C` via the non-cacheable alias `0xFE800000`:
  heartbeat `+0x00` (`0x3700<<16 | jiffies`), magic `+0x08`, counter
  `+0x0C`, 28-slot ring at `+0x10` (word = `code16<<16 | jiffies&0xffff`).
  Raw accesses (no barrier).  Initialised in `gic_of_init()`.
- `analysis/pb_events.py` — reads/decodes it over the door.
  `jiffies` starts at `INITIAL_JIFFIES` (`-300*HZ`); the decoder subtracts
  `0x8AD0`.
- Hooks: GIC IRQ enter/done, initcalls (+ last fn pointer in `+0x04`), MMC
  request/IRQ/set_ios/init_stream/rescan/attach/power_off, RPM
  callback/suspend, ti-sysc suspend/resume, prepare_namespace,
  wait_for_device_probe, async_synchronize_full.

## 2. MMC2 changes kept

- DTS: `&mmc2_target { ti,no-idle; ti,sysc-sidle = <SYSC_IDLE_NO>;
  ti,sysc-midle = <SYSC_IDLE_NO>; };`
- `omap_hsmmc_probe()`: `device_link_add(child, parent,
  DL_FLAG_PM_RUNTIME | DL_FLAG_AUTOREMOVE_CONSUMER)`.
- Readback at probe: `fck=96MHz pclk=96MHz mm=2 idlest=0 stby=0 sel=1
  clkst=2`, `sysc=0x14` (midle/sidle = NO).  The target is fully awake.

## 3. The loop (unchanged)

```bash
/home/psyden/rea-work/door-hold.sh
/tmp/pbssh.sh 'echo OK'

C=/home/psyden/rea-work/playbook-dev/kernel-clean
export PATH=/home/psyden/SSDdrive/wsl-migration/home/psyden/toolchains/armv7-eabihf/bin:$PATH
make ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
ZIMAGE=$C/arch/arm/boot/zImage \
DTB=$C/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb \
python3 build_kernel_image.py

python3 pre_os_ifs_flash.py --template-ifs chainloader/kernel.img &
/tmp/pbssh.sh '/proc/boot/shutdown -S reboot'
# wait for the flash tool to exit
python3 playbook-live-spare-2026-10-26-clean/force_reset_loader.py   # boots the test kernel
python3 pre_os_ifs_flash.py --template-ifs playbook-live-spare-2026-10-25/ifs0.sealed.bin --delay 30 &
# WDT2 resets at ~55 s; the delayed restore catches the bootrom
timeout 40 python3 playbook-live-spare-2026-10-26-clean/force_reset_loader.py   # boots TabletOS
# poll the door, then:
python3 pb_ring.py dump --ring 88000000 --out playbook-live-spare-2026-10-26-clean/latest.txt
python3 pb_events.py
```

`force_reset_loader.py` must be run **once after each phase** (0x80EF first);
the flash tool's own `reboot_loader()` often leaves the unit enumerated at
`0fca:8001`.

## 4. Next steps (MMC is fine — do not chase it)

The three RE reports are written (see `NEXT-SESSION-BOOTSTRAP-2026-10-26.md`
§7 for the condensed results):
`106-re-setup-core-inactive.md`, `107-re-trustzone-omap4.md`,
`108-re-wdtkick-pmic.md`.

1. **Run the epoch test first** (report 108 §6.3): insert a fixed delay D in
   the chainloader/decompressor before any kernel timer, boot at D=0 twice
   (jitter) and at D≈3 s, and compare the halt time in kernel time.  This
   decides "hardware epoch" vs "kernel entry" for the ~1.4 s wall and costs
   two cycles.
2. If **hardware epoch**: focus on always-on/secure timers.  `WDTIMER1`
   (`0x4A318000`) is the best candidate flagged by report 107 — note it is
   also Linux's always-on clockevent; `WDTIMER3` (`0x40130000`) is in the
   disabled ABE.  Try stopping/kicking/reading them (WDT2 and the TWL PMIC
   watchdog are cleared: report 108).
3. If **kernel entry**: replicate the `setup-core-inactive` PRCM/VC/VP state
   (report 106: PRM_VC_VAL_BYPASS `0x4A307BA0` writes `0x013A5612` /
   `0x01405512`, `PM_MPU_PWRSTST`, `CM_SDMA_STATICDEP`, CLKSTCTRL) before the
   wall, and/or check the MPU power domain / clkdm state.
4. **CPU1** — QNX's first action is `setup-core-inactive full`; Linux
   leaves CPU1 parked.  Try forcing GIC SPIs to CPU0 and/or a safe CPU1 park.
5. The TWL6030 watchdog is a real latent bug even though it is not the wall:
   kick `0x48/0x2C=0x7F` every 30 s and WDT2 `TGR` every 15 s (report 108
   §6.2).
6. Then continue: card init (CMD1 already issued; the card does not answer),
   initcalls, display/DSS, USB.

## 5. Gotchas

- The console is expensive and shifts what runs at the ~1.4 s wall;
  `loglevel=0` gets much further.
- `drivers/base/dd.c:619` prints `PBR: dev <- driver` **before** the probe;
  a PBR line with no driver follow-up means the probe hung.
- Printk ring (`__log_buf`) is cache-stale after a wedge; only the DRAM ring
  and the event log are authoritative.
- Do not reintroduce PL310 maintenance; the ring must stay non-cacheable.
- `audio`/EMIF/crypto/ABE/SPI/SGX/IVA stay disabled (session 2 workarounds).
- `CONFIG_MMC_BLOCK=y` is restored; use `=n` for probe builds.
