# Current state

## Current state — 2026-11-16 (session 61, IN PROGRESS)

**THE CPU ITEM IS OPEN AND THE FAILURE IS LOCALISED TO ~0.000 s: `CONFIG_SMP=y`
boots and stops right after the RCU geometry print (`rcu_init()`).**  The SMP kernel (`0x202E9F38`,
`NR_CPUS=2`, `maxcpus=1`) runs (`SLUB: … CPUs=2`) and prints the RCU init lines,
then stops at `rcu: RCU calculated value of scheduler-enlistment delay is 10
jiffies.` (`adjust_jiffies_till_sched_qs()` from `rcu_init_geometry()` inside
`rcu_init()`) with no oops text; the loader's WDT2 then resets it.  The finer
localization is **not** established: three `PBDBG` marker builds were silent
(built while the DTS had `earlyprintk` removed — always
`grep -ac earlyprintk <image>` before trusting a marker run) and the one with
`earlyprintk` left a *shorter* trace than the plain SMP image, so the `+0x1000`
printk window has an unidentified writer (trap 317a).  **Identify that writer
first.**  **The SCU, the register
firewall and the ROM public API are all excluded**: `SCU_CONFIG = 0x1` (2 cores),
`SCU_CTRL = 0x531` (enabled), `0x4824380c = 0`, WUGEN `AUX_CORE_BOOT_0/1 = 0`,
PL310 ID `0x410000C4` all read from non-secure Linux, and SMC is a privileged
instruction (a QNX user probe gets `SIGILL` even for `0x102`, which our own
kernel uses).  [report **254**; traps 315–320]

* **The reference is measured dual-core at 1008 MHz** — `pidin info` on the live
  oracle: `Processor1` and `Processor2`, both `Cortex A9 MPCore 1008MHz`, on
  `/procnto-smp-instr`.  That is the MPU OPP target (handoff §0b, trap 312).
