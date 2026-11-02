# 166 — CFP Loader Action Log: the split rule, the record layout, and how `--06` renders

**Status:** complete. Static only (no device, `CFP.exe` not executed, no binary modified).

**Headline (both questions answered):**

1. **CFP splits on `0x00`, not on a pattern.** The action log is stored in NVRAM record
   `0x2033` as a **sequence of NUL-terminated ASCII strings**, newest first. The loader's
   append path copies `strlen(text)+1` bytes — **including the terminating NUL**
   (`0x801186A4 add r5,r0,#1`, `0x801186F4` copies `r5` bytes) — and CFP's printer walks the
   buffer with `cursor += strlen(cursor) + 1` (`0x4211C9`). There is **no "start of entry"
   pattern** and no separator other than `0x00`. Our reader was showing one packed line
   because it dropped/ignored the NUL bytes.
2. **A record is `<8 hex epoch> ':' <tail>`, and the `--06` is a phantom.** The raw text
   never contains `--06`; it contains `--0` `0x00` `6A93…` — the `6` belongs to the *next*
   record's epoch. `0` is the status code, and `ErrorCode` table entry `0` is `"Success!"`,
   hence `…DEV-ErrorCode:0-Success!`. The trailing `"--<decimal code>"` is rewritten by CFP as
   `"-ErrorCode:<code>-<message>"` (one `-` of the `--` survives).

Both claims are verified end-to-end: a 25-line reimplementation of the measured algorithm
reproduces **all 67 lines** of CFP's own `-v5` dump byte-for-byte from the reconstructed
storage stream (§5).

Claim tags: **[M]** measured off the disassembly/bytes, **[I]** inferred,
**[C]** corroborated by an independent second measurement.

### Address convention

`CFP.exe` PE32 i386, ImageBase `0x400000`; section table is RVA-based
(`.text` RVA `0x1000`, `.rdata` RVA `0x87000` raw `0x86000`, `.data` RVA `0xAF000` raw
`0xADE00`). All addresses below are **VAs**; file offset = `VA − 0x400000 − sectionRVA +
sectionRaw`. Loader VAs are `0x80100000`-based, file offset = `VA − 0x80100000`
(per report 165 §0). `.rdata` and `.data` **are readable statically** (report 165 assumed
`.data` was encrypted; the ErrorCode string table at `0x4AF728` is plain pointers into
`.rdata` and is fully readable — see Appendix A).

---

## 0. Entry grammar (regex-ready) and reference parser

### Storage form (what comes back in the `0xB4` payload)

```
log        := entry ( NUL entry )* NUL          ; NUL = 0x00, newest entry first
entry      := shortcode | timestamped
shortcode  := [SDBE] ':' HEX{8}
timestamped:= HEX{8} ':' tail
tail       := name ( '--' DECIMAL+ )?
name       := printable ASCII, may itself contain ':' (e.g. "DONE:QNXLOAD:2.0.0.4869 DEV")
```

Practical regexes (applied to one NUL-delimited entry):

```python
ENTRY   = re.compile(rb'\A(?:[SDBE]:[0-9A-F]{8}|[0-9A-F]{8}:[^\x00]+)\Z')
SHORT   = re.compile(rb'\A[SDBE]:[0-9A-F]{8}\Z')
STAMP   = re.compile(rb'\A([0-9A-F]{8}):(.*)\Z', re.S)
CODE    = re.compile(rb'--([0-9]+)\Z')            # decimal digits, end of entry
```

Measured field widths **[M]**:

| field | width | producer |
|---|---|---|
| letter | 1 | `0x801170B4` writes `'D'`, `0x801170C0` writes `':'` |
| `:` | 1 | same |
| value / epoch | **8 hex digits, uppercase** | loader: 8 nibble loop `0x801170D0`-`0x80117100`, `addhi #0x37` ⇒ `A`–`F`; CFP: `"%lX"` (`0x49E758`) which is *not* zero-padded — see §8 O2 |
| version | `%d.%d.%d.%d`, no padding | `0x49E758` |
| code | decimal, variable width, no padding | `"%s--%d"` (`0x49E73C`) |

### Reference parser/renderer (this reproduces CFP exactly — verified in §5)

```python
def strtoul16(s):                      # 0x4607B8(s, &end, 16)
    i = 0
    while i < len(s) and s[i] in '0123456789abcdefABCDEF': i += 1
    return (int(s[:i], 16) if i else 0), i

def is_timestamp(e):                   # 0x421051 .. 0x421074
    v, end = strtoul16(e)
    if v <= 0x10: return False         # 0x42105B cmp eax,0x10 / 0x421066 jle  (SIGNED)
    if end + 1 < len(e) and e[end+1] == ':': return False   # 0x421070
    return True

def render(e, table, tz=timezone.utc):
    if not is_timestamp(e):
        return e                                        # verbatim, 0x4211AB
    date = asctime(localtime(int(e[:8], 16), tz))[:-1]  # 0x464C39; '\n' is eaten by the append
    s = date + e[8:]                                    # 0x421137: buf+strlen(buf)-1
    p = e.find('--')                                    # 0x4210AF strstr(e, "--")
    if p >= 0 and len(e) - p > 2:                       # 0x4210C9 strlen(p) <= 2 → skip
        ct = e[p+2:]
        if ct.isdigit():                                # 0x4210F5 (*endptr == 0)
            code = int(ct)
            if code <= 0xA9:                            # 0x42113F cmp edi,0xA9 / ja (UNSIGNED)
                s = s[:len(s)-(len(ct)+1)] + 'ErrorCode:%d-%s' % (code, table[code])
    return s
```

Reading the log: `entries = payload_text.split(b'\x00')[:-1]`.
Displaying: `"\n".join(render(e) for e in entries)`.
Chronological order = **reversed(entries)** (the append prepends, report 165 §5.3;
`0x801186D4`-`0x801186E4` is the `memmove` that makes room at the front).

---

