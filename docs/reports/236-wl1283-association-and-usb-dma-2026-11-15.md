# 236 — The WL1283 associates: the CCMP crypto modules were missing; and the USB gadget's DMA was silently off

Date: 2026-11-15 (session 52, Linux-era 46).
Bench: image **`0x50CD2A48`** (kernel `7.3.3` #67) + region-1 rootfs
**`877c42c3…`**.

## 0. Headline

**The WLAN associates, gets a DHCP lease and passes traffic.**  The blocker was
not RF, not the passphrase and not the per-unit calibration: **`ccm.ko` and
`ctr.ko` were missing from the rootfs**, so `ieee80211_key_alloc()` could not
allocate the `ccm(aes)` AEAD and every WPA2 pairwise-key install failed with
`-ENOENT`.  wpa_supplicant labels that failure `WRONG_KEY`, which is why it
looked like a key problem for a while.

Two secondary findings, both measured:

* the AP is on 5785 MHz, which the **world regulatory domain marks no-IR** —
  until the country is set, cfg80211 refuses to transmit and the auth times
  out (`wlan0: regulatory prevented using AP config, downgraded`);
* the session's other half: the **USB gadget's MUSB HSDMA was never used** —
  `RX 3.3 → 6.3 MB/s`, `TX 3.3 → 4.1 MB/s` after the alignment fix (§7).

## 1. The symptom, measured

With `4-sr` (report 235) the radio works and the interface is stable, so the
association was the next step.  The first attempts failed in a very specific
way:

```
wlan0: authenticate with 58:90:43:66:e8:78        -> authenticated
wlan0: associate with 58:90:43:66:e8:78           -> associated
wlan0: WPA: RX message 1 of 4-Way Handshake
wlan0: RSN: RX message 3 of 4-Way Handshake       (key data decrypts)
wlan0: nl80211: kernel reports: key addition failed
nl80211: set_key failed; err=-2 No such file or directory
wlan0: WPA: Failed to set PTK to the driver
wlan0: CTRL-EVENT-SSID-TEMP-DISABLED ... reason=WRONG_KEY
```

The decisive detail is that **message 3 decrypts** with AES-UNWRAP and carries a
valid GTK: the PMK derived from the passphrase is correct.  So `WRONG_KEY` is a
mislabelled local failure, not a credential problem.

## 2. Where `-ENOENT` comes from

`nl80211_new_key()` wraps the failing call with `GENL_SET_ERR_MSG(info, "key
addition failed")`, so the error came from `rdev_add_key()` — i.e. from mac80211
(`.add_key = ieee80211_add_key`).  Two temporary `pr_info()` probes (mac80211
`cfg.c` and cfg80211 `nl80211.c`, both modules) localised it in one boot:

```
PBNL new_key enter
PBKEY-ENTER mac=ff0f42ce keyidx=0 pair=1 mode=0 cipher=0xfac04 link=-1
PBNL new_key done err=-2 type=1
```

The call **entered** `ieee80211_add_key()` and returned before the station check
— so not the `WLAN_STA_ASSOC` test at `cfg.c:692` (which is what the code
suggests at first glance; a third probe later showed `sta=yes state=3 assoc=1`).
The only remaining early return that can produce `-2` is:

```c
key = ieee80211_key_alloc(params->cipher, ...);   /* CCMP = 0xfac04 */
if (IS_ERR(key)) return PTR_ERR(key);
```

and inside it, for `WLAN_CIPHER_SUITE_CCMP`:

```c
key->u.ccmp.tfm = ieee80211_aes_key_setup_encrypt(key_data, key_len, ...);
if (IS_ERR(key->u.ccmp.tfm)) { err = PTR_ERR(...); ... return ERR_PTR(err); }
```

`crypto_alloc_aead("ccm(aes)")` returns `-ENOENT` when the algorithm is not
registered.  Our kernel has `CONFIG_CRYPTO_CCM=m` and `CONFIG_CRYPTO_CTR=m`,
and **neither module was in the region-1 rootfs** (36 → 34 modules had never
included them).  `mac80211.ko` does not link against them (`depends:
cfg80211,libarc4`), so `depmod` cannot pull them in either.

## 3. The fix

`ccm.ko` and `ctr.ko` are installed in the region-1 rootfs and `rcS` loads them
before the wireless stack:

```sh
for m in ccm ctr cfg80211 mac80211 wlcore wl12xx wlcore_sdio; do
	modprobe "$m" 2>/dev/null
done
```

With them loaded the same handshake completes:

```
wlan0: PBKEY addr=58:90:43:66:e8:78 sta=yes state=3 assoc=1 epp=0 pair=1 same=1
wlan0: nl80211: set_key ... (no error)
wlan0: WPA: Key negotiation completed ... [PTK=CCMP GTK=CCMP]
wlan0: CTRL-EVENT-CONNECTED
wpa_state=COMPLETED   ssid=TeleCentro-AW-5G  freq=5785  pairwise_cipher=CCMP
```

Then `udhcpc` takes a lease (**192.168.0.212**, the same reservation the stock
OS gets) and the link passes traffic to the gateway and beyond.  Note that
BusyBox has no `default.script`, so `udhcpc` acquires the lease but does not
apply it — `ip addr add` / `ip route add` are done by hand on this rootfs.

## 4. The 5 GHz regulatory trap

The first attempts never reached the handshake: `wlan0: regulatory prevented
using AP config, downgraded`, then `authentication ... timed out`.  The AP is on
**5785 MHz (ch 157)**, and the world regdomain (`country 00`) is:

```
(5735 - 5835 @ 80), (N/A, 30), (N/A), PASSIVE-SCAN     <- no IR
```

so transmitting is refused.  `iw reg set AR` (the operator's country) gives
`(5735 - 5835 @ 80), (N/A, 30), (N/A)` — no passive flag — and the association
proceeds.  This is a **deployment/configuration** matter, not a driver bug: the
port must set the country (or the unit will only work in the 2.4 GHz band on
an AP whose channel the world domain allows).

## 5. The per-unit NVS is shipped

The 468-byte NVRAM record captured in session 51
(`local/work/wl128x-nvs-perunit.bin`, md5 `547e62d7…`, first 468 bytes of the
generic 1113-byte file replaced) is now in the image
(`/lib/firmware/ti-connectivity/wl128x-nvs.bin`).  It changes nothing about the
association (as expected — the handshake is a host-side failure) but it is the
correct radio calibration, so it is the baseline from here on.

## 6. Throughput, and what the link actually is

With the NVS and the fix in place, measured with `dd | nc` (no ssh in the path):

| path | direction | rate |
|---|---|---|
| Wi-Fi | device → host | **2.9 MB/s** (23 Mbit/s) |
| Wi-Fi | host → device | **4.4 MB/s** (35 Mbit/s) |
| USB RNDIS | device → host | **4.1 MB/s** (was 3.3) |
| USB RNDIS | host → device | **6.3 MB/s** (was 3.3) |
| USB over ssh | either | 2.3 MB/s (dropbear adds ~30 %) |

The link is **1×1 HT20**: `wl12xx_ht_cap` advertises
`GRN_FLD | SGI_20 | RX_STBC`, `rx_highest = 72`, `ampdu_factor = 8K`
(`drivers/net/wireless/ti/wl12xx/main.c`).  Observed MCS7/65 Mbit/s.

**40 MHz was tested and is a dead end** (§8): the AP offers it
(`HT operation: primary 157, secondary above, width any`), but the firmware
accepts the association and then collapses to MCS0 — RX fell to **622 KB/s**.

## 7. The USB gadget: MUSB's HSDMA was silently unused

The RNDIS link ran at **3.3 MB/s in both directions** with the CPU half idle and
`qlen 10` requests in flight.  The interrupt counters showed why:

```
 87: musb-hdrc.0.auto (mc)    ~1 interrupt per 512-byte packet
126: musb-hdrc.0.auto (dma)   6 interrupts total, ever
```

The DMA engine was never driving a transfer.  `MUSB_HWVERS` reads **0x0800
(RTL 2.00)**, and for RTL ≥ 1.80 `dma_channel_program()` refuses any buffer
that is not **4-byte aligned**, silently falling back to PIO:

```c
if ((musb->hwvers >= MUSB_HWVERS_1800) && (dma_addr % 4))
        return false;   /* -> PIO */
```

u_ether hands it `skb->data`, and both sides are misaligned:

* **RX**: `rx_submit()` does `skb_reserve(skb, NET_IP_ALIGN)` (= 2), so every
  receive buffer is 2 mod 4;
* **TX**: the network stack's `skb->data` was measured at 2 mod 4 too (a
  one-shot probe printed `PBDMA ep3-tx refused: addr 0x81907016`).

Two small changes, both in the gadget path:

1. `musb->g.quirk_avoids_skb_reserve = 1` in `musb_gadget_probe()` — u_ether
   then does not reserve, and RX buffers stay at `NET_SKB_PAD` (32, aligned);
2. `gether_start_xmit()` copies a misaligned frame into a
   `netdev_alloc_skb()` buffer (one 1500-byte copy, only when needed).

Result: the DMA IRQ count now scales with the transfer, and **RX 3.3 → 6.3
MB/s, TX 3.3 → 4.1 MB/s**.  The remaining gap is the RNDIS function's one-frame-
per-transfer structure; the USB 2.0 bulk ceiling for this controller/topology
is far above what RNDIS reaches.

## 8. Negative: 40 MHz on wl12xx

Adding `IEEE80211_HT_CAP_SUP_WIDTH_20_40` (and `rx_highest = 150`) to
`wl12xx_ht_cap`, rebuilt as a module and flashed region 1:

* the AP allows it (`primary 157, secondary above`), and the station associates;
* but the rate collapses — `rx bitrate: 13.0 MBit/s MCS 1`, then `6.5 MCS 0`;
* TX stayed at 2.9 MB/s and **RX fell to 622 KB/s** (from 4.4 MB/s).

So the WL1283 firmware negotiates 40 MHz it cannot sustain on this band.  The
change was reverted and the revert proven: the rebuilt `wl12xx.ko` is
byte-identical to the baseline (`2fb86c4d…`).  **HT20 is the right setting.**

## 9. Operational notes

* **`/tmp` is wiped by every reboot** — `wpa.conf`, the logs and the control
  socket have to be recreated after a flash.
* `/var/run/wpa_supplicant` may not exist on a fresh boot; `wpa_supplicant`
  fails with `mkdir[...]: No such file or directory`.
* **The first authentication attempt sometimes times out** (observed on 20 and
  40 MHz alike); the supplicant then sits in `SCANNING` and needs a `wpa_cli
  scan` nudge or a restart.  A restart after a fresh `iw dev wlan0 scan` has
  been reliable.
* `disable_scan_offload=1` is needed: the scheduled scan did not return the
  target AP often enough, and the supplicant stayed in "No suitable network".
* `wpa_cli status`'s `channel_width=40` reports the **AP's** capability, not
  the operating width — do not read it as a 40 MHz association.

## 10. Artefacts

* Images: region 0 **`0x50CD2A48`** (kernel `7.3.3`, musb DMA fix), region 1
  **`877c42c3…`** (`rootfs-region1-wl1283-final.ext4`, 36 modules: the 34 plus
  `ccm`/`ctr`, per-unit NVS, CAM+4-sr `wl12xx.ko` `2fb86c4d…`).
* Kernel patch: **61 files / 8 755 lines**, sha256 **`741115a4…`**, 0 rejects,
  61/61 byte-identical against pristine 7.3.3 (the two new files are
  `drivers/usb/gadget/function/u_ether.c` and `drivers/usb/musb/musb_gadget.c`).
* `local/work/firmware-nvs/` — the firmware tree with the per-unit NVS.
* Traps 244–247; DECISIONS §70/§71.
