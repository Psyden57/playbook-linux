# 158 — `rfs_validator` on BB10 2.1.0.1917: measured behaviour, and what a modified `/base` needs

Status: **partial** — measured facts plus the binary's own `use` text. The deep
static RE (hash coverage, NVRAM record, blacklist) is still open; see §6.

## Answers in brief

* `-c` is **not just a check**. Its own `use` text says: *"Runs in compare mode,
  which causes the program to generate a hash and verify it against a stored
  value. **The root filesystem will be automatically mounted** to the device
  specified by the NVRAM."*  Deleting the `.script` line therefore removes the
  **mount** as well as the check.
* The stored value is a **signed** hash (`hu_RSA*`/`hu_ECDSA*` in `libhuapi`).
  No signing key exists outside RIM, so **any** change to `/base` fails
  verification — skipping the validator is the only route for a customised
  `/base`, and the boot must then mount the partition itself.
* On the reference unit the signature block is read from **`/dev/hd1`, offset
  `983040` = `0xF0000`** — that is *inside region 0, 64 KiB below the IFS*, not
  inside the RCFS partition.
* `/base` must be mounted as the live system has it: **qnx6, `rdonly`,
  `noatime`, `crypto=enable,sync=optional`**.

## 0. Target and method

| item | value |
|---|---|
| binary | `local/proprietary/device-binaries/ifs-1917/rfs_validator`, 50,554 B, stripped ARM EABI5 ELF, QNX, `/usr/lib/ldqnx.so.2` |
| extracted from | `PlayBook_2.1.0.1917.4.ifs` → `proc/boot/rfs_validator` (offset `0x6a0800`, size `0xc57a`) |
| invoked at boot | `proc/boot/.script`: `service_start rfsvalidator` → `rfs_validator -c` → `service_stop rfsvalidator`, then `sanityChecking test /dev/hd1 /dev/hd2 2564 /proc/boot/sanity 99 /proc/boot/sanity.cfg`, then `ksh /base/scripts/startup.sh` |
| unit | reference, PIN `0x50175127`, OS 2.0.0.4869 UFS + 2.1.0.1917 boot/RCFS under test |

## 1. The binary's own `use` text [M, operator-run]

```
rfs_validator - Calculate the SHA of a compressed filesytem, and verify it against
     a signed hash.

rfs_validator [-vc] -f <input_file>

              -f <input file> A qnx6 filesystem image to compress.
              -c Runs in "compare mode", which causes the program to generate a hash
       and verify it against a stored value.  The root filesystem will be
       automatically mounted to the device specified by the NVRAM.
              -v increase verbosity
```

So `-f` is RIM's *generator* mode and `-c` is the verifier. There is no
mount-only option.

## 2. Measured run on the reference unit [M, operator-run]

```
# /proc/boot/rfs_validator -vc
rfs_validator - running on Winchester.
rfs_validator - can_do_directio: 1
rfs_validator - read signature block from /dev/hd1, at offset 983040.

mounting /dev/hd1t179 at /base failed (16)
```

* machine name is **Winchester** (the binary has separate paths for
  Winchester2 / Winchester / Colt / Spencer and an "unknown machine" path);
* `can_do_directio: 1` — it uses `O_DIRECT` reads for the hash;
* **signature block: `/dev/hd1` @ `983040` = `0xF0000`**, read from the *region-0
  device*, not from the partition;
* the mount failure `16` is `EBUSY`: `/base` is already mounted (expected on a
  running system), so this run did not disturb the mount.

Consequence for the region-0 map [I]: there is a **signature block at
`0xF0000`**, i.e. in the 64 KiB immediately below the IFS image (`0x100000`).
It is neither inside the IFS (`0x100000`–`0x3BFE10`) nor inside the RCFS
partition (`0xC00000`–`0x20000000`). Anything that rewrites that 64 KiB affects
the RCFS signature, not the IFS CRC.

## 3. How the live system has `/base` mounted [M, `df -g`]

```
/dev/hd1t179   /base/
    Blocks:   127996 total     24143 avail   [4096-byte blocks]
    Files :    10000 total      3353 avail
    Type  : qnx6
    Flags : 00000021 [noatime, rdonly]
    Option: crypto=enable,sync=optional
```

and for comparison, the UFS:

```
/dev/hd3   /
    Blocks: 15273980 total  14651092 avail  [4096-byte blocks]
    Type  : qnx6
    Flags : 00000024 [noatime, nosuid]
    Option: crypto=enable,alimit=65536
```

