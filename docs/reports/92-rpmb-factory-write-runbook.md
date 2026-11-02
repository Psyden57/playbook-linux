# 92 — RPMB factory-counter (`BOOTCTR_FCTRY`) interface, read-only capture and Phase 4 write runbook

> **Superseded 2026-11-01: the unit was flashed and is armed+sealed; see `STATE.md`.**
> This runbook describes the pre-flash product-path state.  It is kept as evidence,
> not as a procedure — and an agent never writes RPMB (`SAFETY.md` §6).

Status: static analysis only. **No device was touched.** No USB, NVRAM, RPMB, eMMC or
flash access; no reboots; no writes of any kind. Every claim below is from files in the
workspace.
Date: 2026-10-25. Track: **E** of `85-linux-boot-plan.md` §5 / `87-next-session-subagents.md` §E.

| Provenance | Value |
|---|---|
| Bootloader image | `bootdumps-2026-09-11/hd0head.bin`, 2,097,152 B, load base `0x80000000`, SHA-256 `dda9d955003302adf1f0d41580c50c0c9035126487e7ac53793f4d38a3a23e3d` (matches `71`) |
| OS resource manager | `playbook-dev/dumped4869ifs/proc/boot/rpmb-omap4`, 33,601 B, QNX 6.5.0, `TAGID=333`, built 2011-10-31; vaddr == file offset for both LOAD segments |
| OS secure-world helper | `playbook-dev/dumped4869ifs/proc/boot/trustzone-omap4` (strings only) |
| Boot script | `playbook-dev/dumped4869ifs/proc/boot/.script` (the QNX boot image script) |
| Tools | Capstone 5.0.7 (ARM + Thumb), pyelftools 0.33, Ghidra 12.1.4 headless (`DumpAll.java`), `readelf`, `strings` |
| Prior work re-used | `71` §7b/§7c, `72`, `73`, `76`, `85`, `69`; `analysis/hd0-modemap-2026-10-20/{auth,rpmb-arm,record,verifier}-decompiled.txt` |

Label legend: **[P]** proven from the bytes (instruction/decompilation quoted), **[I]**
inferred from a consistent reading of the code or from two independent implementations,
**[U]** unresolved / needs data we do not have.

Address convention: `hd0head.bin` addresses are image virtual addresses (file offset =
address − `0x80000000`); the `hd0` artifacts from `71` use the same canonical numbers.
`rpmb-omap4` addresses are ELF virtual addresses (file offset = address). In my own
`rpmb-omap4` Ghidra dump (`$COMMANDCODE_SCRATCHPAD/rpmbout/`) Ghidra rebased the image to
`0x10000`, so canonical = Ghidra − `0x10000` (e.g. Ghidra `0x1516c` = canonical `0x516c`).

---

## 0. Executive summary

1. `BOOTCTR_FCTRY` lives in the eMMC RPMB partition in a **packed record store** that is
   shared, byte-for-byte, by the pre-OS bootloader (`hd0head.bin`) and the booted OS
   (`rpmb-omap4`). The layout is the same in both implementations: 512-byte frames,
   `0x1C`/`0x24`-byte descriptor tables, 224-byte (`0xE0`) on-media blocks, 24-byte record
   headers with magic `0x5983BC2D`. **[P]**
2. On the OS side the store is a **directory of files**: `/dev/rpmb` is attached with
   `_RESMGR_FLAG_DIR`; every record is `/dev/rpmb/<NAME>`; `read()` returns the record
   bytes, `write()` replaces them, `unlink()` deletes the record. There is **no devctl
   interface** — `devctl` is left at the `iofunc` default. **[P]**
3. A record write from a rooted OS is one `write()`, but the **MAC, nonce and write
   counter are produced by the secure monitor**, not by the driver: non-secure code
   supplies a plaintext block plus key/context pointers and the monitor does the
   authenticated read/write (SMC services `0x2E`, `0x2F`, `0x31`, `0x33`, `0x12`).
   **[P]** for the call structure, **[U]** for the monitor's internals.
4. **New, and it corrects `71` §7b:** the bootloader *does* run a full RPMB catalogue at
   every boot (`0x800112DC` → `0x80011196`) and *does* decrement the factory counter
   (`0x8000911E`). The counter value is literally "number of factory-mode boots
   remaining": each **successful** factory boot decrements it by one, and at zero the
   bootloader prints `BootModeCount expended` and halts in a `b .` loop
   (`0x800094BE`). **[P]**
5. Because the catalogue builds the record table *from the on-media headers*, a record
   created by the OS is found by the bootloader wherever the OS allocates it. That
   removes the block-placement risk; the risk that remains is the **value** and the
   **record length (must be exactly 4)**. **[P]** for the mechanism, **[I]** for
   placement independence.
6. A record value outside `1..0x32` (including `0xFFFFFFFF`) yields selector `2`. In the
   retry branch every non-success path ends in `0x800094BE` (`b .`): the unit halts with
   no OS, therefore no `/dev/rpmb`, therefore no repair. That is the exact mechanism of
   the two bricked 32 GB units. **[P]**
7. I found **no length/offset/nonce bug that allows a forged record without the key**
   (§8). All copy geometry on the bootloader's read path comes from a runtime descriptor
   table built from media headers *whose bounds are checked during catalogue*, and the
   32-byte MAC covers the entire frame tail including the nonce, counter, address, count
   and request type. The residual is not a parser bug; it is the monitor.
8. The write is a **one-way door conditional on a CRC-valid image in region 0**, and it
   has a real rollback only while a writer-capable OS can still boot:
   `rm /dev/rpmb/BOOTCTR_FCTRY` restores selector `0` (ECDSA/product). See §5.5 and §6.

---

## 1. The store: one format, two implementations

### 1.1 512-byte RPMB frame

Both implementations build the identical 0x200-byte request frame. Offsets are proven
twice: bootloader `0x80010A34` (read) / `0x80010B2E` (write) / `0x800107B6` (frame MAC)
and resmgr `0x1F58` (read block) / `0x1C00` (write block) / `0x1988` (frame MAC).

