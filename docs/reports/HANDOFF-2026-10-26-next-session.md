# Session handoff — 2026-10-26 (CRC path, pre-OS reflash, kernel attempt) → next session

> **Superseded for the kernel work by `HANDOFF-2026-10-26-clean-kernel.md` and
> `103-clean-kernel-crc-bringup-2026-10-26.md`.** The loader/CRC/region material
> below still applies; the kernel is now a clean 6.15.11 build on the CRC path.

Read this first. Then the reports listed in §10, especially `95`, `100`, `101`, `102`.
The long-term goal is unchanged: **boot Linux (or a modified TabletOS) on the PlayBook**,
now via the native CRC/factory path, not kexec.

## 0. TL;DR

- **CRC/factory path proven.** Region 0 is the only region the factory bootloader verifies;
  `0x2074` is not read; a successful factory boot decrements `BOOTCTR_FCTRY`.
- **Pre-OS IFS reflash proven.** The signed ramloader's `0xF7` (path 1) can rewrite the
  region-0 IFS over USB with no OS. A byte-identical reflash was verified on media
  (`sha256 c72b0530…`) and booted. This is the safety net for everything else.
- **Loader probe:** `BS = 0x10000`; the live MCT matches `hd4.bin` and has **no `0x39`
  entries**, so path 1 (`rrcq.offset=0x20`) is mandatory; `0xDC` is **not** zlib CRC-32.
- **State-probe chainloader** ran from the CRC image, blinked the LED, and left breadcrumbs
  at `0x90000000` + mirrors that survived the WDT2 reset, bootrom, loader upload, recovery
  flash and TabletOS boot. Entry state: ARM SVC, I/F masked, MMU/caches off.
- **Kernel chainloader** (zImage 5.2 MB + DTB) was built and flashed, but failed at the
  chainloader's **DTB-magic check** (breadcrumb stage `0xE1` in all four pages): the code
  compared the little-endian word against `0xD00DFEED`; the in-memory LE word is
  `0xEDFE0DD0`. Fixed and rebuilt; the corrected image CRC is `0xA6FF2D25` and has **not**
  been reflashed yet. The recovery tool restored TabletOS in the meantime.

- **Device/tooling reference:** `DEVICE-REFERENCE-2026-10-26.md` — shell tools, binary-data
  handling, `nvram2`/`memdump3`/`on`, device nodes, RPMB/PPS recipes, and the SSH/hold rule.
- **Single-owner USB:** only one session may drive the bootrom/loader. At handoff, **another
  session was running the kernel test on the spare** (`0fca:8001`, connect/disconnect every
  5–10 s); this session stopped its BerryShell `hold` and all flash tools. Check
  `ps -ef | grep -E 'BerryShell|pre_os_ifs_flash|probe_loader'` before starting USB work.

## 1. Units / safety / current state

- **Spare 64 GB** (PIN `0x50176EC1`, BSN `1063043093`) is the test bed. It was used for all
  destructive work.
- **Connected reference 64 GB** (PIN `0x50175127`) must not be touched.
- Two **32 GB** units are bricked recovery targets.
- **Spare current state (end of session):** TabletOS is up as `0fca:8012` (RNDIS, ping OK).
  SSH works **only while the BerryShell `hold` process is running** (it owns the qconn door);
  the interactive `BerryShell-V3.py` auth fails while the hold is active. The recovery flash
  restored `ifs0.sealed.bin` and it was verified on media (`sha256 c72b0530…`). The counter
  was checked at `0x2D` and **topped back to `0x32`**. The kernel rings were read: all four
  pages hold `magic=KBLB, stage=0xE1, nonce="KERN"` (the DTB-magic failure path).
- **On the spare:** `/accounts/devuser/nvram2`, `libgcc_s.so.1`, `memdump3` (persistent);
  `on` at `/bin/on`. `nvram2` needs `LD_LIBRARY_PATH=/accounts/devuser` and loads
  `/lib/libnvram.so.1` (stock 2.x). `/proc/boot/libnvram.so.1` is not needed for `nvram2`.
- The reference unit is not connected in this session's final state.

## 2. Proven this session

1. **CRC/factory path** (`95`): gate `desc[0xD4]==6` at media `0x100100`, CRC-32/ISO-HDLC
   over `[0,end-4)`, region 0 only, no A/B fallback; counter 50→49 on success.
