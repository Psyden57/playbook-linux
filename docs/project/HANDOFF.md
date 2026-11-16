# Handoff — session 60 (absolute) → session 61

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a **Nura** root on hd3
with a GPU-accelerated compositor.

**Working at the end of session 60.**  The `epitaph` panel starts, stays up and
renders its text and icons (r10005/r10006; report 251); its brightness and
Wi-Fi/cellular controls work (group `catacomb` + the udev/polkit rules).
`gorm` lists the Wi-Fi APs after `nmcli radio wifi on` (report 252).  Touch
reaches xdg clients (an injected tap launches apps; `tools/bench/uitap.py`).
Software GL clients use **llvmpipe** instead of softpipe (~4× faster; report
253).  The root is writable, grown to 56.5 G, with SSH.

**THE NEXT ITEM: the CPU.**  The unit runs **one core at 400 MHz**.  The kernel
is built **`# CONFIG_SMP is not set`**, the DTS also sets `maxcpus=1`, and there
is no cpufreq/OPP table (`dpll_mpu_ck = 400 MHz`).  The reference runs 1.0 GHz
dual-core and programs `DPLL_MPU`/`PRM_VC` (report 223).  This is the biggest
remaining performance lever and it affects everything.

## 0a. THE NEXT EXPERIMENT — enable cpu1, in two boots

**Inputs.**

* `kernel/winchester-7.3.3.config`: `# CONFIG_SMP is not set` → set
  `CONFIG_SMP=y` and `CONFIG_NR_CPUS=2`.  Mirror it in
  `local/kernel-tree/.config` (and keep the tracked config in step).
* `kernel/source/omap4-winchester.dts:88` (and the copy in
  `local/kernel-tree/`): `bootargs = "... maxcpus=1 ..."`.  **Boot 1 keeps it;
  boot 2 removes it.**
* `CONFIG_CPU_FREQ=y` and `CONFIG_PM_OPP=y` are already set; no OPP table yet,
  so the clock stays 400 MHz in both boots.

**Build (boot 1: SMP with `maxcpus=1`).**

```bash
REPO=$PWD; . tools/env.sh
K=local/kernel-tree
# the revert image (the current region-0 kernel/DTB):
cp boot/chainloader/kernel.img local/images/kernel-0x294F8E36.img
# set CONFIG_SMP=y / CONFIG_NR_CPUS=2 in $K/.config (scripts/config or edit),
# then:
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- olddefconfig
grep -E '^CONFIG_SMP|^CONFIG_NR_CPUS' $K/.config
arm-linux-gcc -static -O2 -Wall -o initramfs/root/console initramfs/console.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/init    initramfs/init.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/pbrpmb  initramfs/rpmb.c
rm -f $K/usr/initramfs_data.cpio*
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 modules
ZIMAGE=$K/arch/arm/boot/zImage \
DTB=$K/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb \
    python3 tools/build_kernel_image.py          # prints the CRC; < 0xB00000
```

**The vermagic gains `SMP`.**  `make modules` rebuilds the in-tree set **and the
vendored SGX driver** (`drivers/gpu/drm/pvrsgx`, `CONFIG_SGX_OMAP=m`); confirm
`modinfo pvrsrvkm_omap4_sgx540_120.ko | grep vermagic`.  Install the rebuilt
modules into the **live root before the flash** (`/lib/modules/7.3.3/`, paced
push 8 KiB/5 ms — trap 302) and run `depmod -a 7.3.3`.  The region-1 root's
modules are a later copy.

**Flash region 0 only** (the kernel/DTB changed; hd3 and region 1 did not):

```bash
python3 tools/bbusb.py flash --component boot/chainloader/kernel.img \
  --dest-block 0x825 --from-bootrom --password 0000 --apply
# start the flasher first, then `reboot -f` over SSH (or the raw ACM);
# one bootrom session, 60 s window; --from-bootrom needs the unit booted
# (trap 266) unless it is already in the bootrom.
```

**Verify boot 1.**  `nproc` and `/sys/devices/system/cpu/online` = 1 (expected,
`maxcpus=1`); dmesg shows the SMP kernel and **no CPU1 bring-up**; `pvrsrvkm`
loads; `catacomb`/the panel are up.  This proves the SMP kernel is stable with
one CPU.

