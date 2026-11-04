# Decisions

Each entry records a decision, why it was taken, and the evidence that forced it.
Where a decision was taken for a weaker reason than evidence (timing, cost, or
"it cannot hurt"), that is stated.

## 1. Four DSI data lanes

**Decision.** The DSI host endpoint lists five lane pairs
(`lanes = <0 1 2 3 4 5 6 7 8 9>` = clock + 4 data) and the bridge node says
`lanes = <4>`.

**Why.** The reference uses four data lanes. Two lanes force a 75.6 MHz byte
clock; four give the reference's 37.8 MHz.

**Evidence.** `DSI_COMPLEXIO_CFG1` reads `0x2A054321` on QNX (CLK=1, DATA1..4 =
2,3,4,5) versus `0x2A000321` for a two-lane build. The arithmetic agrees:
1024x600 at 60 Hz RGB888 is 50.4 Mpixel/s x 24 bpp = 1.2096 Gbps; over 4 lanes
that is 302.4 Mbps/lane = 37.8 MHz byte clock = QNX's `DSI_VM_TIMING3`
`TL = 1008` (1008 x 625 x 60). After the change `COMPLEXIO_CFG1`,
`VM_TIMING3` (`0x03F00258`), `DSI_IRQSTATUS` (`0x80`) and
`VC_IRQSTATUS(0)` (`0x4`) all became byte-identical to the reference
(report 121 §11, §11.1). `DSI_GNQ[11:9]` reset value 4 means 1+4 = 5 lanes are
supported, so a five-pair list is legal (report 118 §8.2). Reports 111 and 118
§8.1 assumed two lanes and 118 §8.2 called the resulting 2x rate "benign"; both
are superseded (`KNOWN-ISSUES.md` §3.1).

## 2. Match `DSI_CTRL` to the reference

**Decision.** Two bits of the video-mode `DSI_CTRL` setup in
`drivers/gpu/drm/omapdrm/dss/dsi.c` are overridden for this board:
`CS_RX_EN = 0` and `VP_CLK_POL = 1`, with a comment.

**Why.** It was the only remaining difference in the stream configuration, and
one of the two bits has a real effect on the video port.

**Evidence.** Decoded from TRM Table 10-530 (report 121 §11.2): upstream sets
`CS_RX_EN` (bit 1, receive-path checksum, irrelevant to a transmit-only link)
and clears `VP_CLK_POL` (bit 8, video-port sampling edge); QNX has the opposite
of both. After the change `DSI_CTRL` reads `0x00EAEF9D`, byte-identical to the
reference. This did **not** make the panel show an image.

## 3. Program the TC358764 with the reference's own sequence — and never write `SYS_RST <- 0x1f`

**Decision (supersedes the earlier "do not program it" decision).** The
bridge is programmed with the exact register sequence recovered from
`winch_lcdctl` (report 125), at the reference's point: inside
`dsi_init_dsi()`, after `dsi_proto_config()` and before
`dsi_setup_dsi_vcs()`, with the RESX pulse there, `DSI_VC_CTRL(1) = 0x20009D81`
(HS), and a TX-FIFO drain between packets.  The node still carries
`reset-gpios = <&gpio2 24 GPIO_ACTIVE_LOW>`; **whether `GPIO2_24` really is the
bridge's RESX is open** (`docs/contradictions/gpio2-24-resx-vs-datasheet.md`) —
the pulse is kept because it is in the reference's own sequence.

**Why.** The reference *does* program it.  `winch_lcdctl` calls a full bridge
programmer at vaddr `0x13e50` from `0x1938c`, unconditionally, between the DSI
PHY/PLL programming and the DSI VC enable.  Leaving it unprogrammed left the
panel black; with the sequence the panel first produced a partial image — which
report 129 §5 later withdrew as a panel/TCON transient, not our content; the
real display fix came in sessions 14/15 (reports 131–133).

**Evidence.** (a) The call site and the complete sequence, disassembled
instruction by instruction (report 125 §1–2).  (b) The datasheet names
`LV_CFG[0]` (`0x049C`) as the LVDS enable and `0x04A0[8]` as the PLL
power-down.  (c) QNX's own `-t/-T` self-test proves the reference's writes land
(`GPIO2_DATAIN` bit 4 toggles, report 127 §2).  (d) The earlier "nothing
programs it" evidence (the boot ROM cannot; the second-stage loader has no
display constants; the IFS "only has winch_lcdctl") is consistent — the IFS's
`winch_lcdctl` *is* the programmer, and the call was already printed in report
122 §7.1.  (e) `SYS_RST <- 0x1f` left faint vertical artifacts on the reference
until a battery pull (report 121 §7.6, trap 28); the reference writes
`SYS_RST_LCD` (4).

**Retired variable.** A failed BTA only proves the bridge does not answer
*reads*, not that writes did not land.  The command VC must be in the
reference's state (HS, `0x20009D81`) for the writes to land; mainline's LP/no-DMA
state did not deliver them (report 127 §3).

## 3a. Use the reference's exact DSI PLL words

