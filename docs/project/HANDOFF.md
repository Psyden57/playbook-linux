# Handoff — session 32 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 32 = Linux-era 26**.  See
`docs/sessions/NUMBERING.md`.

> **Session-31 result: the board's charger works under Linux, and WDT2 now
> matches the reference.**  The charger is an **Intersil ISL9519** at I²C1
> `0x48070000` slave `0x09`, not the TWL6030 BCI (report 183).  A minimal
> driver (`drivers/power/supply/isl9519.c`) charges the pack at **~0.4 A**
> from a host USB port; image **`0xE36C679A`** also reprograms **WDT2 to the
> reference's 58.6 s window and pets it every 10 s** (the loader's 7.8125 s
> window forced the old 2 s pet).  The **WL1283 FREF route is closed** — the
> `CM_DIV_M5` divider is the DSS clock, and writing the reference's `0x329`
> glitches the panel (trap 116); the chip now needs its datasheet/schematic or
> a bus capture.  The unit is **on our image, charging** at session end.
>
> **Next: ROADMAP item 9 step 5 — the WM8994 audio machine and the Cypress
> CY8CTMA3 touch.**  `hd3`, BB10 and the PlayBook OS image work remain parked.

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
battery-profile intermediate.  **Build from `0xE36C679A`'s source tree.**

**The 6.15.11 backup/reference is intact:** `local/pristine/linux-6.15.11`,
`local/kernel-tree-6.15.11` (byte-exact copy of the old working tree) and
`kernel/patches/0001-playbook-winchester-6.15.11.patch`.  Do not delete any of
them.  The 6.15.11 patch is **incomplete** (trap 106); do not use it for a
clean rebuild.

**The 7.3.3 patch is `kernel/patches/0001-playbook-winchester-7.3.3.patch`** —
**7 103 lines / 45 files** (36 modified + 9 new), sha256 `26babb10…`, verified
by applying to pristine 7.3.3 with 0 rejects and comparing all 45 files
byte-for-byte.  Config: `kernel/winchester-7.3.3.config` (includes
`CONFIG_CHARGER_ISL9519=y`; `CONFIG_KERNEL_XZ=y` keeps the image under
`0xB00000`).

**Parked by the operator:** the `hd3` cut-over, BB10, and the PlayBook OS image
work.  **The WL1283 (item 9 step 4) is blocked, not parked:** its FREF route
was tested and closed in session 31, so the next input must be the chip
datasheet/schematic or a bus capture of CLK/CMD/DAT/EN (report 180 §6/§8).

## 0a. The way back (the unit is on our Linux image, the 1917 IFS is not on media)

To return to PlayBook OS 2.1.0.1917:

```bash
nohup python3 tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs \
    --dest-block 0x825 --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
sleep 2; PBCON_TMO=10 ./tools/pbcon.py 'reboot -f'
until grep -qE 'COMPLETE|done|error' /tmp/f.log; do sleep 2; done; tail -5 /tmp/f.log
```

Region 0 is the **only** region that changed on this trip (the region-1 rootfs
is unchanged), so restoring **region 0 only** is enough — the old "flash both
regions" rule applies only when both were modified (operator correction,
session 31).  The RCFS at region-0 `0xC00000` and the 1917 UFS are still on
media and untouched.  After the boot: `tools/pbssh.sh 'uname -a; mount | grep
base'` and read `/dev/rpmb/BOOTCTR_FCTRY` (expect `0x32` after the TabletOS
top-up).  **Do not restore `local/images/ifs0.sealed.bin`** — it is the
2.0.0.4869 IFS (trap 84).

## 1. The next task: item 9 step 5

Report **176** is the inventory and bus map; DECISIONS §28 is the order.
**One variable per boot.**

### 1.1 WM8994 audio

* **Where:** WM8994 MFD + codec on **I²C4 `0x48350000`** (Linux `i2c-3`),
  7-bit **`0x1a`**; the codec is in the board DTS's `&i2c4` block but the
  mainline **`CONFIG_MFD_WM8994` / `CONFIG_SND_SOC_WM8994` are not enabled**.
* **What the 1917 driver does:** `deva-ctrl-omap4abe-wm8994.so` — McBSP +
  **ABE** DSP (`mcbsp=[0..4]`, protocol default i2s, ABE firmware in
  `/base/lib/libabe.so`); no separate audio config file.
* **Likely Linux path:** bypass the ABE DSP with `simple-audio-card` + McBSP.
  The blocker: McBSP lives in the disabled **`&l4_abe`** segment and the
  WD_TIMER3 target hangs on enable, so enable the segment with the non-audio
  targets disabled.  The McBSP instance is probably **2** (the parser's default
  table), but **confirm it** — this is the open question.
* **First steps:** enable the two symbols, add the codec node under `&i2c4`,
  probe the ID (`i2cget -f -y 3 0x1a 0x0000 w`? the codec is 16-bit register),
  then the machine link.  Speakers, the 3.5 mm jack and the mics are all
  behind this one codec.

### 1.2 Cypress CY8CTMA3 touch (Gen3)

