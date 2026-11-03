# 170 — The 1917 `/base` hook, the UFS swaps, and the `.5.ufs` baseline

Status: **measured on the reference, 2026-11-03.**  Continues report 169 (the
shim IFS boot).  The operator added `/accounts/devuser/startup.sh` after the
final flash and verified the `BOOTCTR_FCTRY` top-up.

## Answers in brief

* **`/base` change is one guarded call.**  `scripts/startup.sh` gains exactly
  eight lines after `start_runlevel FINAL`; it runs
  `/accounts/devuser/startup.sh &` if that file exists.  Nothing else changed
  (byte diff against the vendor RCFS in §2).
* **Use the vendor 1917 `.5.ufs` with the operator's `autorun::0` change**, not the `PlayBook_2.1.0.1917_ROOT_V1.4.EXE` UFS.
  The ROOT V1.4 UFS boots but its `qconnDoor`/root kit never completes the door
  handshake (SSH stays closed); the vendor `.5.ufs` with that change authenticates and `devuser` SSH
  works (§3–§4).
* **The setup skip is `autorun::0`.**  The
  `PlayBook_2.1.0.1917.5.ufs` carries that change: 2 occurrences of
  `autorun::0`, none of `autorun::1`.
* **The top-up works.**  With the operator's 4,566-byte `startup.sh` in place
  (sha256 `8acf568f…`), the boot wrote and read back the counter:
  `bootctr: wrote 0x32, readback ok (was 0x2E (decimal 46))`.
* **Root SSH is not a `/base` edit.**  `/base/bin/qconnDoor` launches sshd with
  `-o AllowUsers=devuser -o DenyUsers=root`; root is a post-boot operator
  method (the `root.sh`/watchdog kit).  `/base` must not try to change sshd or
  the keys.

## 1. What was on media at the end

| component | value |
|---|---|
| IFS | our uncompressed 1917 image with the 8,759-byte shim; `0x788D84` B, `os_type` 6, CRC `0x38EE7B46`, sha256 `b52195bb…` (report 169) |
| RCFS (`/base`) | the vendor `PlayBook_2.1.0.1917.1.rcfs` (sha256 `8ccf518c…`) with **one** `scripts/startup.sh` hunk (§2) |
| UFS (`/`) | `PlayBook_2.1.0.1917.5.ufs` **with the operator's `autorun::0` change**, 2,550,202,368 B, sha256 `0c0f7d77…` |
| `/accounts/devuser/startup.sh` | operator's, 4,566 B, sha256 `8acf568f…` (added after the flash) |
| `.sig` at `0xF0000` | stock 4869; irrelevant with the shim |

Boot evidence (read-only, over the door):

```
QNX localhost 6.6.0 2014/03/19-01:28:41EDT OMAP4430_ES2.2_HS_Winchester_Rev:07 armle
/dev/hd3 on / type qnx6
/dev/hd1t179 on /base type qnx6
2.1.0.1917
-rwxr-xr-x  1 root  nto  8759 Mar 08 2013 /proc/boot/rfs_validator
-rwxrwxrwx  1 devuser devuser 4566 Mar 19 05:51 /accounts/devuser/startup.sh
bootctr: wrote 0x32, readback ok (was 0x2E (decimal 46)) (Wed Mar 19 05:51:34 GMT 2014)
```

The OS clock is the 1914/2014 build date (no RTC/NTP); log timestamps are that
clock.

## 2. The `/base` edit (the only change)

```
$ diff -u <vendor startup.sh> <edited startup.sh>
@@ -1904,6 +1904,14 @@
         start_runlevel FINAL;
+        # --- session 24: our own per-boot commands -------------------------
+        # /accounts/devuser/startup.sh (on the UFS) keeps BOOTCTR_FCTRY topped
+        # up and does our other boot work.  Background it: the script waits
+        # for /dev/rpmb and the symlink overlay itself.
+        if [ -x /accounts/devuser/startup.sh ]; then
+            /accounts/devuser/startup.sh &
+        fi;
+        # -------------------------------------------------------------------
         if [ -n "${ENABLE_BMETRICS}" ]; then
```

Pristine 61,238 B → edited 61,713 B.  The root-SSH/key block that was tried
first was **reverted**: `qconnDoor` is what enforces `DenyUsers=root`, and root
is a post-boot method, so `/base` must not touch sshd or `authorized_keys`.

How the edit was made: copy the RCFS, mount it with
`bb10mt qnx6 mount -i COPY -m DIR`, edit `scripts/startup.sh`, unmount with
`fusermount -u DIR` (not `-z`; wait for any terminal/kate cwd to leave first).

