# Session 58 (absolute) — the touch ghost fixed, the OSK repeat located, and a writable/grown root with SSH

> Numbering: absolute `58` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 52** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-16.
Unit state at the end: **the writable Nura root on hd3, grown to 56.5 G, with
SSH**, region 0 **`0x294F8E36`** (`0x9E0980` B, `boot/chainloader/kernel.img`
sha256 `f0738625…`, kernel `7.3.3 #77`).  The hd3 *media* is the same root grown
in place; the flasher-restorable base is `f7858110…`.  Region 1 `49e318bf…`
(the fallback, not booted).  `PB_LINK=usb tools/pbssh.sh` is the shell; the
ttyGS0 getty is the fallback.  `BOOTCTR_FCTRY` = `0x32` (this boot read `0x1B`
and `/init` wrote `0x32`, RPMB write counter 12049).  The one hardware fault is
the eMMC cache flush (trap 300).

## Goal of this session

The handoff's active item: fix the touch position ghost, then profile the upload
path.  After the ghost was fixed the operator added two requests: a clean shell
(SSH) and a root grown to the partition that keeps its files across a reboot.

## Confirmed (measured, read in a file, or seen on the bench)

* **The position ghost is fixed.**  A fast `hello` types correctly on kernel
  `#74`; before the fix it produced `h,e,h,h` and once only `h,e,o`.  The driver
  now keeps the 10 ms poll armed while the controller asserts its IRQ line and
  for 200 ms after the last contact, and holds a new down back one sample when
  it repeats the slot's previous coordinates within 300 ms.  [report 248 §2;
  `docs/captures/touch-munin-2026-11-16-session58.txt`; kernel `#74`
  region 0 `0xED58EF36`]
* **The vendor driver confirms `b3` is only a down flag.**  In
  `libmtouch-munin.so`, `is_contact_down` tests `b3 != 0 && y != 0xFFF`,
  `get_contact_id` returns `digit_idx`, and `get_seq_id` is an incrementing
  counter at `arg+0xac`; the driver does not set
  `MTOUCH_FLAGS_INCONSISTENT_DIGIT_ORDER`/`_CONTACT_IDS`.  [REA:
  `munin_sample_xy_data` 0x150b0, `get_coords` 0x14df0,
  `is_contact_down` 0x14da0, `get_contact_id` 0x14d94,
  `get_seq_id` 0x14e94, `munin_panel_init` 0x1734c]
* **One tap is one contact at the driver.**  A clean `a,b,c,BackSpace` run gave
  four contacts, each one down and one up; the backspace contact was 131 ms at
  display (969,519), `down=01` → `down=00`, evdev `MT_TRACKING_ID -1`.
  [capture §3]
* **One backspace tap is three `0x7f` bytes at the application.**  `keylog.py`
  on the terminal's stdin logged `7f` at 0 ms, +194 ms and +234 ms for that one
  contact.  A single `a` tap logged one `0x61`; an `a` held ~1.3 s also logged
  one `0x61`.  [capture §4]
* **The key release reaches squeekboard ~100 ms after the driver's release**
  (the repeats continue past the clean contact up).  [capture §4 timestamps]
* **DPMS off cannot be woken by touch.**  With `bl_power=4` the taps advanced
  `/proc/interrupts` for `1-0065` and appeared in evdev and `PBTS`, but
  `bl_power` stayed 4; `catacomb msg dpms on` restored it.  [trap 298]
* **Upstream catacomb 1.7.0 replaced squeekboard with wvkbd** (checked
  2026-11-16; squeekboard is unmaintained, and the image's
  `postmarketos-ui-catacomb` still depends on squeekboard 1.43.1).
* **SSH is up and persistent.**  The initramfs installs the session-48 static
  dropbear (968 032 B) + both host keys + the authorized key + two
  `/etc/local.d` scripts; `pb-ssh.start` brings `usb0` to `169.254.0.1/16` and
  starts dropbear on `:22`.  Measured: `PB_LINK=usb tools/pbssh.sh 'uname -a;
  id'` → `Linux playbook 7.3.3 #77 …` and `uid=0(root)`.  [report 249 §1]
* **The root is read-write and the files persist.**  `/proc/mounts` shows
  `/dev/loop0 / ext4 rw,relatime,nobarrier`; `/root/persist.test` written before
  a reboot read back `PERSIST_1820` after it.  [report 249 §2]
