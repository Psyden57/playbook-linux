# Handoff — session 57 (absolute)

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a **Nura** root on hd3
with a GPU-accelerated compositor.

**Working:** everything from session 56 (audio, touch calibration, display,
eMMC/overlay, WLAN on the region-1 root, BT core, SGX540/catacomb) — plus this
session: **apps and background** (`tabula` wallpaper, `foot`, `netsurf`,
`xournalpp`, the Catacomb companions), the **audio log cleanup**, the
**no-suspend initrc**, and the **touch release fix**.

**THE NEXT ITEM (operator): the touch position ghost, then the upload profile.**
The repeated-key bug is fixed: the controller asserts its line for the whole
contact and the lift is a rising edge, so a falling-edge-only IRQ never read it;
kernel `#73` keeps falling-edge, re-reads until the counter parity matches, and
re-arms the driver's poll work every 10 ms while a contact is down.  Contacts are
now 36–158 ms and separate, and a slow `hello` types correctly at 8–33 ms
end-to-end.  **What remains:** a *fast* tap can be reported at the **previous**
contact's coordinates (h,e,h,h instead of h,e,l,l,o), so typing is unreliable.
Report 247 §2, capture `touch-munin-2026-11-16.txt`, trap 296.

## 0a. THE ONE EXPERIMENT (do this first)

**Question:** does the ghost come from the controller's report or from our read
of it?

**Inputs:** kernel `#73` (region 0 `0xB9DD91E3`); the `trace` parameter; the
`evcap.py` decoder (session scratchpad, or re-create from the capture's tool
note) and, for the raw side, `muninraw` (same source).

1. On the Nura shell:
   `echo 1 > /sys/module/playbook_munin/parameters/trace`
2. Push `evcap.py` over usb0; run `python3 /tmp/evcap.py 120 /tmp/ev.txt &`.
3. Tap a **slow h,e,l,l,o** (pause ~1 s between keys) and then one **fast h,e,l,l,o**.
4. Read `dmesg | grep PBTS` and `/tmp/ev.txt`.

**What each result means:**

* **The `PBTS` down line's `raw=` bytes carry the *new* position, but the
  contact immediately "moves" to the old one, and a polled raw read (driver
  unbound) of the same contact is correct** → our IRQ-timed read is stale; add
  a double-read consistency check (read twice and compare all 18 bytes before
  accepting).
* **The `raw=` bytes themselves show the new contact at the previous
  coordinates for several samples** → the controller's slot update lags; the
  fix is to track contacts by `b3` (which alternates 01/02) or to delay the down
  report until the position is fresh.
* **The down is correct and later samples move to the old position** → the
  controller is reassigning slots; the fix is slot/ID mapping, not timing.

Then implement the matching fix in `kernel/source/playbook-munin.c` (+ the
`local/kernel-tree` copy), rebuild `zImage`, wrap, flash region 0 only (the
touch driver is built in), and re-run the same capture.  Do not enable both IRQ
edges (trap 297).

## 0b. The upload-path profile (second, after the touch)

The input latency is 8–33 ms, so the OSK jank and the glitchy top-left cursor
are redraw/upload, not input.  A profile build of catacomb is **staged**:

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
* Bake the resulting APK into a copy of hd3 with the offline `apk --root`
  method, flash hd3 only, and run catacomb with `PBGL_PROFILE=1` (tinydm env
  file).  Then type on the OSK and read the tinydm log.

Measured facts for the profile: `EGL_KHR_fence_sync` **is** present,
`GL_OES_EGL_sync` is not, and catacomb's `EGLContext::new()` is **not shared**,
so the upload path does **not** call `glFinish`; the cost is the `TexSubImage2D`
calls themselves.

## 0c. The media, and the way back

* **region 0 = `0xB9DD91E3`** (`0x967398` B, `boot/chainloader/kernel.img`
  sha256 `5ac47496…`, kernel **`7.3.3 #73`**) — the touch poll-on-touch fix.
* **hd3 = `f7858110…`** (625 999 872 B;
  `local/work/nura/images/blackberry-playbook-root-apps-audio.img`) — the
  session-56 compositor image + the app set + the rebuilt audio modules +
  `gsettings-desktop-schemas` + the no-suspend initrc, mounted **read-only** at
  boot.  Its base apps image is `blackberry-playbook-root-apps.img`
  (`711cd5ec…`).
