# Current state

## Current state — 2026-11-13/14 (session 47)

**Audio is complete: the 440 Hz tone plays in both ears on headphones and in
both speakers, at 48 kHz ±1 % (operator), image `0xAD85DF0F` (kernel #44) +
rootfs `0342ab06…`.**  The speaker (the session-46 handoff's one open
experiment) needed three corrections, none of them in the data path
(report 215):

1. **The speakers hang off the codec's line outputs, not its class-D
   amplifier.**  The reference plays with `PM1 = 0x0013`, `PM3 = 0x3cf0`
   (LINEOUT1P/N + LINEOUT2P/N), `R0x01e = 0`, `R0x034/0x035 = 1`,
   `R0x036 = 0`, `R0x038 = 0x0080`, `R0x04c = 0x1f25`, `R0x060 = 0`: DAC1 →
   MIXOUT → LINEOUT, never SPKMIX/SPKOUT.
2. **`spk` (GPIO2_12) is an active-low amplifier enable.**  The reference
   leaves it high (amp off) and drives it low for the whole speaker playback.
   Our DTS had it as a permanent `disp-en-12-hog`; the machine driver now owns
   it (`spk-gpios`) and raises it again on `trigger(STOP)`.
3. **GPIO1_15 must be high.**  It is the reference's runtime-only board output
   and the amplifier rail enable the QNX driver requests from `powerman` as
   `audio_boost`; with `spk` low alone the speaker is silent.  The machine
   driver drives it high (`boost-gpios`).

**The data path is unchanged (session 46, reports 213/214).**  The card is two
ASoC links (`mcbsp2 + wm8994-aif1` clocks, `mcbsp3 + wm8994-aif2` data); the
WM8994's AIF1 is on **McBSP3** (the oracle's routing log
`AIF 0 Downlink: MM_DL_PORT->MM_EXT_OUT_PORT->MCBSP3`; `PCR0 = 0x3`; the PDM
pads in mode 1), the codec is the AIF1 slave and FLL1 locks from our BCLK
(`R0x302 = 0x0000`, `R0x732 = 0x6020`, `R0x200 = 0x0011`), so **no MCLK1 is in
the path**, and the `omap-mcbsp` channel-layout guard keeps `XCR2 = 0x8041`
(both slots).  The ABE is not needed (DECISIONS §58/§59).  The output stage now
follows the jack like the reference: headphones → HPOUT1 + charge pump;
jack out → line outputs + `spk` low + boost high.

