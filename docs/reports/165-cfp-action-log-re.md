# 165 — CFP "Loader Action Log": call site, reply structure, and whether it decrypts

**Status:** complete (static RE only; no device touched, `CFP.exe` not executed).

**Headline:** CFP does **not** use `0xB0` for the Loader Action Log. `0xB0` is the
**bugdisp crash log** (`c:\log.bug`). The Loader Action Log is opcode **`0xB3`**, reply
**`0xB4`**, with a **132-byte request payload** `{u32 action; char text[128]}` where
`action == 0` means "read". The reply payload is `{u32 textLen; char text[textLen]}`
inside the normal 8-byte-overhead frame. **No decryption exists on either side**: the
action log is plain ASCII in NVRAM record `0x2033`; the *bugdisp* log is the thing that
is stored encrypted, and neither the loader nor CFP can read it (CFP writes it verbatim
to `c:\log.bug`).

Claim tags: **[M]** = measured off the disassembly / bytes, **[I]** = inferred,
**[C]** = corroborated by an independent second measurement in this session.

### Address convention

* `CFP.exe`: `.text` VMA `0x401000`, `.rdata` `0x487000` (file `0x86000`),
  `.data` `0x4AF000` (file `0xADE00`), ImageBase `0x400000`. All VAs map directly
  (`objdump -d --start-address=…`).
