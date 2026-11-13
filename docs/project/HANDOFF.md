# Handoff — session 45 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§7).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 45 = Linux-era 39**, updated at its
wrap-up.  See `docs/sessions/NUMBERING.md`.

> **Session-45 result, in one line:** **the transport was fixed and the
> silence was localised to one pin** — `CLKSM = 0` was the session-41 stall, the
> SRG divides the ABE *interface* clock (49.152 MHz), the stream now runs at
> 48 kHz with no MCLK1 — and the codec's **DACDAT is `abe_mcbsp1_dx` (pad
> `0x102`)**, not `abe_mcbsp2_dx`, which E5 drives (report 212).

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain, the 64 MiB ext4 BusyBox root in region
1, the peripherals (I²C4 motion sensors, TMP103, BQ27541, FAN5702, CM3217,
ISL9519 charger), and the **Munin touch** (calibrated, IRQ wired; §0d).

**Audio is half-solved and the half that moved is the transport.**  With E5
(session 45) the CPU generates BCLK/LRCLK, the codec is the AIF1 slave, its
FLL1 locks from our BCLK, `AIF1CLK` exists from the FLL, and a stream runs at
48 096 frames/s with the application's samples verified in the DMA ring.  **The
codec outputs silence** (a start crackle, then the noise floor).  The open
question is the codec's AIF1 **data input** — which ball carries DACDAT, and
whether the reference's DSP_A format is needed (§0c).

**The patch** `kernel/patches/0001-playbook-winchester-7.3.3.patch` is the
session-45 export (**54 files / 8 305 lines**, sha256 `180f5e19…`, 0 rejects and
54/54 byte-identical).

## 0a. The media, and the way back

* **region 0 = the session-45 Linux image** (`boot/chainloader/kernel.img`,
  CRC **`0x92458D00`**, 0x939C5C bytes; kernel `7.3.3` #33, E5 clock roles, the
  `mcbsp2_pins` pads back to `PIN_OUTPUT`, and the **disabled** `&mcbsp1` node +
  `mcbsp1_pins` group for the next experiment).
* **region 1 = the audio/OSS rootfs** `a026a454…`
  (`local/images/rootfs-region1-audio-oss.ext4`, 27 modules, `pbplay`/`pbmix`,
  plus the two session-45 diagnostics).
