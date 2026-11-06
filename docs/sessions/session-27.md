# Session 27 (absolute) — the RPMB transform, the Linux top-up, and a compressed IFS

> Numbering: absolute `27`; the reports call this Linux-era **session 21**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-05 → 2026-11-06
Unit state at the end: **PlayBook OS 2.1.0.1917 running from the COMPRESSED
IFS** `local/work/ifs1917-compressed.ifs` (sha256 `f14db207…`, 3 466 232 B,
`os_type` 6, CRC verified), `/dev/hd1t179 on /base`, **`BOOTCTR_FCTRY = 0x32`**
(read as root, `32 00 00 00`), door up on `169.254.0.1` with root ssh.

## Goal of this session

`HANDOFF.md` §1: implement the bootloader's two-stage `0x2E`+`0x2F` transform in
`pbmon` and re-run `blockscan` until it finds the `BOOTCTR_FCTRY` record.  Done
looked like `blockscan` reporting the store and the record, with no RPMB write
by an agent.

It grew, with the operator's approval, into: the `0x32` top-up actually
performed from Linux, item 4 (brightness), the CRNG question, a trustzone
re-test, and a compression test of the booting IFS.

## Confirmed (measured, read in a file, or seen on the bench)

* **The services are named backwards in reports 150/151.**  `0x2F` (wrapper
  `0x8000E83C`) is the **block cipher** — 224 in / 224 out, descriptor byte
  `+0x04` is the direction, `1` = seal — and `0x2E` (`0x8000E758`) is the
  **digest**, 224 → 32, key in its descriptor, second SMC argument `1`.
  Disassembled at the corrected base; the descriptor map matches the live QNX
  page at `+0x480` word for word.  [report 175 §1;
  `local/proprietary/device-binaries/emmc-first1mb.bin` media
  `0x17758`/`0x1783C`]
* **The digest key is `seed[0:16]`** (`page+0x440`).
  `HMAC-SHA256(seed16, C[0:0xE0]) == C[0xE0:0x100]` holds offline on the
  reference's block-0x3B pair, and the monitor returns the plaintext **exactly
  when** the `0x2E` descriptor key is that value — every other candidate gives
  `-EBADMSG`.  [report 175 §3; `pairtest` rows `A=2`]
* **The cipher key is the `0x33` preload `page+0x20`**
  (`86 78 7c 2a 63 2f be 16 48 ec ab a6 d8 84 01 09`), length **16**; a
  32-byte preload is refused (`-EIO`).  [`wsweep` hit at offset `0x20`;
  `pairtest` `preload=0x20` rows]
* **`0x31(id 1)` breaks the transform.**  With the same preload: `id 1` →
  DIFF (`p=336085c2`), `id 0` or no call → `unseal=0/OK p=2dbc8359` and
  `seal=0 ct=OK tag=OK`.  [report 175 §3 table]
* **The seal is exact**: the captured block-0x3B pair round-trips in both
  directions, and live block 0x43 satisfies `seal(P) == block`.
  [`keytest` on media]
* **The raw RPMB relay is the WIRE view**: the 256-byte data field is reversed
  relative to the logical block; the tag sits at logical `+0xE0`.
  [`dump 0x3b` prints `logical=ok` and its reversed bytes equal the captured
  pair; `HMAC(seed16, rev(D)[0:0xE0]) == rev(D)[0xE0:]` for 0x3B and 0x43]
* **The store reads from Linux**: 512/512 frames authenticate, **107 blocks
  carry a valid tag and all 107 unseal**, 405 unused
  (`tag-mismatch 405, smc-error 0`).  **`BOOTCTR_FCTRY` is at block `0x3A`,
  name offset 196, `len = 4`.**  [report 175 §4; `blockscan`]
* **The Linux top-up works and was executed by the operator.**  `prep 0x3a`
  unseal → patch 4 bytes → monitor seal → seal round-trip check → one type-3
  frame; `write 0x3a yes` → `ioctl 0 result=0`, read-back frame MAC ok, block
  tag ok, plaintext `32 00 00 00`.  A scan after the write showed the same
  store structure; the restore boot's bootloader read and decremented that
  record.  [report 175 addendum; operator ran the command]
* **The boot-time top-up is wired and correct**: `/init` runs
  `pbrpmb topup 0x1E` after the console is up; at `0x30` it logs
  `0x30 >= threshold 0x1e, no write`.  [image `0x71F61598`; `kmsg topup`]
* **Item 4 (brightness) works**: writing
  `/sys/class/backlight/backlight/brightness 64` moves LM3532 register `0x74`
  from `0xFF` to `0x40` (read back with `bl rd 0x74`), back to `0xFF` at 255,
  and the panel visibly follows.  [operator observation + register reads]
  `/sys/class/leds/lm3532::backlight/brightness` returns `EBUSY` because
  `led-backlight` owns the LED (`LED_SYSFS_DISABLE`).  [console write]
