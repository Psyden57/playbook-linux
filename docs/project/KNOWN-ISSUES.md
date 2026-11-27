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
      and registers `0x00`–`0x1c` = `00 09 b0 05 09 00 b7 2f`; this is the
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

187. **`SRGR2.CLKSM = 0` clocks the sample-rate generator from the CLKS pad,
      and this board drives no CLKS.**  TRM Table 23-52: `CLKSM = 0` (with
      `SCLKME = 0`) derives the SRG clock from the CLKS pin; `CLKSM = 1` from
      the internal CPU clock.  Read live: `pad_clks_ck` is 12 MHz with
      `enable_cnt 0` and **no consumer**, while the internal path is up
      (`abe_24m_fclk -> abe-clkctrl:0030:24 -> 40124000.mcbsp`).  So a
      `CLKSM = 0` build has no `CLKG`, no FSX/BCLK, and the transmitter sits at
      `XRDY = 0` / `XEMPTY = 1` — this is the session-41 "SRG→shifter stall",
      not a silicon fault.  `OMAP_MCBSP_SYSCLK_CLK` (the `set_sysclk` id that
      sets `CLKSM`) is what to use; `CLKS_FCLK`'s `clk_set_parent(fclk,
      prcm_fck)` is **not** the SRG mux and does not substitute for it.
      (Session 45, report 212.)

188. **The McBSP SRG divides the ABE *interface* clock, not the functional
      clock the clock framework reports.**  `clk_summary` calls
      `abe-clkctrl:0030:24` (12.288 MHz) the McBSP `fck`, but with `CLKSM = 1`
      the SRG divides `ocp_abe_iclk` = **49.152 MHz** (`DPLL_ABE / 2`).
      Measured: `CLKGDV = 8`, `FPER = 32` gave 192 450 frames/s (4.009× the
      requested 48 kHz).  Compute `CLKGDV` from 49.152 MHz, and **verify by
      measuring `hw_ptr` over a known interval** rather than trusting the
      reported fclk.  (Session 45, report 212 §3.)

189. **`DXR` and `DRR` are not readable data registers on the OMAP4 McBSP.**
      They return constants (`0x00000000` / `0xc8318118`) regardless of
      traffic, which two sessions read as "the transmitter is empty".  The
      registers that answer the question are **`XBUFFSTAT`** (transmit FIFO
      level: it toggles while the sDMA feeds the port) and **`IRQST`**
      (`0x0710` = XFSX|XBR|XEOF while framing).  (Session 45, report 212 §5;
      withdraws report 205 §5l's DXR reasoning.)

190. **`omap-mcbsp` / the machine driver are modules — a change to them does
      not change the chainloader image CRC.**  `make zImage` after editing
      `sound/soc/ti/omap-mcbsp.c` legitimately prints the *same* CRC, so
      "the CRC did not change ⇒ the build was stale" (trap 39) does not apply
      to module-only changes.  Build `modules`, copy the `.ko` into
      `local/work/modules-7.3.3/` (trap 163) and flash **region 1 only** — a
      single-region flash, which is allowed with our Linux image on media.
      (Session 45.)

191. **A listening test must state what is actually playing, and a permission
      failure is a wait-for-the-operator, not a workaround.**  A "no tone"
      verdict in this session came from `pbplay 900 0` — **freq 0 is silence**
      — that had been started for a register dump; the codec was behaving
      correctly.  Read `/proc/asound/card0/pcm0p/sub0/status` (state, owner_pid)
      and the `pbplay` line before judging by ear.  Separately: `pkexec chmod
      666 /dev/ttyACM0` hangs when the operator is away, and the right response
      is to wait and re-run it — not to install a workaround.  (Session 45,
      operator.)

192. **The codec's data ball is `abe_mcbsp1_dx` (pad `0x102`), not
      `abe_mcbsp2_dx`.**  Read live on the oracle while it was actually playing
      a wav: `0x4A100100` = `0x00001108` → pad `0x100` (`abe_mcbsp1_dr`) an
      input with pull-up and pad `0x102` (`abe_mcbsp1_dx`) **mux 0, output**;
      `0x4A1000F8` = `0x000F1108` → `abe_mcbsp2_dx` **mux 7 = safe_mode**.  A
      during-vs-after dump of `0x4A100000`–`0x4A1001FC` shows **no changes**
      (the pad configuration is static), so answer "which pads are outputs",
      never "which pads changed".  Driving `mcbsp2_dx` (E5) is why the codec
      stayed silent.  Also: **McBSP1 cannot be the clock master** — an E6 build
      clocked `0x0fe`/`0x104` at 48 053 frames/s and the codec's FLL did not
      lock, so BCLK/LRCLK are on the McBSP2 balls only.  (Session 45, report
      212 §10.)
      **Corrected (session 46, report 214):** the codec's DACDAT is
      **`abe_mcbsp3_dx`** (pad `0x108`, mode 1); `abe_mcbsp1_dx` is the
      reference's `AIF 4 → MCBSP1` route.  The E6 capture did not quote the
      McBSP3/PDM pads (`0x104`/`0x108`/`0x10C`), which are the codec's.  The
      mcbsp2 clock net does reach McBSP3; the two-engine build works in
      stereo at 48 kHz.  See trap 204.

193. **Never poke an idle L4_ABE peripheral from the kernel — it aborts and
      takes the unit down.**  A machine-driver experiment that ioremap'd
      `0x40124000` (McBSP2) and wrote SRGR1/SRGR2/PCR0/SPCR2 while nothing held
      the interconnect target active produced
      `Unhandled fault: imprecise external abort (0x1406) at 0x00082000` and a
      warm reset.  `clk_summary` had `abe-clkctrl:0030:24` at enable count 0 and
      `pm_runtime_get_sync()` on the mcbsp device did **not** activate the
      target.  This is trap 158 at kernel level: user-space `devmem` on an idle
      target just faults the process, the kernel version panics.  Configure a
      McBSP only through its driver (i.e. through an ASoC link), which owns the
      runtime PM.  (Session 45.)

194. **The E9 stall does not prove McBSP1 and McBSP2 have separate
       BCLK/LRCLK nets.**  A one-off probe
       (McBSP1 as a slave with one word in DXR, `XRDY` set 5 ms later) suggested
       they did; it is **withdrawn** — `XRDY` reads 1 both when a word is consumed
       and when nothing pending was ever shifted (trap 197), and repeating the
       experiment through a real stream does not drain: McBSP1 sits at
       `SPCR2 = 0x0235` while the write fails after five periods.  Combined with
       E5 (the codec's BCLK is on McBSP2's pin) and E6 (driving McBSP1's clock
       outputs does not reach the codec), the board has **clocks on McBSP2, data
       on McBSP1**, and the reference bridges them through the ABE (trap 199).
       (Session 45, report 212 §12/§14.)
       **Corrected (session 46, report 213):** the E9 stall does **not** establish
       separate nets.  The committed `mcbsp1_pins` group leaves `clkx`/`fsx` at
       `0x0000` — the pad's **input buffer is disabled** — while the reference's
       live pads are `0x0100`; a clock consumer needs the input enable.  E9's pad
       values were not recorded and its source was not committed.  Re-test with
       input-enabled pads before concluding (trap 203).
       **Resolved (session 46, report 214):** the codec's AIF1 is on **McBSP3**,
       and the E5 mcbsp2 clock net reaches it (the data link drains, the tone is
       audible in both ears).  The E9 stall was the wrong engine (`mcbsp1`), not
       a missing net.  See traps 204/205.

195. **ASoC `dai_fmt` is codec-perspective, and getting it backwards fails in a
      misleading place.**  A link declared `CBC_CFC` makes the *CPU* DAI the
      clock provider; `omap_mcbsp_dai_hw_params()` then runs its master-mode
      bandwidth check and returns `-EINVAL` with
      `omap_mcbsp_dai_hw_params: not enough bandwidth…`, which reads like a
      format problem.  For a CPU DAI that must be a clock *consumer*, declare
      the link `CBP_CFP`.  (Session 45, report 212 §14.)

196. **A second PCM on the same card must run at the same rate as the first.**
      ASoC's parameter symmetry rejects `hw_params` with
      `unmatched rate symmetry: <cpu>:8000 - soc_pcm_params_symmetry:48000`
      when one link is running at another rate.  `snd-pcm-oss` re-negotiates
      the whole parameter set on every `SNDCTL_DSP_*` ioctl using the runtime's
      *current* rate, so a device opened at the 8 kHz default must be given
      `SNDCTL_DSP_SPEED` **first**; otherwise `CHANNELS`/`SETFMT` fail with
      `-EINVAL`.  (Session 45, report 212 §14.)

197. **Do not read `XRDY` (SPCR2 bit 1) as "the shifter consumed the word".**
      It is 1 both when the transmitter is ready and after a word that never
      left; an E7 experiment read it 5 ms after a DXR write and wrongly
      concluded that McBSP1 was clocked by McBSP2's nets.  The honest
      discriminator is a *stream*: with the driver driving the port, McBSP1 as a
      consumer sits at `SPCR2 = 0x0235` (`XRDY = 0`, `XEMPTY = 1`) and the write
      fails after five periods.  (Session 45, report 212 §14.)

198. **Read the raw `dmesg` tail (and the subsystem's own documentation) before
      reasoning about a failure.**  Session 45 lost time on an `-EINVAL` from
      `SNDCTL_DSP_SETFMT` because the greps looked for "ASAoC error"/format
      words; the unfiltered tail carried
      `omap_mcbsp_dai_hw_params: not enough bandwidth for desired rate and
      channels` — a master-mode bandwidth check caused by a backwards
      `dai_fmt`, not a format problem.  The same applies to runtime facts:
      Linux GPIO bases move with probe order, so read
      `/sys/kernel/debug/gpio` (or the chip's `base` file) instead of trusting a
      number from an earlier boot (trap 185), and read
      `Documentation/sound/soc/*.rst` and `sound/core/oss/pcm_oss.c` rather than
      recalling how ASoC and OSS behave.  (Session 45, operator.)

199. **The reference's McBSP is a clock *consumer* — but its clocks come from
       the codec, not through the ABE.**  Decompiled
       `deva-ctrl-omap4abe-wm8994.so` (report 212 §16): `SRGR2` is written as
       `FPER | 0x1000` (`FSGM = 1`, `CLKSM = 0`), and in slave mode only
       `XRST`/`RRST` are set — no `GRST`/`FRST`, so the SRG never runs.  Mainline
       `omap-mcbsp` can only be a *pad* slave, and McBSP1's pads are not on the
       codec's clock net (E5/E6), so the reference's data path is not reachable
       with the plain driver.  Do not spend another session looking for a pad or
       SRGR permutation that reproduces it.  (Session 45.)
       **Corrected (session 46, report 213):** the consumer half is right; the
       "through the ABE" reading is wrong.  The reference's codec is the AIF1
       master (`R0x302 = 0x4000`), all four McBSP clock pads are inputs
       (`0x0100`) while it plays, the slave path starts no SRG, and
       `StereoAudioCodecRequestMcbspClk`/`RequestABEClk` are stubs
       (`mov r0,#1; bx lr`).  The ABE serial port is a firmware *data* path, not
       a clock (traps 201/202).  The pad/consumer configuration is the remaining
       question (trap 203).
       **Resolved (session 46, report 214):** the port is **McBSP3** (the PDM
       pad group in mode 1); its clocks come from the codec's AIF1 master and
       its data is `mcbsp3_dx`.  The working Linux path is the legacy McBSP
       path with mcbsp2 clocks and mcbsp3 data (traps 204/205).

200. **Edit documentation with the native read/edit tools — never `python`,
      `sed`, `awk` or any script.**  Read the whole target document first, then
      edit it in place.  Scripted rewriting has repeatedly left duplicated
      paragraphs, broken tables, stale cross-references and text in the wrong
      document; the docs have needed a dedicated cleanup pass roughly every ten
      sessions, and scripted editing is the cause.  A script is fine for
      *generating* an artefact (a patch, an image, a capture, a CSV); it is not
      fine for rewriting prose that was not read in full.  (Session 45,
      operator.)

      **The operator had to re-state this in session 76**, which is the point:
      the assistant spent the whole wrap-up re-read rewriting these docs with
      `python3 - <<'PY'` heredocs.  The output happened to be sound (verified by
      diff, duplicate-paragraph and table-row scans), but **three lines were
      left unwrapped** and had to be fixed by hand afterwards, which is the
      mild version of the failure this rule exists to prevent.  If a script ever
      *is* used on a doc, it owes the full check: `git diff -U0` for mangled
      hunks, a duplicate-paragraph scan, a table-row count, and a line-length
      pass.  Do not treat "it worked this time" as a licence.

201. **The ABE serial port is a firmware data path, not a clock source.**
      The HAL Addendum (SWPU245F) and the shipped HAL are explicit:
      `abe_connect_serial_port` writes the AE's software port table and an ATC
      descriptor in ABE memory (`abe_block_copy`), `abe_init_atc`'s
      serial-port case is a **no-op**, and `abe_load_fw` loads the
      `abe_firmware_array` that consumes the table.  There is no hardware-only
      "connect a serial port" path.  The ABE's *clock* hardware is a different
      thing (PRCM CM1_ABE/DPLL_ABE, which mainline drives).  (Session 46,
      report 213.)

202. **The reference's `StereoAudioCodecRequestMcbspClk` and
      `StereoAudioCodecRequestABEClk` are stubs.**  Both are
      `mov r0, #1; bx lr` in the shipped 1917
      `deva-ctrl-omap4abe-wm8994.so` (`0x6e94`/`0x6e9c`).  The reference does
      not ask the ABE for a McBSP clock; its playback port is a slave of the
      codec (AIF1 master, `R0x302 = 0x4000`), and its slave path starts no
      SRG.  The functional-clock source is a per-port table field
      (`mcbsp_poweron_port_int`, VA `0x25608`, writes
      `CM1_ABE_MCBSPx_CLKCTRL[25:24]`).  (Session 46, report 213.)

203. **A McBSP clock *consumer* needs its `clkx`/`fsx` pads input-enabled
      (`0x0100`), not `0x0000`.**  `0x0000` is the peripheral-function mux
      with the pad's input buffer **disabled**; a consumer then sees no clock
      even when the net is driven.  The committed `mcbsp1_pins` group is
      `clkx = 0x0000`, `fsx = 0x0000`, while the reference's live values on
      the same pads are `0x0100`.  E9's pad values were not recorded, so the
      E9 stall is not evidence about the nets until the build is repeated
      with `0x0100`.  (Session 46, report 213.)

204. **The WM8994's AIF1 is on McBSP3 (the PDM pad group, mode 1), not
      McBSP1/2.**  The oracle's routing log is
      `AIF 0 Downlink: MM_DL_PORT->MM_EXT_OUT_PORT->MCBSP3`; McBSP3 is the
      only McBSP whose L4_ABE target is active during playback, `PCR0 =
      0x0003` (consumer), and its pads are `0x106/0x108/0x10a/0x10c` =
      `0x0109/0x0009/0x0109/0x0109` (mode 1).  `abe_mcbsp1_dx` (pad `0x102`)
      is the reference's `AIF 4 → MCBSP1` route, and `mcbsp2_dx` is safe
      mode.  The earlier "DACDAT is `mcbsp1_dx`" reading (E6/trap 192)
      sampled the wrong pads.  (Session 46, report 214.)

205. **The `omap-mcbsp` `hw_params` early-return guard must compare the
      channel layout, not only rate/width.**  The OSS layer first opens a
      stream mono at the final rate/width; the later 2-channel call then
      matched `active_rate`/`active_width` and returned early, leaving
      `XPHASE` clear.  The left slot played and the right was silent (`XCR2 =
      0x0041`; the stereo value is `0x8041`).  Compare
      `!!(cfg_regs.xcr2 & XPHASE)` with the requested channels (I2S/LEFT_J)
      as well.  (Session 46, report 214.)

206. **A user-space read of an idle L4_ABE McBSP register aborts (SIGBUS).**
      `memdump3 49022000`/`49024000` on the oracle dies with `fltno 5` while
      only the active McBSP3 target (`49026000`) reads.  Same class as
      traps 193/158: the target is clock-gated.  Read a McBSP's registers
      only while its driver stream holds the target active; for the codec,
      use the regmap debugfs (`/sys/kernel/debug/regmap/3-001a/registers`),
      which is not affected.  (Session 46, report 214.)

207. **The WM8994 regmap debugfs reads the *cache*, not the silicon, for
      non-volatile registers.**  `wm8994_volatile_register()` lists only a few
      addresses; `0x732` (Interrupt Raw Status 2) is **not** one of them, so a
      read of the FLL1-lock bit returns whatever was cached — including the
      value the repair cached 50 ms after `trigger(START)`, when the FLL may
      genuinely not have locked yet.  The repair's own "FLL1 NOT locked" line
      can therefore be a stale/early read while the audio plays.  A register
      value read this way is the driver's *intended* state, not proof of
      silicon; when a hardware fact matters, read it over I²C (oracle
      `pbi2c rd16`) or judge by ear.  (Session 47.)

208. **Start the clock PCM first, wait, then the data PCM.**  Opening
      `/dev/adsp` (link 2) before `/dev/dsp` (link 1) has established its rate
      fails `SNDCTL_DSP_SPEED` with `EINVAL` (rate symmetry), and a data stream
      whose clock link has already stopped dies with
      `Input/output error (after ~36 000 bytes)` — the old McBSP stall, now
      caused by the missing BCLK, not by the transport.  Sequence:
      `pbplay N 0 /dev/dsp &`, sleep 2–3 s, `pbplay N 440 /dev/adsp &`, then
      confirm both `/proc/asound/card0/pcm{0,1}p/sub0/status` are `RUNNING`.
      (Session 47.)

209. **A `devmem` poke to a GPIO data register does not hold.**  Writing
      `GPIO2_DATAOUT` directly to clear the `spk` bit reads back cleared and
      then reverts as soon as the gpiolib touches another line in the same
      bank (it writes the bank's shadow).  Test a board line only by letting
      the driver own it (`gpiod_set_value`).  A userspace `export` +
      `direction`/`value` does stick, but the line must not be claimed by a
      gpio-hog.  (Session 47.)

210. **The speaker path is the codec's line outputs, `spk` is active LOW, and
      GPIO1_15 is required.**  The WM8994 class-D speaker mixer (`R0x036`) and
      `SPKOUT` are unused by the reference: it plays with `PM3 = 0x3cf0`
      (LINEOUT1P/N + LINEOUT2P/N), `R0x01e = 0`, `R0x034/0x035 = 1`,
      `R0x038 = 0x80`, `R0x04c = 0x1f25`, `R0x060 = 0` and `PM1 = 0x0013`, and
      it drives **`spk` (GPIO2_12) LOW** for the whole speaker playback
      (`enable_codec_line_out`, VA `0x2c814`).  `spk` high is the amp-off /
      teardown state.  GPIO1_15 (the reference's runtime-only output, the
      `audio_boost` enable) must be **high** as well; with it low the speaker
      is silent even when the codec and `spk` match.  The `disp-en-12-hog`
      was a misnomer for this line.  (Session 47, report 215.)

211. **A patch export driven from the previous patch's file list cannot find a
      file no earlier export ever listed — and a byte-compare will not either.**
      Session 48 found that `sound/soc/ti/omap-mcbsp-priv.h` was modified in the
      tree yet absent from *every* export since session 41: the patch added
      `active_rate`/`active_width` **uses** in `omap-mcbsp.c` while the header
      that declares them was never included, so the patch could not compile
      from pristine.  Both of the existing checks passed (0 rejects, 54/54
      byte-identical) because a missing file is simply absent from both sides.
      **Fix:** run the tree-vs-pristine walk (trap 139's recipe) whenever the
      export is regenerated, even when "no new sources" were added — it is the
      only check that sees a long-standing omission.  Read the export's
      *file count* as the signal: 57 files now, and the audit trail is
      `kernel/patches/README.md`.  **Session-50 addendum:** the walk's own
      filter must match `Kconfig.*`, not just the exact name `Kconfig` —
      `b in ('Kconfig','Makefile')` silently drops `arch/arm/Kconfig.debug` (a
      real modified file since session 49), which is the same class again.  Use
      `b == 'Makefile' or b.startswith('Kconfig') or rel.endswith(EXT)`, and run
      `make clean` first or a built tree adds ~34 generated sources.
      (Session 48; addendum session 50.)

212. **A bare `pbplay` is a playback command, and the volume must stay down for
      the whole test sequence.**  `pbplay` with no arguments opens `/dev/dsp`
      and plays a **2 s 440 Hz tone** at the current DAC1 volume — probing its
      usage is a listening test.  Session 48 played three tones the operator
      heard, two of them at full volume, because the volume was restored to 96
      after the first run and then two more captures were taken.  The correct
      order is: **lower `pbmix set "DAC1 Volume" 40` once at the start, run
      every test, restore to 96 once at the very end.**  (Operator, session 48.)

213. **`build_rootfs.py --out <path>` overwrites that path, and the image files
      are the only copy.**  Session 48 passed `--out` the same
      `local/images/rootfs-region1-audio-oss-linout.ext4` that held the
      session-47 known-good image, so `0342ab06…` was destroyed by the rebuild
      (no other local copy existed; region 1 still held it on media until the
      flash).  The saved rootfs image **is** the durable record of a working
      userland, so build to a **new** path (`…-<purpose>.ext4`) and retire or
      replace the old file deliberately.  Save the new image under a name that
      says what it demonstrated, and keep the sha256 in `STATE.md`.
      (Session 48.)

214. **A composite USB gadget's network interface is not the plain link name,
      so a NetworkManager profile pinned to the old name silently stays down.**
      With ACM + RNDIS in one configuration the host's net device is
      `enp8s0f3u1i2`, not `enp8s0f3u1` (the `i2` is the interface index within
      the composite device).  The session-20 `pb-rndis` profile named
      `enp8s0f3u1` and therefore never bound: the link came up, `usb0` on the
      unit came up, and nothing was reachable.  Rebind it
      (`nmcli connection modify pb-rndis connection.interface-name
      enp8s0f3u1i2`) rather than re-adding the address by hand.  Check with
      `ip -br link` on the host that the *net* interface, not the ACM one,
      carries the expected MAC (`02:1a:11:00:00:02` as configured).
      (Session 48.)

215. **Two ssh servers answer on `169.254.0.1:22` depending on which OS is
      booted, and they need different client options.**  The QNX door on
      TabletOS is a 2014 sshd that only speaks `ssh-rsa`/`dh-group1`/`cbc`;
      dropbear on our Linux image negotiates modern algorithms.  Offering the
      legacy options to dropbear does work (verified), but it is needless
      weakening, and a client configured only for the door will appear to
      "fail" if the unit happens to be on our image with a different key.
      Decide by **probing the door's qconn 9090**, not by looking at the
      interface — on TabletOS the door arrives over the same RNDIS link, so the
      interface cannot distinguish them.  `tools/pbssh.sh` does this and takes
      `PB_LINK=usb|door`.  (Session 48.)

216. **An agent's shell has no tty, so `ssh` "interactive" tests are invalid —
      and so is any conclusion drawn from them.**  A session that appears to
      "authenticate and then close with no output" is usually an `ssh` whose
      **stdin is not a terminal** (`/proc/self/fd/0 -> /dev/null`): ssh has no
      local tty to put in raw mode, so it requests no pty and the remote shell
      exits at once.  Measured (session 48): the same command from the
      operator's terminal gives a working prompt with history and completion,
      while the agent's invocation produced nothing; wrapping it in a real pty
      (`script -qec "ssh …" /dev/null`) reproduced the operator's result
      exactly, for both a bare `ssh` and `ssh -tt 'cmd'`.
      **Rule:** before reporting anything about interactive behaviour — ssh, a
      pty, a full-screen program, line editing — check
      `ls -l /proc/self/fd/0` first, and use `script -qec` (or `ssh -tt` plus
      stdin from a pty) to test it.  Do not record a defect until the harness
      is known to be capable of the behaviour.  (Session 48; operator.)

217. **A `TERM`-based prompt or banner test misfires: the console getty sets
      `TERM=vt102` too.**  `/etc/profile` must keep `PS1='pb> '` on
      `/dev/ttyGS0` because `tools/pbcon.py` synchronises on that exact marker,
      and an attempt to distinguish "console vs ssh" by testing `$TERM` handed
      the console a `root@playbook:/#` prompt, which made `pbcon.py` report
      "the shell is still running a command" — the console looked dead while
      it was perfectly alive.  Discriminate on the tty name
      (`$(tty)` == `/dev/ttyGS0`), not on `$TERM`.  Recovering it without
      `pbcon.py` needs `tools/pbraw.py <secs> <hexbytes>`, e.g.
      `pbraw.py 6 "$(printf 'export PS1="pb> "\n' | xxd -p | tr -d '\n')"`.
      (Session 48.)

218. **`mount -t devpts devpts /dev/pts` with no options makes every pty
      unusable.**  The default mount is `mode=600,ptmxmode=000`, so
      `/dev/pts/ptmx` lands as `c---------` and no slave can be opened; a pty
      session then has a terminal it cannot use.  The working form is
      `mount -t devpts -o gid=5,mode=620,ptmxmode=0666 devpts /dev/pts`
      (rcS, session 48).  This was found while chasing what turned out to be a
      harness artefact (trap 216) but it is a genuine defect on its own — it
      would have broken any pty client, including the dropbear session once
      that was actually exercised.  Verified on the device: with the options,
      `/dev/pts/ptmx` is `crw-rw-rw-` and a full
      `fork`+`setsid`+`TIOCSCTTY`+`dup2`+`exec` child works with the parent
      reading the master.  (Session 48.)

219. **The BT half of the WL1283 has its own enable, and it is `gpio 55` =
      `GPIO2_23` (`BT_EN`, pad `0x4A10007C` bits 31:16), not WL_EN.**  The QNX
      driver names it only as hwio signal `0x5D`; the pin is driven low → 50 ms
      → high by **`btp-btmgr`** (`UART_Init` → `UART_BTPower`), not by
      `devc-seromap-winbt`, via devctl `0x80040501`.  The driver's other GPIO is
      `GPIO4_28`, the serial "force RTS" line.  **BT_EN is held LOW while the
      WLAN works** (`GPIO2_DATAOUT` bit 23 = 0), so it is not required for the
      WLAN core — but a Linux `HCI_Reset` can never be answered until it is
      driven: pad `0x07C` bits 31:16 must be mux 3 and the line pulsed.  Our
      DTS only muxes the low half (WL_EN).  (Session 49, report 216.)

220. **The reference loads the WLAN firmware *before* it touches the bus, and
      its NVS comes from NVRAM, not a file.**  `/base/scripts/wifi.sh` is
      `mount -Tio-pkt -o nvram,firmdir=/var/ti1283/ /lib/dll/devnp-ti1283.so`
      after copying `/lib/firmware/ti1283` to `/var/ti1283` and renaming the
      per-unit INI.  Without `-o nvram` the driver demands
      `firmdir/nvs_map.bin` and fails (`dev_attach() failed` → `No such
      device`); **no `nvs_map.bin` exists in the 1917 `/base` or the stock
      RCFS** — with `nvram` it reads 468 bytes from NVRAM instead.  The slog
      order is `LoadFiles` → `ConnectBus`, so a missing firmware file looks
      exactly like "the card does not enumerate" when nothing was ever sent.
      The interface then stays down with MAC `ff:ff:ff:ff:ff:ff` until
      `ifconfig ti0 up`.  (Session 49, report 217.)

221. **`slay -f io-pkt-v6-hc` kills the stack that carries `rndis0`.**  On the
      oracle the WLAN driver must be loaded into (or beside) an io-pkt, and a
      stray second instance is tempting to kill by name — but `slay` matches
      the *executable*, so the main `-ptcpip` stack dies with it and the door,
      qconn and ssh all go with it.  There is then no software path back: the
      OS is up and the USB device still enumerates (`0fca:8012`), but nothing
      answers, and only a power cycle recovers.  Kill strays **by pid**
      (`pidin arg`, then `kill <pid>`); trap 172 restated.  (Session 49.)

222. **io-pkt's `-d` takes its driver options whitespace-separated, and a
      mis-typed `-d` silently looks for the wrong library.**  The usage text is
      `-d driver [driver_options]` with options "separated by commas without
      whitespace"; in practice `-d ti1283 firmdir=/var/ti1283` works while
      `-d ti1283,firmdir=/var/ti1283` makes io-pkt try to load
      `devn-ti1283,firmdir=/var/ti1283.so` (`Library cannot be found`).  A
      full path in `-d` fails the same way.  Also: `-i1` gives a second,
      isolated stack (`mount -Tio-pkt1`) which is the safe place to put a
      driver being experimented on.  (Session 49.)

223. **`SRA` (software reset for all) clears `CEN`/`ICE`, so a reset performed
      after the clock setup leaves the controller with no card clock.**  The
      1917 reference resets MMCHS5 *first* and programs the clock afterwards;
      copying its register program into `omap_hsmmc` at the wrong point gave
      `SYSCTL = 0x00000000` and a probe that timed out with no clock at all.
      Order matters: reset → `CLKD`/`ICE`/`ICS`/`CEN` → `CON`.  (Session 49,
      report 217.)

224. **An MMCHS5 left in the reference's command-phase state wedges `reboot
      -f`.**  On both experiment images (`0xE1B40EB4`, `0x04473A45`) `reboot
      -f` blocked with two `reboot` processes stuck while the kernel kept
      serving ssh, the console shell hung behind the stuck one, and the
      flasher never saw a bootrom.  The clean image reboots normally, so it is
      the controller state.  Recovery: **`echo b > /proc/sysrq-trigger`** —
      `sysrq` jumps straight to the reboot and skips the wedged shutdown path.
      Check `/proc/sys/kernel/sysrq` is `1` first.  (Session 49.)

225. **Reading the wrong register block of a GPIO bank produces a confident
      wrong conclusion.**  `0x48055000 + 0x00..0x3C` is `REVISION` and the
      `IRQSTATUS` set/clear registers; `OE`/`DATAIN`/`DATAOUT` are at `+0x134`/
      `+0x138`/`+0x13C` and `SETDATAOUT`/`CLEARDATAOUT` at `+0x194`/`+0x190`.
      A dump of the low block on the oracle (all zeros) was read as "GPIO2 is
      at reset and `startup-playbook` never ran", which is false — the pads were
      already `0x0118`/`0x0003`/`0x410B`.  Read a bank from `+0x130`.  Related:
      `memw32 <addr> <val> <mask>` wants `val` **already masked** (trap 209's
      tool); passing `410B` with mask `FFFF0000` writes zero.  (Session 49.)

226. **`local/work/minimal/wayback.signed` does not boot this armed unit — it is
      the vendor pair, not ours.**  Its IFS is the stock
      `PlayBook_2.1.0.1917.4.ifs` (`os_type` **4**), and an armed unit boots on
      the factory/CRC path, which requires `os_type == 6` (SAFETY §3, trap 68):
      the unit halts with the red error blink.  Its RCFS is the vendor one too,
      whose `/base/scripts/startup.sh` has **no** `/accounts/devuser/startup.sh`
      hook, so even a boot that succeeds has no door and no root ssh.  **The way
      back to TabletOS is two component flashes**:
      `local/work/ifs-shim-sealed.ifs` at block `0x825` **and**
      `local/work/rcfs-1917.rcfs` at block `0x8D5` — the sealed `os_type` 6 shim
      IFS and the modified RCFS `back-1917.signed` uses.  (Session 49; report
      218 §1.)

227. **The region-0 RCFS slot now holds the full 1917 RCFS, not the pruned
      oracle one.**  Session 49 wrote `local/work/rcfs-1917.rcfs` (500 MiB, runs
      `0x8D5`–`0x2814`) to recover the root hook, so `/base` on media is the
      full base and the minimal oracle's pruned RCFS is gone from it.
      `local/work/minimal/setB.signed` is still the pruned pair (shim IFS at
      `0x825` + pruned RCFS at `0x8D5`) — re-flash that to get the minimal
      oracle back.  (Session 49; report 218 §5.)

228. **A GPIO that is "the right line" can still be undriven: check the pad mux,
      not just the number.**  `0x4A100050` and `0x4A100074` were left at mode 0,
      so the pads `gpmc_ad0`/`gpmc_ad1` and `gpmc_ncs0`/`gpmc_ncs1` were
      actively driven by pad logic on a board with **no GPMC device**, while
      gpiolib could not drive them at all (trap 21).  No earlier GPIO
      comparison found it because those compared `OE`/`DATAOUT` of the lines
      they knew about, never the pads of *unclaimed* lines.  This is trap 185's
      class in its pad form; the five lines are the reference's GPIO1
      0/4/18/28/31, all driven low.  Fixed in the board DTS (mode 3 + pull-down
      + `output-low` hogs).  (Session 49; report 220.)

229. **A pinctrl group cannot re-mux a pad another group owns.**  Two groups
      naming the same pad are refused at probe
      (`pin PIN36 already requested by 48055000.gpio` → `could not request pin
      … from group gpio1-en-pins`), and the whole group is dropped, not just the
      entry.  `0x4A100058`'s low half is `gpio_36` = **GPIO2_4**, which
      `bridge_pins` already claims, so the reference's GPIO1_4-low state cannot
      be reproduced without moving that ownership deliberately.  Read the
      overlaps before writing a group; the failure is at boot, in `dmesg`, not
      at build time.  (Session 49; report 220.)

230. **`pinctrl-single` offsets in `&omap4_pmx_wkup` are relative to
      `0x4A31E040`, and that block is only `0x38` long.**  A pad written as
      `0x058` there is rejected (`mux offset out of range: 0x58 (0x38)`); pad
      `0x4A31E058` is offset **`0x18`**.  `&omap4_pmx_core` is the block where
      the same pad really is `0x058`.  (Session 49; report 220.)

231. **`WL_IRQ` (GPIO2_21) is not a "chip alive" signal — the reference reads 0
      too.**  The oracle's **pre-driver** state is `GPIO2_DATAIN = 0x08000018`
      (bit 21 = 0) and it only becomes `0x08200018` after the driver has powered
      the chip and downloaded firmware.  Every comparison since session 33 used
      "bit 21 = 0" as "the chip is not up", i.e. it compared our *pre-driver*
      state against the reference's *post-firmware* state.  A liveness claim
      needs a decoded card response, not a board GPIO.  (Session 49, report 227.)

232. **`MMCHS_STAT` bit 16 is `CTO`; bit 15 is the `ERRI` umbrella.**  `CTO` is
      "no response within **64 clock cycles**" — 160 us at 400 kHz — so a raw
      `status=0x18000` means "Command Timeout, grouped", not two errors, and it
      cannot distinguish "no card" from "card slower than 160 us".  Decode
      `STAT` in the driver; the errno collapses `CTO` and `DTO` into `-110`.
      The bus state to prove the bus was powered and clocked is `CEN`/`ICE`/
      `CLKD`/`SDBP`/`SDVS`/`CON` read in `start_command()`, **not** the same
      registers after a failed probe (which show the torn-down state).
      (Session 49, report 227.)

233. **`CON = 0x85` must not be copied from the reference: `HR` (bit 2) makes the
      core generate a response instead of sending the command, and it then hangs
      on the command that never completes.**  Setting it before the first
      command stalls `CMD52` with no error at all (no `CTO`, no failure line);
      setting it later merely times out, which is why the session-49 replay of
      report 217 looked like an ordinary negative.  `HR` is an MMC
      interrupt-mode feature; the reference's `CON` only tolerates it because
      its command path never relies on a host response.  Port `CON` with `OD`
      only.  (Session 49, report 227 §3a.)

234. **The BT half is not a die-liveness oracle yet: `BT_EN` driven is
      necessary but not sufficient.**  With `BT_EN` (gpio 55 = `GPIO2_23`, pad
      `0x4A10007C` bits 31:16) really driven low → 50 ms → high — pad
      `0x00030003` (mux 3), `GPIO2_DATAOUT` bit 23 `0`→`1`, verified through
      sysfs — and the UART2 path verified (`uart2_rx` pad `0x4118` has
      `INPUT_EN`, `uart2_tx` `0x0000`, `UART2 LSR = 0x60` = THRE/TEMT, i.e. the
      command left the UART), the chip still answers no `HCI_Reset`
      (`01 03 0c 00`) at 115200 in 2 s, on three pulses.  Session 29's probe is
      therefore **superseded**, not just "invalid" (it never drove `BT_EN`).
      **But silence does not prove the die is unpowered:** the oracle's live
      WLAN bring-up proves the chip runs under the reference (report 217), and
      the reference's *BT* half was only ever decoded statically (report 216).
      Do not use BT silence to exonerate the SDIO controller; settle it by
      running `devc-seromap-winbt` + `btp-btmgr` on the oracle first.
      (Session 50, report 228.)

235. **Lowering `f_min` does not lower the initial clock — it opens the rescan
      ladder.**  `mmc_start_host()` sets
      `host->f_init = max(min(400000, f_max), f_min)`, so the power-up clock
      stays 400 kHz whenever `f_min ≤ 400000`; what `f_min` really controls is
      `mmc_rescan()`'s `{400000, 300000, 200000, 100000}` ladder, which is
      clamped to `max(freq, f_min)` and **stops at `if (freqs[i] <= f_min)
      break;`** — with the shipped `f_min = 400000` every entry clamps to
      400 kHz, so only 400 kHz is ever tried.  Set `f_min` to 100000 to make it
      try all four.  (At 100 kHz with MMC5's 48 MHz `fclk`, `CLKD = 480` and
      `CTO` = 64 card clocks = **640 µs**.)  (Session 50, report 229.)

236. **The WL1283's `SLOW_CLK` is the TWL6030 `CLK32KG` resource at physical
      `0xBC`–`0xBE`, and Linux boots it OFF — which is the whole "dead card".**
      `twl_i2c_read/write_u8(TWL_MODULE_PM_RECEIVER, reg)` adds the
      `PM_RECEIVER` base `0x30`, so `clk-twl`'s module-relative `0x8C` (the
      FreeBSD "`0xBC`" claim) is physical `0xBE` for the state.  Read it with
      `i2ctransfer -f -y 0 w1@0x48 0xbc r3` (the `-f` bypasses the twl driver's
      `EBUSY`).  Boot state `07/01/00` = OFF and `mmc4: Failed to initialize`;
      write `0xBE = 0xE1` (or the reference's `0xBD/0xBE = 0x05/0x55`) and the
      card enumerates (`0x0097:0x4076`, two functions).  The permanent fix is
      `CONFIG_CLK_TWL=y` plus a `clk_prepare_enable` of `clk32kg` in
      `clk-twl`'s probe (it runs at ~1.9 s, before MMC5 at ~3.0 s).  Do not use
      physical `0x8C` (VAUX3) or `0x60` (VCORE3) for this resource.
      (Session 50, report 230.)

237. **wlcore on `omap_hsmmc` needs three separate things beyond the kernel
      modules.**  (a) The SDIO function 2 needs a DT child
      (`compatible = "ti,wl1283"`, `reg = <2>`, `interrupts` = WL_IRQ gpio 53 /
      GPIO2_21, edge-falling); without an `of_node` `wlcore_probe_of()` returns
      `-ENODEV` **silently**.  (b) The host must set `cap-power-off-card` or
      `mmc_attach_sdio()` does not enable card runtime PM and
      `pm_runtime_resume_and_get(&card->dev)` returns `-EACCES`
      (`wl12xx_sdio_power_on: failed to get_sync(-13)`).  (c) Every firmware
      boot needs a cold power cycle: `wl12xx_sdio_power_on()` asks for one via
      `mmc_hw_reset()`, which is **`-EOPNOTSUPP` on `omap_hsmmc`** (no
      `card_hw_reset`) and is ignored, and a permanent WL_EN gpio-hog means the
      chip is never cycled — so the first boot works and later ones time out
      (`sdio write failed (-110)`).  Use an `mmc-pwrseq-simple` that owns WL_EN
      (gpio 54 = GPIO2_22) with `reset-gpios = <&gpio2 22 GPIO_ACTIVE_LOW>`,
      `reset-delay-us = <50000>`, `post-power-on-delay-ms = <120>` — the
      reference's own low → 50 ms → high → 120 ms.  Also load the module stack
      in order (`cfg80211 mac80211 wlcore wl12xx wlcore_sdio`): the recorded
      dependency of `wlcore_sdio` is incomplete.  (Session 50, report 231.)

238. **Do not use `IRQ_TYPE_LEVEL_LOW` for `WL_IRQ` (GPIO2_21): it storms at
      boot and wedges the unit.**  The line idles **LOW** before firmware
      (`GPIO2_DATAIN` bit 21 = 0), so a level-low IRQ fires continuously from
      `request_irq()` onward; the threaded handler's SDIO status read cannot
      complete and the CPU is saturated — ssh and the ACM console both stop.
      `IRQ_TYPE_EDGE_FALLING` is the type the reference's hwio detect uses and
      the only one that boots cleanly.  Recovery: a power-cycle (operator) puts
      the unit in the bootrom (`0fca:0001`); flash the known-good region-0 image
      from there.  (Session 50, report 231 §6.)

239. **The WL1283's `-110` recovery loop is the chip's ELP sleep: keep it in
      CAM.**  The failing transfer is the **FW-status read** — CMD53 to
      **`0x1efc0`, 84 bytes, byte mode (`fixed = 0`), 256-byte block** — issued
      from the WL_IRQ thread (`irq/132-wl12xx`) right after `firmware booted`.
      It fails only once the chip has entered ELP; with
      `sta_sleep_auth = WL1271_PSM_CAM` the interface comes up and stays up.
      The fix is `wl12xx_default_conf.conn.sta_sleep_auth =
      WL1271_PSM_CAM` (was `WL1271_PSM_ILLEGAL` → ELP).  Measured causally:
      `echo 2 > /sys/kernel/debug/ieee80211/phyN/wlcore/sleep_auth` on a
      working system reproduces the loop in seconds, `echo 0` restores it.
      **`wlcore_sdio dump=1` hides the failure** (per-transfer printk on fbcon
      changes the timing, trap 52) — get the address from the error path
      instead (a temporary `dev_err` with `addr`/`len`/`fixed`/`cur_blksize`).
      `wl128x-fw-4-sr.bin` was rejected by wlcore's floor (`7.3.10.*.133`;
      4-sr is `7.3.5.0.98`) before that floor was lowered — and with the floor
      lowered, 4-sr is the firmware that works (trap 243).  (Session 51, report 232.)

240. **A `wlcore` scan that "completes due to error" after ~30 s is the
      driver's own `WL1271_SCAN_TIMEOUT`, not a hang and not a transport
      fault.**  `wlcore_scan()` sets `wl->scan.failed = true` *by design* and
      only the firmware's scan-complete event clears it; if no event arrives,
      `wl1271_scan_complete_work()` logs `wlcore: Scan completed due to error.`
      at exactly 30 s (`WL1271_SCAN_TIMEOUT`) and queues an unintended
      recovery.  Count the `wl12xx` IRQs during the window before blaming the
      bus: ~13 means the firmware never reported the scan.  A passive scan, a
      single-channel scan and `power_save off` all behave the same when the
      firmware is the problem.  `wlcore`'s fwlog cannot be enabled from sysfs —
      `fw_logger_write()` refuses while `conf.fwlog.output == 0` — so the conf
      default must be changed in the driver or supplied via `wlconf`.  The
      per-unit NVS does not change this symptom (spliced and tested).  (Session
      51, report 233.)

241. **Do not enable `wlcore`'s host fwlog on wl12xx: the panic-log reader is
      wl18xx-shaped and oopses.**  `modprobe wlcore fwlog=continuous` sets
      `conf.fwlog.output = HOST` (the compiled default is `DBG_PINS`), and the
      next recovery then runs `wlcore_event_fw_logger()`, which reads the FW
      log block and parses it with the **wl18xx** field layout
      (`WL18XX_LOGGER_BUFF_OFFSET`, `max_buff_size`, …).  On this WL1283 the
      log address translates **out of range** — `WARN` at `io.c:84`,
      `HW address 0x40159c out of range` — the read returns garbage, the
      "actual" length is nonsense, and `wl12xx_copy_fwlog()` memcpys from an
      unmapped vmalloc address: **`Unable to handle kernel paging request …
      Internal error: Oops: 5` at `mmiocpy`**, taint `0x1280`
      (`DIE|WARN|OOT`).  The worker dies holding state, so afterwards
      **`ip link show` blocks** (ssh still answers) and the only recovery is a
      reboot (`echo b > /proc/sysrq-trigger`).  `module_param_named(fwlog, …,
      0)` creates no sysfs file, so the setting can only be passed at load.
      **The one useful datum:** the recovery asks for the panic log because
      `WL1271_FLAG_INTENDED_FW_RECOVERY` is clear, i.e. the **firmware itself
      panics** on the failing scan.  Do not repeat this without first fixing
      the parse (the FW log needs the wl12xx layout, not wl18xx's).  (Session
      51, report 233.)

242. **On a stock TabletOS boot the door's `sshd` may not come up — `qconn`
      still does, so drive the unit with `tools/QCL.py`.**  After flashing the
      shim IFS and letting a cold TabletOS boot, `169.254.0.1:22` was **refused**
      while **9090 answered** (`QCL.py info` reports the target:
      `OMAP4430_ES2.2_HS_Winchester_Rev:07`, `RELEASE=6.6.0`).  A stale
      `door-hold` from before the reboot does **not** recover (trap 17) — kill it
      by pid and start a fresh one.  `QCL.py exec /bin/<cmd> <args…>` runs single
      commands; `exec /bin/sh -c '…'` does **not** survive its argument handling
      (it silently runs the wrong thing), except where the whole pipeline is one
      argument (`exec /bin/sh -c 'sloginfo' | grep …` on the host side works).
      And rebind the host's `pb-rndis` profile to the **plain** `enp8s0f3u1` for
      the oracle (trap 214) — with it still on `enp8s0f3u1i2` the unit is
      unreachable even though the link is up.  (Session 51, report 234.)

243. **The WL1283 must run `wl128x-fw-4-sr.bin`, not `5-sr`: the newer service
      pack panics on the first scan.**  `5-sr` (`Rev 7.3.10.0.142`) boots and
      keeps `wlan0` up, but every scan dies inside the firmware (no
      scan-complete event; the driver's 30 s timeout, then recovery — reports
      233/241), while `4-sr` (`Rev 7.3.5.0.98`) scans normally on the same
      driver, NVS and board (**17–21 BSS per scan, both bands, no errors**).
      The port therefore lowers `wl12xx`'s SR floor to **7.3.5.0**
      (`WL128X_MAJOR_SR_VER 5`, `WL128X_MINOR_SR_VER 0`) and sets
      `WL128X_FW_NAME_SINGLE` to the `4-sr` file; both files stay installed so
      switching back is one `#define`.  `5-sr` was also the *newest* wl128x blob
      in linux-firmware, so "use the latest firmware" is the wrong default here.
      (Session 51, report 235.)

244. **A WPA2 association that reports `WRONG_KEY` is usually not the key: the
      `ccm`/`ctr` crypto modules were missing, so the PTK install fails with
      `-ENOENT`.**  The 4-way handshake itself completed — message 3 decrypted
      with AES-UNWRAP, so the PSK was right — and then `nl80211: kernel reports:
      key addition failed` / `set_key failed; err=-2` appeared, after which
      wpa_supplicant logs `reason=WRONG_KEY`.  The `-ENOENT` comes from
      `ieee80211_key_alloc()`: for CCMP it calls
      `crypto_alloc_aead("ccm(aes)")`, which returns `-ENOENT` when the
      algorithm is not registered.  `CONFIG_CRYPTO_CCM=m` and
      `CONFIG_CRYPTO_CTR=m` were set but **neither module was in the region-1
      rootfs**, and `mac80211.ko` does not link against them (`depends:
      cfg80211,libarc4`), so `depmod` never pulls them in.  Fix: ship
      `ccm.ko`/`ctr.ko` and `modprobe ccm ctr` **before** the wireless stack in
      `rcS`.  Note the trap inside the trap: the `-ENOENT` at
      `net/mac80211/cfg.c` `ieee80211_add_key()`'s `WLAN_STA_ASSOC` check looks
      like the culprit and is not — a probe showed `sta=yes state=3 assoc=1`.
      (Session 52, report 236 §1–§3.)

245. **A 5 GHz AP outside the world regulatory domain produces an auth timeout,
      not a driver error.**  The stock regdomain (`country 00`) marks
      5735–5835 MHz `PASSIVE-SCAN` (no IR), so cfg80211 refuses to transmit:
      the log shows `wlan0: regulatory prevented using AP config, downgraded`
      and then `authentication with … timed out` (the same signature as
      `WL_IRQ`/firmware trouble — check the regulatory line first).  `iw reg
      set <CC>` with the operator's country clears it.  This is configuration,
      not a bug: a deployment that only sets the world domain will not
      associate on those channels.  (Session 52, report 236 §4.)

246. **MUSB's HSDMA silently falls back to PIO for buffers that are not 4-byte
      aligned — the USB gadget ran at 3.3 MB/s because of it.**  The tell is
      the interrupt counters: ~1 `mc` interrupt per 512-byte packet and the
      `dma` IRQ essentially never firing (`6` total).  `MUSB_HWVERS` reads
      `0x0800` (RTL 2.00), and for RTL >= 1.80 `dma_channel_program()` does
      `if (dma_addr % 4) return false;` with no message.  u_ether hands it
      `skb->data`: RX is 2 mod 4 because `rx_submit()` reserves
      `NET_IP_ALIGN` (2), and TX was measured 2 mod 4 as well.  Fixes (both in
      the gadget path): `musb->g.quirk_avoids_skb_reserve = 1` in
      `musb_gadget_probe()` (RX stays at `NET_SKB_PAD` = 32) and a realign copy
      for misaligned TX frames in `gether_start_xmit()`.  Measured: RX
      3.3 -> **6.3 MB/s**, TX 3.3 -> **4.1 MB/s**.  Do not conclude "USB is
      slow" from an RNDIS figure without checking the DMA IRQ.  (Session 52,
      report 236 §7.)

247. **Do not advertise 40 MHz on wl12xx: the firmware accepts the association
      and then collapses.**  The AP offers it (`HT operation: primary 157,
      secondary above, width any`) and adding
      `IEEE80211_HT_CAP_SUP_WIDTH_20_40` does associate, but the rate falls to
      MCS1 then MCS0 and **RX drops from 4.4 MB/s to 622 KB/s** (TX unchanged
      at 2.9).  `wl12xx_ht_cap` (`GRN_FLD | SGI_20 | RX_STBC`,
      `rx_highest = 72`, A-MPDU 8K) is correct: the link is 1x1 HT20 and that
      is the ceiling.  Reverted; the rebuilt `wl12xx.ko` is byte-identical to
      the baseline (`2fb86c4d…`).  Also do not trust `wpa_cli status`'s
      `channel_width` — it reports the AP's capability.  (Session 52, report
      236 §8.)

248. **The WL1283's BT core is silent until the UART2 **CTS/RTS** pads are
      muxed — it is not `BT_EN`, the clock or RX/TX.**  `0x4A100118` holds both
      halves (`UART2_CTS[15:0]`, `UART2_RTS[31:16]`); left at the bootloader's
      safe mode it reads `0x011F011F` (mux 7) and the chip answers nothing.  The
      reference's idle value is **`0x00000118`** (CTS = `0x0118`, RTS = mux 0) —
      `devc-seromap-winbt` is the only place it differs, and it even has a
      "force-RTS" devctl that muxes RTS to GPIO4_28.  With the pads muxed,
      `HCI_Reset` is answered immediately (`04 0e 04 01 03 0c 00`).  The DTS
      `uart2_pins` now writes `0x118`/`0x11A` (pinctrl-single is 16-bit wide, so
      a pair is two halfword entries).  **Also:** `HCIDEVUP` returns **`ENOSYS`**
      on 7.3.3, so the controller can only be powered through the **mgmt**
      channel (`HCI_CHANNEL_CONTROL`, `MGMT_OP_SET_POWERED`) — an inquiry on a
      raw socket otherwise fails with `Network is down`.  (Session 52, report
      237.)

249. **`0x201c` is `HCI_LE_Read_Supported_States`, and on a bare ROM it stops
      `hci0` from ever *registering* — it is not a benign TI vendor op.**  Report
      237 §4 called it "a TI vendor op that `hci_uart` sends generically" and
      dismissed the `-38`; that is wrong on both counts.  `0x201c` is OGF 0x08
      (LE Controller Commands), OCF 0x01C.  A bare WL1283 ROM answers it with
      **Command Disallowed (0x0c)** and only **5 payload bytes** where
      `struct hci_rp_le_read_supported_states` needs 9, so `hci_cc_func()`
      (`hci_event.c`) logs `unexpected cc 0x201c length: 5 < 9`, returns
      `HCI_ERROR_UNSPECIFIED`, and `bt_to_errno()` maps that to `-ENOSYS` (the
      `failed: -38`).  `hci_le_read_supported_states_sync()` is the **last** step
      of `le_init2`, and `hci_init_stage_sync()` aborts the stage on any non-zero
      return, so `hci_dev_init_sync()` fails: `hci0` is created but never opened,
      and **mgmt reports `num_controllers = 0`**, which makes
      `MGMT_OP_SET_POWERED` structurally impossible.  Two consequences:
      (a) **loading the BT service pack (`WL1283.bts`) before the attach fixes
      the length** (the reply becomes a full 12-byte event) and the controller
      then registers with mgmt — measured as the
      `READ_INDEX_LIST` difference 0 → 1 controller; (b) presence of
      `/sys/class/bluetooth/hci0` is **not** liveness — the node exists from
      `HCIUARTSETPROTO` alone, and mgmt registration is the real milestone.
      The service pack also survives `HCI_Reset` (patch RAM is not cleared) but
      **not** a `BT_EN` power cycle.  (Session 53, report 238.)

250. **`-84` from the H4 line discipline is `EILSEQ`, and on this board it is a
      lone `0x30` byte arriving after init.**  `h4_recv_buf()`
      (`drivers/bluetooth/hci_h4.c`) returns `EILSEQ` when the first byte of a
      receive chunk is not an H4 packet type (0x01–0x04); `h4_recv()` logs
      `Frame reassembly failed (-84)`.  An instrument in `h4_recv()` (the error
      path does not print the data — the report-232 lesson again) shows the same
      shape in **6 of 6** runs: the last init reply arrives complete, then about
      200 ms later a **single `0x30`** arrives on its own and is (correctly)
      rejected.  **Not yet known which side emits it.**  It never appears when
      the same sequence is driven from userspace, so this is not "the chip emits
      junk after init".  Excluded by measurement: alignment/padding
      (`hu->alignment = 1`, `hu->padding = 0`), chip idle-sleep between commands,
      leftover unread bytes, UART line errors (`LSR = 0x60`), truncated long
      replies, and **hardware flow control** — an A/B on a clean boot gives
      `num_controllers = 1` with `CRTSCTS` on *and* off.  **Two retractions to
      heed:** `hci_uart_set_flow_control()` is called only by the *vendor*
      protocols (`bcm`/`qca`/`intel`/`mrvl`) and **never by H4** (`hci_uart_setup`
      is a no-op for H4), so a flow-control cause was a wrong guess; and the
      `Read_Local_Name` reply is **not** corrupt — it arrives complete at 255
      bytes, and the "corruption" first seen was the probe's own 240-byte display
      buffer.  (Session 53, report 238 §4.)

251. **`insmod` reports success while keeping the old module, when something
      still references it.**  Swapping `hci_uart.ko` for an instrumented build
      appeared to work (`insmod` printed nothing, `/proc/modules` showed
      `hci_uart` loaded) but the old code kept running, so the instrument never
      fired and the experiment looked like a negative.  `btbcm` holds a reference
      to `hci_uart`, so `rmmod hci_uart` fails with `Resource temporarily
      unavailable` while `insmod` cheerfully "succeeds".  Order matters:
      **`rmmod btbcm`, then `hci_uart`, then `insmod`** — and verify the swap by
      content (`strings /path/to/module | grep <marker>`), never by exit status.
      The same trap makes a reverted experiment look un-reverted; after reverting
      `hci_h4.c`, confirm the rebuilt module has no marker rather than trusting
      the build.  A reboot is the sure reset (`/tmp` is wiped, so re-push the
      tools).  (Session 53, report 238 §4.)

252. **After a cold boot `/sys/class/gpio/gpio535` does not exist and `BT_EN`
      reads low, so the BT service pack fails with "no HCI_Reset answer".**
      `pulse_bt_en()` in `pbhci` resolves the GPIO2 sysfs base and exports the
      line itself, but a hand-written script that just writes to
      `/sys/class/gpio/gpio535/value` silently does nothing until the line is
      exported.  Export it first, set `direction` to `out`, then pulse low → 2 s
      → high.  A **2 s** low time was reliably sufficient where 50 ms sometimes
      was not, after the core had been left asleep by a previous run.  This
      wastes real time because the symptom is identical to a dead chip.
      (Session 53.)

253. **`pmbootstrap install` runs `mkinitfs` even for a device whose kernel it
       does not own, and `mkinitfs` requires exactly one kernel release.**  A
       scaffolded device port with `flash_method="none"` and no kernel
       dependency installs `postmarketos-base` and the device package, then
       `create_device_rootfs()` calls `pmb.chroot.initfs.build()`; `mkinitfs`'s
       `GetKernelVersion()` globs `/usr/share/kernel/*/kernel.release` and
       fails with `only one kernel release/flavor is supported, found: []`.
       The rootfs itself is kernel-agnostic; the install pipeline is not.  A
       port whose kernel is external must still install a `linux-*` package
       that provides that file (and the matching modules) for the install to
       finish.  (Session 54, report 240.)

254. **A prebuilt `linux-*` package's module metadata can vanish after the
       package install, and `mkinitfs` then fails with `kernel module.dep not
       found: /usr/lib/modules/<ver>`.**  The package shipped `modules.dep`
       and friends, but after the `kmod` trigger ran, only `kernel/*.ko`
       remained; running `depmod -a 7.3.3` inside the chroot regenerated them
       and `mkinitfs` completed.  Ship a `post-install` that runs `depmod -a
       <release>` and include `modules.order`/`modules.builtin`; do not rely
       on the trigger for a prebuilt module tree.  (Session 54.)

255. **pmbootstrap's split install writes a `/boot` UUID into `/etc/fstab`,
       and `tinydm` needs `localmount`.**  With this chainloader there is no
       separate boot partition, so `localmount`'s `mount -at …` fails on the
       absent `/boot` device; because `tinydm` has `need localmount`, the
       display session never starts.  Remove the `/boot` line from the
       flash-ready image.  The root UUID line can stay: `/` is already mounted
       by the initramfs and its fstab `pass` is 0.  (Session 54, report 240.)

256. **Nura's `inittab` starts gettys on tty1–6 only; the PlayBook bench
       console is `/dev/ttyGS0`.**  After the hand-off there is no shell on the
       USB ACM console unless the image's `/etc/inittab` carries
       `ttyGS0::respawn:/bin/sh -l` (the current BusyBox rootfs does the
       same).  Add it to the flash-ready image; otherwise the only access is
       the panel/touch, and the rootfs was built with `--no-sshd`.  (Session
       54.)

257. **`pmbootstrap install` reuses a cached device package; editing its
       `depends` does not rebuild it.**  The new dependency is then never
       installed, and the failure looks like the dependency was ignored (here:
       `mkinitfs` still found no kernel).  Bump `pkgrel` or run
       `pmbootstrap build --force <pkg>` before installing.  (Session 54.)

258. **`ui=none` selects systemd because `postmarketos-ui-none` does not
       exist, and `--no-firewall` then fails in a never-booted chroot.**  The
       install reaches `systemctl preset nftables.service` and exits 1.  For
       this device use a real UI (`catacomb`, which defaults to OpenRC) when
       testing the rootfs; the `ui=none` run is only useful to observe the
       kernel/initfs gate.  (Session 54.)

259. **The hd3 filesystem starts at loader block `0x4915`, not at the MCT
       entry `0x4905`.**  The MCT type-`0x1C` entry begins at `0x4905` but
       carries a 1 MiB prefix; the QNX6/ext4 superblock is at raw
       `0x49000000`, which is `(0x4915-0x15)*0x10000`.  A flash at `0x4905`
       passes the host dry-run (it only checks the MCT extent) but the
       loader's own gate rejects it at the header (`0xF7 seq 4 -> cmd 0x15
       data 03`, nothing written); `0x4915` writes.  Always dry-run and check
       the printed **raw start** is `0x49000000`, then flash at `0x4915`.
       (Session 54, report 241 §1.)

260. **`access("/rootfs/sbin/init")` follows an absolute rootfs symlink into
       the initramfs.**  Nura's `/sbin/init` is `/bin/busybox` (absolute); the
       ext4 mounts, but `access()` resolves it against the *initramfs* root,
       where `/bin/busybox` does not exist, so the pre-chroot check fails and
       the console fallback runs.  Use `lstat()` (does not follow the final
       symlink).  Symptom: `EXT4-fs (loop0): mounted …` then `PB: rootfs: no
       /sbin/init, keeping the initramfs console`.  (Session 54, report 241
       §2.)

261. **The first rewrite of the bootrom-written hd3 NAND stalls the eMMC.**  A
       597 MiB sequential bootrom image is fine to write and read, but the
       first kernel ext4 journal/udev rewrite gives `mmc1: Card stuck being
       busy! __mmc_poll_for_busy`, the loop write fails, `Aborting journal on
       device loop0-8`, and ext4 remounts `emergency_ro`.  A 4 KiB test write
       afterwards returns `I/O error` (reads still work).  The region-1 root
       has **no journal** and is stable; the read-only hd3 + overlayfs
       (`CONFIG_OVERLAY_FS=y`) avoids the writes.  The **`PB*` prints are not
       the trigger** — the stall precedes the rescan/prints.  (Session 54,
       report 241 §3.)

262. **The Nura DE needs a writable/session-capable `/var`; a bind-tmpfs over
       it is not enough.**  `tinydm` starts after the tmpfs `/var`/`/home`/`/tmp`
       + the `/var/run -> /run` symlinks, but `/run/user/10000` never appears
       and `catacomb` fails `Unable to start libseat session: Ensure
       logind/seatd service is running with no active session`; the tmpfs hides
       the lower `/var/lib/elogind`.  Use overlayfs (lower visible) and check
       the `tty1` getty/PAM; a `seatd` fallback is an option.  (Session 54,
       report 241 §4.)

263. **The pmbootstrap Nura rootfs lacks the WLAN firmware.**  `wlcore` fails
       `Direct firmware load for ti-connectivity/wl128x-fw-4-sr.bin failed
       with error -2` (and `wl128x-nvs.bin`); the root was not given
       `local/work/firmware-nvs`.  Add `ti-connectivity/*` and
       `regulatory.db*` to `/lib/firmware`.  This is a packaging omission, not
       a driver/radio failure.  (Session 54, report 241 §5.)

264. **The `PB*` bring-up instrumentation is now `pr_debug` (fix, session 54).**
       `PBSIO`/`PBINI`/`PBX`/`PBM` (`omap_hsmmc.c`), `PBC`
       (`mmc/core/{core,host,mmc_ops}.c`), `PBR` (`drivers/base/dd.c`), `PBG`
       (`gpio-omap.c`), `PBS` (`ti-sysc.c`) and `PB:` (`arch/arm/mm/mmu.c`)
       were unconditional `pr_info` on every MMC/probe operation — per-I/O
       console logging on the MMC path (trap 52) and a log flood.  They are
       now `pr_debug`, enabled with dynamic debug; `PBREQ`/`PBIRQ` remain
       behind the `pb_trace` module parameter.  Do not re-add per-I/O
       `pr_info`.  (Session 54, report 241 §6.)

265. **Enable `CONFIG_DYNAMIC_DEBUG`/`CONFIG_OVERLAY_FS`, and keep the hd3
       image's modules matched to the kernel.**  The `0xE84693A1` build has
       both; the hd3 image still carries the older pmbootstrap artifact
       modules.  Update `/lib/modules/7.3.3` from `local/work/modules-7.3.3`
       (same vermagic) or bump the kernel package before flashing a new
       region 0.  (Session 54.)

266. **`--from-bootrom` does not mean the unit is in the ramloader; it uploads
       the ramloader and needs the unit booted.**  The flasher must be started
       first, then the running OS rebooted (`reboot -f`; the Nura `ttyGS0`
       shell via `pbcon.py`), which opens the 60 s bootrom window.  "Flash both
       halves in one bootrom session" means one packed container, not two
       `flash` runs.  (Session 54, report 242 §5.)

267. **The `bbusb.py pack` component coordinate is the loader block minus
       `0x815`.**  Region 0 `0x825` → `16`; region 1 `0x2915` → `8448`; hd3
       `0x4915` → `16640`.  `parse` shows the V1 runs; the signature CRC must
       be `OK`.  (Session 54, report 242 §4.)

268. **The Nura `ttyGS0` shell is a plain `/bin/sh -l`, not the old
       `pbcon`/`/console`.**  `tools/pbcon.py 'reboot -f'` works when the shell
       is at a prompt; "no prompt — the shell is still running a command"
       means send Ctrl-C with `tools/pbraw.py 2 03` (or wait).  `reboot -f`
       skips the slow BusyBox shutdown.  (Session 54.)

269. **The read-only overlay root is `MS_RDONLY`+`noload`,
       `LO_FLAGS_READ_ONLY`, an `O_RDONLY` backing, a tmpfs upper/work, and
       `/newroot`.**  The `noload` tolerates the dirty media journal (no replay
       write); the binds and the `lstat` hand-off go on the overlay, not the
       lower.  `CONFIG_OVERLAY_FS=y` is in the `0xE85217E5`.  (Session 54,
       report 242 §1.)

270. **The new 43 modules and the `0xE85217E5` kernel have the same vermagic**
       (`7.3.3 mod_unload modversions ARMv6 p2v8`); the image's modules were
       replaced, the `modules.dep` unchanged.  Keep them matched when changing
       the config.  (Session 54, report 242 §3.)

271. **Before the overlay boot, the hd3 image needs the root `/etc/fstab` line
        removed, the `tty1` getty removed (the `tinydm` VT), and the
        `ti-connectivity` firmware added.**  `e2fsck -fn` must exit 0.  (Session
        54, report 242 §3.)

272. **The overlay root is confirmed on the unit: no eMMC stall, a real seat,
        and `catacomb` running.**  The flashed container (`0xE85217E5` + the
        hd3 `1f14915f…`) boots `7.3.3 #67` with
        `overlay / … lowerdir=/rootfs,upperdir=/overlay/upper,workdir=/overlay/work`,
        the lower `EXT4-fs (loop0): … ro without journal`, no `Card stuck`/no
        journal abort.  `elogind`/`tinydm` start, `/run/user/10000` and
        `catacomb-wayland-1.sock` exist, the touch is ready (irq 110), the
        backlight is 255, and `wlcore: firmware booted (Rev 7.3.5.0.98)`.
        (Session 55, report 243.)

273. **A black screen with only catacomb's empty bar is the GL renderer, not
        the root.**  `/home/playbook/.local/state/tinydm.log` shows
        `MESA: error: ZINK: failed to load libvulkan.so.1` (Mesa carries only
        `swrast`/`kms_swrast`/`zink`; there is no `libvulkan`), plus
        `smithay … failed to create signaled syncobj … ENOTSUP`.  The compositor
        does get the `1024x600` output.  Do not "fix" this by re-flashing the
        root: the next step is SGX540 (or a software-render fallback).  (Session
        55, report 243 §3.)

274. **The Nura `ttyGS0` shell prompt is `playbook:~#`, not `pb> `**, so
        `tools/pbcon.py` (hardcoded to the old `pb> ` marker) reports "no
        prompt" and sends nothing.  Drive that shell with a raw helper that
        writes the line and reads for a fixed time; `reboot -f` sent that way
        opens the bootrom window.  (Session 55; trap 268 is about the shell,
        this is the tool mismatch.)

275. **The SGX driver is the active `openpvrsgx-devgroup/linux_openpvrsgx`
        `pvrsrvkm-7.3` branch, not the `tmlind` 6.1 fork.**  HEAD
        `101c687a…` carries DDK 1.17.4948957 and already selects
        `SOC=omap4 SGX=sgx540 SGX_REV=120`.  It needs one kernel export,
        `EXPORT_SYMBOL(__vmalloc_node_range_noprof);` in `mm/vmalloc.c` (the
        devgroup branch has it); without it modpost fails for every variant.
        (Session 55, report 244.)

276. **The pvrsgx Makefile builds nine SoC variants.**  Only
        `pvrsrvkm_omap4_sgx540_120.ko` is ours; strip it with `--strip-debug`
        (4.0 MB → 347 KB) and write it flat into
        `/lib/modules/7.3.3/kernel/`.  `insmod <full path>` works without a
        depmod index (the module has no external module deps).  (Session 55,
        report 244.)

277. **The SGX DDK userspace is glibc/armhf; the Nura root is musl 1.2.6.**
        `libsrv_um`/`libusc`/`libsrv_init` `NEEDED libc.so.6` and use
        `/lib/ld-linux-armhf.so.3`, so `gcompat` is required, and the Mesa that
        loads `pvr_dri.so` is the patched `mesa-pvr-dri-classic` 21.3.9, not the
        distribution Mesa.  `MESA_LOADER_DRIVER_OVERRIDE=pvr` selects it.
        (Session 55, report 244 §4.)

278. **`/init`'s `BOOTCTR_FCTRY` top-up fires only below `0x1e`.**  A boot
        that reads `0x31` correctly writes nothing (`0x31 >= 0x1e`); do not
        assume the counter is `0x32` — read the `dmesg | grep pbrpmb` line.
        (Session 55; `SAFETY.md` §2.)

279. **The vendor SGX `pvrsrvctl`/DDK blobs do not run under a hand-installed
        `gcompat` 1.1.0 on this musl root.**  `gcompat` installs
        `/lib/ld-linux-armhf.so.3` + `libc.so.6`, but `pvrsrvctl` segfaults even
        on `--help` (`rc=139`), before any service init — i.e. it is the
        loader/ABI path, not the SGX driver.  Do not conclude the kernel or the
        blobs are wrong: build the stack the pvrports way in the Alpine/
        pmbootstrap chroot.  (Session 55.)

280. **The pvrports prebuilt stack (`v23.12/armv7`) installs on this edge
        root and replaces mainline Mesa.**  `apk` purges `mesa` 26.2.6,
        `mesa-{egl,gl,gles,dri-gallium,gbm}`, `llvm23-libs` and `spirv-tools`
        and installs `libglvnd`, `mesa-pvr-dri-classic` and
        `sgx-ddk-um{,-ti443x,-openrc}`.  It also needs `gcompat` (+
        `musl-obstack`, `libucontext`) and **`wayland-libs-server`** (the
        amber EGL needs `libwayland-server.so.0`; the root had only client).
        The packages also build from source in the edge chroot.  (Session 55,
        report 245.)

281. **The SGX540 is usable headless: EGL/GLES 2.0 on the PowerVR SGX 540.**
        `pvrsrvinit` logs `PVR_K: UM DDK-(4948957) and KM DDK-(4948957) match.
        [ OK ]`; a surfaceless EGL pbuffer context (`EGL_PLATFORM_SURFACELESS_MESA
        = 0x31DD`, `MESA_LOADER_DRIVER_OVERRIDE=pvr`) reports
        `GL_RENDERER = PowerVR SGX 540`, `GL_VERSION = OpenGL ES 2.0 build
        1.17@4948957`.  `/tmp/egltest.py` is the ctypes probe; `es2_info` is
        useless headless (it insists on X).  (Session 55, report 245.)

282. **catacomb (smithay) cannot use this GLES2 driver: `GLExtensionNotSupported
        (["GL_EXT_unpack_subimage"])` at renderer creation (`src/udev.rs:400`).**
        The GPU is fine; the compositor requirement is the blocker.  pvrports
        supports Phosh (patched phoc) and Plasma, not catacomb.  Choose a
        wlroots compositor or extend catacomb; do not read the panic as a GPU
        failure.  (Session 55, report 245 §3.)

283. **The pvr userspace was baked into the hd3 image offline with the x86_64
        static `apk`.**  Loop-mount the image rw (as root; `pkexec` works), then
        `apk --root /mnt/... --arch armv7 --allow-untrusted --no-network
        --no-scripts --repositories-file /dev/null --repository <dir with
        armv7/APKINDEX.tar.gz> add <pkgs>`.  The repo path must sit above an
        `armv7/` subdirectory; `apk index --allow-untrusted --rewrite-arch
        armv7` builds it.  Then `depmod -b <mnt> 7.3.3`, append the module to
        `/etc/modules`, link the service in `/etc/runlevels/default`, and
        `e2fsck -fn`.  The image is `85b44deb…`; only hd3 was flashed
        (`--component --dest-block 0x4915`).  (Session 55, report 245 §6.)

284. **After the SGX bake, a black panel with the backlight on is expected.**
        The pvr stack replaces mainline Mesa, and catacomb (the only installed
        compositor) panics on `GL_EXT_unpack_subimage`; the GPU and the display
        are healthy (`card1`, `renderD129`, `PVR_K … match. [ OK ]`), but no
        session runs.  Install a compatible compositor before judging the
        screen.  Do not `dd` `mmcblk1` for a checksum — it is an unnecessary
        eMMC read on the live overlay (session 55; the read caused a visible
        display flicker and proves nothing).  (Session 55.)

285. **smithay hard-requires `GL_EXT_unpack_subimage`; the SGX540 is GLES 2.0
      and has neither that nor GLES 3.0.**  Catacomb 1.5.0 pins smithay 0.7.0
      (`dba255cd…`); `GlesRenderer::new()` returns
      `GLExtensionNotSupported(["GL_EXT_unpack_subimage"])` and catacomb panics
      at `src/udev.rs:400`.  The fix is a smithay patch: remove the hard error
      and implement the upload fallbacks (full tight → one `TexImage2D`; full
      padded → NULL alloc + per-row; partial tight → whole damaged rows; partial
      padded → per-row; `update_memory` → full-texture-layout rows).  Do not
      read the panic as a GPU failure; the EGL/GLES probe works.  (Session 56,
      report 246.)

286. **catacomb/smithay are statically linked; rebuild catacomb with a local
      pmaports package.**  The Alpine `testing/catacomb` APKBUILD is copied to
      `local/work/nura/pmaports/main/catacomb/`, `pkgrel` bumped, `!check`
      added, and `prepare()` applies `smithay-gles2-fallback.patch` to the
      fetched cargo git checkout.  Build with
      `--as-root --no-cross --no-ccache build --lax --arch armv7 catacomb`:
      crossdirect refuses native rustc 1.100.0 against the armv7 chroot's
      1.99.1, and `sccache` is absent from the QEMU buildroot.  LTO under QEMU
      is ~18–20 min with `!check`.  (Session 56, report 246 §3.)

287. **The Munin touch Y axis is inverted relative to the display.**  Measured
      with a top/bottom edge capture: physical top → evdev `Y 577–591`,
      physical bottom → `Y 35–51`; the X axis is correct.  The driver now
      reports `panel_h - 1 - munin_map(cal_y, yr, panel_h - 1)`.  Kernel `#69`.
      Report 198's "Y=0 at the top" does not hold on this display pipeline; the
      bench measurement is authoritative.  (Session 56, report 246 §4.)

288. **The SGX kernel driver is vendored, not folded into the board patch.**
      `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` (362 files, sha256
      `a9d04d3c…`) + `kernel/vendor/README.md`; the main patch is 64 files /
      8 800 lines (sha256 `014eb8a8…`), verified 0 rejects / 64/64 identical,
      and the driver 362/362 after extraction.  The old patch's two generated
      `scripts/mod/*.h` were removed.  Apply the patch, extract the archive
      into `drivers/gpu/drm/`, then build.  (Session 56, report 246 §6.)

289. **Restarting the compositor over a wedged DSI does not recover.**  After a
      panicked/restarted session, stopping tinydm left the screen black and a
      restart produced `DSI: Failed to set dsi_if_enable to 0`, `VC(0) busy`,
      `NO PACKET_SENT`, `VC(1) error`.  A clean boot does one clean modeset
      (`irqstatus=0x4 polls=1`).  Recover with a reboot; do not iterate
      compositor restarts on a wedged link.  (Session 56, report 246 §5.)

290. **OSK key presses still drop frames; the full-row upload optimization did
      not fix it.**  The v2 fallback uploads whole damaged rows for a tight
      stride (one call per rect) instead of one per row, but the key-press cost
      is unchanged.  Next: log the shm stride/damage, consider packing rows,
      and check `EGL_KHR_fence_sync` vs the per-upload `glFinish`
      (`GL_OES_EGL_sync` is absent).  (Session 56, report 246 §7.)

291. **The Nura `usb0` is down after boot, and the host UFW blocks inbound.**
      Bring the link up with `ip link set usb0 up; ip addr add
      169.254.0.1/16 dev usb0`; transfer **host→device** (device `nc -l -p
      8765 > /tmp/f`, host pushes the bytes) at ~10 MB/s.  Do not add a UFW
      inbound rule just for a transfer.  (Session 56.)

292. **Fixed (session 60, report 251): `epitaph` segfaulted, so the panel never
      appeared.**  It logged `Started Epitaph`, the benign UPower
      `GetAll`-on-`/` warning, then `Segmentation fault`.  Two independent
      causes, both fixed in the local `epitaph` 1.4.0 package
      (`local/work/nura/pmaports/main/epitaph/`, `pkgrel=10005`, patch
      `fix-renderer-init.patch`, sha512 `f035f4c5…`):
      (1) the lazy renderer created the 4096×4096 glyph-atlas texture in
      `Renderer::new`, **before any EGL context existed** — the GL calls were
      silent no-ops, so the first `glTexSubImage2D` failed with
      `GL_INVALID_OPERATION (invalid texture level 0)` and sampling the
      undefined texture crashed softpipe; create the rasterizer in
      `SizedRenderer::new` after `create_surface`, and cap the atlas to
      `GL_MAX_TEXTURE_SIZE` (the SGX540 is 2048);
      (2) the text shader's `GL_EXT_blend_func_extended` /
      `gl_SecondaryFragColorEXT` draw segfaults softpipe *even though the
      extension is advertised and the shader links* (trap 303) — use the
      single-output premultiplied shader with `ONE`/`ONE_MINUS_SRC_ALPHA`.
      Verified: `timeout 6 epitaph` → `RC=143`, and the bar renders
      `Thu. 1` / `03:20` / signal / Wi-Fi / battery.  The panel also needed
      its permissions (trap 305).  (Sessions 57/59 localised the fault;
      session 60 fixed it.  Reports 250 §2b, **251**.)

293. **The audio bring-up diagnostics are still at info/error level and fire on
      every stream start.**  `omap_mcbsp_start()` calls
      `omap_mcbsp_dump_reg()` (`sound/soc/ti/omap-mcbsp.c`), which is 14
      `dev_info` lines (upstream is `dev_dbg`; session 45 promoted it for the
      bench because L4_ABE register reads fault in user mode — trap 158).  The
      machine driver also prints `rate … AIF1CLK …` and the repair lines at
      `dev_info`.  With PipeWire/wireplumber installed, probing the card
      produces a burst of these every time a stream opens/closes; the
      `RX Buffer Underflow!` `dev_err` (upstream `omap_mcbsp_irq_handler`) fires
      on playback-only starts.  None of it is a fault.  Gate the dump behind a
      module parameter (or `dev_dbg`) before shipping.  (Session 57.)

294. **A failed `hw_params` taints the kernel: the shutdown path removes an
      inactive PM-QoS request.**  Measured with `xournalpp` opening the card at
      48 kHz: `omap_mcbsp_dai_hw_params: not enough bandwidth …` (`-EINVAL`),
      then `omap_mcbsp_dai_shutdown()` sees `mcbsp->latency[stream]` set (it is
      assigned *before* the bandwidth check) and calls
      `cpu_latency_qos_remove_request()` on a request `prepare()` never added →
      `WARNING: kernel/power/qos.c:327 … called for unknown object`, taint
      `W`.  Guard the remove with `cpu_latency_qos_request_active()`.  The
      48 kHz failure itself is a format/bandwidth question (the card is stereo
      S16); pbplay's direct 48 kHz path still works.  (Session 57.)

295. **Do not suspend this platform: `loginctl suspend` drops USB, blanks the
      panel, and ends in a WDT2 reset (~1 min later, one factory boot).**
      `/sys/power/state` advertises `freeze mem`, so elogind tries `mem`; the
      unit does not resume.  Observed on the Nura root: the default catacomb
      initrc's `swayidle timeout 180 "catacomb msg dpms off"` blanked the
      backlight, `timeout 210 "$systemctl suspend"` took USB away, and the
      unit came back on a fresh boot with `BOOTCTR_FCTRY` `0x2b → 0x2a`.
      The initrc now keeps DPMS off/resume but has **no suspend action**
      (`local/work/nura/pmaports/main/postmarketos-ui-catacomb/initrc`,
      `pkgrel=1`); the image carries the same edit.  Kill `swayidle` on a
      running root to stop the timer before it fires.  (Session 57.)

296. **Fixed (session 58, kernel `#74`): the fast-tap merge and the position
      ghost.**  The driver re-reads a torn report until the counter parity
      matches, keeps the 10 ms poll armed while the controller asserts its IRQ
      line and for a 200 ms grace period, and holds a new down back one sample
      when it repeats the slot's previous coordinates within 300 ms.  Report
      248.  The session-57 analysis and the fix history are in
      `docs/history/KNOWN-ISSUES-HISTORY.md`, `## Trap 296`.

297. **A both-edge GPIO IRQ request on this line silently yields no
      interrupts.**  Changing the Munin IRQ to `IRQF_TRIGGER_FALLING |
      IRQF_TRIGGER_RISING` (to catch the lift, which is a rising edge) made
      `/proc/interrupts` count for `1-0065` stay at **0** while the driver
      still logged "Munin touchscreen ready": the GPIO irqchip accepted the
      request but did not deliver either edge, and there is no polling
      fallback while `using_irq` is true, so touch dies completely.  Use
      falling-edge plus a poll-on-touch watchdog instead.  (Session 57.)

298. **DPMS off is not woken by swayidle's resume on touch; use
       `pb-touchwake.py` (or kill `swayidle`).**
      The image's initrc runs `swayidle -w timeout 180 "catacomb msg dpms off"
      resume "catacomb msg dpms on"`, and after 3 idle minutes `bl_power`
      becomes 4 (`FB_BLANK_POWERDOWN`).  **Touching the screen does not restore
      it**: on a DPMS-off panel the kernel still received the taps
      (`/proc/interrupts` for `1-0065` advanced, `PBTS` logged them, evdev
      delivered them) but `bl_power` stayed 4.  Typing on the ACM console is
      not input activity, so a bench session goes dark while it works.  Restore
      with `XDG_RUNTIME_DIR=/run/user/10000 WAYLAND_DISPLAY=wayland-1 catacomb
      msg dpms on`, and after every boot `pkill -x swayidle` before an
      SSH-only bench.  **Session 59 fixed the touch case:**
      `tools/bench/pb-touchwake.py` (installed `/usr/bin/pb-touchwake`, launched
      by the initrc, session user in the `input` group) watches
      `/dev/input/event0` for `BTN_TOUCH` down and runs `catacomb msg dpms on`
      when `bl_power != 0` — measured `4 → 0` on one tap; the daemon persists
      across reboot.  **But repeated DPMS resume is unsafe on this kernel:**
      a later auto-blank + tap left a black panel with the backlight on — the
      DSI link wedged (`Failed to set dsi_if_enable`, `NO PACKET_SENT`, `VC(1)
      error`) while catacomb/tabula/wvkbd were alive; only `reboot -f` clears
      it (traps 289/301).  With swayidle's `resume` removed so pb-touchwake was
      the sole DPMS-on caller, two cycles were clean but the third wedged.
      **Policy: swayidle removed from the initrc — the panel stays lit**;
      pb-touchwake handles only a manual `catacomb msg dpms off`.
      (Sessions 58/59, reports 248/250; corrects session 57's "any touch
      restores it".)

299. **FIXED (session 59): the repeat was squeekboard's action-key repeat, not the
      touch driver.**  One tap produces one clean evdev contact (`down=01` →
      `down=00`, `MT_TRACKING_ID -1`), but squeekboard 1.43.1 sends **three**
      `0x7f` bytes for it (+0/+194/+234 ms; measured with `keylog.py` on the
      terminal's stdin).  Letters never repeat (one `a` tap = one `0x61`, and
      an `a` held 1.3 s = one `0x61`).  So do not "fix" this in
      `playbook-munin.c`, and do not read it as a lost release.  **Fix
      (session 59): install wvkbd on the existing catacomb 1.5.0.**  catacomb
      1.5.0 already implements `zwp_virtual_keyboard_v1`, the protocol wvkbd
      speaks; no UI upgrade or GLES2-shim rebase is needed.  `wvkbd-mobintl` 0.21
      was installed with DejaVu Sans for the glyphs; one BackSpace tap now
      delivers one `0x7f`.  The pmaport APKBUILD/initrc are updated
      (`postmarketos-ui-catacomb` depends on `wvkbd-mobintl` + `font-dejavu`,
      `pkgrel=3`).  (Sessions 58/59, reports 248/250, DECISIONS §85.)

300. **CORRECTED (session 59): the eMMC stall is a plain WRITE, not a cache
      flush — the card has no cache.**  `EXT_CSD.CACHE_SIZE = 0` and
      `CACHE_CTRL = 0` (CMD8 read via `tools/bench/mmccache.c` and the kernel's
      `/sys/kernel/debug/mmc1/mmc1:0001/ext_csd` agree), so the MMC core never
      issues `FLUSH_CACHE`; `_mmc_cache_enabled()` requires `cache_size > 0`.
      The "CACHE_SIZE = 249" in the session-58 handoff was the byte *index*.
      The error (`I/O error, dev mmcblk1, sector 0 op 0x1:(WRITE) flags
      0x900000`) is `REQ_BACKGROUND|REQ_ALLOC_CACHE` with no `REQ_PREFLUSH`/
      `REQ_FUA` (a flush would be op `0x2`); a traced `sync` showed only
      CMD25/CMD13, no CMD6, and did not stall.  Keep `barrier=0` +
      `write_cache=write through` (the initramfs sets both) — the journal stays
      writable and resize completes.  The intermittent busy-after-WRITE cause
      is open.  (Sessions 58/59, reports 249 §3 / 250 §1.)

301. **One `DSI: omapdss DSI error: Failed to set dsi_if_enable to 0` at boot
      can recover.**  Measured at 52.7 s on the read-write boot, followed by a
      successful PLL re-lock and hs-clock null packets at 54 s; catacomb runs
      and the backlight is on.  A single modeset that recovers is not the
      wedged-link case of trap 289 — that one is a compositor *restart* over a
      live link.  Watch for a repeat; reboot on a real wedge.  (Session 58,
      report 249 §5.)

302. **An unpaced host→device RNDIS burst loses data.**  Two `nc` copies of the
      968 032-byte dropbear came back short (724 960 and 752 384 bytes) when the
      host sent in one go; sending 8 KiB chunks with a 5 ms pause is
      byte-exact.  Pace bulk pushes (`nc -l -p` on the device; the host paces).
      The link itself is fast (session 52: RX 6.3 / TX 4.1 MB/s), so this is a
      burst/queue effect, not the DMA alignment bug.  (Session 58, report
      249 §4.)

303. **Mesa softpipe advertises `GL_EXT_blend_func_extended` but drawing with
      it segfaults.**  The extension is in `GL_EXTENSIONS`
      (`libGLESv2`/surfaceless says so) and `epitaph`'s dual-source text
      shader compiles and links, but the first `glDrawElements` with it dies
      at `swrast_dri.so+0x3da8d0` (`vldr [r5]`, `r5=0`, `fault_addr=0`).
      `MESA_EXTENSION_OVERRIDE=-GL_EXT_blend_func_extended` makes the shader
      fail and the app survive (`RC=143`), so it is the draw, not the
      extension probe.  Use a single-output premultiplied shader
      (`ONE`/`ONE_MINUS_SRC_ALPHA`).  (Session 60, report 251 §3.)

304. **A GL call made before an EGL context is current is a silent no-op.**
      `glGenTextures` leaves the name unchanged, `glTexImage2D` returns no
      error and defines nothing; once the context exists,
      `glTexSubImage2D(TEXTURE_2D, 0, …)` returns `GL_INVALID_OPERATION`
      (`invalid texture level 0`).  Construct GL resources only after
      `eglMakeCurrent`.  Measured with a ctypes probe on the unit.  (Session
      60, report 251 §2.)

305. **`epitaph` ships udev/polkit permission files that Alpine's APKBUILD does
      not install.**  Without them the brightness slider writes a `0644
      root:root` sysfs file (no effect) and the Wi-Fi/cellular toggles get
      `org.freedesktop.NetworkManager.PermissionDenied` (the user is not in
      group `catacomb`).  Install `rules/66-epitaph.rules.udev` to
      `/etc/udev/rules.d/` and `…polkit` to `/etc/polkit-1/rules.d/`, create
      the `catacomb` group and add the session user.  The device backlight
      must then be re-chgrp'd (or `udevadm trigger --subsystem-match=backlight`
      run).  (Session 60, report 251 §6.)

306. **`uinput` is not in the Nura module set, but the kernel has
      `CONFIG_INPUT_UINPUT=m`.**  Build it from the tree and load it for touch
      injection: `make -C local/kernel-tree ARCH=arm CROSS_COMPILE=arm-linux-
      M=drivers/input/misc uinput.ko`, push it paced, `insmod`.  It taints the
      kernel ("loading out-of-tree module") but works: udev tags the device
      `ID_INPUT_TOUCHSCREEN=1` and catacomb hotplugs it (`/dev/input/eventN`
      opens in the compositor's fd table).  `tools/bench/uitap.py <x> <y>`
      emits one tap; the `UI_SET_*` ioctls take the bit **value**, not a
      pointer (a struct-pack argument is `EINVAL`).  (Session 60.)

307. **An xdg window launched while the compositor is not in a clean workspace
      can render tiny/stale and receive no touch.**  Measured: the first
      `tzompantli` (launched from SSH alongside other windows) stayed alive but
      ignored taps, while a fresh fullscreen launch accepted an injected tap
      (Foot launched) and `gorm` accepted taps after the radio was on.  The
      same binary rendered a 3-column grid (scale 2) once and a 6-column grid
      (scale 1) later, so the surface scale/configure can vary.  Return to the
      workspace and relaunch the app before concluding touch is broken; check
      `/proc/<catacomb>/fd` for the `eventN` and the app's own log.  (Session
      60; the exact trigger is unconfirmed.)

308. **Wi-Fi is off in NetworkManager on the Nura root.**  `wlan0` is present
      (`permaddr 14:74:11:8f:9c:c3`), the `wl128x` firmware boots
      (`firmware booted (Rev 7.3.5.0.98)`), but `nmcli radio wifi` says
      **disabled** and `nmcli dev status` shows `wlan0: wifi unavailable`.
      `nmcli radio wifi on` fixes it: `wlan0: disconnected`, and
      `nmcli dev wifi list --rescan yes` returns the APs.  `gorm` (the Wi-Fi
      settings GUI) also needs its own rule, `10-gorm.rules` (scan /
      network-control / settings.modify.system / enable-disable-wifi for
      group `catacomb`), installed at `/etc/polkit-1/rules.d/`.  (Session 60.)

309. **The amber Mesa has no llvmpipe; its software client path is softpipe,
      which is ~50× too slow for a full-screen client.**  A Wayland EGL client
      gets `swrast_dri.so` (softpipe) and cannot use `pvr_dri` (GBM/DRM only);
      `epitaph`'s 1024×585 drawer draw measured 1.6–2.3 s.  Fix: install
      **Alpine 3.16's Mesa 21.3.9 `mesa-dri-gallium` `swrast_dri.so`** (15.8 MB,
      contains llvmpipe) over `/usr/lib/xorg/modules/dri/swrast_dri.so`, with
      `libLLVM-13.so` (llvm13-libs 13.0.1-r2) and `libdrm_amdgpu.so.1` /
      `libdrm_nouveau.so.2` (edge libdrm 2.4.134, matching the root) in
      `/usr/lib/`.  Back up the original (`swrast_dri.so.amber`).  Result:
      `GL renderer "llvmpipe (LLVM 13.0.1, 128 bits)"`, drawer `render`
      2.7–6 ms; the compositor still maps `pvr_dri`.  (Session 60, report 253.)

310. **llvmpipe's first draw pays the LLVM JIT, and its `swap_buffers()` waits
      for the deferred rasterization.**  Measured: first panel draw 1.75 s,
      first drawer draw 4.15 s, then 2.7–6 ms of command submission with
      **420–550 ms in `swap`** for a 1024×585 frame.  Pre-warm the shaders at
      start if the hitch matters; shrinking the drawer surface to its visible
      height (or damaging only the slider row) roughly halves the swap.
      llvmpipe was single-threaded because the kernel then booted `maxcpus=1`
      (now dual-core; trap 313 resolved).  (Session 60, report 253.)

311. **The `PBGL_PROFILE` byte counter wraps.**  `KiB/s` prints huge values
      (`8589936748`) while `import/s`, `texsub/s`, `rects/s`, `avg_us` and
      `max_us` are valid.  The unpack branch multiplies `region.size.w as u64 *
      region.size.h as u64 * bpp` without clamping negative dimensions.  Fix
      the profile patch (`w.max(0)`/`h.max(0)` + saturating arithmetic) before
      trusting the byte rate.  (Session 60.)

312. **RESOLVED (session 67, report 266): the MPU runs at 1008 MHz and
      `cpufreq-dt` is bound and scaling** (OPPs `300000 600000 800000 1008000`,
      `ondemand`; verified up and down).  Only the **unattached CPU supply**
      remains (`vdd_mpu`; no `supply-*` in the OPP debug dirs), so the OPP
      voltages are still not applied.  The text below is the session-60/62
      measurement that preceded the fix, kept for its OPP-table provenance.
      **Then, the MPU ran at 400 MHz with no cpufreq/OPP — but the OPP *table*
      is already there, from the SoC dtsi.**  Measured (session 60, re-read live in
      session 62): `clk_summary` shows `dpll_mpu_ck = 400000000`,
      `dpll_mpu_m2_ck = 400000000`, `div_mpu_hs_clk = 400000000`; BogoMIPS
      795.44; there is no `/sys/devices/system/cpu/cpu0/cpufreq/`.  The board
      DTS adds no `operating-points`/`cpu-supply`, but the **upstream
      `omap443x.dtsi` already carries the OMAP4430 table** — the built DTB's
      `cpu@0` node has `operating-points = <300000 1025000  600000 1200000
      800000 1313000  1008000 1375000>` (kHz/µV) and
      `clock-latency = <300000>` — so "there is no OPP table" is wrong: what is
      missing is a **`cpu-supply` (a `vdd_mpu` regulator) and the driver
      binding** (`CONFIG_CPUFREQ_DT=m` is built but not loaded; no `cpu-supply`
      means nothing binds).  The reference OS programs the MPU DPLL and voltage
      itself (`CM_CLKMODE_DPLL_MPU 0x4A004160` idle-bypass→lock,
      `CM_CLKSEL_DPLL_MPU 0x4A00416C`, `PRM_VC_* 0x4A307BA0–0xBF8`; report 223
      Q6/§9), so our boot chain simply leaves the loader's 400 MHz.  This is the
      biggest performance lever (the OMAP4430's nominal is 1.0 GHz): measure the
      reference's exact OPP/voltage on the oracle first, then wire `cpu-supply`
      + `vdd_mpu` (+ ABB) to the existing table, or write the DPLL/voltage at
      boot, and check the thermal zone (`cpu_thermal` read 65.0 °C idle at
      400 MHz).  (Session 60, report 253; table located session 62.)

313. **RESOLVED (session 67, report 266): both CPUs are online, userspace
      included.**  `CONFIG_SMP=y`, `CONFIG_NR_CPUS=2`, no `maxcpus=1`; the
      remaining defect was **PID 1's affinity mask** (trap 340), not the build.
      The text below is the session-60 state and the procedure that was followed.
      **Only CPU0 was online: the kernel was built `CONFIG_SMP=n`, and the DTS
      also set `maxcpus=1`.**  `dmesg` reported `Unknown kernel command line
      parameters "maxcpus=1"` and `DT /cpu 2 nodes greater than max cores 1,
      capping them`; `online=0`, `nproc=1`.  Enabling the second core needs
      **`CONFIG_SMP=y` (+ `CONFIG_NR_CPUS=2`)** *and* removing `maxcpus=1`
      from `kernel/source/omap4-winchester.dts:88`.  `CONFIG_SMP` changes the
      module vermagic, so **all** modules (the in-tree set and the vendored
      `pvrsrvkm_omap4_sgx540_120.ko`) must be rebuilt and installed before the
      region-0 flash.  Do it in two boots: (a) SMP with `maxcpus=1` still set
      (proves the SMP kernel comes up on one CPU), (b) remove `maxcpus=1` and
      boot cpu1.  Check `online`, dmesg, WDT2, thermal and a llvmpipe drag; a
      cpu1 hang is a revert with dmesg captured.  (Session 60.)

314. **A Wayland GL client cannot use the SGX.**  The amber Mesa Wayland
      platform gives clients `swrast` (softpipe, or llvmpipe after trap 309);
      only the compositor's GBM/DRM path selects `pvr_dri`, and the
      `sgx-ddk-um-ti443x` package ships `libGLESv2_PVR_MESA` but **no
      `libEGL`**.  Consequence: `epitaph`, `tzompantli`, `gorm` and `tabula`
      are software-rendered and pay the full-surface cost; `wvkbd` (cairo),
      `foot` (pixman) and `netsurf` (CPU) are unaffected.  A hardware path
      would need a pvr Wayland EGL platform (`wl_drm`/linux-dmabuf), a client
       rendering via GBM+dmabuf, or an X/Wayland bridge — not started.  (Session
       60, report 253.)

       **Mechanism measured (session 67, reports 267 §3 and 268 §0).**  What is
       true: `pvr_dri.so` is a Mesa gallium DRI driver loaded *by* libEGL (it
       exports no EGL entry points — normal; `swrast_dri` is the same shape), and
       the **GBM/DRM** platform does work:
       `eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA, <renderD129>)` → `EGL 1.4`,
       12 window configs, `GL_RENDERER = PowerVR SGX 540`,
       `EGL_MESA_image_dma_buf_export` present.  The Wayland-client path still
       gets `swrast_dri.so`/`softpipe`, and `MESA_LOADER_DRIVER_OVERRIDE=pvr`
       does **not** change that.  **`libEGL_amber` is not wayland-only** — an
       earlier version of this entry said so, from a bad anchored `grep` (trap
       344); it advertises wayland, x11, gbm, surfaceless and device.

       **WHY, measured (session 68, report 269).**  Mesa 21.3.9's Wayland EGL
       platform gets its DRM fd **only** through the legacy `wl_drm` protocol
       (`dri2_initialize_wayland_drm()`: `dri2_dpy->wl_drm == NULL` → fail,
       `platform_wayland.c:1567`), then `eglInitialize()` retries with
       `ForceSoftware = TRUE` (`eglapi.c:640`) and `dri2_initialize_wayland_swrast()`
       loads `swrast_dri` unconditionally.  `MESA_LOADER_DRIVER_OVERRIDE` is read
       by `loader_get_driver_for_fd()` on the fd path, which the software retry
       never reaches — hence it is ignored.  **catacomb (Smithay) advertises no
       `wl_drm`** (its globals include `wl_shm` and `zwp_linux_dmabuf_v1` but not
       `wl_drm`), which is the whole cause.  Compositors that do serve `wl_drm`
       (wlroots/phoc, KWin, Mutter — exactly pvrports' supported set) let the
       hardware driver load.  So the fix is **above** Mesa's driver selection: a
       compositor that serves `wl_drm`, or a pvr Wayland EGL platform.  The
       `swrast` result is not a missing DRI extension.  Report 269 is current.

315. **CORRECTED TWICE (session 62): the SMP kernel used to die inside the
      loader's watchdog window, not at a statement — and the stop point moved
      boot to boot for that reason.**  Read with a trustworthy ring (trap 321):
      * **`rcu_init()` completes.**  The `rcu2` marker image (`0x4F6CC563`,
        `#85 SMP`) prints **every** marker inside it — `rcu_init enter`,
        `after early_boot_tests`, `after init_geometry`, `after init_one`,
        `after open_softirq`, `after pm_notifier`,
        `after rcutree_prepare_cpu`, `after rcutree_report_cpu_starting`,
        `after rcutree_online_cpu`, `after rcu_gp_wq`, `after sync_wq`,
        `after qovld`, `after start_poll_expedited`, `after test_sync_prims`,
        `rcu_init done` — and continues into `kvfree_rcu_init`/`trace_init`.
        Session 61's "stops right after the RCU geometry print" was a ring
        artefact (trap 317a).
      * **The stop point varies on one image**: the plain SMP image
        (`0x202E9F38`, `#78 SMP`), 7 boots, stopped at 3208 / 3943 (×5) / 4211
        characters.
      * **The cause is the loader's 7.8125 s WDT2 window** (trap 325): measured
        by sampling `CRR` in `head.S` at kernel entry, the kernel starts at
        **7.450 s of 7.812 s — 362 ms left** — and needs ~0.74 s of its own time
        to reach the late initcall that re-programs WDT2.  A fast chainloader
        copy plus a quiet boot gets it there, and **the SMP kernel then boots to
        userspace** (`possible = 0-1`, `online = 0`).
      * Everything the platform SMP path touches is readable/writable from
        non-secure Linux (`SCU_CONFIG = 0x1`, `SCU_CTRL = 0x531`,
        `0x4824380c = 0`, `0x48281800/4 = 0`, PL310 ID `0x410000C4`).
      (Sessions 61/62, reports 254/255.)

316. **A DEBUG_LL ring can only be read by the boot that immediately follows the
      failure — and `dd if=/dev/mem` cannot read it.**  There are only **two**
      writers: the **chainloader** (breadcrumbs at `+0x00..+0x7c`, the epoch
      block at `+0x88..+0xb3`, the ring hand-off record at `+0xb8..+0xc8` — and,
      since session 62, **no clear**) and the DEBUG_LL path (`busyuart` mirrors
      every char into all three character rings via `ring_put`: count `+0x80`,
      index `+0x84`, chars `+0x100`).  There is **no `+0x1000` printk window**
      and no third writer (trap 317a).  The ring pages are `no-map` reserved, so
      `dd if=/dev/mem` returns `Bad address`; use `mmap`
      (`tools/bench/ringdump.c`) or busybox `devmem` one word at a time, and note
      that `mmap` needs a page-aligned offset.  Since the hand-off (trap 321) the
      read-back boot does **not** have to be silent, but it must not print more
      than ~32 K characters or it wraps the ring and overwrites the failure; the
      read-back image used on the bench has `earlyprintk` removed from its DT
      `bootargs` (a `make dtb`-only change).  Root it at region 1 for a shell.
      (Session 61, report 254 §2; revised session 62, report 255.)

317. **QNX fills free RAM with `0xAA`, so a QNX boot destroys the ring.**  On the
      oracle `0x88000000`, `0x90000000` and the pbmon shadow `0x90200000` all read
      back as `0xAA` fill (no printable run in 64 KiB) while the IFS load address
      `0x80000000` keeps real data — procnto initialises the RAM it considers
      free, and the ring pages are free RAM to it.  **Open:** earlier sessions
      read full rings through the QNX door, so some image/config preserved them;
      do not present "QNX always wipes it" as settled.  (Session 61, report 254.)

317a. **CLOSED (session 62): the `+0x1000` "printk window" is the ring's own
      character array at character #3840 — there is no second writer.**  The old
      chainloader cleared `+0x80..+0xFFF` (`0x3E0` words), i.e. exactly up to one
      byte short of `+0x1000`; `+0x1000 - 0x100 = 0xF00 = 3840`, and the only
      writer of the page is `ring_put` in
      `arch/arm/include/debug/omap4bc.S` — which is also the kernel's
      `earlyprintk` path (`early_printk.c` → `printascii` → `busyuart`).  Proof:
      on a live ring the text flows straight across `+0x1000` mid-line
      (`[ 9.624053] … kpp` / `ime\r\n[ 9.628753] … kpp` / `[ 9.633453]`), and
      `index == count mod 0x7F00` exactly.  The "impossible shorter trace" was a
      boot that printed **fewer than 3840 characters**, so it never wrote at
      `+0x1000` and the window still held an older, longer boot's text: the
      offset of `mobility grouping` is 3822 in every SMP log, `SLUB:` is at 3959
      and `RCU calculated` at 4294, so a 3943-character boot leaves
      `mem auto-init…` at `+0x1000` and a 4986-character one leaves `SLUB`+`RCU`.
      Read the ring with `tools/pb_ring.py` (now hand-off aware), never a
      "window".  (Session 61 found it; session 62 closed it — report 255 §1.)

318. **The host's RNDIS interface name flaps, so a NetworkManager profile bound
      by name silently stops activating.**  The PlayBook enumerates as
      `enp8s0f3u1` or `enp8s0f3u1i2` depending on USB interface numbering; the
      `pb-rndis` profile (manual `169.254.0.2/16`) then shows `disconnected` and
      `169.254.0.1` is unreachable ("No route to host" / ARP fails) even though
      `lsusb` shows `0fca:8010`/`8012`.  Bind by MAC
      (`802-3-ethernet.mac-address 02:1a:11:00:00:02`) rather than
      `connection.interface-name`, and do not add the address by hand —
      `nmcli con mod … ; nmcli con up pb-rndis` is the mechanism.  (Session 61.)

319. **`SMC` from a QNX user process is `SIGILL` — it is not evidence about the
      ROM.**  `tools/bench/smcprobe.c` calls services `0x102`–`0x105` with the
      `omap-smc.S` convention (`r12` = service) and every one dies with `SIGILL`,
      **including `0x102`**, which our own non-SMP kernel uses successfully to
      program the PL310 through `omap4_l2c310_write_sec()`.  `SMC` is a
      privileged instruction: a user process gets an undefined-instruction trap
      on any OS that does not emulate it, so the ROM public API demonstrably
      exists here and `omap-smp.c`'s services are not the leading suspect.
      (Session 61, report 254 §4.)

320. **The region-1 root's modules no longer match the current kernel config.**
      Every `insmod` on the region-1 busybox root now fails with
      `module X: .gnu.linkonce.this_module section size must match the kernel's
      built struct module size at run time` — the module set predates the
      current config, so `struct module` differs.  Harmless for `devmem`/
      `ringdump` work, but the region-1 image needs its modules rebuilt (and its
      `/lib/modules/7.3.3` re-staged) before it is a useful bench root again.
      When pushing SMP-built modules to a root, remember the vermagic gains
      `SMP` (trap 313).  (Session 61.)

321. **The chainloader no longer clears the DEBUG_LL ring: it hands it off, and
      the reader must use ring2's recorded entry headers.**  Session 62 replaced
      `clear_ring` (which zeroed `+0x80..+0xFFF` on every boot and destroyed every
      early-death log) with an append-only contract in
      `boot/chainloader/chainloader_kernel.S.in`:
      * an `index >= 0x7F00` is sanitised to `count = index = 0` (a QNX boot
        leaves `0xAA` fill, and `0xAAAAAAAA` as an index would walk `ring_put`'s
        character store out of the page);
      * `+0xb8` = magic `RHND` (`0x444E4852`), `+0xbc`/`+0xc0` = *this* boot's
        entry count/index (= the previous boot's end), `+0xc4`/`+0xc8` = the
        entry of the boot before (= the previous boot's start);
      * the character area is left alone, so a read-back boot **appends** its
        ~1.6 KB after the failed log.
      Read it as: previous log = chars from `prevprev_index`,
      `prev_count - prevprev_count` long; this log = chars from `prev_index`,
      `count - prev_count` long.  `tools/pb_ring.py` implements this and falls
      back to the old model when the magic is absent.  Only **ring2
      (`0x90000000` = `BC_PRIMARY`)** carries a trusted record — the mirrors'
      `+0x80..+0xFF` is overwritten by `bc_mirrors` and by `rlog`.  Two
      consequences: the read-back image must not print more than ~32 K
      characters (it would wrap and overwrite the failure), and **a power-off
      wipes the ring** (DRAM loses content) — capture before any power cycle.
      (Session 62, report 255 §2.)

322. **Do not program WDT2 from the chainloader — it wedges the boot and can
      leave the watchdog stopped.**  Session 62 tried to extend the loader's
      7.8125 s window from the chainloader with the kernel's own `WSPR` sequence
      (stop `0xAAAA`/`0x5555`, write `WLDR`, start `0xBBBB`/`0x4444`, polling
      `WWPS` after each write).  The image never reached the kernel: the unit sat
      dark with **no USB and no periodic reset for minutes**, i.e. the timer had
      been stopped rather than reloaded, so nothing could reset it and the
      bootrom never reappeared.  Recovery was a power-button power cycle (and the
      bootrom window at power-on) — see trap 323.  Report 109 §8.1's
      "loader-side WDT writes wedge the loader" stands; the kernel's
      late-initcall reprogram is the supported path (trap 115), and the window is
      *not* what kills the SMP kernel (trap 315).  (Session 62, report 255 §4a.)

323. **The bootrom can take ~20 s before it hands over to the chainloader, so a
      fixed short delay after a flash proves nothing — poll instead.**  A
      read-back flash armed 10 s after the image-under-test flash caught the
      bootrom *before that image had run at all* (the ring showed no log for it),
      which looked like "the image printed nothing".  Operator's bench fact: the
      bootrom may hold for up to ~20 s.  Wait ≥25 s, or poll `lsusb` for the
      bootrom's disappearance/reappearance (`0fca:0001` ↔ none ↔ `8001`) rather
      than sleeping a fixed time.  Same class as trap 266, one layer out.
      (Session 62.)

324. **`Marks`/`marks2` are silent by construction.**  Both marker images from
      session 61 (`local/work/kernel-smp-marks{,2}.img`) carry a DTB whose
      `bootargs` has **no `earlyprintk`** (96 483-byte DTB, `grep -ac earlyprintk`
      = 0), so their kernels print nothing to the ring whatever their markers do.
      Only `kernel-smp-rcu2.img` (`0x4F6CC563`) has `earlyprintk` *and* markers.
      Check `grep -ac earlyprintk <image>` (`= 1`) before spending a boot on a
      marker build.  (Session 62, following trap 315.)

325. **The loader's 7.8125 s WDT2 window is the real budget for an SMP boot — and
      the chainloader's copy was eating a quarter of it.**  Measured with a WDT2
      `CRR` sample taken in `head.S` at kernel entry (ring2 `+0x18`, saved by the
      chainloader into `+0xd0`; see trap 321):

      | phase | seconds of the 7.812 s |
      |---|---|
      | loader arm → chainloader entry | 0.007 |
      | chainloader copy (word-at-a-time, MMU+caches off) | 0.825 |
      | zImage jump → kernel entry (XZ decompression) | **6.619** |
      | **left when `start_kernel` runs** | **0.362** |

      The kernel needs ~0.74 s of its own time to reach `pb_wdt_timer_init()`
      (the late initcall that re-programs WDT2 to 58.6 s), so it was killed
      mid-init — which is exactly what the varying 3208/3943/4211-character
      stop points are (trap 315).  **Two changes fix it and were verified
      together:** `copy_words` now copies in eight-word `LDMIA`/`STMIA` bursts
      (saving `r4-r10`: the caller keeps `BC_PRIMARY` in `r4` and the CRR in
      `r11`), and the boot is done with `loglevel=0` so the ~4 KB of early
      `printk` through the uncached ring does not spend the rest.  With both,
      the SMP kernel reaches its own reprogram and **boots to userspace**
      (`possible = 0-1`, `online = 0`).  Never solve this by writing WDT2 from
      the chainloader (trap 322).  (Session 62, report 255 §4.)

326. **CORRECTED (session 63): the "SMP-only ext4 mount failure" was a rootfs-
      offset difference between two images, not an SMP kernel bug.**  Session 62
      saw the first SMP boot fall back to its console with

          loop0: detected capacity change from 0 to 124583936
          loop0: detected capacity change from 124583936 to 122191872
          EXT4-fs (loop0): VFS: Can't find ext4 filesystem

      while the UP read-back image mounted "the same" region-1 filesystem fine.
      They were **not** the same filesystem: the SMP image's initramfs has
      `ROOTFS_RAW_OFF = 0x49000000` (hd3, `initramfs/init.c:452`), which at the
      time held the **1917 vendor UFS (QNX6)** — `qnx6 49000000` mounts it
      read-only.  The capacity drop is exactly `0x49000000` bytes
      (`124583936 − 122191872 = 2392064` sectors × 512).  The UP image had
      `0x29000000` (region 1, a real ext4).  After flashing the region-1 ext4
      (`49e318bf…`) to hd3 at `0x4915`, the same SMP kernel printed
      `EXT4-fs (loop0): mounted filesystem … r/w without journal` and booted to
      a full userland with dropbear — no retry, no delay, no kernel change.  The
      `nosmp` discriminator was invalid too: `nosmp` leaves `nr_cpu_ids = 2`
      (`SLUB: CPUs=2`) and only caps online CPUs (already 1).  (Session 63,
      report 256.)

327. **`nosmp` does not reduce `nr_cpu_ids` — it is not a "one possible CPU"
      test.**  On the SMP kernel, a `nosmp` cmdline still prints
      `SLUB: … CPUs=2` and `percpu: [0] 0 [0] 1`, and `smp: Brought up 1 node,
      1 CPU`; only the number of *online* CPUs changes (and that was already 1
      with `maxcpus=1`).  To exercise SMP code paths with one possible CPU you
      need `CONFIG_NR_CPUS=1` (a rebuild) or to drop `cpu@1` from the DTB.
      (Session 63.)

328. **dropbear blocks in `wait_for_random_bytes` on a fresh boot, so SSH opens
      the port but never sends a banner.**  With the firewalled SoC RNG (trap 87)
      the CRNG is unseeded; `netstat` shows dropbear LISTEN and the host gets
      `Connection timed out during banner exchange` with no server banner, while
      ping works both ways.  `/bin/pbconsole`'s `entropy <hex>` (≥32 hex digits;
      64 → "credited 256 bits") triggers `random: crng init done`; dropbear's
      `wchan` moves from `wait_for_random_bytes` to `poll_schedule_timeout` and
      SSH works.  Re-seed after every boot that serves dropbear.  (Session 63.)

329. **CORRECTED later in session 63: cpu1 brings up fine; dual-core boot is
      nondeterministic.**  Early in the session, `kernel-smp-cpu1.img`
      (`0x182B0E00`, no `nosmp`/`maxcpus`) produced no USB for ~70 s then a brief
      `0fca:0001` bootrom — a reset loop draining `BOOTCTR_FCTRY` (`0x26 → 0x22`,
      no top-up because `/init` was never reached).  The full marker trace
      disproved a `smp_init()` hang: a `kernel-smp-cpu1-mark4.img` (`0x0A80BDC6`)
      boot printed `PBCPU: post smp_init, online cpus=02` and every `PBCPU1:`
      marker through `post complete`, and the system then ran with `nproc=2`,
      `online=0-1` and `twd` interrupts on both CPUs (4863 / 5770).  Dual-core at
      1008 MHz also booted once cleanly (`kernel-ipidbg-cpu1.img`, `0xF8264016`,
      `#91 SMP`, both cores, stable >2 min) but a 3-boot set gave 1 success /
      2 wedges.  The failure is a clean `execve("/init")` hang, not a power
      reset; power is ruled out (trap 331).  Recovery from a wedge: reflash the
      known-good `kernel-fastcl-nosmp.img` (`0x8B83421F`).  The remaining fault
      is a timing/race in the IPI/TLB-flush path (trap 330).  (Session 63,
      reports 256 §4 / 257 §4.)

330. **Dual-core wedge at 1008 MHz is a race in the IPI/TLB-flush path, not a
     fixed stall.**  With two cores online, `execve("/init")` →
     `flush_old_exec` → `smp_call_function_many()` → `csd_lock_wait()` sends a
     TLB-flush IPI to CPU1; the boot wedge shows all markers through
     `pre run_init(/init)` (or `post run_init FAILED`) and a frozen cursor.
     Markers `PBSMF: pre/post csd_lock_wait` (CPU0) and `PBIPI1: CALL_FUNC
     enter/done` (CPU1, `do_handle_IPI`) are in the tree but did not remove the
     nondeterminism (1 success / 2 wedges) — timing perturbation, not a single
     stuck statement.  Settle by capturing the ring from a *wedged* boot of
     `kernel-ipidbg-cpu1.img` (`0xF8264016`) via the quiet read-back image and
     `tools/pb_ring.py decode --boot prev`; the last markers show whether the
     IPI is sent, received, or serviced.  **Measured (session 64, report 258):
     the IPI is sent, received and serviced.**  The wedge log (5512 chars) shows
     full CPU1 bring-up, `online cpus=02`, a `PBSMF: pre`/`post csd_lock_wait`
     pair, ~170 `PBIPI1: CALL_FUNC enter`/`done` pairs, and **four clean
     `enter`/`done` pairs after `PBCPU: pre run_init(/init)`** before silence.
     So it is the handoff's fourth branch — the hang is *later* than the IPI
     service; instrument `flush_old_exec`/`mmput` next.  Caveat: the final
     `execve` shows no `PBSMF` line, but CPU0/CPU1 `printascii` share the ring
     and visibly garble markers (trap 332), so that absence is not evidence of a
     csd ack failure.  (Session 63 measured the nondeterminism; session 64,
     report 258, read the wedge markers.)  **Superseded:** report 262 found the
     real fault is the MUSB gadget bind (trap 333), not an IPI/TLB-flush race;
     the "instrument `flush_old_exec`/`mmput` next" inference is obsolete.

331. **The dual-core 1008 MHz wedge is not a power problem.**  MPU rail is
     correctly commanded to the 1 GHz OPP: `PRM_VC_VAL_BYPASS = 0x003A5512`
     (VSEL `0x3A` = 1.35 V) and `PRM_VC_VAL_CMD_VDD_MPU_L = 0x3A3A1400`; battery
     `bq27541-0` = 3819000 µV / 97 % / Full.  Dual-core still wedges with power
     proven good, and the failure is a clean software hang, not a reset.  Note
     the VC's `reg = 0x55`/`slave 0x12` is its own SRI2C addressing; the Linux
     I²C PMIC register for SMPS voltage is `0x30 + 4 = 0x34`, and reading `0x55`
     over Linux I²C 0x48 returns `0x00` and is **not** the MPU rail.  The kernel's
     VDD1 SMPS ops return `-ENODEV` (VC-hardware-managed).  1.2 GHz
     (`clk_set_rate` to 1200000000) crashed the unit.  (Session 63, report 257
     §2/§5.)

332. **CPU0 and CPU1 `printascii` markers share one unlocked ring, so a marker
      can be garbled or lost.**  The `PB*` markers write through `printascii` →
      `busyuart` → `ring_put` into the same append-only character stream, with
      no per-CPU buffer and no lock.  In the session-64 wedge log a `PBSMF: pre
      csd_lock_wait` is visibly interleaved with a CPU1 `PBIPI1: CALL_FUNC` line
      (`…entPeBrS / M / F: pPre csd_lock_wait`), and the final `execve` shows no
      `PBSMF` line at all even though IPIs are being serviced.  **Consequence:**
      absence of a marker in a dual-core log is weak evidence; do not conclude a
      csd-ack failure from it.  Use CPU0-only markers, or a per-CPU marker page,
      for the next instruments in `flush_old_exec`/`mmput`.  (Session 64,
      report 258 §0/§3.)

333. **The dual-core 1008 MHz wedge is the USB gadget bind from `/init`, not an
     IPI race.**  Session 64: initramfs `/init`'s `gadget_setup()` writes the UDC
     via configfs; PID 1 then Oopses in `omap_musb_set_mailbox` from
     `musb_start` (or hangs `pbrpmb`), because the mailbox function jumps to a
     zeroed page.  Disabling `gadget_setup()` entirely gave no wedge (>100 s),
     proving the trigger.  A targeted fix — call only
     `omap_control_usb_set_mode(USB_MODE_DEVICE)` from
     `omap2430_musb_enable()` on winchester, skipping the faulting mailbox —
     gave 3/3 clean dual-core boots (`kernel-usbdev-cpu1.img`, `0xC675E206`).
     Rejected: SMC 0x108 replica (0/3), 1 s gadget delay, `otg->set_vbus` text
     guard, PID-1 CPU0 affinity pin.  The ~32 s USB drop seen later is idle
     runtime PM (`VUSB: disabling`), not a wedge.  (Session 64, report 262.)
     **Corrected in session 65: the drop is *not* an idle timer** (it did not
     occur in a 376 s boot; see trap 337), and **serializing the two callers
     does not fix the mailbox fault** — a glue mutex plus the stock call still
     panicked on dual-core (report 263 §1), so the device-mode-only workaround
     stays and the fault is probably corruption of the `usb_phy`/`usb_otg`, not
     the race.

334. **The kernel was being built `-march=armv6k`, not ARMv7.**  The kernel log
     showed `CPU: ARMv7 Processor`, but module vermagic said `ARMv6 p2v8` and
     the compile flags were `-march=armv6k -D__LINUX_ARM_ARCH__=6`.  Cause:
     `CONFIG_ARCH_MULTI_V6=y` selects `CPU_ARM1176 → CPU_V6K → CPU_32v6`, and
     `arch/arm/Makefile` (lines 68–70 and 84–86) makes the v6K setting
     override the v7 one whenever `CPU_32v6` is set.  `CPU_V7` selecting
     `CPU_32v6K` is normal (v6K extensions are part of v7) and is not the
     trigger.  Fix: `scripts/config --disable ARCH_MULTI_V6` (this unit is
     OMAP4430 only) + `olddefconfig`; then `-march=armv7-a`,
     `__LINUX_ARM_ARCH__=7`, vermagic `ARMv7 p2v8`.  ARMv6 code does run on
     Cortex-A9 (v6K is a subset), so this was a performance defect, not a
     correctness one.  The tracked `kernel/winchester-7.3.3.config` now matches
     the bench (`CONFIG_SMP=y`, `CONFIG_NR_CPUS=2`, no `ARCH_MULTI_V6`).
     (Session 65, report 263 §2.)

335. **A stock-mailbox image with `panic=10` boot-loops and spends
     `BOOTCTR_FCTRY`.**  The first dual-core boot of the mutex build
     (`0x19E24D8F`) panicked in `omap_musb_set_mailbox`; `panic=10` rebooted it
     about six times before the unit was powered off, draining the counter
     `0x22 → 0x1c` (one unit per boot, no `/init` top-up because PID 1 never
     reached userland).  Recovery: reflash `kernel-usbdev-cpu1.img`
     (`0xC675E206`) — its `/init` read `0x1c`, wrote `0x32`
     (`pbrpmb: topup: writing 0x1c -> 0x32 (RPMB counter 12463)`, `PASS`).
     **Rule:** boot a deliberately-faulting image with `panic=0` so a panic
     hangs instead of looping; watch the panel/ring, then power-cycle.
     (Session 65, report 263 §1.)

336. **A power cut can damage the read-write hd3 ext4.**  The root is mounted
     `barrier=0` + write-through (session 59).  After several power cycles the
     BusyBox ext4 on hd3 reported `EXT4-fs (loop0): This should not happen!!
     Data will be lost`, `ext4_lookup: … deleted inode referenced`,
     `ext4_validate_inode_bitmap: … Corrupt inode bitmap`, and a
     `/var/log/pm-trace.log` written minutes earlier was gone.  The image was
     replaced with the baked Nura root (report 263 §3).  **Prefer a clean
     `reboot`; treat a power cut as a filesystem event, not just a DRAM
     event.**  The eMMC WRITE-busy stall (report 250 §1, trap 300) makes this
     more likely during write bursts.  A corrupted root is recoverable by
     reflashing the 625,999,872-byte Nura image at `0x4915`.

337. **The USB (`VUSB`) teardown is intermittent, not an idle timer.**  The
     only path that disables the device's own 3.3 V USB rail is
     `twl6030_usb_irq()` in `drivers/usb/phy/phy-twl6030-usb.c`: on a VBUS
     disconnect it calls `regulator_disable(twl->usb3v3)` (the `VUSB:
     disabling` line).  Measured timings: BusyBox root, a command every 15 s →
     link dead at **~30 s**; Nura, SSH traffic → **~90 s**; final session-65
     boot, 240 s of continuous ACM reads plus 90 s idle → **still up at
     376 s**, no VUSB message.  `audio-ldo: disabling` at ~31.8 s is a
     separate regulator event and occurs on both roots.  Whether the TWL
     reads `CONTROLLER_STAT1.VBUS_DET` low spuriously, or the loaded
     `twl6030_charger` module (it probes with `vmax -22 imax: -22`) disturbs
     the charger block, is not settled — see
     `docs/contradictions/usb-vusb-drop-idle-vs-intermittent.md`.  A dropped
     link does **not** re-enumerate on a cable replug; the unit keeps running.
     (Session 65, report 263 §4/§4a.)

338. **The bad indirect transfer in `omap_musb_set_mailbox()` is
     `phy->comparator->set_vbus`, a stale TWL6030 `phy_companion`.**  Session 66
     ran the marker experiment (`panic=0`, winchester early return removed, mutex
     kept; images `0xF1AA4568` / `0x405E8997`): the DRAM ring, recovered via a
     warm reset + read-back image, shows **valid** entry pointers — `status=4`
     (`MUSB_VBUS_OFF`), valid `xceiv`/`otg`, and `otg->set_vbus = 0xc0729b94`,
     which `System.map` resolves to `omap_usb_set_vbus` (kernel text).  Branch
     markers show `sv` still valid immediately before `otg_set_vbus()`, then the
     Oops: `PC = 0xc2c05800`, decoded as `kmalloc-1k … offset 0`, `LR =
     omap_musb_set_mailbox+0x1e0`; there is no `PBMBX prechain`, so the notifier
     chain is not the fault.  Disassembly pins it to `omap_usb_set_vbus()`'s
     tail-call `phy->comparator->set_vbus` (`drivers/phy/ti/phy-omap-usb2.c`) —
     the `struct phy_companion` registered into the `omap-usb2` PHY by
     `twl6030_usb_probe()` (`phy-twl6030-usb.c:358`) holds a slab address where
     `twl6030_set_vbus` text should be.  So the handoff's "stale object → audit
     the PHY lifecycle" branch is right, but one level *below* `musb->xceiv`:
     the comparator, not the top-level `usb_phy`.  The "wild pointer at entry"
     reading and the session-64 "slab address in `otg->set_vbus`" are superseded
     by this symbolisation.  **Next:** audit the comparator lifetime
     (`omap_usb2_set_comparator()` / probe-deferral/rebind and the
     `cpcap-charger.c` writer of `phy->comparator`), not more markers.  The
      device-mode-only workaround stays until a fix restores the stock call for
      OTG host mode.  (Session 66, report 264; capture
      `docs/captures/musb-mailbox-fault-2026-11-19.txt`.)  **Resolved in session
      67 — see trap 339;** the device-mode-only workaround is gone and the stock
      call runs on dual-core.

339. **The dangling comparator is a TWL6030 probe-lifetime bug.**
      `twl6030_usb_probe()` registers `&twl->comparator` in the `omap-usb2` PHY
      (`omap_usb2_set_comparator()`, `phy-twl6030-usb.c:358`) **before**
      `twl6030_usb_ldo_init()`, whose `regulator_get(dev, "usb")` (the board's
      `usb-supply = <&vusb>`) can return `-EPROBE_DEFER`.  The code returned
      `-ENODEV` for **every** regulator error, so a deferred `vusb` failed the
      probe permanently; devres freed the `devm_kzalloc`'d `twl` and left
      `phy->comparator` dangling.  A later slab reuse put a heap pointer in its
      first word (`set_vbus`), and the stock `omap_musb_set_mailbox()` jumped
      through it — the report-264 Oops.  The faulting `r0` is the tell: a
      `kmalloc-192` **devm** object (`pointer offset 64` = `sizeof(struct
      devres)`), whose word 0 is a `kmalloc-1k` address instead of
      `twl6030_set_vbus` text; the workaround had only hidden the dereference.
      **Fix:** return `PTR_ERR(twl->usb3v3)` so `-EPROBE_DEFER` propagates, add
      a `devm_add_action_or_reset()` action that calls
      `omap_usb2_set_comparator(NULL)` on any probe failure/remove, and
      `usb_put_phy()` the reference `omap_usb2_set_comparator()` leaked.  The
      other writer (`cpcap-charger.c:921/940`) is inert here
      (`CONFIG_CHARGER_CPCAP=m`, no DT node).  Validated **3/3 clean dual-core
      boots** with the stock call restored (image `0x3547D613`, `#109 SMP`).
      (Session 67, report 265.)

340. **On the SMP image the second core took no userspace work: PID 1 was
      affinity-masked to `{0}`.**  `online`, `present` and `possible` all read
      `0-1`, `SMP: Brought up 1 node, 2 CPUs`, and `/sys/devices/system/cpu/
      cpu1/online` was 1 — but `taskset -p 1` read `1`, `nproc` read **1**, and
      every userspace process (init, OpenRC, dropbear, getty, the console shell)
      had `Cpus_allowed_list: 0`, while kernel threads had `0-1`.
      `taskset -c 1 <work>` always worked, so CPU 1 was functional and simply
      never scheduled userspace.  Reproduced on **3/3 fresh boots**; it is why
      report 263 §4b's "two cores in parallel" measured no scaling and why the
      UI/throughput numbers are single-core numbers.  **Workaround (verified,
      2 boots with it and 3 without):** on hd3's root, add to `/etc/inittab` before the
      OpenRC entries — `::sysinit:/usr/sbin/taskset -p 3 1` — which makes PID 1
      `{0,1}`; `nproc` then reads 2, a respawned shell is `0-1`, and a
      two-thread busy loop spreads load evenly (`cpu0` 2643 / `cpu1` 2622 user
      ticks).  It is a root-filesystem change, so the region-0 CRC is unchanged.
      **Not yet explained in the kernel:** `sched_init_smp()` widens init with
      `housekeeping_cpumask(HK_TYPE_DOMAIN)` (= `cpu_possible_mask` with no
      `isolcpus=`), and `local/kernel-tree/kernel/sched/{core,isolation}.c` are byte-identical
      to pristine — the suspicion is that `cpu_active_mask` has not yet gained
      CPU 1 when the call runs (the `CPUHP_AP_ACTIVE`/`sched_cpu_activate`
      state), clamping the mask to CPU 0.  A kernel-side fix is owed.  (Session
      67, report 266 §1.)

341. **A host WireGuard tunnel blackholes the RNDIS link, and it looks like a
      broken gadget.**  `ip rule` had
      `32765: not from all fwmark 0xca6c lookup 51820` with table 51820 =
      `default dev utun420` — a priority *before* `lookup main` — so every
      packet to `169.254.0.1` went into the tunnel.  Symptoms: `ping`/`ssh`
      time out, the host's `tx_packets` freezes, and **`tcpdump -i <rndis>`
      shows zero packets**, which reads exactly like a dead TX path; the
      device's `rx_packets` does not move either.  **The tell:** raw
      `arping -I <rndis> 169.254.0.1` still gets a reply (L2 bypasses routing),
      and `ip route get` shows the right device because the *main* table is
      fine — only `ip rule` is non-default.  Check `ip rule` / `ip route show
      table 51820` before touching the USB side; unbinding `rndis_host`,
      `authorized` 0/1 and `usbreset` merely reset counters and look like a
      partial fix.  (Session 67, report 267 §1.)

342. **`ufw`'s default-deny blocks the RNDIS link even with a correct route.**
      The host runs `ufw` with `default deny (incoming)` and no rule for the
      USB netdev, so SSH to `169.254.0.1:22` is dropped.  Add
      `ufw allow in on <iface>` and `ufw allow out on <iface>`; do **not** add a
      blanket inbound rule just to move a file (trap 291).  (Session 67,
      report 267 §1.)

343. **hd3's `epitaph` is the broken `r0` until the `r10005` APK is installed.**
      A reflash of the session-59-state hd3 image brings back
      `epitaph 1.4.0-r0`, which segfaults ~120 ms after `Started Epitaph` — the
      trap-292 symptom, unfixed.  Install
      `local/work/nura/work/packages/edge/armv7/epitaph-1.4.0-r10005.apk`
      (sha256 `aee3fdc8…`) with `apk add --allow-untrusted --force-overwrite`.
      The same image also lacks the session-60 llvmpipe `swrast_dri.so`: it
      reports `softpipe` and the file is the 6.9 MB amber build, not the 15.8 MB
      Alpine one.  (Session 67, report 267 §2.)

344. **Do not grep for platform/extension names with `^name$` anchors.**  A
      session-67 check of `libEGL_amber.so` used
      `strings … | grep -iE "^(wayland|gbm|x11|surfaceless|drm)$"` and concluded
      the library supported **only** `wayland`.  That is false: ELF `.rodata`
      holds long space-separated extension strings, so anchored patterns miss
      everything.  The real string is
      `… EGL_EXT_platform_wayland … EGL_MESA_platform_gbm … platform_x11 …
      platform_surfaceless … platform_device … EGL_WL_bind_wayland_display`.
      The wrong conclusion propagated into a report and a decision before it
      was caught.  **Grep for the bare substring, then read the whole line** —
      and prefer `nm`/`readelf` on the pulled file (the unit has no binutils, so
      `nm` there returns nothing and every count reads 0, another silent trap).
      (Session 67, reports 267/268.)

345. **A Wayland client gets the SGX only if the compositor serves `wl_drm`.**
      Mesa 21.3.9's Wayland EGL platform obtains the DRM fd **only** through
      `wl_drm` (a prereq at `platform_wayland.c:1567`); without it
      `dri2_initialize_wayland_drm()` fails and `eglInitialize()` silently
      retries with `ForceSoftware = TRUE`, loading `swrast_dri`.  So
      `MESA_LOADER_DRIVER_OVERRIDE=pvr` is powerless there (it is read only by
      `loader_get_driver_for_fd()`), and the symptom is `softpipe` + a first log
      line `MESA-LOADER: dlopen(…/swrast_dri.so)` with no `pvr` attempt.
      **catacomb (Smithay) serves no `wl_drm`** (its globals include `wl_shm` and
      `zwp_linux_dmabuf_v1`); wlroots (phoc), KWin and Mutter do.  Check a
      compositor with `WAYLAND_DEBUG=1` and look for a `wl_drm` global **before**
      blaming the driver or patching Mesa.  (Session 68, report 269.)

      **catacomb now serves a PRIME-only `wl_drm` (session 68): selection fixed,
      PRESENTATION OPEN.**  The patch (`src/protocols/wl_drm.rs`;
      `wl-drm-prime.patch`; package `1.5.0-r10002`) sends the render node +
      ARGB8888/XRGB8888 + `capabilities(PRIME)` and turns `create_prime_buffer`
      into a `Dmabuf`-backed `wl_buffer`.  Measured: `global(32, "wl_drm", 2)`;
      clients map `pvr_dri` and report `PowerVR SGX 540`; their linear `AR24`
      dmabufs import without error.  **But the client surfaces are black** (a
      `grim` capture shows only the compositor chrome), while `wl_shm` clients
      display.  Until the buffer presentation is fixed, `/etc/catacomb/initrc`
      unsets `MESA_LOADER_DRIVER_OVERRIDE` for the clients (backup
      `.session68-pvr`) and the compositor keeps the SGX.  [report 269
      §4c–§4f, DECISIONS §101]

346. **A `wl_drm` global makes clients SELECT the SGX, but their buffers can
      still present black — selection is not presentation.**  **Resolved in
      session 69 by traps 347/348** (the NPOT texture wrap and the panel's
      hard-coded GL locations).  The full text is in
      [`docs/history/KNOWN-ISSUES-HISTORY.md`](../history/KNOWN-ISSUES-HISTORY.md#trap-346--selection--presentation-resolved-session-69).

347. **An imported dma-buf texture samples opaque black if its wrap mode is the
      default `REPEAT` and the buffer is non-power-of-two.**  In GLES 2.0 an
      NPOT texture with `GL_REPEAT` is *incomplete*, so sampling returns
      `(0,0,0,1)` — no GL error, no import error.  Client buffers are NPOT
      (1024x20, 1024x585, 640x200, …), and **Smithay's
      `GlesRenderer::import_egl_image()` binds the EGLImage without setting
      `GL_TEXTURE_WRAP_S/T`**, while the shm/surface import paths in the same
      file do set `CLAMP_TO_EDGE`.  wlroots sets it too
      (`render/gles2/texture.c`), which is why the same client presents under
      phoc and is black under catacomb.  Measured A/B in the pvr driver with
      `tools/bench/egl/pb-gbmsample.c`: no clamp → `first=(0,0,0,255) nz=0`;
      with clamp → `first=(0,255,0,255) nz=64/64`.  Fix:
      `rootfs/catacomb/smithay-eglimage-clamp.patch` (catacomb `pkgrel=10003`).
      **Link EGL/GLES with `dlopen` when reproducing the compositor's path** —
      a binary that links `libEGL.so.1` directly hits `EGL_BAD_MATCH`/a segfault
      on a GBM-platform surfaceless `eglMakeCurrent` and its readback is
      meaningless.  (Session 69, report 269 §4g.)
348. **GLES2 does not fix attribute or uniform indices — the PowerVR linker
      assigns its own.**  epitaph hard-coded `0/1/2` for `glVertexAttribPointer`
      and `glUniform4f(0, …)` for its projection, which happened to match
      Mesa's softpipe but not `pvr_dri`: measured on the unit,
      `a_Position=2, a_UV=1, a_Flags=0, u_Projection=1, u_Texture=2`.  The
      result is *no geometry* (glyphs collapse to a point) plus
      `GL_INVALID_OPERATION` from `glUniform4f(0, …)`; nothing looks like an
      error at the API level.  A client that renders nothing but its clear
      colour is the symptom — check with a one-shot `glReadPixels` histogram of
      the client's own framebuffer before blaming the compositor or the driver.
      Fix: `glBindAttribLocation` before linking and `glGetUniformLocation` by
      name (epitaph `bind-attribs.patch`, `pkgrel=10008`).  (Session 69,
      report 269 §4h.)
349. **A pvr client fails on its second buffer because the OMAP4 TILER
      translation window is too full — an omapdrm scan-out capacity fault, not
      a pvr or compositor bug.**  **ROOT-CAUSED IN SESSION 70 (report 270).**
      The failure chain is `omap_gem_pin_tiler()` → `tiler_reserve_1d()` →
      `-ENOMEM`, logged `omapdrm omapdrm.0: could not remap: -12 (3)`, then
      `dma_buf_map_attachment failed: -12` → `PVRSRVMapDmaBufKM: Failed to get
      dma-buf phys addr` → the client's `CreateImageShared: Failed to create
      buffer`.  **`(3)` is `TILFMT_PAGE`** — the enum is `8BIT=0, 16BIT=1,
      32BIT=2, PAGE=3` — so these are the **linear** path
      (`tiler_reserve_1d(obj->size)`, charged **1:1**), and the GEM
      `OMAP_BO_TILED_MASK` bits are zero.  **Do not read it as tiling.**
      The TILER maps a fixed 128 MiB aperture (`0x60000000`,
      `2^14 × 2^13 = 2^27` B) that the display controller scans out from;
      OMAP4 shares **one** container between the 8/16/32-bit and paged views
      (`omap_dmm_tiler.c:933-935`).  Measured 10/10 on one boot: **59.4 MiB
      pinned → clean; 112.3 MiB → fail.**  Waiting does not free it (60 s with
      no client alive stays high); the freed object also trips
      `WARN_ON(pin_cnt > 0)` at `omap_gem.c:1209` from `dma_buf_release` on
      process exit.
      **The real multiplier is 8×, it is a PITCH-ALIGNMENT bug, and it is FIXED
      in a shim (report 271).**  `libdbm` describes a linear buffer as
      **`width = 1, height = <total bytes>, bpp = 8`**; omapdrm's
      `omap_gem_dumb_create()` calls `drm_mode_size_dumb(dev, args, SZ_8, 0)`,
      which aligns the **scanline pitch to 8 bytes**, so a 1-byte-wide row
      becomes an 8-byte pitch and every allocation is 8× too big
      (`width=1,height=2457600` → `pitch=8, size=19,660,800` for a
      2,457,600-byte frame).  **Fix:** re-express the same byte count as
      `width=<bytes>, height=1` — `tools/bench/gpu/dbmfix.c`, an `LD_PRELOAD`
      wrapper on `ioctl` (`libdbm` reaches the kernel via `drmIoctl`'s PLT, so no
      closed binary is modified).  **Measured working, compositor included:**
      deployed for the whole session (`/usr/local/lib/dbmfix.so` plus a
      `catacomb-dbmfix` exec wrapper, applied by `reboot -f` — zero DSI errors),
      the panel is **operator-confirmed correct** (wallpaper, keyboard, bar) and
      **8/8 re-rendering pvr clients are clean** with pinned TILER **flat at
      20.7 MiB**.  The idle desktop falls **59.4 → 14.0 MiB** and every GEM
      object in the tree is now **1.00×** (largest 2,457,600 B = one full frame,
      versus 19,660,800 before).  **Trap 349 no longer reproduces.**
      Still to do: package it as `pvr-dbmfix` and bake it into the hd3 rebake;
      the permanent fix belongs upstream (in `libdbm`/the DDK, or in omapdrm
      accepting a 1-wide buffer without pitch-alignment waste).
      **Measurement trap:** when fixed and unfixed runs are mixed, the *unfixed*
      ones leave 18,513,920 B objects behind and look like a leak — check object
      sizes, not just the total.  Toolchain:
      `https://musl.cc/arm-linux-musleabihf-cross.tgz` (the pmbootstrap build
      chroot is deleted after each build).
      (Reports 270, 271; probes in `tools/bench/gpu/`.)
      **catacomb 3/5, phoc 5/5 is a headroom difference, not a code-path one** —
      phoc idles at 39.8 MiB, catacomb at 59.4 MiB, and both release client
      buffers (15–17 per run).  **Retracted:** the "prefer LINEAR/non-tiled
      buffers" fix from the first draft of report 270 addresses nothing, because
      these buffers are already linear.  (Session 69 discovered it; session 70
      root-caused it — report 270 §0.1/§0.2.)
350. **Cosmetic pixel glitches on shm and dmabuf clients (observed on the
      working GPU desktop).**  The operator sees "slight pixel glitches" on the
      keyboard (a `wl_shm` client) and, at times, on the epitaph bar (a dma-buf
      client).  It is cosmetic and does not affect function.  **Prime suspect:
      the early "no `GL_EXT_unpack_subimage`" optimisation**, not the session-68
      fixes — the compositor uploads shm buffers per row and widens partial
      damage to full rows (`smithay-gles2-fallback.patch`), and damage tracking
      is shared with the dmabuf path.  That code dates from the renderer
      bring-up.  Decisive experiment: force the slow, per-row-only path (or run
      the clients on `wl_shm` with the old state) and see whether the glitches
      persist; if they do, the remaining candidate is catacomb's damage/repaint
      (its own `DamageBag` tracker + `CropRenderElement`), which is also
      pre-session-68.  Do **not** attribute it to the clamp fix: that touches
      only the dma-buf texture wrap and cannot alter pixels that are drawn.
      (Session 69, operator observation; not root-caused.)
351. **Never read "zero `wl_buffer.release`" from a run that died before its
      first swap.**  A client can fail in **two** places and both print
      `CreateImageShared: Failed to create buffer`: (a) at
      `eglCreateWindowSurface` (`context/surface failed 0x3009`,
      `EGL_BAD_ALLOC`) — **no buffer is ever created**, so of course no release
      arrives; and (b) after the first successful `eglSwapBuffers`, on the
      **second** buffer — which is the real second-buffer symptom.  Session 70
      first sampled mode (a) and concluded "catacomb sends zero releases,
      unlike phoc's 16–17", which pointed at a compositor buffer-lifetime bug.
      Re-measured on runs that reached the swap, catacomb sends **15–16**
      releases per run — the same order as phoc.  Always classify a run by
      whether it reached `swap=1` before drawing conclusions from its event
      trace.  (Session 70, report 270 §5.)
352. **`/sys/kernel/debug/dma_buf/bufinfo` blocks on this kernel, and
      `dri/*/mm` is not TILER occupancy.**  Reading `bufinfo` hangs the SSH
      session (it waits on a lock); do not put it in a command batch that other
      reads depend on.  `dri/omapdrm.0/mm` prints the **VMA** address space
      (`vm_addr_space_mm`), so it always shows `used 0 free <all>` and says
      nothing about the DMM container.  TILER occupancy must be summed from
      `dri/omapdrm.0/gem`: fields are `flags: name (refcount) offset dma_addr
      (pin_cnt) vaddr roll size`, so pin is field 6 and size is field 10 —
      parse by regex, not by positional `awk`, and the sum of `size` where
      `pin_cnt > 0` is the number that matters (report 270 §5).
353. **Restarting `tinydm` to switch compositors wedges the DSI link — it
      costs a reboot.**  Trap 289 said compositor restarts over a live link do
      not recover; session 70 confirmed it the hard way by switching
      catacomb↔phoc four times for a control: `DSI error: Failed to set
      dsi_if_enable to 0` plus `NO PACKET_SENT` on VC0 **and** VC1, then a
      black panel with the backlight lit while DISPC kept scanning
      (`LINE_STATUS` advancing) and catacomb's IPC still answered `dpms on`.
      The panel only came back on a clean `reboot -f` (no DSI error at all on
      that boot).  **Plan a compositor comparison as one switch per boot**, or
      accept a reboot per switch.  (Session 70, report 270 §6; trap 289.)
354. **`/tmp` is NOT tmpfs on the Nura/hd3 root — pushed probes survive a
      reboot.**  `mount` shows only `/dev/loop0 on / type ext4
      (rw,relatime,nobarrier)`; `/tmp` has no separate mount, so it is a
      directory on the 62.5 G ext4 (`df` reports the same 56.5 G for both).
      The handoff and session-69's note said "/tmp is tmpfs, re-push the
      probes"; that is true of the **region-1 BusyBox** root, not of hd3.
      Measured: `/tmp/pb-egltest` and `/tmp/pins.py` were still present
      across a `reboot -f` in session 70.  Do not assume either state —
      check `mount | grep ' /tmp '` on the root in front of you.  (Session 70.)

355. **An SSH timeout is not evidence of trap 337 until the cable is checked.**
      Mid session 70 the link timed out and I was about to record it as the
      intermittent VUSB teardown; the cause was that **the cable had been
      unplugged** (operator).  The host checks were clean (`ip rule` with no
      WireGuard rule, `enp8s0f3u1i2` holding `169.254.0.2`), and the link came
      back on replug.  A long `uptime` at the next connect does **not** prove the
      link survived a drop — it may just mean the session paused.  Check in this
      order: cable, then `ip rule`/route (trap 341), then `ufw` (trap 342), then
      trap 337.  (Session 70; the
      `docs/contradictions/usb-vusb-drop-idle-vs-intermittent.md` question stays
      open.)

356. **The baked hd3 image carried the ARMv6 module set — a rebake must replace
      `/lib/modules/7.3.3`, not only the userspace packages.**  Measured from
      the image's own files: its `pvrsrvkm_omap4_sgx540_120.ko` had vermagic
      `7.3.3 mod_unload modversions ARMv6 p2v8` and lived in a **flat**
      `/lib/modules/7.3.3/kernel/` with 44 `.ko`.  The running `#109 SMP` kernel
      is ARMv7 and the live root carries a **full 842-module tree** (856 files,
      28.1 MB) under `/lib/modules/7.3.3/kernel/...` with vermagic
      `7.3.3 SMP mod_unload modversions ARMv7 p2v8`.  The session-70 delta
      (catacomb, epitaph, inittab, pitch fix) was measured from
      `/lib/apk/db/installed` and files and **missed this**, so a rebake without
      the modules would boot and then fail every `modprobe` (`pvrsrvkm`, WLAN,
      BT, …).  `tools/rebake_hd3.py` now takes `--modules-dir` (default
      `local/work/rebake-modules/7.3.3`), replaces the whole tree, and verifies
      the pvrsrvkm vermagic and `modules.dep` after writing.  Pull the input with
      `tools/pbssh.sh 'tar -C /lib/modules -cf - 7.3.3'`.  (Session 71.)

