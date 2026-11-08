# Handoff — session 33 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 33 = Linux-era 27**.  See
`docs/sessions/NUMBERING.md`.

> **Session-32 result: the WL1283 is silent from the first SDIO command, and
> every software-visible SoC-side difference is matched.**  A fresh QNX read
> with WLAN up shows `0x4A31E058 = 0x000B0003`: pad0 is **mux 3 `gpio_wk31`**,
> so `FREF_CLK2_REQ` is **not used**.  The reference's `fref_clk2_out`
> (`AUXCLK1`, 12.8 MHz) was reproduced on Linux; the first test enabled it in
> a `late_initcall` **after** the MMC5 probe, and the corrected in-probe test
> is also negative.  The second opinion's software audit (report 186) shows
> the default Linux attempt is **one 400 kHz try** with **CMD52 → CMD0 → CMD8
> → CMD5(arg 0)**, and the card times out on every command except CMD0.  The
> 24 MHz figure was a deliberate test, not the default; the boot-time WL_EN
> reset pulse is also negative.
>
> **Next: RE the QNX powerman `winchester` preset** (`bin/powerman` +
> `devpm-omap4.so`: what `activity/wifi_driver`, `air_needs_wifi`,
> `wlan_needs_dma`, `ldo_vaux3`/`ldo_vaux1` enable), then the multimeter DC
> checks at the chip — VBAT/VIO/WL_EN/WL_IRQ/SLOW_CLK (Motorola 148603 guide:
> VBAT 3.3 V, VIO 1.8 V, the 26 MHz TCXO from the internal 1.8 V).  Then step
> 5 (audio/touch).  **Do not repeat the `CM_DIV_M5 = 0x329` write** (trap
> 116) or a runtime MMC5 `unbind`/`bind` (trap 120).  `hd3`, BB10 and the
> PlayBook OS image work remain parked.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain — **the panel shows a clean, stable
image** — and a **64 MiB ext4 BusyBox root in region 1** at raw `0x29000000`;
the initramfs mounts it read-write, chroots and execs `/sbin/init`.
Peripherals: I²C4 motion sensors (BMA150/HMC5883L/MPU3050), TMP103, the
BQ27541 gauge, the FAN5702 status LED, the CM3217 light sensor, and **the
ISL9519 charger (new in session 31)**.  WDT2 runs at the reference's 58.6 s
window with a 10 s pet.

**Last known-good image (7.3.3):** region-0 **`0xE36C679A`**
(`local/images/kernel-0xE36C679A-wdt58s.img`, 0x91A748 bytes) + region-1
`local/images/rootfs-region1-7.3.3-charger.ext4` (17 modules).  The previous
image `0xA0B75C43` is the same without the WDT2 change; `0x306E3DA5` is the
battery-profile intermediate.  **Build from `0xE36C679A`'s source tree.**  The
session-32 test image **`0xAF4FC103`** is what is on media at handoff (FREF2
enable + WL_EN reset pulse inside the MMC5 probe, 3.3 V OCR, `PBCMD` trace;
the CMD5 quirk is present but default-off); it boots and charges but is
**not** a known-good baseline (reports 185/186).

**The 6.15.11 backup/reference is intact:** `local/pristine/linux-6.15.11`,
`local/kernel-tree-6.15.11` (byte-exact copy of the old working tree) and
`kernel/patches/0001-playbook-winchester-6.15.11.patch`.  Do not delete any of
them.  The 6.15.11 patch is **incomplete** (trap 106); do not use it for a
clean rebuild.

**The 7.3.3 patch is `kernel/patches/0001-playbook-winchester-7.3.3.patch`** —
**7 257 lines / 47 files** (38 modified + 9 new), sha256 `ad55511d…`, verified
by applying to pristine 7.3.3 with 0 rejects and comparing all 47 files
byte-for-byte.  Config: `kernel/winchester-7.3.3.config` (includes
`CONFIG_CHARGER_ISL9519=y`; `CONFIG_KERNEL_XZ=y` keeps the image under
`0xB00000`).

**Parked by the operator:** the `hd3` cut-over, BB10, and the PlayBook OS image
work.  **The WL1283 (item 9 step 4) is in progress:** the SoC-side comparison
is exhausted (session 32, report 185); the next step is the **electrical
comparison at the chip**.  The `FREF_CLK2_REQ` **input** is closed (trap 117)
and the `fref_clk2_out` output is matched (trap 118).  Step 5 (audio/touch) is
the follow-on.