**Decision.** For this board the PLL is programmed with REGM = 756, REGN = 96,
mX = 6 for both HSDIVs, `PLL_STOPMODE` (CFG1 bit 0) clear, and CFG2 bits 3:1
clear — `CFG1 = 0x14A5E8BE`, `CFG2 = 0x00056000`, byte-identical to QNX.

**Why.** The earlier relay warning "do not force the PLL words" was about the
2× lane-count confusion, which is resolved; with the lanes/timings matched, the
PLL's reference-divider/jitter was the remaining encoding difference, and a
one-frame-then-black symptom is consistent with the PLL stopping.

**Evidence.** Measured: `clkin = 38400000`, `fint = 400000`,
`clkdco = 604800000`, `CFG1 = 0x14A5E8BE`, `CFG2 = 0x00056000` (report 127 §4).
**Honest caveat:** making the PLL exact did **not** by itself produce a held
image; it is kept because it is the reference's state and removes a variable.

## 4. Keep BURST video mode

**Decision.** `MIPI_DSI_MODE_VIDEO_BURST` stays; non-burst is not an option to
try. **Why.** It does not work at this pixel clock, and it fails *early* — the
mode is rejected, so there is no partial state to learn from.

**Evidence.** A non-burst build produced `[drm] Cannot find any crtc or sizes`,
no `/dev/fb0`, and the DSS stayed in standby (trap 31; image `0x5E40C5C1`, which
also had two lanes, must not be used as a reference). The subtype mismatch
between `VP_CTRL_EVTMODE` and QNX's `DSI_CTRL` bit pattern remains an open
question in report 121 §7.3 item 3, but burst is the mode the reference runs.

## 5. gpio-hog rather than a `regulator-fixed` GPIO for pre-probe enables

**Decision.** Every display enable line that must be true before a device probes
is a `gpio-hog` on `&gpio2`, `&gpio1` (line 16 for the gate, plus 9/12/13/28/29
and GPIO1 2/6/14).

**Why.** A `regulator-fixed`'s GPIO is only driven when the regulator is enabled,
i.e. from `regulator_init_complete()` at `late_initcall_sync`, which races async
device probing. A gpio-hog is applied at gpiochip registration — early and
deterministic.

**Evidence.** Same image, two boots: one bound `lm3532-led`, the next did not,
and an I²C probe that NAKs is never retried. With the hog, every boot since has
bound (report 120 §1.1, trap 22). `GPIO2_16` is the LM3532's I²C gate, proven
causally (line low → `0x38` NAKs; high → ACKs; low again → NAK again, report 120
§0.3).

## 6. VAUX1/2/3 held at 2.8 V with `min == max`

**Decision.** `&vaux1`, `&vaux2`, `&vaux3` carry
`regulator-min-microvolt = regulator-max-microvolt = <2800000>` plus
`always-on`/`boot-on`.

**Why.** That is the reference's own state, and Linux had the rails "enabled"
with no voltage programmed. `min == max` makes the regulator core apply the
voltage at registration (`apply_uV`), which is the only way to force a rail from
the DT without writing a consumer driver.

**Evidence.** `pmic-twl6030-ctl get_state/get_vout` on the reference shows
vaux1/2/3 ON at 2800 mV; Linux exposed `VAUX3_6030` with no `microvolt` at all,
and forcing it removed a real fault — before VAUX3, any transfer to `0x38` hung
the I²C bus and reset the unit; after it, the client enumerated and simply
deferred (report 118 §9.2, §9.6.1; report 120 §0.2).

**Honest caveat.** Forcing the rails did **not** make `0x38` answer; the actual
gate turned out to be `GPIO2_16` (decision 5). The rails are kept because they
match the reference and cost nothing, not because they fixed the backlight.

## 7. initramfs-only userspace

**Decision.** Userspace would be an initramfs only: no separate root filesystem,
no modules, no busybox, no shell. PID 1 plus a
purpose-built console, everything static, every relevant config `=y`.

**Why.** A missing root filesystem is what killed the first bring-ups (the kernel
panicked in `prepare_namespace()` and the panic looked like a hardware freeze),
and an initramfs removes the block layer, the filesystem and the media writes
from the experiment. It also makes every boot cheap and reversible.

**Evidence.** Report 109 §8: a static initramfs `/init` as PID 1 avoids
`prepare_namespace()` entirely, and the "global CPU halt at ~1.5 s" turned out to
be that VFS panic with IRQs disabled. Report 118 §11: several DT nodes have
drivers built as `=m` and this kernel cannot load modules, so they silently never
bind — hence `PWM_TWL=y`, `PWM_TWL_LED=y`, `SPI_SPIDEV=y`, `LEDS_LM3532=y`,
`DRM_OMAP=y`, and `TWL6030_GPADC` left as `=m` only because its parent `IIO` is
`=m`.

**Cost.** Every config change is a rebuild plus a flash cycle; no module can be
tried on the running unit.

## 8. The agent must not write RPMB (or the eMMC, or the NVRAM) — superseded in part (session 18)

