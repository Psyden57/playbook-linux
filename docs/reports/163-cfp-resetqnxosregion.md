# 163 — CFP.exe `RESETQNXOSREGION`: static analysis

Status: COMPLETE for the five questions. No device was touched; the binary was
read only.

Target: `/home/psyden/SSDdrive/playbook-linux/local/vendor/CFP.exe`
- PE32 i386, MSVC "vc9", 14,159,872 bytes, ImageBase `0x400000`
- `.text` VMA `0x00401000`, size `0x85B96`, FO `0x400` (VMA = FO + 0x401000)
- `.rdata` VMA `0x00487000`, size `0x27DFE`, FO `0x86000` (VMA = FO + 0x401000)
- `.data` VMA `0x004AF000`, FO `0xADE00` (VMA = FO - 0xADE00 + 0x4AF000)

Conventions: `[M]` = **measured** (constant/instruction read out of the
disassembly or bytes read out of the image); `[I]` = **inferred** (shape or
cross-document reasoning). Every claim carries a VMA; FO is given where it helps.

## TL;DR

`CFP.exe RESETQNXOSREGION` does exactly one thing on the wire:

```
inner frame (channel 2), 12 bytes total, CRC-32 terminated:
  +0  u16 0x000C          total length (payload 4 + 8)
  +2  u8  0xE8            ramloader opcode
  +3  u8  0x00            mode/flags byte
  +4  u32 0x00000004      argument: the "wipe/clear selector" = 4
  +8  u32 ~CRC32(bytes 0..7)
```
- record cleared: **NVRAM record `0x2074`**, and the operation is a **record
  delete**, not a value write (device side, cross-referenced).
- value written: nothing. The only value on the wire is the `u32 4` selector.
  If the loader implements delete as a write, the record is **24 (0x18) bytes**.
- transport: the **ramloader/loader opcode channel** (16-bit-command frame,
  "channel 2"), *not* a bootrom `0xF0xx` opcode and **not** a `0xF7` block write
  to the `0x1E OS NV` extent.
- no read-first, no verify, no confirmation prompt; one command, then a
  hard-coded success message.

## Command dispatch

### 1a. The command-name table (measured)

`RESETQNXOSREGION` lives in a **37-entry, 12-byte-stride name table** in
`.rdata`, together with every other CLI verb `[M]`:

| field | offset in record *i* | VMA | note |
|---|---|---|---|
| `u32 flags` | `0x48c4c4 + 12*i` | — | 0 = normal CLI verb, ≠0 = only reachable via `PATRIOT -o` |
| `char* name` | `0x48c4c8 + 12*i` | points into `.rdata` | ASCIIZ |
| `u32 ordinal` | `0x48c4cc + 12*i` | — | 1-based display ordinal |

- table spans VMA `0x48c4c4` … `0x48c683`; the last record's ordinal (`37`) is
  also used as the linear-scan bound at `0x48c67c` `[M]` (`cmp DWORD PTR
  ds:0x48c67c,edi` at `0x401fd2`).
- the string `"RESETQNXOSREGION"` is at VMA **`0x48c48c`** (FO `0x8b48c`) `[M]`.
- its record is #24: flags `0` at `0x48c5e4`, name ptr `0x0048c48c`
  at `0x48c5e8`, ordinal `25` at `0x48c5ec` `[M]`.
- `cmdId` = **0-based record index**. Proof: the next record, `PATRIOT`
  (name at `0x48c5f4`, index 25), is tested against the literal `0x19` = 25 at
  `0x402080`/`0x402085` in the dispatcher `[M]`.