## 0a. The way back (the unit is on the session-32 test image, the 1917 IFS is not on media)

To return to PlayBook OS 2.1.0.1917:

```bash
nohup python3 tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs \
    --dest-block 0x825 --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
sleep 2; PBCON_TMO=10 ./tools/pbcon.py 'reboot -f'
until grep -qE 'COMPLETE|done|error' /tmp/f.log; do sleep 2; done; tail -5 /tmp/f.log
```

At session-32 handoff the media holds the **test image `0xAF4FC103`**, not the
known-good baseline; to return to the baseline first, flash
`local/images/kernel-0xE36C679A-wdt58s.img` with the same `--component`
command.  `BOOTCTR_FCTRY` reads **`0x2A`** (the session-32 TabletOS oracle boot
topped it to `0x32`, and the eight Linux test boots spent eight).

Region 0 is the **only** region that changed on this trip (the region-1 rootfs
is unchanged), so restoring **region 0 only** is enough — the old "flash both
regions" rule applies only when both were modified (operator correction,
session 31).  The RCFS at region-0 `0xC00000` and the 1917 UFS are still on
media and untouched.  After the boot: `tools/pbssh.sh 'uname -a; mount | grep
base'` and read `/dev/rpmb/BOOTCTR_FCTRY` (expect `0x32` after the TabletOS
top-up).  **Do not restore `local/images/ifs0.sealed.bin`** — it is the
2.0.0.4869 IFS (trap 84).

## 1. The next task: the WL1283 (item 9 step 4) — the electrical comparison

**Session-32 result (report 185).**  The second opinion's request-input
question is **answered**: with WLAN up, QNX leaves `0x4A31E058` pad0 in
**mux 3 `gpio_wk31`** (`0x000B0003`), not mux 1 `fref_clk2_req`.  The only
real SoC-side difference was the SCRM **`AUXCLK1` / `fref_clk2_out`**
generator: QNX `0x4A30A314 = 0x00020100` (SYS_CLK/3 = 12.8 MHz) and CORE pad
`0x4A10019C` mux 0; Linux had both off.  It is now reproduced on Linux
(images `0xF521AB06` / `0x18D49C22`) and **CMD5 still times out**.  The QNX
driver enumerates at **24 MHz** with **CMD5 arg `0x00FFFF00`**; Linux tests at
24 MHz and with that arg also time out (`err=-110`, `ocr=0`).  The BT driver is
not the enable.  **Every software-visible SoC-side difference is matched; the
chip does not respond at all.**

**Read first:** the canonical docs in the kickoff order, then reports **184**
(the second opinion) and **185** (the session-32 result).  The opinion's
`0x4A31E058` mux table is in the local TRM; its ball-AD4 routing is still not
verifiable from the documents, but QNX's mux choice settles that the input is
not used.

**So the next measurement is electrical (report 184 §5 / report 185 §5).**
**One variable per boot.**  Capture a cold boot on both OSes with a logic
analyser/scope:

* `MMC5_CLK`, `MMC5_CMD`, `MMC5_DAT0` — does the chip drive CMD low at CMD5?
* `WL_EN` — assertion timing and level **at the chip**.
* the **26 MHz TCXO** at the chip (`TCXO_Clk = 1` in
  `tiwlan_winchester.ini`; the main clock) and the 32.768 kHz.
* VBAT and VIO at the chip, before and after WL_EN.
* `CLK_REQ`/FREF only if the board routes them (QNX does not select the
  `FREF_CLK2_REQ` mux; the FREF2 output is a 12.8 MHz `AUXCLK1` clock).

Do **not** repeat the `CM_DIV_M5 = 0x329` write (trap 116) or a runtime MMC5
`unbind`/`bind` (trap 120).  The reference's WLAN works, so the TCXO/power path
exists on the board; the question is whether it runs under Linux.  **If the
traces are identical and valid but CMD5 still differs,** investigate OMAP
HSMMC sampling and the initialization protocol.  **If a TCXO/rail is
missing,** fix that and re-test with one variable.

### 1.1 What is already eliminated (do not re-run)

* The FREF **request** input: QNX does not use it — `0x4A31E058 = 0x000B0003`,
  pad0 mux 3 `gpio_wk31` with the input buffer off (session 32).
* The FREF **output**: `AUXCLK1`/`fref_clk2_out` matched exactly
  (`0x00020100`, 12.8 MHz); no effect on CMD5.
