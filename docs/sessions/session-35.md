# Session 35 (absolute) — S1/S2/S3/H1: `startup-playbook` is the source of the QNX-only GPIOs; every bench lead is negative

> Numbering: absolute `35`; the reports call this Linux-era **session 29**
> (`35 - 6`).  See `NUMBERING.md`.

Date / span: 2026-11-08.
Unit state at the end: **our Linux 7.3.3 known-good image `0xE36C679A` in
region 0**, region 1 the 7.3.3 charger rootfs; `BOOTCTR_FCTRY = 0x2f`; pack
Full (4.152 V, 100 %); no door holder; `/dev/ttyACM0` mode 666.

## Goal of this session

Work the session-35 list (reports 190/191): **S1** audit the QNX
`startup-playbook` binary; **S2** read the TWL6030 `0xBC`–`0xBE` and the other
differences on both OSes; **S3** bisect the QNX-only GPIO outputs on the live
1917; **H1** build/boot the exact QNX SDIO sequence.  "Done" meant naming the
source of the QNX-only GPIOs, reconciling the TWL difference, and either
answering CMD5 or exhausting the software surface.

## Confirmed (measured, read in a file, or seen on the bench)

* **`startup-playbook` is the source of the QNX-only GPIO outputs.**  The
  Winchester board GPIO table (image `0x80122750`, 75 × 32-byte records) marks
  `GPIO1` bits 0/4/15/18/28/31 and `GPIO2` bits 5/7 as **outputs driven low**,
  `gpio 54` (WL_EN) output-low, `gpio 53` (WL_IRQ) input/falling/pull.
  [report 192 §4; trap 135]
* **The binary itself:** `startup.*` = `0x44008` bytes at imagefs offset
  `0x908`, sha256 `bc7d3a414fb1c9f67bdaf71349792af2ee22f1beba7c8691ae8daa8650a4ecda`,
  raw ARM, load base `0x80100000`; Ghidra 12.1.4 reports 461 procedures.
  [extracted to `/tmp/s35/startup-playbook.bin`; report 192 §1]
* **The table mechanism:** `FUN_8010cb9c` walks 32-byte GPIO records (`+0` gpio
  id, `+4` direction `1`=output, `+8` level, `+0xc` detect, `+0x14/+0x15` rev,
  `+0x16` phase) and writes `GPIO_OE`/`SET|CLEARDATAOUT`/detect/debounce;
  `FUN_8010ccf4` does the pads (20-byte records, `PADCONF` `0x4A100000`/WKUP
  `+0x21E000`).  The board descriptor is built at runtime.  [report 192 §3]
* **WLAN syspage node:** the binary carries the option strings
  `WLAN_EN_GPIO=54,WLAN_INT_GPIO=53` (Winchester), `151/152`, `151/163` (other
  boards) and `BT_EN_GPIO=171/55`; the live node is `54/53`.  [report 192 §2;
  report 189 §8]
* **WDT2:** startup writes `WLDR = 0xFFE2B400` through `WSPR` stop/start,
  polling `WWPS`; the same 58.6 s policy as decision §38.  [report 192 §5]
* **S2, both OSes (`i2cget -f -y 0 0x48` on Linux; `pbi2c dump` on QNX):**
  `0x8C`/`0x8D`/`0x8E` = `CFG_GRP`/`CFG_TRANS`/`CFG_STATE` = QNX `01/05/05`,
  Linux `07/05/55`.  `CFG_TRANS` (`0x8D`) is identical; `CFG_STATE` (`0x8E`)
  has APP bits[3:2] = `01` = **ON in both** (the differing bits are CON/MOD);
  `CFG_GRP` differs.  [captures `docs/captures/twl6030-{linux,qnx}-2026-11-08.txt`;
  report 192 §7]
* **`0xB0`–`0xB2` is `REGEN2`:** the QNX PMIC driver's own name/base table gives
  `regen2` base `0xB0` (`regen1` `0xAD`).  QNX `00/01/00` (GPIO-controlled on
  DVT3+), Linux `07/05/55`.  [file `local/work/ifs1917-tree/proc/boot/pmic-winchester-twl6030`,
  table at 0x8170–0x83f0; report 192 §7]
