# Session 32 (absolute) — WL1283: `FREF_CLK2_REQ` unused, the FREF2 output matched, and the chip silent from CMD52

> Numbering: absolute `32` (chronological; see `NUMBERING.md`).  The reports and
> handoffs call this Linux-era **session 26**.

Date / span: 2026-11-08.
Unit state at the end: **our Linux 7.3.3 image `0xAF4FC103` in region 0**
(`boot/chainloader/kernel.img`); region 1 is the 7.3.3 charger rootfs
(`ba275807…`).  The 1917 IFS is **not** on media.  `BOOTCTR_FCTRY = 0x2A`; pack
**Full: 100 %, 4.149 V, +81 mA**.  The console is the USB CDC-ACM `ttyGS0`
(`tools/pbcon.py`); no door.  Known-good baseline: `0xE36C679A`.

## Goal of this session

Resume ROADMAP item 9 step 4 with the operator's second opinion (report 184):
read `0x4A31E058` on QNX with WLAN working and on Linux, establish whether
`FREF_CLK2_REQ` is the missing input, then the TCXO/FREF distinction, the SDIO
init-frequency checks and the electrical comparison.  Deliver the first
measurable difference between QNX and Linux.  One variable per boot; do not
repeat the `CM_DIV_M5 = 0x329` write (trap 116).

## Confirmed (measured, read in a file, or seen on the bench)

* **The reference does not use `FREF_CLK2_REQ`.**  Fresh 1917 read with WLAN
  up (`ti0` RUNNING, slog "SDIO clock Configuration is now set to 24Mhz"):
  `0x4A31E058 = 0x000B0003` — pad0 **mux 3 = `gpio_wk31`**, pull off, input
  buffer off; pad1 mux 3 `gpio_wk7`.  (Report 185 §1; `pbmem3`, live.)
* **The reference enables `fref_clk2_out` from SCRM `AUXCLK1`.**
  `0x4A10019C = 0x41000000` (pad0 mux 0), `0x4A30A314 = 0x00020100` (source
  SYS_CLK, divide 3, enabled ⇒ **12.8 MHz**); the other AUXCLK registers are
  0 except `AUXCLK4 = 0x00010000`.  Linux had the pad in safe mode
  (`0x4118010F`) and `AUXCLK1 = 0`.  (Report 185 §1/§2.)
* **The FREF2 output is not causal, on valid evidence.**  The first two test
  images (`0xF521AB06`, `0x18D49C22`) enabled `AUXCLK1` from a `late_initcall`,
  which runs **after** the `omap_hsmmc` device-init probe; the clock was not on
  for CMD5.  Image `0xF38CF87E` moves the enable into `omap_hsmmc_probe()`
  before `mmc_add_host()`: `PLAYBOOK: fref_clk2_out 12800000 Hz enable=0` at
  2.567 s, `PBM: 480d5000.mmc add_host` at 2.567 s, and the card still times
  out.  (Report 185 §2 correction; session log.)
* **The boot-time WL_EN reset pulse is negative.**  Image `0xAF4FC103` drives
  GPIO2_22 low for 500 ms and high again in `omap_hsmmc_probe()` before
  `mmc_add_host()` (`PLAYBOOK: WL_EN reset pulse (500 ms low, 50 ms settle)`,
  3.336 s), replicating `hPlatform_hardResetTnetw`; CMD52/CMD8/CMD5 still time
  out.  (Report 185 §2; session log.)
* **The default Linux SDIO attempt is one try at 400 kHz, with CMD52 before
  CMD5.**  `f_min = f_init = ios.clock = 400000`, `f_max = 52000000`,
  `SYSCTL.CLKD = 120` on a 48 MHz functional clock ⇒ actual 400 kHz.  The
  trace is CMD52 `0x00000c00` → CMD52 `0x80000c08` → CMD0 → CMD8 `0x1aa` →
  CMD5(arg 0) → CMD55 ×4 → CMD1; every command returns `err=-110`
  (`-ETIMEDOUT`, `resp0=0`) except CMD0.  (Report 186 §1; image `0x9471F6C9`;
  log `local/logs/boot-20261108-sdio-audit.log`.)