| Offset | Size | Field | Notes |
|---|---|---|---|
| `0x000` | `0xC4` | reserved / scratch | `0x00`–`0x0F` receive a "block tag" fill (`block+i`) by the write path; `0x10`–`0x1F` hold a staged block HMAC |
| `0x0C4` | `0x20` | **frame MAC** | written/verified by secure service `0x2E`, op field `3` |
| `0x0E4` | `0xE0` | **record block** (224 B) | the unit of allocation and authentication |
| `0x1C4` | `0x20` | **block HMAC** | separate MAC over `[0x0E4,0x1C4)` only (224 B, 16-byte key) |
| `0x1E4` | `0x10` | **nonce** | from secure service `0x12`, fresh per transaction |
| `0x1F4` | `4` | **write counter** | from secure service `0x31` (or the device's counter reply) |
| `0x1F8` | `2` | address (block number, u16, big-endian on the wire) | |
| `0x1FA` | `2` | block count (big-endian) | always `1` here |
| `0x1FC` | `2` | result (big-endian) | `0` = OK; `7` is treated as "counter not yet valid" |
| `0x1FE` | `2` | request (big-endian) | `2`=counter, `3`=write, `4`=read, `5`=result — exactly JEDEC RPMB request codes |

Sizes partition the frame exactly: `0xC4+0x20+0xE0+0x20+0x10+4+8 = 0x200`. **[P]**

**MAC coverage.** The frame MAC is computed over **`0x11C` = 284 bytes starting at
`0x0E4`** — i.e. block + block HMAC + nonce + counter + address + count + result +
request. Proven at `0x800107F0` (`mov.w r1,#0x11c`) feeding `0x80017758`, and
independently at resmgr `0x1988` (`FUN_000126c4(param_1+0xe4, 0x11c, key, 0x20,
param_1+0xc4, 0x20, 3, 0, 0)`). The block HMAC is computed over **`0xE0` = 224 bytes
starting at `0x0E4`**, output to `0x1C4` in the OS path and to `0x10` (then compared with
`0x1C4`) in the bootloader path (`0x80011048`, `0x80010AD8`). **[P]**

Consequence: **nothing in the frame can be changed without invalidating the frame MAC**,
including the block number and the request type. A replay is impossible because the nonce
is new per read and compared in full.

The `0x200`-byte buffer is converted to/from wire order by a single helper
(`0x80010770` / `0x118D4`): reverse `[0x0E4,0x1E4)` (256 bytes) and byte-swap the four u16
fields at `0x1F8/0x1FA/0x1FC/0x1FE` and the u32 counter at `0x1F4`. The helper is
involutive and is used symmetrically before send and after receive. **[P]**

### 1.2 224-byte record block

A block is a sequence of records, each:

| Offset | Size | Field |
|---|---|---|
| `+0x00` | 4 | magic `0x5983BC2D` |
| `+0x04` | 14 | name, NUL padded |
| `+0x12` | 2 | block number (informational copy) |
| `+0x14` | 2 | block number again |
| `+0x16` | 2 | data length (u16) |
| `+0x18` | len | data |

The next record starts at `+0x18+len`; unused tail is zero. Proven three times: the
packer `0x80013A14` (copies each entry's 0x18-byte header and data into the block and
stores the running offset in the *table entry's* `+0x18`), the scanner `0x80013A9C`
(searches dwords for `0x5983bc2d`), and the bootloader's packer
`0x800115E6`/`0x8001124E`. Header and table entry share the first 0x18 bytes, which is why
the offsets line up; the table entry additionally carries the block number at `+0x12`, the
block number again at `+0x14`, and the in-block offset at `+0x18`. **[P]**

**The reader compares only the 14-byte name.** Both implementations derive the copy
length and offset from their own descriptor, never from the on-media header:
`0x80011690` uses `desc[+0x16]` (length) and `desc[+0x18]` (offset) and only `memcmp`s 14
bytes at `block+offset+4` (bootloader) / `strncmp` 14 bytes (resmgr `0x138C8`). **[P]** —
this is the core of the §8 audit.

### 1.3 Descriptor / record table

| | Bootloader (`hd0head.bin`) | OS (`rpmb-omap4`) |
|---|---|---|
| table base | `0x8002C760` (literal at `0x11710`) | `0x1827C` (BSS) |
| entry size | `0x1C` | `0x24` |
| count | `*(u32*)0x80025FB4` (u32 at `0x80025FB0+4`) | `0x18278` |
| max entries | `0x400` (`0x80011606`) | `0x400` (`0x1364C`) |
| free-space per block | `0x80025FB0` array, `u16[0x400]` | `0x21280` array, `u16[0x400]` |
| built at boot | **yes**: `0x800112DC` → `0x80011196` catalogue | **yes**: `0x14330` catalogue |
| block buffer | `0x80033F80` | `*0x18154` = `page+0x800` |

Both catalogues walk blocks `0..N-1`, read each `0xE0` block, scan for the magic and add a
descriptor per record, decrementing the block's free counter by `0x18+len`. Both abort if
the free counter would go negative (`RPMB rec: more records in block than possible` /
`can't add record, no space`). **This is why the pre-OS stage can find a record that the
OS created, whatever block the OS chose.** **[P]** for the bootloader catalogue
(`0x80011196` writes table entries from `header+0x16`/header position, checks
`free[block] >= 0x18+len`), **[I]** for placement independence.

### 1.4 Sizes and limits

* Record data length: **< 0xC9 (201)** and 4-byte aligned (`0x800115F4`:
  `cmp r4,#0xc8; bhi reject`, `lsls r1,r4,#0x1e; beq ok`). Same in the OS
  (`0x13D44`). Effectively ≤ 200 bytes.
* Name: ≤ 0x11 chars in the OS (`0x1364C`), stored as 14 bytes; names ≥ 14 chars are
  truncated to 13 + a decimal suffix < 0x100 (`0x134D0`). `"BOOTCTR_FCTRY"` is exactly
  13 chars — fits with the NUL. **[P]**
* Blocks per store: ≤ `0x400` (1024) in both.
* `BOOTCTR_FCTRY` data length must be **4**, or the bootloader rejects the record
  (`0x800090EA`: `ldrh r0,[sp,#4]; cmp r0,#4`). **[P]**

---

## 2. The `/dev/rpmb` interface (`rpmb-omap4`) — addresses and strings

### 2.1 Attach and permissions

```
.script:   rpmb-omap4 -U99:102 -f0:102        ; target uid/gid 99:102, file owner 0:102
           /dev/hd6                            ; the RPMB block device it opens
           /dev/rpmb                           ; the path it attaches
```

`main` (`0x516C`):
`iofunc_func_init(8, 0x1814C, 0x1D, 0x18134)` then
`resmgr_attach(dpp, &rattr, "/dev/rpmb", 0, 0x100, connect, io, &ioattr)` — flags `0x100`
= `_RESMGR_FLAG_DIR`, i.e. **a directory node**, `0x1D` = 29 I/O functions. **[P]**

| Table | Address | Overrides |
|---|---|---|
| `io_funcs` (BSS) | `0x11AE0` | `+0x04` = `0x4C78` **read**, `+0x08` = `0x4A48` **write**, `+0x0C` = `0x49F8` **close_ocb** |
| `connect_funcs` (BSS) | `0x11B58` | `+0x04` = `0x5004` **open**, `+0x08` = `0x4F40` **unlink** |
| `ioattr` | `0x11B80` | mode `0x45C8` = `S_IFDIR|02710` |
| `rattr` | `0x11BDC` | — |

`devctl`, `stat`, `lseek` etc. are left at the `iofunc` defaults: **there is no private
devctl API on `/dev/rpmb`** (`devctl()` returns the `iofunc` default, `ENOSYS`). **[P]**
(Proven by the absence of any store to `io_funcs+0x18` — only `+4`, `+8`, `+0xC` are
patched.)

Per-record nodes are created by `0x1356C`:
`iofunc_attr_init(attr, 0x1A0 | 0x8000, …)` → **`S_IFREG | 0640`**, owner/gid from
`-f0:102` i.e. `root:102`; size field = the record length. So records are
`root:102 0640` — root reads/writes them. **[P]** for the mode constant, **[I]** for the
final ownership (options are `-f0:102`).

Resource-manager start-up fails closed: `0x14330` reads block 0..N and returns `6`
(`RPMB rec: can't read RPMB block %d, error %d`) if **any** block fails authentication, and
`main` then exits (`RPMB resmgr: RpmbInit failed`). Therefore: **if `pidin` shows
`rpmb-omap4` running, every block in the store authenticated.** Useful read-only
health check. **[P]**

### 2.2 Name → record resolution

`FUN_00013774(name)` → `FUN_000134D0` normalises the name to 14 bytes → linear `strncmp`
over the table (`0x18280`, stride `0x24`) → returns the table entry. `0x1387C` returns the
entry's `iofunc_attr_t*` for `open`; `0x132A4(recid)` maps `recid = index+1` back to the
entry (`&0x18258 + recid*0x24` ≡ `0x1827C + (recid-1)*0x24`). **[P]**

Record-id ↔ name is the only handle; there is no block-number API. A userland caller
cannot learn which RPMB block a record lives in (and does not need to — see §1.3).

### 2.3 read / write / verify semantics

| Operation | Handler | Semantics (all proven) |
|---|---|---|
| `open()` | `0x5004` | Resolve name → attr. `O_CREAT` when missing → `0x1364C` creates the table entry *without* touching media. **`(flags & 0x202) == 0x202` (`O_RDWR|O_TRUNC`) additionally calls `0x13D00`, which repacks the block, sets the record size to 0 and rewrites the block.** No other flags have side effects. |
| `read()` | `0x4C78` | `iofunc_read_verify`; only `xtype == 0` (plain `read`), else errno `0x59`. **Directory** (`mode & 0x4000`) → `0x33E8` returns 0x2C-byte dirents, so `ls /dev/rpmb` is the catalogue. **Regular record** (`mode & 0x8000`) → bounds-check against attr size, `malloc(size)`, `0x138C8` reads the record from media, `MsgReply` the requested slice at `offset`. A read is a full authenticated block read each time (no cache). |
| `write()` | `0x4A48` | `iofunc_write_verify`; only `xtype == 0`; `count == 0` returns 0 **without touching the record** (no truncate). New size = `offset + count`; if that is smaller than the current size the size is *kept* (no shrink). New size ≥ `0x10000` → `EINVAL (0x1b)`. Reads the old record into a fresh buffer, `resmgr_msgread`s the client bytes in at `offset`, then `0x13D44` rewrites the record (and possibly repacks/rewrites its block). |
| `unlink()` | `0x4F40` | `iofunc_unlink`; if the link count reaches 0 → `0x14264` deletes the record (repacks the block, frees the table entry, rewrites the block). |
| `close_ocb` | `0x49F8` | Repairs the record if it was unlinked while open. |
| `devctl` | — | none (default `ENOSYS`). |

**An OS-side read is write-free**: the resmgr read path (`0x1F58` → `0x11D4C`) issues
request `4` and only verifies nonce/MAC; it never calls the write path. (The *bootloader's*
read path is different — it can repair-rewrite the block, §3.4.) **[P]**

### 2.4 Nonce, MAC, counter and key material

| Service | Wrapper | Purpose |
|---|---|---|
| `0x12` | `0x24A8` → `0x22E8` | fill an `0x80`-byte buffer (nonce source) |
| `0x2E` | `0x26C4` | compute a MAC into a caller buffer: `(data, len 0xE0 or 0x11C, key, keylen, out, 0x20, op=3, dir, key33)` |
| `0x2F` | `0x2530` | authenticated read/write: `0x160`-byte descriptor — `+0x00` u16 `1`, `+0x02` u16 flag word (OR `0x400` when the key-load path is taken), `+0x04` u8 direction (`0` → `1`, `1` → `0`; `1` = write), `+0x05` u8 `0`, then pointer/length pairs at `+0x08`..`+0x28` (data block, frame, key, key length, …) |
| `0x31` | `0x2814` | write-counter / KEK state query (returns a byte, also selects which KEK is provisioned) |
| `0x32` | `0x27D0` | related key/state service (not used on the record path) |
| `0x33` | `0x24EC` | load a 16-byte key/KEK into the monitor |

All go through `0x22E8` → `0x21F0`, which does `mcr p15,c7,c5,6` (ICIALLU), `dsb/isb/dmb`,
then **`smc #1`**. Arguments: `r0 = service`, `r1 = 0`, `r2 = 4` (OS domain id),
`r3 = &{count, arg0, arg1…}` in a shared uncached page. **[P]**

Key material lives in one shared, uncached page (`mmap(0x1000, …, 0x90002)`, `*0x18220`);
the globals `0x1824C`–`0x1826C` are **pointers into it**, assigned in `0x12FFC`:

| Pointer var | Points to | Contents / role |
|---|---|---|
| `0x18254` | `page+0x000` | 16-byte buffer, `param_8` of the key-program `0x2F` call |
| `0x18250` | `page+0x020` | **`.rodata 0x5A94`**: `86 78 7C 2A 63 2F BE 16 48 EC AB A6 D8 84 01 09` — the `0x33` key used by the block-HMAC path |
| `0x18258` | `page+0x040` | **`.rodata 0x5A84`**: `44 3A C2 02 EB 50 05 1F 0C 2C 12 B3 4A C9 3B 4D` — the key passed to the key-program `0x2F` call |
| `0x1825C` | `page+0x060` | 16-byte spare |
| `0x18260`/`0x18264` | `page+0x400` / `+0x420` | arg buffer / nonce buffer |
| `0x1824C` | `page+0x440` | **0x20-byte RPMB key/context**: seeded with the 16-byte device identity read by `devctl 0x40101A44` plus 4 bytes, or — only if the KEK query fails — with the 32-byte ASCII string `"retreating is advancing in the other direction"` (`0x81B4`). Used as the `0x2E` key for the block HMAC |
| `*0x18150` | `page+0x460` | 0x20-byte key/context used by the frame MAC (`0x1988`) |
| `0x18268`/`0x1826C` | `page+0x480` / `+0x600` | AES-op / HMAC-config staging |
| `*0x1817C` / `*0x18154` | `page+0x700` / `+0x800` | block buffer / pack buffer |

`0x12D20` (`RpmbInit`/key setup) does: `devctl(fd, 0x40101A44, buf16)` (device identity),
build the 20-byte blob at `page+0x440`, `0x2F(op=1, dir=1, data=page+0x440, len=0x20,
key=page+0x040, 0x10)` to program/derive, then validate by reading block 0 (`0x11D4C`); on
failure it logs `RPMB key util: RPMB key gen with KEK %d failed, retrying` (`0x5AE8`),
flips `0x18248` (`0x31`) and retries (`RPMB key util: retrying RPMB key gen with KEK %d`,
`0x5B24`).

**This is the first static evidence of RPMB key material in a shipped image**: two 16-byte
candidate KEKs sit in `.rodata` immediately before the `"\tWinchester"` model string, and
they are handed to services `0x33`/`0x2E`/`0x2F`. What the monitor *does* with them
(derive, ignore, or mix with a fuse secret) is **[U]** — see §9.

Private devctl ABI used against `/dev/hd6` (from `devb-mmcsd-winchester`): `0x40101A44`
(16-byte device id), `0x40011A45` (1-byte variant flag), `0x40011A46` (1-byte block count),
`0xC0081A42` (8-byte `{u16 request, u32 physical_frame_addr}`, retried twice — the actual
RPMB transaction, `0x19CC`). The frame must be in the *uncached* mapping (`0x18190`);
`mem_offset64` converts it to a physical address for DMA. A userland process cannot do
this, which is why the write counter is not observable from userland (§4.4). **[P]**

---

## 3. The bootloader read path (`hd0head.bin`) re-derived

### 3.1 Chain

```
0x80014904  boot main
  0x8001376C                    early init
  0x80010D3C  -> 0x80017C38(0)  service 0x31: KEK/counter state   (fail -> status 0x5b)
  0x80010EB8                    "RPMB provisioned?" (OTP/eMMC state, ends in 0x80017FD8)
  0x800112DC  -> 0x80011196     **catalogue**: read blocks, build the record table
    0x80011196: for blk in 0..N-1:
                  free[blk] = 0xE0
                  0x80010A34(blockbuf, 0xE0, key=0x80033760, tbl=0x80025FB0, blk)
                  0x80011166 scan for magic, add descriptor (blk, offset, len)
  0x800090A0(1)                 only if 0x80010EB8 said "not provisioned":
                                  create/update BOOTCTR_FCTRY = 0x32
...
  0x800149BE  0x8000911A        selector  (0 / 1 / 2)
  0x800149C4  0x80014548        ECDSA verifier (product path, flag=0)
  0x80014A1E -> 0x8001453C      retry loop (flag=1) -> driver 0x800142A8
                 0x800142A8 -> 0x8000911A  selector
                 0 = ECDSA (0x80014716), 1 = CRC gate (0x80013FB6) + CRC (0x80014788), 2 = fail
  0x80014A3E                  after a successful retry verify:
                 if selector != 0:  0x8000911E  **decrement the counter**
                 if decrement fails: "BootModeCount expended" -> halt 0x800094BE
                 0x80017F64  post-boot bookkeeping
                 -> 0x80014A90 enter OS
```

`0x800090CE` (`0x8000911A` = `movs r0,#0; b 0x800090CE`) — selector: **[P]**
`0x80011690("BOOTCTR_FCTRY", &v, 4, &len)`; `ret == 3` → `0` (record absent);
`ret == 0 && len == 4 && (u32)v in 1..0x32` → `1`; everything else → `2` (and
`0x80014C98(0,0x5B)` status).

`0x80009100..0x80009114` verbatim:
```
cbz r4,#0x8000910a ; ldr r0,[sp] ; cmp r0,#0x32 ; bhi 0x8000910a ; str r0,[r4]
ldr r0,[sp] ; cbz r0,#0x80009116 ; cmp r0,#0x32 ; bhi 0x80009116 ; movs r0,#1 ; pop
```

### 3.2 `0x80011690(name, dst, dstlen, &outlen)` **[P]**

```c
desc = 0x8001112C(name);                       /* linear scan, stride 0x1C, memcmp 14 B */
if (desc == 0) return 3;                       /* "no such record"  -> selector 0 */
if (dstlen < desc->len) return 2;              /* caller buffer too small */
r = 0x80010A34(blockbuf, 0xE0, key=0x80033760, freeTbl=0x80025FB0, desc->block);
if (r) return 6;                               /* read/MAC failure */
*outlen = desc->len;
p = blockbuf + desc->offset;
if (memcmp(desc->name, p + 4, 14)) return 5;   /* header name mismatch */
memcpy(dst, p + 0x18, desc->len);
return 0;
```

### 3.3 `0x80010A34(blockbuf, 0xE0, key, tbl, blk)` **[P]**

1. `param_2` **must** equal `0xE0`.
2. `0x8001101C(noncebuf=0x8002C6E0, 0x20)` → `0x80017D24` → service `0x12` in `0x80`-byte
   chunks; requires `(buf & 0x1F) == 0` and `len > 0x1F`.
3. `memset(frame, 0x200)`; `frame[0x1FE] = 4` (read); `frame[0x1F8] = blk`;
   `0x80010770` (reverse 256 B + byte-swap); copy 16 nonce bytes to `0x1E4`.
4. `0x8001080E` → eMMC request `4` (data read), then `0x80010CFC` → `0x8000D0EA` for the
   completion/response half of the transaction.
5. Reject unless `frame[0x1FC] == 0`.
6. `memcmp(nonce, frame+0x1E4, 16) == 0` (replay check, full 16 bytes).
7. `saved = frame[0xC4..0xE3]`; `0x800107B6` recomputes (`0x80017758(data=frame+0xE4,
   len=0x11C, key, 0x20, out=frame+0xC4, 0x20, op=3, dir=0, 0)`) and
   `memcmp(saved, frame+0xC4, 0x20) == 0`.
8. Fill `dst` with the expected pad byte (`0x8000D0A8` → `0x00` or `0xFF`), then
   `memcmp(dst, frame+0xE4, 0xE0)`. **Equal → return 0.**
9. Otherwise `0x80011048(dst, 0xE0, frame)` — an *authenticated rewrite* (§3.4); if it
   returns non-zero the read still returns 0.

### 3.4 What a reader accepts, and what a valid write must satisfy

**A read is accepted iff** (all proven): the 512-byte frame arrives with `result == 0`;
the 16-byte nonce equals the freshly requested one; the 32-byte MAC at `0xC4` equals the
monitor's MAC over `[0xE4,0x200)`; and (for the bootloader's fast path) the unused part of
the block matches the platform fill byte. A *failure* of the last two does not fail the
read — it triggers a repair write:

```
0x80011048(dst, 0xE0, frame)
  if (*keyctx != 1 || service 0x31(1,&b) fails) return 0   ; key not usable
  service 0x33(key = keyctx+0x60, 0x10)                ; load the 16-byte block key
  MAC over frame[0xE4..0x1C4) with key keyctx+0x20, 0x10 -> frame[0x10]
  memcmp(frame[0x10], frame[0x1C4], 0x20)              ; must match the existing block HMAC
  service 0x2F(op=0, param_2=0, dir=1, len=0, block=frame+0xE4, 0xE0,
               frame, 0x10, key=keyctx+0x60, 0x10)
```
(`op = 0` is hard-coded at the call site `0x800110EE`–`0x800110F4`; the OS supplies the same
value for Winchester from `0x129CC` (`strstr(confstr(0x205), "Winchester")`).)
So **the bootloader may write during a boot read** — a stdout-visible "read" is not
guaranteed write-free at the hardware level. It only ever rewrites the same block.

**A valid write (from any world) must satisfy:**

| Requirement | Enforced by | Evidence |
|---|---|---|
| current write counter in `frame[0x1F4]` | eMMC device | `0x80010844` reads the counter (request `2`) into the frame; `0x80010B2E` stores it; OS does the same in `0x1C00` |
| fresh 16-byte nonce in `frame[0x1E4]`, echoed | device + caller | `0x8001101C`, `memcmp` at `0x80010AB4` |
| 32-byte frame MAC over `[0xE4,0x200)` (284 B) with the RPMB key | monitor (`0x2E`) + device | `0x800107F0`/`0x1988` |
| 32-byte block HMAC over `[0xE4,0x1C4)` (224 B) with a 16-byte block key, stored at `0x1C4` | monitor + caller | `0x80011048`, `0x12AC`, `0x12BF0` |
| request `3` (write), block count `1`, `result 0` | device | `0x80010B2E`, `0x11C00` |
| block address inside the partition | device | (no software range check) |

Non-secure code never computes a MAC itself; it hands pointers to the monitor. That is
why a `/dev/rpmb` write needs no key: the monitor is the MAC engine and the driver is only
a courier.

---

## 4. Read-only capture procedure (MAIN SESSION)

**MAIN SESSION ONLY** — this is the only part of this document that touches the live unit,
and it is read-only. It targets the **connected 64 GB reference** first (baseline), then
the spare.

### 4.1 Safety rules for this procedure

1. Use `cat` / `dd if=` / `ls` only. **Never** use `>`, `>>`, `tee`, `cp … /dev/rpmb/…`,
   `truncate`, `rm`, `mv`, `chmod`, or any tool that opens the node `O_RDWR`/`O_WRONLY`.
   `> /dev/rpmb/X` = `O_TRUNC` = record destroyed and block rewritten (§2.3).
2. Do not `ls -l /dev/rpmb` in a loop while also writing. Nothing else in this procedure
   writes.
3. Reads are authenticated and idempotent; each `cat` costs one RPMB read transaction
   (no counter change).

### 4.2 Commands

```sh
cd /home/psyden/rea-work                      # rsa + BerryShell-V4.py live here
ssh -i rsa root@169.254.0.1 'uname -a; pidin ar | grep -i rpmb'
ssh -i rsa root@169.254.0.1 'ls -la /dev/rpmb; ls -l /dev/hd6'
```

Expected on a healthy unit: `rpmb-omap4` in `pidin`, `/dev/hd6` present, `/dev/rpmb` a
directory. **`ls -la /dev/rpmb` is the record catalogue** (name + length + mode). Record
the output verbatim.

Then read the counter record without modifying it:

```sh
ssh -i rsa root@169.254.0.1 'cat /dev/rpmb/BOOTCTR_FCTRY' > bootctr_fctry.bin
ls -l bootctr_fctry.bin
xxd -g1 bootctr_fctry.bin            # expect exactly 4 bytes
```

Interpretation:

| bytes | meaning | action |
|---|---|---|
| file missing / `open` fails, `BOOTCTR_FCTRY` not in `ls` | record **absent** → bootloader selector `0` (product/ECDSA) | this is the *normal product state*; note it and stop |
| `32 00 00 00` | value 50 → selector `1` (factory) | factory mode already active |
| `XX 00 00 00`, `1 ≤ XX ≤ 0x32` | factory, `XX` factory boots left | note the number |
| `00 00 00 00` or `XX > 0x32`, e.g. `FF FF FF FF` | selector `2` → **on the retry branch this unit halts at the next boot** | stop; do not reboot this unit |
| length ≠ 4 | selector `2` | stop |

Cross-check with the loader (read-only, `0xDE` action 0). It cannot distinguish
`0xFFFFFFFF` from "absent" (`72` §3.1), so it is a cross-check only:

```sh
cd ~/rea-work/bbusb
python3 bbusb.py --boot-mode --from-bootrom --password 0000
```

### 4.3 Full catalogue capture (recommended, still read-only)

```sh
python3 $COMMANDCODE_SCRATCHPAD/rpmb-readonly-capture.py \
        --host root@169.254.0.1 --key ./rsa --out ./analysis/playbook-live-2026-10-25
```

The script (reproduced in §10.1) runs only `uname`, `pidin`, `ls`, `cat`, writes one local
file per record, and emits `manifest.txt` with length + SHA-256 per record and a
`BOOTCTR_FCTRY present: yes/no` line. It never opens a node for write.

### 4.4 What this procedure *cannot* tell you

* **The RPMB write counter.** No userland path: `devctl` on `/dev/rpmb` is `ENOSYS`, and
  the eMMC transaction requires a physical frame address and the private `0xC0081A42`
  devctl on `/dev/hd6` (§2.4). **[P]** — the counter is only visible to kernel-mode code.
* **The block number / offset** of a record (the resmgr does not expose it), and therefore
  the raw pre-MAC bytes. Backing up RPMB means backing up *record values*, not the
  partition (§6 R8).
* **Which KEK index** is fused. Indirect hint only: if `rpmb-omap4` is running, whichever
  KEK `0x31` selected authenticated block 0. **[P]** for the mechanism.

---

## 5. Phase 4 write runbook — **MAIN SESSION ONLY**, spare 64 GB unit only

> Everything in this section is destructive. Do not run any of it on the connected 64 GB
> reference. Do not run it on either bricked 32 GB unit.

### 5.1 Preconditions (all must be true; each is checkable read-only)

| # | Precondition | How to check |
|---|---|---|
| P1 | The unit is the **spare working 64 GB** test bed, not the reference | serial/label + `info` modelID `0x06001A06`; physically keep the reference disconnected while this runs |
| P2 | Rooted 2.0.0.4869 OS boots, `pidin` shows `rpmb-omap4`, `/dev/rpmb` lists | §4.2 |
| P3 | `0x2801` still reads `+0x18=0`, `+0x1c=10` (retry branch active) | `./nvram read 0x2801 hex` (`76`) |
| P4 | **Both** A/B regions hold a CRC-valid image with `region[0xd4] == 6` and a correct CRC-32/ISO-HDLC trailer | `89` (track B); verify with the host-side CRC tool + bootloader-equivalent logic (`0x80014788`) |
| P5 | Full backups exist off-device: NVRAM records, all RPMB record values, region 0/1 images | §6 R8 |
| P6 | A **validated** in-place write tool exists | §5.2 |
| P7 | A serial/console or USB-log capture is attached for the reboot | bootrom/loader path (`69`) or UART |
| P8 | The current `BOOTCTR_FCTRY` state is recorded (§4.2) | — |

If P4 is not satisfied, **stop**. Writing the counter is what makes the bootloader take
the CRC path; with a non-CRC region 0 there is nothing to fall back to.

### 5.2 The write mechanism is the weak link — validate it first

A shell redirect cannot express "open `O_WRONLY`, seek 0, write 4" on this OS:

* `> /dev/rpmb/X` → `O_TRUNC` → the resmgr **empties the record and rewrites the block**
  before the write (§2.3), and the following `write()` then operates on a freed table
  entry. **Unsafe.**
* `>> /dev/rpmb/X` → offset = current size (4) → the record becomes 8 bytes → the
  bootloader rejects it (`len != 4`) → selector `2`. **Brick.**
* `: > /dev/rpmb/X` → `O_TRUNC` only. **Brick.**

So the write must come from either:

1. **`dd` with `conv=notrunc,seek=0`**, if `dd` exists on the image (`ls /base/bin/dd`,
   `ls /proc/boot/dd`). This is `O_WRONLY` + `lseek(0)` + `write(4)` — exactly right:
   ```sh
   printf '\x32\x00\x00\x00' > /tmp/bc.bin && od -An -tx1 /tmp/bc.bin   # 32 00 00 00
   dd if=/tmp/bc.bin of=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 seek=0 conv=notrunc
   ```
   Verify `dd`'s QNX build honours `conv=notrunc` and does not open `O_TRUNC` (check with
   `dd --help`; if in doubt use option 2).
