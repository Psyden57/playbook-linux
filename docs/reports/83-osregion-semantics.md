# 83 — `0x2074` is the OS-region / A-B-update state record, not a "boot counter"

Status: static analysis + live-record decode complete. **Read-only**: no device
access, no NVRAM writes, no binary modification.
Date: 2026-10-21. Supersedes the `0x2074` statements in `74`, `77` §8 and `79` §3.

Targets:

| target | identity |
|---|---|
| `deckard-2.1.0.1917/platform/osregion.py` | OS-side implementation of the record (Python 2.7, uncompyle6 output; `__version__ = '2.5.1'`, compiled 2012-01-15) |
| `deckard-2.0.0.4869/platform/osregion.py` | older OS (`2.4.0`, compiled 2011-09-06) — V1/V2/V3 structs are **identical**; only `FsRegion` vs `OsRegion` base-class placement and the `inactive` property moved |
| `playbook-live-2026-10-21/device-binaries/nvram_resource_manager` | reader/writer + PPS publisher (`0xC9D0`, `0xD680`) |
| `hd0head.bin` | bootloader region verification/selection (`0x800142A8`) |
| `playbook-live-2026-10-21/{nv,after}-0x2074.txt` | two live reads, one boot apart |

---

## 0. Verdict

1. **`0x2074` = `NV_OSSTORE_OS_REGION`** — the **OS region selector + A/B update
   state record**. It is not a plain boot counter and it is not a password
   record.
2. `deckard/platform/osregion.py:206` pins it: **`NV_OSSTORE_OS_REGION = 8308`**,
   and `8308 == 0x2074` (`0x207C = 8316 = NV_OSSTORE_EXTRA_REGION`, line 101 — the
   radio-region record, same design).
3. The live 72-byte record is **exactly `OsRegionV3.STRUCT.size` = 72** bytes
   (`'=IBBHHHIQ24s24s'`). **There is no 70-byte version of this struct anywhere**;
   the "70" is an arithmetic slip (see §2.4).
4. On-media region values are **0 and 1**, never 1/2. Region 0 = `/dev/hd1`,
   region 1 = `/dev/hd2` (`platform/winchester.py:35`). "1 or 2" is the
   **drive/partition numbering** (`hd1`/`hd2`), i.e. `region + 1`.
5. **0 is a valid region (region A / hd1)**, not "auto" and not invalid. The only
   special zeros are: `version == 0` (record unreadable by the OS), `epoch == 0`
   (unknown install time → "in grace"), `max_boot_count == 0` (**unlimited**).
6. `+0x06` (`0x017C` = 380, `0x017B` one boot earlier) **is** a per-region boot
   counter and it does increment once per boot — but it is not a factory/boot
   *tally*; it is the **boot budget counter for an uncommitted OS image**
   (`boot_count_<region>`), read by the bootloader against `max_boot_count`.

---

## 1. What the record is, and who touches it

| actor | what it does | evidence |
|---|---|---|
| bootloader (`hd0head.bin`) | reads it **once per boot**, picks the region, verifies it, increments that region's counter, writes it back | `0x800142DC` (read), `0x80014508` (write) — the only two `movw #0x2074` in the image |
| OS `deckard` library | reads it on first use, **writes it only on `switch()` / `sane()` / `invalidate()` / `inactive_ecoid=`** | `osregion.py:272-292`, `base.py:296-300,319-325,335-342,344-354` |
| `nvram_resource_manager` | no policy; republishes it to `/pps/system/nvram/osregion` after any write (`0xB7D4` → `0xC9D0`), and can *force* a rollback (`0xD680`) | `0xC9D0`, `0xD680` |
| ramloader / CFP | legacy V2 reader (`0xE9`, 0x18 bytes) and writer (`0xC0` COMPLETE); CFP's hidden `RESETQNXOSREGION` clears it | `66` §3.5, `71` §7c |

It is therefore **update state that happens to be rewritten on every boot** —
which is exactly how it acquired the "boot counter" label. The label is not
invented: `nvram_resource_manager` really does publish PPS attributes called
`RequestedBootCount` / `ActualBootCount` (`0x183D0`, `0x183E4`). They are the
per-region budget counters, not a device boot count (see §5).

---

## 2. The three struct definitions, exactly

`osregion.py` uses `struct` with the **`'='` prefix**: *native* byte order
(little-endian on ARM/QNX) and standard sizes, **no alignment padding**. The live
record decodes little-endian, confirming the native order.

