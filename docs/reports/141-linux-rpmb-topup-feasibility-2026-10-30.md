# 141 — Can Linux itself write `BOOTCTR_FCTRY`? RPMB top-up feasibility for our own kernel/userspace

Status: **static reverse engineering only.** No device, no USB, no `/dev/ttyACM0`, no
flash, no RPMB/eMMC access, no writes outside this workspace. Nothing here was executed
on a PlayBook.
Date: 2026-10-30.
Legend: **[P]** proven from bytes/instructions quoted here, **[I]** inferred,
**[U]** unresolved.

---

## 1. Question and verdict

**Question.** Can our own Linux kernel/userspace on the PlayBook OMAP4430 write the RPMB
record `BOOTCTR_FCTRY` (4 bytes, `0x32`) without a booted TabletOS, so that the QNX root on
`hd3` can be replaced by our ext4 and the factory-counter top-up no longer depends on QNX?

**Verdict: yes — this is feasible, and the decisive piece of prior uncertainty now resolves
in favour of the literal.** The QNX driver does **not** keep the RPMB key in the secure
world: for every authenticated transaction it hands `smc #1` a *descriptor containing a
pointer to the 32-byte key in normal-world, non-cached memory* (§3.4, proven at
`0x2774`–`0x2790`). The monitor is therefore a **crypto engine with a caller-supplied key**,
not the custodian of a fuse secret. On this unit the key material is *seeded* from a
**32-byte ASCII literal that ships in `proc/boot/rpmb-omap4`** at file offset `0x71b4`
(`"retreating is advancing in the o"`, §3.3), which is used exactly when both KEK queries
(secure service `0x31`) fail — the condition report `116` §3 measured on this box. One link
is still not provable from bytes alone: secure service **`0x2F` op 1** turns the 32-byte seed
into the 32-byte frame key, and its body is in the monitor image, which we do not have; the
result is written back into normal-world memory at `page+0x460`, i.e. **the effective key is
readable from a live QNX system** (§6.3) and, once known, is usable from any OS. The Linux
side needs no kernel change at all: `/dev/mmcblk1rpmb` in 6.15.11 is a pure 512-byte frame
relay (`MMC_IOC_CMD`/`MMC_IOC_MULTI_CMD` only), the kernel never sees a key and never
computes a MAC, so the whole top-up is a ~200-line userspace program (§5, §7).

| | claim | status |
|---|---|---|
| A | `BOOTCTR_FCTRY` is an authenticated RPMB record; QNX writes it with one in-place 4-byte `write()` | **P** (report 92 §2.3, §5.2; frames re-derived here §4) |
| B | every MAC in the QNX path is computed by `smc #1` services `0x2E`/`0x2F` | **P** (§3.1, §3.4) |
| C | the MAC service receives the **key bytes themselves** by pointer | **P** (§3.4) — this is what kills "the key is not in the normal world" |
| D | on this unit the key **seed** is the 32-byte in-image ASCII literal | **P** for the code path (§3.3); **[I]** that the branch is taken (depends on `0x31` returning failure, report 116 §3) |
| E | the effective frame key = f(seed) for a monitor-side `f` | **P** that it is produced by `0x2F` op 1 into `page+0x460`; **`f` itself is [U]** |
| F | Linux can build the frame, compute the MAC and issue it | **P** (§5) |
| G | the top-up therefore does not need QNX | **yes, given E**; the one open item is pinning `f`, and §6.3/§7 give two read-only ways to do it |

---

## 2. Inputs

| input | path | identity |
|---|---|---|
| QNX resmgr | `local/proprietary/dumped4869ifs/proc/boot/rpmb-omap4` | 33 601 B, ELF32 LE ARM EABI5, `ET_DYN`, stripped, entry `0x1704`, `NEEDED libc.so.3`, `PLTGOT 0x8000` (readelf `-d`); **sha256 `10fbb62c9fe43dfee3097429511ddaf13a98801b754284271d03a953100a9859`** (matches the task) |
| Secure-world client | `…/proc/boot/trustzone-omap4` | 61 208 B, sha256 `68af63b02e982145993bd9e35219e27578f6878317b4b8092f06a539705e5dc0`; it is a **resmgr for `/dev/trustzone`**, not the monitor image (§3.6) |
| SD/MMC driver | `…/proc/boot/devb-mmcsd-winchester` | sha256 `7e445c5f3dc1e53ecb806f1e893c27fd2564842a6d46a5d2c863a2698882f69e` (owns `/dev/hd6`, the RPMB block device the resmgr talks to) |
| libc | `…/proc/boot/libc.so.3` | sha256 `18af9ea7db71873169a027fb4820f8fdc719c1688ef5471c055896b8f80dad13` |
| ramloader | `local/vendor/bbusb/loaders/loader_06001A06-00.bin` | 238 128 B, sha256 `c7cc33c796f9518bbc0eb929c6096f2473937ad486c579764aa4c57d8165393f`; same 46-byte literal at file offset `0x38da0` (own `strings -t x`, independently of report 116), 11 `smc` instructions |
| Linux kernel | `local/kernel-tree` | 6.15.11 (`Makefile` VERSION/PATCHLEVEL/SUBLEVEL), tree HEAD `e73a4276f94cbf7753878778410cfbd8db4e11d7` (2026-10-30, "policy: writable eMMC for our own data; hd3 to host the Linux root"), `.config`: `CONFIG_MMC_BLOCK=y`, `CONFIG_MMC_BLOCK_MINORS=8`, **`# CONFIG_RPMB is not set`** (line 1885) |
| TRM | `hardware/swpu231ap.pdf.txt` | OMAP4430/4460 TRM SWPU231AP, 399 915 lines |
| prior claims verified | `docs/reports/92-…`, `116-…`, `docs/contradictions/rpmb-key-kek-vs-fallback-literal.md`, `docs/project/SAFETY.md` §2 | treated as claims to check |

