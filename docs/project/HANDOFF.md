# Handoff — session 55

**The one next step: flash the packed both-halves container in one bootrom
session, boot the read-only overlay root, and check the DE/WLAN.**  The
overlay is implemented and built; the flash did not run (the operator stopped
before it).  [report 242; session-54 note continuation]

## State you inherit

* The unit is on the **old** region-0 image `0x060B25F2` + the **dirty** Nura
  root on hd3 (`0x4915`, the journal aborted); region 1 `49e318bf…` intact and
  not booted; `BOOTCTR_FCTRY = 0x1e` (the same boot; no reboot).
* The Nura root is read-only (`emergency_ro`); the ACM is `0fca:8010`,
  `ttyACM0` 0666; no sshd.  The `ttyGS0` shell was busy at wrap-up — if it still
  is, send Ctrl-C with `tools/pbraw.py 2 03`.
* Host artifacts (gitignored):
  * `local/work/nura/images/both-0xE85217E5-overlay.signed` — the packed
    container, 635 961 936 B, sha256 `c3ed1f61…` (region 0 at `16,0`, hd3 at
    `16640,0`; parse CRC `0x493FA440 OK`);
  * `local/work/nura/images/blackberry-playbook-root-overlay.img` — hd3,
    625 999 872 B, sha256 `1f14915f…`;
  * `local/images/kernel-0xE85217E5-overlay.img` — region 0, `0x9662A4` B, CRC
    `0xE85217E5`, sha256 `fbeb6df0…`;
  * `initramfs/init.c` — the read-only/overlay code.
* The way back: the 1917 IFS at `0x825`; region 1; the `0x060B25F2` image.

## Procedure (from the booted Nura root)

```bash
lsusb | grep -i 0fca                       # 0fca:8010 = OS; 0001 = bootrom; 8001 = ramloader
nohup python3 tools/bbusb.py flash --signed \
      local/work/nura/images/both-0xE85217E5-overlay.signed \
      --from-bootrom --password 0000 --apply > /tmp/f.log 2>&1 &
sleep 2
python3 tools/pbcon.py 'reboot -f'          # Nura ttyGS0 BusyBox shell; -f skips the shutdown
until grep -qE 'COMPLETE|done|error' /tmp/f.log; do sleep 2; done; tail -5 /tmp/f.log
# bbusb resets the loader itself after the write.  The bootrom window is 60 s.
# "No BlackBerry device found" = the reboot came too late: retry, reboot sooner.
for i in $(seq 1 40); do ls /dev/ttyACM0 >/dev/null 2>&1 && break; sleep 2; done
```

`--from-bootrom` uploads the ramloader: the unit must be **booted**, not in the
ramloader; the flasher catches the bootrom window opened by `reboot -f`.  One
bootrom session carries both the region 0 and hd3.  Success is
`COMPLETE 0x40C0 -> cmd 0x4006` and `done`.  (Nothing was sent at wrap-up; the
shell was busy.)

## What each result means

* **`COMPLETE`/`done` and the overlay boots** — check
  `dmesg | grep -E 'mmc1|EXT4|overlay|Card stuck|Aborting'` (no stall, no
  journal abort, `overlay` on `/newroot`), `dmesg | grep pbrpmb` (the counter),
  `rc-status`/the tinydm log, `/run/user/10000`, `/dev/dri/card0`, `lsmod`/
  `wlcore`, the touch, the backlight.  If the DE runs → SGX540.
* **The overlay mount fails** — `dmesg` for `overlay: mount` / `tmpfs` /
  `rootfs: mount`; the `0xE85217E5` must carry `CONFIG_OVERLAY_FS=y`; the
  `lowerdir=/rootfs` must be mounted read-only.
* **The eMMC still stalls** — the lower was not read-only (check the mount
  flags/the loop `ro`), or the card is in the failed state from the earlier
  boot; a cold power cycle + a fresh hd3 flash decides.  The writable-root
  fix (busy timeout / cache / no-journal) is a separate experiment.
* **The DE still fails** — read the tinydm/elogind log and `/run/user/10000`;
  the likely causes are the seatd/logind backend or the PAM stack.  The root is
  writable in the overlay, so a `seatd`/`elogind` test is possible.
* **The WLAN firmware loads but no link** — the NVS/regdomain; `iw reg set`.
* **The flash fails** — `0xF7 -> cmd 0x15` means the gate rejected (nothing
  written); check the container's runs (16/16640) and the `--from-bootrom`
  timing.  If the unit is left in the ramloader, `bbusb.py reset-loader`.

## Safety / rules

* One variable; hd3 is flasher-restorable; region 1 stays intact; the 1917 IFS
  at `0x825` is the way back (SAFETY §4).
* hd3 writes use loader block `0x4915`; the pack coordinate is the loader block
  minus `0x815` (`0x825`→16, `0x4915`→16640).
* Re-export the kernel patch after any DTS/driver change; the `initramfs/init.c`
  change is userspace and does not affect the patch.
* No restore at wrap-up unless the operator wants TabletOS or a test image left
  the unit unbootable.

## Paste-ready kickoff

> Read `docs/project/HANDOFF.md`, `docs/reports/242-*` and the session-54 note
> continuation.  Do the session-55 experiment: flash
> `local/work/nura/images/both-0xE85217E5-overlay.signed` with
> `tools/bbusb.py flash --signed … --from-bootrom --password 0000 --apply` and
> `tools/pbcon.py 'reboot -f'`, then boot the read-only overlay root and check
> the eMMC/overlay/tinydm/WLAN.
