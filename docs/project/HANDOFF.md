# Handoff — session 70 (absolute) → session 71

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 71 = Linux-era 65**, written at the
session-70 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 70 RESULT: the GPU buffer waste is FIXED and DEPLOYED; trap 349 is
> closed.**  Two findings.  (1) The "second buffer fails" symptom was **omapdrm
> scan-out capacity** — the SGX540 renders (`renderD129`), omapdrm scans out
> (`renderD128`) through a fixed **128 MiB TILER aperture**, and
> `tiler_reserve_1d()` returns `-ENOMEM` when the next buffer does not fit.
> (2) That capacity was being wasted **8×** by a pitch-alignment bug in the
> vendor userspace.  [reports 270, 271; DECISIONS §104]
>
> **The pitch bug.**  `libdbm` describes a linear buffer as **`width = 1,
> height = <total bytes>, bpp = 8`**, and `omap_gem_dumb_create()` calls
> `drm_mode_size_dumb(dev, args, SZ_8, 0)` — aligning the **scanline pitch to 8
> bytes**.  A 1-byte row therefore becomes an 8-byte pitch, so a 1024x600 AR24
> buffer allocates **19,660,800 B instead of 2,457,600**.
>
> **The fix is deployed on the live root and validated on the panel:**
> `/usr/local/lib/dbmfix.so` (an `LD_PRELOAD` wrapper on `ioctl` that
> re-expresses the bytes as `width=<bytes>, height=1`), enabled by
> `/usr/local/bin/catacomb-dbmfix`, with `catacomb.desktop` `Exec=` repointed
> (backup `catacomb.desktop.orig`).  **No closed binary is modified** —
> `libdbm` reaches the kernel through `drmIoctl`'s PLT.
> Measured: idle desktop **59.4 → 9.5 MiB** pinned, **every** GEM object
> **1.00×**, **8/8** re-rendering pvr clients clean with occupancy flat at
> 20.7 MiB, **panel operator-confirmed correct twice**.
>
> **⚠ The deployment lives only on the live root.**  A reflash of the
> session-59 hd3 image loses the session-69 GPU result *and* this fix.  **The
> hd3 rebake is the owed housekeeping** — `tools/rebake-hd3.sh`, now taking
> `DBMFIX_SO=`.
>
> **Retracted this session** (all kept in the reports): `-12 (3)` is
> `TILFMT_PAGE`, not 32-bit tiling; the 8× is not slot rounding; "prefer LINEAR
> buffers" fixes nothing (they are already linear); "the DDK asks for height×8
> rows" was the right answer to the wrong question (the height was **bytes**);
> and "catacomb sends zero `wl_buffer.release`" came from runs that died before
> their first swap (trap 351).

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**, with
a GPU-accelerated desktop (compositor **and** clients).

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console **and its RNDIS link with ssh**, the whole display chain plus the
DSI-init TCON text on the panel, the **Nura root on hd3** (read-write, grown to
62.5 G), **dual-core at 1008 MHz on the ARMv7 kernel**, **SGX540** (`pvrsrvkm`
bound; the desktop — compositor **and** clients — rendering on it), the
peripherals (I²C4 motion sensors, TMP103, BQ27541, FAN5702, CM3217, ISL9519
charger), the **Munin touch**, the **WM8994 audio** (headphones + both speakers,
48 kHz), and the **WL1283 WLAN** (associates, DHCP, traffic).  The WL1283 **BT
core** works (real inquiry; `hci0` registers with mgmt) with the controller-open
parked (§0c).

**Where it stands, in order:** the **GPU is done** — the client path is
validated (report 269 §4g–§4i), trap 349 is closed (report 270), and the 8×
buffer waste behind it is **fixed and deployed** (report 271).  **Remaining, and
the first item is durability not function: the hd3 rebake** (§0f.1), then trap
**350** (cosmetic glitches), a heavy-redraw measurement, packaging the pitch fix,
the CPU voltage decision (§0e), the kernel-side affinity fix (§0e), the USB
teardown (§0d), and the throughput re-measure.

## 0a. The media, and the way back

* **region 0 = `0x3547D613`** — `boot/chainloader/kernel.img` (10,448,868 B,
  sha256 `6ecd068d…`): kernel `7.3.3 #109 SMP`, `-march=armv7-a`, the fast
  chainloader, the loud DTB **without `maxcpus`**, the DSI-init TCON hook, the
  **stock MUSB mailbox + comparator lifetime fix**, an inert
  `console=ttyGS0,115200`, and **`panic=0`**.  **Verified on media this session**
  (trailer at raw `0x8100000 + 10 448 868 − 8` = `1f 2d c8 d7 13 d6 47 35`).
  The previous `0xC9FEE2D1` (`#105`, device-mode-only workaround, `panic=10`) is
  the known-good fallback; the session-64 recovery image is
  `local/work/kernel-usbdev-cpu1.img` (`0xC675E206`).
* **region 1 = `20a5edd3…`** (re-read this session), 64 MiB ext4 at
  `0x29000000` — the BusyBox fallback root, **not booted**; its 43 modules
  mismatch the current kernel (trap 320).
