# Handoff — session 58 (absolute)

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a **Nura** root on hd3
with a GPU-accelerated compositor.

**Working:** everything from session 57 (apps and background, the audio log
cleanup, the no-suspend initrc) — plus this session: **the touch position ghost
is fixed** (kernel `#74`) and **the root is writable, grown to 56.5 G, with SSH
back** (kernel `#77`, region 0 `0x294F8E36`).  `PB_LINK=usb tools/pbssh.sh` is
the clean shell; the ttyGS0 getty is the fallback.

**THE NEXT ITEM: the upload-path profile (§0b).**  The eMMC "cache" item is
closed by measurement: the card has no cache (`CACHE_SIZE = 0`, `CACHE_CTRL =
0`, read two ways), so no `FLUSH_CACHE` is issued; the "CACHE_SIZE = 249" in
the last handoff was the EXT_CSD byte *index*.  The session-58 stall is a
plain WRITE (`flags 0x900000`, op `0x1`), not a flush.  Report 250 §1, trap 300
corrected.

**Then:** optionally bake the OSK stack (wvkbd + DejaVu + pb-touchwake) into
the flasher-restorable hd3 base so a reflash does not lose it.  The backspace/
enter repeat is fixed by installing wvkbd on catacomb 1.5.0 (no UI upgrade),
and touch now wakes a DPMS-blanked panel.  Report 250 §2/§2a, trap 299 fixed.

## 0a. THE CACHE EXPERIMENT — DONE, NO CHANGE NEEDED (do not repeat)

The session-59 measurement (report 250 §1) closed this: a small
`tools/bench/mmccache.c` (`MMC_IOC_CMD` CMD8 reader) and the kernel's own
`/sys/kernel/debug/mmc1/mmc1:0001/ext_csd` both read
`CACHE_SIZE = 0`, `CACHE_CTRL = 0`, `REV = 0x5`.  The card (Samsung MCGAFA,
manfid `0x15`, quirk `0x400 = SEC_ERASE_TRIM_BROKEN`) has no cache, so the MMC
core never issues `FLUSH_CACHE`.  The "249" and "33" in the last handoff were
the EXT_CSD byte **indices** (CACHE_SIZE = byte 249, CACHE_CTRL = byte 33),
not values; mmc-utils was never installed and the read was never done.

The stall error is a plain WRITE: `blk-mq.c:883` prints
`flags = req->cmd_flags & ~REQ_OP_MASK`, so `0x900000` is
`REQ_BACKGROUND|REQ_ALLOC_CACHE` (no `REQ_PREFLUSH`/`REQ_FUA`, op `0x1` not
`0x2`).  A traced `sync` (`omap_hsmmc` `trace=Y`) produced only CMD25/CMD13 and
no CMD6, and did not stall.  Keep `barrier=0` + write-through; the
busy-after-WRITE cause is open.  No kernel change, no re-export, no flash.

## 0b. The upload-path profile (next performance item)

The input latency is 8–33 ms, so the OSK jank and the glitchy top-left cursor
are redraw/upload.  A profile build of catacomb is **staged**:

* `local/work/nura/pmaports/main/catacomb/APKBUILD` is `pkgrel=10001` and applies
  `smithay-gles2-profile.patch` (env-gated by `PBGL_PROFILE`) after the GLES2
  fallback patch.  The patch prints once per second:
  `PBGL import/s= texsub/s= KiB/s= rects/s= padded= full= avg_us= max_us=
  shared= last=WxH stride= pix=`.
* Build with the session-56 command (18–20 min, hand to the operator):
  ```bash
  cd local/work/nura/pmbootstrap
  sudo env HOME=$HOME XDG_CONFIG_HOME=$PWD/../config PYTHONPATH=$PWD \
    python3 pmbootstrap.py --as-root --no-cross --no-ccache \
    build --lax --arch armv7 catacomb
  ```
* Bake the APK into a copy of hd3 with the offline `apk --root` method, flash
  hd3 only, and run catacomb with `PBGL_PROFILE=1` (tinydm env file).  Then type
  on the OSK and read the tinydm log.

Measured facts: `EGL_KHR_fence_sync` **is** present, `GL_OES_EGL_sync` is not,
and catacomb's `EGLContext::new()` is **not shared**, so the upload path does
**not** call `glFinish`; the cost is the `TexSubImage2D` calls themselves.

