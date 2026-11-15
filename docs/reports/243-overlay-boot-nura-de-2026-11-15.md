# 243 — the overlay root flashed and booted: eMMC quiet, the Nura DE starts, the GPU is the remaining blocker

Date: 2026-11-15 (session 55, Linux-era 49).
Bench: the unit is **on the new image** — region 0 `0xE85217E5`, hd3
`1f14915f…`, both written in **one bootrom session** from the packed container
`c3ed1f61…`; the unit booted the **read-only hd3 + overlayfs root** unattended.
Region 1 (`49e318bf…`) is intact and untouched.

## 0. Headline

The session-54 build was flashed and it works.  The flasher caught the 60 s
bootrom window opened by `reboot -f`, uploaded the ramloader, and wrote the
whole container: `COMPLETE 0x40C0 -> cmd 0x4006`, `done`, `loader exit: device
released`.  The unit then booted `7.3.3 #67` with

```
overlay / overlay rw,relatime,lowerdir=/rootfs,upperdir=/overlay/upper,workdir=/overlay/work,uuid=on
```

and **no eMMC stall and no journal abort**: the lower mounted
`EXT4-fs (loop0): mounted filesystem aed244ce-… ro without journal`.  `elogind`
and `tinydm` started, a `playbook` seat was created (`/run/user/10000`, the
catacomb Wayland socket), and **`catacomb` is running**.  The boot-time top-up
saw `BOOTCTR_FCTRY = 0x1d` and wrote `0x32` (`PASS`).

The screen is black with a small white bar at the bottom: `catacomb` starts and
sets the output mode, but its GL renderer fails (`MESA: error: ZINK: failed to
load libvulkan.so.1`; `failed to create signaled syncobj … ENOTSUP`).  That is
the **SGX540** gap, not a root/seat/write problem — the DE's session stack is
working.

## 1. The flash (one bootrom session)

Started from the booted Nura root, exactly per `DEVELOPMENT.md` §4:

```bash
python3 tools/bbusb.py flash --signed \
    local/work/nura/images/both-0xE85217E5-overlay.signed \
    --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
# then, from the Nura ttyGS0 shell:
reboot -f
```

The flasher log:

```
…: 635961936 bytes (635961376 payload, signature 560 B)
  write block 0x00825..0x008BB (raw 0x08100000..0x08A6FFFF) offset 16
  write block 0x04915..0x06E64 (raw 0x49000000..0x6E4FFFFF) offset 16640
Connected in bootrom
Model ID: 0x06001A06
Sending ramloader to 0x80100000 (260-byte chunks)
Connected in ramloader
PreFlash 0x20EE (21) -> cmd 0x20DE len 4
…
635961376/635961376 payload bytes, seq 38863
0x40F9 signature -> cmd 0x4006 len 0
COMPLETE 0x40C0 -> cmd 0x4006 len 0
done
loader exit: device released (attempt 1)
```

The CMS console shell is a plain `/bin/sh -l` on `ttyGS0` with the prompt
`playbook:~#`, so `tools/pbcon.py` (which matches `pb> `) does not drive it;
`reboot -f` was sent with a one-line raw helper.  The device re-enumerated
`0fca:8010` and the ACM node came back 0666 by itself.

## 2. The boot

`uname -a`: `Linux playbook 7.3.3 #67 Sun Nov 15 17:24:02 -03 2026 armv7l`.

```
overlay / overlay rw,relatime,lowerdir=/rootfs,upperdir=/overlay/upper,workdir=/overlay/work,uuid=on
devtmpfs /dev … /run tmpfs rw … /run/user/10000 tmpfs rw,uid=10000,gid=10000
```

`dmesg` (unfiltered, then filtered for this report):

```
[    2.69] mmc1: new high speed MMC card at address 0001
[    5.18] EXT4-fs (loop0): mounted filesystem aed244ce-… ro without journal. Quota mode: none.
```

**No** `mmc1: Card stuck being busy!`, **no** `Aborting journal`, **no**
`Remounting filesystem read-only`.  The lower is read-only at the loop and the
fs (`noload`), so the journal/udev writes landed in the tmpfs upper.