**Boot 2.**  Remove `maxcpus=1` from both DTS copies, rebuild the DTB/region-0
(zImage and modules unchanged), flash the same way, boot.  Expected
`online=0-1`, `nproc=2`, `Brought up 2 CPUs`; llvmpipe uses two threads; the UI
is faster; the compositor works.

**Result meanings.**

* **Two CPUs, stable, faster** → proceed to the MPU OPP (§0b).
* **cpu1 never onlines, or a WDT reset/hang** → capture the full unfiltered
  dmesg over the ttyGS0 getty/SSH, revert by flashing
  `local/images/kernel-0x294F8E36.img` at `0x825`, and investigate the OMAP4
  SMP bring-up (SCU/local timer/PM).  The reference's startup may do a
  PMIC/PRM step for the second core (report 223 reads `DPLL_MPU` and
  `PRM_VC_*`).
* **Boot 1 (SMP with `maxcpus=1`) does not boot at all** → the kernel config/DT
  is the problem, not cpu1; revert and bisect.

**Also** re-export the kernel patch after the DTS/config change (trap 161),
verify apply-and-compare and run the tree walk (trap 211), and update
`kernel/winchester-7.3.3.config` in the tree.

## 0b. Then the MPU OPP (1.0 GHz)

The reference writes `CM_CLKMODE_DPLL_MPU (0x4A004160)` (idle-bypass → lock),
`CM_CLKSEL_DPLL_MPU (0x4A00416C)` and `PRM_VC_* (0x4A307BA0–0xBF8)`; it writes
no `PRM_VP_*` (report 223 Q6/§9).  Our boot leaves the loader's 400 MHz.

**Measure the reference first.**  Boot the oracle (flash
`local/work/ifs-shim-sealed.ifs` at `0x825`, reboot; `PB_LINK=door`), push
`memdump3`/`memw32`/`pbi2c` to `/tmp`, and read `0x4A004160`, `0x4A00416C` and
the `PRM_VC_*` block — or use the decoded startup-playbook values (report 223).
That gives the target OPP and voltage.

**Then, one variable.**  Add an OPP table to the DTS (`operating-points-v2`,
`opp-hz`, `opp-microvolt`), a `cpu-supply` for `vdd_mpu` (TWL6030 SMPS1) and the
ABB, and let the OMAP cpufreq/OPP code scale; or write the reference's
DPLL/VC sequence at boot.  Check `cpu_thermal` (65.0 °C idle at 400 MHz) and the
pack.  A 1.0 GHz step without the right VCORE/ABB is a hang or damage risk: one
variable, on a unit we can lose, with the revert image in hand.

## 0c. The GL clients

Only the compositor is on the SGX (`pvr_dri` through GBM/DRM).  Wayland GL
clients (`epitaph`, `tzompantli`, `gorm`, `tabula`) get Mesa's `swrast`
(softpipe, now llvmpipe); the amber Mesa Wayland platform cannot select
`pvr_dri`, and `sgx-ddk-um-ti443x` ships `libGLESv2_PVR_MESA` but **no
`libEGL`** (trap 314).  `wvkbd` (cairo), `foot` (pixman) and `netsurf` (CPU) are
responsive.

Paths to hardware GL for clients (not started): a pvr Wayland EGL platform
(`wl_drm`/linux-dmabuf), a client that renders via GBM+dmabuf, or an X/Wayland
bridge.  The reference's WPE/GTA run natively on QNX with the SGX, so its model
differs.  For interactive use prefer CPU-rendered apps (netsurf, not WPE/Kumo);
Kumo remains a WPEWebKit build (report 247 §4).

## 0d. The media, and the way back

* **region 0 = `0x294F8E36`** (`0x9E0980` B, kernel `7.3.3 #77`,
  `boot/chainloader/kernel.img` sha256 `f0738625…`).  No flash this session.
* **hd3 (media)** = the Nura root **grown read-write to 56.5 G**, with the SSH
  kit and this session's live fixes (§0e).  The flasher-restorable base is
  `local/work/nura/images/blackberry-playbook-root-apps-audio.img`
  (`f7858110…`, 625 999 872 B).  The baked `…-osk-wvkbd.img` (`07555bde…`)
  predates llvmpipe, the rules and epitaph r10006.