* **The ring can only be read by the boot that immediately follows the failure.**
  Three writers: the chainloader (`clear_ring 0x88000000` + breadcrumbs), the
  decompressor (`busyuart` mirrors chars into all three rings), and pbmon (a
  printk window at `+0x1000` that does **not** touch count/index — it is what
  carries a failed boot's tail).  QNX fills free RAM with `0xAA`, so an oracle
  boot destroys it.  `dd if=/dev/mem` fails on the `no-map` pages; use
  `tools/bench/ringdump.c` (`mmap`).  [trap 316/317]
* **Bench at the wrap-up (all values read off the media, `/dev/mmcblk1`, on the
  unit):** region 0 = **the region-1 image, CRC `0x433232F1`**; its
  10 355 592 bytes at raw `0x8100000` hash to
  `bbee7bebb61eeaa4167a6bcfef83b6df6e858beb4b902a29846dcea57d91156d`, equal to
  `local/work/kernel-region1.img`, with the trailer `1f2dc8d7 f1323243` =
  magic `0xD7C82D1F` + CRC `0x433232F1` at that offset.  Region 1 = the busybox
  root at raw `0x29000000`, 64 MiB, now
  `a32772720e30d2aad53dc88cc7c7125d81b9cdf339448dff8d27d625c8a05f55` (the baked
  image was `49e318bf…`; the root was mounted read-write).  Linux `7.3.3 #79`,
  `online = 0`, root `/dev/loop0` (58.7 M, `rw,nobarrier`), 43 modules in
  `/lib/modules/7.3.3` (the mismatched set — trap 320), `pb> ` on the ACM,
  dropbear on `169.254.0.1:22` (`PB_LINK=usb tools/pbssh.sh`).
  **`BOOTCTR_FCTRY = 0x31`** at the last boot (`31000000`, ≥ `0x1e`, no write).
  No device-side files are deployed (`/tmp` is tmpfs and was wiped by the
  reboot).  **hd3 = the 1917 vendor UFS** (the Nura root is gone; see below).
* **The region-1 root's modules no longer match the config** —
  `.gnu.linkonce.this_module section size must match` on every `insmod`; rebuild
  them before using region 1 as a real bench root.  [trap 320]
* **Host:** the RNDIS interface name flaps (`enp8s0f3u1` / `enp8s0f3u1i2`), so the
  `pb-rndis` NM profile must be bound by MAC (`02:1a:11:00:00:02`), not by name.
  [trap 318]
* **How the media got here:** to read the ring a live QNX was needed, which
  needed a UFS on hd3.  In order: the shim IFS at `0x825`
  (`ifs-shim-sealed.ifs`, `b52195bb…`); a patched minimal RCFS
  (`minimal/rcfs-noufs.rcfs`, `9b67cbdb…`) — **no network**; the session-43
  `setB.signed` (`d22eab3b…`) — **no network**; the full modified 1917 RCFS
  (`rcfs-1917.rcfs`, `c4ab6e34…`, packed `rcfs1917-flash.signed`) — **USB +
  qconn work**; and the 1917 vendor UFS with the devuser injection
  (`ufs-1917-devuser.ufs`, `482e0473…`, packed `371b09a1…`).  **The Nura root on
  hd3 is gone.**  The way back is `local/work/nura/images/
  blackberry-playbook-root-apps-audio.img` (`f7858110…`, 625 999 872 B) at
  `0x4915`; `/init` regrows it, but the session-59/60 live fixes (llvmpipe,
  `epitaph` r10006, the udev/polkit/gorm rules) must be re-applied.
* **Next:** identify the `+0x1000` window's writer, then bracket `rcu_init()`'s
  own statements (`rcu_init_one`, the three
  `rcutree_*_cpu` calls, the two `alloc_workqueue`s,
  `start_poll_synchronize_rcu_expedited`) with markers and re-run the capture —
  one build, two flashes.  Then the MPU OPP (1008 MHz is the reference's value).

## Current state — 2026-11-16 (session 60)

**THE `epitaph` PANEL IS FIXED — IT STARTS, STAYS UP, AND RENDERS ITS TEXT AND
ICONS (r10005).**  Two client bugs, both measured: the lazy renderer built the
4096×4096 glyph atlas before any EGL context existed (GL calls are silent
no-ops, so the texture was undefined; the first `TexSubImage2D` failed with
`GL_INVALID_OPERATION`, invalid texture level 0, and sampling it crashed
softpipe), and the text shader's dual-source
(`GL_EXT_blend_func_extended`/`gl_SecondaryFragColorEXT`) draw segfaults
softpipe even though the extension is advertised and the shader links.  The
local `epitaph` 1.4.0 package (`pkgrel=10005`) creates the rasterizer with the
EGL context, caps the atlas at `GL_MAX_TEXTURE_SIZE`, and uses a single-output
premultiplied shader.  `timeout 6 epitaph` → `RC=143`; the bar shows `Thu. 1`,
`03:20` and the signal/Wi-Fi/battery icons.  [report 251; trap 292 fixed,
traps 303–305; DECISIONS §86]

* **Region 0 = `0x294F8E36`** (unchanged; kernel `7.3.3 #77`).
* **hd3 (media)** = the grown read-write root with the SSH kit (unchanged);
  the flasher-restorable base is still `f7858110…` (625 999 872 B).  The baked
  `blackberry-playbook-root-osk-wvkbd.img` (`07555bde…`) still carries the old
  `epitaph` and does **not** have the udev/polkit rules — rebake before
  flashing if media should match.
* **Region 1 = `49e318bf…`** (fallback, not booted).
* **Bench:** `PB_LINK=usb tools/pbssh.sh`; pace bulk pushes (8 KiB/5 ms).
  `BOOTCTR_FCTRY` read `0x27` at boot (`27000000`, above the `0x1e` threshold,
  no write).
* **Panel:** r10005 installed.  `catacomb` group (gid 111) with `playbook`;
  `/etc/udev/rules.d/66-epitaph.rules` (backlight) and
  `/etc/polkit-1/rules.d/66-epitaph.rules` (Wi-Fi/cellular).  The backlight is
  `root:catacomb`, `-rw-rw-r--`; a write as `playbook` succeeded
  (`BACKLIGHT_WRITE_OK`).
* **Drawer:** 3 sliders (brightness, DPI scale, flashlight) + 4 toggles
  (cellular, Wi-Fi, GPS, orientation).  Volume is not in the 1.4.0 drawer.
* **Apps / Wi-Fi:** `nmcli radio wifi on` (the radio was disabled) makes
  `wlan0` scan ~10 APs; `gorm` with its `10-gorm.rules` lists them (not
  connected).  tzompantli (1.2.0) launches apps — a tap on Foot runs `foot`;
  launch it into a clean workspace (trap 307).  `uinput.ko` is loaded and
  `tools/bench/uitap.py` injects taps (trap 306).
* **Latency:** the ~2 s slider/button delay was the clients' **softpipe**
  rasterizer (2 s drag: epitaph 3.16 s CPU, catacomb 0.16 s; drawer draw
  1.6–2.3 s).  Replaced `/usr/lib/xorg/modules/dri/swrast_dri.so` with Alpine
  3.16's Mesa 21.3.9 gallium build (**llvmpipe**) + `libLLVM-13.so` + the
  `libdrm_amdgpu`/`libdrm_nouveau` sub-libraries; the original is backed up as
  `swrast_dri.so.amber`.  Drawer `render` is now 2.7–6 ms (~4× overall); the
  remaining 420–550 ms is llvmpipe's deferred `swap` (and the first draw pays
  the LLVM JIT).  The compositor still maps `pvr_dri`.  [report 253, traps
  309–311]
* **CPU: one core at 400 MHz.**  The kernel is built **`# CONFIG_SMP is not
  set`** (so `maxcpus=1` is an unknown parameter and the DT's two CPUs are
  capped), and the DTS bootargs also set `maxcpus=1`
  (`kernel/source/omap4-winchester.dts:88`);
  `/sys/devices/system/cpu/online` = `0`, `nproc` = 1.  The debugfs
  `clk_summary` shows `dpll_mpu_ck = 400000000` (BogoMIPS 795.44); there is no
  cpufreq directory and no OPP table (CONFIG_CPU_FREQ/PM_OPP are on).  Thermal:
  `cpu_thermal` 65.0 °C, `bq27541-0` 34.5 °C.  The reference programs
  `DPLL_MPU` and `PRM_VC_*` (report 223), so this is the biggest remaining
  performance lever.  [traps 312/313]
* **GL clients are software.**  `epitaph`/`tzompantli`/`gorm`/`tabula` use
  glutin/EGL and map `swrast_dri` (llvmpipe); only the compositor maps
  `pvr_dri`.  `wvkbd` (cairo), `foot` (pixman) and `netsurf` (CPU) are
  responsive.  A hardware path for Wayland clients is not started.  [trap 314]
* **Local package:** `local/work/nura/pmaports/main/epitaph/` (`pkgrel=10006`,
  `fix-renderer-init.patch`, sha512 `b149772a…`); APK
  `epitaph-1.4.0-r10006.apk`.  The APKBUILD sets a persistent
  `CARGO_TARGET_DIR` for faster rebuilds.  r10006 carries the draw-phase
  timing logs; build without them for the final image.
* **Next:** enable the second core — build **`CONFIG_SMP=y`** (`CONFIG_NR_CPUS=2`)
  and keep `maxcpus=1` for the first boot; the vermagic changes (`SMP`), so
  rebuild **all** modules including `pvrsrvkm_omap4_sgx540_120.ko` and install
  them in `/lib/modules/7.3.3` before the region-0 flash.  Then remove
  `maxcpus=1` and boot the second core.  After that: the MPU OPP/voltage
  (measure the reference's `DPLL_MPU`/`PRM_VC` on the oracle first), then
  shrink/pre-warm the drawer and pursue a hardware GL path for clients.
  Remove the r10006 draw timing and rebake the hd3 image with the fixes.

## Previous state — 2026-11-16 (session 59)

**THE ROOT IS WRITABLE AND GROWN, AND SSH IS BACK (kernel `#77`, region 0
`0x294F8E36`).**  The initramfs mounts hd3 read-write (no overlay) and a
one-shot `resize2fs` grew it to the partition: **`/dev/loop0 56.5G, 53.9G
free`**, and a file written before a reboot survives it.  A static dropbear and
key are installed into the root on first boot; an OpenRC `local.d` script
brings `usb0` to `169.254.0.1/16` and starts dropbear on `:22`, so
`PB_LINK=usb tools/pbssh.sh` is a clean shell.  The ttyGS0 getty stays as the
recovery console.  [report 249 §1–§2]

**The eMMC "cache flush" item is closed by measurement — the card has no
cache.**  `EXT_CSD.CACHE_SIZE = 0` and `CACHE_CTRL = 0` (direct CMD8 read and
the kernel's debugfs EXT_CSD agree), so the MMC core never issues
`FLUSH_CACHE`; the handoff's "CACHE_SIZE = 249" was the byte *index*, not the
value.  The session-58 error (`sector 0 … flags 0x900000`) is a plain WRITE
(`REQ_BACKGROUND|REQ_ALLOC_CACHE`, no `PREFLUSH`/`FUA`), not a flush; a traced
`sync` showed only CMD25/CMD13 and no CMD6.  The intermittent
busy-after-WRITE remains unexplained, with `barrier=0` + write-through as the
mitigation.  [report 250 §1; traps 300 corrected]

**THE TOUCH POSITION GHOST IS FIXED.**  Fast typing is correct: the driver
keeps sampling while the controller asserts its IRQ line and for a 200 ms grace
period after the last contact, and a new down that repeats its slot's previous
coordinates within 300 ms is held back one sample and reported with the fresh
position.  The same change fixes the lost taps in a fast burst (a fast `hello`
had arrived as `h,e,o`).  [report 248; capture
`touch-munin-2026-11-16-session58.txt`; trap 296]

**The backspace/enter repeat is FIXED — by installing wvkbd on catacomb 1.5.0,
no UI upgrade.**  catacomb 1.5.0 already implements `zwp_virtual_keyboard_v1`
(`VirtualKeyboardManagerState`), which wvkbd speaks; only the OSK package and
initrc changed.  One BackSpace tap now delivers one `0x7f` (was three from
squeekboard).  `wvkbd-mobintl` 0.21 is installed, DejaVu Sans was added for the
glyphs (`--fn "DejaVu Sans 16"`).  `pb-touchwake.py` wakes a manually blanked
panel, but **auto-blank was removed**: repeated DPMS resume wedges the DSI
link (black panel, only a reboot clears it — traps 298/289/301); the panel now
stays lit.  All persist across reboot.  The pmaport
APKBUILD/initrc are updated; the flasher-restorable hd3 base still has
squeekboard.  [report 250 §2/§2a; trap 299 fixed; DECISIONS §85]

**DPMS off cannot be woken by touch on this image.**  After 3 idle minutes the
initrc's `swayidle` sets `bl_power = 4`; the kernel still receives the taps but
the panel stays dark, and `catacomb msg dpms on` restores it.  **Kill
`swayidle` after every boot on the bench.**  [trap 298]

* **Region 0 = `0x294F8E36`** (`0x9E0980` B, `boot/chainloader/kernel.img`
  sha256 `f0738625…`, kernel `7.3.3 #77`) — read-write root, SSH kit,
  `barrier=0`, write-through.
* **hd3 (media)** = the same image grown in place to **56.5 G** with the SSH
  kit: **no longer byte-identical to `f7858110…`**.  The flasher-restorable
  image is still `f7858110…` (625 999 872 B) and is the way back.
* **Region 1 = `49e318bf…`** (the fallback, not booted).
* **Bench:** SSH works — `PB_LINK=usb tools/pbssh.sh 'cmd'` (root, key-only,
  `169.254.0.1:22`); `usb0` is brought up by `local.d/pb-ssh.start`.  The ACM
  `playbook:~#` getty is the fallback.  **Bulk pushes must be paced**
  (8 KiB/5 ms; trap 302).
* **`BOOTCTR_FCTRY` = `0x32`** (this boot read `0x1B` and `/init` wrote
  `0x32`, RPMB counter 12049; the session's reboots drained `0x25 → 0x1B`).
* **OSK:** wvkbd on catacomb 1.5.0 — one tap = one key; panel stays lit (no
  auto-blank — DPMS resume wedges the DSI link).
* **Baked image:** `blackberry-playbook-root-osk-wvkbd.img`, sha256 `07555bde…`
  (hd3 base + wvkbd/DejaVu/pb-touchwake/icon fixes/no swayidle), e2fsck clean —
  **not flashed**; the live rw root already has the fixes.
* **Next:** flash the baked hd3 if media should match; fix epitaph's segfault
  (panel) and optionally install calls/chatty; the upload-path profile
  (`PBGL_PROFILE`); the intermittent WRITE-busy stall.

## Previous states

* **session 58** (2026-11-16) — the root became writable and grew to 56.5 G,
  SSH came back (kernel `#77`, region 0 `0x294F8E36`), the touch position ghost
  was fixed (kernel `#74`); eMMC flush first isolated.  Full snapshot at git
  commit `00228e4` and `docs/sessions/session-58.md`.
* **session 57** (2026-11-16) — apps/background, the touch release fix (kernel
  `#73`), the audio diagnostics gated and the idle suspend removed; a position
  ghost remained.  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
  (`## Previous state — 2026-11-16 (session 57)`).
* **session 56** (2026-11-15) — catacomb runs on the SGX540 (the smithay GLES2
  fallback), the Munin Y axis is fixed, and the SGX driver is vendored; region
  0 `0xAB899C49`, hd3 `89399744…`.  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
  (`## Session 56 snapshot`).
* **session 55** (2026-11-15) — the read-only hd3 + overlayfs
  Nura root booted (region 0 `0xA8F0F30C`, hd3 `1fcf0cb0…`), the eMMC stall
  left the boot path, and the SGX540 kernel driver bound
  (`pvrsrvkm 56000000.gpu`) with the pvrports userspace running EGL/GLES 2.0
  (`PowerVR SGX 540`).  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md).
* **session 54** (2026-11-15) — pmbootstrap built the Nura root, hd3 was flashed
  at `0x4915` and Nura booted, but the eMMC write stall remounted the root
  read-only; the read-only overlay root was implemented and built (reports
  240–242).  The full snapshot is in
  [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md).
* **session 53** (2026-11-15) — the WL1283 BT core works (a full BR/EDR
  inquiry; the reference's `WL1283.bts` replays 315/315 and makes `hci0`
  register with mgmt), and item 7a (hd3/Nura/SGX) was scoped.  The full
  snapshot is in [`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md)
  (`## Session 53 snapshot`).

The per-session snapshots are in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md):

* **session 52** (2026-11-15) — the WL1283 WLAN associates (the `ccm`/`ctr`
  modules), the USB link gets its DMA, and the BT core answers HCI (the UART2
  CTS/RTS pads); reports 236/237.
* **session 51** (2026-11-14) — the WL1283 WLAN works: the `-110` loop was the
  chip's ELP sleep (now CAM) and the never-completing scan was the 5-sr
  firmware (4-sr scans); reports 232–235.
* **session 50** (2026-11-14) — the WL1283 enumerates (`CLK32KG`) and firmware
  boots; the interface recovery-looped (reports 230/231).
* **session 49** (2026-11-14) — the blocker measured from both sides, the host
  program excluded, the "silent card" instrument corrected (reports 216–227).
* **session 48 and older** — see the same file.

## Per-subsystem state

| subsystem | state | detail / evidence |
|---|---|---|
| Chainloader image (CRC path) | **working** | 828 B loader at image `+0x800` = `0x80100000`; copies zImage/DTB high, checks the DTB magic `0xEDFE0DD0`, jumps (`r0=0`, `r1=~0`, `r2=DTB`). Report 109 §8.1, 103 §5.2 |
| DRAM / memory map | **working** | 1 GB at `0x80000000`; kernel runs from `0x84000000`-loaded zImage, decompressed to `0x80008000`; DTS comment |
| Kernel self-capture | **working** | DRAM ring at PA `0x88000000` (32 KB) + breadcrumb/event log; `tools/pb_ring.py`, `tools/pb_events.py`. Bootstrap §3 |
| WDT2 | **working — 58.6 s window, 10 s pet (session 31)** | The loader arms `WLDR = 0xFFFC1800` (7.8125 s); the kernel reprograms it to the reference's `0xFFE2B400` (58.6 s) at the late initcall via `WSPR` (`0xAAAA`→`0x5555` stop, write `WLDR`, `0xBBBB`→`0x4444` start), **polling `WWPS`** after each write, and pets every 10 s (`TGR` read→invert→write); fallback 2 s if the readback fails.  Verified on `0xE36C679A` by `WCRR` reload timing (~10 s) and uptime >275 s.  The expected dmesg line is missing on 7.3.3 (printk anomaly).  Report 109 §8.1 + correction; trap 115; decision §38 |
| eMMC (MMC2) | **reads proven; writes healthy — but writers must use large blocks** | `mmcblk1` = MCGAFA 59.4 GiB, `boot0`/`boot1`/`rpmb`; needs the `vemmc` fixed supply for `ocr_avail` (report 109 §9).  Session 16 read a freshly flashed 11 152 064-byte image back **byte-exactly** and matched a pre-flash TabletOS capture (report 134); the MMC2 functional clock is 96 MHz.  Session 17 measured the write path (report 136): the driver has no request-size limit — an O_DIRECT write gives **256 KiB `CMD25`s, 5.5–7.0 MB/s** (4 KiB chunks: 0.93 MB/s), one `CMD13` per request (normal `mmc_blk_card_busy`).  The old 17–32 KB/s was `recv`'s tiny buffered writes plus 4–7 unthrottled `printk`s per request to **fbcon**.  Use ≥64 KiB (ideally 256 KiB–1 MiB) writes with the MMC trace off.  **Session 18: region 1 now holds a bootable Linux root** (32 MiB ext4 at raw `0x29000000`), written through the pre-OS `0xF7` loop (2055 frames) and read back byte-exactly (QNX `dd` and Linux `blk sumb`); it mounts read-only via a loop and its `/init` runs (report 140).  **Session 54 (report 241): the hd3 writable root stalls this path.**  The first ext4 journal/udev write after the 597 MiB bootrom image gives `mmc1: Card stuck being busy! __mmc_poll_for_busy`; the loop write fails, the journal aborts, and ext4 remounts `emergency_ro`.  A later 4 KiB write to scratch hd3 returns `I/O error`; reads are fine.  The region-1 root is **no journal** and stable.  Workaround: read-only hd3 + overlayfs (`CONFIG_OVERLAY_FS=y`); **session 55 (report 243) confirmed it: the overlay boot has no `Card stuck`, no journal abort, the lower `ro without journal` and all writes in the tmpfs upper.**  The eMMC write-latency/cache fix is a separate experiment. |
| USB device + gadget | **working — DMA now actually used (session 52)** | enumerates `0fca:8010`: CDC-ACM (console) + **RNDIS** (network) in one configuration; High Speed (480 Mbps).  **Session 52 (report 236 §7): the RNDIS path had been running MUSB's PIO fallback** — `dma_channel_program()` refuses buffers that are not 4-byte aligned on RTL 2.00 (`MUSB_HWVERS = 0x0800`), and u_ether hands it 2-mod-4 addresses.  Fixed with `musb->g.quirk_avoids_skb_reserve = 1` (RX) and a TX realign in `gether_start_xmit()`: **RX 3.3 → 6.3 MB/s, TX 3.3 → 4.1 MB/s** (raw `nc`).  The RNDIS function's one-frame-per-transfer structure is now the limit; CDC-NCM is the untested next step.  The ACM console is still only ~134 KB/s and **drops bytes when the host blasts** (report 136 §3.3) — do not send multi-MB images through it.  Reports 114, 136, 236 |
| **WL1283 (MMC5)** | **WORKING — associates, leases and passes traffic (session 52); scans (session 51)** | **Session 52 (report 236): the association blocker was the missing `ccm`/`ctr` crypto modules.**  `ieee80211_key_alloc()` allocates a `ccm(aes)` AEAD for every CCMP key; without it the PTK install returns `-ENOENT` and wpa_supplicant mislabels it `WRONG_KEY` (the handshake itself is fine — message 3 decrypts).  `ccm.ko`/`ctr.ko` ship in region 1 and `rcS` modprobes them before the stack: `wpa_state=COMPLETED`, WPA2-CCMP on 5785 MHz at −41 dBm, MCS7/65 Mbit/s, DHCP **192.168.0.212**, gateway and 1.1.1.1 reachable.  A 5 GHz AP needs `iw reg set <CC>` (the world domain marks 5735–5835 MHz `PASSIVE-SCAN`).  The **per-unit NVS** is shipped (`wl128x-nvs-perunit.bin`).  **Session 51 (report 232): the recovery loop was the firmware's ELP sleep.**  The failing transfer is the **FW-status read** — CMD53 to `0x1efc0`, 84 bytes, byte mode, 256-byte block — from the WL_IRQ thread right after `firmware booted`; it fails only once the chip has slept, and the identical read succeeds when it is awake.  Fix: `wl12xx_default_conf.conn.sta_sleep_auth = WL1271_PSM_CAM` (was `ILLEGAL` → ELP).  Verified after a real region-1 flash + reboot: one firmware boot, zero failures, `wlan0` `UP`, debugfs `sleep_auth` 0.  `wl128x-fw-4-sr.bin` is the **shipped** SR firmware (report 235): 5-sr (7.3.10.0.142) boots but panics on the first scan, so `wl12xx`'s floor is lowered to 7.3.5.0 and `WL128X_FW_NAME_SINGLE` names the 4-sr file; both files stay installed.  **WLAN (report 231):** `wlcore`/`wl12xx`/`mac80211`/`cfg80211` + `wl128x-nvs.bin`, the `ti,wl1283` child (`reg = <2>`, WL_IRQ gpio 53), `cap-power-off-card`, and an `mmc-pwrseq-simple` pulsing `WL_EN` (low → 50 ms → high → 120 ms) → firmware `Rev 7.3.5.0.98`, MAC `14:74:11:8f:9c:c3` (the assigned address, patched into the generic NVS; the radio calibration is still default).  **Enumeration (report 230):** the TWL6030 `CLK32KG` 32.768 kHz SLOW_CLK is physical `0xBC`–`0xBE` and was OFF; `CONFIG_CLK_TWL=y` + an enable in `clk-twl`'s probe (~1.9 s, before MMC5 at ~3.0 s) fixes it.  **Remaining (report 235):** the WLAN **associates** and passes traffic (session 52); open and not blocking use are *why* the 5-sr firmware panics on scan (235 §3) and *why* the ELP wake is not honoured (232 §6).  **Session-49/50 negatives (do not repeat):** the reference's MMCHS5 program and WL_EN ordering replayed, the stock-TabletOS diff, the boot-path RE, the clock sweep (reports 217/218/221–229).  The BT half's enable is `BT_EN` = gpio 55 = `GPIO2_23`, held low while the WLAN works (report 216). |
| **WL1283 BT** | **the CORE WORKS — real inquiry; `hci0` now registers with mgmt (session 53)** | UART2 `0x4806C000` = `/dev/ttyS1`, H4 at 115200 8N1; `BT_EN` = gpio 55 = `GPIO2_23` (pad `0x4A10007C` mux 3), pulsed low → 50 ms → high.  **Measured (report 238):** the core answers `HCI_Reset`, reports `hci ver 6` / `lmp ver 6` / `mfr 0x002b` (TI, BT 4.0), and **runs a full BR/EDR inquiry that finds a peer** (`f0:a3:b2:bb:d1:00`, class `0x011c00`), reproducibly.  **The reference's `WL1283.bts` loads 315/315** and moves LMP subversion `0x2b0f` → `0x2b33`; loaded **before** the attach it makes `hci0` **register with mgmt** (0 → 1 controller, negative control both ways) by fixing the `0x201c` reply length.  **Left:** opening the controller — a stray `0x30` byte arrives ~200 ms after the last init reply and the kernel's H4 receiver correctly rejects it as not-an-H4-type (`-EILSEQ` → `Frame reassembly failed (-84)` → `SET_POWERED` `0x05`); the instrumented `h4_recv()` shows it in 6 of 6 runs, but **which side emits it is open** (it never appears from userspace).  Then ship `l2cap`/`sco`/`rfcomm`/`hidp` (not in region 1 yet) and a stack.  **LE is refused by the controller**, though the chip reports BT 4.0 with the LE-supported bit — no known sequence, and impossibility not demonstrated.  The pack survives `HCI_Reset` but not a `BT_EN` power cycle.  Tools in `rootfs/bt/`.  Reports 216/237/238, traps 248–252, DECISIONS §72/§73 |
| **GPIO1 pads / board lines** | **fixed (session 49): three pads were driving GPMC alternate functions** | `0x4A100050` (`gpmc_ad0/1`) and `0x4A100074` (`gpmc_ncs0/1`) were left at mode 0, so pad logic drove GPMC bus lines on a board with no GPMC device while gpiolib could not touch them (trap 228); `0x4A31E058` (GPIO1_31) was unmuxed with its input buffer off.  The DTS now sets mode 3 + pull-down and hogs GPIO1 0/18/28/31 low, so `GPIO1_OE = 0x6FFB1FBA` against the reference's `0x6FFB1FAA` (the missing bit 4 is pad `0x058`'s low half = GPIO2_4, owned by `bridge_pins`, trap 229) and `GPIO1_DATAOUT` is identical.  Image `0x0114FFD3`.  **Not** the WL1283 blocker (card still silent).  Report 220 |
| **Munin touch (CY8CTMA3)** | **FIXED — the position ghost and the lost burst taps are gone (session 58, kernel `#74`)** | I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` boots the app and maps the 12-bit report through the 1917 `scaling.conf` calib (raw x 172→1108 → 102→922, y 110→658 → 60→540) to 1024×600; IRQ gpio 35 = GPIO2_3 pad `0x056` = `0x410B`, edge-falling, `irq 110` (~174/s idle).  The controller asserts its line for the whole gesture and reuses slots, so the driver re-reads a torn report until the counter parity matches, **keeps polling while the line is asserted (and for 200 ms after the last contact)**, and **holds a new down back one sample when it repeats the slot's previous coordinates within 300 ms**; `irq-gpios` gives it the line level (traps 296/298).  Contacts are separate and fast typing is correct (report 248).  The backspace/enter repeat was squeekboard's, now fixed by wvkbd (trap 299); power/reset GPIO1_14/GPIO4_8 are not driven.  `pb-touchwake.py` wakes a manually blanked panel; auto-blank removed (session 59 — DPMS resume wedges DSI).  Reports 198/247/248/250, DECISIONS §47/§82/§83/§85 |
| **WM8994 audio** | **WORKING — stereo 440 Hz on headphones AND speakers at 48 kHz; diagnostics `dev_dbg` (session 57)** | I²C4 `0x1a` (`0x8994`, revision D); the board machine driver makes `0 [WM8994]: PlayBook_WM8994`.  **Data path (session 46):** the codec's AIF1 is on McBSP3 (the oracle's routing log `AIF 0 Downlink: MM_DL_PORT->MM_EXT_OUT_PORT->MCBSP3`, the live `PCR0 = 0x3`, the PDM pads `0x106/0x108/0x10a/0x10c` = `0x0109/0x0009/0x0109/0x0109`); the card is two ASoC links (`40124000.mcbsp + wm8994-aif1` clocks, `CBC_CFC`; `40126000.mcbsp + wm8994-aif2` data, `CBP_CFP`); the codec is the AIF1 slave, FLL1 locks from our BCLK (`R0x302 = 0x0000`, `R0x732 = 0x6020`, `R0x200 = 0x0011`), no MCLK1; the `omap-mcbsp` channel-layout guard keeps `XCR2 = 0x8041`.  **Speaker (session 47, report 215):** the speakers are on the codec's **line outputs**, so the output stage follows the jack: headphones → HPOUT1 + charge pump (`R0x060 = 0xcc`); jack out → LINEOUT1P/2P (PM3 `0x3cf0`, `R0x01e = 0`, `R0x034/0x035 = 1`, `R0x036 = 0`, `R0x038 = 0x80`, `R0x04c = 0x1f25`, `R0x060 = 0`) with **`spk` (GPIO2_12) driven low** and **GPIO1_15 (the `audio_boost` enable) high**.  **Session 48** removed the `pcm:` ring readback and the `live N:` FIFO dump and re-verified both outputs at DAC1 volume 40: `jack out (speaker)` / `jack in (headphone)`, "done, FLL1 locked", both PCMs `RUNNING`, the full byte count written, `spk` low during the speaker route and high on the headphone route, `boost` high.  Reports 214/215, DECISIONS §59/§60.  **Session 57:** the remaining info-level diagnostics (`omap_mcbsp_dump_reg`, the per-stream `rate/AIF1CLK` and `repair:` lines) are `dev_dbg`, and the PM-QoS remove is guarded, so a failed 48 kHz `hw_params` no longer taints the kernel (trap 294); the 48 kHz rejection itself is open. |
| Userspace (initramfs) | **working, minimal** | `/init` = PID 1, mounts, creates the configfs gadget (**ACM console + RNDIS**, session 48), forks `/console` in the fallback path.  **No auto-reboot since session 15** — PID 1 reboots only on request (the timer spent a `BOOTCTR_FCTRY` unit per iteration).  **Session 28:** after the gadget and the boot-time top-up, `/init` mounts the region-1 ext4 root **read-write**, bind-mounts /dev//proc//sys, chroots and execs `/sbin/init`; with no `/sbin/init` it falls back to the console.  Report 176, session-28 note |
| **USB network + ssh** | **working — commands and interactive sessions (session 48); 2.3 MB/s is the ssh bound, not the USB one** | The gadget carries an **RNDIS** function next to ACM, so the host sees four interfaces and a net device (`enp8s0f3u1i2`, MAC `02:1a:11:00:00:02`); the region-1 `rcS` brings `usb0` up at **`169.254.0.1/16`** and starts **dropbear** listening on `169.254.0.1:22` only.  Key-only auth, host keys shipped, password/PAM/shadow off.  **`tools/pbssh.sh 'cmd'` works**; measured before session 52 at 2.3 MB/s, which is dropbear's cost — the raw path is **6.3 MB/s (host→device) / 4.1 (device→host)** after the session-52 DMA fix, so use raw `nc`/`socat` for bulk copies and ssh for command access.  An **interactive** session works under a real terminal (`script -qec` from this side); an agent's own shell has no tty, so an apparent "closes with no output" is a harness artefact (trap 216).  DECISIONS §62. |
| Userspace (region-1 root) | **working — a real BusyBox userland with ssh, the WLAN stack and the BT stack** | 64 MiB ext4 at raw `0x29000000`; BusyBox 1.36.1 + init + 401 applet links + `pbconsole`/`pbrpmb`/`pbplay`/`pbmix` + `/lib/modules/7.3.3`; boots to `sh -l` on `/dev/ttyGS0` and serves **dropbear on `usb0`**; `insmod`/`modprobe` proven.  Current image: `local/images/rootfs-region1-wl1283-bt.ext4` (sha256 `49e318bf…`, **43 modules** — the sensor/charger/audio set, the wireless stack with **`ccm`/`ctr`**, the seven Bluetooth modules, both wl128x SR firmware files, the per-unit NVS, `iw`, `wpa_*` and the signed `regulatory.db`, session 52).  Earlier: `…-wl1283-final.ext4` (`877c42c3…`, 36 modules), `…-wl1283-assoc.ext4` (`6c2cfb30…`), `…-wl1283-scan.ext4` (`f159a221…`), `…-rndis-ssh.ext4` (`a4e76b18…`) and the plain userland `0xAEDF68D0`. |
| Console | **working** | `help ls cat peek poke kick disp bl i2c i2c16 spi fb kmsg grep blk recv sink blkwr qnx6 rootfs write reboot`; `spi tcon` (low-first) and `spi tconbe` (high-first); `blk info/sum/sumb/rd` is read-only FNV-1a64 over `/dev/mmcblk1` (**`blk info` prints size 0 B — `fstat` bug, see KNOWN-ISSUES**), `grep <substr> <path>` scans any-size text (for `clk_summary`); `sink <len>` measures the link, `blkwr <off> <len> [chunk]` is a timed O_DIRECT write with a deterministic pattern (report 136); `qnx6 [off-hex]` mounts hd3 read-only (report 138); `rootfs [off-hex]` mounts the region-1 ext4 root read-only on `/rootfs` (report 140); argument limit 24.  `pb_reg` op set is now `w a:v` / `r a` / `t a` (16 reads, re-arm) / `u a` (16 reads, no re-arm) / `d from:to` (range dump) / `v` (video liveness).  Every hardware access forks with a bounded wait |
| QNX6 read access (hd3) | **working, read-only** | `CONFIG_QNX6FS_FS=y` + console `qnx6 [off-hex]`: a read-only loop over `/dev/mmcblk1` at raw **`0x49000000`** (superblock `0x49002000`) mounted qnx6 on `/mnt`.  Verified on `0xFC77CD47`; the old `0x48F00000` offset gives `wrong signature (magic)` from the driver.  Report 138 |
| **SGX540 / PowerVR** | **WORKING — catacomb composites on the GPU (session 56)** | The `openpvrsgx-devgroup` 7.3 driver (DDK 1.17.4948957) is in-tree and bound (`pvrsrvkm 56000000.gpu`); `catacomb` 1.5.0 runs with a smithay 0.7.0 GLES2 fallback (`GL_EXT_unpack_subimage` absent → per-row/full-row uploads) and maps `libGLESv2_PVR_MESA`/`libsrv_um`/`libusc`/`pvr_dri.so` (no `swrast`); `squeekboard` composites.  The OSK key-press path still janks (open).  The driver is vendored at `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz`.  Report 246. |
| DSS / DISPC via omapdrm | **working — and the core clock is now right** | `/dev/dri`, `/dev/fb0`, `card0-LVDS-1` connected+enabled, `LINE_STATUS` advancing (37 lines/ms), GFX overlay scanning the painted buffer.  The DSS functional clock, which is the DISPC's **core** clock, was left at 48 MHz by the boot chain while the pixel clock is 50.4 MHz — that was the last display defect.  `dss_probe()` now asks the PER PLL for the reference's rate and gets **96 MHz**.  **Session 18:** the boot-time `pb` bridge-read probes are behind `pb_probe` (default **off**), so a normal boot has **no DSI CIO errors**; the DSS rate-mismatch WARN is gone and the real rate is recorded (report 142).  Report 133 §2/§3 |
| DSI1 link | **byte-identical to the reference, PLL included** | `DSI_CTRL = 0x00EAEF9D`, `CLK_CTRL = 0xA0346005`, `TIMING1 = 0x79190999`, `TIMING2 = 0x7FFF7FFF`, `VM_TIMING1/2/3`, `CLK_TIMING = 0x00000E0B`, `VM_TIMING7/8`, PHY, PLL `CFG1 = 0x14A5E8BE` / `CFG2 = 0x00056000`.  All re-read live in session 15. Reports 127 §4, 132 §6 |
| TC358764 bridge | **programmed; configuration now identical to a working system** | The whole 353-register map was read from **TabletOS with its display working** (`tools/pbdsi`) and diffed against ours.  After fixing `VP_HTIM1/2` (§ below) only four registers differ, three of them live status/counters.  `IDREG = 0x00006500`, `LV_CFG = 1`, `LV_PHY0 = 0x0004C02D`, `VP_HTIM1 = 0x00780001`, `VP_HTIM2 = 0x00760400`, `VP_VTIM1/2`, `VFUEN` self-clears. Report 132 §2/§5/§6 |
| Bridge **reads** | **reliable — with `PPI_TX_RX_TA = 0x00040006`** | The value is latched by `PPI_STARTPPI (0x0104)`, so it must be right **in `pb_bridge_seq`**.  Boot with `4` → 4/8 at boot, 8/16 sampled; boot with `0x00040006` → 8/8 and 16/16.  Live writes after boot change nothing.  The VC re-arm does not affect the rate. Report 132 §3 |
| Backlight (LM3532 @ `0x38`) | **working** | binds, `bl_power = 0`, brightness 255; gate is `GPIO2_16`. Report 120 |
| Panel TCON (MCSPI4) | **working — sent automatically at bring-up** | The 9-word Hitachi table is sent by `/init` right after the display comes up (high byte first, 450 kHz, 100 ms after words 0/2/last), matching the reference's own order (`winch_lcdctl 0x193c4`).  Without it the panel stays black even with the bridge programmed. Report 133 §3/§7 |
| Panel identity | **Hitachi (definitive)** | on TabletOS: `GPIO6_DATAIN = 0x00010800` (bit 16 = 1), slog `LCD is Hitachi`.  Report 127 §1 |
| **Image on the panel** | **WORKING** | With `0x8C146805` (and the same source as `0x068C867F`) the panel shows a clean, stable image: solid white, solid red, and the kernel console as legible text.  The three necessary fixes were the bridge delivery (VC0/HS null packet), the LVDS porches + TCON table at bring-up, and the DISPC core clock. Report 133 |
| QNX oracle tool | **`tools/qnx-dsiread.c` → `tools/pbdsi`** | Reads any TC358764 register over DSI from a TabletOS user process (generic read request `0x24` on hw VC1, BTA, RX FIFO pop; response is a generic long read, DT `0x1A`).  Read-only.  This is how the reference values in this document were obtained. Report 132 §2 |
| Kernel patch + config | **7.3.3 (session 58 export)** | `kernel/patches/0001-playbook-winchester-7.3.3.patch` is **8 981 lines / 64 files**, sha256 `ca17a057…`, 0 rejects / 64/64 identical.  Session 58 fixes the Munin touch position ghost (`playbook-munin.c`: poll while the line is asserted + the fresh-down confirm; the board DTS gains `irq-gpios`; report 248).  Session 54 makes the `PB*` prints `pr_debug` (config `CONFIG_DYNAMIC_DEBUG=y`, `CONFIG_OVERLAY_FS=y`); the rebuilt image is `0xE84693A1` (built, not flashed).  Session 52 adds `drivers/usb/musb/musb_gadget.c` (the `quirk_avoids_skb_reserve` flag) and `drivers/usb/gadget/function/u_ether.c` (the TX realign); the 40 MHz experiment in `wl12xx/main.c` was reverted and is not in the patch.  Session 51 added `wl12xx/main.c` (CAM + the 4-sr firmware name) and `wl12xx.h` (the SR floor 7.3.5.0).  Session 49 added the report-220 board-DTS GPIO1 pad fix and `arch/arm/Kconfig.debug` (missing from every export since session 30 — the tree walk found it, trap 211 class).  Session 48 retires the two diagnostic blocks and adds `sound/soc/ti/omap-mcbsp-priv.h`, which every export since session 41 had omitted although `omap-mcbsp.c` reads the fields it declares (trap 211 — the patch could not compile from pristine until now).  Session 47 added the speaker path (line-output route, `spk`/`boost` GPIOs, the `disp-en-12-hog` removal); session 46 the `&l4_abe` ABE-refclk assignment, the E5 clock-role change, the McBSP3 two-link card and the `omap-mcbsp` channel-layout guard; session 39 the board machine driver (`sound/soc/ti/playbook-wm8994.c`, Kconfig/Makefile), the `wm8994-core.c` runtime-PM quirk and the board `sound` node/codec clocks, and it restored `drivers/input/touchscreen/{Kconfig,Makefile}`, which the 48-file export had silently omitted (trap 154).  Config: `kernel/winchester-7.3.3.config` (includes `CONFIG_CLK_TWL=y`, `CONFIG_CHARGER_ISL9519=y`, `CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`, `CONFIG_KERNEL_XZ=y`, `CONFIG_SND_PCM_OSS=m`, `CONFIG_SND_MIXER_OSS=m`).  The 6.15.11 patch (`5 703 lines / 34 files`) stays as the bisect reference; it is **incomplete** — it omits `cm3217.c`, `leds-fan5702.c`, `pbmon-pair.h`, `pbmon-page.h` and their Kconfig/Makefile lines (trap 106) |
| **ISL9519 charger** | **WORKING under Linux (session 31)** | I²C1 `0x48070000`, slave `0x09`; VendorID `0x0049`, DeviceID `0x0003`.  `isl9519.c` (built-in, DTS `charger@9`) sets the four registers in RIM's order with the masked fields and refreshes every 5 s.  Measured **+358 → +397 mA**, 3.775 → 3.794 V, capacity 8 → 9 %, `Charging`; still on at 197 s (watchdog).  Registers read back `0x14=0x0280`, `0x15=0x1070`, `0x3E=0x0D00`, `0x3F=0x0180`.  Report 183 |
| TWL6030 BCI | **probes after the session-31 fix; NOT the board's charger** | `twl6030_charger` probes with the `simple-battery` profile and sets `CONTROLLER_CTRL1=0x10`, `VOREG=0x21`, `VICHRG=0x08`, `CINLIMIT=0x09`, but the stock OS leaves the whole block at bootloader reset values — it charges through the ISL9519.  The BCI node/modules are inert; `twl6030_usb` reports `ONLINE=1`, VBUS 4.81–4.91 V |

## Current images

The unit table below is authoritative for what is on media.  Keep the current
image, one known-good fallback, and anything an open contradiction still needs;
the durable record is the CRC and what it demonstrated, not the file.

| image | base | role |
|---|---|---|
| `0x294F8E36` | 7.3.3 | **current region 0 — read-write root + SSH + grown FS** (session 58: `0x9E0980` B, `boot/chainloader/kernel.img` sha256 `f0738625…`, kernel `#77`: mounts hd3 rw, installs dropbear, `barrier=0` + write-through, one-shot `resize2fs`) |
| `0xED58EF36` | 7.3.3 | previous region 0 — the touch position-ghost fix (session 58: `0x967CE0` B, sha256 `c67b75c5…`, kernel `#74`) |
| `0xB9DD91E3` | 7.3.3 | previous region 0 — the touch poll-on-touch fix (session 57: `0x967398` B, `boot/chainloader/kernel.img` sha256 `5ac47496…`, kernel `#73`) |
| `0x23D88CBD` | 7.3.3 | session-57 experiment — both-edge IRQ (no interrupts, touch dead; trap 297; `0x967800` B) |
| `0x373674A4` | 7.3.3 | session-57 experiment — the `PBTS` trace kernel (`0x967AA8` B) |
| `0xA544A731` | 7.3.3 | session-57 first touch fix — the parity re-read (`0x967CB0` B) |
| `0xAB899C49` | 7.3.3 | previous region 0 — the overlay init with `&sgx_module` and the Munin Y fix (session 56: `0x9676A0` B, sha256 `c37ee2fe…`, kernel `#69`; `local/images/kernel-0xAB899C49-touch.img`) |
| `0xA8F0F30C` | 7.3.3 | previous region 0 — the overlay init with `&sgx_module` (session 55: `0x967880` B, sha256 `fb9905f3…`, kernel `#68`; `local/images/kernel-0xA8F0F30C-sgx.img`) |
| `0xE85217E5` | 7.3.3 | previous region 0 — the overlay init without SGX (session 55: `0x9662A4` B, sha256 `fbeb6df0…`; kernel `#67`; `local/images/kernel-0xE85217E5-overlay.img`) |
| `0xE84693A1` | 7.3.3 | **built, NOT flashed** (session 54: the PB prints `pr_debug`/dyndbg, `PBREQ`/`PBIRQ` behind `pb_trace`; `CONFIG_DYNAMIC_DEBUG=y`, `CONFIG_OVERLAY_FS=y`; `0x967A94` B, sha256 `663348d0…`; `local/images/kernel-0xE84693A1-quietpb.img`) |
| `0x060B25F2` | 7.3.3 | previous region 0 (session 54: `ROOTFS_RAW_OFF=0x49000000`, the `lstat` hand-off, read-write root; `0x939D04` B, sha256 `b548ec6c…`) |
| `0x8B709950` | 7.3.3 | session-54 intermediate (`ROOTFS_RAW_OFF=0x49000000` with the old `access()` hand-off; `0x939D5C` B, sha256 `835fa863…`) |
| `0xDD66D8C3` (readback) | 7.3.3 | the session-52 image, read back before the hd3 switch and CRC-verified (`local/images/kernel-0xDD66D8C3-readback.img`, sha256 `06e61587…`) |
| `0xDD66D8C3` | 7.3.3 | session 52 (the UART2 **CTS/RTS pads** — the BT core answers HCI; `0x939E4C` B) — no longer current region 0 |
| `0x50CD2A48` | 7.3.3 | session-52 (the musb HSDMA fix — `quirk_avoids_skb_reserve` + the u_ether TX realign; `0x939E3C` B) |
| `0x5C37F181` | 7.3.3 | session-52 intermediate (the TX realign, with the one-shot `PBDMA` diagnostic still in) |
| `0x5A7F15D8` | 7.3.3 | session-52 intermediate (the RX quirk + the diagnostic) |
| `0x9712A086` | 7.3.3 | previous region 0 (session 50: the CLK32KG fix + the `ti,wl1283` child, `cap-power-off-card` and the pwrseq; kernel `#60`) |
| `0xF64A680D` | 7.3.3 | session-50 intermediate (`+cap-power-off-card`; the second firmware boot still failed) |
| `0x53F7F97B` | 7.3.3 | session-50 intermediate (`+ti,wl1283` child) |
| `0x69CDAC94` | 7.3.3 | session-50 first fix (`CONFIG_CLK_TWL=y` + `clk32kg` enable; the card enumerates) |
| `0x17C2E119` | 7.3.3 | previous region 0 (session 50: the session-49 tree rebuilt after the reverted experiments; kernel `#59`) |
| `0x91E22899` | 7.3.3 | session-50 experiment (the slow-identification-clock test: `f_min=100000` + the archived `PBX`/`PBIRQ`/`PBCMD` trace; kernel `#58`) |
| `0xBAFD9B3B` | 7.3.3 | previous region 0 (session 49: the session-48 tree + report 220's GPIO1 pad fix; kernel `#57`) |
| `0x0114FFD3` | 7.3.3 | session-49 experiment (the GPIO1 pad fix, kernel `#51`) |
| `0x71A79419` | 7.3.3 | session-49 intermediate (the session-48 tree rebuilt after the WL1283 experiments were reverted; kernel `#50`) |
| `0x12C8C320` | 7.3.3 | previous region 0 (session 48: the RNDIS + dropbear initramfs; kernel `#45`) |
| `0xAD85DF0F` | 7.3.3 | previous region 0 (session 47: the speaker path — line outputs, `spk`/`boost` GPIOs, `disp-en-12-hog` removed; kernel `#44`) |
| `0xF8BADFF3` | 7.3.3 | previous region 0 (session 46: McBSP3 two-link card, kernel `#42`) |
| `0xE5E2AEAF` | 7.3.3 | session-47 intermediate (jack-aware speaker state; `spk` still a hog) |
| `0x47ED0450` | 7.3.3 | session-46 experiment: mcbsp1 as the data engine (still stalled; pads input-enabled) |
| `0x58F85337` | 7.3.3 | previous region 0 (session 45: the committed E5 tree; `#40`) |
| `0x5A04ACD9` | 7.3.3 | session-45 E5 restore (`#35`) |
| `0xE5A13DE7`, `0x6AD9E204` | 7.3.3 | session-45 E9 two-link experiments (data port stalls — §14) |
| `0x92458D00` | 7.3.3 | session-45 E5 + the disabled `&mcbsp1` node (experiment, not on media) |
| `0x3DC4508D` | 7.3.3 | session-45 E6 experiment (McBSP1 as the port; codec clock lost) |
| `0xA66B40EF` | 7.3.3 | session-45 E7/E8 (two PCMs; raw clock pokes crashed the SoC — see trap 193) |
| `0x7A5DD941` | 7.3.3 | session-45 E5 as first built (McBSP bit/frame provider, pads back to outputs) |
| `0xB43F169B` | 7.3.3 | previous region 0 (session 44: reference codec-GPIO init — boot-on `audio-ldo` + pulse, hp input) |
| `0x7C56BDC0` | 7.3.3 | previous region 0 (session 44: `CM_ABE_PLL_REF_CLKSEL` = SYS_CLK + DPLL_ABE 98.304 MHz; E1 clock roles) |
| `0x36597040` | 7.3.3 | previous region 0 (session 42: E1 clock roles — codec AIF1 master, McBSP consumer, input pads) |
| `0xB8830701` | 7.3.3 | previous region 0 (session 41: AUXCLK1 DTS fix, McBSP dump) |
| `0xE36C679A` | 7.3.3 | known-good region 0 (WDT2 + ISL9519 charger) |
| `0x0F6AC47F` | 7.3.3 | last full-regression image (sensors, CM3217) |
| `0x0D1DA0D3` | 6.15.11 | clean pre-userland fallback |
| `0x8C146805` | 6.15.11 | first image with a correct display |

Full image history and the older per-image notes are in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md).

Do not boot `0x5E40C5C1` or `0xB9A1BC4F` (two lanes), or `0x3B04298E`
(LM3532 without VAUX3).

## The unit

| item | value |
|---|---|
| working units | **one: the reference (`0x50175127`)**, armed AND sealed.  The spare (`0x50176EC1`, BSN `1063043093`) is **down** with the 1-2-2 blink = error `0x5B` (report 153) and presents no USB.  Two 32 GB units are bricked and parked |
| region-0 IFS on media | **the session-58 read-write/SSH image `0x294F8E36`** (`0x9E0980` B, `boot/chainloader/kernel.img` sha256 `f0738625…`, kernel `7.3.3 #77`, the read-write root, the dropbear/SSH install, `barrier=0` + write-through and the one-shot `resize2fs`), region 0.  The way back is the 1917 IFS (`local/work/ifs-shim-sealed.ifs`, 7,900,548 B, sha256 `b52195bb…`); the compressed `local/work/ifs1917-compressed.ifs` (sha256 `f14db207…`) is the alternative.  Earlier images (the `0xED58EF36`/`0xB9DD91E3` touch, `0xAB899C49` compositor, `0xE85217E5` overlay, `0x060B25F2`, `0xDD66D8C3`, …) remain on the host.  Earlier Linux images (region 0 only, block `0x825`): `0x23D88CBD`, `0x373674A4`, `0xA544A731`, `0xAB899C49`, `0xA8F0F30C`, `0xE85217E5`, `0xE84693A1`, `0x8B709950`, `0x50CD2A48`, `0x5C37F181`, `0x5A7F15D8`, `0x9712A086`, `0xF64A680D`, `0x53F7F97B`, `0x69CDAC94`, `0x17C2E119`, `0xBAFD9B3B`, `0x91E22899`, `0x12C8C320`, `0xAD85DF0F`, `0xF8BADFF3`, `0x36597040`, `0xB43F169B`, `0xB8830701`, `0xF97CD9C7`, `0x3B2ECFED`, `0x50842767`, `0x82FC0D35`, the Task-3 `0x0DC36A11`, `0x05819994`, the known-good `0xE36C679A`, and the touch `0x04AF1433` |
| region-0 RCFS (`/base`) | **now the full 1917 RCFS at `0xC00000`** (`local/work/rcfs-1917.rcfs`, the modified one with the `/accounts/devuser/startup.sh` hook, sha256 `c4ab6e34…`): session 49 wrote it to recover root on a stock-TabletOS boot, and it sits **outside** the 7.3.3 test image (which ends at `0x9E0980`).  The pruned oracle RCFS is no longer on media — re-flash `local/work/minimal/setB.signed` for the minimal oracle (trap 227).  Not readable as `/base` without the 1917 IFS |
| UFS (`/`) | **the 1917 vendor `.5.ufs` with `autorun::0`** (the session-27 set); the BB10 `.0.4.ufs` is not on media.  (Not re-hashed this session) |
| `/accounts/devuser/startup.sh` | the 1917 UFS carries the operator's script; it last ran during the session-35 S3/S2 TabletOS bench and topped `BOOTCTR_FCTRY` back to `0x32` |
| `BOOTCTR_FCTRY` | **`0x27`** at the session-59 wrap-up (this boot read `27000000` = `0x27`, above the `0x1e` threshold so `/init` did not write; the session's reboots drained it from `0x2d`).  Block `0x3a` record@196.  **Read it on Linux with `dmesg \| grep pbrpmb`** (the top-up line carries the 4-byte LE record).  Self-replenishing: `/init` tops up at <`0x1e`, a TabletOS boot writes `0x32` (`SAFETY.md` §2, `DEVELOPMENT.md` §5) |
| region-1 Linux root | **present** at raw `0x29000000` (region-1 +16 MiB, block `0x2915`): 64 MiB ext4, no journal, sha256 **`49e318bf…`** (`local/images/rootfs-region1-wl1283-bt.ext4`, session 52: the WLAN fixes, **`ccm`/`ctr`**, the per-unit NVS, the CAM+4-sr `wl12xx.ko` and the seven Bluetooth modules), **43 modules under `/lib/modules/7.3.3`** (the sensor/charger/audio set plus **`cfg80211`, `mac80211`, `wlcore`, `wlcore_sdio`, `wl12xx`, `libarc4`, `crc7`** and **`bluetooth`, `hci_uart`, `btbcm`, `crc-ccitt`, `ecdh_generic`, `ecc`, `kpp`**), `pbplay`, `pbmix`, **dropbear**, **`/usr/bin/iw`**, **`/usr/sbin/wpa_{supplicant,cli,passphrase}`**, and **`/lib/firmware/ti-connectivity/{wl128x-fw-4-sr.bin,wl128x-fw-5-sr.bin,wl128x-nvs.bin}`** plus the signed **`/lib/firmware/regulatory.db`/`.p7s`**; `rcS` loads the wireless stack and `bluetooth hci_uart`.  Earlier images: `…-wl1283-final.ext4` (`877c42c3…`), `…-wl1283-assoc.ext4` (`6c2cfb30…`), `a148c362…`, `7e067560…`, `88084b78…`, `a4e76b18…` and `0342ab06…`.  The modules in any of them can be checked by extracting them with `debugfs` (trap 163).  **Session 55: not booted.**  The region-0 image is the hd3-pointing `0xA8F0F30C`; it mounts hd3 (Nura) and never touches region 1.  This root remains the fallback; its `rcS`/dropbear/WLAN/BT are unchanged. |
| TabletOS / OS | **NOT running** — the unit is on the **Nura root from hd3** (image `0x294F8E36`, `0fca:8010`, console `playbook:~#` on the USB ACM, **sshd: dropbear on `169.254.0.1:22`**, `PB_LINK=usb tools/pbssh.sh`; the root is the hd3 ext4 mounted **read-write**, grown to 56.5 G).  TabletOS 2.1.0.1917 returns by flashing `local/work/ifs-shim-sealed.ifs` at block `0x825`; its door is also `169.254.0.1`, so use `PB_LINK=door` for `tools/pbssh.sh` there |
| hd3 (MCT `0x1C`, FS at loader block `0x4915` / raw `0x49000000`) | **the media is the Nura root, grown in place to 56.5 G and mounted read-write** (session 58; the one-shot `resize2fs` ran on the clean `f7858110…` image and the initramfs installed the dropbear/SSH kit).  It is **no longer byte-identical to `f7858110…`**; that 625 999 872-byte image is the **flasher-restorable way back** (`--component --dest-block 0x4915`).  On top of the session-56 compositor image it adds `tabula` 1.0.2, `epitaph` 1.4.0, `tzompantli` 1.2.0, `gorm` 1.2.1, `swayidle` 1.9.1, `foot` 1.28.0, `grim` 1.5.0, `slurp` 1.5.0, `tremor` 1.1.0, `netsurf` 3.11, `xournalpp` 1.3.8; the rebuilt `snd-soc-omap-mcbsp.ko` / `snd-soc-playbook-wm8994.ko`; `gsettings-desktop-schemas` 51.0-r0 with a recompiled `gschemas.compiled`; and the no-suspend `/etc/catacomb/initrc`.  It still carries the session-54 overlay root + `/lib/modules/7.3.3/kernel/pvrsrvkm_omap4_sgx540_120.ko` + the pvrports stack (`libglvnd`, `mesa-pvr-dri-classic`, `sgx-ddk-um{,-ti443x,-openrc}`, `gcompat`, `wayland-libs-server`), with the module in `/etc/modules` and `modules.dep`, the `sgx-ddk-um` service in the default runlevel, and `MESA_LOADER_DRIVER_OVERRIDE=pvr` in a tinydm env file.  Mainline mesa 26.2.6 / llvm23 / spirv-tools were purged by the replacement.  The base apps image is `blackberry-playbook-root-apps.img` (`711cd5ec…`); the pre-pvr images (`1fcf0cb0…`, `1f14915f…`) and the pristine `076bafcf…` remain on the host.  The QNX userland is no longer on media; the 1917 IFS + vendor UFS are the way back.  Session 59 added wvkbd/DejaVu/pb-touchwake and the icon-theme fix to the **live** root and removed swayidle auto-blank; a flasher-restorable baked base **`local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img`** (`07555bde…`) exists (not flashed).  See reports 249/250 |
| device-side files | **persistent on hd3 (session 58: the root is writable and grown):** the initramfs installs `/usr/sbin/dropbear`, `/etc/dropbear/*`, `/root/.ssh/authorized_keys`, `/etc/local.d/pb-ssh.start` and `/etc/local.d/pb-grow.start` on first boot, and `/etc/pb-grown` marks the grow.  `/var`, `/home` and `/tmp` are now real directories on the 56.5 G FS, so the DE state and the SSH kit survive a reboot (the state files written before the switch are on the grown FS).  `memdump3`/`memw32`/`pbi2c` are outside the tree: push them (paced) after a reflash (trap 177).  The first-Nura-boot log is `docs/captures/playbook-live-2026-11-15-hd3-nura-boot.txt`.  Session 57's touch tools (`evcap.py`, `keylog.py`, `muninraw`) lived only in `/tmp`; the measured evidence is preserved in the captures.  **Session 59 deployed:** `wvkbd-mobintl` 0.21 (`/usr/sbin/wvkbd-mobintl`), `/usr/share/fonts/dejavu/DejaVuSans{, -Bold}.ttf`, `/usr/bin/pb-touchwake`, `/usr/share/icons/default/index.theme` (`Inherits=Adwaita,hicolor`), `adwaita-icon-theme` + `glycin-svg`, a hicolor netsurf PNG, the edited `/etc/catacomb/initrc` (no swayidle, wvkbd + pb-touchwake), and `playbook` in the `input` group (gid 23).  Tools/daemon sources are tracked in `tools/bench/`; the pmaport edits are under gitignored `local/work/nura/pmaports/`. |

The factory boot budget is **self-maintaining in bench use**: every successful
factory/CRC boot spends one unit, and the booted OS puts it back — our Linux
`/init` runs `pbrpmb topup 0x1E`, and every TabletOS boot tops the record to
`0x32`.  **Since session 15 our images no longer reboot themselves**, so a test
boot costs one unit, is replenished at boot, and stays up until asked; it is not
a reason to avoid an experiment.  The hard hazards remain: `0` and `> 0x32` do
not fall back to the product path — they hard-lock the unit (`SAFETY.md` §2) —
and a failed/half-formed write or a flat pack is still a brick risk.

## Blockers in play

**Session 60 — the open blockers are:** (1) the remaining **UI performance** —
the client's softpipe rasterizer was replaced with llvmpipe (report 253), so a
slider draw is now 2.7–6 ms instead of 1.6–2.3 s; what is left is llvmpipe's
deferred `swap` (420–550 ms per full-screen drawer frame), its first-draw LLVM
JIT, and above all the **CPU: one core at 400 MHz** — the kernel is built
`# CONFIG_SMP is not set`, the DTS adds `maxcpus=1`, and there is no
cpufreq/OPP (traps 312/313); (2) **Kumo** needs a WPEWebKit/WebKit-fork build
(not packaged, report 247 §4); (3) the **intermittent WRITE-busy stall** — not
a cache flush (report 250 §1), mitigated by `barrier=0` + write-through.
**Closed:** the **`epitaph` panel** (two client bugs fixed in r10005 —
report 251; trap 292); the panel's **permissions** (group `catacomb` + the
udev/polkit rules — brightness and the Wi-Fi/cellular toggles); the touch
position ghost and lost burst taps (kernel `#74`, report 248); the
backspace/enter repeat (wvkbd on catacomb 1.5.0, report 250 §2); the
app-drawer icons (report 250 §2b); the eMMC cache item (closed by measurement,
report 250 §1).  **Bench note:** DPMS wake-by-touch works with pb-touchwake,
but repeated DPMS resume wedges the DSI link (black panel — only a reboot
clears it), so auto-blank was removed and the panel stays lit (report 250
§2a/§2b, traps 289/298/301).

**The audio blocker is closed, speakers included, and the diagnostics are
retired (sessions 47/48, reports 214/215).**
`pbplay`'s 440 Hz tone is audible in both ears on headphones and in both
speakers at 48 kHz ±1 % on the working image (`0xBAFD9B3B` + rootfs
`a4e76b18…`), re-verified after the cleanup at DAC1 volume 40.  The data path
is session 46's (AIF1 on **McBSP3**; two links `mcbsp2 + wm8994-aif1` clocks,
`mcbsp3 + wm8994-aif2` data; the `omap-mcbsp` channel-layout guard fixed the
right channel; the ABE is not needed).  The speakers needed the codec's **line
outputs** (not class-D), `spk` driven **low**, and GPIO1_15 (the `audio_boost`
enable) **high** — all three in the driver/DTS.  **Remaining audio items: none
required** — the optional **MCLK1 probe** (trap 165) stays optional, and the
`audio_boost` net is behaviourally confirmed but not schematic-confirmed.

**The WL1283 WLAN ASSOCIATES and passes traffic (session 52, report 236).**  The
session-51 stability fixes hold (CAM, 4-sr) and the association blocker is gone:
**`ccm.ko`/`ctr.ko` were missing**, so `ieee80211_key_alloc()` could not allocate
the `ccm(aes)` AEAD and the PTK install failed with `-ENOENT` — which
wpa_supplicant reports as `WRONG_KEY`.  They are now in region 1 and loaded by
`rcS`.  Measured: `wpa_state=COMPLETED`, WPA2-PSK/CCMP, 5785 MHz, −41 dBm,
MCS7/65 Mbit/s, DHCP lease 192.168.0.212, gateway and 1.1.1.1 reachable.  The
per-unit NVS ships; the country must be set (`iw reg set <CC>`) for 5 GHz APs
the world domain marks no-IR (trap 245).  **Wi-Fi is at its hardware ceiling**
(1×1 HT20, 72 Mbit/s; TX 2.9 / RX 4.4 MB/s) — 40 MHz is measured harmful
(trap 247).  **Open (not blocking use):** *why* 5-sr panics on scan (report 235
§3), *why* the ELP wake is not honoured (report 232 §6), and the occasional
first-auth timeout that needs a `wpa_cli scan` nudge.  **Next for the combo
chip:** BT (`BT_EN` = gpio 55, UART2), then FM/GPS (§"What is next").

**The per-unit WLAN calibration is in the QNX `hd5`
partition** (report 197, session 50) — a TI WL128x radio-parameters INI + a
binary blob, and the assigned MAC is in NVRAM `deviceinfo`
(`14:74:11:8f:9c:c3`, patched into the generic NVS).  It is the source for
`wl128x-nvs.bin` **after** enumeration; it was never the `-110`.

**The touch is fixed (session 58).**  I²C2 `0x48072000` = `i2c-1` at
`0x65`, `playbook-munin.c`, `input0`/`event0`; the 1917 `scaling.conf`
calibration maps to 1024×600, the GPIO2_3 edge IRQ is `irq 110`, and the Y
inversion is in (kernel `#69`).  The driver re-reads a torn report until the
counter parity matches, keeps sampling while the controller asserts its IRQ
line (plus a 200 ms grace period) and, on a new down that repeats its slot's
previous coordinates within 300 ms, waits one sample and reports the fresh
position (kernel `#74`, report 248).  Contacts are separate and the fast-tap
ghost is gone.  The remaining backspace/enter repeat is squeekboard's, not the
driver (trap 299).  The power/reset lines are still not driven by the driver;
only cold boots need the GPIO pulse.

**Media state: region 0 = `0xED58EF36`** (kernel `7.3.3 #74`, `0x967CE0` B,
`boot/chainloader/kernel.img` sha256 `c67b75c5…`, the touch position-ghost fix);
region 1 = the WLAN+BT rootfs **`49e318bf…`** (67,108,864 B, **43 modules** +
both `wl128x` SR firmware files + the CAM/4-sr `wl12xx.ko` + the per-unit NVS +
`iw`/`wpa_supplicant`/`wpa_cli`/`passphrase` + the signed `regulatory.db`;
`local/images/rootfs-region1-wl1283-bt.ext4`); hd3 = the Nura app+audio image
**`f7858110…`** (625,999,872 B), the read-only lower.  The unit is on Linux,
not TabletOS.  The way back to TabletOS is the 1917 IFS
(`local/work/ifs-shim-sealed.ifs`, block `0x825`); the previous Linux images
`0xB9DD91E3`, `0xA544A731`, `0x373674A4`, `0x23D88CBD`, `0xAB899C49`,
`0xA8F0F30C`, `0xF64A680D`, `0x53F7F97B`, `0x69CDAC94`, `0x17C2E119`,
`0xBAFD9B3B`, `0x91E22899`, `0xAD85DF0F`, `0xF8BADFF3`, `0xE5E2AEAF`,
`0x58F85337`, `0x47ED0450`, `0x5A04ACD9`, `0x92458D00`, `0x7A5DD941`,
`0x36597040`, `0xB8830701`, `0xF97CD9C7`, `0x3B2ECFED`, `0x0DC36A11`,
`0xE36C679A` and `0x04AF1433` remain.
NVRAM and the MCT remain unwritable, and an agent never writes RPMB
(`SAFETY.md` §6).

## What is next

0. **THE ACTIVE ITEM — the CPU: cpu1, then the MPU OPP (session 60, report
   253, ROADMAP 7a).**  The UI latency was the client's software rasterizer
   and the CPU: the unit runs **one core at 400 MHz** (`maxcpus=1` in the DTS;
   `dpll_mpu_ck = 400 MHz`; no cpufreq/OPP), while the reference runs 1.0 GHz
   dual-core and programs `DPLL_MPU`/`PRM_VC` (report 223).  llvmpipe (report
   253) fixed the 2 s draw (render 2.7–6 ms), but the remaining 420–550 ms per
   full-screen frame, and all CPU work, will improve with the cores and the
   OPP.
   * **First experiment (one variable):** build **`CONFIG_SMP=y`**
     (`CONFIG_NR_CPUS=2`) and keep `maxcpus=1`.  The vermagic gains `SMP`, so
     rebuild **all** modules (in-tree + the vendored `pvrsrvkm_omap4_sgx540_120.ko`)
     and install them in `/lib/modules/7.3.3` **before** flashing region 0.
     The first boot proves the SMP kernel still comes up on one CPU.  Then a
     second build removes `maxcpus=1` from `omap4-winchester.dts:88` and boots
     the second core.  Verify `online=0-1`, dmesg, WDT2, thermal, a llvmpipe
     drag.  A cpu1 hang is a revert with dmesg captured (trap 313).
   * **Then the MPU OPP:** measure the reference's `DPLL_MPU`/`PRM_VC` on the
     oracle, then add an OPP table + `vdd_mpu`/ABB (or a boot-time write) and
     thermal-check 1.0 GHz (trap 312).
   * **Then:** shrink/pre-warm the drawer (the swap) and pursue a hardware GL
     path for Wayland clients (trap 314).  Remove the r10006 draw timing and
     the `PBGL_PROFILE` env before the final image.
   * **Durable:** the llvmpipe files live on the live root only; the
     flasher-restorable hd3 image must be rebaked with them
     (`swrast_dri.so` + `libLLVM-13.so` + `libdrm_amdgpu`/`nouveau`) and with
     `epitaph` r10005+ and the udev/polkit/gorm rules.  The softpipe backup is
     `/usr/lib/xorg/modules/dri/swrast_dri.so.amber`.
   * **Apps / Wi-Fi are up:** `nmcli radio wifi on` (the radio was disabled)
     gives `wlan0` a scan; `gorm` with `10-gorm.rules` lists ~10 APs (joining
     an AP is untried), and a cleanly launched `tzompantli` runs apps (an
     injected tap on Foot ran `foot`).  Traps 306–308.
   * the **export is current**: 64 files / 8 981 lines (`ca17a057…`) with the
     pvr driver vendored; re-run the copy step on the next kernel change.
   * **Build speed:** the armv7 package builds under qemu-user (`--no-cross`)
     and abuild wipes `$srcdir`; the local APKBUILD now sets a persistent
     `CARGO_TARGET_DIR`.  The native `--cross` path is blocked by a
     host/chroot rustc version mismatch — matching them is the real speedup.
1. **WL1283 — the WLAN ASSOCIATES (session 52, report 236).**  Session 51 fixed
   stability (CAM) and the scan (4-sr); session 52 found the association
   blocker — the **missing `ccm`/`ctr` modules** — and the link now reaches
   `COMPLETED`, takes a DHCP lease and passes traffic.  The per-unit NVS is
   shipped.  **Open, none blocking use:** *why* 5-sr panics on scan (235 §3),
   *why* the ELP wake is not honoured (232 §6), and the occasional first-auth
   timeout (restart the supplicant or nudge a scan).
   **Do not** re-run the session-49/50 negatives, do **not** use
   `IRQ_TYPE_LEVEL_LOW` for WL_IRQ (trap 238), do **not** use
   `wlcore_sdio dump=1` to measure a timing fault (trap 239), do **not**
   switch back to 5-sr (trap 243), and do **not** advertise HT40 (trap 247).
1a. **WL1283 BT — the core WORKS; the controller open is parked (session 53,
    report 238).**  Parked by the operator to move to item 7a; the blocker is
    located, so this is resumable in one experiment.
    (a) **What is proven:** the core answers `HCI_Reset`, reports itself
    (`hci ver 6` / `lmp ver 6` / `mfr 0x002b` = TI, BT 4.0) and **runs a full
    BR/EDR inquiry that finds a peer**, reproducibly.  The reference's
    `WL1283.bts` loads **315/315** and moves LMP subversion `0x2b0f` → `0x2b33`;
    loaded **before** the attach it makes `hci0` **register with mgmt** (0 → 1
    controller, negative control both ways).
    (b) **The one experiment when resumed:** find who emits the stray `0x30` that
    breaks the kernel's H4 receiver.  `MGMT_OP_SET_POWERED` returns `0x05`
    (`-ETIMEDOUT`) after `Frame reassembly failed (-84)` (`-EILSEQ`); an
    instrumented `h4_recv()` shows a single `0x30` arriving ~200 ms after the
    last complete init reply, in 6 of 6 runs.  It never appears from userspace.
    Swap the instrument in with `rmmod btbcm` **then** `hci_uart` (trap 251) and
    log the driver's *writes* too, so a host-side echo is visible.
    Excluded by measurement: alignment/padding, chip idle-sleep, leftover bytes,
    UART line errors (`LSR = 0x60`), truncated long replies, and hardware flow
    control (identical A/B on a clean boot).
    (c) **Then** power, an inquiry through the stack, and HID.  **Ship
    `l2cap`/`sco`/`rfcomm`/`hidp` first** — they are modules and none are in
    region 1 yet.
    (d) **LE is refused by the controller** (`0x0c`, `Write_LE_Host_Support` =
    `0x11`, LE feature page zero), but the chip reports BT 4.0 with the
    LE-supported bit and the reference stack being BR/EDR-only reflects the
    vendor's choices, not the silicon — so no enable sequence is known, and
    impossibility is **not** demonstrated.  **The `wl128x-fw-4-sr/5-sr.bin` files
    are WLAN firmware, not BT** — do not swap them expecting a BT change.
    `BT_EN` = gpio 55, UART2 = `/dev/ttyS1`; tools in `rootfs/bt/`.
    **Do not** re-run the "BT is silent" probes — answered.
    **FM:** mainline had `drivers/media/radio/wl128x` from 2.6.39 to 6.13 and
    removed it; bringing it back means porting that driver forward and whatever
    transport it needs.
    **GPS:** no mainline driver; the operator has a 2024 LKML patch to
    evaluate, and the reference's own GPS path is the fallback.
2. **The WM8994 audio — DONE and the diagnostics retired (sessions 47/48,
   reports 214/215).**  Stereo 440 Hz at 48 kHz on headphones and speakers,
   re-verified at DAC1 volume 40 after the cleanup.  The only items left are
   optional: the **MCLK1 probe** (trap 165) and identifying what the GPIO1_15
   `audio_boost` enable supplies if a schematic appears.  The ABE is not needed
   (DECISIONS §58/§59).
3. **Touch: DONE (session 38, report 198) but down after a cold boot.**  The
   calibration and the IRQ are in the tree and proven; after a cold boot the
   controller does not boot (`0x65` NAKs) until the power/reset GPIOs
   (GPIO1_14/GPIO4_8) are pulsed, which the driver still does not drive.  A
   warm reset keeps it up (session 42).
4. **The `wlcore@2` child is in the DTS** (`ti,wl1283`, `reg = <2>`, WL_IRQ,
   26 MHz ref/TCXO), the stack probes, and the firmware stays up; the only
   remaining node-level item is the **per-unit `wl128x-nvs.bin`** from the `hd5`
   calibration (report 197), to replace the generic NVS.
5. **The unit is on the Nura overlay root from hd3** (image `0xED58EF36`), with
   the lower read-only and every write in the tmpfs upper; **no sshd** — use the
   `ttyGS0` shell on the USB ACM (`/dev/ttyACM0`, `playbook:~#`; note
   `tools/pbcon.py` expects `pb> ` and does not drive this shell — trap 274).
   `usb0` is `169.254.0.1/16` (RNDIS); the host is `169.254.0.2/16`.  To return
   to TabletOS, flash the 1917 IFS at block `0x825`; the region-1 root and the
   earlier images remain.
6. **On the old region-1 root, use ssh** (`tools/pbssh.sh`); the Nura root has
   no sshd, so the ACM is its software console.  The QNX door uses
   `PB_LINK=door`.

## Workspace location

The workspace moved at the end of session 15 from the old checkout (the root
filesystem) to this repository.  Start sessions in the repository root.
Absolute paths were retargeted in `tools/` (`pbssh.sh`, `door-hold.sh`,
`pb_ring.py`, `pb_events.py`), the shipped kernel configs
(`CONFIG_INITRAMFS_SOURCE`) and the canonical docs; historical reports keep their
original wording.

A **fallback copy of the old tree is deliberately kept at `$HOME/playbook-linux`**
until the operator is happy with the move.  It is stale from the moment of the
move — do not work in it, and do not let anything (`CONFIG_INITRAMFS_SOURCE`,
the door scripts) point at it.

**Public snapshot:** `$HOME/SSDdrive/playbook-linux-public` (remote
`git@github.com:Psyden57/playbook-linux.git`).  The private tree is the source
of truth; publish with `tools/publish_public.sh` (see `REPO-MANAGEMENT.md`).

**Both cross-toolchains are inside the workspace** in the gitignored payload:
`local/toolchains/armv7-eabihf` (Buildroot gcc 14.3.0) and
`local/toolchains/qnx660-master` (QNX SDP 6.6).  `. tools/env.sh` exports
`PATH`/`CROSS_COMPILE`/`QNX660`/`QNX_HOST`/`QNX_TARGET`; `tools/env.sh --check`
validates them and prints the versions.

---