* **The QNX driver's own enumeration is 24 MHz with CMD5 arg `0x00FFFF00`.**
  Static RE of `devnp-ti1283.so`: `sysctl = 0x000E0080` (CLKD 2) before CMD0,
  no 400 kHz phase, sequence CMD0→CMD5(`0x00FFFF00`)→CMD3→CMD7; it writes no
  WKUP/FREF pad and no PMIC/CM_PER/DPLL_PER register (only the five CORE WLAN
  pads, GPIO2_22/21, MMCHS5, CM2 `0x4A009560` and PRM `0x4A307564`).  (Report
  186 §1; session-32 subagent.)
* **The BT driver is not the WLAN enable.**  `devc-seromap-winbt`'s
  `gpio3_base` is actually GPIO4 (`0x48059000`); its automatic line is GPIO4
  bit 28 (force RTS) high, which Linux matches (`GPIO4 OUT = 0x90000120`); its
  `gpio_set_output` line is GPIO2 bit 23 (BT_EN), low in both captures.  It
  enables only `CM_L4PER_UART2_CLKCTRL` (`0x4A009548`) and touches no
  PMIC/regulator.  (Report 186 §4; session-32 subagent.)
* **The QNX power manager is a separate, unexamined layer.**  `/base/bin/powerman`
  is started as `powerman -c devpm-omap4.so -o1 -l5000 -d
  /etc/system/config/winchester.pm`; the `.pm` file says
  `use_preset_config winchester`, so the dependencies are compiled into the
  binary.  Its strings include `winchester_environment`,
  `activity/wifi_driver/active|idle`, `air_needs_wifi`, `wlan_needs_DMA`,
  `WLAN_NEEDS_PREVENT_OSWR`, `ldo_vaux3`/`ldo_vaux1`, `/pps/services/wifi/control`
  and `msg::wifi_power`.  `scripts/wifi.sh` loads the WLAN driver with
  `-o nvram,firmdir=/var/ti1283`.  (Read in `rcfs-1917-mnt`; RE primed for
  session 33, not completed.)
