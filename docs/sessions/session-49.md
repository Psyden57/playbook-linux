# Session 49 (absolute) — the WL1283 from both sides: the reference runs the chip, the host program is not the blocker, and the measurement itself was wrong

> Numbering: absolute `49` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 43** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-14.
Unit state at the end: **our Linux image `0xBAFD9B3B`** on region 0
(`0x93A384` B, kernel `7.3.3` **#57**, the session-48 tree with the report-220
GPIO1 pad fix), region 1 = the ssh rootfs **`a4e76b18…`** (unchanged,
67,108,864 B, 27 modules).  `0fca:8010`, console `pb> ` on `ttyACM0`, **ssh on
`169.254.0.1`**; `BOOTCTR_FCTRY` read `0x29` at the last boot
(`0x29 >= threshold 0x1e, no write`).  No NVRAM, MCT or RPMB write.  The oracle
was booted twice and left `/var/ti1283/{firmware.bin,tiwlan.ini}` on its `hd3`.

## Goal of this session

ROADMAP item 9's next piece: the **WL1283**, reopened by the operator at the
session-47 close, using the method that solved the speaker (DECISIONS §61) —
static RE of the reference stack in subagents, then **run the reference driver
on the minimal oracle with its own controls**, then implement in the driver/DTS.

## Confirmed (measured, read in a file, or seen on the bench)

* **The reference driver brings the WL1283 up on this unit, live.**
  `devnp-ti1283.so`, loaded as `/base/scripts/wifi.sh` loads it, logs
  `sdioAdapt_ConnectBus: Send commands sequence: 0, 5, 3, 7`, downloads its
  firmware (`Firmware running`, `Firmware Version: Rev 7.1.4.10.77`) and leaves
  `ti0` **UP,RUNNING**, MAC `14:74:11:8f:9c:c3`, `inet 192.168.0.212`.
  [capture `docs/captures/wlan-oracle-up-2026-11-14.txt`; report 217]
* **The reference's load path** — `wifi.sh` copies `/lib/firmware/ti1283` to
  `/var/ti1283`, renames the per-unit INI, then
  `mount -Tio-pkt -o nvram,firmdir=/var/ti1283/ /lib/dll/devnp-ti1283.so`.
  **`-o nvram` is the NVS** (468 bytes from NVRAM); without it the driver demands
  `nvs_map.bin`, which exists in **no** image.  Firmware loads **before** the bus
  connect, and `ti0` stays down with MAC `ff:ff:ff:ff:ff:ff` until
  `ifconfig ti0 up`.  [report 217 §1]
* **The BT half has its own enable.**  `BT_EN` = **gpio 55 = `GPIO2_23`** (pad
  `0x4A10007C` bits 31:16), driven low → 50 ms → high by **`btp-btmgr`** (devctl
  `0x80040501`), and held **low while the WLAN works**.
  [report 216; `docs/reference/qnx-hwio/gpio-table-winchester.csv`]
* **`hwio-config-winchester.so` decoded**: `gpio_config` = 72 × 32 B at file
  `0x1964`, `pad_config` = 248 × 20 B at `0x604`; the resolver arrays are at
  `0x5190`/`0x552C`/`0x58C8`.  [report 216 §5; `docs/reference/qnx-hwio/README.md`]
* **The stock-OS comparison is negative.**  Stock TabletOS with `ti0` up,
  captured (1 136 words: CORE/WKUP pads, GPIO1/2, `CM_PER`/`CM2`/`PRM`, TWL
  `0x80`–`0xB5`) and diffed against our Linux boot: **136 of 1 120 common words
  differ and none is WLAN-relevant** — pads `0x078`/`0x07A`/`0x07C` and
  `0x148`–`0x152` identical, WL_EN high in both, `PRM 0x4A307564` = 1 in both, no
  rail off on our side; the reference's extra GPIO1 outputs (0/4/18/28/31) and
  GPIO2 5/7 are driven **low**, the level our inputs read.
  [report 218; captures `wlan-qnx-state-2026-11-14.txt`, `wlan-linux-state-2026-11-14.txt`]
