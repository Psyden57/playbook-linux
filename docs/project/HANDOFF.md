# Handoff — session 39 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 39 = Linux-era 33**.  See
`docs/sessions/NUMBERING.md`.

> **Session-38 result, in one line:** the **touch is finished** — the report is
> mapped through the 1917 `scaling.conf` calibration to 1024×600 and the
> **GPIO2_3 edge IRQ** works (`irq 110`), both proven on image `0x82FC0D35`
> (report 198); the **ABE segment is enabled** with `WD_TIMER3` off (report
> 199), the WM8994 codec answers `0x8994`, and the **machine link is up**:
> `0 [WM8994]: simple-card - PlayBook WM8994`, with the sound stack loaded as
> modules from `rcS` after the USB gadget (the built-in stack delayed the gadget
> and broke self-enumeration — trap 149).  The base patch is 48 files / 7 530
> lines (sha256 `9089ef49…`).  The unit is **on `0x3B2ECFED`** (region 0) +
> rootfs `07d4e28b…` (region 1).

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain, the 64 MiB ext4 BusyBox root in region
1, and the peripherals: I²C4 motion sensors (BMA150/HMC5883L/MPU3050), TMP103,
BQ27541 gauge, FAN5702 LED, CM3217 light sensor, ISL9519 charger, and the
**Munin touch (calibrated, IRQ wired — session 38)**.  WDT2 runs the
reference's 58.6 s window with a 10 s pet.

**The patch.** `kernel/patches/0001-playbook-winchester-7.3.3.patch` is
**7 426 lines / 46 files on pristine linux-7.3.3**, sha256 `3b44da6c…`,
**0 rejects and 46/46 files byte-identical** (regenerate with the tree walk in
`kernel/patches/README.md`; clean the tree first — trap 139).  Config
`kernel/winchester-7.3.3.config` (`CONFIG_CHARGER_ISL9519=y`,
`CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`, `CONFIG_KERNEL_XZ=y`).
**The WL1283 code is NOT in it** — it is in
`kernel/archive/wl1283/wl1283-instrumentation-7.3.3.patch` (517 lines, applies
to the clean tree with `patch -p1`; round-trip verified).
**The 6.15.11 backup is intact** (`local/pristine/linux-6.15.11`,
`local/kernel-tree-6.15.11`, `kernel/patches/0001-playbook-winchester-6.15.11.patch`);
do not delete it; its patch is incomplete (trap 106).

**The board DTS** (`kernel/source/omap4-winchester.dts` ↔
`local/kernel-tree/.../omap4-winchester.dts`, keep identical) carries the
re-derived minimal 1917-truth `&mmc5` (report 195 §3.4; enabled but negative
for CMD5) and the session-38 touch wiring: `touch_irq_pins`
(`OMAP4_IOPAD(0x056, 0x410b)`), `interrupt-parent = <&gpio2>`,
`interrupts = <3 IRQ_TYPE_EDGE_FALLING>`, and
`rim,x-calibration`/`rim,y-calibration` + `touchscreen-size-x/y` on
`touchscreen@65`.

## 0a. The media, and the way back

* **region 0 = our Linux image `0x3B2ECFED`** (`0x939780` B, kernel `7.3.3`;
  `boot/chainloader/kernel.img`) — touch + ABE + the audio machine link.
* **region 1 = the audio rootfs** (`07d4e28b…`, 24 modules,
  `local/images/rootfs-region1-audio.ext4`) — the sensor modules plus the
  `wm8994`/codec/McBSP/simple-card set the `rcS` loads after the gadget.
  The unit is **on Linux** (`0fca:8010`, console `pb> `), not TabletOS.  The
  previous images rebuild from `12d20b3` (`0x50842767`) and `7ced600`
  (`0x82FC0D35`); the saved fallbacks are `0xE36C679A` and `0x04AF1433`.
