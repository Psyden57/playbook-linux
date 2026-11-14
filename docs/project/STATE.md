# Current state

## Current state — 2026-11-14 (session 50)

**Both of session-49's open tests were run and both are negative: the card is not
"too slow", and the BT core is not a usable die-liveness oracle.**  What is left
is a **live capture of the reference's MMCHS5 register block** at `ti0`-up (the
one class report 218 never captured) or the **QNX-in-Linux harness** — not
another timing sweep and not the BT half.

* **The card is not timing-limited (report 229).**  `OMAP_MMC_MIN_CLOCK` was
  lowered `400000 → 100000`, which opens `mmc_rescan`'s ladder; the card was then
  tried at **400, 300, 200 and 100 kHz** and every attempt ended
  `attach_sdio no` with `STAT = 0x00018000` — **`CTO` only** (`CCRC = CEB =
  CIE = 0`, `RSP0 = 0`, `PSTATE` `CLEV = 1`).  At 100 kHz the `CTO` window is 64
  card clocks = **640 µs**, four times the old 160 µs.  A card that needs more
  than that is not a plausible WL1283; the "slower than the window" hypothesis is
  dead.
* **BT is silent even with `BT_EN` driven (report 228).**  `BT_EN` = gpio 55 =
  `GPIO2_23`, pad `0x4A10007C` bits 31:16.  It was driven low → 50 ms → high
  through sysfs (pad reads `0x00030003`, so it was already mux 3; `GPIO2_DATAOUT`
  bit 23 goes `0`→`1`), and the UART path was verified (`uart2_rx` pad `0x4118`
  has `INPUT_EN`, `uart2_tx` `0x0000`, `UART2 LSR = 0x60` = THRE/TEMT, i.e. the
  `HCI_Reset` left the UART).  The chip sends nothing back in 2 s, on three
  pulses.  **This does not prove the die is unpowered:** the oracle proves the
  chip runs under the reference, the reference's *BT* half was only ever decoded
  statically (report 216), and the 32 kHz SLOW_CLK has never been read.  It only
  shows BT cannot stand in for a card response.
* **Correction: neither `0x8C` nor `0x60` is `CLK32KG`** (KNOWN-ISSUES 3.31).
  `0x8C`/`0x8E` is VAUX3 (`clk-twl.c`'s `0x8C` table is TWL6032-only), and
  `0x60` is **VCORE3's `CFG_STATE`** (`vcore3` base `0x5E` in the QNX regulator
  table), so the live `07/05/55` is VDD3.  The datasheet (local
  `hardware/twl6030.pdf`, `SWCS045C`) says `CLK32KG` is a software resource that
  is on from the startup sequence, but its address table is not in the
  extractable text — **do not claim the 32 kHz was read**.
* **Both experiments were reverted and proven.**  `drivers/mmc/host/omap_hsmmc.c`
  and `drivers/mmc/core/core.c` are byte-identical to pristine 7.3.3 + the
  shipped patch (regenerated with `git apply --include=…` and `cmp`), and the
  board DTS is byte-identical to `kernel/source/`.  `rootfs/hci.c` keeps the
  BT_EN pulse + UART self-check as a durable tool change; the region-1 image
  still carries the session-29 `/bin/pbhci` (the probe was pushed to `/tmp`).

**Bench (session 50, measured).**  Unit on our Linux image **`0x17C2E119`**
(region 0, `0x9398B4` B, kernel `7.3.3` **#59** — the session-49 tree rebuilt
after the two experiments were reverted); region 1 = the ssh rootfs
**`a4e76b18…`** (unchanged, 67,108,864 B, 27 modules).  `0fca:8010`, console
`pb> `, **ssh on `169.254.0.1`**.  Battery `Full`, 4.156 V.  No NVRAM/MCT/RPMB
write; no restore needed.

## Previous state — 2026-11-14 (session 49)

**The WL1283 blocker is measured from both sides, the host program is excluded,
and the "silent card" measurement itself was found to be invalid and fixed.**
Session 49 did what DECISIONS §61 prescribes: it booted the minimal 1917 oracle,
loaded the reference's **own** WLAN driver exactly as `/base/scripts/wifi.sh`
does, and watched it **bring the chip up live**; replayed the reference's entire
MMCHS5 program and its WL_EN power-on ordering on Linux (both negative); compared
stock TabletOS at `ti0`-up against our boot (negative); ran a full RE of the
boot path (negative); tested the one clock-domain difference it found
(negative); and then, on the operator's challenge, found that the way "silence"
had been measured since session 33 was wrong.  Corrected, the measurement is a
stronger negative — see the last bullet of this list.

