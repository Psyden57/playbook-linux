# 155 — Survivor read-only sweep: the reference is unarmed and stock, exactly as recorded

**Session 21 (absolute), Linux-era session 15.  2026-10-31.**
Status: **read-only.  No flash, no RPMB write, no media write, no NVRAM write.**
Every item in `BOOT-CHAIN.md` §5 steps 1–4 was run on the surviving unit and
every value matches the handoff.  The unit is the **reference** (`0x50175127`),
**unarmed** (`BOOTCTR_FCTRY` absent) and **stock** (`os_type` 4, CRC
`0x8D46FB4A`, IFS `75be24e8…`).

Bench: TabletOS up (`QNX localhost 6.6.0 2011/11/11-16:00:16EST
OMAP4430_Winchester_ES2.2_HS_PVT4_Rev:07 armle`), RNDIS door up on
`enp8s0f3u1` (`169.254.0.2/16`), the unit enumerating as `0fca:8010`.

---

## 1. Identity

```
tools/pbssh.sh 'uname -a; id'
QNX localhost 6.6.0 2011/11/11-16:00:16EST OMAP4430_Winchester_ES2.2_HS_PVT4_Rev:07 armle
uid=0(root) gid=0(nto) groups=0(nto),1(bin),3(sys),4(adm),5(tty)
```

NVRAM record `0x2002` (`NV_OSSTORE_PIN_NUM`), read with the 2.x `nvram` tool:

```
27 51 17 50      -> 0x50175127
```

**This is the reference unit.**  Per `SAFETY.md` §6 it is read-only, always.

## 2. `BOOTCTR_FCTRY` — absent

```
ls -l /dev/rpmb/BOOTCTR_FCTRY
ls: No such file or directory (/dev/rpmb/BOOTCTR_FCTRY)
```

The store is otherwise fully populated and mounted (the `/dev/rpmb` directory
holds 115 entries: `BSIS_AUTHKEY`, `BSIS_PRIV`, `BSIS_PUBL`, `FS_DIRTY`, the
`bdevcert`/`devcert`/`bgroupcrt`/`ndrcrttmp`/`ndrgpriv` records, …), so this is
**the unarmed product-path state**, not a store failure.  `FUN_8000911A` takes
selector **0** (product/ECDSA); no record was read and none was written.

## 3. Region 0 / the IFS

Commands (offsets computed with Python first, per the project rule):

```sh
# exact 0x2BFE10 bytes, IFS at region-0 offset 0x100000
tools/pbssh.sh 'dd if=/dev/hd1 bs=16 skip=65536 count=180193 2>/dev/null' | sha256sum
# container descriptor / os_type / CRC
tools/pbssh.sh 'dd if=/dev/hd1 bs=512 skip=2048 count=1 2>/dev/null'      # IFS+0x000..0x200
tools/pbssh.sh 'dd if=/dev/hd1 bs=4 skip=982914 count=2 2>/dev/null'       # IFS end-8..end
```

Measured (all **[P]**, this session):

| field | offset | bytes / value |
|---|---|---|
| IFS sha256 (whole `0x2BFE10` bytes) | media `0x100000` | **`75be24e8297e3a4a27b8f7ce2f9d857b94178b020c063d73ecb0cc90c0fe94ba`** |
| full length | — | `0x2BFE10` = 2 883 088 bytes (`bs=16 skip=65536 count=180193`, no remainder) |
| IFS first word | `+0x000` | `fe 01 00 ea` (ARM branch) |
| model ID | `+0x0A8` | `06 1a 00 06` = **`0x06001A06`** |
| container magic | `+0x0C8` | `93 e1 d0 c3` = **`0xC3D0E193`** |
| `os_type` | `+0x100` (media `0x100100`) | **`04 00 00 00` = 4** |
| end-8 magic | `+0x2BFE08` (media `0x3BFE08`) | **`0xD7C82D1F`** |
| CRC-32/ISO-HDLC word | `+0x2BFE0C` (media `0x3BFE0C`) | **`4a fb 46 8d` = `0x8D46FB4A`** |

**The stock values are exact.**  `os_type` 4 (not 6) and the product-path
selector confirm the unit is on the ECDSA path and could not boot a CRC-sealed
image as it stands.

### 3.1 Offset clarification (not a new fact, but a trap)