2. **Live loader probe** (`100`): `0xB1` → `BS=0x10000`; `0xD9` MCT header identical to
   `hd4.bin`; partitions `0x34 MBR 0x805..0x814`, `0x1B OS 0x815..0x4804`, `0x1E NVRAM
   0x4805..0x4844`, `0x1C FS 0x4905..0xFFFFF`, `0x35` regions; **no `0x39`**; `0xDC
   {0x825,0x2C}` → `0xBB454844` (not zlib); `0xDE` → FACTORY, count 50.
3. **Pre-OS IFS reflash** (`101`): path 1, real autoloader template, `rrcq.offset=0x20`,
   `Count=44`; `PreFlash 0x20EE` → `0x20DE`; 181 × `0xF7` → `0xDF`; `0x40F9` real trailer →
   `0x4006`; `0x40C0` → `0x4015` but the write lands; media byte-identical; boot OK.
4. **State-probe chainloader** (`102`): 428 B at `0x80100000`, breadcrumbs + LED + UART;
   all four breadcrumb pages survived the recovery cycle. Entry state confirmed.
5. **Kernel chainloader** (`102`): `kernel.img` 0x7D0864, corrected CRC `0xA6FF2D25`;
   zImage at `0x2C0000`, DTB at `0x7BB348`; copies to `0x84000000`/`0x844FB348`; flashed
   (509 frames). The first attempt hit the chainloader's DTB-magic check (stage `0xE1`) and
   never entered the kernel; the constant is fixed (`0xEDFE0DD0`) and the image rebuilt.
   The corrected image still needs to be flashed and the rings read.

## 3. Immediate recovery of the spare

1. Kill the stale door holder if any (`shell_tasks`/`kill_shell`), restart:
   ```bash
   cd /home/psyden/rea-work
   python3 BerryShell-V4.py hold --key /home/psyden/rea-work/rsa --pub /home/psyden/rea-work/rsa.pub
   ```
   If the handshake keeps timing out, reboot/power-cycle the unit and retry.
2. SSH:
   ```bash
   SSHOPT="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o IdentitiesOnly=yes \
     -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedKeyTypes=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa \
     -o KexAlgorithms=+diffie-hellman-group1-sha1 -o MACs=+hmac-sha1,hmac-sha1-96 \
     -o Ciphers=+aes128-cbc,3des-cbc,aes256-cbc -o LogLevel=ERROR"
   ssh -i ./rsa $SSHOPT root@169.254.0.1
   ```
3. Verify media and counter:
   ```bash
   dd if=/dev/hd1 bs=512 skip=2048 count=5632 | ... # trim 0x2BFE10, sha256 == ifs0.sealed.bin
   dd if=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1        # expect ~0x2D..0x30; top up to 0x32
   ```
   Top-up recipe (host): `dd if=/dev/zero of=/tmp/bc32.bin bs=4 count=1; printf "2" | dd
   of=/tmp/bc32.bin bs=1 count=1 conv=notrunc; dd if=/tmp/bc32.bin
   of=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 seek=0 conv=notrunc`.
4. Read the kernel rings:
   ```bash
   on -C 0 /accounts/devuser/memdump3 90000000 0x500   # +0x80 count, +0x84 index, +0x100 chars
   on -C 0 /accounts/devuser/memdump3 94000000 0x500
   on -C 0 /accounts/devuser/memdump3 88000000 0x500
   on -C 0 /accounts/devuser/memdump3 9FE00000 0x500
   ```
   Parse the ASCII at `+0x100`; the count/index tell how much the kernel wrote.

## 4. Tooling / commands

- **USB client** `bbusb/bbusb.py` (PyUSB 1.3.1 user-local):
  ```bash
  cd ~/rea-work/bbusb
  python3 bbusb.py --boot-mode --from-bootrom --password 0000 [--debug]
  ```
  Bootrom upload: `loaders/loader_06001A06-00.bin` at `0x80100000`, 260-byte chunks.
- **Probe tool** `analysis/probe_loader.py`: waits for the bootrom, uploads `LDR_01`, runs
  `0xB1/0xB4/0xD9/0xDC/0xDE` read-only, saves to `playbook-live-spare-2026-10-25/loader-probe/`.