### 2.1 `OsRegionV1` — `'=IBBBBQHH'`, **20 bytes (0x14)**, `VERSION = 1`
`osregion.py:295-297`

| off | size | field | notes |
|---|---|---|---|
| `0x00` | u32 | `version` | 1 |
| `0x04` | u8 | `requested_region` | 0/1 |
| `0x05` | u8 | `requested_region_boot_count` | 0..255 |
| `0x06` | u8 | `actual_region` | 0/1 |
| `0x07` | u8 | `actual_region_boot_count` | 0..255 |
| `0x08` | u64 | `epoch` | install time (unix) |
| `0x10` | u16 | `max_boot_count` | `MAX_BOOT_COUNT = 65534` (line 307) |
| `0x12` | u16 | `sw_restore` | never written by any code path here |

V1 has **no** `flags`, **no** version-id slots, and the *counts are u8 and sit
next to their region byte* — this is the layout that makes the bootloader's
`>= 0x18` size check fail (20 < 24). Cross-check: the resmgr's rollback helper
`0xD680` has a `version == 1` branch that writes `strh [sp+0x10] = 5` (max) and
`strb [sp+7] = 5` (actual count) — precisely V1's offsets — and accepts a
`0x14`-byte record.

### 2.2 `OsRegionV2` — `'=IBBHHHIQ'`, **24 bytes (0x18)**, `VERSION = 2`
`osregion.py:372-374`

| off | size | field | notes |
|---|---|---|---|
| `0x00` | u32 | `version` | 2 |
| `0x04` | u8 | `requested_region` | 0/1 |
| `0x05` | u8 | `actual_region` | 0/1 |
| `0x06` | u16 | `boot_count_0` | boots of region 0 |
| `0x08` | u16 | `boot_count_1` | boots of region 1 |
| `0x0A` | u16 | `max_boot_count` | `MAX_BOOT_COUNT = 0` (line 384), `DEF_BOOT_COUNT = 0` |
| `0x0C` | u32 | `flags` | never interpreted by any parser found here |
| `0x10` | u64 | `epoch` | install time |

### 2.3 `OsRegionV3` — `'=IBBHHHIQ24s24s'`, **72 bytes (0x48)**, `VERSION = 3`
`osregion.py:453-455`

| off | size | field | notes |
|---|---|---|---|
| `0x00` | u32 | `version` | 3 |
| `0x04` | u8 | `requested_region` | 0/1 — **the region to try first** |
| `0x05` | u8 | `actual_region` | 0/1 — **the region that verified and booted** |
| `0x06` | u16 | `boot_count_0` | boots of region 0 |
| `0x08` | u16 | `boot_count_1` | boots of region 1 |
| `0x0A` | u16 | `max_boot_count` | `MAX_BOOT_COUNT = 0` (457); `DEF_BOOT_COUNT = 4` (468) |
| `0x0C` | u32 | `flags` | preserved, never interpreted |
| `0x10` | u64 | `epoch` | install time; published as `InstallTime` |
| `0x18` | 24 s | `version_id_0` | EcoId (20 bytes used) of the OS in region 0 |
| `0x30` | 24 s | `version_id_1` | EcoId of the OS in region 1 |

`0x18 + 24 = 0x30`, `0x30 + 24 = 0x48`. No padding anywhere (`'='`).

### 2.4 "The struct is 70 but the record is 72" — there is no discrepancy

`struct.Struct('=IBBHHHIQ24s24s').size == 72` (verified with the platform's own
Python 2.7). Four independent confirmations that **0x48/72 is the real size**:

| confirmation | value |
|---|---|
| `OsRegionV3.STRUCT.size` | 72 |
| `OsRegion.read()` reads exactly `cls.STRUCT.size` at offset 8308 (`osregion.py:277`) | 72 |
| `nvram_resource_manager` `0xC9D0`: `FUN_18FF0(0x2074, buf, 0x48)`, then `cmp r0,#0x48; bls ok` | 0x48 |
| live record from the device | 72 bytes |
| resmgr `0xD680` accepts **0x14 / 0x18 / 0x48** and rejects anything else | V1 / V2 / V3 |

