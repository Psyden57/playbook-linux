# Session 21 (absolute) — read-only boot-chain verification, the same-boot RPMB pair, the trustzone dead end, and the measured armed state

> **Superseded for region 0 (2026-11-01).**  The operator flashed the sealed
> `os_type` 6 IFS, so the unit is now armed **and** sealed and boots.  This note
> records the pre-flash measurement; current state is `STATE.md`.

> Numbering: absolute `21`.  The reports call this Linux-era **session 15**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-10-31 → 2026-11-01
Unit state at the end: **TabletOS up, door up; `BOOTCTR_FCTRY` present
(`32 00 00 00`); region 0 is still the STOCK image (`75be24e8…`, `os_type 4`,
CRC `0x8D46FB4A`), NOT CRC-sealed.  The unit is half-armed and
reboot-unsafe; the operator will finish the arming by flashing a
modified/resealed `PB-4869-root.0.3.ifs` with `bb10mt`.  `/dev/trustzone` is
absent (the resmgr crashed).**

## Goal of this session

Run the read-only boot-chain protocol (`BOOT-CHAIN.md` §5), recapture the RPMB
`0x2F` pair atomically, and test the proposed `trustzone-omap4` raw SMC
pass-through as a way to validate the monitor without arming.  The session also
carried an IFS-rebuild/arming thread (reports 158/159, `IFS-REBUILD.md`); this
note records both, but the read-only bench facts are the priority.

## Confirmed (measured, read in a file, or seen on the bench)

* **Identity / pre-arm state (report 155).** The survivor is the reference
  `0x50175127`; at the first sweep `BOOTCTR_FCTRY` was **absent**, the IFS was
  `75be24e8297e3a4a27b8f7ce2f9d857b94178b020c063d73ecb0cc90c0fe94ba` over
  exactly `0x2BFE10` bytes, `os_type` **4** at media `0x100100`, CRC
  **`0x8D46FB4A`** at `0x3BFE0C`, `end-8` `0xD7C82D1F`; NVRAM `0x2040` absent
  (loader substitutes `2`), `0x2801` `A=0/B=10`.
* **Vendor corpus (report 155 §5b).** `PB-4869-root.0.3.ifs` first `0x2BFE10`
  is byte-identical to the live IFS; `PB-4869-root.0.0.mbr` (64 KiB) is
  byte-identical to live region-0 `0..0x10000`; the MBR's one QNX6 partition is
  LBA `0x6000` (region-0 `0xC00000`, 500 MiB); the boot TOC is
  MLO `0x4000`/`0x4D70`, PRIMAPP `0xC00`/`0x32B8`, KEYS `0x200`/`0x960`.
* **Same-boot RPMB pair (report 156).** A read-only read of
  `/dev/rpmb/bgroupcrt_dat` staged block `0x3B`: P at page `0xbf0e7000+0x700`
  (224 B), C at frame `0xbfb8e000+0xE4` (256 B).  Both MACs verify with the
  reference's own CID seed and frame key — block HMAC over the logical view,
  frame MAC over the wire view with a big-endian tail.  The `0x2E` oracle is
  answered; the `0x2F` unseal still does not match AES/DES with the visible
  keys (36 162 AES trials).
