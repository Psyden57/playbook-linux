# 233 — The WLAN comes up and accepts scans, but the firmware never completes one

Date: 2026-11-14 (session 51, Linux-era 45, after report 232).
Bench: image **`0x9712A086`** (kernel `7.3.3` #60) + region-1 rootfs
**`7e067560…`**; `iw` **6.17** built static against **libnl 3.12.0**.
Capture: `docs/captures/wl1283-scan-2026-11-14.txt`.

## 0. Headline

With the ELP fix (report 232) the interface is stable and a real userspace tool
now reaches the radio — but **the firmware does not complete a scan**:

* `iw dev wlan0 scan` is accepted (cfg80211 → mac80211 → wlcore →
  `wl1271_cmd_scan`), and the wiphy advertises **46 channels** in two bands.
* No scan-complete event ever arrives, so the driver's own
  **`WL1271_SCAN_TIMEOUT` (30 s)** fires and logs
  **`wlcore: Scan completed due to error.`**, which then queues the
  *unintended* recovery (`wl12xx_queue_recovery_work`, `main.c:880`).
* Only ~13 `wl12xx` interrupts are taken during those 30 s, i.e. the firmware is
  not reporting a scan in progress either.
* Passive scan, a single-channel scan (`freq 2412`) and `power_save off` all
  behave identically.

So the `-110` is gone and the driver/SDIO/event plumbing works (firmware boots,
`iw` gets answered, the interface stays up); what fails now is **inside the
firmware's scan**, not the transport.

## 1. What was measured

| test | result |
|---|---|
| `iw dev` | `phy#1`, `wlan0`, `type managed`, MAC `14:74:11:8f:9c:c3` |
| `iw phy` | Band 1 (2.4 GHz, `2412.0 MHz [1] (20.0 dBm)` …) and Band 2 (5 GHz); **46 channels**, HT20, MCS 0–7 |
| `iw dev wlan0 scan` | no output; `Scan completed due to error.` ~30 s later, then recovery |
| `iw dev wlan0 scan passive` | same |
| `iw dev wlan0 scan freq 2412` | same (no completion within 12 s, no error yet) |
| `iw dev wlan0 set power_save off` + scan | same |
| `iw dev wlan0 survey dump` | empty |
| interrupts | `132: … 48055000.gpio 21 Edge wl12xx`, +13 during a scan |
| interface after each | `wlan0` `UP` again after the recovery reboot of the FW |

The interrupt count is the useful number: a scan that ran and reported beacons
would take far more than 13 IRQs.  The firmware is not even finishing the
channel walk, or it is finishing without an event.

## 2. The per-unit NVS was located — and it does not fix this

Report 197 placed the per-unit calibration in the QNX `hd5` data; report 217
recorded that the reference **reads the NVS from NVRAM** (`-o nvram`,
**468 bytes**).  Mainline `wlcore`'s NVS file has the same split: the first
**468 bytes are the NVS section** (`WL1271_INI_NVS_SECTION_SIZE`) and the rest is
the board INI, which is why the reference's 468-byte read is the whole
calibration.

It is in the NVRAM partition, as a record with **length `0x1d4` = 468** at raw
`0x152cc` (`dd` of `/dev/mmcblk1`, a read-only operation):

```
000152c0: 0002 0000 d401 0000 c0b3 22e8 016d 54c3   ........"..mT.
000152d0: 9c8f 1101 7154 7414 ...
```

* The 468 bytes start with the same 12-byte header as `wl128x-nvs.bin`
  (`01 6d 54 c3 9c 8f 11 01 71 54 74 14` — the assigned MAC is inside it), and
  **286 of the 468 bytes differ** from the generic file: that is the per-unit
  calibration.
* Spliced into the generic file (`nvs[0..467]` replaced, INI section kept) and
  installed, the interface behaves identically — **the scan still never
  completes**.  It was then restored, so media still carries the shipped
  generic NVS.
* The NVRAM dump also contains the TI **radio-parameters INI text**
  (`BThWlanCoexistEnable`, `CalibrationChannel24 = 1`,
  `CalibrationChannel5 = 36`, `RxFemInsertionLoss_*`, `IniVersion = 0`), i.e.
  the same file the QNX driver consumes — useful for cross-checking the INI
  section later.

Artefacts kept (gitignored, under `local/`): `local/work/nvram-2026-11-14.bin`
(the 4 MiB partition dump, md5 `e86b98d6…`), `wl128x-nvs-section-468.bin`, and
`wl128x-nvs-perunit.bin` (md5 `547e62d7…`).

## 3. What this rules out

* **The SDIO/IRQ path.**  The firmware boots, answers commands, and the
  interface stays up for minutes; report 232's failure mode is gone.
* **The channel list.**  46 channels are registered from the NVS, with sane
  power limits — the scan command has something to scan.
* **The transport-level block size / byte mode** — unchanged from 232.
* **A missing per-unit NVS as the sole cause of the scan failure** — spliced and
  tested negative (it may still matter for TX power once scanning works).

## 4. What is left, and the next evidence

1. **The firmware's own log — attempted, and it oopses (do not repeat).**
   `wlcore` has the module parameter `fwlog=continuous` (sets
   `conf.fwlog.output = HOST`; the compiled default is `DBG_PINS`) and a
   `fwlog` binary sysfs attribute on
   `…/mmc4:0001:2/wl12xx.1.auto/fwlog`.  Loaded that way the interface comes up
   and the scan fails as before; the attribute reads **0 bytes**, and the next
   recovery tries to read the FW **panic log** — where the driver parses the
   block with the **wl18xx** field layout, the FW's log address translates out
   of range (`WARN io.c:84`, `HW address 0x40159c out of range`), and
   `wl12xx_copy_fwlog()` memcpys from an unmapped address:
   **`Internal error: Oops: 5` at `mmiocpy`** (taint `0x1280`), after which
   `ip link show` blocks until a reboot.  **The one useful datum:** the recovery
   only reads a panic log when the recovery is *unintended*, i.e. **the
   firmware itself panics** on this scan — so the fault is inside the FW, not a
   dropped event.  Fix the parse (wl12xx layout) before using fwlog again;
   trap 241.  A `wl12xx-conf.bin` with a tuned `conf.fwlog` (`mem_blocks`,
   `severity`, `threshold`) is the other route, but the log cannot be trusted
   until the reader is fixed.
2. **The reference's own scan — DONE, and it works (report 234).**  Booting
   stock TabletOS on this unit shows `ti0` **UP,RUNNING**, associated
   (`192.168.0.212`), on firmware **`Rev 7.1.4.10.77`**, with
   `IPC_EVENT_SCAN_COMPLETE` **6 s after boot**.  So the board scans; the fault
   is the newer mainline firmware (`7.3.10.0.142`) paired with `wlcore`.  Next:
   bypass wlcore's firmware floor and try `wl128x-fw-4-sr.bin` (`7.3.5.0.98`);
   if it panics too, compare what `wlcore` sends with the vendor driver's WEXT
   scan (`0x810A22`).
3. **A board RF enable** the reference's *boot* asserts and its driver does not —
   the same class that hid the 32 kHz clock (report 230).  The scan is the first
   operation that needs the RF section, not just the SDIO core.
4. **The INI section**, if the FW validates it: the NVRAM text INI gives the
   reference's values for the fields `wlcore` takes from `wl128x-nvs.bin`.

## 5. The client tooling is now in the image

The scan blocker is in the firmware, but the userspace side is now complete and
shipped, so the next session starts from a usable bench:

| what | where | note |
|---|---|---|
| `iw` **6.17** | `/usr/bin/iw` | static, built against **libnl 3.12.0** |
| `wpa_supplicant` **2.12** | `/usr/sbin/wpa_supplicant` | static, `CONFIG_DRIVER_NL80211`, `CONFIG_TLS=internal` (no OpenSSL), `CONFIG_LIBNL32` |
| `wpa_cli`, `wpa_passphrase` | `/usr/sbin/` | same build |
| `regulatory.db` + `.p7s` | `/lib/firmware/` | wireless-regdb **2026.09.03**, the **signed** pair (the kernel requires a signature: `CONFIG_CFG80211_REQUIRE_SIGNED_REGDB=y`, `USE_KERNEL_REGDB_KEYS=y`) |

Measured after a real region-1 flash and reboot:

* cfg80211 verifies the database — `Loading compiled-in X.509 certificates for
  regulatory database` then `Loaded X.509 cert 'sforshee: 00b28ddf47aef9cea7'`
  and `'wens: 61c03865…'` — and `iw reg get` shows the file's **world (00)**
  domain, with **2.4 GHz 2402–2472 active** (no `PASSIVE-SCAN`), so regulatory
  never blocked the scan.
* `wpa_supplicant -B -i wlan0 -D nl80211 -C /var/run/wpa_supplicant` starts
  (`Successfully initialized`), `wpa_cli scan` returns `OK`, and the state goes
  `SCANNING` — **and stays there with no results**, i.e. the same firmware
  fault.  (`rfkill: Cannot open RFKILL control device` is expected: this kernel
  has no rfkill node for the chip.)
* The tools are in the image via a new `build_rootfs.py --extras <dir>` option
  that copies a prebuilt tree into the image root with its layout and modes
  (`local/work/rootfs-extras`); the firmware tree adds the two regdb files.
  Image **`a148c362…`** (`local/images/rootfs-region1-wl1283-net.ext4`), region
  0 unchanged (`0x9712A086`) — module/tool-only change, region-1 flash only
  (trap 190).

## 6. Artefacts

* `docs/captures/wl1283-scan-2026-11-14.txt` — the scan attempt, the IRQ count
  and the 30 s timeout message.
* `local/work/bin/iw` — the static ARM `iw` (md5 `6bb5a3a5…`).  Recipe:
  libnl 3.12.0 configured
  `--host=arm-linux --disable-shared --enable-static --disable-cli` into
  `local/work/arm-prefix`, then `make CC=arm-linux-gcc LDFLAGS=-static` in
  `iw-6.17` with `PKG_CONFIG_PATH=…/arm-prefix/lib/pkgconfig`, stripped with
  `strip -s`.  Dropbear has no `scp`; push it with
  `ssh … 'cat > /tmp/iw' < local/work/bin/iw`.
* `local/work/rootfs-extras/` — the prebuilt tree the image ships
  (`usr/bin/iw`, `usr/sbin/wpa_{supplicant,cli,passphrase}`); sources and the
  `wpa_supplicant/.config` are under `local/work/src/`.
* `local/work/firmware/{regulatory.db,regulatory.db.p7s}` — wireless-regdb
  2026.09.03, signed.
* `local/images/rootfs-region1-wl1283-net.ext4` — sha256 `a148c362…`.
* `local/work/bin/{wpa_supplicant,wpa_cli}` — stripped copies for a quick push.
* `local/work/{nvram-2026-11-14.bin, wl128x-nvs-section-468.bin,
  wl128x-nvs-perunit.bin}`.
* Traps 240/241; reports 197, 217, 232.
