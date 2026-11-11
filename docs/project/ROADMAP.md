# Roadmap

Open work only.  Closed and parked item history is archived in
[`docs/history/ROADMAP-CLOSED.md`](../history/ROADMAP-CLOSED.md).  Each open
item states the evidence that would close it.

Current focus: **item 9** (peripherals), with the WM8994 codec clock as the one
audio experiment.  Item 7 (packaging) is untouched; item 8 (BB10) is parked by
the operator.

## 7. Packaging (postmarketOS-style)

The intended end state: a reproducible artefact that builds the kernel from the
patch + config, produces the chainloader image, and installs onto the device —
the shape postmarketOS uses (a device package + a kernel package + an installer).
Nothing about tooling, licence or distribution has been decided
(`DECISIONS.md` §Open policy decisions); this item records the target, not a plan.

**Closes when:** a clean checkout plus documented host prerequisites reproduces
the image byte-for-byte (same CRC) and installs it on a unit without manual
steps.

## 8. A BB10 image on the PlayBook — **PARKED (operator, session 25)**

The full analysis and the built set are in
[`docs/parked/BB10-PORT.md`](../parked/BB10-PORT.md) and
[`docs/parked/BB10-IMAGE-GUIDE.md`](../parked/BB10-IMAGE-GUIDE.md); the
session-by-session record is in
[`docs/history/ROADMAP-CLOSED.md`](../history/ROADMAP-CLOSED.md).  Reopen only
with the operator.

## 9. The device's own sensors and peripherals — **active**

**Goal.**  Bring up the board's sensors and peripherals under Linux before any
`hd3` repurpose.  Report 176 is the inventory and the bus map.

**Done.**  I²C4 motion sensors (BMA150/HMC5883L/MPU3050), TMP103, BQ27541,
FAN5702 and CM3217; the Munin touch is calibrated and has its GPIO2_3 edge IRQ
(report 198); the ISL9519 charger and the 58.6 s WDT2 policy are in the
known-good image.

**Open.**

* **WM8994 audio.**  The codec is **done and driver-driven**: from a cold boot
  the machine driver thaws it, locks FLL1, enables AIF1CLK from that FLL, sets
  16-bit word length and enables the whole output chain (PM3 output mixers,
  charge pump `CP_ENA`, DAC→mixer un-mute, AIF1→DAC routing, output stage) — and
  the DAC audibly converts — a steady noise floor plus a brief descending
  sweep at every stream start, which a DAC fed silence cannot produce, so real
  samples do get clocked out.
  **The remaining blocker is a stall in the McBSP transmit path after
  start:** in steady state
  `DXR = 0x00000000` with `SPCR2 = 0x02F5` (`XRDY = 0` buffer full, `XEMPTY = 1`
  shifter empty) while sDMA interrupts fire at 12/s and `hw_ptr` runs at
  48 185 frames/s.  **No hardware probing is available**, so the routes are
  software-only: a capture stream (`/dev/dsp` read) to see what the McBSP
  actually transmitted, a `/dev/mem` `dx`→`dr` loopback, the reference's own
  McBSP registers read mid-stream on QNX (`memdump3`, the `0x49024000` window),
  and — most likely decisive — adopting the reference's clock roles (codec as
  AIF1 master, McBSP as consumer) on both sides at once.  `HANDOFF.md` §0c has
  the four as E1–E4.  Report 205 §5o/§5l.
* **WL1283.**  Formally stopped by the operator (session 36).  The software
  surface is exhausted and the 1917-truth minimal host still fails CMD5
  (report 196).  Do not re-run traps 116–137.  The WLAN calibration/NVS is
  extracted from QNX `hd5` (report 197).
* **Touch.**  Done; the remaining nicety is driving power/reset
  (GPIO1_14 / GPIO4_8) instead of relying on the post-boot levels.

## Out of scope for now

SGX/GPU, IVA-HD, camera and the secure-world (TrustZone) path are all disabled
or untouched in the board DTS.  They are not blocked on the panel.  Audio,
touch, the sensors and WiFi/BT/GPS are **no longer out of scope**: they are
item 9 (report 176).

## Working rules that apply to every item

* One variable per boot; never mix a timing experiment with a structural change.
* `fb <rrggbb>` before judging the panel; `kmsg` before theorising.
* Read `BOOTCTR_FCTRY` as a health check; the booted OS tops it up (Linux
  `/init`, TabletOS `startup.sh`).  Leave the unit on the image the operator
  wants; no restore is required.
* **Everything that writes** — RPMB, NVRAM, the eMMC, the media region, the panel
  chain — is governed by `SAFETY.md`.  Short version: writable eMMC for **our
  own** data is allowed (session 18); NVRAM and the MCT are never written; RPMB
  is the **operator's** `0x32` top-up only (an agent never writes it); never
  `SYS_RST`, never `VAUX1`.
* **Look up every external fact; do not trust recalled knowledge.**  The
  assistant's built-in knowledge is frozen at its training date and this project
  spans months of upstream movement: kernel versions, pmaports package contents,
  `arch=` lists and driver internals all drift.  On 2026-11-07 a session
  asserted that `7.3.3` "doesn't match the 6.x series" and was wrong —
  `kernel.org/releases.json` showed 7.3.3 as the current *stable*, with mainline
  at 7.4-rc1.  **Fetch the authoritative source** (`kernel.org/releases.json`,
  the actual pmaports `APKBUILD`, the tree you are about to build) and cite it,
  rather than reasoning from memory.  The same rule applies to register maps:
  prefer a live read or the local TRM over recollection.