- therefore `cmdId(RESETQNXOSREGION) = 24` `[M]`.
- its `flags` byte is `0`, i.e. it is a first-class top-level verb; it is only
  *absent from the help text* because the help printer (`0x4025e0`-`0x40296f`)
  is a hand-written list of `push str/printf` pairs that simply omits it —
  `push 0x48a9d4 "HELP"` at `0x40264e`, `push 0x48b208 "FLASHINFO"` at
  `0x402676`, `push 0x48b2e0 "INFO"` at `0x402662`, and no push of `0x48c48c`
  anywhere in that function `[M]`. (`SETNFC_LOCK_FLAG`, `LOCKSTATUS`, `BERBUG`,
  `RESETLOG`, `MEMTEST` are hidden the same way.)

### 1b. The name lookup (measured)

Dispatcher function entry `0x401fc0` (`__thiscall`, takes the parser object,
uses `argv` at `[this+0xe8]` / `argc` at `[this+0xe4]`):

```
0x401fca  xor  edi,edi                 ; i = 0
0x401fcc  mov  [esi+0xe0], -1          ; this->cmdId = -1
0x401fd2  cmp  ds:0x48c67c, edi        ; 37 vs i
0x401fde  mov  ebp, 0x48c4c8           ; &table[0].name
0x401fe3  cmp  BYTE PTR [ebp-4], 0     ; table[i].flags == 0 ?
0x401fe7  jne  0x402002                ;   no  -> skip (hidden group)
0x401fec  mov  ecx, [esi+0xe8]         ; argv
0x401fe9  mov  eax, [ebp+0]            ; table[i].name
0x401ff2  mov  edx, [ecx]              ; argv[0] = the typed command
0x401ff6  call 0x465d97                ; strcmp(name, argv[0])
0x402000  je   0x402010
0x402002  inc  edi ; add ebp,0xc        ; next record
0x402010  mov  [esi+0xe0], edi         ; this->cmdId = i   <-- 24 for RESETQNXOSREGION
```

- comparison site: **`0x401ff6`** (`call 0x465d97`, a strcmp; the two arguments
  are pushed at `0x401ff4`/`0x401ff5`) `[M]`.
- a **second** scan loop at `0x402166`-`0x4021a2` (`mov ebp,0x48c5f4` at
  `0x402170`) is the `PATRIOT -o <subcommand>` path: it matches only the
  `flags != 0` records (`cmp BYTE PTR [ebp-0x4],0x0` / `je` skip at
  `0x402175`-`0x402179`) `[M]`. That is why `HELP/INFO/LOAD/...` appear twice
  in the table (records 0-25 vs 26-36).

### 1c. Perform → handler (measured)

Level 2 — per-command argument parser: a **37-entry jump table at `0x40250c`**,
dispatched at `0x40221c`-`0x40222e`:

```
0x40221c  mov  eax,[esi+0xe0]
0x402222  lea  ecx,[eax+1]
0x402225  cmp  ecx,0x24
0x402228  ja   0x402501              ; epilogue only
0x40222e  jmp  DWORD PTR [ecx*4+0x40250c]
```
`table[25]` (`cmdId` 24 + 1) = **`0x402501`**, which is nothing but the function
epilogue (`mov eax,edi / pop…/ ret 4`, `edi` still 0) `[M]` — i.e.
`RESETQNXOSREGION` takes **no arguments** and always passes the parse stage.

Level 3 — the actual execution switch, in the CLI function:

```
0x403998  mov  eax,[esp+0x158]        ; = cmdId (see note)
0x40399f  dec  eax
0x4039a3  cmp  eax,0x23
0x4039a3  ja   0x404b4b              ; default: do nothing
0x4039a9  jmp  DWORD PTR [eax*4+0x404cdc]     ; 36-entry table at 0x404cdc
```
`[esp+0x158]` really is the same `cmdId`: the shared READ/WRITE/CREAD block
compares it to `0x15` = 21 (CREAD) at `0x4041da`, and the shared
GETHISMODE/SETHISMODE block compares it to `0xd` = 13 at `0x4044a1` `[M]`.

`table[cmdId-1]`: `cmdId` 24 → index 23 → **`0x404d38`** → **handler block
`0x404908`** `[M]`.

