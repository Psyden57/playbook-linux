# Handoff — session 42 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 42 = Linux-era 36**.  See
`docs/sessions/NUMBERING.md`.

> **Session-41 result, in one line:** the **WM8994 codec side of playback is
> solved** — thawed, FLL1 locked, AIF1CLK from FLL1, 16-bit, and the whole output
> chain (PM3 output mixers, charge pump, DAC→mixer, AIF1→DAC, output stage) is
> set by the machine driver from a cold boot, and the **DAC audibly converts**
> (its noise floor is heard).  Programme audio still does not appear because the
> **McBSP transmit FIFO never drains**: steady-state `DXR = 0` with
> `XRDY = 0`/`XEMPTY = 1`, while the sDMA runs at exactly the right rate.
> Report **205**.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain, the 64 MiB ext4 BusyBox root in region
1, the peripherals (I²C4 motion sensors, TMP103, BQ27541, FAN5702, CM3217,
ISL9519 charger), the **Munin touch** (calibrated, IRQ wired — but see §0d), and
now the **WM8994 codec up to the DAC**.

**The audio blocker is now the McBSP, not the codec.**  See §0c.

**The patch** `kernel/patches/0001-playbook-winchester-7.3.3.patch` is still the
session-39 export (**53 files / 7 773 lines**, sha256 `434d06c3…`).  **Session
41 changed `omap4-winchester.dts`, `playbook-wm8994.c` and `omap-mcbsp.c` in the
tree but did NOT re-export the patch** — regenerate it before publishing or
handing the tree on (recipe in `kernel/patches/README.md`; **`make clean` the
tree first**, trap 139).  Trap 161.

## 0a. The media, and the way back

* **region 0 = our Linux image `0xB8830701`** (`0x93A8F4` bytes; kernel `7.3.3`;
  the AUXCLK1 DTS fix and the McBSP steady-state dump).
* **region 1 = the audio/OSS rootfs** `7cc63990…`
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

## 0c. Item 9 step 5 — audio: one experiment, with a scope

### What is already proven (do not re-derive)

Under a live 48 kHz stream, all of this is set **by the driver** and holds:

```
PM1 0x0313  PM2 0x6310  PM3 0x03F0  PM5 0x0303
R0x04C 0x9F25   charge pump on
R0x02D 0x0001   R0x02E 0x0001   DAC -> MIXOUT un-muted
R0x420 0x0030   AIF1 DAC1 un-muted
R0x601 0x0001   R0x602 0x0001   AIF1 -> DAC1 routed
R0x200 0x0011   AIF1CLK from FLL1
R0x732 0x6020   FLL1 locked
R0x60  0x00EE   output stage connected
SRGR1 0x0f07    McBSP CLKGDV /8, 16-bit
SRGR2 0x101f    FPER 31 (32 BCLK/frame)
AUXCLK1 (0x4A30A314) = 0x00020100   12.8 MHz, from a cold boot
```

and the **DAC noise floor is audible**.  Programme audio is not.

### The one measurement that is missing

```
DXR   = 0x00000000      <- 32-bit transmit register, offset 0x8
DRR   = 0xc8xxxxxx
SPCR2 = 0x02f5          XRST=1  XRDY=0 (FIFO full)  XEMPTY=1 (shifter empty)
                        GRST=1  FRST=1
```
while `omap-dma-engine` interrupts advance **12/s** (= 48 000 / 4096) and
`hw_ptr` runs at **48 185 frames/s**.

**Do this:** put a scope on **`abe_mcbsp2_dx`** — CORE pad `0x0F8`, **high
half** — during playback and look for activity.

| result | meaning | next |
|---|---|---|
| the pad **toggles** | the McBSP is transmitting; the codec is mis-framing what it receives | re-check `R0x300` format (`FMT` is bits `[4:3]`, `0x1` = I2S) and word length against the McBSP `XCR`/`SRGR1` `FWID`; try the codec-as-AIF1-master pair (§0c note) |
| the pad is **static** | the McBSP never transmits; the fault is inside the port | check the sDMA port config and `ti,buffer-size`, then try `dx`→`dr` loopback with a capture stream |
| no scope available | — | do the **loopback**: set `abe_mcbsp2_dx`→`dr` externally (or configure a capture stream and watch `DRR`) — this proves the transmit path without the codec |