Where "70" comes from: it is what you get if the `flags` word at `+0x0C` is
counted as **u16** instead of u32 —
`4 + 1 + 1 + 2 + 2 + 2 + 2 + 8 + 24 + 24 = 70`. That mis-size is the natural
consequence of describing `+0x06` as a `u16[5]` array (`77` §8, `74` §3,
`79` §3): `max_boot_count` and `flags` get merged into one "counts" run, and the
4-byte `flags` is then easy to lose 2 bytes on. Nothing consumes 70: the
bootloader accepts any size in `0x18..0xC0`, the OS reads `STRUCT.size`, and the
resmgr's three accepted sizes are 0x14/0x18/0x48. The corrected reading of
`+0x06` is **not** `u16[5]`; it is three separate fields:
`u16 boot_count_0`, `u16 boot_count_1`, `u16 max_boot_count`, then
`u32 flags` at `+0x0C`.

---

## 3. Live decode of the 72-byte `0x2074` record

`playbook-live-2026-10-21/after-0x2074.txt` (`Reading block 8308` / `Received
bytes: 72`):

```
03 00 00 00   version        = 3            (OsRegionV3)
00            requested      = 0
00            actual         = 0
7c 01         boot_count_0   = 0x017C = 380 (region 0 boots)
00 00         boot_count_1   = 0
00 00         max_boot_count = 0            (unlimited)
00 00 00 00   flags          = 0
00×8          epoch          = 0            (InstallTime unknown / "in grace")
00×24         version_id_0   = ""           (region 0 has no recorded EcoId)
00×24         version_id_1   = ""
```

`nv-0x2074.txt` (same unit, one boot earlier) is identical except
`boot_count_0 = 0x017B` (379). **379 → 380 across one reboot** = exactly one
increment per boot, by the bootloader (§6).

Consequences of this specific state, all from the code:

* `active == requested == 0` → `is_requested()` true (`osregion.py:57-58`,
  `base.py:267-268`); the region was committed (`sane()` ran at some point),
  hence `max_boot_count == 0`.
* `epoch == 0` → `is_grace()` true (`osregion.py:252-253`), so the installer's
  housekeeping never purges the inactive region (`processor.py:338-343`), and
  `base.py:395` will re-run the blacklist update on every start.
* `version_id_0/1` empty → `active_ecoid` is 20 NULs (`base.py:286-288`); a
  later system-patch whose `Patch-Source-Package-Version-Id` is set will only
  log an EcoId mismatch warning (`coreos.py:750-751`).
* `boot_count_1 == 0` and `requested == actual` → the bootloader takes the
  "don't clear the other counter" path every boot, so `boot_count_0` is free to
  grow without bound (380 and climbing). That monotonic growth is why it *looks*
  like a device boot counter.

---

## 4. `requested_region` / `actual_region`, and "1 or 2?"

### 4.1 Meaning

| field | meaning in code | meaning in the bootloader |
|---|---|---|
| `requested_region` (`+0x04`) | what the *next* boot should run; set by `switch()` to `inactive`, by `sane()` to `active` | `ldrb [rec+4]; and #1` → **the region index to try first** (`0x8001430C-0x80014318`) |
| `actual_region` (`+0x05`) | which region the running OS occupies; `active` property (`osregion.py:479-480`) | **output only**: `strb r0,[rec+5]` when a region verifies (`0x80014504`) |

`inactive = active ^ 1` (`osregion.py:54-55`) — so both values are one bit, and
the two regions are complements. `is_requested()` is `active == requested`
(`osregion.py:57-58`).

**The bootloader never reads `+0x05`.** Region selection is driven purely by
`requested`; `actual` is written *by* the bootloader as a side effect. So
`actual` is not an input to any boot decision — it is a report of the last
successful choice, used by the OS to know which region it is running from.

That matters because the OS *trusts* it: `active_ifs()` / `active_fingerprint` /
`inactive_ifs()` / `inactive_rfs()` all pick their target file purely from
`active_region` (`winchester.py:51-75`, `winchester2.py:110-124`, `base.py:224-229`).
If `actual` were wrong, `inactive = actual ^ 1` is also wrong, and a staged
update would be written over the running region.

### 4.2 0/1 or 1/2? — **0/1 on media, 1/2 only as drive numbering**

Every producer and consumer uses 0/1:

* setters reject anything else: V1 `if value not in (0,1)` (`osregion.py:330-333`);
  V2/V3 `if value == 0 … elif value == 1 … else raise ValueError`
  (`406-413`, `486-493`);