* **`0xBC`–`0xBE` is not `CLK32KG`:** Linux `0xBC=0x07, 0xBD=0x01, 0xBE=0x00`;
  QNX `0x07/0x05/0x55`.  It is not a regulator in the QNX table and no QNX WLAN
  layer touches it.  [captures; report 192 §7]
* **S3, live 1917 (one boot): all eight lines negative.**  Baseline
  `GPIO1_OE=0x6ffb1faa`, `GPIO2_OE=0xce3ecd5f`, `GPIO1_DATAOUT=0x00006044`,
  `GPIO2_DATAIN=0x08201018` (bit21 = 1), pad `0x4A31E058=0x000b0003`, `ti0`
  UP/RUNNING, `wifi_power::on`, 30+ BSSIDs.  Each OE bit set to input stuck
  (`6ffb1faa→effb1faa/6ffb1fba/6ffb9faa/6fff1faa/7ffb1faa`;
  `ce3ecd5f→ce3ecd7f/ce3ecddf`) and each flip left `ti0` RUNNING, `DATAIN`
  bit21 = 1 and `wifi_power::on`; each restored.  [report 192 §8; trap 136]
* **`gpio31` detail:** pad `0x4A31E058` mux 3; with the pad input buffer enabled
  (`0x000b0003 → 0x000b0103`) `GPIO1_DATAIN` bit31 = 0; OE `0x6ffb1faa →
  0xeffb1faa` stayed at +1 s/+2 s (no re-drive); the pin stayed low on release
  (external pull-down); pad restored.  [report 192 §8]
* **H1, image `0x29784F48` (`0x91A2D4`, sha256
  `64f705856487543192cbfe96b33bda1cd12f0d326a9e1de8313f3505c9aa076c`): negative.**
  Trace: `sdio_reset skipped` → `go_idle` → `CLK init override 400000 ->
  24000000`, `CLKD=2 actual=24000000` → `PBD: CON=00000600 HCTL=00000b00
  SYSCTL=000e0087 CAPA=04e10080 STAT=00000000 PSTATE=01f40000
  SYSSTATUS=00000001` → `op=0 err=0` → `attach_sdio` → `op=5 arg=00ffff00
  flags=000002e1 err=-110 resp0=00000000` → `attach_sdio no`; no CMD8/CMD55/CMD1.
  After power-off: `SYSCTL=000e0007`.  [report 192 §9; trap 137;
  `local/logs/boot-H1-0x29784F48.log`]
* **H1 source hunks** (reverted after the test): `MMC_CAP2_NO_SDIO_RESET
  (1<<30)`; skip `sdio_reset()` when set; `480d5000` gets
  `NO_SDIO_RESET|NO_SD|NO_MMC` (never `NO_SDIO`); DTS `bootargs` add
  `omap_hsmmc.init_hz=24000000 omap_hsmmc.sdio_ocr_quirk=1`.  [report 192 §9;
  `grep NO_SDIO_RESET` on the tree now returns nothing]
* **Boot budget this session:** before the bench `0x31`; after the S3/QNX boot
  the TabletOS top-up wrote `0x32`; the H1 boot and the restore boot took it to
  `0x2f`.  Both Linux boots logged `pbrpmb: topup: ... 2f000000`,
  `0x2f >= threshold 0x1e, no write`.  [pbcon `dmesg`, this session]
* **Unit restored:** `zlib.crc32(image[0:-4]) = 0xE36C679A` for
  `local/images/kernel-0xE36C679A-wdt58s.img` (0x91A748, sha256 `bdbaf5fc…`);
  the unit boots Linux 7.3.3 and reports pack 4.152 V / 100 %.  [this session]

## Inferred (reasoning from evidence, not directly measured)

* **The software leads do not explain the WL1283.**  S3 (all eight GPIOs) and
  H1 (exact sequence) are both negative, and `CLK32KG`'s APP state is ON in
  both.  The remaining difference is the chip's non-exposed VBAT/TCXO/reset pins
  or OMAP HSMMC sampling.  *Strength: strong for the software leads; the
  chip-side cause is inferred.*  [report 192 §7/§8/§9]