* **hd3 = the Nura ext4**, from `local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img`
  (sha256 `07555bde…`, 625,999,872 B), written at block `0x4915`, grown to
  **62.5 G** (15,273,984 × 4 KiB — verified this session:
  `s_blocks_count_lo` 15 273 984, `s_log_block_size` 2), with the ARMv7 module
  set installed and `depmod` run.  The same image is the flasher-restorable base.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`, then reboot.  It enumerates `0fca:8012`; qconn
  `169.254.0.1:9090`, root ssh — use **`PB_LINK=door tools/pbssh.sh`**.  The
  region-0 RCFS slot holds the **full 1917 RCFS** (trap 227); for the *minimal*
  oracle flash `local/work/minimal/setB.signed`.  `/tmp` is wiped by a reboot.
* **To stock TabletOS:** the 1917 IFS at `0x825` **and** `local/work/rcfs-1917.rcfs`
  at `0x8D5`, plus the vendor UFS (`local/work/ufs-1917-devuser.ufs`,
  sha256 `482e0473…`, 707 MB) at `0x4915` if hd3 has been repurposed.  **Do not
  restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).
* **hd3 no longer holds the QNX userland.**  The operator has the UFS image, so
  **no backup is required**; a reflash restores TabletOS.  **No restore is
  required at wrap-up.**

## 0b. The WLAN — it associates (session 52, unchanged)

The blocker was the missing `ccm`/`ctr` crypto modules
(`ieee80211_key_alloc()` needs a `ccm(aes)` AEAD; without it the PTK install is
`-ENOENT`, which wpa_supplicant reports as `WRONG_KEY`).  Working sequence after
a boot (`/tmp` is wiped):

```sh
ip link set wlan0 up                 # rcS loads the stack, it does not raise it
iw reg set <CC>                      # e.g. AR — required for a 5 GHz AP
mkdir -p /var/run/wpa_supplicant
{ echo "disable_scan_offload=1"; wpa_passphrase '<SSID>' '<PSK>'; } > /tmp/wpa.conf
iw dev wlan0 scan >/dev/null 2>&1
wpa_supplicant -B -i wlan0 -D nl80211 -c /tmp/wpa.conf -C /var/run/wpa_supplicant
wpa_cli -p /var/run/wpa_supplicant -i wlan0 status      # want wpa_state=COMPLETED
```

Wi-Fi is 1×1 HT20 (72 Mbit/s) — **do not advertise HT40** (trap 247).  Do **not**
use `IRQ_TYPE_LEVEL_LOW` for WL_IRQ (trap 238), **not** `wlcore_sdio dump=1`
(trap 239), **not** 5-sr (trap 243).

## 0c. BT — what works, and where it stopped (parked)

**Proven (report 238):** over a plain H4 tty (`/dev/ttyS1`, 115200 8N1) the core
answers `HCI_Reset`, reports `hci ver 6` / `lmp ver 6` / `mfr 0x002b` (TI) /
BD_ADDR `8e:e4:3c:22:61:00`, and **runs a full BR/EDR inquiry that finds a peer**
(`f0:a3:b2:bb:d1:00`, class `0x011c00`), reproducibly.  Replaying
`local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` answers **315/315** and
moves LMP subversion `0x2b0f` → `0x2b33`.  Loaded **before** the attach it makes
`hci0` **register with mgmt** (0 → 1 controller; negative control both ways) by
fixing the `0x201c` reply length.

**Parked blocker (trap 250):** a stray `0x30` byte.  After the last init reply
completes, ~200 ms later a single `0x30` arrives (6 of 6 runs, instrumented
`h4_recv()`); it is not an H4 type, so the kernel correctly returns `-EILSEQ` and
`SET_POWERED` gives `0x05`.  **Which side emits it is unknown.**  The experiment
to resume is in STATE.md §"What is next" 1a(b) — and note trap 251 (verify a
module swap by content) before running it.

**LE** is refused by the controller, but the chip reports BT 4.0 with the
LE-supported bit, so no enable sequence is known and impossibility is **not**
demonstrated.  **FM:** mainline carried `drivers/media/radio/wl128x` from 2.6.39
to 6.13 and removed it; a forward port is the route.  **GPS:** no mainline
driver; the operator has a 2024 LKML patch to evaluate, the reference's own path
is the fallback.

## 0d. The MUSB mailbox fault — CLOSED (reports 264/265); the VUSB teardown open

**The fault was a freed TWL6030 `phy_companion`, not corruption of
`musb->xceiv`.**  `twl6030_usb_probe()` stores `&twl->comparator` in the
`omap-usb2` PHY *before* `twl6030_usb_ldo_init()`; the ldo init's
`regulator_get(dev, "usb")` (the board's `vusb`) can return `-EPROBE_DEFER`,
but the code returned `-ENODEV` for **every** error.  The probe therefore failed
permanently, devres freed the `devm_kzalloc`'d `twl`, and the PHY kept a
dangling `phy->comparator`; a later reuse put a heap pointer in its first word
(`set_vbus`), and the stock `omap_musb_set_mailbox()` jumped through it.
[report 264 for the Oops, **report 265 for the cause and fix**]

The fix, in the patch: propagate `PTR_ERR()` from the regulator lookup; clear the
comparator with a `devm_add_action_or_reset()` action on any probe
failure/remove; `usb_put_phy()` the reference `omap_usb2_set_comparator()`
leaked.  The winchester early return is removed (the `set_mailbox_lock` mutex
stays), and the stock call runs.  **Validated: image `0x3547D613` (`#109 SMP`),
3/3 clean dual-core boots, gadget bound at ~1.41 s, no Oops.**  OTG host mode is
reachable again (untested).

### Still open: the VUSB teardown

The only writer of `VUSB: disabling` is `twl6030_usb_irq()` in
`drivers/usb/phy/phy-twl6030-usb.c`, on a `CONTROLLER_STAT1.VBUS_DET` read of 0.
Measured: BusyBox root, a command every 15 s → link dead at ~30 s; Nura,
SSH traffic → ~90 s; some boots survive >6 min.  So it is intermittent, not an
idle timer (trap 337; `docs/contradictions/usb-vusb-drop-idle-vs-intermittent.md`).
Run `local/work/kmsgcap.py` (or a userspace `cat /dev/kmsg`) before the link dies
and let it run through a drop; a `VBUS_DET`/`linkstat` log in that handler would
settle it directly.  The ACM (`playbook:~#`, `tools/pbraw.py`) remains the
fallback console.

