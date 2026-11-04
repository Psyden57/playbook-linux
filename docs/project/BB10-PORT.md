# BB10 on the PlayBook — working notes

**Goal (operator, 2026-11-03).**  Boot **BB10** on the PlayBook.  The target
BB10 image/version is **not decided yet**.  This file records what the operator
has stated and what must be established from the images; it deliberately
contains **no guesses** about the BB10 OS.

> Every fact in §1 is **operator-supplied**, not measured by this project.  The
> next session must verify anything it relies on, and ask before assuming
> anything else about BB10.

## 1. Operator-supplied facts

* **BB10 is not QNX 6.6 or 7.0 — it reports as QNX 8.**
* **The Z10 is OMAP-based**; its IFS should run on the PlayBook, given the
  **OMAP4460** code is compatible with the **OMAP4430** (or because RIM left
  TabletOS-era code in the BB10 image).
* **Early BB10 versions already run on the PlayBook**: **10.0.9.1103** is
  reported to run fine.  It is too early to be useful, but it proves BB10 can
  run on this hardware, with its newer **RCFS filesystem and IFS**.
* The **BB10 `/base` is an RCFS image**, not a plain QNX6 filesystem:
  `bb10mt qnx6 mount` does **not** work on it.  The expected route is to
  **dump it, edit it, and repack it**.
* A **QNX 8 SDP / toolchain** will be needed to work with BB10.  (The two
  toolchains in `local/toolchains/` are the ARM Buildroot gcc and the QNX 6.6
  SDP; neither is a QNX 8 SDP.)

**SoC check (session 25, web sources — not measured on any BB10 device).**  The
bullets above say "the Z10 is OMAP-based" and name OMAP4460.  The published
model tables do not support "Z10 + OMAP4460": the only OMAP Z10 SKU is
**STL100-1**, and its SoC is **OMAP4470**; the other Z10 SKUs
(STL100-2/-3/-4) are Qualcomm **MSM8960**, and every documented Q10 SKU is
MSM8960 too.  OMAP4460 is the **BB10 Dev Alpha (A)** and the PlayBook 4G LTE;
OMAP4470 is the **Dev Alpha B**.  So "Z10" without a SKU is ambiguous, and
**OMAP4460 describes a Dev Alpha A, not a retail Z10**.  What that means here:

* the OMAP target is either a **Z10 STL100-1 (OMAP4470)** or a **Dev Alpha A
  (OMAP4460)** — two different devices with two different images;
* OMAP4470 is *closer* to the PlayBook's OMAP4430 than a Snapdragon is, but it
  is still a different part (PowerVR SGX544 vs SGX540, different clock tree),
  so "code compatible" remains an operator claim, not a measured fact;
* the target decision must therefore name **device + BB10 version + image
  path**, which is what session 25 asked for and did not get beyond "a Z10
  OMAP build".

Sources: Wikipedia's Z10/Q10/OMAP/PlayBook tables and the Dev Alpha A/B spec
comparison; URLs in the session-25 note.  **No BB10 image has been inspected by
this project yet.**

## 2. What the PlayBook side already has

The PlayBook image chain is proven end to end (reports 169/170,
`IMAGE-REBUILD-GUIDE.md`):

* IFS: our uncompressed 2.1.0.1917 rebuild with the 8,759-byte
  `rfs_validator` shim; `os_type` 6, CRC, `LoadEnd` patched; `dumpifs`-clean.
* RCFS (`/base`): a plain QNX6 filesystem at `0xC00000`, edited with
  `bb10mt qnx6 mount`; the shim mounts it without the signature gate.
* UFS: `PlayBook_2.1.0.1917.5.ufs` **with the operator's `autorun::0` change** at the UFS delta; it skips setup.
* Container/flash: `bbusb.py pack`/`parse`/`flash` (V1+V2, signature, gate);
  hardware-proven; the reference boots the set and `devuser` SSH works.
* The reference is armed and sealed; `BOOTCTR_FCTRY` is topped up by the
  operator's `/accounts/devuser/startup.sh`.

These are the tools to reuse, but **the BB10 image's own layout may differ** —
do not assume the PlayBook deltas, QNX version, signing or filesystem type.

