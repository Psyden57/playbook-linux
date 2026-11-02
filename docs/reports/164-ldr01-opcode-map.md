# 164 — LDR_01 (`loader_06001A06-00.bin`) command dispatch table

Status: **static, offline, read-only**. No device was attached and nothing was
written to the binary or to any other file in the repo.
Target: `local/loaders/loader_06001A06-00.bin` (then at `local/vendor/bbusb/loaders/`;
the module moved to `tools/bbusb.py` on 2026-11-01)
— 238,128 B, raw ARM little-endian, load/run base `0x80100000`.
Tools: `rizin 0.8.2` (`-a arm -b 32 -m 0x80100000`), `python3`, `xxd`.
All addresses are **loader virtual addresses** (`0x8010xxxx`); file offset =
address − `0x80100000`.

Mode note: the entire command path is **ARM (A32)**, not Thumb. The only Thumb
code in the blob is in the trailer (`0x80135DD4`+ veneers and the
`0x80139130`/`0x80139208` word tables), which is not reachable from the command
loop. Everything below was disassembled with `asm.bits=32`.

Confidence tags: **[M]** = measured (read out of the disassembly at the cited
address), **[I]** = inferred (measured facts + the supplied live-probe table).

---

## Opcode map

The command byte is the **low byte** of the 16-bit command word (`frame[2]`,
loaded at `0x80117378`). The **high byte** (`frame[3]`) is *not* part of the
dispatch key: it is echoed back as the response "mode" byte
(`0x80125464 ldrb r1,[r4,3]` → `strb r1,[resp+3]` at `0x80125468`). This is why
`0x40C0`, `0x20EE`, `0x80EF`, `0x40F9` behave as `0xC0`, `0xEE`, `0xEF`, `0xF9`.
The one exception: `0xC0` and `0xF9` additionally *test* `frame[3] & 0x40`
(`0x8011B25C`, `0x801205DC`) — they act only on the `0x40xx` spelling and do
nothing at all for a bare `0x00C0` / `0x00F9`.

"State" column: **R** = reads persistent device state (flash / NVRAM / OTP / SoC
registers), **W** = mutates persistent state (flash or NVRAM), **w** = mutates
volatile RAM state only, **—** = neither.

The 13 live-measured operations are marked ★ in the last column and all
reproduce.

| op | dispatcher | handler (ARM) | response | state | what it does |
|---:|---|---|---|---|---|
| `0x20` | query | `0x80103AE8` | `0x39` (u16 len) / `0x15` | R | persistent data block; checks magic `0x84D93A96` at `[0x8013EE68]+0`, returns `[+8]` |
| `0x39` | CREAD | `0x80102EE0` | `0x38` (+len) | R | CREAD block read: `0x80111ED8(0x80141040,0x4000)` then `0x80104984` encrypt/copy — **RSA/E4-session gated** |
| `0x3A` | CREAD | `0x80102F80` | `0x20`/`0x11` (len 0) | — | CREAD length probe; always rejects (status `0x20` or `0x11`), never returns data |
| `0xAD` | query | `0x80103304` | `0x33`, 8 B | R | PMIC / power info |
| `0xAE` | actions | `0x80104094` | `0x2E` (len 0) / `6` | **W** | blocklist erase: `0x80104180(len-8)` validates the RIM-signed payload, then **deletes NVRAM `0x2819` + `0x2852`** |
| `0xAF` | actions | `0x80103BDC` | `0x29`, 8 B | w | `*(u32*)0x8013EE88 = arg` (`0x801189E8`); RAM mode flag consumed by F7/FD |
| `0xB0` | **inline** | `0x80117064` (not a dispatcher case) | `0xB5` chunks (≤512 B) then `0xD0`, 4 B | R ★ | loader action log; `0x80116DA8` chunks the buffer |
| `0xB1` | query | `0x80103568` | `0xC8`, 0x3FF0 | R ★ | `[buf]=0xFFFFFFFF`, `[+4]=0x8011A7A0()` (BS = `0x10000`), `[+8]=0x8011A784()` |
| `0xB2` | query | `0x80103520` → `0x80117DBC` | `0xC8`, 0x3FF0 | — | "DSP OS metrics": **memsets 0x3FF0 and logs — returns an all-zero buffer in this build** |
| `0xB3` | actions | `0x80103CCC` | `0xB4` (u16 len) | **R+W** | NVRAM `0x2033` action log: `0x80118688` reads the record, appends the string, writes back (`0x8011871C`) |
| `0xB4` | query | `0x8010332C` | `0xD2`, 188 B | R ★ | flash identity / regions; contains the eMMC CID product name (live: `MCGAFA`) |
| `0xB5` | query | `0x80103710` | `0xD3`, 492 B | R | **CFP's "Flash Info (CSD/ExtCSD)" call** (report `162` §1 row 8): 72 B + 156 B card-descriptor blobs, 1 B, 160 B partition table, 0x5C B tail |
| `0xB7` | query | `0x80103A3C` | `0xF3`, 0x14 B | R | 20-byte hash of the loaded bootrom/firmware image (`0x8011874C`) |
| `0xBD` | query | `0x801033E4` | `0xBE`, 8 B | R/w | hardware-override ID: reads `*0x801182E0()`, compares with `0x60009`, zeroes `0x8013ED08[0..8]` |
| `0xBF` | query | `0x80103954` | `0xD9`, 0x24 B | R | DRAM info: walks memory descriptors via `0x80124078`/`0x80124084`, sums sizes |
| `0xC0` | 0x8011AE30 | `0x8011B258` → `0x801206C8` | `6` (0x4006) / `0x2E` / `0x15` / `0xD4` | **W** ★ | COMPLETE. **Requires `frame[3]&0x40`.** Writes NVRAM `0x2074` = `{u32 2, u16 0, u8 payload[0]}` (24 B) and deletes `0x207C`, then signature gate |
| `0xC2` | query | `0x80103ACC` | `0xC2`, 0x20 B | R | 32-byte status/debug block (`0x80117A00`) |
| `0xC3` | actions | `0x80103F54` | `6` (len 0) | **W** | LED/PWM: `0x8013241C(r4[4],r4[5],r4[6],r4[7])`, `0x801247A0`, `0x8012474C`/`0x80124774` |
| `0xC4` | actions | `0x80103C10` | `0x28`, 4 B | R | read/verify a range: logs, compares `0x8011A784()` against the requested range, memcmp loop at `0x80103FF8` over `0x8BD00000` |
| `0xD1` | query | `0x80103420` | `0xC8`, 4 B | R | 4-byte SVN (`0x801180B8`) |
| `0xD2` | query | `0x80103538` → `0x80117FBC` | `0xC8`, 0x3FF0 | R | metrics variant (`0x80118900` → `[+0x28]` → `0x801188DC`); not attributed by CFP `162` |
| `0xD3` | query | `0x8010352C` → `0x80117DD8` | `0xC8`, 0x3FF0 | R | metrics variant (same skeleton as D2) |
| `0xD8` | query | `0x80103460` → `0x80117A4C` | `0xC8`, 0x3FF0 | R | OS metrics (same skeleton as D2) |
| `0xD9` | query | `0x80103544` → `0x8011814C` | `0xC9` (u16 len) | R ★ | MemConfigTable; magic `0x92BE564A` checked at `0x8011815C` |
| `0xDA` | actions | `0x80103D18` → `0x8011DEAC` | `0xCA`, 8 B / `0x15` | **R+W** | reads NVRAM `0x2027` (16 B); if `payload[0]==1` **deletes `0x2027`** |
| `0xDB` | query | `0x801039E8` | `0xCB`, 4 B | R | vendor ID (`0x80102C44`/`0x80102CF0`), cached in `0x8013ED04` |
| `0xDC` | 0x8011AE30 | `0x8011AE78` | `0xCC`, 4 B / `0xCE` | R ★ | range hash of N blocks: gate `0x80100118(block,0,count,…)` then per block `0x80101F00(state, 0x8BD00000, bs, 0)` — **not zlib CRC-32** |
| `0xDE` | actions | `0x80103D50` | `0xFA`, 0xC B | R ★ | BOOT_MODE. `arg==0` → `{status, mode, count}` via `0x8011E074`; `arg!=0` → **log only, no response** (see Open question 1) |
| `0xE0` | actions | `0x80103CF8` → `0x801199D4` | `0xC3`, 1 B | **W** | installer / OS-region op: device-info id `0x1F`, reads NVRAM `0x206C` (0x400 B), writes region state |
| `0xE1` | query | `0x8010347C` → `0x80117C08` | `0xC8`, 0x3FF0 | R | metrics dump: `0x801176F8(1)`, reads the descriptor at `0x8BD00000`, copies a 0x75-byte header plus pointer/length pairs — **CFP's "Installer Metrics" op** (report `162` §1 row 3) |
| `0xE4` | CREAD | `0x80102E00` → `0x80104970` | `0xE8` (+len) | w | CREAD init: reads 0x20/0x10/0x20/0x100 host material, sets `+0xAEC=1` — **RSA-gated** |
| `0xE5` | CREAD | `0x80102FFC` | `0xE9` (+len) / `0xE7`, 4 B / `0xCE` | R | CREAD page read via `0x80100118` + `0x80119E0C` + `0x80104984` |
| `0xE6` | CREAD | `0x80102EA0` → `0x80104BB0` | `0xEA` (+len) | R | CREAD variant |
| `0xE7` | query | `0x801034E8` → `0x80102668` | `0xD1`, 4 B | R ★ | device PIN = NVRAM `0x2002` (4 B); `0xFFFFFFFF` if absent |
| `0xE8` | actions | `0x80103ECC` | `6` / `0x2E` / `0x15` | **W** | `arg==4` → **delete NVRAM `0x2074`** (= CFP `RESETQNXOSREGION`); `arg==8` → `0x8010288C(4,1)` read-modify-write NVRAM `0x2814` bit 4; anything else → `0x15`. Success → `6`, failure → `0x2E` |
| `0xE9` | actions | `0x80103C94` | `0xFB`, 0x1C B | R | `arg==0` → read NVRAM `0x2074` (24 B) via `0x8010B954`; `arg!=0` → `0x15` |
| `0xEA` | query | `0x80103504` → `0x8011DC60` | `0xFC`, 4 B | R | BSN (board serial number) |
| `0xEB` | CREAD | `0x80102E60` → `0x80104D08` | `0xEC` (+len) | R | CREAD variant |
| `0xEC` | query | `0x80103A94` → `0x8011859C` | `0xFD`, 0x7C B | R ★ | blocked-OS table: NVRAM `0x2819`, count = len/0x0C |
| `0xED` | query | `0x80103AB0` → `0x801185F0` | `0xFE`, 0x7C B | R | blocked-**radio** table: NVRAM `0x2852`, same layout |
| `0xEE` | 0x8011AE30 | `0x8011B09C` | `0xD4` / `0xDE`, 4 B / `0x15` / `0x16` / `0x12` / `0x13` | **W** ★ | erase sector(s); `0x801001C8` gate, `0x8011ACCC`/`0x8011AD78` erase |
| `0xEF` | **inline** | `0x80117580` | `0xC7` (len 0) | — ★ | reboot: logs, sends `0xC7`, sets `[sp+0x3C]=1` which breaks the main loop |
| `0xF3` | 0x8011AE30 | `0x8011B1E4` → `0x80118644` | `0xF4`, 4 B | **W** | EraseMCT: `0x8011A500(*0x8013EEBC)` then **delete NVRAM `0x2028`** |
| `0xF7` | **direct** | `0x8011FDD8` | `0xDF` (len 0) / `0x2E` / `0x11` | w ★ | send block `{u32 seq, u32 len}` + payload; stages into `0x80259794`, ≤ `0x10000` per call |
| `0xF8` | **direct** | `0x8011FDD8` | `0xDF` | w | same routine; the only difference is `0x8011FEB0` (`cmp frame[2],0xF7`) which sets a flag for F8 |
| `0xF9` | **direct** | `0x801205CC` | `6` (0x4006) / `0x2E` | w ★ | signature trailer; **requires `frame[3]&0x40`**, len ≥ 0x230, `[+0x174]==0xC6B71C0E`, `[+0xE0]==0x88` |
| `0xFD` | 0x8011AE30 | `0x8011AEE8` | `0xDF` / `0x11` / `0x12` / `0x13` | **W** (via `0xEE`) | stage/append: queues depth 1, `0x801001C8` erase gate, copies into the 0x80000 staging window at `0x801C9000` |
| `0xFF` | query | `0x801034B8` → `0x80103258` | `0xFF`, 0x78 B | R | DDR/DRAM controller info |