* The unit is **on our Linux image** (`0fca:8010`, console `pb> `).
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825` (the pruned RCFS is still at `0x8D5`/region-0 `0xC00000`), then
  reboot.  It enumerates `0fca:8012`; qconn `169.254.0.1:9090`, root ssh.
* **To stock TabletOS:** flash `local/work/minimal/wayback.signed`.  **Do not
  restore `local/images/ifs0.sealed.bin`** (2.0.0.4869, trap 84).
* **To rebuild the oracle:** `python3 tools/build_minimal_qnx.py`.

## 0b. Item 9 step 4 (WL1283) — software surface exhausted

Unchanged and **formally stopped by the operator** (session 36).  The 1917-truth
minimal `&mmc5` host probes but `attach_sdio no` (report 196).  Do not re-run
traps 116–137.

## 0c. Item 9 step 5 — audio: the transport runs, the codec does not decode

### What is proven (do not re-derive)

* **`CLKSM = 0` was the session-41 stall.**  Per TRM Table 23-52 the SRG is
  clocked from the **CLKS pad** when `CLKSM = 0`; this board drives no CLKS
  (`pad_clks_ck` 12 MHz, `enable_cnt 0`, no consumer).  No `CLKG` ⇒ no FSX/BCLK
  ⇒ `XRDY = 0` / `XEMPTY = 1`.  Trap 187.
* **The SRG divides the ABE interface clock: 49.152 MHz** (`ocp_abe_iclk` =
  `DPLL_ABE/2`), not the 12.288 MHz the framework reports as the McBSP `fck`.
  Measured 192 450 frames/s with `CLKGDV = 8` (4.009×); `CLKGDV = 32` gives
  **48 096 frames/s**.  Trap 188.
* **E5 is implemented and verified on the transport**: `.dai_fmt = I2S |
  CBC_CFC | NB_NF`, CPU `set_sysclk(OMAP_MCBSP_SYSCLK_CLK, 49152000)` +
  `set_clkdiv(CLKGDV, 49152000/bclk)`, codec FLL1 from **BCLK**,
  `mcbsp2_pins` `clkx`/`fsx` = `PIN_OUTPUT`.  Measured: `SRGR1 = 0x0f1f`,
  `SRGR2 = 0x301f`, `PCR0 = 0x0f0f`; codec `R0x302 = 0x0000` (slave),
  `R0x732 = 0x6020` (FLL1 locked), `R0x224 = 0x0c83`, `R0x200 = 0x0011`.
* **The application's samples reach the DMA ring**: `runtime->dma_area` read at
  `trigger(START)+50 ms` shows the ±12000 square wave at the header and at
  `hw_ptr`; `XBUFFSTAT` toggles 0/1 across eight samples and `IRQST = 0x0710`
  (XFSX|XBR|XEOF).  **`DXR`/`DRR` are not readable data registers** — they
  return constants (trap 189).
* **The codec's output chain matches the reference register for register**:
  DAC1 volumes `0xc0`, `R0x601/0x602 = 0x0001`, `R0x02d/0x02e = 0x0001`,
  `R0x060 = 0x00ee`, `R0x210 = 0x0083` (SR 48 kHz, AIF1CLK_RATE 256), DAC1
  mute clear.  The **codec is silent anyway** (operator, headphones: a start
  crackle, then the floor).
* **MCLK1 is still dead at the pin** (reports 206/208/209) and the power/CM
  domains are causally excluded (report 209).  E5 routes around MCLK1; it does
  not fix it.

### The fault is the data ball, and the board splits the two engines (solved)

* **The codec's DACDAT is `abe_mcbsp1_dx`** (pad `0x102`): on the oracle, with
  `audio_out` actually playing a wav (operator confirmed audible), pad `0x102`
  reads **mux 0, output**, while `abe_mcbsp2_dx` (what E5 drives) reads **mux 7
  = safe_mode**.  A during-vs-after pad dump over `0x4A100000`–`0x4A1001FC`
  shows **no changes** — the configuration is static, so "which pad is an
  output" is the question, not "which pad changes".
* **The format is not the fault.**  A DSP_A build (`R0x300 = 0x6018`, the
  reference's format) was tried and stayed silent, because the ball was wrong.
* **McBSP1 cannot be the clock master.**  E6 (McBSP1 enabled, mcbsp2 disabled,
  `dsp_a`) clocked `0x0fe`/`0x104`/`0x102` at 48 053 frames/s but the codec's
  **FLL did not lock** (`R0x732 = 0x6000`) — the codec's BCLK/LRCLK are on the
  **MCBSP2** balls (proven twice: E5 and the pre-E1 BCLK-referenced FLL).
* **The two-engine design is built, and it reaches the wall (E9, report 212
  §14).**  Two links work mechanically — `mcbsp2 + wm8994-aif1` (clocks+codec)
  and `mcbsp1 + wm8994-aif2` (data, consumer; the link must be `CBP_CFP`, and
  the second PCM must open at the card's rate — traps 195/196).  But the data
  port **stalls**: McBSP1 sits at `SPCR2 = 0x0235` (`XRDY = 0`, `XEMPTY = 1`)
  and the write dies after five periods, i.e. **no frame sync reaches it**.
* **E7 is withdrawn** (trap 197): "a word was consumed in 5 ms" was a misread
  of `XRDY`, which is 1 in both "ready" and "never moved" cases.  McBSP1's
  clock pins are **not** on McBSP2's nets.
* **The wall, and it is now proven from the reference's code:** decompiling
  `deva-ctrl-omap4abe-wm8994.so` (report 212 §16) shows its McBSP is a clock
  **consumer** — `SRGR2` is written `FPER | 0x1000` (`FSGM=1`, `CLKSM=0`) and
  slave mode sets only `XRST`/`RRST`, never `GRST`/`FRST` — clocked through the
  **ABE** (`abe_connect_serial_port … mcbsp_id`).  Mainline has no ABE
  serial-port clocking (`omap-abe-twl6040.c` is a machine driver only).
* **Do not program a McBSP from raw registers** — E8's attempt aborted on the
  idle L4_ABE target and took the unit down (trap 193).

### Driver facts you need before touching it

* The repair runs from a **delayed work 50 ms after `trigger(START)`** and
  begins with **`regcache_mark_dirty()` + `regcache_sync()`** (trap 162).
* `hw_params` writes `R0x200 = 0` (thaw), and the repair re-enables `AIF1CLK`
  **last**.
* A frozen codec drops **raw `i2ctransfer` writes too** (trap 167).
* `omap-mcbsp`, `snd-soc-wm8994` and the machine driver are **modules**: a
  change to them does **not** change the chainloader CRC, and needs a
  **region-1-only** flash (trap 190).  Copy the rebuilt `.ko` into
  `local/work/modules-7.3.3/` first (trap 163).
* Two diagnostics are in the tree and in the patch: the machine driver's
  `pcm:` ring readback and `omap-mcbsp`'s eight-sample `live N:` dump.  They
  are the fastest way to answer "is the data there"; retire them when audio
  works.

## 0d. Touch — done, and it survives a warm reboot; cold boots need the pulse

I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` boots the Munin app
and maps the 1917 `scaling.conf` calibration to 1024×600; the GPIO2_3 edge IRQ
works (pad `0x056`, `0x410B`, `irq 110`).  A warm reset keeps the controller up;
after a **cold** boot it does not until the power/reset GPIOs (**GPIO1_14 /
GPIO4_8**) are pulsed — the driver still does not drive them.

