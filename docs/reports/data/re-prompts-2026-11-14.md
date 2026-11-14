# Prompts for the WL1283 boot-path RE, one binary per chat

Session 49.  Each block below is **self-contained**: paste one into a fresh chat
and it can finish without a turn cap.  They are deliberately scoped to a single
binary so 400+ tool calls is enough.

Shared context all of them assume (it is repeated inside each prompt, so no
cross-referencing is needed):

* A BlackBerry PlayBook (OMAP4430 ES2.2, Winchester rev 07) runs QNX 2.1.0.1917,
  where a TI WL1283 WLAN (SDIO on OMAP MMC5 `0x480D5000`) works.
* Under Linux the same chip never answers **any** SDIO command — silent from
  CMD0/CMD52 — although the pads, WL_EN, the rails and the controller program
  have all been compared (reports 217/218/220).
* So the question is what the reference's **boot** powers/clocks/resets that
  Linux does not.

When a chat finishes, save its report under `docs/reports/` as
`221-…` … `226-…` (adjust to whatever the next free number is) and tell me the
path; I will fold it in.

---

## 1. `startup-playbook` (the board init image)

```
Static, file-only reverse engineering. No bench, no USB, no network, no sudo,
and do not run any script under tools/. Allowed shell: readelf, objdump, nm,
strings, python3, grep, sed, wc, xxd, dd, objcopy. Work only inside
/home/psyden/SSDdrive/playbook-linux.

TARGET (one binary): the startup-playbook image inside the IFS — a raw ARM image
at local/work/1917/ifs1917.imagefs offset 0x908, size 0x44008, load base
0x80100000. Extract it to a scratch file first, then disassemble with
`objdump -D -b binary -m arm --adjust-vma=0x80100000` (if objdump lacks an ARM
backend, use python3 + capstone).

CONTEXT: a BlackBerry PlayBook (OMAP4430) runs QNX, where a TI WL1283 WLAN
(SDIO on OMAP MMC5, 0x480D5000) works. Under Linux the chip never answers any
SDIO command. The WLAN driver's own writes and every readable register have
been compared and are negative, so this job is about what the BOOT path does to
power/clock/reset.

READ FIRST, do not repeat: docs/reports/192-wl1283-startup-playbook-audit-2026-11-08.md
(it covers the GPIO table at 0x80122750 and the WLDR write) and
docs/reports/219-wl1283-boot-path-writes-2026-11-14.md §Q1 (a partial store
list). Your job is to be COMPLETE, and to correct them where they are wrong.

DELIVERABLE — a complete store inventory, produced mechanically: scan the whole
image for movw/movt immediate pairs, reconstruct every 32-bit constant, and for
each constant in a peripheral range report every instruction that consumes it
(ldr/str/strh/read-modify-write) with the VA and the value stored. Cover
0x4A000000-0x4A31FFFF (CM/PRM/SCRM/CONTROL/GPIO1/WDT/pads),
0x48000000-0x480DFFFF (GPIO2-6, MMCHS1-5, UART, I2C, MCSPI, GPMC) and
0x4A310000-0x4A31FFFF. For each store give address, value, function VA, and a
one-line meaning ONLY if hardware/swpu231ap.pdf.txt identifies the register
(cite the table); otherwise leave it blank.

ANSWER EXPLICITLY:
1. Does it write CM_L4PER_MMCSD5_CLKCTRL (0x4A009560) or any other CM2 L4PER
   module clock? Exact values.
2. Does it write ANY address in 0x4A307000-0x4A3072FF or 0x4A307400-0x4A3075FF
   (PRM L3INIT / L4PER, which contains RM_L4PER_MMCSD5_CONTEXT at 0x4A307564)?
   Exact values, or say plainly that it does not.
3. Does it write padconf 0x4A100050, 0x4A100058, 0x4A100074, 0x4A100078,
   0x4A10007A, 0x4A10007C, 0x4A100148-0x4A100152, or WKUP 0x4A31E058?
4. Does it touch GPIO1 (0x4A310000) or GPIO2 (0x48055000) direction or data
   registers? Which bits?
5. Does it perform any I2C transaction on I2C1 0x48070000, I2C2 0x48072000,
   I2C3 0x48060000 or I2C4 0x48350000? Slave, register, value.
6. Does it enable/disable any DPLL or write PRM VP/VC/SR
   (0x4A307000-0x4A3072FF, 0x4A306000-0x4A3061FF)?
7. Its GPIO/pad table: report 192 says the Winchester table is at image
   0x80122750 in 32-byte records. Dump it completely and decode every record
   (gpio id, direction, level, detect, rev, phase, signal/pad id).

Save the full disassembly to a scratch file so pages can be re-checked. Report
as compact tables, then the seven answers, then a short "what a minimal image
would skip" note. Every claim needs a VA.
```

