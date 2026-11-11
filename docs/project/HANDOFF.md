# Handoff — session 41 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 41 = Linux-era 35**.  See
`docs/sessions/NUMBERING.md`.

> **Session-40 result, in one line:** the WM8994 "write blocker" is **solved** —
> the codec disables control-register access while `AIF1CLK_ENA = 1` with a
> stopped clock source (datasheet "Control Interface Clocking"); clearing
> `R0x200` bit 0 thaws it.  The codec is frozen from boot because the DAPM
> `AIF1CLK` supply powers with `rcS`'s playback path while `AIF1CLK_SRC` is
> still MCLK1 — and **MCLK1 does not clock**: replaying the reference's own FLL
> programme (with `FRAC` and the PM1/PM2 bias) and the datasheet's free-running
> FLL both leave `0x732` bit 5 = 0.  Reports **201** (static RE of the 1917
> driver) and **203** (the measurement); decision §50; traps 155–159.  No image
> or rootfs change this session.

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
touch (calibrated, IRQ wired — session 38)** and the **WM8994 card + DAI stream
(session 39)**.  WDT2 runs the reference's 58.6 s window with a 10 s pet.

**The audio blocker is now one clock.**  The codec's register map, the DAI link,
the OSS stream and the DAPM path are all known good; what is missing is
`AIF1CLK` — FLL1 will not lock from MCLK1, and the free-running FLL does not
either, so either the MCLK1 pin does not toggle or the FLL's analogue supply
(AVDD2) is wrong.  The reference driver works, so this is a board-level fact on
our side.

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
`clock-names = "MCLK1"`.  **The codec supply mapping is a guess and is out of
spec** (§0c) — treat it as unknown, not as truth.

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

## 0c. Item 9 step 5 — touch DONE; audio blocked on the codec clock, not writes

**Touch — DONE (session 38, report 198).**  I²C2 `0x48072000` = `i2c-1` at
`0x65`; `drivers/input/touchscreen/playbook-munin.c` boots the Munin app, maps
the 12-bit report through the 1917 `scaling.conf` calibration to 1024×600, and
requests the GPIO2_3 IRQ (pad `0x056`, `0x410b`, `IRQ_TYPE_EDGE_FALLING`).
Remaining niceties: drive power/reset (GPIO1_14/GPIO4_8) and coalesce the report
rate.  **Session-40 bench note:** after a reboot the controller did not boot
(`touch did not boot: -110`, `0x65` NAKs) and it stays down until the power/reset
GPIOs are pulsed — that nicety is now the recovery path, not a polish item.

**WM8994 — the write blocker is solved; the clock is the blocker (session 40,
reports 201/203, decision §50).**

* **The rule.**  The codec disables control-register access while
  `AIF1CLK_ENA = 1` and the selected AIF1CLK source is stopped (WM8994 Rev 4.6,
  "Control Interface Clocking").  Measured: reads answer, writes are ACKed and
  dropped; `R0x200 = 0x0000` thaws within one write; a software reset also
  thaws; `R0x200 = 0x0001` (ENA, SRC = MCLK1) freezes the next write; idle does
  not re-freeze it.
* **Why it is frozen from boot.**  The DAPM `AIF1CLK` supply powers with the
  `rcS` playback path while `AIF1CLK_SRC` is still MCLK1.  Every later MFD/
  `regmap` write to the codec is therefore dropped — including the FLL
  configuration at `hw_params`.  `regmap`'s cache divergence (report 200) is a
  symptom, not the cause.
* **The reference (`FUN_0001decc`)** does: AUXCLK1 gate → software reset →
  PM2/PM1/AntiPOP/bias → FLL1 (`0x224 = 0` MCLK1 ÷1, `0x221 = 0x0700`,
  `0x223 = 0x00E0` (N = 7), `0x220 |= 0x0004` (`FLL1_FRAC`), `0x222 = 0xAE14`
  (K), delay, `FLL1_ENA`, delay) → `0x200` SRC = FLL1 → rate/format → **`0x200`
  ENA last**.  AIF1CLK is always from FLL1; MCLK1 is only the FLL reference.  No
  lock polling, no free-running FLL, no write sequencer.