## 0e. The minimal 1917 oracle (session 43; report 207)

A debloated 2.1.0.1917 image that boots in ~30 s and gives a shell over USB.

* **Media:** region 0 = the shim IFS at `0x825` + the pruned RCFS at `0x8D5`
  (`local/work/minimal/setB.signed`).  It is **not** on media at the start of
  session 46 — region 0 holds `0x7A5DD941`.
* **Connect:** `python3 tools/QCL.py info` (qconn, `169.254.0.1:9090`) or
  `tools/pbssh.sh` (root, `uid:0`).  Host link `enp8s0f3u1` carries
  `169.254.0.2/16` — do not re-add.
* **Audio HAL:** `/base/sbin/io-audio -d omap4abe-wm8994 aif=0,secure_capture
  -o enable_buffering`; it leaves FLL1 locked (`R0x732 = 0x6020`).
  `slay io-audio` before the freeze detector.
* **Tools on the UFS:** `/accounts/devuser/bin/{memdump3,memw32,pbi2c}`; they
  survive reboots.  WM8994 writes need `pbi2c wr16r` (trap 176).
* **Bench only:** unauthenticated qconn/root ssh — never on a network (trap 173).
* **A reboot restores volatile register state** (trap 177).

## 1. Useful local extracts

* `local/work/1917/base/`, `ifs/`, `calib/`, `parts/` — the 1917 `/base`,
  IFS tree, WLAN calibration, pulled partitions.
* `local/vendor/bb10mt/bb10mt` — the QNX6/container tool.
* The minimal oracle — `tools/build_minimal_qnx.py`, `local/work/minimal/`.
* The WM8994 datasheet — Rev 4.6, `hardware/WM8994.PDF` (gitignored).  The text
  extracts well with `pdftotext`: Table 106 = the AIF1 clocking codes,
  §"AIF1 Control (1)" = the format encoding.
