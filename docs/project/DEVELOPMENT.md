# Development loop

The bench loop is: **build → wrap into the chainloader image → flash → boot →
console → restore the sealed IFS**. The commands below are the Linux-era
session 6 (absolute 12) handoff §4 loop restated against **this** tree: paths now
come from the repository root, and every tool named here exists (`tools/`,
`tools/README.md`). Where a step is outside the tree — the two cross-toolchains
and the QNX template — `SETUP.md` §8 names it.

**Path variables used below.** Set these once in the shell:

```bash
REPO=$(git rev-parse --show-toplevel)   # this checkout
BB10MT=/path/to/your/bb10mt             # your bb10mt working directory
```

`$REPO` is wherever you cloned this repository; `$BB10MT` is the vendor tool
directory described in §9. Nothing in `tools/` requires a particular absolute
path.

## 1. Preconditions, always

* Check USB ownership: `ps -ef | grep -E 'BerryShell|bbusb|pre_os_ifs_flash|force_reset'`.
  Only **one** process may own the USB link. Bench work and static RE are
  different jobs; subagents never touch the bench.
* The flash precondition is the **unit running media** (`0fca:8010`), not the
  ramloader. In `0fca:8001` there is no `/dev/ttyACM0`, so a console `reboot` is
  a silent no-op and the flasher waits out the ramloader (~90–120 s, the "two
  minutes with the green LED") before falling back to the bootrom (trap 27).
* The flasher's bootrom window is **300 s** and it exits silently: start it
  *immediately* before the reboot and check its log tail (trap 25).
* Every boot: `pkexec chmod 666 /dev/ttyACM0` (trap 24).
* **`tools/pb_bootlog.sh`** captures the console's full `kmsg` to
  `local/logs/boot-YYYYmmdd-HHMMSS.log` (host-side only).  Run it **on
  request** — it is not part of the routine boot loop.
* Read `SAFETY.md` before the first write of a session.  **Do not assume there is
  a spare** — the unit inventory and its state are in `STATE.md` §"The unit".  An
  agent still never writes RPMB; the unit behaves as read-only except the
  documented region-0 image flashes.

## 2. Access to the reference OS (QNX / TabletOS)

```bash
# after a HOST reboot the door supervisor is gone — start it first
pgrep -af door-hold || nohup tools/door-hold.sh >/dev/null 2>&1 &

for i in $(seq 1 40); do timeout 12 tools/pbssh.sh 'echo DOOROK' 2>/dev/null \
    | grep -q DOOROK && { echo "door up ($i)"; break; }; sleep 5; done
timeout 20 tools/pbssh.sh 'uname -a'
```

`tools/pbssh.sh` = `ssh -i local/secrets/rsa` plus
`HostKeyAlgorithms=+ssh-rsa`, `KexAlgorithms=+diffie-hellman-group1-sha1`,
`Ciphers=+aes128-cbc`, target `$PB_USER@$PB_HOST` (default
`root@169.254.0.1`); it starts `door-hold.sh` if the door is down.
**"Connection refused" = the door, not the network.**

**SSH to the 1917 OS runs as `root`** (the operator enabled root SSH on
2026-11-05; the 4869 root key is still rejected — report 169).  `tools/pbssh.sh`
therefore needs no `PB_USER` on the door: `tools/pbssh.sh 'uname -a'` logs in as
`root@169.254.0.1`.  `PB_USER=devuser` still works if a task needs the
unprivileged account, but it cannot reboot the unit or read `/dev/rpmb`.
The 1917 OS comes up on USB `0fca:8012` ("RIM Network Device"), not `0x8010`,
and takes about a minute after the flash before `169.254.0.1` answers.

**These commands are for the TabletOS (QNX) side only.**  Once **our** Linux
image is running there is no door, no ssh and no `/proc/boot/shutdown`; the
console (`tools/pbcon.py 'reboot'`) is the reboot path (trap 88).

The QNX shell has no `head`, `xxd`, `od`, `md5sum`, `sum`, `cmp`, `bc` or
`python`; it has `tail`, `dd`, `strings`, `awk`, `sed`, `grep`, and `python2.7`
only once the devuser overlay is up. Hash host-side: pipe `dd` output to the
host. Two equivalent media checks:

```bash
tools/pbssh.sh 'dd if=/dev/hd1 bs=16 skip=65536 count=180193 2>/dev/null' > /tmp/ifs.bin   # exactly 0x2BFE10 bytes
tools/pbssh.sh 'dd if=/dev/hd1 bs=16 skip=65536 count=180193' | sha256sum
```

## 3. Build and wrap

**The current base is Linux 7.3.3** (session 30).  `local/kernel-tree` is the
7.3.3 tree; `local/kernel-tree-6.15.11` is the untouched reference.  Use
**`kernel/winchester-7.3.3.config`** for a clean build (`kernel/winchester.config`
is the 6.15.11 one), and keep **`CONFIG_KERNEL_XZ=y`**: the chainloader image
must end below the region-0 IFS slot bound `0xB00000`; with gzip the assembled
image was 0xB8ABB4 (the zImage alone is smaller — the 2.88 MB sealed-IFS base
pushes it over).  Module release is **7.3.3**.

**The binaries in `initramfs/root/` are NOT built by the kernel's build** — the
kernel only packs the directory into the cpio — **and they are gitignored**
(`initramfs/root/{init,console,pbrpmb}`; only the sources are tracked), so a
fresh clone has none until you build them.  A source change in `initramfs/*.c`
does nothing until you rebuild the matching binary **and** drop the cpio (the
kernel build reuses it otherwise).  The full recipe, in order:

```bash
K=local/kernel-tree
. tools/env.sh                                  # ARM + QNX toolchains from local/toolchains/
tools/env.sh --check                            # prints the versions; fails loudly if a toolchain is missing

# 1. userspace images — rebuild EVERY one whose .c you touched:
arm-linux-gcc -static -O2 -Wall -o initramfs/root/console initramfs/console.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/init    initramfs/init.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/pbrpmb  initramfs/rpmb.c
#    (console.c, init.c and rpmb.c are self-contained: -static -O2 -Wall is
#     the whole command line; there is no Makefile.  Build to a scratch path
#     first if you want to keep a working binary: -o /tmp/pbrpmb && cp …)

# 2. driver/DTS: the build tree is local/kernel-tree, the tracked copy is
#    kernel/source/.  Edit one, copy it over, and keep them identical:
cp local/kernel-tree/drivers/misc/pbmon.c            kernel/source/pbmon.c
cp local/kernel-tree/arch/arm/boot/dts/ti/omap/omap4-winchester.dts \
   kernel/source/omap4-winchester.dts

# 3. kernel + DTB + modules (rm the cpio — see below)
rm -f $K/usr/initramfs_data.cpio*
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 modules

# 4. verify the cpio actually carries the binaries you just built
(cd $K/usr && cpio -itv < initramfs_data.cpio | grep -E ' (init|console|pbrpmb)$')

# 5. wrap (prints the CRC; writes boot/chainloader/kernel.img)
ZIMAGE=$K/arch/arm/boot/zImage \
DTB=$K/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb \
python3 tools/build_kernel_image.py
```

**The printed CRC must differ from the previous build** (trap 39).  A build that
repeats a CRC has usually failed to link or reused a stale object; if you also
edited `initramfs/root/*`, check step 4 *before* flashing.

`rm -f usr/initramfs_data.cpio*` is not optional: without it the previously
embedded initramfs is reused and your console change is not in the image. The
DTB target is relative to `arch/arm/boot/dts` — passing the full path doubles it.
`ZIMAGE=`/`DTB=` are the builder's own environment overrides, so no constant has
to be edited. `EP_TICKS=<32 kHz ticks>` re-enables the epoch-delay instrument
(needs a WDT2 window that accommodates it).

**The builder rewrites `boot/chainloader/chainloader_kernel.S`.**  It patches the
loader's `ZIMAGE_SIZE`/`DTB_SRC`/`DTB_SIZE`/`DTB_DEST` constants to match the
image it just assembled, so that tracked file shows up modified after every
build.  That is expected — commit it with the build, do not revert it.

**Shell gotcha:** the build tools take plain hex on the command line, but
`head -c` does not (`head -c 0x60d4` → “invalid number of bytes”).  Use
`head -c $((0x60d4))` or decimal.

**Check which initramfs tree the build tree actually uses.**  Session 15 lost a
build to this: `local/kernel-tree/.config` said
`CONFIG_INITRAMFS_SOURCE="$HOME/rea-work/…/initramfs/root"` while
`kernel/winchester.config` says `initramfs/root` here, so the image carried
*rea-work's* older `/init` and nothing in `initramfs/init.c` took effect.
Verify the built cpio, not the source:

```bash
grep CONFIG_INITRAMFS_SOURCE $K/.config
cpio -itv < $K/usr/initramfs_data.cpio | grep ' init$'   # size/mtime must be this repo's
```

`kernel/winchester.config` (and `kernel/patches/winchester.config`) carry
`CONFIG_INITRAMFS_SOURCE="../../initramfs/root"` — relative to the kernel
tree (`local/kernel-tree`), so it works in any clone with this layout.  If you
build out of tree, override it (`make … CONFIG_INITRAMFS_SOURCE=$PWD/initramfs/root`).

The builder assembles the chainloader with the QNX 6.6 toolchain and **rewrites
`0x008` (`LoadEnd`), `end-8` (magic) and `end-4` (CRC)** every time, so the
printed CRC is the only thing worth quoting.

## 3a. The region-1 BusyBox userland and its modules

The real root lives on the eMMC (region 1, raw `0x29000000`), not in the
initramfs.  `tools/build_rootfs.py` stages `rootfs/` plus a static BusyBox plus
the bring-up tools and the module tree, and writes a journal-free ext4:

```bash
. tools/env.sh
# 1. BusyBox (once; the tree is gitignored under local/):
#    download busybox-1.36.1, then:
make ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf- defconfig
sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
make ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf- -j12
make ARCH=arm CROSS_COMPILE=arm-buildroot-linux-gnueabihf- \
     CONFIG_PREFIX=/tmp/bbinstall install   # generates busybox.links

# 2. kernel modules (needed once for Module.symvers; CONFIG_MODVERSIONS=y):
make -C $K ARCH=arm CROSS_COMPILE=$CROSS_COMPILE -j12 modules
make -C $K ARCH=arm CROSS_COMPILE=$CROSS_COMPILE M=$PWD/rootfs/modules modules
cp rootfs/modules/pbhello.ko local/work/modules/

# 3. the region-1 image (64 MiB, ext4, no journal):
python3 tools/build_rootfs.py --size-mb 64 \
  --busybox local/work/src/busybox-1.36.1/busybox \
  --busybox-links local/work/src/busybox-1.36.1/busybox.links \
  --modules local/work/modules-7.3.3 --krel 7.3.3
```

What it installs: `/bin/busybox` + 401 applet links, `/sbin/init` ->
BusyBox init, `/etc/inittab` (a `sysinit` rcS and `ttyGS0::respawn:/bin/sh -l`),
`/etc/init.d/rcS` (mounts proc/sys/dev, tmpfs /tmp//run, hostname, `depmod -a`),
`/etc/profile` (**`PS1='pb> '`** so `tools/pbcon.py` keeps working),
`/bin/pbconsole` (the hardware console), `/bin/pbrpmb`, and the `.ko` files
under `/lib/modules/<uname -r>` with a host-generated `modules.dep`.

**The hand-off.**  `initramfs/init.c`'s `rootfs_boot()` mounts the region-1
ext4 **read-write** (the loop's backing `/dev/mmcblk1` must be opened `O_RDWR`,
trap 92), bind-mounts `/dev` `/proc` `/sys` into it, `chroot`s and `exec`s
`/sbin/init`.  With no `/sbin/init` it runs the old diagnostic and falls back to
the bring-up console.  On the shell, `reboot` is a BusyBox applet (warm reset;
the pre-OS flasher catches the bootrom), `insmod`/`modprobe` load a module, and
`/bin/pbconsole` takes over the tty for the old `fb`/`bl`/`peek`/`spi` commands.

**Flash both halves in ONE bootrom session** (the unit must be booted, not in
the ramloader).  **`tools/bbusb.py` is the flasher**; the older
`pre_os_ifs_flash.py` is kept only for historical recipes (`DECISIONS.md` §29):

```bash
printf 'boot/chainloader/kernel.img=16,0\nlocal/images/rootfs-region1.ext4=8448,0\n' \
    > /tmp/both.lst
python3 tools/bbusb.py pack -l /tmp/both.lst -v 1,2 -s -f -c /tmp/both.signed
python3 tools/bbusb.py flash --signed /tmp/both.signed --from-bootrom \
        --password 0000 --apply
# or one component:
python3 tools/bbusb.py flash --component local/images/rootfs-region1.ext4 \
        --dest-block 0x2915 --from-bootrom --password 0000 --apply
```

Success is `COMPLETE 0x40C0 -> cmd 0x4006` and `done`.  `/etc/inittab`
deliberately has no `umount -a -r`: on a loop root it can only print EBUSY.
Shutdown from this root produced eMMC warnings and a stuck tty once (trap 93);
use **`reboot -f`** from the shell to reset immediately for a flash.

## 4. Flash a test build (from **a booted OS**, TabletOS or our own image)

**`tools/bbusb.py` is the flasher** (`DECISIONS.md` §29); `pre_os_ifs_flash.py`
is the older template tool.  It only speaks the pre-OS protocol, so it cannot
reboot the unit itself.  Start it immediately before the reboot and use **the
reboot command of whatever is running**:

| the unit is running | reboot with |
|---|---|
| TabletOS 2.1.0.1917 | `tools/pbssh.sh '/proc/boot/shutdown -S reboot'` (root ssh) |
| **our Linux image** | **`tools/pbcon.py 'reboot -f'`** — `-f` skips the slow busybox shutdown (trap 93); there is no door (trap 88) |

```bash
lsusb | grep -i 0fca                      # 0fca:8010/8012 = OS running; 0fca:0001 = bootrom, 8001 = ramloader
nohup python3 tools/bbusb.py flash --component boot/chainloader/kernel.img \
      --dest-block 0x825 --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
sleep 2
tools/pbcon.py 'reboot -f'                # our Linux image  (TabletOS: pbssh '/proc/boot/shutdown -S reboot')
until grep -qE 'COMPLETE|done|error' /tmp/f.log; do sleep 2; done; tail -5 /tmp/f.log
# bbusb resets the loader itself after the write.  The bootrom window is 60 s
# (a graceful shutdown can take ~90 s; use reboot -f).  If the log says
# "No BlackBerry device found", the reboot came too late: retry and reboot sooner.
for i in $(seq 1 40); do ls /dev/ttyACM0 >/dev/null 2>&1 && break; sleep 2; done
timeout 15 pkexec chmod 666 /dev/ttyACM0
```

**The plan line's "media 0x…" is a loader-block address, not a raw device
offset.**  Loader block `N` is raw `mmcblk1`/`hd0` offset
`(N - 0x15) * 0x10000`: the IFS at "block `0x825`, media `0x8250000`" is raw <!-- audit-ok: names the old wrong value -->
`0x8100000` (measured, report 134 §5).  The flash itself needs no change — the
loader speaks the same coordinates — but anything that reads the media from
Linux must use the shifted offset, computed with Python.

A flash takes ~13 s once it is in the bootrom (report 121 §8). `--dry-run` builds
and prints the plan (`region 0, block 0x825 … 44 blocks`) without touching USB;
`--build-test` writes a harmless 3-block `KBLB` pattern at block `0x8D0`;
`--delay <s>` waits before hunting for the bootrom (this is what catches the
~7.8 s reset loop of a wedged image).

The bootrom branch now passes `bbusb.DEFAULT_LOADERS` (`local/loaders` here),
and the post-flash loader reset is built in
(`leave_loader()`), so the manual `force_reset_loader.py` step is only a
fallback.  The **pre-flash** reboot is still started by hand: the tool only
speaks the pre-OS protocol and waits for `0fca:0001`, so it cannot reboot the
running OS itself (the OS-side USB interface belongs to the door supervisor or
the ACM console).  Start the flasher immediately before the reboot and check
its log; a `--reboot` option is a possible future addition (HANDOFF §1a).

## 5. The console (test image running)

```bash
ls /dev/ttyACM0 && timeout 15 pkexec chmod 666 /dev/ttyACM0   # after EVERY boot
PBCON_TMO=10 ./tools/pbcon.py 'disp stat'   # PBCON_TMO is a per-command timeout
```

`tools/pbcon.py` holds one `open()` for the session and waits for the `pb> `
prompt; `tools/pbcon.sh` is a shell equivalent that requires
`stty -F /dev/ttyACM0 raw -echo` first.  Since session 15 `/init` **does not
reboot on a timer** — a boot stays up until the console's `reboot` command (or
the power button).  That costs exactly one `BOOTCTR_FCTRY` unit per boot.

**After every console command, dump the last 50–100 lines of the log and read
them** (`./tools/pbcon.py 'kmsg' | tail -60`).  An error the operator has to
notice for you is an error you should have found: the ACM console loses output,
kernel oopses/L3 firewall aborts land in `kmsg`, and a driver's own `pr_err`
report is often the only record of what a probe returned.  Session 20 lost two
boots that way (a misc-node `ESPIPE` that looked like a driver failure).
Non-seekable misc report nodes also mean userspace must **reopen them for every
read** (trap 63).

Console command reference (`initramfs/console.c`):

| command | what it does |
|---|---|
| `help` / `?` | the built-in summary |
| `ls [path]`, `cat <path>` | filesystem inspection (default `/`) |
| `peek <hexaddr> [n<=64]` | read n 32-bit words via `/dev/mem` |
| `poke <hexaddr> <val>` | write one 32-bit word and read it back |
| `kick on\|off` | status / toggle the userspace WDT2 `TGR` kick |
| `disp init\|stat\|mux\|col\|off` | replay the QNX display mode; `stat` is the fastest DSS/DISPC/DSI register dump |
| `bl init\|dump`, `bl rd <reg>`, `bl wr <reg> <val>`, `bl scan [bus]` | LM3532 over I²C |
| `i2c r\|w <bus> <addr> <reg> [val]` | 8-bit-register I²C |
| `i2c16 r\|w <bus> <addr> <reg16> [val] [n]` | 16-bit-register I²C |
| `spi tcon` / `spi tconbe` / `spi w <hexword>…` | MCSPI4: the 9-word Hitachi table (low / high byte first), or raw words |
| `fb <rrggbb>` | paint `/dev/fb0` — **always do this before judging the panel** |
| `kmsg [substr]` | filtered `/dev/kmsg` dump (20 000-record scan) |
| `grep <substr> <path>` | print matching lines of any-size text file (what `cat`'s 4096-byte cap cannot do; used for `clk_summary`) |
| `blk info` | `/dev/mmcblk1` size and mode |
| `blk sum <lba> [sectors]` | FNV-1a64 of raw sectors (hex LBA); read-only |
| `blk sumb <byteoff> <len>` | FNV-1a64 at a byte offset (hex offset, decimal length) |
| `blk rd <lba> [sectors]` | hex dump up to 4 sectors |
| `sink <len>` | read `<len>` bytes from the host and discard them: measures the console link alone (`tools/pbsink.py`); pace it, the ACM path drops bytes when blasted (trap 51) |
| `blkwr <byteoff> <len> [chunk]` | timed `O_DIRECT` write of a deterministic pattern to `/dev/mmcblk1` (hex off, 4096-aligned; FNV-1a64 of the bytes written).  Region-1 test writes only.  This is the tested large-write primitive (report 136) |
| `write <path> <text>` | write a string to sysfs |
| `entropy <hex>` | credit real entropy into the kernel pool (root `RNDADDENTROPY`).  **The SoC RNG is firewalled, so the CRNG never seeds itself** — run this before any RPMB write (trap 87).  32+ hex digits; verify with `kmsg crng` (`crng init done`). |
| `reboot` | reboot now (returns to the boot ROM) |

**The factory counter tops itself up at boot.**  `/init` runs `pbrpmb topup
0x1E` after the console is up: it reads `BOOTCTR_FCTRY` and writes `0x32` only
when the value is inside `1..0x32` and **below `0x1E`** — one sealed frame, one
result read, one read-back, never a loop (report 175).  Its outcome is one
`pbrpmb: topup: ...` line in `kmsg` (`cat` the log or `kmsg topup` on the
console); the earlier modes are `>= threshold, no write` and `PASS`.  Because a
factory boot spends one unit, a normal boot at `0x32` lands at `0x31` and does
**not** write.

Everything that touches hardware runs forked with a bounded wait, so a gated
target's L4 abort is a SIGBUS in the worker, not a dead console (trap 14). Near a
faulting access, `/dev/kmsg` is not trustworthy — write the DRAM ring
(`ARCHITECTURE.md` §3).

### 5a. The `pb_reg` bridge hook (no boot per hypothesis)

`kernel/source/dsi.c` exposes a write-only sysfs attribute through which any
TC358764 register can be read or written over DSI from the console.  The
console passes only the **first token**, so the argument has no spaces:

```bash
P=/sys/bus/platform/devices/58004000.encoder/pb_reg
PBCON_TMO=10 ./tools/pbcon.py \
  'write '$P' w0524:0'    # write register 0x0524 = 0
  'write '$P' r0214'      # one read, one attempt
  'write '$P' t0214'      # 16 reads, re-arming the command VC before each
  'write '$P' u0214'      # 16 reads, no re-arm (control for trap 37)
  'write '$P' d0000:0580' # dump every 32-bit bridge register in the range,   8 per kmsg line
  'write '$P' v'          # is the video VC still being fed?  (PACKET_SENT)
PBCON_TMO=20 ./tools/pbcon.py 'kmsg pb reg'
```

Two things to know (report 132 §3): bridge **reads** only work reliably when
`PPI_TX_RX_TA` was `0x00040006` **before `PPI_STARTPPI`** ran — the register is
latched at that write, so a live `w013c:40006` does nothing.  And a failed read
clears `VC_EN`, which is why `r`/`t` re-arm first.

### 5b. The QNX DSI register oracle

`tools/pbscp.sh` moves files (`:/path` = the device; directories need `-O`,
and `/tmp` cannot `mkdir` because it is QNX's `/dev/shmem`).
`tools/qnx-dsiread.c` → `tools/pbdsi` reads the same registers **from a running
TabletOS**, i.e. from a display that works — the reference values that make a
whole-map diff possible.  Build (QNX 6.6 toolchain, `SETUP.md` §2):

```bash
. tools/env.sh
$QNX_HOST/usr/bin/arm-unknown-nto-qnx6.6.0eabi-gcc -O2 -Wall \
  -o tools/pbdsi tools/qnx-dsiread.c
./tools/pbscp.sh tools/pbdsi :/tmp/          # scp with the door's legacy algorithms
./tools/pbssh.sh 'chmod +x /tmp/pbdsi; /tmp/pbdsi dump 0 580'
```

`/tmp` on QNX is wiped by a reboot — re-push after every one.  The tool is
read-only, but it injects packets into a live link: about half the reads fail
with an ACK/error `0x0003` (that is the reference's own `PPI_TX_RX_TA = 4`) and
it retries.

## 6. Restore and verify (do this before ending a session)

```bash
nohup python3 tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs \
  --dest-block 0x825 --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
sleep 2; ./tools/pbcon.py 'reboot -f'     # the unit is running our Linux image
until grep -qE 'COMPLETE|done|error' /tmp/f.log; do sleep 2; done
# the flasher resets the loader itself; only if it logs "loader still enumerated":
#   timeout 40 python3 tools/force_reset_loader.py
for i in $(seq 1 60); do timeout 12 ./tools/pbssh.sh 'echo DOOROK' 2>/dev/null | grep -q DOOROK && break; sleep 5; done
tools/pbssh.sh 'uname -a; mount | grep base'      # 6.6.0 … and /dev/hd1t179 on /base
tools/pbssh.sh 'cat /dev/rpmb/BOOTCTR_FCTRY' | xxd -p     # expect 32000000
```

**The boot is the verification** (operator, session 27): the loader
CRC-verifies the image before it runs and the OS mounts `/base`, so if the unit
comes up on 2.1.0.1917 the flashed IFS is the one on media — a read-back hash
adds nothing and is no longer part of the loop.  Check instead that the OS
boots, `/base` is mounted, and the counter reads `0x32` (root ssh can finally
read `/dev/rpmb`).

**Restore the 1917 IFS, not `local/images/ifs0.sealed.bin`.**  That file is the
**2.0.0.4869** IFS and is incompatible with the 1917 `/base`/UFS on the media
(trap 84).  The 1917 IFS with the `rfs_validator` shim is
`local/work/ifs-shim-sealed.ifs` (7 900 548 B, block `0x825`), and the full way
back — IFS + RCFS + UFS — is `local/work/back-1917.signed`.

The payload copies are `local/work/ifs-shim-sealed.ifs` (the 1917 IFS — **not**
`local/images/ifs0.sealed.bin`, which is the 4869 IFS, trap 84) and
`local/images/kernel*.img` (the tagged builds). If the restore flash
misbehaves, `--delay 40` is the trick that catches the *next* WDT2 self-reset
instead of the bootrom you just left (report 103 §6.3). The `BOOTCTR` line is a
**read**: see `SAFETY.md` §2 before writing anything there.

## 7. Reading the reference (QNX) while it is up

```bash
./tools/pbssh.sh 'on -C 0 /accounts/devuser/memdump3 4a100000 0x200'   # CORE padconf
./tools/pbssh.sh '/proc/boot/pmic-twl6030-ctl get_state vaux3'          # rails (read-only!)
./tools/pbscp.sh tools/pbi2c :/tmp/ && ./tools/pbssh.sh 'chmod +x /tmp/pbi2c; /tmp/pbi2c scan 0'
```

`/tmp` on QNX is wiped by a reboot; re-push `pbi2c` after every one. Use
`get_state`/`get_vout` only — `set_state vaux1 off` takes the unit down (trap 26).
`memdump3`/`memw32`/`memtest` sources are in `tools/`; the device copies live
outside the repo and are pushed the same way.

## 8. Loop discipline

* **Judge the panel only after `fb <rrggbb>`.** A black framebuffer and a dead
  panel look identical (report 121 §1.2).
* **Read `kmsg` before theorising.** Two rounds of GPIO guessing were answered
  by one `kmsg tc358764` (report 121 §7.1).
* **Do not write DSS targets from userspace** on this kernel: a write while the
  module is in standby never completes and wedges PID 1's `reboot()` (trap 14).
* **Do not do blocking I²C** from the console (trap 16).
* **One variable per boot.** Never mix a timing experiment with a structural
  change; the epoch instrument and the four-register timing experiment are both
  single-purpose builds.
* Budget boots against `BOOTCTR_FCTRY` (`SAFETY.md` §2): one successful
  factory/CRC boot spends one, a reset loop spends them faster, and only a
  booted TabletOS puts one back. The agent must never write RPMB.
* Restore the sealed IFS at the end of every session, and verify the sha256.

## 9. Recovering the TabletOS UFS (`hd3`) with bb10mt

The autoloader is Windows-only (USB drivers), so use `bb10mt` on the host.  Set
`BB10MT` to a clean working directory containing the `bb10mt` binary and the
`.exe` autoloaders (ignore old scratch directories).

```bash
cd "$BB10MT"

# 1. autoloader .exe -> cap.exe + .signed      (split != unpack!)
./bb10mt split -i PB-4869-root.exe
#   -> PB-4869-root.0.signed

# 2. .signed -> the partition components + .lst
./bb10mt unpack -c PB-4869-root.0.signed
#   -> PB-4869-root.0.4.ufs  (compact UFS image, 2,550,202,368 B)

# 3. UFS-only list; use a NEW name -- unpack rewrites <container>.lst
printf 'PB-4869-root.0.4.ufs=16640,0\n' > PB-4869-root-UFS-src.lst

# 4. pack the UFS into a signed QCFM container
./bb10mt pack -l PB-4869-root-UFS-src.lst -v 1,2 -s true \
             -c PB-4869-UFS.signed

# 5. put the unit in the bootrom (0fca:0001), then:
./bb10mt flash -i PB-4869-UFS.signed \
  -r "$REPO/local/loaders"
#   "Enter password (10/10):" -> 0000   (interactive; stdin is not used)
```

The UFS component is a **compact** image: no plain QNX6 superblock at 0 or
0x2000, and `qnx6-usage` cannot parse it.  To inspect or edit it, mount it:

```bash
./bb10mt qnx6 mount -i PB-4869-root.0.4.ufs -m <empty-dir>   # FUSE, rw
fusermount -u <empty-dir>
```

After editing the mounted image, re-pack (step 4) and verify the packed
container by `unpack`ing it again and comparing the `.ufs` sha256.

### What the bb10mt options mean (from its source)

The source is public: <https://github.com/bb10root/bb10mt> — the notes below are
from commit `d45b66ba9594a2912dd005e3607da1d4825fb06e` (`uqcfm.pas` for the
command parameters, `qcfm.pas` for `_packMFCQ`/`signature_data`, `ramloader.pas`
for `FlashFile`).  The binary in `$BB10MT` is not vendored in
this repo.

| command / option | what it does (source) |
|---|---|
| `split -i <autoloader.exe>` | `ExtractBlackBerryAutoloaderFromPE`: pulls `cap.exe` + `<name>.0.signed` out of the Windows autoloader PE. |
| `unpack -c <signed>` | `unpackMFCQ`: extracts the QCFM payload's components (`.mbr`, `.rcfs`, `.ifs`, `.ufs`, …) and writes the `.lst`.  **Not the same step as `split`.** |
| `pack -c <out.signed>` | Writes a QCFM/MFCQ container from `-l <list>` / `-i <files>`. |
| `-v <list>` / `--versions` | **QCFM header versions as a bitmask**: `1` = write the v1 `qcfp` records, `2` = write the v2 `pfcq` records.  The default is `2` (v2 only).  `-v 1,2` = `3` = **both**; this is the form the operator found the loader accepted for the repacked UFS. |
| `-s true` / `--sign` | Adds the hard-coded 560-byte `QNXH`/`QNX-OS-1` **signature trailer** (`signature_data`).  `flash` detects a trailing `QNXH` word and sends the 560 bytes separately as the signature. |
| `-f` / `--fast` | Include empty blocks (skip the sparse-block analysis); larger container, rarely wanted. |
| list entry `file=Delta,flags` | `Delta` is a **64 KiB block offset** added to every run record of that file; `flags` is the QCFM flag word.  The UFS pack uses `=16640,0` (the loader maps the block; it is not a raw device offset). |
| `flash -i <signed> -r <loaders>` | Bootrom (`0fca:0001`) + device password (`0000`).  `TRamLoader.FlashFile` streams the container payload **verbatim** in `MAX_FLASH_BLOCK` chunks and then the 560-byte signature; it does **not** parse or resize the QNX6 filesystem. |
| `qnx6 mkfs\|mount\|compact\|fsck` | Host-side QNX6 helpers (`mkfs`/`mount` use the same `TQNX6Fs` code; `mount` is FUSE).  The mounted UFS tree is ~298 MB apparent.  **`mkfs` compatibility with the on-device QNX6 is not verified** — treat the device's own `mkqnx6fs` as the authority. |

The compact UFS container is portable across 16/32/64 GB units because the
device-side loader/format expands it to the target partition (report 138 §4),
not because bb10mt resizes it.

### Two things that bite when building a *flashable* container

1. **`-s true` is a *dummy* signature, and that is deliberate.**  Measured
   2026-11-01: it grew the container by **8 bytes** (`ffffffff` + CRC32), not the
   560-byte trailer the source describes.  Per the operator: when modifying a
   firmware's **UFS** partition you must **skip the `.sig` component** (if present)
   and pack with `-s true` — the device boots fine with the dummy signature.
   Why: inferred, not measured — the real `QNXH` trailer belongs to the unmodified
   image, so it cannot still describe a changed UFS, whereas the dummy apparently
   does not gate booting.  The vendor `2.1.0.1917.signed` ends in the full 560-byte
   trailer with its CRC field left as `ffffffff`.  `bbusb.py pack -s` writes
   the 560-byte form; use bb10mt's `-s true` when you specifically want the dummy.
2. **`Delta` goes into the V1 `qcfp` record, and V2 gets the block index without
   it** — by design, because path 2 resolves the partition rather than an offset.
   Verified three ways: `qcfm.pas:559` vs `:582`, the vendor container (IFS V1
   `16`, V2 `0`; RCFS V1 `192`, V2 `0`), and the container code in `tools/bbusb.py`, which reproduces
   bb10mt's own output **byte-for-byte (0 differing bytes) at `Delta` 16 and 192**.
   An earlier note in this file said the opposite ("`Delta` is dropped for
   `.ifs`"); that measurement was wrong and is retracted in
   `docs/contradictions/100-bb10mt-flash-path2-vs-operator-flashes.md`.  The
   practical consequence is reassuring: a bb10mt/qcfm IFS container addresses
   `0x815 + 0x10 = 0x825` through V1, which is the IFS slot — the same destination
   `pre_os_ifs_flash.py` reaches with `rrcq.offset = 0x20` under the V2 convention.

Also: **`bb10mt info` prints mostly wrong data for the PlayBook** (operator,
2026-11-01).  Only CFP's `info` is correct — see report 161, and
`tools/bbusb.py info` for the parts we can read without Windows.

**Keep `accounts/devuser/startup.sh` in sync** between the live unit, the UFS
image and the operator's on-device copy (`local/work/devuser-startup.sh`).  An older copy stops the `BOOTCTR_FCTRY` top-up: its first
`dd` runs before the devuser overlay interpreter exists (trap 29), the guard
refuses to write, and the factory budget drains (0x32 → 0x2d was observed).
The good script (4490 B, sha256 `c265cfbb…`) waits for a working `dd` first.

**The bootrom password is `0000`.**  `bb10mt flash` prompts on stdin; run it
interactively.  **`bbusb.py --password` has no default, deliberately.**  Ten wrong attempts wipe
UFS/RCFS/IFS and the password NVRAM record (`SAFETY.md` §7), so a guessed password is
not worth the risk: omit the flag and you are prompted only if the device actually
challenges, or pass `--password 0000` explicitly for this bench unit.  A supplied
password is sent **once** and never retried; interactively the device's own challenge
is re-presented with the remaining budget (`9/10`); any other response aborts with the
code.  Two of these rules are **bb10mt's own** (`bbusb.pas`): it stops once the device
reports two attempts spent (`:493`), and it re-reads an empty line instead of sending
it (`:497-500`).  We are stricter in one place: bb10mt loops forever on a response that
is neither `0x10` nor `0x0E`; we abort and print the code.  After a rejection the boot rom may stop offering a challenge entirely until the
unit boots the OS — do not read that silence as "wrong password" and retry.
`--debug` prints each `password_info` response (`0x0E` = challenge, `0x10` = accepted).

## 10. `bbusb.py info` — device info without Windows

CFP is the only correct `info` for this unit (`bb10mt info` is mostly wrong), and
CFP is Windows-only.  `tools/bbusb.py info` reproduces the parts we can already read
through `bbusb`, and it is **read-only by construction** — it issues only
`0xB1`/`0xB4`/`0xD9`/`0xE7`/`0xDE action 0`/`0xB0`, with no flash, no boot-mode
change and no RPMB:

```bash
cd "$REPO"
pkexec python3 tools/bbusb.py info                  # unit must be in BootROM mode
pkexec python3 tools/bbusb.py info --from-bootrom   # upload the ramloader first
python3 tools/bbusb.py info --json --raw-out /tmp/info-dump
python3 tools/bbusb.py info --mct-file mct.bin    # offline: parse a saved 0xD9 dump
```

Two practical notes.  **The PIN is printed as the raw u32's hex digits** — a live
`0xE7` read returns `0x50175127`, so `info` prints `50175127` as "the PIN" and
keeps the raw value beside it.  **A unit left in the ramloader from a
previous run** refuses `set_mode(2)` on the next connect; `_open_ramloader` treats
that as benign and lets the password stage decide, and `bbusb.py reset-loader` sends
the `0x80EF`/`0xEF` reset (`tools/force_reset_loader.py` is the same thing).

**The boot-ROM metrics are a bootrom-mode read** (channel 0, cmd 5, var 2), captured
during the handshake.  Asking for them after the loader upload times out — that was a
bug, now fixed: run `info --from-bootrom` to get them, and the tool says so if the
session started in the loader.  Only `br_version` (`+0x0C`) and `model_id` (`+0x10`)
are decoded from known offsets; the rest is printed as printable strings + hex.
DRAM info (`0xBF`) and the vendor/branding id (`0xDB`) are now decoded too — verified
field-for-field against CFP's own output (1024 MB / Elpida / rev 0x100, branding 504).
**CSD/ExtCSD is decoded into 50 named fields** and the bootrom block into 13, using
tables solved by aligning CFP's printed field list against a live capture; every
overlapping value agrees with CFP (asserted in the test suite).  Raw captures for
that work live in `docs/reports/data/live-info-2026-11-01/`.  `os_metrics` is decoded
too — OS version, metrics version, image address range, build user/date/time, hardware
id and the `0xC3D0E193` image magic.

**The loader action log is `0xB3`, not `0xB0`** (report 165).  `0xB0` is the *bugdisp
crash log* — plaintext marker record plus ciphertext payload, which CFP writes
verbatim to `c:\log.bug`.  The action log is plain ASCII in NVRAM record `0x2033`
with no crypto at all.  Its request payload **must be 132 bytes**
(`{u32 action, char text[128]}`, `action = 0` for a read): the loader's handler does
no length check, and the append path (`action = 1`) would write into NVRAM.  `info`
shows the action log and keeps the crash log as a separate, labelled section.

(Historic note: before report 165 we read `0xB0` and reported a placeholder,
because the loader's log path goes through the signature gate `0x801184AC` and a
transform (`0x8011F324`) that our session does not unlock; CFP's `0x461750`
turned out to be a plain `memcpy`, so CFP does not decrypt either (report 164
addendum 7).  There is only one ramloader image, so the difference is *how the
log is requested*: our `0xB0` reply is a `Bugdisp-r036` crash container, while
the action log is `0xB3` — plain ASCII in NVRAM record `0x2033` (report 165).
`info` keeps the crash log as a separate, labelled section.)

It prints block size, the PIN as both the raw u32 and its hex display form, boot
mode and factory count, the flash-region identity, the MCT TLV table (types from
report 100), the loader action log, and — since reports 162/164 — the eight reads
CFP does that we had not identified: the `0x2074` OSREGION record (`0xE9`),
CSD/ExtCSD (`0xB5`), DRAM info (`0xBF`), hardware override ID (`0xBD`), vendor ID
(`0xDB`), installer metrics (`0xE1`), OS metrics (`0xD8`) and DSP OS metrics
(`0xB2`).  Those payloads are mostly opaque, so the tool prints length and a hex
head rather than inventing structure; `0x2074` is printed as words.  `0xB2` is a
stub in LDR_01 and always returns zeros (report 164).

**The PIN encoding is not reconciled with CFP.**  CFP displays PIN `50175127`;
`0xE7` returns `0x50175127`, whose hex digits also read `50175127`, so the
*display* matches — but session 22 recorded CFP's underlying value as
`0x02FD9C97`, which is a different encoding.  Do not assume `0xE7` and CFP's PIN
read are the same field, and do not "fix" the number until a live `0xE7` is
compared against CFP's own read.

The MCT header size is not documented, so the parser *searches* for it
(8/7/12/16) and reports which it used — check that line before trusting the TLV
list.

Comparison baseline: `docs/reports/data/cfp-2026-11-01/` and report 161.

## 11. `bbusb.py pack | unpack | split | parse` — containers without bb10mt

The container format (`mfcq` / `qcfp` / `pfcq` / `rrcq`) is transcribed from
bb10mt's source, not from the binary, so this works without bb10mt installed:

```bash
python3 tools/bbusb.py parse PlayBook_2.1.0.1917.signed        # records + signature
python3 tools/bbusb.py parse /tmp/x.signed --json
python3 tools/bbusb.py pack -l images.lst -v 1,2 -s -c out.signed
python3 tools/bbusb.py split PB-4869-root.exe -o /tmp/spl      # .exe -> .signed  (bb10mt split)
python3 tools/bbusb.py unpack out.signed -o /tmp/ex           # .signed -> components
python3 tools/bbusb.py unpack big.signed -o /tmp/ex --only 4  # just the .ifs
```

`-l` takes bb10mt `.lst` lines verbatim (`file=Delta,flags`), `-v` is the same
bitmask (`1` = V1 `qcfp`, `2` = V2 `pfcq`, `1,2` = both — the form this loader
accepts), `-s` appends the 560-byte `QNXH` trailer, `-f` skips the 0xFF block
analysis.

**Acceptance tests that matter** (all against bb10mt's own output, on real files):

| operation | check | result |
|---|---|---|
| `build` 44-block IFS, `-v 1,2` | vs `bb10mt pack` | **0 differing bytes**, at `Delta` 16 and 192 |
| `split 2.0.0.4869-IFS.exe` | vs `bb10mt split` | **identical sha256** (`34f53ae9…`, 2,949,160 B) |
| `extract` that container | vs `bb10mt unpack` | **identical sha256** (`1884be93…`) and the same `.lst` line `=16,0` |
| `extract --only 0,3,4` of `PlayBook_2.1.0.1917.signed` | vs `bb10mt unpack` | `.mbr`, `.sig`, `.ifs` all **identical sha256**; `.lst` deltas match (`mbr=8192,2`, `rcfs=192,0`, `mbr=0,0`, `sig=15,0`, `ifs=16,0`, `ufs=16640,0`) |

If any of those stops matching, the transcription has drifted; check `qcfm.pas`
before flashing anything.

What it does that bb10mt's binary does not: the `.lst` `Delta` lands in the V1
record where the loader reads it, the signature trailer is the full 560 bytes, the
container can be built from raw component files with no template, and `extract`
streams (a 1 GB container holding a 2.5 GB UFS does not need the RAM).  Container
layout and the V1/V2 split are documented at the top of the module; `extract`
copies qcfm.pas `Chunk2Stream` exactly, including **0xFF-filled holes** between
runs, which is why a sparse component comes back at full size.

Reading a container also validates the signature CRC when one is present — note
the vendor file leaves the CRC field as `ffffffff`, so `parse` reports `MISMATCH`
for vendor images while the container itself is fine.

**Still bb10mt-only**, for full independence: `qnx6 mount` (FUSE), `mkfs`,
`compact`, `fsck`, and the in-place QNX6 shrink used for `hd3`.  Those are
host-side filesystem tools, not container or protocol work.

## 12. `bbusb.py flash` — flash a container, or a bare component

Dry run unless `--apply`.  Two inputs, matching what `ramloader.pas:FlashFile`
accepts:

```bash
# whole container, sent as-is; its V1 run records say where everything goes
python3 tools/bbusb.py flash --signed PlayBook_2.1.0.1917.signed
python3 tools/bbusb.py flash --signed out.signed --apply

# bare component: wrapped into a -v 1,2 container with Delta = dest - 0x815
python3 tools/bbusb.py flash --component PB-4869-root.0.3-crc.ifs --dest-block 0x825
python3 tools/bbusb.py flash --component PB-4869-root.0.3-crc.ifs --dest-block 0x825 --apply
```

It prints the plan first — every run as `block`, raw byte range and offset — and
refuses to touch USB if any run falls outside the measured MCT extents (`0x1B`
`0x815..0x4804`, `0x1C` `0x4905..0xFFFFF`).  Checked against the vendor
`2.1.0.1917.signed`: all 12 runs pass, and the IFS lands on `0x825..0x856`.

Wire sequence, verified against `ramloader.pas:925-1071`:
`0x20EE` PreFlash (`$15`, or `$40` when `model_id & 0xFFFF == 0x2C0A`) →
`0xF7` frames `{u32 seq, u32 len}` + ≤`0x3FEC` bytes → `0x40F9`
`{u16 560}` + signature → `0x40C0` Complete.  A trailing 560-byte `QNXH` block is
stripped from the payload and sent as the signature, exactly as FlashFile does.

**`tools/bbusb.py flash` is the flasher** (`DECISIONS.md` §29).  The older
`tools/pre_os_ifs_flash.py` clones a vendor template and is kept for historical
recipes only; both send the same frames.  `bbusb.py` removes the template
dependency, adds whole-container flashing (both regions in one session) and
carries the 560-byte `signature_data` constant that the old tool reads from
bb10mt's `qcfm.pas`.

**Hardware-proven 2026-11-03 (report 169).**  `bbusb.py flash --signed` wrote a
363,790,928-byte container (1917 RCFS + our shim IFS, 22,232 frames, signature,
`COMPLETE`) and the unit booted PlayBook OS 2.1.0.1917 with `/base` mounted.
Run it as the **user**: the USB node carries a `user:psyden` ACL; `pkexec` runs
as root, which lacks the user's `pyusb`, and a killed `pkexec` left an orphan
root `bbusb.py` holding the interface (and a wedged ramloader that only a
power-cycle cleared).  `--from-bootrom` opens the device for only 10 s, so arm a
`while ! lsusb | grep -q 0fca:0001; do sleep 0.5; done` wait first, or run the
flash with the unit already in the ramloader.

## 13. `bbusb.py reset-osregion` — clearing the OSREGION marker without Windows

This is CFP's undocumented `RESETQNXOSREGION`, which has been the only documented
way out of a failed `rfs_validator` (the unit shuts down and refuses to boot that
IFS).  Two independent static analyses agree on what it is:

* CFP.exe (report 163): handler `0x404908` sends the frame
  `0C 00 | E8 | 00 | 04 00 00 00 | ~CRC32`, once, then prints "QNX OSREGION NVRAM
  record cleared." when the status is `6`.
* LDR_01 (report 164): `0xE8` with `arg == 4` runs `0x80103F18 movw r0,0x2074` /
  `0x80103F1C bl 0x8010B8BC` — **delete NVRAM record `0x2074`**.  `arg == 8` is the
  other action (read-modify-write `0x2814` bit 4, the `SETNFC_LOCK_FLAG` verb).

So nothing is written: the 24-byte record is deleted.  The flash `COMPLETE` (`0xC0`)
path writes that same record (report 164), which is why it can represent "region
valid" and why deleting it clears the state.

```bash
python3 tools/bbusb.py reset-osregion                 # read only (dry run)
python3 tools/bbusb.py reset-osregion --apply         # 0xE8 arg 4, then read back
python3 tools/bbusb.py reset-osregion --apply --json
```

**Read the read-back, not the status.**  Report 164 records `0x2E` on both the
success and the failure path of this delete, so the response code cannot
distinguish them; CFP's "cleared" message is therefore not proof either.  The tool
prints the record before and after and says plainly whether it changed.  This is an
NVRAM write: operator-initiated, dry run by default.

## 14. Offline tests for the USB tools

```bash
python3 tools/tests/test_offline.py        # exit 0 = all passed
```

No device, no USB: a stub `bbusb` in `sys.modules` returns canned replies shaped
like the ones in reports 100/162/164, so the tests exercise our code paths —
flashing frame assembly, container build/parse, the MCT walk (including a padded
240-byte reply), the host-side gate, and `reset-osregion`'s read/delete/read-back logic —
rather than the hardware.  They also check the negative case: a second delete on an
already-clear record must report **UNCHANGED** rather than claim success.

Three real bugs were found this way on 2026-11-01, so run it after touching any of
these tools: the MCT parser rejected padded replies, `reset-osregion --json` polluted
stdout with log lines (breaking machine parsing), and its logger captured
`sys.stdout` at import time so redirection silently stopped working.
