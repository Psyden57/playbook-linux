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
| WL1283 / MMC5 | reports 180, 185–197 | **software surface exhausted; report 196 is the minimal 1917-truth negative** |
| WLAN calibration / NVS | report 197 | QNX `hd5` |
| Munin touch | reports 193, 194, 198 | bus, protocol, calibration + IRQ |
| WM8994 audio | reports 199, 200, 201, 203, 204, 205, 206, 209, 212, 213, **214** | **206 is current for the transmit path**: E1 clock roles, the transmit path proven in consumer mode, MCLK1 dead at the pin; 205 = the codec chain solved and driver-driven, the master-mode transmit stall isolated; 203 = the AIF1CLK write rule; 204 = the first live reference codec read (its "MCLK1 never clocks" claim is corrected by 205 §4b).  209 is the fault localisation (power/CM negative; ABE reference clock fixed).  212 is the transport history (E5 — McBSP bit/frame provider, `CLKSM`/SRG-clock findings, 48 kHz; E6 — the earlier, later-corrected data-ball reading; E9 — the two-link build).  213 is current for the ABE question (the serial port is a firmware data path; the McBSP clocks are mainline's).  **214 is current for the working audio**: the WM8994's AIF1 is on **McBSP3** (the oracle's routing log and live registers), the card is two links (`mcbsp2 + wm8994-aif1` clocks, `mcbsp3 + wm8994-aif2` data), and the `omap-mcbsp` channel-layout guard fixes the right channel; the 440 Hz tone is audible in both ears at 48 kHz. |
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
  export is 54 files / 8,440 lines (`kernel/patches/README.md`).
