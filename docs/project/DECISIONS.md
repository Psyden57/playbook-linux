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
It is done by the operator's startup script (`top_up_bootctr`; a source copy at `local/work/devuser-startup.sh`) on the unit, or by the
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

> **Superseded in part by §33 (session 30):** the current diff baseline is
> **7.3.3**, with `local/pristine/linux-7.3.3.tar.xz`.  The 6.15.11 tarball
> remains in the payload as the reference and is still never removed.

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

**Executed (session 27).**  The top-up was performed from Linux: `pbrpmb prep
0x3a` unseals the record block, patches the four bytes to `0x32`, **re-seals it
with the monitor**, checks the seal round-trips, and builds one type-3 frame;
the operator's `pbrpmb write 0x3a yes` sent it, the card accepted it, the
read-back unsealed to `32 00 00 00`, and the restore boot's bootloader consumed
that very record.  Report 175 and its addendum.  The constraint below held and
is what made it safe.

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
exactly one `0x32` write with readback (`SAFETY.md` §2/§4).  **Met and
executed in session 27** (report 175 addendum).  Do not send a seal
with a guessed physical address — the monitor writes 224 bytes where told.
NVRAM and the MCT remain forbidden.

**Evidence.** Reports 143 (keys), 144 (sealed store + known pair), 145 (the
monitor call and `0xFF05`), 146 (static RE: no handshake; `r3` suspect);
`KNOWN-ISSUES.md` traps 59–61.

---

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

**Parked (operator, session 25 end).**  The BB10 port and the PlayBook OS image
(IFS/RCFS) work are set aside for someone else to follow; `HANDOFF.md` returns
to the Linux bring-up.  When BB10 resumes, this control is the first step; its
documents (`BB10-PORT.md`, `BB10-IMAGE-GUIDE.md`, reports 171/172) stay.

---

## 25. The bootloader's load base is `0x80000000` at media `0x9000`; do not reuse its RPMB writer from the chainloader (session 26)

**Decision.**  Treat the code addresses in reports 150/151 as `0x9000` too high
(`real DRAM = quoted - 0x9000`).  Do not pursue the
"call the bootloader's `record_read`/`record_write`/`mount` from our
chainloader" top-up route.  Continue item 6 on the Linux `pbmon` path,
re-checking the seal with the bootloader's own sequence — **domain 5** and
`0x31 -> 0x33 -> 0x2F -> 0x33 -> 0x2E`.

**Why.**  A chainloader dump proved the arm9000 section is loaded at
`0x80000000` from media `0x9000` (DRAM `0x80009000` = media `0x12000`), which
also explains report 151 §Q5's "wipe table anomaly" (the table is valid at
media `0x1D31C`).  The bootloader's tail then wipes
`[0x80025E78, 0x800517C4)`, including its RPMB store state and catalogue, so a
selector call from the chainloader returns `0` (absent); calling its mount
(`0x800082DC`) to rebuild them hung and reset-looped the unit (~10 counter
units).  The data literals in those reports are correct; only code addresses
shift.

**Evidence.**  Report 173 §1–§5; the three probe images
(`0xBD8966DD` dump, `0xA051A23C` selector, `0x984267D3` mount — do not boot the
last); the restored unit (2.1.0.1917, `BOOTCTR_FCTRY = 0x32`).

**Constraint.**  No RPMB write by an agent (`SAFETY.md` §6).  The next
experiment is read-only: re-run `pbmon`'s seal against a known pair with domain
5 before any write, and only then consider the single verified `0x32` top-up.

---

## 26. The region-0 IFS is shipped compressed (session 27)

**Decision (operator, session 27).**  Compress the booting IFS with the QNX
NRV2B imagefs stream rather than leaving it uncompressed: the PlayBook IFS uses
the same `u16` length-prefixed block format as BB10, so `tools/bb10_ifs.py
splice` + `tools/89-crc_tool.py reseal` apply unchanged.  The compressed
container is what is on media now.

**Why.**  The region-0 IFS slot is finite (11.25 MB) and the image work keeps
growing; the stock IFS is compressed for the same reason.  Compression also
cuts a flash from 489 frames to 217.

**Evidence.**  `local/work/ifs-shim.ifs` (our shim imagefs, uncompressed)
spliced into the vendor 1917 container's stream: payload 3 142 560 B for a
7 619 692 B imagefs (41.2 %), `bb10_ifs.py verify` decompresses it back exactly
(117 blocks, `imagefs_ok`, `startup_sum0`), `dumpifs` lists the same 106 entries
with `proc/boot/rfs_validator 2237`, `89-crc_tool.py verify` reports
`RESULT: VERIFIED (CRC path)`, and the unit **booted TabletOS from it**
(217 frames, `/base` mounted, `BOOTCTR_FCTRY = 0x32`).  The artefact is
`local/work/ifs1917-compressed.ifs` (3 466 232 B, sha256 `f14db207…`).

**Constraint.**  The uncompressed `local/work/ifs-shim-sealed.ifs` boots too and
stays as the conservative way back.  Our **Linux** chainloader image is not a
QNX imagefs, so this does not compress it: it would need a decompressing entry
path (RIM's startup blob decompresses the stock IFS; our chainloader replaces
the OS entry), which is a design question, not a tool invocation.

---

## 27. The real root is a BusyBox userland on region 1; the initramfs chroots into it (session 28)

**Decision (operator).**  The region-1 ext4 root becomes a real userland:
BusyBox 1.36.1 (static), BusyBox `init` with `/etc/inittab`, a `rcS` that mounts
the pseudo-filesystems and runs `depmod`, the bring-up console as
`/bin/pbconsole`, `pbrpmb`, and `/lib/modules/$(uname -r)`.  The initramfs
`/init`, after the gadget and the boot-time top-up, mounts the root
**read-write**, bind-mounts /dev//proc//sys into it, `chroot`s and `exec`s
`/sbin/init`.  `PS1='pb> '` keeps `tools/pbcon.py` unchanged.

**Why.**  ROADMAP item 5 asks for "a real root filesystem ... boots to a shell
with modules loadable".  A read-only probe root could not be a userland
(`/run`, `/tmp`, module links), and it kept the initramfs as the only place to
run anything, so every driver test was a kernel rebuild.  With a writable root
and a module tree, the many `=m` drivers can be tried by `insmod`/`modprobe`.

**Evidence.**  `/dev/loop0 on / type ext4 (rw)`, `uname -a` = `6.15.11`,
`insmod`/`modprobe pbhello` and `dmesg`; the 1917 IFS was then flashed and
TabletOS/`/base`/`BOOTCTR_FCTRY = 0x32` verified.  Region 1 is our own data
(§18); the loop must be backed by an `O_RDWR` fd or the `rw` mount is `EACCES`
(trap 92).

**Constraint.**  If there is no `/sbin/init` the initramfs console comes up as
before, so a missing/half-written root never costs the debug channel.  A
`/sbin/init` that hangs is recovered with the power button and the pre-OS
flasher.  The `hd3` cut-over is **parked** (operator, session 28), not part of
this decision.

---

## 28. Peripherals first: mainline drivers where they exist, the QNX oracle for the rest (session 28)

**Decision (operator).**  Before any `hd3` repurpose, bring up the device's own
sensors and peripherals: accelerometer, magnetometer, gyroscope, touchscreen,
the FAN5702 status LED, audio, WiFi/BT/GPS and the battery/thermal parts.
Report **176** is the inventory.  Work in this order: (1) the sensor bus
(OMAP I²C4 `0x48350000`) and BMA150/HMC5883L/MPU3050; (2) CM3217/TMP103/BQ27541;
(3) FAN5702; (4) WL1283 WiFi/BT/GPS; (5) WM8994 audio and the Cypress CY8CTMA3
touch.

**Why.**  Most of these already have mainline drivers (BMA150, HMC5843,
MPU3050, CM32181, TMP103, BQ27xxx, wl12xx, WM8994 codec), so enabling them and
describing the board is cheap; the parts without a driver (FAN5702, NCP5623,
CY8CTMA3 touch, the OMAP4+WM8994 machine glue) are contained RE/driver tasks.
The sensor cluster is a single bus, and the correct one was mislabeled in the
DTS (trap 91), so this is also a necessary correction.

**Evidence.**  Live QNX over the door: `pidin ar` (`sensor-services -d
sensor-accel-bma150 …`, `winchester-lc -d led-fan5702 -d ls-cm3217`,
`io-audio -d omap4abe-wm8994`, `wpa_pps … ti0`, `devlp-ti128x`),
`/base/etc/system/config/sensor_config.xml` (board `Winchester` rev 0),
`/base/lib/dll`, and `pbi2c scan`/`rd` (chip IDs).  The RIM modules are in
`local/work/qnx-libs/` for static RE; the WL1283 firmware/NVS are in
`/base/lib/firmware/ti1283`, the touch firmware in `/base/lib/firmware/munin`,
the GPS patch in `/base/lib/firmware/gps`.

**Constraint.**  One variable per boot; no RPMB writes; the `hd3` repurpose
stays parked.  The sensor interrupt GPIOs are QNX global 34/50/59 (GPIO2
lines 2/18/27 under the Linux numbering) and must be checked against the pad
mux and the GPIO2 reset note (trap 20).

---

## 29. `tools/bbusb.py` is the flasher; the bootrom window is 60 s (session 28)

**Decision (operator).**  Use **`tools/bbusb.py`** for all pre-OS writes;
`tools/pre_os_ifs_flash.py` is the older template tool and is no longer the
runbook.  Raise the bootrom/ramloader open window from 10 s to **60 s**.

