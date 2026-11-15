# Handoff — session 53 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 54 = Linux-era 48**, written at the
session-53 close.  See `docs/sessions/NUMBERING.md`.

> **Session-53 result, in two lines.**  **BT's core works** — real BR/EDR
> inquiry finding a peer, and loading the reference's service pack (315/315)
> makes `hci0` register with mgmt for the first time; the controller *open* is
> blocked by a stray `0x30` byte.  **The operator then redirected the project**:
> the active work is now **ROADMAP item 7a — a real root on hd3 (62.86 GiB),
> then SGX540**, with **Nura** (postmarketOS was renamed 2026-09-27) and
> Catacomb or Sxmo-dwm as the DE.  Region 0 `0xDD66D8C3`; region 1
> `49e318bf…`; patch **61 files / 8 762 lines**, sha256 `20556c5f…` (unchanged —
> no flash, no DTS or driver edit this session).

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real userland, and the
board's own peripherals working — now with the end state being **Nura on hd3
with GPU acceleration**.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console **and its RNDIS network link with ssh**, the whole display chain,
the 64 MiB ext4 BusyBox root in region 1, the peripherals (I²C4 motion sensors,
TMP103, BQ27541, FAN5702, CM3217, ISL9519 charger), the **Munin touch**, the
**WM8994 audio** (headphones + both speakers, 48 kHz), the **WL1283 WLAN**
(associates, DHCP, traffic), and **the WL1283's BT core** (real inquiry; `hci0`
registers with mgmt).

**What is left, in order:** the hd3 rootfs (§0d), then SGX540 (§0e).  BT's
controller open and FM/GPS are parked with their blockers recorded (§0f).

## 0a. The media, and the way back

* **region 0 = `0xDD66D8C3`** (`0x939E4C` B, kernel `7.3.3`,
  `boot/chainloader/kernel.img`; the CRC was recomputed this session and
  matches).  Unchanged in session 53.
* **region 1 = `49e318bf…`** (`local/images/rootfs-region1-wl1283-bt.ext4`,
  67 108 864 B, **43 modules**).  **This stays intact as the fallback through
  the whole hd3 move.**
