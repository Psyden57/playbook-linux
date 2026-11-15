# 234 — The reference firmware scans on this unit; the mainline one panics

Date: 2026-11-14/15 (session 51, Linux-era 45, after reports 232/233).

## 0. Headline

Booted the stock PlayBook OS on this unit and watched its own WLAN stack:

* `ti0` comes up **UP,RUNNING** with the assigned MAC `14:74:11:8f:9c:c3` and a
  DHCP address (`192.168.0.212`) — it associates.
* Its firmware is the vendor **`Rev 7.1.4.10.77`**, and its driver
  **completes scans**: `TIWLDRV … IPC_EVENT_SCAN_COMPLETE, client=1` **6 s after
  boot**, and again for `client=2` later; a `SIOCIWFIRSTPRIV:cmd=0x810A22` comes
  every 10 s (its wifi service's scan trigger).

So the radio, the antenna, the board power and the SDIO path can scan on this
unit.  The failure in reports 232/233 is therefore **the newer mainline
firmware (`wl128x-fw-5-sr.bin`, `7.3.10.0.142`) paired with `wlcore`**, not the
board, not regulatory (report 233 §5) and not the client tooling.

## 1. How it was measured

* Region 0 was flashed with `local/work/ifs-shim-sealed.ifs` at `0x825` (the
  documented way back to TabletOS; region 1 and the full 1917 RCFS untouched),
  the unit rebooted, and it enumerated as `0fca:8012`.
* **Host link:** the `pb-rndis` NetworkManager profile has to name the *plain*
  RNDIS interface, `enp8s0f3u1` (trap 214) — with the profile still pinned to
  the composite name `enp8s0f3u1i2` the unit answers nothing.  After rebinding:
  host `169.254.0.2/16`, unit `169.254.0.1` pings.
* **Access:** the door's `sshd` was **not** running on this boot (port 22
  refused, `qconn` 9090 open), so the session used **`tools/QCL.py`** — `info`
  identifies the target (`MACHINE=OMAP4430_ES2.2_HS_Winchester_Rev:07`,
  `RELEASE=6.6.0`, `VERSION=2014/03/19`) and `exec /bin/<cmd> <args…>` runs
  single commands.  (`exec /bin/sh -c '…'` does **not** survive QCL's argument
  handling — run one command per `exec`.)
* **State read:** `exec /sbin/ifconfig ti0`
  → `flags=8c43<UP,BROADCAST,RUNNING,OACTIVE,SIMPLEX,MULTICAST>`, `mtu 1500`,
  `address: 14:74:11:8f:9c:c3`, `inet 192.168.0.212 netmask 0xffffff00`.
* **Slog** (`exec /bin/sh -c 'sloginfo'` — this one works because the whole
  pipeline is a single argument to `sh`):
  `Firmware running.` / `Firmware Version: Rev 7.1.4.10.77` (twice, at boot),
  `IPC_EVENT_SCAN_COMPLETE, client=1` at `01:56:20` (boot was `01:56:14`),
  `IPC_EVENT_SCAN_COMPLETE, client=2` at `01:57:49`, and
  `SIOCIWFIRSTPRIV:cmd=0x810A22` every 10 s with `0x800301`/`0x820401` around
  it.

## 2. What it means

| | reference (QNX) | ours (Linux) |
|---|---|---|
| firmware | `wl128x` **7.1.4.10.77** (vendor, loaded by `devnp-ti1283.so`) | `wl128x-fw-5-sr.bin` **7.3.10.0.142** (linux-firmware, newest available) |
| driver | TI's io-pkt driver, WEXT private commands | mainline `wlcore`/`wl12xx` |
| scan | **completes**, results delivered | **firmware panics** — no scan-complete, then `WL1271_SCAN_TIMEOUT` and recovery (report 233) |
| interface | UP, RUNNING, associated | UP, stable once CAM (report 232) |

The two remaining classes are therefore:

1. **The firmware build itself** — the newer service pack may need board
   parameters (a `wl12xx-conf.bin`, a different `wl128x-nvs` section) that ours
   does not supply, or may simply be broken for this chip; and
2. **What `wlcore` sends it** — the scan command's channel/dwell parameters
   versus what the vendor driver sends.

Both are testable without new hardware.

## 3. Next experiments (cheap, reversible, one variable each)

1. **Bypass `wlcore`'s firmware floor and try the older service pack.**
   `wl12xx`'s firmware-version check rejects anything below
   `7.3.10.*.133`; relaxing that one comparison lets `wl128x-fw-4-sr.bin`
   (`7.3.5.0.98`) boot.  If it scans, the newer FW is the problem and the fix is
   to find what it needs; if it panics too, the fault is in what `wlcore` sends.
   (Module-only change → region-1 flash, trap 190; revert and prove afterwards.)
2. **Compare the scan command.**  The vendor driver drives scanning through
   WEXT (`0x810A22` …), whose parameters are visible in its binary; reconcile
   them with `wl12xx_cmd_trigger_scan`'s channel list/dwell times.
3. **A `wl12xx-conf.bin`** (`wl->conf` from file instead of the compiled
   defaults) is the supported way to change scan/ps/fwlog parameters without a
   rebuild — worth having anyway.

## 4. Bench state after this

Region 0 was flashed back to `boot/chainloader/kernel.img`
(`0x9712A086`) — region 0 only, since only it had changed — and the unit
rebooted to our image (`0fca:8010`, ssh on `169.254.0.1`); region 1 is
`a148c362…` (report 233 §5) and the full 1917 RCFS is untouched at `0xC00000`.
The host profile was rebound to `enp8s0f3u1i2` afterwards, and the WLAN checked
again: `wlan0` up, one firmware boot, zero failures, `CLK32KG = 07 01 55`.

## 5. Artefacts

* No new capture file: the evidence is the QNX `sloginfo` lines quoted above
  (they are in the device's log buffer, which does not survive a reboot).
* Trap 242 (access to a stock TabletOS boot: sshd may be down, use QCL).
* Reports 232, 233, 197, 217.
