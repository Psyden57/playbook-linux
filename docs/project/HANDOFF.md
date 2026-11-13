# Handoff — session 44 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 44 = Linux-era 38**, updated at its
wrap-up.  See `docs/sessions/NUMBERING.md`.

> **Session-44 result, in one line:** **the power-domain / CM comparison is
> negative** — the Linux image and the working 1917 oracle agree in every field
> that is a *state*, and setting every differing CM policy to Linux's value on
> the live oracle leaves MCLK1 present, so the domain is not the gate (report
> 209).  `CM_ABE_PLL_REF_CLKSEL` is now fixed properly on Linux (SYS_CLK,
> DPLL_ABE locked at 98.304 MHz) — an ABE-path fix, not the MCLK1 cause.

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

**The audio blocker is now below every readable register.**  See §0c.  The tool
that got us here is the **minimal 1917 oracle** (§0e).

**The patch** `kernel/patches/0001-playbook-winchester-7.3.3.patch` is the
session-44 export (**8 109 lines / 54 files**, sha256 `2fc1a865…`, 0 rejects
and 54/54 byte-identical).  It carries the E1 clock-role changes (DTS pads,
machine driver) and this session's `&l4_abe` ABE-refclk assignment.

## 0a. The media, and the way back

* **region 0 = the session-44 Linux image** (`boot/chainloader/kernel.img`,
  CRC **`0xB43F169B`**, 0x93A3E0 bytes; kernel `7.3.3` with the E1 clock roles
  and the reference codec-GPIO init
  and the ABE-refclk fix).
* **region 1 = the E1 audio/OSS rootfs** `3df8056c…`
  (`local/images/rootfs-region1-audio-oss.ext4`, 27 modules, `pbplay`/`pbmix`);
  rebuilt this session with the machine-driver change.