---

## 2. `pmic_twl6030_cfg` (already run once — this is the verification pass)

```
Static, file-only reverse engineering. No bench, no USB, no network, no sudo,
no scripts under tools/. Allowed shell: readelf, objdump, nm, strings, python3,
grep, sed, wc, xxd, dd. Work only inside /home/psyden/SSDdrive/playbook-linux.

TARGET (one binary): local/work/1917/ifs/proc/boot/pmic_twl6030_cfg — a QNX 6.6
ARM executable the IFS boot script runs under the banner "Configure power
management chip".

CONTEXT: a BlackBerry PlayBook (OMAP4430) runs QNX, where a TI WL1283 WLAN
(SDIO on OMAP MMC5) works; under Linux the chip never answers any SDIO command.
This program is the one boot-time TWL6030 configurator Linux has no equivalent
for, so its exact behaviour matters. A previous pass (report 219 §3, and a
second pass) produced a 38-write list; treat both as unverified input.

DELIVERABLE:
1. Full write list in program order — bus, slave, register, value, and the call
   site VA for each. Find the I2C helper by its devctlv/devctl usage and
   enumerate ALL its call sites; that is the authoritative list. Mark every
   difference from report 219's list.
2. Control flow: which writes are unconditional and which are gated (confstr /
   board name / ES revision / any read that can fail), with the exact gate and
   the branch this unit takes (Winchester rev 07, OMAP4430 ES2.2).
3. Slave 0x38 and slave 0x49: identify them from their register maps using the
   local kernel sources (drivers/leds/leds-lm3532.c, drivers/mfd/twl-core.c,
   drivers/usb/phy/phy-twl6030-usb.c), list their writes, and say what they
   control.
4. Register semantics for every TWL6030 register written, established ONLY from
   local sources (local/kernel-tree/drivers/regulator/twl6030-regulator.c,
   include/linux/mfd/twl.h, drivers/mfd/twl-core.c, and any TWL text under
   hardware/). Mark unknown as unknown. Explain specifically what a write of
   0x21 means to base+2 using TWL6030_CFG_STATE_* and VREG_GRP/TRANS/STATE.
5. Does it write MMIO at all (any store not to the stack)? Confirm or refute,
   exhaustively.
6. Cross-check every value it writes against the LIVE captures in
   docs/captures/wlan-qnx-state-2026-11-14.txt and
   docs/captures/wlan-linux-state-2026-11-14.txt (the TWL block is in the
   Linux file as lines "TWLxx 0xYY"). For each register it writes, report what
   both OSes read later — that tells you which writes survive to runtime.
7. Verdict: is there ANY register/value here that could gate a 1.8 V SDIO rail
   or a 26 MHz clock on this board and is not reproduced by Linux's
   twl6030-regulator driver plus the board DTS? Rank them; say plainly if none.

Every claim needs a VA or a quoted line. Under 200 lines.
```

---

## 3. `pmic-winchester-twl6030` (the regulator resource manager)

```
Static, file-only reverse engineering. No bench, no USB, no network, no sudo,
no scripts under tools/. Allowed shell: readelf, objdump, nm, strings, python3,
grep, sed, wc, xxd, dd. Work only inside /home/psyden/SSDdrive/playbook-linux.

TARGET (one binary): local/work/1917/ifs/proc/boot/pmic-winchester-twl6030 — the
QNX TWL6030 regulator resource manager for the Winchester board.

CONTEXT: a BlackBerry PlayBook (OMAP4430) runs QNX, where a TI WL1283 WLAN
(SDIO on OMAP MMC5) works; under Linux the chip never answers any SDIO command,
and every readable register has been compared and is negative. This job is about
power. Unverified prior claims to check: report 219 §4 says the regulator table
is at file 0x8170 (19 records x 32 B) with bases vana 0x80, vaux1 0x84, vaux2
0x88, vaux3 0x8C, vcxio 0x90, vdac 0x94, vmmc 0x98, vpp 0x9C, vusb 0xA0,
vusim 0xA4, regen1 0xAD, regen2 0xB0, vrtc 0xC2, and that on DVT3+/MBID>=5 it
leaves REGEN1/2 at 00/01/00 (GRP/TRANS/STATE).

DELIVERABLE, in this order (write the file early, so a cut-off still helps):
1. The regulator table decoded COMPLETELY: every record with its name string,
   base, and every field you can identify. Say which records are
   Winchester-specific.
2. Every I2C write the binary performs, in order, with VA: bus, slave, register,
   value. Group by code path (init / enable / disable / group / state).
3. **Which code paths ENABLE a rail (write a STATE register with an "on"
   value), and what triggers each?** This is the question that matters.
4. REGEN1/REGEN2: prove or refute that the final state on this board is "off and
   detached from all groups"; identify the physical rail from local sources
   (include/linux/mfd/twl.h, drivers/clk/clk-twl.c,
   drivers/regulator/twl6030-regulator.c, docs/project/KNOWN-ISSUES.md trap
   135/§3.21, report 192 §7); mark inference as inference.
5. Every PPS object and devctl it exposes — that shows what other services can
   switch later, and by whom.
6. Does it touch GPIO (regen1/regen2 are described as GPIO-controlled on
   DVT3+)? Which pins, what values? Our DTS models regen1 as a gpio1 hog
   (GPIO1_2) — is that the same line?
7. Verdict: anything that could gate a 1.8 V SDIO radio's power here and is
   missing from Linux's twl6030 driver + board DTS. Say plainly if none.

Every claim needs a VA or a quoted string. Under 200 lines.
```

