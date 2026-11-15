# Session 51 (absolute) — the WL1283 is stable: the `-110` was the chip's ELP sleep

> Numbering: absolute `51` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 45** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-14.
Unit state at the end: **our Linux image `0x9712A086`** on region 0 (unchanged
this session), region 1 = the CAM rootfs **`7e067560…`**
(`local/images/rootfs-region1-wl1283-elpcam.ext4`, 34 modules, the `wl128x`
firmware, dropbear).  `0fca:8010`, console `pb> `, **ssh on `169.254.0.1`**.
`wlan0` **up and stable** after `ip link set wlan0 up`; firmware
`Rev 7.3.10.0.142`; MAC `14:74:11:8f:9c:c3`.  No NVRAM/MCT/RPMB write; no
restore needed.

## Goal of this session

Run session 50's one experiment: find the `-110` in the WL_IRQ status read —
reload the stack with `wlcore_sdio dump=1`, bring `wlan0` up, read the
unfiltered dmesg and identify the last READ before the failure; try
`wl128x-fw-4-sr.bin`; compare the failing address/length with the block size.

## Confirmed (measured, read in a file, or seen on the bench)

* **The WLAN works on this unit: it scans.**  With `wl128x-fw-4-sr.bin`
  (`Rev 7.3.5.0.98`) `iw dev wlan0 scan` returned **17 / 20 / 21 BSS** on three
  consecutive runs, both bands (e.g. `MovistarFibra` on 2412 MHz at −85 dBm,
  `MovistarFibra-5GHz-EEB0C8`), with zero `sdio read failed` / `wlcore: down` /
  `Scan completed` lines — and the same from flashed media after a reboot
  (`Rev 7.3.5.0.98`, 17 BSS, failures 0).  [§"The scan works" below; report 235;
  image `f159a221…`]
* **`wl128x-fw-5-sr.bin` (`7.3.10.0.142`) panics on the first scan** on this
  board: no scan-complete event, the driver's 30 s timeout, an unintended
  recovery — and, with the host fwlog on, the recovery actually read a **panic
  log** (which then oopsed, trap 241).  [reports 233/235; capture
  `wl1283-scan-2026-11-14.txt`]
* **The reference firmware scans on this unit**: stock TabletOS, `ti0`
  UP,RUNNING, MAC `14:74:11:8f:9c:c3`, inet `192.168.0.212`, firmware
  `Rev 7.1.4.10.77`, `IPC_EVENT_SCAN_COMPLETE` 6 s after boot and a WEXT scan
  command (`0x810A22`) every 10 s.  [report 234; `sloginfo` read over `qconn`]
* **The client tooling runs from the image**: `iw` 6.17 and
  `wpa_supplicant`/`wpa_cli`/`wpa_passphrase` 2.12 execute,
  `wpa_supplicant` initializes and `wpa_cli scan` returns `OK`; the signed
  `regulatory.db` is accepted (`Loaded X.509 cert 'sforshee…'`, `'wens…'`) and
  `iw reg get` shows the world domain with 2.4 GHz 2402–2472 **active**.
  [report 233 §5; image payload verified with `debugfs`]
* **The per-unit NVS is in NVRAM** as a record of length `0x1d4` (468) at raw
  `0x152cc`, differing from the generic file in **286 of 468** bytes — that is
  the calibration, and the 12-byte header (with the MAC) is identical.
  [report 233 §2; `local/work/nvram-2026-11-14.bin`]
* **The `-110` reproduces on every `ip link up`,** as a `sdio read failed
  (-110)` followed by the `wl12xx_queue_recovery_work` WARN
  (`main.c:880`), `Enable for recovery failed`, `wlcore: down`,
  `Runtime PM usage count underflow`, `Hardware restart was requested`, and
  then `ieee80211_reconfig` failing `drv_add_interface` (`util.c:2012`) — a loop.
  On a boot where the interface comes up (`rc=0`) it still goes down this way.
