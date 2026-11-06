# 174 — The RPMB test keys are per-unit; the reference's keys, and the two-stage `0x2E` seal

**Session 26 (absolute), Linux-era 20.  2026-11-04.**
Status: **read-only at the RPMB level** (reads/transforms only; no write).
The unit was restored to PlayBook OS 2.1.0.1917; `BOOTCTR_FCTRY = 0x32`.

This is the follow-up to report 173's option (c): re-open the Linux `pbmon`
seal with the bootloader's own sequence.  It found the real reason the seal
never reproduced: **the compiled test keys were the spare's, and `pbmon` calls
only `0x2F` while the bootloader's block transform is a two-stage
`0x2E`/`0x2F`.**

---

## 0. Headline

* **The compiled RPMB frame key was the spare's.**  `initramfs/rpmb.c`'s
  `rpmb_key` was `dfee c0ce…2274`; the **reference's** frame key is
  `16 11 b3 bb 0e ed aa cb e0 c1 55 b1 a5 45 cc cd 34 b3 f1 ad 78 f2 9a 37
  5c f0 c4 68 f7 9f d9 22` (report 156, `page+0x460`).  Offline, this key
  reproduces the **live** frame MAC of this unit exactly
  (`HMAC(key, msg284) = bcc563ed…ad34`).  After the change, `probe` prints
  `KEY PINNED` and `blockscan` reports **512/512 blocks read/MAC-ok** (was
  **0/512**).
* **The compiled block-HMAC key was the spare's too.**  It ended `46 28 01 b4`
  (the spare); the reference's `seed[0:16]` ends **`2b e1 1d a8`** (report 156).
  Both keys were corrected in `initramfs/rpmb.c`.
* **The keys are per-unit, not bootloader-derived.**  The frame key and the
  block seed are CID/provisioned per unit; only the bootloader code (and the
  passphrase, below) is shared.
