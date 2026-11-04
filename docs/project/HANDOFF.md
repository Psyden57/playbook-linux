# Handoff — session 26 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§5).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 26 = Linux-era 20**.  See
`docs/sessions/NUMBERING.md`.

**The job is the Linux bring-up again.**  The BB10 port and the PlayBook OS
image (IFS/RCFS) work are **parked by the operator** for someone else to
follow; their documents stay in the tree (`BB10-PORT.md`,
`BB10-IMAGE-GUIDE.md`, `IFS-REBUILD.md`, `IMAGE-REBUILD-GUIDE.md`) but they are
**not** this session's work.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 6.15.11** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs PID 1/console on USB, and a read-only root in media region 1.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain — **the panel shows a clean, stable
image** (DSS/DSI, the TC358764 bridge, the Hitachi TCON table, the LM3532
backlight) — and a **32 MiB ext4 root in region 1** at raw `0x29000000` whose
`/init` runs (report 140).  The kernel patch is current: **5 137 lines /
34 files** (`kernel/patches/README.md`).

**Last known-good image:** **`0x0D1DA0D3`**,
`local/images/kernel-0x0D1DA0D3-noirqerr.img` (session 18).  It has 0 DSI CIO
errors, no duplicate/WARN/taint, `fck 96000000`, fbcon up, region-1 root
mounted (`local/logs/boot-20261030-084331.log`, report 142).  **Build from
this.**  (Image CRC convention: `zlib.crc32(image[0:-4])` in the container's
last word; a rebuild with the same CRC has usually failed to link — trap 39.)

**The single current blocker: the Linux `BOOTCTR_FCTRY` top-up.**  Every
factory/CRC boot spends one unit and only a TabletOS boot puts one back; once
Linux is the normal OS nothing repairs it.  The caller is solved (**identity
physical addresses below `0xB0000000`**, report 147) and the whole seal ABI is
accepted (`0x12`/`0x31`/`0x33`/`0x2F` return 0), but the monitor's **`0x2F`
transform does not reproduce the device's own block** (reports 148/156).  The
first write-back — a *captured, genuine* block, accepted by the device — left
the spare in the `0x5B` LED loop (report 149).  **No agent ever writes RPMB**
(`SAFETY.md` §6); QNX's `/dev/rpmb` is the only proven writer.

**Still open:** the real userland in the region-1 root, and the `hd3` cut-over
(overwrite the MCT type-`0x1C` partition, raw `0x49000000`, no MCT edit) —
gated on the top-up.  Packaging (postmarketOS-style, item 7) is untouched.

## 0a. The physical state at session-25 end — restore this first

The unit was used for a BB10 test and is **held in the ramloader**
(`0fca:8001`), with the BB10 10.3 set on media and the factory counter at
**45 (`0x2D`)**.  The way back is packed and verified:

```bash
# from the ramloader — NO --from-bootrom
python3 tools/bbusb.py flash --signed local/work/back-1917.signed \
    --password 0000 --apply
```

That restores PlayBook OS 2.1.0.1917 (our 1917 IFS + edited 1917 RCFS + the
vendor `.5.ufs` with `autorun::0`), and its boot tops `BOOTCTR_FCTRY` back to
`0x32`.  Verify with `tools/door-hold.sh` and
`PB_USER=devuser tools/pbssh.sh 'uname -a; mount | grep base; tail -3
/accounts/devuser/.bootctr.log'`.  **Do not flash anything without the
operator's go-ahead.**

## 1. The one thing to do next

**Resume the Linux work** once the unit is back on 2.1.0.1917.  The Linux
build → flash → boot loop is `DEVELOPMENT.md` §1–§3; in short:

```bash
ZIMAGE=.../zImage DTB=.../omap4-winchester.dtb python3 tools/build_kernel_image.py
nohup python3 tools/pre_os_ifs_flash.py --template-ifs boot/chainloader/kernel.img > /tmp/f.log 2>&1 &
sleep 3; tools/pbssh.sh '/proc/boot/shutdown -S reboot'
until grep -q 'done (' /tmp/f.log; do sleep 2; done; tail -2 /tmp/f.log
```

Then pick up the ordered list in `ROADMAP.md`:

1. **Item 6 (the blocker): reproduce the `0x2F` seal.**  The recapture and the
   `0x2E` oracle are done (report 156); what is missing is the transform.  This
   unblocks the top-up and the hd3 cut-over.
2. **Item 4:** userspace brightness control (small, independent).
3. **Item 5:** a real userland in the region-1 root, then the `hd3` cut-over
   (after item 6).
4. **Item 7:** packaging.

**If you want a single experiment first:** item 4 is self-contained and needs
no counter reasoning; item 6 is the one that changes what the unit can do.
The operator decides.

## 1a. Tools that make this cheap

* **`tools/pbssh.sh`** (door, `PB_USER=devuser` on 1917) and
  **`tools/pbcon.py`** (USB console); `tools/door-hold.sh` keeps the door up.
* **`tools/build_kernel_image.py`** — wraps `zImage` + DTB into a copy of the
  sealed IFS with the chainloader, prints the CRC.
* **`tools/pre_os_ifs_flash.py`** — the pre-OS `0xF7` region-0 write; it resets
  the loader itself.
* **`tools/pb_ring.py` / `pb_events.py`** — the DRAM console ring and the
  printk-free event log.
* **`tools/pbdsi`** (`qnx-dsiread.c`) — the TC358764 oracle over DSI from a
  running TabletOS; push it after every reboot.
* **`local/kernel-tree/drivers/misc/pbmon.c`** + `/dev/pbseal` /
  `/dev/pbunseal` — the monitor seal/unseal call for the `0x2F` work.
