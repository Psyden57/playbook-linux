# 191 — External review of the WL1283 dossier: response3 / response4, verification, and the next-session plan

**Session 34 (absolute), Linux-era 28.  2026-11-08.**
Status: **external opinions recorded and statically verified; no new bench.**
The operator supplied two independent reviews of report 190
(`response3.txt`, 36 lines; `response4.txt`, 329 lines).  This report records
what they claim, which claims are confirmed against the tree, and the ordered
next-session plan.  The dossier's inline errors are corrected in report 190
(§3.2 pad numbering, §7.2 GPIO, §11 H1/H2, §13).

The reviews are opinions, not measurements.  Every claim marked **confirmed**
below was checked against the source or a live value already in the tree;
**unresolved** means it needs the next session's read.

---

## 0. The two reviews in one paragraph

**response3** accepts the core diagnosis (card silent from the first command)
but rejects "software surface exhausted".  It found three leads the dossier
dismissed or never audited: (1) the GPIO OE banks are **not** identical —
QNX drives several GPIO1/GPIO2 outputs that Linux leaves as inputs, including
GPIO1_31 on the `gpio_wk31` pad; (2) TWL registers `0xBD`/`0xBE` may be the
`CLK32KG` resource TRANS/STATE (the 32 kHz clock to the WLAN); (3) the QNX
**`startup`/BSP** binary was never audited, and it is the usual place for
board GPIO/PMIC init.  It proposes a cheap QNX-only bisect of each QNX-only
output, then a startup/TWL decode, then H1, then the external measurements.

**response4** agrees H1 is the priority and adds an important correction to
the dossier's H2: `vmmc` (OCR) and `vqmmc` (signalling) are different, and
MMC5 has no `ti,dual-volt`, so it always uses **SDVS18** — the 3.3 V dummy
`vmmc` does **not** select 3.3 V signalling, and should not be blindly
reverted.  It also notes that `MMC_CAP2_NO_SDIO_RESET` is not upstream/current,
so H1 needs the custom quirk; it flags the 12 TWL differences, the GPIO1
difference and the `0x4A31E05C` pad as static work; and it cautions that a low
WL_IRQ can be asserted or floating, not independent proof of "unpowered".

Both reviews agree on the order: static QNX/PMIC/GPIO work and the exact QNX
SDIO sequence first; external measurement only after those.

---

## 1. Verified claims

### 1.1 GPIO OE banks are not identical — **CONFIRMED, real lead**

The dossier's §7.2 presented GPIO1/GPIO2 as "matched".  That is wrong.  Using
`OE 0 = output, 1 = input` (the OE words are from report 185 §1; the bit deltas
were computed this session):

| bank | QNX `OE` | Linux `OE` | QNX output → Linux input | Linux output → QNX input |
|---|---|---|---|---|
| GPIO1 | `0x6FFB1FAA` | `0xFFFF9FBB` | **bits 0, 4, 15, 18, 28, 31** | none |
| GPIO2 | `0xCE3ECD5F` | `0xCE2ECDFF` | **bits 5, 7** | **bit 20** (the Linux hog) |

`GPIO2` bit 21 (WL_IRQ) is **1 (input) in both**; the dossier's "bit 21 = 0 in
both" was wrong and is corrected.

**Why bit 31 matters.**  GPIO1 bit 31 is `gpio_wk31` on WKUP pad
`0x4A31E058` (mux 3).  QNX samples that pad with the **input buffer off**
(`0x000B0003`), so `GPIO2_DATAIN`/`DATAIN` cannot show the pin level; the
dossier's "reads low either way" is meaningless here.  QNX has the line as an
**output** (OE 0) driving low; Linux leaves it as an **input** (OE 1), with the
pad input buffer off and pulls disabled.  This is a genuine state difference on
a WKUP ball that the QNX BSP configures.  It could be a TCXO enable, a reset,
a load-switch enable, or another board control; the fact that it was
eliminated as a **FREF request input** (trap 117) does not eliminate it as a
plain GPIO output.

The other QNX-only outputs (GPIO1 0/4/15/18/28; GPIO2 5/7) are candidates too.
response3's cheap test is the right one: on QNX, with `ti0` up, flip one OE bit
to input at a time (GPIO1 `0x4A310134`, GPIO2 `0x48055134`), check the scan
status and `DATAIN` bit 21, and cycle `ifconfig ti0 down/up` to restore.  The
line that kills the chip is the answer.

