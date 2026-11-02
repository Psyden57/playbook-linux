# Roadmap

Verified at session 23 (2026-11-02).

Ordered. Each item states the evidence that would close it, so "done" is not a
matter of opinion. Items 1–3 and 3a–3d are closed (the display works). **The
active item is 0, the modified 2.1.0.1917 IFS + RCFS**; item 5's region-1 half
is done, and items 4 and 6–7 follow.

## 0. A modified BB10 2.1.0.1917 boot image — **ACTIVE**

The operator needs `/base` modifiable (bloatware removal, and later BB10 image
porting), and the RCFS validator cannot be re-signed.  The route, the format
facts and four milestones each with an acceptance test are in
**`IFS-REBUILD.md`**: an imagefs emitter appended to RIM's unchanged
`0x0..0x44910` prefix, then a `rfs_validator` mount shim, then the reseal with
the descriptor's image length, then one boot test.

**Closes when:** a 2.1.0.1917 image whose IFS mounts our `/base` boots on the
factory path, with the RCFS signature block irrelevant and unchanged.

## 1. Force the complete QNX timing set as ONE experiment — **DONE, negative**

Forced together and read back: `DISPC_TIMING_H = 0x0890A113`,
`DISPC_DIVISOR = 0x00010002`, `DSI_VM_TIMING1 = 0x01079074`,
`DSI_CLK_CTRL = 0xA0346005` (the divisor/LP divisor required selecting the
reference's HSDIV mX = 6 as well, so the set stayed coherent).  Panel still
black.  Closed by report 124.

## 2. Compare and force the DSI PHY and PLL words — **DONE, byte-identical**

Only `CFG0/1/2` differed; substituted with QNX's `0B190D13`/`42040A22`/`B8800009`.
The PLL was then made exact too (`CFG1 = 0x14A5E8BE`, `CFG2 = 0x00056000`, REGM
756 / REGN 96 / mX 6, `PLL_STOPMODE` and CFG2 3:1 clear).  Closed by reports
124/127.

## 3. Question the LVDS mapping — **CLOSED, not a variable**

The reference *does* program the bridge (report 125), and its `LV_MX0003` …
`LV_MX2427` words are **identical to upstream's VESA-24 table**.  The mapping is
therefore not the difference between the reference and us.

## 3a. Confirm the bridge writes land — **DONE, and it was the HS clock**

The blocker was not the transport: the **DSI HS clock was never started**.  The
reference starts it with a null packet on **VC0 in command mode with
`MODE_SPEED = 1`** (`L1DSS_DSI1_lp_mode_start_DSI_QNX`, report 128); mainline
only ever sends that packet on VC1 in LP.  With it, `IDREG` reads
`0x00006500` over DSI and the reference's GPIO36 test passes 1/0/1/0.  Closed by
report 131, image `0x49A770D3`.

## 3b. ~~Get a clean frame on the panel~~ — **ACHIEVED 2026-10-29**

The panel shows a clean, stable image (solid colours, kernel console as text) on
image `0x8C146805` (the same source as `0x068C867F`, rebuilt from the moved
workspace).  Three defects, each necessary and each found by diffing
against a working TabletOS rather than by reasoning about undecoded registers:

1. **bridge delivery** — the DSI HS clock was never started (report 131);
2. **LVDS porches + TCON table at bring-up** — `VP_HTIM1/2` had the porches in
   the reserved top byte, and the Hitachi table was only sent by hand
   (report 132);
3. **the DISPC core clock** — the DSS functional clock was 48 MHz against a
   50.4 MHz pixel clock, so the controller could not service the stream
   (report 133).

**Follow-up (not blocking):** the rate-mismatch WARN is fixed and the real
96 MHz is recorded (report 142); making the DSS clock a clock-initialisation
property rather than a probe-time `clk_set_rate` remains optional polish
(report 133 §6, trap 45).

## 3c. ~~Settle the Hitachi TCON byte order~~ — **settled 2026-10-29**

High byte first (the reference's, `winch_lcdctl 0x1c51c`).  With the bridge
correctly programmed **both** orders drive the panel; low-first swaps
magenta↔green and inverts black↔white.  Recorded in
`docs/contradictions/111-vs-123-tcon-byte-order.md` (report 132 §8).

## 3d. ~~Fix the horizontal timing~~ — **the real bug was ours, 2026-10-29**

The session-13 "half frame" was withdrawn long ago.  The genuine horizontal
faults were the zeroed LVDS porches in our own bridge constants (report 132 §5)
and the DISPC core clock below the pixel clock (report 133).  Both fixed.

---

## 4. Userspace brightness control

The backlight works and sits at 255; nothing changes it (report 120 §4). The
console already has `write <path> <text>` and `bl wr <reg> <val>`.

**Closes when:** the brightness is settable at run time through
`/sys/class/backlight/backlight/brightness` from the initramfs, with the chip
registers read back (`0x74`) to prove the write reached the LM3532 rather than
being swallowed by `bl_power` (trap 23). Choosing a default level and a policy is
a separate, later question.

## 5. A root filesystem — **region 1 done; real userland + hd3 cut-over next**

Userspace in our own image is an initramfs, so every driver must be `=y` and
nothing can be tried without a rebuild (report 118 §11).
**Session 18 — region 1 is done.**  A 32 MiB ext4 root was written to **region
1** at raw `0x29000000` through the pre-OS `0xF7` loop (2055 frames), read back
byte-exactly, mounted read-only through a loop, and its `/init` ran (reports
139/140; image `0xFB3A9F5D`, root `0b87c29f…`).  The loader container trap is
55.  The next steps are a **real userland** in that root and the **hd3
cut-over**: overwrite `hd3` (the MCT type-`0x1C` partition, raw `0x49000000`,
~58 GiB) with our own ext4 — **no MCT edit is needed**; the loop exposes the full
~58.76 GiB (trap 58) — gated on the Linux `BOOTCTR_FCTRY` top-up (item 6).
`DECISIONS.md` §18 records the writable-eMMC decision.

**Status after session 17 (reports 134/135/136/138):** eMMC **reads** are proven
(an 11 MB image read back byte-exact), and the **write path is healthy** — an
`O_DIRECT` write gives 256 KiB multi-block `CMD25`s at 5.5–7.0 MB/s; trap 49's
"single 512-byte CMD25, 17–32 KB/s" was the console's tiny buffered writes plus
per-request printk to fbcon (report 136).  The remaining transport problem is
that the **ACM console** drains at ~134 KB/s and drops bytes when blasted, so it
cannot supply a multi-MB image; bulk images must go through the pre-OS `0xF7`
loop or QNX over the door (~4 MB/s).  The hd3 **FS-level shrink is dead**: a
smaller filesystem in the 58 GB partition is reformatted and reseeded at boot
(report 138; `rootfs.sh`'s format+seed path, and the base seed is unprovisioned),
so region 1 is the no-MCT home for a Linux root.  The read-only-mount policy is
resolved (operator-approved).  The free-space facts are unchanged:

* **region 0 is full** — the IFS zone is 11.25 MB and the image is 11.15 MB;
* **region 1** — raw `0x28000000`, 512 MB, already partitioned and empty,
  inside the loader's allowed OS range, so an `0xF7` write at loader block
  `0x2815` needs **no MCT edit**;
* **`hd3`** — 58 GB, 2.3 GB used; shrinking it frees 50+ GB, but an FS-only
  shrink is **rejected** (a smaller FS is reformatted and reseeded at boot,
  report 138), so it needs an MCT/partition change and a backup of the live
  TabletOS root, which the sealed IFS does not provide.

The root-location decision is settled by the evidence: **region 1** (no MCT
edit).  The mount decision is **resolved and extended**: the operator approved
read-only eMMC/QNX6 mounts (session 17), and writable eMMC for **our own** data
(session 18) — so a read-only loop root at region 1 is allowed, and so is a
writable mount of our own ext4; a writable mount of QNX's filesystem is only for
the deliberate `hd3` repurpose (`SAFETY.md` §4/§6, `DECISIONS.md` §17/§18,
`docs/contradictions/safety-no-emmc-mount-vs-readonly-root.md`).

**Closes when:** the kernel mounts a real root filesystem from the eMMC
(region 1 first, at a Python-derived offset) and boots to a shell with modules
loadable, with the recovery path back to the sealed IFS verified.

## 6. RPMB / `BOOTCTR_FCTRY` from Linux

Every factory/CRC boot spends one unit of budget, and today only a **booted
TabletOS** puts it back (`bench/device-startup.sh` writes `0x32` and verifies it
by readback each time the reference OS starts). Once Linux is the normal OS,
nothing repairs it — and a wedged image reset-looping every ~7.8 s spends units
fast (`KNOWN-ISSUES.md` trap 33).

**Status after session 20: BLOCKED, and one unit was lost.**  The caller is solved
(identity physical addresses below `0xB0000000`; report 147) and the whole seal ABI
is accepted from Linux, but the transform does not reproduce the device's pair
(148), and the first write-back — a *captured, genuine* block, accepted by the
device — left the spare in the `0x5B` LED loop with no USB (149/153).  **No agent
writes RPMB** (`SAFETY.md` §6).  The reference was armed by the operator in
session 21 and now runs the sealed IFS; the open problem is the `0x2F` transform,
not the arming (`BOOT-CHAIN.md`, reports 147–157).

**Status after session 18 (superseded): feasible, and the read path is proven.**  Report 141
shows the RPMB key is **not** secure-world-only — the monitor receives the key
bytes by pointer, and the key is `f(seed)` of an in-image literal (or the CID) —
and `/dev/mmcblk1rpmb` is a pure 512-byte frame relay with no kernel keying.
`pbrpmb` proved the Linux read path (GET_WRITE_COUNTER = 4179, authenticated
READ_DATA, zero writes).  The last unknown is `f`; the next step is one read-only
extraction from a live QNX page (seed at `page+0x440`, key at `page+0x460`) or
the recovered `smc #1` ABI, then one verified `0x32` write.  The operator has
chosen Linux-only independence (`DECISIONS.md` §19).

**Status after session 19: the key is pinned, and the store is sealed.**  `f` is
known (frame key `dfeec0ce…2274`, block key `seed[0:16]`; report 143), so the
authenticated read path is understood.  But the 224-byte record block is
**encrypted on media by the secure monitor** (`0x2F`, reports 144), and a
plaintext patch is invalid (`DECISIONS.md` §20).  The monitor call from Linux was
implemented (`drivers/misc/pbmon.c`): the first hang was the monitor's DRAM bias
(`phys - 0x10000000`), and with it the call returns, but **every service
(`0x12`, `0x31`, `0x33`) answers `0xFF05`** — a global refusal (report 145).
Static RE (report 146) ruled out a registration/handshake, the domain `r2`, the
`op=1` derivation and `devb … crypto=enable`; the leading suspect is the
**command-page physical address (`r3`)**.  The next experiment is the `0x12`
address oracle.  Until the seal is reproduced, QNX's `/dev/rpmb` remains the only
proven writer.  Item 5's hd3 cut-over is therefore blocked behind item 6.

**Closes when:** a full Linux boot cycle leaves `BOOTCTR_FCTRY` reading `0x32`
afterwards, with the verified one-write/readback shape (exactly 4 bytes, value
`0x32`, read first, write only on a mismatch, verify by readback).

**Read first:** `SAFETY.md` §2. The accepted range is `1..0x32`; `0` and `> 0x32`
are a hard lock ("expired factory mode") with physical recovery only, not a
fallback to the product path; `0x32` is both ceiling and target; the write is
exactly 4 bytes at offset 0; and nothing is ever tested on the working unit.

## 7. Packaging (postmarketOS-style)

The intended end state: a reproducible artefact that builds the kernel from the
patch + config, produces the chainloader image, and installs onto the device —
the shape postmarketOS uses (a device package + a kernel package + an installer).
Nothing about tooling, licence or distribution has been decided
(`DECISIONS.md` OPEN DECISIONS); this item records the target, not a plan.

**Closes when:** a clean checkout plus documented host prerequisites reproduces
the image byte-for-byte (same CRC) and installs it on a unit without manual
steps.

---

## Out of scope for now

SGX/GPU, IVA-HD, audio, touch, sensors, WiFi, camera and the secure-world
(TrustZone) path are all disabled or untouched in the board DTS. They are not
blocked on the panel and are not on this roadmap.

## Working rules that apply to every item

* One variable per boot; never mix a timing experiment with a structural change.
* `fb <rrggbb>` before judging the panel; `kmsg` before theorising.
* Budget boots against `BOOTCTR_FCTRY`; restore and verify the sealed IFS at the
  end of every session.
* **Everything that writes** — RPMB, NVRAM, the eMMC, the media region, the panel
  chain — is governed by `SAFETY.md`.  Short version: writable eMMC for **our
  own** data is allowed (session 18); NVRAM and the MCT are never written; RPMB
  is the **operator's** `0x32` top-up only (an agent never writes it); never
  `SYS_RST`, never `VAUX1`.
