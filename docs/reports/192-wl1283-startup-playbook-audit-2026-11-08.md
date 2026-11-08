# 192 — QNX `startup-playbook` audit: it sets the QNX-only GPIO outputs

**Session 35 (absolute), Linux-era 29.  2026-11-08.**
Status: **static RE only; no bench.**  This is S1 of the session-35 work list
(reports 190/191).  It answers the open question HANDOFF §1.4 / report 191 §1.3
left: *what does the unaudited QNX `startup-playbook` binary configure?*

---

## 0. Headline

`startup-playbook` is the **source of the QNX-only GPIO outputs** that report 191
found (`GPIO1` bits 0/4/15/18/28/31 and `GPIO2` bits 5/7).  It applies a board
GPIO table that marks exactly those lines as **outputs**, plus `gpio 54` (WL_EN)
output-low and `gpio 53` (WL_IRQ) input/falling-with-pull.  It also:

* builds the syspage **`WLAN` hwinfo node** from per-board option strings —
  Winchester `WLAN_EN_GPIO=54,WLAN_INT_GPIO=53` (the live node), and
  `151/152`, `151/163` for other board branches;
* reconfigures **WDT2** to `WLDR = 0xFFE2B400` (58.6 s) via the `WSPR`
  stop/start sequence — independent corroboration of `DECISIONS.md` §38;
* runs the decoded rail-voltage path through the OMAP **PRM voltage controller**
  (`0x4A30_0BF8` / `0x4A30_0BA0`); it also has an I²C1 driver, but no TWL
  `CLK32KG`/`0xBC`–`0xBE` access was found in it.

It contains **no evidence of a `CLK32KG` write**; the one decoded voltage-setting
path is the OMAP PRM voltage controller.  Both-OS reads then show **CLK32KG's
`CFG_STATE` (`0x8E`) has APP = ON in both** (`0x05` vs `0x55`; the differing
bits are CON/MOD), while `CFG_GRP` (`0x8C`) differs and `CFG_TRANS` (`0x8D`) is
identical; the `0xBC`–`0xBE` difference is a *different* resource — **the 32 kHz
/ `CLK32KG` lead is closed for the active clock.**
**S3 was run on the live 1917: all eight QNX-only GPIOs (GPIO1 0/4/15/18/28/31,
GPIO2 5/7) are negative** — releasing each leaves `ti0` RUNNING and `DATAIN`
bit21 = 1, so none is the WL1283 enable/reset/power (§8).  **H1 was then built
and booted and is also negative**: with CMD0 → CMD5 only, 24 MHz and CMD5 arg
`0x00FFFF00`, the card still times out (`err=-110`), so the host sequence is not
the difference either (§9).  The software surface is now exhausted; the
remaining evidence is chip-side (VBAT/TCXO/reset) or OMAP HSMMC sampling.

---

## 1. Extraction and identity

`local/work/ifs1917.imagefs` (`f546769a…`) is the uncompressed 1917 imagefs.
`stock.list` gives the startup records:

```
808  100    Startup-header flags1=0xd flags2=0 startup_vaddr=0x80100cc0
908  44008  startup.*
```

The `startup.*` file is `0x44008` bytes at imagefs offset `0x908`; extracted:

```
sha256  bc7d3a414fb1c9f67bdaf71349792af2ee22f1beba7c8691ae8daa8650a4ecda
size    0x44008  (278 536)
```

It is **raw ARM**, not ELF (the `\x7fELF` at `0x2cef0` is the text `elf_map: 4K`).
It loads at **`0x80100000`** (the `.data` reference to the code resolves under
`--adjust-vma=0x80100000`; `startup_vaddr = 0x80100cc0` is a function address in
the image).  Ghidra 12.1.4 (REA raw-binary import, `ARM:LE:32:v7`) analyses
**461 procedures / 484 strings**.

The strings are the strong fingerprint:

```
2b35c  WLAN_EN_GPIO=151,WLAN_INT_GPIO=152
2b380  WLAN_EN_GPIO=151,WLAN_INT_GPIO=163
2b3a4  WLAN_EN_GPIO=54,WLAN_INT_GPIO=53
2b3d0  BT_EN_GPIO=171
2b3e0  BT_EN_GPIO=55
2b3c8  WLAN     2b3f0 BT
```