**Bench (session 47, measured).**  Unit on our Linux image `0xAD85DF0F`
(region 0, `0x939D3C` B, kernel `7.3.3` **#44**); region 1 = rootfs
`0342ab06…` (67,108,864 B, 27 modules).  `0fca:8010`, console `pb> ` on
`ttyACM0`; no door/ssh (oracle only).  `BOOTCTR_FCTRY` read **0x20** at the
first boot and **0x2c** at the last (the `/init` top-up keeps it inside
`1..0x32`; read it as a health check).  The oracle is not on media; flash
`local/work/ifs-shim-sealed.ifs` at `0x825` to boot it.  To stock TabletOS,
flash `local/work/minimal/wayback.signed`.

**The magic-`pkexec` rule still holds.**  A flash re-enumerates the ACM gadget
and the tty comes back `root:uucp 0660`; `tools/99-playbook-cdc-acm.rules`
makes it `0666` automatically, and `pkexec chmod 666 /dev/ttyACM0` remains the
documented manual path — wait for the operator rather than inventing a
workaround.

* **The kernel patch was re-exported**: 54 files / **8 584 lines**, sha256
  `93a7a8ce…`, 0 rejects / 54/54 byte-identical (session-47 export; it carries
  the speaker route, the `spk`/`boost` GPIOs and the `disp-en-12-hog` removal).
* **Still instrumented:** the machine driver's `pcm:` ring readback and the
  McBSP driver's `live N:` dump are in the tree and the patch; retiring them and
  re-verifying the audio is the next audio item (ROADMAP item 9).
* **`DXR`/`DRR` are not readable data registers on this silicon** — they return
  constants (`0` / `0xc8318118`) regardless of traffic.  Report 205 §5l's
  "`DXR = 0` means empty" is withdrawn; `XBUFFSTAT` is the register that
  answers the question.
* **No boot regressions:** codec revision D, McBSP2/McBSP3, the two-link sound
  card, the touch, the display, the ACM gadget and the charger all come up on
  the final image.

## Per-subsystem state

| subsystem | state | detail / evidence |
|---|---|---|
| Chainloader image (CRC path) | **working** | 828 B loader at image `+0x800` = `0x80100000`; copies zImage/DTB high, checks the DTB magic `0xEDFE0DD0`, jumps (`r0=0`, `r1=~0`, `r2=DTB`). Report 109 §8.1, 103 §5.2 |
| DRAM / memory map | **working** | 1 GB at `0x80000000`; kernel runs from `0x84000000`-loaded zImage, decompressed to `0x80008000`; DTS comment |
| Kernel self-capture | **working** | DRAM ring at PA `0x88000000` (32 KB) + breadcrumb/event log; `tools/pb_ring.py`, `tools/pb_events.py`. Bootstrap §3 |
| WDT2 | **working — 58.6 s window, 10 s pet (session 31)** | The loader arms `WLDR = 0xFFFC1800` (7.8125 s); the kernel reprograms it to the reference's `0xFFE2B400` (58.6 s) at the late initcall via `WSPR` (`0xAAAA`→`0x5555` stop, write `WLDR`, `0xBBBB`→`0x4444` start), **polling `WWPS`** after each write, and pets every 10 s (`TGR` read→invert→write); fallback 2 s if the readback fails.  Verified on `0xE36C679A` by `WCRR` reload timing (~10 s) and uptime >275 s.  The expected dmesg line is missing on 7.3.3 (printk anomaly).  Report 109 §8.1 + correction; trap 115; decision §38 |
| eMMC (MMC2) | **reads proven; writes healthy — but writers must use large blocks** | `mmcblk1` = MCGAFA 59.4 GiB, `boot0`/`boot1`/`rpmb`; needs the `vemmc` fixed supply for `ocr_avail` (report 109 §9).  Session 16 read a freshly flashed 11 152 064-byte image back **byte-exactly** and matched a pre-flash TabletOS capture (report 134); the MMC2 functional clock is 96 MHz.  Session 17 measured the write path (report 136): the driver has no request-size limit — an O_DIRECT write gives **256 KiB `CMD25`s, 5.5–7.0 MB/s** (4 KiB chunks: 0.93 MB/s), one `CMD13` per request (normal `mmc_blk_card_busy`).  The old 17–32 KB/s was `recv`'s tiny buffered writes plus 4–7 unthrottled `printk`s per request to **fbcon**.  Use ≥64 KiB (ideally 256 KiB–1 MiB) writes with the MMC trace off.  **Session 18: region 1 now holds a bootable Linux root** (32 MiB ext4 at raw `0x29000000`), written through the pre-OS `0xF7` loop (2055 frames) and read back byte-exactly (QNX `dd` and Linux `blk sumb`); it mounts read-only via a loop and its `/init` runs (report 140). |
| USB device + gadget | **working, but not a bulk transport** | enumerates `0fca:8010`, CDC-ACM, product string "PlayBook (Linux bring-up)"; `/dev/ttyACM0` ↔ `/dev/ttyGS0`; High Speed (480 Mbps), 512-byte bulk packets.  **Drains at only ~134 KB/s and drops bytes when the host blasts** (report 136 §3.3), so it is a control/debug channel: do not send multi-MB images through `recv`.  Report 114, 136 |
| **WL1283 (MMC5)** | **REOPENED (operator, session 47 close); the "software surface exhausted" verdict is withdrawn** | **Session 47:** the speaker was solved by RE of the reference driver plus running it on the oracle with its own controls, so the WL1283 gets the same treatment (HANDOFF §0b, DECISIONS §61).  **Session 37:** the WL1283 code is archived (`kernel/archive/wl1283/`), the DTS is re-derived from 2.1.0.1917, and the minimal 1917-truth `&mmc5` (image `0x0DC36A11`) probes but returns `attach_sdio no` (report 196); pads `0x0118`, WL_EN `0x0003`, WL_IRQ `0x410B` match QNX and WL_EN is high, but `GPIO2_DATAIN` bit 21 = 0.  Bus, pads, WL_EN/IRQ, PMIC rails, MMC5 CON/HCTL/SYSCTL and the clock manager all match the reference.  QNX enumerates at 24 MHz with CMD5 arg `0x00FFFF00`; Linux's default 400 kHz/CMD52-first also times out.  **Session 35:** `startup-playbook` (never run by our chainloader) is the source of the QNX-only GPIOs — Winchester table at image `0x80122750`; GPIO1 0/4/15/18/28/31 and GPIO2 5/7 are output-low, gpio54 (WL_EN) output-low, gpio53 input/falling (report 192 §4, trap 135).  All eight released to input on the live 1917 are **negative** (trap 136).  `CLK32KG` is base `0x8C` and APP-ON in both (CFG_TRANS identical; CFG_GRP and the CFG_STATE CON/MOD bits differ; trap 135).  **H1** (CMD0 → CMD5 only, no CMD52/CMD8, 24 MHz, arg `0x00FFFF00`, image `0x29784F48`) still times out (`err=-110`, no R4; `HCTL=0x00000b00`, `CAPA=0x04e10080`; trap 137).  The remaining difference is the chip's non-exposed VBAT/TCXO/reset pins or OMAP HSMMC sampling — needs a schematic/board view or logic analyser |
| **Munin touch (CY8CTMA3)** | **working — calibrated, IRQ wired (session 38)** | I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` boots the app and maps the 12-bit report through the 1917 `scaling.conf` calib (raw x 172→1108 → 102→922, y 110→658 → 60→540) to 1024×600.  IRQ gpio 35 = GPIO2_3 pad `0x056` = `0x410B`, `EDGE_FALLING`, `irq 110`; the ~330–380/s rate is the chip's report rate.  Power/reset GPIO1_14/GPIO4_8 are not driven.  Report 198, DECISIONS §47 |
| **WM8994 audio** | **WORKING — stereo 440 Hz on headphones AND speakers at 48 kHz (session 47)** | I²C4 `0x1a` (`0x8994`, revision D); the board machine driver makes `0 [WM8994]: PlayBook_WM8994`.  **Data path (session 46):** the codec's AIF1 is on McBSP3 (the oracle's routing log `AIF 0 Downlink: MM_DL_PORT->MM_EXT_OUT_PORT->MCBSP3`, the live `PCR0 = 0x3`, the PDM pads `0x106/0x108/0x10a/0x10c` = `0x0109/0x0009/0x0109/0x0109`); the card is two ASoC links (`40124000.mcbsp + wm8994-aif1` clocks, `CBC_CFC`; `40126000.mcbsp + wm8994-aif2` data, `CBP_CFP`); the codec is the AIF1 slave, FLL1 locks from our BCLK (`R0x302 = 0x0000`, `R0x732 = 0x6020`, `R0x200 = 0x0011`), no MCLK1; the `omap-mcbsp` channel-layout guard keeps `XCR2 = 0x8041`.  **Speaker (session 47, report 215):** the speakers are on the codec's **line outputs**, so the output stage follows the jack: headphones → HPOUT1 + charge pump (`R0x060 = 0xcc`); jack out → LINEOUT1P/2P (PM3 `0x3cf0`, `R0x01e = 0`, `R0x034/0x035 = 1`, `R0x036 = 0`, `R0x038 = 0x80`, `R0x04c = 0x1f25`, `R0x060 = 0`) with **`spk` (GPIO2_12) driven low** and **GPIO1_15 (the `audio_boost` enable) high**.  The operator hears both speakers at DAC1 volume 96 and 40.  Remaining: retire the `pcm:`/`live N:` diagnostics; the `audio_boost` net is not schematic-confirmed.  Reports 214/215, DECISIONS §59/§60. |
| Userspace (initramfs) | **working, minimal** | `/init` = PID 1, mounts, creates the configfs ACM gadget, forks `/console` in the fallback path.  **No auto-reboot since session 15** — PID 1 reboots only on request (the timer spent a `BOOTCTR_FCTRY` unit per iteration).  **Session 28:** after the gadget and the boot-time top-up, `/init` mounts the region-1 ext4 root **read-write**, bind-mounts /dev//proc//sys, chroots and execs `/sbin/init`; with no `/sbin/init` it falls back to the console.  Report 176, session-28 note |
| Userspace (region-1 root) | **working — a real BusyBox userland** | 64 MiB ext4 at raw `0x29000000`; BusyBox 1.36.1 + init + 401 applet links + `pbconsole`/`pbrpmb` + `/lib/modules/7.3.3`; boots to `sh -l` on `/dev/ttyGS0`; `insmod`/`modprobe` proven.  Current image: `local/images/rootfs-region1-audio-oss-linout.ext4` (sha256 `0342ab06…`, 27 modules, the session-47 speaker path).  The earlier plain userland was image `0xAEDF68D0`. |
| Console | **working** | `help ls cat peek poke kick disp bl i2c i2c16 spi fb kmsg grep blk recv sink blkwr qnx6 rootfs write reboot`; `spi tcon` (low-first) and `spi tconbe` (high-first); `blk info/sum/sumb/rd` is read-only FNV-1a64 over `/dev/mmcblk1` (**`blk info` prints size 0 B — `fstat` bug, see KNOWN-ISSUES**), `grep <substr> <path>` scans any-size text (for `clk_summary`); `sink <len>` measures the link, `blkwr <off> <len> [chunk]` is a timed O_DIRECT write with a deterministic pattern (report 136); `qnx6 [off-hex]` mounts hd3 read-only (report 138); `rootfs [off-hex]` mounts the region-1 ext4 root read-only on `/rootfs` (report 140); argument limit 24.  `pb_reg` op set is now `w a:v` / `r a` / `t a` (16 reads, re-arm) / `u a` (16 reads, no re-arm) / `d from:to` (range dump) / `v` (video liveness).  Every hardware access forks with a bounded wait |
| QNX6 read access (hd3) | **working, read-only** | `CONFIG_QNX6FS_FS=y` + console `qnx6 [off-hex]`: a read-only loop over `/dev/mmcblk1` at raw **`0x49000000`** (superblock `0x49002000`) mounted qnx6 on `/mnt`.  Verified on `0xFC77CD47`; the old `0x48F00000` offset gives `wrong signature (magic)` from the driver.  Report 138 |
| DSS / DISPC via omapdrm | **working — and the core clock is now right** | `/dev/dri`, `/dev/fb0`, `card0-LVDS-1` connected+enabled, `LINE_STATUS` advancing (37 lines/ms), GFX overlay scanning the painted buffer.  The DSS functional clock, which is the DISPC's **core** clock, was left at 48 MHz by the boot chain while the pixel clock is 50.4 MHz — that was the last display defect.  `dss_probe()` now asks the PER PLL for the reference's rate and gets **96 MHz**.  **Session 18:** the boot-time `pb` bridge-read probes are behind `pb_probe` (default **off**), so a normal boot has **no DSI CIO errors**; the DSS rate-mismatch WARN is gone and the real rate is recorded (report 142).  Report 133 §2/§3 |
| DSI1 link | **byte-identical to the reference, PLL included** | `DSI_CTRL = 0x00EAEF9D`, `CLK_CTRL = 0xA0346005`, `TIMING1 = 0x79190999`, `TIMING2 = 0x7FFF7FFF`, `VM_TIMING1/2/3`, `CLK_TIMING = 0x00000E0B`, `VM_TIMING7/8`, PHY, PLL `CFG1 = 0x14A5E8BE` / `CFG2 = 0x00056000`.  All re-read live in session 15. Reports 127 §4, 132 §6 |
| TC358764 bridge | **programmed; configuration now identical to a working system** | The whole 353-register map was read from **TabletOS with its display working** (`tools/pbdsi`) and diffed against ours.  After fixing `VP_HTIM1/2` (§ below) only four registers differ, three of them live status/counters.  `IDREG = 0x00006500`, `LV_CFG = 1`, `LV_PHY0 = 0x0004C02D`, `VP_HTIM1 = 0x00780001`, `VP_HTIM2 = 0x00760400`, `VP_VTIM1/2`, `VFUEN` self-clears. Report 132 §2/§5/§6 |
| Bridge **reads** | **reliable — with `PPI_TX_RX_TA = 0x00040006`** | The value is latched by `PPI_STARTPPI (0x0104)`, so it must be right **in `pb_bridge_seq`**.  Boot with `4` → 4/8 at boot, 8/16 sampled; boot with `0x00040006` → 8/8 and 16/16.  Live writes after boot change nothing.  The VC re-arm does not affect the rate. Report 132 §3 |
| Backlight (LM3532 @ `0x38`) | **working** | binds, `bl_power = 0`, brightness 255; gate is `GPIO2_16`. Report 120 |
| Panel TCON (MCSPI4) | **working — sent automatically at bring-up** | The 9-word Hitachi table is sent by `/init` right after the display comes up (high byte first, 450 kHz, 100 ms after words 0/2/last), matching the reference's own order (`winch_lcdctl 0x193c4`).  Without it the panel stays black even with the bridge programmed. Report 133 §3/§7 |
| Panel identity | **Hitachi (definitive)** | on TabletOS: `GPIO6_DATAIN = 0x00010800` (bit 16 = 1), slog `LCD is Hitachi`.  Report 127 §1 |
| **Image on the panel** | **WORKING** | With `0x8C146805` (and the same source as `0x068C867F`) the panel shows a clean, stable image: solid white, solid red, and the kernel console as legible text.  The three necessary fixes were the bridge delivery (VC0/HS null packet), the LVDS porches + TCON table at bring-up, and the DISPC core clock. Report 133 |
| QNX oracle tool | **`tools/qnx-dsiread.c` → `tools/pbdsi`** | Reads any TC358764 register over DSI from a TabletOS user process (generic read request `0x24` on hw VC1, BTA, RX FIFO pop; response is a generic long read, DT `0x1A`).  Read-only.  This is how the reference values in this document were obtained. Report 132 §2 |
| Kernel patch + config | **7.3.3 (session 46 export)** | `kernel/patches/0001-playbook-winchester-7.3.3.patch` is **8 440 lines / 54 files**, sha256 `fd2774fb…`, applied to pristine 7.3.3 targets with 0 rejects and **54/54 files byte-identical** (session-46 wrap-up; it picks up the `&l4_abe` ABE-refclk assignment, the E5 clock-role change, the McBSP3 two-link card and the `omap-mcbsp` channel-layout guard).  Session 39 adds the board machine driver (`sound/soc/ti/playbook-wm8994.c`, Kconfig/Makefile), the `wm8994-core.c` runtime-PM quirk and the board `sound` node/codec clocks; it also restores `drivers/input/touchscreen/{Kconfig,Makefile}`, which the 48-file export had silently omitted (trap 154).  Config: `kernel/winchester-7.3.3.config` (includes `CONFIG_CHARGER_ISL9519=y`, `CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`, `CONFIG_KERNEL_XZ=y`, `CONFIG_SND_PCM_OSS=m`, `CONFIG_SND_MIXER_OSS=m`).  The 6.15.11 patch (`5 703 lines / 34 files`) stays as the bisect reference; it is **incomplete** — it omits `cm3217.c`, `leds-fan5702.c`, `pbmon-pair.h`, `pbmon-page.h` and their Kconfig/Makefile lines (trap 106) |
| **ISL9519 charger** | **WORKING under Linux (session 31)** | I²C1 `0x48070000`, slave `0x09`; VendorID `0x0049`, DeviceID `0x0003`.  `isl9519.c` (built-in, DTS `charger@9`) sets the four registers in RIM's order with the masked fields and refreshes every 5 s.  Measured **+358 → +397 mA**, 3.775 → 3.794 V, capacity 8 → 9 %, `Charging`; still on at 197 s (watchdog).  Registers read back `0x14=0x0280`, `0x15=0x1070`, `0x3E=0x0D00`, `0x3F=0x0180`.  Report 183 |
| TWL6030 BCI | **probes after the session-31 fix; NOT the board's charger** | `twl6030_charger` probes with the `simple-battery` profile and sets `CONTROLLER_CTRL1=0x10`, `VOREG=0x21`, `VICHRG=0x08`, `CINLIMIT=0x09`, but the stock OS leaves the whole block at bootloader reset values — it charges through the ISL9519.  The BCI node/modules are inert; `twl6030_usb` reports `ONLINE=1`, VBUS 4.81–4.91 V |

## Current images

The unit table below is authoritative for what is on media.  Keep the current
image, one known-good fallback, and anything an open contradiction still needs;
the durable record is the CRC and what it demonstrated, not the file.

| image | base | role |
|---|---|---|
| `0xAD85DF0F` | 7.3.3 | **current region 0** (session 47: the speaker path — line outputs, `spk`/`boost` GPIOs, `disp-en-12-hog` removed; kernel `#44`) |
| `0xF8BADFF3` | 7.3.3 | previous region 0 (session 46: McBSP3 two-link card, kernel `#42`) |
| `0xE5E2AEAF` | 7.3.3 | session-47 intermediate (jack-aware speaker state; `spk` still a hog) |
| `0x47ED0450` | 7.3.3 | session-46 experiment: mcbsp1 as the data engine (still stalled; pads input-enabled) |
| `0x58F85337` | 7.3.3 | previous region 0 (session 45: the committed E5 tree; `#40`) |
| `0x5A04ACD9` | 7.3.3 | session-45 E5 restore (`#35`) |
| `0xE5A13DE7`, `0x6AD9E204` | 7.3.3 | session-45 E9 two-link experiments (data port stalls — §14) |
| `0x92458D00` | 7.3.3 | session-45 E5 + the disabled `&mcbsp1` node (experiment, not on media) |
| `0x3DC4508D` | 7.3.3 | session-45 E6 experiment (McBSP1 as the port; codec clock lost) |
| `0xA66B40EF` | 7.3.3 | session-45 E7/E8 (two PCMs; raw clock pokes crashed the SoC — see trap 193) |
| `0x7A5DD941` | 7.3.3 | session-45 E5 as first built (McBSP bit/frame provider, pads back to outputs) |
| `0xB43F169B` | 7.3.3 | previous region 0 (session 44: reference codec-GPIO init — boot-on `audio-ldo` + pulse, hp input) |
| `0x7C56BDC0` | 7.3.3 | previous region 0 (session 44: `CM_ABE_PLL_REF_CLKSEL` = SYS_CLK + DPLL_ABE 98.304 MHz; E1 clock roles) |
| `0x36597040` | 7.3.3 | previous region 0 (session 42: E1 clock roles — codec AIF1 master, McBSP consumer, input pads) |
| `0xB8830701` | 7.3.3 | previous region 0 (session 41: AUXCLK1 DTS fix, McBSP dump) |
| `0xE36C679A` | 7.3.3 | known-good region 0 (WDT2 + ISL9519 charger) |
| `0x0F6AC47F` | 7.3.3 | last full-regression image (sensors, CM3217) |
| `0x0D1DA0D3` | 6.15.11 | clean pre-userland fallback |
| `0x8C146805` | 6.15.11 | first image with a correct display |

Full image history and the older per-image notes are in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md).