2. **A purpose-built native client** (`open(name, O_WRONLY)`, `lseek(0)`, `write(4)`,
   `close`). No QNX ARM toolchain is in the workspace, so this means either a QNX SDP
   build or the same mechanism the previous `FF FF FF FF` write used. **Ask the main
   session which mechanism produced that write** — it is the only *proven* writer on this
   hardware, and it is the one to re-use.

**Rehearse on a sacrificial record before touching `BOOTCTR_FCTRY`:**

```sh
ls -l /dev/rpmb                                   # ensure TEST_PAD does not exist
# create, write, read back, delete -- proves the tool and the whole path
dd if=/dev/random of=/tmp/t.bin bs=4 count=1
dd if=/tmp/t.bin of=/dev/rpmb/TEST_PAD bs=4 count=1 seek=0 conv=notrunc
dd if=/dev/rpmb/TEST_PAD of=/tmp/t2.bin bs=4 count=1
# compare on the host (do not rely on cmp/od being present in the boot image):
#   scp /tmp/t.bin /tmp/t2.bin back, or `cat` them and diff locally
rm /dev/rpmb/TEST_PAD                             # frees the block slot again
```
(The OS normalises a new record name to 14 bytes; `TEST_PAD` is safe. This consumes one
RPMB write and then frees the slot — harmless, and it validates the exact path Phase 4
needs.)

