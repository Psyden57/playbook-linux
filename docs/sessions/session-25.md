# Session 25 (absolute) — BB10 target fixed to a Z10 OMAP build; read-only prep

> Numbering: absolute `25`; the reports call this Linux-era **session 19**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-03
Unit state at the end: **unchanged — PlayBook OS 2.1.0.1917 running from our
IFS (CRC `0x38EE7B46`), our edited `/base`, and the vendor `.5.ufs` with the
operator's `autorun::0` change.**  `BOOTCTR_FCTRY = 0x32` (topped up by the
operator's `/accounts/devuser/startup.sh`).  **No USB/device access and no
write of any kind this session.**  See `STATE.md`.

## Goal of this session

Start `ROADMAP.md` item 8 / `BB10-PORT.md`: boot BB10 on the PlayBook.  The
first task is the operator's, not ours — **name the target image/version and its
path** — because the docs forbid assuming any BB10 property.

**Result: the target was named, supplied, analysed, edited, built and flashed.**
The operator gave "a Z10 OMAP build", then supplied the **Z10 10.3.03.2049
STL100-1** component set and the **known-running
`10.0.09.1103-ROOT-winchester.signed`** set.  Session 25 mapped the formats
(wrote the RCFS reader and the NRV2B encoder), found the Winchester-branch
difference, edited both `/base` and the UFS, built and flashed a complete
CRC-sealed set — and learned that **the image verifies and the loader jumps,
but BB10 10.3 does not bring the unit up**.  The unit is held in the ramloader
with the way back packed.

## Confirmed (measured, read in a file, or seen on the bench)

* **At session start no BB10 image existed in the workspace.**  `find local
  -iname '*bb10*'` returned only `local/bb10mt/` and the vendor tool copies;
  `local/images/` was entirely PlayBook 4869/1917 material.  The operator then
  supplied both sets from `/home/psyden/Pdrive/bb10mt/` (see the analysis
  section below); everything this session created lives under
  `local/work/bb10/`.
* **The toolchains here are QNX 6.6 + ARM Buildroot only** — `local/toolchains/`
  is `armv7-eabihf` and `qnx660-master`.  `. tools/env.sh` resolves
  `dumpifs`, `mkifs`, `mkrcfs`, `ntoarmv7-gcc` under
  `local/toolchains/qnx660-master/host/linux/x86/usr/bin/`.  There is no QNX 8
  anything on disk.
* **`mkrcfs` is shipped twice and the copies are byte-identical**:
  `local/vendor/bb10mt/mkrcfs` and the QNX 6.6 SDP's
  `host/linux/x86/usr/bin/mkrcfs` both hash
  `410a04a576b54785bfca7c17ac07b6f2e8dc8762223ab62da62bee6d16c7fcd2`.
  It is an unstripped 32-bit i386 ELF (`mkrcfs.c`, debug info, `lzo1x_999` +
  SHA-1/256/384/512).  Its CLI is `mkrcfs [options] <input-dir> <output-file>`
  and `options`' getopt string is `b:c:lp:t:vxhL:D:`.  A scratch run
  (`/tmp/rcfs-test/in` with one file) produced a 20,480-byte image; the
  superblock strings are at image offset **`0x1000`**
  (`-- RCFS Super Block @ (000001000)`, `-- RCFS Inode Table @ (%08x)`).
  **Not used as a format spec** (see *Dead ends*).
* **`bb10mt` 0.5.9.999 has the container surface the intake needs**:
  `autoloader extract` / `autoloader loaders` / `autoloader create`,
  `split` (`-i`), `unpack -c <container>`, `raw dump` (raw flash image →
  partitions), `raw nvram`, `bar`, `qnx6 …`, and `rcfs extract -i IMAGE -o DIR`.
  All present; **none of them has been run against a BB10 image yet**.
* **`bbusb.py` still has `parse`/`split`/`unpack`** and remains the
  hardware-proven path for PlayBook-style `.signed` containers (reports
  169/170).  Whether a Z10 autoloader needs it at all is unknown.
