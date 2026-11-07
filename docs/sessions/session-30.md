# Session 30 (absolute) — the base moves to 7.3.3, and the charging gap

> Numbering: absolute `30`; the reports call this Linux-era **session 24**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-07.
Unit state at the end: **our Linux 7.3.3 image `0x0F6AC47F` in region 0** with
the region-1 root `rootfs-region1-7.3.3.ext4` (`0b3b83e8…`, 15 modules); the
1917 IFS is **not** on media.  `BOOTCTR_FCTRY` read **`0x2D`** at the last
boot.  **The unit is powered off and docked on the rapid charger**: the pack
read **2.817 V, 0 %, `Critical`, Discharging** before it shut down, so there is
no console at session end.

## Goal of this session

`HANDOFF.md` §1 / ROADMAP item 10: **port the patch set to Linux 7.3.3**,
target `linux-postmarketos-stable-7.3.3-r0`, keeping 6.15.11 as the
backup/reference.  **Done and closed.**  The second half was a full peripheral
regression on the new base, which exposed a missing TWL6030 charger and a power
brownout.

## Confirmed (measured, read in a file, or seen on the bench)

### Target and trees

* **Current pmaports still excludes armhf.**  `git clone --filter=blob:none
  --depth=1 https://gitlab.postmarketos.org/postMarketOS/pmaports` (HEAD
  `d58ca1a2`, 2026-11-07) and `git show
  HEAD:device/main/linux-postmarketos-stable/APKBUILD`: `pkgver=7.3.3`,
  `pkgrel=0`, **`arch="all !armhf !s390x"`**.  The base is therefore upstream
  7.3.3, not the package.  (`KERNEL-PORT.md` §0a.)
* **`config-stable.armv7` exists** in that tree: 12 234 lines, sha256
  `dd07f6ad…`; it has `OMAP2_DSS_DSI` **off** and `DRM_OMAP=m`.
* **kernel.org is current:** `releases.json` on 2026-11-07 says stable
  `7.3.3`, released 2026-11-06, mainline `7.4-rc1`.
* **The tarball:** `local/pristine/linux-7.3.3.tar.xz`, sha256 `79c66dde…`,
  fetched from `cdn.kernel.org` (the package builds `linux-7.3.tar.xz` +
  `patch-7.3.3.patch.xz`; ours is the rolled-up release).
* **The 6.15.11 working tree was copied byte-exactly** to
  `local/kernel-tree-6.15.11` (`diff -rq`: 0 differences) before
  `local/kernel-tree` was replaced with the 7.3.3 tree.

### Build and config

* **The 6.15.11 patch applied to 7.3.3 with 7 rejects in 6 files**, all
  rebased: the DTS Makefile (upstream inserted espresso7/10), `tc358764.c`
  h1 (`mod_devicetable.h` gone) and h7 (`drm_bridge_attach(encoder, …)`),
  `omap_hsmmc.c` h14 (`return ret` instead of `goto err1`), `do_mounts.c` h3
  (the `out:` label is gone), and `drivers/misc/Makefile` (append).
* **The 6.15.11 patch was incomplete:** it omitted
  `drivers/iio/light/cm3217.c`, `drivers/leds/leds-fan5702.c`,
  `drivers/misc/pbmon-pair.h`, `drivers/misc/pbmon-page.h` and their
  Kconfig/Makefile lines.  The build stopped on `pbmon-pair.h`; all were
  copied from the 6.15.11 tree.  (Trap 106.)
* **Config:** pmOS `config-stable.armv7` seeded, `winchester.config` merged
  over it, `olddefconfig`.  The merge is load-bearing: it forces
  `OMAP2_DSS_DSI=y` and `DRM_OMAP=y`.  Saved as
  `kernel/winchester-7.3.3.config` (sha256 `7e0aa261…`) and
  `kernel/patches/winchester-7.3.3.config`.
* **gzip does not fit:** image `0xB8ABB4` against the `0xB00000` IFS-slot
  bound.  `CONFIG_KERNEL_XZ=y` cut the zImage from 9 124 816 B to
  **6 568 696 B**; final image **0x91AB20**, CRC **`0x0F6AC47F`** (build
  output of `tools/build_kernel_image.py`).
