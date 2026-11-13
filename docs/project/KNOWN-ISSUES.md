# Known issues

Open issues and current corrections.  The resolved display history and the
per-session "fixed" ledgers are archived in
[`docs/history/KNOWN-ISSUES-HISTORY.md`](../history/KNOWN-ISSUES-HISTORY.md).

## 1. The display works — archived

The panel works (session 15: bridge delivery, LVDS porches + TCON, and the
DISPC core clock).  The three fixes, the falsified hypotheses and the boot-log
history are in the history file above.  Do not re-open them.

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
25. **The flasher's bootrom window is 60 s** and it exits silently.
26. **`pmic-twl6030-ctl set_state vaux1 off` takes the unit down** (needed a power cycle). Read rails; never toggle VAUX1.
27. **A flash cycle's precondition is media running, not the ramloader** — in `0fca:8001` there is no `/dev/ttyACM0`, so a console reboot is a silent no-op and the flasher waits out the ramloader's ~90–120 s timeout (the "two minutes with the green LED").  The flasher now resets the loader itself after the write (`leave_loader()`; tested in session 13), so the manual `force_reset_loader.py` step is only a fallback.  The pre-flash reboot is still started by hand.
28. **Panel-chain writes outlive a warm reset and QNX does not undo them.** A wrong-byte-order TCON table and a full `SYS_RST <- 0x1f` both left the *reference* display with faint vertical artifacts, cleared only by a **cold battery pull**. Prefer `spi tcon`; never `SYS_RST`.
29. **A file-existence check does not prove an overlay tool runs.** `dd` lived in the devuser overlay and `-x` passed while every exec failed with `cannot execute` (its interpreter is created at boot too). Test by **running** it.
30. **`--reuse-loader` can fail** (`set_mode(2) failed`) and strand the bootrom with a **red blinking LED**; recover with a plain `--template-ifs <sealed>` flash, no power cycle needed.
31. **Non-burst DSI video mode is rejected at this pixel clock** — `[drm] Cannot find any crtc or sizes`, no `/dev/fb0`, DSS stays in standby. Keep BURST.
32. **`pgrep -f`/`pkill -f` match your own command line** — use `ps -ef | grep '[p]re_os_ifs_flash'`.
33. **A wedged image used to reset-loop every ~7.8 s, and every iteration was another factory boot.**  Since session 15 `/init` no longer reboots itself, so this only applies to an old image.  A wedged *new* image stays up and costs one unit; the restore flasher's `--delay` is still what catches a loop instead of a stale bootrom.  Read the counter at both ends of a cycle, and treat a `0x32` reading as "the top-up ran", not as "nothing was spent" (`SAFETY.md` §2, report 109 §8.1, session-10 §3).  **On current images the counter self-replenishes** (our Linux `/init` top-up and the TabletOS `startup.sh`), so this is a health check, not a budget to conserve.
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
50. **`accounts/devuser/startup.sh` exists in several versions** — the live UFS, the UFS image backup in `PB-4869-root.exe`, and the operator's on-device copy.  An older copy silently stops the `BOOTCTR_FCTRY` top-up: its first `dd` runs before the devuser overlay interpreter exists (trap 29), the guard refuses to write, and the factory budget drains (0x32 → 0x2d observed).  The good 4869 script is 4490 B, sha256 `c265cfbb…`, and waits for a working `dd` first; the operator's current 1917 script is **4,566 B, sha256 `8acf568f…`** and is the one that topped the counter up on 2026-11-03 (`0x2E → 0x32`, report 170).  Both wait for `dd`; the warning is about older copies.  (Report 135 §5; report 170 §1.)
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
69. **`RESETQNXOSREGION` is a record delete, and CFP's "cleared" message is not proof.**  Two independent static analyses agree (reports 163/164): the command is loader op **`0xE8` with `arg = 4`**, and LDR_01's handler (`0x80103F18`) does `movw r0, 0x2074` + delete-record (`0x8010B8BC`) — nothing is written.  `0x2074` is the 24-byte **OSREGION record**, the same one flash `COMPLETE` (`0xC0`) writes and that `0xE9` reads back (CFP labels that read "boot count info", a mislabel).  CFP prints "QNX OSREGION NVRAM record cleared." on status `6`, but the device sends **`0x2E` on both the success and the failure path**, so the status cannot distinguish them: **always read `0x2074` back** (`0xE9` arg 0) and compare.  `bbusb.py reset-osregion` does the read/write/read and reports whether the record actually changed; dry run unless `--apply`.  Still static-only: the sequence has not been run on a device.  (Reports 162-164.)
72. **The `ROOT_V1.4.EXE` UFS breaks the door; use the vendor `.5.ufs` with the operator's `autorun::0` change.**  The UFS extracted from `PlayBook_2.1.0.1917_ROOT_V1.4.EXE` boots, but its rooted `qconnDoor`/root kit never completes the BerryShell handshake (`Handshake ended before authentication`), so SSH stays closed.  The vendor `PlayBook_<version>.5.ufs` **with `autorun::0`** authenticates and `devuser` SSH works; the `autorun::0` change skips the setup wizard (the 2.1.0.1917 image carries two occurrences and none of `autorun::1`, and that change is the operator's, not the vendor's).  (Report 170 §3.)
73. **Never `pkexec` the USB tools.**  The device node is `crw-rw-rw-+` with `user:<you>:rw-` (udev uaccess), and the user's `pyusb` is in `~/.local/lib/pythonX.Y/site-packages`; `pkexec` runs as root, where `pyusb` is missing.  Worse, killing the `pkexec` wrapper leaves an **orphan root `bbusb.py`** holding the interface (`set_configuration`/`claim_interface` → `Resource busy`) and a **wedged ramloader** (`0x80EF`/`0xEF` time out; only a power-cycle clears it).  Run `bbusb.py`/`pre_os_ifs_flash.py` as the user; `ps -ef | grep bbusb` is the check.  (Report 170 §5.)
74. **Never lazy-unmount the FUSE image mounts.**  `bb10mt qnx6 mount` exposes the image read-write; a terminal or editor with its cwd inside the mount makes `fusermount -u` fail.  Wait for the operator to close it (`fuser -vm DIR`), then `fusermount -u DIR`.  `-z` detaches but leaves the FUSE daemon holding the image, and later mounts fail (`Unable to open file … Try again`).  (Report 170 §5.)
75. **`bbusb.py flash --from-bootrom` opens the device for 10 s.**  Arm `while ! lsusb | grep -q 0fca:0001; do sleep 1; done` first, then start the flash, or run it with the unit already in the ramloader.  The old `pre_os_ifs_flash.py` waited 300 s; this one does not.  (Report 170 §5.)
77. **The 1917 OS clock is the build date (2014).**  There is no RTC/NTP; `uname` and log timestamps read `2014/03/19`, so `.bootctr.log` entries are dated then.  Do not treat that as a stale file.  (Report 170 §1.)
78. **`bb10mt rcfs extract` aborts the whole tree on one bad chunk.**  It parses a BB10 `/base` correctly (names, modes, LZO), but `TRCFS.ExtractFileToStream` raises on the first chunk `lzo1x_decompress_safe` rejects and the exception unwinds `ExtractTree`, so everything after that file is lost.  On the Z10 `10.3.03.2049` `/base` it died at `air/extensions/net.rim.blackberry.PushService.ane/library.swf` after ~16 KB.  The chunk is fine: `tools/rcfs_extract.py` (same chunk table, `liblzo2`, writes the raw bytes instead of raising) extracts **all** 11,911 files with 0 bad chunks.  Use our reader for BB10 `/base` analysis; keep `bb10mt rcfs extract` only as a cross-check.  The container is `r-c-f-s` (superblock at `0x1000`, 1-based inodes, 32-byte inodes, LZO 16 KiB chunks), not QNX6 — `bb10mt qnx6 mount` cannot open it.  (Session 25, `BB10-PORT.md` §4b.)
79. **Any `/base` edit invalidates the RCFS verifier — replace `rfs_validator`.**  `rfs_validator` checks a signed per-file hash table (UUID, inode/name hashes, contents) before mounting the RCFS through `fs-rcfs.so`, so editing one file (session 25: `deckard/platform/blacklist.pyc`) makes the stock validator refuse `/base`.  The route is `tools/rfs_rcfs_shim.c`: it probes `/dev/emmc/os0t179`, `os1t179`, `/dev/hd1t179` and `hd2t179` and calls `mount(dev, "/base", 0, "rcfs", NULL, 0)` (EBUSY = success).  Editing a file in place is possible without repacking: find the inode (32-byte records at `inodes_off`), replace its chunk table + LZO chunks at `inode.offset`, and update `inode.size` (`+0x10`); the slot is only as large as the next file's offset.  Recompress with `lzo1x_999` (a 7,849-byte pyc compressed to 2,957 B in a 3,324 B slot), and verify by decoding the new chunk and comparing.  The inode `hash` field is not the content hash.  (Session 25, `BB10-PORT.md` §4c.)
80. **The loader's factory count is the first diagnostic after a flash.**  Read it with `python3 tools/bbusb.py info --from-bootrom --password 0000 --keep-loader --json` and look at `boot_mode.factory_count`.  A **decrement** means the CRC verify passed and the loader jumped — the failure is the OS (kernel/board support); **no change** means the loader rejected the image (descriptor, CRC or length).  Session 25: 50 → 45 with an image that verified but never brought BB10 up.  Pass the password explicitly (`0000` on this unit), **once** — never retry; `--keep-loader` stops `info` from resetting the unit into another boot that spends a count, and `--from-bootrom` is needed only when the unit is in the bootrom, not the ramloader.  (Session 25.)
81. **A portable CPython 3.2 build needs `-no-pie` on this host.**  The default build segfaults on any `-c` execution (old CPython with a modern toolchain); `CFLAGS="-O0 -g -fcommon -fno-PIE -fno-strict-aliasing" LDFLAGS="-no-pie" ./configure --without-ensurepip --disable-shared` builds a working interpreter.  The 3.2 `.pyc` header is **12 bytes** (magic + mtime + size, not 16), and `uncompyle6` emits `return` in empty exception classes — change those to `pass` before compiling.  (Session 25, `BB10-IMAGE-GUIDE.md` §3c.)

82. **The bootloader's arm9000 section is loaded at `0x80000000` from media
    file `0x9000`, so reports 150/151 quote every *code* address `0x9000` too
    high.**  A code address `A` in those reports is really `A - 0x9000` (the
    selector is `0x800000CE`, the dispatcher `0x8000F3DC`, the L4SEC enable
    `0x8000F108`, the mount `0x800082DC`, the reset path `0x80010224`).  The
    *data* addresses are absolute literals and are correct as written (session
    flag `0x80025F00`, store state `0x80025FB0`, catalogue `0x8002C760`, wipe
    table `0x8001431C`).  Verified by dumping the executing image from the
    chainloader: DRAM `0x80009000` matches media `0x12000`, DRAM `0x80009078`
    matches media `0x12078`, and the report addresses read `0xFFFFFFFF`.
    (Session 26, report 173 §1–§2.)
83. **The bootloader wipes its own RPMB state before jumping to the OS, and its
    mount hangs when re-run from the chainloader.**  The tail wipe covers
    `[0x80025E78, 0x800517C4)` — the session flag, store state, frame,
    catalogue and staging.  A chainloader therefore cannot call the
    bootloader's `record_read`/`record_write` on a pre-built catalogue: the
    selector returns `0` (absent).  Calling the bootloader's mount
    (`0x800082DC`) to rebuild it hung and reset-looped the unit, spending ~10
    counter units.  Any rebuild must account for the MMC controller state (the
    mount re-opens it from a wiped slot) and needs an abort path.  (Session 26,
    report 173 §3/§5.)
84. **`local/images/ifs0.sealed.bin` is the 2.0.0.4869 IFS, not compatible with
    a 1917 RCFS.**  Restoring it to a 2.1.0.1917 media set leaves a 4869 IFS
    with a 1917 `/base`/UFS and the unit does not boot (it reset-looped).  The
    1917 IFS with the `rfs_validator` shim is
    `local/work/ifs-shim-sealed.ifs` (sha256 `b52195bb…`), flashed at block
    `0x825`; the full way back is still `local/work/back-1917.signed`.
    (Session 26.)