- **Pre-OS flash tool** `analysis/pre_os_ifs_flash.py`:
  ```bash
  python3 pre_os_ifs_flash.py --template-ifs <image> [--dry-run] [--delay N] [--reuse-loader] [--no-sig]
  ```
  It waits for `0fca:0001`, uploads `LDR_01`, `PreFlash 0x20EE`, `0xF7` frames, `0x40F9`,
  `0x40C0`, then reboots. `--delay` lets a recovery run start after the probe has run.
  `--template-ifs` handles both `0x2BFE10` images and larger (patches the V2 `rrcq` count
  and the V1 `qcfp` count/checksums; signature = base image trailer).
- **Image builders**: `analysis/build_crc_image.py` (payload at `0x800`), and
  `analysis/build_kernel_image.py` (zImage/DTB appended, LoadEnd/CRC updated).
- **Chainloader sources**: `analysis/chainloader/chainloader.S` (state probe),
  `chainloader_kernel.S.in` (kernel), `chainloader.bin`, `probe.img`, `kernel.img`.
- **QNX ARM toolchain**:
  ```bash
  Q=/home/psyden/SSDdrive/wsl-migration/home/psyden/qnx660-master
  export QNX_HOST=$Q/host/linux/x86 QNX_TARGET=$Q/target/qnx6
  export PATH=$QNX_HOST/usr/bin:$PATH
  arm-unknown-nto-qnx6.6.0eabi-as / -ld / -objcopy
  ```
- **CRC oracle**: `analysis/89-crc_tool.py` (`probe`, `verify`, `reseal`, `selftest`).
  On the extended kernel image the ECDSA trailer-shape check FAILs and the tool prints
  `RESULT: FAILED`; the structural/gate/CRC lines are the ones that matter on the factory
  path.
- **Image artifacts**: `analysis/playbook-live-spare-2026-10-25/` (`ifs0.sealed.bin`,
  `hd1-ifs-image.bin`, `sector-*.bin`, `post-flash-ifs.bin`, `nv-0x*.bin`).
- **OS packages**: `/home/psyden/Pdrive/bb10mt/2.0.0.4869-IFS.exe` (IFS), `2.0.0.4869-BASE.exe`
  (RFS), `PB-4869-root.exe` (only use its UFS; its RFS is partly corrupted).
  `bb10mt split -i <exe>` extracts `*.signed` next to the input.
- **USB timeline**:
  ```bash
  journalctl -k --no-pager -n 300 | grep -iE 'usb 3-1|0fca'
  python3 analysis/usb_watch.py        # live transitions, no root
  udevadm monitor --kernel --subsystem-match=usb
  ```
  IDs: `0fca:0001` bootrom, `0fca:8001` ramloader, `0fca:8010` OS, `0fca:8012` OS/RNDIS,
  `0fca:8020` OS/mass-storage.
- **Device/tooling reference:** `DEVICE-REFERENCE-2026-10-26.md` (shell tools, binary
  handling, `nvram2`/`memdump3`/`on`, nodes, RPMB/PPS, SSH/hold and multi-session rules).

## 5. Pre-OS reflash recipe (exact, path 1)

1. Build the container from the real `2.0.0.4869-IFS.0.signed` template: `headersz=0x10020`,
   V1 `qcfp` + V2 `pfcq(_type=8)`/`rrcq`; payload at `0x10020`.
2. Patch the V2 `rrcq.offset` to `0x20` (block `0x825` from `p34.start=0x805`); `Count` =
   image blocks (44 for the base image, 126 for `kernel.img`). For larger images also patch
   the V1 `qcfp` run0 count and its `datachecksum`/`checksum`.
