# 71 — hd0 mode map + verification chain (CRC-32 vs public-key)

Status: closes items 1–4 of `HANDOFF-2026-10-20-next-session.md` §5.
Date: 2026-10-20. Target: `hd0head.bin` (`hd0` 0x0–0x200000, load base
`0x80000000`; SHA-256
`dda9d955003302adf1f0d41580c50c0c9035126487e7ac53793f4d38a3a23e3d`).

Provenance: LLVM 23.1.3 dual-mode disassembly, Capstone 5.0.7 cross-checks, and
Ghidra 12.1.4 headless (`ARM:LE:32:v7`, raw binary at `0x80000000`, targeted
`TMode` seeding). This is **not** REA MCP Evidence. No device or NVRAM writes
were made.

## 1. Corrected mode map (0x80008000–0x8001D044)

Method: Ghidra auto-analysis supplied a first-pass map; every ARM start and
every disputed boundary was then re-decoded in both modes with
`llvm-objdump` and the raw bytes. Where Ghidra and the bytes disagreed, the
bytes won. Two prior claims are wrong:

| Claim (source) | Correction |
|---|---|
| `0x8001404a` is ARM (`70` §2, "push {r4-r8,lr} = e92de9f0") | **Thumb.** Bytes at `0x8001404a` are `2d e9 f0 41` = Thumb-2 `push.w {r4-r8,lr}`. The whole verifier region `0x80013fb6`–`0x800148ac` is Thumb; no ARM island exists there. `e92de9f0` does not occur. |
| Ghidra's AutoAnalysis run `0x800139c4`–`0x800139e4` is ARM | **Thumb.** Bytes `00 20 a8 80 …` decode as `movs r0,#0; strh r0,[r5,#4]; ldrb r0,[r4,#3]`; ARM is incoherent. This was a bad function discovered from a stale pointer. |

Verified ARM islands (everything else in the code window is Thumb; gaps in the
Ghidra run table are unanalysed regions — some are literal pools, several are
real Thumb code that Ghidra never reached, e.g. `0x8001596E`–`0x80015AA4`):

| ARM range | Role |
|---|---|
| `0x800085A4`–`0x800085CC`, `0x800086D4`–`0x80008A60` | early split/arithmetic helpers (`0x800086D4` = `cmp r2,#3`; not boot-verification) |
| `0x80009078`–`0x800090A0` | **ARM `smc #0` wrapper** (`r6=0xFF`, `r12=0`); used by the RPMB/secure-storage path (§7b) |
| `0x800094DC`–`0x800094E4`, `0x80009FFC`–`0x8000A004` | CP15 / `bx pc` veneers |
| **`0x80013770`–`0x80013814`** | **CRC-32 update** (table-driven, `crc = table[(crc^byte)&0xff] ^ (crc>>8)`) |
| **`0x800175DC`–`0x80018640`** | **SHA-512** (init/pad/block loop + compression helper) |
| **`0x80018688`–`0x800187D0`** | **public-key region verifier** (hash + asymmetric check) |
| `0x80018AC8`–`0x8001C87C` | big-integer library (add/sub/mul/div, modular inverse, wNAF) |
| `0x8001951C`–`0x80019984` | SHA-512 compression function (`FUN_80019558`) |

The ARM/Thumb interleaving in `0x80018Axx`–`0x8001Axxx` is dense: e.g.
`0x80018B40`–`0x80018C86` is Thumb inside the ARM crypto block, and
`0x8001AF9C`–`0x8001AFE0` is a chain of 2/4-byte Thumb `bx pc`/ARM stubs. Do not
decompile across those addresses without the per-address mode.

Reproduce:

```
llvm-mc -triple=armv7-none-eabi -mcpu=cortex-a9 -filetype=obj wrap.s -o wrap.o
ld.lld -Ttext=0x80000000 wrap.o -o wrap.elf
llvm-objdump -d --triple=armv7-none-eabi   --mcpu=cortex-a9 --start-address=A --stop-address=B wrap.elf
llvm-objdump -d --triple=thumbv7-none-eabi --mcpu=cortex-a9 --start-address=A --stop-address=B wrap.elf
```

`wrap.s` is one `.incbin` of `hd0head.bin`; file offset == address offset.

## 2. The region verifier: two distinct formats

`0x80014904` (Thumb boot main) calls one of two wrappers, which set a
verifier flag in `*DAT_800148f0+4` and tail-call the shared driver
`0x800142a8`:

- `0x80014548`: `flag = 0` (normal boot path)
- `0x8001453c`: `flag = 1` (retry/recovery loop)

