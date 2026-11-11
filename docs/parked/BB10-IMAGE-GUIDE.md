# BB10 image editing guide — IFS and RCFS, any BB10 version

How to take a BB10 component set, change its IFS and `/base`, and build a
container the PlayBook loader accepts.  Written from the session-25 run on
`Z10_10.3.03.2049_STL100-1`; the commands are the ones that were run.  **Read
`SAFETY.md` first** — one variable per flash, and keep the way back verified.

Tooling: `. tools/env.sh` (QNX 6.6 SDP: `dumpifs`, `ntoarmv7-gcc`), the Python
tools named below, and `local/vendor/bb10mt/bb10mt` for the QNX6 UFS mount.
A portable CPython 3.2 is needed **only** to recompile a `.pyc` (see §3c); it is
not needed for IFS/RCFS work otherwise.

## 0. What the components are

A BB10 `PlayBook`-style set has the same deltas as TabletOS, and the vendor
`.lst`/`bbusb.py unpack` prints them:

| component | delta (64 KiB blocks) | media offset | format |
|---|---|---|---|
| `.0.mbr` (region-1 MBR) | 8192 | `0x28000000` | 64 KiB |
| `.1.rcfs` (`/base`) | 192 | `0x8C00000` | RCFS (`r-c-f-s`), not QNX6 |
| `.2.mbr` (region-0 MBR) | 0 | `0x8000000` | 64 KiB |
| `.3.sig` | 15 | `0x80F0000` | 64 KiB |
| `.4.ifs` | 16 | `0x8100000` | QNX imagefs in RIM's container |
| `.5.ufs` (`/`) | 16640 | `0x49000000` | QNX6 |

Raw device offsets are `(block - 0x15) * 0x10000`; the flasher speaks loader
blocks, so use the deltas above as-is.

## 1. Identify a set (read-only)

```bash
python3 tools/bbusb.py parse SET.signed          # V1 qcfp + V2 pfcq runs
python3 tools/bbusb.py unpack -o work/ --prefix NAME SET.signed
```

`pfcq` types on a BB10 device container: `0x05` UFS, `0x06` MBR, `0x08` IFS,
`0x09` RCFS, `0x89` sig2.  **A device container is often V2-only**, which this
loader rejects; you will repack V1+V2 with PlayBook deltas (§5).

The IFS descriptor: `os_type` at image `+0x100`, magic `0xC3D0E193` at
`+0x9C`, and the `end-8` cookie at `end-8`.  Vendor BB10 images are
**`os_type = 4`** (product/ECDSA) with the magic/cookie zeroed — they need the
reseal in §2d.

## 2. The IFS

### 2a. Read it

```bash
dumpifs Z10_....0.0.ifs                      # QNX 6.6 dumpifs reads BB10 IFS
python3 tools/ifs_build.py listing IMG.ifs > work/ifs.lst
mkdir work/tree && (cd work/tree && dumpifs -x "$OLDPWD/IMG.ifs")   # may need rounds
```

`tools/ifs_extract.sh IMG tree` loops the "create the missing directory" rounds,
but `dumpifs` creates directories with the image's modes, so a read-only
directory stops it with `Permission denied`; `chmod -R u+rwX tree` and re-run.
Symlinks are not created (they come from the listing at build time).

### 2b. Change it

The builder is listing-driven and writes an **uncompressed** imagefs appended to
RIM's prefix:

```bash
python3 tools/ifs_build.py build \
  --tree work/tree --listing work/ifs.lst --stock IMG.ifs --out work/new.ifs \
  --replace proc/boot/ifs_variables.sh=work/edited.sh \
  --add     proc/boot/newfile=work/newfile \
  --add     proc/boot/gfx4430/displayHAL-winchester.so=work/displayHAL-winchester.so
```

* `--replace` keeps a smaller file in its reference slot; a size increase (or
  any `--add`) repacks the whole imagefs.
* The image header's `boot_ino` (`0xE0000002`, procnto) and `script_ino` (3)
  are taken from the stock listing; do not lose them.
* Every added file needs its parent path as an image path; directories are
  implicit.

Then splice the new imagefs into RIM's container (the vendor IFS is
compressed; see §4 for why this step exists):

```bash
python3 tools/bb10_ifs.py splice --stock IMG.ifs --imagefs work/new.ifs --out work/new.spliced.ifs
python3 tools/bb10_ifs.py verify work/new.spliced.ifs      # blocks, size, trailer, QNXH
dumpifs work/new.spliced.ifs > work/new.lst               # must list your tree
```

### 2c. What the splice does (so you can debug it)

The payload is **one chunked NRV2B stream of the whole imagefs**
(`[u16 BE len][block]`, 64 KiB blocks) from `0x808+startup_size` to
`0x808+stored_size`; RIM's ARM decompressor stub is before it and its
`QNXH-OS-1` block after it.  `bb10_ifs.py` patches exactly:

```
stored_size  (+0x24) = startup_size + C + 8      (C = padded payload length)
imagefs_size (+0x2c) = uncompressed imagefs length
ram_size     (+0x1c) = startup_size + imagefs_size
startup trailer (H+startup_size-4) = -sum32([H, H+startup_size-4))
```

and appends `00000000 + -sum32(payload)` before `QNXH-OS-1`.  `flags1` stays
`0x0d` (VIRTUAL|UCL).  If `dumpifs` lists the tree and `verify` passes, the
container is structurally right.

### 2d. Reseal for the PlayBook factory path

```bash
python3 tools/89-crc_tool.py reseal work/new.spliced.ifs work/new.sealed.ifs \
    --image-off 0 --length auto --append-os-trailer --type 6
```

It must end with `RESULT: VERIFIED (CRC path)`: `os_type = 6`, CRC over
`[0, end-4)`, `end-8 = 0xD7C82D1F`, `LoadEnd` patched.

## 3. The RCFS (`/base`)

`/base` is the `r-c-f-s` container: superblock at `0x1000`, 1-based 32-byte
inodes at `inodes_off`, names at `names_off`, file data LZO1X in 16 KiB chunks
with a 32-bit offset table (`emode` bit 7 selects LZO).  `bb10mt qnx6 mount`
cannot open it.

### 3a. Read and extract

```bash
python3 tools/rcfs_extract.py list    BASE.rcfs > work/base.list
python3 tools/rcfs_extract.py extract BASE.rcfs work/base
```

Do **not** rely on `bb10mt rcfs extract`: it aborts the whole tree on the first
chunk it cannot decode (trap 78).

### 3b. Replace one file in place (no repacker needed)

For a file whose new content compresses into its old slot:

1. find the inode (scan `inodes_off + (idx-1)*32` for the matching `offset`);
   the file's data starts at `inode.offset`, the chunk table is
   `[u32 table_size][u32 off1]…`, and the slot size is
   `next_data_offset - inode.offset`;
2. compress the new bytes with `lzo1x_999` in 16 KiB chunks (the stock
   compressor), build the table (`table_size = (nchunks+1)*4`, then the running
   ends), and check `table + chunks ≤ slot`;
3. write table + chunks at `inode.offset` and set `inode.size` (`+0x10`) to the
   uncompressed length;
4. decode the chunk back and compare — always.

Reference values from the 10.3 `/base`: `deckard/platform/blacklist.pyc`
inode 12,015, 8,780 → 7,849 B, LZO 3,323 → 2,957 in a 3,324 slot;
`seed/rootfs/var/pps/system/navigator/config` inode 3,771, 431 → 432 in 440;
`scripts/startup.sh` inode 1,648, 200,849 → 198,900 B, LZO 57,414 → 57,382 in
57,414 — the last one needed 26 decorative comment lines removed to fit; always
measure the recompressed size before writing.

### 3c. Editing a `.pyc` (e.g. the `autorun`/blocklist writers)

A Python 3.2 `.pyc` must be recompiled with 3.2 (the device's interpreter is
ARM).  Build a portable host interpreter once (nothing system-wide):

```bash
curl -fsSL -o py326.tgz https://www.python.org/ftp/python/3.2.6/Python-3.2.6.tgz
tar xzf py326.tgz && cd Python-3.2.6
CFLAGS="-O0 -g -fcommon -fno-PIE -fno-strict-aliasing" LDFLAGS="-no-pie" \
  ./configure --without-ensurepip --disable-shared && make -j4
```

(The first build without `-no-pie` segfaulted on any `-c` execution.)  Decompile
with `uncompyle6` (a venv is fine), edit, then compile:

```python
import imp, marshal, os
src = open('edited.py','rb').read()
code = compile(src, '/usr/lib/python3.2/site-packages/.../mod.py', 'exec')
out = imp.get_magic() + int(mtime).to_bytes(4,'little') + size.to_bytes(4,'little') + marshal.dumps(code)
```

The pyc header is 12 bytes in 3.2 (magic + mtime + size).  `uncompyle6` outputs
`return` in empty exception classes (invalid source) — change those to `pass`.
Validate with `marshal.loads(pyc[12:])` under the 3.2 interpreter.

### 3d. The verifier, and the shim

`rfs_validator` validates a signed per-file hash table before mounting the
RCFS through `fs-rcfs.so`; any edit invalidates it.  Replace
`proc/boot/rfs_validator` in the IFS with `tools/rfs_rcfs_shim.c` (build with
`ntoarmv7-gcc -O2 -Wall -o rfs_rcfs_shim tools/rfs_rcfs_shim.c`); it mounts
`"rcfs"` at `/base` and treats `EBUSY` as success.  `fs-rcfs.so` itself only
reads.

## 4. Setup skip: `autorun::0` in two places

The first-boot setup is skipped by `autorun::0` in the UFS **and** in the RCFS
seed:

* UFS: `var/pps/system/navigator/config` — mount a **copy** with
  `bb10mt qnx6 mount -i COPY -m DIR`, replace the one byte (`autorun::1` →
  `autorun::0`), `sync`, then `fusermount -u DIR` (never `-z`; check
  `fuser -vm DIR` first);
* RCFS: `seed/rootfs/var/pps/system/navigator/config` — first boot uses it to
  re-seed `var/`, so edit it the same way as §3b.

On 10.3 the UFS `.seed` is only branding JARs plus an empty `config/`; the
second copy is in the RCFS.  Verify by counting in the raw images
(`grep -aoc 'autorun::0' FILE`).

## 5. Pack and flash

```bash
printf '%s=8192,0\n%s=192,0\n%s=0,0\n%s=15,0\n%s=16,0\n%s=16640,0\n' \
  PlayBook_<v>.0.mbr BASE.rcfs PlayBook_<v>.2.mbr PlayBook_<v>.3.sig \
  new.sealed.ifs UFS.ufs > set.lst
python3 tools/bbusb.py pack -l set.lst -c set.signed -v 1,2 -s
python3 tools/bbusb.py parse set.signed
python3 tools/bbusb.py flash --signed set.signed          # dry run: gate only
```

`-v 1,2` is required (V1 is what this loader uses); `-s` appends the 560-byte
signature.  Run the flasher **as the user, never `pkexec`**.  To write:

```bash
while ! lsusb | grep -q '0fca:0001'; do sleep 1; done
python3 tools/bbusb.py flash --signed set.signed --from-bootrom --password 0000 --apply
```

If the unit is already in the ramloader (`0fca:8001`, e.g. after
`info --keep-loader`), omit `--from-bootrom`.  Success: payload reaches the
total, `0x40F9 signature -> cmd 0x4006`, `COMPLETE 0x40C0 -> cmd 0x4006`,
`done`.  The tool resets the unit unless `--keep-loader` was used with `info`.

## 6. Verify, and read the loader's verdict

```bash
python3 tools/bb10_ifs.py verify new.sealed.ifs
python3 tools/89-crc_tool.py verify new.sealed.ifs | tail -3
dumpifs new.sealed.ifs | tail -1
```

On the device, `python3 tools/bbusb.py info --from-bootrom --password 0000
--keep-loader --json` reports `boot_mode.mode_name` and `factory_count`.
**The counter is the diagnostic**: it decrements only on a successful
factory/CRC verify, so

* counter unchanged after an attempt → the loader rejected the image
  (descriptor/CRC/length);
* counter decremented → the image verified and the loader jumped; a black
  screen/no USB after that is the OS (kernel/board support), not the container.

`info` also decodes the region-0 descriptor (`os_metrics`: `os_address`,
`image_magic`, version/build), which confirms what the loader sees.

## 7. Way back, and the counter

The counter is the factory boot budget: every successful boot spends one, and
only a TabletOS boot (`/accounts/devuser/startup.sh` top-up) puts it back to
`0x32`.  Keep a packed way-back container ready:

```bash
printf '%s=8192,0\n%s=192,0\n%s=0,0\n%s=15,0\n%s=16,0\n%s=16640,0\n' \
  PlayBook_<v>.0.mbr PB-<v>.1.rcfs PlayBook_<v>.2.mbr PlayBook_<v>.3.sig \
  ifs-sealed.ifs PlayBook_<v>.5.ufs > back.lst
python3 tools/bbusb.py pack -l back.lst -c back.signed -v 1,2 -s
```

Session 25's way back is `local/work/back-1917.signed`
(1,104,544,336 B: our 1917 IFS + edited 1917 RCFS + the vendor `.5.ufs` with
`autorun::0`).  If the unit is left in the ramloader nothing drains; flash the
way back from there without `--from-bootrom`.

## 8. Pitfalls

| pitfall | rule |
|---|---|
| `bbusb.py pack -l` with a path that already has a prefix | the list takes paths as given; do not prepend `$PWD` to absolute paths |
| V2-only device container | repack `-v 1,2` with PlayBook deltas; `-v 2` is rejected `0x11` |
| IFS slot | region-0 IFS is limited to `0xC00000-0x100000`; a BB10 imagefs is 13.7 MB and must be compressed (§2c) |
| RCFS slot | an in-place replacement must fit `next_offset - inode.offset`; recompress at `lzo1x_999` and measure before writing |
| startup.sh slot | 10.3 had 2 bytes of slack; a hook/addition needs an equivalent comment trim |
| verifier | any `/base` edit needs `rfs_rcfs_shim` (trap 79) |
| `.pyc` | 3.2 magic and 12-byte header; `uncompyle6` needs `return`→`pass` in empty classes |
| `mount`/`fusermount` | edit copies; unmount cleanly (`fusermount -u`, never `-z`) with no cwd inside |
| counter | read it at both ends; a reset-looping image can spend several units per minute |
| password | pass `--password 0000` explicitly for this unit, once; never retry |
