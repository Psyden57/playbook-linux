# Handoff — session 47 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 47 = Linux-era 41**, updated at its
wrap-up.  See `docs/sessions/NUMBERING.md`.

> **Session-47 result, in one line:** **the speaker works too** — the board's
> speakers hang off the WM8994's **line outputs** (not its class-D amp), the
> `spk` line (GPIO2_12) is an **active-low** amplifier enable, and GPIO1_15
> (the reference's `audio_boost` enable) must be **high**.  Headphones and both
> speakers play the 440 Hz tone at 48 kHz (operator).  Report 215;
> DECISIONS §60.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain, the 64 MiB ext4 BusyBox root in region
1, the peripherals (I²C4 motion sensors, TMP103, BQ27541, FAN5702, CM3217,
ISL9519 charger), the **Munin touch** (calibrated, IRQ wired; §0d), and the
**WM8994 audio on headphones and speakers** (stereo, 48 kHz; §0c).

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
session-47 export (**54 files / 8 584 lines**, sha256 `93a7a8ce…`, 0 rejects /
54/54 byte-identical by apply-and-compare).  It carries the McBSP3 two-link
card, the `XPHASE` guard, the E5 clock roles, the jack-dependent output stage,
the line-out speaker route, the `spk`/`boost` GPIOs, the `disp-en-12-hog`
removal and the two session-45 diagnostics.

## 0a. The media, and the way back

* **region 0 = the session-47 Linux image** (`boot/chainloader/kernel.img`,
  CRC **`0xAD85DF0F`**, `0x939D3C` bytes; kernel `7.3.3` **#44**, the speaker
  path).
* **region 1 = the audio/OSS rootfs** `0342ab06…`
  (`local/images/rootfs-region1-audio-oss-linout.ext4`, 27 modules,
  `pbplay`/`pbmix`, the speaker-path machine driver).
* The unit is **on our Linux image** (`0fca:8010`, console `pb> `).  `BOOTCTR_FCTRY`
  read **`0x2c`** at the last boot (`0x2c >= threshold 0x1e, no write`); the
  image tops it up at boot.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825` (the pruned RCFS is still at `0x8D5`/region-0 `0xC00000`), then
  reboot.  It enumerates `0fca:8012`; qconn `169.254.0.1:9090`, root ssh.
  Note `/tmp` is wiped by the reboot — re-push any wav with `tools/QCL.py put`.
  **Device-side file left by session 47:** the oracle's persistent io-audio
  config `/var/etc/system/config/audio/OMAP4 @ 1a.io-ado-cfg` on the UFS has
  `Headphone Select` = **false**, so `io-audio` starts routed to the speaker
  (set it back to `true` if a headphone test is wanted).  Nothing was deployed
  to the Linux root.
* **To stock TabletOS:** flash `local/work/minimal/wayback.signed`.  **Do not
  restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).
* **To rebuild the oracle:** `python3 tools/build_minimal_qnx.py`.
* **Saved images:** `local/images/kernel-0xAD85DF0F-speaker.img` (the working
  region 0, session 47), `local/images/kernel-0xF8BADFF3-mcbsp3-consumer.img`
  (session 46), `local/images/rootfs-region1-audio-oss-linout.ext4`
  (`0342ab06…`, working), `…-stereo.ext4` (`c0719d28…`, session 46),
  `…-e9mcbsp3.ext4` (`a726bb49…`), `…-e5.ext4` (`6d92428e…`).

## 0b. Item 9 step 4 (WL1283) — REOPENED by the operator at the session-47 close

The **software surface is not exhausted after all**: it was closed on session 36
when the 1917-truth minimal `&mmc5` host still returned `attach_sdio no`
(report 196).  The speaker work (session 47) showed what "hardware blocked"
actually needed: **static RE of the reference driver/BSP plus the oracle's own
controls**, and the speaker fell out as a driver problem (report 215,
DECISIONS §60/§61).  The operator's direction for the next session is to apply
the same method to the WL1283.

**Method, in the order that worked:**

1. **RE the reference stack, not our DT.**  `local/work/1917/base/lib/dll/
   devnp-ti1283.so`, `devc-seromap-winbt`, `devpm-omap4.so`,
   `hwio-config-winchester.so`, and the powerman preset (`activity/wifi_driver`,
   `air_needs_wifi`, `wlan_needs_DMA`) — find every GPIO, PMIC/LDO and clock
   write the driver actually performs (the speaker's `spk`/`audio_boost` were
   only visible this way).  Put this in **subagents** (static, file-only,
   trap 109).
2. **Run the reference's own driver on the oracle with its own controls.**  The
   minimal 1917 image starts `net`; start `devnp-ti1283` (and `io-pkt` if it is
   not up), give the PPS/powerman services it expects, and watch the SDIO
   bring-up live — the exact analogue of starting `io-audio` for the speaker.
   `pidin mem`/`pidin irq` name the controller and the chip IRQ; `sloginfo`
   shows the driver's own decisions.
3. **Diff the two, then implement in the driver/DTS** — not with pokes
   (trap 209), and one variable per boot.

Do **not** re-run traps 116–137 unchanged; an overlapping test must state what
differs (report 196 ended with the pads/WL_EN/IRQ/rails matching, so the next
evidence has to come from the reference's *driver behaviour*, not another
SoC-side register sweep).  WL1283 stays item 9; the diagnostics cleanup below
is still the first, small task.

## 0c. Item 9 step 5 — the WM8994 audio works on headphones and speakers

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

### The speaker path — done (session 47, report 215)

The speaker was silent for three reasons and all three are now in the tree:

* **The speakers are on the codec's line outputs, not its class-D amplifier.**
  The reference plays with `PM1 = 0x0013`, `PM3 = 0x3cf0` (LINEOUT1P/N +
  LINEOUT2P/N), `R0x01e = 0`, `R0x034/0x035 = 1`, `R0x036 = 0`,
  `R0x038 = 0x0080` (`LINEOUT_VMID_BUF_ENA`), `R0x039 = 0x01c8`,
  `R0x04c = 0x1f25` (charge pump off), `R0x060 = 0`: DAC1 → MIXOUT → LINEOUT.
  The machine driver's repair now follows the jack: headphones → HPOUT1 +
  charge pump; jack out → the line-out block above.
* **`spk` (GPIO2_12) is an active-low amplifier enable.**  The reference leaves
  it high (amp off) and drives it **low** for the whole speaker playback,
  raising it in the teardown.  The `disp-en-12-hog` is gone; the driver owns it
  (`spk-gpios`) and sets it low for the speaker route, high on `trigger(STOP)`.
* **GPIO1_15 must be high.**  It is the reference's runtime-only board output
  and the `audio_boost` enable the QNX driver asks `powerman` for.  The driver
  owns it as `boost-gpios`, driven high at probe.

**Verified:** `repair: jack out (speaker)`, codec registers as above, `spk` out
`lo`, `boost` out `hi`; both speakers play the 440 Hz tone (operator) at DAC1
volume 96 and 40.  Nothing in the two-link data path changed.

### Driver facts you need before touching it

* The repair runs from a **delayed work 50 ms after `trigger(START)`** and
  begins with `regcache_mark_dirty()` + `regcache_sync()` (trap 162).
* `hw_params` writes `R0x200 = 0` (thaw), and the repair re-enables `AIF1CLK`
  **last**.
* A frozen codec drops **raw `i2ctransfer` writes too** (trap 167).
* `omap-mcbsp`, `snd-soc-wm8994` and the machine driver are **modules**: a
  change to them does **not** change the chainloader CRC and needs a
  **region-1-only** flash (trap 190).  Copy the rebuilt `.ko` into
  `local/work/modules-7.3.3/` first (trap 163).  A DTS change (like the `spk`
  hog removal) changes the image and needs **both** regions.
* **Never test a GPIO by a `devmem` poke:** the gpiolib shadow re-asserts the
  bank's data register on any other operation, so the pin reverts mid-stream
  (trap 209).  Let the driver own the line.
* **A listening test must state what is playing** (trap 191) and the two links
  must both be RUNNING: start the clock link first, wait, then the data link,
  or the second PCM fails rate symmetry and the first can stall at ~36 KB.
* **The ABE HAL Addendum is local:** `hardware/abe-hal-addendum.txt` (PDF
  gitignored).  The TRM is `hardware/swpu231ap.pdf.txt`; the WM8994 is
  `hardware/WM8994.PDF` — Table 106/“AIF1 Control (1)”, the line-output
  registers R30 (0x1e), R52/R53 (0x34/0x35) and the anti-pop R56/R57
  (0x38/0x39).

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
    --out local/images/rootfs-region1-audio-oss-linout.ext4
python3 tools/bbusb.py flash --component local/images/rootfs-region1-audio-oss-linout.ext4 \
    --dest-block 0x2915 --from-bootrom --password 0000 --apply &
tools/pbcon.py 'reboot -f'
```

Both regions (DTS or kernel change) use the `both.lst` recipe in
`DEVELOPMENT.md` §4.

**Patch export** (trap 161): regenerate from `local/pristine/linux-7.3.3` over
the old patch's file list (the `+++ b/` lines; `diff -u --label a/<path>
--label b/<path>`, `/dev/null` for new files), then **apply it to a staged
copy of the pristine files and compare all 54** to `local/kernel-tree` —
54/54 byte-identical is the acceptance test.  The session-47 export is
54 files / 8,584 lines, sha256 `93a7a8ce…`.

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
PlayBook (OMAP4430 "winchester") — session 48 (absolute; Linux-era 42)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 9, KNOWN-ISSUES.md (traps 14-210, especially 187-210,
and §3), DEVELOPMENT.md §3/3a/4/7a, DECISIONS.md (44-61), docs/reports/INDEX.md,
reports 213, 214 and 215, and sessions/session-47.md.

WHERE IT STANDS: AUDIO IS COMPLETE — headphones AND both speakers play
`pbplay`'s 440 Hz tone at 48 kHz +/-1% (operator).  The two-link data path is
session 46's: the WM8994's AIF1 is on McBSP3 (the oracle's routing log
"AIF 0 Downlink: MM_DL_PORT->MM_EXT_OUT_PORT->MCBSP3"), the card is
mcbsp2 + wm8994-aif1 (clocks, CBC_CFC) and mcbsp3 + wm8994-aif2 (data, CBP_CFP);
the codec is the AIF1 slave, FLL1 locks from our BCLK; the omap-mcbsp
channel-layout guard keeps XCR2 = 0x8041; the ABE is NOT needed (DECISIONS
§58/§59).  THE SPEAKER (session 47, report 215) needed three things, all now in
the tree: the speakers are on the codec's LINE OUTPUTS, not the class-D amp
(PM3 = 0x3cf0, R0x01e = 0, R0x034/R0x035 = 1, R0x036 = 0, R0x038 = 0x80,
R0x04c = 0x1f25, R0x060 = 0); `spk` (GPIO2_12) is an ACTIVE-LOW amplifier
enable (low while playing, high on trigger(STOP)); and GPIO1_15 (the
reference's `audio_boost` enable) must be HIGH.  The repair follows the jack:
headphones in -> HPOUT1 + charge pump; out -> the line-out route + spk low.
The patch is 54 files / 8 584 lines, sha256 93a7a8ce..., verified 0 rejects /
54/54.

THE ONE NEXT EXPERIMENT: retire the session-45 diagnostics and re-verify the
audio.  Remove the machine driver's `pcm:` DMA-ring readback
(playbook_wm8994_repair) and the omap-mcbsp eight-sample `live N:` dump
(omap_mcbsp_steady_dump), rebuild only the modules (trap 190: a module-only
change keeps the chainloader CRC), copy the .ko into local/work/modules-7.3.3/
(trap 163), rebuild the region-1 rootfs and flash region 1 only.  Then re-run
the pair and confirm: `repair: jack out (speaker)` / `jack in (headphone)`,
"done, FLL1 locked", the data stream writes its full byte count,
/proc/asound/card0/pcm1p/sub0/status RUNNING, and — BY EAR — headphones and
speakers.  Re-export the patch afterwards (it shrinks).  KEEP LISTENING TESTS
QUIET: lower `pbmix set "DAC1 Volume" 40` first and restore 96 afterwards; the
earlier full-volume tone was loud enough to bother the neighbours.

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE 0xAD85DF0F (0x939D3C, kernel
7.3.3 #44, the speaker path).  REGION 1 holds the audio/OSS rootfs 0342ab06...
(67,108,864 B, 27 modules).  The unit is on LINUX (0fca:8010, console `pb> `);
BOOTCTR_FCTRY read 0x2c at the last boot (`0x2c >= threshold 0x1e, no write`).
To boot the ORACLE: flash local/work/ifs-shim-sealed.ifs at 0x825 (the pruned
RCFS is still at 0x8D5).  /tmp is wiped by a reboot — re-push wavs with
QCL.py put.  To stock TabletOS: flash local/work/minimal/wayback.signed.  Do
NOT restore local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

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
`pbi2c rd16/wr16` (writes can fail while io-audio owns the codec: stop it
first); on Linux use the regmap debugfs (READ-ONLY and cached — a value there
is not proof of silicon; trap 207).  A LISTENING TEST MUST STATE WHAT IS
PLAYING: both PCMs RUNNING, /proc/asound/card0/pcm1p/sub0/status and the pbplay
line — `pbplay N 0` is silence (trap 191).  Start the clock link first and the
data link a few seconds later, or the second PCM fails rate symmetry and the
first can stall at ~36 KB (trap 208).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4 (open_binary,
binary_overview, analyze_function, procedure_pseudo_code, search_strings,
xrefs, read_bytes); raw ARM import format=raw-binary, architecture=arm,
entry_point=0x80100000.  Reconcile with ntoarmv7-readelf/objdump (Ghidra may add
+0x10000 on an ELF).  QNX device tools build -static, and /tmp is wiped by a
reboot.  USE THE REFERENCE FIRST, and put static RE in subagents (file-only,
trap 109).  The WM8994 Rev 4.6 datasheet is LOCAL: hardware/WM8994.PDF
(gitignored) — Table 106 = AIF1 clocking codes, "AIF1 Control (1)" = the format,
R30 = line outputs (0x1e), R52/R53 = line mixers (0x34/0x35), R56/R57 =
anti-pop (0x38/0x39).  The TI TRM is hardware/swpu231ap.pdf.txt.  The ABE/HAL
Addendum is LOCAL: hardware/abe-hal-addendum.txt (SWPU245F).

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed;
a MODULE-only change keeps the CRC and needs a region-1-only flash (trap 190).
The UFS top-up keeps BOOTCTR_FCTRY at 0x32; the hard hazards (0 / >0x32 / failed
write / flat pack) remain.  Operator's call when to stop and to wrap up.

NEXT after retiring the diagnostics: **the WL1283, REOPENED by the operator at
the session-47 close** (HANDOFF §0b).  Use the method that solved the speaker:
static RE of the reference stack in subagents (devnp-ti1283.so,
devc-seromap-winbt, devpm-omap4.so, hwio-config-winchester.so, the powerman
preset), then RUN the reference driver on the minimal oracle with its own
controls (start `devnp-ti1283` under `io-pkt`/powerman as `io-audio` was started
for audio), then diff and implement in the driver/DTS — never by pokes, one
variable per boot.  Do not re-run traps 116–137 unchanged.  Lower priority:
(a) the MCLK1 probe (trap 165) is optional — the working design does not need
it; (b) touch cold-boot power/reset (GPIO1_14/GPIO4_8); (c) identify the
GPIO1_15 `audio_boost` net if a schematic appears; (d) the wl128x-nvs.bin path
from the hd5 calibration for the post-enumeration wl12xx bring-up.

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
