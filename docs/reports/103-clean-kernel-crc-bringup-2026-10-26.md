# 103 — Clean Linux 6.15.11 on the CRC/chainloader path (2026-10-26)

Status: live, **the clean kernel now boots and runs early driver init on the CRC
path**. This report supersedes `102`'s kernel-entry work and records every command,
bug and workaround from this session. Read the companion
`HANDOFF-2026-10-26-clean-kernel.md` first if you just want the next-session prompt.

---

## 0. TL;DR

- **We stopped using the kexec kernel.** A clean `linux-6.15.11` was built from
  `pristine` + only the PlayBook board/debug pieces (no kexec memory hacks). The
  full source delta is `clean-kernel-6.15.11-changes.patch` (12 files).
- The clean kernel **boots through the CRC/chainloader path** and runs ~1.33 s of
  driver init (GPIO, serial, pinctrl, DMA, smartreflex, PMIC, i2c, net, mtd…),
  then **hangs at/after the `gpiochip1` probe**. Last real error:
  `ti-sysc 4a318000.target-module: probe with driver ti-sysc failed with error -16`
  (`-EBUSY`). The next step is the GPIO2+/PER-power-domain/PRCM path.
- **Six independent bugs were found and fixed** (see §7): LZMA input-bound/gzip,
  DTB-trailer overlap, OMAP4 DRAM-barrier abort, PL310 early-VA unmapped after
  `paging_init`, `CMDLINE_FORCE` overriding the DTB, and WDT2 being idled by
  `ti,sysc`.
- **The feedback loop is now automated and reliable:**
  - chainloader clears the debug ring every boot;
  - the bootloader-armed WDT2 self-resets a hung kernel (≈40–58 s);
  - the recovery tool catches the WDT reset's bootrom and restores the sealed IFS;
  - the qconn door auto-reconnects across reboots (keepalive + `TCP_USER_TIMEOUT`
    + non-blocking peek);
  - `/accounts/devuser/startup.sh` tops `BOOTCTR_FCTRY` back to 50 with a verified
    readback on every TabletOS boot.
- **Spare final state:** TabletOS up, media = sealed IFS (`sha256 c72b0530…`),
  `BOOTCTR_FCTRY = 50`, qconn door held by `door-hold.sh`.

---

## 1. Units / safety

| unit | PIN | use |
|---|---|---|
| **Spare 64 GB** | `0x50176EC1`, BSN `1063043093` | the only destructive test bed |
| **Reference 64 GB** | `0x50175127` | **do not touch** |
| 2× 32 GB | — | bricked recovery targets |

- `BOOTCTR_FCTRY` decrements on every successful factory/CRC boot. `0` and
  `>0x32` are fatal (the bootloader falls back to the product/ECDSA path, where
  the resealed `os_type=6` image no longer boots).
- A power-button hold+press **usually preserves DRAM** (ring); a battery pull
  wipes it.
- The recovery channel (bootrom → loader → `0xF7`) never depends on region 0.

---

## 2. Current device state (end of session)

| item | value |
|---|---|
| OS | TabletOS up, `0fca:8010`, ping `169.254.0.1` OK |
| media region-0 IFS | `sha256 c72b0530…` = `ifs0.sealed.bin` (restored) |
| `BOOTCTR_FCTRY` | `50` (`0x32`), verified readback |
| door | `door-hold.sh` supervisor running; `BerryShell-V4 hold` reconnected |
| clean kernel image on disk | `analysis/chainloader/kernel.img`, CRC `0x1C324081` (early-console build, **not** currently on media) |
| last kernel stop | ~1.33 s, `gpiochip1`, `ti-sysc 4a318000 -EBUSY` |

---

## 3. Host environment / exact commands

### 3.1 qconn door (SSH gateway)

The PlayBook's SSH only works while a BerryShell `hold` process owns the qconn
door on `169.254.0.1:4455`. Use the supervisor (it restarts the holder if it ever
exits):