85. **RPMB keys are per-unit, and the repo's compiled test keys were the
    spare's.**  `initramfs/rpmb.c` carried the spare's frame key (`dfee…`) and
    block seed (`…46 28 01 b4`); the reference's are `16 11 b3 bb…d922` and
    `…2b e1 1d a8`.  With the reference keys, `probe` prints `KEY PINNED` and
    `blockscan` reads/MAC-verifies 512/512 blocks (was 0/512).  `cid_seed32()`
    is incomplete — the real 32-byte seed's upper half is a passphrase fragment
    (`3e 00 00 00 "ing in the o"`), not zero.  The block transform is two-stage
    (`0x2E` key `page+0x20`, IV `frame+0x10`; tag check; `0x2F` key
    `page+0x60`), and `pbmon` implemented only the `0x2F` half with the wrong
    key.  (Session 26, report 174.)  **Superseded by trap 89 / report 175:**
    `0x2F` is the cipher and `0x2E` the digest, the digest key is
    `seed[0:16]`, the preload key is `page+0x20`, and `0x31(id 1)` must not be
    issued.

86. **The raw RPMB relay returns the WIRE view; reverse it before the
    monitor.**  `/dev/mmcblk1rpmb` hands userspace the frame exactly as the
    card sent it, so the 256-byte data field is byte-reversed relative to the
    logical block the `0x2E`/`0x2F` transform works on, and the 32-byte tag is
    at logical `+0xE0` (= wire `+0x00`).  Feeding `resp.data` straight to
    `/dev/pbunseal` makes every block fail the tag check.  Proof (session 27):
    `run /pbrpmb dump 0x3b` prints `logical=ok`, its reversed bytes equal the
    captured block-0x3B pair, and `HMAC(seed[0:16], rev(D)[0:0xE0]) ==
    rev(D)[0xE0:]` for blocks 0x3B and 0x43.  Report 156 §5's "the read
    leaves the logical view in `frame.data`" describes the **resmgr's** page,
    not this relay.

87. **The CRNG never initialises on this unit; the console can seed it.**
    The OMAP4 hardware RNG is behind the secure firewall — the board DTS keeps
    `rng_target` disabled because a non-secure read raises an L3 error and an
    imprecise external abort — so no `crng init done` ever appears and
    `/dev/urandom` is predictable (`entropy_avail` reads 18).  That matters
    because the RPMB nonce is covered by the frame MAC.  **Fix: the console's
    `entropy <hex>` command** (root `RNDADDENTROPY`) credits host bytes —
    measured: `entropy: credited 256 bits (avail was 18)` → `random: crng init
    done`.  `pbrpmb` now reads `/dev/random` (`O_NONBLOCK`) for the nonce and
    prints a loud warning when it has to fall back.  **Seed before any RPMB
    write.**  Reads are unaffected.  (Session 27.)

88. **On the Linux image the reboot path is the console, not the door.**
    Once our image runs, QNX/`devuser` and `169.254.0.1` do not exist;
    `tools/pbssh.sh '/proc/boot/shutdown -S reboot'` silently fails.  Use
    `tools/pbcon.py 'reboot'` (and start the pre-OS flasher immediately
    before it).  `devuser` also cannot reboot TabletOS.  (Session 27.)

89. **`0x31(id 1)` breaks the RPMB transform; `id 0` or no call do not.**
    The bootloader calls `0x31` with id 1 before its seal/unseal, but issuing
    that from `pbmon` changes the `0x2F` plaintext and makes the known pair
    fail.  The working combination (session 27, report 175) is: `0x2E` digest
    key = `seed[0:16]` (`page+0x440`), `0x33` preload = `page+0x20`
    (`86 78 7c 2a…`, **16 bytes** — 32 is refused with `-EIO`), and `0x31`
    either omitted or called with id 0.  With those, the captured
    block-0x3B pair round-trips exactly and `blockscan` reads the store.
    Corollary: **`scan`/`cmd_scan` searches the ciphertext** and can never find
    a record — only the monitor unseal (`blockscan`) can.

90. **`trustzone-omap4` on 2.1.0.1917 has no raw SMC passthrough.**  The
    devctl returns `ENOTTY` (25) and the resmgr survives; the 1917 binary
    (65 304 B) contains none of `0xC0280501`/`0x502`, which the 4869 build
    (61 208 B) builds at `0x8A5C`/`0x8A60`.  The feature was dropped in the
    newer build, not broken — do not spend time on it (report 157 addendum).

91. **The sensor cluster is on OMAP I²C4 `0x48350000`, and the board DTS had the
    bus comment inverted.**  QNX instantiates three controllers —
    `0x48070000`, `0x48072000`, `0x48350000` — and names the third
    `/dev/i2c3`; `/dev/i2c2` (`0x48060000`) is **not** instantiated.  The DTS's
    `&i2c4` (`0x48350000`) was `disabled` with the note "the one QNX does not
    instantiate", while `&i2c3` (`0x48060000`) was enabled.  It is the other way
    round: the BMA150/HMC5883L/MPU3050/CM3217/FAN5702/WM8994 all answer on
    `0x48350000`.  RIM's own map is `/base/etc/system/config/sensor_config.xml`
    (board `Winchester` rev 0) and the driver set is `/base/lib/dll`.  (Session
    28, report 176.)

92. **A read-write mount of a loop device backed by an `O_RDONLY` fd fails with
    `EACCES`.**  The first switch_root attempt opened `/dev/mmcblk1` `O_RDONLY`
    and mounted the loop `rw`; `mount(2)` returned "Permission denied" and the
    initramfs fell back to the console.  The loop's write path needs a writable
    backing fd: open the whole-disk node `O_RDWR` for a writable loop root (our
    own ext4, `DECISIONS.md` §18).  (Session 28.)

93. **Shutting down from the loop-RW root produced eMMC errors/warnings and a
    stuck tty — cause not yet identified.**  Observed from the console at the
    first `reboot` (session 28): a burst of eMMC-related messages and the tty
    appeared stuck before the unit reset.  The obvious noise was removed
    (`/etc/inittab` no longer runs `umount -a -r` on a loop root, which can only
    print EBUSY), but the eMMC part is **not** explained.  The next Linux boot
    should capture the full console `dmesg` and the shutdown output; candidates
    are the MMC2 idle-stall (the `mmc2_target` keep-awake) during the final
    writes, the ext4 writeback at shutdown, and the ttyGS0/`sh -l` respawn path.
    Do not assume the inittab change fixed it.  A graceful `reboot` took ~90 s,
    which is why a flasher started before it missed the bootrom; **`reboot -f`**
    from the shell resets immediately and is the flashing workaround (the
    `bbusb.py` window is now 60 s, `DECISIONS.md` §29).

94. **The `=m` sensor drivers need their whole module closure in the rootfs.**
    Putting only `bma150.ko`/`hmc5843*.ko`/`mpu3050.ko` in
    `/lib/modules/6.15.11` gives `bma150` but every IIO driver fails with
    "unknown symbol" (and "module 'industrialio' not found"), because
    `CONFIG_IIO=m` makes the IIO core a module too.  The closure is:
    `industrialio`, `industrialio-triggered-buffer`, `kfifo_buf`, `i2c-mux`
    (for mpu3050); plus `evdev` for `/dev/input/event*` (the input device exists
    without it, but there is no node).  `build_rootfs.py` installs exactly the
    files in `--modules`; add the closure and `modprobe evdev bma150
    hmc5843_i2c mpu3050` in rcS.  The **TMP103** is the same case with
    `hwmon`: `CONFIG_HWMON=m`, so `tmp103.ko` needs `hwmon.ko` first or it
    fails with "Unknown symbol devm_hwmon_device_register_with_info".
    (Session 28, reports 177/178.)

95. **The CM3217 ambient light sensor reads all-zero and has no mainline
    driver.**  Both 0x10 and 0x11 on I²C4 ACK, but every register (0x00–0x10)
    reads `0x00`; the mainline `cm32181` driver accepts only chip IDs
    `0x18` (CM3218), `0x81`/`0x82` (CM32181/82) and would reject it.  The QNX
    reference is `/base/lib/dll/ls-cm3217.so`: disassembly gives address
    **0x10**, 400 kHz, `devctl 0xC0100506` register reads, two ALS bytes
    combined `(hi << 8) | lo`, and a CMD/integration-time/`cal_table` config.
    From Linux the CM32181 sequence is refused: a byte read of 0x04 gave 0x4d
    once then 0x00, a word read gives 0xff00, and a word write of CMD 0x0800
    returns **ENXIO**.  `ls_startup` writes the CMD from a config byte at
    `+0x40` (`ls_shutdown` ORs bit 0) — that byte and the ALS register pair are
    the RE targets.  **ANSWERED (session 29, report 181):** the part has **no
    register file over I²C**.  `ls-cm3217.so` only ever sends a **bare 1-byte
    read/write** to two slave addresses: **0x10** = MSB + command (`0x26`
    enable, `0x27` = ALS off), **0x11** = LSB + config (`cmd2 = 0x60` at init).
    The count is `(byte(0x10) << 8) | byte(0x11)`, needs ~200 ms after `0x26`,
    and no ID is ever read.  `0xC0100506` is QNX `DCMD_I2C_RECV`, not a
    message array.  A driver is ~30 lines with `i2c_smbus_read_byte(0x11)` /
    `(0x10)` — **not** a `cm32181` ID patch, and never `regmap`.  (Sessions
    28/29, reports 178/181.)

96. **The FAN5702 EN is GPIO1_13 on pad `0x1b0`, and the pad boots in safe
    mode.**  Raw `devmem` writes of the pad/`GPIO_OE`/`SETDATAOUT` left the
    chip in reset and every I2C access returned `ENXIO`.  The working path is
    the `leds-fan5702` driver's pinctrl (`OMAP4_IOPAD(0x1b0, 0x0003)`) plus
    `enable-gpios = <&gpio1 13 GPIO_ACTIVE_HIGH>`, which pulses EN low→high
    before the init writes.  Protocol (external `LED-RE.md` + `led-fan5702.so`):
    `GENERAL (0x10)` bits 1/2/3 = red/green/blue, `0x20 = 0`,
    `0x30/0x40/0x50 = 0x36`; the vendor driver sets 100 kHz but 400 kHz works.
    Once the driver owns the client, `i2cget` says "Device or resource busy" —
    use `/sys/class/leds`.  (Session 28, report 179.)

97. **The WL1283 combo is on MMC5 (`0x480D5000`), not MMC1, and its two board
    lines are GPIO2_22 and GPIO2_21.**  Reports 176 / the session-28 DTS said
    "mmc1"; the live QNX process map says otherwise: io-pkt's
    `devnp-ti1283.so` holds `/dev/mem 0x480d5000` (8 KiB) and attaches QNX
    vector `0x5b` = **GIC SPI 59 = mmc5**, while every `sdmmc1`/`sdmmc3` pad is
    still `0x000f` (safe mode) on the reference and the `sdmmc5` pads
    (`0x148`–`0x152`) read `0x0118`.  **WLAN_EN = gpio_54 = GPIO2_22** (pad
    `gpmc_nwp`, `0x07c`, mux 3, active high, 120 ms before the chip is
    touched); **WLAN_IRQ = gpio_53 = GPIO2_21** (pad `gpmc_ncs3`, `0x07a`, an
    *input*, falling edge, active low) — the old DTS drove that line low as an
    output, which is wrong and is now removed from the hogs.  Both come from
    `devnp-ti1283.so` and are corroborated by the live GPIO diff: the
    reference's `GPIO2_DATAOUT` is `0x31413200` today against `0x31013200` in
    report 118, so bit 22 is the *only* output that changes when the WLAN driver
    runs.  (Session 29, report 180.)

98. **`&mmc5` probes and clocks but the WL1283 never answers CMD5 — and the
    chip is alive.**  With `&mmc5` enabled (4-bit, `sdmmc5` pinctrl `0x0118`, a
    1.8 V fixed `vmmc` window, WL_EN hogged high at boot) the controller probes
    (`idlest=0`, `HCTL=0x00000b00` — byte-identical to what QNX writes) and the
    core runs `sdio_reset`, then `attach_sdio`/`attach_sd`/`attach_mmc` all fail
    and it prints `mmc4: Failed to initialize a non-removable card`.  Eliminated
    with evidence: the enable line (hogged high and re-pulsed at runtime, host
    unbound), the pads (full live padconf diff), the IRQ pin, every TWL6030 rail
    and `CLK32KG` (register-for-register identical `CFG_STATE` to QNX), the OCR
    window (1.8 V and 3.3 V), and a full power cycle.  TabletOS's own
    `wpa_supplicant_ti` scans (`CTRL-EVENT-SCAN-RESULTS`) on the same boot, so
    the part works; the gap is host-side.  The two measured differences left are
    the **MMC5 functional clock (Linux 48 MHz, QNX 96 MHz)** and
    **`MMCHS_CON` (reset `0x00000600` here, QNX writes `0x000E0080`)** — see
    report 180 §6 for the order to try them.  Do not re-derive the wiring.
    (Session 29.)

