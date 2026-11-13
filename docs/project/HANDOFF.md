# Handoff — session 43 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 43 = Linux-era 37**, updated at its
wrap-up.  See `docs/sessions/NUMBERING.md`.

> **Session-43 result, in one line:** **a debloated 2.1.0.1917 image is now a
> live hardware oracle (report 207), and it settled the MCLK1 model:** SCRM
> `AUXCLK1` IS the codec's MCLK1 (causality, report 208), and on Linux MCLK1 is
> measurably **absent** with identical `AUXCLK1`/pad/stream-time FLL file (a
> codec freeze detector, validated on the reference).  A broad register sweep
> ruled out every readable difference; the fault is below the registers.  One
> real, separate audio bug found: `CM_ABE_PLL_REF_CLKSEL` is SYS_32K on Linux,
> SYS_CLK on the reference.

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

**The audio blocker is now one thing: a live AIF1CLK.**  See §0c.  The new tool
for it is the **minimal 1917 oracle** (§0e).

**The patch** `kernel/patches/0001-playbook-winchester-7.3.3.patch` is the
session-42 export (**54 files / 8 095 lines**, sha256 `55035aac…`, 0 rejects
and 54/54 byte-identical).  It carries the E1 clock-role changes (DTS pads,
machine driver).  No kernel/DTS source changed in session 43, so it was not
re-exported.

## 0a. The media, and the way back

* **region 0 = the minimal 1917 oracle** — the shim IFS
  (`local/work/ifs-shim-sealed.ifs`, sha256 `b52195bb…`) at region-relative
  block 16 (`0x825`) + the pruned RCFS
  (`local/work/minimal/rcfs-minimal.rcfs`, sha256 `b931a6d4…`) at block 192
  (`0x8D5`).  Both in one container: `local/work/minimal/setB.signed`
  (363,790,928 B, sha256 `d22eab3b…`).
* **region 1 = the E1 audio/OSS rootfs** `b20302dc…`
  (`local/images/rootfs-region1-audio-oss.ext4`, 27 modules,
  `pbplay`/`pbmix`); unchanged this session.
* The unit is **on the minimal 1917 reference** (`0fca:8012`; qconn
  `169.254.0.1:9090`, root ssh), **not** on our Linux image.
* **To our Linux image:** flash `boot/chainloader/kernel.img` at block `0x825`
  only (region 1 is untouched).
* **To stock TabletOS:** flash `local/work/minimal/wayback.signed`
  (stock `.4.ifs` + stock `.1.rcfs`; 359,137,872 B, sha256 `dd03fb8b…`).
  **Do not restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).
* **To rebuild the oracle:** `python3 tools/build_minimal_qnx.py` (see §0e).

## 0b. Item 9 step 4 (WL1283) — software surface exhausted

Unchanged and **formally stopped by the operator** (session 36).  The 1917-truth
minimal `&mmc5` host probes but `attach_sdio no` (report 196).  Do not re-run
traps 116–137.

## 0c. Item 9 step 5 — audio: one blocker (a live AIF1CLK), three routes

**Constraint (operator, session 41 end): there is no scope, no boardview and no
pad-level knowledge.  Every experiment must be run from the bench (the unit
itself) or from static RE.  Nothing may depend on probing a pin.**

### What is already proven (do not re-derive)

* **SCRM `AUXCLK1` is the codec's MCLK1 — proven causally (session 43, report
  208).**  On the live reference: clear the AUXCLK1 gate (`0x00020100 →
  0x00020000`), reset the codec (`R0x000 = 1`), run its exact FLL1 power-up →
  `R0x732 = 0x6000` (**no lock**); restore AUXCLK1 and restart `io-audio` →
  `0x6020`.  The reference's AUXCLK1, pad `0x19C` and full FLL file are
  identical to ours.