357. **Do not install the catacomb APK's `/etc/catacomb/initrc` into hd3.**
      On the unit that file is owned by **`postmarketos-ui-catacomb`**, not by
      `catacomb`, and the image carries the bench-tuned pvr variant (md5
      `00fdff8481e7c4dde730221ef55d53da`: wvkbd, no swayidle, tabula,
      pb-touchwake, no `unset`).  The `catacomb-1.5.0-r10003.apk` **happens to
      carry a different default** (md5 `f829d28d…`: squeekboard/swayidle/bash),
      so `apk --upgrade` of that APK would both **file-conflict** with the UI
      package and **clobber the working initrc**.  Install only the binary and
      desktop files, leave the initrc alone, and splice the package records into
      `/lib/apk/db/installed` + `/etc/apk/world` (which carry apk's opaque `C:`
      identity hashes).  `tools/rebake_hd3.py` does exactly that and checks the
      initrc md5 after writing.  (Session 71.)

358. **`udevadm trigger` defaults to `--action=change`, so an `ACTION=="add"`
      rule silently does not fire.**  The backlight rule
      (`/etc/udev/rules.d/90-playbook-backlight.rules`) matches
      `SUBSYSTEM=="backlight", ACTION=="add"`; after installing it,
      `udevadm control --reload-rules && udevadm trigger --subsystem-match=backlight`
      leaves `/sys/class/backlight/*/brightness` at `root:root 0644`.  With
      `--action=add` the node becomes `root:video 0664` and `playbook` can write
      it.  The rule itself is right — the trigger command was wrong (the same
      mistake would hide any add-only rule).  The desktop permissions set is now
      baked by `tools/rebake_hd3.py`; `playbook` must also be a member of
      `video` (`addgroup playbook video`), or the udev group bit grants nothing.
      (Session 71; `rootfs/README-permissions.md`.)

