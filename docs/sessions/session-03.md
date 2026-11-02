# Session 3 (absolute) — the `hd0` mode map, CRC-32 vs ECDSA, and the RPMB/CFP boot-count dead end

> Numbering: absolute **3** of 12, **pre-Linux** (no Linux-era number). This
> session's report is `71-hd0-mode-map-and-verifier.md`; its §7b (RPMB addendum)
> and §7c (CFP protocol) are also this session's. Session 4's `72`/`73`/`74` and
> session 5's `75`/`76`/`83`/`84` build on it; read `71` §5 with `73`, and `71`
> §7b with `89`/`116`. `NUMBERING.md`.

Date / span: 2026-10-20 → 2026-10-21 (device logs are ART = UTC−3).

Unit state at the end: **no device, NVRAM, RPMB or flash writes.**
`hd0head.bin` still hashed
`dda9d955003302adf1f0d41580c50c0c9035126487e7ac53793f4d38a3a23e3d`; the
working/reference unit was not put at risk (this was a static-RE session), and
the expired 32 GB unit remained parked. `BOOTCTR_FCTRY` was not touched by this
session. (The operator's earlier `FF FF FF FF` was written from the **booted OS**
via `/dev/rpmb`, not by CFP and not by this session.)

## Goal of this session

Start from `HANDOFF-2026-10-20-next-session.md` and close its four bootloader
items:

1. build the ARM/Thumb mode map for `hd0head.bin` `0x80008000`–`0x8001D044`;
2. decompile the ARM crypto and decide whether the `0x80014838` trailer check is
   a CRC or a status;
3. trace `0x80014716` (product path) and `0x80014788` (trailer path) to their
   actual primitives;
4. find writers of NVRAM `0x2004`/`0x2040`.

The operator then forced the RPMB/CFP thread: `BOOTCTR_FCTRY` is an **RPMB**
record, not NVRAM; the CRC path may be factory-gated; and the CFP/cfp-old
boot-count code needed decoding. “Done” became: the four items answered with
evidence, plus a documented RPMB/CFP path and an honest statement of what
remained.

## Confirmed (measured, read in a file, or seen on the bench)

### 1. The mode map — two prior claims were wrong

Method: Ghidra 12.1.4 headless (`ARM:LE:32:v7`, raw binary at `0x80000000`,
targeted `TMode` seeding) supplied a first-pass map; every ARM start and every
disputed boundary was then re-decoded in **both** modes with LLVM 23.1.3
(`llvm-mc`/`ld.lld`/`llvm-objdump`) and Capstone 5.0.7. Where Ghidra and the
bytes disagreed, the bytes won. Reproduction recipe and the 20,881-instruction
run table are in `71` §1 and `analysis/hd0-modemap-2026-10-20/`.

- **`0x8001404a` is Thumb, not ARM.** `70` §2 (and the previous handoff) said
  “ARM, `push {r4-r8,lr} = e92de9f0`”. The bytes at `0x8001404a` are
  `2d e9 f0 41` = Thumb-2 `push.w {r4-r8,lr}`; `e92de9f0` does not occur there.
  The whole verifier region `0x80013fb6`–`0x800148ac` is Thumb; there is no ARM
  island in it. [`71` §1; byte read]
- **Ghidra's auto-analysis run `0x800139c4`–`0x800139e4` is Thumb, not ARM.**
  Bytes `00 20 a8 80 …` decode as `movs r0,#0; strh r0,[r5,#4]; ldrb r0,[r4,#3]`;
  the ARM decode is incoherent. It was a bad function discovered from a stale
  pointer. [`71` §1]
- Verified ARM islands in `0x80008000`–`0x8001D044` (everything else in the code
  window is Thumb; gaps in the Ghidra run table include literal pools and real
  Thumb code Ghidra never reached, e.g. `0x8001596E`–`0x80015AA4`):

  | ARM range | role |
  |---|---|
  | `0x800085A4`–`0x800085CC`, `0x800086D4`–`0x80008A60` | early split/arithmetic helpers, not boot verification |
  | `0x80009078`–`0x800090A0` | **ARM `smc #0` wrapper** (`r6=0xFF`, `r12=0`), the RPMB/secure-storage path |
  | `0x800094DC`–`0x800094E4`, `0x80009FFC`–`0x8000A004` | CP15 / `bx pc` veneers |
  | **`0x80013770`–`0x80013814`** | **CRC-32 update** (table-driven, reflected) |
  | **`0x800175DC`–`0x80018640`** | **SHA-512** (init/pad/block loop + compression) |
  | **`0x80018688`–`0x800187D0`** | **public-key region verifier** |
  | `0x80018AC8`–`0x8001C87C` | big-integer library (add/sub/mul/div, modular inverse, wNAF) |
  | `0x8001951C`–`0x80019984` | SHA-512 compression function (`FUN_80019558`) |

- The interleaving in `0x80018Axx`–`0x8001Axxx` is dense: e.g.
  `0x80018B40`–`0x80018C86` is Thumb inside the ARM crypto, and
  `0x8001AF9C`–`0x8001AFE0` is a chain of 2/4-byte Thumb `bx pc`/ARM stubs.
  Do not decompile across those boundaries without the per-address mode.
  [`71` §1]

### 2. The trailer check is CRC-32/ISO-HDLC, and it is forgeable

`FUN_80014788` (Thumb, `0x80014788`) is the CRC path:

```
start = *(*(region+8));  end = *(*(region+0xc));
stored = *(u32 *)(end-4);
if (stored == 0xFFFFFFFF) { status = 7; fail; }
crc = 0x8001a7b4();                 /* init = 0xFFFFFFFF */
for (chunks of 0x2710) crc = 0x8001a7c0(crc, p, len);
crc = 0x8001a7c8(crc);              /* final XOR 0xFFFFFFFF */
if (crc == stored) return 1;        /* verified */
status = 5; fail;
```

The final compare is exactly `cmp r4,r7; beq` at `0x80014838`. The primitive is
standard CRC-32/ISO-HDLC:

- `0x8001A7B4` (ARM, 8 bytes): `mvn r0,#0` → init `0xFFFFFFFF`.
- `0x8001A7C8` (ARM, 12 bytes): `eor r0,r0,#0xFFFFFFFF` → final XOR.
- `0x8001A7C0` branches to `0x80013770` (ARM), the table-driven update
  `crc = table[(crc ^ byte) & 0xff] ^ (crc >> 8)`.
