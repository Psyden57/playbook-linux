# 78 — `libnvram.so.1` deep dive: stock vs editor-bundled, `/dev/nvram` devctl map, and what `0x2801` really is

Status: read-only static analysis. No device, NVRAM, RPMB or flash writes were made.
Date: 2026-10-22.
Supersedes/extends: `74-nvram-editor-and-0x2801.md`, `75-nvram-2801-password-record.md`,
`76-live-0x2801-audit.md` (§ "Stock vs uploaded library").

## 0. Targets and provenance

**Provenance note (user-confirmed, 2026-10-21):** the editor's bundled
12716-byte `libnvram.so.1` is from **BB10.0.9.388**, a newer QNX TabletOS
build, because `nvram.c` was written against BB10 rather than the TabletOS 2.x
on this unit. The stock `/base/lib/libnvram.so.1` is the 5740-byte TabletOS
build analyzed here. The record devctl ABI used by `nv_get_record` /
`nv_write_record` is compatible between them; the BB10-only additions are the
metrics APIs and the bit-flag helpers. The authoritative record ID→name map
is the TabletOS resmgr's table (`77`), not the BB10 client-side whitelist.

| file | size | SHA-256 | notes |
|---|---|---|---|
| `playbook-live-2026-10-21/device-binaries/libnvram.so.1` | 5740 | `ccab29ce07dd3f8c29a6615aae70af0ae75059360845535da234c9f64aa31688` | stock `/base/lib/libnvram.so.1`, ELF32 ARM, stripped, **ARM state** |
| `playbook-live-2026-10-21/device-binaries/uploaded-libnvram.so.1` | 12716 | `295f1abaf4e221e879c1e20e63920d9f060f378ca5da4b56ba663c4132ac4a3f` | editor-bundled build, ELF32 ARM, stripped, **Thumb-2 state**, 3 section headers |
| `playbook-live-2026-10-21/device-binaries/nvram_resource_manager` | 112873 | — | QNX resmgr behind `/dev/nvram`, ELF32 ARM, runtime base `0x80650000`, stripped (4 sections) |
| `nvram-editor/nvram` | 23585 | — | editor client (not stripped) |
| `nvram-editor/nvram.c` | 18328 | — | editor source |

Tools: `readelf`, `strings`, a hand-written ELF32 dynamic-segment parser (both libraries have
no usable section headers for `.dynsym` in the uploaded/ resmgr cases), and Capstone 5.0.7
(ARM + Thumb + skipdata). All addresses below are **link-time virtual addresses**, which for
these three files are also file offsets for the first `PT_LOAD`. The resmgr's runtime base is
`0x80650000`, so add that to get runtime PCs.

Both libraries build-identify as RIM builds (QNX `QNX_info` section):

| | stock | uploaded |
|---|---|---|
| `DATE` | `2011/06/28-19:04:23-EDT` | `2012/09/14-12:33:13-CDT` |
| `HOST` / `USER` | `gusbuild5` / `builder` | `br369cnc` / `ec_agent` |
| `TAGID` | `1642` | `1169932` |
| `.comment` | `GCC: (GNU) 4.4.2` | none |
| `p_paddr` of first LOAD | `0` | `0x80536000` (fixed load address) |
| section headers | 26 (`.gnu_debuglink` → `libnvram.so.1.sym`) | 3 (`QNX_info`, `.shstrtab`) |

So the 12716-byte library is **not** a modded/patched binary — it is a later QNX 6.6-era
RIM build of the same library with an extended API surface.

---

## 1. Exports / imports

### 1.1 Exported functions (`.dynsym`, `GLOBAL FUNC`, defined)

Stock `libnvram.so.1` — **10** exports, all ARM:

| addr | size | name |
|---|---|---|
| `0x08d0` | 116 | `GetBootromMetrics` |
| `0x0944` | 124 | `nv_write_secure_record` |
| `0x09c0` | 124 | `nv_write_record` |
| `0x0a3c` | 80 | `nv_record_exists` |
| `0x0a8c` | 80 | `nv_is_protected` |
| `0x0adc` | 80 | `nv_get_permissions` |
| `0x0b2c` | 72 | `nv_get_record_count` |
| `0x0b74` | 80 | `nv_delete_secure` |
| `0x0bc4` | 80 | `nv_delete_insecure` |
| `0x0c14` | 204 | `nv_get_record` |

Uploaded `libnvram.so.1` — **20** exports, all Thumb (`st_value` has bit 0 set):

| addr | size | name |
|---|---|---|
| `0x0b77` | 14 | `SetNvramBitFlag` |
| `0x0b85` | 14 | `SetNvramPersistentBitFlag` |
| `0x0b93` | 50 | `IsNvramBitFlagSet` |
| `0x0bc5` | 50 | `IsNvramPersistentBitFlagSet` |
| `0x0c57` | 102 | `nv_get_record` |
| `0x0cbd` | 4 | `NvGetRecordCopy` (alias thunk → `nv_get_record`) |
| `0x0cc1` | 100 | `nv_write_record` |
| `0x0d25` | 4 | `NvUpdateRecord` (alias thunk → `nv_write_record`) |
| `0x0d29` | 100 | `nv_write_secure_record` |
| `0x0d8d` | 52 | `nv_delete_insecure` |
| `0x0dc1` | 52 | `nv_delete_secure` |
| `0x0df5` | 48 | `nv_get_record_count` |
| `0x0e25` | 52 | `nv_get_permissions` |
| `0x0e59` | 52 | `nv_is_protected` |
| `0x0e8d` | 52 | `nv_record_exists` |
| `0x0ec1` | 92 | `allocBootromMetrics` |
| `0x0f1d` | 176 | `readBootromMetrics` |
| `0x0fcd` | 50 | `GetBootromMetrics` |
| `0x0fff` | 94 | `allocOSMetrics` |
| `0x105d` | 176 | `readOSMetrics` |

### 1.2 Imports

| stock | uploaded |
|---|---|
| `__cxa_finalize` (W) | `__cxa_finalize` (W) |
| `__deregister_frame_info` (W) | `__deregister_frame_info` (W) |
| `__register_frame_info` (W) | `__register_frame_info` (W) |
| `_Jv_RegisterClasses` (W) | `_Jv_RegisterClasses` (W) |
| `__get_errno_ptr` | `__get_errno_ptr` |
| `open`, `close` | `open`, `close` |
| `fprintf`, `_Stderr` (OBJ) | — *(debug print removed)* |
| `devctl`, `devctlv` | `devctl`, `devctlv` |
| — | `malloc`, `calloc`, `realloc`, `free`, `memcpy` |

`DT_NEEDED` is `libc.so.3` for both; `SONAME` is `libnvram.so.1` for both.
Hash style differs: stock has `DT_HASH` (SysV, `.hash`) only; uploaded has `DT_GNU_HASH` only.
Both are accepted by QNX `dlopen`/`dlsym`; this is *not* the cause of the failure.

### 1.3 Why the stock library fails with `Error: Symbol not found`

`nvram.c` binds, in this order (`nvram.c:140-222`):

`nv_delete_insecure`, `nv_delete_secure`, `nv_get_record`, `nv_write_record`,
`nv_write_secure_record`, `nv_is_protected`, `nv_record_exists`, `nv_get_permissions`,
`GetBootromMetrics`, **`readOSMetrics`**, `readBootromMetrics`, `nv_get_record_count`.

The first nine all exist in the stock library. The **tenth** — `readOSMetrics` — does not,
and neither does `readBootromMetrics`. QNX `dlsym` failure + `dlerror()` produces exactly the
string the editor printed. So:

* Root cause #1: **`readOSMetrics` is absent** from the 2011 stock build (`nvram.c:203-208`).
* Root cause #2 (would fire immediately afterwards): **`readBootromMetrics` is absent**
  (`nvram.c:210-215`).

`readOSMetrics` uses a devctl the stock library does not implement at all (`0x40080513`, §2.9),
so this is a genuine missing feature, not just a missing symbol wrapper: the stock build
predates the "OS metrics" API. Nothing else in the editor's symbol set is missing, and there
is no hash/visibility problem — `nv_get_record`, `nv_write_record`, `nv_write_secure_record`,
`nv_delete_insecure/secure`, `nv_is_protected`, `nv_record_exists`, `nv_get_permissions`,
`GetBootromMetrics` and `nv_get_record_count` are all present and GLOBAL in the stock lib.

### 1.4 The uploaded library "adds", per record ID

The only *record-specific* code added by the 2012 build is the bit-flag API, which hard-codes
two record IDs (`0x2814`, `0x2019` — §3.2). `readOSMetrics`/`readBootromMetrics` add
per-subsystem metric blobs, not per-record logic. No other record ID appears anywhere in
either library (verified by a full scan for `movw`/`movt`/literal-pool occurrences of
`0x2801, 0x2802, 0x2074, 0x2075, 0x2004, 0x2040, 0x2002, 0x13c4`):