* **MCLK1 works on the reference and not under our image** (reports 203/204).
  The 1917 IFS was booted and the codec read live (root ssh, `tools/pbi2c`
  `dump16` — the tool gained 16-bit-register support): **FLL1 is locked**
  (`0x732 = 0x6020`, bit 5 = 1) with `0x224 = 0x0c80` (MCLK1) and
  `0x220 = 0x0005` (ENA|FRAC), and the reference keeps
  **`0x200 = 0x0010` (`AIF1CLK_ENA = 0`) while idle**.  Under Linux the same
  codec bytes read back and never lock — at any SCRM `AUXCLK1` divider (÷1…÷8
  swept), with the codec reset or with the PM1/PM2 bias on — and the datasheet's
  free-running FLL does not lock either.  Both OSes read `AUXCLK1 = 0x00020100`
  and pad `0x19C` mux 0, so the difference is **below those registers**.
* **One concrete Linux difference:** a boot can leave
  `AUXCLK1 = 0x00000100` (÷1 = **38.4 MHz**, outside the FLL's 0.032–13.5 MHz
  range) until the codec driver's first `set_sysclk(MCLK1, 12800000)` calls
  `clk_set_rate()`; the SCRM value survives a warm reset, so it is not
  deterministic (a later clean reboot showed `0x00020100`).  Forcing ÷3…÷8 by
  hand did not lock it either, so this is a real defect but not the whole story.
  **Where the reference sets it:** `startup-playbook` itself, at
  `0x80107980` (`0x00020100`), with an "off" writer at `0x80100908`
  (`0x00010000`) — see `docs/reference/startup-playbook/clocks-and-wdt.md`.
* **The reference's live register file** (report 204, for diffing): PM1
  `0x0013`, PM2 `0x6000`, `0x200 = 0x0010`, `0x208 = 0x0010`, `0x210/211 =
  0x0083`, `0x220 = 0x0005`, `0x221 = 0x0700`, `0x222 = 0xae14`, `0x223 =
  0x00e0`, `0x224 = 0x0c80`, `0x300 = 0x4018`, `0x301/302 = 0x4000` (codec is
  the AIF1 **master**), `0x303–305 = 0x0040`, `0x601/602 = 0x0001`,
  `0x700 = 0x8101`, `0x701–706 = 0xa101`, `0x707/708 = 0x4007/0x4008`,
  `0x732 = 0x6020`.
* **The DTS supply mapping is a guess and is out of spec:** AVDD1 → `v1v8`
  (1.8 V; the part needs 2.4–3.3 V), AVDD2/CPVDD → `v2v1` (2.1 V; max 2.0 V),
  SPKVDD → `v2v1` (2.1 V; needs 2.7 V+).  The FLL's analogue supply is AVDD2.
* **A driver-only fix cannot work as things stand.**  ASoC order is
  `.prepare` (link/component/DAI) → DAPM stream event (sets `AIF1CLK_ENA`) →
  `.trigger(START)` (starts BCLK).  The codec clock is enabled one step before
  a live reference exists, so moving the FLL setup to `trigger(START)` alone
  would still freeze at the DAPM event.  It needs the codec's `AIF1CLK` DAPM
  supply held off until the McBSP runs (`snd_soc_component_disable_pin()` or
  equivalent), then `R512 = 0`, FLL1 from BCLK, `SRC = FLL1`, `ENA = 1`, verify
  `0x732` bit 5.  **Do this only after the MCLK1 question is answered** — it is
  a fallback, not the preferred path.

### The next audio step — one experiment

**Name the pad that carries the codec's MCLK1.**  The reference locks FLL1 from
MCLK1 and our image does not, with identical `AUXCLK1` and codec bytes; the only
way left to be wrong about "identical" is that `0x19C` is not the pad the codec's
MCLK1 is on.  This settles it from the reference, no meter.

**Inputs**

* `local/work/1917/base/lib/dll/hwio-omap4.so` — the pad-id → {bank, offset,
  half} resolver: three 0xE7-entry tables at file VAs `0x4FF8`, `0x5394`,
  `0x5730` (report 189 §2.1; Ghidra loads this ELF at `file VA + 0x10000`).
