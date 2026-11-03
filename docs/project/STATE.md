# Current state

## Current state — 2026-11-03

**The modified 2.1.0.1917 image is on the reference and the `/base` hook
works.**  The unit runs PlayBook OS **2.1.0.1917** from:
* **our uncompressed IFS** — the stock 1917 tree with `proc/boot/rfs_validator`
  replaced by the 8,759-byte shim; `0x788D84` B, `os_type` 6, CRC
  `0x38EE7B46`, sha256 `b52195bb…` (report 169);
* **our edited 1917 RCFS** (`/base`) — the vendor
  `PlayBook_2.1.0.1917.1.rcfs` (sha256 `8ccf518c…`) with one guarded call in
  `scripts/startup.sh` that runs `/accounts/devuser/startup.sh &` after the
  final runlevel (report 170);
* the **1917 UFS** — the vendor `PlayBook_2.1.0.1917.5.ufs` **with the operator's `autorun::0` change** (2,550,202,368 B, sha256 `0c0f7d77…`).

The operator added `/accounts/devuser/startup.sh` (4,566 B, sha256
`8acf568f…`) after the flash, and it **works**: the boot log records
`bootctr: wrote 0x32, readback ok (was 0x2E (decimal 46))`.  The door
authenticates and **`devuser` SSH works** (`PB_USER=devuser tools/pbssh.sh …`);
root is denied by `qconnDoor` (`DenyUsers=root`) and is a post-boot operator
method, not a `/base` edit.  Evidence: `/dev/hd1t179 on /base type qnx6`,
`/base/etc/os.version` = `2.1.0.1917`, `/proc/boot/rfs_validator` = 8,759 B.

The `PlayBook_2.1.0.1917_ROOT_V1.4.EXE` UFS was **rejected**: it boots but its
`qconnDoor`/root kit never completes the door handshake, so SSH stays closed.
Use the `.5.ufs` with `autorun::0` (report 170 §3).

