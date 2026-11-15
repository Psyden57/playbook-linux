# 242 — the read-only hd3 root: the initramfs overlay, the new images, and the packed container (flash pending)

Date: 2026-11-15 (session 54, Linux-era 48).
Bench: the unit is **unchanged** — on the `0x060B25F2` initramfs + the dirty Nura
root on hd3 (report 241).  The new artifacts are on the host; **the flash did
not run** (the operator stopped before it).  The `pbcon` probe found the Nura
`ttyGS0` shell busy ("no prompt — the shell is still running a command"); the
ACM is `0fca:8010`, `ttyACM0` 0666.

## 0. Headline

The read-only hd3 + overlayfs root that report 241 §7 named as the next step is
**implemented and built**.  The initramfs now mounts hd3 read-only
(`LO_FLAGS_READ_ONLY` + `MS_RDONLY` + `noload`) and lays a tmpfs upper over it
with overlayfs; the hd3 image carries the WLAN firmware, no tty1 getty, no root
fstab line, and the 43 new modules; the region-0 image is CRC `0xE85217E5`; the
two are packed into one both-halves container (`c3ed1f61…`).  The remaining step
is the flash + the overlay boot.

## 1. The design

Report 241 §3 measured the eMMC stall on the first ext4 journal/udev write after
the 597 MiB bootrom image.  The fix here is to keep the eMMC out of the boot
path entirely:

* the backing `/dev/mmcblk1` is opened `O_RDONLY`; the loop is
  `LO_FLAGS_READ_ONLY`;
* the lower is mounted `MS_RDONLY` + `noload` (the `noload` tolerates the dirty
  media journal and prevents a replay write);
* a 256 MiB tmpfs at `/overlay` holds `upper`/`work`; overlayfs is mounted at
  `/newroot` with `lowerdir=/rootfs,upperdir=/overlay/upper,workdir=/overlay/work`;
* the `/dev`/`/proc`/`/sys` binds and the `lstat` hand-off are on the overlay,
  so the lower is untouched; the chroot/exec is unchanged.

`CONFIG_OVERLAY_FS=y` is built into the `0xE85217E5` kernel; `noload` and the
read-only loop are Linux 7.3.3 features, not new code.

## 2. The initramfs and the region-0 image

`arm-linux-gcc -static -O2 -Wall -o initramfs/root/init initramfs/init.c`
(only the pre-existing UDC `snprintf` truncation warning).  The kernel was
relinked with the new init in the cpio (`rm -f usr/initramfs_data.cpio*` first):

```
-rwxr-xr-x 1 psyden psyden 595784 Nov 15 17:18 init
```

`make zImage` + the DTB, then `build_kernel_image.py`:

```
image      : 0x9662A4 bytes; LoadEnd 0x80A65AA4; crc 0xE85217E5
```

`0x9662A4 < 0xB00000`, sha256 `fbeb6df071ce8fc20ddadee626e256b1983b369488d63bbde9bf1d7a31e2612b`;
saved as `local/images/kernel-0xE85217E5-overlay.img`; `boot/chainloader/kernel.img`
replaced.

## 3. The hd3 image

`local/work/nura/images/blackberry-playbook-root-overlay.img` is a copy of the
pristine `blackberry-playbook-root-hd3.img` (`076bafcf…`) edited with `debugfs`:

* `/etc/fstab`: the `UUID=aed244ce-… / ext4 defaults 0 0` line removed; only the
  header comment remains;
* `/etc/inittab`: the `tty1::respawn:/sbin/getty 38400 tty1` line removed; the
  `ttyGS0::respawn:/bin/sh -l` shell kept; the tty2–6 gettys kept;
* `/lib/firmware/ti-connectivity/{wl128x-fw-4-sr.bin,wl128x-fw-5-sr.bin,
  wl128x-nvs.bin}` written (the `wlcore` `-2` errors of report 241 §5);