* **The way back to TabletOS** is the 1917 IFS
  (`local/work/ifs-shim-sealed.ifs`, sha256 `b52195bb…`, 489 frames), flashed
  at block `0x825`, region 0 only:
  ```bash
  nohup python3 tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs \
      --dest-block 0x825 --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
  # then reboot the running Linux image:
  tools/pbcon.py 'reboot -f'
  ```
  **Do not restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).
* The 1917 RCFS at region-0 `0xC00000` and the 1917 UFS are on media, untouched.
* Earlier Linux images (region 0 only): the Task-3 `0x0DC36A11`, `0x05819994`,
  the known-good `0xE36C679A`, and the touch `0x04AF1433`.

## 0b. Item 9 step 4 (WL1283) — software surface exhausted AGAIN

**Not invalidated: reports 180–197 and traps 116–146 stand.**  The 1917-truth
DTS redo (session 37) is **negative**: the minimal `&mmc5` host probes but
`attach_sdio no` (report 196), the DT model is provably applied and
1917-identical, WL_EN is high, and `GPIO2_DATAIN` bit 21 (WL_IRQ) is 0 — the
chip is not up under Linux.  The remaining evidence is **chip-side
(VBAT / 26 MHz TCXO / SLOW_CLK) or OMAP HSMMC sampling**, which needs a board
view or logic analyser.  Do not re-run the closed experiments (traps 116–143)
without stating what is new.

* **Mainline binding (report 195 §3.6).**  `ti,wl1283` requires `interrupts`
  **and** `ref-clock-frequency`; the SDIO form is a `wlcore@2` child of
  `&mmc5` with `ref-clock-frequency`/`tcxo-clock-frequency` = 26 MHz and a
  WL_IRQ `interrupts` entry (gpio 53 → `&gpio2 21`).  Our `&mmc5` has no
  `wlcore` child yet — add it once the bus enumerates; it cannot fix CMD5.
* **The TQP6M9002 FEM (report 195 §3.7).**  The one cheap chip-side check is
  the FEM VCC (and WL1283 VBAT) on live QNX-with-WiFi vs Linux: a rail off
  under Linux would point at a shared board rail the TWL comparison missed.

## 0c. Item 9 step 5 — touch DONE, audio NOT started

**Touch — DONE (session 38, report 198).**  I²C2 `0x48072000` = `i2c-1` at
`0x65`; `drivers/input/touchscreen/playbook-munin.c` boots the Munin app,
maps the 12-bit report through the 1917 `scaling.conf` calibration
(raw x 172→1108 → display 102→922, raw y 110→658 → display 60→540, clamped to
1024×600), and requests the GPIO2_3 IRQ (pad `0x056`, `0x410b`,
`IRQ_TYPE_EDGE_FALLING`).  On `0x82FC0D35`: `Munin touchscreen ready (irq 110)`
at 3.453 s; `/proc/interrupts` has `110: … 48055000.gpio 3 Edge 1-0065`;
`devmem 0x4A100056` = `0x410B`.  The ~330–380/s rate with no finger is the
controller's active report rate (unbinding the driver freezes it; trap 146).
Remaining niceties: drive power/reset (GPIO1_14/GPIO4_8) and coalesce the
report rate.

**WM8994 audio — STARTED (session 38, report 199).**  The codec answers:
`i2ctransfer -y 3 w2@0x1a 0x00 0x00 r2` → **`0x89 0x94`** on I²C4
`0x48350000` (Linux `i2c-3`), 7-bit `0x1a`, 16-bit registers.  But there is
**no mainline OMAP4+WM8994 machine driver**, and `SND_SOC_WM8994` is a **hidden
Kconfig symbol** (no prompt) that a machine driver must `select`.  The 1917
stack is `deva-ctrl-omap4abe-wm8994` (ABE + McBSP), started as
`io-audio -d omap4abe-wm8994 aif=0,secure_capture`.  The McBSPs are in
`l4_abe`, which the board disables because **WD_TIMER3 (0x40130000) hangs
ti-sysc**; the WD target has no label, so:
1. label `target-module@30000` in `omap4-l4-abe.dtsi` as `wdt3_target`;
2. board DTS: `&l4_abe { status = "okay"; }` + `&wdt3_target { status = "disabled"; }`;
3. verify the segment and McBSP targets probe **without a hang** (one variable
   per boot; the loader's WDT2 is 7.8 s until the late reprogram, so a
   deterministic hang can reset-loop — keep `0x82FC0D35`/`0xE36C679A` ready);