* **region 1 = `49e318bf…`** (the BusyBox fallback, not booted).
* **`BOOTCTR_FCTRY = 0x25`** (`dmesg | grep pbrpmb`; started `0x2c`, the flash
  boots and two idle-suspend resets spent the difference).
* **The unit is on the Nura overlay root** (`0fca:8010`, console
  `playbook:~#` on the ACM `/dev/ttyACM0`, **no sshd**).  **`usb0` is down
  after boot** — `ip link set usb0 up; ip addr add 169.254.0.1/16 dev usb0`;
  transfer host→device (device `nc -l -p 8765 > /tmp/f`, host sends).
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`, then reboot; `0fca:8012`, `PB_LINK=door tools/pbssh.sh`.  **To stock
  TabletOS:** that IFS at `0x825` + `local/work/rcfs-1917.rcfs` at `0x8D5`
  (+ the vendor UFS if hd3 was repurposed).  `/tmp` is wiped by a reboot.
  **Do NOT restore `local/images/ifs0.sealed.bin`** (trap 84).

## 0d. What this session fixed (do not redo)

* **Drivers**: `omap_mcbsp_dump_reg()` and the machine driver's per-stream
  prints are `dev_dbg`; the PM-QoS remove is guarded with
  `cpu_latency_qos_request_active()`; the Munin driver has the parity re-read,
  the poll-on-touch watchdog, and an off-by-default `trace` parameter.
* **Image**: `tabula`/`epitaph`/`tzompantli`/`gorm`/`swayidle`/`foot`/`grim`/
  `slurp`/`tremor`/`netsurf`/`xournalpp`; `gsettings-desktop-schemas` +
  `gschemas.compiled`; the rebuilt audio modules; the initrc's
  `swayidle timeout 180 "catacomb msg dpms off"` + `resume "catacomb msg dpms
  on"` and **no suspend**.
* **Export**: 64 files / 8 862 lines, sha256 `be2ca8ba…`, 0 rejects / 64/64
  identical.  Re-run the copy step (`kernel/vendor/README.md`) on the next
  kernel change and the tree walk after `make clean` (traps 161/211).

## 0e. catacomb build (only if the profile or a compositor change needs it)

```bash
cd local/work/nura/pmbootstrap
sudo env HOME=$HOME XDG_CONFIG_HOME=$PWD/../config PYTHONPATH=$PWD \
  python3 pmbootstrap.py --as-root --no-cross --no-ccache \
  build --lax --arch armv7 catacomb
```
`--no-cross` because crossdirect refuses native rustc 1.100.0 against the
armv7 chroot's 1.99.1; `--no-ccache` because `sccache` is absent.  Artifact:
`local/work/nura/work/packages/edge/armv7/catacomb-1.5.0-r10001.apk`.

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

## 0g. FM and GPS (parked)

**FM:** mainline carried `drivers/media/radio/wl128x` from 2.6.39 to 6.13 and
removed it; a forward port is the route.  **GPS:** no mainline driver; the
operator has a 2024 LKML patch to evaluate, and the reference's own path is the
fallback.

## 1. Useful local extracts

* `kernel/patches/0001-playbook-winchester-7.3.3.patch` (64 files / 8 862
  lines, `be2ca8ba…`) + `kernel/vendor/pvrsgx-…tar.xz`.
* `local/work/modules-7.3.3/` — the 43 modules including the rebuilt
  `snd-soc-omap-mcbsp.ko` / `snd-soc-playbook-wm8994.ko`.
* `local/work/nura/images/blackberry-playbook-root-apps-audio.img` (`f7858110…`)
  and `…-root-apps.img` (`711cd5ec…`).
* `local/work/nura/pmaports/main/catacomb/` — the APKBUILD (`pkgrel=10001`),
  the GLES2 fallback patch and the profile patch.
* `local/work/nura/pmaports/main/postmarketos-ui-catacomb/initrc` (no suspend)
  and its `pkgrel=1` APKBUILD.
* `docs/captures/touch-munin-2026-11-16.txt` — the touch evidence.
* Reports **247** (this session), 246, 243–245.

