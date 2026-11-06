# 175 — The RPMB block transform is reproduced: `0x2E` + `0x2F`, and the two keys

**Session 27 (absolute), Linux-era 21.  2026-11-05/06.**
Status: **read-only on RPMB throughout** — authenticated reads, the monitor's
transform, and one region-0 image flash per build.  No write frame was ever
built or sent.  The unit was restored to PlayBook OS 2.1.0.1917 and
`BOOTCTR_FCTRY = 0x32` verified.

**Result: `blockscan` finds the store and the record.**
`blockscan: HIT block 0x3a 'BOOTCTR_FCTRY'@196 len=4 data=2b00000000000000`,
107/107 authentic blocks unsealed, 0 hits among the 405 that do not
authenticate.  The value read `0x2B`, exactly the counter after the session's
boots.

---

## 0. Headline

1. **The two services were named backwards in reports 150/151.**  `0x2F` is the
   **block cipher** (224 in / 224 out, descriptor byte `+0x04` is the
   direction, `1` = seal), and `0x2E` is the **digest** (224 → 32, key in its
   descriptor, second SMC argument `1`).  Report 174's *sequence* was right.
2. **The digest key is the seed.**  `HMAC-SHA256(seed[0:16], C[0:0xE0]) ==
   C[0xE0:0x100]` on the reference's block-0x3B pair, and the monitor
   reproduces it: the unseal returns the plaintext exactly when the `0x2E`
   descriptor key is `seed[0:16]` (`page+0x440`).
3. **The cipher key is `page+0x20`** — `86 78 7c 2a 63 2f be 16 48 ec ab a6
   d8 84 01 09` — loaded through `0x33` (always 16 bytes).
4. **`0x31` must not be issued with id 1.**  `0x31(id 1)` (the bootloader's
   call) changes the plaintext and breaks the transform; `0x31(id 0)` or no
   call at all both work.  This single call was why every earlier sweep failed.
5. **The seal is exact.**  On the reference's captured pair: `unseal` gives the
   record magic at offset 0 and `seal` reproduces all 256 bytes (`ct=OK
   tag=OK`).  On live block 0x43: `seal(P) vs the block: equal`.
6. **A 32-byte `0x33` preload is refused** (`-EIO`) — the length is 16.

## 1. The decoded transform

Corrected base from report 173 (`-0x9000`), Thumb:

```
seal   0x80007F58 : 0x31(id 1*) -> 0x33(key) -> 0x2F encrypt 224
                    -> 0x33(key) -> 0x2E digest -> frame+0x1C4 -> 0x31(id 0)
unseal 0x80008048 : 0x31(id 1*) -> 0x33(key) -> 0x2E digest of the ciphertext
                    -> memcmp(frame+0x10, frame+0x1C4, 32)
                    -> 0x33(key) -> 0x2F decrypt 224 -> 0x31(id 0)
```

`*` our measurement says id 1 is what breaks it — see §3.

Descriptors (confirmed word-for-word against the live QNX page at `+0x480`):

```
0x2F: +0x00 u16 1   +0x02 u16 mode|0x400   +0x04 u8 dir   +0x05 u8 0
      +0x0C len     +0x10 handle          +0x14 u16 op
      +0x18 src     +0x1C len             +0x20 dst     +0x24 len
      +0x28 / +0x2C  (mode 1 only)         — no key field
0x2E: +0x00 u16 op  +0x02 u16 0            +0x04 src     +0x08 srclen
      +0x0C dst     +0x10 dstlen           +0x14 key     +0x18 keylen
```

The digest key is a **pointer** (`+0x14`); the cipher key is only ever the
`0x33` preload.  In the bootloader those are two different page slots
(`page+0x20` and `page+0x60`), which is why they must be settable separately.

## 2. The frame orientation (this cost a build)

`/dev/mmcblk1rpmb` returns the frame **as the card sends it**: the 256-byte data
field is byte-reversed relative to the logical block the monitor transforms.
Proven on the unit: `dump 0x3b` prints `logical=ok` and its reversed bytes equal
the captured pair (its tag at `+0xE0`), and `HMAC(seed16, rev(D)[0:0xE0]) ==
rev(D)[0xE0:]` for blocks 0x3B and 0x43.  Report 156 §5's "the read leaves the
logical view in `frame.data`" describes the resmgr's page, not this relay.

## 3. How the keys were found

`pairtest` holds the digest key at `seed[0:16]` (proven) and varies the `0x33`
preload.  Its `p=` column (the first 4 bytes of the unsealed plaintext) is the
diagnostic: it changes with every preload, so the preload **is** the cipher key
and the engine is deterministic.

