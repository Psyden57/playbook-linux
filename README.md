# PlayBook Linux — clean Linux on a BlackBerry PlayBook (OMAP4430)

Verified at session 23 (2026-11-02).

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

## How it works, in one paragraph

The eMMC holds a CRC-signed IFS image in region 0. We replace that image with a
chainloader: a small ARM stub at `0x800` that copies `zImage` and the DTB to
high DRAM and jumps. The kernel is a pristine 6.15.11 plus one patch set
(`kernel/clean-kernel-6.15.11-changes.patch`, 34 files) carrying the
`omap4-winchester` board DTS and the board quirks. The initramfs contains a
static PID 1 and a purpose-built console (no busybox, no shell) whose commands
read and write device registers and buses directly, with every hardware access
performed in a forked worker so a bus abort cannot wedge the console.

## Quick start

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
docs/history/     placeholder only — the abandoned kexec project is not in the
                  public snapshot (kept in the private working history)
kernel/           patch set, kernel config, board DTS + modified driver copies
boot/             chainloader source and image builder
initramfs/        PID 1 (init.c) and the bench console (console.c)
tools/            host tools (console driver, ssh wrapper, flasher, QNX probes)
bench/            device-side scripts (the BOOTCTR top-up startup script)
hardware/         TRM/datasheet copies (local only — not redistributed)
local/            GITIGNORED payload: device dumps, images, keys, vendor tools
```

## Safety notes

**Full detail: [`docs/project/SAFETY.md`](docs/project/SAFETY.md).**  In short —
this device is a **secure-boot PlayBook**, not a development board:

* **An agent never writes RPMB.** `BOOTCTR_FCTRY` is an RPMB record; `0` or
  `> 0x32` hard-locks the unit and recovery means desoldering the eMMC. The
  top-up is the operator's, from a booted OS, with a readback.
* **NVRAM and the MCT are read-only, always.** Read-only eMMC/QNX6 mounts are
  allowed; writable eMMC is allowed only for **our own** data (region 1, and
  `hd3` once repurposed).
* **Never flash the image with CRC `0x3B04298E`** (an early LM3532 build that
  wedged before the backlight enable line was understood).
* Restore the sealed IFS (`local/images/ifs0.sealed.bin`, sha256 `c72b0530…`)
  after test images and verify the hash.

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