```bash
/home/psyden/rea-work/door-hold.sh          # run in background
```

`door-hold.sh` calls:

```bash
python3 /home/psyden/rea-work/BerryShell-V4.py --log /tmp/berryshell-hold.log \
    hold --key /home/psyden/rea-work/rsa --pub /home/psyden/rea-work/rsa.pub
```

**Argument order matters:** `--key`/`--pub` are on the `hold` subcommand, *after*
`hold`; `--log` is global. Putting them before `hold` gives `rc=2`.

**V4 fixes made this session (already in the file):** on connect it sets
`SO_KEEPALIVE` + `TCP_KEEPIDLE/INTVL/CNT=2` and `TCP_USER_TIMEOUT=4000`, and
`send_heartbeat()` does a non-blocking `MSG_PEEK|MSG_DONTWAIT` to detect EOF/RST.
Without these the 4 s heartbeat keeps the connection non-idle, so keepalive never
probes and a half-open socket after a device reboot is never detected.

SSH wrapper (`/tmp/pbssh.sh`; recreate it on a new host):

```bash
#!/bin/sh
exec ssh -i /home/psyden/rea-work/rsa \
 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o IdentitiesOnly=yes \
 -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedKeyTypes=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa \
 -o KexAlgorithms=+diffie-hellman-group1-sha1 -o MACs=+hmac-sha1,hmac-sha1-96 \
 -o Ciphers=+aes128-cbc,3des-cbc,aes256-cbc -o LogLevel=ERROR -o ConnectTimeout=8 \
 root@169.254.0.1 "$@"
```

```bash
chmod +x /tmp/pbssh.sh
/tmp/pbssh.sh 'uname -a; echo OK'
```

### 3.2 Factory counter

Read (host, no `od` on QNX — pipe the 4 raw bytes back):

```bash
/tmp/pbssh.sh 'dd if=/dev/rpmb/BOOTCTR_FCTRY bs=1 count=4 2>/dev/null' | od -An -tu4
# 50 = 0x32
```

Manual top-up:

```bash
/tmp/pbssh.sh 'dd if=/dev/zero of=/tmp/bc32.bin bs=4 count=1 2>/dev/null
  printf "\062" | dd of=/tmp/bc32.bin bs=1 count=1 conv=notrunc 2>/dev/null
  dd if=/tmp/bc32.bin of=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 conv=notrunc 2>/dev/null
  dd if=/dev/rpmb/BOOTCTR_FCTRY bs=1 count=4 2>/dev/null' | od -An -tu4
```

Automatic top-up: `/accounts/devuser/startup.sh` (installed; backup is
`startup.sh.orig`). It waits for `/dev/rpmb`, writes 0x32, **reads back the first
byte and retries up to 6×**, and logs `bootctr -> 0x32 ok (...)` to
`/accounts/devuser/.bootctr.log`. The readback matters: an early write can be
rejected by the RPMB resmgr.

### 3.3 Debug ring readback

For the spare's QNX shell there is no `od`/`hexdump`/`sha256sum`; use `dd` over
SSH and decode on the host.

```bash
# count (+0x80) and index (+0x84)
/tmp/pbssh.sh 'on -C 0 /accounts/devuser/memdump3 90000080 0x8'
# 3840-char ring at +0x100
/tmp/pbssh.sh 'on -C 0 /accounts/devuser/memdump3 90000100 0xF00' > ring.txt
```

Reconstruct in write order:

```python
# parse "addr: word" lines into mem{}
raw = b''.join(mem[0x90000100+i].to_bytes(4,'little') for i in range(0,0xF00,4))
idx = mem[0x90000084]                # write index, wraps at 0xF00
text = (raw[idx:0xF00] + raw[:idx]) # newest-first rotation
```