Tooling: `local/toolchains/qnx660-master/host/linux/x86/usr/bin/arm-unknown-nto-qnx6.6.0eabi-objdump`
(disassembly of the raw image, `-D -b binary -m arm -EL`), `local/toolchains/armv7-eabihf/bin/arm-linux-readelf`
(dynamic relocations, `needed`), `xxd`, `strings`.
**Address convention:** the resmgr's `vaddr == file offset` for the first `PT_LOAD`
(`off 0x0 → vaddr 0x0`, filesz `0x60d4`) but **not** for the second (`off 0x6f24 →
vaddr 0x7f24`), i.e. **file offset `0x71b4` = vaddr `0x81b4`**. Code addresses below are
link-time vaddrs. Report 92's BSS names (`0x1824c`, `0x18150`, …) are 0x10000 higher than
mine (`0x824c`, `0x8150`, …) because of a Ghidra rebase; the *offsets into the page* and the
`.rodata` vaddrs (`0x5a84`, `0x81b4`) agree exactly. Report 92 also quotes a few **code**
addresses in the Ghidra numbering (`0x14330`, `0x1364C`, `0x13D44`, `0x12D20`, … = mine
`0x4330`, `0x364C`, `0x3D44`, `0x2D20`, …) alongside correct link-time ones (`0x1988`,
`0x1C00`, `0x1F58`, `0x516C`) — subtract `0x10000` from the high ones.

---

## 3. QNX `rpmb-omap4`: the write path and the key source

### 3.1 The monitor interface — every cryptographic step is an SMC

`0x21f0` is the only instruction-level boundary to the secure world **[P]**:

```
21f0: push {r4-r10,fp,ip,lr}
21f4: mov  r6, #0xff          ; the OS-side "monitor call" tag
21f8: mov  ip, #0
21fc: mcr  p15,0,r0,cr7,cr5,{6}   ; ICIALLU
2200: dsb sy / 2204: isb sy / 2208: dmb sy
220c: smc  #1
```

`0x22e8(service, x1, domain, nargs, …)` is the generic marshaller: it copies `nargs` words
into a descriptor taken from a non-cached `mmap` page and calls `0x28c0` → `0x21f0`. It is
always entered with `domain = 4` (`mov r2,#4` at `0x24d0`, `0x2514`, `0x27a4`). The used
services **[P]** (each a small wrapper over `0x22e8`):

| service | wrapper | what it does | descriptor |
|---|---|---|---|
| `0x12` | `0x24a8` → `0x29c0` | fill an `0x20`-byte nonce buffer — `0x1adc: bl 0x29c0` with `r1=0x20` | — |
| `0x2E` | `0x26c4` | **HMAC over caller data with a caller key** | see §3.4 |
| `0x2F` | `0x2530` | RPMB key transaction (op 1 = derive seed → key) | see §3.5 |
| `0x31` | `0x2814` | KEK index/state query; returns 0 on success | — |
| `0x33` | `0x24ec` | load a 16-byte key into the monitor | — |
| — | `0x1998`/`0x19cc` | the *MMC* transaction itself: `devctl(fd, 0xC0081A42, {u16 req, u32 phys_frame}, 8, &ret)`, retried twice (`0x1a44`–`0x1a8c`) | — |

PLT resolution used throughout (own `readelf -r` + stub arithmetic): `0x1440 = memcpy`,
`0x150c = mem_offset64`, `0x1548 = devctl`, `0x159c = memcmp`, `0x15d8 = memset`,
`0x14b8 = mmap_device_io`, `0x14dc = mmap`.

### 3.2 The working buffers

`RpmbInit` at `0x2ffc` maps one 4 KiB non-cached anonymous page
(`0x3060-0x3070: r0=0x1000, r2=0xb00, r3=0x90002`, `bl 0x14dc`) and hands out fixed
offsets inside it (`0x30b0`–`0x3104`) **[P]**:

| slot | page offset | role (proven by its use) |
|---|---|---|
| `0x8254` | `+0x000` | 16-byte scratch / `0x2F` "frame" arg |
| `0x8250` | `+0x020` | passed as the `0x33` key pointer of the `0x2E` calls (`0x2cc0` → `[sp+0x10]`, arg8; unused there because arg7 = 0 skips the preload) |
| `0x8258` | `+0x040` | 16-byte key handed to `0x33` |
| `0x825c` | `+0x060` | 16-byte spare |
| `0x8260/0x8264` | `+0x400/+0x420` | arg / nonce buffers |
| `0x824c` | **`+0x440`** | **32-byte key *seed*** (this is the one that matters) |
| `0x8150`→ | **`+0x460`** | **32-byte frame-MAC key** (`str r1,[r2]` at `0x30f0`) |
| `0x8268/0x826c` | `+0x480/+0x600` | AES-op / HMAC config staging |
| `0x817c`/`0x8154` | `+0x700/+0x800` | block buffer / pack buffer |

**Correction to report 92 §2.4.** It says the two 16-byte `.rodata` constants at `0x5a84`
and `0x5a94` are "handed to `0x33`/`0x2E`/`0x2F`". They are **not**. `0x301c`–`0x303c`
loads them (`ip = GOT+0xffffda84` = `0x5a84`; two `ldm`/`stm` of 16 bytes each, second half
at `0x5a94`) **onto the stack** at `sp+0x1c` and `sp+0x0c`, and those two slots are never
read again in that function (the only later stack uses are the `mmap` args, the `devctl`
arg and the `__stack_chk_guard` canary at `0x3218`–`0x3230`). Likewise nothing in the image
ever *writes* the contents of `page+0x40` or `page+0x60` — only the pointer slots at
`[r5+0x38]`/`[r5+0x3c]` (`0x30c4`, `0x30cc`). So the 16-byte key handed to service `0x33`
is **whatever the freshly `mmap`ped non-cached page contains, i.e. zeros** **[P]** for the
code, **[I]** for the zero (MAP_ANON first touch). The constants are dead data in this
binary; the 32 bytes at `0x5a84` are, read as one blob,
`443ac202 eb50051f 0c2c12b3 4ac93b4d 86787c2a 632fbe16 48ecaba6 d8840109`
(they sit immediately before the `"\tWinchester"` string at `0x5aa3`, i.e. in the
`devb-mmcsd`-variant configuration area).

### 3.3 The seed, when the KEK path fails — the literal, and its address

`0x2d20` = "calculate the RPMB key" (called once from `0x43a0`, right after the device is
opened; failure logs `"RPMB rec: failed to calculate RPMB key"` `0x5d5c`).

