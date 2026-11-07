# Handoff — session 31 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§5).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+7` — this handoff is for **absolute 31 = Linux-era 25**.  See
`docs/sessions/NUMBERING.md`.

> **Session-30 result: the kernel base is now 7.3.3 and the unit boots it.**
> The whole patch set was ported from 6.15.11 to **upstream Linux 7.3.3**
> (`linux-postmarketos-stable-7.3.3-r0` was the target; the package still
> excludes armhf, so upstream is the base).  The unit boots to the region-1
> BusyBox userland, the **panel is clean** (operator-confirmed: console text →
> solid white → console text), **eMMC and the ext4 root work**, and the
> **CM3217 auto-probes and reads 26–30 lux**.  ROADMAP **item 10 is CLOSED**.
>
> **Two defects were found and fixed, both now traps:** the 6.15.11 patch export
> silently omitted every file added after the previous export (trap 106), and
> `cm3217_remove()` dereferenced NULL because probe never called
> `i2c_set_clientdata()` (trap 107 — deleting the client oopsed and killed the
> shell, which is what wedged the console in session 30).
>
> **Next work: ROADMAP item 9 step 5 — WM8994 audio and the Cypress CY8CTMA3
> touch, on the 7.3.3 base.**  The WL1283 is parked (report 180 §6); `hd3`,
> BB10 and the PlayBook OS image work remain parked by the operator.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain — **the panel shows a clean, stable
image** (DSS/DSI, the TC358764 bridge, the Hitachi TCON table, the LM3532
backlight) — and a **64 MiB ext4 BusyBox root in region 1** at raw
`0x29000000`; the initramfs mounts it read-write, chroots and execs
`/sbin/init`, the unit boots to a shell, and `insmod`/`modprobe` work.
Peripherals: I²C4 motion sensors (BMA150/HMC5883L/MPU3050), TMP103 and the
BQ27541 gauge, the FAN5702 status LED, and the CM3217 light sensor.

**Last known-good image (7.3.3):** region-0 **`0x0F6AC47F`**
(`local/images/kernel-0x0F6AC47F-7.3.3.img`, sha256 `fcddda8d…`, 0x91AB20
bytes) + region-1 `local/images/rootfs-region1-7.3.3.ext4` (sha256
`0b3b83e8…`, 15 modules under `/lib/modules/7.3.3`).  **Build from this.**

**The 6.15.11 backup/reference is intact:** `local/pristine/linux-6.15.11`,
`local/kernel-tree-6.15.11` (byte-exact copy of the old working tree) and
`kernel/patches/0001-playbook-winchester-6.15.11.patch`.  Do not delete any of
them.  Note the 6.15.11 patch is **incomplete** (trap 106) and should not be
used for a clean rebuild.

**The 7.3.3 patch is `kernel/patches/0001-playbook-winchester-7.3.3.patch`** —
6 648 lines / 42 files (34 modified + 8 new), verified by applying to pristine
7.3.3 targets with 0 rejects and comparing all 42 files byte-for-byte.  The
42-file export includes the prepared (untested) TWL6030 charger DTS pieces;
the last tested image is `0x0F6AC47F`.  Config:
`kernel/winchester-7.3.3.config` (sha256 `7e0aa261…`).

**Still open:** the peripheral bring-up (ROADMAP item 9), **step 0** (the
prepared TWL6030 charger test) first, then **step 5** (audio + touch).  **Parked
by the operator:** the WL1283 (report 180), the `hd3` cut-over, BB10, and the
PlayBook OS image work.  Packaging (item 7) is untouched.

## 0a. The way back (the unit is on our Linux image, the 1917 IFS is not on media)

To return to PlayBook OS 2.1.0.1917 from our Linux image:

```bash
nohup python3 tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs \
    --dest-block 0x825 --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
sleep 2; PBCON_TMO=10 ./tools/pbcon.py 'reboot -f'
until grep -qE 'COMPLETE|done|error' /tmp/f.log; do sleep 2; done; tail -5 /tmp/f.log
```