**Not implemented** — all of these fall to `0x801175AC`, which logs and sends
`0x15` with a 1-byte status of `4`:

```
0x00-0x1F 0x21-0x38 0x3B-0xAC 0xB6 0xB8-0xBC 0xBE 0xC1 0xC5-0xD0
0xD4-0xD7 0xDD 0xDF 0xE2 0xE3 0xF0 0xF1 0xF2 0xF4 0xF5 0xF6 0xFA 0xFB 0xFC 0xFE
```

This corrects the client-side superset: `0xDD`, `0xF0`-`0xF2`, `0xF4`-`0xF6`,
`0xFA`-`0xFC` and `0xFE` are **not** handled by this loader.

---

## Dispatcher

### Frame handling [M]

* `0x80125318` — frame poll: returns `0` idle, `1` command available,
  `2` reset request, `3` protocol error (`0x80117030 cmp r0,3`).
* Main loop `0x80117020`-`0x80117064`: on `1` it reads `frame[2]`
  (`0x80117060 ldrbeq r0,[r0,2]`) and **special-cases `0xB0` before the
  dispatcher** (`0x80117064 cmpeq r0,0xb0`). `0xB0` therefore never reaches the
  switch — it is handled inline at `0x8011706C`-`0x801170B0`.
* `0x80117374` reloads the frame pointer; `0x80117378 ldrb r0,[r4,2]` is the
  single dispatch key.
* Response framing: `0x80125424(code, mode, len, data)` writes `u16 (len+8)` at
  `+0`, `code` at `+2`, `mode` at `+3`, then the payload
  (`0x8012545C`-`0x80125468`). The wire "0x4006" is code `0x06` with mode `0x40`
  echoed back.

### The router: `0x8011737C` (ARM) [M]

A balanced `cmp`/`bgt` tree ending in four ARM PC-relative jump tables:

```
0x8011737c  cmp  r0, 0xda
0x80117380  beq  0x80117564        -> actions dispatcher  0x80103B90
0x80117384  bgt  0x80117464
0x80117388  cmp  r0, 0xb7
0x8011738c  beq  0x80117554        -> query dispatcher    0x801032AC
0x80117390  bgt  0x801173f8
...
0x801173d4  sub r1,r0,0xb1 ; cmp r1,5    ; addlo pc,pc,r1,lsl 2   ; table @0x801173E4
0x80117404  sub r1,r0,0xbd ; cmp r1,7    ; addlo pc,pc,r1,lsl 2   ; table @0x80117414
0x80117430  sub r1,r0,0xd1 ; cmp r1,9    ; addlo pc,pc,r1,lsl 2   ; table @0x80117440
0x80117464  sub r1,r0,0xdb ; cmp r1,0x25 ; addlo pc,pc,r1,lsl 2   ; table @0x80117474
```

Entry *i* of a table lives at `<table> + 4*i`, because `add pc,pc,r1,lsl#2`
reads `pc` as `<insn>+8`. Every entry is a plain `b` to one of eight shared
tails:

| tail | meaning |
|---|---|
| `0x80117554` | call query dispatcher `0x801032AC(cmd, frame, mode)` |
| `0x80117564` | call actions dispatcher `0x80103B90(cmd, frame, buf, &status, mode)` |
| `0x80117540` | call CREAD dispatcher `0x80102DC4(cmd, frame, buf, mode)` |
| `0x80117508` | call erase/write dispatcher `0x8011AE30(frame, buf, mode, &status)` |
| `0x8011751C` | `0x8011FDD8(frame, buf, mode)` — F7/F8 block write |
| `0x80117534` | `0x801205CC(frame)` — F9 signature |
| `0x80117580` | inline reboot (EF) |
| `0x801175AC` | unknown → `0x15` + status `4` |

Decoded table contents (word by word):

* `0xB1`→query, `0xB2`→query, `0xB3`→actions, `0xB4`→query, `0xB5`→query
* `0xBD`→query, `0xBE`→unknown, `0xBF`→query, `0xC0`→erase/write,
  `0xC1`→unknown, `0xC2`→query, `0xC3`→actions, `0xC4`→actions
