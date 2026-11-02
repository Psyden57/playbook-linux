# Architecture — components and how they interact

Verified at session 23 (2026-11-02).

Two halves: what we build and boot on the unit, and the QNX reference we mine
for facts. Neither shares code with the other.

| component | lives in | role |
|---|---|---|
| chainloader | `boot/chainloader/chainloader_kernel.S(.in)` | first code of ours to run; copies kernel+DTB high and jumps |
| image builder | `tools/build_kernel_image.py` | assembles the chainloader image, patches the IFS header, computes the CRC |
| flasher | `tools/pre_os_ifs_flash.py` | writes an image into media region 0 through the ramloader — the only flasher that has actually written to this unit |
| USB tool | `tools/bbusb.py` | one CFP-style entry point: the protocol module plus `info` / `flash` / `reset-osregion` / `boot-mode` / `reset-loader` and offline `pack` / `unpack` / `split` / `parse` |
| offline tests | `tools/tests/test_offline.py` | stubbed-device checks for the USB tools; no USB, no unit |
| kernel source changes | `kernel/clean-kernel-6.15.11-changes.patch`, `kernel/patches/` | the complete delta vs pristine 6.15.11 + the board `.config` |
| board DTS + patched drivers | `kernel/source/` (`omap4-winchester.dts`, `tc358764.c`, `dsi.c`, `omap4bc.S`) | board description and the three display patches |
| PID 1 | `initramfs/init.c` | mounts, creates the USB gadget, forks the console; no reboot timer since session 15 |
| console | `initramfs/console.c` | the REPL on `/dev/ttyGS0`, and the only way to touch hardware by hand |
| host capture/drive tools | `tools/` (`pbcon.py`, `pb_ring.py`, `pb_events.py`, `pbssh.sh`, `door-hold.sh`, `pbi2c`, `usb_watch.py`, `probe_loader.py`, `force_reset_loader.py`) | console driver, DRAM capture, QNX access, I²C oracle, loader reset |
| boot-chain tools | `tools/` (`89-crc_tool.py`, `build_crc_image.py`, `90-nvram2074.py`, `memdump3.c`, `memw32.c`, `ramloader-commands.txt`) | IFS CRC verify/reseal, the pre-chainloader images, NVRAM `0x2074`, and the QNX-side memory reader/writer |
| reference binaries | `local/proprietary/dumped4869ifs/` (+ RCFS dump) | the QNX boot set used for static RE |

## 1. The chainloader image

The image is a **replacement for the region-0 IFS**, not an extra file. It keeps
the sealed IFS's header and appends the payload, so the bootloader's own
verification still applies (`tools/build_kernel_image.py`).

