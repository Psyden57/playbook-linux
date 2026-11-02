# 137 — The hd3 "new install": the setup marker and the PPS race

> **Superseded by report 138 (newer).**  The recreated filesystem was
> **reformatted to the full partition size and reseeded from `/base`** by the
> boot (`rootfs.sh`'s format+seed path); the pps-race hypothesis below is not
> the cause of the "new install".  The observations here (settings, appdata,
> mount points, the UFS/tar comparison, `dresize`) remain valid; the causal
> section is withdrawn.

**Session 17 (absolute), Linux-era session 11 — continuation.  2026-10-30.**
Status: **mechanism identified; the trigger is a startup race that is not yet
observed directly.**  No bench action was taken for this report — everything is
read-only: the live unit over the door, the `hd3` tar backup, and the exact
2 GiB image that was written to `hd3` (sha256 `96e14d47…cc213f`).

This supersedes the first draft of this report, which blamed the missing
`/base` mount point.  That was **wrong**, and the reason matters:

* `/base/scripts/startup.sh` → `filesystem_common()` **recreates every
  `/base` symlink itself** (`ln -Pfs /base/etc /etc`, …) on every boot;
* `/base/scripts/rootfs.sh` checks `/.rootfs` **first**; it is present in the
  image (inode 1304, 1 byte), so there is no reformat/reseed;
* the IFS mounts `/base` before `startup.sh` runs, so the recreated image's
  missing `base` directory is not fatal.

The measured differences (no `base`/`dev`/`pps`/`proc`/`tmp`/`BlackBerry`
entries in the image; `etc` restored as an empty file) are real but **benign**
for the boot, because `startup.sh` repairs the symlinks and the mount points are
created/mounted by the boot chain.  They are recorded here only so nobody
re-derives them as a cause.

---

## 1. The boot chain (corrected)

```
bootrom -> RIM bootloader (signature checked by the bootrom)
        -> IFS: .script, devb/fs-qnx6, rfs_validator mounts /dev/hd1t179 at /base
        -> /base/scripts/startup.sh
             filesystem_common():
                 /base/scripts/rootfs.sh          (the /.rootfs reseed switch)
                 ln -Pfs /base/* -> /bin /etc ...  (every boot)
                 migrate_rootfs.sh ${OS_VERSION}   (version-gated)
                 . /radio/scripts/env.sh           (hd3; our hook)
             filesystem_encrypt()                  (fsencrypt setup/set on /)
             runlevel 0: ... filesystem pps simconfig ...
        -> /radio/scripts/env.sh: waits for runlevels 0,1,2, then
           /radio/scripts/post-boot.sh  (firewall, dev mode, ssh, symlinks,
           /accounts/devuser/startup.sh)
```

Verified live: `HDROOT="hd3"`, `/dev/hd1t179 on /base`, and `env.sh` really is
sourced from `filesystem_common` — it even overrides `check_sanity()` to
`return 0`.

---

## 2. The setup marker

`autorun` in `/pps/system/navigator/config`:

* **`autorun::0` = already set up.**
* **`autorun::1` = setup pending** (the navigator runs `autorun_app`, which is
  `sys.firstlaunch…`).

Only two places in `startup.sh` write it:

```sh
# launcher_start()
if [ "${FACTORY_MODE}" == "1" ]; then
        echo "autorun::0" >> "${nav_config}"      # factory mode
        touch /.factorymode
else
        if [ -e /.factorymode ]; then
                echo "autorun::1" >> "${nav_config}"
                rm /.factorymode
        fi
fi

# pps_common(), start)
CC_VAL=$(grep "^lang_countryCode:" "/pps/system/settings");
if [ -z "${CC_VAL}" ] || [ "${CC_VAL}" == "lang_countryCode::" ]; then
        echo "autorun::1" >> "/pps/system/navigator/config";
        rm -rf /accounts/1000/appdata/sys.firstlaunch*;
fi;
```

`FACTORY_MODE` comes from the NVRAM PPS mirror (`InProduction:b:true`) and
`/.factorymode` is absent on the image, so the only writer that can fire is the
`pps_common` check.

---

## 3. What the image and the live root contain

| item | live hd3 | hd3 tar | recreated 2 GiB image |
|---|---|---|---|
| `/.rootfs` (seed-done marker) | 1 byte | 1 byte | present, 1 byte (`\n`) |
| `/.rootfs.os.version` / `.pps.version` | 2.0.0.4869 | 2.0.0.4869 | 2.0.0.4869 |
| `autorun` in persistent navigator config | `autorun::0` | `autorun::0` | `autorun::0` |
| `settings@0.0.664` | `lang_countryCode::en_US` | `lang_countryCode::en_US` | `lang_countryCode::en_US`, uid 99, gid 0, 0770 |
| NVRAM mirror `InProduction` | `b:false` | `b:false` | same |
| NVRAM mirror `osregion` | MaxBootCount 0, Actual==Requested 0 | same | same |
| `/accounts/1000` top level | 8 dirs | same 8 dirs | same |
| `/accounts/1000/appdata/sys.firstlaunch*` | absent | absent | absent |

So the setup state was **not** in a file.  The image boots with
`autorun::0`, and `pps_common` must have appended `autorun::1` at runtime.

The `etc` extraction anomaly is real (`etc` is an empty regular file in the
image, while the tar records `-> /base/etc`) but `ln -Pfs` replaces it at boot.

---

## 3a. The restored UFS (`PB-4869-root.0.4.ufs`) comparison

Mounted read-only in analysis with `bb10mt qnx6 mount` (host FUSE), then
unmounted.  Key facts:

* Its `/.rootfs.os.version` is **`10.0.4.197`** (hex
  `31302e302e342e3139370a`), a *newer* OS root than the unit's `/base`
  (`/base/etc/os.version` = `2.0.0.4869`).  The operator used a 10.0.4.197 UFS
  because the 4869 native UFS was awkward to shrink with the bb10mt tools.
* The **live root is that same UFS after its first boot**: `migrate_rootfs.sh`
  compared `OS_VERSION` (2.0.0.4869) with the RW version (10.0.4.197), saw them
  unequal, ran the migrations, and **rewrote `/.rootfs.os.version` and
  `/.rootfs.pps.version` to 2.0.0.4869**.  The tar was taken later, so it says
  2.0.0.4869.
* `PB-4869-root.0.4.ufs` itself is a **portable compact container**: the same
  2.55 GB image flashes a 16, 32 or 64 GB unit, and `bb10mt` sizes the QNX6
  filesystem to the target partition.  It is not a raw filesystem image and
  cannot be `dd`'d as a small FS; only the flasher expands it.  Its **mounted
  tree is ~298 MB apparent**, so it can serve as a *source* for a small FS — but
  only through a copy that preserves QNX ownership (the FUSE mount + host tar
  preserves numeric ids; QNX ACLs are not exposed).
* The factory UFS has an **unversioned** PPS store (`var/pps/system/settings`,
  `lang_countryCode::en_US`) and a real, empty **`etc/` directory**.  The first
  boot's `pps -C` converts the store to `system@0.0.775/settings@0.0.664`, and
  `filesystem_common` replaces `etc/` with the `-> /base/etc` symlink.
* Inventory differences (1,016 UFS-only vs 2,528 tar-only names) are dominated
  by that migration and by runtime state: old `apps/.0.*` directories, the
  unversioned PPS tree, logs, and boot-created mount points.  They are **not**
  evidence that the tar missed data.
* `accounts/devuser/startup.sh` is 4490 B / sha256 `c265cfbb…` in both.

So the UFS confirms the tar is the **migrated live root**, not the factory
root; it does not reveal a missing file.

**`dresize` (rootfs.sh) only shrinks.**  Its usage/strings say *"This tool only
supports shrinking a file system. (%u => %u)"*, and it takes no size argument:
`dresize -v1 /dev/hd3` compares the filesystem with the device and, on a device
that is larger (a 2 GiB FS in the 58 GB hd3 partition), errors instead of
growing.  So the boot does **not** undo the 2 GiB filesystem.  It also means the
shrink only frees space if the *partition* is changed (MCT) — a small FS inside
a 58 GB partition is still a 58 GB partition.

**The tar's one malformed member.**  `bsdtar`/libarchive (and Ark) reject
`./root/.profile` — a 50-byte file written in the **old GNU sparse** format
(`tar -cSf`, `-S` = sparse): "Malformed sparse map data".  GNU tar, Python
`tarfile`, and the device's tar read it.  This is not broad corruption, but a
fresh backup should be made **without `-S`** and verified with `bsdtar` before
it is trusted for another restore.

---

## 4. Why the check can fail: `pps -l 2` loads asynchronously

`pps_common()` does:

```sh
rm -rf /pps
pps -U 99:0 -C -t 300000 -p /var/pps -l 2
WAITFOR "pps" /pps 90                 # waits for the /pps mount, not objects
migrate_pps.sh ${OS_VERSION}
CC_VAL=$(grep "^lang_countryCode:" "/pps/system/settings")
```

The pps server's own usage text (from `/base/usr/sbin/pps`) says:

```
 -l 2        Load directories and objects at startup.
```

The loading is done by `persist_thread`/`persist_queue`, i.e. **asynchronously**.
`WAITFOR` only waits for the mount point.  On a normal boot the store is loaded
by the time the `grep` runs; on a **first boot of a freshly written 2 GiB image
inside a 58 GB partition**, `rootfs.sh` runs `dresize -v1 /dev/hd3` first and the
whole filesystem is cold, so the loader can still be behind when the `grep`
runs.  An empty result appends `autorun::1` and deletes the firstlaunch appdata
— the observed "brand-new install".

This is the strongest explanation that fits every measurement: the data is
correct, the config starts at 0, and the only writer is a check on state that
is loaded asynchronously.

**It is not yet observed directly.**  The failed boot's logs are gone (the UFS
was restored).  The confirmation is a retry that captures the check.

