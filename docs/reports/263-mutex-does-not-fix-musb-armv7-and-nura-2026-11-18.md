# 263 — serializing `omap_musb_set_mailbox` does not fix the dual-core fault; ARMv7 build; Nura root on hd3

Date: 2026-11-18 (session 65, Linux-era 59).  Bench.

## 0. Headline

* **The session-64 "two callers race" hypothesis is insufficient.**  Restoring
  the stock `omap_musb_set_mailbox()` call in `omap2430_musb_enable()` **and**
  serializing both callers with a glue mutex still panics on dual-core.  The
  device-mode-only workaround (`kernel-usbdev-cpu1.img`, `0xC675E206`) remains
  the working path; it is not just a race fix.
* **The kernel was being built `-march=armv6`.**  `CONFIG_ARCH_MULTI_V6=y`
  selected `CPU_ARM1176 → CPU_V6K → CPU_32v6`, and
  `arch/arm/Makefile`'s `ifeq ($(CONFIG_CPU_32v6),y)` branch makes the v6K
  setting override the v7 one.  Disabling `ARCH_MULTI_V6` (this unit is OMAP4430
  only) gives `-march=armv7-a`, `__LINUX_ARM_ARCH__=7`, module vermagic
  `ARMv7 p2v8`.  Image `kernel-armv7-cpu1.img`, CRC **`0x158D8A85`**, `#105 SMP`,
  **6/6 clean dual-core boots**.
* **hd3 now carries the Nura root again.**  The BusyBox ext4 that had been
  flashed there for the session-63 mount test was damaged by power cycles
  (ext4 "Data will be lost", corrupt inode bitmap).  The baked Nura image
  `07555bde…` was written at block `0x4915` (626,065,440 B, `FLASH_OK`), grew
  to 15,273,984 blocks (62.5 GB) on first boot, and runs `catacomb` on
  `pvrsrvkm_omap4_sgx540_120` after the ARMv7 modules were installed.
* **The ~30 s USB teardown is not BusyBox-specific.**  On the Nura root VUSB
  stayed enabled at 82 s and the link survived to ~90 s with continuous SSH
  activity; the busybox root dies at ~32 s.  Activity postpones it, it does not
  prevent it.  The cause is still unproven (the TWL6030 comparator's
  `regulator_disable(usb3v3)` path is the only writer, `phy-twl6030-usb.c`).

## 1. The mutex experiment

Change (`local/kernel-tree/drivers/usb/musb/omap2430.c`, gitignored tree):
add `struct mutex set_mailbox_lock` to `omap2430_glue`, `mutex_init()` in probe,
lock at the top of `omap_musb_set_mailbox()` and unlock after the
`atomic_notifier_call_chain()`; remove the winchester early return in
`omap2430_musb_enable()` so the stock call runs.  Both callers are sleepable
process context (the VBUS/ID work item and PID 1's configfs UDC write; the TWL
IRQ itself only sets `glue->status` and schedules the work), and the function
already sleeps in `pm_runtime_get_sync()`.

| build | CRC | outcome |
|---|---|---|
| mutex, stock call, DTB still `maxcpus=1` | `0xBEF06B78` | boots `#104 SMP`, `online=0` — **cannot test the mailbox single-core** |
| mutex, stock call, `maxcpus=1` removed | `0x19E24D8F` | dual-core boot **panics in `omap_musb_set_mailbox`**; `panic=10` reboots, ~6 cycles |

The panic loop drained `BOOTCTR_FCTRY` `0x22 → 0x1c` (measured across the
cycles), which is exactly the failure mode the top-up threshold exists for.
Recovery: reflash `kernel-usbdev-cpu1.img` (`0xC675E206`); its `/init` read
`0x1c` and wrote `0x32` (`pbrpmb: topup: writing 0x1c -> 0x32 (RPMB counter
12463)`, `PASS`).  No manual RPMB write.

