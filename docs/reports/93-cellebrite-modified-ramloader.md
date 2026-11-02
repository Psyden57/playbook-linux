# 93 — Can a modified ramloader execute on the PlayBook? (track B)

Status: **CLOSED as not feasible via the boot stage; confirmed live on the
spare 2026-10-25. H6 stage 2 live: the `0xF9` copy is session-gated; a no-op
`0xF7` session plus 40 oversize `0xF9` copies survived with no fault, and
`FS_DIRTY` stayed 1. No primitive found.** Static audit plus verification-only,
no-session and no-op-session live probes. No flash/eMMC write; one value-neutral
`FS_DIRTY` RPMB rewrite (1→1). No modified loader was executed.
Date: 2026-10-25.

Supersedes the open question in `HANDOFF-cellebrite-modified-ramloader.md` and
corrects the "handlers are not statically auditable" statement in `69` §9,
`88` §8.2 and `HANDOFF.md`.

---

## 0. Verdict (read this first)

1. **The `0xF009`/`0xF004`/`0xF00A`/`0xF005` USB upload state machine is not in
   the hidden OMAP page.** It is in the **RIM boot stage image `hd0head.bin`**
   at `0x80015084` (Thumb), the same image that carries the `RIM-BootLoader` /
   `RIM-RAMLoader` strings and the `hd0` OS-region verifier. The OMAP ROM
   (`0x40028000`) really has no `movw #0xF00x`, which is why the earlier audits
   concluded "hidden"; that conclusion was correct for the ROM and wrong for the
   live protocol. The handlers are fully statically available.
2. **`0xF00A` is a real cryptographic check, not a marker.** It calls
   `0x80014554(addr, size)` → `0x80018688(addr, addr+size, 0)`, which is the
   SHA-512 + secp521r1 ECDSA region verifier already decoded in `71` §2.2/`73`.
   It hashes `[addr, addr+size-0x230)`, checks the `QNXH-OS-1` trailer magic
   `0xB5A60BFD` at `end-0x178`, and verifies the signature at `end-0x208`
   against the fixed public key at runtime `0x8001D3D9`.
3. **We verified the signatures ourselves.** Both
   `cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A` and the
   shipped `bbusb/loaders/loader_06001A06-00.bin` (`LDR_01`) verify as
   `ECDSA(secp521r1, SHA-512(body), r, s, Q)` with the key embedded in `hd0`.
   The signature block is `r` = 68 little-endian limb bytes at trailer `+0x28`,
   `s` = 68 little-endian limb bytes at `+0x6C`. Flipping one body byte and
   recomputing the CRC-32 trailer makes verification **fail**; flipping one
   trailer byte (body unchanged) still verifies. The CRC-32 trailer is outside
   the signature.
4. **The state machine is correct and closes the BBOS trick.**
   * `0xF009` validates the range, requires 4-byte alignment and
     `size == frame_total-0x14`, then sets `setup_valid=1` and
     `verified=0`.
   * `0xF004` **clears `verified` at handler entry on every write**, requires
     the write to lie wholly inside `[setup_addr, setup_addr+size)`, requires
     4-byte alignment and an exact length, then `memcpy`s. The setup flag is
     separate and survives (otherwise the 916-chunk upload could not work).
   * `0xF00A` requires `setup_valid`, runs the ECDSA verifier, and sets
     `verified=1` **only on success**.
   * `0xF005` requires `verified=1` **and** `frame.addr == setup_addr`, replies,
     sends the `0b 01 01 00` hand-off frame, and only then `blx`es to the setup
     address.
   There is no verify/execute aliasing, no second-setup window, no
   post-verify write, and no address substitution.
5. **Answer to the exact question:** **No.** A modified ramloader cannot be
   executed on this firmware. The boot stage will not accept it, and the
   signature is not forgeable without RIM's P-521 private key. The BBOS
   three-address `WRITE_RAM_SETUP` exploit does **not** transfer to this build:
   the address check, the write-range check and the verified flag are all
   present, and the verification is cryptographic. **Confirmed live
   2026-10-25:** original → `0xFF0A` status 6; one body byte changed (CRC
   fixed) → status `0x15`; one unchecked trailer byte changed → status 6.
   See §5.1.
6. **Only remaining route:** a memory-safety bug in the **running signed
   loader** (H6), which would give code execution after the signed image is
   already executing. A hijack-lens re-audit found no proven primitive. The top
   candidate is the `0xF9` handler's length field (`frame+4`), a separate u16
   from the frame's own size word, with no bound visible in the handler; it can
   request up to 0xFDCF bytes into a session object whose bound lives in BSS.
   This is a *candidate*, not a proven overflow; a staged live probe is
   specified in §5.

---

## 1. Provenance

### 1.1 Inputs re-hashed this session

| # | Path | Size | SHA-256 | Match |
|---|---|---:|---|---|
| 1 | `cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A` | 159,968 | `03ffaf26dc0ce640c2f44dc5e223bb34d15175234f26d9876846484022a5c6dc` | ✅ |
| 2 | `cellebrite-blackberry/ufedblackberrypack.epr/05001A06_0000000A` | 138,796 | `b5f345d36e042a582ce27e383da87250af268ded046facdef05d67ca15e18a3f` | ✅ |
| 3 | `bbusb/loaders/loader_06001A06-00.bin` (`LDR_01`) | 238,128 | `c7cc33c796f9518bbc0eb929c6096f2473937ad486c579764aa4c57d8165393f` | ✅ |
| 4 | `bootdumps-2026-09-11/hd0head.bin` | 2,097,152 | `dda9d955003302adf1f0d41580c50c0c9035126487e7ac53793f4d38a3a23e3d` | ✅ (`71`/`73`) |
| 5 | `bootdumps-2026-09-11/omap4430-bootrom-0x40028000.bin` | 49,152 | `fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961` | ✅ (`HANDOFF`) |
| 6 | `bootdumps-2026-09-11/bootrom-lower-0x40020000.bin` | 28,456 | `4eded955b23dca5bb271fb45760cd2a9752da5edae9901200e8269e916d2b97c` | recomputed |