**When SSH times out, work down this list — the device is the LAST suspect**
(trap 355): **1. the cable is plugged in** (session 70 lost time to exactly this;
a long `uptime` on reconnect does **not** mean the link survived a drop), 2. host
`ip rule`/route (VPN, trap 341), 3. `ufw` (trap 342), 4. only then the VUSB
teardown.  Note this session **did not observe a teardown at all** — it ran a 4 h
session and then an 8 h one without one — so trap 337 is currently
**unmeasured**, not refuted.

## 0e. Owed: the CPU — the voltage decision and the kernel affinity fix

**The defect (report 266, trap 340).**  With `online = 0-1` and
`SMP: Brought up 1 node, 2 CPUs`, **PID 1 was affinity-masked to `{0}`**, so
every userspace process inherited `{0}` and `nproc` read 1.  `taskset -c 1
<work>` always worked, so CPU 1 was healthy and simply never scheduled
userspace.

**Workaround, in place and verified:** on **hd3's root**, `/etc/inittab` has,
before the OpenRC entries,

```
::sysinit:/usr/sbin/taskset -p 3 1
```

→ PID 1 `{0,1}`, `nproc` 2.  Backups on the unit: `/etc/inittab.session67-orig`,
`/etc/inittab.fixed`.  It is a root change, so the region-0 CRC is unchanged —
but it must go into any hd3 rebake.  **It must be re-applied if the hd3 image is
reflashed.**

**Owed — the kernel fix.**  `sched_init_smp()` widens init with
`set_cpus_allowed_ptr(current, housekeeping_cpumask(HK_TYPE_DOMAIN))`, which is
`cpu_possible_mask` (0-1) with no `isolcpus=`; the sources are byte-identical to
pristine, and there is no `isolcpus=`/`nohz_full=` in `/proc/cmdline`.  The
suspicion is that `cpu_active_mask` has not yet gained CPU 1 when the call runs,
so `cpumask_any_and_distribute(cpu_valid_mask, new_mask)` clamps to CPU 0.
**One instrumented boot** printing `cpu_active_mask`/`cpu_online_mask` at the top
of `sched_init_smp()` and PID 1's `cpus_mask` immediately after the call settles
it; the `PBCPU`/`PBDBG` `printascii` markers are already in
`local/kernel-tree/init/main.c` (and are deliberately not in the patch).

**cpufreq — corrected.**  `cpufreq-dt` **is** bound and scales:
`scaling_driver = cpufreq-dt`, OPPs `300000 600000 800000 1008000`, governor
`ondemand`.  **What is missing is the CPU supply**: no `supply-*` entries under
`/sys/kernel/debug/opp/cpu0/`, so the OPP voltages are never applied.  The
`twl6030_uv_to_vsel: OUT OF RANGE` lines are **startup VC init**, not a runtime
cpufreq fault: `omap4_mpu_vc_data.on = 1375000` and
`OMAP4_VP_MPU_VLIMITTO_VDDMAX = 1410000` exceed `twl6030_vsel_to_uv(0x39)` =
1316660, and the function clamps to VSEL `0x3A` = 1.35 V — exactly the value the
chainloader commands.  So 1008 MHz @ 1.35 V is consistent and intended; decide
"1.35 V for all OPPs" (silence/skip the out-of-range entry) vs a real supply
binding — this TWL6030 revision cannot express >1.317 V except the hardcoded
1.35 V.  **1.2 GHz crashes the unit** (report 257).  SmartReflex MPU is present
but idle (`autocomp = 0`; Class3 init only) — that is the next lever.

**Then re-measure** the memory throughput (report 263 §4b) and the UI latency
now that CPU 1 takes userspace work.

## 0f. SGX540 — the client path AND the buffer waste are both done

> **The GPU client path is complete and validated on the panel (session 69).**
> catacomb serves a PRIME-only `wl_drm`; the imported dma-buf is bound with
> `CLAMP_TO_EDGE` (r10003, trap 347); epitaph binds its GL locations explicitly
> (r10008, trap 348).  Wallpaper, keyboard and the bar's text/icons render on the
> SGX; bar draw 1.5–30 ms (softpipe was 1.6–2.3 s).  Report 269 §4g–§4i.
>
> **The 8× buffer waste behind trap 349 is fixed and deployed (session 70).**
> See §0f.1 and report 271.  Nothing in §0f is an open GPU question any more —
> what is left is making it survive a reflash.

### §0f.1 SESSION 71's ONE EXPERIMENT — the hd3 rebake (the only thing standing between here and durable)

**Why it is the one experiment.**  Everything session 69 and 70 won — the
catacomb `wl_drm` + GL-wrap fixes, epitaph's bound attributes, and the 8× pitch
fix — exists **only on the live root**.  The flasher-restorable hd3 image is
still the session-59 state, so one flash reverts all of it (trap-343 class).
Nothing else on the bench is worth doing first.

**The exact delta, measured (not assumed) by reading the baked image's own
`/lib/apk/db/installed` and files:**

| item | baked image | live root | action |
|---|---|---|---|
| `catacomb` | `1.5.0-r10000` (binary sha256 `0af20d3a…`) | `r10003` | **upgrade** |
| `epitaph` | `1.4.0-r0` | `r10008` | **upgrade** |
| `/etc/inittab` | no affinity line | `::sysinit:/usr/sbin/taskset -p 3 1` | **add** |
| PowerVR pitch fix | absent | `/usr/local/lib/dbmfix.so` + wrapper + `.desktop` edit | **add** |

