# PlayBook device & tooling reference (2026-10-26)

Standalone reference for the **booted TabletOS side** of the spare 64 GB PlayBook:
how to get in, what the shell has, which device nodes to use, and the safe recipes for
reading bytes, NVRAM, RPMB, PPS and physical memory. Pair with
`HANDOFF-2026-10-26-next-session.md` (strategy) and `95`/`100`/`101`/`102` (results).

## 1. Identity / access

- QNX 6.6, `OMAP4430_Winchester_ES2.2_HS_PVT4_Rev:07 armle`, model `0x06001A06`.
- Spare: PIN `0x50176EC1`, BSN `1063043093`, `SecureDevice=true`.
- Reference: PIN `0x50175127` — **do not touch**.

**SSH depends on the qconn door keeper.** Start it in the background:
```bash
cd /home/psyden/rea-work
python3 BerryShell-V4.py hold --key /home/psyden/rea-work/rsa --pub /home/psyden/rea-work/rsa.pub
```
Then plain SSH works:
```bash
SSHOPT="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o IdentitiesOnly=yes \
  -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedKeyTypes=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa \
  -o KexAlgorithms=+diffie-hellman-group1-sha1 -o MACs=+hmac-sha1,hmac-sha1-96 \
  -o Ciphers=+aes128-cbc,3des-cbc,aes256-cbc -o LogLevel=ERROR"
ssh -i ./rsa $SSHOPT root@169.254.0.1
```
- The **interactive `BerryShell-V3.py` auth will fail while the hold owns the door** — that is
  expected. Use plain SSH, or stop the hold first.
- Killing the hold closes the door and SSH is refused. Restart it.
- `scp` must use the legacy protocol: `scp -O -i ./rsa $SSHOPT <file> root@169.254.0.1:/path`.
- `/tmp` is wiped on every reboot; `/accounts/devuser` persists.
- **Single-owner USB.** Only one session may drive the bootrom/loader. Before flashing, check
  `ps -ef | grep -E 'BerryShell|pre_os_ifs_flash|probe_loader'`; a second session causes
  connect/disconnect churn and stale loader sessions. Hand the device over explicitly.

## 2. What the shell has

**Present** (confirmed): `sh`, `ls`, `cat`, `dd`, `awk`, `sed`, `grep`, `find`, `rm`,
`printf`, `mount`, `sync`, `df`, `tail`, `strings`, `which`, `chmod`, `cp`, `mv`, `mkdir`,
`ln`, `uname`, `date`, `sleep`, `pidin`, `slay`, `on`.
`ksh` is used by the boot scripts (likely `/base/bin/ksh`).

**Missing** (confirmed): `xxd`, `od`, `hexdump`, `head`, `sha256sum`, `md5sum`, `cmp`,
`diff`, `bc`, `python`, `perl`, `base64`, `kill`.
- `cat -v` is not supported (illegal option).
- Use `tail`, not `head`.
- Use `slay`/`pidin` instead of `kill`.
- Use host-side `xxd`/`sha256sum` after transferring bytes.

## 3. Binary data: the rule

**Direct SSH stdout is binary-clean.** Read on the device, write to a local file, parse on
the host:
```bash
ssh ... 'dd if=/dev/hd1 bs=512 skip=2048 count=5632 2>/dev/null' > /tmp/ifs.bin
python3 - <<'PY'
d=open('/tmp/ifs.bin','rb').read()[:0x2BFE10]
import hashlib; print(hashlib.sha256(d).hexdigest())
PY
```
- Don't pipe binary through the harness logs or `BerryShell` (its log lines go to stdout).
  Plain `ssh ... > file` is clean.
- Don't use `awk`/`sed`/`cat` for binary parsing: NUL handling is fragile. `strings` works.
- **`dd` with no arguments blocks on stdin — never run bare `dd`.**
- `dd` supports `bs=`, `skip=`, `count=`, `seek=`, `conv=notrunc`; `skip`/`seek` are in units
  of `bs`. Use `bs=512` for sector arithmetic.
