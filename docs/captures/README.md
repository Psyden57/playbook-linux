# Captures — live text evidence

These are raw captures taken from the unit during bench sessions: register
dumps, DRAM ring buffers, NV/RPMB reads and console transcripts.  They are
**evidence**: do not edit them.  When a report quotes a register value, check it
here before changing the report.

Naming is `playbook-live[-spare]-<date>-<topic>.txt` (the `spare` captures are
from the unit that is now down).  `docs/reports/data/` holds the binary captures
and the larger dumps.

## WM8994 reference codec, full register file (session 41)

Two complete 1905-register dumps of the **working** WM8994 on the reference
(2.1.0.1917, `io-audio` running), read with `tools/pbi2c dump16` over the door.
**Both were taken during playback.**

| file | when |
|---|---|
| `wm8994-reference-live-2026-11-11.txt` | playback |
| `wm8994-reference-playing-2026-11-11.txt` | playback, moments later |

They differ in **only two registers** (`R0x402`/`R0x403`, DAC1 volumes
`0x007d → 0x0073`) — a volume change, not a state difference.  **Neither is an
idle capture:** the reference's idle state (`R0x200 = 0x0010`,
`R0x003 = 0x0000`, `R0x04C = 0x1F25`, `R0x732 = 0x6020`) was measured with
targeted single-register `pbi2c rd16` reads and is recorded in report 205 §5g,
not in these files.

These are the baseline for any future WM8994 comparison: **diff our live register
file against these first** before theorising (report 205 §5f).


## WM8994 oracle + Linux captures (session 43)

Taken to settle why MCLK1 does not clock under Linux (reports 207/208).

| file | when |
|---|---|
| `wm8994-oracle-live-2026-11-12.txt` | minimal 1917 oracle, `io-audio` running: codec ranges, SCRM `AUXCLK1`/pad/CM2, and the AUXCLK1 causality tests |
| `wm8994-linux-capture-2026-11-12.txt` | our Linux image: idle/stream codec, the SoC-vs-reference diff table, the codec freeze detector, the PRM/CKGEN diff, and the ruled-out list |


## WM8994 PM/CM comparison and the ABE-refclk fix (session 44)

Taken to run and close the power-domain/CM hypothesis, then to fix
`CM_ABE_PLL_REF_CLKSEL` (report 209).

| file | when |
|---|---|
| `wm8994-pm-cm-2026-11-12.txt` | both OSes: `PM_*_PWSTST` and `CM*_CLKSTCTRL`, the oracle causal test (Linux CM policy on the working reference), the Linux detector runs and the write-order replay, and the three-image DPLL_ABE comparison |
| `wm8994-e5-master-srg-2026-11-13.txt` | session 45, both E5 boots (the wrong 12.288 MHz SRG base and the correct 49.152 MHz one), the clock tree, the rate measurements, the codec reads, the PCM-ring and FIFO-status diagnostics, the pad dump, the full register diff against the reference, and E6/E9 (oracle pads during playback; the two-link build and the stalled data port) |


## WM8994 E10 — the codec is on McBSP3, and the stereo path works (session 46)

| file | when |
|---|---|
| `wm8994-e10-mcbsp3-2026-11-13.txt` | the oracle's routing log (`AIF 0 Downlink: …->MCBSP3`), the full pad range, the McBSP3 register block, the Linux two-link pads/`XCR2 = 0x8041`, the codec registers, and the operator's both-ears verdict |

This is the capture behind report 214.  Note that user-space reads of the
idle McBSP1/2 register blocks abort (`SIGBUS`); only the active McBSP3 target
reads (trap 206).

## WM8994 speaker path — line outputs, `spk` low, GPIO1_15 high (session 47)

| file | when |
|---|---|
| `wm8994-speaker-2026-11-13.txt` | (A) the minimal 1917 oracle with `Headphone Select` = false and `audio_out` playing — the full 1865-register dump that shows the speaker route (`PM3 = 0x3cf0`, `R0x01e = 0`, `R0x034/035 = 1`, `R0x036 = 0`, `R0x038 = 0x80`, `R0x039 = 0x1c8`, `R0x04c = 0x1f25`, `R0x060 = 0`); (B) our Linux image during the same speaker playback (readable registers via the regmap debugfs) |

