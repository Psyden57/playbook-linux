# Handoff — session 42 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 42 = Linux-era 36**, updated at its
wrap-up.  See `docs/sessions/NUMBERING.md`.

> **Session-42 result, in one line:** **E1 is done and the transmit path is
> proven** — with the codec as AIF1 master (`R0x302 = 0x4000`) and the McBSP as
> a clock consumer (`PCR0 = 0x000f`, SRG out of the path), a stream with a live
> AIF1CLK drains the FIFO end to end (`pbplay` ran 20 s cleanly; the old
> configuration always died at ~1 s).  **The one remaining blocker is a live
> AIF1CLK**: FLL1 never locks from MCLK1 (register file byte-identical to the
> working reference) and AIF1CLK sourced directly from MCLK1 is dead — the pin
> does not clock.  Report **206**.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain, the 64 MiB ext4 BusyBox root in region
1, the peripherals (I²C4 motion sensors, TMP103, BQ27541, FAN5702, CM3217,
ISL9519 charger), the **Munin touch** (calibrated, IRQ wired — see §0d), and
the **WM8994 codec chain up to a proven transmit path**.

**The audio blocker is now one thing: a live AIF1CLK.**  See §0c.

**The patch** `kernel/patches/0001-playbook-winchester-7.3.3.patch` is the
session-42 export (**54 files / 8 095 lines**, sha256 `55035aac…`, 0 rejects
and 54/54 byte-identical).  It carries the E1 clock-role changes (DTS pads,
machine driver).

## 0a. The media, and the way back

* **region 0 = our Linux image `0x36597040`** (`0x93A2EC` bytes; kernel `7.3.3`;
  the E1 clock roles — codec AIF1 master, McBSP consumer, input pads).
* **region 1 = the E1 audio/OSS rootfs** `b20302dc…`
  (`local/images/rootfs-region1-audio-oss.ext4`, 27 modules, `pbplay`/`pbmix`).
* The unit is **on Linux** (`0fca:8010`, console `pb> `), not TabletOS.
* **The way back to TabletOS** is the 1917 IFS
  (`local/work/ifs-shim-sealed.ifs`, sha256 `b52195bb…`, 489 frames) at block
  `0x825`, region 0 only:
  ```bash
  nohup python3 tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs \
      --dest-block 0x825 --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
  # then, from the running Linux image:
  tools/pbcon.py 'reboot -f'
  ```
  After the flash, `tools/bbusb.py reset-loader` is sometimes needed (it may
  report "no ramloader enumerated" — just wait, the unit boots anyway).
  **Do not restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).

## 0b. Item 9 step 4 (WL1283) — software surface exhausted

Unchanged and **formally stopped by the operator** (session 36).  The 1917-truth
minimal `&mmc5` host probes but `attach_sdio no` (report 196).  Do not re-run
traps 116–137.

## 0c. Item 9 step 5 — audio: one blocker (a live AIF1CLK), three routes

**Constraint (operator, session 41 end): there is no scope, no boardview and no
pad-level knowledge.  Every experiment must be run from the bench (the unit
itself) or from static RE.  Nothing may depend on probing a pin.**

### What is already proven (do not re-derive)

* **The codec chain is driver-driven** (session 41): thaw, FLL, AIF1CLK,
  word length, PM3 output mixers, charge pump, DAC routing/un-mute, output
  stage — the DAC audibly converts (noise floor, pops, the start-up sweep).
* **The transmit path works in consumer mode** (session 42, E1): codec AIF1
  master (`R0x302 = 0x4000`), McBSP clock consumer (`PCR0 = 0x000f`, SRG never
  started, `clkx`/`fsx` pads = `0x0100` inputs).  With a live AIF1CLK
  (free-running NCO) `pbplay 20 440` runs the full 20 s and exits cleanly;
  the old McBSP-master configuration always died with
  `Input/output error` after ~1 s.  **The session-41 `XRDY = 0` stall is a
  property of the master/SRG configuration, not of the silicon.**