- The table at `0x8004CD3C` is **generated at runtime** by `0x80013ED4`; the
  polynomial `0x04C11DB7` is at `0x80013F2C`, and `0x80013F0C` is the
  bit-reverse helper. `0x800159F2` calls the generator. The generated table is
  the reflected form (effective poly `0xEDB88320`). The table is `0xFF` in the
  file because it is RAM, not image data. [`71` §2.1]

This settled the handoff question: `0x80014838` is a direct 32-bit equality
against a CRC, not a status word. Later `89` §4 proves the generator against the
standard table and the `"123456789" → 0xCBF43926` vector. The CRC covers
`[start, end-4)`, i.e. the header (including the verifier's `os_type` byte at
`desc+0xD4`; `89` §1.3 later fixes its media offset) and the payload; the last
4 bytes are the CRC itself. [`71` §2.1; `89` §4.3]

### 3. The product path is SHA-512 + an ECDSA-family P-521 verify

`FUN_80014716` (Thumb) validates the region pointers and calls
`FUN_80018688(start, end, flag)`. `0x80018688` (ARM) does:

1. validate `end-0x230`; require `*(u32 *)(end-0x178) == 0xB5A60BFD` and
   `*(u32 *)(end-0x210)` neither `0` nor `-1`;
2. hash `[start, end-0x230)` with `FUN_80018420`;
3. parse a `0x85`-byte (133-byte) SEC1-style blob via `FUN_8001AFEC`;
4. verify `FUN_8001B194(sig_block = end-0x208, parsed_pubkey, digest)` and
   return `1` only on success.

`FUN_80018420` is **SHA-512**:

- the 16×32-bit IV at `0x80018640`/`0x80018648`…`0x80018680` is byte-for-byte
  the SHA-512 IV (`6a09e667 f3bcc908 … 5be0cd19 137e2179`);
- `0x80`-byte blocks, `0x80` pad byte, 8-byte big-endian bit length at block
  offset `0x78`–`0x7F`;
- 64-byte big-endian output;
- compression `FUN_80019558` (`0x80019558`–`0x80019984`);
- the `0x4B00` checkpoint is a watchdog/abort poll, not a hash limit.

`FUN_8001AFEC` accepts tag `0x02`/`0x03` (compressed) or `0x04` (uncompressed),
then two 66-byte big-endian integers, converted by `FUN_8001C2F0` into 17-word
(68-byte) limbs; `0x85 = 1+66+66`.

`FUN_8001B194` is the public-key operation:

```
z  = SHA512 digest -> 17 limbs
copy pubkey[0]    -> Q.x (require nonzero)
copy pubkey[0x44] -> Q.y
w  = modinv(sig+0x44)                 FUN_8001BE94 (binary extended GCD)
u1 = z * w                            FUN_8001BF5C
u2 = r * w                            FUN_8001BF5C
FUN_8001B730(out, u1, 5, table, u2, 3, Q.x)
      -> FUN_8001B280(2, scalars, out)  (two-scalar, signed-digit wNAF)
compare result with sig block         FUN_8001BED4
```

The 66-byte coordinates, 68-byte limbs and SHA-512 digest identify the ECDSA
family. The curve is **secp521r1 (P-521)**: the QNX secure-world binary
`trustzone-omap4` contains `secp521r1_b`, `secp521r1_ma`, `SHA512_IV`, the
`hu_SHA512*`/`husw_SHA512*` family, ECDH and KEK-selection strings. The
bootloader’s own generator/order table was not extracted; `FUN_8001B730`’s
`5`/`3` arguments and the table at `*0x8001B27C` (`0x800267B8`, all `0xFF` in
the file) remain undecoded. [`71` §2.2]

Neither verifier chain executes an SMC. The only SMC veneers near the crypto
are ARM stubs at `0x80019394`/`0x800193A8`/`0x800193BC`/`0x800193D0`/
`0x800193E4` (services `0x100`/`0x102`/`0x109`/`0x112`) plus a Thumb wrapper at
`0x800043B4` (`r6=0xFF`, `r12=0`, `smc #1`). There is no `bl`/`blx` or literal
reference to them in `hd0head.bin`; they are dead or indirect here. The region
signature check runs in the non-secure world. [`71` §2.2]

### 4. The selector, and the critical boot-flow correction

`0x80014904` (Thumb boot main) calls one of two wrappers, which set a verifier
flag in `*DAT_800148f0+4` and tail-call the shared driver `0x800142a8`:

- `0x80014548`: `flag = 0` (normal boot path)
- `0x8001453c`: `flag = 1` (retry/recovery loop)

**The flag does not select CRC vs public key.** It is passed through
`0x80014716` into `0x80018688` as `param_3`, where it only chooses the hash
checkpoint cadence: `0` → every `0x4B00` bytes, no `FUN_80012516` abort poll;
`1` → every `0x80` bytes plus an abort poll. The algorithm is selected
*separately* by `FUN_8000911a()` at `0x80014254`/`0x80014260`:

- `0` → `FUN_80014716` → SHA-512 + public-key verify;
- `1` → `FUN_80013fb6` (requires `region[0xd4] & 0xff == 6`) → `FUN_80014788`
  → CRC-32 only;
- `2` → status `0x0f`, failure.

`FUN_8000911a` is a Thumb veneer (`movs r0,#0; b 0x800090ce`) that reads the
RPMB record `"BOOTCTR_FCTRY"` and returns `0` when the record is absent, `1`
when it exists with a value in `1..0x32`, `2` otherwise.

Boot-main order (`71` §3; assembled from the bytes):

1. read NVRAM `0x2040`; `tbb [pc,r0]` at `0x8001498a`: `0`/`2` → gate at
   `0x80014998`, `1` → nuke, `3..8` → normal path, `>=9` → nuke;
2. increment `0x2040`, set `bVar1 = true`, write it back;
3. if the write succeeded and `FUN_8000911A() == 0`, run the product verifier
   (`flag = 0`); on success check the region pointers, then read NVRAM `0x2004`:
   `0x2004 == 0xAAAAAAAA` → `LAB_80014A90` (enter OS) **without nuke**;
   otherwise → `0x80014A16`;
4. `LAB_80014A16` calls `FUN_800156E8`, the “Nuking…” routine: writes
   `0x2040 = 1`, erases the OS-region ranges, writes `0x2040 = 2`, then falls
   into `LAB_80014A90` (enter OS) with the region erased;
5. if `bVar1` is false (retry path), enter the `0x8001453c` loop; the same
   `FUN_8000911a` selector then decides public-key vs CRC. A successful result
   (`iVar3 == 1`) goes to `LAB_80014A3E` → enter OS.

