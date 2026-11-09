# Session 37 (absolute) — WL1283 archived, 1917 DTS re-derived, CMD5 negative, WLAN calibration found

> Numbering: absolute `37`; the reports call this Linux-era **session 31**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-09.
Unit state at the end: **on TabletOS 2.1.0.1917** — region 0 = the 1917 IFS
(`local/work/ifs-shim-sealed.ifs`, restored for the calibration check),
region 1 = the 7.3.3 charger rootfs (`ba275807…`, 17 modules).
`ti0` UP (40+ scans, MAC `14:74:11:8f:9c:c3`); `BOOTCTR_FCTRY = 0x32`.
This is the default restored state.  The Linux Task-3 image `0x0DC36A11` is the
way back to Linux (region 0 only); the known-good `0xE36C679A` and touch
`0x04AF1433` remain.  All device access was read-only; no RPMB/NVRAM/MMC write.

## Goal of this session

Per the operator direction at session 36 end (`HANDOFF.md` §0c, `DECISIONS.md`
§46), in order: **(1) archive the WL1283 code** out of the active tree and
patch, **(2) re-derive the board DTS node by node from OS 2.1.0.1917**, and
**(3) restart `&mmc5` from a minimal 1917-truth host** — one variable per boot.
"Done" = the base patch carries no WL1283 code, every DTS node cites 1917, and
the minimal host has been booted with its CMD5 result read.

