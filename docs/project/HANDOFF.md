# Handoff — session 53 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 54 = Linux-era 48**, written at the
session-53 close.  See `docs/sessions/NUMBERING.md`.

> **Session-53 result, in one line:** **the WL1283's BT core works** — it answers
> `HCI_Reset`, reports itself (`hci ver 6` / `lmp ver 6` / `mfr 0x002b` = TI,
> BT 4.0) and **runs a full inquiry that finds a peer** — and **loading the
> reference's `WL1283.bts` (315/315) makes `hci0` register with mgmt for the
> first time** (0 → 1 controller).  What is left is **opening** the controller: a
> **stray `0x30` byte** arrives after init and the H4 receiver (correctly)
> rejects it, so `SET_POWERED` is `0x05`.  Region 0 `0xDD66D8C3`; region 1
> `49e318bf…`; patch **61 files / 8 762 lines**, sha256 `20556c5f…` (unchanged —
> no flash and no DTS edit this session).

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a **real userland on the
eMMC** (region 1), and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console **and its RNDIS network link with ssh**, the whole display chain,
the 64 MiB ext4 BusyBox root in region 1, the peripherals (I²C4 motion sensors,
TMP103, BQ27541, FAN5702, CM3217, ISL9519 charger), the **Munin touch**, the
**WM8994 audio** (headphones + both speakers, 48 kHz), the **WL1283 WLAN**
(associates, DHCP, traffic), and now **the WL1283's BT core** (real inquiry).

**What is left:** open the **BT controller** so the kernel's stack can use it
(§0c); then FM and GPS on the same chip; plus two open WLAN questions that block
nothing.

## 0a. The media, and the way back

* **region 0 = `0xDD66D8C3`** (`0x939E4C` B, kernel `7.3.3`;
  `boot/chainloader/kernel.img`).  Unchanged in session 53.
* **region 1 = `49e318bf…`** (`local/images/rootfs-region1-wl1283-bt.ext4`,
  67 108 864 B, **43 modules**).
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

## 0b. The WLAN — it associates (session 52, unchanged)

The blocker was the missing `ccm`/`ctr` crypto modules
(`ieee80211_key_alloc()` needs a `ccm(aes)` AEAD; without it the PTK install is
`-ENOENT`, which wpa_supplicant reports as `WRONG_KEY`).  Working sequence after
a boot (`/tmp` is wiped):

```sh
ip link set wlan0 up                 # rcS loads the stack, it does not raise it
iw reg set <CC>                      # e.g. AR — required for a 5 GHz AP
mkdir -p /var/run/wpa_supplicant
{ echo "disable_scan_offload=1"; wpa_passphrase '<SSID>' '<PSK>'; } > /tmp/wpa.conf
iw dev wlan0 scan >/dev/null 2>&1
wpa_supplicant -B -i wlan0 -D nl80211 -c /tmp/wpa.conf -C /var/run/wpa_supplicant
wpa_cli -p /var/run/wpa_supplicant -i wlan0 status      # want wpa_state=COMPLETED
```

Wi-Fi is 1×1 HT20 (72 Mbit/s) — **do not advertise HT40** (trap 247).  Do **not**
use `IRQ_TYPE_LEVEL_LOW` for WL_IRQ (trap 238), **not** `wlcore_sdio dump=1`
(trap 239), **not** 5-sr (trap 243).

## 0c. BT — the core works; opening the kernel's controller is the one task

### What is proven (report 238)

* **Real Bluetooth.**  Over a plain H4 tty (`/dev/ttyS1`, 115200 8N1) the core
  answers `HCI_Reset`, reports `hci ver 6` / `lmp ver 6` / `subver 0x0f00` /
  `mfr 0x002b` (TI) and BD_ADDR `8e:e4:3c:22:61:00`, then **runs a full BR/EDR
  inquiry that finds a peer** (`f0:a3:b2:bb:d1:00`, class `0x011c00`),
  reproducibly.  Tools in **`rootfs/bt/`** (see its `README.md`).
