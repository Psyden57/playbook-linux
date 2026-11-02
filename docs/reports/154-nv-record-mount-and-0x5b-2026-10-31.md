# 154 — The NV-record catalogue mount, the record parse, and exactly what can emit `0x5B`

Date: 2026-10-31
Scope: **STATIC RE ONLY.** No device, no USB, no `/dev/ttyACM0`, no ssh, no
network, no flashing. Only bytes already in this workspace were read
(`tools/env.sh` → `arm-linux-objdump` 2.43.1, plus local `python3` for
byte/word scans and literal-pool resolution).
Only file created: this report.

Input: `local/proprietary/device-binaries/emmc-first1mb.bin`, mapping
**file offset == address − 0x80000000**; the ARM stage is file `0x9000..0x1E000`
(address `0x80009000..0x8001E000`). All of the code quoted below is **Thumb**
unless the listing is explicitly marked ARM.

Evidence markers: **[P]** proven from bytes (address + quoted instruction words)
· **[I]** inferred (reasoning stated) · **[U]** unresolved from the bytes
available.

Read with: `docs/project/BOOT-CHAIN.md` §1/§4, report **150** (record path),
report **151** (stage map), report **153** (the `0x5B` emitter and the four
sites). This report **is not a restatement of those**; it re-derives the four
`0x5B` reachability sets from the bytes and corrects several details.

---

## 0. Headline answers

| Q | Answer |
|---|---|
| **1. What does the mount do?** | `0x800112DC` opens the MMC **device** (`0x8000D20C`), runs the **store-header / key-state validator** `0x80010D82` (device-parameter check + a monitor keyed digest + one block read + a secure-module probe + SMC `0x31`/`0x32`), reads **geometry** (`0x8000FAF2`, mode 3 → RPMB block count, must be ≤ 1024), then reads **every block `0..nblocks-1`** with an authenticated RPMB read (`0x80010A34`, frame type 4) and parses records out of each (`0x80011196`). It returns `0`/`6` only, and emits `0x5B` iff **any** of: device open returns `-1`; `0x80010D82` returns 0; `0x8000FAF2` returns non-zero; block count > 1024; **any one block read fails**; **any one block's record layout overruns its 224 bytes**; or the catalogue would exceed 1024 entries. **The check is device-level and block-level. It is never record-level**: the mount never looks at a record's *name*, *slot* or *value*. |
| **2. Init chain** | **Eleven** ordered steps from `0x80014904` to the decrement, §2.1. Three of them produce `0x5B` directly — A (`0x80010D3C`) and D (`0x800090A0(1)`) at **site 2**, C (the mount) at **site 1** — while **site 3** lives inside the selector and is reachable from **five** call sites (not three), and **site 4** sits in the decrement step. Full table in §2.2. The important asymmetry: **a failed RPMB *write* never produces `0x5B`** — it produces **`0x2F`** (1-4) from inside `0x800114FA`/`0x80011332`, and because those calls pass level 0 they never return either; a **write-counter mismatch** is therefore a `0x2F`, and a bad *value* in an otherwise good record is the common way to reach `0x5B` site 4. |
| **3. Record parse** | The on-medium header is **24 (`0x18`) bytes, not `0x1C`**, and it carries **no `next` pointer**: records are located by scanning for the 32-bit magic **`0x5983BC2D`** on 4-byte boundaries, and the `offset` field exists only in the **28-byte RAM catalogue entry** (that is where "`0x1C`" comes from). Multiple records per block are normal and supported. A non-zero tail is harmless **unless it contains the magic**; an overlapping/over-long length, a false magic, or >1024 records fail the **mount** (⇒ `0x5B` site 1). Full analysis in §3. |
| **4. Could one record write cause `0x5B`?** | **Yes, but not the way it is usually described.** A single re-framed block with a valid frame MAC and a valid monitor seal always *mounts*; the mount is content-blind. What can then fire is **site 3** (record readable but `len != 4`, or the re-read name check fails, or a `slot` field naming another block) or **site 4** (record readable, `len == 4`, value `0` or `>0x32`, or the rewrite silently refused because the block is exactly full). **`0x5B` site 1/2 is *not* caused by the record's content** — only by the store's *layout* (a false magic, a length that overruns the block) or by the block becoming unreadable. The code **does** distinguish the two classes: they take different branches and leave different side effects (§4). |
| **5. Preconditions for arming** | Ten read-only checks, §5. The three that actually matter: the boot-state word at media `0x2040` must be **≤ 8 and not 0** (0 ⇒ `0x800156E8` "Nuking…"); `BOOTCTR_FCTRY` must be **exactly 4 bytes** (any other length ⇒ `0x5B` site 3, before the region verify); and its value must be in **`1..0x32`** (0 or >0x32 ⇒ `0x5B` site 4 *after* a full region verify and after the region struct has been written back). |

---

## 1. Q1 — the catalogue mount (`0x800112DC`), and its `0x5B` at `0x80014940`

### 1.1 Where it sits

`0x80014904` is `main` (`blx 0x80014904` from the ARM reset stub at
`0x80019264`, report 151 §1.2). The record-store prologue is byte-for-byte [P]:

```
80014904  b5f0        push {r4,r5,r6,r7,lr} ; sub sp,#20
80014910  f7f4 fde8   bl   0x800094e4        ; WDT2 kick
80014914  a074        adr  r0,0x80014ae8     ; "\nStarting RIM bootrom with UART_DEBUG enabled\n"
8001491a  a07f        adr  r0,0x80014b18     ; "OMAP4430 HS\n"
80014920  a081        adr  r0,0x80014b28     ; "SW_RESTORE support enabled\n"
80014926  f7fe ff21   bl   0x8001376c        ; 4770 "bx lr" — build stub
8001492a  f7fc fa07   bl   0x80010d3c        ; (1) RPMB/secure-store probe   -> 0/1
8001492e  b188        cbz  r0,0x80014954     ; 0 => 0x5B  (SITE 2)
80014930  f7fc fac2   bl   0x80010eb8        ; (2) provisioning probe        -> r7
80014934  4607        mov  r7,r0
80014936  f7fc fcd1   bl   0x800112dc        ; (3) ***** MOUNT / BUILD CATALOGUE *****
8001493a  b118        cbz  r0,0x80014944     ; 0 => 0x5B  (SITE 1)
8001493c  215b        movs r1,#0x5b
8001493e  2000        movs r0,#0
80014940  f000 f9aa   bl   0x80014c98        ; ***** SITE 1 : blink(0,0x5B), never returns *****
80014944  b15f        cbz  r7,0x8001495e     ; r7==0 => RPMB already provisioned
80014946  a07f        adr  r0,0x80014b44     ; "RPMB not provisioned\n"
8001494c  2001        movs r0,#1
8001494e  f7f4 fba7   bl   0x800090a0        ; (4) write BOOTCTR_FCTRY = 0x32
80014952  b970        cbnz r0,0x80014972    ; success -> continue
80014954  215b        movs r1,#0x5b
80014956  2000        movs r0,#0
80014958  f000 f99e   bl   0x80014c98        ; ***** SITE 2 : blink(0,0x5B), never returns *****
8001495c  e009        b    0x80014972
8001495e  f7f4 fbdc   bl   0x8000911a        ; (5) SELECTOR, evaluation #1
```

So `0x80014940` is not a *check*; it is the **only** thing that happens when
`0x800112DC` returns non-zero. `0x800112DC` returns exactly two values:
**`0` = mounted** and **`6` = failed** [P] (`0x8001132E: 2000 movs r0,#0`,
`0x8001132A: 2006 movs r0,#6`). There is no third state and no partial mount.

### 1.2 The mount, instruction by instruction [P]

```
800112dc  b53e        push {r1,r2,r3,r4,r5,lr}
800112de  2000        movs r0,#0
800112e0  a902        add  r1,sp,#8
800112e2  9000        str  r0,[sp,#0]              ; sp+0 = 0
800112e4  9001        str  r0,[sp,#4]
800112e6  2001        movs r0,#1
800112e8  f7fb ffd8   bl   0x8000d29c              ; (a) trivial stub: (1,&sp[8]) -> 1
800112ec  4a91        ldr  r2,=0x80025fb0          ; &store_state
800112ee  2101        movs r1,#1
800112f0  9802        ldr  r0,[sp,#8]              ; = 0 (slot)
800112f2  f7fb ff8b   bl   0x8000d20c              ; (b) block device OPEN (class 0, instance 1)
800112f6  1c40        adds r0,r0,#1
800112f8  d017        beq  0x8001132a              ; (b) handle == -1  => fail 6
800112fa  4c8e        ldr  r4,=0x80025fb0
800112fc  4890        ldr  r0,=0x80033760
800112fe  6821        ldr  r1,[r4,#0]              ; store_state->block_base
80011300  f7ff fd3f   bl   0x80010d82              ; (c) RPMB provisioning/key state
80011304  b188        cbz  r0,0x8001132a           ; (c) 0 => fail 6
80011306  6820        ldr  r0,[r4,#0]
80011308  2103        movs r1,#3
8001130a  466a        mov  r2,sp
8001130c  f7fe fbf1   bl   0x8000faf2              ; (d) GEOMETRY, mode 3
80011310  b958        cbnz r0,0x8001132a           ; (d) non-zero => fail 6
80011312  e9dd 0100   ldrd r0,r1,[sp]
80011316  0a00        lsrs r0,r0,#8
80011318  ea40 6001   orr.w r0,r0,r1,lsl #24
8001131c  f5b0 6f80   cmp.w r0,#1024
80011320  60a0        str  r0,[r4,#8]              ; store_state->nblocks = value
80011322  d802        bhi  0x8001132a              ; (e) nblocks > 1024 => fail 6
80011324  f7ff ff37   bl   0x80011196              ; (f) read+parse ALL blocks
80011328  b908        cbnz r0,0x8001132e           ; ok -> return 0
8001132a  2006        movs r0,#6                   ; ***** FAIL *****
8001132c  bd3e        pop  {r1,r2,r3,r4,r5,pc}
8001132e  2000        movs r0,#0                   ; ***** MOUNTED *****
80011330  bd3e        pop  {r1,r2,r3,r4,r5,pc}
```

Literal values used above, resolved from the pools [P]:

| pool slot | value | role |
|---|---|---|
| `0x80011530` | `0x8002C760` | record table, 1024 × 28 B |
| `0x80011534` | `0x80025FB0` | store state `{+0 block_base, +4 count, +8 nblocks}` |
| `0x80011538` | **`0x5983BC2D`** | **record magic** |
| `0x8001153C` | `0x80033F80` | 224-byte staging buffer |
| `0x80011540` | `0x80033760` | per-record area; `+0x20` ⇒ free-space table `0x80033780` (1024 × u16) |
| `0x80011104` | `0x8002C720` | 32-byte word-protect/state area (used `-0x20`, `+0x20`, `+0xA0`) |
| `0x80011108` | `0x80025F00` | KEK/IV working area (`+0x20`, `+0x80`, `+0xA0`) |

> **Correction to report 150 §1.1.** That report lists the literal at
> `0x80011538` as `0x80025983` and calls it "record magic". The pool slot is at
> address `0x80011538`; its **value is `0x5983BC2D`** [P]. `0x80025983` is not a
> magic anywhere in this image. Every record comparison uses `0x5983BC2D`
> (`0x8001116A: 4bf3 ldr r3,[pc,#972] @ 0x80011538`, then
> `0x80011174: 429e cmp r6,r3`).
> `0x80025983` would be an *unusual* value for this image's data region
> (`0x80025E74` is the OS entry slot, `0x80025F00` the KEK area), so this looks
> like a transcription slip, not a different build.

### 1.3 Each step, and what it actually checks

**(a) `0x8000D29C(1, &l)`** — a stub, return value discarded [P]:
```
8000d29c  b131   cbz r1,0x8000d2ac      ; out == NULL -> 0
8000d29e  b128   cbz r0,0x8000d2ac      ; arg == 0 -> 0
8000d2a0  2801   cmp r0,#1
8000d2a2  d103   bne 0x8000d2ac
8000d2a4  2000   movs r0,#0
8000d2a6  6008   str  r0,[r1,#0]        ; *out = 0
8000d2a8  2001   movs r0,#1
```
It sets a local to 0 and returns 1. Cannot fail the mount.

**(b) `0x8000D20C(0, 1, &dev)` — the block-device open.** This is the MMC
layer's open: it validates its arguments (`cmp r0,#3` / `cmp r6,#3` / `r5 != 0`,
returning `0xFFFFFFFC` on bad args), takes a per-device lock
(`0x8000E340`), and returns a handle or `-1` (`mvn r0,#3` at `0x8000D220` is the
arg-error path; the `-1` path is the device-not-present path). The mount tests
only `handle + 1 == 0`, i.e. **`-1`**, which is the "no such device / not
initialised" result [P for the test, I for the mapping]. **This is a
device-level check.** *(Note the latent weakness: an arg-error return
`0xFFFFFFFC` would pass the mount's test — but the mount's arguments are
constants, so it cannot happen here.)*

**(c) `0x80010D82(0x80033760, block_base)` — the RPMB provisioning/key state.**
Not what report 150 calls it. It is a **key-state validator**, and it is the
same routine the provisioning path uses (`0x80010F00`) [P]:

```
80010d82  b530        push {r4,r5,lr}
80010d84  4604        mov  r4,r0                  ; r4 = 0x80033760 (scratch)
80010d86  460d        mov  r5,r1                  ; r5 = block_base
80010d8c  a90a        add  r1,sp,#40
80010d92  f7fc f921   bl   0x8000cfd8            ; device present + RPMB-capable?  (0 or -4)
80010d96  2800        cmp  r0,#0
80010d98  d17e        bne  0x80010e98            ; -> return 0   (FAIL)
80010d9a  2214        movs r2,#20
80010d9c  48da        ldr  r0,=0x80025f00
80010da2  f008 e832   blx  0x80018e08            ; memmove(0x80033760+0x20, sp+40, 20)
80010da6  ...  bl 0x80009bf4  x3                 ; log(1,4,...) of 32 B at +0x20, +0x80 and -0x20
80010dd8  ...
80010df4  f006 ed22   blx  0x8001783c            ; ***** SMC 0x33 + 0x2F (keyed digest) *****
80010df8  2800        cmp  r0,#0
80010dfa  d058        beq  0x80010eae            ; -> return 0   (FAIL)
80010e0c  ...
80010e12  f7ff fda8   bl   0x80010966            ; RPMB type-1 "program key" result read?
80010e16  2800        cmp  r0,#0
80010e18  d149        bne  0x80010eae            ; -> return 0   (FAIL)
80010e1a  9809        ldr  r0,[sp,#36]
80010e1c  2800        cmp  r0,#0
80010e1e  d149        bne  0x80010eb4            ; -> return 1
80010e20  a808        add  r0,sp,#32
80010e22  f7ff ff77   bl   0x80010d14            ; SMC 0x0F (domain 0) + flag@0x8002C720&1
80010e26  2800        cmp  r0,#0
80010e28  d041        beq  0x80010eae            ; -> return 0   (FAIL)
80010e2a  9808        ldr  r0,[sp,#32]
80010e2c  2800        cmp  r0,#0
80010e2e  d13e        bne  0x80010eae            ; -> return 0   (FAIL)
80010e3e  ...  blx 0x80017cb4                       ; SMC 0x32
80010e48  b330        cbz  r0,0x80010e98            ; -> return 0   (FAIL)
80010e4a  ...  blx 0x80017c38                       ; SMC 0x31
80010e52  b308        cbz  r0,0x80010e98            ; -> return 0   (FAIL)
80010e78  ...  blx 0x8001783c                       ; SMC 0x33 + 0x2F again
80010e7c  b160        cbz  r0,0x80010e98            ; -> return 0   (FAIL)
80010ea4  f7ff fd5f   bl   0x80010966
80010ea8  b908        cbnz r0,0x80010eae           ; -> return 0
80010eaa  9809        ldr  r0,[sp,#36]
80010eac  b910        cbnz r0,0x80010eb4           ; -> return 1
80010eae  2000        movs r0,#0                   ; ***** FAIL *****
80010eb4  2001        movs r0,#1                   ; ***** OK *****
```

and its first gate `0x8000CFD8` [P]:
```
8000cfd8  0002        movs r2,r0
8000cfdc  f06f 0003   mvn.w r0,#3                 ; default 0xFFFFFFFC
8000cfe4  2b00        cmp  r3,#0
8000cfe6  d013        beq  0x8000d010             ; out == NULL -> -4
8000cfe8  494d        ldr  r1,=0x8002a0d0          ; the MMC device/partition struct
8000cfea  680a        ldr  r2,[r1,#0]
8000cfee  2a01        cmp  r2,#1
8000cff0  d10e        bne  0x8000d010             ; dev state != {0,1} -> -4
8000cff2  2a00 / f8d1 21bc  [r1+0x1BC] != 0 ?
8000cffa  2a00 / f8d1 21e0  [r1+0x1E0] == 0 ?
8000d002  2214        movs r2,#20
8000d004  f501 71b2   add  r1,r1,#0x164             ; copy 20 bytes from +0x164
8000d008  4618        mov  r0,r3
8000d00a  f00b eefe   blx  0x80018e08            ; memmove(out, dev+0x164, 20)
8000d00e  2000        movs r0,#0                   ; success
```

**Polarity, stated once and used throughout: `0x80010D82` returns `1` = accept,
`0` = reject** [P] — the mount rejects on `cbz r0` at `0x80011304`.

Step (c) performs, in this order: a **device-parameter check** for the store's
base LBA (`0x8000CFD8`: device state ∈ {0,1}, `[+0x1BC] != 0`, `[+0x1E0] == 0`,
otherwise `0xFFFFFFFC`); a **monitor keyed digest** over three 32-byte state
blocks (SMC `0x33` + `0x2F`, domain 5) — *non-zero* return = success here, as at
every other call site of `0x8001783C`; **one block read** through `0x80010966`;
a **secure-module probe** (`0x80010D14` → SMC `0x0F`, domain 0, plus bit 0 of
`0x8002C720`); then the `0x31` / `0x32` / `0x31` sequence and a second keyed
digest, and a second `0x80010966`.

`0x80010966` is the interesting one, because it is where the RPMB *result code*
is classified [P]:

```
80010966  ...          ; (dev->block_base read, frame type 4)
800109e6  4638        mov  r0,r7                  ; r7 = -1 unless a success path set it
800109ee  f8b4 01fc   ldrh.w r0,[r4,#508]        ; RPMB result field
800109f2  b9a0        cbnz r0,0x80010a1e
800109fe  ...         blx  0x80018e08            ; save the returned 32-byte tag
80010a06  ...         bl   0x800107b6            ; recompute the request MAC
80010a10  f008 f896   bl   0x80018b40            ; compare tags
80010a14  b150        cbz  r0,0x80010a2c           ; equal  -> *out = 0, return 0
80010a16  2000        movs r0,#0                   ; *out = 0
80010a1a  2000        movs r0,#0                   ; return 0
80010a1e  4620        mov  r0,r4
80010a20  f7ff fea6   bl   0x80010770            ; normalise
80010a24  f8b4 01fc   ldrh.w r0,[r4,#508]        ; RPMB result field again
80010a28  2807        cmp  r0,#7                   ; ***** 7 is special *****
80010a2a  d101        bne.n 0x80010a30
80010a2c  2001        movs r0,#1                   ; *out = 0 ; return 1
80010a30  602e        str  r6,[r5,#0]               ; *out = a non-zero pointer
80010a32  e7d8        b.n  0x800109e6             ; return r7
```

Result code **7** is treated as a *distinct, non-error* state in both
`0x80010966` (`0x80010A28 cmp r0,#7`) and `0x80010946`
(`0x80010958 cmp r0,#7 / 2000 movs r0,#0` ⇒ `*out = 0`). In the JEDEC RPMB
result-code set 7 is **"authentication key not yet programmed"**, 2 is
"authentication failure" and 3 is "write counter failure" — **[I]** on the
JEDEC mapping, **[P]** on the constant `7` being singled out. This is the code's
own way of distinguishing "the partition exists but was never keyed" from "the
partition rejected us".

**What the exact accept condition of `0x80010D82` is, is [U].** The bytes are
unambiguous about every individual test and about the two return values, but one
sub-branch (`0x80010E1E d149 bne 0x80010EB4`) returns **accept** when
`0x80010966` reported a *non-zero* RPMB result — i.e. an unusual case is folded
into "accept" rather than "reject". Report that as an oddity, not as a clean
rule. *[I] for the naming of each monitor service (the monitor image is not in
this corpus); [P] for every call, every constant and both return values.*