```
stock    : (only 0x2040 at file offset 0x660, which is a .rel.plt GOT offset — coincidence)
uploaded : Thumb movw 0xb02 r3=0x2019 ; 0xb06 r0=0x2814 ; 0xb4e r3=0x2019 ; 0xb52 r0=0x2814
```

Neither library contains `0x2801`, `0x2802`, `0x2074`, `0x2075`, `0x2004`, `0x2040`,
`5060`/`0x13C4`, nor the strings `PIN`, `password`, `boot`, `count`, `iter`.

---

## 2. The `/dev/nvram` devctl map

### 2.1 Command encoding

Every command is a QNX "dcmd" word built as

```
 dcmd = (direction bits) | (sizeof(payload) << 16) | (major << 8) | subcode
 DEVDIR_TO = 0x4000_0000, DEVDIR_FROM = 0x8000_0000
 major = 0x05 for the whole NVRAM class
```

so `0xc0080502` = TO|FROM, 8-byte size hint, class 5, subcode 2 (get record), and
`0x40080501` = TO, 8-byte hint, class 5, subcode 1 (record count).

### 2.2 API → devctl mapping (identical in both library builds)

| client API | dcmd | transport | payload sent | payload returned |
|---|---|---|---|---|
| `nv_get_record_count` | `0x40080501` | `devctl` | 8-byte zeroed buffer | u32 count (+ garbage tail) |
| `nv_get_record` | `0xc0080502` | `devctlv` | hdr{status,id} + caller buffer (len = caller `size`) | hdr{len,id} + record bytes |
| `nv_get_permissions` | `0xc0080503` | `devctl` | hdr{0,id} (8 B) | hdr{permissions,id} |
| `nv_is_protected` | `0xc0080504` | `devctl` | hdr{0,id} | hdr{0/1,id} |
| `nv_record_exists` | `0xc0080505` | `devctl` | hdr{0,id} | hdr{0/1,id} |
| `nv_delete_secure` | `0xc0080506` | `devctl` | hdr{0,id} | hdr{0,id} |
| `nv_delete_insecure` | `0xc0080507` | `devctl` | hdr{0,id} | hdr{0,id} |
| `nv_write_secure_record` | `0xc0080508` | `devctlv` | hdr{0,id} + data | hdr{0,id} |
| `nv_write_record` | `0xc0080509` | `devctlv` | hdr{0,id} + data | hdr{0,id} |
| *(no client API)* | `0x4008050a` | `devctl` | 8 B | 12 bytes copied from `info+0x36` |
| *(no client API)* | `0x4008050b` | `devctl` | 8 B | 12 bytes copied from `info+0x4e` |
| *(no client API)* | `0xc008050c` | `devctl` | hdr{0,0} | 0 — **clears record `0x2801`** (§3.4) |
| `GetBootromMetrics` (stock) | `0x4008050d` | `devctlv` | hdr + 0x0b4-byte buffer | metrics blob, hdr{len,…} |
| `readBootromMetrics`/`GetBootromMetrics` (uploaded) | `0x4008050d` | `devctlv` | hdr + 0x4e8-byte buffer | metrics blob, hdr{len,…} |
| *(no client API)* | `0x4008050e` | `devctl` | 8 B | u32 security mode (live: `0`) |
| *(no client API)* | `0x4008050f` | `devctl` | hdr+data | record-dump helper (`resmgr 0xe080`) |
| `readOSMetrics` (uploaded only) | `0x40080513` | `devctlv` | hdr + 0x1cc-byte buffer | OS metrics blob, hdr{len,…} |

Any other dcmd returns `0x59` (`resmgr 0xb9b4`). The resmgr's dispatch chain is at
`0xb8a4..0xb9b0` (a compare-chain on the full 32-bit dcmd, not a jump table).

### 2.3 Message layout on the wire

The resmgr's `io_devctl` message is 0x10 bytes (`type/combine_len`, `dcmd` at +4,
`nbytes` at +8, `zero` at +0xc); the client payload starts at **+0x10**. The resmgr treats it as:

```
off  size  field
+0x00 .. +0x0f   standard io_devctl_t header
+0x10  i32  status            <- written by the resmgr, read by the client as the return value
+0x14  u16  record_id         <- set by the client (only the low 16 bits are read)
+0x16  u16  pad
+0x18  ...  record data       <- nbytes-8 bytes
```

Evidence: `resmgr 0xb8a0` (`ldr r3,[r8,#0x10]!` → `r8 = ctp+0x10`), `0xb9d0`
(`r2 = ctp->nbytes - 8`), `0xb9dc` (`ldrh r0,[r8,#4]` = record id), `0xb9e0`
(`r1 = r8+8` = data), `0xb9e8` / `0xba18` / `0xba50` (`str r0,[r8]` = status).
The same layout is produced by the library: in the 8-byte `devctl` form the id is stored at
`[sp+0xc]` with the status area at `[sp+8]` (stock `0xa5c`/`0xa7c`, uploaded `0xdaa`/`0xdb2`);
in the `devctlv` form iov[0] = `{&hdr8, 8}` with `hdr+4 = id` (stock `0xc80-0xc9c`,
uploaded `0xc86-0xc9c`, `0xcf0-0xd08`).

`devctlv` is always called as

```
devctlv(fd, dcmd, nA, nB, &iov, &iov, NULL)      /* iov[0]={&hdr,8}, iov[1]={buf,size} */
```

with `(nA,nB)` = `(2,2)` for `nv_get_record`, `(2,1)` for the two write commands and
`(1,2)` for the metrics commands (`nv_get_record`: stock `0xc90/0xc98/0xcac/0xcb8/0xcbc`,
uploaded `0xc86/0xc98/0xc9a/0xcba/0xcbe`). The constants are byte-identical across the two
builds; the resmgr's actual reply bound comes from `ctp->nbytes - 8`, not from these counts.

### 2.4 Return and error codes

Library-side wrapper semantics — identical code in both builds
(stock helper at `0x08a4`, uploaded at `0x0c40`, reached from every API after its devctl):

```
int ret_helper(int rc, int out_word0) {
    if (rc == 0) return out_word0;   /* success: payload word 0 is the API result   */
    if (rc == 9) nv_close();         /* observed extra: rc==9 also closes the cached fd */
    return -1;
}
```

and the fd helper (stock `0x082c`, uploaded `0x0bf8`) is a lazy-open/close cache on
`"/dev/nvram"` with `O_RDWR`:

```
int nv_open(int do_open) {
    if (do_open) { if (g_fd == -1) g_fd = open("/dev/nvram", O_RDWR); }
    else         { if (g_fd != -1) { close(g_fd); g_fd = -1; } }
    return g_fd;            /* -1 on failure; callers turn that into -1 */
}
```

Stock `nv_get_record` additionally does `fprintf(stderr, "NVRAM: nv_get_record( 0x%04X, %d)\n", id, size)`
before its devctl (`stock 0xc30-0xc48`, format string at `0x0cf4`, `_Stderr` via GOT `0x2038`).
The 2012 build dropped that print (no `fprintf` import, no such string).

Argument validation:

| API | stock | uploaded |
|---|---|---|
| `nv_get_record` | `size==0 || buf==NULL` → `errno = 0x16` (EINVAL), return `-1` (`0xc4c-0xc68`) | same (`0xc58-0xc7a`) |
| `nv_write_record` | **none** (`0x9c0`) | `buf==NULL || size==0` → EINVAL, `-1` (`0xcc2-0xce4`) |
| `nv_write_secure_record` | **none** (`0x944`) | same (`0xd2a-0xd4c`) |
| `GetBootromMetrics` | none — treats arg as a buffer | `buf==NULL` → EINVAL, `-1` (`0xfd0-0xfdc`) |
| all `id`-only APIs | no range check | no range check |

So on the stock library a `NULL`/zero-size write is *not* rejected in the client; it is
forwarded. The resmgr rejects it (`resmgr 0x9c10-0x9c34`: `buf==NULL || len==0` → log, `-10`).

Resmgr-side status values seen in the code: `0` (OK / length / boolean), `-10`
(`0xfffffff6`, generic failure, also the "not allowed" code from the write gate), `-15`
(`0xfffffff1`, length > 0x5FE20), `-17` (`0xffffffef`, protected / write-protected),
`-1` (`io_devctl` return).

### 2.5 `nv_get_record` (`0xc0080502`)

```
stock 0xc14:                          uploaded 0xc57:
  if (size==0 || buf==NULL) EINVAL      if (size==0 || buf==NULL) EINVAL
  fprintf(stderr, "NVRAM: nv_get_record(0x%04X,%d)")   (no print)
  fd = nv_open(1); if (fd<0) return -1  fd = nv_open(1); if (fd<0) return -1
  hdr[0]=0; hdr[1]=id                   hdr[0]=0; hdr[1]=id
  iov[0]={&hdr,8}; iov[1]={buf,size}    iov[0]={&hdr,8}; iov[1]={buf,size}
  devctlv(fd,0xc0080502,...)            devctlv(fd,0xc0080502,...)
  return ret_helper(rc, hdr[0])         return ret_helper(rc, hdr[0])
```