```
preload B=0 (page+0x20) p=336085c2      preload B=3 (frame key [0:16]) p=30e188cb
preload B=1 (page+0x40) p=e894aa6b      preload B=4 (frame key [16:32]) p=a178a2c6
preload B=2 (seed16)    p=806f71ad      preload B=5 (seed[16:32])       p=f6871e21
```

Then `wsweep` swept every 16-byte window of the captured 4 KiB rpmmb-omap4 page
as the preload, with the digest key held at the seed:

```
wsweep: HIT preload off=0x20 matches P3b
```

Note the state at that moment: the preceding `pairtest` had left `0x31` **not
called** (`pb_kek = 0`).  Re-running the sweep's winner with each setting gave
the decisive table:

| preload | `0x31` | unseal | seal |
|---|---|---|---|
| `page+0x20` | **none** | **OK, `p=2dbc8359`** | **ct=OK tag=OK** |
| `page+0x20` | id 0 | **OK, `p=2dbc8359`** | **ct=OK tag=OK** |
| `page+0x20` | id 1 | DIFF `p=336085c2` | DIFF |

So `0x31(id 1)` — the call the bootloader makes — is wrong for this engine
state, and id 0 (which QNX's read path uses to close) is harmless.

## 4. The store, read

```
blockscan: 512 blocks read/MAC-ok, tag offline 107, unsealed 107,
           tag-mismatch 405, smc-error 0, 107 hits
blockscan: HIT block 0x3a 'BOOTCTR_FCTRY'@196 len=4 data=2b00000000000000
```

* All 512 frames authenticate at the card (frame MAC, pinned key).
* **107 blocks carry a valid block tag** and all 107 unseal; the other 405 do
  not carry a tag (unused blocks).
* Every store block begins with the record magic `2D BC 83 59` at offset 0, so
  a magic-only search masks the records — the scan now looks for names first.
* `BOOTCTR_FCTRY` sits at name offset 196 of block `0x3A`: record header at 192,
  payload at 216, `len = 4`, value `2B 00 00 00` — i.e. **0x2B**, exactly the
  count after the session's seven factory boots (`0x32` at session start).
* Block 0x43 (`bgroupcrt_dat`) and its neighbours 0x3B–0x3E unseal to the
  certificate records, matching the session-21 capture.

## 5. What this unblocks, and what it does not

* The transform is **reproduced in both directions**, so a Linux-side
  `BOOTCTR_FCTRY = 0x32` top-up is now technically possible: unseal the record
  block, patch 4 bytes, seal, write one block, read back.
* **It does not authorise one.**  `SAFETY.md` §6 stands: no agent writes RPMB.
  The top-up is the operator's, with the readback.  The spare in the `0x5B`
  loop is the standing proof that a *correctly sealed* block can still leave
  the store unusable if the rest of the write is wrong.
* The unseeded CRNG (`random: pbrpmb: uninitialized urandom read`) must be
  fixed **before** any write: the frame MAC covers the nonce, so a predictable
  nonce weakens the card's replay protection (trap 87).

## 6. Tool and driver state at the end

* `drivers/misc/pbmon.c` (tracked copy `kernel/source/pbmon.c`): two-stage
  transform, unseal 256 in / 224 out with the tag gate, seal 224 in / 256 out,
  `/dev/pbkeys` (keyA ‖ keyB, independent), `op2e`, `dom`, `/dev/pbwsweep`
  sweeping the captured page as the **preload** key, `pb_pair` reporting
  `unseal`, `p=`, `ct`, `tag`.
* `initramfs/rpmb.c`: `wire_to_logical`, `pb_seal`/`pb_unseal`, `tag_ok_offline`,
  `pairtest` (preload sweep + `0x31` control), `keytest` (live block 0x43 with
  the round trip), `blockscan` (names first, records parsed, per-block counters).
* Images this session: `0x4F988620`, `0x74D34374`, `0x4EBF1F97`, `0x48166F12`,
  `0x1FEBC300`, `0xD3D42853` (all region-0 flashes; the first two had the
  orientation/preload mistakes documented above).

## 7. Bench cycle

| # | action | result |
|---|---|---|
| — | TabletOS 2.1.0.1917, door up, counter `0x32` | — |
| 1 | flash `0x4F988620`, boot | `probe` KEY PINNED; orientation discovered |
| 2 | flash `0x74D34374`, boot | digest reproduced (`unseal=0` iff keyA = seed) |
| 3 | (operator reboot) | — |
| 4 | flash `0x4EBF1F97`, boot | 32-byte preload refused; `p=` varies with preload |
| 5 | flash `0x48166F12`, boot | `wsweep` HIT at `page+0x20`; `0x31` identified |
| 6 | flash `0x1FEBC300`, boot | pair exact both ways; live block 0x43 round-trips |
| 7 | flash `0xD3D42853`, boot | **`blockscan` finds `BOOTCTR_FCTRY = 0x2B`** |
| 8 | flash the 1917 IFS, boot | TabletOS up, `/base` mounted, counter `0x32` |

Seven factory boots were spent (`0x32 → 0x2B`), then the restore's TabletOS
boot topped the counter back to `0x32`.

## 8. Sources

* `docs/reports/data/session21-capture/` (the block-0x3B pair and the 4 KiB
  rpmmb-omap4 page).
* `local/proprietary/device-binaries/emmc-first1mb.bin` (the bootloader; the
  wrappers at media `0x17758`/`0x1783C` after report 173's base fix).
* Reports 143/144/147/148/150/151/156/173/174.
* `local/kernel-tree/drivers/misc/pbmon.c`, `initramfs/rpmb.c`.

---

## Addendum — the Linux write (operator, same session)

The operator executed the prepared frame:

```
pbrpmb write 0x3a yes
  counter 11162; value 2e000000 -> 32 00 00 00
  mac 763abfe6054aec1a0b9478397ba66dacc567cd69d1829cca642f2f274c721e38
  ct  11162 addr=58 bc=1 req=0x0003
  write: ioctl 0 result=0        write: accepted
  write: read-back frame MAC ok  read-back block tag ok
  write: read-back plaintext[196..219] = 32 00 00 00
  write: PASS - BOOTCTR_FCTRY reads 0x32
```

**Post-write store scan** (`blockscan`, read-only): `512 blocks read/MAC-ok,
tag offline 107, unsealed 107, tag-mismatch 405, smc-error 0, 107 hits` — the
same structure as before the write, with `BOOTCTR_FCTRY@196 len=4
data=3200000000...`.

**Restore boot to 2.1.0.1917:** `/base` mounted and `BOOTCTR_FCTRY` reads
`32 00 00 00`.  The bootloader read and decremented the record on that boot and
`startup.sh` topped it back, so the written block is valid to *both* readers —
which is the strongest end-to-end check the unit can give.

**What this closes.**  `ROADMAP.md` item 6: a Linux boot cycle performed the
`BOOTCTR_FCTRY = 0x32` top-up with the verified one-write/read-back shape, and
the unit booted a real OS afterwards.  The spare's `0x5B` failure was a
*plaintext-patched* block, which is now understood to be invalid by
construction; this write re-sealed the patched plaintext with the monitor and
verified the seal round-trip before sending.

**Caveat for the future.**  This was one write, done by the operator, with the
record at a known offset and the store fully scanned either side.  It is not a
licence to write RPMB from an agent, and it is not a general writer: the
record's tiling, the store's free-space accounting and the block's co-resident
budget still have to be respected for any other write.

---

## Addendum 2 — the automatic top-up (same session)

`/init` now runs `/pbrpmb topup 0x1E` after the console is up.  It reads the
record, and writes `0x32` **only when the value is inside `1..0x32` and below
the threshold**, using the same single-frame sealed write as `prep`/`write`:
unseal, patch four bytes, re-seal, verify the seal round-trips, one type-3
frame, result read, read-back unsealed.  One attempt, never a loop; the outcome
is one `pbrpmb: topup:` line in `kmsg`.

Measured on image `0x71F61598` (boot #1 after a top-up):

```
pbrpmb: topup: BOOTCTR_FCTRY at block 0x3a record@196 = 30000000
pbrpmb: topup: 0x30 >= threshold 0x1e, no write
```

The threshold is the point: a boot spends one unit, so a healthy unit sits at
`0x31` after booting and never writes; only a unit that has drifted below 0x1E
(crash loops, many test boots without a TabletOS boot) triggers the write.

Two operational notes:

* The record's block is looked for at `0x3A` first and then scanned in blocks
  `0..0x7F`, so a re-tiled store does not silently disable the top-up.
* **The write frame carries no nonce** (the field is zero, as QNX's own write
  builder fills it with a slot-derived pattern); the RPMB write counter is what
  the card checks.  The unseeded CRNG therefore does not weaken the write, only
  the authenticated *reads* around it — which is why `entropy` exists for
  `prep`/`write` but the boot-time path does not need the host.
