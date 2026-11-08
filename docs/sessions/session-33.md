# Session 33 (absolute) — WL1283: the powerman preset has no WLAN rail; the electrical comparison and the CMD52/settle tests are negative

> Numbering: absolute `33`; the reports call this Linux-era **session 27**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-08.
Unit state at the end: **our Linux 7.3.3 image `0x0353A241` in region 0** — the
session-32 tree with the FREF2 enable and the WL_EN reset pulse removed, and
the session-33 MMC experiments reverted; it enumerates by itself on a warm
reboot.  Region 1 is the 7.3.3 charger rootfs (`ba275807…`).  The 1917 IFS is
**not** on media.  `BOOTCTR_FCTRY = 0x27` (`pbrpmb topup` left it alone); pack
**Full: 100 %, 4.155 V, +5 mA**.  The device is reassembled.

## Goal of this session

ROADMAP item 9 step 4, as the handoff ordered it: **first RE the QNX powerman
`winchester` preset** (`bin/powerman` + `devpm-omap4.so`) for a WLAN rail or
clock, then the chip-side checks with the multimeter (VBAT/VIO/WL_EN/WL_IRQ/
SLOW_CLK).  One variable per boot; do not repeat the `CM_DIV_M5 = 0x329` write
(trap 116) or a runtime MMC5 `unbind`/`bind` (trap 120).

## Confirmed (measured, read in a file, or seen on the bench)

* **The powerman `winchester` preset has no WLAN rail or clock** (report 187).
  `winchester_environment` resolves to 94 authorities, 47 edges, 6 dependency
  groups and 35 initial states.  The WLAN authorities
  (`activity/wifi_driver/active|idle`, `air_needs_wifi`, `wlan_needs_DMA`,
  `WLAN_NEEDS_PREVENT_OSWR`) only gate the standby shutdown: groups 3–5
  (`user_standby` + `wifi_driver/idle` + an OSWR-prevention authority) publish
  `msg::shutdown` to `/pps/services/power/shutdown/control`; `air_needs_wifi`
  and `wlan_needs_DMA` are published but referenced by no group or edge.
  `ldo_vaux1`/`ldo_vaux3` are initial PPS commands for `pmic-twl6030-ctl`; the
  startup registers only `regen1,regen2,vaux3`, and VAUX3 is the LM3532
  backlight rail (REGEN1/2 are the panel rails).  `devpm-omap4.so` is the
  OMAP4 PRCM/voltage/DVFS plugin; its only WLAN-adjacent strings are the
  `sdmmc5_*`/`gpmc_nwp` OSWR-wakeup pad names.
* **The QNX services are otherwise not WLAN power:** `winchester_phy_ctrl` is
  the **USB** PHY (its own strings: "turning-on/off phy", OMAP4430_L4CFG_BASE,
  USB PHY/OTG clock); `pmic-twl6030-ctl` handles the display rails; powerman
  handles standby/OSWR.  No service turns on a WLAN supply.
* **The chip is up under QNX and not under Linux, at the chip.**  On the bare
  board with the 1917 IFS, `ti0` is UP/RUNNING and
  `GPIO2_DATAIN = 0x08221018` (bit 21 = WL_IRQ high).  Under our Linux image
  with `GPIO2_DATAOUT = 0x31413200` (WL_EN high), `DATAIN = 0x01000010`
  (bit 21 low).  `ifconfig ti0 down` on QNX drops `DATAOUT` to `0x31013200`
  and `DATAIN` to `0x08021018`; `ifconfig ti0 up` restores both.  The
  **rightmost front pad** follows exactly that (1.8 V ↔ 0 V) — it is the
  chip's IRQ/output, not a rail (report 188 §1).
* **WL_EN is not exposed at the accessible points.**  Pad `0x4A10007C` mux 3 =
  `gpio_54` (TRM table 18-358) was confirmed and GPIO2_22 toggled from the
  shell (`devmem 0x48055190/0x48055194 32 0x00400000`, read-back
  `0x31013200`/`0x31413200`) through a 90 s low, a 5 s/5 s wave and a 2 s/2 s
  wave.  No accessible point moved (report 188 §1).