* 400 kHz vs 24 MHz; CMD5 arg 0 vs `0x00FFFF00`; a 12 MHz init clock.
* WL_EN level, at-power-up, 5 s settle, the 0/500 ms/1 reset pulse,
  `ti,no-reset-on-init`.
* Pads, IRQ pin, every TWL rail, `CLK32KG`, OCR windows (1.8 V and 3.3 V), a
  full power cycle, the GPIO4 outputs.
* The BT driver's GPIO2_23 `BT_EN` (low in both captures) and GPIO4_28
  force-RTS (high in both).
* Report 180 §4/§6 and report 185 for the evidence.  **A runtime MMC5
  `unbind`/`bind` is not a safe rescan** (trap 120).

### 1.2 The clock-source distinction (settled)

* **The vendor `.ini` says `TCXO_Clk = 1` = 26 MHz (the main clock) and
  `STRFRefClock = 2` = 38.4 MHz wired-OR CLK_REQ (the sync clock).**
* `wl128x_boot_clk()` (`drivers/net/wireless/ti/wl12xx/main.c`) selects the
  TCXO unless the hardware reports FREF or the TCXO is 16.368/32.736 MHz; the
  selection runs during firmware init, **after SDIO enumeration**.  FREF is
  therefore not required before CMD5.
* The QNX driver does not configure or gate any TCXO/FREF; the board must
  supply the 26 MHz.  **Whether that oscillator runs under Linux is the open
  measurement** — it is not visible in any PMIC/SoC register already compared.

### 1.3 SDIO protocol and init frequency — audited and negative

* The audit (report 186, image `0x9471F6C9`) shows the default Linux attempt
  is **one try at 400 kHz**: `f_min = f_init = ios.clock = 400000`,
  `SYSCTL.CLKD = 120` (48 MHz / 120; the TRM ratio is direct), actual
  **400 kHz**.  The sequence is **CMD52 (`0x00000c00`/`0x80000c08`) → CMD0 →
  CMD8 (`0x1aa`) → CMD5(arg 0)**; every command times out (`err=-110`)
  except CMD0.  The card is silent from the first command.  The 24 MHz line in
  report 185 was the deliberate `init_hz` test, not the default; report 184's
  "no CMD52 before CMD5" note is corrected.
* Static RE: QNX runs CMD0→CMD5(`0x00FFFF00`)→CMD3→CMD7 at **24 MHz**, with no
  400 kHz phase.
* A runtime `init_hz = 24000000` gave `SYSCTL = 0x000e0087` (CLKD = 2, exactly
  QNX) and still `attach_sdio no`; the rebind wedged the console (trap 120).
* A host quirk with CMD5 arg `0x00FFFF00`
  (`MMC_CAP2_SDIO_OCR_PROBE_FFFF00`, now `omap_hsmmc.sdio_ocr_quirk`, default
  off) and a 3.3 V dummy OCR window both still time out.  Do not re-litigate
  this path (trap 119).

### 1.4 The next experiment: the QNX powerman `winchester` preset

This is the one remaining software layer that can turn on a WLAN rail or clock
outside the driver we already audited.  **Inputs (all local, read-only):**

* `local/work/rcfs-1917-mnt/bin/powerman` (QNX ARM, 1,109,116 B);
* `local/work/rcfs-1917-mnt/lib/dll/devpm-omap4.so` (the `-c` plugin);
* `local/work/rcfs-1917-mnt/etc/system/config/winchester.pm`
  (`use_preset_config winchester`; the dependencies are compiled into the
  binary);
* `local/work/rcfs-1917-mnt/scripts/startup.sh` (the `powerman` line) and
  `scripts/wifi.sh` (`devnp-ti1283.so -o nvram,firmdir=/var/ti1283`).

**Questions:** what does the `winchester_environment` preset map
`activity/wifi_driver/active`, `air_needs_wifi`, `wlan_needs_DMA` and
`WLAN_NEEDS_PREVENT_OSWR` to?  What are `ldo_vaux3`/`ldo_vaux1` for, and does
`devpm-omap4.so` implement them as TWL6030 I2C writes (which register/value)?
Is there any other `ti128`/`wlan`/`wifi` device name?

**Method:** `ntoarmv7-objdump -d -C` + `strings -a -t x` on the two binaries
(call the toolchain binary directly; do not source any `tools/` script).  The
preset is data-driven: find the `winchester_environment` table and the string
addresses for the authorities, then the devpm device names.