* `0xD1`,`0xD2`,`0xD3`→query, `0xD4`-`0xD7`→unknown, `0xD8`,`0xD9`→query
* `0xDB`→query, `0xDC`→erase/write, `0xDD`→unknown, `0xDE`→actions,
  `0xDF`→unknown, `0xE0`→actions, `0xE1`→query, `0xE2`,`0xE3`→unknown,
  `0xE4`,`0xE5`,`0xE6`→CREAD, `0xE7`→query, `0xE8`,`0xE9`→actions,
  `0xEA`→query, `0xEB`→CREAD, `0xEC`,`0xED`→query, `0xEE`→erase/write,
  `0xEF`→reboot, `0xF0`-`0xF2`→unknown, `0xF3`→erase/write,
  `0xF4`-`0xF6`→unknown, `0xF7`,`0xF8`→`0x8011FDD8`, `0xF9`→`0x801205CC`,
  `0xFA`-`0xFC`→unknown, `0xFD`→erase/write, `0xFE`→unknown, `0xFF`→query

### The four sub-dispatchers

All four are ARM functions whose first test after the prologue is a
`cmp r0, <const>`; the case bodies are reached by a `cmp`/`bgt` binary search
tree, **not** by a jump table.

1. **Query** `0x801032AC` (single exit block `0x80103A6C`-`0x80103A74`), 22 cases:
   `0x20 0xAD 0xB1 0xB2 0xB4 0xB5 0xB7 0xBD 0xBF 0xC2 0xD1 0xD2 0xD3 0xD8
   0xD9 0xDB 0xE1 0xE7 0xEA 0xEC 0xED 0xFF`. Every `0xC8` response is the
   0x3FF0 scratch buffer, so a `0xC8` reply carries stale bytes past whatever
   the handler actually filled.
2. **Actions** `0x80103B90` (exit `0x80103F48`), 10 cases:
   `0xAE 0xAF 0xB3 0xC3 0xC4 0xDA 0xDE 0xE0 0xE8 0xE9`.
3. **Erase/write/complete** `0x8011AE30` (exit `0x8011B348`), 5 cases:
   `0xC0 0xDC 0xEE 0xF3 0xFD`.
4. **CREAD** `0x80102DC4` (exit `0x80102E44`), 6 cases:
   `0x39 0x3A 0xE4 0xE5 0xE6 0xEB`.
   Note `0x80102DF8 cmp r0,0xE4 ; bne 0x80102FFC` — an opcode that reached this
   function unhandled would execute the `0xE5` body. The router only sends the
   six opcodes above here, so it is unreachable in practice.

Direct-call opcodes that bypass all four dispatchers: `0xB0` (`0x80117064`),
`0xEF` (`0x80117580`), `0xF7`/`0xF8` (`0x8011FDD8`), `0xF9` (`0x801205CC`).

Total: **48 accepted opcode bytes** (22 query + 10 actions + 5 erase/write
+ 6 CREAD + 5 direct), 208 rejected.

---

## Specific questions

### Q2 — CSD / ExtCSD

**Yes — `0xB5`, answered as `0xD3` with 492 bytes.** CFP's
`Retrieving Flash Info (CSD/ExtCSD)` issues op `0x00B5` and expects `0x00D3`
(report `162` §1 row 8, measured in `CFP.exe` at `0x44B09D`/`0x44B0A4`), and the
router here maps `0xB5` → query case `0x80103710` → `0xD3` (`0x8010394C mov
r0,0xd3`, `0x80103948 mov r2,0x1ec`). The two sides agree exactly.

What the loader actually puts on the wire is **[M]** (`0x80103710`-`0x80103950`,
record built at `sp+8`, sent from `0x80103940`):

| off | size | source |
|---|---|---|
| `0x000` | 0x48 (72) | `0x8011D8F4` → `0x8010CA74`: **72 B from card descriptor `+0x1E8`** |
| `0x048` | 0x9C (156) | `0x8011D914` → `0x8010FBC0`: **156 B from card descriptor `+0x230`** |
| `0x0E4` | 1 | first byte of `0x8011D7D4(&sp+0x1F4)` → `0x8010CA04` (card descriptor `+0x2CC`) |
| `0x0F0` | 0xA0 (160) | `0x801311C0(fp+0xF0,0xA0,0xFF)`, then up to ten 0x14-byte entries for device ids `0x2B`, `0x2C`, `0x31` (`0x801035E4`-`0x80103828`) |
| `0x190` | 0x5C (92) | `0x80112B54(0, fp+0x190)` — card slot 0 |

The three blobs (`+0x1E8`, `+0x230`, `+0x2CC`) are **consecutive fields of one
card descriptor**: 0x48 + 0x9C + 0x14 = 0xE4 = 228 B, and the descriptor stride
is `0x368` per slot (`0x801142C4 mov r1,0x6D; mul; add r5,r2,lsl#3; add r5,#0x1E8`).
They are the loader's parsed view of the card registers: 72 B at `+0x00`, 156 B
at `+0x48`. Which of those fields carry CSD vs ExtCSD content is **[I], not
established** — it needs a live `0xD3` capture to correlate; the loader never
transmits the raw 16-byte CSD or the raw 512-byte ExtCSD.

Ruled out, for the record [M]:

* No response in the entire map is 0x200 bytes long. The set of lengths is
  `0, 1, 4, 8, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x78, 0x7C, 0xBC, 0x1EC, 0x3FF0`
  plus CREAD's variable lengths.
* The only 512-byte constant on the command path is the **log chunk size** of
  `0xB0` (`0x80116DC0 cmp r4,0x200`) — a false positive for "ExtCSD".
* `0xB4` (`0xD2`, 188 B) is the *identity/geometry* record, not CSD/ExtCSD.

**`0xB4` → `0xD2`, 188 B** [M] (`0x8010332C`-`0x801033C4`), for completeness —
it is what carries the CID product string:

| off | size | source |
|---|---|---|
| `0x00` | 4 | constant `2` (`0x8010335C`) |
| `0x04` | 4 | `u16 (b0 | b1<<8)` from `0x8011A9A0(0,&b0,&b1)` — flash device id (`0x80103354`) |
| `0x08` | 4 | `*0x8013EE9C` (block size) |
| `0x0C` | 4 | `*0x8013EEB4 − 1` (last block) |
| `0x10` | 4 | `*0x8013EE98` |
| `0x14` | 0x14 | `0x8011D7D4(&sp+0x1EC)` → `0x8010CA04`: **20 B from card descriptor `+0x2CC`** |
| `0x18`-`0xB7` | — | zero |
| `0xB8` | 4 | `*0x8013EEB0` |

The live `0xD2` payload contains `MCGAFA`, the Samsung CID product-name field,
which is the 20-byte blob at `+0x14` [I].

### Q3 — DRAM / installer / DSP OS / OS metrics, HW-override ID, vendor ID, boot OS count

All of these are exposed here, on the **query** dispatcher (`0x801032AC`) except
the boot-mode/count pair. Column 4 gives CFP's own view (report `162` §1), which
matches the loader's handler mapping row for row:

| CFP `info` item | op | handler | response | CFP's own op/response (`162`) |
|---|---:|---|---|---|
| DRAM info | `0xBF` | `0x80103954` | `0xD9`, 0x24 B — iterates `0x80124078()` regions with `0x80124084(i,&desc)`, sums `[desc+8]`, packs 0x24 B at `sp+0x1E4` (`0x8010397C`-`0x801039D0`) | `0x00BF` → `0x00D9` ✓ |
| DDR controller info (extra) | `0xFF` | `0x801034B8` → `0x80103258` | `0xFF`, 0x78 B | — (not used by CFP) |
| installer metrics | `0xE1` | `0x8010347C` → `0x80117C08` | `0xC8`, 0x3FF0 | `0x00E1` → `0x00C8` ✓ |
| DSP OS metrics | `0xB2` | `0x80103520` → `0x80117DBC` | `0xC8`, 0x3FF0 — **but the handler only memsets 0x3FF0 and logs (`0x80117DC0`-`0x80117DD4`); this build has no DSP metrics and always returns zeros** | `0x00B2` → `0x00C8` ✓ |
| OS metrics | `0xD8` | `0x80103460` → `0x80117A4C` | `0xC8`, 0x3FF0 | `0x00D8` → `0x00C8` ✓ |
| additional metrics variants | `0xD2`, `0xD3` | `0x80103538` → `0x80117FBC`, `0x8010352C` → `0x80117DD8` | `0xC8`, 0x3FF0 | — (not used by CFP) |
| hardware override ID | `0xBD` | `0x801033E4` | `0xBE`, 8 B (`0x8013ED08`) | `0x00BD` → `0x00BE` ✓ |
| vendor ID | `0xDB` | `0x801039E8` | `0xCB`, 4 B (`0x80102C44`/`0x80102CF0`, cached at `0x8013ED04`) | `0x00DB` → `0x00CB` ✓ |
| boot count info | `0xE9` | `0x80103C94` → NVRAM `0x2074` | `0xFB`, 0x1C B | `0x00E9` → `0x00FB`, 4-byte zero request ✓ |
| boot mode + count | `0xDE` | `0x80103D50` | `0xFA`, 0xC B (`{status, mode, count}`) | — (not in `162`; measured live) |