## 1. Q1 — where CFP splits the string into lines

`0x420FF0` is the printer (`__thiscall`, no args, `ret`; stack cookie at `0x420FF6`). Body,
measured, `this = [esp+8]` at `0x42100E`:

```
0x421007  cmp dword [esi+0x8DC0],0 ; jbe 0x4211E4        ; capacity member == 0 → return
0x421021  ebp = [esi+0x8DBC]                              ; ebp = cursor = text base
0x421027  log(4, 0x493E98)                                ; "Dumping the Loader Action Log:"
0x421030  eax = [esi+0x8DC0] ; add eax,[esi+0x8DBC]       ; eax = base + capacity = end
0x42103C  cmp ebp,eax ; jae 0x4211E3                      ; already past end → return
loop @0x421046:
  0x421051  v   = strtoul(cursor, &endp, 16)              ; 0x4607B8 (→0x46055E)
  0x42105B  cmp v,0x10 / 0x421066 jle  verbatim           ; ← signed compare
  0x421070  cmp byte [endp+1],':' / je verbatim           ; ← "X:" short-code guard
  --- timestamp branch ---------------------------------------------------------
  0x421085  memset(buf400, 0, 0x400)                      ; 0x45F2E0
  0x42108F  eax = 0x464C39(&v)                            ; asctime(localtime_s(&v))  ← §2.2
  0x4210A4  snprintf(buf400, 0x400, "%s", eax)            ; 0x48D86C = "%s"
  0x4210AF  p = strstr(cursor, "--")                      ; 0x464920, literal 0x493E94
  0x4210C9  if (strlen(p) <= 2) goto append               ; no code field
  0x4210E9  code = strtol(p+2, &endp2, 10)                ; 0x4607B8, base 10
  0x4210F5  if (*endp2 != 0) code = -1                    ; must be pure digits
  0x421137  snprintf(buf400+strlen(buf400)-1,
                     0x400-strlen(buf400), "%s", cursor+8) ; ← the tail, epoch skipped
  0x42113F  if (code <= 0xA9)  (unsigned)                 ; -1 skips
  0x421188    snprintf(buf400+strlen(buf400)-1-strlen(code_text),
                       0x400-strlen(buf400), "ErrorCode:%d-%s",
                       code, table[code])                 ; 0x493E84, table 0x4AF728
  0x4211A1  log(4, buf400)                                ; the re-formatted line
  goto advance
verbatim @0x4211AB:
  0x4211B6  log(4, cursor)                                ; the entry, verbatim
advance @0x4211B8:
  0x4211C9  ebp = cursor + strlen(cursor) + 1             ; ← skip the NUL
  0x4211D9  if (ebp < base+capacity) goto loop
```

**Answer:** the split is `strlen(cursor)+1` at **`0x4211C9`** (`lea ebp,[eax+ebp*1+0x1]`
after an inline `strlen`). A "new entry" is therefore **any byte following a `0x00`**, and
the end of a record is **the next `0x00`**. There is no start-pattern scan and no newline
handling anywhere in the body (the only byte comparisons are `':'` at `0x421070` and the
`"--"` search at `0x4210AF`).

### 1.1 The NUL really is in the stored data — both sides measured

| side | evidence |
|---|---|
| loader append | `0x801186A0 blx 0x8013117C` = `strlen`; **`0x801186A4 add r5,r0,#1`** ⇒ `n = strlen+1`; `0x801186C0 strb r0,[r1,#-1]` ⇒ `text[n-1]=0`; `0x801186F4 memcpy(out+4, text, r5)` ⇒ **the NUL is copied too**; `0x80118704`-`0x80118714` clamps the *total* to `0xFA0` **[M]** |
| loader read | `0x80118734`-`0x80118748`: `nvram_read(0x2033, out+4, 0x3FE8)`, `out->len = n` — a byte count, no transformation **[M]** |
| loader reply | `0x80103CE8 add r0,r0,4` — payload length is `textLen+4`, text sent verbatim (report 165 §2.2) **[M]** |
| CFP receive | `0x44DAED call 0x461750` = `memcpy(dst, this+0x4024, min(cap,textLen))` — plain copy, no splitting/rewriting (`0x44DAD1`-`0x44DAFB`) **[M]** |
| CFP print | 67 separate `log(4, …)` calls, i.e. 67 entries ⇒ there *are* 67 NULs in the payload **[M]** |

Our reader's "one packed string" is therefore a **display artifact**: the bytes contain
`0x00` between every entry. `textLen` counts those NULs.

### 1.2 The two guards at the top of the loop are classification, not splitting

* `v <= 0x10` — `strtoul("D:00010013", …, 16)` = `0xD` = 13; `"E:…"` = 14; `"B:…"` = 11;
  `"S:…"` = 0 (no hex digit). All ≤ 0x10 ⇒ verbatim. **[M]** (`0x421066` is `0F 8E`, i.e.
  a **signed** `jle` — see O2.)
* `endp[1] == ':'` — catches a *non-hex* one-character prefix followed by `'`:`'`
  (e.g. `"S:"`, where `strtoul` converts nothing and `endp == cursor`, so `endp[1]` is the
  colon). **[M]**

Together: "a short hex number at the start of the line, or a one-letter prefix" ⇒ not an
epoch ⇒ print verbatim. Nothing else about the line is interpreted.

---

## 2. Q2 — the timestamped record

### 2.1 Layout

```
HEX{8} ':' a '.' b '.' c '.' d ':' NAME [ '--' DECIMAL ]
^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^ ^^^^^^   ^^^^^^^^^^^^^^^
epoch (u32)  CFP version       action    status code (optional)
```

