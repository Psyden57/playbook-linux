# 225 — Five one-binary RE passes: the clock-domain difference they surfaced, and why it is not yet a fix

Date: 2026-11-14 (session 49, Linux-era 43).  Static, file-only RE plus
cross-checks against the captures of reports 217/218.  No bench change.

The five reports this one summarises are the operator's second RE pass, run one
binary per chat (the prompts are in
`docs/reports/data/re-prompts-2026-11-14.md`):

| report | binary | verdict |
|---|---|---|
| **221** | `pmic-winchester-twl6030` | nothing gates the WLAN; the resmgr only ever writes "off" |
| **222** | `devb-mmcsd-winchester` | owns MMC1/2 only; no per-domain or MMC5 write |
| **223** | `startup-playbook` | 549 stores; **disables** `CM_L4PER_MMCSD5_CLKCTRL`; no PRM L3INIT/L4PER write |
| **224** | `powerman` + `devpm-omap4.so` | nothing holds the radio down at probe time |
| — | `setup-core-inactive` + `.script` | no PRM L3INIT/L4PER write; the `.script` prunes nothing |

## 0. Headline

Two things came out of these five that change the picture.

1. **A real, never-tested difference: the clock-domain mode.**  On TabletOS,
   with `ti0` RUNNING, **`CM_L3INIT_CLKSTCTRL` and `CM_L4PER_CLKSTCTRL` read
   `…02` (SW_WKUP)** — both domains are held force-awake.  Under our Linux boot
   they read `…03` (**HW_AUTO**), i.e. the framework lets them idle.  MMC5 lives
   in **L3INIT**, and the VDDS bank its pads use is in **L4PER**.  Every earlier
   comparison looked at module clocks (`CM_L4PER_MMCSD5_CLKCTRL`, which matches)
   and never at the *domain* mode.  Measured directly from the two captures of
   report 218:

   | register | Linux | TabletOS (`ti0` up) |
   |---|---|---|
   | `CM_L3INIT_CLKSTCTRL` `0x4A009300` | `0x00045303` (HW_AUTO) | `0x00045302` (**SW_WKUP**) |
   | `CM_L4PER_CLKSTCTRL` `0x4A009400` | `0x00040103` (HW_AUTO) | `0x00040102` (**SW_WKUP**) |
   | `CM_CAM_CLKSTCTRL` `0x4A009000` | `0x00000001` (SW_SLEEP) | `0x00000003` (HW_AUTO) |

2. **And it is negative.**  §1 reports the test: making both domains `SW_WKUP`
   the way the reference does — via `pm_runtime_forbid()` on the MMC5 host, not
   a `CLKTRCTRL` poke — reproduces the reference's domain *and* module-clock
   state and leaves the card silent.  Kept here because it is the first positive
   lead in several sessions and it is now closed.
3. **`startup-playbook` deliberately turns MMC5's module clock *off*, and
   `setup-core-inactive` keeps it off.**  Report 223 shows
   `CM_L4PER_MMCSD5_CLKCTRL (0x4A009560)` written to MODULEMODE=0 in the
   `full` path (`0x6c6c`); the module is enabled later, by the WLAN driver
   itself (`sdio_core_pm`, replayed in report 217).  That is consistent with
   the reference enabling the module **only while the radio driver holds it** —
   the opposite of a boot-time precondition.  It also means the `= 2` this
   project has been setting at boot (and reading as "the reference's value") is
   *not* what the reference does at boot.

## 1. It was tested — and it is negative

The safe test (§2's shape) was run: `pm_runtime_forbid(host->dev)` at the end of
`omap_hsmmc_probe()`, holding the MMC5 host's runtime-PM reference for good, so
the clock framework itself keeps L3INIT awake instead of anyone writing
`CLKTRCTRL`.  Image `0xD6A4A8FF`, kernel #52.

Result — the mechanism works **exactly** as predicted:

| register | before (HW_AUTO) | after this build | TabletOS |
|---|---|---|---|
| `CM_L3INIT_CLKSTCTRL` `0x4A009300` | `0x00045303` | **`0x00077302`** | `0x00045302` |
| `CM_L4PER_CLKSTCTRL` `0x4A009400` | `0x00040103` | **`0x004FFF02`** | `0x00040102` |
| `CM_L4PER_MMCSD5_CLKCTRL` `0x4A009560` | `0x00030000` | `0x00000002` | `0x00000002` |

Both domains are now `SW_WKUP` (bits[1:0] = 2), and MMC5's module clock reads
`MODULEMODE=2` — i.e. **the clock-domain state now matches the reference, and in
the module-clock case it matches it exactly**.  And:

```
mmc4: Failed to initialize a non-removable card
GPIO2_DATAIN = 0x01000010     (bit 21 = 0 — the chip never raises WL_IRQ)
```

**The card is still silent.**  So the domain mode is not the gate either — the
first positive lead in several sessions, tested and closed in the same session.

Note one real difference the experiment leaves behind: our `CLKSEL`/`IDLEST`
fields differ from the reference's (`0x77302` vs `0x45302`, `0x4FFF02` vs
`0x40102`), because the reference's domains are awake with a *different set of
modules enabled* — ours wakes more of L4PER.  That is a consequence of our board
using more peripherals, not a fault.

## 2. Why the domain finding was not shipped as a patch

Promoting `l3_init_clkdm`/`l4_per_clkdm` to `SW_WKUP` from the board DTS is
**not** a safe one-line change on this SoC:

* Trap 183: the Linux clock-domain framework re-asserts its own `CLKTRCTRL`
  policy from driver runtime-PM, so a setting that is not owned by a live
  consumer tends to be reverted — or, worse, to fight the driver.
* A `ti,clockdomain` node only changes the mode when something **enables** the
  clock it is attached to (the `clkctrl_clk` enable path calls
  `clkdm_clk_enable`), and the domain clocks are not exposed as consumers one
  can enable from a board file without claiming a `clkctrl` provider.
* Forcing a domain awake that the framework expects to idle is precisely the
  class of change that has wedged this unit before (trap 14's module-standby
  writes, trap 193's raw McBSP poke).

So the honest form of this finding is a **measurement and a hypothesis**, not a
patch.  The safe way to test it is the same shape as every other successful fix
in this project: give it to a *driver* that owns the resource — e.g. keep the
MMC5 host's runtime-PM reference held for the probe (which keeps L3INIT awake
through the clock framework's own path) and re-check the two registers — rather
than writing `CLKTRCTRL` behind the framework's back.

## 3. What the five passes close

* **`pmic-winchester-twl6030` (221).**  The 20-record table decoded (report
  219's "19 at 0x8170" was off by a record); the only rail writer is the
  `set_state` devctl, **always triggered by an external client**, and the resmgr
  itself only ever writes "off".  REGEN1/2 are proven to end at
  `0xAF←0xE0` then `0xAD←0x00` — bit-identical to Linux's
  `twl6030reg_disable()`.  Its only Linux gap is `GPIO1_15`, a panel rail.
  **Nothing gates the WLAN.**
* **`devb-mmcsd-winchester` (222).**  It owns **only** MMC1 `0x4809C000` and
  MMC2 `0x480B4000` (`omap4_attach` rejects every other base); `0x480D5000`,
  `0x4A009560` and `0x4A307564` appear nowhere in the image.  Its PRM writes are
  **per-module** `RM_L3INIT_MMC1/2_CONTEXT`, not a domain-level clear.
* **`startup-playbook` (223).**  549 stores resolve (not 219's 144).  It writes
  `CM_L4PER_MMCSD5_CLKCTRL = 2` (`0x80101764`) and **no** PRM L3INIT/L4PER, so
  `RM_L4PER_MMCSD5_CONTEXT` is never written by the boot path.  Its pad walker
  *can* reach `0x4A100050`, `0x4A100074` and `0x4A100148/14C/150` but **not**
  `0x58`, `0x78`, `0x7A`, `0x7C` or the WKUP `0x58` — the `0x58` mux reported in
  report 220 must therefore come from `hwio-config-winchester.so`, which is
  consistent with the reference pattern and is now the explanation of record.
* **`powerman` + `devpm-omap4.so` (224).**  Reports 187/189/190 decoded the
  **wrong preset**: `startup.sh:1494` selects `winchester2.pm` on this board, so
  the live tables are the `winc2_*` ones, and the dep-group array is 28–32
  blocks, not 6.  Both presets *do* have a WLAN block (`air_needs_wifi` → a
  `wifi_driver` device → `msg::wifi_power` on `/pps/services/wifi/control`), and
  groups *do* command `ldo_regen1`/`ldo_regen2` — but there is still no WLAN
  rail, clock, power domain, GPIO or I2C.  `powman_alive()` is only a refcount.
  **Verdict: negative.**
* **`setup-core-inactive` + `.script`.**  No PRM L3INIT/L4PER write at all, no
  I²C, VC configured but never triggered (no VP, no bypass).  The `.script`
  format was corrected (`len` includes the 4-byte header; 60 records) and
  transcribed completely: **all 60 records run in a minimal image** — the
  pruning point is `/base/scripts/startup.sh`, not the script.  Its one
  MMC5-relevant action is to **disable** `CM_L4PER_MMCSD5_CLKCTRL`.

## 4. Where the WL1283 question now stands

| surface | state |
|---|---|
| the WLAN driver's own writes | **replayed, negative** (report 217) |
| live register state of both OSes | **diffed, negative** (report 218), except the domain modes above |
| the whole boot path's writes | **exhausted, negative** (report 221 + the five passes) |
| the **clock-domain mode** | **tested and negative** (this report §1): matching the reference's `SW_WKUP` changes nothing |
| inside MMCHS5 | unreadable to a third process (trap 101) |
| the chip's pins | unmeasurable (trap 165) |

The domain finding is the first *positive* lead in several sessions: it is a
real, measured, previously-uncompared difference in the hardware that carries
MMC5 and its pads.  It is ranked above another register sweep, and it should be
tested the way this project tests everything — through a driver that owns the
resource, one variable per boot, with both registers re-read afterwards.

## 5. Sources

* Reports 221–224 (the five passes), this report's table.
* Captures: `docs/captures/wlan-{qnx,linux}-state-2026-11-14.txt`.
* TRM `hardware/swpu231ap.pdf.txt` Table 3-12 (`CLKTRCTRL` semantics:
  0 NO_SLEEP, 1 SW_SLEEP, 2 SW_WKUP, 3 HW_AUTO) and Table 3-1130 (the CM2 block
  map that puts MMCSD5 in L4PER_CM2 and the pads' bank in L4PER).
* Trap 183 (the framework re-asserts `CLKTRCTRL`), trap 101, trap 165.
