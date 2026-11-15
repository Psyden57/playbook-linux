# Current state

## Current state — 2026-11-14 (session 51)

**THE WL1283 WLAN WORKS: it scans.**  Two findings closed the bring-up in one
session — the `-110` recovery loop was the chip's **ELP** sleep (now CAM), and
the never-completing scan was the **5-sr firmware**: with
`wl128x-fw-4-sr.bin` (`Rev 7.3.5.0.98`) `wlan0` is stable **and**
`iw dev wlan0 scan` returns real results (**17–21 BSS**, both bands, no
errors), verified from flashed media after a reboot.

* **The failing transfer (report 232):** the FW-status read — **CMD53 to
  `0x1efc0`, 84 bytes (`0x54`), byte mode, 256-byte block** — issued from the
  WL_IRQ thread (`irq/132-wl12xx`) right after `wlcore: firmware booted`.  The
  same read succeeds whenever the chip is awake, so the transfer is legal and
  the *state* was the fault.  Identified with a temporary `dev_err` on the error
  path only (the prescribed `wlcore_sdio dump=1` changes the timing and hides
  the failure — trap 239); that instrument was reverted.
* **The cause and the fix:** the chip's **ELP** (firmware sleep).  With
  `power/control = on` (no runtime PM) or with `sleep_auth = 0` (CAM) the
  interface is stable; re-enabling ELP at runtime
  (`echo 2 > …/wlcore/sleep_auth`) reproduces the `-110` and the loop within
  seconds.  The fix is one field: `wl12xx_default_conf.conn.sta_sleep_auth =
  WL1271_PSM_CAM` (was `WL1271_PSM_ILLEGAL` → ELP).  Dynamic PS is unaffected;
  only the host-driven ELP is off, which costs power, not function.
* **The scan was the firmware (report 235):** `5-sr` (`Rev 7.3.10.0.142`)
  boots but **panics on the first scan** (no event, 30 s timeout, recovery; the
  panic-log read proved a crash — reports 233/241).  `4-sr`
  (`Rev 7.3.5.0.98`) **scans** on the same driver/NVS/board.  The port lowers
  `wl12xx`'s SR floor to **7.3.5.0** (`WL128X_MAJOR_SR_VER 5`,
  `WL128X_MINOR_SR_VER 0`) and sets `WL128X_FW_NAME_SINGLE` to
  `wl128x-fw-4-sr.bin`; both files stay installed.
* **The board was exonerated (report 234):** stock TabletOS on this unit runs
  `ti0` UP/RUNNING, **associated** (`192.168.0.212`), on vendor firmware
  `Rev 7.1.4.10.77` with `IPC_EVENT_SCAN_COMPLETE` 6 s after boot.
* **Not the cause:** the client tooling and `regulatory.db` (both shipped and
  verified), the per-unit NVS (spliced, no change), the block size/byte mode,
  the 32 kHz slow clock (`CLK32KG` = `07 01 55`).
* **Remaining WL1283 work:** **association** (station config with
  `wpa_passphrase`/`wpa_supplicant`, whose binaries are in the image), the
  per-unit **calibration** NVS (report 233 — matters for TX power now), the
  Linux **BT** bring-up (`BT_EN` = gpio 55 = `GPIO2_23`, UART2), and *why* the
  ELP wake is not honoured (report 232 §6) and *why* 5-sr panics (235 §3) —
  neither blocks use.