* **The service pack loads and it is what registers the controller.**  Replay
  `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` with `btsload`:
  **315/315** answered, LMP subversion `0x2b0f` → **`0x2b33`**.  With the pack
  loaded **before** the attach, `MGMT_OP_READ_INDEX_LIST` returns **1**
  controller; without it, **0**.  Negative control run both ways.
* **Why `hci0` never worked before (trap 249).**  `0x201c` is
  `HCI_LE_Read_Supported_States`, not a "benign TI vendor op".  A bare ROM
  returns **Command Disallowed `0x0c`** with 5 payload bytes where 9 are needed,
  so `hci_cc_func()` rejects it, the LE init stage aborts, `hci0` never opens and
  mgmt sees no controller.  The pack fixes the **length** (11 → 12 bytes).
* The pack **survives `HCI_Reset`** but is lost on a `BT_EN` power cycle.

### THE ONE EXPERIMENT: find who emits the stray `0x30`

`MGMT_OP_SET_POWERED` returns `0x05` (`AUTH_FAILED` = `-ETIMEDOUT`) and dmesg
shows `Frame reassembly failed (-84)` (`-EILSEQ`).  An instrumented `h4_recv()`
(already written and proven, in the session-53 note) shows the identical shape in
**6 of 6 runs**:

```
PBTMP h4_recv count=9 first=04 0e 06 01  rx_len=-1: 04 0e 06 01 12 0c 00 00 00
PBTMP h4_recv count=1 first=30 00 00 00  rx_len=-1: 30
>>> Bluetooth: hci0: Frame reassembly failed (-84)
```

The last init reply is complete and correct; **~200 ms later a single `0x30`**
arrives and is (correctly) rejected, because `0x30` is not an H4 type (0x01–0x04).
**The open question is which side emits it.**  It *never* appears when the same
sequence is driven from userspace (including a passive listen with no command
sent), so a host-side origin is favoured — but that is not established.

**Do the module swap correctly** (trap 251 — this bit me): `btbcm` holds a
reference to `hci_uart`, so `rmmod hci_uart` fails and `insmod` "succeeds" while
keeping the old module, making the experiment look negative.  Order:
`rmmod btbcm`, then `rmmod hci_uart`, then `insmod /tmp/hci_uart.ko`, then
**verify by content** — `strings /tmp/hci_uart.ko | grep PBTMP`.  A reboot is the
sure reset (`/tmp` is wiped; re-push the tools).

**Extend the instrument** to log what the driver *writes* (`hci_uart_write_work`
in `hci_ldisc.c`) next to what it receives, with timestamps.  If a `0x30` shows
up on the wire with nothing written in the preceding milliseconds, it is the
chip; if it correlates with a write, the host path is echoing.

**What each result means.**

* **The `0x30` correlates with a host write** → the fix is in the kernel path, and
  the `hci_uart` write/flush path is the place to look.
* **Nothing is written and the `0x30` still arrives** → the chip emits it; then
  ask whether a quirk/flush is the right response (the kernel's `hci_uart_flush`
  or a `HCI_QUIRK_*`), not a bespoke driver.
* **Open the controller (`SET_POWERED` → status `0x00`)**, then run an inquiry
  through the stack and move to HID.

### Before expecting HID

`l2cap`/`sco`/`rfcomm`/`hidp` are **modules and none are in the region-1 rootfs**
(only `bluetooth`, `hci_uart`, `btbcm` and their crypto deps are).  Ship them.
`CONFIG_BT_HIDP=m` is already set in the kernel config.

### Cold-boot gotchas for the BT bench (both cost real time)

1. **`/sys/class/gpio/gpio535` does not exist until exported**, and it reads `0`
   (BT_EN low, core off) — so a pack load fails with "no HCI_Reset answer",
   identical to a dead chip.  Export it, `direction` = `out`, then pulse
   **low 2 s → high** (50 ms sometimes was not enough after a previous sleep).
