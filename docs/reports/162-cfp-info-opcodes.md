# 162 — CFP `info` unknown-step opcodes (static RE)

**Status:** complete — all 8 opcode rows **and** the bootrom-metrics blob field offsets (§6).
**Target:** `/home/psyden/SSDdrive/playbook-linux/local/vendor/CFP.exe`
PE32 i386, MSVC vc9, 14,159,872 bytes, image base `0x00400000`.
`.text` VMA `0x00401000` / vsize `0x85B96` / foff `0x400`; `.rdata` VMA `0x00487000` / foff `0x86000`.
Method: offline static analysis only — `pefile` + `objdump`/rizin. Binary unmodified, no device touched.

---

## 1. Findings

Wire format reminder (measured, see §3): the command is a **u16 little-endian at packet offset +2**;
the response carries its own u16 command at payload-packet offset +2.

| # | Retrieval (trace line) | Op (cmd, LE16) | Resp op | Request payload | Response layout | Cite | Confidence |
|---|---|---|---|---|---|---|---|
| 1 | Retrieving Hardware Override ID | **0x00BD** | **0x00BE** | none (len 0) | opaque blob, caller-sized | `0x44B5AD` push 0xBD · `0x44B5A8` push 0xBE · call `0x448530` @ `0x44B5B4` | **measured** |
| 2 | Retrieving DRAM Info | **0x00BF** | **0x00D9** | none (len 0) | opaque blob, caller-sized | `0x44B26D` push 0xBF · `0x44B268` push 0xD9 · call @ `0x44B274` | **measured** |
| 3 | Retrieving Installer Metrics | **0x00E1** | **0x00C8** | none (len 0) | opaque blob (metrics struct), caller-sized | `0x44A91D` push 0xE1 · `0x44A918` push 0xC8 · call @ `0x44A924` | **measured** |
| 4 | Retrieving DSP OS Metrics | **0x00B2** | **0x00C8** | none (len 0) | opaque blob (metrics struct), caller-sized | `0x44AA1D` push 0xB2 · `0x44AA18` push 0xC8 · call @ `0x44AA24` | **measured** |
| 5 | Retrieving OS Metrics | **0x00D8** | **0x00C8** | none (len 0) | opaque blob (metrics struct), caller-sized | `0x44A81D` push 0xD8 · `0x44A818` push 0xC8 · call @ `0x44A824` | **measured** |
| 6 | Retrieving boot count info | **0x00E9** | **0x00FB** | **4 bytes, all zero** (`00 00 00 00`) | ≥4 bytes; a dword is consumed at `[this+0x24]` | `0x44D681` push 0xE9 · `0x44D67C` push 0xFB · `0x44D674` push 4 · `0x44D669` zeroes the dword · call @ `0x44D688` | **measured** |
| 7 | Retrieving Vendor ID | **0x00DB** | **0x00CB** | none (len 0) | opaque blob, caller-sized | `0x44D7AD` push 0xDB · `0x44D7A8` push 0xCB · call @ `0x44D7B4` | **measured** |
| 8 | Retrieving Flash Info (CSD/ExtCSD) | **0x00B5** | **0x00D3** | none (len 0) | CSD/ExtCSD register image, caller-sized | `0x44B09D` push 0xB5 · `0x44B098` push 0xD3 · call @ `0x44B0A4` | **measured** |

All eight are `0x00xx` — the high byte of the wire command is `0`, so the LE16 on the wire is
`xx 00` (e.g. hardware override ID = `BD 00`).

### 1.1 Calibration — reproduces every known op

The same call shape (`push <resp> ; push <cmd> ; mov ecx,esi ; call 0x448530`) was checked against
every opcode we already knew from our own loader. 8/8 exact agreement, so the method is trusted.

| Retrieval | Site | arg0 (cmd) | arg1 (resp) | Known op/resp | Match |
|---|---|---|---|---|---|
| Retrieving MCT | `0x44AE2D` / `0x44AE28`, call `0x44AE34` | 0xD9 | 0xC9 | 0xD9 → 0xC9 | ✅ |
| Retrieving Boot Mode | `0x44D4F8` push 0xDE, `0x44D4F7` push edi | 0xDE | 0xFA | 0xDE → 0xFA | ✅ |
| Retrieving Flash Regions Info | `0x44B36D` / `0x44B368`, call `0x44B374` | 0xB4 | 0xD2 | 0xB4 → 0xD2 | ✅ |
| Loader Action Log | `0x44E895` / `0x44E890`, call `0x44E8A0` | 0xB0 | 0xD0 | 0xB0, chunks 0xB5, ends 0xD0 | ✅ |
| PIN (0xE7) | `0x44CF3D` / `0x44CF38`, call `0x44CF44` | 0xE7 | 0xD1 | 0xE7 → 0xD1 | ✅ |
| Range hash (0xDC) | `0x449CA0` / `0x449C9B`, call `0x449CA7` | 0xDC | 0xCC | 0xDC → 0xCC | ✅ |
| Blocked-OS (0xEC) | `0x44CBAD` / `0x44CBA8`, call `0x44CBB4` | 0xEC | 0xFD | 0xEC → 0xFD | ✅ |
| Send block (0x40F9) | `0x44F079` / `0x44F077`, call `0x44F080` | 0x40F9 ¹ | 0x4006 | 0x40F9 → 0x4006 | ✅ |