359. **epitaph's Wi-Fi icon shows the CONNECTION state, not the radio's enabled
      bit.**  Toggling the radio (`nmcli radio wifi off/on`, or the bar's toggle)
      changes `WirelessEnabled` and the button's own pressed look, but leaves
      the icon unchanged — that is the icon's model, **not** a refresh fault and
      **not** a permission problem.  Measured on the flashed rebake: after the
      unit associated to `TeleCentro-AW-5G` (`wlan0` connected, DHCP
      `192.168.0.231`, gateway and `1.1.1.1` reachable, DNS resolving), **the
      icon changed**.  When the radio is on but the icon is dark, check
      `nmcli dev status` / `GENERAL.STATE` rather than polkit.  **An earlier
      reading of this as a client refresh defect is withdrawn** — the icon was
      simply showing "not connected".  (Session 71.)

360. **The GTK WebKit tarball contains no WPE port.**  Patching
      `webkitgtk-2.54.3.tar.xz` fails on `Source/WebKit/UIProcess/wpe/…`
      (the directory does not exist).  The WPE port is a separate release
      tarball: `https://wpewebkit.org/releases/wpewebkit-2.54.3.tar.xz`
      (49,591,296 B, sha512 `0c5d059b…`).  The fork's six commits were made
      directly on the 2.54.3 release commit (`c01fbd90`), so they apply cleanly
      to that tarball.  (Session 71; reports 273/274.)

361. **The WPE port's link line has no `-lintl`, and it needs
      `-DENABLE_JOURNALD_LOG=OFF`.**  The final `libWPEWebKit` link fails with
      `undefined symbol: libintl_bindtextdomain` /
      `libintl_bind_textdomain_codeset` (the GTK port links libintl; WPE's CMake
      does not).  Add `-DCMAKE_C_STANDARD_LIBRARIES=-lintl
      -DCMAKE_CXX_STANDARD_LIBRARIES=-lintl`.  Without
      `-DENABLE_JOURNALD_LOG=OFF` CMake stops at
      `libsystemd or libelogind are needed`.  (Session 71; report 274.)

362. **Alpine's `clang23` has `libclang-cpp`, not the C API `libclang.so`;
      bindgen needs the latter.**  `xkbcommon-sys`'s build script panics with
      `Unable to find libclang` even with `clang` installed.  `libclang.so` is
      provided by `clangNN-libclang` (16/18 are in edge; 23 is not).  Add
      `clang18-libclang` to makedepends and `LIBCLANG_PATH=/usr/lib`; bindgen
      finds `libclang.so.18.1`.  (Session 71; report 274.)

363. **pmbootstrap's `kill_sccache()` aborts the whole run under `--no-cross`.**
      It runs `sccache --stop-server` in the **native** chroot, but qemu-only
      builds install sccache in the armv7 buildroot, so the call exits 127 and
      the run fails even after a successful build.  A local patch
      (`pmb/chroot/shutdown.py`) tries the native and build chroots and never
      makes cleanup fatal.  Related: `CARGO_TARGET_DIR=/home/pmos/cache` is
      **inside** the zapped chroot and loses all compiled crates; use the
      persistent `/mnt/pmbootstrap/rust` mount (`work/cache_rust`, owned by
      uid 12345).  (Session 71; report 274.)

364. **Kumo 1.10.0 does not compile with the servo feature removed.**
      `src/config.rs` imports `crate::engine::servo::ServoState` unconditionally;
      `src/window.rs` imports `tracing::error` only under
      `cfg(all(feature = "servo", feature = "webkit"))`; and
      `src/engine/mod.rs` gates `AtomicU32` on `servo` although `FaviconId` uses
      it unconditionally.  The "both engines are properly cfg-gated" claim in
      HANDOFF-70 §0f.2 is **wrong** for this release.  Fix with
       `0002-webkit-only-cfg.patch` (make `AtomicU32`/`error` unconditional, gate
       the ServoState import/call).  (Session 71; report 274.)

365. **Kumo requires a DRM node path from its EGL display, so software GL panics
       before WebKit starts.**  `Kumo` does
       `egl_display.device().drm_device_node_path().expect("DRM node has no path")`
       (`src/main.rs:385`, glutin).  On this stack only the **pvr** driver yields
       a node path: with no `MESA_LOADER_DRIVER_OVERRIDE`, Mesa resolves the DRM
       name `omapdrm` and there is no `omapdrm_dri.so`, so EGL init fails and the
       device has no path; with `LIBGL_ALWAYS_SOFTWARE=1` or
       `MESA_LOADER_DRIVER_OVERRIDE=swrast` the software EGL device has no DRM
       node.  In both cases Kumo panics and WebKit never starts.  **A software
       proof needs a Kumo change (tolerate a missing/dummy node) and a working
       software GBM — which this Mesa lacks (`gbm: did not find extension
       DRI_DRI2` → `failed to load driver: swrast`).**  (Session 72, report 275.)

366. **The PowerVR GBM backend cannot export dmabufs, so WPE WebKit's accelerated
       surface cannot present — this is the wall for Kumo.**  Proven with
       `local/work/gbmplane.py` (ctypes, calls the same functions as
       `DMABufBufferAttributes::fromGBMBufferObject()`,
       `Source/WebCore/platform/graphics/DMABufBuffer.cpp:78`):
       on `/dev/dri/renderD129` `gbm_bo_create(AR24, …, GBM_BO_USE_RENDERING)`
       succeeds with `plane_count=1` but **`gbm_bo_get_fd_for_plane(bo,0)` and
       `gbm_bo_get_fd(bo)` both return `-1`**; on `/dev/dri/card1`
       `gbm_bo_create()` itself fails (`PVRDRIPlaneCreate: Cannot create display
       buffer without a display device`).  The omapdrm nodes export fine
       (`fd_for_plane0 = 6/7`).  WebKit 2.54's `AcceleratedSurface::create()`
       (`Source/WebKit/WebProcess/WebPage/CoordinatedGraphics/AcceleratedSurface.cpp:306`)
       requires a non-negative plane fd, so it logs `Failed to extract DMA-buf
       attributes from GBM buffer of size WxH` and the page stays **gray** —
       even though `kumo` and `WPEWebProcess` map `pvr_dri` (the engine *is* on
       the SGX).  **No `WPE_*` env fixes it:** `WPE_DRM_DEVICE` only moves the
       node (the pvr node gives garbage handles), and `WPE_BUFFER_FORMAT`
       (e.g. `AR24:0:rendering`) forces a linear buffer and removes the pvr
       *render* errors but `fromGBMBufferObject()` still calls the plane API.
       The rest of the desktop works because libdbm exports via PRIME
       (`drmPrimeHandleToFD`), not GBM.  **A WebKit-side PRIME fallback is
       impossible**: `tools/bench/gpu/gbmprime.c` shows
       `gbm_bo_get_handle_for_plane(0) = 0xFFFFFFFF` and `drmPrimeHandleToFD =
       -1` on the pvr GBM (only the omapdrm nodes export).  So the fix is a
       Mesa/GBM pvr backend that exports dmabufs, or the open `powervr_sgx` stack
       (SGX540 still wanted).  (Session 72,
       report 275 §2.)

367. **Stopping a running Kumo/WPE process wedges the pvr driver and freezes the
       panel (with 0 DSI errors); a reboot clears it, and running Kumo can drive
       GEM toward the TILER budget.**  After killing Kumo mid-render, dmesg
       showed `PVR_K:(Error): FreeResourceByPtr: ERROR calling FreeResource
       function`; the compositor and touch stayed alive (`S`, `wchan
       LinuxEventObjectWait`, IRQ 118 still counting, panel `connected`,
       backlight 255) but the image froze and new clients never appeared.  The
       format-forced run also pushed GEM to **109–113 MiB** of the 128 MiB TILER
       aperture.  Use a `reboot -f`, not a kill, to stop Kumo; a DSI
       `NO PACKET_SENT` wedge can follow a GPU wedge plus a power-button press
       and is cleared by a **second** `reboot -f` (trap-353 class).  (Session 72,
       report 275 §4.)

368. **`/dev/mem` `mmap()` requires a page-aligned offset — non-aligned reads fail
        with `EINVAL`, and the failure looks like a bad register.**  Reading the
        SGX identity with Python, `mmap.mmap(fd, size, offset=0x5600fe00)` raised
        `OSError: [Errno 22] Invalid argument`; the same offset at 0x56000000 (page
        aligned) worked.  The identity read itself was fine from the first attempt
        — only the **module** window at 0x5600fe00 failed, which a hasty reader
        could misreport as "the GPU window reads but the module does not".  Always
        round the offset **down** to 0x1000 and index the in-page bias;
        `tools/bench/gpu/sgxcoreid.py`'s `region()` does this.  `dd if=/dev/mem`
        and mmap both work here because `CONFIG_STRICT_DEVMEM` is **off**
        (`kernel/winchester-7.3.3.config`), which is what makes an agent-side
        register read possible at all.  (Session 73.)

369. **Read only the GPU registers the guide names — the SGX can hang the
        interconnect on an unnamed read.**  The SGX540 identity read used exactly
        the registers `docs/porting/kernel.md` §2 calls safe before the reset
        sequence (`CLKGATECTL`, `CLKGATESTATUS`, `SOFT_RESET`, `EVENT_STATUS`,
        `EVENT_HOST_ENABLE`, `BIF_CTRL`, `CORE_ID`, `CORE_REVISION`) plus
        `DESIGNER_REV_*` and the **read-only** `BIF_INT_STAT`; the write-only
        `*_CLEAR` registers were not read, and nothing past 0x0c08 was touched
        (fact sheet Gap 12).  All of them returned sane values and the DDK stayed
        bound with 0 DSI errors.  Do **not** turn this into a sweep: a read of an
        unnamed register on a gated unit can hang the bus, and the guide's SGX530
        section records a display-register sweep that hung a whole board.  (Session
        73; report 276 step 1.)

370. **The SGX540's errata and `USE_CODE_BASE` are not the SGX545's, and report
        276's summary of both was wrong.**  `sgxerrata.h` gives the SGX540
        **exactly three** BRNs at rev 120 (`26620`, `28011`, `34028`); the
        29954–31780 list report 276 §2 quoted is the **SGX543's** block.  And
        `sgx540defs.h`'s `USE_CODE_BASE` uses a **24-bit** address (`MASK
        0x00FFFFFF`) with the data-master at **bit 24** (`MASK 0x03000000`), while
        `sgx535defs.h`/`sgx545defs.h` use **25 bits** with DM at **bit 25** — so a
        540 entry must not copy the 545's `use_code_base_*_shift` values.  Whether
        the registers exist on rev 120 is still unmeasured.  (Session 73; report
        276 addendum, `docs/contradictions/sgx540-use-code-base.md`.)

371. **The open driver's core check is not "≥32 bits and ≥2 directory lists".**
        `pvrsgx_core_family_usable()` (`drm/powervr_sgx/pvrsgx_core.c`) accepts
        `va_bits` **28 or 32**, and `num_dir_lists` **exactly 1** for a core
        without `MULTIPLE_MEM_CONTEXTS`; it refuses a multi-context core with
        fewer than 2.  The SGX530 is already a **complete** 28-bit family entry
        (`sgx530_regs`, `sgx530_heaps`, `pvrsgx_cmd_core_sgx530`,
        `PVRSGX_QUIRK_BRN_28889`) and the single-context case is gated by
        `powervr_sgx.allow_unisolated=1`.  Report 276 §1/§2 called this "driver
        code, not a table row"; most of it has since landed upstream, so an
        SGX540 entry is **data plus a captured init script**.  Re-read the tree
        before quoting report 276's scoping.  (Session 73, upstream `95f78ca`.)

372. **The SGX540's `CLKGATECTL` and `BIF_INT_STAT` are *not* the SGX545's — a
        copied mask silently mis-gates the core.**  `EUR_CR_CLKGATECTL` on the
        SGX540 gates **IDXFIFO b16-17 and TA b18-19** as well as the ISP…PDS
        pairs, so the driver's `SGX_CLKG_ALL` mask must be `GENMASK(19, 0)`; the
        SGX545's `GENMASK(15, 0)` leaves the **TA unclock-gated**, and its
        `AUTO_MAN_REG` (b24) and `SYSTEM_CLKG` (b28) are single bits, not 2-bit
        fields, so they must stay out of the mask.  `EUR_CR_BIF_INT_STAT` has
        **no fault-type field** on the 540: fault requests are b0-13 and b14 is
        `PF_N_RW`; the SGX545's b0-15 / b16-18 do not apply.  Compare every field
        against `sgx540defs.h` before copying an SGX545 or SGX530 value.
        (Session 73; caught by the header comparison, before any hardware.)

373. **A heap layout that "passes" `heaps_fit()` can still overlap every other
        heap.**  `heaps_fit()` only checks each range is non-empty and inside
        `va_bits`, and `pds_reach()` checks two specific heaps; **neither checks
        overlap**.  The first `sgx540_heaps` gave `texture` `[0x08800000,
        0x10000000)`, which contains `params_3d`, `ta_data`, … right up to
        `usse_vertex`, and both driver checks still passed.  Two heaps covering
        one address lets two allocations collide, so check pairs explicitly —
        `tools/bench/gpu/sgx540-heaps-check.py` now does.  (Session 73.)

374. **The vendor DDK's `sgxconfig.h` is the sourced heap layout for a 28-bit
        core — do not derive the heaps from the SGX530's.**  Our DDK 1.17's
        `services4/srvkm/devices/sgx/sgxconfig.h` has an explicit
        `#if SGX_FEATURE_ADDRESS_SPACE_SIZE == 28` block giving every heap base
        (general `0x00001000`, PDS pixel `0x0dc00000`, kernel code `0x0e400000`,
        PDS vertex `0x0e800000`, kernel data `0x0f000000`, USSE pixel
        `0x0f400000`, USSE vertex `0x0fc00000`).  It has **no texture heap** on a
        28-bit core (textures come from general), which is why `sgx530_heaps`
        carves one from the top of general; the split point is ours, not the
        DDK's.  The header is in the vendored tarball, so extract it rather than
        guessing.  (Session 73.)

375. **KUnit-building this driver needs the DRM helpers the bench kernel never
        enabled, and `obj-.../` in `drivers/gpu/drm/Makefile`.**  Our
        `local/kernel-tree` config lacks `DRM_GPUVM`/`DRM_SCHED`/
        `DRM_GEM_SHMEM_HELPER`, so an out-of-tree `make M=... modules` compiles
        every object and then fails at **modpost** with `symbol
        'drm_gpuvm_exec_lock' undefined` and friends — that is a config gap, not
        a code error.  For the KUnit run, `drivers/gpu/drm/Kconfig` **and**
        `Makefile` must both mention `powervr_sgx` (the Makefile line is easy to
        miss: without it the build says "ready" and produces no objects), the
        driver's `pvrsgx_cmd_defs.c`/`.h` are **symlinks into `docs/uapi/cmd/`**
        so a `cp` that dereferences them or an `rm` in the wrong tree deletes the
        real file, and a stale in-tree `.o` set makes `ar` fail on absolute
        paths.  Clean `O=` plus the in-tree sync is the reliable sequence.
        (Session 73; `drm/ci/omap3630-kernel.sh` is the upstream's own script for
        this.)

376. **`USE_CODE_BASE` is named in `sgx540defs.h` but is NOT on the guide's
        safe-to-read list — do not probe it from userspace.**  Trap 369 said
        "read only the registers the guide names", then this session read
        `USE_CODE_BASE[0..15]` (`0x0a0c + 4n`) anyway, because the header names
        them (`EUR_CR_USE_CODE_BASE`).  **Named-in-a-header is not the same test
        as safe-to-read.**  The upstream guide's list for a core in any state is
        short and specific — `CLKGATECTL`, `CLKGATESTATUS`, `SOFT_RESET`,
        `EVENT_STATUS`, `EVENT_HOST_ENABLE`, `BIF_CTRL`, `CORE_ID`,
        `CORE_REVISION` — and it says "some others hang the bus until the reset
        sequence has run".  The identity probe earlier in the same session, which
        stayed inside that list, ran 20 reads with no effect.

        **What is *not* established: that this read caused the wedge.**  The read
        timed out and the RNDIS link, ACM console and UI all died, which is why it
        is now off-limits — but a wedge also happens **spontaneously on some
        boots** (trap 377): the very next boot wedged at t=28.8 s with nothing
        read at all, and session 72 saw the same right after a reboot.  Record
        this as **unproven-but-unnecessary risk**, not as the proven cause.
        Settle Gap 7 from inside the open driver's reset sequence (write, read
        back, restore) in a boot from RAM.
        (Session 73; operator correction.)

