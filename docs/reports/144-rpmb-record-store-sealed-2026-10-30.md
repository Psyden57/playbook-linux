# 144 — The RPMB record store is sealed by the secure monitor

**Session 19 (absolute), Linux-era session 13.  2026-10-30.**
Image used for the read-only probe: **`0x9CE309CF`** (`pbrpmb` with `scan`/`dump`
diagnostics).  **No RPMB write was issued.**

This report corrects report 141's feasibility verdict.  The frame key and the
block key are pinned (report 143), but the **224-byte record block itself is
sealed on media by the secure monitor**; the MACs verify over the ciphertext.
A plaintext patch of the received block — report 141 §7.1's write recipe — would
corrupt the record.  **The Linux top-up cannot be done that way.**

---

## 1. The scan: every block authenticates, none is plaintext

`pbrpmb scan 512` on `0x9CE309CF`:

| result | count |
|---|---:|
| blocks read and frame-MAC verified | **512** |
| frame-MAC failures | 0 |
| read failures | 0 |
| blocks containing the record magic `2d bc 83 59` (either byte view) | **0** |
| blocks containing `BOOTCTR_FCTRY` anywhere (either view) | **0** |

All 512 x 224-byte blocks are high-entropy.  Blocks 5–15 share the same first
16 bytes (`8c82dfb7 5d62a9d8 9e7047e1 69cf29c5`) — consistent with the eleven
`bdevcert_dat*` records, each alone in a block and each starting with the same
magic+name prefix.

So the store is not plaintext, and it is not a simple transform of it that the
frame MAC exposes: **the block is encrypted at rest.**

## 2. The resmgr unseals it with `smc` service `0x2F`

Static RE of `rpmb-omap4` (link-time vaddrs; report 92's `0x1xxxx` addresses are
Ghidra, −0x10000):

* **Read path `0x1f58`** (block read): after the frame MAC (`0x1988`) it calls
  `0x2aa4(blockbuf, 0xE0, frame)`; `0x2aa4` verifies the **block HMAC over the
  ciphertext** (`0x26c4`, key `page+0x440`, 0xE0 bytes) and then calls the
  **`0x2F` marshaller `0x2530`** with `data = frame+0xE4` (ciphertext),
  `out = blockbuf`, `len = 0xE0`, `direction = 0` (unseal).
* **Write path `0x1c00` → `0x2bf0`**: the same `0x2F` call with
  `data = plaintext`, `out = frame+0xE4`, `len = 0xE0`, `direction = 1`
  (seal), then the block HMAC `0x2e` over the **ciphertext** and the MMC write.
* **Key:** the `0x33` preload takes `page+0x20`; the live page holds
  `86 78 7c 2a 63 2f be 16 48 ec ab a6 d8 84 01 09` — the `.rodata 0x5A94`
  constant (report 92 §2.4); report 141 §3.2's "page+0x20 is zeros" is wrong.
  `page+0x40` holds `.rodata 0x5A84`.
* Descriptor (`0x2530`): `+0x00 = 1`, `+0x02 = op | 0x400`, `+0x04 = direction`,
  `+0x0c/+0x1c/+0x24 = length`, `+0x18 = phys(data)`, `+0x20 = phys(out)`,
  `+0x10 = 0` after the key load, `+0x28/+0x2c = key2/key2len` **only for
  op = 1**.  The block path uses **op = 0** (`0x29cc` returns 0 for
  "Winchester"); the `0x415` slot therefore carries no key2 and the cipher has
  no caller-supplied tweak.

## 3. A known plaintext/ciphertext pair proves the seal

From the same live-QNX page dump (`docs/reports/data/rpmb-page-0xbfb2b000-…`)
the **block buffer at page+0x700** held the decrypted block for
**block 0x43** — three records, `bgroupcrt_dat8` (0x2c), `fsecsyskey` (0x20),
`drmclock.cfg` (0x20).  The on-media bytes of block 0x43 read from Linux are
different throughout:

```
P (page+0x700) : 2dbc8359 6267726f 75706372 745f6461 74...
C (block 0x43) : af02e299 020bfbf7 d60a5e82 3c323833 ...
```

Data: `docs/reports/data/rpmb-seal-block0x43-2026-10-30.txt`.

Two corrections to report 141 that fall out:

* the record header's block and length fields are **little-endian** (`0x43`,
  `0x2c`), not big-endian;
* the "block buffer" at `page+0x700` is the **decrypted** block; `page+0x800`
  is the pack buffer.

## 4. The cipher is not one of the standard ones with the known keys

Against the exact 224-byte pair (P, C) I tested AES-128/256 in ECB, CBC, CTR,
CFB, OFB and XTS, with keys `page+0x20` (0x8678…), `page+0x40` (0x443a…), the
16-byte seed, the 32-byte seed, both halves of the frame key and the
concatenations, and with IVs/tweaks from the page (`0x000…`, `0x43` tag,
`page+0x400/0x420`, the first ciphertext/plaintext block), plus byte-reversed
and word-swapped variants.  **No combination reproduces the pair.**  Whatever
`0x2F` does — a monitor-held key mixed with the `0x33` constant, or a
non-standard mode — is not reproducible offline.

The identical first ciphertext blocks of the `bdevcert_dat*` blocks show the
cipher is deterministic for identical plaintext prefixes (a block cipher with a
fixed IV/tweak, or a constant-keystream stream mode); it does not help without
the key.

## 5. Consequences for the top-up

* **Report 141 §7.1 step 3 is invalid.**  Patching the *received* block changes
  ciphertext, so the monitor's unseal yields garbage; the bootloader would see a
  damaged record (or a wrong value) — the brick path.
* **The Linux top-up now requires the monitor.**  Report 141 §7.3 option 2 —
  call the recovered `smc #1` ABI (`0x33` key load + `0x2F` seal) from Linux —
  is the only Linux-native route.  The ABI is fully recovered (§2) and the key
  is public, so it is implementable; the natural home is a small Linux module
  (physical descriptor + `smc #1`), and it can be **validated read-only** by
  sealing block 0x43's plaintext and comparing with the on-media bytes.
* **What still stands:** the frame key and block key (report 143), the
  authenticated read path, block scanning, the write-counter check, and the
  fact that `pidin` proving `rpmb-omap4` runs means every block authenticated.
* **Do not issue a plaintext write.**  Until the seal step is solved, the only
  proven writer remains QNX's `/dev/rpmb`.

## 6. Next

1. Implement the `0x33` + `0x2F` monitor call in a Linux module and validate it
   by sealing the known block-0x43 plaintext to the media bytes.
2. Only then replace `pbrpmb`'s plaintext patch with a seal, and do the one
   `0x32` write (report 143 §7 gate, plus the counter).
3. Alternatively keep QNX as the writer (the current proven path) and treat the
   Linux top-up as a later project.