Already correct in the image — **do not touch**: `/etc/catacomb/initrc` (md5
`00fdff8481e7c4dde730221ef55d53da`, the pvr variant, no `unset`),
`/etc/profile.d/mesa.sh`, `/etc/modules`, `libglvnd` / `mesa-pvr-dri-classic` /
`sgx-ddk-um`.  Not needed (measured absent on the live root too):
`libLLVM-13`/llvmpipe `swrast_dri.so`, and custom udev/polkit rules (both
directories are empty).

**Inputs.**

```sh
# the shim: rebuild with the musl toolchain (the pmbootstrap build chroot is
# deleted after each build; the host armv7-eabihf is glibc and unusable)
. tools/env.sh   # exports MUSL_CC (armv7 musl; see local/toolchains/)
$MUSL_CC -shared -fPIC -O2 -o local/work/dbmfix.so tools/bench/gpu/dbmfix.c -ldl

# the bake (root needed: loop mount + apk --root)
DBMFIX_SO=$PWD/local/work/dbmfix.so tools/rebake-hd3.sh --dry-run   # inspect
DBMFIX_SO=$PWD/local/work/dbmfix.so tools/rebake-hd3.sh             # bake a COPY
```

Toolchain if it must be refetched:
`https://musl.cc/arm-linux-musleabihf-cross.tgz` (98 MB, sha256 `11e155a0…`).

**Then flash it** (loader block `0x4915`, the pre-OS `0xF7` loop):

```sh
python3 tools/bbusb.py flash \
    --component local/work/nura/images/blackberry-playbook-root-gpu-<DATE>.img \
    --dest-block 0x4915 --from-bootrom --password 0000 --apply
```

Start the flasher, then reset/power on; the 626 MB write takes ~20 min.  **This
is the one genuinely risky step** — read `SAFETY.md` §4 first, and note the write
itself is the documented loop, not a new path.

**What each result means.**

| result | meaning | next action |
|---|---|---|
| script runs, image boots, `apk info -v catacomb epitaph` shows `r10003`/`r10008`, `grep taskset /etc/inittab` matches, desktop renders | **done** — the GPU work is durable | verify the pitch fix survived: `cat /sys/kernel/debug/dri/omapdrm.0/gem`, largest object should be **2,457,600**, not 19,660,800 |
| the image boots but the desktop is black / clients fail | one of the four delta items is wrong or incomplete | check `grep Exec= /usr/share/wayland-sessions/catacomb.desktop` and that `/usr/local/lib/dbmfix.so` exists in the image |
| the image does not boot | the base image or the install step is broken | reflash the **unmodified** base `…-osk-wvkbd.img` (`07555bde…`) — the original is never touched by the script |
| the script fails at `apk --root` | the copy lacks space or the packages do not resolve | the base has ~225 MiB free and no resize is needed; check `DBMFIX_SO` is a real armv7-musl `.so` |

**Pass criterion:** after a flash, the desktop renders, trap 349 does not
reproduce under 8 re-rendering clients, and the pitch fix is in the image — so a
*second* flash changes nothing.

### After the rebake — the queue

1. **Heavy-redraw / frame-latency measurement.**  Still unmeasured, and now
   meaningful: the TILER pressure that used to confound it is gone.
2. **Trap 350** — cosmetic pixel glitches on shm/dmabuf clients.  Prime suspect
   is still the bring-up-era "no `GL_EXT_unpack_subimage`" upload path and the
   damage/repaint code, **not** the session-69 fixes.
3. **Package the pitch fix properly** as a `pvr-dbmfix` APK rather than the
   `/usr/local` files, so the rebake is reproducible from packages.
4. The CPU items (§0e): the voltage decision, the kernel-side PID-1 affinity
   fix, and the memory-throughput re-measure (the old figure is single-core).
5. The intermittent USB teardown (trap 337) — capture before the drop.

### The rest of the GPU state

**Working:** `pvrsrvkm_omap4_sgx540_120` binds (`pvrsrvkm 56000000.gpu: Enabling
quirks`), `catacomb` runs, `card0-LVDS-1` is connected and the backlight is 255.
**The baked image on hd3 is the session-59 state**: it predates `epitaph`
r10008, `catacomb` r10003, and the pitch fix; its `/etc/inittab` has no affinity
line.  **A rebake is owed** to make the session-69/70 result durable.  Note the
earlier claim that the image also predates "the llvmpipe `swrast_dri.so`
replacement and the udev/polkit/gorm rules" is **withdrawn** — measured this
session, the shipped `swrast_dri.so` contains no llvmpipe strings and no LLVM
symbols (so there is no llvmpipe replacement in the working state at all), and
both rule directories are **empty** on the live root.

**Memory throughput (measured 2026-11-18):** the EMIF is correctly configured —
both instances identical, 400 MHz LPDDR2-800, 32-bit, CL 6, power-down disabled,
L3 at 200 MHz — and the CPU is at 1008 MHz, but a userspace benchmark reads only
**0.4–0.6 GB/s** (writes ~2 GB/s; no scaling).  It is an open performance item
(report 263 §4b) **measured single-core**, so it must be redone now that both
cores take userspace work.

## 0g. WM8994 audio, touch, sensors — closed