The three metrics handlers (D2/D3/D8) plus `0xE1` share one skeleton:
`0x80118900()`/`0x801176F8()` → object, `0x801001EC()` → validity, then
`[obj+0x28]` → `0x801188DC()` → fill the 0x3FF0 buffer; on failure they memset
0x3FF0 and log. `0xB2` is the degenerate member: it never even tries.

**Boot OS count detail [M]** (`0x8011E074`): `0x80114224(0)` selects the store,
`0x80113FBC(&val,&len)` reads a 4-byte record, the value is clamped to `<= 0x32`
(50) at `0x8011E0D0`, and the function returns 1 when `0 < val <= 50`, else 0/2.
The `0xDE` payload is `{status, mode, count}` (12 B at `sp+0x2C`), with `mode`
from `0x8011E074`'s return in the `arg==0` path. This reproduces the live
`status=0, mode=1, count=0x32`.

**Note on `0x2074`:** CFP calls its read (`0xE9`→`0xFB`) "boot count info", and
`0xE8 arg 4` deletes that same record. So the "region dirty"/`RESETQNXOSREGION`
marker and CFP's boot-count record are the same 24-byte NVRAM record — see Q4.

### Q4 — NVRAM records

The NVRAM API is five ARM functions in `0x8010B424`-`0x8010B9D0` [M]:

| function | role |
|---|---|
| `0x8010B954(id, buf, len)` | read record |
| `0x8010B990(id, buf, len)` | write record (→ `0x8010B818`; checks attribute bit 2 at `0x8010B864 tst r0,4` and refuses with `-9` when write-protected) |
| `0x8010B7B8(id)`, aliased by `0x8010B8BC` | **delete record** |
| `0x8010B8C0()` | bulk delete of every record `0..0xFFFD` **except** `0x1000-0x10FF`, `0x3000-0x3FFF`, `0x2800-0x2BFF`, `0x2046`, `0x2010`, `0x2016`, `0x2020`, `0x2021`, `0x205A`, `0x2026` (`0x8010B8E4`-`0x8010B948`) — factory wipe |

**Is there an NVRAM write opcode?** Not a generic one. Every reachable write is
record-specific:

| op | record | operation | arguments / checks |
|---|---|---|---|
| `0xC0` (`0x40C0`) | `0x2074` **write** | `0x80120744 bl 0x8010B990`, 24 B `{u32 2, u16 0, u8 payload[0]}` | **requires `frame[3]&0x40`** (`0x8011B25C`); requires the write cursor exactly at `0x20000` (`0x801206EC rsb r6,r0,0x20000` → else status `0x14`); requires the `0x207C` delete to succeed (`0x80120760`); then the signature gate `0x801184AC`/`0x8011E0F8` |
| `0xC0` | `0x207C` delete | `0x80120764 bl 0x8010B8BC` | same call |
| `0xE8` arg 4 | `0x2074` **delete** | `0x80103F18 movw r0,0x2074`; `0x80103F1C bl 0x8010B8BC` | no range check; **return 0 → `0x80103FB4` sends `6`**, non-zero → `0x80103EFC` sends `0x2E` |
| `0xE8` arg 8 | `0x2814` read-modify-write | `0x8010288C(4,1)` → `0x801027C0`, which returns **1 on success** (`0x80102868 moveq r0,1`) | fixed bit, no user data; 1 → `0x80103FB4` sends `6`, 0 → `0x2E` |
| `0xE9` arg 0 | `0x2074` read | `0x80103CA8 bl 0x8010B954` (24 B) | `arg!=0` → `0x15` |
| `0xDA` | `0x2027` read + delete | `0x8011DEC8` read 16 B; `0x8011DEF4` delete | delete only when `payload[0]==1` (`0x8011DEE8`) |
| `0xB3` | `0x2033` read/append | `0x801186C8` read, `0x8011871C` write | length clamped to `0x3FE8` (`0x801186B4`) |
| `0xF3` | `0x2028` delete | `0x80118670` | part of EraseMCT |
| `0xAE` | `0x2819` + `0x2852` delete | `0x80104218` / `0x80104224` | only after `0x80104180` validates the RIM-signed payload (`0x80104124` checks magic `0xE276CA03` at `0x8010414C` and length `0xDAC` at `0x8010415C`) |

Record-id census (immediate scan over the full ARM disassembly) [M]:

| id | referenced at | reachable from |
|---|---|---|
| `0x2002` | `0x80102674` | `0xE7` PIN (read); also `0x8010418C` inside the `0xAE` gate |
| `0x2004` | **only** `0x8010D218`, a **data-table word**, not code | **not an NVRAM record in this build** |
| `0x2040` | **not present anywhere** | — |
| `0x2074` | `0x80103CA8` (E9 read), `0x80103F18` (E8 delete), `0x80117744` (boot scan), `0x80120744` (C0 write) | `0xC0`, `0xE8`, `0xE9` |
| `0x2801` | `0x8011DD2C 0x8011DDA8 0x8011DDF8 0x8011DE64 0x8011DF18 0x8011DFFC 0x8011E028` | password record; **not reachable from any opcode** — no dispatcher case calls into `0x8011DD00`-`0x8011E030` |
| `0x2802` | **not present anywhere** | — |
| `0x2819` | `0x80104214` (delete), `0x801185C0` (EC read) | `0xEC`, `0xAE` |
| `0x2852` | `0x80104220` (delete), `0x80118614` (ED read) | `0xED`, `0xAE` |