**The custom 1917 `/base` (bloatware removal) is no longer the operator's
goal** (2026-11-03).  **The next goal is to boot BB10 on the PlayBook**
(operator, 2026-11-03).  **Session 25 fixed the target to a Z10 OMAP build, but
no BB10 image has been supplied and no BB10 binary has been inspected** — the
work so far is offline/read-only.  Two inputs are outstanding: the **Z10 image**
(SKU + version + path; the only OMAP Z10 is STL100-1 = OMAP4470, while OMAP4460
is the Dev Alpha A — `BB10-PORT.md` §1) and the **QNX 8.0.0 SDP** (the 6.6
`mkrcfs` exists but the operator's instruction is not to trust it).  The
step-by-step for any PlayBook image set is
`docs/project/IMAGE-REBUILD-GUIDE.md`; the BB10 tool surface and intake
procedure are in `BB10-PORT.md` §4a.

**One working unit remains: the reference (`0x50175127`), armed AND sealed.**
`BOOTCTR_FCTRY` was `0x2E` (46) before the operator's top-up and is now
`0x32` (50), written and read back by `/accounts/devuser/startup.sh` at boot
(`.bootctr.log`).  The OS boot tops it up through that script, so read the log
before the next flash; `devuser` cannot read `/dev/rpmb` directly and root is
denied by `qconnDoor`.

**On media now:** region 0 = **our** 2.1.0.1917 IFS (uncompressed, shim
`rfs_validator`, CRC `0x38EE7B46`, sha256 `b52195bb…`, `0x788D84` bytes) at
`0x100000`; the **edited 1917 RCFS** at `0xC00000` (vendor
`PlayBook_2.1.0.1917.1.rcfs`, sha256 `8ccf518c…`, plus the guarded hook);
and the **1917 UFS** (vendor `.5.ufs` + operator `autorun::0`, sha256 `0c0f7d77…`).
The 4869 `.sig` block at `0xF0000` was left stock and is irrelevant with the
shim.  The way back is the 4869 set: sealed IFS + `PB-4869-root.0.1.rcfs` +
the 4869 UFS.

Bench: **PlayBook OS 2.1.0.1917 is up**, USB `0fca:8012` ("RIM Network
Device"), `169.254.0.1`, ports 22/4455/80/443; login **`devuser` only**
(`PB_USER=devuser tools/pbssh.sh …`; root is denied by `qconnDoor`).  The
region-1 Linux root is present, read-only, and left in place.  The spare
(`0x50176EC1`) is **down** — its red 1-2-2 blink is error `0x5B` (NV-record /
RPMB subsystem unusable, report 153) and it presents no USB.  Two 32 GB units
are bricked.  **No RPMB writes by an agent, ever.**

**The active work is the BB10 port** (`BB10-PORT.md`, `ROADMAP.md` item 8),
blocked on the two inputs above; the 1917 IFS work (`IFS-REBUILD.md`) is
complete and the unit's image set is final for now.  The other persistent
blocker is the **Linux `BOOTCTR_FCTRY` top-up**: the monitor's `0x2F` transform
is not reproduced (reports 148/156), so QNX's `/dev/rpmb` is still the only
proven writer.  That gates the hd3 cut-over, not the image work.

## Per-subsystem state

| subsystem | state | detail / evidence |
|---|---|---|
| Chainloader image (CRC path) | **working** | 828 B loader at image `+0x800` = `0x80100000`; copies zImage/DTB high, checks the DTB magic `0xEDFE0DD0`, jumps (`r0=0`, `r1=~0`, `r2=DTB`). Report 109 §8.1, 103 §5.2 |
| DRAM / memory map | **working** | 1 GB at `0x80000000`; kernel runs from `0x84000000`-loaded zImage, decompressed to `0x80008000`; DTS comment |
| Kernel self-capture | **working** | DRAM ring at PA `0x88000000` (32 KB) + breadcrumb/event log; `tools/pb_ring.py`, `tools/pb_events.py`. Bootstrap §3 |
| WDT2 | **working** | Petted from a 2 s `timer_list` (`TGR` kick; `LDR` writes are dropped); heartbeat 14 510 ms measured. Report 109 §8.1 |
| eMMC (MMC2) | **reads proven; writes healthy — but writers must use large blocks** | `mmcblk1` = MCGAFA 59.4 GiB, `boot0`/`boot1`/`rpmb`; needs the `vemmc` fixed supply for `ocr_avail` (report 109 §9).  Session 16 read a freshly flashed 11 152 064-byte image back **byte-exactly** and matched a pre-flash TabletOS capture (report 134); the MMC2 functional clock is 96 MHz.  Session 17 measured the write path (report 136): the driver has no request-size limit — an O_DIRECT write gives **256 KiB `CMD25`s, 5.5–7.0 MB/s** (4 KiB chunks: 0.93 MB/s), one `CMD13` per request (normal `mmc_blk_card_busy`).  The old 17–32 KB/s was `recv`'s tiny buffered writes plus 4–7 unthrottled `printk`s per request to **fbcon**.  Use ≥64 KiB (ideally 256 KiB–1 MiB) writes with the MMC trace off.  **Session 18: region 1 now holds a bootable Linux root** (32 MiB ext4 at raw `0x29000000`), written through the pre-OS `0xF7` loop (2055 frames) and read back byte-exactly (QNX `dd` and Linux `blk sumb`); it mounts read-only via a loop and its `/init` runs (report 140). |
| USB device + gadget | **working, but not a bulk transport** | enumerates `0fca:8010`, CDC-ACM, product string "PlayBook (Linux bring-up)"; `/dev/ttyACM0` ↔ `/dev/ttyGS0`; High Speed (480 Mbps), 512-byte bulk packets.  **Drains at only ~134 KB/s and drops bytes when the host blasts** (report 136 §3.3), so it is a control/debug channel: do not send multi-MB images through `recv`.  Report 114, 136 |
| Userspace (initramfs) | **working, minimal** | `/init` = PID 1, mounts, creates the configfs ACM gadget, forks `/console`.  **No auto-reboot since session 15** — PID 1 reboots only on the console's request (operator decision; the timer was a pre-console way to recover the log and spent a `BOOTCTR_FCTRY` unit per iteration).  **Session 18:** after the gadget is up, `/init` also loop-mounts the region-1 ext4 root read-only at `0x29000000` and forks its `/init` (report 140). Report 132 §10 |
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
| Kernel patch + config | **re-exported (session 20)** | `kernel/patches/0001-playbook-winchester-6.15.11.patch` is **5 137 lines / 34 files**, regenerated from `local/kernel-tree` against pristine 6.15.11 and verified by applying to a pristine copy (`patch -p1 --dry-run`, clean).  Session 18 added the `pb_probe` gating, the `pb_reg` driver group and the DSS clock fix (report 142); session 19 added `drivers/misc/pbmon.c` + `pbmon-pair.h` and the `drivers/misc/Makefile` line (reports 145/146); session 20 changes `pbmon.c` (CM_MPU_STATICDEP, identity low page, the read-only monitors) and the DTS (three `no-map` oracle pages) (reports 147/148) |

## Last known-good image

**`0x0D1DA0D3` remains the boot baseline.**  Session 19's images
(`0x87FD6EED`, `0x9CE309CF`, `0xFCCE507A`, `0x65599D90`, `0xC462AE22`,
`0x771355C0`, `0x94640F82`, `0x853C5323`, `0x96FC5CA9`) are RPMB/monitor
diagnostics; the last, `0x96FC5CA9`, is saved as
`local/images/kernel-0x96FC5CA9-pbmon12.img`, and the earlier ones were
overwritten in `boot/chainloader/kernel.img` (rebuild from the source + patch).
Do not boot `0xFCCE507A` (auto selftest).  None of the session-19 images is a
"last known good" for normal use.

**`0x0D1DA0D3`** — `local/images/kernel-0x0D1DA0D3-noirqerr.img` (session 18).
The `0xFB3A9F5D` userspace (region-1 root mount, `pbrpmb`) with the boot-log
fixes: `pb_probe` gates the DSI/bridge read probes (default off, so **0 DSI CIO
errors**), `pb_reg` is a driver-group attribute (no duplicate/WARN/taint), and
the DSS clock mismatch WARN is fixed with the real 96 MHz rate recorded.
Verified: `boot-20261030-084331.log` has 0 CIO errors, 0 duplicate, 0
Tainted/WARNING, `fck 96000000`, fbcon up, region-1 root mounted (report 142).
**This is the image to build from.**

**`0xFB3A9F5D`** — `local/images/kernel-0xFB3A9F5D-rootfs.img` (session 18).
The `0xFC77CD47` kernel/DTB plus an initramfs whose `/init` loop-mounts the
region-1 ext4 root read-only at raw `0x29000000`, runs `/rootfs/init`, and a
console `rootfs` command; 11 166 136 bytes.  Verified on the bench: ext4 mounted
read-only without a journal, `PB-ROOTFS: /init is running from the region-1 ext4
image (Linux 6.15.11)`, `blk sumb 0x29000000 33554432` = `c2e78e1f09b50208`
(host FNV), boot log `local/logs/boot-20261030-063824.log`.  **This is the image
to build from for the region-1 root.**  Its root is
`local/images/rootfs-region1.ext4` (32 MiB, sha256 `0b87c29f…`, no journal);
rebuild it with `tools/build_rootfs.py` and write it with
`tools/pre_os_ifs_flash.py --payload … --dest-raw 0x29000000` (report 140).

**`0xFC77CD47`** — `local/images/kernel-0xFC77CD47-qnx6read.img` (session 17,
end).  The `0xEB617C60` kernel plus `CONFIG_QNX6FS_FS=y` and the console's
`qnx6` command; 11 164 248 bytes.  Verified on the bench: mounted hd3 read-only
at the corrected offset and exposed the reseed (report 138).  Note the image's
`qnx6` default is still the old `0x48F00000`, so mount with `qnx6 49000000`; the
source default is fixed for the next build.

**`0xEB617C60`** — `local/images/kernel-0xEB617C60-mmcwrite.img` (session 17).
The `0xC07AB9FA` kernel/DTB plus the console's `sink` and `blkwr` commands and
the MMC trace behind `/sys/module/omap_hsmmc/parameters/trace` (default off);
11 159 480 bytes.  Verified on the bench: byte-exact O_DIRECT write read-back in
region 1, and the request shape / throughput in report 136.

**`0xC07AB9FA`** — `local/images/kernel-0xC07AB9FA-emmcproof.img` (session 16).
The `0x8C146805` kernel and DTB plus the console's read-only `blk`/`grep`
commands and the corrected banner text; 11 152 064 bytes.  Verified on the bench:
byte-exact eMMC read-back and a green panel.

**`0x8C146805`** — `local/images/kernel-0x8C146805-portable.img`.  **The first
image with a correct display, built from the portable (moved) workspace.**  It
carries everything: the bridge delivery fix (VC0/HS null packet), the DSI block
matched to the reference, `PPI_TX_RX_TA = 0x00040006` (reliable bridge reads),
the corrected LVDS porches (`VP_HTIM1 = 0x00780001`, `VP_HTIM2 = 0x00760400`),
the **Hitachi TCON table sent by `/init` at bring-up**, the **DSS
functional/core clock raised from 48 to 96 MHz**, the reference's
`VDAC`/`VANA` rails, pads `0x078`/`0x07A`, the `pb_reg` hook with `t`/`u`/`d`/`v`,
PID 1 without the 180 s auto-reboot, and `CONFIG_OMAP2_DSS_DEBUGFS`. <!-- audit-ok: image description, historical -->
Verified on the bench: solid clean magenta, and `pb dss: … -> 96000000`.

`0x068C867F` is the *same source* built before `CONFIG_INITRAMFS_SOURCE` became
repo-relative; the only difference is the config string embedded by
`CONFIG_IKCONFIG`, which changes the CRC (and the image size by 8 bytes).  It is
the image that first showed a correct display.

Image CRC convention: `zlib.crc32(image[0:-4])`, stored in the container's last
word by `tools/build_kernel_image.py`.  A rebuild that prints the same CRC as
the previous one has almost certainly **failed to link** — check the build
output (trap 39).

**Do not reuse:** `0x5E40C5C1` (two lanes + sync-pulse video mode: no display at
all), `0xB9A1BC4F` (two lanes), `0x3B04298E` (LM3532 without VAUX3 — it wedges
the I²C bus and resets the unit).

## The unit

| item | value |
|---|---|
| working units | **one: the reference (`0x50175127`)**, armed AND sealed.  The spare (`0x50176EC1`, BSN `1063043093`) is **down** with the 1-2-2 blink = error `0x5B` (report 153) and presents no USB.  Two 32 GB units are bricked and parked |
| region-0 IFS on media | **ours**: the uncompressed 2.1.0.1917 IFS with the 8,759-byte `rfs_validator` shim, `0x788D84` bytes, `os_type` 6, CRC **`0x38EE7B46`**, sha256 `b52195bb…` (report 169).  Flashed 2026-11-03 with the 1917 RCFS.  The 4869 `.sig` block at `0xF0000` is stock/irrelevant.  **No chainloader/kernel image is on media** |
| region-0 RCFS (`/base`) | **our edited vendor 2.1.0.1917**: `PlayBook_2.1.0.1917.1.rcfs`, sha256 `8ccf518c…`, written sparse at block 192, with one guarded hook in `scripts/startup.sh` (report 170).  Mounted at `/dev/hd1t179`, `/base/etc/os.version` = `2.1.0.1917` |
| UFS (`/`) | **vendor 2.1.0.1917 `.5.ufs` + operator `autorun::0`**: 2,550,202,368 B, sha256 `0c0f7d77…` (setup skipped).  The ROOT V1.4 EXE UFS was rejected — its door never authenticates (report 170 §3) |
| `/accounts/devuser/startup.sh` | **operator's**, 4,566 B, sha256 `8acf568f…`, added after the flash; runs the top-up at boot (`.bootctr.log`) |
| `BOOTCTR_FCTRY` | **`0x32` (50)**, written and read back by the startup script (`bootctr: wrote 0x32, readback ok (was 0x2E (decimal 46))`).  `devuser` cannot read `/dev/rpmb` directly |
| region-1 Linux root | **present, read-only** at raw `0x29000000` (region-1 +16 MiB, block `0x2915`): 32 MiB ext4, no journal, sha256 `0b87c29f…`, FNV-1a64 `c2e78e1f09b50208`.  Written through the pre-OS `0xF7` loop and read back byte-exactly; mounted read-only and its `/init` ran on `0xFB3A9F5D` (report 140).  **Left in place** |
| TabletOS / OS | **PlayBook OS 2.1.0.1917 up** (`QNX localhost 6.6.0 2014/03/19… armle`), **the `.5.ufs` with `autorun::0`** (`/dev/hd3 on /`); USB `0fca:8012` "RIM Network Device", `169.254.0.1`, ports 22/4455/80/443.  Login **`devuser` only** (`PB_USER=devuser tools/pbssh.sh …`); root is denied by `qconnDoor` (`DenyUsers=root`) and is a post-boot operator method |
| device-side files | `tools/pbdsi` was pushed to `/tmp/pbdsi` and is **gone** (QNX `/tmp` is wiped by a reboot).  Re-push before use |

The factory boot budget is real and one-way: every successful factory/CRC boot
spends one unit, and only a **TabletOS** boot puts one back.  `0` and `> 0x32` do
not fall back to the product path — they hard-lock the unit (`SAFETY.md` §2).
**Since session 15 our images no longer reboot themselves**, so a boot now costs
exactly one unit and stays up until asked.

## The single current blocker

**The Linux `BOOTCTR_FCTRY` top-up.**  The caller and the address rule are solved
(identity physical addresses below `0xB0000000`; report 147) and the whole seal
ABI is accepted from Linux, but the monitor's `0x2F` transform does not reproduce
the device's own block-0x43/0x3B pair with any key, framing or ABI variant we can
reach (reports 148/156, trap 63).  The first write-back — a *captured, genuine*
block, accepted by the device — left the spare in the `0x5B` loop (report 149).
Until the transform is reproduced, **QNX's `/dev/rpmb` is the only proven
writer**, `pbrpmb write` stays compiled blocked, and **no agent writes RPMB**
(`SAFETY.md` §6).  This gates the hd3 cut-over, not the IFS milestones.

## What is next

`ROADMAP.md` owns the ordered work list; the 1917 image work is complete
(`IFS-REBUILD.md`, `IMAGE-REBUILD-GUIDE.md`), and **the next goal is item 8, a
BB10 image on the PlayBook** (operator, 2026-11-03), target **a Z10 OMAP
build**.  It cannot start until the **Z10 image** and the **QNX 8.0.0 SDP**
arrive; when the image does, the intake procedure in `BB10-PORT.md` §4a is the
first step (read-only: copy, hash, identify container, list components, then try
the readers on copies).

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

# History

Everything below this divider records what was true earlier.  It is kept for the
evidence and the trail, **not** as current state.

## Session recaps (20–23)

**Session 23 (documentation and publication; no bench).**  The docs audit in
report 167 was executed and the canonical set corrected; the public GitHub
snapshot was built and pushed (`REPO-MANAGEMENT.md`); the earlier-phase document
tree and `SECURITY.md` were deleted; the per-doc session stamps were dropped.
The IFS writer (`tools/ifs_build.py`) was not started.  No USB/device access;
the unit state above is unchanged from the last measurement.

**Session 20 snapshot and the incident (2026-10-31).**  Session 20 performed the
first Linux RPMB write (a captured genuine `BOOTCTR_FCTRY = 0x32` block, with a
verified frame MAC; the device accepted it), and the spare stopped booting and
no longer enumerated on either Linux or Windows.  Report **149**.  The red
**1-2-2** blink is error code **`0x5B`** ("NV-record / RPMB subsystem unusable"),
emitted bit-serially to a FAN5702 LED driver; the SoC does **not** reset and the
loader never brings up a usable USB device (report 153).  The chain is written
down stage by stage in `docs/project/BOOT-CHAIN.md`.

**Session 21 (read-only, report 155).**  The surviving reference (`0x50175127`)
was re-read per `BOOT-CHAIN.md` §5 and every value matched: `BOOTCTR_FCTRY`
**absent**; IFS sha256 **`75be24e8…`** over exactly `0x2BFE10` bytes; `os_type`
**4** at media `0x100100`; CRC **`0x8D46FB4A`**; `end-8` **`0xD7C82D1F`**;
NVRAM `0x2040` **absent** (the loader substitutes `2`), `0x2801` **A=0/B=10**.
The vendor `PB-4869-root.0.3.ifs` and `.0.0.mbr` in
`$BB10MT/` were **byte-identical** to the live IFS/MBR.  **No
writes were performed.**

**Session 21 recapture (report 156).**  A read-only `cat
/dev/rpmb/bgroupcrt_dat` produced a **same-boot pair** for block `0x3B`: P at
page `0xbf0e7000+0x700`, C at frame `0xbfb8e000+0xE4`.  **Both MACs verify** with
the reference's own CID seed and frame key — the block HMAC over the logical
view, the frame MAC over the wire view with a big-endian tail — so the **`0x2E`
oracle is answered**.  The `0x2F` unseal still does **not** match standard
AES/DES with the visible keys (36 162 AES trials).  Files in
`docs/reports/data/session21-capture/`.

**Session 21 trustzone attempt (report 157).**  The proposed
`trustzone-omap4` raw SMC pass-through (devctl **`0xC0280501`**) was tested.  The
resmgr starts and standard devctls work, but the `0x5001` pass-through
**segfaults in libc** (`libc+0x91f8`) on a clean boot; the stock binary, a
bypass patch and a `msg_max_size` patch all fail.  The boot-time `random`
driver's devctl crashes it too.  **The trustzone shortcut is unavailable; the
arm gate remains.**  Tool kept: `tools/qnx-tzsmc.c`.

**Session 21 armed-state measurement (report 160) — superseded for region 0.**
Session 21 found the reference **record-armed but carrying the stock `os_type` 4
image**; on 2026-11-01
the operator **flashed the sealed `os_type` 6 IFS**, so the unit is now **armed
AND sealed**.  The pre-flash facts, for the record: `BOOTCTR_FCTRY` present,
size 4, **`32 00 00 00`** — observed on 2026-11-01 as `50 → 49 → 47 → 49 → 50`.
The falls came from boots into the IFS (operator: reached the splash screen,
then powered off) — not from loader/`info` sessions, which do not consume it.
The rises are the OS top-up.  The **pre-flash** region-0 IFS was sha256
**`75be24e8…`** over exactly `0x2BFE10`, `os_type` **4**, CRC **`0x8D46FB4A`**,
magic `0xC3D0E193`, model `0x06001A06`, `end-8` `0xD7C82D1F` — i.e. **not
CRC-sealed**, which is why it could not boot on the factory path.

**CFP corroboration (report 161, raw dumps in `docs/reports/data/cfp-2026-11-01/`).**
RIM's own tool, run by the operator from Windows, prints the same state from the
loader side: `Boot Mode: Factory (Remaining boot count: 50)`, `Boot OS Count
383,0,0 (Region0,Region1,Max)` / `3,0,0 (Version,Requested Region,Actual Region)`,
`Security: Enabled`, `External MCT/HWV: Enabled`, and **`OS Version: 2.0.0.4869
DEV` in region 0**.  CFP also shows sectors 5-260 of the user area are
**permanently write protected** (MCT/MFG/bootrom), and `ENH_SIZE_MULT = 8`
against `MAX_ENH_SIZE_MULT = 0x72E` — the `hd3` headroom.  CFP is the only
correct `info` for this unit; `bb10mt info` is mostly wrong (operator).

**Static RE of the tooling (reports 162-164, three subagent tracks).**  CFP's
eight unidentified `info` reads are all resolved and confirmed against LDR_01's
dispatcher (`0x8011737C`): `0xBD/0xBE` HW override, `0xBF/0xD9` DRAM, `0xE1/0xC8`
installer metrics, `0xB2/0xC8` DSP metrics (**a stub — always zeros**),
`0xD8/0xC8` OS metrics, `0xE9/0xFB` NVRAM `0x2074`, `0xDB/0xCB` vendor ID,
`0xB5/0xD3` CSD/ExtCSD.  **`RESETQNXOSREGION` is loader op `0xE8 {u32 4}` =
delete NVRAM record `0x2074`** — the same 24-byte record flash `COMPLETE` writes
and `0xE9` reads, so the Windows-only recovery now has an in-tree equivalent
(`tools/bbusb.py reset-osregion`, dry run by default; verify by read-back
because the device returns `0x2E` on both paths).  `0x39` is **not** an MCT
partition type (it is the CREAD opcode), which closes the bb10mt path-2
question.  **One tool now**: `tools/bbusb.py` with subcommands `info`, `flash`,
`reset-osregion`, `boot-mode`, `reset-loader` and `pack`/`unpack`/`split`/`parse`;
offline tests in `tools/tests/test_offline.py` (87 checks, stub device, no USB).

**Session 20's reports** are **147** (the monitor's address view is **identity**
and its reachable DRAM excludes `0xBF040000`: the `0xFF05` refusal was a garbage
command page, not a caller rejection) and **148** (the monitor now serves the
whole seal ABI — all four services return 0 — but the `0x2F` transform does not
reproduce the device's block-0x43 pair with any key/framing/ABI variant tried,
including 512 blocks × 6 framings × 5 keys and 10 560 offline AES combinations).

**Session 19's reports** (the key/seal baseline) are **143** (the RPMB frame key
is **pinned** — read from the live `rpmb-omap4` page with metadata + a physical
read after a `/proc/pid/as` read reset the unit; the block key is `seed[0:16]`)
and **144** (**the 224-byte RPMB record block is encrypted on media by the secure
monitor** — the unseal is `smc` service `0x2F`, and a plaintext block patch is
invalid, which blocks the Linux top-up as written in report 141).