* **All WLAN pads match QNX:** `0x078 = 0x410B4103`, `0x07A = 0x0003410B`,
  `0x07C = 0x00030003`, `0x148/14C/150 = 0x01180118`; `GPIO2_DATAOUT` matches.
  VIO is 1.8 V at several points.  The Linux regulator list has V1V8/VCXIO/
  VDAC 1.8 V, VUSB 3.3 V, V2V1/VANA 2.1 V, VAUX1/2/3 2.8 V, VMMC/VPP/VUSIM
  off.  **No 3.3 V chip rail is exposed**; the one ~4.7 V point is a resistor
  on the VBUS side (report 188 §1).
* **A 200 ms post-power-on settle is negative** (image `0x0B38612D`, DTB-only
  `post-power-on-delay-ms = <200>` on `&mmc5`): `delay1 200ms` /
  `power_on delay 200ms`, then CMD52/CMD8/CMD5 still `err=-110`; IRQ stays low (report 188 §2.1).
* **Skipping the SDIO reset is negative** (image `0xAC98BCAD`): QNX's sequence
  is CMD0 → CMD5; Linux sends CMD52 first.  `MMC_CAP2_NO_SDIO_RESET` (bit 30,
  `include/linux/mmc/host.h`; honoured in `mmc_rescan_try_freq()`; set for the
  `480d5000` host) gives CMD0 → CMD8 → CMD5, and CMD5 still times out (report 188 §2.2).
* **The USB re-enumeration regression is isolated to the session-32 WL_EN
  reset pulse.**  The gadget bound at ~5.6 s but the host configured it only
  on a re-plug; the A/B on warm reboots:

  | image | FREF2 | pulse | enumerates by itself |
  |---|---|---|---|
  | `0xE36C679A` (known-good) | off | absent | **yes, 12–14 s** |
  | `0x5920CAAC` (session-32 code) | on | on | no |
  | `0x90BDE221` (session-32 code) | **off** | on | no |
  | `0x0353A241` (final) | off | **off** | **yes, 14 s (gadget configured 5.28 s)** |

  `pb_wl1283_reset_pulse()` (GPIO2_22 low 500 ms → high) is the cause; the
  FREF2 enable is excluded and the session-33 MMC changes are not involved.
  Both experiments are negative for CMD5 and are removed from the tree.  A
  UDC `soft_connect` disconnect/connect also re-enumerates (Device 117 → 118)
  and remains a workaround, but is no longer needed (report 188 §3).
* **Media/counter:** the region-0 IFS was flashed to the 1917 reference and
  back to Linux; the QNX boot topped `BOOTCTR_FCTRY` to `0x32`, and the final
  Linux boot reads `0x27`.  Region 1 was never changed.  The final tree/patch:
  `kernel/patches/0001-playbook-winchester-7.3.3.patch` is **7 258 lines /
  47 files**, sha256 `e7b7fc2e…`, applied to pristine 7.3.3 with 0 rejects and
  47/47 files byte-identical.

## Inferred (reasoning from evidence, not directly measured)

* **The chip's failure is at its power/clock pins, not the host.**  Pads,
  WL_EN, VIO, the regulator set and the command sequence match the working
  reference; the chip does not drive its IRQ even after 400 ms with the SDIO
  clock running and with CMD52 removed.  *Strength: strong.*
* **The powerman lead is closed.**  No QNX service enables a WLAN rail; the
  `ldo_vaux*` names were a false lead (display rails).  *Strength: strong.*
* **The USB regression is the WL_EN reset pulse's power transient.**  The
  isolation is a clean A/B (`0x90BDE221` pulse on → fails; `0x0353A241` pulse
  off → enumerates); the pulse toggles the WL1283's enable and disturbs the
  shared 1.8 V/USB PHY state.  *Strength: strong (measured, one variable).*