* The epoch+version prefix is produced by CFP itself at `0x44D8AD`
  (`snprintf(this+0x28, 0x7F, "%lX:%d.%d.%d.%d", epoch, 3, 10, 0, 22)`, format at
  `0x49E758`) **[M]**. Note the four constants pushed at `0x44D89C`-`0x44D8A2` are
  `22, 0, 10, 3` — i.e. **3.10.0.22**, this `CFP.exe`'s own version
  (`-v5` banner: "Version 3.10.0.22"). The observed records say **3.11.0.27**, so they
  were written by a *newer* tool than the binary we have [M for the constants, I for the
  conclusion].
* The tail is one of four formats, chosen at `0x44D8D2`-`0x44D937` **[M]**:

  | condition | format | .rdata |
  |---|---|---|
  | `[this+4] != 0`, `arg2 != 0xAA` | `":%s:%s--%d"` | `0x49E74C` |
  | `[this+4] != 0`, `arg2 == 0xAA` | `":%s:%s"` | `0x49E744` |
  | `[this+4] == 0`, `arg2 != 0xAA` | `":%s--%d"` | `0x49E73C` |
  | `[this+4] == 0`, `arg2 == 0xAA` | `":%s"` | `0x49E738` |

  `0xAA` is the "no status code" sentinel (`cmp edx,0xAA` at `0x44D8DD`, `cmp eax,0xAA` at
  `0x44D910`) **[M]**. `[this+4]` is the word that supplies `"DONE"` — matching the observed
  `…:DONE:QNXLOAD:2.0.0.4869 DEV--0` vs `…:QNXLOAD:2.0.0.4869 DEV` pair **[M]/[I]**.

So with `[this+4]="DONE"`, `arg1="QNXLOAD:2.0.0.4869 DEV"`, `arg2=0` you get exactly
`6A93ECC2:3.11.0.27:DONE:QNXLOAD:2.0.0.4869 DEV--0`.

### 2.2 The epoch → date conversion (and why the trailing newline disappears)

`0x464C39` is **`asctime(localtime_s(&tm, &t))`**, measured **[M]**:

* `0x464C39 mov eax,[ebp+8]` = pointer to a **64-bit** `time_t`; `0x464C47` NULL → `errno =
  0x16` (EINVAL); `0x464C68`-`0x464C71` reject negative values by testing the high dword
  (`[eax+4]`) as a signed 64-bit sign check.
* `0x464C85 call 0x4612AE` with `( &tm , &t )` where `tm` is a `0x24`-byte local
  (`0x464C81 lea eax,[ebp-0x24]`) = MSVC `struct tm` (9 ints) ⇒ `_localtime64_s`.
* `0x464C94 call 0x4703C9(&tm)` ⇒ `asctime`, returning a `char*`.

`asctime` returns `"Www Mmm dd hh:mm:ss yyyy\n"` — **25 characters including `\n`**. The
printer's tail append lands at `buf + strlen(buf) − 1` (`0x421132`, `0x421183` — the
`-1` is visible as the `0x2b`/`0x2f` displacements), i.e. **exactly on the `\n`**, which the
following `snprintf` overwrites with `':'` **[M]**. That is why there is no line break
between the date and the version, and it also resolves report 165's uncharacterised
"`0x464C39` helper".

Timezone: `localtime` ⇒ the rendered date is in the **host's** timezone, not UTC. The capture
host was **UTC−3**: `0x6A93ECC2` = 1 788 079 298 = `Sun Aug 30 08:41:38 2026 UTC`, printed as
`Sun Aug 30 05:41:38 2026`; same −3 h for all seven epochs checked **[M][C]**.

### 2.3 The `--06` tail: there is no `--06`

Parsing rule from `0x4210AF`-`0x421189`: `p = strstr(entry, "--")`; require
`strlen(p) > 2`; `code = strtol(p+2, &end, 10)`; require `*end == 0`. So the field is
`"--" <decimal digits> "--"`, and a non-numeric tail makes `code = −1`, which the *unsigned*
`cmp edi,0xA9 / ja` at `0x42113F`/`0x421145` skips ⇒ the record is printed **with its
`--xxx` intact, unreformatted** **[M]**.

The capture's `…DEV--06A93EBF2:…` is, byte for byte,

```
... D E V - - 0 \x00 6 A 9 3 E B F 2 : 3 . 1 1 . 0 . 2 7 : Q N X L O A D ...
                 ^^^^^  ^^^^^^^^^
                 NUL    next record's 8-hex epoch
```

⇒ the code is **0**, not 6, and CFP prints `ErrorCode:0-Success!` because
`table[0] = "Success!"` (`[0x4AF728 + 0*4] → 0x490404`, measured — Appendix A) **[M]**.
This is proven non-circularly in §5: prepending nine extra `D:00010013` entries to the
stream reconstructed from CFP's *rendered* lines reproduces the operator's raw capture
**exactly**, including both `--06` positions.

The rewrite is `"--" + digits` → `"-" + "ErrorCode:<code>-<msg>"`: CFP cuts back
`len(digits)+1` characters (`0x421183` with `eax = strlen(buf) − strlen(code_text)`, minus the
`-1` above) and writes `"ErrorCode:%d-%s"` (`0x493E84`) at that point. The single surviving
`-` is the first half of the `--` separator, which is why the output reads
`DEV-ErrorCode:0-Success!` and not `DEVErrorCode:…` **[M]**.

### 2.4 The status-code enum

`0x421169 mov ecx,[edi*4+0x4AF728]` — a plain `char*[170]` table in `.data` at VA
`0x4AF728`, entries `0..0xA9`, each a pointer into `.rdata`. **It is readable statically**
(report 165 §5.4 assumed the `.data` container was encrypted — it is not; the pointers are
plain). Measured values include

| code | string | pointer |
|---|---|---|
| 0 | `Success!` | `0x490404` |
| 1 | `ABORTED` | `0x4903FC` |
| 2 | `Internal Error.` | `0x4903EC` |
| 3 | `Out of Windows memory` | `0x4903D4` |
| 4 | `Bad Parameters!` | `0x4903C4` |
| 5 | `Internal: invalid object` | `0x4903A8` |
| 6 | `Internal: not connected` | `0x490390` |
| 7 | `Bootrom: sent HUGE frame!` | `0x490374` |
| 8 | `This device is unknown. …` | `0x490328` |
| 9 | `Flash Chip unknown. …` | `0x4902E0` |
| 10 | `This loader software is too old for this device. …` | `0x490290` |
| 11 | `The OS is too new for this loader software.` | `0x490260` |