2. **`insmod` "succeeds" while keeping the old module** when something holds a
   reference.  Verify swaps by content, never by exit status.

### LE

**Refused by the controller**: `0x201c` = Command Disallowed,
`Write_LE_Host_Support` = `0x11`, LE feature page all zeros.  **But do not write
the hardware off**: the chip reports **HCI 4.0 / LMP 4.0** with the LE-supported
LMP bit, and the reference stack being BR/EDR-only reflects BlackBerry never
shipping an LE stack on a 2011 tablet — not silicon.  What is missing is an
enable sequence, and none is known.  The `wl128x-fw-4-sr/5-sr.bin` files are
**WLAN** firmware — swapping them cannot change BT.

## 0d. WM8994 audio — closed

Headphones and both speakers play `pbplay`'s 440 Hz tone at 48 kHz ±1 %.
Listening tests: lower `pbmix set "DAC1 Volume" 40` **once**, restore `96` at the
end; a bare `pbplay` plays a 2 s tone (trap 212); state what is playing
(trap 191).

## 0e. Touch — done; cold boots need the pulse

I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c`; GPIO2_3 edge IRQ
(`irq 110`).  A **cold** boot leaves the controller down until GPIO1_14/GPIO4_8
are pulsed — the driver still does not drive them.

## 0f. The minimal 1917 oracle

Debloated 2.1.0.1917 IFS+RCFS (`tools/build_minimal_qnx.py`), qconn on
`169.254.0.1:9090` + root ssh.  Its `/base` is pruned (no firmware dir).  No
wifi/BT service runs, so `ifconfig ti0 up` and the BT start are manual.  Tools
are not on the UFS — push `memdump3`/`memw32`/`pbi2c` to `/tmp` after every boot.
**Bench only** (trap 173).

## 1. Useful local extracts

* `rootfs/bt/` — **the BT bench tools and their README** (session 53).
* `local/work/1917/` — the 1917 `/base`, IFS tree and the WLAN calibration.
* `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` — the **correct** BT
  service pack (md5 `78bc1f0a…`; `Compatibility: Quattro, 6.0.15 ROM`).
  linux-firmware's `TIInit_*.bts` are for `BL6450, WL1271/3 2.0` — a different
  chip family; the contradiction is resolved against them.
* `local/work/firmware-nvs/` — the firmware tree with the **per-unit NVS**.
* `local/work/modules-7.3.3/` — the staged modules (**43**).
* `tools/build_minimal_qnx.py`, `local/work/minimal/` — the oracle.
* Reports **238** (current), **232–237**; `hardware/twl6030.pdf`,
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

**Build modules IN-TREE, not with `M=`.**  Strip with `--strip-debug`, never
`strip -s`.  A module/firmware-only change keeps the region-0 CRC (trap 190) and
needs a **region-1-only flash** at block `0x2915`:

```bash
python3 tools/build_rootfs.py --size-mb 64 \
  --busybox local/work/src/busybox-1.36.1/busybox \
  --busybox-links local/work/src/busybox-1.36.1/busybox.links \
  --modules local/work/modules-7.3.3 --firmware local/work/firmware-nvs \
  --extras local/work/rootfs-extras --krel 7.3.3 --out local/images/<name>.ext4