`/base` is mounted **read-only** and stays that way; `crypto=enable` appears on
both filesystems, so it is a server-side default rather than something specific
to `/base`.

## 4. The replacement for the `.script` line [I — untested]

```
service_start rfsvalidator
mount -t qnx6 -r -o noatime,crypto=enable,sync=optional /dev/hd1t179 /base
service_stop rfsvalidator
```

* `-r` matches the live `rdonly` flag;
* the device should be **probed** the way the validator probes it —
  `hd1t179`, `hd2t179`, `hd7t179`, `hd7t178` — or taken from the NVRAM record the
  `use` text mentions, so the image is not tied to one unit's layout;
* this is **not yet tested**; see §6 for the cheapest test that would validate it.

## 5. Consequences for a customised kernel/boot image

1. `proc/boot/.script` is inside the **compressed** IFS, so the edit is an
   `mkifs` rebuild, not an in-place patch.
2. The rebuilt image is CRC-sealed only if resealed (`os_type 4 → 6`, CRC over
   `[image, end−4)`); see `ARMING-TUTORIAL.md` §3.
3. A rebuilt image can only boot on the **factory path**, so the unit must be
   armed first.
4. `/base` itself can then be replaced with any qnx6 filesystem of the right
   size, with no signature — but note the size: 127,996 × 4096 = 500 MiB, and
   24,143 blocks were free at measurement time.

## 6. Open questions, and what would settle each

| question | how |
|---|---|
| does the fs server need anything `rfs_validator`'s "Security init" provides? | on the live unit, mount the same partition read-only at a **second** mountpoint (`mkdir /testbase; mount -t qnx6 -r /dev/hd1t179 /testbase; ls /testbase`). Read-only, does not touch `/base`. |
| which NVRAM record names the root-fs device, and what does it contain on this unit? | read-only NVRAM read + static RE of the mount path |
| what exactly does the signed hash cover (whole partition? used blocks? the "compressed filesystem")? | static RE of the `-c` hash loop, or `-f` on a small test image |
| what do the `blacklist check` and `/radio/etc/radio.version` / `os_signature_file` paths do, and do they have side effects boot needs? | static RE |
| is `/base` usable when mounted read-only for the whole uptime (does anything write to it)? | it is mounted `rdonly` on the running system, so presumably yes |

## 7. Supersedes

* The earlier idea that the RCFS could be **re-signed** with `mkrcfs`: wrong for
  this release. `PlayBook_2.1.0.1917.1.rcfs` is a **plain QNX6 filesystem**
  (x86 MBR sector at `0x0`, QNX6 magic `0x68191122` at `0x2000`), and the hash is
  verified against a **signed** value whose key we do not have.
* Report 98's row claiming an RCFS header with `rcfs_hdr.hash1/hash2` at `+0` and
  a `'r-c-f-s'` superblock at `+0x1000` does **not** describe this artifact:
  measured, `+0x0` is an x86 MBR and `+0x1000` is zeros, with the QNX6 superblock
  at `+0x2000`.  Left as a contradiction until the artifact report 98 described is
  identified.

## 8. The signature block is a separate media component [M]

`bb10mt unpack` extracts it as `PlayBook_2.1.0.1917.3.sig` (65,536 bytes), and the
`.lst` numbers are **destination offsets in 64 KiB blocks**, which decode exactly
against the region-0 map:

| component | `.lst` value | × 64 KiB | meaning |
|---|---|---|---|
| `.1.rcfs` | 192 | `0xC00000` | RCFS partition inside region 0 ✔ |
| `.3.sig` | 15 | `0xF0000` | **the signature block `-vc` reported reading** ✔ |
| `.4.ifs` | 16 | `0x100000` | the IFS image ✔ |
| `.0.mbr` | 8192 | `0x20000000` | end of region 0 — not yet decoded |
| `.2.mbr` | 0 | `0x0` | media offset 0 |
| `.5.ufs` | 16640 | `0x41000000` | does not match region 1 (`0x28000000`); not yet decoded |

So the RCFS signature is a **separate 64 KiB block at region-0 `0xF0000`**,
written by the flasher as its own component. It sits outside the IFS
(`0x100000`–`0x3BFE10`) and outside the RCFS partition (`0xC00000`).

Layout of the stock block (only **549 of 65,536 bytes** are non-zero):

