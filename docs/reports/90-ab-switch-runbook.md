# 90 — Phase 1 A/B switch dry run: exact `0x2074` writes, verification, fallback, recovery

> **Superseded 2026-11-01: the unit was flashed and is armed+sealed; see `STATE.md`.**
> This runbook describes the pre-flash product-path state.  It is kept as evidence,
> not as a procedure.

Track C of the 2026-10-25/26 session (see `87-next-session-subagents.md`).
Status: **static analysis only**. No device was touched, no USB/NVRAM/RPMB/eMMC/
flash operation was performed, nothing was rebooted. Every device-side command
below is a *proposal*, marked **MAIN SESSION ONLY**.

Evidence classes used throughout:

* **PROVEN** — read directly out of the artefacts in this repo this session
  (decompiled Python, disassembly of `hd0head.bin`, live record dumps).
* **INFERRED** — a conclusion drawn from those bytes but not independently
  observed on hardware.

Reproduce everything with `analysis/90-nvram2074.py` (§12).

---

## 0. Summary of findings

1. The Phase 1 write is **three `nvram patch` commands totalling 15 bytes** on a
   72-byte record; nothing else in the record is touched by construction.
2. `Platform.switch()` on a **V3** record produces the same bytes **except
   `max_boot_count = 4`**, which must be patched back to 0 — and that patch is a
   *hard safety requirement*, not cosmetic: with `max=4` and an empty region 1
   the unit boots 4 times and then deterministically falls into the bootloader's
   infinite verify-retry loop on the 5th boot (§6.1.2).
3. `OsRegionV2.switch()` is dead code because of the `_replace(reqested_region=…)`
   typo: `_replace` builds the tuple, then raises on the unexpected field name, so
   the assignment never happens **and** `Platform.switch()` never reaches
   `write()`. A V2 record cannot be switched at all.
4. In the live state (`0x2801` A=0, B=10) the bootloader **cannot** reach the
   `0x800149C2` nuke: the gate returns 10 and jumps over the normal path. A
   failed Phase 1 produces a **retry loop**, not a nuke and not a region-0
   fallback failure — and the loop re-services the host command channel (§6.1.1).
5. `0x2074` is **only written after a successful verify** (`0x80014508`), so a
   failed Phase 1 boot leaves our bytes exactly as written: the failure is
   idempotent and re-testable.
6. The bootloader *does* read and write **`+0x0C` `flags` bit 0** — this
   contradicts `83` §2.3/§10.1 ("preserved by every writer, interpreted by none").
   It also gives Phase 1 a free discriminator for whether the fallback actually
   ran (§5.6).
7. **`Platform.sane()` is not the restore the plan describes**: V3 `sane()` sets
   `requested := actual` and `max := 0` only; it does **not** zero the boot
   counters and does **not** touch `epoch`. Restoring "counts 0" needs an extra
   patch (§4.3).

---

## 1. Verified baseline (do not assume — these are the bytes)

Live `0x2074` = `NV_OSSTORE_OS_REGION` (8308 = 0x2074), `OsRegionV3`,
`struct.Struct('=IBBHHHIQ24s24s').size == 72` — **PROVEN** by
`analysis/playbook-live-2026-10-21/after-0x2074.txt` (72 bytes) and
`deckard-2.1.0.1917/platform/osregion.py:453-455` /
`deckard-2.0.0.4869/platform/osregion.py:305` (same struct in the running OS).

```
0000  03 00 00 00 00 00 7c 01 00 00 00 00 00 00 00 00
0010  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0020  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0030  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0040  00 00 00 00 00 00 00 00
```

| off | size | field | live | note |
|---|---|---|---|---|
| `+0x00` | u32 | `version` | 3 | V3 |
| `+0x04` | u8 | `requested_region` | 0 | first region to try |
| `+0x05` | u8 | `actual_region` | 0 | written by the bootloader |
| `+0x06` | u16 | `boot_count_0` | `0x017C` | one increment per successful region-0 boot |
| `+0x08` | u16 | `boot_count_1` | 0 | |
| `+0x0A` | u16 | `max_boot_count` | 0 | **0 = unlimited** |
| `+0x0C` | u32 | `flags` | 0 | bit 0 is RMW'd by the bootloader (§5.6) |
| `+0x10` | u64 | `epoch` | 0 | `is_grace()` permanently true |
| `+0x18` | 24 s | `version_id_0` | empty | |
| `+0x30` | 24 s | `version_id_1` | empty | |

Digests of the baseline artefacts used here:

| file | sha256 |
|---|---|
| `nv-0x2074.txt` (pre-reboot, `0x017B`) | `e816f046645fc13c0b0688a4a38ec31b2266ad84a003457512b7017df8923186` |
| `after-0x2074.txt` (post-reboot, `0x017C`) | `6635ae300e854e0dc35b48727352151d520fb53477ba3e02e43fa01c5932fb35` |
| record bytes, `after-0x2074` | `67897acffa1fe569cdaf39b939ce8826a2726ed19bf7aae11a523fad021cd083` |
| `nv-0x2801.txt` = `after-0x2801.txt` | `5311a1862bdf60617305936be5fcfda2fb8094758f798cf87e899d56d38c0628` |

`0x2801` **PROVEN** to be in the retry branch: `+0x18` = 0, `+0x1c` = 10
(`0x00000000`, `0x0a000000` at record offsets 0x18/0x1c in
`nv-0x2801.txt`), so `FUN_80014F84` returns `clamp(10,3,10) - 0 = 10 != 0`
and `0x8001499E bne 0x80014A1E` is taken. `0x2040` and `0x2004` are absent
(0 bytes), which the pre-initialised `2` at `0x80014978` turns into the gate
case (tbb `0x8001498E`, entry 2 → `0x80014998`). The `0x2801` gate is therefore
**not** a variable of this test.

---

## 2. Byte-level Phase 1 write

### 2.1 Target record

`requested=1`, `actual=0`, `boot_count_0=0`, `boot_count_1=0`,
`max_boot_count=0`, `epoch=now`, `flags` preserved, `version=3`, version IDs
preserved.

```
0000  03 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00
0010  f9 be dd 6a 00 00 00 00 00 00 00 00 00 00 00 00
0020  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0030  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0040  00 00 00 00 00 00 00 00
```

`[+0x10..+0x17] = f9 be dd 6a 00 00 00 00` is the u64 example
`epoch = 0x6ADDBEF9 = 1792917241 = 2026-10-25T08:34:01Z` (host clock at the time
of writing; **use the device clock**, see §2.4). `flags[+0x0C] = 00000000` and
both version IDs = 24 NULs are the live values carried over unchanged.

Difference from the live baseline — exactly four byte runs:

```
+0x04          00 -> 01                    requested_region 0 -> 1
+0x06..+0x07   7c 01 -> 00 00              boot_count_0 0x017C -> 0
+0x10..+0x17   00*8 -> f9 be dd 6a 00*4    epoch 0 -> now
(no other byte differs: +0x08/+0x0A already 0, flags already 0, IDs already empty)
```

15 changed bytes, 57 identical. `version` at `+0x00` and the whole V3 tail are
untouched.

### 2.2 (a) `nvram patch` sequence — the primary form

**MAIN SESSION ONLY.** `nvram` binary = `nvram-editor/nvram` (`nvram.c:360-401`):
`patch` reads the whole record (`nv_get_record`), `memcpy`s the new bytes at the
given offset, then writes back the **same length** with `nv_write_record`
(insecure copy) followed by `nv_write_secure_record` (secure copy), aborting if
the insecure write fails.

