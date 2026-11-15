# Report index — what is current, what is superseded

The numbered reports are history and are left as written.  This index is the
reader's map: which report is the current authority for a topic, and which are
superseded or only useful as evidence.  Canonical summaries live in
`docs/project/`; this file does not restate them.

## Boot chain, loader and media

| topic | current authority | notes |
|---|---|---|
| OMAP4430 boot ROM | report 152 | read-only ROM dump analysis; the readable 48 KiB carries no secure-boot path |
| RIM second-stage loader / arm9000 | reports 150, 151, 173 | **173 corrects the code addresses in 150/151 by `-0x9000`**; data literals are correct |
| RPMB selector, factory/CRC vs product/ECDSA | reports 71, 89, 95, 173 | `SAFETY.md` §3 is the operational form |
| IFS slot, header, CRC recipe | reports 89, 103, 139, 155 | `BOOT-CHAIN.md` §2 and `SAFETY.md` §4 |
| `BOOTCTR_FCTRY` decrement and top-up | reports 116, 150, 154, 175 | `SAFETY.md` §2 |
| NV-record mount, `0x5B`, LED error codes | reports 153, 154 | trap 65/66 |
| Pre-OS `0xF7` reflash / containers | reports 98, 101, 139, 164 | trap 55; `bbusb.py` |

## Display

| topic | current authority | notes |
|---|---|---|
| Four-lane DSI and link rate | reports 118, 121 §11 | 118 §8.2 is corrected in place |
| Bridge programming | reports 125, 132 | 111 §8 item 4 is corrected in place; 121/122's "nothing programs it" was wrong |
| Display fix (bridge delivery, porches, TCON, core clock) | reports 131, 132, 133 | resolved; history in `docs/history/KNOWN-ISSUES-HISTORY.md` |
| TCON byte order | report 132 §8 | high-first; `111-vs-123-tcon-byte-order.md` |
| DISPC/DSI register oracle | reports 132, 133 | `tools/pbdsi` |

## RPMB and the monitor

| topic | current authority | notes |
|---|---|---|
| Keys and frame/block MACs | reports 143, 144, 156 | traps 60/61 |
| Monitor address view / seal ABI | reports 147, 148 | trap 62/63 |
| The monitor transform | report 175 | reproduced; supersedes 148's mismatch |
| Write incident and the lost spare | reports 149, 153 | `SAFETY.md` §6; never write RPMB as an agent |

## PlayBook OS image work (parked)

| topic | report |
|---|---|
| IFS imagefs writer, checksums, format | 168 |
| Shim boot and `/base` hook | 169, 170 |
| NRV2B compression / splice | 171, 172 |
| UFS/container and `autorun::0` | 170 |

The guides are in `docs/parked/`.

## Peripherals