**KEK path** (`0x2d48`–`0x2e20`) **[P]**: `devctl(fd, 0x40101A44, buf16, 16)` at `0x2d50`–
`0x2d68` reads the eMMC **CID**; `0x2d94`–`0x2e0c` permutes it (bytes `4,5,2,3` word-swapped
into `sp+0x2c`; `sp+0x24..0x29` = CID bytes `12,11,10,9,8,7` reversed; `sp+0x22` = CID
`13,14`; `sp+0x20` = CID `15`; `sp+0x30` = CID `1`), then
`0x2e18: memcpy(page+0x440, sp+0x20, 0x14)` — a **20-byte, CID-derived seed**.

**Fallback path** (`0x31cc`–`0x31e8`) **[P]**:

```
31a4: mov r0,#0 ; add r1,r5,#0x28 ; bl 0x2814    ; smc 0x31(0, &state)   KEK 0
31b0: cmp r0,#0 ; bne 0x31cc                      ; failed -> fallback
31b8: mov r0,#1 ; ...             bl 0x2814      ; smc 0x31(1, &state)   KEK 1
31c8: beq 0x3214                                  ; success -> done
31cc: ldr r3,[pc,#116]  ; = 0x220  -> r3 = &struct
31d8: ldr r1,[pc,#124]  ; = 0x1b0  -> r1 = GOT+0x1b0
31dc: ldr r0,[r3,#0x2c]                           ; r0 = page+0x440  (seed buffer)
31e0: add r1,r1,#4                                ; r1 = 0x81b4      (the literal)
31e4: mov r2,#0x20                                ; 32 bytes
31e8: bl  0x1440                                  ; memcpy(seed, literal, 32)
```

`0x81b4` is file offset `0x71b4`, and it holds **[P]** (own `xxd`):

```
000071b0: ff ff ff ff 72 65 74 72 65 61 74 69 6e 67 20 69
000071c0: 73 20 61 64 76 61 6e 63 69 6e 67 20 69 6e 20 74
000071d0: 68 65 20 6f 74 68 65 72 20 64 69 72 65 63 74 69
000071e0: ... 00
```

`"retreating is advancing in the other direction"` = 46 bytes + NUL, at vaddr `0x81b4`
(= file `0x71b4`, because the second `PT_LOAD` is `off 0x6f24 → vaddr 0x7f24`). The 32 bytes
copied are therefore:

```
72 65 74 72 65 61 74 69 6e 67 20 69 73 20 61 64 76   "retreating is adv"
61 6e 63 69 6e 67 20 69 6e 20 74 68 65 20 6f       "ancing in the o"
   ASCII: "retreating is advancing in the o"      (32 bytes, no NUL)
```

This exactly matches report `116` §3, and report `92` §2.4's `0x81B4`. The same 46-byte
string is at file offset `0x38da0` of the signed ramloader (own `strings -t x`), so it is
product content, not a local artefact.

**Answer to sub-question 1.** The key is **not** a secure-world/KEK-only secret and it is
**not** handed to the device as a literal 32-byte key by any single call. It is:
*(a)* a **32-byte ASCII literal in this binary** (`0x81b4`), used as the **seed** for the
monitor's key derivation whenever no KEK is provisioned (both `0x31` queries fail);
*(b)* or, when a KEK is available, a 20-byte **CID-derived** seed (also public — Linux can
read the CID);
*(c)* turned into the effective 32-byte frame key by secure service `0x2F` **op 1**, whose
32-byte output lands in normal-world memory at `page+0x460` (§3.5, §6.3).

### 3.4 The MAC service hands the monitor the key bytes — the decisive fact

`0x1988(frame, key)` is the frame-MAC builder **[P]**:

```
1988: push {lr}; sub sp,#0x1c
1994: r3 = frame+0xc4 ; [sp]   = r3            ; out   = frame[0xC4]  (MAC field)
199c: r3 = 0x20      ; [sp+4] = 0x20           ; outlen= 32
19a4: r1 = 3         ; [sp+8] = 3              ; op    = 3
19ac: r1 = 0         ; [sp+0xc]= 0 ; [sp+0x10]= 0   ; no 0x33 preload
19b8: r0 = frame+0xe4                           ; data  = frame[0xE4]
19bc: r1 = 0x11c                                ; len   = 284
19c0: bl  0x26c4                                ; (data,len,key,keylen,out,outlen,op,0,0)
```

and inside `0x26c4` the key is published to the monitor **by pointer** **[P]**:

```
26d4: r7 = r0 (data)   26d8: r8 = r1 (len)   26dc: r9 = r2 (key ptr)   26e0: r6 = r3 (keylen)
275c: str r8,[r4,#8]        ; [desc+0x08] = data length
276c: ...  2774: ldr r3,[sp,#0x34]; 2778: str r3,[r4,#0x10]  ; [desc+0x10] = out length
277c: r0 = r9 ; bl 0x28c0 ; 2784: str r0,[r4,#0x14]          ; [desc+0x14] = phys(key pointer)
2790: str r6,[r4,#0x18]                                      ; [desc+0x18] = key length (0x20)
279c: mov r0,#0x2e ; mov r1,#0 ; mov r2,#4 ; mov r3,#2 ; bl 0x22e8   ; smc 0x2E
```

So the monitor receives `{data ptr, data len, out ptr, out len, key ptr, key len}` and does
a plain HMAC with *our* key material. **Nothing about the RPMB key is hidden from the
normal world at MAC time.** The same is true of the block HMAC (`0x2cd4`): data = the
224-byte record block, `key = page+0x440`, **keylen = 0x10** (only the first 16 bytes of the
seed), out = block+`0x1C4`.

Callers of `0x1988`, i.e. the key actually used, **[P]**: `0x1cec` in the block-write path
passes `r9` = its own 3rd argument, and that argument is loaded at `0x3c8c`–`0x3ca0` from
`GOT[0x150]` (`0x8150`), i.e. **`page+0x460`** — the pointer installed by `RpmbInit`
at `0x30f0`. The read path (`0x1e84`) passes its own first argument, which upstream is the
same `page+0x460`.

### 3.5 What `0x2F` op 1 is, and where the effective key appears