Resmgr handler at `0xb9c8-0xb9ec`:

```
*status = 0;
len = ctp->nbytes - 8;  if (len < 0) done;
*status = read_record(*(u16*)(ctp+0x14), ctp+0x18, len);
```

and `read_record` (`resmgr 0x8ff0`) returns **the record's stored length** (not the number of
bytes copied), which is why `nv_get_record(id, buf, 1)` returns e.g. 112 and copies only 1
byte — exactly the two-call "probe then fetch" idiom the editor uses (`nvram.c:314-320`).
`read_record` also returns 0 for "absent" and for "permission bit 2 set".

### 2.6 `nv_write_record` / `nv_write_secure_record` (`0xc0080509` / `0xc0080508`)

Only the dcmd and the 5th argument of the shared writer differ:

```
resmgr 0xbaa4 (0x0509):  if (nbytes-8 < 0) done; *status = write_record(id, data, nbytes-8, 0);
resmgr 0xba68 (0x0508):  if (nbytes-8 < 0) done; *status = write_record(id, data, nbytes-8, 1);
```

`write_record` (`resmgr 0x9be0`) is a *gate*, then `0x913c`:

```
desc = record_lookup(id);                       /* 0x6d2c, walks the 0x38-byte const table */
if (buf == NULL || len == 0) { log; return -10; }
if (desc.id != 0) {                             /* i.e. record is known */
    if (desc.perm & 1) { if (g_writeprotect) return -17; }   /* 0x9c58-0x9c70 */
    if (desc.perm & 4) { log; return -10; }                  /* 0x9c74-0x9c98 */
}
return record_write(id, buf, len, secure);      /* 0x913c */
```

On success the handler calls `nv_value_changed(id)` (`resmgr 0xb7d4`) which special-cases
`0x2074` (`→ 0xc9d0`), `0x2002` (`→ 0x369c(0)`, republish PIN) and `0x280f` (`→ 0x369c(2)`).

### 2.7 `nv_delete_secure` / `nv_delete_insecure` (`0xc0080506` / `0xc0080507`)

```
resmgr 0x9e7c:  if (is_protected(id)) return -17;
                return record_write(id, NULL, 0, secure);   /* len==0 == delete */
```

Delete is literally "write zero bytes", with the same `secure` argument.

### 2.8 Query commands

* `nv_get_record_count` `0x40080501` → `resmgr 0x6ae0`: walks 331 runtime slots
  (`0x14b` entries × `0x1c`) at `base+0x2434` and counts slots with both `[+8]` and `[+0xc]`
  non-zero.
* `nv_get_permissions` `0xc0080503` → `resmgr 0x63b4`: **returns the `u16` permission word
  from the constant table entry for that ID** (`ldrh r0,[entry+2]`); 0 if the ID is unknown.
  This is a *static* capability word, not a per-instance ACL.
* `nv_is_protected` `0xc0080504` → `resmgr 0x641c`:
  `return (perms & 1) ? (g_writeprotect != 0) : 0;`
* `nv_record_exists` `0xc0080505` → `resmgr 0x6538`: runtime lookup, `return desc[+8] != 0`.
* `0xc008050c` → `resmgr 0x9ce8` — see §3.4.
* `0x4008050e` → `resmgr 0xbbe8`: calls the bootrom-metrics reader and returns
  `(metrics[+0x84] != 0)` as the "security mode". Live value: `0`.
* `0x4008050d` → `resmgr 0xbb58`: `obj->read(&local /* 0xb4 */, …)`; reply payload is the
  driver's blob and `*status` is its total length; copy is `min(ctp->nbytes-8, len)`.

### 2.9 Bootrom / OS metrics (the interesting stock-vs-uploaded behavioural delta)

| | stock | uploaded |
|---|---|---|
| function | `GetBootromMetrics(buf)` @ `0x8d0` | `readBootromMetrics(void)` @ `0xf1d`, `GetBootromMetrics(buf)` @ `0xfcd` |
| dcmd | `0x4008050d` | `0x4008050d` |
| iov[1] length | **`0x0b4`** (`0x904`-`0x908`) | **`0x4e8`** (`0xf52`, `0xf56`) |
| buffer owner | caller | library (`allocBootromMetrics`, `0xec1`, a `malloc`/`realloc` singleton; `n == -1` frees it) |
| return | `hdr[0]` = length | `readBootromMetrics` returns the singleton pointer; `GetBootromMetrics` `memcpy`s `0x4e8` bytes and returns `*(u32*)(p+4)` |
| size negotiation | none | after the first read it compares `*(u32*)(buf+4)` with `0x4e8`; if different it re-`alloc`s to that size and re-issues the devctl (`0xf82-0xfac`) |
| OS metrics | **absent** | `readOSMetrics` @ `0x105d`, dcmd **`0x40080513`**, iov[1] length **`0x1cc`** |

**Consequence for the editor** (ties off the `get_bootrom_metrics` SIGSEGV in report 76):
`nvram.c:22` defines `BOOTROM_METRICS_SIZE 0x4FC` and `nvram.c:23` `OS_METRICS_SIZE 0x1D4`,
and `nvram.c:484,491` `fwrite()`s exactly those counts from the pointer `readBootromMetrics()`
returned. The 2012 library allocates only **`0x4E8`** (bootrom) / **`0x1CC`** (OS) bytes, so the
editor over-reads by `0x14` / `8` bytes. The fault text in
`playbook-live-2026-10-21/brom.err` — `ip=ldqnx.so.2@memcpy+0xa4 … ref=78f74000` — is
`fwrite`'s internal `memcpy` reading past the end of the `0x4E8` allocation, not a fault inside
NVRAM. `GetBootromMetrics()` (the `void*` variant the editor also binds but never calls) would
be safe: it returns the true size and `memcpy`s `0x4E8`.

---

## 3. Hard-coded IDs and ID-specific logic

### 3.1 Where IDs appear

| ID | symbol in resmgr table | referenced in library code? | referenced in resmgr code? |
|---|---|---|---|
| `0x2002` | `NV_OSSTORE_PIN_NUM` | no | yes — `0x3788` (read 8 B, expect len 4 or 8), `0xb808` (`nv_value_changed` → republish) |
| `0x2004` | `NV_OSSTORE_JvmStart_NUM` | no | **no** (name-table entry only) |
| `0x2019` | `NV_OSSTORE_BitFlags_NUM` | **yes, uploaded only** (`0xb02`,`0xb4e`) | no |
| `0x2040` | `NV_OSSTORE_NukeStatus_NUM` | no | **no** |
| `0x2074` | `NV_OSSTORE_OS_REGION` | no | yes — `0xb7f4` (post-write hook → `0xc9d0`), `0xca18`, `0xd690`, `0xd75c` |
| `0x2075` | `NV_OSSTORE_BOOT_MODE_BLOCK` | no | no (name-table entry only) |
| `0x2801` | `NV_OSSTORE_PasswordStruct_NUM` | no | **yes — exactly two sites**: `0x9d30`, `0x9e04` (§3.4) |
| `0x2802` | `NV_OSSTORE_PasswordStructReserve_NUM` | no | no |
| `0x280f` | `NV_OSSTORE_bsn_NUM` | no | yes — `0xb820` (post-write hook → `0x369c(2)`) |
| `0x2812` | `NV_OSSTORE_Nvram_Signatures` | no | yes — `0xe69c` (read up to 0x10000 B) |
| `0x2814` | `NV_OSSTORE_PersistentBitFlags_NUM` | **yes, uploaded only** (`0xb06`,`0xb52`) | no |
| `0x281b` | `NV_OSSTORE_SecureRecordsHash_NUM` | no | yes — `0xea9c`, `0xeb60` |

`0x2004` and `0x2040` are *not* special-cased anywhere in the user-space NVRAM stack; they
are ordinary IDs (their boot-time significance lives in the boot stage, per `74`/`75`/`76`).

### 3.2 The uploaded library's ID whitelists (`0xb77`..`0xc57`)

Reconstructed source of the added API (Thumb, `0xb28` is the shared worker):

```c
typedef struct { u32 size; u8 *bits; } NVFLAGS;      /* 0xa3c alloc, 0xa68 free */

static NVFLAGS *nv_flags_alloc(int persistent)       /* 0xaf4 */
{
    NVFLAGS *p = nvalloc(0x80);                       /* 0xa3c(0x80): calloc(0x80) */
    if (!p) return NULL;
    u32 id = persistent ? 0x2019 : 0x2814;             /* 0xb02-0xb10 */
    if (nv_get_record(id, p->bits, p->size) < 0) { free(p); return NULL; }
    return p;
}

static int nv_flags_set(int id, int on, int persistent)   /* 0xb28 */
{
    NVFLAGS *p = nv_flags_alloc(persistent); if (!p) return -1;
    if (on) set_bit(p, id); else clear_bit(p, id);        /* 0xa80 / 0xaaa */
    u32 rec = persistent ? 0x2019 : 0x2814;
    int ok = (nv_write_record(rec, p->bits, p->size) >= 0);  /* 0xb60 */
    free(p);
    return ok;
}

int SetNvramBitFlag(int id, int on)              /* 0xb77 */
{   if (id > 0x28) return 0;  return nv_flags_set(id, on, 0); }        /* 0xb76-0xb83 */
int SetNvramPersistentBitFlag(int id, int on)    /* 0xb85 */
{   if (id > 3)    return 0;  return nv_flags_set(id, on, 1); }        /* 0xb84-0xb91 */
int IsNvramBitFlagSet(int id, u8 *out)           /* 0xb93 */
{   if (id > 0x28) return 0;  ... *out = test_bit(p,id); ... return 1; }
int IsNvramPersistentBitFlagSet(int id, u8 *out) /* 0xbc5 */
{   if (id > 3)    return 0;  ... }
```