377. **The hardware buttons are not wired up at all, and the black panel is a
        boot-time DSI wedge that happens spontaneously — not a button effect.**
        Two separate corrections, both from the operator in session 73.

        **(a) No button works.**  The OS does not register the power,
        volume-up/down or pause/resume buttons; no work has ever been done to
        bring them up, and a power-button *hold* reset is the hardware's own.  So
        the initrc's `keyboard` bindings (lines 18–24) can never fire, and the
        "power-button release toggles DPMS" explanation that sessions 59, 70, 71
        and 72 all reached for **cannot be true**.  Treat it as retired folklore.

        **(b) The wedge is spontaneous, at boot, and reproducible.**  On the
        session-73 boots the log shows the failure at a *fixed* point in early
        boot: `Failed to set dsi_if_enable to 0` (t≈28.8 s), then
        `pb hs-clock: null packet on VC0/HS … polls=2000 (NO_PACKET_SENT)` and
        `VC1/LP … polls=2000`, then `DSI VC(1) error, vc irqstatus 8` (t≈30–31 s),
        and a TCON re-send that does not recover it.  Compare the healthy boot:
        the same null packets return **`polls=1`** with `irqstatus=0x104`.  So the
        discriminator is **`polls=2000` (exhausted) vs `polls=1` (immediate)**,
        and the operator reports it happening right after a reboot in session 72
        too.  It is **not** caused by anything read or written that boot.

        **What this means for diagnosis and recovery:** read `dmesg` for
        `NO_PACKET_SENT`/`dsi_if_enable` — never infer the panel state from a
        screenshot.  **`grim` captures the compositor's buffers, not the panel**,
        so a screenshot of a healthy desktop coexists with a black panel (this
        session did exactly that).  Recovery is a `reboot -f` (trap 289: do not
        iterate compositor restarts over a wedged link); it usually comes back
        clean, and if it does not, reboot again.  The open question — why the
        first modeset after a boot fails on some boots — belongs to the display
        work, not to the GPU port.  (Session 73; operator correction.)