* The unit is **on our Linux image** (`0fca:8010`, console `pb> `, **ssh on
  `169.254.0.1`**).
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`, then reboot.  It enumerates `0fca:8012`; qconn
  `169.254.0.1:9090`, root ssh — use **`PB_LINK=door tools/pbssh.sh`**.  The
  region-0 RCFS slot holds the **full 1917 RCFS** (trap 227); for the *minimal*
  oracle flash the pruned pair `local/work/minimal/setB.signed`.  `/tmp` is
  wiped by a reboot.
* **To stock TabletOS:** flash **two components** — `local/work/ifs-shim-sealed.ifs`
  at `0x825` **and** `local/work/rcfs-1917.rcfs` at `0x8D5`, plus the vendor UFS
  (`local/work/ufs-vendor.signed`, 707 MB) if hd3 has been repurposed.
  **Do not restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).
* **hd3 currently holds the QNX6 userland.**  Writing it destroys that on media;
  the operator has the UFS images, so **no backup is required** and a reflash
  restores it.

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

## 0c. BT — what works, and where it stopped

**Proven (report 238):** over a plain H4 tty (`/dev/ttyS1`, 115200 8N1) the core
answers `HCI_Reset`, reports `hci ver 6` / `lmp ver 6` / `mfr 0x002b` (TI) /
BD_ADDR `8e:e4:3c:22:61:00`, and **runs a full BR/EDR inquiry that finds a peer**
(`f0:a3:b2:bb:d1:00`, class `0x011c00`), reproducibly.  Replaying
`local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` answers **315/315** and
moves LMP subversion `0x2b0f` → `0x2b33`.  Loaded **before** the attach it makes
`hci0` **register with mgmt** (0 → 1 controller; negative control both ways) by
fixing the `0x201c` reply length.

**Parked blocker (trap 250):** a stray `0x30` byte.  After the last init reply
completes, ~200 ms later a single `0x30` arrives (6 of 6 runs, instrumented
`h4_recv()`); it is not an H4 type, so the kernel correctly returns `-EILSEQ` and
`SET_POWERED` gives `0x05`.  **Which side emits it is unknown.**  The experiment
to resume is in STATE.md §"What is next" 1a(b) — and note trap 251 (verify a
module swap by content) before running it.

**LE** is refused by the controller, but the chip reports BT 4.0 with the
LE-supported bit, so no enable sequence is known and impossibility is **not**
demonstrated.

## 0d. THE ACTIVE ITEM — a real root on hd3 (report 239, ROADMAP 7a)

### The design, which is the operator's

**hd3 holds a flasher-restorable ext4 image.**  Not an in-place mkfs: a broken
Linux root must be recoverable from the bootrom without needing a working Linux
on the device.  The loader's allow-list already includes MCT type `0x1C` and
`bbusb.py` encodes the extent (`'0x1C FS Fixed': (0x4905, 0xFFFFF)`), so the
existing flash loop works unchanged.

### THE ONE EXPERIMENT: does `pmbootstrap` build a rootfs for this device?

**Inputs:** a host with `pmbootstrap`, a scaffolded PlayBook device port
(`pmbootstrap init` + `aportgen`, modelled on `device-samsung-espresso7`:
`arch=armv7`, `dtb=omap4-winchester`, `chassis=tablet`, `drm=true`).  **No device
writes.**  Run `pmbootstrap install` with `ui=catacomb` (or `sxmo-de-dwm`) and
`split` so it produces image files rather than flashing.

**What each result means.**

* **`pmbootstrap install` produces a root image** → the plan holds.  Then: mount
  it on the host, check `/sbin/init`, the DE and that its kernel modules match
  7.3.3; flash it to hd3 with `bbusb.py --component <root image>
  --dest-block 0x4905` (dry-run first — the plan line must show a run inside
  `0x1C` and `gate_runs` must pass); verify by `dd`-ing the superblock at
  `0x49000000` and by mounting it from the running Linux
  (`losetup -o 0x49000000`) and comparing a checksum with the host image.
* **It refuses because it also wants to own the kernel/boot** → the device port
  must express "the boot chain is external" more explicitly.  Report 239 §4 has
  the analysis; the fallback is to build the rootfs with `pmbootstrap` but take
  the kernel from our tree, which is what we already do.
* **A Nura `linux-*` package cannot wrap the 7.3.3 tree unchanged** → package
  our tree as a `linux-*` aport, or bypass the package entirely and keep
  `build_kernel_image.py` as the only kernel path.

**Then, and only then:** re-point `initramfs/init.c`'s `ROOTFS_RAW_OFF` from
`0x29000000` to `0x49000000`, rebuild the kernel image, flash region 0, boot.
The initramfs already falls back to the bring-up console when the root will not
mount, so a bad hd3 costs a boot, not a brick.  **Region 1 stays intact.**

### Size is a policy choice, not a constraint

`analyze_blocks()` skips runs of **`0xFF`**, not `0x00`, so a sparse ext4 does
**not** get its holes skipped: a fully expanded 62.86 GiB image would send all of
it over the bootrom link.  Sizes: 1 GiB = 1.59 % of hd3, 2 GiB = 3.18 %, 4 GiB =
6.36 %.  Grow by building a larger image and flashing it over the seed — the
device has **no `resize2fs`**.

### Facts about hd3 (measured)

* `0x49000000` = DOS MBR with a QNX **v1.2b** boot sector.
* `0x49002000` = QNX6 superblock (magic `5d 55 29 c2 3e 56 45 21`) — agrees with
  report 138, not with the naive MCT arithmetic (`0x48F00000`).
* Region = MCT `0x4905–0xFFFFF` = **62.86 GiB** (67,494,412,288 B).
* In-place mkfs works on the device (`losetup -f -o` → `mkfs.ext2` → mount →
  write), useful for *populating*, not as the primary route.

## 0e. SGX540 (after the rootfs boots)

**Mainline 7.3.3 has no driver** — nothing binds `ti,omap4430-gpu` /
`img,powervr-sgx540`, though `omap4.dtsi` supplies the target-module, `prm_gfx`
and the L3_GFX clock, and our board DTS leaves `&sgx_module` disabled.  The route
is `gitlab.com/pvrports/pvrports`: `linux-openpvrsgx`
(`CONFIG_SGX=y`, `CONFIG_SGX_OMAP=m`, `CONFIG_SGX_DRM=y`, tagged 6.1 → rebase to
7.3.3) plus **proprietary** `sgx-ddk-um` (`pkgver=1.17.4948957`, SoC subpackage
`ti443x`).  Its reference devices are the **Samsung espresso** — OMAP4430 with
TWL6030, the same SoC family and PMIC generation, including the `clk32kg` work we
already did for the WLAN.  **Blobs stay gitignored in `local/`.**  The
reference's own QNX SGX userspace (`libsrv_um.so`, `libusc.so`, GLESv1/v2, EGL)
is in `local/work/1917/base/usr/lib/graphics/omap4430/` with
`gpu-string = SGX540rev120`, but it is QNX ABI — **there is no separate SGX
firmware file anywhere in the reference**.

## 0f. FM and GPS (parked, after SGX)

**FM:** mainline carried `drivers/media/radio/wl128x` from 2.6.39 to 6.13 and
removed it; a forward port is the route.  **GPS:** no mainline driver; the
operator has a 2024 LKML patch (`lists.openwall.net/linux-kernel/2024/06/06/1445`)
to evaluate, and the reference's own path is the fallback.

## 0g. WM8994 audio, touch — closed

Audio: headphones and both speakers play `pbplay`'s 440 Hz tone at 48 kHz.
Listening tests: lower `pbmix set "DAC1 Volume" 40` **once**, restore `96` at the
end; a bare `pbplay` plays a 2 s tone (trap 212).  Touch: I²C2 `0x48072000` =
`i2c-1` at `0x65`, GPIO2_3 edge IRQ (`irq 110`); a **cold** boot leaves it down
until GPIO1_14/GPIO4_8 are pulsed, which the driver does not drive.

## 0h. The minimal 1917 oracle

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
* `local/work/1917/base/usr/lib/graphics/omap4430/` — the reference's QNX SGX
  userspace and `graphics.conf` (`gpu-string = SGX540rev120`).
* `local/work/firmware-nvs/` — the firmware tree with the **per-unit NVS**.
* `local/work/modules-7.3.3/` — the staged modules (**43**).
* Reports **239** (hd3/Nura/SGX scoping), **238** (BT), 232–237.

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

`build_rootfs.py --out` **overwrites** (trap 213).  **Flash hd3** with
`bbusb.py --component <image> --dest-block 0x4905` (dry-run first).
**Patch export** (traps 161/211): walk the tree against pristine, filter build
artifacts, apply to a staged pristine copy and compare.  Current: **61 files /
8 762 lines**, sha256 `20556c5f…`.

## 3. Bench

* **`tools/pbssh.sh 'cmd'`** (root, key-only, `169.254.0.1`); `PB_LINK=usb|door`.
  Dropbear has **no `scp`** — move files with `ssh … 'cat > /path' < file`, or
  raw `nc`/`socat` for anything large (6.3 MB/s vs ssh's 2.3).
* **Reboot:** `tools/pbssh.sh 'reboot -f'`; if it hangs (trap 224) use
  `echo b > /proc/sysrq-trigger`.  Start the flasher as a **background task**.
* **Never `slay` by name on the oracle** (trap 221); kill by pid.
* Device password `0000`, once.  An agent never writes RPMB; NVRAM read-only.
* **`BOOTCTR_FCTRY` is readable on our image**:
  `tools/pbssh.sh 'dmesg | grep pbrpmb'` → `… record@196 = <4 bytes LE>` then
  `0x<v> >= threshold 0x1e, no write`.  It read **`0x22`** at the session-53
  close (`22000000`; `0x23` at the start — one reboot).  A logged read, not an
  RPMB access (`SAFETY.md` §2).
* **WLAN checks:** `lsmod` must show **`ccm` and `ctr`**;
  `ip link set wlan0 up` → `wlcore: firmware booted (Rev 7.3.5.0.98)`.
* **BT checks:** `BT_EN` at pad `0x4A10007C` mux 3 and `GPIO2_DATAOUT` bit 23;
  UART2 pads `0x4118`/`0x411A`; then `rootfs/bt/`'s tools.  **`btdump` first** —
  it hexdumps and cannot hide a reply.  Check `cat /proc/tty/ldiscs` before
  concluding "silent".  After a cold boot **export `gpio535` first** (trap 252).
* **hd3 checks:** `dd` the superblock at `0x49000000`; mount from the running
  Linux with `losetup -o 0x49000000`.
* WM8994: `i2ctransfer` args are DECIMAL (trap 166); bus 4, addr `0x1a`.
* QNX device tools build `-static`; `/tmp` is wiped by a reboot.

## 4. Traps added last session (full list 14–252 in `KNOWN-ISSUES.md` §2)

* **249.** `0x201c` is **`HCI_LE_Read_Supported_States`**, not a benign TI vendor
  op.  A bare ROM answers **Command Disallowed** with 5 payload bytes where 9 are
  needed, so the LE init stage aborts and **`hci0` never registers with mgmt**
  (`num_controllers = 0`).  Loading `WL1283.bts` first fixes the length.
  `/sys/class/bluetooth/hci0` existing is **not** liveness.
* **250.** `Frame reassembly failed (-84)` = `-EILSEQ`, here a **lone `0x30`**
  arriving ~200 ms after init (6 of 6 runs).  Which side emits it is **open**.
  Hardware flow control is **not** the cause (clean A/B registers with `CRTSCTS`
  on *and* off; H4 never calls `hci_uart_set_flow_control()`).
* **251.** `insmod` reports success while keeping the old module when something
  references it (`btbcm` → `hci_uart`).  `rmmod btbcm` first, then `hci_uart`;
  verify by content, not exit status.
* **252.** After a cold boot `/sys/class/gpio/gpio535` does not exist and reads
  `0`, so a pack load fails as if the chip were dead.  Export, set direction,
  pulse low 2 s.
* Still current: **248** (the UART2 CTS/RTS pads), **247** (no HT40), **244**
  (`ccm`/`ctr`), **246** (MUSB alignment), **243** (4-sr not 5-sr), **241**,
  **239**, **238** (`LEVEL_LOW` WL_IRQ), **237** (three wlcore board
  properties), **236** (`CLK32KG`), **227** (the old measurement flaw), **224**
  (`reboot -f` wedge), **221** (`slay` kills `rndis0`), **214** (composite net
  iface), **212** (bare `pbplay` plays), **211** (tree walk + `Kconfig.*`),
  **190** (module-only CRC).

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **the active plan (hd3/Nura/SGX)** | `docs/project/ROADMAP.md` item 7a, report **239** |
| **BT bench tools** | **`rootfs/bt/`** (and its `README.md`) |
| wireless rootfs builder | `tools/build_rootfs.py` |
| flasher (region 0, region 1, **hd3**) | `tools/bbusb.py` |
| initramfs root selection | `initramfs/init.c` `ROOTFS_RAW_OFF` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| BT service pack | `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` |
| reference SGX userspace | `local/work/1917/base/usr/lib/graphics/omap4430/` |
| OSS player / mixer / BT probe | `rootfs/pbplay.c`, `rootfs/pbmix.c`, `rootfs/hci.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| reports | `docs/reports/` (**239** current) |
| sessions | `docs/sessions/session-53.md` |

