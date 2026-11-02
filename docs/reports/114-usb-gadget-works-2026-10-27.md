# 114 — Linux USB gadget enumerates on the PC (2026-10-27)

Milestone: our **Linux** kernel presents a USB device to the host —
`0fca:8010`, CDC-ACM, `/dev/ttyACM0` on the PC.  This is report 110's **M4**,
reached in four boards-worth of iterations, and it gives us a second console
channel that does not need a flash cycle.

```
host:  19s: Bus 003 Device 035: ID 0fca:8010 ... /dev/ttyACM0
       iManufacturer  1 Research In Motion
       iProduct       2 PlayBook (Linux bring-up)
device: [3.240783] configfs-gadget.g1 gadget.0: reset acm ttyGS0
        [3.240966] configfs-gadget.g1 gadget.0: activate acm ttyGS0
        [3.263305] configfs-gadget.g1 gadget.0: acm ttyGS0 req21.20 v0000 i0000 l7
```

`req21.20` is the host's `SET_LINE_CODING` (bRequest 0x20, bmRequestType 0x21),
so the PC really talked to the gadget, not just to its descriptors.

## 1. The four things that had to change

| # | problem | fix |
|---|---|---|
| 1 | the whole USB stack was `=m` and nothing loads modules in the initramfs | build in: `USB`, `USB_MUSB_HDRC`, `USB_MUSB_OMAP2PLUS`, `USB_GADGET`, `USB_LIBCOMPOSITE`, `USB_CONFIGFS` (+`_ACM`/`_ECM`/`_RNDIS`), `OMAP_USB2`, `TWL6030_USB`, `CONFIGFS_FS` |
| 2 | `musb-hdrc.0.auto: unsupported port mode 0` → controller binds, **no UDC** | the OMAP glue (`omap2430.c:362`) reads the **legacy u32 property `mode`**, not `dr_mode`; DT needed `mode = <2>` (MUSB_PERIPHERAL) |
| 3 | OMAP4430 uses the internal **UTMI+** PHY, not ULPI | `interface-type = <1>` (same as omap4-panda) |
| 4 | UDC bound but reported **"not attached"** — no VBUS/ID | OMAP4's comparators live in the TWL6030: `&twl_usb_comparator { usb-supply = <&vusb>; };` (`drivers/usb/phy/phy-twl6030-usb.c`, `CONFIG_TWL6030_USB=y`) |

Note on (4): the comparator is a *separate* driver from the OMAP USB2 PHY, and
without it musb sits in `not attached` forever even though the bench probe
showed `MUSB_DEVCTL.BSESSVLD=1` and `USBOTGHS_CONTROL=0x15` before the driver
bound.  The seat-of-the-pants read ("the PHY is already up, so we need nothing")
was wrong; `vusb` must be requested so the comparator is powered.

## 2. How the gadget is configured

`/init` (PID 1) builds it with configfs — no shell needed, no modules:

```
/config/usb_gadget/g1       idVendor=0x0fca idProduct=0x8010
  strings/0x409             serialnumber=123456
                            manufacturer=Research In Motion
                            product=PlayBook (Linux bring-up)
  configs/c.1/strings/0x409 configuration=acm
  functions/acm.usb0        symlinked into configs/c.1
UDC <- first entry in /sys/class/udc   (musb-hdrc.0.auto)
```

That gives `/dev/ttyGS0` in Linux and `/dev/ttyACM0` on the host.

**Identification warning:** `0fca:8010` is *also* TabletOS's USB PID, so
`lsusb -d 0fca:8010` alone cannot tell them apart.  Use the product string
(`PlayBook (Linux bring-up)`) or the presence of the CDC-ACM interface /
`/dev/ttyACM0`.

## 3. Timing trap for host-side observation

After a flash the unit sits in the **bootrom** (`0fca:0001`) holding the USB bus
for ~10–12 s before it boots the media image.  A host-side poll must start
immediately after the force-reset and run for ~30 s; the gadget appears ~19 s in
(as measured).  Polling later, or for a shorter window, misses the kernel
entirely.

## 4. What this unlocks and what is next

- **Interactive console**: `/init` can spawn a `getty` on `/dev/ttyGS0` (or a
  shell after `setsid`), which removes the flash/restore cycle from the loop.
  Needs a getty binary in the initramfs; `busybox` is the obvious choice and we
  have the buildroot toolchain.
- **Networking**: the same gadget framework with ECM/RNDIS instead of ACM gives
  a netdev on both sides (TabletOS used RNDIS/ECM in exactly this role), so a
  `usb0` with a static address is a short step if we add a minimal net stack
  (or use the host as the only TCP/IP party later).
- `113-re-usb-device-driver-2026-10-27.md` (subagent, running) is REing
  TabletOS's `devu-omap4430-musb.so` from the fresh `/base` dump; it may add
  PHY details we no longer strictly need, plus the exact strings/descriptors
  TabletOS advertises.
