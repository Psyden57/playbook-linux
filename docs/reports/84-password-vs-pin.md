# 84 — Password vs PIN: `0x2801` is the verifier for the user's `0000`, `0x2002` is the hardware PIN

Status: read-only. No device access, no NVRAM/flash writes, nothing modified outside this file.
Date: 2026-10-25. Complements `75`, `76`, `77`, `78`, `79`; corrects nothing in them but
settles by arithmetic what they could only infer.

> **Terminology (user clarification, 2026-10-25).** The user calls the 4-digit unlock code a
> "PIN". In this report **device password** means that unlock code (→ `0x2801`);
> **device PIN / hardware PIN** means the RIM identity value `0x50175127` (→ `0x2002`).
> They are different records with different code paths.

---

## 0. Verdict (short)

1. **`0x2801` (`NV_OSSTORE_PasswordStruct`) *is* the device-password record, and it is the
   verifier for the password literally `0000`.** The live 64-byte blob at `+0x30` is
   reproduced **exactly** by `SHA-512` iterated 5060 times over `LE32(counter) ‖ salt ‖ state`
   with `state₀ = "0000"`. This is no longer an inference about field layout — it is an
   exact 512-bit match (§4).
2. **`0x2002` (`NV_OSSTORE_PIN_NUM`) is not the password and cannot be `0000` under any
   encoding.** It is the 4-byte BlackBerry *device PIN* (an identity value), and the live
   value `0x50175127` is byte-for-byte the bootrom's `0xE7` PIN (§6).
3. "The password and the PIN are the same thing" is a **UX conflation**: the 4-digit code the
   user types at the unlock screen is the *device password* (→ `0x2801`); the *device PIN* is an
   8-hex-digit hardware identifier (→ `0x2002`). Different records, different code paths,
   different values, no shared field.
4. The `deckard.nvram` Python really is only a thin wrapper: it opens
   `/dev/nvram` and calls `nto.pread/pwrite` with the **record ID in the offset argument**. It
   touches exactly **four** record IDs — `0x2074`, `0x207C`, `0x2819`, `0x2852` — and **none of
   them is `0x2801` or `0x2002`** (§2).
5. **No Python in the decompiled tree handles a device password or PIN at all.** The only
   `password` strings in the whole tree are cellular APN credentials for the update PDP context
   (§3).
6. The bootrom challenge (`0x0A`/`0x0F`) is **built out of `0x2801`**: the live `0x0A` response
   carries that record's salt and iteration count, and the live `0x0F` 64-byte response equals
   `HashPassV2("0000", challenge, salt, 5060)`, which the device can also compute **from the
   record alone** — no plaintext needed (§5). So yes: `0x2801` holds the boot password verifier.
7. **No code that *sets* a password or PIN exists anywhere in the available corpus.** The
   device-password service is `PasswordManager` (`sanity.cfg:110`), whose binary is on the RCFS,
   which is not dumped here (§7).

---

## 1. Corpus and method

Available (all read-only):

| path | content |
|---|---|
| `analysis/deckard-2.1.0.1917/**` | 25 uncompyle6-decompiled modules of `/usr/lib/python2.7/site-packages/deckard` (TabletOS 2.1.0.1917) |
| `analysis/deckard-2.0.0.4869/**` | 22 modules, older OS |
| `playbook-live-2026-10-21/device-binaries/` | `nvram_resource_manager`, `libnvram.so.1` (stock), `uploaded-libnvram.so.1` |
| `playbook-live-2026-10-21/nv-0x2801.txt` | live 112-byte record |
| `playbook-live-2026-10-20/bb10mt-urb-trace.log` | byte-level URB trace incl. the live `0x0A`/`0x0F` exchange |
| `playbook-dev/dumped4869ifs/proc/boot/` | 59 files — **the boot image only** |
| `/home/psyden/rea-work/bb10mt-main/ucrypto.pas` | `HashPassV2` reference implementation |