with (`0xa86-0xa98`, `0xadc-0xaf0`)

```
set/clear/test bit  = id & 0x1f, word index = (id & 0x1f)      /* not id>>5 */
```

i.e. all flags live in `word[0]` of the `0x80`-byte record; ids `0x20..0x28` therefore alias
bits `0..8`. Both flag records are written with the **insecure** `nv_write_record`
(`0xb60` → `0xc0080509`), never `nv_write_secure_record`.

* Whitelists: **`id <= 0x28`** for the volatile bit-flag record `0x2814`, **`id <= 3`** for the
  persistent record `0x2019`. Out-of-range ids return `0` (no error), they do not call the driver.
* This is the *only* per-ID logic in either library. The stock build has none at all.

### 3.3 Permission word ("permissions") per ID

`nv_get_permissions()` returns the `u16` at `entry+2` of the resmgr's constant table
(`resmgr 0x63b4`). The table is 207 × `0x38` bytes starting at file offset `0x12d54`
(`{ u16 id; u16 perm; u8 pad; char name[51]; }`). Distribution: 166 entries `0x0000`,
40 entries `0x0001`, one entry `0x0003`.

Bit semantics, from the two consumers:

| bit | meaning | evidence |
|---|---|---|
| `0x1` | *protected*: writes refused with `-17` while `g_writeprotect != 0`; `nv_is_protected()` returns `(perm&1) && g_writeprotect` | `resmgr 0x9c58-0x9c70`, `0x641c-0x6448` |
| `0x2` | not tested anywhere in this build; present on `0x280f` (`bsn`) only | `0x280f` perm = `0x0003` |
| `0x4` | *read-denied*: `read_record()` logs and returns 0 | `resmgr 0x900c-0x9030` (no entry has it) |

Key IDs: `0x2002 PIN` = `0x0001` (protected), `0x2801` = `0x0000`, `0x2802` = `0x0000`,
`0x2812 Nvram_Signatures` = `0x0001`, `0x2814` = `0x0000`, `0x281b` = `0x0001`.
`0x2801` therefore reports `permissions = 0`, `is_protected = 0` — matching the live audit.

### 3.4 The one place `0x2801` is special-cased: dcmd `0xc008050c`

`resmgr 0x9ce8` (dispatch entry `0xbb4c`) is a "clear / re-seed the password record" verb. It
is not exposed by either `libnvram` build (no wrapper in the symbol table), so only a direct
`devctl(fd, 0xc008050c, …)` reaches it:

```
p = malloc(0x70);                                    /* 0x9cf8 */
if (!p) return -10;
if (read_record(0x2801, p, 0x70) == 0) { log; free; return -10; }   /* 0x9d30, 0x9d3c */
fmt = *(u32*)p;                                      /* 0x9d74 */
if (fmt == 0x00010002) {                             /* v1 layout, 0x3c bytes */
    *(u32*)(p+0x28) = *(u32*)(p+0x2c);               /* 0x9d88 */
    memset(p+0x08, 0xff, 0x20);                      /* 0x9d90 */
    write_record(0x2801, p, 0x3c, 1);                /* 0x9e04, secure=1 */
} else if (fmt >= 0x00020000) {                      /* v2 layout, 0x70 bytes */
    *(u32*)(p+0x18) = *(u32*)(p+0x1c);               /* 0x9db4 */
    memset(p+0x30, 0xff, 0x40);                      /* 0x9dbc */
    write_record(0x2801, p, 0x70, 1);                /* 0x9e04, secure=1 */
} else { log; free; return -10; }
```

Reading it in record terms: for the current v2 record it sets **failed-attempt counter
`+0x18 := max-attempts +0x1C`** and overwrites the 64-byte blob at **`+0x30`** with `0xFF`.
That is "the record exists, the attempt budget is fully consumed, and the password blob is the
'no password' pattern". This is a *lock/clear* verb, and it is the only `0x2801`-specific code
in the OS-side stack. Note it writes with **`secure = 1`**.

### 3.5 `5060` / `0x40` / `PIN` in the binaries

* `5060` (`0x13C4`) does **not** appear as a code constant in either library or in the resmgr.
  It is *data* inside the record (`nv-0x2801.txt`: bytes 12..15 = `c4 13 00 00`). It also
  survives the `0x28`-reset verb above, which only touches `+0x18` and `+0x30..+0x6F`.
* `0x40` and `PIN` likewise appear only as record *content* (`+0x2C` blob length; the PIN is a
  4- or 8-byte value in record `0x2002`).
* The resmgr's PIN handling is entirely about record **`0x2002`**: `0x3788` reads it with an
  8-byte buffer and branches on the returned length (`cmp r0,#4`), matching the strings
  `"found a 32 bit pin"` (`0x11b04`), `"found a 64 bit pin"` (`0x11b34`),
  `"could not find a valid PIN (32 or 64 bit), faking one for PPS object"` (`0x11b64`), and it
  publishes them into `/pps/system/nvram/deviceinfo` as `PIN64` (`0x11c08`) using
  `%s::0x%08X` / `%s::0x%016llX`. `0xb808` re-publishes after any write to `0x2002`.

---

## 4. Normal copy vs secure copy; encryption; is the secure write required?

### 4.1 There is exactly one stored copy per ID

* `nv_get_record` → `0xc0080502` carries **no** secure/insecure selector, and the resmgr's
  `read_record` (`0x8ff0`) has no such parameter either. It returns `desc[+8]` (the stored
  length) from whichever extents the runtime descriptor holds.
* `nv_write_record` (`0x0509`) and `nv_write_secure_record` (`0x0508`) both land in the same
  `record_write()` at `resmgr 0x913c`, allocating extents from the same arena, and both are
  committed through the same block writer `resmgr 0x884c` (single `"NVRE"` magic constant built
  at `0x8870`: `0x4552564e` = `'N','V','R','E'`) using the same `0x200`-byte sector scheme.
* Ergo: `0x2801` is *not* stored twice, and reading it does not require any secure path.

### 4.2 What `secure = 1` actually changes

Only two sites in `record_write` read the `secure` argument:

* `0x9644-0x965c`: `if (secure && class == 6) header[0x0e] = 1;` — i.e. a flag byte in the
  on-flash block header (the 0x20-byte header is CRT-32 sealed by `0x6b54`/`0x6b84` over
  0x18 bytes, CRC at `header+0x18`).
* `0x9a28-0x9a3c`: for `secure == 0` and a large record (`class > 5`) it skips the
  chunk bookkeeping that the secure path performs.

So `secure` is a **provenance/eligibility tag on the stored block**, not a second store, and
not a cipher.

### 4.3 Is `0x2801` encrypted or protected?

No, on both counts:

* Plaintext: `nv-0x2801.txt` decodes field-by-field as a little-endian struct
  (format `0x00020000`, len `0x70`, iter `5060`, two salt words, counters, offset/length,
  64-byte blob). `read_record` → `read_extent` (`resmgr 0x8cc8`) `pread`s sectors and
  `memcpy`s (`0x8e78`) them straight to the caller — no crypto on the read path.
* The only crypto in the resmgr is **verification**: imports `hu_SHA1Begin/End/Msg/Hash`,
  `hu_RSAPKCS1v15VerPadExponent`, `hu_RSAKeySet`, `hu_RegisterSbg56*`, `hu_InitSbg56`
  (from `libhuapi.so.5`, `resmgr` `.dynstr` `0x148a`). It reads `0x2812`
  (`NV_OSSTORE_Nvram_Signatures`, 64 KiB buffer, `0xe69c`) and `0x281b`
  (`NV_OSSTORE_SecureRecordsHash`, `0xea9c`/`0xeb60`) to authenticate the secure set. That is
  an integrity envelope over records, not per-record encryption.
* Permission word for `0x2801` is `0x0000` → neither write-protected (`bit0`) nor read-denied
  (`bit2`). Live confirmation: `get_permissions 0x2801` → `0`, `is_protected 0x2801` → `0`.

### 4.4 Is the secure write required for `0x2801`?