* **The measurement was flawed and is now correct.**  `WL_IRQ` (GPIO2_21) is
  **not** a liveness signal — the oracle's own chip reads `0x08000018`
  (bit 21 = 0) *before* its driver runs and only `0x08200018` after firmware, so
  every "the chip is not up" claim compared our pre-driver state with the
  reference's post-firmware state.  With `PBX`/`PBIRQ` restored (archived at
  `kernel/archive/wl1283/measurement-instrumentation-2026-11-14.patch`) the real
  reading is: at the instant of `CMD52`/`CMD5` the bus is correct
  (`SYSCTL=000e1e07` → `CEN=1`, `ICE=1`, `CLKD=120` = 400 kHz;
  `HCTL=00000b00` → `SDBP=1`, `SDVS=5` = 1.8 V; `CON=00000600`), `STAT=00018000`
  decodes as **`CTO` only** (`CCRC=CEB=CIE=0`, `RSP0=0`, `PSTATE` `CLEV=1`), and
  `CMD0` succeeds.  `CON=0x85` must **not** be copied: its `HR` bit makes the core
  generate a response *instead of* the command and hangs the rescan (trap 233).
  Report 227.

* **The reference works on this unit, live** (not a recollection):
  `sdioAdapt_ConnectBus: Send commands sequence: 0, 5, 3, 7` → firmware download
  → `Firmware running` / `Firmware Version: Rev 7.1.4.10.77` → `ti0`
  **UP,RUNNING**, MAC `14:74:11:8f:9c:c3`, and on the pins WL_EN high **and
  WL_IRQ high** (`GPIO2_DATAIN = 0x08200018`, bit 21 = 1).
* **The reference's load path, which the earlier plan had wrong**: stage
  `firmware.bin` in `/var/ti1283` (the minimal image's `/base` has no firmware
  directory), then `mount -Tio-pkt -o nvram,firmdir=/var/ti1283/
  /lib/dll/devnp-ti1283.so` and **`ifconfig ti0 up`**.  `-o nvram` is what
  supplies the NVS (468 bytes from NVRAM); no `nvs_map.bin` exists anywhere in
  the image.  The driver loads the **firmware before** it touches the bus, so a
  missing file looks exactly like a card that will not enumerate.
* **Our Linux image on the same unit is register-identical apart from one bit**:
  `GPIO2_OE = 0xCE2ECDFF`, `DATAOUT = 0x31413200` (WL_EN high), pads
  `0x07C = 0x00030003`, `0x148/0x14C/0x150 = 0x01180118` — and
  `GPIO2_DATAIN = 0x01000010`, i.e. **WL_IRQ never goes high**, so the chip is
  not up.  Replaying the reference's controller program (`ti,playbook-ref-init`:
  `SYSCONFIG`, `SOFTRESET`, `SRA`, `CAPA|VS18`, `SDBP`, `IE = 0x317f0137`,
  `CON = 0x85`, early DTW) and the reference's EN ordering (driver-owned EN,
  low → 20 ms → high → 120 ms, after the controller is up) both left it
  silent.  **Both experiments were reverted**; the tree is byte-identical to
  pristine + the shipped patch again, per file.
* **The BT half has its own enable, and it is not the WLAN's**: `BT_EN` =
  **gpio 55 = GPIO2_23** (pad `0x4A10007C` bits 31:16), driven low → 50 ms →
  high by `btp-btmgr`, and it is held **low while the WLAN works**.  So it
  explains the silent `HCI_Reset` and nothing about the SDIO card (report 216,
  new `docs/reference/qnx-hwio/gpio-table-winchester.csv`).
* **What is left** is a board line the reference's *boot* asserts and its WLAN
  driver does not, or the chip-side pins (VBAT/TCXO/SLOW_CLK, unmeasurable,
  trap 165).  The next evidence is a **stock-TabletOS** capture at the moment
  `ti0` comes up, diffed against our Linux boot — the oracle cannot stand in
  for it (it needed the firmware and NVS supplied by hand).

