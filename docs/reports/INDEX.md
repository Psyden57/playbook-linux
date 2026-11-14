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
| WL1283 / MMC5 | reports 180, 185–197, **216**–**229** | **229/228 are newest (session 50)**: **228** runs the BT-core liveness probe properly (`BT_EN` = gpio 55 driven low→50 ms→high, pad `0x00030003`, `DATAOUT` bit 23 0→1, UART2 pads and `LSR = 0x60` verified) — the chip stays silent, but because the reference's *BT* half was never shown live this does **not** clear the power/TCXO class; **229** opens the identification ladder (`f_min` 400000 → 100000 makes `mmc_rescan` try 400/300/200/100 kHz) and every frequency ends in `CTO` only, so the card is **not** "slower than 160 µs" (100 kHz = 640 µs) — that hypothesis is dead.  **227 is the measurement authority**: the "card is silent" measurement was itself flawed (`WL_IRQ` is not a liveness signal — the reference reads 0 too; `STAT` was never decoded; the bus state was never read at command time).  Corrected, it shows the bus correct at 400 kHz/1.8 V and **`CTO` only** — and that `CON=0x85` must not be copied (its `HR` bit suppresses the command and hangs).  **225**: the *clock-domain* modes differ — `CM_L3INIT_CLKSTCTRL` and `CM_L4PER_CLKSTCTRL` are `SW_WKUP` on TabletOS with `ti0` up and `HW_AUTO` under Linux, a difference no earlier comparison looked at (module clocks matched; domain modes did not).  It is a measurement, not yet a patch (trap 183).  **221–224** are the five one-binary RE passes (regulator resmgr, MMC block driver, `startup-playbook`, powerman/devpm, and the boot script): all negative, and 223 corrects the "MMC5 clock is set at boot" assumption — the reference *disables* it at `full` setup and lets the WLAN driver enable it.  **226** is the earlier boot-writers pass (its VMMC finding refuted against live silicon).  **220** (the trap-185 pad audit: three pads were driving GPMC alternate functions — a real defect, fixed, *not* the blocker).  **219** is the boot-path RE (`startup-playbook`'s 144 stores, `pmic_twl6030_cfg`, REGEN1/2 — its one new candidate is refuted by the live captures).  **218**: stock TabletOS with `ti0` up was captured and diffed against our Linux boot — no readable board state (pads, WL_EN/WL_IRQ, GPIO1/2 directions, `PRM 0x564`, all TWL rails) can explain the dead card, so the remaining classes are the unreadable MMCHS5 block and the chip-side pins.  Also fixes the way back (trap 226).  **217**: the reference driver brought the chip up live on the oracle and its whole MMCHS5 program (plus its WL_EN ordering) was replayed on Linux — both negative for the card, so the host program is excluded and the next evidence is a stock-TabletOS capture at `ti0`-up.  **216 is current for the BT half**: `BT_EN` = gpio 55 = `GPIO2_23`, driven by `btp-btmgr`, held low while the WLAN works.  196 is the minimal 1917-truth negative; 190 the dossier |
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

## Superseded or read-with-care

* **90** and **92** carry a dated "superseded" banner at the top.
* **150/151** code addresses are `0x9000` high; report 173 is the correction.
* **118 §8.2** and **111 §8 item 4** are corrected inline.
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
