# 217 — The reference's own WLAN bring-up, live on the oracle, and its host program replayed on Linux (both negative for the card)

Date: 2026-11-14 (session 49, Linux-era 43).
Bench: the unit was booted into the **minimal 1917 oracle** twice
(`local/work/ifs-shim-sealed.ifs` at block `0x825`), and returned to our Linux
image (region 0) at the end.  No NVRAM, MCT or RPMB write; the image's own
`/init` top-up ran at each Linux boot.  One device-side change was left on the
oracle's UFS (`hd3`): `/var/ti1283/{firmware.bin,tiwlan.ini}` (see §2.3).

## 0. Headline

The session set out to do what DECISIONS §61 prescribes for a "hardware
blocked" item: **run the reference's own driver on the oracle with its own
controls**, then replay its host-side program in our driver.

Both happened, and both are now measured:

1. **The reference driver brings the WL1283 up on this unit, live** — not a
   recollection from an earlier session.  `devnp-ti1283.so`, loaded exactly as
   `/base/scripts/wifi.sh` loads it, logs
   `sdioAdapt_ConnectBus: Send commands sequence: 0, 5, 3, 7`, downloads
   `firmware.bin`, prints `Firmware running` / `Firmware Version: Rev
   7.1.4.10.77`, and leaves `ti0` **UP,RUNNING** with the assigned MAC
   `14:74:11:8f:9c:c3`.  On the pins: `GPIO2_DATAOUT` bit 22 (WL_EN) **1** and
   `GPIO2_DATAIN` bit 21 (WL_IRQ) **1**.
2. **The same unit, booted into our Linux image, is byte-identical in every
   readable precondition** — WL_EN high, pads `0x0118`/`0x0003`/`0x410B`,
   `GPIO2_OE = 0xCE2ECDFF` — and the chip still never drives WL_IRQ
   (`DATAIN` bit 21 = **0**), so the card is silent.
3. **Replaying the reference's whole MMCHS program on Linux changes nothing**
   (`PBREF: reference MMCHS program applied (CON=0x685 HCTL=0xb02
   SYSCTL=0xe1e07 CAPA=0x04e10080)`; the card still silent), and **so does the
   reference's power-on ordering** (WL_EN owned by the host driver,
   low → 20 ms → high → 120 ms, immediately before the first command).

So the blocker is **not** the MMC host program and **not** the WL_EN ordering.
The two remaining classes are named in §6.

## 1. The reference's own load path

`/base/scripts/wifi.sh` (the 1917 service script) is the authority, and it is
not the invocation the earlier handoff guessed:

```sh
FIRMDIR=/lib/firmware/ti1283
FINAL_FIRMDIR=/var/ti1283
rm -rf ${FINAL_FIRMDIR}
cp -R ${FIRMDIR} ${FINAL_FIRMDIR}
mv -f ${FINAL_FIRMDIR}/tiwlan_winchester.ini ${FINAL_FIRMDIR}/tiwlan.ini
# …merge the per-unit Quanta INI that nvram_resource_manager writes to /tmp…
WAITFOR "networking" /dev/socket
mount -Tio-pkt -o nvram,firmdir=${FINAL_FIRMDIR}/ /lib/dll/devnp-ti1283.so
```

Two facts follow, and both cost time to find:

* **`-o nvram` is what supplies the NVS.**  Without it the driver demands
  `nvs_map.bin` in `firmdir` and fails (`Couldn't open file:
  /var/ti1283/nvs_map.bin: 2` → `dev_attach() failed` → `No such device`).  With
  it, it reads the 468-byte NVS from NVRAM (`NVRAM devctl success, read 468
  bytes`, `NVRAM worked!`).  **Neither the shipped 1917 `/base` nor the stock
  RCFS contains a `nvs_map.bin`** — the file is not part of the image.
* **The interface does not come up by itself.**  After the driver loads,
  `ti0` exists but is down, has the placeholder MAC `ff:ff:ff:ff:ff:ff`, and
  nothing happens until something brings it up.  `ifconfig ti0 up` is that
  event; it is what the wifi stack does on a real boot.

## 2. The live reference bring-up (the oracle)

### 2.1 What the minimal image does by itself

