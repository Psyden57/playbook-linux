# Handoff — session 46 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 46 = Linux-era 40**, updated at its
wrap-up.  See `docs/sessions/NUMBERING.md`.

> **Session-46 result, in one line:** **the ABE question is closed and the
> audio works** — the WM8994's AIF1 is on **McBSP3** (not McBSP1/2), the card
> is two ASoC links (`mcbsp2 + wm8994-aif1` clocks, `mcbsp3 + wm8994-aif2`
> data), and the `omap-mcbsp` channel-layout guard fixed the right channel.
> `pbplay`'s 440 Hz tone is audible in both ears at 48 kHz ±1 % (operator).
> Report 214; DECISIONS §59.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain, the 64 MiB ext4 BusyBox root in region
1, the peripherals (I²C4 motion sensors, TMP103, BQ27541, FAN5702, CM3217,
ISL9519 charger), the **Munin touch** (calibrated, IRQ wired; §0d), and now the
**WM8994 audio** (stereo, 48 kHz; §0c).

**The audio path is the working design (session 46, reports 213/214).**

* **The ABE is not needed.**  Its serial port is a **firmware data path**
  (`abe_connect_serial_port` writes the AE port table/ATC descriptor;
  `abe_init_atc`'s serial case is a no-op; `abe_load_fw` supplies the
  consumer), while the McBSP clocks are CM1_ABE/McBSP hardware that mainline
  already drives.  DECISIONS §58.
* **The codec's AIF1 is on McBSP3, not McBSP1/2.**  The oracle's own routing
  log is `AIF 0 Downlink: MM_DL_PORT->MM_EXT_OUT_PORT->MCBSP3`; McBSP3 is the
  only L4_ABE target active during playback, `PCR0 = 0x3` (consumer), and its
  pads are the PDM group in mode 1 (`0x106/0x108/0x10a/0x10c` =
  `0x0109/0x0009/0x0109/0x0109`).  `abe_mcbsp1_dx` is the
  `AIF 4 → MCBSP1` route.
* **The card is two ASoC links** (`playbook-wm8994.c`): link 1
  `40124000.mcbsp + wm8994-aif1` (E5 clocks/codec, `CBC_CFC`), link 2
  `40126000.mcbsp + wm8994-aif2` (data, `CBP_CFP`, consumer).  The codec is
  the AIF1 slave and FLL1 locks from our BCLK (`R0x302 = 0x0000`,
  `R0x732 = 0x6020`, `R0x200 = 0x0011`), so **no MCLK1 is in the path**.  The
  data link's `wm8994-aif2` is an ASoC stand-in; the physical data is AIF1's
  DACDAT on `mcbsp3_dx`, and DAC1 selects AIF1DAC1 (`R0x601/2 = 1`).
* **The right channel needed the `omap-mcbsp` channel-layout guard.**  The OSS
  first opens a stream mono at the final rate/width; the later 2-channel call
  was skipped (rate/width only), leaving `XPHASE` clear (`XCR2 = 0x0041`).
  Comparing `XPHASE` as well gives `0x8041` and both slots play.

**The patch** `kernel/patches/0001-playbook-winchester-7.3.3.patch` is the
session-46 export (**54 files / 8 440 lines**, sha256 `fd2774fb…`, 0 rejects /
54/54 byte-identical by apply-and-compare).  It carries the McBSP3 two-link
card, the `XPHASE` guard, the E5 clock roles and the two session-45
diagnostics.

## 0a. The media, and the way back

* **region 0 = the session-46 Linux image** (`boot/chainloader/kernel.img`,
  CRC **`0xF8BADFF3`**, `0x93993C` bytes; kernel `7.3.3` **#42**, the McBSP3
  two-link card).
* **region 1 = the audio/OSS rootfs** `c0719d28…`
  (`local/images/rootfs-region1-audio-oss.ext4`, 27 modules, `pbplay`/`pbmix`,
  the stereo-fixed `snd-soc-omap-mcbsp.ko`).
