# HANDOFF — session 6 (2026-10-27 … 2026-11-15) → session 7

**Read this first, then `NEXT-SESSION-BOOTSTRAP-2026-10-26.md` (the manual), then
the reports in §2.**  This file is *state, next steps, and the command
cheat-sheet*; the bootstrap is the long-form manual (access, capture formats,
instrumentation index, and the older trap list).

---

## 0. The job, and where it stands

**Goal.**  A clean, upstream Linux 6.15.11 on a spare BlackBerry PlayBook —
OMAP4430 ES2.2 "winchester", secure device — booting from our own chainloader
image in media region 0 (`/dev/hd1`, IFS at media offset `0x100000`), with an
initramfs-only userspace and a CDC-ACM console on USB.  The current milestone is
**an image on the DSI → TC358764 → LVDS panel**; after that come a root
filesystem and the RPMB/`BOOTCTR_FCTRY` work (report 116).

| item | state |
|---|---|
| SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2 pet) | **working** (reports 104/109) |
| USB gadget + interactive console (`/dev/ttyACM0` ↔ `/dev/ttyGS0`) | **working** (114/115, plus this session's new commands) |
| DSS/DISPC/DSI via omapdrm | **working** — `/dev/dri`, `/dev/fb0`, `card0-LVDS-1` connected+enabled, `LINE_STATUS` advancing, DSI PLL locked |
| TC358764 DSI→LVDS bridge | **alive, needs no programming** — GPIO36 status ball high (`GPIO2_DATAIN` bit 4); RESETN = `GPIO2_24` (20 ms active-low pulse) |
| Backlight (LM3532 @ 0x38 on i2c-0) | **working** — binds, `bl_power=0`, brightness 255, **panel is lit** |
| Panel TCON init (MCSPI4) | **implemented and exercised** — 9-word Hitachi table, both byte orders, both orderings |
| DSI link | **byte-identical to the reference** — 4 lanes, `COMPLEXIO_CFG1 = 0x2A054321`, `VM_TIMING3 = 0x03F00258` (TL 1008), 37.8 MHz byte clock, `DSI_CTRL = 0x00EAEF9D` |
| **Image on the panel** | **NOT YET** — panel is lit but black; only timing-encoding deltas remain (§1) |
| Kernel patch + config | **re-exported at session-6 close** — 29 files / 2 922 lines, verified against pristine (§3) |
| Unit | **closed clean**: sealed IFS restored and verified (`c72b0530…` MATCH), TabletOS up, door up, `BOOTCTR_FCTRY = 0x32` (50) |

Image at handover: `chainloader/kernel.img` crc **`0xE1F79294`** (4 lanes,
`DSI_CTRL` matched, bridge never programmed, MCSPI4 + spidev, `spi`/`fb`/`kmsg`/
`i2c16` console, GPIO1 + GPIO2 hogs, TC358764 reset on `GPIO2_24`).

**The one-sentence version of the current blocker:** every register we can
measure on the host side now matches the reference, the backlight is on and the
TCON is initialised, and the panel is still black — so the difference is in how
the *timing fields* are distributed (§1), not in the link, the bridge or power.

---

## 1. The one thing to do next

**Force the complete QNX timing set as ONE experiment.**  Everything else is
matched or eliminated:

* **The bridge needs no programming** (report 122 + the relay, 2026-11-15).  Its
  control port is DSI-generic only; nothing in the boot ROM, the eMMC
  second-stage loader, the IFS or the RCFS programs it; and the IFS splash
  (`winch_lcdctl -s splash`, logo at `GFX_SIZE 0x003F01ED` = 494×64 at
  `(265,268)`) proves DISPC + DSI + SPI TCON + LM3532 all work with it untouched.
  Our image deliberately leaves it alone.  **Never write `SYS_RST <- 0x1f`**
  (trap 28).  A failed BTA only means the bridge does not answer *reads* — it
  does not mean writes did not land.
* **The DSI link matches**: 4 lanes, TL 1008, 37.8 MHz byte clock,
  `DSI_CTRL = 0x00EAEF9D`, PLL locked, `IRQSTATUS = 0x80`,
  `COMPLEXIO_IRQ_STATUS = 0`, `PACKET_SENT`.
* **Backlight on**: LM3532 ACKs, `0x1d = 0xf9`, `0x74 = 0xff`, `bl_power = 0`
  (GPIO2_16 is the gate — report 120).
* **TCON init**: both byte orders, and both orderings (also with the stream
  stopped via driver `unbind`, i.e. QNX's step-8-before-step-9 order).  No effect.
* **Still black.**

Only timing *encoding* differs, and all of it is measured:

| register | ours | QNX | note |
|---|---|---|---|
| `DISPC_TIMING_H` | `0x0890B400` = HBP137/HFP180/HSW0 | `0x0890A113` = 137/161/19 | totals 320+1024 both |
| `DISPC_DIVISOR` | `0x00010003` (LCD 3) | `0x00010002` (LCD 2) | both 50.4 MHz, different DSS FCK |
| `DSI_VM_TIMING1` | `0x00086067` = HBP103/HFP134/HSA0 | `0x01079074` = 116/121/1 | TXBYTECLKHS units |
| `DSI_CLK_CTRL` | `0xA0306008` (LP div 8) | `0xA0346005` (5, + bit 18) | |

Force **all four together** — a partial mix really is a 2× error, per the relay.
If that changes nothing: question the `DSI_PHY_CFG*` set (QNX's are in report 118
§3.3; mainline computes its own), and finally whether this TCON wants a
different LVDS mapping than the driver's VESA-24 `LV_MX` table.

**Do not**: re-open the backlight (120 §0); re-check the bridge; use non-burst
mode (trap 31); write `SYS_RST`.

---

## 2. Reports (in reading order for session 7)

| report | what |
|---|---|
| **`121-spi-tcon-panel-2026-10-27.md`** | **the display work of session 6** — MCSPI4/TCON, the QNX oracle, the GPIO2 map, the 4-lane discovery (§11), `DSI_CTRL` decode, and the ordering/dpms results (§12).  Start here for the panel. |
| **`120-lm3532-panel-enable-2026-10-27.md`** | the backlight: GPIO2_16 gates the LM3532; how it was found; the pad-mux and gpio-hog and `bl_power` traps; LM3532 register facts; the QNX-side I²C oracle (§0.1) |
| **`122-re-bridge-reset-2026-10-27.md`** | static sweep of the whole QNX boot set for anything that could reset/enable the bridge — found `GPIO2_24` (20 ms active-low pulse) and proved the rest clean |
| **`118-live-qnx-display-registers-2026-10-27.md`** | the reference display mode, measured live: every DISPC/DSI register, the DSS power-domain sequence, the rails, the GPIO map.  **§8.2 and §3 corrected by 121** |
| **`117-re-display-r1-2026-10-27.md`** | static RE of the mode programming (`0x1cce0` is a defaults initialiser; writers are `0x18a7c`/`0x179dc`/`0x16610`) |
| **`119-re-pmic-regen-2026-10-27.md`** | `pmic-twl6030-ctl` does **no I²C**; `regen1` = GPIO1_2; `vaux3` = `0x48[0x8E]←0x21` |
| `111`, `112` | original display RE + bench probes.  111 §3.5 is still the SPI TCON table; **111 §8 item 4 is superseded** (bridge needs no programming) |
| `109`, `116` | epoch test / VFS-panic discovery; RPMB-`BOOTCTR` plan (read-only for the agent) |

---

## 3. Tree state (what builds the current image)

| item | state |
|---|---|
| `omap4-winchester.dts` | `&dss`/`&dsi1` okay.  DSI host endpoint **`lanes = <0 1 2 3 4 5 6 7 8 9>`** (clock + 4 data — this was the big session-6 discovery); `bridge@0` = `toshiba,tc358764` with **`lanes = <4>`**, `hs-rate = <604800000>`, `lp-rate = <10000000>`, **`reset-gpios = <&gpio2 24 GPIO_ACTIVE_LOW>`**, `pinctrl-0 = <&bridge_pins>`.  Top-level `panel-lvds` with the measured timing and **`backlight = <&backlight>`**.  GPIO2: `panel-en-hog` (line 16, the LM3532 gate) + six more hogs (9/12/13/28/29 — line 24 is the reset now, not a hog).  GPIO1: hogs on 2/6/14.  `panel_vdd` = plain always-on `regulator-fixed` (probe ordering only).  `&vaux1/2/3` forced to 2800 mV (`min == max`).  `&lm3532` okay with `vin-supply = <&panel_vdd>`; `led-backlight` at 255.  `&mcspi4` okay + `tcon@0` spidev + `aliases { spi3 = &mcspi4; }`.  19 other nodes disabled on purpose, each with a comment. |
| `bootargs` | `earlyprintk keep_bootcon ignore_loglevel maxcpus=1 clk_ignore_unused panic=10 fw_devlink=permissive` |
| `.config` | `DRM=y`, `DRM_OMAP=y`, `DRM_PANEL_LVDS=y`, `DRM_TOSHIBA_TC358764=y`, `LEDS_LM3532=y`, `BACKLIGHT_LED=y`, **`SPI_SPIDEV=y`**, **`PWM_TWL=y`, `PWM_TWL_LED=y`**, `TWL6030_GPADC=m` (parent `IIO=m`, cannot be `=y`), `CMDLINE_FROM_BOOTLOADER=y` |
| `drivers/gpu/drm/bridge/tc358764.c` | board patches: optional `reset-gpios`; `lanes`/`hs-rate`/`lp-rate` from DT; 20 ms reset pulse; **`tc358764_init()` reduced to a log — it does not program the bridge** (§1); `DSI_CTRL` untouched here |
| `drivers/gpu/drm/omapdrm/dss/dsi.c` | one board quirk: `CS_RX_EN = 0`, `VP_CLK_POL = 1` in the video-mode `DSI_CTRL` setup (matches QNX's `0x00EAEF9D`) |
| `analysis/initramfs/console.c` | commands: `help ls cat peek poke kick disp bl i2c i2c16 spi fb kmsg write reboot`.  Everything that touches hardware runs **forked with a bounded wait**.  Build: `arm-linux-gcc -static -O2 -o root/console console.c` |
| images | `chainloader/kernel.img` crc **`0xE1F79294`** = the current best build.  Kept: `kernel-0xD90E0213-backlight.img`, `kernel-0x4B97D66C-pwm-y.img`, `kernel-0xCEB270AC-vaux2v8.img`, `kernel-safe.img` `0x7777931C`, `kernel-drm-ok.img` `0x33BF3664`.  **Do not reuse `0x5E40C5C1`** (2 lanes + sync-pulse: no display at all) or **`0x3B04298E`** (LM3532 without VAUX3).  crc = `zlib.crc32(data[:-4])`, the last word of the container |
| host tools | `analysis/pbcon.py`, `analysis/pbssh.sh`, `analysis/build_kernel_image.py`, `analysis/qnx-i2c.c` → `pbi2c`, `analysis/pre_os_ifs_flash.py`, `playbook-live-spare-2026-10-26-clean/force_reset_loader.py` |
| patch set | **refreshed at session-6 close:** `analysis/clean-kernel-6.15.11-changes.patch` and `playbook-dev/kernel-patches/0001-playbook-winchester-6.15.11.patch` + `winchester.config`, **29 files / 2 922 lines**, both verified with `patch -p1 --dry-run` against `/home/psyden/SSDdrive/wsl-migration/home/psyden/kernel/pristine`.  Regenerate with bootstrap §5 if the tree changes again |

---

## 4. Command cheat-sheet (what we actually run)

### 4.1 Access (QNX / TabletOS)

```bash
# after a HOST reboot, the door supervisor is gone — start it first
pgrep -af door-hold || nohup /home/psyden/rea-work/door-hold.sh >/dev/null 2>&1 &

cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
for i in $(seq 1 40); do timeout 12 ./pbssh.sh 'echo DOOROK' 2>/dev/null \
    | grep -q DOOROK && { echo "door up ($i)"; break; }; sleep 5; done
timeout 20 ./pbssh.sh 'uname -a'
```

`pbssh.sh` = `ssh -i ~/rea-work/rsa` + `HostKeyAlgorithms=+ssh-rsa`,
`KexAlgorithms=+diffie-hellman-group1-sha1`, `Ciphers=+aes128-cbc`,
`root@169.254.0.1`.  **"Connection refused" = the door, not the network.**
The QNX shell has no `head xxd od md5sum cmp bc python`; it does have `tail dd
strings`, and `python2.7` + `awk` + `printf` + `sed` once the devuser overlay is
up.  `dd if=/dev/hd1 bs=512 skip=2048 count=5632` is the sealed IFS (5 632 blocks
= `0x2BFE10` bytes) — hash it host-side.

### 4.2 The Linux console (test image running)

```bash
ls /dev/ttyACM0 && timeout 15 pkexec chmod 666 /dev/ttyACM0   # after EVERY boot
PBCON_TMO=10 ./pbcon.py 'disp stat'        # PBCON_TMO is a per-command timeout
```

Commands (`./pbcon.py '<cmd>'`): `help`, `ls <path>`, `cat <path>`,
`peek <hex> [n≤64]`, `poke <hex> <val>`, `kick on|off`, `reboot`,
`disp init|stat|mux|col|off`, `bl init|dump|rd|wr|scan [bus]`,
`i2c r|w <bus> <addr> <reg> [val]` (8-bit reg), `i2c16 r|w <bus> <addr> <reg16>
[val] [n]` (16-bit reg), `spi tcon` / `spi w <hexword>…`,
`fb <rrggbb>` (paint /dev/fb0 — **always do this before judging the panel**),
`kmsg [substr]` (kernel log, filtered), `write <path> <text>` (sysfs).

`/init` reboots the unit 180 s after start, so a console session has a time
budget; a warm reset re-boots the media image, so re-boots are free.

### 4.3 Build → wrap

```bash
C=/home/psyden/rea-work/playbook-dev/kernel-clean
export PATH=/home/psyden/SSDdrive/wsl-migration/home/psyden/toolchains/armv7-eabihf/bin:$PATH
# if console.c changed, build it INTO the initramfs tree first:
(cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis/initramfs && \
 arm-linux-gcc -static -O2 -Wall -o root/console console.c)
rm -f $C/usr/initramfs_data.cpio*        # else the old initramfs is reused
make -C "$C" ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make -C "$C" ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
ZIMAGE=$C/arch/arm/boot/zImage \
DTB=$C/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb \
python3 build_kernel_image.py            # prints the crc, writes chainloader/kernel.img
```

### 4.4 Flash a test build (from **media running**, e.g. TabletOS)

```bash
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
lsusb | grep -i 0fca                      # want 0fca:8010; 0fca:0001 = bootrom, 8001 = ramloader
nohup python3 pre_os_ifs_flash.py --template-ifs chainloader/kernel.img > /tmp/f.log 2>&1 &
sleep 3
timeout 30 ./pbssh.sh '/proc/boot/shutdown -S reboot'      # or pbcon 'reboot' from Linux
until grep -q 'done (' /tmp/f.log; do sleep 2; done; tail -2 /tmp/f.log
# leave the ramloader promptly — retry, because the first attempt races enumeration:
for t in 1 2 3 4; do case "$(lsusb | grep -i 0fca)" in
  *8001*) timeout 40 python3 playbook-live-spare-2026-10-26-clean/force_reset_loader.py;;
  *8010*) break;; esac; sleep 5; done
for i in $(seq 1 40); do ls /dev/ttyACM0 >/dev/null 2>&1 && break; sleep 2; done
timeout 15 pkexec chmod 666 /dev/ttyACM0
```

### 4.5 Restore + verify (do this before ending a session)

```bash
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
nohup python3 pre_os_ifs_flash.py \
  --template-ifs playbook-live-spare-2026-10-25/ifs0.sealed.bin > /tmp/f.log 2>&1 &
sleep 3; ./pbcon.py 'reboot'                    # or ./pbssh.sh '/proc/boot/shutdown -S reboot'
until grep -q 'done (' /tmp/f.log; do sleep 2; done
for t in 1 2 3 4; do case "$(lsusb | grep -i 0fca)" in
  *8001*) timeout 40 python3 playbook-live-spare-2026-10-26-clean/force_reset_loader.py;;
  *8010*) break;; esac; sleep 5; done
for i in $(seq 1 60); do timeout 12 ./pbssh.sh 'echo DOOROK' 2>/dev/null | grep -q DOOROK && break; sleep 5; done
./pbssh.sh 'dd if=/dev/hd1 bs=512 skip=2048 count=5632 2>/dev/null' > /tmp/ifs.bin
python3 -c "import hashlib;print(hashlib.sha256(open('/tmp/ifs.bin','rb').read()[:0x2BFE10]).hexdigest())"
# expect c72b053027769c4d49270e9ee208fd2b0afcf69ce4fb3cf0c0be3c938ee05299
./pbssh.sh 'dd if=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 2>/dev/null' | xxd -p   # expect 32000000
```

### 4.6 Useful reads on a live reference (QNX)

```bash
./pbssh.sh 'on -C 0 /accounts/devuser/memdump3 4a100000 0x200'   # CORE padconf
./pbssh.sh '/proc/boot/pmic-twl6030-ctl get_state vaux3'          # rails (read-only!)
./pbssh.sh 'cat > /tmp/pbi2c' < pbi2c && ./pbssh.sh 'chmod +x /tmp/pbi2c; /tmp/pbi2c scan 0'
```

`pbi2c` (source `qnx-i2c.c`, built with the qnx660 toolchain) does
`probe / rd / wr / scan / dump` on `/dev/i2cN` — the fastest way to settle an
I²C question on the reference.  `/tmp` on QNX is wiped by a reboot; re-push it.

---

## 5. Traps (long-form: bootstrap §6, report 120 §3, report 121 §3/§11/§12)

14. A **write to a DSS target in module standby never completes** — it wedges the
    interconnect and PID 1's `reboot()`.  Let `omapdrm` own the DSS; don't poke it.
15. `/init`'s 180 s auto-reboot is only reliable while no bus cycle is wedged.
16. **Blocking I²C is the same hazard.**  Everything that touches I²C runs forked
    with a bounded wait; a kernel probe at 0x38 while the slave was gated did it too.
17. **A stale door holder looks like `Connection refused`** — after a *host* reboot
    the supervisor is gone entirely (start `door-hold.sh`); after a *unit* reboot
    the holder can be stale (kill it by explicit pid, never `pkill -f`).
18. Driving `regen1` (GPIO1_2) without detaching it from the PMIC first
    (`i2c w 0 48 af e0`, `i2c w 0 48 ad 00`) wedges the I²C bus.
19. A `slay` test does not work on this backlight — the LM3532 latches brightness.
20. **GPIO2 is reset by our boot path; GPIO4 is not.**  Never assume a
    reference-high line is high on Linux; always read the pad mux.
21. **A GPIO write to an un-muxed GPMC ball is a silent no-op.**  Check
    `peek 4a1000XX` → `[2:0] == 3` before believing any GPIO test.
22. **`regulator-fixed`'s GPIO loses the race with async probe** — use a gpio-hog
    for anything that must be true before a device probes.
23. **`bl_power = 4` silently swallows brightness writes** (`actual_brightness`
    still echoes your value).  The panel's `backlight = <&backlight>` link fixes it.
24. **`/dev/ttyACM0` is recreated per enumeration** — chmod after every boot.
25. **The flasher's bootrom window is 300 s** and it exits silently.
26. **`pmic-twl6030-ctl set_state vaux1 off` takes the unit down** (needed a
    power cycle).  Read rails, never toggle VAUX1.
27. **A flash cycle's precondition is media running, not the ramloader** — in
    `0fca:8001` there is no `/dev/ttyACM0`, so the console reboot is a silent
    no-op and the flasher waits out the ramloader's ~90–120 s timeout (that is
    the "two minutes with the green LED").  End every flash with
    `force_reset_loader.py` (retry it), or use `--reuse-loader` — see trap 30.
28. **Panel-chain writes outlive a warm reset and QNX does not undo them.**  A
    wrong-byte-order TCON table and a full `SYS_RST <- 0x1f` both left the
    *reference* display with faint vertical artifacts, cleared only by a **cold
    battery pull**.  Prefer `spi tcon`; never `SYS_RST`.
29. **A file-existence check does not prove an overlay tool runs.**  `dd` lived in
    the devuser overlay and `-x` passed while every exec failed with
    `cannot execute` (its interpreter `/usr/lib/ldqnx.so.2` is created at boot
    too).  Test by **running** it.
30. **`--reuse-loader` can fail** (`set_mode(2) failed`) and strand the bootrom
    with a **red blinking LED**; recover with a plain `--template-ifs <sealed>`
    flash, no power cycle needed.
31. **Non-burst DSI video mode is rejected at this pixel clock** —
    `[drm] Cannot find any crtc or sizes`, no `/dev/fb0`, DSS stays in standby.
    Keep BURST.
32. **`pgrep -f`/`pkill -f` match your own command line** — use
    `ps -ef | grep '[p]re_os_ifs_flash'`.

---

## 6. Kickoff prompt for session 7 (paste this)

```
PlayBook (OMAP4430 "winchester") clean Linux 6.15.11 bring-up — session 7.

GOAL: an image on the DSI->TC358764->LVDS panel (1024x600).  Everything else in
the boot already works: DDR, eMMC, USB gadget + console, initramfs, omapdrm.

READ FIRST, in this order:
1. bootdumps-2026-09-11/analysis/HANDOFF-2026-10-27-session6.md   (state, next steps, commands)
2. bootdumps-2026-09-11/analysis/NEXT-SESSION-BOOTSTRAP-2026-10-26.md (the manual)
3. .../121-spi-tcon-panel-2026-10-27.md     (the panel work; §11 = the 4-lane find)
4. .../120-lm3532-panel-enable-2026-10-27.md (the backlight, solved)
5. .../122-re-bridge-reset-2026-10-27.md     (why the bridge is not a variable)

WHERE WE ARE: the panel is LIT but black.  The host side now matches the
reference in every measured register:
  - FOUR DSI data lanes (COMPLEXIO_CFG1 = 0x2A054321, same as QNX);
    1024x600x24bpp@60 = 1.2096 Gbps => 302 Mbps/lane => 37.8 MHz byte clock.
    (The old two-lane build ran 75.6 MHz — that was the whole "2x" confusion.)
  - DSI_VM_TIMING3 = 0x03F00258 (TL 1008), DSI_CTRL = 0x00EAEF9D (both = QNX),
    PLL locked, DSI_IRQSTATUS 0x80, COMPLEXIO_IRQ_STATUS 0, PACKET_SENT.
  - DISPC scanning a painted framebuffer; LM3532 backlight on (0x1d=0xf9,
    0x74=0xff, bl_power=0).
  - TC358764 needs NO programming (proved); the IFS splash does DISPC+DSI+TCON+
    backlight with it untouched.  RESETN is GPIO2_24 (20 ms active-low).
  - TCON table sent in both byte orders and both orderings: no effect.

FIRST TASK: force the COMPLETE QNX timing set as one experiment (never a partial
mix) and see whether the panel lights:
  DISPC_TIMING_H  0x0890B400 -> 0x0890A113   (HBP137/HFP180/HSW0 -> 137/161/19)
  DISPC_DIVISOR   0x00010003 -> 0x00010002   (LCD 3 -> 2)
  DSI_VM_TIMING1  0x00086067 -> 0x01079074   (103/134/0 -> 116/121/1)
  DSI_CLK_CTRL    0xA0306008 -> 0xA0346005   (LP divisor 8 -> 5, +bit 18)
If that does nothing: compare DSI_PHY_CFG* with QNX's (118 §3.3), then question
the LVDS mapping (driver writes VESA-24).

BENCH: the current build is chainloader/kernel.img crc 0xE1F79294.  Console
commands are in HANDOFF §4.2 (note: fb <rrggbb> before judging the panel, and
kmsg [substr] instead of guessing).  The flash loop is HANDOFF §4.4 — start the
flasher immediately before the reboot, retry force_reset, chmod /dev/ttyACM0
after every boot.  Restore the sealed IFS (sha256 c72b0530...) when done.

DO NOT: re-open the backlight; try to program the bridge (and never SYS_RST
0x1f — it degrades the reference display until a battery pull); use non-burst
mode (the modeset is rejected); toggle VAUX1 on the reference; write RPMB or the
eMMC.  Budget ~20 boots against BOOTCTR_FCTRY (0x32 now).

SUBAGENTS: static RE only (no bench).  Report skeleton FIRST, exact paths + TRM
text + ARM binutils path, explicit "stop when answered".  If the docs conflict
or something is genuinely unclear, ask the operator to relay the question to the
previous session's agent — that channel works and has corrected several wrong
premises.

UPDATE THE DOCS as you go; this handoff + the bootstrap are the entry points.
```

---

## 7. Subagents, and the question-relay channel

**Subagents: static work only.**  Right tool for RE of the QNX binaries
(`rizin` / `arm-linux-objdump` over `playbook-dev/dumped4869ifs/proc/boot/` and
`/home/psyden/SSDdrive/4869/berry-rcfs/`, plus the sealed IFS and TRM text), for
large document sets, and for anything that would eat main context.  They are
**not** bench actors — only one process may own the USB link.

Briefing rules that cost a whole agent each time they were broken:

1. **"Write the report file first, as a skeleton, then improve it in place."**
2. **Exact paths, reference documents, toolchain path, a style reference, an
   explicit "stop when the question is answered", and what NOT to touch.**
   `122` and `119` are good templates.

**The relay channel is real and has earned its keep.**  When something is unclear
or two docs contradict each other, say so and ask the operator to relay the
question to the previous session's agent.  Ask crisply: **one question, the
evidence, and what you would do either way** — and **when the answer contradicts
a doc, fix the doc in the same turn.**

Session 6 used it four times, and every answer changed the plan:
* the control port is DSI-generic, not I²C → stopped an I²C hunt;
* the 2× link rate was *not* benign → forced the lane-count check that found the
  four-lane truth;
* nothing programs the bridge → retired it as a variable (and killed a planned
  VC-0 experiment);
* the IFS splash proves the chain works with the bridge untouched → the
  remaining difference is in DISPC/DSI timing fields only.