```sh
# 0. preconditions (read-only)
cd /accounts/devuser
ls -l /proc/boot/libnvram.so.1          # RAM symlink: must be re-created after any reboot
./nvram read 0x2074 hex                 # MUST print "Recieved bytes: 72"
date +%s                                # capture the device clock

# 1. requested_region = 1                                        (+0x04, 1 byte)
./nvram patch 0x2074 0x04 0x01

# 2. boot_count_0 = boot_count_1 = max_boot_count = 0            (+0x06..+0x0B, 6 bytes)
./nvram patch 0x2074 0x06 0x00 0x00 0x00 0x00 0x00 0x00

# 3. epoch = now                                                 (+0x10..+0x17, 8 bytes)
E=$(date +%s)
./nvram patch 0x2074 0x10 0x$(printf %08x $((E & 0xffffffff))) 0x$(printf %08x $((E >> 32)))
```

Notes that matter:

* Offsets and the block id are parsed with `strtol(...,0)`, so `0x2074` / `0x04`
  are hex — **PROVEN** (`nvram.c:361-362`).
* `parse_data_args` writes any `0x…` token with **more than two hex digits** as
  *little-endian* bytes, `ceil(hexdigits/2)` of them — **PROVEN**
  (`nvram.c:80-102`). So `0x6addbef9` → `f9 be dd 6a`. Tokens with ≤ 2 hex
  digits are one byte.
* **Do not pass the epoch as a single >4-byte token.** The editor stores the
  value in `unsigned long` and parses with `strtoul`; on 32-bit ARM/QNX
  (`ILP32`) that is 32 bits, so a 16-digit token saturates at `0xFFFFFFFF` and
  silently writes `ff ff ff ff` plus zero padding. The two-token form above
  (low dword, then high dword) is safe and exact — **INFERRED** from the C type,
  not observed on the device.
* Deliberately **never patch `+0x0C..+0x0F`** (flags) or `+0x18..+0x47`
  (version IDs). Preserving them by *not touching them* is stronger than
  preserving them by re-writing the value we believe they have.
* One command per field group means a mid-sequence abort leaves a legible state,
  not a half-written blend.

### 2.3 (b) `Platform.switch()` + follow-up patch — the deckard form

`Platform.switch()` (`base.py:319-325`) takes `_nvlock`, calls
`OsRegionV3.switch()`, `write()`s 72 bytes at offset 8308 and reloads.
`OsRegionV3.switch()` (`osregion.py:559-560`, identical at 2.0.0.4869:410):

```python
self._t = self._t._replace(requested_region=self.inactive,      # actual ^ 1 == 1
                           boot_count_0=0, boot_count_1=0,
                           max_boot_count=self.DEF_BOOT_COUNT,  # == 4  <-- the problem
                           epoch=int(time.time()))
```

It produces exactly the §2.1 record **except `+0x0A = 04 00`**:

```
0000  03 00 00 00 01 00 00 00 00 00 04 00 00 00 00 00
0010  f9 be dd 6a 00 00 00 00 ...
```

so the follow-up is mandatory:

```sh
# after Platform.switch() has written the record
./nvram patch 0x2074 0x0a 0x00 0x00        # max_boot_count = 0 (unlimited)
```