## 0c. The media, and the way back

* **region 0 = `0x294F8E36`** (`0x9E0980` B, `boot/chainloader/kernel.img`
  sha256 `f0738625…`, kernel **`7.3.3 #77`**) — the read-write root, the
  dropbear/SSH install, `barrier=0` + write-through, and the one-shot
  `resize2fs`.
* **hd3 (media)** = the Nura root **grown in place to 56.5 G**, read-write, with
  the SSH kit installed.  It is **no longer `f7858110…`**; the
  flasher-restorable image is `local/work/nura/images/
  blackberry-playbook-root-apps-audio.img` (`f7858110…`, 625 999 872 B) — the
  session-56 compositor image + the app set + the rebuilt audio modules +
  `gsettings-desktop-schemas` + the no-suspend initrc.
* **region 1 = `49e318bf…`** (the BusyBox fallback, not booted).
* **`BOOTCTR_FCTRY` = `0x2b`** at the last boot (top-up threshold `0x1e`, so
  no write needed; the session's reboots drained `0x2d → 0x2b`).  Read it with
  `dmesg | grep pbrpmb`.
* **The unit runs the Nura root read-write** (`0fca:8010`, `169.254.0.1:22`
  dropbear, `PB_LINK=usb tools/pbssh.sh`; the ACM `playbook:~#` getty is the
  fallback).  `usb0` is brought up by `/etc/local.d/pb-ssh.start`; **pace bulk
  host→device transfers** (8 KiB/5 ms; trap 302).  `/tmp` is a real directory on
  the grown FS now, but still treat a reflash as wiping the root.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`, then reboot; `0fca:8012`, `PB_LINK=door tools/pbssh.sh`.  **To stock
  TabletOS:** that IFS at `0x825` + `local/work/rcfs-1917.rcfs` at `0x8D5`
  (+ the vendor UFS if hd3 was repurposed).  **Do NOT restore
  `local/images/ifs0.sealed.bin`** (trap 84).

## 0d. What this session fixed (do not redo)

* **The root is writable and grown, and SSH is back.**  `initramfs/init.c`
  mounts hd3 read-write (no overlay), installs the static dropbear + host keys +
  authorized key + `/etc/local.d/{pb-ssh,pb-grow}.start` on first boot, sets
  `barrier=0` and `write_cache=write through` on the card and the loop, and
  chroots.  `pb-grow.start` runs `resize2fs` once (`/etc/pb-grown`) → 56.5 G.
  `PB_LINK=usb tools/pbssh.sh` works; the ttyGS0 getty stays.  Kernel `#77`,
  region 0 `0x294F8E36`.
* **Touch** (`playbook-munin.c` + the board DTS): the poll now survives a burst
  (keeps sampling while the controller asserts its line, plus a 200 ms grace)
  and a new down is confirmed on a fresh sample (the ghost).  The optional
  `irq-gpios = <&gpio2 3 GPIO_ACTIVE_LOW>` gives the driver the line level; the
  `trace` output now carries `down=<mask> line=<level>`.  Kernel `#74`.
* **Export**: 64 files / 8 981 lines, sha256 `ca17a057…`, 0 rejects / 64/64
  identical.  The walk excludes `drivers/gpu/drm/pvrsgx/` and `scripts/`.
  Re-export only when the kernel source changes; the session-58 initramfs/SSH
  work did not touch it.

## 0e. catacomb / OSK package work

```bash
cd local/work/nura/pmbootstrap
sudo env HOME=$HOME XDG_CONFIG_HOME=$PWD/../config PYTHONPATH=$PWD \
  python3 pmbootstrap.py --as-root --no-cross --no-ccache \
  build --lax --arch armv7 catacomb
```
`--no-cross` because crossdirect refuses native rustc 1.100.0 against the
armv7 chroot's 1.99.1; `--no-ccache` because `sccache` is absent.  Artifact:
`local/work/nura/work/packages/edge/armv7/catacomb-1.5.0-r10001.apk`.

**The OSK fix (done, session 59):** no catacomb 1.7.x move is needed.
catacomb 1.5.0 already implements `zwp_virtual_keyboard_v1`
(`VirtualKeyboardManagerState`, `src/catacomb.rs:303`), the protocol wvkbd
speaks.  `wvkbd-mobintl` 0.21 (Alpine edge/community armv7) was installed with
`apk add --no-network` (all runtime libs present); the UI APKBUILD now depends
on `wvkbd-mobintl` + `font-dejavu` (`pkgrel=3`) and the initrc spawns
`wvkbd-mobintl -L 260 --fn "DejaVu Sans 16"` instead of squeekboard, with the
OSK keys rebound to `pkill -USR2/-USR1 -x wvkbd-mobintl`.  DejaVu Sans was
installed by hand (the package's `encodings`/`mkfontscale` deps are absent) and
`fc-cache -f` run.  One BackSpace tap now yields one `0x7f`.  Report 250 §2/§2a,
DECISIONS §85.  Do not fix the repeat in the touch driver (trap 299).

**DPMS / touch-wake (measured, policy changed):** `tools/bench/pb-touchwake.py`
is installed at `/usr/bin/pb-touchwake`, started by the initrc, and the session
user is in the `input` group (gid 23).  It watches `/dev/input/event0` for
`BTN_TOUCH` down and calls `catacomb msg dpms on` when `bl_power != 0`.  A
single forced blank + tap woke cleanly.  But a later auto-blank left a black
panel with the backlight on: the DSI link wedged (`NO PACKET_SENT`, `VC error`);
catacomb/tabula/wvkbd were alive, so only a reboot clears it (traps 289/301).
With swayidle's `resume` removed (single DPMS-on caller), two cycles were clean
but the third wedged again.  **So swayidle is removed from the initrc — the
panel stays lit**; pb-touchwake remains for a manual `catacomb msg dpms off`.

**App drawer icons (fixed):** `/usr/share/icons/default/index.theme` was
missing, so tzompantli never reached `hicolor` and every app icon was
"NotFound".  Created it, installed `adwaita-icon-theme` + `glycin-svg`, and
converted netsurf's XPM to a hicolor PNG.  **`epitaph` (the panel) still
segfaults** after EGL setup on the PVR GLES2 driver (a patched Rust rebuild or
a working panel is open); `calls`/`chatty` are not installed (heavy recommends).
A flasher-restorable hd3 image with all these fixes was baked:
`local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img` (`07555bde…`,
e2fsck clean), **not flashed** — hd3-only at `0x4915` if wanted.

## 0f. Apps, the schema, and known breakage

* The app set was installed offline into a copy of the image
  (`apk --root … --arch armv7 --allow-untrusted --no-scripts`), then
  `e2fsck -fn`.  `gsettings-desktop-schemas` ships XML only; the
  `gschemas.compiled` was produced with the host's `glib-compile-schemas`
  2.90.1 (the root's glib is 2.90.0).
* **`epitaph` aborts** (rc 139) after a UPower `GetAll` on `/` fails — no panel
  (trap 292).
* **Kumo is not packaged.**  It links `libWPEWebKit-2.0` and needs the
  `chrisduerr/WebKit` fork; Alpine/Nura have `libwpe`/`wpebackend-fdo` but no
  `wpewebkit`.  `netsurf` is the interim browser; `badwolf` (WebKitGTK 4.1) is
  the packaged WebKit option.
* **Backspace/enter repeat is squeekboard's** (trap 299); see §0e.

## 0g. FM and GPS (parked)

**FM:** mainline carried `drivers/media/radio/wl128x` from 2.6.39 to 6.13 and
removed it; a forward port is the route.  **GPS:** no mainline driver; the
operator has a 2024 LKML patch to evaluate, and the reference's own path is the
fallback.

## 1. Useful local extracts

* `kernel/patches/0001-playbook-winchester-7.3.3.patch` (64 files / 8 981
  lines, `ca17a057…`) + `kernel/vendor/pvrsgx-…tar.xz`.
* `local/work/modules-7.3.3/` — the 43 modules including the rebuilt
  `snd-soc-omap-mcbsp.ko` / `snd-soc-playbook-wm8994.ko`.
* `local/work/nura/images/blackberry-playbook-root-apps-audio.img` (`f7858110…`)
  and `…-root-apps.img` (`711cd5ec…`).
* `local/work/nura/pmaports/main/catacomb/` — the APKBUILD (`pkgrel=10001`),
  the GLES2 fallback patch and the profile patch.
* `local/work/nura/pmaports/main/postmarketos-ui-catacomb/initrc` (no suspend)
  and its `pkgrel=1` APKBUILD.
* `docs/captures/touch-munin-2026-11-16-session58.txt` — this session's touch
  evidence; `…-2026-11-16.txt` is session 57's.
* Reports **249** (the writable/grown root and the eMMC flush), **248** (the
  touch ghost and the squeekboard repeat), 247, 246, 243–245.

## 2. Build and flash (7.3.3)

```bash
REPO=$PWD; . tools/env.sh
K=local/kernel-tree
# after applying the patch to a pristine tree, copy the vendored driver:
tar -xJf kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz -C "$K/drivers/gpu/drm"
arm-linux-gcc -static -O2 -Wall -o initramfs/root/console initramfs/console.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/init    initramfs/init.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/pbrpmb  initramfs/rpmb.c
# the initramfs also installs the SSH kit (session 58); these are gitignored:
cp local/work/dropbear/dropbear initramfs/root/dropbear
cp local/work/ssh/dropbear_*_host_key initramfs/root/
cp local/secrets/rsa.pub initramfs/root/authorized_keys
chmod 600 initramfs/root/dropbear_*_host_key initramfs/root/authorized_keys
rm -f $K/usr/initramfs_data.cpio*
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 modules
ZIMAGE=$K/arch/arm/boot/zImage DTB=$K/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb \
    python3 tools/build_kernel_image.py     # prints the CRC; image must be < 0xB00000
```

Modules **in-tree**, strip with `--strip-debug`.  A module/firmware change
keeps the region-0 CRC and needs an hd3/region-1-only flash (trap 190).  The hd3
edits are `debugfs`/`apk --root` on a copy, then `e2fsck -fn`.

**Pack and flash** (the coordinate is the loader block minus `0x815`:
`0x825`→16, `0x4915`→16640):

```bash
printf 'boot/chainloader/kernel.img=16,0\nlocal/work/nura/images/<hd3>.img=16640,0\n' > /tmp/both.lst
python3 tools/bbusb.py pack -l /tmp/both.lst -v 1,2 -s -f -c /tmp/both.signed
python3 tools/bbusb.py flash --signed /tmp/both.signed --from-bootrom --password 0000 --apply
# hd3 only:
python3 tools/bbusb.py flash --component local/work/nura/images/<hd3>.img \
    --dest-block 0x4915 --from-bootrom --password 0000 --apply
```

Start the flasher first, then `reboot -f` over the raw ACM (trap 266); one
bootrom session; success is `COMPLETE 0x40C0 -> cmd 0x4006` + `done`.  If the
unit is already in the bootrom (a WDT reset), the same command works without a
reboot.

## 3. Bench

* **SSH is the primary shell:** `PB_LINK=usb tools/pbssh.sh 'cmd'` (root,
  key-only, `169.254.0.1:22`; `usb0` is brought up by
  `/etc/local.d/pb-ssh.start`).  The ttyGS0 getty on the ACM is the fallback
  (`playbook:~#`; `tools/pbcon.py` does not drive it — trap 274).
* **Pace bulk host→device pushes:** 8 KiB chunks with a 5 ms pause (trap 302).
  The device side is `nc -l -p <port> > /tmp/f`; the host sends.  An unpaced
  burst is truncated.
* **No need to kill swayidle: it is removed from the initrc** (session 59) —
  the panel stays lit because repeated DPMS resume wedges the DSI link.
  pb-touchwake wakes a panel after a *manual* `catacomb msg dpms off`; do not
  rely on repeated auto-blank cycles (trap 298 updated).  Restore manually with
  `XDG_RUNTIME_DIR=/run/user/10000 WAYLAND_DISPLAY=wayland-1 catacomb msg dpms
  on`; a wedged black panel needs `reboot -f`.
* **Injecting a command into foot's shell:** `TIOCSTI` on `/dev/pts/0` works
  (`tools/bench/inj.py <cmd>`); writing `/proc/<foot>/fd/7` (the pty master) does
  **not** feed the terminal.  `tools/bench/keylog.py` on foot's stdin logs the
  bytes the app receives — the decisive tool for input bugs.
* **The touch trace:** `echo 1 > /sys/module/playbook_munin/parameters/trace`,
  then `dmesg | grep PBTS`; `PBTS` shows `st/x17/b3/x/y/reads/torn/down/line`
  and the 18 raw bytes on a slot-0 transition or a jump > 40.
* `evdev`: `tools/bench/evcap.py <secs> <file>`; characters with
  `tools/bench/keylog.py`.  `tools/bench/acm.py '<cmd>'` drives the ACM;
  `tools/bench/inj.py` injects into foot's pty (`TIOCSTI` on `/dev/pts/0`).
  Push the device-side ones to `/tmp` paced (trap 302).
* **The compositor:** `md5sum /usr/bin/catacomb` (r10000 `7418d9ea…`);
  `pgrep -a catacomb`; `tail -20 /home/playbook/.local/state/tinydm.log`;
  `catacomb msg dpms on|off`; a restart over a wedged DSI does not recover
  (trap 289) — reboot.
* **The root FS:** `/dev/loop0` on `/` is the hd3 ext4, read-write, 56.5 G;
  `barrier=0` and `write_cache=write through` are set by the initramfs.  An
  explicit `sync` still stalls the eMMC (trap 300) — avoid it until the card
  cache is disabled.
* WLAN/BT: as `STATE.md`; WM8994 `i2c2` bus 4 addr `0x1a` (decimal args).  QNX
  tools `-static`.

## 4. Traps added last session (full list 14–302 in `KNOWN-ISSUES.md` §2)

* **300.** The eMMC cache flush hangs (`Card stuck being busy`, sector 0); the
  root needs `barrier=0` + write-through, and an explicit `sync` still stalls
  until the card cache is disabled.
* **301.** One `DSI … Failed to set dsi_if_enable to 0` at boot recovered.
* **302.** An unpaced host→device RNDIS burst loses data; pace it.
* **298.** Updated: pb-touchwake wakes a *manual* DPMS-off, but repeated resume
  wedges the DSI link (black panel), so auto-blank/swayidle is removed.
* **299.** Fixed: the repeat was squeekboard's; wvkbd on catacomb 1.5.0 gives
  one `0x7f` per tap (no 1.7.x move needed).
* **296** is fixed (kernel `#74`): the position ghost and the lost burst taps.
* Still current: 292–295, 285–291, 266, 267, 268/274, 269, 270, 271, 275–284,
  **265**, 264, 261, 260, 259, 253–258, **250**, **248**, **247**, **244**,
  **246**, **243**, **241**, **239**, **238**, **237**, **236**, **227**,
  **224**, **221**, **214**, **212**, **211**, **190**.

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **the active plan (eMMC cache, then OSK/profile)** | `docs/project/ROADMAP.md` items 7a/9, reports **249** and **248** |
| **the writable root / grow / flush** | report **249**, `initramfs/init.c`, trap 300 |
| **the touch evidence** | `docs/captures/touch-munin-2026-11-16-session58.txt` |
| **the catacomb package + patches** | `local/work/nura/pmaports/main/catacomb/` |
| **the OSK package** | `local/work/nura/pmaports/main/postmarketos-ui-catacomb/` |
| **the SGX kernel driver (vendored)** | `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` + `README.md` |
| **the kernel tree (source)** | `local/kernel-tree/` |
| **the export** | `kernel/patches/0001-playbook-winchester-7.3.3.patch` |
| **the config** | `kernel/winchester-7.3.3.config` |
| **the initramfs root code** | `initramfs/init.c` (read-write mount, SSH install, grow script) |
| **the hd3 images** | `local/work/nura/images/` (the flasher-restorable base is `…-apps-audio.img`, `f7858110…`) |
| flasher (region 0/1, **hd3**) | `tools/bbusb.py` |
| the module staging | `local/work/modules-7.3.3/` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| OSS player / mixer / BT probe | `rootfs/pbplay.c`, `rootfs/pbmix.c`, `rootfs/hci.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| reports | `docs/reports/` (**249** current) |
| sessions | `docs/sessions/session-58.md` |

## 6. Working rules (including the operator's explicit preferences)

* **Long builds are handed to the operator.**  Give the exact command and stop
  the turn; the operator runs it in their terminal.  Do not poll a long build.
* **`pkexec` is for the agent; the operator uses `sudo`.**  When handing a
  command to the operator, write `sudo`, not `pkexec`.
* **Explicit preferences go in this file, not the taste system.**  The taste
  system is deliberately off.
* After every console command, dump the log tail and read it.  Read
  `dmesg`/`sloginfo` **unfiltered** after a reboot or structural change.
  **Do not run two console commands in parallel** — they interleave on the one
  tty.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  A test boot is not a budget concern.
* **Never present a guess as a fact.**  Prefer a fresh read.
* **Use the reference before the bench** (DECISIONS §61).  Subagents:
  static/file-only (trap 109) and time-boxed.
* **Look up every external fact.**  Nura was renamed on 2026-09-27; search
  "Nura", and expect `postmarketos`/`pmaports` in package names.  Catacomb's
  OSK move to wvkbd was confirmed this session, not recalled.
* **Revert experiments and prove the revert.**
* **The flash:** `--from-bootrom` needs the unit **booted** (or already in the
  bootrom); one bootrom session; the pack coordinate is the loader block minus
  `0x815`; the 60 s window; `reboot -f` from the Nura `ttyGS0`.
* **The read-only hd3:** never mount it read-write at boot; writes live in the
  tmpfs upper; the lower is `MS_RDONLY`+`noload`.
* **The kernel export:** re-export when the DTS or a driver changes; apply the
  patch + extract the vendor driver; verify by apply-and-compare; run the tree
  walk after `make clean` (traps 161/211), excluding `drivers/gpu/drm/pvrsgx/`
  and `scripts/`.
* **Only the operator decides when to stop / wrap up.**

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 60 (absolute; Linux-era 54)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md items 7a AND 9, KNOWN-ISSUES.md §2 (traps 14-302, especially
296-302) and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md (61-83),
docs/reports/INDEX.md, reports 249 and 248,
docs/captures/touch-munin-2026-11-16-session58.txt and sessions/session-{58,59}.md,
report 250.

WHERE IT STANDS: the touch position ghost is FIXED (kernel #74) and the
backspace/enter repeat is FIXED by installing wvkbd on catacomb 1.5.0 — one tap
is one key, no UI upgrade needed (report 250).  DejaVu Sans gives the OSK its
glyphs.  The panel stays lit (auto-blank removed: repeated DPMS resume wedges DSI).  The root is
WRITABLE and grown to 56.5 G, SSH is back (kernel #77, region 0 0x294F8E36;
PB_LINK=usb tools/pbssh.sh).  The eMMC "cache flush" item is closed by
measurement: the card has no cache (CACHE_SIZE=0/CACHE_CTRL=0), and the stall
is a plain WRITE, not a flush (trap 300 corrected).  The upload-path profile
build is staged (PBGL_PROFILE) but not built.

THE ACTIVE ITEM: fix the `epitaph` panel segfault (EGL/PVR on GLES2) and flash
the baked hd3 image `blackberry-playbook-root-osk-wvkbd.img` (`07555bde…`,
hd3-only at `0x4915`) if media should match the live root; then the upload-path
profile (PBGL_PROFILE).  Do not fix the repeat in playbook-munin.c (trap 299 —
fixed in the OSK).  No auto-blank: repeated DPMS resume wedges the DSI link
(trap 298 updated); a wedged black panel needs `reboot -f`.

THE FACTS: region 0 = 0x294F8E36 (0x9E0980 B, sha256 f0738625..., kernel #77);
hd3 media = the same root grown in place to 56.5 G, read-write (the
flasher-restorable base is f7858110..., 625999872 B); region 1 = 49e318bf...
(fallback, not booted); BOOTCTR_FCTRY = 0x2b at session-59 end (self-maintaining,
threshold 0x1e).  The kernel/module vermagic is 7.3.3 mod_unload modversions
ARMv6 p2v8.

PHYSICAL STATE: the unit runs the Nura root read-write (0fca:8010, SSH dropbear
on 169.254.0.1:22, PB_LINK=usb tools/pbssh.sh; the ACM playbook:~# getty is the
recovery console).  usb0 is brought up by /etc/local.d/pb-ssh.start.  PACE bulk
host->device pushes (8 KiB chunks / 5 ms; trap 302) — an unpaced burst truncates.
To boot the ORACLE: flash local/work/ifs-shim-sealed.ifs at 0x825.  To stock
TabletOS: that IFS at 0x825 + local/work/rcfs-1917.rcfs at 0x8D5 (+ the vendor
UFS if hd3 was repurposed).  Do NOT restore local/images/ifs0.sealed.bin
(trap 84).

BENCH: SSH is the primary shell; the ttyGS0 prompt is playbook:~#, NOT pb> —
pbcon.py does not drive it (trap 274).  REBOOT: reboot -f (over SSH or the raw
ACM).  THE FLASH: start the flasher first, then the reboot; one bootrom session;
the 60 s window; --from-bootrom UPLOADS THE RAMLOADER, so the unit must be BOOTED
(trap 266), unless it is already in the bootrom after a WDT reset.  ttyACM0 is
0666.  An agent NEVER writes RPMB; NVRAM read-only.  Read dmesg UNFILTERED.
One command at a time.

TOUCH/INPUT: trace via /sys/module/playbook_munin/parameters/trace; PBTS lines
in dmesg (down=/line= added); evcap.py/keylog.py/inj.py are /tmp tools.  Inject
into foot with TIOCSTI on /dev/pts/0 (writing /proc/<foot>/fd/7 does not work).
No auto-blank (swayidle removed): repeated DPMS resume wedges DSI; a wedged black panel needs `reboot -f` (trap 298).
A both-edge IRQ request kills touch (trap 297).

BT: BT_EN pad 0x4A10007C mux 3 + GPIO2_DATAOUT bit 23; UART2 pads
0x4118/0x411A; /dev/ttyS1.  After a cold boot EXPORT gpio535 first and pulse low
2 s (trap 252).  ALWAYS check `cat /proc/tty/ldiscs`; swap a module with
`rmmod btbcm` THEN `hci_uart`, verify by content (trap 251).  WL1283 checks:
`i2ctransfer -f -y 0 w1@0x48 0xbc r3` -> 07 01 55; `lsmod` must show ccm/ctr.
WM8994: i2ctransfer args DECIMAL, bus 4 addr 0x1a.  On the oracle use
pbi2c rd16/wr16; push memdump3/memw32/pbi2c to /tmp after every boot.

PARKED: BT controller open (trap 250), LE, FM (forward-port
drivers/media/radio/wl128x), GPS (2024 LKML patch, else RE the reference).

LISTENING TESTS: lower `pbmix set "DAC1 Volume" 40` ONCE, restore 96 ONCE; a
bare `pbplay` plays a 2 s tone (trap 212); state what is playing (trap 191).

BUILDS: modules IN-TREE, strip `--strip-debug`.  The initramfs carries the SSH
kit (copy dropbear + host keys + authorized_keys into initramfs/root before the
build; gitignored).  The hd3 image edits are debugfs/apk on a copy, then
e2fsck -fn.  The pack coordinate is the loader block minus 0x815.  The kernel
export: apply the patch + extract the vendored driver, apply-and-compare, tree
walk after `make clean`, excluding pvrsgx and scripts (traps 161/211).  For a
LONG build, give the operator the command (with sudo) and STOP THE TURN; do not
poll.

SAFETY: read SAFETY.md first.  The root is now mounted READ-WRITE by design
(operator, session 58): the hd3 image is the way back (reflash f7858110... at
0x4915), and the ttyGS0 getty is the recovery console; keep it until SSH is
proven on any new image.  One variable per boot.  Flash region 0 only when only
it changed; both in one bootrom session only when both changed; a
module/firmware-only change needs an hd3/region-1-only flash (trap 190).  The
hard hazards (0 / >0x32 / failed write / flat pack) remain.  Operator's call
when to stop / wrap up.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  Re-export the kernel patch
whenever the DTS or a driver changes (trap 161); verify apply-and-compare and
run the TREE WALK (trap 211).

WRAP-UP: only the operator decides when the session stops.  Record the bench
state, write the session note, update the canonical docs, verify with
tools/docs_audit.py and tools/tests/test_offline.py, then commit privately and
publish the snapshot (docs/project/REPO-MANAGEMENT.md).  No IFS restore is
required.
```
