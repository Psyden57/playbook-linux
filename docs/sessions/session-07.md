# Session 7 (absolute 7, Linux-era "session 1") — clean 6.15.11 on the CRC path: the build, the capture, six bugs, and the first stops

> Numbering: absolute **7** of 12. In the reports and handoffs this is
> **"session 1"** — its output report is
> `docs/reports/103-clean-kernel-crc-bringup-2026-10-26.md` and its handoff is
> `docs/reports/HANDOFF-2026-10-26-clean-kernel.md`. See `NUMBERING.md`.

Date / span: 2026-10-26 (device logs are ART = UTC−3). One long session; it began
from `HANDOFF-2026-10-26-next-session.md` and reports `94`–`102`, and ended with
the clean kernel booting into early driver init. All device timestamps in the
captures are ART.

Unit state at the end: TabletOS from the sealed IFS (sha256 `c72b0530…`,
re-verified after the last restore), door held by `door-hold.sh`,
`BOOTCTR_FCTRY` read `0x32` (50) — but see *Superseded*: the top-up script was
later proved broken, so that reading is not evidence of a repair. Image on disk:
`chainloader/kernel.img` CRC **`0x1C324081`**, not on media.

## Goal of this session

Read the CRC/loader handoff and **stop using the kexec kernel**: build a clean,
upstream Linux 6.15.11 for the PlayBook, boot it through the native CRC/chainloader
path, and diagnose where it stops. "Done" was a clean kernel that boots and a
reliable way to see where it dies — the display was explicitly later work.

The operator's standing rules: the spare 64 GB (`PIN 0x50176EC1`) is the only
test bed; the reference (`0x50175127`) is not to be touched; keep all writes and
the live device in the main session; use subagents for read-only tracks.

## Confirmed (measured, read in a file, or seen on the bench)

### 1. The clean kernel was built from pristine 6.15.11

* Source: `linux-6.15.11.tar.xz` from
  `/home/psyden/SSDdrive/wsl-migration/home/psyden/kernel/`, extracted with
  `--strip-components=1` into `/home/psyden/rea-work/playbook-dev/kernel-clean`.
  The vanilla reference tree is `…/kernel/pristine`; the sibling `…/kernel/linux`
  is the **kexec-phase modified** tree and was not used. [103 §4.1]
* Toolchain: `/home/psyden/SSDdrive/wsl-migration/home/psyden/toolchains/armv7-eabihf/bin/arm-linux-gcc`
  — Buildroot 2021.11, gcc 14.3.0, binutils 2.43.1 (the same compiler family as
  the kexec artifacts). [103 §4.1]
* Host `bc` was installed with **pacman** (the host is Arch); `flex`, `bison`,
  `pahole`, `dtc`, OpenSSL and libelf were already present. `bc` is required for
  `kernel/time/timeconst.h`; without it the build stops at `Kbuild:21`. [103 §4.1]
* Config base: `playbook-dev/kernel-patches/winchester.config` copied to
  `.config`, then exactly: `-d KEXEC -d KEXEC_CORE`, `-d DEBUG_INFO_SPLIT`,
  `-d KERNEL_LZMA -e KERNEL_GZIP`, `-e DEBUG_UNCOMPRESS` (ineffective — see
  below), `-d CMDLINE_FORCE`, `make olddefconfig`. Resulting keys: `CONFIG_HZ=100`,
  `ARM_PATCH_PHYS_VIRT=y`, `AUTO_ZRELADDR=y`, `ARM_APPENDED_DTB=y`,
  `KERNEL_GZIP=y`, `DEBUG_PLAYBOOK_BC=y`, `DEBUG_LL_INCLUDE="debug/omap4bc.S"`,
  `EARLY_PRINTK=y`, `CMDLINE_FORCE` unset, `OMAP_WATCHDOG=m`, `SMP` unset
  (`maxcpus=1`). [103 §4.2]
* Build: `make ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage` and
  `make … ti/omap/omap4-winchester.dtb`. The DTB target is relative to
  `arch/arm/boot/dts`; passing the full path doubles it. gzip `zImage` ≈ 6.96 MB;
  `Image` ≈ 16.9 MB. [103 §4.3]
* At this session's close the source delta was **12 files** in
  `clean-kernel-6.15.11-changes.patch`; later sessions grew it to 29 files /
  2 922 lines (`STATE.md`; 103 §4.4 records the regeneration). [103 §4.4]