* The unit is **on our Linux image** (`0fca:8010`, console `pb> `).  `BOOTCTR_FCTRY`
  read **`0x20`** from the `/init` log (`0x20 >= threshold 0x1e, no write`);
  the image tops it up at boot.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825` (the pruned RCFS is still at `0x8D5`/region-0 `0xC00000`), then
  reboot.  It enumerates `0fca:8012`; qconn `169.254.0.1:9090`, root ssh.
* **To stock TabletOS:** flash `local/work/minimal/wayback.signed`.  **Do not
  restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).
* **To rebuild the oracle:** `python3 tools/build_minimal_qnx.py`.
* **Saved images:** `local/images/kernel-0xF8BADFF3-mcbsp3-consumer.img` (the
  working region 0), `local/images/kernel-0x47ED0450-mcbsp1-consumer.img` (the
  failed mcbsp1 experiment), `local/images/rootfs-region1-audio-oss-stereo.ext4`
  (`c0719d28…`, working), `…-e9mcbsp3.ext4` (`a726bb49…`, mcbsp3 first),
  `…-e5.ext4` (`6d92428e…`, the E5 rootfs).

## 0b. Item 9 step 4 (WL1283) — software surface exhausted

Unchanged and **formally stopped by the operator** (session 36).  The 1917-truth
minimal `&mmc5` host probes but `attach_sdio no` (report 196).  Do not re-run
traps 116–137.

## 0c. Item 9 step 5 — audio works; the next experiment is the speaker

### What is proven (do not re-derive)

* **The transport** (E5): McBSP2 is the bit/frame provider, the codec is the
  AIF1 slave, FLL1 locks from our BCLK.  `SRGR1 = 0x0f1f`, `SRGR2 = 0x301f`,
  `PCR0 = 0x0f0f`, `R0x302 = 0x0000`, `R0x732 = 0x6020`, `R0x200 = 0x0011`.
  `CLKSM = 1` + `CLKGDV = 32` divides the 49.152 MHz ABE interface clock; the
  clock link measures ~48 096 frames/s.
* **The data engine is McBSP3** (report 214): `XCR2 = 0x8041` (stereo),
  `SPCR2 = 0x0235`, `XBUFFSTAT` toggling, the 40 s tone writes 7,684,096 B
  (the old failure was an 82,176-byte `EIO`).  The codec's DAC1 is fed from
  AIF1DAC1; **the 440 Hz tone is audible in both ears at 48 kHz** (operator).
* **The codec's output chain** matches the reference: `PM1 = 0x3303`,
  `PM3 = 0x03f0`, `PM5 = 0x0303`, `0x02d/0x02e = 1`, `0x601/2 = 1`,
  `0x060 = 0x00ee`, DAC1 volumes 0xc0.
* **The right channel** is the `XPHASE` guard (trap 205); do not remove it.

### The one next experiment — the speaker path

The operator's listening test was on **headphones**.  The speaker path
(`SPKMIX`) was never exercised.  The codec's speaker **controls** read routed
(`SPKL/SPKR DAC1 Switch` [1], `SPKL/SPKR Output Switch` [1],
`Speaker Switch` [1 1], `Speaker Volume` [57 57], `Speaker Mode` Class D,
`PM1` `SPKOUTL/R_ENA` set), so the likely gate is **board-level**: the `spk`
line (GPIO2_12/gpmc_a20, signal `0x93`) or the `hp` jack detect (the codec's
HP path is on; the speaker may be muted while headphones are plugged).

**These speaker-path steps are the plan; none has been run yet.**

**Exact inputs and steps:**

1. **Unplug the headphones.**  This is the first variable.
2. On Linux (`0xF8BADFF3` + `c0719d28…`), run the working pair:
   `pbplay 45 0 /dev/dsp &` then `pbplay 40 440 /dev/adsp &`.
3. Read, while it plays:
   * the raw codec registers `0x0001`, `0x0003`, `0x0022`, `0x0023`,
     `0x0026`, `0x0027` (`/sys/kernel/debug/regmap/3-001a/registers`);
   * `pbmix list "SPK"`, `pbmix list "Speaker"`;
   * the board GPIOs (`/sys/kernel/debug/gpio`): the `spk` and `hp` lines,
     and `pinctrl` pads `0x068`/`0x070`/`0x120`.
4. If it is silent, boot the oracle (`ifs-shim-sealed.ifs` at `0x825`), leave
   the headphones **unplugged**, start `io-audio` and
   `audio_out -i /tmp/long30.wav`, and read the same codec registers
   (`pbi2c rd16 3 0x1a …`) and the `spk`/`hp` GPIOs.  Diff the two.
5. Judge by ear and by the same register diff.  One variable per boot; leave
   the audio path otherwise untouched.

**What each result means:**

* **The speaker sounds on Linux with the headphones unplugged.**  The path is
  already fine; the earlier "headphones only" was the jack.  Record it and
  close the speaker nicety.
* **Linux silent, oracle sounds.**  A codec/board difference.  Diff the
  speaker registers (`R0x22`/`R0x23` = SPKMIXL/R, `R0x24`/`R0x25` = SPKLVOL/
  SPKRVOL, `R0x01`/`R0x03` PM1/PM3) and the `spk`/`hp` GPIOs; implement the
  missing route in the machine driver's repair (or the rcS) — a module-only
  change, region-1 flash.
* **Both silent with the headphones unplugged.**  The speaker amplifier/rail
  or the jack switch is the limit; record it and stop (a board-level probe is
  the next evidence, not another register guess).

### Driver facts you need before touching it

* The repair runs from a **delayed work 50 ms after `trigger(START)`** and
  begins with `regcache_mark_dirty()` + `regcache_sync()` (trap 162).
* `hw_params` writes `R0x200 = 0` (thaw), and the repair re-enables `AIF1CLK`
  **last**.
* A frozen codec drops **raw `i2ctransfer` writes too** (trap 167).
* `omap-mcbsp`, `snd-soc-wm8994` and the machine driver are **modules**: a
  change to them does **not** change the chainloader CRC and needs a
  **region-1-only** flash (trap 190).  Copy the rebuilt `.ko` into
  `local/work/modules-7.3.3/` first (trap 163).
* The `pcm:` ring readback (machine driver) and the `live N:` FIFO dump
  (`omap-mcbsp`) are still in the tree and the patch; retire them after the
  speaker work.
* **The ABE HAL Addendum is local:** `hardware/abe-hal-addendum.txt` (PDF
  gitignored).  The TRM is `hardware/swpu231ap.pdf.txt`; the WM8994 is
  `hardware/WM8994.PDF` — Table 106/“AIF1 Control (1)”, the speaker mixer
  registers R34/R35 (SPKMIXL/R) and the output PGA R38/R39
  (SPKLVOL/SPKRVOL).

## 0d. Touch — done, and it survives a warm reboot; cold boots need the pulse

I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` boots the Munin app
and maps the 1917 `scaling.conf` calibration to 1024×600; the GPIO2_3 edge IRQ
works (pad `0x056`, `0x410B`, `irq 110`).  A warm reset keeps the controller up;
after a **cold** boot it does not until the power/reset GPIOs (**GPIO1_14 /
GPIO4_8**) are pulsed — the driver still does not drive them.

