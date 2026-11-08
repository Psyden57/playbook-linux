# Which OMAP I²C controller is the Cypress touch on — I²C2 or I²C3?

**Status: resolved 2026-11-08 (session 36, report 193).**  The touch is on
**OMAP I²C2 `0x48072000`** (QNX `/dev/i2c1`, Linux `i2c-1`) at slave `0x65`.
The `libmtouch-pegasus` `/dev/i2c2` string was the wrong driver: the board
selects `driver = munin`, and `libmtouch-munin` opens `/dev/i2c1`.  The Linux
bus was additionally broken by an unmuxed pad pair; see the resolution below.
The pre-resolution text is kept as written.

## Claims

1. **The touch is on OMAP I²C3 `0x48060000`.**  Report 176 records the
   hard-coded path inside `libmtouch-pegasus.so` as **`/dev/i2c2`**, and QNX's
   naming in that report is `/dev/i2c0` = `0x48070000`, `/dev/i2c1` =
   `0x48072000`, **`/dev/i2c2` = `0x48060000`** (I²C3), `/dev/i2c3` =
   `0x48350000` (I²C4).  The report notes QNX does not instantiate
   `/dev/i2c2`, and suggests the controller may not ACK until firmware is
   loaded.
2. **The touch is on OMAP I²C2 `0x48072000`.**  Report 176's live `pbi2c scan`
   of `/dev/i2c1` (`0x48072000`) found a device at **`0x65`**, recorded as
   "not yet identified".  Session 30 read the 1917 `muninfw` `.data` and found
   its default `i2c_slave_addr` is exactly **`0x65`** (400 kHz), so that ACK is
   the strongest address match on the board.

## Evidence for each

* **For I²C3 (claim 1):** the 1917 `libmtouch-pegasus.so.1` contains the string
   `/dev/i2c2`; session 30's `winchester_tp_init` disassembly opens that path
  after mapping GPIO1/GPIO4.  `libmtouch-munin.so.1` also bundles an
  `i2c-omap4` driver (`i2c_omap_init`/`i2c_omap_read8`), which could explain
  why a controller QNX does not instantiate is still usable.
* **For I²C2 (claim 2):** 1917 `muninfw` `.data` at `0x50fc` is `65 00 00 00`
  (`i2c_slave_addr = 0x65`), and `0x50f8` is `0x00061a80` (400 kHz); report
  176's live scan ACKed `0x65` on `/dev/i2c1` = `0x48072000`.  Both controllers
  are enabled in our board DTS (`&i2c2` and `&i2c3`, both 400 kHz).

## What is uncertain

* Whether QNX's `/dev/i2c2` really maps to `0x48060000` on this image, or
  whether the touch daemon starts its own controller and the number is local to
  it.
* Whether the `0x65` ACK on `0x48072000` is the touch at all, or the still
  unidentified `0x65` device report 176 listed (candidate NCP5623 or a board
  EEPROM).  Two devices at the same 7-bit address on different buses is legal.
* Whether the touch answers before its firmware is loaded; report 176's caveat
  says it may not, which would make a bare scan NAK on the right bus too.

## What would settle it

`i2cdetect -y -r 2` and `-r 3` on our Linux 7.3.3 image (read-byte probing, not
the quick-write scan, which skipped `0x30–0x37`/`0x50–0x5f`), then a read at
`0x65` on whichever bus ACKs, before and after the decoded reset pulse
(GPIO4_8 clear → GPIO1_14 low 500 ms → GPIO4_8 release).  If both ACK, upload
the Munin firmware on one bus and see which device changes behaviour.

## Resolved 2026-11-08 (session 36, report 193)

The deciding evidence is the board's own configuration, not a bare scan:

* **`/base/usr/lib/graphics/omap4430/graphics.conf` selects
  `driver = munin`**, with
  `options = i2c_lib=i2c-resmgr-client,tp_intr=1035,i2c_slave=0x65`.  The
  Pegasus library is not used on this board.
* `libmtouch-munin.so` contains **`/dev/i2c1`** (Pegasus has `/dev/i2c2`), and
  uses the resmgr client, so its bus is QNX `/dev/i2c1`.
* Report 176's QNX `pidin ar` gives `/dev/i2c1` = **`0x48072000`**, and its live
  scan found `0x65` there.  The two facts coincide.
* On the live Linux image, `/sys/bus/i2c/devices/i2c-1/of_node` is the
  `target-module@72000`, i.e. **Linux `i2c-1` = `0x48072000`**.

The Linux side also had a second, independent fault: `0x4A100126`/`0x4A100128`
(i2c2_scl/sda) were **`0x011F` = safe mode**, so every i2c-1 transaction timed
out and `0x65` never answered.  Our DTS enabled `&i2c2` but never muxed its
pads (QNX's `startup-playbook` does).  Writing both pads to `0x0110`
(mux 0, input, pull-up — the value on the working I²C1 pads) made `0x65` ACK
and a register read succeed.  The fix is now an `i2c2_pins` group in
`omap4-winchester.dts`.

Residual: the exact part/protocol (Gen3/Munin) and the `tp_intr = 1035 →
GPIO2_3` inference are open; the bus and address are not.

