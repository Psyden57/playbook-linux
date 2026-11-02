# 136 — The Linux eMMC write path, measured: the driver is fine, the writer was not

**Session 17 (absolute), Linux-era session 11.  2026-10-30.**
Image: **`0xEB617C60`** (`local/images/kernel-0xEB617C60-mmcwrite.img`) — the
`0xC07AB9FA` kernel/DTB plus a console with `sink`/`blkwr` and MMC tracing off
by default.  Raw numbers: `docs/reports/data/mmc-write-0xEB617C60-2026-10-30.txt`.

This session answered the handoff's first task.  The conclusion reverses trap 49:

> **There is no 512-byte request-size limit in `omap_hsmmc` and no write defect
> in the MMC stack.**  With the per-request `pr_info` instrumentation off, an
> O_DIRECT write reaches **5.5–7.0 MB/s**, and the requests are **256 KiB
> multi-block `CMD25`s**, not 512-byte ones.  The old 17–32 KB/s came from the
> *writer* (`recv` writing one small buffer per tty read) combined with 4–7
> unthrottled `printk`s per request to the **fbcon console**, which on this board
> is the only console and draws synchronously.

The 2 GiB QNX6 image wrote byte-exactly in session 16 and the read side was
never in question; this report is about writes from Linux.

---

## 1. Summary

| question | answer |
|---|---|
| Is there a block-count / request-size limit in `omap_hsmmc`? | **No.**  `max_blk_count = 0xFFFF`, `max_req_size = 512 × 0xFFFF`, `max_segs = 64`.  The observed ceiling is **256 KiB per request** = 64 SG entries × 4 KiB (`max_seg_size` resolves to the SDMA 4 KiB segment limit). |
| What do real writes look like? | `CMD25 arg=<lba> W blksz=512 blocks=512 bytes=262144 sg=64`, one `CMD13` (RCA `0x00010000`) after each request. |
| Why a `CMD13` after every write? | `mmc_blk_card_busy()` → `__mmc_poll_for_busy()` on write completion — normal, not a defect. |
| How fast are writes? | 4 KiB chunks **0.93 MB/s**, 64 KiB **5.9 MB/s**, 256 KiB **7.0 MB/s**, 1 MiB O_DIRECT request stream **5.5 MB/s** (measured in the kernel, no link involved). |
| Why was `recv` 17–32 KB/s? | It `pwrite()`d whatever one `read(ttyGS0)` returned (~512 B), so every request was tiny, and each request printed 4–7 `pr_info`s to **tty0/fbcon** (~25–30 ms).  512 B / 30 ms ≈ 17 KB/s. |
| Did the old `recv` write correct data? | **No.**  The 11.16 MB test landed with FNV `25a2c15560ac5bda` vs the file's `473dcd0176320fba`, and did not finish in 300 s. |
| Is the console link a usable bulk transport? | **No.**  Device-side `sink` drains at **≈134 KB/s**, and the ACM path **drops bytes** when the host blasts (16 MiB: ~768 KiB lost; 1 MiB blast: never completed).  The gadget is enumerated at **High Speed (480 Mbps)**, 512-byte bulk packets — the ceiling is a gadget/tty throughput problem, not enumeration. |

**One sentence:** the eMMC write path is healthy; write in blocks of at least
64 KiB (ideally 256 KiB–1 MiB) with the per-request tracing off, and do not use
the ACM console for bulk image transfer.

---

## 2. Method

Built `0xEB617C60` from `local/kernel-tree` (unchanged zImage/DTB) with:

* `drivers/mmc/host/omap_hsmmc.c` — `PBREQ`/`PBIRQ` gated behind a new
  `trace` module parameter (**default off**); when on, `PBREQ` also prints
  direction, `blksz`, `blocks`, `bytes`, `sg_len` and the ns since the previous
  request.
* `initramfs/console.c` —
  * `blkwr <byteoff-hex> <len> [chunk]`: timed raw write with `O_DIRECT`, a
    deterministic pattern `buf[i] = (u8)(off + written + i)`, FNV-1a64 of the
    bytes written, and the rate;
  * `sink <len>`: read and discard, i.e. the console-link rate with no media.
* `tools/pbsink.py` drives `sink`.

Bench cycle: flash `0xEB617C60` at loader block `0x825` (689 frames), boot,
`pkexec chmod 666 /dev/ttyACM0`, run the tests below, restore the sealed IFS
(181 frames), verify.  Every write in this session went to **region 1**
(raw `0x28000000..0x47F00000`, empty; the target range was verified all-zero
before use).  No NVRAM, RPMB, MCT, region 0 or TabletOS filesystem was written.

---

## 3. The measurements

### 3.1 Media throughput, `blkwr` (O_DIRECT, tracing off)