The RCFS at region-0 `0xC00000` and the 1917 UFS are still on media and are not
touched by this write.  After the boot: `tools/pbssh.sh 'uname -a; mount | grep
base'` and `tools/pbssh.sh 'cat /dev/rpmb/BOOTCTR_FCTRY' | xxd -p` (expect
`32000000`; the TabletOS `startup.sh` tops it up).  **Do not restore
`local/images/ifs0.sealed.bin`** — it is the 2.0.0.4869 IFS (trap 84).

## 1. The one thing to do next: the TWL6030 charger test

**Why this first.**  The board had no TWL6030 `charger` child, so
`CONFIG_CHARGER_TWL6030=m` built a driver that never probed and the module was
not even in the 15-module rootfs.  The pack then ran flat on USB and the unit
browned out at 2.817 V/0 % (trap 108).  A fix is **prepared and untested**.
Do this before audio/touch so the bench can run on a charged pack.

**Inputs (all present):**

* image **`0x91AF34DF`** — `local/images/kernel-0x91AF34DF-charger.img`
  (sha256 `7d4b9187…`), built from `local/kernel-tree` with the charger node
  and `kernel/winchester-7.3.3.config`;
* rootfs **`ba275807…`** —
  `local/images/rootfs-region1-7.3.3-charger.ext4` (17 modules:
  `twl6030_charger.ko`, `twl6030-gpadc.ko` added; `rcS` modprobes both).

**Procedure (one variable).**  Undock first — the dock blocks USB.  With the
pack charged enough to boot:

```bash
printf 'local/images/kernel-0x91AF34DF-charger.img=16,0\nlocal/images/rootfs-region1-7.3.3-charger.ext4=8448,0\n' \
  > /tmp/charger.lst
python3 tools/bbusb.py pack -l /tmp/charger.lst -v 1,2 -s -f -c /tmp/charger.signed
# start this immediately before the reboot (or before power-on from off):
python3 tools/bbusb.py flash --signed /tmp/charger.signed --from-bootrom \
        --password 0000 --apply
# from our running image: tools/pbcon.py 'reboot -f'
# then, after pkexec chmod 666 /dev/ttyACM0:
PBCON_TMO=30 ./tools/pbcon.py 'dmesg | grep -iE "twl6030|charger"; \
  ls /sys/class/power_supply; \
  cat /sys/class/power_supply/*/uevent | grep -E "NAME|STATUS|VOLTAGE|CURRENT|CAPACITY"'
```

The expected new supply is `twl6030_usb` (`POWER_SUPPLY_NAME`), alongside
`bq27541-0`.  If the driver does not enable charging itself, its writable
properties are `online` and `current_max` under that supply; set them and
watch `bq27541-0` `CURRENT_NOW`/`VOLTAGE_NOW`.

**Decision table.**

| result | meaning | next |
|---|---|---|
| `twl6030_usb` appears and `CURRENT_NOW` goes positive, voltage rises | BCI works; the drain was the missing driver | keep this image/rootfs as the new known-good; proceed to step 5 |
| the supply appears but current stays ∼0/negative | binding is right, charge path is not enabled or the limit is too low | try `online`/`current_max`; read `VBUS_VOLTAGE`; check the GPADC channel and `CHARGERUSB_CTRLLIMIT1/2`; one variable |
| no `twl6030_usb` in `/sys/class/power_supply` | node/binding problem | read `dmesg | grep -iE "twl6030\|charger"`; check `monitored-battery`, the gpadc phandle and IRQs 2/5; fix the DTS, rebuild the DTB, reflash |
| the unit browns out before the test | pack still too low | charge longer **off the dock** (dock is fine while off); do not loop boots — each spends a counter unit (trap 108) |

## 1a. After the charger: item 9 step 5 (audio + touch)

Report **176** is the inventory and bus map; DECISIONS §28 is the order.

