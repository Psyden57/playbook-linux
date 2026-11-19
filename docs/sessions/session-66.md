# Session 66 (absolute) — the mailbox fault's bad indirect transfer is identified

> Numbering: absolute `66` (the project's chronological scheme — see
> `NUMBERING.md`).  The reports call this **Linux-era session 60**
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-19 (one calendar day).
Unit state at the end: Linux/Nura on hd3, region 0 restored to `0xC9FEE2D1`
(ARMv7 `#105 SMP` dual-core), `BOOTCTR_FCTRY` not re-read this session (last
known `0x26`); USB link dropped at ~60 s after the restore boot (trap 337),
unit still running.

## Goal of this session

Run HANDOFF-65 §0d's one experiment to identify the bad indirect transfer in
`omap_musb_set_mailbox()`: remove the winchester early return (keep the
`set_mailbox_lock` mutex), add `printascii()` markers at entry and before the
notifier chain, set `panic=0`, rebuild/flash region 0, boot, and read the ring.
"Done" = know which pointer is corrupt, so the next step can be targeted.  The
operator also asked for a thorough wrap-up and for the unit to be left on the
working image.

## Unit state inherited and left behind

* Inherited: region 0 `0xC9FEE2D1` (ARMv7 dual-core, device-mode-only mailbox
  workaround), region 1 `6f5b8ca7…` (BusyBox fallback, not booted), hd3 the
  Nura ext4 `07555bde…` grown to 62.5 G with ARMv7 modules, `BOOTCTR_FCTRY
  = 0x26`, 65 files / 9 110 lines patch (`6f9bef24…`).
* **Left behind:** region 0 **restored to `0xC9FEE2D1`** after the experiment
  (the marker/read-back images were temporary).  Region 1 and hd3 untouched —
  no hd3 flash this session, so no 626 MB write and no ext4 power-cut risk.
  The USB link dropped ~60 s after the restore boot (the intermittent VUSB
  teardown); the unit keeps running on its panel console.  **No restore to
  TabletOS required.**
* Device-side files: none deployed persistently.  `/tmp/ringdump` and the raw
  ring dumps were pushed to the *temporary read-back image* and are gone with
  it (`/tmp` is tmpfs).  Nothing was installed on hd3.

## Confirmed (measured, read in a file, or seen on the bench)

* **Entry pointers are valid.**  `PBMBX entry cpu=0 status=4 … xceiv=c2838440
  otg=c1fd34c0 sv=c0729b94 nh=00000000`; `0xc0729b94` resolves to
  `omap_usb_set_vbus` in `local/kernel-tree/System.map`.  [capture
  `docs/captures/musb-mailbox-fault-2026-11-19.txt`; report 264 §2]
* **`sv` stays valid through the branch markers** (`pre-periph`,
  `post-periph`, `pre-setvbus` all `sv=c0729b94`), then the Oops follows
  immediately; there is no `PBMBX prechain` line.  [same capture; report 264 §2]
* **The faulting transfer is `phy->comparator->set_vbus`, not `otg->set_vbus`.**
  Oops `PC = 0xc2c05800`, decoded by the kernel as `slab kmalloc-1k … offset 0`;
  `LR = omap_musb_set_mailbox+0x1e0/0x4bc`.  Disassembly of `+0x1e0` is the
  `blx r6` in inlined `otg_set_vbus()`, and `omap_usb_set_vbus()` is a tail-call
  chain whose last `bx r3` uses `comparator->set_vbus`.  [`arm-linux-objdump -d
  local/kernel-tree/vmlinux`; report 264 §3]
* **Build 1 faulted the same way** at `0xc2d49400` (also `kmalloc-1k`), so the
  bad-pointer class is reproducible, not a one-off.  [capture, CAPTURE 1;
  report 264 §2]
* **`panic=0` worked as intended**: the unit hung for capture instead of the
  trap-335 boot loop, and `BOOTCTR_FCTRY` was not drained by a loop this
  session.  [two warm resets + read-back flashes; report 264 §1]
* **The read-back capture procedure works on this image**: hung boot → warm
  reset → flash `kernel-readback-quiet.img` (`0x5BBCD22E`) → SSH → push
  `ringdump` → `tools/pb_ring.py decode --boot prev`.  [flash logs, `r2-*.bin`]
* **The device-mode-only workaround image boots**: region 0 restored to
  `0xC9FEE2D1`, operator saw the Nura desktop come up after a few reset
  cycles; the link then dropped at ~60 s while the unit kept running.  [flash
  `flash-restore-session66.log`; operator observation; trap 337]
* **The build tree is reverted and proven so**: `omap2430.c` and the DTS match
  their pre-experiment backups byte-for-byte, rebuilt `vmlinux` has 0 `PBMBX`
  strings, DTB bootargs back to `panic=10`.  [sha256 `c57c3e24…` / `46dffeb2…`;
  rebuild output]

## Inferred (reasoning from evidence, not directly measured)