**(d) `0x8000FAF2(dev, 3, &q)` — geometry** [P]:
```
8000faf2  ...  r5 = 0x8002A0D0 (same device struct)
8000fb24  6828        ldr r0,[r5,#0]
8000fb26  2801        cmp r0,#1
8000fb28  d105        bne 0x8000fb36            ; not state 1 -> error
8000fb2a  f8d5 01bc   ldr r0,[r5,#0x1BC] ; must be != 0
8000fb30  f8d5 01e0   ldr r0,[r5,#0x1E0] ; must be == 0
8000fb42  4650        mov  r0,sl                ; r1 arg = mode
8000fb50  2803        cmp  r0,#3                ; mode 3 (RPMB)
8000fb54  95f8 4101   ldrb.w r0,[r5,#0x141]     ; RPMB size/geometry byte
8000fb58  0bc1        lsrs r1,r0,#15
8000fb5a  0440        lsls r0,r0,#17
8000fb66  c403        stmia r4!,{r0,r1}         ; (hi<<8 | lo>>24) = byte<<9
```
and mode 0 (`0x8000FB5E`) reads the 32-bit word at `+0x190` (`(X<<9, X>>23)` ⇒
`X<<1`). The mount's mode 3 therefore yields **`nblocks = byte@+0x141 << 9`**
[P for the arithmetic; [I] for "byte@+0x141 is the RPMB size in 128 KiB units",
which would make `1` ⇒ 512 blocks of 256 B = 128 KiB]. Return values: `0` ok,
`-1` (device error), or `0xFFFFFFFC` (bad args) — **the mount's `cbnz` treats
both error forms as failure** [P].

**(e) `nblocks > 1024` ⇒ fail.** The catalogue is 1024 × 28 B, so this is the
hard record-count ceiling [P].

**(f) `0x80011196` — read and parse every block.** This is the only step that
touches the stored *data*; see §3.

### 1.4 The exact set of states that make the mount emit `0x5B`

| # | condition | site of the test | value returned | level |
|---:|---|---|---|---|
| 1 | `0x8000D20C` returns `-1` (device not present / not initialised) | `0x800112F8 beq` | `6` | **device** |
| 2 | `0x8000CFD8` ≠ 0, i.e. device not registered, not RPMB-capable, or `[+0x1BC]==0` / `[+0x1E0]!=0` | `0x80010D98 bne` → `0x80011304 cbz` | `6` | **device / capability** |
| 3 | the `0x33`+`0x2F` keyed digest of the three state blocks returns 0 (digest failure) | `0x80010DF8`/`0x80010DFA beq` | `6` | **partition (keys)** |
| 4 | the block read `0x80010966` returns non-zero (transport failure) | `0x80010E18 bne`, `0x80010EA8 cbnz` | `6` | **partition (keys)** |
| 5 | the secure-module probe `0x80010D14` fails, or reports bit 0 of `0x8002C720` already set | `0x80010E28` (query failed), `0x80010E2E` (flag already set) | `6` | **partition (keys)** |
| 6 | SMC `0x32`, `0x31` or the second `0x33`+`0x2F` digest fails | `0x80010E48`, `0x80010E52`, `0x80010E7C` | `6` | **partition** |
| 6b | the second `0x31`/`0x32` block's `0x80010966` returns non-zero | `0x80010EA8` | `6` | **partition** |
| 7 | geometry: device not state 1, not RPMB-capable, or arg error | `0x80011310 cbnz` | `6` | **device** |
| 8 | `nblocks > 1024` | `0x80011322 bhi` | `6` | **store (size)** |
| 9 | **any one block read fails** (`0x80010A34` ≠ 0) | `0x800111DA cbnz` → `0x80011214` → `0x80011328 cbz` | `6` | **block** |
| 10 | **any one block's record layout overruns 224 B** (`free[blk] < off+24+len`) | `0x80011200 bcc` → `0x80011214` | `6` | **block (layout)** |
| 11 | catalogue would exceed 1024 entries | `0x8001120A bcc` not taken → `0x80011214` | `6` | **store (count)** |

All eleven converge on one instruction, `0x8001132A: 2006 movs r0,#6`, and boot
main then executes `0x80014940: f000 f9aa bl 0x80014c98` with `r0 = 0`
(level 0) and `r1 = 0x5B`. Per report 153 §4.1, **level 0 never returns**, so
`0x80014944` is never reached from this path.

### 1.5 What the mount does **not** check — the load-bearing negative

[P] The mount:
* never calls `0x8001112C` (`find`) or `0x80011690` (`record_read`);
* never compares any name against a literal (the only `memcmp`s on the record
  path are inside `0x80011166`, which compares a *word* against the magic, and
  inside `0x80010A34`, which compares RPMB nonces/MACs);
* never inspects a record's payload or its value;
* has no notion of `BOOTCTR_FCTRY`, `TEST_PAD`, or any other name. The string
  `"BOOTCTR_FCTRY"` occurs twice in the image (`0x80009148`, used by the
  selector and the provision helper) and is **not** referenced by the mount.

**Therefore: a block that reads cleanly and whose record headers tile within
224 bytes always mounts, whatever it contains.** The mount is a *structural*
gate over the whole store, not a *semantic* gate over any record.

---

## 2. Q2 — the whole record-store init chain, failure by failure

### 2.1 The chain, in program order

`0x80014904` → the selector is exactly **eleven** steps, of which five are
"record-store" steps and six are boot-policy steps that run *between* them.
Every one is listed with the **exact** value that makes it fail.

```
0x80014910  WDT2 kick                                   (cannot fail)
0x8001492A  A. 0x80010D3C   secure-store / RPMB session probe   -> 0/1   [SITE 2 if 0]
0x80014930  B. 0x80010EB8   provisioning probe                  -> r7
0x80014936  C. 0x800112DC   MOUNT                               -> 0/6   [SITE 1 if 6]
0x8001494E  D. 0x800090A0(1) provision BOOTCTR_FCTRY = 0x32     -> 0/1   [SITE 2 if 0]
0x8001495E  E. selector #1                              [SITE 3 fires inside, if status-class]
0x80014980     media read 0x2040 (boot-state word)
0x80014998     state 1/3 -> 0x80014F84                  [0x37 blink inside if 0x2801 read fails]
0x800149B4     media write 0x2040 (state+1)
0x800149BE  F. selector #2                              [SITE 3 inside if status-class; result != 0 -> nuke]
0x80014A16     0x800156E8 "Nuking..." if any of the above diverted
0x800149CC / 0x80014A2C   region load (unverified / verified)
0x8001415E    G. selector #3 (inside 0x80014148)        [SITE 3 inside]
0x800142B0    H. selector #4 (inside 0x800142A8)        [SITE 3 inside]
0x80014A3E  I. selector #5                              [SITE 3 inside if status-class]
0x80014A4A  J. 0x8000911E   decrement + re-seal + write  -> 0/1   [SITE 4 if 0]
0x80014A62     SMC 0x3A
```

Step **A** (`0x80010D3C`) in full, because its polarity is the opposite of what
report 150 §1.7 implies [P]:

```
80010d3c  49f2        ldr  r1,=0x80025f00
80010d3e  2000        movs r0,#0
80010d40  b510        push {r4,lr}
80010d42  f006 ef7a   blx  0x80017c38      ; SMC 0x31 (domain 5, 1 arg): query  id = 0
80010d46  b998        cbnz r0,0x80010d70   ; wrapper returns 1 = SMC ok -> return 1
80010d48  ...         bl 0x80009bf4        ; log(1,4, &KEK+0xA0, 32)
80010d56  48ec        ldr  r0,=0x80025f00
80010d58  2110        movs r1,#16
80010d5c  f006 efaa   blx  0x80017cb4      ; SMC 0x32 (domain 5, 2 args): provision 16 B
80010d60  b120        cbz  r0,0x80010d6c   ; 0 == SMC failed -> return 0
80010d62  ...         blx 0x80017c38       ; SMC 0x31 again, id = 1 (close)
80010d6a  b908        cbnz r0,0x80010d70   ; ok -> return 1
80010d6c  2000        movs r0,#0           ; ***** return 0 = NOT AVAILABLE *****
80010d70  ...         ; zero 16 B at 0x8002C700
80010d7e  2001        movs r0,#1           ; ***** return 1 = AVAILABLE *****
```

Wrapper polarity [P]:
`0x80017C38` ends `80017C5C: e3500000 cmp r0,#0 / 0a000001 beq 0x80017c6c /
e3a00000 mov r0,#0 / pop` — i.e. **SMC r0 == 0 ⇒ wrapper returns 1**
(success). `0x80017CB4` ends `80017CDC: e2700001 rsbs r0,r0,#1 /
33a00000 movcc r0,#0` — same polarity. And the `id == 0` branch of
`0x80017C38` post-checks a register block at `0x4A30C000`
(`[+0x100] & 4`, `[+0x104] == 0x7FF`, from `0x80017CF0 = 0xFFE00000` with
`cmp r2,r1,lsl #21`).

So **A returns 0 only when the "already open" query fails *and* the `0x32`
provisioning step cannot fix it** — i.e. "the secure/RPMB store cannot be brought
into a usable state". That is precisely the condition that fires **SITE 2** at
`0x80014958`, and it fires **before the mount is ever attempted**.

Step **B** (`0x80010EB8`) is the **RPMB key-provisioning routine**, and it is
worth naming precisely because `r7` decides whether the next step *writes*
[P]:

```
80010ec8  2001        movs r0,#1
80010ecc  f7fc f9e6   bl   0x8000d29c        ; stub: (1,&l)
80010ed6  f7fc f999   bl   0x8000d20c        ; open the block device
80010eda  1c40        adds r0,r0,#1
80010edc  d038        beq  0x80010f50        ; -> return 0
80010ee0  f7ff ff18   bl   0x80010d14        ; secure-module probe
80010ee4  b3a0        cbz  r0,0x80010f50     ; -> return 0
80010ee6  9800        ldr  r0,[sp,#0]
80010ee8  bb90        cbnz r0,0x80010f50     ; flag already set -> return 0
80010eee  f7ff fd2a   bl   0x80010946        ; ***** read the write counter *****
80010ef2  1c40        adds r0,r0,#1
80010ef4  d02c        beq  0x80010f50        ; transport error -> return 0
80010ef6  9800        ldr  r0,[sp,#0]
80010ef8  bb50        cbnz r0,0x80010f50     ; ***** counter ok => KEY ALREADY PROGRAMMED => return 0 *****
80010f04  2801        cmp  r0,#1
80010f06  d123        bne  0x80010f50        ; store header not acceptable -> return 0
80010f0e  f7ff fce3   bl   0x800108d8        ; ***** RPMB frame type 1 : PROGRAM KEY *****
80010f1e  b9bc        cbnz r4,0x80010f50     ; program-key failed -> return 0
80010f26  d015        beq  0x80010f54        ; [0x80025F00]==1 -> return 1
80010f2a  f006 eb6c   blx  0x80017604        ; SMC 0x0F, domain 0
80010f4a  f007 e846   blx  0x80017fd8        ; SMC 0x28/0x29/0x2A/0x0F
80010f4e  b908        cbnz r0,0x80010f54     ; -> return 1
80010f50  2000        movs r0,#0            ; ***** return 0 = "nothing to do" *****
80010f54  2001        movs r0,#1            ; ***** return 1 = "was provisioned" *****
```