Verified by construction in `90-nvram2074.py`: `switch()`-then-patch is
**byte-identical** to the §2.2 patch sequence ("result must equal the Phase 1
record: YES").

`switch()` does not touch `actual_region`, `flags` or the version IDs, and it
writes all 72 bytes from the *loaded* object, so those survive a round trip —
provided the record was read as V3. Exact on-device invocation is left to the
main session (`deckard` is Python 2.7 and only present on the unit); the
underlying operation is a 72-byte `pwrite` at 8308 (`osregion.py:288-292`), which
is why the patch path is the recommended one: no Python 2.7, no service state,
no stale object.

### 2.4 Why V3 is the only working switch path (the `_replace` typo)

`OsRegionV2.switch()` — `osregion.py:446-447` (2.1.0.1917) and
`deckard-2.0.0.4869/platform/osregion.py:297` — contains:

```python
self._t = self._t._replace(reqested_region=self.inactive, boot_count_0=0, ...)
#                                 ^^^^^^^^^^^^^^^^ typo: "requested_region"
```

CPython 2.7's `namedtuple._replace` is:

```python
result = _self._make(map(kwds.pop, _self._fields, _self))
if kwds:
    raise ValueError('Got unexpected field names: %r' % kwds.keys())
return result
```

so the right-hand side **raises `ValueError` before the assignment**, and the
raise propagates through `Platform.switch()` before `self.osregion.write(self)`
is reached. Consequences — all **PROVEN** from the code:

1. No field is updated (not even the counts or `epoch`) — the exception happens
   on the RHS, so `self._t` is never rebound.
2. Nothing is written to NVRAM: the record on media is unchanged.
3. The error is a bare `ValueError`, not a `QualifiedPlatformError`, so the txn
   layer reports it as an unhandled failure.
4. A V2 record therefore has exactly two ways to move `requested`:
   the `requested` property setter (`osregion.py:406-413`, which works because
   it spells the field correctly) and `sane()`. There is no `switch()`.

On this unit none of that is reachable: the live record is `version = 3`, so
`OsRegion.load()` tries `OsRegionV3.read` first and succeeds
(`osregion.py:255-263`). **V3 `switch()` is the only working switch path**, and
it is the only path that also sets `max := 4` / `epoch := now` atomically, which
is why the follow-up patch exists.

Related trap: if `0xC0 COMPLETE` (ramloader) ever runs, it writes
`{u32 2, u16 0, u8 …}` into `0x2074` (`66` §3.5) — a **version downgrade** to 2.
After that, `OsRegionV3.read` raises `OsRegionVersionError`, `load()` falls back
to `OsRegionV2.read` (24 bytes), `Platform.switch()` raises the typo error
forever, and the resmgr's `0xD680` rollback clamp still works. Never use `0xC0`
as a repair for a Phase 1 problem.

### 2.5 Preconditions and hazards for the write

| # | check / hazard | why |
|---|---|---|
| 1 | `./nvram read 0x2074 hex` returns **72** bytes | `patch` re-writes the length it read; against an absent record it computes size 0 and calls `nv_write_record(id, buf, 0)` — a zero-length/truncated record. **PROVEN** (`nvram.c:367-391`). |
| 2 | `/proc/boot/libnvram.so.1` resolves | RAM symlink; gone after every reboot (`76` §6). |
| 3 | no installer/update transaction is running | `base.py` caches `_osregion` and only reloads via `switch()`/`sane()`; a stale in-memory copy written by a later `sane()` would **silently revert** our patch. Read back immediately before rebooting. |
| 4 | `patch` writes insecure **then** secure | if the secure write fails the editor exits non-zero but the insecure copy is already changed → possible split-brain between `./nvram read` (insecure) and the bootloader (unknown preference, `79` residual 1). Check both write exit codes; the post-boot counter increment (§5.7 / W3) is the authoritative confirmation. |
| 5 | never write `requested`/`actual` > 1 | the bootloader masks `&1` (`0x80014314`) but the OS does not validate on read: `active = 2` → `inactive = 3` → `('hd1','hd2')[2]` IndexError in `winchester.py:35`. |
| 6 | device clock | epoch from the wrong clock lands in the past → `is_grace()` false immediately → the purge path is armed at once (§6.1.5). Use `date +%s` on the unit. |

---

## 3. Read-back verification (byte-for-byte)

**MAIN SESSION ONLY.** Three independent witnesses, in increasing strength:

```sh
# W1 — the record itself, from the OS (reads the insecure copy)
./nvram partialread 0x2074 0 72 hex
```

must print (one line, 72 bytes):

```
03 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00
<f9> <be> <dd> <6a> 00 00 00 00 00 00 00 00 00 00 00 00
00 × 40                                     (<- +0x18..+0x47, 48 bytes)
```

i.e. `+0x00`=3, `+0x04`=1, `+0x05`=0, `+0x06..+0x0B`=0, `+0x0C..+0x0F`=live
flags, `+0x10..+0x17`=the epoch written, `+0x18..+0x47`=the pre-write 48 bytes.

Offsets in `partialread` are parsed with `strtol(...,0)`, so a bare `10` is
**decimal** 10 = `+0x0A` and `16` is decimal 16 = `+0x10`; write `0x0a`/`0x10`
when in doubt. Field-level assertions:

```sh
./nvram partialread 0x2074 4 12 hex    # -> 01 00 00 00 00 00 00 00 <flags*4...>
./nvram partialread 0x2074 16 8 hex    # -> f9 be dd 6a 00 00 00 00
```

```sh
# W2 — the NVRAM resource manager's own decode of the record it just re-read
cat /pps/system/nvram/osregion
```
Expected attributes (**PROVEN** strings in
`analysis/playbook-live-2026-10-21/device-binaries/nvram_resource_manager`):
`RequestedBootRegion::1`, `ActualBootRegion::0`, `RequestedBootCount::0`,
`ActualBootCount::0`, `MaxBootCount::0`, `InstallTime::<epoch>`.
W2 proves the **resmgr** accepted the write and re-published it (the resmgr
republishes after any `0x2074` write, `83` §1).

```sh
# W3 — the bootloader's own confirmation, after the Phase 1 boot (see §5.7)
./nvram partialread 0x2074 0 12 hex    # expect: 03 00 00 00 01 00 01 00 00 00 00 00
```
W3 is the only witness that proves the **bootloader** read our bytes.

Do **not** accept a read-back that shows `version != 3` (downgrade), or
`+0x0A != 0` (budget left armed), or `+0x04 != 1` (stale in-memory copy
overwrote us).

---

## 4. Restore to sane

### 4.1 What the plan asks for

`requested = actual = 0`, `boot_count_0 = boot_count_1 = 0`, `max_boot_count = 0`.

### 4.2 Restore sequence — variant A ("sane", matches the plan's field list)

**MAIN SESSION ONLY.** Run while the unit is booted in region 0, before any
reboot cycle is handed to anyone else.

```sh
cd /accounts/devuser
./nvram patch 0x2074 0x04 0x00                                  # requested = 0 (actual is already 0)
./nvram patch 0x2074 0x06 0x00 0x00 0x00 0x00 0x00 0x00          # counts 0, max 0
./nvram patch 0x2074 0x10 0x00000000 0x00000000                  # epoch = 0 (back to live baseline)
./nvram partialread 0x2074 0 72 hex
```

Result, byte-for-byte:

```
03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  00 × 56
```

This is the pre-test baseline in every field **except `boot_count_0`**, which
becomes 0 instead of `0x017C` (see §4.4).

Equivalent deckard call: `Platform.sane()` — but see §4.3, it is *not* equal to
the three patches.

### 4.3 `sane()` is narrower than the plan's restore

`OsRegionV3.sane()` (`osregion.py:556-557`, 2.0.0.4869:407):

```python
self._t = self._t._replace(requested_region=self._t.actual_region,
                           max_boot_count=self.MAX_BOOT_COUNT)   # 0
```

It sets **only** `requested := actual` and `max := 0`. It does **not** zero
`boot_count_0`/`boot_count_1` and does **not** touch `epoch` or `flags`. So
`Platform.sane()` after Phase 1 leaves `boot_count_0 = 1` and `epoch = now`,
i.e. `is_grace()` becomes time-limited (§6.1.5) instead of permanently true.
If the intent is the plan's field list, `sane()` must be followed by

```sh
./nvram patch 0x2074 0x06 0x00 0x00 0x00 0x00 0x00 0x00
./nvram patch 0x2074 0x10 0x00000000 0x00000000
```

### 4.4 Variant B — exact pre-test baseline (restores `boot_count_0`)

If bit-exactness with the 2026-10-21 baseline matters (e.g. for a later
"one increment per boot" measurement), restore the captured value instead:

```sh
./nvram patch 0x2074 0x04 0x00
./nvram patch 0x2074 0x06 0x7c 0x01 0x00 0x00 0x00 0x00     # boot_count_0 = 0x017C
./nvram patch 0x2074 0x10 0x00000000 0x00000000
```

→ byte-identical to `after-0x2074.txt`. (Note the bootloader will already have
made it `0x017D`, so this is a *deliberate* roll-back of one count; prefer
variant A unless there is a reason.)

Restoring `flags` is not needed today (`+0x0C` is 0, and only bit 0 is ever
touched, §5.6), but the **pre-test flags word must be in the backup** so the
restore never has to guess.

---

## 5. Predicted boot outcome

### 5.1 State at the moment of reset

`0x2074` = §2.1 record. `0x2801` A=0/B=10. `0x2040` absent (→2). `0x2004`
absent. Region 1 (`/dev/hd2`) is all-zero at the IFS offset `0x100000`
(`85` §0, `85` §4). Region 0 (`/dev/hd1`) is the running, ECDSA-signed image with
`region[+0xd4] = 0`. `BOOTCTR_FCTRY` = 0 (product) as read by `0xDE` action 0
on the connected unit (`HANDOFF` §1: `status=0, mode=product, count=0`).

### 5.2 Step-by-step (all addresses are `hd0head.bin`, runtime base `0x80000000`)

| # | where | what happens | class |
|---|---|---|---|
| 1 | `0x80014974-0x80014980` | pre-init `2`, read `0x2040` → absent, buffer keeps `2` | PROVEN |
| 2 | `0x80014986-0x8001498A` | `2 < 9` → tbb `0x8001498E` entry 2 → `0x80014998` | PROVEN |
| 3 | `0x80014998` → `0x80014F84` | gate: `clamp(+0x1C,3,10) - [+0x18]` = `10 - 0` = **10** | PROVEN |
| 4 | `0x8001499E` | `bne 0x80014A1E` → **retry branch**, normal path skipped | PROVEN |
| 5 | `0x80014A1E-0x80014A2C` | `bl 0x8001453C` sets flag `[global+4]=1` and tail-calls the driver `0x800142A8` | PROVEN |
| 6 | `0x800142B0` | driver calls `0x8000911A` (BOOTCTR_FCTRY) → **0** → normal region path | PROVEN (value INFERRED from the live `0xDE` read) |
| 7 | `0x800142DC-0x800142E8` | read `0x2074`, 72 bytes; `72-0x18 = 0x30 < 0xA9` → size OK | PROVEN |
| 8 | `0x8001430C-0x80014318` | `requested = rec[+4] & 1` = **1**; published to the driver's global | PROVEN |
| 9 | `0x8001433A-0x80014396` | `flags[+0x0C] &= ~1`; two media probes; **if both probes return 0**: `flags |= 1` **and `region ^= 1`** | PROVEN as code; branch condition INFERRED (§5.6) |
| 10 | `0x8001439A-0x800143AC` | log `"Requested region %d"` / `"Verifying region %d"` | PROVEN (format strings `79` §2) |
| 11 | `0x800143B2-0x800143CC` | `count = u16[+0x06+2*i]` = `boot_count_1` = **0**; `max = u16[+0x0A]` = **0**; `0 < 0` false → `cbz max` → **max==0 ⇒ verify** | PROVEN |
| 12 | `0x800144AC` → `0x80014148` | verify region **1**. `0x8001415E` re-reads the selector → 0 → `0x80014254 cbnz r6` false → `0x80014716` **ECDSA** | PROVEN |
| 13 | region 1 is all-zero | IFS at `hd2+0x100000` is zero, so no valid header/cookie/signature → verify **fails**, no "Verified" log | INFERRED (dump PROVEN, verifier behaviour inferred; expected to fail fast on the header, not after hashing 512 MB) |
| 14 | `0x800144C2` | result 0 → toggle: `i ^= 1` → **region 0**; iteration 2 of 2 | PROVEN (`0x8001451E-0x8001452E`) |
| 15 | `0x800143B2` again | `count = boot_count_0` = 0, `max` = 0 → verify | PROVEN |
| 16 | `0x800144AC` | verify region **0** → ECDSA → **passes** (this is the image the unit boots today) | PROVEN for the path, INFERRED for the outcome |
| 17 | `0x800144C4` | log `"Verified region 0 successfully"` | PROVEN |
| 18 | `0x800144D4-0x800144EC` | clear rule (see §5.5): `rec[+4]`=1 vs `i`=0 → **different** → jump to increment, **no clear** | PROVEN |
| 19 | `0x800144EE-0x80014500` | `boot_count_0 += 1` → **1** (16-bit, wraps at 0xFFFF) | PROVEN |
| 20 | `0x80014504` | `rec[+5] = 0` → **`actual` stays 0** | PROVEN |
| 21 | `0x80014502/0x80014508` | write `0x2074`, **same length as read (72)** → V3 tail preserved | PROVEN |
| 22 | `0x8001451A` → `0x800142CA` | return 1 → `0x80014A30` `cbz` false, `cmp r0,#1` → `beq 0x80014A3E` | PROVEN |
| 23 | `0x80014A3E-0x80014A42` | re-read selector → 0 → `cbz` → `0x80014A68` → `0x80014A8A` → `0x80014A90` **enter OS** | PROVEN |
| 24 | — | region 0 OS boots normally, ~one boot later than usual | INFERRED |

### 5.3 What the record looks like afterwards

```
0000  03 00 00 00 01 00 01 00 00 00 00 00 00 00 00 00
0010  f9 be dd 6a 00 00 00 00 00 00 00 00 00 00 00 00
0020..0047  00 × 40
```

| field | before boot | after boot | why |
|---|---|---|---|
| `version` | 3 | 3 | preserved |
| `requested` | 1 | **1** | the bootloader never writes `+0x04` |
| `actual` | 0 | **0** | winner is region 0 → same value; `0x80014504` writes 0 |
| `boot_count_0` | 0 | **1** | one increment on successful verify |
| `boot_count_1` | 0 | **0** | never verified; and not cleared in this path either way |
| `max_boot_count` | 0 | **0** | not written |
| `flags` | 0 | **0 or 1** | bit 0 is RMW'd by the driver (§5.6) |
| `epoch` | now | now | not written |
| version IDs | empty | empty | not written (72-byte write-back) |

The single uncertain byte is `+0x0C` bit 0.

### 5.4 The CRC-only variant

**Important structural point first:** "CRC" is not a property of the image, it is
the *mode* returned by `0x8000911A` (RPMB `BOOTCTR_FCTRY`), and the driver
branches on it **before** it reads `0x2074`:

```
0x800142B0  bl 0x8000911A
0x800142BA  cbz r0, 0x800142D0      ; mode 0 (product) -> read 0x2074, 2-region loop
0x800142BC  ...                     ; mode 1 or 2 -> region := 0, verify region 0 ONCE, return
```

So with a nonzero factory counter, `requested_region` is **ignored entirely**
and there is **no fallback**: exactly one verify of region 0. (This is `83` §6's
"factory path skips the record entirely", now confirmed at instruction level.)

| scenario | mode | what runs | outcome with `requested=1`, region 1 empty |
|---|---|---|---|
| **A. today (ECDSA product)** | 0 | ECDSA on region 1, then ECDSA on region 0 | region 1 fails → region 0 boots → `actual=0`, `bc0=1` (§5.3) |
| **B. `BOOTCTR_FCTRY = 1..50` (factory/CRC)** | 1 | `0x2074` **skipped**; region 0 only, via CRC gate `0x80013FB6` | gate needs `region[+0xd4] & 0xff == 6`; region 0 has `+0xd4 = 0` → fail → **no fallback**, `0x80014A30 cbz` → retry loop (`0x80014A6C` → `0x80014A20`) |
| **C. `+0xd4 = 6` but selector still 0** | 0 | ECDSA over both regions | region 0's ECDSA signature is invalidated by the `+0xd4` edit (it is inside the hashed `[start, end-0x230)` range) → region 0 also fails → retry loop |

Consequences to carry into `85`:

* Phase 1 must run in **scenario A**. Touching RPMB turns it into B and removes
  the very fallback the test is meant to demonstrate.
* Scenario C is why "make region 0 CRC-valid as a safety net" (`85` §2 Phase 3
  item 2) is **not** a safety net while the unit is in product mode: an image
  whose `+0xd4` was flipped to 6 cannot pass the ECDSA verifier that is actually
  running. The `+0xd4` edit and the `BOOTCTR_FCTRY` write must happen in the same
  step, or region 0 becomes unbootable.
* If region 0 were CRC-only **and** the selector were 1, an empty region 1 would
  never be consulted, so "requested=1 with region 1 empty" would be a no-op —
  and a CRC-invalid region 0 would hang the unit rather than fall back.

### 5.5 The clear/increment rule — correction to `83` §5.1

`83` states: *"the loser's counter is cleared when the winner is not the
requested region … if `rec[+4] != current_region` then `u16[+0x06+2*(current^1)]
= 0`, i.e. `boot_count_<requested> := 0`."*

The bytes at `0x800144D4` are `21 79 a8 68 81 42 08 d1`
(`ldrb r1,[r4,#4]` / `ldr r0,[r5,#8]` / `cmp r1,r0` / `bne 0x800144EE` at
`0x800144DA`), i.e.

```
if (requested == winner) { u16[+0x06 + 2*(winner^1)] = 0; }   /* clears the LOSER  */
u16[+0x06 + 2*winner] += 1;                                   /* then counts itself */
```

The comparison is **`beq` semantics for the clear**, not `bne`. Practical effect:

* steady state (`requested == actual`): the *other* region's counter is zeroed
  on every boot — which is exactly why live `boot_count_1` has stayed 0;
* fallback (`requested != winner`): **no clear at all** — a failed region keeps
  its accumulated counter (sensible: keeps its budget being consumed).

For Phase 1 the two readings produce the **same bytes** (`boot_count_1` is 0
either way), so the artefact is unaffected — but `83`'s sentence should be
corrected before anyone reasons about budget accounting on a real rollback.

### 5.6 `flags` bit 0 is written by the bootloader — and gives Phase 1 its witness

`fp` is set to `record+0x0C` (`0x8001433A add.w r0,r4,#0xc`), and the ARM
helpers resolve unambiguously:

* `0x80018EF8` = `u32 load` (`ldrb`/`orr` ×4, little-endian);
* `0x80018F18` = `u32 store` (`strb` ×4, little-endian).

So the driver executes

```
flags &= ~1;                                  /* 0x80014340-0x8001434A */
if (probe(r5,6) == 0 && probe(r8,6) == 0) {   /* 0x8001434E-0x8001437A, r5=0x00DC0014, r8=0x00DD0015 */
    flags |= 1;                               /* 0x8001437C-0x80014388 */
    region ^= 1;                              /* 0x8001438C-0x80014392  <-- first attempt becomes requested^1 */
}
```

**PROVEN**: `+0x0C` is read-modify-written by the bootloader, so `flags` is *not*
"interpreted by none of them" (`83` §10.1) and is *not* preserved across a boot.
**INFERRED**: the probe semantics (a two-entry table lookup at `0x800092DA` with
index 6; `r5`/`r8` are descriptor words `0x00DC0014` / `0x00DD0015`) and
therefore whether the flip branch fires on this unit.

Two consequences:

1. The post-boot record's `+0x0C` bit 0 is **0 or 1**, not a fixed value. It is
   the only predicted byte in doubt.
2. It is a free discriminator for the *purpose* of the test:
   * `+0x0C` bit0 = **0** ⇒ the flip branch did not run ⇒ region **1** was tried
     first, failed, and region 0 was reached by **fallback** ⇒ Phase 1 proved
     what it set out to prove;
   * `+0x0C` bit0 = **1** ⇒ the flip branch ran ⇒ region **0** was tried first
     and succeeded immediately ⇒ **the fallback was never exercised** and the
     test is inconclusive (repeat after investigating the probe).
   The authoritative witness, if a console/log is available, is the driver's own
   `"Requested region %d"` / `"Verifying region %d"` log pair.

### 5.7 What Phase 1 does and does not prove

Proves (if `+0x0C` bit0 = 0): `requested` is honoured as the first choice; a
failed region is rejected without a write; the unit falls back and boots region
0; the counter of the successful region increments; `actual` is written; the
72-byte V3 tail survives a bootloader write-back; no hang.

Does **not** prove: that `actual` can *change*. Region 1 cannot verify today, so
`actual` stays 0 — the `write of +0x05` is only exercised when a *different*
region wins. That is Phase 2's job. `85` §2 Phase 1 step 2 ("proves the
requested/actual/fallback path") overstates it: Phase 1 proves
requested+fallback+counter, not `actual` switching.

---

## 6. Failure modes

Rule that dominates the whole table: **`0x2074` is written only after a
successful verify** (`0x80014508`; the failure path at `0x80014532` logs
`"Verification of regions failed"` and returns 0 without writing). A failed
Phase 1 boot therefore leaves our bytes intact and the failure is
**idempotent** — power-cycling repeats it exactly.

### 6.1 Symptom classes

| # | failure | symptom | detection | prevention / recovery |
|---|---|---|---|---|
| 6.1.1 | **retry loop (the normal failure)** — both regions fail to verify (region 0 corrupted, wrong mode, phantom header) | unit produces no OS; **not** a nuke and **not** a reset loop. `0x80014A30 cbz` → `0x80014A6C` → `0x80014A76-0x80014A88`: log, `bl 0x8001217E` (host-command dispatcher), `bl 0x800094BE`, then `cmp r6,#0; beq 0x80014A20` — **loop**. `r6` is 0 for the whole of boot main (`0x80014906 movs r6,#0`, and every callee preserves it), so the loop is unconditional | no `0x2074` change; unit likely still answers the bootrom/loader protocol (the loop services `0x80012182`) | try `bbusb --boot-mode` read-only first; then `0xE9` (§8) |
| 6.1.2 | **`max_boot_count = 4` left armed** (`switch()` without the follow-up patch) | **exactly 4 good boots, dead on the 5th**: on boot 5 `count(4) >= max(4)` and `max != 0` → `0x800143CE` logs `"Exceeded boot count (4 >= 4)"` → toggle to region 1 → also fails → both failed → 6.1.1 loop | `./nvram partialread 0x2074 10 2 hex` before reboot must be `00 00` | mandatory `patch 0x2074 0x0a 0x0000` |
| 6.1.3 | **`actual` corruption** (hand-written `actual=1` while running region 0) | boot is unaffected (the bootloader **never reads** `+0x05`; PROVEN — only `strb r0,[r4,#5]` at `0x80014504`). The OS is affected: `active=1` → `inactive = active^1 = 0` → `inactive_ifs()`/`inactive_rfs()`/`zone()` target **hd1, the running region**, so the installer stages a new OS **over the live OS**; `active_fingerprint` is read from hd2 | `cat /pps/system/nvram/osregion` → `ActualBootRegion`; `Platform.active_region` in installer logs | never write `+0x05` by hand. Only the bootloader sets it. Detect before any update transaction |
| 6.1.4 | **`requested != actual` left behind** (the expected post-Phase-1 state!) | `is_requested()` false → `Activator.run()` takes `__activate(force=True)` (`activator.py:136-143`) → region 0's app set is force re-activated on the next OS start; app-registration churn, log noise. After the grace window, `housekeeping()` → `purge()` (`processor.py:338-343/375-385`) → `invalidate()` zeroes **16 KB** of the inactive IFS and RFS (`base.py:344-354`) | `is_requested_region()` in installer logs; PPS `RequestedBootRegion != ActualBootRegion` | run §4 restore promptly after the boot; **do not** leave `requested=1` across a Phase 2 staging run |
| 6.1.5 | **grace window armed unintentionally** — epoch written from a wrong/past clock, or `now` + a >48 h gap before Phase 2 | `is_grace()` = `epoch == 0 or epoch + 172800 >= now` (`osregion.py:252-253`). With `epoch=0` grace is **permanent** (the live baseline). With `epoch=now` it expires 48 h later, after which the purge in 6.1.4 can zero the inactive region — i.e. it can destroy a staged Phase 2 image | `InstallTime` in PPS; compare with `date +%s` | either restore `epoch=0` (§4) or complete Phase 1→2 inside 48 h. Note `epoch != 0` also disables the blacklist update (`base.py:395`) |
| 6.1.6 | **`0x800149C2` "any nonzero selector nukes"** — `cbnz r0,0x80014A16` after `bl 0x8000911A` | **not reachable in the live state.** `0x800149BE/C2` is only reached through `0x800149A4` (tbb cases 3..8) or by falling through from `0x80014998` when the gate returns **0** (A ≥ B). Live A=0, B=10 → gate 10 → `bne 0x80014A1E` → the whole normal path incl. `0x800149C2` is skipped | — | the only way to reach it is to raise `0x2801 +0x18` to ≥ 10 (or shrink B). Both are outside this plan. **Do not patch `0x2801`** |
| 6.1.7 | **nuke path** `0x800156E8` (deletes `0x2801` at `0x8001587E`), then `0x80014A1A cbnz r4,0x80014A90` continues to the enter-OS path | only from the normal path (6.1.6) or `0x2040 ∈ {1, >=9}` | — | unreachable from Phase 1 |
| 6.1.8 | **A/B swap by unit confusion** — someone "fixes" the record to 1/2 | a stored `2` is masked with `&1` (`0x80014314`) → boots region **0**; a stored `1` in a human "region 2" mental model boots **hd2**. Silent, not an error | — | only ever write 0/1 |
| 6.1.9 | **phantom verification of an empty region 1** (verifier bug / all-zero image accepted) | after reboot `actual` = 1 and `boot_count_1` = 1; the unit jumps into a zeroed image (`0x80014A90` enter-OS) → reset/exception loop, no OS | W3 read-back (§3) shows `+0x05` = 1 | expected probability ~0 (ECDSA signature required), but it is the one outcome with no OS-side recovery: bootrom/loader only |
| 6.1.10 | **record destroyed by a bad patch** (patch against a 0-byte or short record; `nv_write_record(id,buf,0)`) | `0x2074` reads 0 bytes or a truncated record. Bootloader size check `size-0x18 < 0xA9` fails → logs `"Invalid NvOSRegionStruct (size %d, expected %d)"`, zeroes 0x18 bytes → **region 0, unlimited, counters 0** (`0x800142EC-0x80014308`) → the unit still boots region 0 | `./nvram read 0x2074 hex` → bytes != 72 | pre-check §2.5 #1. Recovery is benign here (see §8.3) |
| 6.1.11 | **V2 downgrade via `0xC0` COMPLETE** | `version=2` in a 72-byte record → `OsRegionV3.read` raises, `load()` falls to V2 (24 bytes); `Platform.switch()` raises the `_replace` typo forever; only `sane()`/the `requested` setter can move it | `+0x00` = `02 00 00 00` | never use `0xC0` as a repair |
| 6.1.12 | **write-back failure** at `0x80014508` | `bl 0x8000A9FE` returns nonzero → logs code `0x2F`, calls `0x80014C98`, **and still returns success / boots** (`0x80014510 cbz r0,0x8001451A`) → the counter does not advance and the record is not updated, but the boot looks fine | no `bc0` increment after the boot | NVRAM full/failing; verify with W3, do not assume the write happened |
| 6.1.13 | **split brain insecure/secure** | `./nvram read` shows the new bytes, the bootloader may read the old ones (which copy the boot stage prefers is unresolved, `79` residual 1) | post-boot W3 unchanged → Phase 1 "did nothing" | prefer the `patch` path (writes both); confirm with W3 |
| 6.1.14 | **stale in-memory OS region overwrites the patch** | an installer/update transaction later calls `sane()` from its cached `_osregion` → `requested` back to 0 | read-back immediately before the reboot | do not run update transactions between patch and reboot |

### 6.2 "Bootloader hang vs nuke vs fallback loop" in one line each

* **hang** = 6.1.1/6.1.2; retry loop, no write, no nuke, host channel serviced.
* **nuke** = 6.1.6/6.1.7; needs the normal path (A ≥ B) or `0x2040 ∈ {1,≥9}`;
  unreachable with A=0.
* **fallback** = the intended path (5.2 steps 13-23): one extra verify, one log
  line, then region 0 boots.

---

## 7. Backup checklist (before Phase 1)

Everything here is **read-only**. Nothing in this list writes to the device.

Destination convention (one directory per attempt, plus one copy off the laptop):

```
$D=/home/psyden/rea-work/bootdumps-2026-09-11/analysis/playbook-ab-phase1-<YYYY-MM-DD>/
D2=/media/<usb-stick>/playbook-ab-phase1-<YYYY-MM-DD>/     # second physical copy
```

**MAIN SESSION ONLY** for the device-side reads.

| item | command | expected size | why |
|---|---|---|---|
| `2074` | `./nvram read 0x2074 hex` | 72 | the record being changed |
| `2801` | `./nvram read 0x2801 hex` | 112 | password/attempt record, boot-critical, must be provably unchanged |
| `2802` | `./nvram read 0x2802 hex` | 112 | reserve; a failure-path write would show here |
| `2040` | `./nvram read 0x2040 hex` | 0 (absent) | `NukeStatus`; absence is load-bearing for the retry branch |
| `2004` | `./nvram read 0x2004 hex` | 0 (absent) | `JvmStart`; must stay absent |
| `2002` | `./nvram read 0x2002 hex` | 4 (`27 51 17 50`) | hardware PIN, untouched — record it so a later diff proves it |
| `205A` | `./nvram read 0x205a hex` | unknown | **new**: read during region verification (`0x800141F8` bit-test against `region[+0xd4]`); never dumped. Also the candidate source of the two probes in §5.6 |
| `206C` | `./nvram read 0x206c hex` | 0x400 | installer/region table, read by `0xE0` |
| PPS | `cat /pps/system/nvram/osregion` (and `/pps/system/nvram/deviceinfo`) | text | the resmgr's decode; the human-readable half of the record |
| clock | `date -u; date +%s` | text | makes `epoch` interpretable and detects skew |
| region 0 image | `dd if=/dev/hd1 of=$D/hd1.img bs=1M count=512` | 512 MiB | the running OS; the only pristine copy |
| region 1 image | `dd if=/dev/hd2 of=$D/hd2.img bs=1M count=512` | 512 MiB | must be all-zero at `0x100000`; proves the "empty region" premise |
| SIG zones | `dd if=/dev/hd1 of=$D/hd1.sig bs=4096 skip=240 count=16` (`0xF0000..0x100000`) and the same for `hd2` | 64 KiB each | 64-byte fingerprint at `0xF0000` (`winchester.py` `_OFF_SIG`), used by fingerprints/PPS |
| IFS headers | `dd if=/dev/hd1 of=$D/hd1.ifshead bs=1M skip=1 count=1` (and `hd2`) | 1 MiB | header + `+0xd4` + cookie `0xD7A82D1F`; record the `+0xd4` byte for both |
| RPMB factory counter | bootrom `0xDE` action 0 through `bbusb.py --boot-mode --from-bootrom` (**read-only**; the shipped loaders return without acting on actions 1/2) | 12-byte reply | the selector's value is the mode that decides the verifier; **do not write it** |
| bootrom/loader access path | record in `$D/ACCESS.md`: `modelID 0x06001A06`, `BR_ver 0x051B0014`, device password `0000`, loader `bbusb/loaders/loader_06001A06-00.bin` sha256 `c7cc33c796f9518bbc0eb929c6096f2473937ad486c579764aa4c57d8165393f` (238128 bytes), the exact `bbusb` command line that worked, and the qconn/SSH path (`BerryShell-V4.py`, `ssh -i rsa root@169.254.0.1`) | text | this is the recovery rope; it must be written down *before* it is needed |
| digests | `sha256sum $D/* > $D/SHA256SUMS`; re-check after the copy to `$D2` | text | integrity of the backup itself |

Also record, in `$D/NOTES.md`: the OS version on the unit (2.0.0.4869), the
installed editor paths and hashes (`/accounts/devuser/nvram` `160c51db…`,
`/accounts/devuser/libnvram.so.1` `295f1aba…`), and the fact that
`/proc/boot/libnvram.so.1` is a RAM symlink.

Two physical copies minimum: the whole point of the backup is the case where the
laptop or the unit is the casualty. `512 MiB × 2` per attempt is cheap on a
64 GB unit and on a $D volume.

Explicitly **not** to be touched, and therefore not "backed up" by writing
anything: RPMB content (read the counter only), `0x2819`/downgrade state
(already done), `0x2002`.

---

## 8. Recovery path if the unit fails to boot after Phase 1

**Golden rule: the failure mode of Phase 1 does not destroy anything.** `0x2074`
was written and is intact (§6 rule), region 0 is untouched, RPMB is untouched,
`0x2801` is untouched. So every recovery below is non-destructive until someone
writes something.

### 8.1 Step 0 — is it the retry loop or something else?

No OS after ~2 minutes, no repeated USB re-enumeration, no reset cycling →
almost certainly §6.1.1. If the unit re-enumerates as a BlackBerry USB device, the
retry loop is alive and the loader channel may be usable

### 8.2 Step 1 — bootrom/loader re-entry, read-only only

**MAIN SESSION ONLY** (this is a live step even though it only reads).

```sh
cd ~/rea-work/bbusb
python3 bbusb.py --boot-mode --from-bootrom --password 0000 --debug
```

* `0xDE` action 0 is a **read** in every shipped loader (`HANDOFF` §2:
  actions 1/2 are unimplemented and time out). Read `status/mode/count`.
* If the bootrom does not answer: the documented behaviour is that the bootrom
  can get stuck after a failed attempt; retry once, then power-cycle
  (`HANDOFF` §1). No NVRAM/flash implication.

### 8.3 Step 2 — read the record from the pre-OS side (`0xE9`)

**MAIN SESSION ONLY — read-only; do not send `0xC0` or `0xE0`.**

| opcode | direction | effect |
|---|---|---|
| `0xE9` | read | reads `0x2074`, **24 bytes**, into reply `0xFB` (`FUN_8010B954(0x2074,buf,0x18)`) — i.e. the V2-shaped prefix: version, requested, actual, counts, max, flags. Safe. |
| `0xC0` COMPLETE | **write** | writes `{u32 2, u16 0, u8 …}` into `0x2074` → **downgrades the record to V2** (§6.1.11) and permanently breaks `Platform.switch()`. Requires the image cursor at `0x20000` and `0x207C` present. **Never use for recovery.** |
| `0xE0` REMOVE_INSTALLER | **write** (installer/region table) | reads `0x206C` (0x400) and rewrites region state via `FUN_80119538`. **Do not use.** |

`0xE9` is therefore the correct recovery diagnostic: it tells you
`requested`/`actual`/`counts`/`max` *before* any OS runs, and it is read-only.
If `0xE9` shows `requested=1, actual=0, bc0=1, max=0` you are looking at exactly
the predicted post-Phase-1 record and the boot failure is a verifier/mode
problem, not a record problem.

### 8.4 Step 3 — repair levers, in order of increasing violence

1. **Nothing.** If the record is the predicted one and region 0 is intact, the
   most likely cause is that the unit is not in mode 0 (someone changed RPMB) or
   the fallback was never reached. Re-read `0xDE` first (§8.2).
2. **`./nvram delete 0x2074`** (OS booted only) — a *deliberate* absence is
   benign: the bootloader's size check fails and it substitutes a zeroed
   0x18-byte struct = "region 0, unlimited, counters 0"
   (`0x800142EC-0x80014308`), so region 0 boots. Caveat: `nvram delete` calls
   `nv_delete_insecure` only (`nvram.c:436`), so if the boot stage prefers the
   secure copy this may not take effect — verify with a boot.
3. **`Platform.sane()` / the §4 patch set** (OS booted only) — normal repair of
   `requested != actual` and a stale `max`.
4. **Re-flash region 0** from the `hd1.img` backup — the 512 MiB image is the
   ground truth; this only matters if region 0 itself was damaged, which Phase 1
   cannot do.
5. **CFP `RESETQNXOSREGION`** clears `0x2074` (`71` §7c) — same effect as (2)
   with more moving parts; do not prefer it.
6. **eMMC desolder / programmer** — only for the already-bricked 32 GB units
   (`85` §0). Not a Phase 1 outcome.

### 8.5 Constraint on writes

All NVRAM writes and all loader **write** opcodes stay in the **main session**
with the live unit and the backup set in hand. Subagents (this track included)
are read-only: no `patch`, no `delete`, no `0xC0`/`0xE0`, no RPMB write, no
reboot request. If a subagent's analysis implies a write, it goes into a runbook
and is executed — or not — by the main session.

---

## 9. Statements in `83` / `85` this reading contradicts or narrows

| # | source | statement | this reading |
|---|---|---|---|
| 1 | `83` §2.3, §10.1 | `flags` (`+0x0C`) is "preserved, never interpreted" / "interpreted by none of them" | **Contradicted.** The bootloader RMWs **bit 0** at `0x80014340-0x8001434A` (`&= ~1`) and `0x8001437C-0x80014388` (`\|= 1`), using the ARM helpers `0x80018EF8` (u32 load) / `0x80018F18` (u32 store) on `record+0x0C`. `flags` is not preserved across a boot. |
| 2 | `83` §5.1 | "the loser's counter is cleared when the winner is **not** the requested region … i.e. `boot_count_<requested> := 0`" | **Inverted.** `0x800144DA` is `bne` to the *increment*, so the clear runs when `requested == winner`, and it clears the **other** (`winner^1`) region's counter. Effect on Phase 1: none (both readings yield `boot_count_1 = 0`). Effect on rollback accounting: significant. |
| 3 | `85` §2 Phase 1 step 2 | the empty-region reboot "proves the requested/actual/fallback path" | **Overstated.** `actual` cannot change (region 1 cannot verify), so the `+0x05` write is not exercised; and ordering may be pre-empted by the `region ^= 1` branch (§5.6). Phase 1 proves requested+fallback+counters only; Phase 2 proves `actual`. |
| 4 | `85` §2 Phase 1 step 1 and §2 Phase 1 step 3 | "`Platform.switch()` then patch `max_boot_count=0`, or patch directly"; later "restore the sane state … (`Platform.sane()` or `nvram patch`)" | **Not equivalent.** `switch()` sets `max=4` (mandatory follow-up), and `sane()` does *not* zero the counters or reset `epoch`, so it is not the restore the plan's own field list describes (§4.3). |
| 5 | `85` §2 Phase 1 step 3 | "Do not leave `max=4` for a long test; the budget can expire after four fallback boots" | **Too soft.** With `requested=1` and region 1 empty, `max=4` is not a slow risk: the unit boots exactly four times and then hangs in the retry loop on the fifth (`0x800143C8-0x800143D0` → toggle → both fail → `0x80014A1E` loop). `max=0` is a hard requirement. |
| 6 | `85` §2 Phase 3 item 2 | "Original-image CRC fallback (region 0): set `+0xd4=6` … Its ECDSA signature becomes invalid, but the CRC path does not use it" (presented as a safety net) | **Unsafe as stated.** While the unit is in product mode (selector 0) the ECDSA verifier is what runs, and `+0xd4` is inside the hashed `[start, end-0x230)` range, so the "safety net" image is *unbootable* on the path that is actually executing. The `+0xd4` edit and the `BOOTCTR_FCTRY` write must be one step. |
| 7 | `85` §2 Phase 1 "Risk: if the bootloader does not fall back, the unit may hang" | "hang" is left unspecified | **Pinned.** It is the `0x80014A20` retry loop: no nuke, no write to `0x2074`, host-command dispatcher re-serviced each iteration, so the unit is probably still commandable — a materially better prognosis than "hang". |
| 8 | `85` §0 / §2 | "`0x2801` … the 0x2801 gate is NOT the test variable" | **Agreed and confirmed** at instruction level (gate 10 → `bne 0x80014A1E`); adding only that the `0x800149C2` nuke is unreachable *because* of it, so no Phase 1 outcome can nuke. |
| 9 | `83` §1, `79` §3 | `0x2074` read/write sites are `0x800142DC` / `0x80014508` only | **Confirmed**, with the addition that the driver also writes `+0x05` (`0x80014504`) and `+0x0C` bit 0, and that `+0x04` has exactly one reader (`0x8001430C`) and no writer in the image. |

No contradiction found with `76`/`79`'s `0x2801` findings, the `0x2801`/`0x2040`
model, the 72-byte struct size, or the 0/1 region numbering.

---

## 10. Residual unknowns

1. **Which branch of `0x8001434E-0x80014396` runs on this unit** — i.e. whether
   the two media probes (`0x800092DA` with the descriptor words `0x00DC0014` /
   `0x00DD0015` and index 6) return 0, which would set `flags` bit 0 and flip
   the first region attempted. Observable after the boot via `+0x0C` bit 0
   (§5.6) or via the `"Verifying region %d"` log. Testable read-only *before*
   Phase 1 if `0x205A` turns out to be involved.
2. **What `0x205A` contains** (never dumped). It is read inside the verifier
   (`0x800141F8`, `size >= 8`, bit `1 << region[+0xd4]` must be set for `r7` to
   take its alternate value) and is therefore an input to region verification
   that has never been characterised. Absent → the check is skipped.
3. **Which NVRAM copy the boot stage reads** (insecure vs secure) for `0x2074`.
   `79` residual 1 leaves this open for `0x2801`; `patch` writes both, so the
   risk is a *silent* patch, not a corrupt one. W3 (§3) is the discriminator.
4. **Exact `flags` semantics** beyond bit 0. Live 0. Bit 0 is now known to be
   bootloader-managed; bits 1..31 have no reader found.
5. **Whether the retry loop still answers the full loader protocol** (§6.1.1).
   The loop calls `0x8001217E`/`0x80012182` (host-command dispatcher) and
   `0x800094BE`; whether the USB transport is still up depends on where the
   failure happened. Unverified without a test.
6. **`0x80014510 cbz r0,0x8001451A`** — a failed `0x2074` write-back is logged
   and then ignored (boot proceeds). Whether any higher layer notices is
   unknown; treat "counter did not advance" as the only signal.
7. **The verify fast-fail point for an all-zero region** (§5.2 step 13):
   assumed to fail on the header, so the fallback costs milliseconds rather than
   a 512 MB hash. Not measured.

---

## 11. Phase 1 execution order (the runbook proper)

**Every line below is MAIN SESSION ONLY.**

```
0. backup (§7) -> $D, then copy to $D2, verify SHA256SUMS
1. precondition checks (§2.5 #1-#3): 0x2074 reads 72 bytes; libnvram symlink OK;
   no update transaction running
2. write:  ./nvram patch 0x2074 0x04 0x01
           ./nvram patch 0x2074 0x06 0x00 0x00 0x00 0x00 0x00 0x00
           E=$(date +%s); ./nvram patch 0x2074 0x10 0x$(printf %08x $((E&0xffffffff))) 0x$(printf %08x $((E>>32)))
3. verify: ./nvram partialread 0x2074 0 72 hex   == §2.1 bytes
           ./nvram partialread 0x2074 10 2 hex   == 00 00        (max == 0 -- gate for step 4)
           cat /pps/system/nvram/osregion        == RequestedBootRegion::1, MaxBootCount::0
   abort and restore (§4) if any of these differ
4. reboot (single reset, no power-cycle games)
5. after the OS is up:
   a. verify W3: 03 00 00 00 01 00 01 00 ...  -> boot_count_0 == 1, actual == 0
   b. record +0x0C bit 0 -> the fallback witness (0 = fallback proven, 1 = inconclusive)
   c. run the §4 restore (variant A) and re-verify
   d. write the result into analysis/ as 90a-phase1-result.md, including the
      three read-backs and whether the fallback witness is 0 or 1
```

If anything in step 3 is wrong, do not reboot: re-run the patches or restore
from `$D`.

---

## 12. Reproduction

```sh
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis

# record image, patch commands, post-boot prediction, restore set
python3 90-nvram2074.py                       # uses the host clock
python3 90-nvram2074.py --epoch 0x6addbef9    # the epoch used in this report

# bootloader driver: region selection, counters, one-region factory path
python3 loader-audit-2026-10-21/disarm.py ../hd0head.bin 80000000 800142a8 thumb 72
python3 loader-audit-2026-10-21/disarm.py ../hd0head.bin 80000000 800144d4 thumb 12
python3 loader-audit-2026-10-21/disarm.py ../hd0head.bin 80000000 8001453c thumb 20
```

Key instruction sites read for this report (all `hd0head.bin`, runtime base
`0x80000000`, Thumb unless noted):

| address | role |
|---|---|
| `0x80014974-0x8001499E` | `0x2040` read, tbb dispatch, `0x2801` gate, `bne` to the retry branch |
| `0x80014A1E-0x80014A38` | retry wrapper call, result dispatch (`0`/`1`/`2`) |
| `0x80014A3E-0x80014A4A` | selector re-read after a pass; factory-consumption call |
| `0x80014A6C-0x80014A8A` | failure: log, host servicing, `beq 0x80014A20` → **retry loop** |
| `0x80014A90` | enter-OS |
| `0x8001453C` / `0x80014548` | flag=1 (retry) / flag=0 (product) driver wrappers |
| `0x800142A8-0x800142CC` | driver prologue; `0x8000911A`; **mode≠0 ⇒ region 0 only**; epilogue |
| `0x800142D0-0x80014308` | `0x2074` read (max 0xC1), size check `0x18..0xC1`, zero-fill fallback, `+0x0A=0` |
| `0x8001430C-0x80014318` | `requested = rec[+4] & 1` |
| `0x8001433A-0x80014396` | `flags` bit-0 RMW; media probes; `region ^= 1` |
| `0x8001439A-0x800143D6` | log lines, `count`/`max` compare (`0x800143C8` `blo`, `0x800143CC` `cbz max`) |
| `0x800144AC-0x800144C6` | verifier call, result dispatch, "Verified region %d" |
| `0x800144D4-0x80014500` | clear rule (`bne` skips the clear) + 16-bit increment |
| `0x80014502-0x80014510` | `mov r2,r6` (same length), `actual = i`, write-back, `0x2F` on failure |
| `0x8001451E-0x8001453A` | 2-iteration loop, "Verification of regions failed", return 0 |
| `0x80014148-0x8001415E` | verifier entry; `0x8000911A` → mode in `r6` |
| `0x80014254-0x80014286` | mode dispatch: `0` → `0x80014716` ECDSA, `1` → `0x80013FB6` CRC gate + `0x80014788`, `2` → status `0x0f` |
| `0x80018EF8` / `0x80018F18` | ARM `u32 load` / `u32 store` (BLX-immediate ARM island) |

Decompiled-Python sites: `osregion.py` 54-58, 206-207, 252-263, 272-292,
288-292 (`write`), 372-374, 406-413, 446-447 (**the typo**), 453-455, 467-468,
479-480, 486-493, 515-543, 556-560; `base.py` 224-229, 267-268, 296-301,
319-325 (`switch`), 335-342 (`sane`), 344-354 (`invalidate`); `winchester.py`
30-34 (`_OFF_*`), 35 (`hd1`/`hd2`), 51-75; `nvram.c` 74-129 (`parse_data_args`),
360-401 (`patch`), 434-436 (`delete`).