## 0e. The minimal 1917 oracle (session 43; report 207)

A debloated 2.1.0.1917 image that boots in ~30 s and gives a shell over USB.

* **Media:** region 0 = the shim IFS at `0x825` + the pruned RCFS at `0x8D5`
  (`local/work/minimal/setB.signed`).  It is **not** on media at the start of
  session 47 — region 0 holds the Linux image.
* **Connect:** `python3 tools/QCL.py info` (qconn, `169.254.0.1:9090`) or
  `tools/pbssh.sh` (root, `uid:0`).  Host link `enp8s0f3u1` carries
  `169.254.0.2/16` — do not re-add.
* **Audio HAL:** `/base/sbin/io-audio -d omap4abe-wm8994 aif=0,secure_capture
  -o enable_buffering`; it leaves FLL1 locked (`R0x732 = 0x6020`).
  `slay io-audio` before the freeze detector.
* **Tools on the UFS:** `/accounts/devuser/bin/{memdump3,memw32,pbi2c}`; they
  survive reboots.  WM8994 writes need `pbi2c wr16r` (trap 176).
* **Bench only:** unauthenticated qconn/root ssh — never on a network (trap 173).
* **A reboot restores volatile register state** (trap 177).
* **`memdump3` on an idle McBSP target aborts (`SIGBUS`)**; only the active
  McBSP3 target reads during playback (trap 206).  The codec's regmap debugfs
  on Linux is the safe codec path.

