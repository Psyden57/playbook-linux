# 185 — The WL1283: `FREF_CLK2_REQ` is unused, `FREF_CLK2_OUT` is enabled on the reference, and CMD5 still times out

**Session 32 (absolute), Linux-era 26.  2026-11-08.**
Status: **bench + static RE.**  Region 0 was flashed to the 1917 IFS for the
QNX oracle and then to four Linux test images.  The 1917 `/base` was mounted
read-only for static RE.  No RPMB, NVRAM or MCT write.  The factory counter went
`0x32 → 0x2D` over five Linux boots (TabletOS topped it up during the trip).

This is the session-31 follow-up requested by the operator's second opinion
(report 184): read `0x4A31E058` on QNX with WLAN working and on Linux, compare
the whole register, and establish whether the `FREF_CLK2_REQ` input is the
missing variable.

---

## 0. Headline

* **The second opinion's request-input candidate is eliminated by a fresh
  reference read.**  With WLAN up (`ti0` RUNNING, `slog` "SDIO clock
  Configuration is now set to 24Mhz"), `0x4A31E058 = 0x000B0003`: pad 0 is
  **mux 3 = `gpio_wk31`**, pull off, input buffer **off**; pad 1 is
  `gpio_wk7`.  QNX does **not** select mux 1 (`fref_clk2_req`).
* **The one real SoC-side difference is the FREF2 output generator.**  QNX
  enables SCRM `AUXCLK1` (`0x4A30A314 = 0x00020100`: source SYS_CLK, divide 3,
  enabled ⇒ **12.8 MHz**) and muxes CORE pad `0x4A10019C` to
  `fref_clk2_out` (`0x41000000`).  Linux had `AUXCLK1 = 0` and the pad in safe
  mode (`0x4118010F`).  Both were reproduced exactly on Linux; **MMC5 still
  does not answer CMD5**.
* **The QNX enumeration protocol differs, and it was tested.**  Static RE of
  `devnp-ti1283.so`: the first and only divider is **CLKD = 2 (24 MHz)**, there
  is no 400 kHz pre-enumeration clock, and CMD5 is sent with **argument
  `0x00FFFF00`**.  Linux 7.3.3 sends 400 kHz and CMD5 arg 0.  A Linux test at
  24 MHz (`SYSCTL = 0x000e0087`) and a test with arg `0x00FFFF00` both leave
  the card silent.
* **The chip does not respond at all.**  The last image's diagnostic prints
  `PBC: sdio probe_ocr=00ffff00 err=-110 ocr=00000000` — CMD5 times out at the
  controller, no response.  Pads, GPIO2, PMIC rails, MMC5 CON/HCTL/SYSCTL and
  the clock manager are matched; the remaining gap is at the chip
  (TCXO/power/reset), which needs an electrical measurement.

## 1. The fresh reference capture

From the live 1917 with WLAN working (`pidin`/`ifconfig ti0`, `pbmem3` reads of
identity physical addresses):

| register | QNX 1917 (WLAN up) | Linux (`0xE36C679A`) | decode |
|---|---|---|---|
| `0x4A31E050` | `0x00080000` | `0x01080000` | pad0 `fref_slicer_in`; pad1 `fref_clk_ioreq` input (pull differs) |
| `0x4A31E054` | `0x000F0003` | `0x010F0003` | pad0 `gpio_wk6`; pad1 safe mode |
| **`0x4A31E058`** | **`0x000B0003`** | **`0x01080003`** | **pad0 mux 3 `gpio_wk31`, input off**; pad1 differs (`gpio_wk7` vs `fref_clk4_req`) |
| `0x4A31E05C` | `0x0100000B` | `0x01000108` | pad0 `gpio_wk8`; pad1 `sys_32k` |
| **`0x4A10019C`** | **`0x41000000`** | `0x4118010F` | QNX pad0 mux 0 **`fref_clk2_out`**; Linux safe mode |
| **`0x4A30A314`** (`AUXCLK1`) | **`0x00020100`** | `0x00000000` | QNX source SYS_CLK, /3, **enabled**; Linux off |
| `0x4A30A310/318/31C/320/324` | `0`, `0`, `0`, `0x00010000`, `0` | `0`, `0`, `0`, `0x00010100`, `0` | only AUXCLK1 differs functionally |
| `0x4A310134/0138/013C` (GPIO1) | `6FFB1FAA` / `20700600` / `00006044` | `FFFF9FBB` / `0` / `00006044` | QNX drives bit 31 low as an output; the line reads low either way |
| `0x48055134/0138/013C` (GPIO2) | `CE3ECD5F` / `08201018` / `31413200` | `CE2ECDFF` / `01000010` / `31413200` | WL_EN high; WL_IRQ input **high** on QNX, low on Linux |
| `0x4A00814C..164` (DPLL_PER) | `2801`, M2 `808`, M3 `30D`, M4 `2C`, M5 `329`, M6 `24`, M7 `25` | `C326`, M2 `802`, M3 `3`, M4 `3`, M5 `204`, M6 `4`, M7 `3` | QNX `dpll_per_ck` 768 MHz, M5/9 gated; Linux 192 MHz, M5/4 = 96 MHz DSS |

The pad-0 line level was also measured on Linux by temporarily enabling the
input buffer: it reads **0**, so the QNX-output/Linux-input difference on
`gpio_wk31` is not a level difference.

## 2. The FREF2 output test

Two single-variable images, both booted:

| CRC | change | result |
|---|---|---|
| `0xF521AB06` | pad `0x19C` → `fref_clk2_out`; `AUXCLK1` gate enabled (divider 1 ⇒ 38.4 MHz) | `attach_sdio no` |
| `0x18D49C22` | `AUXCLK1 = 0x00020100` (12.8 MHz) | `attach_sdio no`; `0x4A30A314 = 0x00020100`, `0x4A10019C = 0x41180000`, `auxclk1_ck` 12.8 MHz / enabled |

The reference's FREF2 output is therefore matched but **not causal**.

**Correction (same session, later): those two images did not actually test
CMD5 with the clock on.**  `playbook_fref_clk2_init()` was a `late_initcall`
(level 7), which runs **after** the `omap_hsmmc` device-init probe (level 6);
the clock was enabled after the rescan.  The test was repeated with the enable
moved into `omap_hsmmc_probe()` before `mmc_add_host()` (image
`0xF38CF87E`): `PLAYBOOK: fref_clk2_out 12800000 Hz enable=0` prints at
2.567 s, before `PBM: 480d5000.mmc add_host`, and CMD52/CMD8/CMD5 still time
out.  The conclusion stands, now on valid evidence.

**The WL_EN reset pulse is also negative.**  Image `0xAF4FC103` drives
GPIO2_22 low for 500 ms and high again in `omap_hsmmc_probe()` before
`mmc_add_host()` (`PLAYBOOK: WL_EN reset pulse`, 3.336 s), replicating
`hPlatform_hardResetTnetw`; CMD52/CMD8/CMD5 still time out.

## 3. The SDIO protocol difference and its negative

Static RE of `devnp-ti1283.so` (file-only):

* `sdio_core_pm` programs `SYSCTL = 0x000E0080` (CLKD 2, DTO 14) before any
  command; the divider helper at `0xcc5b4` is called only with div 2, and the
  driver logs 24 MHz.  There is **no 400 kHz** phase.
* `sdioAdapt_ConnectBus` sends **CMD0 → CMD5 (arg `0x00FFFF00`) → CMD3 (arg 0)
  → CMD7 (arg `0x00010000`)**.
* It writes **no WKUP/FREF pad** and **no PMIC/CM_PER/DPLL_PER** register;
  only the five CORE WLAN pads, GPIO2_22/21, MMCHS5, CM2 `0x4A009560`
  (MODULEMODE=2) and PRM `0x4A307564 = 1`.  GPIO1 is mapped but unused.

Linux 7.3.3 `mmc_attach_sdio()` sends CMD5 with arg 0 at 400 kHz.  Tested:

| CRC | change | result |
|---|---|---|
| `0xEAADE61D` | host quirk: CMD5 arg `0x00FFFF00` | `attach_sdio no` |
| `0xE6F77A9B` | + dummy `vwl1283` OCR window 3.3 V + `PBC: sdio` prints | `PBC: sdio probe_ocr=00ffff00 err=-110 ocr=00000000` |

The runtime 24 MHz attempt (existing `omap_hsmmc.init_hz` knob) gave
`SYSCTL=0x000e0087` (CLKD 2, exactly QNX) and also `attach_sdio no`.  That
test's `unbind`/`bind` wedged the ttyGS0 shell while the kernel kept running;
the unit was power-cycled.  Do not use a runtime MMC5 rebind again.

## 4. The BT driver is not the WLAN enable

The 1917 `/base` `devc-seromap-winbt` was inspected (file-only).  Its
`gpio3_base` is actually **0x48059000 (GPIO4)**, and its only automatic GPIO is
GPIO4 bit 28 (force RTS), driven **high** at init — our Linux GPIO4
`OUT = 0x90000120` matches.  The `gpio_set_output` line is the libhwio id
**55 = GPIO2 bit 23**, driven only by a client `devctl 0x80040501`; it is
**low in both captures** (BT off with WLAN up).  The driver also enables
`CM_L4PER_UART2_CLKCTRL` (0x4A009548) for the BT UART; it touches no PMIC or
regulator and has no WLAN/FM/GPS/TCXO sequencing.  This line is not the missing
WLAN variable.

## 5. What is left

The WL1283's main clock is the **26 MHz TCXO** (vendor
`tiwlan_winchester.ini`: `TCXO_Clk = 1`; `wl128x_boot_clk()` selects it unless
the hardware reports FREF or the TCXO is 16.368/32.736 MHz).  `STRFRefClock = 2`
(38.4 MHz, wired-OR CLK_REQ) is the sync clock, not the enumeration clock.  The
QNX driver does not configure or gate any TCXO, so the reference's 26 MHz must
be board-supplied; whether it runs under Linux is the open question.