**Not available**: the RCFS / MFM content — no `/usr/bin`, `/usr/sbin`, `/etc` (beyond two PMIC
configs), no `/pps` tree, and not the full `site-packages` (deckard's `util/`, `authman.py` and
the compiled `nvram/_nvram` extension are absent). `hd4.bin`/`hd5.bin` in the dump root are a
128 KB QNX6 superblock fragment and a WLAN calibration blob, not filesystem content. This bounds
task 4 and is stated again in §7.

Tools: `grep`/`strings`, Capstone 5.0.7 (ARM-state linear sweep with `skipdata`, plus a
`MOVW/MOVT`+literal-pool immediate scanner), `hashlib`.

---

## 2. Task 1 — every NVRAM record the decompiled Python touches

### 2.1 Mechanism: pread/pwrite with the record ID as the offset

The `deckard.nvram` package is a stub — it re-exports only `__version__` and
`is_board_insecure` from a native extension that is not in the corpus:

```python
# deckard-2.1.0.1917/nvram/__init__.py:9-14
__all__ = ['__version__', 'is_board_insecure']
try:    from deckard.nvram._nvram import *
except ImportError:  from deckard.nvram.dummy import *
```

Record access does **not** go through that extension and does **not** go through `libnvram` /
`devctl`. It is a two-line file-offset layer:

```python
# platform/base.py:466-467
def nvram(self):    return os.path.join(self.devpath(), 'nvram')      # -> /dev/nvram
# platform/base.py:123-127 (abstract), platform/live.py:48-52 (live impl)
def pread(self, fd, size, offset):  return nto.pread(fd, size, offset)
def pwrite(self, fd, data, offset): return nto.pwrite(fd, data, offset)
```

and every caller passes an **`NV_OSSTORE_*` record ID as `offset`** and the record length as
`size`, e.g. `pfm.pread(fd, cls.STRUCT.size, cls.NV_OSSTORE_OS_REGION)`. So `/dev/nvram` is used
as a record-addressed device where "offset" = 16-bit record ID, not as a byte stream. (`77` §4
documents the parallel devctl ABI; deckard never uses it. Residual in §9.1.)

Writes are wrapped in `superuser()` / `superuserfd()` (setuid), i.e. deckard is meant to run as
a normal admin tool and escalate only for its write paths.

### 2.2 Complete list (both OS versions)

| record | name | deckard constant | 2.0.0 | 2.1.0 | use in deckard |
|---|---|---|---|---|---|
| **`0x2074`** | `NV_OSSTORE_OS_REGION` | `8308` | ✔ | ✔ | **read + write** the boot-region struct: `requested_region`, per-region boot counts, `max_boot_count`, `epoch`, and in v3 the two 24-byte `version_id_*` (OS version strings). This is the A/B slot switch and its boot budget. `osregion.py:206` (const), `:277` (read), `:292` (write); versioned structs `OsRegionV1 '=IBBBBQHH'` (0x14), `V2 '=IBBHHHIQ'` (0x18), `V3 '=IBBHHHIQ24s24s'` (0x48 — matches the live 72-byte record in `76` §4). |
| **`0x207C`** | `NV_OSSTORE_EXTRA_REGION` | `8316` | — | ✔ | **read + write** the radio-region table: header `'=4BII'` with magic `M R M C`, version 1, up to 15 entries `'=8sHH'` = `{name[8], requested_region, actual_region}`; deckard looks for the entry named `MODEM` and flips its requested region. `osregion.py:101` (const), `:148` (write), `:154`/`:161` (read). |
| **`0x2819`** | `NV_OSSTORE_NotSupportedOS_NUM` | `10265` | ✔ | ✔ | **read + write** the OS-version blocklist: 10 × 12-byte `{rangeStart, rangeEnd, osTypeBitMask}` (`OSRangeAndType '=III'`, 120 bytes). This is the downgrade lock. `blacklist.py:46`, `:126`, `:150`. |
| **`0x2852`** | `NV_OSSTORE_NotSupportedRADIO_NUM` | `10322` | — | ✔ | same, for radio images; only touched when `pfm.supports_zone(pfm.ZONE_RADIO_RFS)`. `blacklist.py:47`, `:117`, `:142`. **Not present in the TabletOS resmgr's 288-entry table** (`77` §2 ends at `0x284B`) — a BB10/radio-era extension, consistent with deckard 2.1 being written for later hardware. |