```

`build_rootfs.py --out` **overwrites** (trap 213).  **Patch export** (traps
161/211): walk the tree against pristine, filter build artifacts, apply to a
staged pristine copy and compare.  Current: **61 files / 8 762 lines**, sha256
`20556c5f…`.

## 3. Bench

* **`tools/pbssh.sh 'cmd'`** (root, key-only, `169.254.0.1`); `PB_LINK=usb|door`.
  Dropbear has **no `scp`** — move files with `ssh … 'cat > /path' < file`, or
  raw `nc`/`socat` for anything large (6.3 MB/s vs ssh's 2.3).
* **Reboot:** `tools/pbssh.sh 'reboot -f'`; if it hangs (trap 224) use
  `echo b > /proc/sysrq-trigger`.  Start the flasher as a **background task** —
  a `nohup … &` inside a foreground shell call keeps the tool's pipe open.
* **Never `slay` by name on the oracle** (trap 221); kill by pid.
* Device password `0000`, once.  An agent never writes RPMB; NVRAM read-only.
* **`BOOTCTR_FCTRY` is readable on our image**:
  `tools/pbssh.sh 'dmesg | grep pbrpmb'` → `… record@196 = <4 bytes LE>` then
  `0x<v> >= threshold 0x1e, no write`.  It read **`0x22`** at the session-53
  close (`22000000`; `0x23` at the start — one reboot for a clean module state).
  A logged read, not an RPMB access (`SAFETY.md` §2).
* **WLAN checks:** `lsmod` must show **`ccm` and `ctr`**;
  `ip link set wlan0 up` → `wlcore: firmware booted (Rev 7.3.5.0.98)`.
* **BT checks:** `BT_EN` at pad `0x4A10007C` mux 3 and `GPIO2_DATAOUT` bit 23;
  UART2 pads `0x4118`/`0x411A`; then `rootfs/bt/`'s tools.  **`btdump` first** —
  it hexdumps and cannot hide a reply.  Check `cat /proc/tty/ldiscs` before
  concluding "silent" (a stale `N_HCI` eats the bytes).
* WM8994: `i2ctransfer` args are DECIMAL (trap 166); bus 4, addr `0x1a`.
* QNX device tools build `-static`; `/tmp` is wiped by a reboot.

## 4. Traps added last session (full list 14–250 in `KNOWN-ISSUES.md` §2)

* **249.** `0x201c` is **`HCI_LE_Read_Supported_States`**, not a "benign TI
  vendor op" (report 237 §4 is wrong).  A bare ROM answers it **Command
  Disallowed** with 5 payload bytes where 9 are needed, so `hci_cc_func()`
  rejects it, the LE init stage aborts, and **`hci0` never registers with mgmt**
  (`num_controllers = 0`) — so `SET_POWERED` was structurally impossible.
  Loading `WL1283.bts` first fixes the length and the controller registers.
  `/sys/class/bluetooth/hci0` existing is **not** liveness.
* **250.** `Frame reassembly failed (-84)` = `-EILSEQ`, and on this board it is a
  **lone `0x30` byte** arriving ~200 ms after init completes (instrumented
  `h4_recv()`, 6 of 6 runs).  `0x30` is not an H4 type, so the kernel correctly
  rejects it.  **Which side emits it is open** — it never appears from userspace.
  Excluded: alignment/padding, chip idle-sleep, leftover bytes, UART line errors
  (`LSR = 0x60`), truncated long replies, and **hardware flow control** (that
  early claim is retracted: H4 never calls `hci_uart_set_flow_control()`, and a
  clean A/B registers the controller with `CRTSCTS` on *and* off).
* **251.** `insmod` reports success while keeping the old module when something
  references it (`btbcm` → `hci_uart`).  `rmmod btbcm` first, then `hci_uart`,
  then `insmod`; verify by content, not exit status.  Also the way a reverted
  experiment looks un-reverted.
* **252.** After a cold boot `/sys/class/gpio/gpio535` does not exist and reads
  `0`, so a pack load fails as if the chip were dead.  Export, set direction,
  pulse low 2 s.
* Still current: **249** (`0x201c` is LE states, not benign), **248** (the UART2
  CTS/RTS pads), **247** (no HT40), **244** (`ccm`/`ctr`), **246** (MUSB
  alignment), **243** (4-sr not 5-sr), **241**, **239**, **238** (`LEVEL_LOW`
  WL_IRQ), **237** (three wlcore board properties), **236** (`CLK32KG`), **227**
  (the old measurement flaw), **224** (`reboot -f` wedge), **221** (`slay` kills
  `rndis0`), **214** (composite net iface), **212** (bare `pbplay` plays), **211**
  (tree walk + `Kconfig.*`), **190** (module-only CRC).

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **BT bench tools** | **`rootfs/bt/`** (and its `README.md`) |
| wireless rootfs builder | `tools/build_rootfs.py` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| BT service pack | `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` |
| OSS player / mixer / BT probe | `rootfs/pbplay.c`, `rootfs/pbmix.c`, `rootfs/hci.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| reports | `docs/reports/` (**238** current) |
| sessions | `docs/sessions/session-53.md` |