```
+0x0000  32 B  digest, high entropy
+0xFDD0  "QNXH-OS-1"  ... "                      QNXH" 88
+0xFDF8  65 B  high entropy   (signature material)
+0xFE3C  44 B  high entropy
+0xFE69  20 B  high entropy
+0xFE88  "QNXL-OS-1"  ... "                      QNXL" 88
+0xFEB4  65 B / 47 B / 17 B
+0xFF44  "QNX-OS-1"   ... "                       QNX" 80
+0xFF70  63 B / 65 B
+0xFFF8  0xD7C82D1F FFFFFFFF   (the container cookie, cf. IFS end-8)
```

Three records — **`QNXH-OS-1`, `QNXL-OS-1`, `QNX-OS-1`** — i.e. one per
security variant (high / low / general purpose). That is consistent with
`rfs_validator` carrying `hu_RSA*` **and** `hu_ECDSA*` and with its
machine-specific paths (`Winchester2` / `Winchester` / `Colt` / `Spencer`): the
record is selected by the device's type, not hardcoded.

**What the digest covers is still open.** `sha256(whole .rcfs) =
8ccf518cc2a82f0d127af1000b3f73505aa1f402d1c10d48a339708d7ae6beb4`, but the block's
digest is `1567bb9a2fcd2faf9511e0d3417b0eec7aa351b601999bdf1ca4922f334f6c98` — so
it is *not* the whole 500 MB file. Candidates to test next, all cheap:
the file minus the `0x2000` partition header; the QNX6 "used" extent from the
superblock; a hash over the *compressed* block stream; or a hash over the
container's `[0x2000, filesystem end)` range.

Practical consequence: replacing the RCFS leaves this block stale, and we cannot
re-sign it. Leaving it means verification fails; zeroing it means an untested
path. Patching the IFS to mount instead (§4) remains the only route that keeps
the unit bootable.

## 9. Recommended order of operations for a customised 1917 boot

Written for the reference unit, which is the last one. Each step changes **one**
variable, and each has a stated check and a way back.

**0. Freeze a byte-exact way back, and make a manifest.** You already have all
five 2.1.0.1917 components plus the `.signed` container; add a checksum list
(`sha256sum`) for each, and note that the pre-OS `0xF7` loop writes whatever it
is handed — it verifies nothing, so it is the recovery path for every step below
as long as the ROM/loader still enumerate.

**1. Finish the `rfs_validator` RE before building anything** (§6, still open).
Two answers change the plan:
* is a *failed* verification fatal (shutdown) or does the OS continue with `/base`
  unmounted/read-only? If it is not fatal, the IFS patch may be unnecessary;
* what the signed hash covers — if it is the "used extent" only, a same-size edit
  of `/base` might survive verification, which would remove the IFS patch
  entirely.
Cheap, static, no device. Do this first because it can delete a whole step.

**2. Prove the mount line alone, read-only.** On the running stock unit:
`mkdir /testbase; mount -t qnx6 -r /dev/hd1t179 /testbase; ls /testbase`.
If it lists files, the fs server mounts the partition without `rfs_validator`
having run — the replacement is sound. `EBUSY` is inconclusive, a hard error means
the plan changes.

**3. Reproduce the stock IFS with `mkifs` *before* editing it.** Reconstruct the
build file (entry list, permissions, `[+script] .script`, the 44 KB `startup.*`),
rebuild, and compare with `PlayBook_2.1.0.1917.4.ifs`: entry names/offsets/sizes
from `dumpifs`, and the `Checksums:` line. Byte-identical output is unlikely
(different `mkifs` build, different compressor), so the acceptance test is
*functional* — but you must be able to build the unmodified image before you trust
a build of the modified one.

**4. Arm with the stock CRC-sealed IFS first.** That proves the arming path itself
on this unit while every byte inside the IFS is still RIM's. Boot, confirm the
factory path runs and the counter decrements. Way back: the pre-OS loop.

**5. Then the mount replacement, still on the stock `rcfs`.** Build the edited
`.script`, reseal (`os_type 4→6`, CRC), flash, boot. Success criterion: `/base`
is mounted, `ksh /base/scripts/startup.sh` runs, and the OS comes up — with the
RCFS untouched, so a failure here cannot be blamed on `/base`.

**6. Only then the custom RCFS.** Keep the release consistent: 2.1.0.1917 IFS +
2.1.0.1917 RCFS + a matching UFS. Mixing a 1917 boot image with the 4869 UFS
(library/`startup.sh` versions) is a second variable you do not want in this test.
The `.sig` block at `0xF0000` is then irrelevant — leave it stock and record it.

**Never** in one step: change the IFS *and* the RCFS, or change the record *and*
the IFS. And keep a note of the exact media ranges written, so a later session can
tell what is stock and what is not.