Do not boot `0x5E40C5C1` or `0xB9A1BC4F` (two lanes), or `0x3B04298E`
(LM3532 without VAUX3).

## The unit

| item | value |
|---|---|
| working units | **one: the reference (`0x50175127`)**, armed AND sealed.  The spare (`0x50176EC1`, BSN `1063043093`) is **down** with the 1-2-2 blink = error `0x5B` (report 153) and presents no USB.  Two 32 GB units are bricked and parked |
| region-0 IFS on media | **the session-47 Linux image `0xAD85DF0F`** (`0x939D3C`, kernel `7.3.3` #44, the speaker path; `boot/chainloader/kernel.img`), region 0.  The way back is the 1917 IFS (`local/work/ifs-shim-sealed.ifs`, 7,900,548 B, sha256 `b52195bb…`); the compressed `local/work/ifs1917-compressed.ifs` (sha256 `f14db207…`) is the alternative.  Earlier Linux images (region 0 only, block `0x825`): `0xF8BADFF3` (session 46), `0x36597040` (session 42), `0xB43F169B` (session 44), `0xB8830701` (session 41), `0xF97CD9C7` (session 39), `0x3B2ECFED` (session 38), `0x50842767`, `0x82FC0D35`, the Task-3 `0x0DC36A11`, `0x05819994`, the known-good `0xE36C679A` (`local/images/kernel-0xE36C679A-wdt58s.img`, 0x91A748, sha256 `bdbaf5fc…`), and the touch `0x04AF1433` |
| region-0 RCFS (`/base`) | **still the 1917 RCFS at region-0 offset 0xC00000** — outside the 7.3.3 test image (which ends at 0x939D3C) and not touched this session; not readable as `/base` without the 1917 IFS |
| UFS (`/`) | **the 1917 vendor `.5.ufs` with `autorun::0`** (the session-27 set); the BB10 `.0.4.ufs` is not on media.  (Not re-hashed this session) |
| `/accounts/devuser/startup.sh` | the 1917 UFS carries the operator's script; it last ran during the session-35 S3/S2 TabletOS bench and topped `BOOTCTR_FCTRY` back to `0x32` |
| `BOOTCTR_FCTRY` | **self-replenishing** — the Linux `/init` tops it up at ≤`0x1e` and a TabletOS boot writes `0x32`; read it as a health check (`SAFETY.md` §2) |
| region-1 Linux root | **present** at raw `0x29000000` (region-1 +16 MiB, block `0x2915`): 64 MiB ext4, no journal, sha256 **`0342ab06…`** (`local/images/rootfs-region1-audio-oss-linout.ext4`), **27 modules under `/lib/modules/7.3.3`** (the sensor/charger/audio set plus `snd-pcm-oss`, `snd-mixer-oss`, `snd-soc-playbook-wm8994`), `pbplay` and `pbmix`; the initramfs mounts it **read-write** and chroots into it |
| TabletOS / OS | **NOT running** — the unit is on our Linux image `0xAD85DF0F` (`0fca:8010`, console `pb> `).  TabletOS 2.1.0.1917 returns by flashing `local/work/ifs-shim-sealed.ifs` at block `0x825`; the door (`169.254.0.1`, root ssh) is available only on TabletOS |
| device-side files | **persistent on the UFS**: `/accounts/devuser/bin/{memdump3,memw32,pbi2c}`, the cleaned `/accounts/devuser/startup.sh`, and our `.ssh/authorized_keys` (root ssh).  Re-push QNX `/tmp` tools only after a TabletOS restore |

The factory boot budget is **self-maintaining in bench use**: every successful
factory/CRC boot spends one unit, and the booted OS puts it back — our Linux
`/init` runs `pbrpmb topup 0x1E`, and every TabletOS boot tops the record to
`0x32`.  **Since session 15 our images no longer reboot themselves**, so a test
boot costs one unit, is replenished at boot, and stays up until asked; it is not
a reason to avoid an experiment.  The hard hazards remain: `0` and `> 0x32` do
not fall back to the product path — they hard-lock the unit (`SAFETY.md` §2) —
and a failed/half-formed write or a flat pack is still a brick risk.

## Blockers in play

**The audio blocker is closed, speakers included (session 47, reports 214/215).**
`pbplay`'s 440 Hz tone is audible in both ears on headphones and in both
speakers at 48 kHz ±1 % on the working image (`0xAD85DF0F` + rootfs
`0342ab06…`).  The data path is session 46's (AIF1 on **McBSP3**; two links
`mcbsp2 + wm8994-aif1` clocks, `mcbsp3 + wm8994-aif2` data; the `omap-mcbsp`
channel-layout guard fixed the right channel; the ABE is not needed).  The
speakers needed the codec's **line outputs** (not class-D), `spk` driven
**low**, and GPIO1_15 (the `audio_boost` enable) **high** — all three now in
the driver/DTS.  Remaining audio items: retire the `pcm:`/`live N:`
diagnostics, and the optional **MCLK1 probe** (trap 165).