## 3. The UFS experiments

| attempt | result |
|---|---|
| `PlayBook_2.1.0.1917_ROOT_V1.4.EXE` → `bbusb.py split` → `.0.signed` (1,152,385,064 B) → `unpack --only 5` → `.5.ufs` (2,550,202,368 B); flashed with the IFS+RCFS (1,156,841,504 B, 70,694 frames, exit 0) | **boots, but the door never authenticates**: BerryShell connects to `169.254.0.1:4455`, then `Handshake ended before authentication (wrong password? device dropping?)`; SSH 22 stays closed.  The UFS carries a rooted `qconnDoor`/`root.sh`/watchdog kit that the 1917 door path does not accept. |
| the same ROOT V1.4 UFS with `/accounts/devuser/startup.sh` removed (pristine extract), flashed (1,156,841,504 B, exit 0) | same door failure — the script was not the cause. |
| the vendor `.5.ufs` **with `autorun::0`** (UFS only; 740,622,928-byte container, 45,259 frames, exit 0) | **works**: door authenticates, key uploaded, `devuser` SSH works; no `startup.sh` initially; `autorun::0` skips setup. |

The `.5.ufs`'s `qconnDoor` is the one the old BerryShell/`tools/door-hold.sh`
understands.  The ROOT V1.4 UFS is the one with the reported SSH failures; the
operator's root kit there is a post-boot method, not a boot-time one.

## 4. Root SSH, precisely

* The live sshd on the root UFS is launched by **`/base/bin/qconnDoor`** with
  `-f /dev/null -o permitopen=127.0.0.1:8000 -o PasswordAuthentication=no -o
  Protocol=2 -o HostKey=/etc/ssh/ssh_host_rsa_key -o AllowUsers=devuser -o
  DenyUsers=root …`.  A grep of `/base` finds `DenyUsers=root` only in that
  binary; `startup.sh`'s `tools_qconn_sshd` is just
  `waitfor /etc/ssh/ssh_host_rsa_key 30 && /usr/sbin/sshd`.
* The operator's `/accounts/devuser/root.sh` + `sshd-watchdog.sh` are the
  **post-boot** method (symlink `/accounts/devuser/.ssh` → `/root/.ssh`, slay
  the restricted sshd, restart it without flags).  They are not run at boot and
  are not a `/base` edit.
* Therefore: root SSH is out of scope for the `/base` image; `devuser` is the
  boot-time login (`PB_USER=devuser tools/pbssh.sh …`).

## 5. Bench pitfalls met this session

1. **Do not `pkexec` the USB tools.**  The device node has `user:psyden:rw-`
   (udev uaccess); the user can flash.  `pkexec` runs as root, which lacks the
   user's `pyusb`, and a killed `pkexec` left a **root orphan `bbusb.py`**
   holding the interface (`Resource busy`) and a wedged ramloader.  A
   power-cycle cleared it; no media was written before PreFlash.
2. **`bbusb.py flash --from-bootrom` opens for 10 s.**  Arm
   `while ! lsusb | grep -q 0fca:0001; do sleep 1; done` and then flash, or run
   it with the unit already in the ramloader.
3. **Do not lazy-unmount the FUSE images.**  Wait for terminals/editors to
   leave the mount (check `fuser -vm DIR`), then `fusermount -u DIR`.  A lazy
   unmount leaves the FUSE daemon and later mounts fail.
4. **`bb10mt qnx6 fsck` cannot open the RCFS** (it has an MBR at `0x0`; the
   QNX6 superblock is at `0x2000`).  Use the mount to verify the RCFS.  The UFS
   fsck reports 140 bitmap warnings that are **identical** on the pristine and
   edited images (the sparse extract's `0xFF` holes), so they are not our edit.
5. **The UFS boot takes 2–4 minutes** and ICMP may stay silent while TCP works;
   check ports 4455/80/22, not just ping.
6. **The 1917 clock is 2014** (build date); `.bootctr.log` timestamps are that
   clock.

## 6. What was not changed

No RPMB write by the agent (the OS's own `startup.sh` does the top-up, at the
operator's direction).  No UFS file was edited except adding the operator's
`startup.sh` after the fact.  The `.sig` block, MBRs and RPMB were untouched.

## 7. Repeatable procedure

The step-by-step for any `.ifs` + `/base` (+ UFS) is now in
`docs/project/IMAGE-REBUILD-GUIDE.md`.