**The consequence that mattered most:** `0x800149C2 cbnz r0, 0x80014a16` nukes
on **any** nonzero selector. A valid factory counter (`1..0x32`) on the
**normal path** therefore nukes; it does not give a benign factory boot. The
CRC-only verifier is reached only through the `0x80014998` branch when
`FUN_80014F84() != 0`, which jumps to the retry loop `0x80014A1E` before the
normal-path selector read. I called that condition a “recovery/time condition”;
later `75`/`76` identify it as the `0x2801` device-password attempt budget
(`A < clamp(B,3,10)`), and the live reference is already `A=0`, `B=10`. The
structure of the correction — a valid counter alone is not enough; the retry
branch is required — is still current. [`71` §3; `75` §3/§5]

Related corrections in the same flow:

- `0x2004 == 0xAAAAAAAA` is **not** a verification bypass. It is read only
  *after* the public-key verifier has returned success; its effect is “the
  region is committed/bootable; do not nuke”. Verification failure jumps to the
  nuke before the `0x2004` read. `70` §6’s “skips the second verifier loop” is
  true only in the narrow sense that success skips the `LAB_80014A16` nuke.
  [`71` §3]
- The nuke path writes `0x2040 = 1`, erases the region ranges, writes
  `0x2040 = 2`, then enters the OS with the region erased. It is not a silent
  “try the other region” fallback. [`71` §3]

### 5. NVRAM IDs and the two flags

The full scan of `0x80008000`–`0x8001D044` for immediates fed to the NVRAM
reader/writer (`FUN_8000A9D8` / `FUN_8000A9FE`) and to `0x8001397E` found:

| ID | access | sites |
|---|---|---|
| `0x2004` | read only | `0x80014A06` |
| `0x2019` | r/w | `0x80015360`, `0x8000CC60` |
| `0x2028` | r/w | `0x800155A0`, `0x800155DE` |
| `0x205a` | read (8-byte region/type bitmask) | `0x800141F8` |
| `0x2074` | r/w (OS-region struct) | `0x800142DC` read, `0x80014508` write |
| `0x2079`, `0x207a` | read | `0x80013CE0`/`0x80013D0E`/`0x80013DAE`/`0x80013E52`, `0x80013C44` |
| `0x2801`, `0x2802` | r/w, 0x70 B | `0x80014FA6`, `0x80014D88`, `0x8001587A`, … |
| `0x2819`, `0x282F`, `0x280E` | read | `0x80013F98`, `0x80013A6E`, `0x80013A22` |

`0x2710` (10000) is the CRC/hash chunk constant, not an NVRAM ID; `0x2434` is a
length passed to `0x80018C9C`, not an NVRAM ID. [`71` §3]

- **`0x2004` has no writer in any available image.** The boot ROM, the lower
  boot ROM, the ramloader (`loader_06001A06-00.bin`) and the appliance
  partitions `hd4.bin`/`hd5.bin` contain no `movw #0x2004` and no NVRAM write
  of it. The writer must be the flash/update path not in the workspace (CFP,
  the OS updater, or the OS). Later `79` names it `NV_OSSTORE_JvmStart`; the
  writer is still the update path. [`71` §3; `79` §3]
- **`0x2040` writers in this image**, all via `FUN_8000A9FE`:
  `0x800149AE` (incremented value), `0x80015700` (`=1`, start of nuke),
  `0x8001588C` (`=2`, end of nuke), `0x800158D6` (`=3`, state transition in
  `0x800158A2`). Reads at `0x80014974`, `0x800158AC`, `0x80015A22`. Later `76`
  finds `0x2040` absent on the live reference; the boot main substitutes the
  pre-initialised `2`. [`71` §3; `76` §3]
- **`0x2074` is the OS-region struct.** As read this session: a `0xC1`-byte
  buffer, accepted size `0x18..0xC0`; `+0x04` requested region, `+0x05` last
  verified region, `+0x06 + 2*i` u16 per-region counters, `+0x0A` u16 max
  (`0` = unlimited). The verifier logs `Verifying region %d`, calls
  `0x80014148`; on success it clears the other region’s counter, increments
  this one, stores the index at `+0x05`, and writes the record back. If both
  regions fail it returns `0` → nuke path. Later `83` corrects the size to
  **72 bytes** (`OsRegionV3`, `+0x0C` is a u32 `flags`, regions are 0/1, and
  `max == 0` is unlimited). [`70` §3; `71` §3; `83` §2]
- **`0x2801`/`0x2802`** were the other 0x70-byte record pair. I read the gate as
  a generic attempt/“recovery” record; later `75` proves it is
  `NV_OSSTORE_PasswordStruct` (0x2802 its reserve), `+0x18` the failed-attempt
  counter **A**, `+0x1c` the clamped maximum **B**, and the gate is
  `clamp(B,3,10) - A`. The live digest is the iterated SHA-512 of `0000`
  (`84` §4). [`71` §3; `75` §1–§3]
- **`0x205a`** is an 8-byte per-OS-type model-ID override/bitmask read at
  `0x800141F8`. The compare I saw was
  `(value & 0xff00ffff) == (*(region+0x7c) & 0xff00ffff)`, with bits 16–23
  ignored; I described it as an anti-rollback/version compare. Later `89` §3.3
  identifies it as the **bootrom `modelID` match** (`0x06001A06`), not an
  anti-rollback counter. [`71` §2/§3; `89` §3.3]

### 6. `BOOTCTR_FCTRY` is an authenticated RPMB record

The operator supplied the key domain fact: `BOOTCTR_FCTRY` is a file/record in
the **RPMB** partition, not the plain NVRAM store, and its value must be
**1..0x32 (50)**. `0` and `>0x32` both fail; writing `FF FF FF FF` on a unit
produced **“Expired factory mode”** in CFP and the unit would not boot, and the
failure is not reversible through the normal interfaces. Later sessions confirm
the hard lock and that recovery is physical (`116` §2.2). [operator statement;
`71` §7b; `116` §2.2]

The code agrees, and the mechanism is:

- `FUN_8000911a` (`0x8000911a`) is a veneer to `0x800090ce`, which reads
  `"BOOTCTR_FCTRY"` via `0x80011690(name, &value, 4, &len)`. The accepted range
  is checked at `0x80009100`–`0x80009114`: `1 <= value <= 0x32` returns 1;
  `value == 0` or `value > 0x32` returns 2. Status 2 sends `0x80014904` to the
  nuke path, which matches the observed failure. [`71` §7b]
