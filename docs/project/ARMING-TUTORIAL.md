# Arming a PlayBook: the ECDSA (product) → CRC (factory) switch, step by step

**Who may run this: the operator, on a unit you can afford to lose.**  An agent
must never create or write the RPMB record (`SAFETY.md` §6).  Read `SAFETY.md`
§2–§4 and `FACTORY-PATH.md` first; this page is the *procedure*.

> **First, ask whether you need this at all.**  Arming exists so a **CRC-sealed**
> image can boot.  If the goal is only to *call the secure monitor* (the RPMB
> seal work), QNX's `/dev/trustzone` was the hoped-for shortcut: a root-only raw
> SMC pass-through (`devctl 0xC0280501`, report 107 §5.1 — the `0xC0285001` in
> the older text is a typo).  It was **tested and is blocked** (report 157: the
> stock resmgr segfaults in libc on the `0x5001` devctl), so it cannot replace
> arming.  The arm gate remains.

---

## 0. Why the switch exists

The loader chooses how to verify the OS by reading the RPMB record
`BOOTCTR_FCTRY` (`FUN_8000911A` → `0x800090CE`, report 150 §1):

| record | selector | verify |
|---|---|---|
| **absent** | 0 | product: **ECDSA/SHA-512** over the signed image |
| **present, 4 bytes, `1 … 0x32`** | 1 | factory: **CRC-32/ISO-HDLC**, and only if `os_type == 6` |
| present, `0` / `>0x32` / length ≠ 4 | 2 | failure |

**What is actually verified is the IFS image, not "region 0".**  The checked
object is the `0x2BFE10`-byte image at region-0 offset `0x100000`: the CRC covers
`[image, image_end−4)`, `os_type` is at image `+0x100`, and the stored CRC is the
image's last word.  Everything is then checked *inside* that image — the
signature trailer lives at `end−0x178` (magic `0xB5A60BFD`), so resealing
deliberately invalidates it, which is exactly why the product path stops trusting
the image.

The **RCFS** is a *different* partition and is **not** part of that image: it
starts at region-0 `0xC00000` (MBR entry type `0xB3`, LBA `0x6000`, `0xFA000`
sectors = 500 MiB, ending exactly at region-0 `0x20000000`), while the IFS image
ends at `0x3BFE10`.  The RCFS is validated later, by the IFS itself
(`rfs_validator`, which ships **inside the IFS** at `proc/boot/rfs_validator`).

Do not confuse the two filesystems: the **RCFS** is mounted at **`/base`**, the
**UFS** (the `hd3` FS partition, MCT type `0x1C`) is mounted at **`/`**, and they
are separate components of a release (`*.rcfs` and `*.ufs` in a bb10mt
extraction).  Neither is inside the `.ifs` — only the boot image is.  **Region 1
is not the UFS**: it is the second OS slot (`/dev/hd2`, 512 MB, another
IFS/RCFS), separate from the `hd3` filesystem.

Any self-built image is CRC-sealed (`os_type = 6`, CRC trailer), which breaks the
ECDSA signature — so it can only boot on the **factory** path.  Arming = make the
record exist **and** make **the IFS image** CRC-valid.

## 1. Host setup

The unit answers on **`169.254.0.1`** over its USB RNDIS interface (`0fca:8010`).
The host needs an address in that subnet *on that interface*; without one the
route falls through to the normal gateway and every connection times out.

```bash
# 1. find the RNDIS interface the unit created
ip -br link | grep -iE 'enx|usb|enp'      # Linux; look for the new one
# 2. give the host an address on it
pkexec ip addr add 169.254.0.2/16 dev <iface>
ping -c2 169.254.0.1                      # expect ~1 ms
# 3. start the door keeper (holds the door, starts the ssh forward)
(setsid nohup tools/door-hold.sh > /tmp/door.log 2>&1 < /dev/null &)
for i in $(seq 1 30); do timeout 12 tools/pbssh.sh 'echo DOOROK' 2>/dev/null \
  | grep -q DOOROK && { echo "door up"; break; }; sleep 4; done
tools/pbssh.sh 'uname -a'
```

Persistent variants, if you would rather not repeat step 2:

```bash
# NetworkManager
pkexec nmcli con add type ethernet ifname <iface> con-name pb-rndis \
     ipv4.method manual ipv4.addresses 169.254.0.2/16 ipv6.method ignore
# systemd-networkd: /etc/systemd/network/50-pb-rndis.network
#   [Match] Name=<iface>
#   [Network] Address=169.254.0.2/16  DHCP=no
```

On Windows and macOS the OS normally self-assigns a link-local address on the
RNDIS adapter, so the door often works without any setup — check with `ipconfig`
/ `ifconfig` (**untested here**).

**If step 2 seems unnecessary, it is not:** a profile or rule may be masking the
requirement.  See the appendix (§10) for the shapes such local rules take, and
remember they are host-specific — nothing in this section depends on them.

## 1a. Moving files to the unit, and where to run what

The device runs QNX with the door's ssh server, so **`scp` works and is the normal
way to push a file** — this project has always transferred binaries that way, and
**`sftp` clients connect too** (the ssh server implements the subsystem; there is
no separate `sftp` binary on the device, which is why it looks absent).

```bash
tools/pbscp.sh tools/memdump3 :/tmp/          # push  (':/path' = the device)
tools/pbscp.sh :/tmp/memdump3 ./              # pull
tools/pbscp.sh -O -r somedir :/accounts/devuser/   # directories need -O
```

`tools/pbscp.sh` is `tools/pbssh.sh`'s twin: same legacy algorithms, address
fixed, and the device side written as `:/path`.  Two device quirks it documents:
OpenSSH's default SFTP backend will not upload a *directory* whose destination
does not already exist, and **`/tmp` cannot `mkdir` at all** — it is a symlink to
QNX's `/dev/shmem`.  Use `-O` (legacy protocol; the device does have `scp`) and a
real directory such as `/accounts/devuser` for directory copies.

If `scp` is unavailable in your setup, `cat >` over the ssh channel is the
fallback the repo's own scripts use:

```bash
tools/pbssh.sh 'cat > /tmp/bc.bin' < /tmp/bc.bin
```

**What the device does *not* have is the inspection toolset** — `od`, `xxd`,
`head`, `cmp`, `md5sum`, `sum`, `bc`, `python` are all absent
(`DEVELOPMENT.md` §2), and `printf` is not reliable on a stock image (this bench
has it only through a custom UFS symlink).  So **create binary payloads and
inspect bytes on the host**, and only ever move finished files across:

```bash
printf '\x32\x00\x00\x00' > /tmp/bc.bin          # host
scp … /tmp/bc.bin root@169.254.0.1:/tmp/         # or: tools/pbssh.sh 'cat > /tmp/bc.bin' < /tmp/bc.bin
tools/pbssh.sh 'ls -l /tmp/bc.bin'               # size must be 4
tools/pbssh.sh 'dd if=/tmp/bc.bin bs=4 count=1 2>/dev/null' | od -An -tx1   # od on the host
```

Every `dd if=/tmp/<file> of=/dev/…` below therefore has a matching transfer step;
if you skip it the `dd` fails on a missing file (or worse, on a stale one from an
earlier attempt).

## 2. Read-only preconditions — stop at the first mismatch

```bash
# identity: the hardware PIN is NVRAM record 0x2002 (SAFETY.md §1)
tools/pbssh.sh 'uname -a'      # expect ...OMAP4430_Winchester_ES2.2_HS...
# known units: reference 0x50175127 (bytes 27 51 17 50), was the spare 0x50176EC1
#   The reference was armed by the operator on 2026-11-01 (DECISIONS.md §21) and
#   is the one working unit; this procedure is for a fresh unit.  An agent still
#   performs none of the writes.

# the record must be ABSENT for a product-path unit
tools/pbssh.sh 'ls /dev/rpmb/BOOTCTR_FCTRY'    # expect: No such file or directory

# the IFS image: read exactly 0x2BFE10 bytes (trap: bs=512 count=5632 is 496 too long)
tools/pbssh.sh 'dd if=/dev/hd1 bs=16 skip=65536 count=180193 2>/dev/null' | sha256sum
# os_type = image+0x100 = region-0 0x100100; CRC = image end-4 = region-0 0x3BFE0C
tools/pbssh.sh 'dd if=/dev/hd1 bs=512 skip=2048 count=1 2>/dev/null' > /tmp/s.bin  # byte 0x100
tools/pbssh.sh 'dd if=/dev/hd1 bs=4 skip=982915 count=1 2>/dev/null' | od -An -tx4 # CRC word

# NVRAM 0x2040 must be absent or 1..8 (0 and >=9 route to the nuke path)
# and 0x2801 +0x18 (A) / +0x1C (B) tell you which branch the unit takes
```

