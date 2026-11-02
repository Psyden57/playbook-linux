# 70 — RIM bootrom stage on eMMC (`hd0` 0x14900) — first pass

Status: first-pass map of the RIM second-stage bootloader, aimed at the
signature/verification path. Date: 2026-10-20.

Target: `/home/psyden/rea-work/bootdumps-2026-09-11/hd0head.bin`
(`hd0` offsets 0x0-0x200000, 2,097,152 bytes). Load base inferred as
**0x80000000** (file offset == address offset): the boot ROM's metrics pointer
`0x80014674` lands inside this image, and Thumb disassembly at `0x80014900`
is coherent while ARM is not.

Provenance: LLVM 23 dual-mode disassembly + Ghidra 12.1.4 headless
decompilation (targeted TMode seeding). This is **not** REA MCP Evidence.

## 1. Image layout

| offset | content |
|---|---|
| `0x0` | MLO-style header: `{0x4000, 0x4d70, 0, 0, 0}`, name `"MLO"` at `0x14` |
| `0x200` | `"CertPK_"`, `0xa48 "PK_SIG_INFO_ROM_PKC"` |
| `0xc00` | `"CertPPA"`, `0xcc4 "PPA_SIGINFO_ROM_PKC"` |
| `0x4000` | `"CertISW"` certificate block |
| `0x8000` | code begins |
| `0x143f8`-`0x148d0` | region-verification strings and code (Thumb) |
| `0x14900` | `seeded_80014904` — bootrom main (`movs r0,#0; b 0x80014122` stub) |
| `0x1d044`/`0x1d050` | metrics block, `"RIM BlackBerry Device"` |
| `0x20000` | 0xFF fill |

The stage logs `Starting RIM bootrom with UART_DEBUG enabled`, `OMAP4430 HS`,
`SW_RESTORE support enabled`, `RPMB not provisioned`, `FACTORY`/`PRODUCT`,
`BOOT_MODE: %s`, `BootModeCount expended`, `Leaving bootrom and entering OS`.

## 2. Mixed ARM/Thumb — important caveat

The stage is **interleaved**, like the boot ROM. Confirmed by byte decode:

- **Thumb:** `0x80014148` (region verifier), `0x800142a8` (verifier main),
  `0x80014610`, `0x8001468c`, `0x800146f4`, `0x80014716`, `0x80014788`,
  `0x80014904` (bootrom main), `0x80013fb6`, `0x80013f64`.
- **ARM:** `0x80013770` (`push {r4,r5,lr}` = `e92d4030`), `0x8001404a`
  (`push {r4-r8,lr}` = `e92de9f0`), `0x80018688` (`e92d40f0`),
  `0x8001a7b4`+ (bignum/crypto area).

A Thumb decompile of the ARM regions is meaningless. The verifier analysis below
is Thumb-only and did decompile coherently (it reaches the NVRAM calls and the
log strings), so it is treated as valid.

## 3. NVRAM `0x2074` is the OS-region table (`NvOSRegionStruct`)

`seeded_800142a8` reads record `0x2074` into a `0xC1`-byte buffer. The size
check is `size - 0x18 < 0xA9`, i.e. accepted size is **0x18..0xC0**; anything
else logs `Invalid NvOSRegionStruct (size %d, expected %d)` and substitutes a
zeroed 0x18-byte record.

Observed layout (byte offsets in the record):

| offset | meaning |
|---|---|
| `+0x04` | boot mode / requested region, bit 0 (`0`/`1`) |
| `+0x05` | last verified region index |
| `+0x06 + 2*i` | u16 per-region boot counter, i = 0,1 |
| `+0x0A` | u16 max boot count (`0` = unlimited) |

Verification loop (two regions):

1. Log `Verifying region %d`.
2. If `counter[i] < max` or `max == 0`, call `seeded_80014148`.
3. On success (`== 1`): log `Verified region %d successfully`, **clear the other
   region's counter**, increment this counter, store the index at `+0x05`, and
   **write `0x2074` back** (`0x8000a9fe`).
4. Otherwise toggle region and repeat; after both, log
   `Verification of regions failed` and return 0.
5. If the counter is at/over `max`, log `Exceeded boot count (%d >= %d)`.

This is the structure `resetqnxosregion`/`0xC0` manipulate; it is an
anti-rollback/boot-count record, **not** the image signature itself.

## 4. Per-region verifier (`seeded_80014148`, Thumb)

- Validates `param_4` and the global at `0x80026144`; requires
  `*(global+0x24) == global` (self-pointer).
- Calls `seeded_80014610(global, param_4)` — structural validation only:
  - pointer range check `FUN_80013fdc`: aligned and within
    **`0x800ff800`-`0x800102b0`**;
  - `*(p+0x9c) == 0xC3D0E193`;
  - `length > 0x1FFFF`;
  - `*(q-8) == 0xD7C82D1F` (the OS signature cookie);
  - `*(q-4) != -1`.