* **`wlcore_sdio dump=1` hides the failure.**  With the dump on, the same
  sequence ran without a single `-110`: the per-transfer hex dump (and its
  printk cost on this board, trap 52) changes the timing.  The prescribed recipe
  cannot work.  [trap 239]
* **The failing transfer, from an instrumented error path.**  A temporary
  `dev_err` in `wl12xx_sdio_raw_read()` that prints only on failure gave:
  `PBRD addr 0x1efc0 len 84 fixed 0 blksz 256: sdio read failed (-110)` —
  i.e. the **84-byte FW-status block at `0x1efc0`**, in **CMD53 byte mode**,
  with a **256-byte block size**, from `irq/132-wl12xx`, issued right after
  `firmware booted` and timing out ~1.5–1.6 s later with **no `mmc_ios` change**
  in between (no `PBSIO` lines).  The same read **succeeds** later in the same
  boot, so the transfer is legal.  The instrument was reverted —
  `wlcore/sdio.c` is `cmp`-identical to pristine.
* **The discriminator is ELP, not runtime PM.**  (A) `power/control = on` on
  `wl12xx.1.auto` (no runtime PM): `wlan0` up and stable, 1 firmware boot,
  0 failures.  (B) `power/control = auto` + `sleep_auth = 0` (CAM): also stable,
  and the platform device still sits `suspended`.  (C) On that working system,
  `echo 2 > …/wlcore/sleep_auth` (ELP) reproduces the `-110` and the loop within
  seconds.  Capture: `docs/captures/wl1283-elp-2026-11-14.txt`.
* **The fix works end to end.**  `wl12xx_default_conf.conn.sta_sleep_auth =
  WL1271_PSM_CAM` (was `WL1271_PSM_ILLEGAL` → ELP).  After a **real region-1
  flash and reboot**: `ip link set wlan0 up` → `rc=0`,
  `wlcore: firmware booted (Rev 7.3.10.0.142)`, one boot, **zero** `sdio read
  failed`/`wlcore: down` lines, `wlan0` `<NO-CARRIER,…,UP>`, debugfs
  `sleep_auth` reads **0**.  Repeated `down`/`up` cycles each re-boot the
  firmware cleanly (the pwrseq still does its job).
* **`wl128x-fw-4-sr.bin` cannot be used.**  wlcore's floor is `7.3.10.*.133` and
  4-sr reports `7.3.5.0.98` → *"Your WiFi FW version (7.3.5.0.98) is invalid"*
  and `firmware boot failed despite 3 retries`, before any interrupt work.  It
  is also **older** than the 5-sr we run (7.3.10.0.142), which is the newest
  wl128x blob in linux-firmware.
* **`CLK32KG` is `07 01 55`** — the reference's own value — so the slow clock is
  not the ELP problem.
* **Building modules with `M=` is wrong for this tree.**  It marks them `(O)`
  (out-of-tree), which taints the kernel and pollutes every later dmesg, and it
  leaves a partial `Module.symvers` that breaks the next in-tree build.  A full
  in-tree `make … modules` reproduces every module **byte-identically** to the
  shipped set (`cfg80211`, `mac80211`, `wlcore`, `wlcore_sdio` all match); only
  `wl12xx.ko` changed.  Strip with `--strip-debug`: a `strip -s` module is
  rejected by the kernel ("module has no symbols (stripped?)").
* **The patch applies cleanly.**  Re-exported: **58 files / 8 676 lines**,
  sha256 `1c57e575…`, 0 rejects, **58/58** byte-identical against a staged
  pristine tree.  The tree walk (with the `Kconfig.*` filter) shows
  `drivers/net/wireless/ti/wl12xx/main.c` as the only new file.

## Inferred (reasoning from evidence, not directly measured)

