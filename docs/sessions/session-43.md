# Session 43 (absolute) — a minimal 1917 oracle; AUXCLK1 proven as MCLK1; the Linux fault is below the registers

> Numbering: absolute `43` (the project's chronological scheme — see
> `NUMBERING.md`).  This is Linux-era **session 37** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-12 (single-day session).
Unit state at the end: **on our Linux image** `boot/chainloader/kernel.img`,
CRC **`0x36597040`** (0x93A2EC bytes — re-read from the file's trailer this
session); region 1 = the audio/OSS rootfs sha256 **`b20302dc…d488`**; console
`pb> ` on `ttyACM0`; no door.  `BOOTCTR_FCTRY` read **`0x32`** (low byte `'2'`)
by the operator on the oracle boot; the Linux `/init` tops it up at boot.

## Goal of this session

The operator's direction: stop guessing at the WM8994 MCLK1 fault from Linux and
build a heavily debloated 2.1.0.1917 QNX image as a **live hardware oracle** —
boot fast, give a shell over USB (qconn/`QCL.py`), and let us run the
reference's own HAL and read the hardware while it runs.  "Done" = a minimal
bootable image reachable over the network, then its first diagnostic use on the
MCLK1 question.  (The unit was not restored; the operator chose to stop.)

## Confirmed (measured, read in a file, or seen on the bench)

* **The minimal 1917 oracle is built, flashed and boots.**  Service set reduced
  to `filesystem pps random pty net powerman phyctrl usbmgr rimusb`; 224.5 MiB
  of GUI/app/font/browser/DRM subtrees pruned (live `/base` 314.9 → 90.4 MiB);
  it enumerates as `0fca:8012` and answers `tools/QCL.py` at
  `169.254.0.1:9090` and root `sshd`.  Stage A and Stage B both booted.
  [`docs/reports/207-minimal-1917-oracle-2026-11-12.md`; `tools/build_minimal_qnx.py`]
* **On the live reference the WM8994 FLL1 is locked** (`R0x732 = 0x6020`) with
  `AUXCLK1 = 0x00020100`, pad `0x19C = 0x41000000`, and the FLL file
  `0x220=0x0005, 0x221=0x0700, 0x222=0xAE14, 0x223=0x00E0, 0x224=0x0C80`.
  [`docs/captures/wm8994-oracle-live-2026-11-12.txt`]
* **AUXCLK1 is causally the codec's MCLK1.**  On the live reference: clear the
  AUXCLK1 gate (`0x00020100 → 0x00020000`), reset the codec (`R0x000=1`), run
  its exact FLL1 power-up → `R0x732 = 0x6000` (no lock); restore AUXCLK1 and
  restart `io-audio` → `0x6020`.  [report **208**]
* **Linux programs the exact reference FLL file during a stream** (`R0x200 =
  0x0011`, `0x220=0x0005 … 0x224=0x0C80`) yet `R0x732 = 0x6000`; its
  `AUXCLK1 = 0x00020100` and fref pad low half `0x0000` are identical to the
  reference.  [`docs/captures/wm8994-linux-capture-2026-11-12.txt`]
* **The codec freeze detector proves MCLK1 is absent under Linux** (thaw,
  select MCLK1 + ENA, test write dropped), and the detector was **validated on
  the reference** (same sequence, write lands).  [capture; report 208 §2]
* **No AUXCLK1 source produces MCLK1 on Linux** (sys_clkin / dpll_core_m3x2 /
  dpll_per_m3x2 / gate-off all frozen).  [capture]
* **Broad register diff done.**  Differences found: `PMICSETUPTIME` (0x4A30A104),
  `AUXCLK4` (0x4A30A320), pads `0x190/0x194/0x198/0x19C` high halves, CM2
  PER-PLL dividers (`0x14C–0x15C`), `CM_ABE_PLL_REF_CLKSEL` (0x4A30610C:
  reference 0 = SYS_CLK, Linux 1 = SYS_32K), and PRM IRQ/CAM registers.  **All
  were tested on the reference/Linux and do not restore MCLK1.**
  [`docs/captures/wm8994-linux-capture-2026-11-12.txt`]