* **The Kindle Fire HD 3 (`amazon-soho`) is not a WL1283 reference.**  The
  archived pmaports packages install **Broadcom BCM4330** firmware
  (`bcmdhd.cal`, `fw_bcm4330.bin`), load `dhd`, and the vendor kernel config
  has `CONFIG_WL12XX_MENU` off.  (`/tmp/pmaports/.../device/archived/` from
  the operator's `pmaports-main.tar.gz`.)
* **The multimeter access is unlabelled.**  The WL1283C is under a shield with
  three unlabelled test points nearby and several on the other side; there is
  no PlayBook schematic or board view.  (Operator observation.)

## Inferred (reasoning from evidence, not directly measured)

* **The WL1283's digital core is not running under Linux.**  CMD52 itself
  times out at 400 kHz with the FREF2 output on, WL_EN high and the WL_EN
  reset pulse applied; the controller sees no response at all.  *Strength:
  strong.*
* **The host-side SDIO configuration is matched.**  Pads, GPIO2, MMC5
  CON/HCTL/SYSCTL, the functional clock and the init sequence have all been
  compared with the reference or tested; the card is silent before any
  host-specific timing could matter.  *Strength: strong.*
* **The remaining candidates are the chip's 26 MHz TCXO / VBAT / VIO and any
  board-level rail or clock enabled by QNX's power manager.**  The Motorola
  148603 guide says the TCXO is powered from the chip's **internal 1.8 V**, so
  a missing VIO kills the TCXO too.  *Strength: strong for the TCXO/VIO
  dependency; the powerman rail is unproven.*

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`FREF_CLK2_REQ` (mux 1) is the missing input."**  QNX leaves
  `0x4A31E058` pad0 as mux 3 `gpio_wk31` with the input off.  Killed by the
  fresh reference read (report 185 §1).
* **"The first FREF2 test proved the output is not the clock."**  It did not:
  the enable was a `late_initcall` after the probe.  The corrected in-probe
  test is negative (report 185 §2 correction).  The general lesson is trap
  124.
* **"24 MHz is the Linux default / the init clock is the problem."**  The
  default is one 400 kHz try; 24 MHz was the deliberate `init_hz` test (report
  186 §1, trap 119).
* **"Linux sends CMD5 first; it never sends CMD52/CMD0 before it."**  Report
  184's recording note looked only inside `mmc_attach_sdio()`; its caller
  `mmc_rescan_try_freq()` sends `sdio_reset()` (CMD52), CMD0 and CMD8 first.
  Corrected in report 184 and report 186 §2.
* **"A runtime MMC5 `unbind`/`bind` is a cheap rescan."**  It wedged the
  ttyGS0 shell while the kernel stayed up; the unit needed a power cycle
  (trap 120).
* **"The Kindle Fire HD 3 is a WL1283C reference."**  Its pmaports packages use
  a BCM4330 (`dhd`), not the WL1283.
* **"`CM_DIV_M5 = 0x329` is the FREF divider."**  Session 31 already showed it
  is the DSS divider (trap 116); this session did not repeat it.

## Unknown / open questions

* **What does QNX `powerman`'s `winchester` preset enable for the WLAN?**
  Which rails/clocks do `wifi_driver`, `air_needs_wifi`, `wlan_needs_DMA` and
  `ldo_vaux3`/`ldo_vaux1` map to, and does any of them reach the WL1283?
  *Would settle it:* RE `bin/powerman` + `devpm-omap4.so` for the preset table
  and the devpm device names, then read those TWL/CM registers on both OSes.
* **Are VBAT/VIO/WL_EN/IRQ/SLOW_CLK actually present at the chip?**  The
  motorola guide gives the expected DC values, but the three nearby test
  points are unlabelled and there is no schematic.  *Would settle it:* a
  multimeter survey of the accessible points (VBAT ~3.3 V, VIO ~1.8 V, WL_EN
  ~1.8 V, SLOW_CLK ~0.9 V average) or a board view.
* **Is the 26 MHz TCXO running?**  A multimeter cannot measure its frequency;
  it can only check its supply/bias.  *Would settle it:* a scope/analyser, or
  a known-good TCXO swap.

## Superseded / uncertain

* **Report 185's first FREF2 test (images `0xF521AB06` / `0x18D49C22`) is not
  valid as a CMD5 test** — the enable ran after the probe.  Report 185 now
  carries the correction; the corrected test (`0xF38CF87E`) is negative.
* **Report 184's "Linux does not send CMD52/CMD0 before CMD5" is corrected**
  at the `mmc_rescan_try_freq()` level (report 186 §2).
* **The session-31 statement "the WL1283 needs a bus capture" is still true
  for the chip side, but it is not the whole remaining software surface** —
  the QNX power manager is unexamined (primed above).

## Artefacts produced

* Reports: `docs/reports/185-wl1283-fref-request-and-fref2-2026-11-08.md`
  (the fresh QNX capture, the FREF2 work and its timing correction, the reset
  pulse), `docs/reports/186-wl1283-sdio-audit-2026-11-08.md` (second opinion 2,
  the CMD trace, the Motorola guide facts).
* Images (region 0, all under `boot/chainloader/kernel.img` history):
  `0xF521AB06` (FREF pad + gate, 0x91A8A4), `0x18D49C22` (FREF 12.8 MHz,
  0x919BDC), `0xEAADE61D` (CMD5 arg, 0x91AEAC), `0xE6F77A9B` (CMD5 arg +
  3.3 V OCR + PBC diagnostics, 0x91B0A4), `0x9471F6C9` (SDIO audit: quirk off,
  PBCMD trace, 0x91ACDC), `0xF38CF87E` (FREF2 enabled before the probe,
  0x91B2EC), **`0xAF4FC103`** (plus the WL_EN reset pulse; 0x91A23C; on media).
* Source in `local/kernel-tree` (and the patch): board DTS (`fref_clk2_out_pins`,
  `vwl1283` 3.3 V), `arch/arm/mach-omap2/omap4-common.c` (the `late_initcall`
  removed), `drivers/mmc/host/omap_hsmmc.c` (`sdio_ocr_quirk` param default
  off, computed CLKD/actual, `pb_enable_fref_clk2()`,
  `pb_wl1283_reset_pulse()` before `mmc_add_host`), `drivers/mmc/core/core.c`
  (`PBCMD` trace, `f_min`/`f_init`/`ios.clock` line), `drivers/mmc/core/sdio.c`
  (probe OCR + diagnostics), `include/linux/mmc/host.h`
  (`MMC_CAP2_SDIO_OCR_PROBE_FFFF00`).
* Patch: `kernel/patches/0001-playbook-winchester-7.3.3.patch` — **7 257
  lines / 47 files**, sha256 `ad55511d…`, 0 rejects, 47/47 byte-identical.
* Logs: `local/logs/boot-20261108-sdio-audit.log` (the full audit boot).
* QNX trip: 1917 IFS restored to region 0, door up (root SSH), `pbmem3`
  pushed; WKUP/GPIO/DPLL capture in the session log.  The 1917 `/base` was
  mounted read-only for the driver/powerman RE.
