# Handoff — session 28 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§5).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 28 = Linux-era 22**.  See
`docs/sessions/NUMBERING.md`.

> **Session-28 update (read first).  ROADMAP item 5 is CLOSED.**  The region-1
> root is a real BusyBox userland (BusyBox 1.36.1, init, 401 applet links,
> `pbconsole`/`pbrpmb`, `/lib/modules/6.15.11`); the initramfs mounts it
> **read-write**, bind-mounts /dev//proc//sys, chroots and execs `/sbin/init`.
> The unit boots to `sh -l` on `/dev/ttyGS0` with `PS1='pb> '`, and
> `insmod`/`modprobe` load a module.  Images: region 0
> `local/images/kernel-0xAEDF68D0-rootuserland.img`; region 1
> `local/images/rootfs-region1.ext4` (sha256 `e1ec2bf3…`, raw `0x29000000`).
> The sealed 1917 IFS was flashed back and verified (`BOOTCTR_FCTRY = 0x32`).
> **The `hd3` cut-over is PARKED (operator, session 28).**
>
> **The new focus is the device's own sensors and peripherals** (ROADMAP item 9,
> report 176).  The sensor cluster is on OMAP **I²C4 `0x48350000`**, which the
> board DTS disabled: BMA150 accel `0x38`, HMC5883L mag `0x1E`, MPU3050 gyro
> `0x68`, CM3217 light `0x10`, FAN5702 LED `0x36`.  The BQ27541 gauge `0x55` and
> TMP103 `0x77` are on I²C1; the WM8994 codec is on I²C4 `0x1a`; the TI WL1283
> combo (WiFi/BT/GPS+AGPS) is SDIO/UART on `mmc1`.  Most mainline drivers exist
> but are disabled; FAN5702, NCP5623, the Cypress CY8CTMA3 touch and the
> OMAP4+WM8994 machine glue are real work.
>
> **Step 1 is DONE (report 177):** `&i2c4` is enabled and the BMA150
> (`3-0038`, `input0`/evdev), HMC5883L (`3-001e`, IIO) and MPU3050 (`3-0068`,
> IIO) all report live data.  **Step 2 (report 178):** the **TMP103**
> (`hwmon0`, 37.0 °C) and the **BQ27541** gauge (`bq27541-0`: 100 %, 4.093 V,
> 28.8 °C, 5.307 Ah, cycle 5) work; the **CM3217 light sensor is open** —
> 0x10/0x11 ACK but read all-zero, the mainline `cm32181` accepts only
> 0x18/0x81/0x82, and the QNX `ls-cm3217` disassembly gives 0x10, 400 kHz,
> `devctl 0xC0100506` (trap 95).  **Step 3 (report 179): the FAN5702 status LED
> works** — `leds-fan5702` probes `3-0036`, the three channels are in
> `/sys/class/leds`, and EN = GPIO1_13 (pad `0x1b0`, safe mode at boot) is
> driven by the driver's pinctrl + `enable-gpios`.  **Next: the WL1283 WiFi/BT/
> GPS combo (step 4); the CM3217 RE and audio/touch (step 5) remain.**
> **`tools/bbusb.py` is the flasher**, and both regions flash in one bootrom
> session (`DECISIONS.md` §29).  Session 27's RPMB result stands (report 175):
> items 4 and 6 are closed, the counter tops itself up at boot.

**The job is the Linux bring-up again.**  The BB10 port and the PlayBook OS
image (IFS/RCFS) work are **parked by the operator** for someone else to
follow; their documents stay in the tree (`BB10-PORT.md`,
`BB10-IMAGE-GUIDE.md`, `IFS-REBUILD.md`, `IMAGE-REBUILD-GUIDE.md`) but they are
**not** this session's work.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 6.15.11** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain — **the panel shows a clean, stable
image** (DSS/DSI, the TC358764 bridge, the Hitachi TCON table, the LM3532
backlight) — and a **64 MiB ext4 BusyBox root in region 1** at raw
`0x29000000`; the initramfs mounts it read-write, chroots and execs
`/sbin/init`, the unit boots to a shell, and `insmod`/`modprobe` work.  The
kernel patch is current: **5 137 lines / 34 files**
(`kernel/patches/README.md`).

