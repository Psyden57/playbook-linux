# Session 65 (absolute) — the mailbox mutex is not the fix; ARMv7; Nura back on hd3

> Numbering: absolute `65` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 59** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-18 (one calendar day).

## Unit state at the end (what the next session inherits)

* **Region 0 = `local/work/kernel-armv7-console-cpu1.img`, CRC `0xC9FEE2D1`**
  (10,450,108 B) — the ARMv7 `#105 SMP` build (`0x158D8A85`) re-wrapped with an
  **inert** `console=ttyGS0,115200` bootarg (the gadget tty has no kernel-console
  path; verified, no `console [ttyGS0] enabled`).  Fast chainloader, loud DTB
  **without** `maxcpus`, the session-64 DSI-init TCON hook and the
  **device-mode-only mailbox workaround** in `omap2430_musb_enable()`.  Dual-core
  `online=0-1`, 1008 MHz; the 6/6 reboot set ran on `0x158D8A85` (the same
  zImage), and this image booted once for the 376 s soak.
* **hd3 = the Nura root** — the baked image
  `local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img`
  (sha256 `07555bde…`, 625,999,872 B) flashed at loader block `0x4915`
  (626,065,440 B written).  First boot grew the ext4 to **62.5 GB**
  (15,273,984 blocks).  Its modules were replaced with the ARMv7 set
  (`/lib/modules/7.3.3`, 29 MB installed, `depmod` OK); after that
  `pvrsrvkm_omap4_sgx540_120` binds, `catacomb` runs, panel connected,
  backlight 255.  dropbear SSH works without an entropy seed (`crng init` at
  13.1 s).
* **Region 1 = the BusyBox ext4**, raw `0x29000000`, sha256 `6f5b8ca7…`
  (inherited; not re-hashed) — the fallback root; nothing boots it now.
* **`BOOTCTR_FCTRY` = `0x26`** (last boot, `0x26 ≥ 0x1e`, no write).  The panic
  loop drained it `0x22 → 0x1c`; the recovery image topped it `0x1c → 0x32`
  (`PASS`).  Healthy; the image tops up below `0x1e`.
* **OS: Linux (Nura)**, not TabletOS.  `ttyGS0` gives a root shell
  (`playbook:~#`) and dropbear answers on `169.254.0.1:22`
  (`PB_LINK=usb tools/pbssh.sh`).  At wrap-up the unit is **up and on USB**
  (uptime 376 s, link alive), running the desktop.
* **No restore required.**  The way back to TabletOS is the 1917 IFS at block
  `0x825`; the vendor UFS at `0x4915` is the other half.  The BusyBox root that
  used to be on hd3 is gone (region 1 still has it).
* **Known gap:** the baked Nura image predates the session-60/61 fixes
  (`epitaph` r10005, llvmpipe, the udev/polkit rules); those live only in the
  session-59 image and the pmaports tree.

## Goal of this session

Run HANDOFF-64 §0a: five more reboots and a >5 min soak of the fixed dual-core
image, to close the dual-core item.  During the session the operator asked to
(i) restore the full mailbox call with proper serialization so OTG host mode
stays possible, (ii) fix the ARMv6 code generation, and (iii) boot the Nura root
from hd3 instead of the BusyBox root.

## Confirmed (measured, read in a file, or seen on the bench)

* **6/6 clean boots of `kernel-usbdev-cpu1.img` (`0xC675E206`)** — the power-on
  boot plus five `reboot -f` cycles: `online=0-1`, no panic, `pbrpmb` read
  `0x29→0x24` (one unit per boot, above the `0x1e` threshold).
* **The soak over USB is not possible:** with a command every 15 s the ACM read
  returned `EIO` at ~30 s and the unit vanished from the bus.  Host activity
  does not prevent the teardown.  The unit keeps running.
* **The mutex does not fix the mailbox fault.**  `omap_musb_set_mailbox()` with
  a glue mutex and the stock bind-path call (`0x19E24D8F`) panicked on the first
  dual-core boot; `panic=10` turned it into a ~10 s reboot loop (6 cycles);
  `BOOTCTR_FCTRY` drained `0x22 → 0x1c`.  Recovery: reflash `0xC675E206`, whose
  `/init` topped `0x1c → 0x32` (`PASS`, RPMB counter 12463).