* **region 1 = `49e318bf…`** (fallback, not booted).
* **`BOOTCTR_FCTRY` = `0x27`** at the wrap-up (this boot; no reboot this
  session; `/init` tops up at <`0x1e`).  Read with `dmesg | grep pbrpmb`.
* **Bench:** SSH `PB_LINK=usb tools/pbssh.sh` (root, `169.254.0.1:22`); the ACM
  `playbook:~#` getty is the recovery console; pace bulk pushes.
* **ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at `0x825`, reboot,
  `PB_LINK=door`.  **Stock TabletOS:** that IFS at `0x825` +
  `local/work/rcfs-1917.rcfs` at `0x8D5` (+ the vendor UFS if hd3 was
  repurposed).  **Do NOT restore `local/images/ifs0.sealed.bin`** (trap 84).

## 0e. What this session changed (do not redo)

* **`epitaph` 1.4.0-r10006** (local pmaport `main/epitaph`, patch
  `fix-renderer-init.patch`, sha512 `b149772a…`): rasterizer after the EGL
  context, atlas capped at `GL_MAX_TEXTURE_SIZE`, single-output premultiplied
  text shader; draw-phase timing logs (drop for the final image).  APK sha256
  `857cb5b8…`.
* **Panel permissions:** group `catacomb` (gid 111) with `playbook`;
  `/etc/udev/rules.d/66-epitaph.rules`;
  `/etc/polkit-1/rules.d/66-epitaph.rules`;
  `/etc/polkit-1/rules.d/10-gorm.rules`; backlight `root:catacomb -rw-rw-r--`.
* **llvmpipe:** `/usr/lib/xorg/modules/dri/swrast_dri.so` from Alpine 3.16 Mesa
  21.3.9 gallium (backup `swrast_dri.so.amber`), `/usr/lib/libLLVM-13.so`,
  `libdrm_amdgpu.so.1`/`libdrm_nouveau.so.2` (edge libdrm 2.4.134).  The
  compositor still maps `pvr_dri`.  Payload `local/work/llvmpipe/`
  (`payload.tar.gz`, sha256 `820d7ece…`).
* **catacomb 1.5.0-r10001** (profile build, `PBGL_PROFILE` env-gated; the env
  file was removed).  APK sha256 `b0097340…`.
* **uinput.ko** built/loaded (sha256 `7c99e8bf…`; taints the kernel).
  `tools/bench/uitap.py` taps/drags (`X Y` or `X1 Y1 X2 Y2 [steps] [ms]`).
* **Wi-Fi:** `nmcli radio wifi on`; `wlan0` scans (not connected).
* No region-0/1 flash and no kernel/DTS change this session.

## 0f. Build speed

The armv7 packages build under qemu-user (`--no-cross`; crossdirect is blocked
by a host/chroot rustc mismatch) and abuild wipes `$srcdir`.  Both local
APKBUILDs (`main/epitaph`, `main/catacomb`) set a persistent
`CARGO_TARGET_DIR=/home/pmos/cache/cargo-target-$pkgname`.  The catacomb
profile patch needed an `upload_full` scope fix and a `usize`→`i32` cast; its
sha512 is `c924aa90…`.

```bash
cd local/work/nura/pmbootstrap
sudo env HOME=$HOME XDG_CONFIG_HOME=$PWD/../config PYTHONPATH=$PWD \
  python3 pmbootstrap.py --as-root -y -w $PWD/../work -p $PWD/../pmaports \
  --no-cross --no-ccache build --lax --arch armv7 <epitaph|catacomb>
```

## 1. Useful local extracts

* `local/work/llvmpipe/` — the Mesa/LLVM/libdrm payload and the pushed tar.
* `local/work/nura/pmaports/main/{epitaph,catacomb}/` — the APKBUILDs/patches.
* `kernel/patches/0001-playbook-winchester-7.3.3.patch` (64 files / 8 981
  lines, `ca17a057…`) + `kernel/vendor/pvrsgx-…tar.xz`.
* `local/work/modules-7.3.3/` — the 43 modules (rebuild with the SMP config).
* Reports **253** (latency/llvmpipe), 252 (touch/gorm/Wi-Fi), 251 (epitaph), 250,
  249, 248; `docs/captures/` for the boot/input logs.
* `tools/bench/uitap.py` (uinput tap/drag), `evcap.py`, `keylog.py`, `inj.py`,
  `segvtrap.c`, `mmccache.c`.

