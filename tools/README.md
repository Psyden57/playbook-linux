# tools/ — host-side bench tools

Every path is derived from this script's own location, so a clone works
anywhere; none of them refers to the old `rea-work` tree (verified by `grep`).

**What lives here vs. what is gitignored.**  The USB library (`bbusb.py`) is ours
and tracked in this directory.  Two things must still exist under `local/`:

* `local/loaders/` — the ramloader blob the flasher uploads (`LDR_01`,
  RIM material), plus `local/vendor/bb10mt*` for the signature/container tooling.
  `bbusb.DEFAULT_LOADERS` resolves `$BBUSB_LOADERS`, then `tools/loaders`, then
  `local/loaders`.
* `local/images/ifs0.sealed.bin` — the sealed reference IFS this image is derived
  from (sha256 `c72b0530…`), and `local/secrets/rsa` — the SSH key.

If `local/` is missing, the flashing and SSH tools fail with a path error rather
than doing something wrong.

| tool | what it does |
|---|---|
| `env.sh` | sets up **both** cross-toolchains from `local/toolchains/`: `. tools/env.sh` exports `PATH`/`CROSS_COMPILE`/`QNX660`/`QNX_HOST`/`QNX_TARGET`; `tools/env.sh --check` validates them and prints versions without changing anything (`PB_ARM_TOOLCHAIN`/`QNX660` override) |
| `pbcon.py` | drives the bench console over `/dev/ttyACM0`: waits for the `pb> ` prompt, drains before each write, `PBCON_TMO=<s>` per command.  Usage: `PBCON_TMO=10 ./pbcon.py 'disp stat'` |
| `pbssh.sh` | QNX/TabletOS SSH wrapper: key, legacy algorithms, `$PB_HOST` (169.254.0.1) and `$PB_USER` (default `root`; the operator enabled root SSH on 2026-11-05).  `PB_USER=devuser` still works for the unprivileged account (it cannot reboot or read `/dev/rpmb`).  It starts `tools/door-hold.sh` if the door is down |
| `door-hold.sh` | keeps the qconn door open (`BerryShell-V4.py … hold`); the supervisor restarts it on exit |
| `BerryShell-V4.py` | the QNX door/SSH helper itself; derived from the author's own earlier BerryShell-V3 (now in `tools/legacy/`) |
| `QCL.py` | a standalone qconn client for QNX: connects to the target's qconn port, runs commands and reads output without the door/SSH path.  Used for QNX-side tests; see the file header for usage |
| `build_kernel_image.py` | wraps `zImage` + DTB into a copy of the sealed IFS with the chainloader, prints the image CRC.  Defaults: `local/kernel-tree/…/zImage`, `boot/chainloader/chainloader_kernel.S.in`, output `boot/chainloader/kernel.img` |
| `pre_os_ifs_flash.py` | **older template flasher — use `bbusb.py flash` instead** (`DECISIONS.md` §29).  It writes one image per bootrom session: `--template-ifs <img>` = region 0 (`0x825`); `--payload <img> --dest-raw <hex>` (or `--dest-block`) = an arbitrary block-aligned payload, **region 1 only** unless `--allow-region0`; `--dry-run`, `--delay <s>`, `--reuse-loader`, `--keep-loader`.  It builds containers from the vendor hybrid template and patches both the V1 `qcfp` and V2 `rrcq` records (trap 55 / report 139), and reads the 560-byte `signature_data` constant from bb10mt's `qcfm.pas` (not in this tree) |
| `ifs_build.py` | **the imagefs emitter** (IFS-REBUILD.md milestones 1–4). `build` writes `image_header + dirents + data + trailer` and appends it to RIM's stock prefix; `listing` wraps `dumpifs -vv`; `check` re-reads the output with `dumpifs` and compares names/sizes/modes plus the `Checksums:` line; `--replace NAME=PATH` swaps one file (a smaller file keeps its reference slot; only a size increase repacks) and `--add NAME=PATH` adds a **new** file (session 25, the BB10 backport), which forces a repack. Output is uncompressed, so it clears the startup header's compression bits (`flags1` at `0x80e`) and recomputes the startup checksum it invalidates.  **It does not compress**: for a BB10 imagefs that must fit the PlayBook slot, feed its output to `bb10_ifs.py splice` (`BB10-PORT.md` §4b). |
| `ifs_extract.sh` | `dumpifs -x` wrapper: extracts an IFS into a tree, creating missing parent directories in the rounds `dumpifs` needs. Modes and mtimes are preserved; symlinks are **not** created (the builder takes the 26 from the listing) |
| `rcfs_extract.py` | **reads a BB10 `/base`** (the `r-c-f-s` RCFS container; `rimh` v2 or the earlier header-less form), which `bb10mt qnx6 mount` cannot and `bb10mt rcfs extract` abandons on one bad chunk (trap 78): `list <img>` / `extract <img> <dir>`.  Parses the superblock at `0x1000`, 1-based 32-byte inodes, directory arrays, and LZO1X 16 KiB chunks via `liblzo2`; a chunk it cannot decode is written raw and counted instead of aborting.  Z10 10.3.03.2049 `/base`: 11,911 files / 579 symlinks / 601 MB, 0 bad chunks.  Read-only |
| `ucl_nrv2b.py` | **NRV2B (UCL) codec**, the compression QNX `mkifs` uses for an IFS imagefs.  `encode`/`decode`/`stream`/`selftest`.  The stream form is mkifs's layout: 64 KiB blocks, each prefixed with a 2-byte big-endian compressed length.  Token coding follows UCL 0.91 (GPL-2+, Oberhumer) and is validated against QNX's own decompressor: the SDP's `libucl` decodes our output.  ~0.41 ratio on the 10.3 imagefs, ~38 s for 13.7 MB |
| `bb10_ifs.py` | **splices a new imagefs into RIM's BB10 IFS container**: compresses it (above) and replaces the payload at `0x808 + startup_size`, preserving RIM's 0x800 preamble, `startup_header` and ARM decompressor stub; patches `ram_size`, `stored_size`, `imagefs_size` and the startup trailer, and appends the payload checksum + RIM's `QNXH-OS-1` block.  `splice --stock --imagefs --out`; `verify` walks the block chain and checks every field.  Validated on 10.3.03.2049: same 339-entry tree, then `89-crc_tool.py reseal` gives a `VERIFIED (CRC path)` image |
| `rfs_validator_shim.c` | **QNX-side** replacement for `proc/boot/rfs_validator` (IFS-REBUILD.md milestone 2): probes `hd1t179`/`hd2t179`/`hd7t179`/`hd7t178`, mounts `/base` qnx6 read-only with `noatime,crypto=enable,sync=optional`, treats `EBUSY` as success. Build with `ntoarmv7-gcc -O2 -Wall -o rfs_validator_shim tools/rfs_validator_shim.c`. Tested read-only as `/tmp/rfs_validator_shim -c`, then **flashed in the IFS**: it mounted `/base` and the 1917 OS booted (report 169) |
| `rfs_rcfs_shim.c` | **QNX-side** replacement for the **BB10** `rfs_validator`: the stock one verifies a signed RCFS hash table and mounts `/base` through `fs-rcfs.so`; this one skips the verification and does only the mount — probes `/dev/emmc/os0t179`, `os1t179`, `/dev/hd1t179`, `hd2t179`, then `mount(dev, "/base", 0, "rcfs", NULL, 0)`, `EBUSY` = success.  Needed after any `/base` edit (trap 79).  Build with `ntoarmv7-gcc -O2 -Wall -o rfs_rcfs_shim tools/rfs_rcfs_shim.c`; 8,256 B |
| `tests/test_ifs_build.py` | offline acceptance test for `ifs_build.py`: the synthetic selftest always runs; if the vendor `PlayBook_2.1.0.1917.4.ifs` and `dumpifs` are present it does the full 72-file/26-symlink round trip, otherwise it SKIPs |
| `bbusb.py` | **the one tool, and the flasher of record** (`DECISIONS.md` §29) (CFP-style subcommands, one file): the USB protocol module *and* the commands.  Device: `info` (read-only, the CFP `info` analogue — block size, PIN, boot mode/count, MCT TLVs, the action log via `0xB3`, the bugdisp crash log via `0xB0`, and the eight reads from reports 162/164), `flash` (a `.signed` as-is, or a bare `.ifs`/`.rcfs`/`.mbr`/`.ufs` with `--dest-block`; dry run unless `--apply`, gated against the measured MCT extents; **hardware-proven 2026-11-03 and 2026-11-06** — a whole container can carry **region 0 + region 1 in one bootrom session**, `pack -l both.lst -v 1,2 -s -f` then `flash --signed`, report 177; success is `COMPLETE 0x40C0 -> cmd 0x4006`), `reset-osregion` (the OSREGION delete with a read-back), `boot-mode`, `reset-loader` (reset a unit left in the loader).  The bootrom/ramloader open window is **60 s**.  Containers, no device needed: `pack`/`unpack`/`split`/`parse`, byte-identical to bb10mt.  It is a Python port of bb10mt's `bbusb.pas`/`ucrypto.pas` (GPL-3 — see `DECISIONS.md`), and the container part transcribes `qcfm.pas` |
| `tests/test_offline.py` | offline checks against a stubbed device — no device, no USB. 87 checks. Run it after touching `bbusb.py` |
| `docs_audit.py` | mechanical guard for the canonical docs: session counts/ranges, stale phrases, removed tool names, `report NNN` resolution, contradiction dates and index consistency. `python3 tools/docs_audit.py` must print `clean`; run it last in the wrap-up |
| `publish_public.sh` | dry-run by default: builds a clean snapshot from the private `HEAD` (`git archive`) and rsyncs it into the public checkout, preserving its `.git`. `--apply` writes; it never commits or pushes |
| `build_rootfs.py` | builds `local/images/rootfs-region1.ext4` from `rootfs/` — the real BusyBox userland: BusyBox + `/sbin/init` + applet links, inittab/rcS/profile, `pbconsole`/`pbrpmb`, and any `--modules` `.ko` under `/lib/modules/<uname -r>`; ext4 (no journal), root-owned via `fakeroot`, 64 MiB default.  Prints the sha256 and the `bbusb.py flash --component` command.  The image lives at region-1 +16 MiB (raw `0x29000000`, loader block `0x2915`) |
| `force_reset_loader.py` | sends `0x80EF` then `0xEF` to an enumerated ramloader (`0fca:8001`) so the unit leaves the loader and boots media.  Same as `bbusb.py reset-loader` |
| `tools/legacy/probe_loader.py`, `tools/legacy/usb_watch.py` | earlier bench-debug helpers: watch the boot ROM/ramloader enumerate, and poll `lsusb` identity changes |
| `pb_ring.py` | dumps the DRAM console ring (`--ring 88000000`) and decodes the event log |
| `pb_events.py` | decodes the printk-free event ring (timestamps, resolved symbols) |
| `qnx-i2c.c` / `pbi2c` | **QNX-side** I²C probe (`probe / rd / rdw / wr / scan / dump / dumpw`), built with the qnx660 toolchain; push it to `/tmp` on the unit and run it there.  `rdw`/`dumpw` read 2-byte SMBus words (LO first) and are what the ISL9519 needs.  Source is the documentation |
| `qnx-hwi.c` | **QNX-side** syspage hwinfo dumper: `ntoarmv7-gcc -std=gnu99 -O2 -o /tmp/qnx-hwi tools/qnx-hwi.c`, then `/tmp/qnx-hwi WLAN` (or `all`) on a live TabletOS boot.  It implements the by-name search (`hwi_find` is a driver-local helper, not in libc) and prints every tag; read-only.  Built for the WL1283 `WLAN` node (report 189) |
| `memdump3.c`, `memw32.c`, `memtest.c` | QNX-side memory reader / writer / tester sources (built for QNX ARM; the device copies live outside this repo) |
| `89-crc_tool.py` | IFS CRC verify/reseal: `reseal` sets `os_type` and the CRC; `--length auto --append-os-trailer` also appends the 8-byte OS trailer and patches `LoadEnd` (`image+0x08`) for a differently-sized image.  The earlier `build_crc_image.py` and `90-nvram2074.py` are in `tools/legacy/` |
| `ramloader-commands.txt` | the boot-ROM/ramloader command map recovered during the boot-chain work |

