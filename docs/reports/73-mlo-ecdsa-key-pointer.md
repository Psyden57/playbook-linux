# 73 — MLO ECDSA key pointer resolved: +0x9000 load-time relocation

Status: **closes the top product-path item** in
`HANDOFF-2026-10-21-next-session.md` §4.4 and `71` §5.
Date: 2026-10-21.
Target: `hd0head.bin` (2 MB, SHA-256
`dda9d955003302adf1f0d41580c50c0c9035126487e7ac53793f4d38a3a23e3d`).
Tools: Capstone 5.0.7, Python P-521 arithmetic. No device or flash writes.

**Result: the key pointer is not broken. `hd0head.bin` is a
pre-relocation image whose absolute literals are `0x9000` below their runtime
values.** The image is linked at `0x7FFF7000` and loaded at `0x80000000`; a
load-time fixup adds `+0x9000`. With the fixup applied, every verifier literal
lands exactly on the secp521r1 key and curve constants.

```
0x800187D0: 0x800143D9  +0x9000 -> 0x8001D3D9  uncompressed P-521 public key (on curve)
0x8001B188: 0x800144E8  +0x9000 -> 0x8001D4E8  p = 2^521 - 1
0x8001B18C: 0x80014460  +0x9000 -> 0x8001D460  3        (the a term, a = -3)
0x8001B190: 0x800144A4  +0x9000 -> 0x8001D4A4  secp521r1 b (exact match)
```

The ECDSA product path is functional. The working unit's
`mode=product, count=0` read is consistent with it. The expired unit's
`0xFFFFFFFF` is a separate matter: its bootloader reader returns `2` and still
nukes, while the PlayBook ramloader (`LDR_01`) masks the sentinel as absent
(see `72` §3.1).

## 1. MLO header at `0x0`

The three 0x20-byte records match the OMAP Configuration-Header TOC item
layout (`SWPU231AP` §27.4.8.2, Table 27-47): `{u32 start, u32 size, u32 rsv,
u32 rsv, u32 rsv, char name[12]}`.

| Name | Start | Size | End | Block at Start |
|---|---|---|---|---|
| `KEYS` | 0x200 | 0x960 | 0xB60 | `CertPK_` |
| `PRIMAPP` | 0xC00 | 0x32B8 | 0x3EB8 | `CertPPA` |
| `MLO` | 0x4000 | 0x4D70 | 0x8D70 | `CertISW` |

Each block is `{header, signed payload}`: `CertISW` 0x350 + 0x4A20,
`CertPPA` 0x1DC + 0x30DC. The blocks are contiguous with zero padding. The
code starts at file offset 0x8000, inside the final 0x9000-byte header/cert
area; that is why the whole image is linked 0x9000 below its load address.

## 2. Verifier chain (normal product path)

Thumb disassembly, runtime base `0x80000000`:

```
0x80014904  boot main
0x800149be  bl FUN_8000911a            ; BOOTCTR_FCTRY: 0 product, 1 factory, 2 bad
0x800149c2  cbnz r0, 0x80014a16        ; nonzero -> nuke
0x800149c4  bl 0x80014548              ; verifier wrapper, flag = 0
0x800149d0  cmp r0,#1
0x800149d2  bne 0x80014a16             ; not verified -> nuke

0x80014548  wrapper: *DAT_800148f0+4 = 0; tail 0x800142a8
0x800142a8  driver -> 0x80014148       ; dispatcher
0x8001415e  bl FUN_8000911a            ; r6 = selector
0x80014254  cbnz r6, 0x80014260
0x80014256  bl 0x80014716              ; selector 0 -> SHA-512 + ECDSA
0x80014264  bl 0x80013fb6              ; selector 1 -> CRC factory gate
0x80014266  bl 0x80014788              ; selector 1 -> CRC-32
0x80014290  selector 2 -> status 0x0f
```

`0x80014716` validates the region pointers and calls
`0x80018688(start, end, flag)`. At `0x8001877c`:

```
8001877c  ldr  r1, [pc, #0x4c]     ; [0x800187D0] = 0x800143D9 (link-time)
80018780  add  r2, sp, #4
80018784  mov  r0, #0x85
80018788  bl   0x8001afec          ; parse_point(len=0x85, ptr, out)
```

`0x8001AFEC` reads the tag at `r1` and accepts `0x02/0x03/0x04`. Read without
relocation, the byte at `0x800143D9` is `0xF8`; after `+0x9000` it is the
`0x04` of a valid uncompressed P-521 point.

## 3. Relocation evidence

All four verifier literals resolve after the same `+0x9000`:

| Literal (file) | +0x9000 | File offset | Content |
|---|---|---|---|
| `0x800143D9` | `0x8001D3D9` | 0x1D3D9 | tag 04, X/Y on secp521r1 |
| `0x80014460` | `0x8001D460` | 0x1D460 | 17-limb big-int `3` |
| `0x800144A4` | `0x8001D4A4` | 0x1D4A4 | `b` (exact secp521r1 match) |
| `0x800144E8` | `0x8001D4E8` | 0x1D4E8 | `p = 2^521 - 1` |

Numeric checks (`check_reloc.py`):

- `p@0x1D4E8 == 2^521-1` (top limb `0x000001FF`, 64 bytes of `FF`).
- `b@0x1D4A4 == 0x0051953EB9...FD46B503F00` exactly (diff bits = 0).
- `const@0x1D460 == 3`.
- The point at `0x1D3D9` satisfies `y^2 = x^3 - 3x + b (mod p)`.

The three constants are contiguous at 0x1D460, 0x1D4A4, 0x1D4E8 — 0x44 bytes
apart, exactly the 17×u32 limb size the parser expects. The key is immediately
before them at 0x1D3D9. This is a coherent relocated data section, not
coincidence.

**Methodology note:** when reading *absolute literals* out of `hd0head.bin`,
add `0x9000` to get the runtime address. Branch/call targets are PC-relative
and are already correct at the runtime base. This is why the earlier static
pass mistook the key pointer for a code address.

## 4. Consequence

- The public-key verifier is **functional**; the key pointer, curve `p`, `b`,
  and `a=-3` are all present and correct after relocation.
- A product unit (selector 0) boots through `0x80014716` → `0x80018688` →
  `0x8001AFEC` → `0x8001B194` exactly as the code reads.
- The working unit's `0xDE` read (`mode=product, count=0`) is therefore
  consistent with its normal boot.
- The expired unit's `0xFFFFFFFF` still returns `2` from the bootloader reader
  (`0x800090CE`) and nukes; the PlayBook ramloader (`LDR_01`) masks it as
  absent (`72` §3.1).
  It is not evidence about the product path.

## 5. Remaining question (lower priority)

The fixup itself is not in the static image: no in-image writer to
`0x800143D8` was found, and the boot ROM has no hard-coded `0x80014xxx`
reference. The `+0x9000` is a load-time fixup; the exact agent/table (boot ROM
or first-stage image loader) is not yet located. Finding it is now an
implementation detail, not a verification blocker. A live read of
`0x800187D0` during the boot window would confirm it directly, but the numeric
evidence above already closes the product-path question.

## 6. Reproduction

Scripts and decompiles are in `analysis/loader-audit-2026-10-21/`:

```
python3 check_reloc.py                                   # p, b, 3, key-on-curve
python3 disarm.py ../../../bootdumps-2026-09-11/hd0head.bin 80000000 80018688 arm 60
python3 disarm.py ../../../bootdumps-2026-09-11/hd0head.bin 80000000 80014148 thumb 90
python3 find_p521.py                                     # full-image on-curve scan
```