* The unit is **on our Linux image** (`0fca:8010`, console `pb> `), not on the
  oracle.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825` (the pruned RCFS is still at `0x8D5`/region-0 `0xC00000`), then
  reboot.  It enumerates `0fca:8012`; qconn `169.254.0.1:9090`, root ssh.
* **To stock TabletOS:** flash `local/work/minimal/wayback.signed` (stock
  `.4.ifs` + stock `.1.rcfs`).  **Do not restore `local/images/ifs0.sealed.bin`**
  (2.0.0.4869, trap 84).
* **To rebuild the oracle:** `python3 tools/build_minimal_qnx.py` (see §0e).

## 0b. Item 9 step 4 (WL1283) — software surface exhausted

Unchanged and **formally stopped by the operator** (session 36).  The 1917-truth
minimal `&mmc5` host probes but `attach_sdio no` (report 196).  Do not re-run
traps 116–137.

## 0c. Item 9 step 5 — audio: one blocker, every readable cause excluded

**Constraint (operator, session 41 end): there is no scope, no boardview and no
pad-level knowledge.  Every experiment must be run from the bench (the unit
itself) or from static RE.  Nothing may depend on probing a pin.**

### What is proven (do not re-derive)

* **SCRM `AUXCLK1` is the codec's MCLK1 — proven causally (session 43, report
  208).**  On the live reference: clear the AUXCLK1 gate (`0x00020100 →
  0x00020000`), reset the codec (`R0x000 = 1`), run its exact FLL1 power-up →
  `R0x732 = 0x6000` (**no lock**); restore AUXCLK1 and restart `io-audio` →
  `0x6020`.
* **On Linux MCLK1 is measurably absent.**  `AUXCLK1 = 0x00020100`, the fref
  pad (`0x19C` low `0x0000`) and the stream-time FLL file (`R0x200 = 0x0011`,
  `0x220=0x0005 … 0x224=0x0C80`) all match the reference, but `R0x732 =
  0x6000`.  The **codec freeze detector** (thaw `R0x200=0`; select MCLK1+ENA
  `R0x200=1`; write `R0x220=4`) lands on the reference and is dropped on Linux.
* **The power/CM state is NOT the gate (session 44, report 209).**  Captured on
  both systems: every `POWERSTATEST`/`LOGICSTATEST`/memory field matches and
  `CM2_CORE_CLKSTCTRL` (the pad's domain) is identical.  The only high-half
  difference is `PM_*_PWSTST[25:24] = LASTPOWERSTATEENTERED`, a **debug latch**
  (trap 181).  Setting every differing CM policy to Linux's value on the live
  working oracle leaves the detector landing (`R0x220` = `0x0004`).  Replaying
  the reference's write order on Linux (pad transition, then the AUXCLK1 gate)
  does not restore the clock either.
* **`CM_ABE_PLL_REF_CLKSEL` is fixed** (session 44): the kernel's own clock
  init forces `SYS_32K` (`clk-44xx.c`); the board DTS now reparents
  `abe_dpll_refclk_mux_ck` to `SYS_CLK` **and** sets `dpll_abe_ck` to
  98.304 MHz, so the DPLL locks (`CM_CLKSEL_DPLL_ABE = 0x00804018`,
  `ST_DPLL_CLK = 1`, zero boot errors).  Reparenting **alone** leaves the M/N
  programmed for 32 kHz and the DPLL never locks — do not do that again
  (trap 182).  This is **not** the MCLK1 cause.
* **The reference's codec-GPIO init is implemented and tested** (session 44,
  report 211): the `audio-ldo` line (GPIO4_31) is a boot-on regulator pulsed at
  probe, `spk` is high via the hog, `hp` input.  It does **not** restore MCLK1,
  but the WM8994 probe now needs it high before module load (trap 186).
* **The codec chain is driver-driven** (session 41) and the **transmit path
  works in consumer mode** (session 42, E1): codec AIF1 master (`R0x302 =
  0x4000`), McBSP clock consumer (`PCR0 = 0x000f`, SRG never started,
  `clkx`/`fsx` pads = `0x0100` inputs).  With a live AIF1CLK (free-running
  NCO) `pbplay 20 440` runs the full 20 s and exits cleanly; the old
  McBSP-master configuration always died with `Input/output error` after ~1 s.
  **The session-41 `XRDY = 0` stall is a property of the master/SRG
  configuration, not of the silicon.**
* **MCLK1 does not clock at the pin.**  FLL1 never locks from MCLK1 with a
  byte-identical register file; the direct test (AIF1CLK from MCLK1, FLL
  bypassed, `R0x200 = 0x0001`) also dies.  `AUXCLK1 = 0x00020100` and pad
  `0x19C` low `0x0000` re-verified.  Caveat: session 41 measured one successful
  MCLK1 lock, so "marginal" is still live.
* The free-running NCO (datasheet Rev 4.6 p. 206: `OSC_ENA = 1`, `OUTDIV = ÷8`,
  `FRC_NCO = 1`, `FRC_NCO_VAL = 0x19` ≈ 12 MHz, ±10–30 %) is a switch that
  makes AIF1CLK exist — the mechanism probe of session 42, not a hi-fi clock.

### The routes forward (operator's call)

1. **The McBSP master/SRG stall — the only software-only design left.**  In
   McBSP-master mode the codec was slave with a BCLK-referenced FLL that locked
   every time — so BCLK ran, and the codec consumed.  The stall was between
   the SRG and the shifter; the shifter is proven good (it works when
   externally clocked), so the question is the SRG's output distribution:
   `CLKSM`/`FSGM`/`CLKGDV` permutations, **one boot at a time**, judged by
   `pbplay`'s survival (trap 169) and the steady-state dump.  This route needs
   **no MCLK1** (FLL1 from BCLK).
2. **The free-running NCO** — already proven as a 20 s clean stream; it is a
   ±10–30 % clock, so it demonstrates the path but is not a hifi source.
3. **A probe on `fref_clk2_out`/MCLK1** — impossible today (trap 165); if one
   appears it is a two-minute measurement that decides dead-vs-marginal.

### Driver facts you need before touching it

* The repair runs from a **delayed work 50 ms after `trigger(START)`** and
  begins with **`regcache_mark_dirty()` + `regcache_sync()`** (trap 162).
* `hw_params` writes `R0x200 = 0`, so the repair re-enables `AIF1CLK` last.
* The FLL is referenced from **MCLK1** since E1 — and that reference never
  arrives, which is the whole problem.
* A frozen codec drops **raw `i2ctransfer` writes too** (trap 167): thaw
  (`R0x200 = 0`) before any register surgery, re-enable AIF1CLK last.
* The driver's `set_pll()` clears `FRC_NCO` (trap 168): a hand-forced NCO must
  be injected after `trigger(START)` + the repair, or the driver must be
  changed; the NCO must **not** be shipped in the driver (DECISIONS §53).

## 0d. Touch — done, and it survives a warm reboot; cold boots need the pulse

I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` boots the Munin app
and maps the 1917 `scaling.conf` calibration to 1024×600; the GPIO2_3 edge IRQ
works (pad `0x056`, `0x410B`, `irq 110`).  A warm reset keeps the controller up
(session 42/44); after a **cold** boot it does not (`0x65` NAKs,
`touch did not boot: -110`) until the power/reset GPIOs (**GPIO1_14 /
GPIO4_8**) are pulsed — the driver still does not drive them.