- For a 4-byte value: `ssh ... 'dd if=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1' | xxd -p`.
- Construct byte strings on the device with `dd if=/dev/zero` + `printf 'X' | dd ... conv=notrunc`
  (e.g. `32 00 00 00` = `dd if=/dev/zero of=/tmp/bc32.bin bs=4 count=1; printf '2' | dd
  of=/tmp/bc32.bin bs=1 count=1 conv=notrunc`).

## 4. Device nodes

| node | use |
|---|---|
| `/dev/hd0` | raw eMMC (whole 64 GB); sector 0 is the MLO header, not an MBR |
| `/dev/hd1` | **region 0** (512 MB; MBR 0, SIG `0xF0000`, IFS `0x100000`, RFS `0xC00000`) |
| `/dev/hd2` | **region 1** (512 MB; empty/ignored by the factory boot) |
| `/dev/hd3` | root UFS partition |
| `/dev/hd4`..`/dev/hd6` | small partitions (512 KB / 128 KB) |
| `/dev/hd1t179`, `/dev/hd2t179` | the QNX RFS partition inside each region (`/base`) |
| `/dev/nvram` | NVRAM resmgr; **`read()` = ENOSYS** — use `nvram2` |
| `/dev/rpmb/` | **directory** of RPMB records (`BSIS_*`, `devcert_dat*`, `FS_DIRTY`, …) |
| `/dev/mem` | physical memory (used by `memdump3`) |
| `/dev/ser1` | UART3 (pads not accessible) |
| `/dev/i2c1`, `/dev/i2c3` | PMIC / FAN5702 LED bus |

`df -k` reports `/dev/hd1` and `/dev/hd2` as 512 MB each; `/dev/hd0` is the 64 GB device.

## 5. Physical memory: `memdump3`

`/accounts/devuser/memdump3` reads **physical memory** through `mmap_device_memory` with
`PROT_NOCACHE`; it does **not** read files or device nodes. Bad addresses are contained
(SIGBUS), the unit survives. One address per call; never sweep the bus.

```bash
on -C 0 /accounts/devuser/memdump3 90000000 0x500
```
- Output is `addr: value` (u32) lines.
- `on -C 0` pins the process to CPU0 (0-based). Mandatory for payloads/memdump.
- Breadcrumb/Debug ring convention (from the kexec project): `+0x80` count, `+0x84` index,
  `+0x100` 1024 chars; pages at `0x90000000`, `0x94000000`, `0x88000000`, `0x9FE00000`.

## 6. NVRAM: `nvram2` (the 2.x tool)

`/accounts/devuser/nvram2` is an ARM QNX PIE built for TabletOS 2.x. It loads
`/lib/libnvram.so.1` (stock 2.x, 5740 B) and needs `libgcc_s.so.1` from
`/accounts/devuser`:
```bash
cd /accounts/devuser
LD_LIBRARY_PATH=/accounts/devuser ./nvram2 read 0x2801 hex
```
Commands: `read`, `partialread`, `patch`, `write`, `delete`, `delete_secure`,
`get_permissions`, `is_protected`, `record_exists`, `get_bootrom_metrics`,
`get_os_metrics`.

- `record_exists <id>` → 0/1; `is_protected`/`get_permissions` for the permission word.
- `patch <id> <off> <bytes>` re-reads the record, patches in place, and writes the insecure
  then secure copy (returns early if the insecure write fails). **Use `patch`, not `write`**:
  `write` uses the supplied size as the new record length and truncates.
- Read-only examples:
  ```bash
  ./nvram2 read 0x2002 hex   # PIN
  ./nvram2 read 0x2074 hex   # OsRegionV3 (72 B)
  ./nvram2 read 0x2801 hex   # PasswordStruct (112 B)
  ./nvram2 read 0x2802 hex   # reserve
  ./nvram2 read 0x2004 hex   # often absent
  ./nvram2 read 0x2040 hex   # NukeStatus (spare: 2)
  ```