(The `ATTILA_L4PER_TA_GPIO2`/`GPIO1`/`GPIO2` strings report 191 quoted are at
imagefs `0x47c…`, i.e. a **later imagefs entry, not `startup-playbook`**.  The
startup's own only WLAN strings are the option strings above.)

Note the option strings are the `devnp-ti1283.so` option names.  "Winchester"
(ES2.2) uses **54/53**; the other two pairs are the Colt/London board branches.

---

## 2. The WLAN syspage node builder

The board-info builder (Ghidra `FUN_801037f0`, the code around `0x801020A0`)
picks the option string by board identity and writes the syspage `WLAN` item.  This is the
same node `tools/qnx-hwi.c` dumped live in report 189 §8
(`WLAN_EN_GPIO=54,WLAN_INT_GPIO=53`, empty `location`).  **Startup publishes the
pin numbers; it does not name a rail, clock or second enable** — matching report
189 and trap 131.

---

## 3. The board GPIO table (the mechanism)

Two routines implement the board tables:

* **`FUN_8010ccf4(rev, phase, pad_table, count)`** — **pad** config.  Walks
  20-byte records and writes `PADCONF` at `0x4A100000` (CORE) or
  `0x4A100000 + 0x21E000` (WKUP).  Record: `+0` pad id, `+4` AF byte, `+6/+7`
  rev range, `+8` phase, `+16` flags (the flag bits map to pad attributes).
* **`FUN_8010cb9c(rev, phase, gpio_table, count)`** — **GPIO** config.  Walks
  **32-byte** records; for each matching `(rev, phase)` it programs one GPIO
  line:
  * `+0`  gpio id (`bank*32 + line`)
  * `+4`  direction: **1 = output, 0 = input** (other values = leave)
  * `+8`  output level (`0` → `CLEARDATAOUT`, nonzero → `SETDATAOUT`)
  * `+0xc` edge detect (1/3 rising, 2/3 falling)
  * `+0x11` pull/enable, `+0x12/+0x13` debounce enable/time
  * `+0x14/+0x15` rev range, `+0x16` phase
  It writes `GPIO_OE` (`+0x134`), `GPIO_DATAIN`-adjacent uses, `SET/CLEARDATAOUT`
  (`+0x194`/`+0x190`), `RISING/FALLINGDETECT` (`+0x148`/`+0x14C`) and
  `DEBOUNCENABLE` (`+0x150`).

The board descriptor that holds the table pointers is **built at runtime**, so
there is no static `{table,count}` pointer in the image; the tables were
identified by scanning for the confirmed 32-byte record signature and by their
content.

---

## 4. The Winchester GPIO table and the QNX-only outputs

Scanning the whole image for runs of valid 32-byte records finds the board
tables.  The Winchester table (contains `gpio 54`/`53`) is at **`0x80122750`**,
**75 records**, ending `0x801230B0`; similar Winchester variants sit at
`0x80127250` (66) and `0x80128d64` (43).  The other boards' tables
(`0x801243b4`/`0x80124474`) carry `gpio 151/152/163` instead.

The entries that matter (all `rev 0–15`, phase 1):

| gpio | line | direction | level | meaning / note |
|---|---|---|---|---|
| 0  | GPIO1_0  | **output** | low | QNX-only |
| 4  | GPIO1_4  | **output** | low | QNX-only |
| 15 | GPIO1_15 | **output** | low | QNX-only |
| 18 | GPIO1_18 | **output** | low | QNX-only |
| 28 | GPIO1_28 | **output** | low | QNX-only |
| 31 | GPIO1_31 | **output** | low | **`gpio_wk31`, WKUP pad `0x4A31E058` mux 3** |
| 37 | GPIO2_5  | **output** | low | QNX-only |
| 39 | GPIO2_7  | **output** | low | QNX-only |
| 53 | GPIO2_21 | input  | —   | **WL_IRQ**, falling, pull |
| 54 | GPIO2_22 | **output** | **low** | **WL_EN** (driver raises it later) |
| 55 | GPIO2_23 | output | low | BT_EN |

This is a **direct match** to report 191's computed QNX-only OE bits
(`QNX output → Linux input`: GPIO1 0/4/15/18/28/31; GPIO2 5/7).  The live QNX
`GPIO1_OE = 0x6FFB1FAA` / `GPIO2_OE = 0xCE3ECD5F` are exactly those lines as
outputs.  **Conclusion: `startup-playbook` is the source; Linux's boot never runs
it, so those lines stay inputs/floating.**

The table also has a few outputs Linux already matches (GPIO1 2/6/13/14 and
GPIO2 8/9/12/13/14/16/17/…), and a second table may re-touch a line; the audit's
point is the QNX-only subset, which is unambiguous.

**Most interesting for the WL1283:** `gpio 31` is `gpio_wk31` on the WKUP ball
`0x4A31E058` (mux 3, input buffer off in the live QNX read).  QNX drives it
**low**; Linux leaves it as an input.  If that ball is a TCXO/load-switch/reset
control on the board, the float-vs-driven-low difference is a real candidate —
exactly report 191 §1.1's lead, now confirmed as a startup action rather than a
FREF side effect.

### 4a. Caveats

* The GPIO tables are selected/copied at runtime; the identification above is by
  record content, the `gpio 54`/`53` presence (Winchester) and the live QNX OE
  match — not by a static descriptor xref.
* `gpio 3`/`19` also appear as outputs in the table but read as inputs in the
  live QNX capture; a later QNX driver probably re-touches them.  They are not
  in the QNX-only WL1283 subset, so this does not affect the conclusion.
* Levels are the table's initial values; a driver may change a line later
  (WL_EN is the documented case: startup low → `devnp-ti1283` high).

---

## 5. WDT2

`FUN_801036a4`–`FUN_801037dc` writes `0x4A314000` (WDT2):

```
WSPR = 0xAAAA  (stop, poll WWPS 0x34)
WLDR = 0xFFE2B400            (58.6 s)
WCRR = ~0
WSPR = 0xBBBB / 0x4444       (start)
```

This is the reference value `DECISIONS.md` §38 already uses on Linux;
independent confirmation from the reference's own startup.

---

## 6. TWL / `CLK32KG`

* Startup has an **I²C1** driver (`0x48070000`; `i2c_omap_init/reset/
  set_reg_addr_len/write_reg/write`) and a `"unable to set voltage"` error
  string, but the voltage path resolved to the OMAP **PRM voltage controller**
  (`FUN_80107748` touches `0x4A30_0BF8`/`0x4A30_0BA0`), not direct TWL register
  writes.
* **No `CLK32KG` / `0xBC`–`0xBE` write was found.**  The only `0xbc` immediate
  is an MMIO byte offset (`0x80113614`, `[device+0xbc]`), not a TWL register.
* The QNX PMIC path is the `pmic-winchester-twl6030` resmgr +
  `pmic-twl6030_winchester2.cfg`.  The cfg only issues regulator
  `set_state`/`set_trans`/`set_groups`; the resmgr's strings are all
  regulator-state/trans (`twl6030_read/write_state`, `_trans`), **no
  `CLK32KG`** and no clock resource.

So startup is **not** where the QNX `CLK32KG` state comes from.

---

## 7. S2 (TWL `0xBC`–`0xBE` and the other differences) — answered

**Static facts.**

* TWL6030 datasheet (local `hardware/twl6030.pdf`, `SWCS045C`): `CLK32KG` is a
  **software-controlled clock resource** in groups APP/CON/MOD (Table 11), and
  it is **on from the start-up sequence** (`TWL6030B107` figure: no enable
  delay; it is not one of the late-enabled resources).  It is the 32.768 kHz
  output the WL1283's `SLOW_CLK` wants.
* The regulator resources use the same three-register pattern the review
  attributes to `0xBC`–`0xBE`: `base` = group, `base+1` = transition,
  `base+2` = state (mainline `twl6030-regulator.c`: VANA `0x50`, VAUX1 `0x54`,
  VAUX2 `0x58`, VAUX3 `0x5C`, VCXIO `0x60`, VDAC `0x64`, VMMC `0x68`, VPP
  `0x6C`, VUSB `0x70`, VUSIM `0x74`).  `0xBC`/`BD`/`BE` is therefore a
  group/transition/state triple of *some* resource, but **not `CLK32KG`** (see
  the resolved bullet below); the datasheet text does not carry the address
  table to name it.
* **ANSWERED (session 35, both OSes): the `0xBC`–`0xBE` difference is NOT
  `CLK32KG`, and `CLK32KG` is enabled in APP in both.**  The kernel's own
  `drivers/clk/clk-twl.c` places `clk32kg` at base **`0x8C`** with
  `VREG_GRP 0`, `VREG_TRANS 1`, `VREG_STATE 2`; the 2014 LKML TWL6030 clock
  patch independently gives `CLK32KG_CFG_STATE = 0x8E`.  Read on both OSes
  (`i2cget -f -y 0 0x48` on Linux; `pbi2c dump` on the live 1917):

  | reg | field | QNX | Linux | note |
  |---|---|---|---|---|
  | `0x8C` | `CFG_GRP` | `0x01` | `0x07` | group differs (APP only vs APP\|CON\|MOD) |
  | `0x8D` | `CFG_TRANS` | `0x05` | `0x05` | **identical** |
  | `0x8E` | `CFG_STATE` | `0x05` | `0x55` | APP bits[3:2] = `01` = **ON in both**; CON/MOD bits differ |

  `TWL6030_CFG_STATE_APP(v) = (v & 0x0C) >> 2`: `0x05 → 1` and `0x55 → 1`, so
  the **APP state is ON in both**; the full `CFG_STATE` byte is not identical
  (CON/MOD bits differ).  Report 180's phrasing ("CFG_STATE identical; CFG_TRANS
  `0x55` vs `0x05`") mislabeled the two bytes: the `0x05`-in-both register is
  `CFG_TRANS` (`0x8D`), and the `0x55`-vs-`0x05` register is `CFG_STATE`
  (`0x8E`).  Corrected statement: **APP state identical/ON; `CFG_GRP` differs;
  `CFG_TRANS` identical; `CFG_STATE` CON/MOD bits differ.**  The running clock is
  enabled in APP in both; the CON/MOD difference can only affect sleep states.
  The FreeBSD "CLK32KG at `0xBC`" claim (report 191 §1.2) is **wrong**.  (Raw
  captures `docs/captures/twl6030-{linux,qnx}-2026-11-08.txt`.)

* **`0xB0`–`0xB2` is `REGEN2`** (the QNX PMIC driver's own regulator table gives
  `regen2` base `0xB0`; `regen1` is `0xAD`).  QNX `00/01/00` (TWL register
  unassigned; on DVT3+ REGEN1/2 are GPIO-controlled — the `winchester2` cfg
  comment), Linux `07/05/55` (TWL register on).  REGEN1/2 are the **display /
  panel** rails (report 187), not WLAN.

* **`0xBC`–`0xBE` is an unidentified non-regulator control resource**: QNX
  `07/05/55` (on), Linux `07/01/00` (off).  It is not a regulator in the QNX
  driver table and not `CLK32KG`; the QNX WLAN driver/powerman do not name or
  touch it (report 189).  Its identity still needs the TWL6030 register map
  (`SWCU084`, not public).

* **Other measured differences** (QNX → Linux): `0x00` `0x19`→`0x52`,
  `0x01` `0x50`→`0x10`, `0x20` `0xC0`→`0xE0`, `0x2D` `0x02`→`0x82`,
  `0xE4` `0x07`→`0x07`, `0xF4` `0x7F`→`0xFF`, `0xFA` `0x00`→`0x01`.  These are
  interrupt/status/config registers, not rails or clocks (this is an
  *inference*, not a decoded identity).

**S2 answers the WLAN question:** `CLK32KG` is APP-ON in both; the remaining TWL
differences are the display REGEN2 path, one unnamed control resource, and the
eight other registers whose decode is still an inference.  The `0xBC` name and
the exact decode of those eight remain open; none reopens the 32 kHz lead.

---

## 8. S3 (QNX-only GPIO bisect) — **DONE, all eight negative**

**Bench (session 35, one 1917 boot).**  The 1917 IFS was flashed to region 0,
`ti0` came up `UP,BROADCAST,RUNNING,OACTIVE`, and `memdump3`/`memw32` were
pushed to `/tmp`.  Baseline: `GPIO1_OE=0x6ffb1faa`, `GPIO2_OE=0xce3ecd5f`,
`GPIO1_DATAOUT=0x00006044`, `GPIO2_DATAIN=0x08201018` (bit21 = **1**; report
185's value `0x08221018` differs only in bit17, so the IRQ observation is
unchanged), pad `0x4A31E058 = 0x000b0003`, `wifi_power::on`, 30+ BSSIDs in
`/pps/services/wifi/status`.

**`gpio31` (`gpio_wk31`, WKUP `0x4A31E058`, mux 3) — verified in detail.**
With the pad's input buffer temporarily enabled (`0x000b0103`), `GPIO1_DATAIN`
bit31 read **0** — QNX drives it low.  Setting `GPIO1_OE` bit31 to input
(`0x6ffb1faa → 0xeffb1faa`) stuck at +1 s and +2 s (**no re-drive**); the pin
still read **0** with the OE released (external pull-down / board hold), and the
chip was unchanged (`DATAIN` bit21 = 1, `ti0` RUNNING, `wifi_power::on`).  The
pad was restored (`0x000b0003`) and the OE word written back.

**The other seven lines** (GPIO1 bits 0/4/15/18/28, GPIO2 bits 5/7) were flipped
one at a time with the same check.  In every case the OE bit stuck
(`6ffb1faa→6ffb1fba`, `→6ffb9faa`, `→6fff1faa`, `→7ffb1faa`;
`ce3ecd5f→ce3ecd7f`, `→ce3ecddf`), `GPIO2_DATAIN` bit21 stayed 1, `ti0` stayed
RUNNING and `wifi_power::on`; each was restored (bit cleared + `ifconfig ti0
down/up`).  **Result: none of the eight QNX-only outputs is the WL1283
enable/reset/power.**  The OE difference is a real board-state difference but it
is not the missing variable; do not repeat this bisect.  (Trap 136.)

For a Linux-side equivalent, the correct model remains `mmc-pwrseq-simple`
(`reset-gpios`/`ext_clock`/delays) on `&mmc5`, not a boot-time `gpio-hog` —
report 191 §3's optional follow-on.

---

## 9. H1 (CMD0 → CMD5 only) — **DONE, negative**

**Built and booted (session 35).**  Image **`0x29784F48`** (0x91A2D4 bytes,
sha256 `64f70585…`, `local/images/kernel-0x29784F48-h1-nosdioreset.img`); the
four source hunks (reverted after the test, so the tree/patch stay at
`0xE36C679A`):

1. `include/linux/mmc/host.h`: `#define MMC_CAP2_NO_SDIO_RESET (1 << 30)`.
2. `drivers/mmc/core/core.c` (`mmc_rescan_try_freq`): skip `sdio_reset()` when
   `MMC_CAP2_NO_SDIO_RESET` is set (`if (!(caps2 & NO_SDIO) && !(caps2 &
   NO_SDIO_RESET)) … else pr_info("sdio_reset skipped")`).
3. `drivers/mmc/host/omap_hsmmc.c` (`480d5000` only):
   `mmc->caps2 |= MMC_CAP2_NO_SDIO_RESET | MMC_CAP2_NO_SD | MMC_CAP2_NO_MMC;`
   (**not** `MMC_CAP2_NO_SDIO`), plus the existing `sdio_ocr_quirk`.
4. Board DTS `bootargs`: `omap_hsmmc.init_hz=24000000
   omap_hsmmc.sdio_ocr_quirk=1`.

**Measured trace** (`local/logs/boot-H1-0x29784F48.log`), mmc4:

```
PBC: mmc4 try 400000Hz ...
PBC: mmc4 sdio_reset skipped
PBC: mmc4 go_idle
PBCLK: mmc4 init override 400000 -> 24000000Hz
PBCLK: mmc4 CLKD=2 actual=24000000Hz
PBD: mmc4 CON=00000600 HCTL=00000b00 SYSCTL=000e0087 CAPA=04e10080 \
     STAT=00000000 PSTATE=01f40000 SYSSTATUS=00000001
PBCMD: mmc4 op=0 arg=00000000 flags=000000c0 err=0 resp0=00000000
PBC: mmc4 attach_sdio
PBCMD: mmc4 op=5 arg=00ffff00 flags=000002e1 err=-110 resp0=00000000
PBC: sdio probe_ocr=00ffff00 err=-110 ocr=00000000
PBC: mmc4 attach_sdio no
PBD: mmc4 CON=00000600 HCTL=00000b00 SYSCTL=000e0007 CAPA=04e10080 \
     STAT=00000000 PSTATE=01f40000 SYSSTATUS=00000001
mmc4: Failed to initialize a non-removable card
```

**Result: negative.**  The host now sends **exactly** the reference's sequence —
CMD0 → CMD5, no CMD52, no CMD8, at **24 MHz** (`CLKD=2`), CMD5 arg
`0x00FFFF00` — and the card still returns nothing (`err=-110`, `resp0=0`; there
is no R4 to record).  **`HCTL = 0x00000b00`** is SDVS18 (0xA00) | SDBP (0x100),
confirming H2: MMC5 uses 1.8 V signalling and the 3.3 V `vmmc` is an OCR
advertisement only.  `CAPA = 0x04e10080` (VS18/30/33 capable).

This closes the last host-sequence hypothesis.  The remaining difference is at the
chip's non-exposed power/clock/reset pins or in OMAP HSMMC sampling; **the
software surface is now exhausted.**  The H1 test image was replaced by
`0xE36C679A` on media and the source hunks reverted.  (Trap 137.)

---

## 10. Sources

* `local/work/ifs1917.imagefs` (`f546769a…`); extracted
  `/tmp/s35/startup-playbook.bin`
  (`bc7d3a41…`, `0x44008` @ imagefs `0x908`).
* Ghidra 12.1.4 via REA: `binary_overview` (461 procedures), `analyze_function`/
  `procedure_pseudo_code` for `0x8010cb9c` (GPIO), `0x8010ccf4` (pad),
  `0x80100e24`/`0x80101440` (board-descriptor callers), `0x80107748` (PRM VC).
* Board GPIO tables: `0x80122750` (75 records, Winchester),
  `0x80127250` (66), `0x80128d64` (43), `0x801243b4`/`0x80124474`
  (151/152/163 boards).
* `local/work/ifs1917-tree/etc/system/config/pmic-twl6030_winchester2.cfg`,
  `.../pmic-twl6030.cfg`; `.../proc/boot/pmic-winchester-twl6030`,
  `pmic-twl6030-ctl`, `pmic_twl6030_cfg` strings.
* `local/kernel-tree/drivers/regulator/twl6030-regulator.c` (resource bases),
  `local/kernel-tree/include/linux/mfd/twl.h` (`TWL6030_REG_CLK32KG 48`).
* `hardware/twl6030.pdf` (`SWCS045C`) — CLK32KG resource table/boot sequence.
* **Bench:** the 1917 IFS (region 0), the door (root SSH), `tools/memdump3.c` →
  `/tmp/pbmem3`, `tools/memw32.c` → `/tmp/pbmemw`, `tools/pbi2c` (`dump`); the
  live `GPIO1_OE`/`GPIO2_OE`/`DATAIN`/WKUP-pad reads.  Raw TWL captures:
  `docs/captures/twl6030-{linux,qnx}-2026-11-08.txt`.
* **H1 image/log:** `local/images/kernel-0x29784F48-h1-nosdioreset.img`
  (`0x91A2D4`, sha256 `64f70585…`) and `local/logs/boot-H1-0x29784F48.log`.
* Reports 180, 185, 188, 189, 190, 191; session-29 note.

## 11. Limitations / what is not claimed

* The GPIO/pad tables are selected at runtime; this is a content + live-OE
  identification, not a static descriptor xref.
* The exact pad (ball) for each QNX-only GPIO other than `gpio31` (WKUP
  `0x4A31E058`) is **not** mapped here; the `hwio-config-winchester.so`
  `gpio_config`/`pad_config` decode (report 189 §2) has the signal-id mapping if
  a named ball is wanted.
* The `0xBC`–`0xBE` resource is measured but **not named** (needs `SWCU084`).
  Its QNX-on/Linux-off state is recorded; it is not `CLK32KG` and no QNX WLAN
  layer touches it.
* S3 released each line but did not measure the ball with a scope; H1 matched the
  host sequence but still saw no response.  The chip-side VBAT/TCXO/MMC5-signal
  measurements are still owed.
