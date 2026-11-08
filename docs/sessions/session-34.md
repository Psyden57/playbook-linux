# Session 34 (absolute) — the QNX-side WL1283 RE is complete: no WLAN rail, clock or extra pad

> Numbering: absolute `34`; the reports call this Linux-era **session 28**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-08.
Unit state at the end: **the known-good Linux image `0xE36C679A` in region 0**
(restored after the QNX read); region 1 is the 7.3.3 charger rootfs
(`ba275807…`, 17 modules).  `BOOTCTR_FCTRY = 0x31` after the restore boot
(`0x31 >= threshold 0x1e, no write`); pack **Full: 100 %, 4.126 V, +265 mA,
ISL9519 `Charging`**.  The 1917 IFS is not on media.  The operator's Linux
RPMB top-up test succeeded (one write at/below the threshold, read back).

## Goal of this session

Finish the **QNX-side WL1283 RE** that HANDOFF §1.4 left open after session 33
(report 188 §4), so the WL1283 can be formally stopped on evidence rather than
on the bench-visible exhaustion alone:

* read `/pps/services/hw_info/inventory` (`hw_info_pps`);
* decode `hwio-config-winchester.so`'s `pad_config`/`gpio_config`;
* RE `devb-mmcsd-winchester`'s powerman client (clock/voltage/bus power?);
* decode `devpm-omap4.so`'s `system/power/dev` registration and OSWR tables;
* read the full `tiwlan_winchester.ini` clock/power fields.

## Confirmed (measured, read in a file, or seen on the bench)

* **`hw_info_pps` publishes only `/pps/services/hw_info/inventory?nopersist`,
  built at runtime.**  Attributes are `AudioCodec_Name`, `Processor_*`,
  `Board_*`, `eMMC_*`, `CPU_Type`, `RAM_Size`, `Processor_Numbers`; sources
  are ALSA, `confstr(_CS_MACHINE)`, `/dev/hd0` devctls, `/proc`.  It links no
  HWI library and contains no WLAN string.  No static hwinfo database exists
  in either 1917 tree (`board.tdf` is boot/partition identity only).  The
  `WLAN` node is in the boot-time **syspage** and is read by
  `devnp-ti1283.so`'s own `hwi_find`/`hwi_tag_find`; its documented tags are
  `WLAN_EN_GPIO`/`WLAN_INT_GPIO`.  (Report 189 §1; trap 131.)
* **`hwio-config-winchester.so` is a signal database, not a rail table.**
  `pad_config` is 248 × 20 B, `gpio_config` 72 × 32 B; the resolver in
  `hwio-omap4.so` maps a pad id to `(CORE 0x4A100000 | WKUP +0x21E000, offset,
  half)`.  The WLAN entries: sdmmc5 pads at mux 0 (signals `0x52`–`0x57`),
  WL_IRQ `0x07A` mux 3/signal `0x59`, WL_EN `0x07C` mux 3/signal `0x58`,
  0x07E mux 3/signal `0x5D`, FREF2_OUT `0x19C` mux 0, and WKUP `0x4A31E058`
  **mux 3 on both halves** — the static form of the live `0x000B0003` (trap
  117).  GPIO 53 is input/pull/falling, GPIO 54 output/no-pull; no other GPIO
  maps to a WLAN signal.  (Report 189 §2.)
* **`devb-mmcsd-winchester` has a powerman client, but it registers only a
  binary `-power on/off` mode (cost 3)** and requests no voltage, clock rate
  or bus power.  Its init enables `CM2_L3INIT` MMC1/2 module clocks
  (`0x4A009328`/`0x4A009330` = `0x01000002`), clears `PRM_L3INIT`
  `LOSTCONTEXT` (`0x4A30732C`/`0x4A307334` = 2), sets `SDBP = 1`/`SDVS18`, and
  assumes a 96 MHz input clock; it writes no CM divider/DPLL and touches no
  regulator.  Its base constants are MMC1/MMC2 only, and the WL1283 is on
  MMC5 (owned by `devnp-ti1283.so`).  (Report 189 §3.)
