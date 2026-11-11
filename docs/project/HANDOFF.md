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
export at the session-41 wrap-up (**54 files / 8 113 lines**, sha256
`fc3b2e54…`, 0 rejects and 54/54 byte-identical).  It picked up session 41's
`omap4-winchester.dts`, `playbook-wm8994.c` changes and `sound/soc/ti/
omap-mcbsp.c`, which the session-39 export had omitted.  Trap 161.

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

## 0c. Item 9 step 5 — audio: the next experiments, all bench/software only

**Constraint (operator, session 41 end): there is no scope, no boardview and no
pad-level knowledge.  Every experiment must be run from the bench (the unit
itself) or from static RE.  Nothing may depend on probing a pin.**

That rules out the obvious next step (watch `abe_mcbsp2_dx`) and leaves four
software-only routes, in order of cost.

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
SRGR1 0x0f07    McBSP CLKGDV /8, CLKWD 16-bit
SRGR2 0x101f    FPER 31 (32 BCLK/frame), CLKSM 0, FSGM 0
AUXCLK1 (0x4A30A314) = 0x00020100   12.8 MHz, from a cold boot
```

and the **DAC audibly converts** — a steady noise floor, and a **brief
descending sweep at every stream start**.  That sweep matters: a DAC fed silence
cannot chirp, so **real samples do get clocked out**, and the failure is a
**stall after start**, not a dead data path.  Sustained programme audio is not
achieved.  The open measurement is:

```
DXR   = 0x00000000      <- 32-bit transmit register, offset 0x8
SPCR2 = 0x02f5          XRST=1  XRDY=0 (FIFO full)  XEMPTY=1 (shifter empty)
```

### E1 (cheapest, most likely) — make the McBSP a clock *consumer*

The reference runs the codec as **AIF1 master** (`R0x302 = 0x4000`) with its
McBSP as a consumer; ours is the reverse.  That structural difference puts the
McBSP's sample-rate generator in the path on our side and removes it entirely on
theirs.  Change **both** sides together (one boot, one variable = "clock roles
inverted"):

* board DTS: the `sound` node's DAI link format, and
* `playbook-wm8994.c`: `dai_fmt` to `SND_SOC_DAIFMT_CBC_CFC` → the codec as
  provider (`SND_SOC_DAIFMT_CBP_CFP` is codec-provider), and
  `snd_soc_dai_set_sysclk(cpu_dai, …, aifclk, SND_SOC_CLOCK_IN)` so the McBSP
  takes the codec's BCLK instead of generating it; drop the
  `set_clkdiv`/`PB_MCBSP_FCLK_HZ` maths on that path.

Read back `SPCR2` after the change: `XRDY` should stop being 0 with `XEMPTY` 1.

### E2 — capture what the McBSP actually transmitted (no probe)

**This is the discriminating test, and it matters because audio *was* heard** —
a faint descending sweep at every stream start (report 205 §5o), which a DAC fed
silence cannot produce.  So samples *do* get clocked out briefly and the fault is
a **stall after start**, not a dead path.

Enable a **capture** stream alongside playback: open `/dev/dsp` for reading at
the same format while `pbplay` writes, and dump what arrives.  The sDMA delivers
what the McBSP genuinely put on the line, so:

* capture shows the square wave `pbplay` wrote → the data path works and the
  codec is mis-framing it (compare `R0x300` `FMT` bits `[4:3]`, `XCR1`
  `XWDLEN1`, `SRGR1` `FWID`);
* capture shows a short burst then silence → confirms the stall, and the
  register dump after it says whether the sDMA stopped or the shifter did;
* capture is silent while the sweep is audible → the sweep is **not our data**,
  and the WM8994's DC servo or an FLL re-lock is the likelier source (hold both
  static and repeat).

`dx` and `dr` are the **two halves of one register** (`0x4A1000F8`; TRM Table
18-420), so a hardware loopback is also pure software: mux one half to `gpio`
(mux 3), toggle it from `/dev/mem`, and read the other half's `DATAIN`.

### E3 — ask the reference the same question with a real stream

Boot TabletOS, play a file, and while it plays read the **McBSP2 registers**
with `memdump3` (they are blocked from Linux userspace, trap 158, but QNX can
read them).  We have the codec side from a live reference (§5g of report 205) but
**not** its `SPCR2`/`SRGR1`/`SRGR2`/`PCR0` *during playback* — and that is exactly
the register set that differs in meaning between master and consumer.  This is
pure static/bench work and needs no probe.
Note `memdump3` reads physical memory, so `0x49024000`-based addresses are the
right ones to try (the "dma" window), and QNX does not enforce the Linux L4
firewall for these.

### E4 — re-read the driver against the reference's DMA usage

Static RE only: compare how the 1917 stack programs sDMA for McBSP2 (the
`deva-ctrl-omap4abe-wm8994.so` re in report 201 covers the codec; the McBSP/sDMA
side has **not** been decompiled).  A threshold/port mismatch would explain
`XRDY` stuck at 0 with the sDMA running.

### Driver facts you need before touching it

* The repair runs from a **delayed work 50 ms after `trigger(START)`** — the
  only point where BCLK is running (the machine `.trigger` is *first* in ASoC's
  trigger order, before the CPU DAI starts BCLK).
* The repair begins with **`regcache_mark_dirty()` + `regcache_sync()`**.  Without
  it every `update_bits()` is a silent no-op because the cache holds the values
  the driver tried to write while the codec was frozen.  **Trap 162.**
* `hw_params` writes `R0x200 = 0` to thaw, so **something must re-enable
  `AIF1CLK` afterwards** — otherwise the DAC loses its clock.
* The FLL is referenced from **BCLK**, not MCLK1 (MCLK1 locked only
  intermittently; see the open contradiction).

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

WHERE IT IS: FOUR software-only experiments, in cost order (there is NO scope,
NO boardview and NO pad knowledge — nothing may depend on probing a pin):
E1 make the McBSP a CLOCK CONSUMER, matching the reference (codec as AIF1 master,
R0x302=0x4000) on BOTH sides at once — the DAI link format plus the CPU DAI
taking the codec's BCLK — which removes the sample-rate generator from the path
where XRDY=0 sits.  E2 prove the transmit path without the codec: dx/dr are the
two halves of one register (0x4A1000F8), so drive one as a GPIO and read the
other from /dev/mem, or use internal loopback with a capture stream so the sDMA
delivers what was transmitted into a /dev/dsp read.  E3 boot TabletOS and read
the reference's McBSP2 SPCR2/SRGR1/SRGR2/PCR0 DURING playback with memdump3 (the
"dma" window 0x49024000; Linux userspace cannot, trap 158) — we have its codec
file but not its McBSP mid-stream.  E4 static RE of the 1917 sDMA/McBSP setup
(report 201 decompiled the codec, not the McBSP/sDMA).

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