* **The transform is two-stage.**  The bootloader's unseal is
  `0x2E` (decrypt `frame+0xE4` in place, key `page+0x20`, IV `frame+0x10`,
  ivlen 32) → compare `frame+0x10` with `frame+0x1C4` (the 32-byte tag) →
  `0x2F` (transform to the caller's buffer, key **`page+0x60`**).  The seal is
  the reverse.  `pbmon` issues only `0x2F`, and preloads it with
  `page+0x20` — **the wrong key for that stage**.
* **A faithful unseal needs the 32-byte tag**, which the current
  `/dev/pbunseal` (224-byte) interface does not carry.  This is a driver
  rewrite, not a one-line change.

---

## 1. The reference page material (this unit)

`docs/reports/data/session21-capture/page_block003b.bin` (4 KiB, captured on
the reference):

| page offset | value | role |
|---|---|---|
| `+0x20` | `86 78 7c 2a 63 2f be 16 48 ec ab a6 d8 84 01 09` | the `0x33` key the `0x2E` stage uses (`page+0x20`) |
| `+0x40` | `44 3a c2 02 eb 50 05 1f 0c 2c 12 b3 4a c9 3b 4d` | second key in the page |
| `+0x60` | `00 …` (zero) | the `0x2F` stage's key (`page+0x60`) at read time |
| `+0x440[0:32]` | `15 00 00 01 4d 43 47 41 46 41 10 00 2b e1 1d a8` + `3e 00 00 00` + `"ing in the o"` | the **32-byte seed** (upper half is **not** zero) |
| `+0x460[0:32]` | `16 11 b3 bb … d9 22` | the **frame key** |

**Consequence for `cid_seed32()`.**  It fills only the CID-derived first half
and leaves `seed[16:32]` zero; the real seed's upper half is the passphrase
fragment, so the `cid`/`literal` candidates in `try_seed()` can never match.
The frame key is a stored per-unit value, not a `f(seed)` candidate.

---

## 2. The passphrase, and the `0x33` key

The bootloader contains the RPMB passphrase at media `0x1D73E`:

```
retreating is advancing in the other direction
```

and **immediately after it** the `0x33` key `86 78 7c 2a 63 2f be 16 48 ec
ab a6 d8 84 01 09` (media `0x1D76C`-ish), i.e. the value `pbmon` already uses
for the `0x2E` stage.  `rpmb.c`'s `lit_seed` is only the 32-byte prefix of the
passphrase (`"retreating is advancing in the o"`); the full string is 49 bytes.

---

## 3. The two-stage seal/unseal, decoded

Corrected addresses (report 173: subtract `0x9000` from report 150/151 code
addresses).

**Unseal, `0x80008048` (report `0x80011048`):**

1. if `[0x80025F00] == 1`: `0x31(mode 1)` (open).
2. `0x2E` (wrapper `0x8000E758`): descriptor
   `{u16 op=3, u16 0, src=frame+0xE4, srclen=224, iv=frame+0x10, ivlen=32,
   key=page+0x20, keylen=16}`, SMC args `{desc, 1}`; domain 5.
3. `memcmp(frame+0x10, frame+0x1C4, 32)` — the tag check; mismatch ⇒ tamper.
4. `0x2F` (wrapper `0x8000E83C`): src=`frame+0xE4`, dst=caller buffer,
   key=**`page+0x60`**, len 224, `dir=0`, `op=0`, flags `|0x400`.
5. `0x31(mode 0)` (close).

**Seal, `0x80007F58` (report `0x80010F58`):** the reverse —
`0x31`, then `0x2F` (plaintext → `frame+0xE4`, key `page+0x60`), then `0x2E`
(`frame+0xE4` → ciphertext in place, IV `frame+0x1C4`, key `page+0x20`), then
`0x31`.  The 256-byte media block is `reverse(frame+0xE4 .. +0x1E3)`.

**What `pbmon` does (and why it fails):** it calls `0x31`/`0x33`/`0x2F` with
`page+0x20` for the `0x33` preload, and never calls `0x2E`.  It also has no
place for the tag (`frame+0x10`/`frame+0x1C4`).  So the service returns 0 but
the output is not the plaintext, and `blockscan`/`keytest` never see the record
magic.

### Why the domain was not the variable

`pb_dom` (a new runtime module parameter, default 5) was tested at 5 and 4:
both gave `512/512` MAC-ok and **0 hits**.  So the domain is not the fix
(consistent with reports 147/148).  The `0x2E` stage and the `0x2F` key are.

---

## 4. Driver/tool changes in the tree

* `initramfs/rpmb.c`: `rpmb_key` → the reference frame key; `block_key` and
  `keytest_keys[2]` → the reference seed.  Verified: `probe` `KEY PINNED`,
  `blockscan` 512/512 MAC-ok.
* `local/kernel-tree/drivers/misc/pbmon.c`: `pb_dom` module parameter (default
  5) for the transform domain.  The `pr_err` per-transform log
  (`pbmon: xform …`) produced 543 console records; make it `pr_debug` before
  any further sweep.
* The `0x2E` stage is **not** implemented yet.

---

## 5. What is next

1. Implement the two-stage `pb_xform`: extend the input to **256 bytes**
   (`C` + 32-byte tag) for the unseal, and use `page+0x60` for the `0x2F`
   stage and `page+0x20` for `0x2E`.  Seal takes the 224-byte plaintext and
   produces the 256-byte block.
2. Re-run `blockscan` (should find the record magic) and `pairtest`/`sealtest`.
3. Only then the single verified `BOOTCTR_FCTRY = 0x32` top-up (operator).
4. Silence the `pbmon: xform` `pr_err` before any large sweep.

**Bench state at the end:** 2.1.0.1917 running (1917 IFS `b52195bb…`, `/base`
mounted), `BOOTCTR_FCTRY = 0x32` (it had drained to `0x2F`; the TabletOS boot
topped it up), door up.

---

## 6. Sources

* `docs/reports/data/session21-capture/` (the reference page/frame/pair), report
  156 (the same-boot pair and keys), reports 143/144/147/148, report 173 (the
  load base).
* `local/proprietary/device-binaries/emmc-first1mb.bin` (the bootloader
  passphrase and `0x33` key at media `0x1D73E`/`0x1D76C`).
* `initramfs/rpmb.c`, `local/kernel-tree/drivers/misc/pbmon.c`.