**Session 18's reports** are **139** (a region-1 write was misrouted into region
0 because the loader uses the region-relative V1 `qcfp` record; the sealed IFS
and the RCFS head were restored and verified), **140** (**a Linux root is in
media region 1**: 32 MiB ext4 at raw `0x29000000`, written through the pre-OS
`0xF7` loop, read back byte-exactly, mounted read-only from `/init`, and its
`/init` ran), **141** (**the Linux RPMB key material is normal-world and the read
path works** — its "top-up is feasible" verdict is now qualified by 144), and
**142** (**the boot log is clean**: the DSI CIO errors and the DSS clock warning
are fixed).

**Session 17's reports** are **136** — the Linux eMMC **write path is healthy**
(256 KiB `CMD25`s at 5.5–7.0 MB/s with tracing off); the old 17–32 KB/s was the
writer's tiny buffered writes plus per-request `printk` to the **fbcon** console,
and the ACM console link itself is only ~134 KB/s and drops bytes when blasted
(trap 49 replaced) — **137** (superseded on its causal claim) — and **138**: **a
smaller hd3 filesystem is reformatted and reseeded at boot**; the FS-level shrink
is dead and the partition must change (MCT).

**Session 16's reports** remain **134** (eMMC reads byte-exact under the DSS
clock regime; the MCT/loader block numbers are not raw offsets) and **135** (the
hd3 shrink was attempted, made the unit boot as new, and was reverted; the UFS
was restored with `bb10mt`).