* `version_id_0` vs `version_id_1`, `inactive_version_id` (`515-543`);
* disk layout: `drive = ('hd1','hd2')[region]` (`winchester.py:35`), RFS
  `hd1t179`/`hd2t179` (`winchester.py:44`), radio RFS
  `('hd7t179','hd7t178')[region]` (`winchester2.py:46`);
* PPS directories: `altpps(region=0)`, `altpps(region=1)`, `stgpps(region=0/1)`
  are created at setup (`base.py:637-644`) — on the device the paths are
  `/pps/system/installer/…/0` and `…/1`;
* the resmgr publishes the byte **raw**, no `+1`: `ldrb [rec+4]`,
  `ldrb [rec+5]` formatted with `%s:n:%i` (`0xC9D0`, `0x183A8`, `0x183BC`);
* the live record holds `0x00 0x00`.

**Where the off-by-one comes from:** the two OS regions live on `/dev/hd1` and
`/dev/hd2` — QNX drive names are 1-based, while the NVRAM field and every line of
code are 0-based. "Region 1" (human, = hd1) is `0x00` in the record; "region 2"
(= hd2) is `0x01`. Any doc, CFP screen or partition map that says "1 or 2" is
speaking drive/partition numbers. There is no code path, PPS attribute or log
line in the analysed set that adds 1 to the stored value.

**Hazard worth recording:** because the bootloader masks with `&1`
(`0x80014314`), a record written with the 1/2 convention would be *read
inverted*: stored `1` boots region 1 (hd2), stored `2` masks to `0` and boots
region 0 (hd1). If anyone ever "fixes" a tool to write 1/2, the two regions swap.

### 4.3 What 0 means

* **0 is a valid region** — region A / `/dev/hd1` — not "auto", not "unset",
  not "invalid". Nothing in the OS or the bootloader treats 0 specially:
  the bootloader's only transformation is `& 1`; the OS indexes
  `version_id_0`, `boot_count_0` and `hd1` with it.
* The values that *do* carry "unset" semantics are different fields:
  `version == 0` → `OsRegionV2/V3.__init__` raises, `read()` raises
  `OsRegionVersionError`, and `OsRegion.load()` ends in
  `OsRegionError('cannot read NV_OSSTORE_OS_REGION')` (`osregion.py:255-263`);
  the bootloader, by contrast, *accepts* a zeroed record (it only checks the
  size) and substitutes a zeroed 0x18-byte struct when the size is wrong
  (`0x800142EC-0x80014308`), which then means "region 0, unlimited, counters 0".
  `epoch == 0` → unknown install time ("in grace"). `max_boot_count == 0` →
  unlimited (§5).
* "Invalid" applies only to values > 1: the setters raise `ValueError`; the
  read path does **not** validate (`read()` only checks the version), so a
  hand-written `2` loads as `active = 2`, `inactive = 3` in the OS and behaves
  as region 0 in the bootloader.

### 4.4 What sets `requested != actual`, and what clears it

Set by `switch()` — `Platform.switch()` (`base.py:319-325`) →
`OsRegionV3.switch()` (`osregion.py:559-560`):

```
requested_region = inactive (= active ^ 1)
boot_count_0 = boot_count_1 = 0
max_boot_count = DEF_BOOT_COUNT = 4      # budget for the new image
epoch = now                              # InstallTime
```

Cleared by `sane()` — `Platform.sane()` (`base.py:335-342`) →
`OsRegionV3.sane()` (`osregion.py:556-557`):

```
requested_region = actual_region
max_boot_count  = MAX_BOOT_COUNT = 0     # unlimited: the image is committed
```

Nothing else converges the two: the bootloader sets `actual := verified region`
but never touches `requested`. So after a failed switch the record stays
`requested != actual` until either the new region finally verifies (then
`actual == requested` again) or the OS calls `sane()` on the old region, which is
the rollback commit.

Consequence in the OS: `Activator.run()` (`activator.py:136-143`) checks
`is_requested_region()`; when false it runs `__activate(force=True)`, i.e. it
re-activates the *old* region's staged application set, and
`Activator.setup()`/`reconcile()` keep the catalogues consistent. The V2
implementation of `switch()` is broken — `_replace(reqested_region=…)`
(`osregion.py:446-447`, a typo, present in 2.0.0 and 2.1.0) raises
`ValueError: Got unexpected field names` — so on a V2 record only `sane()` and a
direct field write can move `requested`. V3 is the only working switch path.