Whole table in **Appendix A**. Code `6` in this enum is *"Internal: not connected"* — worth
noting because it rules out the `--06` reading a second way: had the digits been `6`, CFP
would have printed `ErrorCode:6-Internal: not connected`.

---

## 3. Q3 — what `S:` `D:` `B:` `E:` and `0x0001000A/0B/10/11/13` are

**CFP knows nothing about them.** A byte search of `.rdata` + `.data` for `00010013`,
`00010011`, `0001000B`, `QNXLOAD` returns **zero** hits **[M]**. The only CFP-side treatment
of these lines is the pair of guards in §1.2, i.e. "short hex / one-letter prefix ⇒ print
verbatim". Their only CFP-visible property is that they are 10 characters.

Occurrences in the captured log (67 entries, counted from `CFP-info-V5.txt`) **[M]**:

| token | count | chronological neighbours (log is newest-first) |
|---|---|---|
| `D:00010013` | 21 | the most common; brackets every load attempt |
| `S:00010011` | 14 | runs of 1–3 before a load |
| `S:0001000B` | 7 | in the oldest (2011) region |
| `S:00010010` | 4 | in pairs, immediately before each `QNXLOAD` record |
| `E:0001000B` | 2 | immediately before each `SBM` record |
| `D:0001000B` | 1 | oldest region |
| `B:0001000A` | 1 | immediately after the `…:WF` record |

What *can* be traced into code, in our loader image **[M]**:

* Only **one** of these strings is built by the loader we have: `'D'`, `':'`, then 8 nibbles
  of the literal **`0x10013`** (`0x801170B4 mov r0,#0x44`, `0x801170C0 mov r0,#0x3a`,
  `0x801170D0`-`0x80117100` the nibble loop, literal word `0x10013` at `0x801171E4`).
* Its sink is `bl 0x80124E18` at `0x8011710C`, and `0x80124E18` is literally
  `1e ff 2f e1` = **`bx lr`**, a stub **[M]** — the same finding as report 165 §5.5, now
  with the byte pattern recorded. There is exactly **one** caller of that stub (scan of all
  `BL` encodings) **[M]**.
* The 8-hex formatter emits **uppercase** hex (`0x801170F0 addls #0x30` / `0x801170F4
  addhi #0x37`), matching the captured `00010013` **[M]**.
* Nothing in the image builds `S:`, `B:` or `E:` prefixed strings — no `'S'`/`'B'`/`'E'`
  letter-then-colon store sequence exists, and there is no ASCII `"S:"`/`"E:"` string
  (the only `"D:"` in the image is data at `0x80138E00`, a record-table key, not code) **[M]**.

Reading, therefore: **[I]** — the letters look like phase markers for a loader-side
"action" record (`S`=start, `D`=done, `B`=begin, `E`=end/exit) and the 8-hex value is an
opaque 32-bit id in the `0x000100xx` range (a "what" code), not an address and not a counter:
it repeats identically across boots and across years, and `0x00010013` is recorded at both
`2026` and `2011` timestamps. Confirming the vocabulary would need the *device's* loader
image (ours stubs the sink) or the newer 3.11.x host tool.

Adjacent, possibly-relevant vocabulary measured in CFP itself: `.rdata` contains 12
`"DONE:<NAME>"` string pairs (`DONE:E`@`0x4932B8`, `DONE:LOS`@`0x4932C4`, `DONE:EF`@`0x493D38`,
`DONE:WF`@`0x493D44`, `DONE:WFBF`@`0x493D50`, `DONE:RFBF`@`0x493DCC`, `DONE:HIS`@`0x493DE0`,
`DONE:HBR`@`0x493DEC`, `DONE:MS`@`0x493DFC`, `DONE:WSEC`@`0x49430C`, `DONE:LFS`@`0x49494C`,
`DONE:LI`@`0x494C54`), each immediately followed by the bare name — the same
`"DONE:<name>"` idiom the LAL uses, but a **different, older name set** (no `QNXLOAD`, no
`SBM`, no `WF` entry there), which is consistent with the LAL entries being written by the
newer 3.11.x tool **[M for the table, I for the attribution]**.

---

## 4. Q4 — separator, or fixed-size array with a zero-padded tail?

**Separator, and no fixed-size array** **[M]**:

* The separator is `0x00`; a record's end is found by `strlen` (`0x4211C9`) and the loader
  writes each record's NUL with it (`0x801186A4`/`0x801186F4`).
* The log is **variable-length and length-counted**, not padded: the loader stores the new
  total in `out[0]` (`0x80118708`), clamps it to `0xFA0`
  (`0x8011870C movhi r0,#0xfa0`, `0x80118714`), and writes exactly `len` bytes to NVRAM
  (`0x8011871C`). The read path returns exactly `n` (`0x80118744`). No zero-fill appears.
* Our capture's log is only **1359 bytes** (1260 for CFP's identical read + 9 extra
  `D:00010013`), far below both limits, so nothing was truncated.

Two real truncation paths do exist, and they are what a "mid-pattern end" would look like:

1. **CFP-side**: `0x44DAD8`-`0x44DADE` copies `min(capacity=4000, textLen)`; **`0x44DAD1
   mov byte [esi+0x4FC3],0`** forces a NUL at `text+0xF9F` (i.e. byte 3999 of the 4000-byte
   buffer). If `textLen > 4000`, the caller logs `"Action log overflow"` (`0x4932A4`) at
   level 2 (`0x41DF07`) and clamps (`0x41DF10`). The last printed line would then be a
   **partial** record, and (because of the forced NUL) it would terminate cleanly rather
   than run off the buffer **[M]**.