**The long-standing WL1283 blocker is REOPENED (operator, session-47 close).**
The session-36 "software surface exhausted" claim is withdrawn: it rested on the
1917-truth minimal `&mmc5` host returning `attach_sdio no` (report 196) with the
pads, WL_EN and IRQ matching QNX.  The speaker work showed the missing step was
**static RE of the reference driver + running that driver on the oracle with
its own controls**, not another SoC-side sweep (report 215, DECISIONS §60/§61).
The next session re-does the WL1283 that way: RE `devnp-ti1283.so`,
`devc-seromap-winbt`, `devpm-omap4.so`, `hwio-config-winchester.so` and the
powerman preset in subagents; start the reference driver on the minimal oracle
(`net` is already up) and watch the SDIO/CMD5 bring-up live; then implement in
the driver/DTS.  Do not re-run traps 116–137 unchanged; an overlapping test must
state what differs.

**Found this session: the per-unit WLAN calibration is in the QNX `hd5`
partition** (report 197) — a TI WL128x radio-parameters INI + a binary blob, and
the assigned MAC is in NVRAM `deviceinfo` (`14:74:11:8f:9c:c3`).  It is the
source for `wl128x-nvs.bin` **after** enumeration; it cannot fix CMD5.

**The touch is finished** (I²C2 `0x48072000` = `i2c-1` at `0x65`,
`playbook-munin.c`, `input0`/`event0`; reports 193/194/198, traps 138–141).
The 1917 `scaling.conf` calibration maps the report to 1024×600 and the
GPIO2_3 edge IRQ works (`irq 110`); the power/reset lines are still not driven
by the driver.  It survived this session's warm boot; only cold boots need the
GPIO pulse.