### 5.3 The write

```sh
# 5.3.1 capture the current state (read-only)
ls -la /dev/rpmb
dd if=/dev/rpmb/BOOTCTR_FCTRY of=/accounts/devuser/bootctr_before.bin bs=4 count=1

# 5.3.2 write 0x32 = 50 (little-endian), in place, offset 0, no truncate
printf '\x32\x00\x00\x00' > /tmp/bc32.bin
od -An -tx1 /tmp/bc32.bin                     # must print:  32 00 00 00
dd if=/tmp/bc32.bin of=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 seek=0 conv=notrunc

# 5.3.3 read back (authoritative)
dd if=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 | od -An -tx1     # must print:  32 00 00 00
ls -l /dev/rpmb/BOOTCTR_FCTRY                                # size must be 4
```

* Value **`0x32` (50)** is the right number: it is both the provisioning default and the
  maximum the selector accepts, and the counter is *decremented per successful factory
  boot* (§3.1), so 50 is the largest possible allowance. Values `1..0x32` are accepted;
  `0` and `>0x32` are fatal.
* Read back **must** be exactly 4 bytes with the new value. A length other than 4 is
  fatal — do not reboot; delete the record instead (§5.5).
* Cross-check with the loader: `python3 bbusb.py --boot-mode --from-bootrom --password 0000`
  should now report `status=0, mode=1 (factory), count=50`. (This read cannot distinguish
  a sentinel, so §5.3.3 is authoritative.)