2. **Loader-side**: the append clamps the *total* to `0xFA0` = 4000 and then writes that many
   bytes (`0x80118704`-`0x8011871C`), which can cut the **oldest** record mid-string. The
   read path caps at `0x3FE8` = 16 360 bytes **[M]**.

Trailing repeats of `D:00010013` are **genuine entries**, not padding. Compare the two reads:

* CFP's read (`CFP-info-V5.txt` lines 216–282): 67 entries, 1260 bytes including NULs.
* the operator's live blob: **exactly** `"D:00010013" × 9` followed by CFP's stream with the
  NULs removed — 76 entries, 1359 bytes. Verified by exhaustive comparison (§5).

Nine extra `D:00010013` at the *head* means nine entries were appended **after** CFP's read
(the append prepends). The obvious candidate is our own `0xB0` bugdisp-handshake attempts
(the loader builds a `D:00010013` on that path, `0x801170B4`) — on the *device's* loader that
stub evidently logs; on our dumped image it is a `bx lr` **[I]**, testable by one more
`0xB3` read before/after a single `0xB0`.

**And the "text ends mid-pattern with `S:0001000B`" premise is wrong**: the operator's text
ends with `…4DB9C6D8:3.9.0.165:SBM` NUL `E:0001000B` — a complete 10-character record. It
only *looks* cut because the NULs are missing from the display, so `…SBME:0001000B` reads
like one run-on token.

---

## 5. Validation — the measured algorithm reproduces CFP's own output

Method: take CFP's 67 rendered lines (`docs/reports/data/cfp-2026-11-01/CFP-info-V5.txt`,
lines 215–282), invert the rendering (date → epoch with a `−3 h` host offset; `-ErrorCode:N-`
→ `--N`), rebuild the NUL-separated stream, then **re-render with the algorithm of §0** and
diff.

```
lines 67  entries 67  mismatches 0
total storage bytes (with NULs): 1260
```

and, independently, the operator's raw blob:

```
raw len 1283
EXACT: raw == 9 x "D:00010013" + CFP stream (NULs stripped)
total storage bytes for our read = 1359 | for CFP read = 1260
```

⇒ the entry grammar, the NUL split, the `asctime(localtime())` rendering (with the `\n`-eating
`-1`) and the `ErrorCode` rewrite are **all confirmed by an exact 67/67 round trip on data
CFP itself produced** **[M][C]**. Nothing in the model is fitted: the only free parameter is
the capture host's timezone offset.

Grammar coverage on the 76-entry stream (67 + 9): the `ENTRY` regex of §0 accepts **76/76** —
59 short codes and 17 timestamped. The 17 split as **7 `…:DONE:QNXLOAD:<ver> DEV--0`** plus
**7 matching bare `…:QNXLOAD:<ver> DEV` start records** (versions `2.0.0.4869`, `0.0.0.0`,
`2.1.0.1917` ×3, `10.0.9.1103` ×2, each start/done pair carrying the same version string),
plus **3 records from 2011 with no code field** (`3.9.0.165:SBM`, `3.9.1.157:WF`,
`3.9.0.165:SBM`). `--0` therefore appears on exactly the 7 completion records and nowhere
else; no other code value occurs in this log **[M]**. Longest entry is 50 bytes.

---

## 6. Evidence

### 6.1 CFP.exe — measured

| address | fact |
|---|---|
| `0x420FF0` | printer body; capacity `[this+0x8DC0]`, text base `[this+0x8DBC]` |
| `0x421051` | `strtoul(cursor,&endp,16)` (`0x4607B8`→`0x46055E`) |
| `0x421066` | `cmp eax,0x10` / `0F 8E` **signed** `jle` → verbatim |
| `0x421070` | `cmp byte [endp+1],':'` → verbatim (short-code guard) |
| `0x421085` / `0x4210A4` | `memset(buf,0,0x400)`; `snprintf(buf,0x400,"%s",date)`; `"%s"` = `0x48D86C` |
| `0x464C39` | `asctime(_localtime64_s(&t))`; `0x4612AE` = localtime64_s (`struct tm` = `0x24` at `0x464C81`), `0x4703C9` = asctime |
| `0x4210AF` | `strstr(cursor,"--")`, literal `0x493E94` = `"--"` (`0x464920` is a hand-rolled `strstr`) |
| `0x4210C9` | `strlen(p) <= 2` → no code field |
| `0x4210E9` | `strtol(p+2,&endp2,10)` |
| `0x4210F5` | `*endp2 != 0` → `code = -1` (unreformatted) |
| `0x421132` / `0x421183` | tail written at `buf+strlen(buf)-1`; capacity `0x400-strlen(buf)` |
| `0x42113F` / `0x421145` | `cmp edi,0xA9 / ja` (**unsigned**) gates the ErrorCode rewrite |
| `0x421169` | `[edi*4 + 0x4AF728]` — the status-string table |
| `0x421188` | `snprintf(…, "ErrorCode:%d-%s")`, `0x493E84` |
| `0x4211B6` | verbatim `log(4, cursor)` |
| `0x4211C9` | `cursor += strlen(cursor)+1` ← **the split** |
| `0x493E98` / `0x4932A4` / `0x48DEBE` | `"Dumping the Loader Action Log:"` / `"Action log overflow"` / `"--> %s\n"` |
| `0x44DA10` body | `0x44DAD1` forces `text[0xF9F]=0`; `0x44DAD8-0x44DADE` `min(cap,textLen)`; `0x44DAED` `memcpy` (`0x461750`); `0x44DAFB` `*pLen = textLen` |
| `0x41DED1` caller | `0x41DECB` capacity = `0xFA0`; `0x41DEF7`-`0x41DF10` "Action log overflow" + clamp |
| `0x44D860` writer | `0x44D893` `memset(this+0x28,0,0x80)`; `0x44D8AD` `"%lX:%d.%d.%d.%d"` (`0x49E758`); `0x44D8D2`-`0x44D937` the four tail formats (`0x49E73C/44/4C`, `0x49E738`); `0xAA` = "no code" sentinel |
| `0x49E758`…`0x49E738` | `%lX:%d.%d.%d.%d`, `:%s--%d`, `:%s:%s`, `:%s:%s--%d`, `:%s` |
| `0x4AF728` | `char*[0xAA]` status strings, entry 0 → `0x490404 "Success!"` (Appendix A) |
| `.rdata` scan | **no** occurrence of `00010013`/`00010011`/`0001000B`/`QNXLOAD`; 12 `DONE:<NAME>` pairs (§3) |