For the *editor's* path: no. `0x2801` is an ordinary record with `perm = 0`, and
`nv_write_record` (`0x0509`) satisfies the write gate at `0x9c58/0x9c74` exactly like
`0x0508` does. The editor's `write`/`patch` commands issue both (`nvram.c:391-401`,
`420-430`) and treat either failure as fatal.

Where `secure = 1` *does* matter is the resmgr's own `0x2801` mutation (`0xc008050c`, §3.4),
which deliberately uses `secure = 1` — i.e. RIM treats the password record as belonging to the
signed/authenticated set even though its permission word is 0 and it is not write-protected.
Practical reading: `nv_write_record` writes will land and be readable, but a `0x2801` written
*without* `secure=1` will not carry the "secure" header bit that the signature/ hash
bookkeeping (`0x2812`/`0x281b`) is built over, so the boot-side verifier may treat it
differently. That last step is an **inference** from the header-flag + signature-record design;
I did not locate the verifier's per-record loop in the resmgr within this pass.

---

## 5. Verdict on `0x2801`

**`0x2801` is `NV_OSSTORE_PasswordStruct` — the device-password record.** It is *not* the PIN,
and there is a separate PIN record.

Three independent lines of evidence, all inside the four target binaries:

1. The resmgr's ID table maps `0x2801` → string `NV_OSSTORE_PasswordStruct_NUM`
   (entry at file offset `0x14a6c`; name at `0x14a71`) and `0x2802` →
   `NV_OSSTORE_PasswordStructReserve_NUM` (`0x14aa4`/`0x14aa9`). The table also gives
   `0x2002` → `NV_OSSTORE_PIN_NUM` (`0x12f14`/`0x12f19`) and `0x2074` → `NV_OSSTORE_OS_REGION`
   (`0x14804`/`0x14809`) — matching every ID in this task.
2. The only code that names `0x2801` (`resmgr 0x9d30`, `0x9e04`) reads, rewrites and
   *clears the blob of* that record — a credential-reset verb (§3.4), not a generic one.
3. Live data (`nv-0x2801.txt`) is a 112-byte credential struct, not a PIN.

### 5.1 What the fields are

Live record (`nv-0x2801.txt`, 112 bytes, `0x70`), little-endian:

| off | live | meaning |
|---|---|---|
| `+0x00` | `0x00020000` | format tag (v2). The reset verb also understands `0x00010002` (v1, `0x3c` bytes) |
| `+0x04` | `0x00000070` | record length (112) |
| `+0x08` | `0x00000001` | version/flags |
| `+0x0C` | **`0x000013C4` = 5060** | **hash iteration count** for the password blob (see 5.3) |
| `+0x10` | `0xB9A043B5` | salt/challenge word 0 |
| `+0x14` | `0x222D86B5` | salt/challenge word 1 |
| `+0x18` | `0x00000000` | failed-attempt counter A |
| `+0x1C` | `0x0000000A` | attempt budget B (10) |
| `+0x20` | `0xA1E864CE` | sequence/nonce |
| `+0x24` | `0` | — |
| `+0x28` | `0x00000030` | blob offset inside the record |
| `+0x2C` | `0x00000040` | blob length = 64 |
| `+0x30` | `91 1d 08 b1 … cd a1` | the 64-byte blob |

`0x2802` (`PasswordStructReserve`) is a 112-byte reserve copy, all-zero except a stray
`00 b0 30 40 00` near `+0x53`; nothing in the user-space resmgr writes or reads it.

### 5.2 Where `0x2801` is *not* PIN

The PIN is **`0x2002` `NV_OSSTORE_PIN_NUM`**, a 4-byte (32-bit) or 8-byte (64-bit) value:
read with an 8-byte buffer at `resmgr 0x3788-0x379c` (`cmp r0, #4` distinguishes the two
lengths), republished into PPS as `PIN64` after any write (`0xb814` → `0x369c(0)`), with the
strings `"found a 32 bit pin"` / `"found a 64 bit pin"` / `"faking one"` at `0x11b04`,
`0x11b34`, `0x11b64`. `PIN` has `permissions = 0x0001` (protected); `0x2801` has `0x0000`.

**So: `0x2801` is the device *password* record (a hashed credential + attempt budget), and the
device *PIN* is a separate record at `0x2002`.** Any code or notes that treat `0x2801` as
"the PIN" are conflating the two.

### 5.3 `5060` and the `0x40`-byte blob

* **`5060` (`0x13C4`)** is stored *in the record* at `+0x0C` and is the **iteration count of the
  password hash** — the same 5060 used by the bootrom password challenge (reports `69` §3 and
  `75` §1 both reach `5060`/`0x13C4` independently; `76` confirms it live at `+0x0C`).
  It is never a code constant in these four binaries (§3.5), and the resmgr's `0x2801` reset verb
  preserves it. Functionally: the blob at `+0x30` is `H^5060(salt‖password)` (or the platform's
  equivalent salted/iterated digest), and 5060 is what the consumer must use to recompute it.
  *Caveat*: the iteration-count → hash-algorithm binding is derived from `69`/`75`'s boot-stage
  analysis, not from these four files; from these four files I can only assert that the field is
  a per-record iteration parameter that the OS-side code treats as opaque data.
* **The `0x40`-byte blob at `+0x30`** (length in `+0x2C`, offset in `+0x28`) is the credential
  material: the iterated digest of the device password plus its salt context. `all-0xFF` is the
  "no password set" pattern — that is exactly what the reset verb writes
  (`memset(p+0x30, 0xff, 0x40)` at `resmgr 0x9dbc`). It is *not* the PIN (PIN is 4/8 bytes at
  `0x2002`) and it is *not* encrypted on flash.

### 5.4 One-line answer to the posed question

> Is `0x2801` the device PIN, the device password, or something else? What do the 5060 iteration
> count and the `0x40`-byte blob represent? Is there any separate PIN record?

`0x2801` is the **device password record** (`NV_OSSTORE_PasswordStruct`, 0x70 bytes) — not the
PIN. `5060` is that record's hash-iteration count (stored at `+0x0C`), and the `0x40` bytes at
`+0x30` are the salted, iterated password digest (`0xFF…` = no password). Yes, there is a
separate PIN record: **`0x2002` `NV_OSSTORE_PIN_NUM`**, a 4- or 8-byte value that the resmgr
republishes to `/pps/system/nvram/deviceinfo` as `PIN64`.

---

## 6. Notes / corrections relative to prior reports

* `74`/`75`/`76` are consistent with everything above on the *identity* of `0x2801`; this pass
  confirms it from the resmgr's own ID table plus the two `0x2801` code sites, and adds the
  store/copy and permission analysis.
* `76` attributed the bootrom-metrics crash loosely to `memcpy`. Precisely: the editor
  `fwrite`s `0x4FC` bytes (`nvram.c:22`, `nvram.c:484`) from a `0x4E8`-byte library allocation
  (`uploaded 0xf2c`), so `fwrite`'s internal `memcpy` reads 20 bytes past the block. The
  library's own `GetBootromMetrics` would be safe; only the buffer-size constant in the editor
  is wrong (OS metrics are likewise `0x1D4` in the editor vs `0x1CC` in the library).
* `76` observed `get_record_count` printing `27 3` and `get_security_mode` printing `0 3`.
  Both commands return a single 32-bit payload word; the second number is uninitialised stack in
  the editor's `int output[2]` (`nvram.c:494`, `nvram.c:507`). `27` = live record count,
  `0` = live security mode. Cosmetic, but worth not over-reading.
* The stock library is **not** symbol-stripped of the APIs the editor uses except
  `readOSMetrics`/`readBootromMetrics`; it is an older (2011-06-28) build that predates the
  OS-metrics API. `Error: Symbol not found` is reproducible and fully explained by
  `readOSMetrics` + `readBootromMetrics` only.

## 7. Open items

1. The per-record loop of the secure-set verifier (the code that consumes `0x2812`
   `NV_OSSTORE_Nvram_Signatures` and `0x281b` `SecureRecordsHash`, `resmgr 0xe640-0xeb80`) was
   only skimmed; whether it hashes records whose header flag `[0x0e] == 1` (my inference) or an
   explicit ID list is unconfirmed.
2. `resmgr 0x4008050f` → `0xe080` and the `0x4008050a`/`0x4008050b` info copies
   (`resmgr 0x5f98` → `info+0x36` / `info+0x4e`) were not decoded; they are undocumented
   verbs with no client wrapper in either library.
3. The on-flash `NVRE` block format (0x20-byte header + CRC-32 at `+0x18`, extent packing
   `sector = v>>16`, `count = ubfx(v,9,7)`, `offset = v & 0x1ff` — `resmgr 0x8cc8`,
   `0x8d08-0x8d18`) is documented here only to the depth needed to prove there is one arena and
   no encryption.

---

## Appendix A — devctl cheat sheet (copy/paste)

