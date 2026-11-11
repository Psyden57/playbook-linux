# Safety rules — the two stores, the boot budget, and what may be written

Read this before any write to the unit. It consolidates rules that were spread
over the README, `DECISIONS.md` §8, `KNOWN-ISSUES.md` §2 and `ROADMAP.md`, and it
carries the device facts those summaries left out. Authorities: reports `71`
§7b, `72`, `75`, `76`, `83`, `84`, `89`, `92`, `98`, `100`, `101`, `116` §2.2,
and the session notes `session-01.md` … `session-06.md`.

One sentence: **the unit is a secure-boot PlayBook with two independent
persistent stores, a finite factory boot budget, and a verifier that erases the
OS region on some failures — so every write is either normal maintenance with a
verified readback, or a brick.**

## 1. Two stores, and they are not the same thing

| store | device | contents | access | writer |
|---|---|---|---|---|
| **NVRAM** — `nvram_resource_manager` | `/dev/nvram`, an eMMC partition (MCT type `0x1E`, 64 KiB blocks `0x4805`–`0x4844`) | boot flags, `0x2801` password/attempt record, `0x2074` A/B state, PIN, blocklists | plain read/write, **no authentication** | the booted OS (`nvram_resource_manager`, `nvram-editor/nvram … patch`); the bootloader for a few IDs |
| **RPMB** — `rpmb-omap4` | `/dev/rpmb/<record name>`, eMMC RPMB partition | `BOOTCTR_FCTRY`, `BSIS_PRIV` (the OS key store) | **authenticated**: secure-monitor MAC over a nonce, monotonic write counter, key derived from a hardware KEK | a booted OS (`/dev/rpmb`); the bootloader itself provisions an *absent* record and **rewrites the decremented `BOOTCTR_FCTRY` on every successful factory boot** (`FACTORY-PATH.md`, report 150) |

The records that matter, and why NVRAM is read-only for us (report `77` §2,
`79` §3, `83` §2, `84` §6; session-05 §5):

| ID | name | why it is dangerous |
|---|---|---|
| `0x2002` | `NV_OSSTORE_PIN_NUM` | the hardware PIN, live `0x50175127`, published as `PIN`/`PIN64`; read and **never compared** by the resmgr — but it must stay consistent with the bootrom |
| `0x2004` | `NV_OSSTORE_JvmStart` | read after a *successful* verify; `0xAAAAAAAA` = committed image, skip the nuke. No writer exists in any image we have |
| `0x2040` | `NV_OSSTORE_NukeStatus` | **writing `1` starts the nuke** (erase the OS-region ranges), `2` ends it, `3` routes the next boot down the normal path. Absent on the measured unit; boot main substitutes the pre-initialised `2` |
| `0x2074` | `NV_OSSTORE_OS_REGION` (`OsRegionV3`, 72 B) | product-path A/B state only; the factory/CRC path never reads it (`85` contradiction, resolved) |
| `0x2801` | `NV_OSSTORE_PasswordStruct` | `+0x18` = failed-attempt counter **A**, `+0x1C` = maximum **B**; A selects the retry branch vs the normal path. `0x2802` is the reserve (written only when the primary write fails) |
| `0x2819` | `NV_OSSTORE_NotSupportedOS` | the anti-downgrade blocklist (10 × `{start, end, os_type_mask}`), read by the verifier |

**Rule: the NVRAM is read-only.** A read (`nvram read`, the loader's `0xE9`,
which returns the 24-byte prefix of `0x2074`) is always safe; a write is not,
because two of the fields above change which boot branch the loader takes and
one of those branches erases region 0.

## 2. `BOOTCTR_FCTRY`: the values, the top-up, and the brick

`BOOTCTR_FCTRY` is a **4-byte RPMB record**, not NVRAM (report `116` §1
corrects `108` §4.4). It is the factory boot budget.

