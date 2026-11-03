# Handoff — session 23 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§5).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — the reports written in absolute 18 are Linux-era "session 12";
absolute 19 is Linux-era 13, absolute 20 is Linux-era 14, absolute 21 is
Linux-era 15 and absolute 22 is Linux-era 16.  **This handoff is for absolute 23 =
Linux-era 17.**  See `docs/sessions/NUMBERING.md`.

> **Session 18 result — superseded by the session-19 and session-20 updates below.**
> The first task is **done**: a **32 MiB ext4 Linux root is in media region 1**
> at raw `0x29000000`, written through the **pre-OS `0xF7` loop** (2055 frames),
> read back byte-exactly (QNX `dd` and Linux `blk sumb`), mounted **read-only**
> through a loop (`LO_FLAGS_READ_ONLY`, `MS_RDONLY`, ext4 `noload`, no journal),
> and its `/init` ran: `PB-ROOTFS: /init is running from the region-1 ext4 image
> (Linux 6.15.11)`.  Image **`0xFB3A9F5D`**
> (`local/images/kernel-0xFB3A9F5D-rootfs.img`); root
> `local/images/rootfs-region1.ext4` (32 MiB, sha256 `0b87c29f…`, FNV
> `c2e78e1f09b50208`); boot log `local/logs/boot-20261030-063824.log` (1452
> lines, operator request).  Reports **139** and **140**.
>
> **The loader container trap that cost region 0 (trap 55).**  This unit's
> loader uses the **V1 `qcfp`** run record, whose `offset` is **region-relative**
> (`dest = 0x815 + offset`); the V2 `rrcq` is ignored.  Vendor autoloaders are
> built with **`bb10mt pack -v 1,2`**; a `-v 2` container is rejected `0x11`.
> Patch **both** records (V1 `dest-0x815`, V2 `dest-0x805`).  Leaving V1 at
> `0x10` sent a 32 MiB payload to region-0 block `0x825`, destroying the sealed
> IFS and the first 21 MiB of the RCFS at `0xC00000`; both were restored from
> `local/images/ifs0.sealed.bin` and the vendor `PB-4869-root.0.1.rcfs`, and
> verified (report 139).
>
> **Bench now:** TabletOS up, door up; sealed IFS sha256 `c72b0530…`
> (`os_type` 6, CRC `0x116B2562`); RCFS head == the vendor component
> (`1e04336b…`); **region-1 root present** (`0b87c29f…`, left in place);
> `BOOTCTR_FCTRY = 0x32`; `startup.sh` 4490 B; no chainloader/kernel on media.
> **Session 19 update:** the RPMB frame key is **pinned** (report 143), but the
> 224-byte record block is **encrypted on media by the secure monitor** (report
> 144) — a plaintext block patch is invalid, so the Linux top-up now needs the
> monitor's seal.  The monitor call was implemented (`drivers/misc/pbmon.c`): the
> first hang was the monitor's DRAM bias (`phys - 0x10000000`), and with it every
> service (`0x12`, `0x31`, `0x33`) returns `0xFF05` — a global refusal, with the
> command-page address (`r3`) the leading suspect (reports 145/146).
> `pbrpmb write` is compiled blocked.  **Next:** the `0x12` address oracle (§1),
> then the seal, then the one `0x32` top-up, then the hd3 cut-over (overwrite
> hd3, no MCT edit) and a real userland.  The ACM link is still control-only; the
> pre-OS loop still needs a reboot per image.