- **Provisioning/delete:** `0x800090a0(1)` creates the record with the literal
  value `0x32` via `0x800115e6("BOOTCTR_FCTRY", &0x32, 4)`; this is the path
  taken after `"RPMB not provisioned"` in `0x80014904`. `0x800090a0(0)` deletes
  it via `0x8001167e`. This stage may provision/delete the record; the
  pre-OS setter is a separate question (§8). [`71` §7b]
- The backing block is a packed record store with an `0x18`-byte record header:
  `+0` magic `0x5983BC2D`, `+4` 14-byte name, `+0x16` u16 length, `+0x18` data.
  `0x80011166` locates the magic, `0x8001124e` repacks, `0x80010B2E` writes the
  block back. [`71` §7b]
- `0x80011690` is **not** the plain NVRAM API (`FUN_8000A9D8`/`FUN_8000A9FE`).
  It looks up a `0x1c`-byte descriptor by 14-byte name in the table at
  `*0x80011530 = 0x8002C760` (count via `*0x80011534 = 0x80025FB0 + 4`), then
  calls `0x80010a34(0x80033F80, 0xE0, 0x80033760, 0x80025FB0, desc[+0x12])`.
  Descriptor: `+4` 14-byte name, `+0x12` u16 RPMB address/index, `+0x16` u16
  data length, `+0x18` u16 offset in the block. After the block read it
  compares `desc+4` with `block+offset+4` (14-byte `memcmp` at `0x80018BB4`)
  and copies `desc[+0x16]` bytes from `block+offset+0x18`. [`71` §7b]
- `0x80010a34` is a **512-byte authenticated block read**:
  1. gets a 16-byte nonce via `0x8001101C -> 0x80017D24`, which calls secure
     service **0x12** in 0x80-byte chunks into `0x8002A4E0 + 0x2200`;
  2. builds the frame: zero 0x200 bytes, `frame[+0x1FE] = 4` (read request),
     `frame[+0x1F8] = desc[+0x12]` (RPMB address), nonce at `+0x1E4`;
  3. `0x80010770` byte-swaps the tail u16s and reverses the 0x100-byte data
     area (RPMB frame endianness);
  4. `0x8001080E -> 0x8000DF48` (request 4) and `0x80010CFC -> 0x8000D0EA`
     (request 1) drive the eMMC block layer;
  5. checks `frame[+0x1FC] == 0`, re-compares the nonce (replay check), then
     computes the 32-byte MAC via `0x800107B6 -> 0x80017758(..., 3, 0, 0)`
     (secure service **0x2E**) and compares it with the stored MAC at `+0xC4`;
  6. on MAC mismatch, `0x80011048` writes the block back via
     `0x80017758(..., 3, 1, key+0x18)` → service **0x33** (load 16-byte key)
     then service **0x2F** (authenticated write), and re-reads.
  [`71` §7b]
- All service calls go through `0x800183DC`, a thin wrapper over `0x80009078` =
  **ARM `smc #0` with `r6=0xFF`, `r12=0`**, the SAR/monitor ABI in `65`.
  Services seen on this path: `0x12` (fill 0x80-byte buffer / nonce), `0x2E`
  (compute a 32-byte MAC over a 0x100-byte request; the caller compares), `0x2F`
  (0x160-byte authenticated read/write), `0x33` (load 16-byte key), `0x31`
  (query/write-counter), `0x28`/`0x29`/`0x2A`/`0x0F` (fuse/OTP read in
  `0x80017FD8`), plus `0x2D`, `0x3C`, `0x0E`. [`71` §7b; `65`]
- **The 14-byte tag compare is not the security boundary.** The key is not in
  the static image: the `0x2E` request carries a 32-byte key/context pointer
  (`0x80033760`, a runtime buffer above the image); the MAC is produced by the
  secure monitor and compared by the caller, and the nonce comes from service
  `0x12`. A tag-only patch or a forged value cannot pass unless that MAC/key
  path is broken. The record table and block buffers (`0x8002C760`,
  `0x8002A4E0`, `0x80033F80`, `0x80025FB0`, `0x80033760`) are all above the
  0x20000-byte file image (BSS/secure-shared); the record metadata is
  runtime-populated. [`71` §7b]

### 7. The OS side is the same store, and it is the general writer

- `playbook-dev/dumped4869ifs/proc/boot/rpmb-omap4` (now
  `local/proprietary/dumped4869ifs/proc/boot/`) is the `/dev/rpmb` resource
  manager: `iofunc_read_verify`/`iofunc_write_verify`, `dispatch_block`, a
  record catalogue, metadata/block consistency checks, and a nonce. It derives
  the RPMB key from a hardware KEK (`"RPMB key util: RPMB key gen with KEK %d"`,
  `select_kek`, `choose_hw_kek`), so the key is not a fixed constant. Records
  are mutable; the only strings that limit writes are size/space related
  (“can’t change record size, block relocation not implemented”, “can’t allocate
  record entry”). The monotonic RPMB counter prevents replay, not a new value.
  [`71` §7b]
- `trustzone-omap4` also reads `/dev/rpmb/BSIS_PRIV` and decrypts it with a KEK —
  RPMB is the OS’s key store too. (The same binary supplies the P-521 strings.)
  [`71` §7b]
- The operator’s `FF FF FF FF` was written from the booted OS over SSH through
  `/dev/rpmb`; that is the general writer. The catch is asymmetric: a bad value
  prevents the OS from booting, which removes the writer. The expired unit is
  the proof. [`71` §7b; operator statement]

### 8. CFP and the ramloader: no pre-OS counter writer

The operator supplied `CFP.exe` 3.10.0.22 and `cfp-old.exe` 3.9.0.165; CFP is a
PE with an encrypted/compressed `FileImage`, and the embedded agent is not
plaintext (the reference doc describes it as the same ramloader family at
`0x80100000`–`0x80130230`). Static x86 decompilation (Ghidra 12.1.4) resolves
the host side:

- **`RESETQNXOSREGION` is not the boot-count command.** It is a hidden CLI
  command (accepted, no help text) whose handler calls `FUN_00422270(4)` and
  logs `"QNX OSREGION NVRAM record cleared."` (cfp-old handler
  `0x454010 → FUN_00422270(4)`). It drives the loader’s OSREGION/NVRAM `0x2074`
  reset; it never touches RPMB `BOOTCTR_FCTRY`. The operator had already tried
  it on the expired unit with no change. [`71` §7c; operator statement]