### Handler entry (the answer to Q1)

**Handler entry: `0x404908`** (block `0x404908`-`0x40494a`), reached from the
switch at `0x4039a9` via `[0x404cdc + 4*(24-1)] = [0x404d38] = 0x404908` `[M]`.

```
0x404908  mov  ecx,[esp+0x7c]        ; port/device selector string (-u<port>)
0x40490c  mov  eax,[edi]             ; device object vtable
0x40490e  mov  edx,[eax+0x38]        ; -> virtual "open/connect"
0x404911  push 0
0x404913  push 0
0x404915  push 0xffffffff
0x404917  push ecx
0x40491a  call edx                   ; Connect(port, -1, 0, 0)
0x40491e  test esi,esi
0x404920  jne  0x404b4b              ; abort on error
0x404926  push 0x4
0x404928  mov  ecx,edi
0x40492a  call 0x422270              ; <-- the work function, arg = 4
0x40492f  test esi,esi
0x404933  jne  0x404b4b              ; abort on error
0x404939  push 0x48cf44              ; "\nQNX OSREGION NVRAM record cleared.\n"
0x40493e  push 0x1181dd0             ; CLI output object
0x404943  call 0x406f40              ; printf-style printer
```

Note the message string starts with a newline: VMA **`0x48cf44`**
(FO `0x8bf44`) = `"\nQNX OSREGION NVRAM record cleared.\n"` `[M]`. (Searching
for `0x48cf45` finds nothing — that is why a naive xref hunt "misses" it.)

## NVRAM record

**Answer: NVRAM record `0x2074`. The operation is a record *delete*. CFP itself
contains no record id at all — it sends a 4-byte selector whose value is `4`.**

Measured, CFP side:

- the argument passed to the work function is the immediate **`4`**
  (`push 0x4` at `0x404926`) `[M]`.
- that `4` becomes the 4-byte frame payload (`mov [esi+0x24],edi` at
  `0x44e4ea`, see "Value written") `[M]`.
- the image contains **no** `0x2074`, `0x2002`, `0x2004`, `0x2040`, `0x2801`,
  `0x2802`, `0x2819`, `0x4805`, `0x4844` as a `u32` anywhere — whole-file dword
  scan, hits: `0x2002` 0, `0x2004` 0, `0x2040` 1 (inside a word table at
  `0x49bd08`, not a record id), `0x2074` 1 (byte pattern inside string data at
  `0x48cefa`, not a record id), `0x2801` 0, `0x2802` 0, `0x2819` 0, `0x4805` 0,
  `0x4844` 0 `[M]`. There is no table of record ids and no immediate: the
  selection is **not** local at all.

Cross-reference to the device side (our own prior RE, `docs/reports/164-ldr01-opcode-map.md`
and `docs/reports/66-ramloader-opcodes-and-sachesi-debug.md` §3.4): the loader's
opcode `0xE8` handler dispatches on the 32-bit argument:

```
0x80103ECC  (case 0xE8)
0x80103F18  movw r0, 0x2074
0x80103F1C  bl   0x8010B8BC      ; 0x8010B8BC is the NVRAM *delete record* primitive
```
`arg == 4` → delete NVRAM **`0x2074`**; `arg == 8` → `0x8010288C(4,1)`
read-modify-write of NVRAM `0x2814` bit 4 `[I — from report 164]`.