* **Audio.**  WM8994 MFD + codec (`CONFIG_MFD_WM8994`, `CONFIG_SND_SOC_WM8994`)
  on **I²C4 `0x1a`**.  The 1917 driver says the data path is **McBSP + ABE**
  (`mcbsp=[0..4]`, protocol default i2s, ABE firmware in
  `/base/lib/libabe.so`); the likely Linux path is **bypass the ABE DSP** and
  use `simple-audio-card` with McBSP.  The blocker is that McBSP lives in the
  disabled `&l4_abe` segment (the WD_TIMER3 target hangs on enable), so enable
  the segment with the non-audio targets disabled.  The McBSP instance is
  probably 2 (the parser's default table), but confirm it.
* **Touch.**  Cypress **CY8CTMA3** (Gen3).  1917 facts: **slave `0x65`**,
  400 kHz; **GPIO4_8 = reset** (active low), **GPIO1_14** pulsed low 500 ms
  then high (power/enable); firmware
  `/base/lib/firmware/munin/{Pegasus_50,Pegasus2_50,Pegasus_D4_A}.hex`
  (`muninfw`, daemon `touch`).  The bus is either I²C2 `0x48072000` (QNX saw
  `0x65` there) or I²C3 `0x48060000` (the library's `/dev/i2c2`, and the
  controller QNX does not instantiate); settle it with `i2cdetect -y -r 2` and
  `-r 3`.  See `docs/contradictions/touch-bus-i2c2-vs-i2c3.md`.

**Rules for both.**  One variable per boot; characterise a device only from the
OS that owns it (DECISIONS §31, trap 103); ask the reference OS what it maps
(`pidin mem`/`pidin irq`) before writing a DT node (DECISIONS §30); **hardware
RE from OS 2.x only, never BB10** (DECISIONS §34, trap 110).  Neither needs an
RPMB write.

**The WL1283 stays parked.**  It is on **MMC5 (`0x480D5000`)**, WL_EN =
**GPIO2_22**, WL_IRQ = **GPIO2_21** (input); `&mmc5` probes and clocks and the
card never answers CMD5.  Read report 180 §6 before touching it: every SoC-side
register now matches the reference, and the one untested mechanism is enabling
the **FREF reference-clock parent** in CM.

## 1a. Tools that make this cheap

* **`tools/pbssh.sh`** (the door; **root** ssh since 2026-11-05) and
  **`tools/pbcon.py`** (USB console — the only way into our Linux image);
  `tools/door-hold.sh` keeps the door up.  **On our Linux image there is no
  door**: use `pbcon.py`, and `reboot -f` from its shell (trap 88).
* **`tools/build_kernel_image.py`** — wraps `zImage` + DTB into a copy of the
  sealed IFS container; prints the CRC.  The image must end below **0xB00000**
  (the region-0 IFS slot); the 7.3.3 build relies on **XZ** to fit.
* **`tools/bbusb.py`** — the flasher (DECISIONS §29): `pack -l both.lst -v 1,2
  -s -f` and `flash --signed --from-bootrom --password 0000 --apply` write both
  regions in one bootrom session.
* **`tools/build_rootfs.py`** — the region-1 ext4; `--krel 7.3.3 --modules DIR`.
* **`local/kernel-tree/drivers/misc/pbmon.c`** + `/dev/pbseal`/`/dev/pbunseal`/
  `/dev/pbkeys`/`/dev/pboracle` — the monitor transform (reports 174/175).
* `tools/env.sh` — both cross-toolchains; `tools/env.sh --check`.

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
printed `image 0x…` must be **< 0xB00000**.  Check the config line
`CONFIG_KERNEL_XZ=y` if the image grows past the slot.

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

## 3. Traps

Full text: `KNOWN-ISSUES.md` §2.  The ones that end sessions:

9. A write to a DSS target in module standby never completes; let `omapdrm` own the DSS.
16. Blocking I²C is the same hazard — fork with a bounded wait.
17. A stale door holder looks like `Connection refused`; kill by explicit pid.
24. `/dev/ttyACM0` is recreated per enumeration (`pkexec chmod 666` after every boot).
26. `pmic-twl6030-ctl set_state vaux1 off` takes the unit down; read rails.
27. A flash cycle's precondition is **media running** (`0fca:8010`), not the ramloader.
28. Panel-chain writes outlive a warm reset (`SYS_RST <- 0x1f`; trap 28).
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
     tree, apply the patch to pristine, and compare (the 7.3.3 export did).
107. **`cm3217_remove()` needs `i2c_set_clientdata()`** or deleting/unbinding the
     client oopses and kills the shell.  Fixed in the 7.3.3 tree/source; do not
     unbind the driver on the 6.15.11 build.
108. **A nearly empty battery causes spontaneous warm resets that look like a
     software fault.**  Session 30 ended with the BQ27541 reading **2.817 V,
     0 %, Critical**; the unit had reset on its own 11 minutes after reading
     3.153 V / 71 %.  Read `power_supply/bq27541-0/uevent` before diagnosing a
     reset, and charge at ≤3.0 V — each boot costs a counter unit.
109. **Subagents share this session's shell; "static RE only" is not enforced.**
     Audit their `/tmp/commandcode/shellout/` logs, give them file-only tasks
     while the bench is connected, and never let them run `tools/` scripts.

## 4. Where things are in this workspace

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console commands | `docs/project/DEVELOPMENT.md` |
| host prerequisites, toolchains | `docs/project/SETUP.md` |
| board DTS and patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + exact config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| chainloader source | `boot/chainloader/chainloader_kernel.S.in` |
| tools | `tools/` (see `tools/README.md`) |
| images, sealed IFS, backups | `local/images/` (gitignored) |
| 6.15.11 reference tree | `local/kernel-tree-6.15.11/` |
| OMAP4430 TRM (text) | `hardware/swpu231ap.pdf.txt` |
| public/private repositories | `docs/project/REPO-MANAGEMENT.md` |

## 5. Working rules

* **After every console command, dump the log tail and read it.**
* **Write the durable fact into the repository in the same turn.**
* **Do not budget work against a context window**; the operator decides when to
  stop.
* **Static RE only for subagents** (one process owns the USB link).
* **Never present a guess as a fact.**  External facts are looked up and cited
  (DECISIONS §32, trap 105); device facts prefer a fresh read over an old note.
* **One variable per boot**; the whole-map diff is the highest-yield technique.
* **Update the docs as you go**; `STATE.md` and `KNOWN-ISSUES.md` go stale first.
* **Restore the sealed IFS at the end of every session** unless the operator
  ends on our image (session 30 did).

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 31 (absolute; Linux-era 25)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md,
KERNEL-PORT.md (especially §7), ROADMAP.md items 9 and 10, KNOWN-ISSUES.md
(traps 9-110, especially 91-110), DEVELOPMENT.md sections 3/3a/4, DECISIONS.md
(17-34), reports 176 and 180/181/182, sessions/session-30.md, and
docs/contradictions/fan5702-safe-model-vs-cm3217-race.md.

WHERE IT STANDS: **the patch set is on Linux 7.3.3 and the unit boots it**
(session 30).  ROADMAP item 10 is CLOSED: region-0 0x0F6AC47F
(local/images/kernel-0x0F6AC47F-7.3.3.img) + region-1
local/images/rootfs-region1-7.3.3.ext4 (sha256 0b3b83e8..., 15 modules under
/lib/modules/7.3.3).  Panel clean (operator-confirmed), eMMC and the ext4 root
work, and the CM3217 auto-probes 3-0010 (26-30 lux).  The 6.15.11 tree, tarball
and patch are intact as the backup/reference; the 7.3.3 patch is 6 648 lines /
42 files and reproduces the tree byte-for-byte.  Build with
kernel/winchester-7.3.3.config; CONFIG_KERNEL_XZ keeps the image under the
0xB00000 IFS slot.  **One gap found at session end: the board had no TWL6030
charger node, so the BCI never probed and the pack ran flat.**  The fix is
prepared (image 0x91AF34DF + rootfs ba275807) but NOT flashed; test it first.

PHYSICAL STATE: OUR LINUX IMAGE IS IN REGION 0 — region-0 0x0F6AC47F and
region-1 the 7.3.3 rootfs.  The 1917 IFS is NOT on media (session 29 ended the
same way).  BOOTCTR_FCTRY read 0x2E at the final planned boot and **0x2D after a
power-brownout reset** at 12:50 (top-up threshold 0x1E, no write).  **The battery
was critical at session end (2.817 V, 0 %) — charge before bench work.**  Restore TabletOS with tools/bbusb.py flash --component
local/work/ifs-shim-sealed.ifs --dest-block 0x825 --from-bootrom --password
0000 --apply, then re-read /dev/rpmb/BOOTCTR_FCTRY.  Do NOT restore
local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

NEXT WORK: **first the charger test** (the pack ran flat because the TWL6030
BCI was never instantiated — see below), then ROADMAP item 9 step 5.  The
charger fix is prepared but NOT flashed: image `0x91AF34DF`
(`local/images/kernel-0x91AF34DF-charger.img`) + rootfs `ba275807…`
(`local/images/rootfs-region1-7.3.3-charger.ext4`, 17 modules).  The unit was
docked on the rapid charger at session end; dock blocks USB, so undock it when
flashing.  Then item 9 step 5 — the WM8994 audio machine (MFD+codec on I2C4
0x1a, McBSP + ABE; the driver's `mcbsp` default still to pin down) and the
Cypress CY8CTMA3 touch (Gen3; QNX Munin/Pegasus firmware; likely I2C3
0x48060000, already enabled but empty).  Report 176 is the inventory.  The
WL1283 is parked (report 180 §6).  hd3/BB10/PlayBook OS work remain parked.

HARDWARE RE RULE: **OS 2.x only** (live 1917 oracle, the 1917 `/base` QNX6
image, the 1917 IFS tree, the 4869 IFS dump).  Never use BB10 images for
PlayBook hardware facts — BB10 is for newer devices (DECISIONS §34, trap 110).

Two rules: ask the reference OS what it maps (pidin mem / pidin irq) before
writing a DT node (DECISIONS 30); do NOT characterise a device on TabletOS if a
QNX driver owns it (DECISIONS 31, trap 103).  Two new traps from session 30:
106 (the patch export must be tree-derived, not previous-patch-derived) and
107 (cm3217 remove needs i2c_set_clientdata).

An agent NEVER writes RPMB (SAFETY.md section 6); the CRNG needs `entropy
<hex>` before any write (trap 87).

BENCH: door = 169.254.0.1 (host: ip addr add 169.254.0.2/16 dev <iface>);
tools/pbssh.sh logs in as ROOT.  Start tools/door-hold.sh with setsid (never
`pgrep -af door-hold`).  On our Linux image there is NO door: use
tools/pbcon.py (the shell prompt is `pb> `), and reboot with
tools/pbcon.py 'reboot -f'.  `pkexec chmod 666 /dev/ttyACM0` after EVERY boot
and the operator must answer the prompt.  Run the USB/flash tools as the USER —
never pkexec.  The device password is 0000, sent once, never retried.
Rebuild the QNX oracle tools after `. tools/env.sh`: pbmem3 = `ntoarmv7-gcc -O2
-o /tmp/pbmem3 tools/memdump3.c`, and /tmp is wiped on reboot.

SAFETY: read SAFETY.md first.  One variable per boot.  To return to Linux after
a TabletOS session, flash BOTH regions in one bootrom session
(`pack -l both.lst -v 1,2 -s -f` then `flash --signed`) — do not flash only
region 1, or the unit boots TabletOS with no console node.  Operator's call when
to stop; the operator also decides when to wrap up.

SUBAGENTS: static RE only, no bench — and enforce it: they share your shell.
Give them file-only tasks, never `tools/` scripts, and audit their
`/tmp/commandcode/shellout/` logs.  Skeleton first, exact paths, an explicit
"stop when answered".  Check the battery before diagnosing any reset (trap 108).

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.
```
