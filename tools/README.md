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
| `pbssh.sh` | QNX/TabletOS SSH wrapper: key, legacy algorithms, `root@169.254.0.1`.  It starts `tools/door-hold.sh` if the door is down |
| `door-hold.sh` | keeps the qconn door open (`BerryShell-V4.py … hold`); the supervisor restarts it on exit |
| `BerryShell-V4.py` | the QNX door/SSH helper itself; derived from the author's own earlier BerryShell-V3 (not a third-party project) |
| `build_kernel_image.py` | wraps `zImage` + DTB into a copy of the sealed IFS with the chainloader, prints the image CRC.  Defaults: `local/kernel-tree/…/zImage`, `boot/chainloader/chainloader_kernel.S.in`, output `boot/chainloader/kernel.img` |
| `pre_os_ifs_flash.py` | writes an image to media through the boot ROM.  `--template-ifs <img>` = region 0 (`0x825`); `--payload <img> --dest-raw <hex>` (or `--dest-block`) = an arbitrary block-aligned payload, **region 1 only** unless `--allow-region0`; `--dry-run`, `--delay <s>`, `--reuse-loader`, `--keep-loader`.  Containers are built from the vendor hybrid template and patch **both** the V1 `qcfp` and V2 `rrcq` records — see trap 55 / report 139 (a V2-only `bb10mt pack -v 2` container is rejected `0x11`, and the loader uses the region-relative V1 offset).  **Public-snapshot caveat:** it reads the 560-byte `signature_data` constant from bb10mt's `qcfm.pas`, which is not included here; supply your own bb10mt checkout, or use `bbusb.py flash`, which carries the constant (but has not been run on hardware) |
| `bbusb.py` | **the one tool** (CFP-style subcommands, one file): the USB protocol module *and* the commands.  Device: `info` (read-only, the CFP `info` analogue — block size, PIN, boot mode/count, MCT TLVs, the action log via `0xB3`, the bugdisp crash log via `0xB0`, and the eight reads from reports 162/164), `flash` (a `.signed` as-is, or a bare `.ifs`/`.rcfs`/`.mbr`/`.ufs` with `--dest-block`; dry run unless `--apply`, gated against the measured MCT extents), `reset-osregion` (the OSREGION delete with a read-back), `boot-mode`, `reset-loader` (reset a unit left in the loader).  Containers, no device needed: `pack`/`unpack`/`split`/`parse`, byte-identical to bb10mt.  It is a Python port of bb10mt's `bbusb.pas`/`ucrypto.pas` (GPL-3 — see `DECISIONS.md`), and the container part transcribes `qcfm.pas` |
| `tests/test_offline.py` | offline checks against a stubbed device — no device, no USB. 87 checks. Run it after touching `bbusb.py` |
| `docs_audit.py` | mechanical guard for the canonical docs: freshness stamps, session counts/ranges, stale phrases, removed tool names, `report NNN` resolution. `python3 tools/docs_audit.py` must print `clean`; run it last in the wrap-up |
| `build_rootfs.py` | builds `local/images/rootfs-region1.ext4` from `rootfs/`: static ARM `/init`, ext4 (no journal), root-owned, 32 MiB default.  Prints the sha256 and the flash command.  The image lives at region-1 +16 MiB (raw `0x29000000`, loader block `0x2915`) |
| `force_reset_loader.py` | sends `0x80EF` then `0xEF` to an enumerated ramloader (`0fca:8001`) so the unit leaves the loader and boots media.  Same as `bbusb.py reset-loader` |
| `probe_loader.py` | watches the boot ROM / ramloader enumerate and dumps what it sees (bench debugging) |
| `usb_watch.py` | polls `lsusb`-level identity changes of the unit |
| `pb_ring.py` | dumps the DRAM console ring (`--ring 88000000`) and decodes the event log |
| `pb_events.py` | decodes the printk-free event ring (timestamps, resolved symbols) |
| `qnx-i2c.c` / `pbi2c` | **QNX-side** I²C probe (`probe / rd / wr / scan / dump`), built with the qnx660 toolchain; push it to `/tmp` on the unit and run it there.  Source is the documentation |
| `memdump3.c`, `memw32.c`, `memtest.c` | QNX-side memory reader / writer / tester sources (built for QNX ARM; the device copies live outside this repo) |
| `build_crc_image.py`, `89-crc_tool.py`, `90-nvram2074.py` | earlier CRC/NVRAM tooling from the boot-chain phase |
| `ramloader-commands.txt` | the boot-ROM/ramloader command map recovered during the boot-chain work |

## Verified at workspace creation

* `python3 -m py_compile *.py` — all clean.
* `python3 tests/test_offline.py` — 87 checks, exit 0, no device required.
* `python3 docs_audit.py` — `clean` (17 canonical files).
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