- **The boot-count functions are separate.** CFP.exe `FUN_0044d400` (wrapped by
  `CFP_Imp::SetBootCount` at `0x420ef0`) and cfp-old `0x48BAE0` send loader
  command **0xDE** with an 8-byte `{u32 action, u32 value}` payload:
  - `action 0` — read boot mode (INFO uses this);
  - `action 1` — switch mode, `value` 1 = FACTORY, else PRODUCT;
  - `action 2` — set boot count, `value` = count (1..0x32).
  The response status is read from `[this+0x4020]`: `0` ok, `2` requested
  count out of range (max at `[this+0x4028]`), `3` cannot set in the current
  device mode (`[this+0x4024]`). The setter’s exact call is
  `FUN_0044d400(this, 0, NULL, &count, 1)` / cfp-old
  `0x48BAE0(this, 0, 0, &count, 1)`. [`71` §7c]
- **No CLI command invokes the setter.** The top-level dispatcher decompiled in
  `cfp-dispatcher.c` has no case for it, and the LOAD/erase path sends `0xDE`
  only in its flash protocol (responses `0xCE`/`0xCF`/`0x11`/`0xD4`). If
  flashing resets the counter, it is done by the **agent** on the device, not by
  CFP userland. [`71` §7c]
- **The PlayBook loader’s `0xDE` is read-only.** `loader_06001A06-00.bin`
  (238,128 B, base `0x80100000`, code to `0x80130230`) has the `0xDE` case at
  `0x80103D50`; the action-0 reader is `0x8011E074`; the setter `0x8011E250`
  and decrement `0x8011E1EC` exist but have no caller. In the reader, the only
  “write” is `strls r0, [r4]` at `0x8011E074`, which copies the stored
  boot-mode value into the request buffer, **not** back to RPMB. The loader
  does write RPMB record index **2** at `0x8010083C`, but not `BOOTCTR_FCTRY`
  (index 0). The same image carries the record writer `0x80113D9C →
  0x80113C3C`/`0x80113AC4` with `"Fail to write RPMB"`. [`71` §7b; `72` §5;
  this session’s re-check]
- `0xDE` response is **`0xFA`**, 12 bytes `{status, mode, count}`; mode `0`
  product, `1` factory, `2` expired; status `0` ok, `1` not allowed, `2` out of
  range, `3` wrong device mode. The ramloader command mirror
  `analysis/ramloader-commands.txt` confirms `0xDE = BOOT_MODE` and
  `0xE9 = boot count info`. [`72` §3; `analysis/ramloader-commands.txt`]
- The loader is **the same file inside `bb10mt/loaders.dat` and CFP’s
  ramimage**. The operator corrected an earlier assumption that CFP’s agent
  might be a different build: it is `loader_06001A06-00.bin`. Other
  `loaders.dat` entries are for different devices. Only this loader works on
  the PlayBook, and the boot ROM signature-checks the uploaded ramloader before
  running it, so it cannot be patched without a flaw in that check. [operator
  statement; `72` §2; `HANDOFF-2026-10-21-next-session.md` §4]

### 9. Operator-supplied facts and the session’s safety decision

- `BOOTCTR_FCTRY` is an RPMB file, exactly 4 bytes, accepted value **1..0x32**;
  `0` and `>0x32` are rejected; `FF FF FF FF` produced **“Expired factory
  mode”** and the unit would not boot. [operator statement; code at
  `0x80009100`–`0x80009114`]
- The write was made from the booted OS via `/dev/rpmb`, not CFP. [operator
  correction]
- The expired unit is parked; do not risk the known-working unit. Any RPMB
  write is a sacrificial-unit experiment. [operator constraint]
- Do not patch `CFP.exe`: there is no Windows device currently, and
  dual-boot/VM is uncertain because of drivers/BlackBerry Device Manager. If a
  patch is ever attempted, implement it in `bb10mt` or `bbusb`, not in
  `cfp.exe`. [operator constraint]
- `resetqnxosregion` was already tried on the expired unit with no change; it
  clears `0x2074`, not the RPMB counter. [operator statement; `71` §7c]

## Inferred (reasoning from evidence, not directly measured)

- **The CRC-only path is factory-RPMB-gated, not NVRAM-gated.** The selector
  reads the RPMB record; NVRAM writes cannot reach it. The CRC path is not a
  product bypass; it is the factory/unsigned-OS path. Strength: strong — the
  selector and the range check are read directly. [`71` §2/§7b]
- **A valid factory counter on the normal path nukes.** The `0x800149C2 cbnz`
  is unconditional on any nonzero selector; the only benign route is the
  `0x80014998` retry branch. Strength: strong at the instruction level; the
  live consequence was later proved by `95` (a 5-byte reseal with the retry
  gate booted and decremented 50 → 49). [`71` §3; `89` §3.5; `95` §4]
- **The 14-byte tag is a name check; the secure-service 0x2E MAC is the
  boundary.** The tag `memcmp` is plain; the MAC covers the 0xE0-byte payload
  and is produced by the monitor. A metadata rewrite still has to produce a
  valid MAC. Strength: strong from the call chain; the key source itself is not
  in the static image. [`71` §7b]
- **The RPMB key is secure-world/KEK-derived, so a normal-world patch cannot
  forge it.** The OS resmgr derives it from a hardware KEK; the static image
  only has a runtime pointer. Strength: strong for “not a static constant in
  `hd0`”; the exact key location was not recovered this session. (Later `116`
  §3 recovered a 32-byte fallback literal in `rpmb-omap4`; this session did
  not.) [`71` §7b; `116` §3]
- **There is no pre-OS `BOOTCTR_FCTRY` writer in the known signed code.** The
  PlayBook loader’s action 2 is rejected; the setters are dead; CFP’s setter
  needs a loader that accepts action 2. The only general writer is `/dev/rpmb`
  on a booted OS. Strength: strong for the shipped loader; the Cellebrite
  loader and other builds were later audited (`72`, `88`) and did not change
  it. [`71` §7b; `72` §5/§6]
- **The expired unit cannot be recovered in software through the known
  interfaces.** `0xFFFFFFFF` is selector 2; `/dev/rpmb` needs an OS; the
  bootloader’s `0x800090a0(1)` provisions only an **absent** record at `0x32`,
  not an invalid one. Strength: strong; the remaining theoretical route is a
  memory-safety bug in the signed loader that reaches the dead setter, or
  physical eMMC/RPMB access. [`71` §7b; `72` §6; `92` §6]