---

## 5. `boot_count_0` / `boot_count_1` / `max_boot_count`

### 5.1 What increments them

Exactly one writer increments: the bootloader, after a region **verifies**:

```
0x800144EE  r1 = rec + 2*region
0x800144F2  r2 = u16[rec+0x06+2*region] + 1
0x800144FA  strh r2 -> rec+0x06+2*region          ; increment
0x80014504  strb region -> rec+0x05               ; actual := this region
0x80014506  write 0x2074 back, same length as read
```

Two rules complete the semantics:

1. **Increment only on success.** A region that fails verification never
   increments its counter.
2. **The loser's counter is cleared when the winner is not the requested
   region** (`0x800144D4-0x800144EC`): `if rec[+4] != current_region` then
   `u16[rec+0x06+2*(current^1)] = 0`, i.e. `boot_count_<requested> := 0`. In
   steady state (`requested == actual`) this never fires, which is why the live
   counter just climbs (380).

### 5.2 What `max_boot_count` means, and `max == 0`

```
0x800143B2  r1 = u16[rec+0x06+2*region]     ; count
0x800143C4  r2 = u16[rec+0x0A]              ; max
0x800143C8  cmp r1,r2 ; blo -> verify       ; count < max        -> try it
0x800143CC  cbz r2   -> verify              ; max == 0           -> try it
            else log "Exceeded boot count (%d >= %d)" -> other region
```

So **`max_boot_count == 0` means "unlimited"** (the counter is compared and then
ignored), which is the steady state of a committed OS: `sane()` sets it to 0.
Anything non-zero is a **budget of successful boots granted to the requested
image**: `switch()` sets 4 (V3 `DEF_BOOT_COUNT`), so a freshly installed OS may
boot at most 4 times to prove itself; the 5th attempt logs "Exceeded boot count"
and the bootloader takes the other region. V1 used `65534` and kept the counts
as u8 next to their region bytes; V2 defaulted both max and DEF to 0 (i.e. the
V2 library never imposed a budget).

The counters are 16-bit and wrap silently; with `max == 0` a wrap is harmless.

### 5.3 Relation to the bootloader struct and to rollback

* The bootloader struct is a **prefix-compatible subset** of V2/V3: it reads
  `+0x04`, `+0x05`, `+0x06+2i`, `+0x0A` and nothing else — no version check, no
  `flags`, no `epoch`, no version ids. Accepted size `0x18..0xC0`
  (`size-0x18 < 0xA9`), so V2 (0x18) and V3 (0x48) both work; **V1 (0x14) is
  rejected** and replaced by a zeroed 0x18-byte buffer — a V1 record silently
  means "region 0, unlimited".
* It writes back **the same byte count it read** (`mov r2,r6`, `0x80014502`), so
  the record's length is preserved and the 72-byte V3 tail (`flags`, `epoch`,
  version ids) is never truncated by the bootloader. This is another reason the
  on-media record is exactly `STRUCT.size`.
* Rollback logic is split between three layers:
  1. **Bootloader**: budget exhausted → try the other region → set `actual` to
     it, clear the failed image's counter, leave `requested` alone.
  2. **OS installer**: `is_requested_region() == False` → force-activate the old
     region's apps (`activator.py:136-143`), and `purge()`
     (`processor.py:375-385` → `base.py:344-354` `invalidate()` +
     `invalidate_inactive()` + 16 KB zeroing of the inactive IFS/RFS) once the
     2-day grace window has passed (`GRACE_PERIOD = 172800`, `osregion.py:207`,
     `is_grace` at `252-253`).
  3. **`nvram_resource_manager` `0xD680`**: "burn the budget deliberately" —
     `max_boot_count := 5` **and** `u16[+0x06+2*actual] := 5` (V2/V3;
     `u16[+0x10] := 5` and `u8[+7] := 5` for V1), then write. Since
     `count >= max`, the next boot logs "Exceeded boot count (5 >= 5)" and takes
     the other region. That is the OS-side "force a rollback on next reset"
     primitive, used on the NVRAM-signature-failure path (`77` §7.3).