* **Patch export:** `kernel/patches/0001-playbook-winchester-7.3.3.patch`,
  currently **6 648 lines / 42 files** (34 modified + 8 new), sha256
  `726b953b…`.  Generated from the verified 41-file list plus
  `arch/arm/boot/dts/ti/omap/twl6030.dtsi` (the charger prep), applied to
  pristine 7.3.3 targets with **0 rejects**, and **42/42 files compare
  byte-identical** to `local/kernel-tree`.

### Boots, panel and storage

* **Three planned 7.3.3 boots plus one power-reset boot.**
  * `0x83E1A570` — first 7.3.3 image: to the region-1 userland, panel clean,
    eMMC/root rw.  The CM3217 was absent because the patch's DTS snapshot
    predated the cm3217 node (patch exported 07:24; node added 09:23).
  * rootfs-only flash — 15 modules loaded; a hand-instantiated CM3217 read
    **32 lux**.
  * `0x0F6AC47F` + rootfs `0b3b83e8…` — final tested image: `3-0010` probes
    from DT, IIO `cm3217`, **26–30 lux**.
  * post-reset boot — same image, `BOOTCTR_FCTRY = 0x2D`.
* **`uname` is 7.3.3**; the root is `/dev/loop0` ext4 rw
  (`local/logs/boot-20261107-7.3.3-final.log`, 1068 lines, no WARN/Oops/L3
  errors).
* **The panel is clean on 7.3.3** — operator observation on `0x83E1A570`:
  kernel console text, then solid white after the `dd`-paint, then console
  text redrawn over it.  The final image differs only by the CM3217 node.
  `dmesg`: DSS fck 96 MHz, DSS/DISPC/DSI/TC358764 bound, `Console: switching
  to colour frame buffer device 128x37`.
* **eMMC:** `mmcblk1` = 62 291 968 KiB (59.4 GiB); root ext4 rw; a 4 KiB raw
  read hashed `53f688656d6e91ad5c45b26dca05f641` (host `md5sum` of `dd`).
* **Boot-time top-up:** `pbrpmb topup` read `0x30` (first 7.3.3 boot), `0x2E`
  (final planned boot) and `0x2D` (post-reset), always
  `>= threshold 0x1e, no write`.

### Peripheral regression on 7.3.3 (operator request)

| subsystem | measurement |
|---|---|
| I²C topology | `0-0038` lm3532, `0-0048` twl6030, `0-0055` bq27541, `0-0077` tmp103, `3-0010` cm3217, `3-001e` hmc5883l, `3-0036` fan5702, `3-0038` bma150, `3-0068` mpu3050 — all bound |
| BMA150 | `input0` = `bma150` |
| HMC5883L | `hmc5883l`, raw `-876 / 1075 / -1894` |
| MPU3050 | `mpu3050`, raw `255 / -19 / 0`, `in_temp_raw -12560` |
| CM3217 | `in_illuminance_input` = **30** (26 later) |
| TMP103 | `hwmon0` `tmp103`, `temp1_input = 41000` |
| BQ27541 | capacity **71 %**, 3.153 V, −213 mA, 31.2 °C, cycle 6 (12:39) |
| FAN5702 | operator saw **red → green → blue**; `GENERAL (0x10)` read `0x02` with red on and `0x00` off; `GPIO1_DATAOUT = 0x00006044` (bit 13 high); pad `0x4A1001B0 = 0x00030003` |
| LM3532 backlight | `brightness 64` dimmed and `255` restored (operator) |
| eMMC | as above |
| WDT2 | no reset for >11 min uptime; the later reset was power, not WDT |

### The power brownout (12:50)

* Host USB log: **12:50:17** disconnect (device 25) → **12:50:19** bootrom
  `0fca:0001` (device 26) → **12:50:33** disconnect → **12:50:46** the same
  7.3.3 image `0fca:8010` (device 27).  A warm reset through the bootrom, not
  a flash.
* Post-reset BQ27541: **2.817 V, capacity 0 %, `Critical`, Discharging**,
  temp 31.5 °C — eleven minutes after 3.153 V / 71 %.  TMP103 41 °C.