378. **A kernel must be built from `local/kernel-tree`, never from
        `local/pristine/linux-7.3.3` — the pristine tree has no PlayBook support
        and the unit will not boot.**  Session 73 wanted an out-of-tree `O=`
        build, and the built `local/kernel-tree` is dirty so `O=` refuses it;
        `local/pristine/linux-7.3.3` accepted it, the driver compiled cleanly and
        the image wrapped, flashed and **failed to boot — the unit fell back to
        bootrom**.  The reason: the board support exists **only in
        `local/kernel-tree`** (the applied patch).  Missing from pristine:
        `drivers/misc/pbmon.c`, `drivers/input/touchscreen/playbook-munin.c`,
        `sound/soc/ti/playbook-wm8994.c`, `arch/arm/boot/dts/ti/omap/
        omap4-winchester.dts` (so no DTB for this board, no display bring-up, no
        DSI-init TCON hook).  **Check before wrapping any image:**

        ```sh
        for f in drivers/misc/pbmon.c arch/arm/boot/dts/ti/omap/omap4-winchester.dts; do
            test -e $TREE/$f || echo "NOT A PLAYBOOK TREE: $TREE"; done
        ```

        A clean `O=` build is only possible from a **patched** copy of pristine
        (apply `kernel/patches/0001-playbook-winchester-7.3.3.patch` first, plus
        the vendored SGX driver).  Region 0 was restored by re-wrapping the
        `0x3547D613` image and re-flashing; the unit came back with 0 DSI errors.
        (Session 73.)

