# 156 — A same-boot RPMB pair on the reference: both MACs verify, the `0x2F` transform still does not

**Session 21 (absolute), Linux-era session 15.  2026-10-31.**
Status: **read-only.  No RPMB, NVRAM or media writes.**  The pair was produced
by a normal record *read* (`cat /dev/rpmb/bgroupcrt_dat`), which changes only
the resmgr's in-memory page.

This is the recapture report 148 §6 asked for: a plaintext block and its
ciphertext read on **one boot**, with the live key material captured from the
same page.  It closes the `0x2E` (HMAC-SHA256) oracle and strengthens the `0x2F`
negative.

---

## 0. Headline

* `rpmb-omap4` is pid **49168**; its RPMB working page is vaddr
  **`0x78f47000`** / paddr **`0xbf0e7000`**, and the frame/out page is paddr
  **`0xbfb8e000`** (QNX paddrs, from `DCMD_PROC_PTINFO`).
* Reading `/dev/rpmb/bgroupcrt_dat` returned block **`0x3B`** (`result=0`, read
  response) and staged, on the same boot:
  * **P** = page `+0x700`, 224 bytes — record `bgroupcrt_dat`, `slot 0x3B`,
    `len 0xC8` (200); `24 + 200 = 224`.
  * **C** = frame `+0xE4`, 256 bytes — the 224-byte ciphertext plus its 32-byte
    block HMAC.
* **The block HMAC verifies:** `HMAC-SHA256(seed16, C[0:0xE0]) ==
  C[0xE0:0x100]`, with `seed16 = page+0x440[0:16] =
  15 00 00 01 4d 43 47 41 46 41 10 00 2b e1 1d a8`.
* **The frame MAC verifies:** `HMAC-SHA256(frame_key, C[::-1] || nonce ||
  BE(counter,addr,count,result,type)) == frame+0xC4`, with
  `frame_key = page+0x460[0:32] =
  16 11 b3 bb 0e ed aa cb e0 c1 55 b1 a5 45 cc cd
  34 b3 f1 ad 78 f2 9a 37 5c f0 c4 68 f7 9f d9 22`.
* **The `0x2F` unseal does not match standard AES/DES** with the visible key
  material: 36 162 AES variants and a DES/3DES set, no match (§4).
* No `/proc/<pid>/as` reads were used (trap 59).  The capture files are in
  `docs/reports/data/session21-capture/`.

## 1. Method

1. `pidin ar` → `rpmb-omap4` pid `49168`.
2. `pbmem 49168 map` / `page` (metadata devctls only), then
   `pbmem 49168 pt <vaddr> 1` for each 4 KiB mapping.  The working page is
   **`0x78f47000` → paddr `0xbf0e7000`**; the frame page is
   **`0xbfb8e000`**.
3. `memdump3 0xbf0e7000 0x1000` and `memdump3 0xbfb8e000 0x1000` (QNX
   `mmap_device_memory` physical reader).
4. `cat /dev/rpmb/bgroupcrt_dat` (read-only; rc 0), then re-dumped both pages.
   The page went from block `0x43` to block `0x3B`, i.e. the read staged the
   block that actually contains `bgroupcrt_dat`.

## 2. The pair

| item | value |
|---|---|
| record | `bgroupcrt_dat`, magic `0x5983BC2D`, `slot 0x3B`, `len 0xC8` (200) |
| **P** (page+0x700, 224 B) | sha256 `e44f7bc7fdad588719bbfc4610c05953c40fd3677649ab1582065e7341ef9bc6` |
| **C** (frame+0xE4, 256 B) | sha256 `1d2e6b1eb7dedf9594f9d6bccfcb00bdcf9a75ab9ae6b567d806c5fbd9bfffb4` |
| block HMAC | `C[0xE0:0x100]`, verifies over `C[0:0xE0]` |
| frame MAC | `frame+0xC4` |
| nonce | `23 15 e8 bf df 45 1a 42 c6 50 6f a3 7a 82 04 09` |
| counter / addr / count / result / type | `0` / `0x3B` / `1` / `0` / read (4) |

The record payload is a certificate chain (`CHAI`, `CERT`); the block is
exactly full (`24 + 200 = 224`), which is why `bgroupcrt_dat` is alone in it.