* **Part:** CY8CTMA3 TrueTouch Gen3; mainline has Gen2 `cyttsp_core` and Gen5
  `cyttsp5`, not this part.  RIM's stack: `libmtouch-munin.so.1` /
  `libmtouch-pegasus.so`, `muninfw`, daemon `/base/usr/bin/touch`, firmware
  `/base/lib/firmware/munin/{Pegasus_50,Pegasus2_50,Pegasus_D4_A}.hex`.
* **1917 facts:** slave **`0x65`**, **400 kHz**; **GPIO4_8 = reset**
  (active low), **GPIO1_14** pulsed low 500 ms then high (power/enable).
* **The bus is unresolved:** I²C2 `0x48072000` (QNX saw `0x65` there) vs
  I²C3 `0x48060000` (the library's `/dev/i2c2`, the controller QNX does not
  instantiate).  See `docs/contradictions/touch-bus-i2c2-vs-i2c3.md`.
  **First bench step:** `i2cdetect -y -r 2` and `i2cdetect -y -r 3` on the
  running image (never run in session 30/31).
* **Next:** confirm the bus and whether the part answers before its firmware
  is loaded, then decide between a new driver and the Gen2 core with a Gen3
  protocol layer.  This is the largest remaining driver/RE task.

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
PlayBook (OMAP4430 "winchester") — session 32 (absolute; Linux-era 26)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md,
KERNEL-PORT.md (especially §7), ROADMAP.md item 9 (steps 4 and 5),
KNOWN-ISSUES.md (traps 9-116, especially 105-116), DEVELOPMENT.md sections
3/3a/4, DECISIONS.md (17-38), reports 176, 180/181/182 and 183,
sessions/session-31.md.

WHERE IT STANDS: the patch set is on Linux 7.3.3 and the unit boots it.
The last tested image is region-0 0xE36C679A
(local/images/kernel-0xE36C679A-wdt58s.img) + region-1
local/images/rootfs-region1-7.3.3-charger.ext4 (17 modules).  The board's
charger is an Intersil ISL9519 at I2C1 0x09 (report 183), NOT the TWL6030
BCI: drivers/power/supply/isl9519.c charges the pack at ~0.4 A from host
USB, and WDT2 now runs the reference's 58.6 s window with a 10 s pet
(trap 115).  The 6.15.11 tree/tarball/patch are intact as the backup.
The 7.3.3 patch is 7 103 lines / 45 files (sha256 26babb10...) and
reproduces the tree byte-for-byte; config kernel/winchester-7.3.3.config
(CONFIG_CHARGER_ISL9519=y, CONFIG_KERNEL_XZ=y).

PHYSICAL STATE: OUR LINUX IMAGE 0xE36C679A IS IN REGION 0 — region 1 is the
7.3.3 charger rootfs; the 1917 IFS is NOT on media.  BOOTCTR_FCTRY read 0x2D
on the final boot.  The unit is ON and CHARGING at ~0.4 A.  Restore TabletOS
by flashing region 0 only:
tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs
--dest-block 0x825 --from-bootrom --password 0000 --apply, then re-read
/dev/rpmb/BOOTCTR_FCTRY.  Do NOT restore local/images/ifs0.sealed.bin
(2.0.0.4869, trap 84).

NEXT WORK: ROADMAP item 9 step 5 — the WM8994 audio machine (MFD+codec on
I2C4 0x1a, McBSP + ABE; the driver's mcbsp default still to pin down) and
the Cypress CY8CTMA3 touch (Gen3; QNX Munin/Pegasus firmware; the bus is
either I2C2 0x48072000 or I2C3 0x48060000 — settle it with i2cdetect -y -r
2 and -r 3).  Report 176 is the inventory.  The WL1283 (step 4) is BLOCKED
on external evidence: its FREF route was tested and closed (the CM_DIV_M5
divider is the DSS clock; trap 116), so it needs the chip datasheet or a
bus capture.  hd3/BB10/PlayBook OS work remain parked.

HARDWARE RE RULE: OS 2.x only (live 1917 oracle, the 1917 /base QNX6 image,
the 1917 IFS tree, the 4869 IFS dump).  Never use BB10 images for PlayBook
hardware facts (DECISIONS §34, trap 110).  Ask the reference OS what it maps
(pidin mem / pidin irq) before writing a DT node (DECISIONS 30); do NOT
characterise a device on TabletOS if a QNX driver owns it (DECISIONS 31,
trap 103).

An agent NEVER writes RPMB (SAFETY.md section 6); the CRNG needs `entropy
<hex>` before any write (trap 87).

BENCH: door = 169.254.0.1 (host: ip addr add 169.254.0.2/16 dev <iface>);
tools/pbssh.sh logs in as ROOT.  Start tools/door-hold.sh with setsid (never
`pgrep -af door-hold`).  On our Linux image there is NO door: use
tools/pbcon.py (the shell prompt is `pb> `), and reboot with
tools/pbcon.py 'reboot -f'.  `pkexec chmod 666 /dev/ttyACM0` after EVERY
boot and the operator must answer the prompt.  Run the USB/flash tools as
the USER — never pkexec.  The device password is 0000, sent once, never
retried.  Rebuild the QNX oracle tools after `. tools/env.sh`:
pbmem3 = `ntoarmv7-gcc -O2 -o /tmp/pbmem3 tools/memdump3.c`, and /tmp is
wiped on reboot.

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