Audio: headphones and both speakers play `pbplay`'s 440 Hz tone at 48 kHz.
Listening tests: lower `pbmix set "DAC1 Volume" 40` **once**, restore `96` at the
end; a bare `pbplay` plays a 2 s tone (trap 212).  Touch: I²C2 `0x48072000` =
`i2c-1` at `0x65`, GPIO2_3 edge IRQ (`irq 110`); a **cold** boot leaves it down
until GPIO1_14/GPIO4_8 are pulsed, which the driver does not drive.  The sensors,
charger, FAN5702 and CM3217 work; the ISL9519 is the board's charger (the TWL6030
BCI is inert).

## 0h. The minimal 1917 oracle

Debloated 2.1.0.1917 IFS+RCFS (`tools/build_minimal_qnx.py`), qconn on
`169.254.0.1:9090` + root ssh.  Its `/base` is pruned (no firmware dir).  No
wifi/BT service runs, so `ifconfig ti0 up` and the BT start are manual.  Tools
are not on the UFS — push `memdump3`/`memw32`/`pbi2c` to `/tmp` after every boot.
**Bench only** (trap 173).  The full 1917 RCFS is in the region-0 slot; the
minimal pair is `local/work/minimal/setB.signed`.

## 1. Useful local extracts

* `local/kernel-tree/` — the 7.3.3 build tree (gitignored).  It carries the
  session-62/63/64 **diagnostics** that are deliberately not in the patch:
  `PBDBG` markers (`init/main.c`), `PBCPU`/`PBIPI1`
  (`arch/arm/kernel/smp.c`, `arch/arm/mach-omap2/omap-smp.c`), `PBSMF`
  (`local/kernel-tree/kernel/smp.c`), the `head.S` `CRR` stamp, and the
  `MINI4x6` font link (`lib/fonts/Makefile`).
* `local/pristine/linux-7.3.3` — the pristine tree for exports and diffs
  (`local/pristine/linux-7.3.3.tar.xz`).
* `boot/chainloader/kernel.img` (`0x3547D613`) — the current region-0 image;
  the previous `0xC9FEE2D1` (`local/work/kernel-armv7-console-cpu1.img`) and the
  recovery `kernel-usbdev-cpu1.img` (`0xC675E206`) are beside it.
* `local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img`
  (`07555bde…`, 625,999,872 B) — the hd3 root and its flasher-restorable base.
* `local/work/egltest-client/` — the prebuilt armv7 probes
  (`pb-egltest` sha256 `7eda9fa3…`, plus `pb-gbmsample`, `pb-gbmimport`,
  `pb-dlopen`, `pb-layerdrm`, `pb-atlas`, `pb-attrloc`, …).  Sources tracked
  under `tools/bench/egl/`.
* `local/work/src/catacomb-150/` and `local/work/src/smithay-check/` — the
  sources the compositor is built from (Smithay pinned at `dba255c`), and the
  reference for anything that needs changing in the compositor.
* `local/work/nura/run-build-{catacomb,epitaph,phoc}.sh` — the root-owned
  pmbootstrap chroot builds (the operator runs them).
* `local/work/soak65.py` — the reboot+soak driver (console shell `playbook:~#`).
* `local/work/kmsgcap.py` — the ACM capture (retries `cat /dev/kmsg`; also
  `--raw`).
* `tools/bbusb.py`, `local/work/flash825.sh` — the flasher and the region-0
  wrapper; hd3 takes `--component … --dest-block 0x4915`.
* Reports **270** (trap 349, current), **269** (the GPU client path),
  **268** (the GPU plan), 267, 266, 265, 264, 263, 257.

## 2. Build and flash (7.3.3)

```bash
REPO=$PWD; . tools/env.sh
K=local/kernel-tree
arm-linux-gcc -static -O2 -Wall -o initramfs/root/console initramfs/console.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/init    initramfs/init.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/pbrpmb  initramfs/rpmb.c
rm -f $K/usr/initramfs_data.cpio*
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 modules
ZIMAGE=$K/arch/arm/boot/zImage DTB=$K/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb \
    python3 tools/build_kernel_image.py     # prints CRC; image must be < 0xB00000
```

**Build modules IN-TREE, not with `M=`.**  Strip with `--strip-debug`, never
`strip -s`.  A module/firmware-only change keeps the region-0 CRC (trap 190);
with the root on hd3, `modules_install` into a staging dir, tar it, and install
it over ssh (`/lib/modules/7.3.3` + `depmod`).  `.config` changes are expensive:
they change the vermagic (`SMP`, `ARMv7`) and require rebuilding **every**
module.

**Flash region 0** at block `0x825`, hd3 at `0x4915`
(`python3 tools/bbusb.py flash --component <img> --dest-block 0x4915
--from-bootrom --password 0000 --apply`; the 626 MB write takes ~20 min).
Start the flasher first, then reset/power on; the power-on bootrom is caught
reliably (trap 323: it can hold ~20 s).  Region-1-only flashes are at `0x2915`.