* **The first mutex build (`0xBEF06B78`) booted single-core** because the tree's
  DTB still had `maxcpus=1`; the session-64 image had used a DTB with it
  removed.  The tree and tracked DTS now both omit it.
* **The kernel was built `-march=armv6k`/`__LINUX_ARM_ARCH__=6`** because
  `CONFIG_ARCH_MULTI_V6=y` → `CPU_ARM1176` → `CPU_V6K` → `CPU_32v6`, and
  `arch/arm/Makefile` lets the v6K assignment override v7 while `CPU_32v6` is
  set.  Disabling `ARCH_MULTI_V6` gives `-march=armv7-a` and vermagic
  `ARMv7 p2v8`.  `0x158D8A85` gave **6/6 clean dual-core boots**.
* **The `otg.h` `func_ptr_is_kernel_text()` guard had to go:** it breaks the
  module build (`dwc3.ko` references an unexported symbol) and it never stopped
  the Oops.
* **hd3's BusyBox ext4 was damaged** by the power cycles needed to work around
  the USB teardown: `EXT4-fs: This should not happen!! Data will be lost`,
  corrupt inode bitmap, deleted-inode references; a `/var/log` trace file was
  lost.  The Nura image was flashed over it and the filesystem re-created.
* **Nura runs on the ARMv7 kernel.**  The initramfs mounts hd3 rw, grows it on
  first boot, `lstat`s the absolute `/sbin/init` symlink and execs it.  With the
  ARMv7 modules installed the `.gnu.linkonce.this_module … must match` errors
  are zero, `pvrsrvkm 56000000.gpu` probes ("Enabling quirks 00000001"), and
  `catacomb` composites on the SGX540.
* **The USB teardown is not BusyBox-specific and not an idle timer.**  On Nura
  `VUSB` was still `enabled` at 82 s and the link survived to ~90 s with SSH
  traffic; it then dropped and did not re-enumerate.  On the final boot the ACM
  was read continuously (`cat /dev/kmsg` streamed to the host) for **240 s**,
  then left closed and idle for **90 s** — the link stayed up throughout
  (uptime 376 s), with no VUSB message.  `audio-ldo: disabling` at ~31.8 s
  occurs on both roots and is unrelated.
* **A 6-minute dual-core soak passes on `0xC9FEE2D1`:** uptime 376 s,
  `online=0-1`, no panic, gadget enumerated, `pbrpmb` `0x26`.
* **The kernel patch was re-exported** (it was still the session-58 export and
  no longer reproduced the tree): **65 files / 9,110 lines**, sha256
  `6f9bef24…`, **0 rejects / 65/65 byte-identical** against pristine 7.3.3.
  It carries the `omap2430.c` workaround + mutex, the DSI-init TCON hook and the
  DTS change; the session-62/63/64 diagnostics are excluded (report 263 §5).

## Inferred (reasoning from evidence, not directly measured)

* **The mailbox fault is corruption of the `usb_phy`/`usb_otg` object, not (only)
  a two-caller race** — moderate confidence.  A mutex removes the race and the
  fault remains; the session-64 observation of a slab address in
  `otg->set_vbus` fits a stale/freed object better.  The next instrument is an
  entry marker (CPU, `glue`, `musb`, `xceiv`, `otg`, `set_vbus`,
  `notifier.head`) with `panic=0`.
* **The VUSB teardown is a spurious/incorrect `VBUS_DET` read in the TWL6030
  comparator IRQ** — moderate confidence; it is the only path that calls
  `regulator_disable(usb3v3)`.  Whether userspace (charger module/probe) or the
  PMIC itself causes it is open.
* **ARMv7 codegen is the right default for this unit** — high confidence: the
  SoC is Cortex-A9, the ARMv6 path was a config artefact, and 6/6 boots plus the
  Nura userland work.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"Serializing the two mailbox callers fixes the dual-core wedge."**  It does
  not; the panic survives the mutex.  [report 263 §1]