- **The key-pointer anomaly is a relocation/section problem, not a broken
  product path.** `0x800187D0` holds `0x800143D9`; the byte at `0x800143D9` is
  `0xF8`, which the SEC1 parser rejects, and `*(0x8001B188) = 0x800144E8` is
  Thumb code, not a bignum constant. Candidate explanations I listed:
  (a) the MLO is a multi-section image whose other sections populate a key slot;
  (b) a load-time fixup patches these words; (c) the path is not exercised on
  this build. Strength: the observations are exact; the explanation was
  unresolved this session. Later `73` proves (b): `hd0head.bin` is
  pre-relocation, linked at `0x7FFF7000`, loaded at `0x80000000`, with a
  `+0x9000` fixup; the literals resolve to a valid P-521 key and the curve
  constants. [`71` §5; `73` §3]

## Dead ends and wrong assumptions (valuable — do not skip)

1. **“`0x8001404a` is ARM.”** It is Thumb (`2d e9 f0 41` = `push.w
   {r4-r8,lr}`); the `e92de9f0` bytes do not exist there. The whole verifier
   region is Thumb. This came from `70` §2 and the previous handoff; the byte
   read settled it. [`71` §1]
2. **“Ghidra’s `0x800139c4` run is ARM.”** It is Thumb; the ARM decode is
   incoherent. It was a bad function discovered from a stale pointer. [`71` §1]
3. **“The CRC is forgeable, therefore the boot path is open.”** The CRC is
   reachable only on the factory path, and a valid factory counter on the
   normal path nukes. The CRC chain alone does not bypass the selector or the
   `0x2801` gate. [`71` §2/§3]
4. **“`0x2004 == 0xAAAAAAAA` skips verification.”** It is read only after the
   product verifier returns success; it skips the nuke, not the verifier. `70`
   §6’s wording was too broad. [`71` §3]
5. **“`FUN_80014F84` is a time/recovery condition.”** It is the `0x2801`
   attempted-password budget (`A < clamp(B,3,10)`), not a clock. The structure
   of the boot flow was right; the label was wrong. [`71` §3; `75` §1–§3]
6. **“The expired unit’s bootloader nukes it.”** `0xFFFFFFFF` is selector 2;
   on the normal path that nukes, but on the retry branch selector 2 is a
   failure/halt in `FUN_80014C98`, not the normal-path nuke. The expired unit’s
   `0x2801`/`0x2040` was never read, so which branch it takes is unresolved.
   See `docs/contradictions/expired-counter-nuke-vs-retry-halt.md`. [`71` §2/§3;
   `72` §3.1; `75` §6]
7. **“The CRC path is reachable by writing an NVRAM record.”** It is RPMB-gated;
   the tag path is authenticated. [`71` §7b]
8. **“The 14-byte tag is the security boundary.”** It is a plain `memcmp`; the
   secure-service 0x2E MAC is the boundary. A tag-only patch is not enough.
   [`71` §7b]
9. **“CFP’s ramimage is a different agent, so it may have the setter.”** The
   operator corrected this: the ramimage is the same
   `loader_06001A06-00.bin`; its `0xDE` is read-only. [`HANDOFF-2026-10-21-next-session.md`
   §4; this session’s re-check]
10. **“Another `loaders.dat` entry may implement action 2.”** The operator: only
    `loader_06001A06-00.bin` works on the PlayBook; the other entries are for
    other devices. Later `72` confirms no recoverable entry implements action 2.
    [`72` §2/§5]
11. **“`RESETQNXOSREGION` may reset the boot count.”** It calls
    `FUN_00422270(4)` and clears NVRAM `0x2074`; it never touches the RPMB
    counter. The operator had already tried it on the expired unit with no
    change. [`71` §7c; operator statement]
12. **“Flashing an image/OS may reset the counter.”** No CLI or loader path in
    the analyzed binaries does this; a successful factory boot decrements, and
    the OS-side script tops up. The hypothesis was not confirmed. [`71` §7c;
    `89` §3.5; `116` §2]
13. **“Patching cfp-old to call `0x48BAE0` is a viable recovery.”** It is a
    host-side setter, but the PlayBook loader rejects action 2, so it cannot
    work through this loader. It remains a route only if a loader that accepts
    action 2 is ever recovered. [`71` §7c; `72` §5/§6]
14. **“The stage never decrements the counter.”** I concluded this from the
    call graph (`0x800115e6` one caller `0x800090c0`, `0x8001167e` one caller
    `0x800090a6`). Later `89` §3.5 shows a successful factory verify at
    `0x80014A42`–`0x80014A4A` calls `0x8000911E`, which does
    read/decrement/write via `0x800115E6`; `95` measured 50 → 49. My “never”
    was wrong. See
    `docs/contradictions/71-bootctr-stage-decrement-vs-provision-only.md`.
    [`71` §7b; `89` §3.5; `95` §4]
15. **“The `0x2074` record is a 0xC1-byte buffer / the struct is 70 bytes.”** The
    live record is 72 bytes (`OsRegionV3`); the 70-byte reading merges
    `max_boot_count` and `flags`. Later `83` §2.4. [`70` §3; `83` §2]
16. **“The `0x205a`/`0xff00ffff` compare is anti-rollback.”** It is the bootrom
    `modelID` match (`0x06001A06`), with bits 16–23 ignored. Later `89` §3.3.
    [`71` §2; `89` §3.3]
17. **“The `0x80014A90` label is the enter-OS jump.”** I used the `70`/`71`
    label; later `91` shows it is a log/return label and the real hand-off is
    ARM `bx r0` at `0x800192A8`, with `r0 = *(0x80025E74)`. [`71` §3; `91` §2.2]
18. **“The `0x803BF610` word is an entry point.”** It is `LoadEnd`, the first
    byte past the image; the effective entry is word 0 `0xEA0001FE` → image
    `+0x800` = `0x80100000`. `71` already calls it `end`; later `85`/`89`/`91`
    make the correction explicit. [`71` §2; `89` §2; `91` §0.1]

## Unknown / open questions

- **What applies the `+0x9000` fixup?** The static image has no writer to
  `0x800143D8`–`0x800144F0`, and the boot ROM has no hard-coded `0x80014xxx`
  reference. The boot ROM or a first-stage image loader applies it from the
  signed header; the exact table/agent is unfound. A live read of
  `0x800187D0` during the boot window, or a RAM dump of the MLO after the boot
  ROM starts it, would settle it. [`71` §5; `73` §5]
