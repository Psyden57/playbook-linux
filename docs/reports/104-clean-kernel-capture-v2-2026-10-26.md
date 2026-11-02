# 104 — Clean 6.15.11: capture v2 and init progress (2026-10-26, session 2)

Status: bring-up continues. The old "1.33 s / gpiochip1" stop was a **capture
artifact**. The DRAM ring is now trustworthy, and the clean kernel reaches
**~1.51 s**, past GPIO2–6, i2c1–4, GPMC, PMU, IOMMU and the MMC host probe.
The current wall is the eMMC card init on MMC2: the host probe, power-up and
CMD0 init stream complete, but the rescan's MMC2 register accesses stall in a
timing-dependent way (see §2).

All source changes are in `clean-kernel-6.15.11-changes.patch` (now **20 files,
1247 lines**, dry-run applies cleanly to `pristine/`). The spare is restored:
TabletOS up, media `sha256 c72b0530…`, `BOOTCTR_FCTRY = 50`, door held.

---

## 0. TL;DR

- **Capture v2**: 32 KB ring at PA `0x88000000`, written through dedicated
  **non-cacheable** aliases `0xFE800000/0xFE900000/0xFEA00000`
  (wrap `0x7F00`, count/index at +0x80/+0x84). No cache maintenance at all.
  Reader: `analysis/pb_ring.py`.
- The earlier capture froze at ~4376 chars because the cacheable ring flush
  used non-secure PL310 maintenance, which **aborts after the L2C driver takes
  secure ownership**. That is what looked like a `gpiochip1` hang.
- New walls found and worked around, in order:
  1. crypto engines (`l4_secure_clkdm`) behind the L3 firewall — DES rev read
     gives `L3 Custom Error: MASTER MPU TARGET L4PER2 (Read)` + imprecise abort;
     all five crypto targets disabled.
  2. ABE `WD_TIMER3` (`0x40130000`) stalls in the ti-sysc enable/reset path;
     `&l4_abe` disabled.
  3. timing-dependent L4PER idle race between a ti-sysc target probe and its
     child driver; board-wide `SYSC_QUIRK_NO_IDLE_ON_INIT` applied in
     `sysc_probe()` for the PlayBook compatible.
  4. MCSPI4 child probe; all four MCSPI children disabled for now.
  5. **EMIF1/EMIF2** (`0x4c000000/0x4d000000`, the DRAM controllers) must not
     be reset/probed; both target modules disabled.
  6. DSS/GPU/IVA-HD: DSS `ti,no-reset-on-init`; SGX and IVA-HD disabled.
  7. **MMC2**: the host probe now completes (power-up, CMD0 init-stream,
     `mmc_add_host`). A stray init-stream completion IRQ used to spin forever
     in `omap_hsmmc_irq()` because `req_in_progress == false`; stray status is
     now acked. The remaining stall is `mmc_rescan` (card init).
- MMC probe guardrails: `CONFIG_MMC_BLOCK=n` for the probe runs,
  `CONFIG_MMC_RPMB` off, no initramfs, no userspace, no mounts/dd/mkfs.
  `CONFIG_MMC_BLOCK=y` was restored in the build tree afterwards.

---

## 1. Capture v2 (why the old capture lied)

Old scheme: ring VA = PA + 0x40000000 (write-back cached), flush ring1 only,
and only `+0x80..+0x4FF` every 64 chars by PL310 clean+invalidate by PA.

Three independent problems:

1. The newest bytes stayed dirty in L1 (PL310 clean-by-PA does not clean L1),
   and only the first 0x480 bytes were ever flushed. The capture was a mosaic
   of old evictions.
2. The decompressor writes ring2 (`0x90000000`) non-cached with a 1 KB wrap,
   while the kernel wrote all three rings cached with a 3.8 KB wrap — reading
   ring2 mixed two different windows.
3. After `l2c310_enable()` the PL310 is owned through secure SMC writes. A
   non-secure CIPA from the debug path then aborts inside printk. This is the
   abort storm that truncated the first v2 attempt at L2C init.

New scheme:

- 32 KB per ring: `+0x80` count, `+0x84` index (wrap `0x7F00`),
  chars `+0x100..+0x7FFF`.
- MMU-on debug writes use `0xFE800000 / 0xFE900000 / 0xFEA00000`
  (`PA 0x88000000 / 0x90000000 / 0x94000000`), mapped as
  `MT_MEMORY_RWX_NONCACHED` in `omap44xx_io_desc` and as normal non-cacheable
  sections in `head.S`. MMU-off uses the PAs.
- Ring pages are `no-map` in the DTS, so there is no cacheable linear alias.
- No PL310 access at all. A `dsb` every 8 chars drains the posted writes.
- `stdout-path` removed from `chosen`, so the 8250 driver no longer registers
  ttyS2 as a second console; earlycon is the only console and the ring is the
  only output.