**The page is in the unseal direction after the read**: the descriptor at
`page+0x490` carries handle `0x004017C8`, `data = 0xafb8e0e4` (C, identity
address), `len = 0xE0`, `out = 0xaf0e7700` (P, identity).  That is the monitor's
`0x2F` call with `dir = unseal`.

## 3. The `0x2E` oracle: both MACs verify

The `0x2E` path is HMAC-SHA256 (report 143/148), so a same-boot pair with the
live keys settles its byte order:

```
block HMAC : HMAC-SHA256(seed16, C[0:0xE0])          == C[0xE0:0x100]   OK
frame MAC  : HMAC-SHA256(frame_key, C[::-1] || tail) == frame[0xC4:0xE4)  OK
             tail = nonce || BE(counter,addr,count,result,type)
```

Details that matter:

* the block HMAC is over the **logical** view (the 256-byte field as captured
  after the read);
* the frame MAC is over the **wire** view (`C` reversed) with the tail fields
  in **big-endian** order (the page stores addr/count/result little-endian:
  `3b 00`, `01 00`, `00 00`; the wire tail is `00 3b`, `00 01`, `00 00`; the
  type bytes at `0x1FE` are `00 04`, i.e. type 4 in wire order);
* the seed is **CID-derived** and per-unit: the first 12 bytes
  (`15 00 00 01 4d 43 47 41 46 41 10 00`) are the reference's, the last 4
  differ from the spare's (`46 28 01 b4`);
* the frame key is also per-unit; the reference's is the 32 bytes above.

This is the `0x2E` oracle **answered offline**: standard HMAC-SHA256 with the
visible seed/frame key, logical block / wire frame, big-endian tail.

## 4. The `0x2F` transform: still not standard AES/DES

With the same-boot pair and the reference's own keys, the offline search was:

| search | scope |
|---|---|
| AES | keys: seed, seed-reversed, SHA-256/MD5/SHA-1 derivations, frame key, both halves (fwd/rev), seed+half and half+seed; modes ECB/CBC/CFB/OFB/CTR; IVs zero/seed/frame halves/nonce/C0/P0/seed-rev/nonce-counter; 7 byte-order transforms of P and C (identity, reverse-all, per-16, per-8, per-4, and two compositions) — **36 162 trials** |
| DES/3DES | frame-key 24-byte slices, `seed+fk[:8]`, SHA-256(seed)[:24], DES with 8-byte slices |
| structure | `P XOR C` (not periodic), full reverse, per-word swap |

**No match.**  The visible seed verifies the block HMAC but does not produce
the `0x2F` mapping.  So the transform is **not standard AES/DES with the
normal-world key material**, even on a genuine same-boot pair — the session-20
"bad pair" caveat (report 148 §5) is now removed.  It is monitor-derived or a
non-standard engine; the monitor image, or the `pbmon` call on an armed unit,
is what can settle it.

## 5. Correction to trap 60: the orientation is direction-dependent

Trap 60 says "the record block is the reverse of the frame's 256-byte data
field, and both HMACs follow that view".  That is true for the **write/probe**
frame report 143 captured (`logical = reverse(frame.data)`), but **not** for a
**read response**:

* after a read, `frame.data` is already the **logical** view (the block HMAC
  verifies directly, no reversal), and the **wire** view is
  `reverse(frame.data)`, which is what the frame MAC uses;
* the direction is the reason: a write builds logical → seals → reverses to
  wire; a read receives wire → reverses to logical → unseals, and leaves the
  logical view in the buffer.

The invariant is therefore "block HMAC over **logical**, frame MAC over
**wire**, tail big-endian on the wire", not "frame.data is always wire".

## 6. Bench state

* No RPMB/NVRAM/media writes.  The record read changed only the resmgr's
  in-memory page (block `0x43` → `0x3B`); the unit is up, door up.
* Files: `docs/reports/data/session21-capture/`
  (`page_block003b.bin`, `frame_block003b.bin`, `p_block003b.bin`,
  `c_block003b.bin`, `README.txt`).

## 7. Next

* The `0x2E` half is done.  The `0x2F` half needs the monitor image or our
  `pbmon` driver — and `pbmon` needs an armed unit, because our CRC-sealed
  Linux image cannot boot on the product/ECDSA path.  **The operator's arming
  decision is the gate.**
* A further read-only option: capture more unseal pairs from other records
  (ECB/structure tests).  A hidden monitor key will not fall to that, so it is
  only worth doing if a structural test is wanted.