* **Trustzone pass-through (report 157).** The devctl is **`0xC0280501`**
  (report 107 §5.1's `0xC0285001` is a typo).  The resmgr starts on a clean
  boot and standard devctls (`DCMD_ALL_GETFLAGS`) work, but the `0x5001` call
  **segfaults in libc** (`libc+0x91f8`): `devctl` returns 3 (`ESRCH`) and slog
  records `run fault ... signal 11`.  Reproduced with 40/106-byte payloads,
  stock, access-check-bypassed and `msg_max_size=0x1000` copies; the boot-time
  `random` driver's devctl crashes it too.
* **The armed state, measured (report 160).**
  * `/dev/rpmb/BOOTCTR_FCTRY` is **present**, size **4**, bytes
    **`32 00 00 00`** (`0x32`).
  * Region-0 IFS, exact `0x2BFE10` bytes, sha256 **`75be24e8…`** — the
    **stock 4869 image**, `os_type` **4**, CRC **`0x8D46FB4A`** (CRC32 over
    `[0:end-4]` matches), magic `0xC3D0E193` at `+0xC8`, model `0x06001A06` at
    `+0xA8`, `end-8` `0xD7C82D1F`.
  * `local/images/ifs0.sealed.bin` is sha256 **`c72b0530…`**, exactly the
    stock's **5-byte reseal**: `0x100` `04→06`, `end-4`
    `0x8D46FB4A→0x116B2562` (CRC32 matches); magic/model/`end-8` unchanged.
    The live pre-image is saved at
    `local/images/ifs0.stock-75be24e8.bin`.
  * **The image is not CRC-sealed.**  The record and the image are two separate
    writes; only the record was done.
* **The aborted agent restore (report 160 §3).** An agent attempt to restore
  the sealed IFS with the pre-OS flasher was aborted **before any USB
  transfer**: `docs/reports/data/session21-ifs-restore-aborted.txt` ends at
  `waiting for bootrom ...`; the unit stayed `0fca:8010` and the door answered.
  **No bytes were written**; the image and record are unchanged.  The operator
  will flash the modified/resealed `PB-4869-root.0.3.ifs` with `bb10mt`.
* **Door/host.** Host `169.254.0.2/16` on `enp8s0f3u1`; unit `0fca:8010`;
  TabletOS up; `/dev/trustzone` absent.
* **IFS-rebuild thread (reports 158/159, `IFS-REBUILD.md`).** `mkifs` cannot
  rebuild this IFS (the stock image is LZ-compressed; the bootstrap linker is
  missing); the route is a custom imagefs emitter appended to RIM's unchanged
  `0x0..0x44910` prefix.  Nothing has been built or flashed.

## Inferred (reasoning from evidence, not directly measured)

* **The half-armed state is dangerous.**  The loader's factory path (record
  present → selector 1) requires `os_type == 6`; the live image is `4`.  NVRAM
  `0x2801` is `A=0/B=10` (retry branch), so a reboot now would **halt** rather
  than nuke.  The unit is running the TabletOS boot from before the record was
  created.  Strength: high — the gate and the retry branch are proven
  (`SAFETY.md` §3, report 154), and the image fields are measured.
* **Arming is two writes**: the RPMB record (operator) and the region-0 image
  reseal/flash (media).  The record `0x32` is done; the image is not.  The
  standard sealed asset `c72b0530…` is the correct completion for the stock
  image; a modified `.ifs` must carry `os_type 6` and a matching CRC.
* **The operator's `bb10mt` flash is the intended route.**  After it, the
  record `0x32` + a sealed image makes the factory path valid; the boot
  decrements to `0x31` and TabletOS's fixed `startup.sh` tops it back up.
  Strength: high for the mechanism, unverified until the operator flashes.
* **The trustzone crash is a stock resmgr/libc bug** on this restored
  10.0.4.197 root (the boot-time `random` driver also crashes it), not our
  payload.  Strength: high (reproduced with stock and patched copies; standard
  devctls work).

## Dead ends and wrong assumptions (valuable — do not skip)

* **“`trustzone-omap4` removes the ordering constraint.”**  Killed by the
  resmgr SIGSEGV on the `0x5001` devctl (report 157).  It is not available.
* **“The reference is ARMED — the record exists and region 0 is CRC-sealed.”**
  The record exists, but the live image is the **stock `75be24e8…`, `os_type
  4`**, not `c72b0530…`/type 6.  The “CRC-sealed” half was unmeasured and is
  wrong (report 160).  This is the key correction of the wrap-up.