**Last known-good image:** **`0xB759C3A2`** (session 28: I²C4 motion sensors,
TMP103/BQ27541 and the FAN5702 LED) with the region-1 root
`local/images/rootfs-region1.ext4` (sha256 `0a55a170…`, 15 modules).  The
earlier steps `0x1D9159E0`, `0x3E479F6F` and `0xAEDF68D0` are behind;
`0x0D1DA0D3` is the clean pre-userland fallback.  **Build from this.**  (Image
CRC convention: `zlib.crc32(image[0:-4])` in the container's last word; a
rebuild with the same CRC has usually failed to link — trap 39.)

**Diagnostic images** (the `pbmon` RPMB work) are built from the current
`local/kernel-tree` (which now carries the working two-stage transform);
`0x0D1DA0D3` is the clean fallback.  Session 27 built `0x4F988620`,
`0x74D34374`, `0x4EBF1F97`, `0x48166F12`, `0x1FEBC300` and `0xD3D42853`; the
last is the image that reads the store.

**The region-0 IFS is compressed and boots (session 27).**  The QNX IFS format
is the same on the PlayBook as on BB10, so `tools/bb10_ifs.py splice` +
`89-crc_tool.py reseal` turn the uncompressed 7.9 MB container into a booting
3.47 MB one (`local/work/ifs1917-compressed.ifs`).  That is the route if a
region-0 payload ever needs the room — but our chainloader image is not a QNX
imagefs, so it needs a decompressing entry path first, not just `splice` (see
the session note's open questions, including the unverified 11.25 MB figure).

**Item 6 is CLOSED (session 27).**  The transform is reproduced (report 175)
and the operator performed the single `BOOTCTR_FCTRY = 0x32` top-up **from
Linux**: `pbrpmb prep 0x3a` builds the whole pipeline read-only (authenticated
read, monitor unseal, patch 4 bytes, monitor seal, seal round-trip check, one
type-3 frame) and `pbrpmb write 0x3a yes` sends it, reads the result and
unseals the read-back.  The write was accepted, the read-back read `32 00 00
00`, a read-only scan either side showed 512/512 frames authenticating and
107/107 blocks unsealing, and the restore boot's bootloader consumed that very
record.  **An agent still never writes RPMB** (`SAFETY.md` §6): session 20's
accepted write was a *plaintext-patched* block and left the spare in the `0x5B`
loop; the fix is the monitor re-seal, not a licence to write.

**Still open:** the peripheral bring-up (ROADMAP item 9, report 176) and,
**parked by the operator**, the `hd3` cut-over.  Packaging (item 7) is
untouched.

## 0a. The way back (the unit is currently up on 2.1.0.1917)

The unit is **running PlayBook OS 2.1.0.1917** (door up, `BOOTCTR_FCTRY =
0x32`).  The ramloader/BB10 text below is the session-25 procedure for the case
where the unit is stuck in the loader.  The way back is packed and verified:

```bash
# from the ramloader — NO --from-bootrom
python3 tools/bbusb.py flash --signed local/work/back-1917.signed \
    --password 0000 --apply
```

That restores PlayBook OS 2.1.0.1917 (our 1917 IFS + edited 1917 RCFS + the
vendor `.5.ufs` with `autorun::0`), and its boot tops `BOOTCTR_FCTRY` back to
`0x32`.  Verify with `tools/door-hold.sh` and
`tools/pbssh.sh 'uname -a; mount | grep base; tail -3
/accounts/devuser/.bootctr.log'` (**root** ssh since 2026-11-05).
**Do not flash anything without the operator's go-ahead.**

From **our Linux image** the way back is the shorter one: flash
`local/work/ifs-shim-sealed.ifs` (the 1917 IFS) with
`tools/pre_os_ifs_flash.py` and reboot with `tools/pbcon.py 'reboot'`
(`DEVELOPMENT.md` §6).

## 1. The one thing to do next

**`ROADMAP.md` item 9 — the device's own sensors and peripherals** (report
176).  Item 5 is closed; the `hd3` cut-over is **parked**.  One variable per
boot:

1. ~~The sensor bus + BMA150/HMC5883L/MPU3050.~~  **DONE (report 177).**  `&i2c4`
   is enabled; the three motion sensors report live data.  The IRQ GPIOs
   (GPIO2 2/27/18) are still not described (polling/direct reads) — that is the
   next variable if triggered sampling is wanted.