* `tools/env.sh` — both cross-toolchains; `tools/env.sh --check`.

## 2. Traps

Full text: `KNOWN-ISSUES.md` §2.  The ones that end sessions:

9. A write to a DSS target in module standby never completes; let `omapdrm`
   own the DSS.
16. Blocking I²C is the same hazard — fork with a bounded wait.
17. A stale door holder looks like `Connection refused`; kill by explicit pid.
24. `/dev/ttyACM0` is recreated per enumeration (`pkexec chmod 666` after every
   boot; if `pkexec` is denied, the operator runs `sudo chmod`).
26. `pmic-twl6030-ctl set_state vaux1 off` takes the unit down; read rails.
27. A flash cycle's precondition is **media running** (`0fca:8010`), not the
   ramloader; end with a loader reset.
28. Panel-chain writes outlive a warm reset (`SYS_RST <- 0x1f`; trap 28).
31. Non-burst DSI video mode is rejected at this pixel clock.
33. Our images no longer auto-reboot; a boot costs one counter unit.
39. A failed link leaves the previous image with the previous CRC.
43. `local/kernel-tree/.config` can point at a different initramfs tree.
45. The DISPC core clock is raised at probe time (96 MHz).
46. Reading DSS/DSI registers from userspace perturbs the picture.
47. MCT/loader block numbers are not raw offsets: `(block-0x15)*0x10000`.
49. eMMC writes are only as large as the writer makes them (≥64 KiB).
51. The ACM console is not a bulk transport.
54. A smaller hd3 filesystem is reformatted and reseeded at boot.
59. **Never `lseek()`+`read()` `/proc/<pid>/as`** — it reset the unit.
60–63. RPMB: the block is reversed; the 224-byte block is monitor-encrypted; the
   monitor reads identity addresses below `0xB0000000`; the transform is
   unreproduced.  **No agent writes RPMB.**

Session-25 additions relevant here: the unit may be in the ramloader, and the
factory counter is read with `bbusb.py info --from-bootrom --password 0000
--keep-loader --json` (`boot_mode.factory_count`).  The password is `0000`,
sent once, never retried.

## 3. Where things are in this workspace

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console commands | `docs/project/DEVELOPMENT.md` |
| host prerequisites, toolchains | `docs/project/SETUP.md` |
| board DTS and patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + exact config | `kernel/patches/`, `kernel/winchester.config` |
| chainloader source | `boot/chainloader/chainloader_kernel.S.in` |
| tools | `tools/` (see `tools/README.md`) |
| images, sealed IFS, backups | `local/images/` (gitignored) |
| QNX boot set for RE | `local/proprietary/dumped4869ifs/proc/boot/` |
| OMAP4430 TRM (text) | `hardware/swpu231ap.pdf.txt` |
| public/private repositories | `docs/project/REPO-MANAGEMENT.md` |

## 4. Working rules

* **After every console command, dump the log tail and read it.**
* **Write the durable fact into the repository in the same turn.**
* **Do not budget work against a context window**; the operator decides when to
  stop.
* **Static RE only for subagents** (one process owns the USB link).
* **Never present a guess as a fact.**  Mark inferred claims.
* **The whole-map diff is the highest-yield technique** — and ask the operator
  what the panel shows before building a theory.
* **Update the docs as you go**; `STATE.md` and `KNOWN-ISSUES.md` go stale
  first.
* Publishing is separate (`REPO-MANAGEMENT.md`, `tools/publish_public.sh`).

## 5. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 26 (absolute; Linux-era 20)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md,
ROADMAP.md items 4-7, KNOWN-ISSUES.md (traps 9-63), DEVELOPMENT.md,
DECISIONS.md (17-20), reports 139/140/142/147/148/156.

SCOPE: this session is the LINUX bring-up.  The BB10 port (BB10-PORT.md,
BB10-IMAGE-GUIDE.md) and the PlayBook OS image work (IFS-REBUILD.md,
IMAGE-REBUILD-GUIDE.md) are PARKED by the operator — do not work on them.

PHYSICAL STATE AT SESSION-25 END: the unit is held in the ramloader
(0fca:8001) with a BB10 set on media and the factory counter at 45 (0x2D).
FIRST restore the working set from the ramloader (no --from-bootrom):
  python3 tools/bbusb.py flash --signed local/work/back-1917.signed \
      --password 0000 --apply
Then verify PlayBook OS 2.1.0.1917 and that the top-up log reads 0x32.
Do not flash anything without the operator's go-ahead.

THEN resume Linux: build from the last known-good image
0x0D1DA0D3 (local/images/kernel-0x0D1DA0D3-noirqerr.img), use
tools/build_kernel_image.py and tools/pre_os_ifs_flash.py --template-ifs
(DEVELOPMENT.md §1-§3).  The ordered next work is ROADMAP item 6 (the
Linux BOOTCTR_FCTRY top-up: reproduce the monitor's 0x2F transform,
reports 148/156), then item 4 (brightness), then item 5 (real userland
and the hd3 cut-over), then item 7 (packaging).

BENCH: door = 169.254.0.1 (host: ip addr add 169.254.0.2/16 dev <iface>);
tools/door-hold.sh; PB_USER=devuser tools/pbssh.sh.  Run the USB tools as
the USER — never pkexec.  The device password is 0000, sent once, never
retried.  No RPMB writes by an agent, ever.

SAFETY: read SAFETY.md first.  One variable per boot; budget boots against
BOOTCTR_FCTRY; restore and verify the sealed IFS at the end.

SUBAGENTS: static RE only, no bench.  Skeleton first, exact paths, an explicit
"stop when answered".

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.
```
