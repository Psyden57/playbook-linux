# Host setup

The host is an x86_64 Linux box (Arch) with the unit on USB. Nothing in this
project is installed system-wide. The tools resolve their own files from the
repository root, and **both cross-toolchains live in the gitignored payload**
under `local/toolchains/` (§1–§2), so nothing outside the workspace is needed.
The rest of `local/` is the device payload — the vendor IFS template, the sealed
IFS, the images and the SSH key (§7–§8).

## 1. ARM cross toolchain (builds the kernel and the userspace)

**Both toolchains live in the workspace payload** (gitignored), so no session has
to hunt for them and nothing outside the repository is required:

| toolchain | location | size |
|---|---|---|
| ARMv7 hard-float, Buildroot 2021.11 (gcc 14.3.0, binutils 2.43.1) | `local/toolchains/armv7-eabihf` | 391 MB |
| QNX SDP 6.6 (gcc 4.7.3, `as`/`ld`/`objcopy`) | `local/toolchains/qnx660-master` | 937 MB |

Set both up with the helper (it also validates them and prints the versions):

```bash
. tools/env.sh            # exports PATH, CROSS_COMPILE, QNX660, QNX_HOST, QNX_TARGET
tools/env.sh --check      # same checks, changes nothing, non-zero if missing
```

Overrides: `PB_ARM_TOOLCHAIN` for the ARM tree, `QNX660` for the QNX tree.  The
ARM toolchain is relocatable (verified: `arm-linux-gcc -print-sysroot` resolves
inside the copy and it links a static ARM binary); the QNX tree is used through
explicit `QNX_HOST`/`QNX_TARGET` paths, not its own `/opt/qnx660` env script.

**These trees are not in git and are not part of any release.**  QNX SDP 6.6 is
proprietary, and the Buildroot toolchain carries its source-offer obligations —
keep both under `local/` and out of any publication.

Used for `zImage`, the DTB and the static initramfs binaries. The same
`bin/` also supplies `arm-linux-objdump`, `readelf` and `gcc` for RE work and for
the console build (`arm-linux-gcc -static -O2 -o root/console console.c`,
handoff §3). The toolchain is Buildroot 2021.11 — gcc 14.3.0, binutils 2.43.1
(report 103 §4.1).

Host-side build prerequisites (the host is Arch; `pacman -S bc` was the one
missing package, report 103 §4.1): `bc` — required by `kernel/time/timeconst.h`,
without it the build stops at `Kbuild:21` — plus `flex`, `bison`, `pahole`,
`dtc`, OpenSSL and libelf. Host python is 3 with `pyusb` (§6). A full `zImage`
build takes `-j12`-level parallelism; the **XZ** `zImage` is ≈ 6.57 MB and the
assembled chainloader image is 0x91AB20 (inside the `0xB00000` region-0 slot).
With gzip the zImage alone is 9.12 MB and the assembled image (2.88 MB sealed-IFS
base + zImage + DTB + trailer) was **0xB8ABB4**, over the slot — hence XZ.
`Image` ≈ 16.9 MB.

## 2. QNX 6.6 toolchain (builds the chainloader and the QNX-side probes)

Same story as §1: it is at `local/toolchains/qnx660-master`, and `. tools/env.sh`
exports `QNX660`, `QNX_HOST` and `QNX_TARGET` for you.  By hand it is:

```bash
Q=${QNX660:-local/toolchains/qnx660-master}
export QNX_HOST=$Q/host/linux/x86
export QNX_TARGET=$Q/target/qnx6
```

Two different consumers:

* **The chainloader.** `tools/build_kernel_image.py` invokes
  `$QNX_HOST/usr/bin/arm-unknown-nto-qnx6.6.0eabi-{as,ld,objcopy}` with an
  explicit `QNX_TARGET`. It needs `-march=armv7-a` assembly and a plain binary
  output; any ARM assembler/linker would do, but these are the ones the current
  images were built with, so keep them for reproducibility.
* **QNX-side probe tools.** `tools/pbi2c` (from `tools/qnx-i2c.c`), the I²C
  oracle. Rebuild (report 120 §0.1):

```bash
. tools/env.sh
$QNX_HOST/usr/bin/arm-unknown-nto-qnx6.6.0eabi-gcc -O2 -Wall -o tools/pbi2c tools/qnx-i2c.c
```

`tools/build_kernel_image.py` finds the toolchain through `QNX660` (or `Q`), or
through `QNX_HOST`; if none is set it looks in `~/qnx660-master` and otherwise
stops with an instruction.  `tools/get_pristine.sh` needs no external toolchain.

The resulting binary is dynamically linked against `/usr/lib/ldqnx.so.2` on the
unit, so it only runs while TabletOS is up. Anything else you want to ask the
reference must be built and pushed the same way (this is the cheapest oracle the
project has).

