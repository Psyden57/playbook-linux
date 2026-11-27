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

## MUSB mailbox fault — DRAM ring with `PBMBX` markers (session 66)

| file | what |
|---|---|
| `musb-mailbox-fault-2026-11-19.txt` | decoded DEBUG_LL ring from two hung boots of the instrumented stock-mailbox kernel on dual-core (`0xF1AA4568`: entry marker only; `0x405E8997`: + per-branch markers), recovered via warm reset + the quiet read-back image.  Shows valid entry pointers (`status=4`, `otg->set_vbus = 0xc0729b94` = `omap_usb_set_vbus` text), `sv` still valid immediately before `otg_set_vbus()`, and the Oops at `PC = 0xc2c05800` (`kmalloc-1k` slab), `LR = omap_musb_set_mailbox+0x1e0`.  Some bytes are corrupted by the shared dual-core `printascii` ring (trap 332).  Report 264 §2; raw dumps under gitignored `local/work/r2-session66{,-m2}.bin` (sha256 in the capture header). |

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

## GPU client presentation (session 68)

`grim` captures of the catacomb compositor (1024x600) after the PRIME-only
`wl_drm` patch (catacomb `1.5.0-r10002`).  They are **PNG screencopies**, not
register dumps.

| file | what it shows |
|---|---|
| `session68-screen-black.png` | the compositor frame with **only its gesture bar**: the pvr dmabuf client surfaces (`epitaph` bar, `tabula` wallpaper) are missing — the presentation failure |
| `session68-screen-restored.png` | the safe state after `/etc/catacomb/initrc` unsets `MESA_LOADER_DRIVER_OVERRIDE` for the clients: wallpaper, status bar and keyboard display (clients on `wl_shm`, compositor still on the SGX) |

Report 269 §4e–§4f; traps 345/346.

## GPU buffer accounting and the pitch fix (session 70)

Text captures from the shell probes in `tools/bench/gpu/` (Python `ctypes`, so
they run on the unit without a compiler).  Each records the **kernel's** view
(`/sys/kernel/debug/dri/omapdrm.0/gem`), not a userspace guess.

| file | what it shows |
|---|---|
| `gpu-tiler-exhaustion-2026-11-24.txt` | the trap-349 mechanism: node map, the `-12 (3)` / `dma_buf_map_attachment` chain, the 10/10 threshold (59.4 MiB clean vs 112.3 MiB fail), and the `WARN_ON(pin_cnt > 0)`; report 270 |
| `gpu-8x-allocation-multiplier-2026-11-24.txt` | the 8× measured three ways — `libgbm` on `renderD128`, the DDK's own `PVRDRIBufferCreate`, and the faithful raw `CREATE_DUMB` baseline; report 270 §0.2 |
| `gpu-pitch-fix-2026-11-24.txt` | the root cause and the fix: the logged `CREATE_DUMB 1x<bytes>` arguments, the pitch-alignment effect, and the 1.00× result after re-expressing as `width=<bytes>, height=1`; report 271 |
| `gpu-pitch-fix-desktop-2026-11-24.txt` | the deployment: the wrapper/`.desktop` form, a clean boot with zero DSI errors, the idle 14.0 MiB tree, and 8/8 clean client runs; report 271 §5 |

## hd3 rebake: the first post-flash boot (session 71)

| file | what it shows |
|---|---|
| `rebake-2026-11-25-boot1-evidence.txt` | the first boot after flashing the rebaked hd3 image: the DSI wedge at boot+37 s (`dsi_if_enable to 0`, `NO PACKET_SENT`), the compositor alive and `dpms on` (so it is not a dead process), the healthy touch IRQ (118: 20,126 events), the GEM tree, and the clean second boot (0 DSI errors) |

Report 272 §4; `reboot -f` cleared it.  **Session 73 retired the "power-button
release → DPMS binding" explanation**: the OS registers **no** hardware buttons at
all, so those initrc bindings are dead code.  The wedge is the boot-time DSI fault
of trap 377 (`NO_PACKET_SENT` with `polls=2000`, where a healthy boot returns
`polls=1`).

## Kumo / WPE WebKit GBM export (session 72)