379. **`CONFIG_SGX=n` does not stop the closed DDK — hd3's `/etc/modules` loads
        `pvrsrvkm` as a module and it wins the GPU probe race.**  Building the
        open-driver kernel with the closed DDK off (`CONFIG_SGX` unset) removes only
        the *built-in* copy.  The unit's root has `pvrsrvkm_omap4_sgx540_120` in
        `/etc/modules`, it probes `56000000.gpu` at boot too, and on both
        session-73 boots it won:

        ```
        /sys/bus/platform/devices/56000000.gpu/driver -> .../drivers/pvrsrvkm
        /sys/class/drm/card1/device/uevent            -> DRIVER=pvrsrvkm
        /sys/bus/platform/drivers/powervr_sgx/        -> (no device)
        ```

        `powervr_sgx` still probed, read the core ID and logged
        `TI OMAP4430 (SGX540 1.2.0)`, then failed on its missing firmware — but it
        never owned the device.  **To actually give the open driver the GPU**, stop
        the closed one from loading: `modprobe.blacklist=pvrsrvkm_omap4_sgx540_120`
        on the kernel command line (or remove it from `/etc/modules` on hd3).  Two
        drivers that both match the same DT node are a race, not a preference —
        check `/sys/bus/platform/devices/56000000.gpu/driver` before concluding
        anything about which one is bound.  (Session 73;
        `docs/captures/sgx540-bind-2026-11-26.txt`.)