**Scope decision (operator, session 37):** strip only the **named WL1283 code**
— the SDIO quirk/cap2, `pb_sdio_ocr_quirk`, `pb_init_hz`, `pb_enable_fref_clk2`,
`pb_wl1283_reset_pulse`, the `PBCMD`/`PBCLK`/`PBD`/`PBOCR`/`PBVOLT` prints, and
the DTS `vwl1283`/`fref*`/`wl1283_ctrl_pins`/`wl1283-en-hog`/`&mmc5` nodes.  The
general eMMC bring-up prints (`PBM`/`PBINI`/`PBSIO`/`PBX`/`PBREQ`/`PBIRQ` and
`pb_event`) stay (KERNEL-PORT §7.2's full PB* prune is a separate change).

## Confirmed (measured, read in a file, or seen on the bench)

### Task 1 — archive

* **The WL1283 code is out of the active tree and patch.**
  `kernel/archive/wl1283/wl1283-instrumentation-7.3.3.patch` (517 lines) +
  `README.md`; the patch is the exact `clean tree → pre-archive` diff for six
  files and **round-trips** (applied to a copy, `cmp` OK on all six)
  [`kernel/archive/wl1283/`].
* **Stripped:** `omap_hsmmc.c` (`pb_init_hz`+`calc_divisor` override, `PBCLK`,
  `PBD`, `pb_sdio_ocr_quirk`+the `480d5000` `MMC_CAP2_SDIO_OCR_PROBE_FFFF00`
  select, `pb_enable_fref_clk2`, `pb_wl1283_reset_pulse`, the `480d5000` PBGPIO
  dump); `core.c` (`PBCMD`, `PBVOLT`, the added `<linux/string.h>`); `mmc_ops.c`
  (`PBOCR`); `sdio.c` and `host.h` reverted to pristine; the DTS `vwl1283`,
  `sdmmc5_pins`, `fref*`, `wl1283_ctrl_pins`, `wl1283-en-hog`, `&mmc5`
  [`local/kernel-tree/`].
* **Base patch re-exported:** 46 files, **7 305 lines** (36 modified + 10 new),
  sha256 `d0d00009…`, **0 rejects / 46/46 byte-identical** against pristine
  7.3.3 (`sdio.c`/`host.h` left the patch) [`kernel/patches/`].
* **Builds clean:** `make zImage modules` + DTB, no warnings on
  `omap_hsmmc.o`/`core.o`/`mmc_ops.o`.  The WL1283-free image `0x05819994`
  (`0x91A304`) was built (later superseded by `0x0DC36A11`).

### Task 2 — the 1917-truth DTS audit (report 195)

* **The 1917 `/base` was mounted read-only** with
  `local/vendor/bb10mt/bb10mt qnx6 mount -i local/work/rcfs-1917.rcfs -m …`
  and the artifacts read directly: `lib/dll/hwio-config-winchester.so`
  (13 968 B, 2013-01-15; `pad_config` 248 × 20 B, `gpio_config` 72 × 32 B,
  sizes 248/72), `usr/lib/graphics/omap4430/graphics.conf`
  (`driver = munin`, `tp_intr=1035,i2c_slave=0x65`),
  `lib/firmware/ti1283/tiwlan_winchester.ini`, `board.tdf`
  [report 195 §3.2].
* **The 1917 `devnp-ti1283.so` hashes to `a5fd35de…`**, identical to
  `local/work/qnx-libs/devnp-ti1283.so` (so the local copy is 1917)
  [`sha256sum` of the mounted file and the local copy].
* **`startup-playbook` extracted and analysed:** IFS imagefs `0x908`,
  `0x44008` B, sha256 `bc7d3a41…` (matches report 192); raw ARM at
  `0x80100000`.  Its Winchester GPIO table (image `0x80122750`, 75 × 32 B)
  holds records 13/14 **byte-identical** to the hwio `gpio_config`
  (`35 … 02 … 59` / `36 … 01 … 58`): **gpio 53 = input/falling signal `0x59`,
  gpio 54 = output/low signal `0x58`**, rev 0–15, phase 1.  Its option strings
  are `WLAN_EN_GPIO=54,WLAN_INT_GPIO=53` [report 195 §3.2].
* **Report 189's `gpio_config` field labels are corrected:** `+0x0c` is the
  edge-detect field (2 = falling), `+0x11` the pull/enable, `+0x18` the pad
  signal id (not `+0x1c`) [`KNOWN-ISSUES.md` §3.22].
* **The board DTS header is corrected** (it no longer claims a 2.0.0.4869
  reverse-engineering; it names the 1917 provenance), and two stale comments
  fixed (I²C1 = `0x48070000`; I²C3 is not the touch bus) [report 195 §0/§4].
* **Node-by-node provenance audit + the `&mmc5` re-derivation** are in report
  195 (provenance classes P1 live-1917 / P2 1917-artifact / P3 derived / P0
  SoC-TRM).

### Task 3 — the one experiment (report 196, negative)

* **The re-derived minimal `&mmc5` is in the tree and enabled**: `sdmmc5_pins`
  (`0x148`–`0x152` mux 0/pull-up), `wl1283_ctrl_pins` (WL_EN `0x07c` = `0x0003`),
  `wl1283-en-hog` (gpio 22), `vwl1283` fixed **1.8 V**, 4-bit,
  `non-removable`/`ti,non-removable`, `no-sd`, `no-mmc`,
  `max-frequency = <24000000>`; WL_IRQ `0x07a` stays in `bridge_pins`
  [`kernel/source/omap4-winchester.dts`, report 195 §3.4].
* **Image `0x0DC36A11`** (`0x91A514` B, kernel `7.3.3 #24`), flashed **region 0
  only** (588 frames, `COMPLETE 0x40C0 -> cmd 0x4006`) [flash log].
* **CMD5 still does not answer.**  `mmc4` (480d5000) probes at 400 kHz with
  `f_max=24000000`; `sdio_reset`(CMD52) → `go_idle`(CMD0) → `attach_sdio`(CMD5)
  → **`attach_sdio no` at 2.829 s**, `Failed to initialize a non-removable
  card` at 2.831 s; **no "no support for card's volts" warning**, so the
  failure is at CMD5 [`local/logs/boot-0x0DC36A11-1917mmc5.log`].
* **The DT is provably applied and 1917-identical** (`devmem` readback):
  `0x4A100148`/`0x14C`/`0x150` = `0x0118`, `0x4A10007C` = `0x0003`,
  `0x4A10007A` = `0x410B`; `GPIO2_OE = 0xCE2ECDFF`,
  `GPIO2_DATAOUT = 0x31413200` (WL_EN bit 22 high),
  **`GPIO2_DATAIN = 0x01000010` (WL_IRQ bit 21 = 0 — chip not up)**
  [report 196 §2].
* **The stripped base is healthy:** `mmcblk1` (`mc1:0001 MCGAFA 59.4 GiB`,
  boot0/boot1/rpmb), the **Munin touch works** (`input`/`event0`, bootloader
  `0xb0`, app `0x020a`, build `0x3b`), `bma150` (`event1`), root
  `/dev/loop0 ext4 (rw)` [boot log; `/proc/partitions`, `/proc/bus/input/devices`].

### Calibration — the QNX `hd5` factory partition (report 197)

* **QNX exposes `hd4` (512 KiB, MCT header, magic `0x92BE564A`), `hd5`
  (512 KiB, the factory/calibration partition), `hd6` (128 KiB, empty)**
  [report 197 §1].
* **`hd5` layout:** header `0x0`–`0x9e4` (2 532 B: serials
  `PRU2QCIK014M03D23`/`DRU2QCI14P7530`, module MACs `E89A8F165145`/
  `7C8EE43C2261`, 2011-04 factory timestamps); **radio-parameters INI
  `0x9e4`–`0x5492` (19 118 B, 252 lines)**; **binary calibration blob
  `0x5492`–`0x10206` (44 405 B)**; zero to 512 KiB [report 197 §2].
* **The INI carries the per-unit TI WL128x calibration** (`TxPower = 205`,
  `XTALItrimVal = 4`, `TxBiPReferencePower_2_4G = 128`,
  `RxFemInsertionLoss_2_4G = 16`, 2.4/5 GHz `TxPDVs*` tables; a
  "Quanta Ver 4.3" changelog) [report 197 §2.2].
* **The assigned WLAN MAC is in NVRAM `deviceinfo`:** `WLANMAC = 1474118f9cc3`,
  `BTMAC = 1474118f9cd9` [PPS `deviceinfo@0.0.660`].
* **TabletOS confirms the WL1283 works under QNX:** `ti0` UP/RUNNING, 40+ scan
  results (2.4 and 5 GHz), `wifi_power::on`, MAC `14:74:11:8f:9c:c3`
  [`/pps/services/wifi/status`, `ifconfig ti0`].
* **`BOOTCTR_FCTRY = 0x32`** on this TabletOS boot (the TabletOS top-up ran)
  [`cat /dev/rpmb/BOOTCTR_FCTRY` → the byte `0x32`].

## Inferred (reasoning from evidence, not directly measured)

* **The 1917-truth DT model is not the missing piece for CMD5.**  The negative
  plus the bit-for-bit-identical pad/GPIO readback (WL_EN high, WL_IRQ low)
  rules out a wrong/missing DT model.  *Strength: strong (measured).*
* **The chip is not up under Linux.**  WL_IRQ bit 21 = 0 with WL_EN high, the
  same as report 188; under QNX it is 1.  *Strength: strong (measured).*
* **The hd5 binary blob is the machine-readable side of the same calibration**
  as the INI (it begins after the INI's `Radio parameters - End`, 44 405 B of
  IEEE-754-looking tables).  *Strength: moderate — the format is not decoded.*
* **The strip is functionally safe for the eMMC** — the removed code is
  MMC5-gated or diagnostic; the eMMC path (`vemmc`, `&mmc2_target ti,no-idle`,
  the init-stream mask, the IRQ stray-stat ack) is untouched.  *Strength:
  strong — confirmed by the healthy Task-3 boot.*

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`bb10mt` is not available, so the 1917 `/base` cannot be mounted."**
  **Wrong.**  `bb10mt` is a self-contained ELF at
  **`local/vendor/bb10mt/bb10mt`** (v0.5.9.999); it mounts
  `local/work/rcfs-1917.rcfs` fine.  (I looked only on `PATH`/`$BB10MT`; the
  operator corrected it.)  All Task-2 artifact reads came after this.
* **"The 1917-truth DT will make CMD5 answer."**  **Disproved** by Task 3:
  `attach_sdio no`, no voltage warning, pad/GPIO readback identical to QNX,
  WL_IRQ low [report 196].
* **"Removing the board `&mmc5` node disables the controller."**  **Wrong:**
  `omap4-l4.dtsi`'s `mmc5` has no `status`, so it is enabled by default; the
  board DTS must set `status` explicitly (trap 142).
* **"Archiving `PBCMD`/`PBOCR` still lets me read the CMD5 error code."**
  **Wrong:** the per-command prints are the only source of `err`; the Task-3
  image cannot print it.  The failure location is inferred from the absence of
  the voltage warning (trap 143).
* **"Revert the five MMC files to pristine to strip the WL1283 code."**
  Rejected: much of `omap_hsmmc.c`/`core.c`/`mmc_ops.c` is eMMC bring-up and
  two functional fixes; the operator chose the named list.
* **"hd4/hd5/hd6 are small filesystems."**  Wrong: `hd4` is the MCT header
  (`0x92BE564A`), `hd5` the factory/calibration block, `hd6` empty [report 197].

## Unknown / open questions

* **What is at the chip's non-exposed pins — VBAT / 26 MHz TCXO / SLOW_CLK —
  and what do MMC5 CLK/CMD/DAT0 look like?**  *Would settle it:* a board view /
  schematic or a logic analyser (the software surface is exhausted; reports
  180–196).  Do not re-run traps 116–141 without stating what is new.
* **The hd5 binary blob's format** — is it directly a mainline `wl128x-nvs.bin`
  body, or a QNX-specific container?  *Would settle it:* decode the blob against
  the TI NVS layout and the INI's field lengths; or a capture of what the QNX
  driver hands the firmware.
* **`hd5`'s exact MCT entry/offset.**  `hd4` is the MCT header; the entry that
  maps `hd5` was not parsed.  *Would settle it:* parse the MCT TLVs and match
  the 512 KiB extent.
* **Does the 1.8 V vs 3.3 V OCR window matter?**  CMD5 never reaches
  `mmc_select_voltage`, so this image cannot say.  *Would settle it:* a bus that
  enumerates.
* **Touch axis calibration/orientation** (vendor 1280×768 → 1024×600) and the
  IRQ pad (`tp_intr=1035` = GPIO2_3, inferred); **WM8994 audio not started**
  (read-only codec-ID at I²C4 `0x1a` is the first step).

## Superseded / uncertain

* **The 48-file / 7 620-line / sha256 `28e48f56…` base patch** is superseded by
  the 46-file / 7 305-line / sha256 `d0d00009…` export (the WL1283 hunks moved
  to the archive).
* **"`&mmc5` is disabled in the base."**  After Task 1 it was
  `status = "disabled"`; after Task 3 the re-derived minimal host is **enabled**
  (and negative).  The current tree/patch has it enabled.
* **"The media holds `0x0DC36A11`."**  True mid-session; at session end region 0
  is the **1917 IFS** (TabletOS), the default restored state.
* **"The DTS was reverse-engineered from 2.0.0.4869 and needs no pinctrl."**
  Corrected: functional nodes are 1917-sourced and the file has many pinctrl
  groups (report 195; the header was rewritten).
* **Report 189's `gpio_config` field offsets** are superseded by the direct
  1917 read (KNOWN-ISSUES §3.22).

## Artefacts produced

* `kernel/archive/wl1283/wl1283-instrumentation-7.3.3.patch` (517 lines) +
  `kernel/archive/wl1283/README.md`.
* `kernel/patches/0001-playbook-winchester-7.3.3.patch` — 46 files / 7 305 lines
  / sha256 `d0d00009…`; `kernel/patches/README.md` updated.
* `kernel/source/omap4-winchester.dts` and
  `local/kernel-tree/.../omap4-winchester.dts` — old WL1283 nodes archived; the
  re-derived minimal `&mmc5` added and enabled.
* `boot/chainloader/kernel.img` — Task-3 image **`0x0DC36A11`**
  (`0x91A514` B, kernel `7.3.3 #24`).  Earlier `0x05819994` superseded.
* Reports **195** (1917-truth DTS audit), **196** (minimal host, negative),
  **197** (the factory/calibration partition).
* `local/work/1917/base/` (whole 1917 `/base`), `local/work/1917/ifs/`
  (+ `proc/boot/startup-playbook`), `local/work/1917/parts/{hd4,hd5}.bin`,
  `local/work/1917/calib/{hd5-header.bin,wlan-calib.ini,wlan-calib-blob.bin}`.
* `local/logs/boot-0x0DC36A11-1917mmc5.log`.
* Docs: `STATE.md`, `HANDOFF.md` (session-38 kickoff), `ROADMAP.md`,
  `KNOWN-ISSUES.md` (traps 142/143, correction 3.22), `DECISIONS.md` §46
  executed.