That split is independently corroborated **inside CFP** by the other caller of
the same work function: `SETNFC_LOCK_FLAG` (record #6, exec block `0x404457`)
calls `0x422270` with **`8`** (`push 0x8` at `0x404475`, `call 0x422270` at
`0x404479`) `[M]`. Two commands, two selectors, `4` and `8` — exactly the two
argument values report 164 lists for opcode `0xE8`.

`0x2074` is the OS-region state record: `0xE9` reads 24 bytes of it, `0xC0`
(COMPLETE) writes `{u32 2, u16 0, u8 payload[0]}` into it, and a boot scan reads
it at `0x80117744` (report 164, Q4 table).

So: **record id `0x2074`** `[I, cross-binary]`, reached by an opaque selector
`4` `[M]`. Not a table lookup, and not an immediate — a *protocol argument*.

## Value written

**Nothing is written by CFP.** The command is a delete, and CFP's only value on
the wire is the 4-byte argument `4`.

Measured, CFP side — the whole protocol body is `0x44e4b0`
(`WipeSecurity`, log tag `WSEC`):

```
0x44e4b0  ...
0x44e4b8  mov  eax,[esi+0x10]          ; logger
0x44e4c1  push 0x49eda8                ; "WipeSecurity: 0x%08x"
0x44e4ce  test edi,edi / je 0x44e52a   ; arg == 0  -> return 4 (error)
0x44e4d2  lea  eax,[edi-1]
0x44e4d5  test edi,eax / jne 0x44e52a  ; not a power of two -> return 4
0x44e4e1  push 0x49ed90                ; "Sending WIPE_SECURITY"
0x44e4ea  mov  [esi+0x24],edi          ; <-- payload dword := 4  (buf+4)
0x44e4ed  movzx eax, WORD [esi+0x801a] ; arg6 (timeout nudge)
0x44e4f4  push eax                     ; arg6
0x44e4f5  push 0x4                     ; arg5 = payload length   = 4
0x44e4f7  lea  ecx,[esp+0x14]
0x44e4fb  push ecx                     ; arg4 = &status (out)
0x44e4fc  push 0x0                     ; arg3 = frame[3] mode    = 0
0x44e4fe  push 0x6                     ; arg2 = expected status  = 6
0x44e500  push 0xe8                    ; arg1 = opcode           = 0xE8
0x44e507  call 0x448530                ; Exec(...)
0x44e51a  push 0x49ed78                ; "Received successful ACK"
```

Key points `[M]`:
- payload length `4` (not 24, not 0x4000) — `push 0x4` at `0x44e4f5`, consumed
  as the frame payload length in `0x448530` → `0x459700`.
- the payload *value* is the argument itself (`4`); it is placed at `this+0x24`
  and the frame builder at `0x459700` writes its 4-byte header at `this+0x20`,
  so `this+0x24` is exactly `frame+4`.
- the argument must be a non-zero power of two (`0x44e4ce`-`0x44e4d7`) — i.e.
  the API is a **bit selector / mask**, which is consistent with "clear exactly
  one thing" and with `SETNFC_LOCK_FLAG` using the *other* bit `8`.
- there is no `0xFFFFFFFF`, no zero-fill buffer, no magic constant, no struct
  initialisation anywhere in this path.

Device side `[I — report 164]`: `0x8010B8BC(id)` = delete record. If the loader
implements "delete" as a write, the record extent is **24 bytes (0x18)**
(consistent with `0xE9` reading 24 bytes of `0x2074` and `0xC0` writing a
24-byte struct). CFP does not learn or carry that length.

## Transport

**Answer: the ramloader / loader opcode channel ("channel 2" inner frame).
Not the bootrom `0xF0xx` set, and not a `0xF7` block write of the `0x1E OS NV`
extent.**

### The frame builder (measured)

`0x459700(this, u8 opcode, u8 mode, void* buf, u32 payloadLen)`:

```
0x459701  eax = payloadLen              ; arg4
0x459705  dl  = mode                    ; arg2
0x45970a  esi = buf                     ; arg3
0x45970e  eax += 8
0x459711  mov WORD [esi], ax            ; +0 u16 total = payloadLen + 8
0x459722  mov BYTE [esi+2], opcode      ; +2 opcode        (arg1)
0x459732  mov BYTE [esi+3], mode        ; +3 mode/flags    (arg2)
0x459735  call 0x443e40                 ; CRC-32 over bytes 0 .. total-5
0x459743  mov [esi+total-4], ~crc       ; +len-4 complemented CRC32
0x45974e  call [vtbl+0x14]              ; virtual Write(buf, total)
```
- CRC: `0x443e40` is a stock table-driven reflected **CRC-32** (poly
  `0xEDB88320`, table at `0x49be98`), and the stored value is `not crc`
  (`0x459741`) `[M]`.
- payload goes at `frame+4`; the caller pre-stores it (here `this+0x24`, i.e.
  `buf+4`) `[M]`.
- the executor `0x448530` (`ret 0x18`, 6 args) is the single funnel:
  `Exec(opcode, expectedStatus, mode, u8* outStatus, payloadLen, timeoutHint)`.
  It logs, calls `0x459700`, receives via `0x459760` into `this+0x401c`
  (0x3FFC-byte buffer), and treats the response's **frame byte +2** as the
  status byte (`mov al,[esi+2]` in `0x459760` at `0x4597ee`/`0x4597f8`) `[M]`.
- status handling in `0x448530`: `0x18` → log, `0x19` (busy) → **resend**
  (`0x4485c3`-`0x448602`, loops back to `0x44863f`), `0x1a` → log; otherwise
  `0x448130(status, expected)` is called, which is literally
  `cmp al,cl / jne … / return 0` at `0x44813b` — success means
  **received status == expected status** `[M]`.

So the on-wire frame for `RESETQNXOSREGION` is `[M]`:

```
0C 00  E8 00  04 00 00 00  <~CRC32 of the first 8 bytes>
^^^^  ^^ ^^  ^^^^^^^^^^^
len   op md   arg = 4
```

### Why this is the "channel 2 / loader" frame and not something else (measured)

- The layout matches the device-side router documented in report 66 §1 exactly:
  *"inner channel-2 frame … switches on the byte at frame offset +2 (the low
  byte of the u16 command word); the high byte at +3 is the mode/sequence byte"*.
  CFP writes exactly `+2 = opcode`, `+3 = mode` `[M]`.