### 2. The image builder and chainloader fixes that made a boot possible

* `tools/build_kernel_image.py` gained `ZIMAGE=`/`DTB=` environment overrides, so
  the clean artifacts could be wrapped without editing constants. [103 §5.1]
* **The IFS trailer was being written inside the appended DTB**: `end-8` magic and
  `end-4` CRC landed in the DTB's last bytes because the image ended at the DTB.
  `dtc` on the embedded copy failed with a string-table overrun; the kernel could
  not have parsed its FDT. Fixed by
  `end = align(dtoff + len(d) + 0x230, 4)`, reserving the 560-byte trailer after
  the DTB. [103 §5.1/§7.2]
* The chainloader's DTB magic test compares the little-endian word **`0xEDFE0DD0`**
  (not `0xD00DFEED`). A `clear_ring` macro zeroes the count/index and characters
  (`+0x80..+0x1000`) of `0x88000000`, `0x90000000`, `0x94000000`, `0x9FE00000`
  at every boot. [103 §5.2]
* Copy/jump layout: zImage → `0x84000000`, DTB →
  `0x84000000 + align(len(z),8)`, then `r0=0, r1=~0, r2=DTB, pc=0x84000000`.
  The chainloader's `SP=0x800F0000` is only its own stack; the decompressor sets
  its own from `_edata`. [103 §5.2]

### 3. Six bugs, each isolated by the DRAM ring and markers

1. **LZMA decompressor never returned.** `decompress_kernel` entry marker
   `0x51000006` was present, the return marker `0x51000008` was not. The LZMA-alone
   stream declares `dict_size = 0x04000000` (64 MB) and `dst_size = ~0`; the ARM
   range decoder reads a few bytes past the end marker and, with
   `input_data_end` (the piggy-size bound) as the length and `fill == NULL`, it
   aborted at `rc.buffer_size <= 0` → `ret = -1` → `error()`'s infinite halt.
   Two fixes were applied: pass **`_edata - input_data`** (the whole
   initialized-data range, giving the decoder lookahead), and switch to
   **gzip** (`CONFIG_KERNEL_GZIP`), which booted immediately. [103 §7.1]
2. **The DTB trailer overlap** above. [103 §7.2]
3. **`omap_barriers_init()` aborted the boot.** The last clean line was
   `OMAP4: Map 0xafe00000 to (ptrval) for dram barrier`; the OMAP4 DRAM-barrier
   page is not usable on this unit and the first `soc_mb()` access aborted. The
   body was `#if 0`'d, leaving `soc_mb` at the default `mb()`
   (`arch/arm/mach-omap2/omap4-common.c`). [103 §7.3]
4. **The PL310 debug VA was unmapped after `paging_init`.** The ring flush used
   `0xFEB42000`; `paging_init` drops the `head.S` early section map (OMAP4 has
   `map_io`, so `debug_ll_io_init` is not called). The stale TLB entry worked
   until `local_flush_tlb_all()`; the next 64-char flush walked an unmapped page
   and faulted mid-printk (the log ended mid-`PB:`). Fixed with a permanent
   `0xFEB00000 → 0x48200000`, 1 MB `MT_DEVICE` entry in `omap44xx_io_desc`
   (`arch/arm/mach-omap2/io.c`). [103 §7.4]
5. **`CONFIG_CMDLINE_FORCE=y` overrode the DTB.** The built-in cmdline
   (`… root=/dev/mmcblk0p2 rootwait`) was in force, so the DTB's
   `clk_ignore_unused` never applied and `rootwait` could wait forever. The
   option was disabled; the DTB supplies
   `earlyprintk keep_bootcon ignore_loglevel maxcpus=1 clk_ignore_unused panic=10`
   (no `console=ttyO2`). [103 §7.5]
6. **WDT2 was idled by `ti,sysc`.** `CONFIG_OMAP_WATCHDOG=m` never loads, but the
   `ti,sysc` layer for `4a314000.target-module` idled/reset the module, so the
   bootloader-armed WDT2 stopped counting and a hung kernel never reset (the unit
   went dark and stayed dark). `ti,no-idle; ti,no-reset-on-init;` was added to
   that target-module in `omap4-l4.dtsi`; with `clk_ignore_unused` the WDT2 then
   reset a hung kernel. [103 §7.6]

### 4. The first capture and the markers