* The ramloader speaks only V2: `0xE9` reads `0x2074` into a **0x18**-byte
  buffer and `0xC0` COMPLETE writes a small V2-shaped prefix
  (`{u32 2, …}`) (`66` §3.5). If that ever ran against a V3 record, `read()`
  would see version 2, `OsRegionV3.read` would raise `OsRegionVersionError`, and
  `OsRegion.load()` would fall through to `OsRegionV2.read` (24 bytes) — the V3
  tail (version ids, epoch) would be abandoned. `OsRegionV3.convert()` only
  upgrades V2→V3 when the bootrom build date is ≥ 2011-01-01
  (`osregion.py:552-553`), otherwise `upgrade()` keeps V2 (`265-270`).

---

## 6. `hd0head.bin` cross-check (task 5)

Scan of the whole code window for `movw #imm` on the IDs of interest found
exactly these references:

| ID | sites |
|---|---|
| `0x2074` | **`0x800142DC` (read), `0x80014508` (write)** — nothing else |
| `0x2801` | `0x80014D8E`, `0x80014E3A`, `0x80014FAC`, `0x8001587A` |
| `0x2004` | `0x80014A06` (read only) |
| `0x205A` | `0x800141F8` |
| `0x2040` | `mov.w r7,#0x2040` @ `0x80014974` (read `0x80014980`, write `0x800149B4`) |

So **yes**: the bootloader uses `0x2074` for region selection, and that is its
*only* use of the record. `0x800142A8` (Thumb) is the whole policy:

```
0x800142DC  read 0x2074 -> sp+0x28, max 0xC1
0x800142E6  size-0x18 ; cmp #0xA9 ; blo ok
            else log "Invalid NvOSRegionStruct (size %d, expected %d)";
                 zero 0x18 bytes; r6 = 0x18
0x8001430C  ldrb [rec+4] ; and #1 ; -> global region index
0x8001439A  log "Requested region %d"
0x800143A8  log "Verifying region %d"      <- loop, max 2 iterations
0x800143B2  count = u16[rec+0x06+2*i] ; max = u16[rec+0x0A]
0x800143C8  count < max  -> 0x800144AC      (verify)
0x800143CC  max == 0     -> 0x800144AC      (verify, unlimited)
            else log "Exceeded boot count (%d >= %d)" -> toggle
0x800144AC  verify(i) via 0x80014148
0x800144C2  result 0  -> toggle
0x800144C4  result 1  -> log "Verified region %d successfully"
0x800144D4  if rec[+4] != i: u16[rec+0x06+2*(i^1)] = 0    ; clear loser
0x800144EE  u16[rec+0x06+2*i] += 1                        ; count this boot
0x80014504  rec[+5] = i                                   ; actual := winner
0x80014508  write 0x2074 back (same length)
0x8001451E  toggle i ^= 1, second iteration
0x80014532  log "Verification of regions failed", return 0 -> nuke path
```

Answers to the sub-questions:

* **What it reads at `+0x04`/`+0x05`:** `+0x04` = requested, masked with `1`, used
  as the first region index and *only* as that; `+0x05` is **written, never
  read**.
* **Does it treat 0 specially?** No. 0 is simply the first region (and `&1`
  makes 2 → 0). The only zero with special meaning on this path is
  `max_boot_count == 0` (unlimited).
* **The boot gate at `0x80014998` is not this record.** `bl 0x80014F84` →
  `0x80014F28`/`0x80014F84` → `0x80014FA6`, which does
  `movw r0,#0x2801; movs r2,#0x70; bl 0x8000A9D8` — the **112-byte password
  struct**, computing remaining attempts as `clamp(max,3,10) - failures`
  (`0x80014FC6` clamps `+0x1C` to 3..10). It is reached only for boot-mode
  values `0` and `2` (tbb table at `0x8001498E` = `{0:0x80014998, 1:nuke,
  2:0x80014998, 3..8:0x800149A4}`), and it is completely independent of
  `0x2074`. `0x2074` is consulted later, on the product path, through
  `0x80014548` (public-key verifier wrapper) at `0x800149CC`.
* **Factory path skips the record entirely:** if `FUN_8000911A()` (the RPMB
  `BOOTCTR_FCTRY` probe) is non-zero, `0x800142BC-0x800142C6` sets the region
  index to 0 and verifies region 0 directly — `0x2074` is not read at all.

---

## 7. Installer switch / activate flow (end to end)

