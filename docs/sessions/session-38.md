# Session 38 (absolute) — touch finished; WM8994 machine link up

> Numbering: absolute `38` (the project's chronological scheme — see
> `NUMBERING.md`).  The reports call this Linux-era **session 32** (`N − 6`).

Date / span: 2026-11-09.
Unit state at the end: **on our Linux image `0x3B2ECFED`** in region 0
(`0x939780` B, kernel `7.3.3`; touch + ABE + audio machine link), region 1 = the
audio rootfs (`07d4e28b…`, 24 modules,
`local/images/rootfs-region1-audio.ext4`).  `BOOTCTR_FCTRY = 0x2d` at the final
boot (`pbrpmb topup` saw `0x2d >= threshold 0x1e, no write`).  The unit is left
**on our image** (the operator's choice); the 1917 IFS
(`local/work/ifs-shim-sealed.ifs`, block `0x825`) is the way back to TabletOS.
All device access was read-only except the region-0/region-1 test flashes and
the touch's own app-boot writes.

## Goal of this session

The operator chose **touch polish** from the four open directions: the axis
calibration/orientation (vendor 1280×768 → 1024×600 panel) and the IRQ pad that
reports 193/194 left inferred.  "Done" = the report maps to panel coordinates
and the interrupt works on a plain boot.

Once the touch was verified, the operator chose to **keep the Linux image and
start the WM8994 audio half** (item 9 step 5's other half): confirm the codec,
enable the ABE/McBSP path, and get the card registered.  Playback was not part
of the session's stopping point.

## Confirmed (measured, read in a file, or seen on the bench)

### Touch calibration

* **The 1917 vendor calibration is in `etc/system/config/scaling.conf`.**  The
  first valid `1024x600` entry is `mode=calib`:
  `disp_x=[102:922:922:102]`, `disp_y=[60:60:540:540]`,
  `mtouch_x=[172:1108:1108:172]`, `mtouch_y=[110:110:658:658]`.  The four
  pairs are axis-aligned corner correspondences: raw `x 172→1108` → display
  `102→922`; raw `y 110→658` → display `60→540`.  No swap, no inversion.
  [`local/work/1917/base/etc/system/config/scaling.conf`]
* **`playbook-munin.c` maps through it.**  The endpoints come from
  `rim,x-calibration` / `rim,y-calibration`, the panel from
  `touchscreen-size-x/y = <1024 600>`; `munin_map()` uses `div_s64()` and clamps
  to `0..1023` / `0..599`.  The same values are the driver defaults.
  [`local/kernel-tree/drivers/input/touchscreen/playbook-munin.c`]
* **Measured orientation:** a left-edge drag read `X = 0..7` while `Y` swept
  `43 → 29`; a top-edge drag read `X 86 → 957` monotonically while `Y = 0..27`.
  X increases left-to-right and Y = 0 is the top; all values are inside the
  panel.  [event0 capture over the `pb> ` console, this session]

### Touch IRQ

* **`tp_intr=1035` = gpio 35 = GPIO2_3.**  The 1917 `gpio_config` record for
  gpio 35 is `dir=0` (input), `edge=2` (falling), `pull=1`, signal `0x16` —
  the same class as `WL_IRQ` (gpio 53).
  [`local/work/1917/base/lib/dll/hwio-config-winchester.so`, record 6]
* **The physical pad is CORE `0x056` (`gpmc_ad11`).**  The board `pad_config`
  record for signal `0x16` is pad id `0x23`; mainline OMAP4 names anchor the
  range (`0x054` = `gpmc_ad10` = gpio_34, `0x058` = `gpmc_ad12` = gpio_36 in
  `omap4-sdp.dts` / `omap4-samsung-espresso-common.dtsi`).
* **The DTS adds `touch_irq_pins` (`OMAP4_IOPAD(0x056, 0x410b)`),
  `interrupt-parent = <&gpio2>`, `interrupts = <3 IRQ_TYPE_EDGE_FALLING>` and
  the calibration properties.**  [`kernel/source/omap4-winchester.dts`]
* **On the bench:** `devmem 0x4A100056 16` = `0x410B`;
  `FALLINGDETECT (0x4805514C)` = `0x8`, `RISINGDETECT` and both LEVELDETECT
  registers = `0`; the driver logs `Munin touchscreen ready (irq 110)` at
  3.453 s; `/proc/interrupts` shows `110: … 48055000.gpio 3 Edge 1-0065`.
* **The ~330–380/s rate with no finger is the controller's active report
  rate, not noise.**  Unbinding the driver froze the count at `65169` for 3 s;
  rebinding resumed it.  The vendor library also attaches an ISR
  (`tp_intr_attach`).  The 20 ms poll remains the fallback.

### WM8994 codec

* **The codec answers at I²C4 `0x1a`.**  `i2ctransfer -y 3 w2@0x1a 0x00 0x00
  r2` → `0x89 0x94` (`0x8994`), 16-bit registers, on Linux `i2c-3`.
* **There is no mainline OMAP4+WM8994 machine driver**, and `SND_SOC_WM8994` is
  a **hidden Kconfig symbol** (no prompt): `scripts/config -e` writes it but
  `olddefconfig` drops it.  A machine driver must `select` it.
  `CONFIG_SND` is capped at `m` unless `CONFIG_SOUND=y`.
  [`sound/soc/codecs/Kconfig`, `sound/soc/ti/Kconfig`]
* **The 1917 stack is ABE + McBSP.**  `/base/scripts/startup.sh` runs
  `io-audio -d omap4abe-wm8994 aif=0,secure_capture -o enable_buffering`; the
  driver is `deva-ctrl-omap4abe-wm8994.so` (`libabe` + McBSP + WM8994, default
  protocol i2s, `aif=0` = codec AIF1).
  [`local/work/1917/base/scripts/startup.sh` line 626]

### ABE segment

* **McBSP1/2/3 are inside `l4_abe`**, and the board disabled the segment
  because the **WD_TIMER3 target (0x40130000) hangs ti-sysc** after its revision
  read.  The WD target had no label.  [board DTS comment; `omap4-l4-abe.dtsi`]
* **Labelling `wdt3_target` and keeping it disabled lets the segment probe.**
  Image `0x50842767` booted: `4012208c/4012408c/4012608c` (McBSP1/2/3) probe
  `pm ok`, `40130000` is absent from the probe list, uptime 37 s,
  `BOOTCTR_FCTRY = 0x30`, no hang.  [boot log, this session]

### The machine link

* **The board DTS adds `wm8994@1a` on `&i2c4` (seven supplies),
  `mcbsp2_pins` (epson pads `0x0f6`–`0x0fc`), `&mcbsp2` and a
  `simple-audio-card` with McBSP2 as bit/frame master.**
  [`kernel/source/omap4-winchester.dts`]
* **The sound stack is modules.**  A new `SND_SOC_PLAYBOOK_WM8994` board
  Kconfig selects `SND_SOC_WM8994`, `SND_SOC_OMAP_MCBSP` and
  `SND_SIMPLE_CARD`; the region-1 `rcS` loads `wm8994 snd-soc-wm-hubs
  snd-soc-wm8994 snd-soc-ti-sdma snd-soc-omap-mcbsp
  snd-soc-simple-card-utils snd-soc-simple-card` after the initramfs gadget.
  [`sound/soc/ti/Kconfig`; `rootfs/etc/init.d/rcS`]
* **The card is up** on image `0x3B2ECFED` + rootfs `07d4e28b…`:
  `/proc/asound/cards` = `0 [WM8994]: simple-card - PlayBook WM8994`;
  `/proc/asound/pcm` = `40124000.mcbsp-wm8994-aif1 … playback 1 : capture 1`.
* **USB enumerates by itself (~22 s).**  The gadget is configured at 5.33 s;
  the audio modules load at 6.8–7.0 s.  `dmesg | grep -ci "unknown symbol"` = 0.
* **The first rootfs (`d448d418…`) was missing
  `snd-soc-simple-card-utils`**, which produced a page of
  `Unknown symbol simple_util_*` errors and no card; adding it to
  `07d4e28b…` fixed that.  [boot log, this session]
* **The remaining audio messages are benign:** the kernel's
  `ALSA device list: No soundcards found.` at 4.5 s is the pre-module
  enumeration, and four `wm8994-codec: Failed to request Mic1/Mic2 detect IRQ:
  -22` are the unwired codec jack-detect IRQs.  The card still binds.

### Build / export

* Images built this session: `0x82FC0D35` (touch only, `0x91A9C4`),
  `0x50842767` (ABE, `0x91A9D8`), `0x95C11147` (built-in audio, `0x941E88`;
  superseded), `0x3B2ECFED` (final, `0x939780`).
* Base patch exports: `3b44da6c…` (46 files), `dafede46…` (47), `9089ef49…`
  (48 files / 7 530 lines); the final export verified **0 rejects / 48/48
  byte-identical** against pristine 7.3.3.
* Rootfs build: `d448d418…` then `07d4e28b…` (24 modules).

## Inferred (reasoning from evidence, not directly measured)

* **The calibration offset is the vendor's own.**  The four `disp_*` values are
  inset calibration targets (102,60)…(922,540), not panel edges; stretching the
  raw range across the panel would shift every touch by tens of pixels.
  *Strength: strong — it is the vendor file's first valid entry.*
* **The touch IRQ rate is the chip's scan/report rate.**  The driver's read
  releases the line and the next report pulls it down again; the
  freeze-on-unbind / resume-on-rebind isolates it to the driver↔chip exchange.
  *Strength: strong (measured).*
* **The pad offset is right.**  The hwio signal chain plus the mainline pad
  anchors agree; the measured readback is exactly the requested value.
  *Strength: strong.*
* **The codec's `Mic1/Mic2` IRQ failures are harmless.**  The card binds and
  the PCM appears; the failures are only the jack-detect requests on a codec
  with no IRQ described.  *Strength: strong (the card works).*
* **McBSP2 is the codec's I2S port.**  The 1917 driver defaults to some McBSP
  and `aif=0` selects AIF1; McBSP2 is the OMAP4 ABE audio port the epson board
  uses, and the card binds with it.  *Strength: moderate — the QNX default was
  not resolved; see Unknown.*

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`__aeabi_ldivmod` means the math is impossible."**  It is only the ARM
  compiler runtime for `s64 / s64`; the kernel provides `div_s64()`.  The fix
  was `<linux/math64.h>` + `div_s64()`.
* **"SSH root is disabled on 1917; use devuser."**  **Wrong.**  Root works
  (`uid:0 euid:0`).  The early `Permission denied (publickey,password)` was the
  door-hold not yet having uploaded the key; `tools/pbssh.sh`'s comment claiming
  a devuser-only image was wrong and is corrected.  Start `door-hold.sh` with
  `setsid`, wait, then retry.  [operator correction; the verbose SSH trace]
* **"The 330/s interrupt rate is an electrical storm / wrong pad."**  The
  unbind test disproves it: the line is quiet when the driver is not reading
  the chip, and the pad readback `0x410B` is exact.
* **"The board `pad_config` word0 is the physical offset."**  It is a pad id;
  `0x23` is not a CORE offset.  The signal chain plus mainline names resolve
  it.
* **"Pad `0x056` in the WKUP domain is the touch IRQ."**  Wrong: WKUP `0x056`
  is `fref_clk3_req/gpio_wk30`.  The touch is on `&omap4_pmx_core`; the
  mainline anchors used are CORE.
* **"Build the sound stack into the kernel."**  The built-in card made
  `asoc-simple-card` take 3.35→4.63 s; the host then configured the gadget only
  after a physical replug.  As modules loaded after the gadget it enumerates by
  itself (~22 s).  [image `0x95C11147` vs `0x3B2ECFED`; trap 149]
* **"The first audio rootfs is enough."**  It lacked
  `snd-soc-simple-card-utils`; `modprobe` then failed with a page of
  `Unknown symbol simple_util_*` errors and no card.  Adding the helper to
  `07d4e28b…` gave `dmesg | grep -ci "unknown symbol"` = 0.
* **"`ALSA device list: No soundcards found.` means the card failed."**  It is
  the built-in ALSA enumeration at 4.5 s, before the module card registers at
  7.0 s.  `/proc/asound/cards` is the live check.
* **"The codec's `Mic detect IRQ: -22` lines are fatal."**  They are the
  unwired jack-detect IRQs; the card binds and the PCM appears.

## Unknown / open questions

* **Does playback work?**  The card and PCM exist but no stream has been
  started.  *Would settle it:* a playback tool (a static ALSA/OSS player or a
  tiny ioctl program) and one WAV/tone write; the `hw_params` error, if any,
  names the clock/format problem.  The codec has no MCLK described yet.
* **What MCLK/FLL does the WM8994 need, and where does it come from?**  The
  1917 driver calls `StereoAudioCodecRequestMcbspClk`/`RequestABEClk`; our
  simple-card lets the CPU (McBSP2) drive BCLK/LRCLK and the codec is a
  consumer.  *Would settle it:* a playback attempt plus the codec's `MCLK1`
  source (the ABE clock or the 26 MHz TCXO) and the FLL configuration.
* **What is the QNX driver's default McBSP?**  The option help says
  `mcbsp=[0..4]` (0 = disabled) but the startup command does not set it; the
  disassembly was not finished.  McBSP2 is the working candidate.  *Would
  settle it:* decompile `omap4_parse_commandline` (0xd22c) and read the
  default, or the ABE port map.
* **Where is the codec's IRQ / jack-detect line?**  `wm8994-codec` logs the
  four `Mic detect IRQ: -22` requests because the node has no `interrupts`.
  *Would settle it:* the board schematic or the 1917 `wm8994_accessory_detect_intr`
  GPIO, then an `interrupts` property (and the jack widgets).
* **Which rail is SPKVDD?**  The supplies are provisionally `v1v8`/`v2v1`;
  SPKVDD needs 2.5–5.5 V, so the speaker rail is almost certainly not v2v1.
  *Would settle it:* a schematic/board view or a live rail measurement.
* **The precise four-corner residual** on this panel.  The vendor calibration
  is authoritative, but the bench check was a drag, not four held targets.
  *Would settle it:* a printed overlay and four held targets.
* **Report-rate coalescing.**  `input_sync()` runs on every controller frame
  (~330/s) even with no contact.  *Would settle it:* a userspace consumer or a
  change-only guard in the driver.

## Superseded / uncertain

* **"The WM8994 audio has not started."**  Superseded: the codec is confirmed
  and the machine link is up (report 199); playback is the remaining step.
* **"The sound stack can be built into the kernel."**  Superseded: it must be
  modules loaded after the USB gadget (trap 149).
* **"region 0 = `0x82FC0D35` / `0x50842767`."**  Superseded by the final image
  `0x3B2ECFED` + rootfs `07d4e28b…`.
* **"Root is disabled on 2.1.0.1917."**  Superseded: root works after the
  door-hold uploads the key.
* **"The touch IRQ (`tp_intr=1035`) is inferred."**  Superseded: pad `0x056`,
  the trigger and the live IRQ are measured.

## Artefacts produced

* `local/kernel-tree/drivers/input/touchscreen/playbook-munin.c` (and the
  identical `kernel/source/playbook-munin.c`): calibration + IRQ.
* `local/kernel-tree/arch/arm/boot/dts/ti/omap/omap4-winchester.dts` (and
  `kernel/source/omap4-winchester.dts`): `touch_irq_pins`, `interrupts`,
  calibration/size properties, `wm8994@1a`, `mcbsp2_pins`, `&mcbsp2` and the
  `simple-audio-card`.
* `local/kernel-tree/arch/arm/boot/dts/ti/omap/omap4-l4-abe.dtsi`: the
  `wdt3_target` label.
* `local/kernel-tree/sound/soc/ti/Kconfig`: the `SND_SOC_PLAYBOOK_WM8994`
  select.
* `rootfs/etc/init.d/rcS`: the audio module load after the gadget.
* `kernel/winchester-7.3.3.config` and
  `kernel/patches/0001-playbook-winchester-7.3.3.patch` — 48 files / 7 530
  lines / sha256 `9089ef49…`, 0 rejects / 48/48 identical.
* `boot/chainloader/kernel.img` — image **`0x3B2ECFED`** (region 0; touch +
  ABE + audio machine link); `local/images/rootfs-region1-audio.ext4`
  (region 1, sha256 `07d4e28b…`, 24 modules).
* `local/work/modules-7.3.3/` — the sensor set plus `wm8994`, `snd-soc-wm-hubs`,
  `snd-soc-wm8994`, `snd-soc-ti-sdma`, `snd-soc-omap-mcbsp`,
  `snd-soc-simple-card-utils`, `snd-soc-simple-card`.
* Report **198** (touch calibration and IRQ), report **199** (WM8994 audio
  reconnaissance + machine link).
* Docs: `STATE.md`, `HANDOFF.md`, `ROADMAP.md`, `KNOWN-ISSUES.md` (traps
  144–149), `DECISIONS.md` §47/§48, `SAFETY.md` budget wording,
  `tools/pbssh.sh` comment.