The ring is **0xF00 (3840) chars** at PA `0x90000100`, count at `+0x80`, index at
`+0x84`. Mirrors: `0x88000000`, `0x94000000`, `0x9FE00000` (all written by
`debug/omap4bc.S`). The kernel produced ~12.5 K chars in one boot, so the ring
**wraps ~3×**; only the tail (the hang) survives. The capture is **interleaved**
because concurrent printk contexts share `printascii`; sort by `[ sec.usec]`
timestamps to read it.

### 3.4 Media verification

```bash
/tmp/pbssh.sh 'dd if=/dev/hd1 bs=16 skip=65536 count=180193 2>/dev/null' | sha256sum
# c72b053027769c4d49270e9ee208fd2b0afcf69ce4fb3cf0c0be3c938ee05299 = ifs0.sealed.bin
```

---

## 4. The clean kernel build

### 4.1 Source / toolchain

- Tarball: `/home/psyden/SSDdrive/wsl-migration/home/psyden/kernel/linux-6.15.11.tar.xz`
- Pristine reference tree: `…/kernel/pristine` (vanilla 6.15.11)
- **Clean build tree:** `/home/psyden/rea-work/playbook-dev/kernel-clean`
  (extracted from the tarball with `--strip-components=1`)
- Toolchain: `/home/psyden/SSDdrive/wsl-migration/home/psyden/toolchains/armv7-eabihf/bin/arm-linux-gcc`
  (Buildroot 2021.11, gcc 14.3.0, binutils 2.43.1) — same compiler as the kexec
  artifacts.
- Host `bc` was installed via **pacman** (Arch); `flex`, `bison`, `pahole`, `dtc`,
  OpenSSL and libelf are already present.

### 4.2 Config

Base: `playbook-dev/kernel-patches/winchester.config` copied to `.config`, then:

```bash
cd /home/psyden/rea-work/playbook-dev/kernel-clean
scripts/config --file .config -d KEXEC -d KEXEC_CORE
scripts/config --file .config -d DEBUG_INFO_SPLIT
scripts/config --file .config -d KERNEL_LZMA -e KERNEL_GZIP
scripts/config --file .config -e DEBUG_UNCOMPRESS     # cannot be enabled, see §7.1
scripts/config --file .config -d CMDLINE_FORCE        # CRITICAL, see §7.5
make ARCH=arm CROSS_COMPILE=arm-linux- olddefconfig
```

Key resulting options:

```
CONFIG_ARCH_OMAP4=y                CONFIG_HZ=100
CONFIG_ARM_PATCH_PHYS_VIRT=y       CONFIG_AUTO_ZRELADDR=y
CONFIG_ARM_APPENDED_DTB=y          CONFIG_CMDLINE_FORCE is not set
CONFIG_KERNEL_GZIP=y               CONFIG_KERNEL_LZMA is not set
CONFIG_DEBUG_LL=y                  CONFIG_DEBUG_PLAYBOOK_BC=y
CONFIG_DEBUG_LL_INCLUDE="debug/omap4bc.S"
CONFIG_EARLY_PRINTK=y              CONFIG_OMAP_WATCHDOG=m
CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED=y
CONFIG_CMA_SIZE_MBYTES=16
# CONFIG_SMP is not set (maxcpus=1)
```

### 4.3 Build

```bash
C=/home/psyden/rea-work/playbook-dev/kernel-clean
export PATH=/home/psyden/SSDdrive/wsl-migration/home/psyden/toolchains/armv7-eabihf/bin:$PATH
make ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb
```

- The DTB target is **relative to `arch/arm/boot/dts`**; passing
  `arch/arm/boot/dts/ti/omap/...` doubles the path.
- `zImage` is gzip ≈ 6.96 MB; the `Image` is ≈ 16.9 MB.
- On a config change, if the compressed stage doesn't pick it up, delete
  `arch/arm/boot/compressed/*.o` and rebuild.

### 4.4 Source patch set