The minimal oracle's `net` service loads `devnp-ti1283` at boot, and the pruned
`/base` has **no** `/base/lib/firmware/ti1283/`, so the boot log is:

```
TIWLAN: driver entry
TIWLAN SDIO probe WAS CALLED: initializing mmc device
SDIO clock Configuration is now set to 24Mhz
******* Driver Created *******
wl1283_attach: call wlanDrvIf_LoadFiles
Couldn't open file: /var/ti1283//firmware.bin: 2
wl1283_entry(): dev_attach() failed
Unable to init /lib/dll/devnp-ti1283.so: No such device
```

The driver never reaches the bus connect: in the reference, **firmware load
comes before the connect** (a subagent's inference that it came after is wrong —
see §7).

### 2.2 With the firmware staged and `-o nvram`

`/var/ti1283` was created on `hd3` (persistent) with `firmware.bin` (275 256 B,
from `local/work/1917/base/lib/firmware/ti1283/`) and `tiwlan.ini` (20 058 B),
then:

```
mount -Tio-pkt -o nvram,firmdir=/var/ti1283/ /lib/dll/devnp-ti1283.so
ifconfig ti0 up
```

produced (capture: `docs/captures/wlan-oracle-up-2026-11-14.txt`):

```
TIWLAN: driver entry
"nvram" option set
ti0EN 54, INT 53
hPlatform_DevicePowerOff++
TIWLAN SDIO probe WAS CALLED: initializing mmc device
SDIO clock Configuration is now set to 24Mhz
******* Driver Created *******
wl1283_attach: call wlanDrvIf_LoadFiles
firmware.bin file size = 275256
NVRAM devctl success, read 468 bytes
NVRAM worked!
tiwlan.ini file size = 20058
******* Driver Created *******
SDIO clock Configuration is now set to 24Mhz
sdioAdapt_ConnectBus: Send commands sequence: 0, 5, 3, 7
Starting to download firmware...            (×6)
Finished downloading firmware.
Firmware running.
Firmware Version: Rev 7.1.4.10.77
```

and the interface:

```
ti0: flags=8c43<UP,BROADCAST,RUNNING,OACTIVE,SIMPLEX,MULTICAST> mtu 1500
        address: 14:74:11:8f:9c:c3
```

with, on the pins,

| register | value | meaning |
|---|---|---|
| `GPIO2_OE` (`0x48055134`) | `0xCE3ECD5F` | bit 22 = 0 → output; bit 21 = 1 → input |
| `GPIO2_DATAOUT` (`0x4805513C`) | `0x21413000` | bit 22 = **1**, WL_EN high |
| `GPIO2_DATAIN` (`0x48055138`) | `0x08200018` | bit 21 = **1**, WL_IRQ high — the chip is up |
| pads `0x148`/`0x14C`/`0x150` | `0x01180118` | sdmmc5, mux 0 + pull-up |
| pad `0x07C` | `0x00030003` | WL_EN (`gpmc_nwp`) + BT_EN both mux 3 |

The `ifconfig ti0 up` is what fires `sdioProbe`; the driver then does its whole
power-on + connect + firmware download inside **~30 ms** of slog time.

### 2.3 Device-side file left behind

`/var/ti1283/{firmware.bin,tiwlan.ini}` on the oracle's `hd3`.  It is a copy of
shipped firmware plus the shipped INI, it is what `wifi.sh` itself creates on a
real boot, and it makes the next oracle boot log reach `LoadFiles`
successfully.  Delete it if a clean oracle is wanted.

## 3. The Linux side, measured on the same unit

Our Linux image (the session-48 tree), with MMC5 probing as usual:

| register | value | reference | verdict |
|---|---|---|---|
| `GPIO2_OE` | `0xCE2ECDFF` | `0xCE3ECD5F` | bit22 out, bit21 in — same |
| `GPIO2_DATAOUT` | `0x31413200` | `0x21413000` | WL_EN (bit 22) **high in both** |
| `GPIO2_DATAIN` | `0x01000010` | `0x08200018` | **WL_IRQ (bit 21) low here, high there** |
| pad `0x07C` | `0x00030003` | `0x00030003` | identical |
| pads `0x148`/`0x150` | `0x01180118` | `0x01180118` | identical |

