# Session 52 (absolute) — the WLAN associates, the USB link gets its DMA, and the BT core answers

> Numbering: absolute `52` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 46** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-15.
Unit state at the end: **our Linux image `0xDD66D8C3`** on region 0, region 1
**`49e318bf…`** (43 modules).  `0fca:8010`, console `pb> `, **ssh on
`169.254.0.1`**.  No NVRAM/MCT/RPMB write; no restore needed.

## Goal of this session

Handoff (a): associate to a real AP and measure throughput; then (b) the
per-unit calibration NVS.  The operator then added the USB throughput question,
and after that BT / FM / GPS on the same combo chip.

## Confirmed (measured, read in a file, or seen on the bench)

* **The WLAN associates.**  `wpa_state=COMPLETED`, WPA2-PSK/CCMP on 5785 MHz at
  −41 dBm, MCS7/65 Mbit/s; DHCP lease **192.168.0.212**; `ping` to the gateway
  (0% loss) and to 1.1.1.1 (0% loss).  [report 236 §3–§4; bench, image
  `0x50CD2A48` + rootfs `6c2cfb30…` at the time]
* **The association blocker was the missing CCMP crypto modules.**  The 4-way
  handshake completed (message 3 decrypted with AES-UNWRAP, so the PSK was
  right) and the pairwise-key install returned `-ENOENT`:
  `nl80211: kernel reports: key addition failed` / `set_key failed; err=-2`.
  Two probes (`PBNL new_key enter` in cfg80211, `PBKEY-ENTER … cipher=0xfac04`
  in mac80211, then `err=-2`) placed the failure inside
  `ieee80211_key_alloc()`, whose CCMP path allocates `ccm(aes)`.
  `CONFIG_CRYPTO_CCM=m` / `CONFIG_CRYPTO_CTR=m` were set but **neither module
  was in the region-1 rootfs**, and `mac80211.ko` has no link dependency on
  them (`depends: cfg80211,libarc4`).  With them shipped and `modprobe`d by
  `rcS`, the same handshake ends `PTK=CCMP GTK=CCMP` / `CTRL-EVENT-CONNECTED`.
  [report 236 §1–§3; trap 244]
* **A 5 GHz AP needs the country set.**  The world regdomain is
  `(5735 - 5835 @ 80), (N/A, 20), (N/A), PASSIVE-SCAN`; cfg80211 refused to
  transmit (`regulatory prevented using AP config, downgraded`) and the auth
  timed out.  `iw reg set AR` gives the same range without the passive flag and
  the association proceeds.  [report 236 §4; `iw reg get` read live]
* **The per-unit NVS is shipped.**  `local/work/wl128x-nvs-perunit.bin`
  (md5 `547e62d7…`) is the generic 1113-byte file with its first 468 bytes
  replaced by the NVRAM record; the device carried the generic file
  (`9a7b84…`) before, and the built image was verified with `debugfs`
  (`547e62d7…`).  [report 236 §5]
* **The USB gadget had been running MUSB's PIO fallback.**  Raw `nc`/`socat`
  measured **3.3 MB/s both ways**; the interrupt counters showed ~1 `mc`
  interrupt per 512-byte packet and the `dma` IRQ at **6** total (ever);
  `MUSB_HWVERS` reads **`0x0800`** (RTL 2.00), and `dma_channel_program()`
  refuses any buffer with `dma_addr % 4` (silently → PIO).  A one-shot probe
  printed `PBDMA ep3-tx refused: addr 0x81907016`.  [report 236 §7; trap 246]
* **The gadget fixes work.**  `musb->g.quirk_avoids_skb_reserve = 1` (RX) and a
  TX realign in `gether_start_xmit()`: **RX 3.3 → 6.3 MB/s, TX 3.3 → 4.1 MB/s**
  measured with `dd | nc`, and the `dma` IRQ count now scales with the transfer
  (1 346 → 42 441 over 40 MB).  [report 236 §7]
* **40 MHz on wl12xx is harmful.**  The AP offers it (`HT operation: primary
  157, secondary above, width any`); with `SUP_WIDTH_20_40` the station
  associates but the rate collapses (`rx bitrate 13.0 MBit/s MCS 1`, then
  `6.5 MCS 0`) and **RX falls to 622 KB/s** (TX unchanged at 2.9).  Reverted;
  the rebuilt `wl12xx.ko` is byte-identical to the baseline (`2fb86c4d…`).
  [report 236 §8; trap 247]
* **Wi-Fi is 1×1 HT20.**  `wl12xx_ht_cap` is
  `GRN_FLD | SGI_20 | RX_STBC`, `rx_highest = 72`, `ampdu_factor = 8K`; the
  measured link is MCS7/65 Mbit/s and TCP buffer tuning does not move it.
  [report 236 §6]
