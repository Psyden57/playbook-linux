# A vendor driver owning a bus makes external probes lie — the CM3217 case

**Status: open (as a general rule); the specific CM3217 question is resolved.**

## Claims

1. **Report 182 (current).**  The CM3217 works, and reads 32–37 lux with the room
   lights off and 191 with them on, when read from our Linux image where our
   driver is the only client on I2C4.
2. **Report 182's own earlier drafts, and report 178.**  The part "reads
   all-zero", "is dead", or "does not yield a stable value under any sequence"
   — based on readings taken **from TabletOS**.

## Evidence for each

* For (1): `in_illuminance_input` on Linux, `cm3217.ko`, `3-0010`, 32 32 32 32 37
  then 191 ×6 (and 189/191 later).  Reproducible, and it tracks the room lights.
* For (2): every QNX-side read we made — `pbi2c rd 3 10/11 0` returning `0x00`,
  `0x0d`, `0x6a`, `0x70`, `0xb4`, `0xf6` at different times;
  `/pps/services/light_controller` reporting `Lux::0000`; the panel's automatic
  brightness freezing during probing.
* **The reconciling observation (operator, not a register):** the panel's
  brightness *froze exactly while we were probing* and *recovered when we
  stopped*, and recovered immediately when the vendor idle state was written
  back.  That is the behaviour of a shared bus with two masters' sequences
  interleaved, not of a dead part.

## What is uncertain

* Whether *any* QNX-side reading of a device a vendor driver owns can be trusted.
  The padconf/GPIO/PMIC/CM comparisons in report 180 used the same technique
  (`pbmem3`) on the same OS and were consistent and self-corroborating — so the
  rule is narrower than "never read from TabletOS": it applies to **devices with
  a live driver doing transactions**, not to SoC-side configuration registers.

## What would settle it

A bus capture of `ls-cm3217` doing a measurement would show the interleaving
directly.  Failing that, the rule stands on the operator observation plus the
Linux-side reproduction, which is what DECISIONS §31 and trap 103 record.

## Resolution of the specific question

The CM3217 is **working** (report 182).  The "dead"/"unstable" claims are
withdrawn; the general rule ("do not characterise a driven device from the
reference OS") is adopted as DECISIONS §31.
