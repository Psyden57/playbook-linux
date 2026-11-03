# Known issues

## 1. The display works

**Resolved 2026-10-29 (session 15, report 133).**  The panel shows a clean,
stable image — solid colours and the kernel console as text.  Three defects had
to be fixed, each necessary:

1. **Bridge delivery** (session 14): the DSI HS clock was never started; fixed
   with the reference's VC0/HS null packet (report 131).
2. **Video configuration and panel TCON** (session 15): our `VP_HTIM1/2` kept
   the LVDS porches in the reserved top byte, so the bridge held HBP = HFP = 0
   (report 132 §5), and the Hitachi TCON table was only ever sent by hand.  The
   porches are `0x00780001`/`0x00760400` and `/init` sends the table at bring-up.
3. **The DISPC core clock** (report 133): the DSS functional clock was 48 MHz
   while the pixel clock is 50.4 MHz, so the controller could not service the
   pixel stream; the picture was correct on the left of each line and failed
   progressively across it.  `dss_probe()` now asks the PER PLL for the
   reference's rate and gets 96 MHz.

**Two measurements that misled earlier sessions are withdrawn.**  `LANESTATUS0`
and `SYSSTAT` read 0 on a **working** TabletOS display (report 132 §4), so they
were never fault indicators; and the session-13 "partial live image" was never
ours (report 129 §5).

The history below is kept because the method matters: every real defect in this
chain was found by reading the same register on a working system and diffing,
never by reasoning about undecoded bits.

### History: what was falsified or settled along the way (reports 124–133)

* The **timing-encoding**, **PHY-word** and **DISPC I/O-pad** hypotheses
  (report 124).
* **"The bridge needs no programming"** — the reference programs it at
  `winch_lcdctl 0x13e50` (report 125).
* **The panel is a Hitachi** — `GPIO6_DATAIN = 0x00010800` (bit 16 = 1) and
  slog `LCD is Hitachi` on TabletOS.
* **The PLL, the DSI block, the PHY, `DSS_CONTROL` and every PMIC rail** match
  the reference.
* **`PPI_TX_RX_TA (0x013C)` decides whether bridge reads answer**, and it is
  latched by `PPI_STARTPPI`: `4` → 50 % failures, `0x00040006` → none
  (report 132 §3).