* **The BT core answers HCI.**  `0x4A100118` (the UART2 CTS/RTS pair) was
  `0x011F011F` (mux 7, safe mode); the reference's idle value is
  `0x00000118`.  Writing it at runtime made `pbhci`'s `HCI_Reset` return
  `04 0e 04 01 03 0c 00` on the first attempt; after the DTS fix the pad reads
  `0x00000118` at boot and the probe answers with no runtime poke.
  [report 237 §1–§2; trap 248]
* **`hci0` appears.**  `pbhci --attach` (`TIOCSETD` `N_HCI` +
  `HCIUARTSETPROTO` `HCI_UART_H4`) makes the kernel create
  `/sys/class/bluetooth/hci0`, which then exchanges commands with the chip
  (`Bluetooth: hci0: Opcode 0x201c failed: -38`, a benign short vendor reply).
  [report 237 §3–§4]
* **`HCIDEVUP` is `ENOSYS` on 7.3.3**, so the controller stays down and a raw
  HCI inquiry fails `Network is down`; the mgmt `SET_POWERED` attempt made no
  visible status.  [report 237 §5]
* **`BOOTCTR_FCTRY` is readable on our image.**  `/init`'s top-up logs the record
  it read, so `dmesg | grep pbrpmb` gives
  `pbrpmb: topup: BOOTCTR_FCTRY at block 0x3a record@196 = 23000000` and
  `pbrpmb: topup: 0x23 >= threshold 0x1e, no write` — the counter read **`0x23`**
  and the image did not write.  (`/dev/rpmb` itself is TabletOS-only; this is a
  logged read, not an RPMB access.)  [bench; `SAFETY.md` §2, `DEVELOPMENT.md` §5]
* **Patch re-export**: **61 files / 8 762 lines**, sha256 `20556c5f…`,
  0 rejects, 61/61 byte-identical against pristine 7.3.3 (including the
  `musb_gadget.c`/`u_ether.c` additions and the DTS pad fix).

## Inferred (reasoning from evidence, not directly measured)

* **The -ENOENT is crypto, not the station state.**  Strong: the probe printed
  `PBKEY addr=… sta=yes state=3 assoc=1 epp=0 pair=1 same=1` at the same time
  the key install succeeded once `ccm` was loaded, which rules out the
  `WLAN_STA_ASSOC` check at `cfg.c:692`.
* **The chip's BT transmitter waits for CTS.**  Strong but indirect: with the
  RTS half un-muxed the chip never answers; the moment both halves are muxed it
  answers, and the reference implements a "force-RTS" devctl for the same pair.
* **RNDIS is now the USB limit.**  Medium: with DMA engaged the link moves
  6.3 MB/s and RNDIS carries one Ethernet frame per transfer; CDC-NCM
  (aggregation) is untested.
* **The `0x201c` error is harmless.**  Medium: it is a TI vendor op answered
  with a short Command Complete; `hci0` is created and other commands complete.
  Not proven by a scan, because power is still missing.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`WRONG_KEY` means the passphrase is wrong."**  It does not: message 3
  decrypted with AES-UNWRAP.  wpa_supplicant uses that label for any PTK-install
  failure; the discriminator is the `set_key failed; err=-2` line.
* **"The `-ENOENT` is `ieee80211_add_key()`'s `WLAN_STA_ASSOC` check."**  It
  looks exactly like it (`cfg.c:692` returns `-ENOENT`), and a probe proved the
  station was `state=3 assoc=1`.  The error came earlier, from
  `crypto_alloc_aead()` inside `ieee80211_key_alloc()`.
* **"`channel_width=40` means we are at 40 MHz."**  It reports the *AP's*
  advertised width; the operating width is visible only in the rate
  (MCS7/65 = HT20).
* **"Advertise HT40 and the link doubles."**  It associated and then collapsed
  to MCS0; RX fell 4.4 MB/s → 622 KB/s.
* **"USB is USB — 480 Mbit should be tens of MB/s."**  Only with the DMA engine;
  the PIO fallback is ~4 MB/s and says nothing in the log.
* **"Dropbear at 2.3 MB/s is the USB speed."**  It was 30 % of it; the raw path
  was 3.3 MB/s and is now 6.3.
* **"BT is silent because `BT_EN` is not driven / the chip is unpowered."**
  `BT_EN` was verified at the pad and driven; the silence was the **CTS/RTS
  pads**.  (This also retires the trap-234 reasoning as a blocker.)
* **"The DTS `OMAP4_IOPAD(0x11e, …)` entry is a mistake."**  It is not:
  pinctrl-single here is **16-bit** wide, so `0x11e` is the high halfword of
  the `0x11c` pad (uart2_tx) — the pairs are written as two halfword entries.