* Reports **207**, **208**, **209**, **212** (current); captures in
  `docs/captures/` (`wm8994-e5-master-srg-2026-11-13.txt`,
  `wm8994-pm-cm-2026-11-12.txt`, `wm8994-oracle-live-2026-11-12.txt`).

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

**A module-only change keeps the CRC** (trap 190): build `modules`, copy the
`.ko` into `local/work/modules-7.3.3/` (trap 163), rebuild the rootfs and flash
**region 1 only**:

```bash
python3 tools/build_rootfs.py --size-mb 64 \
    --modules local/work/modules-7.3.3 --krel 7.3.3 \
    --out local/images/rootfs-region1-audio-oss.ext4
python3 tools/bbusb.py flash --component local/images/rootfs-region1-audio-oss.ext4 \
    --dest-block 0x2915 --from-bootrom --password 0000 --apply &
tools/pbcon.py 'reboot -f'
```

Both regions (DTS or kernel change) use the `both.lst` recipe in
`DEVELOPMENT.md` §4.

**Patch export** (trap 161): regenerate from `local/pristine/linux-7.3.3` over
the old patch's file list (new files become `--- a/<path>` with `@@ -0,0 +1,N @@`),
then **apply it to a staged copy of the pristine files and compare all 54** to
`local/kernel-tree` — 54/54 byte-identical is the acceptance test.

## 3. Bench

* **The unit is on our Linux image** (`0fca:8010`, console `pb> `).
  `tools/pbcon.py 'cmd'`; reboot with `tools/pbcon.py 'reboot -f'`.
  The tty comes back `0666` via the installed
  `tools/99-playbook-cdc-acm.rules`; if it does not, `pkexec chmod 666
  /dev/ttyACM0` **only after a genuine re-enumeration**, and **wait for the
  operator** rather than working around it (trap 191).  After a kernel flash
  the ACM gadget may need a **physical replug** (trap 184); a console that
  echoes but prints nothing is a **busy shell** (trap 164).
* **The oracle** (`0fca:8012`): `tools/QCL.py info|exec|put|rm`,
  `tools/pbssh.sh 'cmd'` as root.  Reboot it with `QCL.py exec
  /proc/boot/shutdown -S reboot -f`.
* Device password `0000`, once.  An agent **never writes RPMB**; NVRAM is
  read-only (SAFETY §1/§6).  The image's UFS top-up keeps `BOOTCTR_FCTRY` at
  `0x32`.
* WM8994 register work: `i2ctransfer` arguments are **DECIMAL** (trap 166);
  bus `4`, address `0x1a`, 16-bit registers.  A frozen codec drops raw writes
  too (trap 167).  On the oracle use `pbi2c wr16r` (trap 176).
* QNX device tools build **`-static`**; `/tmp` is wiped by a reboot.

## 4. Traps added last session (full list 14–191 in `KNOWN-ISSUES.md` §2)

* **187.** `CLKSM = 0` clocks the SRG from the CLKS pad; this board drives no
  CLKS (`pad_clks_ck` 12 MHz, no consumer).  `CLKS_FCLK`'s `clk_set_parent` is
  not the SRG mux.
* **188.** The SRG divides the ABE **interface** clock (49.152 MHz), not the
  12.288 MHz the framework reports.  Measure `hw_ptr`, do not trust the fclk.
* **189.** `DXR`/`DRR` are not readable data registers; use `XBUFFSTAT` and
  `IRQST`.
* **190.** `omap-mcbsp` and the machine driver are modules: a module-only
  change keeps the CRC and needs a region-1-only flash.
* **191.** A listening test must state what is playing (`pbplay … 0` is
  silence); `pkexec` is a wait-for-the-operator, not a workaround.