| value | behaviour |
|---|---|
| `1 … 0x32` | factory/CRC path; our `os_type = 6` image boots. **One successful factory boot decrements it by one** |
| `0` | **hard lock** — "expired factory mode", the unit stops booting entirely |
| `> 0x32` | the same hard lock — `0x32` is both the ceiling and the target |
| `0xFFFFFFFF`, or a record of length ≠ 4 | selector 2 → failure (`KNOWN-ISSUES.md` §3.5; `docs/contradictions/expired-counter-nuke-vs-retry-halt.md`) |

* **Writing exactly `0x32` is the normal top-up, not a hack.** It is what the
  reference OS does on every TabletOS boot (`top_up_bootctr` in the operator's startup script, wait for `/dev/rpmb`, refuse to write if the value already
  reads `0x32`, build the 4-byte value, verify the temp file, write, read back
  the first byte, retry up to 6×). Report `116` §2.2 settled that `74`'s
  "never write `BOOTCTR_FCTRY`" was right for a better reason than it gave.
* **Linux performs the top-up now (session 27, report 175).**  Our image's
  `/init` runs `pbrpmb topup 0x1E` on every boot: it reads the record and, only
  when the value is inside `1..0x32` **and below `0x1E`**, unseals the block,
  patches the four value bytes, **re-seals with the monitor**, checks the seal
  round-trips to the patched plaintext, sends **one** type-3 frame, reads the
  result and unseals the read-back.  One attempt, never a loop (a failed write
  does not return).  The threshold leaves ~30 boots of headroom so a reset loop
  is noticed before the budget can reach `0`.  `pbrpmb topup <thresh>` does the
  same thing by hand; nothing else writes RPMB.
* **On the unit the **TabletOS** top-up is `/accounts/devuser/startup.sh` on the UFS**
  (the operator's script; run via `/radio/scripts/env.sh` →
  `post-boot.sh` on every TabletOS boot).
  It must be the 4490-byte version (sha256 `c265cfbb…`) that waits for a
  working `dd`; an older copy fails its first `dd` (traps 29/50), refuses the
  write, and the counter drains silently (`0x32 → 0x2d` was observed after a
  UFS restore).  Check it after any UFS restore or flash, and read the counter
  at both ends of a cycle.
* **Never test a write on the working unit.** The hazard is a wrong value or a
  half-formed write and the penalty is a brick: the known recovery is
  desoldering the eMMC, clearing the record in a reader/writer and resoldering.
  There is a unit in exactly this state. Report `116` §2.2, session-03 §6/§9.
* **`>` , `>>` and `O_TRUNC` on `/dev/rpmb/<record>` are brick operations.** The
  verified shape is `open(O_WRONLY)`, `lseek(0)`, write exactly 4 bytes
  (report `92` §5.2).
* **The record block is sealed (session 19).** The 224-byte record block is
  encrypted on media by the secure monitor (`0x2F`); the frame MAC and the
  block HMAC both cover the ciphertext. A top-up written from Linux must
  reproduce that seal (or call the monitor); **patching the received block in
  plaintext is never permitted** — the unseal yields a damaged record or a wrong
  value (reports 144, 145; `KNOWN-ISSUES.md` traps 61/62). A monitor seal must
  never be sent with an unvalidated physical address: the monitor writes 224
  bytes where it is told.
* **The failure is asymmetric**: a bad value stops the OS from booting, which
  removes the only writer. The expired unit is the proof (`71` §7b).
* Each RPMB write spends one unit of the RPMB **write counter**, which is not
  userland-readable; do not loop writes (report `92` §4.4).
* **A failed write is as fatal as a bad value** (session 20, report `154` §Q2):
  the bootloader's own decrement write blinks `0x2F` (LED **1-4**) and never
  returns, so there is no "it failed, we keep booting" path.  And the store is
  read **whole** at mount: any block that fails to read/authenticate, or any
  block whose record tiling overruns 224 bytes, makes the store unusable and
  the unit blink `0x5B` (LED **1-2-2**) forever (trap 65/66).