- The BB10 `nvram-editor/nvram` is a different build (BB10.0.9.388 lib) and needs a
  `/proc/boot/libnvram.so.1` symlink; **do not mix it with `nvram2`**. On the spare, use
  `nvram2`. The symlink is RAM/temporary and dies on reboot.

## 7. RPMB: `/dev/rpmb`

It is a directory resmgr; each record is a file.
```bash
ls -la /dev/rpmb
dd if=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1          # read (read-only)
dd if=/tmp/bc32.bin of=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 seek=0 conv=notrunc
rm /dev/rpmb/TEST_PAD                                # delete a record
```
- `BOOTCTR_FCTRY` is **absent** on the working units until created; value `1..0x32`; it
  decrements on every successful factory boot. Never write `0` or `>0x32`.
- `>`/`>>`/`: >` on a record truncates/replaces it — use `dd ... conv=notrunc`.
- Each authenticated write bumps the monotonic RPMB counter; keep writes few.

## 8. PPS

```bash
cat /pps/system/nvram/osregion     # Actual/RequestedBootRegion, counts, Max, BootStatus
cat /pps/system/nvram/deviceinfo   # PIN, PIN64, BSN, HardwareID, MACs, SecureDevice
```

## 9. Reboot / processes

```bash
sync; /proc/boot/shutdown -S reboot    # this build: -S system|reboot; no reboot/shutdown -r
pidin | grep -i <name>
slay -f <pid>                          # kill is absent
```
`/bin/on`, `/bin/slay`, `/proc/boot/pidin`, `/proc/boot/shutdown` are the paths.

## 10. Tools installed on the spare (`/accounts/devuser`)

`nvram2`, `libgcc_s.so.1`, `memdump3`, plus `ledtest.sh`, `startup.sh`, `symlink`.
Re-upload after a factory reset; they persist across normal reboots:
```bash
scp -O -i ./rsa $SSHOPT nvram-2.x/nvram  root@169.254.0.1:/accounts/devuser/nvram2
scp -O -i ./rsa $SSHOPT nvram-2.x/libgcc_s.so.1 root@169.254.0.1:/accounts/devuser/
scp -O -i ./rsa $SSHOPT playbook-dev/kexec/memdump3 root@169.254.0.1:/accounts/devuser/
ssh ... 'chmod +x /accounts/devuser/nvram2 /accounts/devuser/memdump3'
```

## 11. USB / boot-stage timeline

IDs: `0fca:0001` bootrom, `0fca:8001` ramloader, `0fca:8010` OS, `0fca:8012` OS/RNDIS,
`0fca:8020` OS/mass-storage. A `0x8001 → 0x0001 → ...` cadence every 5–10 s is a reset loop.
```bash
journalctl -k --no-pager -n 300 | grep -iE 'usb 3-1|0fca'
python3 bootdumps-2026-09-11/analysis/usb_watch.py      # live, no root
```

## 12. Gotchas

- `head`, `od`, `xxd`, `hexdump`, `sha256sum`, `python`, `perl`, `kill` do not exist.
- `cat -v` is unsupported; `dd` with no args hangs on stdin.
- `/dev/nvram` read is ENOSYS; use `nvram2`.
- `nvram2` needs `LD_LIBRARY_PATH=/accounts/devuser` for `libgcc_s.so.1`; the stock
  `/lib/libnvram.so.1` is the 2.x lib.
- `/tmp` is RAM and wiped on reboot; `/accounts/devuser` persists.
- SSH depends on the BerryShell hold; the interactive qconn auth fails while it holds.
- `scp` needs `-O`.
- Never sweep addresses with `dd if=/dev/mem`; use one `memdump3` address per call.
- The device can drop into `0fca:8001` (loader) after a crash; send `0x80EF` then `0xEF`
  (or power-cycle) to get back to the OS.