That is the whole list: **four IDs across ~3 700 lines of decompiled code**, all of them the
installer's own bookkeeping (slot selection + downgrade blocklists). There is no generic NVRAM
browser, no record dumper, no `0x2801`, no `0x2802`, no `0x2002`.

---

## 3. Task 2 — does any Python handle the device password or PIN?

**No.** Exhaustive, case-insensitive search of both decompiled trees for
`passw|passwd|pass_word|\bpin\b|credential|auth|secret|unlock` yields only:

1. **Cellular APN credentials** — `installer/downloader.py:190-298`. `get_apn_setting` /
   `set_apn_setting` over the CNICE PPS API for the logical connection `software_update`, with
   `{'APN':…, 'username':…, 'password':…, 'zero_rated':…}`. This is the PDP context used to
   download OS updates from RIM; it is a *network* password.
2. **Capability / debug-token authorisation** — `installer/application.py:21`
   (`from deckard.authman import authman_send`) and `:684-687`
   (`getcap <pkg> <author-id> permanent`), plus `platform/winchester.py:140-160`
   (`check_author` over PPS). This is authman *capabilities*, not credentials.
3. Everything else is `__authors__ = [u'Neil Schellenberger <nschellenberger@qnx.com>']`
   (the `auth` substring), `Package-Author` manifest fields, and `Package-Author-Id`.

`platform/deviceinfo.py` reads the whole `/pps/.../deviceinfo` object (which contains `PIN` /
`PIN64`) but uses only `BootromBuildDate` and `BootromBuildTime` — it never reads `PIN`.

So: the OS installer has **no** notion of the device password or the device PIN.

---

## 4. Task 3a — what `0x2801` actually contains (arithmetic, not inference)

Live record (`nv-0x2801.txt`, 112 bytes; field decode per `76`/`77` §6):

```
+0x00 00 00 02 00   format 0x00020000 (v2)
+0x04 70 00 00 00   length 0x70
+0x08 01 00 00 00   flags
+0x0C c4 13 00 00   iterations = 5060
+0x10 b5 43 a0 b9   ┐ salt (8 bytes, used in this byte order)
+0x14 b5 86 2d 22   ┘
+0x18 00 00 00 00   failed-attempt counter A = 0
+0x1C 0a 00 00 00   attempt budget B = 10
+0x20 ce 64 e8 a1   nonce/sequence  (= 0xA1E864CE)
+0x28 30 00 00 00   digest offset 0x30
+0x2C 40 00 00 00   digest length 0x40
+0x30 91 1d 08 b1 … cd a1         64-byte digest
```

The digest is reproduced **exactly** by this loop, with the password as ASCII `0000`:

```python
h = b"0000"
for c in range(5060):
    h = sha512(struct.pack("<I", c) + salt + h).digest()   # salt = rec[0x10:0x18]
# h == rec[0x30:0x70]   -> True
```

Uniqueness checks that all **failed** (so the match is not one of several plausible KDFs):

* argument order `salt‖counter‖state`, `counter‖state‖salt`, `state‖counter‖salt`, …;
* big-endian or absent counter;
* 5059 / 5061 rounds, or 1-based counting;
* `PBKDF2-HMAC-{SHA1,SHA256,SHA512}("0000", salt, 5060)`;
* password variants `b"0000\0"`, `b"\0\0\0\0"`, `b""`, `b"0"`, `b"1234"`, …
  (and each with the challenge/nonce mixed in, single- and two-pass).

Only `counter(LE32) ‖ salt ‖ state`, 5060 rounds starting at `count = 0`, `state₀ = b"0000"`
matches. **Therefore the live `0x2801` is the verifier of the password `0000`.**