**Next session:** map each QNX-only output to its pad and to the
`hwio-config-winchester.so` `gpio_config` entry, then run the QNX bisect.

### 1.2 TWL `0xBD`/`0xBE` may be the CLK32KG resource — **UNRESOLVED, high priority**

The TWL6030 datasheet (`hardware/twl6030.pdf`) confirms `CLK32KG` is a
32 kHz digital **software-controlled** clock output, listed as a clock
resource in the APP/CON/MOD groups.  Linux's `include/linux/mfd/twl.h` has
`TWL6030_REG_CLK32KG 48` (a resource ID, not the register address).  The
session-29 comparison recorded `0xBD` and `0xBE` among the 12 non-rail
differences; the external review's FreeBSD-based claim is that the
`CLK32KG` group/transition/state registers are at `0xBC`/`0xBD`/`0xBE`.
That is plausible but **not verified in this tree** — the actual values were
not recorded in the reports.  **Recorded as a contradiction:**
`docs/contradictions/twl-clk32kg-on-both-vs-0xbd-be-difference.md`.

**Why it matters.**  If QNX configures the `CLK32KG` resource differently
(e.g. always-on vs off in sleep, or a different group), the 32.768 kHz slow
clock to the WL1283 could be absent or gated under Linux even though the
"regulator state" comparison looked identical.  The Motorola 148603 guide
lists SLOW_CLK as a required 32.768 kHz input.

**Next session:** read `0xBC`–`0xBE` on both OSes (read-only; the QNX PMIC
driver owns the bus — use the PMIC service or the same read-only `pbi2c` route
as before, and heed trap 103), decode the TWL6030 resource map from the
datasheet, and compare the group/transition/state bits.  Also decode the other
9 differences (`0x00`, `0x01`, `0x20`, `0x2d`, `0xb0`, `0xb1`, `0xb2`, `0xe4`,
`0xf4`, `0xfa`) for GPIO/oscillator/eVT functions.

### 1.3 The QNX `startup`/BSP binary was never audited — **CONFIRMED, real lead**

The 1917 boot image contains a **`startup-playbook`** binary, not the IFS
tree:

* `local/work/stock.list`: `startup.*` at image offset `0x908`, size
  `0x44008`; `startup_vaddr=0x80100cc0`; checksum `0x90310cb3`.
* `strings` on `local/work/ifs1917.imagefs`: `startup-playbook`,
  `/builds/.../hardware/startup/boards/playbook/hwi_omap4430.c`, `GPIO1`,
  `GPIO2`, `GPIO2_DATA...`, `ATTILA_L4PER_TA_GPIO2`.

It is the board's boot/startup code (GPIO/pad/PMIC init) and it never runs
under our chainloader.  This is the most likely source of the QNX-only GPIO
outputs in §1.1, and it may also set a board rail or the `CLK32KG` resource.
It was not in the report-189 inventory (which covered the WLAN driver,
powerman, devpm, devb-mmcsd, hwio, hw_info and the `.ini`, but not the
startup/BSP).

**Next session:** extract the `startup-playbook` image (offset `0x908`,
`0x44008` bytes from `local/work/ifs1917.imagefs` or the sealed IFS container),
disassemble it, and grep for GPIO1/GPIO2 register writes, TWL I²C writes,
`CLK32KG`, and pad addresses.  Map the writes to the QNX-only outputs.

### 1.4 The 3.3 V `vmmc` does **not** select SDVS33 on MMC5 — **CONFIRMED correction**

`ti,dual-volt` appears in `local/kernel-tree/arch/arm/boot/dts/ti/omap/omap4-l4.dtsi`
on **`mmc1`** (`0x4809c000`, line 2164), not on `mmc5`.  `&mmc5` has no such
property.  In `omap_hsmmc.c` the non-dual-volt branch is unconditional:

```c
if (host->pdata->controller_flags & OMAP_HSMMC_SUPPORTS_DUAL_VOLT) {
        if (host->power_mode != MMC_POWER_OFF &&
            (1 << ios->vdd) <= MMC_VDD_23_24)
                hctl = SDVS18;
        else
                hctl = SDVS30;
        capa = VS30 | VS18;
} else {
        hctl = SDVS18;          /* MMC5 takes this branch */
        capa = VS18;
}
```