## 1. Useful local extracts

* `local/work/1917/base/`, `ifs/`, `calib/`, `parts/` — the 1917 `/base`,
  IFS tree, WLAN calibration, pulled partitions.
* `local/vendor/bb10mt/bb10mt` — the QNX6/container tool.
* The minimal oracle — `tools/build_minimal_qnx.py`, `local/work/minimal/`.
* The WM8994 datasheet — Rev 4.6, `hardware/WM8994.PDF` (gitignored).  Table 106
  = the AIF1 clocking codes, “AIF1 Control (1)” = the format encoding, the
  Speaker Mixer Control = R34/R35.
* **The ABE/HAL Addendum — SWPU245F**, `hardware/abe-hal-addendum.txt` (PDF
  gitignored; the public TRM's chapter 12 defers to it).
* Reports **207**, **208**, **209**, **212**, **213**, **214** (current);
  captures in `docs/captures/`
  (`wm8994-e10-mcbsp3-2026-11-13.txt` is this session's working-audio
  evidence).

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

**A module-only change keeps the CRC** (trap 190): build `modules`, copy the
`.ko` into `local/work/modules-7.3.3/` (trap 163), rebuild the rootfs and flash
**region 1 only**:

```bash
python3 tools/build_rootfs.py --size-mb 64 \
    --modules local/work/modules-7.3.3 --krel 7.3.3 \
    --out local/images/rootfs-region1-audio-oss.ext4
python3 tools/bbusb.py flash --component local/images/rootfs-region1-audio-oss.ext4 \
    --dest-block 0x2915 --from-bootrom --password 0000 --apply &
tools/pbcon.py 'reboot -f'
```

Both regions (DTS or kernel change) use the `both.lst` recipe in
`DEVELOPMENT.md` §4.

**Patch export** (trap 161): regenerate from `local/pristine/linux-7.3.3` over
the old patch's file list (the `+++ b/` lines; `diff -u --label a/<path>
--label b/<path>`, `/dev/null` for new files), then **apply it to a staged
copy of the pristine files and compare all 54** to `local/kernel-tree` —
54/54 byte-identical is the acceptance test.  The session-46 export is
54 files / 8,440 lines, sha256 `fd2774fb…`.

## 3. Bench

* **The unit is on our Linux image** (`0fca:8010`, console `pb> `).
  `tools/pbcon.py 'cmd'`; reboot with `tools/pbcon.py 'reboot -f'`.
  The tty comes back `0666` via the installed
  `tools/99-playbook-cdc-acm.rules`; if it does not, `pkexec chmod 666
  /dev/ttyACM0` **only after a genuine re-enumeration**, and **wait for the
  operator** rather than working around it (trap 191).  After a kernel flash
  the ACM gadget may need a **physical replug** (trap 184); a console that
  echoes but prints nothing is a **busy shell** (trap 164).
* **The oracle** (`0fca:8012`): `tools/QCL.py info|exec|put|rm`,
  `tools/pbssh.sh 'cmd'` as root.  Reboot it with `QCL.py exec
  /proc/boot/shutdown -S reboot -f`.
* Device password `0000`, once.  An agent **never writes RPMB**; NVRAM is
  read-only (SAFETY §1/§6).  The image's UFS top-up keeps `BOOTCTR_FCTRY` at
  `0x32`.
* WM8994 register work: `i2ctransfer` arguments are **DECIMAL** (trap 166);
  bus `4`, address `0x1a`, 16-bit registers.  A frozen codec drops raw writes
  too (trap 167).  On the oracle use `pbi2c wr16r` (trap 176); on Linux the
  regmap debugfs (`/sys/kernel/debug/regmap/3-001a/registers`) is read-only and
  safe.
* QNX device tools build **`-static`**; `/tmp` is wiped by a reboot.

## 4. Traps added last session (full list 14–206 in `KNOWN-ISSUES.md` §2)

* **201.** The ABE serial port is a **firmware data path**, not a clock source.
* **202.** The reference's `StereoAudioCodecRequestMcbspClk`/`RequestABEClk`
  are stubs; its playback port is a codec-master slave.
* **203.** A McBSP clock **consumer** needs its `clkx`/`fsx` pads
  input-enabled (`0x0100`), not `0x0000`.
* **204.** The WM8994's AIF1 is on **McBSP3** (PDM pads mode 1), not
  McBSP1/2; `mcbsp1_dx` is the AIF4/MCBSP1 route.
* **205.** The `omap-mcbsp` `hw_params` early-return must compare the channel
  layout (`XPHASE`), not only rate/width; otherwise the right slot is silent.
* **206.** User-space reads of an idle L4_ABE McBSP register abort
  (`SIGBUS`); read only the active target.
* Corrected in place: **192** (data ball), **194** (nets), **199** (ABE
  clocking).

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **minimal 1917 oracle** | `tools/build_minimal_qnx.py`, `local/work/minimal/` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| **ABE/HAL Addendum (SWPU245F)** | `hardware/abe-hal-addendum.txt` (PDF gitignored) |
| TI TRM | `hardware/swpu231ap.pdf.txt` |
| WM8994 datasheet | `hardware/WM8994.PDF` (gitignored) |
| reference codec captures | `docs/captures/wm8994-*` |
| **working-audio capture** | `docs/captures/wm8994-e10-mcbsp3-2026-11-13.txt` |
| 1917 audio driver (RE'd) | `local/work/1917/base/lib/dll/deva-ctrl-omap4abe-wm8994.so` |
| 1917 ABE HAL | `local/work/1917/base/lib/libabe.so.1` |
| audio machine driver | `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` |
| McBSP driver (patched) | `local/kernel-tree/sound/soc/ti/omap-mcbsp.c` |
| OSS player / mixer | `rootfs/pbplay.c`, `rootfs/pbmix.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| console udev rule | `tools/99-playbook-cdc-acm.rules` |
| reports | `docs/reports/` (**214** is this session's working audio; 213 the ABE feasibility) |
| sessions | `docs/sessions/session-46.md` (last session) |

## 6. Working rules

* After every console command, dump the log tail and read it.  After a reboot or
  a structural change, **check the last 100–200 lines of `dmesg`**.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**
* **A test boot is not a budget concern.**
* **Never present a guess as a fact.**  External facts are looked up and cited;
  device facts prefer a fresh read over an old note.
* **Use the reference before the bench** — diff the live register file against a
  capture of the working reference (or against the oracle).
* **A listening verdict requires knowing what is playing** — check
  `/proc/asound/card0/pcm0p/sub0/status` and the `pbplay` line first.
* Subagents: static/file-only, never a `tools/` script (trap 109).
* **No restore is required at wrap-up.**  Leaving the unit on Linux is valid.

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 47 (absolute; Linux-era 41)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 9, KNOWN-ISSUES.md (traps 14-206, especially 187-206,
and §3), DEVELOPMENT.md §3/3a/4/7a, DECISIONS.md (44-59), docs/reports/INDEX.md,
reports 212, 213 and 214, and sessions/session-46.md.

WHERE IT STANDS: AUDIO WORKS.  `pbplay`'s 440 Hz tone is audible in BOTH ears
at 48 kHz +/-1% (operator, headphones).  The WM8994's AIF1 is on McBSP3, not
McBSP1/2 (the oracle's routing log "AIF 0 Downlink:
MM_DL_PORT->MM_EXT_OUT_PORT->MCBSP3"; the live McBSP3 PCR0=0x3 and the PDM pads
0x106/0x108/0x10a/0x10c = 0x0109/0x0009/0x0109/0x0109).  The card is two ASoC
links: mcbsp2 + wm8994-aif1 (E5 clocks/codec, CBC_CFC) and mcbsp3 +
wm8994-aif2 (data, CBP_CFP, consumer).  The right channel needed the
omap-mcbsp channel-layout guard (XPHASE); XCR2 is 0x8041.  The ABE is NOT
needed (DECISIONS §58/§59).  The patch is 54 files / 8 440 lines, sha256
fd2774fb..., verified 0 rejects / 54/54.

THE ONE NEXT EXPERIMENT: the SPEAKER PATH.  The listening test so far was on
headphones; SPKMIX was never exercised.  The codec's speaker controls already
read routed (SPKL/SPKR DAC1 Switch=1, SPKL/SPKR Output Switch=1, Speaker
Switch=1, Speaker Volume=57, Speaker Mode=Class D, PM1 SPKOUTL/R_ENA set), so
the likely gate is board-level: the spk line (GPIO2_12/gpmc_a20, signal 0x93)
or the hp jack detect.  STEPS (one variable): unplug the headphones; run
`pbplay 45 0 /dev/dsp &` then `pbplay 40 440 /dev/adsp &`; read the raw codec
registers 0x0001/0x0003/0x0022/0x0023/0x0024/0x0025 from
/sys/kernel/debug/regmap/3-001a/registers, `pbmix list "SPK"`, and
/sys/kernel/debug/gpio (spk/hp).  If silent, boot the oracle (IFS at 0x825),
leave the headphones unplugged, io-audio + audio_out, and read the same
registers/GPIOs; diff.  IF the speaker sounds on Linux with the headphones
unplugged: done.  IF Linux is silent and the oracle sounds: a codec/board
register or the spk/hp gate — implement it in the machine driver/repair
(module-only, region-1 flash).  IF both are silent: the speaker amp/rail or the
jack switch is the limit; record and stop.  Do not change the working two-link
audio path in the same boot.

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE 0xF8BADFF3 (0x93993C, kernel
7.3.3 #42, the McBSP3 two-link card).  REGION 1 holds the audio/OSS rootfs
c0719d28... (67,108,864 B, 27 modules, the stereo-fixed omap-mcbsp).  The unit
is on LINUX (0fca:8010, console `pb> `); BOOTCTR_FCTRY read 0x20 at this boot
(the /init log says `0x20 >= threshold 0x1e, no write`).  To boot the ORACLE:
flash local/work/ifs-shim-sealed.ifs at 0x825 (the pruned RCFS is still at
0x8D5).  To stock TabletOS: flash local/work/minimal/wayback.signed.  Do NOT
restore local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

BENCH: the unit is on OUR LINUX IMAGE (0fca:8010, console `pb> `).  qconn/ssh
are available only when the oracle is booted (flash
local/work/ifs-shim-sealed.ifs at 0x825): `tools/QCL.py info|exec|put|rm`
(169.254.0.1:9090), `tools/pbssh.sh 'cmd'` as root.  Reboot the oracle:
`QCL.py exec /proc/boot/shutdown -S reboot -f`.  The host link enp8s0f3u1
carries 169.254.0.2/16 (do not re-add).  To REBUILD the oracle:
`python3 tools/build_minimal_qnx.py`.  An OS 2.x .rcfs is a QNX6 filesystem —
NOT rcfs_extract.py (BB10-only), never rebuild from a tree.  Our Linux image has
NO door: tools/pbcon.py (prompt `pb> `), reboot with `tools/pbcon.py 'reboot
-f'`; the tty comes back 0666 via tools/99-playbook-cdc-acm.rules, otherwise
`pkexec chmod 666 /dev/ttyACM0` ONLY when the operator is present — wait for
them, do not work around it (trap 191); after a kernel flash the ACM gadget may
need a PHYSICAL USB REPLUG (trap 184); a console that echoes but prints nothing
is a BUSY SHELL (trap 164).  Run USB/flash tools as the USER, never pkexec.
Device password 0000, once.  An agent NEVER writes RPMB; NVRAM is read-only
(SAFETY §1/§6).  The oracle is UNAUTHENTICATED (qconn 9090, root ssh, firewall
off): bench only, never on a network (trap 173).  After every reboot, check
`pidin arg` and the services.  WM8994 register work: i2ctransfer arguments are
DECIMAL (trap 166); bus 4, addr 0x1a; a frozen codec drops raw writes too
(trap 167 — thaw with R0x200=0, re-enable AIF1CLK last).  On the oracle use
`pbi2c wr16r` (trap 176); on Linux use the regmap debugfs (read-only).  A
LISTENING TEST MUST STATE WHAT IS PLAYING: check
/proc/asound/card0/pcm0p/sub0/status and the pbplay line — `pbplay N 0` is
silence (trap 191).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4 (open_binary,
binary_overview, analyze_function, procedure_pseudo_code, search_strings,
xrefs, read_bytes); raw ARM import format=raw-binary, architecture=arm,
entry_point=0x80100000.  Reconcile with ntoarmv7-readelf/objdump (Ghidra may add
+0x10000 on an ELF).  QNX device tools build -static, and /tmp is wiped by a
reboot.  USE THE REFERENCE FIRST.  The WM8994 Rev 4.6 datasheet is LOCAL:
hardware/WM8994.PDF (gitignored) — Table 106 = AIF1 clocking codes, "AIF1
Control (1)" = the format, the Speaker Mixer Control = R34/R35.  The TI TRM is
hardware/swpu231ap.pdf.txt.  The ABE/HAL Addendum is LOCAL:
hardware/abe-hal-addendum.txt (SWPU245F).

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed;
a MODULE-only change keeps the CRC and needs a region-1-only flash (trap 190).
The UFS top-up keeps BOOTCTR_FCTRY at 0x32; the hard hazards (0 / >0x32 / failed
write / flat pack) remain.  Operator's call when to stop and to wrap up.

NEXT after the speaker experiment, in order: (a) retire the `pcm:`/`live N:`
diagnostics and re-verify the audio; (b) the MCLK1 probe (trap 165) is
optional — the working design does not need it; (c) touch cold-boot power/reset
(GPIO1_14/GPIO4_8); (d) the wl128x-nvs.bin path (hd5).  WL1283 remains stopped
by the operator.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note — and move replaced or
resolved material to docs/history/ or docs/parked/ with a link stub.  Re-export
the kernel patch whenever the DTS or a driver changes (trap 161) and verify it
by apply-and-compare, never by --dry-run alone.

WRAP-UP: only the operator decides when the session stops.  Record the bench
state, write the session note, update the canonical docs, verify with
tools/docs_audit.py and tools/tests/test_offline.py, then commit privately and
publish the snapshot (docs/project/REPO-MANAGEMENT.md).  No IFS restore is
required.
```