* Ring: **0xF00 (3840) chars** at PA `0x90000100`, monotonic count at `+0x80`,
  wrapping index at `+0x84` (`0xF00`), mirrors at `0x88000000`/`0x94000000`/
  `0x9FE00000`. The kernel produced ~12.5 K chars in one boot, so the ring
  wrapped ~3×; only the tail survived. Concurrent printk contexts interleaved
  the character stream; the timestamps (`[ sec.usec]`) were the reliable ordering.
  [103 §3.3/§8]
* `CONFIG_DEBUG_UNCOMPRESS` cannot be enabled on `ARCH_MULTIPLATFORM`, so the
  decompressor's own console was wired by editing
  `arch/arm/include/debug/uncompress.h`'s `putc()` to write the ring. Additional
  markers were planted at the decompressor entry (`+0x0c = 0x51000002`),
  `__enter_kernel` (`+0x10 = 0x51000003`), around `decompress_kernel`
  (`+0x18/0x1c`), in `misc.c` (`+0x20/0x28/0x2c`), and at `stext`
  (`0x90000014 = 0x51000001`). Marker values `0x51000001`–`0x51000008`.
  [103 §8]
* The capture was reconstructed with the write index:
  `text = raw[idx:0xF00] + raw[:idx]` (the QNX shell has no `od`; the raw `dd`
  bytes were hexdumped on the host). [103 §3.3]

### 5. The iteration/recovery loop as it stood

* Flash a kernel:
  `python3 analysis/pre_os_ifs_flash.py --template-ifs analysis/chainloader/kernel.img`,
  with the tool already polling, then `/proc/boot/shutdown -S reboot` from the
  running OS (or a power-cycle). The tool catches `0fca:0001`, uploads `LDR_01`,
  `PreFlash 0x20EE`→`0x20DE`, the `0xF7` frames, the real `0x40F9` trailer
  (→`0x4006`), `0x40C0` (→`0x4015`), then reboots. [103 §6.1]
* The post-flash `0x80EF` is often too short / the loader wedges; the session-local
  `force_reset_loader.py` opened `0fca:8001`, sent `0x80EF` (then `0xEF`), and
  treated the `[Errno 19] No such device` as success. [103 §6.2]
* Restore: the sealed IFS with `--delay 40`, so the tool starts polling after the
  first bootrom and catches the next WDT self-reset; then `0x80EF`, wait ~2 min
  for TabletOS, read the ring. [103 §6.3]
* The qconn door: `door-hold.sh` supervises `BerryShell-V4 hold`. V4 was fixed
  this session to detect a dead/half-open door: `SO_KEEPALIVE` +
  `TCP_KEEPIDLE/INTVL/CNT=2`, `TCP_USER_TIMEOUT=4000`, and a non-blocking
  `MSG_PEEK|MSG_DONTWAIT` in `send_heartbeat()`. Without these, the 4 s heartbeat
  kept the connection non-idle, keepalive never probed, and a device reboot left
  the holder stuck. The `--key`/`--pub` flags belong **after** `hold` (`--log` is
  global); putting them before `hold` gives `rc=2`. [103 §3.1]
* The counter top-up `/accounts/devuser/startup.sh` was rewritten to wait for
  `/dev/rpmb`, write `0x32`, read the first byte back, retry up to six times, and
  log `bootctr -> 0x32 ok`. The installed script used
  `printf '\062' | dd …`; see *Superseded* — that was later found to be broken.
  [103 §3.2]
* USB phase IDs used before concluding anything was broken: `0fca:0001` bootrom,
  `0fca:8001` ramloader, `0fca:8010` OS/RNDIS (TabletOS) or our kernel (product
  string), `0fca:8012` OS/RNDIS, `0fca:8020` mass-storage. A ~20 s bootrom cadence
  is a reset loop. [103 §6.4]

### 6. The stop this session measured

* The last clean line was at kernel time **~1.33 s** (`gpiochip1`); the one sysc
  failure in the tail was
  `[0.778167] ti-sysc 4a318000.target-module: probe with driver ti-sysc failed
  with error -16` (`-EBUSY`), which is earlier than the last line. [103 §9]
* The decoded tail also showed serial/8250, pinctrl, DMA, smartreflex, PMIC
  (`twl6030_uv_to_vsel: OUT OF RANGE!`), i2c, net and mtd lines; the ordered
  reconstruction's highest timestamp was ~1.33 s. [103 §9; capture
  `playbook-live-spare-2026-10-26-clean/`]