## Legacy tools

`tools/legacy/` holds superseded tools kept for historical recipes.  They
are not part of the current workflow: `BerryShell-V3.py`, `build_crc_image.py`,
`90-nvram2074.py`, `probe_loader.py` and `usb_watch.py`.  `pre_os_ifs_flash.py`
stays at the top level only because the older runbooks still cite it; use
`bbusb.py flash` for new work.

## Verified at workspace creation

* `python3 -m py_compile *.py` — all clean.
* `python3 tests/test_offline.py` — 87 checks, exit 0, no device required.
* `python3 ifs_build.py selftest` — PASSED (synthetic imagefs, dumpifs round
  trip when the QNX toolchain is present).
* `python3 tests/test_ifs_build.py` — PASSED; with
  `PlayBook_2.1.0.1917.4.ifs` available it rebuilds the stock image and
  `dumpifs` lists the same 99 dirents (root + 72 files + 26 symlinks) and a
  `Checksums:` line.  It also checks the reseal-with-length path and the shim
  build.
* **Hardware, 2026-11-03:** `bbusb.py flash --signed local/work/pair.signed`
  wrote the 1917 RCFS + shim IFS in one container (22,232 frames, signature,
  `COMPLETE 0x40C0`) and the unit booted PlayBook OS 2.1.0.1917 with `/base`
  mounted (report 169).  Use the user, **never `pkexec`** — the device node has
  a user ACL and root lacks the user's `pyusb`.