**Also this session, from the bench:** leaving MMCHS5 in the reference's
command-phase state **wedges `reboot -f`** (trap 224; `sysrq b` recovers), and
`slay -f io-pkt-v6-hc` on the oracle kills the stack that carries `rndis0`
(trap 221 — it cost a power cycle).

**Bench (session 49, measured).** Unit on our Linux image **`0xBAFD9B3B`**
(region 0, `0x93A384` B, kernel `7.3.3` **#57**, the session-48 tree plus
report 220's GPIO1 pad fix); region 1 = rootfs **`a4e76b18…`** (unchanged).
`0fca:8010`, console `pb> `, **ssh on `169.254.0.1`**.  `BOOTCTR_FCTRY` read
`0x29` at the last boot (`>= threshold 0x1e, no write`).
The oracle is not on media; its `hd3` now carries `/var/ti1283/{firmware.bin,
tiwlan.ini}` (report 217 §2.3 — delete for a clean oracle).

## Earlier state — 2026-11-14 (session 48)

**The unit has an ssh shell over USB.**  The gadget now carries an **RNDIS**
network function alongside the CDC-ACM console, `usb0` comes up at
`169.254.0.1/16` from the region-1 `rcS`, and **dropbear** (key-only, static
ARM, 968 KB stripped) listens there: `tools/pbssh.sh 'cmd'` runs a command and
`tools/pbssh.sh` with no arguments gives an interactive `root@playbook:~#`.
Measured **2.3 MB/s** host→device, against the ACM
link's 134 KB/s that drops bytes under load (trap 51) — this is the difference
between a debug channel and a usable one.  The ACM console is unchanged and
still bound, so it remains the fallback.  DECISIONS §62.

* **RNDIS, not CDC-ECM**, because Windows has an in-box RNDIS driver and no
  in-box ECM class driver, and because **the reference firmware itself
  presents RNDIS** (`0fca:8012`, "RIM Network Device") — the path this
  project's TabletOS bench has used since session 20.
* **The host side needed one change**: the composite gadget's network
  interface is `enp8s0f3u1i2`, not the `enp8s0f3u1` the `pb-rndis`
  NetworkManager profile was pinned to; rebinding it (`nmcli connection modify
  pb-rndis connection.interface-name enp8s0f3u1i2`) brought the link up with
  the address it already had.
* **`tools/pbssh.sh` now serves both OSes.**  `PB_LINK=usb` (the default when
  nothing answers on the door's qconn 9090) omits the legacy
  algorithm options that the 2014 QNX sshd needs and dropbear does not.
  `PB_LINK=door` keeps the old behaviour.

**Also this session: the audio diagnostics were retired** — the machine
driver's `pcm:` DMA-ring readback and `omap-mcbsp`'s `live N:` dump are out of
the tree — and **the kernel patch gained a file every export since session 41
had missed**: `sound/soc/ti/omap-mcbsp-priv.h` declares the
`active_rate`/`active_width` fields `omap-mcbsp.c` reads, so the patch could
not compile from pristine.  The export is **56 files / 8 583 lines**, sha256
`455b45d8…`, verified 0 rejects / 56/56 byte-identical (session-49 export, tra
211).

**Re-verified after both changes (operator, DAC1 volume 40, tones a few
seconds each):**

* **Speakers** — `repair: jack out (speaker)` → `done, FLL1 locked`; both PCMs
  `RUNNING`; `XCR2 = 0x8041`; `spk` out **lo** and `boost` out **hi** during
  the stream, `spk` back **hi** after; the data link wrote its full byte count.
* **Headphones** (jack in) — `repair: jack in (headphone)` → `done, FLL1 locked`;
  both PCMs `RUNNING`; **`spk` stays high** (amp off), the correct state for
  the headphone route; the data link wrote its full 770 048 bytes; the
  operator heard the tone **in both ears**.
* **The audio path is unchanged** — session 46's two ASoC links (`mcbsp2 +
  wm8994-aif1` clocks, `mcbsp3 + wm8994-aif2` data), the codec as the AIF1
  slave with FLL1 locked from our BCLK, the `omap-mcbsp` channel-layout guard
  keeping `XCR2 = 0x8041`, and no ABE (DECISIONS §58/§59).  The speaker route
  is session 47's (line outputs, `spk` active low, GPIO1_15 high).

**Bench (session 48, measured).** Unit on our Linux image **`0x12C8C320`**
(region 0, `0x939D24` B, kernel `7.3.3` **#45** — the RNDIS/dropbear
initramfs); region 1 = rootfs **`a4e76b18…`** (67,108,864 B, 27 modules,
dropbear + host keys + `authorized_keys`).  `0fca:8010`, console `pb> ` on
`ttyACM0`, and **ssh on `169.254.0.1` over `enp8s0f3u1i2`**.  `BOOTCTR_FCTRY`
read **0x28** at the last boot (`0x28 >= threshold 0x1e, no write`).  The oracle is
not on media; flash `local/work/ifs-shim-sealed.ifs` at `0x825` to boot it.  To
stock TabletOS, flash **two components**: `local/work/ifs-shim-sealed.ifs` at
block `0x825` and `local/work/rcfs-1917.rcfs` at `0x8D5` — `wayback.signed`
will not boot this armed unit (its IFS is `os_type` 4 and its RCFS has no root
hook; trap 226).

**The magic-`pkexec` rule still holds.**  A flash re-enumerates the ACM gadget
and the tty comes back `root:uucp 0660`; `tools/99-playbook-cdc-acm.rules`
makes it `0666` automatically, and `pkexec chmod 666 /dev/ttyACM0` remains the
documented manual path — wait for the operator rather than inventing a
workaround.

* **The kernel patch was re-exported (session 49)**: **56 files / 8 583 lines**,
  sha256 `455b45d8…`, 0 rejects / 56/56 byte-identical.  It adds the report-220
  board-DTS GPIO1 pad fix and `arch/arm/Kconfig.debug`, which the tree walk
  showed had been missing from **every** export since session 30 (trap 211 class:
  a byte-compare cannot see a file absent from both sides).
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
| **WL1283 (MMC5)** | **blocked; host program, timing and the BT lead all excluded — next is a live reference MMCHS5 capture** | **Session 50 (reports 228/229):** the identification ladder (400/300/200/100 kHz) ends in `CTO` only (`STAT=0x00018000`, `RSP0=0`) at every frequency, so the card is not "slower than 160 µs" (100 kHz = 640 µs); and `HCI_Reset` gets no reply with `BT_EN` genuinely driven and the UART path verified, so BT is not a liveness oracle (it does not prove the die is off — the reference's BT half was never shown live, and the 32 kHz has still never been read: `0x8C` is VAUX3 and `0x60` is VCORE3, KNOWN-ISSUES 3.31).  **Session 49 (reports 216/217):** the reference driver, loaded as `wifi.sh` does (`-o nvram`, firmware staged in `/var/ti1283`, then `ifconfig ti0 up`), **enumerates and runs the chip live on the oracle** (`Send commands sequence: 0, 5, 3, 7`, firmware `Rev 7.1.4.10.77`, `ti0` RUNNING with MAC `14:74:11:8f:9c:c3`, `GPIO2_DATAIN` bit 21 = 1).  The same unit under our Linux image is register-identical apart from that bit (WL_EN high, pads `0x0118`/`0x0003`, `OE = 0xCE2ECDFF`) and the chip stays down.  Replaying the reference's whole MMCHS5 program and its driver-owned WL_EN power-on order changed nothing.  The BT half's enable is `BT_EN` = gpio 55 = `GPIO2_23`, driven by `btp-btmgr` and **low** while the WLAN works (report 216), so it is unrelated.  Earlier: session 37's 1917-truth `&mmc5` (`0x0DC36A11`) probes but returns `attach_sdio no` (report 196); pads/WL_EN/IRQ/rails/CM all matched QNX; H1 (QNX's CMD0→CMD5 sequence at 24 MHz, `0x29784F48`) negative.  **Closes when** the chip enumerates under Linux and `wl12xx` probes with the `hd5` NVS.  Remaining: a board line the reference's *boot* asserts (stock-TabletOS capture at `ti0`-up time) or the chip-side pins (trap 165) |
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
| Kernel patch + config | **7.3.3 (session 49 export)** | `kernel/patches/0001-playbook-winchester-7.3.3.patch` is **8 583 lines / 56 files**, sha256 `455b45d8…`, applied to pristine 7.3.3 targets with 0 rejects and **56/56 files byte-identical**.  Session 49 adds the report-220 board-DTS GPIO1 pad fix and `arch/arm/Kconfig.debug` (missing from every export since session 30 — the tree walk found it, trap 211 class).  Session 48 retires the two diagnostic blocks and adds `sound/soc/ti/omap-mcbsp-priv.h`, which every export since session 41 had omitted although `omap-mcbsp.c` reads the fields it declares (trap 211 — the patch could not compile from pristine until now).  Session 47 added the speaker path (line-output route, `spk`/`boost` GPIOs, the `disp-en-12-hog` removal); session 46 the `&l4_abe` ABE-refclk assignment, the E5 clock-role change, the McBSP3 two-link card and the `omap-mcbsp` channel-layout guard; session 39 the board machine driver (`sound/soc/ti/playbook-wm8994.c`, Kconfig/Makefile), the `wm8994-core.c` runtime-PM quirk and the board `sound` node/codec clocks, and it restored `drivers/input/touchscreen/{Kconfig,Makefile}`, which the 48-file export had silently omitted (trap 154).  Config: `kernel/winchester-7.3.3.config` (includes `CONFIG_CHARGER_ISL9519=y`, `CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`, `CONFIG_KERNEL_XZ=y`, `CONFIG_SND_PCM_OSS=m`, `CONFIG_SND_MIXER_OSS=m`).  The 6.15.11 patch (`5 703 lines / 34 files`) stays as the bisect reference; it is **incomplete** — it omits `cm3217.c`, `leds-fan5702.c`, `pbmon-pair.h`, `pbmon-page.h` and their Kconfig/Makefile lines (trap 106) |
| **ISL9519 charger** | **WORKING under Linux (session 31)** | I²C1 `0x48070000`, slave `0x09`; VendorID `0x0049`, DeviceID `0x0003`.  `isl9519.c` (built-in, DTS `charger@9`) sets the four registers in RIM's order with the masked fields and refreshes every 5 s.  Measured **+358 → +397 mA**, 3.775 → 3.794 V, capacity 8 → 9 %, `Charging`; still on at 197 s (watchdog).  Registers read back `0x14=0x0280`, `0x15=0x1070`, `0x3E=0x0D00`, `0x3F=0x0180`.  Report 183 |
| TWL6030 BCI | **probes after the session-31 fix; NOT the board's charger** | `twl6030_charger` probes with the `simple-battery` profile and sets `CONTROLLER_CTRL1=0x10`, `VOREG=0x21`, `VICHRG=0x08`, `CINLIMIT=0x09`, but the stock OS leaves the whole block at bootloader reset values — it charges through the ISL9519.  The BCI node/modules are inert; `twl6030_usb` reports `ONLINE=1`, VBUS 4.81–4.91 V |

## Current images

The unit table below is authoritative for what is on media.  Keep the current
image, one known-good fallback, and anything an open contradiction still needs;
the durable record is the CRC and what it demonstrated, not the file.

| image | base | role |
|---|---|---|
| `0x17C2E119` | 7.3.3 | **current region 0** (session 50: the session-49 tree rebuilt after the two experiments were reverted; kernel `#59`, `0x9398B4` B) |
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
| region-0 IFS on media | **the session-50 Linux image `0x17C2E119`** (`0x9398B4`, kernel `7.3.3` #59, the session-49 tree rebuilt after the two experiments were reverted; `boot/chainloader/kernel.img`), region 0.  The way back is the 1917 IFS (`local/work/ifs-shim-sealed.ifs`, 7,900,548 B, sha256 `b52195bb…`); the compressed `local/work/ifs1917-compressed.ifs` (sha256 `f14db207…`) is the alternative.  Earlier Linux images (region 0 only, block `0x825`): `0xBAFD9B3B` (session 49), `0x91E22899` (session 50 experiment), `0x12C8C320` (session 48), `0xAD85DF0F` (session 47), `0xF8BADFF3` (session 46), `0x36597040` (session 42), `0xB43F169B` (session 44), `0xB8830701` (session 41), `0xF97CD9C7` (session 39), `0x3B2ECFED` (session 38), `0x50842767`, `0x82FC0D35`, the Task-3 `0x0DC36A11`, `0x05819994`, the known-good `0xE36C679A` (`local/images/kernel-0xE36C679A-wdt58s.img`, 0x91A748, sha256 `bdbaf5fc…`), and the touch `0x04AF1433` |
| region-0 RCFS (`/base`) | **now the full 1917 RCFS at `0xC00000`** (`local/work/rcfs-1917.rcfs`, the modified one with the `/accounts/devuser/startup.sh` hook, sha256 `c4ab6e34…`): session 49 wrote it to recover root on a stock-TabletOS boot, and it sits **outside** the 7.3.3 test image (which ends at `0x93A1DC`).  The pruned oracle RCFS is no longer on media — re-flash `local/work/minimal/setB.signed` for the minimal oracle (trap 227).  Not readable as `/base` without the 1917 IFS |
| UFS (`/`) | **the 1917 vendor `.5.ufs` with `autorun::0`** (the session-27 set); the BB10 `.0.4.ufs` is not on media.  (Not re-hashed this session) |
| `/accounts/devuser/startup.sh` | the 1917 UFS carries the operator's script; it last ran during the session-35 S3/S2 TabletOS bench and topped `BOOTCTR_FCTRY` back to `0x32` |
| `BOOTCTR_FCTRY` | **self-replenishing** — the Linux `/init` tops it up at ≤`0x1e` and a TabletOS boot writes `0x32`; read it as a health check (`SAFETY.md` §2) |
| region-1 Linux root | **present** at raw `0x29000000` (region-1 +16 MiB, block `0x2915`): 64 MiB ext4, no journal, sha256 **`a4e76b18…`** (`local/images/rootfs-region1-rndis-ssh.ext4`), **27 modules under `/lib/modules/7.3.3`** (the sensor/charger/audio set plus `snd-pcm-oss`, `snd-mixer-oss`, `snd-soc-playbook-wm8994`), `pbplay`, `pbmix` and **dropbear**; the initramfs mounts it **read-write** and chroots into it.  Earlier images: `…-audio-oss-linout.ext4` (`af5a0a20…`, session 48) and `0342ab06…` (session 47).  The modules in any of them can be checked by extracting them with `debugfs` (trap 163) |
| TabletOS / OS | **NOT running** — the unit is on our Linux image `0x17C2E119` (`0fca:8010`, console `pb> `, **ssh on `169.254.0.1`**).  TabletOS 2.1.0.1917 returns by flashing `local/work/ifs-shim-sealed.ifs` at block `0x825`; its door is also `169.254.0.1`, so use `PB_LINK=door` for `tools/pbssh.sh` there |
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

**The WL1283 is blocked, and session 50 closed both hypotheses session 49 left
open: the card is not "too slow", and the BT half cannot tell us whether the die
is running (session 50, reports 228/229).**  The reference's own driver, loaded
the way `/base/scripts/wifi.sh` loads it, **enumerates and runs the chip on this
unit, live** (`Send commands sequence: 0, 5, 3, 7` → firmware → `ti0` RUNNING).
Under Linux every readable precondition matches, and five independent surfaces
are negative: the reference's MMCHS5 program (replayed), its WL_EN power-on order
(replayed), the readable board state of stock TabletOS at `ti0`-up (report 218),
the whole boot path's writes (reports 221–226), and the one clock-domain
difference (report 225).  The "silence" instrument was itself wrong until report
227 (`WL_IRQ` is not a liveness signal; `STAT` was never decoded; the bus was
never read at command time); corrected, the card returns `CTO` only on a bus that
is powered and clocked at 400 kHz/1.8 V.  **Session 50:** the identification
ladder now runs all the way to 100 kHz and the answer is still `CTO` only
(`RSP0 = 0`), so "slower than 160 µs" is **dead**; and `HCI_Reset` gets no reply
with `BT_EN` genuinely driven and the UART path verified, so **BT is not a
liveness oracle** — which does not prove the die is off, because the oracle
proves the chip runs and the reference's BT half was never shown live.  What is
left is a **live capture of the reference's MMCHS5 block** at `ti0`-up (never
captured — report 218's other unmeasured class) or the **QNX-in-Linux harness**.
The chip-side pins (VBAT/TCXO/SLOW_CLK) remain unmeasurable (trap 165), and the
32 kHz has still never been read (KNOWN-ISSUES 3.31).
The BT half's enable (`BT_EN` = gpio 55) is separate and is held low while the
WLAN works (report 216).

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

**Media state: region 0 = `0x17C2E119`** (kernel `7.3.3` **#59**,
`0x9398B4` B, the session-49 tree rebuilt after the session-50 experiments were
reverted); region 1 = the ssh rootfs
`a4e76b18…` (67,108,864 B, 27 modules;
`local/images/rootfs-region1-rndis-ssh.ext4`).  The unit is on Linux,
not TabletOS.  The way back to TabletOS is the 1917 IFS
(`local/work/ifs-shim-sealed.ifs`, block `0x825`); the previous Linux images
`0xBAFD9B3B`, `0x91E22899`, `0xAD85DF0F`, `0xF8BADFF3`, `0xE5E2AEAF`,
`0x58F85337`, `0x47ED0450`, `0x5A04ACD9`, `0x92458D00`, `0x7A5DD941`,
`0x36597040`, `0xB8830701`, `0xF97CD9C7`, `0x3B2ECFED`, `0x0DC36A11`,
`0xE36C679A` and `0x04AF1433` remain.
NVRAM and the MCT remain unwritable, and an agent never writes RPMB
(`SAFETY.md` §6).

## What is next

1. **WL1283 — the host program, the timing and the BT lead are all excluded;
   the next evidence is a live reference capture (session 50, reports 228/229).**
   The driver's own writes, the WL_EN ordering, the whole readable board state
   (stock TabletOS at `ti0`-up vs our boot), the entire boot path's writes, the
   one clock-domain difference, the BT-core HCI probe and the identification
   clock sweep are all measured and negative.  **Session 50 settled the two
   tests session 49 left:** (a) the BT core answers no `HCI_Reset` even with
   `BT_EN` genuinely driven and the UART path verified — so BT cannot serve as a
   die-liveness oracle, but because the *reference's* BT half was never shown
   live and the 32 kHz has never been read, it does **not** prove the die is
   unpowered; (b) the rescan ladder tried **400/300/200/100 kHz** and every
   frequency returned `CTO` only (`RSP0 = 0`), so the card is **not** "slower
   than 160 µs" (100 kHz = 640 µs).
   **Next, in order:**
   (a) **a live capture of the reference's MMCHS5 register block** at the moment
   `ti0` comes up — report 218 lists that block as one of the two classes it
   never captured, and it is the only host-side surface still unmeasured;
   (b) the **QNX-in-Linux harness** (run `devnp-ti1283.so`'s bring-up under a
   shim so its exact sequence executes) — now more valuable than before, because
   `CON=0x85` showed the reconciliation can be subtly wrong;
   (c) the **32 kHz SLOW_CLK** only once its register address is taken from a
   register map (KNOWN-ISSUES 3.31: `0x8C` is VAUX3, `0x60` is VCORE3 — neither
   is `CLK32KG`), or a probe/scope (trap 165).
   **Do not** re-run the readable-register diffs, the two replays, the boot-path
   RE, the domain test, the BT probe or the clock sweep.  The post-enumeration
   work (`wl128x-nvs.bin` from the `hd5` calibration, the `wlcore@2` child) is
   ready for whenever the card answers.
2. **The WM8994 audio — DONE and the diagnostics retired (sessions 47/48,
   reports 214/215).**  Stereo 440 Hz at 48 kHz on headphones and speakers,
   re-verified at DAC1 volume 40 after the cleanup; image `0x17C2E119` +
   rootfs `a4e76b18…`.  The only items left are optional: the **MCLK1 probe**
   (trap 165) and identifying what the GPIO1_15 `audio_boost` enable supplies if
   a schematic appears.  The ABE is not needed (DECISIONS §58/§59).
3. **Touch: DONE (session 38, report 198) but down after a cold boot.**  The
   calibration and the IRQ are in the tree and proven; after a cold boot the
   controller does not boot (`0x65` NAKs) until the power/reset GPIOs
   (GPIO1_14/GPIO4_8) are pulsed, which the driver still does not drive.  A
   warm reset keeps it up (session 42).
4. **Build `wl128x-nvs.bin` from the hd5 calibration** (report 197), and add the
   `wlcore@2` child the mainline binding requires (`ti,wl1283`, `reg = <2>`,
   `interrupts` for WL_IRQ, `ref-clock-frequency`/`tcxo-clock-frequency` 26 MHz),
   for the post-enumeration `wl12xx` path — report 195 §3.6, HANDOFF §0b.
5. **The unit is on Linux `0x17C2E119`** (region 0, kernel #59) + the ssh rootfs
   (`a4e76b18…`, region 1), reachable over **ssh on `169.254.0.1`** as well as
   the ACM console.  To return to TabletOS, flash the
   1917 IFS at block `0x825`; the previous Linux images remain.
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
