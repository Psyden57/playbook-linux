# 197 — The factory/calibration partition: the per-unit WL128x radio data

Date: 2026-11-09 (session 37, Linux-era 31).  Follow-up to the operator's
session-37 lead: "NVRAM (or some partition exposed in QNX) has calibration data
in there, which might also contain something important for bringing WLAN up."

## 0. Headline

TabletOS 2.1.0.1917 exposes three small partitions beyond the OS/region/UFS
ones: **`hd4` (512 KiB, the MCT header)**, **`hd5` (512 KiB, the factory /
calibration partition)** and **`hd6` (128 KiB, empty)**.  **`hd5` contains the
per-unit TI WL128x radio calibration**: a 19 118-byte, 252-line INI plus a
44 405-byte binary block, alongside the unit serials and the 2011-04 factory
test timestamps.  This is the NVS/calibration source needed for the
*post-enumeration* `wl12xx` bring-up.  It cannot fix CMD5 (firmware-time), but
it is the data mainline `wl128x-nvs.bin` would otherwise have to invent.

Read-only throughout: the partitions were `dd`'d to the device's `/tmp`
(shmem) and pulled over `scp`; nothing was written to the media, NVRAM or RPMB.

## 1. How the partitions were found

On TabletOS (root ssh through the door):

```
/dev/hd3 on / type qnx6                 # 58 GB UFS
/dev/hd1t179 on /base type qnx6         # the RCFS
/dev/hd4  512 KiB   /dev/hd5  512 KiB   /dev/hd6  128 KiB  (df -k)
```

`/dev/hd4` begins with `4a 56 be 92` = **`0x92BE564A`**, the MCT v1.24 magic
(report 100).  The MCT TLV table is intact and matches report 100's model:
`0x34` MBR `0x805–0x814`, `0x1B` OS, `0x1E` NVRAM `0x4805–0x4844`, `0x36`,
`0x1C` FS (UFS), `0x35` region 0 `0x815–0x2804` and region 1 `0x2815–0x4804`.

`hd5` is a **512 KiB factory block**; `hd6` is empty (0 bytes read).

## 2. `hd5` layout

| range | bytes | content |
|---|---|---|
| `0x000000`–`0x0009e4` | 2 532 | factory header: serials, MACs, test timestamps |
| `0x0009e4`–`0x005492` | 19 118 | TI WL128x radio-parameters **INI** (252 lines) |
| `0x005492`–`0x010206` | 44 405 | binary **calibration blob** |
| `0x010206`–`0x080000` | — | zero padding to 512 KiB |

### 2.1 The factory header (`0x0`–`0x9e4`)

ASCII, NUL/`0xFF`-padded:

```
10PRU2QCIK014M03D23     # MB serial (deviceinfo MBSerialNumber)
DRU2QCI14P7530          # system serial (SystemSerialNumber)
E89A8F165145            # module/base MAC
7C8EE43C2261            # module/base MAC
BFTP2011\04\26\13:59:09 # factory test timestamps (2011-04-26 .. 04-29)
BWTP2011\04\26\14:11:16
FATP2011\04\29\02:42:01
FUTP2011\04\29\02:54:41
FCTP2011\04\29\02:46:11
FFTP2011\04\29\03:02:39
FWTP2011\04\29\03:08:03
6PCP2011\04\29\03:08:03
```

The **assigned** WLAN/BT MACs are not here — they are in the NVRAM `deviceinfo`
PPS object: `WLANMAC = 1474118f9cc3`, `BTMAC = 1474118f9cd9` (the live ones,
matching the QNX WLAN MAC `14:74:11:8f:9c:c3`).  The header's
`E89A8F165145`/`7C8EE43C2261` are the module's pre-assignment/base MACs.

### 2.2 The radio-parameters INI (`0x9e4`–`0x5492`)

A TI WL128x radio-parameters file with a **Quanta** (ODM) changelog
(`### Quanta Ver 4.3 ###`).  Per-unit values include:

* `TxPower = 205` (default 20.5 dBm)
* `XTALItrimVal = 4` (XTAL current trim)
* `TxBiPReferencePower_2_4G = 128`, `RxFemInsertionLoss_2_4G = 16`
* `TxBiPReferencePower_5G = 80,80,…`, `RxFemInsertionLoss_5G = 10,10,…`
* `RxTraceInsertionLoss_2_4G/_5G`, `TxPDVsRateOffsets`/`TxPDVsChannelOffsets`/
  `TxPDVsTemperature` (2.4/5 GHz), `TxIbiasTable`
* `CalibrationChannel24 = 1`, `CalibrationChannel5 = 36`
* `STRFRefClock = 2` (38.4 MHz), `Platform_configuration = 2`

These are the per-unit TX power / power-detector / insertion-loss calibration
values the firmware needs.  (The generic `/var/ti1283/tiwlan.ini` and
`/base/lib/firmware/ti1283/tiwlan_winchester.ini` are the *shipped* copies; the
calibrated values are here.)

### 2.3 The binary blob (`0x5492`–`0x10206`)

44 405 bytes of calibration data (IEEE-754-looking tables; the INI's
`TxPDVs*`/`TxBiP*` are the human-readable side of the same calibration).  It is
the QNX driver's machine-readable calibration block.

## 3. What this means for the WL1283 bring-up

* **This is the calibration/NVS data** the operator remembered.  Mainline
  `wl12xx` expects `wl128x-nvs.bin` (a header + MAC + TX calibration).  The
  per-unit radio parameters are here; the **assigned MAC** is in NVRAM
  `deviceinfo` (`14:74:11:8f:9c:c3`).
* **It does not explain CMD5.**  Calibration is consumed by the firmware after
  the SDIO function is enumerated, so it cannot make a silent card answer.
* **When the bus is up** (chip-side measured, or a different unit): derive
  `wl128x-nvs.bin` from this INI/binary (TI's NVS format) with the NVRAM MAC, or
  hand the QNX INI to the firmware.  Keep the values unit-specific.

## 4. Artifacts and sources

* `local/work/1917/parts/hd4.bin`, `hd5.bin` — the pulled partitions.
* `local/work/1917/calib/hd5-header.bin` (2 532 B),
  `wlan-calib.ini` (19 118 B), `wlan-calib-blob.bin` (44 405 B).
* NVRAM `deviceinfo`/`osregion` PPS objects (live, read-only).
* Reports 100 (MCT), 176 (inventory), 196 (the CMD5 negative); `SAFETY.md` §1
  (NVRAM read-only), §6 (an agent never writes NVRAM/RPMB).