* **`CM_SYS_CLKSEL` = 7 (38.4 MHz) on both**, and `CM_ABE_PLL_REF_CLKSEL` is the
  real audio-path difference: our Linux image references DPLL_ABE to 32 kHz
  (the reset default), the reference to SYS_CLK.  [capture; TRM tables 3-390]
* **Tools:** the WM8994 write ABI is the reference's 20-byte inline `devctl`
  struct (`{addr,2,4,1,reg,val}`, big-endian); `tools/pbi2c` now has `wr16r`.
  `memdump3`, `memw32`, `pbi2c` live in `/accounts/devuser/bin` (UFS) and
  survive reboots.  [report 208 §5; `tools/qnx-i2c.c`]
* **The UFS `/accounts/devuser` automation was cleaned**: `startup.sh` reduced
  to top-up + dev mode + qconn + sshd; `restart-launcher.sh`, `sshd-watchdog.sh`,
  `symlink.sh` removed; our `rsa.pub` installed for root ssh.  [report 207 §1;
  `local/work/minimal/devuser-startup.sh`]
* **Region 0 is the Linux image CRC `0x36597040`** (verified from the file's
  trailer) and region 1 the rootfs `b20302dc…` (sha256 re-read this session).

## Inferred (reasoning from evidence, not directly measured)

* **The MCLK1 fault is below the readable registers.**  *Strong.*  Every
  register that could plausibly gate AUXCLK1 matches the reference or is proven
  irrelevant, and the codec detector (validated on the reference) says MCLK1 is
  absent on Linux.
* **The reference's chain is AUXCLK1 (sys_clkin/3) → fref_clk2_out → codec
  MCLK1.**  *Strong.*  The TRM maps `fref_clk1_out` to AUXCLK0 (so AUXCLK1 →
  fref_clk2_out), the pad `0x19C` low half is the codec pad, and the causality
  test gates it.
* **`CM_ABE_PLL_REF_CLKSEL = 32k` is a genuine audio bug in our image.**
  *Strong* (register read), but **not** the MCLK1 cause (setting it to SYS_CLK
  on Linux did not unfreeze the codec).
* **The Linux driver's idle `R0x200 = 0x0001` (frozen) is a driver wart**, not
  the root cause: during a stream the driver thaws and writes the correct FLL
  file.  *Strong.*

## Dead ends and wrong assumptions (valuable — do not skip)

* **`rcfs_extract.py` on the OS 2.x `.rcfs`.**  Wrong tool: an OS 2.x `.rcfs` is
  a QNX6 filesystem; use `bb10mt qnx6 mount` (operator correction, twice).
* **Rebuilding the QNX6 filesystem from a copied tree.**  The operator's rule
  is better: copy the `.rcfs`, mount the copy read-write, edit through the
  mount, unmount.
* **`devctlv`-iovec writes to the WM8994.**  They fail; the reference driver
  uses a 20-byte inline `devctl` struct.  Disassembly of `codec_write` plus a
  successful `wr16r` test disproved the iovec assumption.
* **`slay -f sshd-watchdog.sh`.**  No-op (slay matches the executable `/bin/sh`);
  the script lived on.  Match the argument via `pidin arg` and kill the PID.
  Killing the parent also took its `sshd -D` child.
* **"`launcher-secure` is qconn's remote-exec helper."**  Wrong: it is the
  TabletOS GUI/app launcher, started by `/accounts/devuser/restart-launcher.sh`
  (operator correction; `pidin arg`).
* **"A bare FLL disable/re-enable with AUXCLK1 gated off proves the clock."**
  No: the lock bit froze at `0x6020` until the codec was reset — the detector
  needs its reference clock to update.  Only **reset + full power-up** is a
  valid test.
* **"`dd` comes from the symlink overlay."**  `/base/bin/dd` exists; the overlay
  is not needed for the top-up (operator + `ls`).
* **"Pruning shrinks the flash container."**  It does not: `bbusb.py` packs only
  non-`0xFF` runs and the freed blocks are not `0xFF`; Stage A and Stage B both
  packed to 363,790,928 B.