* **MCLK1 does not clock at the pin.**  FLL1 never locks from MCLK1 with a
  register file byte-identical to the working reference
  (`0x220 = 0x0005, 0x221 = 0x0700, 0x222 = 0xAE14, 0x223 = 0x00E0,
  0x224 = 0x0C80`; `R0x732 = 0x6000`), reproduced with the reference's enable
  timing and 2–3 s settles.  The discriminator: with the FLL bypassed and
  `AIF1CLK` sourced **directly from MCLK1** (`R0x200 = 0x0001`), the stream
  dies like the FLL case.  `AUXCLK1 = 0x00020100` and pad `0x19C` low
  `0x0000` re-verified on the session-42 boot — the DTS fix holds.  Caveat:
  session 41 measured one successful MCLK1 lock, so "marginal" is still live.
* The free-running NCO (datasheet Rev 4.6 p. 206: `OSC_ENA = 1`,
  `OUTDIV = ÷8`, `FRC_NCO = 1`, `FRC_NCO_VAL = 0x19` ≈ 12 MHz, ±10–30 %) is a
  switch that makes AIF1CLK exist — the mechanism probe of session 42, not a
  hi-fi clock.

### The three routes forward (operator's call)

1. **MCLK1 at the hardware layer** — a probe/meter on `fref_clk2_out`/MCLK1.
   Blocked by trap 165 while no probe exists.  If one ever appears, this is a
   two-minute measurement that decides everything.
2. **A non-reference AIF1CLK design** — route some other live clock to the
   codec (no reference precedent; a new design).
3. **The master-mode SRG stall, now narrow.**  In McBSP-master mode the codec
   was slave with a BCLK-referenced FLL that locked every time — so BCLK ran,
   and the codec consumed.  The stall was between the SRG and the shifter.
   The shifter is now proven good (it works when externally clocked), so the
   question is the SRG's output distribution: `CLKSM`/`FSGM`/`CLKGDV`
   permutations, one boot at a time, judged by `pbplay`'s survival (trap 169)
   and the steady-state dump.  E2's capture stream (`/dev/dsp` read alongside
   playback) remains the tool if audio is heard but wrong.

### Driver facts you need before touching it

* The repair runs from a **delayed work 50 ms after `trigger(START)`** and
  begins with **`regcache_mark_dirty()` + `regcache_sync()`** (trap 162).
* `hw_params` writes `R0x200 = 0` to thaw, so the repair re-enables
  `AIF1CLK` last.
* The FLL is referenced from **MCLK1** since E1 (BCLK would be circular with
  the codec as master) — and that reference never arrives, which is the whole
  problem.
* A frozen codec drops **raw `i2ctransfer` writes too** (trap 167): thaw
  (`R0x200 = 0`) before any register surgery, re-enable AIF1CLK last.
* The driver's `set_pll()` clears `FRC_NCO` (trap 168): a hand-forced NCO must
  be injected after `trigger(START)` + the repair, or the driver must be
  changed.

## 0d. Touch — done, and it survives a warm reboot; cold boots need the pulse

I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` boots the Munin app
and maps the 1917 `scaling.conf` calibration to 1024×600; the GPIO2_3 edge IRQ
works (pad `0x056`, `0x410B`, `irq 110`).  **A warm reset keeps the controller
up** (session 42: the touch bound and worked after the flash reboot); after a
**cold** boot it does not (`0x65` NAKs, `touch did not boot: -110`) until the
power/reset GPIOs (**GPIO1_14 / GPIO4_8**) are pulsed — the driver still does
not drive them.  That pulse is the remaining work, and it is the recovery path
for cold boots.

## 1. Useful local extracts

* **`local/work/1917/base/`** — the whole 1917 `/base` (333 MB).
* **`local/work/1917/ifs/`** — the 1917 IFS tree + `proc/boot/startup-playbook`.
* **`local/work/1917/calib/`**, **`local/work/1917/parts/`** — WLAN calibration
  and pulled partitions.
* **`local/vendor/bb10mt/bb10mt`** — the QNX6/container tool.
* **The WM8994 datasheet** — Rev 4.6; the authority for clocking, PM3/PM4/PM5
  and the charge pump.  Fetch from
  `https://statics.cirrus.com/pubs/proDatasheet/WM8994_Rev4.6.pdf` (the FLL and
  free-running-NCO sections are around p. 201–208; session 42 used them).
  A session-scoped text extraction does **not** survive between sessions.