* **The mechanism is the ELP wake.**  `wlcore_runtime_suspend()` writes
  `ELPCTRL_SLEEP`; `wlcore_runtime_resume()` writes `ELPCTRL_WAKE_UP` and, because
  it is running in the IRQ thread (`WL1271_FLAG_IRQ_RUNNING`), **skips** the wake
  completion wait; the caller then reads the FW status immediately.  On this
  chip/firmware that first read is not answered.  The A/B/C results pin the
  *state* (ELP), not the exact micro-timing.
* **CAM is a workaround, not a fix of the wake path.**  It costs power.  Only
  relevant if the WLAN must run on battery.
* **The WLAN is now functional but not yet useful**: `wlan0` is `UP`, but with
  no supplicant and no `regulatory.db` nothing scans or associates.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"Read the last READ with `wlcore_sdio dump=1`."**  It hides the failure.
  Instrument the error path instead (prints nothing on success).  (Trap 239.)
* **"`wl128x-fw-4-sr.bin` is the previous/newer firmware to compare."**  It is
  older *and* below the driver's floor; it cannot boot at all.
* **"The `-110` is the block size / byte mode."**  The 84-byte byte-mode read is
  legal and succeeds whenever the chip is awake.
* **"`build_rootfs.py --firmware` ships only `ti-connectivity/`."**  It installs
  the whole `local/work/firmware` tree, so a loose file at its root also lands
  in `/lib/firmware` — the 4-sr spare is shipped that way (harmless, but check
  the printed count).
* **"A module reload is enough to prove the fix."**  It is not: the first
  version of the module was an `M=` build and carried the `(O)` taint.  Prove it
  on media, after a flash and a reboot.

## Unknown / open questions

* **Why the ELP wake is not honoured.**  Wake latency versus the immediate
  read, or the chip asserting `WL_IRQ` around the sleep transition.  Needs a
  non-printk instrument (the DRAM ring) — deferred.
* **Does the radio actually work?**  Not yet exercised: no scan, no associate,
  no TX/RX.  That is the next milestone (supplicant + `regulatory.db`).
* **The per-unit calibration** is still the generic NVS (the MAC is the assigned
  one); `hd5` has it (report 197).