* Refreshing the harness: the capture files preserved in
  `analysis/playbook-live-spare-2026-10-26-clean/` (`gz-full.txt`,
  `econ-ring.txt`, `noidle-ring.txt`, `dm-ring.txt`, …), plus
  `door-hold.sh` and `force_reset_loader.py`, are this session's artifacts.
  [103 §11]

### 7. Device facts used repeatedly

* Spare vs reference PINs and the 32 GB recovery targets; `BOOTCTR_FCTRY` is
  decremented on every successful factory/CRC boot, and `0`/`>0x32` send the
  bootloader to the product/ECDSA path where our `os_type=6` image does not
  boot. [103 §1]
* Sealed IFS hash: `c72b0530…`; media check is
  `dd if=/dev/hd1 bs=16 skip=65536 count=180193 | sha256sum`.
  [103 §3.4]
* QNX shell lacks `od`, `hexdump`, `sha256sum`, `cmp`; it has `awk`, `sed`,
  `grep`, `dd`, `cat`. Raw `dd` output is piped to the host. [103 §3.3/§10]
* A power-button hold+press usually preserves DRAM; a battery pull wipes it.
  [103 §1]

## Inferred (reasoning from evidence, not directly measured)

* **The ~1.33 s stop was a driver hang at the next GPIO bank** (GPIO2–6 are in the
  PER power domain; PRCM clock/power writes were the known risky area from the
  kexec `probe.S`). Strength: the last line was in `omap_gpio`, and removing the
  previous driver moved the last line — but this interpretation was later shown
  to be wrong (see *Superseded*). [103 §9]
* **The WDT2 self-reset was ~40–58 s** after the bootloader's kick, inferred from
  `--delay 40` catching a bootrom on every restore. Strength: the catch was real;
  the timeout attribution was wrong (later measured 7.812 s). [103 §6.3;
  *Superseded*]
* **A read-only MMC2 probe was safe.** At this point `CONFIG_MMC_BLOCK=y`, I
  believed `CONFIG_MMC_RPMB` was "not set" (6.15 has no such symbol — see
  *Superseded*), and there was no initramfs/userspace, so nothing could mount or
  write the eMMC; the probe command set is read-only. Strength: the config and
  the absence of userspace are measured; the "no write path" conclusion is an
  inference and was qualified later. [This session's operator advice; 109 §9;
  *Superseded*]
* **The character interleaving was concurrent printk contexts sharing
  `printascii`**, not OS churn. Strength: the timestamps from two streams were
  both present and coherent; the OS could also churn the page, so both were
  plausible. [103 §8]
* The ring page sometimes survived the OS boot and sometimes held power-up/garbage
  patterns; the session concluded the OS may overwrite it and readback should be
  prompt. Strength: observed both outcomes. [103 §10]

## Dead ends and wrong assumptions (the valuable part)

1. **The chainloader stack would collide with the decompressed image.** `SP` was
   `0x800F0000`, inside the 16.9 MB output region, and I suspected it caused the
   decompressor death. It did not: the decompressor overrides `SP` from `_edata`
   at `restart`, before its first C call. The actual LZMA failure was the input
   bound. [103 §4; the decompressor's `restart` in
   `arch/arm/boot/compressed/head.S`]
2. **LZMA just needed a bigger arena.** The 64 MB dictionary looked like a
   `large_malloc` failure; the markers showed the decoder was entered and never
   returned, and the stream was valid (host `lzma.decompress` reached the Image
   when given enough lookahead). The fix was the `_edata` bound plus gzip, not an
   arena change. [103 §7.1]
3. **Disabling `omap_barriers_init` would let the boot continue.** It did, one
   printk further, and then died mid-`PB:` at the first TLB flush because the
   PL310 VA was gone. Two separate bugs, discovered in sequence. [103 §7.3/§7.4]
4. **`ti,no-idle` + `clk_ignore_unused` would make the WDT2 fire.** The unit still
   went dark. That was because `ti,sysc` had already idled the WDT2 target; the
   `ti,no-idle`/`ti,no-reset-on-init` change was what fixed it. I had earlier
   tried `&wdt2 { status = "disabled"; }` and `clk_ignore_unused` alone, both
   insufficient. [103 §7.6]