* **Before any `BOOTCTR_FCTRY` creation, verify read-only:** the record is to be
  exactly 4 bytes with a value in `1..0x32`; the NVRAM `0x2040` byte must be
  `1..8` (0 and ≥9 route to the nuke path, report `154` §Q5); and the block the
  record lands in must keep its co-resident record budget `L ≤ 172`.  The
  procedure is `ARMING-TUTORIAL.md`.
* Ordinary recoverable mistakes are different: a bad **region-0 image** is
  recoverable through the pre-OS reflash (§4). A counter at `0` with no OS is
  not (report `98` §9.5).

**Budget accounting.** One successful factory/CRC boot = one count (measured
`0x32 → 0x31` on the 5-byte reseal, report `95` §4; the firmware path is
`0x8000911E` → `0x800115E6`, report `89` §3.5). Report `71` §7b's "the stage
never decrements" is an incomplete caller audit — see
`docs/contradictions/71-bootctr-stage-decrement-vs-provision-only.md`. A
**pre-session-15** test image sat on media reset-looped every ~7.8 s (WDT2), so
such an image could spend several counts per minute (session-10 §3); **our
images since session 15 do not auto-reboot**, so a wedged new image stays up and
costs one count. Read the counter at *both* ends of a cycle, and note that a
reading of `0x32` only means "topped up": while the top-up script was broken it
wrote `0x00000000` (the brick
value) and logged "ok" (`docs/contradictions/bootctr-drain-vs-session10-readings.md`).

