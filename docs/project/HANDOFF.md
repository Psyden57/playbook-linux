# Handoff — session 28 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§5).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 28 = Linux-era 22**.  See
`docs/sessions/NUMBERING.md`.

> **Session-27 update (read first).  Items 4 and 6 are CLOSED.**  The RPMB
> block transform is reproduced and **the factory counter was topped up FROM
> LINUX**: `0x2E -> 0x32`, one type-3 frame, accepted, read-back
> `32 00 00 00`, and the restore boot's bootloader read and decremented that
> very record (report 175 + addendum).  The transform is `0x33` → `0x2F` (**the
> cipher**, `dir 1` seals) → `0x33` → `0x2E` (**the digest**, 224 → 32, tag
> gate), with digest key `seed[0:16]`, cipher key the `0x33` preload
> `page+0x20` (16 bytes), and **no `0x31(id 1)`** (trap 89).  `BOOTCTR_FCTRY`
> lives at block `0x3A`, name offset 196, len 4.  Runtime brightness also works
> (`/sys/class/backlight/backlight/brightness` → LM3532 `0x74`), and the CRNG
> has a seeding path (console `entropy <hex>`, trap 87).  **Next: item 5** —
> the real userland and the `hd3` cut-over — then item 7.  The unit is on
> 2.1.0.1917 (from the **compressed** IFS, `local/work/ifs1917-compressed.ifs`,
> sha256 `f14db207…`), `BOOTCTR_FCTRY = 0x32`, door up with **root ssh**.

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

**Diagnostic images** (the `pbmon` RPMB work) are built from the current
`local/kernel-tree` (which now carries the working two-stage transform);
`0x0D1DA0D3` is the clean fallback.  Session 27 built `0x4F988620`,
`0x74D34374`, `0x4EBF1F97`, `0x48166F12`, `0x1FEBC300` and `0xD3D42853`; the
last is the image that reads the store.

