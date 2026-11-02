# Session 20 (absolute) — the monitor's address view, the seal mismatch, and a brick the RE explains

> Numbering: absolute `20`.  The reports call this Linux-era **session 14**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-10-31
Unit state at the end: **one unit is down and one remains.**
* **Down:** the spare (`0x50176EC1`) — unbootable after the first Linux RPMB
  write; **1-2-2 red blink = error `0x5B` ("NV-record / RPMB subsystem
  unusable")**, no USB device ever, only the eMMC route remains (report 149).
* **Remaining: the reference (`0x50175127`)**, **unarmed and stock** —
  `BOOTCTR_FCTRY` **absent** (product/ECDSA path), region-0 `os_type` 4, CRC
  `0x8D46FB4A`, IFS sha256 `75be24e8…`, `startup.sh` 138 B.  Measured at ~14:50;
  **unplugged at handover** (no `0fca` device, no RNDIS interface).

## Goal of this session

Run the `0x12` nonce-fill address oracle for the three candidate command-page
addresses, then reproduce block 0x43's media bytes with a monitor seal, then one
verified `0x32` top-up (the stated FIRST TASK).

What happened: the oracle **succeeded** (report 147), the seal **did not
reproduce** (148), a top-up was written anyway and **killed a unit** (149), and
the rest of the session was the static RE that explains the failure (150–154).

## Confirmed (measured, read in a file, or seen on the bench)

* **The monitor reads identity physical addresses** and cannot reach
  `0xBF040000`; every monitor page must be below `0xB0000000`; the domain is
  irrelevant.  Probe 6 (`r3 = 0xA0200000`) returned **0** and the nonce landed in
  that page (`w=low`); probe 7 (`r3 = 0x90200000`, the `0xAA`-filled reserved
  shadow) returned **1** and wrote nothing.  — report 147 §3; `pbrpmb oracle 6/7`.
* **`0xFF05` is a page-validation error, not a caller rejection**; a differently
  malformed page returns `1`.  — report 147 §3.
* **`CM_MPU_STATICDEP` is `0x4A004304` bit 14**, not the `0x4A004004` session 19
  wrote (undefined in `INTRCONN_SOCKET_CM1`).  The kernel's privileged access
  works; user-mode `/dev/mem` gets `SIGBUS` (`TARGET GPMC … User mode`).  —
  report 147 §1; console `peek 4a04304`.
* **The whole seal ABI is accepted from Linux**: `0x12`/`0x31`/`0x33`/`0x2F` all
  return 0, `seal(unseal(C)) == C` exactly, `op=0` (`op=2` → `-EIO`), the key is
  used (five keys, five outputs), and the monitor writes back
  `desc+0x10 = 0x004017C8` — the same handle as QNX's live page.  — report 148 §1.
* **The transform does not reproduce the device's pair**: 6 op/kek variants ×
  5 keys (fwd+rev), **all 512 blocks** (the partition is exactly 512: 0–511
  `result=0`, 512 → `result=4`) × 6 framings, and **10 560 offline AES
  combinations** — no record magic anywhere.  — report 148 §2.
* **After a warm reboot `0x12` returns 0 but writes 16 zero bytes** (536 logged
  transforms).  — report 148 §3.
* **A fresh same-boot pair exists**: the page at `0xbfb86000` held the unsealed
  record block, the frame at `0xbf0cf000` its ciphertext (read response, addr 2);
  QNX's descriptor named the buffers **`phys − 0x10000000`** while the command-page
  arguments were identity.  — reports 147/148; `docs/reports/data/session20-capture/`.
* **The first Linux RPMB write was performed**: rehearsal on block 0x43 (re-sent
  its own bytes — accepted, read-back identical), then the captured `0x32` block
  on block 2 (`result=0`, read-back MAC ok, bytes identical).  — report 149 §1.
* **The unit then never enumerated again** (Linux `-110`/`-71`/`-62`, Windows CFP
  "unknown descriptor", blinking red LED); the aborted sealed-IFS restore wrote
  **zero** blocks (`fr3.log` stops at `waiting for bootrom`).  — report 149 §2;
  `docs/reports/data/session20-capture/fr3.log`.
* **The 1-2-2 blink is error code `0x5B`**, rendered bit-serially by `0x80014C98`
  to a **FAN5702 I²C LED driver** (7-bit `0x36`, red channel): `1` = ON/OFF 20
  units, `0` = OFF 40, end = OFF 200.  `0x5B = 1011011` ⇒ the measured groups
  (904/323/891/297/3353 ms) and a **7344 ms** period vs **7347 ms** measured.
  — report 153 §Q2; operator's video timings.
* **`0x5B` means "NV-record / RPMB subsystem unusable"**, from four sites
  (catalogue mount `0x80014940`, RPMB probe/provision `0x80014958`, selector
  `0x800090F6`, counter re-seal `0x80014A5A`).  **The loop does not reset** — it
  kicks WDT2 ~48 000× per cycle.  — report 153 §Q4/§Q5.
* **The boot ROM** (operator's XDA dump): reset `EA000242` → `0x40028910`;
  `SYS_BOOT` indexes a 32-entry command table at `0x4002F7C4`; the first read is
  **LBA 0, 512 bytes = 16 × 32-byte TOC records** (14 names, `MLO` among them);
  **no authentication code at all** — the hand-off is **`SMC #0`, `r12 = 0xF0`**
  to the secure world; USB boot presents **`0000:0000`** *and answers
  `GET_DESCRIPTOR`*; failure is an infinite retry loop; the only watchdog use is
  a 180 s WDT2 around the download, stopped after; **no GPIO/I²C/PMIC/LED**; and
  nothing in it can produce 7.35 s.  — report 152 §0.
* **Our unit's ROM is byte-identical to that dump** — 49 152 / 49 152 bytes, zero
  differences (`memdump3 40028000 c000` from TabletOS); the SRAM API at
  `0x4030D000` is readable.  — measured this session; dumps kept in
  `local/proprietary/device-binaries/`.
* **`arm9000` (`0x80009000..0x8001E000`)** is RIM's second-stage loader: own
  vector table, MMU enabled, verifies the image it jumps to (CRC-32 factory /
  pubkey), **never loads the IFS** (already in DRAM) and never touches media
  `0x100000`; programmes the MUSB core but **never the TLL/PHY**, and has **no
  descriptors and no `GET_DESCRIPTOR`**.  — report 151.
* **The MLO/ARM record path**: reader `0x800090CE`; the **decrement write runs
  after the region verify**; the halt is `0x800094BE`; WDT2 armed at `0x800094F0`
  (`WCRR = WLDR = 0xFFFC1800`, ≈7.8 s) and **not kicked from the halt**.  —
  report 150 §1/§2/§5.
* **The record store, corrected**: the on-medium header is **24 (`0x18`) bytes,
  not `0x1C`, with no `next` field**; magic **`0x5983BC2D`**; a `slot` field at
  `+0x12` selects the block; records are found by scanning for the magic on
  4-byte boundaries; two records per block are normal, with a **strict budget**:
  `BOOTCTR_FCTRY` (len 4) + a co-resident record of length `L` needs **`L ≤ 172`**;
  and the mount **reads every block `0..nblocks−1`** and fails if any block fails
  to read/authenticate or any block's tiling overruns 224 bytes.  — report 154
  §Q1/§Q3.
* **A failed RPMB *write* does not produce `0x5B`** — it blinks **`0x2F` (1-4)**
  and never returns.  `0x5B` comes from the *mount* (layout/readability), the
  selector (shape: len ≠ 4, wrong slot, duplicate) or the value.  — report 154
  §Q2/§Q4.
* **The survivor is unarmed and stock**: `BOOTCTR_FCTRY` absent, `os_type` 4,
  CRC `0x8D46FB4A`, IFS `75be24e8…`, `startup.sh` 138 B.  — measured this session
  (three reads; offsets computed with Python).
* **Host**: the door needs a link-local address on the RNDIS interface; the
  automatic setup is a **NetworkManager profile `pb-rndis`** bound to
  `enp8s0f3u1` with `169.254.0.2/16`, so it silently does nothing on another
  port.  — `nmcli connection show pb-rndis`; measured today.

## Inferred (reasoning from evidence, not directly measured)

* **The secure world does the authentication.**  The ROM has no crypto code and
  hands off with `SMC #0`, so authentication happens behind that call; the ECDSA
  verify we can see is RIM's `arm9000`.  Strength: high for the mechanism,
  unverified for the monitor's internals.  [152 §0; 151 §2]
* **The seal is deterministic in the plaintext.**  The same 16-byte ciphertext
  block (`608c40a6 49bb6c8b 3d1d4bb4 0e69ce42`) appears in two different records
  captured on different boots and counters, which a per-write tweak would not
  produce.  Strength: medium — it does not explain why no AES mode matches.
* **The dead unit is in the `0x5B` loop with the store unusable.**  All four sites
  emit the identical pattern, so *which* site fired is unknown [154 §4.5].
* **The GP/HS label is not decidable from the readable ROM** — the dump is
  identical to ours; the difference lives in the secure-world image/eFUSEs.

## Dead ends and wrong assumptions (the part handoffs lose)

* **"The monitor subtracts `0x10000000` from our buffer" (session 19's bias).**
  Killed by probe 6: with an identity page address the nonce landed in that page.
  The bias was QNX's own `paddr_t` offset.  — report 147 §3/§4.
* **"The monitor globally refuses our caller."**  Killed by probe 6 (`ret 0`);
  `0xFF05` is a page-validation error.
* **"The `0x33` key must be at the fixed `page+0x20` slot."**  Killed by changing
  the layout: the transform output was **byte-identical**, so the monitor uses the
  pointer we pass.  — report 148 §2.
* **"`op = 2` is the real block cipher."**  Both directions return `-EIO`.
* **"The reset loop explains the dead unit."**  Killed by report 153: the blink
  loop kicks WDT2 continuously; nothing resets.
* **"Catch the bootrom window with `--delay`."**  Weak: no USB device is ever
  presented (report 149 §6, corrected).
* **"The XDA ROM is a GP part's, so it does not apply."**  Half wrong: it is
  byte-identical to ours; the security simply is not in the ROM.
* **"The host has a udev rule naming the interface."**  It is a NetworkManager
  profile (`pb-rndis`), not udev.
* **"A device-accepted write proves the record is valid."**  Report 149 refutes
  exactly this: `result=0`, identical read-back, and the unit died.
* **Report 150's "the decrement write's error is discarded, so a failed write
  halts"** — corrected by 154: the write paths blink `0x2F` and never return.
* **My own arithmetic**: I read `os_type` and the CRC at wrong offsets twice
  today; the project rule (compute every offset with Python) exists for exactly
  this, and I broke it.
* **Two boots lost to the non-seekable misc nodes**: reusing one fd for a second
  read returns nothing; reopen per read (trap 63).
* **A chained `cmd & sleep; pgrep` did not actually start `door-hold.sh`** (the
  operator's standing advice); the fix was `setsid nohup … < /dev/null &` as its
  own command, verified separately.

## Unknown / open questions

* **Which `0x5B` site fired on the dead unit**, and the dead block's bytes — needs
  the unit readable again, i.e. the eMMC route.
* **Whether the monitor's `0x2F` mixes state we do not reproduce** (counter,
  nonce, engine session).  Settle it with a reproduced seal on a read-only pair
  (report 148 §6), or with the monitor image.
* **What `0x2E`/`0x2F`/`0x31`/`0x32` compute** — the monitor is not in the corpus.
* **Whether the reference unit is truly HS** — the readable ROM is identical to a
  public dump and carries no secure-boot code.
* **What the mount's accept condition `0x80010D82` really is** — one sub-branch
  folds a non-zero RPMB result into "accept" (report 154 §9).
* **Whether QNX can do a raw RPMB block read** — needed to check the store
  read-only before arming (report 154 §Q5 item 3).

## Superseded / uncertain

* **Reports 145/146** ("global refusal with `0xFF05`", "the monitor addresses DRAM
  `0x10000000` below the non-secure physical address") — **superseded by report
  147** (measured).  Report 146's *static* findings (no registration SMC, identical
  veneer, domain not validated, `op=1` not a prerequisite) stand.
* **Report 141 §7.1** (patch the received block) — superseded by 144.
* **Report 150 §2** ("a failed decrement write returns 0 and halts") —
  **superseded by report 154 §Q2** (it blinks `0x2F` and never returns).
* **Report 153 §Q3** — the `0x800094BE` halt is dead code on the verify path
  because the blinker never returns; 153 is current, 150's ordering is stale.
* **`FACTORY-PATH.md` §2** ("the bootloader can read the record and provision an
  *absent* one only … it has no write path") — contradicted by the measured
  decrement: the bootloader **rewrites** the record every factory boot (reports
  89 §4, 95 §4, 150 §2, 154).  See
  `docs/contradictions/bootloader-writes-the-record.md`.
* **Report 71 §2's selector call site** (`0x80014254`/`0x80014260`) — report 150
  puts the call at `0x800142B0`; those two are the SHA-512/CRC branch points.
* **Report 146 §0's `bootblob_arm9000.bin` range** (`[0x9000:0xe9000]`) — it is
  `[0x9000:0x1E000]` (report 151; `cmp` over 86016 bytes).

## Artefacts produced

Reports **147** (address view), **148** (seal mismatch), **149** (incident), **150**
(MLO/RPMB path), **151** (arm9000), **152** (boot ROM), **153** (LED/`0x5B`),
**154** (NV-record mount).  Docs: `docs/project/BOOT-CHAIN.md`,
`docs/project/ARMING-TUTORIAL.md`; `SAFETY.md` §6, `KNOWN-ISSUES.md` traps 62–64,
`STATE.md`, `HANDOFF.md`, `DEVELOPMENT.md` §3/§5, `kernel/patches/README.md`.

Driver (`local/kernel-tree/drivers/misc/pbmon.c`, tracked `kernel/source/`):
`CM_MPU_STATICDEP` at `0x4A004304`; identity low page; three `no-map` oracle pages
in the DTS; `/dev/pboracle`, `/dev/pbkey`, `/dev/pbctl`, `/dev/pbpair`,
`/dev/pbwsweep`; `pbmon-page.h`, `pbmon-pair.h`.

Userspace (`initramfs/rpmb.c`): `oracle`, `keytest`, `pairtest`, `wsweep`,
`blockscan`, `replay`, `replay-ident`, diagnostic `sealtest`.

Images flashed (cache entries; rebuild from source): `0x3116B6F8`, `0xC71BEF44`,
`0x476A084F`, `0x30CE1FEA`, `0xBE48A6C5`, `0xA3382D01`, `0xF8C9E5C6`, `0xC6720E6A`,
`0xA6B91878`, `0x0A52B4B9` (the one that performed the write).  One sealed-IFS
restore to the spare (now dead).

Evidence: `docs/reports/data/session20-capture/` (both page+frame captures, the
exact 256 bytes written, the built frame, every flasher log),
`docs/reports/data/rpmb-block2-replay-2026-10-31.txt`, the ROM and SRAM-API dumps
in `local/proprietary/device-binaries/`.

## Lessons (tooling) — now in the permanent docs

* **`initramfs/root/*` are checked-in prebuilt binaries with no Makefile.**  The
  docs listed only `console`/`init`; `pbrpmb` was missing.  The full ordered
  recipe, the cpio check and the `kernel-tree` ↔ `kernel/source/` sync rule are in
  `DEVELOPMENT.md` §3.
* **The misc report nodes are non-seekable** — reopen per read (trap 63).
* **The kernel patch has no generator script**; the per-file `diff -u -N` recipe is
  in `kernel/patches/README.md`.
* **Add a reserved-memory node → rebuild the DTB**, and keep the node's unit
  address equal to `reg`.
* **The builder rewrites `boot/chainloader/chainloader_kernel.S`** size constants
  every build — expected, not drift.
* **`head -c` does not take hex**; use `$((0x...))`.
* **Dump and read the console tail after every command** — two real signals
  (`crng init done`, the zero nonces) came from the operator noticing them first.
  Now in `DEVELOPMENT.md` §5 and the handoff rules.