* **`devpm-omap4.so` registers exactly one powerman device, `iva_opp`.**  No
  `system/power/dev` literal, no MMCHS5/`CM_L4PER_HSMMC5` write, no TWL WLAN
  rail.  The `sdmmc5_*`/`gpmc_*` names are four `mmap_device_io()` globals
  that are never read; the OSWR wake table (Winchester2) includes the WL_IRQ
  pad (`0x07A`, GPIO2 bit 21) but is applied only on OSWR entry/exit.  (Report
  189 §4.)
* **The `tiwlan_winchester.ini` clock/power fields are all firmware-time:**
  `TCXO_Clk = 1` (26 MHz), `STRFRefClock = 2` (38.4 MHz wired-OR, normal
  polarity), settling 5 ms each, `ClockValidOnWakeup = 0`,
  `TCXO_ClockValidOnWakeup = 0`, `TCXO_LDO_Voltage = 0` (internal LDO 2.5 V),
  `Platform_configuration = 2` (4 mA SDIO drive, no level shifter/EEPROM).
  (Report 189 §5.)
* **`tools/qnx-hwi.c` is built and was run live** (`ntoarmv7-gcc -std=gnu99
  -O2 -static`): it implements the by-name hwinfo search (`hwi_find` is a
  driver-local helper, not in QNX libc) and dumps every tag of the `WLAN`
  item; read-only.  The `-static` is required — the device's 2014 libc does
  not export `hwi_off2tag`/`__hwi_find_string`/`hwi_next_tag`/`hwi_next_item`.
  (Report 189 §1/§8; trap 131.)
* **The live syspage `WLAN` node exists and carries only the two pin
  options.**  On the 1917 boot: item at syspage offset `0x344`, `itemsize=13`,
  an empty `location` tag (base/len/regshift/addrspace all 0), and one
  `optstr`: **`WLAN_EN_GPIO=54,WLAN_INT_GPIO=53`**.  No rail, clock or second
  enable.  (Report 189 §8; captured with `tools/qnx-hwi.c` built `-static`.)
* **The live 1917 confirms the chip works:** `/pps/services/hw_info/inventory`
  has no WLAN attribute (it reports `AudioCodec_Name::Wolfson WM8994`,
  `Board_Type::ES2.2`, `Processor_Type::Winchester`, eMMC `MCGAFA`,
  `RAM_Size::1024Mb`); `/pps/services/wifi/status` has `wifi_power::on`,
  `wifi_interface::ti0`, MAC `14:74:11:8f:9c:c3` and 40+ scan results;
  `ifconfig ti0` is UP/RUNNING; `sloginfo` shows `TIWLAN SDIO probe WAS
  CALLED`, `SDIO clock ... 24Mhz`, `sdioAdapt_ConnectBus: Send commands
  sequence: 0, 5, 3, 7`.  `BOOTCTR_FCTRY` read `32 00 00 00` on that boot.
* **The door needed a restart.**  A stale `BerryShell-V4` holder from 09:48
  and the session's new holder were killed by **explicit PID** (never
  `pgrep -f`/`pkill -f`), then a fresh `setsid tools/door-hold.sh` connected on
  the qconn door and authenticated; root SSH worked immediately afterwards.
* **The restore is verified:** the known-good `0xE36C679A` was flashed to
  region 0 only; the unit boots 7.3.3, the ISL9519 probes and charges
  (`STATUS=Charging`, `ONLINE=1`, 640 mA / 4208 mV / 384 mA), and
  `pbrpmb: topup` read `0x31 >= threshold 0x1e, no write`.
* **The bench at session start:** `lsusb` = `0fca:8010`, `/dev/ttyACM0`
  present; the unit re-enumerated through `0fca:0001` and back while the
  operator reset it for the RPMB top-up test.  No agent console or flash
  command was issued until the operator said the test was complete.

### External review (same session, report 191)

The operator supplied two independent reviews of report 190
(`response3.txt`, `response4.txt`).  The verified corrections:

* **The GPIO OE banks are not identical.**  QNX drives GPIO1 bits
  0/4/15/18/28/31 and GPIO2 bits 5/7 that Linux leaves as inputs; Linux drives
  GPIO2 bit 20 (the hog).  `GPIO2` bit 21 (WL_IRQ) is **1 (input) in both** —
  the dossier's "bit 21 = 0" was wrong.  GPIO1_31 is `gpio_wk31` on WKUP pad
  `0x4A31E058` (mux 3, input buffer off): QNX drives it low, Linux leaves it
  floating.  This is a real, untested lead.  The OE words are from report 185
  §1; the bit deltas were computed this session.  [report 191 §1.1]
