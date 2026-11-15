# Handoff — session 52 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 53 = Linux-era 47**, written at the
session-52 close.  See `docs/sessions/NUMBERING.md`.

> **Session-52 result, in one line:** **the WL1283 WLAN associates** — the
> blocker was the **missing `ccm`/`ctr` crypto modules** (the PTK install failed
> with `-ENOENT`, which wpa_supplicant reports as `WRONG_KEY`), the **USB link
> got its DMA back** (RX 3.3 → 6.3 MB/s, TX 3.3 → 4.1), and the **BT core now
> answers HCI** (the UART2 CTS/RTS pads).  Region 0 is now **`0xDD66D8C3`**;
> region 1 **`49e318bf…`**; patch **61 files / 8 762 lines**, sha256
> `20556c5f…`.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a **real userland on the
eMMC** (region 1), and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console **and its RNDIS network link with ssh**, the whole display chain,
the 64 MiB ext4 BusyBox root in region 1, the peripherals (I²C4 motion sensors,
TMP103, BQ27541, FAN5702, CM3217, ISL9519 charger), the **Munin touch**, the
**WM8994 audio** (headphones + both speakers, 48 kHz), and the **WL1283 WLAN**:
it scans, **associates**, takes a DHCP lease and passes traffic.

**What is left:** the WL1283's **other cores** — **BT**, then **FM** and
**GPS** (operator's order; §0c) — plus two open WLAN questions that block
nothing.

## 0a. The media, and the way back

* **region 0 = `0xDD66D8C3`** (`0x939E4C` B, kernel `7.3.3`;
  `boot/chainloader/kernel.img`).  Session 52's kernel changes are the MUSB DMA
  fix and the **UART2 CTS/RTS pads** (BT).
* **region 1 = `49e318bf…`** (`local/images/rootfs-region1-wl1283-bt.ext4`,
  67 108 864 B, **43 modules** — the 36 plus the seven Bluetooth ones
  (`bluetooth`, `hci_uart`, `btbcm`, `crc-ccitt`, `ecdh_generic`, `ecc`,
  `kpp`), both wl128x SR firmware files, the **per-unit NVS**, the CAM+4-sr
  `wl12xx.ko` `2fb86c4d…`, `iw` 6.17, `wpa_supplicant`/`wpa_cli`/`wpa_passphrase`
  2.12 and the signed `regulatory.db`).
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

## 0b. The WLAN — it associates, and how to get there

### The blocker was crypto, not the key (report 236)

The 4-way handshake completed all along: **message 3 decrypts with
AES-UNWRAP**, so the passphrase was never wrong.  The failure was the
pairwise-key install:

```
nl80211: kernel reports: key addition failed
nl80211: set_key failed; err=-2 No such file or directory
```

`-ENOENT` comes from `ieee80211_key_alloc()` — for CCMP it allocates a
`ccm(aes)` AEAD, and **`ccm.ko`/`ctr.ko` were not in the rootfs** (and are not a
link dependency of `mac80211.ko`, so `depmod` never supplied them).  They now
ship and `rcS` loads them before the wireless stack.

### The working sequence (after a boot; `/tmp` is wiped)

```sh
ip link set wlan0 up                 # rcS loads the stack, it does not raise it
iw reg set <CC>                      # e.g. AR — required for a 5 GHz AP
mkdir -p /var/run/wpa_supplicant
{ echo "disable_scan_offload=1"; wpa_passphrase '<SSID>' '<PSK>'; } > /tmp/wpa.conf
iw dev wlan0 scan >/dev/null 2>&1
wpa_supplicant -B -i wlan0 -D nl80211 -c /tmp/wpa.conf -C /var/run/wpa_supplicant
wpa_cli -p /var/run/wpa_supplicant -i wlan0 status      # want wpa_state=COMPLETED
```

* **`iw reg set <CC>` is not optional on 5 GHz.**  The world domain marks
  5735–5835 MHz `PASSIVE-SCAN`; without the country the log shows
  `regulatory prevented using AP config, downgraded` and the auth times out
  (trap 245).
* **The first auth attempt sometimes times out** and the supplicant then sits
  in `SCANNING` without retrying.  Nudge it (`wpa_cli … scan`) or kill it,
  run `iw dev wlan0 scan`, and restart.
* `disable_scan_offload=1` matters: the scheduled scan often did not return the
  target AP and the supplicant stayed in "No suitable network".
* **DHCP:** `udhcpc -i wlan0` gets a lease (192.168.0.212 here) but BusyBox has
  no `default.script`, so apply it by hand (`ip addr add`, `ip route add`).
* The AP used for the bench is **TeleCentro-AW-5G** (5785 MHz, WPA2-PSK) at
  −41 dBm.

### Measured throughput (raw `nc`/`socat`, no ssh)

| path | device → host | host → device |
|---|---|---|
| Wi-Fi (1×1 HT20, MCS7/65) | 2.9 MB/s | 4.4 MB/s |
| USB RNDIS | 4.1 MB/s | 6.3 MB/s |
| USB over ssh (dropbear) | 2.3 MB/s | 2.3 MB/s |

**Use raw `nc`/`socat` for bulk copies**; ssh is for command access.
**Do not advertise HT40** — it associates and then collapses to MCS0
(trap 247).  Wi-Fi is at its hardware ceiling.

### Left to do on the WLAN (none blocking)

* *Why* the 5-sr firmware panics on scan (report 235 §3) and *why* the ELP wake
  is not honoured (report 232 §6).
* The **per-unit NVS** is shipped; measuring the TX-power difference with and
  without it is optional.

Do **not** re-run the session-49/50 negatives (readable diff 218, replays 217,
boot-path RE 221–226, domain test 225, BT probe 228, clock sweep 229).  Do
**not** use `IRQ_TYPE_LEVEL_LOW` for WL_IRQ (trap 238), **not**
`wlcore_sdio dump=1` for a timing fault (trap 239), **not** 5-sr (trap 243),
**not** HT40 (trap 247).

## 0c. The WL1283's other cores — BT, then FM, then GPS

**Order (operator, session 52 end): BT first, then FM, then GPS.**  They share
the chip, and BT is the transport the other two have historically used.

### BT — the transport works; powering it is the next step

**The core answers HCI (report 237).**  The three-session silence was the
**UART2 CTS/RTS pads**: `0x4A100118` sat in the bootloader's safe mode
(`0x011F011F`); the reference's idle value is **`0x00000118`**.  The DTS
(`uart2_pins`, two halfword entries `0x118`/`0x11A`) now sets it, so the chip
answers `HCI_Reset` (`04 0e 04 01 03 0c 00`) at boot.

* **Wiring:** `BT_EN` = **gpio 55 = `GPIO2_23`** (pad `0x4A10007C`, mux 3),
  pulsed low → 50 ms → high (`pbhci` does it); UART2 = **`/dev/ttyS1`**,
  115200 8N1.
* **`hci0` exists:** `pbhci --attach --hold N` sets `N_HCI` + `HCI_UART_H4`
  (the two ioctls `hciattach` uses) and the kernel creates
  `/sys/class/bluetooth/hci0`, then exchanges commands with the chip.  The
  `Bluetooth: hci0: Opcode 0x201c failed: -38` line is benign.
* **Powering is the blocker now:** `HCIDEVUP` (`_IOW('H', 201, int)`) returns
  **`ENOSYS`** on 7.3.3, so the controller stays down and a raw-socket inquiry
  says `Network is down`.  Use the **mgmt** channel (`HCI_CHANNEL_CONTROL` +
  `MGMT_OP_SET_POWERED` 0x0005) — `pbhci --inquiry` has a first attempt at it
  that did not report a status, so finish/debug it, or bring in BlueZ.
* **The service pack** is `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts`
  (315 HCI commands: 227 × `0xFF05` write-memory-block = 56 771 bytes, then
  `0xFD09` release Cortex Reset and `0xFDA8` enable Cortex Sleep, report 216
  §0).  A userspace H4 loader is the next tool.  **The handoff's earlier advice
  to prefer linux-firmware's `TIInit_7.2.31.bts` is unverified** — the
  reference's file is the one that matches this chip.
* **Do not** re-run the "BT is silent" probes: that question is answered, and
  `BT_EN` driven alone is not evidence either way (trap 234 → now read together
  with trap 248).

#### THE ONE EXPERIMENT: power `hci0` and run one inquiry

**Inputs.**  The unit on `0xDD66D8C3` + `49e318bf…`; `pbhci` built from
`rootfs/hci.c` and pushed to `/tmp` (dropbear has no `scp`:
`ssh … 'cat > /tmp/pbhci' < /tmp/pbhci`); nothing else.  Run as root.

1. `/tmp/pbhci /dev/ttyS1` — sanity: expect **`04 0e 04 01 03 0c 00`**.
2. `nohup /tmp/pbhci --attach --hold 60 >/tmp/att.log 2>&1 &` — expect
   `/sys/class/bluetooth/hci0` to appear.  The line discipline dies when the
   process exits, so keep it alive for the next step.
3. `/tmp/pbhci --inquiry 10` — mgmt `SET_POWERED` then one inquiry.

**What each result means.**

* **A `mgmt set_powered(1) -> status 0x00` line and `found xx:xx:…` lines** →
  BT works.  Record the addresses; then choose the stack (BlueZ over mgmt, or
  extend the probe's HCI socket path) and only afterwards tackle the service
  pack (contradiction `bt-firmware-tiinit-vs-wl1283.md`).
* **A non-zero mgmt status** → the request reached the kernel and was refused;
  the status byte is the answer (the probe currently assumes it is `buf[8]`,
  which is **unverified** — print the whole reply and read it against
  `include/net/bluetooth/mgmt.h` before trusting it).
* **No `mgmt` line at all** (the probe returns before printing) → the socket,
  bind or write failed; add `perror` to `mgmt_power()` and re-test.  Do **not**
  fall back to `HCIDEVUP` — it is `ENOSYS` on 7.3.3.
* **Powered, but no devices found in 10 s** → not a failure: repeat next to a
  device in discoverable mode before concluding anything.
* **Any `sdio read failed` / `wlcore: down` at the same time** → the WLAN core
  is being disturbed; stop, and settle whether `BT_EN` high conflicts with the
  WLAN (report 216 says the reference holds it low while `ti0` is up).

### FM

Mainline carried `drivers/media/radio/wl128x` from 2.6.39 to 6.13 and **removed
it**.  Bringing FM back means porting that driver forward (7.3.3 has no
replacement) and settling its transport.  Start from the last tree that had it
(v6.12) and diff the V4L2/radio APIs it uses against 7.3.3.

### GPS

There is **no mainline driver**.  The operator has a **2024 LKML patch** to
evaluate (`lists.openwall.net/linux-kernel/2024/06/06/1445`); if it does not
fit, the fallback is to RE how the reference OS drives the GPS core.

## 0d. WM8994 audio — closed

Headphones and both speakers play `pbplay`'s 440 Hz tone at 48 kHz ±1 %; the
path is session 46's and the speaker route session 47's.  Only the optional
MCLK1 probe (trap 165) remains.

Listening tests: lower `pbmix set "DAC1 Volume" 40` **once**, restore `96` at
the end; a bare `pbplay` plays a 2 s tone (trap 212); state what is playing
(trap 191).

## 0e. Touch — done; cold boots need the pulse

I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c`; GPIO2_3 edge IRQ
(`irq 110`).  A **cold** boot leaves the controller down until GPIO1_14/GPIO4_8
are pulsed — the driver still does not drive them.

## 0f. The minimal 1917 oracle

Debloated 2.1.0.1917 IFS+RCFS (`tools/build_minimal_qnx.py`), qconn on
`169.254.0.1:9090` + root ssh.  Its `/base` is pruned (no firmware dir); for
WLAN/BT work push the firmware to the device.  No wifi/BT service runs, so
`ifconfig ti0 up` and the BT start are manual.  Tools are not on the UFS —
push `memdump3`/`memw32`/`pbi2c` to `/tmp` after every boot.  **Bench only**
(trap 173).

## 1. Useful local extracts

* `local/work/1917/` — the 1917 `/base`, IFS tree and the WLAN calibration.
* `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` — the reference's BT
  firmware (for comparison only); linux-firmware's `TIInit_7.2.31.bts` is the
  one to use.
* `local/work/firmware/` — the firmware tree; `local/work/firmware-nvs/` is the
  same tree with the **per-unit NVS** (what the shipped image is built from).
* `local/work/modules-7.3.3/` — the staged modules (**36**); source of truth for
  region 1.
* `local/images/rootfs-region1-wl1283-final.ext4` — the current region-1 image.
* `tools/build_minimal_qnx.py`, `local/work/minimal/` — the oracle.
* Reports **236** (current), **232–235**, **216–229**; `hardware/twl6030.pdf`,
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

**Build modules IN-TREE, not with `M=`.**  `M=` marks them `(O)`/out-of-tree,
taints the kernel and breaks the next in-tree build.  Use `make … modules` and
strip with **`--strip-debug`**, never `strip -s`.

**The wireless modules and firmware live in region 1**, not the kernel image.  A
module/firmware-only change keeps the region-0 CRC (trap 190) and needs a
**region-1-only flash** at block `0x2915`:

```bash
python3 tools/build_rootfs.py --size-mb 64 \
  --busybox local/work/src/busybox-1.36.1/busybox \
  --busybox-links local/work/src/busybox-1.36.1/busybox.links \
  --modules local/work/modules-7.3.3 --firmware local/work/firmware-nvs \
  --extras local/work/rootfs-extras --krel 7.3.3 --out local/images/<name>.ext4
```

`build_rootfs.py --out` **overwrites** (trap 213) — build to a new purpose-named
path.  Check what landed with `debugfs -R "dump /lib/modules/7.3.3/kernel/X.ko …"`.

**Patch export** (traps 161/211): walk the tree against pristine, filter build
artifacts (`*.o`, `*.ko`, `*.orig`, `.tmp*`, generated `.c/.h`, the `scripts/`
binaries), apply the result to a staged pristine copy and compare every file.
Current: **61 files / 8 755 lines**, sha256 `741115a4…`, 0 rejects / 61/61.

## 3. Bench

* **`tools/pbssh.sh 'cmd'`** (root, key-only, `169.254.0.1`); `PB_LINK=usb|door`.
  Dropbear has **no `scp`** — move files with `ssh … 'cat > /path' < file`, or
  better use raw `nc`/`socat` for anything large (6.3 MB/s vs ssh's 2.3).
* **Reboot:** `tools/pbssh.sh 'reboot -f'`; if it hangs (trap 224) use
  `echo b > /proc/sysrq-trigger`.  Arm the flasher first, and start the flasher
  as a **background task** — a `nohup … &` inside a foreground shell call keeps
  the tool's pipe open and looks like a hang.
* **Never `slay` by name on the oracle** (trap 221); kill by pid.
* Device password `0000`, once.  An agent never writes RPMB; NVRAM read-only.
  `/dev/rpmb/` exists only on TabletOS; our `pbrpmb` tops up raw at boot.
* **`BOOTCTR_FCTRY` is readable on our image** without `/dev/rpmb`:
  `tools/pbssh.sh 'dmesg | grep pbrpmb'` →
  `pbrpmb: topup: BOOTCTR_FCTRY at block 0x3a record@196 = <4 bytes LE>` then
  `pbrpmb: topup: 0x<v> >= threshold 0x1e, no write`.  It read **`0x23`** at the
  session-52 close (`23000000`).  This is a logged read, not an RPMB access
  (`SAFETY.md` §2, `DEVELOPMENT.md` §5).
* **WLAN checks:** `ls /sys/bus/sdio/devices/` → `mmc4:0001:1/:2`;
  `ip link set wlan0 up` → `wlcore: firmware booted (Rev 7.3.5.0.98)`;
  stability = zero `sdio read failed`/`wlcore: down` lines; `lsmod` must show
  **`ccm` and `ctr`** (if not, association will fail with `-ENOENT`).
* **BT checks (new):** `BT_EN` at pad `0x4A10007C` mux 3 and `GPIO2_DATAOUT`
  bit 23; UART2 pads `0x4118`/`0x411A`; then a `pbhci` probe over `/dev/ttyS1`.
* WM8994: `i2ctransfer` args are DECIMAL (trap 166); bus 4, addr `0x1a`; the
  regmap debugfs is a cache (trap 207) and debugfs is not mounted by `rcS`.
* QNX device tools build `-static`; `/tmp` is wiped by a reboot.

## 4. Traps added last session (full list 14–247 in `KNOWN-ISSUES.md` §2)

* **244.** A WPA2 `reason=WRONG_KEY` is usually **not** the key: the `ccm`/`ctr`
  modules were missing, so `ieee80211_key_alloc()`'s `ccm(aes)` AEAD failed and
  the PTK install returned `-ENOENT`.  The handshake itself was fine (message 3
  decrypts).  Ship and load the modules.
* **245.** A 5 GHz AP outside the world regdomain gives an **auth timeout**
  (`regulatory prevented using AP config, downgraded`); `iw reg set <CC>`.
* **246.** MUSB HSDMA silently falls back to PIO for non-4-byte-aligned buffers;
  the tell is the `dma` IRQ never firing.  Fixed for RX
  (`quirk_avoids_skb_reserve`) and TX (realign copy).
* **247.** Do **not** advertise HT40 on wl12xx — it associates and collapses to
  MCS0 (RX 622 KB/s).  `wpa_cli status`'s `channel_width` is the AP's, not ours.
* **248.** The BT core's silence was the **UART2 CTS/RTS pads** (`0x4A100118`,
  safe mode `0x011F011F`); the reference's `0x00000118` makes it answer.  Also,
  `HCIDEVUP` is **`ENOSYS`** on 7.3.3 — power the controller through **mgmt**.
* Still current: **247** (no HT40), **243** (4-sr, not 5-sr), **241** (do not enable wlcore fwlog),
  **239** (the `-110` was ELP), **238** (`LEVEL_LOW` WL_IRQ storms), **237**
  (the three wlcore board properties), **236** (`CLK32KG` at `0xBC`), **234**
  (BT silence is not a liveness oracle), **227** (the old measurement flaw),
  **224** (`reboot -f` wedge), **221** (`slay` kills `rndis0`), **214**
  (composite net iface), **212** (bare `pbplay` plays), **211** (tree walk +
  `Kconfig.*`), **190** (module-only CRC), **165** (chip-side pins).

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| wireless rootfs builder | `tools/build_rootfs.py` (`--modules`, `--firmware`, `--extras`) |
| reference WLAN/BT load path | `local/work/1917/base/scripts/wifi.sh`, `…/startup.sh` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| TWL6030 datasheet / TRM | `hardware/twl6030.pdf`, `hardware/swpu231ap.pdf.txt` |
| BT firmware | linux-firmware `TIInit_7.2.31.bts`; reference `WL1283.bts` |
| OSS player / mixer / BT probe | `rootfs/pbplay.c`, `rootfs/pbmix.c`, `rootfs/hci.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| reports | `docs/reports/` (**236** current) |
| sessions | `docs/sessions/session-52.md` |

## 6. Working rules

* After every console command, dump the log tail and read it.  Read
  `dmesg`/`sloginfo` **unfiltered** after a reboot or structural change — a
  `grep` has already hidden the line that mattered.
* **Don't clear `dmesg` under the operator** without saying so.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  **A test boot is not a budget concern.**
* **Never present a guess as a fact.**  Prefer a fresh read.
* **Use the reference before the bench** (DECISIONS §61).  Subagents:
  static/file-only (trap 109).
* **Revert experiments and prove the revert** (DTS vs `kernel/source`, driver vs
  pristine + patch, module hash).
* **Only the operator decides when to stop / wrap up.**

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 53 (absolute; Linux-era 47)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 9, KNOWN-ISSUES.md §2 (traps 14-247, especially 236-247)
and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md (61-71),
docs/reports/INDEX.md, reports 232-236, and sessions/session-52.md.

WHERE IT STANDS: audio (headphones + both speakers, 48 kHz), the touch, the
sensors/charger, ssh over USB, the display and the WL1283 WLAN all work.  THE
WLAN ASSOCIATES (session 52): the blocker was the missing ccm/ctr crypto
modules — ieee80211_key_alloc() needs a ccm(aes) AEAD for every CCMP key, so
without them the PTK install returned -ENOENT (which wpa_supplicant reports as
WRONG_KEY) — now shipped and loaded by rcS.  wpa_state=COMPLETED, WPA2-CCMP on
5785 MHz, DHCP 192.168.0.212, traffic.  A 5 GHz AP needs `iw reg set <CC>`
(world regdomain marks 5735-5835 no-IR).  Wi-Fi is 1x1 HT20 (72 Mbit/s) —
2.9/4.4 MB/s; do NOT advertise HT40 (trap 247).  The USB RNDIS link had been
running MUSB PIO because buffers were not 4-byte aligned; fixed (quirk +
realign): RX 6.3 / TX 4.1 MB/s raw, ssh 2.3.

WHAT IS LEFT (operator's order): (a) BT — THE TRANSPORT WORKS (report 237):
the UART2 CTS/RTS pads (0x4A100118) were the three-session blocker — safe mode
0x011F011F vs the reference's 0x00000118 — and with them in the DTS the core
answers HCI_Reset (04 0e 04 01 03 0c 00) at boot and `pbhci --attach` (N_HCI +
HCI_UART_H4) creates /sys/class/bluetooth/hci0.  Next: power it (HCIDEVUP is
ENOSYS; use mgmt SET_POWERED), load the service pack
(local/work/1917/base/lib/firmware/bluetooth/WL1283.bts), and a stack.
(b) FM — mainline had drivers/media/radio/wl128x from 2.6.39 to 6.13 and removed
it; a forward port is the route.  (c) GPS — no mainline driver; evaluate the
2024 LKML patch (lists.openwall.net/linux-kernel/2024/06/06/1445), else RE the
reference.  (d) open, neither blocking: why 5-sr panics on scan (235 §3) and why
the ELP wake is not honoured (232 §6).
Do NOT re-run the session-49/50 negatives (readable diff 218, replays 217,
boot-path RE 221-226, domain test 225, BT probe 228, clock sweep 229).  Do NOT
use IRQ_TYPE_LEVEL_LOW for WL_IRQ (trap 238), NOT wlcore_sdio dump=1 for a
timing fault (trap 239), NOT 5-sr (trap 243), NOT HT40 (trap 247).

PHYSICAL STATE: region 0 = our Linux image 0xDD66D8C3 (0x939E4C, kernel 7.3.3,
the musb DMA fix + the UART2 CTS/RTS pads).  Region 1 = 49e318bf... (43 modules:
the WLAN stack incl. ccm/ctr, the seven Bluetooth ones, BOTH wl128x SR firmware
files, the per-unit NVS, dropbear, the CAM+4-sr wl12xx.ko, iw 6.17,
wpa_supplicant/wpa_cli/wpa_passphrase 2.12 and the signed regulatory.db+.p7s) —
local/images/rootfs-region1-wl1283-bt.ext4.  Kernel patch 61 files / 8 762
lines, sha256 20556c5f...  The unit is on LINUX
(0fca:8010, console `pb> `, ssh on 169.254.0.1); rcS loads the wireless stack
but does not raise the link — see HANDOFF §0b for the exact association
sequence (reg set, disable_scan_offload, and a wpa_cli scan nudge when the first
auth times out).  To boot the ORACLE: flash local/work/ifs-shim-sealed.ifs at
0x825 (the RCFS slot holds the FULL 1917 RCFS, trap 227; the pruned oracle pair
is local/work/minimal/setB.signed at 0x8D5).  /tmp is wiped by a reboot.  To
stock TabletOS: ifs-shim-sealed.ifs at 0x825 + rcfs-1917.rcfs at 0x8D5 (NOT
wayback.signed, trap 226).  Do NOT restore local/images/ifs0.sealed.bin
(2.0.0.4869, trap 84).

BENCH: `tools/pbssh.sh 'cmd'` is the DEFAULT ACCESS for commands; for bulk file
moves use raw nc/socat (6.3 MB/s vs ssh 2.3).  The HOST's pb-rndis profile must
bind the net interface of the booted OS — enp8s0f3u1i2 for our composite
gadget, enp8s0f3u1 for the oracle's plain RNDIS (trap 214).  Dropbear has no
scp.  REBOOT: if `reboot -f` hangs (trap 224) use `echo b >
/proc/sysrq-trigger`.  NEVER `slay` by name on the oracle (trap 221).  Dropbear
is key-only, root, on 169.254.0.1.  Device password 0000, once.  An agent NEVER
writes RPMB; NVRAM read-only.  Read dmesg/sloginfo UNFILTERED.  Do not clear
dmesg under the operator without saying so.

BT: BT_EN pad 0x4A10007C mux 3 + GPIO2_DATAOUT bit 23; UART2 pads
0x4118/0x411A; /dev/ttyS1.  A silent BT core is NOT a liveness oracle (trap
234) — prove BT_EN at the pad first, and use the reference (devc-seromap-winbt
+ btp-btmgr) before writing anything.  WL1283 checks: `i2ctransfer -f -y 0
w1@0x48 0xbc r3` -> 07 01 55; `lsmod` must show ccm and ctr; `ip link set
wlan0 up` -> firmware Rev 7.3.5.0.98.  WM8994: i2ctransfer args DECIMAL, bus 4
addr 0x1a; debugfs is not mounted by rcS.  On the oracle use pbi2c rd16/wr16;
push memdump3/memw32/pbi2c to /tmp after every boot.

LISTENING TESTS ARE QUIET AND SHORT: lower `pbmix set "DAC1 Volume" 40` ONCE,
run every test, restore 96 ONCE at the end; a bare `pbplay` plays a 2 s tone
(trap 212); state what is playing (trap 191).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4; for a QNX ELF reconcile
with ntoarmv7-readelf/objdump.  QNX device tools build -static.  USE THE
REFERENCE FIRST, and put static RE in subagents (file-only, trap 109).  The TI
TRM is local (hardware/swpu231ap.pdf.txt); the TWL6030 datasheet is
hardware/twl6030.pdf; the WM8994 one hardware/WM8994.PDF (gitignored).

BUILD: modules IN-TREE (`make ... modules`), never `M=`; strip with
`--strip-debug`, never `strip -s`.

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
A reverted experiment still has to be proven reverted.

WRAP-UP: only the operator decides when the session stops.  Record the bench
state, write the session note, update the canonical docs, verify with
tools/docs_audit.py and tools/tests/test_offline.py, then commit privately and
publish the snapshot (docs/project/REPO-MANAGEMENT.md).  No IFS restore is
required.
```