Two consequences worth stating plainly:

* `+0x0C = 5060` is not merely "iteration-count-shaped" (the caveat in `77` §6.1) — it is the
  exact round count that reproduces the stored digest. The scheme is **iterated SHA-512**, not
  PBKDF2 and not SHA-1.
* the plaintext `0000` is **nowhere** on the device. The record is a one-way verifier: no salt
  reuse across records is even needed, because the salt is stored next to it.
  `+0x30 = FF…FF` remains the "no password set" sentinel (that is what the resmgr wipe writes).

---

## 5. Task 5 — the bootrom challenge is computed *from* `0x2801`

### 5.1 The live exchange

From `playbook-live-2026-10-20/bb10mt-urb-trace.log`:

```
SUBMIT 000008000a010003                                     PASSWORD_INFO (0x0A)
REAP   000020000e010103 0a0a1400 ca64e8a1 01000000 b543a0b9b5862d22 c4130000
SUBMIT 00004c000f010004 00004000 21032517b6ed086a…09a67c018a92bbd3
REAP   0000080010010004                                     SEND_PASSWORD accepted (0x10)
```

Decoding the `0x0E` payload (24 bytes):

| bytes | value | meaning |
|---|---|---|
| `0:4` | `0a 0a 14 00` | **B = 10**, remaining budget = **10**, then u16 `0x0014` (unidentified) |
| `4:8` | `ca 64 e8 a1` = `0xA1E864CA` | **challenge / nonce** |
| `8:12` | `01 00 00 00` | flag |
| `12:20` | `b5 43 a0 b9 b5 86 2d 22` | **salt — identical to `0x2801 +0x10..+0x17`** |
| `20:24` | `c4 13 00 00` = **5060** | iterations — identical to `0x2801 +0x0C` |

That is `75` §4.1's description of `FUN_8001212E` (B, remaining budget, sequence) confirmed
field-by-field against the record: **`PASSWORD_INFO` is generated from `0x2801`**, not from a
separate credential store, and not from `0x2002`.

### 5.2 The `0x0F` response verified

`HashPassV2` (`bb10mt-main/ucrypto.pas:94-160`) is two concatenated passes:

```
pass(x) = state := x; repeat 5060×: state := SHA512(LE32(c) ‖ salt ‖ state)
HashPassV2(pw) = pass( challenge ‖ pass(pw) )
```

With `challenge = 0xA1E864CA`, `salt = rec[0x10:0x18]`, `iterations = 5060`, `pw = "0000"`:

```
HashPassV2("0000")                    = 21032517b6ed086a…8a92bbd3   == wire payload   ✔
pass(challenge ‖ rec[0x30:0x70])      = 21032517b6ed086a…8a92bbd3   == wire payload   ✔
```

So the wire answer is reproducible **from the record alone**. Independent in-session check
(2026-10-25): both equalities hold exactly, and `pass(ca64e8a1 ‖ record_digest)` reproduces
the 64-byte wire payload. This is the design:

* `0x2801 +0x30` stores the **inner** (first-pass) verifier `pass1(pw)`;
* the boot stage / bootrom, holding only the record, computes `pass2(challenge ‖ pass1(pw))` and
  compares it with the host's 64-byte `0x0F` payload — the plaintext password never has to exist
  on the device, nor to be recovered by it;
* a "change password" operation therefore only needs to rewrite `+0x10/+0x14` (new salt),
  `+0x0C` (iterations) and `+0x30` (new `pass1`), and bump `+0x20`. (`75` §4.1 shows the
  match path `FUN_80014F5C` doing exactly the `+0x20` bump and `A=0`; the mismatch path
  `FUN_80014F3C` doing `A++`.) That the *setter* uses this recipe is an inference — the setter
  binary is not in the corpus (§7).
* The gate `clamp(+0x1C,3,10) − +0x18` (`75` §3, `79` §1.2) is the same attempt budget the
  `0x0E` response advertises as `0a 0a`. Live `A=0`, `B=10` → 10 attempts remaining, which is
  why the `0x0F` in the trace was accepted.

