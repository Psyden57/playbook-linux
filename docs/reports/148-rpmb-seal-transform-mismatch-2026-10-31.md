# 148 — The monitor serves the seal ABI, but the transform does not reproduce the store

**Session 20 (absolute), Linux-era session 14.  2026-10-31.**
Status: **the caller problem is gone (report 147) and the whole seal ABI is
accepted — `0x12`, `0x31`, `0x33`, `0x2F` all return 0 — but the transform's
output does not match the device's own ciphertext/plaintext pair, and no
candidate key, framing or ABI variant found one.**  No RPMB write was issued.
The bench was restored to the sealed IFS and TabletOS.

Images: **`0xC71BEF44`**, **`0x476A084F`**, **`0x30CE1FEA`**, **`0xA3382D01`**
(the identity-era builds with `/dev/pboracle`, `/dev/pbkey`, `/dev/pbctl`,
`/dev/pbpair` and `pbrpmb keytest|pairtest|blockscan`).

---

## 1. The identity-era call works end to end

With the page at the reserved low page `0xA0200000` and true physical addresses
(report 147), the block-0x43 path runs and every service returns success:

```
pbmon: xform dir=0 op=0 kek=2 page=0xa0200000 0x12=0 0x31=0 0x33=0 0x2F=0
```

and the round trip is exact: `seal(unseal(C)) == C`.  So the veneer, the page
layout, the command pages, the descriptor, the clock setup and the key load are
all accepted — nothing about the caller is wrong.

The monitor also **writes back into the descriptor**, which is a useful
fingerprint.  After a call our descriptor reads:

```
+0x04 dir=0   +0x0c 0xE0
+0x10 0x004017C8      <- monitor-written, IDENTICAL to the live QNX page
+0x18 0x90200700      <- our 0xA0200700, i.e. its internal view is phys-0x10000000
+0x1c 0xE0
+0x20 0x90200800      <- our 0xA0200800
+0x24 0xE0
+0x28 0x00300000      <- monitor-written
```

`+0x10 = 0x004017C8` matching the live QNX page exactly says the monitor's
session/key handle for our call is the same one it had for QNX.

## 2. What the transform does not do

Against the device's own pair (P = QNX page+0x700, C = media block 0x43,
`docs/reports/data/rpmb-seal-block0x43-2026-10-30.txt`):

* `unseal(C) != P` and `seal(P) != C`, though the round trip holds.
* The key **is** used: changing `/dev/pbkey` changes the output (`d0a95319…`,
  `955180f4…`, `0fac64f9…`, …).
* `op = 2` is refused (`-EIO` for both directions); `op = 0` is right.
* `select_kek` before the transform changes nothing observable (index 0, index
  1 and no call all give `ret 0`, and none reproduce the pair).
* **All 512 blocks** (the partition is exactly 512 blocks: 0–511 read
  `result=0`, block 512 returns `result=4`) were unsealed with six framings of
  the 256-byte data field and five candidate keys: **no block produced the
  record magic `2dbc8359` or a record name.**  Framings tried: `D[0:224]`,
  `reverse(D[0:224])`, `D[32:256]`, `reverse(D[32:256])` (= `logical[0:224]`,
  the strongest candidate from report 143), `D[16:240]`, `reverse(D[16:240])`.
* Keys tried: `page+0x20` (`8678…`), `page+0x40` (`443a…`), `page+0x440[0:16]`
  (`1500…`, the block-HMAC key), both 16-byte halves of the frame key — each
  forward and byte-reversed — in 6 op/kek variants (60 pair tests).
* **Offline**, 10 560 AES operations (44 keys × ECB/CBC/CFB/OFB/CTR × 4 IVs ×
  six byte-order transforms of both P and C) reproduce neither P nor C.
  Report 144 §4's conclusion stands and is now stronger: the transform is not
  standard AES with any key material we have.

## 3. A second observation: the nonce fill went to zeros after a reboot

On the first identity boot, `0x12` wrote a real nonce (`w=low`, and the driver
logged `419e5ebf…`).  **After a warm reboot of the same image, every `0x12`
returns 0 but writes 16 zero bytes** (`nonce=00000000000000000000000000000000`
on all 536 logged transforms).  The service still "succeeds".  This is recorded
as a measured anomaly, not explained: either the monitor's RNG needs an
initialisation we do not perform and the first boot had it by accident, or the
nonce path degrades after a warm reset.  **It means a transform result must not
be trusted until the nonce fill is known to be live** — and it is another reason
not to attempt a write yet.

## 4. What the resmgr code says about the key (static, [I] for the pointer
identification)

* `0x2E`'s parameter block (`0x26c4`) is a clean
  `{u16 mode, u16 0, phys(data), len, phys(out), outlen, phys(ptr), lens}`
  structure; the read path calls it with `mode = 3`, `out = frame+0x10`,
  `outlen = 32`, and **loads the key through `0x33` from `[obj+0x30]` with
  length 16**.
* The read path's `0x2F` call passes `key33 = [obj+0x30]` as well; `r2` (the
  second pointer, used as the `0x2E` block's last address field) is
  `[obj+0x2c]`.
* Report 143 proved the block HMAC is `HMAC-SHA256([obj+0x30]'s bytes =
  seed[0:16], logical[0:0xE0])`.  So `[obj+0x30]` is the **seed**, and the same
  value is what the `0x33` preload hands the transform.
* The seed nevertheless does not reproduce the pair (§2), so either the
  transform mixes in something else (the `0x2E` path's second pointer, an
  engine state, or a monitor-held value), or the pair itself is not a pair.

## 5. Why the pair may not be a pair [I]

P was captured from the live page at 10:09 and C was read from media at 10:27 in
session 19.  The resmgr is a write-back cache: if block 0x43 was rewritten in
between, P and C do not correspond, and **no key can reproduce them**.  That
would not explain the 512-block scan failing (a correct key would still reveal
the store in some block), so the scan is the stronger negative — but the pair
should not be trusted as a gate until it is recaptured on one boot.

## 6. What would settle it (next experiments, in order)

1. **Recapture the pair atomically.**  On one TabletOS boot, read block 0x43's
   media bytes and dump the live `rpmb-omap4` page (page+0x700 plaintext,
   page+0x400 command pages, descriptor, and the *whole* 4 KiB this time —
   session 19 captured only parts).  Sweep **every 16-byte window of the page**
   as a key candidate against that block with `/dev/pbkey` + `blockscan`
   (a 4 KiB page is 256 candidates — one boot).
2. **Use `0x2E` as a known-algorithm oracle.**  It is HMAC-SHA256 (report 143),
   so calling it with a chosen input and key and comparing with a local HMAC
   pins the monitor's byte-order convention for the *page* (straight or
   reversed, and what `[obj+0x2c]` is).  That convention is then the one to use
   for `0x2F`.
3. Only with a reproduced pair: move `pb_xform` onto the confirmed convention,
   re-run the pair gate, and only then consider the single `0x32` top-up.

## 7. Bench state at the end

* Sealed IFS restored and verified: **`c72b0530…`, 0x2BFE10 bytes**.
* TabletOS up, door up, `startup.sh` 4490 B, **`BOOTCTR_FCTRY = 32 00 00 00`**.
* Region-1 ext4 root intact at raw `0x29000000` (`ext4` magic `0xEF53`, label
  `pbroot`).
* `pbrpmb write` remains compiled blocked; **no RPMB write was issued** in this
  session; the only tools added are read-only monitors.