- **What are the MLO header records?** `0x0` has three `0x20`-byte name records
  (`MLO`/`PRIMAPP`/`KEYS`) with `{0x4000,0x4D70}`, `{0xC00,0x32B8}`,
  `{0x200,0x960}`; I did not decode them. Later `73` §1 maps them to the
  `CertPK_`/`CertPPA`/`CertISW` blocks. [`71` §5; `73` §1]
- **What is `BOOTCTR_SIG`?** The newer OMAP loaders (`LDR_13`/`14`/`15`/`22`/
  `24`/`29`/`44`/`47`/`49`) carry a second record name; this session found the
  string but did not reverse its use. It does not add an action-2 write. [`72`
  §4]
- **What is `LDR_29`’s `action==1 && value==3 && metrics[0x84] & 0x10000`
  gate for?** Its true branch still reaches the `action != 0` rejection; the
  intent is unknown. [`72` §5]
- **The exact CFP `FUN_0044d400` request payload and whether any hidden path
  calls it.** The 8-byte `{action,value}` and the status fields are decoded,
  but no CLI trigger was found. A live CFP trace would settle it. [`71` §7c]
- **The working unit’s RPMB counter value.** The `0xDE` read says product/0;
  the `0x2801` live state is later `A=0/B=10`. This session did not read
  `/dev/rpmb`; the reference’s factory counter is not directly known here.
  [`72` §3; `76` §1]
- **The expired unit’s exact branch and `0x2801`/`0x2040` state.** `0xFFFFFFFF`
  is selector 2; whether that is the normal-path nuke or the retry-branch halt
  depends on the gate, which cannot be read without an OS. See the
  contradiction file. [`71` §2/§3; `72` §3.1; `75` §6]
- **Whether the RPMB read parser can be bypassed.** The name check is a plain
  `memcmp`; the MAC covers the 0xE0-byte payload, so a pure metadata rewrite
  must still produce a valid MAC. Look for nonce/length/offset confusion in
  `0x80011690`/`0x80010A34`; the key source is the secure world. [`71` §7b]
- **TOCTOU and the `NvOSRegionStruct` size/mask issues.** The region is hashed
  from RAM in chunks and then verified; nothing in the stage freezes it between
  hash and jump. The valid window is `0x800FF800`–`0x800102B0`. [`71` §5]
- **Who writes `0x2004`?** No writer in any available image; the update path
  is the candidate. Later `79` names it `NV_OSSTORE_JvmStart`, but the writer
  remains unknown. [`71` §3; `79` §3]
- **Whether a valid 1..50 counter on the retry branch actually boots the CRC
  path.** The code says yes; this session did not test it. Later `95` proves it
  with the 5-byte reseal (`os_type 4 → 6`, CRC `0x8D46FB4A → 0x116B2562`,
  TabletOS booted, counter 50 → 49). [`71` §3; `95` §2–§4]

## Superseded / uncertain

- **`0x8001404a` as ARM — superseded by my own `71` §1.** The bytes are Thumb.
  The old claim is in `70` §2 and the 2026-10-20 handoff.
- **The ECDSA key-pointer anomaly — superseded by `73`.** I believed the
  literals at `0x800187D0`/`0x8001B188` did not resolve to key data (byte
  `0xF8` at the point, Thumb code at the `p` pointer) and listed multi-section,
  relocation and “path not exercised” as candidates. `73` proves the
  pre-relocation `+0x9000` fixup: `0x8001D3D9` is a valid P-521 point,
  `0x8001D4E8` is `p = 2^521−1`, `0x8001D4A4` is `b`, `0x8001D460` is `3`.
  The product path is functional; the working unit’s `product/0` boot is
  consistent. [`71` §5; `73` §3/§4]
- **`0x2801` as a “recovery/time condition” — superseded by `75`/`76`.** I
  read the gate as a generic attempt/recovery record. `75` proves it is
  `NV_OSSTORE_PasswordStruct`; `+0x18` is the failed-attempt counter A,
  `+0x1c` the clamped maximum B; `A < B` selects the retry branch. The live
  reference is `A=0/B=10`; the digest is the iterated SHA-512 of `0000`
  (`84` §4). [`71` §3; `75` §1–§3; `84` §4]
- **`0x2074` as an “OS-region/boot counter” — refined by `83`.** My `0xC1`
  buffer/`0x18..0xC0` size and `+0x06+2i` counter model are the bootloader’s
  prefix view. The record is `OsRegionV3`, 72 bytes: `requested`/`actual` 0/1
  at `+4/+5`, `boot_count_0/1` at `+6/+8`, `max_boot_count` at `+0x0A`,
  `flags` u32 at `+0x0C`, `epoch` at `+0x10`, version IDs at `+0x18/+0x30`.
  `max == 0` is unlimited; `switch()` sets 4. There is no 70-byte version.
  [`70` §3; `71` §3; `83` §2/§5]
- **`0x2040` as a “boot-mode/region counter” — refined by `76`/`79`.** The
  live reference has `0x2040` **absent**; the boot main substitutes the
  pre-initialised `2`. The id is `NV_OSSTORE_NukeStatus`. [`71` §3; `76` §3;
  `79` §3]
- **`0x2801`/`0x2802` as a generic “boot-info/password” pair — refined by
  `75`/`77`/`84`.** `0x2801` is `NV_OSSTORE_PasswordStruct`; `0x2802` is the
  reserve, written only when the primary write fails. The hardware PIN is a
  different record, `0x2002` (`NV_OSSTORE_PIN_NUM`, live `0x50175127`). [`71`
  §3; `75` §1/§4.1; `84` §6]
- **“The stage never decrements `BOOTCTR_FCTRY`” — contradicted by `89` §3.5.**
  I found `0x800115E6` with one caller `0x800090C0` and concluded the budget
  was consumed by factory/CFP tooling, not the stage. `89` §3.5 shows a
  successful factory verify at `0x80014A42`–`0x80014A4A` calls `0x8000911E`,
  which does read/decrement/write via `0x800115E6`; `95` measured 50 → 49.
  See `docs/contradictions/71-bootctr-stage-decrement-vs-provision-only.md`.
  [`71` §7b; `89` §3.5; `95` §4]
- **“The expired unit’s bootloader nukes it” — ambiguous, not simply true.**
  `0xFFFFFFFF` is selector 2; the normal path’s `0x800149C2 cbnz` nukes, but
  the retry branch’s selector 2 is `FUN_80014C98` failure/halt. The expired
  unit’s `0x2801` was never read. See
  `docs/contradictions/expired-counter-nuke-vs-retry-halt.md`. [`71` §2/§3;
  `72` §3.1; `75` §6]