So the difference visible to software on a booted system is exactly one bit:
the chip never raises its interrupt line under Linux, i.e. it is not up.

## 4. The reference's MMCHS program, replayed on Linux (negative)

The exhaustive re-audit of `devnp-ti1283.so` (subagent, file-only) produced the
controller program and corrected the earlier transcriptions.  Implemented as a
DT-gated one-shot (`ti,playbook-ref-init` on `&mmc5`), applied at the first
command after power-up:

```
SYSCONFIG = (v & ~0x331d) | 0x320d ; SOFTRESET ; poll RESETDONE ;
SYSCONFIG |= AUTOIDLE ; SRA (software reset for all) ;
HCTL |= SDVS18 ; CAPA |= VS18 ; HCTL |= SDBP (poll) ;
ISE = 0 ; IE = 0x317f0137 ; STAT = ~0 ;
re-program the card clock ; CON = 0x85 (OD|HR|CDP) ; HCTL |= DTW
```

Measured result (image `0x04473A45`, kernel #47):

```
omap_hsmmc 480d5000.mmc: PBREF: reference MMCHS program applied
    (CON=00000685 HCTL=00000b02 SYSCTL=000e1e07 CAPA=04e10080)
```

`CON` reads `0x685` rather than `0x85` because `DVAL` (bits 10:9, card-detect
debounce, reset `3`) is not cleared by the write; `DVAL` is inert on a
non-removable host.  **The card still did not answer** (`DATAIN` bit 21 = 0,
no `attach_sdio`, IRQ 115 count 0).

Two things this experiment taught, both recorded as traps:

* **`SRA` clears `CEN`/`ICE`** — the first build performed the reset *after*
  mainline had started the clock, so `SYSCTL` read `0` and the controller ran
  with no card clock at all.  The reference order is *reset, then program the
  clock*; the second build does that (`SYSCTL = 0x000e1e07`).
* **Leaving MMCHS5 in that state wedged `reboot -f`** on both quirk images
  (two `reboot -f` processes blocked, the console shell hung behind one).  The
  clean image reboots normally, so it is the controller state, not the image.
  `echo b > /proc/sysrq-trigger` recovered it and is the documented workaround.

Why the program cannot be the difference, independent of the measurement: the
TRM (`hardware/swpu231ap.pdf.txt`, Table 24-75) marks `CON` bits 0 (`OD`),
2 (`HR`) and 7 (`CDP`) "only for MMC cards" — open drain, broadcast host
response and card-detect polarity are inert for an SDIO card — and the rest of
the program already matched mainline's effect (`SDVS18`, `VS18`, `SDBP`,
`ISE = 0`, `IE`, `STAT` cleared, `DTO = 14`).  The one functional difference, the
24 MHz init clock, was tested in session 35 (H1, `0x29784F48`) and was negative.

## 5. The power-on ordering, replayed on Linux (negative)

The reference powers the chip from its driver: `hPlatform_DevicePowerOff()`
(EN low) at entry, then `hPlatform_DevicePowerOn()` = EN high + **120 ms**
before the first command.  Our board held WL_EN high from a **gpio-hog** at
~1 s, i.e. ~1.8 s before the probe — and session 33's pulse test was in
`omap_hsmmc_probe()`, before the controller was programmed.