* `python3 docs_audit.py` — `clean` (19 canonical files).
* `python3 pre_os_ifs_flash.py --template-ifs ../local/images/ifs0.sealed.bin --dry-run`
  → `plan: region 0, block 0x825 … 44 blocks, container 2949160 bytes`,
  `dry run: stopping before USB`.
* `build_kernel_image.py` resolves its inputs inside this workspace; it fails
  cleanly with `FileNotFoundError … local/kernel-tree/arch/arm/boot/zImage`
  until a kernel has been built there.

## Path notes

* `pre_os_ifs_flash.py` needs the stock signed container the region-0 image is
  derived from.  It defaults to `local/images/ifs_template.signed` and can be
  pointed anywhere with `PB_IFS_TEMPLATE`; if it is missing the tool says so
  instead of failing obscurely.  Verified: `--template-ifs
  local/images/ifs0.sealed.bin --dry-run` reaches `plan:` and stops before USB.
* `build_kernel_image.py` looks for the QNX 6.6 toolchain in this order:
  `QNX660` / `Q` / `QNX_HOST`, then `local/toolchains/qnx660-master`, then
  `~/qnx660-master`; it stops with an instruction if none is found.  `. tools/env.sh`
  sets the overrides from `local/toolchains/` (`docs/project/SETUP.md` §1–§2).
* Everything else derives from the repository root
  (`Path(__file__).resolve().parent.parent`, or `ROOT=$(dirname "$0")/..` in the
  shell tools), so the tree can be moved or cloned.