`BOOT-CHAIN.md` §0 labels the diagram's `+0x000100` as `os_type` inside **region
0**.  Measured, region-0 offset `0x100` is **`0x61`** — the `'a'` in the MBR's
`"\r\nMissing OS Image"` string (`0x0F0..0x115`) — and the vendor
`PB-4869-root.0.0.mbr` matches it byte-for-byte.  `os_type` is at **IFS image
`+0x100` = media `0x100100`**.  *(Correction: an earlier draft of this report
said `0x03`; that read used `bs=512 skip=1` (offset `0x200`) and so sampled
region-0 `0x300`.  The corrected `skip=0` read is `0x61`.)*  The descriptor's
base is `IFS+0x2C` (`desc+0x9C = IFS+0xC8` magic, `desc+0xD4 = IFS+0x100`
os_type, model at `desc+0x7C = IFS+0xA8`), which is why `SAFETY.md` §3's
"image offset `0x100`, media `0x100100`" is right and the diagram label is the
shorthand.  Any future check must read `0x100100`, not region-0 `0x100`.

## 4. NVRAM gates

Read-only, with the 2.x `nvram` tool against the stock `/lib/libnvram.so.1`:

| record | result |
|---|---|
| `0x2004` | **absent** (0 bytes) |
| `0x2040` | **absent** (0 bytes) |
| `0x2074` | 72 bytes: version 3, requested/actual region 0, `boot_count_0 = 0x017E` (382), `max_boot_count = 0` |
| `0x2801` | 112 bytes: `+0x18` **A = 0**, `+0x1C` **B = 10**; iteration count `0x13C4` (5060), salt `b5 43 a0 b9 b5 86 2d 22`; the 64-byte verifier matches the documented `0000` blob |
| `0x2802` | 112 bytes, zeroed except `b0 30 40 00` near `+0x53` (the documented reserve) |

The `0x2801` gate is `clamp(B,3,10) − A = 10 − 0 = 10`, so the unit takes the
**retry branch** (`0x80014A1E`) on every boot, as before — a failed verify
**halts** rather than nukes.

### 4.1 `0x2040` absent is the safe state — correct the wording

`HANDOFF.md` and `ARMING-TUTORIAL.md` §8a say media `0x2040` "must read `1..8`".
The live read is **absent**, and that is the expected stock state: boot main
pre-initialises the `0x2040` buffer to **`2`** before the read, so an absent
record takes the `0/2` gate value, not the nuke path (`SAFETY.md` §3, report 76
§3).  The precondition should be read as:

> **`0x2040` absent (substituted with `2`) or `1..8`; only a *present* `0` or
> `≥ 9` routes to the nuke.**

Report 154 §Q5 item 5's "must be `1..8`" is a static simplification that omits
the pre-initialisation; the live measurement settles it.  This is a wording
correction, not a contradiction in the boot model.

## 5. Tooling notes (why the first attempt failed)

The `nvram` binary deployed at `/accounts/devuser/nvram`
(sha256 `160c51db…df164`, matching `local/vendor/nvram-editor/nvram`) **only
hardcodes `/proc/boot/libnvram.so.1`**, although
`local/vendor/nvram-editor/nvram.c` lists four paths.  `LD_DEBUG=libs` shows a
single `dlopen("/proc/boot/libnvram.so.1",1)` attempt.  On this restored
TabletOS `/proc/boot` is the read-only boot-image filesystem (`cp`/`ln` both fail
with ENOENT), so that binary cannot be used as-is.  (A patched copy run from
`/tmp` against the bundled BB10 library also works, but it is not needed.)

**Use the 2.x binary** (`local/vendor/nvram-2.x/nvram`, sha256 `7b52689b…`),
which loads the device's own `/lib/libnvram.so.1`:

```sh
tools/pbssh.sh 'cat > /accounts/devuser/nvram2' < local/vendor/nvram-2.x/nvram
tools/pbssh.sh 'cat > /accounts/devuser/libgcc_s.so.1' \
    < local/vendor/nvram-2.x/libgcc_s.so.1
tools/pbssh.sh 'chmod +x /accounts/devuser/nvram2; cd /accounts/devuser; \
    LD_LIBRARY_PATH=/accounts/devuser ./nvram2 read 0x2801 hex'
```

It prints `Loaded libnvram from /lib/libnvram.so.1` and then the record, exactly
as the 2026-10-25 capture.  `libgcc_s.so.1` is required; both files live in the
writable user partition (`/accounts/devuser`), no NVRAM/media write.

## 5b. Vendor-corpus cross-check (operator-supplied, read-only)