## 6. Working rules

* After every console command, dump the log tail and read it.  Read
  `dmesg`/`sloginfo` **unfiltered** after a reboot or structural change.
* **Don't clear `dmesg` under the operator** without saying so.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  **A test boot is not a budget concern.**
* **Never present a guess as a fact.**  Prefer a fresh read.  Two claims from
  this session were retracted after re-measurement (trap 250); a diagnostic can
  manufacture the fault it reports.
* **Use the reference before the bench** (DECISIONS §61).  Subagents:
  static/file-only (trap 109) — and **time-box them**; one ran 87 minutes with no
  result in session 53.
* **Look up every external fact.**  postmarketOS was renamed **Nura** on
  2026-09-27; search "Nura", and expect `postmarketos`/`pmaports` in package
  names and many URLs.
* **Revert experiments and prove the revert.**
* **Only the operator decides when to stop / wrap up.**

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 54 (absolute; Linux-era 48)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a AND item 9, KNOWN-ISSUES.md §2 (traps 14-252,
especially 248-252) and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md (61-73),
docs/reports/INDEX.md, reports 238-239, and sessions/session-53.md.

WHERE IT STANDS: audio (headphones + both speakers, 48 kHz), the touch, the
sensors/charger, ssh over USB, the display and the WL1283 WLAN all work.  BT
(session 53): THE CORE WORKS — over /dev/ttyS1 as H4 it answers HCI_Reset,
reports hci ver 6 / lmp ver 6 / mfr 0x002b (TI, BT 4.0), BD_ADDR
8e:e4:3c:22:61:00, and RUNS A FULL BR/EDR INQUIRY finding a peer
(f0:a3:b2:bb:d1:00, class 0x011c00).  Loading the reference's
local/work/1917/base/lib/firmware/bluetooth/WL1283.bts (315/315, LMP subversion
0x2b0f -> 0x2b33) BEFORE the H4 attach makes hci0 REGISTER WITH MGMT for the
first time (READ_INDEX_LIST 0 -> 1 controller, negative control both ways).
Tools are in rootfs/bt/ — READ ITS README FIRST.

