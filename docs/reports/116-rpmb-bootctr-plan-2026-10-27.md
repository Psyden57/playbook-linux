# 116 — Maintaining `BOOTCTR_FCTRY` from Linux: RPMB plan (2026-10-27)

Long-term requirement: once Linux is the normal OS, **every Linux boot must put
`BOOTCTR_FCTRY` back to 50 (0x32)**.  That record lives in **RPMB**, so Linux
needs the RPMB key and an authenticated write path.

---

## 1. `BOOTCTR_FCTRY` is RPMB, not NVRAM

It is read through `rpmb-omap4`'s `/dev/rpmb/<name>` namespace — every read in
this project has been `dd if=/dev/rpmb/BOOTCTR_FCTRY …`.  Report 108 §4.4 said
"NVRAM record maintained by `nvram_resource_manager`"; that is wrong and is now
corrected in 108.  The two stores are unrelated:

| store | device | contents | access |
|---|---|---|---|
| **RPMB** (`rpmb-omap4`) | `/dev/rpmb/<name>` | `BOOTCTR_FCTRY`, `BSIS_PRIV` (107 §5.2) | authenticated; key + monotonic write counter |
| **NVRAM** (`nvram_resource_manager`) | `/dev/nvram`, region of `hd0` | boot flags, password/attempt record `0x2801`, MAC/GUID | plain read/write |

Earlier sessions already knew this: `71` says plainly that `BOOTCTR_FCTRY` is
"RPMB-gated, not NVRAM-gated", and that the OS's own `/dev/rpmb` driver is an
authorized writer (`77`, `89`).

## 2. The mechanism, exactly — from the live top-up script

`/accounts/devuser/startup.sh` (run on every TabletOS boot via
`/radio/scripts/post-boot.sh`) is the reference implementation.  Its own comment
states the semantics:

> Keep the factory boot budget topped up.  Every successful factory/CRC boot
> **decrements** `BOOTCTR_FCTRY`.  A value of 0 (or > 0x32) makes the bootloader
> fall back to the product/ECDSA path, where the resealed os_type=6 image no
> longer boots.  Reset it to 50 (0x32) each boot while the OS is still up.
> The readback check matters: an early write can be rejected by the RPMB resmgr,
> and logging success without verifying would hide a draining counter.

What it does, in order:

1. Wait up to 20 s for `/dev/rpmb/BOOTCTR_FCTRY` to appear (100 × 0.2 s).
2. Up to **6 attempts** of: write a 4-byte little-endian `0x00000032`
   (`dd of=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 conv=notrunc`), then **read back
   1 byte and require `0x32`**, else `sleep 1` and retry.
3. Log `bootctr -> 0x32 ok (date)` or `top-up FAILED … first byte='…'` to
   `/accounts/devuser/.bootctr.log`.

`.bootctr.log` shows it succeeding on **every** TabletOS boot of this session
(~25 entries), i.e. the boot budget really is being spent and repaired each
cycle.

### 2.1 Why the counter "looked static"

This session read `0x32` at the end of ~40 flash/boot cycles and I concluded
"nothing consumes it".  Wrong: **the value is static only because the script
repairs it on every TabletOS boot**, and our final reads always happened *after*
TabletOS was back up.  Without the script the budget drains by one per
factory/CRC boot and the unit is finished in 50 boots.  Lesson for future
inference: check the OS's own plumbing before concluding a counter is inert.

### 2.2 Valid range and failure modes — **out of range bricks the unit**

| value | behaviour |
|---|---|
| `1 … 0x32` | factory/CRC path; our resealed `os_type=6` image boots |
| `0` | **hard lock**: the bootloader errors *"expired factory mode"* and the unit refuses to boot, no matter what |
| `> 0x32` | the same hard lock |

The script's own comment understates this ("falls back to the product/ECDSA
path").  In practice it is **not a fallback**: the unit stops booting entirely
and there is **no software recovery** — the known remedy is physical: desolder
the eMMC, clear the record in an eMMC reader/writer, resolder.  There is a
bricked unit in exactly this state, and that is the price of one wrong value.