* **"A soak with host activity every ~20 s keeps the gadget alive."**  It does
  not on the BusyBox root (dies at ~30 s) and only delays it on Nura (~90 s).
* **"The tree's DTB is the dual-core DTB."**  It carried `maxcpus=1`; a fresh
  build boots single-core until that line is removed (and the tracked copy
  updated).
* **"The kernel is ARMv7 because the SoC is."**  It was ARMv6K because of
  `ARCH_MULTI_V6`; the vermagic said so all along.
* **"The `otg->set_vbus` text guard is harmless."**  It breaks `dwc3.ko` at
  modpost (unexported symbol) and was ineffective.
* **"A tracer file on the rw root survives the power cycle."**  It did not — the
  ext4 was already damaged.

## Unknown / open questions

* **What exactly faults inside `omap_musb_set_mailbox`?**  Needs the entry
  marker with `panic=0` and the ring (or the panel) — the mutex result rules out
  the pure-race explanation.
* **What disables VUSB, and when?**  It did not happen on the final 376 s boot
  despite 240 s of ACM reads and 90 s idle, so it is intermittent.  Only
  `twl6030_usb_irq()` calls `regulator_disable(usb3v3)`; a capture that starts
  before the event and survives it (or the panel) is still needed.  The
  `twl6030_charger` module does probe ("vmax -22 imax: -22").
* **Is the eMMC write stall (`Card stuck being busy`, lost WRITE at 27 s) going
  to bite the rw Nura root?**  It happened during the first-boot resize burst
  and the FS survived; the open item remains (report 250 §1).
* **The MPU OPP** — `twl6030_uv_to_vsel: OUT OF RANGE` (1.375/1.41 V vs max
  1.31666 V) and no `cpufreq` policy.  Report 257 §6 still applies.
* **Host mode/OTG** — the hardware side (ID pin wiring, TWL VBUS boost) is
  untested; the kernel side is `CONFIG_USB_MUSB_DUAL_ROLE=y` with
  `mode = <2>` in the DTS.

## Superseded / uncertain

* **The session-64 "keep the mutex-serialized mailbox" plan** (suggested during
  this session) is withdrawn: the mutex does not fix the fault.  The
  device-mode-only workaround stays until the real cause is known.
* **The handoff's "soak >5 min with host activity every ~20 s"** is not a
  viable procedure on this image; the USB link cannot be held open.

## Artefacts produced

* Report **263** (`docs/reports/263-mutex-does-not-fix-musb-armv7-and-nura-2026-11-18.md`).
* Traps **334–337** added, trap **333** corrected; DECISIONS §94–§96.
* Host captures (gitignored): `local/work/soak65-20261118-183036.log`,
  `local/work/armv7-boots.log`; the tracked captures
  `docs/captures/playbook-live-2026-11-18-nura-armv7-boot.txt` and
  `docs/captures/playbook-live-2026-11-18-nura-kmsg-stream.txt` (working copies
  `local/work/dmesg-nura-1924.txt`, `local/work/kmsg-drop2-*.log`); the flash
  logs `flash-mutex-*`, `flash-mutex2-*`, `flash-recover-*`, `flash-armv7-*`,
  `flash-armv7con-*`, `flash-nura-*`.
* Helpers: `local/work/soak65.py` (reboot+soak driver),
  `local/work/kmsgcap.py` (ACM capture), `local/work/modstage/` +
  `modules-7.3.3-armv7.tar.gz`.
* Images: `kernel-armv7-console-cpu1.img` (`0xC9FEE2D1`, current),
  `kernel-armv7-cpu1.img` (`0x158D8A85`),
  `kernel-usbdev-cpu1.img` (`0xC675E206`), `kernel-mutexmailbox-cpu1.img`
  (`0x19E24D8F`, rejected).
* Tracked: `kernel/patches/0001-playbook-winchester-7.3.3.patch` (65 files /
  9,110 lines, sha256 `6f9bef24…`) and `kernel/patches/README.md`,
  `kernel/winchester-7.3.3.config` (SMP/ARMv7),
  `kernel/source/omap4-winchester.dts` (no `maxcpus`), this note, report 263.