* The reset spent one counter unit (`0x2E → 0x2D`).
* **Subagent audit:** the two static-RE agents produced only
  `/tmp/commandcode/shellout/2026-11-07T15-45-30…` and `…15-47-54…`, both
  `objdump` output (touch and WM8994).  Neither log contains `pbcon`,
  `ttyACM`, `0fca`, `reboot` or `devmem`; there is no subagent shell call after
  12:47:54.  They did not reset the unit.  (Traps 108/109.)

### The charging gap (found at session end)

* The board TWL6030 node includes `twl6030.dtsi` (regulators, USB comparator,
  GPADC) but has **no `charger` child**.  `CONFIG_CHARGER_TWL6030=m` builds
  `twl6030_charger.ko` that never probes, and the module was not in the
  15-module rootfs closure.  Nothing configures the BCI under Linux.
* **Prepared, not flashed:** a `charger` child under `&twl`
  (`interrupts = <2>, <5>`, `io-channels = <&twl_gpadc 10>` with
  `io-channel-names = "vusb"`, `monitored-battery = <&battery>`); labels on
  `battery@55` and on `gpadc` in `twl6030.dtsi`.  The DTB compiles and the
  node is in the blob (`dtc -I dtb -O dts … | grep twl6030-charger`).
  `twl6030_charger.ko` + `twl6030-gpadc.ko` are in
  `local/work/modules-7.3.3` (17 modules) and `rcS` modprobes both.
* **Candidate images:** image **`0x91AF34DF`**
  (`local/images/kernel-0x91AF34DF-charger.img`, sha256 `7d4b9187…`) +
  rootfs **`ba275807…`**
  (`local/images/rootfs-region1-7.3.3-charger.ext4`).  **Untested.**

### Static RE (OS 2.x only)

* **Audio (1917 `deva-ctrl-omap4abe-wm8994.so`):** the codec is a **WM8994**
  on I²C with 16-bit registers; data path is **McBSP + ABE**
  (`mcbsp_configure_port`, `mcbsp=[0..4]`, `protocol=[i2s|pcm|tdm]` default
  i2s, `clk_mode`, `StereoAudioCodecRequestMcbspClk`/`…ABEClk`, OMAP4 AUXCLK1
  and `abe_clkctrl`).  The ABE firmware is **`/base/lib/libabe.so`** (the
  driver imports `abe_force_reload_fw`); there is no separate file.  The 1917
  `/base` has no `libaudio_config`.  The parser's default table initialises a
  port id with 2 (likely McBSP2, **inferred**).
* **Touch (1917 `libmtouch-munin.so.1` / `libmtouch-pegasus.so`):**
  `muninfw` `.data` gives the default slave **`0x65`** and bus speed **400 kHz**
  (`0x00061a80`).  The library opens QNX `/dev/i2c2`; `winchester_tp_init`
  maps GPIO1 (`0x4A310000`) and GPIO4 (`0x48059000`).  Decoded reset:
  **GPIO4_8 = reset** (CLEARDATAOUT `0x100` → 20 units → SETDATAOUT `0x100`),
  **GPIO1_14** pulsed low for **500 ms** then high in between (power/enable).
  The 1917 `/base` carries
  `/lib/firmware/munin/{Pegasus_50,Pegasus2_50,Pegasus_D4_A}.hex`,
  `/usr/bin/muninfw` and `/usr/bin/touch`.

## Inferred (reasoning from evidence, not directly measured)

* **The port's functional surface survived 6→7.**  Every reject was a context
  move or API rename, and all patched subsystems compiled and ran unchanged
  (display, GIC/WDT2, eMMC, MMC core, GPIO, sysc).  *Strength: strong.*
* **XZ is safe here.**  The kernel decompresses and boots on the bench; no
  compression-related fault in four boots.  *Strength: strong for this image.*
* **The 12:50 reset is a power brownout, not software.**  The pack was at
  0 %/2.817 V and the reset traversed the bootrom to the same image; no panic
  or WDT message survives, but the gauge is the direct evidence.
  *Strength: strong.*