## 0e. The minimal 1917 oracle (session 43; report 207)

A debloated 2.1.0.1917 image that boots in ~30 s and gives a shell over USB.
Use it to run the reference's own code (`io-audio`, HALs) and read the hardware
while it runs.

* **Media:** region 0 = the shim IFS at `0x825` + the pruned RCFS at `0x8D5`
  (`local/work/minimal/setB.signed`); region 1 is untouched.
* **Connect:** `python3 tools/QCL.py info` (qconn, `169.254.0.1:9090`) or
  `tools/pbssh.sh` (root, `uid:0`).  Host link `enp8s0f3u1`
  `169.254.0.2/16` — do not re-add.
* **What runs:** `filesystem pps random pty net powerman phyctrl usbmgr rimusb`,
  plus the UFS `/accounts/devuser/startup.sh` (top-up, dev mode,
  `qconn port=9090`, root `sshd`).  No GUI/launcher/camera/radio.
* **Audio HAL (session 43/44):** `/base/sbin/io-audio -d omap4abe-wm8994
  aif=0,secure_capture -o enable_buffering`; it leaves FLL1 locked
  (`R0x732 = 0x6020`, `R0x200 = 0x0010`).  `slay io-audio` before the freeze
  detector.
* **Build:** `python3 tools/build_minimal_qnx.py`.  An OS 2.x `.rcfs` is a
  **QNX6 filesystem** — mount a copy with `bb10mt qnx6 mount`; not
  `rcfs_extract.py`, never a rebuild from a tree.
* **Tools on the UFS:** `/accounts/devuser/bin/{memdump3,memw32,pbi2c}`; they
  survive reboots.  WM8994 writes need `pbi2c wr16r` (the reference's 20-byte
  inline `devctl` ABI, trap 176).  `memdump3 <addr> 4` reads one word.
* **Bench only:** unauthenticated qconn/root ssh — never on a network (trap 173).
* **A reboot restores volatile register state** (trap 177) — the fast way to
  undo a register experiment.

## 1. Useful local extracts

* **`local/work/1917/base/`**, **`ifs/`**, **`calib/`**, **`parts/`** — the
  1917 `/base` (333 MB), the IFS tree, WLAN calibration, pulled partitions.
* **`local/vendor/bb10mt/bb10mt`** — the QNX6/container tool.
* **The minimal oracle** — `tools/build_minimal_qnx.py`, and the built images
  under `local/work/minimal/` (`rcfs-minimal.rcfs`, `setB.signed`,
  `wayback.signed`, `devuser-startup.sh`).
* **The WM8994 datasheet** — Rev 4.6, in the tree at `hardware/WM8994.PDF`
  (gitignored, local-only; FLL/NCO ≈ p. 201–208).
* Reports **206**, **207**, **208**, **209** (current); captures in
  `docs/captures/` (`wm8994-oracle-live-2026-11-12.txt`,
  `wm8994-pm-cm-2026-11-12.txt`).

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