THE ACTIVE ITEM (operator, session 53): ROADMAP item 7a — A REAL ROOT ON hd3
(62.86 GiB), THEN SGX540.  The design is the operator's: hd3 holds a
FLASHER-RESTORABLE ext4 image (not an in-place mkfs), so a broken root is
recoverable from the bootrom.  The loader allow-lists MCT type 0x1C and
bbusb.py encodes the extent ('0x1C FS Fixed': (0x4905, 0xFFFFF)), so the
existing flash loop works: bbusb.py --component <image> --dest-block 0x4905.
Userspace = Nura (postmarketOS was RENAMED Nura on 2026-09-27 — search "Nura")
with Catacomb or Sxmo-dwm.  The PlayBook is NOT a supported device; model the
port on device-samsung-espresso7 (OMAP4430, TWL6030).  Note the structural
difference: espresso flashes with heimdall-bootimg, which we have no equivalent
of — OUR kernel must stay the CRC-sealed chainloader image, so the Nura kernel
package is NOT what flashes.

THE ONE EXPERIMENT: a host-side pmbootstrap init + install with a scaffolded
PlayBook device port (arch=armv7, dtb=omap4-winchester, chassis=tablet, drm=true)
and ui=catacomb, using split image files.  NO DEVICE WRITES.  It answers: (a)
will pmbootstrap build a rootfs for a device whose kernel it does not own? (b)
can a Nura linux-* package wrap our 7.3.3 tree (61 files / 8 762 lines, sha256
20556c5f...)?  If yes: mount the root image on the host and check /sbin/init +
the DE + module versions; dry-run the hd3 flash and require a run inside 0x1C;
flash; verify the superblock at 0x49000000 and a checksum; then re-point
initramfs/init.c ROOTFS_RAW_OFF from 0x29000000 to 0x49000000, rebuild, flash
region 0, boot.  REGION 1 STAYS INTACT as the fallback throughout.