## 3. Open questions (ask the operator / read the image)

1. **Target image** — **answered in part (session 25): Z10 10.3.03.2049
   STL100-1, supplied as components.**  That SKU is **OMAP4470** (see the SoC
   check in §1), so the port is OMAP4470-PMU/board code on an OMAP4430.
2. **The BB10 RCFS format** — **answered (session 25, §4b).**  It is the
   `r-c-f-s` container (`rimh` v2 on the Z10, earlier variant on 10.0.9.1103),
   with its superblock at `0x1000`, 1-based inodes and LZO1X 16 KiB chunks.
   `tools/rcfs_extract.py` reads both fully, including the chunk `bb10mt rcfs
   extract` chokes on.  **Repacking is still open**: a `mkrcfs` that writes
   this format has not been confirmed (the QNX 8.0.0 one is still being
   located; the 6.6 one is not trusted).
3. **The BB10 IFS** — **answered (session 25): it is a QNX imagefs** and the
   QNX 6.6 `dumpifs` reads it; `flags1 = 0xd` (compressed).  Both supplied
   images are `os_type = 4` (product/ECDSA), so a PlayBook experiment needs
   our reseal to `os_type = 6` + CRC.
4. **QNX 8 SDP/toolchain** — is one available, where, and which host binaries
   (`dumpifs`, `mkifs`, `ntoarmv7-gcc`, `mkrcfs`) does it carry?  **The operator
   is looking for the QNX 8.0.0 one (session 25).**
5–7.  See §4b for what the images already answer: the loader/factory fit is
   shown by the 10.0.9.1103 set using the PlayBook container deltas; the
   `/base` gate is `rfs_validator` + `fs-rcfs.so`; the hardware gap is the
   platform branch (`*Winchester*` present in 10.0.9.1103, absent in 10.3).

## 4. Starting procedure for the next session

1. **Ask** which image/version and path; do not pick one silently.
2. Copy the image to `local/work/`; never edit the vendor file.
3. Identify the container and components **read-only** (`bbusb.py parse`,
   `split`, `unpack`; `bb10mt`), and record sizes, hashes, deltas and any
   signature.
4. Identify the IFS and RCFS formats; try the existing readers on **copies**,
   and record exactly which tool understands them.
5. Only then propose the first variable.  The PlayBook chain gives the way back
   (the working 2.1.0.1917 set); no flash until the operator approves.

## 4a. Session 25 (2026-11-03) — read-only preparation

**Decision.** Target = **Z10 10.3.03.2049 STL100-1**.  The components were
supplied later in the session and analysed in **§4b**; the notes below are the
tool survey and the intake procedure that was used (keep them for a new
component set).  Nothing was copied into git, nothing was flashed, and no USB
device was touched.

**Tool surface confirmed present (no image needed to check):**

| need | tool | state |
|---|---|---|
| PlayBook container parse/split/unpack | `tools/bbusb.py parse` / `split` / `unpack` | hardware-proven (reports 169/170) |
| BB10/CFP autoloader + container | `bb10mt autoloader extract` / `loaders`, `bb10mt split`, `bb10mt unpack -c` | present, **untested on a BB10 image** |
| raw flash image → partitions | `bb10mt raw dump` | present, untested |
| RCFS read | `bb10mt rcfs extract -i IMG -o DIR` | reads the BB10 container, but **aborts the tree on one bad chunk** (hit on the Z10 `library.swf`) |
| RCFS read (works) | `tools/rcfs_extract.py list\|extract` | new in session 25; reads **both** supplied BB10 `/base` images fully (0 bad chunks) |
| RCFS build | `mkrcfs <dir> <out>` | present in the QNX 6.6 SDP; **not trusted** (below) |

**The `mkrcfs` caution (operator, session 25).**  The QNX 6.6 SDP ships
`mkrcfs` and the copy inside `local/vendor/bb10mt/` is the **same binary**
(sha256 `410a04a5…`); it does build an image from a directory (a scratch test
produced a valid 20 KiB image).  **The operator instructed that it must not be
trusted and that nothing may be based on it yet**, pending the **QNX 8.0.0**
`mkrcfs`.  So the BB10 RCFS format stays **open**, and no format claim from the
6.6 tool is recorded as fact.