**Decision table:**

| result | next |
|---|---|
| the preset names a rail/clock/GPIO for `wifi_driver` | read that register on QNX and Linux; if Linux differs, enable it as the one variable and re-test CMD52 |
| the preset only prevents OSWR/CPU sleep (no WLAN rail) | close the powerman lead; the chip's TCXO/VIO is the remaining suspect → §1.5 |
| `devpm-omap4.so` names a TWL6030/CM register | compare it live on QNX vs Linux (TWL via `pbi2c`, CM via `pbmem3`) |
| no WLAN-specific dependency | close the lead; go to §1.5 |

### 1.5 The multimeter checks (no analyser)

The operator has a multimeter, not a logic analyser/scope, so the waveform
comparison is out.  Check at the chip (or the nearest accessible test point):

* VBAT ≈ **3.3 V**, VIO ≈ **1.8 V** (Motorola 148603 guide: VBAT 3.0–3.6 V,
  VIO 1.71–1.89 V).
* WL_EN ≈ 1.8 V when enabled (active high, default pull-down).
* WL_IRQ level (active low; our SoC read is 0).
* SLOW_CLK DC average ≈ 0.9 V if the 32.768 kHz is running; 0 or 1.8 V if
  stuck.
* The 26 MHz TCXO is powered from the chip's internal 1.8 V; if VIO is
  missing the TCXO is dead with it.  A multimeter cannot check its frequency,
  only the supply/bias.

The **boot-time WL_EN 0/500 ms/1 reset pulse** was tested (image
`0xAF4FC103`, in `omap_hsmmc_probe()` before `mmc_add_host()`) and is
negative.  A PMIC register read proves programming, not the voltage at the
chip.

### 1.6 Stop condition

* The request pin is **eliminated** (QNX does not select it; session 32).
* If the electrical traces are identical and valid but CMD5 still differs,
  investigate OMAP HSMMC sampling and the initialization protocol.
* Do not make speculative clock changes that affect the DSS (trap 116).
* **Delivered in session 32:** the first measurable difference is the FREF2
  output generator (`AUXCLK1` 12.8 MHz + pad `0x19C`), now matched and not
  causal.  The next deliverable is the electrical difference at the chip.

## 1a. Tools that make this cheap

* **`tools/pbcon.py`** — the USB console (the only way into our Linux image;
  there is **no door**, trap 88).  `PBCON_TMO=30 ./tools/pbcon.py 'cmd'`.
  Reboot with `tools/pbcon.py 'reboot -f'`.
* **`tools/pbssh.sh`** (the door; **root** ssh) and **`tools/door-hold.sh`** —
  only on TabletOS.  Start the door with `setsid`, never `pgrep -af door-hold`.
* **`tools/build_kernel_image.py`** — wraps zImage + DTB into the sealed IFS
  container; prints the CRC; the image must end below **`0xB00000`**.
* **`tools/bbusb.py`** — the flasher: `pack -l both.lst -v 1,2 -s -f` +
  `flash --signed --from-bootrom --password 0000 --apply`; one component with
  `--component FILE --dest-block N`.
* **`tools/build_rootfs.py`** — the region-1 ext4; `--krel 7.3.3 --modules DIR`.
* **`tools/pbi2c`** — the QNX-side I²C tool, now with `rdw`/`dumpw` for 16-bit
  SMBus words (the ISL9519/charger work).  Push it to `/tmp` on TabletOS.
* `tools/env.sh` — both cross-toolchains; `tools/env.sh --check`.

## 1b. The follow-on: item 9 step 5 (audio + touch)

Report **176** is the inventory and bus map; DECISIONS §28 is the order.
**One variable per boot.**