**Session 18 update.** The operator dropped the blanket eMMC prohibition:
writable eMMC for **our own** data is allowed (§18).  The RPMB rule is also
moving — the goal is for Linux to perform the `BOOTCTR_FCTRY = 0x32` top-up
itself so the unit is independent of QNX.  That is gated on the static-RE
verdict (report 141): if the key is accessible to Linux, the verified
one-write/readback shape is the only RPMB write permitted; if not, the top-up
stays a QNX/operator action.  **NVRAM remains read-only.**  The reasoning below
is the history, and it still governs the *form* of any RPMB write.

**Session 20 correction (the rule is absolute again).**  The first Linux
write-back — a *captured, genuine* `BOOTCTR_FCTRY = 0x32` block with a verified
frame MAC — was accepted by the device (`result=0`, read-back identical) and the
spare then stopped booting (report 149).  So the "Linux performs the top-up"
goal is **not** a licence for an agent to write RPMB: `SAFETY.md` §6 now says an
agent never writes RPMB, at all, and any top-up is the operator's, from a booted
OS, with a readback.  The transform is still unreproduced (reports 148/156).

**Decision (as originally taken).** `BOOTCTR_FCTRY` maintenance is out of bounds for automated work.
It is done by `bench/device-startup.sh` (`top_up_bootctr`) on the unit, or by the
operator by hand.

**Why.** The record lives in RPMB behind an authenticated write path (nonce +
secure-monitor MAC + monotonic write counter, key KEK-derived). `0` and `> 0x32`
are **not** a fallback: the bootloader refuses to boot at all — "expired factory
mode" — and recovery is physical (desolder the eMMC, clear the record, resolder).
One unit is in that state. Report 116 §2.2.

**What "must not write" does not mean.** Writing exactly `0x32` is the normal,
intended top-up — it is how the current state was reached, from QNX, once per
TabletOS boot. The rule is narrower and harder: **no write is ever tested on the
working unit**, every write is exactly 4 bytes at offset 0 and is verified by
readback, and `>`/`>>`/`O_TRUNC` are brick operations. `SAFETY.md` §2 has the
full set; report 92 §5.2 is the write recipe.

**Evidence.** Report 116 §1–§2: RPMB, not NVRAM; every factory/CRC boot
decrements the counter (measured 50 → 49 on the 5-byte reseal, report 95 §4);
the reference implementation is `/accounts/devuser/startup.sh`, which verifies
its own write by readback. Report 116 §2.1 records the trap that a *static*
reading of `0x32` was only static because that script repairs it every boot —
and session 10 later showed the script had been writing the brick value
`0x00000000` while logging "ok"
(`docs/contradictions/bootctr-drain-vs-session10-readings.md`).

## 10. Region 0 only — no A/B staging for our image

**Decision.** Our image is written to region 0 and verified there. Region 1 is
not prepared as a fallback, and the NVRAM A/B record `0x2074` is not used to
point a factory boot at a region.

**Why.** The factory path hardcodes region 0, verifies it *once*, and never reads
`0x2074`; only the product/ECDSA path has the requested/actual-region fallback.
A "fallback image" would have to occupy region 0 too, so the real fallback is the
pre-OS `0xF7` reflash.

**Evidence.** `0x800142B0` reads the RPMB selector and does `region := 0; bl
0x80014148; return` on a nonzero value (reports 89 §3.5, 94 §0, 97 §0); the
5-byte reseal booted with `0x2074` and `0x2801` byte-identical, i.e. nothing read
region 1 (report 95 §5). The earlier plan in report 85 §1–§3 assumed a global
selector and A/B fallback and was wrong — see
`docs/contradictions/85-ab-fallback-vs-factory-region0.md`.

## 11. The pristine 6.15.11 tarball is part of the repository payload

**Decision.** `local/pristine/linux-6.15.11.tar.xz` (151 MB, gitignored like the
rest of `local/`) is the canonical copy of the diff baseline.  The pre-extracted
tree under the wsl-migration directory is a convenience, not a dependency.

**Why.** The kernel patch is only trustworthy if it can be *re-verified*, and
until session 14 the only pristine 6.15.11 tree lived outside the repository.  A
single external clean-up would have destroyed the ability to check the patch.

**Evidence.** The tarball extracts to a tree that is **byte-identical** to the
pre-extracted pristine tree (`diff -rq`, 0 differences), and the patch as
regenerated at that time (`4 003` lines, 31 files) applies to it and reproduces
`local/kernel-tree`'s 31 changed files byte-identically.  **The current patch is
`5 137` lines / 34 files** (re-exported in session 20; `SETUP.md` §3,
`kernel/patches/README.md`).  sha256 of the tarball:
`89ab469fc35bd9cbc6c5bf4e5cb802581806d5cbc14fb47e5c9edcc477e65d93`.

**Honest caveat.** 151 MB is large for a payload the previous session had just
pruned to ~35 MB (`local/images`).  It is kept outside `local/images`
deliberately: that directory is a cache, this is a reproducibility baseline.

## 12. Keep a register-access hook for the TC358764 in the board kernel