**Conclusion.**  The mutex removes one race, but the fault survives it, so the
bad indirect transfer has another cause — most likely corruption of the
`usb_phy`/`usb_otg` object (the session-64 observation that `otg->set_vbus`
once held a slab address `0xc2b1b800` fits that better than a state-machine
race).  The next experiment is an entry marker in `omap_musb_set_mailbox()`
printing CPU, `glue`, `musb`, `xceiv`, `otg`, `otg->set_vbus` and
`xceiv->notifier.head` through `printascii()`/the ring, booted **with
`panic=0`** so a fault does not loop.

Also reverted: the session-64 `func_ptr_is_kernel_text()` guard in
`include/linux/usb/otg.h`.  It cannot build as a module dependency
(`dwc3.ko: symbol 'func_ptr_is_kernel_text' undefined`, the symbol is not
exported), and it was already measured not to stop the Oops.  The observation
it encoded (a slab address in `set_vbus`) is kept as evidence.

## 2. ARMv6 → ARMv7

Mechanism:

* `.config` had `CONFIG_ARCH_MULTI_V6=y`, which selects `CPU_ARM1176`, which
  selects `CPU_V6K`, which selects `CPU_32v6`.
* `arch/arm/Makefile`:
  `arch-$(CONFIG_CPU_32v7) := -march=armv7-a` is followed by
  `ifeq ($(CONFIG_CPU_32v6),y) arch-$(CONFIG_CPU_32v6K) := -march=armv6k endif`
  (same for `-D__LINUX_ARM_ARCH__`), so the v6K assignment wins whenever
  ARMv6 SoC support is present.
* `CPU_V7` also selects `CPU_32v6K` (the v6K extensions are part of ARMv7);
  only `CPU_32v6` gates the ISA, so disabling `ARCH_MULTI_V6` is enough.

Measured after `scripts/config --disable ARCH_MULTI_V6` + `olddefconfig`:

* `CPU_ARM1176`, `CPU_V6K`, `CPU_32v6` gone; `CPU_V7`, `CPU_32v6K`,
  `CPU_32v7` remain.
* compile flags `-march=armv7-a`, `-D__LINUX_ARM_ARCH__=7`.
* module vermagic `7.3.3 SMP mod_unload modversions ARMv7 p2v8` (was `ARMv6`).

Image `kernel-armv7-cpu1.img` (`0x158D8A85`, 10,449,064 B, `#105 SMP`,
`bootargs` without `maxcpus`): **6/6 clean dual-core boots** (the power-on boot
plus five `reboot -f` cycles), `online=0-1`, gadget up, `pbrpmb` OK
(`0x31 ≥ 0x1e`, no write), no `Unable to handle`/panic.  This keeps the
session-64 device-mode-only mailbox workaround.  It was then re-wrapped with the
inert `console=ttyGS0,115200` bootarg as `kernel-armv7-console-cpu1.img`
(`0xC9FEE2D1`), which is what is on media and what the §4a soak ran on.

Both tracked artefacts were updated: the board DTS
(`local/kernel-tree/.../omap4-winchester.dts` and
`kernel/source/omap4-winchester.dts`) no longer carries `maxcpus=1`, and
`kernel/winchester-7.3.3.config` now matches the working tree config
(`CONFIG_SMP=y`, `CONFIG_NR_CPUS=2`, no `ARCH_MULTI_V6`).

## 3. hd3: the BusyBox ext4 was damaged, so Nura was restored

The session-63/64 bench root was the region-1 BusyBox ext4 flashed to hd3 and
mounted read-write with `barrier=0` + write-through.  After several power
cycles (needed because the USB link dies at ~30 s) the next boot reported:

```
EXT4-fs (loop0): This should not happen!! Data will be lost
EXT4-fs error (device loop0): ext4_lookup: … deleted inode referenced: 4098
EXT4-fs error (device loop0): ext4_validate_inode_bitmap: … Corrupt inode bitmap
```

A `/var/log/pm-trace.log` written by a periodic `dmesg -c` tracer was lost with
it.  The image was then replaced:

* **Flashed** `local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img`
  (sha256 `07555bde…`, 625,999,872 B) to hd3 at loader block **`0x4915`**
  (626,065,440 B written, `COMPLETE 0x40C0 -> cmd 0x4006`, `FLASH_OK`).
* First boot: the initramfs mounted the ext4 rw, `resize2fs` grew it
  `152832 → 15273984` blocks (**62.5 GB**), `lstat` handoff to
  `/sbin/init`, `ttyGS0` root shell, dropbear SSH (crng init at 13.1 s, no
  entropy seed needed).
* **Modules:** the image's were pre-SMP/pre-ARMv7.  Installed the tree's set
  (`29 MB` installed, `8.4 MB` tarball) over SSH into `/lib/modules/7.3.3`,
  `depmod -a 7.3.3` OK; after a reboot the
  `.gnu.linkonce.this_module section size must match` errors are **0**, and
  `pvrsrvkm 56000000.gpu: Enabling quirks 00000001`,
  `pvrsrvkm_omap4_sgx540_120` (used by 3), `catacomb` running, `card0-LVDS-1`
  `connected`, backlight 255, `online=0-1`.

Boot-log review (full 707-line dump in `local/work/dmesg-nura-1924.txt`):

* `mmc1: Card stuck being busy! __mmc_poll_for_busy` at 27.09 s followed by a
  lost asynchronous **WRITE** on `mmcblk1` — the known eMMC stall (report 250
  §1), hit during the first-boot `resize2fs` burst.  The resize completed; the
  filesystem survived.
* `ti-sysc 4a318000.target-module: probe failed with error -16` — that node is
  **timer1**; `-EBUSY` is benign.
* `twl6030_uv_to_vsel: OUT OF RANGE` ×7 (1.375 V / 1.41 V requested, table max
  1.31666 V) — the OPP/VC path; no `cpufreq` policy exists (report 257 §6 is
  still the MPU-OPP item).
* `PLAYBOOK WDT: crr fffdd4fa->fffdd4fa (KICK FAILED)` at t=0 is before
  `WLDR` is programmed; `PLAYBOOK WDT: ldr fffc1800->ffe2b400 (58.6 s window),
  pet 10s` follows and uptime passes 58.6 s.
* `hw-breakpoint: Failed to enable monitor mode on CPU 0` — DBGDSCR.IMOD is
  secure-world blocked; harmless.
* `Warning: unable to open an initial console` — `ttyGS0` is not registered
  when `kernel_init` looks; `/init` opens it directly.
* The DSI-init TCON hook works: `DSI: PBTCON: Hitachi table sent from DSI init
  path` at 1.48 s, so the panel has text before `/init`.

## 4. The USB teardown, measured again

* BusyBox root, host activity every 15 s: `ttyACM0` read returned `EIO` at
  **~30 s** after kernel start; the unit kept running.
* Nura root, `VUSB` read **enabled** at 82 s; with SSH commands flowing the
  link survived to **~90 s** and then dropped (`0fca:8010` gone, no
  re-enumeration).
* Both roots log `audio-ldo: disabling` at ~31.8 s; that is a separate
  regulator and does not coincide with the VUSB event on Nura.

Only `twl6030_usb_irq()` calls `regulator_disable(twl->usb3v3)` (the
`VUSB: disabling` line), and only when it reads `CONTROLLER_STAT1.VBUS_DET`
clear.  Whether that is a spurious TWL read, or something the booted userspace
does to the charger block, is not established.

### 4a. A 6-minute soak that did not drop

`kernel-armv7-console-cpu1.img` (`0xC9FEE2D1`) adds an inert
`console=ttyGS0,115200` to the bootargs (the gadget serial driver has no
kernel-console write path, so printk never reaches the ACM — verified: no
`console [ttyGS0] enabled` line and no kernel text in the capture).

With the ACM **open and read continuously** (a userspace `cat /dev/kmsg`
streamed to the host), the unit ran **240 s without a disconnect** and captured
records 0–722 of the boot log; a further **90 s with the port closed and no
traffic** also stayed up; at the end of the test:

```
uptime 376.24 s   online 0-1   no panic
pbrpmb: topup: 0x26 >= threshold 0x1e, no write
0fca:8010 still enumerated
```

So the teardown is **intermittent**, not an idle timer: it did not occur in
>6 minutes on this boot, with or without host traffic.  The earlier drops
(30 s / 90 s) remain unexplained but are not a property of idleness alone.
The capture also showed:

* `twl6030_charger 48070000.i2c:twl@48:charger: battery with vmax -22 imax:
  -22` — the TWL6030 charger module does probe, with invalid values (the board
  charges through the ISL9519).
* `thermal thermal_zone2: Temperature check failed (-19)` then
  `Unable to get temperature, disabling!` at 228.7 s.
* The eMMC stall repeated (`mmc1: Card stuck being busy`, lost async WRITE)
  at 33.7 s with flags `0x800000`.

## 5. Patch re-export

The tracked patch was still the session-58 export (64 files / 8,981 lines,
sha256 `ca17a057…`): it did not touch `omap2430.c`, had no `PBTCON` in `dsi.c`,
and its board-DTS hunk still set `maxcpus=1`, so it no longer reproduced the
kernel in `local/kernel-tree` after sessions 62–65.

Re-exported in session 65: **65 files / 9,110 lines, sha256 `6f9bef24…`**,
verified by applying it to pristine 7.3.3 targets — **0 rejects, 65/65
byte-identical**.  Contents added/changed over session 58:

* `drivers/usb/musb/omap2430.c` (new, +23): the winchester device-mode-only
  enable path and the `set_mailbox_lock` mutex.
* `drivers/gpu/drm/omapdrm/dss/dsi.c`: the DSI-init TCON hook.
* `arch/arm/boot/dts/ti/omap/omap4-winchester.dts`: `maxcpus=1` removed; inert
  `console=ttyGS0,115200` added.

Deliberately **not** exported (diagnostics only): the `PBDBG` markers in
`init/main.c` and the `head.S` `CRR` stamp (session-58 content is used for both
files), the `PBCPU`/`PBIPI1` markers in `arch/arm/kernel/smp.c` and
`arch/arm/mach-omap2/omap-smp.c`, the `PBSMF` markers in `kernel/smp.c`, the
`MINI4x6` font link in `lib/fonts/Makefile`, and the reverted `otg.h` include.
The vendored `drivers/gpu/drm/pvrsgx/` tree remains a tarball, not patch content.

## 6. Evidence

* Boot/soak logs: `local/work/soak65-20261118-183036.log` (6/6 on
  `0xC675E206`), `local/work/armv7-boots.log` (6/6 on `0x158D8A85`), the tracked
  captures `docs/captures/playbook-live-2026-11-18-nura-armv7-boot.txt`
  (707 lines) and `docs/captures/playbook-live-2026-11-18-nura-kmsg-stream.txt`
  (72,992 bytes, records 0–722, 240 s; working copies in `local/work/`).
* Flash logs: `local/work/flash-mutex-*.log`, `flash-mutex2-*.log`,
  `flash-recover-190153.log` (top-up `0x1c -> 0x32`), `flash-armv7-*.log`,
  `flash-nura-*.log`.
* Images (gitignored): `kernel-usbdev-cpu1.img` (`0xC675E206`, working),
  `kernel-armv7-cpu1.img` (`0x158D8A85`, current region 0),
  `kernel-mutexmailbox-cpu1.img` (`0x19E24D8F`, rejected),
  `nura/images/blackberry-playbook-root-osk-wvkbd.img` (`07555bde…`, on hd3).
* Module staging: `local/work/modstage/lib/modules/7.3.3`,
  `local/work/modules-7.3.3-armv7.tar.gz`.
* Kernel-tree experiments (gitignored): `omap2430.c` mutex + early return,
  `otg.h` guard reverted, DTS `maxcpus` removed (tracked copy updated),
  session-64 DSI TCON hook still present.