### 1.2 Tools

| Tool | Version / path | Use |
|---|---|---|
| Python | 3.14.8 | SHA-512, P-521 arithmetic, signature verification |
| Capstone | 5.0.7 | independent ARM/Thumb disassembly of the loaders |
| Ghidra | 12.1.4 (prior session output reused) | `out06001A06/decompiled.txt`, `insns.txt`, `functions.txt`; `hd0-modemap-2026-10-20/` |
| LLVM objdump | prior `hd0-modemap-2026-10-20/thumb.txt`, `arm.txt` | instruction-level evidence for `hd0` |
| `zlib` | stdlib | CRC-32 trailer check/demo |

The P-521 verifier used the standard secp521r1 `p`, `a=-3`, `b`, `Gx`, `Gy`,
`n`; the public key was read from `hd0head.bin` file offset `0x1D3D9`
(runtime `0x8001D3D9` after the `+0x9000` relocation of `73`). Key blob
SHA-256: `2fd43aeb19f8b0bc5c0085488fad7980ee270679eb250380b5d6ee8a3d479bdb`.
Point-on-curve check passed.

### 1.3 Re-verified vs taken from `88`

**Re-verified from scratch:** hashes and headers (§1.1); `06001A06` CRC-32
`87B5B04D` over `[0,len-4)` and `LDR_01` CRC-32 `FFFFFFFF`; the loader's
`0x230`-byte `QNXH-OS-1` trailer and `0xB5A60BFD` magic; the flat router
`0x8010F280`; the opcode jump tables; the `0xDE` read-only path; the separate
`0x6A0000EA` occurrences; the boot-stage handler and verifier addresses; the
ECDSA signatures.

**Taken from `88` and not re-derived:** the eMMC allow-list detail of
`FUN_8011B774`/`FUN_8011B920`, the RPMB `FS_DIRTY` gating of
`0xF7`/`0xF8`, the CREAD state machine, and the QCFM container layout. None of
those bear on the execution question.

---

## 2. The verification model (decoded)

### 2.1 Which code answers the live upload

The live `0fca:0001` stage speaks the same inner framing as `bbusb`/`bb10mt`:
`u16 total | u16 cmd | u8 seq | ... | u32 crc` at the outer layer. The `0xF00x`
loop is `FUN_80015084` (Thumb, 682 bytes, `0x80015084`–`0x8001532E`) in
`hd0head.bin`. Evidence that this is the live handler, not a look-alike:

* The switch covers exactly the live commands `0xF000`, `0xF001`, `0xF002`,
  `0xF004`, `0xF005`, `0xF008`, `0xF009`, `0xF00A` (`0x80015084`
  decompilation); `0xF006`/`0xF007` are absent, and the live client uses
  channel-0 `0x0A`/`0x0F` for the password instead of `0xF006`.
* The `0xF005` success path calls `FUN_800125fe` at `0x800152da`, which builds
  and sends exactly `{u16 8, u8 0x0B, u8 1, u16 1, ...}` = the `0b 01 01 00`
  frame the host reads as the "third read" in `69` §4.
* `0x8001530e` then executes `blx r0` with `r0 = *(frame+0xc)`, the setup
  address, which is the observed hand-off.
* `hd0head.bin` carries the `RIM-BootLoader` / `RIM-RAMLoader` strings
  (offsets `0x12488`/`0x12498`), so the RIM stage, not the OMAP ROM, owns the
  RIM USB modes.

The OMAP ROM itself was re-scanned for `0xF000..0xF00D` in ARM and Thumb
encodings (Capstone linear sweeps, both dumps): no `movw`/`movt`/imm16
command constants. The earlier "not in ROM" observation is correct; the
conclusion "therefore not auditable" was not. The handler is on eMMC in `hd0`.

### 2.2 State variables

The `0x80015084` loop keeps four locals across commands:

| local | meaning | set by |
|---|---|---|
| `bVar2` | setup valid | `0xF009` success |
| `bVar3` | image verified | `0xF00A` success; **cleared by `0xF004` and `0xF009`** |
| `puVar11` | setup address | `0xF009` |
| `uVar12` | setup size | `0xF009` |

### 2.3 `0xF009` WRITE_RAM_SETUP — `0x800151c8`

```
800151c8  mov.w r8,#0               ; bVar3 = 0   (verified cleared)
800151cc  cbnz  r6, 0x800151d8      ; bVar2 already set?
800151d8  ldr   r1,[r7]              ; frame
800151da  subs  r0,#0x14             ; declared data length = total - 0x14
800151dc  ldr   r1,[r1,#0x10]        ; size field
800151de  cmp   r1,r0
800151e0  beq   0x800151f4           ; size must equal total - 0x14
800151f4  lsls  r0,r1,#0x1e          ; size & 3 must be 0
80015202  ldr   r0,[r0,#0xc]         ; addr
80015204  lsls  r2,r0,#0x1e          ; addr & 3 must be 0
80015212  cmp   r0,r4                ; addr vs setup_addr
80015216  adds  r2,r4,r5             ; setup_addr + setup_size
80015218  add   r1,r0                ; addr + size
8001521a  cmp   r1,r2 ; bhi error    ; must fit below setup end
8001521e  cmp   r1,r0 ; bhs ok      ; reject 32-bit wrap
800151a6  movs  r3,#0
800151a8  movs  r6,#1                ; bVar2 = 1
...
8001518a  movs  r2,#0x15 ; movw r1,#0xff09  ; error reply
```

The range test is `setup_addr <= addr && addr+size <= setup_addr+setup_size`,
with an explicit wrap check. The lower bound comes from the literal at
`0x800153f0` = `0x800FF800`; the upper bound is a runtime value loaded through
the literal at `0x800153f4` (relocated by `+0x9000` for `hd0`), which the live
238,128-byte upload at `0x80100000` proves is above `0x8013A230`. The exact
upper-bound value is a residual unknown (§7).

### 2.4 `0xF004` WRITE_RAM — `0x8001516a`