380. **The GPU firmware must live in the INITRAMFS: the probe runs before hd3 is
        mounted.**  `powervr_sgx` probes `56000000.gpu` at t≈0.88 s, while the root
        is still the initramfs; hd3 (with its `/lib/firmware`) is mounted later.
        A microkernel placed only on the real root gives
        `Direct firmware load ... failed with error -2`.  Put it in
        `initramfs/root/lib/firmware/powervr_sgx/` and rebuild the image; the error
        then changes (in session 73, to "not a microkernel file", which is progress).
        `initramfs/root/lib/firmware/` is gitignored as a built artifact.
        (Session 73.)

        **Corrected in session 74: the t≈0.9 s figure is a *different* boot's.**
        With the driver built as `=m` (`CONFIG_DRM_POWERVR_SGX=m`) and nothing
        bound in the initramfs, the firmware is requested when the **module**
        loads, which on this root is **after** hd3 is mounted:

        ```
        [ 1.923889] EXT4-fs (loop0): mounted filesystem … r/w      <- hd3 up
        [ 6.914581] powervr_sgx 56000000.gpu: … microkernel         <- probe
        ```

        and the file the driver actually read was the **raw 14,536-byte** image
        left at hd3's `/lib/firmware/powervr_sgx/`, not the wrapped one in the
        initramfs: moving it aside made the load fail with
        `Direct firmware load … failed with error -2`, which proves which copy was
        in use.  So in *this* configuration the **root's** `/lib/firmware` is the
        effective one and the initramfs copy is inert.  The trap stands for a
        **built-in** driver or one loaded before the root is up; the rule that
        avoids both is: **keep the two copies identical and never let a stale one
        linger** — a stale root copy is what made the corrected image look
        broken for a whole boot cycle.  `open microkernel <build id>` in `dmesg`
        names the build that was actually loaded.  (Session 74; report 277 §1,
        `docs/captures/sgx540-ukernel-start-2026-11-26.txt`.)

381. **`modprobe.blacklist=` does not stop OpenRC loading a module by name.**  The
        kernel parameter only suppresses *alias-based* autoloading;
        `/etc/init.d/modules` reads `/etc/modules` and runs
        `modprobe <exact-name>`, which bypasses it — `pvrsrvkm` still loaded and
        took the GPU.  To actually block it, use a modprobe.d entry with
        `install <mod> /bin/false` (a plain `blacklist` line is not enough for an
        exact-name load either), or drop it from `/etc/modules`.  Verify with
        `modprobe -n -v <mod>`: it must print the `/bin/false` line, not an
        `insmod`.  (Session 73.)

382. **The open microkernel image is built for `srvinit`, not `powervr_sgx`, and a
        single-context core needs a PD-table patch it does not have.**  `mkukfw.py`
        (the wrapper `powervr_sgx` requires) rejects an image that is not
        `SGX5`/`UKRN`-headered, and for a one-context core it requires
        `SINGLE_CONTEXT` set, `SECURE` clear **and** a `pdtable` patch: the driver
        allocates a PD table at `kernel_code.base + 0xf000` and the microkernel
        must load directory list 0 from it.  `ukernel/ukernel.py` cannot — it
        routes TA/3D to **list 1** through `BIF_BANK0` and assumes list 0 is
        `srvinit`'s shared kernel PD ("under `powervr_sgx` list 0 is an empty idle
        PD and the transfer BIF-faults on a client address").  Treat "build an
        SGX540 microkernel" as: add the `pdtable` patch site and load list 0 from
        it, set the single-context feature bits, and re-audit the ~39 references
        to `list 1`/`BANK0`/`0xc78` — not as a `cores/sgx540.py` data file.
        (Session 73.)

383. **Never unbind `pvrsrvkm` from `56000000.gpu` at runtime — it oopses, and the
        rebind deadlocks.**  Freeing the GPU for another driver by writing the
        device name to `/sys/bus/platform/drivers/pvrsrvkm/unbind` crashed the
        closed DDK:

        ```
        Internal error: Oops - undefined instruction: 0 [#2] SMP ARM
        PC is at PVRLinuxFenceDeInit+0x50/0x68 [pvrsrvkm_omap4_sgx540_120]
        ... PVRLinuxFenceDeInit <- PVRCore_Cleanup <- pvr_drm_unload <- pvr_remove
        ```

        The device was left unbound, and the follow-up write to
        `/sys/bus/platform/drivers/powervr_sgx/bind` then hung in D state inside
        `device_driver_attach` (`/proc/<pid>/wchan`), which only a reboot cleared.
        To give the open driver the GPU, **block `pvrsrvkm` at boot** (a modprobe.d
        `install … /bin/false`, trap 381) and boot the open-driver image — do not
        unbind the closed driver live.  Note the asymmetry that makes this
        tempting: **loading** `pvrsrvkm` at runtime is harmless and was used to
        restore the bench.  (Session 73.)

384. **`/dev/mem` cannot reach the GPU once `powervr_sgx` owns it — the userspace
         register read that opened this port is unavailable in the open-driver
         configuration.**  `mmap`/`read` at `0x56000000` returns
         `[Errno 13] Permission denied`, and the RAM and control windows refuse
         the same way.  It is **not** a devmem hardening option:
         `CONFIG_STRICT_DEVMEM` is off and `IO_STRICT_DEVMEM` is not in the
         config at all.  The cause is the driver's own reservation —
         `grep 56000000 /proc/iomem` prints
         `56000000-56003fff : 56000000.gpu powervr_sgx`.  Session 73's identity
         read was necessarily taken with the closed `pvrsrvkm` bound, and that is
         the only configuration in which it works.  **Consequence:** any further
         register evidence has to come from *inside* the driver (a debugfs node, a
         module parameter, a print), not from a userspace peek.  Do not spend a
         boot rediscovering this.  (Session 74; report 277 §7.)

385. **The open SGX540 microkernel is started by `edm_init=1`, and the image alone
         is not enough.**  With the corrected image the probe reaches
         `open microkernel …, features 0x8` and then, 20 ms later, fails:
         `microkernel did not start (EDM launch faulted: BIF int 0xc002 fault
         0x8779000, event status 0x0)` / `probe … failed with error -110`.  With
         `modprobe powervr_sgx edm_init=1` it completes (`EDM init: event status
         0xa0000000, BIF int 0x8000`, `microkernel started`).  That is the
         driver's own `pvrsgx_edm_init`, documented as "the §9 bare-DOUTU launch
         folded into the real init" for **single-context cores** — a supported
         path, not a workaround.  `modprobe -r` cannot unload the module while
         anything holds `card1` (`elogind` does, from session start), so an image
         or module change costs a reboot.  (Session 74; report 277 §1.)

386. **The control block's free words differ by feature flag: an instrumentation
         store can silently land on a word the image already uses.**
         The first kernel-CCB trace written this session used `0x3a`/`0x3b`
         (which `UK_WAITLOG` writes) and `0x3c` (the cleanup force-complete
         marker, written unconditionally on that path), and its reading was
         meaningless — it reported "no wait spinning" for a build where that could
         not be known.  Before instrumenting the control block, diff the candidate
         words against **every** definition in `ukernel/cores/<core>.py` *and* the
         image's optional diagnostic flags; `0x39`/`0x3a`/`0x3b`/`0x1c` were the
         genuinely free ones.  The same class of mistake as trap 368 (an off-by-one
         reader) and as report 271's "log the arguments, not the result".
         (Session 74.)

387. **The image's `0xb14` "keep-alive" is not an EDM progress register on this
         core.**  `ukernel/` advances `0xb14`/`0xb20` each timer task, inherited
         from the SGX545; on the SGX540 the GPL-2 DDK 1.8 header names
         `0xb14` `EUR_CR_USE1_SERV_PIXEL` — a USSE **pixel service queue**
         (`COUNT` b0-15, `EMPTY` b16) — and `0xb20` `EUR_CR_USE_FILTER2_LEFT`.
         So a stall diagnosed as "the keep-alive stopped" is not thereby a stall
         of the EDM, and the write may be consuming a queue rather than ticking a
         clock.  Likewise `0xa74` is `EUR_CR_DMS_CTRL` (its *value* is right —
         `0x208200` = 32 EDM / 32 vertex / 32 pixel tasks; see
         `docs/reference/sgx540-ddk18-facts.md` §3) and `0xa78` is
         `EUR_CR_DMS_AGE`.  Header:
         `/home/psyden/Pdrive/GFX_Linux_DDK-master/eurasia/hwdefs/sgx540defs.h`.
         (Session 74.)

388. **A NULL job that prints `1/1 passed` here is the driver's 2 s timeout — the
          fence is signalled *without* an error, so `syncobj_wait` returns 0.**
          The test's syncobj is bound to **`drm_sched`'s** `s_fence->finished`
          (`drm_syncobj_replace_fence(sig->syncobj, &job->base.s_fence->finished)`),
          not to `job->hw_fence`.  `pvrsgx_timedout_job()` sets `-ETIME` on
          `job->hw_fence` and resets, then `drm_sched_start(sched, 0)` re-adds a
          callback on the *parent* fence and finishes it with `errno = 0`
          (`drm_sched_job_done_cb` passes `f->error`, the parent's), and
          `drm_syncobj_array_wait_timeout()` never inspects the error — it returns
          0 as soon as `dma_fence_is_signaled()`.  Both 2000 ms, so the race is
          invisible.  Evidence: the two "passing" runs read `2016806` and
          `2020499` µs against `job_timeout_ms = 2000`, with the context then
          marked `GUILTY` (so an immediate second run reads `0/1`), and six
          back-to-back jobs gave six resets.  **Never read `1/1 passed` as "the
          microkernel did something" in this configuration** — check
          `/sys/kernel/debug/dri/1/status`'s `resets` (it rises) and the
          control-block `kick` counters (they do not).  `pvrsgx_job.c`,
          `drm_sched_stop`/`drm_sched_start` (`sched_main.c`),
          `drm_syncobj.c`.  (Session 75; report 278, capture
          `sgx540-ukernel-kick-counter-2026-11-26.txt`.)

389. **`consumed` is an artefact after a GPU reset — `consumed == issued`, so the
          `consumed 2..5` in report 277 measured nothing.**  `pvrsgx_fw_start()`
          calls `pvrsgx_ccb_reset()`, which sets `ccb WO := 0`, `RO := 0` and
          `ccb_reset_seq := ccb_issued`; `pvrsgx_ccb_consumed()` then returns
          `ccb_issued - min(pending, issued - reset_seq)`, i.e. `issued` when the
          ring is empty.  An idle boot reads `ccb wo 0 ro 0 issued 1 consumed 1`.
          While a job is stuck, the kernel side shows `wo 1 ro 0` and the verdict
          is correctly `consumed 8` — again the pre-job value.  **Read the ring
          (`wo`/`ro` in `status`, and the microkernel's own `UK_CCBLOG` words
          0x39/0x3a), never `consumed`, as "commands the microkernel took".**
          (Session 75; report 278.)

390. **The build id in `dmesg` names what was really loaded; the file on hd3 may
          already be a different build.**  The whole of session 75 ran on
          `open microkernel ccb2` (the session-74 `UK_CCBLOG` trace build, md5
          `7056fc74…`) while hd3's `/lib/firmware/powervr_sgx/sgx540_ukernel.bin`
          had been switched to `invfix` (md5 `12f12abb…`) afterwards without a
          reboot — the firmware is read only at module load, so the swap was
          inert until the next boot.  This is trap 380's "never let a stale copy
          linger" from the other direction: the **handoff's record** of which
          build is loaded can be stale even when the file is current.  Always
          confirm with `dmesg | grep 'open microkernel'` before attributing a
          result to a build.  (Session 75.)

391. **hd3's dropbear has no `scp` and no `sftp-server`, so `tools/pbscp.sh`
          cannot push a file to this root.**  Both backends fail
          (`/usr/libexec/sftp-server: not found`, then `scp: not found`), so a
          firmware push silently leaves the old file in place — verify with
          `md5sum` before rebooting, or the boot tests the previous image.  Use
          the ssh stream instead:
          `cat FILE | tools/pbssh.sh 'cat > /path'`.  `pbscp.sh`'s "DIRECTORIES
          NEED -O" note is about the QNX door; this root has no `scp` at all.
          (Session 75.)

392. **The SGX540 has FOUR kick registers, and which one is the doorbell is a
          feature flag — "the init kick and the job kick use different offsets"
          is the vendor's own shape, not a bug.**  `sgx540defs.h` defines
          `EVENT_KICK1` 0x0ab0, `EVENT_KICK2` 0x0ac0, `EVENT_KICK` 0x0ac8 and
          `EVENT_KICK3` 0x0ad8.  The DDK's kernel driver writes the init kick to
          `EVENT_KICK2` when `SGX_FEATURE_MULTI_EVENT_KICK` is set (else
          `EVENT_KICK`), and the per-command kick to `EVENT_KICK` (or to
          `EVENT_KICK2` under `FIX_HW_BRN_26620 && SYSTEM_CACHE`); the multi-event
          path does **not** increment the kernel CCB's event-kicker word.  So
          `pvrsgx_fw_start()` → 0x0ac0 and `ccb_write()` → 0x0ac8 is correct for
          this core, and `pvrsgx_regs.h`'s `EVENT_KICK2 = 0x0ae8` is only ever
          used by cores that set `event_kick2` on the *SGX545*'s routing.
          **Recorded because session 75 first read this as a defect and had to
          retract it** — check a differing constant against the vendor's call
          sites before calling it one.  (Session 75; contradiction file
          `sgx540-event-kick2-offset.md`, now resolved.)

393. **A read-back that passes proves less than it looks, when both sides are
          computed from the same assumption.**  `check_event_pds()` compares
          `EUR_CR_EVENT_OTHER_PDS_EXEC` (0x0a68) against
          `event_pds_addr()` = `fw.addr[PDS0] & ~0xf`, minus `pds_pixel.base`
          when `EDM_VERTEX_PDSADDR_FULL_RANGE` is clear.  That is real evidence
          the write landed on an implemented register, but the *value* is ours on
          both sides: the *expected* address is derived from our heap layout and
          compared with what the same write put back.  It cannot detect "the PDS
          program is in the wrong heap".  Compare the derivation against the
          vendor's own (`srvinit`, which subtracts `sDevVAddrPDSExecBase` for
          exactly this core) rather than against itself.  (Session 75.)

394. **A counter read through `ctlblk` is a STATIC snapshot — it can never show a
        1 kHz event, so "the word stayed 0" from that path says nothing about
         whether a periodic task ran.**  `ctlblk` prints the microkernel's
         control block through a normal memory read; the session-75 instrument
         stored its counters there and concluded "the event program is never
         re-entered" from words that could not have moved either way for a
         timer.  The fix is a **dynamic** counter: increment a value in the
         microkernel's own memory and have the host read it twice.  With one
         (`UK_TICKLOG`, control-block `0x2c`/`0x2d`) the answer was immediate —
         **0 after 12 s idle**, i.e. the timer branch never runs at all
         (capture `sgx540-tick-counter-2026-11-26.txt`).  Note the conclusion it
         replaced (`kick` never entered) still stands: that one *is* valid from a
         static word, because it is a count of entries, not of time.
         (Session 75.)