## 5. Where things are

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console | `docs/project/DEVELOPMENT.md` |
| **minimal 1917 oracle** | `tools/build_minimal_qnx.py`, `local/work/minimal/` |
| board DTS + patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + config | `kernel/patches/`, `kernel/winchester-7.3.3.config` |
| reference codec captures | `docs/captures/wm8994-*` |
| 1917 audio driver (RE'd) | `local/work/1917/base/lib/dll/deva-ctrl-omap4abe-wm8994.so` |
| audio machine driver | `local/kernel-tree/sound/soc/ti/playbook-wm8994.c` |
| McBSP driver (patched) | `local/kernel-tree/sound/soc/ti/omap-mcbsp.c` |
| OSS player / mixer | `rootfs/pbplay.c`, `rootfs/pbmix.c` |
| qconn / ssh | `tools/QCL.py`, `tools/pbssh.sh` |
| console udev rule | `tools/99-playbook-cdc-acm.rules` |
| reports | `docs/reports/` (**212** is this session; 207 the oracle; 208 AUXCLK1; 209 power/CM) |
| sessions | `docs/sessions/session-45.md` (last session) |

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
* **A listening verdict requires knowing what is playing** — check
  `/proc/asound/card0/pcm0p/sub0/status` and the `pbplay` line first.
* Subagents: static/file-only, never a `tools/` script (trap 109).
* **No restore is required at wrap-up.**  Leaving the unit on Linux is valid.

## 7. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 46 (absolute; Linux-era 40)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 9, KNOWN-ISSUES.md (traps 14-191, especially 165-191,
and §3), DEVELOPMENT.md §3/3a/4/7a, DECISIONS.md (44-57), docs/reports/INDEX.md,
reports 206, 209 and 212, and sessions/session-45.md.

WHERE IT STANDS: the audio TRANSPORT is solved and the codec still decodes
nothing.  E5 (session 45, report 212) made the McBSP the bit/frame provider
again with the codec as AIF1 slave and its FLL1 referenced to our BCLK — no
MCLK1 in the path.  The session-41 stall was CLKSM=0 (SRG clocked from the
undriven CLKS pad; trap 187); the SRG divides the ABE interface clock,
49.152 MHz, not the 12.288 MHz the framework reports (trap 188), so CLKGDV=32.
Measured: SRGR1=0x0f1f, SRGR2=0x301f, PCR0=0x0f0f, codec R0x302=0x0000,
R0x732=0x6020, R0x224=0x0c83, R0x200=0x0011, hw_ptr 48 096 frames/s.  The
application's +/-12000 tone is verified in runtime->dma_area and XBUFFSTAT shows
the sDMA feeding the transmitter (DXR/DRR are NOT readable data registers,
trap 189).  The operator hears only a start crackle plus the DAC noise floor.
The playback chain matches the reference register for register (DAC1 0xc0,
R0x601/2=1, R0x02d/2e=1, R0x060=0x00ee, R0x210=0x0083).
MCLK1 is still dead at the pin (reports 206/208/209); E5 routes around it.

THE FAULT IS THE DATA BALL (report 212 §10): the codec's DACDAT is
abe_mcbsp1_dx (pad 0x102) — on the oracle, actually playing, pad 0x102 is mux 0
OUTPUT while abe_mcbsp2_dx (what E5 drives) is mux 7 SAFE MODE, and the
configuration is static.  DSP_A is NOT the fault.  McBSP1 cannot be the clock
master (E6: its clocks do not reach the codec), but E7 proved the engines SHARE
the clock nets: a consumer-configured McBSP1 consumed a DXR word in 5 ms while
McBSP2 streamed.  So: mcbsp2 = clocks, mcbsp1 = data, as two driver-managed
ASoC links.  E8's attempt to be the clock generator with raw mcbsp2 registers
CRASHED the SoC (imprecise external abort, trap 193) — do not do that.
CLOSES WHEN: pbplay's 440 Hz tone is audible at 48 kHz +/-1%.