---

## 4. `devb-mmcsd-winchester` (the MMC block driver)

```
Static, file-only reverse engineering. No bench, no USB, no network, no sudo,
no scripts under tools/. Allowed shell: readelf, objdump, nm, strings, python3,
grep, sed, wc, xxd, dd. Work only inside /home/psyden/SSDdrive/playbook-linux.

TARGET (one binary): local/work/1917/ifs/proc/boot/devb-mmcsd-winchester — the
QNX block driver for the OMAP MMC controllers on the Winchester board.

CONTEXT: a BlackBerry PlayBook (OMAP4430) runs QNX, where a TI WL1283 WLAN
(SDIO on OMAP MMC5, 0x480D5000) works. Under Linux the chip never answers ANY
SDIO command, and Linux's own MMC5 program has already been replayed from the
reference WLAN driver (negative). This driver owns MMC1/MMC2 on QNX, not MMC5,
but it initialises the MMC subsystem and the interconnect/power domain MMC5 also
lives in — its DOMAIN-LEVEL writes are the question. Report 190 §3.3 claims it
sets SDBP/SDVS18, enables CM2_L3INIT MMC1/2 module clocks, clears PRM_L3INIT
LOSTCONTEXT, and writes no CM divider/DPLL and no regulator: verify from scratch
and be exhaustive.

DELIVERABLE, in this order (write the file early):
1. Every MMIO store: VA, address, value, grouped by target — CM2
   0x4A009000-0x4A0095FF (incl. 0x4A009560 MMCSD5, 0x4A009548 UART2), PRM
   0x4A307000-0x4A3072FF and 0x4A307300-0x4A3075FF, the MMCHS register files
   (0x4809C000, 0x480B4000, 0x480AD000, 0x480D1000, 0x480D5000), pads it muxes,
   GPIO banks.
2. Which MMCHS instances does it own and initialise? Does anything touch
   0x480D5000 (MMC5), even indirectly — e.g. a loop over instances that would
   include 5?
3. **The domain/LOSTCONTEXT question (most important):** exactly which PRM bits
   it clears, in which order, and the consequence for a module in the same
   domain that this driver does not own. Name the registers from
   hardware/swpu231ap.pdf.txt where possible.
4. Does it enable any regulator, clock output, pad group or I2C device that MMC5
   would also need (CONTROL_MMC1 0x4A100628, CONTROL_PBIASLITE 0x4A100600,
   VDDS timers, vmmc over I2C)? Slave/register/value for any I2C access.
5. Anything that looks like sequencing, delays, polls or retries applied to all
   instances that a single-instance driver (Linux's omap_hsmmc on MMC5 only)
   would not perform.
6. Verdict: anything here that could explain a chip silent under Linux's MMC5
   only. Say plainly if none.

Every claim needs a VA or a quoted string. Under 200 lines.
```

---

## 5. `powerman` + `devpm-omap4.so` (the dynamic power decisions)