1. **Stage** into the inactive region. `CoreOS.do_stage` (`coreos.py:817-863`)
   writes the QCFM payloads through `platform.zone()`/`mct_zone()`, which force
   the **inactive** region (`winchester.py:121-135`, `winchester2.py:110-133`),
   and records the new image's EcoId:
   `self._pfm.inactive_ecoid = self.ecoid(mf['Package-Version-Id'])`
   (`coreos.py:863` → `base.py:294-301` → `OsRegionV3.inactive_version_id`,
   `osregion.py:536-543`, then `write()`); `ecoid()` is a urlsafe-b64 decode
   (`package.py:192-197`) into the 24-byte slot.
2. **Switch** — PPS txn `switch` / `switchzone` (`transaction.py:88-89`,
   `423-442`) → `Platform.switch()` (`base.py:319-325`) →
   `OsRegionV3.switch()` (`osregion.py:559-560`), then `write()`
   (`osregion.py:288-292`, `pwrite` at offset 8308) and a reload.
3. **Reboot** is requested by the client (`transaction.py:444-449`); the txn
   layer never reboots by itself.
4. **Boot**: the bootloader tries `requested` first, verifies it (SHA-512 +
   ECDSA on the product path, `71` §2), increments `boot_count_<requested>`,
   sets `actual`, writes back. If it fails, it tries `requested ^ 1` — the old
   OS — and *that* boot clears `boot_count_<requested>`, so the failed image
   keeps a full budget and is retried on the next reset.
5. **Commit / rollback on the OS side**:
   * running the new image: `is_requested_region()` is true →
     `Activator.run()` activates the staged apps in `active_region`
     (`activator.py:41,136-139`) → then `Platform.sane()`
     (`base.py:335-342`) sets `requested := actual`, `max := 0`.
   * running the old image: `is_requested_region()` false →
     `__activate(force=True)` (`activator.py:141`) re-asserts the old apps;
     `housekeeping()` (`processor.py:338-343`) purges the failed region once the
     grace window after `epoch` has expired.
6. `sane()`/`invalidate()` are `Platform` methods with no caller in this
   decompiled module set (the txn action table has no `sane`,
   `transaction.py:60-119`) — the caller is the update service that drives
   `deckard`. Their *effect* is unambiguous from the code and is what terminates
   the retry loop.

---

## 8. Corrections to earlier reports

| earlier claim | verdict |
|---|---|
| `0x2074` is "the boot counter" (`74` §3, `77` §3/§8, `79` §3) | **Wrong as a name.** It is the OS-region/A-B update state record. `+0x06` *is* a boot counter, but a per-region budget counter, not a device tally. |
| `+0x06` is `u16[5]` "requested/actual count, `[2]` = MaxBootCount" (`77` §8, `74` §3, `79` §3) | **Wrong.** `+0x06` u16 = `boot_count_0`, `+0x08` u16 = `boot_count_1`, `+0x0A` u16 = `max_boot_count`, `+0x0C` **u32** `flags`. The `u16[5]` reading is what produces the spurious 70-byte struct. |
| `+0x10` u64 = `InstallTime` (`77` §8) | Correct (it is `epoch`; `switch()` sets it, `is_grace()` consumes it). |
| bootloader `+0x04` = "boot mode / requested region, bit 0" (`70` §3) | Correct, and now proven by `ldrb/and #1` at `0x8001430C-0x80014314`. |
| bootloader `+0x0A` "u16 max boot count (`0` = unlimited)" (`70` §3) | Correct (`0x800143C8-0x800143D6`). |
| "`0xD680` forces the count slots to 5" (`77` §8) | Refined: `max := 5` **and** `count[actual] := 5`, so the *next* boot is forced to the other region. |
| region values are 1/2 | **0/1.** 1/2 is `hd1`/`hd2` drive numbering (`winchester.py:35`). |
| struct size 70 | **72** (`0x48`); see §2.4. |

---

## 9. Explicit answers

**Is `0x2074` a boot counter, a region/partition selector, or an OS-update state
record?**
An **OS-update state record that contains the region selector**. Correct name:
`NV_OSSTORE_OS_REGION` — "OS region selector + A/B update state (requested and
actual region, per-region boot budget, install epoch, per-region OS version id)".
It carries a boot counter; it is not one.