## 3. Pristine Linux 7.3.3 tree (the diff baseline; 6.15.11 kept as reference)

**Both tarballs live in the repository payload**, so either patch can be
regenerated and re-verified without depending on anything outside the repo:

```
local/pristine/linux-7.3.3.tar.xz    154.9 MB  (gitignored, like all of local/)
sha256 79c66dde66cd96877fcde8f7d36aec946b1f423d2b97b5a7b1751b75c764f3d1
local/pristine/linux-6.15.11.tar.xz  151 MB
sha256 89ab469fc35bd9cbc6c5bf4e5cb802581806d5cbc14fb47e5c9edcc477e65d93
```

Recreate either tree with the helper (it verifies the payload's sha256 and
reuses an existing extraction):

```bash
tools/get_pristine.sh                     # 7.3.3 -> /tmp/linux-7.3.3
tools/get_pristine.sh 6.15.11             # reference -> /tmp/linux-6.15.11
tools/get_pristine.sh 7.3.3 local/pristine/linux-7.3.3   # or on disk (gitignored)
# the trees are unmodified 7.3.3 / 6.15.11, no PlayBook strings
```

`/tmp` is a tmpfs on the bench host, so the 1.3–1.8 GB extraction costs RAM
unless you give it a disk path.  **Nothing outside this workspace is needed** —
the tarballs in `local/pristine/` are the authoritative copies.  The
pre-extracted tree that used to live at `$HOME/wsl-migration/kernel/pristine` is
no longer part of any procedure.

**Do not diff against `…/kernel/linux`** — that is a *modified* tree from an
earlier phase, so it is neither pristine nor this project's tree
(bootstrap §5). The working kernel tree is in the gitignored payload
(`local/kernel-tree`); it is regenerable from
`kernel/patches/0001-playbook-winchester-7.3.3.patch` (53 files / 7 773 lines)
plus `kernel/winchester-7.3.3.config`.  The 6.15.11 reference is
`kernel/patches/0001-playbook-winchester-6.15.11.patch` (34 files / 5 703 lines)
plus `kernel/winchester.config`.

## 4. RE tools

| tool | used for |
|---|---|
| `rizin` (0.8.2 was used for reports 119/122) | string/section inventory, `iI/iS/izz/ir`, disassembly of QNX ARM `ET_DYN` binaries |
| `arm-linux-objdump` (from the ARM toolchain above) | `-D -b binary -m arm --adjust-vma=0` over the first `PT_LOAD`, which is how the QNX boot set was read (`vaddr == file offset`) |
| `arm-linux-readelf` | `--use-dynamic -r` for GOT/PLT resolution, `-lS` for load addresses |

Reports 122 (appendix A) and 119 (§PIC anchors) contain the reproduction recipes.

## 5. Host permissions: use `pkexec`, not `sudo`

`sudo` requires a password on this host, so the bench scripts use `pkexec`,
which works non-interactively for users in `wheel`:

```bash
ls /dev/ttyACM0 && timeout 15 pkexec chmod 666 /dev/ttyACM0
```

`/dev/ttyACM0` is recreated `root:uucp 0660` on **every** enumeration. The chmod
is needed after every boot, not once per session. USB access for the flash
tools is plain `pyusb` (see §7) and needs no privileges beyond the usual udev
rules for the device.

## 6. Python

Python 3 with `pyusb` — `tools/pre_os_ifs_flash.py`, `tools/legacy/probe_loader.py` and
`tools/force_reset_loader.py` all reach `usb.core` through
`tools/bbusb.py` (ours, tracked; the ramloader blobs it uploads stay in `local/`). The rest of `tools/` is standard library only
(`pbcon.py` uses `termios`, `pb_ring.py`/`pb_events.py` are pure Python). The
host used pyusb 1.3.1 installed user-local (`~/.local`, system pip is PEP
668-managed, so `--user --break-system-packages`); `libusb-1.0` and the
`MODE=0666` udev rule were already present (session-04 §4).

## 7. Where the `local/` payload lives

`local/` is gitignored (see `.gitignore`, whose header comment is the inventory).
It holds everything that is either not ours to redistribute, device-derived, or
regenerable:

| path | contents |
|---|---|
| `local/proprietary/dumped4869ifs/` | the QNX 6.6 boot set (`proc/boot/.script` + 52 binaries), the `etc/` config, used for all RE |
| `local/proprietary/device-binaries/` | other device-derived binaries |
| `local/images/` | `ifs0.sealed.bin` (the sealed region-0 IFS — **never prune**) plus a cache of built `kernel*.img` images.  **Images are a cache, not an archive.**  Keep: the current best, one known-good fallback, and anything an *open* contradiction still needs to re-measure.  The durable record is the CRC plus what that image demonstrated, written in `STATE.md`, the reports and the session notes — an image is only worth keeping while a claim about it is live.  Note builds are **not** byte-reproducible (toolchain/ordering), so a deleted image cannot be recreated exactly; that is the one argument for keeping an old one.  It was pruned on 2026-10-28; the **current** inventory and the image to build from are in `STATE.md` §"Current images". |
| `local/secrets/` | `rsa` / `rsa.pub` (the SSH key for the QNX door), `playbook.ppk` |
| `local/loaders/` | the ramloader blob the flasher uploads (`LDR_01`, RIM material; `bbusb.DEFAULT_LOADERS` resolves it) |
| `local/vendor/` | third-party tooling: `bb10mt*`, `cellebrite-blackberry`, `CFP.exe`, `nvram-*`.  (`bbusb` is ours and lives in `tools/`; the ramloader moved to `local/loaders/`) |
| `local/research/` | RE scratch: `hd0-modemap-2026-10-20`, `deckard-*`, `ghidra-mmc-2026-10-26`, `loader-audit-2026-10-21`, `loader-test-2026-10-25` |
| `local/kernel-tree/` | the kernel build tree (regenerable from the 7.3.3 patch + `kernel/winchester-7.3.3.config`) |
| `local/legacy/` | an earlier, abandoned phase (kept locally, separate from this one) |

`hardware/` (the TI OMAP4430 TRM as text and PDF, the TWL6030 datasheet and the
TC358764 datasheet) is **entirely gitignored and untracked** — nothing there is
committed or published.  A new contributor needs their own copies (SWPU231AP is
a public TI document).  The pad tables, PRCM maps, `COMPLEXIO_CFG1`/`DSI_CTRL`
fields (TRM Table 10-530) and the DISPC timing encodings all come from that TRM,
and every value derived from it is quoted in `docs/reports/` with its table
reference, so the reports are usable without the PDF.

## 8. Paths: what the tools need (read this before running anything in `tools/`)

The tools were **repointed at this workspace** when it was created. None of them
reads or writes the old `rea-work` tree any more (`tools/README.md`, verified by
`grep`): every path derives from the repository root via
`os.path.dirname(os.path.dirname(os.path.abspath(__file__)))` (Python) or
`ROOT=$(dirname "$0")/..` (shell), so a clone works anywhere.
`tools/force_reset_loader.py` — the step that gets the unit out of the ramloader
— is in the tree and imports `bbusb` from `tools/`.

What is still outside the tree, and how each is overridden:

| dependency | used by | override |
|---|---|---|
| ARMv7 cross gcc | kernel, DTB, initramfs binaries | **in the tree**: `local/toolchains/armv7-eabihf`; `. tools/env.sh` sets `PATH`/`CROSS_COMPILE`; `PB_ARM_TOOLCHAIN` overrides (§1) |
| QNX 6.6 toolchain | `tools/build_kernel_image.py` (chainloader), the QNX probe tools | **in the tree**: `local/toolchains/qnx660-master`; `. tools/env.sh` exports `QNX660`/`QNX_HOST`/`QNX_TARGET`; `QNX660`/`Q`/`QNX_HOST` override, and the builder stops with an instruction if none is found (§2) |
| vendor IFS template (signed skeleton) | `tools/pre_os_ifs_flash.py --template-ifs` | `PB_IFS_TEMPLATE`, default `local/images/ifs_template.signed`; a clear error if absent |
| `local/` payload — `loaders`, `local/vendor/bb10mt*`, `images/ifs0.sealed.bin`, `secrets/rsa` | flashing, SSH, the door | `PB_KEY` / `PB_PUB` for the key pair; the rest are fixed relative paths under `local/` |
| device SSH key | `tools/pbssh.sh`, `pb_ring.py`, `pb_events.py`, `BerryShell-V4.py`, `door-hold.sh` | `PB_KEY` / `PB_PUB` (default `local/secrets/rsa[.pub]`) |
| `System.map` | `tools/pb_events.py` symbol names | `PB_SYSTEMMAP`, default `local/kernel-tree/System.map` |

`CONFIG_INITRAMFS_SOURCE` is **repo-relative** in the shipped config
(`"../../initramfs/root"`, resolved from `local/kernel-tree`), so no clone needs
a machine-specific path. `pre_os_ifs_flash.py`'s ramloader directory is resolved by
`bbusb.DEFAULT_LOADERS` (`$BBUSB_LOADERS`, then `tools/loaders`, then `local/loaders`).

History, so nobody "re-fixes" it: the earlier tree hard-coded
`$HOME/rea-work` in `pre_os_ifs_flash.py`, `build_kernel_image.py` and
`pbssh.sh`; the shipped `CONFIG_INITRAMFS_SOURCE` named the `rea-work` initramfs;
and this workspace did not have `force_reset_loader.py` at all.  All of that is
done.