The operator pointed at `/home/psyden/Pdrive/bb10mt/` (the `bb10mt`
extraction).  Two artefacts match the live unit **byte for byte**:

| vendor file | compared against | result |
|---|---|---|
| `PB-4869-root.0.3.ifs` (2 883 584 B) | live IFS, first `0x2BFE10` bytes | **sha256 `75be24e8…` — identical**; the remaining `0x1F0` (496) bytes of the file are **zero** (exactly the 496-byte over-read trap in `SAFETY.md` §4) |
| `PB-4869-root.0.0.mbr` / `.0.2.mbr` (64 KiB each, identical) | live region-0 `0x000000..0x010000` | **sha256 `429beab3…` — identical** |

Re-checked against a fresh live read: **zero differing bytes** over the full
64 KiB, and region-0 `0x100` is `0x61`, the `'a'` in `"\r\nMissing OS Image"`
(`0x0F0..0x115`).  The `.0`/`.2`/`.3` numbers are `bb10mt` extraction indices,
not versions.

The region-0 MBR (live and vendor) is a real partition table:

```
disk signature @0x1B8 = cd 42 b4 00 ; magic @0x1FE = 55 aa
part0: type 0xB3 (QNX6), LBA 0x6000, 0xFA000 sectors
       -> region-0 offset 0xC00000, length 0x1F400000 (500 MiB)
```

That is exactly the RCFS start and the region-0 size used everywhere
(`0xC00000`, `0x20000000`); the IFS at `0x100000` is outside any partition, raw
in the 12 MiB pre-partition area.  The live `0x10000..0x100000` gap is zeros.

The boot TOC is in the **user-area first 1 MiB** (`emmc-first1mb.bin`,
`dd if=/dev/hd0` per `docs/history/docs/03_DEBUGGING_SESSIONS.md`), not in region
0.  Its 32-byte records decode as `{u32 offset, u32 length, …, char name[14] at
+0x14}`:

```
0: off=0x004000 len=0x004d70 name="MLO"       -> 0x4000..0x8D70 (BOOT-CHAIN §1)
1: off=0x000c00 len=0x0032b8 name="PRIMAPP"   -> 0xC00..0x3EB8
2: off=0x000200 len=0x000960 name="KEYS"      -> 0x200..0xB60
3: 0xFFFFFFFF sentinel
```

This confirms the MLO location and the TOC layout; region-0 `0x10000`+ is zero,
so the boot image is not duplicated in region 0.  The `arm9000` stage
(`0x9000..0x1E000`) is **not a TOC entry**; it sits in the same boot-partition
image immediately after the MLO payload.

**The `.rcfs` in that folder is corrupt/not working (operator, this session);
it was not used.**  The live RCFS head at region-0 `0xC00000` is
`e1 12 e3 17…`, unrelated to the vendor file's leading `eb 10 90…`.

This is a strong offline/live anchor: the stock IFS and region-0 MBR can be
analysed from the vendor corpus, and their bytes are exactly what the unit
holds.

## 6. Store geometry — still `[U]`

`/dev` exposes `hd0`–`hd6` and no raw RPMB block device; `/dev/rpmb` is the
resmgr's named-record directory only.  A whole-store raw block read is therefore
still not available from QNX userland (report 154 §9 item 8).  This does not
block the read-only protocol above, but the "every block `0..nblocks−1` reads"
check cannot be done from here yet.

## 7. What this means

* The **read-only precondition sweep passes**: identity is the reference, the
  record is absent, the IFS is the stock `75be24e8…` image with `os_type` 4 and
  CRC `0x8D46FB4A`, and the NVRAM gates are `0x2040` absent / `A=0,B=10`.
* **Nothing here authorises arming.**  The seal is still unreproduced
  (reports 148, 149).
* **The ordering constraint:** the pair *recapture* is read-only and can be
  done now on TabletOS (`pbmem` → `memdump3` of the whole `rpmb-omap4` page and
  the frame it names).  The `0x2E`/`0x2F` *validation* cannot: it needs
  `pbmon`, i.e. our CRC-sealed Linux image, which cannot boot on this unit until
  it is armed (the product path ECDSA-verifies region 0 and a CRC-sealed image
  fails).  So the safe order is: recapture the pair **now**; precondition sweep
  **done**; then the arming decision; only after arming does the monitor-call
  work become possible.
* **No RPMB, NVRAM or media writes were performed in this sweep.**