* Reports **201**, **203**, **204**, **205** and the current **206**; the
  reference captures in `docs/captures/`.

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

The printed CRC must differ from the previous build (trap 39).  **Region 1
rebuild**, and the two-region flash:

```bash
python3 tools/build_rootfs.py --size-mb 64 \
    --modules local/work/modules-7.3.3 --krel 7.3.3 \
    --out local/images/rootfs-region1-audio-oss.ext4
printf 'boot/chainloader/kernel.img=16,0\nlocal/images/rootfs-region1-audio-oss.ext4=8448,0\n' > /tmp/both.lst
python3 tools/bbusb.py pack -l /tmp/both.lst -v 1,2 -s -f -c /tmp/both.signed
nohup python3 tools/bbusb.py flash --signed /tmp/both.signed \
    --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
tools/pbcon.py 'reboot -f'
```

**Copy the rebuilt `.ko` into `local/work/modules-7.3.3/` first** — session 41
lost a flash to a stale staged module.  **Trap 163.**

## 3. Bench

* **The unit is on our Linux image** (`0fca:8010`).  There is **no door**: use
  `tools/pbcon.py` (prompt `pb> `), reboot with `tools/pbcon.py 'reboot -f'`.
  `pkexec chmod 666 /dev/ttyACM0` **only after a genuine re-enumeration** — it
  does nothing for a busy shell, and the node is recreated per enumeration.
* **The console is not wedged when it echoes but prints nothing** — the target
  is still inside a foreground command.  `pbcon.py` now says so; `tools/pbraw.py
  N 0a` dumps raw bytes, and `tools/pbraw.py 2 03` sends Ctrl-C.  **Trap 164.**
* **On TabletOS** the door is `169.254.0.1`; host link `enp8s0f3u1` carries
  `169.254.0.2/16` (do not re-add).  **Do not start `door-hold.sh` yourself if
  the operator has one running** — stale holders cause an auth loop; the
  operator restarts it.  `tools/pbssh.sh` logs in as `root` (`uid:0`).
* Device password `0000`, once.  An agent **never writes RPMB**; NVRAM is
  read-only.  A test boot is not a budget concern (the Linux `/init` tops
  `BOOTCTR_FCTRY` up at ≤`0x1e`).  After every reboot, read the last 100–200
  lines of `dmesg`.
* QNX device tools must be **`-static`** (trap 131): build `tools/pbi2c` and
  `tools/memdump3` with `$QNX_HOST/usr/bin/arm-unknown-nto-qnx6.6.0eabi-gcc
  -static`.  **`/tmp` is wiped by a reboot** — re-push after every one.
* `tools/pbssh.sh` needs `/proc/boot/pmic-twl6030-ctl` **without** `printf`/`tr`
  (QNX has no such binaries) — use `echo` and separate invocations.

## 4. Traps added last session

Full list 14–169 in `KNOWN-ISSUES.md` §2.  The session-42 additions:

* **166.** `i2ctransfer` arguments are **decimal** — a shell loop over `$r`
  reads the wrong register (`732` → `0x2DC`).  Write the address bytes
  explicitly in hex.
* **167.** A **frozen codec drops raw `i2ctransfer` writes too** — thaw
  (`R0x200 = 0`) before any register surgery; a half-landing sequence looks
  like a flaky device.
* **168.** The driver's `set_pll()` **clears `FRC_NCO`** — a hand-forced NCO
  does not survive the next `hw_params`; inject after `trigger(START)` + the
  repair.