**Fixed next step when the image arrives** (do not re-derive it):

1. copy to `local/work/`; never touch the vendor file;
2. `file`, size, sha256, and any signature/trailer;
3. identify the container: `bb10mt autoloader extract`/`split` for an
   autoloader, `bb10mt raw dump` for a raw flash image, `bbusb.py parse` for a
   PlayBook-style `.signed`;
4. list the components and their deltas; hash each;
5. on **copies**, try `bb10mt unpack`, `bb10mt rcfs extract` and (only once the
   QNX 8 tool exists) the QNX 8 `mkrcfs`/`dumpifs`;
6. record which tool understood which component, and stop.

## 4b. Session-25 analysis of the supplied images (2026-11-03)

**Inputs** (outside the repository, in `/home/psyden/Pdrive/bb10mt/`): the Z10
`10.3.03.2049_STL100-1` component set (`0.0.ifs`, `0.1.rcfs`, `0.2.sig2`,
`0.3.mbr`, `0.4.ufs`, `0.lst`, `0.signed`, `1.signed`, `.exe`) and the
known-running PlayBook set `10.0.09.1103-ROOT-winchester.signed`.  Everything
below is **read-only**; the vendor files were not modified and the extracts
live in the gitignored `local/work/bb10/`.

### Formats (measured)

* **The BB10 IFS is a QNX imagefs.**  The QNX **6.6** `dumpifs` lists the Z10
  image (`0x5A0000` B) and the 10.0.9.1103 image (`0x540000` B) with no change:
  `*.boot` at 0, startup at `0x908`, `flags1 = 0xd` (VIRTUAL|UCL, compressed).
  The Z10 listing is 339 lines.  So the imagefs container is not a QNX 8
  novelty; our emitter/extractor domain applies.
* **Both are product/ECDSA images, not CRC-sealed.**  `os_type = 4` at image
  `+0x100`, no `0xC3D0E193` at `+0x9C`, and the `end-8`/CRC words are zero
  (Z10 CRC of `[0,-4)` is `0xbf18835b` but is not stored).  The reference's
  factory path needs the type-6/CRC form, i.e. our reseal step.
* **The BB10 `/base` is RCFS, and it has a documented layout.**  Not QNX6:
  header `rimh` v2 on the Z10 (absent on 10.0.9.1103) and an `"r-c-f-s"`
  superblock at **`0x1000`** (uuid, magic, `image_size`, `inodes_count`,
  `inodes_off/size` at `0x2000`, `names_off/size`, `data_off/size`).  Inode
  numbers are **1-based**; an inode is 32 B (`mode`, `emode`, `nameoffset`,
  `offset`, `size`, `time`, `uid`, `gid`); a directory's data is an array of
  inodes; file data is LZO1X in 16 KiB chunks with a 32-bit offset table,
  selected by `emode` bit 7.  Z10: `image_size` 367,063,040, 14,926 inodes;
  10.0.9.1103: 280,399,872, 10,780 inodes.
* **`tools/rcfs_extract.py` is new and reads both.**  `list` and `extract`
  (LZO via `liblzo2`).  Z10: **11,911 files, 579 symlinks, 601 MB, 0 bad
  chunks**; 10.0.9.1103: 8,549 files, 448 symlinks.  `bb10mt rcfs extract`
  also parses the container but **aborts the whole tree on one chunk** (its
  failure on `library.swf`, which this reader decodes).

### Boot identity and the platform dispatch (measured)

