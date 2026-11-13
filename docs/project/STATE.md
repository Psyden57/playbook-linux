# Current state

## Current state — 2026-11-13 (session 45)

**The McBSP is the bit/frame clock provider again (E5) and the transport now
runs — at 48 kHz and with data in the ring — but the codec still does not
decode it.**  Two findings made this the biggest step on the audio blocker
since E1:

1. **The session-41 master-mode stall was `CLKSM`.**  `CLKSM = 0` derives the
   sample-rate generator clock from the **CLKS pad** (TRM Table 23-52), and this
   board drives no CLKS: `pad_clks_ck` is 12 MHz with no consumer and not
   enabled.  `CLKSM = 1` uses the internal path, and the port then frames
   (`SRGR2 = 0x301f`, `SRGR1 = 0x0f1f`, `PCR0 = 0x0f0f`).
2. **The SRG divides 49.152 MHz, not the 12.288 MHz the clock framework reports
   as the McBSP functional clock.**  Measured 4.009× fast with `CLKGDV = 8`;
   `CLKGDV = 32` gives **48 096 frames/s**.  49.152 MHz = `ocp_abe_iclk` =
   `DPLL_ABE/2`, which is why the fix also depends on session 44's DPLL_ABE
   lock: before it, `ST_DPLL_CLK = 0` and the ABE clock was not at its nominal
   rate.

**The codec is now the AIF1 slave and its FLL1 locks from our BCLK** —
`R0x302 = 0x0000`, `R0x732 = 0x6020`, `R0x224 = 0x0c83`, `R0x200 = 0x0011`.
**AIF1CLK exists with no MCLK1 in the path.**  The playback chain is identical
to the reference register for register (DAC1 volume `0xc0`, `R0x601/0x602 = 1`,
`R0x02d/0x02e = 1`, `R0x060 = 0x00ee`, `R0x210 = 0x0083`).

**It is still silent.**  The application's ±12000 square wave was read back out
of `runtime->dma_area` (both at the header and at `hw_ptr`), `XBUFFSTAT` shows
the sDMA filling the transmitter, and the operator hears only a start crackle
plus the DAC noise floor.  The remaining candidates are **the ball that carries
DACDAT** (the reference's own playback capture leaves *both* `dx` — mux 7 safe
mode — and `dr` undriven) and **the frame format** (the reference uses DSP_A,
`R0x300 = 0xc018`; we use I2S).  Report **212**.

**The codec's DATA ball is `abe_mcbsp1_dx`, not `abe_mcbsp2_dx` (E6, report
212 §10).**  The oracle was booted and made to play a wav (operator confirmed it
is audible), and its pads read `abe_mcbsp1_dx` = mux 0 **output** while
`abe_mcbsp2_dx` = mux 7 safe mode — a static configuration (no pad changes
between playing and idle).  That is the silence: E5 drives a ball the codec is
not connected to.  A first attempt at using McBSP1 as the port (E6) clocked
its own pads at 48 053 frames/s but the codec's **FLL did not lock**, so the
codec's *clock* is on the McBSP2 balls.  The board therefore splits the signals
across the two engines: **mcbsp2 for clocks, mcbsp1 for data**.  The design that
follows is McBSP2 as clock master plus McBSP1 as a clock *consumer* on the same
nets, transmitting on `mcbsp1_dx` — see "What is next".

**Bench:** the unit is on **our Linux image `0x92458D00`** (region 0, 0x939C5C
bytes; E5 clock roles + the (disabled) `&mcbsp1` node and its pad group);
region 1 = the audio/OSS rootfs `a026a454…` (with the session-45 diagnostics).  Console `pb> `, no door.
`BOOTCTR_FCTRY` is not readable from the chroot userland (no `/dev/rpmb` node);
the initramfs `/init` top-up ran at boot.  The oracle IFS is no longer on media
(region 0 holds the Linux image); re-flash `local/work/ifs-shim-sealed.ifs` at
`0x825` to boot it.  **To stock TabletOS**, flash
`local/work/minimal/wayback.signed`.

**The magic-`pkexec` rule still holds.**  A flash re-enumerates the ACM gadget
and the tty comes back `root:uucp 0660`; `tools/99-playbook-cdc-acm.rules`
(installed this session) makes it `0666` automatically, and
`pkexec chmod 666 /dev/ttyACM0` remains the documented manual path — wait for
the operator rather than inventing a workaround.