Consequences:

- The write must be **exactly `0x32`** and **verified by readback**.  `0x32` is
  both the ceiling and the target; anything above is not "more budget", it is
  invalid.
- `74`'s "never write `BOOTCTR_FCTRY` on the working unit" is *not* obsolete —
  it was right, for a better reason than it gave.  What *is* settled is that
  writing `0x32` is the normal, intended operation (it is how the current state
  was reached, from QNX, with the script's readback check).  The hazard is a
  wrong value or a half-formed write, and the penalty is a brick.
- Therefore **no write is ever tested on the working unit**: E4/E5 below run on
  a sacrificial/bricked unit only.  The working unit stays read-only until the
  exact write path has been proven elsewhere.

## 3. The key (recovered)

`proc/boot/rpmb-omap4` (33 601 B) and the bootloader (`loader_06001A06-00.bin`,
string at file offset `0x38da0`) both contain:

```
"retreating is advancing in the other direction"     (46 bytes)
```

and a decompilation of `rpmb-omap4` (scratchpad `rpmbout/decompiled.txt`, line
1999) shows how it is used:

```c
iVar2 = FUN_00012814(0, &DAT_00018248);                 /* RPMB key gen with KEK 0 */
if (iVar2 != 0 || (iVar2 = FUN_00012814(1, &DAT_00018248)) != 0) {
        memcpy(_DAT_0001824c, "retreating is advancing in the o", 0x20);   /* 32 B */
        ...
}
```

- the driver first tries to **derive the RPMB key from a KEK** (index 0, then 1 —
  matching *"RPMB key gen with KEK %d failed, retrying"* / *"retrying RPMB key gen
  with KEK %d"*);
- on failure it uses the **first 32 bytes of the literal**,
  `"retreating is advancing in the o"` (exactly 0x20 bytes).

On this unit the KEK path is unavailable — our `select_kek` SMC (`0x31`) returns
`−1` at every probe point (109 §6) — so TabletOS itself runs on the fallback.
**The RPMB key on this box is therefore almost certainly those 32 bytes**, which
E2 proves with a read.

## 4. The Linux side

- `mmcblk1rpmb` exists as a chardev (250:0), created unconditionally by
  `mmc_block` in 6.15 (`CONFIG_MMC_RPMB` no longer exists; see 109 §9).  It
  takes/returns **raw 512-byte RPMB frames** (JEDEC JESD84-B51): 256-byte data +
  nonce/MAC/result area, big-endian fields, HMAC-SHA256 with the 32-byte key.
- Nothing in mainline provides the *named record store* — that is `rpmb-omap4`'s
  own layer (its strings describe a metadata/catalogue block: *"RPMB rec:
  metadata does not match block data"*, *"more records in block than possible"*,
  *"done cataloguing records"*).
- **Shortcut**: the partition is 128 KiB.  With the key, authenticated-read every
  block and scan for the ASCII name `BOOTCTR_FCTRY`; once found, the offset is
  known forever and no layout reversal is needed.
- Implementation can live entirely in userspace (an `rpmb` command in the USB
  console), so **no kernel changes are required** for any step below.

## 5. Experiment ladder (read-only until E4 passes)

| # | experiment | success signal | risk |
|---|---|---|---|
| E1 | RPMB **read write-counter** (`cmd 0x0002`, no key needed) | a plausible monotonic counter | none |
| E2 | Authenticated **read** of block 0 with the 32-byte literal (nonce + HMAC) | `result == 0` and the MAC verifies → **key proven** | none |
| E3 | Sweep all data blocks, locate `BOOTCTR_FCTRY`, decode it, confirm it equals 50 | record found, value matches QNX's read | none |
| E4 | **Sacrificial unit only.** Write **0x32** with the same framing, then read back | first byte reads `0x32`; QNX still reads 0x32 | **write — can brick** |
| E5 | **Sacrificial unit only.** Integrate the per-boot top-up with the script's retry/verify discipline, run several boot cycles | `-> 0x32 ok` every boot, counter never observed out of range | **write** |

The correct way to build E4/E5 is to **reproduce `rpmb-omap4`'s exact request
frame** (RE the frame construction in that binary) rather than hand-rolling the
JEDEC framing from the spec: the QNX path is the one proven to work on this
device, and every deviation is a potential brick.  Validate read-first (E2/E3)
so the key, nonce and MAC handling are already known-good before a single write
byte is sent.

When porting, copy `startup.sh`'s discipline verbatim:

- write a 4-byte LE `0x00000032` (not a longer buffer with junk);
- **verify by readback** (first byte == 0x32) — the RPMB resmgr can reject an
  early write, so retry with a delay rather than logging success blindly;
- clamp: only ever `0x32`; refuse any other value in the code path;
- run it as soon as the RPMB device exists, before anything else can boot again.

## 6. Recovery: there is none in software

Do **not** count on the pre-OS flash to rescue a bad counter.  The observed
behaviour is a refusal to boot with *"expired factory mode"* — the bootloader
will not proceed regardless of what is on media, so flashing a good image does
not help and `startup.sh` never runs because TabletOS never starts.  The only
known remedy is physical (desolder the eMMC, clear the record, resolder).

Treat a counter write as **the most dangerous operation in this project** —
worse than the media restore, which is at least repeatable.  A wrong value costs
a unit and a soldering session.

## 7. Open questions

- Does the bootloader read-then-decrement, or decrement-then-read?  It decides
  whether a top-up during the *previous* boot suffices (evidently yes, since the
  script runs post-boot).
- Does `0x800149C2` (`75`) *consume* the record or only validate it?  `75` calls
  it "nuke"; the observable behaviour is a drain of one per factory/CRC boot,
  which is exactly what `startup.sh` compensates for.  The *reason* an
  out-of-range value hard-locks (rather than falling back) is presumably in that
  path — worth understanding before any write.
- Whether the KEK path would work if L4SEC were set up as `trustzone-omap4` does
  it — irrelevant while the fallback works, but it is the "clean" path.
- Only `BOOTCTR_FCTRY` may be written.  `BSIS_PRIV` (168 B, the device key, 107
  §5.2) must never be touched.

---

## Session 5 addendum (2026-10-27) — the top-up script was broken, and its check lied

Found by the operator: `cat /accounts/devuser/.startup.log` on TabletOS showed

```
/accounts/devuser/startup.sh[40]: printf: cannot execute - No such file or directory
```

`[40]` is the **call site** (`top_up_bootctr`), not the failing line — QNX's ksh
reports the caller's line for a failure inside a function.  The command actually
failing was line 25:

```sh
printf '\062' | dd of=/tmp/bc32.bin bs=1 count=1 conv=notrunc 2>/dev/null
```

**`printf` exists neither as a builtin nor as a binary on TabletOS**, so the
producer of that pipe failed, `dd` wrote nothing, the temp file stayed
`00 00 00 00`, and the script then wrote **`0x00000000` — the brick value — to
`BOOTCTR_FCTRY`, six times per boot**, since the readback check then failed and
the loop retried.

### The check could not have caught it

```sh
v=$(dd if=/dev/rpmb/BOOTCTR_FCTRY bs=1 count=1 2>/dev/null)
if [ "$v" = "2" ]; then echo "bootctr -> 0x32 ok" ...
```

That only re-reads the counter.  It cannot distinguish

* "our write of 0x32 landed", from
* "our write was rejected / never happened, and the counter still holds its
  previous 0x32".

So `.bootctr.log` read `bootctr -> 0x32 ok` on **every boot** (most recently
2026-10-27 15:18) while the script was, in fact, asking the device to store
`0x00000000`.  **The counter reading 50 has never been evidence that the repair
works.**  Whether the zero-write is rejected outright, rejected by RPMB auth, or
silently ignored is *not* established — and must not be established on this unit
(see §2.2: writes get proven on the sacrificial unit only).

### The fix (deployed 2026-10-27)

`/accounts/devuser/startup.sh` (3 142 B; the old one is at
`startup.sh.bak-20261027`; workspace copies are
`analysis/device-startup.sh` and `analysis/device-startup.sh.orig-2026-10-27`):

1. **No `printf`.**  The value is built with `echo` (a builtin, already used
   elsewhere in the script): `echo 2 | dd … bs=1 count=1 conv=notrunc` writes
   exactly `0x32` (verified on the unit: the temp file reads `32 00 00 00`).
2. **Verified before writing.**  The temp file's low byte is read back and the
   write is *refused* unless it is `'2'` — a bad temp file can no longer turn
   into a zero write.
3. **No write when already correct.**  If the counter already reads `0x32` the
   script logs `already 0x32, no write` and returns, so RPMB wear is bounded by
   actual decrements rather than by boots.
4. Distinct log lines now separate "wrote and verified" from "refused" and from
   "already correct" (`wrote 0x32, readback ok`, `REFUSING to write`,
   `already 0x32, no write`).

Smoke test on the live unit (counter at 0x32): `sh ./startup.sh` returns 0 with
`bootctr: already 0x32, no write`, and the counter still reads `2` (`32 00 00 00`).

**Consequence for the counter budget:** the "50 boots of headroom, repaired
every QNX boot" assumption is only true from 2026-10-27 onward.  Before that the
repair was a no-op at best and a zero-write at worst, so treat any BOOTCTR
observation from earlier sessions as *unrepaired* — the value may simply never
have been decremented on the boots we were counting.

### Addendum 2 (2026-10-27 16:18) — the guard fired for a second, different reason

Two things were established by the operator's own test:

1. **RPMB writes DO land.**  The counter was set to `0x28` by hand, the script
   was run, and it logged `bootctr: wrote 0x32, readback ok (was '(')` — the
   counter then read `2\000\000\000`.  So the write path works; the earlier
   zero-writes were being refused for a different reason (a `0x00000000` payload
   is presumably rejected as out of range, which is the only reason the unit
   survived).
2. **The counter really does drain one per boot.**  Between 15:33 (0x32) and
   16:18 the counter fell to `0x28` (40) over roughly ten boots, with the
   top-up refusing at 16:18.  That confirms the earlier warning: the "50 all
   day" readings from before this session were *not* evidence of a working
   repair, and the margin was being consumed.

**Why the guard fired.**  `bootctr: REFUSING to write - temp byte '' != '2'`.
An empty value from `$(dd … bs=1 count=1)` means NUL or a missing file.  The
script was building the temp file in **`/tmp`, while `post-boot.sh` runs it in
the background** (`/accounts/devuser/startup.sh > .startup.log 2>&1 &`) next to
the other boot scripts, so the build raced with whatever else was touching
`/tmp`.  The guard did its job — a zero write was prevented — but the reason was
invisible because every `dd` had `2>/dev/null`.

Deployed fix (3 083 B, old one kept as `startup.sh.bak2-20261027`):

* temp file is `/accounts/devuser/.bc32.bin`, **not `/tmp`** — same directory as
  the log, which is demonstrably writable at that point in the boot;
* `dd`'s stderr is captured and, on refusal, logged together with
  `sed -n l` of the file, so the next failure explains itself
  (`dd1='…' dd2='…'`);
* the success line reports the **pre-write** value (`was '('`), not the readback
  it just checked — the old line printed the post-write value twice.

**How to verify the boot path without doing anything special:** the next
TabletOS boot decrements the counter to `0x31`, so `post-boot.sh`'s run should
exercise the *write* branch and log `wrote 0x32, readback ok (was '1')`.  If it
logs `already 0x32`, that boot simply did not decrement; if it logs `REFUSING`,
the new message carries the dd errors and the file rendering.