99. **Reading one register bank past GPIO6 (`0x4805F000`) aborts the kernel, and
    with `panic=10` that is a reset loop.**  A debug print that walked the LPER
    GPIO banks from `0x48055000` in `0x2000` steps ran one bank too far: GPIO2–6
    are five banks ending at `0x4805D000`, and `0x4805F000` is not a target.
    The read raised an external abort at ~2.5 s — before the boot console is up
    — the `panic=10` bootarg rebooted, the bootloader re-armed WDT2, and the
    unit reset-looped through the bootrom, **spending a `BOOTCTR_FCTRY` unit per
    loop**.  Recovery is the usual pre-OS flash: the bootrom reappears every
    ~10 s, so `bbusb.py flash --component ... --from-bootrom` catches it and the
    next boot's loader reset ends the loop.  Lesson: an instrumented kernel that
    can fault before PID 1 has no console to report it — check every address a
    new probe touches against the TRM's target map first, and prefer a read
    behind a `devm_ioremap()` that can fail over a raw address arithmetic walk.
    (Session 29.)

100. **Never read another module's registers from `omap_hsmmc_set_ios()`.**
    A debug dump that ioremapped the GPIO2–6 banks inside `set_ios` and read
    them there faulted on a **runtime** `unbind`/`bind` of `480d5000.mmc`
    (`ldr r3, [r1, #0x134]`, i.e. a GPIO `OE` read) *after* `mmc_power_up`, and
    killed `sh[101]` "with irqs disabled" — which took the USB console down
    with it while the kernel itself kept running (the unit still enumerated as
    `0fca:8010`, so a reset loop was correctly ruled out).  `set_ios` runs on
    every power-up/clock change, including long after probe, when a cached
    mapping of a different target is not guaranteed live.  Put one-shot board
    dumps in `*_probe()` and `iounmap` them; never touch a second module from a
    hot path.  (Session 29.)

101. **A QNX `SIGBUS` on MMCHS5 is not an L4 firewall and not `ThreadCtl`.**
    `ThreadCtl(_NTO_TCTL_IO)` returns `-1/EINVAL` in an sshd session, but the
    *same* session reads `0x4A100000`, `0x4A31E000`, `0x48055000` and
    `0x4A009000` fine, and `procmgr_ability(ALLOW|AID_IO)` returning 0 does not
    change it.  So the privilege is a red herring and the refusal is specific to
    that block: **only io-pkt can read MMCHS5 on QNX**, and there is no way to
    obtain the reference's live SDIO controller state from the bench.  Do not
    spend another session on it; use the driver's own register writes instead
    (report 180 §5).  `tools/qnx-iotest.c` is the tool that settles the question
    in one run.  (Session 29.)

102. **A probe run must be synchronised with the operator action it is meant to
    observe.**  A cover/uncover test of the CM3217 was run as two unbounded
    48-second probe loops; nothing marked when the operator covered the sensor,
    so no sample could be attributed to a state, and the "covered" vs
    "uncovered" split was inferred from the order the loops were *issued* —
    which is not a measurement.  The numbers then looked suggestive (different
    values dominate each loop), which is exactly how an artefact becomes a
    finding.  Rule: when a probe depends on something the operator does, either
    have them act on an explicit cue inside a single bounded run, or timestamp
    both sides; otherwise record the run as **invalid** rather than
    interpreting it.  (Session 29.)

103. **Do not characterise a device on TabletOS if a QNX driver already owns
    it.**  Every external I2C access we made to the CM3217 (0x10/0x11) while
    `winchester-lc -d ls-cm3217.so` was running interleaved with that driver's
    own transfers and broke its sequence: `/pps/services/light_controller`
    reported `Lux::0000`, the panel's automatic brightness froze *for the
    duration of the probing* and recovered when we stopped, and the values we
    collected (`0x6a` plateaus, `0x00` gaps, apparently cover-dependent runs)
    were artefacts of the race, not properties of the part.  The same part reads
    a clean, stable **32 -> 191 lux** from Linux, where we are the only client
    on the bus.  Rule: **characterise from the OS that owns the device, or stop
    the vendor's driver first** — and treat any reading that contradicts a
    direct operator observation as suspect until reconciled.  (Session 29,
    report 182.)

104. **The CM3217 needs no settle delay, and must be left in its idle state.**
    Two facts that are easy to get wrong and were the whole of report 182's
    difficulty: **(a) read immediately** — the count is valid the instant `0x26`
    is written to 0x10, and every wait from 1 ms to 200 ms reads back zero, so a
    driver must poll for the first nonzero `(hi, lo)` pair rather than sleep;
    **(b) restore the idle state** (`0x60`→0x11, then `0x27`→0x10) after every
    measurement, because leaving the ALS enabled wedges the panel's own
    automatic-brightness path (measured: the panel froze while a probe left it
    enabled, and recovered as soon as the idle state was written back).  On
    Linux, our driver is the only client on that bus; on TabletOS our accesses
    race RIM's `ls-cm3217` (trap 103).  (Session 29, report 182.)

105. **An assistant's recalled knowledge is stale — look external facts up.**
    The built-in knowledge has a training cut-off and this project spans months
    of upstream movement.  In session 29 an assistant asserted that kernel
    `7.3.3` "doesn't match the 6.x series"; `kernel.org/releases.json` shows
    **7.3.3 is the current stable** (released 2026-11-06, mainline 7.4-rc1).  In
    the same session a 2026-09-01 pmaports snapshot was read as current
    pmaports.  Both needed the operator to correct them.  Rule: fetch the
    primary source (`kernel.org/releases.json`, the actual pmaports `APKBUILD`,
    the tree you are about to build) and cite it; label anything recalled as a
   hypothesis (DECISIONS §32).  This applies to register maps too — prefer a
   live read or the local TRM over recollection.  (Session 29.)

106. **The patch-regeneration recipe cannot discover new files — it only
     re-emits the files the previous patch listed.**  `kernel/patches/README.md`
     derives the file list from the old patch's `+++ b/` lines, so a file added
     to `local/kernel-tree` in a later session is silently absent from every
     re-export unless someone adds it by hand.  Session 29's "5 703 lines / 34
     files" patch therefore **omits `drivers/iio/light/cm3217.c`,
     `drivers/leds/leds-fan5702.c`, `drivers/misc/pbmon-pair.h` and
     `drivers/misc/pbmon-page.h`** (and the `Kconfig`/`Makefile` lines and the
     `arch/arm/boot/dts/ti/omap/Makefile` `omap4-winchester.dtb` entry was only
     saved by a context rebase).  Evidence (session 30): `grep '^--- a/'` lists
     34 files with none of those; a pristine-vs-tree walk prints them as
     `ONLY-NEW`; the 7.3.3 port failed with
     `drivers/misc/pbmon.c:167: fatal error: pbmon-pair.h: No such file or
     directory`.  **Fix before the next export:** build the list from the tree
     (`diff -ruN` over tracked source files, plus every `Only in` source file),
     not from the old patch, and verify by applying to a pristine tree *and*
     building it.  (Session 30.)

107. **`cm3217_remove()` oopses: probe never calls `i2c_set_clientdata()`.**
     The remove path does `i2c_get_clientdata(client)` and `iio_priv()` on it,
     but nothing ever set the client data, so the pointer is NULL and removing
     the client (e.g. `echo 0x10 > /sys/bus/i2c/devices/i2c-3/delete_device`,
     or an unbind) faults in kernel context.  Measured on 7.3.3 (session 30):
     the console showed the exception and the `sh` on ttyGS0 was respawned, so
     the following shell commands — including `reboot -f` — never ran and the
     flasher could not catch the bootrom.  Fixed by
     `i2c_set_clientdata(client, indio_dev)` in `cm3217_probe()`.  **The same
     bug is in the 6.15.11 reference tree/patch**; do not unbind or delete this
     driver on a build older than the fix.  (Session 30.)

108. **A PlayBook on a nearly empty battery warm-resets on its own — check the
     gauge before blaming software.**  At the end of session 30 the unit had
     been up ~24 min on USB when it disconnected, sat in the bootrom for 14 s
     and re-booted the *same* image (host log 12:50:17 → 12:50:46).  The
     BQ27541 then read **2.817 V, capacity 0 %, `Critical`, Discharging** — it
     had read 3.153 V / 71 % eleven minutes earlier.  That is a power brownout,
     not a kernel/WDT reset (a WDT reset would come from a stall; a flash looks
     different again — the bootrom stays open while the payload is written).
     It also spent one `BOOTCTR_FCTRY` unit (`0x2E → 0x2D`).  Before diagnosing
     a spontaneous reboot, read
     `/sys/class/power_supply/bq27541-0/uevent`; at ≤3.0 V stop bench work and
     charge with the proper charger.  (Session 30.)

109. **Subagents share this session's shell, so keep them off the bench.**  In
     session 30 two general subagents were asked to disassemble QNX libraries;
     they *could* have run `tools/pbcon.py`, `bbusb.py` or `ssh` while the unit
     was connected.  The reset in that window turned out to be the low battery
     (trap 108), **not** the agents.  Rule: while the bench is connected, give
     subagents file-only tasks and never let them run a `tools/` script.  The
     `/tmp/commandcode/shellout/` audit requirement added after that scare is
     **dropped** (operator, session 31) — it was a one-off misdiagnosis.
     (Session 30; revised session 31.)

110. **Do not reverse-engineer PlayBook hardware from BB10 images.**  BB10/10.x
     targets newer devices; its Winchester branch is a porting target, not the
     PlayBook's shipping configuration.  Use OS 2.x only: the live 1917 oracle,
     the 1917 `/base` QNX6 image (`local/work/rcfs-1917.rcfs`, a QNX6
     filesystem despite the name — mount with `bb10mt qnx6 mount`), the 1917
     IFS tree and the 4869 IFS dump.  Concrete difference: BB10 has
     `libaudio_config.so.1`, the 1917 `/base` does not.  (Operator, session 30;
     `DECISIONS.md` §34.)

111. **The TWL6030 charger's `monitored-battery` must point at a
     `simple-battery` profile, not at the fuel gauge.**  `twl6030_charger`
     calls `power_supply_get_battery_info(charger->usb, …)`, which resolves the
     phandle in `monitored-battery` and then **requires the target node's
     `compatible` to be `simple-battery`** (or a static Samsung battery);
     anything else returns `-ENODEV` and the probe fails with
     `Failed to get battery info`.  The session-30 prepared DTS pointed it at
     `battery@55` (`ti,bq27541`), so the driver built, probed and died — the
     node and module were present, which is what made it look like a binding
     problem.  Fix: keep the gauge node label-free and add
     `battery { compatible = "simple-battery"; };`.  With no properties the
     driver falls back to the bootloader's `CHARGERUSB_CTRLLIMIT1/2`
     (measured `0x21` → 4.16 V, `0x08` → 900 mA), both inside its accepted
     ranges.  (Session 31.)

112. **`twl6030_usb` = `Charging` does not prove the pack is charging — read the
     gauge.**  After the session-31 fix the supply registered and reported
     `ONLINE=1`, VBUS 4.81–4.91 V and `STATUS=Charging`, while the BQ27541 read
     **0 mA**, and with the panel on it read **−132 mA, Discharging** (the
     supply then flipped to `Not charging`).  The driver's `input_current_limit`
     is fixed at **500 mA** on probe; the running system can consume all of it,
     leaving nothing for the battery.  Judge the charge path by a positive
     `CURRENT_NOW` on `bq27541-0`, not by the charger supply's status.  The
     pack in session 31 was also flagged `HEALTH=Dead` (the gauge's SOC-final
     flag at 2 %), so a negative result needs the TabletOS cross-check before
     blaming the driver.  (Session 31, `DECISIONS.md` §35.)