`0x2530` is the `0x2F` marshaller; its 0x160-byte descriptor fields are filled at
`0x25b0`–`0x263c` **[P]**: `+0x00 = 1`, `+0x02 = op`, `+0x04 = direction`,
`+0x0c/+0x1c/+0x24 = length`, `+0x18 = phys(data)`, `+0x20 = phys(out)`,
`+0x28 = phys(key2)`, `+0x2c = key2 len`; if `r3 == 0` it also sets `+0x02 |= 0x400` and
calls `0x33` (`0x2648`–`0x265c`).

The key-calculation call sits at `0x2e24`–`0x2e60` **[P]**:

```
2e18: memcpy(page+0x440, seed20, 0x14)        ; seed (CID path) or, on failure, 32-byte literal
2e2c: [sp]    = page+0x440   (data, len 0x20)
2e34: [sp+0x0c] = page+0x000 (out len 0x10)
2e44: [sp+0x14] = page+0x040 (key2, len 0x10 = zeros)
2e50: r0=1 (op) ; r1=0 ; r2=1 (dir) ; r3=0 (=> 0x400 + smc 0x33 preload)
2e60: bl 0x2530                               ; smc 0x2F
2e6c: bl 0x1d4c                               ; validate: authenticated READ of block 0
2e88: "RPMB key util: RPMB validate key failed with error %d"
```

The retry twin at `0x2f08`–`0x2f7c` does the same after logging
`"RPMB key util: retrying RPMB key gen with KEK %d"` (`0x5b24`).

The **out** buffer of this call is `page+0x460` in both cases: the only caller (`0x43a0`)
loads `r0` from `GOT[0x150]` (`0x4394`–`0x439c`), i.e. `*(0x8150)` = `page+0x460` — the
very pointer that `0x1988` later uses as the frame-MAC key. **Therefore: seed → `0x2F` op 1
→ 32-byte frame key at `page+0x460`, in normal-world memory** **[P]**.

What `0x2F` computes from `(seed 32 B, key2 = 16 zero bytes)` is inside the monitor and is
**[U]** (§6.2, §9).

`0x1d4c(fd, block0_out, &flag)` is the on-init self-test: it builds request `4`
(`0x1dbc: mov r2,#4`), address `0` (`0x1dc8`), copies the `0x12` nonce to `frame+0x1E4`
(`0x1de8`), issues the transaction (`0x1df4`), then checks `result == 0` (`0x1e1c`),
`memcmp(nonce, frame+0x1E4, 16) == 0` (`0x1e38`), recomputes the MAC over `0x11C` bytes
with the key it was given (`0x1e84`) and `memcmp`s 32 bytes into the reported flag
(`0x1e94`). **The resmgr refuses to start unless block 0 authenticates** (`main` exits on
`"RPMB resmgr: RpmbInit failed"`, `0x6050`) — which is why "`pidin` shows `rpmb-omap4`" is
a strong read-only health check (report 92 §2.1).

### 3.6 Where the monitor comes from

The monitor is **not** part of QNX: `hd0head.bin` already drives the same services
(`0x31`, `0x12`, `0x2E`) through the same `smc #1` veneer (report 92 §3.1, §1.1), and
`proc/boot/trustzone-omap4` is only a resmgr for `/dev/trustzone`
(`strings`: `"trustzone devctl needs root privelige"`, `"failed to calculate ECDH key in
devctl trustzone resmgr"`, `"/dev/trustzone"`, `"error: secure debug enabled"`). So the
secure world is resident **before any OS** and survives an OS replacement **[I]** — which
means a Linux kernel module could, in principle, issue the same `smc #1` calls (§7.3).

---

## 4. The RPMB frame and the `BOOTCTR_FCTRY` record mapping

### 4.1 The 512-byte frame — QNX offsets and Linux fields are identical

Proven at the writers/readers `0x1c00` (write), `0x1aa8` (counter), `0x1d4c` (read), `0x18d4`
(byte order) **[P]**; cross-checked against Linux `struct rpmb_frame`
(`local/kernel-tree/drivers/mmc/core/block.c:106-125`, `BUILD_BUG_ON(sizeof(...) != 512)` at
`:2867`):

| offset | size | QNX writer | Linux field |
|---|---|---|---|
| `0x000` | `0xC4` (196) | write path fills `0..0x0F` with `blknum+i` (`0x1c94`–`0x1ca8`); `0x10..0x1F` scratch | `stuff[196]` |
| `0x0C4` | `0x20` (32) | **frame MAC**, produced into `frame+0xC4` by `0x1988` | `key_mac[32]` |
| `0x0E4` | `0x100` (256) | record block (224 B) + block HMAC (32 B at `+0x1C4`) | `data[256]` |
| `0x1E4` | `0x10` (16) | **nonce** from `0x12` (`0x1b10`–`0x1b1c`) | `nonce[16]` |
| `0x1F4` | 4 | **write counter** (BE on the wire) | `__be32 write_counter` |
| `0x1F8` | 2 | **address** = block number | `__be16 addr` |
| `0x1FA` | 2 | **block count** = `1` (`0x1c7c`: `mov r2,#1`) | `__be16 block_count` |
| `0x1FC` | 2 | **result** (`0` = OK) | `__be16 result` |
| `0x1FE` | 2 | **request/response type** | `__be16 req_resp` |

Request types are the JEDEC ones and the same as `block.c:128-132` **[P]**:
`0x1aa8`/`0x1b00` → `2` (GET_WRITE_COUNTER), `0x1c68` → `3` (WRITE_DATA), `0x1dbc` → `4`
(READ_DATA), and `5` (result read) is used by the kernel's own sequence (§5.3).

`0x18d4(frame)` is the memory↔wire converter, applied symmetrically before send and after
receive **[P]**: it reverses **all 256 bytes of the data field** (`0x18e4`–`0x190c`, index
`i` ↔ `255-i`) and byte-swaps the u32 at `0x1F4` and the four u16 at `0x1F8/0x1FA/0x1FC/0x1FE`.
(The 256-byte reversal is a QNX-side convention; mainline Linux sends `data[256]` as-is.
For our purposes it is avoidable — see §7.2: patch and re-MAC the *received* byte image, so
the question never arises. For the read test it is irrelevant: the MAC is verified over the
bytes exactly as received.)

### 4.2 The record block and where `BOOTCTR_FCTRY` lives