5. **`clk_ignore_unused` was in the bootargs.** It was not in effect: the build
   had `CONFIG_CMDLINE_FORCE=y`, so the built-in `root=/dev/mmcblk0p2 rootwait`
   cmdline overrode the DTB. Finding that resolved a long "why does the unit hang
   before any output" confusion. [103 §7.5]
6. **A "dark unit" means a hung kernel.** It can also mean the kernel finished
   early and the display/PMIC path is off, or the WDT has not fired. This session
   burned several power-cycles and lost the ring twice before the
   `ti,no-idle`/`ti,no-reset-on-init` fix made the WDT self-reset reliable.
   [103 §6/§7.6; operator observed the dark unit]
7. **The counter script's log meant the top-up worked.** It did not; see
   *Superseded*. The lesson: a readback that only re-reads the same register
   cannot distinguish "my write landed" from "nothing changed". [116 addenda 1–2;
   `docs/contradictions/bootctr-drain-vs-session10-readings.md`]
8. **"`CONFIG_MMC_RPMB` is not set, so Linux cannot address RPMB."** Wrong in
   6.15: the symbol does not exist, and `mmcblkXrpmb` is an unconditional
   chardev of `mmc_block`; the correct guardrail is "never open it", not a config
   option. This was said to the operator in-session, before the MMC probe.
   [109 §9 note; *Superseded*]
9. **A `--delay 40` catch measured the WDT timeout.** It only measured the
   polling start; the WDT is 7.812 s and the bootrom recurs, so a delayed poll
   catches a later iteration. [109 §1.1/§8.1; *Superseded*]
10. **`0x4A318000` was "timer1".** Report 109 later reads `REV = 0x15` and no 32k
    countdown, i.e. not a watchdog; my `-EBUSY` note was a sysc-probe failure,
    not a watchdog identity. [109 §8.1; *Superseded*]

## Unknown / open questions

* **What exactly was at 1.33 s?** This session could not distinguish "driver hang"
  from "capture artifact". Resolved by capture v2: the boot reaches ~1.51 s past
  GPIO2–6/i2c/MMU/MMC; the 1.33 s line was a frozen ring. [104 §1/§2]
* **Is the PER-domain/PRCM the hang?** This session inferred it; later sessions
  found the ~1.5 s cluster was the WDT2 deadline, and disabled the crypto/ABE/
  EMIF/DSS/MCSPI targets for other reasons. [104 §2; 109 §5.2]
* **What is the WDT2 timeout/mechanism?** Later measured: `LDR = 0xfffc1800` →
  7.812 s, `LDR` writes dropped, `TGR` kick reloads `CRR`, and a 2 s
  `timer_list` keeps it alive. [109 §8.1]
* **Did the counter top-up actually work?** Later: no — `printf` absent, zero
  writes, the check could not notice; the counter drains 0x32→0x28 over ~10 boots.
  Resolved by `bench/device-startup.sh`. [116 addenda]
* **Does the OS churn the ring?** Observed both survival and garbage; later
  capture v2 (non-cacheable, 32 KB) proved the page stable for minutes, so the
  old mosaic was the **capture**, not the OS. [104 §1]

## Superseded / uncertain

* **The "1.33 s / `gpiochip1` stop" was a capture artifact.** My evidence was the
  wrapped 3.8 KB ring, the interleaved tail, and the last clean timestamp at
  1.33 s. Report 104 §1/§2 and the session-8 note show the old ring was a mosaic
  (PL310 flush after the L2C driver took secure ownership, L1 dirty lines, a
  decompressor 1 KB wrap vs the kernel 3.8 KB wrap) and that capture v2 reaches
  ~1.51 s, past GPIO2–6, i2c1–4, GPMC, PMU, IOMMU and the MMC host probe. The
  current reading is `docs/contradictions/104-mmc2-wall-vs-wdt2-vfs.md`.
* **My ring design (cacheable, PL310 CI-by-PA, 3 840 chars at `0x90000000`) and
  the `0xFEB00000` iotable entry** were superseded by capture v2: 32 KB per ring
  at `0x88000000`, dedicated **non-cacheable** aliases
  `0xFE800000/0xFE900000/0xFEA00000` (`MT_MEMORY_RWX_NONCACHED`), wrap `0x7F00`,
  no cache maintenance, `no-map` DTS pages, `stdout-path` removed. [104 §1;
  session-8 note]