**Why.**  `bbusb.py` takes a whole `-v 1,2` container, so region 0 and region 1
can be written in **one** bootrom session (`pack` both, `flash --signed`),
instead of two reboots; it also flashes a bare component
(`flash --component FILE --dest-block N`) and reports `COMPLETE 0x40C0 ->
cmd 0x4006` on success.  The 10 s window is shorter than a graceful BusyBox
shutdown on this unit (~90 s observed), so a flasher started before a reboot
missed the bootrom and reported "No BlackBerry device found".

**Evidence.**  Session 28 wrote both regions in one session:
`pack -l both.lst -v 1,2 -s -f` → 78 709 328 B, `flash --signed --from-bootrom`
→ `COMPLETE 0x4006`, and the unit booted the new kernel + rootfs.  A single
`--component` rootfs flash also worked (`COMPLETE 0x4006`).  The window change
is in `_open_ramloader` (`timeout_ms=60000`).

**Constraint.**  From our Linux shell, `reboot -f` resets immediately and avoids
the slow shutdown (trap 93); a graceful `reboot` can exceed the flasher's
window.  The levers below `O_TRUNC`/`>` on RPMB and all `SAFETY.md` rules are
unchanged.

---

## 30. Identify a peripheral's bus from the reference OS's own process map (session 29)

**Decision.**  Before writing a DT node for a peripheral, ask the running
TabletOS which physical block its driver owns: `pidin mem` prints every
`/dev/mem` mapping of every process, and `pidin irq` prints the vectors it
attached.  The pair names the controller exactly (base address + interrupt
number), with no static RE.  It is read-only and needs no device-side tool.

**Why.**  Report 176 and the session-28 DTS both placed the WL1283 on MMC1 —
wrongly.  `pidin mem` shows `devnp-ti1283.so` holding `/dev/mem 0x480d5000`
(8 KiB) and `pidin irq` shows it attaching QNX vector `0x5b` (= GIC SPI 59,
`mmc5`) plus `0x41d` (`1000 + gpio 53`, the chip's interrupt).  Five minutes of
read-only query replaced an RE task and removed a wrong assumption that would
have cost several boots to disprove on the bench.

**Evidence.**  Report 180 §1; the full `pidin mem` dump also lists the six GPIO
banks and CM2/PRM/sDMA, which is how the WLAN enable line was then found by
diffing `GPIO2_DATAOUT` against report 118's session-5 capture (bit 22 is the
only output that changes when the WLAN driver runs).

**Constraint.**  Only the *first* question ("which block") is answered this way;
pin-level wiring still needs either the driver's static RE or a live
register/pad diff.  Addresses below `0x480d5000`-class L4PER2 targets are not
readable from user mode on either OS (Linux logs an L3 custom error, QNX takes a
SIGBUS), so `pidin mem` is the only route to them short of kernel
instrumentation.

---

## 31. Characterise a peripheral from the OS that owns it (session 29)

**Decision.**  When a peripheral is already claimed by a vendor driver on the
reference OS, **do not** characterise it from that OS: every external I2C access
races the vendor's own transaction sequence.  Characterise it from our Linux
image, where our driver is the only client on the bus, or stop the vendor driver
first if the reference must be used.

**Why.**  The CM3217 investigation produced three false conclusions in a row, all
from QNX-side readings: `Lux::0000` from `/pps/services/light_controller`, `0x6a`
plateaus with `0x00` gaps, and a "covered vs uncovered" difference.  All were
artefacts — the operator noticed the panel's automatic brightness froze *exactly
while we were probing* and recovered when we stopped, because our reads
interleaved with `ls-cm3217`'s own sequence.  Read from Linux, the same part gives
a clean, stable 32 → 191 lux (report 182).

**Evidence.**  Report 182 §2; traps 103 (the race) and 102 (the invalid
cover/uncover run that followed from it).  The complementary case is the WL1283,
where the reference *is* the right place to ask "what does the SoC map" (`pidin
mem`, DECISIONS §30) and the *wrong* place to ask "what is the controller doing"
(only io-pkt can read MMCHS5, trap 101).

**Constraint.**  Comparing register *state* between the two OSes is still valid
and is how the pads, GPIO banks, PMIC and clock manager were cleared — the rule
is about *poking or reading a device a vendor driver owns*, not about comparing
SoC-side configuration, which is inert.

---

## 32. Verify every external fact against a primary source, never recall (session 29)

**Decision.**  Any fact that lives outside this repository — kernel versions,
pmaports package contents and their `arch=` lists, upstream driver internals,
dating of releases — is **looked up at the time it is needed and cited**, never
asserted from the assistant's built-in knowledge.  Recalled facts are treated as
hypotheses, and are labelled as such if they cannot be checked.

**Why.**  The assistant's knowledge is frozen at its training date, and this
project spans months of upstream movement.  In session 29 it asserted that a
kernel version `7.3.3` "doesn't match the 6.x series I'd expect" and was wrong:
`https://www.kernel.org/releases.json` fetched the same minute shows **`stable` =
`7.3.3`, released 2026-11-06**, with mainline at `7.4-rc1` and longterm branches
at `6.18.62`, `6.12.116`, `6.6.162`, `6.1.192`, `5.15.225`, `5.10.274`.  The
operator had to correct it.  The same failure mode had already produced the
session's *other* stale reading — the pmaports snapshot's `pkgver=7.2.2`
`arch="all !armhf !s390x"` was mistaken for the current state of a package that
had moved on.

**Evidence.**  `docs/project/KERNEL-PORT.md` §0 (the fetched release list and
the APKBUILD fields, both dated); the operator's correction in session 29.
Recorded as a working rule in `ROADMAP.md` §"Working rules".

**Constraint.**  The rule is about *external* facts.  Measurements from this
unit (`devmem`, `pbmem3`, `pidin`, the TRM in `hardware/`) and statements in
`docs/` remain the authorities for the device itself — but even there, prefer a
fresh read over an older note when they disagree, and record the disagreement as
a contradiction rather than silently picking one.

---

## 33. The 7.3.3 base is upstream 7.3.3, and the zImage is XZ-compressed (session 30)

**Decision.**  Build **upstream `linux-7.3.3`**, not the
`linux-postmarketos-stable` package, and seed the config from the package's
`config-stable.armv7` with `winchester.config` merged over it.  Compress the
kernel with **`CONFIG_KERNEL_XZ`**.

**Why.**  Current pmaports HEAD (2026-11-07, commit `d58ca1a2`) carries the
package at exactly `7.3.3-r0`, but its `arch="all !armhf !s390x"` still excludes
our 32-bit ARM SoC (the APKBUILD comment: pmbootstrap cannot dynamically choose
a defconfig for armhf).  The package's `config-stable.armv7` is still the best
starting point, but it has `OMAP2_DSS_DSI` **off** and `DRM_OMAP=m`; our merged
config forces both on.  With gzip the chainloader image was **0xB8ABB4**, over
the region-0 IFS-slot bound `0xB00000` (the base container is 0x2BFE10, so the
zImage budget is ~8.6 MB); XZ took the zImage from 9 124 816 B to 6 568 696 B
and the image to **0x91AB20 (CRC `0x0F6AC47F`)**.

**Evidence.**  `git clone https://gitlab.postmarketos.org/postMarketOS/pmaports`
(blobless, depth 1) and `git show HEAD:device/main/linux-postmarketos-stable/APKBUILD`
— `pkgver=7.3.3`, `pkgrel=0`, `arch="all !armhf !s390x"`; the config checks in
`KERNEL-PORT.md` §0a; the three 7.3.3 boots and the exported 41-file patch in
§7.  `kernel.org/releases.json` on 2026-11-07: stable = 7.3.3.

**Constraint.**  Keep the 6.15.11 tree/tarball/patch untouched as the
backup/reference.  XZ is a packaging choice only: any future need to bisect a
compression-sensitive bug should re-test with gzip once the image budget allows
it.  The 1917 IFS is still not on media after this session (the operator ended on
our image), so the way back remains the documented IFS flash.

---

## 34. PlayBook hardware facts come from OS 2.x dumps, never from BB10 (session 30)

**Decision.**  Reverse-engineer the PlayBook's hardware and configuration only
from **OS 2.x** sources: the live 2.1.0.1917 oracle, the 1917 `/base` QNX6 image
(`local/work/rcfs-1917.rcfs`, mounted read-only with
`local/vendor/bb10mt/bb10mt qnx6 mount`), the 1917 IFS tree, and the 2.0.0.4869
IFS dump.  Do **not** use BB10/10.x images for PlayBook hardware facts.

**Why (operator correction, session 30).**  I cited
`local/work/bb10/win1103-base/scripts/startup-support/startup_variables` for the
audio driver argument.  BB10 is for newer devices; its Winchester branch is a
*porting target*, not the PlayBook's own shipping configuration.  The same
argument is already attested from the live 1917 oracle (report 176:
`io-audio -d omap4abe-wm8994 aif=0,secure_capture`), so nothing was lost — but
the method was wrong, and the trees differ concretely: BB10 carries
`lib/libaudio_config.so.1`, the 1917 `/base` does not.

**Constraint.**  The 1917 `.rcfs` file is a **QNX6 filesystem image**, not an
r-c-f-s container despite the name; mount it with `bb10mt qnx6 mount` and treat
it as read-only.  BB10 documents remain valid for the parked BB10 port itself,
not for PlayBook hardware.