The next measurement is electrical, as report 184 recommended: at the chip,
capture the 26 MHz TCXO, VBAT and VIO, WL_EN, and the MMC5 CLK/CMD/DAT0 during
a cold QNX boot and a Linux boot.  A PMIC register read proves programming, not
the waveform.  The stop condition is unchanged: if the traces are identical and
valid but CMD5 still differs, investigate OMAP HSMMC sampling; the current
evidence points at the chip's clock/power, not the host.

## 6. Sources

* Live 1917 over the door: `ifconfig ti0`, `sloginfo`, `pbmem3` reads of
  `0x4A31E040..09C`, `0x4A100190..19C`, `0x4A310130..13C`, `0x48055130..13C`,
  `0x4A008140..164`, `0x4A30A210..224`, `0x4A30A310..324`.
* Linux: the same reads via the `pb> ` console, `devmem` and debugfs
  (`clk_summary`, `mmc4`/`auxclk1_ck`), plus the `PBD:`/`PBC:` boot prints.
* Static RE: `local/work/qnx-libs/devnp-ti1283.so` (session-32 subagent) and
  `local/work/rcfs-1917-mnt/sbin/devc-seromap-winbt` +
  `lib/libhwio-omap4.so.1` + `lib/dll/hwio-config-winchester.so` (session-32
  subagent).
* Reports 180 (the bus/wiring), 184 (the second opinion); traps 97/98/101/116.