**The expected values depend on the OS version.**  The table below is the
**reference unit in this repository, OS `2.0.0.4869`**.  Most retail units run
**`2.1.0.1917`**, whose IFS is a different image with a different sha256 *and* a
different CRC — so **do not compare against these numbers unless you are on
4869**.  What is generic is the *layout*, not the values:

* the IFS is `0x2BFE10` bytes at region-0 `0x100000` (its length can differ per
  OS — read `end−4` from the descriptor's length field, or take it from the
  vendor `.ifs` file);
* `os_type` sits at **image `+0x100`** (region-0 `0x100100` for this layout);
* the CRC is the image's **last word** (`end−4`), so its *offset* moves if the
  image length differs;
* `89-crc_tool.py` computes and verifies the right CRC for whatever image you
  give it — prefer it over these literal values.

The reference unit's **stock, pre-flash** image (OS 2.0.0.4869) read:

| item | value |
|---|---|
| IFS sha256 (`0x2BFE10` bytes) | `75be24e8297e3a4a27b8f7ce2f9d857b94178b020c063d73ecb0cc90c0fe94ba` |
| `os_type` (region-0 `0x100100`) | **4** |
| CRC word (region-0 `0x3BFE0C`) | **`0x8D46FB4A`** |
| cookie (image `end−8`) | `0xD7C82D1F` |
| descriptor | `desc = image + 0x2C`: model `0x06001A06` at `desc+0x7C`, magic `0xC3D0E193` at `desc+0x9C`, `os_type` at `desc+0xD4` |

The operator flashed the sealed `os_type` 6 IFS on 2026-11-01, so the unit now
carries the sealed image, not these bytes; the table is the stock reference and
a precondition example, not a current reading (`STATE.md`).

**Which OS to start from is a choice, not a requirement.**  4869 is used here
because its UFS ships `/radio/scripts/env.sh`, which gives early automatic root
(later releases patched that path).  The same early root on `2.1.0.1917` is
available by patching the IFS itself — which is exactly what §3(c) is for, once
the unit is armed and only the CRC check applies.

Anything else means the unit has history — read `FACTORY-PATH.md` §5 first.

## 3. Make the IFS image CRC-valid (5 bytes)

Three ways; **(a)** is what report 95 proved, **(c)** is the one to use when you
start from a vendor image.

**(a) Patch the five bytes in place, from the booted OS** (no reboot, no flash):
`os_type` at image `+0x100` (`4 → 6`) and the CRC at image `end−4`
(`0x8D46FB4A → 0x116B2562` for the stock image).  Build and verify the result on
the host first, then write only the two sectors and read both back.

```bash
python3 tools/89-crc_tool.py reseal --image-off 0 --type 6 ifs_in.bin ifs_out.bin
python3 tools/89-crc_tool.py verify ifs_out.bin          # expect VERIFIED (CRC path)

# extract only the two sectors that actually changed, on the host:
#   sector 2048 (region-0 0x100000) holds os_type at image+0x100
#   the CRC sits in the image's last sector   (image end-4)
python3 - <<'EOF'
img = open('ifs_out.bin','rb').read()[:0x2BFE10]
end = len(img)
open('/tmp/sec_ostype.bin','wb').write(img[0:512])
open('/tmp/sec_crc.bin','wb').write(img[(end-4)//512*512:((end-4)//512+1)*512])
print("os_type sector -> LBA 2048 ; CRC sector -> LBA", 2048 + (end-4)//512)
EOF

# transfer them (scp, or cat > as here) and write (bs=512 conv=notrunc), then read both back
tools/pbssh.sh 'cat > /tmp/sec_ostype.bin' < /tmp/sec_ostype.bin
tools/pbssh.sh 'cat > /tmp/sec_crc.bin'    < /tmp/sec_crc.bin
tools/pbssh.sh 'dd if=/tmp/sec_ostype.bin of=/dev/hd1 bs=512 seek=2048 count=1 conv=notrunc'
tools/pbssh.sh 'dd if=/tmp/sec_crc.bin    of=/dev/hd1 bs=512 seek=7679 count=1 conv=notrunc'
```

