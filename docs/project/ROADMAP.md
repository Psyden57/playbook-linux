# Roadmap

Ordered. Each item states the evidence that would close it, so "done" is not a
matter of opinion. Items 1–3 and 3a–3d are closed (the display works). The
1917 image work is complete and lives in `IFS-REBUILD.md` /
`IMAGE-REBUILD-GUIDE.md`. **Item 8 (BB10) and the image work are parked by the
operator (session 25 end)** — their documents stay, but the work returns to
the Linux items: **item 6 (the factory-counter top-up) is the blocker, then
items 4, 5 and 7.**

## 1. Force the complete QNX timing set as ONE experiment — **DONE, negative**

Forced together and read back: `DISPC_TIMING_H = 0x0890A113`,
`DISPC_DIVISOR = 0x00010002`, `DSI_VM_TIMING1 = 0x01079074`,
`DSI_CLK_CTRL = 0xA0346005` (the divisor/LP divisor required selecting the
reference's HSDIV mX = 6 as well, so the set stayed coherent).  Panel still
black.  Closed by report 124.

## 2. Compare and force the DSI PHY and PLL words — **DONE, byte-identical**

Only `CFG0/1/2` differed; substituted with QNX's `0B190D13`/`42040A22`/`B8800009`.
The PLL was then made exact too (`CFG1 = 0x14A5E8BE`, `CFG2 = 0x00056000`, REGM
756 / REGN 96 / mX 6, `PLL_STOPMODE` and CFG2 3:1 clear).  Closed by reports
124/127.

## 3. Question the LVDS mapping — **CLOSED, not a variable**

The reference *does* program the bridge (report 125), and its `LV_MX0003` …
`LV_MX2427` words are **identical to upstream's VESA-24 table**.  The mapping is
therefore not the difference between the reference and us.

## 3a. Confirm the bridge writes land — **DONE, and it was the HS clock**

The blocker was not the transport: the **DSI HS clock was never started**.  The
reference starts it with a null packet on **VC0 in command mode with
`MODE_SPEED = 1`** (`L1DSS_DSI1_lp_mode_start_DSI_QNX`, report 128); mainline
only ever sends that packet on VC1 in LP.  With it, `IDREG` reads
`0x00006500` over DSI and the reference's GPIO36 test passes 1/0/1/0.  Closed by
report 131, image `0x49A770D3`.

## 3b. ~~Get a clean frame on the panel~~ — **ACHIEVED 2026-10-29**

The panel shows a clean, stable image (solid colours, kernel console as text) on
image `0x8C146805` (the same source as `0x068C867F`, rebuilt from the moved
workspace).  Three defects, each necessary and each found by diffing
against a working TabletOS rather than by reasoning about undecoded registers:

1. **bridge delivery** — the DSI HS clock was never started (report 131);
2. **LVDS porches + TCON table at bring-up** — `VP_HTIM1/2` had the porches in
   the reserved top byte, and the Hitachi table was only sent by hand
   (report 132);
3. **the DISPC core clock** — the DSS functional clock was 48 MHz against a
   50.4 MHz pixel clock, so the controller could not service the stream
   (report 133).

**Follow-up (not blocking):** the rate-mismatch WARN is fixed and the real
96 MHz is recorded (report 142); making the DSS clock a clock-initialisation
property rather than a probe-time `clk_set_rate` remains optional polish
(report 133 §6, trap 45).

## 3c. ~~Settle the Hitachi TCON byte order~~ — **settled 2026-10-29**

High byte first (the reference's, `winch_lcdctl 0x1c51c`).  With the bridge
correctly programmed **both** orders drive the panel; low-first swaps
magenta↔green and inverts black↔white.  Recorded in
`docs/contradictions/111-vs-123-tcon-byte-order.md` (report 132 §8).

## 3d. ~~Fix the horizontal timing~~ — **the real bug was ours, 2026-10-29**

The session-13 "half frame" was withdrawn long ago.  The genuine horizontal
faults were the zeroed LVDS porches in our own bridge constants (report 132 §5)
and the DISPC core clock below the pixel clock (report 133).  Both fixed.

---

## 4. Userspace brightness control — **CLOSED 2026-11-06**

**Measured (session 27):** `write /sys/class/backlight/backlight/brightness 64`
moves LM3532 register **`0x74` from `0xFF` to `0x40`** — read back with `bl rd
0x74` — and back to `0xFF` at 255, and the panel visibly follows (operator).
The attribute is there from the initramfs, so the acceptance criterion is met
with no code change.

The path is `led-backlight` (`/sys/class/backlight/backlight`) over the LM3532
LED, which is why `/sys/class/leds/lm3532::backlight/brightness` returns
**`EBUSY`**: the LED core sets `LED_SYSFS_DISABLE` when a backlight consumer
owns it.  Correct behaviour, not a fault — use the backlight attribute.

**Left open (policy, not mechanism):** the default level (255, the boot state)
and whether `/init` should set one.

## 5. A root filesystem — **CLOSED 2026-11-06 (session 28)**

**Closed.**  The initramfs now mounts the region-1 ext4 **read-write**,
bind-mounts /dev//proc//sys, chroots and execs BusyBox `/sbin/init`; the unit
boots to a `sh -l` shell on `/dev/ttyGS0` from the eMMC root, `insmod`/
`modprobe` load a module from `/lib/modules/6.15.11`, and the way back to the
sealed 1917 IFS was flashed and verified (`BOOTCTR_FCTRY = 0x32`).  The image is
`local/images/rootfs-region1.ext4` (sha256 `e1ec2bf3…`) with kernel image
`0xAEDF68D0`; tools `tools/build_rootfs.py` + `rootfs/`.  The `hd3` cut-over is
**parked by the operator** (session 28), so this item closes on the region-1
root; the `hd3` repurpose, when it resumes, is a separate deliberate step.

Userspace in our own image is an initramfs, so every driver must be `=y` and
nothing can be tried without a rebuild (report 118 §11).
**Session 18 — region 1 is done.**  A 32 MiB ext4 root was written to **region
1** at raw `0x29000000` through the pre-OS `0xF7` loop (2055 frames), read back
byte-exactly, mounted read-only through a loop, and its `/init` ran (reports
139/140; image `0xFB3A9F5D`, root `0b87c29f…`).  The loader container trap is
55.  The next steps are a **real userland** in that root and the **hd3
cut-over**: overwrite `hd3` (the MCT type-`0x1C` partition, raw `0x49000000`,
~58 GiB) with our own ext4 — **no MCT edit is needed**; the loop exposes the full
~58.76 GiB (trap 58) — gated on the Linux `BOOTCTR_FCTRY` top-up (item 6).
`DECISIONS.md` §18 records the writable-eMMC decision.

**Status after session 17 (reports 134/135/136/138):** eMMC **reads** are proven
(an 11 MB image read back byte-exact), and the **write path is healthy** — an
`O_DIRECT` write gives 256 KiB multi-block `CMD25`s at 5.5–7.0 MB/s; trap 49's
"single 512-byte CMD25, 17–32 KB/s" was the console's tiny buffered writes plus
per-request printk to fbcon (report 136).  The remaining transport problem is
that the **ACM console** drains at ~134 KB/s and drops bytes when blasted, so it
cannot supply a multi-MB image; bulk images must go through the pre-OS `0xF7`
loop or QNX over the door (~4 MB/s).  The hd3 **FS-level shrink is dead**: a
smaller filesystem in the 58 GB partition is reformatted and reseeded at boot
(report 138; `rootfs.sh`'s format+seed path, and the base seed is unprovisioned),
so region 1 is the no-MCT home for a Linux root.  The read-only-mount policy is
resolved (operator-approved).  The free-space facts are unchanged:

* **region 0 is tight, but the 11.25 MB figure is unverified** — the Linux
  images built in session 27 are 11 505 896 B and flash and boot, and the
  flasher's gate only checks the MCT type-`0x1B` extents.
  **A compression path now exists for a QNX IFS** (`tools/bb10_ifs.py splice` +
  reseal; a booting 3.47 MB 1917 IFS, session 27 / report 175) — it does **not**
  apply to our chainloader image, which is not a QNX imagefs and would need a
  decompressing entry path.  Session 28 restored the **uncompressed** 1917 IFS
  (`b52195bb…`, 7.9 MB) as the media asset; the compressed one (`f14db207…`,
  3.47 MB) is the session-27 alternative.  Reclaiming the ~11 MB region-0
  budget for the Linux image therefore needs a decompressing entry path, not
  just `splice`;
* **region 1** — raw `0x28000000`, 512 MB, already partitioned and empty,
  inside the loader's allowed OS range, so an `0xF7` write at loader block
  `0x2815` needs **no MCT edit**;
* **`hd3`** — 58 GB, 2.3 GB used; shrinking it frees 50+ GB, but an FS-only
  shrink is **rejected** (a smaller FS is reformatted and reseeded at boot,
  report 138), so it needs an MCT/partition change and a backup of the live
  TabletOS root, which the sealed IFS does not provide.

The root-location decision is settled by the evidence: **region 1** (no MCT
edit).  The mount decision is **resolved and extended**: the operator approved
read-only eMMC/QNX6 mounts (session 17), and writable eMMC for **our own** data
(session 18) — so a read-only loop root at region 1 is allowed, and so is a
writable mount of our own ext4; a writable mount of QNX's filesystem is only for
the deliberate `hd3` repurpose (`SAFETY.md` §4/§6, `DECISIONS.md` §17/§18,
`docs/contradictions/safety-no-emmc-mount-vs-readonly-root.md`).

**Closes when:** the kernel mounts a real root filesystem from the eMMC
(region 1 first, at a Python-derived offset) and boots to a shell with modules
loadable, with the recovery path back to the sealed IFS verified.

## 6. RPMB / `BOOTCTR_FCTRY` from Linux — **CLOSED 2026-11-06**

Every factory/CRC boot spends one unit of budget, and today only a **booted
TabletOS** puts it back (the operator's startup script writes `0x32` and verifies it
by readback each time the reference OS starts). Once Linux is the normal OS,
nothing repairs it — and a wedged image reset-looping every ~7.8 s spends units
fast (`KNOWN-ISSUES.md` trap 33).

**Status after session 27: CLOSED — the top-up was performed from Linux.**
The operator ran the prepared frame (`pbrpmb write 0x3a yes`) on image
`0x2238D00A`: the block was unsealed, the four bytes patched to `0x32`,
re-sealed by the monitor, the seal round-trip checked, one type-3 frame sent,
the result read, and the read-back unsealed and printed `32 00 00 00`.  A full
read-only scan after the write still showed 512/512 frames authenticating and
107/107 blocks unsealing, and the restore boot to 2.1.0.1917 left
`BOOTCTR_FCTRY` reading `0x32` — the bootloader read and decremented that very
record on the way in, so both readers accept it.  Report 175 addendum.

The research half:  `pbmon` runs
`0x33` → `0x2F` (**the cipher**, 224 in/out, `dir 1` seals) → `0x33` → `0x2E`
(**the digest**, 224 → 32, tag gate) with digest key `seed[0:16]`, cipher key
the `0x33` preload `page+0x20` (16 bytes), and **no `0x31(id 1)`** (that call
breaks it — trap 89).  The captured block-0x3B pair round-trips exactly in both
directions and live block 0x43 satisfies `seal(P) == block`.  `blockscan` reads
the store from Linux: 512/512 frames authenticate, 107 blocks carry a valid tag
and all 107 unseal, and `BOOTCTR_FCTRY` is found at block `0x3A` (len 4; the
value read `0x2B` after seven factory boots).  Report 175, session-27 note.
The write itself is the operator's (`SAFETY.md` §6) and was executed the same
day; the CRNG has a seeding path for it (the console's `entropy` command,
trap 87).

**Status after session 26: the address map is corrected, and the
chainloader-reuses-the-bootloader route is out.**  Reports 150/151's code
addresses are `0x9000` too high (the arm9000 section is loaded at `0x80000000`
from media `0x9000`); the bootloader wipes its RPMB store state before the OS
jump (`[0x80025E78, 0x800517C4)`); and calling its mount from the chainloader
reset-loops the unit.  The next experiment is to re-open the Linux `pbmon` seal
with the corrected evidence — **domain 5** and the exact order
`0x31 -> 0x33 -> 0x2F -> 0x33 -> 0x2E` (report 173).

**Status after session 20: BLOCKED, and one unit was lost.**  The caller is solved
(identity physical addresses below `0xB0000000`; report 147) and the whole seal ABI
is accepted from Linux, but the transform does not reproduce the device's pair
(148), and the first write-back — a *captured, genuine* block, accepted by the
device — left the spare in the `0x5B` LED loop with no USB (149/153).  **No agent
writes RPMB** (`SAFETY.md` §6).  The reference was armed by the operator in
session 21 and now runs the sealed IFS; the open problem is the `0x2F` transform,
not the arming (`BOOT-CHAIN.md`, reports 147–157).

**Status after session 18 (superseded): feasible, and the read path is proven.**  Report 141
shows the RPMB key is **not** secure-world-only — the monitor receives the key
bytes by pointer, and the key is `f(seed)` of an in-image literal (or the CID) —
and `/dev/mmcblk1rpmb` is a pure 512-byte frame relay with no kernel keying.
`pbrpmb` proved the Linux read path (GET_WRITE_COUNTER = 4179, authenticated
READ_DATA, zero writes).  The last unknown is `f`; the next step is one read-only
extraction from a live QNX page (seed at `page+0x440`, key at `page+0x460`) or
the recovered `smc #1` ABI, then one verified `0x32` write.  The operator has
chosen Linux-only independence (`DECISIONS.md` §19).

**Status after session 19: the key is pinned, and the store is sealed.**  `f` is
known (frame key `dfeec0ce…2274`, block key `seed[0:16]`; report 143), so the
authenticated read path is understood.  But the 224-byte record block is
**encrypted on media by the secure monitor** (`0x2F`, reports 144), and a
plaintext patch is invalid (`DECISIONS.md` §20).  The monitor call from Linux was
implemented (`drivers/misc/pbmon.c`): the first hang was the monitor's DRAM bias
(`phys - 0x10000000`), and with it the call returns, but **every service
(`0x12`, `0x31`, `0x33`) answers `0xFF05`** — a global refusal (report 145).
Static RE (report 146) ruled out a registration/handshake, the domain `r2`, the
`op=1` derivation and `devb … crypto=enable`; the leading suspect is the
**command-page physical address (`r3`)**.  The next experiment is the `0x12`
address oracle.  Until the seal is reproduced, QNX's `/dev/rpmb` remains the only
proven writer.  Item 5's hd3 cut-over is therefore blocked behind item 6.

**Closed when (met):** a Linux boot cycle left `BOOTCTR_FCTRY` reading `0x32`
afterwards, with the verified one-write/readback shape (exactly 4 bytes, value
`0x32`, read first, write only on a mismatch, verify by readback).  The one
addition the transform work forced: the patched plaintext is **re-sealed by the
monitor**, never patched in ciphertext.

**Read first:** `SAFETY.md` §2. The accepted range is `1..0x32`; `0` and `> 0x32`
are a hard lock ("expired factory mode") with physical recovery only, not a
fallback to the product path; `0x32` is both ceiling and target; the write is
exactly 4 bytes at offset 0; and nothing is ever tested on the working unit.

## 7. Packaging (postmarketOS-style)

The intended end state: a reproducible artefact that builds the kernel from the
patch + config, produces the chainloader image, and installs onto the device —
the shape postmarketOS uses (a device package + a kernel package + an installer).
Nothing about tooling, licence or distribution has been decided
(`DECISIONS.md` OPEN DECISIONS); this item records the target, not a plan.

**Closes when:** a clean checkout plus documented host prerequisites reproduces
the image byte-for-byte (same CRC) and installs it on a unit without manual
steps.

## 8. A BB10 image on the PlayBook — **PARKED (operator, session 25 end)**

**Goal: boot BB10 on the PlayBook.**  Target: **Z10 10.3.03.2049 STL100-1**
(OMAP4470), supplied as components; the known-running
`10.0.09.1103-ROOT-winchester.signed` set is also in hand.  Session 25 analysed
both **read-only** (`BB10-PORT.md` §4b).  What that settled:

* both BB10 IFS images are plain **QNX imagefs** and the QNX 6.6 `dumpifs`
  reads them; both are `os_type = 4` (**product/ECDSA**, not CRC-sealed), so a
  PlayBook experiment needs our reseal to `os_type = 6` + CRC;
* the BB10 `/base` is the **`r-c-f-s` RCFS** container, not QNX6;
  `tools/rcfs_extract.py` now reads both images fully (11,911 files on the Z10);
* **the port is a platform-support problem**: the 10.3 IFS has only
  `*London*`/MSM branches, while 10.0.9.1103 still has **`*Winchester*`**
  (OMAP4430/4460, `graphics/omap4430`, `devpm-omap4.so`, `displayHAL-winchester`)
  — that branch is how BB10 ran on the PlayBook;
* containers differ: 10.0.9.1103 uses the **PlayBook deltas** (16/192/16640/…),
  the Z10 set is **V2-only**, which this loader rejects (trap 55);
* `/base` is gated by `rfs_validator` and mounted through `fs-rcfs.so` — the
  PlayBook shim has a direct BB10 analogue to prove later.

**Route chosen (operator, session 25): backport into 10.3** (`DECISIONS.md`
§22).  The surface is small and dependency-clean — the Manifest is
`local/work/bb10/backport-manifest.txt`; the IFS set is ~21 files plus
`librocket.so.1`, and the omap4430 graphics stack (26 files, carried in the IFS
so `/base` stays stock) — and every binary resolves against the 10.3 IFS
libraries.  The plan:

1. graft the 10.0.9.1103 `*Winchester*` block into 10.3's `ifs_variables.sh`,
   adapted to the variable names 10.3's scripts consume and with paths pointing
   into `/proc/boot` (the IFS), not `/base`;
2. add the manifest files to the 10.3 IFS (`tools/ifs_build.py --add`) and keep
   `/base` stock by putting the omap4430 graphics stack in the IFS too, with
   `DCONF_GRAPHICS_ROOT` pointing at it (no RCFS writer needed);
3. store the IFS compressed (13.7 MB imagefs vs an 11.0 MiB slot).  **Solved
   in session 25** (reports 171/172, `BB10-PORT.md` §4b): `tools/ifs_build.py`
   emits the uncompressed imagefs, `tools/ucl_nrv2b.py` is the host NRV2B
   encoder (QNX's own `libucl` decodes its output), `tools/bb10_ifs.py splice`
   puts it into RIM's preamble/header/stub and patches the fields, and
   `89-crc_tool.py reseal` gives a `VERIFIED (CRC path)` image.  The unmodified
   10.3 tree round-trips through the whole chain (`dumpifs` lists the same 339
   entries).  Then reseal and pack V1+V2 with the PlayBook deltas
   (16/192/16640) — the Z10 container is V2-only and is rejected (trap 55);
4. craft the actual Winchester branch: add the manifest's winchester/colt
   binaries and configs plus the omap4430 graphics stack to the IFS (keeping
   `/base` stock), graft `ifs_variables.sh` with values that 10.3's scripts
   consume, and build/splice/verify/reseal the real image;
5. only with the operator's approval, flash and read the first boot.

Repacking an RCFS needs a writer that emits this format; the QNX 8.0.0
`mkrcfs` is still being located (the 6.6 one is not trusted), so RCFS edits
are the gated step.

The PlayBook image chain is ready (IFS/RCFS/UFS, container/flash, the
`rfs_validator` shim; reports 169/170, `IMAGE-REBUILD-GUIDE.md`).  The facts,
the open questions, the tool surface and the intake procedure are in
**`BB10-PORT.md`** (§3, §4a, §4b).

**Result (session 25):** the first BB10 10.3.03.2049 set was built, packed and
flashed.  **The loader verified it and jumped five times** (`factory_count`
50 → 45, read with `bbusb.py info`), but **BB10 did not run** — no splash, no
USB, and the unit re-entered the bootrom on each reset.  So the container / IFS
/ RCFS chain is sound and the gap is inside BB10 (kernel/board support for the
OMAP4430).  The unit is held in the ramloader; the way back
(`local/work/back-1917.signed`) is packed and not flashed.  The repeatable
procedure is **`BB10-IMAGE-GUIDE.md`**.

**Next:** restore the 1917 way back (tops `BOOTCTR_FCTRY` to `0x32`), then run
the **10.0.9.1103 winchester set**, resealed to `os_type 6` + CRC, as a
compatibility control.  If it also fails to run, diff its IFS/`proc/boot` and
`/base` against the 10.3 set for what the 10.3 branch backport did not cover.

**Closes when:** BB10 boots on the PlayBook, or the first hard blocker is
proven and recorded with its evidence.

---

## 9. The device's own sensors and peripherals — **next (operator, session 28)**

**Goal:** bring up the sensors and the rest of the board under Linux, before any
`hd3` repurpose.  Report **176** is the inventory and the bus map.

**Measured (read-only, on TabletOS):** the sensor cluster is on OMAP **I²C4
`0x48350000`** (QNX `/dev/i2c3`), which the board DTS currently **disables**:
BMA150 accelerometer `0x38`, HMC5883L magnetometer `0x1E`, MPU-3050 gyroscope
`0x68`, CM3217 ambient light `0x10`/`0x11`, FAN5702 LED driver `0x36`; the
WM8994 codec is also on that bus at `0x1a`.  The BQ27541 gauge `0x55` and TMP103
`0x77` are on I²C1 (with the LM3532 `0x38` and the TWL6030 `0x48`–`0x4a`).  The
TI **WL1283** combo (WiFi/BT/**GPS+AGPS**) is SDIO/UART on `mmc1`/UART3.

**Step 1 — DONE (session 28, report 177).**  `&i2c4` is enabled and the three
motion sensors work: BMA150 `3-0038` (`input0`, live evdev events), HMC5883L
`3-001e` (IIO raw values), MPU3050 `3-0068` (IIO, die ID `0E9E`).  Images:
region-0 `0x3E479F6F`, region-1 `6aa322bd…`.  The IRQ GPIOs are still not
described (polling/direct reads); that is the next variable if triggered
sampling is wanted.

**Step 2 — TMP103 + BQ27541 DONE, CM3217 open (report 178).**  The BQ27541
gauge (`bq27541-0`: 100 %, 4.093 V, 28.8 °C, 5.307 Ah, cycle 5) and the TMP103
(`hwmon0`, 37.0 °C) work; the TMP103 needs `hwmon.ko` (trap 94).  The CM3217
light sensor is **open**: 0x10/0x11 ACK but read all-zero and the mainline
`cm32181` accepts only IDs 0x18/0x81/0x82 — it needs the `ls-cm3217` RE
(trap 95).  Images: region-0 `0x1D9159E0`, region-1 `c689ca9e…`.

**Step 3 — DONE (report 179).**  A mainline `leds-fan5702` driver probes
`3-0036`; the red/green/blue channels work via `/sys/class/leds`.  EN =
GPIO1_13 (pad `0x1b0`, safe mode at boot) is driven by the driver's pinctrl +
`enable-gpios`.  Images: region-0 `0xB759C3A2`, region-1 `0a55a170…`.

**Plan (one variable per boot):**
1. ~~enable `&i2c4`, add the BMA150/HMC5883L/MPU3050 nodes and their IRQ GPIOs;
   enable `INPUT_BMA150` and HMC5843, keep `MPU3050=m`; read via IIO.~~
   **DONE** (session 28).
2. ~~add the CM3217, TMP103 and BQ27541 (I²C4/I²C1) and enable their drivers.~~
   **TMP103 + BQ27541 DONE; the CM3217 is still open (RE, trap 95).**
3. ~~the FAN5702 LED driver (no mainline driver; RE `led-fan5702.so`).~~
   **DONE** (report 179: `leds-fan5702`, EN = GPIO1_13).
4. the WL1283: enable `mmc1` SDIO, the power/32k GPIOs, `wl12xx` + firmware/NVS;
   then BT (`hci_ll`/TI ST) and GPS/AGPS.
5. audio (WM8994 MFD+codec + an OMAP4 machine driver) and the Cypress
   **CY8CTMA3** touch (QNX "Munin/Pegasus" firmware; no mainline Gen3 driver).

**Closes when:** each peripheral reports data under Linux (IIO/evdev/ALSA/input)
and survives a reboot; the touch and FAN5702 have a driver; WiFi/BT/GPS are
enumerated, with firmware and calibration sourced from the QNX blobs.

## Out of scope for now

SGX/GPU, IVA-HD, camera and the secure-world (TrustZone) path are all disabled
or untouched in the board DTS.  They are not blocked on the panel.  Audio,
touch, the sensors and WiFi/BT/GPS are **no longer out of scope**: they are
item 9 (report 176).

## Working rules that apply to every item

* One variable per boot; never mix a timing experiment with a structural change.
* `fb <rrggbb>` before judging the panel; `kmsg` before theorising.
* Budget boots against `BOOTCTR_FCTRY`; restore and verify the sealed IFS at the
  end of every session.
* **Everything that writes** — RPMB, NVRAM, the eMMC, the media region, the panel
  chain — is governed by `SAFETY.md`.  Short version: writable eMMC for **our
  own** data is allowed (session 18); NVRAM and the MCT are never written; RPMB
  is the **operator's** `0x32` top-up only (an agent never writes it); never
  `SYS_RST`, never `VAUX1`.