**Decision.** `dsi.c` exposes `/sys/bus/platform/devices/58004000.encoder/pb_reg`
(`w<addr>:<val>`, `r<addr>`, `v`), compiled in only for
`blackberry,omap4-winchester`.

**Why.** Every bridge hypothesis used to cost a flash plus a boot (~4 minutes).
Once the bridge answered, a single console command proved the whole path:
`w0524:0` dropped GPIO36 from 1 to 0.  It also allows reading state *during*
video, which no earlier build could do.

**Evidence.** Reports 131 §4a/§4c: `r0214`, `r0218`, `r0500`, the D-PHY dump and
the `v` (video-activity) op were all used to characterise the remaining fault
without a single reboot.

**Honest caveat.** It is a debug surface that writes a DSS-owned bridge from a
sysfs attribute.  It takes `dsi_bus_lock()` and only runs while the interface is
enabled, and it is gated to this board — but it should not ship in a general
kernel.  Session 15 extended the op set to `t<addr>` (16 reads, re-arming the
VC), `u<addr>` (16 reads, no re-arm), `d<from>:<to>` (range dump) and made the
documented `v` reachable; see report 132 §10.

## 13. No automatic reboot timer in PID 1

**Decision.** `/init` no longer reboots the unit after 180 s. <!-- audit-ok: the decision records the removed timer -->
It reboots only
when the console asks (`reboot` → the pipe → `shutdown_now`).  The operator holds
the power button if a boot is wedged.

**Why.** The timer was written when there was no USB console: a warm reboot was
the only way to read the log back out of the DRAM rings, so the unit had to come
back to the boot ROM by itself.  Two things then made it a liability rather than
a safety net: the ACM console works, and every reboot spends one
`BOOTCTR_FCTRY` unit — a wedged image could spend several a minute (trap 33).
The operator asked for it to be removed (report 132 §10).

**Evidence.** Session 15 ran four flashes and two IFS restores; the timer cost
nothing this session only because each boot was productive.  Earlier sessions
lost units to reset loops (`SAFETY.md` §2, session-10 §3).

**Honest caveat.** Without the timer a wedged image stays powered until someone
acts.  That is the intended trade: a boot is now a deliberate act, and the
factory counter is a finite resource.

## 14. `PPI_TX_RX_TA = 0x00040006` in the bridge sequence

**Decision.** `pb_bridge_seq` writes `0x00040006` to `PPI_TX_RX_TA (0x013C)`
instead of the reference's `4`.

**Why.** It is the difference between bridge reads answering half the time and
answering always, and reads are the project's main diagnostic.

**Evidence.** Report 132 §3: boot with `4` → boot-time sampling 4/8 and console
samples 8/16; boot with `0x00040006` → 8/8 and 16/16, in two separate boots.  The
value is latched at `PPI_STARTPPI`, so live writes change nothing (that is why
the first A/B looked like "no effect").  The VC re-arm does not affect the rate.

**Honest caveat.** It is a deliberate deviation from the reference.  It is safe
because the reference's own value differs only in the bus turn-around window and
QNX never reads the bridge, but it should be revisited if a clean
reference-identical build is ever wanted for a side-by-side.

## 15. Raise the DSS functional clock at probe time (it is the DISPC core clock)