| chunk | bytes written | time | rate | notes |
|---|---|---|---|---|
| 4 KiB | 1 MiB | 1 132 447 µs | **925 KB/s** | 256 requests × 4.42 ms |
| 64 KiB | 2 MiB | 354 553 µs | **5 914 KB/s** | 32 requests × 11.1 ms |
| 256 KiB | 2 MiB | 300 232 µs | **6 985 KB/s** | 8 requests × 37.5 ms |
| 1 MiB (default) | 4 MiB | 760 589 µs | **5 514 KB/s** | split to 256 KiB requests |
| 1 MiB (default) | 1 MiB | 184 906 µs | 5 670 KB/s | with tracing on (few requests) |

Data correctness, checked with the read-only `blk sumb` on the same image:

| range | write | expected FNV-1a64 | device `blk sumb` |
|---|---|---|---|
| `0x40000000 +4 MiB` | pattern | `529aaa6348e22325` | `529aaa6348e22325` **MATCH** |
| `0x40400000 +4 MiB` | untouched | zeros | `f8e3e56ce9222325` (zeros) |
| `0x40400000 +1 MiB` | pattern (traced) | — | intact |

The pattern is fully deterministic, so this is a byte-exact write proof, not a
command-success proof.

### 3.2 The request shape (tracing on, 1 MiB O_DIRECT chunk)

```
PBREQ: mmc1 CMD25 arg=00202000 W blksz=512 blocks=512 bytes=262144 sg=64 dt=0ns
PBREQ: mmc1 CMD13 arg=00010000                        dt=27832032ns
PBREQ: mmc1 CMD25 arg=00202200 W blksz=512 blocks=512 bytes=262144 sg=64 dt=5615234ns
PBREQ: mmc1 CMD13 arg=00010000                        dt=29663086ns
...  (four 256 KiB requests per MiB)
```

* four requests per MiB, each **256 KiB**, 64 SG entries of 4 KiB;
* one `CMD13` per request (`mmc_blk_card_busy`), not one per block;
* `dt` is inflated by the trace prints themselves, which is the point of §4.

The 256 KiB request ceiling is the host's DMA model:
`mmc->max_seg_size = min3(max_req_size, rx_dma_max_seg, tx_dma_max_seg)` collapses
to **4 KiB** (the SDMA segment size), and `max_segs = 64`, so 64 × 4 KiB.

### 3.3 The link (`sink`) and the transport defect

* `sink 16777216`: device reports `16777216/16777216 bytes in 124695252 us` =
  **134 KB/s**.  The host handed the 16 MiB to the tty in ~3 s, so ~16 MiB was
  in flight/buffered; the device then drained it at its own rate.  It was
  short by ~768 KiB and needed paced bytes to finish.
* `sink 1048576`: the host wrote the MiB in **0.08 s (>12 MB/s)** and the device
  did not finish it in 120 s at all — the blaster outruns the gadget and bytes
  are **lost** (the console had to be paced out of the command).
* `lsusb -d 0fca:8010 -v`: **High Speed (480 Mbps)**, `wMaxPacketSize 0x0200`
  (512 B) bulk endpoints.  So this is not a full-speed enumeration limit.

Consequence: the ACM console is a control/debug channel.  It is **not** a bulk
image transport, and `recv` cannot be made reliable just by making the disk
write faster.

### 3.4 The old `recv` path, reproduced

`tools/pbrecv.py 0x41000000 boot/chainloader/kernel.img` (11 159 480 B):

* the host send returned quickly; the device did not finish within the tool's
  300 s wait and was still inside `recv` afterwards;
* after recovery, `blk sumb 0x41000000 11159480` = `25a2c15560ac5bda`, while the
  file's FNV-1a64 is `473dcd0176320fba` — **not a copy**.

That is expected: `recv` wrote small buffered chunks and the ACM path lost
bytes.  It is not evidence of a media defect (the same media passed the
`blkwr` proofs above).

---

## 4. Root cause, and the fix

`recv` looped `read(ttyGS0, buf, 64 KiB)` then `pwrite(fd, buf, n)`.  With the
ACM delivering ~512-byte chunks, the block layer saw ~512-byte writes; each one
became a request, and each request printed:

```
PBREQ: mmc1 CMD25 ...          (1 line)
PBIRQ: mmc1 enter              (1)
PBIRQ: mmc1 stat=... req=1     (2–3, one per IRQ)
PBREQ: mmc1 CMD13 ...          (1)
```

The **only** console is tty0 (fbcon): `docs/captures/…full-boot-from-printk-ring.txt`
records `printk: legacy console [tty0] enabled`, the DTS deliberately has no
`stdout-path`, and `ignore_loglevel` is on.  Every line is drawn synchronously,
so the printk cost was ~25–30 ms per write request — larger than the write
itself at 512 B.  That is the 17–32 KB/s.

