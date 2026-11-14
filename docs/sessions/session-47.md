# Session 47 (absolute) — the speaker path: line outputs, an active-low `spk`, and GPIO1_15

> Numbering: absolute `47` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 41** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-13/14 (it started in the evening and ran past midnight).
Unit state at the end: **on our Linux image `0xAD85DF0F`** (region 0,
`0x939D3C` B, kernel `7.3.3` **#44**), region 1 = rootfs **`0342ab06…`**
(67,108,864 B, 27 modules).  `0fca:8010`, console `pb> ` on `ttyACM0`, no
door/ssh (Linux has no door).  `BOOTCTR_FCTRY` read `0x20` at the first boot and
`0x2c` at the last (the image tops it up at boot; every reading is a health
check).  The oracle (1917 minimal IFS at `0x825`) was booted twice for the
speaker capture and the unit was returned to Linux.  No NVRAM/MCT/RPMB write.
The final listening test was **both speakers, 440 Hz, at DAC1 volume 96 and
40**.  Device-side changes: `/tmp/tone30.wav` on the oracle (wiped by the
reboot) and the oracle's **persistent** io-audio config
`/var/etc/system/config/audio/OMAP4 @ 1a.io-ado-cfg` on the UFS, where
`Headphone Select` was set from `true` to `false` — so a future oracle boot
starts routed to the speaker (the driver rewrites the file on its own exits).
No file was deployed to the Linux root.

## Goal of this session

The one experiment the session-46 handoff specified: **the speaker path**.
`pbplay`'s tone had only ever been heard on headphones; the codec's speaker
controls read routed, so the question was whether the speaker sounded with the
headphones unplugged, and if not, what gated it.

## Confirmed (measured, read in a file, or seen on the bench)

* **The reference's speaker route is the codec's line outputs.**  With the
  oracle playing through the speakers (headphones out, driver `Headphone
  Select` = false) the codec reads `PM1 = 0x0013`, `PM3 = 0x3cf0` (LINEOUT1P/N
  + LINEOUT2P/N), `R0x01e = 0`, `R0x034/0x035 = 1`, `R0x036 = 0`,
  `R0x038 = 0x0080`, `R0x039 = 0x01c8`, `R0x04c = 0x1f25`, `R0x060 = 0`.
  [capture `wm8994-speaker-2026-11-13.txt`; report 215 §1]
* **The driver's speaker route is `enable_codec_line_out`**
  (`deva-ctrl-omap4abe-wm8994.so` VA `0x2c814`): `powman_alive("audio_boost")`
  → `gpio_set_output(spk, 0)` → `R0x39 |= 0x1ce`, `R0x38 |= 0x80`,
  `PM3 |= 0x3cf0`, `R0x38 &= ~0x30`, `R0x39 &= ~0x06`, `R0x1e &= ~0x66`.
  Teardown mirrors it and raises `spk`.  `codec_gpio_init` leaves `spk` **high**
  (amp off).  [static RE, report 215 §2]
* **`spk` is active low.**  Our DTS held GPIO2_12 high permanently as
  `disp-en-12-hog`; the reference calls that pad `spk` (hwio signal 0x93) and
  drives it low for the whole speaker playback.  [oracle register/pin capture;
  report 215 §2]
* **GPIO1_15 is required as well.**  The reference's live GPIO1 output word is
  `0x0000a044` (bits 2/6/13/**15**) while the speakers play; the startup table
  leaves bit 15 low, so the running OS raises it (the `audio_boost` request).
  With `spk` low but GPIO1_15 low the speaker is still silent; with both right
  it sounds.  [bench A/B; report 215 §0]
* **The codec writes land and the output stage is now the reference's**: Linux
  reads the same `PM1/PM3/R0x01e/R0x034/R0x035/R0x036/R0x038/R0x039/R0x04c/
  R0x060` during a speaker stream.  [regmap debugfs; capture]
* **Both speakers play the 440 Hz tone** (operator), at DAC1 volume 96 (loud)
  and 40 (clearly audible, ~28 dB down).  [operator]
* **The jack detect works and is the reference's polarity**: `hp` high with
  headphones in, low with them out; the reference routes to the speaker when it
  reads low.  Our driver now follows it.  [sysfs/debugfs + oracle]
* **The teardown de-asserts the amp**: `spk` is low during a stream and back
  high afterwards.  [debugfs, silent clock-link run]
* **The patch is 54 files / 8,584 lines, sha256 `93a7a8ce…`**, 0 rejects and
  54/54 byte-identical by apply-and-compare.  [patch export]

## Inferred (reasoning from evidence, not directly measured)

* **GPIO1_15 is the `audio_boost` rail enable.**  The QNX driver has no
  register write for it; it asks powerman for `audio_boost`, whose device table
  carries `ldo_regen*`/`rocket_ldo*`.  GPIO1_15 is the only board line that
  changes the outcome on this unit, so the request most plausibly lands there.
  *Strong, but the net itself was not traced.*
* **`audio_boost` may be an external LDO enable** rather than a TWL6030
  regulator.  *Moderate*; needs a schematic.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The speaker is gated only by the `spk` line or the jack detect."**  With
  `spk` low and the jack out (verified in debugfs) the speaker was still
  silent.  The codec's *output stage* was also wrong (class-D instead of line
  out), and GPIO1_15 was missing.
* **"Match the reference's codec registers and it will play."**  It did not:
  every visible WM8994 register matched and the speaker stayed silent.
* **"The `disp-en-12-hog` is a display enable."**  It is the audio `spk` line;
  the panel is fine without the hog.
* **"A devmem poke can test the `spk` line."**  The gpiolib shadow re-asserts
  the bank's data register on any other GPIO2 operation, so the poke reverted
  mid-stream.  The line must be owned by the driver.
* **"`Output Device Class` selects the route."**  Its setter is a no-op stub
  (`codec_audio_output_class_set`, VA `0x28024`); `Headphone Select` is the
  real switch.
* **"The oracle's speaker playback proves the codec config."**  The minimal
  image's `audio_out` needs a jack event to route; the first captures were of
  an idle codec (`PM3 = 0`).  Only a capture taken with the stream verified
  running is meaningful.
* **A listening test must state what is playing** (trap 191): two runs were
  invalid because the clock link had ended or `/dev/dsp` was busy, and one data
  stream died at 36 KB (the McBSP stall) before any judgment.

## Unknown / open questions

* **The `audio_boost` net.**  GPIO1_15 works, but what it enables is not
  identified.  *Would settle it:* a schematic, or a PMIC/LDO diff.
* **Whether the amp needs `spk` low *and* boost, or boost alone.**  Both were
  asserted when the speaker first sounded; the intermediate test (`spk` low,
  boost low) was silent, so `spk` low + boost high is sufficient; the
  individual necessity of `spk` was not re-tested with boost high alone.
* **The `pcm:`/`live N:` diagnostics** are still in the drivers and the patch;
  retire them and re-verify (ROADMAP next (a)).
* **MCLK1** is still optional and unused by the working design.
* **WL1283 / `wl128x-nvs.bin`**: unchanged, operator-stopped.
* **Touch cold boot**: still needs the GPIO1_14/GPIO4_8 pulse.

## Superseded / uncertain

* **Session-46 handoff's "the likely gate is the spk line or the hp jack
  detect"** — right about the board, incomplete about the codec (line out, not
  class-D) and about `spk`'s polarity, and missing GPIO1_15.
* **STATE session 46's "SPKMIX/R0x36 routes the speaker"** — the reference's
  speaker path leaves R0x36 at 0; the class-D speaker mixer is not used.
* **Report 214 §5's speaker register list (R0x22-0x27)** — those are the
  class-D path; the speaker path is R0x01e/R0x034/R0x035/PM3 bits 12:10.

## Artefacts produced

* **Report 215** — `docs/reports/215-speaker-linout-2026-11-13.md`.
* **Capture** — `docs/captures/wm8994-speaker-2026-11-13.txt` (the oracle's and
  Linux's full register dumps during speaker playback).
* **Machine driver** — jack-dependent output stage, line-out route, `spk` and
  `boost` GPIOs (`sound/soc/ti/playbook-wm8994.c`).
* **DTS** — `disp-en-12-hog` removed; `spk-gpios`/`boost-gpios` on the `sound`
  node.
* **Images** — region 0 **`0xAD85DF0F`** (kernel #44), region 1
  **`0342ab06…`**.
* **Patch** — 54 files / 8,584 lines, sha256 `93a7a8ce…`, verified 0 rejects /
  54/54.
* **DECISIONS §60**, KNOWN-ISSUES traps 207–210, ROADMAP/STATE/HANDOFF updates.

## Operator direction at the close

* **Next session: (1) retire the `pcm:`/`live N:` diagnostics and re-verify;
  (2) continue the WL1283.**  The WL1283 is **reopened** — the session-36
  "software surface exhausted" verdict is withdrawn.
* **Method:** the same one that turned "needs hardware probes" into a driver
  fix — **static RE of the reference driver/BSP (in subagents) plus running the
  reference's own driver on the oracle with its own controls** (as `io-audio`
  was started and `Headphone Select` set for the speaker), then implement and
  verify on the bench.  Recorded as DECISIONS §61; the concrete steps are in
  HANDOFF §0b.
* The operator asked for **quiet listening tests** (the full-volume tone was
  loud late at night): lower DAC1 volume first.