3. Bootrom → `LDR_01` (password `0000`).
4. `PreFlash 0x20EE` with `[0]=0x15,[1]=0x28,[6]=0x02,[28]=0x01,[32]=0x02`; accept `0x20DE`.
5. `0xF7` frames `{u32 seq, u32 len, data}` (`0x3FEC` data max); expect `0xDF`.
6. `0x40F9` with `{u16 0x230, 560-byte real trailer}` (the base image's last 560 bytes);
   expect `0x4006`.
7. `0x40C0` COMPLETE; may return `0x4015` — the write is already on media. Do not rely on it.
8. The loader often does **not** self-reset: send `0x80EF`, then `0xEF`, or power-cycle.
9. The bootloader verifies the CRC and boots; the media readback is the authoritative check.

## 6. Chainloader / payload details

- Entry: header word 0 at `0x800FF800` is `0xEA0001FE` = ARM `b +0x800` → `0x80100000`.
- State: ARM SVC, IRQ/FIQ masked, MMU off, caches not cleared, WDT2 armed/kicked.
- Probe: `SP=0x800F0000`; disable C/I; VBAR→park; breadcrumbs at `0x90000000` + mirrors
  (`0x94000000`, `0x88000000`, `0x9FE00000`); LED = FAN5702 EN = GPIO1_13 (`0x4A310000`,
  `SET +0x194`, `CLEAR +0x190`, bit `0x2000`); UART3 `0x48020000`; WDT2 `0x4A314000`,
  kick `+0x30`; stop kicking to warm-reset in ~58 s.
- Kernel chainloader copies zImage → `0x84000000` and DTB → `0x844FB348`, then
  `r0=0, r1=~0, r2=DTB`, `pc=0x84000000`. The zImage header is 8 NOPs then `b` at `+0x20`;
  entry at base is correct. Kernel config: `CONFIG_DEBUG_PLAYBOOK_BC=y`,
  `CONFIG_ARM_APPENDED_DTB=y`, `EARLY_PRINTK`, `DEBUG_LL` via `debug/omap4bc.S`.

## 7. Lessons / gotchas

- **Factory path is region 0 only.** `0x2074` (`resetqnxosregion`) is a product-path control;
  it cannot point a CRC boot at region 1. Region 1 is ignored in factory mode.
- **Counter**: each successful factory boot decrements `BOOTCTR_FCTRY`; `0` and `>0x32` are
  fatal. Keep it topped up from the booted OS (`/dev/rpmb`).
- **`0x2801`** is `NV_OSSTORE_PasswordStruct`; both units have `A=0/B=10` (retry branch);
  password `0000` (spare 5810 iterations, reference 5060). Do not write it.
- **`bb10mt flash` cannot do this** on the spare: it uses path 2 (`_type=8`,
  `rrcq.offset=0`) and the live MCT has no `0x39`; path 1 would write the MBR. Our tool
  patches the offset.
- **Loader quirks**: `PreFlash 0x20EE` → `0x20DE` (not `0x39`); `0x40F9` must be the real
  560-byte trailer (the static `qcfm.pas` dummy yields `COMPLETE 0x4015`); `0x4015` on
  `COMPLETE` does not prevent the write; `reboot_loader` is unreliable — use `0x80EF`/`0xEF`
  or a power-cycle.
- **0xDC is not zlib CRC-32.** Use it as a relative fingerprint only; the bootloader CRC
  after boot is the real check.
- **Bootrom can wedge** after a failed attempt; power-cycle. The loader can wedge after a
  failed/partial session; power-cycle.
- **Pre-OS reflash is the recovery path**: bootrom → loader → `0xF7`. It is independent of
  region 0. Power-cycling loses DRAM; a WDT2 warm reset preserves it.
- **Breadcrumbs survive** the WDT2 reset + bootrom + loader + recovery flash + TabletOS boot;
  the kernel's DEBUG_LL rings use the same pages (`+0x80` count, `+0x100` chars).
- **Subagents**: launch read-only tracks in parallel; the main session keeps the device and
  all writes. Reports 88–92 came from the A–E tracks. The Cellebrite modified-ramloader track
  is a separate chat (`HANDOFF-cellebrite-modified-ramloader.md`).
- **USB re-enumeration is a signal**: `0fca:8001` → `0x0001` → `0x8020` → `0x8012` with
  timestamps; a 5–10 s cadence indicates a reset loop (loader/bootrom or kernel early reset).
  Capture with `journalctl -k`, `usb_watch.py`, or `udevadm monitor`.
- **SSH is tied to the BerryShell `hold`.** Plain SSH works while a hold owns the qconn door;
  the interactive `BerryShell-V3.py` auth fails while it does. Killing the hold closes the
  door and SSH is refused. If a second session is active, do not start a second hold blindly;
  check `ps`/`shell_tasks` first.
- **One USB owner.** Two sessions driving the bootrom/loader produce connect/disconnect churn
  and stale loader sessions. Hand the device over explicitly.
- **DTB magic is `0xEDFE0DD0` as a little-endian word** (file bytes `d0 0d fe ed`). The first
  kernel chainloader compared against `0xD00DFEED`, failed, and parked at stage `0xE1`; the
  corrected image is `chainloader/kernel.img`, CRC `0xA6FF2D25`.

## 8. Next steps (value order)

1. SSH: start the BerryShell `hold` (SSH depends on it); media and counter are verified
   (`c72b0530…`, counter topped to `0x32`).
2. Flash the **corrected** `chainloader/kernel.img` (CRC `0xA6FF2D25`) via the pre-OS tool;
   observe the chainloader LED (1/2/3) and recover with the same tool after the WDT2 reset.
3. Read the kernel rings (`0x90000000` etc.) and parse the ASCII. A stage `0x03` means the
   chainloader jumped; the kernel's `DEBUG_PLAYBOOK_BC` output at `+0x80`/`+0x100` then tells
   how far it got (decompressor banner, earlyprintk, panic).
4. Correlate the USB timeline with the chainloader LED stages (1/2/3/0xE1) and the rings.
5. If the rings are empty: add a chainloader eMMC/persistent log or USB; or try a smaller
   kernel/initramfs and the kexec project's T3-proven kernel.
6. Debug the kernel entry: zImage load/relocation (`0x84000000`), DTB (`0x844FB348`), the
   DTB `bootargs`/`stdout-path`, WDT2 timing, and the chainloader's copy.
7. Continue the eMMC/region-1 design (`99`, `97`) for a payload store and automatic restore.
   The pre-OS reflash already makes iteration safe.
8. The Cellebrite modified-ramloader chat (`HANDOFF-cellebrite-modified-ramloader.md`, `88`).

## 9. Suggested prompt for the next session

> **OUTDATED — this is the prompt that started the 2026-10-26 session, kept for
> history. The current next-session prompt is in
> `HANDOFF-2026-10-26-clean-kernel.md` §6.** The kernel is now a clean 6.15.11
> build on the CRC path, not the kexec image.

> Read `bootdumps-2026-09-11/analysis/HANDOFF-2026-10-26-next-session.md` first, then
> `95`, `100`, `101`, `102`, `97`, `96`, `98`, `99`, and `94`. The CRC/factory path is
> proven; the pre-OS IFS reflash via the loader's `0xF7` path 1 is proven and is the safety
> net (template container, `rrcq.offset=0x20`, `PreFlash 0x20EE` → `0x20DE`, `0xF7`
> frames, `0x40F9` real trailer, `0x40C0` may return `0x4015`, loader often needs
> `0x80EF`/`0xEF` or a power-cycle). The spare is on TabletOS but its qconn handshake is
> timing out; reboot/power-cycle, restart the BerryShell door, verify the media is
> `ifs0.sealed.bin`, check/top up `BOOTCTR_FCTRY` to 0x32, and read the kernel's
> `DEBUG_PLAYBOOK_BC` rings at `0x90000000` (count `+0x80`, chars `+0x100`) and the mirrors
> with `on -C 0 /accounts/devuser/memdump3`. The kernel chainloader (`chainloader/kernel.img`,
> zImage at image `0x2C0000`, DTB at `0x7BB348`, copies to `0x84000000`/`0x844FB348`) was
> flashed but not diagnosed; the USB timeline showed a ~5–10 s reset cadence. Keep all
> writes and the live device in the main session; use subagents for read-only tracks.

## 10. Report index

`88` Cellebrite audit · `89` IFS/CRC recipe + `89-crc_tool.py` · `90` A/B runbook ·
`91` Linux entry state · `92` RPMB runbook · `94` collation/revised plan ·
`95` CRC-path proof · `96` feedback/debug channels · `97` region architecture/live USB ·
`98` loader write/autoloader · `99` payload eMMC access · `100` live loader probe ·
`101` pre-OS reflash · `102` state probe + kernel chainloader ·
`DEVICE-REFERENCE-2026-10-26.md` device/tooling reference.