**Fallback experiment (no scope needed): the reference's clock roles.**
The reference runs the codec as **AIF1 master** (`R0x302 = 0x4000`) with its
McBSP as a consumer; ours is the reverse.  Switching needs **both** sides at
once:
1. board DTS DAI link format, and
2. the CPU DAI in `SND_SOC_DAIFMT_BC_FC`, with `set_sysclk` taking the codec's
   BCLK.
This removes the McBSP sample-rate generator from the path entirely.

### Driver facts you need before touching it

* The repair runs from a **delayed work 50 ms after `trigger(START)`** — the
  only point where BCLK is running (the machine `.trigger` is *first* in ASoC's
  trigger order, before the CPU DAI starts BCLK).
* The repair begins with **`regcache_mark_dirty()` + `regcache_sync()`**.  Without
  it every `update_bits()` is a silent no-op because the cache holds the values
  the driver tried to write while the codec was frozen.  **Trap 162.**
* `hw_params` writes `R0x200 = 0` to thaw, so **something must re-enable
  `AIF1CLK` afterwards** — otherwise the DAC loses its clock (that bug was found
  and fixed in session 41).
* The FLL is referenced from **BCLK**, not MCLK1: MCLK1 locked it only
  intermittently (§0c note in report 205 §5h).

## 0d. Touch — done, but it does not survive a warm reboot

