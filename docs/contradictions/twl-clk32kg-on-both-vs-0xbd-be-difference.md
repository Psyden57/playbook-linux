# TWL6030 `CLK32KG`: "on in both" vs the `0xBD`/`0xBE` differences

**Status: open.**  Two readings of the same PMIC comparison disagree about
whether the 32.768 kHz clock resource is actually configured the same under
QNX and Linux.  Nothing has been measured that settles it.

## Claim 1 — `CLK32KG` is ON in both (report 180 §4, dossier §7.3)

The session-29 TWL6030 comparison (report 180 §4) compared the `0x80`–`0xB0`
block and concluded:

* every regulator `CFG_STATE` is identical (`… = 0x05` = ON/APP);
* only group/transition words differ (`CFG_GRP 0x07` vs `0x01`,
  `CFG_TRANS 0x55` vs `0x05`), which cannot switch power;
* `VMMC`/`VPP`/`VUSIM` are off in both, and `CLK32KG` is **ON in both**.

The claim is a *state* claim: the clock resource's current state is on under
both OSes.

## Claim 2 — `0xBD`/`0xBE` may be the `CLK32KG` resource and may differ
(report 191 §1.2, from response3)

The session-29 full-register comparison also recorded **12 non-rail
differences**, including `0xBD` and `0xBE` (the full list: `0x00`, `0x01`,
`0x20`, `0x2d`, `0xb0`, `0xb1`, `0xb2`, `0xbd`, `0xbe`, `0xe4`, `0xf4`,
`0xfa`).  Those values were classified as "non-rail" but never individually
decoded.

The external review (response3, recorded in report 191 §1.2) reports that
FreeBSD's TWL driver places the `clk32kg` resource at base `0xBC`, with
`GRP`/`TRANS`/`STATE` at `0xBC`/`0xBD`/`0xBE`.  If that mapping is right, the
`0xBD`/`0xBE` differences are the `CLK32KG` transition/state registers, and
the "ON in both" statement is at least incomplete — possibly wrong for the
group/transition behaviour (e.g. always-on vs off-in-sleep), even if the
current state bit is on.

Supporting facts, both verified this session:

* The TWL6030 datasheet (`hardware/twl6030.pdf`) confirms `CLK32KG` is a
  32 kHz digital **software-controlled** clock output and lists it as a clock
  resource in the APP/CON/MOD groups.
* Linux's `include/linux/mfd/twl.h` has `TWL6030_REG_CLK32KG 48` — a resource
  ID, not a register address; the register base must come from the datasheet
  or the vendor driver.

## What is uncertain

* **The actual values of `0xBC`–`0xBE` on QNX and Linux were not recorded.**
  The reports quote the difference list, not the bytes.  Until they are read,
  neither "ON in both" nor "the CLK32KG resource differs" can be confirmed.
* **The register base `0xBC` is from FreeBSD, not from this tree.**  It is
  plausible but unverified against the TWL6030 datasheet's register map.
* **A group/transition difference does not necessarily change the current
  output.**  `CFG_GRP`/`CFG_TRANS` control which group owns the resource and
  how it transitions; the `CFG_STATE` bit is the current on/off state.  The
  WLAN needs the clock running at boot; a group/transition difference could
  matter for sleep/wake, or not at all.
* **The WL1283's SLOW_CLK pin is not proven to be this resource.**  The
  Motorola 148603 guide requires a 32.768 kHz input; the board could route it
  from `CLK32KG`, `CLK32KAO` or another source.  The pad that may carry it is
  WKUP `0x4A31E05C` pad1 `SYS_32K` (high half `0x0100` on both), but the
  board net is not traced.

## What would settle it

1. **Read `0xBC`–`0xBE` on both OSes** (read-only).  QNX: the PMIC driver owns
   the bus; use the PMIC service or the same read-only `pbi2c` route as the
   earlier TWL dumps, and heed trap 103.  Linux: the TWL is on I²C1; use
   `i2cget -f -y 0 0x48 0xbc` etc. (the PMIC driver may hold the client, so
   `-f` is needed).
2. **Decode the TWL6030 resource map** from `hardware/twl6030.pdf` for the
   `CLK32KG` resource: confirm the `GRP`/`TRANS`/`STATE` offsets and the bit
   meanings.
3. **Compare the group/transition/state bits** and decide whether the
   difference can affect the running clock or only sleep/wake.
4. **Measure the 32.768 kHz at the chip** (or at the `SYS_32K` pad) under QNX
   and Linux, if the register comparison is ambiguous.

Until then, do not quote "CLK32KG is ON in both" as proof that the WLAN slow
clock is identical.

## Sources

* `docs/reports/180-wl1283-sdio-2026-11-06.md` §4 (the TWL comparison).
* `docs/reports/190-wl1283-dossier-2026-11-08.md` §7.3 (the "ON in both"
  statement and the 12-difference list).
* `docs/reports/191-wl1283-external-review-2026-11-08.md` §1.2 (response3's
  `0xBC`–`0xBE` claim, recorded as an external opinion).
* `hardware/twl6030.pdf` (CLK32KG is a software-controlled clock resource;
  Table 11).
* `local/kernel-tree/include/linux/mfd/twl.h` (`TWL6030_REG_CLK32KG 48`).
* `docs/sessions/session-29.md` (the full 256-register comparison and the
  12-difference list).