### 5.4 Reboot expectation

On the first power cycle after the write:

1. BootROM → ramloader → `hd0` stage; the stage catalogues RPMB (`0x800112DC`).
2. `0x80014F84` still returns 10 (`0x2801 A=0 < B=10`) → the **retry branch**
   `0x80014A20` → `0x8001453C` → driver `0x800142A8`.
3. Selector = `1` (record present, 1..0x32) → the CRC gate `0x80013FB6` requires
   `region[0xd4] & 0xff == 6` on the region being tried, then `0x80014788` CRC-32/ISO-HDLC
   over `[start, end-4)` vs the u32 at `end-4`.
4. It tries the **requested** region first, then the other. On success it prints
   `BOOT_MODE: FACTORY`, then **decrements the counter** (`0x8000911E`, one RPMB write of
   4 bytes), then `0x80017F64` and enters the OS. The next capture should read `31 00 00 00`.
5. On failure of **both** regions: `0x80014C98(status)` → `0x80015084` → `0x8001217E` →
   **`0x800094BE` = `b .`** — a halt loop (watchdog permitting). There is no "boot
   nothing and wait" state and **no fall-through to an older OS**. See §5.5 / §6.

### 5.5 Rollback

| Situation | Rollback |
|---|---|
| Before the first reboot, OS still running | `rm /dev/rpmb/BOOTCTR_FCTRY` → the record disappears from the catalogue → the bootloader's descriptor lookup returns `3` → selector `0` → the original **ECDSA/product** path is restored. This is a genuine, complete rollback and should be rehearsed (§5.2) before the first reboot. |
| After the first reboot, CRC image booted a TabletOS | rewrite the record from that OS (`0x32`), or `rm` it to return to the product path. Budget can be topped up indefinitely **while a writer-capable OS can boot**. |
| After the first reboot, only a non-TabletOS (Linux) payload boots | no writer: `rm` / rewrite are impossible. The counter can only run down. Treat as one-way. |
| Both regions fail CRC | unit halts in `0x800094BE`; no OS, no `/dev/rpmb`. Recovery needs a pre-OS writer (**none exists**, `72`), eMMC-level access, or a memory-corruption bug in a signed loader (`86`). Treat as **bricked**. |

