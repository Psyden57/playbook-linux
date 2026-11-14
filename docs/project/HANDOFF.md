# Handoff — session 50 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 51 = Linux-era 45**, written at the
session-50 wrap-up.  See `docs/sessions/NUMBERING.md`.

> **Session-50 result, in one line:** both tests session 49 left were run and
> **both are negative** — the card answers at **none** of 400/300/200/100 kHz
> (`CTO` only; 100 kHz = 640 µs, so "slower than 160 µs" is dead), and the **BT
> core stays silent even with `BT_EN` genuinely driven** and the UART path
> verified (so BT is not a die-liveness oracle, though it does not prove the die
> is off).  The next evidence is a **live capture of the reference's MMCHS5
> register block** at `ti0`-up, or the **QNX-in-Linux harness** — not another
> timing sweep and not the BT half.  Also corrected: `0x8C` is VAUX3 and `0x60`
> is VCORE3's state, so the 32 kHz (`CLK32KG`) has **still never been read**.
> Image `0x17C2E119` (kernel #59, the session-49 tree rebuilt after the
> experiments were reverted) + rootfs `a4e76b18…`; patch unchanged (56 files /
> 8 583 lines, sha256 `455b45d8…`).

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

**The one open item is the WL1283 (ROADMAP item 9).**  Session 49 measured the
host side from both sides and excluded the host program; session 50 killed the
last two host-side hypotheses it left open.  See §0b.

## 0a. The media, and the way back