**What do `requested_region` and `actual_region` mean?**
`requested` = the region the next boot should run (the installer's wish; the
bootloader's first choice). `actual` = the region that last verified and booted
(the OS's belief about where it is running from). `inactive = actual ^ 1`.

**1 or 2?** **0 or 1.** Off-by-one source: `/dev/hd1` and `/dev/hd2` are 1-based
names for the 0-based region index. Do not write 1/2 — the bootloader masks with
`&1`, so a "2" would boot region 0.

**What does 0 mean?** Region A — the first region, `/dev/hd1`. A valid value, and
the default (`__init__` starts at 0/0). Not "auto", not invalid. Unset flavours
live in other fields: `version 0` (OS refuses the record), `epoch 0` (unknown
install time), `max_boot_count 0` (unlimited).

**On boot with `actual = 0` vs `1`?** Nothing differs. The boot path uses
`requested` only; `actual` is overwritten with whichever region verified. A
wrong `actual` does not change the boot decision — but it inverts what the OS
thinks the *inactive* region is, which is how a bad `actual` could make the
installer write into the running region.

**What sets `requested != actual`, and what clears it?**
`switch()` sets `requested := inactive` (plus counters 0, `max := 4`,
`epoch := now`). `sane()` clears it with `requested := actual` and `max := 0`.
The bootloader only ever moves `actual`.

**What increments the counters; `max = 0`?**
Only the bootloader, once per **successful** verification of a region, and it
zeroes the *other* region's counter when it boots a region that is not the
requested one. `max_boot_count = 0` = **unlimited** (no budget); V3's
`DEF_BOOT_COUNT = 4` is the budget a freshly switched image gets.

---

## 10. Residual unknowns

1. **`flags` (`+0x0C`, u32)** is preserved by every writer found and interpreted
   by none of them (OS, resmgr, bootloader, loader). Live value 0. Its bits are
   unnamed.
2. **`sane()`'s caller** is outside the decompiled module set (see §7.6). The
   effect is certain; the trigger point (boot-time service vs. CFP "OS accepted")
   is not.
3. **`sw_restore` (V1 `+0x12`)** has no writer/reader in any analysed file.
4. The ramloader's `0xC0` COMPLETE write pattern (`66` §3.5: `{u32 2, …}`) was
   not re-derived here; if it writes a version-2 prefix into a 72-byte record it
   downgrades the record to V2 semantics, but the exact byte it lands was not
   verified against the loader binary in this pass.
5. Whether `0x2074` is in the resmgr's "secure records" SHA-1/RSA set (`0x281B`/
   `0x2812`, `77` §7.3) is still unresolved — the list is relocation-time data.

---

## 11. Reproduce

```bash
# struct sizes and the live decode (Python 2.7, as shipped)
py27/bin/python2.7 - <<'EOF'
import struct
for f in ('=IBBBBQHH','=IBBHHHIQ','=IBBHHHIQ24s24s'):
    print f, struct.Struct(f).size          # 20 24 72
b = open('playbook-live-2026-10-21/after-0x2074.txt').read().splitlines()[2]
rec = ''.join(chr(int(x,16)) for x in b.split())
print len(rec), struct.unpack('=IBBHHHIQ24s24s', rec)
EOF

# bootloader: every movw of the record IDs
python3 - <<'EOF'
import capstone
d=open('hd0head.bin','rb').read()
md=capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
for a in range(0x80008000,0x8001D044,2):
    c=d[a-0x80000000:a-0x80000000+4]
    if len(c)<4 or (c[1]&0xF8)!=0xF0: continue
    i=list(md.disasm(c,a,1))
    if i and i[0].mnemonic=='movw': print(hex(a), i[0].op_str)
EOF

# resmgr: PPS republish + rollback clamp
python3 nvdis.py 0xC9D0 0xCC00 arm     # 0x48-byte read, attribute map
python3 nvdis.py 0xD680 0xD780 arm     # max=5 / count[actual]=5, sizes 0x14/0x18/0x48
```

Key decompiled line numbers (2.1.0.1917): `osregion.py` 54-55, 57-58, 101, 200,
206-207, 252-253, 255-263, 272-292, 295-297, 307-308, 330-333, 361-366, 372-374,
384-385, 406-413, 443-447, 453-455, 467-468, 486-493, 515-543, 548-554,
556-563; `base.py` 224-229, 243-245, 256-259, 267-268, 280-281, 286-301,
319-325, 335-342, 344-354, 356-390, 392-401; `winchester.py` 34-49, 51-75;
`winchester2.py` 25-49, 110-133; `processor.py` 139-157, 338-343, 375-385;
`activator.py` 39-49, 136-143; `transaction.py` 60-119, 423-442;
`coreos.py` 750-751, 794-811, 863; `package.py` 192-197.