* **The boot path writes nothing that could gate the radio.**  `startup-playbook`
  = 549 MMIO stores, **no** PRM L3INIT/L4PER write (so `RM_L4PER_MMCSD5_CONTEXT`
  is never written by the boot path), and it **disables** MMC5's module clock
  (`CM_L4PER_MMCSD5_CLKCTRL` → MODULEMODE 0) at `full` setup.
  [report 223; `.script` transcribed in full, all 60 records run in a minimal image]
* **`pmic_twl6030_cfg` decoded** — 38 TWL writes, all execute on this board; it
  **does** enable VMMC (`0x98`–`0x9B` = `01/03/21/15` → 3.0 V group P1) and writes
  six GROUP registers.  **VMMC STATE reads `0x00` on TabletOS too** with `ti0` up
  (turned off again before `wifi.sh`).  Everything else it writes is already
  replicated by Linux by name and value (VAUX1/2/3 by the DTS, `E5`/`F5`/`FA` by
  `twl6030_usb_ldo_init()`; slave `0x38` is the LM3532 backlight).
  [reports 221, 226; live captures]
* **The clock domains differ**: with `ti0` up, `CM_L3INIT_CLKSTCTRL` and
  `CM_L4PER_CLKSTCTRL` read `…02` (**SW_WKUP**) on TabletOS against `…03`
  (**HW_AUTO**) here.  [report 218 captures; report 225]
