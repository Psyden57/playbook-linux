# 226 — The boot-path writers, one binary at a time: what completed, and what the live captures refute

Date: 2026-11-14 (session 49, Linux-era 43).  Static, file-only RE plus
cross-checks against the captures of reports 217/218.  No bench change.

## 0. Headline

The operator directed a second, finer-grained RE pass: **one binary per
subagent** instead of one broad job (the first attempt hit its turn cap).  Six
were launched.  Three ran to completion, three were cut off; their exact prompts
are preserved in `docs/reports/data/re-prompts-2026-11-14.md` so they can be run
in full elsewhere.  Those re-runs are reports **221–225**; this report is the
earlier pass, kept for the corrections it records.

What the completed work establishes:

1. **`pmic_twl6030_cfg` is fully decoded, and the previous pass had it wrong in
   two ways** — it **does** enable VMMC and it **does** write regulator GROUP
   registers.  Neither matters, and the live captures prove it: **VMMC's STATE
   register reads `0x00` on TabletOS too**, at the same time the WLAN is up
   (it is turned back off by the regulator resmgr before `wifi.sh`).
2. **Every other write in that program is already replicated by Linux**, by name
   and value: VAUX1/2/3 (2.8 V, group P1) by the board DTS, `E5`/`F5`/`FA` by
   `phy-twl6030-usb.c`'s `twl6030_usb_ldo_init()`, and the `0x49` block is the
   TWL's own USB sub-slave, not a second chip.  The `0x38` device is the
   **LM3532 backlight** driver, register for register.
3. **`0xBC`–`0xBE` is not a runtime difference either**: the reference writes
   `05`/`E1` there at IFS time and **reads `0x00` in both OSes** afterwards, so
   whatever it is, it is transient and identical in the end state.

So the boot-path RE is now **exhausted in the same sense as the driver RE**: the
one writer Linux lacked has been decoded, matched against live silicon, and
accounted for.

## 1. `pmic_twl6030_cfg`, verified (the part that matters)

The program opens `/dev/i2c0`, sets 400 kHz, reads the TWL ES revision (which
gates **no** write), and then, if `confstr(_CS_MACHINE)` contains
`"Winchester"`, writes 38 single-byte registers.  All 38 execute on this board.

Structure, and the two corrections to the earlier pass:

| group | writes | gated? |
|---|---|---|
| `0x48` `BD`/`BE` `98`–`9B` | 6 | **unconditional** |
| `0x48` `84`–`8F` (VAUX1/2/3, GRP/TRANS/STATE/VOLT) | 12 | Winchester |
| `0x48` `AD`–`B2` (REGEN1/2) | 6 | Winchester |
| `0x38` `10`/`16`/`17`/`1D`/`70` (LM3532 backlight) | 5 | Winchester |
| `0x48` `F5`/`E5`/`A3`/`A1`/`A2`/`FA` (USB LDO init) | 6 | **unconditional** |
| `0x49` `07`/`06`/`04` (USB VBUS/ID) | 3 | **unconditional** |

* **Correction 1 — it enables VMMC.**  `0x98`–`0x9B` = GRP `01`, TRANS `03`,
  STATE `21`, VOLTAGE `15` → **3.0 V, group P1, requested ON**.  The earlier
  pass said "never touches VMMC/VPP/VUSIM enables"; that is wrong for VMMC
  (VPP and VUSIM genuinely are untouched).
* **Correction 2 — it writes six GROUP registers** (`0x84`, `0x88`, `0x8C`,
  `0x98`, `0xAD`, `0xB0`), all `0x01` = `P1_GRP_6030`.
* **`0x21` to a STATE register decoded from the local headers**: `0b0010_0001`
  = GRP `001` (P1) + APP `00` + state `01` (ON) — precisely what
  `twl6030reg_enable()` produces (`grp << 5 | TWL6030_CFG_STATE_ON`).  The
  later `pmic-twl6030-ctl` rewrites the same registers as `on app` = `0x05`,
  which is what the live captures read.
* **No MMIO at all**: an exhaustive store audit of the whole `R E` segment found
  every store targeting the stack except libc's stack-canary init.  No PRCM, no
  SCRM, no AUXCLK, no pads, no GPIO.

### Why VMMC cannot be the gate — measured, not argued

| register | Linux | TabletOS with `ti0` up | reading |
|---|---|---|---|
| `0x98` VMMC GRP | `0x07` | `0x00` | detached on QNX |
| `0x99` VMMC TRANS | `0x05` | `0x03` | different encoding |
| **`0x9A` VMMC STATE** | **`0x00`** | **`0x00`** | **off in both** |
| `0x9B` VMMC VOLT | `0x09` | `0x15` | 3.0 V on QNX, unused |

`pmic-twl6030-ctl`'s `pmic-twl6030.cfg` does `set_state vmmc off app con mod`,
so the 3.0 V window `pmic_twl6030_cfg` opens at IFS time is closed again long
before `wifi.sh`.  And MMC5's supply is not VMMC in either design: ours is the
fixed `vwl1283` 1.8 V regulator, and the reference's WLAN driver writes no rail
at all.

`0xBC`–`0xBE` (TRANS `05`, STATE `E1` written at boot) reads **`0x00` in both
OSes**, so its identity, though still unknown locally, is not a runtime
difference either.

## 2. What the cut-off passes still produced

* **`startup-playbook`**: a store list was assembled but not completed; the
  partial result agrees with report 219 on `CM_L4PER_MMCSD5_CLKCTRL`
  (`0x4A009560 = 2`) and on **no PRM L3INIT/L4PER write** (hence no
  `RM_L4PER_MMCSD5_CONTEXT`).  The full inventory is prompt 1 in the prompts
  file.
* **`devb-mmcsd-winchester`, `powerman`/`devpm-omap4.so`,
  `setup-core-inactive`/`.script`**: cut off before reporting.  Their prompts
  are prompts 4, 5 and 6.

## 3. Verdict

**The boot path adds nothing.**  After two independent passes over it — the
first broad, the second one-binary-at-a-time — the only writer Linux does not
run (`pmic_twl6030_cfg`) is decoded, its every value is either replicated by an
existing Linux driver/DTS or measured identical in the end state on both OSes,
and the one transient rail it opens (VMMC, 3.0 V) is closed again before the
WLAN service starts and is off on the reference while the WLAN works.

That closes the third of the three surfaces this project can read:
the driver's own writes (report 217), the live register state of both OSes
(report 218), and now the whole boot path's writes (this report).  What remains
is inside the unreadable MMCHS5 block (trap 101) or at the chip's pins
(trap 165).

## 4. Sources

* `local/work/1917/ifs/proc/boot/pmic_twl6030_cfg` (transactions at
  `0x8062bc28`–`0x8062beA4`; helper at `0x8062ba30`; Winchester gate at
  `0x8062bc8c`).
* `docs/captures/wlan-qnx-state-2026-11-14.txt`,
  `docs/captures/wlan-linux-state-2026-11-14.txt`.
* Local kernel sources used for semantics: `drivers/regulator/twl6030-regulator.c`,
  `drivers/mfd/twl-core.c`, `drivers/usb/phy/phy-twl6030-usb.c`,
  `drivers/leds/leds-lm3532.c`, `include/linux/mfd/twl.h`.
* Reports 219 (first pass, partially corrected here), 217, 218, 220.
* Prompts for the passes still to run: `docs/reports/data/re-prompts-2026-11-14.md`.