* `loader_06001A06-00.bin`: **there is no `0x10000`-byte header.** File offset `0` is the
  boot vector — `b #8`, then at `+0x0C` the image's own load address **`0x80100000`**
  (bytes `00 00 10 80`), then `mov r0,0xd3 / msr cpsr_c,r0`. So
  **file offset = VA − 0x80100000**, exactly what `r2 -m 0x80100000` assumes. Report 164's
  loader addresses are therefore correct as written.
  **Correction to the task brief:** the two loader facts in the brief are mutually
  inconsistent; the header rule is the wrong one, and consequently the plaintext marker
  `"This log is encrypted."` is at **VA `0x80138B8C` (file `0x38B8C`)**, not `0x80128B8C`
  (measured: `ps 24 @ 0x80138b8c` returns the string; `ps 24 @ 0x80128b8c` returns code —
  the marker's *file offset* `0x38B8C` in the brief is right, the VA is off by `0x10000`).
  Every loader address in this report is a plain VA, with the file offset in parentheses
  where it helps; `file = VA − 0x80100000`.

---

## 1. The call site

### 1.1 The Loader-Action-Log reader: `0x44DA10` — opcode `0xB3`, response `0xB4`

`0x44DA10` is a `__thiscall` (`ret 0x8`, 2 stack args = `(void *dst, size_t *pLen)`), and
the only caller in the whole image is `0x41DED1` inside CFP's `info` collector **[M]**
(`grep 'call 0x44da10'` over the full `.text` disassembly → one hit).

```
0x44DA11  cmp  dword [esp+0x8], 0          ; dst != NULL
0x44DA2F  push 0x49E768 / push 3 / call [logger vtbl+0x10]
                                           ; "Retrieving Loader Action Log", level 3   [M]
0x44DA38  mov  dword [esi+0x24], 0         ; ← request payload dword 0 = 0  ("read")
0x44DA3F  movzx eax, word [esi+0x801A]     ; arg6 = reply data buffer capacity
0x44DA47  push 0x84                        ; arg5 = request payload length = 132     [M]
0x44DA4C  lea  eax,[esp+0x17] / push eax   ; arg4 = 1-byte stack slot for reply code
0x44DA53  push edi (=0)                    ; arg3 = request frame type/flag byte = 0  [M]
0x44DA54  push 0xB4                        ; arg2 = expected response opcode
0x44DA59  push 0xB3                        ; arg1 = opcode                        [M]
0x44DA60  call 0x448530                    ; "send one frame and receive one frame"
```

**So the answer to Q1 is `0xB3` with a 132-byte payload, not `0xB0`.**

The 6-argument signature of `0x448530` was recovered from the callee, not guessed **[M]**:

| arg | meaning | evidence |
|---|---|---|
| 1 | request opcode | `0x44854A mov ebx,[esp+0x14]` → pushed as the callee's arg1 at `0x448554` |
| 2 | expected reply opcode | `0x448661 mov ecx,[esp+0x18]`, compared at `0x44866C call 0x448130` |
| 3 | request frame byte `[3]` (mode/flag) | `0x448546` → `0x459722 mov [esi+3],dl` |
| 4 | reply header buffer (1 byte written: reply opcode) | `0x4597F8 mov [ecx],al` (see §2) |
| 5 | **request payload length** | `0x448542` → `0x459700` arg4 → `0x45970E add eax,8; mov [esi],ax` |
| 6 | reply payload buffer capacity | `0x44859D` → `0x459760` arg3 → vtable `+0x18` recv |

### 1.2 What the request frame actually looks like

`0x448530` → `0x4596D0` (lock/begin) → `0x459700` builds the frame **[M]**:

```
0x459701  eax = arg4 (payloadLen)
0x45970E  eax += 8                    ; total frame length
0x459711  mov  word [frame], ax       ; frame[0..1] = total length (LE)
0x459721  push eax-4                  ; checksum covers 4 + payloadLen bytes
0x459722  mov  [frame+2], cl (opcode)
0x459732  mov  [frame+3], dl (type byte from arg3)
0x459735  call 0x443E40               ; checksum(frame, payloadLen+4)
0x459743  mov  [frame+payloadLen+4], ~checksum
0x459751  push len / push frame / call [device vtbl+0x14]   ; write to the device
```

⇒ **wire request = `[u16 totalLen][u8 opcode][u8 type][payload:N][u32 ~cksum]`,
`totalLen = N + 8`.**

For the LAL read that is **[M]** (all fields measured, layout self-consistent with 0x459700's arithmetic):

```
8C 00 | B3 | 00 | 00 00 00 00 | 00 ×128 | <u32 ~cksum>          ; 140 bytes
 ^len   ^op  ^type  ^action=0  ^text[128]
```

The payload area is the session object's frame buffer at `this+0x24` (i.e. `frame+4`), and
`this+0x28` (= `frame+8`) is the 128-byte text field: the writer `0x44D860` does
`0x44D893 memset(this+0x28, 0, 0x80)` and `0x44D8AC snprintf(this+0x28, 0x7F, …)`, so
payload = 4 + 128 = **132 = 0x84** exactly matches `push 0x84`.

### 1.3 The writer half of the same opcode: `0x44D860` (action = 1)

`0x44D860` is the LAL **append** call (only caller `0x421894`) **[M]**:

* `0x44D880 0x4654BC(&local)` then `0x44D89C-0x44D8AD 0x45FACF(this+0x28, 0x7F, "%lX:%d.%d.%d.%d", …)`
  — format string at `.rdata 0x49E758` **[M]**;
* then one of `.rdata 0x49E73C ":%s--%d"`, `0x49E744 ":%s:%s"`, `0x49E74C ":%s:%s--%d"`,
  `0x49E738 ":%s"` chosen by `[esi+0x4] != 0` and `arg2 != 0xAA` (`0x44D8D2`-`0x44D93A`) **[M]**;
* `0x44D93A-0x44D94D` logs **`"Writing to Loader Action Log \"%s\""`** (`.rdata 0x49E714`) **[M]**;
* `0x44D950  mov dword [esi+0x24], 1`  ← **action = 1 = append** **[M]**;
* `0x44D95F-0x44D978` same six pushes as the reader (`0xB3`,`0xB4`, type 0, `0x84`, `[esi+0x801A]`) **[M]**.

So `0xB3` is a **read/write** command on the same record, selected by the first payload
dword. All four `0xB3` call sites in CFP are `0x44D978` (append), `0x44D9BC` (append retry),
`0x44DA60` (read), `0x44DAAD` (read retry) — **there is no other `0xB3` caller** **[M]**.

### 1.4 Is that call site the only feeder of the LAL printer?

Yes **[M]**. The chain is:

```
0x41DED1 call 0x44DA10            (dst = [ebp+0x8DBC], capacity [ebp+0x8DC0] = 0xFA0)
   └─ 0x420FF0 : the printer, reading [this+0x8DBC] / [this+0x8DC0]
```
* `0x41DEBB  mov ecx,[ebp+0x8DBC]` / `0x41DECB mov dword [ebp+0x8DC0], 0xFA0` =
  destination + capacity **4000** (`MAX_CFP_LAL_LENGTH`, cf. `.rdata 0x494A03
  "CFP_Imp failed to allocate MAX_CFP_LAL_LENGTH bytes for the action log."`) **[M]**;
* `0x41DEF7 cmp dword [esi],0xFA0 / jbe` with `.rdata 0x4932A4 "Action log overflow"` —
  CFP itself expects ≤ 4000 bytes and warns otherwise **[M]**;
* the printer `0x420FF0` (see §3) reads exactly those two members **[M]**.

And the `0xB0` call site (`0x44E8A0`) belongs to a *different* function: it logs
`"Requesting BootImage Handshake"` (`.rdata 0x49EF7C`), then on reply code `0xB5`
logs **`"Bugdisp log received"`** (`.rdata 0x49EF64`) and writes
**`"c:\\log.bug"`** (`.rdata 0x49EF58`, used at `0x44E929`) **[M]**.
⇒ **our tool has been reading the crash-dump log.** The contradictory-dump question is answered.

---

## 2. The reply structure

### 2.1 Outer frame (already handled by our tool — it got a `0xB0` reply)

Built by CFP at `0x459700` (request) and validated by CFP at `0x459760` (reply) **[M]**;
produced by the loader at `0x80125424` `(code, mode, len, data)`, which does
`strh r5(len+8),[frame]`, `strb code,[frame+2]`, `strb mode,[frame+3]`, then a 4-byte
checksum loop (`0x8012546C`-`0x80125488`) **[M]**:

```
+0x00  u16  totalLen = payloadLen + 8          (little-endian)
+0x02  u8   reply opcode                       (0xB4 for the LAL)
+0x03  u8   mode  (echo of request frame[3])
+0x04  u8   payload[payloadLen]
+4+plen u32 ~checksum over the first (payloadLen+4) bytes
```

CFP's reply validator `0x459760` **[M]**:
* `0x459770` re-reads until ≥ 2 bytes; `0x45979B cmp ecx, word[frame]` — received length
  must equal the frame length word, else `*code = 0x2F`, `0x4597E8` checksum compare,
  else `*code = 0x0F`; `0x4597EE ldrb al,[frame+2]; strb [out],al` hands the reply opcode
  back to `0x448530`, which is replaced by `payloadLen = totalLen − 8`
  (`0x4597FA add ebx,-8; strh [frame],bx`) **[M]**.

### 2.2 LAL payload — the deliverable

```
+0x00  u32  textLen                          (little-endian)
+0x04  u8   text[textLen]                    plain ASCII, '\n'-separated, NOT NUL-terminated
                                             by the loader
total payload length = textLen + 4
```

Every step measured:

| step | address | measurement |
|---|---|---|
| CFP reads the length **from inside the payload** | `0x44DACB mov ecx,[esi+0x4020]` | the dword at `payload+0` **[M]** |
| …and the text starts 4 bytes later | `0x44DAE5 lea ecx,[esi+0x4024]` | **[M]** |
| copies `min(cap, textLen)` | `0x44DAD8 mov eax,[edi]; 0x44DADA cmp eax,ecx; 0x44DADE` | **[M]** |
| returns `textLen` | `0x44DAF2 mov eax,[esi+0x4020]; 0x44DAFB mov [edi],eax` | **[M]** |
| forces a NUL at `text+0xF9F` | `0x44DAD1 mov byte [esi+0x4FC3],0`; `0x4FC3 − 0x4024 = 0xF9F` | ⇒ text buffer = **0xFA0 = 4000** **[M]** |
| loader sends `len = [out]+4`, `data = out` | `0x80103CDC ldr r0,[r5]` / `0x80103CE8 add r0,r0,4` / `0x80103CEC uxth r2,r0` / `0x80103CF0 mov r0,0xb4` | **[M]** |

Limits **[M]**:
* read side: NVRAM record `0x2033` is read with a **`0x3FE8` = 16 360-byte** cap
  (`0x80118734 movw r2,0x3fe8; … bl 0x8010b954`);
* append side: the total is clamped to **`0xFA0` = 4000** before write-back
  (`0x80118704 cmp r0,0xfa0; movhi r0,0xfa0; strhi [r4]`). So CFP's 4000-byte
  buffer is exactly the loader's own append cap.

Corroboration of the "length word + text" idiom: the *bugdisp* payload our tool captured
also begins with one packed little-endian word, `1b 25 00 00`, in which the low byte
(`0x1B` = 27 = `strlen(marker)+5`) is a length and the high byte (`0x25`) is a record tag,
with the text immediately after it at `+4` (see §5.2 for the loader code that builds that
word, and §7.3 for the dump). Same idiom as the LAL's `u32 textLen` + text, different
record type.

---

## 3. The printer

`0x420FF0`, `__thiscall` on `CFP_Imp`, no args, `ret` (plain) **[M]**. It is *not* a
parser: it is a line splitter plus one optional re-formatting rule.

```
0x421007  cmp dword [esi+0x8DC0], 0 ; jbe 0x4211E4      ; nothing received → return
0x421027  log(level 4, "Dumping the Loader Action Log:")   ; .rdata 0x493E98        [M]
0x421030  end = [esi+0x8DBC] + [esi+0x8DC0]
loop @0x421046:
  0x421051  v = strtoul(cursor, &endp, 16)               ; callee 0x4607B8 → _strtol_l 0x46055E
  0x42105B  if (v <= 0x10) goto verbatim
  0x421070  if (endp[1] == ':') goto verbatim
  format path:
    0x42107A  memset(buf400, 0, 0x400)                   ; 0x45F2E0
    0x4210A4  snprintf(&buf400[8], 0x400, "%s", 0x464C39(buf400))
    0x4210A9  p = 0x464920(cursor, 0x493E94 "--")        ; split on the 0x493E94 literal
    0x4210E9  code = strtol(p+2, &endp, 10)              ; base 10
    0x421188  if (code <= 0xA9) snprintf(…, 0x493E84 "ErrorCode:%d-%s",
                                        code, [0x4AF728 + code*4])   ; message table in .data
    0x4211A1  log(level 4, buf400)
verbatim @0x4211AB:
  0x4211B6  log(level 4, cursor)                         ; the line pointer, verbatim
  0x4211C9  cursor += strlen(cursor) + 1                 ; next line
```

Key conclusions:

* **`S:` / `D:` / `QNXLOAD` lines are never synthesised by CFP** **[M]**. `"S:00010011"`
  gives `strtoul → 0`, so it takes the verbatim branch at `0x4211AB`; a line that starts
  with a date string likewise. CFP prints the bytes it received.
* The `--> ` prefix in the operator's dump comes from CFP's logger format
  `.rdata 0x48DEB8 "%s--> %s\n"` (and `0x48DEC4 "--> %s %s\n"`), not from the LAL text **[M]**.
* `"ErrorCode:%d-%s"` (`.rdata 0x493E84`) **is** a CFP-side formatter, and the
  `-Success!` in the operator's line is `[0x4AF728 + 0*4]` (code 0) **[M]** for the
  address arithmetic, **[I]** for the table content (the table lives in `.data`, which is
  the encrypted container, so it cannot be read statically — see §5.4).
* The first field is machine-readable in storage and prettified for display: the storage
  format written by CFP itself is `"%lX:%d.%d.%d.%d"` (`.rdata 0x49E758`), i.e. a hex
  `u32` followed by a 4-part version, and the printer replaces that hex field with a
  rendered date string. **[I]** but tightly bracketed: `asctime(0x6A93C292)` is exactly
  `"Sun Aug 30 05:41:38 2026"`, `0x6A93C292` is a plausible `time_t` for that line, and
  the MSVC `asctime` day/month tables are present in the image
  (file `0xA39AC` `"SunMonTue…"`, file `0xA39C4` `"JanFeb…"`). The exact helper is
  `0x464C39` (not characterised further; it is an MSVC STL/CRT wrapper).
* **No decryption, no decompression, no transform in the printer** **[M]**: the only
  callees are `strtoul`/`strtol` (`0x4607B8`→`0x46055E`), `memset` (`0x45F2E0`),
  bounded `snprintf` (`0x45FACF`), one string splitter (`0x464920`), one CRT wrapper
  (`0x464C39`) and the logger vtable. There is no `0x461750`-style copy and no crypto.

---

## 4. Candidate opcodes

All 100 call sites of the frame engine `0x448530` were extracted mechanically
(`grep -B9 'call 0x448530'`), showing `(opcode, expected reply, frame[3], payloadLen)` **[M]**.
Complete list of opcodes CFP can send: `0x20, 0x21, 0x28, 0x29, 0x2E, 0x39, 0xAE, 0xAF,
0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xBD, 0xBE, 0xBF, 0xC0, 0xC3, 0xC4, 0xC8,
0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB,
0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFB,
0xFC, 0xFF` **[M]**.

The `0xA8`–`0xC8` window specifically:

| op | expected | frame[3] | payloadLen | call sites | what it is |
|---|---|---|---|---|---|
| `0xB0` | `0xD0` (+ `0xB5` chunks) | `0` | **8** | `0x44E8A0`, `0x44EA19` | **bugdisp crash log** → `c:\log.bug` (`0x44E929`, `0x49EF58`), `.rdata 0x49EF64 "Bugdisp log received"` **[M]** |
| `0xB1` | `0xC8` | 0 | 0 | `0x44AF34`, `0x44AF7D` | metrics (`DSP/BS`) |
| `0xB2` | `0xC8` | 0 | 0 | `0x44AA24`, `0x44AA7A` | DSP-OS metrics |
| **`0xB3`** | **`0xB4`** | `0` | **`0x84`** | **`0x44D978`, `0x44D9BC`, `0x44DA60`, `0x44DAAD`** | **Loader Action Log read/append** **[M]** |
| `0xB4` | `0xD2` | 0 | 0 | `0x44B374`, `0x44B3BD` | flash identity / geometry |
| `0xB5` | `0xD3` | 0 | 0 | `0x44B0A4`, `0x44B0E9` | CSD/ExtCSD |
| `0xB6` | `0xF2` | 0 | 4 | `0x44D115`, `0x44D159` | — |
| `0xB7` | `0xF3` | 0 | 4 | `0x44D009`, `0x44D056` | image hash |
| `0xBD` | `0xBE` | 0 | 4 | `0x44B5B4`, `0x44B5F5` | HW override id |
| `0xBF` | `0xD9` | 0 | 8 | `0x44B696`, `0x44B6D7` | DRAM info |
| `0xC0` | `0x06` | **`0x40`** | `0` | `0x44BE5F`, `0x44F0CE` | COMPLETE |
| `0xC3` | `0x06` | `0x40` | 4 | `0x44CE6E`, `0x44CEAF` | LED/PWM |
| `0xC4` | `0x28` | 0 | `0xC` | `0x44E61F` | range read/verify |

**Q3 answer: there is exactly one log opcode pair — `0xB3`→`0xB4` for the *action* log and
`0xB0`→`0xB5`+`0xD0` for the *bugdisp* log.** Nothing else in the window returns a
variable-length text blob (`0xC8` replies are 0x3FF0 metric buffers). Our tool is reading
the crash log **[M]**.

---

## 5. The loader side

### 5.1 `0xB0` never looks at the payload, and never decrypts

Inline case at `0x80117058`-`0x801170B2` (file `0x17058`) **[M]**:

```
0x80117058  cmp r0,1 ; ldreq r0,[r6] ; ldrbeq r0,[r0,2]   ; only frame[2] (the opcode)
0x80117064  cmpeq r0,0xb0 ; bne loop
0x8011706C  bl 0x80128D5C            ; gate/builder, no arguments
0x8011708C  r0 = [desc+8] (data ptr) ; 0x80117098 blne 0x80116DA8(ptr, [desc+0] = len)
0x8011709C  send 0xD0, len 4, data @0x8013EE78, mode = frame[3]
0x801170B4  build "D:00010013" ('D', ':', then 8 hex nibbles of 0x10013 from 0x801171E4)
0x8011710C  bl 0x80124E18            ; ← this is `bx lr` in this image (an empty stub)
```

* The request payload is read **nowhere** in the `0xB0` path. The chunker
  `0x80116DA8(r0=ptr, r1=len)` (file `0x16DA8`) only re-reads `frame[3]` for the reply
  mode byte (`0x80116DD0 ldrb r1,[r0,3]`, `0x80116DD4 mov r0,0xb5`,
  `0x80116DD8 uxth r2,r5` with `r5 = min(len,0x200)` at `0x80116DC0`) **[M]**.
* ⇒ **CFP's 8-byte `0xB0` payload, and our tool's zero-length `0xB0` payload, are
  equivalent.** (Note: `0x44E865` does stage `this+0x28 = 0x0ADD869E` when CFP's caller
  argument is 5 — an apparent "log kind" selector at payload+4 — but the loader never
  consults it **[M]** for the write, **[I]** for the intent.)

### 5.2 The `0xB0` blob is *served* encrypted, not decrypted by the loader

The gate `0x80128D5C` (file `0x28D5C`) **[M]**:

```
0x80128D60  r4 = [0x80128F58] = 0x802886C4          ; descriptor {u32 len; u32 flag; u8 *ptr; u32 magic}
0x80128D64  r0 = [r4+0xC] ; beq return 0            ; magic slot empty → not ready
0x80128D7C  bl 0x801184AC                            ; ← the "signature gate"
0x80128D84  if (gate == 0) { [r4] = 0x8000; [r4+4] = 0; return 1 }   ; serve the whole 32 KiB
0x80128DB0  bl 0x8011F324(0x802775AC, 0x9000)        ; ← the "record builder"
0x80128DD4  bl 0x8011F450(0x802805B8, 0x8000, …)     ; ← serialiser, accumulates [r4] (len)
0x80128DEC  [r4+0xC] = 0xdeadbeef ; return 1
```

* `0x801184AC` is **not a verifier** despite its name in report 164: it is
  `bl 0x801182E0; ldr r0,[r0+0x7C]; rsbs r0,r0,1; movlo r0,0` = "return `[obj+0x7C] == 1`",
  one flag test, no crypto **[M]**.
* `0x8011F324` (file `0x1F324`) is a **container writer**, not a crypt routine **[M]**:
  `0x8011F334 strlen`; `0x8011F340 cmp len+0xE1, outcap`; zeroes `[r4+4]`,`[r4+0x1C]`;
  stores `0x1001` at `[r4+8]`, `0x8001` at `[r4+0xC]`; then
  `0x8011F370 r0 = (strlen+5) | 0x2500; str r0,[r5]` and
  `0x8011F38C memcpy(out+4, str, strlen+1)`.
  **`(strlen("This log is encrypted.") = 22) + 5 | 0x2500 = 0x251B`**, which is
  byte-for-byte the first four bytes of our capture `1b 25 00 00` **[M][C]**. That pins
  the marker string (`0x80138B8C`, file `0x38B8C`) as *written by
  the loader in plaintext* immediately in front of the stored ciphertext.
* `0x8011F450` (file `0x1F450`) is a field serialiser: 4-byte `0x320C` tag, `u16` from
  `[r4]`, 4-byte copies from `0x802590FC`, `memcpy` at `0x8011F4B8` — no crypto **[M]**.
* ⇒ **Nothing in the `0xB0` path decrypts anything.** The plaintext "This log is
  encrypted." marker plus the encrypted records are the *stored* form, re-served as-is
  (§7 cross-check: CFP dumps the same bytes to `c:\log.bug` untouched).

### 5.3 `0xB3` on the loader: plain NVRAM record `0x2033`, no crypto

Case body `0x80103CCC` (file `0x3CCC`), reached from the *actions*
dispatcher at `0x80103B90` **[M]**:

```
0x80103CCC  ldr r0,[r4,4]              ; r4 = request frame → r0 = frame[4] = payload dword 0 = ACTION
0x80103CD0  r2 = r5                    ; out struct
0x80103CD4  add r1, r4, 8              ; r1 = frame+8 = payload dword 1 = TEXT[128]
0x80103CD8  bl 0x80118688              ; (action, text, out)
0x80103CDC  ldr r0,[r5]                ; out->len = text length
0x80103CE4  ldrb r1,[r4,3]             ; reply mode = request frame[3]
0x80103CE8  add r0, r0, 4              ; payload length = textLen + 4
0x80103CF0  mov r0, 0xB4               ; reply opcode
0x80103CF4  b 0x80103F44               ; → 0x80125424(code, mode, len, data = the out struct)
```

`0x80118688(action, text, out)` (file `0x18688`) **[M]**:

```
if action == 0:                                   ; == READ
    n = nvram_read(0x2033, out+4, 0x3FE8)          ; 0x80118740 bl 0x8010B954
    out[0] = n                                     ; 0x80118744
else:                                             ; == APPEND
    n = strlen(text); n+1, clamp to 0x3FE8         ; 0x8011869C-0x801186B4
    text[clamp-1] = 0
    len = nvram_read(0x2033, out+4, 0x3FE8)        ; 0x801186CC
    memmove(out+4+n, out+4, 0x3FE8-n)              ; 0x801186E4 bl 0x801312A4 (overlap-aware)
    memcpy(out+4, text, n)                         ; 0x801186F4 bl 0x801311D0
    out[0] = min(len+n, 0xFA0)                     ; 0x801186FC-0x80118714
    nvram_write(0x2033, out+4, out[0])             ; 0x8011871C bl 0x8010B990
```

* The append **prepends** (memmove up, then copy at the front) ⇒ the log is newest-first.
  That matches the operator's dump ordering (`05:41:38` before `05:38:10`) **[M]/[I]**.
* Only three instructions in the whole image reference record id `0x2033`
  (byte search for `movw rX,#0x2033` ⇒ `0x801186C8`, `0x80118718`, `0x8011873C`,
  all inside `0x80118688`) **[M]** ⇒ **the 0xB3 handler is the only reader *and* writer of
  the action log in this loader**.
* NVRAM record layer: `0x8010B954` (read) → `0x8010B644` → `0x8010B484` (straight
  copy path, no crypto call) **[M]**; `0x8010B990` (write) → `0x8010B818` →
  write-protect bit test (`0x8010B864 tst r0,4`) → `0x80130518` (store) **[M]**.
* ⇒ **The action log is stored as plain ASCII. There is no transform to undo.**

### 5.4 What the "signature gate"/"record builder" in report 164 actually are

| loader VA (file offset) | what it is | crypto? |
|---|---|---|
| `0x801184AC` (file `0x184AC`) | flag test `[obj+0x7C] == 1` | no **[M]** |
| `0x8011F324` (file `0x1F324`) | container writer: `u32 (strlen+5)|0x2500`, `0x1001`/`0x8001` tags, string copy | no **[M]** |
| `0x8011F450` (file `0x1F450`) | binary field serialiser (`0x320C`, `u16`, memcpys) | no **[M]** |
| `0x80128D5C` (file `0x28D5C`) | computes the `0xB0` descriptor length via the two above | no **[M]** |

The "encrypted" `.data` container of `CFP.exe` (13.45 MB, 7.97 bits/byte) is a separate
matter and was not touched; the LAL message table `[0x4AF728]` lives there and is
therefore unreadable statically — which is why §3's `-Success!` table entry is marked
**[I]**. Nothing in the LAL path *needs* it.

### 5.5 Residual oddity: the `S:` / `D:` lines

The operator's dump contains two bare lines `S:00010011` and `D:00010013`. The loader
*does* build the `D:` string (`0x801170B4`, `'D'`, `':'`, 8 hex nibbles of `0x10013`),
but hands it to `0x80124E18`, which is literally `bx lr` in this image, and the loader
touches record `0x2033` nowhere else (§5.3) **[M]**. So on **this** loader the `S:`/`D:`
lines cannot be produced by the loader, and CFP's own writer would prefix them with
`"<hex>:<a.b.c.d>:"` (§1.3). They must have been appended by some other host tool that
speaks `0xB3` action=1, or by a different loader build. This does not affect what we must
send — the printer passes such lines through verbatim either way. See §9.

---

## 6. What our tool should change

1. **Send `0xB3`, not `0xB0`.** `0xB0` is the bugdisp crash log; its payload is
   encrypted at rest and neither CFP nor the loader can read it (§5.2) — stop trying.
2. **Request frame** (140 bytes total):
   `u16 0x008C | u8 0xB3 | u8 0x00 | u32 action=0 | 128 zero bytes | u32 ~checksum`.
   The checksum routine is the same one already used for `0xB0` (CFP `0x443E40`,
   loader `0x8012546C`) — no change needed if `0xB0` framing already works.
3. **Do not shorten the payload.** This is a safety issue, not cosmetics: the loader's
   `0xB3` case body performs **no length check** — it reads `frame[+4]` as the action and
   `frame+8` as a NUL-terminated string. A zero-length or short `0xB3` frame would make
   `action` = the checksum word (almost certainly ≠ 0) and let the loader `strlen()` past
   the frame and **append garbage to NVRAM `0x2033`** (the real action log). Always send
   `payloadLen = 0x84` with `action = 0` unless appending is intended.
4. **Parse the reply:** validate `frame[2] == 0xB4` and the checksum; then
   `textLen = LE u32 at payload+0`, `text = payload[4 : 4+textLen]`; payload length is
   `textLen + 4`. Do not assume a NUL terminator — the loader does not add one; CFP forces
   one at `text+0xF9F` (`0x44DAD1`).
5. **Sizing:** the log can be up to `0x3FE8` = 16 360 bytes on the read path; CFP
   truncates to 4000 and warns "Action log overflow". Mirror that (use 4000-byte capacity)
   so output matches CFP.
6. **Retries:** CFP retries the *same* frame while its validator `0x448130` returns
   `0x6C` (frame length mismatch / checksum failure → `0x0F` → `0x6C`) with a 300 ms
   sleep (`0x44DA85 push 0x12c`), up to `[this+0x801C]` attempts. Retry `0xB3`.
7. **Presentation note (optional):** for parity with CFP's dump, a line whose first field
   parses as hex > `0x10` (and is not immediately followed by `':'`) and which contains
   `--<code>` should be pretty-printed as `<date>:<rest>-ErrorCode:<code>-<table[code]>`
   (§3). Everything else — including the `S:`/`D:` lines — is printed verbatim with a
   `"--> "` prefix.

---

## 7. Evidence

### 7.1 Measured — CFP.exe (`objdump -d -M intel`, plus raw `.rdata` reads)

| address | fact |
|---|---|
| `0x44DA10` | `LALGet(dst,&len)`: `push [esi+0x801A] / 0x84 / &stk / 0 / 0xB4 / 0xB3` → `call 0x448530` at `0x44DA60` |
| `0x44D860` | `LALPut`: builds text at `this+0x28`, logs `0x49E714`, `[esi+0x24]=1` (`0x44D950`), `call 0x448530` at `0x44D978` |
| `0x448530` | 6-arg frame engine `(op, expected, frame[3], hdrBuf, payloadLen, replyCap)` |
| `0x459700` | request frame builder: `[0]=len+8`, `[2]=op`, `[3]=type`, `~cksum` at `len+4` |
| `0x459760` | reply validator: len==`[0]`, cksum at `len-4`, returns `[2]`, rewrites `[0]=len-8` |
| `0x448130` | reply-code validator; `0x448183 cmp al,0xf` → `0x6C` = retry |
| `0x44DACB` / `0x44DAE5` / `0x44DAD1` | reply payload = `u32 len` at `this+0x4020`, text at `this+0x4024`, NUL at `text+0xF9F` |
| `0x420FF0` | LAL printer (line loop `0x421046`, verbatim `0x4211AB`, format `0x42107A-0x4211A5`) |
| `0x493E98` / `0x493E84` / `0x49E714` / `0x49E768` / `0x49E700` | `"Dumping the Loader Action Log:"` / `"ErrorCode:%d-%s"` / `"Writing to Loader Action Log \"%s\""` / `"Retrieving Loader Action Log"` / `"Got LAL_RESPONSE"` |
| `0x49EF58` / `0x49EF64` / `0x49EF7C` | `"c:\\log.bug"` / `"Bugdisp log received"` / `"Requesting BootImage Handshake"` — the `0xB0` function |
| `0x41DED1` / `0x421894` | the only callers of `0x44DA10` / `0x44D860` |
| `0x443E40` | the frame checksum used in both directions |
| `0x461750` | `memcpy` — the only transform between the read and the printer (as the brief states) |
| `0x4AF728` | the `ErrorCode` message pointer table (in the encrypted `.data`) |

### 7.2 Measured — loader (`r2 -q -m 0x80100000 -a arm -b 32`, ARM/A32; file offset = VA − 0x80100000)

| loader VA (file offset) | fact |
|---|---|
| `0x80117058`-`0x801170B2` (0x17058) | `0xB0` inline case: reads only `frame[2]`/`frame[3]`; sends `0xD0`(4 B) after `0xB5` chunks |
| `0x80116DA8` (0x16DA8) | chunker `(ptr,len)`; `r5=min(len,0x200)`, reply code `0xB5`, mode from `frame[3]` |
| `0x80128D5C` (0x28D5C) | `0xB0` gate/descriptor builder; flag test + container writers |
| `0x801184AC` (0x184AC) | `[obj+0x7C]==1` — not a signature verifier |
| `0x8011F324` (0x1F324) | writes `u32 (strlen+5)|0x2500` then the plaintext marker ⇒ `0x251B` |
| `0x8011F450` (0x1F450) | record field serialiser (`0x320C`, `u16`, memcpys) |
| `0x80138B8C` (0x38B8C) | `"This log is encrypted."` — plaintext, written by the loader |
| `0x80103CCC` (0x3CCC) | `0xB3` case: `action = frame[4]`, `text = frame+8`, reply `0xB4`, len = `[out]+4` |
| `0x80118688` (0x18688) | `(action, text, out)`: `0` = read `0x2033` (cap `0x3FE8`), else prepend text, clamp `0xFA0`, write back |
| `0x8010B954` / `0x8010B990` / `0x8010B484` / `0x8010B818` / `0x80130518` | NVRAM read / write / copy / store — no crypto call in any of them |
| `0x80125424` (0x25424) | response sender: `strh len+8`, `strb code`, `strb mode`, 4-byte checksum loop |
| `0x801170B4` (0x170B4) | builds `"D:00010013"`; callee `0x80124E18` is `bx lr` |
| byte scan | `movw rX,#0x2033` appears only at `0x801186C8`, `0x80118718`, `0x8011873C` |

### 7.3 Cross-check on the captured `0xB0` blob

`docs/reports/data/live-info-2026-11-01/loader_action_log.bin` (34 215 B) **[M]**:

```
+0x0000  1b 25 00 00            = 0x251B = (strlen("This log is encrypted.")+5) | 0x2500
+0x0004  "This log is encrypted." 00
+0x0023  a4 00 00 01 0c 00 10 00 01 00 80 00 01 00 01 00 80 00 01 00 01 00 00 00   (24-byte header)
+0x003B  "Bugdisp-r036"         00?  then 25 73 73 6d then 7.9-bit/byte ciphertext
+0x00DB  the identical 24-byte header + "Bugdisp-r036" again, more ciphertext
```

⇒ it is a **bugdisp** container (report 164 Add. 3 was right about the payload), and it is
the *wrong* log. The tag arithmetic in §5.2 explains `0x251B`; the `0x2500` high byte is
the record type the loader's writer emits.

---

## 8. Confidence

| claim | confidence |
|---|---|
| LAL = `0xB3`, reply `0xB4` | **high** — four call sites, all with `0x84`, plus a complete loader case body and a worker whose only NVRAM id is `0x2033` |
| request payload = `{u32 action; char text[128]}`, 132 B, action 0 = read | **high** — 0x84 = 4+0x80, matched independently by the writer's `memset(this+0x28,0,0x80)` and the loader's `frame[+4]` / `frame+8` |
| reply payload = `{u32 textLen; text[textLen]}` | **high** — CFP reads the dword at `+0` and copies from `+4`; the loader computes `len+4` from that same dword and sends the struct itself as data |
| `0xB0` = bugdisp, payload ignored by the loader | **high** |
| no decryption in either direction for the LAL | **high** — every callee on both paths was disassembled; none is a cipher |
| no decryption for `0xB0` (blob served as stored) | **high** for the loader path; the blob is nevertheless encrypted at rest — *who* encrypts it (a signed boot-stage writer) was not investigated and is out of scope |
| printer's `ErrorCode:%d-%s` table contents | **low/medium** — table lives in the encrypted `.data` |
| printer renders the leading hex field via `asctime` | **medium** — format match (`0x6A93C292` → the observed string) + CRT tables present; helper `0x464C39` not fully characterised |
| `S:`/`D:` lines are host-written | **medium** — measured that this loader cannot write `0x2033` except via `0xB3`; which host tool wrote them is unknown |

---

## 9. Open questions

1. **Who wrote `S:00010011` / `D:00010013` into NVRAM `0x2033`?** This loader cannot
   (only `0x80118688` touches `0x2033`, and the `D:` builder's sink `0x80124E18` is a
   `bx lr` stub). Candidates: an older/different CFP build whose `LALPut` format lacked
   the `"%lX:%d.%d.%d.%d:"` prefix, another Cellebrite host tool, or a modified ramloader
   (cf. report 93). Worth one live `0xB3` read to see whether such entries are still
   present, and worth diffing against report 93's modified loader.
2. **What is `0x0ADD869E` at `0xB0` payload+4** (staged at `0x44E865` when CFP's caller
   argument is 5)? The loader ignores it, so it is either dead or a selector for a build
   where the `0xB0` case reads the payload. Cheap to test live: `0xB0` with 8-byte payload
   `00…|AE 86 DD 0A` should behave identically to a zero-length payload if it is dead.
3. **`0x80124E18` (`bx lr`)** — is the D:/S: logging path present in the *device's*
   loader image but stubbed in our copy? A byte-level diff of our
   `loader_06001A06-00.bin` against a freshly uploaded LDR_01 dump would settle it.
4. **`0x464C39`** — the exact CRT/STL helper the printer uses to render the leading hex
   field. Only matters for byte-identical output parity; not needed to *read* the log.
5. **`.data` message table at `0x4AF728`** — holds the `ErrorCode:N-<text>` strings
   (index 0 ⇒ "Success!"). Reading it would need the container's unpacking, which the
   brief explicitly excludes.