* **The CRNG never seeds itself**: no `crng init done`, `entropy_avail` = 18,
  because the board DTS keeps `rng_target` disabled (a non-secure read is an
  L3 error).  The new console command `entropy <hex>` credits host bytes via
  root `RNDADDENTROPY`: `credited 256 bits (avail was 18)` → `crng init done`,
  and `pbrpmb` then takes its nonce from `/dev/random` with no warning.
* **`trustzone-omap4` on 1917 has no raw SMC passthrough**: `/dev/trustzone`
  exists, the resmgr is alive, `tzsmc nonce` returns **`ENOTTY`** and the
  resmgr survives; the 1917 binary (65 304 B) contains none of
  `0xC0280501`/`0x502` while the 4869 one (61 208 B) builds them at
  `0x8A5C`/`0x8A60`.  [report 157 addendum]
* **The booting IFS can be compressed.**  `tools/bb10_ifs.py splice
  --stock PlayBook_2.1.0.1917.4.ifs --imagefs local/work/ifs-shim.ifs`
  produced a 3 466 232-byte container (payload 3 142 560 B, 41.2% of the
  7 619 692-byte imagefs), `verify` decompresses it back exactly (117 blocks),
  `dumpifs` lists the same 106 entries with `proc/boot/rfs_validator 2237`,
  the reseal reports `RESULT: VERIFIED (CRC path)`, **and the unit booted
  TabletOS from it** (217 frames, `/base` mounted, `BOOTCTR_FCTRY = 0x32`).
  [local/work/ifs1917-compressed.ifs, sha256 `f14db207…`; operator saw it
  boot]
* **The "11.25 MB IFS budget" is not enforced by anything we run**: the Linux
  images built this session are 11 505 896 B and flash and boot, and the
  flasher's gate only checks the MCT type-`0x1B` extents.
  [`pre_os_ifs_flash.py` plan lines; `ls -l
  local/images/kernel-0x71F61598-boottopup.img`]

## Inferred (reasoning from evidence, not directly measured)