Readback: `python3 analysis/pb_ring.py dump --ring 88000000 --out FILE`.
The ring page is stable for minutes after the OS boot (verified twice, byte
identical). It holds the last 32512 chars; `count`/`index` rotate it.

The kernel's own 64 KB printk ring (`__log_buf` at PA `0x81027ad0`,
descriptors at PA `0x80f523c8`, stride 12, lpos `0xFFFF0000+offset`) also
survives and reconstructs the boot from `Booting Linux` (the last records are
less reliable). Two readable files were left in
`playbook-live-spare-2026-10-26-clean/`:
`full-boot-2026-10-26.txt` (clean ring tail) and
`full-boot-from-printk-ring.txt` (early boot, 2-byte storage artifact trimmed).

---

## 2. Boot progress and each wall

| time | stop | cause / action |
|---|---|---|
| 1.33 s (old) | `gpiochip1` | **capture artifact** — ring froze at L2C init |
| 0.4 s (first v2) | L2C init | non-secure PL310 CIPA aborts after secure L2C init → non-cacheable ring |
| 1.42 s | DES `0x480a5030` | L3 firewall (l4_secure_clkdm) → disable aes1/aes2/sham/rng/des |
| 1.45 s | MCSPI1/4 | L4PER idle race → board-wide NO_IDLE_ON_INIT; later disable mcspi1–4 |
| 1.49 s | `0x40130000` ABE | WD_TIMER3 sysc enable → `&l4_abe` disabled |
| 1.49 s | EMIF2 `0x4d000000` | DRAM controller softreset poll → EMIF1/2 disabled |
| 1.50 s | DSS `0x58000000` / SGX | DSS init reset poll → `ti,no-reset-on-init`; SGX/IVA-HD disabled |
| 1.51 s | `mmc_power_up` | stray init-stream IRQ spin → ack stray STAT in `omap_hsmmc_irq` |
| 1.51 s | `mmc_rescan` | **current**: eMMC card init |

Latest image: `analysis/chainloader/kernel.img`, CRC `0x11807DDA`
(`CONFIG_MMC_BLOCK=n` build; last log line
`[1.510864] PBC: mmc1 rescan`).

### MMC2 detail

The host probe now gets through:

```
PBM: 480b4000.mmc probe → of_parse → fclk → pm_get → ctx_save → bus_power
     → rx/tx dma → reg_get → add_host
PBC: mmc1 device_add → start_host → power_up
PBSIO/PBCLK/PBINI: set_ios(pm=1, 0 Hz), set_ios(pm=2, 400 kHz),
     init_stream (CMD0, cc=1), set_bus_mode
PBC: power_up done → detect_change → start_host done → add_host done
PBC: mmc1 rescan → claimed → try 400000Hz → hw_reset → go_idle
PBC: mmc1 go_idle cs_high → (set_ios) → go_idle cs_high done
                               <-- stops here (1 ms mmc_delay)
```

The first `mmc_delay(1)` in `mmc_go_idle` is where one run stops. The MMC2 DT
node is now marked eMMC-only (`no-sdio; no-sd;`) so `sdio_reset`/SD probing is
skipped. A diagnostic `mdelay(1)` did **not** fix it: with the busy-wait the
stall moved *earlier*, into `send_init_stream`'s `STAT` poll (after the CMD
write, before `cc=1`). The MMC2 register accesses are timing-dependent and
move with code layout/timing, which points at the MMC2 module clock /
L4PER clock-domain (or the MMC child's runtime PM not keeping the ti-sysc
parent active), not at one instruction or the timer.

The MMC host itself is proven: clocks, power-up, CMD0 init stream, and
`mmc_add_host` all complete when the timing cooperates.

---

## 3. Current device / build state

| item | value |
|---|---|
| OS | TabletOS up, SSH works over the door |
| media region-0 IFS | `sha256 c72b0530…` (sealed, verified) |
| `BOOTCTR_FCTRY` | 50 (`0x32`) |
| build tree | `playbook-dev/kernel-clean`, `.config` restored to documented state (`CONFIG_MMC_BLOCK=y`, no RPMB) |
| latest image on disk | `chainloader/kernel.img`, CRC `0x51A36ED9` (matches the tree: MMC_BLOCK=y, `mmc_delay` restored) |
| patch | `analysis/clean-kernel-6.15.11-changes.patch`, 20 files / 1247 lines |
| reader | `analysis/pb_ring.py` |

The image on disk was rebuilt from the current tree after reverting the
`mdelay` experiment and restoring `CONFIG_MMC_BLOCK=y`, so it matches the
source/patch. It is **not** on media; the spare has the sealed IFS.