`analysis/clean-kernel-6.15.11-changes.patch` is the exact delta against
`pristine`.  **It was regenerated on 2026-10-27 (session 5): 2 482 lines, 28
files, verified with `patch -p1 --dry-run` against a real pristine 6.15.11**
(the pristine tree is at
`/home/psyden/SSDdrive/wsl-migration/home/psyden/kernel/pristine`; the exact
recipe is in the bootstrap §5 "Source patch").  The table below is the
**session-2 subset** that this report described; sessions 3–5 added the display,
WDT2/ti-sysc, eMMC and event-log changes — see
`playbook-dev/kernel-patches/README.md` for the current inventory.

| file | change |
|---|---|
| `arch/arm/boot/dts/ti/omap/omap4-winchester.dts` | **new board DTS**: `/memory@80000000` = `0x80000000 + 0x40000000` (1 GB, not the kexec `0xa0000000` bank); `earlyprintk keep_bootcon ignore_loglevel maxcpus=1 clk_ignore_unused panic=10`; UART3/TWL6030/MMC2; reserved ring pages; PL310 latencies |
| `arch/arm/boot/dts/ti/omap/Makefile` | build `omap4-winchester.dtb` |
| `arch/arm/boot/dts/ti/omap/omap4-l4.dtsi` | WDT2 target-module: `ti,no-idle; ti,no-reset-on-init;` (§7.6) |
| `arch/arm/include/debug/omap4bc.S` | **new**: UART3 + 3 DRAM rings, MMU-on/off aware, PL310 CI-by-PA flush |
| `arch/arm/Kconfig.debug` | `DEBUG_PLAYBOOK_BC` + `DEBUG_LL_INCLUDE` default |
| `arch/arm/kernel/head.S` | early-map the ring VAs (`0xC8/0xD0/0xD4/0xFEB`) + `stext` marker (`0x90000014 = 0x51000001`) |
| `arch/arm/boot/compressed/head.S` | decompressor entry marker (`+0x0c = 0x51000002`), `__enter_kernel` marker (`+0x10 = 0x51000003`), before/after `decompress_kernel` markers (`+0x18/0x1c`) |
| `arch/arm/boot/compressed/misc.c` | `decompress_kernel` markers (`+0x20/0x28/0x2c` + output/arena/length at `+0x34..0x40`) and **input bound `_edata - input_data`** (§7.1) |
| `arch/arm/include/debug/uncompress.h` | decompressor `putc()` writes the DRAM ring (`0x90000080/+0x100`) |
| `arch/arm/mach-omap2/omap4-common.c` | `omap_barriers_init()` body disabled (§7.3) |
| `arch/arm/mach-omap2/io.c` | permanent `0xFEB00000 → 0x48200000` 1 MB `MT_DEVICE` iotable (§7.4) |
| `arch/arm/mm/mmu.c` | `PB: post-map_io/post-fill_pmd_gaps/post-pci_reserve_io/post-tlb/post-cache/post-abt` printks in `devicemaps_init` |

The markers are debug-only and can be removed once bring-up is done. Keep
`omap4bc.S`, the DTS, the head.S early map, `io.c`, `omap4-common.c` and the
`ti,no-idle` change.

---

## 5. Image builder and chainloader

### 5.1 `build_kernel_image.py` (two fixes)

- **Env overrides** for the clean artifacts:
  `ZIMAGE=… DTB=… python3 build_kernel_image.py`.
- **IFS trailer reservation (critical):** the signature trailer's magic/CRC at
  `end-8`/`end-4` used to be written **inside** the appended DTB (the image ended
  right after the DTB), corrupting the FDT tail (`dtc`: string-table overrun).
  `end = align(dtoff + len(d) + 0x230, 4)` now reserves 560 bytes after the DTB.
  Verify: extract the DTB from the image and `dtc -I dtb -O dts` must succeed.

### 5.2 `chainloader_kernel.S.in`