**The flag does not select CRC vs public key.** It is passed through
`0x80014716` into `0x80018688` as `param_3`, where it only chooses the hash
checkpoint cadence: `0` → every `0x4B00` bytes, no `FUN_80012516` abort poll;
`1` → every `0x80` bytes plus an abort poll. The algorithm is selected
*separately* by `FUN_8000911a()` at `0x80014254`/`0x80014260`:

- `0` → `FUN_80014716` → **SHA-512 + public-key verify**
- `1` → `FUN_80013fb6` (requires `region[0xd4] & 0xff == 6`) → `FUN_80014788`
  → **CRC-32 only**
- `2` → status `0xf`, failure

`FUN_8000911a` is a Thumb veneer (`movs r0,#0; b 0x800090ce`) that reads the
RPMB record `"BOOTCTR_FCTRY"` and returns `0` when the record is absent,
`1` when it exists with a value in `1..0x32`, `2` otherwise. So the public-key
path is the normal/product path, and CRC is **strictly factory-gated** — it is
not reachable on a product unit merely through the recovery loop.

### 2.1 CRC path (`FUN_80014788`) — the 32-bit trailer is CRC-32 (settled)

`FUN_80014788` (Thumb, `0x80014788`):

```
start = *(*(region+8));
end   = *(*(region+0xc));
stored = *(u32 *)(end-4);
if (stored == 0xFFFFFFFF) { status = 7; fail; }
crc = 0x8001a7b4();                       /* returns 0xFFFFFFFF */
for (chunks of 10000 bytes) crc = 0x8001a7c0(crc, p, len);
crc = 0x8001a7c8(crc);                    /* eor r0,r0,#0xFFFFFFFF */
if (crc == stored) return 1;              /* verified */
status = 5; fail;
```

The final compare is exactly `cmp r4,r7; beq` at `0x80014838`.

The primitive is standard CRC-32/ISO-HDLC:

- `0x80013770` (ARM): reflected table-driven update.
- `0x8001A7B4` (ARM, 8 bytes): `mvn r0,#0` → init `0xFFFFFFFF`.
- `0x8001A7C8` (ARM, 12 bytes): `eor r0,r0,#0xFFFFFFFF` → final XOR.
- The table is **generated at runtime** at `0x8004CD3C` by `0x80013ED4`
  (the word at `0x80013F2C` is the polynomial **`0x04C11DB7`**; a bit-reverse
  helper is at `0x80013F0C`). `0x800159F2` calls the generator. The generated
  table is the reflected form (effective poly `0xEDB88320`). This is why the
  table is 0xFF in the file: it is RAM, not image data.

So the CRC trailer is a forgeable integrity checksum, but it is only reached
on the factory path (RPMB `"BOOTCTR_FCTRY"` present) and when
`region[0xd4] == 6`, or from the recovery/timeout branch of `0x80014904`
(§3). It is **not** the product verification.

### 2.2 Public-key path (`FUN_80014716`) — SHA-512 + asymmetric verification (settled in kind)

`FUN_80014716` (Thumb) validates the region pointers and calls
`FUN_80018688(start, end, flag)`.

`FUN_80018688` (ARM, `0x80018688`–`0x800187D0`):

1. Validates `end-0x230` as a pointer and requires
   `*(u32 *)(end-0x178) == 0xB5A60BFD` and `*(u32 *)(end-0x210)` neither `0`
   nor `-1`.
2. Hashes `[start, end-0x230)` with `FUN_80018420`.
3. Parses a `0x85`-byte (133-byte) blob via `FUN_8001AFEC`.
4. Verifies with `FUN_8001B194(sig_block = end-0x208, parsed_pubkey, digest)`
   and returns `1` only if it returns `1`.

`FUN_80018420` (ARM) is **SHA-512**:

- copies the 16×32-bit IV at `0x80018640` / `0x80018648`…`0x80018680`, which
  is byte-for-byte the SHA-512 IV
  (`6a09e667 f3bcc908 bb67ae85 … 5be0cd19 137e2179`, verified word by word);
- 0x80-byte blocks, `0x80` pad byte, 8-byte big-endian bit length written at
  block offset `0x78`–`0x7F`;
- 64-byte (16-word) output, big-endian;
- per-block compression `FUN_80019558` (ARM, `0x80019558`–`0x80019984`);
- the 0x4B00 checkpoint interval is just a watchdog/abort check
  (`FUN_800094E4` / `FUN_80012516`), not a hash-length limit.