* **The missing BCI is why the pack drained on USB.**  The driver never
  probed and the module was absent; the PMIC can still precharge on its own,
  but nothing configured the charge current/limits.  *Strength: strong.*
* **The touch bus is I²C2 `0x48072000` or I²C3 `0x48060000`.**  The live QNX
  scan saw `0x65` on `/dev/i2c1` (I²C2), while the library opens `/dev/i2c2`
  (I²C3) and bundles `i2c-omap4` code.  *Strength: moderate; unresolved.*
* **A Linux audio path can bypass the ABE DSP:** McBSP + WM8994 with
  `simple-audio-card`.  The blocker is that McBSP lives in the disabled
  `&l4_abe` segment (WD_TIMER3 hangs on enable).  *Strength: moderate.*
* **The WM8994's McBSP is likely #2**, from the parser's default table.
  *Strength: weak; not followed through `mcbsp_initialize`.*
* **GPIO1_14 is the touch power/enable line** (gated flag always set, 500 ms
  pulse).  *Strength: moderate; it could be a second reset.*

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The pmOS package now builds armv7."**  The current APKBUILD still says
  `arch="all !armhf !s390x"`.  Killed by reading HEAD, not the 2026-09-01
  snapshot.
* **"The 6.15.11 patch is complete."**  The export recipe derives its file
  list from the old patch, so it had silently dropped every file added after
  the previous export.  Killed by the build failure on `pbmon-pair.h` and a
  pristine-vs-tree walk (trap 106).
* **"`delete_device` removes the auto-probed CM3217."**  It only removes
  sysfs-created clients; an OF client returns ENOENT.  Killed by the 7.3.3
  test (`Can't find device in list`); driver unbind is the test.
* **"The subagents rebooted the unit."**  Their only shell logs are `objdump`
  dumps with no device strings and no call after 12:47:54; the host USB log
  shows a power-driven warm reset.  Killed by the audit (traps 108/109).
* **"BB10's Winchester startup is evidence for this board."**  Operator
  correction: BB10 targets newer devices; use OS 2.x only.  Concrete
  difference: BB10 has `libaudio_config.so.1`, the 1917 `/base` does not.
  (DECISIONS §34, trap 110.)
* **"The 1917 `.rcfs` is an RCFS container."**  It is a **QNX6 filesystem
  image**; `bb10mt qnx6 mount` is the right tool.
* **"The charger driver is already probing."**  It is not: there is no
  charger node and the module was not in the rootfs.  The config symbol alone
  proves nothing.
* **"`i2cdetect -y 2` identified the touch bus."**  It is inconclusive: the
  kernel lacks SMBus quick-write and skipped `0x30–0x37`/`0x50–0x5f`.
  Read-byte probing (`-r`) is required.
* **"The unit was stable for 11 minutes, so the reset was a kernel problem."**
  The unit later browned out at 0 % battery; the early stability says nothing
  about the pack twelve minutes later.

## Unknown / open questions

* **Does the TWL6030 BCI actually charge?**  *Would settle it:* flash
  `0x91AF34DF` + rootfs `ba275807`, load `twl6030-gpadc`/`twl6030_charger`,
  check for a `twl6030_usb` supply and a positive BQ27541 `CURRENT_NOW` with
  rising voltage.
* **Is GPADC channel 10 the TWL6030 VBUS channel?**  It is the TWL6032 mainline
  choice and the driver's header says channel 10 is internal VBUS; not
  measured on this unit.  *Would settle it:* the charger's `VBUS_VOLTAGE` vs a
  meter.
* **Why did the gauge fall 71 % → 0 % in twelve minutes?**  An aged pack,
  gauge estimate error under load, or a real load spike.  *Would settle it:* a
  controlled discharge with a known load, or a fresh pack.
* **Does the pack charge while the unit is off on the dock?**  *Would settle
  it:* read the gauge after an hour off the dock, or the dock LED.
* **Which McBSP feeds the WM8994?**  The parser default suggests 2; not
  confirmed.  *Would settle it:* follow `omap4_parse_commandline` into
  `mcbsp_initialize`, or enable the three McBSPs and probe the codec's DAI.