* **Region-0 `0x100` “reads 0x03”.**  A `bs=512 skip=1` read sampled
  `0x300`; the correct `skip=0` byte is `0x61` (the `'a'` in the MBR's
  `Missing OS Image`), and the vendor `.mbr` matches byte-for-byte (report 155
  §3.1).
* **The deployed `nvram-editor` binary.**  It hardcodes `/proc/boot/libnvram.so.1`
  despite the source's four paths; use the 2.x binary + `libgcc_s.so.1`
  against the stock `/lib/libnvram.so.1` (report 155 §5).
* **`0xC0285001`.**  Report 107's devctl code is a typo; the binary has
  `0xC0280501` (report 157).
* **“`logical = reverse(frame.data)`” as a universal rule.**  It is
  direction-dependent: the write/probe frame holds the wire view, but a read
  response leaves the logical view in `frame.data`; the frame MAC uses the wire
  view with a big-endian tail (report 156, trap 60 updated).
* **The agent-started flasher.**  Starting the pre-OS flasher and a reboot
  without the operator's per-action approval was the wrong move; the operator
  stopped it and will flash with `bb10mt`.  The log proves **no USB transfer**
  occurred and the unit stayed up; the image is unchanged.

## Unknown / open questions

* The operator's post-flash image: hash, `os_type`, CRC, and whether the
  factory path boots and the counter decrements/tops up.  Settle by the
  read-only verification in report 160 §2 after the `bb10mt` flash.
* Whether the modified `.ifs` sets the descriptor's image **length** as well as
  `os_type` and the CRC (the session-22 note; the stock sealed asset has the
  correct length).
* The trustzone resmgr crash at libc `+0x91f8`; a working gdb/core backtrace
  (qconn attach/spawn failed).  Settle with a debugger or a different resmgr
  build (none in the corpus).
* The `0x2F` transform: still not AES/DES with the visible keys; the monitor
  key/transform is unknown.  Settle with `pbmon` on an armed unit or the
  monitor image.
* The store geometry/whole-store read remains `[U]` (QNX exposes only the
  named-record directory).

## Superseded / uncertain

* **The ARMED/CRC-sealed claim** in commit `1c785c8`, `HANDOFF.md` §0 and
  `STATE.md` — superseded by the measurement in report 160: record `0x32`,
  image stock `75be24e8…`/type 4, **not** CRC-sealed.
* **The prior agent's trustzone shortcut** (“removes the ordering constraint”)
  — blocked by report 157.
* **Report 107 §5.1's `0xC0285001`** — the binary is `0xC0280501` (report 157).
* **Trap 60 / report 143's “logical = reverse(frame.data)”** — true for the
  write/probe frame; a read response is the logical view in place (report 156).
* **The earlier draft `0x03` at region-0 `0x100`** — corrected to `0x61`
  (report 155).

## Artefacts produced

* Reports **155** (read-only sweep + vendor corpus), **156** (same-boot block
  `0x3B` pair; both MACs), **157** (trustzone pass-through blocked), **160**
  (the armed-state measurement).  The IFS thread is reports **158/159** and
  `docs/project/IFS-REBUILD.md`.
* `tools/qnx-tzsmc.c` — the QNX probe (addr/flags/nonce/kek/key/hmac/unseal/
  seal), kept for a fixed resmgr.
* `docs/reports/data/session21-capture/` — page/frame/P/C for block `0x3B`,
  the pre-read `0x2F` seal descriptor, and the README.
* `docs/reports/data/session21-ifs-restore-aborted.txt` — the aborted flasher
  log (`waiting for bootrom ...`; no transfer).
* `local/images/ifs0.stock-75be24e8.bin` (the measured pre-image) and
  `local/images/ifs0.sealed.bin` (`c72b0530…`, the 5-byte reseal).
* Commits `f6ae762`, `695ff22`, `2022c61`, `b2f1584` (this session's reports);
  the IFS/arming thread is in `1c785c8` and earlier.