* **The session-33 MMC changes are not involved.**  They are confined to
  `480d5000` and were reverted in the final image, which still enumerates with
  the pulse off.  *Strength: strong.*

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`ldo_vaux1`/`ldo_vaux3` are WLAN rails."**  They are the display/
  backlight rails (`vaux3` → LM3532, `regen1/2` → panel); `vaux1` is not even
  registered with the pmic client.  The WLAN authorities only gate standby.
* **"The chip needs more time after WL_EN."**  A 200 ms post-power-on delay
  (400 ms before the first command) is negative; the baseline had seconds.
* **"CMD52 before CMD5 wedges the card."**  Skipping `sdio_reset()` gives
  CMD0 → CMD8 → CMD5 and CMD5 still times out.
* **"The rightmost front pad is a rail."**  It follows `ti0` down/up, so it is
  the chip's IRQ/output; it is a symptom, not a supply.
* **"The USB regression is caused by the session-33 MMC changes."**  They are
  confined to `480d5000`; reverting them still failed with the pulse on.
* **"The QNX trip caused the USB regression."**  Past QNX↔Linux trips were
  clean, and the A/B isolates the session-32 pulse.
* **"The FREF2/AUXCLK1 enable caused it."**  `0x90BDE221` has FREF off and the
  pulse on, and still fails; the FREF enable is not involved.
* **"The USB regression is the pulse's 500 ms busy-wait shifting the boot."**
  The gadget still binds at ~5.6 s; the host misses the *connect*, not the
  boot.  The pulse's power transient is the supported mechanism.

## Unknown / open questions

* **Is VBAT actually present at the chip's BGA pins?**  The accessible points
  show 1.8 V and 0 V, no 3.3 V.  *Would settle it:* a board view/schematic or
  probing the chip's supply pins under the shield.
* **Is the 26 MHz TCXO running?**  A multimeter cannot measure it.  *Would
  settle it:* a scope/analyser or a known-good TCXO.
* **Does the WL1283 drive CMD5 on the wire?**  The controller sees `err=-110`
  (no response).  *Would settle it:* a logic-analyser capture of MMC5_CLK/
  CMD/DAT0 during the QNX and Linux boots.
* **What does the hwinfo `WLAN` node / `/pps/services/hw_info/inventory`
  say?**  The driver's `hwi_find("WLAN")`/`hwi_tag_find` may name a power rail,
  clock or GPIO beyond EN 54/IRQ 53.  *Would settle it:* one TabletOS boot and
  the PPS inventory read (HANDOFF §1.4).
* **Does `devb-mmcsd-winchester` set the MMC5 functional clock/voltage or the
  SDIO bus power?**  Linux uses a 48 MHz functional clock; QNX's `CLKD = 2` on
  96 MHz was noted but the 96 MHz parent was never tested.  *Would settle it:*
  RE its powerman client (`system/power/mgr`, `-power`, `clock`) and compare
  the CM register.
* **Does `devpm-omap4.so` set an HSMMC5/WLAN device state at boot?**  Its
  `system/power/dev` registration and OSWR/wakeup tables were not decoded.
  *Would settle it:* decode the device registration and the `sdmmc5_*` OSWR
  table.
* **Do the `hwio-config-winchester.so` `pad_config`/`gpio_config` arrays name
  a WLAN-adjacent pad/GPIO beyond `0x078`/`0x07A`/`0x07C`/`0x148`–`0x152`?**
  *Would settle it:* decode the arrays.
* **Should `/init` still toggle `soft_connect`?**  The pulse is removed and
  the final image enumerates by itself, so the toggle is no longer needed; it
  remains a proven fallback if the warm-reset state ever recurs.

## Superseded / uncertain

* **"The `winchester` preset will name a WLAN rail/clock."**  It does not;
  report 187 is the answer.  The handoff's decision table takes the
  "preset only prevents OSWR/CPU sleep" branch.
