# WL1283 instrumentation — archived (session 37)

This directory holds the WL1283 (TI WiLink 8 combo) bring-up code that was
**removed from the active kernel tree and from
`kernel/patches/0001-playbook-winchester-7.3.3.patch`** at the operator's
direction (session 36 end; `DECISIONS.md` §46, `HANDOFF.md` §0c).

The active patch is now the **base** set only: chainloader/boot, display,
sensors, charger, touch, WDT2 and eMMC.  The WL1283 is to be **re-derived from
OS 2.1.0.1917** and restarted from a minimal 1917-truth `&mmc5` host, one
variable per boot.  The negative results of reports 180–192 (traps 116–137)
stand; this archive is their source state, not a to-do list.

## Contents

| file | what it is |
|---|---|
| `wl1283-instrumentation-7.3.3.patch` | re-adds the removed WL1283 code to the clean 7.3.3 tree (`patch -p1`) |

The patch is generated as `diff -u <clean tree> <pre-archive tree>` for exactly
the six files below, so it contains **only** the WL1283 deltas — the general
MMC/eMMC bring-up prints (`PBM`, `PBINI`, `PBSIO`, `PBX`, `PBREQ`, `PBIRQ`,
`pb_event`) stay in the active tree and are *not* part of this archive.

### What was moved out

* `drivers/mmc/host/omap_hsmmc.c`
  * `pb_init_hz` — the MMC5 400 kHz init-clock override (`calc_divisor`).
  * `PBCLK` — clock-divisor / SYSCTL tracing in `omap_hsmmc_set_clock`.
  * `PBD` — the per-`set_ios` CON/HCTL/SYSCTL/CAPA/STAT/PSTATE/SYSSTATUS dump.
  * `pb_sdio_ocr_quirk` + the `480d5000` branch selecting
    `MMC_CAP2_SDIO_OCR_PROBE_FFFF00`.
  * `pb_enable_fref_clk2()` — SCRM `AUXCLK1` = `0x00020100` (12.8 MHz),
    `__maybe_unused` since session 33.
  * `pb_wl1283_reset_pulse()` — WL_EN low 500 ms → high, `__maybe_unused`
    since session 33 (it also broke USB warm-reset enumeration, trap 130).
  * the one-shot `PBGPIO` bank dump in `omap_hsmmc_probe()` for `480d5000`.
* `drivers/mmc/core/core.c` — `PBCMD` (the `480d5000` CMD trace) and `PBVOLT`.
* `drivers/mmc/core/mmc_ops.c` — `PBOCR`.
* `drivers/mmc/core/sdio.c` — the CMD5 `arg = 0x00ffff00` probe quirk and the
  `PBC: sdio` prints.
* `include/linux/mmc/host.h` — `MMC_CAP2_SDIO_OCR_PROBE_FFFF00` (bit 29).
* `arch/arm/boot/dts/ti/omap/omap4-winchester.dts`
  * `vwl1283` regulator (the 3.3 V `ocr_avail` window, not a real rail).
  * `sdmmc5_pins` (pads `0x148`–`0x152`), `fref_clk2_out_pins` (pad `0x19c`),
    the WKUP `fref_clk_pins` group (pad `0x018` = `0x000B0003`).
  * `wl1283_ctrl_pins` (pad `0x07c`, GPIO2_22 / WL_EN) and `wl1283-en-hog`.
  * the `&mmc5 { status = "okay"; … }` host node.

The active board DTS keeps `&mmc5 { status = "disabled"; };` so the controller
does not probe with no board wiring, and keeps `&gpio2_target {
ti,no-reset-on-init; }` (display-output protection, not WL1283).

## Why it was moved (evidence)

The WL1283 (WiFi/BT/FM/GPS combo) is on **MMC5 `0x480D5000`** (QNX
`devnp-ti1283.so`, GIC SPI 59), **WL_EN = GPIO2_22 (gpio 54)**,
**WL_IRQ = GPIO2_21 (gpio 53)**.  Every software- and SoC-side variable was
matched to the reference and the card still never answered CMD5:

* pads, GPIOs, PMIC rails (TWL6030), `CLK32KG`, the clock manager — identical
  to QNX (reports 180/185/187/188);
* the `FREF_CLK2_REQ` input is unused by the reference; `fref_clk2_out`
  (`AUXCLK1` = 12.8 MHz) reproduced exactly — still no CMD5 (report 185);
* `CM_DIV_M5` divider write = DSS glitch, do not retry (trap 116);
* WL_EN reset pulse — negative for CMD5 and caused the USB warm-reset
  regression (trap 130);
* 200 ms post-power-on settle, CMD52 skip, the exact QNX sequence
  (CMD0 → CMD5 only, 24 MHz, arg `0x00FFFF00`) — all negative (traps 128/129/137);
* `startup-playbook`'s QNX-only GPIO outputs all released — negative (trap 136).

The remaining difference is at the chip's non-exposed VBAT/TCXO/SLOW_CLK pins or
in OMAP HSMMC sampling — a schematic/board view or logic analyser (reports
190–192).  The re-derivation (§46) exists to rule out a wrong or missing DTS
model (pads, `vmmc`/`vqmmc`, pwrseq, clocks, bus properties) before that
conclusion.

## How to restore the pre-archive state

```bash
# in the repository root, on the clean tree:
patch -p1 < kernel/archive/wl1283/wl1283-instrumentation-7.3.3.patch
```

This reproduces the exact files from before session 37 (verified byte-for-byte
against the pre-archive snapshot).  The pre-archive images and the report set
(180–194) remain the reference.  **Do not re-apply this patch on the
"1917-truth re-derivation" path** without stating what is different in the
new experiment (DECISIONS §46).
