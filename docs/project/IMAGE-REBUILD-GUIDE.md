# Image rebuild and flash guide — any `.ifs` + `/base` (+ UFS)

How to build a modified PlayBook OS image, edit its `/base`, and flash the set
to the reference unit.  Written from the session-24 run that booted the
modified 2.1.0.1917 image (reports 169/170); the commands below are the ones
that were actually run.

**Scope.** The IFS is rebuilt from the stock tree with our own imagefs emitter
(`tools/ifs_build.py`) and resealed for the factory/CRC path.  `/base` is the
RCFS component; it is edited as a plain QNX6 filesystem and is no longer
gated by `rfs_validator` because the IFS carries the mount shim.  The UFS is
the root filesystem; it is normally the vendor component.  For 2.1.0.1917,
`PlayBook_2.1.0.1917.5.ufs` is the vendor image **plus the operator's
`autorun::0` change** — not an unmodified stock image.

> **One variable per flash**, and `SAFETY.md` comes first (this guide assumes it
> has been read).  The only thing that should change the OS image's
> **descriptor** — the `TOSMetrics` block at `image+0x2C`, whose
> `LoadStart`/`LoadEnd`/`os_type` fields the bootloader reads — is the reseal
> tool (`os_type` and `LoadEnd`).  Do not hand-edit it.

## 0. Inputs and deltas

