# Handoff — session 49 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 50 = Linux-era 44**, written at the
session-49 wrap-up.  See `docs/sessions/NUMBERING.md`.

> **Session-49 result, in one line:** the WL1283 host side is now excluded by
> measurement — the reference driver, loaded the way its own `wifi.sh` loads it,
> **brings the chip up live on the oracle**, and replaying its entire MMCHS5
> program, its WL_EN power-on ordering, the stock-TabletOS board state and the
> whole boot path all change nothing — **and the way "silence" was measured
> turned out to be invalid**, which is the session's most useful finding.
> Corrected, the card genuinely does not drive the line (`CTO` only, on a bus
> that is powered and clocked at 400 kHz/1.8 V), but `CTO` is a 160 µs window
> that cannot separate "no card" from "card slower than that" — hence the two
> next tests: the **BT-core HCI probe with `BT_EN` driven** (never done: session
> 29's ran without it) and a **slower identification clock** (§0b).
> Also found: `BT_EN` = gpio 55 = `GPIO2_23`; the GPIO1 pads 0x050/0x074 were
> driving GPMC alternate functions and are fixed; `CON = 0x85` **hangs** the
> rescan; and leaving MMCHS5 in the reference's command-phase state **wedges
> `reboot -f`** (`sysrq b` recovers).
> Image `0xBAFD9B3B` (kernel #57) + rootfs `a4e76b18…`; patch **56 files /
> 8 583 lines**, sha256 `455b45d8…` (the session-49 export adds the report-220
> board-DTS GPIO1 pad fix and `arch/arm/Kconfig.debug`, which had been missing
> since session 30 — see §0a).

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
(calibrated, IRQ wired), and the **WM8994 audio on headphones and speakers**
(stereo, 48 kHz).  **ssh over USB is the bench default** (`tools/pbssh.sh`,
2.3 MB/s); the ACM console is the recovery path.

**The one open item is the WL1283 (ROADMAP item 9), and this session moved it
from "the software surface looks exhausted" to "the host side is measured and
excluded".**  Details in §0b.

## 0a. The media, and the way back

* **region 0 = the session-49 image** (`boot/chainloader/kernel.img`,
  CRC **`0xBAFD9B3B`**, `0x93A384` bytes; kernel `7.3.3` **#57**).  It is the
  session-48 tree plus the **GPIO1 pad fix** of report 220 (three pads were
  driving GPMC alternate functions, trap 228) — the earlier `0x71A79419` is the
  same tree without it.
* **region 1 = the ssh rootfs** `a4e76b18…`
  (`local/images/rootfs-region1-rndis-ssh.ext4`, 27 modules,
  `pbplay`/`pbmix`/**dropbear**).
* The unit is **on our Linux image** (`0fca:8010`, console `pb> `, **ssh on
  `169.254.0.1`**).  `BOOTCTR_FCTRY` read **`0x27`** at the first boot
  (`>= threshold 0x1e, no write`); the image tops it up at boot.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`, then reboot.  It enumerates `0fca:8012`; qconn
  `169.254.0.1:9090`, root ssh — use **`PB_LINK=door tools/pbssh.sh`** (trap
  215).  `/tmp` is wiped by a reboot — re-push tools.
* **To stock TabletOS:** flash **two components** — `local/work/ifs-shim-sealed.ifs`
  at `0x825` **and** `local/work/rcfs-1917.rcfs` at `0x8D5`.
  `local/work/minimal/wayback.signed` does **not** boot this armed unit (vendor
  `os_type` 4 IFS + a vendor RCFS with no root hook; trap 226).  **Do not
  restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).
* **To rebuild the oracle:** `python3 tools/build_minimal_qnx.py`.
* **Device-side file left by session 49:** the oracle's `hd3` carries
  `/var/ti1283/{firmware.bin,tiwlan.ini}` (the reference WLAN's own staging
  directory; delete it for a clean oracle).
* **Saved images:** `local/images/kernel-0x12C8C320-rndis-ssh.img` (the
  session-48 region 0), `rootfs-region1-rndis-ssh.ext4` (`a4e76b18…`), and the
  older per-session images listed in `STATE.md`.

## 0b. The WL1283 — what session 49 settled, and the one experiment that is left

### What is now measured (do not re-derive)

* **The reference driver brings the chip up on this unit, live.**  Loaded
  exactly as `/base/scripts/wifi.sh` loads it, it logs
  `sdioAdapt_ConnectBus: Send commands sequence: 0, 5, 3, 7`, downloads its
  firmware (`Firmware Version: Rev 7.1.4.10.77`) and leaves `ti0`
  **UP,RUNNING** with MAC `14:74:11:8f:9c:c3`; on the pins `GPIO2_DATAOUT`
  bit 22 **1** (WL_EN) and `GPIO2_DATAIN` bit 21 **1** (WL_IRQ).
  Capture: `docs/captures/wlan-oracle-up-2026-11-14.txt`.
* **The reference's load path** (report 217 §1): stage `firmware.bin` + the ini
  in `/var/ti1283`, then
  `mount -Tio-pkt -o nvram,firmdir=/var/ti1283/ /lib/dll/devnp-ti1283.so`, then
  **`ifconfig ti0 up`**.  `-o nvram` supplies the NVS (468 B from NVRAM);
  `nvs_map.bin` exists in no image.  The driver loads the **firmware before**
  the bus connect, so a missing file looks like a dead card.  `ti0` comes up
  with MAC `ff:…` and stays down until the `ifconfig`.
* **Our Linux image is register-identical apart from one bit** — WL_EN high,
  pads `0x07C = 0x00030003`, `0x148/0x14C/0x150 = 0x01180118`,
  `GPIO2_OE = 0xCE2ECDFF` — and WL_IRQ never goes high
  (`GPIO2_DATAIN = 0x01000010`).
* **Replaying the reference's MMCHS5 program is negative** (`0x04473A45`,
  kernel #47): `SYSCONFIG = (v & ~0x331d) | 0x320d`, `SOFTRESET` + poll, `SRA`,
  `HCTL |= SDVS18`, `CAPA |= VS18`, `SDBP`, `ISE = 0`, `IE = 0x317f0137`,
  `STAT = ~0`, `CON = 0x85`, early `DTW` →
  `PBREF: … CON=0x685 HCTL=0xb02 SYSCTL=0xe1e07 CAPA=0x04e10080`, card silent.
* **Replaying the power-on ordering is negative** (`0x0C48AE8A`, kernel #49):
  the WL_EN hog removed, `wl-en-gpios` on `&mmc5`, EN driven low → 20 ms →
  high → 120 ms in `set_ios(MMC_POWER_UP)` → `Failed to initialize a
  non-removable card`.
* **The BT half is a separate thing** (report 216): `BT_EN` = **gpio 55 =
  `GPIO2_23`**, pad `0x4A10007C` bits 31:16, driven low → 50 ms → high by
  **`btp-btmgr`** (devctl `0x80040501`), and it is **low while the WLAN
  works** — so it is not the WLAN's blocker, but a Linux BT bring-up cannot
  answer `HCI_Reset` until that pad is muxed and the line pulsed.

### Two audits run at the operator's direction (reports 219, 220)

* **The trap-185 audit is closed**: the WL1283 work never used a sysfs global
  GPIO number — every pin was a DT phandle or an absolute bank address.  But the
  same class in its **pad** form was real: `0x4A100050` and `0x4A100074` were at
  mode 0, driving `gpmc_ad0/1` and `gpmc_ncs0/1` on a board with no GPMC device
  while gpiolib could not touch them, and `0x4A31E058` (GPIO1_31) was unmuxed
  with its input buffer off.  **Fixed in the DTS** (mode 3 + pull-down + four
  `output-low` hogs); `GPIO1_OE` is now `0x6FFB1FBA` vs the reference's
  `0x6FFB1FAA` and `GPIO1_DATAOUT` is identical.  It did **not** move the card.
* **The boot-path RE** (all of `startup-playbook`'s 144 stores, the IFS boot
  order, `pmic_twl6030_cfg`'s 38 TWL writes, REGEN1/2) found **one** new writer
  Linux never runs — `pmic_twl6030_cfg` — and the live cross-OS captures refute
  it: 15 of its values are overridden before TabletOS's own read, and no
  remaining one is a rail the WLAN needs.  `RM_L4PER_MMCSD5_CONTEXT` is never
  written by the boot path at all.  Report 219.

### The next experiment: is the chip's digital core running at all?

The stock-TabletOS comparison is **done and negative** (report 218): 136 of
1 120 words differ and none is WLAN-relevant.  The boot-path RE is **done and
negative** (221–226), and the one clock-domain difference was **tested and
negative** (225).  What is *not* known is whether the chip runs at all, because
until report 227 "silent" was inferred from a board GPIO — `WL_IRQ`, which the
reference also reads **0** before its driver runs.

**Test 1 (do this first): the BT-core liveness probe, done properly.**

`BT_EN` (gpio 55 = `GPIO2_23`, pad `0x4A10007C` bits 31:16) was only discovered
in session 49 (report 216), and **session 29's HCI probe ran without it** — so
that probe is invalid, not negative.

* Inputs, all already in the tree or the image: UART2 `0x4806c000` =
  `/dev/ttyS1` (irq 103, `&uart2 { status = "okay" }`, pads `0x11c`/`0x11e` =
  `0x4118`/`0x0000`); `BT_EN` free (not hogged — `/sys/kernel/debug/gpio` shows
  only `gpio-20` and `gpio-22` claimed); 115200 8N1.
* Steps: drive `BT_EN` **low → 50 ms → high**, then write `HCI_Reset`
  (`01 03 0c 00`), then read for the command-complete event
  (`04 0e 04 01 03 0c 00`).  A ~40-line static ARM tool, or sysfs + a shell
  write; no kernel change.
* **Result meanings.**  *A reply* → the die is powered and its digital core
  runs, so the fault is SDIO-path-specific (interface not enabled, or the CMD
  path only) and every power/TCXO/reset hypothesis dies.  *Silence* → the chip
  is not powered/clocked, and the SDIO controller is no longer a suspect.
  *Neither, but `BT_EN`'s pad reads wrong* → fix the pad first; `0x07C`'s high
  half must be mux 3.

**Test 2 (cheap, one boot): lower the identification clock.**  `CTO` is a fixed
**64 card clocks** (TRM Table 24-102), so 400 kHz gives a 160 µs window and
**100 kHz gives 640 µs**.  `f_min` is `OMAP_MMC_MIN_CLOCK` (`400000`) in
`omap_hsmmc.c`.  This settles report 227 §2's one doubt: "no card" versus "card
slower than 160 µs".  A card that answers at 100 kHz and not at 400 kHz is the
whole bug.

**Test 3 (the endgame): the QNX-in-Linux harness.**  Run `devnp-ti1283.so`'s
bring-up under a shim (`mmap_device_io`, `devctl`, `InterruptAttachEvent`,
`slogf`, `ThreadCtl`, `resmgr_*`/io-net registration stubs) and call
`sdioAdapt_ConnectBus`, so its exact sequence executes on this silicon.  More
valuable than it looked before: `CON = 0x85` proved the reconciliation can be
subtly wrong (its `HR` bit hangs the rescan — trap 233).

One candidate is newly *open* rather than closed: the WL1283 needs a 32.768 kHz
SLOW_CLK, and the "`CLK32KG` is ON in both OSes" conclusion was based on the
register at `0x8C` — which is **VAUX3** (KNOWN-ISSUES §3.30).  The 32 kHz has
therefore never actually been verified on either OS.  The TWL6030's
`CLK32KG` is module index 48 in the PM slave-misc block; read it on both sides
before trusting any 32 kHz claim.

Do **not** re-run: traps 116–137, the two replays of session 49 (217), the
boot-path RE (221–226), the domain test (225), or the readable-register diff
(218).  An overlapping test must state what differs.

## 0c. WM8994 audio — closed

Headphones and both speakers play `pbplay`'s 440 Hz tone at 48 kHz ±1 %
(operator, re-verified in session 48 at DAC1 volume 40).  The path is session
46's (AIF1 on **McBSP3**; card = `mcbsp2 + wm8994-aif1` clocks and
`mcbsp3 + wm8994-aif2` data; codec is the AIF1 slave with FLL1 from our BCLK;
`omap-mcbsp`'s `XPHASE` guard keeps `XCR2 = 0x8041`; no ABE), and the speaker
route is session 47's (codec **line outputs**, `spk` = GPIO2_12 **active low**,
GPIO1_15 `audio_boost` **high**).  Nothing required is left; the MCLK1 probe
(trap 165) stays optional.

Listening tests: lower `pbmix set "DAC1 Volume" 40` **once**, run every test,
restore `96` **once at the end**; a bare `pbplay` plays a 2 s tone (trap 212);
state what is playing and that both PCMs are `RUNNING` (traps 191/208).

## 0d. Touch — done, survives a warm reboot; cold boots need the pulse

I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` maps the 1917
`scaling.conf` calibration to 1024×600; GPIO2_3 edge IRQ works (`irq 110`).  A
**cold** boot leaves the controller down (`0x65` NAKs) until GPIO1_14/GPIO4_8
are pulsed — the driver still does not drive them.

## 0e. The minimal 1917 oracle (and what it cannot do)

* **Media:** region 0 = the shim IFS at `0x825` + the pruned RCFS at `0x8D5`.
* **Connect:** `python3 tools/QCL.py info` (qconn 9090) or
  `tools/pbssh.sh` (root; `PB_LINK=door`).  The host's `pb-rndis` profile must
  bind the **plain** link name for the oracle (`enp8s0f3u1`) and
  `enp8s0f3u1i2` for our composite gadget (trap 214) — rebind with `nmcli` when
  you switch OSes.
* **Its `/base` is pruned**: no `/base/lib/firmware/ti1283`.  For WLAN work
  push `firmware.bin` and the ini to `/var/ti1283` (hd3, persistent) and load
  with `-o nvram`.
* **No wifi service runs**, so nothing brings `ti0` up — do it by hand.
* **Tools are not on the UFS**: push `memdump3`/`memw32`/`pbi2c` from
  `local/work/minimal/` to `/tmp` after every reboot.
* `/tmp` is wiped by a reboot; a reboot restores volatile register state.
* **Bench only:** unauthenticated qconn/root ssh — never on a network (trap 173).

## 1. Useful local extracts

* `local/work/1917/base/`, `ifs/`, `calib/`, `parts/` — the 1917 `/base`, IFS
  tree, WLAN calibration (hd5) and pulled partitions.
* `local/work/1917/base/scripts/wifi.sh` — **the reference's WLAN load path**.
* `local/work/1917/base/lib/dll/devnp-ti1283.so`, `devc-seromap-winbt`.
* `local/vendor/bb10mt/bb10mt` — the QNX6/container tool.
* The minimal oracle — `tools/build_minimal_qnx.py`, `local/work/minimal/`.
* `docs/captures/wlan-oracle-up-2026-11-14.txt` — the reference bring-up.
* `docs/reference/qnx-hwio/{pad,gpio}-table-winchester.csv` + README.
* Reports **216**, **217** (current), **214**, **215**; the WM8994 datasheet
  `hardware/WM8994.PDF` (gitignored), the TI TRM
  `hardware/swpu231ap.pdf.txt`, the ABE/HAL Addendum
  `hardware/abe-hal-addendum.txt`.

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

**The CRC is per-build** (the kernel version string carries the build time), so
it identifies an image but cannot be used to prove a source revert — compare
files against `local/pristine/linux-7.3.3` + the patch instead (§ below).

**A module-only change keeps the CRC** (trap 190): build `modules`, copy the
`.ko` into `local/work/modules-7.3.3/` (trap 163), rebuild the rootfs and flash
**region 1 only** (block `0x2915`).

**Patch export** (traps 161/211): drive the file list from the tree (a walk —
after `make clean`, or restricted to known source paths), apply the result to a
staged copy of pristine and compare every file.  The session-49 export is
**56 files / 8 583 lines**, sha256 `455b45d8…`, 0 rejects / 56/56 — the
session-49 export, which adds the report-220 board-DTS GPIO1 pad fix and
`arch/arm/Kconfig.debug` (missing from every earlier export since session 30,
trap 211 class).

## 3. Bench

* **`tools/pbssh.sh 'cmd'` is the default access** (root, key-only,
  `169.254.0.1`, 2.3 MB/s).  `PB_LINK=usb|door`.  The host profile must be
  bound to the **net** interface of whichever OS is booted (trap 214).
* **Reboot:** `tools/pbcon.py 'reboot -f'` (our image) or
  `tools/pbssh.sh 'reboot -f'`.  **If `reboot` hangs** (a wedged controller can
  do it — trap 224), use `tools/pbssh.sh 'echo b > /proc/sysrq-trigger'`: sysrq
  skips device shutdown and the bootrom window opens immediately.  Arm the
  flasher first, or start it with `--from-bootrom` and reboot within ~2 s.
* **Never `slay` by name on the oracle** — `slay -f io-pkt-v6-hc` kills the
  stack that carries `rndis0` and costs a power cycle.  Kill by pid (trap 221).
* **`pin`/`slay`:** `pidin arg` to find pids.
* Device password `0000`, once.  An agent **never writes RPMB**; NVRAM is
  read-only (SAFETY §1/§6).  The image's UFS top-up keeps `BOOTCTR_FCTRY` at
  `0x32`; read it as a health check.
* WM8994 register work: `i2ctransfer` arguments are **DECIMAL** (trap 166);
  bus 4, addr `0x1a`.  A frozen codec drops raw writes (trap 167).  On the
  oracle use `pbi2c rd16/wr16`; on Linux the regmap debugfs is a *cache*
  (trap 207).
* QNX device tools build **`-static`**; `/tmp` is wiped by a reboot.

## 4. Traps added last session (full list 14–233 in `KNOWN-ISSUES.md` §2)

* **219.** `BT_EN` = gpio 55 = `GPIO2_23` (pad `0x07C` bits 31:16), driven by
  `btp-btmgr`, held **low** while the WLAN works.  Not the WLAN's blocker; a
  Linux BT bring-up needs the pad muxed and the line pulsed.
* **220.** The reference loads the WLAN **firmware before** the bus connect, and
  its NVS comes from NVRAM (`-o nvram`) — `nvs_map.bin` exists in no image.
  `ti0` stays down until `ifconfig ti0 up`.
* **221.** `slay -f io-pkt-v6-hc` kills the stack that carries `rndis0`; kill
  strays by pid (trap 172 restated).
* **222.** io-pkt's `-d` takes whitespace-separated driver options;
  `-d name,opt=…` and full paths fail as `Library cannot be found`.  `-i1`
  gives an isolated stack (`mount -Tio-pkt1`).
* **223.** `SRA` clears `CEN`/`ICE`: reset first, then program the clock, or the
  controller runs with no card clock.
* **224.** An MMCHS5 left in the reference's command-phase state **wedges
  `reboot -f`**; `sysrq b` recovers.
* **225.** `0x48055000..0x3C` is the GPIO **interrupt** block; `OE`/`DATAIN`/
  `DATAOUT` are at `+0x134`/`+0x138`/`+0x13C`.  And `memw32 <addr> <val>
  <mask>` wants `val` **already masked**.
* **226.** `wayback.signed` does **not** boot this armed unit (vendor `os_type` 4
  IFS halts; its RCFS has no root hook).  The way back is `ifs-shim-sealed.ifs`
  at `0x825` **+** `rcfs-1917.rcfs` at `0x8D5`.
* **227.** The region-0 RCFS slot now holds the **full** 1917 RCFS, not the
  pruned oracle one; `setB.signed` restores the pruned pair.
* **228.** A GPIO that is "the right line" can still be **undriven**: check the
  **pad mux**, not just the number.  `0x4A100050`/`0x4A100074` were at mode 0,
  driving `gpmc_ad0/1` and `gpmc_ncs0/1` on a board with no GPMC device.
* **229.** A pinctrl group **cannot** re-mux a pad another group owns; the whole
  group is dropped (`pin PIN36 already requested`).  `0x058`'s low half is
  GPIO2_4, owned by `bridge_pins`.
* **230.** `&omap4_pmx_wkup` offsets are relative to `0x4A31E040` and the block
  is only `0x38` long — pad `0x4A31E058` is offset **`0x18`**, not `0x058`.
* **231.** **`WL_IRQ` (GPIO2_21) is not a liveness signal** — the reference reads
  0 too until its driver has run.  A liveness claim needs a decoded card
  response, not a board GPIO.
* **232.** `MMCHS_STAT` bit 16 is `CTO`, bit 15 is the `ERRI` umbrella.  `CTO` =
  "no response within **64 clock cycles**", so a raw `0x18000` is one grouped
  timeout, and it cannot separate "no card" from "slower than the window".
  Decode `STAT`; the errno collapses `CTO` and `DTO`.
* **233.** Read the bus state **at the instant of the command**
  (`CEN`/`ICE`/`CLKD`/`SDBP`/`SDVS`/`CON` in `start_command()`), not after a
  failed probe, which is already torn down.
* Still current: **207** (regmap debugfs reads the cache), **208** (clock link
  first), **209** (a `devmem` GPIO poke does not hold), **210** (line outputs,
  `spk` active low, GPIO1_15 high), **211** (tree walk for the patch), **212**
  (a bare `pbplay` plays), **213** (`build_rootfs.py --out` overwrites), **214**
  (composite net interface), **215** (two ssh servers), **216** (an agent shell
  has no tty), **217** (`$TERM` cannot tell console from ssh), **218**
  (`devpts` needs options).
* **Prefer `sloginfo` unfiltered.**  A filter hid the reference's
  `Send commands sequence` line and produced a wrong conclusion this session.

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **minimal 1917 oracle** | `tools/build_minimal_qnx.py`, `local/work/minimal/` |
| **reference WLAN load path** | `local/work/1917/base/scripts/wifi.sh` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| TI TRM | `hardware/swpu231ap.pdf.txt` |
| hwio tables (pads, GPIOs) | `docs/reference/qnx-hwio/` |
| **reference bring-up capture** | `docs/captures/wlan-oracle-up-2026-11-14.txt` |
| **cross-OS state captures** | `docs/captures/wlan-{qnx,linux}-state-2026-11-14.txt` |
| audio machine driver | `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` |
| OSS player / mixer | `rootfs/pbplay.c`, `rootfs/pbmix.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` (`PB_LINK=usb|door`) |
| **USB network gadget (RNDIS)** | `initramfs/init.c` (`gadget_setup`) |
| ssh bring-up in the root | `rootfs/etc/init.d/rcS` |
| reports | `docs/reports/` (**216** BT, **217** WLAN replay, **218** stock-OS diff, **219** boot-path RE, **220** pad audit) |
| sessions | `docs/sessions/session-49.md` (last session) |

## 6. Working rules

* After every console command, dump the log tail and read it.  After a reboot or
  a structural change, read the last 100–200 lines of `dmesg`/`sloginfo`
  **unfiltered**.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**
* **A test boot is not a budget concern.**
* **Never present a guess as a fact.**  External facts are looked up and cited;
  device facts prefer a fresh read over an old note.
* **Use the reference before the bench** — and note that the oracle is a
  *minimal* reference: for WLAN it needed the firmware and NVS supplied by hand.
* Subagents: static/file-only, never a `tools/` script (trap 109).
* **Revert experiments and prove the revert** (DTS vs `kernel/source`, driver vs
  pristine + patch) before wrapping up.
* **No restore is required at wrap-up.**  Leaving the unit on Linux is valid.

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 50 (absolute; Linux-era 44)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 9, KNOWN-ISSUES.md §2 (traps 14-225, especially 190-225)
and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md (60-63),
docs/reports/INDEX.md, reports 216 and 217, and sessions/session-49.md.

WHERE IT STANDS: audio is complete (headphones + both speakers, 48 kHz, session
48) and the bench has ssh over USB (`tools/pbssh.sh`, 2.3 MB/s; the ACM console
is the recovery path).  THE ONE OPEN ITEM IS THE WL1283, and session 49 settled
the host side: the reference driver, loaded as its own wifi.sh loads it
(`-o nvram`, firmware in /var/ti1283, then `ifconfig ti0 up`), brings the chip up
LIVE on the oracle (ti0 RUNNING, MAC 14:74:11:8f:9c:c3, WL_IRQ high), while the
same unit under Linux is register-identical apart from that one bit; replaying
the reference's ENTIRE MMCHS5 program and its WL_EN power-on ordering changed
nothing (both reverted; tree == pristine + patch).  The BT half's enable is
BT_EN = gpio 55 = GPIO2_23, driven by btp-btmgr and held LOW while the WLAN
works (report 216) — not the WLAN's blocker.

THE STOCK-TABLETOS COMPARISON IS DONE AND NEGATIVE (report 218): 136 of 1120
words differ and none is WLAN-relevant.  The boot-path RE is done and negative
(221-226) and the one clock-domain difference was tested and negative (225).

THE MEASUREMENT WAS ALSO WRONG, AND THAT IS THE NEW STARTING POINT (report 227).
"Silent" had been inferred from WL_IRQ (GPIO2_21) since session 33 — but the
reference's own chip reads 0 too until its driver runs, so that compared our
pre-driver state with the reference's post-firmware state.  With the decoded
traces restored (kernel/archive/wl1283/measurement-instrumentation-2026-11-14.patch)
the real reading is: at the instant of CMD52/CMD5 the bus is correct (CEN=1,
ICE=1, CLKD=120 = 400 kHz, SDBP=1, SDVS=5 = 1.8 V, CON=0x600), STAT=0x18000
decodes as CTO ONLY (CCRC=CEB=CIE=0, RSP0=0, PSTATE CLEV=1), and CMD0 succeeds.
CON=0x85 must NOT be copied — its HR bit suppresses the command and hangs the
rescan (trap 233).  CTO is a fixed 64-card-clock window, i.e. 160 us at 400 kHz,
which cannot separate "no card" from "card slower than 160 us".

THE NEXT EXPERIMENT (do these in order):
(a) THE BT-CORE LIVENESS PROBE, DONE PROPERLY.  Drive BT_EN (gpio 55 = GPIO2_23,
    pad 0x4A10007C bits 31:16) low -> 50 ms -> high, then send HCI_Reset
    (01 03 0c 00) on /dev/ttyS1 (UART2 0x4806c000, irq 103); read for 04 0e 04 01
    03 0c 00.  Session 29's probe ran WITHOUT BT_EN and is invalid, not negative.
    A reply proves the die is powered and its digital core runs -> the fault is
    SDIO-path-specific and every power/TCXO/reset hypothesis dies.  Silence puts
    the fault in power/TCXO/reset and the SDIO controller stops being a suspect.
(b) A SLOWER IDENTIFICATION CLOCK.  CTO is 64 card clocks, so 100 kHz gives a
    640 us window against 400 kHz's 160 us (f_min is OMAP_MMC_MIN_CLOCK in
    omap_hsmmc.c).  A card that answers at 100 kHz and not at 400 kHz is the bug.
(c) Only then the QNX-IN-LINUX HARNESS (run devnp-ti1283.so's bring-up under a
    shim) — more valuable now, because CON=0x85 showed the reconciliation can be
    subtly wrong.

Do NOT re-run traps 116-137, session 49's two replays (217), the readable-state
diff (218), the boot-path RE (221-226) or the domain test (225).  An overlapping
test must state what differs.

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE 0xBAFD9B3B (0x93A384, kernel
7.3.3 #57 — the session-48 tree plus the GPIO1 pad fix of report 220).
REGION 1 holds the ssh rootfs a4e76b18... (67,108,864 B, 27 modules, dropbear +
host keys + authorized_keys).  The unit is on LINUX (0fca:8010, console `pb> `,
ssh on 169.254.0.1); BOOTCTR_FCTRY read 0x29 at the last boot.  To boot the
ORACLE: flash local/work/ifs-shim-sealed.ifs at 0x825 (the RCFS slot now holds
the FULL 1917 RCFS, trap 227; the pruned oracle pair is local/work/minimal/setB.signed;
at 0x8D5); its hd3 now carries /var/ti1283/{firmware.bin,tiwlan.ini} left by
session 49 — delete for a clean oracle.  /tmp is wiped by a reboot.  To stock
TabletOS: flash ifs-shim-sealed.ifs at 0x825 + rcfs-1917.rcfs at 0x8D5 (NOT
wayback.signed, trap 226).  Do NOT restore
local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

BENCH: `tools/pbssh.sh 'cmd'` is the DEFAULT ACCESS (auto-detects the link;
PB_LINK=usb|door forces it).  The HOST's pb-rndis NetworkManager profile must be
bound to the interface of whichever OS is booted — enp8s0f3u1i2 for our
composite gadget, enp8s0f3u1 for the oracle's plain RNDIS (trap 214).  TWO ssh
servers can answer on 169.254.0.1 depending on the OS (trap 215).  REBOOT: if
`reboot -f` hangs (a wedged MMCHS5 can do it, trap 224) use
`echo b > /proc/sysrq-trigger`; arm the flasher first or use --from-bootrom and
reboot within ~2 s.  NEVER `slay` by name on the oracle — it kills the stack
carrying rndis0; kill by pid (trap 221).  Dropbear is key-only, root, on
169.254.0.1; the key is local/secrets/rsa.  Device password 0000, once.  An
agent NEVER writes RPMB; NVRAM is read-only.  The packet filter is off and the
address is link-local: bench only, never on a network (trap 173).  Read
`sloginfo`/`dmesg` UNFILTERED — a grep hid the reference's connect line in
session 49.  WM8994 work: i2ctransfer arguments are DECIMAL; bus 4, addr 0x1a; a
frozen codec drops raw writes (trap 167); the regmap debugfs is a cache (trap
207).  On the oracle use pbi2c rd16/wr16; push memdump3/memw32/pbi2c to /tmp
after every boot (they are not on the UFS).

LISTENING TESTS ARE QUIET AND SHORT: lower `pbmix set "DAC1 Volume" 40` ONCE, run
every test, restore 96 ONCE at the end; a bare `pbplay` plays a 2 s tone (trap
212); a listening test must state what is playing (trap 191); start the clock
link first (trap 208).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4 (open_binary,
binary_overview, analyze_function, procedure_pseudo_code, search_strings,
xrefs, read_bytes); for a QNX ELF reconcile with ntoarmv7-readelf/objdump
(Ghidra may add +0x10000).  QNX device tools build -static.  USE THE REFERENCE
FIRST, and put static RE in subagents (file-only, trap 109).  The TI TRM is
local (hardware/swpu231ap.pdf.txt); the WM8994 datasheet is
hardware/WM8994.PDF (gitignored).

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed; a
MODULE-only change keeps the CRC and needs a region-1-only flash (trap 190).
The UFS top-up keeps BOOTCTR_FCTRY at 0x32; the hard hazards (0 / >0x32 / failed
write / flat pack) remain.  Operator's call when to stop and to wrap up.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note — and move replaced or
resolved material to docs/history/ or docs/parked/ with a link stub.  Re-export
the kernel patch whenever the DTS or a driver changes (trap 161) and verify it
by apply-and-compare, and run the TREE WALK too (trap 211).  A reverted
experiment still has to be proven reverted: DTS against kernel/source, the
driver against pristine + patch.

WRAP-UP: only the operator decides when the session stops.  Record the bench
state, write the session note, update the canonical docs, verify with
tools/docs_audit.py and tools/tests/test_offline.py, then commit privately and
publish the snapshot (docs/project/REPO-MANAGEMENT.md).  No IFS restore is
required.
```