`proc/boot/.script` in both images: *"Welcome to QNX Neutrino on the Texas
Instruments OMAP4 (ARMv7 Cortex-A9 core)"*, `IFS Name:
winchester-rimboot-secure`, `BOOT_LOADER=RIMBOOT`; it starts `devc-seromap` at
`0x48020000`, `trustzone-omap4`, `nvram_resource_manager` (`/dev/nvram`),
`rpmb-omap4` (`/dev/rpmb`), `bide-omap4`, `devb-sdmmc-rim-omap`, then
`run_rfs_validator`, then `/base/scripts/startup.sh`.

`proc/boot/ifs_variables.sh` derives `PLATFORM` from `uname -m` (split on `_`,
4th field) and branches on the machine string:

| image | branches present |
|---|---|
| Z10 10.3.03.2049 | **`*London*` and MSM only** — no Winchester/Colt |
| 10.0.9.1103 | `*Winchester*` (with `*OMAP4430*`/`*OMAP4460*` sub-branches, `graphics/omap4430`, `devpm-omap4.so`), Henry, Blaze, Spencer, Colt, Rubicon, Tedeschi, London, Longbow, MSM |

**This is the decisive difference.**  The 10.0 image still contains RIM's
PlayBook platform support; the 10.3 image dropped it, along with the libraries
that back it (`libpmconfig-winchester.so.1`, `libpmconfig-winchester2.so.1`,
`adc-colt.conf`, `colt.pm`, `cpuTemp_omap4430.so`, `colt-ledctl`, …).

**10.3 keeps partial Winchester support in `/base`** even though the IFS branch
is gone: `etc/system/config/glo/winchester.conf`,
`etc/system/config/splash_winchester.conf` (a **1024x600** splash, i.e. the
PlayBook panel), `etc/system/config/sensor/sensor_config_winchester.xml`,
`lib/dll/devu-ehci-omap4430-winchester.so`,
`sbin/probe_ehci_udev_winchester`, and deckard's
`usr/lib/python3.2/site-packages/deckard/platform/winchester{,2}.pyc`.  So the
board description survives in the runtime configuration layer; what 10.3
dropped is the **IFS platform branch** and the **omap4430 display HAL**.

### The `/base` gate

Both IFS carry `proc/boot/fs-rcfs.so` (the RCFS resource manager).  The
`rfs_validator` strings are explicit: `mount`, `rcfs`, `/base`,
`/dev/emmc/os%ut179`, `/dev/emmc/radio0t17%u`, `/dev/emmc/dmi0t179`, and the
failures `RCFS UUID mismatch`, `RCFS signature mismatch`, `RCFS length
mismatch`, `RCFS Inodes don't match`, `RCFS names don't match`, `Image is RCFS
formatted`.  So BB10 validates the RCFS and mounts it through `fs-rcfs.so`.
The PlayBook shim technique has a BB10 analogue: replace `rfs_validator` with
a shim that mounts the RCFS via `fs-rcfs.so`.  *(Inferred from strings; the
mount invocation itself is untested.)*

### Graphics configuration (measured)

| image | `graphics.conf` |
|---|---|
| Z10 10.3 (`omap4470`) | `SGX544rev112`, `gpu-dlls … pvrsrv.so`, `libWFDwinchester.so`; winmgr `display internal`, `cursor-type = …`, `pipeline = 3` |
| 10.0.9.1103 (`omap4430`) | `SGX540rev120`, `libWFDomap4.so`, `hdmi=1`; winmgr `display 1`, **`video-mode = 1280 x 720 @ 60`**, `clip-size = 1024 x 600`, `clip-position = 128, 60`, `mtouch driver = pegasus` |
| reference TabletOS 2.1 (`omap4430`) | `SGX540rev120`, `libWFDwinchester.so`, `rotation_dma = simcop`; winmgr `display 1`, **`video-mode = 1024 x 600 @ 60`**, `cursor-shape = …` |

The 10.0.9.1103 `/base` also carries `displayHAL-winchester.so`; the Z10 only
has `displayHAL-london.so`.  **Operator (2026-11-03): the 1280x720 surface with
the 1024x600 clip at (128,60) is the PlayBook's micro-HDMI mirroring path, not
the panel timing.**  Note the winmgr dialect differs between BB10 and TabletOS
(`display internal` vs `display 1`, `cursor-type` vs `cursor-shape`), so the
Z10 conf cannot be dropped onto the PlayBook unchanged.

### Containers (measured)

* **10.0.9.1103 `.signed` uses the PlayBook layout**: `qcfp` deltas 16 (IFS),
  192 (RCFS), 16640 (UFS), 15 (sig), 0 (MBR), 8192 (second MBR); V1+V2;
  UFS 2,550,202,368 B.
* **The Z10 `.0.signed` is V2-only**, with `pfcq` types `0x05` UFS, `0x06`
  MBR, `0x08` IFS, `0x09` RCFS, `0x89` sig2, and a container CRC
  (`0x9A0100B6`).  Our loader **rejects V2-only containers** (trap 55), so any
  Z10-derived set must be repacked V1+V2 with PlayBook deltas.
* The BB10 10.0 root UFS is **not** the TabletOS one: `sha256 163cb508…` versus
  the operator's 1917 `.5.ufs` `0c0f7d77…`, despite the identical length.

### What this implies — route B is chosen

**Decision (operator, 2026-11-03, `DECISIONS.md` §22): backport the Winchester
support from 10.0.9.1103 into the 10.3 IFS and `/base`.**  Route A (boot
10.0.9.1103 first) is moot: its IFS is `os_type = 4` and the operator confirms
that run was on an **unarmed** unit; the reference is armed, so it could not
boot that image until resealed.  Route C (replace the display stack) is the
fallback if 10.3's winmgr rejects the 10.0 HAL.

**The backport surface is small and dependency-clean** (measured, session 25;
manifest in `local/work/bb10/backport-manifest.txt`):

* **IFS (`proc/boot`)** — the Winchester set: `pmic-winchester-twl6030`,
  `devb-mmcsd-winchester`, `phy_ctrl-winchester`, `winchester-bootreason`,
  `hwio-config-winchester{,2}.so`, `libpmconfig-winchester{,2}.so.1`,
  `winchester{,-nocore}.pm`, `winchester2{,-4460}.pm`, `pmic-twl6030*.cfg`,
  `pmic-twl6030-ctl`, `cpuTemp_omap4430.so`, `omap4430-wdtkick`,
  `phy_ctrl_script`; the Colt/PMIC/thermal configs (`adc-colt*`,
  `battmgr-colt.conf`, `colt{,-4460}.pm`, `colt-ledctl`, `hwio-config-colt.so`,
  `libpmconfig-colt.so.1`, `ldo-colt.conf`); `thermal`,
  `thermal-init_script` and the `thermal_win*` configs; and `librocket.so.1`
  (the only library 10.3 lacks).
* **`/base`** — `usr/lib/graphics/omap4430/` (26 files: `displayHAL-winchester.so`,
  `hdmiHAL-winchester.so`, `graphics.conf`, `graphics_winchester.conf`, the
  SGX540 userland and `libWFDwinchester.so`), plus the winchester
  deviceproperties/acoustic/NFC configs as later phases.
* **The IFS branch** — graft the `*Winchester*` block of 10.0.9.1103's
  `ifs_variables.sh` into 10.3's, adapting the `/base` prefix that 10.3 uses
  for `DCONF_GRAPHICS_ROOT` (`/base/usr/lib/graphics/omap4430`).

**Dependency result:** every one of those binaries resolves against the **10.3**
IFS libraries (`libhwio-gen`, `libhwio-omap4`, `libcsm`, `libcam`, `libpps`,
`libslog2`, `libforensics`, libc/libm).  Three plugins (`cpuTemp_omap4430.so`,
`cpuTemp_omap4460.so`, `led-led_rocket.so`) reference `slog2*`,
`thermalVerbose` and `pps_*` symbols they do not link — expected for
`dlopen`ed plugins; verify at run time.

**Packaging: the compressed IFS is solved (session 25).**  The 10.3 imagefs is
**13,737,716 bytes** uncompressed and the PlayBook's region-0 IFS slot is
**0xB00000 = 11,534,336 bytes**, so it must be stored compressed.  Reports
171/172 pinned the format: one chunked NRV2B stream of the whole imagefs
(`[u16 BE length][block]`, 64 KiB blocks) from `0x808 + startup_size` to
`0x808 + stored_size`, with `stored_size = startup_size + C + 8`,
`ram_size = startup_size + imagefs_size`, and an 8-byte trailer after the
payload (`00000000` + `-sum32(payload)`) before RIM's `QNXH-OS-1` block.

The toolchain now does it end to end:

1. `tools/ifs_build.py build … --add/--replace` — emit the new **uncompressed**
   imagefs (it preserves `boot_ino`/`script_ino` from the stock listing);
2. `tools/ucl_nrv2b.py` — the host NRV2B encoder (validated: QNX's own `libucl`
   decodes its output; 13.7 MB → ~5.6 MB, ratio 0.41, ~38 s);
3. `tools/bb10_ifs.py splice --stock … --imagefs … --out …` — compress, splice
   into RIM's preamble/header/stub, patch the three fields and the startup
   trailer, append the payload checksum and keep `QNXH-OS-1`; `verify` walks the
   chain and checks every field;
4. `tools/89-crc_tool.py reseal … --length auto --append-os-trailer --type 6` —
   gives `RESULT: VERIFIED (CRC path)` (`os_type 6`, CRC, `end-8 = 0xD7C82D1F`).

Validated on 10.3.03.2049: the rebuilt image lists the same 339-entry tree with
`dumpifs`, and the resealed image passes the PlayBook's own factory checks
(image `0x5EF044`, CRC `0x9AA0639A`).  A backport test with one added winchester
library and one replaced `ifs_variables.sh` also splices, verifies and reseals.
The only fallback, if a compressed image is ever unwanted, is enlarging the slot
by moving the region-0 RCFS start in `.2.mbr` (no MCT change) — not needed now.

**Then the packaging is the known PlayBook one**: reseal the IFS to
`os_type = 6` + CRC, build the RCFS, pack V1+V2 with deltas 16/192/16640 (the
Z10 container is V2-only and is rejected), and flash only with the operator's
approval after the way back is verified.

Residual unknowns: 10.3 winmgr vs the 10.0 `graphics.conf` dialect
(`cursor-type` vs `cursor-shape`, `display internal` vs `display 1`); whether
10.3's `rfs_validator` finds the RCFS at the PlayBook device path; whether the
10.3 UFS works in `hd3`.

## 4c. The `/base` edit: the deckard OS blocklist (session 25)

**Why.**  The operator requires the mechanism that writes the OS-blocklist
NVRAM record to be removed/invalidated: the verifier reads
`NV_OSSTORE_NotSupportedOS` (`0x2819`), and a BB10 OS writing our version into
it could stop the unit booting.

**Where.**  `usr/lib/python3.2/site-packages/deckard/platform/blacklist.pyc`
(decompiled with `uncompyle6`).  Class `Blacklist`:

* `NV_OSSTORE_NotSupportedOS_NUM = 10265` (`0x2819`), plus the radio record
  `10322`;
* `write()` → `_write()` opens `pfm.nvram()` (`/dev/nvram`) `O_WRONLY` and
  `pwrite()`s the records; `update(pfm)` recomputes them from the class's
  `BLACKLIST` (six `OSRangeAndType` ranges, including a
  `0…0xffffffff` untrusted/RMA/RAM-factory catch-all);
* `read()` is the harmless consumer and is kept.

`sbin/blupdater` (native) has its **own** blacklist writer ("update the
os/radio blacklist … NVRAM"); it is an update-time tool, not a boot path, and
is noted rather than edited.

**The edit.**  `update`, `write` and `_write` are replaced with no-ops
(`return False`, `return 0`, `return 0`); everything else (the error classes,
`read`, the constants) is unchanged.  It was compiled with a **portable
CPython 3.2.6** built in the session scratchpad (the device's `python3.2` is
ARM), producing a 7,849-byte `.pyc` with the same magic (`6c0c0d0a`), and
loaded back with `marshal.loads` to prove validity.  The RCFS is edited **in
place**: inode 12,015 at `0x105c8938` (LZO, one 16 KiB chunk), recompressed
with `lzo1x_999` to 2,957 bytes (the slot allows 3,324), `inode.size` set to
7,849, and the readback decodes byte-identically.

**The second `/base` change — the setup seed.**  The `autorun::0` change is
needed in **two** places, as on TabletOS: the UFS
(`var/pps/system/navigator/config`, one occurrence, `autorun::1` → `::0`)
**and** the RCFS seed at `seed/rootfs/var/pps/system/navigator/config`, which
the first boot uses to seed `var/`.  Missing the seed copy would silently undo
the UFS edit on first boot.  The RCFS copy (inode 3,771 at `0x37386ec`, 693 B,
one LZO chunk) was recompressed 431 → 432 B (slot 440) and its value changed to
`autorun::0`; both files were decoded back to verify.

**The `/base` startup hook (operator request).**  `/base/scripts/startup.sh`
now runs our own script after the final runlevel, as in the 1917 `/base`
(report 170):

```sh
		# session 25: run our own per-boot commands after the final runlevel
		if [ -x /accounts/devuser/startup.sh ]; then
			/accounts/devuser/startup.sh &
		fi;