Inside the 256-byte data field (`0xE4`), `0..0xDF` is a packed record store and
`0xE0..0xFF` is a 32-byte **block HMAC** (`0x2bf0`, `key = first 16 bytes of the seed`) **[P]**.
Record header (report 92 §1.2, re-read here at `0x1c00`'s packer and `0x2bf0`'s MAC):

```
+0x00 u32 magic 0x5983BC2D   +0x04 char name[14] (NUL padded)
+0x12 u16 block   +0x14 u16 block   +0x16 u16 len   +0x18 data[len]
```

`"BOOTCTR_FCTRY"` is 13 chars, fits 14 with the NUL, and the bootloader requires its
length to be **exactly 4** (report 92 §1.4 `0x800090EA`).

**Which RPMB address?** There is none in the code. The resmgr's catalogue/init function
(the one that calls the key calculation at `0x43a0`, link-time vaddr **`0x4330`** — report
92 calls it `0x14330`, which is its Ghidra address, +0x10000; the same function reaches
`"RPMB rec: failed to calculate RPMB key"` `0x5d5c`, `"RPMB rec: can't read RPMB block %d,
error %d"` `0x5db8` and `"RPMB: done cataloguing records"` `0x5de8` through the pool at
`0x4694`–`0x46ac`) walks addresses `0..N-1`, reads each 224-byte block and records
`(name → block, offset)`; the bootloader does the same at
every boot (report 92 §1.3). So the address is **whatever was free when the record was
first created**, and any tool must **discover it by scanning** (report 116 §4's shortcut,
endorsed here). With `mmcblk1rpmb` being 128 KiB = 512 × 256 B, that is at most 512
authenticated reads.

### 4.3 Cross-check with reports 92 and 116

* Report 92 §1.1/§1.2/§2.3/§5.2 (frame layout, record header, resmgr read/write semantics,
  the `open(O_WRONLY)+lseek(0)+write(4)` shape, `>`/`>>`/`O_TRUNC` as brick operations) —
  **confirmed** by independent disassembly of `0x1c00`, `0x18d4`, `0x1d4c`, `0x1aa8`.
* Report 92's **one error**: the `0x5a84`/`0x5a94` constants are *not* passed to any service
  (§3.2). Its "the key is not in any static image" qualification stands and is now sharper:
  a **seed** is in the image; the *effective* key is a monitor output.