- DTB magic check compares against `0xEDFE0DD0` (the LE word), not `0xD00DFEED`.
- `clear_ring` macro zeroes count/index + chars (`+0x80..+0x1000`) of
  `0x88000000`, `0x90000000`, `0x94000000`, `0x9FE00000` at every boot, so each
  kernel log starts clean.
- `SP = 0x800F0000` is only used by the chainloader; the decompressor sets its
  own stack from `_edata`.
- Copies zImage → `0x84000000`, DTB → `0x84000000 + align(len(z),8)`, then
  `r0=0, r1=~0, r2=DTB, pc=0x84000000`.

### 5.3 Verify the image

```bash
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
python3 89-crc_tool.py verify chainloader/kernel.img --image-off 0
```

Structural, `desc[0xD4]==6` and CRC must PASS. The **ECDSA trailer-shape check
FAILs on the extended image — that is informational**; the factory path never
uses ECDSA.

---

## 6. Flash / reset / restore / recover (exact)

### 6.1 Flash a kernel image (or the sealed IFS)

```bash
cd /home/psyden/rea-work
python3 bootdumps-2026-09-11/analysis/pre_os_ifs_flash.py \
  --template-ifs bootdumps-2026-09-11/analysis/chainloader/kernel.img
```

The tool **waits for the bootrom** (`0fca:0001`). The bootrom window only exists
for ~13 s on power-on, so the tool must already be polling when you power-cycle.
From a running OS you can open the window with:

```bash
/tmp/pbssh.sh '/proc/boot/shutdown -S reboot'
```

Then it uploads `LDR_01`, `PreFlash 0x20EE`→`0x20DE`, 124–152 `0xF7` frames,
`0x40F9` (real 560-byte trailer) → `0x4006`, `0x40C0`→`0x4015` (write is on
media), and reboots.

### 6.2 Force the loader to reset

The post-flash `0x80EF` is often too short / the loader wedges. Force it:

```bash
python3 /home/psyden/rea-work/bootdumps-2026-09-11/analysis/playbook-live-spare-2026-10-26-clean/force_reset_loader.py
```

It opens `0fca:8001` and sends `0x80EF` (then `0xEF`), confirming the disconnect
(`Errno 19` after the first write means success). If the loader is gone and the
SoC is wedged, power-cycle.

### 6.3 Restore TabletOS after a kernel test

Use the sealed IFS; `--delay 40` makes the tool start polling *after* the first
bootrom, so it catches the **WDT2 self-reset** of the hung kernel:

```bash
python3 bootdumps-2026-09-11/analysis/pre_os_ifs_flash.py \
  --template-ifs bootdumps-2026-09-11/analysis/playbook-live-spare-2026-10-25/ifs0.sealed.bin \
  --delay 40
```

Then force `0x80EF`, wait ~2 min for TabletOS (the door auto-reconnects), and read
the ring (§3.3).

### 6.4 USB IDs / timeline

```
0fca:0001 bootrom   0fca:8001 ramloader
0fca:8010 OS        0fca:8012 OS/RNDIS   0fca:8020 OS/mass-storage
```

`analysis/usb_watch.py` logs transitions. A ~20 s bootrom cadence = reset loop.

---

## 7. The six root causes (what actually broke, in order)

### 7.1 LZMA decompressor never returned

- Markers showed `decompress_kernel` entered (`0x51000006`) but never returned.
- The LZMA stream declares a **64 MB dictionary** and `dst_size = ~0`; the ARM
  decompressor's range decoder reads a few bytes past the end marker, and with
  `input_data_end` (the piggy-size bound) it ran off the buffer with `fill ==
  NULL`, aborting (`rc.buffer_size <= 0` → `ret = -1` → `error()` halt).
- **Two fixes:** pass `_edata - input_data` (the full initialized-data range,
  giving the decoder lookahead) **and** switch to **gzip** (`CONFIG_KERNEL_GZIP`)
  — simpler 32 KB window, no 64 MB dictionary. Gzip immediately booted.