## 2. Build and flash (7.3.3)

The loop is §0a.  Modules **in-tree**, strip `--strip-debug`; the initramfs
binaries are static and rebuilt separately; the hd3 edits are `debugfs`/`apk`
on a copy, then `e2fsck -fn`.  The pack coordinate is the loader block minus
`0x815` (`0x825`→16, `0x4915`→16640).  A module-only change keeps the region-0
CRC and needs an hd3/region-1 flash (trap 190) — but the root is rw, so copy
modules over SSH.

## 3. Bench

* SSH is the primary shell; the ttyGS0 getty (`playbook:~#`) is the recovery
  console (`pbcon.py` does not drive it — trap 274).
* **No auto-blank** (swayidle removed): repeated DPMS resume wedges the DSI
  link; a wedged black panel needs `reboot -f` (trap 298).
* Launch apps from SSH as the session user with `cd /home/playbook`,
  `XDG_RUNTIME_DIR=/run/user/10000`, `WAYLAND_DISPLAY=wayland-1`,
  `DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/10000/bus`, `setsid`.  A stale
  window can ignore touch (trap 307): relaunch it in a clean workspace.
* Touch trace: `/sys/module/playbook_munin/parameters/trace`, `dmesg | grep
  PBTS`.  Inject with `uitap.py` (needs `uinput.ko`).
* The compositor: `pgrep -a catacomb`; `tinydm.log`; `catacomb msg dpms
  on|off`.  Root is `/dev/loop0` on the hd3 ext4, rw, 56.5 G.
* `iw` is not in the Nura root; use `/usr/sbin/nmcli`.

## 4. Traps added this session (full list 14–314 in `KNOWN-ISSUES.md` §2)

* **303** softpipe advertises `GL_EXT_blend_func_extended` but the draw
  segfaults; **304** GL calls before a context are silent no-ops; **305**
  epitaph's udev/polkit rules are not installed by the APKBUILD; **306** uinput
  build/load and the `UI_SET_*` value form; **307** stale/tiny xdg window state;
  **308** NetworkManager radio off + gorm's rule; **309** no llvmpipe (the
  Alpine 21.3.9 replacement); **310** llvmpipe JIT/swap; **311** the profile's
  `KiB/s` wrap; **312** the MPU at 400 MHz with no cpufreq/OPP; **313** only
  CPU0 (`CONFIG_SMP=n` + `maxcpus=1`); **314** Wayland GL clients cannot use the
  SGX.
* 292 is fixed (the epitaph panel); 298/299 fixed; 300 corrected.

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| the active plan | `docs/project/ROADMAP.md` 7a; report **253** |
| the CPU finding | report **253**; `KNOWN-ISSUES.md` 312/313 |
| the epitaph package | `local/work/nura/pmaports/main/epitaph/` |
| the catacomb package | `local/work/nura/pmaports/main/catacomb/` |
| llvmpipe payload | `local/work/llvmpipe/` |
| the SGX driver (vendored) | `kernel/vendor/pvrsgx-…tar.xz` |
| the kernel tree/config | `local/kernel-tree/`, `kernel/winchester-7.3.3.config` |
| the export | `kernel/patches/0001-playbook-winchester-7.3.3.patch` |
| the initramfs code | `initramfs/init.c` |
| the hd3 images | `local/work/nura/images/` (`f7858110…` way back) |
| flasher | `tools/bbusb.py` |
| reports | `docs/reports/` (**253** current) |
| sessions | `docs/sessions/session-60.md` |

## 6. Working rules (including the operator's explicit preferences)

* **Hand long builds to the operator** (sudo, then stop the turn); do not poll.
  `pkexec` is for the agent; the operator uses `sudo`.
* **Do not take screenshots to inspect the screen** — ask the operator to look.
* One variable per boot; a test boot is not a budget concern.
* Read dmesg **unfiltered** after a boot or structural change; do not run two
  console commands in parallel.
* Never present a guess as a fact; prefer a fresh read.  Look up external facts
  (Nura/`pmaports`, kernel contents) from the primary source.
* Revert experiments and prove the revert.
* The flash: `--from-bootrom` needs the unit booted (or already in the bootrom);
  one bootrom session; the pack coordinate is the loader block minus `0x815`.