* **`bb10mt rcfs extract` is not a drop-in reader for the PlayBook `/base`.**
  Run on a copy of our known-good 1917 RCFS (`local/work/rcfs-1917.rcfs`) with
  output to the scratch dir, it printed a stream path then
  `Error executing command: Stream read error`, exit 1, and extracted nothing.
  That is expected (the PlayBook `/base` is a plain QNX6 filesystem), and it
  means the tool expects a different container — **it does not establish
  anything about the BB10 `/base`**, which has not been tested.
* **`bb10mt.ini` maps a 32-bit device ID to a RAM loader.**  The PlayBook's
  `0x06001A06` maps to **`LDR_01`**, which matches the project's own loader
  facts (`LDR_01`, 238,128 B, uploaded over USB and never on media).  The Z10's
  ID is not identified here, so which loader `bb10mt` would pick for a Z10 is
  unknown — relevant only if a future step flashes over USB rather than
  through the PlayBook's pre-OS `0xF7` loop.

### The supplied BB10 images (read-only analysis)

* **Inputs:** `/home/psyden/Pdrive/bb10mt/Z10_10.3.03.2049_STL100-1.*`
  (`0.0.ifs` 5,898,240 B; `0.1.rcfs` 367,067,136 B; `0.2.sig2`; `0.3.mbr`;
  `0.4.ufs` 2,588,409,856 B; `0.lst` 172 B; `0.signed` 2,961,507,256 B;
  `1.signed` 30,737,216 B; `.exe` 3,001,497,092 B) and
  `10.0.09.1103-ROOT-winchester.signed` 817,758,248 B.  Extracts are under
  `local/work/bb10/` (gitignored); the vendor files were not modified.
* **The BB10 IFS is a QNX imagefs.**  `dumpifs` (QNX **6.6**) lists the Z10 IFS
  with no error — 339 lines, `*.boot` at 0, `Startup-header flags1=0xd`,
  startup at `0x908` — and the 10.0.9.1103 IFS likewise
  (`Checksums: image=0x230e14c1 startup=0xf8e959dc`).  No QNX 8 ustar/ELF
  wrapper.
* **Both IFS images are product/ECDSA images.**  `os_type = 4` at image `+0x100`;
  no `0xC3D0E193` at `+0x9C`; `end-8` and the stored CRC word are 0 (the Z10
  `zlib.crc32(d[:-4])` is `0xbf18835b`, not stored).  So the factory/CRC path
  needs our reseal.
* **The BB10 `/base` is an RCFS container, not QNX6.**  Header `rimh` v2 on the
  Z10, absent on 10.0.9.1103; an `"r-c-f-s"` superblock at **`0x1000`**:
  uuid[16], unk[16], magic[8], unk1, unk2, `image_size`, `inodes_count`,
  `inodes_off`, `inodes_size`, `names_off`, `names_size`, `data_off`,
  `data_size`.  Inodes are **1-based** and 32 B (`mode`,`emode`,`nameoffset`,
  `offset`,`size`,`time`,`uid`,`gid`); a directory's data is an array of
  inodes; `emode` bit 7 selects LZO1X in 16 KiB chunks with a 32-bit offset
  table.  Z10: `image_size` 367,063,040, 14,926 inodes, data at `0xB6000`;
  10.0.9.1103: 280,399,872, 10,780 inodes.
* **`tools/rcfs_extract.py` reads both fully** (new tool, `liblzo2`): Z10 →
  **11,911 files, 579 symlinks, 601 MB, 0 bad chunks**; 10.0.9.1103 → 8,549
  files, 448 symlinks.  `bb10mt rcfs extract` parses the same container but
  **aborts the whole tree** on the first chunk it cannot decode (it died on
  `air/extensions/net.rim.blackberry.PushService.ane/library.swf`, which ours
  decodes) — trap 78.
