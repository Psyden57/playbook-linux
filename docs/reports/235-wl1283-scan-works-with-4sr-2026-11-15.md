# 235 — The scan works with the older service pack: 5-sr panics, 4-sr scans

Date: 2026-11-15 (session 51, Linux-era 45, after reports 232–234).
Bench: image **`0x9712A086`** + region-1 rootfs **`f159a221…`**.

## 0. Headline

**The WL1283 WLAN now scans.**  The never-completing scan of report 233 was
**the 5-sr firmware build**: `wl128x-fw-5-sr.bin` (`Rev 7.3.10.0.142`) boots and
keeps the interface up, but **panics on the first scan**.  The older
`wl128x-fw-4-sr.bin` (**`Rev 7.3.5.0.98`**) scans normally on the same driver,
same NVS, same board:

```
wlcore: firmware booted (Rev 7.3.5.0.98)
# iw dev wlan0 scan
BSS 44:05:3f:9d:ca:16  SSID: Peperina
BSS 50:91:e3:88:42:60  SSID: MovistarFibra
BSS cc:ed:dc:ee:b0:cf  SSID: MovistarFibra-5GHz-EEB0C8
...
count = 17        (repeats: 17, 20, 21 BSS; 2.4 and 5 GHz; zero errors)
```

The port therefore ships **4-sr as the single-role firmware** and a lowered
firmware floor.

## 1. How it was tested

1. `wl12xx`'s floor was lowered for the experiment (`WL128X_MAJOR_SR_VER` 10 →
   5, `WL128X_MINOR_SR_VER` 133 → 0 in `wl12xx.h`) and `wl12xx.ko` rebuilt
   in-tree; the 4-sr file was copied over the `ti-connectivity/wl128x-fw-5-sr.bin`
   name the driver requested.
2. Load → `wlcore: firmware booted (Rev 7.3.5.0.98)`, `ip link set wlan0 up`
   → `rc=0`.
3. `iw dev wlan0 scan` returned **17 / 20 / 21 BSS** on three consecutive runs,
   both bands, **no `sdio read failed`, no `Scan completed due to error`, no
   recovery** — the opposite of 5-sr, which never completed a single scan.
4. Made it the default: floor **7.3.5.0** plus
   `WL128X_FW_NAME_SINGLE = "ti-connectivity/wl128x-fw-4-sr.bin"`; region 1
   rebuilt (`f159a221…`) and **flashed**; after the reboot, from media:
   firmware `Rev 7.3.5.0.98`, scan **17 BSS**, failures **0**.

## 2. What this settles

| firmware | boots | interface | scan |
|---|---|---|---|
| vendor `7.1.4.10.77` (QNX, report 234) | yes | UP, associated | **completes** |
| mainline **`4-sr` 7.3.5.0.98** | yes | UP, stable | **completes, 17–21 BSS** |
| mainline `5-sr` 7.3.10.0.142 | yes | UP, stable (CAM, report 232) | **FW panics** — no event, 30 s timeout, recovery (report 233/241) |

So the ordering of suspects from report 233 is now resolved the other way round:
it was never the transport, the tooling, regulatory or the board — **the newest
wl128x service pack is broken for scanning on this chip**, and the vendor's own
sub-7.2 firmware scans.  4-sr is the newest mainline build below the break.

## 3. The change, and what it costs

* `drivers/net/wireless/ti/wl12xx/wl12xx.h` — `WL128X_MAJOR_SR_VER 5`,
  `WL128X_MINOR_SR_VER 0`, with the reason in a comment.
* `drivers/net/wireless/ti/wl12xx/main.c` — `WL128X_FW_NAME_SINGLE` is
  `ti-connectivity/wl128x-fw-4-sr.bin`.
* Both firmware files stay installed (`4-sr` and `5-sr`), so going back to 5-sr
  is one `#define` — useful if a later check wants to re-test it.
* **Cost:** 4-sr is an older service pack.  It is what the vendor's own firmware
  generation does here; if BaT/coex work ever needs 5-sr, the 5-sr panic is the
  thing to fix, and reports 233/241 are the reproduction.
* The floor override is deliberate and local: it says "this board runs 7.3.5",
  not "any firmware is fine" — 3.x and 2.x are still rejected.

## 4. Next

1. **Association.**  The client side is shipped (report 233 §5): station config
   with `wpa_passphrase`/`wpa_supplicant`, then `wpa_cli status`.  A scan now
   returns real BSSIDs, so a real network can be joined.
2. **The per-unit NVS** (`local/work/wl128x-nvs-perunit.bin`, report 233) — it
   matters for TX power now that the radio transmits.
3. **BT** — untouched (`BT_EN` = gpio 55, UART2, `TIInit_7.2.31.bts`).

## 5. Artefacts

* Patch **59 files / 8 713 lines**, sha256 `2be9e99c…` — 0 rejects, 59/59
  byte-identical from pristine (the new file is `wl12xx.h`).
* Region-1 image `local/images/rootfs-region1-wl1283-scan.ext4`, sha256
  **`f159a221…`** (34 modules, `wl12xx.ko` md5 `2fb86c4d…`, both SR firmware
  files, iw/wpa/regdb from report 233 §5).
* Traps 239–243; reports 232–234.