* **`gpio 3`/`19` are re-touched by a later QNX driver.**  They appear as
  outputs in the Winchester table but read as inputs in the live OE; they are
  outside the WL1283 subset.  *Strength: moderate.*  [report 192 §4a]
* **The `0xBC` resource is a non-regulator control resource.**  It is not in the
  QNX regulator table, but its name is not decoded.  *Strength: moderate.*
  [report 192 §7]

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The `ATTILA_L4PER_TA_GPIO2`/`GPIO1`/`GPIO2` strings are inside
  `startup-playbook`."**  False: they are a later imagefs entry at `0x47c…`;
  the startup's own WLAN strings are the option strings.  The disproving
  measurement was `strings -t x` on the extracted `0x44008`-byte range vs the
  whole imagefs.  [report 192 §1]
* **"The board descriptor/table is statically resolvable."**  False: the
  descriptor is built at runtime and there is no `{table,count}` pointer; the
  tables were found by scanning for the 32-byte record signature and by content
  (the `54/53` entries).  [report 192 §3/§4a]
* **"`0x8C`–`0x8E` are not in the session-29 12-difference list, so they are
  identical on both OSes."**  False: the both-OS read shows `0x8C` and `0x8E`
  differ; only `0x8D` (`CFG_TRANS`) is byte-identical.  The conclusion survives
  because `0x8E`'s APP bits are `01` in both.  [report 192 §7; this session]
* **"`CLK32KG` is at TWL `0xBC` (the FreeBSD mapping)."**  False: base `0x8C`
  (`clk-twl.c`; the 2014 LKML patch gives `CFG_STATE = 0x8E`; the QNX read
  confirms).  [report 192 §7]
* **"The QNX-only GPIO outputs might be the missing WL1283 enable."**  False:
  releasing all eight on the live 1917 left the chip up.  The disproving
  measurement is the S3 flip set.  [report 192 §8; trap 136]
* **"`gpio31` may be kept high/re-driven by something."**  False: the OE write
  to input stuck at +1/+2 s, the pad input buffer showed the pin low, and the
  pin stayed low on release (external pull-down).  [report 192 §8]
* **"The host sequence (CMD52 first, CMD8, 400 kHz) is the blocker."**  False:
  H1 sent CMD0 → CMD5 only at 24 MHz with arg `0x00FFFF00` and the card still
  timed out.  [report 192 §9; trap 137]
* **"The 3.3 V `vwl1283` `vmmc` selects 3.3 V signalling."**  False:
  `HCTL = 0x00000b00` = SDVS18|SDBP; the 3.3 V is OCR only.  [report 192 §9]
* **"`scan_results_timestamp` is a live indicator."**  False: it stayed
  `132264` across repeated samples; `ti0` flags and `GPIO2_DATAIN` bit21 were
  used instead.  [this session]
* **"`kmsg` is a shell command on the region-1 root."**  False: it is a
  pbconsole command; the BusyBox shell has no `kmsg`, so `dmesg` was used.
  [this session]

## Unknown / open questions

* **What is the `0xBC`–`0xBE` resource?**  QNX `07/05/55`, Linux `07/01/00`; not
  a regulator in the QNX table, not `CLK32KG`.  *Settle with:* TI `SWCU084`
  (TWL6030 register map) or the QNX source.  Does not reopen the 32 kHz lead.
* **What are the other eight differing registers?**  `0x00/0x01/0x20/0x2D/0xE4/
  0xF4/0xFA` and the `0xE5/0xE6` context; the "interrupt/status/config" reading is
  an inference.  *Settle with:* the TWL register map / a decode.
* **Chip-side VBAT/TCXO/SLOW_CLK and MMC5_CLK/CMD/DAT0.**  *Settle with:* a board
  view or logic analyser; decision table in `HANDOFF.md` §0c.
* **The pad (ball) of each QNX-only GPIO other than `gpio31`.**  *Settle with:*
  the `hwio-config-winchester.so` `gpio_config`/`pad_config` decode (report 189
  §2) or a board view.