PHYSICAL STATE: REGION 0 HOLDS OUR LINUX IMAGE (CRC 0x5A04ACD9, 0x9395C4,
kernel 7.3.3 #35, the committed E5 tree).  REGION 1 holds the audio/OSS rootfs
c6dfd25f... (67,108,864 B, 27 modules).  The unit is on LINUX (0fca:8010, console `pb> `).
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
-f'`; the tty comes back 0666 via tools/99-playbook-cdc-acm.rules, otherwise
`pkexec chmod 666 /dev/ttyACM0` ONLY when the operator is present — wait for
them, do not work around it (trap 191); after a kernel flash the ACM gadget may
need a PHYSICAL USB REPLUG (trap 184); a console that echoes but prints nothing
is a BUSY SHELL (trap 164).  Run USB/flash tools as the USER, never pkexec.
Device password 0000, once.  An agent NEVER writes RPMB; NVRAM is read-only
(SAFETY §1/§6).  The oracle is UNAUTHENTICATED (qconn 9090, root ssh, firewall
off): bench only, never on a network (trap 173).  After every reboot, check
`pidin arg` and the services.  WM8994 register work: i2ctransfer arguments are
DECIMAL (trap 166); bus 4, addr 0x1a; a frozen codec drops raw writes too
(trap 167 — thaw with R0x200=0, re-enable AIF1CLK last).  On the oracle use
`pbi2c wr16r` (trap 176).  A LISTENING TEST MUST STATE WHAT IS PLAYING: check
/proc/asound/card0/pcm0p/sub0/status and the pbplay line — `pbplay N 0` is
silence (trap 191).

STATIC RE: the REA MCP server exposes Ghidra 12.1.4 (open_binary,
binary_overview, analyze_function, procedure_pseudo_code, search_strings,
xrefs, read_bytes); raw ARM import format=raw-binary, architecture=arm,
entry_point=0x80100000.  Reconcile with ntoarmv7-readelf/objdump (Ghidra may add
+0x10000 on an ELF).  QNX device tools build -static, and /tmp is wiped by a
reboot.  USE THE REFERENCE FIRST — the highest-yield method is a LIVE REGISTER
DIFF against the oracle itself.  The WM8994 Rev 4.6 datasheet is LOCAL:
hardware/WM8994.PDF (gitignored, do not fetch it online) — it extracts with
pdftotext; Table 106 = AIF1 clocking codes, "AIF1 Control (1)" = the format
encoding.  The TI TRM is hardware/swpu231ap.pdf.txt (Table 23-52 = SRGR2).

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed;
a MODULE-only change keeps the CRC and needs a region-1-only flash (trap 190).
The UFS top-up keeps BOOTCTR_FCTRY at 0x32; the hard hazards (0 / >0x32 / failed
write / flat pack) remain.  Operator's call when to stop and to wrap up.

NEXT (an ABE bring-up project, in three steps — ROADMAP item 9 has the same
list): (1) **feasibility, static and free** — read the TRM's ABE chapter for the
serial-port clocking and decide whether an ABE serial port can be clocked and
pointed at a McBSP *without* the DSP firmware.  The reference's firmware is in
hand (`abe_firmware_array` is embedded in
`local/work/1917/base/lib/libabe.so.1`) and the QNX HAL's sequence
(`abe_initialize`, `abe_init_mem`, `abe_connect_serial_port`,
`abe_configure_port`) is the map.  (2) **confirm the routing on the live oracle**
— which `mcbsp_id` the audio port binds to (it comes from a per-port field in
the driver's table, not a constant) and whether `abe_clks` or an internal ABE
clock feeds it.  (3) **only then a driver**: if the ABE can clock a McBSP, E9's
two-link build (report 212 §14) becomes the audio path; if it cannot, record the
limitation and stop.  Everything up to the data port is proven: E5's clocks, the
codec's FLL lock from our BCLK (cold and warm), the 48 kHz rate, and the samples
in the DMA ring.  Backlog: (a) the MCLK1 probe (trap 165); (b) the speaker path
(SPKMIX is not routed — headphones only); (c) touch cold-boot power/reset
(GPIO1_14/GPIO4_8); (d) the wl128x-nvs.bin path (hd5).

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