---

## 35. Instantiate the TWL6030 BCI charger; the board had no charger node (session 30)

**Decision.**  Use the mainline **TWL6030 BCI charger** driver
(`CONFIG_CHARGER_TWL6030=m`) and instantiate it from the board DTS: a `charger`
child under `&twl` with `interrupts = <2>, <5>`,
`io-channels = <&twl_gpadc 10>`, `io-channel-names = "vusb"` and
`monitored-battery = <&battery>`.  Add the module and `twl6030-gpadc.ko` to the
region-1 rootfs and modprobe them in `rcS`.

**Why.**  The board includes `twl6030.dtsi` (regulators, USB comparator, GPADC)
but had **no charger child**, so the built driver never probed and the module
was not in the rootfs at all.  Nothing configured the BCI under Linux: the pack
ran flat on USB and the unit browned out at 2.817 V / 0 % (trap 108).  The
operator's hypothesis — power management missing in our image — was correct.

**Evidence.**  Board DTS before the change: `&twl` with no charger child;
`CONFIG_CHARGER_TWL6030=m` in `.config`; the 15-module rootfs closure had no
`twl6030_charger.ko`.  The binding
(`Documentation/devicetree/bindings/power/supply/ti,twl6030-charger.yaml`)
requires `compatible`, `interrupts` and `monitored-battery`; the driver's probe
also takes the optional `vusb` IIO channel.  GPADC channel 10 is the TWL6030
internal VBUS channel (`drivers/iio/adc/twl6030-gpadc.c`, and the TWL6032
mainline board uses it).  The DTB with the node compiles and the node is in the
blob.  Candidate images: `0x91AF34DF` + rootfs `ba275807…`.

**Constraint.**  This is **prepared and untested** as of session 30 end: the
unit must charge first, then the test is one variable (see `HANDOFF.md` §1).
If the BCI does not charge, fall back to the values the bootloader left in
`CHARGERUSB_CTRLLIMIT1/2`, and check the actual GPADC channel before changing
the node.

**Superseded in premise by §37 (session 31).**  The BCI is not the board's
charger: the stock OS leaves it disabled and charges through the ISL9519 at
I²C1 0x09.  The probe fix below is correct and harmless, but it cannot charge
this pack.

**Executed (session 31).**  The prepared node was **wrong in one detail and
the driver failed**: `monitored-battery` pointed at the bq27541 gauge node
(`compatible = "ti,bq27541"`), and `power_supply_get_battery_info()` only
accepts a node whose compatible is **`simple-battery`** (or a static Samsung
battery), so the probe returned `-ENODEV` (`Failed to get battery info`) and no
supply appeared.  The fix is a separate label-free gauge node plus
`battery { compatible = "simple-battery"; }` with **no properties**; the driver
then falls back to `CHARGERUSB_CTRLLIMIT1/2` (measured on this unit `0x21` →
4.16 V, `0x08` → 900 mA).  Image `0x306E3DA5` (sha256 `b42f9272…`) boots with
`twl6030_usb` registered: `ONLINE=1`, VBUS 4.81–4.91 V, input limit 500 mA, and
the driver sets `CONTROLLER_CTRL1=0x10` (EN_CHARGER), `VOREG=0x21`,
`VICHRG=0x08`, `CINLIMIT=0x09`.  **The pack still showed no positive charge
current** (0 mA, or −132 mA with the panel on), so the test passes on
instantiation but **not on charging**; the TabletOS cross-check is the next
measurement.  Traps 111/112.

---

## 36. Resume the WL1283 as the second task, after the charger confirmation (session 30 end)

**Decision (operator, session 30 end).**  The next session's **first** task is
to confirm the prepared TWL6030 charger support; the **second** is to resume
the **WL1283**.  Audio/touch (item 9 step 5) comes after.

**Why.**  The WL1283 work was parked in session 29 only for fresh eyes, not
because it was closed.  The single untested mechanism is the **FREF
reference-clock parent**: the pads are muxed but the CM parent/gate was never
enabled by us, and the vendor config wants a 38.4 MHz reference clock with a
CLK_REQ handshake.  Report 180 §6's CM diff suggests the output may already
run, so the experiment is cheap and a negative result closes the route rather
than leaving it open.  Doing it after the charger test means the bench can run
on a charged pack and the result is not disturbed by a brownout.

**Evidence.**  Report 180 §6 (what is falsified and what is left);
`HANDOFF.md` §1.2 (procedure, `/bin/pbhci` probe and decision table); the
operator's order at session 30 end.

**Constraint.**  One variable per boot; do not characterise the chip from
TabletOS (trap 103); a negative result must stop the hunt rather than spawn
another register diff.

**Executed (session 31) — negative, and the route is closed.**  The FREF pad
was muxed (`0x4A10019C = 0x41000000`) and `CM_DIV_M5_DPLL_PER` forced to the
reference's `0x329`, with WL_EN verified high; MMC5 still did not answer CMD5
and UART2 HCI still got nothing.  The write also **changed the DSS clock**
(`dpll_per_m5x2_ck` is the DSS parent) and glitched the panel until a clean
boot restored `0x204`.  The reference's divider value is tied to its own
DPLL_PER setup, so this is not a testable single variable and must not be
retried (trap 116, report 180 §8).  The WL1283 now needs a bus capture or the
chip datasheet, as report 180 §6 predicted.

---

## 37. The PlayBook's charger is the ISL9519; implement an ISL9519 driver (session 31)

**Decision.**  Treat the **Intersil ISL9519** at I²C1 `0x48070000`, slave
**`0x09`**, as the board's charger.  Write a minimal Linux power-supply driver
for it: verify VendorID `0x0049` / DeviceID `0x0003`, set the charge current
(`0x14`), max system voltage (`0x15`), input current limit (`0x3F`) and
control (`0x44`), and **refresh `0x14`/`0x15` at least every 175 s**.  The
TWL6030 BCI node/modules are no longer on the charge path; keep them only if
they are inert, or drop them when convenient.

**Why.**  The stock 1917 OS leaves the TWL6030 MAIN_CHARGE block at its
bootloader reset values (`CONTROLLER_CTRL1 = 0x00`, `VOREG = 0x02`,
`VICHRG = 0x04`, `CINLIMIT = 0x01`) and charges through the ISL9519: the live
registers are `0x14 = 0x0280` (640 mA), `0x15 = 0x1070` (4208 mV),
`0x3F = 0x0180` (384 mA), `0x44 = 0`, with VendorID `0x0049` and DeviceID
`0x0003`, and `/pps/services/power/charger` publishes exactly those values.
RIM's `battery_win` drives it (`isl_9519_init`).  The pack charged on the dock
(2 % → 7 %, 3.33 → 3.683 V; SoH 90), so the pack and the chip work; Linux
never configures the chip, so the output stays disabled and the pack drains
(Intersil AN1570: the output is disabled while the charge-current and
max-system-voltage registers are zero).

**Executed (session 31).**  `drivers/power/supply/isl9519.c` (built-in,
`CONFIG_CHARGER_ISL9519=y`, DTS `charger@9` under `&i2c1`) probes the part,
checks VendorID/DeviceID, writes `0x3E`, `0x15`, `0x14`, `0x3F` in RIM's order
with the masked field steps (charge/input 128 mA/LSB, max system 16 mV/LSB,
min system 256 mV/LSB), and refreshes every 5 s.  Image `0xA0B75C43` charges
the pack: `CURRENT_NOW` +358 → +397 mA, 3.775 → 3.794 V, capacity 8 → 9 %,
still charging at 197 s uptime.  Static RE of RIM's `battery_win`
(`isl_9519_init` @ 0xBB18, `isl_9519_update_regs` @ 0xB1B8) supplied the field
steps, the write order and the ~1.07 s cadence.

**Constraint.**  Words are LO-first.  The 175 s watchdog rules out a one-shot
enable.  The CC/CV/complete decisions are the ISL's own open-loop ceilings
here; a later policy layer (charge current vs temperature/source) can be added
if needed.  One variable per boot; no RPMB write; the `hd3` repurpose, BB10
and PlayBook OS work stay parked.

