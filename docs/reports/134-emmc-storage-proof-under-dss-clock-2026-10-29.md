# 134 — The eMMC under the DSS clock regime: a byte-exact read-back proof, and the real media offset

**Session 16 (absolute), Linux-era session 10.  2026-10-29.**
Image: **`0xC07AB9FA`** (`local/images/kernel-0xC07AB9FA-emmcproof.img`) — the
`0x8C146805` kernel plus the console's new read-only `blk`/`grep` commands.
Raw data: `docs/reports/data/emmc-proof-0xC07AB9FA-2026-10-29.txt`.

This session answered the handoff's first question: **did the DSS clock fix
disturb the eMMC?**  It did not — proven by reading back, byte-exactly, the
11 MB image that was just written, and by matching a pre-flash TabletOS capture
of static media.  The session also found that the project's media offsets were
wrong by 1.3125 MiB: the MCT/loader's 64 KiB block numbers are not raw device
offsets.  That correction matters for the root filesystem, and it is the main
new fact in this report.

---

## 1. Summary

| question | answer |
|---|---|
| Does the eMMC still work under the new clock regime? | **Yes.** A freshly written 11 152 064-byte image reads back with the identical FNV-1a64, and a static region matches a pre-flash TabletOS capture. |
| Why was the eMMC expected to be safe? | The MMC2 functional clock is `func_96m_fclk` = **96 MHz**, which descends from DPLL_PER **M2** (192 MHz) — the output the DSS fix did not move. Measured in `/sys/kernel/debug/clk/clk_summary`, not inferred. |
| Is `mmcblk1` the same device QNX calls `hd0`? | **Yes**, byte for byte: LBA 0 hashes identically to the TabletOS capture of `hd0` offset 0. |
| Where does the flashed image actually live? | Raw offset **`0x8100000`**, not `0x8250000`. The MCT/loader 64 KiB block `N` maps to raw `(N - 0x15) * 0x10000`. |
| Is there room in region 0 for a Linux root? | **No.** The 11 MB image fills the whole IFS zone (`0x8100000..0x8C00000`, before the RFS table). |
| Where can a root go without an MCT edit? | **Region 1**: raw `0x28000000..0x47F00000`, 512 MB, already partitioned and empty (its first sector is a QNX boot sector with an unused `t179`). |

---

## 2. Method (read-only)

No eMMC write was made from Linux.  The bench work was:

1. On TabletOS, capture two static references **before** flashing:
   media LBA 0 (1 MiB) and region 0's sector 0 (64 KiB, `hd0` offset
   `0x8000000`).
2. Build `0xC07AB9FA` with two new console commands:
   * `blk info` / `blk sum <lba> <sectors>` / `blk sumb <byteoff> <len>` /
     `blk rd <lba> [sectors]` — `O_RDONLY` on `/dev/mmcblk1`, FNV-1a64 over the
     bytes; there is deliberately **no** write op (`SAFETY.md`), and
   * `grep <substr> <path>` — whole-file substring filter, because `cat` stops
     at 4096 bytes and `/sys/kernel/debug/clk/clk_summary` does not fit.
3. Flash it through the documented region-0 loop (171 blocks, container
   11 272 232 B), boot, and hash:
   * the image bytes at their media offset,
   * the whole padded payload range,
   * the static references.
4. Read the clock tree and the display regression check.

The console's own transcript of every command is in the DRAM ring, as usual.

---

## 3. The proof

FNV-1a64 (`h = 0xcbf29ce484222325`, `h ^= b`, `h *= 0x100000001b3`) is computed
host-side over the exact bytes and compared with the device's value.

| what | host value | device value | verdict |
|---|---|---|---|
| image bytes, `boot/chainloader/kernel.img` (11 152 064 B) at raw `0x8100000` | `8308f0ff29452390` | `8308f0ff29452390` | **MATCH** |
| payload padded to `0xAB0000` (`blk sum 0x40800 21888`) | `8615f45509bff390` | `8615f45509bff390` | **MATCH** |
| region-0 sector 0 (static, captured on TabletOS before the flash) | `c4feeffb5dbcdfb0` | `c4feeffb5dbcdfb0` (`blk sum 0x40000 128`) | **MATCH** |
| media LBA 0, 64 KiB (static, captured on TabletOS) | `41b9c0a35a996242` | `41b9c0a35a996242` (`blk sum 0x0 128`) | **MATCH** |