* **The filesystem grew to the partition.**  `resize2fs -P` reported 131 107
  blocks minimum; the grow log records `The filesystem on /dev/loop0 is now
  15273984 (4k) blocks long`, and `df -h /` is `56.5G … 53.9G available`;
  `/etc/pb-grown` marks it.  [report 249 §2]
* **The eMMC cache flush is the fault, in two measured forms.**  Without the
  workaround a journalled write produced `mmc1: Card stuck being busy!
  __mmc_poll_for_busy`, `I/O error, dev mmcblk1, sector 0 op 0x1:(WRITE) flags
  0x800800`, `Aborting journal on device loop0-8`, remount read-only.  With
  `barrier=0` + `write_cache=write through`, an explicit `sync` still produced
  the stall (`flags 0x900000`, a lost async page write) but the FS stayed `rw`.
  Data writes themselves succeed (a 16 KiB O_DIRECT probe into unused hd3 space
  read back byte-exact; the 15-million-block resize completed).  [report 249 §3]
* **The full 694-line dmesg was read.**  The only real errors are the flush
  above and one `DSI: omapdss DSI error: Failed to set dsi_if_enable to 0` at
  52.7 s, followed by a successful PLL re-lock at 54 s (compositor running,
  backlight on).  Everything else is expected or cosmetic (the complete list is
  report 249 §5).  [report 249 §5]
* **The counter self-maintained:** `pbrpmb: topup: BOOTCTR_FCTRY … =
  1b000000` then `writing 0x1b -> 0x32 (RPMB counter 12049)`.  The session's
  reboots drained `0x25 → 0x1B`.  [dmesg lines 586/588]
* **An unpaced host→device RNDIS burst truncates.**  Two direct copies of the
  968 032-byte dropbear arrived short (724 960 and 752 384 bytes); 8 KiB chunks
  with a 5 ms pause are byte-exact (sha256 `78619fcb…`).  [trap 302]
* **The export is still 64 files / 8 981 lines**, sha256 `ca17a057…`, 0
  rejects / 64/64 byte-identical.  The walk excludes `drivers/gpu/drm/pvrsgx/`
  (271 files) and `scripts/` (2); without those it reports 337/337.

## Inferred (reasoning from evidence, not directly measured)

* **The ghost's first sample was stale because the IRQ precedes the report
  update.**  The down-confirm fixes the observed symptom; the exact raw sample
  at the ghost moment on `#73` was not recorded.  (Strong.)
* **The dropped burst taps were the poll abandoning a burst at a mid-burst
  all-up sample.**  A fast `hello` types correctly with continuous polling, but
  the chip can also physically miss taps shorter than its scan interval, so not
  every drop is attributable.  (Moderate.)
* **The repeat is a ~200 ms delay / ~40 ms interval action-key repeat**, and
  the release reaches the OSK late because the compositor's event loop is slow
  (the upload path) or squeekboard's timer is not cancelled promptly.
  (Moderate; the split is not measured.)
* **The stall is the card's `FLUSH_CACHE`/FUA path, not the data writes.**  The
  failing request has no data segments and is reported at sector 0, while data
  writes and the resize succeed.  (Strong.)
* **`CACHE_CTRL = 0` and/or a `cache_size = 0` quirk will eliminate the
  stall**, because the MMC core gates the flush on `EXT_CSD.CACHE_SIZE`.
  (Unproven; that is the next experiment.)

## Dead ends and wrong assumptions

* **"Track contacts by `b3`."**  `b3` is not an id; the vendor's
  `get_contact_id` returns the digit index.  Measurement: the REA decompilation
  above.
* **"The repeat is the touch driver leaving a contact down."**  Evdev shows a
  clean `-1` and `keylog.py` logged three bytes for one contact; the live
  `BTN_TOUCH` is 0.  Measurement: capture §3/§4.
* **"Touching the screen wakes DPMS."**  It does not; `bl_power` stayed 4 while
  the taps reached evdev and `PBTS`.  Measurement: the forced DPMS-off test.
* **"Writing `/proc/<foot>/fd/7` feeds the terminal."**  It does not;
  `TIOCSTI` on `/dev/pts/0` does.  Measurement: `/tmp/INJ2` appeared only with
  `TIOCSTI`.
* **"A read-write root just works."**  The first attempt aborted the journal on
  the cache flush.  Measurements: the `-8` abort and emergency remount.
