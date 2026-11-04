# Handoff — session 26 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6`.  Absolute 25 = Linux-era 19.  See `docs/sessions/NUMBERING.md`.

## 0. Where it stands, in one block

**The unit is held in the ramloader (`0fca:8001`) with the first BB10 test set
on media.**  The reference (`0x50175127`) is the only working unit.

* **On media:** the BB10 10.3.03.2049 set — IFS `z10-win.sealed.ifs`
  (8,555,080 B, `VERIFIED (CRC path)`, sha256 `50e1c04a…`), the edited 10.3
  `/base` (sha256 `88711f9e…`), and the 10.3 UFS with `autorun::0` (sha256
  `8bc7c13f…`).
* **The image verified, BB10 did not run.**  The loader's `info` reads
  `boot_mode = factory`, `factory_count = 45` (it was 50), so the CRC path
  accepted the image and the loader jumped five times; no splash, no USB, and
  the unit re-entered the bootrom each time.  The container/IFS/RCFS chain is
  sound; the stall is inside BB10 (kernel/board support for the OMAP4430).
* **Way back packed and verified, NOT flashed:** `local/work/back-1917.signed`
  (1,104,544,336 B) = our 1917 IFS + the edited 1917 RCFS + the vendor `.5.ufs`
  with `autorun::0`.  Flashing it from the ramloader (no `--from-bootrom`)
  restores TabletOS, and its boot tops `BOOTCTR_FCTRY` to `0x32`.
* **Factory budget: 45 (`0x2D`).**  Only a TabletOS boot refills it.
* **No RPMB writes by an agent, ever.**  The operator's
  `/accounts/devuser/startup.sh` top-up was on the 1917 UFS and is not on the
  BB10 UFS.

## 1. The one thing to do next

**Restore the unit first.**  From the ramloader (it is there now):

```bash
python3 tools/bbusb.py flash --signed local/work/back-1917.signed \
    --password 0000 --apply          # no --from-bootrom: it is in the loader
```

Then verify the unit: `tools/door-hold.sh`, `PB_USER=devuser tools/pbssh.sh
'uname -a; mount | grep base; cat /base/etc/os.version;
tail -5 /accounts/devuser/.bootctr.log'` — expect 2.1.0.1917 and `wrote 0x32,
readback ok`.  If the door does not come up, check ports 22/4455/80/443 before
concluding anything.

**Then the BB10 compatibility control:** the reporter says 10.0.9.1103 ran on a
PlayBook.  Build that set for this unit — its IFS is `os_type 4`, so reseal it
(`tools/89-crc_tool.py reseal … --type 6`) and repack V1+V2 with the PlayBook
deltas exactly as in `BB10-IMAGE-GUIDE.md` §2/§5.  If 10.0.9.1103 also fails to
run with the same "verified, no OS" symptom, the problem is not the 10.3 branch
backport but the kernel/board-support gap; if it runs, diff its IFS/`proc/boot`
and `/base` against the 10.3 set to find what 10.3 dropped that matters beyond
`ifs_variables.sh`.

## 2. What session 25 established (read these)

* **`BB10-IMAGE-GUIDE.md`** — the repeatable procedure: IFS build → splice →
  reseal; `/base` RCFS in-place LZO edits, `.pyc` editing, the verifier shim;
  the UFS `autorun` change; packing, flashing, verifying, way back.
* **`BB10-PORT.md` §4b/§4c** — the image analysis: formats, the Winchester
  branch, the `/base` edits, the built set.
* **Reports 171/172** — the IFS compression format (`startup_header` from
  `mkxfs` DWARF, the chunked NRV2B payload, the patch rules).
* **New tools:** `tools/rcfs_extract.py` (read a BB10 `/base`),
  `tools/ucl_nrv2b.py` (NRV2B codec), `tools/bb10_ifs.py` (splice + verify),
  `tools/rfs_rcfs_shim.c` (mount the RCFS without the verifier),
  `ifs_build.py --add` (new IFS files).
* **The `/base` edits in the test set:** deckard
  `platform/blacklist.pyc` `update`/`write`/`_write` are no-ops (the NVRAM
  `0x2819` writer); the seed `autorun::0`; the startup hook
  `/accounts/devuser/startup.sh &` after the final runlevel.
* **UFS:** `autorun::0` in `var/pps/system/navigator/config`.

## 3. Traps added in session 25 (full list in `KNOWN-ISSUES.md`)

* **78** — `bb10mt rcfs extract` aborts the tree on one bad chunk; use
  `rcfs_extract.py`.
* **79** — any `/base` edit invalidates the RCFS verifier; replace
  `rfs_validator` with `rfs_rcfs_shim`.