**For the "region dirty" marker (CFP's `RESETQNXOSREGION`):** `0xE8` with
`arg == 4` deletes NVRAM record `0x2074`, the same record `0xE9` reads and `0xC0`
writes as the OS-region state — and the same record CFP calls "boot count info"
(`0xE9` → `0xFB`, report `162` §1 row 6). It is the only opcode in the map that
clears it, it takes no value (pure delete), and there is **no range check** on
the action dword. `0xE8` with any other action (except 8) returns `0x15`,
so a stray `0xE8` cannot damage another record.

### Q5 — `0x39` as an MCT partition type / `PartitionID == 8`

**`0x39` is not a partition type in this loader, and `PartitionID == 8` cannot be
resolved.** Evidence:

1. A whole-image scan of the ARM disassembly finds exactly **two** comparisons
   against the immediate `0x39`:
   * `0x801173B4 cmp r0, 0x39` — the **command byte** in the router;
   * `0x80102DE8 cmp r0, 0x39` — the **command byte** in the CREAD dispatcher.
   There is no `cmp …, 0x39` against an MCT TLV type byte anywhere.
2. `0x39` is also a **response code** (`0x80103B08 mov r0,0x39`, opcode `0x20`)
   and a **device-registry object id** (`0x8011FF48 mov r0,0x39; bl 0x80100DA0`
   in the `0xF7` path). `0x80100DA0(id,arg)` is a driver/object lookup
   (`0x80100DDC b 0x80100D50`), not an MCT read. The `0xB5` path uses the same
   registry with ids `0x2B`/`0x2C`/`0x31` (`0x80103788`, `0x801037E4`, `0x80103844`).
3. The loader's own partition resolution does **not** scan the MCT for types:
   `0x8011B430` iterates a **fixed 4-entry u16 table at `0x80136998` =
   `{0x31, 0x2B, 0x2C, 0xFF}`** (`0x8011B44C ldr r8,[0x8011B86C]`), looks each id
   up with `0x80100DA0`, and builds a 4-entry `{start,end}` list at `0x80259000`
   from `[obj+4]`/`[obj+8]` scaled by `*0x8013EEE8+0x40`
   (`0x8011B4B8`-`0x8011B4E0`). `0xFF` marks the unused slot.
4. Address → partition index is `0x8011B588`: it walks that same 4-entry list
   (`0x8011B5A8`-`0x8011B5E4 cmp r2,4`) and returns the **index 0..3**. A sector
   it cannot place hits `0x8011B5F8` → `"Cannot find logical sector in partition
   table"` (string at `0x8011B88C`).
5. No `cmp …, 8` exists anywhere in the partition/write region
   (`0x8011B400`-`0x80121000`); the only uses of `8` there are loop bounds.

Therefore, on a live MCT such as the one measured earlier (types `0x1D 0x31 0x18
0x34 0x1B 0x1E 0x36 0x1C 0x35`×2, **no `0x39`**), the write path resolves
partitions **only** through the three device objects `0x31`/`0x2B`/`0x2C` and
the fixed list — i.e. **path 1** (`type 0x34` start + offset), never a
`PartitionID == 8` lookup. The conflicting evidence is resolved: `0x39` seen in
a trace is the CREAD **opcode**, not a partition type.

The type bytes the loader *does* compare against are the fixed-region ones used
by the flash access gate: `0x1B`/`0x1C` at `0x80101344`, `0x8010166C`,
`0x801016A4`, `0x80101C24`; `0x34` at `0x8011E374`; `0x1C` at `0x8011FB7C`,
`0x80120C28`, `0x80120DB0`, `0x80120F64`, `0x80121064`.

---

## Evidence

Every address below was re-read in this session with
`rizin -a arm -b 32 -m 0x80100000`, i.e. ARM/A32.

| address | role |
|---|---|
| `0x80125318` | frame poll (`0`/`1`/`2`/`3`) |
| `0x80125424` | response sender `(code, mode, len, data)`; header `u16 len+8`, code, mode |
| `0x80117064` | `0xB0` inline log trigger; `0x80116DA8` emits `0xB5` chunks ≤ 512 B |
| `0x80117378` | `ldrb r0,[frame,2]` — the dispatch key |
| `0x8011737C` | the router (`cmp`/`bgt` tree) |
| `0x801173E4` `0x80117414` `0x80117440` `0x80117474` | the four PC-relative jump tables (branch words) |
| `0x80117508` `0x8011751C` `0x80117534` `0x80117540` `0x80117554` `0x80117564` `0x80117580` `0x801175AC` | dispatcher tails |
| `0x801032AC` | query dispatcher (22 cases) |
| `0x80103B90` | actions dispatcher (10 cases) |
| `0x8011AE30` | erase/write/complete dispatcher (5 cases) |
| `0x80102DC4` | CREAD dispatcher (6 cases) |
| `0x8011FDD8` | `0xF7`/`0xF8` block write; parses `pfcq`/`rrcq` at `0x8011FF84`/`0x8011FFAC`; staging at `0x80259794` (`0x8011FEA0`) |
| `0x801205CC` | `0xF9` signature trailer: `0x230` bytes, magic `0xC6B71C0E` @`+0x174`, `0x88` @`+0xE0` |
| `0x801206C8` | COMPLETE body: NVRAM `0x2074` write (`0x80120744`), `0x207C` delete (`0x80120764`) |
| `0x8010B954` `0x8010B990` `0x8010B7B8` (=`0x8010B8BC`) `0x8010B8C0` | NVRAM read / write / delete / bulk-wipe |
| `0x80102668` | `0xE7` PIN → NVRAM `0x2002` |
| `0x8011859C` `0x801185F0` | `0xEC` → `0x2819`, `0xED` → `0x2852` |
| `0x80118644` | `0xF3` EraseMCT + delete `0x2028` |
| `0x80118688` | `0xB3` NVRAM `0x2033` read/append |
| `0x8011DEAC` | `0xDA` NVRAM `0x2027` read/delete |
| `0x8011E074` | `0xDE` boot-mode/count reader (clamp `0x32`) |
| `0x801199D4` | `0xE0` installer / OS-region op (NVRAM `0x206C`) |
| `0x8011B430` / `0x8011B588` | partition list builder / address→index resolver; id table at `0x80136998` |
| `0x80124078` / `0x80124084` | memory-region enumeration used by `0xBF` |
| `0x80117A4C` `0x80117DBC` `0x80117DD8` `0x80117FBC` | OS / DSP-OS (stub) / variant / installer metric collectors |
| `0x8010CA04` `0x8010CA74` `0x8010FBC0` | card-descriptor field copies: 20 B @`+0x2CC`, 72 B @`+0x1E8`, 156 B @`+0x230` |
| `0x80100DA0` | device/driver object lookup by id |
| `0x80136998` | `u16[4] = {0x31, 0x2B, 0x2C, 0xFF}` — the loader's only partition id table |

Cross-check against the client side (both already in this repo, used only as
corroboration, not as target evidence):

* `docs/reports/162-cfp-info-opcodes.md` §1 — CFP's own op/response pairs
  (`0xBD`→`0xBE`, `0xBF`→`0xD9`, `0xE1`→`0xC8`, `0xB2`→`0xC8`, `0xD8`→`0xC8`,
  `0xE9`→`0xFB`, `0xDB`→`0xCB`, `0xB5`→`0xD3`) match this loader's handler table
  **row for row**, including the `0xB5`/`0xD3` CSD/ExtCSD case.
* `docs/reports/163-cfp-resetqnxosregion.md` — CFP's `RESETQNXOSREGION`; the
  loader-side counterpart found here is `0xE8 {action=4}` → delete NVRAM `0x2074`.

Reproduction: `rizin -q -e scr.color=0 -a arm -b 32 -m 0x80100000 -c 'pd 260
@ 0x801172d0' <loader>` prints the whole router including all four tables.

## Confidence

* **High [M]** — the opcode set (48 accepted, 208 rejected), the four jump
  tables, every handler address, every response code/length quoted, and the
  NVRAM record ids reachable from opcodes. All are read straight from the
  instruction stream and reproduce the supplied live measurements
  (`0xB1`→`0xC8`, `0xB4`→`0xD2`/188, `0xD9`→`0xC9`, `0xDC`→`0xCC`, `0xDE`→`0xFA`,
  `0xE7`→`0xD1`/4, `0xEC`→`0xFD`, `0xF7`→`0xDF`, `0xC0`→`6`, `0xF9`→`6`,
  `0xEE`→`0xDE`, `0xEF`→`0xC7`, `0xB0`→`0xB5`+`0xD0`).
* **High [M]** — the *client-side* attribution of the query opcodes: every
  op/response pair CFP uses (report `162`) is produced by the loader case at the
  address listed here, with the same response code and length.
* **High [M]** — Q5 (`0x39` / `PartitionID == 8`): the negative findings come
  from a whole-image scan, which is the strongest evidence available statically.
* **Medium [I]** — the *meaning* of `0xD2`/`0xD3` (the two metrics variants CFP
  does not call): they share the collector skeleton with `0xD8`/`0xE1`, so they
  are metrics, but which metric is not proven.
* **Medium [M/I]** — the `0xB4`/`0xB5` payload offsets (offsets measured; that
  `0xB5` is the CSD/ExtCSD retrieval is established by CFP, but the field-level
  split between CSD-derived and ExtCSD-derived bytes is inferred).
* **Low [I]** — the meaning of `0xF7`'s `seq` field beyond the measured
  `len ≤ 0x10000` and the `rrcq`/`pfcq` container parse.

## Open questions

1. **`0xDE` with `arg != 0` produces no response.** `0x80103DF8 cmp r7,0;
   bne 0x80103E60` → log → `0x80103F48` return. The read path works (live:
   `status=0 mode=1 count=0x32`), but "switch mode" / "set count" appear
   unimplemented in LDR_01. A live `0xDE {1,x}` should just time out; earlier
   `{action=2,count}` evidence may have come from the OS side.
2. **`0xB4`/`0xB5` field semantics.** `0xB5` is CFP's CSD/ExtCSD retrieval, and
   the 72 B / 156 B blobs are the parsed card descriptor, but the field-level
   split between CSD-derived and ExtCSD-derived bytes needs a live `0xD3`
   capture to correlate.
3. **`0xDC` hash algorithm.** `0x80101F00(state, 0x8BD00000, bs, 0)` per block,
   accumulated over `count` blocks — a loader-private hash, not CRC-32. The
   routine was not reversed here (out of scope for the opcode map).
4. **`0xB1`'s live 12-byte response** (`FFFFFFFF 00010000 000EDA04`) versus the
   code's declared `0x3FF0` length (`0x80103488 movw r2,0x3ff0`). Either the host
   probe stopped after one frame or the sender trims. Not resolvable statically.
5. Whether the `0x2801` password records (`0x8011DD2C`-`0x8011E028`) are
   reachable at all from the command loop — no dispatcher case calls that range,
   so the loader likely only uses them during its own bootrom handshake.
6. `0xEE` against the live table: the static code emits `0xD4`, `0xDE`, `0x16`,
   `0x12`, `0x13` or `0x15`; the supplied calibration says `0x39` / `0x20DE`.
   `0x20DE` is code `0xDE` with mode `0x20` echoed, so it matches
   `0x8011B18C`; the `0x39` in that row most likely belongs to the `0x20`
   (persistent-data) response at `0x80103B08`, not to `0xEE`.

---

## Addendum (2026-11-01, main session): file-offset mapping, and independent verification

**There is no header — the mapping is simply `file_offset = VA - 0x80100000`.**
(This paragraph first claimed a `0x10000`-byte header; report 165 checked it and the
claim was wrong, so it is corrected here rather than deleted.)  The image's first
bytes are `02 00 00 ea` (the ARM boot vector) followed by the `0xD7D32D1F` tag, and
the image's own load address `0x80100000` sits at **`+0x0C`**.  So this report's
addresses are correct as written, and the `movw r0,0x2074` / `bl 0x8010b8bc` pair at
file offset `0x3F18` *is* VA `0x80103F18`.  The only consequence of the mistake is
that the "This log is encrypted." marker's VA is **`0x80138B8C`**, not the
`0x80128B8C` quoted in addendum 3.

**Verified independently (same session), with `r2 -m 0x80100000 -a arm -b 32`:**

```
0x80103ef0  mov r1,#1 ; mov r0,#4 ; bl 0x8010288c     ; the arg==8 action on 0x2814
0x80103ef4  cmp r0,#0
0x80103ef8  bne 0x80103fb4                            ; success -> sends 6
0x80103efc  mov r0,#0x2e                              ; failure status
0x80103f18  740002e3  movw r0, 0x2074                 ; <- Q4/Q5 claim, exact
0x80103f1c  661e00eb  bl   0x8010b8bc                 ; delete record
0x80103f20  cmp r0,#0
0x80103f24  bne 0x80103efc                            ; failure -> 0x2E
0x80103f28  b   0x80103fb4                            ; success -> 6
```

So the `0xE8`/`arg==4` → delete NVRAM `0x2074` finding holds, and both the
success and the failure path really do leave through different status bytes — the
same conclusion report 163 reached from the other binary.

**Open question 1 (`0xDE` write actions) is resolved the way this report guessed, and
it is confirmed at instruction level.**  The handler at `0x80103D50` splits on the
argument (`ldr r7,[r4,4]`):

```
0x80103d50  ldr r7,[r4,4]            ; arg
0x80103d60  bne 0x80103d9c           ; arg != 0 -> the action path
0x80103d88  mov r0,#0xfa             ; arg == 0 -> reply {status, mode, count}
...
0x80103e60  ldr r0,=0x700ab05        ; action path: log(...) with the arg in r1
0x80103e68  blx 0x80132254
0x80103e6c  b 0x80103f48             ; return; NO response frame is sent
```

So `action != 0` really does answer nothing, which is why a `bbusb` write call would
time out rather than fail cleanly.  The gate above it (`0x80103d9c`-`0x80103dd4`)
compares the model id against `0x6001A06` and a `0x60020` threshold before allowing
the action at all, so on this hardware the write path is doubly out of reach.

Also visible in the same region: `0x80103e70`-`0x80103ea8` builds a **`0xFB` reply of
`0x1C` bytes** (the `0xE9` record read), and `0x80103ECC` is the `0xE8` entry
(`ldr r1,[r4,4]; cmp r1,#4; beq 0x80103f10`) — consistent with the `0xE8` disassembly
in the addendum above.

---

## Addendum 2 (2026-11-01): the loader action log is gated and encrypted

A live `bbusb.py info` on 2026-11-01 returned one line from `0xB0`: **"This log is
encrypted."** — not the `S:00010011` / `QNXLOAD` entries CFP prints.  Tracing it:

* The string sits at loader **`0x80128B8C`**.  It is **not referenced by any 32-bit
  literal**, so it is reached PC-relatively — i.e. it is the loader's own message.
* Immediately after it, `0x80128D5C` is the log entry point:

```
0x80128d5c  push {r4,r5,r6,lr} ; ldr r4,=0x802886c4 ; ldr r0,[r4,0xc]
0x80128d68  cmp r0,#0 ; beq 0x80128dfc         ; nothing there -> return 0
0x80128d70  movw r1,0x8118 ; ldr r0,=0x802805ac ; bl 0x80131438
0x80128d7c  bl 0x801184ac                      ; *** signature gate ***
0x80128d80  cmp r0,#0 ; moveq r0,#0x8000 ; streq r0,[r4] ; beq 0x80128de8
0x80128d94  mov r2,#0x8000 ; ldr r1,=0x802775ac ; add r5,r1,#0x9000
0x80128da4  bl 0x8013148c                      ; memcpy 0x8000 bytes from 0x802775ac
0x80128db0  bl 0x8011f324                      ; transform over (r5-0x9000, 0x9000)
```

`0x801184AC` is the **same signature gate** the flash `COMPLETE` path uses (this
report, `0xC0` row), and `0x8011F324` is a transform over a `0x9000`-byte buffer, so
the log is **encrypted at rest behind a signature check** and the placeholder is what
the loader returns when that path does not unlock.

* CFP does not print the raw reply either: after its `0xB0` call it calls
  **`0x461750(len, src, dest = buf+0x4020)`** (`0x44E8A0` → `0x44E919`), a host-side
  transform of the log bytes.  So CFP has a post-processing step we do not.

**Open:** what satisfies `0x801184AC` in this context (it may be the loader's own
image signature, or a signature received in the session), and what `0x461750` does.
Until that is answered, `bbusb.py info` shows the placeholder and says so rather than
pretending the log is empty.

### Correction to addendum 2, and a better lead

Two things in the addendum above need care.

**`0x8011F324` is not demonstrably a decrypt.**  Disassembled, it writes a structure
around `0x8013EF34` — `0x2500|…` at `+0`, `movw 0x1001` at `+8`, `movw 0x8001` at
`+0xC`, `0xD4` at `+0x20` — and then calls `0x8011EFBC` with `0x802590E4`.  That reads
like *building and committing a record*, not transforming the log in place.  So
"the log is encrypted at rest and this decrypts it" is **unproven**; what is actually
established is only that the path is gated by `0x801184AC` and that the loader emits
the literal "This log is encrypted." on the live unit.

**The operator's lead is stronger than mine:** CFP prompts for the device password
for **every command**, including `info`, while our session authenticates only during
the loader upload.  If the loader derives a session key at the password stage and the
log needs that key, then the difference is *authentication*, not a missing algorithm —
which would also explain why nothing crypto-looking is visible in the 0xB0 handler.

**Test in flight (one run distinguishes the two):** our `0xB0` now sends a
**zero-length payload**, exactly as CFP does (`0x44E890`: `push 0xd0`, `push 0xb0`,
length 0).  We previously sent eight zero bytes, which is not what CFP sends.  `info`
also prints the first 64 bytes of the reply now, so a single run tells us whether the
answer is high-entropy ciphertext or a short placeholder record.

### Addendum 3: the `0xB0` reply is the loader's own record container

The live reply (2026-11-01, after matching CFP's zero-length `0xB0` request) starts:

```
1b 25 00 00 | "This log is encrypted." 00 | ac003020 006a2701 a4000001
0c001000 01008000 01000100 80000100 01000000 | "Bugdi…"
```

Decoded: the first word is **`0x251B = 0x2500 | (22 + 5)`**, then the 22-byte string
plus its NUL at `+4` — which is exactly what `0x8011F324` builds
(`str [r5], 0x2500|(len+5)`; `0x801311D0` copies `len+1` bytes to `r5+4`; `0xD4` at
`+0x20`).  So:

* **`0x8011F324` is a record builder, not a decryptor** — the correction above is
  confirmed by the data, and my first reading is withdrawn.
* The reply is a **container of records**, and the placeholder is **record #1**, not
  the whole log.  "Bugdi…" sits at `0x3B`, so more records follow (the full reply
  length needs one `--raw-out` capture).
* This means the loader is *telling* us the log is encrypted rather than failing to
  find it: the plaintext is a marker record inside the container, and whatever
  holds the entries is either after it or absent.

The counter also did **not** move across this run (47 before and after), which
confirms the operator's correction: loader/`info` sessions do not consume a factory
boot — only boots that reach the OS do.

### Addendum 4: the log body is encrypted — measured, with the plaintext header shape

A full `--raw-out` capture (2026-11-01) settles the question by measurement rather
than by reading code:

* reply is **34,215 bytes**;
* the region after the second record header — **33,948 bytes — has 7.98 bits/byte
  entropy**.  That is ciphertext (or compressed), not text we are failing to parse;
* the plaintext part is small and structured: at `0x00` the placeholder record
  (`0x251B` = `0x2500|27`, "This log is encrypted.\0"), then two 24-byte headers
  (`a4000001 0c001000 01008000 01000100 80000100 01000000`) spaced **0xB8** apart,
  each followed by the same 12-byte name **"Bugdisp-r036"** and a 4-byte marker
  `%ssm`.

So the loader's message is literally true: it hands us a plaintext descriptor for an
encrypted log.  CFP's host-side call at `0x461750` is the obvious decryption
candidate, and the operator's password-per-command observation remains the other
(if the key is session-derived, it never reaches the wire).

## Also measured from the same capture: three probes now decode exactly

| probe | our bytes | CFP prints | verdict |
|---|---|---|---|
| `0xBF` DRAM info (36 B) | word2 `0x400`, word4 `3`, word5 `0x100` | `Size 1024 MB`, `VendorID 0x3 Elpida`, `Revision 0x100` | **match** |
| `0xDB` vendor ID (4 B) | `{u16 1, u16 0x1F8}` | `Branding: VendorID 504` | **match** (`0x1F8`) |
| `0xBD` HW override (8 B) | all zero | `HW ID Override: None` | **match** |

And the two big blobs are *decoded structures*, not raw registers, so a field table is
derivable by aligning them against CFP's printed values:

* `0xB5` CSD/ExtCSD (492 B) starts `03 04 27 01 32 …`, which is CFP's CSD print order
  (`CSD_STRUCTURE 3`, `SPEC_VERS 4`, `TAAC 0x27`, `NSAC 1`, `TRAN_SPEED 0x32`), with
  `C_SIZE 0xFFF` at `+0x18`, `CCC 0xF5` at `+0x1C`, `MAX_ENH_SIZE_MULT 0x72E` at
  `+0x7C` and `SECTOR_COUNT 0x76D0000` at `+0x48`;
* `0xF001/var 2` bootrom metrics (736 B) holds its strings at `+0x14` (HW-ID),
  `+0x54` (`ec_agent`), `+0x64` (`Mar 24 2011`), `+0x74` (`15:31:02`) — matching CFP's
  build-user/date/time lines;
* `os_metrics` (16,368 B) holds `Nov 15 2011` at `+0x24`, `14:51:36` at `+0x30` and
  the `RIM BlackBerry Device` string at `+0x3C` — that is CFP's OS-version block.

### Addendum 5: two blobs decoded field-for-field against CFP (method + result)

The CSD/ExtCSD (492 B) and bootrom metrics (736 B) blobs are **decoded structs**, not
register images, so their offsets cannot come from the JEDEC layout.  They were
solved instead by aligning **CFP's own printed field list** (report 161 data,
`CFP-flashinfo.txt`) against the live capture
(`docs/reports/data/live-info-2026-11-01/`): a DP over (field, offset) matching each
printed value as a 1/2/4-byte little-endian integer in print order, allowing skips
and small gaps.

**CSD/ExtCSD — 50 of 56 non-zero fields located**, from
`csd_version_number` at `+0x000` through `bkops_support` at `+0x099`.  Anchors worth
naming: `c_size` `+0x018` = `0xFFF`, `ccc` `+0x01C` = `0xF5`, `erase_grp_mult`
`+0x046` = `0x1F`, `sector_count` `+0x048` = `0x76D0000` (= 63.75 GB, matching the
device), `ext_csd_rev` `+0x05B` = `5`, `hc_wp_grp_size` `+0x06C` = `0x20`,
`rpmb_size_mult` `+0x079` = `1`, `partition_support` `+0x07B` = `3`,
`max_enh_size_mult` `+0x07C` = `0x72E`, `partition_setting_completed` `+0x081` = `1`.
**Verification: every one of the 30+ fields that also appears in CFP's table has the
same value** (asserted in `tools/tests/test_offline.py`).  Not located: `s_cmd_set`,
`hpi_features`, `ini_timeout_ap`, `partition_switch_time`, `wr_rel_set`,
`wr_rel_param` — their bytes are packed into neighbouring words.

**Bootrom metrics — 13 fields located and verified** (the table is in
`tools/bbusb.py`): `+0x04/+0x06` metrics version (`6.24`), `+0x0C` bootrom version
(`0x051B0014` → `5.27.0.20`, i.e. bytes b3.b2.b1.b0), `+0x10` hardware id
(`0x06001A06`), `+0x14` the HW-ID string, `+0x54/+0x64/+0x74` build user/date/time
(`ec_agent`, `Mar 24 2011`, `15:31:02`), `+0x88` options (`0x9`), `+0xA0` drivers
(`0x10`), `+0xA4` LDR blocks (`0x80014674`), `+0xA8` bootrom size (`0x20000`),
`+0xB0` processor (`0x20`), `+0xB4` flash id (`0x0`).

Two consequences worth recording:

* **Report 162's metric offsets are 4 lower than this blob's** for the upper block
  (options `0x84` there vs `0x88` here, drivers `0x9C` vs `0xA0`, LDR `0xA0` vs
  `0xA4`, size `0xA4` vs `0xA8`, processor `0xAC` vs `0xB0`) — the same struct, read
  from 4 bytes later.  Offsets from that report must be shifted by +4 for a var-2
  blob.
* **`security`, `hardware OS id` and `BR id` are not in this blob.**  At the
  post-shift offsets the values are `0`, `0x1040103` and `0x020B3E07`, none of which
  is what CFP prints (`Enabled`, `None`, `0xFF000000`), so CFP takes those from its
  metrics object rather than the blob — consistent with report 162's note that
  `+0x20/+0x24/+0x28/+0x2C` live on the object.

### Addendum 6: `os_metrics` (0xD8) decoded — the OS-version block

Same alignment pass as addendum 5, against CFP's `info` OS block:

| offset | field | live value | CFP prints |
|---|---|---|---|
| `+0x00` / `+0x02` | metrics version (u16 minor, u16 major) | `18`, `3` | `3.18` |
| `+0x08` / `+0x0C` | OS image start / **exclusive** end | `0x800FF800`, `0x803BF610` | `0x800FF800-0x803BF60F` |
| `+0x14` | build user | `cfp` | `cfp` |
| `+0x24` | build date | `Nov 15 2011` | `Nov 15 2011` |
| `+0x30` | build time | `14:51:36` | `14:51:36` |
| `+0x3C` | hardware-id string | `RIM BlackBerry Device` | same |
| `+0x7C` | hardware id | `0x06001A06` | same |
| `+0x80` (u16) / `+0x83` (u8) | OS build / major | `4869` / `2` | `2.0.0.4869` |
| `+0x9C` | image magic | `0xC3D0E193` | — (the region-0 IFS magic, report 160) |

Three of these needed correcting against the data (the metrics version is two u16s,
the OS major is the byte at `+0x83`, and the magic is at `+0x9C`, not `+0x98`) — worth
noting because guessing byte-aligned offsets from CFP's print order would have been
wrong three times in nine fields.

### Addendum 7: two corrections about the log, and where the plaintext probably comes from

**`0x461750` is `memcpy`, not a transform.**  Disassembled, it is MSVC's unrolled
copy (`rep movs` plus `jmp [table]` dispatch).  So **CFP does not decrypt the log
host-side**, and addenda 2 and 3's "CFP post-processes/decrypts" reading is withdrawn
— the second time in this file that a routine was over-read from its call site rather
than its body.  Lesson for the next pass: disassemble the callee before attributing
crypto to it.

That leaves a contradiction and an opening.  CFP's `-V5` output shows a readable
action log (`S:00010011`, `D:00010013`, `QNXLOAD:…` entries) on this unit, while our
`0xB0` returns a plaintext "Bugdisp-r036" container with an encrypted payload.  The
salient difference is **which image is running**:

* CFP uploads **its own RAM image at `0x80139230`** (its `-V5` trace:
  "Uploading RAM Image 0x80139230").
* We upload **LDR_01 at `0x80100000`**.

**Correction, from the operator: there is only one loader.**  The ramloader is always
the same image (`LDR_01`); there is no second loader available, so `0x80139230` is not
evidence of a different payload (it is more plausibly an entry point or a stage address
inside the same image — `0x80139230 - 0x80100000 = 0x39230`, near the end of its
238 KB).  That kills the "CFP runs a different loader" idea and with it the
`loaders.dat` avenue, which the operator also ruled out.

So the remaining difference is *how the log is asked for*, not what is running.  CFP
contains `"Bugdisp"` (at `0x97998`) but **not** `"Bugdisp-r036"` and no literal
`0x80139230`; combined with our reply being a `Bugdisp-r036` container, the leading
hypothesis is that `0xB0` serves the **bugdisp crash log** and the **action log** is
either a different opcode or the same opcode with a selector argument.  A dedicated RE
pass is in flight (report 165).

Open question worth testing first, because it is cheap: our `0xB0` reply is a
**bugdisp** container.  CFP's "Loader Action Log" may be served by a *different*
opcode or a different `0xB0` argument, and we may simply be reading the crash log.

**Why CFP's image is not cheaply extractable.**  `CFP.exe` is 14.16 MB, and its
`.data` section (13.45 MB at file `0xADE00`) has **7.97 bits/byte entropy with only
0.6 % zero bytes** — i.e. it is an encrypted container, not code or a resource blob.
There are **no zlib streams over 50 KB** anywhere in the file, the `.rsrc` section is
240 bytes, and the nine scattered "ARM `push`" matches are inside that high-entropy
region (false positives).  So extracting the loader that produces the plaintext log
means cracking that container first — a much larger job than disassembling a routine.

That leaves two options for the log, cheapest first:

1. **Check whether we are reading the wrong log** (the live hypothesis).  Our `0xB0`
   reply is a *bugdisp* container (`Bugdisp-r036`); CFP's "Loader Action Log" may be a
   different opcode or the same opcode with a selector.  **Do not sweep opcodes on the
   unit**: report 164's map has write opcodes in the same range (`0xC0` COMPLETE
   writes NVRAM), so a blind sweep is dangerous.  The answer has to come from CFP's
   code (report 165).
