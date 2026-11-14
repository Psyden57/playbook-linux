# Handoff — session 48 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 49 = Linux-era 43**, updated at its
wrap-up.  See `docs/sessions/NUMBERING.md`.

> **Session-48 result, in one line:** the bench now has **ssh over USB** —
> RNDIS + dropbear, `tools/pbssh.sh` at **2.3 MB/s** instead of the ACM
> console's 134 KB/s — and the session-45 audio diagnostics are retired and the
> audio re-verified on both outputs.  The kernel patch also gained
> `sound/soc/ti/omap-mcbsp-priv.h`, a file **every export since session 41 had
> omitted**, so the patch could not compile from pristine until now.
> Patch: **55 files / 8 500 lines**, sha256 `f5856a8b…`, 0 rejects / 55/55.
> Image `0x12C8C320` (kernel #45) + rootfs `a4e76b18…`.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console **and its RNDIS network link with ssh**, the whole display chain,
the 64 MiB ext4 BusyBox root in region 1, the peripherals (I²C4 motion sensors,
TMP103, BQ27541, FAN5702, CM3217, ISL9519 charger), the **Munin touch**
(calibrated, IRQ wired; §0d), and the **WM8994 audio on headphones and
speakers** (stereo, 48 kHz; §0c).

**The bench link is now ssh, not the console (session 48).**  With the unit on
our image:

```bash
tools/pbssh.sh 'uname -a'        # root shell over USB, key-only
tools/pbssh.sh 'dmesg | tail -40'
PB_LINK=usb tools/pbssh.sh 'cmd' # explicit; PB_LINK=door for the QNX door
```

2.3 MB/s, no dropped bytes, and it does not need `pkexec` or a replug.  Use it
for everything substantial; the ACM console stays for recovery and for when the
network stack is what broke.  The host's NetworkManager profile must be bound
to the composite gadget's **net** interface (`enp8s0f3u1i2`), not the plain
link name — trap 214.

**Interactive sessions work too.**  `ssh` from a real terminal gives
`root@playbook:~#` with history and completion (operator), and it was
reproduced from this side with `script -qec "ssh …" /dev/null`.  An agent's own
shell has **no tty** (`/proc/self/fd/0 -> /dev/null`), so an `ssh` launched from
one requests no pty and exits at once — do not read that as a defect
(trap 216).

**The audio item is closed, diagnostics included (session 48).**  The data path
is session 46's, the speaker route session 47's; session 48 only removed the
`pcm:`/`live N:` instrumentation and proved nothing else changed.

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
* **The right channel needs the `omap-mcbsp` channel-layout guard.**  The OSS
  first opens a stream mono at the final rate/width; the later 2-channel call
  would be skipped (rate/width only), leaving `XPHASE` clear (`XCR2 = 0x0041`).
  Comparing `XPHASE` as well gives `0x8041` and both slots play.
* **The speakers are the codec's line outputs**, `spk` (GPIO2_12) is an
  **active-low** amp enable driven low only while they play, and **GPIO1_15**
  (the reference's `audio_boost` enable) must be **high**.  The repair follows
  the jack: in → HPOUT1 + charge pump; out → the line-out route + `spk` low.

**The patch** `kernel/patches/0001-playbook-winchester-7.3.3.patch` is the
session-48 export (**55 files / 8 500 lines**, sha256 `f5856a8b…`, 0 rejects /
55/55 byte-identical by apply-and-compare).  It carries the McBSP3 two-link
card, the `XPHASE` guard with its `omap-mcbsp-priv.h` fields, the E5 clock
roles, the jack-dependent output stage, the line-out speaker route, the
`spk`/`boost` GPIOs and the `disp-en-12-hog` removal.  **Re-exporting it needs
a tree walk** — see trap 211 before you trust a file list.

## 0a. The media, and the way back

* **region 0 = the session-48 Linux image** (`boot/chainloader/kernel.img`,
  CRC **`0x12C8C320`**, `0x939D24` bytes; kernel `7.3.3` **#45** — the
  RNDIS + dropbear initramfs).  It was a region-0 change, so the CRC moved from
  `0xAD85DF0F`.
* **region 1 = the ssh rootfs** `a4e76b18…`
  (`local/images/rootfs-region1-rndis-ssh.ext4`, 27 modules,
  `pbplay`/`pbmix`/**dropbear**, the session-48 drivers).
* The unit is **on our Linux image** (`0fca:8010`, console `pb> `, **ssh on
  `169.254.0.1`**).  `BOOTCTR_FCTRY` read **`0x2a`** at the last boot
  (`0x2a >= threshold 0x1e, no write`); the image tops it up at boot.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825` (the pruned RCFS is still at `0x8D5`/region-0 `0xC00000`), then
  reboot.  It enumerates `0fca:8012`; qconn `169.254.0.1:9090`, root ssh — use
  **`PB_LINK=door tools/pbssh.sh`**, because the door is on the same address as
  dropbear but speaks only the old algorithms (trap 215).  Note `/tmp` is wiped
  by the reboot — re-push any wav with `tools/QCL.py put`.
  **Device-side file left by session 47:** the oracle's persistent io-audio
  config `/var/etc/system/config/audio/OMAP4 @ 1a.io-ado-cfg` on the UFS has
  `Headphone Select` = **false**, so `io-audio` starts routed to the speaker
  (set it back to `true` if a headphone test is wanted).  Nothing was deployed
  to the Linux root in session 48.
* **To stock TabletOS:** flash `local/work/minimal/wayback.signed`.  **Do not
  restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).
* **To rebuild the oracle:** `python3 tools/build_minimal_qnx.py`.
* **Saved images:** `local/images/kernel-0x12C8C320-rndis-ssh.img` (the working
  region 0), `local/images/rootfs-region1-rndis-ssh.ext4` (`a4e76b18…`, the
  working rootfs), `kernel-0xAD85DF0F-speaker.img` (session 47 region 0),
  `rootfs-region1-audio-oss-linout.ext4` (`af5a0a20…`, session 48
  diagnostics-retired), `…-stereo.ext4` (`c0719d28…`, session 46),
  `…-e9mcbsp3.ext4` (`a726bb49…`), `…-e5.ext4` (`6d92428e…`).

## 0b. Item 9 step 4 (WL1283) — REOPENED, and this is the next work

The **software surface is not exhausted**: it was closed on session 36 when the
1917-truth minimal `&mmc5` host still returned `attach_sdio no` (report 196).
The speaker work (session 47) showed what "hardware blocked" actually needed:
**static RE of the reference driver/BSP plus the oracle's own controls**, and
the speaker fell out as a driver problem (report 215, DECISIONS §60/§61).  The
operator's direction is to apply the same method to the WL1283.  The small
diagnostics cleanup that was scheduled first is **done** (session 48).

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

**The one experiment, with its inputs and its readings.**  Boot the oracle and
start the reference WLAN driver by hand.  **Not yet run — this is the plan, not a
transcript**; only the flash and reboot lines are known-good from earlier
sessions.

```bash
# 1. boot the oracle
nohup python3 tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs \
      --dest-block 0x825 --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
sleep 2; tools/pbcon.py 'reboot -f'          # the unit is normally on our Linux image
# 2. once 0fca:8012 and the door answer:
PB_LINK=door tools/pbssh.sh 'pidin arg | grep -i -E "io-pkt|ti1283|powerman"'
PB_LINK=door tools/pbssh.sh 'sloginfo -w'    # before: the driver's own decisions
# 3. start the driver.  The stack binary in the 1917 /base is io-pkt-v6-hc
#    (local/work/1917/base/sbin/); its own start line and devnp flags are what
#    the RE step has to establish — use those, not a guess.
PB_LINK=door tools/pbssh.sh '/base/sbin/io-pkt-v6-hc -d ti1283'
PB_LINK=door tools/pbssh.sh 'ifconfig ti0; sloginfo -w | tail -40'
```

**What each result means:**

| reading | meaning | next step |
|---|---|---|
| `ti0` appears and `ifconfig` shows RUNNING | the reference driver enumerates the chip on this unit; the chip and its rails are fine | capture its register writes (`pidin mem` + the RE'd write sites) and diff against our `&mmc5`/driver — the difference is host-side configuration we can implement |
| the driver logs CMD5/CMD3/CMD7 and then fails as it does under Linux | the reference driver *also* cannot enumerate, so the blocker is chip-side or supply-side, not our driver | the remaining evidence is electrical (VBAT/TCXO/reset), which the operator cannot probe (trap 165) — record it as blocked and stop |
| `devnp-ti1283` refuses to start (missing service, PPS, powerman resource) | the driver needs the powerman/PPS plumbing; that plumbing is exactly what a Linux port would need to provide | read the driver's dependency failures, then reproduce them as a checklist for the Linux side |
| the driver starts but no `ti0` and no CMD traffic | it is not reaching the controller (wrong UDC/`hwi` node, missing `WLAN_EN`) | capture `pidin mem` for the mapped MMIO and compare with report 180's `0x480D5000` binding |

Do **not** re-run traps 116–137 unchanged; an overlapping test must state what
differs (report 196 ended with the pads/WL_EN/IRQ/rails matching, so the next
evidence has to come from the reference's *driver behaviour*, not another
SoC-side register sweep).  Note the oracle is not on media at the start of a
session — flashing it is the first step, and the way back to our image is the
normal region-0 flash of `boot/chainloader/kernel.img` at `0x825`.

## 0c. Item 9 step 5 — the WM8994 audio is closed, both outputs

### What is proven (do not re-derive)

* **The transport** (E5): McBSP2 is the bit/frame provider, the codec is the
  AIF1 slave, FLL1 locks from our BCLK.  `SRGR1 = 0x0f1f`, `SRGR2 = 0x301f`,
  `PCR0 = 0x0f0f`, `R0x302 = 0x0000`, `R0x732 = 0x6020`, `R0x200 = 0x0011`.
  `CLKSM = 1` + `CLKGDV = 32` divides the 49.152 MHz ABE interface clock; the
  clock link measures ~48 096 frames/s.
* **The data engine is McBSP3** (report 214): `XCR2 = 0x8041` (stereo),
  `SPCR2 = 0x0235`, `XBUFFSTAT` toggling, the data stream writes its full byte
  count (the old failure was an 82 176-byte `EIO`).  The codec's DAC1 is fed
  from AIF1DAC1; the tone is audible in both ears at 48 kHz.
* **The speaker route** (report 215): the codec's **line outputs**,
  `PM3 = 0x3cf0`, `R0x01e = 0`, `R0x034/0x035 = 1`, `R0x036 = 0`,
  `R0x038 = 0x0080`, `R0x039 = 0x01c8`, `R0x04c = 0x1f25`, `R0x060 = 0`,
  with `spk` **low** and `boost` (GPIO1_15) **high**.
* **The header exists in the patch now** (`sound/soc/ti/omap-mcbsp-priv.h`) —
  without it the patch does not build.  Trap 211.

### Driver facts you need before touching it

* The repair runs from a **delayed work 50 ms after `trigger(START)`** and
  begins with `regcache_mark_dirty()` + `regcache_sync()` (trap 162).
* `hw_params` writes `R0x200 = 0` (thaw), and the repair re-enables `AIF1CLK`
  **last**.
* A frozen codec drops **raw `i2ctransfer` writes too** (trap 167).
* `omap-mcbsp`, `snd-soc-wm8994` and the machine driver are **modules**: a
  change to them does **not** change the chainloader CRC and needs a
  **region-1-only** flash (trap 190).  Copy the rebuilt `.ko` into
  `local/work/modules-7.3.3/` first (trap 163) and verify it landed in the
  image (`debugfs -R 'dump …'`) — a forgotten copy is silent.
* **Never test a GPIO by a `devmem` poke:** the gpiolib shadow re-asserts the
  bank's data register on any other operation (trap 209).
* **A listening test must state what is playing** (trap 191) and the two links
  must both be RUNNING: start the clock link first, wait a few seconds, then
  the data link (trap 208).
* **Keep listening tests quiet and short** (operator, session 48): lower
  `pbmix set "DAC1 Volume" 40` **once**, run every test, restore `96` **once at
  the end**.  A bare `pbplay` plays a 2 s tone — it is a playback command, not a
  usage query (trap 212).
* The WM8994 datasheet is local: `hardware/WM8994.PDF` — Table 106/“AIF1
  Control (1)”, the line-output registers R30 (0x1e), R52/R53 (0x34/0x35) and
  the anti-pop R56/R57 (0x38/0x39).

## 0d. Touch — done, and it survives a warm reboot; cold boots need the pulse

I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` boots the Munin app
and maps the 1917 `scaling.conf` calibration to 1024×600; the GPIO2_3 edge IRQ
works (pad `0x056`, `0x410B`, `irq 110`).  A warm reset keeps the controller up;
after a **cold** boot it does not until the power/reset GPIOs (**GPIO1_14 /
GPIO4_8**) are pulsed — the driver still does not drive them.

## 0e. The minimal 1917 oracle (session 43; report 207)

A debloated 2.1.0.1917 image that boots in ~30 s and gives a shell over USB.

* **Media:** region 0 = the shim IFS at `0x825` + the pruned RCFS at `0x8D5`
  (`local/work/minimal/setB.signed`).  It is **not** on media at the start of a
  session when the unit holds the Linux image — flash it to boot the oracle.
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
  on Linux is the safe codec path — and it needs
  `mount -t debugfs none /sys/kernel/debug` first on some boots.

## 1. Useful local extracts

* `local/work/1917/base/`, `ifs/`, `calib/`, `parts/` — the 1917 `/base`,
  IFS tree, WLAN calibration, pulled partitions.
* `local/vendor/bb10mt/bb10mt` — the QNX6/container tool.
* The minimal oracle — `tools/build_minimal_qnx.py`, `local/work/minimal/`.
* The WM8994 datasheet — Rev 4.6, `hardware/WM8994.PDF` (gitignored).
* **The ABE/HAL Addendum — SWPU245F**, `hardware/abe-hal-addendum.txt` (PDF
  gitignored; the public TRM's chapter 12 defers to it).
* Reports **207**, **208**, **209**, **212**, **213**, **214**, **215** (current);
  captures in `docs/captures/`
  (`wm8994-e10-mcbsp3-2026-11-13.txt` and `wm8994-speaker-2026-11-13.txt`).

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

**Patch export** (trap 161, and now trap 211): a tree walk is the check that
finds a file the previous patch never listed.  Regenerate from
`local/pristine/linux-7.3.3` over the last patch's file list **plus anything the
walk reports that is not a build artifact**, then apply to a staged copy of the
pristine files and compare all of them — 55/55 byte-identical is the acceptance
test.  The session-48 export is 55 files / 8 500 lines, sha256 `f5856a8b…`.

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
  regmap debugfs is read-only and safe (mount debugfs first).
* QNX device tools build **`-static`**; `/tmp` is wiped by a reboot.

## 4. Traps added last session (full list 14–218 in `KNOWN-ISSUES.md` §2)

* **211.** A patch export driven from the previous patch's file list **cannot
  find a file no earlier export ever listed**, and a byte-compare cannot either
  (a missing file is absent from both sides).  `omap-mcbsp-priv.h` had been
  missing since session 41.  Run the tree walk every time; read the file count
  as the signal.
* **212.** A bare `pbplay` **plays a 2 s tone** — it is a playback command, not
  a usage query.  And keep the listening volume down for the **whole** test
  sequence: lower once, test everything, restore once at the end.
* **213.** `build_rootfs.py --out` **overwrites** the path, and the saved
  rootfs image is the only record of a working userland.  Build to a new name.
* **214.** A composite gadget's **net** interface is `enp8s0f3u1i2`, not
  `enp8s0f3u1`: a NetworkManager profile pinned to the plain name stays down
  while both sides look "up".  Rebind `pb-rndis` to the interface carrying the
  host MAC.
* **215.** **Two ssh servers answer on `169.254.0.1:22`** — the QNX door
  (legacy algorithms) and dropbear (modern).  Probe qconn 9090 to decide, not
  the interface; `tools/pbssh.sh` does, and takes `PB_LINK=usb|door`.
* **216.** **An agent's shell has no tty**, so an `ssh` launched from one
  requests no pty and the remote shell exits at once — which looks exactly like
  "interactive ssh is broken".  It is not.  Check
  `ls -l /proc/self/fd/0` and use `script -qec "ssh …" /dev/null` to test
  interactive behaviour.
* **217.** The console getty sets `TERM` too, so a `$TERM` test cannot tell the
  console from ssh.  `/etc/profile` discriminates on `$(tty)` /
  `/dev/ttyGS0` and must keep `PS1='pb> '` there, or `tools/pbcon.py` loses the
  console.
* **218.** `mount -t devpts` **with no options** leaves `/dev/pts/ptmx` as
  `c---------`, so no pty can be allocated.  Use
  `-o gid=5,mode=620,ptmxmode=0666`.
* Still current from session 47: **207** (regmap debugfs reads the cache),
  **208** (clock link first), **209** (a `devmem` GPIO poke does not hold),
  **210** (line outputs, `spk` active low, GPIO1_15 high).

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
| **working-audio captures** | `…-e10-mcbsp3-2026-11-13.txt` (data path), `…-speaker-2026-11-13.txt` (speaker) |
| 1917 audio driver (RE'd) | `local/work/1917/base/lib/dll/deva-ctrl-omap4abe-wm8994.so` |
| 1917 ABE HAL | `local/work/1917/base/lib/libabe.so.1` |
| **1917 WLAN driver (to RE)** | `local/work/1917/base/lib/dll/devnp-ti1283.so` |
| audio machine driver | `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` |
| McBSP driver (patched) | `local/kernel-tree/sound/soc/ti/omap-mcbsp.c` (+ `omap-mcbsp-priv.h`) |
| OSS player / mixer | `rootfs/pbplay.c`, `rootfs/pbmix.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` (`PB_LINK=usb|door`) |
| **USB network gadget (RNDIS)** | `initramfs/init.c` (`gadget_setup`) |
| **ssh bring-up in the root** | `rootfs/etc/init.d/rcS` |
| **dropbear build + install** | `tools/build_rootfs.py` (`--dropbear`/`--hostkeys`/`--authorized-key`); DEVELOPMENT.md §3a |
| **dropbear binary / keys (local)** | `local/work/dropbear/`, `local/work/ssh/` (gitignored) |
| console udev rule | `tools/99-playbook-cdc-acm.rules` |
| reports | `docs/reports/` (**214** the data path, **215** the speaker) |
| sessions | `docs/sessions/session-48.md` (last session) |

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
  `/proc/asound/card0/pcm1p/sub0/status` and the `pbplay` line first, and keep
  the volume low (trap 212).
* Subagents: static/file-only, never a `tools/` script (trap 109).
* **No restore is required at wrap-up.**  Leaving the unit on Linux is valid.

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 49 (absolute; Linux-era 43)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 9, KNOWN-ISSUES.md (traps 14-218, especially 190-218,
and §3), DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md (57-62),
docs/reports/INDEX.md, reports 214 and 215, and sessions/session-48.md.

WHERE IT STANDS: AUDIO IS COMPLETE AND ITS DIAGNOSTICS ARE RETIRED, AND THE
BENCH NOW HAS SSH OVER USB.  Headphones and both speakers play `pbplay`'s 440 Hz
tone at 48 kHz +/-1% (operator, re-verified in session 48 at DAC1 volume 40).
The bench link is RNDIS + dropbear: `tools/pbssh.sh 'cmd'` is a root shell at
2.3 MB/s, versus the ACM console's 134 KB/s that drops bytes (trap 51).  PREFER
SSH; the console is the recovery path.  The two-link audio data path is session
46's: the WM8994's AIF1 is on McBSP3 (the oracle's routing log
"AIF 0 Downlink: MM_DL_PORT->MM_EXT_OUT_PORT->MCBSP3"), the card is
mcbsp2 + wm8994-aif1 (clocks, CBC_CFC) and mcbsp3 + wm8994-aif2 (data, CBP_CFP);
the codec is the AIF1 slave, FLL1 locks from our BCLK; the omap-mcbsp
channel-layout guard keeps XCR2 = 0x8041; the ABE is NOT needed (DECISIONS
§58/§59).  THE SPEAKER (session 47, report 215): the speakers are on the codec's
LINE OUTPUTS, not the class-D amp (PM3 = 0x3cf0, R0x01e = 0, R0x034/R0x035 = 1,
R0x036 = 0, R0x038 = 0x80, R0x04c = 0x1f25, R0x060 = 0); `spk` (GPIO2_12) is an
ACTIVE-LOW amplifier enable (low while playing, high on trigger(STOP)); and
GPIO1_15 (the reference's `audio_boost` enable) must be HIGH.  The repair
follows the jack.  The patch is 55 files / 8 500 lines, sha256 f5856a8b...,
verified 0 rejects / 55/55 — session 48 also added
sound/soc/ti/omap-mcbsp-priv.h, which every export since session 41 had
omitted (trap 211: the patch was not buildable from pristine before that).

THE ONE NEXT PIECE OF WORK: **the WL1283, REOPENED by the operator at the
session-47 close** (HANDOFF §0b).  The diagnostics cleanup that came first is
DONE.  Use the method that solved the speaker: static RE of the reference stack
in subagents (devnp-ti1283.so, devc-seromap-winbt, devpm-omap4.so,
hwio-config-winchester.so, the powerman preset), then RUN the reference driver
on the minimal oracle with its own controls (start `devnp-ti1283` under
`io-pkt`/powerman as `io-audio` was started for audio), then diff and implement
in the driver/DTS — never by pokes, one variable per boot.  Do not re-run traps
116-137 unchanged; an overlapping test must state what differs.  Lower
priority: (a) the MCLK1 probe (trap 165) is optional — the working design does
not need it; (b) touch cold-boot power/reset (GPIO1_14/GPIO4_8); (c) identify
the GPIO1_15 `audio_boost` net if a schematic appears; (d) the wl128x-nvs.bin
path from the hd5 calibration for the post-enumeration wl12xx bring-up.

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE 0x12C8C320 (0x939D24, kernel
7.3.3 #45 — the RNDIS + dropbear initramfs; a region-0 change, so the CRC moved
from 0xAD85DF0F).  REGION 1 holds the ssh rootfs a4e76b18... (67,108,864 B, 27
modules, dropbear + host keys + authorized_keys).  The unit is on LINUX
(0fca:8010, console `pb> `, ssh on 169.254.0.1); BOOTCTR_FCTRY read 0x2a at the
last boot (`0x2a >= threshold 0x1e, no write`).  To boot the ORACLE: flash
local/work/ifs-shim-sealed.ifs at 0x825 (the pruned RCFS is still at 0x8D5).
/tmp is wiped by a reboot — re-push wavs with QCL.py put.  To stock TabletOS:
flash local/work/minimal/wayback.signed.  Do NOT restore
local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

BENCH: the unit is on OUR LINUX IMAGE (0fca:8010, console `pb> `, and ssh).
`tools/pbssh.sh 'cmd'` is the DEFAULT ACCESS: it auto-detects the link (usb
when nothing answers on the door's qconn 9090) and PB_LINK=usb|door forces it.
Dropbear is key-only, root, 169.254.0.1:22, listening on that address only; the
key is local/secrets/rsa.  The HOST's pb-rndis NetworkManager profile must be
bound to the composite gadget's NET interface (enp8s0f3u1i2, MAC
02:1a:11:00:00:02), not the plain link name — trap 214.  TWO ssh servers can
answer on that address depending on which OS is booted (door = legacy
algorithms, dropbear = modern) — trap 215.
For the QNX oracle: flash local/work/ifs-shim-sealed.ifs at 0x825, then
`tools/QCL.py info|exec|put|rm`
(169.254.0.1:9090) or `PB_LINK=door tools/pbssh.sh 'cmd'` as root.  Reboot the
oracle: `QCL.py exec /proc/boot/shutdown -S reboot -f`.  To REBUILD the oracle:
`python3 tools/build_minimal_qnx.py`.  An OS 2.x .rcfs is a QNX6 filesystem —
NOT rcfs_extract.py (BB10-only), never rebuild from a tree.  Our Linux image has
NO door: tools/pbcon.py (prompt `pb> `), reboot with `tools/pbcon.py 'reboot
-f'`; the tty comes back 0666 via tools/99-playbook-cdc-acm.rules, otherwise
`pkexec chmod 666 /dev/ttyACM0` ONLY when the operator is present — wait for
them, do not work around it (trap 191); after a kernel flash the ACM gadget may
need a PHYSICAL USB REPLUG (trap 184); a console that echoes but prints nothing
is a BUSY SHELL (trap 164).  Run USB/flash tools as the USER, never pkexec.
Device password 0000, once.  An agent NEVER writes RPMB; NVRAM is read-only
(SAFETY §1/§6).  The packet filter is off and the address is link-local: bench
only, never on a network (trap 173) — that now applies to the unit's OWN ssh
server too (dropbear, key-only, on 169.254.0.1).  After every reboot, check
`pidin arg` and the services on the oracle.  WM8994 register work: i2ctransfer
arguments are DECIMAL (trap 166); bus 4, addr 0x1a; a frozen codec drops raw
writes too
(trap 167 — thaw with R0x200=0, re-enable AIF1CLK last).  On the oracle use
`pbi2c rd16/wr16` (writes can fail while io-audio owns the codec: stop it
first); on Linux use the regmap debugfs (READ-ONLY and cached — a value there
is not proof of silicon; trap 207), and mount debugfs first on some boots.

LISTENING TESTS ARE QUIET AND SHORT (operator, session 48): lower
`pbmix set "DAC1 Volume" 40` ONCE, run every test, restore 96 ONCE at the very
end — restoring between runs is what put two session-48 captures at full
volume.  A bare `pbplay` plays a 2 s tone (trap 212): it is a playback command,
not a usage query.  A LISTENING TEST MUST STATE WHAT IS PLAYING: both PCMs
RUNNING, /proc/asound/card0/pcm1p/sub0/status and the pbplay line — `pbplay N 0`
is silence (trap 191).  Start the clock link first and the data link a few
seconds later, or the second PCM fails rate symmetry (trap 208).

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

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note — and move replaced or
resolved material to docs/history/ or docs/parked/ with a link stub.  Re-export
the kernel patch whenever the DTS or a driver changes (trap 161) and verify it
by apply-and-compare, never by --dry-run alone — and run the TREE WALK too,
because a file that no earlier export listed is invisible to both (trap 211).

WRAP-UP: only the operator decides when the session stops.  Record the bench
state, write the session note, update the canonical docs, verify with
tools/docs_audit.py and tools/tests/test_offline.py, then commit privately and
publish the snapshot (docs/project/REPO-MANAGEMENT.md).  No IFS restore is
required.
```