* **The counter is the diagnostic.**  A decrement means the loader verified and
  jumped (the OS failed); no decrement means the loader rejected the image.
  Read it with `bbusb.py info --from-bootrom --password 0000 --keep-loader
  --json` (`boot_mode.factory_count`).
* **`info` needs the device password** — `0000` on this unit, sent once, never
  retried.  `--keep-loader` stops it rebooting (and spending a count).
* **A flash reset boots media** and spends a count; `--keep-loader` on `info`
  avoids that.
* **The IFS slot is 11 MiB**; BB10 imagefs is 13.7 MB and must be compressed
  (the splice does it).  `bb10_ifs.py verify` must pass.
* **The RCFS slot is tight** — recompress at `lzo1x_999` and check
  `table+chunks ≤ next_offset - inode.offset` before writing.
* **Python 3.2 `.pyc`** — build a portable 3.2.6 (`-no-pie`), 12-byte pyc
  header, `return`→`pass` fixups after `uncompyle6`.
* **The 10.3 UFS `.seed` has no autorun**; the second copy is in the RCFS seed.

## 4. Bench and commands

* Door: `169.254.0.1`; host `ip addr add 169.254.0.2/16 dev <iface>`;
  `tools/door-hold.sh`; `PB_USER=devuser tools/pbssh.sh …`;
  `tools/pbscp.sh` (directories need `-O`).
* USB tools **as the user, never `pkexec`** (udev uaccess; root lacks pyusb; a
  killed `pkexec` leaves an orphan).
* `bbusb.py flash --from-bootrom` opens for 10 s — arm
  `while ! lsusb | grep -q 0fca:0001; do sleep 1; done` first.
* FUSE images: never `fusermount -z`; wait for terminals/editors.
* The 1917 OS clock is 2014.

## 5. Working rules

* **After every console/USB command, read the tail of the log** before acting.
* **Write the durable fact into the repository in the same turn.**
* **Do not budget work against a context window**; the operator decides when to
  stop.
* **Static RE only for subagents.**
* **Never present a guess as a fact.**
* **Update the docs as you go**; `STATE.md` and `KNOWN-ISSUES.md` go stale
  first.
* Publishing is separate; see `REPO-MANAGEMENT.md`.

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 26 (absolute; Linux-era 20)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md,
BB10-PORT.md, BB10-IMAGE-GUIDE.md, ROADMAP.md item 8, KNOWN-ISSUES.md
(traps 72–79), reports 171/172, DECISIONS.md §22/§23.

STATE: ONE WORKING UNIT, the reference (0x50175127), armed and sealed.
It is held in the RAMLOADER (0fca:8001) with the first BB10 10.3.03.2049 set
on media (IFS 8,555,080 B, VERIFIED CRC path; edited /base; 10.3 UFS with
autorun::0).  The loader verified the image and jumped five times
(factory_count 45, down from 50) but BB10 did not run: no splash, no USB.
The way back is packed: local/work/back-1917.signed (1,104,544,336 B), not
flashed.

FIRST: restore the unit from the ramloader with
  python3 tools/bbusb.py flash --signed local/work/back-1917.signed \
      --password 0000 --apply
(no --from-bootrom), then verify PlayBook OS 2.1.0.1917 and that the top-up
log reads 0x32.  The operator handles flashing if they prefer; do not flash
anything without their go-ahead.

THEN: continue BB10 offline.  The repeatable editing procedure is
docs/project/BB10-IMAGE-GUIDE.md.  The next experiment is the 10.0.9.1103
winchester set (reported to run on the PlayBook), resealed to os_type 6 + CRC
and repacked V1+V2 — as a compatibility control against 10.3.

TOOLS: bbusb.py (info/flash/reset-osregion/pack/unpack/split/parse),
rcfs_extract.py, ucl_nrv2b.py, bb10_ifs.py, ifs_build.py (--add/--replace),
89-crc_tool.py, rfs_rcfs_shim.c, bb10mt qnx6 mount,
PB_USER=devuser tools/pbssh.sh, tools/pbscp.sh.

BENCH: door = 169.254.0.1 (host: ip addr add 169.254.0.2/16 dev <iface>);
tools/door-hold.sh.  Run the USB tools as the USER — never pkexec.  The
device password is 0000, sent once, never retried.  bbusb.py info
--from-bootrom opens for 10 s; --keep-loader stops it rebooting.  No RPMB
writes by an agent, ever.

SAFETY: read SAFETY.md first.  One variable per flash; the factory counter
only refills on a TabletOS boot.  Keep the way back verified.

SUBAGENTS: static RE only, no bench.  Skeleton first, exact paths, an explicit
"stop when answered".

UPDATE THE DOCS as you go and commit: BB10-PORT.md, BB10-IMAGE-GUIDE.md,
STATE.md, HANDOFF.md, ROADMAP.md, KNOWN-ISSUES.md, DECISIONS.md and the
session note.
```