* **The QNX `startup-playbook`/BSP was never audited.**  The 1917 boot image
  contains `startup-playbook` (`0x44008` bytes at image offset `0x908`, source
  `hwi_omap4430.c`); it never runs under our chainloader and is the likely
  source of the QNX-only GPIO outputs.  [report 191 §1.3]
* **TWL `0xBD`/`0xBE` may be the `CLK32KG` resource.**  The datasheet confirms
  `CLK32KG` is a software-controlled 32 kHz clock; the session-29
  12-register difference list includes `0xBD`/`0xBE`.  The actual values were
  not recorded, so this is unresolved.  [report 191 §1.2]
* **MMC5 has no `ti,dual-volt`; it always uses SDVS18.**  `ti,dual-volt` is on
  `mmc1` only (`omap4-l4.dtsi:2164`), so the 3.3 V dummy `vwl1283` is an OCR
  advertisement, not the signalling voltage.  The dossier's H2 ("3.3 V can
  select SDVS33; revert to 1.8 V") is withdrawn; verify
  `HCTL = 0x00000B00` and `CAPA` VS18 instead.  [report 191 §1.4]
* **`MMC_CAP2_NO_SDIO_RESET` is a session-33 custom bit, not upstream and not
  in the current tree.**  H1 needs the quirk re-added; do not set
  `MMC_CAP2_NO_SDIO` (it skips CMD5).  [report 191 §1.5]
* **Pad numbering:** `0x078` = gpio_52, `0x07A` = gpio_53 (WL_IRQ); the
  dossier's §3.2 was off by one.  [report 191 §1.6]
* **WL_IRQ is active-low:** a low level can be asserted or floating, not
  independent proof of "unpowered"; the QNX/Linux `DATAIN` difference and the
  rightmost pad following `ti0` remain the indicator.  [report 191 §1.7]

## Inferred (reasoning from evidence, not directly measured)

* **The QNX software surface is nearly exhausted, but the startup/BSP is not
  audited.**  Five independent layers (powerman preset, devpm plugin, MMC
  driver, board pad/GPIO database, WLAN `.ini`) name no WLAN rail or clock;
  the only WLAN-adjacent pads are the six known lines and the two FREF pads.
  The remaining difference is at the chip's non-exposed power/clock pins or in
  OMAP HSMMC sampling.  *Strength: strong for those five layers; the QNX
  `startup-playbook`/BSP (report 191) remains unread, and it is the likely
  source of the QNX-only GPIO outputs.*
* **The syspage `WLAN` node was the last software fact, and the live dump
  confirmed it is only a pin description.**  `devnp-ti1283.so` uses it only for
  `WLAN_EN_GPIO`/`WLAN_INT_GPIO`; its whole-binary audit touches no
  PMIC/CM/DPLL/regulator.  The live node carries exactly those two options and
  an empty `location`.  *Strength: strong (live + static).*
* **The `TCXO_LDO_Voltage = 0` (2.5 V) is the chip's internal LDO setting, not
  the board's 1.8 V VIO.**  The Motorola module guide's "TCXO powered from the
  chip's internal 1.8 V" is module wording; the `.ini` field is explicit and
  is firmware-time.  It does not reopen the pre-enumeration power question.
  *Strength: moderate — the field is clear; its electrical meaning is inferred
  from the name and the option list.*

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The live `/pps/services/hw_info/inventory` will name the WLAN node."**
  It cannot: `hw_info_pps` has no WLAN attributes and no HWI library; the
  inventory is a board/eMMC/CPU/audio report.  The driver's node is in the
  syspage.  (Report 189 §1.)
* **"`devpm-omap4.so`'s `sdmmc5_*` names are OSWR wake-table entries."**  They
  are four `mmap_device_io()` results stored into globals and never read;
  they are not in any table.  (Report 189 §4.)
* **"The MMC driver's powerman client might request a clock or voltage."**  It
  registers only `-power on/off`, and the one flag it toggles gates a
  `mmc_power()` that is a no-op in the Winchester2 board path.  (Report 189
  §3.)
* **"The board `pad_config` is a sleep-only table."**  It is a signal
  database with active mux values; it contains the sdmmc5 pads at mux 0 and
  the WLAN lines at mux 3, matching the live 1917 state.  Its absence of a
  rail pad is therefore meaningful, not an artefact of it being "sleep only".
  (Report 189 §2.)