- **“The CRC path is an NVRAM-state write away” — wrong.** It is RPMB-gated;
  the tag path is authenticated by the secure-service 0x2E MAC. [`71` §7b]
- **“The 14-byte tag is the security boundary” — wrong.** The tag is a name
  check; the MAC is the boundary. [`71` §7b]
- **“The RPMB key is secure-world/KEK-derived, so a normal-world patch cannot
  forge it” — softened by `116` §3.** I inferred this from the secure-service
  `0x2E` MAC and the absence of key bytes in `hd0head.bin`; `116` §3 later
  recovered a 32-byte **fallback literal** (`"retreating is advancing in the o"`)
  in `proc/boot/rpmb-omap4`, and `select_kek` returns `-1` on this unit, so the
  OS-side writer may not be KEK-keyed at all. The “not in the static image” part
  is right for `hd0`; the “cannot be forged from the normal world” consequence is
  not established. See
  `docs/contradictions/rpmb-key-kek-vs-fallback-literal.md`. [`71` §7b;
  `116` §3; `109` §6]
- **“CFP’s ramimage may be a different build / another loader may implement
  action 2” — closed.** The operator confirmed the ramimage is
  `loader_06001A06-00.bin`, and the loader’s `0xDE` is read-only; later `72`
  confirms no recoverable entry implements action 2. [`HANDOFF-2026-10-21-next-session.md`
  §4; `72` §5/§6]
- **“`RESETQNXOSREGION` may reset the boot count” — wrong.** It clears NVRAM
  `0x2074` (`FUN_00422270(4)`), not RPMB. [`71` §7c]
- **“Flashing an image/OS resets the counter” — unconfirmed.** The successful
  factory boot decrements; the OS-side script tops up. No CLI/loader reset was
  found. [`71` §7c; `89` §3.5; `116` §2]
- **“`0x205a`/`0xff00ffff` is anti-rollback” — refined by `89` §3.3.** It is
  the bootrom `modelID` match; bits 16–23 are ignored. [`71` §2; `89` §3.3]
- **“`0x803BF610` is an entry / `0x80014A90` is the jump” — wrong.** `0x803BF610`
  is `LoadEnd`; `0x80014A90` is a log/return label; the real hand-off is
  `0x800192A8 bx r0`, with `r0 = *(0x80025E74)`. [`71` §2/§3; `89` §2; `91`
  §2.2]
- **“The counter is static” — not a fact.** The working unit’s counter is
  repaired by the TabletOS-side script on every boot; later sessions found the
  script was broken (`printf` absent) and the counter really drains. This
  session did not read `/dev/rpmb`, so it makes no claim about the live value.
  [`116` addenda; `session-10.md`/`session-11.md`]

## Artefacts produced

- `docs/reports/71-hd0-mode-map-and-verifier.md` — the main report: the
  corrected mode map, the CRC-32 proof, the SHA-512/ECDSA-family analysis,
  the boot flow, the NVRAM ID scan, §7b (RPMB), §7c (CFP). Later edits in the
  repo add the `73` resolution banner in §5 and a `75`/`77` note in the NVRAM
  table; the technical content is this session’s.
- `analysis/hd0-modemap-2026-10-20/` — `modes-corrected.txt` (final Ghidra
  mode runs + function list, 20,881 instructions), `verifier-decompiled.txt`
  (Thumb verifier + ARM crypto: `0x80014716`, `0x80014788`, `0x80018688`,
  `0x8001AFEC`, `0x8001B194`, `0x8001C2F0`, `0x80019C08`, `0x80019C40`, …),
  `arm-decompiled.txt`, `aux-decompiled.txt`, `gate-decompiled.txt`,
  `bootlogic-decompiled.txt`, `nvram-decompiled.txt`, `auth-decompiled.txt`
  (`0x800090ce`/`0x80011690`/`0x80010a34` and the record lookup),
  `rpmb-arm-decompiled.txt` (`0x80017758`/`0x8001783c`/`0x80017d24`/
  `0x80017c38`/`0x800183dc`), `record-decompiled.txt` (`0x800090a0`,
  `0x800115e6`, `0x8001167e`, `0x80011166`/`0x8001124e`/`0x80010b2e`),
  `loader-rpmb-decompiled.txt`/`loader-rpmb2`/`loader-rpmb3` (the ramloader’s
  record writer, `0xDE` reader, set/decrement helpers), `arm.txt`/`thumb.txt`
  (LLVM dual-mode listings), `wrap.elf`/`wrapl.elf` (base `0x80000000` and
  `0x80100000`), `DumpModes.java`, `DecompileList.java`, `SeedCorrect.java`,
  `RefsTo.java`, `find_nvram.py`.
- `analysis/ramloader-commands.txt` — saved mirror of
  `https://bb10.root.sx/downloads/ramloader.txt`; confirms `0xDE = BOOT_MODE`
  and `0xE9 = boot count info`.
- (Session workspace) `HANDOFF-2026-10-20-next-session.md` — I updated it with
  the RPMB correction, the secure-world/irreversible wording, and the revised
  next-work list. The repo’s copy is the later consolidated version.
- (Session workspace) `HANDOFF-2026-10-21-next-session.md` — I wrote/rewrote
  it as a self-contained handoff; the repo copy is the later session-5
  consolidated version (it lists `72`–`87` and the live `0x2801` audit), not my
  exact rewrite.
- (Session workspace) `HANDOFF.md` — continuation section updated to point at
  the 2026-10-21 handoff.
- On-device state: none; no NVRAM/RPMB/eMMC/flash write. `hd0head.bin` hash
  unchanged.

## Deliberately left out

- The full `loaders.dat` extraction and per-loader action-0 table — session 4’s
  `72` §1–§5 does it; this note only records the PlayBook loader’s read-only
  `0xDE` and the operator’s “only `LDR_01` works” constraint.
- The `+0x9000` relocation proof and the MLO header decode — session 4’s `73`;
  this note records only the anomaly I saw and points at the resolution.
- The `0x2801` password digest arithmetic and the live audit — session 5’s
  `75`/`76`/`84`; this note records the gate as it looked from boot main.
- The full `0x2074` A/B update semantics, `switch()`/`sane()` and the resmgr
  rollback — session 5’s `83`; this note records the bootloader’s prefix view.
- The later Linux-era work (`103`–`122`) and the `BOOTCTR` top-up/drain
  history — later sessions; this note only flags where the counter is consumed.
- The Cellebrite loader audit and the native-boot plan — sessions 5/6
  (`86`/`88`/`89`/`95`); this note stops at “no pre-OS writer in the known
  signed loader”.