`0x80010946` is the discriminator: it reads the write counter and, if the RPMB
**result code is 7** ("authentication key not yet programmed"), returns
`*out = 0`, else `*out = 1` (`0x80010958 2807 cmp r0,#7 / 8001095e 2001 movs
r0,#1`). So:

* **`r7 = 0`** — the key is already programmed (or the probe could not run): the
  boot will **not** write anything before the mount;
* **`r7 = 1`** — the key was just programmed at `0x80010F0E` (`0x800108D8`,
  frame type 1), and boot main will print `"RPMB not provisioned"` and call
  **D**, which *creates* `BOOTCTR_FCTRY = 0x32` — **an RPMB write on this boot,
  before any region verification.**

That is a state worth knowing before arming a unit: on a fully-provisioned store
the next boot performs exactly **one** write (the decrement); on a
key-not-programmed store it performs **two** (the `0x32` creation and, later,
the decrement).

Step **D** (`0x800090A0(1)`) [P]:

```
800090a0  b508        push {r3,lr}
800090a2  b930        cbnz r0,0x800090b2    ; r0 != 0 -> the write path
800090a4  ...         bl 0x8001167e        ; (r0 == 0) record_len/delete path
800090b2  2801        cmp  r0,#1
800090b4  d109        bne  0x800090ca      ; only mode 1 writes
800090b6  2032        movs r0,#50          ; value = 0x32
800090ba  2204        movs r2,#4
800090be  a022        adr  r0,"BOOTCTR_FCTRY"
800090c0  f008 fa91   bl   0x800115e6      ; record_write(name, &50, 4)
800090c4  b908        cbnz r0,0x800090ca  ; non-zero -> return 0
800090c6  2001        movs r0,#1          ; success
800090ca  2000        movs r0,#0          ; failure
```

Note the value it writes: **`0x32` (50)**. This is the "re-arm the factory
budget" operation. *Correction to report 150 §1.7's label:* the `r0 == 0` branch
calls `0x8001167E`, which is **not** `record_len` — it is
`find(name) ? 0x8001141E(entry) : 3`, and `0x8001141E` **deletes** the record
(marks `slot = 0xFFFF` at `0x8001144E`, repacks the block, rewrites it through
`0x80010B2E`, and drops the catalogue entry at `0x800114CA`–`0x800114F2`). So
`0x800090A0(0)` = "erase the counter", `0x800090A0(1)` = "set the counter to
`0x32`". Boot main only ever calls mode 1.

### 2.2 Failure table — step, error code, and which `0x5B` site

| # | step | test (address) | failing value | what the unit emits | `0x5B` site |
|---:|---|---|---|---|---|
| A | secure-store probe `0x80010D3C` | `0x8001492E cbz` | returns **0** | `blink(0,0x5B)` at `0x80014958` | **2** |
| B | key provisioning `0x80010EB8` | (result kept in `r7`) | — never blinks itself — | `r7 = 1` ⇒ step D will **write** `0x32` | — (drives D) |
| C | mount `0x800112DC` | `0x8001493A cbz` | returns **6** | `blink(0,0x5B)` at `0x80014940` | **1** |
| D | provision `0x800090A0(1)` | `0x80014952 cbnz` | returns **0** | `blink(0,0x5B)` at `0x80014958` | **2** |
| E | selector #1 `0x8001495E` | inside `0x800090CE` | `record_read` = **2/5/6**, or `stored_len != 4` | `blink(0,0x5B)` at `0x800090F6` | **3** |
| E' | selector #1 | `0x80009116` | value **0** or **>0x32**, len == 4 | **nothing** — returns 2 silently | — |
| — | boot-state read `0x80014980` | (return value **not checked**) | read failure leaves `[sp+8] = 2` | none | — |
| — | state 1/3 pre-check `0x80014F84` | `0x80014FB6 cmp r4,#0x70` | `0x2801` read **< 112 B** | `blink(0,0x37)` at `0x80014FBE` | no (`0x37`) |
| — | boot-state write `0x800149B4` | `0x800149B8 cbnz` | write fails | divert to `0x80014A16` (nuke) | no |
| F | selector #2 `0x800149BE` | `0x800149C2 cbnz` | any non-zero | `0x5B` **only if status-class**; the 1/2 themselves divert to `0x80014A16` (nuke) | **3** (or none) |
| G | selector #3 `0x8001415E` | (result → `r6`) | status-class | `blink(0,0x5B)` | **3** |
| G' | selector #3 | `0x80014290` | value-class (`r6 == 2`) | status **`0x0F`**; *no blink here* | — |
| H | selector #4 `0x800142B0` | `0x800142BA cbz` | 0 → read `NvOSRegionStruct`; ≠0 → straight to `0x80014148` | as G | **3** |
| I | selector #5 `0x80014A3E` | `0x80014A44 cbz` | 0 → skip the decrement entirely (no error!) | — | — |
| J | decrement `0x8000911E` | `0x80014A4E cbnz` | returns **0** | `"\nBootModeCount expended\n"` then `blink(0,0x5B)` at `0x80014A5A` | **4** |
| J' | inside J, the RPMB **write** | `0x800115D8 cbz` / `0x80011410 cbz` | write fails | **`blink(0,0x2F)` and never returns** | **none** |

Two structural facts that the table makes obvious:

1. **`0x80014A5E bl 0x800094BE` is dead code on every `0x5B` path.** All four
   sites pass level `r0 = 0`, and `0x80014C98` cannot return with `sl == 0`
   (`0x80014D64: cbz r5,0x80014d70` skips the only abort source). Report 153 §4.1
   established this; §4.4 below shows it applies to site 4 as well.
2. **`0x80014A5A` (site 4) is reached by a *value* defect or by a *silently
   refused* rewrite — never by a *failed* write.** A failed write lights
   **`0x2F`** one layer deeper (`0x800115DE` on the create path, `0x80011416` on
   the rewrite path), and because both pass `r0 = 0` they never return —
   `0x800114FA`'s "returns 0 either way" in report 150 §2.3 is wrong: it never
   returns at all. What *does* reach site 4 is `record_write` returning **1**
   ("no room" / "table full", `0x80011632` / `0x80011658` → `0x80011664`), which
   `0x8000911E` treats as failure without any write having been attempted.

### 2.3 The five selector evaluations, and why "value" and "length" land in different places

`0x800090CE` has exactly **one** caller-list of five, found by a full `bl`
decode of the 1 MiB [P]:

| # | call site | context | reached when |
|---:|---|---|---|
| 1 | `0x8001495E` | boot main, after mount + provision | always |
| 2 | `0x800149BE` | boot main, after the boot-state write | "advance" arm (state 2, 4..8) |
| 3 | `0x8001415E` | `0x80014148`, the per-region load+verify worker | every region load |
| 4 | `0x800142B0` | `0x800142A8`, the region driver | every region load |
| 5 | `0x80014A3E` | boot main, gating the decrement | only on the `return 1` arm of the verified load |

*(Report 150 §1.7 lists three of these and attributes #4 to `0x80014254`/`0x80014260`;
those two addresses are branch points inside `0x80014148`, not selector calls.
Report 151 §2.4 makes the same slip. The call is at `0x8001415E`.)*

The selector's return is a **three-way** classification, and only two of the
three sub-cases log `0x5B` [P]:

```
800090e4  2803        cmp  r0,#3
800090e6  d009        beq  0x800090fc      ; status 3 (absent) -> return 0
800090e8  b918        cbnz r0,0x800090f2    ; status 2/5/6      -> ***** 0x5B *****
800090ea  f8bd 0004   ldrh.w r0,[sp,#4]    ; stored length
800090ee  2804        cmp  r0,#4
800090f0  d006        beq  0x80009100      ; len == 4 -> check the value
800090f2  215b        movs r1,#91          ; ***** SITE 3 *****
800090f4  2000        movs r0,#0
800090f6  f00b fdcf   bl   0x80014c98      ; blink(0,0x5B)
800090fa  e00c        b    0x80009116      ; -> return 2
80009100  b11c        cbz  r4,0x8000910a
80009102  9800        ldr  r0,[sp,#0]
80009104  2832        cmp  r0,#50
80009106  d800        bhi.n 0x8000910a
80009108  6020        str  r0,[r4,#0]      ; *out = value (0..0x32)
8000910a  9800        ldr  r0,[sp,#0]
8000910c  b118        cbz  r0,0x80009116    ; value == 0  -> return 2  (NO 0x5B)
8000910e  2832        cmp  r0,#50
80009110  d801        bhi.n 0x80009116     ; value >0x32 -> return 2  (NO 0x5B)
80009112  2001        movs r0,#1           ; 1..0x32 -> return 1
```

**This is the single most important branch in the whole report:**

| record state | selector returns | logs `0x5B`? | where the boot dies |
|---|---|---|---|
| name not in the catalogue at all | `0` | no | not at all — product path |
| read I/O error (`0x80010A34` ≠ 0) | `2` | **yes, here** | `0x800090F6` |
| re-read name mismatch (`memcmp` ≠ 0) | `2` | **yes, here** | `0x800090F6` |
| `rec->len > 4` | `2` | **yes, here** | `0x800090F6` |
| `len != 4` (1,2,3,…) | `2` | **yes, here** | `0x800090F6` |
| `len == 4`, value `0` | `2` | **no** | much later, `0x80014A5A` (if the arm reaches it) |
| `len == 4`, value `> 0x32` | `2` | **no** | much later, `0x80014A5A` (if the arm reaches it) |
| `len == 4`, `1 ≤ value ≤ 0x32` | `1` | no | — factory path |

So **"the record is malformed as a *record*" and "the record's *value* is out of
range" are two different failure modes in this code**, separated by whether the
store-level read succeeded. `0x5B` at `0x800090F6` says *the store could not give
me a well-formed 4-byte record*; `0x5B` at `0x80014A5A` says *the store gave me a
well-formed record whose value I refuse*.

### 2.4 The four scenarios named in the brief