### 5.3 Is `0x2002` ever the unlock credential? No.

The resmgr reads `0x2002` in exactly one place and only to publish it:

```
0x3788  movw r0, #0x2002 ; mov r1,r8 ; mov r2,#8 ; bl 0x8ff0      ; read up to 8 bytes
        cmp r0,#4 / cmp r0,#8 -> "found a 32 bit pin" / "found a 64 bit pin"
                               -> else "faking one for PPS object"
```

No comparison against user input, no hash, no counter, no digest, no salt. `0x2002` is data,
never a check.

---

## 6. Task 3b — what `0x2002` is (and why it cannot be `0000`)

* Name: `NV_OSSTORE_PIN_NUM`, ID `0x2002`, permission word **`0x0001`** (`77` §2 row 8).
* Length: 4 or 8 bytes; the resmgr branches on the returned length.
* Published to `/pps/system/nvram/deviceinfo` as `PIN` / `PIN64`
  (`%s::0x%08X` / `%s::0x%016llX`, strings `found a … pin` at `0x11B04`/`0x11B34`/`0x11B64`),
  and re-published after any write via the `0xB7D4` hook (`0x2002 -> 0x369C(0)`).
* Live value `27 51 17 50` = `0x50175127` = **the bootrom `0xE7` PIN** (`69` §5: `0xE7` → `0xD1`,
  4 bytes, `PIN 0x50175127`; bb10mt/Sachesi call that field "PIN"). It is the BlackBerry device
  PIN — the 8-hex-digit identity used for BBM/BBID and printed on the box, not a code you type
  to unlock.

Why it cannot be the 4-digit password:

| encoding of "0000" | bytes | equals `0x50175127`? |
|---|---|---|
| ASCII `"0000"` (4 B) | `30 30 30 30` | no |
| ASCII+NUL (5 B) | `30 30 30 30 00` | no (and 5 is not a legal length) |
| u32 numeric 0 | `00 00 00 00` | no |
| BCD 0 | `00 00 00 00` | no |
| 8-byte little-endian 0 | `00…00` | no |

`0x50175127` is `1343705383` decimal and `"50175127"` as hex text — neither is `0000` in any
encoding. Conversely, if `0x2002` "were the password", setting `0000` would have written
`0x00000000` there.

Also note: `0x2002` is in the *protected* class, but protection here is **conditional** — the
write gate at `0x9BE0` only refuses `flags&1` records while a global is non-zero, and that global
is initialised by reading NVRAM **`0x2050` (`NV_OSSTORE_NVR_CONDITIONAL_WRITE_NUM`)**:

```
0xB344 bl 0x9ebc                     ; init
0xB348 ldr r5,[pc,#0x4c] ; add r5,r4,r5
0xB350 str r3(=0),[r5]               ; default 0
0xB358 movw r0,#0x2050 ; mov r1,r5 ; mov r2,#4 ; bl 0x8ff0   ; gate := record 0x2050
```

On the units audited in `PLAYBOOK-REFERENCE.md` §5.3 `0x2050` is **absent**, so `0x2002` is in
practice writable from root (the 2026-10-21 audit of `76` did not read `0x2050`, so treat this as
"absent on the audited units" rather than re-verified here) — which is *not* an invitation:
`0x50175127` has to stay consistent with the bootrom/identity, and nothing in the OS recomputes
it.

### 6.1 The two records side by side

| | `0x2801` `NV_OSSTORE_PasswordStruct` | `0x2002` `NV_OSSTORE_PIN_NUM` |
|---|---|---|
| size | 0x70 (112 B) structured | 4 or 8 B scalar |
| contents | format, iter=5060, 8-byte salt, failed/max attempts, nonce, 64-byte digest | one integer |
| protection flags | `0x0000` (unprotected) | `0x0001` (conditional) |
| who reads it | `hd0` boot stage (`FUN_80014F84`), bootrom `0x0A`/`0x0F`, resmgr wipe `0x9CE8` | resmgr `0x3788` → PPS `PIN`/`PIN64` |
| what it is | **verifier** for the device password | **identity** value (device PIN) |
| live content | digest of `0000` | `0x50175127` |
| relation to the other | none — no field shared, no code path shared | none |