2. Accept it and keep the plaintext header + entropy line that `info` prints.

On the password question, for the record: `bbusb.py` **does** supply the password —
`--password` defaults to `0000`, so `password_info` completes without a prompt.  CFP
prompts only because it has no default.  Whether the loader actually *challenges* in
each stage is visible with `--debug`, which prints every `password_info` response.

---

## Addendum 8: `0xB0` is the bugdisp crash log — the action log is `0xB3` (report 165)

The reason our log looked encrypted while CFP printed `S:00010011` and `QNXLOAD`
lines is now settled, and it was not crypto:

* **`0xB0` serves the bugdisp crash log.**  CFP's code at `0x44E8A0` logs "Requesting
  BootImage Handshake", then on a `0xB5` reply logs "Bugdisp log received" and writes
  `c:\log.bug` — verbatim, no decryption.  The loader likewise serves it as stored:
  `0x8011F324` builds the `0x251B` marker record and the payload stays ciphertext.
* **The action log is `0xB3` → `0xB4`**, plain ASCII out of **NVRAM record `0x2033`**,
  with no crypto anywhere in the path (`0x80118688` is the only code that references
  `0x2033`; the NVRAM layer below it has no cipher call).
* Request payload must be **132 bytes** — `{u32 action, char text[128]}` — even for a
  read (`action = 0`).  The handler at `0x80103CCC` does **no length check**, so a
  short frame would `strlen()` past it, and the append path (`action = 1`) would write
  that into NVRAM `0x2033`.  `bbusb.py` therefore always sends the full 132 bytes with
  `action = 0`, and the test asserts both.
