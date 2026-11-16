# 249 — a writable root on hd3, grown to the partition; the eMMC cache-flush fault

Date: 2026-11-16 (session 58 continuation, Linux-era 52).
Bench: the unit boots the Nura root **read-write** from hd3, region 0
**`0x294F8E36`** (`0x9E0980` B, `boot/chainloader/kernel.img` sha256
`f0738625…`, kernel `7.3.3 #77`), and SSH is back over the RNDIS link.
The filesystem is grown to **56.5 GB** (`53.9 GB` free).  The one hardware
fault is the eMMC's cache flush.

## 0. Headline

* **SSH is back and persistent.**  A static dropbear + host keys + the
  authorized key are installed into the root by the initramfs; an OpenRC
  `local.d` script brings `usb0` to `169.254.0.1/16` and starts dropbear on
  `:22`.  `PB_LINK=usb tools/pbssh.sh 'cmd'` is a clean shell.  The ttyGS0
  getty stays as the recovery console.
* **The root is writable** (no overlay), and a one-shot `resize2fs` grows it
  from 626 MB to the whole hd3 region: `/dev/loop0 56.5G, 53.9G free`.
  A file written before a reboot is present after it.
* **The eMMC cache flush is the fault.**  A journalled write at 20 s hit
  `mmc1: Card stuck being busy! __mmc_poll_for_busy` and aborted the journal
  (the session-54 stall, reproduced).  Mounting `barrier=0` and setting the
  card and loop queues to `write through` keeps the FS writable; the grow then
  succeeds.  An **explicit** `sync` still stalls, because the MMC core issues
  `FLUSH_CACHE` whenever `EXT_CSD.CACHE_SIZE != 0`.
* **The full dmesg was read** (694 lines).  Besides the flush and one recovered
  DSI modeset, every other warning is expected or cosmetic (list in §5).
* **The counter self-maintained:** `BOOTCTR_FCTRY` read `0x1B` and `/init`
  wrote `0x32` (RPMB write counter 12049); the session's reboots had drained
  `0x25 → 0x1B`.

## 1. The shell

`openssh-server` is not installed (only `openssh-server-common`, with no
binary).  The session-48 static **dropbear** (968 032 B, musl, static) and the
two host keys are embedded in the initramfs; `/etc/local.d/pb-ssh.start` is
installed into the root on first boot:

```sh
ip link set usb0 up
ip addr add 169.254.0.1/16 dev usb0
[ -x /usr/sbin/dropbear ] || exit 0
pgrep -x dropbear >/dev/null && exit 0
/usr/sbin/dropbear -r /etc/dropbear/dropbear_ed25519_host_key \
  -r /etc/dropbear/dropbear_rsa_host_key -p 169.254.0.1:22 -P /run/dropbear.pid
```

`usb0` exists from the initramfs gadget (ACM + RNDIS); the key is
`local/secrets/rsa`.  `PB_LINK=usb tools/pbssh.sh` connects as root.

**Bulk transfers must be paced.**  Two direct `nc` copies of the 968 032-byte
dropbear arrived short (724 960 and 752 384 bytes) when the host sent in one
burst; 8 KiB chunks with a 5 ms pause are byte-exact.  Use the paced push.

## 2. The writable root and the grow

`initramfs/init.c` now mounts hd3 read-write directly (no overlay):

* open `/dev/mmcblk1` **O_RDWR**, loop with `lo_flags = 0`, offset `0x49000000`;
* `mount(…, "ext4", 0, "barrier=0")`;
* `install_bench_tools()` installs the SSH kit and the two `local.d` scripts on
  the first boot;
* chroot to the root and exec `/sbin/init`.

`/etc/local.d/pb-grow.start` runs once:

```sh
[ -e /etc/pb-grown ] && exit 0
dev=$(awk '$2=="/"{print $1}' /proc/mounts)
resize2fs "$dev" >>/etc/pb-grow.log 2>&1 && touch /etc/pb-grown
```

Measured on the clean hd3 image (`f7858110…` reflashed first, because the
aborted journal had set `s_state` errors and `resize2fs` refuses while errors
are recorded):

```
resize2fs 1.47.4
Filesystem at /dev/loop0 is mounted on /; on-line resizing required
old_desc_blocks = 1, new_desc_blocks = 8
The filesystem on /dev/loop0 is now 15273984 (4k) blocks long.
EXT4-fs (loop0): resized filesystem to 15273984
```

`df -h /` → `56.5G ... 53.9G available`; `/etc/pb-grown` present.  A
`/root/persist.test` written before a reboot was read back after it.

The **media** hd3 is now the grown FS with the installed kit; it is no longer
byte-identical to `f7858110…`.  That image remains the flasher-restorable way
back.

## 3. The flush, and why `barrier=0` is not enough

The first read-write boot (no barrier change) reproduced session 54 exactly:

```
mmc1: Card stuck being busy! __mmc_poll_for_busy
I/O error, dev mmcblk1, sector 0 op 0x1:(WRITE) flags 0x800800
Aborting journal on device loop0-8.
EXT4-fs (loop0): Remounting filesystem read-only
```

The block layer reports a flush at **sector 0** because a flush carries no
data.  The failing operation is the card's `FLUSH_CACHE` (or a write's FUA),
and the card stays busy past `__mmc_poll_for_busy`.

With `barrier=0` ext4 stops issuing the journal flush; the FS stays writable
and the grow completes.  But an **explicit** `sync` (or any `blkdev_issue_flush`)
still reaches the card: the MMC core gates flushes on
`card->ext_csd.cache_size`, not on the queue's write-cache flag, so setting
`/sys/block/mmcblk1/queue/write_cache` to `write through` does not suppress it.
Measured: `sync` at 45.8 s → stuck busy, `sector 0 … flags 0x900000`, one lost
async page write; the FS stayed `rw` and no journal aborted.

**The clean fix** is to make the card look cacheless to the MMC core:
set `EXT_CSD.CACHE_CTRL` (index 33) to 0 and/or add a board quirk clearing
`card->ext_csd.cache_size`, so `mmc_flush_cache()` becomes a no-op.  Then no
flush command is ever issued.  This is the next item; data writes themselves
are proven (a 16 KiB `O_DIRECT` probe into unused space and the 15-million-block
resize both succeeded).

## 4. The counter and the RNDIS pacing

`pbrpmb: topup: BOOTCTR_FCTRY at block 0x3a record@196 = 1b000000` then
`pbrpmb: topup: writing 0x1b -> 0x32 (RPMB counter 12049)`: the session's
flash boots and fast resets drained `0x25 → 0x1B`, and the image's own top-up
(threshold `0x1E`) replenished it.  One successful boot spends one unit.

The RNDIS RX path at 6.3 MB/s is real, but an unpaced host→device burst loses
data before `nc` writes it; pace at 8 KiB / 5 ms (~1.6 MB/s).  (This is the
transfer side, not the driver: the same file arrives byte-exact paced.)

## 5. Full dmesg review (kernel `#77`, 694 lines)

The dump is preserved as
`docs/captures/playbook-boot-2026-11-16-rw-writethrough-dmesg.txt`; the first
working read-write boot (`#76`, with the grow) is
`docs/captures/playbook-boot-2026-11-16-rw-barrier0-grow-dmesg.txt`.

**Real:** the flush stall (§3) and one `DSI: omapdss DSI error: Failed to set
dsi_if_enable to 0` at 52.7 s, followed by a successful PLL re-lock and
hs-clock packets at 54 s — a modeset that recovered (catacomb running,
backlight on), not the wedged-link case of trap 289.

**Cosmetic or expected:**

* `WARNING … arm_dt_init_cpu_maps` and `DT /cpu 2 nodes greater than max cores
  1, capping them` — from `maxcpus=1`.
* `Unknown kernel command line parameters "maxcpus=1"`.
* `PLAYBOOK WDT: crr … (KICK FAILED)` before the WDT init; the later line shows
  `ldr fffc1800->ffe2b400 (58.6 s window), pet 10s`.
* `hw-breakpoint: Failed to enable monitor mode on CPU 0`.
* `twl6030_uv_to_vsel: OUT OF RANGE! 1375000/1410000 Vs max 1316660` — clamped.
* `ti-sysc 4a318000.target-module: probe … failed with error -16`.
* `gpio … Persistence not supported for GPIO N` (many).
* `Skipping twl internal clock init …`; `lm3532 … ramp-up/down-ms missing`.
* `ocp:…mpu:fck: device ID is greater than 24` (and `pmu:fck`) — the clock
  domain table; clocks work.
* `gpmc_mem_init: disabling cs 0`; `armv7-pmu … no irqs`; `card claims to
  support voltages below defined range` (the WL1283 SDIO).
* `Warning: unable to open an initial console` — the pre-`/init` console.
* `PLAYBOOK: /init running as PID 1, no rootfs, no media access` — stale
  wording in `init.c`; it mounts the media immediately after.
* `hwmon … temp1_input not attached to any thermal zone`;
  `twl6030_usb: is N writeable?`; `battery with vmax -22 imax: -22` (the inert
  TWL BCI).
* `hmc5843/mpu3050: mounting matrix not found`; `wm8994: No interrupt
  specified`; `Mic1/2 detect/short IRQ -22`.
* `urandom warning(s) missed`; `wlcore: firmware booted` → `down` → `booted`
  (the re-probe).
* `EXT4-fs (loop0): recovery complete` — journal replay after a `reboot -f`;
  expected.

## 6. Next

1. Make the card cacheless (`CACHE_CTRL = 0` / a `cache_size = 0` quirk) so no
   flush is issued; then `sync`/shutdown are safe.
2. The OSK repeat (wvkbd; report 248) and the upload profile remain.
3. The stale `init.c` console message and `/dev/console` are one-line cleanups.