(`seek=7679` assumes the 4869 layout; the script above prints the right LBA for
any image.)

**(a2) Prepare a vendor IFS file for the factory path (tested 2026-11-01)**

This is the file-based route: reseal a `.ifs` you already have, then flash it.
Measured on `PB-4869-root.0.3.ifs` (`$BB10MT/2.0.0.4869/`,
2,883,584 bytes = 44 × 64 KiB: the `0x2BFE10` image plus a **496-byte zero tail**).

```bash
# 1. look at it first: --image-off 0 is REQUIRED for a bare .ifs
#    (the tool defaults to 0x100000 for a raw region dump and then finds nothing)
python3 tools/89-crc_tool.py verify PB-4869-root.0.3.ifs --image-off 0
#   stock: structure PASS, "desc[0xD4] == 6 FAIL (4)", CRC PASS 0x8D46FB4A

# 2. reseal (writes a NEW file, never in place)
python3 tools/89-crc_tool.py reseal PB-4869-root.0.3.ifs \
        PB-4869-root.0.3-crc.ifs --image-off 0
#   -> os_type 4 -> 6 ; CRC 0x8D46FB4A -> 0x116B2562 ; "wrote 0x2C0000 bytes"

# 3. verify the result
python3 tools/89-crc_tool.py verify PB-4869-root.0.3-crc.ifs --image-off 0
#   -> RESULT: VERIFIED (CRC path), rc=0

# 4. exactly 5 bytes must have changed: os_type (1) + CRC (4)
cmp -l PB-4869-root.0.3.ifs PB-4869-root.0.3-crc.ifs
```

The tool takes the image extent from the descriptor, so the 496-byte tail is
ignored correctly — the CRC is computed over `[0, 0x2BFE0C)` and stored at
`0x2BFE0C`, **not** at `file_end-4`.

**Flashing it.** Our own pre-OS loop is the proven path for this slot (this
project has written it ~10 times), and the 44-block size matches it exactly:

```bash
python3 tools/pre_os_ifs_flash.py --template-ifs PB-4869-root.0.3-crc.ifs --dry-run
#   expect: "plan: region 0 (hd1), block 0x825 (raw 0x8100000), 44 blocks"
nohup python3 tools/pre_os_ifs_flash.py --template-ifs PB-4869-root.0.3-crc.ifs \
      > /tmp/flash.log 2>&1 &
# then, ~3 s later, reboot the running OS (TabletOS: /proc/boot/shutdown -S reboot)
# and wait for "done (" in /tmp/flash.log
```

After the flash, read the media IFS back and compare its sha256 with the file you
wrote, and read `BOOTCTR_FCTRY` (a successful factory boot decrements it).