* `local/work/1917/base/lib/dll/hwio-config-winchester.so` — `pad_config`
  (248 × 20 B) and `gpio_config` (72 × 32 B); the FREF2_OUT entry is pad id
  `0xB7` (report 189 §2.2).
* `docs/reference/startup-playbook/pads.md` (our signal-group table) and
  `gpio-pins.md` (the signal ids: `0x58` WL_EN, `0x59` WL_IRQ, …).
* Live reads: on TabletOS, build `tools/memdump3.c` with the QNX 6.6 toolchain
  (`. tools/env.sh`) and run `memdump3 4a10019c 8` and `memdump3 4a30a314 4`
  over the door; on Linux, `devmem 0x4A10019C 32` and `devmem 0x4A30A314 32`.
  The padconf is **16-bit per pad** (`pinctrl-single,register-width = <16>`,
  `omap4-l4.dtsi` `omap4_pmx_core`), so a 32-bit read gives pad `0x19C` (low)
  and pad `0x19E` (high).

**What each result means**

| result | meaning | next |
|---|---|---|
| `fref_clk2_out` resolves to `0x19C`, and QNX and Linux both read `0x0000` | the pad is not the difference | the codec's analogue side: decode the reference's codec power path (report 201 `FUN_0001decc`) against our DAPM sequence; then the supplies (AVDD2) |
| `fref_clk2_out` resolves to `0x19E` (or another pad) | our DTS muxes the wrong pad — this would explain everything | change `codec_mclk_pins` to the correct pad and retry the FLL lock |
| the low half of `0x19C` differs between QNX and Linux | our pad word is wrong | adopt the reference's exact pad value in the DTS |
| `AUXCLK1` differs at the moment of the test | the rate/gate, not the pad | `assigned-clock-rates = <12800000>` on `auxclk1_ck` (or an explicit `clk_set_rate` before the interface is enabled) |
| neither pad is a clock output | the board drives MCLK1 from something outside the SoC | find it in the reference (ABE? PMIC?) before any driver change |

**Only after that**, the driver work — in this order, each verifiable:
(i) `AIF1CLK_ENA = 0` while idle (do not power the codec path from `rcS` until a
stream needs it); (ii) `AUXCLK1` = 12.8 MHz before the interface is enabled;
(iii) FLL1 first, with `0x224` masked to bits 4:0 (`FRC_NCO_VAL = 0x19`
survives), `N` at `[14:5]`, `FRAC` set; (iv) `AIF1CLK` last, then check `0x732`
bit 5.

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
* **`local/work/1917/ifs/`** — the 1917 IFS tree + `proc/boot/startup-playbook`
  (sha256 `bc7d3a41…`).
* **`local/work/1917/calib/`**, **`local/work/1917/parts/`** — the WLAN
  calibration and the pulled partitions (report 197).
* **`local/vendor/bb10mt/bb10mt`** — the QNX6/container tool; use it for
  `qnx6 mount/compact/mkfs/fsck` and `rcfs`.
* **The WM8994 datasheet** — Rev 4.6, fetched 2026-11-11 and the authority for
  every clocking/register statement in reports 201/203 (control-interface
  clocking, free-running FLL, recommended operating conditions).
* Reports **176**, **193–201**, **203** and **204**; the newest are **201**
  (QNX driver RE), **203** (the write-freeze measurement) and **204** (the live
  QNX codec read).  The numbering intentionally skips one number between 201 and
  203: that attempt (ABE/AUXCLK1 RE) produced nothing, and its question is folded
  into reports 203/204 and the session note.

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
  `pkexec chmod 666 /dev/ttyACM0` after EVERY boot — it is ours to run (the
  operator confirmed it in session 40).  Run USB/flash tools as the **user**,
  never pkexec.
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
* The 1917 QNX tools that run on the device must be `-static` (trap 131).
* **Tracepoints work on our image:** `mount -t tracefs none /sys/kernel/tracing`
  then `events/regmap/regmap_reg_write` gives the driver's exact codec write
  order.  `debugfs` must be mounted at `/sys/kernel/debug` for the regmap cache.

## 4. Traps added last session (full list 9–159 in `KNOWN-ISSUES.md` §2)

* **155.** The WM8994 disables control-register access while `AIF1CLK_ENA = 1`
  with a stopped source; `R0x200 = 0` thaws it, a software reset does too.