So the honest framing: **the write is a one-way door unless region 0 boots a TabletOS you
can log into.** The `rm` rollback only exists inside that window.

---

## 6. Risk checklist

| # | Risk | Detail / mitigation |
|---|---|---|
| R1 | **Writing the counter arms the CRC path globally** | The selector is not per-region. Once it is `1`, both regions are verified with CRC only; the ECDSA signatures become irrelevant. Both regions must be CRC-valid *before* the write (`85` §3). |
| R2 | **Both A/B regions must be CRC-valid** | `region[0xd4] == 6` **and** a correct CRC-32/ISO-HDLC trailer over `[start,end-4)`. The live `hd1` has `+0xd4 = 0` (§0/`85`); `hd2` is zero at the IFS offset. Verify with the host tool from track B before the write; re-verify after any region write. |
| R3 | **Budget semantics** | `BOOTCTR_FCTRY` = factory boots remaining. Every *successful* factory boot decrements by one (`0x8000911E`). Failures do not decrement (they halt). Reaching `0` prints `BootModeCount expended` and halts. Keep a reserve; top up from any bootable TabletOS. |
| R4 | **Monotonic RPMB write counter** | Each authenticated write (record write, block repack, record delete, bootloader decrement/repair) bumps the eMMC RPMB write counter by one. The counter cannot be reset and saturating it permanently write-protects the partition. It is not readable from userland (§4.4). Keep the number of writes small; do not loop write/delete experiments. |
| R5 | **Value/length mistakes are fatal** | `0`, `>0x32`, `0xFFFFFFFF`, or a length ≠ 4 ⇒ selector `2` ⇒ halt on the retry branch. Use `0x32`, verify the read-back length is 4, never use `>`, `>>` or `O_TRUNC`. |
| R6 | **Unit inventory** | (a) connected **64 GB reference** — root, 2.0.0.4869, `0x2801 A=0`; **never write `BOOTCTR_FCTRY` on it**; (b) **spare working 64 GB** — the only permitted Phase 4 target; (c) two **bricked 32 GB** units (one expired with `0xFFFFFFFF`) — recovery targets, physically disconnected for Phase 4 so they cannot be confused with the test bed. |
| R7 | **Explicit prohibition** | Do not write `BOOTCTR_FCTRY`, delete it, or write any NVRAM record on the connected reference. Its only role is the read-only baseline of §4. |
| R8 | **Backup scope** | NVRAM: `0x2004`, `0x2040`, `0x2074`, `0x2801`, `0x2802`, bootrom metrics (`69`/`76`). RPMB: **record values only** (names + bytes + SHA-256), *not* the raw partition — you cannot read raw frames, and a raw copy is unusable without the key. Regions: full `hd1`/`hd2` images off-device. Bootrom/loader access path documented (`69`). |
| R9 | **The bootloader may write during a read** | `0x80010A34` inspects the block and calls `0x80011048` (authenticated rewrite) if the pad bytes or MAC do not match (§3.4). Harmless for the value but it consumes counter increments and can rewrite a block the OS created with a different pad pattern. If the CRC image never boots, expect up to one extra write per boot. |
| R10 | **`0x2801` is not the blocker** | Already `A=0`/`B=10`. Do not patch it (`76`). If it ever reached `A >= B` the normal path would run instead, where *any* nonzero selector goes to the nuke path (`0x800149C2`). |
| R11 | **Writing from the OS is not the same as writing from the bootloader** | The OS writes through the monitor with its own 16-byte keys (§2.4); the bootloader validates with a 32-byte context at `0x80033760`. If the two disagree the record will not validate at boot — this is exactly what the read-back + loader cross-check in §5.3 detects *before* a reboot. |
| R12 | **Do not create the record on a unit that never had one** unless the goal is factory mode | On the reference unit the record is expected **absent**; creating it there would arm the CRC path. Read-only there. |

---

## 7. Reconciliation with `71` §7b, `72`, `85` Phase 4

Legend: **C** confirmed, **Q** qualified, **X** refuted.