* the 43 `.ko` under `/lib/modules/7.3.3/kernel/` replaced from
  `local/work/modules-7.3.3` (the `modules.dep` is unchanged — the same 43
  names); `e2fsck -fn` exit 0.

Both the kernel (`local/kernel-tree/vmlinux`) and the new `wl12xx.ko` report
`7.3.3 mod_unload modversions ARMv6 p2v8`; the module ABI matches.
Image: 625 999 872 B, sha256
`1f14915fab795560fe208466bfc4264a1382311d7f00d510c91aff368a09d662`.

## 4. The packed container

The docs' supported path is both halves in one bootrom session, so:

```
printf 'boot/chainloader/kernel.img=16,0\nlocal/work/nura/images/blackberry-playbook-root-overlay.img=16640,0\n' > /tmp/both.lst
bbusb.py pack -l /tmp/both.lst -v 1,2 -s -f -c /tmp/both.signed
```

`/tmp/both.signed` is 635 961 936 B, sha256
`c3ed1f61f979d014512e4e5b63ae8545245dfe225ac1ea3e22dafb4d7d44ee2d`; `parse`
shows the V1 runs at offset **16** (151 blocks, region 0) and **16640**
(9552 blocks, hd3), V2 at `0xA0`, signature CRC `0x493FA440 OK`.  Saved as
`local/work/nura/images/both-0xE85217E5-overlay.signed` (the 636 MiB container
does not belong in `/tmp` across sessions).

The component coordinate is the loader block **minus `0x815`**: region 0
`0x825` → 16; region 1 `0x2915` → 8448; hd3 `0x4915` → 16640.

## 5. The flash procedure (not run)

From the booted Nura root, per `DEVELOPMENT.md` §4:

```bash
lsusb | grep -i 0fca                       # 0fca:8010 = OS; 0001 = bootrom; 8001 = ramloader
nohup python3 tools/bbusb.py flash --signed \
      local/work/nura/images/both-0xE85217E5-overlay.signed \
      --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
sleep 2
python3 tools/pbcon.py 'reboot -f'          # the Nura ttyGS0 BusyBox shell
until grep -qE 'COMPLETE|done|error' /tmp/f.log; do sleep 2; done; tail -5 /tmp/f.log
```

`--from-bootrom` uploads the ramloader: the unit must be **booted**, not in the
ramloader; the flasher catches the 60 s bootrom window opened by `reboot -f`.
The Nura shell is a plain `/bin/sh -l` on `ttyGS0`; `pbcon` is the console tool.
Nothing was sent — the shell was still running a command at wrap-up.

## 6. Next

1. Flash the container (above); check `COMPLETE 0x40C0 -> cmd 0x4006` / `done`.
2. Boot the overlay root; check `dmesg | grep -E 'mmc1|EXT4|overlay|Card stuck'`
   (no stall, no journal abort, `overlay` on `/newroot`), `dmesg | grep pbrpmb`
   (`BOOTCTR_FCTRY`), `rc-status`/the tinydm log, `/run/user/10000`,
   `/dev/dri/card0`, `lsmod`/`wlcore`, the touch, the backlight.
3. If the DE runs: SGX540 (ROADMAP item 7a step 5).  A writable eMMC root
   remains a separate experiment.

## 7. Artefacts

* `initramfs/init.c` — the read-only loop + `MS_RDONLY`/`noload` + overlayfs.
* `local/images/kernel-0xE85217E5-overlay.img` — CRC `0xE85217E5`, `0x9662A4` B,
  sha256 `fbeb6df0…`.
* `local/work/nura/images/blackberry-playbook-root-overlay.img` — hd3,
  625 999 872 B, sha256 `1f14915f…`.
* `local/work/nura/images/both-0xE85217E5-overlay.signed` — the packed
  container, 635 961 936 B, sha256 `c3ed1f61…`.
* This report; the session-54 note continuation; traps 266–271; DECISIONS §76.