* **On Linux MCLK1 is measurably absent (session 43).**  `AUXCLK1 =
  0x00020100`, the fref pad (`0x19C` low `0x0000`) and the stream-time FLL file
  (`R0x200 = 0x0011`, `0x220=0x0005 … 0x224=0x0C80`) all match the reference,
  but `R0x732 = 0x6000`.  The **codec freeze detector** (thaw `R0x200=0`;
  select MCLK1+ENA `R0x200=1`; write `R0x220=4`) lands on the reference and is
  dropped on Linux.  No AUXCLK1 source works on Linux, and every differing
  SCRM/pad/CM/PRM register was tested and ruled out.  The fault is below the
  readable registers (dynamic/domain/pad/board).
* **One separate audio bug: `CM_ABE_PLL_REF_CLKSEL` (0x4A30610C).**  Linux
  leaves it at the reset default SYS_32K; the reference sets SYS_CLK.  It is
  the DPLL_ABE reference, not MCLK1 (setting it on Linux did not unfreeze the
  codec), but it should be fixed for the ABE/McBSP path.
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

### The one next experiment (session 44): power-domain / CM comparison

MCLK1 is absent on Linux with every readable config matching, so the last
readable gap is the **power/clock-domain state**.

* **Inputs — capture on both the oracle and the Linux image:**
  `PM_CORE_PWRSTST` `0x4A306704`, `PM_WKUP_PWRSTST` `0x4A307704`,
  `PM_L4PER_PWRSTST` `0x4A307404`, `PM_ABE_PWRSTST` `0x4A306504`,
  `CM_ABE_PLL_REF_CLKSEL` `0x4A30610C` (known: reference 0, Linux 1), and the
  CM `*_CLKSTCTRL` for CORE/L4PER/WKUP (CM2 `0x4A008000`, CM1 `0x4A004000`).
  Oracle: `/accounts/devuser/bin/memdump3 <addr> 4` over `tools/pbssh.sh`.
  Linux: `busybox devmem <addr> 32` over `tools/pbcon.py`.