```
/dev/nvram   O_RDWR
hdr8 = { i32 status; u16 recid; u16 pad; }      /* 8 bytes, always first */

0x40080501  get_record_count      devctl (hdr8 out: count in word0)
0xc0080502  get_record            devctlv(hdr8, data[len])
0xc0080503  get_permissions       devctl (hdr8 out: perm word in word0)
0xc0080504  is_protected          devctl (hdr8 out: 0/1)
0xc0080505  record_exists         devctl (hdr8 out: 0/1)
0xc0080506  delete_secure         devctl (hdr8)
0xc0080507  delete_insecure       devctl (hdr8)
0xc0080508  write_secure_record   devctlv(hdr8, data[len])
0xc0080509  write_record          devctlv(hdr8, data[len])
0x4008050a  info_a                devctl (12 bytes from info+0x36)
0x4008050b  info_b                devctl (12 bytes from info+0x4e)
0xc008050c  password_struct_clear (reads/rewrites 0x2801, writes secure)
0x4008050d  bootrom_metrics       devctlv(hdr8, buf[0xb4 stock | 0x4e8 2012])
0x4008050e  security_mode         devctl (u32: metrics[+0x84] != 0)
0x4008050f  record_dump           (resmgr 0xe080)
0x40080513  os_metrics            devctlv(hdr8, buf[0x1cc])   [2012 lib only]
other       -> 0x59
```

## Appendix B — the resmgr's full ID → permission/name table (207 entries)

Parsed from `nvram_resource_manager` file offsets `0x12d54` + `n*0x38`
(`{ u16 id; u16 perm; u8 pad; char name[51]; }`).
`perm` bit0 = protected (writes refused while the global write-protect flag is set);
`perm` bit2 = read-denied (unused in this build).