---

## 4. Next steps

1. **eMMC card init**: the remaining stalls are MMC2 register accesses that
   move with timing (`mmc_delay` after CS-high, `send_init_stream` STAT poll).
   Next experiments, in order:
   - add `ti,no-idle;` to the MMC2 target module (label `mmc2_target` in
     `omap4-l4.dtsi`) so the ti-sysc parent never gates it;
   - make the MMC child's runtime PM hold the parent (device link / parent
     resume), or keep the `l4_per_clkdm` denied during the rescan;
   - verify the MMC functional clock (`fck` from `l3_init_clkctrl`) is enabled
     and its rate is sane (400 kHz divisor vs the parent clock);
   - only then instrument CMD1/CMD2/CMD3/CMD9/CMD7/CMD6.
   Keep `CONFIG_MMC_BLOCK=n` and no userspace; the probe is read-only.
2. Then initcalls, display/DSS (needs the DSS power domain), and USB gadget.
3. Revisit the bring-up hacks: board-wide `SYSC_QUIRK_NO_IDLE_ON_INIT`,
   disabled crypto/ABE/SPI/EMIF/SGX/IVA, DSS `no-reset-on-init`. Each should
   become a proper power-domain/clkdm policy once the PM framework is up.
4. Remove the `PBG:/PBS:/PBM:/PBC:/PBSIO:/PBCLK:/PBINI:/PBR:/PBIRQ:` debug
   prints when bring-up is done.

---

## 4a. PMIC note: `twl6030_uv_to_vsel: OUT OF RANGE`

The repeated boot messages

```
twl6030_uv_to_vsel:OUT OF RANGE! non mapped vsel for 1375000 Vs max 1316660
twl6030_uv_to_vsel:OUT OF RANGE! non mapped vsel for 1410000 Vs max 1316660
```

come from `arch/arm/mach-omap2/omap_twl.c:twl6030_uv_to_vsel()` during OMAP4
VC/VP init. The OMAP4 VC/VP data asks for ON/ONLP = 1.375 V
(`vc44xx_data.c:OMAP4_ON_VOLTAGE_UV`) and VP `vddmax` = 1.41 V
(`voltage.h:OMAP4_VP_MPU_VLIMITTO_VDDMAX`). This TWL6030 reports
`SMPS_OFFSET = 0` (0.6–1.31666 V range), so everything above `VSEL 0x39`
hits the `pr_err`; the function clamps to `0x3A` and continues.

- **Not fatal**: boot continues; the value is clamped, not rejected.
- **Not the MMC2 wall**: these are VDD_MPU/IVA/CORE VC/VP rails, not the eMMC
  supplies (MMC2 has no `vmmc`/`vqmmc` in our DTS).
- **Will matter** before cpufreq/ondemand or SmartReflex is trusted at high
  OPP. The mainline OMAP4 VC/VP data assumes a 0.7–1.4 V SMPS range; this unit
  is the 0.6–1.3166 V variant.
- **Next**: read the PMIC `SMPS_OFFSET`/voltage table from QNX (RE the TWL
  driver or I²C), then either correct the table/range in `omap_twl.c` or lower
  the OMAP4 ON/VDDMAX constants for this board. Check whether VC/VP init
  actually lowers the bootloader CPU voltage to the clamped `0x3A` (≈1.329 V);
  if so that is a latent stability risk at 1 GHz.

---

## 5. Gotchas added this session

- Do **not** reintroduce PL310 cache maintenance in the debug path; after
  `l2c310_enable()` non-secure CIPA aborts.
- The ring is only 32 KB and wraps; the tail is the newest. Read it promptly;
  QNX has not churned ring1 in these runs, but it can.
- EMIF1/EMIF2 are the DRAM controllers: never reset/probe them from the
  non-secure kernel.
- Crypto engines are in `l4_secure_clkdm` and behind the L3 firewall; a
  non-secure revision read raises `L3 Custom Error ... L4PER2` and an
  imprecise abort (`0x1406`).
- The board-wide `NO_IDLE_ON_INIT` quirk is a bring-up workaround, not a
  fix; it keeps every ti-sysc target clocked for 3 s.
- The 8250 ttyS2 console must stay unregistered (no `stdout-path`) or the
  console interleaves/hangs on the dead UART.

---

## 6. Artifacts (this session)

`analysis/playbook-live-spare-2026-10-26-clean/`:
`nocrypto-ring.txt`, `noabe-ring.txt`, `noemif-ring.txt`, `nosgx-ring.txt`,
`pbr-ring.txt`, `mmc-*-ring.txt`, `full-boot-2026-10-26.txt`,
`full-boot-from-printk-ring.txt`.