### 6.2 So how does a user-set "0000" appear in each?

* **`0x2801`: as `SHA-512^5060(salt ‖ "0000")` at `+0x30`, plus the salt, iteration count,
  attempt budget and nonce that the boot side needs to evaluate a challenge.** That is exactly
  what the live record is (§4), which is why the user's "`0000` is my set device password" is
  **confirmed** — and why `0x2801` *is* "the password record" as they suspected.
* **`0x2002`: not at all.** It holds the hardware PIN. The user's statement "the password and pin
  are the same thing" is false at the record level; it is only true at the *UI* level, where the
  4-digit unlock code is called the "device password" while marketing material calls the
  8-hex-digit identifier the "PIN".

---

## 7. Task 4 — searching the rest of the RCFS

### 7.1 What could be searched, and what could not

The workspace contains the **boot image** (`dumped4869ifs/proc/boot`, 59 files) plus the three
`device-binaries`. It does **not** contain the RCFS: there is no `/usr/bin`, `/usr/sbin`,
`/etc` (only two PMIC configs), `/pps`, and no full `/usr/lib/python2.7/site-packages` (only the
25/22 decompiled deckard modules; `deckard/util/*`, `deckard/authman.py` and `nvram/_nvram` are
missing even though `installer/application.py:21` imports `deckard.authman`). `hd4.bin` /
`hd5.bin` are a QNX6 superblock fragment and a Quanta WLAN calibration blob. **So the
`/base`/`/usr` half of the RCFS — where a password setter would live — is simply not in the
corpus.** Everything below is what *is* answerable.

### 7.2 `0x2801` in the boot image: exactly one writer, and it is the wipe

Code-reference scan (ARM `movw` immediates + 32-bit literal pools) over all 59 boot-image files:

```
nvram_resource_manager  arm-movw 0x2801 @0x9d30, 0x9e04      <-- the only code references
                        lit32   0x2801 @0x14a6c (descriptor table row 133)
                        lit32   0x2802 @0x14aa4 (descriptor table row 134)
ksh                     lit32   0x2801 @0x43fe8               <-- data, no code path
```

`0x9D30`/`0x9E04` are the read and write inside the password wipe `0x9CE8` (dcmd `0xC008050C`,
`77` §4.4 / `78` §3.4): `A := B`, `memset(+0x30, 0xFF, 0x40)`, secure write. **That function can
only erase a password, never create one** — it has no hashing, no salt generation, and it
preserves `+0x0C`/`+0x10`/`+0x14`/`+0x20`.

Additional structural argument: **`/proc/boot` contains no `libnvram.so.1`** (imports are
`libc.so.3`, `libsocket.so.3`, `libm.so.2`, `libz.so.2`, `libhuapi.so.5`, `libsbg56`, `libcam`,
`libdisputil`). The only `/dev/nvram` server is `nvram_resource_manager`
(`sanity.cfg:36-38`), so no other boot-image process can even reach a record.

### 7.3 The device-password component is named — and it is not in the corpus

```
# dumped4869ifs/proc/boot/sanity.cfg:110-111
550  PROCESS       c  "PasswordManager"
550  STATE         c  "PasswordManager" "RECEIVE"
```

A boot-critical (`sanity`-checked, priority 550) service called **`PasswordManager`**. That is
the TabletOS component that owns the device-password UI/policy — and its binary, its PPS object
and any `libnvram` use are all outside this workspace. This is the honest endpoint of task 4:
the only OS-visible password machinery in the corpus is (a) the boot stage / loader (docs
`75`/`79`), (b) the resmgr wipe, (c) the name of the missing service.

### 7.4 Strings that look like hits but are not

