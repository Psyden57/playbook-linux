# PlayBook Linux — clean Linux on a BlackBerry PlayBook (OMAP4430)

A from-scratch, upstream-kernel port to a **BlackBerry PlayBook**: TI
**OMAP4430 ES2.2 "winchester"**, secure device, 1 GB RAM, 1024×600 panel behind a
**TC358764** DSI→LVDS bridge, with a **Hitachi** panel and an **LM3532**
backlight controller.

The device boots a **chainloader image** written into media region 0 that loads
a stock **Linux 6.15.11** `zImage` + our `omap4-winchester.dtb`, then an
initramfs whose PID 1 brings up a USB CDC-ACM console (and optionally the
read-only region-1 root). Everything is derived from live measurements of the
device's original QNX 6.6 ("TabletOS") and from static analysis of its boot
image — no vendor kernel, no vendor device tree.

## Status

**The current state lives in [`docs/project/STATE.md`](docs/project/STATE.md) and
only there**; this page does not restate it. The next experiment is in
[`docs/project/HANDOFF.md`](docs/project/HANDOFF.md), and the active image work
is in [`docs/project/IFS-REBUILD.md`](docs/project/IFS-REBUILD.md).

## How it works

The hard part is not Linux; it is getting the bootloader to run an image we
built.  The PlayBook is a secure-boot device: with no factory record, RIM's
second-stage loader verifies region 0 by SHA-512 + ECDSA against RIM's key, so a
self-built image cannot pass.  The enabling discovery is the loader's
**factory/CRC path**.  The loader reads a 4-byte RPMB record,
`BOOTCTR_FCTRY`.  When the record is present with a value in `1..0x32`, it
verifies region 0 by **CRC-32** instead — provided the image's `os_type` byte is
`6`.  That is the gate that makes our images bootable.  The record is a budget:
every successful factory boot decrements it, and only a booted stock OS (our
`devuser` startup script) puts it back.  A unit must be armed — the record
created from a booted OS — before a CRC-sealed image will boot; see
`docs/project/SAFETY.md` and `docs/project/FACTORY-PATH.md`.

Our image is a **replacement for the region-0 IFS**, not an extra file: RIM's
fixed startup prefix and header are kept, and our chainloader, `zImage` and DTB
are placed in the image behind them.  (The separate 2.1.0.1917 work rebuilds the
imagefs itself behind the same prefix; see `docs/project/IFS-REBUILD.md`.)  A
small ARM chainloader at image offset `0x800` copies the 6.15.11 `zImage` and the
board DTB to high DRAM, checks the DTB magic, and jumps with the ARM boot
protocol.  The kernel is pristine Linux 6.15.11 plus one patch set
(`kernel/clean-kernel-6.15.11-changes.patch`, 34 files) carrying the
`omap4-winchester` board DTS and the board quirks.  The initramfs contains a
static PID 1 and a purpose-built console (no busybox, no shell) whose commands
read and write device registers and buses directly, with every hardware access
performed in a forked worker so a bus abort cannot wedge the console.  A 32 MiB
ext4 root in media region 1 can be loop-mounted read-only; the longer-term
direction is postmarketOS/nura.

Everything the chain needed — the loader's selector, the RPMB record format, the
verifier's field offsets, the display and PMIC behaviour — came from live
measurements on the device's own QNX 6.6 OS and static RE of its binaries.
Those notes are in `docs/reports/`; the canonical summary is `docs/project/`.

## Quick start

**Prerequisites.** The two cross-toolchains and the kernel build tree live in
the gitignored `local/` payload and are not part of a clone;
[`docs/project/SETUP.md`](docs/project/SETUP.md) explains how to populate them,
or point `CROSS_COMPILE` / `KSRC` at your own.  The commands below assume
`local/toolchains` and `local/kernel-tree` exist.  Flashing is
hardware-specific: read [`docs/project/DEVELOPMENT.md`](docs/project/DEVELOPMENT.md)
and [`docs/project/SAFETY.md`](docs/project/SAFETY.md) before writing anything.

```bash
# both cross-toolchains live in the workspace payload (local/toolchains/);
# this puts them on PATH and exports CROSS_COMPILE / QNX_HOST / QNX_TARGET
. tools/env.sh

# build
make -C local/kernel-tree ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make -C local/kernel-tree ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb

# wrap into the chainloader image (prints the image CRC; tools/env.sh above
# already provided the QNX 6.6 toolchain)
python3 tools/build_kernel_image.py

# flash the test image and boot it  (full loop: docs/project/DEVELOPMENT.md)
python3 tools/pre_os_ifs_flash.py --template-ifs boot/chainloader/kernel.img
```

The bench console is driven from the host with `tools/pbcon.py`; the console's
own command set (`peek`/`poke`, `i2c`, `i2c16`, `spi`, `fb`, `kmsg`, `disp`,
`bl`) is documented in [`docs/project/DEVELOPMENT.md`](docs/project/DEVELOPMENT.md).

## Repository layout

```
docs/project/     canonical documentation (overview, state, architecture,
                  setup, development, decisions, known issues, roadmap, handoff)
docs/reports/     the analysis reports this work is built on (numbered)
docs/sessions/    per-session notes (one file per session; the backfill trail)
docs/captures/    live text captures (register dumps, ring buffers)
docs/contradictions/  recorded disagreements between sources, left unresolved
kernel/           patch set, kernel config, board DTS + modified driver copies
boot/             chainloader source and image builder
initramfs/        PID 1 (init.c) and the bench console (console.c)
tools/            host tools (console driver, ssh wrapper, flasher, QNX probes)
hardware/         TRM/datasheet copies (local only — not redistributed)
local/            GITIGNORED payload: device dumps, images, keys, vendor tools
```

## Safety notes

**Full detail: [`docs/project/SAFETY.md`](docs/project/SAFETY.md).**  This is a
secure-boot PlayBook, not a development board; the stores and the boot budget
are unforgiving:

* **The factory boot record is a budget, not a flag.** `BOOTCTR_FCTRY` is a
  4-byte RPMB record; `0` or `> 0x32` hard-locks the unit, and the documented
  recovery is desoldering the eMMC.  Creating or writing the record is an
  operator decision, made from a booted OS with a readback — do not write it
  casually.
* **NVRAM and the MCT are read-only.** Read-only eMMC/QNX6 mounts are fine;
  writable eMMC is only for your own data (region 1, and `hd3` once repurposed).
* **Flashing can brick the unit.** The pre-OS path is the recovery net; read
  `docs/project/DEVELOPMENT.md` and `docs/project/SAFETY.md` before writing
  anything, and keep the vendor images you need to get back.
* There is no warranty.  If you are not prepared to lose the device, do not
  flash it.

## License and publication

The project's own code and documentation are **GPL-3.0-or-later** (the full text
is in [`LICENSE`](LICENSE)).  The kernel patch and the copies under
`kernel/source/` remain **GPL-2.0-only**, and `tools/bbusb.py` is a GPL-3.0
derivative of bb10mt; [`NOTICE`](NOTICE) records the component licences and the
provenance.

The repository is public as a fresh snapshot; the full working history stays
private.  The proprietary QNX dumps, the TI TRM/datasheets, the Toshiba
TC358764 datasheet, the bb10mt source and the device images are deliberately
**not** in the snapshot or the tree.