```
8001516a  ldr   r0,[r7]
8001516c  movs  r6,#0
8001516e  mov   r8,r6                ; <-- verified flag r8 = 0 ON EVERY WRITE
                                     ;     (r8 is proven the verified flag: 0xF00A
                                     ;      sets it at 0x80015290, 0xF005 tests it
                                     ;      at 0x800152a4; the setup flag is the
                                     ;      other local and is not cleared here)
80015172  ldrd  r4,r5,[r0,#12]       ; r4 = addr, r5 = size
80015176  lsls  r0,r5,#0x1e          ; size & 3
80015182  lsls  r0,r4,#0x1e          ; addr & 3
800151d8  ldr   r1,[r0,#0x10] ...
800151de  cmp   r1,r0                ; size == total - 0x14
80015212  cmp   r0,r4 ; blo error    ; addr >= setup_addr
8001521a  cmp   r1,r2 ; bhi error    ; addr+size <= setup_end
8001521e  cmp   r1,r0 ; bhs ok      ; no wrap
80015256  blx   0x80018c9c           ; memcpy(addr, frame+0x14, size)
```

The write is confined to the *same* range that `0xF00A` will hash, and it
invalidates the verified flag unconditionally. This is the exact opposite of
the BBOS bug, where establish/verify/execute were three separate address
setups.

### 2.5 `0xF00A` WRITE_RAM_VERIFY — `0x80015266`

```
80015266  cbnz  r6, 0x80015272       ; require setup_valid (bVar2)
...
80015272  mov   r1,r5                ; size
80015274  mov   r0,r4                ; addr
80015276  bl    0x80014554           ; verifier
8001527a  cmp   r0,#0
80015280  beq   0x80015296           ; failure path
80015282  movs  r2,#6
80015288  movw  r1,#0xff0a
8001528c  bl    0x80014fe4           ; reply 0xFF0A status 6
80015290  mov.w r8,#1                ; <-- verified = 1 ONLY HERE
80015294  b     0x80015314
80015296  ... movs r2,#0x15 ...      ; reply 0xFF0A status 0x15
```

The verifier is:

```
80014554  ldr   r2,[pc,#0x398]       ; state block
80014556  mov   r3,r0                ; addr
8001455e  sub.w r2,r1,#0x230         ; size - 0x230
80014562  cmp   r2,#0
80014564  ble   fail                 ; need a 0x230 trailer
80014568  add   r1,r3                ; end = addr + size
8001456a  mov   r0,r3                ; start = addr
8001456c  blx   0x80018688           ; SHA-512 + ECDSA P-521
80014570  cmp   r0,#1
80014572  beq   success
80014574  movs  r0,#0
```

`0x80018688` is the verifier already decoded in `71` §2.2 and `73`:
`end-0x178 == 0xB5A60BFD`, `end-0x210` neither `0` nor `-1`, SHA-512 over
`[start, end-0x230)`, parse the fixed `0x85`-byte SEC1 P-521 point, and verify
the `r`/`s` block at `end-0x208`. The public-key pointer is the relocated
literal `0x8001D3D9`; the curve is secp521r1.

### 2.6 `0xF005` EXECUTE_RAM — `0x800152a2`

```
800152a2  ldr   r0,[r7]
800152a4  cmp.w r8,#0
800152a8  bne   0x800152b2           ; require verified
800152aa  ... error
800152b2  ldr   r0,[r0,#0xc]         ; frame addr
800152b4  cmp   r0,r4                ; must equal setup_addr
800152b6  beq   0x800152cc
800152cc  movs  r2,#6
800152d2  movw  r1,#0xff05
800152d6  bl    0x80014fe4           ; reply 0xFF05 status 6
800152da  bl    0x800125fe           ; send 0b 01 01 00 go-ahead
800152de  ... device/mode setup ...
8001530e  blx   r0                   ; r0 = frame addr
```

Exact match on the address; no size field; no second setup; no post-verify
write can survive `0xF004`'s flag clear.

### 2.7 What is signed, and what is not

The signature covers `[addr, addr+size-0x230)`. The last `0x230` bytes are the
`QNXH-OS-1` trailer:

| trailer offset | content | checked by `0x80018688`? |
|---|---|---|
| `+0x00` | `"QNXH-OS-1"` | no |
| `+0x20` | nonzero / not `-1` word | yes (nonzero check) |
| `+0x28` | `r` (68 bytes, LE limbs) | yes (ECDSA) |
| `+0x6C` | `s` (68 bytes, LE limbs) | yes (ECDSA) |
| `+0xB8` | `0xB5A60BFD` | yes |
| `+0x220` | `0x000000B4` | no |
| `+0x228` | `0xD7C82D1F` | no |
| `+0x22C` | CRC-32 of `[0,len-4)` | **no** |

This explains two things `88` treated as significant:

* `LDR_01`'s CRC-32 is `FFFFFFFF` and it still verifies — the CRC is not the
  boot check.
* The Cellebrite pack loader's CRC-32 is intact only because the pack tool
  recomputed it; it is not a signature.

The trailer is not hashed, but it is not executed: `0xF005` jumps to
`setup_addr`, the first byte of the signed body. Modifying a trailer field
(except `r`, `s`, the `+0x20` nonzero word, or the magic) does not affect
verification or the entry point.

### 2.8 Cryptographic confirmation

I extracted the fixed key and verified both PlayBook-class images:

```
key   = hd0head.bin[0x1D3D9:0x1D3D9+133]      # 04 || X(66) || Y(66)
curve = secp521r1                              # on-curve: True
body  = image[0 : len-0x230]
r     = LE_limbs(trailer[0x28:0x6C])           # 68 bytes
s     = LE_limbs(trailer[0x6C:0xB0])           # 68 bytes
e     = SHA-512(body)
check: x( (e * s^-1)G + (r * s^-1)Q ) mod n == r
```

| image | signature verifies |
|---|---|
| `06001A06_0000000A` | **True** |
| `loader_06001A06-00.bin` (`LDR_01`) | **True** |

Differential demonstration on `06001A06_0000000A`:

| modification | signature |
|---|---|
| none | ✅ valid |
| one code byte at `0x1000` flipped, CRC-32 trailer recomputed | ❌ invalid |
| one trailer byte at `len-0x100` flipped, body unchanged | ✅ valid (trailer unsigned) |

The second row is the decisive one: **a modified *code* image cannot pass
`0xF00A` even with a correct CRC**, and the last row shows why CRC-based
reasoning about "signature area non-FF" was misleading.

---

## 3. Hypothesis matrix

| # | hypothesis | verdict | governing evidence | smallest test | expected observation | risk | kill criterion |
|---|---|---|---|---|---|---|---|
| H1 | verify is skipped/optional | **not feasible** | `0xF005` requires `bVar3` (`0x800152a4`); only `0xF00A` success sets it (`0x80015290`) | upload signed loader, omit `0xF00A`, send `0xF005` | `0xFF05` status `0x15`; no `0fca:8001` | boot-stage wedge only | any successful execution without `0xF00A` |
| H2 | verify/execute aliasing (short/short, tail, wrap, cache) | **not feasible** | `0xF004` clears `bVar3` (`0x8001516e`) and confines writes to `[setup,setup+size)` (`0x80015212`–`0x80015222`); `0xF005` addr == setup (`0x800152b4`) | after good verify, send one in-range `0xF004` then `0xF005` | `0xFF05` status `0x15` | none beyond H1 | execution after a post-verify write |
| H3 | address-parameter substitution / second setup | **not feasible** | `0xF009` resets `bVar2`/`bVar3` and saves one range; `0xF005` exact-match check | setup A, verify, setup B, execute A or B | rejected at setup/execute | none | any cross-range execution |
| H4 | signature scope / CRC-only | **proven cryptographic; confirmed live 2026-10-25** | ECDSA P-521 over `[0,len-0x230)`; key `0x8001D3D9`; both loaders verify; code-flip + CRC fix fails live (`0xFF0A`/`0x15`); trailer flip passes live (§5.1) | verify a code-flipped copy (no execute) | `0xFF0A` status `0x15` | boot-stage wedge only | `0xFF0A` status 6 on a code-flipped body |
| H5 | model-ID substitution / foreign signed loader | **not a path to our bytes** | verifier does not bind the load address; window is `[0x800FF800, upper)`; body signature is address-independent | upload a loader signed for `0x80000000` at `0x80100000`, verify only | verify may pass; execution at wrong base crashes | power-cycle | any way to execute *our* bytes; none |
| H6 | loader-side control-flow hijack | **not demonstrated; stage 2 live sweep survived (closed negative)** | no host-controlled indirect call; `0xF9` `frame+4` length feeds `FUN_801035fc`, but the session handle is 0 without `0xF7` (live no-session → `0x402E`); with a no-op `0xF7` session (`0xDF`), 40 × `0xF9` `len=0x4000` (0x9A400 total, past the 0x80000 buffer) all returned `0x402E`, `0xB4` stayed `0xD2`, `FS_DIRTY` 1→1 (§5.2); `0xB3` bounded copy; `0xF7/F8`/`0xFD` cursors bounded where visible | `0xF7` no-op QCFM session, then `0xF9` length/cursor sweep | `0x402E`; `0xB4` still works | FS_DIRTY value-neutral (1→1); loader crash → power-cycle | no fault after 40 oversize copies (**met**) |
| H7 | signed-loader feature that loads/executes | **not feasible as a direct path** | router has no branch-to-host-address; indirect calls use static driver/object tables (`DAT_80103ba0`, `DAT_80113440`, `+0x24c/0x250`); `0xE0` writes MCT, `0xF9` stores a trailer | audit; no live test needed | none | — | any command taking an address and branching |
| H8 | CFP RAM agent omits verify | **not a bypass** | CFP uses the same `0xF00x` sequence; `0xF005` still needs `bVar3` | inspect CFP `0xF00x` sequence / trace | same result | — | a host sequence that executes without a verify success |
| H9 | other pack loaders | **not a bypass** | 77 `D7D32D1F` entries; all carry the `QNXH-OS-1`/`0x230` trailer; only `06001A06` at `0x80100000`; same fixed key | inventory; verify a few | same signature gate | — | a signed loader with a different verifier |
| H10 | OMAP ROM USB peripheral boot instead of RIM stage | **blocked/out of scope** | OMAP ROM has no visible `0xF00x`; the live stage matches `hd0`; HS device | not attempted | unknown | unknown | needs a different boot mode |

---

## 4. Static findings per hypothesis

### 4.1 H1/H2/H3 — the state machine

The four quoted blocks in §2.3–§2.6 are the finding. The important contrasts
with the BBOS exploit (from `wumbomumbo.txt`):

| BBOS requirement | this build |
|---|---|
| `WRITE_RAM_SETUP` accepts loosely checked addresses | `0xF009` enforces alignment, exact length, a lower/upper window and no wrap |
| three overlapping setups at `addr`, `addr+Δ`, `addr+2Δ` | one saved `setup_addr/size`; each `0xF009` resets both flags |
| verify reads a different copy from the one executed | `0xF005` requires `addr == setup_addr`, and writes cannot leave the setup range |
| execute after verify, with the verified bytes intact elsewhere | `0xF004` clears `verified` on **every** write, even a zero-length one |
| patched loader signature check is weak/buggy | SHA-512 + P-521 ECDSA over the exact body, fixed key, no CRC path in this call |

The `0xF00A` path calls `0x80018688` **directly**, bypassing the
`FUN_8000911a` selector that would allow the CRC-32 factory path for OS-region
verification. So even a factory-mode unit does not get a CRC-only RAM verify
through this handler.

### 4.2 H4 — signature and key

* Key: `hd0head.bin[0x1D3D9:0x1D3D9+133]` = `04 || X || Y`; on secp521r1.
* Signature: `r` at trailer `+0x28`, `s` at `+0x6C`, each 68 bytes of raw
  little-endian limbs.
* Digest: SHA-512 of `[0, len-0x230)`.
* Both `06001A06_0000000A` and `LDR_01` verify.
* One body byte flipped + CRC fixed → invalid.
* One trailer byte flipped → still valid.