2. ~~CM3217 `0x10`, TMP103 `0x77`, BQ27541 `0x55`.~~  **TMP103 + BQ27541 DONE
   (report 178).**  The **CM3217 is OPEN**: 0x10/0x11 ACK but read all-zero;
   the QNX `ls-cm3217` is the reference (0x10, 400 kHz,
   `devctl 0xC0100506`, CMD/IT/cal_table) and the mainline `cm32181` needs an
   ID of 0x18/0x81/0x82 (trap 95).  Static-RE it, then add a CM3217 ID/CMD
   path or a small driver.
3. ~~FAN5702 `0x36`.~~  **DONE (report 179):** `leds-fan5702` probes
   `3-0036`; EN = GPIO1_13 (pad `0x1b0`) driven by the driver's pinctrl +
   `enable-gpios`; `GENERAL 0x10` bits 1/2/3 = R/G/B.
4. **WL1283** — enable `mmc1` SDIO + the power/32k GPIOs, `wl12xx`, and the
   firmware/NVS from `/base/lib/firmware/ti1283`; then BT (UART3) and GPS/AGPS
   (`devlp-ti128x`, `/base/lib/firmware/gps`).
5. **Audio (WM8994 + ABE machine)** and **touch (Cypress CY8CTMA3,
   Munin/Pegasus)** — the two big RE/driver tasks.

### The one next experiment: does the WL1283 enumerate on `mmc1`?

**Inputs.**  The unit is on TabletOS, so the QNX reference is readable:
`/base/lib/dll/devnp-ti1283.so`, `/base/lib/firmware/ti1283/{firmware.bin,
tiwlan_winchester.ini}` (pull them with `tools/pbscp.sh`; the `/base` files are
root-readable).  The mainline modules are already built as `=m`: `wl12xx`,
`wlcore`, `cfg80211`, `mac80211`.  The board DTS `&mmc1` is currently
"no wifi SDIO brought up yet"; the WL1283 power/32k/enable lines and the SDIO
IDs are what the static RE (`devnp-ti1283.so`) must give (subagent, static
only).  The CM3217 RE (`ls-cm3217.so`, trap 95) is a parallel static task.

**Steps (one variable).**
1. Enable `&mmc1` as an SDIO bus (non-removable, 4-bit), add the WL1283 card
   node (`compatible = "ti,wl1283"`) and the regulator/GPIO enable lines from
   the RE.
2. Build the DTB/image, put `wl12xx`/`wlcore`/`cfg80211`/`mac80211` and the
   firmware/NVS into the rootfs, flash **both** with one `bbusb.py` container,
   boot.
3. `dmesg | grep -i -E 'mmc1|wl12'`, `ls /sys/bus/sdio/devices`, `lsmod`.

**Decision table.**

| result | meaning | next |
|---|---|---|
| `mmc1: new SDIO card` and the SDIO device appears | bus, power and pinctrl are right | convert the firmware/NVS and load `wl12xx` |
| `mmc1` probes, no card / timeout | the power/32k/enable or pinctrl is wrong | apply the RE GPIOs/regulators; check `vmmc` |
| card enumerates, driver `-ENOENT` on firmware | the SDIO path is right, the blob format is not | convert `firmware.bin` → `wl128x-fw-4-sr.bin`, `tiwlan_winchester.ini` → `wl128x-nvs.bin` |
| `mmc1` fails to probe at all | DTS/pinctrl/clock | check the MMC1 pins and the controller node |

The Linux build/loop is `DEVELOPMENT.md` §3/§3a/§4: `tools/build_kernel_image.py`
for region 0 and `tools/build_rootfs.py` (+ BusyBox) for region 1.  The working
images are region-0 `0xB759C3A2` and region-1 `0a55a170…`.  **Flash with
`tools/bbusb.py`** (`pack` + `--signed` writes both regions in one bootrom
session; `--component` for one).  If a fresh 1917 boot is needed, the compressed
IFS on media boots and the uncompressed `local/work/ifs-shim-sealed.ifs` is the
conservative choice.

Useful state from session 27, if anything RPMB-adjacent comes up again:

```bash
run /pbrpmb pairtest    # the transform: expect unseal=0/OK p=2dbc8359, ct=OK tag=OK
run /pbrpmb keytest     # live block 0x43 round-trip
run /pbrpmb blockscan   # 512/512 frames, 107/107 unseal, BOOTCTR_FCTRY@196
run /pbrpmb prep 0x3a   # build the sealed top-up, write nothing
```

Seed the CRNG first (`entropy <hex>` from the console, trap 87) before any
write; `pbrpmb` warns if it has to fall back to an unseeded nonce.

