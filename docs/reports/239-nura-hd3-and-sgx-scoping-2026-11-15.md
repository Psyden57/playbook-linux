# 239 — hd3, Nura and SGX540: scoping the next phase

Date: 2026-11-15 (session 53, Linux-era 47).
Bench: image **`0xDD66D8C3`** + region-1 rootfs **`49e318bf…`**.
Status: **research and design only.  No writes to hd3, the MCT, NVRAM or RPMB.**

## 0. Headline

Three questions were asked and answered enough to plan against:

1. **Can hd3 hold a Linux root without a huge transfer?  Yes** — and the
   operator's constraint is the right one: hd3 should hold a **flasher-restorable
   ext4 image**, so a broken root is recoverable with the pre-OS flasher rather
   than needing a working Linux on the device.  The loader already allow-lists
   MCT type `0x1C`.
2. **Which userspace?  Nura** (postmarketOS was renamed on 2026-09-27), with
   **Catacomb** or **Sxmo-dwm** as the DE.  The PlayBook is not a supported
   device, so this is a new port; the OMAP4430 reference is
   `device-samsung-espresso7`.
3. **Is SGX540 reachable?  Not with mainline 7.3.3** — nothing in-tree binds
   `ti,omap4430-gpu`.  The route is the `pvrports` kernel driver plus TI's
   proprietary userspace, on the same SoC family as the espresso reference.

## 1. hd3: what is actually there

Measured by `dd` from `/dev/mmcblk1` on the running unit:

* **`0x49000000`** — a DOS MBR carrying a QNX **v1.2b** boot sector
  (`eb 10 90 …`, the string "QNX v1.2b Boot Loader" at `0x1A0`).
* **`0x49002000`** — the QNX6 superblock (magic `5d 55 29 c2 3e 56 45 21`),
  which agrees with report 138's corrected offset and disagrees with the naive
  MCT arithmetic (`(0x4905-0x15)*0x10000 = 0x48F00000`).  The **region** starts
  at `0x49000000`; the filesystem starts 8 KiB into it.

Region size, from the MCT extent `0x4905–0xFFFFF`: **62.86 GiB**
(67,494,412,288 B).

## 2. hd3: the two ways to put a filesystem there