| finding | where | verdict |
|---|---|---|
| `Password:` , `/usr/bin/passwd`, `-c Only use the QNX4 compatible password encryption routine`, `-u Only use the Unix compatible…` | `proc/boot/login` (26 KB) | QNX text-console `login`/`passwd` for `/etc/passwd` accounts. Nothing to do with NVRAM, `0x2801` or the tablet device password. |
| `/etc/passwd`, `Password file does not exist`, `Password and Shadow files on different devices` | `proc/boot/waitfor` | same QNX account machinery |
| `_pthread_spin_*`, `nanospin_ns`, `disp_usecspin_init` | libc, libdisputil, drivers | substring `pin` only |
| `authman` in `sanity.cfg:64-66` and `base.py:469` (`/dev/authman`) | — | capability/debug-token service; deckard's only use is `getcap … permanent` (`installer/application.py:684-687`) |
| `5060` / `0x13C4` as a code constant | none | not in the resmgr, not in either `libnvram`, not anywhere in the boot image. It exists only as *record data* at `+0x0C`. SHA-512 as a primitive does exist in the OS (`hu_SHA512Msg/Begin/Hash/End` in `libhuapi.so.5`, imported e.g. by `proc/boot/rfs_validator`), so a user-space `PasswordManager` could implement the scheme — but that binary is absent. |

### 7.5 Net answer for task 4

Within the corpus, **no code anywhere writes `0x2801` except the resmgr's erase-only wipe**, and
**no code anywhere writes or compares `0x2002` except the resmgr's PPS publisher**. There is no
"set password" / "change password" / "PIN" API in the boot image, in the two `libnvram` builds
(verified in `78` §1.4/§3.1 — neither library contains `0x2801`, `0x2002`, `5060`, or the strings
`PIN`/`password`), or in the decompiled deckard tree. The setter is `PasswordManager` on the
RCFS, out of reach here.

---

## 8. Answers to the six task items

1. **Python NVRAM surface** — `§2`: four IDs only (`0x2074`, `0x207C`, `0x2819`, `0x2852`), all
   installer bookkeeping, all reached through a thin `os.open("/dev/nvram")` +
   `nto.pread/pwrite(fd, size, offset = record_id)` layer with no devctl and no `libnvram`.
2. **Password/PIN in Python** — `§3`: none. Only APN credentials and authman capabilities.
3. **Password/PIN relationship** — `§4`, `§6`: `0x2801` is the **device-password verifier** and
   the live digest is literally `SHA-512^5060(salt ‖ "0000")`; `0x2002` is the **hardware device
   PIN** `0x50175127`. They are two unrelated records; the user's `0000` lives only in `0x2801`,
   and only as a one-way digest.
4. **Rest of the RCFS** — `§7`: corpus does not include it; within what exists, the only
   `0x2801` writer is the erase-only resmgr wipe, and the password service is named
   `PasswordManager` (`sanity.cfg:110`) but not present. No `5060` code constant anywhere.
5. **Bootrom cross-check** — `§5`: yes. The live `0x0A` response carries `0x2801`'s salt and
   iteration count; the live `0x0F` 64-byte response equals `HashPassV2("0000", …)` **and**
   equals `pass2(challenge ‖ record_digest)`. `0x2801` therefore holds the boot-password
   verifier, and `0x2002` is never used as an unlock credential (no comparison exists).
6. **Report** — this file.

---

## 9. Residual unknowns

1. **The resmgr handler behind `nto.pread/pwrite(offset = id)`** was not identified. All record
   reads funnel through `0x8FF0` and all writes through `0x9BE0`; the only *generic* call sites
   are the devctl dispatcher (`0xB9E4` read, `0xBA88`/`0xBAC4` write, `id = u16[msg+0x14]`) and
   internal helpers. No `io_read`-shaped call site was found, and the `resmgr_io_funcs` table was
   not located in the image (pointer table is built/relocated at load). The
   offset-is-the-record-ID behaviour is documented in `PLAYBOOK-REFERENCE.md` §5.1 and is implied
   by deckard's use; treat the *handler address* as open.