**The hd3 rebake (owed) — `tools/rebake-hd3.sh` writes the delta to a new
image.**  **Verified in session 70 by reading the baked image's own
`/lib/apk/db/installed` and files (not assumed):** the image carries
**catacomb `1.5.0-r10000`** (binary sha256 `0af20d3a…`) and **epitaph
`1.4.0-r0`**, and its `/etc/inittab` has **no** affinity line.  So the delta is
exactly three things: catacomb → `r10003`, epitaph → `r10008`, and
`::sysinit:/usr/sbin/taskset -p 3 1` before the OpenRC entries.
**Already correct in the baked image — do not "fix" them:** `/etc/catacomb/initrc`
(md5 `00fdff8481e7c4dde730221ef55d53da`, the pvr variant with no `unset`,
identical to the live root), `/etc/profile.d/mesa.sh` (exports the pvr
override), `/etc/modules` (`pvrsrvkm_omap4_sgx540_120`), `libglvnd 1.6.0-r2`,
`mesa-pvr-dri-classic 21.3.9-r1`, `sgx-ddk-um 1.17.4948957-r1`.
**Not needed (the older handoff listed them; measured absent on the live root
too):** `libLLVM-13.so` / an llvmpipe `swrast_dri.so` (the live
`swrast_dri.so` pulls no LLVM and the desktop runs on `pvr_dri`), and custom
udev/polkit "catacomb" rules (`/etc/udev/rules.d` and `/etc/polkit-1/rules.d`
are both **empty**).
Run `tools/rebake-hd3.sh --dry-run` first, then as root; it copies the base,
loop-mounts the copy, `apk --root`-installs the two packages (offline,
`--allow-untrusted`), adds the inittab line, and prints the new image's sha256
plus the versions read back from the image's database.  Verify with
`tools/pbssh.sh 'apk info -v catacomb epitaph; grep taskset /etc/inittab'`
after a flash.  Installing an APK on the unit directly is
`apk add --no-network --allow-untrusted --upgrade /tmp/x.apk`.

**Patch export (traps 161/211).**  Current: **67 files / 9 136 lines**, sha256
`d19adff8…`, verified **0 rejects / 67/67** against pristine 7.3.3 (session 67).
**Unchanged by sessions 68/69/70** (no kernel/DTS change).  Method: start a
scratch tree from pristine + the *previous* patch, overwrite the files whose
functional delta you intend, regenerate with `diff -u -N --label a/… --label b/…`
over the file list, then apply the new patch to a fresh pristine scratch and
`cmp` every file byte-for-byte.  Also run the tree-vs-pristine walk (trap 211).
Update `kernel/patches/README.md` and `KERNEL-PORT.md`.

## 3. Bench — state inherited at the session-70 close (all values re-measured)

| item | value |
|---|---|
| OS | **Linux 7.3.3 `#109 SMP`**, ARMv7, `armv7l`, dual-core 1008 MHz — **not** TabletOS |
| region 0 | CRC **`0x3547D613`** (media trailer `1f 2d c8 d7 13 d6 47 35` at raw `0x8100000 + 10 448 868 − 8`); `boot/chainloader/kernel.img`, 10,448,868 B |
| region 1 | sha256 **`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`**, 64 MiB at `0x29000000`, **not booted** |
| hd3 | Nura ext4, magic `0xEF53`, **15 273 984** × 4 KiB (62.56 GB), mounted rw at `/` (`/dev/loop0`, `nobarrier`) |
| `BOOTCTR_FCTRY` | **`0x1e`** (block `0x3a` record@196); `/init` tops up below `0x1e` |
| CPU | `nproc` **2**, `taskset -p 1` → **3** (the inittab widening is in place) |
| desktop | catacomb `1.5.0-r10003` + epitaph `1.4.0-r10008`, all processes on **`pvr_dri`**, idle pinned TILER **9.5 MiB** |
| panel | `card0-LVDS-1` **connected**, backlight **255**, zero DSI errors; operator confirms wallpaper/keyboard/bar |
| battery | **Full**, 4.148 V, 100 % |
| SSH | `PB_LINK=usb tools/pbssh.sh` **works**; the door (`PB_LINK=door`) is the TabletOS oracle |

**Device-side files deployed this session and NOT in the baked image**
(`/usr/local` on the live root — a reflash loses them):
`/usr/local/lib/dbmfix.so` (sha256 `cc302b42…`),
`/usr/local/bin/catacomb-dbmfix`,
`/usr/share/wayland-sessions/catacomb.desktop` (Exec repointed) and
`catacomb.desktop.orig` (the backup; revert = copy it back and reboot).
`/tmp` also holds the ~15 `tools/bench/gpu/` probes — they are on the ext4, not
tmpfs (trap 354), but they are throwaway.

**No restore is required.**  The 1917 IFS at block `0x825` is only needed if the
unit must be left on TabletOS.

* **SSH on Nura:** `PB_LINK=usb tools/pbssh.sh 'cmd'` (root, key-only,
  `169.254.0.1:22`) — verified this session.  **If it times out, check the HOST
  first:** `ip rule` / `ip route show table 51820` for a WireGuard
  `default dev utun420` before `lookup main` (trap 341), and
  `ufw allow in/out on enp8s0f3u1i2` (trap 342).  The ACM shell is
  `playbook:~#`, **not** `pb> ` (trap 274); use `tools/pbraw.py`.
* **Kernel printk goes to the panel** (`tty0`/fbcon).  The DTB's
  `console=ttyGS0,115200` is **inert**.  Read the panel, or `cat /dev/kmsg`.
* **Reboot:** use **`reboot -f`** (plain reboot is very slow).  A `reboot -f`
  wedge is trap 224; use `echo b > /proc/sysrq-trigger`.  The root is ext4 with
  `barrier=0`, so a clean reboot still beats a power cut.
* **`BOOTCTR_FCTRY` is readable on our image:**
  `tools/pbssh.sh 'dmesg | grep pbrpmb'` → `… record@196 = <4 bytes LE>` then
  `0x<v> >= threshold 0x1e, no write`.  It read **`0x1e`** at the session-70
  close (three boots: `0x20 → 0x1f → 0x1e`) — that is the top-up threshold, so
  the next boot tops it up.  A logged read, not an RPMB access (`SAFETY.md` §2).
* **Check `nproc` after every boot,** not just `online` (trap 340).  `taskset -p
  1` should read `3`; if it reads `1`, the inittab line is missing.
* **The eMMC WRITE-busy stall is live:** `mmc1: Card stuck being busy` plus a
  lost async WRITE during write bursts.  It has already damaged the rw ext4 once
  (trap 336); prefer clean reboots.