* **The `struct phy_companion` in the `omap-usb2` PHY is stale/corrupt** —
  moderate confidence.  `comparator->set_vbus` should be the text pointer
  `twl6030_set_vbus` installed by `twl6030_usb_probe()`; a `kmalloc-1k` address
  is what freed/reused memory looks like.  The alternative — an in-call write
  clobbering the word — is not excluded, but `sv` (a neighbouring word in the
  same `otg` object) is stable across the call.  Report 264 §5.
* **The session-64 "slab address in `otg->set_vbus`" was likely this nested
  pointer**, mis-symbolised through the inlined call chain — low/moderate
  confidence, consistent with both Oops PC values being `kmalloc-1k`.
* **The `twl6030_charger` module probing with `vmax -22 imax: -22` (report 263
  §4a) is a clue** that the PMIC/charger block is touched by more than one
  driver, which could rebind the comparator.  Speculative; a static audit
  would settle it.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"A wild `xceiv`/`otg`/`set_vbus` at entry."**  False: every pointer read at
  entry is valid and `sv` is correct kernel text.  [PBMBX entry marker, report
  264 §2]
* **"The notifier chain (`nh`) is the bad transfer."**  False: no `PBMBX
  prechain` marker printed; the fault is before it.  [capture]
* **"The mutex serialization fix just needs the stock call restored."**  That
  is what this experiment tested; it still panics (as session 65 found).  The
  useful output is the *identity* of the faulting pointer, not a fix.  [reports
  263 §1, 264]
* **"A rebuilt image with reverted sources should reproduce `0xC9FEE2D1`
  byte-for-byte."**  False: the kernel version string carries a build
  timestamp (`#107` vs `#105`), so the CRC changes (`0x0C58475B`) even with
  identical sources.  The revert was therefore proven by source hashes +
  marker-string absence + bootargs, not by CRC.  I did not flash the rebuilt
  image; `boot/chainloader/kernel.img` was reset to the saved `0xC9FEE2D1`
  file.  [build output, `sha256sum`]
* **"The unit stays on USB until asked."**  Not this session: after the restore
  boot the link dropped at ~60 s (trap 337).  The panel console remains the
  recovery path.  [lsusb poll log, operator observation]

## Unknown / open questions

* **Why is `comparator->set_vbus` a slab address?**  Audit `omap_usb2_set_comparator()`
  / `twl6030_usb_probe()` lifetimes (report 264 §5).  Settled by reading the
  comparator registration and any probe-deferral/rebind paths.
* **Does `cpcap-charger.c` (`omap_usb2_set_comparator(NULL)`) clobber the
  pointer on this board?**  Static check + a `phy->comparator` marker.
* **What drives the intermittent VUSB teardown?**  Still open (trap 337,
  `docs/contradictions/usb-vusb-drop-idle-vs-intermittent.md`); this session
  saw the drop again but did not capture the TWL IRQ.  A `VBUS_DET`/linkstat
  log in `twl6030_usb_irq()` would settle it.
* **`BOOTCTR_FCTRY` value after this session.**  Not re-read (the USB link was
  down at the close).  Last known `0x26`; the Linux `/init` tops up below
  `0x1e`.  Read `dmesg | grep pbrpmb` on the next SSH-capable boot.
* **MPU OPP**, memory read throughput, UI perf — unchanged from session 65.

## Superseded / uncertain

* **HANDOFF-65 §0d's decision tree** is refined, not overturned: the "wild
  pointer at entry" branch is ruled out at the `musb->xceiv` level; the
  corruption is in the nested `phy_companion`.  Report 264 is current.
* **The session-64 `func_ptr_is_kernel_text()` guard on `otg->set_vbus`**
  (report 263 said it was reverted because it broke `dwc3.ko`): even if it
  built, it would not have caught this fault — the faulting word is
  `comparator->set_vbus`, one level below the guard.  The revert stands.
* Nothing else this session contradicts a newer document.

## Artefacts produced

* Report **264** (`docs/reports/264-musb-mailbox-bad-indirect-transfer-2026-11-19.md`).
* Capture `docs/captures/musb-mailbox-fault-2026-11-19.txt` (decoded ring,
  both builds); raw dumps `local/work/r2-session66{,-m2}.bin`.
* Images (gitignored): `kernel-mbxmark-panic0.img` (`0xF1AA4568`),
  `kernel-mbxmark2-panic0.img` (`0x405E8997`); read-back
  `kernel-readback-quiet.img` (`0x5BBCD22E`); restored working image
  `kernel-armv7-console-cpu1.img` (`0xC9FEE2D1`) back on region 0.
* Experiment sources/backups (gitignored): `local/work/omap2430.c.session66-{orig,markers}`,
  `local/work/omap4-winchester.dts.session66-{orig,panic0}`; build tree reverted.
* Flash logs (gitignored): `local/work/flash-session66.log`,
  `flash-readback-session66.log`, `flash-session66-m2.log`,
  `flash-readback-session66-m2.log`, `flash-restore-session66.log`.
* No tracked DTS/driver or patch change: the experiment was reverted; the
  export stays 65 files / 9 110 lines (`6f9bef24…`).  Traps 338 added;
  DECISIONS §97; STATE/HANDOFF/ROADMAP/KNOWN-ISSUES updated.