- The `mode` byte is not cosmetic: `COMPLETE` (`0xC0`) is sent with
  `mode = 0x40` (`push 0x40` at `0x44be54`, `push 0xc0` at `0x44be58`) and the
  device side requires `frame[3]&0x40` for that opcode (report 164); for
  `RESETQNXOSREGION` the mode is `0x00` (`0x44e4fc`) `[M]`.
- `expectedStatus` in `Exec` is demonstrably the device's documented response
  code, e.g. `0xC4`/`0x28` at `0x44e616`/`0x44e613` (READVERIFY → `0x28`),
  `0xB4`/`0xD2` at `0x44b368`/`0x44b36d`, `0xE8`/`0x06` at `0x44e4fe`, all
  matching the response column of report 66 `[M]`.

### Ruled out (measured)

1. **`0xF7` block write — ruled out.** The `0xF7` code path is a *different*
   function: `push 0xf7` appears exactly twice in the whole image
   (`0x44de18`, `0x44de60`), inside the block get/set helper that also uses
   `0xF4`, and neither is reachable from `0x44e4b0` or `0x404908` (no call or
   jump from the QNX block into that function; `0x448530`'s own `cmp bl,0xf7`
   at `0x4485da` only tags a retry counter for other callers) `[M]`.
2. **Bootrom `0xF0xx` opcode — ruled out.** The only opcode the QNX path sends
   is the immediate at `0x44e500` = `0xE8` `[M]`. All 100 `Exec` call sites in
   the image were enumerated (script over the full `.text` disassembly); the
   QNX path's is one of them and it is the *only* call site whose `arg1` is
   `0xE8` with a non-zero payload length.
3. **Direct NVRAM/flash write — ruled out.** The extent words `0x4805`,
   `0x4844`, `0x1E` as data, and every candidate record id, are absent from the
   image (see "NVRAM record"), and the whole path is one `0x448530` call `[M]`.