| image offset | content | notes |
|---|---|---|
| `0x000` | startup branch `0xEA0001FE` | ARM `b` to image `+0x800`; executed, never compared (report 89 §A) |
| `0x004` | `LoadStart = 0x800FF800` | the address the bootloader loads the image at |
| `0x008` | `LoadEnd` (patched) | builder writes `BASE + end`. This is the first byte past the image, **not** an entry point (`0x803BF610` in the stock image) |
| `0x020` | cookie `0xD7A82D1F` | header form; the verifier checks `end-8` instead (report 89 §B) |
| `0x024` | `LoadBase = 0x800FF800` | must equal the load address |
| `0x028` | `MetricsPtr -> 0x800FF82C` | the verifier's **descriptor** `desc = hdr + 0x2C`. `desc+0xD4` = `os_type` = **image offset `0x100`** (media `0x100100`), `6` = CRC/factory path; `desc+0x9C` = magic `0xC3D0E193` = image `0xC8` (report 89 §1.3 — reading `hdr+0xD4` gives a different field) |
| `0x800` | **chainloader code** | linked at `0x80100000`, ≤ `0x2BFBE0` bytes |
| `0x2C0000` | `zImage` | read at `BASE + 0x2C0000` = `0x803BF800` |
| `0x2C0000+len(z)`, 8-aligned | DTB | read at `BASE + dtoff` (`dtoff = align(0x2C0000 + len(z), 8)`; the generated `.S` carries the last build's value, so do not quote it as a constant) |
| after the DTB | 0x230 (560 B) reserved | the trailer magic/CRC must not land inside the DTB — that bug cost session 7 a boot: `dtc` failed on the embedded copy with a string-table overrun (report 103 §7.2) |
| `end-8` | `0xD7C82D1F` | trailer magic; the verifier also requires `end-4 != 0xFFFFFFFF` |
| `end-4` | CRC32 | `zlib.crc32(image[0:-4])`, i.e. CRC-32/ISO-HDLC over `[start, end-4)`; printed by the builder and quoted everywhere |

Source/destination addresses in that table are the ones the checked-in
`chainloader_kernel.S` was generated with; `tools/build_kernel_image.py`
recomputes them from the actual zImage/DTB sizes on every build.
Limits: the image may not exceed offset `0xB00000` (the IFS zone is
region-0-relative `0x100000`–`0xC00000`). The base is the sealed IFS, `0x2BFE10` bytes
(sha256 `c72b0530…`), which is also the media length of a stock region-0 IFS.

The chainloader's five actions: clear the DEBUG_LL ring headers in all four
breadcrumb pages (`+0x80..+0x1000` of `0x88000000`, `0x90000000`, `0x94000000`,
`0x9FE00000`), copy the zImage to `0x84000000`, copy the DTB to
`0x84000000 + align(len(z), 8)`, verify the DTB magic `0xEDFE0DD0` as a
little-endian word, publish the epoch block at `+0x88` of the breadcrumb page,
then jump. It never touches WDT2 (report 109 §8.1) and never writes media.

### 1.1 Where the image sits on the media

The eMMC is partitioned by an MCT (v1.24) in 64 KiB blocks (report 100 §1).
Region 0 = `/dev/hd1`, region 1 = `/dev/hd2`, NVRAM is MCT type `0x1E`, and there
is no `0x39` entry. The IFS is the raw region at **region-0 offset `0x100000`**
(loader block `0x825`, raw `mmcblk1` offset **`0x8100000`**); "media offset
`0x100000`" elsewhere in these docs is region-0-relative shorthand.  The old
`0x8250000` figure ignored the measured `(block - 0x15) * 0x10000` shift (trap <!-- audit-ok: names the old wrong value -->
47).  `SAFETY.md` §4 has the full extent table and the write rules.

The factory path verifies **region 0 only, once**, and never reads region 1 or the
NVRAM A/B record `0x2074` (reports 94 §0, 95 §5, 97 §0). Region 1 is therefore
not a fallback image; the fallback is the pre-OS `0xF7` reflash.


## 2. Kernel and DTB placement

The kernel is entered from `0x84000000` with `r0 = 0`, `r1 = 0xffffffff`,
`r2 = DTB` — the standard ARM boot protocol, not RIM's. The decompressor lands
the kernel at `zreladdr = PHYS_OFFSET + 0x8000 = 0x80008000`; the DTS pushes its
`reserved-memory` breadcrumb/ring pages out of the way. `bootargs` in the board
DTS:

```
earlyprintk keep_bootcon ignore_loglevel maxcpus=1 clk_ignore_unused panic=10 fw_devlink=permissive
```

`clk_ignore_unused` keeps WDT2's clock alive so a hung kernel still self-resets;
`ignore_loglevel` exists because `loglevel=0` hid a kernel panic for two
sessions (report 109). There is no `stdout-path` on purpose — it would register
`ttyS2` as a second console writing to a dead UART (DTS comment).

The state our code is entered in (report 91 §2–§3): the second stage leaves
through `bx r0` at `0x800192A8`, where `r0 = *(0x80025E74)` was written from the
loaded descriptor — ARM state, SVC, IRQ/FIQ masked, `SCTLR.M = 0` (MMU off),
caches as the loader left them, `VBAR` never written, `TTBR0 = 0x8005D000`,
`TTBCR.N = 0`, `DACR = 0x55555555`, `SP` restored from `0x8005A7C0`, `r1–r3`
undefined, no return path, and WDT2 counting. The chainloader therefore runs
with no stack it did not set itself (`SP = 0x800F0000`) and cannot call back into
the loader: `0x80009BF4` quiesces the monitor immediately before MMU-off and its
semantics are still open (report 91 §9 Q1).

## 3. Userspace: PID 1 and the console

`/init` is a static binary and PID 1 (`initramfs/init.c`). It mounts
`devtmpfs`/`proc`/`sys`/`configfs`, builds the USB gadget by `mkdir` in
configfs (`g1`, function `acm.usb0`, product string *"PlayBook (Linux
bring-up)"*), waits for `/dev/ttyGS0`, then forks `/console`. It reboots **only when the
console asks** through a pipe (or the power button is held); the old 180 s <!-- audit-ok: history of the removed timer -->
`BOOT_LIFETIME_S` timer was removed in session 15, because a boot is now a
deliberate act and every one spends a factory boot (reports 109 §8, 115;
`DECISIONS.md` §13).

`/console` is a REPL on `/dev/ttyGS0`; every operation that touches hardware runs
in a forked worker with a bounded wait, because a write to a gated OMAP target
hangs the interconnect (trap 14). Its transcript also mirrors into the DRAM ring.
That mirror is not a convenience: a `/dev/kmsg` record written around a faulting
device access simply vanished while PID 1 kept running, so probes write the ring
page at PA `0x88000000` directly, with the kernel's own count/index protocol
(session-10 §6, report 112 §4). The ring wraps at 32 KB — a run that printed
~90 KB kept only its tail (report 115).

## 4. The display chain

```
DISPC (LCD manager)  ->  DSI1 protocol engine  ->  DSI1 PHY/PLL  ->  4 data lanes
      ->  TC358764 (DSI receive / LVDS transmit)  ->  panel LVDS input
panel control port:  MCSPI4 (write-only, 3 signals)
panel light:         LM3532 over I2C1
```

| block | base | important registers |
|---|---|---|
| DSS top | `0x48040000` | `DSS_CONTROL` `+0x40` |
| DISPC | `0x48041000` | `CONTROL 0x40`, `TIMING_H 0x64`, `TIMING_V 0x68`, `POL_FREQ 0x6C`, `DIVISOR 0x70`, `SIZE_LCD 0x7C`, `GFX_BA0 0x80`, `LINE_STATUS 0x5C` |
| DSI1 protocol engine | `0x48044000` | `CTRL 0x40`, `COMPLEXIO_CFG1 0x48`, `CLK_CTRL 0x54`, `VM_TIMING1..3 0x60/0x64/0x68`, `VC_CTRL(0) 0x100` |
| DSI1 PHY | `0x48044200` | `DSIPHY_CFG0..15` |
| DSI1 PLL | `0x48044300` | `CONFIGURATION1 0x0C`, `CONFIGURATION2 0x10`, `STATUS 0x04` |
| DSS power/clock | `0x4A307100`, `0x4A009100`, `0x4A009120` | `PM_DSS_PWRSTCTRL`, `CM_DSS_CLKSTCTRL`, `CM_DSS_DSS_CLKCTRL` (bit 18 `STBYST` must be 0 before any DSS access) |
| MCSPI4 | `0x480BA100` | QNX's `/dev/spi3`; ours is `/dev/spidev3.0` via an `spi3` alias |
| I2C1 | `0x48070000` | LM3532 `0x38`, TWL6030 `0x48/0x49/0x4a` |
| GPIO1 / GPIO2 / GPIO4 / GPIO6 | `0x4A310000` / `0x48055000` / `0x48059000` / `0x4805D000` | `OE +0x134`, `DATAIN +0x138`, `DATAOUT +0x13C`, `CLEARDATAOUT +0x190`, `SETDATAOUT +0x194` |
| pad mux | `0x4A100000` | `CONTROL_CORE_PAD0_*`; a GPIO write to a pad not in mux 3 is a silent no-op |

Software entities: `omapdrm` owns DSS/DISPC/DSI (`DRM_OMAP=y`), `panel-lvds`
describes the timing, `tc358764` is a `mipi_dsi_driver` bound at
`58004000.encoder.0`, `leds-lm3532` + `led-backlight` own the backlight, and
`spidev` on MCSPI4 exists so the console can send the TCON table. The mode is
1024x600 @ 60 Hz, RGB888, `DISPC_DIVISOR` giving a 50.4 MHz pixel clock, four DSI
data lanes at 302.4 Mbps each (37.8 MHz byte clock).

**The reference does program the bridge; it is not left untouched.**  Its
control port accepts only MIPI DSI generic packets (there is no I²C table — the
I²C slave is not wired, trap 42), and `winch_lcdctl` calls a full bridge
programmer unconditionally from the boot splash, between the DSI PHY/PLL setup
and the VC enable (report 125).  Our `tc358764_init()` reproduces that sequence;
`reset-gpios = <&gpio2 24 GPIO_ACTIVE_LOW>` gives it the reference's 20 ms
active-low reset pulse (report 122 §7.3), and `SYS_RST <- 0x1f` is never written
(trap 28).  The whole-map diff against a working TabletOS is what confirmed the
match (report 132 §2/§6); `STATE.md` records the current register values.

**Panel TCON.** The Hitachi 9-word table —
`0029 0025 0840 00AD 0430 385F 3CA4 3409 4041` — is sent at 450 kHz, 8-bit,
each word as two bytes **high-first** (the reference's order), one
`SPI_IOC_MESSAGE` per word, with 100 ms after word 0, word 2 and the last word
(report 118 §4.2, 121 §1.2; the byte-order question is settled in
`KNOWN-ISSUES.md` §3.14 — low-first also drives the panel but swaps
magenta↔green and inverts black↔white). QNX runs it *before* DSI/DISPC (its step
8 vs step 9); both orderings were tried (§12.1).

## 5. USB: the gadget path, and the other door

Two completely separate paths share the same cable.

**Our path (Linux running).** The unit enumerates as `0fca:8010` with product
string *"PlayBook (Linux bring-up)"*, CDC-ACM, and the host sees
`/dev/ttyACM0`. The node is `root:uucp 0660` and is **recreated on every
enumeration** — `pkexec chmod 666` after every boot. `tools/pbcon.py` drives it
(binary-clean, 115200, waits for the `pb> ` prompt); the console stays silent
until the first host byte.

**The reference path (TabletOS running).** A TCP `qconn` "door" is kept alive on
the unit by `BerryShell-V4.py … hold` under `door-hold.sh`, and plain SSH then
works to `root@169.254.0.1` with legacy algorithms (`tools/pbssh.sh`, key
`local/secrets/rsa`). There is no separate SSH listener: `Connection refused`
*is* the door. After a **host** reboot the supervisor is gone entirely; after a
**unit** reboot the holder can be stale and must be killed by explicit pid.

`lsusb` identifies the phase — do this before concluding anything is broken:

| USB id | product string | meaning |
|---|---|---|
| `0fca:0001` | — | boot ROM (USB recovery); flash now |
| `0fca:8001` | — | ramloader; one `force_reset` gets the image on media booted |
| `0fca:8010` | `RIM Network Device` | TabletOS running (RNDIS): the normal capture harness |
| `0fca:8010` | `PlayBook (Linux bring-up)` | our kernel: use `/dev/ttyACM0`; SSH will not answer |

## 6. The QNX reference as an oracle

The device's shipped OS is the specification. Four ways it is queried, in order
of preference:

1. **Ask it.** `tools/pbi2c` (source `tools/qnx-i2c.c`, built with the in-tree
   QNX 6.6 toolchain, deployed with `pbssh 'cat > /tmp/pbi2c'`) does
   `probe/rd/wr/scan/dump` on `/dev/i2cN`. It settled the LM3532 backlight in
   minutes after two sessions of guesswork (report 120 §0.1).
   `pmic-twl6030-ctl get_state/get_vout` reads the rails safely;
   `set_state` does not (trap 26).
2. **Read the bridge over DSI.** `tools/pbdsi` (source `tools/qnx-dsiread.c`,
   same toolchain) maps the OMAP DSI protocol engine at `0x58004000` and issues
   MIPI generic read requests on hardware VC 1, so any TC358764 register can be
   read **while QNX's own display works**. This is what turned the display
   problem around in session 15: a whole-map diff against a working system found
   the wrong LVDS porches in our bridge sequence and disproved the
   `LANESTATUS0` interpretation (report 132 §2–§6). Read-only; about half the
   reads fail with an ACK/error `0x0003` and the tool retries.
3. **Read its registers.** `memdump3` for reads, `memw32` for PRCM/clock writes
   only. Report 118 is entirely live register capture, and it is why the port's
   timing table is measured rather than inferred. The DISPC whole-file diff in
   `docs/reports/data/` was taken this way.
4. **Read its binaries.** `local/proprietary/dumped4869ifs/proc/boot/*` and the
   RCFS, with `rizin 0.8.2` and `arm-linux-objdump -D -b binary -m arm`
   (`vaddr == file offset` for the first `PT_LOAD` of nearly every file). This is
   how the DSI/DISPC programming (`winch_lcdctl`), the PMIC semantics
   (`pmic-twl6030-ctl`) and the bridge reset pulse were recovered.

The reference also self-validates the display: `winch_lcdctl -s splash` puts a
494x64 logo at `(265,268)` in a 1024x600 field before the RFS is mounted, so a
working DISPC + DSI + TCON + backlight is observable in one command (report 121
§12).