* **"The remaining software surface is the QNX power manager."**  It was the
  last software layer, and it is now closed; the remaining difference is the
  chip's power/clock pins or host sampling, which the bench cannot see.
* **"The session-32 `0xAF4FC103` image is the same as the new test images."**
  The new images add the 200 ms MMC5 settle (`0x0B38612D`) and the CMD52 skip
  (`0xAC98BCAD`); both are negative and are not known-good baselines.
* **"The USB regression is caused by the QNX trip"** (this session's first
  hypothesis, before the A/B).  Superseded: past QNX↔Linux trips were clean,
  and the pulse on/off A/B isolates the session-32 WL_EN reset pulse.
* **"The USB regression trigger is undetermined"** (this session's second
  statement, after the operator's correction).  Superseded by the
  `0x90BDE221`/`0x0353A241` warm-reboot A/B: the pulse is the cause.
* **"The FREF2 enable or the session-33 MMC changes caused the USB
  regression."**  Excluded: `0x90BDE221` has FREF off and still fails; the
  session-33 changes are confined to `480d5000` and were reverted.

## Artefacts produced

* Reports: **187** (`docs/reports/187-wl1283-powerman-preset-2026-11-08.md` —
  the preset tables, the WLAN authority mapping and the display-rail
  correction), **188** (`docs/reports/188-wl1283-session33-bench-2026-11-08.md`
  — the QNX/Linux pad/GPIO comparison, the two negative test images and the
  USB regression).
* Source: the **final** tree has `local/kernel-tree/drivers/mmc/host/
  omap_hsmmc.c` with `pb_enable_fref_clk2()` and `pb_wl1283_reset_pulse()`
  present but **not called** (both `__maybe_unused`), and the session-33 MMC
  experiments reverted.  The patch
  `kernel/patches/0001-playbook-winchester-7.3.3.patch` is **7 258 lines /
  47 files**, sha256 `e7b7fc2e…`, applied to pristine 7.3.3 with 0 rejects and
  47/47 files byte-identical.
* Images: `0x0B38612D` (DTB 200 ms, 0x91A264), `0xAC98BCAD` (CMD52 skip,
  0x91A664), `0x5920CAAC` (session-32 code, 0x91A48C), `0x90BDE221` (FREF off,
  pulse on, 0x91AC04) and **`0x0353A241`** (final: FREF and pulse removed,
  0x91A434, warm-reboot self-enumeration), saved as
  `local/images/kernel-0x0353A241-final.img` (crc `0x0353A241`).  The
  known-good baseline remains `0xE36C679A`.
* QNX trip: the 1917 IFS in region 0 for the comparison boot; `pbmem3` GPIO2
  reads; the soft-connect re-enumeration test (`Device 117 → 118`).

## Next

The operator's call.  The WLAN software/electrical comparison is exhausted
with the available equipment, but the QNX-side RE is **not** complete: the
next session should read `/pps/services/hw_info/inventory` on the live 1917
(`hw_info_pps`), decode `hwio-config-winchester.so`'s `pad_config`/`gpio_config`
arrays, RE `devb-mmcsd-winchester`'s powerman client (does it set the MMC5
functional clock/voltage or bus power?), decode `devpm-omap4.so`'s
`system/power/dev` registration and OSWR tables, and check the full
`tiwlan_winchester.ini` clock/power fields (HANDOFF §1.4).  The chip's
non-exposed VBAT/TCXO pins and OMAP HSMMC sampling still need a
schematic/board view or logic analyser.  If the WL1283 is formally stopped,
**item 9 step 5** (WM8994 audio + CY8CTMA3 touch, report 176) follows.  The
USB regression is fixed by removing the WL_EN pulse; the `soft_connect`
toggle is a proven fallback but is not needed in the final image.  The unit is
left on the final image `0x0353A241` (operator's choice).
