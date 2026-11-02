# Session 15 (absolute) — the QNX register oracle, the display fix, and a portable workspace

> Numbering: absolute 15.  The reports call it Linux-era **"session 9"** (`NN-6`).
> This note is written by the session itself, and was rewritten at wrap-up: the
> first half was recorded in it while the session was still running.

Date / span: 2026-10-29, one long session.
Unit state at the end: **TabletOS from the sealed IFS** (`sha256 c72b0530…`,
`os_type` 6, stored CRC `0x116B2562`), `BOOTCTR_FCTRY = 0x32`, door up, **no test
image on media**, no device-side files.

## Goal of this session

The handoff's FIRST TASK: raise `PPI_TX_RX_TA` to `0x00040006`, re-read
`0x0214/0x0218/0x0500/0x0300` with reads that could be trusted, and — with no
scope on the bench — find out whether the DSI clock lane switches.

That was answered, and then the session changed shape twice, both times because
of the operator: (a) the spare's own TabletOS displays correctly, so the hardware
path was proven good and an on-device oracle became possible; (b) with a black
framebuffer the panel showed white lines, which cannot be carried data.  The
session ended with **the display working**, the workspace portable and moved to
SSDdrive, and the factory/CRC path documented.

## Confirmed (measured, read in a file, or seen on the bench)

**Read reliability and the TA value**

* `PPI_TX_RX_TA (0x013C)` decides whether bridge reads answer, and the value is
  **latched at `PPI_STARTPPI (0x0104)`**: boot with `4` → boot-time sampling 4/8,
  console samples 8/16; boot with `0x00040006` → 8/8 and 16/16; writing `0x013C`
  after boot changes nothing.  [images `0xFCC09B7B`, `0x5934491B`, `0xD670FA8C`;
  report 132 §3]
* The VC re-arm (trap 37) does **not** change the read success rate: in the `4`
  boot, `t0214` 8/16, `u0214` 8/16 and 9/16.  [`0x5934491B`]
* Reading the bridge on a live QNX link with the reference's own `TA = 4` fails
  about half the time with an ACK/error `0x0003`; the successful samples agree.
  [`tools/pbdsi` runs; report 132 §2]

**The QNX register oracle and the whole-map diffs**

* `tools/pbdsi` reads TC358764 registers over DSI from a running TabletOS
  (generic read request DT `0x24` on hw VC 1, BTA, RX FIFO pop; response is a
  generic long read, DT `0x1A`, 4 bytes + 2-byte checksum).  Its mapping sanity
  print reproduces the reference values `DSI_CTRL = 0x00EAEF9D`,
  `VC_CTRL(0) = 0x20808791`, `VC_CTRL(1) = 0x20009D81`.  [report 132 §2;
  `docs/reports/data/qnx-bridge-map-2026-10-29.txt`, 353 registers]
* On that **working** display: `0x0214 = 0`, `0x0218 = 0x1D`, `0x0500 = 0`,
  `0x0230 = 0xFFFFFFFF`, `0x0464 = 0`.  [same capture]
* Our `VP_HTIM1/HTIM2` were `0x78000001`/`0x76000400`; the reference writes
  `0x00780001`/`0x00760400`.  Readback was `0x00000001`/`0x00000400`, i.e.
  HBP = HFP = 0.  After the fix readback matches and `VP_VFUEN (0x0464)` reads 0
  like QNX.  [image `0x8C82DD62`; report 132 §5]