Do **not** boot the mount probe (`0x984267D3`); it reset-loops.  Do not boot
`local/images/ifs0.sealed.bin` on a 1917 media set (trap 84).

## 1a. Tools that make this cheap

* **`tools/pbssh.sh`** (the door; **root** ssh since 2026-11-05, no `PB_USER`
  needed) and **`tools/pbcon.py`** (USB console — the only way into our Linux
  image); `tools/door-hold.sh` keeps the door up.
* **`tools/build_kernel_image.py`** — wraps `zImage` + DTB into a copy of the
  sealed IFS with the chainloader, prints the CRC.
* **`tools/pre_os_ifs_flash.py`** — the pre-OS `0xF7` region-0 write; it resets
  the loader itself.
* **`tools/pb_ring.py` / `pb_events.py`** — the DRAM console ring and the
  printk-free event log.
* **`tools/pbdsi`** (`qnx-dsiread.c`) — the TC358764 oracle over DSI from a
  running TabletOS; push it after every reboot.
* **`local/kernel-tree/drivers/misc/pbmon.c`** + `/dev/pbseal` /
  `/dev/pbunseal` / `/dev/pbkeys` / `/dev/pboracle` — the monitor transform.
  Session 27 implemented the bootloader's **two-stage** sequence (`0x2F` is the
  cipher, `0x2E` the digest; unseal takes 224+32 and checks the tag), added
  `/dev/pbkeys` ({keyA,keyB}) and the `op2e` parameter, and pinned both key
  defaults to the seed.  `dom` (default 5) still selects the domain.
* `tools/env.sh` — both cross-toolchains; `tools/env.sh --check`.

## 2. Traps

Full text: `KNOWN-ISSUES.md` §2.  The ones that end sessions:

9. A write to a DSS target in module standby never completes; let `omapdrm`
   own the DSS.
16. Blocking I²C is the same hazard — fork with a bounded wait.
17. A stale door holder looks like `Connection refused`; kill by explicit pid.
24. `/dev/ttyACM0` is recreated per enumeration (`pkexec chmod 666` after every
   boot; if `pkexec` is denied, the operator runs `sudo chmod`).
26. `pmic-twl6030-ctl set_state vaux1 off` takes the unit down; read rails.
27. A flash cycle's precondition is **media running** (`0fca:8010`), not the
   ramloader; end with a loader reset.
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
60–63. RPMB: the block is reversed; the 224-byte block is monitor-encrypted; the
   monitor reads identity addresses below `0xB0000000`.  **No agent writes RPMB.**
80. The loader's factory count is the first diagnostic after a flash
   (`bbusb.py info --from-bootrom --password 0000 --keep-loader --json`).
82. The bootloader's code addresses in reports 150/151 are `0x9000` too high
   (the arm9000 section loads at `0x80000000` from media `0x9000`).
83. The bootloader wipes its own RPMB state before the OS jump, and its mount
   reset-loops when called from the chainloader.  Never boot the mount probe.
84. `local/images/ifs0.sealed.bin` is the 2.0.0.4869 IFS — wrong for a 1917
   media set.  Use `local/work/ifs-shim-sealed.ifs`.
85. RPMB keys are per-unit; the compiled keys were the spare's; the transform is
   a two-stage `0x2E`+`0x2F` (report 174).
86. **The raw RPMB relay is the WIRE view** — reverse the 256-byte data field
   before `/dev/pbunseal`, or every block fails the tag check.
87. **The initramfs CRNG is unseeded** (`random: … uninitialized urandom read`)
   — fix it before any RPMB write, whose replay protection is the nonce.
88. **On our Linux image there is no door and no `/proc/boot/shutdown`**: reboot
   with `tools/pbcon.py 'reboot'`, and start the flasher just before it.

Session-25/26/27 additions relevant here: the factory counter is read with
`bbusb.py info --from-bootrom --password 0000 --keep-loader --json`
(`boot_mode.factory_count`); the password is `0000`, sent once, never retried;
`pkexec` is only for `chmod /dev/ttyACM0`, never for the USB/flash tools; **the
door ssh is root** (2026-11-05) and `devuser` cannot reboot or read `/dev/rpmb`;
**the flasher's precondition is a booted OS** — `0fca:8012` for TabletOS,
`0fca:8010` for our image — never the ramloader (`0fca:8001`).