* **E5 landed and is verified** (report 212): DAI format `CBC_CFC`, CPU-side
  `set_sysclk(CLKSM=1)` + `set_clkdiv`, FLL1 from BCLK, `clkx`/`fsx` pads back
  to `PIN_OUTPUT`.  `pbplay 10 440` writes its full 1 933 312 bytes.
* **The two session-45 diagnostics are in the patch** — the machine driver's
  `pcm:` ring readback and the McBSP driver's eight-sample `live N:` dump.  They
  are instrumentation; retire them when the audio path is fixed.
* **`DXR`/`DRR` are not readable data registers on this silicon** — they return
  constants (`0` / `0xc8318118`) regardless of traffic.  Report 205 §5l's
  "`DXR = 0` means empty" is withdrawn; `XBUFFSTAT` is the register that
  answers the question.
* **The kernel patch was re-exported**: 54 files / **8 331 lines**, sha256
  `82d08fcc…`, 0 rejects / 54/54 byte-identical.
* **No boot regressions:** codec revision D, McBSP2, the sound card, the touch,
  the display and the ACM gadget all come up on the final image.

## Per-subsystem state

| subsystem | state | detail / evidence |
|---|---|---|
| Chainloader image (CRC path) | **working** | 828 B loader at image `+0x800` = `0x80100000`; copies zImage/DTB high, checks the DTB magic `0xEDFE0DD0`, jumps (`r0=0`, `r1=~0`, `r2=DTB`). Report 109 §8.1, 103 §5.2 |
| DRAM / memory map | **working** | 1 GB at `0x80000000`; kernel runs from `0x84000000`-loaded zImage, decompressed to `0x80008000`; DTS comment |
| Kernel self-capture | **working** | DRAM ring at PA `0x88000000` (32 KB) + breadcrumb/event log; `tools/pb_ring.py`, `tools/pb_events.py`. Bootstrap §3 |
| WDT2 | **working — 58.6 s window, 10 s pet (session 31)** | The loader arms `WLDR = 0xFFFC1800` (7.8125 s); the kernel reprograms it to the reference's `0xFFE2B400` (58.6 s) at the late initcall via `WSPR` (`0xAAAA`→`0x5555` stop, write `WLDR`, `0xBBBB`→`0x4444` start), **polling `WWPS`** after each write, and pets every 10 s (`TGR` read→invert→write); fallback 2 s if the readback fails.  Verified on `0xE36C679A` by `WCRR` reload timing (~10 s) and uptime >275 s.  The expected dmesg line is missing on 7.3.3 (printk anomaly).  Report 109 §8.1 + correction; trap 115; decision §38 |
| eMMC (MMC2) | **reads proven; writes healthy — but writers must use large blocks** | `mmcblk1` = MCGAFA 59.4 GiB, `boot0`/`boot1`/`rpmb`; needs the `vemmc` fixed supply for `ocr_avail` (report 109 §9).  Session 16 read a freshly flashed 11 152 064-byte image back **byte-exactly** and matched a pre-flash TabletOS capture (report 134); the MMC2 functional clock is 96 MHz.  Session 17 measured the write path (report 136): the driver has no request-size limit — an O_DIRECT write gives **256 KiB `CMD25`s, 5.5–7.0 MB/s** (4 KiB chunks: 0.93 MB/s), one `CMD13` per request (normal `mmc_blk_card_busy`).  The old 17–32 KB/s was `recv`'s tiny buffered writes plus 4–7 unthrottled `printk`s per request to **fbcon**.  Use ≥64 KiB (ideally 256 KiB–1 MiB) writes with the MMC trace off.  **Session 18: region 1 now holds a bootable Linux root** (32 MiB ext4 at raw `0x29000000`), written through the pre-OS `0xF7` loop (2055 frames) and read back byte-exactly (QNX `dd` and Linux `blk sumb`); it mounts read-only via a loop and its `/init` runs (report 140). |
| USB device + gadget | **working, but not a bulk transport** | enumerates `0fca:8010`, CDC-ACM, product string "PlayBook (Linux bring-up)"; `/dev/ttyACM0` ↔ `/dev/ttyGS0`; High Speed (480 Mbps), 512-byte bulk packets.  **Drains at only ~134 KB/s and drops bytes when the host blasts** (report 136 §3.3), so it is a control/debug channel: do not send multi-MB images through `recv`.  Report 114, 136 |
| **WL1283 (MMC5)** | **software surface exhausted (session 37: the 1917-truth minimal host also fails CMD5; the chip is not up — WL_IRQ low)** | **Session 37:** the WL1283 code is archived (`kernel/archive/wl1283/`), the DTS is re-derived from 2.1.0.1917, and the minimal 1917-truth `&mmc5` (image `0x0DC36A11`) probes but returns `attach_sdio no` (report 196); pads `0x0118`, WL_EN `0x0003`, WL_IRQ `0x410B` match QNX and WL_EN is high, but `GPIO2_DATAIN` bit 21 = 0.  Bus, pads, WL_EN/IRQ, PMIC rails, MMC5 CON/HCTL/SYSCTL and the clock manager all match the reference.  QNX enumerates at 24 MHz with CMD5 arg `0x00FFFF00`; Linux's default 400 kHz/CMD52-first also times out.  **Session 35:** `startup-playbook` (never run by our chainloader) is the source of the QNX-only GPIOs — Winchester table at image `0x80122750`; GPIO1 0/4/15/18/28/31 and GPIO2 5/7 are output-low, gpio54 (WL_EN) output-low, gpio53 input/falling (report 192 §4, trap 135).  All eight released to input on the live 1917 are **negative** (trap 136).  `CLK32KG` is base `0x8C` and APP-ON in both (CFG_TRANS identical; CFG_GRP and the CFG_STATE CON/MOD bits differ; trap 135).  **H1** (CMD0 → CMD5 only, no CMD52/CMD8, 24 MHz, arg `0x00FFFF00`, image `0x29784F48`) still times out (`err=-110`, no R4; `HCTL=0x00000b00`, `CAPA=0x04e10080`; trap 137).  The remaining difference is the chip's non-exposed VBAT/TCXO/reset pins or OMAP HSMMC sampling — needs a schematic/board view or logic analyser |
| **Munin touch (CY8CTMA3)** | **working — calibrated, IRQ wired (session 38)** | I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` boots the app and maps the 12-bit report through the 1917 `scaling.conf` calib (raw x 172→1108 → 102→922, y 110→658 → 60→540) to 1024×600.  IRQ gpio 35 = GPIO2_3 pad `0x056` = `0x410B`, `EDGE_FALLING`, `irq 110`; the ~330–380/s rate is the chip's report rate.  Power/reset GPIO1_14/GPIO4_8 are not driven.  Report 198, DECISIONS §47 |
| **WM8994 audio** | **transport works in master mode at 48 kHz; the codec does not decode it (session 45)** | I²C4 `0x1a` (`0x8994`, revision D); the board machine driver makes `0 [WM8994]: PlayBook_WM8994`.  **Session 45 (E5):** the McBSP is the bit/frame provider again (codec AIF1 slave, `R0x302 = 0x0000`) and the codec's **FLL1 locks from our BCLK** (`R0x732 = 0x6020`, `R0x224 = 0x0c83`), so `AIF1CLK` exists from the FLL — **no MCLK1 anywhere in the path**.  The session-41 stall was `CLKSM = 0` (SRG clocked from the undriven CLKS pad); `CLKSM = 1` with `CLKGDV = 32` runs the port at **48 096 frames/s** (`SRGR1 = 0x0f1f`, `SRGR2 = 0x301f`, `PCR0 = 0x0f0f`).  The SRG divides the ABE **interface** clock (49.152 MHz = `ocp_abe_iclk`), not the 12.288 MHz the framework reports.  **Open:** still silent — the app's ±12000 tone is verified in the DMA ring and `XBUFFSTAT` shows the sDMA feeding the transmitter, but the codec decodes nothing.  Candidates: the ball carrying DACDAT (the reference's playback capture leaves `dx` in mux 7 safe_mode and `dr` an input) and the frame format (reference = DSP_A `R0x300 = 0xc018`, ours I2S `0x6010`).  Report 212. |
| Userspace (initramfs) | **working, minimal** | `/init` = PID 1, mounts, creates the configfs ACM gadget, forks `/console` in the fallback path.  **No auto-reboot since session 15** — PID 1 reboots only on request (the timer spent a `BOOTCTR_FCTRY` unit per iteration).  **Session 28:** after the gadget and the boot-time top-up, `/init` mounts the region-1 ext4 root **read-write**, bind-mounts /dev//proc//sys, chroots and execs `/sbin/init`; with no `/sbin/init` it falls back to the console.  Report 176, session-28 note |
| Userspace (region-1 root) | **working — a real BusyBox userland** | 64 MiB ext4 at raw `0x29000000`; BusyBox 1.36.1 + init + 401 applet links + `pbconsole`/`pbrpmb` + `/lib/modules/7.3.3`; boots to `sh -l` on `/dev/ttyGS0`; `insmod`/`modprobe` proven.  Current image: `local/images/rootfs-region1-audio-oss.ext4` (sha256 `968d2f20…`, 27 modules).  The earlier plain userland was image `0xAEDF68D0`. |
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
| Kernel patch + config | **7.3.3 (session 44 export)** | `kernel/patches/0001-playbook-winchester-7.3.3.patch` is **8 109 lines / 54 files**, sha256 `2fc1a865…`, applied to pristine 7.3.3 targets with 0 rejects and **54/54 files byte-identical** (session-44 wrap-up; it picks up the `&l4_abe` ABE-refclk assignment, the E1 clock-role changes in `omap4-winchester.dts` and `playbook-wm8994.c`, and `sound/soc/ti/omap-mcbsp.c`).  Session 39 adds the board machine driver (`sound/soc/ti/playbook-wm8994.c`, Kconfig/Makefile), the `wm8994-core.c` runtime-PM quirk and the board `sound` node/codec clocks; it also restores `drivers/input/touchscreen/{Kconfig,Makefile}`, which the 48-file export had silently omitted (trap 154).  Config: `kernel/winchester-7.3.3.config` (includes `CONFIG_CHARGER_ISL9519=y`, `CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`, `CONFIG_KERNEL_XZ=y`, `CONFIG_SND_PCM_OSS=m`, `CONFIG_SND_MIXER_OSS=m`).  The 6.15.11 patch (`5 703 lines / 34 files`) stays as the bisect reference; it is **incomplete** — it omits `cm3217.c`, `leds-fan5702.c`, `pbmon-pair.h`, `pbmon-page.h` and their Kconfig/Makefile lines (trap 106) |
| **ISL9519 charger** | **WORKING under Linux (session 31)** | I²C1 `0x48070000`, slave `0x09`; VendorID `0x0049`, DeviceID `0x0003`.  `isl9519.c` (built-in, DTS `charger@9`) sets the four registers in RIM's order with the masked fields and refreshes every 5 s.  Measured **+358 → +397 mA**, 3.775 → 3.794 V, capacity 8 → 9 %, `Charging`; still on at 197 s (watchdog).  Registers read back `0x14=0x0280`, `0x15=0x1070`, `0x3E=0x0D00`, `0x3F=0x0180`.  Report 183 |
| TWL6030 BCI | **probes after the session-31 fix; NOT the board's charger** | `twl6030_charger` probes with the `simple-battery` profile and sets `CONTROLLER_CTRL1=0x10`, `VOREG=0x21`, `VICHRG=0x08`, `CINLIMIT=0x09`, but the stock OS leaves the whole block at bootloader reset values — it charges through the ISL9519.  The BCI node/modules are inert; `twl6030_usb` reports `ONLINE=1`, VBUS 4.81–4.91 V |

## Current images

The unit table below is authoritative for what is on media.  Keep the current
image, one known-good fallback, and anything an open contradiction still needs;
the durable record is the CRC and what it demonstrated, not the file.

| image | base | role |
|---|---|---|
| `0x92458D00` | 7.3.3 | **current region 0** (session 45: E5 restore + the disabled `&mcbsp1` node/pads for E6; `#33`) |
| `0x3DC4508D` | 7.3.3 | session-45 E6 experiment (McBSP1 as the port; codec clock lost — not a good state) |
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
| region-0 IFS on media | **the session-45 Linux image `0x92458D00`** (`0x939C5C`, kernel `7.3.3` #33, E5 clock roles + the disabled `&mcbsp1` node; `boot/chainloader/kernel.img`), region 0.  The way back is the 1917 IFS (`local/work/ifs-shim-sealed.ifs`, 7,900,548 B, sha256 `b52195bb…`); the compressed `local/work/ifs1917-compressed.ifs` (sha256 `f14db207…`) is the alternative.  Earlier Linux images (region 0 only, block `0x825`): `0x36597040` (session 42), `0xB43F169B` (session 44), `0xB8830701` (session 41), `0xF97CD9C7` (session 39), `0x3B2ECFED` (session 38), `0x50842767`, `0x82FC0D35`, the Task-3 `0x0DC36A11`, `0x05819994`, the known-good `0xE36C679A` (`local/images/kernel-0xE36C679A-wdt58s.img`, 0x91A748, sha256 `bdbaf5fc…`), and the touch `0x04AF1433` |
| region-0 RCFS (`/base`) | **still the 1917 RCFS at region-0 offset 0xC00000** — outside the 7.3.3 test image (which ends at 0x939C5C) and not touched this session; not readable as `/base` without the 1917 IFS |
| UFS (`/`) | **the 1917 vendor `.5.ufs` with `autorun::0`** (the session-27 set); the BB10 `.0.4.ufs` is not on media.  (Not re-hashed this session) |
| `/accounts/devuser/startup.sh` | the 1917 UFS carries the operator's script; it last ran during the session-35 S3/S2 TabletOS bench and topped `BOOTCTR_FCTRY` back to `0x32` |
| `BOOTCTR_FCTRY` | **self-replenishing** — the Linux `/init` tops it up at ≤`0x1e` and a TabletOS boot writes `0x32`; read it as a health check (`SAFETY.md` §2) |
| region-1 Linux root | **present** at raw `0x29000000` (region-1 +16 MiB, block `0x2915`): 64 MiB ext4, no journal, sha256 **`a026a454…`** (`local/images/rootfs-region1-audio-oss.ext4`), **27 modules under `/lib/modules/7.3.3`** (the sensor/charger/audio set plus `snd-pcm-oss`, `snd-mixer-oss`, `snd-soc-playbook-wm8994`), `pbplay` and `pbmix`; the initramfs mounts it **read-write** and chroots into it |
| TabletOS / OS | **NOT running** — the unit is on our Linux image `0x92458D00` (`0fca:8010`, console `pb> `).  TabletOS 2.1.0.1917 returns by flashing `local/work/ifs-shim-sealed.ifs` at block `0x825`; the door (`169.254.0.1`, root ssh) is available only on TabletOS |
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

**For the current focus (item 9 step 5 audio), the blocker is now named: the
codec's data ball.**  `abe_mcbsp1_dx` (pad `0x102`) is where the reference puts
DACDAT (mux 0, output, static); E5 drives `abe_mcbsp2_dx` (pad `0x0fa`), which
the codec is not connected to.  The transport, the rate and the codec's clocks
are all verified: the transport runs at 48 kHz with the codec clocked from our
own BCLK.  Session 45 (E5, report 212) put the
McBSP back as the bit/frame provider with the codec as AIF1 slave and the FLL
referenced to BCLK — **no MCLK1 in the path** — and fixed the session-41 stall
(`CLKSM = 0` clocked the SRG from the undriven CLKS pad; `CLKSM = 1` uses the
internal path).  The SRG divides the ABE interface clock (49.152 MHz), so
`CLKGDV = 32`; the stream then measures 48 096 frames/s.  The application's tone
is verified in the DMA ring and the sDMA is verified feeding the transmitter
(`XBUFFSTAT`).  **What remains is the codec's AIF1 data input:** either the ball
that carries DACDAT (the reference's playback capture leaves `dx` in mux 7
safe_mode and `dr` an input — so the reference drives neither) or the frame
format (the reference runs DSP_A, `R0x300 = 0xc018`; we run I2S).  The old
MCLK1 route is closed for this design but not solved: MCLK1 still does not
clock (reports 206/208/209), and the pad/board/PMIC probe (trap 165) is still
the only direct measurement.  Reports 206, 209, 212.

**The long-standing WL1283 blocker is unchanged (chip-side, software
exhausted).**  The 1917-truth minimal `&mmc5` host (report 195) boots and
probes but **CMD5 fails** (report 196): the pads, WL_EN level and IRQ
direction are bit-for-bit identical to QNX (`0x148/14C/150 = 0x0118`,
`0x07C = 0x0003`, `0x07A = 0x410B`, `GPIO2_DATAOUT = 0x31413200`), yet
`GPIO2_DATAIN` bit 21 (WL_IRQ) is 0 — **the chip is not up under Linux**, while
QNX brings it up (`ti0` UP/RUNNING, 40+ scans).  The DT model is therefore not
the missing piece; the remaining evidence is chip-side (VBAT / 26 MHz TCXO /
SLOW_CLK) or OMAP HSMMC sampling, and needs a **board view or logic analyser**.
Do not repeat traps 116–143 as-is; an overlapping test must state what differs.

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

**Media state: region 0 = the session-44 image `0xB43F169B`** (kernel `7.3.3`
with the E1 clock roles, the ABE-refclk fix and the codec-GPIO init); region 1 =
the audio/OSS rootfs (`3df8056c…`, 27 modules;
`local/images/rootfs-region1-audio-oss.ext4`).
The unit is on Linux, not TabletOS.  The way back to TabletOS is
the 1917 IFS (`local/work/ifs-shim-sealed.ifs`, block `0x825`); the previous
Linux images `0x36597040`, `0xB8830701`, `0xF97CD9C7`, `0x3B2ECFED`, `0x0DC36A11`,
`0xE36C679A` and `0x04AF1433` remain.
NVRAM and the MCT remain unwritable, and an agent never writes RPMB
(`SAFETY.md` §6).

## What is next

1. **WL1283 — stopped by the operator (session 36).**  The 1917-truth DT redo
   is done and negative (report 196); the remaining measurements (VBAT / 26 MHz
   TCXO / SLOW_CLK, MMC5 CLK/CMD/DAT0) need a board view or logic analyser.  Do
   not re-open it without the operator.
2. **The WM8994 audio half of item 9 step 5 — the transport runs; the codec
   does not decode (report 212).**  E5 is in the tree and on media
   (`0x7A5DD941`): McBSP bit/frame provider, codec AIF1 slave, FLL1 from BCLK,
   48 096 frames/s, data verified in the ring.  **Next, in order:**
   (a) **resolved (E6):** the data ball is `abe_mcbsp1_dx` (`0x102`) —
   `abe_mcbsp2_dx`, which E5 drives, is not connected to the codec;
   (b) **the two-engine design** — keep McBSP2 as the clock master (E5, proven:
   FLL locks) and add **McBSP1 as a clock consumer** (clkx/fsx inputs on the
   same nets) transmitting on `mcbsp1_dx`.  ASoC carries one CPU DAI per link:
   use a second `snd_soc_dai_link` with `snd-soc-dummy` as its codec, or
   configure McBSP1 by hand in the machine driver.  **First sub-question to
   settle:** do mcbsp1's clock *inputs* see mcbsp2's clock *outputs*?  E6 only
   showed that mcbsp1's clock *outputs* do not reach the codec.  A consumer-mode
   build whose steady dump shows `XEMPTY`/`XRDY` moving answers it;
   (c) the **DSP_A build** was tried and is not the fault (E6 note in report
   212 §4/§10: `R0x300 = 0x6018` matched the reference's format and stayed
   silent, because the ball was wrong);
   (d) keep the free-running NCO as a mechanism probe only.  The MCLK1 route
   (probe on `fref_clk2_out`) remains closed by trap 165.
3. **Touch: DONE (session 38, report 198) but down after a cold boot.**  The
   calibration and the IRQ are in the tree and proven; after a cold boot the
   controller does not boot (`0x65` NAKs) until the power/reset GPIOs
   (GPIO1_14/GPIO4_8) are pulsed, which the driver still does not drive.  A
   warm reset keeps it up (session 42).
4. **Build `wl128x-nvs.bin` from the hd5 calibration** (report 197), and add the
   `wlcore@2` child the mainline binding requires (`ti,wl1283`, `reg = <2>`,
   `interrupts` for WL_IRQ, `ref-clock-frequency`/`tcxo-clock-frequency` 26 MHz),
   for the post-enumeration `wl12xx` path — report 195 §3.6, HANDOFF §0b.
5. **The unit is on Linux `0xB43F169B`** (region 0) + the E1 audio/OSS rootfs
   (`3df8056c…`, region 1).  To return to TabletOS, flash the 1917 IFS at
   block `0x825`; the previous Linux images remain.

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
