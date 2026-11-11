# Handoff — session 40 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 40 = Linux-era 34**.  See
`docs/sessions/NUMBERING.md`.

> **Session-39 result, in one line:** the **WM8994 playback path is built** — a
> board machine driver (not simple-audio-card) registers
> `0 [WM8994]: PlayBook_WM8994`, an OSS write reaches the PCM at 48 kHz, and
> the DAPM path powers (`PM4=0x0300`, `PM5=0x0303`, `AIF1CLK_ENA=1`,
> `DAC1L`/`SPKL`/`HPOUT1L`/`SPKOUTLP` `On`) — but the **codec clock is blocked
> chip-side**: FLL1 never locks from MCLK1 (the pin does not clock on this
> unit; it locks from **BCLK**), and the chip stops accepting register writes
> after boot, so the driver's FLL configuration stays in the regmap cache.
> No audible output yet (report 200).  The base patch is **53 files / 7 773
> lines** (sha256 `434d06c3…`), 0 rejects / 53/53 byte-identical.  The unit is
> on **`0xF97CD9C7`** (region 0) + rootfs `968d2f20…` (region 1).

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain, the 64 MiB ext4 BusyBox root in region
1, and the peripherals: I²C4 motion sensors (BMA150/HMC5883L/MPU3050), TMP103,
BQ27541 gauge, FAN5702 LED, CM3217 light sensor, ISL9519 charger, the **Munin
touch (calibrated, IRQ wired — session 38)**, and the **WM8994 card + DAI
stream (session 39)**.  WDT2 runs the reference's 58.6 s window with a 10 s pet.

**The patch.** `kernel/patches/0001-playbook-winchester-7.3.3.patch` is
**7 773 lines / 53 files** on pristine linux-7.3.3, sha256 `434d06c3…`,
**0 rejects and 53/53 files byte-identical** (regenerate with the tree walk in
`kernel/patches/README.md`; **`make clean` the tree first** — trap 139).
Config `kernel/winchester-7.3.3.config` (`CONFIG_CHARGER_ISL9519=y`,
`CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`, `CONFIG_KERNEL_XZ=y`,
`CONFIG_SND_PCM_OSS=m`, `CONFIG_SND_MIXER_OSS=m`).
**The WL1283 code is NOT in it** — it is in
`kernel/archive/wl1283/wl1283-instrumentation-7.3.3.patch` (517 lines, applies
to the clean tree with `patch -p1`; round-trip verified).
**The 6.15.11 backup is intact** (`local/pristine/linux-6.15.11`,
`local/kernel-tree-6.15.11`, `kernel/patches/0001-playbook-winchester-6.15.11.patch`);
do not delete it; its patch is incomplete (trap 106).

**The board DTS** (`kernel/source/omap4-winchester.dts` ↔
`local/kernel-tree/.../omap4-winchester.dts`, keep identical) carries the
re-derived minimal 1917-truth `&mmc5`, the session-38 touch wiring
(`touch_irq_pins`, `interrupts = <3 IRQ_TYPE_EDGE_FALLING>`, the calibration
properties), and the session-39 audio wiring: a
`compatible = "blackberry,playbook-wm8994"` `sound` node with
`pinctrl-0 = <&codec_mclk_pins>`, `codec_mclk_pins` (`OMAP4_IOPAD(0x19c,
0x0000)`, `fref_clk2_out`), and the codec `clocks = <&auxclk1_ck>` /
`clock-names = "MCLK1"`.  The `simple-audio-card` node is gone.

## 0a. The media, and the way back

* **region 0 = our Linux image `0xF97CD9C7`** (`0x9396B4` B, kernel `7.3.3`;
  touch + ABE + the audio `sound` node/codec clock;
  `boot/chainloader/kernel.img`).
* **region 1 = the audio/OSS rootfs** (`968d2f20…`, 27 modules,
  `local/images/rootfs-region1-audio-oss.ext4`) — the sensor/charger/audio
  modules, `snd-pcm-oss`/`snd-mixer-oss`, `pbplay`, `pbmix`, and the `rcS`
  mixer setup.
  The unit is **on Linux** (`0fca:8010`, console `pb> `), not TabletOS.  The
  previous images rebuild from `0x3B2ECFED` (session 38) and `0x50842767` /
  `0x82FC0D35` (session 38); the saved fallbacks are `0xE36C679A` and
  `0x04AF1433`.
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

## 0b. Item 9 step 4 (WL1283) — software surface exhausted

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