113. **The PlayBook's charger is an Intersil ISL9519 at I²C1 `0x48070000`
     slave `0x09` — not the TWL6030 BCI.**  Measured on the running 1917:
     VendorID (reg `0xFE`) = `0x0049` ('I'), DeviceID (reg `0xFF`) = `0x0003`;
     `0x14 = 0x0280` (charge current 640 mA), `0x15 = 0x1070` (max system
     voltage 4208 mV), `0x3F = 0x0180` (input current limit 384 mA),
     `0x44 = 0x0000` (control).  The `/pps/services/power/charger` values match
     exactly.  The stock OS leaves the TWL MAIN_CHARGE block at its bootloader
     reset values (`CONTROLLER_CTRL1 = 0x00`) and only masks/unmasks its
     VBUS/VAC interrupts (`CONTROLLER_INT_MASK = 0xDC`).  **So a working
     `twl6030_charger` probe does not charge this pack**; the 1917 charger is
     the ISL9519.  Its host-port policy (384 mA input limit) is below the
     system load (~430 mA), which is why 1917 shows no charging on a PC port;
     the pack charged on the rapid-charger dock (2 % → 7 %).  (Session 31,
     report 183.)

114. **The ISL9519 needs non-zero `0x14`/`0x15` and a refresh at least every
     175 s.**  Intersil AN1570 (ISL9519TQFNEVAL2Z user guide): the output is
     disabled while the Charge Current and Max System Voltage registers are
     `0x0000`, and **the part shuts down if it does not receive a
     MaxSystemVoltage or ChargeCurrent command every 175 s**.  SMBus words are
     LO-byte first.  Registers `0x14`, `0x15`, `0x3F` and `0x44` are the four
     the vendor's own tool writes (charge current, max system voltage, input
     current, control).  A Linux driver must therefore set both non-zero
     registers and refresh them on a timer; a one-shot write stops charging
     after 175 s.  (Session 31, report 183.)

115. **WDT2's load can be changed — but only while the timer is stopped via
     `WSPR`.**  The loader arms WDT2 with `WLDR = 0xFFFC1800` (**7.8125 s**) on
     the chainloader jump path; live QNX runs `LDR = 0xFFE2B400` (**58.6 s**).
     Report 109 §8.1's "`LDR` writes are dropped" is true only while the timer
     runs: TRM §22.3.4.8/§22.3.4.9 require **disable = `WSPR` `0xAAAA` →
     `0x5555`**, then write `WLDR`, then **enable = `WSPR` `0xBBBB` →
     `0x4444`**; the new load takes effect on the next `TGR` kick/overflow.
     Verified live in session 31 (`WLDR` read back `0xFFE2B400`, `WCRR`
     reloaded and counted in the 58.6 s window).  `WCLR = 0x20` (PTV 0,
     PRE 1) and the period is `(0xFFFFFFFF − WLDR + 1)/32768`.  The kick is
     still `TGR` read → invert → write; QNX's `wdtkick` never disables the
     timer.  **The stage after the request:** successive writes to the WDT
     functional registers are posted through the L4 clock domain, so **poll
     `WWPS` (`0x34`) after every write** — bit 4 `W_PEND_WSPR`, bit 3
     `W_PEND_WTGR`, bit 2 `W_PEND_WLDR` (TRM §22.3.4.13).  A fixed delay is
     not enough: with `udelay(200)` the sequence silently stopped at
     `WSPR=0xBBBB` and `WLDR` stayed `0xFFFC1800`.  The kernel now does the
     repogram at the late initcall (the inlined GIC-probe path did not run on
     the 7.3.3 base), pets every 10 s with a 2 s fallback, and the unit was
     verified alive past 275 s with `WCRR` reloading every ~10 s
     (image `0xE36C679A`).  The expected dmesg line does **not** appear on
     7.3.3 — verify by `WLDR`/`WCRR` registers.  (Session 31; corrects
     report 109 §8.1.)

116. **`CM_DIV_M5_DPLL_PER` is the DSS clock divider — never write the
     reference's `0x329`.**  The OMAP4 DSS functional clock's parent list
     (`omap4_dss_dss_clk_parents[]` in `drivers/clk/ti/clk-44xx.c`) is
     `dpll_per_m5x2_ck`, and the Linux clock framework runs M5 at `0x204`
     (DIV 4) to produce the 96 MHz DSS clock.  Writing the reference's
     `0x329` (GATE_CTRL=1, DIV=9) from the session-31 FREF experiment
     **changed the display core clock and made the panel glitch
     (timing-related)**.  **Confirmed by isolation (session 31):** the glitch
     appears immediately on the `0x329` write, and writing `0x204` back
     **recovers the panel without a reboot**.  The GPIO2_22, FREF-pad and
     WDT2 writes were tested separately and are clean.  The reference's
     divider value belongs with its own DPLL_PER rate/post-divider setup and
     is not portable alone.  The same M5 output is the FREF source, so the
     WL1283 reference-clock experiment cannot be done by poking this divider
     without breaking the display.  (Session 31; report 180 + addendum.)

117. **The WL1283 `FREF_CLK2_REQ` input is not used by the reference — do not
     select mux 1.**  Fresh 1917 read with WLAN up: `0x4A31E058 = 0x000B0003`,
     pad0 **mux 3 = `gpio_wk31`** with the input buffer off; pad1 mux 3
     `gpio_wk7`.  The second opinion's mux 1 (`fref_clk2_req`) is not
     selected.  The pad's line is low on both OSes (measured by temporarily
     enabling the input buffer on our image).  (Session 32, report 185.)

118. **The FREF2 output is SCRM `AUXCLK1` (`0x4A30A314`), not the DPLL_PER M5
     divider.**  QNX sets `0x00020100`: source SYS_CLK, /3 = **12.8 MHz**,
     enabled, and muxes `0x4A10019C` to `fref_clk2_out`; Linux had both off.
     It can be enabled through the clock framework, but `of_clk_get()` on a
     provider node returns the node's *parent* clock; use
     `of_clk_get_from_provider()` to get `auxclk1_ck` itself.  Reproducing it
     exactly (images `0xF521AB06`/`0x18D49C22`) does **not** make CMD5 answer,
     so it is not the missing variable.  (Session 32, report 185.)

119. **QNX enumerates the WL1283 at 24 MHz and sends CMD5 with argument
     `0x00FFFF00`.**  Static RE of `devnp-ti1283.so`: `SYSCTL = 0x000E0080`
     (CLKD=2) before CMD0, no 400 kHz phase, sequence CMD0→CMD5
     (`0x00FFFF00`)→CMD3→CMD7.  Linux's 400 kHz + arg 0 is not the difference:
     a runtime 24 MHz attempt (`SYSCTL=0x000e0087`) and a host quirk with arg
     `0x00FFFF00` both still time out (`err=-110`, no response).  The audit
     (report 186) shows the default Linux attempt is **one try at 400 kHz**
     (`f_min = f_init = ios.clock = 400000`, `CLKD = 120`, actual 400 kHz)
     and the full sequence **CMD52 → CMD0 → CMD8 → CMD5(arg 0)**; the card
     times out on every command, starting with CMD52.  The 24 MHz line in
     report 185 was the deliberate `init_hz` test, not the default.  Do not
     re-litigate the init clock.  (Session 32, reports 185/186.)

120. **Do not use a runtime MMC5 `unbind`/`bind` as a rescan.**  It wedges the
     ttyGS0 shell (the kernel keeps running and USB keeps enumerating), so the
     console dies and `reboot -f` never executes; the unit needs a power
     cycle.  Change the DTS/driver and boot instead.  (Session 32; extends
     trap 100.)

121. **The WL1283's main clock is its 26 MHz TCXO; FREF is not needed before
     enumeration.**  `tiwlan_winchester.ini` has `TCXO_Clk = 1`;
     `wl128x_boot_clk()` selects the TCXO unless the hardware reports FREF or
     the TCXO is 16.368/32.736 MHz.  `STRFRefClock = 2` is the sync clock.
     The QNX driver does not configure or gate the TCXO, so the remaining
     question is whether the board's 26 MHz oscillator runs under Linux —
     measure TCXO/VBAT/VIO/reset and MMC5 CLK/CMD/DAT0 at the chip; no further
     SoC-side register diff is expected to answer it.  (Session 32, report
     185.)

122. **Never retry `pkexec` after a failure on this host.**  The operator's
     faillock policy can lock the host for 10 minutes after repeated failed
     authentications.  If `pkexec chmod 666 /dev/ttyACM0` fails or the prompt
     is not answered, stop and ask the operator to run the chmod.  (Operator,
     session 32.)

123. **The WL1283's 26 MHz TCXO is powered from the chip's internal 1.8 V.**
     The Motorola 148603 module guide (a WL1283-based module) gives
     VBAT 3.3 V, VIO 1.8 V, WL_EN active high, WLAN_IRQ active low,
     SLOW_CLK 32.768 kHz, and says the TCXO is supplied by the SoC's internal
     1.8 V.  If VIO is missing, the TCXO is dead with it.  A multimeter can
     check VBAT/VIO/WL_EN/WL_IRQ and the SLOW_CLK DC average, but not the
     26 MHz waveform.  (Session 32, report 186.)

124. **A `late_initcall` runs after the `device_initcall` probes — not before
     them.**  The first session-32 FREF2 test enabled SCRM `AUXCLK1` from a
     `late_initcall` (level 7); the `omap_hsmmc` platform driver probes at
     device-init level 6, so the clock was enabled **after** the MMC5 rescan
     and the test was invalid.  Anything the probe needs (clock, reset,
     regulator) must be set in the probe itself or at an earlier level.
     (Session 32, reports 185/186.)

125. **The Kindle Fire HD 3 (`amazon-soho`, P48WVB4) is a BCM4330 device,
     not a WL1283C reference.**  The archived pmaports packages install
     `bcmdhd.cal`/`fw_bcm4330.bin` and load `dhd`, and the vendor kernel config
     has `CONFIG_WL12XX_MENU` off.  Do not use its firmware, DT or power
     sequencing as a WL1283 comparison.  (Session 32, report 186.)

126. **The QNX `winchester` powerman preset has no WLAN rail or clock.**  Its
     WLAN authorities (`activity/wifi_driver/active|idle`, `air_needs_wifi`,
     `wlan_needs_DMA`, `WLAN_NEEDS_PREVENT_OSWR`) only gate the standby
     shutdown/OSWR decision; `ldo_vaux1`/`ldo_vaux3` are the display/backlight
     rails, not WLAN.  `devpm-omap4.so` is the PRCM/voltage/DVFS plugin, and
     `winchester_phy_ctrl` is the **USB** PHY.  Do not re-open the powerman
     lead for WLAN power.  (Session 33, report 187.)

127. **The WL1283's IRQ is the only accessible chip-alive indicator, and WL_EN
     is not brought out.**  The rightmost front pad is GPIO2_21/WL_IRQ: it is
     1.8 V when the chip is up (QNX, WLAN up) and 0 V when WL_EN is low; under
     Linux with WL_EN high it stays 0 V, so the chip is not up.  GPIO2_22
     (`WL_EN`, pad `0x4A10007C` mux 3) can be toggled from the shell with
     `devmem 0x48055190/0x48055194 32 0x00400000`, but no accessible point
     follows it.  The accessible DC points are 1.8 V and 0 V; no 3.3 V chip
     rail is exposed.  (Session 33, report 188.)

128. **A 200 ms MMC5 post-power-on settle does not make the chip answer.**
     Image `0x0B38612D` (`post-power-on-delay-ms = <200>` on `&mmc5`) gives
     `delay1 200ms`/`power_on delay 200ms` before the first command; CMD52/
     CMD8/CMD5 still time out (`err=-110`) and the IRQ stays low.  The chip
     had seconds of WL_EN in the baseline images anyway.  (Session 33, report
     188.)

129. **Skipping the SDIO reset (CMD52) does not make the chip answer.**
     QNX's sequence is CMD0 → CMD5; Linux sends CMD52 first.  Image
     `0xAC98BCAD` adds `MMC_CAP2_NO_SDIO_RESET` (bit 30) for the `480d5000`
     host, yielding CMD0 → CMD8 → CMD5; CMD5 still times out.  The core's
     CMD52 is not the cause.  (Session 33, report 188.)

130. **The WL_EN reset pulse breaks USB warm-reset enumeration.**  The gadget
     bound at ~5.6 s but the host only configured it on a physical re-plug
     (~28.6 s).  The warm-reboot A/B isolates the session-32
     `pb_wl1283_reset_pulse()` in `omap_hsmmc_probe()` (GPIO2_22 low 500 ms →
     high): image `0x90BDE221` (FREF off, pulse on) fails, image `0x0353A241`
     (FREF off, pulse off) enumerates by itself in 14 s (gadget configured at
     5.28 s).  The FREF2 enable and the session-33 MMC changes are excluded
     (`0x5920CAAC` with the session-32 code and `0x90BDE221` without FREF both
     fail; the session-33 changes are confined to `480d5000`).  The pulse is
     removed from the tree — it was also negative for CMD5.  A UDC
     `soft_connect` disconnect/connect re-enumerates the host (Device 117 →
     118) and remains a fallback.  (Session 33, report 188.)