For a vendor set (`PlayBook_<version>.{0.mbr,1.rcfs,2.mbr,3.sig,4.ifs,5.ufs}`)
the destination deltas are the numbers in the vendor `.lst` (and in
`bbusb.py unpack`'s `.lst`):

| component | delta (64 KiB blocks) | media offset |
|---|---|---|
| `.2.mbr` | 0 | `0x0` |
| `.3.sig` | 15 | `0xF0000` |
| `.4.ifs` | 16 | `0x100000` |
| `.1.rcfs` | 192 | `0xC00000` |
| `.0.mbr` | 8192 | `0x20000000` (end of region 0) |
| `.5.ufs` | 16640 | `0x41000000` |

The loader uses the **V1 `qcfp`** run record with region-relative offsets; a
container from `bbusb.py pack -v 1,2 -s` carries both V1 and V2 and is the
form this unit accepts (trap 55).

Toolchain: `. tools/env.sh` (QNX 6.6 SDP gives `dumpifs`/`mkifs`/`ntoarmv7-gcc`).

## 1. Build the IFS

```bash
. tools/env.sh
STOCK=/path/PlayBook_<version>.4.ifs
TREE=local/work/ifs-tree
tools/ifs_extract.sh "$STOCK" "$TREE"          # 72 files / 11 dirs for 1917
tools/ifs_build.py listing "$STOCK" --out local/work/stock.lst   # dumpifs -vv
tools/ifs_build.py build --tree "$TREE" --listing local/work/stock.lst \
                         --stock "$STOCK" --out local/work/new.ifs \
                         [--replace proc/boot/rfs_validator=/path/to/shim]
tools/ifs_build.py check --image local/work/new.ifs --listing local/work/stock.lst
```

* `build` writes `image_header + dirents + data + trailer` and appends it to
  the stock `0x0..0x44910` prefix.  It clears the startup header's compression
  bits (an uncompressed stream must say `COMPRESS_NONE` or `dumpifs` and the
  loader will not treat it as compressed) and recomputes the startup checksum
  that the flag change invalidates.
* A **smaller** replacement keeps its reference slot; only a size increase
  repacks (on mkifs' address-space rule).
* `check` is the real acceptance test: QNX's own `dumpifs` must list the same
  dirents, sizes and modes and print a `Checksums:` line.

The build output is still `os_type 4` (the stock descriptor).  **Reseal it**:

```bash
python3 tools/89-crc_tool.py reseal local/work/new.ifs local/work/new-sealed.ifs \
    --image-off 0 --length auto --append-os-trailer --type 6
```

* `--length auto` appends the 8-byte OS trailer, patches `LoadEnd` at
  `image+0x08` (the field the descriptor's `load_end_ptr` points at), writes
  the end-8 cookie and the CRC over `[0, length-4)`.
* The command prints `RESULT: VERIFIED (CRC path)`; the `ECDSA trailer shape`
  lines are informational (the CRC path does not use the ECDSA material).
* The pre-seal image may be kept for debugging; only the **sealed** file is
  flashed.

## 2. Edit `/base` (the RCFS)

Work on a **copy**; never edit the vendor component.

```bash
cp /path/PlayBook_<version>.1.rcfs local/work/rcfs.rcfs
mkdir -p local/work/base-mnt
./local/vendor/bb10mt/bb10mt qnx6 mount -i local/work/rcfs.rcfs -m local/work/base-mnt
# edit .../base-mnt/scripts/startup.sh
# then, after every terminal/editor has left the mount:
fusermount -u local/work/base-mnt        # NEVER -z
```

The `/base` hook that runs our own boot commands is **one guarded call** in
`scripts/startup.sh`, after the last runlevel:

```sh
        start_runlevel FINAL;
        if [ -x /accounts/devuser/startup.sh ]; then
            /accounts/devuser/startup.sh &
        fi;
```

Why here: `main()` starts runlevels 0..2 and `FINAL`; after that the OS is up
and `/accounts` (on the UFS) is mounted.  The script is backgrounded because it
waits for `/dev/rpmb` and the symlink overlay itself.

The boot-time login is `devuser`; do not add sshd or root-account changes to
`/base`.

Verify the edit by diffing against a pristine mount (mount the vendor RCFS in a
second directory); the only hunk should be the block above.

## 3. Prepare the UFS

Use the **vendor UFS for the release**, with the operator's `autorun::0` change
(for 2.1.0.1917 that is `PlayBook_2.1.0.1917.5.ufs`).  The
`PlayBook_<version>_ROOT_V1.4.EXE` UFS is a rooted/branded image whose
`qconnDoor` does not complete the door handshake on this platform — SSH stays
closed (report 170 §3).  The vendor `PlayBook_<version>.5.ufs` does
authenticate.

Two things matter:

1. **Skip the setup wizard**: the image must have `autorun::0` in its
   provisioning files (the 2.1.0.1917 `.5.ufs` already carries that change: two
   occurrences of `autorun::0`, none of `autorun::1`).  Verify:
   ```bash
   grep -aoc 'autorun::0' PlayBook_<version>.5.ufs
   grep -aoc 'autorun::1' PlayBook_<version>.5.ufs
   ```
2. **Our boot script** goes at `/accounts/devuser/startup.sh` on the UFS.  It
   can be added after the first boot (as `devuser` over SSH), or before
   flashing by mounting the UFS image with `bb10mt qnx6 mount` and copying it
   in.  The `/base` hook is guarded, so its absence is harmless.

Do not add the script to the UFS unless you intend it to run; if a door/SSH
failure is being debugged, flash the clean UFS first and add the script after
the door works.

## 4. Build the container

List the components you are writing, with their deltas.  For a UFS-only update:

```bash
printf '%s=16640,0\n' /path/PlayBook_<version>.5.ufs > local/work/ufs.lst
python3 tools/bbusb.py pack -l local/work/ufs.lst -c local/work/ufs.signed -v 1,2 -s
```

For the full set (IFS + RCFS + UFS in one PreFlash):

```bash
printf '%s=16,0\n%s=192,0\n%s=16640,0\n' \
    "$PWD/local/work/new-sealed.ifs" "$PWD/local/work/rcfs.rcfs" \
    /path/PlayBook_<version>.5.ufs > local/work/set.lst
python3 tools/bbusb.py pack -l local/work/set.lst -c local/work/set.signed -v 1,2 -s
```

`-v 1,2` is required (this loader reads V1; the vendor form carries both), `-s`
appends the 560-byte `QNXH` signature, and sparse analysis is correct — the
loader expands each run to its partition (do **not** force `-f`).

Always inspect and dry-run first:

```bash
python3 tools/bbusb.py parse local/work/set.signed      # deltas, runs, signature
python3 tools/bbusb.py flash --signed local/work/set.signed   # dry run
```

Every run must be inside the measured MCT extents; the tool prints `GATE
REJECT` and refuses otherwise.  A UFS-only container is roughly 700 MB; a full
IFS+RCFS+UFS set is roughly 1.1 GB.

## 5. Flash

The USB node carries a `user:psyden` ACL.  **Run the flasher as the user; do
not use `pkexec`** (root lacks the user's `pyusb`, and a killed `pkexec` leaves
a root orphan holding the interface).  `--password` has no default; the proven
value for this unit is `0000`, sent once.

```bash
# Arm a wait for a FRESH bootrom, then flash.  The tool's own open window is 10 s.
while ! lsusb | grep -q '0fca:0001'; do sleep 1; done
python3 tools/bbusb.py flash --signed local/work/set.signed \
    --from-bootrom --password 0000 --apply
```

* Put the unit into BootROM/CFP download mode (`0fca:0001`) **after** starting
  the armed command; the unit's bootrom window is short.
* Success: the last line is `done`, the payload count reaches the total,
  `0x40F9 signature -> cmd 0x4006`, `COMPLETE 0x40C0 -> cmd 0x4006`, exit 0.
* A transient `loader exit: device released` can appear mid-write and recover;
  only a failure before PreFlash means nothing was written.
* The tool resets the unit out of the loader afterwards.  The first boot takes
  2–4 minutes; check TCP ports, not ICMP.

## 6. Verify (read-only)

```bash
# door (BerryShell authenticates qconn, uploads the key, starts sshd)
tools/door-hold.sh &          # or run it in a supervisor
for i in $(seq 1 30); do timeout 2 bash -c '</dev/tcp/169.254.0.1/22' && break; sleep 2; done

PB_USER=devuser tools/pbssh.sh '
  uname -a
  mount | grep -E "base|/dev/hd3"
  cat /base/etc/os.version
  ls -l /proc/boot/rfs_validator /accounts/devuser/startup.sh
  tail -20 /accounts/devuser/.bootctr.log
'
```

Expected for the 2.1.0.1917 reference:

* `uname` = `QNX localhost 6.6.0 2014/03/19… OMAP4430_ES2.2_HS_Winchester_Rev:07 armle`;
* `/dev/hd1t179 on /base type qnx6`, `/dev/hd3 on / type qnx6`;
* `/base/etc/os.version` = `2.1.0.1917`;
* `/proc/boot/rfs_validator` = **8,759** B (the shim; stock is 50,554);
* the top-up log ends with `bootctr: wrote 0x32, readback ok (was …)`.

The 1917 OS logs in as **`devuser`**; root is denied by `qconnDoor`.  The clock
is the build date (no RTC), so log timestamps are 2014.

## 7. Way back

Keep the previous release's components and the same procedure.  For the
session-24 reference the way back is the sealed 4869 IFS
(`local/images/ifs0.sealed.bin`), the 4869 RCFS
(`PB-4869-root.0.1.rcfs`) and the 4869 UFS, in one container:

```
printf '%s=16,0\n%s=192,0\n%s=16640,0\n' \
    "$PWD/local/images/ifs0.sealed.bin" "$PWD/path/PB-4869-root.0.1.rcfs" \
    "$PWD/path/PB-4869-root.0.4.ufs" > local/work/back.lst
python3 tools/bbusb.py pack -l local/work/back.lst -c local/work/back.signed -v 1,2 -s
python3 tools/bbusb.py flash --signed local/work/back.signed --from-bootrom --password 0000 --apply
```

## 8. Pitfalls (learned the hard way)

| pitfall | rule |
|---|---|
| `pkexec` on the USB tools | user has the ACL; root lacks `pyusb`; a killed `pkexec` leaves an orphan holding the interface |
| lazy FUSE unmount | wait for terminals/editors (`fuser -vm DIR`), then `fusermount -u`; never `-z` |
| `bbusb.py flash --from-bootrom` window | 10 s; arm the `lsusb` wait first |
| `bb10mt qnx6 fsck` on the RCFS | it cannot open a QNX6 image with an MBR at `0x0`; verify by mounting |
| UFS `fsck` warnings | the sparse extract has `0xFF` holes; pristine and edited report the same 140 lines |
| setup wizard | `autorun::0`; use the vendor `.5.ufs` with that change, not the ROOT V1.4 EXE |
| mixed IFS/RCFS versions | ship the matching pair; a 4869 `/base` under a 1917 IFS mixes libraries |
| UFS plus IFS/RCFS in one flash | fine when they are one container (one PreFlash); do not chain separate PreFlashes and expect the first write to survive |

## 9. One-page checklist

1. `. tools/env.sh`
2. `ifs_extract.sh` + `ifs_build.py listing`
3. `ifs_build.py build` (+ `--replace`) → `ifs_build.py check`
4. `89-crc_tool.py reseal --length auto --append-os-trailer --type 6`
5. copy the RCFS, `bb10mt qnx6 mount`, add the guarded `/accounts/devuser/startup.sh` call, `fusermount -u`
6. vendor UFS: `autorun::0` (operator change); add `/accounts/devuser/startup.sh` before or after
7. `bbusb.py pack -l … -c … -v 1,2 -s` → `parse` → dry-run
8. arm the `0fca:0001` wait → `bbusb.py flash --signed … --from-bootrom --password 0000 --apply` (user, no `pkexec`)
9. boot; `door-hold`; `PB_USER=devuser tools/pbssh.sh` checks; read `.bootctr.log`
10. record the exact components, hashes and the way back