**Evidence.**  Report 183; the live 1917 PPS and ISL9519 register reads; the
`battery_win` strings; Intersil AN1570 and the ISL9519 data short (FN6773).
Supersedes the premise of §35 (the TWL6030 BCI is not this board's charger).

---

## 38. WDT2: reprogram to the reference's 58.6 s window and pet every 10 s (session 31)

**Decision.**  At boot the kernel reprograms the always-on WDT2 from the
loader's `WLDR = 0xFFFC1800` (7.8125 s) to the reference's `0xFFE2B400`
(58.6 s) and pets it from a 10 s `timer_list`
(`drivers/irqchip/irq-gic.c`).  If the load readback does not match, the pet
falls back to 2 s so the loader's shorter window is still covered.

**Why.**  QNX kicks WDT2 only every ~60 s because its window is 58.6 s; our
bootloader-armed 7.812 s window forced a 2 s pet.  A 10 s pet with the longer
window saves five-sixths of the wakeups and tolerates a ~48 s stall before the
watchdog fires (instead of ~5.8 s).  The load is writable only while the timer
is stopped; the `WSPR` stop/start sequence does that, and the posted writes
must be waited on via `WWPS`.

**Constraint.**  `WWPS` polling after every WDT functional-register write is
mandatory (a fixed delay silently dropped half the sequence).  The
reprogram/pet runs from the **late initcall**, not the inlined GIC probe (that
path did not execute on 7.3.3); the loader's 7.812 s window covers the gap.
The pet is a timer, not a tick (the kernel is tickless at idle).  The expected
`PLAYBOOK WDT` dmesg line is missing on 7.3.3 — verify by `WLDR`/`WCRR`.

**Evidence.**  Session 31: live `WSPR`/`WLDR` writes (with the userland
`devmem` timing) then the kernel implementation on image `0xE36C679A`;
`WCRR` sampled every second reloads every ~10 s (`0xFFE7C494 → 0xFFE328B3`
across a kick) and the unit is alive past 275 s.  Trap 115; report 109 §8.1 +
correction.

---

## 39. The WL1283 FREF request input is closed; the next step is the chip-side checks (session 32)

**Decision.**  Do not pursue the `FREF_CLK2_REQ` input (mux 1 on `0x4A31E058`)
or further SoC-side SDIO register diffs for the WL1283.  The reference leaves
that pad as **mux 3 `gpio_wk31`** (`0x4A31E058 = 0x000B0003`); the one real
SoC-side difference — the SCRM `AUXCLK1` / `fref_clk2_out` generator — was
reproduced exactly on Linux and is not causal (the first test enabled it
after the MMC5 probe; the corrected in-probe test is also negative).  The
boot-time WL_EN 0/500 ms/1 reset pulse was tested (image `0xAF4FC103`) and is
negative.  **The remaining unexamined software layer is the QNX power
manager:** `bin/powerman` runs with `winchester.pm`
(`use_preset_config winchester`), and its strings include
`activity/wifi_driver`, `air_needs_wifi`, `wlan_needs_dma` and
`ldo_vaux3`/`ldo_vaux1`; RE it + `devpm-omap4.so` for a WLAN rail/clock
before trusting the chip-only hypothesis.  Then the measurement at the chip
with the available equipment: with a **multimeter**, VBAT ≈ 3.3 V,
VIO ≈ 1.8 V, WL_EN ≈ 1.8 V, WL_IRQ and the SLOW_CLK DC average (the Motorola
148603 guide; the 26 MHz TCXO is powered from the chip's internal 1.8 V).

**Why.**  A fresh QNX read with WLAN up settles the request-input question
directly.  The first FREF2 output test (images `0xF521AB06` / `0x18D49C22`)
matched `AUXCLK1 = 0x00020100`, pad `0x19C` mux 0 and `auxclk1_ck` 12.8 MHz,
but enabled it **after** the MMC5 probe; the corrected test (image
`0xF38CF87E`) enables it before `mmc_add_host()` and CMD5 still times out.  The QNX driver's protocol differences (24 MHz, CMD5 arg
`0x00FFFF00`) were tested and are not sufficient.  The software audit (report
186) shows the default Linux attempt is one 400 kHz try with CMD52 → CMD0 →
CMD8 → CMD5(arg 0), and the card times out from CMD52 onward.  The chip does
not respond at all (`err=-110`), so the gap is at the chip, not the host.

**Constraint.**  Do not repeat the `CM_DIV_M5 = 0x329` write (trap 116).  Do
not use a runtime MMC5 `unbind`/`bind` (trap 120).  No logic analyser is
available, so the waveform comparison is out; a PMIC register read proves
programming, not the voltage at the chip.

**Evidence.**  Reports 185 and 186; the fresh QNX capture; the five test
images; the static RE of `devnp-ti1283.so` and `devc-seromap-winbt`; the
Motorola 148603 guide.

---

## 40. The powerman preset is not a WLAN power source; the WL1283 needs external evidence (session 33)

**Decision.**  Close the QNX power-manager lead for the WL1283 and do not
re-open it.  The `winchester` preset's WLAN authorities
(`activity/wifi_driver/active|idle`, `air_needs_wifi`, `wlan_needs_DMA`,
`WLAN_NEEDS_PREVENT_OSWR`) only gate the standby shutdown/OSWR decision;
`ldo_vaux1`/`ldo_vaux3` are the display/backlight rails (VAUX3 → LM3532,
REGEN1/2 → panel).  Do not repeat the closed bench experiments: the 200 ms
MMC5 post-power-on settle and the CMD52 skip are both negative (traps 128/
129), as are the earlier FREF/init-clock/reset-pulse tests (traps 116–120,
124).  The next WL1283 step needs a schematic/board view or a logic analyser
(the chip's non-exposed power/clock pins, or OMAP HSMMC sampling); if the
operator formally stops the item, item 9 step 5 (audio/touch) follows.

**Why.**  The preset is compiled into `powerman`: `winchester_environment`
resolves to 94 authorities, 47 edges, 6 dependency groups and 35 initial
states.  Only the standby groups reference the WLAN authorities, and their
action is a PPS shutdown message; `devpm-omap4.so` is the OMAP4 PRCM/voltage/
DVFS plugin and `winchester_phy_ctrl` is the USB PHY.  On the bare board the
chip is alive under QNX (`ti0` UP, `GPIO2_DATAIN` bit 21 = 1) and not under
Linux with WL_EN high (bit 21 = 0); the rightmost front pad follows the chip's
IRQ (1.8 V ↔ 0 V), WL_EN is not brought out, VIO is 1.8 V, and no 3.3 V chip
rail is exposed.  Matching the settle and the first-command sequence does not
make CMD5 answer, so the remaining difference is not host-visible.

**Constraint.**  No speculative rail or pin injection; no repeat of the closed
register experiments.  The `soft_connect` USB re-enumeration workaround is
separate (trap 130) and does not authorise touching the WLAN power path.

**Evidence.**  Reports 187 (the preset tables and authority mapping) and 188
(the QNX/Linux electrical comparison, the two test images and the USB
regression); traps 126–130.

**Addendum (session 34, report 189) — the QNX-side RE is complete, and the
software stop condition is met.**  Every file session 33 left open was read:

* `hw_info_pps` publishes only the runtime-probed
  `/pps/services/hw_info/inventory` (board/eMMC/CPU/audio); it has **no WLAN
  node**, no HWI library, and there is no static hwinfo database in the 1917
  trees.  The `WLAN` node is in the **syspage**; the live dump is
  `WLAN_EN_GPIO=54,WLAN_INT_GPIO=53` with an empty `location` (report 189 §8).
* `hwio-config-winchester.so` (`pad_config` 248 × 20 B, `gpio_config`
  72 × 32 B) contains only the six known WLAN lines (sdmmc5 pads mux 0,
  WL_EN/WL_IRQ mux 3, FREF2_OUT mux 0) and WKUP `0x4A31E058` mux 3 both
  halves; no rail/clock pad.
* `devb-mmcsd-winchester`'s powerman client registers only `-power on/off`;
  no voltage, clock rate or bus power.
* `devpm-omap4.so` registers only `iva_opp`; no MMCHS5/`CM_L4PER_HSMMC5` write
  and no WLAN rail; its `sdmmc5_*` globals are mmap-only and never read.
* `tiwlan_winchester.ini`'s clock/power fields are all firmware-time.

No QNX software layer names a WLAN rail or clock.  The remaining candidate is
the chip's non-exposed VBAT/TCXO pins or OMAP HSMMC sampling, which needs a
schematic/board view or a logic analyser (report 189 §6).  **The operator has
kept the item open (session 34); report 190 is the dossier and report 191
records two external reviews.**  The next work is the QNX
`startup-playbook`/TWL/GPIO audit, the QNX-only GPIO bisect, and H1 (CMD0 →
CMD5 only).  H2's "revert 3.3 V" is withdrawn: MMC5 has no `ti,dual-volt`, so
it always uses SDVS18; the 3.3 V `vmmc` is OCR only.  If it is later stopped,
item 9 step 5 (audio/touch) follows.

---

## 41. The WL_EN reset pulse is removed: it broke USB warm-reset enumeration (session 33)

**Decision.**  Remove the session-32 `pb_wl1283_reset_pulse()` call (and the
FREF2 `pb_enable_fref_clk2()` call) from `omap_hsmmc_probe()`; both were
negative CMD5 experiments.  Keep the functions in the tree marked
`__maybe_unused` until the next cleanup.  The final image is `0x0353A241`.

**Why.**  The pulse (GPIO2_22 low 500 ms → high, `mdelay` in the MMC5 probe)
made the USB host miss the gadget connect on a warm reboot; the host only
enumerated on a physical re-plug.  The warm-reboot A/B is one variable:
`0x90BDE221` (FREF off, pulse on) fails; `0x0353A241` (FREF off, pulse off)
enumerates by itself in 14 s (gadget configured at 5.28 s).  The known-good
`0xE36C679A` enumerates in 12–14 s.  The FREF2 enable is excluded (it is off in
the failing `0x90BDE221`) and the session-33 MMC changes are confined to
`480d5000`.

**Constraint.**  Do not re-add the WL_EN reset pulse as a CMD5 experiment; it
is negative and it disturbs the USB PHY.  A UDC `soft_connect` toggle remains
the fallback if the warm-reset state ever recurs.  The pulse is not a
substitute for the chip-side evidence (report 188).

**Evidence.**  Report 188 §3; trap 130; the five test images and the warm-reboot
A/B.

---

## 42. The QNX-only GPIO outputs are a startup-playbook action; S3 starts with gpio31 (session 35)

**Decision.**  Treat the QNX-only `GPIO1` bits 0/4/15/18/28/31 and `GPIO2`
bits 5/7 as **deliberate `startup-playbook` board outputs**, not a side effect
of FREF or the WLAN driver.  The S3 bisect therefore tests exactly those lines,
one OE bit at a time, and starts with **`gpio31`** (`gpio_wk31`, WKUP pad
`0x4A31E058`, mux 3, input buffer off).  Do not re-open the QNX-side software
inventory for a WLAN rail/clock (report 189 stands); this is a *pin-state*
difference, not a power-service one.

**Why.**  The unaudited `startup.*` (`0x44008` bytes at imagefs `0x908`, raw ARM
at `0x80100000`) applies a runtime-selected board GPIO table whose Winchester
entries mark those GPIOs `direction=1` (output), level 0 (driven low), plus
`gpio 54` (WL_EN) output-low and `gpio 53` (WL_IRQ) input/falling.  That is
byte-for-byte the live QNX OE subset report 191 computed.  Linux's chainloader
never runs startup, so the lines remain inputs/floating.  `gpio31` is the most
plausible board control (WKUP ball, input buffer off) and is the cheapest
single-line test.

**Evidence.**  Report 192; the Winchester table at image `0x80122750` (75
32-byte records); `FUN_8010cb9c` (GPIO) and `FUN_8010ccf4` (pad) decompiled;
the live `GPIO1_OE = 0x6FFB1FAA` / `GPIO2_OE = 0xCE3ECD5F` match.  Startup has
**no** `CLK32KG` write (PMIC voltage setup is the OMAP PRM VC, `FUN_80107748`).
**S3 (session 35) released all eight lines on the live 1917 and every one is
negative** (trap 136), so this is a real but non-causal board-state
difference.  Both-OS TWL reads show `CLK32KG` `CFG_STATE` (`0x8E`) APP-ON in
both (`CFG_TRANS` identical; `CFG_GRP` and the CON/MOD state bits differ;
`0xB0` is REGEN2, `0xBC` an unnamed resource), so the 32 kHz lead is closed
(`docs/contradictions/twl-clk32kg-…`).

**Constraint.**  One variable per boot; restore OE with `ifconfig ti0 down/up`
and the saved word; never leave a line driven wrong.  Do not re-add the WL_EN
reset pulse (trap 130).  The chip-side measurements still follow if the bisect is
negative.

---

## 43. H1 is negative; the WL1283 software surface is exhausted (session 35)

**Decision.**  Do not run further host-side SDIO experiments for the WL1283.
The exact reference sequence — CMD0 → CMD5 only (no CMD52, no CMD8), 24 MHz,
CMD5 arg `0x00FFFF00`, `HCTL = 0x00000B00` (SDVS18|SDBP) — still times out
(`err=-110`, no R4).  The remaining work is the chip-side measurement
(VBAT/TCXO/SLOW_CLK, MMC5_CLK/CMD/DAT0) with a board view or logic analyser; if
the item is stopped, item 9 step 5 (audio/touch) follows.

**Why.**  H1 (image `0x29784F48`) matched the reference's SDIO init in sequence
and clock, and the card is silent from CMD5 (the first response-required
command).  S3 (trap 136) and S2 (trap 135; `CLK32KG` APP-ON in both) are also
negative, so the difference is not host-visible.

**Evidence.**  Report 192 §9; log `local/logs/boot-H1-0x29784F48.log`; the four
H1 source hunks are recorded in the report and were reverted after the test;
trap 137.

**Constraint.**  The H1 image is not the known-good baseline; the unit is back on
`0xE36C679A` (region 0).  Do not repeat traps 116–137.

---

## 44. The touch is on I²C2; mux its pads before any scan or driver work (session 36)

**Decision.**  Treat the Cypress touch as **OMAP I²C2 `0x48072000`** — QNX
`/dev/i2c1`, Linux **`i2c-1`** — at slave **`0x65`**, driven by the board's
`graphics.conf` selection `driver = munin`.  Mux `i2c2_scl`/`i2c2_sda`
(pads `0x4A100126`/`0x4A100128`) to **`0x0110`** (mux 0, input, pull-up) in
`omap4-winchester.dts`; do not scan or bind a driver on `i2c-2`/`i2c-3`.

**Why.**  `graphics.conf` is explicit
(`driver = munin`, `i2c_lib=i2c-resmgr-client,tp_intr=1035,i2c_slave=0x65`);
`libmtouch-munin.so` opens `/dev/i2c1`; QNX's `pidin ar` maps `/dev/i2c1` to
`0x48072000`, and report 176's live scan found `0x65` there.  The
`libmtouch-pegasus` `/dev/i2c2` string belongs to a driver this board does not
select.  On Linux the controller was enabled but **its pads were in safe mode**
(`0x011F`), so every transaction timed out; writing them to `0x0110` (the value
the bootloader leaves on the working I²C1 pads) made `0x65` ACK at once.  This
is the same class as the QNX-only GPIO outputs: board pad setup that QNX's
`startup-playbook` performs and our chainloader does not.

**Constraint.**  The pad write is the durable fix (DTS `i2c2_pins`); the
runtime `devmem` write is volatile.  One variable per boot: `&i2c3` is left
enabled-and-unmuxed.  The part is CY8CTMA3 Gen3; mainline has Gen2/Gen5 only,
so the driver is still open.  `tp_intr = 1035 → GPIO2_3` is inferred from the
QNX `1000 + gpio` convention, not independently measured.  No RPMB, NVRAM or
MMC write is involved.

**Evidence.**  Report **193**; `graphics.conf` lines 61–65; the
`libmtouch-munin` dynamic symbols and `/dev/i2c1`; `winchester_tp_init`
(`0x9120`, GPIO1 `0x4A310000` + GPIO4 `0x48059000`) and `winchester_tp_reset`
(`0x90a0`, masks `0x4000` on GPIO1_14 and `0x100` on GPIO4_8); the live
`i2c-1` of_node, pad reads and `i2cdetect`/`i2ctransfer` result; the resolved
contradiction `docs/contradictions/touch-bus-i2c2-vs-i2c3.md`.

---

## 45. The touch is a Munin-protocol Gen3: write a small driver, not `cyttsp_core` (session 36)

**Decision.**  Write a **new small input driver** for the CY8CTMA3 (for example
`drivers/input/touchscreen/playbook-munin.c`) rather than extending mainline
`cyttsp_core` (Gen2) or `cyttsp5` (Gen5).  The driver speaks RIM's **Munin**
protocol: I²C2 `0x48072000` (Linux `i2c-1`), slave `0x65`, **1-byte register
addressing**, `reg0` = command, `reg1` = status/data.  It must run the vendor
boot: `reg0 = 0x81`, read the 21-byte bootloader block at `reg1`, require
`status & 0x09 == 0x09`, `reg0 = 0x80` (start app), `reg0 = 0x02`,
`reg 0x1D = 1`, `reg 0x1F = 1`, then read **18 bytes at `reg1`** on the IRQ.
The report is a status byte plus four 4-byte contacts:
`X = b0 | ((b2 & 0x0F) << 8)`, `Y = b1 | ((b2 & 0xF0) << 4)`, `b3` = down/id,
`0xFFF` = no contact.  **No firmware upload is needed on this unit:** the live
bootloader block (`ID 0xB0` Pegasus, app `0x020A`, build `0x3B`) matches
`Pegasus_50.hex` exactly.

**Why.**  The live chip boots into its **bootloader** (status `0x09`), not an
app.  The vendor driver's own sequence and report parser are decompiled and
reproduced; the report is structurally valid after the app start (four sentinel
`0xFFF` contacts, counter `0x86`).  Mainline has no Munin/Gen3 support, and the
Gen2/Gen5 register maps do not match.  The driver is small: reset/power GPIOs,
two commands, a fixed report, and an IRQ.

**Constraint.**  Wiring: power **GPIO1_14** (low 500 ms → high), reset
**GPIO4_8** (active low), IRQ **GPIO2_3** (`tp_intr=1035`, inferred), 400 kHz.
The firmware-upload path (`reg0` `0x84`/`0x83`, 0x80-byte blocks, Intel HEX) is
documented but not implemented unless a version check fails.  Do not probe the
whole bus (trap 138).  No RPMB/NVRAM/MCT involvement.

**Evidence.**  Report **194**; the live 21-byte block and app start; the Ghidra
decompile of `tp_reset`, `munin_panel_init`, `munin_sample_xy_data`,
`get_coords`, `tp_firmware_check`, `firmware_upload`; the three `.hex` files
(`Pegasus_50.hex` == `Pegasus2_50.hex`, build `0x3B`; `Pegasus_D4_A.hex` build
`0x35`); report 193 for the bus.

**Executed (session 36).**  `drivers/input/touchscreen/playbook-munin.c`
(input multitouch, 1-byte register I2C, the boot sequence above, the 18-byte
report parser, a 20 ms poll) with the DTS `touchscreen@65` node and
`CONFIG_TOUCHSCREEN_PLAYBOOK_MUNIN=y`.  On image **`0x04AF1433`** (on media)
the driver registers `input0`, boots the controller (`bootloader 0xb0`,
`app 0x020a`, `build 0x3b`) and reports touches on `event0`.  **The probe must
not block:** the controller is not ready while the i2c adapter is still
probing (first read `-EREMOTEIO` at ~2.7 s), and a retry inside `probe()`
delayed the boot and broke USB warm-reset enumeration (trap 141); the boot now
runs in a delayed work that retries every 500 ms.  Remaining touch work: the
axis calibration/orientation (vendor input 1280×768 → 1024×600 panel) and the
IRQ pad.

---

## 46. Re-derive the board DTS from 2.1.0.1917; archive the WL1283 code and restart it from basics (session 36 end)

**Decision (operator, session 36 end).**  The board DTS is to be **re-derived
node by node from 2.1.0.1917** (the live oracle, its `/base`, and the 1917 IFS
tree), not from the 2.0.0.4869 register map the header names.  The
WL1283-specific instrumentation is to be **moved out of the active 7.3.3 tree
and patch into an archive** and the WL1283 started again from a minimal
1917-truth `&mmc5` (pads, WL_EN/WL_IRQ), one variable per boot.  This is the
next session's **first task**; the WM8994 audio experiment follows.

**Why.**  The WL1283 is one of the device's most important parts and the
current approach has not brought it up.  The DTS header still says it was
reverse-engineered from 2.0.0.4869 and the OS 2.0 IFS buildfile, while the
WL1283, display, sensor, charger and touch nodes were later corrected from
1917; a full 1917-truth audit is the systematic way to rule out a missing or
wrong model (pads, `vmmc`/`vqmmc`, pwrseq, clocks, bus properties).  The
accumulated MMC instrumentation (`PBCMD`/`PBCLK`/`PBD`/`PBOCR`/`PBVOLT`, the
`sdio_ocr_quirk`, `pb_enable_fref_clk2`, `pb_wl1283_reset_pulse`, the
`MMC_CAP2_SDIO_OCR_PROBE_FFFF00` bit) also obscures the baseline.

**What this does not change.**  The negative results of reports 180–192 stand
(the pads/GPIOs/PMIC rails/clock manager matched; S3 and H1 were negative; the
remaining candidate was chip-side VBAT/TCXO/SLOW_CLK or OMAP HSMMC sampling).
The redo is a **new derivation**, not a re-run: do not repeat traps 116–137
as-is, and if a new test overlaps one (H1, S3), say what is different and keep
it one variable.

**Constraint.**  Archive before editing; keep the base patch (display, sensors,
charger, touch) clean; no RPMB/NVRAM/MCT writes.  The old code and its
experiment log stay in the archive as the reference.  The touch image
`0x04AF1433` is the current media and the working baseline.

**Evidence.**  `kernel/source/omap4-winchester.dts` header lines 5–6 (the 4869
provenance); the 1917 citations already in the file (the charger comment,
the MMC5 `pidin` note); the instrumentation file list
(`omap_hsmmc.c`, `mmc/core/{core,sdio,mmc_ops}.c`, `include/linux/mmc/host.h`,
the board DTS); reports 180–192; the operator's direction at session 36 end.

**Executed (session 37).**  Task 1 is done: `kernel/archive/wl1283/` holds
`wl1283-instrumentation-7.3.3.patch` (517 lines, round-trip verified) and a
`README.md`; the WL1283 code is out of the tree and the base patch, which is
now **7 305 lines / 46 files**, sha256 `d0d00009…`, verified 0 rejects /
46/46 byte-identical against pristine 7.3.3.  Scope was the **named WL1283
code only** (operator): the SDIO quirk (`sdio.c`, `host.h`), `pb_sdio_ocr_quirk`,
`pb_init_hz`, `pb_enable_fref_clk2`, `pb_wl1283_reset_pulse`, the
`PBCMD`/`PBCLK`/`PBD`/`PBOCR`/`PBVOLT` prints, the `480d5000` PBGPIO dump and
the DTS `vwl1283`/`sdmmc5_pins`/`fref*`/`wl1283_ctrl_pins`/`wl1283-en-hog`/
`&mmc5` nodes.  The general eMMC `PB*` prints and `pb_event` stay (KERNEL-PORT
§7.2's full prune remains separate).  `&mmc5` is now `status = "disabled"` in
the board DTS (the dtsi node has no status and is enabled by default).  The
build is clean and produced the first WL1283-free base image, CRC `0x05819994`
(not flashed).  Task 2 (the 1917-truth DTS audit) is **report 195**: a
node-by-node audit with provenance classes, the corrected header (the 4869
claim removed; I²C1/I²C3 comment fixes), and the `&mmc5` re-derivation (pads,
WL_EN/WL_IRQ, 4-bit, 24 MHz, 1.8 V OCR window, no `ti,dual-volt`, no pwrseq),
each value cited.  The re-derived `&mmc5` is not added to the tree yet; Task 3
is the one boot.  Patch re-exported after the comment edits: 46 files, 7 305
lines, sha256 `d0d00009…`, 0 rejects / 46/46 identical.

**Executed (session 37) — Task 3.**  The re-derived minimal `&mmc5` host was
built (image `0x0DC36A11`, kernel `7.3.3 #24`), flashed region 0 only, and
booted: `mmc4` probes (`f_max=24000000`, 400 kHz init) and **CMD5 still does
not answer** — `attach_sdio no` at 2.829 s, `Failed to initialize a
non-removable card`, no voltage warning (report 196).  The eMMC, the Munin
touch (`event0`) and the rest of the stripped base are healthy.  **The DT
model is not the missing piece; the redo reproduced the old state.**  The
remaining evidence is chip-side or host-sampling (needs a board view or logic
analyser).  A new lead from the operator: WLAN calibration/NVS data may live in
NVRAM or a QNX-exposed partition, to check on the next TabletOS boot (read-only;
it matters for the post-enumeration `wl12xx` bring-up, not CMD5).

---

## 47. The touch uses the 1917 scaling.conf calibration and the GPIO2_3 edge IRQ (session 38)

**Decision.**  Map the Munin report's 12-bit raw coordinates through the 1917
vendor calibration in `etc/system/config/scaling.conf`
(`1024x600:mode=calib`, raw `x 172→1108` → display `102→922`, raw `y 110→658`
→ display `60→540`), driven from DT properties
(`rim,x-calibration`, `rim,y-calibration`, `touchscreen-size-x/y`), and clamp
to the 1024×600 panel.  Wire the IRQ as **gpio 35 = GPIO2_3**, CORE pad
**`0x056` (`gpmc_ad11`)**, mux 3 / input / pull / falling (`0x410b`), with
`interrupts = <3 IRQ_TYPE_EDGE_FALLING>` on the `touchscreen@65` node.  Request
it as a threaded IRQ after the controller enters app mode; fall back to the
20 ms poll if the request fails.

**Why.**  `scaling.conf` is the vendor's own mapping and its first valid
`1024x600` entry is `calib`; the `disp_*` endpoints are inset calibration
targets, so stretching the raw range across the panel would shift the touch.
The IRQ number is the documented QNX `1000 + gpio` convention
(`tp_intr=1035`), and the 1917 `gpio_config` record for gpio 35 is
input/falling/pull (signal `0x16`), the same class as `WL_IRQ`.  The pad offset
is confirmed by mainline OMAP4 names (`0x054` = `gpmc_ad10` = gpio_34, `0x058`
= `gpmc_ad12` = gpio_36).

**Constraint.**  The IRQ fires at the controller's active report rate
(~330–380/s with no finger); that is the chip, not noise — unbinding the driver
freezes the count and rebinding resumes it.  The vendor library attaches an ISR
too (`tp_intr_attach`).  The power/reset lines (GPIO1_14 / GPIO4_8) are still
not driven by the driver; they read output-high after boot and the part answers
without a pulse.  No RPMB/NVRAM/MCT involvement.

**Evidence.**  Report **198**; the decoded `scaling.conf` line; the 1917
`gpio_config`/`pad_config` tables; `devmem 0x4A100056 = 0x410B`; the live
`/proc/interrupts` line and the unbind/rebind freeze; the `event0` drag capture
(left edge `X 0..7`, top edge `X 86→957` at `Y ≈ 0`).

**Executed (session 38).**  Region-0 image **`0x82FC0D35`** (`0x91A9C4`, kernel
`7.3.3 #26`) on media; `Munin touchscreen ready (irq 110)` at 3.453 s.

---

## 48. WM8994 audio: ABE + McBSP, with the WD_TIMER3 target off (session 38)

**Decision.**  Bring the WM8994 up on the 1917 layout: the codec on I²C4 `0x1a`
(16-bit registers), the CPU side on the OMAP4 **ABE/McBSP** (the 1917 driver is
`deva-ctrl-omap4abe-wm8994`, started as
`io-audio -d omap4abe-wm8994 aif=0,secure_capture`).  Use a **machine driver** —
a small board driver or `simple-audio-card` — that `select`s
`SND_SOC_WM8994` and `SND_SOC_OMAP_MCBSP`, and enable `&l4_abe` with the
**WD_TIMER3 target (`0x40130000`) disabled**, after labelling that target in
`omap4-l4-abe.dtsi`.

**Why.**  The codec answers its ID as `0x8994` (report 199).  There is no
mainline OMAP4+WM8994 machine driver, and `SND_SOC_WM8994` has no Kconfig
prompt — it is selected by a machine driver, so `scripts/config -e` alone is
dropped by `olddefconfig`.  The McBSPs are inside `l4_abe`, and the board
already documents that enabling the segment hangs on the WD_TIMER3 target in
ti-sysc; the target has no label, so the board DTS cannot disable it today.

**Constraint.**  The codec needs seven supplies
(`DBVDD/DCVDD/AVDD1/AVDD2/CPVDD/SPKVDD1/SPKVDD2`); map them provisionally to
the always-on TWL `v1v8`/`v2v1` for the probe, and refine SPKVDD when the
speaker rail is identified.  **The ABE boot is the risky one:** the loader's
WDT2 is 7.8 s until the kernel's late reprogram, so a deterministic hang can
reset-loop and drain `BOOTCTR_FCTRY`; one variable per boot, with
`0x82FC0D35`/`0xE36C679A` ready to reflash.

**Evidence.**  Report **199**; the live `0x8994` read; `scripts/startup.sh`
(`SERVICE_audio_winchester`); the 1917 driver's option strings;
`drivers/mfd/wm8994-core.c` (the supply list); `sound/soc/codecs/Kconfig`
(hidden symbol); `omap4-l4-abe.dtsi` (McBSP/WD_TIMER3); the board DTS
`&l4_abe`.

**Executed (session 38) — the ABE segment.**  `wdt3_target` labelled in
`omap4-l4-abe.dtsi`; board DTS `&l4_abe { status = "okay"; }` +
`&wdt3_target { status = "disabled"; }`.  Image **`0x50842767`** booted: the
McBSP1/2/3 targets (`4012208c/4012408c/4012608c`) probe `pm ok`, `40130000`
(WD_TIMER3) is absent from the probe list, uptime 37 s, `BOOTCTR_FCTRY = 0x30`,
no hang.

**Executed (session 38) — the machine link, as modules.**  The card is built as
**modules** (`MFD_WM8994`, `SND_SOC_WM8994`, `SND_SOC_OMAP_MCBSP`,
`SND_SIMPLE_CARD`, selected by a new `SND_SOC_PLAYBOOK_WM8994` board Kconfig)
and loaded from the region-1 `rcS` **after** the initramfs USB gadget.  Image
**`0x3B2ECFED`** + rootfs `07d4e28b…` gives
`0 [WM8994]: simple-card - PlayBook WM8994`; the unit enumerates by itself.
Building the stack into the kernel delayed `asoc-simple-card` by ~1.3 s and
broke USB self-enumeration (trap 149).

---

## 49. WM8994 playback: a board machine driver, FLL from BCLK, and the write blocker (session 39)

**Decision.**  Drive the WM8994 with a **board machine driver**
(`sound/soc/ti/playbook-wm8994.c`), not `simple-audio-card`.  Its DAI link is
`40124000.mcbsp` ↔ `wm8994-aif1` (I2S, CPU bit/frame provider); `hw_params`
sets the McBSP fclk (`12.288 MHz`) and `CLKGDV` so BCLK = 32 fs, moves
`AIF1CLK` to MCLK1, configures **FLL1 from BCLK** (BCLK is a documented
WM8994 FLL reference) at 256 fs, and switches `AIF1CLK` to FLL1.  The card
adds the `Headphone`/`Speaker` DAPM widgets/routes, and the region-1 `rcS`
sets the playback mixer switches with the new `pbmix` tool.  Do **not** clock
the FLL from MCLK1 on this unit until the pin is proven.

**Why.**  `simple-audio-card` cannot do it: its `mclk-fs` path calls
`set_sysclk(codec, 0, …)` and the WM8994 driver's `WM8994_SYSCLK_MCLK1 = 1`,
so `hw_params` fails with `AIF1CLK not configured` (trap 150).  The 1917
driver configures FLL1 from MCLK1 at 12.8 MHz, but FLL1 never reports lock on
this unit (`0x732` bit 5 = 0) although AUXCLK1 (12.8 MHz) and CORE pad `0x19c`
(`fref_clk2_out`) are configured; relocking FLL1 to BCLK locks immediately
(`0x732 = 0x6020`) (trap 151).  A second `hw_params` (`SETFMT`→`CHANNELS`→
`SPEED`) hits `FLL1 is currently providing SYSCLK`; moving `AIF1CLK` to MCLK1
first fixes it.

**Constraint.**  The codec stops accepting register writes after boot (reads
still answer): the regmap cache holds the FLL config while the chip keeps the
reset values, and `regmap_update_bits()` then skips the write (trap 152).
`cache_only` is `N`, the device is `active`, and a plain `component_write`
does not stick, so the FLL stays unconfigured and the stream runs at the OSS
default 8 kHz from the dead MCLK1 — **no audible output yet**.  Do not report
playback as working until the FLL registers are seen in hardware
(`i2ctransfer`) and `0x732` bit 5 reads 1.  The MFD gets a board quirk
(`pm_runtime_forbid`) to stop its reset-on-suspend from complicating the
audio path; it did not fix the write blocker.

**Evidence.**  Report **200**; the live `i2ctransfer`/regmap-cache pair, the
DAPM files, `PM4=0x0300`/`PM5=0x0303`, the `0x732 = 0x6020` BCLK lock, the
1917 FLL1 disassembly, and the WM8994 Rev 4.6 datasheet (AIF1CLK ≤ 12.5 MHz;
FLL reference MCLK1/2/BCLK/LRCLK; `FLL1_LOCK_STS` bit 5).  Traps 150–153.

---

## 50. The WM8994 write "blocker" is the clocking rule: never enable AIF1CLK from a stopped source (session 40)

**Decision.**  Treat report 200's "the chip stops accepting register writes" as
solved and closed: the codec disables **control-register access** while
`AIF1CLK_ENA = 1` and the selected AIF1CLK source is not running (WM8994 Rev 4.6
"Control Interface Clocking").  Do not chase `regmap`/I²C again.  When the
register map must be writable, either keep `AIF1CLK_ENA = 0` (write `R0x200 = 0`)
or have a live clock.  On this board the only live source at power-up would be
MCLK1, and MCLK1 does not clock, so **the audio path is blocked on the codec
clock, not on a write path.**

**Why.**  Measured three ways on the live codec (report 203): clearing `R0x200`
bit 0 restores writes within one write; a software reset does the same; and
`R0x200 = 0x0001` (ENA, SRC = MCLK1) from a thawed codec freezes the next write.
The codec is in that state from boot (the DAPM `AIF1CLK` supply powers with the
`rcS` path and `AIF1CLK_SRC` is still MCLK1), which is why the FLL configuration
at `hw_params` is dropped while the cache keeps it.

**The reference does it in the safe order** (static RE, report 201,
`deva-ctrl-omap4abe-wm8994.so` `FUN_0001decc`): AUXCLK1 gate on → software
reset → PM2/PM1/AntiPOP/bias → FLL1 (`0x224 = 0` MCLK1 ÷1, `0x221 = 0x0700`,
`0x223 = 0x00E0` (N=7), `0x220 |= 0x0004` (FRAC), `0x222 = 0xAE14` (K), delay,
`0x220` ENA, delay) → `0x200` SRC = FLL1 → rate/format → **`0x200` ENA last**.
AIF1CLK is always sourced from FLL1; MCLK1 is only the FLL reference.  It never
polls the lock, never uses the free-running FLL, and never touches the write
sequencer.

**Constraint (measured).**  Replaying the reference's own power-up and FLL
programme — including `FRAC` and the PM1/PM2 bias — still leaves `0x732` bit 5 =
0, and the datasheet's free-running FLL1 does not lock either.  SCRM
`AUXCLK1 = 0x00020100` (gate on, source `sys_clkin_ck`, ÷3 = 12.8 MHz) and CORE
pad `0x19C = 0x0000` (`fref_clk2_out`) match the reference's values.  So the next
evidence is the pin or the FLL's analogue supply (AVDD2), and it needs a QNX
read or a meter — not another driver change.  Also note the DTS supply mapping
is out of spec (AVDD1 mapped to 1.8 V vs a 2.4 V minimum; AVDD2/CPVDD mapped to
2.1 V vs a 2.0 V maximum) and is a guess.

**The reference read (same session, report 204).**  The 1917 IFS was booted and
the codec read live over the door (root ssh, `tools/pbi2c dump16`; the tool
gained 16-bit-register support).  The reference has **FLL1 locked**:
`0x732 = 0x6020` (bit 5 = 1) with `0x224 = 0x0c80` (`REFCLK_SRC = MCLK1`) and
`0x220 = 0x0005` (`ENA|FRAC`), and it keeps **`0x200 = 0x0010`
(`AIF1CLK_ENA = 0`)** while idle.  So MCLK1 **is** clocked on this board and the
codec clock is not a dead end; what fails is our Linux side, and it fails with a
byte-identical codec register file and identical `AUXCLK1`/pad values.  A Linux
boot can also leave `AUXCLK1 = 0x00000100` (÷1 = **38.4 MHz**, outside the
FLL's 0.032–13.5 MHz range) until the codec driver's first
`set_sysclk(MCLK1, 12800000)` calls `clk_set_rate()` (the SCRM value survives a
warm reset, so it is not deterministic); sweeping that divider
(÷1…÷8) on Linux did not lock the FLL either.  **Therefore the next step is a
meter** on pad `0x19C`/the codec's MCLK1 and on DCVDD/DBVDD/AVDD1/AVDD2/CPVDD/
`/RESET` — QNX vs Linux on the same points — not a driver change.  Trap 151 is
corrected accordingly.

**Do not** "fix" this by moving the FLL setup to `.trigger(START)` alone.  The
ASoC order is `.prepare` (all three) → DAPM stream event (sets `AIF1CLK_ENA`) →
`.trigger(START)` (starts BCLK), so the codec clock is enabled one step before
any live reference exists.  A trigger-only change would still freeze at the
DAPM event; it needs the codec's `AIF1CLK` DAPM supply held off until the McBSP
runs (trap 155, session-40 backlog).

**Evidence.**  Reports **201** and **203**; the live `i2ctransfer` pairs, the
regmap tracepoints, `devmem` of `0x4A30A314`/`0x4A10019C`, and the WM8994 Rev
4.6 datasheet ("Control Interface Clocking", "Free-Running FLL Clock", the
recommended operating conditions).  Traps 155–159.

---

## 51. The reference OS is the oracle: reconstruct it before poking the hardware (session 40)

**Decision.**  For any open question about how a device is meant to be driven,
go to the **1917 reference OS** first: static RE of its binaries (REA MCP +
Ghidra), a live read over the door, and a reconstruction committed under
`docs/reference/`.  Do not start with meter/scope work on the unit — the
operator's session-40 direction is that hardware poking is risky and
unnecessary while a working reference exists.

**Why.**  This session's bench work produced one real result (the AIF1CLK rule,
because it is a documented register-level behaviour) and several false starts,
while the reference answered the structural questions directly: the 1917
driver's exact start-up order (report 201), the live codec state with FLL1
locked (report 204), and `startup-playbook`'s own `AUXCLK1`/WDT2/GPIO data
(`docs/reference/startup-playbook/`).  The reference also settled the
`WLAN_EN_GPIO` question: 54/53 (BT 55) is correct for Winchester, and the
151/152, 151/163 and 171 sets belong to board type 4 — a question several
software sessions had left ambiguous.

**Constraint.**  The reference artifacts are proprietary and stay in the
gitignored `local/`; only our reconstruction (addresses, decoded tables,
pseudo-code we write) is committed.

**Evidence.**  Reports **201** and **204**; `docs/reference/startup-playbook/`;
the operator's session-40 correction, recorded in `docs/sessions/session-40.md`.

---

## 52. The board machine driver owns the whole codec chain, and repairs it from a delayed work (session 41)

**Decision.**  `sound/soc/ti/playbook-wm8994.c` configures and, if necessary,
re-asserts **every** WM8994 block the output path needs — word length, FLL,
AIF1CLK, PM1/PM3/PM5, the charge pump, the DAC→mixer and AIF1→DAC routing, and
the analogue output stage — rather than trusting DAPM to do it.  The repair runs
from a **delayed work scheduled at `trigger(START)`** (50 ms), and begins with
`regcache_mark_dirty()` + `regcache_sync()`.  The `.trigger` is kept only for
that scheduling.

**Why.**  DAPM's power-up writes all land *after* the DAPM stream event that
turns on `AIF1CLK_ENA`, which is exactly when the codec freezes (trap 155), so
they are silently dropped; and ASoC offers no hook after the CPU DAI starts
BCLK — the machine `.trigger` is *first* in the trigger order.  A delayed work is
therefore the only point at which the codec is both unfrozen and about to stream.
Separately, because writes were dropped while frozen, the regmap cache holds the
*intended* values and every later `update_bits()` is a no-op until the cache is
discarded (trap 162).

**Evidence.**  Measured: PM3 read `0x0000` while the driver had written
`0x00F0`, and only `regcache_mark_dirty`/`regcache_sync` made it stick; the
`AIF1CLK` re-enable fixed a bug where the driver's own thaw left the DAC without
a clock; the charge pump enable produced the first audible codec output from
this image.  Report 205 §5b/§5m.

**Constraint.**  This is a workaround for the ASoC/clock ordering, not a
correct design: the reference avoids the whole problem by keeping FLL1 locked
permanently from MCLK1 and gating only `AIF1CLK` (report 205 §5g).  If MCLK1 is
ever made reliable, the driver should adopt the reference's model and the repair
work can be retired.

**Honest caveat.**  It addresses the *codec*; it does not fix the remaining
McBSP transmit fault, which is upstream of everything listed above.

**Related: the DT clock rate must be forced, not inherited.**  `AUXCLK1` is not
reset by a warm reset and a cold boot leaves it at `0x00000100` (÷1 = 38.4 MHz,
outside the WM8994 FLL's 0.032–13.5 MHz range), so `assigned-clocks` /
`assigned-clock-rates = <12800000>` on the codec node is part of this decision —
the divider cannot be left to whatever the boot chain happened to leave.
Verified on a battery-pulled boot.

---

## 53. The codec is the AIF1 master and the McBSP is a clock consumer — the reference's roles (session 42)

**Decision.**  The DAI link format is `SND_SOC_DAIFMT_CBP_CFP` (codec
provider), `hw_params` locks **FLL1 from MCLK1** (12.8 MHz) — a BCLK-referenced
FLL is circular in master mode — and the CPU-side SRG calls
(`set_sysclk(FCLK)`, `set_clkdiv`) are dropped.  The `mcbsp2_pins` group makes
`clkx`/`fsx` `PIN_INPUT` (`0x0100`, the reference's live pad values); `dx`
stays an output because the McBSP still drives the data.  The free-running NCO
(`FRC_NCO`, `FRC_NCO_VAL = 0x19`, ±10–30 %) is the accepted **mechanism probe**
but is not a production clock and must not be shipped in the driver.

**Why.**  Experiment E1 (report 206): with these roles, `PCR0 = 0x000f`
(consumer), the SRG is never started, and with a live AIF1CLK forced from the
NCO the transmit FIFO drains — `pbplay` ran 20 s cleanly where every run in the
McBSP-master configuration died at ~1 s.  The session-41 `XRDY = 0` stall is a
property of the **master/SRG configuration**, not of the silicon's transmit
path.

**Constraint.**  The design is gated by a live AIF1CLK: FLL1 never locks from
MCLK1 (register file byte-identical to the working reference) and AIF1CLK
sourced directly from MCLK1 is dead — the pin does not clock under our image.
Everything readable matches the working reference, so the fault is below the
registers and needs the probe the operator does not have (trap 165).  Session
41's single successful MCLK1 lock keeps "marginal clock" alive.  The three
routes forward are recorded in `STATE.md` §What is next.

**Evidence.**  Report **206**; the measured `PCR0`/`SPCR2`/`R0x302` readbacks,
the byte-identical FLL file, the direct-MCLK1 discriminator, and the 20 s NCO
stream.  Traps 166–169.

---

## 54. Run the reference itself: a debloated 2.1.0.1917 image as the hardware oracle (session 43)

**Decision.**  For any behaviour the reference OS implements (clocking for the
WM8994, pads, I²C, HAL start-up), the first tool is a **minimal 1917 QNX image**
we build ourselves: the stock IFS (the session-24 `rfs_validator` shim) plus a
**copy** of the vendor `.1.rcfs` whose `startup.sh` starts only
`filesystem pps random pty net powerman phyctrl usbmgr rimusb` and whose GUI,
app, font, browser and DRM subtrees are deleted.  The image starts `qconn` on
port **9090** and root `sshd`; it is driven with `tools/QCL.py` or
`tools/pbssh.sh`.  Then the reference's own code is run (e.g. `io-audio`) and
the registers are read while it runs, instead of inferring them from Linux.

**Why.**  The MCLK1 fault is below every register Linux can read, while the
reference clocks the codec correctly.  Observing the reference directly is the
highest-yield evidence; the operator's session-43 direction was to stop
guessing and watch the reference.

**How (and the operator's rule).**  `tools/build_minimal_qnx.py` copies the
vendor `.1.rcfs`, mounts the copy read-write with `bb10mt qnx6 mount`, makes
every change **through the mount** (patch `startup.sh`, delete subtrees),
unmounts, and re-mounts to verify.  An OS 2.x `.rcfs` is a **QNX6 filesystem**;
do not use `rcfs_extract.py` (that is for BB10 `.rimf`-style images) and do not
rebuild the filesystem from a tree.  The prune is a live-filesystem win, not a
flash-byte win (the packer skips only `0xFF`).

**Constraint.**  The minimal image is **unauthenticated** (qconn on 9090, root
sshd, packet filter off): bench-only, never a product image and never on a
networked unit.  The UFS `/accounts/devuser` automation is ours; keep it to the
top-up, dev mode, qconn and sshd.

**Evidence.**  Report **207**, session-43; Stage A and Stage B both boot and
answer qconn; `pidin arg` shows the intended services; `BOOTCTR_FCTRY` = `0x32`;
`tools/pbssh.sh` gives `uid:0`.

**Update (session 43, same day).**  The oracle's first use confirmed the model:
clearing the AUXCLK1 gate on the live reference, resetting the codec and running
its FLL1 power-up leaves FLL1 **unlocked** (`R0x732 = 0x6000`); restoring
AUXCLK1 re-locks it (`0x6020`).  So **SCRM AUXCLK1 is the codec's MCLK1**
(report **208**), and the open question is why the identical AUXCLK1 value does
not produce a clock at the pin under Linux.



The repository licence and visibility are decided below; the remaining
policy questions (publication of the proprietary corpus) stay open and are
recorded here so nobody assumes a policy exists.

### Repository license

**Decided (session 23): GPL-3.0-or-later.**  The project's own code and
documentation are licensed under GPL-3.0-or-later (see `LICENSE`).  This is the
simplest licence compatible with `tools/bbusb.py`, a GPL-3.0 derivative of
bb10mt.  The kernel patch and the copies under `kernel/source/` remain
**GPL-2.0-only** (they are Linux 7.3.3 / 6.15.11 derivatives); `NOTICE` records that and
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
other images) and the whole `hardware/` directory (the TI TRM text/PDF and the
TWL6030/TC358764 datasheets) are gitignored as blocks — nothing there is tracked
or published.  Whether, when and in what form any of this is published is open;
the default in force is "device-derived and vendor material stays local".  The
reports quote TRM page/line references precisely so
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