| Source | Claim | Verdict | Evidence |
|---|---|---|---|
| 71 §7b | `BOOTCTR_FCTRY` is an authenticated RPMB record | **C** | record store + MAC in both images (§1–§3) |
| 71 §7b | accepted range `1..0x32`, `0` and `>0x32` fail, provisioning writes `0x32` | **C** | `0x80009100`–`0x80009114`, `0x800090B6` (`movs r0,#0x32`) |
| 71 §7b | "**This stage only reads/provisions/deletes the record — it never decrements it**" | **X** | `0x8000911E` = read via `0x800090CE`; if selector `1` and value ≠ 0 then `value-1` and `0x800115E6(name,&v,4)`; called from `0x80014A3E` on every successful factory boot, and on failure the stage prints `BootModeCount expended` (`0x80014B84`) and halts at `0x800094BE` |
| 71 §7b | `0x800090A0(1)` provisions an **absent** record at `0x32` and does not fire for `0xFFFFFFFF` | **Q** | `0x800115E6` is create-**or-update** (`0x80011644` takes the update branch when the name is found). The gate is `0x8001494E`: it runs only when `0x80010EB8` says "RPMB not provisioned" (an OTP/eMMC state check ending in `0x80017FD8`), not when the record is absent. On a provisioned unit it is never called, which is why `0xFFFFFFFF` is not repaired |
| 71 §7b | 0x18-byte header: magic `0x5983BC2D`, 14-byte name at `+4`, u16 length at `+0x16`, data at `+0x18` | **C** | `0x80013A14`, `0x80013A9C`, `0x80013D44`, `0x800115E6` |
| 71 §7b | `0x80011166` locates the magic, `0x8001124E` repacks, `0x80010B2E` writes the block back | **C** | all three disassembled |
| 71 §7b | descriptor: `+4` name, `+0x12` u16 block, `+0x16` u16 length, `+0x18` u16 offset | **C** | `0x8001112C` stride `0x1C`, `0x80011690` uses `+0x12/+0x16/+0x18` |
| 71 §7b | `0x80010A34` is a 512-byte authenticated block read, steps 1–5 | **C** | §3.3; full offset map now pinned (§1.1) |
| 71 §7b | step 6: on MAC mismatch `0x80011048` writes the block back **and re-reads** | **Q** | the write-back is real (block HMAC `0xE0` + service `0x2F`, §3.4) but there is no re-read; the read returns success if the repair succeeds. It also fires on a *pad/content* mismatch, not only a MAC mismatch |
| 71 §7b | all service calls go through `0x800183DC` = **ARM `smc #0`** with `r6=0xFF`, `r12=0` | **X** (immediate only) | `0x80009078`: `movs r6,#0xFF; mov ip,#0; mcr p15,c7,c5,6; dsb; isb; dmb; **smc #1**`. The register ABI claim is right; the immediate is `1`, and an I-cache invalidate precedes it |
| 71 §7b | the `0x2E` request carries a 32-byte key/context pointer `0x80033760` | **C** | `0x800107F0` (`r1=0x11C`) + stack args `{key, 0x20, 3, 0, 0}`; `0x80011048` additionally flushes `key+0x60` (16 B) for `0x33` and uses `key+0x20` (16 B) for the block HMAC |
| 71 §7b, HANDOFF §7 | "the key is not in the static image" / "the RPMB MAC key is not in the static images" | **Q** | the *bootloader* key context is a runtime buffer, but `rpmb-omap4` ships two 16-byte candidate KEKs in `.rodata` (`0x5A84`, `0x5A94`) and a 32-byte ASCII fallback, all handed to `0x33`/`0x2E`/`0x2F`. Whether the monitor treats them as key material or as selectors is **[U]** (§2.4, §9) |
| 71 §7b | writable from a rooted OS via `/dev/rpmb` | **C** | `0x4A48` write + `0x13D44` + `0x11C00` |
| 71 §7b | the remaining question is a reader/parser bypass | **C** (as a question) | answered in §8: no key-less forgery found |
| 71 §7b | the record metadata is runtime-populated, not static file data | **C** (and stronger) | it is populated by a **media catalogue** at every boot (`0x800112DC` → `0x80011196`), so the bootloader finds records wherever they are |
| 72 §3, §5 | no recoverable loader implements `0xDE` action 2; the OMAP setters are dead code | **C** (accepted, not re-run) | not re-audited here (out of track E scope); the *bootloader* has a live writer (`0x800115E6` + `0x8000911E`) while the loaders do not |
| 72 §3.1 | `LDR_01` masks `0xFFFFFFFF` as "absent", newer readers and `0x800090CE` return `2` | **C** | consistent with `0x8000910A`–`0x80009116` |
| 72 §6 | the bootloader's `0x800090A0(1)` "can provision an absent record at `0x32`, but it does not fire for `0xFFFFFFFF`" | **Q** | same as the `71` row above: gated on `0x80010EB8`, not on record presence |
| 85 Phase 4.1 | "Full backup: NVRAM, RPMB, region 0/1, bootrom/loader access" | **Q** | RPMB backs up as *record values* only; there is no raw-partition read and the write counter cannot be read (§4.4). A raw image would be unusable without the key |
| 85 Phase 4.3 | write `BOOTCTR_FCTRY` `1..50` via `/dev/rpmb`; read back with `0xDE` action 0 "or the RPMB tooling" | **Q** | feasible, but the write must be an **in-place 4-byte write at offset 0** (`dd conv=notrunc` or a native client); a shell `>`/`>>` is a brick (§5.2). `0xDE` action 0 cannot distinguish `0xFFFFFFFF` from absent — use `/dev/rpmb` read-back as authoritative |
| 85 Phase 4.4 | reboot: retry loop reads selector `1` → CRC, region 1 then region 0 | **C** + **Q** | selector logic confirmed; add: a successful factory boot **decrements** the counter, and *any* failure in the retry branch halts at `0x800094BE` rather than falling through |
| 85 Phase 4.5 | "if both fail … recover via the bootrom/loader or re-flash" | **Q** | no pre-OS writer exists (`72`), the loader cannot write the counter, and the re-flash path is `resetqnxosregion`, which touches NVRAM `0x2074` only (`71` §7c). Treat "both fail" as bricked |
| 85 §3 | do not write the counter while region 0 is ECDSA-only; both regions must be CRC-valid first | **C** (reinforced) | §5.1 P4, §6 R1/R2 |
| 85 §0 | live `region[0xd4]=0`, `hd2` zero at the IFS offset, `0x2801 A=0` | **C** (from prior sessions, unchanged) | out of scope here |
| 85 §1 | the CRC path is gated by the RPMB record + `region[0xd4]==6` + CRC | **C** | §3.1 |

---

## 8. Parser audit — is there a length/offset/nonce bug that yields a forged record without the key?

Bounded answer: **not found**, and the reason is structural rather than lucky. Concrete
results:

1. **All copy geometry is non-secure-code-owned, not media-owned.** `0x80011690` copies
   `desc[+0x16]` bytes from `block + desc[+0x18] + 0x18`. `desc` is an entry of the
   runtime table built by the catalogue from *validated* headers: `0x80011196` computes
   the offset as `header_ptr - blockbase` and refuses to add the record unless
   `free[block] >= 0x18 + len` (`0x800111FE`/`0x80011200`, message
   `RPMB rec: more records in block than possible`). So `offset + 0x18 + len <= 0xE0` for
   every descriptor that exists. The on-media `len` is *not* used to copy anything; only
   the 14-byte name is compared (`memcmp`/`strncmp`). A crafted header therefore cannot
   enlarge or redirect the copy. **Proven.**
2. **No MAC truncation.** The frame MAC compare is a full 32-byte `memcmp`
   (`0x80010ADC`, `0x11d4c`/`0x11f58`), and the comparison helper `0x80018B40` is a normal
   word/byte memcmp that returns a full difference (no early-exit/zero-length path that
   could be abused for equality).
3. **No nonce truncation.** 16 bytes are copied in and 16 bytes compared
   (`0x80010AB0`: `movs r2,#0x10`). Service `0x12` is asked for `0x20` bytes but only the
   first 16 are used; the extra 16 are unused, not compared — harmless.
4. **MAC coverage closes field substitution.** `0x11C` bytes from `0x0E4` include the
   nonce, counter, block address, block count, result and request type. Replaying a block
   read from another address or as another request type fails the MAC. The block HMAC
   layer is redundant against this class of attack.
5. **The only reachable "write" from a read is the repair path** (`0x80011048`), and it can
   only rewrite the block it just read, with the monitor's own MAC.
6. **Latent defects that are *not* reachable from media** (worth recording, not
   exploitable by an attacker who controls only RPMB contents):
   * `0x80011690` never checks `desc[+0x18] + 0x18 + desc[+0x16] <= 0xE0`; a malformed
     *descriptor* (i.e. a bug in the catalogue, or a future table format) would over-read
     the `0xE0` block buffer into adjacent secure-shared RAM. The catalogue bounds check
     is what makes this unreachable.
   * `0x80010A34` does not range-check `desc[+0x12]` (block number) against the
     catalogued size; an out-of-range block simply returns a MAC/result failure.
   * `0x8000D0A8`'s pad-byte probe reads OMAP control registers; if the platform selected
     `0xFF` padding, every boot read of a zero-padded block would take the repair path
     (an unexpected write, not a bypass).

**What would close the audit (needs X):** the secure-monitor image (the `0x40027xxx` /
hidden page handlers behind `smc #1`, services `0x12/0x2E/0x2F/0x31/0x33`) to confirm
(a) the MAC algorithm and key derivation, (b) whether a caller-supplied key buffer is
honoured, and (c) whether `r2` (domain `4` vs `5`) actually partitions key space between
the OS and the bootloader. Until then, "no key-less forgery from the media side" is the
strongest supportable statement; "the non-secure side cannot forge a record" rests on the
monitor, not on the parser.

---

## 9. Residual unknowns