This is the capture behind report 215.  Read it with the caveat that the regmap
debugfs side (B) is the driver's cached/intended state, not a silicon read
(trap 207).  The decisive board facts are the GPIO states recorded in the report
(`spk` out lo, `boost` out hi while playing).

## WL1283 session-50 probes (reports 228/229)

| file | what |
|---|---|
| `wl1283-bt-probe-2026-11-14.txt` | the BT-core liveness probe on Linux `0xBAFD9B3B`: baseline (no `BT_EN`) and three pulses, with the pad/GPIO2/UART2 evidence (`pad 0x4A10007C = 0x00030003`, `DATAOUT` bit 23 0→1, `LSR = 0x60`) — silent |
| `wl1283-slow-clock-2026-11-14.txt` | full `dmesg` of image `0x91E22899` (kernel #58): the identification ladder at 400/300/200/100 kHz and the decoded `CTO`-only `STAT` at 100 kHz |

Read them with report 229's caveat: the `PBC: mmc4 try …` lines come from a
pre-existing trace, `PBX: … CMD…`/`PBIRQ`/`PBCMD` from the archived 66-line
instrumentation, and the experiment was reverted afterwards.

## WL1283 bring-up (reports 230/231)

| file | what |
|---|---|
| `wl1283-enumerate-2026-11-14.txt` | the card enumerating after `CLK32KG` is enabled: `CLK32KG = 07/01/55`, `mmc4: new high speed SDIO card`, `0x0097:0x4076`, two functions (image `0x69CDAC94`) |
| `wl1283-wlan-up-2026-11-14.txt` | the full WLAN bring-up: `wlcore: loaded`, `firmware booted (Rev 7.3.10.0.142)`, `wlan0` link/ether, the modules (image `0x9712A086`) — **and the `sdio read failed (-110)` / hardware-recovery loop** (report 231 §6) |

The raw capture is `dmesg`; the `PBR:`/`PBSIO:`/`PBC:` lines are the project's
own driver-binding/bus traces.

## WL1283 stability — the `-110` is ELP (report 232, session 51)

| file | what |
|---|---|
| `wl1283-elp-2026-11-14.txt` | the causal toggle on a working `wlan0`: `echo 2 > …/wlcore/sleep_auth` (ELP) and the `-110` + `wlcore_irq` recovery loop that follows, on the final image (`0x9712A086` + rootfs `7e067560…`).  The header quotes the instrumented run that located the failing transfer (`PBRD addr 0x1efc0 len 84 fixed 0 blksz 256`), which a `dump=1` trace cannot show — it hides the failure (trap 239) |

**It is not a fault capture**: the failure is induced on purpose, in one
variable, to prove the mechanism.  With the driver's CAM default the same read
succeeds and the interface stays up.

## WL1283 scan — accepted, never completed (report 233, session 51)

| file | what |
|---|---|
| `wl1283-scan-2026-11-14.txt` | `iw dev wlan0 scan` with the interface stable: power save, the failed scan, the `wl12xx` IRQ count and the `wlcore: Scan completed due to error.` at the driver's 30 s timeout, which then queues the unintended recovery |

The 30 s gap is the driver's `WL1271_SCAN_TIMEOUT`, not a hang (trap 240).

## hd3/Nura first boot — the eMMC write stall (report 241, session 54)

| file | what |
|---|---|
| `playbook-live-2026-11-15-hd3-nura-boot.txt` | the full first Nura boot `dmesg` from hd3 (image `0x060B25F2`, hd3 at 0x4915): the ext4 mount, `BOOTCTR_FCTRY = 0x1e`, `omapdrm`/Munin readiness, the `mmc1: Card stuck being busy! __mmc_poll_for_busy` at 21.376 s, the journal abort/`emergency_ro`, the second stall at 45.756 s, the `wl128x-fw-4-sr.bin`/`wl128x-nvs.bin` `-2` errors, and the unconditional `PBC`/`PBSIO`/`PBINI`/`PBX`/`PBM`/`PBG`/`PBS`/`PB:` traces (now `pr_debug`) |

**Read the eMMC lines as the failure and the `PB*` lines as instrumentation.**
The stall precedes the `mmc1` rescan/prints; the trigger is the first rewrite of
the bootrom-written NAND, not the logging (report 241 §3).  The `PB*` prints are
now `pr_debug` (dynamic debug); `PBREQ`/`PBIRQ` stay behind `pb_trace` (§6).

## Munin touch — evdev, raw report and driver trace (session 57)

| file | what |
|---|---|
| `touch-munin-2026-11-16.txt` | the chip's raw report stream with the driver unbound (a stable hold, a smooth drag, and five fast double-taps giving 14 contacts with 76–118 ms lift gaps); the `PBTS` driver trace showing **2 reads across a 480 ms contact** while the chip's counter advanced 40 samples; the evdev+character alignment for a slow `hello` (8–33 ms end-to-end); and the position-ghost example (`h,e,h,h` plus an in-contact move).  This is the evidence behind report 247 and traps 296/297.  The tools (`evcap.py`, `keylog.py`, `muninraw`) lived in `/tmp` and are not tracked. |
| `touch-munin-2026-11-16-session58.txt` | the fixes on kernel `#74`: the clean `a,b,c,BackSpace` run (four contacts, each one down and one up), the `keylog.py` proof that **one backspace contact becomes three `0x7f`** while one `a` tap is one `0x61` and a 1.3 s `a` hold is still one `0x61`, and the DPMS-off observation (taps reach the kernel, `bl_power` stays 4).  Evidence behind report 248 and traps 298/299.  The tools are now tracked in `tools/bench/`. |
| `playbook-boot-2026-11-16-rw-barrier0-grow-dmesg.txt` | the full 696-line dmesg of the first working read-write boot (**kernel `#76`**): `EXT4-fs (loop0): barriers disabled`, the `Card stuck being busy!` / `sector 0` flush at 43 s (the grow's start), and `resized filesystem to 15273984`.  Evidence for report 249 §2–§3. |
| `playbook-boot-2026-11-16-rw-writethrough-dmesg.txt` | the full 694-line dmesg of the final boot (**kernel `#77`**, region 0 `0x294F8E36`): the write-through setting, the one `sync` stall at 45.8 s (`flags 0x900000`), and the complete error review in report 249 §5. |

## Nura on the ARMv7 kernel — boot log and a 4-minute kmsg stream (session 65)

| file | what |
|---|---|
| `playbook-live-2026-11-18-nura-armv7-boot.txt` | the full 707-line `dmesg` of the first Nura boot on `0xC9FEE2D1` (`#105 SMP`, ARMv7, region 0): the `smp: Brought up 1 node, 2 CPUs`, the early WDT `KICK FAILED` followed by the successful `ldr fffc1800->ffe2b400, pet 10s`, the `twl6030_uv_to_vsel: OUT OF RANGE` ×7, the initramfs handoff and `resize2fs` (`152832 → 15273984`), the `mmc1: Card stuck being busy` + lost async WRITE at 27 s, `audio-ldo: disabling` at 31.8 s, and the DSI/TCON timing.  It was taken before the ARMv7 modules were installed, so the `.gnu.linkonce.this_module … must match` lines are the **old** modules (0 after the install + reboot).  Report 263 §3. |
| `playbook-live-2026-11-18-nura-kmsg-stream.txt` | 72,992 bytes of `/dev/kmsg` (records 0–722) streamed to the ACM and captured host-side for 240 s with **no disconnect**, followed by 90 s idle with the port closed (still up at uptime 376 s, no `VUSB` line).  The capture is the evidence that the USB teardown is intermittent, not an idle timer (report 263 §4a; trap 337; `docs/contradictions/usb-vusb-drop-idle-vs-intermittent.md`).  It also shows `twl6030_charger … battery with vmax -22 imax: -22` and `thermal thermal_zone2: Unable to get temperature, disabling!`. |

## Exact duplicates

These pairs are byte-identical; keep one of each when pruning.

| file | duplicate of |
|---|---|
| `oracle-live-2026-11-16-ring-and-cpu.txt` | session 61: the DEBUG_LL ring read *after* a QNX boot (both rings and the pbmon shadow are `0xAA` fill, no printable run in 64 KiB, while the IFS region keeps data); `pidin info` from the live oracle (`Processor1` **and** `Processor2`, both `Cortex A9 MPCore 1008MHz`); the user-space `mmap_device_memory` accessibility table for the SCU/WUGEN/SAR/DPLL/PRM ranges; the SMC probe (`SIGILL` for `0x102`–`0x105`); and the SMP kernel's own tail from the zero + silent-read-back capture (`SLUB … CPUs=2`, then the RCU geometry line).  Report 254 |
| `playbook-live-spare-2026-10-26-clean-mmc2-noidle-ring2.txt` | `…-clean-mmc2-noidle-ring.txt` |
| `docs/reports/data/live-info-2026-11-01/dsp_os_metrics.bin` | `…/installer_metrics.bin` (the DSP metrics read is a stub; both are zero) |
| `playbook-live-2026-10-21-after-0x2004.txt` | `playbook-live-2026-10-21-nv-0x2004.txt` |
| `playbook-live-2026-10-21-after-0x2040.txt` | `playbook-live-2026-10-21-nv-0x2040.txt` |
| `playbook-live-2026-10-21-after-0x2801.txt` | `playbook-live-2026-10-21-nv-0x2801.txt` |
| `playbook-live-2026-10-21-after-0x2802.txt` | `playbook-live-2026-10-21-nv-0x2802.txt` |

## Near-duplicates

These are distinct captures but differ only in one or a few bytes; keep the
pair only when a report needs a before/after comparison.

* `after-0x2074` vs `nv-0x2074`: 1 byte.
* The 13 `…-ring.txt` files of 648 bytes are distinct captures that differ by
  up to 4 bytes (the ring header/count).
* The 19 `…-ring.txt` files of 32,549 bytes are **distinct** captures from
  different experiments, not copies.
* The 8 `…-ring-*.txt` files of 6,080 bytes are the four breadcrumb rings from
  two sessions (distinct contents).

## What proves what

| capture group | used by |
|---|---|
| `playbook-live-2026-10-20-*` | boot ROM / IRAM mapping (reports 65, 152) |
| `playbook-live-2026-10-21-*` | NVRAM records `0x2002`/`0x2004`/`0x2040`/`0x2074`/`0x2801`/`0x2802` (reports 74–79) |
| `playbook-live-2026-10-25-*` | spare-unit RPMB/NVRAM read-only sweep and the `TEST_PAD` rehearsal (reports 92, 95) |
| `playbook-live-spare-2026-10-26-clean-*` | the clean-kernel boot captures, ring buffers and per-experiment event logs (reports 103–109) |
| `playbook-live-spare-2026-10-27-*` | live QNX display/register captures (reports 118, 121) |
| `twl6030-{linux,qnx}-2026-11-08.txt` | the both-OS TWL comparison (report 192) |
| `wlan-oracle-up-2026-11-14.txt` | the oracle's whole-session slog, including the reference's live WLAN bring-up (`sdioAdapt_ConnectBus: Send commands sequence: 0, 5, 3, 7` → firmware `Rev 7.1.4.10.77`); report 217 |
| `wlan-qnx-state-2026-11-14.txt` | stock TabletOS with `ti0` RUNNING: CORE/WKUP pads, GPIO1/2, `CM_PER`/`CM2`/`PRM`, TWL `0x80`–`0xB5`; report 218 |
| `wlan-linux-state-2026-11-14.txt` | the same ranges on our Linux boot, for the report-218 diff; report 225 reads its `CM2` domain modes |
| `ring-handoff-2026-11-16.txt` | the DEBUG_LL ring hand-off: the proof that `+0x1000` is the character array at #3840, the per-boot segmentation of one ring (3208/3943/4211 characters), the recovered `rcu2` log showing **`rcu_init()` completes**, the watchdog timing decode (kernel entry at 7.450 s of 7.812 s) and the first SMP boot to userspace; report 255 |

`docs/reports/data/` is indexed by the reports that produced it; the report
INDEX (`docs/reports/INDEX.md`) says which report is current for a topic.