The image is written by the flash path and read back by Linux: any read-side
corruption (or a write-side error under the new clock) would break the first
two rows.  The last two rows are independent of our write and prove that
*pre-existing* media reads correctly.  A 640 MiB-offset read (region 1) also
succeeds, so high LBAs are fine.

One operational detail the read-back settled: **the loader strips the
mfcp/qcfp container header**.  The destination block starts with the image's
first byte (`fe 01 00 ea …`), not with the 0x10020-byte template header.  The
flasher's own log ("block 0x825 … 171 blocks") is about the loader's
coordinates, not the raw device's.

---

## 4. The clock evidence

From `/sys/kernel/debug/clk/clk_summary` on `0xC07AB9FA`:

```
dpll_per_ck              192000000
dpll_per_x2_ck           384000000
dpll_per_m2x2_ck         192000000   <- DPLL_PER M2, the output the DSS fix did not move
func_96m_fclk             96000000   <- dpll_per_m2x2 / 2
func_64m_fclk             64000000   <- dpll_per_m4x2 / 2
l3-init-clkctrl:0010:0    96000000   consumer 480b4000.mmc fck   <- MMC2 functional clock
dpll_per_m5x2_ck          96000000   <- the DSS fck
l3-dss-clkctrl:0000:8     96000000   consumers 58000000.dss fck, 58004000.encoder fck
```

The MMC2 functional clock reads **96 MHz**, which matches the `func_96m_fclk`
branch (M2-derived) and not `func_64m_fclk` (M4-derived, 64 MHz, and M4 did move
with the DSS fix).  The mux selection bit itself was **not** read; the rate and
the unchanged M2 output are the evidence.  So the handoff's reasoning — "eMMC is
on M2, which did not move" — is supported, and the read-back proof is the
end-to-end confirmation.  The `kmsg mmc` log shows the reads as
`CMD18` at the expected LBAs; the only noise is the known `PBREQ`/`PBIRQ`
bring-up instrumentation in `omap_hsmmc.c` (`KNOWN-ISSUES.md` §4).

`fb 00ff00` painted the framebuffer green and the operator confirmed a full
green panel: the display is unaffected by the new userspace.

---

## 5. The correction: the MCT/loader block numbers are not raw offsets