**Media state: region 0 = `0xAD85DF0F`** (kernel `7.3.3` **#44**,
`0x939D3C` B, the speaker path); region 1 = the audio/OSS rootfs
`0342ab06…` (67,108,864 B, 27 modules;
`local/images/rootfs-region1-audio-oss-linout.ext4`).  The unit is on Linux,
not TabletOS.  The way back to TabletOS is the 1917 IFS
(`local/work/ifs-shim-sealed.ifs`, block `0x825`); the previous Linux images
`0xF8BADFF3`, `0xE5E2AEAF`, `0x58F85337`, `0x47ED0450`, `0x5A04ACD9`,
`0x92458D00`, `0x7A5DD941`, `0x36597040`, `0xB8830701`, `0xF97CD9C7`,
`0x3B2ECFED`, `0x0DC36A11`, `0xE36C679A` and `0x04AF1433` remain.
NVRAM and the MCT remain unwritable, and an agent never writes RPMB
(`SAFETY.md` §6).

## What is next

1. **WL1283 — REOPENED (operator, session-47 close).**  After the diagnostics
   cleanup, apply the speaker's method: RE the reference stack in subagents
   (`devnp-ti1283.so`, `devc-seromap-winbt`, `devpm-omap4.so`,
   `hwio-config-winchester.so`, the powerman preset), run `devnp-ti1283` on the
   minimal oracle with its own controls and watch the bring-up, then implement
   in the driver/DTS.  The session-36 "software exhausted" verdict is
   withdrawn; report 196's matching pads/WL_EN/IRQ means the next evidence must
   be the reference's driver behaviour, not another register sweep (HANDOFF
   §0b).  Do not re-run traps 116–137 unchanged.