Fix in `omap_hsmmc.c` (in the patch, verified):

* `PBREQ`/`PBIRQ` are now behind
  `/sys/module/omap_hsmmc/parameters/trace` (bool, default **0**);
* with the switch on, `PBREQ` prints `R/W blksz blocks bytes sg_len dt`, which
  is what made this report possible.  The boot log is now quiet, and a normal
  request costs no printk.

The recommended on-device writer for real data is the `blkwr` shape: accumulate
at least 64 KiB (256 KiB–1 MiB is better), write with `O_DIRECT`.

---

## 5. What this changes elsewhere

* **Trap 49 is withdrawn and replaced.**  It was built on an inference: the old
  `PBREQ` printed only opcode and LBA, never the block count, so "single
  512-byte CMD25" was never observed.  The correct statement is: *requests are
  as large as the writer submits, up to 256 KiB; the old `recv` submitted tiny
  buffered writes, and per-request fbcon printk dominated.*
* **The `hd3`/rootfs plan is unaffected in principle but must not use `recv`.**
  A rootfs written from the Linux console is limited by the ~134 KB/s link
  (4.4 h for 2 GiB), so bulk images should go through the pre-OS `0xF7` loop
  (region 1 is inside the loader's allowed `0x1B` range and needs no MCT edit,
  report 134 §6) or through QNX/`bb10mt`.
* **`blk info` reports `size 0 B`.**  It uses `fstat().st_size`, which is 0 on a
  block device; it needs `BLKGETSIZE64`.  Cosmetic (documented), not fixed in
  this image.
* **A `/dev/kmsg` anomaly was seen once:** one record contained non-printk bytes
  (a fragment of `/proc/interrupts` and console buffers).  The DRAM ring at
  `0x88000000` is `no-map` reserved in the DTS, so this is not a ring overlap;
  the cause is not established.  It did not affect any measurement here.
  Recorded so it is not silently lost.

---

## 6. Bench cycle

| # | action | result |
|---|---|---|
| — | TabletOS up, door up, `BOOTCTR_FCTRY = 0x32` | — |
| 1 | flash `0xEB617C60` at block `0x825` | 689 frames, loader reset, done |
| 2 | boot, `chmod 666 /dev/ttyACM0` | console up |
| 3 | pre-write `blk sumb 0x40000000 8388608` | `637a2df54e222325` = 8 MiB of zeros |
| 4 | `sink` 16 MiB, then 1 MiB | 134 KB/s; drops when blasted (§3.3) |
| 5 | `blkwr` 4 MiB, 1 MiB, 4 KiB, 64 KiB, 256 KiB | table in §3.1 |
| 6 | trace on, `blkwr` 1 MiB, trace off, `kmsg PBREQ` | request shape in §3.2 |
| 7 | `pbrecv.py` 11.16 MB → `blk sumb` | **mismatch** (§3.4) |
| 8 | restore `local/images/ifs0.sealed.bin` | 181 frames |

End state, verified: sealed IFS sha256 `c72b0530…`, `os_type` 6;
`BOOTCTR_FCTRY = 32 00 00 00`; TabletOS up on the full 58 GB UFS (`df /` = 58 G,
2.2 G used); `accounts/devuser/startup.sh` = 4490 B; door up.

Region-1 scratch now contains test writes at raw `0x40000000..0x40500000`
(pattern), `0x41000000..0x41AA47F8` (the failed `recv`), `0x42000000..0x42100000`,
`0x43000000..0x43400000` (pattern).  All inside region 1; nothing else touched.

---

## 7. Residual uncertainty / next

* **Why is the ACM drain only ~134 KB/s on a 480 Mbps link?**  Not investigated.
  Candidates: musb gadget URB queueing/polling, f_acm read-buffer size, tty flip
  buffering.  A faster transport (RNDIS/ECM, or USB mass storage) is the right
  long-term answer for the postmarketOS userspace; the debug console can stay.
* **Is the `recv` corruption only transport loss?**  Strongly indicated (the same
  media passes `blkwr`), but not separated from buffered-bdev semantics.  A
  rewritten `recv` (aggregate + `O_DIRECT` + per-chunk ack pacing) would settle
  it; it still cannot beat the link.
* A buffered (page-cache) large-write test was not run; `blkwr` is O_DIRECT by
  design.  If a future writer must be buffered, measure it first.
* Still open and untouched here: the recreated-`hd3` "new install" cause
  (report 135 §2 — **do not retry the shrink**), and the DSS clock as a
  clock-initialisation property (report 133 §6, trap 45).