**Patch export** (session 44 procedure, trap 161): regenerate from
`local/pristine/linux-7.3.3` over the old patch's file list, then **apply the
patch to a staged copy of the pristine files and compare all of them** to
`local/kernel-tree` — 54/54 byte-identical is the acceptance test, not a
`--dry-run`.

## 3. Bench

* **The unit is on our Linux image** (`0fca:8010`, console `pb> `).
  `tools/pbcon.py 'cmd'`; reboot with `tools/pbcon.py 'reboot -f'`.
  `pkexec chmod 666 /dev/ttyACM0` **only after a genuine re-enumeration** —
  a kernel change/flash often needs a **physical USB replug** before the ACM
  gadget appears (observed twice this session; trap 184).
* **The oracle** (`0fca:8012`): `tools/QCL.py info|exec|put|rm`
  (`169.254.0.1:9090`), `tools/pbssh.sh 'cmd'` as root.  Reboot the oracle with
  `QCL.py exec /proc/boot/shutdown -S reboot -f`.  Host link `enp8s0f3u1`
  carries `169.254.0.2/16` (do not re-add).
* Device password `0000`, once.  An agent **never writes RPMB**; NVRAM is
  read-only (SAFETY §1/§6).  The minimal image's UFS top-up keeps
  `BOOTCTR_FCTRY` at `0x32`.  After every reboot, check `pidin arg` / services.
* WM8994 register work: `i2ctransfer` arguments are **DECIMAL** (trap 166); a
  frozen codec drops raw writes too (trap 167 — thaw with `R0x200=0`,
  re-enable AIF1CLK last).
* QNX device tools build **`-static`**; `/tmp` is wiped by a reboot.

## 4. Traps added last session (full list 14–184 in `KNOWN-ISSUES.md` §2)

* **181.** `PM_*_PWSTST[25:24]` is `LASTPOWERSTATEENTERED`, a W1toSet **debug
  latch**, not a power state.  A higher value there does not mean a domain was
  awake or asleep at a different time; do not read it as evidence.
* **182.** `CM_ABE_PLL_REF_CLKSEL` is forced to `SYS_32K` by the kernel's own
  clock init; the DTS value must include **both** the parent (`sys_clkin_ck`)
  and the rate (`98304000`).  Reparenting alone leaves the M/N programmed for
  32 kHz and DPLL_ABE never locks (7 boot errors).
* **183.** The Linux clock-domain framework re-asserts its `CLKTRCTRL` policy
  after a `devmem` poke (only domains it does not manage keep the new value).
  Do not treat "the write did not stick" as hardware.