## 2. Build and flash (7.3.3)

```bash
REPO=$PWD; . tools/env.sh
K=local/kernel-tree
# after applying the patch to a pristine tree, copy the vendored driver:
tar -xJf kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz -C "$K/drivers/gpu/drm"
arm-linux-gcc -static -O2 -Wall -o initramfs/root/console initramfs/console.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/init    initramfs/init.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/pbrpmb  initramfs/rpmb.c
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

* The Nura `ttyGS0` prompt is **`playbook:~#`**; `tools/pbcon.py` does not drive
  it (trap 274).  `pbraw.py` for raw reads; the session's `nr.py`-style helper
  (marker + paced write) is the convenient driver.
* **After a boot, kill `swayidle` if you will be idle**: the image's initrc now
  only does DPMS, but a *pre-session-57* root would still suspend and reset.
  The current image is safe (`timeout 180` DPMS off, `resume` on).
* **The touch trace:**
  `echo 1 > /sys/module/playbook_munin/parameters/trace`, then
  `dmesg | grep PBTS`; `PBTS` shows `st/x17/b3/x/y/reads/torn` and the 18 raw
  bytes on every contact transition and every jump > 40.
* `evdev`: `evcap.py <secs> <file>`; characters with `keylog.py` inside `foot`.
* **The compositor:** `md5sum /usr/bin/catacomb` (r10000 `7418d9ea…`);
  `pgrep -a catacomb`; `tail -20 /home/playbook/.local/state/tinydm.log`;
  `catacomb msg dpms on|off`; a restart over a wedged DSI does not recover
  (trap 289) — reboot.
* hd3 checks: `dd` the superblock at `0x49000000`; read-only is safe.
* WLAN/BT: as `STATE.md` §0b/§0c of the session-56 handoff; WM8994
  `i2ctransfer` args DECIMAL, bus 4 addr `0x1a`.  QNX tools `-static`; `/tmp`
  is wiped by a reboot.

## 4. Traps added last session (full list 14–297 in `KNOWN-ISSUES.md` §2)