---

## 5. Retry / verification plan

1. **Take a fresh backup without `-S`** (`tar -cf`, not `-cSf`), and verify it
   with `bsdtar -tf` on the host (it must return 0).  The existing tar is fine
   for GNU tar but has one old-GNU sparse member that libarchive rejects, and
   the image built from it lost the `etc` symlink — do not build another image
   from it without checking.
2. **Verify the extracted root by listing, not only by hashing files**:
   top-level types and symlink targets, `/.rootfs` (1 byte), both version files,
   `settings@0.0.664` (en_US), and `etc` as a symlink.  The Python QNX6 reader
   used for this report can do it offline from the image.
3. **Capture the `pps_common` check.**  Two ways, in order of preference:
   * a **serial console** on the unit (UART3, 115200) — `startup.sh` prints its
     progress and errors there, including the PPS/migration output;
   * otherwise enable `CONFIG_QNX6FS_FS=y` in our Linux kernel (it is off
     today), boot that image after the failed TabletOS boot (region 0 only, hd3
     untouched), and mount the recreated hd3 **read-only** to read
     `var/pps/system@0.0.775/navigator@0.0.775/config@89.0.660`,
     `var/pps/system@0.0.775/settings@0.0.664`, `var/log/migrate/*` and
     `var/log/boot`.  This is the read-only-mount policy question; it needs the
     operator's explicit approval.