2. **Challenge vs. nonce.** The live `0x0A` challenge is `0xA1E864CA`; the record's `+0x20` is
   `0xA1E864CE` — three bytes identical, low byte differs. Either the challenge is a session
   variant of the stored nonce, or `+0x20` advanced after the trace (`FUN_80014F5C` bumps it on a
   successful password). Two samples cannot separate these; a third `PASSWORD_INFO` read with a
   fresh record dump would.
3. **The setter's recipe** (`salt ← random`, `+0x30 ← pass1(newpw)`, `+0x20` bump, write) is an
   inference from the verifier structure and the boot-stage writers in `75` §4.1. `PasswordManager`
   would have to be pulled from the RCFS to confirm.
4. **Is `0x2801` in the signed "secure records" set?** Still open (`77` §7.3): the per-platform ID
   list is relocation-time data. The resmgr's own wipe does write with `secure = 1`, which hints
   it belongs to that class, but the list itself is unresolved.

---

## 10. Reproduction

```bash
A=/home/psyden/rea-work/bootdumps-2026-09-11/analysis
# 1. Python NVRAM surface
grep -rn 'NV_OSSTORE\|pfm.pread\|pfm.pwrite' $A/deckard-2.1.0.1917 $A/deckard-2.0.0.4869
# 2. no password/PIN in Python
grep -rniE 'passw|passwd|\bpin\b|credential|unlock' $A/deckard-2.1.0.1917
# 3. the digest is SHA-512^5060(salt||"0000")
python3 - <<'PY'
import hashlib, struct
rec = bytes.fromhex(open('/home/psyden/rea-work/bootdumps-2026-09-11/analysis/'
                         'playbook-live-2026-10-21/nv-0x2801.txt').read().splitlines()[2])
salt, n, pw = rec[0x10:0x18], struct.unpack_from('<I', rec, 0xc)[0], b'0000'
h = pw
for c in range(n):
    h = hashlib.sha512(struct.pack('<I', c) + salt + h).digest()
print(n, h.hex() == rec[0x30:0x70].hex())          # -> 5060 True
# 4. bootrom 0x0F response == HashPassV2("0000") == pass2(challenge || record digest)
def rnd(s):
    for c in range(n):
        s = hashlib.sha512(struct.pack('<I', c) + salt + s).digest()
    return s
ch = bytes.fromhex('ca64e8a1')
wire = bytes.fromhex('21032517b6ed086a' 'eda77938964a3b77' 'b5db15a6c0fe6651'
                     'd85088ad500b65b9' '626c34ea77dc8f7f' '6bc6a430b8957bfd'
                     '6b930580df2f1e1d' '09a67c018a92bbd3')
print(rnd(ch + rnd(pw)) == wire, rnd(ch + rec[0x30:0x70]) == wire)   # -> True True
PY
# 5. resmgr: only two 0x2801 code sites, and the PIN is read-only-for-PPS
python3 - <<'PY'   # ARM movw immediate scan (see §7.2 for the full output)
import struct
d = open('/home/psyden/rea-work/bootdumps-2026-09-11/analysis/playbook-live-2026-10-21/'
         'device-binaries/nvram_resource_manager','rb').read()
for i in range(0, len(d)-4, 4):
    w = struct.unpack_from('<I', d, i)[0]
    if (w & 0x0FF00000) == 0x03000000:                     # ARM movw
        imm = ((w >> 16) & 0xF) << 12 | (w & 0xFFF)
        if imm in (0x2801, 0x2002, 0x2802, 0x2074):
            print(hex(i), hex(imm))
PY
```

Referenced artifacts: `playbook-live-2026-10-21/nv-0x2801.txt`,
`playbook-live-2026-10-20/bb10mt-urb-trace.log`, `playbook-live-2026-10-21/device-binaries/`,
`playbook-dev/dumped4869ifs/proc/boot/sanity.cfg`, `/home/psyden/rea-work/bb10mt-main/ucrypto.pas`.