4. then `CONFIG_MFD_WM8994` + a board Kconfig selecting `SND_SOC_WM8994`
   (+ `SND_SOC_OMAP_MCBSP`), the codec node (seven supplies:
   `DBVDD/DCVDD/AVDD1/AVDD2/CPVDD/SPKVDD1/SPKVDD2`; provisional
   `v1v8`/`v2v1`), and the DAI link (`simple-audio-card` or a small machine
   driver; codec `wm8994-aif1`, CPU `omap-mcbsp.<n>`).  See report 199.

**Executed — the ABE segment (image `0x50842767`).**  With `wdt3_target` labelled
and `&l4_abe { status = "okay"; }` + `&wdt3_target { status = "disabled"; }`,
the boot log shows `4012208c/4012408c/4012608c` (McBSP1/2/3) probing `pm ok`;
`40130000` (WD_TIMER3) is skipped, and the unit reaches a shell (uptime 37 s,
`BOOTCTR_FCTRY = 0x30`, no write).  No hang, no reset loop.

**Executed — the machine link (image `0x3B2ECFED` + rootfs `07d4e28b…`).**
Board DTS: `wm8994@1a` on `&i2c4` (seven supplies, provisional
`v1v8`/`v2v1`), `mcbsp2_pins` (epson pads `0x0f6`–`0x0fc`), `&mcbsp2` with
`#sound-dai-cells`, and a `simple-audio-card` with the CPU (McBSP) as bit/frame
master.  The card is a **module** (`SND_SOC_PLAYBOOK_WM8994` selects
`SND_SOC_WM8994`/`SND_SOC_OMAP_MCBSP`/`SND_SIMPLE_CARD`), loaded from `rcS`
after the gadget.  Result: `0 [WM8994]: simple-card - PlayBook WM8994`,
`/proc/asound/pcm` = `40124000.mcbsp-wm8994-aif1 … playback 1 : capture 1`, and
the unit enumerates by itself.  **Do not build it in** (trap 149).  The
jack-detect IRQs are unwired (the `Mic detect IRQ: -22` lines are benign).

### The next experiment — one WM8994 playback attempt (untested)

**Inputs.**  The unit on `0x3B2ECFED` + rootfs `07d4e28b…` (already on media),
and one playback path.  The rootfs has no `aplay`; the two candidates are:

1. **OSS emulation (smallest):** add `snd-pcm-oss.ko` (and `snd-mixer-oss.ko`
   if the build wants it) to `local/work/modules-7.3.3`, add `snd-pcm-oss` to
   the `rcS` audio loop, rebuild the rootfs, flash region 1, then
   `modprobe snd-pcm-oss` and `dd if=/dev/zero of=/dev/dsp bs=4800 count=10`.
2. **A tiny ALSA ioctl player** (`/dev/snd/pcmC0D0p`, S16_LE, 2ch, 48 kHz,
   `HW_PARAMS`/`SW_PARAMS`/`PREPARE`/`WRITE`/`START`), built static and copied
   in.  More code, but it controls the format and rate.

Neither has been run.  The **write is what calls `hw_params`**; a silent
`/dev/zero` write is enough to exercise the DAI, no audible tone required for
the first pass.

**Decision table.**