**(a) Write-counter mismatch.** The RPMB write counter is read *only* on the
write path: `0x80010B2E` calls `0x80010844` (frame type 2) at `0x80010B4A`, and
the return value is **discarded** —

```
80010b46  4651        mov  r1,sl
80010b48  a801        add  r0,sp,#4
80010b4a  f7ff fe7b   bl   0x80010844      ; get write counter  (returns -1 on failure)
80010b6e  9801        ldr  r0,[sp,#4]      ; counter (written only on success, 0x800108D0)
80010b72  f8c4 01f4   str.w r0,[r4,#500]   ; frame[0x1F4] = counter
```

A read (`0x80010A34`) uses frame type 4 and **no counter at all**. So:
* a counter mismatch or a counter-read failure does **not** affect the mount,
  `record_read`, or the selector — the store still reads perfectly;
* it makes the *card* reject the type-3 write, so the type-5 result field is
  non-zero (`0x80010BF6 ldrh.w r0,[r4,#508] / cmp r0,#0`), `0x80010B2E`
  returns `-1`, and the caller blinks **`0x2F`** and hangs;
* **it never produces `0x5B`.** Predicted pattern from report 153 §3.4:
  `0x2F = 101111` ⇒ groups **1-4**.

**(b) Authentication failure.** Every block read verifies four things before the
payload is accepted [P]:

| order | test | code |
|---:|---|---|
| 1 | `[frame+0x1FC]` (RPMB result) == 0 | `0x80010AA8 ldrh.w r0,[r4,#508]; bbe0 cbnz r0,0x80010b28` |
| 2 | returned nonce == the 16-byte nonce we sent | `0x80010AB4 f008 f844 bl 0x80018b40` |
| 3 | `[frame+0x1FC]` still 0 | `0x80010ABA` |
| 4 | the 32-byte MAC at `frame+0xC4` == `frame+0x1C4` | `0x80010AE0 cbnz r0,0x80010b28` |
| 5 | the monitor unseal (`0x80011048`) succeeds | `0x80010B20 b110 cbz r0,0x80010b28` |

Any of them returning non-zero goes to `0x80010B28 → 0x80010B2A: mov r0,r6`
with `r6 = -1` (`0x80010A3E: 1e6e subs r6,r5,#1`, `r5 = 0`). That `-1`
propagates: `0x800111D6 bl 0x80010A34` → `0x800111DA cbnz r0,0x80011214`
→ **`0x80011196` returns 0** → mount returns 6 → **`0x5B` site 1**. It is a
**block-level authentication failure with a store-level consequence**: one block
failing to authenticate kills the whole catalogue, hence the whole boot, before
any region is looked at.

**(c) Damaged block.** Three distinct sub-cases, three different outcomes:

| damage | detected where | outcome |
|---|---|---|
| frame MAC invalid / result non-zero | `0x80010A34` (table above) | **`0x5B` site 1** |
| frame valid, payload garbage **without** the magic word | `0x80011166` returns 0 | block contributes **no records**; mount **succeeds**; the boot continues. Nothing in the image notices. |
| frame valid, payload contains `0x5983BC2D` at a 4-aligned offset | `0x80011166` returns 1 | parsed as a record; its `len` (byte `+22` of the "header") is used at `0x800111F8`; if `off+24+len > free[blk]` ⇒ **`0x5B` site 1**; else a phantom catalogue entry exists |
| frame valid, payload contains a *stale copy* of a real record header | `0x80011166` | **two catalogue entries with the same name**; `0x8001112C` (`find`) returns the **first**, i.e. the lowest block index, then the lowest offset — see §3.4 |

**(d) Unreadable RPMB partition.** Two different sites depending on *which*
layer notices:

| what is broken | first failing test | site |
|---|---|---|
| SMC `0x31`/`0x32` (key/session) cannot be established | `0x80010D46`/`0x80010D60` → `0x80010D3C` returns 0 → `0x8001492E` | **2** (mount never runs) |
| keys are fine but the block device cannot be opened | `0x800112F8` | **1** |
| device opens, but is not flagged RPMB-capable (`[+0x1BC]`/`[+0x1E0]`) or geometry fails | `0x80010D98` / `0x80011310` | **1** |
| device and geometry fine, but *every* block read fails (e.g. partition erased) | `0x800111DA` on block 0 | **1** |
| device fine, `nblocks` absurd (>1024) | `0x80011322` | **1** |

**Summary for Q2:** of the four named scenarios, only **(b) authentication
failure** and **(d) unreadable partition** reach `0x5B`, and they reach it at
sites **1** or **2**. **(a) a counter mismatch cannot reach `0x5B` at all** — it
produces `0x2F`. **(c) a damaged block** reaches `0x5B` site 1 only if the
damage breaks the frame or the 224-byte layout; a semantically meaningless but
well-framed block is invisible to the mount.

---

## 3. Q3 — the record parse, in detail

### 3.1 Two different structures — this is where the `0x1C` comes from

The brief's "0x1C-byte header, `next` field, `offset` field" does not exist as
such. There are **two** structures, and only one of them has an `offset`:

**(i) The on-medium record header — 24 bytes (`0x18`).** No `next`, no offset,
no name length, no checksum [P from `0x800115B6` (`movs r2,#24` copy into
staging) and `0x8001117E` (`movs r2,#24` copy out)]:

```
+0x00  u32  magic  0x5983BC2D
+0x04  char name[14]        (NUL padded; compared with all 14 bytes)
+0x12  u16  slot            (the block index this record lives in)
+0x14  u16  slot (duplicate — written by every writer, never read)
+0x16  u16  len             (payload length, 0..200)
+0x18  ...  payload[len]    (starts 24 bytes after the header)
```

**(ii) The RAM catalogue entry — 28 bytes (`0x1C`)**, 1024 of them at
`0x8002C760` [P]. Bytes `+0x00..+0x17` are a **verbatim copy of the on-medium
header**; the last four bytes are **computed at mount and never stored on
media**:

```
+0x00..+0x17   copy of the on-medium 24-byte header
+0x18  u16  offset   = byte offset of the header inside the 224-byte block payload
```

So the `offset` is a *parse artefact*, and the `0x1C` is the RAM entry, not the
on-medium header. There is **no `next` pointer anywhere** in either structure:
record chaining is implicit ("scan forward for the next magic word").

### 3.2 How a record is located: `0x80011166` [P]

```
80011166  b570        push {r4,r5,r6,lr}
80011168  461d        mov  r5,r3                  ; &out_offset
8001116a  4bf3        ldr  r3,=0x5983BC2D         ; (pool 0x80011538)
8001116c  2400        movs r4,#0
8001116e  e00d        b    0x8001118c
80011170  f851 6024   ldr.w r6,[r1,r4,lsl #2]     ; word at buf + 4*i
80011174  429e        cmp  r6,r3
80011176  d108        bne  0x8001118a
80011178  2218        movs r2,#24
8001117a  eb01 0184   add.w r1,r1,r4,lsl #2
8001117e  f007 ee44   blx  0x80018e08           ; memmove(entry, buf+4*i, 24)
80011182  00a0        lsls r0,r4,#2
80011184  6028        str  r0,[r5,#0]             ; *out = 4*i
80011186  2001        movs r0,#1
80011188  bd70        pop  {r4,r5,r6,pc}
8001118a  1c64        adds r4,r4,#1
8001118c  ebb4 0f92   cmp.w r4,r2,lsr #2           ; i < floor(len/4)
80011190  d3ee        bcc  0x80011170
80011192  2000        movs r0,#0
80011194  bd70        pop  {r4,r5,r6,pc}
```

Three consequences [P]:
* the magic must be **4-byte aligned relative to the current cursor**, and every
  record's payload length must therefore keep the cursor word-aligned in
  practice (`24 + len` ≡ 0 mod 4 ⇔ `len` ≡ 0 mod 4 — which is exactly what
  `record_write` enforces at `0x800115F8: lsls r1,r4,#30 / beq`);
* the scan never reads past `buf + len` **as a word**, but the 24-byte
  `memmove` can read up to **20 bytes past** the end of the region if the magic
  is at the last whole word. Those bytes are the stale contents of the 512-byte
  staging buffer at `0x80033F80` — stale *DRAM*, not a fault (report 150 §3.1);
* only the **first** match is returned; the caller loops.

### 3.3 The mount loop: `0x80011196` [P]

```
80011196  e92d 5ffc   push {r2,r3,r4..ip,lr}
8001119a  mov.w sl,#0                    ; (unused block counter)
8001119e  ldr  r1,=0x80025FB0            ; store state
800111a0  mov  r5,sl                     ; r5 = record count
800111a2  ldr.w r8,=0x80033F80           ; staging
800111a6  mov  r4,sl                     ; r4 = block index
800111a8  ldr.w fp,=0x8002C760           ; catalogue
800111b0  sub.w r9,r8,#2048              ; 0x80033780 free-space table
800111b4  str.w sl,[r1,#4]               ; state->count = 0
800111b8  b    0x8001123e
; ---- per block ----
800111ba  movs r0,#224
800111be  strh.w r0,[r9,r4,lsl #1]       ; free[blk] = 224
800111c4  mov  r0,r8 ; blx 0x80018d74    ; memset(staging, 224)
800111d0  ldr  r2,=0x80033760
800111d4  ldr  r3,[r6,#0]                ; block_base
800111d6  bl   0x80010a34                ; ***** authenticated RPMB READ of block r4 *****
800111da  cbnz r0,0x80011214             ; ***** read failed => return 0 *****
; ---- per record within the block ----
8001121a  rsv  r1 = 28*count
80011220  ldrh.w r2,[r9,r4,lsl #1]       ; free[blk]  (remaining)
80011224  r0 = catalogue + 28*count      ; &entry[count]
8001122a  mov  r1,r6                     ; cursor
8001122c  bl   0x80011166                ; scan for the magic
80011230  cmp  r0,#0 ; bne 0x800111e2    ; not found -> next block
800111e2  ldr  r1,[sp,#4]                ; the offset the scan returned
800111ea  sub.w r0, r6+off, r8           ; entry->offset = absolute staging offset
800111ee  strh r0,[r7,#24]               ; (u16, but <= 224 in practice)
800111f0  ldrh r3,[r7,#22]               ; entry->len
800111f6  r6 = r6 + off + 24 + len       ; advance the cursor
800111f8  adds r2,r3                     ; need = off + 24 + len
800111fe  cmp  r0,r2                     ; free[blk] >= need ?
80011200  bcc  0x80011214                ; ***** NO => return 0 (mount fails) *****
80011204  r0 = r0 - (off+len) - 24 ; strh free[blk] = r0
80011206  adds r5,r5,#1                  ; count++
8001120a  cmp.w r5,#1024 ; bcc 0x8001121a ; more records allowed
80011214  movs r0,#0                     ; ***** return 0 *****
```