2. **The WM8994 audio — DONE, speakers included (session 47, reports 214/215).**
   Stereo 440 Hz at 48 kHz on headphones and speakers, image `0xAD85DF0F` +
   rootfs `0342ab06…`.  Remaining items: (a) retire the `pcm:`/`live N:`
   diagnostics and re-verify; (b) the optional **MCLK1 probe** (trap 165);
   (c) confirm what the GPIO1_15 `audio_boost` enable supplies if a schematic
   appears.  The ABE is not needed (DECISIONS §58/§59).
3. **Touch: DONE (session 38, report 198) but down after a cold boot.**  The
   calibration and the IRQ are in the tree and proven; after a cold boot the
   controller does not boot (`0x65` NAKs) until the power/reset GPIOs
   (GPIO1_14/GPIO4_8) are pulsed, which the driver still does not drive.  A
   warm reset keeps it up (session 42).
4. **Build `wl128x-nvs.bin` from the hd5 calibration** (report 197), and add the
   `wlcore@2` child the mainline binding requires (`ti,wl1283`, `reg = <2>`,
   `interrupts` for WL_IRQ, `ref-clock-frequency`/`tcxo-clock-frequency` 26 MHz),
   for the post-enumeration `wl12xx` path — report 195 §3.6, HANDOFF §0b.
