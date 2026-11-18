# The USB/VUSB drop: "idle runtime PM at ~32 s" vs "intermittent"

**Status: open.**  Two readings of the same event, with different consequences.

## Claim A (session 64, report 262 §3a and the session-64 note)

The unit's USB gadget deactivates **after ~32 s of idleness**: the kernel logs
`VUSB: disabling` / `audio-ldo: disabling`, the gadget goes away, and the unit
keeps running.  It was written up as *idle runtime PM*, "a separate PM matter",
with the implication that host activity (a command every ~20 s) would keep the
link alive.

Evidence: the session-64 operator capture of those two lines, and the timing
("after ~32 s idle") on the BusyBox image.

## Claim B (session 65, report 263 §4/§4a)

The teardown is **intermittent and not an idle timer**:

* BusyBox root, a console command every **15 s**: `EIO` at **~30 s** — activity
  did not prevent it.
* Nura root, SSH traffic: `VUSB` read `enabled` at **82 s**, link dead at
  **~90 s**.
* Final boot (Nura, `0xC9FEE2D1`): the ACM was read **continuously** for
  **240 s** (`cat /dev/kmsg` streamed to the host), then left **closed and
  idle for 90 s** — the link was **still up at uptime 376 s**, and `dmesg` had
  **no** `VUSB: disabling` line.

Evidence: `local/work/soak65-20261118-183036.log` (EIO at 30 s),
`local/work/kmsg-drop2-*.log` (72,992 bytes, records 0–722, 240 s without a
drop), and the live reads at 82 s and 376 s (`VUSB enabled`; `0fca:8010`
enumerated).

## What is not in dispute

* `drivers/usb/phy/phy-twl6030-usb.c`'s `twl6030_usb_irq()` is the only caller
  of `regulator_disable(twl->usb3v3)` — the `VUSB: disabling` line — and it does
  so when it reads `CONTROLLER_STAT1.VBUS_DET` clear.
* A dropped link does **not** re-enumerate on a cable replug; the unit keeps
  running.
* `audio-ldo: disabling` at ~31.8 s occurs on both roots and is a separate
  regulator (it is not the VUSB event).

## Uncertainty

The event's trigger is not established.  Candidates: a spurious/low
`VBUS_DET` read in the TWL comparator IRQ; the `twl6030_charger` module (it
probes — `battery with vmax -22 imax: -22` — while the board charges through
the ISL9519); or a real host-side port event.  Claim A's `audio-ldo` line is
reproducible; its attribution of the *VUSB* line to idleness is what Claim B
contradicts.

## What would settle it

A capture that spans the event: start `local/work/kmsgcap.py` (or a userspace
`cat /dev/kmsg`) before the link dies and let it run through the disconnect —
the last records before the `EIO` identify the caller and the TWL state.  The
session-65 run did exactly this but the event did not occur (376 s); repeat it
until a boot drops.  A hook in `twl6030_usb_irq()` logging the `VBUS_DET` byte
and `linkstat` would settle the spurious-read question directly.