* **A long-running `nohup … &` inside a foreground shell call** keeps the tool's
  pipe open and looks like a hang after the flasher has finished.  Start the
  flasher as a background task instead (operator noticed twice).

## Unknown / open questions

* **Why the first authentication attempt often times out** and the supplicant
  then sits in `SCANNING` without retrying.  Resolve by capturing the
  supplicant log with `-d` across a failing and a succeeding attempt.
* **Why the 5-sr firmware panics on scan** (report 235 §3) — needs the fwlog
  reader fixed for the wl12xx layout first (trap 241).
* **Why the ELP wake is not honoured** (report 232 §6) — needs a non-printk
  instrument (the DRAM ring).
* **Whether `BT_EN` must stay low while the WLAN runs** on this board (report
  216 says the reference holds it low).  Resolve by running both cores together
  and watching for WLAN recovery.
* **Which service pack the BT core needs** — see the contradiction
  `bt-firmware-tiinit-vs-wl1283.md`.

## Superseded / uncertain

* **"The WLAN is up but not usable; association is the next milestone"**
  (sessions 50/51) — superseded: it associates, leases and passes traffic.
* **"The per-unit NVS is the next calibration step"** — superseded: it is
  shipped; measuring the TX-power difference with/without it is still open and
  not needed for function.
* **"Wi-Fi can be improved to 40 MHz"** — superseded by the measurement
  (trap 247).
* **"BT is silent / not a liveness oracle"** (trap 234) — superseded as a
  *blocker*: the core answers once the CTS/RTS pads are muxed (trap 248).
* **The handoff's "use linux-firmware's `TIInit_7.2.31.bts`, not the reference's
  `WL1283.bts`"** — unverified and now recorded as a contradiction; the
  reference's file is the chip's own service pack (report 216 §0), but no
  service pack has been loaded under Linux yet.

## Bench state inherited by session 53

| item | value |
|---|---|
| region 0 | **`0xDD66D8C3`**, `0x939E4C` B, kernel `7.3.3` (MUSB DMA fix + UART2 CTS/RTS pads) |
| region 1 | **`49e318bf11bccba163ef990c9fd8858baff0497e696ab3d8371f56ca28ddfef1`**, 67 108 864 B, **43 modules** (`local/images/rootfs-region1-wl1283-bt.ext4`) |
| kernel patch | **61 files / 8 762 lines**, sha256 `20556c5f…`, 0 rejects, 61/61 |
| OS running | **our Linux image** (`0fca:8010`), console `pb> `, **ssh `169.254.0.1`** (dropbear, key-only); no door (that exists only on TabletOS) |
| `BOOTCTR_FCTRY` | **`0x23`** — `dmesg \| grep pbrpmb` gives `pbrpmb: topup: BOOTCTR_FCTRY at block 0x3a record@196 = 23000000` then `0x23 >= threshold 0x1e, no write`; `/init` tops up at ≤`0x1e` (`SAFETY.md` §2) |
| device-side files | `/tmp/pbhci`, `/tmp/wpa.conf`, `/tmp/att.log` — all on **tmpfs**, gone after a reboot; **nothing added to the UFS** |
| flashes this session | region 0 four times (DMA work ×2, pads), region 1 four times (NVS, key diagnostics ×2, BT), plus one both-region session |
| way back to TabletOS | `local/work/ifs-shim-sealed.ifs` at `0x825` **+** `local/work/rcfs-1917.rcfs` at `0x8D5` |
| restore required | **none** |

## Artefacts produced

* Reports `docs/reports/236-wl1283-association-and-usb-dma-2026-11-15.md`
  and `237-wl1283-bt-hci-2026-11-15.md`.
* `kernel/source/omap4-winchester.dts` — `uart2_pins` gains the CTS/RTS pair
  (`0x118`/`0x11A`).
* `drivers/usb/musb/musb_gadget.c` — `quirk_avoids_skb_reserve`.
* `drivers/usb/gadget/function/u_ether.c` — TX realign for DMA alignment.
* `rootfs/etc/init.d/rcS` — `ccm ctr` before the wireless stack; `bluetooth
  hci_uart` after it.
* `rootfs/hci.c` — `--attach` (N_HCI + H4) and `--inquiry` modes.
* Images: `local/images/rootfs-region1-wl1283-nvs.ext4`,
  `…-keydiag.ext4`, `…-keydiag2.ext4`, `…-keydiag3.ext4`, `…-ht40.ext4`,
  `…-wl1283-final.ext4` (`877c42c3…`) and the shipped
  `…-wl1283-bt.ext4` (`49e318bf…`).
* `local/work/firmware-nvs/` — the firmware tree with the per-unit NVS.
* Patch re-exported and verified (61 / 8 762, `20556c5f…`).