## The display milestone is met

**The panel works.**  On `0x8C146805` it shows solid white, solid red and the
kernel console as legible text, stable, with no bands or noise (operator,
report 133 §4).

Three defects had to be fixed, each necessary, each found by comparing against
the reference rather than by reasoning about it:

1. **Bridge delivery** (session 14): the DSI HS clock was never started.  Fixed
   with the reference's VC0/HS null packet (report 131).
2. **Bridge video configuration + panel TCON** (session 15): our `VP_HTIM1/2`
   had the LVDS porches in the reserved top byte (HBP = HFP = 0), and the
   Hitachi TCON table was only ever sent by hand.  Fixed porches
   (`0x00780001`/`0x00760400`) plus the table sent at bring-up (reports 132,
   133).
3. **The DISPC core clock** (session 15): the DSS functional clock was left at
   48 MHz by the boot chain while the pixel clock is 50.4 MHz.  Raised to 96 MHz
   in `dss_probe()`, matching the reference's 100.8 MHz regime (report 133).

Two things that misled earlier sessions should not be reused: `LANESTATUS0` and
`SYSSTAT` read 0 on a **working** display (report 132 §4), and the session-13
"partial live image" was never ours (report 129 §5).

## Session-18 blocker text (kept for the trail)

**The region-1 root path works and the boot log is clean; the remaining blockers
are the Linux RPMB top-up and a real userland.  `hd3` can then be repurposed
without an MCT edit.**