- `CONFIG_DEBUG_UNCOMPRESS` cannot be enabled on `ARCH_MULTIPLATFORM`, so the
  decompressor's own console was wired up by editing
  `arch/arm/include/debug/uncompress.h` `putc()` to write the DRAM ring.

### 7.2 Image builder corrupted the DTB

The IFS trailer (`end-8` magic, `end-4` CRC) landed **inside** the appended DTB
because the image ended at the DTB. `dtc` failed on the embedded copy. Fixed by
reserving 0x230 bytes after the DTB (§5.1). This alone would have stopped any
kernel from parsing its FDT.

### 7.3 `omap_barriers_init()` aborted the boot

The kernel died immediately after `OMAP4: Map 0xafe00000 ... for dram barrier`.
The OMAP4 DRAM-barrier page is not usable here; the first `soc_mb()` access
aborts. Body `#if 0`'d; `soc_mb` stays the default `mb()`. (`omap4-common.c`.)

### 7.4 PL310 debug VA unmapped after `paging_init`

The ring flush reads/writes the PL310 via `0xFEB42000`. `paging_init` drops the
`head.S` early section map (and `debug_ll_io_init` is not called because OMAP4 has
`map_io`). The stale TLB entry worked until `local_flush_tlb_all()`; the next
64-char ring flush then walked an unmapped page and faulted mid-printk (the log
ended mid-`PB:`). Fixed with a permanent
`0xFEB00000 → 0x48200000`, 1 MB `MT_DEVICE` entry in `omap44xx_io_desc`
(`arch/arm/mach-omap2/io.c`).

### 7.5 `CONFIG_CMDLINE_FORCE=y` overrode the DTB

The built-in cmdline (`… root=/dev/mmcblk0p2 rootwait`) was used, ignoring the
DTB's `clk_ignore_unused`, and `rootwait` can wait forever for a root device.
Disabled `CMDLINE_FORCE`; the DTB now supplies
`earlyprintk keep_bootcon ignore_loglevel maxcpus=1 clk_ignore_unused panic=10`
(no `console=ttyO2` — see §9).

### 7.6 WDT2 was idled by `ti,sysc`

`CONFIG_OMAP_WATCHDOG=m` never loads, but the `ti,sysc` layer for
`4a314000.target-module` idled/reset the module, so the bootloader-armed WDT2
stopped counting and a hung kernel never reset (the unit went dark and stayed
dark). Added `ti,no-idle; ti,no-reset-on-init;` to that target-module in
`omap4-l4.dtsi`; combined with `clk_ignore_unused` the WDT2 now fires ~40–58 s
after the last bootloader kick. This is what makes the recovery/ring loop work.

---

## 8. Debug ring protocol / markers

- Rings at PA `0x88000000`, `0x90000000`, `0x94000000`, mirror `0x9FE00000`.
- `0x90000000+0x00` = `KBLB`; `+0x04` = chainloader stage (`3` = jumped);
  `+0x08` = `KERN`; `+0x0c/0x10/0x14` = decompressor entry / hand-off / `stext`
  markers; `+0x18/0x1c` = before/after `decompress_kernel`;
  `+0x20/0x28/0x2c` = `decompress_kernel` entry / return / ret.
- `+0x80` count, `+0x84` index (wrap 0xF00), `+0x100..+0x10FF` chars.
- Marker values: `0x51000001` stext, `0x51000002` decompressor entry,
  `0x51000003` `__enter_kernel`, `0x51000004/05` around `decompress_kernel`,
  `0x51000006/08` entry/return in `misc.c`.
- Caveat: concurrent printks interleave the char stream; sort by timestamps.
  The QNX OS may also churn the ring pages after boot — read soon.

---

## 9. Current stop point and next steps

The clean kernel's last log is at **~1.33 s**, after `omap_gpio` added
`gpiochip0`/`gpiochip1`, with:

```
[0.778167] ti-sysc 4a318000.target-module: probe with driver ti-sysc failed with error -16
```

`0x4A318000` is `timer1`; `-16` = `-EBUSY` (the bootloader left it running and the
sysc layer cannot reset it). The hang is during/after the next GPIO bank probe;
GPIO2–6 live in the **PER power domain**, and the PRCM clock/power writes are the
known risky area from the kexec work (`probe.S`: PRCM `CLKCTRL` writes can abort
from non-secure).

Recommended order:

1. **Make the capture unambiguous:** enlarge the ring (currently one 4 KB page;
   `ring_put` wraps at `0xF00`) or reduce log volume, so the exact last probe is
   explicit. Consider writing the ring to a reserved eMMC sector before parking.
2. **Isolate the ~1.33 s hang:** disable `CONFIG_GPIO_OMAP`, or the GPIO2–6
   banks, and see if the boot advances. Then fix the PER-domain/PRCM access
   (runtime PM, `omap4_prminst` SMC path, `ti,no-idle` for the affected modules).
3. Continue down the init sequence: i2c/PMIC regulators, MMC/eMMC, display/DSS,
   USB gadget. Expect more OMAP4 bring-up quirks; the kexec tree's W-notes are a
   checklist of known walls (TLB broadcast, CP13/TLS writes, L2, barriers).
4. Long term: the one-shot/restore + eMMC ring design (`97`/`99`) for iteration
   without the WDT/recovery cycle.

---

## 10. Gotchas / troubleshooting

- **SSH refused after a reboot:** the door holder is re-handshaking; wait ~10 s,
  or check `tail /tmp/berryshell-hold.log`. The supervisor restarts rc=2/args
  failures. `--key/--pub` must come after `hold`.
- **`0xEF` alone usually doesn't reset the loader**; use `0x80EF` (helper) or a
  power-cycle.
- **Power-cycle:** hold Power ~10–20 s to force off, press to boot. Usually keeps
  DRAM; battery pull wipes it.
- **Ring garbage / no `Uncompressing`:** the OS may have churned the pages, or the
  power-cycle wiped DRAM. Re-run and read soon after boot.
- **QNX lacks `od`/`hexdump`/`sha256sum`/`cmp`**; it has `awk`, `sed`, `grep`,
  `dd`, `cat`. Pipe raw `dd` output to the host.
- **`bc` is required** by the kernel build (`timeconst.h`); installed via pacman.
- **DTB target path** is relative to `arch/arm/boot/dts`.
- **`89-crc_tool.py` ECDSA FAIL is informational** on the factory path.
- **`pre_os_ifs_flash.py` catches the bootrom; start it before the power-cycle.**
  Use `--delay 40` to catch a WDT self-reset instead of the first bootrom.
- **The OS boot can overwrite the ring page**; read it promptly.

---

## 11. Artifacts / index

- `analysis/playbook-live-spare-2026-10-26-clean/` — ring captures
  (`gz-full.txt`, `econ-ring.txt`, `noidle-ring.txt`, `dm-ring.txt`, …),
  `door-hold.sh`, `force_reset_loader.py`.
- `analysis/clean-kernel-6.15.11-changes.patch` — the exact 12-file source delta.
- `analysis/chainloader/kernel.img` — latest clean kernel image, CRC `0x1C324081`.
- `analysis/chainloader/kernel.kexec.img` — previous kexec-based image (reference).
- `analysis/playbook-live-spare-2026-10-25/ifs0.sealed.bin` — restore image
  (`sha256 c72b0530…`).
- `/home/psyden/rea-work/playbook-dev/kernel-clean` — the build tree.
- `/home/psyden/rea-work/door-hold.sh` — qconn door supervisor.
- `/accounts/devuser/startup.sh` (+ `.orig`) / `.bootctr.log` — counter top-up.
- Report index: `88`–`103`; this session: `102` (state probe/kernel chainloader)
  → `103` (clean kernel bring-up).