* **169.** `pbplay`'s **`Input/output error` after five periods is a
  stream-health observable**: a run that lives past it proves the ring drains.

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| reference codec captures | `docs/captures/wm8994-reference-*` |
| 1917 `/base`, IFS, calibration | `local/work/1917/{base,ifs,calib,parts}/` |
| 1917 audio driver (RE'd) | `local/work/1917/base/lib/dll/deva-ctrl-omap4abe-wm8994.so` |
| touch driver | `local/kernel-tree/drivers/input/touchscreen/playbook-munin.c` |
| audio machine driver | `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` |
| McBSP driver (patched) | `local/kernel-tree/sound/soc/ti/omap-mcbsp.c` |
| OSS player / mixer | `rootfs/pbplay.c`, `rootfs/pbmix.c` |
| console diagnostics | `tools/pbcon.py`, `tools/pbraw.py` |
| reports | `docs/reports/` (205 is the audio record) |
| sessions | `docs/sessions/session-41.md` (last session) |

## 6. Working rules

* After every console command, dump the log tail and read it.  After a reboot or
  a structural change, **check the last 100–200 lines of `dmesg`**.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**
* **A test boot is not a budget concern.**
* **Never present a guess as a fact.**  External facts are looked up and cited;
  device facts prefer a fresh read over an old note.
* **Use the reference before the bench** — and note that session 41's single
  highest-yield act was **diffing our live register file against a live capture
  of the working reference**.  Do that first for any new peripheral.
* **When a register "reverts", find out which writer did it** (driver, DAPM,
  `hw_params`, `trigger`, regmap cache) before concluding the hardware ignored
  it.  That mistake cost most of session 41.
* Subagents: static/file-only, never a `tools/` script (trap 109).
* **No restore is required at wrap-up.**  Leaving the unit on Linux is valid.

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 43 (absolute; Linux-era 37)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), docs/history/KERNEL-PORT.md §7, ROADMAP.md item 9 (step 5),
KNOWN-ISSUES.md (traps 14-169, especially 165-169, and §3), DEVELOPMENT.md
§3/3a/4/7a, DECISIONS.md (44-53), docs/reports/INDEX.md, report 206 (the E1
record; 205 is the session-41 codec record), and sessions/session-42.md.

WHERE IT STANDS: the WM8994 transmit path is PROVEN working.  E1 inverted the
clock roles to match the reference — codec AIF1 master (R0x302=0x4000), McBSP
clock consumer (PCR0=0x000f, SRG out of the path, clkx/fsx pads = 0x0100
inputs) — and with a live AIF1CLK (forced from the codec's free-running NCO,
datasheet Rev 4.6 p.206) pbplay streamed a full 20 s and exited cleanly; the
McBSP-master configuration always died with "Input/output error" after ~1 s.
The session-41 XRDY=0 stall is a property of the master/SRG configuration, not
of the silicon.  THE ONE REMAINING BLOCKER IS A LIVE AIF1CLK: FLL1 never locks
from MCLK1 (register file byte-identical to the working reference:
0x220=0x0005, 0x221=0x0700, 0x222=0xAE14, 0x223=0x00E0, 0x224=0x0C80; 0x732
stays 0x6000), and the discriminator — AIF1CLK sourced DIRECTLY from MCLK1
with the FLL bypassed (R0x200=0x0001) — also dies, so THE PIN DOES NOT CLOCK.
AUXCLK1=0x00020100 and pad 0x19C are correct on every measured boot; SCRM,
padconf mode, SMART3 drive, DPLL_PER, all PMIC rails and the FLL file are
byte-identical to the working reference, so the fault is below the registers.
Caveat: session 41 measured ONE successful MCLK1 lock, so "marginal clock" is
still live.  The NCO is ±10-30% — a mechanism probe, NOT a hi-fi clock; do not
ship it in the driver.  The touch now survives warm reboots (cold boots still
need the GPIO1_14/GPIO4_8 pulse).  The WL1283 is software-exhausted.  The
kernel patch WAS re-exported at session-42 wrap-up (54 files / 8 095 lines,
sha256 55035aac..., verified 0 rejects / 54/54 byte-identical).

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE 0x36597040 (0x93A2EC bytes,
kernel 7.3.3, E1 clock roles); region 1 is the E1 audio/OSS rootfs b20302dc...
(27 modules).  The way back to TabletOS is the 1917 IFS
(local/work/ifs-shim-sealed.ifs) at block 0x825.  Do NOT restore
local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