4. **A different command block** — ruled out by three independent message
   calibrations (below) `[M]`.

Not traced (stated plainly): the layer **below** the virtual `Write` at
`([this+0x14]->vtbl)+0x14` (i.e. the USB endpoint/channel selection inside
`0x45974e`). It is common to *every* command, including `INFO`/`FLASHINFO`, so
the frame above is complete at the protocol level; the USB wrapper itself was
out of scope for this report.

## Sequence

Measured, in order, from `0x404908` + `0x44e4b0` `[M]`:

1. `Connect(port, -1, 0, 0)` — device object virtual `[vtbl+0x38]`
   (`0x40490e`/`0x40491a`). Failure → abort, nothing printed.
2. `WipeSecurity(4)` (`0x40492a`). Inside:
   a. gate `[this+4] == 6` (`0x422278`) — see "device mode" below;
   b. log `"WipeSecurity: 0x%08x"` (`0x44e4c1`);
   c. reject `0` / non-power-of-two (`0x44e4ce`-`0x44e4d7`, returns 4);
   d. log `"Sending WIPE_SECURITY"` (`0x44e4e1`);
   e. **one** `Exec(0xE8, expected 6, mode 0, &status, len 4, …)`
      (`0x44e4ed`-`0x44e507`);
   f. success → log `"Received successful ACK"` (`0x44e51a`), return 0.
3. print `"\nQNX OSREGION NVRAM record cleared.\n"` (`0x404939`).

- **No read-first.** There is no `0xE9` (read `0x2074`) anywhere in this path;
  the block is 67 bytes long and contains exactly one protocol call `[M]`.
- **No verify.** No second command, no compare, no read-back `[M]`.
- **No confirmation prompt** (no `-y`/`-F` type argument; the level-2 parser for
  this command is the empty `0x402501` stub) `[M]`.
- **Retry:** only `Exec`'s own busy loop — if the response status byte is
  `0x19`, the same frame is re-sent (`0x4485c3` → `0x4485f3` → `0x44863f`).
  `WipeSecurity` itself adds no retry/backoff (`0x44e4b0` has no loop) `[M]`.
- **Important operational caveat** `[M]`+`[I]`: CFP prints the "cleared" message
  whenever the transport returned status `== 6` (`0x44813b` compares received
  status to expected). Report 164 records that the device's `0xE8` handler
  reaches `0x80103EFC` and sends `0x2E` on **both** its success and failure
  paths, and that it has no range check. So (a) CFP's success message is not
  proof the delete happened, and (b) a reimplementation should accept `0x2E`
  as well and confirm by reading `0x2074` back with `0xE9`.

### Device mode required

Measured `[M]`:
- the device object must be in the "initialized" state: `0x422270` bails unless
  `[this+4] == 6` (`cmp DWORD PTR [esi+0x4],0x6` at `0x422278`). That field is
  set to `6` only by the connect path, at `0x424126` (and `0x4243c7`), right
  after `0x44e7c0` finishes initialising the protocol sub-object at
  `this+0xd94` and after a 500 ms settle `Sleep(0x1f4)`.
- the command therefore assumes a **running loader (ramloader) on the device**,
  i.e. exactly the mode in which `INFO`/`FLASHINFO`/`READ` work — the same
  `0x448530`/`0x459700` frame path. It does **not** use bootrom opcodes, and no
  bootrom-specific setup appears in the block `[M]`.
- it needs no argument and no extra flag, and it does *not* require the
  `0xC0`-style precondition `frame[3]&0x40`; mode is sent as `0` `[M]`.

## Calibration (method check, as required)

Three independent calibrations of the "message string → exec block" method,
plus the opcode cross-check `[M]`:

| command | `cmdId` | switch index | exec block | proof |
|---|---|---|---|---|
| `READVERIFY` | 8 | 7 | `0x404950` | pushes `0x48cf10` = `"\nSuccess: flash reagion was verified successfully\n"` at `0x40498f` |
| `USBMS` | 20 | 19 | `0x404872` | pushes `0x48cf6c` = `"\nUSBMS: is %s."` at `0x4048c4` |
| `SETNFC_LOCK_FLAG` | 6 | 5 | `0x404457` | only other caller of `0x422270`, with selector `8` (`0x404475`) |
| `HASHBOOTROM` | 15 | 14 | `0x404551` | pushes `0x48cf90` = `"SHA-1 hash of Bootrom (%s):  %s"` at `0x40485b` |
| **`RESETQNXOSREGION`** | **24** | **23** | **`0x404908`** | pushes `0x48cf44` = the QNX message at `0x404939` |

Known ramloader opcodes found with the same method (each `push hi/lo` pair is
`arg2/arg1`, i.e. expected-status / opcode) `[M]`:

| opcode | site | companion (expected status) |
|---|---|---|
| `0xB4` (flash regions) | `push 0xb4` at `0x44b36d` (retry at `0x44b3b5`) | `0xD2` (`0x44b368`) |
| `0xD9` (MCT) | `0x44ae2d` | `0xC9` (`0x44ae28`) |
| `0xB0` | `0x44e895` | `0xD0` (`0x44e8a0`) |
| `0xDE` (security/RPMB) | `0x44d4f8` | `0x00` (`0x44d4ff`) |
| `0xE7` (PIN) | `0x44cf3d`, `0x44cf7d` | `0xD1` (`0x44cf44`) |
| `0xEE` (erase sector) | `0x448d0e` | `0xDE` (`0x448d09`) |
| `0xC4` (READVERIFY) | `0x44e618` | `0x28` (`0x44e616`) |
| `0xC0` (COMPLETE) | `0x44be58` | `0x06` + `mode 0x40` (`0x44be54`) |
| `0xF7`/`0xF4` (block write/read helper) | `0x44de18`/`0x44de60` | — |
| **`0xE8` (this command)** | **`0x44e500`** | **`0x06` + payload `4`** |

All companion values match the device-side response codes in report 66
(`0xB4→0xD2`, `0xD9→0xC9`, `0xC4→0x28`, `0xE8→6/0x2E`, …), which validates the
argument-order reading of `Exec`.

## Evidence

| what | VMA | FO | type |
|---|---|---|---|
| `"RESETQNXOSREGION"` | `0x48c48c` | `0x8b48c` | M |
| name-table record 24 (`flags`/`name`/`ordinal`) | `0x48c5e4` / `0x48c5e8` / `0x48c5ec` | — | M |
| name-table layout, 12-byte stride, flags/name/ordinal | `0x48c4c4`…`0x48c683` | `0x8b4c4`… | M |
| scan bound (`ordinal 37`) | `0x48c67c` | — | M |
| name compare (`strcmp`) call | `0x401ff6` | — | M |
| `cmdId` = record index (PATRIOT vs `0x19`) | `0x402080`/`0x402085` | — | M |
| `PATRIOT -o` second scan loop | `0x402166`-`0x4021a2` | — | M |
| level-2 jump table (37 entries) | `0x40250c`, `jmp` at `0x40222e` | — | M |
| level-2 entry for this command (no-op) | `0x402501` | — | M |
| level-3 switch | `0x403998`-`0x4039a9`, table `0x404cdc` | — | M |
| level-3 slot for `cmdId` 24 | `0x404d38` → `0x404908` | — | M |
| **handler entry** | **`0x404908`** | — | M |
| connect call `[vtbl+0x38]` | `0x40490e`,`0x40491a` | — | M |
| `push 0x4` (selector argument) | `0x404926` | — | M |
| `call 0x422270` (work function) | `0x40492a` | — | M |
| message push | `0x404939` | — | M |
| `"\nQNX OSREGION NVRAM record cleared.\n"` | `0x48cf44` | `0x8bf44` | M |
| facade, `[this+4]==6` gate + tag `WSEC` | `0x422270`-`0x4222c2` | — | M |
| protocol body (log/power-of-2/Exec) | `0x44e4b0`-`0x44e533` | — | M |
| payload store (`this+0x24 = 4`) | `0x44e4ea` | — | M |
| `Exec(0xE8, 6, 0, &st, 4, …)` | `0x44e4ed`-`0x44e507` | — | M |
| `Exec` (frame+send+recv+status) | `0x448530`-`0x448675` (`ret 0x18`) | — | M |
| frame builder | `0x459700`-`0x459758` | — | M |
| frame receiver | `0x459760`-`0x459811` | — | M |
| CRC-32 routine / table | `0x443e40` / `0x49be98` | — | M |
| status equality test | `0x44813b` | — | M |
| `SETNFC_LOCK_FLAG` selector `8` | `0x404475` / `0x404479` | — | M |
| absent record ids / extents (dword scan) | whole file | — | M |
| record `0x2074` = opcode `0xE8` arg 4 → delete | (device) `0x80103F18`/`0x80103F1C` | — | I (report 164) |