### 6.2 Loader — measured (file offset = VA − `0x80100000`)

| VA | fact |
|---|---|
| `0x801186A0` / `0x801186A4` | `strlen(text)`; **`n = strlen + 1`** |
| `0x801186C0` | `text[n-1] = 0` |
| `0x801186E4` / `0x801186F4` | `memmove(out+4+n, out+4, 0x3FE8-n)`; `memcpy(out+4, text, n)` — **NUL included** |
| `0x80118704`-`0x80118714` | total clamped to `0xFA0`, stored, then written |
| `0x80118718` / `0x8011871C` | `nvram_write(0x2033, out+4, len)` |
| `0x80118734`-`0x80118748` | read path: `nvram_read(0x2033, out+4, 0x3FE8)`, `out->len = n` |
| `0x801170B4`-`0x80117108` | builds `'D' ':' <8 uppercase nibbles of 0x10013>`; literal `0x10013` at `0x801171E4` |
| `0x80124E18` | `1e ff 2f e1` = `bx lr` (stub); sole caller `0x8011710C` |

### 6.3 Reproduction data

* `docs/reports/data/cfp-2026-11-01/CFP-info-V5.txt` lines 215–282 — CFP's own 67-line dump.
* Scratchpad (session-local): `printer.asm` (raw disassembly of `0x420FF0`), `errorcodes.txt`
  (full 170-entry table dump), `xcheck.py` (rebuild-and-diff), `render.py` (reference renderer).

---

## 7. Confidence

| claim | confidence |
|---|---|
| entries are NUL-terminated, split at `0x4211C9` by `strlen+1` | **high** — printer body + loader append (`strlen+1` copy) + 67/67 round trip |
| the payload really contains `0x00` bytes | **high** — 67 log lines from a printer that splits on NUL, and the exact rebuilt stream |
| record = `HEX8 ':' tail [ '--' DEC ]` | **high** — writer formats at `0x44D8AD`/`0x44D8D2`-`0x44D937`, and the round trip |
| date = `asctime(localtime(t))`, `\n` overwritten | **high** — `0x464C39` body, and 7/7 epochs match at a −3 h offset |
| `--06` is `--0` + NUL + `6A93…` | **high** — exact reconstruction of the operator's blob (§5) |
| `ErrorCode:0-Success!` = table entry 0 | **high** — table read statically at `0x4AF728` |
| status enum is the same table for all codes `0..0xA9` | **high** — single table, unsigned bound matches `0xA9` |
| `S/D/B/E` + `0x000100xx` semantics | **low** — CFP has no mapping; letters are [I] only; our loader builds just `D:00010013` into a stub |
| the 9 extra `D:00010013` came from `0xB0` handshakes | **low/medium** — [I], consistent but untested here |

---

## 8. Open questions

1. **Who writes `S:`/`B:`/`E:`?** Our loader can only build `D:00010013`, and only into a
   `bx lr` stub (`0x80124E18`). The device's loader (or the 3.11.x host tool) evidently does
   log them. A byte-diff of our `loader_06001A06-00.bin` against a live LDR_01 dump, or a
   disassembly of the 3.11.0.27 CFP, would settle it.
2. **`"%lX"` is not zero-padded** (`0x49E758`) but the printer skips a hard **8** characters
   (`cursor+8`, `0x421134`). Any epoch `< 0x10000000` (i.e. before 1978-12-13) would render
   with the first `':'` at the wrong offset and corrupt that one line. Not reusable in
   practice, but it is a genuine asymmetry — a defensive parser should require exactly 8 hex
   digits and fall back to verbatim otherwise.
3. **The `v <= 0x10` test is a *signed* compare** (`0x421066` = `0F 8E jle`). A `time_t` with
   bit 31 set (`0x80000000`+ = after 2038-01-19) would be negative and would fall into the
   **verbatim** branch, printing raw hex in CFP but a date in a correct parser. Worth
   deciding deliberately for our tool: mirror CFP's signed test, or render the date.
4. **`[this+0x4]` (the `"DONE"` word)** — the member that supplies the `"DONE"` prefix is
   written elsewhere in `CFP_Imp`; not tracked in this pass. Only matters if we ever *write*
   LAL records with CFP-parity text.
5. **Codes `0xAA`+** — the `ErrorCode` table stops at `0xA9`; a record with code `0xAA`
   (= the "no code" sentinel, but also a valid-looking digit string) would be printed with
   the raw `--170` retained. No such record was observed.

---

## Appendix A — `ErrorCode` table (`[0x4AF728 + 4*code]`, codes 0…0xA9)