```
Static, file-only reverse engineering. No bench, no USB, no network, no sudo,
no scripts under tools/. Allowed shell: readelf, objdump, nm, strings, python3,
grep, sed, wc, xxd, dd. Work only inside /home/psyden/SSDdrive/playbook-linux.

TARGETS: local/work/1917/base/bin/powerman and
local/work/1917/base/lib/dll/devpm-omap4.so (and devpm-omap4-4460.so only where
it differs). The board preset is
local/work/1917/base/etc/system/config/winchester.pm.

CONTEXT: a BlackBerry PlayBook (OMAP4430) runs QNX, where a TI WL1283 WLAN
(SDIO on OMAP MMC5) works; under Linux the chip never answers any SDIO command,
and every readable register is compared and negative. The reference's WLAN
driver asks powerman for resources (the analogous `audio_boost` request is what
solved the WM8994 speaker), so powerman is the remaining place where a DYNAMIC
power decision could differ. Read first, do not repeat:
docs/reports/187-wl1283-powerman-preset-2026-11-08.md and report 190 §3.3.

DELIVERABLE, in this order (write the file early):
1. The preset decoded COMPLETELY: every authority, edge, dependency group and
   initial state, with line numbers. Then: is there ANY edge or group whose
   action is a regulator, power domain, clock, GPIO or I2C device?
2. powerman's device model: how a device (not an authority) is registered and
   switched — the devctl/PPS interface, and exactly how a driver's
   `powman_alive("name")` maps to an action. List the resource names it knows.
3. devpm-omap4.so: every MMIO write and every I2C/regulator request with
   VA/address/value, grouped (PRCM voltage/DPLL/DVFS, power domains, OPP
   tables, GPIO wake tables). Report 190 says it has one powerman device
   (`iva_opp`), no MMC5 write and no WLAN rail, and that some mmap globals are
   stored but never read: verify, and be exhaustive about every write.
4. The OSWR wake table(s): dump them completely, list which pads/GPIOs are in
   them (report 190 says the WL_IRQ pad 0x07A / GPIO2_21 is included on
   Winchester2), and say what happens on OSWR entry/exit.
5. The WLAN authorities (`activity/wifi_driver/active|idle`, `air_needs_wifi`,
   `wlan_needs_DMA`, `WLAN_NEEDS_PREVENT_OSWR`): what do they actually do, and
   is there a path where a MISSING client (Linux has no powerman) leaves a rail
   or clock off?
6. Verdict: anything here that could hold the radio down, or that the reference
   does and Linux's runtime-PM/clock framework does not. Say plainly if none.

Every claim needs a VA or a quoted line. Under 200 lines.
```

---

## 6. `setup-core-inactive` + the IFS boot script `.script`

```
Static, file-only reverse engineering. No bench, no USB, no network, no sudo,
no scripts under tools/. Allowed shell: readelf, objdump, nm, strings, python3,
grep, sed, wc, xxd, dd. Work only inside /home/psyden/SSDdrive/playbook-linux.

TARGETS:
1. local/work/1917/ifs/proc/boot/setup-core-inactive — a QNX boot utility the
   IFS script runs as `setup-core-inactive full` before any driver.
2. local/work/1917/ifs/proc/boot/.script — the compiled IFS boot script
   (records are u16 length, u16 type, then payload; report 219 §Q2 partially
   decoded it).

CONTEXT: a BlackBerry PlayBook (OMAP4430) runs QNX, where a TI WL1283 WLAN
(SDIO on OMAP MMC5) works; under Linux the chip never answers any SDIO command,
and every readable register is compared and negative. This job is about the
EARLIEST boot-time power/clock programming, and about which boot steps a minimal
image skips.

PART A — setup-core-inactive (write this first):
1. What does the `full` argument select, and what does it do? Every PRM/CM write
   with VA, address and value: voltage processor/controller (VP/VC),
   SMARTREFLEX, DPLLs, OPP/AVS, power domains, PM_*_PWSTST, RM_*_CONTEXT. Find
   them mechanically from movw/movt constants.
2. Does it write anything in 0x4A307300-0x4A3075FF (PRM L3INIT / L4PER,
   containing RM_L4PER_MMCSD5_CONTEXT at 0x4A307564)? Does it touch CM2
   0x4A009560 (MMCSD5) or 0x4A009400-0x4A0095FF?
3. Does it change any voltage rail (TWL6030 at slave 0x48 over I2C, or PRM
   VC/VP)? Values.
4. Its other subcommands, if any, versus `full`.

PART B — .script:
5. A COMPLETE ordered transcript: every record, its type, the command it runs.
   Parse the record stream with python3 and print every string in order with its
   offset. This is the authoritative boot order.
6. For each step, mark whether an image containing only these services —
   `filesystem pps random pty net powerman phyctrl usbmgr rimusb` — would still
   run it. That is how the minimal oracle was built, and knowing what it skips
   is the point of the exercise.
7. Every place the script runs setup-core-inactive, pmic_twl6030_cfg,
   pmic-winchester-twl6030, pmic-twl6030-ctl, devb-mmcsd-winchester,
   splash_script, and where it first runs /base/scripts/startup.sh.

Every claim needs a VA or an offset. Under 220 lines.
```