## 0c. Item 9 step 5 — touch DONE, audio playback blocked at the codec clock

**Touch — DONE (session 38, report 198).**  I²C2 `0x48072000` = `i2c-1` at
`0x65`; `drivers/input/touchscreen/playbook-munin.c` boots the Munin app, maps
the 12-bit report through the 1917 `scaling.conf` calibration (raw x 172→1108
→ display 102→922, raw y 110→658 → display 60→540, clamped to 1024×600), and
requests the GPIO2_3 IRQ (pad `0x056`, `0x410b`, `IRQ_TYPE_EDGE_FALLING`).
On `0x82FC0D35`: `Munin touchscreen ready (irq 110)`; `/proc/interrupts` has
`110: … 48055000.gpio 3 Edge 1-0065`.  The ~330–380/s rate with no finger is
the controller's active report rate.  Remaining niceties: drive power/reset
(GPIO1_14/GPIO4_8) and coalesce the report rate.

**WM8994 audio — machine driver + stream UP (session 39, report 200).**

* **The card is a board machine driver, not simple-audio-card.**
  `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` (+ `kernel/source/`)
  defines the link `40124000.mcbsp` ↔ `wm8994-aif1`, I2S, CPU bit/frame
  provider (`CBC_CFC`).  `hw_params` sets the McBSP fclk (`12.288 MHz`) and
  `CLKGDV` (BCLK = 32 fs), moves `AIF1CLK` to MCLK1 (avoids `-EBUSY` when
  reconfigured), configures **FLL1 from BCLK** (256 fs), and switches
  `AIF1CLK` to FLL1.  Board DAPM: `Headphone`/`Speaker` ←
  `HPOUT1L/R`/`SPKOUT[LP/LN/RP/RN]`.
* **Stream proven.**  `pbplay` (region 1) opens `/dev/dsp` S16_LE/2ch/48 kHz;
  `SETFMT`/`CHANNELS`/`SPEED` succeed and the write completes.  OSS modules
  (`snd-mixer-oss`, `snd-pcm-oss`) are loaded by `rcS`.
* **DAPM path complete** after `pbmix` (new ALSA-control tool, also in `rcS`)
  sets `DAC1L/R Mixer AIF1.1`, `DAC1 Switch`, the output-mixer DAC switches,
  `SPKL/SPKR DAC1 Switch` and `SPKL/SPKR Output Switch`: `PM4=0x0300`,
  `PM5=0x0303`, `AIF1CLK_ENA=1`, and the DAC/HP/SPK widgets are `On`.
* **The blocker.**  (1) The 1917 FLL1 scheme is MCLK1 at 12.8 MHz, but FLL1
  never locks from MCLK1 (`0x732` bit 5 = 0) although `AUXCLK1 = 0x00020100`
  and CORE pad `0x19c` = `0x0000` are configured; relocking FLL1 to **BCLK**
  locks immediately (`0x732 = 0x6020`).  (2) The chip stops accepting register
  writes after boot (reads still answer): the regmap cache holds
  `0224=0c83`, `0220=0001` while the chip reads `0c80`, `0000`, so
  `regmap_update_bits()` skips the write.  `cache_only=N`, the codec is
  runtime-active, and direct `i2ctransfer`/plain `component_write` do not
  stick.  The stream therefore runs at the OSS default 8 kHz from the dead
  MCLK1 — no audible output.

**The next audio experiment (one variable per boot).**

1. **Read the codec on the working QNX stack.**  Boot TabletOS, stop
   `io-audio` first (do not race the vendor driver — trap 103), and read
   `0x200`, `0x220`–`0x224`, `0x732` and `PM4/PM5` with `pbi2c`.  This is the
   strongest next fact: it shows the reference's FLL source/config and whether
   its MCLK1 lock is real on this same board.
2. **Settle the 1917 AUXCLK1 question.**  Decompile the `0x1ecf0` map/unmap
   region of `deva-ctrl-omap4abe-wm8994.so` (Ghidra) and read the
   `startup-playbook` call site that writes `AUXCLK1 = 0x00020100` (`0x7980`).
3. **Characterise the write blocker on Linux.**  At the first prompt, write a
   known register (`i2ctransfer -f -y 3 w4@0x1a 0x00 0x36 0x00 0x03` → read
   `0x0036`) and repeat after one `pbplay`.  If the first works and the second
   does not, bisect between `rcS`/module load and the first stream (the DAPM
   `BIAS_OFF` cycle is the suspect).  Also read
   `WM8994_WRITE_SEQUENCER_CTRL_1/2` (`0x110`/`0x111`); the mainline driver
   does not start the sequencer, but a stuck sequence owns the register map.