| topic | current authority | notes |
|---|---|---|
| Peripheral inventory and bus map | report 176 | |
| I²C4 motion sensors | report 177 | BMA150/HMC5883L/MPU3050 |
| BQ27541 gauge, TMP103 | report 178 | |
| FAN5702 LED | report 179 | |
| CM3217 light sensor | reports 181, 182 | protocol + working driver |
| WL1283 / MMC5 | reports 180, 185–197, **216**–**238** | **238 is newest (session 53) and the BT core WORKS**: driven over UART2 as H4 it answers `HCI_Reset`, reports `hci ver 6 / lmp ver 6 / mfr 0x002b` (TI, BT 4.0) and **runs a full BR/EDR inquiry that finds a nearby device** (`f0:a3:b2:bb:d1:00`, class `0x011c00`), reproducibly.  **Loading the reference's `WL1283.bts` (315/315 commands, LMP subversion `0x2b0f` → `0x2b33`) is what makes `hci0` register with mgmt for the first time** — measured as `MGMT_OP_READ_INDEX_LIST` 0 → 1 controller, with the no-pack case as the negative control.  It works by fixing the **length** of the `LE_Read_Supported_States` (`0x201c`) reply (11 → 12 bytes).  **The remaining blocker is a stray byte**: an instrumented `h4_recv()` shows a **lone `0x30`** arriving ~200 ms after the last complete init reply, in 6 of 6 runs; `0x30` is not an H4 type, so the kernel correctly returns `-EILSEQ` (`Frame reassembly failed (-84)`) and `SET_POWERED` returns `0x05` (`-ETIMEDOUT`).  **Which side emits it is open** — it never appears from userspace.  Hardware flow control was tested and is **not** the cause (identical A/B on a clean boot; H4 never calls `hci_uart_set_flow_control()`) — that early claim is retracted (trap 250).  **LE is refused by the controller** (`0x0c` with a plausible payload, `Write_LE_Host_Support` = `0x11`, LE feature page zero), and the reference stack is BR/EDR-only so there is no LE sequence to copy.  Also **settles the BT-firmware contradiction** in favour of the reference's file (its header says `Quattro, 6.0.15 ROM`; `TIInit_*` says `BL6450, WL1271/3 2.0 ROM`), and **corrects report 237 §4**: `0x201c` is standard `HCI_LE_Read_Supported_States`, not a benign TI vendor op.  **237 (session 52)**: the three-session "silent BT" was the **UART2 CTS/RTS pads** (`0x4A100118`) left in safe mode (`0x011F011F`); the reference's idle value is `0x00000118`, and muxing them makes `HCI_Reset` return `04 0e 04 01 03 0c 00` at once.  The DTS now carries them and `pbhci --attach` creates `hci0`.  **236 (session 52): the WLAN ASSOCIATES** — the blocker was the **missing `ccm`/`ctr` crypto modules** (`ieee80211_key_alloc()` needs a `ccm(aes)` AEAD; without it the PTK install is `-ENOENT`, which wpa_supplicant reports as `WRONG_KEY`).  Also 236: `iw reg set <CC>` for a 5 GHz AP the world domain marks no-IR; the per-unit NVS shipped; and the USB gadget's MUSB HSDMA fixed (RX 6.3 / TX 4.1 MB/s).  **235 (session 51): the WLAN scans** — `5-sr` (`7.3.10.0.142`, the newest wl128x blob anywhere) boots but **panics on the first scan**; `4-sr` (`7.3.5.0.98`) scans normally (17–21 BSS, both bands), so the port lowers `wl12xx`'s SR floor to 7.3.5.0 and ships 4-sr.  **234: the board is exonerated** — stock TabletOS runs `ti0` UP/RUNNING and associated on vendor firmware `Rev 7.1.4.10.77`, with `IPC_EVENT_SCAN_COMPLETE` 6 s after boot.  **233: the firmware never completed a scan under Linux** — the driver accepts `iw dev wlan0 scan`, no scan-complete event arrives, the 30 s `WL1271_SCAN_TIMEOUT` fires and wlcore recovers; the per-unit NVS was located in NVRAM (468-byte record at raw `0x152cc`) and is not the cause; the client tooling (iw, wpa_supplicant, signed regdb) is shipped.  **232: the stability blocker** — the `-110` recovery loop was the chip's **ELP** sleep (FW-status read, CMD53 to `0x1efc0`, 84 bytes, byte mode), fixed with `sta_sleep_auth = WL1271_PSM_CAM`.  **230/231 are the session-50 half:** **230** is the enumeration root cause — the TWL6030 `CLK32KG` (32.768 kHz SLOW_CLK, physical `0xBC`–`0xBE`, module-relative `0x8C`) was OFF under Linux; enabling it makes the card enumerate (`0x0097:0x4076`).  **231** brings the WLAN up: the `ti,wl1283` child (`reg = <2>`), `cap-power-off-card`, and an `mmc-pwrseq-simple` that pulses `WL_EN` give `wlcore: firmware booted` and a live `wlan0`.  **229/228** (also session 50): the BT core is silent even with `BT_EN` driven, and the card answers at none of 400/300/200/100 kHz — both negative.  **227 is the measurement authority**: the "card is silent" measurement was itself flawed (`WL_IRQ` is not a liveness signal — the reference reads 0 too; `STAT` was never decoded; the bus state was never read at command time).  Corrected, it shows the bus correct at 400 kHz/1.8 V and **`CTO` only** — and that `CON=0x85` must not be copied (its `HR` bit suppresses the command and hangs).  **225**: the *clock-domain* modes differ — `CM_L3INIT_CLKSTCTRL` and `CM_L4PER_CLKSTCTRL` are `SW_WKUP` on TabletOS with `ti0` up and `HW_AUTO` under Linux, a difference no earlier comparison looked at (module clocks matched; domain modes did not).  It is a measurement, not yet a patch (trap 183).  **221–224** are the five one-binary RE passes (regulator resmgr, MMC block driver, `startup-playbook`, powerman/devpm, and the boot script): all negative, and 223 corrects the "MMC5 clock is set at boot" assumption — the reference *disables* it at `full` setup and lets the WLAN driver enable it.  **226** is the earlier boot-writers pass (its VMMC finding refuted against live silicon).  **220** (the trap-185 pad audit: three pads were driving GPMC alternate functions — a real defect, fixed, *not* the blocker).  **219** is the boot-path RE (`startup-playbook`'s 144 stores, `pmic_twl6030_cfg`, REGEN1/2 — its one new candidate is refuted by the live captures).  **218**: stock TabletOS with `ti0` up was captured and diffed against our Linux boot — no readable board state (pads, WL_EN/WL_IRQ, GPIO1/2 directions, `PRM 0x564`, all TWL rails) can explain the dead card, so the remaining classes are the unreadable MMCHS5 block and the chip-side pins.  Also fixes the way back (trap 226).  **217**: the reference driver brought the chip up live on the oracle and its whole MMCHS5 program (plus its WL_EN ordering) was replayed on Linux — both negative for the card, so the host program is excluded and the next evidence is a stock-TabletOS capture at `ti0`-up.  **216 is the BT half's RE**: `BT_EN` = gpio 55 = `GPIO2_23`, driven by `btp-btmgr`, held low while the WLAN works; also the `.bts` script's shape and the UART2 "force-RTS" devctl.  196 is the minimal 1917-truth negative; 190 the dossier |
| WLAN calibration / NVS | report 197 | QNX `hd5`; the reference takes its NVS from NVRAM (`-o nvram`, 468 B), not from a file (report 217) |
| Munin touch | reports 193, 194, 198 | bus, protocol, calibration + IRQ |
| WM8994 audio | reports 199, 200, 201, 203, 204, 205, 206, 209, 212, 213, **214**, **215** | **214 is current for the data path**: the AIF1 is on McBSP3, the card is two links (`mcbsp2 + wm8994-aif1` clocks, `mcbsp3 + wm8994-aif2` data), the `omap-mcbsp` channel-layout guard fixes the right channel, and the 440 Hz tone is audible in both ears at 48 kHz.  **215 is current for the speaker**: the board's speakers are on the codec's **line outputs** (PM3 `0x3cf0`, `R0x01e`, `R0x034/035`, `R0x038`), `spk` (GPIO2_12) is an **active-low** amplifier enable, and GPIO1_15 (the `audio_boost` enable) must be high; both speakers play.  206 = the transmit path (E1 clock roles, MCLK1 dead at the pin); 205 = the codec chain and driver-driven repair; 203 = the AIF1CLK write rule; 204 = the first live reference codec read; 209 = the power/CM negative and the ABE reference clock; 212 = the transport history (E5/E6/E9); 213 = the ABE question (a firmware data path). |
| ISL9519 charger | report 183 | |
| WDT2 / PMIC | reports 183, 184, 192 | |

## Tooling and repository

| topic | report / file |
|---|---|
| CFP info opcodes and action log | reports 161–166 |
| LDR_01 opcode map / `reset-osregion` | reports 163, 164 |
| Documentation audit | report 167 |
| **Minimal 1917 oracle image** (debloated reference, qconn on 9090) | **report 207** | `tools/build_minimal_qnx.py` |
| **Oracle causality: AUXCLK1 is the codec MCLK1** | **report 208** | live AUXCLK1 gate test, `docs/captures/wm8994-oracle-live-2026-11-12.txt` |
| **Power-domain/CM comparison (negative) + the ABE-refclk fix** | **report 209** | live captures both sides, the oracle causal test, three-image DPLL_ABE comparison, `docs/captures/wm8994-pm-cm-2026-11-12.txt` |
| **QNX audio chain RE — board pad database, codec-GPIO init** | **report 210** | `docs/reference/qnx-hwio/` (249-record pad table + resolver) |
| **Codec-GPIO init implemented in the machine driver** | **report 211** | negative for MCLK1; fixes the WM8994 probe; Linux/OMAP gpio-numbering trap |
| **E5/E6/E7-E9: the McBSP as clock provider, the data ball, and the ABE wall** | **report 212** | current for the audio transport and the fault: `CLKSM`/SRG-clock findings, 48 kHz, the data ball `abe_mcbsp1_dx`, the two-link build, and the reference's ABE-clocked McBSP; `docs/captures/wm8994-e5-master-srg-2026-11-13.txt` |
| Public snapshot / repository management | `docs/project/REPO-MANAGEMENT.md` |
| **hd3 + Nura + SGX540 scoping** (**239 is current for the next phase**) | **report 239** | hd3's live contents (QNX v1.2b MBR at `0x49000000`, QNX6 superblock at `0x49002000`); the loader's `0x1C` allow-list; why image size is a policy choice (`analyze_blocks` skips `0xFF`, not zeros); the **Nura** rename (2026-09-27) and the espresso OMAP4430 reference port; the SGX route through `pvrports` (`linux-openpvrsgx` + proprietary `sgx-ddk-um`).  Design decision: hd3 holds a **flasher-restorable** ext4 image |

## Superseded or read-with-care

* **90** and **92** carry a dated "superseded" banner at the top.
* **150/151** code addresses are `0x9000` high; report 173 is the correction.
* **118 §8.2** and **111 §8 item 4** are corrected inline.
* **231 §6's "remaining suspects" for the WL1283 `-110` are resolved by 232**:
  the firmware/chip match is unusable rather than negative (`wl128x-fw-4-sr.bin`
  is below the driver's `7.3.10.*.133` floor), the block size/byte mode is
  excluded (the same 84-byte byte-mode read succeeds), and the fault is the
  **ELP** path.  Read 231 for the bring-up, 232 for the stability fix.
* **212 §16's "the reference clocks its port through the ABE" is corrected by
  213**: the port is a codec-master consumer, the ABE serial port is a
  firmware data path, and the McBSP clocks are CM1_ABE/McBSP hardware.
* **212 §10's "the codec's DACDAT is `abe_mcbsp1_dx`" is corrected by 214**:
  it is `abe_mcbsp3_dx` (pad `0x108`); `abe_mcbsp1_dx` is the
  `AIF 4 → MCBSP1` route.  The working two-link path and the stereo fix are
  in report 214.
* The session-13 "partial live image" was withdrawn by report 129 §5.
* Old patch-size statements (42, 46, 47, 48 files) are historical; the current
  export is 54 files / 8,584 lines (`kernel/patches/README.md`).