`BOOTCTR_FCTRY` (the image's own boot-time top-up, `SAFETY.md` §2):

```
pbrpmb: topup: BOOTCTR_FCTRY at block 0x3a record@196 = 1d000000
pbrpmb: topup: writing 0x1d -> 0x32 (RPMB counter 11995)
pbrpmb: topup: PASS - BOOTCTR_FCTRY = 0x32
```

`rc-status` (default runlevel): `dbus`, `elogind`, `tinydm`, `udev-postmount`,
`networkmanager`, `wpa_supplicant`, `bluetooth`, `iio-sensor-proxy`,
`sleep-inhibitor`, `localmount`, `root`, `fsck` all **started**; `tinydm` and
`elogind` report started at 00:00:28/31 (the read-only-root failure of report
241 §4 is gone).

The seat:

```
-rw-r--r--  /run/user/10000/bus
srwxr-xr-x  /run/user/10000/catacomb-wayland-1.sock
srwxr-xr-x  /run/user/10000/wayland-1
drwxr-xr-x  /run/user/10000/openrc
```

`pgrep`: `catacomb` (pid 1588), `elogind` (1272), `squeekboard` (1684).

Touch, backlight and WLAN:

```
playbook-munin 1-0065: Munin bootloader id 0xb0 ver 0x05 app 0x020a build 0x3b
playbook-munin 1-0065: Munin touchscreen ready (irq 110)
input: PlayBook Munin Touch as …/input/input0        (/dev/input/event0)
/sys/class/backlight/backlight/brightness = 255
wlcore: firmware booted (Rev 7.3.5.0.98)   (twice; the second after a "down")
wlan0 UP (DORMANT, no carrier yet)
```

Modules: 33 loaded (of the 43 in the image), including
`wlcore`, `wlcore_sdio`, `wl12xx`, `mac80211`, `cfg80211`, `libarc4`.

## 3. The DE: it starts, the renderer is the wall

`/home/playbook/.local/state/tinydm.log`:

```
--- tinydm ---
Session: /usr/share/wayland-sessions/catacomb.desktop
Desktop: catacomb
Exec:    catacomb
---
tinydm: sourcing file: /usr/share/tinydm/env-wayland.d/50-sdl-wayland.sh
 INFO catacomb: Wayland socket: wayland-1
 WARN catacomb::output: Ignoring invalid calculated scale: NaN
 INFO catacomb::output: Using output scale: 2 (PPI NaN)
 INFO catacomb::orientation: Using iio-sensor-proxy accelerometer
 INFO catacomb: Loading global config at "/etc/catacomb/initrc"
 WARN smithay::backend::drm::device::fd: Unable to become drm master, assuming unprivileged mode
MESA: error: ZINK: failed to load libvulkan.so.1
 INFO catacomb::output: Using output scale: 1 (PPI 169.83)
 WARN smithay::backend::drm::compositor: failed to create signaled syncobj err=Os { code: 95, kind: Unsupported, message: "Not supported" }
 WARN smithay::backend::drm::surface::atomic: Failed to destroy old mode property blob: No such file or directory (os error 2)
/etc/catacomb/initrc: line 56: swayidle: not found
No such schema "org.gnome.desktop.a11y.applications"
/etc/catacomb/initrc: line 70: tabula: not found
/etc/catacomb/initrc: line 74: epitaph: not found
/etc/catacomb/initrc: line 75: gnome-calls: not found
/etc/catacomb/initrc: line 76: chatty: not found
** (squeekboard:1684): WARNING … Gsettings schema … not installed …
```

So: the compositor gets an output (`1024x600`, `connected`), loads its config,
starts the keyboard, and the missing pieces are the **GL renderer** (Mesa has
only `swrast`/`kms_swrast`/`zink`, and Zink cannot load `libvulkan.so.1`) and
the app set (`swayidle`/`tabula`/`epitaph`/`chatty` are not installed).  The
black screen with a small white bar is catacomb's empty panel, not a failed
root.

`/dev/dri` has `card0` and `renderD128`; `omapdrm` owns the display; **nothing
binds the GPU** (`ti,omap4430-gpu`), exactly as report 239 scoped.  SGX540 is
the next step (ROADMAP 7a step 5).

## 4. What this settles

* **The eMMC is out of the boot path.**  The read-only lower + tmpfs overlay
  removed the journal/udev writes that stalled the card, and the boot is
  repeatable with no `Card stuck`/`Aborting`.
* **The read-only root does not starve elogind/tinydm.**  The overlay keeps the
  lower `/var`/`/etc` state visible while writes go to the tmpfs, and the
  session/seat come up (`/run/user/10000`, a Wayland socket, `catacomb`).
* **The Nura root is now self-sufficient for the boot:** it carries the WLAN
  firmware (`wlcore: firmware booted`) and the matched 43 modules.
* **The remaining blocker for a usable DE is the GPU renderer**, not the root,
  the seat, the touch, the backlight or the eMMC.

## 5. Next

1. **SGX540** (ROADMAP 7a step 5): `linux-openpvrsgx` rebased to 7.3.3
   (`CONFIG_SGX`, `CONFIG_SGX_OMAP`, `CONFIG_SGX_DRM`) plus the proprietary
   `sgx-ddk-um` `ti443x` userspace; blobs stay gitignored in `local/`.
2. Optional, independent of SGX: a software-render fallback (`libvulkan`/
   lavapipe so Zink loads, or a llvmpipe/kms_swrast override) to get a working
   UI before the GPU driver; and the missing DE apps (`swayidle`, `tabula`,
   `epitaph`, `chatty`) if the desktop is wanted complete.
3. The WLAN is up but unconfigured on this root; NetworkManager is running, so
   the association test is a separate item.

## 6. Artefacts

* The flasher log: `/tmp/f.log` (host, volatile).
* `dmesg`/console prose captured above; no new capture file was added.
* No source change; the kernel patch, config and images are session-54's.
* Traps 272–274; DECISIONS §76 "Executed (session 55)"; report INDEX row.