* **`/tmp` is NOT tmpfs on this root** — it is on the 62.5 G ext4, so probes
  survive a reboot (trap 354; the session-69 note's "re-push the probes" was
  true of the region-1 BusyBox root).  Check `mount | grep ' /tmp '`.
* **Never `slay` by name on the oracle** (trap 221); kill by pid.  Device
  password `0000`, once.  An agent never writes RPMB; NVRAM read-only.
* **WLAN checks:** `lsmod` must show **`ccm` and `ctr`**;
  `ip link set wlan0 up` → `wlcore: firmware booted (Rev 7.3.5.0.98)`.
* **WM8994:** `i2ctransfer` args are DECIMAL (trap 166); bus 4, addr `0x1a`.
* **Do not restart `tinydm` to switch compositors** — it wedges the DSI link and
  costs a reboot (traps 289/353).  One switch per boot.

## 4. Traps added last session (full list 14–355 in `KNOWN-ISSUES.md` §2)

* **Session 70 added 351–355, closed 349, and fixed the 8× waste:** **351**
  never read "zero `wl_buffer.release`" from a run that died before its first
  swap (two distinct failure modes look alike); **352** `dma_buf/bufinfo`
  **blocks** and `dri/*/mm` is the VMA space, not TILER occupancy — sum
  `dri/omapdrm.0/gem`; **353** restarting `tinydm` to switch compositors wedges
  the DSI link; **354** `/tmp` is not tmpfs on the hd3 root; **355** an SSH
  timeout is **not** evidence of trap 337 until the cable is checked.
* **349 is closed** — the OMAP4 TILER window (128 MiB) was the buffer budget,
  and the 8× pitch-alignment waste that filled it is **fixed and deployed**
  (report 271).  **Log a syscall's arguments rather than inferring from its
  result** — every size-only probe could not tell "8× height" from "8 bytes per
  row".
* **347** imported dma-buf bound as an incomplete GLES2 NPOT texture (the black
  clients); **348** hard-coded GL attribute/uniform indices in epitaph; **350**
  cosmetic pixel glitches (still open).
* **334.** The kernel was built **`-march=armv6k`** because
  `CONFIG_ARCH_MULTI_V6=y`; fix: `scripts/config --disable ARCH_MULTI_V6`.
* **335.** A stock-mailbox image with **`panic=10`** boot-loops and spends
  `BOOTCTR_FCTRY`.  **Boot a deliberately-faulting image with `panic=0`.**
* **336.** A **power cut damages the read-write hd3 ext4** (`barrier=0`).
* **337.** The **USB/VUSB teardown is intermittent**, not an idle timer.
* **339.** `twl6030_usb_probe()` stored `&twl->comparator` before `ldo_init()`,
  which returned `-ENODEV` for every error → a dangling comparator.  Fixed.
* **340.** **`online=0-1` does not mean both cores take userspace work.**
  **Check `nproc`, not just `online`.**
* **341.** A host **WireGuard VPN** blackholes the RNDIS link (`tcpdump` shows
  zero packets while raw `arping` replies).  Check `ip rule` first.
* **342.** **`ufw` default-deny** blocks the RNDIS link.
* **343.** A reflash of the **session-59 image** reverts hd3's `epitaph` and
  `catacomb` to their older versions and **loses the pitch fix and the inittab
  affinity line** (measured in session 70: the image has `catacomb` r10000,
  `epitaph` `1.4.0-r0` and no `taskset` line).
* **344.** **Do not grep ELF strings with `^name$` anchors**; the unit has no
  binutils, so `nm`/`readelf` there return nothing — pull the file to the host.
* **345.** A Wayland client gets the SGX only if the compositor serves `wl_drm`.
* Still current: **333**, **332**, **331**, **330**, **327**, **323**, **322**,
  **321**, **320**, **302**, **300**, **274**, **190**, **84**, **55**, **47**.

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **the pitch fix / trap 349 (current)** | **report 271** (fix), report 270 (mechanism); `ROADMAP.md` item 7a; DECISIONS §104 |
| **the pitch-fix source and probes** | `tools/bench/gpu/` (`dbmfix.c` + eight `ctypes` probes) |
| **the hd3 rebake** | `tools/rebake-hd3.sh` (`DBMFIX_SO=…`) |
| **the GPU client path** | report 269 §4g–§4i; DECISIONS §102/§103 |
| **the GPU probes** | `tools/bench/egl/`, prebuilt in `local/work/egltest-client/` |
| **the CPU-affinity workaround** | hd3 `/etc/inittab` (`::sysinit:/usr/sbin/taskset -p 3 1`); backups `.session67-orig`, `.fixed` |
| **the TILER evidence** | `docs/captures/gpu-tiler-exhaustion-2026-11-24.txt` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| initramfs root selection | `initramfs/init.c` `ROOTFS_RAW_OFF` (hd3) |
| flasher (region 0/1, hd3) | `tools/bbusb.py` (+ `local/work/flash825.sh`) |
| the hd3 root image | `local/work/nura/images/…-osk-wvkbd.img` |
| catacomb/smithay sources | `local/work/src/catacomb-150/`, `local/work/src/smithay-check/` |
| reports / sessions | `docs/reports/` (271 current), `docs/sessions/session-70.md` |

## 6. Working rules

* After every console command, dump the log tail and read it.  Read
  `dmesg`/`sloginfo` **unfiltered** after a reboot or structural change.