**The region-0 IFS is compressed and boots (session 27).**  The QNX IFS format
is the same on the PlayBook as on BB10, so `tools/bb10_ifs.py splice` +
`89-crc_tool.py reseal` turn the uncompressed 7.9 MB container into a booting
3.47 MB one (`local/work/ifs1917-compressed.ifs`).  That is the route if a
region-0 payload ever needs the room — but our chainloader image is not a QNX
imagefs, so it needs a decompressing entry path first, not just `splice` (see
the session note's open questions, including the unverified 11.25 MB figure).

**Item 6 is CLOSED (session 27).**  The transform is reproduced (report 175)
and the operator performed the single `BOOTCTR_FCTRY = 0x32` top-up **from
Linux**: `pbrpmb prep 0x3a` builds the whole pipeline read-only (authenticated
read, monitor unseal, patch 4 bytes, monitor seal, seal round-trip check, one
type-3 frame) and `pbrpmb write 0x3a yes` sends it, reads the result and
unseals the read-back.  The write was accepted, the read-back read `32 00 00
00`, a read-only scan either side showed 512/512 frames authenticating and
107/107 blocks unsealing, and the restore boot's bootloader consumed that very
record.  **An agent still never writes RPMB** (`SAFETY.md` §6): session 20's
accepted write was a *plaintext-patched* block and left the spare in the `0x5B`
loop; the fix is the monitor re-seal, not a licence to write.

**Still open:** the real userland in the region-1 root, and the `hd3` cut-over
(overwrite the MCT type-`0x1C` partition, raw `0x49000000`, no MCT edit) —
gated on the top-up.  Packaging (postmarketOS-style, item 7) is untouched.

## 0a. The way back (session-25 restore procedure; the unit is currently up)

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
`tools/pbssh.sh 'uname -a; mount | grep base; tail -3
/accounts/devuser/.bootctr.log'` (**root** ssh since 2026-11-05).
**Do not flash anything without the operator's go-ahead.**

From **our Linux image** the way back is the shorter one: flash
`local/work/ifs-shim-sealed.ifs` (the 1917 IFS) with
`tools/pre_os_ifs_flash.py` and reboot with `tools/pbcon.py 'reboot'`
(`DEVELOPMENT.md` §6).

## 1. The one thing to do next

**`ROADMAP.md` item 5 — the real userland, then the `hd3` cut-over.**  Items 4
and 6 are closed (session 27), so this is the last functional item.

1. **Build a real userland into the region-1 root** (host-side; writable eMMC
   for our own data is allowed, `DECISIONS.md` §18).  The current root is a
   32 MiB ext4 at raw `0x29000000`, written through the pre-OS `0xF7` loop and
   mounted read-only by `/init` (reports 139/140, `tools/build_rootfs.py`).
2. **Then the `hd3` cut-over**: overwrite the MCT type-`0x1C` partition (raw
   `0x49000000`, ~58 GiB) with our own ext4 — **no MCT edit needed**; the loop
   exposes the full ~58.76 GiB (trap 58).  The counter no longer gates this:
   Linux can top it up itself (report 175).

If the region-1 root or `hd3` work needs a fresh 1917 boot, the compressed IFS
is the one on media and it boots; the uncompressed
`local/work/ifs-shim-sealed.ifs` also boots and is the conservative choice.

Useful state from session 27, if anything RPMB-adjacent comes up again:

```bash
run /pbrpmb pairtest    # the transform: expect unseal=0/OK p=2dbc8359, ct=OK tag=OK
run /pbrpmb keytest     # live block 0x43 round-trip
run /pbrpmb blockscan   # 512/512 frames, 107/107 unseal, BOOTCTR_FCTRY@196
run /pbrpmb prep 0x3a   # build the sealed top-up, write nothing
```

Seed the CRNG first (`entropy <hex>` from the console, trap 87) before any
write; `pbrpmb` warns if it has to fall back to an unseeded nonce.

Do **not** boot the mount probe (`0x984267D3`); it reset-loops.  Do not boot
`local/images/ifs0.sealed.bin` on a 1917 media set (trap 84).

## 1a. Tools that make this cheap

* **`tools/pbssh.sh`** (the door; **root** ssh since 2026-11-05, no `PB_USER`
  needed) and **`tools/pbcon.py`** (USB console — the only way into our Linux
  image); `tools/door-hold.sh` keeps the door up.
* **`tools/build_kernel_image.py`** — wraps `zImage` + DTB into a copy of the
  sealed IFS with the chainloader, prints the CRC.
* **`tools/pre_os_ifs_flash.py`** — the pre-OS `0xF7` region-0 write; it resets
  the loader itself.
* **`tools/pb_ring.py` / `pb_events.py`** — the DRAM console ring and the
  printk-free event log.
* **`tools/pbdsi`** (`qnx-dsiread.c`) — the TC358764 oracle over DSI from a
  running TabletOS; push it after every reboot.
* **`local/kernel-tree/drivers/misc/pbmon.c`** + `/dev/pbseal` /
  `/dev/pbunseal` / `/dev/pbkeys` / `/dev/pboracle` — the monitor transform.
  Session 27 implemented the bootloader's **two-stage** sequence (`0x2F` is the
  cipher, `0x2E` the digest; unseal takes 224+32 and checks the tag), added
  `/dev/pbkeys` ({keyA,keyB}) and the `op2e` parameter, and pinned both key
  defaults to the seed.  `dom` (default 5) still selects the domain.
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
   monitor reads identity addresses below `0xB0000000`.  **No agent writes RPMB.**
80. The loader's factory count is the first diagnostic after a flash
   (`bbusb.py info --from-bootrom --password 0000 --keep-loader --json`).
82. The bootloader's code addresses in reports 150/151 are `0x9000` too high
   (the arm9000 section loads at `0x80000000` from media `0x9000`).
83. The bootloader wipes its own RPMB state before the OS jump, and its mount
   reset-loops when called from the chainloader.  Never boot the mount probe.
84. `local/images/ifs0.sealed.bin` is the 2.0.0.4869 IFS — wrong for a 1917
   media set.  Use `local/work/ifs-shim-sealed.ifs`.
85. RPMB keys are per-unit; the compiled keys were the spare's; the transform is
   a two-stage `0x2E`+`0x2F` (report 174).
86. **The raw RPMB relay is the WIRE view** — reverse the 256-byte data field
   before `/dev/pbunseal`, or every block fails the tag check.