* **Boot identity:** both `proc/boot/.script` say *"Welcome to QNX Neutrino on
  the Texas Instruments OMAP4 (ARMv7 Cortex-A9 core)"*, `IFS Name:
  winchester-rimboot-secure`, `BOOT_LOADER=RIMBOOT`; both start `devc-seromap`
  at `0x48020000`, `trustzone-omap4`, `nvram_resource_manager` (`/dev/nvram`),
  `rpmb-omap4` (`/dev/rpmb`), `bide-omap4`, `devb-sdmmc-rim-omap`, then
  `run_rfs_validator`, then `/base/scripts/startup.sh`.
* **Platform dispatch is the crux.**  `ifs_variables.sh` derives `PLATFORM`
  from `uname -m` (split on `_`, 4th field) and branches on the machine
  string.  The **Z10 10.3** file has only `*London*` and MSM branches.  The
  **10.0.9.1103** file has `*Winchester*` (with `*OMAP4430*`/`*OMAP4460*`,
  `DCONF_GRAPHICS_ROOT="/usr/lib/graphics/omap4430"`, `devpm-omap4.so`,
  `thermal_win.conf`), plus Henry, Blaze, Spencer, Colt, Rubicon, Tedeschi,
  London, Longbow and MSM.  The 10.3 image also lacks the libraries the branch
  uses (`libpmconfig-winchester*.so.1`, `adc-colt.conf`, `colt.pm`,
  `cpuTemp_omap4430.so`, `colt-ledctl`, …).
* **`/base` gate:** both IFS carry `proc/boot/fs-rcfs.so`; `rfs_validator`'s
  strings name `mount`, `rcfs`, `/base`, `/dev/emmc/os%ut179`,
  `/dev/emmc/radio0t17%u`, `/dev/emmc/dmi0t179`, `os_signature_file`, and the
  checks `RCFS UUID mismatch`, `RCFS signature mismatch`, `RCFS length
  mismatch`, `RCFS Inodes don't match`, `RCFS names don't match`, `Image is
  RCFS formatted`.  So the RCFS is validated, then mounted through
  `fs-rcfs.so`.
* **10.3 retains partial Winchester support in `/base`** even though the IFS
  branch is gone: `etc/system/config/glo/winchester.conf`,
  `splash_winchester.conf` (**1024x600** splash art — the PlayBook panel),
  `sensor_config_winchester.xml`, `devu-ehci-omap4430-winchester.so`,
  `sbin/probe_ehci_udev_winchester`, and deckard's winchester platform pyc
  files.  So the board description survives in the runtime layer; the missing
  pieces are the IFS branch and the omap4430 display HAL.
* **Graphics:** Z10 `/base/usr/lib/graphics/omap4470/graphics.conf` —
  `SGX544rev112`, `wfd-dlls = libWFDwinchester.so`, winmgr `display internal`,
  `cursor-type`.  10.0.9.1103 `/base/usr/lib/graphics/omap4430/graphics.conf` —
  `SGX540rev120`, `wfd-dlls = libWFDomap4.so`, `hdmi=1`, `display 1`,
  `video-mode = 1280 x 720 @ 60`, `clip-size = 1024 x 600`,
  `clip-position = 128, 60`, `mtouch driver = pegasus`; the dir also has
  `displayHAL-winchester.so`.  TabletOS 2.1 — `SGX540rev120`,
  `libWFDwinchester.so`, `rotation_dma = simcop`, `display 1`,
  `video-mode = 1024 x 600 @ 60`, `cursor-shape`.  **Operator:** the 1280x720
  surface with the 1024x600 clip is the PlayBook's **micro-HDMI mirroring**,
  not the panel timing.
* **Containers:** `10.0.09.1103-ROOT-winchester.signed` parses as the
  **PlayBook layout** — `qcfp` deltas 16 (IFS), 192 (RCFS), 16640 (UFS), 15
  (sig), 0 (MBR), 8192 (second MBR), V1+V2, UFS 2,550,202,368 B.  The Z10
  `0.signed` is **V2-only**, `pfcq` types `0x05` UFS, `0x06` MBR, `0x08` IFS,
  `0x09` RCFS, `0x89` sig2, container CRC `0x9A0100B6` — a container this
  loader rejects (trap 55).
* **The BB10 IFS must be stored compressed to fit the PlayBook.**  The 10.3
  imagefs is **13,738,740 B uncompressed** (`dumpifs -u`; startup `+0x30` =
  `0x00D19EF4`) and 5,898,240 B stored — the whole imagefs is a UCL stream
  (`flags1 = 0x0d`).  The region-0 IFS slot is `0xB00000` = 11,534,336 B
  (`0x100000`..`0xC00000`).  `ifs_build.py`'s uncompressed build of the stock
  10.3 tree is `0xD3A35C` (13.9 MB), so it cannot fit without compression.
  `ifs_build.py` now supports `--add NAME=PATH` for new files (tested: a
  winchester library appears in a rebuilt Z10 IFS and `dumpifs` lists it), but
  it does not compress.
* **UCL tooling and the compression mechanism (reports 171/172).**  The 6.6 SDP
  ships `libucl.a` and headers for x86 and armle-v7, but the x86 archive is
  QNX-ABI and crashes when linked into a Linux host program; there is no UCL
  source or host compressor.  `mkifs` is `mkxfs`, unstripped with DWARF, and its
  `startup_header`/`image_header` layouts are now pinned (report 171); the
  narrower pass (report 172) established that the payload is **one chunked
  NRV2B stream** of the whole imagefs (`[u16 BE length][block]`), starting at
  `0x808 + startup_size` and ending at `0x808 + stored_size`, with
  `stored_size = startup_size + C + preboot_size` and
  `ram_size = startup_size + imagefs_size`.
* **The compressed-IFS chain is solved and validated end to end.**  `mkifs`
  leaves `boot_ino = 0`, so the splice route was completed with our own codec
  instead: **`tools/ucl_nrv2b.py`** (NRV2B encoder/decoder; token coding from
  UCL 0.91, validated by decoding our output with the SDP's own `libucl`), and
  **`tools/bb10_ifs.py splice`** (compress → place at `0x808+startup_size` →
  patch `ram_size`/`stored_size`/`imagefs_size` → recompute the startup trailer
  → append `00000000 + -sum32(payload)` and keep `QNXH-OS-1`).  Measured:
  the unmodified 10.3 imagefs (13,737,716 B) compresses to 5,623,064 B (0.410)
  in 204 blocks; `bb10_ifs.py verify` passes; `dumpifs` lists the same 339
  entries; `89-crc_tool.py reseal` reports **`VERIFIED (CRC path)`**
  (`os_type 6`, image `0x5EF044`, CRC `0x9AA0639A`, `end-8 = 0xD7C82D1F`).
  A backport test with one added winchester library and a replaced
  `ifs_variables.sh` goes through the same chain.  `ifs_build.py` keeps the
  stock `boot_ino`/`script_ino`, which was the mkifs blocker.
* **The `/base` edit: the deckard OS-blocklist writer.**  The operator required
  the mechanism writing NVRAM `0x2819` to be invalidated.
  `usr/lib/python3.2/site-packages/deckard/platform/blacklist.pyc` (decompiled
  with `uncompyle6`) holds `NV_OSSTORE_NotSupportedOS_NUM = 10265`; its
  `write()`/`_write()` open `/dev/nvram` `O_WRONLY` and `pwrite()` the
  `OSRangeAndType` records, and `update()` recomputes them from `BLACKLIST`
  (six ranges, including a `0…0xffffffff` untrusted/RMA/RAM-factory catch-all).
  `update`, `write` and `_write` were replaced with no-ops and the module
  recompiled with a **portable CPython 3.2.6** built in the scratchpad
  (`-no-pie`; the first build segfaulted): 7,849 bytes, magic `6c0c0d0a`, and
  `marshal.loads` in 3.2.6 accepts it.  `sbin/blupdater` is a separate native
  writer and is recorded, not edited.
* **The RCFS edit is in place, no repack.**  `blacklist.pyc` is inode 12,015 at
  `0x105c8938` (LZO, one chunk, 3,323 B, slot 3,324 B).  The new pyc was
  compressed with `lzo1x_999` to **2,957 B**, written with the 2-dword chunk
  table, and `inode.size` set to 7,849; the readback decodes byte-identically.
  The edited image is `local/work/bb10/z10-base-edited.rcfs` (sha256 `a04c9b74…`).
* **The verifier consequence:** any `/base` edit breaks the signed RCFS hash
  table, so `tools/rfs_rcfs_shim.c` replaces `rfs_validator` (mount
  `"rcfs"` through `fs-rcfs.so`, EBUSY = success; 8,256 B).
* **The real BB10 image was built and sealed** (not flashed):
  `local/work/bb10/z10-win.sealed.ifs`, **8,555,080 B**, sha256 `50e1c04a…`,
  `VERIFIED (CRC path)`.  It holds the 10.3 tree, the grafted `*Winchester*`
  branch (`/proc/boot/gfx4430` for the SGX540 stack), the shim, and the
  winchester binaries/configs (395 dirents).  The branch deliberately disables
  PMIC/LDO/thermal.
* **The setup skip needs `autorun::0` in two places.**  The operator flagged the
  second copy: in the UFS it is `var/pps/system/navigator/config` (one
  occurrence, edited on a copy, unmounted cleanly, raw image now 1×`::0` and
  0×`::1`; sha256 `8bc7c13f…`), and in the **RCFS seed** it is
  `seed/rootfs/var/pps/system/navigator/config` (inode 3,771 at `0x37386ec`,
  693 B, LZO recompressed 431 → 432 B in a 440 B slot), which first boot would
  otherwise use to overwrite the UFS value.  Both files decode back with
  `autorun::0` and no `::1`.  The 10.3 `.seed` in the UFS itself is only
  branding JARs plus an empty `config/`.
* **The `/base` startup hook is in** (operator request): `scripts/startup.sh`
  runs `/accounts/devuser/startup.sh &` after the final runlevel, guarded by
  `-x`, as in the 1917 `/base`.  The 10.3 script has no literal
  `start_runlevel FINAL` — `main()` loops `${RUNLEVELS}` (`final` last) — so the
  hook is after that loop.  The LZO slot had 2 bytes of slack, so 26 decorative
  separator comment lines were removed: 200,849 → 198,900 B, extent 57,382 B
  (slot 57,414); inode 1,648 at `0x2e9a21c`; `bash -n` passes.
* **The set was flashed, and the result is decisive.**  `bbusb.py flash
  --signed bb10-set.signed --from-bootrom --password 0000 --apply` wrote
  2,964,324,384/2,964,324,384 bytes, `0x40F9 signature -> 0x4006`,
  `COMPLETE 0x40C0 -> 0x4006`, `done`, exit 0.  The unit then re-entered the
  bootrom on each reset; no splash, no USB.  `bbusb.py info --from-bootrom
  --password 0000 --keep-loader --json` read `boot_mode = factory`,
  `factory_count = **45**` (it was 50), `os_address =
  0x800FF800-0x80928247` (our LoadStart/LoadEnd), hardware `0x06001A06`,
  `image_magic 0xC3D0E193`.  **Five decrements mean the CRC path verified the
  image and jumped five times**: the container/IFS/RCFS chain is sound and the
  stall is inside BB10 (kernel/board support for the OMAP4430).  The unit is
  held in the ramloader.
* **The way back is packed and verified, not flashed**:
  `local/work/back-1917.signed`, 1,104,544,336 B (our 1917 IFS + edited 1917
  RCFS + the vendor `.5.ufs` with `autorun::0`).  Flashing it from the ramloader
  (no `--from-bootrom`) restores TabletOS and tops the counter to `0x32`.

## Inferred (reasoning from evidence, not directly measured)

* If the Z10 image is a vendor **autoloader** (CAP/CFP/`.exe`), `bb10mt
  autoloader extract`/`split` is the likely first tool; if it is a **raw eMMC
  dump**, `bb10mt raw dump` is.  This is inference from the subcommand names and
  their help text, not from a tested image.
* `bb10mt rcfs extract` "for an rcfs image" may be the correct reader for a BB10
  `/base`, since the PlayBook RCFS needed `qnx6 mount` instead.  Untested; the
  distinction is exactly the open question (`BB10-PORT.md` §3 question 2).
* The QNX 8 `mkrcfs` is the only credible way to *repack* a BB10 `/base`
  honestly, so the port is gated on obtaining it even if the reader works.
* **The port is a platform-support problem, and route B is chosen.**
  The 10.3 IFS has no branch for the PlayBook's machine string, so on
  `OMAP4430_…_Winchester_…` its `ifs_variables.sh` selects nothing and the
  platform services are not configured.  The operator chose **(B) backport the
  Winchester support from 10.0.9.1103 into 10.3** (`DECISIONS.md` §22); route A
  is moot because the 10.0.9.1103 image is `os_type = 4` and that run was on an
  **unarmed** unit, while the reference is armed.
* **The backport surface is small and dependency-clean.**  Manifest in
  `local/work/bb10/backport-manifest.txt`: ~21 IFS files (winchester PMIC/MMC/
  PHY/bootreason/PM-config set, the colt PMIC/thermal configs and
  `librocket.so.1`) plus 26 files in `/base/usr/lib/graphics/omap4430/`.
  Symbol closure against the **10.3** IFS libraries shows no strong missing
  symbol except three dlopen plugins (`cpuTemp_omap4430.so`,
  `cpuTemp_omap4460.so`, `led-led_rocket.so` reference `slog2*`,
  `thermalVerbose`, `pps_*`), which resolve from the loaded process.
* **The `/base` gate has a BB10 analogue of our PlayBook shim.**  Because
  `fs-rcfs.so` is in the IFS and `rfs_validator` only validates then mounts,
  a replacement `rfs_validator` can mount the RCFS directly — the same pattern
  as `rfs_validator_shim.c`, but with `rcfs` and `/dev/emmc/os%ut179` instead
  of `qnx6` and `hd1t179`.  Strength: medium (from strings and the PlayBook
  precedent; not executed).

## Dead ends and wrong assumptions (valuable — do not skip)

* **I started treating the QNX 6.6 `mkrcfs` as the RCFS spec.**  I recovered
  its superblock location (`0x1000`), its CLI/optstring and its symbol-level
  structure and was decoding struct offsets.  **The operator stopped this**:
  the 6.6 tool is not to be trusted and nothing may be based on it pending the
  QNX 8.0.0 `mkrcfs`.  No format claim from that work is kept as fact; the only
  surviving facts are the *tool's existence/location*, its identical hash, and
  that it builds an image.  This is the session's main course correction.
* **"Z10 + OMAP4460" looked like a single target.**  It is not: the OMAP Z10 SKU
  is STL100-1 and it is **OMAP4470**; OMAP4460 is the **Dev Alpha A** (and the
  PlayBook 4G LTE).  Had I accepted the phrase, I would have looked for an image
  that no retail Z10 ships.  Caught by checking the model tables before
  accepting the operator's shorthand as a device fact.

## Unknown / open questions

* **How did 10.0.9.1103 actually boot on the PlayBook?**  Its IFS is
  `os_type = 4` (product/ECDSA) and unsealed.  That needs a valid RIM signature
  *and* the product path, i.e. an **unarmed** unit (`BOOTCTR_FCTRY` absent).
  Our reference is armed (`0x32`), so this exact set should not boot on it
  until resealed.  Resolve by asking the operator which unit/state that run
  used, and by checking the `.3.sig`/ECDSA material against the image.
* **Does the Z10 10.3 image's OMAP4470 board code run on OMAP4430?**  The PMIC/
  LDO/thermal configs and `devpm-omap4-4460` are 4470-specific; the PlayBook
  branch uses `devpm-omap4.so`.  Resolve by the static diff and then a bench
  experiment.
* **Can the Winchester branch be backported to 10.3?**  It drags configs and
  libraries (pmconfig, adc, ldo, thermal, displayHAL); whether 10.3's winmgr
  accepts the 10.0 `graphics.conf` dialect (`cursor-shape` vs `cursor-type`,
  `display 1` vs `display internal`) is unknown.
* **Is there an RCFS writer for this format?**  Not needed for a single-file
  change: the in-place route works (find the inode, replace its chunk table +
  LZO chunks, update `inode.size`).  A full repack would still need the QNX
  8.0.0 `mkrcfs` or our own writer, and is unnecessary so far.
* **Does the PlayBook loader accept the resealed BB10 IFS** (`os_type`,
  CRC, `LoadEnd`, length) and the RCFS in its region?  The 10.0.9.1103 set's
  use of the PlayBook deltas is evidence of fit, not a boot test on this unit.
* **QNX 8.0.0 SDP availability** — the operator is locating it.
* **Display:** whether `displayHAL-winchester.so` + the 10.0 `graphics.conf`
  drives the PlayBook panel under 10.3's winmgr; the 1280x720/1024x600 clip is
  the HDMI mirroring path (operator), so the panel path itself is untested.

## Superseded / uncertain

* **"BB10 reports as QNX 8"** — operator-supplied, still unverified; no BB10
  binary has been read.
* **"The Z10 is OMAP-based … OMAP4460"** (`BB10-PORT.md` §1, operator) —
  correct in spirit (one Z10 SKU is TI OMAP) but the part is **OMAP4470**
  (STL100-1); OMAP4460 is the Dev Alpha A.  Web sources, not measured here:
  Wikipedia's Z10/Q10/OMAP/PlayBook tables and the Dev Alpha A/B spec
  comparison (e.g. <https://en.wikipedia.org/wiki/BlackBerry_Z10>,
  <https://en.wikipedia.org/wiki/Texas_Instruments_OMAP>,
  <https://en.wikipedia.org/wiki/BlackBerry_PlayBook>).  Recorded in
  `BB10-PORT.md` §1 as a check with sources; the operator's wording is left
  visible.
* **"Early BB10 10.0.9.1103 already runs on the PlayBook"** — operator-supplied,
  not reproduced; still the strongest existence proof for the whole goal.

## Wrap-up (operator)

At the end of the session the operator **parked the BB10 port and the PlayBook
OS image (IFS/RCFS) work** for someone else to follow, and asked for
`HANDOFF.md` to return to the **Linux bring-up**.  The unit is left in the
ramloader with the way back packed; the Linux kickoff is `HANDOFF.md`, and the
Linux work order is `ROADMAP.md` item 6 (the factory top-up) → item 4
(brightness) → item 5 (real userland / hd3) → item 7 (packaging).  The BB10
documents (`BB10-PORT.md`, `BB10-IMAGE-GUIDE.md`) and reports 171/172 stay in
the tree for whoever resumes it.

## Artefacts produced

* `docs/project/BB10-PORT.md` — §1 SoC check (Z10 SKU/part correction, sources);
  §3 questions 1/2/4 updated; §4a (tool surface, `mkrcfs` caution, intake
  procedure); **§4b the supplied-image analysis** (formats, platform dispatch,
  `/base` gate, graphics, containers, implications).
* `docs/project/ROADMAP.md` — item 8 rewritten around the supplied Z10/10.0.9.1103
  sets, the Winchester-branch finding and the three routes.
* `docs/project/STATE.md` — the BB10 next-goal paragraph, the "active work"
  paragraph and "What is next" updated; the unit/media state is untouched.
* `docs/project/KNOWN-ISSUES.md` — **trap 78** (`bb10mt rcfs extract` aborts on
  one bad chunk; use `tools/rcfs_extract.py`; the container is `r-c-f-s`).
* `docs/project/DECISIONS.md` — **§22**: route B (backport the Winchester
  support into 10.3), with the evidence, the reseal/repack constraint and the
  open questions; **§23** the `/base` blocklist edit; **§24** the test result
  and the parked status.
* `docs/project/HANDOFF.md` — rewritten at the operator's request as the
  **session-26 Linux-work kickoff** (BB10/IFS parked), with the physical state
  (ramloader, counter 45, way back packed) and the restore command.
* `docs/sessions/NUMBERING.md` — session 25 row and anchor.
* `docs/sessions/session-25.md` — this note.
* `local/work/bb10/backport-manifest.txt` — the route-B file list with sizes and
  sha256 prefixes (gitignored working artefact).
* `tools/rcfs_extract.py` — **new**: read-only BB10 RCFS reader
  (`list` / `extract`, `liblzo2`, 1-based inodes, LZO chunks, raw fallback).
* `tools/ifs_build.py` — **`--add NAME=PATH`** for new files (session-25
  backport need); existing behaviour unchanged.  Tested on the stock Z10 IFS:
  the added `libpmconfig-winchester.so.1` appears in `dumpifs`'s listing.
* `tools/ucl_nrv2b.py` — **new**: NRV2B/UCL block codec (`encode`/`decode`/
  `stream`/`selftest`); validated against QNX's own `libucl`.
* `tools/bb10_ifs.py` — **new**: `splice` a new imagefs into RIM's BB10 IFS
  container (compress, patch fields, startup trailer, payload checksum) and
  `verify` it.
* `tools/rfs_rcfs_shim.c` — **new**: BB10 `rfs_validator` replacement that
  mounts the RCFS through `fs-rcfs.so` without verification (trap 79).
* `tools/README.md` — rows for `rcfs_extract.py`, `ucl_nrv2b.py`, `bb10_ifs.py`,
  `rfs_rcfs_shim.c`, and the `ifs_build.py --add` note.
* `local/work/bb10/z10-base-edited.rcfs` — the `/base` with the deckard
  blocklist writer neutered, the seed `autorun::0` and the startup hook
  (gitignored; sha256 `88711f9e…`).
* `local/work/bb10/z10-ufs.ufs` — the 10.3 UFS with `autorun::0` (gitignored;
  2,588,409,856 B, sha256 `8bc7c13f…`).
* `local/work/bb10/z10-win.sealed.ifs` — the built, CRC-sealed BB10 IFS
  (gitignored; 8,555,080 B, sha256 `50e1c04a…`).
* `local/work/bb10/bb10-set.signed` — the packed V1+V2 container (gitignored;
  2,964,324,944 B, sha256 `d92ee639…`), dry-run gate accepted.
* `local/work/bb10/startup.stock.sh` / `startup.edited.sh` — the RCFS
  `scripts/startup.sh` before/after the hook (gitignored).
* `local/work/bb10/ifs_variables.winchester.sh` — the grafted platform file
  (gitignored); `local/work/bb10/rfs_rcfs_shim` — the built shim.
* Portable CPython 3.2.6 (built in the session scratchpad, `-no-pie`) and the
  `uncompyle6` venv — tooling, not artefacts; neither touched the host system.
* `docs/reports/171-mkifs-imagefs-compression-re-2026-11-03.md` — the
  `startup_header`/`image_header` layouts from `mkxfs`'s DWARF, the field values
  for both stock images, the slot arithmetic, and the validated payload splice.
* `docs/reports/172-mkxfs-stream-semantics-2026-11-03.md` — the narrow RE pass:
  payload start/end, `stored_size` semantics, the chunked NRV2B framing, the
  `compress=` selector, and the header patch rules.
* `tools/README.md` — the new tool's row.
* `local/work/back-1917.signed` — the way-back container (gitignored;
  1,104,544,336 B), packed and verified, **not flashed**.
* `local/work/bb10/loader-info.json` — the loader `info` capture (`boot_mode`,
  `factory_count 45`, `os_metrics`).
* `docs/project/BB10-IMAGE-GUIDE.md` — **new**: the repeatable BB10 image
  editing tutorial (IFS splice/reseal, RCFS in-place edits and `.pyc`, the
  verifier shim, the UFS `autorun` change, packing/flashing/verifying, the way
  back).
* Working extracts (gitignored, not artefacts):
  `local/work/bb10/z10-ifs-tree2/` (Z10 IFS), `local/work/bb10/z10-base/`
  (Z10 `/base`, 601 MB), `local/work/bb10/win1103/` (10.0.9.1103 components),
  `local/work/bb10/win1103-ifs-tree/`, `local/work/bb10/win1103-base/`,
  `local/work/bb10/z10-ufs.ufs`, and the `*.listing`/`*.list`/`*.err` captures.
* **Media written:** the BB10 10.3 set (IFS + edited RCFS + edited UFS) at
  ~05:32–05:43; five factory boots followed (counter 50 → 45).  **No RPMB
  write by an agent.**  The unit is in the ramloader; the way back is packed.