131. **`/pps/services/hw_info/inventory` does not contain a WLAN node, and
     there is no static HWI database to read.**  `hw_info_pps` builds the
     inventory at runtime from `confstr(_CS_MACHINE)`, `/dev/hd0` devctls,
     `/proc` and the ALSA mixer; its attributes are `AudioCodec_Name`,
     `Processor_*`, `Board_*`, `eMMC_*`, `CPU_Type`, `RAM_Size` and
     `Processor_Numbers`.  It links no HWI library, and no `etc/hwi*`, `hw*`
     or `board.tdf` file carries the node.  The `WLAN` hwinfo node is in the
     boot-time **syspage**, and only `devnp-ti1283.so` (which carries its own
     `hwi_find`/`hwi_tag_find`/`hwi_off2tag`) reads it.  Use
     `tools/qnx-hwi.c` on a live TabletOS boot to dump it (`/tmp/qnx-hwi WLAN`
     or `all`); do not spend a boot expecting the PPS inventory to answer the
     WLAN question.  **Confirmed live (session 34, report 189 §8):** the node
     exists at syspage offset `0x344` with `itemsize=13`, an empty `location`
     tag (base/len/regshift/addrspace all 0) and exactly one `optstr`:
     `WLAN_EN_GPIO=54,WLAN_INT_GPIO=53`.  There is no rail, clock or second
     enable in it.  **Build the tool with `-static`** — the device's 2014 libc
     does not export `hwi_off2tag`/`__hwi_find_string`/`hwi_next_tag`/
     `hwi_next_item` (the driver carries its own), so a dynamically linked
     build dies with `ldd:FATAL: Unresolved symbol "hwi_off2tag"`.
     (Session 34, report 189.)

132. **`MMC_CAP2_NO_SDIO_RESET` is not upstream, and `MMC_CAP2_NO_SDIO` must
     not be used to skip CMD52.**  The bit was a session-33 custom patch
     (image `0xAC98BCAD`) and is not in the current tree.  H1 needs the quirk
     at the `sdio_reset()` call site
     (`if (!(caps2 & MMC_CAP2_NO_SDIO) && !(caps2 & MMC_CAP2_NO_SDIO_RESET))
     sdio_reset(host);`); setting `MMC_CAP2_NO_SDIO` would skip the CMD5 probe
     entirely.  (Session 34, report 191 §1.5.)

133. **MMC5 has no `ti,dual-volt`; it always uses SDVS18, and the 3.3 V
     `vwl1283` `vmmc` is an OCR advertisement, not the signalling voltage.**
     `ti,dual-volt` is on `mmc1` only (`omap4-l4.dtsi:2164`); `&mmc5` lacks
     it, so `omap_hsmmc` takes the non-dual-volt branch (`hctl = SDVS18;
     capa = VS18;`).  Do not "fix" the 3.3 V window by blindly reverting to
     1.8 V; verify `HCTL = 0x00000B00` (SDVS18 | SDBP) and `CAPA` VS18.  The
     known-good 1.8 V vs current 3.3 V DTB difference is a tree-state
     difference, not a 3.3 V-signalling hazard.  (Session 34, report 191
     §1.4.)

134. **The GPIO OE banks are not identical, and a `DATAIN` read on a pad with
     the input buffer off proves nothing.**  QNX drives GPIO1 bits
     0/4/15/18/28/31 and GPIO2 bits 5/7 that Linux leaves as inputs (Linux
     drives GPIO2 bit 20, the hog).  GPIO1_31 is `gpio_wk31` on WKUP pad
     `0x4A31E058` (mux 3, input buffer off), so QNX drives it low and Linux
     leaves it floating; the dossier's "reads low either way" is meaningless.
     The QNX `startup-playbook` binary (`local/work/ifs1917.imagefs` offset
     `0x908`, size `0x44008`, source `hwi_omap4430.c`) was never audited and
     is the likely source of these outputs; audit it before treating the GPIO
      banks as matched.  (Session 34, report 191 §1.1/§1.3.)

135. **`startup-playbook` is the source of the QNX-only GPIO outputs, and it does
      not touch `CLK32KG`.**  The 1917 imagefs `startup.*` (`0x44008` bytes at
      offset `0x908`, raw ARM load base `0x80100000`) applies a runtime-selected
      board GPIO table that marks **`GPIO1` bits 0/4/15/18/28/31 and `GPIO2`
      bits 5/7 as outputs driven low**, and `gpio 54` (WL_EN) output-low /
      `gpio 53` (WL_IRQ) input/falling-with-pull — the exact live QNX OE subset.
      The Winchester table is at image `0x80122750` (32-byte records: `+0` gpio
      id, `+4` direction `1`=output, `+8` level, `+0xc` detect, `+0x14/+0x15`
      rev, `+0x16` phase); the `151/152`/`151/163` strings are the other boards.
      It also builds the syspage `WLAN` node (`WLAN_EN_GPIO=54,WLAN_INT_GPIO=53`)
      and sets `WLDR = 0xFFE2B400` (58.6 s).  It has **no TWL register
      programming**: voltage setup is the OMAP PRM voltage controller
      (`0x4A30_0BF8`/`BA0`), and the only `0xbc` immediate is a device MMIO
      offset; the `pmic-twl6030_winchester2.cfg`/`pmic-winchester-twl6030` path
      is regulator group/state/trans only and has no `CLK32KG` string.
      **Both-OS reads settled `CLK32KG`:** it is at base `0x8C` (kernel
      `clk-twl.c`, not the FreeBSD `0xBC`), with `CFG_GRP`/`CFG_TRANS`/
      `CFG_STATE` = `0x8C`/`0x8D`/`0x8E`.  QNX `01/05/05`, Linux `07/05/55`:
      `CFG_TRANS` (`0x8D`) is **identical**, `CFG_STATE` (`0x8E`) is APP-ON in
      both (CON/MOD bits differ), `CFG_GRP` differs — report 180's labels were
      swapped.
      `0xB0`–`0xB2` is **REGEN2** (QNX `00/01/00`, GPIO-controlled on DVT3+;
      Linux `07/05/55`) — the display/panel rail.  `0xBC`–`0xBE` is an unnamed
      non-regulator resource, not `CLK32KG`.  (Session 35, report 192;
      contradiction resolved.)

136. **The eight QNX-only GPIO outputs are NOT the WL1283 enable/reset/power
      — S3 is negative.**  On the live 1917 (`ti0` UP/RUNNING), set each OE bit
      to input one at a time — `GPIO1_OE 0x4A310134` bits 0/4/15/18/28/31,
      `GPIO2_OE 0x48055134` bits 5/7 — and check `GPIO2_DATAIN` (`0x48055138`)
      bit 21, `ifconfig ti0` and `/pps/services/wifi/status`: every flip left
      the chip up (`DATAIN` bit21 = 1, `ti0` RUNNING, `wifi_power::on`) and the
      OE bit stuck (no re-drive).  Restore by clearing the bit and
      `ifconfig ti0 down/up`.  **Verify the flip actually took:** `gpio31`
      (`gpio_wk31`, WKUP pad `0x4A31E058` mux 3) has its input buffer off, so
      `DATAIN` bit31 reads 0 either way — temporarily set pad bit8
      (`0x000B0003 -> 0x000B0103`) to read the pin (it is driven low and stays
      low when released: external pull-down), then restore the pad.  Do not
      repeat the bisect; the remaining WL1283 difference is chip-side or OMAP
      HSMMC sampling.  (Session 35, report 192 §8.)

137. **H1 — the exact QNX SDIO sequence (CMD0 → CMD5 only) is also negative.**
      Built and booted image **`0x29784F48`** (`0x91A2D4`, sha256 `64f70585…`,
      `local/images/kernel-0x29784F48-h1-nosdioreset.img`) with four hunks:
      `MMC_CAP2_NO_SDIO_RESET (1 << 30)`; skip `sdio_reset()` in
      `mmc_rescan_try_freq` when it is set; for `480d5000` only set
      `MMC_CAP2_NO_SDIO_RESET | MMC_CAP2_NO_SD | MMC_CAP2_NO_MMC` (never
      `MMC_CAP2_NO_SDIO`); and `omap_hsmmc.init_hz=24000000
      omap_hsmmc.sdio_ocr_quirk=1` in the DTS `bootargs`.  The trace is exactly
      `sdio_reset skipped` → `go_idle` → 24 MHz (`CLKD=2`) → `op=0` →
      `attach_sdio` → **`op=5 arg=00ffff00 err=-110 resp0=0`** → `attach_sdio no`
      (no CMD8, no CMD55/CMD1).  `HCTL = 0x00000b00` (SDVS18|SDBP) and
      `CAPA = 0x04e10080`, confirming H2 (1.8 V signalling; the 3.3 V `vmmc` is
      OCR only).  **The host sequence is not the difference; the software
      surface is exhausted.**  The test image was replaced by `0xE36C679A` and
      the four hunks reverted; log `local/logs/boot-H1-0x29784F48.log`.
      (Session 35, report 192 §9.)

138. **An enabled I²C controller is not a wired bus — mux its pads.**  `&i2c2`
      and `&i2c3` were `status = "okay"` in the board DTS with **no
      `pinctrl-0`**, so the controllers probed but their SCL/SDA pads stayed in
      **safe mode**: live 16-bit reads, `0x4A100126` (i2c2_scl) and
      `0x4A100128` (i2c2_sda) = **`0x011F`**, versus `0x0110` on the working
      I²C1 pads.  The symptom is a **timeout**, not a NAK: an absent address on
      a working bus returns `--` + `Remote I/O error`, while every i2c-1
      read/write returned `--` + `Connection timed out`, and a whole-bus
      `i2cdetect -y -r 1` blocked the ttyGS0 console for over a minute (send
      Ctrl-C on `/dev/ttyACM0` to recover).  Writing both pads to `0x0110`
      (mux 0, input, pull-up) made the touch at `0x65` ACK immediately.  The
      same fix is the touch-bus resolution: **I²C2 `0x48072000` = Linux
      `i2c-1`**, not `i2c-2`/`i2c-3` (report 193, DECISIONS §44).  When a bus
      "does not work", read its pad mux before suspecting the device; and when
      the board DTS enables a controller, add the pin group in the same change.
      (Session 36, report 193.)

139. **The tree-vs-pristine patch walk picks up build artifacts on a built
      tree.**  The recipe in `kernel/patches/README.md` §"Regenerating the
      patch" keeps a `GEN` exclusion set that is incomplete: run on
      `local/kernel-tree` after a build it produced **85 files / 371 678
      lines** instead of the true 47 files, because the tree contains
      `.vmlinux.export.c`, `.tmp_vmlinux*.kallsyms.S`,
      `scripts/{mod,dtc,genksyms,kconfig}/*` generated sources,
      `crypto/*.asn1.[ch]`, `lib/oid_registry_data.c`,
      `lib/crc/crc32table.h`, `drivers/tty/vt/ucs_*`,
      `drivers/video/logo/*.c`, `net/wireless/shipped-certs.c`, `*.dtbo.S`,
      and the build copies under `arch/arm/boot/compressed/`.  `make clean`
      first, or restrict the walk to the known source paths; only use the old
      patch's file list as the source set when no new source file was added
      (session 36 did, and the export verified 47/47 byte-identical).
      (Session 36.)