4. **Configure the FLL after DAPM power-up.**  The first ASoC hook after
   `__soc_pcm_prepare`'s `snd_soc_dapm_stream_event(START)` is the machine's
   `.trigger(START)`.  Move the `set_sysclk`/`set_pll`/`set_sysclk` sequence
   there (keep the `hw_params` call so the codec's `aifclk` is valid), and
   check the hardware registers (`i2ctransfer`) and `0x732` bit 5.
5. **If the chip still ignores writes, measure it.**  DCVDD/DBVDD at the part,
   `/RESET`, and the FLL supplies (`AVDD2`/`CPVDD`).

**Decision table.**

| result | meaning | next |
|---|---|---|
| the QNX read shows FLL1 locked from MCLK1 | the pin is wired; our pad/source is wrong | fix the pad/source, then retry the MCLK1 FLL |
| the QNX read shows FLL1 from another clock / not locked | MCLK1 is not the answer on this board | use the QNX source (likely BCLK), then the write blocker |
| FLL registers land in hardware and `0x732` bit 5 = 1 | the codec clock is configured | listen for the tone; then the speaker/headphone routing and the SPKVDD check |
| `trigger` writes still ignored | the chip is write-blocked in this state | measure the rails/reset; try a cold power cycle before blaming the driver |
| FLL locks but no sound | DAPM routing or SPKVDD | `pbmix list` the output switches; identify the speaker rail |

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
* **The WM8994 datasheet** — Rev 4.6, fetched 2026-11-11 (used for
  `AIF1CLK ≤ 12.5 MHz`, the FLL reference sources and `FLL1_LOCK_STS`).
* Reports **176**, **193–200** (200 is this session's playback work).

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

**The region-1 rootfs** (the audio/OSS set) is rebuilt with
`tools/build_rootfs.py --modules local/work/modules-7.3.3 --krel 7.3.3`; it
builds `pbplay` and `pbmix` from `rootfs/*.c` and installs `rootfs/etc/init.d/
rcS` (module loads + the mixer setup).

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

## 4. Traps added this session (full list 9–154 in `KNOWN-ISSUES.md` §2)

* **150.** `simple-audio-card` cannot clock the WM8994: its `mclk-fs` path
  calls `set_sysclk(codec, 0, …)` and `WM8994_SYSCLK_MCLK1 = 1`, so
  `hw_params` fails with `AIF1CLK not configured`.  A board machine driver
  must call `set_sysclk`/`set_pll`; move `AIF1CLK` off the FLL before
  reconfiguring it (`FLL1 is currently providing SYSCLK`, `-EBUSY`).
* **151.** FLL1 does not lock from MCLK1 on this unit even though AUXCLK1
  (12.8 MHz) and pad `0x19c` (`fref_clk2_out`) are configured; it locks from
  **BCLK** at once (`0x732 = 0x6020`).  Use BCLK, or find the real MCLK1
  wiring from a board view.
* **152.** The WM8994 stops accepting register writes after boot (reads still
  answer); the regmap cache then diverges and `regmap_update_bits()` skips the
  write.  The FLL config (written at `hw_params`) is lost.  Compare the
  cache (`/sys/kernel/debug/regmap/3-001a/registers`) with `i2ctransfer`;
  configure the FLL after DAPM power-up.
* **153.** The ACM console drops lines; re-run a filtered `pbmix list | grep`
  before concluding a control is missing.
* **154.** The tree-derived export found two files the 48-file export had
  omitted (`drivers/input/touchscreen/{Kconfig,Makefile}`); the patch is now
  53 files / 7 773 lines.  Diff the walk's file list against the old patch.

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
| audio machine driver | `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` |
| charger driver | `local/kernel-tree/drivers/power/supply/isl9519.c` |
| OSS player / mixer tool | `rootfs/pbplay.c`, `rootfs/pbmix.c` |
| reports | `docs/reports/176…200` (200 is this session) |
| sessions | `docs/sessions/session-39.md` (this session) |

## 6. Working rules

* After every console command, dump the log tail and read it.  After a reboot
  or a structural change, **check the last 100–200 lines of `dmesg` for
  errors** — the operator asked for this explicitly in session 39.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  The whole-map diff is the highest-yield technique.
* **A test boot is not a budget concern.**  The Linux image's `/init` tops
  `BOOTCTR_FCTRY` up automatically (`pbrpmb topup 0x1E`), and TabletOS tops it
  back to `0x32`; read the counter as a health check.  (The hard hazards in
  `SAFETY.md` §2 are unchanged.)
* **Never present a guess as a fact.**  External facts are looked up and cited
  (trap 105); device facts prefer a fresh read over an old note.
* **OS 2.x only** for PlayBook hardware facts (trap 110, DECISIONS §34).  Ask
  the reference OS what it maps before writing a DT node (DECISIONS 30).
* Subagents: static/file-only, never a `tools/` script (trap 109).
* **Restore the sealed IFS at the end of every session** unless the operator
  ends on our image.  Session 39 ended on **Linux `0xF97CD9C7`** (region 0);
  reflashing the 1917 IFS at block `0x825` returns to TabletOS.

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 40 (absolute; Linux-era 34)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), KERNEL-PORT.md §7, ROADMAP.md item 9 (step 5), KNOWN-ISSUES.md (traps
9-154, especially 150-154, and §3), DEVELOPMENT.md §3/3a/4/7a, DECISIONS.md
(17-49), reports 176, 180-200, sessions/session-38.md and session-39.md.

WHERE IT STANDS: the WM8994 PLAYBACK PATH IS BUILT — a board machine driver
(sound/soc/ti/playbook-wm8994.c) registers `0 [WM8994]: PlayBook_WM8994`, an
OSS write reaches the PCM at 48 kHz, and the DAPM path powers (PM4=0x0300,
PM5=0x0303, AIF1CLK_ENA=1, DAC1L/SPKL/HPOUT1L/SPKOUTLP On) after rcS sets the
mixers with pbmix.  BUT THE CODEC CLOCK IS BLOCKED chip-side: FLL1 never locks
from MCLK1 (the pin does not clock on this unit; AUXCLK1/fref_clk2_out are
configured) while FLL1 from BCLK locks (0x732=0x6020), and the chip stops
accepting register writes after boot, so the driver's FLL config stays in the
regmap cache (trap 152).  No audible output yet (report 200).  The touch is
DONE (report 198).  The WL1283 is software-exhausted (report 196); do NOT
repeat traps 116-149 without stating what differs.  The base patch is 53
files / 7 773 lines (sha256 434d06c3...).

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE 0xF97CD9C7 (kernel 7.3.3;
touch + ABE + audio sound node/codec clock); region 1 is the audio/OSS rootfs
968d2f20... (27 modules + pbplay/pbmix).  BOOTCTR_FCTRY is self-replenishing.
The way back to TabletOS is the 1917 IFS (local/work/ifs-shim-sealed.ifs) at
block 0x825.  Do NOT restore local/images/ifs0.sealed.bin (2.0.0.4869, trap
84).

WHERE IT IS: the next audio experiment is (a) characterise the register-write
blocker (write a register at the first prompt vs after one stream), (b)
configure the FLL in .trigger(START), the first hook after the DAPM stream
event, and (c) if writes are still ignored, measure DCVDD/DBVDD, /RESET and the
FLL supplies at the part.  Item 9 step 4 (WL1283) is software-exhausted; the
hd5 calibration is the asset for the post-enumeration wl12xx path.

BENCH: the unit is on OUR LINUX IMAGE — there is NO door.  Use tools/pbcon.py
(prompt `pb> `), reboot with tools/pbcon.py 'reboot -f'; pkexec chmod 666
/dev/ttyACM0 after EVERY boot (operator answers; never retry a failed pkexec).
After every reboot/change, check the last 100-200 lines of dmesg for errors.
On TabletOS: door = 169.254.0.1 (host link 169.254.0.2/16 on enp8s0f3u1 — do
not re-add); start tools/door-hold.sh; never pgrep/pkill -f; tools/pbssh.sh
logs in as ROOT (uid:0) after the key upload (trap 145).  Run USB/flash tools
as the USER, never pkexec.  Device password 0000, once.  An agent NEVER writes
RPMB; NVRAM is read-only (SAFETY §1/§6).

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

NEXT (operator's call): (a) the WM8994 write-blocker/FLL experiment above;
(b) the WL1283 chip-side measurements (needs a board view/logic analyser);
(c) build wl128x-nvs.bin from hd5 and add the `wlcore@2` child when the bus
works; (d) touch niceties (drive power/reset, coalesce the report rate).  The
unit is on Linux; region 0/region 1 both change when the kernel/rootfs pair
does, otherwise flash only the changed region; the 1917 IFS at block 0x825 is
the way back to TabletOS.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.
```