* **"The clean hd3 image can be grown after the failed write."**  It could not:
  the aborted journal set `s_state` errors and `resize2fs` refused
  (`ext4_resize_begin: There are errors in the filesystem`).  Fix: reflash the
  clean `f7858110…` image and grow from a clean FS.
* **"`write through` makes `sync` safe."**  It does not; an explicit flush
  still reaches the card.  Measurement: the `flags 0x900000` stall after
  `sync`.
* **"The one-time grow's flush error will recur."**  It was the grow's only
  flush; with `/etc/pb-grown` set the grow does not rerun, and the later stall
  was the agent's own `sync`.
* **"openssh-server can be used."**  Only `openssh-server-common` (config, no
  binary) is installed; dropbear is the path.

## Unknown / open questions

* **Does disabling the card cache remove the stall?**  Settle by reading
  `EXT_CSD` (CACHE_SIZE 249, CACHE_CTRL 33), setting `CACHE_CTRL = 0` via
  `MMC_IOC_CMD` (or adding a `cache_size = 0` quirk), then `sync` and reading
  dmesg.
* **Does wvkbd avoid the backspace/enter repeat?**  Settle by building
  catacomb 1.7.x / wvkbd and repeating the single-tap `keylog.py` test.
* **Is the DSI `dsi_if_enable` error benign?**  Watch for a repeat; a wedge
  would need a reboot (trap 289).
* **Is the repeat delay squeekboard's or the compositor's?**  The upload
  profile (release-to-client timing) would separate them.
* **Why does the card's flush hang?**  Card firmware/cache behaviour; no
  datasheet.  Bounded by the workaround.
* **Does a cold power loss lose cached writes?**  `barrier=0` + write-through
  means no flush; the card's cache may still hold recent data.  A power-cut
  test would settle it.
* **What does `xournalpp`'s 48 kHz `hw_params` ask for?**  Carried over from
  session 57 (trap 294); not measured.

## Superseded / uncertain

* **Session 57's "any touch restores DPMS".**  Not true; trap 298 is current.
* **Session 57's "the driver drops the lifted-contact sample" as the cause of
  repeats.**  The release is read now; the remaining repeat is downstream.  The
  release fix stays.
* **The session-57/58 read-only hd3 + tmpfs overlay working rule.**  Superseded
  by the operator's session-58 decision to mount read-write and grow (DECISIONS
  §84); the image `f7858110…` is the way back.
* **Region 0 `0xB9DD91E3`/`#73` and `0xED58EF36`/`#74`.**  Current is
  `0x294F8E36`/`#77`.
* **"hd3 = `f7858110…`" as the media identity.**  The media is now the grown FS
  with the SSH kit; `f7858110…` is the flasher-restorable image.
* **The first draft's "`BOOTCTR_FCTRY` 0x25, not re-read".**  This boot measured
  0x1B → topped up to 0x32.

## Artefacts produced

* Reports `docs/reports/248-touch-ghost-fixed-repeat-squeekboard-2026-11-16.md`
  and `docs/reports/249-writable-grown-root-emmc-flush-2026-11-16.md`.
* Capture `docs/captures/touch-munin-2026-11-16-session58.txt` (evdev, PBTS,
  keylog, DPMS).
* Initramfs `initramfs/init.c` (read-write mount, `barrier=0`,
  write-through, `install_bench_tools`, `pb-ssh.start`, `pb-grow.start`,
  `sysfs_write`); `.gitignore` for the embedded kit.
* `kernel/source/playbook-munin.c` and `kernel/source/omap4-winchester.dts`
  (the touch fix; also in `local/kernel-tree/`).
* `kernel/patches/0001-playbook-winchester-7.3.3.patch` (64 files / 8 981
  lines, sha256 `ca17a057…`).
* Region 0 images: **`0x294F8E36`** (flashed; `0x9E0980` B, sha256
  `f0738625…`, kernel `#77`), `0xA1B13FBF` (`barrier=0`, not kept), `0x5A71CA6F`
  (read-write, no barrier), `0xED58EF36` (`#74`, the ghost fix).
* Traps 296 (fixed), 298, 299, 300, 301, 302; DECISIONS §83/§84;
  `docs/sessions/NUMBERING.md`, `STATE.md`, `HANDOFF.md`, `ROADMAP.md`,
  `KNOWN-ISSUES.md`, `reports/INDEX.md`, `captures/README.md`.