## 3. Where things are in this workspace

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console commands | `docs/project/DEVELOPMENT.md` |
| host prerequisites, toolchains | `docs/project/SETUP.md` |
| board DTS and patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + exact config | `kernel/patches/`, `kernel/winchester.config` |
| chainloader source | `boot/chainloader/chainloader_kernel.S.in` |
| tools | `tools/` (see `tools/README.md`) |
| images, sealed IFS, backups | `local/images/` (gitignored) |
| QNX boot set for RE | `local/proprietary/dumped4869ifs/proc/boot/` |
| OMAP4430 TRM (text) | `hardware/swpu231ap.pdf.txt` |
| public/private repositories | `docs/project/REPO-MANAGEMENT.md` |

## 4. Working rules

* **After every console command, dump the log tail and read it.**
* **Write the durable fact into the repository in the same turn.**
* **Do not budget work against a context window**; the operator decides when to
  stop.
* **Static RE only for subagents** (one process owns the USB link).
* **Never present a guess as a fact.**  Mark inferred claims.
* **The whole-map diff is the highest-yield technique** — and ask the operator
  what the panel shows before building a theory.
* **Update the docs as you go**; `STATE.md` and `KNOWN-ISSUES.md` go stale
  first.
* Publishing is separate (`REPO-MANAGEMENT.md`, `tools/publish_public.sh`).

## 5. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 29 (absolute; Linux-era 23)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md,
ROADMAP.md items 5 and 9, KNOWN-ISSUES.md (traps 9-93, especially 91-93),
DEVELOPMENT.md section 3/3a/4, DECISIONS.md (17-28), report 176 (and 140/175),
session-28.md, and any relevant documentation.

SCOPE: the LINUX bring-up, now the DEVICE'S OWN SENSORS AND PERIPHERALS.
The hd3 cut-over, the BB10 port and the PlayBook OS image work are PARKED by
the operator — do not work on them.

PHYSICAL STATE (session 28 end): PlayBook OS 2.1.0.1917 is running (the
restored uncompressed IFS local/work/ifs-shim-sealed.ifs, /base mounted),
BOOTCTR_FCTRY = 0x32, door up with ROOT ssh.  The Linux images are region-0
0xAEDF68D0 (local/images/kernel-0xAEDF68D0-rootuserland.img) and region-1
local/images/rootfs-region1.ext4 (sha256 e1ec2bf3...) — re-flash both to return
to Linux.  Do NOT restore local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

WHERE IT STANDS: ROADMAP item 5 is CLOSED — the region-1 root is a BusyBox
userland; the initramfs mounts it read-write, chroots and execs /sbin/init;
modules load.  The next work is ROADMAP item 9 / report 176.  The sensor
cluster is on OMAP I2C4 0x48350000 (QNX /dev/i2c3), which the DTS currently
DISABLES: BMA150 accel 0x38, HMC5883L mag 0x1E, MPU3050 gyro 0x68, CM3217 light
0x10, FAN5702 LED 0x36; WM8994 codec 0x1a on the same bus.  BQ27541 0x55 and
TMP103 0x77 on I2C1.  TI WL1283 (WiFi/BT/GPS+AGPS) on SDIO mmc1/UART3.  Most
mainline drivers exist but are disabled; FAN5702, NCP5623, the Cypress
CY8CTMA3 touch and the OMAP4+WM8994 machine glue are real work.  One variable
per boot; start with the sensor bus + BMA150/HMC5883L/MPU3050.

An agent NEVER writes RPMB (SAFETY.md section 6); the CRNG needs `entropy
<hex>` before any write (trap 87).

BENCH: door = 169.254.0.1 (host: ip addr add 169.254.0.2/16 dev <iface>);
tools/pbssh.sh logs in as ROOT.  Start tools/door-hold.sh with setsid (never
`pgrep -af door-hold`).  On our Linux image there is NO door: use
tools/pbcon.py (the shell prompt is `pb> `), and reboot with
tools/pbcon.py 'reboot' (trap 88).  Run the USB/flash tools as the USER — never
pkexec; `pkexec chmod 666 /dev/ttyACM0` is the documented console step.  The
device password is 0000, sent once, never retried.  The QNX oracle tools
(pbi2c, pbdsi) are pushed to /tmp and wiped on reboot.

SAFETY: read SAFETY.md first.  One variable per boot; budget boots against
BOOTCTR_FCTRY; restore 2.1.0.1917 and verify the counter at the end.

SUBAGENTS: static RE only, no bench.  Skeleton first, exact paths, an explicit
"stop when answered".

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.
```