SIZE IS POLICY, NOT CONSTRAINT: analyze_blocks() skips 0xFF runs, not zeros, so
a sparse ext4 does NOT get its holes skipped — a full 62.86 GiB image sends all
of it.  1 GiB = 1.59% of hd3, 2 GiB = 3.18%, 4 GiB = 6.36%.  The device has NO
resize2fs, so grow by flashing a bigger image.

hd3 TODAY (measured): 0x49000000 = DOS MBR with a QNX v1.2b boot sector;
0x49002000 = QNX6 superblock (magic 5d 55 29 c2 3e 56 45 21).  Writing it
destroys the QNX userland on media; the operator has the UFS images so no backup
is required, and reflash restores it.

SGX (after the rootfs boots): mainline 7.3.3 binds NOTHING to ti,omap4430-gpu
though omap4.dtsi has the target-module, prm_gfx and L3_GFX clock.  Route:
gitlab.com/pvrports/pvrports — linux-openpvrsgx (CONFIG_SGX=y, CONFIG_SGX_OMAP=m,
CONFIG_SGX_DRM=y; tagged 6.1, so rebase to 7.3.3) plus PROPRIETARY sgx-ddk-um
(1.17.4948957, subpackage ti443x).  Reference devices are OMAP4430 Samsung
espresso.  Blobs stay gitignored in local/.  The reference's QNX SGX userspace
(graphics.conf: gpu-string = SGX540rev120) is QNX ABI and there is NO separate
SGX firmware file in it.