**bb10mt, for reference** (BB10-world tool, not what this repo uses).  Measured,
and the repo already documents most of it — read **report 139** (container shape)
and **report 100 §4** (why `flash` cannot address this unit's IFS) before using
bb10mt for anything here.

* `bb10mt pack ... -c out.signed` produces the **`.signed`** container (not an
  `.exe`; `.exe` autoloaders come from `bb10mt autoloader create`).
* **`-v 1,2` is required.**  With it the container carries the V1 `qcfp` record
  plus V2 `pfcq`/`rrcq` (measured: 2,949,152 B for our 44-block IFS).  `-v 2`
  alone omits V1, which **this loader rejects with `0x11`** (report 139) — and
  empirically `unpack` of such a container also fails (exit 192).
* `bb10mt unpack -c <container>` is the right tool for `.signed` files.  It writes
  each component **next to the container** as `<basename>.<n>.<ext>` (not into the
  current directory — that is why an empty cwd looks like a failure).  **Verified
  round-trip: byte-exact**, our resealed IFS came back with the same sha256.
* `bb10mt split` is for **`.exe` autoloaders** only; on a `.signed`/QCFM file it
  exits 1 with `Invalid or corrupted PE file`.  That says nothing about `flash`.
* **Which loader path a bb10mt container takes is an open question, and it is a
  safety issue.**  Report 98 §9.3 gives two equivalent forms: **path 2**
  (`_type=8`, `rrcq.offset=0`, needs an MCT `0x39` entry with
  `PartitionID == 8`) and **path 1** (`rrcq.offset=0x20`; needs only the `0x34`
  MBR extent).  Measured on 2026-11-01: `bb10mt pack` maps the **IFS** component's
  `.lst` offset (`16`) to **`0`** — a path-2 container — while other components
  keep their offset (`192` → `0xC0`).  Report 100 §4 says the live MCT has no
  `0x39` entries (path 2 unavailable), yet the operator has flashed full firmware
  including the IFS with bb10mt, and report 139 §3 caught the loader consuming
  **V1** (`0x815 + 0x10 = 0x825`, path 1).  Both cannot be right about this unit —
  see `docs/contradictions/100-bb10mt-flash-path2-vs-operator-flashes.md`.  Note
  the danger if path 2 does *not* resolve: a V1 fallback with offset `0` would
  write at `0x815`, i.e. over the region-0 header and the `0xF0000` signature
  block.  Settle it read-only with `bb10mt info` (BootROM mode; it prints the MCT
  it sees) before flashing with bb10mt.  Our own
  `tools/pre_os_ifs_flash.py` emits **path 1 explicitly** (`rrcq` offset `0x20`,
  block `0x825`), which needs no `0x39` — the proven route.
* `bb10mt info` / `flash` both begin with `Connecting to BootROM...`, so the unit
  must be in BootROM/CFP download mode, not running an OS.

**Warning — keep the release consistent.** `/base` validation is per *RCFS*, not
per IFS: a 4869 IFS running against a 2.1.0.1917 `/base` may pass the signature
check and then misbehave badly at runtime (libraries, `startup.sh`).  Flash a 4869
IFS only where the RCFS is 4869.

**(b) Flash a CRC-sealed image through the pre-OS `0xF7` loop** — what we do for
our own chainloader (`tools/pre_os_ifs_flash.py --template-ifs …`).

**(c) Prepare an IFS extracted with `bb10mt` (e.g. `PB-4869-root.0.3.ifs`).**
This is how you make *any* IFS factory-path-ready, including one you have
modified (a custom RCFS loader, `rfs_validator` removed, extra utilities).

The vendor file's structure, as measured on `PB-4869-root.0.3.ifs`:

```
size 0x2C0000  (= 44 × 64 KiB — exactly the pre-OS flash payload)
[0x000000..0x2BFE10)  the IFS image            <- the only part the CRC covers
[0x2BFE10..0x2C0000)  496 zero bytes           <- keep it: the flasher works in 64 KiB blocks
image+0x000           fe 01 00 ea              ARM branch (image stub)
image+0x0A8           0x06001A06               model
image+0x0C8           0xC3D0E193               desc magic  (=> desc = image+0x2C)
image+0x100           0x04                     os_type (stock)
image end−0x210       0x48584E51 "QNXH"        QNX image header
image end−0x178       0xB5A60BFD               signature trailer (inside the image)
image end−8           0xD7C82D1F               cookie
image end−4           0x8D46FB4A               CRC
```

Procedure:

1. **Make every content change first** — rebuild with QNX `mkifs`, or patch the
   files inside the image (removing `rfs_validator`, changing what it points at,
   …).  Any byte change invalidates the CRC, so this must happen before step 2.
2. **Reseal last**, on the `0x2BFE10` image:
   `python3 tools/89-crc_tool.py reseal --image-off 0 --type 6 in.ifs out.ifs`,
   then `… verify out.ifs` must print `VERIFIED (CRC path)`.

   **Feed the tool the exact image, not the padded file.**  These lengths are
   OS-specific (they are the 4869 values); for another version take the image
   length from the `.ifs` itself, and if the tool would otherwise hash the whole
   padded file, cut it to that length first
   (`head -c $((0x2BFE10)) in.ifs > image.bin`, reseal `image.bin`, then
   concatenate the tail back).  **Untested** — verify the result with `verify`
   before you trust it.
3. **Keep the 496-byte zero tail** so the file is a whole number of 64 KiB blocks.
4. **A custom RCFS is not inside the .ifs.**  The RCFS is its own partition at
   region-0 `0xC00000`; write it there separately.  What rejects an unsigned or
   modified RCFS is `rfs_validator` *inside the IFS*, which is why step 1 is where
   that change belongs.

Caveats: this section is **untested in this repository** (we read the vendor file
and the tool's `--help`; the reseal numbers come from report 95's measured run),
the image's internal self-checks after a content change are unknown, and a
CRC-sealed image only boots on an **armed** unit.

Verify every result with the bootloader's own model, not by eye:
`89-crc_tool.py verify` over a full read-back must print `VERIFIED (CRC path)`.

## 4. Rehearse the record writer (optional, but it is the only rollback proof)

```bash
# sacrificial payload: 4 known bytes, created on the host and transferred (§1a)
printf '\xde\xad\xbe\xef' > /tmp/t.bin
tools/pbssh.sh 'cat > /tmp/t.bin' < /tmp/t.bin
# TEST_PAD is a sacrificial name; the resmgr is just a directory of records
tools/pbssh.sh 'dd if=/tmp/t.bin of=/dev/rpmb/TEST_PAD bs=4 count=1 seek=0 conv=notrunc'
tools/pbssh.sh 'dd if=/dev/rpmb/TEST_PAD bs=4 count=1 2>/dev/null' | od -An -tx1   # host-side compare
tools/pbssh.sh 'rm /dev/rpmb/TEST_PAD'
```

**What the write counter is.**  The eMMC's RPMB keeps a monotonic **32-bit write
counter**: every authenticated write must carry its current value and the device
increments it on success.  So each write "spends a unit" — but this is **replay
protection, not a budget**: 32 bits is ≈4.3 billion (the spare measured 4417).
It is a different thing from `BOOTCTR_FCTRY`, which is a 4-byte *record* the
loader uses as a boot budget of 50.

The rules that follow are only: **read the counter immediately before a write**
(a stale value is rejected), and **do not loop writes** (report 92 R4: a saturated
counter permanently write-protects the partition — and every write is a chance to
damage a record, which is what killed the spare).

## 5. Create the record

Exactly 4 bytes, `open(O_WRONLY)`, `lseek(0)`, write — **never** `>`, `>>` or
`O_TRUNC` (those are brick operations).  Create the 4 bytes **on the host**
(`printf` is not reliable on a stock image) and transfer them with `scp` or with
`cat >` as below:

```bash
printf '\x32\x00\x00\x00' > /tmp/bc.bin
tools/pbssh.sh 'cat > /tmp/bc.bin' < /tmp/bc.bin
tools/pbssh.sh 'ls -l /tmp/bc.bin'                     # size must be 4 before you write
tools/pbssh.sh 'dd if=/tmp/bc.bin of=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 seek=0 conv=notrunc'
# read back BOTH the value and the length (od runs on the host)
tools/pbssh.sh 'dd if=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 2>/dev/null' | od -An -tx1   # 32 00 00 00
tools/pbssh.sh 'ls -l /dev/rpmb/BOOTCTR_FCTRY'                                          # size 4
tools/pbssh.sh 'sync'
```

## 6. First factory boot

```bash
tools/pbssh.sh '/proc/boot/shutdown -S reboot'    # this OS has no plain `reboot`
```

Expected on return: OS up, **`BOOTCTR_FCTRY` = `31 00 00 00`** (one factory boot
consumed), `0x2801` and `0x2074` byte-identical, PPS `osregion` still
`requested=actual=0`.  Then top the counter back up and verify.

## 7. Getting to Linux afterwards

Once armed, flash the CRC-sealed chainloader through the ordinary pre-OS loop
(`DEVELOPMENT.md` §4).  The loader does not verify the payload it flashes, and a
CRC-sealed image on an armed unit boots the factory path.  To go back to the
stock OS, reflash the stock image and either reseal it or delete the record from
a booted TabletOS.

## 8. Failure modes — read before you write

| what you did | what you get |
|---|---|
| made the IFS CRC-valid, **rebooted**, then created the record | the boot between the two fails both verifies → halt (retry branch) |
| record created but the IFS still has `os_type = 4` | factory path CRC-gate fails → halt |
| wrong value / wrong length / a damaged or shadowing record | selector 2 → halt |
| the loader's own decrement write fails | **`0x2F` blink (LED 1-4)** and it never returns — there is no "keep booting on failure" path (report 154 §Q2) |
| the record store unusable, or one of its blocks unreadable | **`0x5B` blink (LED 1-2-2)** — the mount reads **every** block (report 154) |

**The blink code is a diagnostic, not decoration** (report 153): the red LED is a
FAN5702 I²C driver, and codes are emitted bit-serially (`1` = on/off 20 units,
`0` = off 40, end = off 200).  **1-2-2 = `0x5B` = NV-record / RPMB subsystem
unusable**; **1-4 = `0x2F` = a record write failure**.  The emitter never returns
and keeps the watchdog fed, so the unit neither resets nor presents USB — the
pre-OS flash loop cannot reach it, and the documented recovery is the eMMC route.
That is the state one unit is in now (report 149).

## 8a. Which steps are tested, and which are not

| step | status |
|---|---|
| §1 host setup (`ip addr add`, `door-hold.sh`) | **run** (on the reference; the `sudo`/`pkexec` variants are equivalent) |
| §2 read-only checks | **run** on the reference **pre-flash**: `os_type` 4, CRC `0x8D46FB4A`, IFS `75be24e8…`, `0x2040` absent |
| §3(a) reseal + the two-sector write | **not re-run**; the procedure measured in report `95` §2–§3.  The tool's CLI was read, not exercised |
| §3(b) pre-OS flash | **run** many times (every kernel image this project has flashed) |
| §3(c) preparing a bb10mt IFS | **not run** — the file structure above is measured, the procedure is not |
| §4 `TEST_PAD` rehearsal | **run** on the spare (created `de ad be ef`, read back, deleted) |
| §5 record creation | **run by the operator** in session 21 (`32 00 00 00`, report 160) |
| §6 first factory boot / `32 → 31` | **run**: the unit boots the factory path and the counter moves (`50 → 49 → 47 → 49 → 50`, report 160) |
| the `0x2040` range and the `L ≤ 172` block budget | **static** ([P] report `154` §Q3/§Q5) |
| the `/dev/trustzone` alternative in the header | **tried and blocked** (report 157: the stock resmgr segfaults in libc on `0xC0280501`) |

Anything marked *not run* is an untested procedure for this repository's purposes
until a session exercises it on a unit it can lose.

## 9. Rollback

* **While a rooted TabletOS still boots:** `rm /dev/rpmb/BOOTCTR_FCTRY` returns
  the selector to 0 (product path), and the resealed bytes must be restored
  (keep the original image — the vendor `.ifs` is a byte-exact copy of the stock
  one).  Both are needed: the signature only validates the original content.
* **Once the only bootable thing is our Linux payload:** there is no
  `/dev/rpmb`, so the counter can only run down and the switch is permanent.
  Keep the UFS image and the stock IFS as the way back.

## 10. Appendix — host-specific rules (one operator's machine, not the norm)

These explain why step 2 is sometimes already done, and why it can silently *not*
be done.  None of them is required, and none should be assumed by anyone else:

* a **NetworkManager connection** (here named `pb-rndis`) with
  `ipv4.method manual`, `ipv4.addresses 169.254.0.2/16` and
  `connection.interface-name` bound to **one port's** interface name — on any
  other port it simply does not apply, so the door times out.  Fix with
  `nmcli con modify … connection.interface-name <current>`, or do step 2 by hand;
* `/etc/udev/rules.d/99-playbook-to-vm.rules` can auto-attach `0fca:8010` to a
  **QEMU VM**, taking the device away from the host entirely;
* `/etc/udev/rules.d/99-playbook-bootrom.rules` only sets `MODE=0666` on
  `0fca:0001`/`0fca:8001` (the bootrom/loader nodes).