* **184.** After a kernel flash, the ACM gadget may not enumerate until the USB
  cable is physically replugged.  If `lsusb` shows no `0fca:` device and the
  unit is demonstrably booted, replug before diagnosing the image.

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **minimal 1917 oracle** | **`tools/build_minimal_qnx.py`**, `local/work/minimal/` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| reference codec captures | `docs/captures/wm8994-*` |
| 1917 `/base`, IFS, calibration | `local/work/1917/{base,ifs,calib,parts}/` |
| 1917 audio driver (RE'd) | `local/work/1917/base/lib/dll/deva-ctrl-omap4abe-wm8994.so` |
| touch driver | `local/kernel-tree/drivers/input/touchscreen/playbook-munin.c` |
| audio machine driver | `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` |
| McBSP driver (patched) | `local/kernel-tree/sound/soc/ti/omap-mcbsp.c` |
| OSS player / mixer | `rootfs/pbplay.c`, `rootfs/pbmix.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| reports | `docs/reports/` (**209** is this session; **207** the oracle; 208 the AUXCLK1 causality) |
| sessions | `docs/sessions/session-44.md` (last session) |

## 6. Working rules

* After every console command, dump the log tail and read it.  After a reboot or
  a structural change, **check the last 100–200 lines of `dmesg`**.
* Write the durable fact into the repository in the same turn.
* **One variable per boot.**
* **A test boot is not a budget concern.**
* **Never present a guess as a fact.**  External facts are looked up and cited;
  device facts prefer a fresh read over an old note.
* **Use the reference before the bench** — diff the live register file against a
  capture of the working reference (or against the oracle).
* **When a register "reverts", find out which writer did it** (driver, DAPM,
  `hw_params`, `trigger`, regmap cache, clock framework) before concluding the
  hardware ignored it.
* Subagents: static/file-only, never a `tools/` script (trap 109).
* **No restore is required at wrap-up.**  Leaving the unit on Linux is valid.

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 45 (absolute; Linux-era 39)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 9, KNOWN-ISSUES.md (traps 14-184, especially 165-184,
and §3), DEVELOPMENT.md §3/3a/4/7a, DECISIONS.md (44-55), docs/reports/INDEX.md,
reports 207 (the oracle), 208 (AUXCLK1 causality) and 209 (the power/CM
comparison), and sessions/session-44.md.

WHERE IT STANDS: a debloated 2.1.0.1917 image is a LIVE HARDWARE ORACLE (report
207): it boots in ~30 s, enumerates as 0fca:8012, and answers qconn at
169.254.0.1:9090 and root ssh.  SCRM AUXCLK1 IS the codec's MCLK1 (report 208),
while on Linux MCLK1 is measurably ABSENT with identical AUXCLK1=0x00020100,
fref pad 0x0000 and the exact stream-time FLL file (0x220=0x0005 .. 0x224=0x0C80).
The codec freeze detector (validated on the reference) is the tool.  SESSION 44
RAN THE LAST READABLE HYPOTHESIS AND IT IS NEGATIVE: the power/CM state matches
in every field that is a state (only PM_*_PWSTST[25:24], the LASTPOWERSTATEENTERED
debug latch, differs), and setting every differing CM policy to Linux's value on
the live working oracle leaves MCLK1 present.  The fault is below the readable
registers (pad/board/PMIC, trap 165).  CM_ABE_PLL_REF_CLKSEL is now SYS_CLK with
DPLL_ABE locked at 98.304 MHz — an ABE-path fix, NOT the MCLK1 cause.  The
reference's codec-GPIO init is now implemented and tested (image 0xB43F169B):
FLL1 still does not lock, but the WM8994 probe needs that ldo line high.

The WM8994 transmit path remains PROVEN working (E1, session 42): codec AIF1
master (R0x302=0x4000), McBSP consumer (PCR0=0x000f, SRG out of the path,
clkx/fsx pads = 0x0100 inputs); with a live AIF1CLK (free-running NCO) pbplay
streamed 20 s cleanly, where the McBSP-master config died at ~1 s.  THE ONE
REMAINING BLOCKER IS A LIVE AIF1CLK.  The software-only route left is the
McBSP-master/SRG stall: the codec was slave with a BCLK-referenced FLL that
locked every time (so BCLK ran), the shifter is proven good when externally
clocked, so try CLKSM/FSGM/CLKGDV permutations, ONE PER BOOT, judged by pbplay's
survival (trap 169).  The NCO is ±10-30%, a mechanism probe, not a hifi clock.
The touch survives warm reboots (cold boots need the GPIO1_14/GPIO4_8 pulse).
WL1283 is software-exhausted (but audit sysfs-gpio-number experiments against
trap 185).  Kernel patch: session-44 export, 54 files / 8214 lines, sha256
90391b96..., 0 rejects / 54/54 byte-identical.

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE (CRC 0xB43F169B, 0x93A3E0,
kernel 7.3.3 with the ABE-refclk fix and the codec-GPIO init).  REGION 1 holds the E1 audio/OSS rootfs
3df8056c... (67,108,864 B).  The unit is on LINUX (0fca:8010, console `pb> `).
To boot the ORACLE: flash local/work/ifs-shim-sealed.ifs at 0x825 (the pruned
RCFS is still at 0x8D5).  To stock TabletOS: flash
local/work/minimal/wayback.signed.  Do NOT restore local/images/ifs0.sealed.bin
(2.0.0.4869, trap 84).

BENCH: the unit is on OUR LINUX IMAGE (0fca:8010, console `pb> `).  qconn/ssh
are available only when the oracle is booted (flash
local/work/ifs-shim-sealed.ifs at 0x825): `tools/QCL.py info|exec|put|rm`
(169.254.0.1:9090), `tools/pbssh.sh 'cmd'` as root.  Reboot the oracle:
`QCL.py exec /proc/boot/shutdown -S reboot -f`.  The host link enp8s0f3u1
carries 169.254.0.2/16 (do not re-add).  To REBUILD the oracle:
`python3 tools/build_minimal_qnx.py`.  An OS 2.x .rcfs is a QNX6 filesystem —
NOT rcfs_extract.py (BB10-only), never rebuild from a tree.  Our Linux image has
NO door: tools/pbcon.py (prompt `pb> `), reboot with `tools/pbcon.py 'reboot
-f'`; `pkexec chmod 666 /dev/ttyACM0` only after a genuine re-enumeration, and
after a kernel flash the ACM gadget may need a PHYSICAL USB REPLUG (trap 184); a
console that echoes but prints nothing is a BUSY SHELL (trap 164).  Run
USB/flash tools as the USER, never pkexec.  Device password 0000, once.  An
agent NEVER writes RPMB; NVRAM is read-only (SAFETY §1/§6).  The oracle is
UNAUTHENTICATED (qconn 9090, root ssh, firewall off): bench only, never on a
network (trap 173).  After every reboot, check `pidin arg` and the services.
WM8994 register work: i2ctransfer arguments are DECIMAL (trap 166); a frozen
codec drops raw writes too (trap 167 — thaw with R0x200=0, re-enable AIF1CLK
last).  On the oracle use `pbi2c wr16r` (the reference 20-byte devctl ABI,
trap 176).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4 (open_binary,
binary_overview, analyze_function, procedure_pseudo_code, search_strings,
xrefs, read_bytes); raw ARM import format=raw-binary, architecture=arm,
entry_point=0x80100000.  Reconcile with ntoarmv7-readelf/objdump (Ghidra may add
+0x10000 on an ELF).  QNX device tools build -static, and /tmp is wiped by a
reboot.  USE THE REFERENCE FIRST — the highest-yield method is a LIVE REGISTER
DIFF against the oracle itself.  The WM8994 Rev 4.6 datasheet is LOCAL:
hardware/WM8994.PDF (gitignored, do not fetch it online) — FLL / free-running
NCO ≈ p.201-208.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
The UFS top-up keeps BOOTCTR_FCTRY at 0x32; the hard hazards (0 / >0x32 / failed
write / flat pack) remain.  Operator's call when to stop and to wrap up.

NEXT (the one experiment): **the McBSP-master / SRG design**.  Restore McBSP
master mode (CPU bit/frame provider) with the codec slave, FLL1 referenced from
BCLK, and permute `CLKSM`/`FSGM`/`CLKGDV` ONE PER BOOT; judge by `pbplay`'s
survival past five periods (trap 169) and the existing steady-state dump.
Inputs: `sound/soc/ti/playbook-wm8994.c` `hw_params` (drop the consumer path,
re-add `set_sysclk`/`set_clkdiv` and the SRG start), `playbook-wm8994.c` /
`omap-mcbsp.c` as needed, DTS `mcbsp2_pins` back to `PIN_OUTPUT` for
clkx/fsx.  Results: a stream that lives past ~5 periods proves the SRG→shifter
distribution is fixed (then verify the rate/tone); a stream that still dies at
~1 s rules out that permutation and the next boot tries the next one.  If all
permutations fail, the remaining software option is the NCO as a degraded
clock, and the real fix needs a probe.  Backlog: (a) a probe on fref_clk2_out/MCLK1; (b) the non-reference
AIF1CLK design; (c) touch cold-boot power/reset (GPIO1_14/GPIO4_8); (d) the
wl128x-nvs.bin path (hd5).  The unit is on LINUX (0xB43F169B) + the rootfs
3df8056c; region 0/region 1 both change when the kernel/rootfs pair does,
otherwise flash only the changed region.

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