* **"The QNX MMC driver might be configuring MMC5."**  Its base constants are
  MMC1 (`0x4809C000`) and MMC2 (`0x480B4000`) only; MMC5 is owned by
  `devnp-ti1283.so` (`pidin mem`).  (Report 189 §3.)
* **"The session-33 image's warm-reset enumeration is a stable property."**
  The unit was seen in the bootrom (`0fca:0001`) during the operator's reset
  and came back; this is a normal boot transient, not a new regression, and no
  agent action was taken.  (Session log.)
* **"GPIO1/GPIO2 are matched."**  Wrong: the OE banks differ (QNX-only outputs
  GPIO1 bits 0/4/15/18/28/31 and GPIO2 bits 5/7; Linux-only GPIO2 bit 20).
  GPIO1_31 is `gpio_wk31` on `0x4A31E058`, input buffer off; QNX drives it low,
  Linux leaves it floating.  (Report 191 §1.1.)
* **"The 3.3 V `vmmc` can select SDVS33."**  Wrong: `ti,dual-volt` is on
  `mmc1` only; MMC5 always uses SDVS18.  The 3.3 V is OCR only.  (Report 191
  §1.4.)
* **"The QNX software surface is fully read."**  The QNX
  `startup-playbook`/BSP was not; it is the likely source of the GPIO outputs.
  (Report 191 §1.3.)

## Unknown / open questions

* **Is VBAT actually present at the WL1283's BGA pins, and is the 26 MHz TCXO
  running?**  The accessible points show 1.8 V and 0 V; the 26 MHz waveform is
  invisible to a multimeter.  *Would settle it:* a schematic/board view or a
  logic analyser/scope at the chip.
* **Does the WL1283 drive CMD5 on the wire under QNX?**  *Would settle it:* a
  logic-analyser capture of MMC5_CLK/CMD/DAT0 on both OSes.  This is the one
  measurement that splits "chip not up" from "host sampling".
* **What exactly keeps the chip's digital core from starting under Linux?**
  Every QNX software layer is read and names no rail/clock (report 189); the
  remaining candidates are the non-exposed VBAT/TCXO/reset pins and OMAP HSMMC
  sampling.  *Would settle it:* the two measurements above.
* **What does the QNX `startup-playbook` set?**  GPIO1/GPIO2 outputs, TWL
  I²C writes, `CLK32KG`, pads?  *Would settle it:* disassemble it (report 191
  §1.3).
* **What are TWL `0xBC`–`0xBE` (`CLK32KG`) on both OSes?**  *Would settle it:*
  read and decode them (report 191 §1.2).
* **Which QNX-only GPIO output matters?**  *Would settle it:* the QNX bisect
  (report 191 §1.1).

## Superseded / uncertain

* **"The WL1283's 26 MHz TCXO is powered from the chip's internal 1.8 V"**
  (report 186 §3, from the Motorola 148603 module guide).  The board's
  `tiwlan_winchester.ini` sets `TCXO_LDO_Voltage = 0`, i.e. the chip's internal
  TCXO LDO is **2.5 V**.  The module guide may be describing a different
  module's supply arrangement; the `.ini` is the PlayBook's own configuration.
  This does not change the static conclusion (all firmware-time), but do not
  quote "1.8 V TCXO supply" as a PlayBook fact without this qualification.
* **"The remaining software surface is the QNX power manager / hwinfo / MMC
  driver."**  All three are now read; **but report 191 shows the QNX
  `startup-playbook`/BSP, the GPIO OE banks and the TWL `CLK32KG` resource
  still need reading.**  The surface is *nearly* closed, not closed.
* **The dossier's H2 ("revert the 3.3 V dummy OCR window to 1.8 V") is
  withdrawn.**  MMC5 has no `ti,dual-volt`, so it always uses SDVS18; the
  3.3 V `vmmc` is an OCR advertisement.  Verify `HCTL = 0x00000B00` and
  `CAPA` VS18 instead.  (Report 191 §1.4.)

## Artefacts produced

* Report **191** (`docs/reports/191-wl1283-external-review-2026-11-08.md`) —
  the two external reviews (`response3.txt`, `response4.txt`), the verified
  corrections, and the ordered next-session plan.