* **"The WDT2 self-reset is ~40–58 s."** Superseded by 109 §1.1/§8.1: the
  bootloader arms `LDR = 0xfffc1800` = **7.812 s**; `LDR` writes are dropped and
  a `TGR` kick reloads `CRR`; the kernel pets it from a 2 s `timer_list`. My
  `--delay 40` caught a later iteration of the reset loop.
* **`0x4A318000` as "timer1"/watchdog.** 109 §8.1: `REV = 0x15`, not a watchdog;
  report 107's "always-on secure timer" candidate was a misidentification. The
  `-EBUSY` sysc probe failure stands, but the identity is wrong.
* **The top-up script's `bootctr -> 0x32 ok` log was a false positive.** My
  evidence: the installed script text (`printf '\062' | dd …`), a test run that
  read 50, and the log line. 116 addenda 1–2: `printf` does not exist on TabletOS,
  the temp file stayed zero, the script wrote `0x00000000` six times per boot
  (refused as out-of-range), the check could not distinguish, and the counter
  really drains. The current implementation is `bench/device-startup.sh`; see
  `docs/contradictions/bootctr-drain-vs-session10-readings.md`.
* **"`CONFIG_MMC_RPMB` is not set, so Linux cannot address RPMB."** Wrong in
  6.15: there is no such Kconfig symbol, and `mmcblkXrpmb` is part of
  `mmc_block` unconditionally. Those were the days before the eMMC worked; the
  safe rule is now "never open `/dev/mmcblkXrpmb`", and `CONFIG_MMC_BLOCK=n` for
  probe-only builds. [109 §9 note; 104 §0]
* **The patch and image counts.** At my close the delta was 12 files (103 §4.4)
  and the image was CRC `0x1C324081` (103 §2). The tree now has 29 files /
  2 922 lines and the current best image is `0xE1F79294` (`STATE.md`); do not
  confuse them.
* **`force_reset_loader.py`** was session-local in `analysis/…-clean/`; it is now
  in `tools/` (the handoff §3 path debt is partly paid). [tools listing;
  HANDOFF §3]

## Artefacts produced

* `docs/reports/103-clean-kernel-crc-bringup-2026-10-26.md` — this session's
  report: the clean build, the six bugs, the capture, the recovery loop, the
  1.33 s stop.
* `docs/reports/HANDOFF-2026-10-26-clean-kernel.md` — the next-session handoff.
* `analysis/clean-kernel-6.15.11-changes.patch` — 12 files at the time (later
  regenerated to 29/2 922; `kernel/clean-kernel-6.15.11-changes.patch`).
* Board DTS (`omap4-winchester.dts`): memory at `0x80000000`+1 GB, the bootargs
  above, reserved ring pages, PL310 latency, WDT2 `ti,no-idle`; plus the
  `omap4-l4.dtsi` WDT2 properties.
* `arch/arm/include/debug/omap4bc.S` (UART3 + three DRAM rings, MMU-aware),
  `arch/arm/Kconfig.debug` (`DEBUG_PLAYBOOK_BC`), `head.S` early ring/PL310 maps
  and `stext` marker, `compressed/head.S` + `misc.c` markers and the `_edata`
  bound, `debug/uncompress.h` ring `putc`.
* `arch/arm/mach-omap2/omap4-common.c` (barrier init disabled),
  `arch/arm/mach-omap2/io.c` (the `0xFEB00000` PL310 map), `arch/arm/mm/mmu.c`
  (the `PB: post-…` checkpoints).
* `boot/chainloader/chainloader_kernel.S.in` — `clear_ring` macro and the
  `0xEDFE0DD0` magic fix.
* `tools/build_kernel_image.py` — env overrides and the 0x230 trailer reservation.
* `tools/door-hold.sh` + the `tools/BerryShell-V4.py` keepalive/USER_TIMEOUT/peek
  fix; `tools/force_reset_loader.py`; `analysis/playbook-live-spare-2026-10-26-clean/`
  (ring captures, scripts).
* `/accounts/devuser/startup.sh` (the top-up; **later found broken** — see
  *Superseded*) and `.bootctr.log`; `startup.sh.orig` was kept.
* Images: `chainloader/kernel.img` CRC `0x1C324081` (early-console build; not on
  media); the sealed IFS (`sha256 c72b0530…`) restored and verified.