* Reply is `{u32 textLen, char text[textLen]}`: a packed array of **NUL-terminated**
  records, newest first (report 166 corrected the earlier "newline separated" guess).
  Entries are `[SDBE]:HEX8` or `HEX8:tail`, where the hex is a Unix epoch rendered
  with `localtime`, and a trailing `--<n>` means `-ErrorCode:<n>-<text>`.

`bbusb.py info` now shows the action log from `0xB3` and keeps `0xB0` as a clearly
labelled "bugdisp crash log (encrypted at rest)" section, so the two are never
confused again.  Full analysis: `docs/reports/165-cfp-action-log-re.md`.

---

## Addendum 9: the opcode space is fully mapped; what we did *not* use

The dispatcher is exhaustively mapped (ARM `0x8011737C`, `cmp`/`bgt` tree on the low
byte, four jump tables, plus `0xB0`/`0xEF` inline and `0xF7`/`0xF8`/`0xF9` direct):
**48 opcodes accepted, 208 rejected (`0x15`, status 4)**.  So nothing is hidden — but
`bbusb.py` only uses about fifteen of them.  The rest, classified by what they cost:

**Read-only, known, and not yet exposed — candidates worth adding:**

| op | resp | what it gives |
|---|---|---|
| `0xC4` | `0x28`, 4 B | **read/verify a range**: compares `0x8011A784()` against the requested range with a `memcmp` loop.  This is the natural post-flash verifier — check what was written without booting |
| `0xDC` | `0xCC`, 4 B | **range hash** of N blocks (the gate is `0x80100118`).  Report 100 measured `0xBB454844` over 44 blocks at `0x825`, i.e. the IFS region.  Useful as a before/after fingerprint; the algorithm itself is **not** reversed (it is not zlib CRC-32) |
| `0xEC` | `0xFD`, `0x7C` B | blocked-OS table (NVRAM `0x2819`, records of `0x0C`).  Relevant when moving to 2.1.0.1917 — it says what the loader refuses |
| `0xED` | `0xFE`, `0x7C` B | blocked-**radio** table (NVRAM `0x2852`), same layout |
| `0xDA` | `0xCA`, 8 B | reads NVRAM `0x2027` (16 B).  **Deletes it only when `payload[0]==1`** |
| `0xEA` | `0xFC`, 4 B | BSN (board serial) — we only have it from CFP (`1063035517`) |
| `0xD1` | `0xC8`, 4 B | SVN (4 bytes) |
| `0xC2` | `0xC2`, `0x20` B | 32-byte status/debug block |
| `0xFF` | `0xFF`, `0x78` B | DDR/DRAM controller info |
| `0xB7` | `0xF3`, `0x14` B | 20-byte hash of the loaded bootrom/firmware image |
| `0x20` | `0x39` | persistent data block (`0x84D93A96` magic gate) |
| `0xAD` | `0x33`, 8 B | PMIC / power info |
| `0xD2`, `0xD3` | `0xC8` | metrics variants on the same skeleton as `0xD8` |

**Write or state-changing — never sweep these blindly** (they sit in the same opcode
range as the reads, which is why an opcode sweep on the unit is off the table):

`0xAE` blocklist erase · `0xC0` COMPLETE (writes NVRAM `0x2074`) · `0xC3` LED/PWM ·
`0xE0` installer · `0xE8` record delete / `0x2814` read-modify-write · `0xEE` erase ·
`0xF3` EraseMCT (deletes `0x2028`) · `0xFD` staging · `0xF8` block send · `0xF9`
signature · `0xAF` RAM-mode flag · `0xEF` reboot.

**Where the RE is *not* complete**, and why it does not block anything yet: the CREAD
family (`0x39`, `0x3A`, `0xE4`, `0xE5`, `0xE6`, `0xEB`) is RSA-gated behind host
material we do not have; `0xE0` (installer) and `0xFD` (staging) are known only by
shape; the write/erase allow-list inside `0x80100118` has not been decoded field by
field; and `0xDC`'s hash algorithm is unreversed — which is the one gap that would turn
`0xDC` from a before/after fingerprint into a standalone verifier.