395. **RETRACTED — `HOSTCTL_UK_TIMER_CLOCK` = 307 200 is CORRECT, and the EDM
          timer does fire.**  I first wrote that the driver's period was 1000× the
          DDK's because it stores 307 200 where I believed the DDK stores 307.
          **That compared a count against a rate and was wrong.**
          `ui32uKernelTimerClock` is a count of *core-clock cycles*, so
          307 200 / 307 200 000 Hz = **1.000 ms = 1 kHz**, exactly the request;
          and the DDK's own expression is the same quotient the driver computes
          (`ui32CoreClockSpeed / ui32uKernelFreq` = 307200000/1000 = 307 200).
          Measured afterwards: `EUR_CR_EVENT_TIMER` (0x0acc) reads
          **`0x010228fe`** with `ENABLE` b24 set, and five samples two seconds
          apart all sit inside `0..0x04B000` — the counter is running and
          reloading.  **Lesson:** a "N× wrong" comparison needs both sides in the
          same units; check whether the field is a count or a rate before calling
          it a factor.  (Session 75; capture
          `sgx540-dispatch-regs-2026-11-26.txt`.)

396. **With `edm_init=1` the HOST writes `ui32InitStatus` itself, so "microkernel
         started" in `dmesg` is not the microkernel's report.**  The branch does
         `msleep(20)`, checks `BIF_INT_STAT` for a fault, then writes
         `INIT_COMPLETE`; the regular path polls for the microkernel to set it.
         The image's `init_set` *also* writes it.  Use the independent evidence
         instead — and there is plenty: `SC_VERSION` (control block 0x18 =
         `0x00010000`, a microkernel-only store) and `EVENT_HOST_ENABLE` b29
         (`0x0130` = `0x20004000`, ORed in by `init_set` after the timer write)
         both prove the init task ran to its end.  (Session 75.)

397. **`EVENT_PDS_ENABLE` (0x0a58) bit 14 (`SW_EVENT`) is NOT IMPLEMENTED on
         this core — and session 74's `SGX540_CR_EVENT_PDS_ENABLE_ALL` is
         therefore a no-op.**  `sgx540defs.h:2043` names
         `EUR_CR_EVENT_PDS_ENABLE_SW_EVENT_MASK 0x4000`, and session 74 changed
         the entry's value to `0x2204f00f` to add it, presenting that as one of
         four corrections that made the microkernel "start".  A per-bit
         write/read-back sweep settles it: writing **each bit of the word
         alone**, every bit sticks **except 14**, which returns 0 even on its
         own — so it is absent, not masked.  The driver's value therefore
         behaves identically to the SGX545 constant it replaced.  **`SW_EVENT`
         does work in the other register** — `EVENT_HOST_ENABLE` (0x130) reads
         `0x20004000`, bit 14 set — so the field is real, just not a *PDS
         dispatch source* here.  **Do not "fix" a missing PDS bit again without
         the sweep: a header naming a mask is not evidence that the register
          holds it** (the same lesson as trap 392, one level down).  (Session 75;
          capture `sgx540-pds-enable-sweep-2026-11-26.txt`.)

398. **The "panel goes black on fast bar taps" is catacomb's double-tap SLEEP
          gesture, and the black that will not come back is the trap-298 DSI
          resume wedge.**  catacomb's `on_double_tap()`
          (`src/windows/mod.rs`) splits the bottom gesture handle into
          40% / 20% / 40%; a double-tap in the **centre 20%** calls
          `set_display_status(false)`, while `src/input.rs` calls
          `set_display_status(true)` on **every** double-tap — so a double-tap in
          the notch does ON then OFF.  A double-tap needs two taps within
          **300 ms** and ~44 px (`MAX_DOUBLE_TAP_DURATION` /
          `MAX_DOUBLE_TAP_DISTANCE`), which is exactly why ~1 s taps do nothing
          and ~0.2–0.5 s taps trigger it.  Measured with kprobes on
          `drm_panel_disable`/`_enable`/`bl_power_store` (session 76): a fast-tap
          burst gave **4 disable + 4 enable, all `comm="catacomb"`**, and one
          cycle was `enable @196.06` then `disable @196.09` — **34 ms apart**.
          The panel disable/enable is `backlight_disable`/`enable`, which is why
          `bl_power` flips 0↔4 while the DRM connector's `dpms` stays `On` (a
          real `catacomb msg dpms off` moves **both**: `bl_power=4` *and*
          `dpms=Off`).  That sub-frame ON→OFF tears the DSI down while it is
          re-initialising (~2.5 s) — the trap-298 wedge: backlight returns,
          picture dead, only `reboot -f` clears it.  **Fix (applied session 76):**
          `catacomb msg gesture-handle --height 0` in `/etc/catacomb/initrc`
          removes the notch zone, because `ui_rect(false)` subtracts the handle
          height so `on_double_tap`'s `point.y < bottom` early-return always
          fires — measured **0** disable/enable afterwards, panel stayed lit.
          Trade-off: the handle's window-cycle gestures are lost.  **Do NOT read
          this as a rendering race** — swapping softpipe for llvmpipe made the
          desktop snappier but did not stop the blanking, which is what pointed
          at the gesture.  The operator also saw it from the top bar; the
          `on_double_tap` y-guard should reject above-handle taps, so re-check
          the top bar after this fix rather than assuming a second vector.
          **This guard is TEMPORARY, not the fix** (DECISIONS §114): it deletes
          a real UI affordance to dodge the defect.  The fix is one guarded line
          in catacomb — `input.rs` wakes on *every* double-tap, so it should be
          `Some(TouchAction::DoubleTap) if !self.display_on =>` — and then the
          handle height can come back.  Still unexplained: 20 IPC
          `dpms on/off` cycles did **not** wedge, while the in-gesture ON→OFF
          pair does.  (Session 76.)

399. **The SGX540 microkernel is in the DDK 1.8 drop as SOURCE and as a PREBUILT
          BINARY — and the vendor's event path is a PDS dispatcher over
          `ir0`/`ir1`, not a poll of `EUR_CR_EVENT_STATUS`.**  HANDOFF §5
          predicted the source might answer the PDS-exec question; it does, and
          it reframes the remaining work.  `services4/srvinit/devices/sgx/
          eventhandler.pds.asm` is the program `EVENT_PDS_EXEC` points at: it
          declares six `data dword INPUT_*` constants, starts
          `movs douta, ir0, INPUT_IR0_PA_DEST`, branches on those bits, and ends
          `movs doutu, INPUT_GENERIC_DOUTU0/1/2` + `halt` — a pure dispatcher
          into the USSE `EventHandler`, which also reads `PA(ui32PDSIn0/1)`.
          **`EURASIA_PDS_IR0_EDM_EVENT_TIMER` is bit 1
          (`hwdefs/sgxdefs.h:3270`), not `EUR_CR_EVENT_STATUS` bit 29.**  The
          constants are patched at load (`sgxinit.c:2507-2510`,
          `PDSUKERNEL_EVENTSSetINPUT_*`) and the generated
          `eventhandler_pds.h` gives the exact words (IR0 `[17]`, IR1 `[20]`,
          timestamp `[16]`, DOUTU1 `[1],[5],[9],[13]`, DOUTU2 `[29]`, generic
          DOUTU0 `[12]`, 3D `[0]`, TA `[4]`, SPM `[8]`; segment 144 B).
          **This port's loader patches only `ctlblk` and `pdtable`, so no PDS
          input routing exists at all** — a concrete candidate for "entered once,
          never again".  Also in the drop: a **complete prebuilt vendor 540
          microkernel**, `binary2_540_omap4430_android_{debug,release}/…
          /ukernel/ukernel.bin` (22 192 B `cfc577aa…` / 19 984 B `ff69e400…`),
          plus `ukernel_labels.h`'s per-command entry table and the 2D/3D/SPM/TA
          splits; it decodes as SGX540 code with this repo's disassembler.  **It
          is REFERENCE evidence, NOT a deployment target** — operator direction,
          session 76: the open driver compiles its own microkernel, because ours
          can be cleaner and the vendor's 2014 blob predates what modern Mesa
          needs; reading their source is explicitly fine.  Use it as the
          cross-check for our own image, never as a replacement for it.  (It
          already cleared one suspicion: rev 120 defines no
          `FIX_HW_BRN_29104`, so the vendor also ends a task with a bare
          `mov.end` and no `phas`, exactly as our `a.end()` does — not a
          difference, do not "fix" it.)  **Do not "fix" `event_pds_data0` to the
          vendor's 9 on this basis** either: 144 B is the dispatcher's segment,
          ours is a 4-word stub whose 16 B is genuinely 1 (session 76 retracted
          exactly that in place).
          (Session 76; capture `sgx540-vendor-ukernel-source-2026-11-26.txt`.)

400. **RETRACTED — "the SGX540 event entry is fetched EXACTLY ONCE".  The counter
          was reading a control-block word the microkernel itself clears.**  This
          trap first recorded an unconditional entry counter (before the
          `ui32InitStatus` test) reading **1** at uptime 44 s and again at 140 s,
          concluded "the program is not re-fetched at all", and closed the whole
          handler-side axis on it.  **All of that is withdrawn.**  The cause is
          trap 403: the counter constant was written `0x07 * 4` and consumed as a
          **word index**, so it sat on word **0x1c = `CLEANUP_DEFER_CT`**, which
          `init_set` clears and the cleanup path writes twice more.  Caught when
          the `rearm2` build booted with the counter at **0** while
          `SC_VERSION = 0x10000` proved init had run — impossible for a program
          that executes.  **With the counter on word 0x07 (firmware `fix1`,
          `f468a71d…`) the event program is fetched TWICE:** `entry = 2`,
          `tick (0x2c) = 1`, `SC_VERSION = 0x10000`, stable over 94 s and
          **unchanged across eight host kicks**.  So there is one init pass and
          one dispatch pass, and the dispatch pass is the timer's.  **Do not
          re-derive "how many times is it fetched" from this trap's original
          text — and treat session 75's "entered once" as likewise unproven, since
          it sampled the same class of word.**  What *does* survive here:
          an entry-top store is safe **when it is ctlblk-relative** (`r17` is
          valid from `a.patch(17,"ctlblk")`); only stores through the derived
          `sa` attributes faulted there (session 74); and a store's offset field
          is narrow (the assembler rejects large ones — word 0x3f needs the
          `limm`+`imae` rebase `program()` uses).  (Session 76; capture
          `sgx540-entry-counter-2026-11-26.txt`.)

401. **`EUR_CR_DMS_CTRL` was never written: the EDM ran on its reset default of
          32 tasks where the vendor sets 63 for BRN-23632.**  Measured with a new
          `status` line: live `0x00208200` (EDM 32 / vertex 32 / pixel 32) before,
          `0x003f8200` after the fix — i.e. **EDM 63**, the vendor's launch value
          from `sgxinit.c:4492`.  Decode with the SGX540's masks: b16-21 EDM,
          b10-15 vertex, b4-9 pixel (`sgx540defs.h:2209-2216`; **pixel is shift
          4, not 5** — I mis-read it once).  `FIX_HW_BRN_23632` **is** in the
          rev-120 errata and the vendor's comment is *"At least 33 EDM tasks is
          needed to fix BRN23632"*.  Fixed per-core in `pvrsgx_fw.c` (`.dms_ctrl`,
          0 on the SGX545/530 so their behaviour is unchanged).  **This is a real
          defect and it did NOT change the symptom** — the entry counter stayed 1
          — so do not re-run it expecting a dispatch.  Related, unmeasured: the
          vendor also writes `EUR_CR_DMS_AGE = ENABLE | PVRSRV_SGX_MAX_DMS_AGE`,
          and **`PVRSRV_SGX_MAX_DMS_AGE` is not defined anywhere in the DDK 1.8
          drop**, so that value cannot be derived from it.  (Session 76; capture
          `sgx540-entry-counter-2026-11-26.txt`.)

402. **The host side is configured as well as the vendor's public source can say —
          and a PDS-task emit from the USSE task end does NOT re-fetch either.**
          Confirmed good by measurement or derivation: `EVENT_PDS_ENABLE` (TIMER
          set), `EVENT_PDS_EXEC` (resolves onto the PDS0 stub), `EVENT_PDS_DATA`,
          `EVENT_PDS_INFO`, `EUR_CR_PDS`, `CLKGATECTL` (BRN-30852),
          `EVENT_HOST_ENABLE`, the OCP wrapper gate, and the DMS task counts.
          (The counts this trap first quoted were taken on the bad word — see
          trap 400; the real figure is `entry = 2, tick = 1`.)  So: **do not keep
          tuning host registers.**
          **The emit is encodable — that was established here and stands:**
          `sgx-asm` takes input on **stdin with `-b`** (exactly as
          `ukernel/build.py` calls it), and

              echo 'emit.t2 r0, r2, r4 [f12 f13 imm=3138]' | sgx-asm -b -c sgx540
              -> 00800104 fb20b188   rc=0

          is **byte-identical to the vendor's own instruction** and round-trips.
          (The earlier claim that "this toolchain cannot encode that mnemonic" was
          my own invocation error, withdrawn in place.)
          **Then it was tried, and it does not work:** `UK_REARM` puts that exact
          instruction — with its resolved payload `imm=0x843` and its two
          loader-patched operands — on `tmr_tick_end`, the one dispatch pass that
          runs, excluding `init_set` by a per-call-site flag.  Measured: **the
          one-shot flag goes 1 → 0, so the emit executed, and `entry`/`tick` do
          not move** (control `fix1`, same instrument without the emit, reads
          identically).  Also measured on the way: **emitting from `init_set`'s
          task end faults the launch outright** — `BIF int 0xe000 fault 0xfef4000`,
          probe `-5`, no `card1` — so the EDM must not be re-emitted while it is
          starting.  **Next is trap 404.**  (Session 76; capture
          `sgx540-entry-counter-2026-11-26.txt`.)

403. **A control-block word is only "free" if no store anywhere touches it — and
          this cost the session three conclusions and two boots.**  The entry
          counter was placed on word **0x1c** because its constant read
          `0x07 * 4` and `ld.a.d`/`st.a.d` take a **WORD index**: 0x1c is
          `CLEANUP_DEFER_CT` (`cores/sgx540.py:65`), which `init_set` clears and
          the cleanup path writes twice more, so the count was read out of a cell
          the microkernel zeroes in the same pass.  The same unit error appeared
          three times in the session: this one, a `relaunch` debugfs node reading
          `ctl[0x1c]` (which read 0 and nearly concluded "the launch never ran"
          — **and whose first repair changed the comment, not the index**, so it
          was briefly documented as fixed when it was not; it reads `ctl[0x07]`
          now and prints 2), and an init gate written `SA_HOSTCTL * 4` (word 4,
          an unrelated word).  **Do this before writing any control-block store:**
          (1) list every constant in `ukernel/cores/<core>.py` that is reached
          through `r17`, and every `st.a.d/ld.a.d rN, r17, #…` in `ukernel.py`,
          and pick a word on neither; (2) **read an existing, working use of the
          same constant in the same file and copy its form exactly** — every other
          `SA_HOSTCTL` use says `#{SA_HOSTCTL}`, unscaled, which is what makes the
          odd one out findable; (3) verify by parsing the `ctlblk` dump **by word
          index**, not by trusting the numeral.  Measured free on this core:
          **0x07** (used by `UK_ENTRYLOG`) and 0x3f.  Not free: 0x1c.  A store
          whose offset is past ~0x1f also needs the `limm`+`imae` rebase that
          `program()` documents.  (Session 76; capture
          `sgx540-entry-counter-2026-11-26.txt`.)

404. **The whole re-emit line is BLOCKED on something simpler: an idle boot never
          reaches a second task end, so there is nothing to re-emit and nothing to
          measure.**  With the emit correctly placed on `tmr_tick_end`, correctly
          gated, and provably firing (trap 402), `REARM flag` still reads **1**
          after **eight host kicks** — the one-shot was never consumed, so **no
          `a.end()` executed at all** during them.  `init_set`'s end is the only
          task end that ever runs, and it is (correctly) excluded.  A first
          attempt gated the emit on a runtime `ui32InitStatus` test, which is also
          wrong: the host writes `INIT_COMPLETE` ~20 ms *after* the kick
          (`pvrsgx_fw.c:775`), so a runtime gate can exclude the only pass there
          is.  **So the next question is not "who re-emits" but "does the kick path
          reach the microkernel at all".**  The `kick`/empty-CCB counters
          (0x3d/0x3e) read 0 in every build, including ones where the path
          reportedly ran, so they cannot be trusted as they stand — re-instrument
          the `kick` entry on a word verified per trap 403, and read `ctlblk`
          before and after a job.  Interpretation is in HANDOFF §0b.  (Session
          76; capture `sgx540-entry-counter-2026-11-26.txt`.)

405. **`build.py`'s core token is consumed only when `SGX_CORE` is UNSET, so
          the documented gate command silently overwrites the image with a
          manifest.**  `_select_core()` returned early if `SGX_CORE` was in the
          environment, before deleting the leading bare token from `sys.argv`.
          The token therefore stayed as `argv[1]`, `resolve()` read *it* as the
          **output** path and the real `.bin` as the **manifest** path: the image
          was written to `./sgx545` and the JSON manifest **over**
          `sgx545_ukernel.bin`.  Then `test_emu.py`'s `load()` read the JSON as
          code and died with `undecodable 726f6322 20200a7b` — ASCII `"cor"` /
          `" {\n"`.  So `SGX_CORE=sgx540 SGX_ASM=… python3 ukernel/test_emu.py`
          (HANDOFF §3, the command run every session) **corrupts the sgx545
          reference image and reports a failure that looks like a firmware
          bug.**  Fixed in two steps, because the first fix was wrong too: the
          token is now always consumed, and a conflicting `SGX_CORE` is a hard
          error (`build.py: SGX_CORE='sgx540' contradicts the 'sgx545' token`)
          — `setdefault` let the ambient env silently beat the explicit token,
          so `SGX_CORE=sgx540` built the *sgx540* config into
          `sgx545_ukernel.bin`.  `test_emu.py` now honours `SGX_CORE` when no
          positional core is given, so the documented command tests the core it
          names.  Detect the corruption with `head -c 4
          ukernel/<core>_ukernel.bin` — it must be `00000000`, never
          `7b0a2020`.  The images are `*.bin` and gitignored, so nothing in git
          was ever wrong; the *workdir* was.  (Session 76 wrap-up re-read.)

## 3. Corrections to older reports

These corrections are preserved deliberately. Each is newer than the text it
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
differ (`CFG_GRP` `0x1c` vs `0x01`, `CFG_TRANS` `0x55` vs `0x05`)".  The
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

**3.27 "The reference clocks its McBSP through the ABE" is wrong; the port is
a codec-master consumer and the ABE serial port is a firmware data path.**
Report 212 §16 concluded that the reference's McBSP is "clocked through the
ABE" because `SRGR2` is `FPER | 0x1000` and slave mode starts no SRG.  The
consumer half is right; the clock source is not the ABE.  The reference's
codec is AIF1 master (`R0x302 = 0x4000`), all four McBSP clock pads are
inputs (`0x0100`) while it plays, AIF2 is a slave (`R0x304 = 0x0040`), and the
driver's `StereoAudioCodecRequestMcbspClk`/`RequestABEClk` are stubs.  The ABE
serial port is a firmware *data* path (`abe_connect_serial_port` writes the AE
port table/ATC descriptor; `abe_init_atc`'s serial case is a no-op), and the
McBSP's clock hardware is CM1_ABE/McBSP, which mainline drives.  Source:
report 213, DECISIONS §58, traps 201–203.

**3.28 The board's speakers are on the codec's line outputs, not the class-D
"speaker" path.**  The session-46 handoff and STATE described the speaker as
`SPKMIX`/`R0x036`, and read `SPKL/SPKR DAC1 Switch` = 1 as "routed".  Those
controls belong to the class-D block, which the reference does not use: while
it plays through the speakers `R0x036 = 0`, `PM1 = 0x0013` (no `SPKOUT`), and
the route is `DAC1 -> MIXOUT -> LINEOUT1P/LINEOUT2P` with `PM3 = 0x3cf0`,
`R0x01e = 0`, `R0x034/0x035 = 1` and `R0x038 = 0x0080`.  The board enables are
`spk` (GPIO2_12, **active low**) and GPIO1_15 (the `audio_boost` enable,
**high**).  Source: report 215, trap 210, DECISIONS §60.

**3.29 `pmic_twl6030_cfg` *does* enable VMMC and *does* write regulator groups.**
Report 219 §Q3 concluded the boot-image PMIC configurator "never touches
VMMC/VPP/VUSIM enables" and "never sets a regulator GROUP".  A second pass with
a mechanical `movw`/`movt` + call-site sweep (report 226, corroborated by report
221 §3) shows **six** GROUP writes (all `0x01` = `P1_GRP_6030`, at `0x84`,
`0x88`, `0x8C`, `0x98`, `0xAD`, `0xB0`) and a VMMC enable at `0x98`–`0x9B`
(`GRP 01`, `TRANS 03`, `STATE 21`, `VOLT 15` → **3.0 V**).  VPP `0x9C`–`0x9F` and
VUSIM `0xA4`–`0xA7` genuinely are untouched, so half the old statement stands.
**It is still not the WLAN's gate**, and that is measured rather than argued:
**VMMC's STATE register reads `0x00` on TabletOS too**, with `ti0` up — the
regulator resmgr turns it back off before `wifi.sh` — and MMC5's supply is the
fixed 1.8 V `vwl1283` in our design, not VMMC.  (Session 49, reports 221/226.)

**3.30 The TWL6030 register at `0x8C`–`0x8E` is VAUX3, not `CLK32KG`.**
Report 192 §7 (and §3.21 above) label that triple `CLK32KG`, taking the base from
`drivers/clk/clk-twl.c`.  That table is `twl6032_clks[]` — a **TWL6032**-only
table — and `TWL6030_REG_CLK32KG` is module index 48 in the PM slave-misc block,
a different address entirely.  In the TWL6030 map `0x84`/`0x88`/`0x8C` are
VAUX1/2/3, confirmed by the QNX regulator resmgr's own 20-record name table
(report 221 §1).  Consequence: the "`CLK32KG` is APP-ON in both OSes" conclusion
was about **VAUX3**, and the 32 kHz to the WL1283 has **never actually been
verified** — the local read of `0x60` (`0x55`, i.e. fully on) is the first
candidate but its identity is not nailed down.  Do not cite `0x8C` as `CLK32KG`.
(Session 49, reports 219 §4.2, 221/226.)

**3.31 `CLK32KG` is physical `0xBC` (module-relative `0x8C`), and it was OFF —
which is why the WL1283 never enumerated.**  §3.30 above and the earlier text
mixed two address spaces.  `twl_i2c_read/write_u8(TWL_MODULE_PM_RECEIVER, reg)`
adds the `PM_RECEIVER` base **`0x30`**, so a module-relative `reg` lands at
physical `0x30 + reg`.  `drivers/clk/clk-twl.c` registers `clk32kg` at
module-relative `0x8C` (GRP/TRANS/STATE `0x8C`/`0x8D`/`0x8E`), i.e. **physical
`0xBC`/`0xBD`/`0xBE`**, and its probe uses that table for the TWL6030 too.
Physical `0x8C` is VAUX3 (`twl6030-regulator.c` offset `0x5C` + `0x30`) — that
is what report 192 read as "CLK32KG", so its dismissal of the FreeBSD `0xBC`
claim was backwards.  §3.30's "module index 48" (= `0x60`) is VCORE3's state,
not a clock.  **The measured state:** Linux boots `0xBC..0xBE = 07/01/00`
(OFF) and the reference runs `07/05/55` (ON).  Enabling it
(`0xBE = 0xE1`, `clk-twl`'s prepare value) makes the WL1283 enumerate;
`CONFIG_CLK_TWL=y` + an enable in `clk-twl`'s probe is the permanent fix.
(Session 50, report 230; supersedes the session-50 text that called `0x60` the
candidate.)

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

* **The hd3 flash destination is loader block `0x4915`, not `0x4905`.**  The MCT
  type-`0x1C` entry at `0x4905` has a 1 MiB prefix, so raw `0x49000000` is
  `(0x4915-0x15)*0x10000`.  The session-54 flash at `0x4905` was rejected by the
  loader gate (`0xF7 seq 4 -> cmd 0x15 data 03`, nothing written); `0x4915`
  wrote 626 065 440 B and verified sha256 `076bafcf…`.  This corrects the
  session-53 handoff/ROADMAP and report 239/240 wording; `SAFETY.md` §4 already
  said `0x4915`.  (Report 241, trap 259.)

**3.32 Report 198's "no inversion" Y orientation does not hold on the current
display pipeline.**  Report 198 §1/§3 read the vendor calibration as axis-
aligned/increasing and observed `ABS_Y = 0` at the top edge.  Session 56's
controlled top/bottom edge capture reads the opposite: physical top →
evdev `Y 577–591`, physical bottom → `Y 35–51` (X is correct).  The driver now
inverts the mapped Y (`panel_h - 1 - munin_map(...)`, kernel `#69`).  The
calibration *values* are unchanged; only the axis direction was wrong on this
pipeline.  Report 246 §4.

**3.33 Report 264 stopped one level short: the "stale TWL6030 comparator" is a
freed `devm` object from a failed probe, not corruption in place.**  Report
264 §2/§3 correctly identified `phy->comparator->set_vbus` as the faulting
transfer, but read the `kmalloc` address as a live object that had been
corrupted.  Session 67's static audit (report 265) shows the object was
**freed**: `twl6030_usb_probe()` stored `&twl->comparator` in the PHY before
`twl6030_usb_ldo_init()`, and that function collapsed every `regulator_get()`
error to `-ENODEV`, so a deferred `vusb` failed the probe permanently and
devres freed `twl`.  The faulting `r0` being a `kmalloc-192` devm allocation
(offset 64) with a heap pointer in word 0 is the evidence; no in-place writer
exists.  Report 265 supersedes 264 §5's "audit the lifetime" with the answer.

**3.34 Report 263's "no `cpufreq` policy exists" is wrong: `cpufreq-dt` is bound
and scales.**  Report 263 §3/§4 recorded no cpufreq policy and left the MPU at
the loader's 400 MHz.  Session 67's live read (report 266 §2) shows
`scaling_driver = cpufreq-dt`, OPPs `300000 600000 800000 1008000`, governor
`ondemand`, and `scaling_max_freq` drops the clock and restoring it returns
1008000.  What is actually missing is the **CPU supply** (`vdd_mpu`), so the OPP
voltages are not applied.  Reports 266/HANDOFF/STATE are current; see traps
312 (resolved) and 340.

**3.35 Report 263 §4b's "two cores in parallel: no scaling" and its UI-latency
figures were single-core measurements.**  Session 67 found PID 1 affinity-masked
to `{0}`, so every userspace process — including the benchmark and the UI —
ran on CPU 0 only, while `online` read `0-1` (report 266 §1, trap 340).  The
throughput and latency numbers must be re-measured on two real cores before they
are used; they are not evidence against dual-core scaling.

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