Reproduction: `objdump -d -M intel CFP.exe > cfp.dis` (194,787 lines) and grep
for the VMA; the whole analysis above is reproducible from that single file plus
a `u32` scan of `CFP.exe`.

## Confidence

| item | answer | confidence |
|---|---|---|
| 1. dispatch: name-table scan → `cmdId` 24 → switch slot → handler `0x404908` | measured | **high** (3 independent message calibrations, all consistent) |
| 2. record id `0x2074` | inferred from device-side RE (report 164); CFP itself carries no id (measured negative) | **high** for the record, **medium-high** for relying on report 164 |
| 2b. "table lookup vs immediates" | neither — a single protocol argument (`u32 4`) | **high** |
| 3. value written | deleted, not written; wire value is `u32 4` | **high**; the "24-byte record" extent is **medium** (inferred) |
| 4. transport = ramloader/loader channel-2 frame, opcode `0xE8`, mode `0`, payload `4` | measured | **high** |
| 4b. `0xF7` block write / bootrom `0xF0xx` ruled out | measured | **high** |
| 4c. the USB layer under the virtual `Write` | not traced | **n/a / open** |
| 5. no read, no verify, no confirmation; single command; needs initialised loader mode | measured | **high** |
| 5b. whether a returned `0x2E` is treated as success | CFP demands status `6`; device reportedly sends `0x2E` on both paths | **medium** — verify empirically with `0xE9` |

## Open questions

1. **Which status byte does the loader actually return for `0xE8`?** CFP requires
   exactly `6` (`0x44813b`), report 164 says the handler reaches `0x80103EFC`
   and sends `0x2E` on both outcomes. Either CFP's `WIPE_SECURITY` silently
   fails while still printing "cleared", or the loader can emit `6`. Needs one
   live capture (or a device-side re-read of `0x80103ECC`) to settle.
2. **Is delete implemented as a write?** If the loader's `0x8010B8BC` tombstones
   the record rather than erasing it, a reimplementation may prefer to write a
   known-good 24-byte `{u32 2, u16 0, u8 0}` via `0xC0` instead of relying on
   `0xE8`. Not answerable from CFP.
3. **The `[this+0x801a]` "timeout nudge" word** (`arg6`) — it is added to a
   2000 ms base in other call sites (`0x7d0 + word` at `0x448cfa`), so it looks
   like a per-endpoint timeout offset. Its population was not traced.
4. **USB wrapper below `0x45974e`** (endpoint/channel selection for the frame)
   was deliberately out of scope; the frame produced here is already the
   documented channel-2 inner frame, so a reimplementation only needs to feed
   it to the channel-2 endpoint.
5. Whether the device needs the `0x207C` record present (as `0xC0` does) is a
   device-side question; report 164 records **no** such precondition for the
   `0xE8` delete path.