* Whole-map diff after the fix: **4 differences** — `013c` (our deliberate
  `0x40006`), `0220`, `0270`, `0300` (live status/counters).  Host-side DSI
  block, PHY (25 words) and PLL compared the same way: PHY/PLL identical, DSI
  differences are IRQ enables and FIFO allocation only.  [report 132 §4/§6 and
  this session's `peek` captures]

**The display**

* With `0x8C82DD62` + `spi tconbe` the operator saw the first image of ours on
  the panel: a magenta band, then alternating black/white lines.  [operator;
  report 132 §7]
* With the TCON table sent by `/init` at bring-up (`0xD9CB7D72`) the picture
  became *mostly* correct.  [operator; report 133 §3]
* `/sys/kernel/debug/omapdss/clk` on `0x01F0F890`: `FCK = 48000000`,
  `dispc fclk source = FCK`, `DISPC-CORE-CLK lck 48000000`, `LCD pck 50400000`.
  [report 133 §2]
* Raising the DSS fck logs `pb dss: parent want 100800000 rr 0 -> 96000000, fck
  96000000`, and the panel shows **solid white, solid red, and the kernel console
  as text**.  [image `0x068C867F`; operator; report 133 §3/§4]
* The PER PLL itself did not change (`4A008140 = 7` before and after); only its
  output dividers moved (M5 `0x23 → 0x03`).  [report 133 §3]
* The portable rebuild `0x8C146805` (repo-relative `CONFIG_INITRAMFS_SOURCE`)
  reproduces it: solid clean magenta.  [operator; this session]

**Tooling and tree**

* The build tree's `.config` had `CONFIG_INITRAMFS_SOURCE` pointing at
  `…/rea-work/…/initramfs/root`, so every image built here embedded rea-work's
  older `/init` (455 284 B, 27 Oct).  Corrected; the built cpio now carries this
  repo's `init` (590 608 B) and the string `no auto-reboot`.  [trap 43]
* The documented `v` op was unreachable (`sscanf` returns 1 for a bare `v`, the
  code rejected `n < 2`); fixed.  [report 132 §10]
* The workspace moved to `/home/psyden/SSDdrive/playbook-linux`; the tools
  derive their paths from their own location and take `PB_KEY`/`PB_PUB`/
  `PB_SYSTEMMAP`/`PB_IFS_TEMPLATE`/`QNX660` overrides.  [commits `f47fbef`,
  `f28fd53`, `5f57959`; `docs/project/SETUP.md` §8]
* Kernel patch re-exported twice; final **4 169 lines / 32 files**, verified by
  applying to a pristine tree extracted from `local/pristine/` and comparing all
  32 files byte-identically.  [`kernel/patches/README.md`]

**Bench at the end (re-read, not remembered)**

* `lsusb` → `0fca:8010`; `tools/pbssh.sh 'uname -a'` → TabletOS
  (`OMAP4430_Winchester_ES2.2_HS_PVT4_Rev:07 armle`); door up, supervisor running
  from the new tree.
* media → `dd if=/dev/hd1 bs=512 skip=2048 count=5632`: sha256 `c72b0530…`,
  `os_type` byte at `0x100` = 6, stored CRC `0x116B2562`.  **No chainloader or
  kernel image on media.**
* `BOOTCTR_FCTRY` → `32 00 00 00` (`0x32`).
* Device-side files: `/tmp/pbdsi` and `/tmp/pbi2c` are **gone** (QNX `/tmp` is
  wiped per boot).

## Inferred (reasoning from evidence, not directly measured)

* **The frame was generated wrong, not carried wrong.**  A black framebuffer
  produced white lines and coloured dots, and content changes did reach the
  panel.  Strong.
* **The DISPC core clock below the pixel clock was the last defect.**  Every
  register matched a working system; raising only the fck fixed the picture; the
  symptom (clean left edge, failing across the line) matches a pipeline serviced
  too slowly.  Strong; the mechanism inside the DISPC is not measured.
* **`LANESTATUS0/1`, `SYSSTAT`, `INTSTATUS`, `0x0270`, `DSIERRCNT` are not fault
  indicators** — they read the same on a working display.  Strong for "useless as
  evidence"; their meaning is unknown.
* **The PER-PLL divider change can affect other peripherals.**  eMMC is on M2
  (unchanged); backlight I²C and TCON SPI still work.  Medium.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`LANESTATUS0 = 0` means no lane is in HS RX, so the clock lane is the
  fault."**  Two sessions of reasoning rested on it; the oracle read `0` on a
  working display.  Killed by: `tools/pbdsi` on TabletOS.  Lesson: a register
  whose meaning is unknown is not evidence.
* **"`VP_HTIM1/2` only store their upper half."**  It was our own constant in the
  reserved byte.  Killed by: the reference's value in `winch_lcdctl` and the
  whole-map diff.
* **"A live write of `PPI_TX_RX_TA` is the A/B."**  It showed 16/16 both ways and
  looked like "no effect" — the value is latched at `PPI_STARTPPI`.  Killed by:
  three boots, one value each.
* **"The console's `v` command works."**  Documented, in `help`, unreachable.
  Killed by: running it.
* **"Editing `initramfs/init.c` changes the image."**  It did not.  Killed by:
  `cpio -itv` on the built cpio.
* **"Our TA deviation destabilises the video."**  A build with the reference's
  `4` was no better.  Killed by: `0xD2602864`.
* **"Reading DSS/DSI registers is harmless."**  Repeatedly the operator saw the
  picture shift right after a `peek` of the DSI block or a `cat` of the DSS
  register dumps.  Killed by: operator observation (trap 46).
* **"Flashing a CRC image onto an unarmed unit erases its OS region."**  Wrong
  for these units: the retry branch **halts** on a failed verify without erasing;
  the nuke is the normal path (`A >= B`) and has never been observed.  Corrected
  at wrap-up after the operator pushed back — `FACTORY-PATH.md` §1/§4.
* **"The DSS fck warning is cosmetic."**  It was the display bug.  Killed by:
  `/sys/kernel/debug/omapdss/clk` and one build with the rate raised.

## Unknown / open questions

* **Does the normal-path nuke ever run on a *verify failure*?**  Documented
  statically (report 71 §3.4), never observed.  Settled by a sacrificial unit
  with `0x2801 A >= B` and a bad image.  [contradiction file, open]
* **What do the bridge status registers mean** (`LANESTATUS0/1`, `SYSSTAT`,
  `INTSTATUS`, `0x0270`, `DSIERRCNT`)?  Needs the TC358764XBG register manual
  (V1.19, not public).  No longer load-bearing.
* **Is `GPIO2_24` the bridge RESX?**  Open
  (`docs/contradictions/gpio2-24-resx-vs-datasheet.md`).
* **Does every retail PlayBook start unarmed?**  Measured on one unit (report 95
  §1); other units' history is unknown.
* **Can the DSS clock fix become a clock-initialisation property?**  Settled by
  moving it to the DT/board clock setup and re-testing the picture and eMMC.

## Superseded / uncertain

* **Report 131 §4a/§4b/§4f** — the `LANESTATUS0`/`SYSSTAT` reading and the
  clock-lane conclusion.  Withdrawn by report 132 §4.
* **Report 131 §3's "upper half not stored"** — it was our constant; report 132
  §5.
* **Report 131 §4g** — confirmed, with the latching mechanism (report 132 §3).
* **The session-13/14 TCON byte-order observations** — both orders drive the
  panel; the earlier "white vs partial image" was the unprogrammed bridge
  (report 132 §8).
* **This note's own first version** — it presented the session as the TA
  experiment ending in a glitchy image; the sections above are current.
* **The mid-session claim that an unarmed unit would be nuked** — corrected in
  `FACTORY-PATH.md` §1/§4.

## Artefacts produced

**Reports**: `132-session15-oracle-porch-fix-first-image-2026-10-29.md`,
`133-display-fixed-dss-core-clock-2026-10-29.md`.
**Raw captures**: `docs/reports/data/{qnx-bridge-map-2026-10-29,
linux-bridge-map-0x8C82DD62, qnx-dispc-2026-10-29, linux-dispc-0x8C82DD62}.txt`.
**Canonical doc added**: `docs/project/FACTORY-PATH.md`.
**Tools**: `tools/qnx-dsiread.c` → `tools/pbdsi` (built, stripped);
`tools/get_pristine.sh` (new); portable paths in `pbssh.sh`, `door-hold.sh`,
`pb_ring.py`, `pb_events.py`, `BerryShell-V4.py`, `build_kernel_image.py`,
`pre_os_ifs_flash.py`.
**Source**: `kernel/source/dsi.c` (`PPI_TX_RX_TA = 0x00040006`, corrected
`VP_HTIM1/2`, failure-counted boot reads, `t`/`u`/`d`/`v`), `kernel/source/tc358764.c`
(same values), `local/kernel-tree/…/dss/dss.c` (**the PER-PLL rate request that
fixed the display**), `initramfs/init.c` (no lifetime cap, Hitachi TCON table at
bring-up, debugfs dump), `kernel/winchester.config` (relative initramfs,
`CONFIG_OMAP2_DSS_DEBUGFS=y`).
**Images** (`local/images/`): `0xFCC09B7B`, `0x5934491B`, `0xD670FA8C`,
`0x8C82DD62`, `0xD9CB7D72`, `0xD2602864`, `0x95B03A6C`, `0x01F0F890`,
`0x068C867F`, **`0x8C146805` (last known-good, re-verified)**.
**Patch**: `kernel/patches/0001-playbook-winchester-6.15.11.patch`, 4 169 lines /
32 files.