Reproduction script:
`$COMMANDCODE_SCRATCHPAD/verify_loader_sig.py` (this session).

### 4.3 H5 — address window, not model binding

`0xF009` checks only `[0x800FF800, runtime_upper)`. The loader header's load
address at `+8` is inside the signed body but the boot stage never compares it
with `setup_addr`. A validly signed loader for `0x80000000` can therefore be
placed at `0x80100000` and will pass `0xF00A`; executing it will run
position-dependent code at the wrong base and crash. This is a robustness gap,
not a code-execution primitive: the bytes are still RIM's signed bytes.

### 4.4 H6 — loader-side hijack re-audit

Scope: the Cellebrite `06001A06` flat router `0x8010F280` (5,116 B) and its
inlined handlers, with a control-flow lens rather than the `88` eMMC lens.

**Indirect transfers.** The decompilation contains indirect calls only through
static tables:

* `FUN_80102c1c` → `param_1[10]` (a descriptor built on the stack by
  `FUN_80102d14`, function pointer passed by the caller, never from a frame).
* `FUN_8010307c`/`801030cc`/`80103078`/`80103128`/`80103180` →
  `DAT_80103ba0 + kind*0x24 + off` (driver/object method table).
* `FUN_80113440`-based flash/transport vtable calls (`+0x48`, `+0x4c`, etc.).
* `FUN_80118be8` → `*(*(DAT_80118e44+4)+0x48)`.

No host field reaches a function pointer or a `bx`/`blx` register.

**Length sites examined:**

| site | length source | destination | bound |
|---|---|---|---|
| `0xB3` `FUN_80111334` | `strnlen(frame+8)+1`, capped `0x3FE8` | NVRAM record buffer `0x8015EA50+4` | cap; NUL write stays in the 0x4000 frame buffer |
| `0xF7/F8` `0x8010F660` | frame total − 0x10, and QCFM `+0x10` total | session + flash staging `0x8017EDE0` | flush path `FUN_8010f188` bounds to `0x20000` words (0x80000 B) |
| `0xFD` `0x8010F71C` | frame total − 0x10 | staging `DAT_80110104` | explicit `0x80000` checks |
| `0xE5` `0x8010FDEC` | frame size, rounded, must be `0x3FE8`/`0x4000` | `DAT_80110148` | size gate |
| `0xF9` `0x8010FC34` | **`frame+4` u16 − 0x230, no bound visible in the handler, up to `0xFDCF`** | session `DAT_80110100[0xb]` via `FUN_801035fc` | **not resolved statically (object table in BSS)** |

The `0xF9` path is the only place where a **second, independent 16-bit length**
is used. It copies `iVar6` bytes from `frame+6` into the session object and
advances `ctx.cursor += iVar6`; then it copies a fixed `0x230` trailer from
`frame+6+iVar6` into the router's stack region (`&local_290`, which is sized
for exactly `0x230` and does not overflow). The fixed copy is safe; the
variable-length session copy is the open question. If the session object has a
0x80000-byte buffer with a cursor, repeated `0xF9` commands could pass the end;
if it validates, they cannot. The object table is populated at runtime, so this
cannot be closed from the image alone.

**Not found:** any stack buffer whose size is host-controlled; any
`memcpy` into a return-address-adjacent buffer with a host length; any
`frame+4/8` offset used as a pointer; any signed/unsigned underflow that
produces a large length. `88`'s conclusion ("no memory-safety primitive in the
audited handlers") still stands after this re-audit, with `0xF9` added as the
top residual.

### 4.5 H7 — no signed-loader execute/load command

The router's only branch-like handlers are `0xEF` (reboot) and the write/erase
paths. `0xE0` (installer) rebuilds the MCT from fixed partition types; `0xF9`
stores a trailer; `0xFD` stages; `0xF7/F8` program through the flash driver.
None takes a host address and calls it. The only `blx r0` with a host-derived
`r0` is the boot stage's `0xF005` (`0x8001530e`), which is gated by `bVar3`
and the exact-address check.

### 4.6 H8 — CFP

`71` §7c resolved CFP's `0xDE` boot-count sender and `RESETQNXOSREGION`; the
loader upload path is the same `0xF009`/`0xF004`/`0xF00A`/`0xF005`. Because
`0xF005` requires the flag set only by a successful `0xF00A`, a host-side
sequence that omits verify cannot execute. A different CFP "RAM agent" would
have to be another signed image; none is in the pack. No new evidence.

### 4.7 H9 — pack inventory

`ufedblackberrypack.epr/` has 82 files; 77 have the `D7D32D1F` signed-loader
header and a valid CRC-32 trailer. Load bases are
`0x40000000` (20), `0x10000000` (14), `0x18000000` (8), `0x80000000` (7),
`0xB8000000` (7), `0x02000000`/`0x02000008` (2), `0x20000000` (2),
`0x08030000` (1), `0x00000008` (1), and **`0x80100000` (1:
`06001A06_0000000A`)**. Four `*_00000008` files use the `EA000008` header (a
different format), and `BB.bin` is the BBOS payload. Nothing else is
PlayBook-OMAP. All signed entries share the `0x230` trailer shape; the two
PlayBook-class images both verify against the `hd0` key. A foreign loader is
still a signed RIM image; it cannot carry our bytes.

---

## 5. Live plan (static-only session; hand back to the bootrom owner)

**This session did not touch the bootrom.** The spare may be connected; the
following is the exact hand-back. Use the **spare 64 GB** only; never the
connected 64 GB reference; never the bricked 32 GB units. Coordinate with the
main session so it is not polling/uploading (single-owner bootrom).

### Phase V0 — verification-only differential probe (recommended; no execution)

Goal: confirm on hardware that `0xF00A` is the ECDSA body check and that a
code patch is rejected. Nothing is executed; the loader is uploaded to RAM and
the unit is power-cycled. No flash/NVRAM/RPMB writes.