* **156.** A fractional FLL1 needs `FLL1_FRAC` (R544 bit 2); a whole-register
  `0x220 = 0x0001` clears it.  Use a read-modify-write.
* **157.** Reserved bits read back 0; one register is not a valid write test.
* **158.** `devmem` on an unclocked module (GPIO5 `0x4805B038`) gives an L3
  external abort and taints the kernel.
* **159.** Prove the codec is thawed before concluding writes are dropped.

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| archived WL1283 code | `kernel/archive/wl1283/` |
| 1917 `/base`, IFS, calibration | `local/work/1917/{base,ifs,calib,parts}/` |
| 1917 audio driver (RE'd) | `local/work/1917/base/lib/dll/deva-ctrl-omap4abe-wm8994.so` |
| touch driver | `local/kernel-tree/drivers/input/touchscreen/playbook-munin.c` |
| audio machine driver | `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` |
| charger driver | `local/kernel-tree/drivers/power/supply/isl9519.c` |
| OSS player / mixer tool | `rootfs/pbplay.c`, `rootfs/pbmix.c` |
| reports | `docs/reports/176…203` (201 and 203 are this session's) |
| sessions | `docs/sessions/session-40.md` (last session) |

## 6. Working rules

* After every console command, dump the log tail and read it.  After a reboot
  or a structural change, **check the last 100–200 lines of `dmesg` for
  errors** — the operator asked for this explicitly.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  The whole-map diff is the highest-yield technique.
* **A test boot is not a budget concern.**  The Linux image's `/init` tops
  `BOOTCTR_FCTRY` up automatically (`pbrpmb topup 0x1E`), and TabletOS tops it
  back to `0x32`; read the counter as a health check.  (The hard hazards in
  `SAFETY.md` §2 are unchanged.)
* **Never present a guess as a fact.**  External facts are looked up and cited
  (trap 105); device facts prefer a fresh read over an old note.
* **Use the reference before the bench.**  The 1917 QNX tree in
  `local/work/1917` is a working implementation: static RE (REA MCP + Ghidra)
  and a live QNX read answer "how does the driver do it" faster than a bench
  bisect, and the operator said so in session 40.  Bench experiments are for
  what only the hardware can answer.
* **OS 2.x only** for PlayBook hardware facts (trap 110, DECISIONS §34).  Ask
  the reference OS what it maps before writing a DT node (DECISIONS 30).
* Subagents: static/file-only, never a `tools/` script (trap 109).
* **Restore the sealed IFS at the end of every session** unless the operator
  ends on our image.  Session 40 ended on **Linux `0xF97CD9C7`** (region 0);
  reflashing the 1917 IFS at block `0x825` returns to TabletOS.

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 41 (absolute; Linux-era 35)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), KERNEL-PORT.md §7, ROADMAP.md item 9 (step 5), KNOWN-ISSUES.md (traps
9-159, especially 150-159, and §3), DEVELOPMENT.md §3/3a/4/7a, DECISIONS.md
(44-50), reports 176, 180-204, sessions/session-39.md and session-40.md.

WHERE IT STANDS: the WM8994 "write blocker" is SOLVED — the codec disables
control-register access while AIF1CLK_ENA=1 with a stopped clock source
(datasheet "Control Interface Clocking"); clearing R0x200 bit 0 thaws it within
one write.  The 1917 driver (FUN_0001decc) programs the FLL first and sets
AIF1CLK_ENA LAST.  A live TabletOS read (report 204, tools/pbi2c dump16) proves
the codec clock works on this board: FLL1 is locked (0x732=0x6020) from MCLK1
(0x224=0x0c80) with 0x220=0x0005 (ENA|FRAC), and the reference keeps
0x200=0x0010 (AIF1CLK_ENA=0) while idle.  Under our Linux image the same codec
bytes read back and NEVER lock — at any AUXCLK1 divider (1..8 swept), codec reset
or biased — and both OSes read AUXCLK1=0x00020100 / pad 0x19C mux 0.  One
concrete Linux defect: a boot can have AUXCLK1=0x00000100 (divide by 1 = 38.4
MHz, outside the FLL's 0.032-13.5 MHz range) until the codec driver's first
set_sysclk(MCLK1,12800000); the value survives a warm reset, so it is not
deterministic.  So the blocker is ONE CLOCK, below the registers we
can see (pad drive, a supply, /RESET).  The touch is DONE (report 198) but did
not boot after the last reboot (0x65 NAKs; it needs the power/reset GPIO pulse
the driver does not drive).  The WL1283 is software-exhausted (report 196).  The
base patch is 53 files / 7 773 lines (sha256 434d06c3...).

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE 0xF97CD9C7 (kernel 7.3.3;
touch + ABE + audio sound node/codec clock); region 1 is the audio/OSS rootfs
968d2f20... (27 modules + pbplay/pbmix).  BOOTCTR_FCTRY is self-replenishing.
The way back to TabletOS is the 1917 IFS (local/work/ifs-shim-sealed.ifs) at
block 0x825.  Do NOT restore local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

WHERE IT IS: one experiment — name the pad that carries the codec's MCLK1.
Decode hwio-omap4.so's pad-id -> {bank,offset,half} tables (report 189 §2.1) to
resolve which pad is fref_clk2_out (pad 0x19C low half, or 0x19E high half — the
padconf is 16-bit per pad, pinctrl-single,register-width = <16>), then read
0x4A10019C (8 bytes) and 0x4A30A314 live on QNX (memdump3, built with the QNX
toolchain, over the door) and on Linux (devmem).  If 0x19C is fref_clk2_out and
both read 0x0000, the pad is not the difference and the codec's analogue side is
next; if our pad is wrong, fix codec_mclk_pins and retry the FLL lock.  Then the
driver work in order: AIF1CLK_ENA=0 when idle, AUXCLK1 12.8 MHz before enabling
the interface, FLL1 first with 0x224 masked to bits 4:0, AIF1CLK last, verify
0x732 bit 5.  The QNX codec read is done (report 204); the codec clock works on
this board.  The reference's own AUXCLK1 write is startup-playbook 0x80107980
(docs/reference/startup-playbook/), and the DTS supply mapping is a guess and
out of spec (AVDD1 on 1.8 V vs a 2.4 V minimum).

BENCH: the unit is on OUR LINUX IMAGE — there is NO door.  Use tools/pbcon.py
(prompt `pb> `), reboot with tools/pbcon.py 'reboot -f'; run
`pkexec chmod 666 /dev/ttyACM0` after EVERY boot (it is ours to run).  On
TabletOS: door = 169.254.0.1 (host link 169.254.0.2/16 on enp8s0f3u1 — do not
re-add); start tools/door-hold.sh; never pgrep/pkill -f; tools/pbssh.sh logs in
as ROOT (uid:0) after the key upload (trap 145).  Run USB/flash tools as the
USER, never pkexec.  Device password 0000, once.  An agent NEVER writes RPMB;
NVRAM is read-only (SAFETY §1/§6).  After every reboot/change, check the last
100-200 lines of dmesg.

STATIC RE: the REA MCP server exposes Ghidra 12.1.4 (open_binary,
binary_overview, analyze_function, procedure_pseudo_code, search_strings,
xrefs, read_bytes); raw ARM import format=raw-binary, architecture=arm,
entry_point=0x80100000.  Reconcile with ntoarmv7-readelf/objdump (Ghidra may
add +0x10000 on an ELF).  QNX device tools build -static.  USE THE REFERENCE
FIRST: the 1917 tree in local/work/1917 is a working implementation, and static
RE plus a live QNX read answer driver questions faster than a bench bisect.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
A test boot is not a budget concern (the counter self-replenishes), but the
hard hazards (0 / >0x32 / failed write / flat pack) remain.  Operator's call
when to stop and to wrap up.

NEXT (operator's call): (a) the meter on MCLK1 and the supplies, QNX vs Linux;
(b) the driver work in the right order (AIF1CLK_ENA=0 when idle, AUXCLK1 12.8
MHz, FLL first, ENA last); (c) touch power/reset (the touch is down); (d) the
wl128x-nvs.bin path (hd5).  The unit is on Linux; region 0/region 1 both
change when the kernel/rootfs pair does, otherwise flash only the changed
region; the 1917 IFS at block 0x825 is the way back to TabletOS.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.
```