* **The TCON byte order is high-first** (the reference's): both orders drive the
  panel, but low-first swaps magenta↔green and black↔white (report 132 §8).
* **`dss_setup_default_clock()`'s "clk rate mismatch: 48000000 != 128000000" was
  the display bug**, not a harmless warning (report 133 §2).

### Session 18: the boot log is clean (report 142)

* **`DSI CIO error, cio irqstatus 800000` is benign when it is one per bridge
  read.**  `0x800000` is bit 23 = `DSI_CIO_IRQ_ERRCONTENTIONLP1_2`, the LP
  contention of a DSI generic read's bus turnaround; it fires on successful
  reads too (the 121 boot errors were exactly the 121 `pb` reads).  The
  boot-time probes are now behind `pb_probe` (default off).
* **The duplicate `pb_reg` sysfs warning** (the encoder is probed twice) is
  fixed by creating the attribute through the driver's `.dev_groups`.
* **`clk rate mismatch: 48000000 != 128000000` is fixed**:
  `dss_setup_default_clock` uses `clk_round_rate` on winchester and
  `dss_probe` records the real 96 MHz.
* A normal boot still contains only benign messages: dummy regulators, the
  optional-DT GPIO lookups, `hw perfevents: no irqs for PMU`, clkdev's
  `device ID is greater than 24`, `Warning: unable to open an initial console`,
  and the `/dev/kmsg` binary record (still open, §4).

## 2. Operational traps

Condensed from `HANDOFF-2026-10-27-session6.md` §5; numbers are stable and are
referenced by the other docs. Long-form versions: bootstrap §6, report 120 §3,
report 121 §3/§11/§12.

14. A **write to a DSS target in module standby never completes** — it wedges the interconnect and PID 1's `reboot()`. Let `omapdrm` own the DSS; do not poke it.
15. `/init` used to auto-reboot after 180 s.  **Session 15 removed it** <!-- audit-ok: trap history -->
(operator decision: it existed only because there was no console, and every iteration spent a factory boot).  Our images now stay up until the console asks for a reboot; the power button is the fallback.
16. **Blocking I²C is the same hazard.** Everything that touches I²C runs forked with a bounded wait; a kernel probe at `0x38` while the slave was gated did it too.
17. **A stale door holder looks like `Connection refused`** — after a *host* reboot the supervisor is gone entirely (start `door-hold.sh`); after a *unit* reboot kill the stale holder by explicit pid, never `pkill -f`.
18. Driving `regen1` (`GPIO1_2`) without detaching it from the PMIC first (`i2c w 0 48 af e0`, `i2c w 0 48 ad 00`) wedges the I²C bus.
19. A `slay` test does not work on this backlight — the LM3532 latches brightness.
20. **GPIO2 is reset by our boot path; GPIO4 is not.** Never assume a reference-high line is high on Linux; always read the pad mux.
21. **A GPIO write to an un-muxed GPMC ball is a silent no-op.** Check `peek 4a1000XX` → `[2:0] == 3` before believing any GPIO test.
22. **`regulator-fixed`'s GPIO loses the race with async probe** — use a gpio-hog for anything that must be true before a device probes.
23. **`bl_power = 4` silently swallows brightness writes** (`actual_brightness` still echoes your value). The panel's `backlight = <&backlight>` link fixes it.
24. **`/dev/ttyACM0` is recreated per enumeration** — `pkexec chmod 666` after *every* boot.
25. **The flasher's bootrom window is 300 s** and it exits silently.
26. **`pmic-twl6030-ctl set_state vaux1 off` takes the unit down** (needed a power cycle). Read rails; never toggle VAUX1.
27. **A flash cycle's precondition is media running, not the ramloader** — in `0fca:8001` there is no `/dev/ttyACM0`, so a console reboot is a silent no-op and the flasher waits out the ramloader's ~90–120 s timeout (the "two minutes with the green LED").  The flasher now resets the loader itself after the write (`leave_loader()`; tested in session 13), so the manual `force_reset_loader.py` step is only a fallback.  The pre-flash reboot is still started by hand.
28. **Panel-chain writes outlive a warm reset and QNX does not undo them.** A wrong-byte-order TCON table and a full `SYS_RST <- 0x1f` both left the *reference* display with faint vertical artifacts, cleared only by a **cold battery pull**. Prefer `spi tcon`; never `SYS_RST`.
29. **A file-existence check does not prove an overlay tool runs.** `dd` lived in the devuser overlay and `-x` passed while every exec failed with `cannot execute` (its interpreter is created at boot too). Test by **running** it.
30. **`--reuse-loader` can fail** (`set_mode(2) failed`) and strand the bootrom with a **red blinking LED**; recover with a plain `--template-ifs <sealed>` flash, no power cycle needed.
31. **Non-burst DSI video mode is rejected at this pixel clock** — `[drm] Cannot find any crtc or sizes`, no `/dev/fb0`, DSS stays in standby. Keep BURST.
32. **`pgrep -f`/`pkill -f` match your own command line** — use `ps -ef | grep '[p]re_os_ifs_flash'`.
33. **A wedged image used to reset-loop every ~7.8 s, and every iteration was another factory boot.**  Since session 15 `/init` no longer reboots itself, so this only applies to an old image.  A wedged *new* image stays up and costs one unit; the restore flasher's `--delay` is still what catches a loop instead of a stale bootrom.  Read the counter at both ends of a cycle, and treat a `0x32` reading as "the top-up ran", not as "nothing was spent" (`SAFETY.md` §2, report 109 §8.1, session-10 §3).
34. **QNX's `-t/-T` bridge self-test leaves the panel in a grey vertical-line test pattern.**  It is a diagnostic, not part of the display path; do not run it at boot.  To make it run, `winch_lcdctl power_off` first, then `winch_lcdctl -t <n> power_on`; `-T 1` alone returns before the test.  While it runs, `GPIO2_DATAIN` bit 4 (GPIO36) toggles — that is the valid delivery indicator (report 127 §2).
35. **`unbind`/`bind` of the TC358764 disables the whole display output**, not just the stream: after the bind, `DSI_CLK_CTRL` loses its PLL power bits, `VC_CTRL(0)` loses `VC_EN`, and `LINE_STATUS` stops.  Frames seen during the bind are the output restarting, not a TCON result.  Judge the panel on a fresh boot, not after an unbind/bind.
36. **A null packet written to `VC_SHORT_PACKET_HEADER(vc)` in *video* mode never completes.** `PACKET_SENT` times out (`polls=2000`).  The reference's clock-start packet needs VC0 in **command** mode (bit 4 = 0) with `MODE_SPEED` (bit 9) set; `0x20800791` is the wrong word (bit 4 = 1) and hangs the wait.
37. **A failed BTA clears `VC_EN` on that VC behind the driver's back** — `VC_CTRL(1)` reads `0x20001DC0` afterwards.  Any probe that reads the bridge must re-establish the VC (`pb_vc_setup_lp()`) before the writes it is supposed to be measuring.  Note: the re-arm does **not** change the read *success* rate — that is `PPI_TX_RX_TA` (trap 44; report 132 §3).
38. **Whole-word stores to `VC_CTRL(0)` destroy `dsi_vc_initial_config()`'s fields** — the DMA/no-DMA bits (29:27, 23:21) and `OCP_DATA_BUS_WIDTH` (11:10).  Always read-modify-write.  A whole-word store silently reverted an OCP fix in image `0xA3A24D73`.
39. **A failed link leaves the previous image in place, with the previous CRC.**  If a build prints the same CRC as the last one, check the build output — it probably failed (`udelay(n)` with n > 2000 is `__bad_udelay`, an undefined reference at link time).  Session 14 lost a cycle to this.
40. **The console's `write <path> <text>` passes only the first token.**  A sysfs interface driven from the console must take a space-free argument (`w0524:0`, `r0214`), not `w 0524 0`.
42. **The TC358764's I²C slave is not wired.**  `pbi2c probe <bus> 0x0f` returns **NAK** on `/dev/i2c0`, `/dev/i2c1` and `/dev/i2c3` on TabletOS, so I²C is not a second route into the bridge.  Also note QNX's third bus is at `0x48350000`, which our DTS does not enable (ours is `0x48060000`).
43. **`local/kernel-tree/.config` can name a different initramfs tree than `kernel/winchester.config`.**  It did until session 15: it said `CONFIG_INITRAMFS_SOURCE="$HOME/rea-work/…/initramfs/root"`, so every image built here embedded *rea-work's* older `/init` and no change to this repo's `initramfs/init.c` took effect (which is why the 180 s reboot survived several edits). <!-- audit-ok: trap history -->  Check the line, and verify the built cpio — `cpio -itv < usr/initramfs_data.cpio` must show this repo's `init` with the current size/mtime.
44. **`PPI_TX_RX_TA (0x013C)` is latched by `PPI_STARTPPI (0x0104)`.**  Writing it after boot changes nothing; the value must be correct in `pb_bridge_seq`, eight entries before the `0x0104` write.  `4` (the reference's) leaves bridge reads failing ~50 %; `0x00040006` makes them 8/8 and 16/16 (report 132 §3).
45. **The DISPC core clock must be at least the pixel clock, and this board's boot chain leaves it below.**  The DSS functional clock is the DISPC core clock; it was 48 MHz against a 50.4 MHz pixel clock, which corrupts the right-hand part of every line while the left stays perfect.  `dss_setup_default_clock()`'s *"clk rate mismatch: 48000000 != 128000000"* is that bug, not a harmless warning.  `dss_probe()` now sets it to 96 MHz (report 133).  **Caveat:** the fix runs at probe time and moves the PER-PLL output dividers, so a driver that computed a divider before the DSS probed will be off — re-check the eMMC clock (M2 did not move) before relying on storage, and prefer making this a clock-initialisation property.
46. **Reading DSS/DSI registers from userspace perturbs the live picture.**  Repeatedly observed with `peek` of the DSI block and with the DSS register dumps.  Do not judge the panel while probing (report 133 §6).
47. **The MCT/loader 64 KiB block numbers are NOT raw `mmcblk1`/`hd0` offsets.**  The measured rule is `raw byte = (block - 0x15) * 0x10000` (`0x15` blocks = `0x150000` = 1.3125 MiB), verified at five points in session 16 (report 134 §5): media LBA 0, region-0 sector 0 (`0x8000000`), the IFS (`0x8100000`), the region-0 RFS table (`0x8C00000`), region 1 (`0x28000000`).  The old "media offset" column (`block * 0x10000`, in `SAFETY.md`, report 100 and the flasher's log) is 1.3125 MiB high.  **The flash loop is unaffected** — the flasher and the loader both use loader-block coordinates — but any raw offset for `/dev/mmcblk1` must use the shifted rule (compute it with Python).
48. **The flash loader strips the mfcp/qcfp container header.**  The payload starts at the destination block; the first bytes on media are the image's own (`fe 01 00 ea …`), not the template header.  Do not compute an image's media position from header sizes.  (Report 134 §3.)
49. **Linux eMMC writes are only as large as the writer makes them, and were never 512-byte-limited by the driver.**  The driver advertises `max_blk_count = 0xFFFF` and 64 segments; the measured ceiling is 256 KiB per request (64 × 4 KiB SDMA segments).  An `O_DIRECT` write gives 256 KiB multi-block `CMD25`s at **5.5–7.0 MB/s** (4 KiB chunks 0.93 MB/s), with one `CMD13` per request from `mmc_blk_card_busy` — normal.  The old "single 512-byte `CMD25`, ~17–32 KB/s" was the console's `recv` `pwrite`ing one tty-sized buffer at a time, plus 4–7 unthrottled `pr_info`s per request to the **fbcon** console (~25–30 ms each).  Write ≥64 KiB `O_DIRECT` blocks with the MMC trace off.  (Report 136.)
50. **`accounts/devuser/startup.sh` exists in three places** — the live UFS, the UFS image backup in `PB-4869-root.exe`, and `bench/`.  An older copy silently stops the `BOOTCTR_FCTRY` top-up: its first `dd` runs before the devuser overlay interpreter exists (trap 29), the guard refuses to write, and the factory budget drains (0x32 → 0x2d observed).  The good script is 4490 B, sha256 `c265cfbb…`, and waits for a working `dd` first.  (Report 135 §5.)
51. **The ACM console is not a bulk transport.**  It drains at ~134 KB/s and **drops bytes** when the host blasts: `sink 16777216` lost ~768 KiB, and `sink 1048576` never completed after a 0.08 s host blast.  The gadget is enumerated at High Speed (480 Mbps), 512-byte bulk packets, so this is not an enumeration limit.  `recv` therefore corrupts multi-MB images (an 11 159 480-byte test read back `25a2c15560ac5bda` vs the file's `473dcd0176320fba`).  Bulk images go through the pre-OS `0xF7` loop or QNX/`bb10mt`.  (Report 136 §3.3/§3.4.)
52. **Nothing on this board is free to print per I/O.**  The only console is tty0 (fbcon) — the DTS deliberately has no `stdout-path` and printk draws synchronously — so per-request logging materially slows the path it measures.  Keep `PBREQ`/`PBIRQ` behind `/sys/module/omap_hsmmc/parameters/trace` (default off); with it on, `PBREQ` prints `R/W blksz blocks bytes sg_len dt`.  Use the DRAM ring for high-rate events.  (Report 136 §4.)
53. **`blk info` reports `size 0 B`.**  It reads `fstat().st_size`, which is 0 for a block device; it needs `BLKGETSIZE64`.  Cosmetic (the device is 59.4 GiB); use `blk sum`/`sumb` for real work.
54. **A smaller hd3 filesystem is reformatted and reseeded at boot.**  `rootfs.sh` checks `/.rootfs`; if the boot did not accept the R/W FS as seeded, it runs `mkqnx6fs -E -b 4096 -r 5 -q /dev/hd3` at the full partition size and copies `/base/seed/rootfs`.  The base seed is unprovisioned (`var/pps` has no `system/`), so the unit shows the setup wizard.  A 1 GiB FS from the UFS factory tree does exactly this (measured: hd3 became a 58 GB 2.0.0.4869 seed with 238,656 inodes).  An FS-only shrink is dead: the partition must change (MCT).  hd3's raw start is **`0x49000000`**, and `console: qnx6` mounts it read-only.  (Report 138.)
55. **The pre-OS loader uses the V1 `qcfp` run record, and its offset is *region-relative*.**  The destination is `region_start (0x815) + qcfp.run[0].offset`; the V2 `rrcq` is ignored on this unit.  Vendor autoloaders are built with **`bb10mt pack -v 1,2`** (both headers); a `-v 2` container has a V1 `mfcq` header but **no `qcfp` block** and its first frame is rejected with status **`0x11`**.  Patch **both** records: V1 offset is `dest - 0x815`, V2 offset is `dest - 0x805`.  Session 18 left the V1 record at `0x10` while sending a 32 MiB payload meant for region 1, so it was written at region-0 block `0x825`: the sealed IFS and the first ~21 MiB of the RCFS at `0xC00000` were destroyed (the unit red-LED'd, then booted the splash and reset-looped).  Recovery: sealed IFS at block `0x825` (`--template-ifs`), then the RCFS head from the vendor `PB-4869-root.0.1.rcfs` at block `0x8D5` (`--payload … --dest-block 0x8D5 --allow-region0`).  The vendor `.lst` offsets are region-relative 64 KiB blocks (`ifs=16`→`0x100000`, `rcfs=192`→`0xC00000`).  (Report 139.)

56. **`DSI CIO error, cio irqstatus 800000` is one error per DSI bridge read, and is benign.**  `0x800000` is bit 23 = `DSI_CIO_IRQ_ERRCONTENTIONLP1_2`, the LP contention of a generic read's bus turnaround; it fires on **successful** reads too (the 121 boot errors were exactly the 121 `pb` reads).  The boot probes are now behind `pb_probe` (default off); the video VC0/HS path is unaffected.  (Report 142.)
57. **`pb_reg` must be created once, through `.dev_groups`.**  The encoder is probed twice (the DT dependency cycle defers the first probe); a manual `device_create_file` hit `sysfs_warn_dup` and tainted the kernel (`G W`).  (Report 142.)
58. **`/sys/block/*/size` and the loop's "detected capacity change" are 512-byte sectors, not bytes.**  124,583,936 sectors = 59.4 GiB; a loop over `/dev/mmcblk1` at `0x29000000` exposes 123,240,448 sectors = ~58.76 GiB.  Do not read them as 118 MB.  (Report 142.)
59. **Never read another process's address space through `/proc/<pid>/as` with `lseek()`+`read()` on this unit.**  It is the documented QNX interface, but a read at a virtual address faulted the procfs server ("Server fault on msg pass") and took TabletOS down with an unplanned reboot (session 19).  Use `DCMD_PROC_MAPINFO`/`DCMD_PROC_PTINFO` — metadata queries, the same class `pidin` uses — to get the virtual→physical mapping, then read the physical page with `memdump3` (`tools/qnx-procmem.c` → `pbmem` is the metadata tool).  (Report 143.)
60. **The block HMAC is over the *logical* view and the frame MAC over the *wire* view; the frame data field's orientation depends on the direction.**  The record block's 32-byte HMAC is `HMAC-SHA256(seed[0:16], logical[0:0xE0])`, stored at `logical[0xE0:0x100]`.  The frame MAC is over the wire view (`logical` reversed) with the tail fields **big-endian** (the page stores addr/count/result little-endian: `3b 00`, `01 00`, `00 00`; the type bytes `00 04` are type 4 in wire order).  On a **write/probe** frame `frame.data` is the wire view, so `logical = reverse(data[256])` (report 143).  On a **read response** the resmgr leaves the **logical** view in `frame.data`, so the block HMAC verifies directly and the wire view is `reverse(data[256])` (report 156).  Patch in the logical view, re-MAC it, reverse to wire, then MAC the frame; any other order corrupts the block (the bootloader then treats it as damaged).  Verified against real captured blocks (reports 143, 156).
61. **The 224-byte RPMB record block is *encrypted on media* by the secure monitor; the MACs cover the ciphertext.**  The resmgr unseals each block with `smc` service `0x2F` (op 0, direction 0); the write path seals with direction 1 (`0x2aa4`/`0x2bf0`).  The `0x33` key is `page+0x20` = `86 78 7c 2a 63 2f be 16 48 ec ab a6 d8 84 01 09`, but no standard AES mode with any known key reproduces a known pair, so the cipher is monitor-side.  **Do not patch a plaintext block**: the unseal yields garbage and a wrong `BOOTCTR_FCTRY` value is the brick path.  The Linux-native top-up therefore needs the monitor's `0x2F` seal; otherwise QNX's `/dev/rpmb` stays the only proven writer.  A **genuine same-boot pair** on the reference (report 156) still does not match AES/DES with the visible seed/frame key, so the `0x2F` cipher is monitor-side.  (Report 144; refines report 141 §7.1, which assumed a plaintext block; report 156.)
62. **The monitor reads identity physical addresses, and it cannot reach `0xBF040000`.**  There is **no `−0x10000000` DRAM bias** — that was QNX's own physical-address offset (`QNX` maps DRAM at `0x90000000`, so its `paddr_t` values are `+0x10000000` relative to the TRM).  Passing the biased address made the monitor read a *different* page and return `0xFF05`; passing an identity address into a page below the window returns 0 and fills the buffer (`0x12`, probe 6: `r3=0xA0200000`, ret 0, nonce in that page).  `0xFF05` is therefore a **page-validation error, not a caller rejection**; a differently malformed page returns `1`.  Keep every monitor page — command page, descriptors, data, key — **below `0xB0000000`** (`0xA0200000` works, `0xBF040000` hangs the bus).  The domain is irrelevant (`0`/`4`/`5` behave identically).  Two related facts: user-mode `/dev/mem` access to `0x4A009580`/`0x4A004304` gets `SIGBUS` from the L4 firewall (`TARGET GPMC … User mode`) while the kernel's privileged access works, so read those registers from kernel code; and the QNX `0x2F` descriptor does carry `−0x10000000` values, which is what made the bias look right.  The veneer (`r6=0xff`, `r12=0`, ICIALLU, `smc #1` = `0xe1600071`) and the CM2 L4SEC setup are correct; session 19 also wrote the wrong second PRCM register (`0x4A004004` instead of `CM_MPU_STATICDEP` `0x4A004304` bit 14).  **Never** send a seal with an unvalidated address (the monitor writes 224 bytes where told).  **Confirmed live (report 147): `0x12` ret 0 with the nonce in the low page.**
63. **The monitor accepts the whole seal ABI and still does not reproduce the store.**  With the identity page, `0x12`/`0x31`/`0x33`/`0x2F` all return 0 and `seal(unseal(C)) == C`, but neither direction reproduces the block-0x43 pair; `op=2` is refused (`-EIO`) so `op=0` is right; the key is used (changing it changes the output); all 512 blocks × 6 framings × 5 keys and 10 560 offline AES combinations give no record magic.  The `0x2E` path shows the `0x33` key pointer is `[obj+0x30]` — the same pointer that carries `seed[0:16]` for the verified block HMAC.  **Two operational traps found the hard way:** the misc report nodes are **non-seekable**, so userspace must reopen them per read (a reused fd leaves the offset advanced and the next read returns nothing — this looked like a driver failure for two boots); and after a **warm reboot** the `0x12` nonce fill returns 0 but writes **16 zero bytes** (536 logged transforms), so a transform result is not trustworthy unless the nonce is known to be live.  The pair was recaptured on one boot on the reference (report 156): both MACs verify and the `0x2E` byte order is pinned, but the `0x2F` still does not match AES/DES with the visible keys (36 162 AES trials).  (Report 148; report 156.)

64. **A successful RPMB write proves storage, not semantics.**  Session 20 wrote a *captured, genuine* `BOOTCTR_FCTRY = 0x32` block (plus `TEST_PAD`) back to block 2 from Linux: the device accepted the frame (`result=0`) and the read-back was byte-identical — and the unit then stopped booting and stopped enumerating on both Linux and Windows (report 149).  A read-back compares the bytes we sent; it says nothing about whether the record still *unseals* to a valid value for the bootloader.  Note also (report 154 §Q2) that a *failed* write is just as fatal: the write paths blink `0x2F` and never return, so there is no "it failed, we keep booting" path.  **An agent must never write RPMB** (`SAFETY.md` §6): no top-up, no replay, no rehearsal record.  Also: a rehearsal that succeeds is not authorisation for the real write — ask again.  And when a unit stops enumerating, eliminate the host first (both hosts here used the same AMD xHCI controller), then power, before blaming the store.

65. **The red LED is an error-code display, and the code identifies the failure class.**  A FAN5702 I²C LED driver (7-bit address `0x36`, red channel) is driven by `0x80014C98`, which emits an 8-bit code **bit-serially**: `1` = ON 20 units, OFF 20; `0` = OFF 40; then OFF 200.  One unit ≈ 15.3 ms.  So **1-2-2 = `0x5B`** = "NV-record / RPMB subsystem unusable" (the dead unit's pattern), **1-4 = `0x2F`** = a record **write** failure, and the other codes are tabulated in report 153 §Q2.  The emitter **never returns** and kicks WDT2 ~48 000× per cycle, so the unit is *not* reset-looping and can never present USB — do not wait for a bootrom window (report 153).
66. **The RPMB record store's on-medium format, corrected.**  The record header is **24 (`0x18`) bytes, not `0x1C`, and has no `next` field**; the record magic is **`0x5983BC2D`**; a `slot` field at `+0x12` chooses the block a record is read from; records are located by scanning for the magic on 4-byte boundaries; two records per block are normal and the tail need not be zero.  The mount **reads every block `0..nblocks−1`** and fails if any block cannot be read/authenticated or any block's tiling overruns 224 bytes.  Budget: `BOOTCTR_FCTRY` (len 4) next to a co-resident record of length `L` requires **`L ≤ 172`**, else the mount fails permanently.  A record that is not exactly 4 bytes, a wrong `slot`, a shadowing duplicate, or a value outside `1..0x32` each reach a different `0x5B` site (report 154).

67. **`trustzone-omap4`'s raw SMC pass-through is broken on this restored root.**  The root-only devctl is **`0xC0280501`** (report 107 §5.1's `0xC0285001` is a typo; the binary has `movw 0x501` / `movt 0xC028` at `0x8a5c`/`0x8a60`).  The resmgr starts on a clean boot and **standard** devctls (`DCMD_ALL_GETFLAGS`) work, but the `0x5001` pass-through **segfaults in libc** (`libc+0x91f8`): `devctl` returns **3** (`ESRCH`) and slog records `run fault ... signal 11`.  Reproduced with 40- and 106-byte payloads, with the stock binary, a copy patched to bypass `iofunc_client_info`/`iofunc_check_access`, and a copy with `msg_max_size = 0x1000`.  The boot-time `random` driver's devctl crashes it too (`entropy_source_start_thread: rng_hwRNGen() failed`).  So the trustzone shortcut for `0x12`/`0x2E`/`0x2F` is **not available**; the arm gate remains.  Tool: `tools/qnx-tzsmc.c`.  (Report 157.)

68. **A present `BOOTCTR_FCTRY` with a type-4 region-0 image is a halt.**  *(Resolved
    for this unit on 2026-11-01: the operator flashed the sealed `os_type` 6 IFS, so the
    precondition is gone and the unit boots.  Kept because the rule itself — arming is
    **two** writes — still stands.)*  Arming is **two writes**: the RPMB record (operator) and the region-0 reseal/flash.  Session 21 measured the reference with the record present (`32 00 00 00`) but region 0 still the stock `75be24e8…` image (`os_type` 4, CRC `0x8D46FB4A`); the factory path's CRC gate requires `os_type == 6`, so the **next boot would halt** (retry branch).  The sealed asset `local/images/ifs0.sealed.bin` (`c72b0530…`, `os_type` 6, CRC `0x116B2562`) is the 5-byte reseal; the operator flashed a resealed/modified `PB-4869-root.0.3.ifs` with `bb10mt` on 2026-11-01, so that hazard is gone.  (Report 160.)
70. **Never default or retry the device password, and do not expect a challenge after a rejection.**  Observed 2026-11-01: one wrong entry left the boot rom answering neither accepted nor with a fresh challenge — later runs failed **without a prompt**, even with the correct `--password` — until the unit booted the OS.  So a retrying tool could spend attempts invisibly; `bbusb.py` sends a supplied password once, re-prompts only on a real challenge, and aborts otherwise (report-level detail in `SAFETY.md` §7).  Ten wrong attempts wipe UFS, RCFS, IFS and several NVRAM records including the password record (operator, 2026-11-01).  `bbusb.py` therefore has **no** `--password` default (it used to be `0000`, which would have spent attempts silently on any unit with a different password); omit it and the library prompts only when the device challenges.  `SAFETY.md` §7.
69. **`RESETQNXOSREGION` is a record delete, and CFP's "cleared" message is not proof.**  Two independent static analyses agree (reports 163/164): the command is loader op **`0xE8` with `arg = 4`**, and LDR_01's handler (`0x80103F18`) does `movw r0, 0x2074` + delete-record (`0x8010B8BC`) — nothing is written.  `0x2074` is the 24-byte **OSREGION record**, the same one flash `COMPLETE` (`0xC0`) writes and that `0xE9` reads back (CFP labels that read "boot count info", a mislabel).  CFP prints "QNX OSREGION NVRAM record cleared." on status `6`, but the device sends **`0x2E` on both the success and the failure path**, so the status cannot distinguish them: **always read `0x2074` back** (`0xE9` arg 0) and compare.  `bbusb.py reset-osregion` does the read/write/read and reports whether the record actually changed; dry run unless `--apply`.  Still static-only: the sequence has not been run on a device.  (Reports 162-164.)

## 3. Corrections to older reports

These seventeen are preserved deliberately. Each is newer than the text it
contradicts, and the older reports have been corrected in place where they could
be.

**3.1 The reference runs FOUR DSI data lanes, not two.** The "2x link rate,
benign, just an encoding artefact" claim in 118 §8.2 is **wrong**. Two lanes
force a 75.6 MHz byte clock; four give the reference's 37.8 MHz with `TL = 1008`.
The difference is a missing pair of lanes, not a benign redistribution. Source:
report 121 §11. Anything built on 118 §8.1's two-lane `lanes = <0 1 2 3 4 5>` or
on 111's "2 lanes" is superseded.

**3.2 The TC358764 control port is DSI-generic, and the reference DOES program
it.** 111 §8 item 4 (and 118 §4.3's earlier wording) is superseded: there is no
16-bit-address I²C table to find, because the port is MIPI DSI generic packets
(16-bit register address + 32-bit data).  **Report 125 (2026-10-28) then showed
that report 121 §12/122's "nothing programs it" was wrong**: `winch_lcdctl`
calls a full bridge programmer at vaddr `0x13e50` from the splash sequencer at
`0x1938c`, unconditionally, between the DSI PHY/PLL programming and the DSI VC
enable.  Our driver now reproduces that exact sequence (`LV_CFG = 1`,
`LV_PHY0` `IS(3)/ND(13)`, `SYS_RST = 4`).  Corollary: never write
`SYS_RST <- 0x1f`.

**3.3 `loader_06001A06-00.bin` is a ramloader, never on media.** It is uploaded
over USB by the host tools; it is not part of the boot chain and is not stored
on the eMMC. Source: report 121 §12 (also report 98 §1.1, where it is `LDR_01`,
238 128 B).

**3.4 The vertical porch decode is 12/12, not 13/13.** 117 §2.1 read
`DISPC_TIMING_V = 0x00C00C00` as VBP 13 / VFP 13 / VSW 1 ⇒ Vtotal 627 ⇒
50.56 MHz. TRM Table 10-21 (and mainline `dispc.c`) apply `+1` only to the H
fields and VSW, so it is **VBP 12 / VFP 12 / VSW 1 ⇒ Vtotal 625 ⇒ 50.400 MHz
exactly**. Source: report 118 §6.3.

**3.5 `BOOTCTR_FCTRY` out of range is a hard lock, not a fallback to the product
path.** The device-side top-up script, report 116 §2 and (in this tree)
`bench/device-startup.sh`'s own comment used to say a value of `0` or `> 0x32`
"makes the bootloader fall back to the product/ECDSA path" (the comment was
corrected in session 23). It does not: the unit stops
booting with "expired factory mode" and there is no software recovery.
`0x32` is the ceiling *and* the target; writing exactly `0x32` with a readback is
the normal top-up. Report 116 §2.2 corrects its own §2 quotation; `SAFETY.md` §2
is the operational form.

**3.6 The factory path is region 0 only, once, with no A/B fallback.** Report
85's plan assumed a global selector and a requested/actual-region fallback, so it
staged region 1 and made both regions CRC-valid. The factory path hardcodes
region 0 and never reads NVRAM `0x2074`; the fallback driver is the *product*
path. Sources: reports 89 §3.5, 94 §0, 95 §5, 97 §0;
`docs/contradictions/85-ab-fallback-vs-factory-region0.md`.

**3.7 The panel is a Hitachi; the LG-table result was a host-state artefact.**
Report 118 §6.2's "this unit is Hitachi" stands.  On TabletOS with the display
up, `GPIO6_DATAIN = 0x00010800` (bit 16 = 1) and slog prints `LCD is Hitachi`
(report 127 §1).  The Linux run in which the LG table appeared to produce frames
was made with the host in a different state (live streaming vs the
unbind/bind-disabled output); the LG table is not for this unit.  Source:
report 127 §1/§5, `docs/sessions/session-13.md`.

**3.8 QNX's `-t/-T` bridge self-test is a diagnostic, not the display path.**
It programs the bridge's GPIO block and leaves the panel in a grey test pattern;
it must not run in the boot path.  It is the valid delivery proof
(`GPIO2_DATAIN` bit 4 toggles), but run it only with `power_off` then
`-t <n> power_on`.  Source: reports 126/127, traps 34/35.

**3.9 Report 127 §3's "QNX runs the command VC in HS" is wrong.**  `MODE_SPEED`
is `DSI_VC_CTRL[9]` and `0x0` selects **LP**; QNX's measured `0x20009D81` has
bit 9 **clear**, so both systems were in LP.  The real deltas from mainline's
`0x20808D81` are bit 23 (`DMA_TX_REQ_NB`) and bit 12 (`RGB565_ORDER`).  The
"HS" reading was an assumption, not a decode.  Source: report 129 §3, TRM
Table 10-530.

**3.10 Report 127 §5's "first partial live image" is withdrawn.**  With the
framebuffer painted magenta the flash the operator saw was **grey** — it was
never our content.  It is a panel/TCON transient.  Report 129 §5; repeated in
report 131 §4e, where the same test with the bridge actually programmed leaves
the panel black.  Anything built on "the left half was our framebuffer" is
superseded.

**3.11 `GPIO2_24` is not established as the bridge's RESX.**  Report 122 rated
it "medium"; two measurements now argue against it: the reference reads that
bit **low** during a working display, and the TC358764 datasheet says `RESX`
(pin B3) is "hardware reset, **low active**" — both cannot hold.  Pulsing it or
not pulsing it changed nothing on the bench (images `0xDED99C5E` vs
`0x48048809`).  Source: report 129 §7e.

**3.12 `LANESTATUS0 = 0` / `SYSSTAT = 0` are not fault indicators.**  Report 131
§4a/§4b/§4f read them as "no lane in HS RX" and "the LVDS PLL has no input", and
§4f concluded the fault was the DSI clock lane.  Read over DSI from a working
TabletOS display with `tools/pbdsi`, both registers are **0** as well
(`LANESTATUS1 = 0x1D` too), and `0x0230` reads `0xFFFFFFFF` on a working chip.
Source: report 132 §2/§4.

**3.13 Our `VP_HTIM1` / `VP_HTIM2` constants were wrong, not the hardware.**
Report 131 §3 saw `0x0454`/`0x0458` read back as `0x00000001`/`0x00000400` for a
write of `0x78000001`/`0x76000400` and recorded "upper half not stored … probably
a 16-bit register pair; observation, not a fault".  The reference writes
`0x00780001`/`0x00760400` (report 130 §3.3) — HBP 120, HFP 118 in bits 24:16 —
and our values had the porches in the reserved top byte, so the bridge really had
`HBP = HFP = 0`.  Fixed in session 15; `VP_VFUEN` then self-clears as on QNX.
Source: report 132 §5.

**3.14 The TCON byte order is high-first, and neither order was ever
"broken".**  With the bridge correctly programmed both orders drive the panel:
high-first (the reference's, `winch_lcdctl 0x1c51c/0x1c520`) gives the
non-inverted field, low-first swaps magenta↔green and black↔white.  Session 13's
"low-first → white, high-first → partial image" was measured with the bridge
unprogrammed and is not about the byte order.  Source: report 132 §8,
`docs/contradictions/111-vs-123-tcon-byte-order.md`.

**3.15 `PPI_TX_RX_TA` must be `0x00040006` for our bridge reads.**  Report 131
§4g proposed raising it and left it as a hypothesis.  It is confirmed, with the
mechanism: the value is latched at `PPI_STARTPPI`, `4` gives ~50 % `-EIO`, and
the VC re-arm (trap 37) does not affect the rate.  The reference keeps `4`
because it never reads.  Source: report 132 §3.

**3.16 The last display defect was the DSS functional clock, not the DSI or the
bridge.**  Report 129 §7e recorded *"clk rate mismatch: 48000000 != 128000000"*
as one of two unexplained mismatches and never acted on it; every later report
reasoned about the link instead.  The DSS functional clock is the DISPC's **core
clock**, and it was 48 MHz against a 50.4 MHz pixel clock: the left of every line
was perfect and the picture failed progressively across it.  The reference runs
the DSS at 100.8 MHz.  Fixed in `dss_probe()` (report 133 §2/§3).

**3.17 The published "media offsets" were 1.3125 MiB high.**  `SAFETY.md` §4,
report 100 §1 and the flasher's plan line computed `block * 0x10000`.  Session
16 measured the raw (`mmcblk1`/`hd0`) offsets and found `(block - 0x15) * 0x10000`:
the region-0 IFS is at **`0x8100000`** (not `0x8250000`), region 0 starts at <!-- audit-ok: names the old wrong value -->
`0x8000000`, region 1 at `0x28000000`, the FS partition at **`0x49000000`** (measured in session 17; 1 MiB above the rule's value — report 138).  The
flash arguments are unchanged (loader coordinates); the raw offsets are not.
Source: report 134 §5, trap 47.

**Also corrected elsewhere, for completeness:** `0x1cce0` in `winch_lcdctl` is a
HAL parameter-block defaults initialiser, not the register programmer — the
writers are `0x18a7c`/`0x179dc`/`0x16610` (report 117, correcting 111); the
Linux-era session 3 (absolute 9)'s "global CPU halt / hardware wall" was the
kernel's own VFS panic with IRQs disabled (report 109, correcting 105);
`BOOTCTR_FCTRY` only looked static because the device-side script repairs it
every boot (report 116 §2.1) — and then only while that script worked, which it
did not (see
`docs/contradictions/bootctr-drain-vs-session10-readings.md`); report 118 §10.1's
GPIO2 map missed bit 24 (report 121 §2); 117's evaluated `DSI_DSIPHY_CFG0` word is
byte-scrambled (`0x0B190D13` on hardware); and 71 §7b's "the stage never
decrements `BOOTCTR_FCTRY`" was an incomplete caller audit — reports 89 §3.5 and
95 measured a decrement (`docs/contradictions/
71-bootctr-stage-decrement-vs-provision-only.md`).

* **Report 98 §2.3's path-1 base (`p34.start = 0x805`) is wrong for this
  loader.**  The V1 `qcfp` offset is region-relative to the region start
  `0x815`: the RCFS restore with V1 offset `0xC0` landed at `0x8D5` and matched
  the vendor component, and a base of `0x805` would have landed 1 MiB low.  The
  V2 `rrcq` was not used.  A `bb10mt pack -v 2` container has a V1 header
  without a `qcfp` block and is rejected `0x11`; use `-v 1,2`.  (Trap 55,
  report 139.)

## 4. Documentation and tree defects (not behavioural bugs)

### Still open

* **The repository history still contains `hardware/TC358764XBG.PDF`.**  The file
  was tracked by mistake (a case-sensitive `*.pdf` ignore missed `.PDF`); it was
  untracked in session 23 and `.gitignore` is now case-insensitive, but the blob
  remains in earlier commits.  Do not publish the history without removing it.
* **The `/dev/kmsg` record with embedded binary fragments is unexplained.**  A
  `/proc/interrupts` record contains non-text bytes, including what is itself a
  `8,<seq>,<usec>,-;` kmsg header (lines 1509–1510 of
  `local/logs/boot-20261030-084331.log`).  The DRAM ring is `no-map` reserved,
  so it is not a ring overlap; the cause is not established.  It does not affect
  operation (reports 136 §5, 142 §4).
* **The shipped patch carries temporary instrumentation.**  `PB_SMC_PROBES`
  (report 105's SMC liveness probe) is compiled out (`#define PB_SMC_PROBES 0`),
  and `PBVOLT:`/`PBOCR:` `pr_info`s in the MMC2 path still print on every boot
  (`kernel/clean-kernel-6.15.11-changes.patch`, `drivers/mmc/host/omap_hsmmc.c`).
  **`PBREQ`/`PBIRQ` no longer do**: since session 17 they are behind the
  `trace` module parameter (default off), because per-request printk to fbcon
  was itself the write-path bottleneck (trap 52, report 136).  The remaining
  PB prints belong in the bring-up instrumentation list, not in a publishable
  patch — see `ROADMAP.md`.
* **`A/B` and `0x2074` material in `docs/reports/85-*`** plans a two-region
  staging the factory path does not do; the correction is in
  `docs/contradictions/85-ab-fallback-vs-factory-region0.md`, not in the report.
* Older reports keep their original wording where later ones corrected them
  (`118` §8.2, `111` §8 item 4).  The corrections are applied *in place* in those
  two files and listed in §3; the other reports are left as written so the
  section-by-section citations elsewhere still resolve.

### Fixed in session 17 (write path, tooling, policy)

* **The Linux eMMC write path was not a driver defect.**  `omap_hsmmc` is fine:
  256 KiB `CMD25`s, 5.5–7.0 MB/s with the trace off.  The old 17–32 KB/s was
  `recv`'s tty-sized buffered writes plus 4–7 `pr_info`s per request to fbcon;
  the trace is now behind `/sys/module/omap_hsmmc/parameters/trace` (default
  off) and `blkwr` is the tested large-write primitive.  Trap 49 replaced
  (report 136).
* **`tools/env.sh` no longer shadows the host `make`.**  It put the QNX host bin
  first, and that tree's GNU make 3.82 broke kernel builds (>= 4.0 required);
  the QNX bin is appended last now.
* **hd3's raw offset is `0x49000000`,** not `0x48F00000`; corrected in SAFETY,
  STATE, trap 47, report 134 and the console `qnx6` default (report 138).
* **The read-only-mount policy is resolved** (operator): read-only eMMC/QNX6
  mounts are allowed, writable are not; SAFETY §4/§6 and DECISIONS §17.
* **New capability: read-only QNX6 access** (`CONFIG_QNX6FS_FS=y` + console
  `qnx6`), used to diagnose the reseed (report 138).
* **A smaller hd3 FS is reseeded at boot** — trap 54; an FS-only shrink is dead.

### Fixed in session 15 (display and tooling)

* **The display.**  Three defects, each necessary: bridge delivery (report 131),
  the LVDS porches plus the TCON table at bring-up (report 132), and the DISPC
  core clock (report 133).  §1 above records them; do not re-open.
* **`LANESTATUS0`/`SYSSTAT` as fault indicators** — withdrawn (report 132 §4).
* **`VP_HTIM1/2` "don't store the upper half"** — our constant; fixed
  (report 132 §5).
* **The console's `v` op was unreachable** — `sscanf` returns 1 for a bare `v`
  and the code rejected `n < 2`; fixed (report 132 §10).
* **`/init` no longer auto-reboots after 180 s** — the timer spent a factory <!-- audit-ok: fixed-list history -->
  boot per iteration and the console made it unnecessary (trap 15/33).
* **`CONFIG_INITRAMFS_SOURCE` is repo-relative** (`"../../initramfs/root"`), so
  no clone needs a machine-specific path (trap 43).
* **The host tools derive every path from the repository** and take
  `PB_KEY`/`PB_PUB`/`PB_SYSTEMMAP`/`PB_IFS_TEMPLATE`/`QNX660` overrides; the two
  QNX binaries are stripped.  [SETUP.md §8; commits `f47fbef`, `f28fd53`]

### Fixed during the consolidation (recorded so nobody re-fixes them)

* `CONFIG_INITRAMFS_SOURCE` in `kernel/winchester.config` and
  `kernel/patches/winchester.config` named the old `rea-work` initramfs tree; both
  now name `initramfs/root` here.
* **`local/kernel-tree/.config` (the live build tree) still named the `rea-work`
  tree until session 15**, which is why initramfs changes silently did not reach
  any image.  Corrected; see trap 43.
* `tools/pre_os_ifs_flash.py` passed `ROOT + '/bbusb/loaders'` to bbusb; it now
  passes `bbusb.DEFAULT_LOADERS` (`local/loaders` here, `tools/loaders` or
  `$BBUSB_LOADERS` if present).
* `tools/README.md` claimed the vendor `TEMPLATE` was unused by the
  `--template-ifs` flow; it is read on every flash, and the README now says so.
* The board DTS described "2 data lanes" (it documents four), and
  `kernel/patches/README.md` quoted 2 482 lines / 28 files (it is 2 922 / 29).
* `docs/reports/HANDOFF-2026-10-27-session6.md` §0 called the patch set "stale"
  while its own §3 says it was re-exported; §0 now matches §3.
* The host tools under `tools/` no longer hard-code `$HOME/rea-work`, and
  `tools/force_reset_loader.py` is in the tree.