WHERE IT IS — THREE ROUTES (operator's call; trap 165 still applies — no scope,
no boardview, no pad knowledge, nothing may depend on probing a pin):
(1) MCLK1 at the hardware layer — a probe/meter on fref_clk2_out/MCLK1 decides
everything if one ever exists; blocked until then.
(2) A non-reference AIF1CLK design — route some other live clock to the codec.
(3) The master-mode SRG stall, now NARROW: in the old master config BCLK ran
(the codec's BCLK-referenced FLL locked every time) and the codec consumed, so
the stall was between the SRG and the shifter; the shifter is now proven good
(externally clocked it streams), so it is the SRG's output distribution —
CLKSM/FSGM/CLKGDV permutations, ONE per boot, judged by pbplay's survival
(trap 169: dying at ~5 periods = stalled; running the full duration =
draining) and the steady-state dump.  E2's capture stream (/dev/dsp read
alongside playback) is the tool if audio is heard but wrong.

BENCH: the unit is on OUR LINUX IMAGE — there is NO door.  Use tools/pbcon.py
(prompt `pb> `), reboot with tools/pbcon.py 'reboot -f'; run
`pkexec chmod 666 /dev/ttyACM0` ONLY after a genuine re-enumeration.  A console
that echoes but prints nothing is a BUSY SHELL, not dead — tools/pbraw.py shows
the raw bytes and `tools/pbraw.py 2 03` sends Ctrl-C (trap 164).  On TabletOS:
door = 169.254.0.1 (host link 169.254.0.2/16 on enp8s0f3u1 — do not re-add); do
NOT start door-hold.sh if the operator already has one running (stale holders
cause an auth loop); tools/pbssh.sh logs in as ROOT (uid:0).  Run USB/flash
tools as the USER, never pkexec.  Device password 0000, once.  An agent NEVER
writes RPMB; NVRAM is read-only (SAFETY §1/§6).  After every reboot/change,
check the last 100-200 lines of dmesg.  WM8994 register work over i2ctransfer:
arguments are DECIMAL (trap 166), and a frozen codec drops raw writes too
(trap 167 — thaw with R0x200=0 first, re-enable AIF1CLK last).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4 (open_binary,
binary_overview, analyze_function, procedure_pseudo_code, search_strings,
xrefs, read_bytes); raw ARM import format=raw-binary, architecture=arm,
entry_point=0x80100000.  Reconcile with ntoarmv7-readelf/objdump (Ghidra may add
+0x10000 on an ELF).  QNX device tools build -static, and /tmp is wiped by a
reboot.  USE THE REFERENCE FIRST — and the highest-yield method is a LIVE
REGISTER DIFF against a capture of the working reference (docs/captures/).
The WM8994 Rev 4.6 datasheet:
https://statics.cirrus.com/pubs/proDatasheet/WM8994_Rev4.6.pdf (FLL /
free-running NCO ≈ p.201-208).

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
A test boot is not a budget concern (the counter self-replenishes; NOTE the
chroot has no /dev/rpmb node — the counter is read from the initramfs console,
not the BusyBox shell), but the hard hazards (0 / >0x32 / failed write / flat
pack) remain.  Operator's call when to stop and to wrap up.

NEXT (operator's call): (a) MCLK1 hardware-layer question (needs a probe);
(b) a non-reference AIF1CLK design; (c) the master-mode SRG stall, one SRG
register permutation per boot; (d) touch cold-boot power/reset
(GPIO1_14/GPIO4_8); (e) the wl128x-nvs.bin path (hd5).  The unit is on Linux;
region 0/region 1 both change when the kernel/rootfs pair does, otherwise flash
only the changed region; the 1917 IFS at block 0x825 is the way back to
TabletOS.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note — and move replaced or
resolved material to docs/history/ or docs/parked/ with a link stub.  Re-export
the kernel patch whenever the DTS or a driver changes (trap 161) and verify it
by apply-and-compare, never by --dry-run alone.

WRAP-UP: only the operator decides when the session stops.  Record the bench
state, write the session note, update the canonical docs, verify with
tools/docs_audit.py and tools/tests/test_offline.py, then commit privately and
publish the snapshot (docs/project/REPO-MANAGEMENT.md).  No IFS restore is
required.
```
