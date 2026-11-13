# Roadmap

Open work only.  Closed and parked item history is archived in
[`docs/history/ROADMAP-CLOSED.md`](../history/ROADMAP-CLOSED.md).  Each open
item states the evidence that would close it.

Current focus: **item 9** (peripherals), with the AIF1CLK source as the one
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

* **The minimal 1917 oracle is built and has been used** (session 43/44, reports
  207/208/209).  A debloated 2.1.0.1917 IFS+RCFS — services reduced to
  `filesystem pps random pty net powerman phyctrl usbmgr rimusb`, GUI/app/font/
  browser/DRM subtrees pruned (live `/base` 90.4 MiB) — boots in ~30 s with
  `qconn` on `169.254.0.1:9090` and root `sshd`; built by
  `tools/build_minimal_qnx.py`.  **It settled the MCLK1 model:** AUXCLK1 IS the
  codec's MCLK1 (gate off + codec reset + FLL power-up → no lock; restore →
  lock), and on Linux MCLK1 is measurably **absent** with identical
  AUXCLK1/pad/stream-time FLL file (codec freeze detector, validated on the
  reference).  **The power-domain/CM comparison is done and negative** (report
  209): every power *state* matches, the only difference is the
  `LASTPOWERSTATEENTERED` debug latch, and forcing Linux's CM policies on the
  working oracle leaves MCLK1 present (report 209).  **ABE path fixed:**
  `CM_ABE_PLL_REF_CLKSEL` is SYS_CLK with DPLL_ABE locked at 98.304 MHz.  **The
  reference's codec-GPIO init is implemented** (report 211): a boot-on
  `audio-ldo` on GPIO4_31 pulsed 100 ms at probe, spk high via the hog, hp
  input.  It does **not** restore MCLK1 (`FLL1 NOT locked`) but it fixes the
  WM8994 probe (-121 without it).
* **WM8994 audio — the transport works, the codec does not decode (session 45,
  report 212).**  E5 put the McBSP back as the bit/frame provider with the codec
  as AIF1 slave and FLL1 referenced to our BCLK: **no MCLK1 in the path**.  The
  session-41 stall was `CLKSM = 0` (SRG clocked from the undriven CLKS pad);
  `CLKSM = 1` plus `CLKGDV = 32` runs the port at **48 096 frames/s**, the
  codec's FLL1 locks, `AIF1CLK` exists, and the application's tone is verified
  in the DMA ring and at the transmitter (`XBUFFSTAT`).  **Open:** the codec
  still outputs silence.  **The cause is the data ball (E6, report 212 §10):**
  on the live reference the codec's DACDAT is `abe_mcbsp1_dx` (pad `0x102`,
  mux 0, output — a static configuration), while `abe_mcbsp2_dx`, which E5
  drives, is mux 7 safe mode and not connected to the codec.  DSP_A was tried
  and is not the fault.  McBSP1 as a *clock master* loses the codec clock, so
  the board splits the engines: **mcbsp2 = clocks, mcbsp1 = data**.  Next: make
  McBSP1 a clock *consumer* on the same nets and carry the samples on
  `mcbsp1_dx`.  E9 built that (two links, `wm8994-aif2` on the data link) and
  the data port **stalls**: McBSP1 as a consumer never gets a frame sync
  (`SPCR2 = 0x0235`), so its clock pins are **not** on McBSP2's nets (E7's
  positive result is withdrawn, trap 197).  The board splits clocks (mcbsp2)
  from data (mcbsp1), and the reference clocks McBSP1 through the **ABE**
  through the ABE — confirmed by decompiling that driver (report 212 §16: a
  clock *consumer*, `SRGR2 = FPER | 0x1000`, slave mode starts no SRG).  Mainline
  has no ABE serial-port clocking (`omap-abe-twl6040.c` is only a machine
  driver), so the remaining work is an **ABE bring-up project**: establish
  whether the OMAP4 ABE's serial-port clock can be programmed without the DSP
  firmware, and if so route the codec's BCLK/LRCLK to McBSP1.  Raw register
  pokes to McBSP2 are forbidden (trap 193); the MCLK1 route stays closed by
  trap 165 (no probe).  **Closes when:** a 440 Hz tone from `pbplay` is audible
  at 48 kHz.  The free-running NCO remains a mechanism
  probe only.  **Closes when:** a 440 Hz tone from `pbplay` is audible at the
  measured rate (48 kHz ±1 %).
* **WL1283.**  Formally stopped by the operator (session 36).  The software
  surface is exhausted and the 1917-truth minimal host still fails CMD5
  (report 196).  Do not re-run traps 116–137.  The WLAN calibration/NVS is
  extracted from QNX `hd5` (report 197).  **Audit note (session 44):** trap 185
  shows Linux gpio numbers are by probe order; any WL1283 experiment that used
  sysfs/global gpio numbers (not DT/OF phandles) should be re-checked before
  the software-exhausted claim is treated as final.
* **Touch.**  Done; the remaining nicety is driving power/reset
  (GPIO1_14 / GPIO4_8) for cold boots — a warm reset keeps the controller up
  (session 42).

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