¹ for the `0x40xx` family the high byte is passed separately as `arg2 = 0x40` (`0x44F075: push 0x40`);
the wire u16 is still `arg0 | arg2<<8`. See §3.2.

Boot Mode's response op is not a literal — it is a stack local: `0x44D40B: mov BYTE PTR [esp+0xc],0xfa`
(a second branch at `0x44D462` sets it to `0xC5`), reloaded at `0x44D49F` and pushed at `0x44D4F7`.

---

## 2. Method

1. **Locate the trace strings.** All eight `"Retrieving ..."` strings live in one `.rdata` cluster:

   | String | VA | file offset |
   |---|---|---|
   | `"Retrieving OS Metrics"` | `0x49DAF4` | `0x9CAF4` |
   | `"Retrieving Installer Metrics"` | `0x49DB44` | `0x9CB44` |
   | `"Retrieving DSP OS Metrics"` | `0x49DBAC` | `0x9CBAC` |
   | `"Retrieving Flash Info"` | `0x49DD20` | `0x9CD20` |
   | `"Retrieving DRAM Info"` | `0x49DDAC` | `0x9CDAC` |
   | `"Retrieving Flash Regions Info"` | `0x49DDFC` | `0x9CDFC` |
   | `"Retrieving Hardware Override ID"` | `0x49DEA0` | `0x9CEA0` |
   | `"Retrieving Boot Mode."` | `0x49E5BC` | `0x9D5BC` |
   | `"Retrieving boot count info"` | `0x49E6A0` | `0x9D6A0` |
   | `"Retrieving Vendor ID"` | `0x49E6DC` | `0x9D6DC` |
   | `"Retrieving MCT"` | `0x49DCA4` | `0x9CCA4` |
   | `"Retrieving Loader Action Log"` | `0x49E768` | `0x9D768` |

   Matched `"Got ..."` partners: `"Got Hardware Override ID"` `0x49DE84`, `"Got DRAM Info"` `0x49DD9C`,
   `"Got Installer Metrics"` `0x49DB2C`, `"Got DSP OS Metrics"` `0x49DB7C`, `"Got OS Metrics"` `0x49DAE4`,
   `"Got Flash Info"` `0x49DD10`, `"Got VendorID"` `0x49E6CC`, `"Got MemCfgTable"` `0x49DC94`.
   (`"Retrieving MCT"`'s partner is `"Got MemCfgTable"` — MCT is the memory-config table.)

2. **Find the code xrefs.** A plain scan of `.text` for the 4-byte little-endian VA found exactly one
   `push imm32` site per string (PowerShell-free; `pefile` + raw `find`):

   | String VA | xref (`push imm32` + 1) | containing handler |
   |---|---|---|
   | `0x49DAF4` | `0x44A7FE` → pushes `0x49DAF4` at `0x44A7FF` | starts `0x44A7E0` |
   | `0x49DB44` | `0x44A8FE` | mid-fn (starts ≈`0x44A8E0`) |
   | `0x49DBAC` | `0x44A9FE` | mid-fn |
   | `0x49DD20` | `0x44B07E` | starts `0x44B060` |
   | `0x49DDAC` | `0x44B24E` | starts `0x44B240` |
   | `0x49DEA0` | `0x44B58E` | starts `0x44B580` |
   | `0x49E6A0` | `0x44D65E` | starts `0x44D650` |
   | `0x49E6DC` | `0x44D78E` | starts `0x44D780` |

   Each is `push <string>; push 3; call [vtbl+0x10]` = the level-3 log print.

3. **Read the two constants pushed immediately after the print.** Each handler follows the identical
   shape; the two `push imm32` right before `mov ecx,esi / call 0x448530` are
   **`push <expected response op>` then `push <command op>`**.

4. **Enumerate every call to the send routine** to cross-check. `0x448530` has exactly **100** direct
   call sites, all inside `0x448D1A`–`0x44F0CE`. Interpreting each one produced a consistent table and
   reproduced all 8 previously-known op/resp pairs (§1.1), which is the calibration gate.

---

## 3. The send routine — `0x448530`

`__thiscall`, `ret 0x18` (`0x448675`) = 6 stack arguments plus `ecx = this`.

Prototype (as reconstructed):

```c
int SendOp(/*ecx*/ Loader* this,
           uint8_t cmd,        // arg0  -> packet byte +2 (low byte of wire cmd)
           uint8_t expectedResp,// arg1 -> response-discriminator / expected response op
           uint8_t cmdHigh,    // arg2  -> packet byte +3 (high byte of wire cmd)
           uint8_t* statusOut, // arg3  -> in/out transport status byte
           uint32_t reqLen,    // arg4  -> request payload length
           uint32_t timeout);  // arg5  -> always WORD PTR [this+0x801A]
```

### 3.1 Request packet construction — `0x459700`

`0x448542`–`0x448557` forwards `(arg0, arg2, &this[0x20], arg4)` to `0x459700`, which builds the frame:

```
0x45970E: add  eax,8                    ; eax = reqLen + 8
0x459711: mov  WORD PTR [esi],ax        ; buf[0..1] = total frame length  (LE16)
0x459722: mov  BYTE PTR [esi+0x2],cl    ; buf[2]     = cmd      (arg0)
0x459732: mov  BYTE PTR [esi+0x3],dl    ; buf[3]     = cmdHigh  (arg2)
0x459735: call 0x443E40                 ; checksum/header finalise
0x459741: not  ecx                      ; ecx = reqLen
0x459743: mov  DWORD PTR [edx+esi-0x4],ecx ; trailer = ~reqLen, at buf[total-4]
0x459753: call [vtbl+0x14]              ; Write(buf, total)
```

So the frame is:

| off | size | meaning |
|---|---|---|
| `+0` | 2 | total frame length = `reqLen + 8`, LE16 |
| `+2` | 2 | **command, LE16 = `cmd | cmdHigh<<8`** |
| `+4` | `reqLen` | request payload — lives at `this+0x24` |
| `+4+reqLen` | 4 | trailer = `~reqLen` (bitwise NOT of the payload length) |

The packet buffer is `this+0x20`, so the **request payload base is `this+0x24`**.
Callers stage their arguments by writing to `[this+0x24 …]` before the send. Two independent
confirmations of that:

* Boot Mode: `0x44D4B8: mov DWORD PTR [esi+0x24],0x1` / `0x44D4C4 … 0x44D4DA: mov [esi+0x28],eax`,
  then `0x44D4EE: push 0x8` → an 8-byte request payload `{mode, value}` at `this+0x24`.
* `0x44DC49` path: `0x44DC27: mov BYTE PTR [esi+0x24],0xFF`, then `0x44DC33: push 0x1` → a 1-byte
  request payload of `0xFF`.

### 3.2 Why `0x40xx` opcodes need `arg2`

`0x459732` writes `arg2` into `buf[3]`. For every opcode below `0x100`, `arg2 = 0` and the wire u16 is
just `arg0`. For the `0x40xx` family the caller pushes `0x40` as `arg2`:

* `0x44F075: push 0x40` / `0x44F077: push 0x6` / `0x44F079: push 0xF9` → wire cmd `C0`? no —
  `buf[2]=0xF9`, `buf[3]=0x40` → **0x40F9** on the wire (LE16 `F9 40`), expected resp `0x4006`.
* `0x44BE54: push 0x40` / `0x44BE56: push 0x6` / `0x44BE58: push 0xC0` → **0x40C0**, resp `0x4006`.

All eight UNKNOWN ops in §1 are `< 0x100` and pass `arg2 = 0` (`push edi`, `edi` zeroed by
`xor edi,edi` at `0x44A80F`, `0x44A90F`, `0x44AA0F`, `0x44B08F`, `0x44B25F`, `0x44B59F`, `0x44D667`,
`0x44D79F`), so no high-byte ambiguity exists for them.

### 3.3 Response packet — `0x459760`

`0x448599`–`0x4485AC` forwards `(&this[0x401C], statusOut, timeout)`. `0x459760` does
`Read(buf /*= &this[0x401C]*/, &len, 0x3FFC, timeout)` via `[vtbl+0x18]` and validates the declared
length. The received frame therefore starts at `this+0x401C` with the *same* 4-byte header:

| off | size | meaning |
|---|---|---|
| `this+0x401C` | 2 | total frame length, LE16 |
| `this+0x401E` | 2 | **response command, LE16** |
| `this+0x4020` | n | response payload, **n = totalLen − 8** |
| last 4 | 4 | trailer |

Status byte values written by `0x459760`: `0x1F` = timeout (`0x4597BD`), `0x2F` = length mismatch
(`0x4597AE`). `0x448604`/`0x448626` also treat `0x18`, `0x19`, `0x1A` as "continue / retry".

### 3.4 How each handler consumes the response

All eight handlers end with the same tail (e.g. `0x44A883`–`0x44A8A6` for OS Metrics):

```
movzx eax, WORD PTR [esi+0x401C]   ; total frame length
lea   edx, [esi+0x4020]            ; payload base
cmp   eax, ecx                     ; ecx = caller's buffer size
jbe   ...
mov   eax, ecx                     ; clamp
push  eax ; push edx ; push ebp    ; memcpy(callerBuf, payload, min(totalLen, callerSize))
call  0x461750
mov   [callerLenPtr], eax
```

⇒ the response is handed back **verbatim** as an opaque blob; CFP does no per-field parsing in the
`info` path. Note the clamp uses the **total** frame length rather than `totalLen − 8`, so CFP
over-copies by up to 8 bytes into its own buffer — harmless for us but worth mirroring carefully.

Response is **retried** in a loop bounded by `this+0x801C` with a 300 ms sleep
(`0x44A844: push 0x12C / call [0x487014]`) whenever the send returns `0x6C` (=`108`, "retry").

### 3.5 Error / control strings seen while tracing

| VA | string | role |
|---|---|---|
| `0x49D140` | `"Re-Sending (0x%02X,%d)..."` | logged with `cmd` at `0x44858C` |
| `0x49D11C` | `"Retry Requested, Re-Sending (%d)..."` | |
| `0x49D10C` | `"RAMIMAGE: %s\n"` | |
| `0x49D0E0` | `"QCFM idle timeout, attempting recovery ..."` | |
| `0x49D088` | `"Command failed due to timeout"` | → `0x0F` |
| `0x49D060` | `"Command failed due to device removal"` | → `0x28` |
| `0x49D04C` | `"CRC error on host"` | → `0x6C` (retry) |
| `0x49D030` | `"Security Failure on device"` | → `0x49` |
| `0x49DB90` | `"Failure retrieving metrics."` | DSP OS Metrics extra log, `0x44AA44` |

The status→return-code mapping lives in `0x448130` (`__thiscall`, `ret 8`), called from `0x44866C` with
`(actualStatusByte, arg1)`.

---

## 4. Evidence — per-row transcript

All excerpts are verbatim `objdump -d -M intel` output; `esi` = `this` throughout.

### 4.1 Retrieving Hardware Override ID → cmd `0xBD`, resp `0xBE`

```
44b580: push ecx
44b582: push esi
44b583: mov  esi,ecx
44b58e: push 0x49dea0            ; "Retrieving Hardware Override ID"
44b593: push 0x3                 ; log level 3
44b595: call edx                 ; [vtbl+0x10] = Log
44b597: movzx eax, WORD PTR [esi+0x801a]
44b59e: push eax                 ; arg5 = timeout
44b59f: xor  edi,edi
44b5a1: push edi                 ; arg4 = 0          <-- request payload length 0
44b5a2: lea  eax,[esp+0x17]
44b5a6: push eax                 ; arg3 = &status
44b5a7: push edi                 ; arg2 = 0 (cmd high byte)
44b5a8: push 0xbe                ; arg1 = EXPECTED RESPONSE 0xBE
44b5ad: push 0xbd                ; arg0 = COMMAND 0xBD
44b5b2: mov  ecx,esi
44b5b4: call 0x448530
...
44b606: push 0x49de84            ; "Got Hardware Override ID"
```
Retry clone at `0x44B5F5` (same `0xBE`/`0xBD` constants at `0x44B5E8`/`0x44B5ED`).

### 4.2 Retrieving DRAM Info → cmd `0xBF`, resp `0x00D9`

```
44b24e: push 0x49ddac            ; "Retrieving DRAM Info"
44b255: call edx
44b257: movzx eax, WORD PTR [esi+0x801a]
44b25e: push eax                 ; arg5
44b25f: xor  edi,edi
44b261: push edi                 ; arg4 = 0
44b262: lea  eax,[esp+0x17]
44b266: push eax                 ; arg3 = &status
44b267: push edi                 ; arg2 = 0
44b268: push 0xd9                ; arg1 = EXPECTED RESPONSE 0xD9
44b26d: push 0xbf                ; arg0 = COMMAND 0xBF
44b272: mov  ecx,esi
44b274: call 0x448530
44b2ca: push 0x49dd9c            ; "Got DRAM Info"
```
Note the cross-family collision: **DRAM Info's *response* opcode `0xD9` is the same value as the MCT
*command*** (`0x44AE2D`). This is a real quirk of the protocol, not a transcription error — direction
matters. Retry clone at `0x44B2B9`.

### 4.3 Retrieving Installer Metrics → cmd `0xE1`, resp `0x00C8`

```
44a8fe: push 0x49db44            ; "Retrieving Installer Metrics"
44a905: call edx
44a907: movzx eax, WORD PTR [esi+0x801a]
44a90e: push eax                 ; arg5
44a90f: xor  edi,edi
44a911: push edi                 ; arg4 = 0
44a912: lea  eax,[esp+0x1c]
44a916: push eax                 ; arg3 = &status
44a917: push edi                 ; arg2 = 0
44a918: push 0xc8                ; arg1 = EXPECTED RESPONSE 0xC8
44a91d: push 0xe1                ; arg0 = COMMAND 0xE1
44a922: mov  ecx,esi
44a924: call 0x448530
44a97a: push 0x49db2c            ; "Got Installer Metrics"
```

### 4.4 Retrieving DSP OS Metrics → cmd `0x00B2`, resp `0x00C8`

```
44a9fe: push 0x49dbac            ; "Retrieving DSP OS Metrics"
44aa05: call edx
44aa07: movzx eax, WORD PTR [esi+0x801a]
44aa0e: push eax                 ; arg5
44aa0f: xor  edi,edi
44aa11: push edi                 ; arg4 = 0
44aa12: lea  eax,[esp+0x1c]
44aa16: push eax                 ; arg3 = &status
44aa17: push edi                 ; arg2 = 0
44aa18: push 0xc8                ; arg1 = EXPECTED RESPONSE 0xC8
44aa1d: push 0xb2                ; arg0 = COMMAND 0xB2
44aa22: mov  ecx,esi
44aa24: call 0x448530
44aa44: push 0x49db90            ; "Failure retrieving metrics."  (level 2, on retry)
44aa8b: push 0x49db7c            ; "Got DSP OS Metrics"
```
This is the only one of the eight that logs an extra diagnostic before retrying.

### 4.5 Retrieving OS Metrics → cmd `0xD8`, resp `0x00C8`

```
44a7fe: push 0x49daf4            ; "Retrieving OS Metrics"
44a805: call edx
44a807: movzx eax, WORD PTR [esi+0x801a]
44a80e: push eax                 ; arg5
44a80f: xor  edi,edi
44a811: push edi                 ; arg4 = 0
44a812: lea  eax,[esp+0x1c]
44a816: push eax                 ; arg3 = &status
44a817: push edi                 ; arg2 = 0
44a818: push 0xc8                ; arg1 = EXPECTED RESPONSE 0xC8
44a81d: push 0xd8                ; arg0 = COMMAND 0xD8
44a822: mov  ecx,esi
44a824: call 0x448530
44a87a: push 0x49dae4            ; "Got OS Metrics"
```

### 4.6 Retrieving boot count info → cmd `0xE9`, resp `0x00FB`

The only one of the eight with a non-empty request payload.

```
44d65e: push 0x49e6a0            ; "Retrieving boot count info"
44d665: call edx
44d667: xor  edi,edi
44d669: mov  DWORD PTR [esi+0x24],edi   ; request payload dword <- 0  (= 00 00 00 00)
44d66c: movzx eax, WORD PTR [esi+0x801a]
44d673: push eax                 ; arg5
44d674: push 0x4                 ; arg4 = 4  <-- REQUEST PAYLOAD LENGTH = 4
44d676: lea  eax,[esp+0x17]
44d67a: push eax                 ; arg3 = &status
44d67b: push edi                 ; arg2 = 0
44d67c: push 0xfb                ; arg1 = EXPECTED RESPONSE 0xFB
44d681: push 0xe9                ; arg0 = COMMAND 0xE9
44d686: mov  ecx,esi
44d688: call 0x448530
44d697: cmp  eax,0x20            ; distinct return code 0x20 handled specially
```
`[this+0x24]` is the request-payload base (§3.1), so the request body is four zero bytes.
On the retry path (`0x44D6AF`) the dword is re-zeroed before each re-send.

### 4.7 Retrieving Vendor ID → cmd `0xDB`, resp `0x00CB`

```
44d78e: push 0x49e6dc            ; "Retrieving Vendor ID"
44d795: call edx
44d797: movzx eax, WORD PTR [esi+0x801a]
44d79e: push eax                 ; arg5
44d79f: xor  edi,edi
44d7a1: push edi                 ; arg4 = 0
44d7a2: lea  eax,[esp+0x17]
44d7a6: push eax                 ; arg3 = &status
44d7a7: push edi                 ; arg2 = 0
44d7a8: push 0xcb                ; arg1 = EXPECTED RESPONSE 0xCB
44d7ad: push 0xdb                ; arg0 = COMMAND 0xDB
44d7b2: mov  ecx,esi
44d7b4: call 0x448530
44d80a: push 0x49e6cc            ; "Got VendorID"
```

### 4.8 Retrieving Flash Info (CSD/ExtCSD) → cmd `0xB5`, resp `0x00D3`

```
44b07e: push 0x49dd20            ; "Retrieving Flash Info"
44b085: call edx
44b087: movzx eax, WORD PTR [esi+0x801a]
44b08e: push eax                 ; arg5
44b08f: xor  edi,edi
44b091: push edi                 ; arg4 = 0
44b092: lea  eax,[esp+0x1c]
44b096: push eax                 ; arg3 = &status
44b097: push edi                 ; arg2 = 0
44b098: push 0xd3                ; arg1 = EXPECTED RESPONSE 0xD3
44b09d: push 0xb5                ; arg0 = COMMAND 0xB5
44b0a2: mov  ecx,esi
44b0a4: call 0x448530
44b0fa: push 0x49dd10            ; "Got Flash Info"
44b136: push 0x49dcf0            ; "GET_FLASH_INFO failed - timeout"
```
⚠ Register the value collision: **`0xB5` is the Flash-Info *command* here but was previously observed
as the Loader-Action-Log *chunk response* opcode.** With the bootrom in the action-log state that would
be ambiguous, so the 0xB0 action-log stream must be drained to its `0xD0` terminator (`0x44E890`) before
issuing `0xB5`, or the two directions must be disambiguated by frame direction. The parser should
treat `0xB5` as direction-dependent.

---

## 5. Confidence

| Row | Confidence | Basis |
|---|---|---|
| 0xBD / 0xBE (HW Override ID) | **measured** | both constants read as `push imm32` at the call site |
| 0xBF / 0xD9 (DRAM Info) | **measured** | idem |
| 0xE1 / 0xC8 (Installer Metrics) | **measured** | idem |
| 0xB2 / 0xC8 (DSP OS Metrics) | **measured** | idem |
| 0xD8 / 0xC8 (OS Metrics) | **measured** | idem |
| 0xE9 / 0xFB + 4 zero bytes (boot count) | **measured** | idem, plus `arg4=4` and the dword zeroing |
| 0xDB / 0xCB (Vendor ID) | **measured** | idem |
| 0xB5 / 0xD3 (Flash Info) | **measured** | idem |
| Frame layout (§3.1/§3.3) | **measured** | `0x459711`/`0x459722`/`0x459732`/`0x459743` write the bytes |
| "arg1 = expected response op" semantic label | **measured value, inferred label** | arg1 is never written to the wire (`0x459722`/`0x459732` use arg0/arg2 only), so the *meaning* is an inference — but it is corroborated 8/8 against independently measured response ops and Boot Mode loads it from a local explicitly initialised to `0xFA` at `0x44D40B` |
| Request payload lengths | **measured** | `arg4` is consumed only as the payload length in `0x459700` |
| Response payload sizes | **inferred** | derivable only from the runtime frame (`totalLen − 8`); no compile-time size per op |
| Metrics blob offsets (§6) | **measured** | each is a literal displacement in an accessor (`mov eax,DWORD PTR [esi+0xA4]` etc.) |
| Metrics version gates (§6.2) | **measured** | `cmp DWORD PTR [esi],0x4000D` immediates |

Nothing in §1 is inferred-only: all 16 opcode constants (8 commands + 8 responses) are read directly
off the disassembly as immediate operands at the named addresses. The residual inference is limited to
the *meaning* of arg1 and to per-op payload sizes, both flagged above.

One requested label does not exist: **there is no `"Bootrom Version"` string anywhere in the file**
(0 hits). The version line CFP prints is `"    Metrics Version:   %d.%d\n"` (`0x496BE8`), which is the
packed dword at blob `+0x00`. The `Bootrom Size` line is a different field (`+0xA4`).

---

## 6. Bootrom-metrics blob field offsets — RECOVERED

The metrics block printer is one function spanning `0x42DCF0`–`0x42E1C0`. Inside it `edi` holds a
**metrics object**; the actual byte blob is at **`B = [edi+0x4]`**, and every field is read through a
tiny `__thiscall` accessor of the shape
`if (valid) return *(u32*)(B + off); else return <default>;`
(e.g. `0x42D680: … 0x42D6A9 cmp DWORD PTR [esi],0x4000D ; 0x42D6B1 mov eax,DWORD PTR [esi+0xA4]`).

All labels are `push imm32` immediates, e.g. `0x496CE8`/`0x496CEC` at `0x42DD6D`:

| VA | format string | printed by |
|---|---|---|
| `0x496A04` | `"Factory (Remaining boot count: %i)\n"` | `0x42E1A3` |
| `0x496A28` | `"Expired factory mode\n"` | `0x42E189` |
| `0x496A40` | `"Unknown\n"` | `0x42E17D` |
| `0x496A4C` | `"N/A\n"` | `0x42E153` |
| `0x496A54` | `"    Boot Mode:         "` | `0x42E13B` |
| `0x496A6C` | `"    External MCT/HWV:  %sabled\n"` | `0x42E0EA` |
| `0x496A8C` | `"    Persist Data Addr: 0x%08X\n"` | `0x42E0B6` |
| `0x496AAC` | `"    Bootrom Size:      0x%08X\n"` | `0x42E08E` |
| `0x496ACC` | `"    LDR Blocks:        0x%08X\n"` | `0x42E066` |
| `0x496AEC` | `"    FlashID:           0x%08x\n"` | `0x42E03E` |
| `0x496B0C` | `"    Processor:         0x%08x\n"` | `0x42E00B` |
| `0x496B2C` | `"    Drivers:           0x%08X\n"` | `0x42DFE3` |
| `0x496B4C` | `"    Supported Options: 0x%08X\n"` | `0x42DFBB` |
| `0x496B6C` | `"    Security:          %sabled\n"` | `0x42DF8A` |
| `0x496B94` | `"    Build User:        %s\n"` | `0x42DF66` |
| `0x496BB0` | `"    Build Time:        %s\n"` | `0x42DF2D` |
| `0x496BCC` | `"    Build Date:        %s\n"` | `0x42DEF4` |
| `0x496BE8` | `"    Metrics Version:   %d.%d\n"` | `0x42DEBB` |
| `0x496C30` | `"    Supported Bands:   0x%04X"` | `0x42DE66` |
| `0x496C78` | `"    BR ID:"` | `0x42DDFD` |
| `0x496C94` | `"    Hardware OS ID:    "` | `0x42DDB6` |
| `0x496CD8` | `"    HW ID Override:"` | `0x42DD7A` |
| `0x496CEC` | `"    Hardware ID:       0x%08X %s\n"` | `0x42DD6D` |

### 6.1 Blob field table (`B = [edi+0x4]`)

| Blob offset | Width | Field | Accessor | Print site | Version gate on `[B+0]` |
|---|---|---|---|---|---|
| `+0x00` | u32 | **Metrics Version** (packed: `%d.%d` from `hi16`/`lo16`) | `0x42D376: mov eax,[eax]` | `0x42DEBB` | — |
| `+0x08` | u32 | (read by accessor, not printed) | `0x42D3A9: [eax+0x8]` | — | — |
| `+0x0C` | u32 | (read by accessor, not printed) | `0x42D449: [eax+0xC]` | — | — |
| `+0x10` | char[] | **Hardware ID** companion string (`%s`) | `0x42DD5D: mov eax,[edi+0x4]; add eax,0x10` | `0x42DD6D` | — |
| `+0x50` | char[] | **Build User** | `0x42DF5A: add eax,0x50` | `0x42DF66` | — |
| `+0x60` | char[] | **Build Date** | `0x42DEE8: add eax,0x60` | `0x42DEF4` | — |
| `+0x70` | char[] | **Build Time** | `0x42DF21: add eax,0x70` | `0x42DF2D` | — |
| `+0x7C` | u32 | **Security** (`0` ⇒ "disabled") | `0x42D3FD: cmp [eax+0x7C],0` | `0x42DF8A` | `0x20001` |
| `+0x82` | u16 | **Supported Bands** | `0x42DE56: movzx eax,WORD [ecx+0x82]` | `0x42DE66` | — |
| `+0x84` | u32 | **Supported Options**; **bit 0 also = External MCT/HWV enabled** | `0x42D560: [esi+0x84]` / `0x42D3D8: and eax,1` | `0x42DFBB`, `0x42E0EA` | `0x60006` / `0x4000E` |
| `+0x9C` | u32 | **Drivers** (default `1`) | `0x42D5A6: [esi+0x9C]` | `0x42DFE3` | `0x40005` |
| `+0xA0` | u32 | **LDR Blocks** | `0x42D671: [esi+0xA0]` | `0x42E066` | `0x40008` |
| `+0xA4` | u32 | **Bootrom Size** | `0x42D6B1: [esi+0xA4]` | `0x42E08E` | `0x4000D` |
| `+0xAC` | u32 | **Processor** | `0x42D5E1: [esi+0xAC]` | `0x42E00B` | `0x60007` |
| `+0xB0` | u32 | **FlashID** | `0x42D62E: [esi+0xB0]` | `0x42E03E` | `0x60011` |
| `+0xB8` | u32 | **Hardware OS ID** (default `0xFF000000` when gated out) | `0x42D491`/`0x42D4AD: [ecx+0xB8]` (fn `0x42D450`) | `0x42DDB6` header → `0x42DDDB`/`0x42DDE8` value | `0x6001B` |
| `+0xBC` | u32 | **BR ID** | `0x42D501`/`0x42D51D: [ecx+0xBC]` (fn `0x42D4C0`) | `0x42DDFD` header → `0x42DE22`/`0x42DE2F` value | `0x6001D` |
| `+0xC0` | u32 | **Persist Data Addr** | `0x42D6F1: [esi+0xC0]` | `0x42E0B6` | `0x60024` |

Two fields live on the **metrics object**, not the blob:

| Object field | Field | Print site |
|---|---|---|
| `[edi+0x28]` | **Hardware ID** value (`0x%08X`), printed with the blob `+0x10` string as `%s` | `0x42DD63` / `0x42DD6D` |
| `[edi+0x20]` / `[edi+0x24]` | **HW ID Override**: override value / `OSTypes` (format `0x496CB8` = `"0x%08X (OSTypes: 0x%08X)\n"`, or `"None\n"` when `[edi+0x20]==0`) | `0x42DD82`, `0x42DD8E` |
| `[edi+0x2C]` → `[+0x00]` | **Boot Mode** enum: `1` → `"Factory (Remaining boot count: %i)"` (`0x496A04`); `2` → `"Expired factory mode"` (`0x496A28`); `3` → `"Unknown"` (`0x496A40`); `0` → `0x4969F8`/`0x4969EC` | `0x42E16C`–`0x42E1B9` |
| `[edi+0x2C]` → `[+0x04]` | remaining boot count for the `Factory` line | `0x42E19C` |

### 6.2 Version gating (important for a parser)

CFP prints a field only when `[B+0]` (the packed Metrics Version) is `>=` the threshold listed above,
and the comparison values are themselves packed `major.minor` dwords:

| Threshold | Meaning |
|---|---|
| `0x20001` | 2.1 — gate for **Security** |
| `0x40005` | 4.5 — gate for **Drivers** |
| `0x40008` | 4.8 — gate for **LDR Blocks** |
| `0x4000D` | 4.13 — gate for **Bootrom Size** |
| `0x4000E` | 4.14 — gate for **External MCT/HWV** |
| `0x60006` | 6.6 — gate for **Supported Options** |
| `0x60007` | 6.7 — gate for **Processor** |
| `0x60011` | 6.17 — gate for **FlashID** |
| `0x6001B` | 6.27 — gate for **BR ID** |
| `0x6001D` | 6.29 — gate for **Hardware OS ID** |
| `0x60020` | 6.32 — gate for the leading `***** WARNING *****` banner (`0x496C08`) |
| `0x60024` | 6.36 — gate for **Persist Data Addr** |

Also note `[edi+0x1C]` and `[edi+0x8]` are validity flags checked before every accessor, and
`lea ecx,[edi+0xC]; call 0x446480` is a further "is-populated" predicate. A parser that ignores these
will see all-zero fields on an older bootrom.

**Correction to two earlier dead-ends** (recorded so it is not repeated): the *exact* string VAs
`0x496A70`/`0x496AB0`/`0x496C7C`/`0x496B50` really are **unreferenced** — the printed formats actually
begin **4 bytes earlier**, at `0x496A6C`/`0x496AAC`/`0x496C78`/`0x496B4C` (the leading four spaces of
`"    Bootrom Size: …"` are part of the string). Searching for the label text as a bare literal
finds `0x496A70`, but the code pushes `0x496A6C`; scanning for the literal's VA therefore misses the
xrefs and scanning for `push imm32` into `0x496A00`–`0x496D00` finds all 30 of them.

---

## 7. Open questions

1. **`0x20EE` preflash does not fit the model.** At `0x44E6F3` the pushes are `0x39` (arg1/resp) and
   `0x20` (arg0/cmd) with `arg2 = 0`, i.e. wire command **`0x0020`**, not `0x20EE`. Either our earlier
   `0x20EE` note is wrong, or preflash uses a different send routine (`0x44E46D` nearby uses
   `mov ecx,edi` rather than `mov ecx,esi`, so it targets a different object). Worth re-checking
   against a live trace before relying on it. This does **not** affect any §1 row.
2. **`arg2` high-byte encoding** — verified for `0x40C0`/`0x40D7`/`0x40F9` (all `arg2 = 0x40`). No
   opcode above `0x40FF` was found in the 100 call sites, so there is no evidence yet of a >8-bit
   extension mechanism beyond this single shared high byte.
3. **`0xB5` direction ambiguity** (§4.8) — needs a live capture to settle whether the terminal logic
   distinguishes request vs response `0xB5`.
4. **Response size per op** is not encoded at compile time; we can only bound it by the caller's buffer.
   If we need exact sizes, the transport `Read` at `[vtbl+0x18]` and the per-op callers in the
   `0x44A7E0`–`0x44DAxx` family would have to be traced for their declared buffer lengths.
5. **Boot Mode has two expected responses** (`0xFA` at `0x44D40B`, `0xC5` at `0x44D462`) selected by a
   branch on an incoming flag. We only ever saw `0xFA`; which condition selects `0xC5` is unresolved.
6. **Row 8 (Flash Info `0xB5`) payload internal layout is not decoded.** There is a large
   `"<REG_NAME> = 0x%X"` format-string run in `.rdata` covering the whole JEDEC CSD / eMMC ExtCSD
   register set (`ERASE_GRP_MULT`, `C_SIZE`, `EXT_CSD_REV`, `BOOT_SIZE_MULT`, `PARTITION_CONFIG`, …),
   which is clearly the printer for the `0xB5` response, but its exact VAs were not pinned down in this
   pass (an early offset arithmetic slip put them 0x1000 out, and they were not re-verified). If the
   register offsets inside the CSD/ExtCSD image are needed, that printer is where to start — it is the
   natural follow-up and is the same shape as §6.