| # | Unknown | Impact | How to close |
|---|---|---|---|
| U1 | The monitor's RPMB key derivation and MAC algorithm; whether the `.rodata` KEK constants at `rpmb-omap4:0x5A84/0x5A94` are the actual key material | determines whether a MAC can be produced off-device | read the secure-world page (`0x40027xxx`) or run a controlled experiment on a scratch record |
| U2 | Whether `r2` = `4` (OS) vs `5` (bootloader) selects a different key/domain | explains why both implementations pass different key lengths (`0x10` vs `0x20`) | same as U1 |
| U3 | Whether the OS and the bootloader really address the same RPMB block space (`/dev/hd6` vs the bootloader's block enumeration) | if not, an OS-written record is invisible to the bootloader | empirical: §4.2 on a unit where the record exists, then the loader `0xDE` read; or a record written by the bootloader and read by the OS |
| U4 | The write counter value and its remaining headroom | wear/rollback budget | needs a kernel-mode helper using `0xC0081A42` (out of userland reach) |
| U5 | Exact JEDEC wire mapping done by `devb-mmcsd` (`0x8000D0EA`/`0x8000DF48` in `hd0head`) | documentation only | disassemble those two functions with the same method |
| U6 | What `0x80017F64` does after a successful factory boot (commit/latch?) | could add a write or a one-shot latch | disassemble `0x80017F64` (not present as a function in `thumb.txt`) |
| U7 | The pad-byte probe result on this unit (`0x00` vs `0xFF`) | whether a boot read rewrites the block | infer from the fact that an OS-written record booted historically (meaning `0x00`), or observe RPMB traffic |
| U8 | Which mechanism performed the historical `FF FF FF FF` write | the only *proven* writer | ask the main session; then re-use it instead of §5.2 option 1 |

---

## 10. Reproduction commands

```sh
# 0. environment
cd /home/psyden/rea-work
P=/tmp/commandcode-1000/-home-psyden-rea-work/6e866a18-07be-42a2-b01a-cda985fc6cae/scratchpad

# 1. bootloader side (ARM/Thumb, base 0x80000000, file offset == addr - 0x80000000)
python3 $P/d0.py 0x80009078 0x800090a0 arm      # SMC veneer: r6=0xFF, r12=0, smc #1
python3 $P/d0.py 0x800090a0 0x80009120 thumb    # provision/delete + selector + range check
python3 $P/d0.py 0x8000911e 0x80009148 thumb    # decrement (read, -1, write back)
python3 $P/d0.py 0x800107b6 0x80010810 thumb    # frame MAC request (len 0x11C, key 0x20)
python3 $P/d0.py 0x80011048 0x80011120 thumb    # block HMAC + service 0x2F rewrite
python3 $P/d0.py 0x80011196 0x8001122c thumb    # catalogue: blocks -> descriptors
python3 $P/d0.py 0x800112dc 0x80011350 thumb    # catalogue entry point
python3 $P/d0.py 0x80011690 0x80011718 thumb    # record read
python3 $P/d0.py 0x80010a34 0x80010b34 thumb    # block read + nonce/MAC checks

# 2. resmgr side (QNX ELF, vaddr == file offset)
cd /home/psyden/rea-work/playbook-dev/dumped4869ifs/proc/boot
readelf -lhS rpmb-omap4
strings -a -t x rpmb-omap4 | sed -n '/RPMB/p'
python3 - <<'EOF'
import capstone
d=open('rpmb-omap4','rb').read()
md=capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM); md.detail=True
for i in md.disasm(d[0x4a48:0x4c78], 0x4a48):        # io_write
    print("%06x %-8s %s"%(i.address,i.mnemonic,i.op_str))
EOF

# 3. Ghidra (whole resmgr, decompiled + strings + insns)
/home/psyden/tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless $P/ghproj rpmb1 \
  -import rpmb-omap4 -scriptPath $P/gs -postScript DumpAll.java $P/rpmbout -deleteProject

# 4. read-only live capture (MAIN SESSION, §4)
python3 $P/rpmb-readonly-capture.py --host root@169.254.0.1 --key ./rsa \
        --out bootdumps-2026-09-11/analysis/playbook-live-2026-10-25
```

### 10.1 `rpmb-readonly-capture.py` (the §4.3 tool)

```python
#!/usr/bin/env python3
"""READ-ONLY capture of the PlayBook /dev/rpmb record store.
Runs only ls/pidin/uname and `cat` (O_RDONLY). Never opens a record for write."""
import argparse, hashlib, os, shlex, subprocess, sys

LIST_CMDS = [
    ("identity.txt",    "uname -a; echo; pidin ar | grep -i -E 'rpmb|mmcsd'"),
    ("dev-rpmb-ls.txt", "ls -la /dev/rpmb; echo; ls -l /dev/hd6 /dev/rpmb"),
]

def run(host, key, cmd, dry):
    argv = ["ssh", "-i", key, "-o", "BatchMode=yes",
            "-o", "StrictHostKeyChecking=no", host, cmd]
    print("+ " + " ".join(shlex.quote(a) for a in argv), file=sys.stderr)
    if dry:
        return b""
    p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0:
        sys.stderr.write(p.stderr.decode("utf-8", "replace")); raise SystemExit(3)
    return p.stdout

ap = argparse.ArgumentParser()
ap.add_argument("--host", default="root@169.254.0.1")
ap.add_argument("--key", default="./rsa")
ap.add_argument("--out", default="./rpmb-capture")
ap.add_argument("--omit", action="store_true")
ap.add_argument("--dry-run", action="store_true")
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)

for name, cmd in LIST_CMDS:
    open(os.path.join(a.out, name), "wb").write(run(a.host, a.key, cmd, a.dry_run))

listing = open(os.path.join(a.out, "dev-rpmb-ls.txt"), "rb").read().decode("utf-8", "replace")
names = [f[-1] for f in (l.split() for l in listing.splitlines())
         if len(f) >= 9 and f[0].startswith("-") and f[-1] not in (".", "..")]
print("records: %d -> %s" % (len(names), ", ".join(names) or "(none)"))

manifest = []
for nm in names:
    if a.omit:
        break
    data = run(a.host, a.key, "cat " + shlex.quote("/dev/rpmb/" + nm), a.dry_run)
    open(os.path.join(a.out, "rec-" + nm + ".bin"), "wb").write(data)
    manifest.append((nm, len(data), hashlib.sha256(data).hexdigest()))
    print("%-16s len=%d sha256=%s hex=%s"
          % (nm, len(data), hashlib.sha256(data).hexdigest()[:16], data[:16].hex()))

with open(os.path.join(a.out, "manifest.txt"), "w") as fh:
    for nm, n, h in manifest:
        fh.write("%-16s len=%d sha256=%s\n" % (nm, n, h))
    fh.write("\nBOOTCTR_FCTRY present: %s\n"
             % ("yes" if any(m[0] == "BOOTCTR_FCTRY" for m in manifest) else "no"))
```

(Working copy: `$COMMANDCODE_SCRATCHPAD/rpmb-readonly-capture.py`, syntax-checked with
`python3 -m py_compile` and exercised with `--dry-run`.)

---

## 11. One-line answers

1. **Interface:** `/dev/rpmb` is a directory resmgr; each record is a file; `open/read/
   write/unlink` only, **no devctl**; records are `root:102 0640`; names ≤ 13 chars here;
   data ≤ 200 B, 4-aligned; `BOOTCTR_FCTRY` must be exactly 4 bytes. **[P]**
2. **Read path:** `0x80011690 → 0x8001112C → 0x80010A34 → 0x800107B6/0x80017758`; accepts a
   frame only with `result 0`, a fresh 16-byte nonce and a 32-byte MAC over `[0xE4,0x200)`;
   a write additionally needs the block HMAC over `[0xE4,0x1C4)` and the current write
   counter; all three are produced by the monitor. **[P]**
3. **Capture:** `ls -la /dev/rpmb`, `cat /dev/rpmb/BOOTCTR_FCTRY` (or the §10.1 script) —
   read-only, no counter change; the write counter itself is not observable. **[P]**
4. **Write:** on the spare 64 GB only, both regions CRC-valid first, in-place 4-byte write
   `32 00 00 00` at offset 0 (`dd conv=notrunc` or the historical writer), read back to 4
   bytes, reboot, expect `BOOT_MODE: FACTORY` and the counter to drop by one. Rollback is
   `rm /dev/rpmb/BOOTCTR_FCTRY` **before** the first reboot (or from a booted CRC
   TabletOS); after that it is one-way unless a TabletOS still boots. **[P]/[I]**
5. **Risk:** value/length errors and a non-CRC region 0 are fatal and unrecoverable
   (no pre-OS writer); never write the connected reference; RPMB backs up as record values
   only. **[P]**
6. **Parser:** no key-less forgery found; geometry comes from a bounded catalogue
   descriptor, the MAC covers the whole frame tail, the nonce is compared in full; the
   gate is the monitor (`smc #1`), which is **[U]**. **[P]/[U]**
7. **Reconciliation:** `71` §7b is substantially correct except the "never decrements"
   claim (**refuted**), the `smc #0` immediate (**refuted**), the "provision only when
   absent" wording (**qualified**), and "the key is not in any static image"
   (**qualified**). `72` stands. `85` Phase 4 stands with four qualifications
   (write mechanism, read-back authority, failure-half-halts, no recovery path).
