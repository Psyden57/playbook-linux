# 139 — The region-1 write landed in region 0: the V1 `qcfp` record, and the recovery

**Session 18 (absolute), Linux-era session 12.  2026-10-30.**
Status: **incident contained and recovered.**  The unit is back on TabletOS with a
verified sealed IFS and a verified RCFS head; `BOOTCTR_FCTRY = 0x32`.  The
corrected flasher is in the tree and dry-ran clean; the region-1 write has **not**
been retried yet.

This report exists because the first attempt to use the new bulk path went to the
**wrong region**.  The mechanism is a durable, non-obvious property of the signed
loader, and the recovery is reproducible from components already on the host.

---

## 1. Summary

| question | answer |
|---|---|
| What was being written? | the first Linux root: a 32 MiB ext4 image (`local/images/rootfs-region1.ext4`), built by `tools/build_rootfs.py` |
| Where was it supposed to go? | media region 1 (`/dev/hd2`, raw `0x28000000`) at region-1 offset 16 MiB: raw **`0x29000000`**, loader block **`0x2915`** |
| Where did it go? | **region 0, loader block `0x825`** (raw `0x8100000`) — the sealed IFS |
| How much? | 512 loader blocks (32 MiB): region-0 relative `0x100000..0x20FFFFF` |
| What was destroyed? | the sealed IFS (`0x100000..0x3BFFFF` slot, restored), unused IFS-zone padding, and the **first 21 MiB of the RCFS** at region-0 `0xC00000..0x20FFFFF` |
| How was it fixed? | sealed IFS restored at block `0x825`; RCFS head (first 32 MiB) restored from the vendor `PB-4869-root.0.1.rcfs` at block `0x8D5` |
| Is it verified? | **yes** — IFS sha256 `c72b0530…`, `os_type` 6, CRC `0x116B2562`; RCFS first 32 MiB sha256 `1e04336b…` == the vendor component; MBR zone intact; region 1 still holds its old test blob |
| Root cause | the loader applies the **V1 `qcfp`** run record, whose offset is **region-relative**; the flasher had patched only the V2 `rrcq` |
| Correct container | **`bb10mt pack -v 1,2`** (V1 `qcfp` + V2 `pfcq`/`rrcq`); `-v 2` alone is rejected `0x11` |

---

## 2. Timeline

1. `tools/pre_os_ifs_flash.py --payload local/images/rootfs-region1.ext4
   --dest-raw 0x29000000` was started, the unit rebooted, and the bootrom
   appeared.  The flasher's first `0xF7` frame was **rejected with status
   `0x11`**: the container had been built with `bb10mt pack -v 2`, which emits a
   V1 `mfcq` header with **no `qcfp` block**.
2. The loader was left running with a stale session; `--reuse-loader` then failed
   `set_mode(2)`.  `tools/force_reset_loader.py` reset it (back to the bootrom).
3. The container was rebuilt by cloning the **vendor signed IFS template**
   (`local/images/ifs_template.signed`), replacing the payload, and patching the
   **V2 `rrcq`** offset/count — the same code path the proven `--template-ifs`
   mode uses for region 0.
4. The write ran: 2055 frames accepted (`0xDF`), `0x40F9` → `0x4006`,
   `COMPLETE` → `0x4015` (normal on this loader), loader reset.
5. The unit **red-LED'd** (bootloader/factory-gate error), then the operator
   reported the splash and a reset loop.
6. The sealed IFS was restored (`--template-ifs local/images/ifs0.sealed.bin`,
   181 frames).  The unit then showed the **IFS splash** and reset — the factory
   CRC gate passed, so the IFS was good, but the OS could not continue: the
   **RCFS (`/base`) at region-0 `0xC00000` was clobbered**.
7. The RCFS head was restored (first 32 MiB of the vendor component at block
   `0x8D5`, 2055 frames).  The unit **booted TabletOS fully**.
8. Read-back verification passed (§5).  `BOOTCTR_FCTRY` read `32 00 00 00` and
   the TabletOS top-up held it there.

---

## 3. Root cause: the V1 `qcfp` record, and a region-relative offset

Report 98 §2.3 describes path 1 as `dest = p34.start + rrcq.offset`.  On this
unit that model is incomplete in a way that matters:

* The live MCT has **no `0x39` entries**, so the loader's "big V2 container"
  branch (which would parse `pfcq`/`rrcq`) does not engage.  What it uses is the
  **V1 `qcfp` run record**, which `QCFM_parse` returns for the whole-image path.