**WM8994 audio.**  MFD + codec on **I²C4 `0x48350000`** (Linux `i2c-3`),
7-bit **`0x1a`**; the codec node exists but `CONFIG_MFD_WM8994` /
`CONFIG_SND_SOC_WM8994` are **not enabled**.  The 1917 driver
(`deva-ctrl-omap4abe-wm8994.so`) is McBSP + **ABE** (`mcbsp=[0..4]`, protocol
default i2s, ABE firmware in `/base/lib/libabe.so`).  The likely Linux path is
to bypass the ABE with `simple-audio-card` + McBSP; the blocker is that McBSP
lives in the disabled `&l4_abe` segment (WD_TIMER3 hangs on enable), so enable
the segment with the non-audio targets disabled.  The McBSP instance is
probably **2** (the parser's default table) — confirm it.  First steps: enable
the two symbols, probe the codec ID, then the machine link.  Speakers, the
3.5 mm jack and the mics are all behind this codec.

**Cypress CY8CTMA3 touch (Gen3).**  Mainline has Gen2 `cyttsp_core` and Gen5
`cyttsp5`, not this part.  RIM's stack: `libmtouch-munin.so.1` /
`libmtouch-pegasus.so`, `muninfw`, daemon `/base/usr/bin/touch`, firmware
`/base/lib/firmware/munin/{Pegasus_50,Pegasus2_50,Pegasus_D4_A}.hex`.  1917
facts: slave **`0x65`**, 400 kHz; **GPIO4_8 = reset** (active low),
**GPIO1_14** pulsed low 500 ms then high (power/enable).  **The bus is
unresolved:** I²C2 `0x48072000` (QNX saw `0x65` there) vs I²C3 `0x48060000`
(the library's `/dev/i2c2`); see `docs/contradictions/touch-bus-i2c2-vs-i2c3.md`.
First bench step: `i2cdetect -y -r 2` and `-r 3` on the Linux image, never run
in sessions 30/31.  Then decide between a new driver and the Gen2 core with a
Gen3 protocol layer.

## 2. Build and flash (7.3.3)

```bash
REPO=$PWD
. tools/env.sh
K=local/kernel-tree
# 1. userspace images — rebuild EVERY one whose .c you touched:
arm-linux-gcc -static -O2 -Wall -o initramfs/root/console initramfs/console.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/init    initramfs/init.c
arm-linux-gcc -static -O2 -Wall -o initramfs/root/pbrpmb  initramfs/rpmb.c
# 2. kernel + DTB + modules (rm the cpio or the old initramfs is reused)
rm -f $K/usr/initramfs_data.cpio*
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb
make -C $K ARCH=arm CROSS_COMPILE=arm-linux- -j12 modules
# 3. wrap (prints the CRC; writes boot/chainloader/kernel.img)
ZIMAGE=$K/arch/arm/boot/zImage \
DTB=$K/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb \
python3 tools/build_kernel_image.py
```

**The printed CRC must differ from the previous build** (trap 39), and the
printed `image 0x…` must be **< 0xB00000**.  Check `CONFIG_KERNEL_XZ=y` if the
image grows past the slot.

**Rootfs and both-region flash:**

```bash
python3 tools/build_rootfs.py --size-mb 64 \
  --out local/images/rootfs-region1-7.3.3.ext4 \
  --busybox local/work/src/busybox-1.36.1/busybox \
  --busybox-links local/work/src/busybox-1.36.1/busybox.links \
  --modules local/work/modules-7.3.3 --krel 7.3.3
printf 'boot/chainloader/kernel.img=16,0\nlocal/images/rootfs-region1-7.3.3.ext4=8448,0\n' \
    > /tmp/both.lst
python3 tools/bbusb.py pack -l /tmp/both.lst -v 1,2 -s -f -c /tmp/both.signed
python3 tools/bbusb.py flash --signed /tmp/both.signed --from-bootrom \
        --password 0000 --apply
```

**From a running image**, start the flasher immediately before
`tools/pbcon.py 'reboot -f'`; from **power-off**, start the flasher first and
power on — the bootrom enumerates briefly as `0fca:0001`.  The window is 60 s
(DECISIONS §29).  Run the USB tools as the **user**, never `pkexec` (trap 73).
After every boot: `pkexec chmod 666 /dev/ttyACM0` (the operator must answer).

**Flashing only region 0** when only the kernel image changed (the usual
case now) is enough: `flash --component boot/chainloader/kernel.img
--dest-block 0x825`.  Flash both only when both regions were modified.

## 3. Traps

Full text: `KNOWN-ISSUES.md` §2.  The ones that end sessions:

9. A write to a DSS target in module standby never completes; let `omapdrm` own the DSS.
16. Blocking I²C is the same hazard — fork with a bounded wait.
17. A stale door holder looks like `Connection refused`; kill by explicit pid.
24. `/dev/ttyACM0` is recreated per enumeration (`pkexec chmod 666` after every boot).
26. `pmic-twl6030-ctl set_state vaux1 off` takes the unit down; read rails.
27. A flash cycle's precondition is **media running** (`0fca:8010`), not the ramloader.
28. Panel-chain writes outlive a warm reset (`SYS_RST <- 0x1f`); a cold battery pull clears them.
31. Non-burst DSI video mode is rejected at this pixel clock.
33. Our images no longer auto-reboot; a boot costs one counter unit.
39. A failed link leaves the previous image with the previous CRC.
43. `local/kernel-tree/.config` can point at a different initramfs tree.
45. The DISPC core clock is raised at probe time (96 MHz).
46. Reading DSS/DSI registers from userspace perturbs the picture.
47. MCT/loader block numbers are not raw offsets: `(block-0x15)*0x10000`.
49. eMMC writes are only as large as the writer makes them (≥64 KiB).
51. The ACM console is not a bulk transport.
54. A smaller hd3 filesystem is reformatted and reseeded at boot.
59. **Never `lseek()`+`read()` `/proc/<pid>/as`** — it reset the unit.
60–63. RPMB: the block is reversed; the 224-byte block is monitor-encrypted;
    the monitor reads identity addresses below `0xB0000000`.  **No agent writes RPMB.**
80. The loader's factory count is the first diagnostic after a flash.
82. The bootloader's code addresses in reports 150/151 are `0x9000` too high.
83. The bootloader wipes its own RPMB state before the OS jump.
84. `local/images/ifs0.sealed.bin` is the 2.0.0.4869 IFS — wrong for 1917.
85–90. RPMB keys/transform history (reports 143–175).
91–96. The sensor bus/I²C4 (trap 91), the loop-root `EACCES` (92), shutdown noise
    (93), the `=m` module closure (94), the CM3217 protocol history (95), the
    FAN5702 EN pad (96).
97–98. The WL1283 is on MMC5; the card never answers CMD5 (report 180).
99. A GPIO read one bank past GPIO6 aborts; with `panic=10` it reset-loops.
100. Never read another module's registers from `omap_hsmmc_set_ios()`.
101. A QNX `SIGBUS` on MMCHS5 is block-specific; only io-pkt can read the SDIO host.
102. Synchronise a probe with the operator action it observes.
103. Do not characterise a device on TabletOS if a QNX driver owns it.
104. The CM3217 needs no settle delay and must be left idle.
105. **Your own knowledge is out of date — look external facts up and cite them.**
106. **The old patch-export recipe re-emits only the files the previous patch
     listed** — every new file is silently dropped.  Build the list from the
     tree, apply the patch to pristine, and compare.
107. **`cm3217_remove()` needs `i2c_set_clientdata()`** or unbinding oopses.
108. **A nearly empty battery warm-resets on its own — check the gauge first.**
     The charger now works, so the pack can stay charged on USB.
109. **Subagents share this session's shell — keep them off the bench.**  Give
     them file-only tasks and never a `tools/` script.  The old log-audit
     requirement is dropped (the session-30 reset was low power).
110. **Do not reverse-engineer PlayBook hardware from BB10 images.**  OS 2.x only.
111. **The TWL6030 charger's `monitored-battery` must point at a
     `simple-battery` profile, not the fuel gauge** (`-ENODEV` otherwise).
112. **`twl6030_usb = Charging` does not prove the pack is charging — read the
     gauge.**  (And the TWL BCI is not this board's charger.)
113. **The PlayBook's charger is the ISL9519 at I²C1 0x09**, not the TWL BCI.
     VendorID `0xFE=0x0049`, DeviceID `0xFF=0x0003`; registers `0x14` charge
     current, `0x15` max system voltage, `0x3E` min system voltage, `0x3F`
     input current; field steps 128 mA / 16 mV / 256 mV / 128 mA.  Host-port
     input limit 384 mA on 1917 — below the system load, which is why 1917
     looks like it is not charging on a PC port.
114. **The ISL9519 needs non-zero `0x14`/`0x15` and a refresh every ≤175 s.**
     The driver writes all four in RIM's order and refreshes every 5 s.
115. **WDT2's load is writable only while stopped via `WSPR`**; poll `WWPS`
     after every WDT write (fixed delays drop the sequence).  The kernel now
     uses 58.6 s and pets every 10 s; the expected dmesg line is missing on
     7.3.3 — verify by `WLDR`/`WCRR`.
116. **`CM_DIV_M5_DPLL_PER` is the DSS clock divider — never write the
     reference's `0x329`.**  It glitches the panel; `0x204` restores it.  This
     is why the FREF route is closed.
117. **The WL1283 `FREF_CLK2_REQ` input is not used** — QNX leaves
     `0x4A31E058 = 0x000B0003` (pad0 mux 3 `gpio_wk31`, input off).
118. **The FREF2 output is SCRM `AUXCLK1` (`0x4A30A314`), not the M5
     divider.**  QNX = `0x00020100` (12.8 MHz); matching it does not fix CMD5.
119. **QNX enumerates at 24 MHz with CMD5 arg `0x00FFFF00`**; Linux's
     400 kHz/arg 0 is not the difference (both tested; `err=-110`, no
     response).
120. **A runtime MMC5 `unbind`/`bind` wedges the ttyGS0 shell** — use a boot
     image, not a rescan.
121. **The WL1283 main clock is the 26 MHz TCXO; FREF is not needed before
     enumeration.**  The chip's TCXO/power/reset is the remaining unknown.
122. **Never retry `pkexec` after a failure on this host** — faillock can lock
     the host for 10 minutes; ask the operator.

## 4. Where things are in this workspace

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console commands | `docs/project/DEVELOPMENT.md` |
| host prerequisites, toolchains | `docs/project/SETUP.md` |
| board DTS and patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + exact config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| charger driver | `local/kernel-tree/drivers/power/supply/isl9519.c` |
| WDT2 policy | `local/kernel-tree/drivers/irqchip/irq-gic.c` |
| chainloader source | `boot/chainloader/chainloader_kernel.S.in` |
| tools | `tools/` (see `tools/README.md`) |
| images, sealed IFS, backups | `local/images/` (gitignored) |
| 6.15.11 reference tree | `local/kernel-tree-6.15.11/` |
| OMAP4430 TRM (text) | `hardware/swpu231ap.pdf.txt` (untracked/local) |
| public/private repositories | `docs/project/REPO-MANAGEMENT.md` |

## 5. Working rules

* **After every console command, dump the log tail and read it.**
* **Write the durable fact into the repository in the same turn.**
* **Do not budget work against a context window**; the operator decides when to
  stop.
* **Static RE only for subagents** (one process owns the USB link).  Give them
  file-only tasks and never let them run a `tools/` script; skeleton first,
  exact paths, an explicit "stop when answered".  The old log-audit
  requirement is dropped (the session-30 reset was low power, not an agent).
* **Never present a guess as a fact.**  External facts are looked up and cited
  (DECISIONS §32, trap 105); device facts prefer a fresh read over an old note.
* **One variable per boot**; the whole-map diff is the highest-yield technique.
* **Update the docs as you go**; `STATE.md` and `KNOWN-ISSUES.md` go stale first.
* **Restore the sealed IFS at the end of every session** unless the operator
  ends on our image.  Session 31 ended on our image (charging); region 0 is the
  only region that needs restoring.

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 33 (absolute; Linux-era 27)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md,
KERNEL-PORT.md (especially §7), ROADMAP.md item 9 (steps 4 and 5),
KNOWN-ISSUES.md (traps 9-122, especially 105-122), DEVELOPMENT.md sections
3/3a/4, DECISIONS.md (17-39), reports 176, 180/181/182, 183, 184, 185 and
186, sessions/session-31.md and session-32.md.

WHERE IT STANDS: the patch set is on Linux 7.3.3 and the unit boots it.
The known-good image is region-0 0xE36C679A
(local/images/kernel-0xE36C679A-wdt58s.img) + region-1
local/images/rootfs-region1-7.3.3-charger.ext4 (17 modules).  The media
currently holds the session-32 test image 0xAF4FC103 (FREF enable + WL_EN
reset pulse inside the MMC5 probe + 3.3 V OCR + PBCMD trace; the CMD5 quirk
is default-off); it boots and charges but is not a known-good baseline.  The charger is an Intersil ISL9519 at I2C1 0x09 (report 183);
WDT2 runs the reference's 58.6 s window with a 10 s pet (trap 115).  The
6.15.11 tree/tarball/patch are intact as the backup.  The 7.3.3 patch is
7 257 lines / 47 files (sha256 ad55511d...) and reproduces the tree
byte-for-byte; config kernel/winchester-7.3.3.config
(CONFIG_CHARGER_ISL9519=y, CONFIG_KERNEL_XZ=y).  The session-32 source
changes (FREF enable + WL_EN pulse in the MMC5 probe, CMD5 quirk + sdio.c/
host.h, DTS) are in the patch.

PHYSICAL STATE: THE SESSION-32 TEST IMAGE 0xAF4FC103 IS IN REGION 0 — region
1 is the 7.3.3 charger rootfs; the 1917 IFS is NOT on media.
BOOTCTR_FCTRY read 0x2A on the final boot; the pack is Full (100 %, 4.149 V).
Restore the known-good image or TabletOS by flashing region 0 only:
tools/bbusb.py flash --component <image> --dest-block 0x825 --from-bootrom
--password 0000 --apply, then re-read /dev/rpmb/BOOTCTR_FCTRY.  Do NOT
restore local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

NEXT WORK: the WL1283 chip-side checks (ROADMAP item 9 step 4).  FIRST, RE
the QNX powerman `winchester` preset (local/work/rcfs-1917-mnt/bin/powerman +
lib/dll/devpm-omap4.so; winchester.pm says `use_preset_config winchester`;
strings `activity/wifi_driver`, `air_needs_wifi`, `wlan_needs_dma`,
`ldo_vaux3`, `ldo_vaux1`) for a WLAN rail/clock.  Then the SoC-side
comparison is exhausted: the reference does not use FREF_CLK2_REQ
(0x4A31E058 = 0x000B0003, pad0 gpio_wk31), the corrected in-probe FREF2 test
(AUXCLK1 = 0x00020100, 12.8 MHz, pad 0x4A10019C mux 0) is negative, and the
audit (report 186) shows the default Linux attempt is one 400 kHz try with
CMD52 -> CMD0 -> CMD8 -> CMD5(arg 0); the card times out on every command
except CMD0 (err=-110).  The boot-time WL_EN 0/500 ms/1 reset pulse is also
negative.  With the multimeter, check at the chip: VBAT ~3.3 V, VIO ~1.8 V,
WL_EN ~1.8 V, WL_IRQ, SLOW_CLK DC average; the 26 MHz TCXO is powered from
the chip's internal 1.8 V.  Do NOT repeat the CM_DIV_M5=0x329 write (trap
116) or a runtime MMC5 unbind/bind (trap 120).
Step 5 (WM8994 audio + CY8CTMA3 touch, report 176) follows.
hd3/BB10/PlayBook OS work remain parked.

HARDWARE RE RULE: OS 2.x only (live 1917 oracle, the 1917 /base QNX6 image,
the 1917 IFS tree, the 4869 IFS dump).  Never use BB10 images for PlayBook
hardware facts (DECISIONS §34, trap 110).  Ask the reference OS what it maps
(pidin mem / pidin irq) before writing a DT node (DECISIONS 30); do NOT
characterise a device on TabletOS if a QNX driver owns it (DECISIONS 31,
trap 103).

An agent NEVER writes RPMB (SAFETY.md section 6); the CRNG needs `entropy
<hex>` before any write (trap 87).

BENCH: door = 169.254.0.1 (the host link already carries 169.254.0.2/16 —
do not re-add it).  tools/pbssh.sh logs in as ROOT.  Start
tools/door-hold.sh with setsid (never `pgrep -af door-hold` or `pkill -f
door-hold`).  After a cold TabletOS boot the qconn handshake can take 1-2
minutes; wait for "[+] Authenticated. Key uploaded." before SSH.  On our
Linux image there is NO door: use tools/pbcon.py (the shell prompt is
`pb> `), and reboot with tools/pbcon.py 'reboot -f'.  `pkexec chmod 666
/dev/ttyACM0` after EVERY boot and the operator must answer the prompt.  If
it fails, DO NOT retry: the host's faillock can lock for 10 minutes; ask
the operator to run the chmod.  Run the USB/flash tools as the USER — never
pkexec.  The device password is 0000, sent once, never retried.  Rebuild the
QNX oracle tools after `. tools/env.sh`: pbmem3 = `ntoarmv7-gcc -O2 -o
/tmp/pbmem3 tools/memdump3.c`, and /tmp is wiped on reboot.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only
when only it changed; flash both regions in one bootrom session only when
both changed.  Operator's call when to stop; the operator also decides when
to wrap up.

SUBAGENTS: static RE only, no bench — they share your shell.  Give them
file-only tasks and never let them run a `tools/` script; skeleton first,
exact paths, an explicit "stop when answered".  The old log-audit
requirement is dropped: the session-30 reset was low power, not an agent
(traps 108/109).  Check the battery before diagnosing any reset (trap 108).

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.
```