* **292.** `epitaph` 1.4.0 aborts (rc 139) after a UPower `GetAll` failure.
* **293.** The audio bring-up diagnostics were still at info level; now
  `dev_dbg` (`omap_mcbsp_dump_reg`, the machine driver's per-stream lines).
* **294.** A failed `hw_params` removed an inactive PM-QoS request → WARN +
  taint; guarded.
* **295.** `loginctl suspend` blanks the panel, drops USB and WDT2-resets the
  unit; the initrc no longer suspends.
* **296.** The driver dropped the lifted-contact sample; the controller holds
  its line through the contact, so the release needs a poll (kernel `#73`).
* **297.** A both-edge GPIO IRQ request on this line yields **0** interrupts.
* Still current: 285–291, 266, 267, 268/274, 269, 270, 271, 275–284, **265**,
  264, 261, 260, 259, 253–258, **250**, **248**, **247**, **244**, **246**,
  **243**, **241**, **239**, **238**, **237**, **236**, **227**, **224**,
  **221**, **214**, **212**, **211**, **190**.

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **the active plan (touch ghost, then profile)** | `docs/project/ROADMAP.md` items 7a/9, report **247** |
| **the touch evidence** | `docs/captures/touch-munin-2026-11-16.txt` |
| **the catacomb package + patches** | `local/work/nura/pmaports/main/catacomb/` |
| **the SGX kernel driver (vendored)** | `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` + `README.md` |
| **the kernel tree (source)** | `local/kernel-tree/` |
| **the export** | `kernel/patches/0001-playbook-winchester-7.3.3.patch` |
| **the config** | `kernel/winchester-7.3.3.config` |
| **the overlay root code** | `initramfs/init.c` |
| **the hd3 images** | `local/work/nura/images/` |
| flasher (region 0/1, **hd3**) | `tools/bbusb.py` |
| the module staging | `local/work/modules-7.3.3/` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| OSS player / mixer / BT probe | `rootfs/pbplay.c`, `rootfs/pbmix.c`, `rootfs/hci.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| reports | `docs/reports/` (**247** current) |
| sessions | `docs/sessions/session-57.md` |

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
  tty (a session-57 mistake).
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  A test boot is not a budget concern.
* **Never present a guess as a fact.**  Prefer a fresh read.
* **Use the reference before the bench** (DECISIONS §61).  Subagents:
  static/file-only (trap 109) and time-boxed.
* **Look up every external fact.**  Nura was renamed on 2026-09-27; search
  "Nura", and expect `postmarketos`/`pmaports` in package names.
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
PlayBook (OMAP4430 "winchester") — session 58 (absolute; Linux-era 52)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md items 7a AND 9, KNOWN-ISSUES.md §2 (traps 14-297, especially
292-297) and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md (61-82),
docs/reports/INDEX.md, report 247, docs/captures/touch-munin-2026-11-16.txt and
sessions/session-57.md.

WHERE IT STANDS: apps and background are in (wallpaper, terminal, browser,
paint, the Catacomb companions), the audio diagnostics are gated, the idle
suspend is removed, and the touch RELEASE bug is fixed (contacts separate).
The touch POSITION GHOST remains: a fast tap can be reported at the previous
contact's coordinates.  The upload-path profile build of catacomb is staged
(PBGL_PROFILE) but not built.

THE ACTIVE ITEM (operator): fix the touch position ghost, then profile the
upload path.  THE ONE EXPERIMENT: enable
`echo 1 > /sys/module/playbook_munin/parameters/trace`, type a slow h,e,l,l,o
with evcap.py running, and correlate the PBTS raw bytes with evdev; the three
outcomes and their fixes are in HANDOFF §0a.  Do not request both IRQ edges
(trap 297).

THE FACTS: region 0 = 0xB9DD91E3 (0x967398 B, sha256 5ac47496..., kernel #73);
hd3 = f7858110... (apps + audio modules + gsettings + no-suspend initrc);
region 1 = 49e318bf... (fallback, not booted); BOOTCTR_FCTRY = 0x25.  The
kernel/module vermagic is 7.3.3 mod_unload modversions ARMv6 p2v8.

PHYSICAL STATE: the unit is on the Nura overlay root (0fca:8010, console
playbook:~# on the ACM /dev/ttyACM0, no sshd); usb0 is DOWN after boot (bring it
up by hand; the host UFW blocks inbound, so push host->device).  To boot the
ORACLE: flash local/work/ifs-shim-sealed.ifs at 0x825.  To stock TabletOS: that
IFS at 0x825 + local/work/rcfs-1917.rcfs at 0x8D5 (+ the vendor UFS if hd3 was
repurposed).  /tmp is wiped by a reboot.  Do NOT restore
local/images/ifs0.sealed.bin (trap 84).

BENCH: the Nura ttyGS0 prompt is playbook:~#, NOT pb> — pbcon.py does not drive
it (trap 274); use a raw send/read helper.  pbssh.sh is for the REGION-1 root.
REBOOT: reboot -f over the raw ACM.  THE FLASH: start the flasher first, then
the reboot; one bootrom session; the 60 s window; --from-bootrom UPLOADS THE
RAMLOADER, so the unit must be BOOTED (trap 266), unless it is already in the
bootrom after a WDT reset.  ttyACM0 is 0666.  An agent NEVER writes RPMB; NVRAM
read-only.  Read dmesg UNFILTERED.  One console command at a time.

TOUCH: trace via /sys/module/playbook_munin/parameters/trace; PBTS lines in
dmesg; evcap.py/keylog.py are /tmp tools.  A both-edge IRQ request kills touch
(trap 297).  Suspend resets the unit (trap 295); the current initrc does not
suspend.

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

BUILDS: modules IN-TREE, strip `--strip-debug`.  The hd3 image edits are
debugfs/apk on a copy, then e2fsck -fn.  The pack coordinate is the loader
block minus 0x815.  The kernel export: apply the patch + extract the vendored
driver, apply-and-compare, tree walk after `make clean`, excluding pvrsgx and
scripts (traps 161/211).  For a LONG build, give the operator the command (with
sudo) and STOP THE TURN; do not poll.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both in one bootrom session only when both changed; a
module/firmware-only change needs an hd3/region-1-only flash (trap 190).  The
hard hazards (0 / >0x32 / failed write / flat pack) remain.  The read-only hd3
is never mounted read-write.  Operator's call when to stop / wrap up.

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