140. **The Gen3 touch boots into its Munin bootloader; `reg0` is a command
      register, and it is not a `cyttsp` part.**  At I²C2 `0x65` with **1-byte
      register addressing**, `reg0` is a command and `reg1` is status/data.
      A cold boot reads `reg1` = `0x09` (bit3 = bootloader, bit0 = just-reset)
      and registers `0x00`–`0x07` = `00 09 b0 05 09 00 b7 2f`; this is the
      bootloader, not a fault.  The vendor boot is `reg0 = 0x81`, sleep 10 ms,
      **read 21 bytes at `reg1`** (status, bootloader ID `0xB0` = Pegasus, ver,
      app start `0x900`, app checksum `0xB72F`, app version `0x020A`, silicon
      `0x0586`, build `0x3B`), `reg0 = 0x80` (start app), sleep 50 ms, then
      `reg0 = 0x02`, poll `reg1` bit0, `reg 0x1D = 1`, `reg 0x1F = 1`.  The app
      report is **18 bytes at `reg1`**: a status byte plus four 4-byte contacts,
      `X = b0 | ((b2 & 0x0F) << 8)`, `Y = b1 | ((b2 & 0xF0) << 4)`, `b3` =
      down/id, `0xFFF` = no contact; accept a read when `byte17 & 1` equals the
      status counter bit.  **No firmware upload is needed on this unit**: the
      chip's app version/build matches `Pegasus_50.hex` (build `0x3B`;
      `Pegasus_50.hex` == `Pegasus2_50.hex`; `Pegasus_D4_A.hex` is build
      `0x35`).  Mainline `cyttsp_core` (Gen2) / `cyttsp5` (Gen5) do not speak
      this protocol — write a small driver (DECISIONS §45).  (Session 36,
      report 194.)

141. **A blocking retry in a built-in probe delays the boot enough to break
      USB warm-reset enumeration.**  The touch controller is not ready while
      the i2c adapter is still probing (the first read at ~2.7 s returns
      `-EREMOTEIO`), so the driver retried 40 × 50 ms inside `probe()`.  That
      2.6 s delay moved the boot: the gadget bound at 5.78 s but the host
      configured it only after a physical replug (74.7 s) — the same class as
      trap 130, from a different cause.  Keep the probe non-blocking and run
      the controller boot in a delayed work that retries (`MUNIN_BOOT_RETRIES`,
      500 ms); the driver then binds, logs the 21-byte block and starts polling
      without touching the boot timing.  (Session 36.)

142. **A DT node with no `status` property is enabled; deleting the board
      override does not disable it.**  `omap4-l4.dtsi`'s `mmc5` node has no
      `status`, so it is enabled by default.  When the session-37 archive
      removed the board DTS `&mmc5 { status = "okay"; … }` block, the controller
      would still have probed — with no `pinctrl`, no `vmmc` and no board
      wiring.  The base DTS must therefore set `&mmc5 { status = "disabled"; }`.
      Check the dtsi default before assuming "remove the override = disable".
      (Session 37.)

143. **Archiving the WL1283 `PBCMD`/`PBOCR` prints removes the CMD5 error
      code from the boot log.**  The minimal 1917-truth `&mmc5` host
      (`0x0DC36A11`) reproduces the negative — `PBC: mmc4 attach_sdio` →
      `attach_sdio no` → `Failed to initialize a non-removable card` — but the
      log cannot say whether CMD5 timed out (`-110`) or NAKed, because the
      per-command prints were moved to `kernel/archive/wl1283/` (Task 1).  The
      absence of a "no support for card's volts" warning places the failure at
      CMD5 (before voltage selection).  For a future run that needs the exact
      error, re-add a one-line `PBCMD`/`PBOCR` trace (or apply the archive
      patch) — do not mistake the missing line for a different failure.
      (Session 37, report 196.)

144. **A 64-bit divide in kernel C on 32-bit ARM needs `div_s64()`, not `/`.**
      `munin_map()`'s first build used `s64 num / span` and the link failed with
      `undefined reference to __aeabi_ldivmod` (`drivers/input/touchscreen/
      playbook-munin.o`).  Include `<linux/math64.h>` and call `div_s64()` (or
      `div64_s64()`); the compiler runtime is not linked into the kernel.
      (Session 38, report 198.)

145. **Root SSH works on 2.1.0.1917, but only after the door-hold has uploaded
      the key.**  A cold TabletOS boot's first `tools/pbssh.sh` attempts (default
      `root`) fail with `Permission denied (publickey,password)`; the same script
      succeeding as `devuser` is *not* evidence that root is disabled — the door
      handshake had simply completed between attempts.  Start
      `setsid tools/door-hold.sh`, wait for it to connect, then retry as root.
      The old "1917 only accepts devuser" comment in `tools/pbssh.sh` was wrong.
      (Session 38; operator correction.)

146. **The Munin touch IRQ fires at the controller's report rate (~330–380/s)
      with no finger — that is not noise.**  The driver reads the 18-byte report
      on every falling edge; the read releases the line and the next report pulls
      it down again.  Prove it by unbinding the driver
      (`echo 1-0065 > /sys/bus/i2c/drivers/playbook-munin/unbind`): the
      `/proc/interrupts` count freezes; rebinding resumes it.  The vendor library
      attaches an ISR too (`tp_intr_attach`).  Do not chase it as an electrical
      fault; the 20 ms poll is the fallback if the IRQ request fails.
      (Session 38, report 198.)

147. **`SND_SOC_WM8994` has no Kconfig prompt; `scripts/config -e` does not
      enable it.**  The symbol is `tristate` with only `depends on MFD_WM8994`
      and no description, so `olddefconfig` silently drops a hand-set value.
      It must be `select`ed by a machine driver (the samsung/fsl boards do
      this); add a board `sound/soc/ti/Kconfig` entry that selects it (and
      `SND_SOC_OMAP_MCBSP`).  Similarly, `CONFIG_SND` is capped at `m` unless
      `CONFIG_SOUND=y` — a built-in sound core needs both.  (Session 38,
      report 199.)

148. **Enabling `&l4_abe` hangs on the WD_TIMER3 target (0x40130000) in
      ti-sysc, right after its revision read.**  The board DTS therefore
      keeps `&l4_abe { status = "disabled"; }`, but the McBSP1/2/3 audio
      peripherals are inside that segment.  The WD target
      (`target-module@30000`) has **no label** (only its child `wdt3:
      watchdog@0` does); label the target in `omap4-l4-abe.dtsi` and keep it
      `status = "disabled"` while enabling the segment.  Attack this one
      variable per boot: the loader's WDT2 is 7.8 s until the kernel's late
      reprogram, so a deterministic hang can reset-loop and drain
      `BOOTCTR_FCTRY`; keep a known-good image ready.  (Session 38, report
      199.)

149. **Do not build the ASoC card/codec/McBSP into the kernel: the card
      registration delays the USB gadget enough to break self-enumeration.**
      The built-in `asoc-simple-card` probe took ~1.3 s (3.35 s → 4.63 s) and
      the host configured the gadget only after a physical replug — the
      trap-141 class again.  Build `MFD_WM8994`, `SND_SOC_WM8994`,
      `SND_SOC_OMAP_MCBSP` and `SND_SIMPLE_CARD` as modules and load them from
      the region-1 `rcS` **after** the initramfs gadget is up.
      `snd-soc-simple-card` also needs `snd-soc-simple-card-utils.ko` or it
      fails with a page of `Unknown symbol simple_util_*` errors, and the card
      never registers.  (Session 38, report 199.)

150. **A generic machine driver cannot clock the WM8994: `simple-audio-card`
      cannot set its AIF1CLK source.**  `simple-audio-card,mclk-fs` calls
      `snd_soc_dai_set_sysclk(codec_dai, 0, …)`, but the WM8994 driver expects
      `WM8994_SYSCLK_MCLK1 = 1`, so the call returns `-EINVAL` and
      `snd_soc_dai_hw_params()` fails with `AIF1CLK not configured`.  A board
      machine driver must call `set_sysclk`/`set_pll` itself.  Related: the
      FLL refuses to be reconfigured while it is the active SYSCLK
      (`FLL1 is currently providing SYSCLK`, `-EBUSY`); move `AIF1CLK` to
      MCLK1 before `set_pll` when `hw_params` is called again.  (Session 39,
      report 200.)