| id | perm | file off | name |
|---|---|---|---|
| `0x1000` | `0x0000` | `0x12D54` | `NV_CAL_NUM` |
| `0x10FF` | `0x0000` | `0x12D8C` | `NV_CAL_LAST_NUM` |
| `0x1100` | `0x0000` | `0x12DC4` | `NV_GPRS_NUM` |
| `0x1101` | `0x0000` | `0x12DFC` | `NV_GPRS_PlmnNameOverrideFirstEntry_NUM` |
| `0x1164` | `0x0000` | `0x12E34` | `NV_GPRS_PlmnNameOverrideLastEntry_NUM` |
| `0x1FFF` | `0x0000` | `0x12E6C` | `NV_GPRS_LAST_NUM` |
| `0x2000` | `0x0000` | `0x12EA4` | `NV_OSSTORE_NUM` |
| `0x2001` | `0x0000` | `0x12EDC` | `NV_OSSTORE_Version_NUM` |
| `0x2002` | `0x0001` | `0x12F14` | `NV_OSSTORE_PIN_NUM` ** |
| `0x2003` | `0x0000` | `0x12F4C` | `NV_OSSTORE_LcdCal_NUM` |
| `0x2004` | `0x0000` | `0x12F84` | `NV_OSSTORE_JvmStart_NUM` ** |
| `0x2005` | `0x0000` | `0x12FBC` | `NV_OSSTORE_CalSectorChecksum_NUM` |
| `0x2006` | `0x0000` | `0x12FF4` | `NV_OSSTORE_CalTestStatus_NUM` |
| `0x2007` | `0x0000` | `0x1302C` | `NV_OSSTORE_LocalInfo_NUM` |
| `0x2008` | `0x0001` | `0x13064` | `NV_OSSTORE_MepData_NUM` |
| `0x2009` | `0x0000` | `0x1309C` | `NV_OSSTORE_DevRandomNumber_NUM` |
| `0x200A` | `0x0000` | `0x130D4` | `NV_OSSTORE_DevProfileTable_NUM` |
| `0x200B` | `0x0001` | `0x1310C` | `NV_OSSTORE_IdenRadioBsn_NUM` |
| `0x200C` | `0x0001` | `0x13144` | `NV_OSSTORE_IdenIMEI_NUM` |
| `0x200D` | `0x0000` | `0x1317C` | `NV_OSSTORE_OnTime_NUM` |
| `0x200E` | `0x0000` | `0x131B4` | `NV_OSSTORE_GprsRadioFeatures_NUM` |
| `0x200F` | `0x0000` | `0x131EC` | `NV_OSSTORE_WLANMain_NUM` |
| `0x2010` | `0x0001` | `0x13224` | `NV_OSSTORE_Bluetooth_NUM` |
| `0x2011` | `0x0000` | `0x1325C` | `NV_OSSTORE_TestMode_NUM` |
| `0x2012` | `0x0000` | `0x13294` | `NV_OSSTORE_BaryonSuperTunnel_NUM` |
| `0x2013` | `0x0000` | `0x132CC` | `NV_OSSTORE_PatriotLoadState_NUM` |
| `0x2014` | `0x0000` | `0x13304` | `NV_OSSTORE_RtcBackup_NUM` |
| `0x2015` | `0x0000` | `0x1333C` | `NV_OSSTORE_UtcBackup_NUM` |
| `0x2016` | `0x0001` | `0x13374` | `NV_OSSTORE_SerialCalMode_NUM` |
| `0x2017` | `0x0001` | `0x133AC` | `NV_OSSTORE_Bluetooth_CAL_NUM` |
| `0x2018` | `0x0000` | `0x133E4` | `NV_OSSTORE_LcdAutoDetect_NUM` |
| `0x2019` | `0x0000` | `0x1341C` | `NV_OSSTORE_BitFlags_NUM` ** |
| `0x201A` | `0x0000` | `0x13454` | `NV_OSSTORE_RngState_NUM` |
| `0x201B` | `0x0000` | `0x1348C` | `NV_OSSTORE_unused2_NUM` |
| `0x201C` | `0x0000` | `0x134C4` | `NV_OSSTORE_unused3_NUM` |
| `0x201D` | `0x0000` | `0x134FC` | `NV_OSSTORE_DSVL_DUID_INFO_NUM` |
| `0x201E` | `0x0000` | `0x13534` | `NV_OSSTORE_WLANRCI_NUM` |
| `0x201F` | `0x0000` | `0x1356C` | `NV_OSSTORE_WLANNVRADIO_NUM` |
| `0x2020` | `0x0001` | `0x135A4` | `NV_OSSTORE_PM_VAMP_NUM` |
| `0x2021` | `0x0001` | `0x135DC` | `NV_OSSTORE_PM_1_2VINTREF_NUM` |
| `0x2022` | `0x0000` | `0x13614` | `NV_OSSTORE_LcdBacklightCaledVal_NUM` |
| `0x2023` | `0x0000` | `0x1364C` | `NV_OSSTORE_KeypadBacklightCaledVal_NUM` |
| `0x2024` | `0x0000` | `0x13684` | `NV_OSSTORE_LcdBacklightTimeoutCaledVal_NUM` |
| `0x2025` | `0x0000` | `0x136BC` | `NV_OSSTORE_KeypadBacklightTimeoutCaledVal_NUM` |
| `0x2026` | `0x0001` | `0x136F4` | `NV_OSSTORE_CalibrationOSinfo_NUM` |
| `0x2027` | `0x0000` | `0x1372C` | `NV_OSSTORE_HeadInSand_NUM` |
| `0x2028` | `0x0000` | `0x13764` | `NV_OSSTORE_BackupMCT_NUM` |
| `0x2029` | `0x0000` | `0x1379C` | `NV_OSSTORE_SWPartsList_NUM` |
| `0x202A` | `0x0000` | `0x137D4` | `NV_OSSTORE_IdenFlags_NUM` |
| `0x202B` | `0x0000` | `0x1380C` | `NV_OSSTORE_AVP_NUM` |
| `0x202C` | `0x0000` | `0x13844` | `NV_OSSTORE_MFG_ERROR_NUM` |
| `0x202D` | `0x0000` | `0x1387C` | `NV_OSSTORE_InteractiveTestSel_NUM` |
| `0x202E` | `0x0000` | `0x138B4` | `NV_OSSTORE_bkltModeD_NUM` |
| `0x202F` | `0x0000` | `0x138EC` | `NV_OSSTORE_LcdWriteReadTestLog_NUM` |
| `0x2030` | `0x0000` | `0x13924` | `NV_OSSTORE_CommonSTPCommandStatus_NUM` |
| `0x2031` | `0x0000` | `0x1395C` | `NV_OSSTORE_CryptoBatteryInfoCache_NUM` |
| `0x2032` | `0x0000` | `0x13994` | `NV_OSSTORE_unused5_NUM` |
| `0x2033` | `0x0000` | `0x139CC` | `NV_OSSTORE_CFP_LAL_NUM` |
| `0x2034` | `0x0000` | `0x13A04` | `NV_OSSTORE_USB_NUM` |
| `0x2035` | `0x0000` | `0x13A3C` | `NV_OSSTORE_CHARGER_TEMP_CAL_NUM` |
| `0x2036` | `0x0000` | `0x13A74` | `NV_OSSTORE_LOG_VERBOSITY_NUM` |
| `0x2037` | `0x0000` | `0x13AAC` | `NV_OSSTORE_SYS_PROFILE_ENABLE_NUM` |
| `0x2038` | `0x0000` | `0x13AE4` | `NV_OSSTORE_InstallerOSSize_NUM` |
| `0x2039` | `0x0000` | `0x13B1C` | `NV_OSSTORE_DefaultGancInformation` |
| `0x203A` | `0x0000` | `0x13B54` | `NV_OSSTORE_TCXO_TEMP_CAL_NUM` |
| `0x203B` | `0x0000` | `0x13B8C` | `NV_OSSTORE_BATTERY_TEMP_CAL_NUM` |
| `0x203C` | `0x0000` | `0x13BC4` | `NV_OSSTORE_InteractiveTestResults_NUM` |
| `0x203D` | `0x0000` | `0x13BFC` | `NV_OSSTORE_appstore_NUM` |
| `0x203E` | `0x0000` | `0x13C34` | `NV_OSSTORE_branding_NUM` |
| `0x203F` | `0x0000` | `0x13C6C` | `NV_OSSTORE_CamAutoDetect_NUM` |
| `0x2040` | `0x0000` | `0x13CA4` | `NV_OSSTORE_NukeStatus_NUM` ** |
| `0x2041` | `0x0000` | `0x13CDC` | `NV_OSSTORE_GpsAlmanac_NUM` |
| `0x2042` | `0x0000` | `0x13D14` | `NV_OSSTORE_GpsAdc_NUM` |
| `0x2043` | `0x0000` | `0x13D4C` | `NV_OSSTORE_AudioTrim_NUM` |
| `0x2044` | `0x0000` | `0x13D84` | `NV_OSSTORE_USB_COPY_NUM` |
| `0x2045` | `0x0000` | `0x13DBC` | `NV_OSSTORE_LegacyBacklightSettings_NUM` |
| `0x2046` | `0x0000` | `0x13DF4` | `NV_OSSTORE_AudioLogging_NUM` |
| `0x2047` | `0x0000` | `0x13E2C` | `NV_ORPHEUS_3G_OVERRIDE` |
| `0x2048` | `0x0000` | `0x13E64` | `NV_ORPHEUS_3G_NETPAR_NUM` |
| `0x2049` | `0x0000` | `0x13E9C` | `NV_OSSTORE_UtcTimeOffset_NUM` |
| `0x204A` | `0x0000` | `0x13ED4` | `NV_OSSTORE_brandingMulti_NUM` |
| `0x204B` | `0x0000` | `0x13F0C` | `NV_OSSTORE_brandingMulti_NUM_2` |
| `0x204C` | `0x0000` | `0x13F44` | `NV_OSSTORE_brandingMulti_NUM_3` |
| `0x204D` | `0x0000` | `0x13F7C` | `NV_OSSTORE_brandingMulti_SPARE_1` |
| `0x204E` | `0x0000` | `0x13FB4` | `NV_OSSTORE_brandingMulti_SPARE_2` |
| `0x204F` | `0x0000` | `0x13FEC` | `NV_OSSTORE_brandingMulti_SPARE_3` |
| `0x2050` | `0x0000` | `0x14024` | `NV_OSSTORE_NVR_CONDITIONAL_WRITE_NUM` |
| `0x2051` | `0x0000` | `0x1405C` | `NV_OSSTORE_CamIspVersionInfo_NUM` |
| `0x2052` | `0x0000` | `0x14094` | `NV_OSSTORE_OpenVGStatistics_NUM` |
| `0x2053` | `0x0000` | `0x140CC` | `NV_OSSTORE_StartupTime_NUM` |
| `0x2054` | `0x0000` | `0x14104` | `NV_OSSTORE_WLANApps_NUM` |
| `0x2055` | `0x0000` | `0x1413C` | `NV_OSSTORE_Iden_Data_Off` |
| `0x2056` | `0x0000` | `0x14174` | `NV_OSSTORE_CfpVersion_NUM` |
| `0x2057` | `0x0000` | `0x141AC` | `NV_OSSTORE_BacklightSettings_NUM` |
| `0x2058` | `0x0000` | `0x141E4` | `NV_OSSTORE_CfpBugDispLog_NUM` |
| `0x2059` | `0x0000` | `0x1421C` | `NV_OSSTORE_PROFILE_DATA` |
| `0x205A` | `0x0000` | `0x14254` | `NV_OSSTORE_HW_ID_OVERRIDE_NUM` |
| `0x205B` | `0x0000` | `0x1428C` | `NV_OSSTORE_VCLICK_TRIM_NUM` |
| `0x205C` | `0x0000` | `0x142C4` | `NV_OSSTORE_AccelOrientationOverride_NUM` |
| `0x205D` | `0x0000` | `0x142FC` | `NV_OSSTORE_RimCryptKeyStorage_NUM` |
| `0x205E` | `0x0000` | `0x14334` | `NV_OSSTORE_OMADM_Info_NUM` |
| `0x205F` | `0x0000` | `0x1436C` | `NV_OSSTORE_AccelOrientationAlgorithmOverride_NUM` |
| `0x2060` | `0x0000` | `0x143A4` | `NV_OSSTORE_SDBugdispLogMaxLogCount` |
| `0x2061` | `0x0000` | `0x143DC` | `NV_OSSTORE_MFG_PERSISTENT_DATA` |
| `0x2062` | `0x0000` | `0x14414` | `NV_OSSTORE_AccelDSPGestureSettings_NUM` |
| `0x2063` | `0x0000` | `0x1444C` | `NV_OSSTORE_BqHugeDefault_NUM` |
| `0x2064` | `0x0000` | `0x14484` | `NV_OSSTORE_Battery_ID_NUM` |
| `0x2065` | `0x0000` | `0x144BC` | `NV_OSSTORE_ProxSensorCal_NUM` |
| `0x2066` | `0x0000` | `0x144F4` | `NV_OSSTORE_HSJackState_NUM` |
| `0x2067` | `0x0000` | `0x1452C` | `NV_OSSTORE_WONDERWALL_TRIM_NUM` |
| `0x2068` | `0x0000` | `0x14564` | `NV_OSSTORE_RADIO_STORED_CELL_INFO` |
| `0x2069` | `0x0000` | `0x1459C` | `NV_OSSTORE_SDBugdispLogMaxFilesize` |
| `0x206A` | `0x0000` | `0x145D4` | `NV_OSSTORE_RPLMN_RAT` |
| `0x206B` | `0x0000` | `0x1460C` | `NV_OSSTORE_SIM_DATA` |
| `0x206C` | `0x0000` | `0x14644` | `NV_OSSTORE_OTASL_MCT_BACKUP` |
| `0x206D` | `0x0000` | `0x1467C` | `NV_OSSTORE_VOLT_RESIST_LOOKUP_TABLE` |
| `0x206E` | `0x0000` | `0x146B4` | `NV_OSSTORE_BATTERY_KILL_SWITCH_COUNT` |
| `0x206F` | `0x0000` | `0x146EC` | `NV_OSSTORE_GPS_INTERNAL` |
| `0x2070` | `0x0000` | `0x14724` | `NV_OSSTORE_GPS_VENDOR_1` |
| `0x2071` | `0x0000` | `0x1475C` | `NV_OSSTORE_GPS_VENDOR_2` |
| `0x2072` | `0x0000` | `0x14794` | `NV_OSSTORE_BATTERY_MONITORING_FLAG` |
| `0x2073` | `0x0000` | `0x147CC` | `NV_OSSTORE_RADIO_3GPP_CONFIGURATIONS` |
| `0x2074` | `0x0000` | `0x14804` | `NV_OSSTORE_OS_REGION` ** |
| `0x2075` | `0x0000` | `0x1483C` | `NV_OSSTORE_BOOT_MODE_BLOCK` ** |
| `0x2076` | `0x0000` | `0x14874` | `NV_OSSTORE_CELL_BROADCAST_SETTING` |
| `0x2077` | `0x0000` | `0x148AC` | `NV_OSSTORE_NETUSB_CONFIG` |
| `0x2078` | `0x0000` | `0x148E4` | `NV_OSSTORE_HDCP_KEYS` |
| `0x2079` | `0x0000` | `0x1491C` | `NV_OSSTORE_MAC_ADDR` |
| `0x207A` | `0x0000` | `0x14954` | `NV_OSSTORE_MFG_SERIAL` |
| `0x207B` | `0x0000` | `0x1498C` | `NV_OSSTORE_CalTestStatus2_NUM` |
| `0x207C` | `0x0000` | `0x149C4` | `NV_OSSTORE_EXTRA_REGION` |
| `0x27FF` | `0x0000` | `0x149FC` | `NV_OSSTORE_LAST_NUM` |
| `0x2800` | `0x0000` | `0x14A34` | `NV_OSSTORE_PERSISTENT_FIRST_NUM` |
| `0x2801` | `0x0000` | `0x14A6C` | `NV_OSSTORE_PasswordStruct_NUM` ** |
| `0x2802` | `0x0000` | `0x14AA4` | `NV_OSSTORE_PasswordStructReserve_NUM` ** |
| `0x2803` | `0x0000` | `0x14ADC` | `NV_OSSTORE_WLAN_EEPROM` |
| `0x2804` | `0x0001` | `0x14B14` | `NV_OSSTORE_JANUS_AFE_NUM` |
| `0x2805` | `0x0000` | `0x14B4C` | `NV_OSSTORE_JANUS_L1CONTROL_NUM` |
| `0x2806` | `0x0000` | `0x14B84` | `NV_OSSTORE_JANUS_MSR_NUM` |
| `0x2807` | `0x0001` | `0x14BBC` | `NV_OSSTORE_JANUS_RF_CALIB_NUM` |
| `0x2808` | `0x0000` | `0x14BF4` | `NV_OSSTORE_JANUS_RF_TYPE_NUM` |
| `0x2809` | `0x0001` | `0x14C2C` | `NV_OSSTORE_CryptoBatteryChallenge_NUM` |
| `0x280A` | `0x0000` | `0x14C64` | `NV_OSSTORE_JANUS_AFC_DAC_NUM` |
| `0x280B` | `0x0000` | `0x14C9C` | `NV_OSSTORE_JANUS_PRE_PRODUCT_NUM` |
| `0x280C` | `0x0001` | `0x14CD4` | `NV_OSSTORE_JANUS_RF_ANALYSIS_NUM` |
| `0x280D` | `0x0001` | `0x14D0C` | `NV_OSSTORE_PcbBarcode_NUM` |
| `0x280E` | `0x0001` | `0x14D44` | `NV_OSSTORE_WLAN_TI_CAL_NUM` |
| `0x280F` | `0x0003` | `0x14D7C` | `NV_OSSTORE_bsn_NUM` ** |
| `0x2810` | `0x0001` | `0x14DB4` | `NV_OSSTORE_AntiCloneData_NUM` |
| `0x2811` | `0x0000` | `0x14DEC` | `NV_OSSTORE_JANUS_RX_DP_CAL_NUM` |
| `0x2812` | `0x0001` | `0x14E24` | `NV_OSSTORE_Nvram_Signatures` ** |
| `0x2813` | `0x0001` | `0x14E5C` | `NV_OSSTORE_JANUS_RF_RIM_NUM` |
| `0x2814` | `0x0000` | `0x14E94` | `NV_OSSTORE_PersistentBitFlags_NUM` ** |
| `0x2815` | `0x0000` | `0x14ECC` | `NV_OSSTORE_PatriotDigest_NUM` |
| `0x2816` | `0x0000` | `0x14F04` | `NV_OSSTORE_NANDEraseCountStats_NUM` |
| `0x2817` | `0x0000` | `0x14F3C` | `NV_OSSTORE_FgUpdateStatus_NUM` |
| `0x2818` | `0x0000` | `0x14F74` | `NV_OSSTORE_RefurbishedTime_NUM` |
| `0x2819` | `0x0001` | `0x14FAC` | `NV_OSSTORE_NotSupportedOS_NUM` |
| `0x281A` | `0x0000` | `0x14FE4` | `NV_OSSTORE_TavorPVUsbCal_NUM` |
| `0x281B` | `0x0000` | `0x1501C` | `NV_OSSTORE_SecureRecordsHash_NUM` ** |
| `0x281C` | `0x0000` | `0x15054` | `NV_OSSTORE_AccelZeroGOffset_NUM` |
| `0x281D` | `0x0000` | `0x1508C` | `NV_OSSTORE_RimCryptPersistentKeyStorage_NUM` |
| `0x281E` | `0x0000` | `0x150C4` | `NV_OSSTORE_L3_Configuration_NUM` |
| `0x281F` | `0x0000` | `0x150FC` | `NV_OSSTORE_RAMDumpConfiguration_NUM` |
| `0x2820` | `0x0000` | `0x15134` | `NV_OSSTORE_BatterySerial_NUM` |
| `0x2821` | `0x0000` | `0x1516C` | `NV_OSSTORE_ResetInfo_NUM` |
| `0x2822` | `0x0000` | `0x151A4` | `NV_OSSTORE_QC_NV_Checksum_Storage_NUM` |
| `0x2823` | `0x0000` | `0x151DC` | `NV_OSSTORE_MMCBootInfo_NUM` |
| `0x2824` | `0x0000` | `0x15214` | `NV_OSSTORE_UMPPagePoolSize_NUM` |
| `0x2825` | `0x0000` | `0x1524C` | `NV_OSSTORE_MMCStats_NUM` |
| `0x2826` | `0x0001` | `0x15284` | `NV_OSSTORE_SUE2_MICROCODE` |
| `0x2827` | `0x0001` | `0x152BC` | `NV_OSSTORE_SUE2_PREBOOT` |
| `0x2828` | `0x0001` | `0x152F4` | `NV_OSSTORE_SUE2_CALIB` |
| `0x2829` | `0x0001` | `0x1532C` | `NV_OSSTORE_SUE2_SETUP` |
| `0x282A` | `0x0001` | `0x15364` | `NV_OSSTORE_SUE2_PARAMS` |
| `0x282B` | `0x0000` | `0x1539C` | `NV_OSSTORE_MAG_MFG_CAL` |
| `0x282C` | `0x0000` | `0x153D4` | `NV_OSSTORE_OndevQXDMConfiguration_NUM` |
| `0x282D` | `0x0000` | `0x1540C` | `NV_OSSTORE_OndevQXDMMaskIdx_NUM` |
| `0x282E` | `0x0000` | `0x15444` | `NV_OSSTORE_JANUS_AFC_DAC_NEW_NUM` |
| `0x282F` | `0x0000` | `0x1547C` | `NV_OSSTORE_AccelTransformInfo_NUM` |
| `0x2830` | `0x0001` | `0x154B4` | `NV_OSSTORE_UMTS_AFC_PARAMS` |
| `0x2831` | `0x0000` | `0x154EC` | `NV_OSSTORE_MAG_DSP_CAL` |
| `0x2832` | `0x0001` | `0x15524` | `NV_OSSTORE_UMTS_APC_PARAMS` |
| `0x2833` | `0x0001` | `0x1555C` | `NV_OSSTORE_UMTS_LLDR_INIT_PARAMS` |
| `0x2834` | `0x0001` | `0x15594` | `NV_OSSTORE_CryptoBatteryCrPairs_NUM` |
| `0x2835` | `0x0001` | `0x155CC` | `NV_OSSTORE_SUE2_CAL_TABLE0` |
| `0x2836` | `0x0001` | `0x15604` | `NV_OSSTORE_SUE2_CAL_TABLE1` |
| `0x2837` | `0x0001` | `0x1563C` | `NV_OSSTORE_SUE2_CAL_TABLE2` |
| `0x2838` | `0x0001` | `0x15674` | `NV_OSSTORE_SUE2_CAL_TABLE3` |
| `0x2839` | `0x0001` | `0x156AC` | `NV_OSSTORE_SUE2_CAL_TABLE4` |
| `0x283A` | `0x0000` | `0x156E4` | `NV_OSSTORE_CryptoBatteryFailedPairIndex_NUM` |
| `0x283B` | `0x0001` | `0x1571C` | `NV_OSSTORE_UMTS_BAND_FILTER` |
| `0x283C` | `0x0000` | `0x15754` | `NV_OSSTORE_MEID` |
| `0x283D` | `0x0000` | `0x1578C` | `NV_OSSTORE_ESN` |
| `0x283E` | `0x0001` | `0x157C4` | `NV_OSSTORE_RIM_DIGRF3_BAND0` |
| `0x283F` | `0x0001` | `0x157FC` | `NV_OSSTORE_RIM_DIGRF3_BAND1` |
| `0x2840` | `0x0001` | `0x15834` | `NV_OSSTORE_RIM_DIGRF3_BAND2` |
| `0x2841` | `0x0001` | `0x1586C` | `NV_OSSTORE_RIM_DIGRF3_BAND3` |
| `0x2842` | `0x0001` | `0x158A4` | `NV_OSSTORE_RIM_DIGRF3_BAND4` |
| `0x2843` | `0x0000` | `0x158DC` | `NV_OSSTORE_WIMAX_ROOTCA2_CERT` |
| `0x2844` | `0x0000` | `0x15914` | `NV_OSSTORE_WIMAX_SUBCA_CERT` |
| `0x2845` | `0x0000` | `0x1594C` | `NV_OSSTORE_WIMAX_DEV_CERT` |
| `0x2846` | `0x0000` | `0x15984` | `NV_OSSTORE_WIMAX_DEV_PKEY` |
| `0x2849` | `0x0000` | `0x159BC` | `NV_OSSTORE_STE_CALIBDATA` |
| `0x284A` | `0x0000` | `0x159F4` | `NV_OSSTORE_STE_COPS` |
| `0x284B` | `0x0000` | `0x15A2C` | `NV_OSSTORE_STE_GDFS` |
| `0x2FFF` | `0x0000` | `0x15A64` | `NV_OSSTORE_PERSISTENT_LAST_NUM` |