1. **Solved: bulk data and a bootable root in region 1.**  A 32 MiB ext4 root
   reached region 1 through the pre-OS `0xF7` loop (2055 frames), read back
   byte-exactly on the media and from Linux, mounted **read-only** through a
   loop, and its `/init` ran (report 140).  The eMMC write path itself was
   already known healthy (256 KiB `CMD25`s at 5.5–7.0 MB/s, report 136); the
   missing pieces were a transport and a root, and both now exist.  The loader
   container detail that bit us — the region-relative **V1 `qcfp`** record — is
   trap 55 / report 139.  From Linux the eMMC is accessed read-only by
   `blk`/`rootfs` today; **writable eMMC for our own data is now allowed**
   (`DECISIONS.md` §18).
2. **The ACM console is not a bulk transport.**  It drains at ~134 KB/s and
   **drops bytes** when the host blasts (16 MiB lost ~768 KiB; a 1 MiB blast
   never completed), so `recv` cannot carry a rootfs image and its old transfer
   did not read back correctly.  Bulk images must go through the pre-OS `0xF7`
   loop or QNX/`bb10mt`; a faster userspace transport (RNDIS/ECM, USB mass
   storage) is the real fix for the nura direction.  (Report 136 §3.3/§3.4.)
3. **The hd3 shrink is not an FS-level operation** (report 138; supersedes the
   `pps`-race reading of report 137 and report 135 §2).  A 1 GiB QNX6 FS built
   from the **UFS factory tree** and written to hd3 booted to setup; the
   read-only inspection showed the boot had **reformatted hd3 to 58 GB and
   reseeded it from `/base`** (`num_blocks` 15,273,980, inodes 238,656,
   `/.rootfs.os.version` = 2.0.0.4869, `var/pps` without `system@`).  So
   `rootfs.sh`'s format+seed path ran: the smaller FS was not accepted as the
   seeded R/W root (QNX can mount a smaller FS — the rejection is in the
   boot/root-selection path, not `fs-qnx6`).  The base seed is unprovisioned,
   hence setup.  **A smaller hd3 root needs an MCT/partition change
   (operator-only); do not retry an FS-only shrink.**