Structural invariants [P]:
* `cursor + free[blk] == staging + 224` after every record, so **the scan is
  bounded by the true end of the 224-byte payload**. No field in a record can
  make the parser read outside the block.
* the space test is `free[blk] >= off + 24 + len`, evaluated **after** the
  header has been copied into the catalogue and `entry->offset` has been
  written, but **before** the record is counted.
* the loop **terminates on the first failing block**, not on the last.

### 3.4 The four edge cases the brief asks about

#### (a) More than one record in a block (our dead unit's `BOOTCTR_FCTRY` + `TEST_PAD`)

**Supported, and normal** [P]. The loop at `0x8001121A` re-scans from the
advanced cursor, so a block is simply a packed list. The only constraint is the
224-byte budget:

```
24 + len(a) + 24 + len(b) + ... + gaps  <=  224
```

For two contiguous records that is `len(a) + len(b) <= 176`. For a
`BOOTCTR_FCTRY` of length 4 with a co-resident record of length `L`, the
condition is **`L <= 172`**. If `L > 172` the mount fails at `0x80011200` and the
unit blinks **`0x5B` at site 1** on every subsequent boot, permanently, because
the block is on media.

> **This is the single most plausible mechanism for the dead unit**, and it is
> testable without touching a device: read block 2 raw and add up
> `24 + len` over the records that the scan would find. See §5.
> **[I]** — the block's bytes are not in this corpus, so the specific lengths
> cannot be checked here.

#### (b) A record whose `next` points at its own block

**Not expressible** — there is no `next` field (§3.1) [P].

The nearest thing that *is* expressible, and is dangerous, is the **`slot`
field at `+0x12`**. `0x80011166` copies it from the medium into the catalogue
entry, and nothing on the mount path checks that it equals the block the record
was actually found in. `record_read` then **uses it to choose the block to
read** [P]:

```
800116b4  8a63        ldrh r3,[r4,#18]     ; rec->slot  ← came from the MEDIUM
800116ba  9300        str  r3,[sp,#0]      ; arg5 = block number
800116c2  f7ff f9b7   bl   0x80010a34      ; authenticated read of THAT block
800116d8  1d29        adds r1,r5,#4
800116da  f007 fa6b   bl   0x80018bb4      ; memcmp(rec->name, staging+off+4, 14)
800116e0  2005        movs r0,#5           ; mismatch -> status 5
```

So a record whose `slot` says "block 5" but which physically lives in block 2
produces: mount **succeeds**; `record_read` reads block 5; the name at
`offset` in block 5 does not match; status **5**; selector **2**; **`0x5B` at
site 3**, on the very next `record_read`, and for ever.

*(This is a real hazard our own tooling can create: a hand-built frame must set
`+0x12` to the target block number. A frame replayed from a different block will
not.)* **[P] for the mechanism, [I] for its applicability to the dead unit.**

#### (c) Overlapping lengths

Lengths are only used to *advance* the cursor and to *bound the copy*; there is
no overlap check and no record map. So:

| state | detected? | outcome |
|---|---|---|
| `len` runs past the end of the block | **yes**, `0x80011200` | mount fails ⇒ **`0x5B` site 1** |
| `len` runs past the *next* record header (the next header lives inside the previous payload) | **no** | that next header is **never seen** — the cursor jumps over it. The record silently disappears from the catalogue. |
| two records claim the same extent | **no** | both are entered; the second is found only if its magic is reachable from the first record's end |
| `len` is odd / not a multiple of 4 | **no** at mount | the next scan starts at a non-word-aligned cursor; `ldr.w` on Cortex-A9 with `SCTLR.A = 0` is a legal unaligned access (report 150 §3.2), so it either works or, if `SCTLR.A` is 1, takes a Data Abort **[U]** |

#### (d) A non-zero tail

The bytes between the end of the last record and byte 224 are **skipped, not
checked**. They may be anything, including `0xFF` from an erased frame or
leftovers from a previous, longer record set. Two outcomes [P]:

* **no magic word in the tail** → the scan returns 0, the block contributes its
  records and nothing more, mount succeeds. **Nothing in the image notices.** A
  "dirty" block is not an error.
* **the magic word appears in the tail at a 4-aligned offset** → the tail is
  parsed as a record. Its `len` is read from tail-byte `+22`, which is arbitrary
  data. If `len` is large the space check fails ⇒ **`0x5B` site 1**; if it is
  small, a **phantom catalogue entry** appears whose name is arbitrary. A
  phantom with a name equal to a real one shadows or is shadowed by it (§3.5).

There is **no block checksum, no "used length" field, no generation counter and
no zero-terminator** anywhere in the 224-byte payload [P]. The block's record
set is defined purely by where the magic words are.

### 3.5 Duplicate names

`0x8001112C` (`find`) is a linear scan of the catalogue and returns the
**first** entry whose 14-byte name matches [P]:

```
8001112c  ...  r5 = 0x8002C760 (catalogue) ; r6 = &store_state
8001113a  rsv  r0, r4, r4, lsl #3            ; 28*n
80011142  add.w r0,r5,r0,lsl #2
80011146  adds r0,r0,#4
80011148  f007 fd34  bl 0x80018bb4           ; memcmp(entry->name, name, 14)
8001114c  b928  cbnz r0, 0x8001115a          ; no match -> next
8001114e  ...   return entry
8001115c  ldr  r0,[r6,#4] ; cmp r4,r0 ; bcc loop
```

Catalogue order is **block index ascending, then record offset ascending**
(because `0x80011196` walks blocks in order and each block's records in scan
order). Therefore:

* a **second copy of `BOOTCTR_FCTRY` in a lower-numbered block shadows the one in
  block 2** — the boot will read and decrement the wrong one;
* a second copy in a **higher-numbered** block is shadowed *by* block 2;
* `record_write`'s "does it exist?" test is the same `find`, so an update
  rewrites the **first** copy and leaves the other intact.

This is worth stating plainly: **duplicate names are not an error condition in
this store.** They are silently resolved by position. [P]

### 3.6 What the parse can and cannot fail on — summary

| condition | mount result | reason |
|---|---|---|
| block read fails (auth/result/nonce/MAC) | **fail (6)** | `0x800111DA` |
| a record's `off+24+len` > remaining free | **fail (6)** | `0x80011200` |
| > 1024 records in the whole store | **fail (6)** | `0x8001120A` fall-through |
| bad name, wrong value, duplicate name | **succeeds** | not examined |
| garbage tail without the magic | **succeeds** | skipped |
| `len` not a multiple of 4 | **succeeds** | only `record_write` cares |
| `slot` field disagreeing with the real block | **succeeds** | not checked (fails later, site 3) |
| record with `len` > 200 | cannot exist in a mounted store | the space check rejects it |
| non-zero tail containing the magic | **may fail** | becomes a record; layout decides |

---

## 4. Q4 — could one record write cause `0x5B`? *(the central question)*

### 4.1 The blast radius of one write, from the code

Every RPMB write in this stage is a **single-block authenticated write** [P]:

```
80010b64  2001        movs r0,#1
80010b6a  f8a4 01fa   strh.w r0,[r4,#506]      ; frame[0x1FA] = block count = 1
80010bb6  f7ff fe2a   bl   0x8001080e          ; send (type 3)
```

and it is preceded in the same call by a fresh counter read (`0x80010B4A`) and a
monitor seal (`0x80010F58`: SMC `0x31`, `0x33`+`0x2F`, `0x33`+`0x2E`, `0x31`).
So one write **touches exactly one 256-byte RPMB block and nothing else**. Any
subsequent failure is therefore a *consequence of the parse of that one block*,
not further media damage. That bounds the whole analysis: **one write, one
block, one record set.**

### 4.2 Which sites a single re-framed block 2 can subsequently reach

Assume the write succeeded (its frame MAC verified and the card accepted it) —
our case. On the next boot:

| site | reachable from a block-2 rewrite? | exact condition |
|---|---|---|
| **2** `0x80014958` | **No** | it runs *before* the mount and before any record is read; it tests the SMC `0x31`/`0x32` key state and the `0x32`-provision write. A data-block write cannot change that state. |
| **1** `0x80014940` | **Yes — but only via the block's *layout* or the block's *readability*, never via the record's meaning** | (i) the block fails to read/authenticate (`0x800111DA`); or (ii) the block's record set overruns 224 B (`0x80011200`); or (iii) the block adds enough records to push the store past 1024 (`0x8001120A`). |
| **3** `0x800090F6` | **Yes** | the block reads and parses, but `record_read("BOOTCTR_FCTRY")` disagrees with it: `rec->len > 4`, or `len != 4`, or `record_read`'s re-read name check fails, or the read of `rec->slot` fails. |
| **4** `0x80014A5A` | **Yes** | the block reads and parses, the record is `len == 4`, and its **value** is `0` or `> 0x32`, **and** the boot reaches the counter arm (see 4.4). |

Worked through in order:

* **Mount.** The mount is content-blind (§1.5). A block whose bytes are a
  well-formed tiling of `{24-byte header, payload}` with the magic
  `0x5983BC2D` and total extent ≤ 224 always mounts. So *the value we wrote, the
  name we wrote, and the `slot` field we wrote are all invisible to the mount.*
  What the mount cares about is only: does the block read, and does
  `Σ(24 + len) + gaps ≤ 224`.
* **Provision (step D).** Only runs if the RPMB key-state probe says
  "not provisioned"; unaffected by a data-block write.
* **Selector.** Now the record is read. `record_read` **re-reads the block from
  the card** (`0x800116C2`), locates the payload at the *mount-time* offset, and
  re-checks the 14-byte name (`0x800116DA`). Three ways for this to go wrong
  that the *mount* would not have caught:
  1. `rec->len != 4` (the record is a different size than the reader demands)
     → status 2 → **site 3**;
  2. `rec->slot` (+0x12 of the on-medium header) is not the block the record is
     physically in → a *different* block is read → name mismatch → status 5 →
     **site 3**;
  3. `rec->len == 4` but the value is out of `1..0x32` → returns 2 **silently**
     → **site 4** (if the arm is reached).

### 4.3 "The record is semantically wrong" vs "the store became unusable"

These are genuinely different in the code, and the boundary is precise:

| | "the record is wrong" | "the store is unusable" |
|---|---|---|
| **where it is detected** | in `0x80011690`/`0x800090CE`, *after* the mount succeeded | in `0x800112DC`/`0x80010D3C`, *before* any record is examined |
| **what fails** | the *interpretation* of one record | the *catalogue* / the *device* |
| **how many records are affected** | one name | all of them — the catalogue is empty or absent |
| **what the boot has already done** | printed the banner; possibly advanced the boot-state word; for site 4, run the full region verify and written media `0x2074` (0xC1 B) and `0x2819` (via `0x80013F64`, §6 #7) | nothing but the banner and (site 1) a full sweep of RPMB reads |
| **can the value be "fine" and the store still fail?** | — | **yes**: a bad *layout* in one block fails the mount even if `BOOTCTR_FCTRY` is byte-perfect |
| **can the record be "bad" and the store still work?** | **yes**: that is exactly sites 3 and 4 | — |
| **site emitted** | 3 or 4 | 1 or 2 |

**So the answer to the central question is: yes, the code distinguishes them — in
control flow, in the number of records affected, and in what it has already
written to media before it dies. It does *not* distinguish them in the LED code,
because all four sites emit the identical `0x5B` at level 0 and the emitter never
returns, so no later information can be added.** The distinction is recoverable
only from *side effects*, and §4.5 lists what those are.

A corollary that matters for the survivor: **a store-level fault is
permanent and not self-healing.** `0x800112DC` has no repair path and
`0x80011196` has no salvage path; it aborts on the first failing block and the
mount then returns 6 with the catalogue left empty. The only repair primitives in
the image are `record_write` (`0x800115E6`) and the delete (`0x800090A0(0)` →
`0x8001141E`), and **both require the mount to have succeeded first**. If the
mount fails, nothing in this image can ever fix it; only an external writer can.

### 4.4 The reachability of site 4 in detail

Site 4 is *not* simply "the record is bad". The chain is:

```
0x80014A2C bl 0x8001453C          ; verified region load
0x80014A30 cbz r0 -> 0x80014A6C   ; r0 == 0 => blink(1, status)
0x80014A32 cmp r0,#1 ; beq 0x80014A3E
0x80014A3E bl 0x8000911A          ; selector #5
0x80014A44 cbz r0, 0x80014A68     ; selector == 0  => NO decrement, NO 0x5B
0x80014A46 movs r0,#1
0x80014A48 str  r0,[0x80026150]   ; "wipe pending" flag
0x80014A4A bl 0x8000911E          ; decrement
0x80014A4E cbnz r0, 0x80014A62
0x80014A50 "\nBootModeCount expended\n"
0x80014A5A bl 0x80014C98          ; ***** SITE 4 *****
```

So site 4 needs **three** things at once:

1. the region load returns exactly **1**;
2. selector #5 returns **non-zero** (so `2` — a *value* defect — or `1`);
3. `0x8000911E` then returns **0**.

And (3) is guaranteed whenever (2) is a `2`, because `0x8000911E` re-evaluates
the selector itself at `0x80009122` and bails out at `0x80009128: d10b bne`.

**The subtle part is (1) with a selector of `2`.** `0x800142A8` sees a non-zero
selector at `0x800142B0` and skips the `NvOSRegionStruct` entirely
(`0x800142BA b148 cbz r0,0x800142D0` — not taken → `0x800142BC` straight to
`0x80014148`). Inside `0x80014148`, `r6 = 2` sets status `0x0F`
(`0x80014290: 210f movs r1,#15 / f8ca 1000 str.w r1,[sl]`) — **but the function
still returns 1** [P]:

```
80014290  210f        movs r1,#15
80014292  f8ca 1000   str.w r1,[sl]        ; *status = 0x0F
80014296  2d01        cmp  r5,#1            ; r5 = 1 (container checks passed)
80014298  d104        bne  0x800142a4
8001429a  6820        ldr  r0,[r4,#0]
8001429c  f7ff fe62   bl   0x80013f64       ; region-usage bookkeeping (media 0x2819)
800142a0  b900        cbnz r0,0x800142a4    ; success -> keep r5 = 1
800142a2  2500        movs r5,#0            ; only a bookkeeping failure demotes it
800142a4  4628        mov  r0,r5            ; ***** return 1 *****
```

i.e. **a stage that has just recorded "status = 0x0F (no usable signature path)"
still reports the region as loaded**, and boot main walks straight into the
counter arm. This is the mechanism that lets a *value*-only defect reach site 4.

Which boot-state arm can get there (media `0x2040`, `tbb` at `0x8001498A`,
table bytes `44 05 0b 05 0b 0b 0b 0b 0b 00` at `0x8001498E`) [P]:

| state at `0x2040` | arm | reaches site 4 with selector = 2? |
|---:|---|---|
| `0` | `0x80014A16` → nuke (`r4 = 0`) → `0x80014A1E` verified load | **yes** |
| `1`, `3` | `0x80014998` → `0x80014F84`; if non-zero → `0x80014A1E` | **yes** (if the pre-check says so) |
| `2`, `4..8` | advance (`r4 = 1`) → selector #2 = 2 → nuke → `0x80014A90` | **no** — jumps to the OS instead |
| `≥ 9` | `0x80014A16` → nuke (`r4 = 0`) → `0x80014A1E` | **yes** |

**[U]** the actual value of media `0x2040` on either unit — it is a read-only
`nvram`/media read away (BOOT-CHAIN §5 step 4), and it decides which of these two
very different outcomes a value-only defect produces.

### 4.5 What the code leaves behind that *does* distinguish the sites

Nothing on the LED. But the four sites differ in **what has already happened**:

| site | delay from reset to the first blink | media writes already performed | UART lines already printed |
|---|---|---|---|
| 2 `0x80014958` | shortest — one SMC round-trip | **none** | banner, "OMAP4430 HS", "SW_RESTORE support enabled" |
| 1 `0x80014940` | one full sweep of `nblocks` authenticated RPMB reads | none | the same three |
| 3 (`0x800090F6`) at selector #1 (`0x8001495E`) | mount + provision probe | **none** | + `BOOT_MODE: PRODUCT` (`0x8001496E`) |
| 3 at a later evaluation (G/H/I) | after the boot-state word is handled | `0x2040` written (`0x800149B4`); maybe a nuke | + same |
| 4 `0x80014A5A` | **after the entire region-0 CRC-32 / signature verify** (≥ `0x2BFE10` bytes hashed at `0x80014788`, 10 KiB per WDT kick) plus the `0x8001450C` write of the 193-byte region struct | `0x2040`, media `0x2074` (0xC1 B), media `0x2819` (`0x80013F64`) | + "Verified region %d successfully" only if it really verified |

**The discriminators a static-only analysis can offer the operator are
therefore: (i) does the first blink arrive within a fraction of a second of
power-on (⇒ site 2 or 1) or after a visible delay (⇒ site 4)? and (ii) has the
unit written to media before it died (⇒ site 3-at-later-evaluation or 4)?**

### 4.6 Answer to Q4, stated plainly

* **One record write can cause `0x5B`.** It cannot cause site **2**; it can
  cause site **1**, **3** and **4**.
* **It causes site 1 only through *layout*, not through *meaning*.** The two
  concrete layout faults an outside writer makes are
  (a) a record set whose `Σ(24+len)` exceeds 224 — for `BOOTCTR_FCTRY` +
  `TEST_PAD` that is `len(TEST_PAD) > 172` — and
  (b) a stale tail that still contains the magic `0x5983BC2D` after a
  shorter record set was written over a longer one.
* **It causes site 3 through the record's *shape***: `len ≠ 4`, or a `slot`
  field that does not name the block the record is in, or a shadowing duplicate.
* **It causes site 4 through the record's *value***: `len == 4` and value `0` or
  `> 0x32`.
* **The code can distinguish "wrong record" from "unusable store"** — they reach
  different sites, affect different numbers of records, and differ in what has
  been written to media — **but it cannot report the difference**, because all
  four sites emit the same `0x5B` and never return.

---

## 5. Q5 — read-only preconditions for arming the next unit

This section lists what the **next boot** will require, derived only from the
tests quoted above. Everything here is checkable without writing anything.

### 5.1 The ten things a factory boot requires of the store

| # | requirement | where the next boot tests it | what happens if it is false |
|---:|---|---|---|
| 1 | the SMC `0x31`/`0x32` key/session pair succeeds | `0x80010D42`–`0x80010D6A` (step A) | `0x5B` **site 2**, nothing else attempted |
| 2 | the block device opens (≠ `-1`) | `0x800112F2`–`0x800112F8` | `0x5B` **site 1** |
| 3 | the device is RPMB-capable (`[+0x1BC] != 0`, `[+0x1E0] == 0`), the keyed digest over the three 32-byte state blocks verifies, the block read and the secure-module probe pass, and the `0x31`/`0x32` session pair succeeds — `0x80010D82` returns **1** | `0x80010D82` (step C) | `0x5B` **site 1** |
| 4 | geometry returns 0 and `nblocks ≤ 1024` | `0x8001130C`–`0x80011322` | `0x5B` **site 1** |
| 5 | **every** block `0..nblocks-1` authenticates and reads | `0x80010A34` per block | `0x5B` **site 1** |
| 6 | **every** block's record tiling fits 224 B (`Σ(24+len)+gaps ≤ 224`) | `0x80011200` per record | `0x5B` **site 1** |
| 7 | total record count ≤ 1023 | `0x8001120A` | `0x5B` **site 1** |
| 8 | `BOOTCTR_FCTRY` is **present**, its 14-byte name matches exactly, its `len` is **exactly 4**, its `slot` names the block it lives in, and the block re-reads for `record_read` | `0x80011690` via the selector | `len`/name/read → `0x5B` **site 3**; absent → product path (no `0x5B`) |
| 9 | its **value** is in `1..0x32` | `0x8000910C`–`0x80009110` | `0x5B` **site 4** (if the counter arm is reached) |
| 10 | `record_write` can rewrite it: the block is not exactly full, the monitor seal succeeds, the RPMB counter is current, the card accepts type 3 with result 0 | `0x800115E6` → `0x80011332` → `0x80010B2E` | `0x2F` (1-4) if the card/write fails; `0x5B` **site 4** if `record_write` returns 1 |

Requirements 8–10 are the *record* level; 1–7 are the *store* level. **A unit can
fail 1–7 while holding a byte-perfect record, and can hold a nonsense record
while satisfying 1–7.** That is the split §4.3 is about.

### 5.2 What "the counter write needs", in full

The decrement is `0x8000911E`: selector must be 1, value must be non-zero, then
`record_write(name, &(value−1), 4)`. `record_write`'s preconditions [P]:

| check | address | failing value |
|---|---|---|
| `len ≤ 200` and `len % 4 == 0` | `0x800115F4`/`0x800115F8` | returns 2 |
| record exists → rewrite path, else create path | `0x80011600 cbnz` | — |
| create path: `state->count < 1024` | `0x80011606` | returns **1** (= "success", silently no write) |
| rewrite path: `new_len − old_len < free[slot]` | `0x8001164A`–`0x80011658` | returns **1** (= "success", silently no write) |
| the target block reads (`0x80010A34`) | `0x80011354`, `0x8001156C` | returns 6 → write aborted |
| the 24-byte header + payload fit the remaining 224 | `0x800115A2` | returns 5 |
| payload and frame are **32-byte aligned** | `0x80010F66`, `0x80010F76` | seal returns 0 → write fails |
| the monitor `0x31` / `0x33`+`0x2F` / `0x33`+`0x2E` / `0x31` all succeed | `0x80010F94`, `0x80010FCC`, `0x80010FF0`, `0x80011008` | write fails |
| the RPMB write counter reads | `0x80010B4A` (`0x80010844`) | result **ignored**; a garbage counter is sent |
| the card accepts type 3 and returns result 0 to type 5 | `0x80010BB6`, `0x80010BF6` | write fails |

Two non-obvious consequences:

* **`free[slot] == 0` makes the decrement silently refuse.** Since
  `new_len == old_len == 4`, `need = 0`, and `0x80011656: cmp r1,r2 / da04 bge`
  branches when `0 <= free` is *false* only for `free == 0`. `record_write` then
  returns **1**, which `0x8000911E` treats as failure (`0x8000913C cbnz`), so the
  unit prints `"\nBootModeCount expended\n"` and blinks **`0x5B` site 4** — for a
  record that is perfectly valid. **A record placed so that its block is exactly
  full will kill the unit on the first factory boot.** [P]
* a `record_write` that fails *below* that level never returns 0 (it blinks
  `0x2F`), so the two failures are distinguishable in the field only by pattern.

### 5.3 The read-only checklist (nothing here writes)

Ordered by what is cheapest and most decisive. Every item is a *read*.

1. **`BOOTCTR_FCTRY` value.** `dd if=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1` →
   expect exactly `32 00 00 00`. Anything else is a §5.1-item-9 problem.
   A short or long read means the record's `len` is not 4 → site 3 territory.
2. **Whether the record is there at all**, and whether a *second* one exists.
   The store resolves duplicates by position (§3.5), so a duplicate in a lower
   block overrides.
3. **The block that holds it — its whole 224-byte payload.** This is the only
   way to check §5.1 items 5–8, the ones that kill at site 1 and site 3. What to
   add up: every record found by scanning for `0x5983BC2D` on 4-byte boundaries
   must satisfy `Σ(24 + len) + gaps ≤ 224`, and **no magic may appear after the
   last record**. `[U]` whether the QNX resmgr exposes a raw-block read; if it
   does not, this check needs a raw RPMB reader that does not exist in this
   project yet (report 148's transform is still unreproduced, so *reads* are the
   only safe primitive we have).
4. **The `slot` field** of the `BOOTCTR_FCTRY` header must equal the number of
   the block it sits in. A frame copied from a different block will not.
5. **Boot-state word, media `0x2040`.** Must be **1..8** — and specifically
   **not 0**. `0` and `≥ 9` route to `0x80014A16` → `0x800156E8`
   ("Nuking…", `0x80015970` at `0x800156EC`), which writes **1** to media
   `0x2040` (`0x80015700: f7f5 f97d bl 0x8000a9fe`) and then, per report 151
   §5.3, wipes the ranges listed at `0x8001597C` in `0x80000`-byte chunks. This
   check is independent of the record and is the largest single hazard on the
   next boot.
6. **`0x2801` (112 bytes).** A read shorter than `0x70` blinks `0x37` at
   `0x80014FBE` — but only on the boot-state 1/3 arm.
7. **Region 0 container.** `0xC3D0E193` at image `+0x9C`, size ≥ `0x20000`,
   `os_type @ +0xD4 == 6` (indexed by `0x80013FB6`), `0xD7C82D1F` at `end-8`,
   length word at `end-4` ≠ `0xFFFFFFFF`, and the CRC-32/ISO-HDLC over
   `[start, end-4)` equal to the word at `end-4`. On the factory path this is the
   *only* content check that runs; if it passes and the record's value is bad,
   the unit dies at **site 4** after having written media.
8. **`nblocks`.** If the store's geometry can be read (`0x8000FAF2` mode 3, byte
   at `+0x141`), `nblocks = byte << 9` must be ≤ 1024. `[U]` from user space.