The canonical docs (and this project's own logs) have always said "block `0x825`
= media `0x8250000`".  The device says otherwise.  Five independent points fit
one rule:

```
raw (mmcblk1 = hd0) byte offset = (loader_block - 0x15) * 0x10000
```

| point | loader block | predicted raw | observed |
|---|---|---|---|
| media LBA 0 | — | `0x0` | same byte as TabletOS `hd0` offset 0 |
| region 0 sector 0 | `0x815` | `0x8000000` | hashes identically to the TabletOS capture |
| the flashed image | `0x825` | `0x8100000` | image bytes hash-identical |
| region-0 RFS table | `0x8D5` | `0x8C00000` | QNX boot sector + partition table |
| region 1 start | `0x2815` | `0x28000000` | QNX boot sector + partition table |

`0x15` blocks = `0x150000` = 1 310 720 B = 1.3125 MiB.  The offset is
consistent with a boot-ROM-reserved prefix in front of the area the OS sees as
`hd0`/`mmcblk1`; the MCT's own low entries (`0x18` Bootrom `5..0xC`, …) refer
to that prefix and must not be interpreted with the shifted rule.  What matters
operationally:

* **the flash loop is unaffected** — the flasher and the loader both speak
  loader-block coordinates, so `0x825` is still correct for a flash;
* **every raw-offset computation is affected** — the IFS is at `0x8100000`, the
  RFS table at `0x8C00000`, region 1 at `0x28000000`, and the FS partition at
  **`0x49000000`** (measured in session 17 by writing to `/dev/hd3` and reading
  it back; 1 MiB above the rule's `0x48F00000`, report 138).

The MBR TLV range (`0x805..0x814` → raw `0x7F00000..0x8000000`) reads all zero
on this unit; the RFS and region 1 areas carry the QNX partition tables.

---

## 6. What this means for the root filesystem

> **Update, report 135 (newer):** the `hd3` shrink was attempted and
> reverted — the recreated filesystem made the unit boot as a new install, and
> the Linux eMMC **write** path was found to be unusable for large writes.
> Read report 135 before acting on this section's `hd3` option; the region-1
> facts below are unchanged.

* **Region 0 cannot hold it.**  The IFS zone is `0x8100000..0x8C00000` (11.25 MB)
  and the current image is 11.15 MB.  The builder's `MAX_END` already enforces
  this.
* **Region 1 is the only pre-partitioned free space**: raw
  `0x28000000..0x47F00000`, 512 MB, with an unused `t179` at region-1 offset
  `0xC00000`.  It is inside the loader's allowed OS range (`0x1B`
  `0x815..0x4804`), so an `0xF7` write needs **no MCT edit** and cannot touch
  TabletOS, NVRAM or RPMB.  512 MB is enough for a minimal Linux root and
  modules.
* The `hd3` root is 58 GB with 2.3 GB used; shrinking it would free 50+ GB but
  requires an MCT rewrite (a documented brick path), recreating the live
  TabletOS root (which the sealed IFS does **not** back up), and a backup
  procedure we do not have.  Recorded as the long-term option, not the next step.
* A Linux root in region 1 would be mounted with a byte offset
  (`0x28000000`) on `/dev/mmcblk1`; the 4 KiB/512 B offset arithmetic must come
  from Python, never from mental arithmetic (operator rule, this session).

---

## 7. Bench cycle

| # | action | result |
|---|---|---|
| — | TabletOS up, door up | `0fca:8010`; captures of media LBA 0 and `hd0` `0x8000000` |
| 1 | flash `0xC07AB9FA` (171 blocks at loader block `0x825`) | 689 frames, loader reset, done |
| 2 | boot Linux, chmod ttyACM0 | console up |
| 3 | `blk sum 0/a80/40000/40a80 128` | mapping settled: `hd0` == `mmcblk1` |
| 4 | `blk sumb 0x8100000 <len>` and `blk sum 0x40800 21888` | both match the host image/payload |
| 5 | `grep mmc|dpll_per|func_*|dss clk_summary` | clock table in §4 |
| 6 | `blk rd 0x140000`, `blk rd 0x46000` | region 1 and RFS tables at the derived offsets |
| 7 | `fb 00ff00` | full green panel (operator) |

No eMMC write from Linux, no NVRAM/RPMB access, no DSS/DSI register probing.
The sealed IFS was restored at session close and verified: sha256
`c72b0530…`, `os_type` 6, `BOOTCTR_FCTRY = 0x32`, TabletOS up.

---

## 8. Tooling and tree changes

* `initramfs/console.c`: new `blk info|sum|sumb|rd` (FNV-1a64, read-only) and
  `grep <substr> <path>`; the stale "180 s boot lifetime / reboots at 180 s"
  banner text (removed in session 15) is corrected.
* Image `0xC07AB9FA` (`kernel-0xC07AB9FA-emmcproof.img`), 11 152 064 B.
* No kernel or DTB change: the image is the `0x8C146805` kernel plus the new
  initramfs binary.  The DSS clock cleanup is **not** in this image (deferred by
  the operator).
* New evidence file `docs/reports/data/emmc-proof-0xC07AB9FA-2026-10-29.txt`.

## 9. Residual uncertainty

* Whether the 1.3125 MiB prefix exists in the physical eMMC or only in the
  loader's coordinate system is **not** measured; only the OS-visible mapping is.
  It does not affect any current procedure.
* The MBR TLV range reads zero.  Whether that is normal or specific to this unit
  is unknown; nothing reads it.
* The `PBREQ`/`PBIRQ` prints remain in the shipped patch (`KNOWN-ISSUES.md` §4).