1. Copy the signed loader and make two probe copies in
   `$COMMANDCODE_SCRATCHPAD`:
   * `probe_code.bin`: flip one byte in the body (e.g. offset `0x1000`),
     recompute the CRC-32 at `len-4`.
   * `probe_trailer.bin`: flip one byte in the trailer (e.g. offset
     `len-0x100`), leave the body and CRC alone.
2. For each of `original`, `probe_code`, `probe_trailer`:
   * power-cycle, poll for `0fca:0001`, `PING`, `GETVAR(2)`, `SETMODE(1)`,
     password `0000`, `SWITCH_CHANNEL`;
   * `0xF009 {addr=0x80100000, size=<len>}`, `0xF004` × chunks,
     `0xF00A` (capture the raw reply), **do not send `0xF005`**;
   * power-cycle.
3. Expected:
   * original → response `0xFF0A`, status `6`;
   * `probe_code` → response `0xFF0A`, status `0x15` (signature failure);
   * `probe_trailer` → response `0xFF0A`, status `6` (trailer unsigned).
4. If `probe_code` returns status `6`, **stop**: the static model is wrong on
   that unit (a different verifier or a bypass); preserve the log and hand it
   back. Do not execute anything.

Risk: a failed `0xF00A`/abandoned boot stage can wedge; a power-cycle recovers
(`69` §1). No persistent state changes.

Exact client: `bbusb/bbusb.py` (PyUSB 1.3.1, user-local), or
`analysis/rea-ghidra/playbook-usb-probe.py`. The `0xF00A` reply is currently
discarded by `bbusb.py:459`; capture it by calling `channel1()` directly.

### Phase E0 — gated execute (not recommended; only on explicit approval)

The static proof predicts this fails. If it is still requested:

1. Use a **benign** modified image: change one byte in an unused string or an
   inter-function pad, recompute the CRC-32 trailer. Do **not** change code
   semantics.
2. Run Phase V0's upload, send `0xF00A`, expect status `0x15`.
3. Send `0xF005 {addr=0x80100000}` and the third read. Expected: `0xFF05`
   status `0x15`; no re-enumeration as `0fca:8001`; the unit stays in the boot
   stage or wedges.
4. Power-cycle. Confirm the unit still boots the OS.

Do **not** run a modified loader whose code has been changed; it cannot verify,
and if the model is wrong the unit would execute unreviewed bytes. Do not use
`0xF007`/`0xF008`/`0xF00A`-adjacent destructive commands.

### Phase H6 — loader-mode fault probe (later, separate approval)

Only after the signed loader has been run normally and a full backup exists:

1. Upload the genuine signed loader, enter `0fca:8001`, drain `0xB0`/`0xB5`.
2. Establish a write session (`0xF7` with a minimal valid QCFM) so the session
   cursor is known/advanced; or use `0xF9` alone if it is stateless.
3. Send a short `0xF9` frame (`frame+3 & 0x40`, `frame+4 = 0x4000`, payload
   ~0x40 bytes) and then a normal `0xB4`.
4. Expected safe outcome: `0x15`/`0x2E` rejection or a normal `0xD2` reply.
   A hang, reset, re-enumeration, or corrupted reply is a fault surface.
5. Escalate only with a prior receive-buffer fill pattern, and only if the
   first probe shows a fault. Kill criterion: no fault after length/layout
   sweeps (0x230..0xFFCF, cursor positions) ⇒ H6 is not reachable from `0xF9`.

### 5.1 Live confirmation — 2026-10-25 (verification only)

Ran on the **spare 64 GB** unit, boot stage `0fca:0001`, model `0x06001A06`,
`BR_ver 0x051B0014`, password `0000`. Each probe was a fresh power-cycle. The
client was `analysis/loader-test-2026-10-25/probe_verify93.py` (built on the
working `analysis/rea-ghidra/playbook-usb-probe.py`); it uploads to
`0x80100000`, calls `0xF00A`, and **never** calls `0xF005`. No flash, NVRAM,
RPMB or eMMC write was issued.

| probe | image change | image SHA-256 | `0xF00A` raw (crc/size/pkt/cmd/status/msg) | result |
|---|---|---|---|---|
| original | none | `03ffaf26…c6dc` | `c1121e31140000006a020aff0600000000000080` | **PASS** (`0xFF0A`, 6) |
| code | body byte `0x1000` XOR 1, CRC-32 fixed | `f1505396…9647` | `6dc497ac140000006a020aff150000000a000080` | **FAIL** (`0xFF0A`, `0x15`, msg `0x8000000A`) |
| trailer | trailer byte `0x26FE0` (= `len-0x100`) XOR 1, CRC-32 fixed | `8edb61cc…151e` | `c1121e31140000006a020aff0600000000000080` | **PASS** (`0xFF0A`, 6) |

Every run's setup reply was `05d885db14000000010009ff0600000000000080`
(`0xFF09`, status 6). Uploads took 0.45–0.64 s for 159,968 bytes in 260-byte
chunks.

This is the live reproduction of §2.8: a body change is rejected even with a
correct CRC-32, while an unchecked trailer change is accepted. It also closes
U2/U6 — the live stage uses the `hd0` reply format and the `hd0` ECDSA
behavior.

Raw logs, the probe script, and a manifest are in
`analysis/loader-test-2026-10-25/` (`run_orig.log`, `run_code.log`,
`run_trailer.log`, `manifest.txt`, `SHA256SUMS.txt`). The modified images were
kept in the session scratchpad and are reproducible; they were not committed.

### 5.2 H6 stage 1 — `0xF9` is reachable but session-gated (2026-10-25)