**In routine bench use the budget maintains itself.**  Our Linux image's
`/init` runs `pbrpmb topup 0x1E` on every boot (one sealed frame and a
read-back, never a loop; report 175), and every TabletOS boot writes the record
back to `0x32` (the operator's `startup.sh`).  A normal test boot therefore
spends one unit and is replenished at the next boot; **it is not a reason to
avoid an experiment.**  What still matters is the failure modes below — a value
of `0`, `> 0x32`, a failed or half-formed write, a store that fails to read
whole, and a pack too flat to boot.  Read the counter at both ends of a cycle as
a **health check**, not as a go/no-go budget.

**Power is part of the budget.** A unit on a flat pack can brown out and
warm-reset on its own, and each such boot spends a counter unit. Session 30
measured **2.817 V / capacity 0 % / `Critical`** immediately before such a
reset (`BOOTCTR_FCTRY 0x2E → 0x2D`).  Before a bench session read
`/sys/class/power_supply/bq27541-0/uevent`; at **≤3.0 V** or capacity 0 %,
stop and charge the unit with the proper charger (the PlayBook dock charges
while the unit is off but blocks the USB port).  Do not reset-loop a low pack
to "try again".  (Trap 108.)

## 3. The boot gate, so a failure can be read correctly

The chain that decides whether our image runs (reports `71` §3, `75` §3,
`89` §3.5, `97` §0; session-06 §1/§6):

1. **NVRAM `0x2040`** read; `tbb` → `0`/`2` gate, `1` nuke, `3..8` normal, `≥9`
   nuke. Absent → the pre-initialised `2`.
2. **The `0x2801` password-budget gate**: `clamp(B,3,10) − A`. Nonzero → the retry
   branch `0x80014A1E`. The surviving unit is `A=0`, `B=10`, so it takes the
   retry branch on every boot. **Do not "restore" `0x2801 +0x18` to `10` while
   the RPMB counter is `1..0x32`**: `A=10` takes the *normal* path, where
   `0x800149C2 cbnz` nukes on any nonzero selector.
3. **The RPMB selector** `FUN_8000911A` is read *inside* whichever branch was
   taken: `0` → product/ECDSA verify, `1` (value `1..0x32`) → CRC verify,
   `2` → failure status `0x0F`.
4. **The verify is region 0 only, once, and reads no NVRAM A/B state**: gate
   `desc[0xD4] == 6` (image offset `0x100`, media `0x100100`), magic
   `0xC3D0E193` at `desc+0x9C`, `end−8 == 0xD7C82D1F`, `end−4 != 0xFFFFFFFF`,
   length ≥ `0x20000`, model-ID match `0x06001A06`, then CRC-32/ISO-HDLC over
   `[start, end−4)` compared with `end−4`.
5. Success **decrements** the counter; a failed verify does not. On the normal
   path a nonzero selector does not boot at all — it nukes.

**How a unit gets switched from the product path to the factory/CRC path** — what
a clean tablet looks like, why a CRC-sealed image on an unarmed unit simply
refuses to boot (it does not wipe anything on the retry branch), and the
operator-only ordering — is in [`FACTORY-PATH.md`](FACTORY-PATH.md).  The short
version: a clean unit has the `BOOTCTR_FCTRY` record **absent** (product/ECDSA),
so it must be armed from a booted, rooted TabletOS before any CRC-sealed image is
written.

## 4. Media, regions, and the only write path we use

The eMMC is partitioned by an MCT (v1.24, magic `0x92BE564A`) in 64 KiB blocks
(report `100` §1, probed live; session-06 §3):

| MCT type | what | 64 KiB blocks | raw `mmcblk1`/`hd0` offset (measured) |
|---|---|---|---|
| `0x34` | MBR | `0x805`–`0x814` | `0x7F00000` (reads all zero on the spare) |
| `0x1B` | OS (region 0 + region 1) | `0x815`–`0x4804` | `0x8000000` |
| `0x1E` | OS NVRAM | `0x4805`–`0x4844` | `0x47F00000` |
| `0x36` | — | `0x4845`–`0x4904` | `0x48300000` |
| `0x1C` | FS (UFS) | `0x4905`–`0xFFFFF` | `0x49000000` (measured; 1 MiB above the rule's value) |
| `0x35` id 0 | **region 0** = `/dev/hd1` | `0x815`–`0x2804` | `0x8000000`, 512 MB |
| `0x35` id 1 | **region 1** = `/dev/hd2` | `0x2815`–`0x4804` | `0x28000000`, 512 MB |

**Block number → raw offset (measured in session 16, report 134 §5).**  The MCT
block numbers are *loader* coordinates, not raw device offsets:

```
raw mmcblk1/hd0 byte = (block - 0x15) * 0x10000
```

`0x15` blocks = `0x150000` = 1.3125 MiB.  The earlier "≈ media offset" column
(this table, report 100 and the flasher's log) computed `block * 0x10000` and is
therefore 1.3125 MiB high.  The **flash loop is unaffected** — the flasher and
the loader both speak loader-block coordinates, so block `0x825` is still the
right argument — but anything that computes a raw offset for `/dev/mmcblk1`
must use the shifted rule (and Python, not mental arithmetic).

**One measured exception (session 17, report 138): the `hd3` FS partition.**  The
QNX6 root is at raw **`0x49000000`**, 1 MiB above the rule's value: writing an
image to `/dev/hd3` and reading it back put its first sector at `0x49000000`, and
the QNX6 superblock at `0x49002000`.  So the FS partition's loader start is
`0x4915` under the rule (or the MCT's `0x4905` entry has a 1 MiB prefix).  The
region 0/1, IFS and NVRAM offsets above are unaffected.  A Linux-side mount of
hd3 must use `0x49000000` (`console: qnx6`, default).

There is **no `0x39` entry**, which is why path 2 of the flash protocol (and
therefore `bb10mt flash`) cannot be used on this unit (reports `98` §2.3,
`100` §1, `101` §3).

* The region-0 IFS lives at **region-0 offset `0x100000`** — loader block
  `0x825`, raw `mmcblk1`/`hd0` offset **`0x8100000`** — and is `0x2BFE10` bytes
  (44 blocks).  "IFS at media offset `0x100000`" elsewhere in these docs is
  region-0-relative shorthand.  (Session 16 measured the raw offset; the old
  `0x8250000` figure ignored the `-0x15` block shift.) <!-- audit-ok: names the old wrong value -->
* **Region 1 is not a fallback for a factory boot.** The factory path hardcodes
  region 0 and never reads region 1 or `0x2074` (`docs/contradictions/
  85-ab-fallback-vs-factory-region0.md`, resolved). Region 1 is the payload and
  backup store for later automation (report `99` §6).
* **The only write path this project uses** is the pre-OS `0xF7` reflash: boot
  ROM → `LDR_01` → `PreFlash` → `0xF7` frames → `0x40F9` with the image's real
  560-byte trailer. The loader checks only that the destination falls in an
  allow-listed MCT range; **it does not verify the payload** (report `98`
  §2–§4, `101`). It is the recovery net precisely because it does not depend on
  region 0, on the chainloader, or on the RPMB counter.
* **Writable eMMC is now allowed for our own data** (operator decision, session
  18; the Linux RPMB top-up is the gating research, report 141).  The old
  blanket "never write the eMMC from Linux" is **dropped** so `hd3` can be
  repurposed as a Linux/postmarketOS root.  What remains forbidden: **NVRAM**
  (read-only, always); the **MCT** — `0xE0`/`0xF1`/`0xF2`/`0xF3` rewrite or
  resize the partition table and are the brick path; any **RPMB** write by an
  agent (an agent never writes it; the verified `BOOTCTR_FCTRY = 0x32` top-up is
  the **operator's**, once the Linux path is proven); and `0xC0`/`0xE8`/`0xDA`
  without a recovery plan.
  Region-0's IFS/RCFS still go through the documented pre-OS `0xF7` loop.
  Overwriting QNX's own filesystem is a deliberate repurpose, not maintenance:
  keep the UFS image and the sealed IFS/RCFS as the way back.  (The stock
  loader's allow-list still does not include NVRAM type `0x1E`; the Cellebrite
  build does — report `88` §5/§7.6, `98` §2.5, session-01 §11.)
* **Read-only mounts are a separate, allowed case** (operator decision, session
  17).  A **read-only** mount of the eMMC or a QNX6 partition from the Linux
  console is allowed **with the operator's approval**; it must be read-only at
  the block layer (the console's `qnx6` uses a `LO_FLAGS_READ_ONLY` loop and
  mounts `MS_RDONLY`).  A writable mount of **our own** ext4 is now allowed
  under the session-18 decision; a writable mount of QNX's filesystem is only
  for a deliberate repurpose (and the UFS image is the way back).  See
  `docs/contradictions/safety-no-emmc-mount-vs-readonly-root.md`.
* **The way back** is a **1917 IFS** flash at block `0x825`.  It is needed to
  return to TabletOS or recover a bad test image, not to replenish the counter
  (the Linux `/init` tops that up at boot).  Two are in the tree and both boot: the **compressed**
  `local/work/ifs1917-compressed.ifs` (3 466 232 B, sha256 `f14db207…`, 217
  frames — the smaller alternative) and the **uncompressed**
  `local/work/ifs-shim-sealed.ifs` (7 900 548 B, sha256 `b52195bb…`, 489
  frames — the conservative choice).  Both are `os_type=6`, CRC-sealed.  **The boot is the verification** (operator,
  session 27): the loader CRC-verifies the image before it runs and the OS
  mounts `/base` — if the unit comes up, the flashed IFS is the one on media,
  so a read-back hash adds nothing.  What to check instead: the OS boots,
  `/base` is mounted, and `BOOTCTR_FCTRY` reads `0x32` (`cat
  /dev/rpmb/BOOTCTR_FCTRY`, now readable as root).
  `local/images/ifs0.sealed.bin` (sha256 `c72b0530…`, CRC `0x116B2562`,
  `os_type=6`) is the **2.0.0.4869** IFS and is the wrong asset for a 1917
  media set (trap 84).  The original shipping image is the *other* one —
  `75be24e8…`, CRC `0x8D46FB4A`, `os_type=4` (report `95`; session-06
  §Superseded).
* **Never flash image `0x3B04298E`** (LM3532 without VAUX3 — it wedges the I²C
  bus and resets the unit; `STATE.md`).

## 5. Hardware writes that outlive a warm reset

* **Never write the TC358764's `SYS_RST <- 0x1f`.** It left faint vertical
  artifacts on the *reference* display until a cold battery pull (trap 28).
* **Never toggle `VAUX1` on the reference** (`pmic-twl6030-ctl set_state vaux1
  off` took the unit down and needed a power cycle, trap 26). Read rails only.
* GPIO2 is reset by our boot path and GPIO4 is not, and a GPIO write to an
  un-muxed ball is a silent no-op (traps 20/21) — read the pad mux, never assume.
* Any of these can survive a warm reset; QNX does not undo them. A cold battery
  pull does, and it also wipes DRAM — including the capture rings you may need.

## 6. Who may write

| actor | may |
|---|---|
| an **agent / automated session** | read everything; build and flash images through the documented loops; **write the eMMC for our own data** (region 1, and `hd3` once repurposed) under the session-18 operator decision; never **write** NVRAM or the MCT; **never issue an RPMB write itself** — not even the `BOOTCTR_FCTRY` top-up.  The **image** performs that one write automatically at boot (`/init` → `pbrpmb topup 0x1E`, report 175); that is the operator's standing decision, not an agent action, and it obeys the threshold, single-frame and read-back rules above.  Session 20 wrote a *captured genuine* `0x32` block from Linux with a verified frame MAC; the device accepted it and the unit then stopped booting (report 149).  A write-back that proves storage does **not** prove the record still unseals.  Apart from the image's own boot-time top-up, any RPMB write is the operator's, from a booted OS, with a readback |
| the **operator**, or a guarded on-device script | the `0x32` top-up, NVRAM maintenance, and anything else in this file — on a unit they can afford to lose, with a readback |
| the **reference unit** (`0x50175127`) | the operator's writes only.  It was armed by the operator on 2026-11-01 (record `0x32`) and carries the sealed IFS; an agent treats it as read-only except the documented region-0 image flashes |

The unit inventory and its current state are in `STATE.md`; the rules above
apply to whichever unit is in front of you.  An agent's default is read-only,
with the documented image flashes as the exception; the operator performs the
writes in this file on a unit they can afford to lose.

## 7. The device password: never guess it, never default it

**Ten wrong password attempts wipe the unit** — operator, 2026-11-01: the device
destroys UFS, RCFS, IFS and several NVRAM records, including the password record
itself.  That makes a *guessed* password as dangerous as a wrong flash.

Rules:

* **No tool may default the password.**  If the caller does not supply one, pass
  nothing and let the device challenge; `bbusb.py --password` now has **no default**
  (`0000` was wrong: a unit with a different password would have spent attempts
  silently, and the failure would surface as something else).
* **Never retry a password.**  A rejection is a stop-and-ask, not a loop.  The
  library's `password_info` prompts once per challenge and gives up; it does not
  cycle guesses.
* The PlayBook password is `0000` only for *this* unit's bench setup — pass it
  explicitly (`--password 0000`) when scripting, and say so in the command.
* If a password stage fails, check the USB state and the mode *before* concluding
  "wrong password", and do not re-run blindly.
* **A rejection is not retried automatically — by us or by the tool.**  Observed
  2026-11-01: after one wrong entry the boot rom stopped offering a challenge at all
  (every later run failed without a prompt, including with the correct `--password`),
  and the budget only came back after booting the OS.  A tool that retried the
  handshake could have walked the device toward a wipe without the operator seeing a
  prompt at all.  `bbusb.py` therefore: sends a supplied password **once**, re-prompts
  interactively only when the device itself presents a fresh challenge (showing the
  budget, e.g. `9/10`), **stops once the device reports two attempts spent** (bb10mt's
  own policy, `bbusb.pas:493`), aborts on any response that is neither "accepted" nor
  "challenge" (bb10mt loops forever there — do not copy that), never sends a blank
  line as a guess, and never reboots or retries after a password failure.