**Bench (session 51, measured).**  Unit on our Linux image **`0x9712A086`**
(region 0 unchanged this session: `0x939A74` B, kernel `7.3.3` **#60**); region 1
= `local/images/rootfs-region1-wl1283-scan.ext4` (sha256 **`f159a221…`**, 34
modules, the CAM + 4-sr `wl12xx.ko` md5 `2fb86c4d…`, built in-tree so it carries
no `(O)` taint, both SR firmware files, plus **`/usr/bin/iw`**,
**`/usr/sbin/wpa_{supplicant,cli,passphrase}`** and the signed
**`/lib/firmware/regulatory.db`(.p7s)**).  Region 1 was flashed on its own at
`0x2915` three times (module/firmware/tool changes → region-0 CRC unchanged,
trap 190).  `0fca:8010`, console `pb> `, **ssh on `169.254.0.1`**; `wlan0` `UP`
after `ip link set wlan0 up` (rcS loads the stack, it does not raise the link),
firmware `Rev 7.3.5.0.98`, `iw dev wlan0 scan` → 17 BSS.  Kernel patch:
**59 files / 8 713 lines**, sha256 `2be9e99c…`.  **`BOOTCTR_FCTRY` is not
readable from Linux** (`/dev/rpmb` exists only on TabletOS); every boot's `/init`
runs `pbrpmb topup 0x1E`, so read it as a health check on the next TabletOS
boot (`SAFETY.md` §2).  **No device-side files were deployed** (`/tmp` is empty
after the last reboot; nothing added to the UFS).  No NVRAM/MCT/RPMB write; **no
restore required** — the unit is left on our Linux image.

## Previous states

The per-session snapshots are in
[`docs/history/STATE-HISTORY.md`](../history/STATE-HISTORY.md):

* **session 50** (2026-11-14) — the WL1283 enumerates (`CLK32KG`) and firmware
  boots; the interface recovery-looped (reports 230/231).
* **session 49** (2026-11-14) — the blocker measured from both sides, the host
  program excluded, the "silent card" instrument corrected (reports 216–227).
* **session 48 and older** — see the same file.

## Per-subsystem state

| subsystem | state | detail / evidence |
|---|---|---|
| Chainloader image (CRC path) | **working** | 828 B loader at image `+0x800` = `0x80100000`; copies zImage/DTB high, checks the DTB magic `0xEDFE0DD0`, jumps (`r0=0`, `r1=~0`, `r2=DTB`). Report 109 §8.1, 103 §5.2 |
| DRAM / memory map | **working** | 1 GB at `0x80000000`; kernel runs from `0x84000000`-loaded zImage, decompressed to `0x80008000`; DTS comment |
| Kernel self-capture | **working** | DRAM ring at PA `0x88000000` (32 KB) + breadcrumb/event log; `tools/pb_ring.py`, `tools/pb_events.py`. Bootstrap §3 |
| WDT2 | **working — 58.6 s window, 10 s pet (session 31)** | The loader arms `WLDR = 0xFFFC1800` (7.8125 s); the kernel reprograms it to the reference's `0xFFE2B400` (58.6 s) at the late initcall via `WSPR` (`0xAAAA`→`0x5555` stop, write `WLDR`, `0xBBBB`→`0x4444` start), **polling `WWPS`** after each write, and pets every 10 s (`TGR` read→invert→write); fallback 2 s if the readback fails.  Verified on `0xE36C679A` by `WCRR` reload timing (~10 s) and uptime >275 s.  The expected dmesg line is missing on 7.3.3 (printk anomaly).  Report 109 §8.1 + correction; trap 115; decision §38 |
| eMMC (MMC2) | **reads proven; writes healthy — but writers must use large blocks** | `mmcblk1` = MCGAFA 59.4 GiB, `boot0`/`boot1`/`rpmb`; needs the `vemmc` fixed supply for `ocr_avail` (report 109 §9).  Session 16 read a freshly flashed 11 152 064-byte image back **byte-exactly** and matched a pre-flash TabletOS capture (report 134); the MMC2 functional clock is 96 MHz.  Session 17 measured the write path (report 136): the driver has no request-size limit — an O_DIRECT write gives **256 KiB `CMD25`s, 5.5–7.0 MB/s** (4 KiB chunks: 0.93 MB/s), one `CMD13` per request (normal `mmc_blk_card_busy`).  The old 17–32 KB/s was `recv`'s tiny buffered writes plus 4–7 unthrottled `printk`s per request to **fbcon**.  Use ≥64 KiB (ideally 256 KiB–1 MiB) writes with the MMC trace off.  **Session 18: region 1 now holds a bootable Linux root** (32 MiB ext4 at raw `0x29000000`), written through the pre-OS `0xF7` loop (2055 frames) and read back byte-exactly (QNX `dd` and Linux `blk sumb`); it mounts read-only via a loop and its `/init` runs (report 140). |
| USB device + gadget | **working, but not a bulk transport** | enumerates `0fca:8010`, CDC-ACM, product string "PlayBook (Linux bring-up)"; `/dev/ttyACM0` ↔ `/dev/ttyGS0`; High Speed (480 Mbps), 512-byte bulk packets.  **Drains at only ~134 KB/s and drops bytes when the host blasts** (report 136 §3.3), so it is a control/debug channel: do not send multi-MB images through `recv`.  Report 114, 136 |
| **WL1283 (MMC5)** | **WORKING — enumerates, firmware `Rev 7.3.5.0.98`, `wlan0` stable, and it SCANS (session 51)** | **Session 51 (report 232): the recovery loop was the firmware's ELP sleep.**  The failing transfer is the **FW-status read** — CMD53 to `0x1efc0`, 84 bytes, byte mode, 256-byte block — from the WL_IRQ thread right after `firmware booted`; it fails only once the chip has slept, and the identical read succeeds when it is awake.  Fix: `wl12xx_default_conf.conn.sta_sleep_auth = WL1271_PSM_CAM` (was `ILLEGAL` → ELP).  Verified after a real region-1 flash + reboot: one firmware boot, zero failures, `wlan0` `UP`, debugfs `sleep_auth` 0.  `wl128x-fw-4-sr.bin` is the **shipped** SR firmware (report 235): 5-sr (7.3.10.0.142) boots but panics on the first scan, so `wl12xx`'s floor is lowered to 7.3.5.0 and `WL128X_FW_NAME_SINGLE` names the 4-sr file; both files stay installed.  **WLAN (report 231):** `wlcore`/`wl12xx`/`mac80211`/`cfg80211` + `wl128x-nvs.bin`, the `ti,wl1283` child (`reg = <2>`, WL_IRQ gpio 53), `cap-power-off-card`, and an `mmc-pwrseq-simple` pulsing `WL_EN` (low → 50 ms → high → 120 ms) → firmware `Rev 7.3.5.0.98`, MAC `14:74:11:8f:9c:c3` (the assigned address, patched into the generic NVS; the radio calibration is still default).  **Enumeration (report 230):** the TWL6030 `CLK32KG` 32.768 kHz SLOW_CLK is physical `0xBC`–`0xBE` and was OFF; `CONFIG_CLK_TWL=y` + an enable in `clk-twl`'s probe (~1.9 s, before MMC5 at ~3.0 s) fixes it.  **Remaining (report 235):** the WLAN **scans** (17–21 BSS per scan, both bands) — next is **association** (station config with `wpa_passphrase`/`wpa_supplicant`, both in the image), then the per-unit **NVS** (found: the 468-byte NVRAM record at raw `0x152cc`; matters for TX power), the BT bring-up, and two open questions: *why* the 5-sr firmware panics on scan (235 §3) and *why* the ELP wake is not honoured (232 §6).  **Session-49/50 negatives (do not repeat):** the reference's MMCHS5 program and WL_EN ordering replayed, the stock-TabletOS diff, the boot-path RE, the clock sweep (reports 217/218/221–229).  The BT half's enable is `BT_EN` = gpio 55 = `GPIO2_23`, held low while the WLAN works (report 216). |
| **GPIO1 pads / board lines** | **fixed (session 49): three pads were driving GPMC alternate functions** | `0x4A100050` (`gpmc_ad0/1`) and `0x4A100074` (`gpmc_ncs0/1`) were left at mode 0, so pad logic drove GPMC bus lines on a board with no GPMC device while gpiolib could not touch them (trap 228); `0x4A31E058` (GPIO1_31) was unmuxed with its input buffer off.  The DTS now sets mode 3 + pull-down and hogs GPIO1 0/18/28/31 low, so `GPIO1_OE = 0x6FFB1FBA` against the reference's `0x6FFB1FAA` (the missing bit 4 is pad `0x058`'s low half = GPIO2_4, owned by `bridge_pins`, trap 229) and `GPIO1_DATAOUT` is identical.  Image `0x0114FFD3`.  **Not** the WL1283 blocker (card still silent).  Report 220 |
| **Munin touch (CY8CTMA3)** | **working — calibrated, IRQ wired (session 38)** | I²C2 `0x48072000` = `i2c-1` at `0x65`; `playbook-munin.c` boots the app and maps the 12-bit report through the 1917 `scaling.conf` calib (raw x 172→1108 → 102→922, y 110→658 → 60→540) to 1024×600.  IRQ gpio 35 = GPIO2_3 pad `0x056` = `0x410B`, `EDGE_FALLING`, `irq 110`; the ~330–380/s rate is the chip's report rate.  Power/reset GPIO1_14/GPIO4_8 are not driven.  Report 198, DECISIONS §47 |
| **WM8994 audio** | **WORKING — stereo 440 Hz on headphones AND speakers at 48 kHz; diagnostics retired and re-verified (session 48)** | I²C4 `0x1a` (`0x8994`, revision D); the board machine driver makes `0 [WM8994]: PlayBook_WM8994`.  **Data path (session 46):** the codec's AIF1 is on McBSP3 (the oracle's routing log `AIF 0 Downlink: MM_DL_PORT->MM_EXT_OUT_PORT->MCBSP3`, the live `PCR0 = 0x3`, the PDM pads `0x106/0x108/0x10a/0x10c` = `0x0109/0x0009/0x0109/0x0109`); the card is two ASoC links (`40124000.mcbsp + wm8994-aif1` clocks, `CBC_CFC`; `40126000.mcbsp + wm8994-aif2` data, `CBP_CFP`); the codec is the AIF1 slave, FLL1 locks from our BCLK (`R0x302 = 0x0000`, `R0x732 = 0x6020`, `R0x200 = 0x0011`), no MCLK1; the `omap-mcbsp` channel-layout guard keeps `XCR2 = 0x8041`.  **Speaker (session 47, report 215):** the speakers are on the codec's **line outputs**, so the output stage follows the jack: headphones → HPOUT1 + charge pump (`R0x060 = 0xcc`); jack out → LINEOUT1P/2P (PM3 `0x3cf0`, `R0x01e = 0`, `R0x034/0x035 = 1`, `R0x036 = 0`, `R0x038 = 0x80`, `R0x04c = 0x1f25`, `R0x060 = 0`) with **`spk` (GPIO2_12) driven low** and **GPIO1_15 (the `audio_boost` enable) high**.  **Session 48** removed the `pcm:` ring readback and the `live N:` FIFO dump and re-verified both outputs at DAC1 volume 40: `jack out (speaker)` / `jack in (headphone)`, "done, FLL1 locked", both PCMs `RUNNING`, the full byte count written, `spk` low during the speaker route and high on the headphone route, `boost` high.  Reports 214/215, DECISIONS §59/§60. |
| Userspace (initramfs) | **working, minimal** | `/init` = PID 1, mounts, creates the configfs gadget (**ACM console + RNDIS**, session 48), forks `/console` in the fallback path.  **No auto-reboot since session 15** — PID 1 reboots only on request (the timer spent a `BOOTCTR_FCTRY` unit per iteration).  **Session 28:** after the gadget and the boot-time top-up, `/init` mounts the region-1 ext4 root **read-write**, bind-mounts /dev//proc//sys, chroots and execs `/sbin/init`; with no `/sbin/init` it falls back to the console.  Report 176, session-28 note |
| **USB network + ssh** | **working — commands and interactive sessions (session 48)** | The gadget carries an **RNDIS** function next to ACM, so the host sees four interfaces and a net device (`enp8s0f3u1i2`, MAC `02:1a:11:00:00:02`); the region-1 `rcS` brings `usb0` up at **`169.254.0.1/16`** and starts **dropbear** listening on `169.254.0.1:22` only.  Key-only auth, host keys shipped, password/PAM/shadow off.  **`tools/pbssh.sh 'cmd'` works and is 2.3 MB/s** (vs the ACM link's 134 KB/s that drops bytes, trap 51), and an **interactive** session works too — verified by the operator under a real terminal (prompt, history, completion) and reproduced from this side with `script -qec`.  An agent's own shell has no tty, so an apparent "closes with no output" is a harness artefact, not a defect (trap 216).  DECISIONS §62. |
| Userspace (region-1 root) | **working — a real BusyBox userland, now with ssh** | 64 MiB ext4 at raw `0x29000000`; BusyBox 1.36.1 + init + 401 applet links + `pbconsole`/`pbrpmb`/`pbplay`/`pbmix` + `/lib/modules/7.3.3`; boots to `sh -l` on `/dev/ttyGS0` and serves **dropbear on `usb0`**; `insmod`/`modprobe` proven.  Current image: `local/images/rootfs-region1-rndis-ssh.ext4` (sha256 `a4e76b18…`, 27 modules, dropbear + host keys + `authorized_keys`, session 48).  Earlier: `…-audio-oss-linout.ext4` (`af5a0a20…`, session 48 diagnostics-retired), `0342ab06…` (session 47) and the plain userland `0xAEDF68D0`. |
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
| Kernel patch + config | **7.3.3 (session 50 export)** | `kernel/patches/0001-playbook-winchester-7.3.3.patch` is **8 658 lines / 57 files**, sha256 `e7cf9628…`, applied to pristine 7.3.3 targets with 0 rejects and **57/57 files byte-identical**.  Session 50 adds `drivers/clk/clk-twl.c` (enable `clk32kg` — the WL1283's 32 kHz) and the board-DTS WL1283 changes (`ti,wl1283` child, `cap-power-off-card`, the WL_EN pwrseq, the hog removed).  Session 49 added the report-220 board-DTS GPIO1 pad fix and `arch/arm/Kconfig.debug` (missing from every export since session 30 — the tree walk found it, trap 211 class).  Session 48 retires the two diagnostic blocks and adds `sound/soc/ti/omap-mcbsp-priv.h`, which every export since session 41 had omitted although `omap-mcbsp.c` reads the fields it declares (trap 211 — the patch could not compile from pristine until now).  Session 47 added the speaker path (line-output route, `spk`/`boost` GPIOs, the `disp-en-12-hog` removal); session 46 the `&l4_abe` ABE-refclk assignment, the E5 clock-role change, the McBSP3 two-link card and the `omap-mcbsp` channel-layout guard; session 39 the board machine driver (`sound/soc/ti/playbook-wm8994.c`, Kconfig/Makefile), the `wm8994-core.c` runtime-PM quirk and the board `sound` node/codec clocks, and it restored `drivers/input/touchscreen/{Kconfig,Makefile}`, which the 48-file export had silently omitted (trap 154).  Config: `kernel/winchester-7.3.3.config` (includes `CONFIG_CLK_TWL=y`, `CONFIG_CHARGER_ISL9519=y`, `CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`, `CONFIG_KERNEL_XZ=y`, `CONFIG_SND_PCM_OSS=m`, `CONFIG_SND_MIXER_OSS=m`).  The 6.15.11 patch (`5 703 lines / 34 files`) stays as the bisect reference; it is **incomplete** — it omits `cm3217.c`, `leds-fan5702.c`, `pbmon-pair.h`, `pbmon-page.h` and their Kconfig/Makefile lines (trap 106) |
| **ISL9519 charger** | **WORKING under Linux (session 31)** | I²C1 `0x48070000`, slave `0x09`; VendorID `0x0049`, DeviceID `0x0003`.  `isl9519.c` (built-in, DTS `charger@9`) sets the four registers in RIM's order with the masked fields and refreshes every 5 s.  Measured **+358 → +397 mA**, 3.775 → 3.794 V, capacity 8 → 9 %, `Charging`; still on at 197 s (watchdog).  Registers read back `0x14=0x0280`, `0x15=0x1070`, `0x3E=0x0D00`, `0x3F=0x0180`.  Report 183 |
| TWL6030 BCI | **probes after the session-31 fix; NOT the board's charger** | `twl6030_charger` probes with the `simple-battery` profile and sets `CONTROLLER_CTRL1=0x10`, `VOREG=0x21`, `VICHRG=0x08`, `CINLIMIT=0x09`, but the stock OS leaves the whole block at bootloader reset values — it charges through the ISL9519.  The BCI node/modules are inert; `twl6030_usb` reports `ONLINE=1`, VBUS 4.81–4.91 V |

## Current images

The unit table below is authoritative for what is on media.  Keep the current
image, one known-good fallback, and anything an open contradiction still needs;
the durable record is the CRC and what it demonstrated, not the file.

| image | base | role |
|---|---|---|
| `0x9712A086` | 7.3.3 | **current region 0** (built session 50: the CLK32KG fix + the `ti,wl1283` child, `cap-power-off-card` and the pwrseq; kernel `#60`, `0x939A74` B).  Unchanged in session 51 — that session's work is module-only (region 1) |
| `0xF64A680D` | 7.3.3 | session-50 intermediate (`+cap-power-off-card`; the second firmware boot still failed) |
| `0x53F7F97B` | 7.3.3 | session-50 intermediate (`+ti,wl1283` child) |
| `0x69CDAC94` | 7.3.3 | session-50 first fix (`CONFIG_CLK_TWL=y` + `clk32kg` enable; the card enumerates) |
| `0x17C2E119` | 7.3.3 | previous region 0 (session 50: the session-49 tree rebuilt after the reverted experiments; kernel `#59`) |
| `0x91E22899` | 7.3.3 | session-50 experiment (the slow-identification-clock test: `f_min=100000` + the archived `PBX`/`PBIRQ`/`PBCMD` trace; kernel `#58`) |
| `0xBAFD9B3B` | 7.3.3 | previous region 0 (session 49: the session-48 tree + report 220's GPIO1 pad fix; kernel `#57`) |
| `0x0114FFD3` | 7.3.3 | session-49 experiment (the GPIO1 pad fix, kernel `#51`) |
| `0x71A79419` | 7.3.3 | session-49 intermediate (the session-48 tree rebuilt after the WL1283 experiments were reverted; kernel `#50`) |
| `0x12C8C320` | 7.3.3 | previous region 0 (session 48: the RNDIS + dropbear initramfs; kernel `#45`) |
| `0xAD85DF0F` | 7.3.3 | previous region 0 (session 47: the speaker path — line outputs, `spk`/`boost` GPIOs, `disp-en-12-hog` removed; kernel `#44`) |
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
| region-0 IFS on media | **the session-50 Linux image `0x9712A086`** (`0x939A74`, kernel `7.3.3` #60, the CLK32KG fix + the wl1283 DT child and pwrseq; `boot/chainloader/kernel.img`), region 0.  The way back is the 1917 IFS (`local/work/ifs-shim-sealed.ifs`, 7,900,548 B, sha256 `b52195bb…`); the compressed `local/work/ifs1917-compressed.ifs` (sha256 `f14db207…`) is the alternative.  Earlier Linux images (region 0 only, block `0x825`): `0xF64A680D`, `0x53F7F97B`, `0x69CDAC94`, `0x17C2E119`, `0xBAFD9B3B`, `0x91E22899`, `0x12C8C320`, `0xAD85DF0F`, `0xF8BADFF3`, `0x36597040`, `0xB43F169B`, `0xB8830701`, `0xF97CD9C7`, `0x3B2ECFED`, `0x50842767`, `0x82FC0D35`, the Task-3 `0x0DC36A11`, `0x05819994`, the known-good `0xE36C679A` (`local/images/kernel-0xE36C679A-wdt58s.img`, 0x91A748, sha256 `bdbaf5fc…`), and the touch `0x04AF1433` |
| region-0 RCFS (`/base`) | **now the full 1917 RCFS at `0xC00000`** (`local/work/rcfs-1917.rcfs`, the modified one with the `/accounts/devuser/startup.sh` hook, sha256 `c4ab6e34…`): session 49 wrote it to recover root on a stock-TabletOS boot, and it sits **outside** the 7.3.3 test image (which ends at `0x93A1DC`).  The pruned oracle RCFS is no longer on media — re-flash `local/work/minimal/setB.signed` for the minimal oracle (trap 227).  Not readable as `/base` without the 1917 IFS |
| UFS (`/`) | **the 1917 vendor `.5.ufs` with `autorun::0`** (the session-27 set); the BB10 `.0.4.ufs` is not on media.  (Not re-hashed this session) |
| `/accounts/devuser/startup.sh` | the 1917 UFS carries the operator's script; it last ran during the session-35 S3/S2 TabletOS bench and topped `BOOTCTR_FCTRY` back to `0x32` |
| `BOOTCTR_FCTRY` | **self-replenishing** — the Linux `/init` tops it up at ≤`0x1e` and a TabletOS boot writes `0x32`; read it as a health check (`SAFETY.md` §2) |
| region-1 Linux root | **present** at raw `0x29000000` (region-1 +16 MiB, block `0x2915`): 64 MiB ext4, no journal, sha256 **`f159a221…`** (`local/images/rootfs-region1-wl1283-scan.ext4`, session 51: the CAM + 4-sr `wl12xx.ko` and the WLAN client tooling), **34 modules under `/lib/modules/7.3.3`** (the sensor/charger/audio set plus **`cfg80211`, `mac80211`, `wlcore`, `wlcore_sdio`, `wl12xx`, `libarc4`, `crc7`**), `pbplay`, `pbmix`, **dropbear**, **`/usr/bin/iw`**, **`/usr/sbin/wpa_{supplicant,cli,passphrase}`**, and **`/lib/firmware/ti-connectivity/{wl128x-fw-4-sr.bin,wl128x-fw-5-sr.bin,wl128x-nvs.bin}`** plus the signed **`/lib/firmware/regulatory.db`/`.p7s`**; `rcS` loads the wireless stack in order.  Earlier images: `a148c362…`, `7e067560…` and `88084b78…` (session 51/50), `a4e76b18…` (sessions 48–49, 27 modules), `…-audio-oss-linout.ext4` (`af5a0a20…`, session 48) and `0342ab06…` (session 47).  The modules in any of them can be checked by extracting them with `debugfs` (trap 163) |
| TabletOS / OS | **NOT running** — the unit is on our Linux image `0x9712A086` (`0fca:8010`, console `pb> `, **ssh on `169.254.0.1`**).  TabletOS 2.1.0.1917 returns by flashing `local/work/ifs-shim-sealed.ifs` at block `0x825`; its door is also `169.254.0.1`, so use `PB_LINK=door` for `tools/pbssh.sh` there |
| device-side files | **persistent on the UFS**: `/accounts/devuser/startup.sh` and our `.ssh/authorized_keys` (root ssh); **session 49 added `/var/ti1283/{firmware.bin,tiwlan.ini}`** on `hd3` — the reference WLAN's own staging directory (report 217 §2.3; delete it for a clean oracle).  `memdump3`/`memw32`/`pbi2c` are **not** on the UFS: push them to `/tmp` after every reboot (trap 177) |

The factory boot budget is **self-maintaining in bench use**: every successful
factory/CRC boot spends one unit, and the booted OS puts it back — our Linux
`/init` runs `pbrpmb topup 0x1E`, and every TabletOS boot tops the record to
`0x32`.  **Since session 15 our images no longer reboot themselves**, so a test
boot costs one unit, is replenished at boot, and stays up until asked; it is not
a reason to avoid an experiment.  The hard hazards remain: `0` and `> 0x32` do
not fall back to the product path — they hard-lock the unit (`SAFETY.md` §2) —
and a failed/half-formed write or a flat pack is still a brick risk.

## Blockers in play

**The audio blocker is closed, speakers included, and the diagnostics are
retired (sessions 47/48, reports 214/215).**
`pbplay`'s 440 Hz tone is audible in both ears on headphones and in both
speakers at 48 kHz ±1 % on the working image (`0xBAFD9B3B` + rootfs
`a4e76b18…`), re-verified after the cleanup at DAC1 volume 40.  The data path
is session 46's (AIF1 on **McBSP3**; two links `mcbsp2 + wm8994-aif1` clocks,
`mcbsp3 + wm8994-aif2` data; the `omap-mcbsp` channel-layout guard fixed the
right channel; the ABE is not needed).  The speakers needed the codec's **line
outputs** (not class-D), `spk` driven **low**, and GPIO1_15 (the `audio_boost`
enable) **high** — all three in the driver/DTS.  **Remaining audio items: none
required** — the optional **MCLK1 probe** (trap 165) stays optional, and the
`audio_boost` net is behaviourally confirmed but not schematic-confirmed.

**The WL1283 WLAN is WORKING and stable (session 51, report 232).**  The session-50
recovery loop was the chip's **ELP** sleep, not a firmware/chip mismatch, the
block size or the calibration: the FW-status read (CMD53 to `0x1efc0`, 84 bytes,
byte mode) times out only after the chip has slept, and
`sta_sleep_auth = WL1271_PSM_CAM` in `wl12xx_default_conf` keeps it awake —
`wlan0` now comes up and stays up across a real flash and reboot.  The
enumeration blocker was the TWL6030 `CLK32KG` 32.768 kHz SLOW_CLK being OFF
(report 230); the reference's own driver, loaded as `/base/scripts/wifi.sh`
loads it, runs the chip live on this unit (report 217), so the chip and its
rails were never in doubt.  **Remaining (the WLAN is up but not yet useful):** a
userspace supplicant (`iw`/`wpa_supplicant`) + `regulatory.db` for
scan/associate, the per-unit calibration NVS from `hd5` (report 197), and the
BT half (`BT_EN` = gpio 55, held low while the WLAN works — report 216).
**Open:** why the ELP wake is not honoured (report 232 §6).

**The per-unit WLAN calibration is in the QNX `hd5`
partition** (report 197, session 50) — a TI WL128x radio-parameters INI + a
binary blob, and the assigned MAC is in NVRAM `deviceinfo`
(`14:74:11:8f:9c:c3`, patched into the generic NVS).  It is the source for
`wl128x-nvs.bin` **after** enumeration; it was never the `-110`.

**The touch is finished** (I²C2 `0x48072000` = `i2c-1` at `0x65`,
`playbook-munin.c`, `input0`/`event0`; reports 193/194/198, traps 138–141).
The 1917 `scaling.conf` calibration maps the report to 1024×600 and the
GPIO2_3 edge IRQ works (`irq 110`); the power/reset lines are still not driven
by the driver.  It survived this session's warm boot; only cold boots need the
GPIO pulse.

**Media state: region 0 = `0x9712A086`** (kernel `7.3.3` **#60**,
`0x939A74` B, the CLK32KG fix + the wl1283 DT child and pwrseq — unchanged in
session 51); region 1 = the WLAN rootfs **`f159a221…`** (67,108,864 B, 34
modules + **both** `wl128x` SR firmware files + the CAM/4-sr `wl12xx.ko` + `iw`/
`wpa_supplicant`/`wpa_cli`/`passphrase` + the signed `regulatory.db`;
`local/images/rootfs-region1-wl1283-scan.ext4`).  The unit is on Linux,
not TabletOS.  The way back to TabletOS is the 1917 IFS
(`local/work/ifs-shim-sealed.ifs`, block `0x825`); the previous Linux images
`0xF64A680D`, `0x53F7F97B`, `0x69CDAC94`, `0x17C2E119`, `0xBAFD9B3B`,
`0x91E22899`, `0xAD85DF0F`, `0xF8BADFF3`, `0xE5E2AEAF`,
`0x58F85337`, `0x47ED0450`, `0x5A04ACD9`, `0x92458D00`, `0x7A5DD941`,
`0x36597040`, `0xB8830701`, `0xF97CD9C7`, `0x3B2ECFED`, `0x0DC36A11`,
`0xE36C679A` and `0x04AF1433` remain.
NVRAM and the MCT remain unwritable, and an agent never writes RPMB
(`SAFETY.md` §6).

## What is next

1. **WL1283 — the WLAN WORKS: it scans (session 51, reports 232–235).**  Two
   blockers closed: the `-110` loop was the chip's ELP sleep (now CAM), and the
   never-completing scan was the **5-sr firmware** — `wl128x-fw-4-sr.bin`
   (`Rev 7.3.5.0.98`) scans (17–21 BSS, both bands), so the floor is 7.3.5.0
   and `WL128X_FW_NAME_SINGLE` names 4-sr.  **Next, in order:**
   (a) **association** — station config via `wpa_passphrase`/`wpa_supplicant`
   (both in the image), then `wpa_cli status`; that is also where real
   throughput gets measured;
   (b) the per-unit **NVS** (the 468-byte NVRAM record at raw `0x152cc`) for TX
   power — `local/work/wl128x-nvs-perunit.bin`, spliced but not yet shipped;
   (c) the **BT** bring-up (`BT_EN` = gpio 55, UART2, linux-firmware's
   `TIInit_7.2.31.bts`; mainline 7.3.3 has no `btwilink`/`ti-st`);
   (d) open questions, neither blocking use: *why* 5-sr panics on scan (235 §3)
   and *why* the ELP wake is not honoured (232 §6).
   **Do not** re-run the session-49/50 negatives, do **not** use
   `IRQ_TYPE_LEVEL_LOW` for WL_IRQ (trap 238), do **not** use
   `wlcore_sdio dump=1` to measure a timing fault (trap 239), and do **not**
   switch back to 5-sr without reproducing the panic (trap 243).
2. **The WM8994 audio — DONE and the diagnostics retired (sessions 47/48,
   reports 214/215).**  Stereo 440 Hz at 48 kHz on headphones and speakers,
   re-verified at DAC1 volume 40 after the cleanup; image `0x9712A086` +
   rootfs `f159a221…`.  The only items left are optional: the **MCLK1 probe**
   (trap 165) and identifying what the GPIO1_15 `audio_boost` enable supplies if
   a schematic appears.  The ABE is not needed (DECISIONS §58/§59).
3. **Touch: DONE (session 38, report 198) but down after a cold boot.**  The
   calibration and the IRQ are in the tree and proven; after a cold boot the
   controller does not boot (`0x65` NAKs) until the power/reset GPIOs
   (GPIO1_14/GPIO4_8) are pulsed, which the driver still does not drive.  A
   warm reset keeps it up (session 42).
4. **The `wlcore@2` child is in the DTS** (`ti,wl1283`, `reg = <2>`, WL_IRQ,
   26 MHz ref/TCXO), the stack probes, and the firmware stays up; the only
   remaining node-level item is the **per-unit `wl128x-nvs.bin`** from the `hd5`
   calibration (report 197), to replace the generic NVS.
5. **The unit is on Linux `0x9712A086`** (region 0, kernel #60) + the WLAN
   rootfs (`f159a221…`, region 1), reachable over **ssh on `169.254.0.1`** as
   well as the ACM console.  `rcS` loads the wireless stack but does **not**
   raise the link: bring `wlan0` up with `ip link set wlan0 up`.  To return to
   TabletOS, flash the 1917 IFS at block `0x825`; the previous Linux images
   remain.
6. **Use ssh, not the console, for anything substantial.**  `tools/pbssh.sh
   'cmd'` (root, key-only) runs at 2.3 MB/s and does not drop bytes; the ACM
   console is for recovery and for when the network stack is broken.  The same
   script serves the QNX door with `PB_LINK=door`.

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