* **Then, on Linux, with one variable changed at a time** (set a differing
  domain to the reference's value), run the freeze detector:
  `i2ctransfer -f -y 3 w4@0x1a 0x02 0x00 0x00 0x00` (thaw),
  `... 0x02 0x20 0x00 0x02` (baseline), `... 0x02 0x00 0x00 0x01`
  (MCLK1+ENA), `... 0x02 0x20 0x00 0x04` (test), then read `0x02 0x20 r2`.
* **What each result means:** `R0x220 = 0x0004` → MCLK1 is present, the domain
  was the gate (then fix it in the DTS); `R0x220 = 0x0002` → the domain is not
  the cause, and the fault is the pad/board/PMIC, i.e. trap 165 (a probe).

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

## 0e. The minimal 1917 oracle (session 43; report 207)

A debloated 2.1.0.1917 image that boots in ~30 s and gives a shell over USB.
Use it to run the reference's own code (`io-audio`, HALs) and read the hardware
while it runs.

* **Media:** region 0 = the shim IFS at `0x825` + the pruned RCFS at `0x8D5`
  (`local/work/minimal/setB.signed`); region 1 is untouched.
* **Connect:** `python3 tools/QCL.py info` (qconn, `169.254.0.1:9090`) or
  `tools/pbssh.sh` (root, `uid:0`; needs `local/secrets/rsa.pub` in
  `/accounts/devuser/.ssh/authorized_keys`).  Host link `enp8s0f3u1`
  `169.254.0.2/16` — do not re-add.
* **What runs:** `filesystem pps random pty net powerman phyctrl usbmgr rimusb`,
  plus the UFS `/accounts/devuser/startup.sh` (top-up, dev mode,
  `qconn port=9090`, root `sshd`).  No GUI/launcher/camera/radio.
* **Build:** `python3 tools/build_minimal_qnx.py` (copies the vendor `.1.rcfs`,
  mounts the copy RW with `bb10mt qnx6 mount`, patches `startup.sh`, prunes,
  unmounts, verifies).  `--no-prune` is stage A.  **An OS 2.x `.rcfs` is a QNX6
  filesystem**: do not use `rcfs_extract.py`, do not rebuild from a tree.
* **Flash:** `set.lst` (`=16,0` IFS, `=192,0` RCFS), `bbusb.py pack -v 1,2 -s`;
  arm the flasher, then reboot the running QNX with
  `QCL.py exec /proc/boot/shutdown -S reboot -f`.
* **Way back:** `local/work/minimal/wayback.signed` (stock IFS+RCFS), or the
  Linux kernel image at `0x825` only.
* **Bench only:** unauthenticated qconn/root ssh — never on a network (trap 173).
* **Already used (session 43, report 208):** `io-audio` running, FLL1 locked
  (`R0x732 = 0x6020`); AUXCLK1 gate off + codec reset + FLL power-up → **no lock**
  (`0x6000`); restore → `0x6020`.  So AUXCLK1 IS the codec's MCLK1.  **Next:**
  capture the same SoC/codec set on the Linux image and diff; if identical, the
  fault is the pad output / clock-domain state.
* **Tools on the UFS:** `/accounts/devuser/bin/{memdump3,memw32,pbi2c}`.
  WM8994 writes need `pbi2c wr16r` (the reference's 20-byte inline `devctl`
  ABI; `wr16`/`wr16c` with iovecs fail — trap 176).  A reboot restores any
  volatile register state (trap 177).

## 1. Useful local extracts

* **`local/work/1917/base/`** — the whole 1917 `/base` (333 MB).
* **`local/work/1917/ifs/`** — the 1917 IFS tree + `proc/boot/startup-playbook`.
* **`local/work/1917/calib/`**, **`local/work/1917/parts/`** — WLAN calibration
  and pulled partitions.
* **`local/vendor/bb10mt/bb10mt`** — the QNX6/container tool (`qnx6 mount` for
  the OS 2.x `.rcfs`; `rcfs_extract.py` is BB10-only).
* **The minimal oracle** — `tools/build_minimal_qnx.py`, and the built images
  under `local/work/minimal/` (`rcfs-minimal.rcfs`, `setB.signed`,
  `wayback.signed`, `devuser-startup.sh`).
* **The WM8994 datasheet** — Rev 4.6; the authority for clocking, PM3/PM4/PM5
  and the charge pump.  **It is in the tree at `hardware/WM8994.PDF`**
  (gitignored, local-only) — use that, do not fetch it online.  The FLL and
  free-running-NCO sections are around p. 201–208 (session 42 used them).
* Reports **201**, **203**, **204**, **205**, **206** and the current **207**;
  the reference captures in `docs/captures/`.

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

* **The unit is on the minimal 1917 reference** (`0fca:8012`).  Two channels:
  * **qconn:** `python3 tools/QCL.py info` / `... exec /proc/boot/pidin arg` /
    `... put <local> <remote>` / `... rm <remote>` (defaults host
    `169.254.0.1`, port `9090`).
  * **root ssh:** `tools/pbssh.sh 'cmd'` (`uid:0`).  It needs our
    `local/secrets/rsa.pub` in `/accounts/devuser/.ssh/authorized_keys` (done)
    and the UFS host key.  Reboot the running QNX with
    `QCL.py exec /proc/boot/shutdown -S reboot -f` (or `pbssh 'shutdown -S reboot -f'`).
* **Our Linux image** (`0fca:8010`) has **no door**: `tools/pbcon.py` (prompt
  `pb> `), reboot with `tools/pbcon.py 'reboot -f'`; `pkexec chmod 666
  /dev/ttyACM0` **only after a genuine re-enumeration**.
* **The console is not wedged when it echoes but prints nothing** — the target
  is still inside a foreground command.  `pbcon.py` now says so; `tools/pbraw.py
  N 0a` dumps raw bytes, and `tools/pbraw.py 2 03` sends Ctrl-C.  **Trap 164.**
* **On TabletOS** the door is `169.254.0.1`; host link `enp8s0f3u1` carries
  `169.254.0.2/16` (do not re-add).  **Do not start `door-hold.sh` yourself if
  the operator has one running** — stale holders cause an auth loop; the
  operator restarts it.  `tools/pbssh.sh` logs in as `root` (`uid:0`).
* Device password `0000`, once.  An agent **never writes RPMB**; NVRAM is
  read-only.  The minimal image's UFS top-up keeps `BOOTCTR_FCTRY` at `0x32`.
  After every reboot, check `pidin arg` / the services.
* QNX device tools must be **`-static`** (trap 131): build `tools/pbi2c` and
  `tools/memdump3` with `$QNX_HOST/usr/bin/arm-unknown-nto-qnx6.6.0eabi-gcc
  -static`.  **`/tmp` is wiped by a reboot** — re-push after every one.
* `tools/pbssh.sh` needs `/proc/boot/pmic-twl6030-ctl` **without** `printf`/`tr`
  (QNX has no such binaries) — use `echo` and separate invocations.

## 4. Traps added last session

Full list 14–174 in `KNOWN-ISSUES.md` §2.  The session-43 additions:

* **170.** An OS 2.x `.rcfs` is a **QNX6 filesystem** — mount a *copy* with
  `bb10mt qnx6 mount`, edit through the mount; `rcfs_extract.py` is BB10-only.
* **171.** Pruning the RCFS does **not** shrink the flash container (the packer
  skips only `0xFF`); it is a live-filesystem win.
* **172.** `slay -f <script>.sh` does **not** kill a script (matches `/bin/sh`);
  match the argument via `pidin arg` and kill the PID.  Killing a parent can take
  its `sshd -D` child; restart with `on -d /usr/sbin/sshd`.
* **173.** The minimal oracle is **unauthenticated** (qconn 9090, root ssh,
  firewall off) — bench only; never on a network.
* **174.** `/base/air` and `/air` are **runtime (RAM)** entries from
  `pps_common`, not on the RCFS.

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **minimal 1917 oracle** | **`tools/build_minimal_qnx.py`**, `local/work/minimal/` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| reference codec captures | `docs/captures/wm8994-reference-*` |
| 1917 `/base`, IFS, calibration | `local/work/1917/{base,ifs,calib,parts}/` |
| 1917 audio driver (RE'd) | `local/work/1917/base/lib/dll/deva-ctrl-omap4abe-wm8994.so` |
| touch driver | `local/kernel-tree/drivers/input/touchscreen/playbook-munin.c` |
| audio machine driver | `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` |
| McBSP driver (patched) | `local/kernel-tree/sound/soc/ti/omap-mcbsp.c` |
| OSS player / mixer | `rootfs/pbplay.c`, `rootfs/pbmix.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| reports | `docs/reports/` (**207** is the oracle; 206 the E1/audio record) |
| sessions | `docs/sessions/session-43.md` (last session) |

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
PlayBook (OMAP4430 "winchester") — session 44 (absolute; Linux-era 38)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 9, KNOWN-ISSUES.md (traps 14-180, especially 165-180,
and §3), DEVELOPMENT.md §3/3a/4/7a, DECISIONS.md (44-54), docs/reports/INDEX.md,
reports 207 (the oracle) and 208 (AUXCLK1 causality + the Linux capture), and
sessions/session-43.md.

WHERE IT STANDS: a debloated 2.1.0.1917 image is a LIVE HARDWARE ORACLE (report
207): it boots in ~30 s, enumerates as 0fca:8012, and answers qconn at
169.254.0.1:9090 and root ssh.  Session 43 used it and settled the MCLK1 model:
SCRM AUXCLK1 IS the codec's MCLK1 (gate off + codec reset + FLL power-up => no
lock; restore => lock; report 208), while on Linux MCLK1 is measurably ABSENT
with identical AUXCLK1=0x00020100, fref pad 0x0000 and the exact stream-time FLL
file (0x220=0x0005 .. 0x224=0x0C80).  The codec freeze detector (validated on
the reference) is the tool.  Every readable difference (SCRM 0x104/0x320, pads
0x190/0x194/0x198/0x19C high, CM2 dividers, PRM IRQ/CAM) was tested and ruled
out, so the fault is below the registers.  One separate audio bug:
CM_ABE_PLL_REF_CLKSEL is SYS_32K on Linux, SYS_CLK on the reference.

The WM8994 transmit path remains PROVEN working (E1, session 42): codec AIF1
master (R0x302=0x4000), McBSP consumer (PCR0=0x000f, SRG out of the path,
clkx/fsx pads = 0x0100 inputs); with a live AIF1CLK (free-running NCO) pbplay
streamed 20 s cleanly, where the McBSP-master config died at ~1 s.  THE ONE
REMAINING BLOCKER IS A LIVE AIF1CLK: FLL1 never locks from MCLK1 (register file
byte-identical to the reference: 0x220=0x0005, 0x221=0x0700, 0x222=0xAE14,
0x223=0x00E0, 0x224=0x0C80; 0x732 stays 0x6000), and AIF1CLK sourced DIRECTLY
from MCLK1 with the FLL bypassed (R0x200=0x0001) also dies — so the pin does not
clock under Linux.  AUXCLK1=0x00020100, pad 0x19C, padconf mode, SMART3 drive,
DPLL_PER, the PMIC rails and the FLL file are byte-identical to the working
reference.  Caveat: session 41 measured ONE MCLK1 lock, so "marginal" is live.
The NCO is ±10-30%, a mechanism probe, not a hifi clock.  The touch survives warm
reboots (cold boots need the GPIO1_14/GPIO4_8 pulse).  WL1283 is
software-exhausted.  Kernel patch: session-42 export, 54 files / 8 095 lines,
sha256 55035aac..., 0 rejects / 54/54 byte-identical (unchanged this session).

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE (CRC 0x36597040, 0x93A2EC,
sha256 53c9fde0...), not the oracle.  REGION 1 holds the E1 audio/OSS rootfs
b20302dc... (67,108,864 B).  The unit is on LINUX (0fca:8010, console `pb> `).
To boot the ORACLE: flash local/work/ifs-shim-sealed.ifs at 0x825 (the pruned
RCFS is still at 0x8D5).  To stock TabletOS: flash
local/work/minimal/wayback.signed.  Do NOT restore local/images/ifs0.sealed.bin
(2.0.0.4869, trap 84).

THE ORACLE HAS BEEN USED (report 208): io-audio running, FLL1 locked
(R0x732=0x6020); gating AUXCLK1 off + codec reset + FLL power-up gave no lock
(0x6000).  On Linux the freeze detector drops the write => MCLK1 absent.  THE
NEXT EXPERIMENT IS THE POWER-DOMAIN/CM COMPARISON: capture PM_CORE/WKUP/L4PER/
ABE_PWRSTST (0x4A306704 / 0x4A307704 / 0x4A307404 / 0x4A306504) and the CM
*_CLKSTCTRL on both, diff, then on Linux set a differing domain to the
reference's value and re-run the freeze detector.  R0x220=0x0004 means the
domain was the gate; 0x0002 means pad/board/PMIC (trap 165).  Also fix
CM_ABE_PLL_REF_CLKSEL = SYS_CLK in the DTS.  The three older routes also stand
(operator's call; trap 165): (a) a probe on fref_clk2_out/MCLK1; (b) a
non-reference AIF1CLK design; (c) the master-mode SRG stall (one permutation per
boot, judged by pbplay's survival — trap 169).

BENCH: the unit is on OUR LINUX IMAGE (0fca:8010, console `pb> `).  qconn/ssh
are available only when the oracle is booted (flash
local/work/ifs-shim-sealed.ifs at 0x825): `tools/QCL.py info|exec|put|rm`
(169.254.0.1:9090), `tools/pbssh.sh 'cmd'` as root (our rsa.pub is in
/accounts/devuser/.ssh/authorized_keys; the host key is on the UFS).  Reboot the
oracle: `QCL.py exec /proc/boot/shutdown -S reboot -f`.  The host link
enp8s0f3u1 carries 169.254.0.2/16 (do not re-add).  To REBUILD the
oracle: `python3 tools/build_minimal_qnx.py` (copy the vendor .1.rcfs, RW-mount
the copy with `bb10mt qnx6 mount`, patch startup.sh, prune, unmount, verify;
`--no-prune` = stage A); pack with `bbusb.py pack -v 1,2 -s`, arm the flasher,
then reboot the running QNX.  An OS 2.x .rcfs is a QNX6 filesystem — NOT
rcfs_extract.py (BB10-only), never rebuild from a tree.  Our Linux image has NO
door: tools/pbcon.py (prompt `pb> `), reboot with `tools/pbcon.py 'reboot -f'`;
`pkexec chmod 666 /dev/ttyACM0` only after a genuine re-enumeration; a console
that echoes but prints nothing is a BUSY SHELL (trap 164).  Run USB/flash tools
as the USER, never pkexec.  Device password 0000, once.  An agent NEVER writes
RPMB; NVRAM is read-only (SAFETY §1/§6).  The oracle is UNAUTHENTICATED (qconn
9090, root ssh, firewall off): bench only, never on a network (trap 173).  After
every reboot, check `pidin arg` and the services.  WM8994 register work:
i2ctransfer arguments are DECIMAL (trap 166); a frozen codec drops raw writes
too (trap 167 — thaw with R0x200=0, re-enable AIF1CLK last).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4 (open_binary,
binary_overview, analyze_function, procedure_pseudo_code, search_strings,
xrefs, read_bytes); raw ARM import format=raw-binary, architecture=arm,
entry_point=0x80100000.  Reconcile with ntoarmv7-readelf/objdump (Ghidra may add
+0x10000 on an ELF).  QNX device tools build -static, and /tmp is wiped by a
reboot.  USE THE REFERENCE FIRST — and the highest-yield method is a LIVE
REGISTER DIFF against a capture of the working reference (docs/captures/), or
now against the oracle itself.  The WM8994 Rev 4.6 datasheet is LOCAL:
hardware/WM8994.PDF (gitignored, do not fetch it online) — FLL /
free-running NCO ≈ p.201-208.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
The UFS top-up keeps BOOTCTR_FCTRY at 0x32; the hard hazards (0 / >0x32 / failed
write / flat pack) remain.  Operator's call when to stop and to wrap up.

NEXT (the one experiment): the POWER-DOMAIN/CM COMPARISON above — capture
PM_CORE/WKUP/L4PER/ABE_PWRSTST and the CM *_CLKSTCTRL on the oracle and Linux,
diff, then on Linux set a differing domain to the reference's value and re-run
the codec freeze detector (R0x220=0x0004 => the domain was the gate; 0x0002 =>
pad/board/PMIC, trap 165).  Also fix CM_ABE_PLL_REF_CLKSEL = SYS_CLK in the DTS.
Backlog: (a) a probe on fref_clk2_out/MCLK1; (b) a non-reference AIF1CLK design;
(c) the master-mode SRG stall; (d) touch cold-boot power/reset
(GPIO1_14/GPIO4_8); (e) the wl128x-nvs.bin path (hd5).  The unit is on LINUX
(0x36597040) + the rootfs b20302dc; region 0/region 1 both change when the
kernel/rootfs pair does, otherwise flash only the changed region.

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