* **Does `CLK32KG`'s CON/MOD state difference matter in sleep?**  The APP field
  is ON in both; the CON/MOD bits differ.  *Settle with:* a sleep-entry test or
  the datasheet state tables.

## Superseded / uncertain

* **"`CLK32KG` is identical on both OSes" (my own first S2 commit `bb00859`).**
  Superseded by the both-OS read in `f63bfcc`/`2b5a96f`: `CFG_TRANS` is
  identical but `CFG_GRP` and the `CFG_STATE` CON/MOD bits differ; APP is ON in
  both.  The older evidence (a Linux-only read plus the session-29 difference
  list) was incomplete.
* **"`0xBD`/`0xBE` are the `CLK32KG` trans/state" (report 191 §1.2, from the
  external review).**  Superseded: `CLK32KG` is base `0x8C`; `0xB0`–`0xB2` is
  REGEN2 and `0xBC`–`0xBE` is an unnamed resource.  [report 192 §7]
* **Report 180 §4's "CFG_STATE identical; CFG_TRANS 0x55 vs 0x05".**  The
  labels are swapped (corrected inline and as `KNOWN-ISSUES` §3.21): the
  `0x05`-in-both is `CFG_TRANS` (`0x8D`); the `0x55`-vs-`0x05` is `CFG_STATE`
  (`0x8E`), whose APP field is `01` in both.
* **The earlier session-35 state "pack 4.144 V, counter 0x31".**  Superseded by
  the end-of-session measurement: pack 4.152 V, `BOOTCTR_FCTRY = 0x2f`.
* **"H1 is the next test."**  Done this session, negative (trap 137); the next
  work is chip-side.
* **"The S3 bisect starts with `gpio31`."**  Run and negative; all eight lines
  are closed.

## Artefacts produced

* **Report 192** — `docs/reports/192-wl1283-startup-playbook-audit-2026-11-08.md`
  (S1 audit, S2 both-OS reads, S3 bench, H1 result).
* **Captures** — `docs/captures/twl6030-linux-2026-11-08.txt`,
  `docs/captures/twl6030-qnx-2026-11-08.txt`.
* **Contradiction** — `docs/contradictions/twl-clk32kg-on-both-vs-0xbd-be-difference.md`
  resolved (both-OS data); `docs/contradictions/README.md` updated (six open).
* **Traps** — 135 (startup-playbook / CLK32KG), 136 (S3 negative + gpio31
  verification), 137 (H1 negative); correction 3.21; "Fixed in session 35
  (documentation)".
* **Decisions** — §42 (startup-playbook is the source of the QNX-only GPIOs),
  §43 (H1 negative; software surface exhausted).
* **Canonical docs** — STATE (session-35 state, unit table, blocker, next),
  HANDOFF (session-36 kickoff + §0c decision table), ROADMAP (item 9 step 4),
  DEVELOPMENT §7a (REA/Ghidra method).
* **Images** — H1 test `local/images/kernel-0x29784F48-h1-nosdioreset.img`
  (`0x91A2D4`, CRC `0x29784F48`, sha256 `64f70585…`); the known-good
  `local/images/kernel-0xE36C679A-wdt58s.img` (`0x91A748`, CRC re-verified
  `0xE36C679A`, sha256 `bdbaf5fc…`) restored to region 0; region-1 rootfs
  `ba275807…` unchanged.
* **Logs** — `local/logs/boot-H1-0x29784F48.log`; the session's pbcon dmesg
  captures are quoted in report 192/STATE.
* **No net source change** — the four H1 hunks were reverted; the 7.3.3 patch
  is unchanged (sha256 `e7b7fc2e…`) and the tree is back at the `0xE36C679A`
  baseline.  QNX bench tools rebuilt locally (`/tmp/pbmem3`, `/tmp/pbmemw`,
  `tools/pbi2c` pushed) and `/tmp` is volatile.

## Next

Only the chip-side measurement remains (VBAT/TCXO/SLOW_CLK, MMC5_CLK/CMD/DAT0;
`HANDOFF.md` §0c).  If the WL1283 is formally stopped, item 9 step 5 (WM8994
audio + CY8CTMA3 touch, report 176) follows.  Do not repeat traps 116–137.