* **Can McBSP + WM8994 work without the ABE DSP?**  *Would settle it:* enable
  `&l4_abe` with `wd_timer3`/the timer targets disabled and a
  `simple-audio-card` link; check for a WM8994 ID and a playback stream.
* **Which I²C bus is the touch on?**  `0x65` on I²C2 (live QNX scan) vs
  `/dev/i2c2` = I²C3 (library).  *Would settle it:* `i2cdetect -y -r 2` and
  `-r 3` on the Linux image; see
  `docs/contradictions/touch-bus-i2c2-vs-i2c3.md`.
* **Which line is the touch IRQ?**  `tp_intr_attach` uses a PPS/GPIO path not
  yet decoded.  *Would settle it:* disassemble `tp_intr_attach` and match the
  pad mux.
* **Does the touch answer before its firmware is loaded?**  *Would settle it:*
  a read at `0x65` on both buses before/after a reset pulse.

## Superseded / uncertain

* **"Nothing is ported yet"** (`KERNEL-PORT.md`/`STATE.md` at session start) —
  the port is done and ROADMAP item 10 is closed.
* **"`0xFC32F008` is the current image"** — it is now the **6.15.11
  reference**; the current tested image is `0x0F6AC47F`.
* **"The 7.3.3 patch is 41 files / 6 618 lines"** — the first export was;
  the current export is **42 files / 6 648 lines** because the charger prep
  adds `twl6030.dtsi`.
* **"The 6.15.11 patch reproduces the tree"** — withdrawn: it is incomplete
  (trap 106) and kept for bisect reference only.
* **"The 1917 `.rcfs` is an RCFS image"** — it is a QNX6 filesystem.
* **"The unit is up on the Linux image at session end"** (true mid-session) —
  it is powered off and docked, with the pack critical.
* **The session-30 handoff's "charge before bench work"** remains current; the
  charger fix is prepared but not validated.

## Artefacts produced

* `kernel/patches/0001-playbook-winchester-7.3.3.patch` — 6 648 lines /
  42 files, sha256 `726b953b…`, verified 42/42 byte-identical.
* `kernel/winchester-7.3.3.config` (+
  `kernel/patches/winchester-7.3.3.config`) — sha256 `7e0aa261…`.
* `local/images/kernel-0x0F6AC47F-7.3.3.img` — sha256 `fcddda8d…`;
  `local/images/rootfs-region1-7.3.3.ext4` — sha256 `0b3b83e8…` (15 modules).
* `local/images/kernel-0x91AF34DF-charger.img` — sha256 `7d4b9187…`;
  `local/images/rootfs-region1-7.3.3-charger.ext4` — sha256 `ba275807…`
  (17 modules).  **Untested.**
* `local/work/modules-7.3.3/` — the module closure (15, then 17 with
  `twl6030_charger.ko`/`twl6030-gpadc.ko`); `local/work/modules-7.3.3-stage/`
  — the full `modules_install` tree.
* Source changes in `local/kernel-tree` and `kernel/source/`: `cm3217.c`
  (`i2c_set_clientdata`), `leds-fan5702.c` (fnode put), the board DTS
  (CM3217 node, `battery` label, `charger` child), `twl6030.dtsi`
  (`twl_gpadc` label), `rootfs/etc/init.d/rcS` (charger modprobes).
* Boot logs: `local/logs/boot-20261107-7.3.3-first.log` (1076 lines),
  `local/logs/boot-20261107-7.3.3-final.log` (1068 lines).
* 1917 `/base` extracts: `local/work/qnx-2x-1917/`
  (`deva-ctrl-omap4abe-wm8994.so`, `libmtouch-{pegasus,munin,inject}.so.1`,
  `touch`, `muninfw`, `io-audio`, the three `munin/*.hex`, `acoustic.conf`,
  `sensor_config.xml`).
* Docs: `KERNEL-PORT.md` §7, `STATE.md`, `HANDOFF.md`, `ROADMAP.md`,
  `KNOWN-ISSUES.md` traps 106–110, `DECISIONS.md` §33/§34/§35,
  `kernel/patches/README.md`, this note,
  `docs/contradictions/touch-bus-i2c2-vs-i2c3.md`.