| result | meaning | next |
|---|---|---|
| write completes, `dmesg` has no `hw_params`/`-EINVAL` | the DAI and PCM configure | add the `simple-audio-card,widgets`/`routing`, then a real tone and the SPKVDD check |
| `open`/`hw_params` fails `-EINVAL` or "No MCLK" | the codec sysclk/FLL or the DAI format/master flags are wrong | inspect `wm8994_set_dai_sysclk`, the simple-card master phandles; add the codec `clocks`/MCLK and the FLL source |
| write completes but no sound | DAPM routing or SPKVDD | add the widgets/routing; identify the speaker rail |
| open fails `-ENODEV` / no `/dev/dsp` | the OSS module or the PCM is absent | check `/proc/asound/pcm` and the module order |

## 0d. The WLAN calibration partition

QNX `hd5` (512 KiB) is the factory/calibration partition: unit serials +
2011-04 factory timestamps, a 19 118-byte TI WL128x radio-parameters INI
(TxPower, XTAL trim, TX BiP reference, FEM/trace insertion loss, 2.4/5 GHz
TXPD tables) and a 44 405-byte binary blob; the assigned WLAN MAC is in NVRAM
`deviceinfo` (`14:74:11:8f:9c:c3`).  Extracted read-only to
`local/work/1917/calib/` (report 197).  It is the source for a mainline
`wl128x-nvs.bin` **after** the SDIO function enumerates; it cannot fix CMD5.

## 1. Useful local extracts

* **`local/work/1917/base/`** — the whole 1917 `/base` (333 MB), copied out of
  the mounted `rcfs-1917.rcfs`.  No remount needed for 1917 `/base` reads.
* **`local/work/1917/ifs/`** — the 1917 IFS tree + `proc/boot/startup-playbook`.
* **`local/work/1917/calib/`**, **`local/work/1917/parts/`** — the WLAN
  calibration and the pulled partitions (report 197).
* **`local/vendor/bb10mt/bb10mt`** — the QNX6/container tool; use it for
  `qnx6 mount/compact/mkfs/fsck` and `rcfs`.
* Reports **176**, **193–198** (198 is this session's touch calibration + IRQ).

## 2. Build and flash (7.3.3)

```bash
REPO=$PWD; . tools/env.sh
K=local/kernel-tree
arm-linux-gcc -static -O2 -Wall -o initramfs/root/console initramfs/console.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/init    initramfs/init.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/pbrpmb  initramfs/rpmb.c
rm -f $K/usr/initramfs_data.cpio*
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 modules
ZIMAGE=$K/arch/arm/boot/zImage DTB=$K/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb \
    python3 tools/build_kernel_image.py     # prints CRC; image must be < 0xB00000
```

The printed CRC must differ from the previous build (trap 39).  Flash region 0
only when only it changed; both regions in one bootrom session only when both
did.  Start the flasher immediately before the reboot; the bootrom window is
60 s; `tools/bbusb.py` resets the loader itself after the write.

## 3. Bench

* **The unit is on our Linux image** (`0fca:8010`).  There is **no door**:
  use `tools/pbcon.py` (prompt `pb> `), reboot with `tools/pbcon.py 'reboot -f'`.
  `pkexec chmod 666 /dev/ttyACM0` after EVERY boot (operator answers; never
  retry a failed pkexec).  Run USB/flash tools as the **user**, never pkexec.
* **On TabletOS** the door is `169.254.0.1`; host link `enp8s0f3u1` carries
  `169.254.0.2/16` (do not re-add).  Start `setsid tools/door-hold.sh` (never
  `pgrep -af door-hold` / `pkill -f door-hold`).  **`tools/pbssh.sh` logs in as
  `root` (`uid:0`)** — but only after the door-hold has uploaded the key; a
  cold boot's first attempts get `Permission denied` until it connects (trap
  145).  After a cold TabletOS boot the qconn handshake can take 1–2 minutes.