**(A) In place, on the device.**  Rehearsed non-destructively on a scratch file
and it works: `losetup -f -o <ofs>` → `mkfs.ext2 -F -b 4096` → mount → write →
unmount.  `losetup -o` is present; `initramfs/init.c` already mounts region 1
through `LOOP_SET_STATUS64` + `lo_offset`, so the mechanism is native to this
project.  **But there is no `resize2fs`** on the device (busybox: "applet not
found"), and busybox `mke2fs` has no `offset=` option — so anything involving
growth has to happen on the host anyway.

**(B) Build on the host, flash with `bbusb.py`.**  Preferred, and it is the
operator's decision.  The pre-OS loader's allow-list is built from MCT TLV types
**including `0x1C`**, and `bbusb.py` encodes the extent as
`'0x1C FS Fixed': (0x4905, 0xFFFFF)`, so `--component <image> --dest-block
0x4905` is a path the project already trusts.  The recovery property is the
point: hd3 always holds an image that can be put back from the bootrom.

### The size question, and the one surprise

No 62 GiB transfer is needed, but **not** for the reason first assumed.  The
container builder's `analyze_blocks()` skips runs of **`0xFF`**, not `0x00`
("runs of consecutive non-0xFF blocks").  A sparse ext4 is zero-filled, so its
holes would **not** be skipped, and a fully expanded 62.86 GiB image would send
all 62.86 GiB over the bootrom link.  So image size is a policy choice set by
the transfer budget:

| seed | sent | covers |
|---|---|---|
| 1 GiB | 1 GiB | 1.59 % of hd3 |
| 2 GiB | 2 GiB | 3.18 % |
| 4 GiB | 4 GiB | 6.36 % |

Growing means building a larger image and flashing it over the seed — the gate
accepts any run inside `0x1C` — rather than `resize2fs`, which the device does
not have.

## 3. Nura (formerly postmarketOS)

**The rename is recent and matters for searching.**  postmarketOS became **Nura**
on **2026-09-27**, announced at the project's own conference after a ~1.5-year
naming process.  Package names and many URLs still say `postmarketos` and
`pmaports`, so both names appear in the ecosystem.

| fact | source |
|---|---|
| postmarketOS renamed Nura, 2026-09-27 | `nura.eco/blog/2026/09/27/nura-rename/` |
| Nura, formerly postmarketOS, Alpine-based | Wikipedia, "Nura (operating system)" |
| `pmaports` = "Nura aports"; still a separate repo | `wiki.nura.eco/wiki/Pmaports.git` |
| Catacomb = lightweight, for low-end hardware | `wiki.nura.eco/wiki/Category:Interface` |
| Sxmo selectable as `sxmo-de-dwm` | `wiki.nura.eco/wiki/Sxmo` |
| `pmb.install.blockdevice.create(…, split)` builds image files | `docs.nura.eco` API reference |

**Neither interface is a compromise here.**  Catacomb is explicitly for limited
hardware and battery; Sxmo is a menu-driven tiling WM available in a dwm flavour.
On 1 GB RAM with SGX540 either is sensible.

## 4. The structural difference: how the kernel gets on the device

The espresso reference flashes with
`deviceinfo_flash_method="heimdall-bootimg"` — a Samsung download-mode boot image
at fixed offsets.  **The PlayBook has no equivalent path.**  Its kernel must be
the **CRC-sealed chainloader image** produced by `tools/build_kernel_image.py`
and written by the pre-OS `0xF7` flasher to region 0, block `0x825`.

This is not a blocker, because the halves separate cleanly:

| half | who provides it | state |
|---|---|---|
| kernel | our 7.3.3 tree + patch + config, wrapped by `build_kernel_image.py` | **already done** |
| rootfs | Nura | produced by `pmbootstrap install` |

So the Nura **kernel package is not what flashes the device** — the chainloader
is.  What the device port must express is that its rootfs is flashed to hd3 and
mounted by our initramfs, not that it owns the boot chain.

**Unverified:** that `pmbootstrap install` will produce a rootfs for a device
whose kernel it neither builds nor flashes, and whether a Nura `linux-*` package
can wrap this tree unchanged.  Both are cheap to test with one `pmbootstrap init`
run and no device writes.  This is recorded in ROADMAP item 7a as the gate on
step 1.

## 5. SGX540

**Mainline 7.3.3 has no driver.**  `omap4.dtsi` defines
`sgx_module: target-module@56000000` with the `prm_gfx` power domain and the
`l3_gfx_clkctrl` clock, and a child `gpu@0` compatible with
`ti,omap4430-gpu` / `img,powervr-sgx540` — but **nothing binds it**, and our
board DTS leaves `&sgx_module` disabled.  So the kernel-side infrastructure
exists upstream and only the driver is missing.

### What the reference ships

`local/work/1917/base/usr/lib/graphics/omap4430/graphics.conf` declares:

```
gpu-string = SGX540rev120
egl-dlls  = [IMG%s] libusc.so libsrv_um.so libpvr2d.so libIMGegl.so
```

and the directory holds the full QNX PVR userspace — `libsrv_um.so` (186 712 B),
`libusc.so` (1 169 640 B), `libIMGegl.so`, `libImgGLESv1_CM.so`,
`libImgGLESv2.so`, `libImgOpenVG.so`, `libglslcompiler.so`, `libpvr2d.so`,
`pvrsrvinit.so` (91 520 B), `pvrsrv.so` (191 196 B), plus
`displayHAL-winchester.so` and `libWFDwinchester.so`.

**There is no separate SGX firmware file** anywhere in the reference tree — so
the SGX microcode is not a loose blob we can copy, and the QNX binaries are for
QNX's driver ABI, not Linux's.

### The route

`gitlab.com/pvrports/pvrports` (cloned and read this session):

* `community/linux-openpvrsgx` — kernel driver, tagged 6.1.0, with
  `config-openpvrsgx.armv7` setting **`CONFIG_SGX=y`**, **`CONFIG_SGX_OMAP=m`**,
  **`CONFIG_SGX_DRM=y`**.
* `non-free/sgx-ddk-um` — TI userspace, `pkgver=1.17.4948957`,
  **`license="proprietary"`**, sourced from
  `git.ti.com/cgit/graphics/omap5-sgx-ddk-um-linux`, with SoC subpackages
  including **`ti443x`** (OMAP4430).
* `community/mesa-pvr-dri-classic` — the Mesa side.

Its reference devices are the **Samsung espresso** (Galaxy Tab 2) — OMAP4430 with
TWL6030, the same SoC family and PMIC generation as this unit, including the
`clk32kg` and TWL6030 power work we already did for the WLAN.  That is the most
encouraging fact in this report: someone has SGX540 working on our SoC family
under mainline.

**Expected work:** the driver targets 6.1 and we run 7.3.3, so a rebase is
required.  The blobs are proprietary and stay in gitignored `local/`, as the QNX
dumps and the TI TRM already are.

## 6. What this report does not decide

* The exact seed image size (policy, set by transfer budget).
* Whether the Nura kernel package wraps our tree or is bypassed entirely.
* Anything about LE on the BT core, which remains a separate open question
  (report 238 §5).

## 7. Artefacts

* No files changed on the device or on the media.
* `gitlab.com/pvrports/pvrports` cloned to `/tmp` for inspection only (not added
  to the repository).
* ROADMAP item **7a** carries the plan and the sequence;
  `docs/sessions/session-53.md` carries the session record.