9. **Free space in the record's block.** Must be **≥ 1 byte** (§5.2), which in
   practice means the record set must not exactly fill all 224 bytes.
10. **The provisioning flag `r7`.** If the next boot prints
    `"RPMB not provisioned"` it will call `0x800090A0(1)`, i.e. it will **write**
    `0x32` before doing anything else. That write is a full
    read-modify-seal-write of the block and is subject to every precondition in
    §5.2.

### 5.4 The two failure classes the checklist separates

* **Fails 1–7 (store-level).** Nothing about the record can save the unit and
  nothing in the image can repair it (§4.3). It will blink `0x5B` at site 1 or 2
  on every boot, for ever, before reading any region.
* **Fails 8–10 (record-level).** The store is fine; the record is wrong. Site 3
  if the shape is wrong, site 4 if only the value is wrong. Site 4 additionally
  implies the unit has already verified a region and written media `0x2074` /
  `0x2819` / `0x2040`.

**Therefore the arming step must be preceded by a check of items 1–7, not just
of the record.** Checking that `BOOTCTR_FCTRY` reads back as `32 00 00 00` after
the write verifies item 8 and part of item 9 and says nothing at all about
items 5–7 — which are exactly the ones a hand-built block can break, and exactly
the ones that produce a permanent site-1 death.

---

## 6. Residual unknowns

| # | question | why unresolved |
|---|---|---|
| 1 | Which `0x5B` site fired on the dead unit | all four emit the identical 1-2-2; §4.5 lists the side-effect discriminators but none was captured |
| 2 | The contents of the dead unit's block 2 (`Σ(24+len)`, the tail, the `slot` fields) | the block is on the dead unit's RPMB and we have no raw RPMB reader |
| 3 | `nblocks` on either unit (byte at device `+0x141`) | `0x8000FAF2` mode 3 reads a **byte** and shifts it by 9; the field's units (128 KiB?) are not stated anywhere in the image |
| 4 | The value of media `0x2040` (boot-state) on either unit | decides whether a value-only defect ends at site 4 or jumps to the OS (§4.4) |
| 5 | What monitor services `0x31`, `0x32`, `0x2F`, `0x2E` compute, and what `[0x4A30C000+0x100/0x104]` actually is | the monitor image is not in this corpus; only the ABI, the call order and the polarity are recoverable |
| 6 | Whether `SCTLR.A` is 0 when the parse runs (an odd `len` makes the next scan unaligned) | the stage never writes `SCTLR.A`; it inherits it (report 150 §3.2) |
| 7 | Whether `0x80013F64`'s bookkeeping at media `0x2819` writes | it calls `0x80013F34` twice around a 120-byte read of `0x2819`; the second call is the likely writer, not disassembled here — it matters only because a `1` return from it is what lets a `0x0F` status be reported as "loaded" (§4.4) |
| 8 | Whether the QNX `rpmb-omap4` resmgr exposes a raw block read | needed for checklist item 3 |

## 7. Reproduce

```sh
cd /home/psyden/SSDdrive/playbook-linux
. tools/env.sh
IMG=local/proprietary/device-binaries/emmc-first1mb.bin
TH="-D -b binary -m arm --adjust-vma=0x80000000 -M force-thumb"

# boot main, the four 0x5B sites and the boot-state dispatch
arm-linux-objdump $TH --start-address=0x80014904 --stop-address=0x800149d0 "$IMG"
arm-linux-objdump $TH --start-address=0x80014a1e --stop-address=0x80014ad0 "$IMG"
arm-linux-objdump $TH --start-address=0x800090a0 --stop-address=0x80009160 "$IMG"

# the mount and the parse
arm-linux-objdump $TH --start-address=0x8001112c --stop-address=0x80011332 "$IMG"
arm-linux-objdump $TH --start-address=0x80011660 --stop-address=0x80011760 "$IMG"

# the three "make the store usable" helpers
arm-linux-objdump $TH --start-address=0x80010d14 --stop-address=0x80010d3c "$IMG"
arm-linux-objdump $TH --start-address=0x80010d3c --stop-address=0x80010d82 "$IMG"
arm-linux-objdump $TH --start-address=0x80010eb8 --stop-address=0x80010f58 "$IMG"
arm-linux-objdump $TH --start-address=0x8000faf2 --stop-address=0x8000fb74 "$IMG"
arm-linux-objdump $TH --start-address=0x8000cfd8 --stop-address=0x8000d012 "$IMG"

# the RPMB frame layer: read, write, counter, seal
arm-linux-objdump $TH --start-address=0x80010a34 --stop-address=0x80010b2c "$IMG"
arm-linux-objdump $TH --start-address=0x80010844 --stop-address=0x800108d8 "$IMG"

# the region-verify return that lets a value defect reach site 4
arm-linux-objdump $TH --start-address=0x80014148 --stop-address=0x80014200 "$IMG"
arm-linux-objdump $TH --start-address=0x80014248 --stop-address=0x800142a8 "$IMG"

# SMC wrapper polarity (ARM)
arm-linux-objdump -D -b binary -m arm --adjust-vma=0x80000000 \
    --start-address=0x80017c10 --stop-address=0x80017ce8 "$IMG"

# every literal the record code uses
python3 - <<'EOF'
import struct
d=open('local/proprietary/device-binaries/emmc-first1mb.bin','rb').read()
for a in (0x80011530,0x80011534,0x80011538,0x8001153c,0x80011540,
          0x80011104,0x80011108,0x80010c44,0x80010c48):
    print(hex(a), hex(struct.unpack_from('<I',d,a-0x80000000)[0]))
EOF
```

The tabulated reachability in §2.2 and §4.2 was produced by decoding every
`bl`/`blx` target in the 1 MiB and cross-checking the five selector call sites
(`0x8001495E`, `0x800149BE`, `0x8001415E`, `0x800142B0`, `0x80014A3E`) and the
eleven `0x5B`-adjacent call sites by hand.