* Device password `0000` (sent once, never retried).  An agent **never writes
  RPMB**; NVRAM is read-only.  **A test boot is not a budget concern** — the
  Linux `/init` tops `BOOTCTR_FCTRY` up at ≤`0x1e`, and TabletOS writes `0x32`;
  read the counter as a health check.
* Reads via `plink`-style impossible; use the tools.  The 1917 QNX tools that
  run on the device must be `-static` (trap 131).

## 4. Traps added this session

* **144.** A 64-bit divide in kernel C on 32-bit ARM needs `div_s64()`, not
  `/` — otherwise `undefined reference to __aeabi_ldivmod`.
* **145.** Root SSH on 2.1.0.1917 works, but only after the door-hold has
  uploaded the key; `devuser` succeeding is not evidence that root is disabled.
* **146.** The touch IRQ fires at the controller's report rate (~330–380/s)
  with no finger; unbinding the driver freezes the count, rebinding resumes it.
  It is not an electrical fault; the 20 ms poll is the fallback.
* **147.** `SND_SOC_WM8994` has no Kconfig prompt; `scripts/config -e` writes
  it but `olddefconfig` drops it.  A machine driver must `select` it (the board
  entry in `sound/soc/ti/Kconfig` or a small machine driver).
* **148.** Enabling `&l4_abe` hangs on the WD_TIMER3 target (0x40130000) in
  ti-sysc; the target has no label.  Label it in `omap4-l4-abe.dtsi` and keep
  it `status = "disabled"` while enabling the segment (report 199).
* **149.** Do not build the ASoC card/codec/McBSP into the kernel: the card
  registration delays the USB gadget (~1.3 s) and breaks self-enumeration.
  Build them as modules and load from `rcS` after the gadget; `snd-soc-simple-card`
  also needs `snd-soc-simple-card-utils.ko` (report 199).

The full trap list (9–149) is `KNOWN-ISSUES.md` §2.

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| archived WL1283 code | `kernel/archive/wl1283/` |
| 1917 `/base`, IFS, calibration | `local/work/1917/{base,ifs,calib,parts}/` |
| touch driver | `local/kernel-tree/drivers/input/touchscreen/playbook-munin.c` |
| charger driver | `local/kernel-tree/drivers/power/supply/isl9519.c` |
| QNX hwinfo dumper | `tools/qnx-hwi.c`; I²C `tools/pbi2c` |
| reports | `docs/reports/176…198` (198 is this session) |
| sessions | `docs/sessions/session-38.md` (this session) |

## 6. Working rules

* After every console command, dump the log tail and read it.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  The whole-map diff is the highest-yield technique.
* **A test boot is not a budget concern.**  The Linux image's `/init` tops
  `BOOTCTR_FCTRY` up automatically (`pbrpmb topup 0x1E`), and TabletOS tops it
  back to `0x32`; read the counter as a health check, not as something to
  conserve.  (The hard hazards in `SAFETY.md` §2 are unchanged.)
* **Never present a guess as a fact.**  External facts are looked up and cited
  (trap 105); device facts prefer a fresh read over an old note.
* **OS 2.x only** for PlayBook hardware facts (trap 110, DECISIONS §34).  Ask
  the reference OS what it maps before writing a DT node (DECISIONS 30).
* Subagents: static/file-only, never a `tools/` script (trap 109).
* **Restore the sealed IFS at the end of every session** unless the operator
  ends on our image.  Session 38 ended on **Linux `0x50842767`** (region 0
  only); reflashing the 1917 IFS at block `0x825` returns to TabletOS.

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 39 (absolute; Linux-era 33)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), KERNEL-PORT.md §7, ROADMAP.md item 9 (steps 4 and 5), KNOWN-ISSUES.md
(traps 9-149, especially 105-149, and §3 corrections 3.22), DEVELOPMENT.md
§3/3a/4/7a, DECISIONS.md (17-48), reports 176, 180-199, sessions/session-37.md
and session-38.md.