151. **The WM8994 MCLK1 pin does not clock *under our Linux image* — but it does
      under TabletOS (corrected session 40).**  The 1917 driver configures FLL1
      from MCLK1 at 12.8 MHz (`REFCLK_DIV=0`, `FRATIO=0`, `OUTDIV=7`, `N=7`,
      `K=0xAE14`), and on live TabletOS FLL1 **is locked**
      (`0x732 = 0x6020`, bit 5 = 1) with `0x224 = 0x0c80`
      (`REFCLK_SRC = MCLK1`) and `0x220 = 0x0005` (`ENA|FRAC`) — report 204.
      Under Linux the same FLL bytes read back correctly and never lock, at any
      SCRM `AUXCLK1` divider (÷1…÷8), with the codec reset or with the PM1/PM2
      bias on; the datasheet's free-running FLL does not lock either.  Both OSes
      read `AUXCLK1 = 0x00020100` and pad `0x19C` mux 0, so the difference is
      below those registers (pad drive, a supply, or `/RESET`).  **Do not** say
      "MCLK1 is dead on this unit"; say "it does not clock under our image".
      Also note a Linux boot can leave `AUXCLK1 = 0x00000100` (÷1 = **38.4
      MHz**, outside the FLL's 0.032–13.5 MHz range) until the codec driver's
      first `set_sysclk(MCLK1, 12800000)` calls `clk_set_rate()`; the value
      survives a warm reset, so it is not deterministic — set it, do not assume.  (Sessions 39–40,
      reports 200/203/204.)

152. **The WM8994 stops accepting register writes after boot; reads still
      work, and the regmap cache then diverges.**  *(Explained by trap 155: the
      cause is `AIF1CLK_ENA = 1` with a stopped source, not a broken bus.  The
      observation below stands; the "chip" conclusion was incomplete.)*
      The `rcS` mixer writes and
      the DAPM `AIF1CLK_ENA` write land, but later writes (the driver's FLL
      configuration at `hw_params`, and even direct `i2ctransfer` writes to
      `0x0220`/`0x0224`/`0x0601`) return success and do not change the
      register.  The driver's cache reads `0224: 0c83`, `0220: 0001` while
      the chip reads `0x0c80`, `0x0000`; because the cache already matches,
      `regmap_update_bits()` skips the write, so a plain `component_write`
      or a `cache_bypass` write is needed to see whether the chip accepts it.
      `cache_only=N`, `cache_dirty=N`, the codec/MFD are runtime-`active`, and
      `power/control=on` does not change it.  The stream therefore runs at the
      OSS default 8 kHz with `AIF1CLK` from the dead MCLK1.  Next: write a
      register at the first prompt and again after one playback, bisect the
      boot, and try configuring the FLL in `.trigger(START)` (the earliest
      hook after the DAPM stream event).  (Session 39, report 200.)

153. **The ACM console drops lines; re-run a filtered listing to confirm a
      result.**  `pbmix list "<name>"` returned nothing for a control that
      `pbmix list | grep "<name>"` then showed (and the same control's
      `0x0601` read as `1`).  Do not treat a missing line from a long console
      command as evidence; repeat it, or filter on the device.

154. **The tree-derived patch export found two files the previous export had
      omitted.**  The session-38 48-file patch was verified 48/48
      byte-identical but it did not list `drivers/input/touchscreen/Kconfig`
      and `drivers/input/touchscreen/Makefile` (the touch driver's Kconfig/
      Makefile entries), so those changes were never applied by the patch.
      A tree-vs-pristine walk (after `make clean`, trap 139) now yields
      **53 files / 7 773 lines**, sha256 `434d06c3…`, 0 rejects and 53/53
      byte-identical.  Keep using the tree walk and diff its file list
      against the old patch before exporting.  (Session 39.)

155. **The WM8994 freezes its register map when `AIF1CLK_ENA = 1` with a stopped
      clock source.**  Datasheet Rev 4.6, "Control Interface Clocking": "If the
      AIF1CLK source is stopped, then AIF1CLK_ENA must be set to 0 for control
      register access."  In that state the I²C bus ACKs, writes return success
      and are dropped, and reads keep answering (with the values from before the
      freeze).  Clearing `R0x200` bit 0 (`= 0x0000`) thaws it immediately, and a
      software reset does too.  Our driver is in that state from boot, because
      the DAPM `AIF1CLK` supply powers up with the `rcS` playback path while
      `AIF1CLK_SRC` is still MCLK1 — and MCLK1 does not clock under our image — so
      the FLL configuration written at `hw_params` is lost.  This is report
      200's "write blocker"; report 203 is the measurement.  **The reference does
      the opposite and it is the rule to copy: on live TabletOS `0x200 = 0x0010`
      — source already FLL1, `AIF1CLK_ENA = 0` while idle** (report 204), so its
      register map is always accessible.  Never leave `AIF1CLK_ENA = 1` when
      idle.  (Session 40.)

156. **A fractional FLL1 needs `FLL1_FRAC` (R544 bit 2), and a plain whole-register
      write of `0x220 = 0x0001` clears it.**  The 1917 driver does
      `0x220 |= 0x0004` after N/K/FRATIO and then sets `FLL1_ENA` with a masked
      write that preserves bit 2; the mainline `_wm8994_set_fll()` sets
      `WM8994_FLL1_FRAC` in its enable write too.  A hand-written FLL sequence
      that writes `0x220 = 0x0001` runs in integer mode with a fractional N/K and
      never locks.  Use a read-modify-write (`0x0005`), and give the FLL ~100 ms.
      (Session 40, report 201/203.)

157. **Reserved bits read back 0, so one register is not a valid write test.**
      `R0x223` holds `FLL1_N` at `[14:5]` (`N = 7` → `0x00E0`, not `0x0008`);
      `R0x220` bit 2 is `FLL1_FRAC`; `R0x601` bit 6 is reserved.  Writing a
      reserved bit and reading 0 looks exactly like "the chip ignored the write".
      Probe with a register and a bit that are known writable — `R0x601` bit 5
      (`0x0020`) works — and thaw (`R512 = 0`) first.  (Session 40.)

158. **`devmem` on a module whose interconnect target is idle aborts on the L3
      bus.**  Reading `0x4805B038` (GPIO5 `DATAIN`) eight times produced eight
      `Unhandled fault: external abort on non-linefetch` / `L3 Custom Error:
      MASTER MPU TARGET L4PER2` pairs and a `W`-tainted kernel.  It is not fatal
      — the console and the unit stay up — but it costs a clean `dmesg` and a
      reboot to clear the taint.  GPIO5 is not among the banks our DT state
      clocks.  This is trap 100's class: do not poke a module the DT has not
      enabled.  (Session 40.)

159. **Do not conclude "the writes are dropped" without first proving the codec is
      thawed.**  Two session-40 tests "proved" the driver's writes were lost
      while the codec was already frozen from an earlier experiment, and one
      "proved" the opposite by writing reserved bits.  The reliable shape is:
      write `R512 = 0`, write/read a known-good scratch bit (`R0x601` bit 5),
      then run the test.  (Session 40.)

160. **`pbi2c` now reads 16-bit-register devices; use it to read the codec on
      TabletOS.**  The QNX i2c ABI is a 16-byte header `{addr7, len, nbufs,
      flags}` for writes and a 5-word descriptor `{addr7, 2, regsize, n, 1}` for
      reads; for the WM8994 (16-bit register, 16-bit data) the header is
      `{0x1a, 4, 2, 1}` with a 2-byte big-endian register iovec, and the read
      descriptor's register-size word is 2.  `tools/pbi2c rd16|dump16|wr16`
      implements it (`tools/qnx-i2c.c`, built with the in-tree QNX 6.6
      toolchain) and reads the ID as `0x8994` on the first try.  This is how the
      reference's live codec state was obtained (report 204).  (Session 40.)

161. **The kernel patch export must be driven from the tree, and `omap-mcbsp.c`
      was missing from the session-39 export.**  Session 41 changed
      `omap4-winchester.dts`, `playbook-wm8994.c` **and**
      `sound/soc/ti/omap-mcbsp.c`; only the first two were already in the patch.
      The export is now **54 files / 8 113 lines**, sha256 `fc3b2e54…`, verified
      0 rejects / 54/54 byte-identical.  Two lessons: (a) check every modified
      file is present with `grep '^+++ b/<path>$'`, not by assuming; (b) do
      **not** run the README's tree-walk on a built tree — it returned **90
      files** because it sweeps build artifacts (`.vmlinux.export.c`,
      `.tmp_vmlinux*.kallsyms.S`, `crypto/*.asn1.[ch]`, `scripts/dtc/*`,
      `drivers/tty/vt/ucs_*`, `logo_*.c`, `lib/oid_registry_data.c`, …).  Either
      `make clean` first or drive the export from the previous patch's file list
      plus any genuinely new source.  (Session 41.)

162. **After a period of dropped codec writes, `update_bits()` is a silent
      no-op — discard the regmap cache first.**  The WM8994 freezes its register
      map when `AIF1CLK_ENA = 1` with a stopped source (trap 155), so writes are
      dropped while the *cache* still records the intended value.  Every later
      `snd_soc_component_update_bits()` compares against that cache, sees no
      difference, and never touches the bus — so a driver can log a successful
      write while the hardware holds the reset value.  Measured: PM3 read
      `0x0000` after the driver had "written" `0x00F0`, and only
      `regcache_mark_dirty()` + `regcache_sync()` made the writes land.
      This is the same class as report 200 / DECISIONS §49.  Whenever a period
      of dropped writes is possible, mark the cache dirty before writing again.
      (Session 41.)

163. **Copy a rebuilt `.ko` into `local/work/modules-7.3.3/` before
      `build_rootfs.py`, and verify it landed.**  Session 41 rebuilt
      `snd-soc-playbook-wm8994.ko`, flashed a rootfs, and the change had no
      effect because the staging directory still held the previous module — the
      device's copy had no new strings.  The build script only walks the staging
      directory, so a forgotten copy is silent.  Check with
      `debugfs -R 'dump /lib/modules/<krel>/kernel/<name>.ko /tmp/x' <image>`
      and grep the extracted file for a string only the new build has.
      (Session 41.)

164. **A console that echoes input but prints nothing is a BUSY SHELL, not a
      dead console.**  The target prints no prompt while a foreground command
      runs, but the ACM tty keeps echoing every byte sent.  Session 41
      mis-diagnosed this as a wedged console repeatedly and re-ran
      `pkexec chmod 666 /dev/ttyACM0`, which does nothing for it (the chmod is
      only needed after a genuine re-enumeration).  Use `tools/pbraw.py <secs>
      0a` to see the raw bytes — buffered output arrives at once, proving the
      console was fine — and `tools/pbraw.py 2 03` to send Ctrl-C.
      `tools/pbcon.py` now waits for a prompt up to `PBCON_SYNC` seconds and
      reports "the shell is still busy" instead of queueing bytes into it.
      (Session 41.)

165. **No hardware probing is available — every experiment must be bench or
      static-RE only.**  The operator has **no scope, no logic analyser, no
      boardview and no pad-level knowledge** of the board (stated at session 41
      end).  Do not propose an experiment whose answer is "probe pin X" or
      "measure pad Y": it cannot be run.  Substitute a software-visible
      equivalent — a register readback, a `/dev/mem` loopback, a capture stream,
      a QNX `memdump3` read of a window Linux cannot touch, or static RE of the
      reference driver.  The audio next-steps in `HANDOFF.md` §0c are written to
      this constraint.  (Session 41, operator.)

166. **`i2ctransfer` arguments are DECIMAL — a shell loop over `$r` reads the
      wrong register.**  `for r in 732 …; do i2ctransfer … $((r>>8)) $((r&255))
      …; done` reads register `0x2DC`, not `0x732`, because the shell treats
      `732` as decimal.  Session 42's bulk register loop therefore returned
      zeros/garbage ("all registers read 0" was an artefact, not a device
      state).  Always write the two address bytes explicitly in hex
      (`i2ctransfer -f -y 3 w2@0x1a 0x07 0x32 r2`).  (Session 42.)

167. **A frozen codec drops raw `i2ctransfer` writes too — not just regmap
      writes.**  The freeze rule (`AIF1CLK_ENA = 1` with a stopped source,
      trap 155) applies at the chip level, so hand-written register surgery
      over I²C silently half-lands: session 42's first mid-stream injection
      stuck `R0x224` while `R0x220` did not take.  Before any register
      sequence on the WM8994, thaw first (`R0x200 = 0` — that write itself
      always lands), do the surgery, then re-enable AIF1CLK last.  A sequence
      that half-lands looks exactly like a flaky device.  (Session 42.)

168. **The driver's `set_pll()` clears `FRC_NCO` — a hand-forced free-running
      NCO does not survive the next `hw_params`.**  `wm8994_set_fll()` writes
      `FLL1_CONTROL_5` with the `FRC_NCO` bit derived from the requested
      source, so any stream start reverts a hand-set NCO to the (dead)
      MCLK1-referenced FLL.  To test the NCO mid-stream, inject *after*
      `trigger(START)` + the repair (>50 ms), using the freeze-safe sequence of
      trap 167.  (Session 42.)

169. **`pbplay`'s `Input/output error` after five periods is a usable
      stream-health observable.**  With a stalled transmit path the OSS write
      dies at ~5 × 4096 frames (~1 s, 82 176 bytes); with a draining path the
      same command runs its full duration and exits cleanly.  A run that lives
      past the five-period point proves the ring drains — i.e. XRDY asserts,
      i.e. BCLK exists — with no scope and no register access.  (Session 42.)

170. **An OS 2.x `.rcfs` is a QNX6 filesystem, not a BB10 `rimh` container.**
      Mount it with `local/vendor/bb10mt/bb10mt qnx6 mount -i <copy.rcfs> -m
      <dir>`; `tools/rcfs_extract.py` is for BB10 `.RCFS` images and will not
      read it.  Work on a **copy**: the mount is read-write and edits/deletions
      persist on unmount.  Do not rebuild the filesystem from a tree.  (Session
      43, operator.)

171. **Pruning the RCFS does not shrink the flashed container.**  `bbusb.py`
      packs only runs of non-`0xFF` blocks, and the blocks freed by
      `bb10mt qnx6 mount` are not `0xFF`; Stage A and the pruned Stage B both
      packed to 363,790,928 B.  Pruning is a live-filesystem win (314.9 → 90.4
      MiB).  `bb10mt qnx6 compact` is the tool for a smaller file.  (Session
      43.)

172. **`slay -f <script>.sh` does not kill a script.**  `slay` matches the
      executable name (the script runs under `/bin/sh`), so
      `slay -f sshd-watchdog.sh` is a no-op.  Match the argument via
      `pidin arg` and `slay -f <pid>`.  Killing a parent can also take its
      child (e.g. `sshd -D`) with it; restart `sshd` with `on -d /usr/sbin/sshd`.
      (Session 43.)

173. **The minimal 1917 oracle is unauthenticated.**  `qconn port=9090`, root
      `sshd` (`PermitRootLogin yes`, `StrictModes no`) and the packet filter
      off are a **bench-only** configuration.  Never ship it or connect it to a
      network.  Root SSH needs our `local/secrets/rsa.pub` written to
      `/accounts/devuser/.ssh/authorized_keys`; the host key is
      `/var/etc/ssh/ssh_host_rsa_key`.  (Session 43.)

174. **`/base/air` and `/air` are runtime (RAM) entries, not on the RCFS.**
      `pps_common` creates `/base/air/native*` symlinks at boot; the pruned
      image itself has no `air`.  Do not "fix" the image to remove it.  (Session
      43.)

175. **`bb10mt qnx6 compact` is broken/incomplete — do not use it.**  It
      corrupts files in the image (operator, session 43).  The pruned RCFS's
      flash container does not shrink anyway (trap 171); leave the file size
      alone.

176. **WM8994 register writes need the reference driver's write ABI.**  The
      1917 `codec_write` uses a 20-byte inline `devctl` struct
      `{addr, 2, 4, 1, reg, val}` with `reg`/`val` big-endian; `devctlv` with
      iovecs returns an error and the write is dropped.  `tools/pbi2c wr16r`
      implements the reference ABI (`rd16`/`dump16` already work).  Verified by
      toggling `R0x220` and watching `R0x732` follow.  (Session 43.)

177. **Put device tools in `/accounts/devuser/bin`, not `/tmp`.**  `/tmp` is
      wiped by a reboot; the UFS persists.  A **reboot is the fast, clean way
      to restore the reference's register state** after a volatile experiment
      (the minimal image boots in ~30 s).  (Operator, session 43.)

178. **`CM_ABE_PLL_REF_CLKSEL` (0x4A30610C) is the DPLL_ABE reference, and our
      Linux image leaves it at the reset default SYS_32K.**  The reference sets
      it to SYS_CLK (`0`).  It is not the MCLK1 cause (setting it to SYS_CLK on
      Linux does not unfreeze the codec), but it is a real ABE/audio-path bug
      and should be fixed in the DTS/machine driver.  (Session 43.)

179. **The WM8994 freeze detector is a software-only MCLK1 presence probe.**
      Thaw (`R0x200=0`), set `R0x220` to a baseline, select MCLK1 + ENA
      (`R0x200=0x0001`), then write `R0x220` to a different value and read it
      back.  If it lands, MCLK1 is present (unfrozen); if it is dropped, MCLK1
      is absent.  **Validated on the working reference** (write lands) and used
      to prove MCLK1 is absent under Linux.  Do not use a bare FLL toggle with
      the clock removed — the lock bit freezes at its last value.  (Session 43.)

180. **`CM_SYS_CLKSEL` (0x4A306110) is a frequency declaration, not a clock
      mux.**  It is warm-reset-insensitive and tells the hardware the SYS_CLK
      frequency (7 = 38.4 MHz, the reference's value); it does not gate the
      clock or the AUXCLK1 output.  Do not treat it as a suspect for a missing
      clock.  (Session 43.)

181. **`PM_*_PWSTST[25:24]` is `LASTPOWERSTATEENTERED`, a debug latch — not a
      power state.**  TRM Table 3-416: it is a W1toSet field with reset 0, and
      the reference writes 3 (CORE/L4PER/L3INIT) or 2 (MPU); Linux leaves it 0.
      Reading the high half of `PM_CORE_PWSTST` (`0x03003FF7` vs `0x00003FF7`)
      as evidence of a different power history is wrong; the actual state is
      `[1:0]` (`POWERSTATEST`), `[2]` (`LOGICSTATEST`) and the memory-bank
      fields, all identical.  (Session 44, report 209.)

182. **`CM_ABE_PLL_REF_CLKSEL` is forced by the kernel's own clock init, and a
      DTS reparent must include the rate.**  `clk-44xx.c` deliberately sets
      `abe_dpll_refclk_mux_ck` to `sys_32k_ck` and `dpll_abe_ck` to 98.304 MHz
      at clock init.  Changing only the parent leaves the DPLL's M/N programmed
      for 32 kHz, and DPLL_ABE never locks (`clock: dpll_abe_ck failed
      transition to 'locked'` seven times during boot).  The working form is
      `assigned-clocks = <&abe_dpll_refclk_mux_ck>, <&dpll_abe_ck>;`
      `assigned-clock-parents = <&sys_clkin_ck>;`
      `assigned-clock-rates = <0>, <98304000>;` — parents are applied before
      rates, so the M/N are recomputed for SYS_CLK (`CM_CLKSEL_DPLL_ABE =
      0x00804018`, `ST_DPLL_CLK = 1`).  (Session 44, report 209 §7.)

183. **The Linux clock-domain framework re-asserts its own `CLKTRCTRL`
      policy.**  A `devmem` write to `CM*_CLKSTCTRL` is often reverted by the
      kernel within the same command (driver runtime PM / clockdomain state);
      only domains it does not manage keep the new value (`CM2_L4SEC` did).
      Do not read "the write did not stick" as a hardware or interconnect
      fault — compare readbacks and, for a causal test, use the oracle.
      (Session 44, report 209 §6.)

184. **After a kernel flash the ACM gadget may not enumerate until the USB
      cable is physically replugged.**  Twice this session `lsusb` showed no
      `0fca:` device and `/dev/ttyACM0` was absent while the unit was
      demonstrably booted; a replug brought it straight back.  Replug before
      diagnosing the image or the gadget code.  (Session 44, operator.)

185. **Linux gpio numbers are not OMAP gpio numbers — resolve by phandle, never
      by arithmetic.**  `/sys/class/gpio` assigns chip bases in *probe order*:
      GPIO2=512, GPIO3=544, GPIO4=576, GPIO5=608, GPIO6=640, GPIO1=676 (labels
      are just names).  So `gpio_127` = **607** (`&gpio4 31`), `gpio_44` = 524,
      `gpio_49` = 529.  A session-44 replay used 639/556/561 and therefore drove
      GPIO5_31 / GPIO3_12 / GPIO3_17 — the wrong pins entirely.  Use the DT
      phandle plus the chip's `base` file.  **Also audit the WL1283 work:** any
      experiment that used sysfs/global gpio numbers rather than DT/OF phandles
      should be re-checked against this list before the "software exhausted"
      claim is final.  (Session 44, report 211 §3.)

186. **The WM8994's ID read needs the audio "ldo" line (GPIO4_31) high before
      the codec module probes.**  On two builds where the line was left as an
      input until the machine driver loaded, `wm8994 3-001a` failed with
      `Failed to read ID register` (-121) and no card registered; with a
      boot-on `regulator-fixed` on that line (enabled at `late_initcall`, before
      the rcS module load) it probes every boot.  Also: a boot-on regulator has
      use count 0, so `regulator_disable()` warns "unbalanced disables" and
      returns `-EIO` — use `regulator_force_disable()` to gate it.  (Session 44,
      report 211 §1–§2.)

## 3. Corrections to older reports

These twenty-three are preserved deliberately. Each is newer than the text it
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
the startup script's own comment used to say a value of `0` or `> 0x32`
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

**3.18 The bootloader is loaded at `0x80000000` from media file `0x9000`, so
reports 150/151's code addresses are `0x9000` too high.**  Those reports used
`address = file + 0x80000000`, which assumes media file 0 maps to `0x80000000`.
The arm9000 section at media `0x9000` is what is loaded at `0x80000000`, so a
code address `A` there is really `A - 0x9000`.  This also withdraws report 151
§Q5's "wipe table is inconsistent": at media `0x1D31C` (= DRAM `0x8001431C`)
the table holds valid ranges (`0x800267C0`/`0x2B004`, `0x80025E78`/`0x948`).
The data literals are correct as written.  Source: session 26, report 173
§1–§3.

**3.19 Report 176's unidentified `0x09` on I²C1 is the ISL9519 charger.**
Report 176 §1.1 recorded `0x09` on `/dev/i2c0` as unidentified ("candidates:
a board EEPROM/GPIO expander and the NCP5623").  It is the **Intersil
ISL9519** battery charger: VendorID `0xFE = 0x0049`, DeviceID `0xFF = 0x0003`,
charge-current/max-system-voltage registers `0x14`/`0x15`, input limit `0x3F`.
The TWL6030 BCI is not the board's charger.  Source: report 183, session 31,
traps 113/114.

**3.20 Report 109 §8.1's "the WDT2 window cannot be lengthened" is
incomplete.**  `WLDR` is write-protected only while the timer *runs*; the
`WSPR` stop sequence (`0xAAAA` → `0x5555`) allows the load to be rewritten,
and `0xBBBB` → `0x4444` restarts it.  Verified live in session 31:
`WLDR = 0xFFE2B400` (58.6 s, QNX's value) read back and `WCRR` reloaded.
Trap 115; the inline correction is in report 109 §8.1.

**3.21 Report 180 §4's `CLK32KG` register labels are swapped.**  The report claims
"every regulator `CFG_STATE` is identical (`…=0x05`); only group/transition
differ (`CFG_GRP` `0x07` vs `0x01`, `CFG_TRANS` `0x55` vs `0x05`)".  The
`0x05`-in-both register is `CFG_TRANS` (TWL `0x8D`), not `CFG_STATE`; the
`0x55`-vs-`0x05` register is `CFG_STATE` (`0x8E`), not `CFG_TRANS`.  Corrected
statement: `CFG_GRP` (`0x8C`) differs; `CFG_TRANS` (`0x8D`) is identical;
`CFG_STATE` (`0x8E`) differs only in the CON/MOD bits — its APP field (bits
3:2) is `01` in both, so the 32 kHz clock is APP-ON in both.  Sources: session
35, report 192 §7; `local/kernel-tree/drivers/clk/clk-twl.c`
(`VREG_GRP 0`, `VREG_TRANS 1`, `VREG_STATE 2`).  (Session 35.)

**3.22 Report 189 §2.1's `gpio_config` field offsets/labels are off.**
Re-read directly from the mounted 1917 `/base` and cross-checked against
startup-playbook (session 37): the record is `+0x04` direction, **`+0x0c`
edge-detect** (not "pull/attr"; 2 = falling), **`+0x11` pull/enable** (not
`+0x10`), `+0x14/+0x15` rev, `+0x16` phase, and **`+0x18` pad signal id** (not
`+0x1c`, which is zero).  Values: gpio 53 (WL_IRQ) input/falling, signal
`0x59`; gpio 54 (WL_EN) output/low, signal `0x58`.  The same 32-byte records
appear byte-identically in startup-playbook's Winchester GPIO table (image
`0x80122750`, records 13/14), so the hwio database is the table startup
applies.  (Session 37, report 195 §3.2.)

**3.23 "MCLK1 does not clock under our image" (reports 203/204, handoff) is too
strong.**  With the codec thawed, the reference's exact FLL1 programme and
`R0x224 = 0x0000` (`REFCLK_SRC = MCLK1`), FLL1 **did** lock once
(`R0x732 = 0x6020`) — so the capability exists on this hardware under our image
and report 204's blanket denial is wrong.  However it then **failed to lock on
later boots and on a battery-pulled cold boot** with an identical register file
and identical SoC programming, so it is **not reliable**, and the driver
therefore stays on the BCLK path, which locked on every trial.  The real,
reproducible defect was `AUXCLK1` booting at `0x00000100` (÷1 = 38.4 MHz,
outside the FLL's 0.032–13.5 MHz range) — now fixed in the DTS.
(Session 41, report 205 §4b/§5h.)

**3.24 The MCLK1 pad and the AUXCLK mapping were never in doubt.**
`hwio-omap4.so`'s pad resolver maps pad id `0xB7` → CORE `0x4A10019C` **low
half**, and only `0xB7`/`0xE6` map to that offset; the TRM names the register
`CONTROL_CORE_PAD0_FREF_CLK2_OUT_PAD1_SYS_NIRQ1` (mux 0 = `fref_clk2_out`) and
states the AUXCLK rule directly ("`fref_clk1_out` is controlled by the
`SCRM.AUXCLK0` register"), so `fref_clk2_out` ← **AUXCLK1**.  The board DTS was
right; the `0x19E` difference report 204 noticed is `sys_nirq1`, not the clock.
(Session 41, report 205 §8.)

**3.25 The DTS codec supply mapping is not the cause of the silence.**  Measured
read-only on both OSes (`pmic-twl6030-ctl get_state/get_vout`): V1V8 1823 mV,
V2V1 2127 mV, VANA 2100 mV, VDAC 1800 mV, VAUX1/2/3 2800 mV, all ON — the same
rails and voltages, and the same PM2/PM3/PM5 codec values as the working
reference.  The mapping is still out of the datasheet's range and worth fixing,
but it does not explain the fault.  (Session 41, report 205 §5.)

**3.26 `CM_ABE_PLL_REF_CLKSEL = SYS_32K` is upstream's deliberate choice, not a
bootloader leftover or a bug in our image.**  `drivers/clk/ti/clk-44xx.c`
reparents `abe_dpll_refclk_mux_ck` to `sys_32k_ck` and sets `dpll_abe_ck` to
98.304 MHz at clock init ("On OMAP4460 the ABE DPLL fails to turn on if in idle
low-power state … Lock the ABE DPLL in any case to avoid issues with audio").
The 1917 reference runs the same DPLL from `SYS_CLK`, so matching it is a
legitimate board choice — but the register difference alone is **not** the
MCLK1 cause (setting it on Linux never unfroze the codec), and reparenting
without re-applying the rate breaks DPLL_ABE locking (trap 182).  Session 44
fixed it with parent + rate, and DPLL_ABE locks at 98.304 MHz from SYS_CLK
(report 209 §7).

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
  (`drivers/mmc/host/omap_hsmmc.c`).  **`PBREQ`/`PBIRQ` no longer do**: since
  session 17 they are behind the `trace` module parameter (default off), because
  per-request printk to fbcon was itself the write-path bottleneck (trap 52,
  report 136).  Session 30 carried the whole set (including the `init/main.c`,
  `async.c`, `dd.c` and `runtime.c` breadcrumbs) into the 7.3.3 tree
  deliberately, because the first 7.3.3 boot used them.  Pruning is a separate,
  testable change (the completed port's plan records it in
  `docs/history/KERNEL-PORT.md` §7.2).
* **`A/B` and `0x2074` material in `docs/reports/85-*`** plans a two-region
  staging the factory path does not do; the correction is in
  `docs/contradictions/85-ab-fallback-vs-factory-region0.md`, not in the report.
* Older reports keep their original wording where later ones corrected them
  (`118` §8.2, `111` §8 item 4).  The corrections are applied *in place* in those
  two files and listed in §3; the other reports are left as written so the
  section-by-section citations elsewhere still resolve.