* **region 0 = the session-50 image** (`boot/chainloader/kernel.img`,
  CRC **`0x17C2E119`**, `0x9398B4` bytes; kernel `7.3.3` **#59**).  It is the
  session-49 tree rebuilt after the two session-50 experiments were reverted —
  functionally the session-49 tree (the previous image was `0xBAFD9B3B`, #57;
  the session-50 experiment was `0x91E22899`, #58).
* **region 1 = the ssh rootfs** `a4e76b18…`
  (`local/images/rootfs-region1-rndis-ssh.ext4`, 27 modules,
  `pbplay`/`pbmix`/**dropbear**).  **`rootfs/hci.c` changed this session**
  (`pnbchi` now drives `BT_EN` and checks the UART); the region-1 image was
  **not** rebuilt, so its `/bin/pbhci` is the session-29 build — push the new
  binary to `/tmp` rather than flashing for a diagnostic.
* The unit is **on our Linux image** (`0fca:8010`, console `pb> `, **ssh on
  `169.254.0.1`**).
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`, then reboot.  It enumerates `0fca:8012`; qconn
  `169.254.0.1:9090`, root ssh — use **`PB_LINK=door tools/pbssh.sh`** (trap
  215).  The region-0 RCFS slot holds the **full 1917 RCFS** (trap 227); for the
  *minimal* oracle flash the pruned pair `local/work/minimal/setB.signed`
  (shim IFS at `0x825` + pruned RCFS at `0x8D5`).  `/tmp` is wiped by a reboot —
  re-push tools.  Its `hd3` carries `/var/ti1283/{firmware.bin,tiwlan.ini}` left
  by session 49 (delete for a clean oracle, or keep it — it makes the reference
  WLAN load directly).
* **To stock TabletOS:** flash **two components** — `local/work/ifs-shim-sealed.ifs`
  at `0x825` **and** `local/work/rcfs-1917.rcfs` at `0x8D5`.
  `local/work/minimal/wayback.signed` does **not** boot this armed unit (vendor
  `os_type` 4 IFS + a vendor RCFS with no root hook; trap 226).  **Do not
  restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).
* **To rebuild the oracle:** `python3 tools/build_minimal_qnx.py`.

## 0b. The WL1283 — what is measured, and the one experiment that is left

### Measured, do not re-derive

* **The reference driver brings the chip up live on this unit** — loaded exactly
  as `/base/scripts/wifi.sh` loads it (`-o nvram`, firmware in `/var/ti1283`,
  then `ifconfig ti0 up`): `sdioAdapt_ConnectBus: Send commands sequence: 0, 5,
  3, 7`, firmware `Rev 7.1.4.10.77`, `ti0` **UP,RUNNING**, MAC
  `14:74:11:8f:9c:c3`, `GPIO2_DATAIN` bit 21 = 1.  Capture
  `docs/captures/wlan-oracle-up-2026-11-14.txt`.
* **Our Linux image is register-identical apart from that one bit** — WL_EN
  high, pads `0x07C = 0x00030003`, `0x148/0x14C/0x150 = 0x01180118`,
  `GPIO2_OE = 0xCE2ECDFF` — and WL_IRQ never goes high.
* **The reference's MMCHS5 program was replayed** (`SYSCONFIG`, `SOFTRESET`,
  `SRA`, `CAPA|VS18`, `SDBP`, `ISE = 0`, `IE = 0x317f0137`, `STAT = ~0`,
  `CON = 0x85`, early `DTW`) — negative.  **`CON = 0x85` must not be copied**;
  its `HR` bit suppresses the command and hangs the rescan (trap 233).
* **The reference's WL_EN power-on ordering was replayed** (driver-owned EN,
  low → 20 ms → high → 120 ms) — negative.
* **The readable board state of stock TabletOS at `ti0`-up was diffed** against
  our boot — 136 of 1 120 words differ and none is WLAN-relevant (report 218).
* **The whole boot path's writes were RE'd** and are negative (reports
  221–226); the one clock-domain difference was tested and negative (225);
  `rm_l4per_mmcsd5_context` is never written at all.
* **The measurement itself was wrong until report 227.**  `WL_IRQ` is not a
  liveness signal (the reference reads 0 too before its driver runs), `STAT` was
  never decoded, and the bus was never read at command time.  Corrected, at
  `CMD52`/`CMD5` the bus is right (`CEN = ICE = 1`, `CLKD = 120` = 400 kHz,
  `SDBP = 1`, `SDVS = 5` = 1.8 V, `CON = 0x600`), `STAT = 0x18000` is **`CTO`
  only**, and `CMD0` succeeds.

### Session 50 closed the two remaining host-side hypotheses

* **The card is not "too slow" (report 229).**  `mmc_rescan`'s ladder was opened
  (`OMAP_MMC_MIN_CLOCK` 400000 → 100000) and tried **400/300/200/100 kHz**;
  every attempt ended `attach_sdio no` with `STAT = 0x00018000` — `CTO` only,
  `RSP0 = 0`.  At 100 kHz the `CTO` window is **640 µs**.  Note `f_min` is not
  the initial clock (trap 235): the ladder is what changes.
* **BT is not a die-liveness oracle (report 228).**  `BT_EN` (gpio 55 =
  `GPIO2_23`, pad `0x4A10007C` bits 31:16) was really driven low → 50 ms → high
  (pad `0x00030003`, `GPIO2_DATAOUT` bit 23 0→1), and the UART2 path was
  verified (`0x4118` input-enabled, `0x0000` TX, `LSR = 0x60` = THRE/TEMT) —
  and `HCI_Reset` still gets no reply.  **This does not prove the die is
  unpowered**: the oracle proves the chip runs, the reference's BT half was never
  shown live, and the 32 kHz has never been read.  It only means BT cannot
  substitute for a card response.

### What is left

1. **A live capture of the reference's MMCHS5 register block at `ti0`-up.**
   Report 218 lists that block (and the chip-side pins) as the two classes it
   never captured.  Do it on stock TabletOS with `ti0` RUNNING (the stock boot
   is the full reference; the minimal oracle needed firmware supplied by hand),
   or on the oracle with the driver loaded.  Compare against our register block
   at the same point (the decoded `PBX` trace is the Linux side).
2. **The QNX-in-Linux harness.**  Run `devnp-ti1283.so`'s bring-up under a shim
   (`mmap_device_io`, `devctl`, `InterruptAttachEvent`, `slogf`, `ThreadCtl`,
   `resmgr_*`/io-net registration stubs) and call `sdioAdapt_ConnectBus`, so its
   exact sequence executes on this silicon.  More valuable now: `CON = 0x85`
   showed the reconciliation can be subtly wrong.
3. **The 32 kHz SLOW_CLK only once its register address is taken from a register
   map.**  `CLK32KG` is neither `0x8C` (VAUX3) nor `0x60` (VCORE3's state); the
   datasheet says it is a software resource that is on from startup, but the
   address table is not in the extractable PDF text (KNOWN-ISSUES 3.31).

Do **not** re-run: traps 116–137, session 49's two replays (217), the
readable-state diff (218), the boot-path RE (221–226), the domain test (225),
the BT probe (228) or the clock sweep (229).  An overlapping test must state
what differs.

## 0c. WM8994 audio — closed

Headphones and both speakers play `pbplay`'s 440 Hz tone at 48 kHz ±1 %
(operator, re-verified session 48 at DAC1 volume 40).  The path is session
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

* **Media:** region 0 = the shim IFS at `0x825` + the pruned RCFS at `0x8D5`
  (`local/work/minimal/setB.signed`); the full 1917 RCFS is currently on media
  (trap 227).
* **Connect:** `python3 tools/QCL.py info` (qconn 9090) or `tools/pbssh.sh`
  (root; `PB_LINK=door`).  The host's `pb-rndis` profile must bind the **plain**
  link name for the oracle (`enp8s0f3u1`) and `enp8s0f3u1i2` for our composite
  gadget (trap 214) — rebind with `nmcli` when you switch OSes.
* **Its `/base` is pruned**: no `/base/lib/firmware/ti1283`.  For WLAN work push
  `firmware.bin` and the ini to `/var/ti1283` (hd3, persistent) and load with
  `-o nvram`.
* **No wifi service runs**, so nothing brings `ti0` up — do it by hand.
* **Tools are not on the UFS**: push `memdump3`/`memw32`/`pbi2c` from
  `local/work/minimal/` to `/tmp` after every reboot.
* `/tmp` is wiped by a reboot; a reboot restores volatile register state.
* **Bench only:** unauthenticated qconn/root ssh — never on a network (trap 173).

## 1. Useful local extracts

* `local/work/1917/base/`, `ifs/`, `calib/`, `parts/` — the 1917 `/base`, IFS
  tree, WLAN calibration (hd5) and pulled partitions.
* `local/work/1917/base/scripts/wifi.sh` — **the reference's WLAN load path**.
* `local/work/1917/base/lib/dll/devnp-ti1283.so`, `devc-seromap-winbt`,
  `btp-btmgr.so`, `lib/firmware/bluetooth/WL1283.bts`.
* `local/vendor/bb10mt/bb10mt` — the QNX6/container tool.
* The minimal oracle — `tools/build_minimal_qnx.py`, `local/work/minimal/`.
* `docs/captures/wlan-oracle-up-2026-11-14.txt` — the reference bring-up.
* `docs/captures/wl1283-bt-probe-2026-11-14.txt`,
  `docs/captures/wl1283-slow-clock-2026-11-14.txt` — session 50.
* `docs/reference/qnx-hwio/{pad,gpio}-table-winchester.csv` + README.
* Reports **228**, **229** (current), **216**–**227**; the WM8994 datasheet
  `hardware/WM8994.PDF` (gitignored), the TI TRM
  `hardware/swpu231ap.pdf.txt`, the TWL6030 datasheet `hardware/twl6030.pdf`,
  the ABE/HAL Addendum `hardware/abe-hal-addendum.txt`.

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
staged copy of pristine and compare every file.  **Unchanged this session**:
**56 files / 8 583 lines**, sha256 `455b45d8…`, 0 rejects / 56/56.  Session 50
changed no kernel file in the final tree (both experiments reverted and proven
by `git apply --include=…` + `cmp`), so there is nothing to re-export.

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
  `0x32`; `/dev/rpmb/` exists only on TabletOS — on our image `pbrpmb` reads and
  tops up the counter raw at boot, so there is nothing to read by hand.
* WM8994 register work: `i2ctransfer` arguments are **DECIMAL** (trap 166);
  bus 4, addr `0x1a`.  A frozen codec drops raw writes (trap 167).  On the
  oracle use `pbi2c rd16/wr16`; on Linux the regmap debugfs is a *cache*
  (trap 207) — and `debugfs` is **not mounted** by `rcS`, so mount it first
  (`mount -t debugfs debugfs /sys/kernel/debug`).
* QNX device tools build **`-static`**; `/tmp` is wiped by a reboot.
* **BT probe:** `BT_EN` = gpio 55 = `GPIO2_23`; the GPIO2 sysfs base is dynamic
  (512 on the session-50 boot) — resolve it from
  `/sys/class/gpio/gpiochip*/device` → `48055000.gpio`.  `UART2` is `/dev/ttyS1`
  (the board DTS already enables it).  See `rootfs/hci.c`.

## 4. Traps added last session (full list 14–235 in `KNOWN-ISSUES.md` §2)

* **234.** `BT_EN` driven is **necessary but not sufficient**: the BT core
  answers no `HCI_Reset` even with the pad verified (`0x00030003`,
  `DATAOUT` bit 23 0→1) and the UART path verified (`LSR = 0x60`).  Session 29's
  probe is superseded; do not use BT silence to exonerate the SDIO controller.
* **235.** Lowering `f_min` does **not** lower the initial clock
  (`mmc_start_host` sets `f_init = max(min(400000, f_max), f_min)`); it opens
  `mmc_rescan`'s `{400k,300k,200k,100k}` ladder, which stops at
  `freqs[i] <= f_min`.  With the default 400000 only 400 kHz is ever tried.
* Still current: **227** (the measurement flaw and `CON=0x85`), **228/229**
  above, **231** (`WL_IRQ` is not a liveness signal), **232** (`STAT` bit 16 is
  `CTO`, a 64-clock window), **233** (`CON=0x85` hangs), **224** (`reboot -f`
  wedge → `sysrq b`), **221** (`slay` kills `rndis0`), **219** (`BT_EN` = gpio
  55), **220** (the loader/FW order; `-o nvram`), **225** (GPIO register blocks;
  `memw32` needs a pre-masked value), **226** (the way back is two components),
  **228/229** (pads).  Also **190** (module-only change keeps the CRC), **200**
  (edit docs with native tools), **207** (regmap debugfs is a cache), **208**
  (clock link first), **211** (tree walk for the patch), **212** (a bare
  `pbplay` plays), **213** (`build_rootfs.py --out` overwrites), **214**
  (composite net interface), **215** (two ssh servers), **216** (an agent shell
  has no tty), **217** (`$TERM` cannot tell console from ssh), **218**
  (`devpts` needs options).
* **Prefer `sloginfo`/`dmesg` unfiltered.**  A filter hid the reference's
  `Send commands sequence` line in session 49.

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **minimal 1917 oracle** | `tools/build_minimal_qnx.py`, `local/work/minimal/` |
| **reference WLAN load path** | `local/work/1917/base/scripts/wifi.sh` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| TI TRM / TWL6030 datasheet | `hardware/swpu231ap.pdf.txt`, `hardware/twl6030.pdf` |
| hwio tables (pads, GPIOs) | `docs/reference/qnx-hwio/` |
| **reference bring-up capture** | `docs/captures/wlan-oracle-up-2026-11-14.txt` |
| **session-50 captures** | `docs/captures/wl1283-{bt-probe,slow-clock}-2026-11-14.txt` |
| audio machine driver | `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` |
| OSS player / mixer / BT probe | `rootfs/pbplay.c`, `rootfs/pbmix.c`, `rootfs/hci.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` (`PB_LINK=usb|door`) |
| **USB network gadget (RNDIS)** | `initramfs/init.c` (`gadget_setup`) |
| ssh bring-up in the root | `rootfs/etc/init.d/rcS` |
| reports | `docs/reports/` (**228** BT probe, **229** slow clock, **227** measurement, **216**–**226** WLAN) |
| sessions | `docs/sessions/session-50.md` (last session) |

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
PlayBook (OMAP4430 "winchester") — session 51 (absolute; Linux-era 45)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 9, KNOWN-ISSUES.md §2 (traps 14-235, especially 190-235)
and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md (60-65),
docs/reports/INDEX.md, reports 228 and 229, and sessions/session-50.md.

WHERE IT STANDS: audio is complete (headphones + both speakers, 48 kHz, session
48) and the bench has ssh over USB (`tools/pbssh.sh`, 2.3 MB/s; the ACM console
is the recovery path).  THE ONE OPEN ITEM IS THE WL1283.  Session 49 settled the
host side: the reference driver, loaded as its own wifi.sh loads it (`-o nvram`,
firmware in /var/ti1283, then `ifconfig ti0 up`), brings the chip up LIVE on the
oracle (ti0 RUNNING, MAC 14:74:11:8f:9c:c3, WL_IRQ high), while the same unit
under Linux is register-identical apart from that one bit; replaying the
reference's ENTIRE MMCHS5 program and its WL_EN power-on ordering changed
nothing (both reverted).  Session 50 then ran the two tests session 49 left and
BOTH ARE NEGATIVE: (a) the identification ladder now tries 400/300/200/100 kHz
and every frequency ends in CTO only (STAT=0x18000, RSP0=0), so the card is NOT
"slower than 160 us" (100 kHz = 640 us); (b) the BT core answers no HCI_Reset
even with BT_EN (gpio 55 = GPIO2_23) genuinely driven low->50ms->high and the
UART2 TX/RX path verified (LSR=0x60), so BT is not a die-liveness oracle --
though that does NOT prove the die is unpowered, because the oracle proves the
chip runs and the reference BT half was never shown live.  Also corrected: 0x8C
is VAUX3 and 0x60 is VCORE3's state, so CLK32KG has still never been read.

THE NEXT EXPERIMENT (do these in order):
(a) A LIVE CAPTURE OF THE REFERENCE'S MMCHS5 REGISTER BLOCK at the moment ti0
    comes up.  Report 218 lists that block (and the chip-side pins) as the two
    classes it never captured; it is the only host-side surface still
    unmeasured.  Do it on stock TabletOS with ti0 RUNNING (full reference), or
    on the oracle with the driver loaded, and diff it against our register block
    at the same point (our side is the decoded PBX trace).
(b) Then the QNX-IN-LINUX HARNESS (run devnp-ti1283.so's bring-up under a shim
    and call sdioAdapt_ConnectBus) -- more valuable now, because CON=0x85 showed
    the reconciliation can be subtly wrong.
(c) The 32 kHz SLOW_CLK ONLY once its register address is taken from a register
    map (KNOWN-ISSUES 3.31); hardware/twl6030.pdf is local but its register
    table is not in the extractable text.

Do NOT re-run traps 116-137, session 49's two replays (217), the readable-state
diff (218), the boot-path RE (221-226), the domain test (225), the BT probe
(228) or the clock sweep (229).  An overlapping test must state what differs.

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE 0x17C2E119 (0x9398B4, kernel
7.3.3 #59 -- the session-49 tree rebuilt after the session-50 experiments were
reverted).  REGION 1 holds the ssh rootfs a4e76b18... (67,108,864 B, 27 modules,
dropbear + host keys + authorized_keys); its /bin/pbhci is the session-29 build
(rootfs/hci.c changed this session -- push the new binary to /tmp).  The unit is
on LINUX (0fca:8010, console `pb> `, ssh on 169.254.0.1).  To boot the ORACLE:
flash local/work/ifs-shim-sealed.ifs at 0x825 (the RCFS slot now holds the FULL
1917 RCFS, trap 227; the pruned oracle pair is local/work/minimal/setB.signed at
0x8D5); its hd3 carries /var/ti1283/{firmware.bin,tiwlan.ini} left by session
49.  /tmp is wiped by a reboot.  To stock TabletOS: flash ifs-shim-sealed.ifs at
0x825 + rcfs-1917.rcfs at 0x8D5 (NOT wayback.signed, trap 226).  Do NOT restore
local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

BENCH: `tools/pbssh.sh 'cmd'` is the DEFAULT ACCESS (auto-detects the link;
PB_LINK=usb|door forces it).  The HOST's pb-rndis NetworkManager profile must be
bound to the interface of whichever OS is booted -- enp8s0f3u1i2 for our
composite gadget, enp8s0f3u1 for the oracle's plain RNDIS (trap 214).  TWO ssh
servers can answer on 169.254.0.1 depending on the OS (trap 215).  REBOOT: if
`reboot -f` hangs (a wedged MMCHS5 can do it, trap 224) use
`echo b > /proc/sysrq-trigger`; arm the flasher first or use --from-bootrom and
reboot within ~2 s.  NEVER `slay` by name on the oracle -- it kills the stack
carrying rndis0; kill by pid (trap 221).  Dropbear is key-only, root, on
169.254.0.1; the key is local/secrets/rsa.  Device password 0000, once.  An
agent NEVER writes RPMB; NVRAM is read-only.  The packet filter is off and the
address is link-local: bench only, never on a network (trap 173).  Read
`sloginfo`/`dmesg` UNFILTERED.  WM8994 work: i2ctransfer arguments are DECIMAL;
bus 4, addr 0x1a; a frozen codec drops raw writes (trap 167); the regmap debugfs
is a cache (trap 207) and debugfs is not mounted by rcS -- mount it first.  On
the oracle use pbi2c rd16/wr16; push memdump3/memw32/pbi2c to /tmp after every
boot.  BT probe: BT_EN = gpio 55 = GPIO2_23; the GPIO2 sysfs base is dynamic
(512 last boot).

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
hardware/WM8994.PDF (gitignored); the TWL6030 datasheet is
hardware/twl6030.pdf.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed; a
MODULE-only change keeps the CRC and needs a region-1-only flash (trap 190).
The UFS top-up keeps BOOTCTR_FCTRY at 0x32; the hard hazards (0 / >0x32 / failed
write / flat pack) remain.  Operator's call when to stop and to wrap up.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note -- and move replaced or
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
