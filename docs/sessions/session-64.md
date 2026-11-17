# Session 64 (absolute) — the dual-core wedge is the MUSB gadget bind; dual-core boots cleanly with a targeted fix

> Numbering: absolute `64` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 58** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-17 (one calendar day, extended).

## Unit state at the end (what the next session inherits)

* **Region 0 = `local/work/kernel-usbdev-cpu1.img`, CRC `0xC675E206`** — the
  `#102 SMP` zImage with the fast chainloader, the no-`nosmp` loud DTB
  (`earlyprintk keep_bootcon ignore_loglevel clk_ignore_unused panic=10
  fw_devlink=permissive`), the DSI-init TCON hook, and the **targeted MUSB
  device-mode fix** (report 262).  Trailer `1f2dc8d7 06e275c6` read when flashed.
  Dual-core `online = 0-1`, 1008 MHz, root `/dev/loop0` ext4 on hd3.
* **Region 1 = the busybox ext4**, raw `0x29000000`, sha256 `6f5b8ca7…`
  (inherited from session 63 — *not re-hashed this session*; modules mismatch,
  trap 320).  **hd3** = the same ext4 at `0x4915`, the mounted rw root.
* **OS: Linux**, not TabletOS.  dropbear on `169.254.0.1:22` after a
  `pbconsole entropy <hex>` seed (trap 328); ACM `pb> ` on ttyGS0 is the
  fallback.  At wrap-up the unit is **off USB** after ~32 s idle: the kernel
  logs `VUSB: disabling` / `audio-ldo: disabling` and the gadget deactivates
  (idle runtime PM, not a wedge — the unit keeps running with its panel
  console).  Cable replug did not re-enumerate; this is a separate PM matter.
* **`BOOTCTR_FCTRY = 0x2c`** at the last boot (`2c000000`, above the `0x1e`
  threshold, no write) — self-topping and healthy after the session's many
  wedge cycles (it was read at `0x31`, drained to `0x18`, topped to `0x32`,
  then `0x29`/`0x2d`/`0x2c`).
* **Device-side files**: `/tmp/ringdump` (pushed repeatedly, tmpfs — ephemeral).
  All ring dumps were pulled to the host.  **No restore is required**; the 1917
  IFS at block `0x825` is the way back if TabletOS is wanted.
* The gitignored kernel tree carries experiments (DSI TCON work, MUSB device-mode
  fix, otg-text guard, MINI4x6 font linkage); the **tracked** `initramfs/init.c`
  was reverted to clean (the CPU-affinity experiment is rejected).

## Goal of this session

Start with HANDOFF-63 §0a: capture the ring from a wedged dual-core boot and read
the `PBSMF`/`PBIPI1` IPI markers.  The goal expanded during the session to
identify and fix the dual-core wedge, using reference RE, panel diagnostics,
and one-variable experiments.  "Done" became a dual-core image that boots
reliably with the USB gadget working.

## Confirmed (measured, read in a file, or seen on the bench)

* **The wedge IPI is sent, received and serviced.**  Ring capture of a wedged
  `kernel-ipidbg-cpu1.img` (`0xF8264016`) boot via the quiet read-back image:
  full `PBCPU1:` bring-up, `online cpus=02`, a `PBSMF: pre`/`post csd_lock_wait`
  pair, ~170 `PBIPI1: CALL_FUNC enter`/`done` pairs, and four more clean
  `enter`/`done` pairs after `PBCPU: pre run_init(/init)` before silence.
  Kernel entry 3.299 s of 7.812 s; power ruled out.  [report 258 §2/§3;
  `local/work/r2-64-{prev,all}.txt`]
* **The reference's core-up and PMIC order** (report 259, static RE): the 1917
  IFS `.script` runs `setup-core-inactive full` first (PRCM HAL,
  `prcm_vc_bypass_write 0x3a` = our 1.35 V VSEL), before I²C drivers;
  `pmic_twl6030_cfg` comes later.  Both cores come up via `procnto-smp-instr` +
  `startup-playbook`'s `cpu_startnext`; no IFS CPU1-start or delay.  Evidence:
  the `.script`, shipped disassembly, reports 192/219.