WHERE IT STANDS: the touch is FINISHED — the Munin report is mapped through the
1917 scaling.conf calibration to 1024x600 and the GPIO2_3 edge IRQ works (pad
0x056 = 0x410b, IRQ_TYPE_EDGE_FALLING, irq 110); report 198.  The WM8994 AUDIO
CARD IS UP: the codec answers 0x8994, the ABE segment is enabled with the
WD_TIMER3 target off, McBSP2 is enabled, and the card is
`0 [WM8994]: simple-card - PlayBook WM8994` (report 199).  The sound stack is
MODULES loaded from rcS after the USB gadget (building it in broke
self-enumeration — trap 149).  The base patch is 7 530 lines / 48 files (sha256
9089ef49...) and reproduces the tree byte-for-byte.  The WL1283 code is
archived; the 1917-truth minimal &mmc5 probes but FAILS CMD5 (report 196) — the
chip is not up under Linux, and the remaining evidence is chip-side (needs a
board view/logic analyser).  The hd5 WLAN calibration is extracted to
local/work/1917/calib/.

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE 0x3B2ECFED (kernel 7.3.3; touch
+ ABE + audio machine link); region 1 is the audio rootfs 07d4e28b... (24
modules); BOOTCTR_FCTRY = 0x2d at the last boot (pbrpmb topup saw 0x2d >= 0x1e,
no write; the counter is self-replenishing).  The way back to TabletOS is the
1917 IFS (local/work/ifs-shim-sealed.ifs) at block 0x825.  Do NOT restore
local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

WHERE IT IS: item 9 step 5's touch half is DONE and the audio card is UP.  The
remaining audio work is PLAYBACK: the codec's MCLK/FLL and the DAI clocking are
not yet verified (the jack-detect IRQs are unwired; the four `Mic detect IRQ:
-22` lines are benign).  Item 9 step 4 (WL1283) is software-exhausted; do NOT
repeat traps 116-149 without stating what differs.  The WLAN calibration
partition is the asset for the post-enumeration wl12xx path.

BENCH: the unit is on OUR LINUX IMAGE — there is NO door.  Use tools/pbcon.py
(prompt `pb> `), reboot with tools/pbcon.py 'reboot -f'; pkexec chmod 666
/dev/ttyACM0 after EVERY boot (operator answers; never retry a failed pkexec).
On TabletOS: door = 169.254.0.1 (host link 169.254.0.2/16 on enp8s0f3u1 — do
not re-add); start tools/door-hold.sh with setsid (never pgrep/pkill -f);
tools/pbssh.sh logs in as ROOT (uid:0) once the door-hold has uploaded the key
(trap 145); after a cold boot the qconn handshake can take 1-2 minutes.  Run
USB/flash tools as the USER, never pkexec.  Device password 0000, once.  An
agent NEVER writes RPMB; NVRAM is read-only (SAFETY §1/§6).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4 (open_binary,
binary_overview, analyze_function, procedure_pseudo_code, search_strings,
xrefs, read_bytes); raw ARM import format=raw-binary, architecture=arm,
entry_point=0x80100000.  Reconcile with ntoarmv7-readelf/objdump (Ghidra may
add +0x10000 on an ELF).  QNX device tools build -static.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
A test boot is not a budget concern (the counter self-replenishes), but the
hard hazards (0 / >0x32 / failed write / flat pack) remain.  Operator's call
when to stop and to wrap up.

NEXT (operator's call): (a) the WM8994 PLAYBACK test (the card is up; the codec
MCLK/FLL and DAI clocking are next); (b) the WL1283 chip-side measurements
(needs a board view/logic analyser); (c) build wl128x-nvs.bin from hd5 and add
the `wlcore@2` child when the bus works; (d) touch niceties (drive power/reset,
coalesce the report rate).  The unit is on Linux; region 0/region 1 both change
when the kernel/rootfs pair does, otherwise flash only the changed region; the
1917 IFS at block 0x825 is the way back to TabletOS.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.
```