* The V1 record's `offset` is **region-relative**, and the loader adds the region
  start **`0x815`**: `dest = 0x815 + qcfp.run[0].offset`.

The vendor component list proves the units.  `PB-4869-root.0.lst` (from
`bb10mt unpack` of the operator's autoloader) is:

```
PB-4869-root.0.0.mbr=8192,2
PB-4869-root.0.1.rcfs=192,0     -> 192 x 64 KiB = 0xC00000  (RCFS)
PB-4869-root.0.2.mbr=0,0        -> 0                        (MBR)
PB-4869-root.0.3.ifs=16,0       -> 16 x 64 KiB = 0x100000   (IFS)
PB-4869-root.0.4.ufs=16640,0    -> portable UFS container
```

And the IFS template's V1 record is `offset=0x10, count=44` — exactly the `.lst`
`ifs=16`, i.e. region-0 `0x100000`, the IFS slot.  `0x815 + 0x10 = 0x825`, the
IFS.  The proven `--template-ifs` flash works because the template's V1 record
already carries the right region-relative offset; its V2 patch to `0x20` is
irrelevant on this loader.

The new payload container left V1 at `0x10` but patched only V2 to `0x2110`.
The loader used V1: `0x815 + 0x10 = 0x825` → the IFS.  The write should have
used `0x2915 - 0x815 = 0x2100`.

### Container shape

`bb10mt pack -v 2` produces:

```
mfcq V1 (version 1, no qcfp) ... mfcq V2 ... pfcq ... rrcq
```

The loader rejects the first frame with `0x11`.  Vendor autoloaders are built
with **`bb10mt pack -v 1,2`**, which emits both the V1 `qcfp` record and the V2
`pfcq`/`rrcq`.  The template clones that shape.

---

## 4. The exact damage (computed, not estimated)

```
payload            32 MiB = 512 loader blocks, container 33,620,008 B
container patch    V1 run0.offset = 0x10  (unchanged), Count = 512
                   V2 rrcq.offset = 0x2110,        Count = 512
loader used        V1: dest_block = 0x815 + 0x10 = 0x825
dest raw           (0x825 - 0x15) * 0x10000 = 0x8100000
written            blocks 0x825..0xA24 = raw 0x8100000..0xA0FFFFF
region-0 relative  0x100000..0x20FFFFF
```

Region 0 layout (report 98 §5.4 / the vendor `.lst`):

| zone | region-0 offset | loader block | fate |
|---|---:|---:|---|
| MBR / SIG | `0x0..0xFFFFF` | `0x815..0x824` | **untouched** (write began at `0x825`) |
| IFS slot | `0x100000..0xBFFFFF` | `0x825..0x8D4` | first 44 blocks overwritten, rest padding |
| RCFS / `/base` | `0xC00000..0x20000000` | `0x8D5..0x2814` | **first 21 MiB overwritten** (`0xC00000..0x20FFFFF`) |

The RCFS is 500 MiB (`PB-4869-root.0.1.rcfs` = 524,288,000 B = 8000 blocks); only
its first `0x1500000` bytes (21 MiB) were inside the write.  Everything from
region-0 `0x2100000` onward was untouched.

---

## 5. Recovery, and what was verified

### 5.1 Sealed IFS (region-0 block `0x825`)

```bash
python3 tools/pre_os_ifs_flash.py --template-ifs local/images/ifs0.sealed.bin
```

181 frames, loader reset.  Verified over the door:

```
dd if=/dev/hd1 bs=512 skip=2048 count=5632   # 0x2C0000, then take 0x2BFE10
sha256 c72b053027769c4d49270e9ee208fd2b0afcf69ce4fb3cf0c0be3c938ee05299
os_type 6, stored CRC 0x116B2562 == CRC32/ISO-HDLC of [0, 0x2BFE0C)
```

(The first read-back looked like a mismatch because it hashed the 44-block
padded slot `0x2C0000`; the image is `0x2BFE10`.  Hash the image length.)

### 5.2 RCFS head (region-0 block `0x8D5`)

Only the first 32 MiB of the vendor component were written — enough to cover the
21 MiB that was destroyed, with 11 MiB of margin:

```bash
head -c $((32*1024*1024)) PB-4869-root.0.1.rcfs > /tmp/rcfs-head32m.bin
python3 tools/pre_os_ifs_flash.py --payload /tmp/rcfs-head32m.bin \
        --dest-block 0x8D5 --allow-region0
# container: dest 0x8D5, V1 offset 0xC0, V2 offset 0xD0, count 512
```

2055 frames, loader reset, unit booted TabletOS.  Verified:

```
media:  dd if=/dev/hd1 bs=1M skip=12 count=32 | sha256sum
vendor: head -c 32M PB-4869-root.0.1.rcfs      | sha256sum
both   1e04336b8f70f172367ab43502d38abce164f5eadefbc9afcb815981b1eba9eb   MATCH
```

This also **proves the base**: V1 offset `0xC0` landed at `0x8D5` (region-0
`0xC00000`), i.e. `0x815 + 0xC0`.  Had the base been `0x805`, the restore would
have landed 1 MiB low, the RCFS would still be broken, and the unit would not
have booted.

### 5.3 End state

| item | value |
|---|---|
| TabletOS | up (QNX 6.6, `df /` 58 G / 2.2 G) |
| sealed IFS | sha256 `c72b0530…`, `os_type` 6, CRC `0x116B2562` |
| RCFS head | first 32 MiB == vendor component (`1e04336b…`) |
| region-0 MBR zone | intact (`eb 10 90 …`) |
| region 1 at `0x29000000` | still the pre-existing test blob (`4a448e1c…`) — **the rootfs never landed there** |
| `BOOTCTR_FCTRY` | `32 00 00 00` |
| `accounts/devuser/startup.sh` | 4490 B |

---

## 6. The corrected tool

`tools/pre_os_ifs_flash.py`:

* `--payload IMG --dest-raw HEX` (or `--dest-block`): an arbitrary block-aligned
  payload, **region 1 only** unless `--allow-region0` (region-0 is for recovery
  only).  `--dest-raw` does the `(block - 0x15) * 0x10000` conversion in Python.
* Every container now comes from the **vendor hybrid template** and patches
  **both** records:
  * V1 `qcfp.run[0].offset = dest_block - 0x815`, `Count = n`;
  * V2 `rrcq.offset = dest_block - 0x805`, `Count = n`;
  * the two `qcfp` checksums are recomputed.
* The host-side gate refuses anything outside the measured `0x1B` extent and
  refuses a region-1 destination below `0x2815`.
* `--keep-loader` / `--reuse-loader` for chained writes in one bootrom session.

`bb10mt pack` recipes: **`-v 1,2`** for an autoloader-style file (V1 `qcfp` +
V2 `pfcq`/`rrcq`).  `-v 2` alone is not accepted by this loader.

---

## 7. Residual / next

* **The region-1 rootfs write has not been retried.**  The corrected container
  dry-runs as `dest 0x2915, V1 offset 0x2100, V2 offset 0x2110, count 512`.
  Because the write leaves region 0 alone, the next attempt can be verified
  **before** any kernel flash: after the loader resets, TabletOS boots and
  `dd if=/dev/hd2 bs=1M skip=16 count=32 | sha256sum` must equal the rootfs
  sha256.  If it does not, nothing in region 0 was touched.
* Region-0's IFS-zone padding `0x3C0000..0xBFFFFF` still contains ext4 bytes
  from the misrouted write.  It is unused; the IFS image is `0x2BFE10`, well
  inside the first 44 blocks.  Not restored.
* The recovery used a 32 MiB slice of the vendor RCFS.  A full 500 MiB RCFS
  restore remains possible from `PB-4869-root.0.1.rcfs` if ever needed.
* Report 98 §2.3's `dest = p34.start + rrcq.offset` is the model that misled the
  first attempt.  The operational rule is now trap 55: **V1, region-relative,
  base `0x815`**.

---

## 8. Bench cycle

| # | action | result |
|---|---|---|
| — | TabletOS up, door up, `BOOTCTR_FCTRY = 0x32` | — |
| 1 | `--payload rootfs-region1.ext4 --dest-raw 0x29000000` (V2-only container) | first frame `0x11`; nothing written |
| 2 | `--reuse-loader` on the stale loader | `set_mode(2) failed` |
| 3 | `tools/force_reset_loader.py` | back to the bootrom |
| 4 | rebuilt from the template, V2 patched only | 2055 frames, **written at region-0 block 0x825** |
| 5 | unit red-LED → operator: splash + reset loop | region-0 IFS + RCFS damaged |
| 6 | `--template-ifs ifs0.sealed.bin` | 181 frames, splash now reached |
| 7 | `--payload rcfs-head32m.bin --dest-block 0x8D5 --allow-region0` | 2055 frames, **TabletOS boots** |
| 8 | verification (§5) | IFS, RCFS, MBR, region 1, `BOOTCTR` all as recorded |