Tested properly (image `0x0C48AE8A`, kernel #49): the hog removed, `wl-en-gpios`
on `&mmc5`, and the host driving EN **low → 20 ms → high → 120 ms** in
`set_ios(MMC_POWER_UP)`.

```
PBSIO: mmc4 … clock done … done
mmc4: Failed to initialize a non-removable card
GPIO2_DATAOUT = 0x31413200   (bit 22 high — the pulse ran)
GPIO2_DATAIN  = 0x01000010   (bit 21 low — the chip did not come up)
```

Negative.  Both experiments were reverted; the tree is byte-identical to
pristine + the shipped patch again (verified per file, §7).

## 6. What this proves, and what is left

**Proved.**  The chip, its SDIO interface, its firmware and its rails are fine
on this unit — the reference driver enumerates and runs it, live, in this
session.  The host program is not the blocker (replayed), the WL_EN ordering is
not the blocker (replayed), the pads and GPIO directions match, and VIO/VBAT
cannot be the difference in a way a register write explains (the reference's
driver writes no rail, no clock and no second enable — confirmed twice).

**Left, in the order a bench without a probe can still attack them:**

1. **A board line the reference's *boot* asserts and its WLAN driver does not.**
   The reference is a full OS: `startup-playbook`, `pmic-twl6030-ctl_script`,
   powerman and the USB/BT services all run before (and around) `wifi.sh`.  Our
   Linux boot asserts a different subset.  The strongest untested version of
   this: take the **stock TabletOS** capture (where WLAN works) of every
   readable register *at the moment `ti0` comes up*, and diff it against our
   Linux boot at the same point — the same method that found the speaker's
   `audio_boost`.  The oracle cannot stand in for it: it is the minimal image,
   and its WLAN only came up here because the missing firmware and NVS were
   supplied by hand.
2. **The chip-side pins** (VBAT, the 26 MHz TCXO, SLOW_CLK, RESET_N) remain
   unmeasurable (trap 165, no probe).  This session narrows them rather than
   clearing them: the reference proves that *with the reference's boot* they are
   correct.
3. **A boot-time sequencing requirement**: EN must be asserted only after some
   other board line is stable.  Testable in the driver, but one variable per
   boot and the list of candidates is now the eight QNX-only GPIO outputs
   (GPIO1 0/4/18/28/31, GPIO2 5/7), which were only ever tested *after* the
   chip was up (trap 136) — never as power-up prerequisites.

## 7. Errors made this session (each is a trap now)

* **I read the wrong GPIO2 registers and drew a conclusion from them.**  A dump
  of `0x48055000..0x3C` is the *interrupt* block; `OE`/`DATAIN`/`DATAOUT` are at
  `+0x134`/`+0x138`/`+0x13C`.  From that I concluded "the minimal image never
  ran startup-playbook" — wrong; the pads were already `0x0118`/`0x0003`/`0x410B`.
* **`slay -f io-pkt-v6-hc` killed the stack that carries `rndis0`**, i.e. our
  only access to the oracle, needing a power cycle.  `slay` matches the
  executable name (trap 172); kill strays **by pid**.
* **A filtered `sloginfo` hid the connect.**  `sloginfo | grep -iE
  "ti1283|sdio|wlan"` showed no `Send commands sequence` line, and I concluded
  the driver never reached the bus.  The unfiltered log had it (and the
  earlier attempt in §5's log had it twice).  Read the log whole.
* **`memw32 <addr> <val> <mask>` wants the value already masked.**  Passing
  `410B` with mask `FFFF0000` wrote `0x0000` over pad `0x07A` (WL_IRQ's
  mux+pull); restored by writing `410B0000` with the same mask.
* **The reference loads the firmware *before* connecting.**  The order in slog
  is `LoadFiles` → `ConnectBus`, so a missing `firmware.bin`/NVS looks like "the
  card does not enumerate" when nothing was ever sent.
* **`-d` takes whitespace-separated driver options** (`-d ti1283 firmdir=…`),
  not commas; `-d ti1283,firmdir=…` makes io-pkt look for
  `devn-ti1283,firmdir=….so`.

## 8. Sources

* `docs/captures/wlan-oracle-up-2026-11-14.txt` (942 lines, the oracle boot and
  the live bring-up).
* `/base/scripts/wifi.sh`, `/base/etc/system/config/bluetooth.conf`,
  `/proc/boot/pmic-twl6030-ctl_script` (the 1917 tree under
  `local/work/1917/`).
* Static RE: reports 180/186/189/190 (prior) and this session's re-audit
  (`devnp-ti1283.so`: `sdio_core_pm` `0xcd04c`, pulse-init `0xcd420`,
  `sdioAdapt_ConnectBus` `0x25700`, `hPlatform_DevicePowerOn` `0x28e4c`);
  report 216 (the BT half).
* TRM: `hardware/swpu231ap.pdf.txt` Table 24-75 (`MMCHS_CON`), Table 24-98
  (`HCTL`), Table 24-100 (`SYSCTL`), Table 24-110 (`CAPA`).
* Prior negatives not re-run: reports 180 §6, 185, 186, 188, 191, 192, 196.