* **`cpu_startnext` decompiled** (report 260, Ghidra via REA CLI + `pyghidra`):
  SMC 0x104/0x105/**0x108** (`OMAP4_MON_SCU_PWR_INDEX`, args 0/0xff) + SEV.
  Our kernel omits 0x108.  Replicating it did not improve reliability.
* **The black panel on wedges was `loglevel=0` + TCON in `/init`** (report 261):
  `fbcon.c:1113` suppresses Tux when `console_loglevel <= 4`; a loud dual-core
  image wedged black because `panel_tcon_init()` never ran.  A retrying delayed
  work off `dsi.c`'s `pb_bridge_program()` (report 261, `kernel-dsitcon-cpu1.img`
  `0x445D5D60`) put the console on the panel and **revealed a kernel panic**.
* **The panic is in the MUSB gadget bind** (report 262).  Operator screenshots:
  `omap_musb_set_mailbox` from `musb_start`, from `musb_gadget_start`, from
  `gadget_dev_desc_UDC_store` (PID 1 writing the UDC via configfs); `Code:
  00000000 … (00090000)`; PID 1 dies with irqs disabled → `Attempted to kill
  init!`.  A second mode was a hang at `random: pbrpmb: uninitialized urandom
  read`.  Disabling `gadget_setup()` entirely (`kernel-nogadget-cpu1.img`
  `0xF0027A02`) gave **no wedge** (>100 s up, console live) — proving the
  trigger.
* **Targeted fix works**: call only `omap_control_usb_set_mode(USB_MODE_DEVICE)`
  from `omap2430_musb_enable()` on winchester, skipping the faulting
  `omap_musb_set_mailbox()` (report 262).  `kernel-usbdev-cpu1.img`
  (`0xC675E206`) gave **3/3 clean dual-core boots**, `online=0-1`, gadget up,
  `pbrpmb topup` OK (`0x2c`, no hang), SSH after entropy seed.  Baseline ~1/3.
* **The ~32 s USB drop is idle runtime PM**, not a wedge: operator captured
  `VUSB: disabling`, `audio-ldo: disabling`, gadget deactivated while the unit
  kept running.  Same with or without the mailbox change.
* **Ghidra MCP diagnosis**: the server lacked `GHIDRA_INSTALL_DIR`; `doctor`
  is healthy with it set.  Worked around via REA CLI + bundled `pyghidra`.

## Inferred (reasoning from evidence, not directly measured)

* **The gadget bind is the common trigger** for both observed failures — high
  confidence (no-gadget image never wedges; targeted fix avoids both).  The
  underlying bad indirect call inside `omap_musb_set_mailbox` is not pinned
  down: a `func_ptr_is_kernel_text()` guard on `otg->set_vbus` did not stop the
  Oops, so another call in that function faults.  [report 262]
* **The "clean `execve` hang" of report 258 was an incomplete read** — high
  confidence: the panel shows a real Oops/panic, and earlier ring captures had
  no display so the panic text was not visible.  [reports 261/262]
* **Multi-second boot delays and CPU pinning are not the answer** — moderate
  confidence: a 1 s gadget delay, a PID-1 CPU0 affinity pin, and the SMC 0x108
  replica all failed to change the failure.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The wedge is a TLB-flush IPI race at `execve`."**  The markers show the IPI
  is serviced; the real fault is the USB gadget bind.  [reports 258 → 262]
* **"A wedged boot shows no console, so capture the ring."**  True only while
  the panel is unprogrammed; once a DSI-init TCON hook exists, the panic is
  visible live.  [report 261]
* **"Replicate the reference's SMC 0x108 / PMIC order and it will boot."**
  Tested — 0/3, no improvement.  [report 260]
* **"Delay gadget setup / pin PID 1 to CPU0 / guard `otg->set_vbus`."**  All
  three failed; the Oops persisted (or changed only in timing).  [report 262]
* **"Skipping the whole mailbox is a fix."**  4/4 boots but USB eventually
  drops; the targeted device-mode-only call is better.  [report 262]
* **"`echo <64 hex> > /dev/random` seeds dropbear."**  The ACM wraps at ~80
  columns and split the command; short writes credit ~21 bits each.  Use
  `/bin/pbconsole`'s `entropy <hex>` via `tools/pbraw.py` (trap 328).
* **Screenshot OCR** misread the kernel version as `4.14.78`; the built
  `vmlinux` says `7.3.3 … #96/101/102` with the same build timestamp.

## Unknown / open questions

* **Which indirect call in `omap_musb_set_mailbox` jumps to the zeroed page?**
  Needs a full Oops header from a ring capture taken on an *Oops* boot (earlier
  captures landed on hang boots), or a per-instruction marker.  [report 262]
* **Is the targeted fix safe long-term?**  3/3 boots is a small sample; a longer
  soak and a USB plug/unplug test are outstanding.
* **Is the idle `VUSB: disabling` a problem?**  Normal runtime PM or a real
  regression; check `power/autosuspend` and whether a host wake restores it.
* Would `CONFIG_NR_CPUS=1` (trap 327) boot reliably?  Untested.
* Is 1008 MHz thermally stable under sustained dual-core load?  Only minutes.

## Superseded / uncertain

* **Report 258's "clean `execve` hang"** and the inference that the hang is
  downstream in `flush_old_exec`/`mmput` are superseded by reports 261/262:
  it is a real MUSB gadget-bind Oops/panic.  Report 262 is current.
* The session-64 handoff's "next: CPU0-only flush_old_exec markers" is obsolete;
  the MUSB device-mode fix is the current next step (HANDOFF.md).

## Artefacts produced

* Reports **258–262** (`docs/reports/258-* … 262-*`), covering the IPI markers,
  reference core-up/PMIC order, `cpu_startnext` decompile, panel/TCON timing,
  and the MUSB fix.
* Traps **332** (dual-CPU `printascii` race) added; trap 330 updated; DECISIONS
  §92; STATE, HANDOFF, ROADMAP, INDEX, NUMBERING updated.
* Host captures (gitignored): `local/work/r2-{64,oops,vg}.*`,
  `ipi-wedge-findings-64.txt`, `flash-*.log`, `*-usb.log`, `cpu1-boot-outcomes.txt`.
* Helpers (gitignored): `raw2words.py`, `paced_push.py`, `monitor-boot-64.sh`.
* Images (gitignored): `kernel-{ipidloud,earlytcon,slowmo,gadgetdelay,vbusguard,
  affinity,nogadget,mini4x6,musbskip,usbdev}-cpu1.img` with CRCs in report 262.
* Kernel-tree experiments (gitignored): `dsi.c` TCON delayed work, `omap2430.c`
  device-mode fix, `otg.h` text guard, `lib/fonts/Makefile` MINI4x6 link.
  Tracked `initramfs/init.c` reverted to clean.