* The engine's two keys are the device's two page slots (digest at the
  bootloader's `page+0x20`, cipher at `page+0x60`); our page layout differs but
  the values pair up as measured.  *Strength: strong for the values, weak for
  the slot mapping.*
* `0x31(id 1)` selects a KEK under which the `0x33` preload must be a different
  value; we never needed to find it.  *Strength: speculation, consistent with
  the measurement.*
* The 405 non-authenticating blocks are unused rather than a stale generation.
  *Strength: plausible — they carry no valid tag and do not unseal.*
* RIM's IPL decodes our stream because it decodes the stock stream with the
  same algorithm and framing, and our decoder reproduces the stock imagefs
  exactly.  *Strength: strong (the boot proves it).*

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`pbmon` only needs `0x2F` and the right key."**  Killed by 512/512
  `-EBADMSG`: without the `0x2E` digest first, nothing authenticates.
* **"Feed `resp.data` to the monitor."**  Killed by every block failing the tag
  check — the relay is the wire view (trap 86).  Fixed with `wire_to_logical`.
* **"One key for both stages."**  Killed by `p=` not moving across candidates
  in one build and moving in the next: the slots had been set together, hiding
  that the preload alone drives the cipher.
* **"A 32-byte `0x33` preload is the longer key schedule QNX uses."**  Killed by
  `-EIO` on all 36 rows of that pass.
* **"The bootloader's sequence is the sequence."**  Killed by the `kek`
  control: `0x31(id 1)`, which the bootloader issues, changes the plaintext;
  `id 0`/none reproduce the pair.
* **"`scan` can find the record."**  Killed by `scan: 0 marker hit(s)` while
  `blockscan` found 107: `cmd_scan` searched the *ciphertext* and never called
  the monitor.
* **"`prep` can patch the plaintext in the block image."**  Killed on the bench
  by `prep: REFUSED (-14)`: the record is not in the ciphertext; it must be
  read back through the monitor unseal.
* **"`/proc/boot/shutdown -S reboot` works everywhere."**  Killed by a silent
  failure: that path is QNX-only; on our image the console's `reboot` is the
  way (trap 88).  `devuser` cannot reboot either.
* **"The CRNG just needs the right config."**  Killed by `entropy_avail` = 18
  and no `crng init done` ever, with the DTS explicitly disabling `rng_target`
  behind the firewall.
* **"The 1917 trustzone resmgr is broken like 4869's."**  Killed by `ENOTTY`
  plus the missing devctl constants: the feature is absent, not faulting.
* **`strtoul("1e", NULL, 0)` is hex.**  It is decimal `1`; the first boot-time
  top-up ran with threshold `0x01` (image `0xAC90E3DD`).  Caught by the bench
  log `0x31 >= threshold 0x01`; fixed to `"0x1e"` (`0x71F61598`).

## Unknown / open questions

* Why `0x31(id 1)` is wrong — which KEK it selects and what preload would match
  it.  *Would settle it:* a QNX-side capture of a seal with that KEK, or the
  monitor image (neither available).
* Whether the 405 non-authenticating blocks are unused or stale.  *Would settle
  it:* a fresh store's block map from QNX, or a deliberate write to one.
* Whether the *Linux* chainloader image can be QNX-compressed the same way.
  *Would settle it:* a decompressing entry path for our chainloader — RIM's
  startup blob is what decompresses the stock IFS, and our chainloader replaces
  the OS entry, so this needs design, not just `splice`.
* **What the real region-0 size limit is.**  Older notes say the IFS budget is
  11.25 MB, yet the images built this session are **11 505 896 B (0x71F61598)**
  and flash and boot: the flasher's gate only checks the MCT type-`0x1B`
  extents (`0x815..0x4804`), not 11.25 MB.  *Would settle it:* the loader's own
  range check (a read of the descriptor limits), or a deliberate oversize flash
  in region 1's shadow.  Until then treat `0xB40000` (11.79 MB) as a
  suspicion, not a fact.
* Whether `0x2F`'s mode-1 (`+0x28`/`+0x2C`) fields matter for the seal.  Not
  exercised; the mode-0 descriptor reproduces the device's bytes.

* **`tools/QCL.py` is untracked and unrecorded.**  It is the operator's new
  qconn client ("Based on https://github.com/zayfod/qcl") with no licence or
  provenance line; it was left out of the commit and must not be published
  until `NOTICE` records where it came from.  *Would settle it:* the upstream
  licence, then a `NOTICE` line and a commit.

## Superseded / uncertain

* **Report 174's naming** ("`0x2E` decrypt … `0x2F` keyed digest") is
  superseded by report 175: `0x2F` is the cipher, `0x2E` the digest.  Report
  174's *sequence* and its "two stages" finding stand.  Report 175 is current.
* **Report 156 §5's** "after a read `frame.data` is already the logical view"
  is true of the **resmgr's** page, not of `/dev/mmcblk1rpmb` (trap 86).
* **The session-19/20 plaintext-patch model** of the top-up is superseded: the
  block must be re-sealed by the monitor (report 175).
* **`IFS-REBUILD.md` §5's** "Not attempted here: compression" is superseded by
  the measurement above (the compressed IFS boots).

## Artefacts produced

* `docs/reports/175-rpmb-transform-reproduced-2026-11-06.md` (+ two addenda:
  the executed write, the boot-time top-up).
* Addendum to `docs/reports/157-trustzone-smc-passthrough-blocked-2026-10-31.md`:
  the 1917 result.
* `local/kernel-tree/drivers/misc/pbmon.c` (tracked `kernel/source/pbmon.c`):
  two-stage transform, 256-in/224-out unseal with the tag gate, 224-in/256-out
  seal, independent `/dev/pbkeys`, `op2e`, `/dev/pbwsweep` (preload sweep),
  `pb_pair` with `unseal`/`p=`/`ct`/`tag`; defaults are the measured pair and
  no `0x31`.
* `kernel/source/pbmon-pair.h`: the reference's block-0x3B pair.
* `initramfs/rpmb.c` and `initramfs/root/pbrpmb`: `wire_to_logical`, `pb_seal`,
  `pb_unseal`, `tag_ok_offline`, `pairtest`, `keytest`, `blockscan` (names
  first, records parsed), `prep`, `write`, `topup`, a seeded-nonce policy.
* `initramfs/console.c` and `initramfs/root/console`: the `entropy <hex>`
  command.
* `initramfs/init.c` and `initramfs/root/init`: the boot-time `topup_try()`.
* Images (region-0 IFS flashes, CRC): `0x4F988620`, `0x74D34374`,
  `0x4EBF1F97`, `0x48166F12`, `0x1FEBC300`, `0xD3D42853`, `0xBDE3F222`,
  `0x03953871`, `0x18B42D8C`, `0x2238D00A`, `0xAC90E3DD`, `0x71F61598` (only
  the last is archived, as `local/images/kernel-0x71F61598-boottopup.img`; the
  earlier ones were overwritten in `boot/chainloader/kernel.img` and are
  reproducible from the tree).
* `local/work/ifs1917-compressed.ifs` (3 466 232 B, sha256 `f14db207…`) — the
  compressed, CRC-sealed 1917 IFS that boots.
* Docs: STATE, HANDOFF, ROADMAP (items 4 and 6 closed), KNOWN-ISSUES (traps
  86–90), SAFETY (the Linux top-up; the boot-is-the-verification rule),
  DECISIONS (§19/§20 executed; the compressed-IFS entry), DEVELOPMENT (the
  console `entropy` command, the boot-time top-up, the restore section).