- Reads NVRAM `0x205a` (8 bytes) and, if a bit selected by
  `(*(region+0xd4) & 0xff) < 0x20` is set, changes the compared value.
- Scans records via `FUN_8001569c(0xb, ...)` for one with `[2] == 0x11`; if
  `[3] == 1`, XORs `0x4000000`.
- **Anti-rollback/version compare:** `(value & 0xff00ffff) ==
  (*(region+0x7c) & 0xff00ffff)`. Bits 16-23 are masked out.
- Then one of two paths: `seeded_80014716` (mode 0) or `seeded_80014788`
  (mode 1; the one that walks the image in 10,000-byte chunks and compares a
  trailer).

## 5. The trailer check is a 32-bit equality — flagged, not yet concluded

`seeded_80014788` (Thumb) does:

```
stored = *(u32 *)(region_end - 4);
if (stored == -1) { status = 7; }
else {
    compute a digest over (region_end - region_start - 4) bytes
    if (computed == stored) return 1;   /* verified */
    status = 5;
}
```

The assembly is unambiguous at `0x80014838`: `cmp r4, r7; beq success`, where
`r7 = *(region_end - 4)` and `r4` is the return of the digest finaliser
(`0x8001a7c8`). **Caveat:** the digest machinery (`0x8001a7b4` onward,
`0x80013770`, `0x8001404a`, `0x80018688`) is **ARM**, not Thumb, so it has not
been decompiled correctly yet. Do not conclude "CRC32, therefore forgeable"
until those ARM functions are mapped. Two possibilities remain:

- a 32-bit CRC/checksum compared directly (forgeable), or
- a wrapper whose 32-bit return is a status/handle and the real RSA/ECC check is
  in the ARM crypto area or the secure monitor.

The presence of `CertPK_`/`CertPPA`/`CertISW` blocks and a large bignum routine
at `0x8001a7d4` makes a real asymmetric check likely somewhere in the chain.

## 6. NVRAM `0x2004` magic skips the second verification loop

In `seeded_80014904` (assembly confirmed):

```
movw r0, #0x2004
bl   read_nvram                 ; 4-byte record
ldr  r0, [sp]
cmp  r0, #0xAAAAAAAA
beq  LAB_80014a90               ; "Leaving bootrom and entering OS"
```

The branch is reached only after the first `0x80014548` (mode-0) verifier call
returns 1 and several pointer checks pass. So `0x2004 == 0xAAAAAAAA` does not
skip the whole chain — it skips the **second** (`0x8001453c`, mode-1) loop and
enters the OS directly. What that second loop does (load vs re-verify) is not
settled. **No loader opcode writes `0x2004`**; whether a CFP command or the OS
updater can set it is unknown.

## 7. Candidate bug surfaces (ordered by promise)

1. **ARM crypto area** `0x8001a7b4`-`0x8001c...`: map ARM/Thumb properly first.
   This decides whether the trailer check is cryptographic.
2. **TOCTOU between verify and use:** the verifier's valid window is
   `0x800ff800`-`0x800102b0`; the image is walked in 10,000-byte chunks. If the
   loaded header/TOC can change between the verify pass and the OS jump, the
   hash covers different bytes than the loader executes.
3. **`NvOSRegionStruct` variable size (0x18..0xC0):** the counters/index are
   within 0x18, but the rest of the record is used by the caller; check every
   field access against the actual size.
4. **Anti-rollback mask `0xff00ffff`:** bits 16-23 are ignored; determine which
   version field that is.
5. **`0x2004 == 0xAAAAAAAA`** semantics; find who writes `0x2004`.
6. Boot-count logic: `max == 0` means unlimited verification (not skip); the
   `counter >= max` path only logs.

## 8. Next steps

1. Build a mode map for `0x80008000`-`0x8001D044` (dual-mode disassembly per
   range, as in `60-mode-map-resolution.md`).
2. Decompile the ARM crypto functions and identify the digest/verify primitive.
3. Trace `seeded_80014716` (`0x80018688`) and `seeded_80014788` to their
   secure-monitor calls, if any.
4. Find writers of NVRAM `0x2004`/`0x2040` (CFP, OS updater, loader opcodes).
5. Only then assess whether a crafted NVRAM/region structure can pass.

## 9. Artifacts

- `analysis/rea-ghidra/hd0-first-pass/hd0-decompiled.txt` — Ghidra pseudocode
  for the Thumb verifier functions (`0x80014148`, `0x800142a8`, `0x80014610`,
  `0x8001468c`, `0x800146f4`, `0x80014716`, `0x80014788`, `0x80013fdc`,
  `0x80014904`).
- `analysis/rea-ghidra/hd0-first-pass/hd0-crypto.txt` — the (invalid) Thumb
  decompile of the ARM crypto area, kept as evidence of the mode-map problem.
- `analysis/rea-ghidra/hd0-first-pass/SeedStarts.java`, `hd0starts.txt`,
  `hd0crypto.txt` — the seeding script and address lists used.
- `hd0head.bin` is the source dump (`hd0` 0x0-0x200000).