* **That difference is not the gate either.**  `pm_runtime_forbid()` on the MMC5
  host made both domains `SW_WKUP` (`0x77302` / `0x4FFF02`) and
  `CM_L4PER_MMCSD5_CLKCTRL` `0x00000002` — matching the reference — and the card
  stayed silent.  [report 225 §1; image `0xD6A4A8FF`, kernel #52]
* **The GPIO1 pad defect was real and is fixed.**  `0x4A100050` and `0x4A100074`
  were at mode 0, so pads `gpmc_ad0/1` and `gpmc_ncs0/1` were actively driven on a
  board with no GPMC device, and gpiolib could not touch them (trap 21);
  `0x4A31E058` (GPIO1_31) was unmuxed with its input buffer off.  Now mode 3 +
  pull-down + `output-low` hogs: `GPIO1_OE` `0xFFFF1FBB` → **`0x6FFB1FBA`**
  (reference `0x6FFB1FAA`) and `GPIO1_DATAOUT` **byte-identical** to it.
  [report 220; image `0x0114FFD3`]
* **The "silent card" measurement was flawed and is corrected.**  With `PBX`
  (bus state at `start_command`) and `PBIRQ` (decoded `STAT`) restored: at the
  instant of `CMD52`/`CMD5` the bus is right (`SYSCTL=000e1e07` → `CEN=1`,
  `ICE=1`, `CLKD=120` = 400 kHz; `HCTL=00000b00` → `SDBP=1`, `SDVS=5` = 1.8 V;
  `CON=00000600`), `STAT=00018000` decodes as **`CTO` only** (`CCRC=CEB=CIE=0`),
  `RSP0=0`, `PSTATE=01f40001` (`CLEV=1`), while **`CMD0` succeeds** (`err=0`).
  [report 227; image `0xDEB38EFD`, kernel #54]
* **`CON = 0x85` must not be copied.**  Applied before the first command it
  reaches `CON=0x85 HCTL=0xb02 SYSCTL=0xe0087` and then **hangs** at `sdio_reset`
  with no error, because `CON[2] HR` makes the core generate a response instead
  of transmitting the command (TRM Table 24-75).  [report 227 §3a; image
  `0xE55B3A44`]
* **Leaving MMCHS5 in the reference's command-phase state wedges `reboot -f`**
  (two stuck `reboot` processes, console hung behind one, flasher never saw a
  bootrom); the clean image reboots normally.  `echo b > /proc/sysrq-trigger` is
  the recovery and became the flash routine's reboot.  [trap 224]
* **All experiments were reverted and verified exactly**, per file: the DTS is
  byte-identical to `kernel/source/omap4-winchester.dts` and `omap_hsmmc.c` /
  `core.c` are byte-identical to pristine 7.3.3 + the delivered patch.
* **The patch was re-exported at wrap-up, because the DTS changed** (the report
  220 pad fix is kept): **56 files / 8 583 lines**, sha256 `455b45d8…`, 0
  rejects, **56/56 byte-identical** by apply-and-compare.  The **tree walk**
  (trap 211) found two more things: a **stray blank line** my earlier revert left
  in `omap44xx-clocks.dtsi` (removed), and — the real find —
  **`arch/arm/Kconfig.debug` was missing from every export since session 30**
  even though the tree changes it (`DEBUG_PLAYBOOK_BC`).  It is now in the patch.
* **The GPIO-numbering audit trap 185 asked for is closed**: the WL1283 work
  never used a sysfs global GPIO number — every pin was a DT phandle or an
  absolute bank address.

## Inferred (reasoning from evidence, not directly measured)

* **The blocker is outside the MMC host and outside the readable board state.**
  Strong: three independent surfaces are negative (the driver's own writes, the
  live state of both OSes, the whole boot path's writes).  What remains is the
  unreadable MMCHS5 block and the chip-side pins.
* **The chip is probably powered** — moderately strong: the reference runs it with
  WL_EN high and BT_EN **low**, VIO/VBAT rails identical in both OSes, and the
  corrected measurement shows our bus powered and clocked at the command.  But it
  is **not proven**, because nothing readable reports the chip's own power state.
* **`CTO` at 400 kHz cannot distinguish "no card" from "card slower than
  160 µs"** — certain from the TRM (`CTO` = 64 card clocks), and the reason the
  settle-time and slow-clock experiments below exist.
* **The oracle cannot answer the remaining question**: it is the minimal image,
  its `/base` had no firmware directory, it needed the firmware staged by hand
  and `-o nvram` for the NVS, and no wifi service runs to bring `ti0` up.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`WL_IRQ` (GPIO2_21) = 1 means the chip is up."**  The reference's own chip
  reads **0** before its driver runs (`0x08000018`) and only goes to 1 after
  firmware.  Every "the chip is not up" claim since session 33 compared our
  pre-driver state with the reference's post-firmware state.  Killed by the
  oracle's own pre-driver capture (report 217 §2.1) plus report 227 §1.
* **"The reference's SDIO probe in the boot log means it enumerated."**  It does
  not: `TIWLAN SDIO probe WAS CALLED` → `SDIO clock … 24Mhz` → `Driver Created`
  → `LoadFiles` all precede the connect; the marker for a real connect is
  `sdioAdapt_ConnectBus: Send commands sequence: 0, 5, 3, 7`.
* **"The minimal image never ran `startup-playbook`."**  I dumped
  `0x48055000..0x3C`, which is the GPIO **interrupt** block, not
  `OE`/`DATAIN`/`DATAOUT` (`+0x134/+0x138/+0x13C`).  The pads were already
  `0x0118`/`0x0003`/`0x410B`; the board init had run.  (Trap 225.)
* **"`slay -f io-pkt-v6-hc` kills only my stray instance."**  It killed the
  `-ptcpip` stack carrying `rndis0` — door, qconn and ssh — with no software way
  back; it cost a power cycle.  Kill strays by pid.  (Trap 221.)
* **"No `Send commands sequence` in a filtered slog means no connect."**  The
  filter (`grep -iE "ti1283|sdio|wlan"`) dropped it; the unfiltered log had it.
* **"`-d ti1283,firmdir=…` is the io-pkt syntax."**  It is
  `-d driver [driver_options]`, whitespace-separated; commas make io-pkt look for
  `devn-ti1283,firmdir=….so`.  (Trap 222.)
* **"A reset for all (`SRA`) can go anywhere."**  It clears `CEN`/`ICE`; done
  after the clock setup the controller runs with **no card clock** (`SYSCTL = 0`)
  — indistinguishable from a silent card.  (Trap 223.)
* **"`CON = 0x85` is the reference's value, so copy it."**  Its `HR` bit
  suppresses the command entirely and hangs the rescan.  (Trap 236.)
* **"`wayback.signed` is the way back to stock TabletOS."**  It does not boot this
  armed unit (vendor `os_type` 4 IFS → factory/CRC path halts, red blink) and its
  RCFS has no `/devuser/startup.sh` hook, so no root.  (Trap 226.)
* **"`memw32 <addr> 410B FFFF0000`"** writes zero — the value must be pre-masked
  (`410B0000`).  It clobbered pad `0x07A` and had to be restored.
* **"`&omap4_pmx_wkup` pad offsets are the same as `&omap4_pmx_core`."**  They
  are relative to `0x4A31E040` and the block is only `0x38` long; `0x4A31E058` is
  offset `0x18`.  (Trap 230.)
* **"A pinctrl group can re-mux a pad another group owns."**  It cannot; the whole
  group is dropped with `pin PIN36 already requested`.  (Trap 229.)
* **"The kernel patch needs no checking after an experiment."**  Reverting by hand
  is error-prone; only the byte-comparison against pristine + patch proved it.
* **"One broad subagent can do a boot-path RE."**  It hit the per-agent turn cap;
  one binary per agent is the workable size (`maxTurns` is per-agent front
  matter, and a custom agent's first tool call came back malformed — plain new
  chats with the prompts worked).

## Unknown / open questions

* **Is the chip's digital core running at all?**  Settle it with the **BT-core
  HCI probe done properly**: drive `BT_EN` (gpio 55) low → 50 ms → high, then
  `HCI_Reset` (`01 03 0c 00`) on `/dev/ttyS1` (UART2 `0x4806c000`, irq 103, pads
  muxed).  Session 29's probe ran **without** `BT_EN` (report 180 §6 said so) and
  is therefore invalid.  Answer either way is decisive: a reply proves the die is
  powered and its core runs; silence puts the fault in power/TCXO/reset.
* **Is the card merely slower than 160 µs?**  Settle it by lowering the
  identification clock: `CTO` is a fixed **64 card clocks**, so 100 kHz gives
  640 µs (4× the window) versus 400 kHz's 160 µs.  `f_min` is
  `OMAP_MMC_MIN_CLOCK` (400000) in `omap_hsmmc.c`.
* **What exactly does the reference's `sdioDrv_ExecuteCmd` sequence do on this
  silicon?**  Settle it with the QNX-in-Linux harness (below) — it removes every
  reconstruction error at once, which now matters because `CON=0x85` proved the
  reconciliation can be subtly wrong.
* **The chip-side pins** (VBAT, 26 MHz TCXO, SLOW_CLK, RESET_N) — unmeasurable on
  this bench (trap 165).
* **Which firmware the part needs**: the reference uses
  `/base/lib/firmware/ti1283/firmware.bin` Rev 7.1.4.10.77, mainline expects
  `wl128x-fw-5-sr.bin` + `wl128x-nvs.bin`.  Post-enumeration only.
* **The WL1283C revision** — the board's part is a WL1283C.  Enumeration precedes
  any driver, so `wlcore`'s version cannot affect `CMD5`; whether the *silicon*
  revision needs different sequencing is unknown and would need a datasheet or a
  known-good unit.

## Superseded / uncertain

* **"The reference sets `CM_L4PER_MMCSD5_CLKCTRL = 2` at boot, and our boot
  matches."**  Superseded by report 223: `startup-playbook` sets it to 2 and
  `setup-core-inactive full` then **disables** it; the WLAN driver re-enables it.
  Our boot-time `= 2` was never the reference's value (it is equivalent, but for
  a different reason).
* **"A full TabletOS boot asserts power rail X that the minimal oracle skips."**
  Superseded by the `.script` transcript (report 223 §B6): **all 60 IFS records
  run in a minimal image**; the pruning point is `/base/scripts/startup.sh`, not
  the script.  And by report 221/226: the only writer Linux lacks
  (`pmic_twl6030_cfg`) is refuted by the live captures.
* **"The session-36 verdict stands: the software surface is exhausted."**
  Withdrawn twice: by session 47 (report 215) and again here — the measurement
  flaw (report 227) means several "negatives" from sessions 33–37 were not
  measurements at all.
* **Anything measuring "the chip is not up" by `GPIO2_DATAIN` bit 21** — see the
  first dead end.  Reports 196, 217, 218 and 220 all lean on it; their *verdicts*
  survive (the card does not respond to `CMD5`), but the evidence quoted does not.

## Bench state inherited by session 50

| item | value |
|---|---|
| region 0 | **`0xBAFD9B3B`**, `0x93A384` B, kernel `7.3.3` **#57** — the session-48 tree + report-220's GPIO1 pad fix |
| region 1 | **`a4e76b18…`**, 67,108,864 B, 27 modules — unchanged this session |
| OS running | **our Linux image** (`0fca:8010`); console `pb> `, ssh on `169.254.0.1` |
| `BOOTCTR_FCTRY` | `0x29` at the last boot (`>= threshold 0x1e, no write`); self-replenishing |
| device-side files | oracle's `hd3`: `/var/ti1283/{firmware.bin,tiwlan.ini}` (report 217 §2.3); Linux side: none |
| region-0 RCFS | **the full 1917 RCFS** (`local/work/rcfs-1917.rcfs`), not the pruned oracle one (trap 227) |
| way back to TabletOS | `ifs-shim-sealed.ifs` at `0x825` **+** `rcfs-1917.rcfs` at `0x8D5` (NOT `wayback.signed`, trap 226) |
| reboot note | `reboot -f` works on the clean image; `sysrq b` if a wedged image hangs it |
| no restore needed | the unit is left on our Linux image |

## Artefacts produced

* **Reports** `docs/reports/`: **216** (the BT half), **217** (the oracle
  bring-up and the two replays), **218** (the stock-OS diff), **219** (boot-path
  writers, first pass), **220** (the GPIO1/pad audit), **221** (regulator
  resmgr), **222** (MMC block driver), **223** (`startup-playbook` + `.script`),
  **224** (powerman/devpm), **225** (the clock-domain difference, tested
  negative), **226** (boot-writers verified), **227** (the measurement flaw).
* **Prompts** for the one-binary RE passes: `docs/reports/data/re-prompts-2026-11-14.md`.
* **Captures** `docs/captures/`: `wlan-oracle-up-2026-11-14.txt`,
  `wlan-qnx-state-2026-11-14.txt`, `wlan-linux-state-2026-11-14.txt`.
* **Reference tables**: `docs/reference/qnx-hwio/gpio-table-winchester.csv` +
  README corrections.
* **Instrumentation archived**: `kernel/archive/wl1283/measurement-instrumentation-2026-11-14.patch`
  (`PBX`/`PBIRQ`/`PBCMD`, 66 lines) — the fix for the measurement flaw.
* **DTS change (kept)**: `omap4-winchester.dts` — GPIO1 pads 0x050/0x074 to mux 3
  with pull-down, `gpio1-ref-low-{0,18,28,31}` hogs, `gpio1-wk31-pins` in
  `&omap4_pmx_wkup`.  Mirrored to `kernel/source/`.
* **Images flashed and retired**: `0xE1B40EB4` (#46, `SRA` after the clock),
  `0x04473A45` (#47, the corrected replay), `0x4280487F` (#48, first pad attempt),
  `0x0114FFD3` (#51, the pad fix), `0xD6A4A8FF` (#52, runtime-PM domain test),
  `0x7E211599`/`0xDEB38EFD` (#53/#54, the measurement), `0xE55B3A44` (#56, the
  command phase that hung).  Unit left on `0xBAFD9B3B` (#57).
* **Docs**: traps 219–233, `STATE.md`, `HANDOFF.md`, `ROADMAP.md` item 9,
  `DECISIONS.md` §63, `reports/INDEX.md`, `sessions/NUMBERING.md`, this note.