* **"`bb10mt qnx6 compact` can shrink the image."**  It is broken/incomplete and
  corrupts files (operator; trap 175).
* **"The differing pads / `PMICSETUPTIME` / `AUXCLK4` / `CM_ABE_PLL_REF_CLKSEL`
  cause MCLK1."**  Each was set to the Linux value on the working reference (or
  the reverse on Linux) and the FLL stayed locked / the detector stayed frozen.
* **"The codec is frozen at idle under Linux because MCLK1 is absent."**  True but
  a *symptom*: the idle `R0x200=0x0001` freezes because AIF1CLK from the
  (unlocked) FLL is stopped.  The stream-time FLL programming is correct.

## Unknown / open questions

* **Why is the AUXCLK1 output absent under Linux with identical registers?**
  *Would settle it:* compare the PRM power-domain status (`PM_CORE_PWRSTST`
  `0x4A306704`, `PM_WKUP_PWRSTST` `0x4A307704`, `PM_L4PER_PWRSTST`
  `0x4A307404`, `PM_ABE_PWRSTST` `0x4A306504`) and the CM clock-domain control
  between the two, then force the differing domain; or a board probe (trap 165).
* **Is MCLK1 dead or marginal?**  *Would settle it:* the probe, or repeated
  direct tests across boots.
* **Does the ABE-PLL reference fix restore the ABE/McBSP audio path?**
  *Would settle it:* set `CM_ABE_PLL_REF_CLKSEL = 0` in the DTS, rebuild, boot,
  read `0x4A30610C`, and try the ABE/McBSP path.
* **Does the reference's `startup-playbook` do anything else to AUXCLK1's
  output path (PRM/domain) that our Linux boot does not?**  *Would settle it:*
  the power-domain/CM comparison above.

## Superseded / uncertain

* **Report 206's "the pin does not clock" (a register-equality argument)** is now
  strengthened to a *causal* result: AUXCLK1 is the codec's MCLK1 (report 208)
  and MCLK1 is measurably absent on Linux with a validated detector (capture).
  Report 206's measurements stand; the model is confirmed, not replaced.
* **"The Linux HAL/GPIO/i2c configuration is likely wrong"** (the operator's
  working hypothesis) is **largely disproven**: the driver writes the exact
  reference FLL file during a stream, I²C works, and the AUXCLK1/pad config
  matches.  The one real clock-definition bug found is
  `CM_ABE_PLL_REF_CLKSEL` (not MCLK1).
* **"A pruned image changes the flash byte count."**  Superseded: pruning
  changes the live filesystem only (trap 171).

## Artefacts produced

* **Report 207** — `docs/reports/207-minimal-1917-oracle-2026-11-12.md`.
* **Report 208** — `docs/reports/208-oracle-auxclk1-causality-2026-11-12.md`.
* **Captures** — `docs/captures/wm8994-oracle-live-2026-11-12.txt`,
  `docs/captures/wm8994-linux-capture-2026-11-12.txt`.
* **`tools/build_minimal_qnx.py`** — the reproducible oracle builder.
* **`tools/qnx-i2c.c`** — added the reference WM8994 write ABI (`pbi2c wr16r`).
* **`local/work/minimal/devuser-startup.sh`** — the cleaned UFS `startup.sh`
  now installed at `/accounts/devuser/startup.sh`.
* **Images (gitignored)** — `local/work/minimal/rcfs-stageA.rcfs` (sha256
  `ed8ac3ce…`), `rcfs-minimal.rcfs` (`b931a6d4…`), `setB.signed`
  (`d22eab3b…`), `wayback.signed` (`dd03fb8b…`); `local/work/ifs-shim-sealed.ifs`
  (`b52195bb…`).
* **UFS device files** — `/accounts/devuser/bin/{memdump3,memw32,pbi2c}`,
  cleaned `/accounts/devuser/startup.sh`, `.ssh/authorized_keys` (our pubkey);
  launcher/watchdog/symlink scripts removed.
* **Media** — region 0 Linux image CRC `0x36597040` (0x93A2EC, sha256
  `53c9fde0…`); region 1 rootfs `b20302dc…` (67,108,864 B).