**Decision.** For `blackberry,omap4-winchester`, `dss_probe()` asks the DSS fck's
parent for `100800000` (the reference's rate).  The clock tree rounds it to
**96 MHz**.

**Why.** The DSS functional clock *is* the DISPC's core clock, and the boot chain
leaves it at 48 MHz while the pixel clock is 50.4 MHz.  That is the last display
defect: the picture was correct at the left of every line and failed across it,
with a black framebuffer still producing white lines and coloured dots.

**Evidence.** `dss_setup_default_clock()`'s `clk rate mismatch: 48000000 !=
128000000` had been in every log and never acted on.  `/sys/kernel/debug/omapdss/clk`
on `0x01F0F890`: `FCK = 48000000`, `DISPC-CORE-CLK lck 48000000`, `LCD pck
50400000`.  After the change: `pb dss: parent want 100800000 rr 0 -> 96000000, fck
96000000`, and the panel shows solid white, solid red and the kernel console as
text.  The PER PLL core did not change (`4A008140 = 7`); only its output dividers
moved (M5 `0x23 → 0x03`).  Report 133 §2/§3.

**Honest caveat.** It runs at probe time, so any driver that computed a divider
from the PER-PLL rate *before* the DSS probed is now wrong; eMMC is on M2 (which
did not move) and the backlight I²C and TCON SPI still work, but this should
become a clock-initialisation property (DT `assigned-clock-rates`, or a board
initcall that runs before the consumers).  Trap 45, report 133 §6.

## 16. An FS-level hd3 shrink is rejected; a smaller FS is reseeded at boot

**Decision.** Do not attempt an FS-only shrink of `hd3` (the 58 GB TabletOS
root).  A smaller hd3 root needs an **MCT/partition change** (operator-only, a
documented brick path); otherwise the Linux root goes to **region 1** (512 MB,
no MCT edit).  The UFS recovery procedure is `bb10mt` (split → unpack → UFS-only
pack → flash at the bootrom; this bench unit's password is `0000`, passed
explicitly — `DEVELOPMENT.md` §9).

**Why.** A 1 GiB QNX6 FS built from the UFS factory tree (10.0.4.197), written
to hd3 and booted, came up at setup; the read-only inspection then showed the
boot had **reformatted hd3 to 58 GB and reseeded it from `/base`**
(`num_blocks` 15,273,980, inodes 238,656, `/.rootfs.os.version` = 2.0.0.4869,
`var/pps` without `system@`).  That is `rootfs.sh`'s format+seed path; the
smaller FS was not accepted as the seeded R/W root (QNX can mount a smaller FS,
so the rejection is in the boot/root-selection path), and the base seed is
unprovisioned — hence setup.  The earlier 2 GiB attempt (report 135) had the same
symptom and is presumed to be the same mechanism (it was not re-inspected).  Separately, the Linux initramfs cannot be the offline writer *through
the console*: the ACM link drains at ~134 KB/s and drops bytes when blasted
(report 136 §3.3).  The media itself writes at 5.5–7.0 MB/s with 256 KiB
`O_DIRECT` requests; the old "single 512-byte `CMD25`" figure was the console's
tiny buffered writes plus per-request printk to fbcon (report 136).

**Evidence.** Report 138 §2: the post-boot hd3 superblock and store listing, the
migrate log, and the 1 GiB build/boot; report 137 §3a: the UFS is a 10.0.4.197
factory root and its mounted tree is 298 MB; report 136 and
`docs/reports/data/mmc-write-0xEB617C60-2026-10-30.txt` for the write path; the
recovery and its hashes are in report 135 §4–§5.

**Honest caveat.** The exact acceptance check for the R/W root is not identified
(no serial console), and a **full-partition** freshly built FS from the same
tree has not been boot-tested; the vendor UFS restore does exactly that and
works.

---

## 17. Read-only eMMC/QNX6 mounts from Linux are allowed; writable ones are not — superseded in part by §18

**Decision.** A **read-only** mount of the eMMC (or a QNX6 partition) from the
Linux bring-up console is allowed, with the operator's approval; writable mounts
remain forbidden.  The verified mechanism is the console's `qnx6 [off-hex]`
(read-only loop over `/dev/mmcblk1`, `LO_FLAGS_READ_ONLY`, `MS_RDONLY`).

**Why.** The operator explicitly approved it ("I approve of the read-only mount
on linux"), and it was used to diagnose the hd3 reseed: hd3 was mounted
read-only at raw `0x49000000` and the navigator store, versions and logs were
read.  No write was made through the mount.

**Evidence.** Report 138 §2; the operator's approval in the session-17
continuation; `docs/contradictions/safety-no-emmc-mount-vs-readonly-root.md`
(resolved).

---

## 18. Writable eMMC for our own data; hd3 becomes the Linux root (session 18)

**Decision.** The old blanket "never write the eMMC" is dropped.  We may write
the eMMC for **our own** data: region 1 holds the read-only Linux root, and
`hd3` (the MCT type-`0x1C` partition, raw `0x49000000`) will be repurposed as a
Linux/postmarketOS root, overwriting QNX's filesystem.  Still forbidden: NVRAM
(always read-only), the MCT (`0xE0`/`0xF1`/`0xF2`/`0xF3`), and any RPMB change
other than the verified `BOOTCTR_FCTRY = 0x32` top-up (pending report 141).

**Why.** Once postmarketOS boots, QNX has no runtime role; keeping a 58 GB QNX
filesystem is wasted space.  The MCT already defines the partition, so **no MCT
edit is needed** — we overwrite its contents.  The operator wants the unit to be
independent of QNX, including the boot-counter top-up.

**Constraint.** QNX remains recoverable, not dual-bootable: the factory path
boots region 0 only, so running QNX again means restoring the sealed IFS/RCFS
and the vendor UFS (bb10mt).  Those, and the `hd3` tar, are preserved on the
host.  The RPMB top-up is the gating item: if Linux cannot write it, the counter
has a finite budget — the operator's choice is to solve it from Linux, not with
periodic QNX restores.

**Evidence.** Reports 139/140 (region-1 root, the loader write path); 138 (the
FS-level shrink is dead); 141 (RPMB top-up feasibility, in progress); the
operator's decision in session 18.

---

## 19. The `BOOTCTR_FCTRY` top-up must be done from Linux (session 18)

**Decision.** The unit must be independent of QNX for the factory-counter
top-up: our Linux kernel/userspace performs the `BOOTCTR_FCTRY = 0x32` write.
The operator explicitly rejected periodic QNX restores as the mechanism.

**Why.** Once `hd3` is repurposed as the Linux root, TabletOS is gone; the
counter would otherwise run out.  Report 141 shows this is feasible: the RPMB
key is **not** secure-world-only (the monitor receives the key bytes by
pointer), the key is `f(seed)` of an in-image literal (or the CID), and
`/dev/mmcblk1rpmb` is a pure 512-byte frame relay with no kernel keying.  The
Linux **read** path is proven (counter and authenticated read, zero writes);
the remaining unknown is `f`, settable by one read-only extraction from a live
QNX page or by calling the recovered `smc #1` ABI from Linux.

**Constraint.** Only the verified one-write/readback shape is permitted
(`SAFETY.md` §2/§4): exactly 4 bytes, value `0x32`, read first, write only on a
mismatch, verify by readback, never a loop.  NVRAM and the MCT remain forbidden.

**Evidence.** Report 141 (feasibility), report 142 (the Linux read path and the
clean boot); the operator's session-18 direction ("i'd prefer that we do it all
from linux, be independent").

---

## 20. The RPMB record block is sealed; a plaintext patch is forbidden (session 19)

**Decision.** The `BOOTCTR_FCTRY` top-up is a **sealed** write.  The 224-byte
record block is encrypted on media by the secure monitor (`0x2F`); both HMACs
cover the ciphertext.  A Linux top-up must reproduce the seal (or call the
monitor); **patching the received block in plaintext is forbidden** — the unseal
would produce a damaged record or a wrong value, which is the brick path.  The
monitor-call route is the chosen path, and it may only be exercised after the
command-page physical address is pinned and the block-0x43 known pair is
reproduced.  Until then QNX's `/dev/rpmb` is the only proven writer.

**Why.** Session 19 pinned the frame key (`dfeec0ce…2274`) and the block key
(`seed[0:16]`), then found that all 512 blocks authenticate but none is
plaintext; the resmgr unseals with `smc` `0x2F` and the write path seals with it.
A known block-0x43 pair (live page buffer vs media) confirms the seal, and no
standard AES mode with any known key reproduces it.  The first hang of the
implemented monitor call was the monitor's DRAM bias (`phys - 0x10000000`); with
it the call returns, but every service answers `0xFF05` — **that reading is
superseded by report 147: `0xFF05` was a page-validation error, not a caller
refusal; the address rule is identity physical pages below `0xB0000000`.**

**Constraint.** No RPMB write until a seal reproduces the media bytes; then
exactly one `0x32` write with readback (`SAFETY.md` §2/§4).  Do not send a seal
with a guessed physical address — the monitor writes 224 bytes where told.
NVRAM and the MCT remain forbidden.

**Evidence.** Reports 143 (keys), 144 (sealed store + known pair), 145 (the
monitor call and `0xFF05`), 146 (static RE: no handshake; `r3` suspect);
`KNOWN-ISSUES.md` traps 59–61.

---

## 22. BB10: backport the Winchester platform support into 10.3 (session 25)

**Decision (operator, 2026-11-03).** The first BB10 attempt targets **Z10
10.3.03.2049 STL100-1**, with the **Winchester (PlayBook) platform support
backported from the known-running `10.0.09.1103-ROOT-winchester` set** — route B
of the three in `BB10-PORT.md` §4b.  It is not a "boot 10.0.9.1103 first"
experiment (route A), and not a wholesale display-stack replacement (route C).

**Why.** The operator wants 10.3 as the target, and the analysis shows the port
is a *platform-support* problem, not a container one: the 10.3 IFS has only
`*London*` and MSM branches in `ifs_variables.sh` while 10.0.9.1103 still has
the full `*Winchester*` branch (OMAP4430/4460, `graphics/omap4430`,
`devpm-omap4.so`, `thermal_win.conf`).  The 10.3 `/base` also still carries
partial winchester support (deckard platform, `devu-ehci-omap4430-winchester.so`,
a 1024x600 `splash_winchester.conf`), so the board was not fully removed.

**Evidence.** `BB10-PORT.md` §4b: both IFS images are plain QNX imagefs read by
`dumpifs`; both are `os_type = 4`; `/base` is the `r-c-f-s` RCFS and
`tools/rcfs_extract.py` reads both fully; the branch tables differ as above.
Dependency check (session 25): every winchester/colt binary from 10.0.9.1103
resolves against the **10.3** IFS libraries (`libhwio-gen`, `libhwio-omap4`,
`libcsm`, `libcam`, `libpps`, `libslog2`, `libforensics`, libc/m); only
`librocket.so.1` is absent and is carried along.  Three plugins
(`cpuTemp_omap4430.so`, `cpuTemp_omap4460.so`, `led-led_rocket.so`) reference
symbols (`slog2*`, `thermalVerbose`, `pps_*`) they do not link, i.e. they rely
on the dlopen/process namespace — to verify at run time.

**Constraint.** Route A would not even boot the armed reference: the
10.0.9.1103 IFS is `os_type = 4` (product/ECDSA), and the operator confirms
that run was on an **unarmed** unit; the reference is armed, so only the
factory/CRC path is available until the set is resealed.  Whatever is built
must therefore be resealed (`os_type = 6` + CRC) and packed V1+V2 with the
PlayBook deltas (the Z10 container is V2-only, trap 55).  One variable per
flash; the way back is the working 2.1.0.1917 set.

**Open.** Whether 10.3's winmgr accepts the 10.0 `graphics.conf` dialect and
`displayHAL-winchester.so`; whether the 10.3 `rfs_validator` mounts the RCFS at
the PlayBook's device path (`/dev/emmc/os%ut179` vs the TabletOS `hd1t179`);
whether the 10.3 UFS works in the PlayBook's `hd3`.

---

## 23. BB10: disable the deckard OS-blocklist NVRAM writer (session 25)

**Decision (operator, 2026-11-03).** The BB10 `/base` is edited so the
OS-blocklist record (`NV_OSSTORE_NotSupportedOS`, NVRAM `0x2819`) can no longer
be written by the OS: `deckard/platform/blacklist.pyc`'s `update`, `write` and
`_write` are no-ops.

**Why.**  The bootloader verifier reads `0x2819`; if BB10 writes our image's
version range into it as unsupported, the unit can stop booting.  The operator
identified the mechanism and asked for it to be invalidated.

**Evidence.**  `uncompyle6` of the 10.3.03.2049 `/base` module: the class holds
`NV_OSSTORE_NotSupportedOS_NUM = 10265` and `_write` opens `/dev/nvram`
`O_WRONLY` and `pwrite`s the `OSRangeAndType` records; `read` is the harmless
consumer.  The edit was compiled with a portable CPython 3.2.6 and spliced into
the RCFS in place (inode 12,015, LZO recompressed 2,957 B, `inode.size` 7,849,
byte-identical readback).  `sbin/blupdater` is a separate, native, update-time
writer and is recorded, not edited.

**Constraint.**  Any `/base` edit invalidates the RCFS verifier hash/signature,
so the stock `rfs_validator` must be replaced by `tools/rfs_rcfs_shim.c`, which
mounts the RCFS through `fs-rcfs.so` without verification.  The reference stays
recoverable only through the pre-OS `0xF7`/`bbusb.py flash` path with the
working 2.1.0.1917 set.

---

## 24. BB10 10.3 verified but does not run; use 10.0.9.1103 as the control (session 25)

**Decision.** The first BB10 10.3.03.2049 set is **not** to be iterated on the
unit for now.  Restore the working 2.1.0.1917 set, then test the
**10.0.9.1103 winchester set** (resealed to `os_type 6` + CRC) as a
compatibility control before any further 10.3 work.

**Why.** The 10.3 image is structurally correct — the loader's CRC path
accepted it and jumped (`factory_count` 50 → 45), but BB10 never brought up a
display or USB and the unit re-entered the bootrom on each reset.  With one
working unit and a finite factory counter, the cheapest next fact is whether a
BB10 image that is *reported to run* on the PlayBook behaves differently.  If
it runs, the 10.3 gap is something the Winchester backport did not cover; if it
also fails, the gap is the kernel/board support and 10.3 iteration is pointless
without a different route (for example a PlayBook-native BB10 image or RIM's
own TabletOS-era platform code).

**Evidence.** `BB10-PORT.md` §4b/§4c and report 171/172 (the format/toolchain);
the loader `info` output (`os_address 0x800FF800-0x80928247`, `0x06001A06`,
`factory_count 45`); the session-25 flash log (2,964,324,384/2,964,324,384
bytes, signature, COMPLETE).

**Constraint.** The counter only refills on a TabletOS boot; the way back
(`local/work/back-1917.signed`) is packed and must be flashed from the
ramloader.  An agent never writes RPMB; no flash without the operator.

---

These were the open policy questions.  The licence and visibility are decided
(session 23); the rest remain open and are recorded so nobody assumes a policy
exists.

### Repository license

**Decided (session 23): GPL-3.0-or-later.**  The project's own code and
documentation are licensed under GPL-3.0-or-later (see `LICENSE`).  This is the
simplest licence compatible with `tools/bbusb.py`, a GPL-3.0 derivative of
bb10mt.  The kernel patch and the copies under `kernel/source/` remain
**GPL-2.0-only** (they are Linux 6.15.11 derivatives); `NOTICE` records that and
the rest of the provenance.  The mixed corpus is unchanged: our own sources, the
GPL-2.0 kernel delta, TI TRM/datasheet references, and third-party vendor
tooling under the gitignored `local/vendor/`.

**Addendum (2026-11-01): three tracked files now carry GPL-3 derivation, which the
choice has to account for.**

* the container code in `tools/bbusb.py` is a transcription of `TMFCQPackStream`
  from bb10mt's `qcfm.pas`, and bb10mt is **GPL-3.0** (`local/vendor/bb10mt-main/LICENSE`). It is
  transcribed rather than imported because the Pack Pascal needs cannot be
  installed here — but transcription is still a derivative work, and the file also
  **embeds the 560-byte `signature_data` constant copied verbatim** from that
  source. This is the clearest GPL-derived artifact in the tree.
* `tools/bbusb.py` is a **Python port of `bbusb.pas` and `ucrypto.pas`**
  (`HashPassV2` directly, framing and handshake by the same source).  It moved out
  of `local/vendor/` into `tools/` on the operator's ruling that it is our code,
  which brings a second substantial GPL-3 derivation into the tracked tree.
* `tools/pre_os_ifs_flash.py` already *reads* `signature_data` out of
  `qcfm.pas` at run time (`load_signature()`), so it depends on the GPL source
  being present, though it does not carry the constant itself.

Files written this session that are **not** GPL-derived, for contrast:
the `info`, `flash` and `reset-osregion` commands in `tools/bbusb.py`, and
`tools/tests/test_offline.py`.  Their content came from static RE of `CFP.exe` and
of the LDR_01 loader (opcodes and payload shapes — protocol facts), not from
copied source.  The project chose **GPL-3.0-or-later** in session 23 (see
`LICENSE`/`NOTICE`); the QC container format itself is RIM's, and reimplementing
a format is not the same as copying an implementation.

### Repository visibility

**Public.**  The GitHub repository is a fresh public snapshot of this tree; the
full working history remains in the private local repository.  The snapshot
excludes `.commandcode/`, the proprietary datasheets, the bb10mt source and the
path-leaking host binary.  Visibility is a property of the host, not something
the tree can prove.

### Publishing the proprietary QNX dumps and the TI TRM

**Undecided, currently withheld.** `local/` (device dumps, the sealed IFS, the
other images) is gitignored as a block, and `*.pdf` is gitignored separately —
that is what keeps the TRM out of version control, although
`hardware/swpu231ap.pdf.txt` (a text extraction, ~31 MB) and
`hardware/twl6030.pdf` are on disk. Whether, when and in what form any of this
is published is open; the default in force is "device-derived and vendor
material stays local". The reports quote TRM page/line references precisely so
the analysis can be followed without redistributing it.

### Provenance of the `bbusb` module

**Location resolved 2026-11-01; licensing still open.**  The module is **ours** and
now lives at **`tools/bbusb.py`**, tracked.  The operator's ruling: it was never
vendor material — putting it under `local/vendor/` was a mistake ("the rest of the
stuff in vendor does check out, but bbusb should be part of tools").  The ramloader
blob it uploads is the opposite case: `loader_06001A06-00.bin` (**LDR_01**, uploaded
over USB, never on media — report 98 §1.1 item 1, report 121 §12) is RIM material and
therefore stays gitignored, now at **`local/loaders/`**; `bbusb.DEFAULT_LOADERS`
resolves `$BBUSB_LOADERS`, then `tools/loaders`, then `local/loaders`, then the old
`local/vendor/bbusb/loaders` path.

**Licensing, recorded rather than decided:** the module is a Python port of bb10mt's
`bbusb.pas` and `ucrypto.pas` (`HashPassV2` is a direct port; framing, packet
numbering and the bootrom handshake follow the same source), and bb10mt is
**GPL-3.0**.  Moving it into the tree therefore puts a second GPL-3-derived file
alongside the container code in `tools/bbusb.py` (see the addendum above).  Reports `135`/`141` and
older docs still cite the pre-move `local/vendor/bbusb/...` paths — those are
historical records and are left as written.

## 21. The reference unit may be armed, gated on RE and on the preconditions (session 20)

**Decision (operator, 2026-10-31).** The last working unit is the **reference
(`0x50175127`)**.  The operator authorises putting it on the factory/CRC path
despite the standing "reference is read-only" rule, **provided** that the RE is
finished first and every precondition below is verified read-only before the
record is created.  The procedure is `ARMING-TUTORIAL.md`; **an agent still never
writes RPMB** (`SAFETY.md` §6) — the operator runs the write.

**Executed.**  The operator created the record during session 21, and the sealed
`os_type` 6 IFS was flashed on 2026-11-01; the unit is **armed and sealed** and
boots.  Current state: `STATE.md`.

**Why this is a real decision and not a formality.** Session 20 wrote a
*captured, genuine* record block to the spare with a correct frame, the device
accepted it (`result=0`) and the read-back matched, and the unit is now
permanently in the LED error loop **1-2-2 = `0x5B` (NV-record / RPMB subsystem
unusable)** with no USB path at all (reports 149/153).  There is one unit left,
so the cost of a second mistake is the whole project.

**Preconditions to verify read-only before arming** (report 154 §Q5):
1. The record will be **exactly 4 bytes**, value in **`1..0x32`**.
2. NVRAM `0x2040` on media must read **`1..8`** — `0` and `≥9` route to the nuke
   path (`0x800156E8`).
3. The block the record lands in must keep its co-resident record budget
   (**`L ≤ 172`** for a record sharing `BOOTCTR_FCTRY`'s block).
4. The store must be fully readable (the mount reads **every** block).
5. The unit must be able to boot a rooted OS afterwards, or the rollback window
   (`rm /dev/rpmb/BOOTCTR_FCTRY`) is closed.

**A failed write is not survivable**: the bootloader's write paths blink `0x2F`
and never return (`KNOWN-ISSUES.md` trap 65).  So "we can always retry" is false.