I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` boots the Munin app
and maps the 1917 `scaling.conf` calibration to 1024×600; the GPIO2_3 edge IRQ
works (pad `0x056`, `0x410B`, `irq 110`).  **After a reboot the controller does
not boot** (`0x65` NAKs, `touch did not boot: -110`) until the power/reset GPIOs
(**GPIO1_14 / GPIO4_8**) are pulsed — the driver still does not drive them.
That pulse is the remaining work, and it is the recovery path.

## 1. Useful local extracts

* **`local/work/1917/base/`** — the whole 1917 `/base` (333 MB).
* **`local/work/1917/ifs/`** — the 1917 IFS tree + `proc/boot/startup-playbook`.
* **`local/work/1917/calib/`**, **`local/work/1917/parts/`** — WLAN calibration
  and pulled partitions.
* **`local/vendor/bb10mt/bb10mt`** — the QNX6/container tool.
* **The WM8994 datasheet** — Rev 4.6; the authority for clocking, PM3/PM4/PM5
  and the charge pump.  A text extraction exists at
  `/tmp/commandcode-*/…/scratchpad/wm8994.txt` **but is session-scoped — it will
  not survive**.  Re-fetch if needed.
* Reports **201**, **203**, **204** and the current **205**; the reference
  captures in `docs/captures/`.

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

Full list 14–164 in `KNOWN-ISSUES.md` §2.  The session-41 additions:

* **161.** The kernel patch was **not re-exported** after session 41's DTS and
  driver edits.
* **162.** **After any period of dropped codec writes, `regcache_mark_dirty()`
  before writing again** — otherwise `update_bits()` is a silent no-op.
* **163.** **Copy the rebuilt `.ko` into `local/work/modules-7.3.3/`** before
  `build_rootfs.py`, and verify with `debugfs -R 'dump …'`.
* **164.** A console that echoes but prints nothing is a **busy shell**, not a
  dead one.

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
PlayBook (OMAP4430 "winchester") — session 42 (absolute; Linux-era 36)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), docs/history/KERNEL-PORT.md §7, ROADMAP.md item 9 (step 5),
KNOWN-ISSUES.md (traps 14-164, especially 160-164, and §3), DEVELOPMENT.md
§3/3a/4/7a, DECISIONS.md (44-51), docs/reports/INDEX.md, report 205 (§5g-§5n are
the audio state), and sessions/session-41.md.

WHERE IT STANDS: the WM8994 CODEC side of playback is SOLVED and driver-driven.
From a cold boot the machine driver thaws the codec (R0x200=0), locks FLL1 from
BCLK (0x732=0x6020), enables AIF1CLK from FLL1, sets 16-bit word length, and
enables the whole output chain — PM3 0x03F0 (MIXOUTL/R_ENA live in PM3, NOT
PM4), charge pump CP_ENA (R0x4C bit 15), R0x02D/0x02E un-mute, R0x420 un-mute,
R0x601/0x602 AIF1->DAC routing, R0x60=0x00EE — and the DAC's noise floor is
AUDIBLE.  The remaining blocker is the McBSP transmit path: in steady state
DXR = 0x00000000 with SPCR2 = 0x02F5 (XRDY=0 FIFO full, XEMPTY=1 shifter empty),
i.e. data enters the FIFO and never shifts out, while omap-dma-engine interrupts
fire at exactly 48/4096 per second and hw_ptr runs at 48 185 frames/s.  The
sDMA target address is correct (phys_dma_base = 0x49024000, the "dma" window).
AUXCLK1 now boots at 0x00020100 (DTS assigned-clock-rates — a real defect that
is FIXED); pads, padconf mode, SMART3 drive, DPLL_PER and all PMIC rails are
byte-identical to QNX, so the supply and pad theories are dead.  The touch is
DONE but does not survive a warm reboot (GPIO1_14/GPIO4_8 pulse missing).  The
WL1283 is software-exhausted.  THE PATCH WAS NOT RE-EXPORTED — 161.

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE 0xB8830701 (0x93A8F4 bytes,
kernel 7.3.3); region 1 is the audio/OSS rootfs 7cc63990... (27 modules).  The
way back to TabletOS is the 1917 IFS (local/work/ifs-shim-sealed.ifs) at block
0x825.  Do NOT restore local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

WHERE IT IS: ONE experiment — scope abe_mcbsp2_dx (CORE pad 0x0F8, HIGH half)
during playback.  Toggling => the McBSP transmits and the codec mis-frames;
static => the fault is inside the port (check sDMA port config, ti,buffer-size,
then a dx->dr loopback with capture).  No scope? Do the loopback.  FALLBACK:
the reference runs the codec as AIF1 MASTER (R0x302=0x4000) with its McBSP as a
consumer; switching needs BOTH the DAI link format and the CPU DAI in
SND_SOC_DAIFMT_BC_FC, which removes the SRG from the path.

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
check the last 100-200 lines of dmesg.

STATIC RE: the REA MCP server exposes Ghidra 12.1.4 (open_binary,
binary_overview, analyze_function, procedure_pseudo_code, search_strings,
xrefs, read_bytes); raw ARM import format=raw-binary, architecture=arm,
entry_point=0x80100000.  Reconcile with ntoarmv7-readelf/objdump (Ghidra may add
+0x10000 on an ELF).  QNX device tools build -static, and /tmp is wiped by a
reboot.  USE THE REFERENCE FIRST — and the highest-yield method is a LIVE
REGISTER DIFF against a capture of the working reference (docs/captures/).

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.  A
test boot is not a budget concern (the counter self-replenishes), but the hard
hazards (0 / >0x32 / failed write / flat pack) remain.  Operator's call when to
stop and to wrap up.

NEXT (operator's call): (a) the scope/loopback on the McBSP transmit path;
(b) the codec-as-AIF1-master pair; (c) re-export the kernel patch (trap 161);
(d) touch power/reset (GPIO1_14/GPIO4_8); (e) the wl128x-nvs.bin path (hd5).
The unit is on Linux; region 0/region 1 both change when the kernel/rootfs pair
does, otherwise flash only the changed region; the 1917 IFS at block 0x825 is
the way back to TabletOS.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note — and move replaced or
resolved material to docs/history/ or docs/parked/ with a link stub.

WRAP-UP: the operator decides when the session stops.  Record the bench state,
write the session note, update the canonical docs, verify with
tools/docs_audit.py and tools/tests/test_offline.py, then commit privately and
publish the snapshot (docs/project/REPO-MANAGEMENT.md).  No IFS restore is
required.
```