## 6. Working rules

* After every console command, dump the log tail and read it.  Read
  `dmesg`/`sloginfo` **unfiltered** after a reboot or structural change.
* **Don't clear `dmesg` under the operator** without saying so.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  **A test boot is not a budget concern.**
* **Never present a guess as a fact.**  Prefer a fresh read.
* **A diagnostic that reports "nothing" may be lying.**  Three ways they did in
  session 53 (buffered stdout, wrong Command Status offset, stale `N_HCI`) —
  `rootfs/bt/README.md` lists them.  When in doubt, hexdump.
* **Use the reference before the bench** (DECISIONS §61).  Subagents:
  static/file-only (trap 109) — and **time-box them**; one ran 87 minutes with no
  result in session 53 and had to be killed.
* **Revert experiments and prove the revert.**
* **Only the operator decides when to stop / wrap up.**

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 54 (absolute; Linux-era 48)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 9, KNOWN-ISSUES.md §2 (traps 14-250, especially 244-250)
and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md (69-73),
docs/reports/INDEX.md, reports 236-238, and sessions/session-53.md.

WHERE IT STANDS: audio (headphones + both speakers, 48 kHz), the touch, the
sensors/charger, ssh over USB, the display and the WL1283 WLAN all work.  THE
WLAN ASSOCIATES (session 52) — the blocker was the missing ccm/ctr modules.
BT (session 53): THE CORE WORKS.  Over /dev/ttyS1 as H4 it answers HCI_Reset,
reports hci ver 6 / lmp ver 6 / mfr 0x002b (TI, BT 4.0), BD_ADDR
8e:e4:3c:22:61:00, and RUNS A FULL BR/EDR INQUIRY that finds a peer
(f0:a3:b2:bb:d1:00, class 0x011c00), reproducibly.  Loading the reference's
local/work/1917/base/lib/firmware/bluetooth/WL1283.bts (315/315 commands, LMP
subversion 0x2b0f -> 0x2b33) BEFORE the H4 attach makes hci0 REGISTER WITH MGMT
for the first time (READ_INDEX_LIST 0 -> 1 controller, negative control both
ways) by fixing the length of the LE_Read_Supported_States (0x201c) reply
(11 -> 12 bytes).  Tools are in rootfs/bt/ — READ ITS README FIRST, it lists
three ways a diagnostic lied.