* The kernel export: re-export when the DTS/config/driver changes; apply and
  compare; tree walk after `make clean` (traps 161/211), excluding
  `drivers/gpu/drm/pvrsgx/` and `scripts/`.
* Only the operator decides when to stop / wrap up.

## 7. Paste-ready kickoff prompt (session 61)

```
PlayBook (OMAP4430 "winchester") — session 61 (absolute; Linux-era 55)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md items 7a and 9, KNOWN-ISSUES.md §2 (traps 14-314, especially
303-314) and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md (84-87),
docs/reports/INDEX.md, reports 253, 252, 251, 250 and 249,
docs/captures/touch-munin-2026-11-16-session58.txt and
sessions/session-60.md.

WHERE IT STANDS: the epitaph panel is FIXED (r10006; report 251).  Its
brightness and Wi-Fi/cellular controls work (group catacomb + the udev/polkit
rules); gorm lists the Wi-Fi APs after `nmcli radio wifi on` (report 252);
touch reaches xdg clients (tools/bench/uitap.py over uinput.ko).  Software GL
clients use llvmpipe (report 253): the drawer render is 2.7-6 ms, but the
remaining swap is 420-550 ms per full-screen frame and the first draw pays the
LLVM JIT.  The root is WRITABLE, grown to 56.5 G, SSH is back (kernel #77,
region 0 0x294F8E36; PB_LINK=usb tools/pbssh.sh).  The GPU compositor is on the
SGX (pvr_dri); GL clients are software (trap 314).

THE ACTIVE ITEM: the CPU.  The unit runs ONE core at 400 MHz: the kernel is
built CONFIG_SMP=n, the DTS sets maxcpus=1 (line 88), dpll_mpu_ck = 400 MHz,
and there is no cpufreq/OPP (CONFIG_CPU_FREQ/PM_OPP are on).  The reference
programs DPLL_MPU/PRM_VC (report 223) and runs 1.0 GHz dual-core.  THE NEXT
EXPERIMENT (HANDOFF §0a, two boots): build CONFIG_SMP=y + CONFIG_NR_CPUS=2 with
maxcpus=1 still set, rebuild ALL modules (the vermagic gains SMP, including the
vendored pvrsrvkm), install them in /lib/modules/7.3.3, save the revert image,
flash region 0 only, boot and verify; then remove maxcpus=1, rebuild/flash and
boot cpu1.  Then the MPU OPP/voltage (measure the reference on the oracle
first) and the GL-client path.  Do NOT take screenshots; ask the operator to
look at the screen.

THE FACTS: region 0 = 0x294F8E36 (0x9E0980 B, sha256 f0738625..., kernel #77);
hd3 media = the same root grown in place to 56.5 G, read-write (the
flasher-restorable base is f7858110..., 625999872 B); region 1 = 49e318bf...
(fallback, not booted); BOOTCTR_FCTRY = 0x27 (self-maintaining, threshold 0x1e).
The kernel/module vermagic is currently 7.3.3 mod_unload modversions ARMv6
p2v8; CONFIG_SMP changes it (verify with modinfo) — every module, including
pvrsrvkm, must match.

PHYSICAL STATE: the unit runs the Nura root read-write (0fca:8010, SSH dropbear
on 169.254.0.1:22, PB_LINK=usb tools/pbssh.sh; the ACM playbook:~# getty is the
recovery console).  The root carries this session's live fixes: epitaph r10006,
catacomb r10001, group catacomb (gid 111) + playbook, the 66-epitaph
udev/polkit rules, 10-gorm.rules, and the llvmpipe install (swrast_dri.so,
libLLVM-13.so, libdrm_amdgpu/nouveau; the softpipe backup is
swrast_dri.so.amber).  usb0 is brought up by /etc/local.d/pb-ssh.start.  PACE
bulk host->device pushes (8 KiB chunks / 5 ms; trap 302) — an unpaced burst
truncates.  To boot the ORACLE: flash local/work/ifs-shim-sealed.ifs at 0x825.
To stock TabletOS: that IFS at 0x825 + local/work/rcfs-1917.rcfs at 0x8D5 (+
the vendor UFS if hd3 was repurposed).  Do NOT restore
local/images/ifs0.sealed.bin (trap 84).

BENCH: SSH is the primary shell; the ttyGS0 prompt is playbook:~#, NOT pb> —
pbcon.py does not drive it (trap 274).  REBOOT: reboot -f (over SSH or the raw
ACM).  THE FLASH: start the flasher first, then the reboot; one bootrom
session; the 60 s window; --from-bootrom UPLOADS THE RAMLOADER, so the unit
must be BOOTED (trap 266), unless it is already in the bootrom after a WDT
reset.  ttyACM0 is 0666.  An agent NEVER writes RPMB; NVRAM read-only.  Read
dmesg UNFILTERED.  One command at a time.  The root is /dev/loop0 on the hd3
ext4, rw, barrier=0 + write-through; an explicit sync can still stall (trap
300).

TOUCH/INPUT: trace via /sys/module/playbook_munin/parameters/trace; PBTS lines
in dmesg (down=/line= added).  Inject a tap/drag with tools/bench/uitap.py
(X Y, or X1 Y1 X2 Y2 [steps] [ms]) — it needs uinput.ko loaded (built from the
tree; trap 306).  evcap.py/keylog.py/inj.py are /tmp tools; inject into foot
with TIOCSTI on /dev/pts/0 (writing /proc/<foot>/fd/7 does not work).  No
auto-blank (swayidle removed): repeated DPMS resume wedges DSI; a wedged black
panel needs reboot -f (trap 298).  A both-edge IRQ request kills touch (trap
297).  A stale/tiny xdg window can ignore touch (trap 307): relaunch it in a
clean workspace.

BT: BT_EN pad 0x4A10007C mux 3 + GPIO2_DATAOUT bit 23; UART2 pads
0x4118/0x411A; /dev/ttyS1.  After a cold boot EXPORT gpio535 first and pulse
low 2 s (trap 252).  ALWAYS check `cat /proc/tty/ldiscs`; swap a module with
`rmmod btbcm` THEN `hci_uart`, verify by content (trap 251).  WL1283 checks:
`i2ctransfer -f -y 0 w1@0x48 0xbc r3` -> 07 01 55; `lsmod` must show ccm/ctr.
WM8994: i2ctransfer args DECIMAL, bus 4 addr 0x1a.  On the oracle use
pbi2c rd16/wr16; push memdump3/memw32/pbi2c to /tmp after every boot.

PARKED: BT controller open (trap 250), LE, FM (forward-port
drivers/media/radio/wl128x), GPS (2024 LKML patch, else RE the reference),
Kumo/WPEWebKit (needs a build; netsurf is the CPU interim).

LISTENING TESTS: lower `pbmix set "DAC1 Volume" 40` ONCE, restore 96 ONCE; a
bare `pbplay` plays a 2 s tone (trap 212); state what is playing (trap 191).

BUILDS: modules IN-TREE, strip --strip-debug.  The initramfs carries the SSH
kit (copy dropbear + host keys + authorized_keys into initramfs/root before the
build; gitignored).  The hd3 image edits are debugfs/apk on a copy, then
e2fsck -fn.  The pack coordinate is the loader block minus 0x815.  The kernel
export: apply the patch + extract the vendored driver, apply-and-compare, tree
walk after `make clean`, excluding pvrsgx and scripts (traps 161/211).  A
CONFIG_SMP build changes the module vermagic: rebuild and install every module
before the region-0 flash.  For a LONG build, give the operator the command
(with sudo) and STOP THE TURN; do not poll.

SAFETY: read SAFETY.md first.  The root is mounted READ-WRITE by design
(operator, session 58): the hd3 image is the way back (reflash f7858110... at
0x4915), and the ttyGS0 getty is the recovery console; keep it until SSH is
proven on any new image.  One variable per boot.  Flash region 0 only when only
it changed; both in one bootrom session only when both changed; a
module/firmware-only change needs an hd3/region-1-only flash (trap 190).  The
hard hazards (0 / >0x32 / failed write / flat pack) remain.  Operator's call
when to stop / wrap up.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  Re-export the kernel patch
whenever the DTS/config or a driver changes (trap 161); verify apply-and-compare
and run the TREE WALK (trap 211).

WRAP-UP: only the operator decides when the session stops.  Record the bench
state, write the session note, update the canonical docs, verify with
tools/docs_audit.py and tools/tests/test_offline.py, then commit privately and
publish the snapshot (docs/project/REPO-MANAGEMENT.md).  No IFS restore is
required.
```