87. **The initramfs CRNG is unseeded** (`random: … uninitialized urandom read`)
   — fix it before any RPMB write, whose replay protection is the nonce.
88. **On our Linux image there is no door and no `/proc/boot/shutdown`**: reboot
   with `tools/pbcon.py 'reboot'`, and start the flasher just before it.

Session-25/26/27 additions relevant here: the factory counter is read with
`bbusb.py info --from-bootrom --password 0000 --keep-loader --json`
(`boot_mode.factory_count`); the password is `0000`, sent once, never retried;
`pkexec` is only for `chmod /dev/ttyACM0`, never for the USB/flash tools; **the
door ssh is root** (2026-11-05) and `devuser` cannot reboot or read `/dev/rpmb`;
**the flasher's precondition is a booted OS** — `0fca:8012` for TabletOS,
`0fca:8010` for our image — never the ramloader (`0fca:8001`).

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
PlayBook (OMAP4430 "winchester") — session 28 (absolute; Linux-era 22)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md,
ROADMAP.md items 4-7, KNOWN-ISSUES.md (traps 9-89), DEVELOPMENT.md,
DECISIONS.md (17-25), reports 174/175 (and 147/148/156), session-27.md, and
any relevant documentation.

SCOPE: this session is the LINUX bring-up.  The BB10 port (BB10-PORT.md,
BB10-IMAGE-GUIDE.md) and the PlayBook OS image work (IFS-REBUILD.md,
IMAGE-REBUILD-GUIDE.md) are PARKED by the operator — do not work on them.

PHYSICAL STATE (session 27 end): PlayBook OS 2.1.0.1917 is running (1917 IFS
local/work/ifs-shim-sealed.ifs, /base mounted), BOOTCTR_FCTRY = 0x32, door up
with ROOT ssh.  Do NOT restore local/images/ifs0.sealed.bin (2.0.0.4869,
incompatible - trap 84).  The restore boot is the IFS verification; the
read-back hash rule is dropped.

WHERE IT STANDS: ROADMAP items 4 and 6 are CLOSED (report 175).  The RPMB block
transform is REPRODUCED and the factory counter was topped up FROM LINUX
(0x2E -> 0x32, one frame, accepted, read-back 32 00 00 00, and the restore
boot's bootloader read that record).  0x2E is
the digest (224 -> 32, key seed[0:16] = page+0x440), 0x2F is the cipher (224
in/out, dir 1 seals) keyed by the 0x33 preload page+0x20 (86 78 7c 2a..., 16
bytes), and 0x31 must NOT be called with id 1 (id 0 or no call both work -
trap 89).  The captured block-0x3B pair round-trips exactly and blockscan reads
the store: 512/512 frames authenticate, 107 blocks carry a valid tag and all
107 unseal, and BOOTCTR_FCTRY is at block 0x3A, name offset 196, len 4 (it read
0x2B after seven factory boots).

THE ONE THING NEXT: ROADMAP item 5 - a real userland in the region-1 root,
then the hd3 cut-over (no MCT edit).  Then item 7 (packaging).  An agent NEVER
writes RPMB (SAFETY.md section 6); the CRNG needs `entropy <hex>` before any
write (trap 87).

BENCH: door = 169.254.0.1 (host: ip addr add 169.254.0.2/16 dev <iface>);
tools/pbssh.sh logs in as ROOT (no PB_USER; the operator enabled root ssh on
2026-11-05).  Start tools/door-hold.sh with setsid (never `pgrep -af
door-hold`).  Once our Linux image runs there is NO door: use
tools/pbcon.py, and reboot with `tools/pbcon.py 'reboot'` — /proc/boot/shutdown
is a QNX path only (trap 88).  Run the USB/flash tools as the USER — never
pkexec; `pkexec chmod 666 /dev/ttyACM0` is the documented console step.  The
device password is 0000, sent once, never retried.  No RPMB writes by an
agent, ever.

SAFETY: read SAFETY.md first.  One variable per boot; budget boots against
BOOTCTR_FCTRY; restore 2.1.0.1917 and verify the counter at the end.

SUBAGENTS: static RE only, no bench.  Skeleton first, exact paths, an explicit
"stop when answered".

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.
```