4. **Remove the first-boot cost.**  The 2 GiB→58 GB `dresize` errors (it only
   shrinks) but the cold filesystem can still be slower; either way, check
   `dresize`'s output on the retry.  Do not expect a smaller FS to free space
   without an MCT/partition change.
5. Only then decide whether the shrink is safe to keep.

If the race is confirmed, the durable fix is **not** on hd3 (the check is in the
signed `/base` OS): it is either to give the pps loader time (an operator-side
change to `/base/scripts/startup.sh`, out of scope) or to make the first boot
fast enough that the race is not lost.

---

## 6. Residual uncertainty

* The race is inferred from the code and the `-l 2` semantics; it was not
  captured on the failed boot.
* Whether the pps loader was slow, or failed on one store object, is unknown.
  The store is a live backup (the tar warned about files changing while read,
  e.g. samba); a malformed object could also stop the loader.
* `fsencrypt` re-runs `-c setup` on the new filesystem (`/var/keymgr`,
  `/var/cryptoservice`, `/var/etc/netsecure`).  The security state was not
  compared; it is a second candidate if the pps timing is ruled out.
* `autorun::0` on the live unit means the restored factory UFS is set up — the
  pre-shrink and post-restore roots agree, which is why the unit is usable now.
* The UFS comparison (§3a) confirms the tar is the migrated live root; it does
  not distinguish "pps load was late" from "pps load failed on one object".
  `sloginfo`/`/var/log` from the failed boot are gone (the UFS was restored).

---

## 7. Bench

None.  Read-only: `ls`/`mount`/`df`/`cat` over the door, `dd if=/dev/hd3` for
the superblock, host-side parsing of `local/backups/newfs-2g.img`, `tar -tvf`
of `local/backups/hd3-2026-10-29.tar`, `bsdtar -tvf` for the libarchive check,
`strings` of `/base/usr/sbin/pps` and `/base/bin/dresize`, and a read-only FUSE
mount of `PB-4869-root.0.4.ufs` (mounted and unmounted; nothing written).
The unit remains on the restored 58 GB UFS, sealed IFS verified,
`BOOTCTR_FCTRY = 0x32`.