> **Session 20 result (reports 147/148).**  **The `0xFF05` refusal is solved —
> it was a wrong command-page address.**  The monitor reads **identity physical
> addresses** ("QNX's `−0x10000000`" was QNX's own paddr offset) and cannot reach
> `0xBF040000`; any domain works.  The `0x12` address oracle proved it: with the
> page at the reserved `0xA0200000`, `0x12` returns **0 and fills the page**
> (`w=low`), while the shadow address returns a *different* error (`1`) because
> it read our `0xAA`-filled shadow page.  `CM_MPU_STATICDEP` was also fixed to
> `0x4A004304` (session 19 wrote the undefined `0x4A004004`).  **The seal is a
> different problem now:** `0x12`/`0x31`/`0x33`/`0x2F` all return 0 and
> `seal(unseal(C)) == C`, but the transform does **not** reproduce the device's
> block-0x43 pair — 6 op/kek variants × 5 keys (fwd+rev), all 512 blocks × 6
> framings, and 10 560 offline AES combinations all fail; `op=2` is refused
> (`-EIO`), the key *is* used, and the monitor writes `+0x10 = 0x004017C8`, the
> same handle as QNX's live page.  Two new traps: the misc report nodes are
> **non-seekable** (reopen per read), and after a warm reboot `0x12` returns 0
> with **16 zero bytes**.  Bench restored: sealed IFS `c72b0530…` verified,
> TabletOS up, counter `0x32`, region-1 root intact.

> **Session 21 additions:** the ROM dump the operator supplied is **byte-identical**
> to the unit's own ROM (49 152/49 152) and the ROM carries no secure-boot path
> (the ECDSA verify is RIM's `arm9000`); the dead unit's LED is decoded — **1-2-2
> = error `0x5B` = NV/RPMB subsystem unusable**, an infinite blink loop with no
> USB (report 153); and the arming procedure is written up as a tutorial
> (`ARMING-TUTORIAL.md`).  **The survivor was armed by the operator during
> session 21, but the armed state was measured in the wrap-up (report 160):**
> `BOOTCTR_FCTRY` is **present, size 4, `32 00 00 00`**, but region 0 is still
> the **stock 4869 image** (`75be24e8…`, `os_type` **4**, CRC **`0x8D46FB4A`**,
> magic `0xC3D0E193`, model `0x06001A06`, `end-8` `0xD7C82D1F`) — **not
> CRC-sealed**.  The factory `os_type == 6` gate would fail; the unit is
> **half-armed and must not be rebooted** until the operator flashes a <!-- audit-ok: session-21 quote, superseded -->
> resealed/modified `PB-4869-root.0.3.ifs` with `bb10mt`.  The sealed stock
> asset `local/images/ifs0.sealed.bin` is `c72b0530…` (`os_type` 6, CRC
> `0x116B2562`, the 5-byte reseal); the pre-image is
> `local/images/ifs0.stock-75be24e8.bin`.  Before arming it measured:
> `BOOTCTR_FCTRY` absent, `os_type` 4, CRC `0x8D46FB4A`, IFS `75be24e8…`,
> `startup.sh` 138 B.
>
> **Superseded 2026-11-01 (operator):** the sealed (`os_type` 6) IFS **was flashed**,
> so region 0 is no longer the stock type-4 image and the unit boots normally — see the Unit row.
>
> **Session 21 read-only sweep (report 155):** the survivor was re-read per
> `BOOT-CHAIN.md` §5 and everything matches — `BOOTCTR_FCTRY` **absent**; IFS
> `75be24e8…` over exactly `0x2BFE10` bytes; `os_type` **4** at media
> `0x100100`; CRC **`0x8D46FB4A`**; `end-8` **`0xD7C82D1F`**; NVRAM `0x2040`
> **absent** (the loader substitutes `2`), `0x2801` **A=0/B=10**.  The vendor
> `PB-4869-root.0.3.ifs` and `.0.0.mbr` in `$BB10MT/` are
> byte-identical to the live IFS/MBR.  **No writes.**
>
> **Session 21 recapture (report 156):** a read-only record read produced a
> same-boot block-`0x3B` pair; **both MACs verify** (block HMAC over the logical
> view, frame MAC over the wire view with a big-endian tail), answering the
> `0x2E` oracle.  The `0x2F` unseal still does **not** match AES/DES with the
> visible keys.  Capture in `docs/reports/data/session21-capture/`.
>
> **Session 21 trustzone attempt (report 157):** the proposed
> `trustzone-omap4` raw SMC pass-through (devctl **`0xC0280501`**) was tested.
> The resmgr starts and standard devctls work, but the `0x5001` call
> **segfaults in libc** (`libc+0x91f8`) on a clean boot; stock, bypass-patched
> and `msg_max_size`-patched copies all fail.  The boot-time `random` driver's
> devctl crashes it too.  **The shortcut is unavailable; the arm gate
> remains.**
>
> **INCIDENT at the end of session 20 (report 149).**  The first Linux RPMB
> write was performed — a **captured genuine** `BOOTCTR_FCTRY = 0x32` block
> (with `TEST_PAD`), re-sent in a fresh frame whose MAC was verified host-side.
> The device accepted it (`result=0`, read-back matched), then the unit rebooted
> and **never enumerated again** (blinking red LED; Linux `-110`/`-71`/`-62`,
> Windows CFP "unknown descriptor").  The aborted sealed-IFS restore wrote
> nothing (`fr3.log`), so region 0 is untouched.  **One working unit remains.**
> From now on: **no RPMB writes by an agent, at all** (`SAFETY.md` §6), and every
> action pristine and verifiable.  The boot chain is written down in
> `docs/project/BOOT-CHAIN.md` (stage table, who writes the record, read-only
> verification protocol, open questions).

## What to read first, in this order

0. `docs/project/SAFETY.md` — the two stores, the boot budget, what may never be
   written. Nothing else in this file is safe to act on without it.
1. `docs/project/STATE.md` — the dated current block, then the history below its
   divider. That is the only place current status is stated.
2. **`docs/project/IFS-REBUILD.md`** — the active work: the 2.1.0.1917 layout,
   the format structs and the four milestones with acceptance tests.  Its
   evidence is reports **159** (feasibility / why `mkifs` fails) and **158**
   (`rfs_validator`, the `/base` mount and the signature block).
3. **Report 167** — the docs audit and its execution record (the ownership model,
   `tools/docs_audit.py`, and the two extra stale statements it found).
4. **Reports 147, 148, 156 and 157** — the RPMB state: caller/address solved
   (147), transform not reproduced (148), a genuine same-boot pair with both
   MACs verified (156), trustzone shortcut blocked (157).  Report **154** is the
   `0x5B`/record-format correction; **149** is the incident.
5. **Reports 160 and 161** — the measured armed state (pre-flash, report 160)
   and CFP's corroborating `info` (report 161, raw data in
   `docs/reports/data/cfp-2026-11-01/`).
6. **Reports 162–166** — the tooling RE: CFP's `info` opcodes, `RESETQNXOSREGION`,
   LDR_01's opcode map, and the action log (`0xB3`) vs the crash log (`0xB0`).
7. **Storage and the boot log**: report **134** (eMMC reads proven; the
   `(block−0x15)·0x10000` offset correction), **136** (write path and the ACM
   limit), **138** (the hd3 reseed), **140** (the region-1 root) and **142**
   (the clean boot log).
8. **The display, if you need it**: **133** (the DSS core clock — the last
   defect), then **132** (the QNX oracle and the porch fix).  Then
   `docs/sessions/session-22.md` for the most recent session note, and
   `docs/contradictions/README.md` for the open disagreements.

## 0. The job, and where it stands

**Goal.** A clean, upstream **Linux 6.15.11** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting from our own chainloader
image in media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`), with an
initramfs PID 1/console on USB (and optionally the read-only region-1 root).
**The stated goal — an image on the DSI → TC358764 → LVDS panel (1024x600) — is
met.**  The operator's direction after that is postmarketOS/nura.

| item | state |
|---|---|
| SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2 pet) | **working** (reports 104/109) |
| eMMC storage (MMC2) | **reads proven; writes healthy; region 1 holds a bootable Linux root** — an 11 MB freshly flashed image read back byte-exact, static media matched a pre-flash TabletOS capture (report 134); an O_DIRECT write gives **256 KiB `CMD25`s at 5.5–7.0 MB/s** (report 136); the old "single 512-byte `CMD25`, 17–32 KB/s" was `recv`'s tiny buffered writes plus per-request `printk` to **fbcon** (trap 49, replaced).  A 32 MiB ext4 root is in region 1 (raw `0x29000000`, `0b87c29f…`, report 140) and writable eMMC for our own data is allowed (`DECISIONS.md` §18).  The ACM console drains at ~134 KB/s and **drops bytes** when blasted, so it is not a bulk image transport |
| USB gadget + interactive console (`/dev/ttyACM0` ↔ `/dev/ttyGS0`) | **working** (114/115) |
| DSS/DISPC/DSI via omapdrm | **working** — and the DISPC **core clock is now correct** (96 MHz vs a 50.4 MHz pixel clock); that was the last display defect (report 133) |
| DSI link + timings + PHY + PLL | **byte-identical to the reference**, re-read live (report 132 §6) |
| TC358764 DSI→LVDS bridge | **programmed and configuration-identical to a working system**; `VP_HTIM1/2 = 0x00780001/0x00760400` |
| Bridge register **reads** | **reliable** with `PPI_TX_RX_TA = 0x00040006`, which must be the value in `pb_bridge_seq` (latched by `PPI_STARTPPI`) |
| Panel TCON (MCSPI4) | **sent automatically at bring-up by `/init`** (Hitachi table, high byte first) |
| Backlight (LM3532 @ `0x38` on i2c-0) | **working** — brightness 255, panel lit, I²C registers readable |
| **Image on the panel** | **WORKING** — clean, stable; solid colours and the kernel console as legible text |
| Unit | **ONE WORKING UNIT REMAINS: the reference (`0x50175127`), armed AND sealed** — `BOOTCTR_FCTRY` present, size 4, `32 00 00 00` (Factory mode, counter ~50); the operator **flashed the sealed `os_type` 6 IFS on 2026-11-01**, so region 0 boots (the unit has been booted into the OS repeatedly since, and the counter rises on an OS boot because that runs the devuser top-up: `50 → 49 → 47 → 49 → 50`).  **Trap 68's halt never applied — its precondition (the stock type-4 image) was removed by that flash.**  Session 21 had measured the pre-flash state (`75be24e8…`, `os_type` 4, report 160); **read region 0 back to confirm which sealed asset is on media** — the two candidates are `local/images/ifs0.sealed.bin` (`c72b0530…`) and `PB-4869-root.0.3-crc.ifs` (`561e3fae…`), both `os_type` 6 / CRC `0x116B2562`.  CFP `info` (report 161) reports mode Factory and `OS Version 2.0.0.4869 DEV` in region 0.  Door up.  The spare is **down** with the **1-2-2 blink = error `0x5B` (NV/RPMB unusable, report 153)** and presents no USB.  **Treat the reference as pristine: no RPMB writes by an agent** |

**Image on media now: a sealed 4869 IFS** — the operator flashed the `os_type` 6 image
on 2026-11-01 so the unit would boot, which is why the unit boots today.  Session 21
measured the *pre-flash* stock image (`75be24e8…`, `os_type` 4, report 160); **the exact
asset on media has not been read back yet** (candidates: `local/images/ifs0.sealed.bin`
`c72b0530…`, or `PB-4869-root.0.3-crc.ifs` `561e3fae…` — both `os_type` 6 / CRC
`0x116B2562`).  Reading region 0 back is a cheap first check for session 23, and the
loader's `0xDC` range hash or `0xC4` verify would do it without booting (report 164
addendum 9).  Session
20 flashed six diagnostic images — **`0x3116B6F8`** (oracle), **`0xC71BEF44`**
(identity page), **`0x476A084F`** (key slot + `/dev/pbkey`), **`0x30CE1FEA`**
(`/dev/pbpair`), **`0xBE48A6C5`** (`/dev/pbctl`), **`0xA3382D01`** (non-seekable
fix + `blockscan`) — each saved only in `boot/chainloader/kernel.img` as it was
built (they are a cache, not an archive; rebuild from `kernel/source` +
`initramfs/` if one is needed again).  The last session-19 test image,
**`0x96FC5CA9`**, is saved as `local/images/kernel-0x96FC5CA9-pbmon12.img`.  The **boot baseline** is
`local/images/kernel-0x0D1DA0D3-noirqerr.img`, CRC
**`0x0D1DA0D3`** — the `0xFB3A9F5D` userspace (region-1 root mount, `pbrpmb`)
with the boot-log fixes: `pb_probe` gates the DSI/bridge read probes (default
off, **0 DSI CIO errors**), `pb_reg` is a driver group (no duplicate/WARN/taint),
and the DSS clock mismatch WARN is fixed with the real 96 MHz recorded.  Verified
in `local/logs/boot-20261030-084331.log` (report 142).  Its root is
`local/images/rootfs-region1.ext4` (32 MiB, `0b87c29f…`, no journal).
(`0xFB3A9F5D` is the same userspace before the boot-log fixes; `0xFC77CD47` the
QNX6-read image; `0xEB617C60` the write-path measurement; `0x8C146805` the first
image with a correct display.)

**In one sentence:** the display works, eMMC reads are proven and the write path
is healthy, and a Linux root is in region 1; the gating blocker is the
**RPMB record-block transform** — the block is encrypted on media by the secure
monitor and Linux cannot yet reproduce the `0x2F` seal (reports 148/156) — and
the active work is the modified 2.1.0.1917 IFS (`IFS-REBUILD.md`).  The ACM
console is still control-only, and an FS-level hd3 shrink is dead (a smaller FS
is reseeded at boot).

> **Session 19 result (reports 143–146) — the `0xFF05` reading is superseded by
> report 147.**  The RPMB frame key is **pinned**
> (`dfeec0ce…2274`, block key `seed[0:16]`; report 143).  The 224-byte record
> block is **encrypted on media by the monitor**, so the plaintext patch is
> invalid (report 144, trap 61).  The monitor call was implemented
> (`drivers/misc/pbmon.c`); the first hang was the monitor's DRAM bias
> (`phys - 0x10000000`), and with it every service (`0x12`, `0x31`, `0x33`)
> returns **`0xFF05`** — a global refusal, with the command-page address (`r3`)
> the leading suspect (reports 145/146, trap 62).  The next experiment is the
> `0x12` address oracle (§1).

## 1. The one thing to do next

**Two threads; the active one is the IFS rebuild.**  The documentation audit
(report 167) is **done this session** — the ownership model, the freshness
stamps, the banners and `tools/docs_audit.py` are in place; keep the guard green
at wrap-up.

**ACTIVE: a modified 2.1.0.1917 boot image.**  The operator needs `/base`
modifiable (bloatware removal, and later PlayBook OS image porting), and the validator
cannot be re-signed, so the image itself must change.  `docs/project/IFS-REBUILD.md`
has the environment, the measured layout, the format structs, the builder design
and four milestones each with an acceptance test.  **Nothing has been built or
flashed.**  `mkifs` is blocked (it links every bootstrap and needs an ARM BSP this
SDP does not have), so the route is our own imagefs emitter appended to RIM's
unchanged `0x0..0x44910` prefix.

**SECONDARY, BLOCKED: the RPMB counter top-up.**  Stop writing to the store.
Report **149** is the incident; `docs/project/BOOT-CHAIN.md` is the stage-by-stage
model the operator asked for.

State of the RPMB work, precisely:

* the caller is solved: identity physical addresses, every monitor page below
  `0xB0000000`, any domain; `0x12`/`0x31`/`0x33`/`0x2F` all return 0 (report 147);
* the transform is **not** reproduced: no key, framing, `op`/`kek` variant, all
  512 blocks, or 10 560 offline AES combinations match (report 148);
* a **captured, genuine** sealed block written back with a correct frame
  (`result=0`, read-back byte-identical) was followed by a unit that will not
  boot — so **"the device stored my bytes" proves nothing about the record**
  (report 149).

**Job 0 is done — report 154 answers the `0x5B` question.**  The result that
matters: **a failed RPMB *write* does not produce `0x5B`** — it blinks **`0x2F`
(LED 1-4)** and never returns — so the spare did not die at the write step.
`0x5B` comes from the *mount* (it reads **every** block `0..nblocks−1`, and any
block that fails to read/authenticate or whose record tiling overruns 224 bytes
kills the store), from the *selector* (shape: length ≠ 4, wrong `slot`, a
shadowing duplicate) or from the *value*.  The record format is corrected: header
**24 bytes, no `next` field**, magic **`0x5983BC2D`**, `slot` at `+0x12`, and a
co-resident record needs **`L ≤ 172`** next to `BOOTCTR_FCTRY`.

**The read-only precondition sweep is done (session 21, report 155).**
Inputs and how they read on the reference:

* `ls /dev/rpmb/BOOTCTR_FCTRY` → **present**, size **4**, `32 00 00 00`
  (measured in the session-21 wrap-up, report 160; it was absent at the first
  sweep);
* NVRAM `0x2040` on media (read-only; `nvram read 0x2040`, `SAFETY.md` §1) →
  **absent** on the reference.  Absence is safe: the loader pre-initialises the
  buffer to `2`.  The condition is **"absent (→2) or `1..8`"**; only a *present*
  `0` or `≥9` routes to the nuke path (`0x800156E8`) and means *do not arm*;
* the region-0 image: sha256 (exact `0x2BFE10` bytes), `os_type` at 0x100100,
  CRC at 0x3BFE0C → **`75be24e8…`, 4, `0x8D46FB4A` (stock — NOT CRC-sealed;
  report 160)**;
* the store's geometry (`nblocks`) and whether any block is unreadable — QNX
  exposes only `/dev/rpmb`'s named records, so this remains **[U]** (report 154
  §9; report 155 §6);
* the door interface: `ip -br link`, then `ip addr add 169.254.0.2/16 dev <iface>`
  (the `pb-rndis` NM profile only matches `enp8s0f3u1`).

**Result: the identity, record, region and NVRAM-gate checks are clean; the record is
present (`0x32`) and the operator flashed the sealed `os_type` 6 IFS on 2026-11-01, so
the unit boots (report 160 measured the pre-flash state).**  The `0x2F` seal is still unreproduced
(report 148).  Any mismatch in the measured checks → stop; the unit is the only
one left.

Also still open: the `0x2F` transform.  The **recapture is done** (report 156):
the pair is genuine and both MACs verify, which answers the `0x2E` byte-order
question; the `0x2F` unseal still does not match AES/DES with the visible keys.
Validating/sealing needs our `pbmon` driver, i.e. our CRC-sealed Linux image;
the unit is now **armed**, so that image can boot — the remaining gate is the
transform, not arming.  The **trustzone shortcut was tried and is blocked**
(report 157: the stock resmgr segfaults in libc on the `0x5001` devctl).
2. **Reproduce the `0x2F` seal**: the atomic recapture is **done** (report 156)
   and the `0x2E` oracle is answered; the `0x2F` transform itself needs `pbmon`,
   which the now-armed unit can run.
3. **Only then** does a write become discussable — and the *operator* runs it,
   on a unit they can afford to lose, with a readback (`SAFETY.md` §6).

What the chain says about safety: the **bootloader itself** writes the record on
every factory boot (SMC `0x33` → `0x2E`/`0x2F` → `0x32`, report 146 §Q1.1f), and
QNX writes it from the OS.  Both use the same monitor services, which is why a
record written by either is readable by the other — and why a record we write
with a *different* understanding is not.

## 1a. Tools that make this cheap

* **`tools/pbdsi`** (`tools/qnx-dsiread.c`, QNX 6.6 toolchain) — read any
  TC358764 register **over DSI from a running TabletOS**, i.e. on a display that
  works.  `pbdsi dump 0 580` prints the whole map.  Push it after every unit
  reboot: `tools/pbssh.sh 'cat > /tmp/pbdsi' < tools/pbdsi`.
* **`tools/qnx-procmem.c` → `pbmem <pid> map|page|pt <vaddr>`** — metadata-only
  mapping and virtual→physical lookup for a live QNX process (the same devctls
  `pidin` uses; safe).  Pair it with `memdump3` to read the physical page.
  **Never** `lseek()`+`read()` `/proc/<pid>/as` (trap 59) — that reset the unit.
* **`nvram` read-only record reader** — the deployed
  `local/vendor/nvram-editor/nvram` only hardcodes `/proc/boot/libnvram.so.1`,
  which is a read-only image fs on this unit; use
  `local/vendor/nvram-2.x/nvram` + `libgcc_s.so.1` with
  `LD_LIBRARY_PATH=/accounts/devuser` (it loads the stock
  `/lib/libnvram.so.1`).  Report 155 §5 has the exact commands.
* **`local/kernel-tree/drivers/misc/pbmon.c`** (tracked copy
  `kernel/source/pbmon.c`) **+ `/dev/pbseal` / `/dev/pbunseal`** — the monitor
  seal/unseal call (`0x33` + `0x2F`, CM2 clocks, coherent buffers).  `run
  /pbrpmb sealtest` validates block 0x43 against the media.  The selftest is
  **off by default**; never run it at probe (it reset-loops).  The caller and
  address convention are solved (report 147); the transform is the open problem
  (reports 148/156).
* **The whole-map diff** found every real defect.  Bridge registers at
  `0x0000–0x0580`; DISPC at `0x58001000`; DSI PE at `0x58004000`; PHY
  `0x58004200`; PLL `0x58004300`.  Raw captures are in `docs/reports/data/`.
* `/sys/kernel/debug/omapdss/{clk,dispc}` — `/init` mounts debugfs at boot; the
  `clk` file is what identified the core-clock fault.
* `pb_reg` console hook (space-free argument, trap 40): `w0524:0`, `r0214`,
  `t0214` (16 reads, re-arming the VC), `u0214` (16 reads, no re-arm),
  `d0000:0580` (range dump), `v` (is the video VC still being fed).
* `fb <rrggbb>` paints the framebuffer — the panel is framebuffer console, so
  the kernel log is visible on it.
* **`sink <len>`** (device) + **`tools/pbsink.py <len>`** (host) — measure the
  console link with no media.  ~134 KB/s; it **drops bytes** if you blast, so
  pace it (report 136 §3.3).
* **`blkwr <byteoff-hex> <len> [chunk]`** (device) — timed `O_DIRECT` write of a
  deterministic pattern to `/dev/mmcblk1`, FNV-1a64 of what was written.  Use it
  to measure or to place known data (region 1 only).  256 KiB chunks ≈ 7 MB/s.
* **MMC tracing**: `write /sys/module/omap_hsmmc/parameters/trace 1` prints
  `PBREQ … R/W blksz blocks bytes sg_len dt` and the IRQ lines; leave it **off**
  for any timing measurement (printk goes to fbcon, trap 52).
* **`tools/pb_bootlog.sh`** — run it when a boot log is wanted (console up,
  `/dev/ttyACM0` chmod 666); it writes the console's full `kmsg` to
  `local/logs/boot-YYYYmmdd-HHMMSS.log`.  It takes ~10–60 s over the ACM and
  does not touch the unit's state.  (The console's `kmsg` prints message text
  without timestamps; add a `kmsgraw` if timestamps are needed.)

## 2. Traps

Full text: `docs/project/KNOWN-ISSUES.md` §2. The ones that end sessions:

9. A **write to a DSS target in module standby never completes** — it wedges the interconnect and PID 1's `reboot()`. Let `omapdrm` own the DSS.
16. **Blocking I²C is the same hazard** — everything that touches I²C runs forked with a bounded wait.
17. **A stale door holder looks like `Connection refused`** — after a host reboot start `door-hold.sh`; after a unit reboot kill the holder by explicit pid, never `pkill -f`.
21. **A GPIO write to an un-muxed GPMC ball is a silent no-op** — check `peek 4a1000XX` → `[2:0] == 3` first.
24. **`/dev/ttyACM0` is recreated per enumeration** — `pkexec chmod 666` after *every* boot (if `pkexec` starts being denied, the operator can run `sudo chmod 666 /dev/ttyACM0`).
26. **`pmic-twl6030-ctl set_state vaux1 off` takes the unit down.** Read rails; never toggle VAUX1.
27. **A flash cycle's precondition is media running, not the ramloader** — end every flash with a loader reset (the flasher now does it), or use `--reuse-loader`.
28. **Panel-chain writes outlive a warm reset and QNX does not undo them** — `SYS_RST <- 0x1f` degraded the *reference* display until a cold battery pull.
31. **Non-burst DSI video mode is rejected at this pixel clock.** Keep BURST.
33. **Our images no longer auto-reboot** (session 15 removed the 180 s timer), so a boot costs exactly one `BOOTCTR_FCTRY` unit and stays up until asked. <!-- audit-ok: trap history -->
36. **A null packet written to `SPH(vc)` in video mode never completes** — the clock-start packet needs VC0 in command mode with `MODE_SPEED` set.
37. **A failed BTA clears `VC_EN`** on that VC (does **not** explain the `-EIO` rate — that is `PPI_TX_RX_TA`, trap 44).
39. **A failed link leaves the previous image in place, with the previous CRC** — if a build prints the same CRC twice, read the build output.
43. **`local/kernel-tree/.config` can point at a different initramfs tree than `kernel/winchester.config`** — check `CONFIG_INITRAMFS_SOURCE` and verify the built cpio.
44. **`PPI_TX_RX_TA` is latched at `PPI_STARTPPI`** — writing it later does nothing.
45. **The DISPC core clock must be ≥ the pixel clock**, and the boot chain leaves it at 48 MHz; `dss_probe()` raises it to 96 MHz at probe time, which moves the PER-PLL output dividers.  Re-check the eMMC clock before relying on storage.
46. **Reading DSS/DSI registers from userspace perturbs the live picture** — repeatedly observed with `peek` of the DSI block and with the DSS register dumps.  Do not judge the panel while probing.
47. **The MCT/loader 64 KiB block numbers are not raw device offsets** — `raw = (block - 0x15) * 0x10000`; every published "media offset" was 1.3125 MiB high.  The flash loop is unaffected (it speaks loader blocks); raw `/dev/mmcblk1` offsets must use the rule.  Report 134 §5.
48. **The flash loader strips the mfcp/qcfp container header** — the payload starts at the destination block, not after a header.  Report 134 §3.
49. **Linux eMMC writes are only as large as the writer makes them.**  The driver has no request-size limit: an O_DIRECT write gives 256 KiB multi-block `CMD25`s at 5.5–7.0 MB/s, with one `CMD13` per request (`mmc_blk_card_busy`, normal).  The old "single 512-byte `CMD25`, 17–32 KB/s" was `recv` `pwrite`ing one tty-sized buffer at a time, with 4–7 unthrottled `pr_info`s per request to **fbcon** (~25–30 ms each).  Write ≥64 KiB `O_DIRECT` blocks with the MMC trace off.  Report 136.
50. **Keep `accounts/devuser/startup.sh` in sync** (live unit, UFS image, `bench/`).  An older copy stops the `BOOTCTR_FCTRY` top-up (its first `dd` runs before the overlay interpreter) and drains the factory budget.  Report 135 §5.
51. **The ACM console is not a bulk transport.**  It drains at ~134 KB/s and **drops bytes** when the host blasts (16 MiB lost ~768 KiB; a 1 MiB blast never completed), so `recv` corrupts multi-MB images (the 11.16 MB test did not read back correctly).  Bulk images go through the pre-OS `0xF7` loop or QNX/`bb10mt`.  Report 136 §3.3.
52. **Nothing on this board is free to print per I/O.**  The only console is tty0 (fbcon) and printk draws synchronously; per-request logging materially slows the path it measures.  Use the DRAM ring for high-rate events, and keep `PBREQ`/`PBIRQ` behind `/sys/module/omap_hsmmc/parameters/trace`.
53. **`blk info` reports `size 0 B`** — it uses `fstat().st_size`, which is 0 for a block device; it needs `BLKGETSIZE64`.  Cosmetic; the device is 59.4 GiB.
54. **A smaller hd3 filesystem is reformatted and reseeded at boot.**  `rootfs.sh` checks `/.rootfs`; if the boot did not accept the R/W FS as seeded, it runs `mkqnx6fs -E -b 4096 -r 5 -q /dev/hd3` at the full partition size and copies `/base/seed/rootfs`.  The base seed is unprovisioned (`var/pps` has no `system/`), so the unit shows the setup wizard.  A 1 GiB FS from the UFS factory tree does exactly this (report 138).  An FS-only shrink is dead: the partition must change (MCT).  hd3's raw start is **`0x49000000`** (report 138), and `console: qnx6` mounts it read-only.
55. **The pre-OS loader uses the V1 `qcfp` record, whose offset is region-relative** (`dest = 0x815 + offset`); the V2 `rrcq` is ignored here.  Vendor autoloaders are built with **`bb10mt pack -v 1,2`**; a `-v 2` container is rejected `0x11`.  Patch **both** records (V1 `dest-0x815`, V2 `dest-0x805`).  Leaving V1 at `0x10` while sending a 32 MiB payload meant for region 1 wrote it at region-0 block `0x825` and destroyed the sealed IFS + the first 21 MiB of the RCFS at `0xC00000`; the recovery is the sealed IFS at `0x825` then the RCFS head from the vendor `PB-4869-root.0.1.rcfs` at block `0x8D5` (report 139).
59. **Never `lseek()`+`read()` `/proc/<pid>/as` on this unit.**  The documented QNX interface faulted procnto ("Server fault on msg pass") and reset the unit (session 19).  Use `DCMD_PROC_MAPINFO`/`PTINFO` + a physical dump (report 143).
60. **The RPMB block is the reverse of the frame data field**; both HMACs follow the reversed (logical) view (report 143).
61. **The 224-byte RPMB record block is encrypted on media by the secure monitor** (`0x2F`, op 0, dir 1=seal / 0=unseal); the MACs cover the ciphertext.  A plaintext patch is invalid and a wrong `BOOTCTR_FCTRY` is the brick path.  The Linux top-up needs the monitor call (report 144).
62. **The monitor reads identity physical addresses and cannot reach `0xBF040000`.**  There is **no `−0x10000000` DRAM bias** (that was QNX's own paddr offset).  Keep every monitor page — command page, descriptor, data, key — **below `0xB0000000`** with true physical addresses; `0xA0200000` works, `0xBF040000` hangs the bus.  `0xFF05` is a **page-validation error**, not a caller rejection (a differently malformed page returns `1`), and the domain is irrelevant.  Never send a seal with an unvalidated address (it writes 224 bytes where told).  ARM `smc` = `0xe1600070|imm4`, Thumb = `0xF7F0|imm4, 0x8000`.  (Supersedes 145/146; report 147.)
63. **The seal ABI is accepted and the transform still disagrees with the device.**  `0x12`/`0x31`/`0x33`/`0x2F` all return 0 and `seal(unseal(C)) == C`, but no key/framing/op variant reproduces block 0x43 (`op=2` → `-EIO`; the key *is* used; the monitor writes `desc+0x10 = 0x004017C8`, the same handle as QNX).  Two tooling traps found the hard way: the misc report nodes are **non-seekable** — reopen them for every read, or the next read silently returns nothing; and after a **warm reboot** `0x12` returns 0 with **16 zero bytes**, so a transform result is untrustworthy until the nonce is known live.  Recapture the pair on one boot before trusting it.  (Report 148.)
The rest (15, 18, 19, 20, 22, 23, 25, 29, 30, 32, 34, 35, 38, 40, 42) are
one-liners in `KNOWN-ISSUES.md` §2.  Trap 42: the bridge's I²C slave is **not
wired**.

## 3. Where things are in this workspace

| need | path |
|---|---|
| safety rules before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console commands | `docs/project/DEVELOPMENT.md` |
| host prerequisites, toolchain paths (both toolchains) | `docs/project/SETUP.md` |
| **both cross-toolchains** (gitignored payload) | `local/toolchains/`; `. tools/env.sh` sets them up, `tools/env.sh --check` validates |
| board DTS, patched `tc358764.c` / `dsi.c` / `dss.c` | `kernel/source/` and `local/kernel-tree/` |
| kernel delta + exact config | `kernel/clean-kernel-6.15.11-changes.patch`, `kernel/patches/` |
| chainloader source | `boot/chainloader/chainloader_kernel.S.in` |
| image builder / flasher / console driver / capture | `tools/` |
| **QNX DSI register oracle** | `tools/qnx-dsiread.c` → `tools/pbdsi` |
| PID 1 and the console source | `initramfs/init.c`, `initramfs/console.c` (binaries in `initramfs/root/`) |
| **raw register captures + the DISPC diff inputs** | `docs/reports/data/` |
| **eMMC proof data (hashes, verified media map)** | `docs/reports/data/emmc-proof-0xC07AB9FA-2026-10-29.txt` |
| **MMC write-path measurements (report 136)** | `docs/reports/data/mmc-write-0xEB617C60-2026-10-30.txt` |
| **hd3 "new install" mechanism (report 137)** | `docs/reports/137-hd3-new-install-autorun-pps-race-2026-10-30.md`; the evidence is `local/backups/hd3-2026-10-29.tar` and `local/backups/newfs-2g.img` |
| **UFS recovery (bb10mt) and the shrink post-mortem** | `docs/reports/135-hd3-shrink-attempt-and-ufs-recovery-2026-10-30.md`, `DEVELOPMENT.md` §9, clean tool dir `$BB10MT` |
| image under test and the sealed IFS | `local/images/` (gitignored) |
| QNX boot set for RE | `local/proprietary/dumped4869ifs/proc/boot/` (gitignored) |
| OMAP4430 TRM (text) | `hardware/swpu231ap.pdf.txt` (`hardware/README.md`) |
| **private/public repository management** | `docs/project/REPO-MANAGEMENT.md`, `tools/publish_public.sh` |

## 4. Working rules

* **After every console command, dump the last 50–100 lines of the log and read
  them.**  The ACM console loses output, kernel oopses and L3 firewall aborts
  land in `kmsg`, and a driver's `pr_err` is often the only record of a probe's
  return codes.  Do not wait for the operator to spot an error; the operator
  asked for this explicitly after session 20.
* **Write the durable fact into the repository in the same turn you learn it.**
* **Do not budget your work against an assumed context window.** The operator
  decides when a session is wrapped up; keep working at full scope until told to
  stop.
* **Static RE only for subagents** (no bench: one process may own the USB link).
* **Use the relay channel** when something is unclear or two docs contradict
  each other.
* **Never present a guess as a fact.** Mark inferred claims as inferred; when
  reports disagree, say which is newer.
* **The whole-map diff is the highest-yield technique on this project.** It found
  every real defect in the display chain; so did asking the operator what the
  panel shows before building a theory.
* **Update the docs as you go.** `STATE.md` and `KNOWN-ISSUES.md` go stale first.
* **Publishing is a separate step.**  The public repository is a curated
  snapshot, not the private history.  Follow `docs/project/REPO-MANAGEMENT.md`
  and use `tools/publish_public.sh` (dry run, then `--apply`).  Never push the
  private history, and never force-push the public repo without the operator.

## 5. Paste-ready kickoff prompt

Start the next session in this repository root as the working directory, then
paste this.  It is self-contained; the sections above are the long form.

```
PlayBook (OMAP4430 "winchester") boot-image work — session 23
(absolute numbering; the reports call it Linux-era "session 17").

Workspace: this repository — a git checkout.  The earlier work tree
`$HOME/rea-work` is READ-ONLY.  Map: docs/README.md.

STATUS: ONE UNIT IS DOWN, ONE WORKING UNIT REMAINS — NO RPMB WRITES, EVER.
The spare died after an RPMB write-back (report 149), blinks 0x5B and presents no USB.
The survivor is the REFERENCE unit (0x50175127): Boot Mode factory, counter ~50 (the OS
boot tops it up — it can RISE: the OS boot runs the devuser top-up, so do not assume it
is monotonic).  **The operator flashed the sealed os_type 6 IFS on 2026-11-01** so the
unit boots; the pre-flash measurement (stock, os_type 4) is what report 160 recorded.
Treat the unit as the only one: no RPMB writes by an agent, and no edited-IFS flash until the offline milestones
pass.

GOAL FOR THIS SESSION: a modified **2.1.0.1917** image in which **both** the IFS
and the RCFS (/base) are ours — /base no longer gated by rfs_validator, so bloatware
can be removed and later PlayBook OS images ported.

TOOLS THAT EXIST NOW (session 22; see docs/project/DEVELOPMENT.md §10-§14):
* `tools/bbusb.py` — one CFP-style tool with all the subcommands:
    info               read-only device report: bootrom block, PIN, boot mode/count,
                       block size, flash identity, 19-TLV MCT, loader action log
                       (opcode 0xB3), bugdisp crash log (0xB0), OSREGION record, DRAM,
                       vendor id, 50-field CSD/ExtCSD, OS-version block
    flash              a .signed as-is, or a bare .ifs/.rcfs/.mbr/.ufs with
                       --dest-block; dry run unless --apply; host-side gate against the
                       measured MCT extents
    reset-osregion     delete NVRAM 0x2074 (= CFP's RESETQNXOSREGION), read-back
                       verified; --apply to write
    boot-mode          read the factory counter (writes look unimplemented in LDR_01)
    reset-loader       reset a unit left in the ramloader (0fca:8001)
    pack/unpack/split/parse   QCFM containers, byte-identical to bb10mt
* `tools/tests/test_offline.py` — 87 checks, stub device, no USB.  Run it after any
  change to bbusb.py.
* `tools/pre_os_ifs_flash.py` — **the only flasher that has ever written to this
  unit.**  `bbusb.py flash` has never run on hardware; treat it as untested until it
  has, and prefer the proven tool for anything that matters.
* `tools/ifs_extract.sh` (dumpifs wrapper), `tools/89-crc_tool.py` (reseal; still needs
  the image-LENGTH patch), `tools/pbscp.sh` (scp), plus the QNX probe tools.
* **Unused but known loader commands** (report 164 addendum 9):  `0xEC`/`0xED` report the blocked-OS and
  blocked-radio tables (relevant to moving to 2.1.0.1917).  Do **not** sweep opcodes:
  write commands (`0xC0` COMPLETE, `0xEE` erase, `0xE8`, `0xF3`) share the range.

SECOND TASK — **DONE this session.**  Report 167's audit was executed: findings
#1–#14, the ownership model in `docs/README.md`, `tools/docs_audit.py` and the
"superseded" banners on reports 90/92 (plus pointers on report 160 and
`session-21.md`).  The two §6 decisions were:
rewrite `OVERVIEW.md` as a timeless page, and add the banners.  Run
`python3 tools/docs_audit.py` at wrap-up and keep it green.

FIRST TASK: docs/project/IFS-REBUILD.md §6 milestone 1 — write `tools/ifs_build.py`
(imagefs emitter: image_header + dirents + data + trailer, appended to RIM's unchanged
0x0..0x44910 prefix) and pass its acceptance test: `dumpifs` must list the SAME 72
entries with the same names, sizes and modes from our output, and print a
`Checksums:` line.  Offline only.  Known gaps are in that file §4/§5 (trailer checksum
unknown; `dumpifs -x` does not create symlinks — take the 26 from the listing).
Then, in order: (2) the rfs_validator mount shim (ntoarmv7-gcc), tested read-only on the
stock unit as `/tmp/shim -c`; (3) teach the reseal tool the descriptor's image LENGTH as
well as os_type and CRC; (4) a boot test with the mount line over the STOCK /base.  Only
after those, build the 2.1.0.1917 IFS+RCFS pair.

THE RCFS SIDE: /base is the `1.rcfs` component — a plain QNX6 filesystem (x86 MBR at
0x0, QNX6 magic 0x68191122 at 0x2000); the RCFS signature block is a separate 64 KiB
component destined for region-0 0xF0000 (report 158).  **Do not change the IFS and the
RCFS in the same step.**

BENCH: door = 169.254.0.1 over the RNDIS interface; the host needs
`ip addr add 169.254.0.2/16 dev <iface>` (the pb-rndis NetworkManager profile is bound
to one port).  The host's /tmp is volatile.  Files move with
`tools/pbscp.sh local :/tmp/` (directories need -O; the device's /tmp is /dev/shmem and
cannot mkdir).  pkexec chmod 666 /dev/ttyACM0 after every boot of our own images.  After
EVERY console command, dump the last 50-100 lines of kmsg and read them.

PASSWORD — READ SAFETY.md §7 FIRST (new rules): `--password` has NO default; ten wrong
attempts WIPE UFS/RCFS/IFS and the password NVRAM record.  A supplied password is sent
exactly once; interactively you are re-prompted with the remaining budget (e.g. 9/10)
and the tool stops at two spent attempts, as bb10mt does.  After a rejection the boot rom
stops offering a challenge until the unit boots the OS — that silence is NOT "wrong
password", so do not retry blindly.

DO NOT: write RPMB in any form; flash an edited IFS before milestones 1-3 pass; modify
the IFS and the RCFS together.  A failed rfs_validator run shuts the unit down and marks
the region dirty in NVRAM 0x2074; recover with `tools/bbusb.py reset-osregion --apply`,
which reads the record back before and after (CFP's "cleared" print is only a status-6,
so it proves nothing).

SUBAGENTS: static RE only, no bench.  Skeleton FIRST, exact paths, an explicit "stop
when answered", findings written as you go.  Reports 162-166 are the pattern working —
two of them also corrected this project's own errors, which is exactly why the habit
(disassemble the callee before trusting a call site) is written down.

CORRECTIONS since the previous handoff (all committed, do not re-derive):
* LDR_01 has **no 0x10000-byte header**: `file_offset = VA - 0x80100000`;
* the PIN is the raw u32's **hex digits** (0x50175127 -> "50175127");
* the loader action log is **0xB3** (0xB0 is the bugdisp crash log, encrypted at rest,
  written verbatim by CFP to c:\log.bug);
* CFP's `0x461750` is `memcpy`, not a decrypt — CFP does not decrypt the log;
* the CSD/ExtCSD (50 fields), bootrom block (13) and OS-version block are decoded
  against CFP's own printed values, with captures in
  docs/reports/data/live-info-2026-11-01/.

UPDATE THE DOCS as you go and commit: HANDOFF.md, STATE.md, IFS-REBUILD.md and the
reports are the entry points and must stay true.  Record contradictions rather than
resolving them silently.  The kernel patch is current (5137 lines / 34 files; see
kernel/patches/README.md §"Regenerating this patch").
```
