# Handoff — session 50 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 51 = Linux-era 45**, written at the
session-50 close.  See `docs/sessions/NUMBERING.md`.

> **Session-50 result, in one line:** the **WL1283 enumerates and `wlcore`
> boots the firmware, but the `wlan0` interface is not stable yet.**  The
> enumeration blocker was the TWL6030 **`CLK32KG`** (the 32.768 kHz SLOW_CLK,
> physical `0xBC`–`0xBE`) being OFF under Linux; enabling it makes the card
> enumerate (`0x0097:0x4076`).  The mainline `wlcore` stack then boots firmware
> (`Rev 7.3.10.0.142`) once three board properties are in place (the
> `ti,wl1283` child, `cap-power-off-card`, and an `mmc-pwrseq-simple` pulsing
> `WL_EN`) — but the IP `WL_IRQ` status read then times out (`-110`) and
> wlcore recovery-loops, so `wlan0` comes up and goes down.  Both of session
> 49's tests were also run and are negative (BT is not a liveness oracle; the
> card is not timing-limited).  Image `0x9712A086` (kernel #60) + wireless
> rootfs `88084b78…`; patch **57 files / 8 658 lines**, sha256 `e7cf9628…`.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a **real userland on the
eMMC** (region 1), and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console **and its RNDIS network link with ssh**, the whole display chain,
the 64 MiB ext4 BusyBox root in region 1, the peripherals (I²C4 motion sensors,
TMP103, BQ27541, FAN5702, CM3217, ISL9519 charger), the **Munin touch**
(calibrated, IRQ wired), the **WM8994 audio** (headphones + both speakers,
48 kHz), and the **WL1283** (enumerates, firmware boots; **the `wlan0`
interface is not stable yet** — see §0b).  **ssh over USB is the bench default**
(`tools/pbssh.sh`, 2.3 MB/s).

**Remaining WL1283 work:** the **`-110` status-read recovery loop** first, then
the per-unit calibration NVS from the `hd5` data (the MAC is already the
assigned one), a userspace supplicant (`iw`/`wpa_supplicant`) + `regulatory.db`,
and a Linux **BT** bring-up (untouched — `HCI_Reset` is silent even with `BT_EN`
driven).

## 0a. The media, and the way back

* **region 0 = the session-50 image** (`boot/chainloader/kernel.img`,
  CRC **`0x9712A086`**, `0x939A74` bytes; kernel `7.3.3` **#60**).  It is the
  reverted session-49 tree plus the `CLK32KG` fix (`clk-twl`), the `ti,wl1283`
  child, `cap-power-off-card` and the WL_EN pwrseq.
* **region 1 = the wireless rootfs** `88084b78…`
  (`local/images/rootfs-region1-wl1283.ext4`, 34 modules incl. `cfg80211`,
  `mac80211`, `wlcore`, `wlcore_sdio`, `wl12xx`, `libarc4`, `crc7`; the
  `wl128x` firmware; dropbear).
* The unit is **on our Linux image** (`0fca:8010`, console `pb> `, **ssh on
  `169.254.0.1`**).
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`, then reboot.  It enumerates `0fca:8012`; qconn
  `169.254.0.1:9090`, root ssh — use **`PB_LINK=door tools/pbssh.sh`**.  The
  region-0 RCFS slot holds the **full 1917 RCFS** (trap 227); for the *minimal*
  oracle flash the pruned pair `local/work/minimal/setB.signed`.  `/tmp` is
  wiped by a reboot.  Its `hd3` carries `/var/ti1283/…` from session 49.
* **To stock TabletOS:** flash **two components** — `local/work/ifs-shim-sealed.ifs`
  at `0x825` **and** `local/work/rcfs-1917.rcfs` at `0x8D5`.
  `wayback.signed` does **not** boot this armed unit (trap 226).  **Do not
  restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).

## 0b. The WL1283 — how it works now, and what is left

### The root cause (report 230)

The WL1283's 32.768 kHz `SLOW_CLK` is the TWL6030 **`CLK32KG`**: group /
transition / state at **physical `0xBC`/`0xBD`/`0xBE`** (module-relative
`0x8C`/`0x8D`/`0x8E`; `twl_i2c` adds the `PM_RECEIVER` base `0x30`).  Linux
booted it `07`/`01`/`00` — **state OFF** — and the reference runs `07`/`05`/`55`
(ON).  On a fresh boot, writing `0xBE = 0xE1` (what `clk-twl`'s prepare computes)
turns `mmc4: Failed to initialize a non-removable card` into `mmc4: new high
speed SDIO card at address 0001`.

The permanent fix: **`CONFIG_CLK_TWL=y`** (it was `=m` and nothing loaded it)
plus `clk_prepare_enable("clk32kg")` in `clk-twl`'s probe.  It runs at ~1.9 s,
before the MMC5 probe at ~3.0 s.

**Do not** use physical `0x8C` (VAUX3) or `0x60` (VCORE3) for this resource —
both were cited in older docs and are wrong (KNOWN-ISSUES 3.31, trap 236).

### The wireless stack (report 231)

`CONFIG_WL12XX=m`, `CONFIG_WLCORE=m`, `CONFIG_WLCORE_SDIO=m`,
`CONFIG_MAC80211=m`, `CONFIG_CFG80211=m` built; the stripped modules (~2.5 MB)
+ `libarc4` + `crc7` are in the region-1 rootfs, and `rcS` loads them in order
(`cfg80211 mac80211 wlcore wl12xx wlcore_sdio`) — the recorded dependency of
`wlcore_sdio` is incomplete.  Firmware: the mainline `wl128x-fw-5-sr.bin` +
`wl128x-nvs.bin` (host linux-firmware, decompressed) under
`local/work/firmware/ti-connectivity/`, installed by
`tools/build_rootfs.py --firmware`.

Three DTS pieces (trap 237):

* **`ti,wl1283` child under `&mmc5`** — `reg = <2>` (SDIO function 2 = WLAN;
  function 1 is BT and the driver rejects it), `interrupt-parent = <&gpio2>`,
  `interrupts = <21 IRQ_TYPE_EDGE_FALLING>` (WL_IRQ), `ref-clock-frequency` and
  `tcxo-clock-frequency` 26 MHz.  **Without the node the probe returns
  `-ENODEV` silently.**
* **`cap-power-off-card;`** on `&mmc5` — without it `mmc_attach_sdio()` does not
  enable card runtime PM and wlcore logs `failed to get_sync(-13)`.
* **`mmc-pwrseq-simple`** owning `WL_EN` (gpio 54 = GPIO2_22):
  `reset-gpios = <&gpio2 22 GPIO_ACTIVE_LOW>`, `reset-delay-us = <50000>`,
  `post-power-on-delay-ms = <120>` — the reference's low → 50 ms → high →
  120 ms on **every** power cycle.  `omap_hsmmc` has no `card_hw_reset`, so
  wlcore's "brute force HW reset" is a no-op; without the pwrseq the first
  firmware boot works and later ones time out (`sdio write failed (-110)`).
  The pwrseq replaced the `wl1283-en-hog`; the two cannot own the line.

Result: `wlcore: firmware booted (Rev 7.3.10.0.142)`, `wlan0` appears with
MAC `14:74:11:8f:9c:c3` (the assigned address, patched into the NVS) — **but
the interface does not stay up**: the WL_IRQ status read times out (`-110`) and
wlcore recovery-loops (report 231 §6).  `NO-CARRIER` is normal before
association; the recovery loop is not.

### Left to do

1. **THE ONE EXPERIMENT: find the `-110` in the WL_IRQ status read.**  After
   `wlcore: firmware booted`, the threaded IRQ's CMD53 status read times out and
   wlcore recovery-loops.
   * **Inputs.** The booted unit (`0x9712A086` + rootfs `88084b78…`), the
     modules loaded in order, `wlcore_sdio dump=1` to print the transfer
     address/length, and the host's `/lib/firmware/ti-connectivity/`
     (`wl128x-fw-4-sr.bin` and `-5-sr.bin`, `wl128x-nvs.bin`).
   * **Run:** `modprobe ... wlcore_sdio dump=1`, `ip link set wlan0 up`, then
     read the **unfiltered** dmesg tail: the last `READ` before
     `sdio read failed (-110)` is the failing address/length.
   * **Results mean:** a read in the FW-status/CPU space with a size that
     matches the module's block size points at the block-size/byte-mode path; a
     read that succeeds with the 4-sr firmware points at the firmware/chip
     revision match; a read that fails only after ELP points at the ELP wake.
   * Then, in order: the per-unit calibration NVS from `hd5` (report 197; the
     MAC is already `14:74:11:8f:9c:c3`), a supplicant + `regulatory.db`, and
     the Linux BT bring-up.
2. **Do not** use `IRQ_TYPE_LEVEL_LOW` for WL_IRQ (trap 238) — the line idles
   low before firmware and the level IRQ wedges the unit.

Do **not** re-run the session-49/50 negatives: the readable-state diff (218),
the two replays (217), the boot-path RE (221–226), the domain test (225), the BT
probe (228) or the clock sweep (229).

## 0c. WM8994 audio — closed

Headphones and both speakers play `pbplay`'s 440 Hz tone at 48 kHz ±1 %
(operator, re-verified session 48 at DAC1 volume 40).  The path is session 46's
(AIF1 on **McBSP3**; `mcbsp2 + wm8994-aif1` clocks, `mcbsp3 + wm8994-aif2` data;
codec AIF1 slave, FLL1 from our BCLK; `XCR2 = 0x8041`; no ABE), and the speaker
route is session 47's (codec **line outputs**, `spk` = GPIO2_12 **active low**,
GPIO1_15 `audio_boost` **high**).  Only the optional MCLK1 probe (trap 165)
remains.

Listening tests: lower `pbmix set "DAC1 Volume" 40` **once**, run every test,
restore `96` **once at the end**; a bare `pbplay` plays a 2 s tone (trap 212);
state what is playing and that both PCMs are `RUNNING` (traps 191/208).

## 0d. Touch — done, survives a warm reboot; cold boots need the pulse

I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` maps the 1917
`scaling.conf` calibration to 1024×600; GPIO2_3 edge IRQ (`irq 110`).  A **cold**
boot leaves the controller down until GPIO1_14/GPIO4_8 are pulsed — the driver
still does not drive them.

## 0e. The minimal 1917 oracle

Debloated 2.1.0.1917 IFS+RCFS (`tools/build_minimal_qnx.py`), qconn on
`169.254.0.1:9090` + root ssh.  Its `/base` is pruned (no firmware dir); for
WLAN work push `firmware.bin` + the ini to `/var/ti1283`.  No wifi service runs,
so `ifconfig ti0 up` is manual.  Tools are not on the UFS — push
`memdump3`/`memw32`/`pbi2c` to `/tmp` after every boot.  **Bench only** (trap
173).

## 1. Useful local extracts

* `local/work/1917/base/`, `ifs/`, `calib/` — the 1917 `/base`, IFS tree and the
  WLAN calibration (`hd5`).
* `local/work/1917/base/scripts/wifi.sh` — the reference's WLAN load path.
* `local/work/firmware/ti-connectivity/` — the mainline `wl128x` firmware.
* `local/work/modules-7.3.3/` — the staged modules (34, incl. the wireless set).
* `local/images/rootfs-region1-wl1283.ext4` — the wireless rootfs.
* `tools/build_minimal_qnx.py`, `local/work/minimal/` — the oracle.
* Captures: `wlan-oracle-up-…`, `wl1283-enumerate-…`, `wl1283-wlan-up-…`,
  `wl1283-{bt-probe,slow-clock}-…`.
* `docs/reference/qnx-hwio/{pad,gpio}-table-winchester.csv`.
* Reports **230/231** (current), **216–229**; `hardware/twl6030.pdf`,
  `hardware/WM8994.PDF` (gitignored), `hardware/swpu231ap.pdf.txt`.

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

**The CRC is per-build** (the version string carries the build time), so it
identifies an image but cannot prove a source revert — compare files against
`local/pristine/linux-7.3.3` + the patch.

**The wireless modules and firmware live in region 1**, not the kernel image.  A
module/firmware-only change keeps the region-0 CRC (trap 190) and needs a
**region-1-only flash** at block `0x2915`:

```bash
python3 tools/build_rootfs.py --size-mb 64 \
  --busybox local/work/src/busybox-1.36.1/busybox \
  --busybox-links local/work/src/busybox-1.36.1/busybox.links \
  --modules local/work/modules-7.3.3 --firmware local/work/firmware \
  --krel 7.3.3 --out local/images/rootfs-region1-wl1283.ext4
```

(`build_rootfs.py --firmware` was silently ignored until session 50; it now
installs the tree and prints `firmware : N file(s)`.)

**Patch export** (traps 161/211): drive the file list from the tree walk, apply
it to a staged pristine copy and compare every file.  Current export: **57 files
/ 8 658 lines**, sha256 `e7cf9628…`, 0 rejects / 57/57.  The walk recipe must
filter `Kconfig.*` (not just `Kconfig`), or it drops `arch/arm/Kconfig.debug`.

## 3. Bench

* **`tools/pbssh.sh 'cmd'`** (root, key-only, `169.254.0.1`, 2.3 MB/s);
  `PB_LINK=usb|door`; the host profile must bind the net interface of whichever
  OS is booted (trap 214).
* **Reboot:** `tools/pbcon.py 'reboot -f'` or `tools/pbssh.sh 'reboot -f'`; if it
  hangs (trap 224) use `echo b > /proc/sysrq-trigger`.  Arm the flasher first.
* **Never `slay` by name on the oracle** (trap 221); kill by pid.
* Device password `0000`, once.  An agent never writes RPMB; NVRAM read-only.
  `/dev/rpmb/` exists only on TabletOS; our `pbrpmb` tops up raw at boot.
* **WL1283 checks:**
  * `i2ctransfer -f -y 0 w1@0x48 0xbc r3` → `07 01 55` (CLK32KG ON; `-f`
    bypasses the twl driver's `EBUSY`).
  * `ls /sys/bus/sdio/devices/` → `mmc4:0001:1` and `:2`.
  * `ip link set wlan0 up` → `wlcore: firmware booted`; `wlan0` UP, no carrier.
  * `wlcore` needs the pwrseq for every firmware re-boot; removing the hog or
    the pwrseq makes the second boot time out.
* WM8994: `i2ctransfer` args are DECIMAL (trap 166); bus 4, addr `0x1a`; the
  regmap debugfs is a cache (trap 207) and debugfs is not mounted by `rcS`.
* QNX device tools build `-static`; `/tmp` is wiped by a reboot.

## 4. Traps added last session (full list 14–237 in `KNOWN-ISSUES.md` §2)

* **234.** `BT_EN` driven is necessary but not sufficient: the BT core answers
  no `HCI_Reset` even with the pad and UART verified.  Session 29's probe is
  superseded; do not use BT silence to exonerate the SDIO controller.
* **235.** Lowering `f_min` does not lower the initial clock; it opens
  `mmc_rescan`'s 400/300/200/100 kHz ladder (which stops at `freqs[i] <= f_min`).
* **236.** The WL1283 `SLOW_CLK` is `CLK32KG` at physical `0xBC`–`0xBE`; Linux
  boots it OFF.  `0xBE = 0xE1` enables it; the permanent fix is `CONFIG_CLK_TWL=y`
  + `clk-twl`'s probe.  Physical `0x8C` is VAUX3; `0x60` is VCORE3.
* **237.** wlcore on `omap_hsmmc` needs the `ti,wl1283` child (`reg = <2>`),
  `cap-power-off-card`, and an `mmc-pwrseq-simple` WL_EN power cycle; load the
  module stack in order.
* Still current: **227** (the measurement flaw / `CON=0x85`), **231**
  (`WL_IRQ` is not liveness), **232** (`CTO` = 64 clocks), **224** (`reboot -f`
  wedge), **221** (`slay` kills `rndis0`), **219** (`BT_EN` = gpio 55), **220**
  (`-o nvram`), **225** (GPIO register blocks; `memw32` pre-masked), **226**
  (two-component way back), **228/229** (pads), **190** (module-only CRC),
  **200** (native doc edits), **207** (regmap cache), **208** (clock link first),
  **211** (tree walk + `Kconfig.*`), **212** (bare `pbplay` plays), **213**
  (`build_rootfs --out` overwrites), **214** (composite net iface), **215** (two
  ssh servers), **216** (no tty), **217** (`$TERM`), **218** (`devpts`).

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| wireless rootfs builder | `tools/build_rootfs.py` (`--modules`, `--firmware`) |
| reference WLAN load path | `local/work/1917/base/scripts/wifi.sh` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| TWL6030 datasheet / TRM | `hardware/twl6030.pdf`, `hardware/swpu231ap.pdf.txt` |
| captures | `docs/captures/wl1283-{enumerate,wlan-up,bt-probe,slow-clock}-2026-11-14.txt` |
| audio machine driver | `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` |
| OSS player / mixer / BT probe | `rootfs/pbplay.c`, `rootfs/pbmix.c`, `rootfs/hci.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| reports | `docs/reports/` (**230** root cause, **231** WLAN, **228/229** negatives) |
| sessions | `docs/sessions/session-50.md` |

## 6. Working rules

* After every console command, dump the log tail and read it.  Read
  `dmesg`/`sloginfo` **unfiltered** after a reboot or structural change.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  **A test boot is not a budget concern.**
* **Never present a guess as a fact.**  Prefer a fresh read.
* **Use the reference before the bench.**  Subagents: static/file-only (trap 109).
* **Revert experiments and prove the revert** (DTS vs `kernel/source`, driver vs
  pristine + patch).
* **Only the operator decides when to stop / wrap up.**

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 51 (absolute; Linux-era 45)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 9, KNOWN-ISSUES.md §2 (traps 14-237, especially 190-237)
and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md (60-67),
docs/reports/INDEX.md, reports 230 and 231, and sessions/session-50.md.

WHERE IT STANDS: audio (headphones + both speakers, 48 kHz), the touch, the
sensors/charger, ssh over USB and the display are all working.  THE WL1283
ENUMERATES (session 50): the card was silent because the TWL6030 CLK32KG (the
32.768 kHz SLOW_CLK, physical 0xBC-0xBE, module-relative 0x8C) was OFF; enabling
it (CONFIG_CLK_TWL=y + clk32kg in clk-twl's probe) makes the card enumerate
(0x0097:0x4076), and the mainline wlcore stack with the ti,wl1283 child
(reg = <2>), cap-power-off-card and an mmc-pwrseq-simple pulsing WL_EN boots
firmware (Rev 7.3.10.0.142) and brings up wlan0.  BUT THE INTERFACE IS NOT
STABLE: the WL_IRQ status read times out (-110) and wlcore recovery-loops
(report 231 section 6).  Both of session-49's tests are run and negative: the BT
core is silent even with BT_EN driven, and the card answers at none of
400/300/200/100 kHz.

WHAT IS LEFT (WL1283, in order):
(a) THE ONE EXPERIMENT: find the -110.  Reload the stack with `wlcore_sdio
    dump=1`, `ip link set wlan0 up`, read the unfiltered dmesg, and identify the
    last READ before `sdio read failed (-110)`.  Try wl128x-fw-4-sr.bin (copy it
    over the 5-sr name in local/work/firmware), and compare the failing
    address/length with the block size.  Do NOT use IRQ_TYPE_LEVEL_LOW for WL_IRQ
    (trap 238 — it storms at boot and wedges the unit).
(b) the per-unit CALIBRATION NVS from the QNX hd5 data (report 197); the MAC is
    already the assigned 14:74:11:8f:9c:c3 (patched into the generic NVS), but
    the radio calibration is still the default.
(c) a userspace supplicant (iw / wpa_supplicant) and regulatory.db, for
    scan/associate; the BusyBox root has neither.
(d) BT: a Linux bring-up (BT_EN = gpio 55 = GPIO2_23, UART2 /dev/ttyS1) — the
    HCI probe is silent (report 228) and the reference's BT half has never been
    shown live.
Do NOT re-run the session-49/50 negatives (readable diff 218, replays 217,
boot-path RE 221-226, domain test 225, BT probe 228, clock sweep 229).

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE 0x9712A086 (0x939A74, kernel
7.3.3 #60 — the CLK32KG fix + the ti,wl1283 child, cap-power-off-card and the
pwrseq).  REGION 1 holds the wireless rootfs 1008d021... (67,108,864 B, 34
modules incl. wlcore/wl12xx, the wl128x firmware, dropbear).  The unit is on
LINUX (0fca:8010, console `pb> `, ssh on 169.254.0.1).  To boot the ORACLE:
flash local/work/ifs-shim-sealed.ifs at 0x825 (the RCFS slot holds the FULL 1917
RCFS, trap 227; the pruned oracle pair is local/work/minimal/setB.signed at
0x8D5).  /tmp is wiped by a reboot.  To stock TabletOS: ifs-shim-sealed.ifs at
0x825 + rcfs-1917.rcfs at 0x8D5 (NOT wayback.signed, trap 226).  Do NOT restore
local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

BENCH: `tools/pbssh.sh 'cmd'` is the DEFAULT ACCESS.  The HOST's pb-rndis
profile must bind the net interface of the booted OS — enp8s0f3u1i2 for our
composite gadget, enp8s0f3u1 for the oracle's plain RNDIS (trap 214).  REBOOT:
if `reboot -f` hangs (trap 224) use `echo b > /proc/sysrq-trigger`.  NEVER
`slay` by name on the oracle (trap 221).  Dropbear is key-only, root, on
169.254.0.1; key local/secrets/rsa.  Device password 0000, once.  An agent
NEVER writes RPMB; NVRAM read-only.  Read dmesg/sloginfo UNFILTERED.
WL1283 checks: `i2ctransfer -f -y 0 w1@0x48 0xbc r3` -> 07 01 55 (CLK32KG ON);
`ls /sys/bus/sdio/devices/` -> mmc4:0001:1/:2; `ip link set wlan0 up` ->
`wlcore: firmware booted`.  WM8994: i2ctransfer args DECIMAL, bus 4 addr 0x1a;
debugfs is not mounted by rcS.  On the oracle use pbi2c rd16/wr16; push
memdump3/memw32/pbi2c to /tmp after every boot.

LISTENING TESTS ARE QUIET AND SHORT: lower `pbmix set "DAC1 Volume" 40` ONCE, run
every test, restore 96 ONCE at the end; a bare `pbplay` plays a 2 s tone (trap
212); state what is playing (trap 191); start the clock link first (trap 208).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4; for a QNX ELF reconcile
with ntoarmv7-readelf/objdump.  QNX device tools build -static.  USE THE
REFERENCE FIRST, and put static RE in subagents (file-only, trap 109).  The TI
TRM is local (hardware/swpu231ap.pdf.txt); the TWL6030 datasheet is
hardware/twl6030.pdf; the WM8994 one hardware/WM8994.PDF (gitignored).

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed; a
module/firmware-only change keeps the CRC and needs a region-1-only flash (trap
190).  The UFS top-up keeps BOOTCTR_FCTRY at 0x32; the hard hazards (0 / >0x32 /
failed write / flat pack) remain.  Operator's call when to stop and to wrap up.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note — and move replaced or
resolved material to docs/history/ or docs/parked/ with a link stub.  Re-export
the kernel patch whenever the DTS or a driver changes (trap 161) and verify it
by apply-and-compare, and run the TREE WALK too (trap 211, filtering Kconfig.*).
A reverted experiment still has to be proven reverted: DTS against
kernel/source, the driver against pristine + patch.

WRAP-UP: only the operator decides when the session stops.  Record the bench
state, write the session note, update the canonical docs, verify with
tools/docs_audit.py and tools/tests/test_offline.py, then commit privately and
publish the snapshot (docs/project/REPO-MANAGEMENT.md).  No IFS restore is
required.
```