Ran the genuine signed Cellebrite loader in loader mode (`0fca:8001`), then sent
one channel-2 `0x40F9` with `data = u16 0x4000 + 32 bytes 'A'` — a length field
of `0x4000` (so `frame+4 - 0x230 = 0x3DD0`) but **no write session**. Reply:
`cmd=0x402E`, zero-length payload (the handler's magic-check failure path).
A follow-up `0xB4` still returned `0xD2` / 188 bytes, so the loader survived.
No `0xF7`, `0xF8`, `0xC0`, `0xEE`, flash, NVRAM or RPMB operation was issued.

Static reason the copy did not run: `0xF9` calls
`FUN_801035fc(DAT_80110100[0xb], frame+6, iVar6, 0)`, but
`DAT_80110100[0xb]` (the write-session handle) is zero until the `0xF7` path
calls `FUN_80103648(3, &DAT_80110100[0xb], 0)`. `FUN_80102e84(0)` finds
`table[0] == NULL` (the table is zeroed by `FUN_80102eb0`) and returns 3, so
`FUN_8010307c` returns without copying. The handler ignores the return and
still adds `0x3DD0` to the cursor; the `0xF7` path resets the cursor to 0
before creating the session, so this is not persistent.

**FS_DIRTY baseline (2026-10-25).** Root access on the spare required
`BerryShell-V3.py` through a PTY: V4 `exec` root SSH failed because V4 uploads
only the first 720 bytes of the public key, while V3 uploads the full key.
`devuser` SSH works but cannot read `/dev/rpmb`. `/dev/rpmb/FS_DIRTY` exists,
4 bytes, mode `0640 root:drm_license`, value `01 00 00 00` (little-endian 1).
The `0xF7` path would read 1, OR 1, and write 1 back: the record value is
**unchanged**; only the RPMB write counter advances. This makes the no-op
`0xF7` session materially safer than a dirty/clean transition.

**Stage 2 — no-op `0xF7` session + `0xF9` sweep (2026-10-25).** With approval,
built a minimal 80-byte QCFM (`mfcq`, file count 1, total `0x1000`, file entry
`pfcq`, run count 0, run record offset/count 0). The `0xF7` first chunk returned
`0xDF`; `FS_DIRTY` was read as 1, ORed with 1, and written back as 1 (its mtime
changed 11:41 → 11:54, so the RPMB RMW really happened). `0xB4` still answered
`0xD2`/188. Then:

* `0x40F9` with `len=0x240` (variable copy 0x10) → `0x402E`.
* 40 × `0x40F9` with `len=0x4000` (variable copy `0x3DD0` each) → every reply
  `0x402E`. The cumulative copy is `40 × 0x3DD0 = 0x9A400`, i.e. `0x1A400`
  bytes past the `0x80000` staging buffer.
* `0xB4` after every fifth request → `0xD2`/188; no crash, hang, reset or fault.
* After power-cycle, `/dev/rpmb/FS_DIRTY` is still `01 00 00 00` (1).

**Result: H6 not demonstrated.** The only concrete candidate (`0xF9`'s
variable-length session copy) is session-gated, and with a live session it
survived a sweep well past the staging-buffer capacity. The append is bounded
(or the copy is otherwise safe); no memory-safety fault was observed. No flash
sector was programmed (run count 0); no `0xF7` second chunk, no `0xC0`/`0xEE`.
The `FS_DIRTY` RMW was value-neutral (1→1); only the RPMB write counter
advanced.

---

## 6. Decision and smallest next experiment

**Decision: not feasible — confirmed live.** A modified ramloader cannot be
executed on this PlayBook firmware through the boot stage. The boot stage's
`0xF00A` is a real SHA-512 + secp521r1 ECDSA verification of the exact uploaded
body against a fixed key, and the command state machine prevents unverified or
post-verify bytes from being executed. The BBOS three-address exploit is fixed
on this build. H6 (loader-side memory corruption) was then tested live and
produced no fault (§5.2); no loader-side primitive was found.

**Phase V0 is complete and confirms the static proof** (§5.1): original PASS,
body-change FAIL, trailer-change PASS, on the spare unit. The boot-stage route
is closed.

**H6 is closed as a negative** (§5.2): the `0xF9` variable-length copy is
session-gated; a no-op `0xF7` session plus 40 oversize `0xF9` copies (0x9A400
total, past the 0x80000 staging buffer) produced no fault, and `FS_DIRTY`
stayed 1. The append is bounded.

**Smallest remaining experiment:** the **gated execution-gate test**
(H1/H2/H3). Upload the genuine signed `LDR_01`, skip `0xF00A`, send `0xF005`
plus the third read, and observe whether it re-enumerates as `0fca:8001`.
Static analysis says it will not (`verified` is required and only a successful
`0xF00A` sets it); this is the one test that could falsify the gate. It is a
state change — if the gate is broken, the genuine signed loader runs — so it
needs explicit approval. Use `LDR_01` (already run live in `69`), never a
modified image. If the gate holds, the remaining work is the hardware/RPMB
route for the bricked units, unchanged from `72`.

**What this does not change:** the bricked 32 GB units still need an RPMB
`BOOTCTR_FCTRY` writer or hardware access; `06001A06` still cannot write RPMB
(`88`). The Cellebrite loader remains useful as a *diagnostic* (`0xDE` mode 2)
and as a bounded eMMC writer inside the MCT allow-list, not as a code-execution
vector.

---

## 7. Residual unknowns

| id | question | why it matters | how to close |
|---|---|---|---|
| U1 | Runtime upper bound of the `0xF009` window (literal at `0x800153f4` after the `+0x9000` relocation, dereferenced) | Determines how far a signed image can be placed; does not affect the signature | live read of the resolved pointer/IRAM; or test a setup at a high address |
| U2 | **Closed 2026-10-25.** Is the live `0fca:0001` stage exactly `hd0`? | The proof is for `hd0`; a different implementation could have a different verifier | Live body-change FAIL and the exact `0xFF09`/`0xFF0A` frames (§5.1) match `hd0`. |
| U3 | **Closed (negative) 2026-10-25.** Does `FUN_801035fc` bound the `0xF9`/`0xF7` session copy? | Decides H6 | No-session path is a no-op; with a live `0xF7` session, 40 oversize `0xF9` copies (0x9A400 > 0x80000) produced no fault; the append is bounded. |
| U4 | Are there implementation bugs in `FUN_8001AFEC` (point parser) or `FUN_8001B194` (ECDSA) that accept a degenerate signature? | A parser bug could bypass the key | targeted crypto audit / test vectors; not attempted here |
| U5 | Does the OMAP ROM's own USB peripheral-boot path (if reachable by a boot switch) use a different verifier? | Alternative route, out of scope | boot-mode experiments on the spare, with the main session |
| U6 | **Closed 2026-10-25.** `0xF009`/`0xF00A` wire words | Client compatibility | Captured `0xFF09`/`0xFF0A`, status 6/`0x15` (§5.1). |
| U7 | Does `0xF005` execute without a successful `0xF00A` on hardware? | Last live check of the H1 execution gate | Gated H1 test with genuine `LDR_01`; expected `0xFF05` status `0x15`, no `0fca:8001`. |
| U8 | **Closed (negative) 2026-10-25.** Does the session append bound the copy at the `0x80000`-byte staging limit? | The actual H6 primitive | 40 × 0x3DD0 = 0x9A400 caused no fault; bounded for this length (max 0xFDCF not separately swept). |

---

## 8. Contradictions and corrections to prior documents

| doc | statement | correction |
|---|---|---|
| `69` §9, `88` §8.2, `HANDOFF.md` | "the `0xF00x` handlers are not in the NS-readable boot ROM; static analysis may be impossible" | Correct for the **OMAP ROM** (`0x40028000`), wrong for the **live protocol**: the handlers are in `hd0head.bin` at `0x80015084`, statically available. |
| `88` §8.2 | "nothing shows a way to make the bootrom accept a modified image" | Now proven: `0xF00A` → `0x80014554` → `0x80018688`, SHA-512 + P-521 ECDSA, fixed key `0x8001D3D9`; both shipped loaders verify, code flips do not. |
| `88` §2, §9 | emphasis on the CRC-32 trailer ("intact" vs "blank") | The CRC-32 is in the **unsigned** `0x230` trailer and is not checked by `0xF00A`. `LDR_01`'s `FFFFFFFF` is harmless. |
| `88` §8.2 | "no signature re-check after the write" | Stronger: the write itself clears the verified flag, and the execute requires the exact verified address. |
| `88` §9.1 | "no arbitrary RAM write / code execution" | Confirmed for the loader; the boot stage itself has no host-address branch beyond the gated `0xF005`. |
| `86` §1 | "entry `0x801234FC`" | Still wrong; `88`'s descriptor-table correction stands. |
| `71` §2.2 | P-521 verifier for the OS region | Confirmed **and reused for the ramloader upload**; the same body/trailer format and key. |
| `72` §3.1 | `LDR_01` masks `0xFFFFFFFF` as absent | Unchanged; irrelevant to execution. |

---

## 9. Reproduction

```bash
cd /home/psyden/rea-work

# 1. hashes
sha256sum \
  cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A \
  bbusb/loaders/loader_06001A06-00.bin \
  bootdumps-2026-09-11/hd0head.bin

# 2. loader trailer shape (QNXH-OS-1 + 0xB5A60BFD + 0x230)
python3 - <<'PY'
import struct
for p in ('cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A',
          'bbusb/loaders/loader_06001A06-00.bin'):
    d=open(p,'rb').read(); L=len(d); t=d[L-0x230:]
    print(p, L, t[:16], hex(struct.unpack_from('<I',t,0xb8)[0]),
          hex(struct.unpack_from('<I',t,0x20)[0]))
PY

# 3. cryptographic verification (this session's script)
python3 "$COMMANDCODE_SCRATCHPAD/verify_loader_sig.py" \
  cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A \
  bbusb/loaders/loader_06001A06-00.bin

# 4. boot-stage handler disassembly (Thumb)
python3 bootdumps-2026-09-11/analysis/loader-audit-2026-10-21/disarm.py \
  bootdumps-2026-09-11/hd0head.bin 80000000 80015160 thumb 120

# 5. verifier disassembly
python3 bootdumps-2026-09-11/analysis/loader-audit-2026-10-21/disarm.py \
  bootdumps-2026-09-11/hd0head.bin 80000000 80014554 thumb 40
python3 bootdumps-2026-09-11/analysis/loader-audit-2026-10-21/disarm.py \
  bootdumps-2026-09-11/hd0head.bin 80000000 80018688 arm 50

# 6. live verification-only probe (2026-10-25; results in §5.1)
#    one fresh power-cycle per run; never sends 0xF005
python3 bootdumps-2026-09-11/analysis/loader-test-2026-10-25/probe_verify93.py \
  --image "$COMMANDCODE_SCRATCHPAD/probe93/probe_orig.bin" \
  --label ORIGINAL --timeout 180
# probe copies: probe_orig.bin / probe_code.bin / probe_trailer.bin, built as
# described in analysis/loader-test-2026-10-25/manifest.txt
```

Note: the `hd0` image is linked `0x9000` below its load address; absolute
literals need `+0x9000` (`73`). Branch/call targets are already correct.

---

## 10. Bottom line for the main session

* **The modified-ramloader route is closed — confirmed live** (original PASS,
  one body byte changed → `0xFF0A` status `0x15`, unchecked trailer byte
  changed → status 6; §5.1). The signed image is genuinely verified, not
  marker-checked, and the command state machine cannot be reordered to execute
  unverified bytes.
* **The prior "hidden page" blocker was misdiagnosed.** The upload protocol is
  in the RIM `hd0` stage, which we already have; the OMAP ROM's absence of
  `0xF00x` is irrelevant to the live path.
* **H6 is closed as a negative.** A no-op `0xF7` session (`0xDF`) created the
  session; 40 oversize `0xF9` copies (0x9A400 total, past the 0x80000 staging
  buffer) all returned `0x402E`, `0xB4` kept working, and `FS_DIRTY` stayed 1.
  The `0xF9` append is bounded; no loader-side memory-safety primitive.
* **Remaining live question:** the optional H1 execution-gate test with genuine
  `LDR_01` (skip `0xF00A`, send `0xF005`; expected `0xFF05` status `0x15`, no
  `0fca:8001`). Otherwise the work returns to the bricked-unit RPMB/hardware
  route.
* **Do not** run a patched loader on any unit. **Do not** touch the connected
  reference. **Do not** use the bricked 32 GB units as test beds.