* **BT** is untouched this session; see the handoff for the firmware question
  (`TIInit_7.2.31.bts` vs the reference's `WL1283.bts`) and the missing
  `btwilink`/`ti-st` in mainline 7.3.3.

## Superseded / uncertain

* **Report 231 §6's "remaining suspects"** — resolved (report 232): the
  firmware match is unusable rather than negative, the block size is excluded,
  and the ELP/status path is the fault.
* **The session-50 advice** to identify the read with `dump=1` — do not use it
  (trap 239).
* **"`wl128x-fw-4-sr.bin` is unusable / cannot be a control"** (report 233 §4,
  trap 239, DECISIONS §68 as first written) — **superseded by report 235**: with
  the version floor lowered, 4-sr (`7.3.5.0.98`) is the firmware that **works**
  and the one this port ships; 5-sr (`7.3.10.0.142`) is the one that panics.
  The evidence then was the driver's rejection ("firmware boot failed despite 3
  retries"), which only ever showed the *floor*, not that the firmware was bad.
* **"The newest firmware is the right one"** (the session-50/51 assumption
  behind trying 5-sr first) — superseded by report 234/235: the newest wl128x
  blob panics on scan on this chip, and nothing newer than 5-sr exists.
* **"A scan that never completes is a transport/protocol fault"** — superseded:
  the transport was fine; the firmware crashed (reports 233/241/235).

## Continuation — `iw`, and the scan the firmware never completes (report 233)

With the interface stable the next step was to exercise the radio:

* **`iw` 6.17 built static** (libnl 3.12.0, `--host=arm-linux --disable-shared
  --enable-static`; iw with `LDFLAGS=-static` and `PKG_CONFIG_PATH` pointing at
  the arm prefix; stripped).  It runs on the unit (`local/work/bin/iw`, pushed
  over ssh — dropbear has no `scp`), sees `wlan0` and **46 channels in two
  bands** with sane power limits.
* **The scan never completes.**  `iw dev wlan0 scan` is accepted, no
  scan-complete event arrives, and the driver's **`WL1271_SCAN_TIMEOUT`
  (30 s)** fires: `wlcore: Scan completed due to error.` then the unintended
  recovery.  Only **~13 `wl12xx` IRQs** are taken in the window, i.e. the
  firmware never reports the scan.  Passive scan, a single-channel scan
  (`freq 2412`) and `power_save off` all behave the same.  [trap 240; capture
  `docs/captures/wl1283-scan-2026-11-14.txt`]
* **The per-unit NVS was found — and is not the cause.**  It is the **468-byte
  NVRAM record** (`WL1271_INI_NVS_SECTION_SIZE`, record length `0x1d4`) at raw
  `0x152cc` in the NVRAM partition; it differs from the generic file in **286 of
  468 bytes** (the header, including the MAC, is identical).  Spliced into
  `wl128x-nvs.bin` and installed, the scan behaves identically, so media was
  **restored to the shipped generic NVS** and the candidate kept in
  `local/work/wl128x-nvs-perunit.bin`.  The NVRAM dump also contains the TI
  radio-parameters **INI text** the QNX driver consumes.
* **What it means:** the transport is fine (firmware boots, commands answered,
  interface stays up), so the remaining fault is inside the firmware's scan —
  either an RF-side state the reference sets at boot, a scan parameter, or the
  FW itself.
* **The firmware log was tried — and it oopses.**  `wlcore` takes
  `fwlog=continuous` (vs the compiled `DBG_PINS`) and exposes the log as the
  `fwlog` binary sysfs attribute
  (`…/mmc4:0001:2/wl12xx.1.auto/fwlog`); the buffer read 0 bytes, and the next
  recovery tried the **panic log**, where the reader parses the block with the
  **wl18xx** layout: `HW address 0x40159c out of range` (`io.c:84`), a bogus
  length, and `wl12xx_copy_fwlog()` reading unmapped vmalloc →
  **`Internal error: Oops: 5` at `mmiocpy`** (taint `0x1280`).  Afterwards
  `ip link show` blocked (ssh still worked) and the unit was rebooted with
  `sysrq b`; it came back clean, `wlan0` up, zero failures.  The useful datum:
  the recovery only reads a panic log for an **unintended** recovery, so **the
  firmware panics** on this scan.  [trap 241]

## The client tooling, and it is now in the image (report 233 §5)

The userspace side is finished and shipped, so the next session starts from a
usable bench even though the firmware still refuses to scan:

* **`wpa_supplicant` 2.12** built static for ARM: `CONFIG_DRIVER_NL80211`,
  `CONFIG_LIBNL32` (against the same libnl 3.12.0 as `iw`),
  `CONFIG_TLS=internal` (no OpenSSL), `CONFIG_IEEE80211N`, `CONFIG_ELOOP_POLL`;
  plus `wpa_cli` and `wpa_passphrase`.
* **`regulatory.db`** from wireless-regdb **2026.09.03**, the **signed** pair
  (`regulatory.db` + `.p7s`) because the kernel requires a signature
  (`CONFIG_CFG80211_REQUIRE_SIGNED_REGDB=y`).  At boot cfg80211 verifies it
  (`Loaded X.509 cert 'sforshee…'`, `'wens…'`) and `iw reg get` shows the
  world domain with **2.4 GHz 2402–2472 active** — so regulatory never blocked
  the scan.
* **Shipped in region 1** via a new `build_rootfs.py --extras <dir>` option
  (layout- and mode-preserving copy of a prebuilt tree): `/usr/bin/iw`,
  `/usr/sbin/wpa_{supplicant,cli,passphrase}`, and the regdb pair through
  `--firmware`.  Image **`a148c362…`**
  (`local/images/rootfs-region1-wl1283-net.ext4`), flashed region-1-only;
  region 0 unchanged.  Verified from media after a reboot: `iw dev` sees
  `phy#0 wlan0`, `wpa_supplicant` starts, `wpa_cli scan` returns `OK`, and the
  state goes `SCANNING` with no results — the same firmware fault, reproduced
  with a second, independent client.

## The reference's own scan — the board is exonerated (report 234)

Booted stock TabletOS on the unit (region-0 flash of
`local/work/ifs-shim-sealed.ifs` at `0x825`, the documented way back) and
watched its WLAN stack through `qconn`:

* `ti0` is **UP,BROADCAST,RUNNING,OACTIVE,SIMPLEX,MULTICAST**, MAC
  `14:74:11:8f:9c:c3`, **inet `192.168.0.212`** — it associates.
* `sloginfo`: `Firmware running.` / **`Firmware Version: Rev 7.1.4.10.77`**,
  then **`IPC_EVENT_SCAN_COMPLETE, client=1` six seconds after boot** and
  `client=2` later; a `SIOCIWFIRSTPRIV:cmd=0x810A22` every 10 s (its service's
  scan trigger).
* So the radio, antenna, board power and SDIO path all scan fine on this unit,
  and the failure is the **newer mainline firmware (`7.3.10.0.142`) paired with
  `wlcore`** — not the board, not regulatory, not the tooling.
* Access notes: the `pb-rndis` profile has to name the plain **`enp8s0f3u1`**
  for the oracle (trap 214); the door's `sshd` was **not** running on that boot
  (22 refused, 9090 open), so the session used **`tools/QCL.py`** (`info`,
  `exec /bin/<cmd>`), kill/restart the stale `door-hold` by pid (trap 17), and
  note that `exec /bin/sh -c '…'` does not survive QCL's argument handling
  (trap 242).
* Region 0 was then flashed back to `boot/chainloader/kernel.img`
  (`0x9712A086`, region 0 only), the unit rebooted to our image, the host
  profile rebound to `enp8s0f3u1i2`, and the WLAN re-checked: `wlan0` up, one
  firmware boot, zero failures, `CLK32KG = 07 01 55`.
* **Next:** bypass wlcore's firmware floor (`7.3.10.*.133`) and try
  `wl128x-fw-4-sr.bin` (`7.3.5.0.98`); if it panics too, reconcile `wlcore`'s
  scan command with the vendor driver's.

## The scan works — it was the 5-sr firmware (report 235)

The session's last experiment was the one report 234 pointed at: **try the older
service pack**.  `wl12xx`'s SR floor (`7.3.10.*.133`) was lowered to 7.3.5.0
(`WL128X_MAJOR_SR_VER 5`, `WL128X_MINOR_SR_VER 0`) and the 4-sr file was copied
over the 5-sr name the driver requested:

* `wlcore: firmware booted (Rev 7.3.5.0.98)`, `ip link set wlan0 up` `rc=0`.
* **`iw dev wlan0 scan` returned 17, 20 and 21 BSS** on three consecutive runs,
  **both bands**, with **no** `sdio read failed`, no `Scan completed due to
  error`, no recovery — i.e. the exact opposite of 5-sr.
* Made it the default (`WL128X_FW_NAME_SINGLE` → the 4-sr file, both files
  installed), region 1 rebuilt (`f159a221…`) and **flashed**; after the reboot,
  from media: `Rev 7.3.5.0.98`, scan **17 BSS**, failures **0**.

So `wl128x-fw-5-sr.bin` (`Rev 7.3.10.0.142`) — the newest wl128x blob in
linux-firmware *and* in TI's own firmware tree, nothing newer exists — boots but
**panics on the first scan** on this chip, while 4-sr (`7.3.5.0.98`) scans.  The
port ships 4-sr with the floor lowered; both files stay installed so going back
is one `#define`.  Patch **59 files / 8 713 lines**, sha256 `2be9e99c…`
(adds `wl12xx.h`), 0 rejects / 59/59.  [trap 243; DECISIONS §69]

**Where the WLAN stands:** enumerates, firmware `Rev 7.3.5.0.98`, `wlan0`
stable, **scans both bands**; the client side (`iw`, `wpa_supplicant`/`wpa_cli`/
`wpa_passphrase`, signed `regulatory.db`) is in the image, so **association is
the next step** — and the point where real throughput gets measured.

## Bench state inherited by session 52

| item | value |
|---|---|
| region 0 | **`0x9712A086`**, `0x939A74` B, kernel `7.3.3` **#60** (built session 50; **unchanged** in session 51) |
| region 1 | **`f159a221c9dd7133206c0998d581efa1096b495dc91c4c6eb6859a4faa3ec39a`**, 67,108,864 B, 34 modules + **both** `wl128x` SR firmware files + the CAM/4-sr `wl12xx.ko` + `iw` 6.17 + `wpa_supplicant`/`wpa_cli`/`wpa_passphrase` 2.12 + the signed `regulatory.db` (`local/images/rootfs-region1-wl1283-scan.ext4`); flashed region-1-only at `0x2915` |
| kernel patch | **59 files / 8 713 lines**, sha256 `2be9e99c…` (adds `wl12xx/main.c` and `wl12xx.h`) |
| OS running | **our Linux image** (`0fca:8010`); console `pb> `, ssh on `169.254.0.1`; `wlan0` present but **down after a boot** — `ip link set wlan0 up` raises it, it **stays up** on firmware `Rev 7.3.5.0.98`, and **`iw dev wlan0 scan` returns 17–21 BSS** (report 235) |
| device-side tools | **in the image**: `/usr/bin/iw` (6.17), `/usr/sbin/wpa_{supplicant,cli,passphrase}` (2.12), `/lib/firmware/regulatory.db`+`.p7s`.  Rebuild them with `local/work/rootfs-extras` + `build_rootfs.py --extras` |
| `BOOTCTR_FCTRY` | not read directly (`/dev/rpmb` is TabletOS-only); `/init` tops up at boot.  Health check only (`SAFETY.md` §2) |
| device-side files | none added on the UFS; `/tmp` tools are not persistent |
| region-0 RCFS | the full 1917 RCFS (trap 227); the pruned oracle pair is `local/work/minimal/setB.signed` |
| way back to TabletOS | `ifs-shim-sealed.ifs` at `0x825` **+** `rcfs-1917.rcfs` at `0x8D5` (NOT `wayback.signed`, trap 226) |
| flashes this session | region 1 only, twice (the first with an `M=`-built `wl12xx.ko`, corrected by the second); region 0 untouched; no IFS restore needed |

## Artefacts produced

* **Report** `docs/reports/232-wl1283-elp-status-read-2026-11-14.md` and
  **`233-wl1283-scan-not-completed-2026-11-14.md`**.
* **Captures** `docs/captures/wl1283-elp-2026-11-14.txt` and
  `wl1283-scan-2026-11-14.txt`.
* **Tools** `local/work/bin/iw` (static ARM, 6.17 + libnl 3.12.0);
  `local/work/arm-prefix/` (the static libnl install).
* **Device data** `local/work/nvram-2026-11-14.bin` (the 4 MiB NVRAM partition,
  read-only), `wl128x-nvs-section-468.bin`, `wl128x-nvs-perunit.bin`.
* **Kernel** `drivers/net/wireless/ti/wl12xx/main.c` (the CAM default); patch
  re-exported and verified.
* **Userspace** `local/work/modules-7.3.3/wl12xx.ko` (in-tree,
  `--strip-debug`, md5 `1ff6daa7…`); `local/images/rootfs-region1-wl1283-elpcam.ext4`.
* **Docs** trap 239, DECISIONS §68, report 231/INDEX corrections, `STATE.md`,
  `HANDOFF.md`, `ROADMAP.md` item 9, `captures/README.md`,
  `kernel/patches/README.md`, `NUMBERING.md`, this note; the session-50 and
  session-49 state snapshots moved to `docs/history/STATE-HISTORY.md`.