| file | what it shows |
|---|---|
| `kumo-gbm-export-2026-11-26.txt` | the live re-run of the GBM/PRIME probes (`gbmplane.py`, `gbmprime.c`, `gbmprime2.c`) on `renderD129`/`card1`/`card0`, and the two Kumo logs.  **The pvr node gives a rendering buffer `plane_count=1` but no fd and no GEM handle** (`gbm_bo_get_fd` = -1, `handle_for_plane` = `0xffffffff`, `drmPrimeHandleToFD` = -1), while omapdrm exports; `card1` is already DRM-master (`EBUSY`); there is no `CREATE_DUMB` (a real DRI image).  Plus `Failed to extract DMA-buf attributes from GBM buffer` and the `SGXKickSubmit`/`FreeResourceByPtr` lines |

This is the evidence behind report 275: WebKit's `AcceleratedSurface` needs a GBM
dmabuf and the closed pvr DDK cannot export the buffer it allocates.  The probes
are tracked at `tools/bench/gpu/`.  The probe output is reproducible; the `dmesg`
chain was measured live during the runs and is gone after the reboots (§E).


## SGX540 open-stack port (session 73)

Three captures from the first hardware work on the open `powervr_sgx` port:

| file | what it shows |
|---|---|
| `sgx540-identity-2026-11-26.txt` | read-only `/dev/mem` identity probe with `pvrsrvkm` bound: `CORE_ID` id **0x0114**, `CORE_REVISION` **0x00010200** (Rev 120), `DESIGNER_REV_FIELD1/2` 0, ti-sysc `rev` `0x40000000` / `sysc` `0x0000003c`, `BIF_INT_STAT` `0x00008000` (`FLUSH_COMPLETE`).  The read set is the guide's safe list only |
| `sgx540-bind-2026-11-26.txt` | the open driver on hardware: `SGX core id 0x01140000 revision 0x00010200: TI OMAP4430 (SGX540 1.2.0)`, `allow_unisolated` refusing then accepting the core, and the measured `pvrsrvkm` race (`56000000.gpu` owned by the closed DDK even with `CONFIG_SGX=n`) |
| `sgx540-ukernel-2026-11-26.txt` | the microkernel work: the 3-byte delta from the SGX545 image, the offline tests, the initramfs firmware requirement (probe at t≈0.9 s), the `modprobe.blacklist` failure, and the structural blocker — `not a microkernel file`, then the list-0 / `pdtable` requirement |

The probes are tracked at `tools/bench/gpu/sgxcoreid.py` (safe) and
`sgxusecodebase.py` (**DO NOT RUN** — trap 376).  The fact sheet
`docs/reference/sgx540-factsheet.md` carries the **C** facts; DECISIONS §109-§111
and traps 368-382 are the operational record for session 73.

## SGX540 open microkernel, first run (session 74)

Three captures from the session that got the microkernel running:

| file | what it shows |
|---|---|
| `sgx540-ukernel-list0-2026-11-26.txt` | **host only**: the one-context image (list 0 instead of 0xc38/0xc78, the `pdtable` slot resolution, `SINGLE_CONTEXT`/`SECURE`), `mkukfw --check` output, the 8/8 offline suites, the SGX545 golden hash still intact, and the frame-level A/B in the emulator (slot 1 → `0x0e500000`, slot 3 → `0x0e600000`, no bank write) |
| `sgx540-ukernel-start-2026-11-26.txt` | the first silicon run: `open microkernel …, features 0x8`, `EDM init: event status 0xa0000000, BIF int 0x8000`, **`microkernel started`**, the driver's own `status`/`ctlblk` dump, the `edm_init=1` requirement, and the corrected reading of trap 380 (the driver reads **hd3**, not the initramfs) |
| `sgx540-ukernel-first-job-2026-11-26.txt` | the NULL job: `1/1 passed` against a universal `consumed 0`, the pass/stall pattern per reset, the `FIX_HW_BRN_31620` experiment (tested, rejected, revert proven byte-identical), the corrected CCB trace and what it showed, and the new `/dev/mem` constraint |

Session **75** added one more, and it answers the question the three above left
open:

| file | what it shows |
|---|---|
| `sgx540-ukernel-kick-counter-2026-11-26.txt` | **HANDOFF-74 §0b, answered**: the `UK_KICKLOG` instrument (control-block words 0x3d/0x3e, read through the existing `ctlblk` node — no `/dev/mem`), its offline negative control (3 entries / 3 empty exits) and its one-flag revert, then the measurement — **0 in every sample of a whole job window**, `ccb wo 1 ro 0` on the kernel side against `0/0` on the microkernel side, and six jobs → six resets.  Also the mechanism for `1/1 passed` (the 2 s timeout signals `drm_sched`'s finished fence *without* an error, so `syncobj_wait` returns 0 at 2016806/2020499 µs), two corrected session-74 figures (`consumed` is a ring-reset artefact; the boot ran `ccb2`, not `invfix`), and the ssh-stream workaround for hd3 having no `scp` |

| `sgx540-timer-clock-2026-11-26.txt` | the reading that started it: the driver stores `HOSTCTL_UK_TIMER_CLOCK` = 307 200 (which a later capture shows to be **correct** — the 1 kHz period — so this row's "1000×" note is retracted), the whole microkernel control block showing `SC_VERSION` as its only written word, and the `edm_init` branch writing `ui32InitStatus` itself |
| `sgx540-dispatch-regs-2026-11-26.txt` | **the register read** (boot 3): seven read-only entries added to `pvrsgx_dump.c`, and the whole dispatch configuration — `EVENT_TIMER` `0x010228fe` **cycling inside its period**, `EVENT_KICKER` the driver's own word, `EVENT_PDS_EXEC/DATA/INFO` as written, and **`EVENT_PDS_ENABLE` reading `0x2204b00f` where the driver writes `0x2204f00f`** (bit 14 `SW_EVENT`).  Records the retraction of the "1000× period" claim — it compared a count against a rate |
| `sgx540-pds-enable-sweep-2026-11-26.txt` | **the per-bit sweep** (boot 4): a debugfs node that *writes* `EVENT_PDS_ENABLE` one bit at a time.  Every bit of `0x2204f00f` sticks except **14**, which returns 0 even when written alone — so `SW_EVENT` is **absent** from that register on this silicon, not masked, and session 74's `0x2204f00f` was a no-op.  It *is* implemented in `EVENT_HOST_ENABLE` |
| `sgx540-tick-counter-2026-11-26.txt` | **the measurement**: `UK_TICKLOG`'s dynamic counter (0x2c/0x2d), its emulator negative control (0 → 3 on three timer events, a doorbell is not a tick), and **0 after 12 s idle** on the unit with the GPU IRQ never fired — the EDM timer does not run at all |

`docs/reference/sgx540-ddk18-facts.md` carries the **K** facts from the
operator-supplied GPL-2 DDK 1.8 drop; traps 384-404 are the operational record
for sessions 74-76, and reports 277-279 are the reports.

`docs/reports/data/` is indexed by the reports that produced it; the report
INDEX (`docs/reports/INDEX.md`) says which report is current for a topic.

Session **76** added two, and the first is the one to read before quoting
anything the earlier captures say about *entry counts*:

| file | what it shows |
|---|---|
| `sgx540-vendor-ukernel-source-2026-11-26.txt` | **the vendor's microkernel as source**: the DDK 1.8 drop's `services4/srvinit/devices/sgx/eventhandler.pds.asm` — the program `EVENT_PDS_EXEC` points at, a **PDS dispatcher** that does `movs douta, ir0, INPUT_IR0_PA_DEST`, branches on the bits and `halt`s after `movs doutu, INPUT_GENERIC_DOUTU0/1/2` — plus the real entry table (`ukernel_labels.h`), the compiled 220-word program and its **host-patch word indices** (`eventhandler_pds.h`), and the same drop's **prebuilt SGX540 image**.  Key finding: `EURASIA_PDS_IR0_EDM_EVENT_TIMER` is **bit 1** there, versus b29 in the `EUR_CR_EVENT_STATUS` this port polls.  Also four retractions and the operator's ruling that the vendor image is reference, never a deployment target (DECISIONS §115) |
| `sgx540-entry-counter-2026-11-26.txt` | **the entry instrument, and the correction that matters**: the `UK_ENTRYLOG` counter, its placement and the two bugs in building it, then the measurement — and then the discovery that the counter had been sitting on control-block word **0x1c = `CLEANUP_DEFER_CT`**, which `init_set` clears, so **every "fetched exactly once" reading in sessions 75 and 76 is withdrawn**; on word 0x07 the program is fetched **twice** (`entry = 2`, `tick = 1`).  Records the `UK_REARM` re-emit rounds and that a correctly-placed, provably-firing PDS-task emit produces **no re-fetch**, the `init_set`-emit launch fault, an **evidence inventory** saying which of the session's numbers may be cited, and three instances of one unit bug (`* 4` on a word index) |

Read `sgx540-entry-counter-2026-11-26.txt`'s evidence inventory **before**
citing any dispatch number from sessions 74–76: the register readings in the
earlier captures stand, but their *entry counts* do not.

Session **77-78** added one, and it covers sixteen boots across two sessions —
the tick fix, the instrument corrections, and the dispatch fix:

| file | what it shows |
|---|---|
| `sgx540-kick-entry-counter-2026-11-27.txt` | **HANDOFF-77 §0b answered, then the fix — boots 1-16 in order.**  (1) The re-instrumented kick counter (ctlblk word **0x3f**, the `kick` entry's **first instruction**) reads 0 across eight job resets.  (2) The doorbell read-back's landing control.  (3) The dispatch-register dump (`DMS_AGE 0`, the zero PIXEL pair).  (4) **The tick counter fixed with a GPU-side seed** (0x100 → 0x101 at exactly word 0x2c — the GPU's initial view of the tail is not the CPU's zeros).  (5) The double-counting correction ("4 fetches" → 2).  (6) **The CACHE_CTRL bisect** (the vendor's own 0x0000ffff breaks dispatch).  (7) **THE FIX: `EVENT_PDS_INFO`'s USE_PIPELINE TOGGLE → PIPE_0** — and the live pipeline: `null job signalled in 946 µs`, `600 null jobs in 91 ms`, zero resets, `ccb issued 615 consumed 615`, `USE0_SERV_EVENT` count tracking the fetch count.  Read with traps 406-412 and DECISIONS §116 |

Session **79** added one, covering the first render campaign (boots 1-7):

| file | what it shows |
|---|---|
| `sgx540-first-render-2026-11-27.txt` | **The first render jobs through the open SGX540 microkernel.**  The render job type wired (the sgx545 command tables verbatim; 31/50, the null family intact); the first job consumed (`RO 0x2c0` of the context CCB, MADD latched); UK_WAITLOG naming each stuck wait site (`_wait8` the 0x138 invalidate, then `_wait15` the OTPM mem-clear); three SGX545-shaped µkernel bugs fixed (traps 413/414); the continuation — the vendor-binary write-set diff (the DPM PDS threshold 0x614, no mode selector), the extended status dump, the mid-hang TA/VDM/DPM state, the vendor `SGXMKIF_TAREGISTERS` field-for-field match, and the stall localized inside the VDM's control-stream execution.  Session 80 added three sections: the VDM stream's **record field layout** found in the vendor's own client builder and emitted for real renders (identical timeout — trap 416); the second continuation (`EVENT_STATUS2` settled as non-discriminating by disassembly, trap 417; the state-load programs decode clean; **`IDXFIFO`** wakes and latches, trap 419); and the bench at the close (region 0 CRC, region 1 hashed live, `BOOTCTR_FCTRY`, wlan0 up) plus the trap-421 evidence that the vendor image's `limm` sites are filled, not blank.  **Session 81** added five sections: the `UK_CCBTRACE` measurement (`ta_flags` = `0x71` on kick 1, `0x73` with `-k 1`; **LASTKICK is on kick 2**, so it is downstream of the stall — trap 420), the IDXFIFO gate test (a non-fixer, trap 419), the IRQ-147 storm and the DMM/TILER wedge (**intermittent**; traps 423/424), the static DDK reading (launch stub, `KickTA`, `USEPIPE_ALL`, the b26 MADD wait+ack), and the **VDM-stream bisection** (removing the STATE_LOAD records wakes the TE; the draw/index path is not the wall — trap 425, with the three field, segment and USE-program variants all excluded).  Read with traps 413-425, DECISIONS §117/§118/§120/§121, report 282 |