5. **The unit is on Linux `0xAD85DF0F`** (region 0, kernel #44) + the
   audio/OSS rootfs (`0342ab06…`, region 1).  To return to TabletOS, flash the
   1917 IFS at block `0x825`; the previous Linux images remain.

## Workspace location

The workspace moved at the end of session 15 from the old checkout (the root
filesystem) to this repository.  Start sessions in the repository root.
Absolute paths were retargeted in `tools/` (`pbssh.sh`, `door-hold.sh`,
`pb_ring.py`, `pb_events.py`), the shipped kernel configs
(`CONFIG_INITRAMFS_SOURCE`) and the canonical docs; historical reports keep their
original wording.

A **fallback copy of the old tree is deliberately kept at `$HOME/playbook-linux`**
until the operator is happy with the move.  It is stale from the moment of the
move — do not work in it, and do not let anything (`CONFIG_INITRAMFS_SOURCE`,
the door scripts) point at it.

**Public snapshot:** `$HOME/SSDdrive/playbook-linux-public` (remote
`git@github.com:Psyden57/playbook-linux.git`).  The private tree is the source
of truth; publish with `tools/publish_public.sh` (see `REPO-MANAGEMENT.md`).

**Both cross-toolchains are inside the workspace** in the gitignored payload:
`local/toolchains/armv7-eabihf` (Buildroot gcc 14.3.0) and
`local/toolchains/qnx660-master` (QNX SDP 6.6).  `. tools/env.sh` exports
`PATH`/`CROSS_COMPILE`/`QNX660`/`QNX_HOST`/`QNX_TARGET`; `tools/env.sh --check`
validates them and prints the versions.

---
