# Project overview — mainline Linux on a BlackBerry PlayBook

A from-scratch port of **upstream Linux 7.3.3** to a
**BlackBerry PlayBook** (TI OMAP4430 ES2.2, RIM codename "winchester").  The
6.15.11 tree is kept as the backup/reference.  For the
Linux image, no RIM kernel, bootloader, driver or filesystem is reused: the unit
boots an image this project builds, written into **media region 0**
(`/dev/hd1`), with an **initramfs** PID 1/console, a **USB CDC-ACM console**,
and a **BusyBox userland on the region-1 ext4 root**.  Everything is recovered
by measurement — from a live QNX 6.6 reference on the same hardware and from
static RE of its binaries — rather than from a vendor source drop, which does
not exist.

A second artefact is built here: a modified **2.1.0.1917** boot image whose
IFS and `/base` are ours (`IFS-REBUILD.md`).  For what works, what is next and
the current unit, read `STATE.md`; this page deliberately carries no status.

Session numbering is explained in `docs/sessions/NUMBERING.md`: the reports call
the Linux-era sessions "session 1…N", which are absolute `N+6`.

## The hardware

| item | detail | source |
|---|---|---|
| SoC | TI OMAP4430 **ES2.2**, "winchester", secure (HS) part, PVT4 rev 07 | `uname -a` on the reference: `OMAP4430_Winchester_ES2.2_HS_PVT4_Rev:07` (bootstrap §1) |
| DRAM | 1 GB Elpida at `0x80000000`–`0xBFFFFFFF` (two 512 MB banks) | DTS memory node comment, bootstrap §3 |
| eMMC | Samsung MCGAFA, 59.4 GiB, on MMC2, with `boot0`/`boot1`/`rpmb` | report 109 §9 |
| Console UART | UART3 at `0x48020000` (dead as a physical port; used for DEBUG_LL markers and the DRAM ring) | report 122 §1.1 |
| PMIC | TWL6030 at slaves `0x48`/`0x49`/`0x4a` on I²C1 (`0x48070000`) | reports 119, 120 §0.2 |
| Watchdog | OMAP WDT2 at `0x4A314000`, always on; 7.812 s after the loader arms it | reports 109 §8.1 |
| Display | 1024x600 panel behind a **TC358764** DSI→LVDS bridge; DSI1 from DISPC (LCD manager), RGB888, 4 data lanes | reports 118, 121 §11 |
| Panel control port | **MCSPI4** at `0x480BA100` (QNX calls it `/dev/spi3`), Hitachi 9-word TCON table | report 111 §3.5, 121 §1 |
| Backlight | **LM3532** at I²C `0x38`, gated by OMAP `GPIO2_16` (`gpio_48`) | report 120 |
| USB | OMAP MUSB OTG + TWL6030 comparators; gadget enumerates as `0fca:8010` | reports 114, 115 |
| Media | eMMC partitioned by an MCT in 64 KiB blocks; region 0 = `/dev/hd1` (512 MB), region 1 = `/dev/hd2`; the region-0 IFS sits at region offset `0x100000` = block `0x825` | report 100 §1, session-06 §3 |
| Units | a hardware unit is identified by its PIN (`0x50175127` is the reference). The inventory changes; see `STATE.md` §"The unit" | report 85 §0, sessions 5/6 |

The device is a **secure** part: the product boot path verifies ECDSA signatures.
This project does not attack that path. It uses the bootloader's **CRC/factory
path** (`os_type = 6`), which is what makes a self-built image bootable at all.
That path spends a factory boot budget — `BOOTCTR_FCTRY`, a 4-byte **RPMB**
record that accepts `1..0x32` — on every successful factory boot (report 116).
`0` or `> 0x32` is not a fallback: it hard-locks the unit ("expired factory
mode") with no software recovery. Every write rule, and the difference between
the RPMB and the NVRAM store, is in **`SAFETY.md`**; read it before touching the
unit.

## The boot chain

```
OMAP4430 boot ROM
  -> eMMC second stage: hd0 0x4000.. loaded at 0x80000000
       (the ROM's own loader; report 91 §1)
     -> verifies the region-0 IFS image, loads it at 0x800FF800
        -> executes the header's ARM branch at image +0x000 (0xEA0001FE)
           -> image +0x800 = 0x80100000: OUR CHAINLOADER
              -> copies zImage to 0x84000000, DTB to 0x84000000 + align(len(z),8)
                 -> jumps: r0=0, r1=0xffffffff, r2=DTB (ARM Linux boot protocol)
                    -> decompressor -> kernel at 0x80008000
                       -> initramfs /init as PID 1
                          -> /console on /dev/ttyGS0 <-> host /dev/ttyACM0
```

Only two of those stages are ours: the chainloader and everything after it.
What each stage is allowed to do matters when a boot fails:

| stage | what it does | what it cannot do |
|---|---|---|
| OMAP4430 boot ROM (`0x40028000`, 48 KB, non-secure-readable) | a complete USB device stack plus an MMC/FAT media stack; finds and loads the second stage | no display of any kind (it has no DSI host), no arbitrary read of its own protected page `0x40027000`–`0x40027FFF`, and its signed ramloader's read opcodes are RSA-gated |
| eMMC second stage (`hd0`/`hd0head.bin`, linked `0x7FFF7000`, loaded `0x80000000`) | reads NVRAM `0x2040` and `0x2801`, reads the RPMB selector, verifies region 0 (ECDSA or CRC), may **nuke** on some paths, then hands off | it is not the OS loader: its only MMC image loader has no caller, so the 2.75 MB image at `0x800FF800` was placed by something else (open question, report 99 §3) |
| region-0 image = our chainloader image | word 0 `0xEA0001FE` branches to image `+0x800` = `0x80100000` | — (`LoadEnd` at `hdr+0x08` is the first byte past the image, not an entry point) |
| chainloader (our code) | five actions: clear the four breadcrumb rings, copy zImage/DTB high, check the DTB magic, publish the epoch block, jump | it never writes media and never touches WDT2 |
| kernel | pets WDT2 from a 2 s `timer_list`, devicetree describes the board | modules load from the region-1 root, not from the initramfs |
| userspace | PID 1 in the initramfs hands over to the BusyBox `/sbin/init` on the region-1 ext4 root (rw); the console is on USB | — |

The chainloader is entered with the MMU off, in ARM state, SVC, IRQ/FIQ masked
(report 91 §2). The epoch-delay instrument is still in its source:
`EP_TICKS=<32 kHz ticks> tools/build_kernel_image.py` holds kernel entry for
that many WDT2 ticks (report 109 §1).

The full stage-by-stage model, with how each claim is known, is
`BOOT-CHAIN.md`. The two verification paths and the operator-only switch are
`FACTORY-PATH.md` and `ARMING-TUTORIAL.md`.