* Report 116 §3 (the literal, the KEK-then-fallback code, the 32-byte copy) — **confirmed**,
  with a correction of the address (`vaddr 0x81b4` = file `0x71b4`; `0x7fb4` is the
  mistaken mapping you get if you forget the second `PT_LOAD`'s `-0x1000` bias).
* Report 116 §4's "nothing in mainline provides the named record store" — **confirmed**
  (§5.4).

---

## 5. What Linux exposes: `/dev/mmcblk1rpmb` and the ioctl/ABI

### 5.1 The character device

`drivers/mmc/core/block.c:2982` `mmc_blk_alloc_rpmb_part()` creates it for every partition
whose `area_type` has `MMC_BLK_DATA_AREA_RPMB` (`:3064`, `mmc_blk_alloc_parts`), names it
`"mmcblk%u%s"` (`:3004`), and announces `pr_info("%s: … chardev (%d:%d)")` (`:3032`). The
major comes from a dynamic `alloc_chrdev_region` held in `static dev_t mmc_rpmb_devt`
(`:189`), so the minor pair varies per boot: the real boot log shows **`250:0`**
(`docs/reports/109-epoch-test-2026-10-27.md:476`), the task statement quotes `248:0`.
`# CONFIG_RPMB is not set` (`.config:1885`) removes the rpmb_dev subsystem
(`block/rpmb.c`) but **not** the chardev — as report 109 §… already concluded.

### 5.2 The only two ioctls

`block.c:2809-2817` `mmc_rpmb_fileops` = `{ open, release, unlocked_ioctl, compat_ioctl }`,
and `mmc_rpmb_ioctl` (`:2755`) accepts exactly **[P]**:

```c
case MMC_IOC_CMD:        mmc_blk_ioctl_cmd(rpmb->md, (…*)arg, rpmb);
case MMC_IOC_MULTI_CMD:  mmc_blk_ioctl_multi_cmd(rpmb->md, (…*)arg, rpmb);
default:                 ret = -EINVAL;
```

There is **no key ioctl, no read/write/seek, no mmap** — `open` is `nonseekable_open`
(`:2796`). The `MMC_RPMB_SET_KEY`/`MMC_RPMB_WRITE` family that existed in older kernels is
**gone in 6.15**: `grep -n rpmb drivers/mmc/core/block.c` shows only the device plumbing and
the (uncompiled) `mmc_route_rpmb_frames`. So **[P]: the kernel neither knows nor accepts the
RPMB key, and never computes a MAC.** It copies the caller's 512 (or N×512) bytes into a
bounce buffer (`mmc_blk_ioctl_copy_from_user`, `:469`), turns them into CMD23+CMD25/CMD18,
and copies the response back.

`CAP_SYS_RAWIO` is required (`block.c:840`: `if (!capable(CAP_SYS_RAWIO) ||
bdev_is_partition(bdev)) return -EPERM;`).

### 5.3 The exact command sequence the kernel emits

`mmc_route_rpmb_frames()` (`block.c:2878`+) is the normative description, and it is what
userspace must reproduce with raw `MMC_IOC_CMD`s **[P]**:

* **write** (`RPMB_WRITE_DATA`, or `RPMB_PROGRAM_KEY`):
  `CMD25(write_flag = 1 | BIT(31))` with the request frame →
  `CMD25(write_flag = 1)` with a frame whose `req_resp = 5` (RESULT_READ) →
  `CMD18` to read the response frame.
* **read** (`RPMB_READ_DATA`, `RPMB_GET_WRITE_COUNTER`):
  `CMD25(write_flag = 1)` with the request frame → `CMD18` for the response frame.
* `set_idata()` (`:2860`) sets `flags = MMC_RSP_R1|MMC_CMD_ADTC`, `blksz = 512`,
  `blocks = len/512`, and leaves **`arg = 0`** (it is `kcalloc`ed).
* the auto-CMD23 is generated by the kernel in `__mmc_blk_ioctl_cmd` (`:601`):
  `sbc.arg = data.blocks | (idata->ic.write_flag & BIT(31))` — i.e. **bit 31 of
  `write_flag` is the JEDEC "Reliable Write" bit**, and `blocks = 1` for a single frame.
  Every command on the RPMB chardev gets its own CMD23 because `idata->rpmb` is set.
* `mmc_blk_part_switch(card, EXT_CSD_PART_CONFIG_ACC_RPMB | part_index)` (`:549`,
  `:925`, `:944`) — the RPMB partition select is automatic; the caller must **not** do it.

Because `MMC_IOC_MULTI_CMD` runs the array in one request (`:789`–`:815`), one ioctl per
transaction is enough, and both buffers are ordinary userspace memory.

### 5.4 Tools

No `mmc-utils` in this workspace and no `mmc` binary in either toolchain. `mmc-utils`
would only give us `rpmb read-counter` / `rpmb read`; its `rpmb write-key` has no
counterpart in this kernel, and it has **no** code that would help find the record inside a
block. So the tool is ours: ~200 lines of C, no kernel change.

**Answer to sub-question 3.** The ABI is `open("/dev/mmcblk1rpmb")` +
`MMC_IOC_CMD`/`MMC_IOC_MULTI_CMD` with `struct mmc_ioc_cmd` arrays
(`include/uapi/linux/mmc/ioctl.h:8-60`); frames are raw 512-byte
`struct rpmb_frame` in the caller's buffer; **the kernel neither computes the MAC nor
accepts/caches a key** — the caller supplies the complete authenticated frame
(`block.c:2755-2778`, `:2860-2876`, `:601`); the partition select, CMD23 (with the
reliable-write bit from `write_flag` bit 31) and the CMD25/CMD18 pair are done for us
(`:549`, `:580-612`, `:925-948`). Our tool therefore does: frame build → HMAC-SHA256 → one
`MMC_IOC_MULTI_CMD` → result/nonce/MAC check → readback (§7.1).

---

## 6. Can Linux supply the key? (MAC/key semantics)

### 6.1 What the device requires

Per JESD84-B51 (and mirrored in `block.c:106-125`): the frame MAC is an **HMAC-SHA256 over
the 284 bytes `[0xE4, 0x200)`** with the **32-byte** RPMB key, stored at `[0xC4, 0xE4)`.
QNX proves the same two numbers independently **[P]**: `0x19bc: mov r1,#0x11c` (284) and
`0x26e0/0x2790: keylen = 0x20` (32). The device also requires a **fresh 16-byte nonce**
echoed back and the **current write counter**; the counter is monotonic and cannot be reset.

Nothing in the SoC does this for us: the TRM's only two RPMB statements are that the
attached eMMC *offers* RPMB (`hardware/swpu231ap.pdf.txt:377156`, eMMC feature table,
TRM p.5402: `"Secure memory block (RPMB)                Yes"`) and that the **ROM code** does
not use it (`:395382`: *"The booting image is assumed to be located in the User Data Area
(no use of boot and/or RPMB partitions)"*). The string `"replay protected"` does not occur
in the TRM at all, and the `MMCHS` chapter has no key/MAC register and no data-endianness
control (`grep -i endian` hits only the sDMA chapter). **[P]: RPMB is a *device* feature
reached through plain CMD23/CMD25/CMD18 — there is no controller-side key or MAC engine.**
Whatever holds the key, it is software.

### 6.2 Where the key can possibly come from, given §3.4

Since the MAC service is handed *our* key material, the effective key is
`key = f(seed)` where `seed` is 32 bytes of normal-world data and `f` is whatever `0x2F`
op 1 computes with a 16-byte key of **zeros** (§3.5) — or, equivalently, `f` may be the
identity. Enumerable candidates, all cheap:

| # | hypothesis | why plausible |
|---|---|---|
| H1 | `key = seed` (identity) | fallback seed is already exactly 32 bytes; report 116 §3's reading |
| H2 | `key = HMAC-SHA256(0^16, seed)` | the 16-byte "key2" arg is zeros; a KDF is needed for the 20-byte CID seed anyway |
| H3 | `key = SHA-256(seed)` | same reason, simpler |
| H4 | `key = HMAC-SHA256(seed[0..16], seed[16..32])` or similar | 16-byte block key vs 32-byte frame key split (§3.4) |

Note that in the **KEK** path the seed is the eMMC **CID**, which Linux reads from
`/sys/block/mmcblk1/device/cid` — so even the non-fallback hypothesis is testable and
usable.

### 6.3 The two read-only ways to pin `f` (both cost zero writes)

1. **From Linux, against the device (preferred).** Issue `RPMB_GET_WRITE_COUNTER`
   (`req_resp = 2`, **no MAC at all**) and then an authenticated `RPMB_READ_DATA`
   (`req_resp = 4`, `addr = 0`, random nonce). Verify the response MAC as
   `HMAC-SHA256(key, resp[0xE4..0x200]) == resp[0xC4..0xE4]` for each candidate key in
   §6.2. A match proves the key **and** the whole frame convention in one shot; a mismatch
   costs nothing (reads do not spend the write counter). This is report 116's E1/E2, and it
   is the reason the answer is "feasible": the test needs only a booted Linux, which we
   have.
2. **From a live QNX, memory dump (fallback, and it also settles `f` directly).**
   The effective key sits in `rpmb-omap4`'s non-cached page at `page+0x460` (§3.5). Read
   32 bytes from `/proc/<pid>/as` at that address (or with `gdb -p`) while TabletOS is up.
   Read-only, no RPMB traffic at all, and it makes hypothesis testing unnecessary.

Either gives us the key; then Linux needs nothing from QNX ever again.

### 6.4 Closing `docs/contradictions/rpmb-key-kek-vs-fallback-literal.md`

**Claim A** ("the key is secure-world/KEK-derived, so a normal-world patch cannot forge it")
is **refuted in substance and confirmed in a narrow sense**:

* **Refuted:** the monitor never holds the key. `0x26c4` passes `phys(key_ptr)` and the key
  length in the descriptor (`0x2784`, `0x2790`) and `0x1988` passes `page+0x460`. A secret
  that is handed to a MAC engine on every transaction, in plaintext, in normal-world
  memory, is not a secure-world secret. A's consequence — "a normal-world patch cannot
  forge a record" — is **false**.
* **Confirmed narrowly:** the *derivation constant* `0x80033760` that report 71 §7b saw in
  the **bootloader** is indeed a runtime buffer, and the bootloader does not ship the key.
  A was really a statement about the static **hd0** image, which is right.

**Claim B** ("on this unit the fallback literal is used") is **confirmed as the code path**
(`0x31a4`–`0x31e8`, and the identical 46-byte literal in both the resmgr and the signed
ramloader) and **still [I] as a runtime fact** — it depends on `0x31` returning failure,
which report 116 §3 measured (`-1` at every probe) but which remains a probe result, not a
byte-level proof. B's wording "the RPMB key is therefore almost certainly those 32 bytes"
needs one correction: those 32 bytes are the **seed**; the *frame key* is `f(seed)` (§6.2),
produced by `0x2F` op 1 and visible at `page+0x460`.

**The contradiction dissolves** as follows: there is exactly **one** keyspace (the eMMC
programmed key, whatever `f(seed)` produced), it is *not* fuse-only, and both readings were
descriptions of different layers of one public-input derivation. Neither "secure-world only"
nor "the key is that literal" is exactly right; "the key is a monitor-computed function of a
seed that ships in the image (or the CID)" is.

---

## 7. Exact Linux procedure if possible, or the precise blocker if not

### 7.1 The procedure

**Step 0 — read-only self-test, no key.**
`open("/dev/mmcblk1rpmb", O_RDWR)`; one `MMC_IOC_MULTI_CMD` with
`{ CMD25 write_flag=1, blksz=512, blocks=1, arg=0, frame{req_resp=htobe16(2)} }` then
`{ CMD18 write_flag=0, blksz=512, blocks=1 }`. Success = `result == 0` and a plausible
`write_counter`. Proves partition switch, CMD23/25/18, frame layout and field byte order.
**Reads spend nothing.**

**Step 1 — pin the key (§6.3 option 1).** For each candidate in §6.2, an authenticated read
of `addr = 0` with a random nonce, MAC verified as
`HMAC-SHA256(key, resp[0xE4..0x200]) == resp[0xC4..0xE4]`. Stop at the first match; record
it. (Or take the key from a live QNX, §6.3 option 2, and skip the search.)

**Step 2 — locate the record.** For `addr = 0 … 511`, authenticated-read the block and scan
its 256-byte data field for the magic `2D BC 83 59` **and** the 13 bytes `BOOTCTR_FCTRY`
(both plain and 256-byte-reversed order — see §4.1). Record `(addr, offset_of_data,
current 4 bytes)`. Do **not** write yet.

**Step 3 — the write (one write, exactly `0x32`).**
1. Re-read the target block (fresh nonce, fresh counter) into a 512-byte buffer `F`.
2. Patch the 4 record bytes **in place** in `F`, at the offset found in step 2. Patching
   the *received* byte image sidesteps the §4.1 byte-order convention entirely; the current
   value (must be `1..0x32`, i.e. exactly one of the four byte positions is `0x01`–`0x32`
   and the other three are `0`, for a value such as `0x28`/`0x31`) shows which end is the
   low byte, and the new 4 bytes go in the same order with `0x32` in the low position (the
   QNX view of the record is `32 00 00 00`, report 116 addendum 2). Re-read and compare
   before patching; if the layout does not look like a 1-byte value + 3 zeros, stop.
3. Recompute the **block HMAC** over `F[0xE4..0x1C4)` with the first 16 bytes of the seed
   (§3.4) into `F[0x1C4..0x1E4)` — required or the bootloader treats the block as damaged
   (report 92 §3.4).
4. `F[0x1F4] = htobe32(counter)`, `F[0x1F8] = htobe16(addr)`, `F[0x1FA] = 1`,
   `F[0x1FC] = 0`, `F[0x1FE] = htobe16(3)`.
5. `F[0xC4..0xE4) = HMAC-SHA256(key, F[0xE4..0x200))`.
6. One `MMC_IOC_MULTI_CMD`: `CMD25 write_flag = 1|BIT(31)`, blksz 512, blocks 1, `F`; then
   `CMD25 write_flag = 1` with `req_resp = 5`; then `CMD18` into a response buffer.
   Require `result == 0` in the response.

**Step 4 — readback.** Repeat step 2 on `addr` and require the new 4 bytes. Also confirm the
write counter advanced by exactly 1.

**Step 5 — the per-boot hook.** Run steps 2–4 at boot *only if* the value is not already
`0x32` (the discipline `docs/report 116` §5 and `bench/device-startup.sh` already encode:
wait for the device, read first, refuse anything but `0x32`, verify by readback, retry with
delay). Cache `(addr, offset)` so the steady state costs one read and zero writes.

### 7.2 Why nothing here needs QNX

* the record store layout is fully known (§4.2) and reachable by scanning;
* the block HMAC and the frame MAC are plain HMAC-SHA256 over known byte ranges with keys
  derived from public data (§3.4, §6.2);
* the transport is a documented, already-present chardev (`mmcblk1rpmb`) with a 2-ioctl
  ABI (§5.2) and no kernel involvement in keying;
* the partition select, CMD23 and reliable-write bit are all done by the kernel (§5.3).

### 7.3 If step 1 fails for every candidate — the precise blocker and the ways out

Blocker: **`f` is not identity and not one of the candidates**, i.e. the frame key is only
obtainable by executing secure service `0x2F` op 1. Consequences and options, in order of
cost:

1. **Still no QNX needed** if we take the key from a live QNX *once* (§6.3 option 2,
   read-only `/proc/<pid>/as` dump, 32 bytes). Then Linux owns the top-up forever. **This
   is the recommended path regardless**, because it removes all guessing.
2. **Call the monitor from Linux.** The `smc #1` ABI is fully recovered here: `r0 = service`,
  `r1 = 0`, `r2 = 4` (domain), `r3 = 2` (arg count), and a two-word arg list pointing at a
  non-cached descriptor. A ~40-line out-of-tree module could reimplement `0x2F` op 1 and
  `0x2E` verbatim (`§3.1`, `§3.4`) — no key needed at all. Precondition, **[U]**: that the
  monitor is resident after our Linux boot (it is loaded before any OS, §3.6) and that it
  accepts domain 4 from a different non-secure OS (it is stateless per call in this ABI).
3. **Do not replace `hd3`** until 1 or 2 is verified on the *sacrificial* unit — the
   current "keep a writer-capable OS" rule (`docs/project/SAFETY.md` §2).

---

## 8. Safety (write counter, one write, readback)

* **Values.** Only `0x32`; `1..0x32` are the only values the bootloader's selector accepts,
  `0` and `> 0x32` halt the unit with "expired factory mode", and a record length ≠ 4 is
  selector 2 (`docs/project/SAFETY.md` §2; report 92 §1.4, §5.3). Clamp in code: refuse any
  value != `0x32`.
* **Write counter.** Every authenticated write (including the bootloader's own per-boot
  decrement and its repair rewrites) spends one unit of a monotonic counter that cannot be
  reset; saturating it permanently write-protects the partition (report 92 §6 R4). Never
  loop writes, never write when the value already reads `0x32`, and never "test" a write.
* **One write per top-up.** Read first, write only on a mismatch, verify by readback, then
  stop. Reads are free — use them for all discovery and validation (§7 steps 0–2).
* **Never rebuild the block from scratch.** Patch the freshly read image and re-MAC it
  (§7.1 step 3); that preserves every other record in the block (`BSIS_PRIV`, 168 B, must
  never be touched — report 116 §7) and the block HMAC.
* **The working unit stays read-only** until steps 0–4 have run end-to-end on a
  sacrificial unit and survived several boot cycles (report 116 §5's ladder).
* **The one-way door.** If Linux becomes the only bootable OS and then writes a bad value,
  there is no software recovery (no pre-OS writer; the bootloader halts before any OS). The
  physical remedy is desoldering the eMMC. Treat the first write from Linux as the most
  dangerous operation in the project.

---

## 9. Residual unknowns

| # | unknown | impact | how to close |
|---|---|---|---|
| U1 | **What `0x2F` op 1 computes** (identity? HMAC-zero-key? SHA-256?) | decides whether the key can be computed offline at all | §6.3: candidate-key authenticated read from Linux, or a 32-byte `/proc/<pid>/as` dump from a live QNX |
| U2 | Whether the `0x31` KEK query really fails on this unit at runtime (report 116 §3's `-1` probe) | selects the CID seed instead of the literal | one QNX boot: `pidin` + `/proc/<pid>/as` dump; or the CID-seeded candidate set in the same read test |
| U3 | The 256-byte data-field byte order the eMMC expects (QNX reverses, mainline does not) | wrong order ⇒ unreadable/incorrectly-MAC'd writes | avoid it by patching the received image (§7.1); the step-0 counter read proves the header ordering |
| U4 | Whether `devb-mmcsd` / the monitor adds a further transform to the 224-byte block (the `0xE0` vs `0x100` length mismatch at `0x1c2c`/`0x2ccc`) | could change the on-media block layout | the same authenticated read: compare the read-back 224-byte block with the catalogue expectation |
| U5 | Which RPMB address holds `BOOTCTR_FCTRY` on each unit | tool must scan (≤512 reads) | step 2 |
| U6 | Whether the monitor is resident, and accepts domain 4, when *our* Linux boots | decides whether option 2 in §7.3 exists | a sacrificial unit: one `smc #1` probe (service `0x31`) from a Linux module — read-only |
| U7 | `0x2f`'s exact framing when the seed is 20 bytes (CID) vs 32 bytes (literal): the `0x20` length is passed in both cases, so the tail is 12 zeros | changes the CID-path candidates | covered by the read test |

---

## 10. Bench result (session 18): the Linux read path works; `f` is not a simple KDF

The probe was built and run on the spare.  `tools/pbrpmb` (source
`initramfs/rpmb.c`, image **`0xDDB9D733`**) does step 0 and step 1 with **zero
writes**:

| check | result |
|---|---|
| SHA-256/HMAC selftest (FIPS + RFC 4231#2) | PASS |
| ioctl numbers vs the kernel | `MMC_IOC_CMD=0xc048b300`, `MMC_IOC_MULTI_CMD=0xc008b301`, `sizeof(mmc_ioc_cmd)=72` |
| GET_WRITE_COUNTER | `result=0`, `req_resp=0x0200`, **counter 4179**; block_count must be **0** (QNX leaves it 0; 1 gives result=1) |
| authenticated READ_DATA block 0 | `result=0`, `req_resp=0x0400`, nonce echoed |
| frame MAC / 284-byte input | captured in `docs/reports/data/rpmb-probe-0xDDB9D733-2026-10-30.txt` |
| candidate keys | **none matched** (literal and CID seeds, five hash derivations each; plus offline SHA/AES/CMAC variants) |

Two useful facts fell out:

* **The counter advances per boot**: 4177 on one boot, 4179 two boots later.
  Each factory/CRC boot performs an authenticated RPMB write (the bootloader's
  per-boot decrement), so a read-only counter check is a per-boot witness.
* **The `0x2F` op 1 call outputs only 16 bytes** (descriptor at `0x2e24`:
  data=seed 32 B, out=page+0x000 16 B, key2=page+0x040 16 B zeros).  The 32-byte
  frame key at `page+0x460` is set up separately (`0x30a0`–`0x30f4` stores the
  *pointer*), and its contents are not `seed` or a simple hash/AES/CMAC of it.
  `f` remains inside the monitor.

**Next (the report's §6.3 option 2):** extract the 32-byte frame key once from a
live QNX page, read-only.  The page layout is confirmed (`0x30a0`–`0x30f4`):
seed at `page+0x440`, key at `page+0x460`.  Find the seed in `rpmb-omap4`'s
address space (it is the literal, or the CID permutation), then read 32 bytes at
`seed+0x20`.  QNX ASLR moved the segments between boots (`pidin -p <pid> map`
showed `libc @ 0x1ad0000`, `rpmb-omap4 @ 0x1b4c000` and `@ 0x7894c000` on the
second boot), so the extraction needs a memory scan of the mappings, not a fixed
address.  Once the key is known, Linux owns the top-up forever and the one
`0x32` write can be implemented in `pbrpmb` (HMAC-SHA256 is already there).

