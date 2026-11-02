# Handoff — 2026-10-26 (clean 6.15.11 on the CRC path) → next session

**Read `103-clean-kernel-crc-bringup-2026-10-26.md` first** — it has the full detail,
every command, the six root causes, and the source patch. This file is the short
entry point. It supersedes `HANDOFF-2026-10-26-next-session.md` for the kernel
work (the loader/CRC/region material in `94`–`102` still stands).

## 0. TL;DR

- We abandoned the kexec kernel. A **clean `linux-6.15.11`** was built from
  `pristine` + only the PlayBook board/debug pieces (no kexec memory hacks).
- It **boots through the CRC/chainloader path** and runs ~**1.33 s** of driver
  init (GPIO, serial, pinctrl, DMA, smartreflex, PMIC, i2c, net, mtd…), then
  **hangs at/after the `gpiochip1` probe**.
- Last real error:
  `ti-sysc 4a318000.target-module: probe with driver ti-sysc failed with error -16`
  (`-EBUSY`, timer1). The next GPIO banks are in the PER power domain — the
  likely hang (PRCM clock/power access), the known wall from the kexec notes.
- **The iteration loop is automated and reliable** (see §2). No manual
  power-cycle is needed except after a hard wedge.

## 1. Current state of the spare

| item | value |
|---|---|
| OS | TabletOS up, `0fca:8010`, ping `169.254.0.1` |
| media region-0 IFS | `sha256 c72b0530…` = `ifs0.sealed.bin` (restored) |
| `BOOTCTR_FCTRY` | `50` (`0x32`), verified |
| qconn door | `door-hold.sh` supervisor running |
| clean kernel image | `analysis/chainloader/kernel.img`, CRC `0x1C324081` (early-console build) |
| clean build tree | `/home/psyden/rea-work/playbook-dev/kernel-clean` |
| source delta | `analysis/clean-kernel-6.15.11-changes.patch` |

## 2. The loop (exact commands, no guessing)

```bash
# 0. door (background; reconnects across reboots)
/home/psyden/rea-work/door-hold.sh

# 1. SSH wrapper
/tmp/pbssh.sh 'echo OK'     # see 103 §3.1 for the full ssh wrapper

# 2. build
C=/home/psyden/rea-work/playbook-dev/kernel-clean
export PATH=/home/psyden/SSDdrive/wsl-migration/home/psyden/toolchains/armv7-eabihf/bin:$PATH
make ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb

# 3. image
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
ZIMAGE=$C/arch/arm/boot/zImage \
DTB=$C/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb \
python3 build_kernel_image.py
python3 89-crc_tool.py verify chainloader/kernel.img --image-off 0   # ECDSA FAIL is informational

# 4. flash + open the bootrom window
python3 pre_os_ifs_flash.py --template-ifs chainloader/kernel.img &
/tmp/pbssh.sh '/proc/boot/shutdown -S reboot'

# 5. after the flash, force the loader to reset
python3 playbook-live-spare-2026-10-26-clean/force_reset_loader.py

# 6. the kernel runs and the WDT2 self-resets it; restore the OS while catching
#    the WDT bootrom (delay 40 skips the first bootrom)
python3 pre_os_ifs_flash.py \
  --template-ifs playbook-live-spare-2026-10-25/ifs0.sealed.bin --delay 40
python3 playbook-live-spare-2026-10-26-clean/force_reset_loader.py
# wait ~2 min; the door auto-reconnects

# 7. read the ring
/tmp/pbssh.sh 'on -C 0 /accounts/devuser/memdump3 90000080 0x8'
/tmp/pbssh.sh 'on -C 0 /accounts/devuser/memdump3 90000100 0xF00' > ring.txt
# index at +0x84; text = raw[idx:0xF00] + raw[:idx]

# 8. counter
/tmp/pbssh.sh 'dd if=/dev/rpmb/BOOTCTR_FCTRY bs=1 count=4 2>/dev/null' | od -An -tu4
# startup.sh auto-tops to 0x32 with a verified readback on every OS boot
```

## 3. What is proven

- Clean kernel builds (gcc 14.3.0), CRC-seals, flashes, boots via the chainloader.
- The gzip decompressor returns 0; `stext` and `earlyprintk` work.
- The DRAM ring capture works; the chainloader clears it each boot.
- The bootloader-armed WDT2 self-resets a hung kernel
  (`ti,no-idle`/`ti,no-reset-on-init` + `clk_ignore_unused`), so the recovery
  tool catches the bootrom and DRAM survives.
- The qconn door auto-reconnects (keepalive + `TCP_USER_TIMEOUT` + peek).
- The counter top-up verifies its own write and retries.

## 4. Open / next

1. **Capture cleanly:** the ring is 3840 chars and wraps (~12.5 K chars/boot); the
   stream interleaves from concurrent printk. Enlarge the ring (change `0xF00` in
   `omap4bc.S` and reserve more DRAM), or reduce console volume, to make the last
   probe unambiguous.
2. **Isolate the 1.33 s hang:** disable `CONFIG_GPIO_OMAP`/GPIO2–6 and see if the
   boot advances; then fix the PER-domain/PRCM access (runtime PM, `omap4_prminst`
   SMC path, `ti,no-idle` for the modules that hang).
3. Continue: i2c/PMIC regulators → MMC/eMMC → display/DSS → USB gadget.
4. Long term: one-shot/restore + eMMC ring (`97`/`99`).

## 5. Gotchas that bit us (full list in 103 §10)

- `CMDLINE_FORCE` was overriding the DTB (so `clk_ignore_unused` and the
  bootargs were ignored; `rootwait` can hang). It is now off — keep it off.
- The image builder's IFS trailer must reserve 0x230 bytes **after** the DTB, or
  the DTB is corrupted.
- `0xEF` alone doesn't reset the loader; use `0x80EF` (helper) or a power-cycle.
- Power-button hold+press usually preserves DRAM; battery pull wipes it.
- The OS may churn the ring pages; read soon after boot.
- QNX has no `od`/`hexdump`/`sha256sum`; pipe raw `dd` output to the host.
- `--key/--pub` go **after** `hold` in BerryShell-V4.
- The bootrom window is ~13 s; the flash tool must be polling first.

## 6. Suggested prompt for the next session

> Read `bootdumps-2026-09-11/analysis/103-clean-kernel-crc-bringup-2026-10-26.md`
> and `HANDOFF-2026-10-26-clean-kernel.md`. The clean 6.15.11 kernel now boots via
> the CRC/chainloader path and reaches ~1.33 s of driver init, then hangs at/after
> `gpiochip1` (`ti-sysc 4a318000 -EBUSY`). The loop is automated: chainloader
> clears the ring, WDT2 self-resets (`ti,no-idle` + `clk_ignore_unused`), the
> recovery tool catches the bootrom with `--delay 40`, `door-hold.sh` keeps the
> qconn door up, and `/accounts/devuser/startup.sh` tops `BOOTCTR_FCTRY` to 50 with
> a verified readback. Spare: TabletOS, media sealed (`c72b0530…`), counter 50.
> Next: make the ring capture unambiguous, isolate the GPIO2+/PER-domain/PRCM
> hang, then continue init. Keep all writes and the live device in the main
> session; use subagents for read-only tracks.