WHAT IS LEFT: OPEN THE CONTROLLER.  MGMT_OP_SET_POWERED returns 0x05
(-ETIMEDOUT) after "Frame reassembly failed (-84)" (-EILSEQ).  The cause is
LOCATED but not fixed: an instrumented h4_recv() shows a LONE 0x30 BYTE arriving
~200 ms after the last complete init reply, in 6 of 6 runs; 0x30 is not an H4
packet type, so the kernel correctly rejects it.  WHICH SIDE EMITS IT IS OPEN --
it never appears when the same sequence is driven from userspace, so a host-side
origin is favoured.  Next: extend the instrument to log what hci_uart WRITES
(hci_uart_write_work in hci_ldisc.c) alongside what it receives, with timestamps.
When swapping the module: rmmod btbcm FIRST, then hci_uart, then insmod, and
verify by content (strings mod.ko | grep PBTMP) -- insmod "succeeds" while
keeping the old module and the experiment silently looks negative (trap 251).
Excluded already: alignment/padding, chip idle-sleep, leftover bytes, UART line
errors (LSR=0x60), truncated long replies, and hardware flow control (an A/B on
a clean boot registers the controller with CRTSCTS on AND off; H4 never calls
hci_uart_set_flow_control() -- an earlier claim to the contrary is retracted).
Then ship l2cap/sco/rfcomm/hidp (CONFIG_BT_HIDP=m is set but the modules are NOT
in region 1) and a stack.  LE is refused by the controller (0x0c,
Write_LE_Host_Support=0x11, LE feature page zero) BUT the chip reports BT 4.0
with the LE-supported bit, and the reference being BR/EDR-only reflects the
vendor never shipping an LE stack -- not the silicon -- so no enable sequence is
known, and impossibility is NOT demonstrated.  The wl128x-fw-4-sr/5-sr.bin files
are WLAN firmware — swapping them cannot change BT.
COLD BOOT: /sys/class/gpio/gpio535 does not exist until exported and reads 0, so
a pack load fails as if the chip were dead; export it, direction=out, pulse
low 2 s -> high (trap 252).
Then: FM (forward-port drivers/media/radio/wl128x, removed in 6.13) and GPS
(2024 LKML patch, else RE the reference).  Open, neither blocking: why 5-sr
panics on scan (235 §3) and why the ELP wake is not honoured (232 §6).
Do NOT re-run the session-49/50 negatives (readable diff 218, replays 217,
boot-path RE 221-226, domain test 225, BT probe 228, clock sweep 229).  Do NOT
use IRQ_TYPE_LEVEL_LOW for WL_IRQ (trap 238), NOT wlcore_sdio dump=1 (trap 239),
NOT 5-sr (trap 243), NOT HT40 (trap 247).  Do NOT pulse BT_EN after loading the
service pack (it wipes it; HCI_Reset does not).

PHYSICAL STATE: region 0 = our Linux image 0xDD66D8C3 (0x939E4C, kernel 7.3.3).
Region 1 = 49e318bf... (43 modules) —
local/images/rootfs-region1-wl1283-bt.ext4.  Kernel patch 61 files / 8 762
lines, sha256 20556c5f... (no re-export needed unless the DTS/driver changes).
The unit is on LINUX (0fca:8010, console `pb> `, ssh on 169.254.0.1); rcS loads
the wireless stack but does not raise the link — see HANDOFF §0b for the
association sequence.  To boot the ORACLE: flash
local/work/ifs-shim-sealed.ifs at 0x825 (the RCFS slot holds the FULL 1917 RCFS,
trap 227; the pruned oracle pair is local/work/minimal/setB.signed at 0x8D5).
/tmp is wiped by a reboot.  To stock TabletOS: ifs-shim-sealed.ifs at 0x825 +
rcfs-1917.rcfs at 0x8D5 (NOT wayback.signed, trap 226).  Do NOT restore
local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

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
0x4118/0x411A; /dev/ttyS1.  Use rootfs/bt/ (build one, push to /tmp; dropbear
has no scp).  ALWAYS check `cat /proc/tty/ldiscs` — a stale N_HCI eats the
bytes and the chip looks dead.  WL1283 checks: `i2ctransfer -f -y 0 w1@0x48 0xbc
r3` -> 07 01 55; `lsmod` must show ccm and ctr; `ip link set wlan0 up` ->
firmware Rev 7.3.5.0.98.  WM8994: i2ctransfer args DECIMAL, bus 4 addr 0x1a;
debugfs is not mounted by rcS.  On the oracle use pbi2c rd16/wr16; push
memdump3/memw32/pbi2c to /tmp after every boot.

LISTENING TESTS ARE QUIET AND SHORT: lower `pbmix set "DAC1 Volume" 40` ONCE,
run every test, restore 96 ONCE at the end; a bare `pbplay` plays a 2 s tone
(trap 212); state what is playing (trap 191).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4; for a QNX ELF reconcile
with ntoarmv7-readelf/objdump.  QNX device tools build -static.  USE THE
REFERENCE FIRST, and put static RE in subagents (file-only, trap 109) — and
TIME-BOX them (one ran 87 min with no result in session 53).  The TI TRM is
local (hardware/swpu231ap.pdf.txt); the TWL6030 datasheet is
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