```

The 10.3 script has **no literal `start_runlevel FINAL`**: `main()` loops
`${RUNLEVELS}` (whose profile list ends with `final`), so the hook goes after
that loop, before the bmetrics rotation.  The file's LZO slot had only **2 bytes
of slack**, so 26 decorative separator comment lines were removed to keep the
recompressed extent at **57,382 B** (slot 57,414); `bash -n` passes on both the
stock and the edited script.  Inode 1,648 at `0x2e9a21c`, 200,849 → 198,900 B.
The hook is backgrounded and guarded, so a missing `/accounts/devuser` (BB10's
accounts are `/accounts/1000`) is harmless; the operator can create the script
on the UFS when needed.

**Consequence.**  The RCFS verifier hash table (and its signature) no longer
matches, so the stock `rfs_validator` must not run.  `tools/rfs_rcfs_shim.c`
replaces it: it probes `/dev/emmc/os0t179`, `os1t179`, `/dev/hd1t179`,
`hd2t179` and `mount(dev, "/base", 0, "rcfs", NULL, 0)` (EBUSY = success),
using the `fs-rcfs.so` already in the IFS.

**Built set (offline, not flashed).**

| component | value |
|---|---|
| IFS | `local/work/bb10/z10-win.sealed.ifs`, 8,555,080 B, `VERIFIED (CRC path)`, sha256 `50e1c04a…` |
| `/base` | `local/work/bb10/z10-base-edited.rcfs`, 367,067,136 B, sha256 `88711f9e…` — the deckard blocklist writer neutered, the seed `autorun::0`, and the startup hook |
| UFS | the Z10 `10.3.03.2049` `.0.4.ufs` **with `autorun::0`** — one change in `var/pps/system/navigator/config` (`autorun::1` → `autorun::0`, the setup skip); 2,588,409,856 B, sha256 `8bc7c13f…` |
| container | `local/work/bb10/bb10-set.signed`, 2,964,324,944 B, V1+V2, signed, container CRC `0xFBA1980E` OK, sha256 `d92ee639…` |
| dry-run gate | accepted: second MBR 0x2815, RCFS 0x8D5..0x1EB5, MBR 0x815, sig 0x824, IFS 0x825..0x8A7, UFS 0x4915..0xE35C — all inside the measured MCT extents |
| IFS contents | 395 dirents: the 10.3 tree, the `*Winchester*` branch, the shim, the winchester set, and `proc/boot/gfx4430/` (the SGX540 stack) |

**Not flashed.**  The next action is a device write and needs the operator:
flashing replaces region 0 (IFS + edited RCFS) and the whole UFS partition
(hd3), so the way back must be the verified 2.1.0.1917 set through the pre-OS
`0xF7` loop.  One boot then costs one `BOOTCTR_FCTRY` unit (currently 50).

**Safety choice for the first image:** the Winchester branch disables
`DCONF_PMIC_DISABLED`, `DCONF_LDO_DISABLED` and `DCONF_THERMAL_DISABLED` — the
boot chain leaves the rails in a sane state, and an invented regulator write is
worse than none.  Enabling them is a later, evidence-backed step.

## 5. Safety

The reference is the only working unit.  `SAFETY.md` applies: no RPMB writes by
an agent, no MCT/NVRAM writes, one variable per flash, and the way back verified
before anything is written.  A BB10 experiment that cannot be reverted is not a
valid first step.