So MMC5 uses **1.8 V signalling** regardless of the `vwl1283` dummy regulator's
voltage.  The 3.3 V `vmmc` is an **OCR advertisement** (the chip's VBAT is
3.3 V), not the signalling voltage.  The dossier's H2 ("3.3 V can select
SDVS33; revert to 1.8 V") was wrong and is corrected in report 190 §11/§13.

**Do instead:** record `HCTL = 0x00000B00` (SDVS18 | SDBP) and `CAPA` VS18
before/after CMD5; if a 1.8 V I/O model is wanted, add a `vqmmc-supply`, not a
change to the OCR `vmmc`.  The known-good 1.8 V vs current 3.3 V DTB difference
is still a tree-state difference, but it is not the cause and not a
3.3 V-signalling hazard.

### 1.5 `MMC_CAP2_NO_SDIO_RESET` is not upstream/current — **CONFIRMED correction**

The current `include/linux/mmc/host.h` does **not** define
`MMC_CAP2_NO_SDIO_RESET`; it was a session-33 custom bit (image `0xAC98BCAD`)
and was reverted.  H1 must add the quirk at the `sdio_reset()` call site:

```c
if (!(host->caps2 & MMC_CAP2_NO_SDIO) &&
    !(host->caps2 & MMC_CAP2_NO_SDIO_RESET))
        sdio_reset(host);
```

**Do not set `MMC_CAP2_NO_SDIO`** — that would skip the CMD5 probe entirely.
Use `MMC_CAP2_NO_SDIO_RESET | MMC_CAP2_NO_SD` (optionally `NO_MMC`).

### 1.6 Pad numbering — **CONFIRMED correction**

`0x078` is **gpio_52**, `0x07A` is **gpio_53** (WL_IRQ).  The dossier's §3.2
said "`0x078` (gpio_53)"; corrected.  The driver writes the 32-bit word at
`0x078`, which covers both pads.

### 1.7 WL_IRQ active-low nuance — **CONFIRMED caution**

WL_IRQ is active-low.  A low reading can mean **asserted** or **floating /
pulled low**, not necessarily "unpowered".  The dossier's "the chip never
drives its IRQ" should be read as "the accessible line is low"; the stronger
evidence is the QNX/Linux `DATAIN` difference and the rightmost front pad
following `ti0` down/up.  Do not use the low level alone as a power-good
signal.

### 1.8 Public repository state — **stale in response4**

response4 noted that the public `STATE.md` still described session 33.  That
was true when it reviewed the public snapshot (before the session-34 public
push).  The public repo was updated to session 34 at commit **`e35534e`**
(after the review).  No action needed.

---

## 2. What the reviews did not change

* The QNX-side software inventory (report 189) is complete for the files it
  covered; the startup/BSP is the missing piece, not a correction to the
  existing findings.
* The FREF request input (trap 117), FREF2 output (trap 118), init clock
  (trap 119), WL_EN pulse (traps 124/130), 200 ms settle (trap 128) and
  CMD52-only skip (trap 129) remain closed.  H1 is the *complete* sequence,
  not a repeat.
* The QNX `winchester` powerman preset, `devpm-omap4.so`,
  `devb-mmcsd-winchester` and `hw_info_pps` findings stand.
* The chip still works under QNX and is silent under Linux; the remaining
  difference is chip-side or host-sampling, but the GPIO/startup/TWL leads
  above are cheaper and must be closed first.

---

## 3. Next-session plan (ordered)

**0. Read** report 190 (dossier, corrected) and this report; do not repeat
traps 116–131.

**1. Static — QNX `startup-playbook`/BSP.**
Extract `startup-playbook` (`local/work/ifs1917.imagefs`, offset `0x908`,
`0x44008` bytes) and disassemble.  Grep for GPIO1/GPIO2 register writes, TWL
I²C writes, `CLK32KG`, and pad addresses.  The goal: find which QNX-only GPIO
outputs (GPIO1 0/4/15/18/28/31, GPIO2 5/7) and which TWL resource it sets.
Map the GPIOs to `hwio-config-winchester.so`'s `gpio_config` entries and pads.

**2. Static — TWL `0xBC`–`0xBE` (CLK32KG) and the 12 differences.**
Decode the TWL6030 resource map from `hardware/twl6030.pdf`; read `0xBC`–`0xBE`
and the other 9 differences on QNX and Linux; compare group/transition/state
bits.  Confirm whether `CLK32KG` is actually on in both and whether the
group/transition difference matters.  Read-only; heed trap 103 (the QNX PMIC
driver owns the bus).