* `docs/contradictions/twl-clk32kg-on-both-vs-0xbd-be-difference.md` — the
  unresolved `CLK32KG` "on in both" vs TWL `0xBD`/`0xBE` question, indexed in
  `docs/contradictions/README.md`.
* Report **190** (`docs/reports/190-wl1283-dossier-2026-11-08.md`) — the
  fresh-eyes dossier: every experiment, comparison, QNX RE result, remaining
  unknown and ranked hypothesis.  It also records the known-good-vs-current
  DTB difference (1.8 V vs 3.3 V dummy WLAN OCR window, FREF2 pad mux) and
  the inline corrections from report 191.
* Report **189** (`docs/reports/189-wl1283-qnx-side-re-2026-11-08.md`) — the
  five-layer static RE, the pad/GPIO table decode, and the conclusion.
* `tools/qnx-hwi.c` — the read-only syspage hwinfo dumper; built **static**
  (`ntoarmv7-gcc -std=gnu99 -O2 -static`, 198,520 B) and run on the live 1917.
  Documented in `tools/README.md`.
* `docs/project/STATE.md` — the session-34 current state, the unit tables
  (known-good `0xE36C679A` in region 0, `BOOTCTR_FCTRY = 0x31`, pack Full
  4.126 V / +265 mA) and the revised blocker/next sections.
* `docs/project/ROADMAP.md` — the "priority after session 34" paragraph and a
  session-34 paragraph in item 9 step 4.
* `docs/project/KNOWN-ISSUES.md` — traps **131** (PPS inventory has no WLAN
  node; syspage node is EN/INT only; build `qnx-hwi` `-static`), **132**
  (`NO_SDIO_RESET` is custom; never set `NO_SDIO`), **133** (MMC5 has no
  `ti,dual-volt`; 3.3 V `vmmc` is OCR only) and **134** (GPIO OE banks differ;
  `DATAIN` on an input-buffer-off pad is meaningless; audit
  `startup-playbook`).
* Commits `52716e6` (report 189 + `qnx-hwi.c` + ROADMAP/KNOWN-ISSUES),
  `0a6e18f` (session note + NUMBERING + tools index), `c03202a` (live read +
  restore), `04e611c` (handoff/docs), `4bfd532` (report 190), and the public
  push `e35534e` on `Psyden57/playbook-linux`; plus the wrap-up commit with
  report 191 and traps 132–134.
* Images: region 0 restored to **`0xE36C679A`** (0x91A748, sha256 `bdbaf5fc…`)
  after the QNX read; region 1 unchanged.  No kernel, DTS or driver changes.

## Next

**The operator has kept the WL1283 open** (session 34).  Read reports **190**
(dossier) and **191** (external review + corrections).  Work in this order:

1. **Audit the QNX `startup-playbook`/BSP** (1917 boot image, offset `0x908`,
   size `0x44008`, source `hwi_omap4430.c`) for GPIO1/GPIO2/TWL/`CLK32KG`
   init.  It never runs under our chainloader and is the likely source of the
   QNX-only GPIO outputs.
2. **Decode TWL `0xBC`–`0xBE`** (`CLK32KG` group/transition/state) and the
   other 9 differences on both OSes.
3. **Bisect the QNX-only GPIO outputs** on the live 1917 (GPIO1 bits
   0/4/15/18/28/31, GPIO2 bits 5/7) one at a time.
4. **H1:** the exact QNX sequence — CMD0 → CMD5 only, no CMD52 *and no CMD8*,
   24 MHz, arg `0x00FFFF00`.  Re-add the custom `NO_SDIO_RESET` quirk; do not
   set `MMC_CAP2_NO_SDIO`.  Record `HCTL`/`CAPA` and the raw R4 response.
   **H2 is withdrawn:** MMC5 has no `ti,dual-volt`; the 3.3 V `vmmc` is OCR
   only, so verify `HCTL = 0x00000B00`/`CAPA` VS18 rather than reverting it.
5. Then the chip-side measurements (VBAT/TCXO/SLOW_CLK, MMC5_CLK/CMD/DAT0)
   with a board view or logic analyser.

If the WL1283 is later stopped, **item 9 step 5** (WM8994 audio + CY8CTMA3
touch, report 176) follows.  Do not re-run the closed experiments (traps
116–134).
