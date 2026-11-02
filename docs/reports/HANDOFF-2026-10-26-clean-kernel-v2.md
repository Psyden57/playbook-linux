# Handoff — 2026-10-26 session 2 (clean 6.15.11, capture v2) → next session

**Read `104-clean-kernel-capture-v2-2026-10-26.md` first** — full detail, every
wall, the source changes and artifacts. This file is the short entry point and
supersedes `HANDOFF-2026-10-26-clean-kernel.md`.

## 0. TL;DR

- **The old "1.33 s / gpiochip1" stop was a capture artifact.** The DRAM ring is
  fixed (32 KB, non-cacheable, `analysis/pb_ring.py`) and the clean kernel now
  reaches **~1.51 s**, past GPIO2–6, i2c1–4, GPMC, PMU, IOMMU and the MMC2
  host probe.
- **Current wall: eMMC card init on MMC2.** The host probe, power-up, CMD0
  init-stream and `mmc_add_host` all complete. The rescan then hits MMC2
  register accesses that stall **timing-dependently** (one run in
  `mmc_go_idle`'s 1 ms delay, a busy-wait variant moved it into
  `send_init_stream`'s STAT poll).
- **Everything is in the refreshed patch**: `clean-kernel-6.15.11-changes.patch`,
  **20 files / 1247 lines**, dry-run applies cleanly to `pristine/`.
- **Spare is clean**: TabletOS up, media `sha256 c72b0530…`, `BOOTCTR_FCTRY=50`,
  sealed IFS restored, door held.
- Latest image on disk: `analysis/chainloader/kernel.img`, CRC `0x51A36ED9`
  (matches the tree: `CONFIG_MMC_BLOCK=y`, `mmc_delay` restored). Not on media.

## 1. What changed this session

- **Capture v2**: 32 KB ring (count +0x80, index +0x84, wrap 0x7F00) through
  dedicated **non-cacheable** aliases `0xFE800000/0xFE900000/0xFEA00000`
  (`omap44xx_io_desc` + head.S), ring pages `no-map` in the DTS. **No PL310
  maintenance** — the old cacheable ring + CIPA aborts after the L2C driver
  takes secure ownership.
- Removed `stdout-path` (single console; ttyS2 was a second writer).
- Crypto engines (`l4_secure_clkdm`) disabled — DES rev read gives an L3
  firewall error + imprecise abort.
- `&l4_abe` disabled (WD_TIMER3 sysc hang).
- Board-wide `SYSC_QUIRK_NO_IDLE_ON_INIT` in `sysc_probe()` (L4PER idle race).
- MCSPI1–4 disabled; EMIF1/EMIF2 disabled (DRAM controllers, never reset);
  DSS `ti,no-reset-on-init`; SGX/IVA-HD disabled.
- MMC2: `no-sdio; no-sd;`; stray init-stream IRQ acked in `omap_hsmmc_irq()`;
  extensive `PBM/PBC/PBSIO/PBCLK/PBINI/PBREQ/PBIRQ` checkpoints.
- PMIC note: `twl6030_uv_to_vsel: OUT OF RANGE` is real but clamped/non-fatal
  (OMAP4 VC/VP asks 1.375/1.41 V; this TWL6030 SMPS_OFFSET=0 maxes at
  1.31666 V). See report §4a. Not the MMC wall; fix before DVFS/SmartReflex.

## 2. The loop (unchanged)

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
python3 playbook-live-spare-2026-10-26-clean/force_reset_loader.py

python3 pre_os_ifs_flash.py \
  --template-ifs playbook-live-spare-2026-10-25/ifs0.sealed.bin --delay 40
python3 playbook-live-spare-2026-10-26-clean/force_reset_loader.py
# wait ~2 min; the door reconnects

python3 pb_ring.py dump --ring 88000000 --out playbook-live-spare-2026-10-26-clean/latest.txt
```

## 3. Next steps (MMC first)

1. The MMC2 stalls are timing-dependent register accesses, so treat it as a
   clock/power problem, not a command bug:
   - add `ti,no-idle;` to the MMC2 target module (add a `mmc2_target:` label in
     `omap4-l4.dtsi`) so the ti-sysc parent never gates;
   - or keep `l4_per_clkdm` denied during the rescan, and/or make the MMC
     child runtime-PM hold the parent (device link);
   - verify the functional `fck` (`l3_init_clkctrl`) is enabled and its rate
     is sane for the 400 kHz divisor.
2. Only then instrument CMD1/CMD2/CMD3/CMD9/CMD7/CMD6 and read the eMMC.
   Guardrails: `CONFIG_MMC_BLOCK=n` for probe builds (restore `=y` after),
   `CONFIG_MMC_RPMB` off, no initramfs, no userspace, no mounts/dd/mkfs.
3. Then initcalls, display/DSS (needs the DSS power domain), USB gadget.
4. **RE the QNX/TabletOS drivers** where mainline has no equivalent: TWL6030
   PMIC voltage tables/rail mapping, USB PHY/gadget, display/DSS panel and
   backlight, eMMC pad/clock setup. The QNX IFS is on the 2.0.0.4869 media and
   the `deckard`/`bb10mt` tools are in the workspace.
5. Remove the debug prints when bring-up is done (`PBG/PBS/PBM/PBC/PBSIO/
   PBCLK/PBINI/PBREQ/PBIRQ`), and revisit the board-wide hacks.

## 4. Gotchas

- Do not reintroduce PL310 cache maintenance in the debug path.
- Ring is 32 KB and wraps; the tail is newest. QNX has not churned ring1 in
  these runs, but read promptly.
- EMIF1/2 are the DRAM controllers: never reset/probe them.
- Crypto engines are behind the L3 firewall (`l4_secure_clkdm`).
- The board-wide `NO_IDLE_ON_INIT` is a workaround, not a fix.
- Keep `stdout-path` out and the 8250 ttyS2 console unregistered.