**3. QNX bench — bisect the QNX-only GPIO outputs (response3's test).**
With `ti0` up, use a tiny `mmap_device_io` program to flip one OE bit to input
at a time (GPIO1 `0x4A310134`, GPIO2 `0x48055134`), then check:
`ifconfig ti0`, `GPIO2_DATAIN` bit 21, `/pps/services/wifi/status` scan
results.  Restore with `ifconfig ti0 down/up` and/or the original OE value.
The line that kills the chip is the answer.  One variable at a time; the
operator's call; do not leave a line driven wrong.

**4. Linux — H1, exact QNX sequence (one region-0 boot).**
Add the custom `NO_SDIO_RESET` quirk; set
`MMC_CAP2_NO_SDIO_RESET | MMC_CAP2_NO_SD | MMC_CAP2_NO_MMC` for `480d5000`;
`omap_hsmmc.init_hz=24000000`; `omap_hsmmc.sdio_ocr_quirk=1`.  Do **not** set
`MMC_CAP2_NO_SDIO`.  Record `HCTL`/`CAPA`/`SYSCTL`/`PSTATE`/`STAT` immediately
before and after CMD5, and the raw R4 response.  The current 3.3 V OCR window
is fine (response4); record `HCTL = 0x00000B00` to prove SDVS18.  If CMD5
answers, stop and report the response before interpreting any OCR/voltage
failure.

**5. If H1 is negative — MMC5 write-order comparison.**
Trace every MMC5 register write in `omap_hsmmc` and compare the order/timing to
QNX's `sdio_core_pm` (report 180 §5): `SYSCONFIG`, `HCTL` SDVS18, `CAPA` VS18,
`HCTL` SDBP, `ISE`, `IE`, `STAT`, `CON`, `HCTL` DTW, `SYSCTL` CLKD=2/DTO=14.

**6. Only then — external measurements.**
VBAT/VIO/26 MHz TCXO/32.768 kHz SLOW_CLK/WL_EN/WL_IRQ at the chip; current draw
on VBAT when WL_EN rises; a 1.8 V logic analyser on MMC5_CLK/CMD/DAT0.  This
is the decisive evidence if the software leads are all negative.

**Optional follow-on:** if a specific board line is identified, model the MMC5
power sequence with `mmc-pwrseq-simple` (`reset-gpios`, an `ext_clock` if the
tree exposes CLK32KG, and delays) rather than the boot-time WL_EN hog/pulse.
That orders clock/reset/CMD0 and avoids the session-32 pulse's USB regression.

---

## 4. Artefacts and sources

* Operator-supplied reviews: `response3.txt` (36 lines), `response4.txt`
  (329 lines) — external opinions, recorded here and verified as above.
* Verified against:
  * `local/kernel-tree/drivers/mmc/host/omap_hsmmc.c` lines 719–729, 1614
    (SDVS logic), 1904–1905 (`ti,dual-volt` property).
  * `local/kernel-tree/arch/arm/boot/dts/ti/omap/omap4-l4.dtsi` line 2164
    (`ti,dual-volt` on `mmc1`).
  * `local/kernel-tree/include/linux/mmc/host.h` (no
    `MMC_CAP2_NO_SDIO_RESET`), `drivers/mmc/core/core.c` (`sdio_reset` site).
  * `local/work/stock.list` (startup offset/size/checksum),
    `local/work/ifs1917.imagefs` (`startup-playbook`, `hwi_omap4430.c`,
    `GPIO1`, `GPIO2`).
  * `hardware/twl6030.pdf` (CLK32KG is a software-controlled clock resource;
    `include/linux/mfd/twl.h` `TWL6030_REG_CLK32KG 48`).
  * `docs/reports/180-wl1283-sdio-2026-11-06.md`,
    `docs/reports/189-wl1283-qnx-side-re-2026-11-08.md`,
    `docs/reports/190-wl1283-dossier-2026-11-08.md`.
* This report corrects report 190 §3.2 (pad numbering), §7.2 (GPIO bit 21 and
  the QNX-only outputs), §7.3 (CLK32KG caveat), §11 H1 (custom capability),
  §11 H2 (SDVS/vmmc) and §13 (why the 3.3 V/1.8 V difference matters).