* **Don't clear `dmesg` under the operator** without saying so.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**  **A test boot is not a budget concern.**
* **Never present a guess as a fact.**  Prefer a fresh read.  A diagnostic can
  manufacture the fault it reports — session 70 produced a whole wrong theory
  from sampling runs that had died before the event being counted (trap 351).
* **Revert experiments and prove the revert.**  A reverted experiment still has
  to be shown reverted.
* **Use the reference before the bench** (DECISIONS §61).  Subagents:
  static/file-only (trap 109) — and **time-box them**.
* **Look up every external fact** (the operator's rule; the assistant's
  knowledge is frozen).
* **Only the operator decides when to stop / wrap up.**
* **Ask the operator what is on the panel** for a visual check; do not take
  screenshots (operator preference, session 69).
* **Do not restart the compositor session to switch sessions** (trap 353).

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 71 (absolute; Linux-era 65)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a, KNOWN-ISSUES.md §2 (traps 14-355, especially
334-355) and §3, DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 91-104,
docs/reports/INDEX.md, reports 271 (current), 270, 269, 268, 267, 266, 265,
264, 263, 257, and docs/sessions/session-70.md (and session-69.md for the GPU
client path history).

WHERE IT STANDS: the unit runs Linux 7.3.3 (region 0 0x3547D613, #109 SMP,
ARMv7, dual-core 1008 MHz) with the Nura root on hd3 (62.5 G).  **THE GPU IS
DONE AND VALIDATED ON THE PANEL, INCLUDING THE BUFFER WASTE (sessions 69-70).**
Wayland clients render on the SGX (catacomb r10003, epitaph r10008; report 269
§4g-§4i), and **the 8x buffer waste is root-caused, fixed and deployed**
(report 271): libdbm described a linear buffer as width=1, height=<bytes>,
bpp=8, and omapdrm aligns the scanline pitch to 8 bytes, so every buffer was 8x
too big.  The fix (tools/bench/gpu/dbmfix.c, an LD_PRELOAD on ioctl) re-expresses
the bytes as width=<bytes>, height=1.  Deployed on the live root
(/usr/local/lib/dbmfix.so + a catacomb-dbmfix wrapper + catacomb.desktop
repointed), measured idle desktop **59.4 -> 9.5 MiB** pinned, every GEM object
**1.00x**, **8/8** re-rendering clients clean, panel operator-confirmed twice.
**Trap 349 no longer reproduces.**

THE ONE EXPERIMENT (session 71): **the hd3 REBAKE** — the only thing between
here and durable.  Everything the GPU won lives only on the live root; the
flasher-restorable image is still the session-59 state, so one flash reverts it
(trap-343 class).  HANDOFF §0f.1 has the exact measured delta (catacomb r10003,
epitaph r10008, the inittab affinity line, and the pitch fix), the inputs
(tools/rebake-hd3.sh with DBMFIX_SO=..., plus the musl toolchain URL), the flash
command (block 0x4915), and a table of what each possible result means.  The
script is dry-run verified and NOT yet run (needs root for the loop mount).

IF THE REBAKE IS BLOCKED, the queue is: a heavy-redraw/frame-latency
measurement (still unmeasured, and now meaningful because the TILER pressure is
gone); trap 350 (cosmetic glitches on shm/dmabuf clients; suspect the
bring-up-era upload/damage path, NOT the session-69 fixes); packaging the pitch
fix as a proper pvr-dbmfix APK; then the CPU items in §0e and the USB teardown
(§0d).

BENCH (verified at the session-70 close): region 0 trailer = 1f 2d c8 d7 13 d6
47 35 -> CRC 0x3547D613 (boot/chainloader/kernel.img, 10,448,868 B, sha256
6ecd068d...); region 1 = sha256 20a5edd3... (64 MiB at 0x29000000, not booted);
hd3 = the Nura ext4 (ext4 magic 0xEF53 at 0x49000438, 15,273,984 x 4 KiB
blocks, root /dev/loop0 rw).  BOOTCTR_FCTRY = 0x1e at the close (0x20 -> 0x1f ->
0x1e over three boots — that is the top-up threshold, so the next boot tops it
up); /init tops up below 0x1e.  Idle desktop pinned TILER = 9.5 MiB with the
pitch fix live (was 59.4 MiB).
SSH: PB_LINK=usb tools/pbssh.sh WORKS; if it times out check the host VPN (trap
341) and ufw (trap 342) first.  Kernel printk goes to the panel; console=ttyGS0
is inert.  Use `reboot -f` (plain reboot is very slow).  Start the flasher, then
reset/power on (the bootrom holds ~20 s); a 626 MB hd3 write takes ~20 min.
Read dmesg UNFILTERED.  **Ask the operator what is on the panel for a visual
check; do not take screenshots.**  **Do not restart tinydm to switch
compositors — it wedges the DSI link and costs a reboot (trap 353).**  An agent
never writes RPMB; NVRAM read-only; never program WDT2 from the chainloader
(trap 322).

PATCH: unchanged by sessions 68/69/70 (no kernel/DTS change — the pitch fix is
userspace, an LD_PRELOAD, so it does not touch the kernel or the patch) — 67 files / 9 136
lines, sha256 d19adff8..., verified 0 rejects / 67/67 against pristine 7.3.3.
Re-export (and update kernel/patches/README.md + KERNEL-PORT.md) after any DTS
or driver change; the reliable method is in §2.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
The hard hazards (BOOTCTR_FCTRY 0 / >0x32 / failed write / flat pack) remain.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  WRAP-UP: only the operator
decides when the session stops; then record the bench state, write the session
note, update the canonical docs, verify with tools/docs_audit.py and
tools/tests/test_offline.py, commit privately and publish the snapshot
(docs/project/REPO-MANAGEMENT.md).  No restore is required.
```