`FUN_8001AFEC` parses an SEC1-style point: tag byte `0x02`/`0x03`
(compressed) or `0x04` (uncompressed), then two 66-byte big-endian integers,
converted by `FUN_8001C2F0` into 17-word (68-byte) limbs. `0x85 = 1+66+66`.

`FUN_8001B194` performs the public-key operation:

```
z  = SHA512 digest (64 bytes) -> 17 limbs
copy pubkey[0]    -> Q.x (require nonzero)
copy pubkey[0x44] -> Q.y
w  = modinv(sig+0x44)                 FUN_8001BE94 (binary extended GCD) = s^-1
u1 = z * w                            FUN_8001BF5C
u2 = r * w                            FUN_8001BF5C
FUN_8001B730(out, u1, 5, table, u2, 3, Q.x)
      -> FUN_8001B280(2, scalars, out)  (two-scalar, signed-digit wNAF)
compare result with sig block         FUN_8001BED4
```

`FUN_8001B280` is a textbook interleaved wNAF multi-scalar multiplier: it
recodes two scalars into signed digits (`iVar6*2`, `±1`), walks a table of
affine points of stride `0x88` (136 = 2×68-byte coordinates), and selects
precomputed multiples. Modular inverse + two-scalar point multiplication +
66-byte integers + SHA-512 digest is the **ECDSA** family. The curve is
**secp521r1 (P-521)**: the QNX secure-world binary `trustzone-omap4` (in
`playbook-dev/dumped4869ifs/proc/boot/`) contains `secp521r1_b`,
`secp521r1_ma`, `SHA512_IV`, the `hu_SHA512*`/`husw_SHA512*` functions, ECDH,
and the KEK-selection strings. That matches the bootloader's 66-byte
coordinates, 68-byte limbs and SHA-512 digest. The bootloader's own generator/
order table was not extracted, but the curve identity is no longer in doubt.

No SMC is executed by either verifier chain. The only SMC veneers in the image
near the crypto are ARM stubs at `0x80019394`/`0x800193A8`/`0x800193BC`/
`0x800193D0`/`0x800193E4` (services `0x100`, `0x102`, `0x109`, `0x112`) plus a
Thumb wrapper at `0x800043B4` (`r6=0xFF`, `r12=0`, `smc #1`). No `bl`/`blx` or
literal reference to those veneers exists in `hd0head.bin`, so they are dead or
indirect here. The region signature check runs in the non-secure world.

## 3. Boot gating and NVRAM (0x2004 / 0x2040)

`0x80014904` (Thumb boot main) in order:

1. Reads NVRAM **`0x2040`** (u32, boot-mode/region counter).
   - `0`/`2`: `FUN_80014F84()` decides. Nonzero → jump straight to the
     **retry loop** (`0x80014A20`). Zero → continue.
   - `3..8`: continue. Anything else → nuke path.
2. Increments `0x2040`, sets `bVar1 = true`, writes `0x2040` back.
3. If the write succeeded and `FUN_8000911A() == 0`, runs the public-key
   verifier (via the `flag = 0` wrapper); on success it checks the region
   pointers, then reads NVRAM
   **`0x2004`** (u32):
   - `0x2004 == 0xAAAAAAAA` → `LAB_80014A90` (enter OS) **without nuke**;
   - otherwise → `0x80014A16`.
4. `LAB_80014A16` calls `FUN_800156E8` — the **"Nuking…"** routine: writes
   `0x2040 = 1`, erases the OS-region ranges, writes `0x2040 = 2`. Then, because
   `bVar1` is true, it falls into `LAB_80014A90` (enter OS) with the region
   erased.
5. If `bVar1` is false (retry path), it enters the `0x8001453c` loop; the
   same `FUN_8000911a` selector then decides public-key vs CRC. A successful
   result (`iVar3 == 1`) goes to `LAB_80014A3E` → enter OS.

Consequences:

- **`0x2004 == 0xAAAAAAAA` is not a verification bypass.** It is read only
  *after* the public-key verifier has returned success. Its effect is "the region
  is committed/bootable; do not nuke". Verification failure jumps to the nuke
  path *before* the `0x2004` read. The old hypothesis in `70` §6 ("skips the
  second verifier loop") is true only in the narrow sense that success skips
  the `LAB_80014A16` nuke; it does not skip the public-key verifier.
- The **CRC-only path does exist**, but it requires
  `FUN_8000911a() == 1`, i.e. the RPMB `"BOOTCTR_FCTRY"` record present with a
  value in `1..0x32` (factory/provisioning state) and `region[0xd4] == 6`. On a
  product unit (`FUN_8000911a() == 0`) the retry loop entered at
  `0x80014A20` still runs the public-key verifier, only with the `flag = 1`
  checkpoint cadence. So the 4-byte CRC trailer is not a product-path bypass;
  it is a factory-path shortcut that still requires an NVRAM state write to
  reach.
- NVRAM **`0x2040` writers** found in this image, all via `FUN_8000A9FE`:
  `0x800149AE` (incremented value), `0x80015700` (`=1`, start of nuke),
  `0x8001588C` (`=2`, end of nuke), `0x800158D6` (`=3`, state transition in
  `0x800158A2`). Reads at `0x80014974`, `0x800158AC`, `0x80015A22`.
- **No writer of NVRAM `0x2004` exists in any available image.** It is only
  read, at `0x80014A06`. The boot ROM (`0x40028000`), the lower boot ROM, the
  ramloader (`loader_06001A06-00.bin`), and the appliance partitions
  `hd4.bin`/`hd5.bin` contain no `movw #0x2004` and no NVRAM write of it.
  The writer must be the flash/update path that is not in the workspace (CFP
  `resetqnxosregion`, the OS updater, or the OS itself).

Full NVRAM ID use in `0x80008000`–`0x8001D044` (immediates fed to the record
read/write wrappers `0x8000A9D8` / `0x8000A9FE` and to `0x8001397E`):

| ID | Access | Sites |
|---|---|---|
| `0x2004` | read only | `0x80014A06` |
| `0x2019` | r/w | `0x80015360`, `0x8000CC60` |
| `0x2028` | r/w | `0x800155A0`, `0x800155DE` |
| `0x205a` | read | `0x800141F8` (8-byte region/type bitmask) |
| `0x2074` | r/w | `0x800142DC` read, `0x80014508` write (`NvOSRegionStruct`) |
| `0x2079`, `0x207a` | read | `0x80013CE0`/`0x80013D0E`/`0x80013DAE`/`0x80013E52`, `0x80013C44` |
| `0x2801`, `0x2802` | r/w, 0x70 B | password record (`NV_OSSTORE_PasswordStruct`/`Reserve`; the earlier "boot-info" name is superseded, see `75`/`77`) (`0x80014FA6`, `0x80014D88`, `0x8001587A`, …) |
| `0x2819`, `0x282F`, `0x280E` | read | `0x80013F98`, `0x80013A6E`, `0x80013A22` |

`0x2710` (10000) is the CRC/hash chunk constant, not an NVRAM ID; `0x2434` is
a length passed to `0x80018C9C`, not an NVRAM ID.

`FUN_8000A9D8` is the record reader and `FUN_8000A9FE` the writer; both route
through the NVRAM record table and are the same pair used for `0x2074`.

## 4. Certificates

`hd0head.bin` contains `CertPK_` at `0x200`, `CertPPA` at `0xC00`, and
`CertISW` at `0x4000`, but **this stage never references them**: the literals
`0x80000200`, `0x80000C00`, `0x80004000` do not occur, and the block addresses
are not loaded anywhere. The certificate blocks are consumed by the boot ROM
and/or the secure world, not by `0x80014904`. The only in-image key material
the stage points at is the (anomalous, §5) blob pointer at `0x800187D0`.

## 5. Residual unknowns

> **Resolved in `73` (2026-10-21).** `hd0head.bin` is a pre-relocation image:
> linked at `0x7FFF7000`, loaded at `0x80000000`, with a load-time `+0x9000`
> fixup. Add `0x9000` to absolute literals. The key pointer resolves to
> `0x8001D3D9` (valid secp521r1), with `p`, `b`, and `3` at `0x8001D4E8`,
> `0x8001D4A4`, `0x8001D460`. The product path is functional; no key anomaly.

1. **The public-key path's key/constant pointers do not resolve to data in this image.**
   - `0x80018688` loads its blob pointer from the literal at `0x800187D0`; the
     literal is `0x800143D9` (odd). `0x800143D9` is one byte into the verifier's
     literal pool (`0x800143D8` holds the valid-window low bound `0x800FF800`),
     and its first byte is `0xF8`, which `0x8001AFEC` rejects (tags 2/3/4).
   - `0x8001AFEC` compares the parsed X against `*(0x8001B188) = 0x800144E8`;
     `0x800144E8` is Thumb code (`strb.w r10,[r2,#6]`), not a bignum constant.
   - Both are single, unpatched literals in the static image.
   Candidate explanations, none proven: (a) the MLO is a multi-section image
   whose other sections populate `0x80040000`-range RAM and a key slot at
   `0x800143D8`, and the dump only covers the `0x0`–`0x20000` section;
   (b) a load-time relocation/fixup patches these words; (c) the public-key path is
   not exercised on this unit/build. The header at `0x0` has three 0x20-byte
   name records (`MLO`, `PRIMAPP`, `KEYS`) with the pairs
   `{0x4000,0x4D70}`, `{0xC00,0x32B8}`, `{0x200,0x960}` that have not been
   decoded. Resolving this needs the MLO loader format or a live read of
   `0x800143D0`/`0x800187D0` during the boot window.
   Until then, treat "SHA-512 + ECDSA-family verify" as structurally certain
   but "which bytes are the key" as unproven.
2. **Curve identity — resolved.** `trustzone-omap4` contains `secp521r1_b`,
   `secp521r1_ma`, `SHA512_IV` and the `husw_SHA512*` family, so P-521 is the
   working assumption. What remains is the bootloader's own generator/order
   table: `FUN_8001B730`'s `5`/`3` arguments and the `*0x8001B27C` table
   (`0x800267B8`, all `0xFF` in the file) are still not decoded.
3. **TOCTOU.** The region is hashed from RAM in 0x4B00-byte chunks and then
   verified; nothing in the stage remaps or freezes the region between the hash
   and the jump at `0x80014A90`. The window `0x800FF800`–`0x800102B0` is the
   only pointer gate.
4. **`NvOSRegionStruct` size 0x18–0xC0** (`0x800142A8`) remains variable; the
   counters/index are inside 0x18, but every field access beyond that was not
   audited against the actual record size.
5. **Anti-rollback mask `0xff00ffff`** (`0x80014148`): bits 16–23 ignored; the
   version field they belong to is still unidentified.
6. **Dead/indirect SMC veneers** at `0x80019394`–`0x800193F4` (services
   `0x100`/`0x102`/`0x109`/`0x112`): no caller found. If a runtime table calls
   them, they may be the secure key/hash service; verify on a live boot.

## 6. Answers to the handoff questions

- **Is `0x80014838` a 32-bit CRC or a status?** It is a direct 32-bit compare
  of a standard CRC-32/ISO-HDLC (`init 0xFFFFFFFF`, reflected table poly
  `0xEDB88320`, final XOR) against the u32 at `end-4`. It is forgeable.
- **Is there a real signature check behind it?** Not behind it, beside it.
  `FUN_80014716 → FUN_80018688` hashes the region with SHA-512, parses a
  133-byte SEC1-style public-key blob, and verifies the signature in the
  0x230-byte trailer against it with a two-scalar big-integer (ECDSA-family)
  operation. This is the product path; CRC is factory-only.
- **Where do `0x80014716`/`0x80014788` end?** `0x80014788` ends at the CRC
  compare `0x80014838`. `0x80014716` ends at `FUN_80018688`'s return.
- **Secure monitor?** No SMC in either chain.
- **Writers of `0x2004`/`0x2040`?** `0x2040` is written by this stage
  (`0x800149AE` increment; nuke writes 1 then 2). `0x2004` has no writer in any
  available image; it is a committed-image flag written by the update path.

## 7. Artifacts

Under `analysis/hd0-modemap-2026-10-20/`:

- `modes-corrected.txt` — final Ghidra mode runs + function list
  (20,881 instructions, `0x80008000`–`0x8001D044`).
- `verifier-decompiled.txt` — corrected Thumb verifier + ARM crypto
  decompiles (`0x80014716`, `0x80014788`, `0x80018688`, `0x8001AFEC`,
  `0x8001B194`, `0x8001C2F0`, `0x80019C08`, `0x80019C40`, …).
- `arm-decompiled.txt`, `aux-decompiled.txt`, `gate-decompiled.txt`,
  `bootlogic-decompiled.txt`, `nvram-decompiled.txt` — supporting decompiles.
- `auth-decompiled.txt` — `0x800090ce`/`0x80011690`/`0x80010a34` and the
  record-lookup chain.
- `rpmb-arm-decompiled.txt` — `0x80017758`/`0x8001783c`/`0x80017d24`/
  `0x80017c38`/`0x800183dc` (secure-storage requests and SMC dispatcher).
- `record-decompiled.txt` — `0x800090a0` (provision/delete), `0x800115e6`
  (create/update), `0x8001167e` (delete), `0x80011166`/`0x8001124e`/
  `0x80010b2e` (find/repack/write-back).
- `loader-rpmb-decompiled.txt`, `loader-rpmb2-decompiled.txt`,
  `loader-rpmb3-decompiled.txt` — the ramloader's matching record writer,
  BOOTCTR_FCTRY reader (0xDE), set/decrement helpers, and the containing
  dispatcher.
- `arm.txt` / `thumb.txt` — LLVM dual-mode listings of the whole code window.
- `wrap.elf`, `wrapl.elf` — base-`0x80000000` and base-`0x80100000` wrappers.
- `DumpModes.java`, `DecompileList.java`, `SeedCorrect.java`, `RefsTo.java`,
  `find_nvram.py` — tooling.

## 7b. Addendum — `BOOTCTR_FCTRY` is an authenticated RPMB record

User domain knowledge: `BOOTCTR_FCTRY` is a file/record in the **RPMB**
partition, not the plain NVRAM store, and its value is a factory-boot budget
that must be **1..0x32 (50)**. `0` and `>0x32` both fail. Writing
`FF FF FF FF` on a unit produced **"Expired factory mode"** in CFP and the unit
would not boot; the failure is not reversible through the normal interfaces.

The code agrees, and the mechanism is now resolved:

- `FUN_8000911a` (`0x8000911a`) is a veneer to `0x800090ce`, which reads
  `"BOOTCTR_FCTRY"` via `0x80011690(name, &value, 4, &len)`. The accepted range
  is checked at `0x80009100`–`0x80009114`: `1 <= value <= 0x32` returns 1;
  `value == 0` or `value > 0x32` returns 2. Status 2 sends `0x80014904` to the
  nuke path, which matches the observed failure.
- **Provisioning/delete:** `0x800090a0(1)` creates the record with the literal
  value `0x32` (50) via `0x800115e6("BOOTCTR_FCTRY", &0x32, 4)`; this is the
  path taken after `"RPMB not provisioned"` in `0x80014904`. `0x800090a0(0)`
  deletes it via `0x8001167e`. So 50 is the provisioning default, not an
  arbitrary cap. This stage only reads/provisions/deletes the record — it never
  decrements it (`0x800115e6` has exactly one caller, `0x800090c0`; `0x8001167e`
  exactly one, `0x800090a6`). The budget is consumed by the factory/CFP
  tooling, which is consistent with CFP reporting "Expired factory mode".
- The backing block is a packed record store with an 0x18-byte record header:
  `+0` magic `0x5983BC2D`, `+4` 14-byte name, `+0x16` u16 length, `+0x18`
  data. `0x80011166` locates the magic, `0x8001124e` repacks, `0x80010B2E`
  writes the block back.
- `0x80011690` is **not** the plain NVRAM API (`FUN_8000A9D8`/`FUN_8000A9FE`).
  It looks up a 0x1c-byte descriptor by 14-byte name in the table at
  `*0x80011530 = 0x8002C760` (count via `*0x80011534 = 0x80025FB0 + 4`), then
  calls
  `0x80010a34(0x80033F80, 0xE0, 0x80033760, 0x80025FB0, desc[+0x12])`.
  - Descriptor: `+4` 14-byte name, `+0x12` u16 RPMB address/index,
    `+0x16` u16 data length, `+0x18` u16 offset in the block.
  - After the block read it compares `desc+4` with `block+offset+4`
    (14-byte `memcmp` at `0x80018BB4`) and copies `desc[+0x16]` bytes from
    `block+offset+0x18`.
- `0x80010a34` is a **512-byte authenticated block read**:
  1. Gets a 16-byte nonce via `0x8001101C -> 0x80017D24`, which calls secure
     service **0x12** in 0x80-byte chunks into `0x8002A4E0 + 0x2200`.
  2. Builds the frame: zero 0x200 bytes, `frame[+0x1FE] = 4` (read request),
     `frame[+0x1F8] = desc[+0x12]` (RPMB address), nonce at `+0x1E4`.
  3. `0x80010770` byte-swaps the tail u16s and reverses the 0x100-byte data
     area (RPMB frame endianness).
  4. `0x8001080E -> 0x8000DF48` (request 4) and `0x80010CFC -> 0x8000D0EA`
     (request 1) drive the eMMC block layer.
  5. Checks `frame[+0x1FC] == 0`, re-compares the nonce (replay check), then
     computes the 32-byte MAC via `0x800107B6 -> 0x80017758(..., 3, 0, 0)`
     (secure service **0x2E**) and compares it with the stored MAC at `+0xC4`.
  6. On MAC mismatch, `0x80011048` writes the block back via
     `0x80017758(..., 3, 1, key+0x18)` -> service **0x33** (load 16-byte key)
     then service **0x2F** (authenticated write), and re-reads.
- All service calls go through `0x800183DC`, a thin wrapper over `0x80009078`
  = **ARM `smc #0` with `r6=0xFF`, `r12=0`**, exactly the SAR/monitor ABI in
  `65`. Services seen on this path: `0x12` (fill 0x80-byte buffer / nonce),
  `0x2E` (compute a 32-byte MAC over a 0x100-byte request; the caller
  compares), `0x2F` (0x160-byte authenticated read/write), `0x33` (load
  16-byte key), `0x31`
  (query/write-counter), `0x28`/`0x29`/`0x2A`/`0x0F` (fuse/OTP read in
  `0x80017FD8`), plus `0x2D`, `0x3C`, `0x0E`.

Consequences:

- The 14-byte tag compare is **not** the security boundary. The key is not in
  the static image: the `0x2E` request carries a 32-byte key/context pointer
  (`0x80033760`, a runtime buffer above the image); the MAC is produced by the
  secure monitor and compared by the caller, and the nonce comes from service
  `0x12`. Whether the secret itself lives in that buffer or inside the secure
  world is not settled. A tag-only patch or a forged value cannot pass unless
  that MAC/key path is broken.
- `BOOTCTR_FCTRY` is therefore **RPMB-gated**, not NVRAM-gated. The CRC-only
  factory path is not reachable by writing an NVRAM record; it needs a valid,
  monotonic, authenticated RPMB record. That record is writable from a rooted
  OS via **`/dev/rpmb`** (the OS's RPMB driver is the authorized
  secure-service caller; the user wrote `FF FF FF FF` this way over SSH). The
  gate is therefore the record *value* and the CRC chain, not a key we lack.
  The catch is asymmetric: a bad value prevents the OS from booting, which
  removes the writer — the `FF FF FF FF` unit proves it.
- The remaining RPMB-relevant question is a **reader/parser bypass** (can a
  forged block pass the MAC/nonce/length checks without the key), not a value
  write. The parser is `0x80011690`/`0x80010A34`; lengths and offsets are
  `u16` and the name check is a plain `memcmp`, so audit for offset/length
  confusion — but the MAC covers the 0xE0-byte payload, so a pure metadata
  rewrite still has to produce a valid MAC.
- The record table and block buffers live at `0x8002C760`, `0x8002A4E0`,
  `0x80033F80`, `0x80025FB0` — all above the 0x20000-byte file image
  (BSS/secure-shared). The record metadata is runtime-populated, not static
  file data.
- **The OS side is the same store.** `playbook-dev/dumped4869ifs/proc/boot/
  rpmb-omap4` is the `/dev/rpmb` resource manager (`iofunc_read_verify`/
  `iofunc_write_verify`, `dispatch_block`, record catalogue, metadata/block
  consistency checks, nonce). It derives the RPMB key from a hardware KEK
  (`RPMB key util: RPMB key gen with KEK %d`, `select_kek`, `choose_hw_kek`),
  so the key is not a fixed constant. Records are mutable; the only strings
  that limit writes are size/space related ("can't change record size, block
  relocation not implemented", "can't allocate record entry"). The monotonic
  RPMB counter prevents replay, not a new value. `trustzone-omap4` also reads
  `/dev/rpmb/BSIS_PRIV` and decrypts it with a KEK — RPMB is the OS's key
  store too.
- **Loader side.** `loader_06001A06-00.bin` carries the same record writer
  (`0x80113D9C` -> `0x80113C3C`/`0x80113AC4`, with "Fail to write RPMB") and a
  BOOTCTR_FCTRY reader (`0x8011E074`); opcode **0xDE** returns its status as
  response **0xFA**. It also contains a decrement (`0x8011E1EC`) and a
  set-value-<0x33 (`0x8011E250`) helper, but this build has no caller for
  either. A pre-OS factory write therefore needs cfp's RAM agent (or another
  loader build), not the standard OS-update loader.

## 7c. CFP-side boot-count protocol (2026-10-21)

The user supplied `CFP.exe` (3.10.0.22) and `cfp-old.exe` (3.9.0.165). Static
decompilation (Ghidra 12.1.4, x86) resolves the CFP side:

- **`RESETQNXOSREGION` is not the boot-count command.** It is a hidden CLI
  command (accepted, no help text) whose handler calls `FUN_00422270(4)` and
  logs `"QNX OSREGION NVRAM record cleared."`. That function drives the
  loader's OSREGION/NVRAM `0x2074` reset. It never touches RPMB
  `BOOTCTR_FCTRY`.
- **The boot-count functions are separate.** CFP.exe `FUN_0044d400` (wrapped by
  `CFP_Imp::SetBootCount` at `0x420ef0`) and cfp-old `0x48BAE0` send loader
  command **0xDE** with an 8-byte `{u32 action, u32 value}` payload:
  - `action 0` — read boot mode (INFO uses this);
  - `action 1` — switch mode, `value` 1 = FACTORY, else PRODUCT;
  - `action 2` — set boot count, `value` = count (1..0x32).
  The response status is read from `[this+0x4020]`: `0` ok, `2` requested
  count out of range (max at `[this+0x4028]`), `3` cannot set in the current
  device mode (`[this+0x4024]`). The setter's exact call is
  `FUN_0044d400(this, 0, NULL, &count, 1)` / cfp-old
  `0x48BAE0(this, 0, 0, &count, 1)`.
- **No CLI command invokes the setter.** The top-level dispatcher in CFP.exe
  (decompiled in `cfp-dispatcher.c`) has no case for it, and the LOAD/erase
  path sends 0xDE only in its flash protocol (responses `0xCE`/`0xCF`/`0x11`/
  `0xD4`). If flashing resets the counter, it is done by the **agent** on the
  device, not by CFP userland.
- **Recovery implication.** `RESETQNXOSREGION` clears `0x2074` and is still
  worth running on the expired unit, but it will not restore
  `BOOTCTR_FCTRY`. The counter needs a pre-OS `action 2` write. Two direct
  routes: (a) patch a copy of cfp-old so its `RESETQNXOSREGION` handler calls
  `0x48BAE0(this, 0, 0, &count, 1)` with `count = 50`; or (b) run cfp-old
  under a debugger, let it connect, and call that function with `count = 50`.
  The boot-count setter lives at `0x48BAE0` in cfp-old (prologue
  `push ebp; mov ebp,esp; sub esp,0x45c; ...; mov [ebp-8],ecx`).

## 8. Next steps

1. **ECDSA enforcement (top).** The curve is settled (secp521r1 via
   `trustzone-omap4`). The remaining question is whether the bootloader's
   `0x800187d0 -> 0x800143d9` key pointer is real at runtime. Decode the `0x0`
   MLO header records (`MLO`/`PRIMAPP`/`KEYS`) and the `0x1D95E` table; check
   for a load-time relocation. If that fails, read `0x800143D0`–`0x800144F0`
   and `0x800187D0` live during the bootloader window.
2. **Recover the expired unit via a pre-OS writer.** The standard loader reads
   BOOTCTR_FCTRY (0xDE) but does not expose the set/decrement helpers; extract
   cfp's RAM agent and look for the factory-counter command. Do not use
   `/dev/rpmb` for recovery — it needs a booted OS.
3. **Validate a valid rewrite on a sacrificial unit** via `/dev/rpmb` (1..50),
   then reboot and check whether the bootloader reports FACTORY. `rpmb-omap4`
   supports record writes; the counter blocks replay, not a new value.
4. **Image side.** Once the factory path is confirmed: a region header with
   `region[0xd4] == 6` and a CRC-32 trailer over `[start, end-4)`.
5. Audit `NvOSRegionStruct` field accesses against the actual 0x18–0xC0 size,
   and the `0xff00ffff` version mask.
5. **RPMB writes are the lever for the factory path, and the writer is
   userland: `/dev/rpmb` on the booted OS.** Capture `BOOTCTR_FCTRY` read-only
   first, determine the rewrite/write-counter semantics, then on a sacrificial
   unit write a valid value (1..50) and test the CRC chain. Keep the working
   unit off-limits: a bad value prevents the OS from booting, which removes the
   writer. Recovery for an expired unit needs CFP or the ramloader's RPMB path
   (`Fail_to_write_RPMB` strings); the record is a finite budget and cannot be
   reset through normal interfaces.
6. Audit the RPMB read parser (`0x80011690`/`0x80010A34`) for length/offset
   confusion and nonce handling, but expect the secure-world MAC (`0x2E`) to
   remain the gate. Map services `0x12`/`0x2E`/`0x2F`/`0x31`/`0x33` against
   the hidden dispatcher (`65`) if it ever becomes readable.