| code | `.rdata` ptr | string |
|---|---|---|
| 0 | `0x490404` | Success! |
| 1 | `0x4903fc` | ABORTED |
| 2 | `0x4903ec` | Internal Error. |
| 3 | `0x4903d4` | Out of Windows memory |
| 4 | `0x4903c4` | Bad Parameters! |
| 5 | `0x4903a8` | Internal: invalid object |
| 6 | `0x490390` | Internal: not connected |
| 7 | `0x490374` | Bootrom: sent HUGE frame! |
| 8 | `0x490328` | This device is unknown. Please use a newer version of the loader software. |
| 9 | `0x4902e0` | Flash Chip unknown. Please use a newer version of the loader software. |
| 10 | `0x490290` | This loader software is too old for this device. Please use a newer version. |
| 11 | `0x490260` | The OS is too new for this loader software. |
| 12 | `0x490220` | This loader software requires a more recent OS on the device. |
| 13 | `0x4901f8` | COM Error. Unable to open/config port |
| 14 | `0x4901e0` | Communications error. |
| 15 | `0x4901c4` | Communications timed out |
| 16 | `0x4901b4` | File not valid |
| 17 | `0x490194` | Can't open file/File not found |
| 18 | `0x490180` | Can't seek in file |
| 19 | `0x490170` | Can't read file |
| 20 | `0x49015c` | Can't write file |
| 21 | `0x490138` | This OS does not match this device! |
| 22 | `0x4900fc` | Sorry, without an OS, I can't save or load the file system |
| 23 | `0x4900c8` | This filesystem is too large to fit on this device |
| 24 | `0x4900a4` | Internal Error: RAM Image invalid. |
| 25 | `0x490074` | Operation/parameters invalid on secure device! |
| 26 | `0x490060` | File type unknown |
| 27 | `0x490048` | OS is not crypto signed |
| 28 | `0x490030` | OS signature is invalid |
| 29 | `0x48ffe0` | Connected to BootImage. Don't call ConnectToBootImage before making this call! |
| 30 | `0x48ff90` | Not connected to BootImage. Call  ConnectToBootImage before making this call! |
| 31 | `0x48ff74` | Invalid CRC in the bootrom |
| 32 | `0x48ff4c` | Command not supported on this device |
| 33 | `0x48ff18` | More than one USB device connected, please choose |
| 34 | `0x48fef8` | Could not connect to USB Device |
| 35 | `0x48fe88` | USB loader channel busy -- if you aren't running another instance of CFP, reset your device (... |
| 36 | `0x48fe54` | Could not connect to BlackBerry Device Manager |
| 37 | `0x48fe34` | USB Channel closed unexpectedly |
| 38 | `0x48fe10` | Unable to transmit to USB device |
| 39 | `0x48fdec` | Unable to receive from USB device |
| 40 | `0x48fdc8` | Internal Error: device disappeared |
| 41 | `0x48fdb8` | NUKE underway |
| 42 | `0x48fd8c` | NUKE error. Shouldn't be connected first |
| 43 | `0x48fd54` | This device's Memory Configuration Table is corrupt. |
| 44 | `0x48fd18` | The specified file system is incompatible with this device. |
| 45 | `0x48fd00` | The file has a bad CRC. |
| 46 | `0x48fcc4` | The specified HWV file contains no useful MCT information. |
| 47 | `0x48fc94` | The MCT specified is too big for this bootrom |
| 48 | `0x48fc68` | This bootrom does not match this device! |
| 49 | `0x48fc40` | Only QCT devices need the QCNV file! |
| 50 | `0x48fc24` | Flash verification error |
| 51 | `0x48fbfc` | Device uncalibrated -- missing band map |
| 52 | `0x48fbd8` | OS band map does not match device's |
| 53 | `0x48fbb8` | OS version no longer supported |
| 54 | `0x48fb78` | The OSStore format expected by OS is incompatible with device |
| 55 | `0x48fb3c` | This OS software does not support this hardware variant. |
| 56 | `0x48faf8` | Battery level too low for loading. Please charge your device first. |
| 57 | `0x48fad4` | Error accessing the Registry |
| 58 | `0x48fab8` | Battery voltage is invalid. |
| 59 | `0x48fa9c` | Patriot RAM image error. |
| 60 | `0x48fa84` | No bootrom specified. |
| 61 | `0x48fa60` | FAT FS is not supported by this OS. |
| 62 | `0x48fa48` | SW Parts List is full. |
| 63 | `0x48fa28` | SW Parts List is already empty. |
| 64 | `0x48f9f0` | Expected OS address is not appropriate for this device. |
| 65 | `0x48f9c8` | No more room for labels in this SFI. |
| 66 | `0x48f98c` | This OS software does not support this version of hardware. |
| 67 | `0x48f8c8` | Support for this version of hardware has been dropped in the OS.  Either get newer hardware, ... |
| 68 | `0x48f828` | This OS software has not been updated to support this version of hardware yet.  Either get an... |
| 69 | `0x48f804` | Flash region is not sector aligned. |
| 70 | `0x48f7d4` | Password detected, this device was not nuked |
| 71 | `0x48f79c` | Write must be sector aligned and a multiple of 2 bytes. |
| 72 | `0x48f770` | iDEN binary does not support this device. |
| 73 | `0x48f760` | Security error. |
| 74 | `0x48f738` | Patriot flash program command failed. |
| 75 | `0x48f70c` | S19 file has records in undefined regions |
| 76 | `0x48f6f4` | Invalid Berbug script. |
| 77 | `0x48f6a0` | The specified file system is incompatible with this device (sector size mismatch). |
| 78 | `0x48f648` | The specified file system is incompatible with this device (App FS version mismatch). |
| 79 | `0x48f5f0` | The specified file system is incompatible with this device (OS FS version mismatch). |
| 80 | `0x48f5c4` | API table included, but OS already linked. |
| 81 | `0x48f598` | import table found, but no failure table. |
| 82 | `0x48f56c` | export table found, but no OS is present. |
| 83 | `0x48f540` | import table found, but no App is present. |
| 84 | `0x48f514` | failure table found, but no App is present. |
| 85 | `0x48f4fc` | No HWV file specified |
| 86 | `0x48f4dc` | OS file too big for this device |
| 87 | `0x48f4a4` | An OTA update is in progress: complete it or do a WIPE |
| 88 | `0x48f48c` | LDR section corrupt. |
| 89 | `0x48f480` | Fix failed. |
| 90 | `0x48f454` | One or more files specified was not used |
| 91 | `0x48f430` | Cannot load encrypted file system |
| 92 | `0x48f410` | RAMIMAGE_LIB_VERSION mismatch |
| 93 | `0x48f3e0` | RTAS error.  See BBToolAuth log for details. |
| 94 | `0x48f3a8` | BBToolAuth is not installed.  See http://go/bbtoolauth |
| 95 | `0x48f360` | USB driver is too old to support RTAS.  Upgrade to at least 4.0.1.1. |
| 96 | `0x48f340` | Cannot downgrade bootrom. |
| 97 | `0x48f2f0` | This OS Build suffers from SDR178315.  See http://go/oswiki/SDR178315 for help. |
| 98 | `0x48f2cc` | EFS partitions must be contiguous. |
| 99 | `0x48f2b4` | Battery Voltage Unknown |
| 100 | `0x48f294` | No Patriot application loaded |
| 101 | `0x48f268` | Not allowed to erase Patriot FlashStrap. |
| 102 | `0x48f24c` | Flash dump buffer overflow. |
| 103 | `0x48f224` | Merged files have duplicate sections. |
| 104 | `0x48f20c` | Patriot unresponsive |
| 105 | `0x48f1e8` | Command works only on NAND devices. |
| 106 | `0x48f1c0` | Invalid or incompatible flash drivers. |
| 107 | `0x48f188` | RTAS permissions failure.  See http://go/rtasrequest |
| 108 | `0x48f15c` | Unexpected frame returned by the device. |
| 109 | `0x48f148` | Value unavailable |
| 110 | `0x48f130` | File already exists. |
| 111 | `0x48f118` | OS does not support NDK |
| 112 | `0x48f0ec` | File does not support this signature type |
| 113 | `0x48f0c0` | The ramimage UMPStore failed to initialize. |
| 114 | `0x48f080` | Error occurred when trying to read UMP file info from device. |
| 115 | `0x48f05c` | The UMP header version is too old. |
| 116 | `0x48f02c` | LDR Append data found but not supported by OS |
| 117 | `0x48f000` | OS requires LDR Append data but none found |
| 118 | `0x48efc8` | Hash of LDR append data does not match that in the OS. |
| 119 | `0x48ef90` | The ramimage does not support this hardware variant. |
| 120 | `0x48ef58` | The ramimage does not support this version of hardware. |
| 121 | `0x48eec0` | Support for this version of hardware has been dropped in the ramimage. Either get newer hardw... |
| 122 | `0x48ee10` | The ramimage has not been updated to support this version of hardware yet.  Either get an old... |
| 123 | `0x48eda8` | The UMP file is probably corrupted, the file size does not match that which is stated in its ... |
| 124 | `0x48ed8c` | UMP is not crypto signed |
| 125 | `0x48ed70` | UMP signature is invalid |
| 126 | `0x48ed40` | Required security dongle support is deprecated |
| 127 | `0x48ed1c` | The ramimage is not crypto signed. |
| 128 | `0x48ed00` | QCFP file headers invalid |
| 129 | `0x48ecc0` | Unsupported QCFM file version. Get a newer version of the tool. |
| 130 | `0x48ec48` | This OS does not support this version of hardware. Try a version of this OS that supports thi... |
| 131 | `0x48ebc8` | This ramimage does not support this version of hardware. Try a version of this tool that supp... |
| 132 | `0x48eb94` | QCFM archive has unexpected trailer size/content |
| 133 | `0x48eb70` | Imbedded signature does not match. |
| 134 | `0x48eb54` | Signature file not found. |
| 135 | `0x48eb48` | RSA error. |
| 136 | `0x48eb2c` | BRW signature is invalid |
| 137 | `0x48eafc` | The UMP partition was not found on the device. |
| 138 | `0x48eacc` | The number of UMPs on the device is incorrect. |
| 139 | `0x48ea98` | Flash verification failed for the UMP partition. |
| 140 | `0x48ea7c` | Invalid CRC in UMP data. |
| 141 | `0x48ea3c` | Attempted to modify Write Protected area on the flash device |
| 142 | `0x48ea20` | Flash Operation Failure. |
| 143 | `0x48e9f8` | This file does not match this device! |
| 144 | `0x48e9d8` | Error reading/parsing HWV file |
| 145 | `0x48e9c0` | Command requires a file |
| 146 | `0x48e9b0` | Missing Command |
| 147 | `0x48e9a0` | Unknown command |
| 148 | `0x48e990` | Invalid option |
| 149 | `0x48e978` | Missing Patriot Command |
| 150 | `0x48e960` | Unknown Patriot command |
| 151 | `0x48e948` | Invalid Patriot option |
| 152 | `0x48e938` | Missing Range |
| 153 | `0x48e920` | Missing HIS/HOS Mode |
| 154 | `0x48e910` | Option Conflict |
| 155 | `0x48e8fc` | Duplicate Option |
| 156 | `0x48e8e0` | Password cannot be empty |
| 157 | `0x48e8c4` | Unable to create log file |
| 158 | `0x48e8b0` | Invalid USB port |
| 159 | `0x48e88c` | Failed to set Hardware ID Override. |
| 160 | `0x48e868` | Hardware ID Override not supported. |
| 161 | `0x48e840` | Invalid Hardware ID Override OS Type. |
| 162 | `0x48e818` | This OS version is blocked from device. |
| 163 | `0x48e7fc` | Virtual Machine Detected. |
| 164 | `0x48e7bc` | The Secondary Bootrom partition was not found on this device. |
| 165 | `0x48e780` | Secondary bootrom loading is not supported on this device. |
| 166 | `0x48e754` | Secondary bootrom region error detected. |
| 167 | `0x48e73c` | Invalid QCNV object. |
| 168 | `0x48e6e8` | The bootrom version found on this device is too old. It doesn't support debugflags. |
| 169 | `0x48e698` | The flash range specified failed verification against verification value. |

Reproduce with: parse the PE section table, `off(a) = raw + (a-0x400000-sectionRVA)`,
then read `u32[170]` at `0x4AF728` and dereference each pointer as a C string.