PARKED: BT controller open (trap 250: a lone 0x30 byte breaks the kernel's H4
receiver; origin unknown; flow control ruled out), LE (refused by the
controller, but the chip reports BT 4.0 with the LE bit — no known enable
sequence, impossibility NOT demonstrated), FM (forward-port
drivers/media/radio/wl128x, removed in 6.13), GPS (2024 LKML patch, else RE the
reference).

PHYSICAL STATE: region 0 = our Linux image 0xDD66D8C3 (0x939E4C, kernel 7.3.3,
CRC recomputed and matching).  Region 1 = 49e318bf... (43 modules).  Kernel
patch 61 files / 8 762 lines, sha256 20556c5f...  The unit is on LINUX
(0fca:8010, console `pb> `, ssh on 169.254.0.1); rcS loads the wireless stack
but does not raise the link — see HANDOFF §0b for the association sequence.
BOOTCTR_FCTRY = 0x22 (dmesg | grep pbrpmb).  To boot the ORACLE: flash
local/work/ifs-shim-sealed.ifs at 0x825.  To stock TabletOS: that IFS at 0x825 +
local/work/rcfs-1917.rcfs at 0x8D5 (+ the vendor UFS if hd3 was repurposed).
/tmp is wiped by a reboot.  Do NOT restore local/images/ifs0.sealed.bin (trap 84).

BENCH: `tools/pbssh.sh 'cmd'` is the DEFAULT ACCESS; for bulk moves use raw
nc/socat (6.3 MB/s vs ssh 2.3).  The HOST's pb-rndis profile must bind the net
interface of the booted OS — enp8s0f3u1i2 for our composite gadget, enp8s0f3u1
for the oracle (trap 214).  Dropbear has no scp.  REBOOT: if `reboot -f` hangs
(trap 224) use `echo b > /proc/sysrq-trigger`.  NEVER `slay` by name on the
oracle (trap 221).  An agent NEVER writes RPMB; NVRAM read-only.  Read
dmesg/sloginfo UNFILTERED.  Do not clear dmesg under the operator without
saying so.

BT: BT_EN pad 0x4A10007C mux 3 + GPIO2_DATAOUT bit 23; UART2 pads
0x4118/0x411A; /dev/ttyS1.  After a cold boot EXPORT gpio535 first and pulse low
2 s (trap 252).  ALWAYS check `cat /proc/tty/ldiscs` — a stale N_HCI eats the
bytes and the chip looks dead.  To swap a module: rmmod btbcm THEN hci_uart, and
verify by content (trap 251).  WL1283 checks: `i2ctransfer -f -y 0 w1@0x48 0xbc
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
TIME-BOX them.  The TI TRM is local (hardware/swpu231ap.pdf.txt); the TWL6030
datasheet is hardware/twl6030.pdf.

BUILD: modules IN-TREE (`make ... modules`), never `M=`; strip with
`--strip-debug`, never `strip -s`.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed; a
module/firmware-only change keeps the CRC and needs a region-1-only flash (trap
190).  The UFS top-up keeps BOOTCTR_FCTRY at 0x32; the hard hazards (0 / >0x32 /
failed write / flat pack) remain.  Operator's call when to stop and to wrap up.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  Re-export the kernel patch
whenever the DTS or a driver changes (trap 161) and verify by
apply-and-compare, and run the TREE WALK too (trap 211, filtering Kconfig.*).
A reverted experiment still has to be proven reverted.

WRAP-UP: only the operator decides when the session stops.  Record the bench
state, write the session note, update the canonical docs, verify with
tools/docs_audit.py and tools/tests/test_offline.py, then commit privately and
publish the snapshot (docs/project/REPO-MANAGEMENT.md).  No IFS restore is
required.
```
