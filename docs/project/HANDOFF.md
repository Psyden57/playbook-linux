# Handoff — session 64 (absolute) → session 65

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**, with
a GPU-accelerated compositor.

**THE ACTIVE ITEM: the CPU — now with a working dual-core fix.**  The MPU runs
at **1008 MHz** (fast chainloader).  Session 64 found the dual-core wedge is
**not** an IPI race: it is the USB gadget bind from initramfs `/init`
(`musb_start` → `omap2430_musb_enable` → `omap_musb_set_mailbox`), which either
Oopses (PID 1 dies) or hangs `pbrpmb`.  A targeted MUSB device-mode fix gives
**3/3 clean dual-core boots**.  Report **262** is the current authority; reports
258–261 are its evidence chain.

* **Region 0 = `local/work/kernel-usbdev-cpu1.img`, CRC `0xC675E206`** — `#102
  SMP`, dual-core `online=0-1`, gadget up, `pbrpmb` OK.  This is what the unit
  boots at wrap-up.
* **Rejected hypotheses** (all with measurements, report 262): replicate the
  reference's SMC `0x108` SCU-power call (0/3), delay gadget setup by 1 s (same
  Oops later), guard `otg->set_vbus` with `func_ptr_is_kernel_text()` (Oops
  persisted), pin PID 1 to CPU0 (4/4 Oopses).
* **Disabling `gadget_setup()` entirely** gave no wedge (>100 s up) — proof of
  the trigger — but no USB console.  Skipping the whole mailbox gave 4/4 boots
  but USB eventually dropped.
* **The ~32 s USB drop is idle runtime PM** (`VUSB: disabling`,
  `audio-ldo: disabling`, gadget deactivated) while the unit keeps running; it
  is a separate matter from the boot wedge.
* The panel can show the panic live: a DSI-init TCON retrying work
  (`dsi.c`, report 261) programs the panel before `execve("/init")`.

## 0a. THE NEXT EXPERIMENT — validate the MUSB fix, then find the exact fault

**One variable, primary: longer soak + reliability of `kernel-usbdev-cpu1.img`
(`0xC675E206`).**  It has booted 3/3; that is a small sample.

1. Reboot via the ACM console (`pbcon.py 'reboot -f'`) at least **5 more times**,
   recording `lsusb` for `0fca:8010` and `cat /sys/devices/system/cpu/online`
   for `0-1`.  Each success raises confidence; any wedge means the fix is
   partial — go to step 3.
2. Soak **>5 minutes** with host activity every ~20 s (e.g. `pbssh.sh 'uptime'`)
   to avoid the idle-PM gadget teardown.  If it stays up with both cores and
   USB, the dual-core item is **closed** and the next work is the MPU OPP soak
   (report 257 §6) and the drawer/hardware-GL items.
3. If a wedge recurs, capture the full Oops header: reflash the failing image,
   then **immediately** (before the WDT2 reset log overwrites it) flash the
   quiet read-back image and decode `local/work/r2-*.words` for the
   `Unable to handle kernel …`/`PC is at`/`LR is at` lines.  Earlier captures
   landed on the **hang** variant, not the Oops variant — that is why the exact
   faulting indirect call inside `omap_musb_set_mailbox` is still unknown.
4. Optional follow-up once boots are reliable: replace the broad
   device-mode-only workaround with a guard on the specific bad call once step
   3 identifies it; re-test 5 boots.

**Do not** change `.config` (would change module vermagic and force rebuilding
every module).  Marker-only / driver-only edits are zImage-only rebuilds.

## 0b. Reading the ring

* ring2 = `0x90000000` (`BC_PRIMARY`); hand-off magic `RHND` at `+0xB8`, prev/
  prevprev headers at `+0xBC/+0xC4`, chars `+0x100..` append-only.
  `tools/pb_ring.py decode <words> --boot prev` (or `all`).
* Cross-build `tools/bench/ringdump.c` to a static ARM binary and push it to
  `/tmp` paced (8 KiB/5 ms, trap 302).  Run
  `ringdump 90000000 8000 /tmp/r2.bin`, pull it back, convert with
  `local/work/raw2words.py <bin> 90000000 <out.words>`, then decode.
  `dd if=/dev/mem` fails on the `no-map` pages.  A power-off wipes the ring.
* Worked examples: `local/work/r2-64.*` (IPI markers), `r2-oops.*` (Oops tail),
  `r2-vg.*` (pbrpmb hang).

## 0c. Media, and the way back

* **Region 0 = `local/work/kernel-usbdev-cpu1.img` (CRC `0xC675E206`)** — dual
  core, loud bootargs.  Reliable single-core fallback:
  `local/work/kernel-fastcl-nosmp.img` (`0x8B83421F`).
* **Region 1** = the busybox ext4 at `0x29000000`, sha256 `6f5b8ca7…`
  (inherited, not re-hashed this session; modules mismatch, trap 320).
  **hd3** = the same ext4 at `0x4915`, mounted rw.
* Way back to the QNX oracle: `ufs-1917-devuser.ufs` (`482e0473…`) at `0x4915`
  + shim IFS (`ifs-shim-sealed.ifs`) at `0x825`; qconn `169.254.0.1:9090`.
  Do NOT restore `local/images/ifs0.sealed.bin` (trap 84).  No restore needed.

## 0d. Bench

* **`BOOTCTR_FCTRY = 0x2c`** at wrap-up (self-topping; a test boot is fine, a
  boot-loop is not).  Read with `dmesg | grep pbrpmb`.
* SSH: `PB_LINK=usb tools/pbssh.sh` — **after** seeding entropy (trap 328).
  The ACM wraps at ~80 columns; `echo <hex> > /dev/random` only credits ~21
  bits per short write.  Seed with `/bin/pbconsole`'s `entropy <hex>` driven by
  `tools/pbraw.py`.  On the read-back image the ACM is the bring-up console, so
  `entropy <hex>` works directly.
* **At wrap-up the unit is off USB** after ~32 s idle (runtime PM); the panel
  console still works.  A fresh boot restores ACM/RNDIS.  RNDIS interface name
  flaps (trap 318) — bind NM `pb-rndis` by MAC `02:1a:11:00:00:02`.
* `/tmp` is tmpfs: re-push `ringdump`.  Flash: start the flasher first, then
  reset; one bootrom session, 60 s window; bootrom can hold ~20 s (trap 323) —
  poll `lsusb`.  Wrappers: `local/work/flash825{,-long}.sh`.
* Read dmesg UNFILTERED.  One command at a time.  An agent never writes RPMB;
  NVRAM read-only.  A boot-looping image drains `BOOTCTR_FCTRY` with no top-up:
  stop it and reflash the known-good image.  NEVER program WDT2 from the
  chainloader (trap 322).  Flash region 0 only when only it changed.

## 0e. The tree (gitignored kernel tree; remove before any patch re-export)

`local/kernel-tree/.config` = `CONFIG_SMP=y`, `CONFIG_NR_CPUS=2`.  Instruments:
session-62 `PBDBG` set + `head.S` CRR stamp; `PBCPU:`/`PBCPU1:`/`PBIPI1:`/
`PBSMF:` markers (backups `local/work/carve/*.session63-orig`); session-64
additions: DSI-init TCON delayed work (`dsi.c`), MUSB device-mode fix
(`omap2430.c`, report 262), `otg_set_vbus` text guard (`otg.h`), MINI4x6 font
link (`lib/fonts/Makefile`).  The **tracked** `initramfs/init.c` was reverted to
clean (the CPU-affinity experiment was rejected).  Tracked chainloader template
holds the VC/DPLL program (DECISIONS §91).  Ghidra MCP needs
`GHIDRA_INSTALL_DIR` in `~/.config/opencode/opencode.json` + restart.

## 1. Traps / reports

Traps 330 updated (measured IPI markers), 332 added (dual-CPU `printascii`
race); report 262 adds the MUSB-bind diagnosis.  Reports 256–262 are current for
the CPU work; report 258's "clean hang" is superseded by 261/262.

## 2. Paste-ready kickoff prompt (session 65)

```
PlayBook (OMAP4430 "winchester") — session 65 (absolute; Linux-era 59)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST: SAFETY.md, STATE.md, this HANDOFF, ROADMAP 7a, KNOWN-ISSUES §2
(esp. 312-332) and §3, DEVELOPMENT §2/3/3a/4/5/7a, DECISIONS 84-93,
reports 262, 261, 260, 259, 258, 257, 256, and docs/sessions/session-64.md.

WHERE IT STANDS: THE DUAL-CORE WEDGE IS THE USB GADGET BIND, NOT AN IPI RACE.
Session 64 found an Oops in omap_musb_set_mailbox from musb_start (PID 1 writing
the UDC) and a pbrpmb hang, both triggered by /init's gadget_setup. A targeted
fix (call only omap_control_usb_set_mode(USB_MODE_DEVICE) from
omap2430_musb_enable on winchester) gave 3/3 clean dual-core boots with the
gadget up. Region 0 = kernel-usbdev-cpu1.img (0xC675E206), online=0-1,
BOOTCTR_FCTRY=0x2c. At wrap-up the unit is off USB after ~32s idle (runtime PM,
VUSB disabling) but still running with panel console.

NEXT EXPERIMENT: soak and confirm the fix. Reboot 5+ times via pbcon.py
'reboot -f', record lsusb 0fca:8010 and online=0-1 each time; soak >5 min with
host activity every ~20s to avoid idle PM. If it stays up, the dual-core item is
closed: move to the MPU OPP soak (report 257 §6), drawer, hardware GL. If a
wedge recurs, capture the ring via the read-back image IMMEDIATELY (earlier
captures landed on the hang, not the Oops) and read the Unable-to-handle/PC/LR
lines to identify the exact bad indirect call in omap_musb_set_mailbox.

BENCH: region 1 = ext4 6f5b8ca7... at 0x29000000; hd3 = ext4 at 0x4915 rw.
Seed entropy with /bin/pbconsole's 'entropy <hex>' via tools/pbraw.py (the ACM
wraps at ~80 columns; echo-hex-to-/dev/random credits only ~21 bits). RNDIS name
flaps (trap 318) — bind NM pb-rndis by MAC 02:1a:11:00:00:02. /tmp is tmpfs;
re-push ringdump; pace bulk pushes 8KiB/5ms. Flash: start flasher, then reset;
bootrom can hold ~20s (trap 323), poll lsusb. Read dmesg UNFILTERED; one command
at a time. An agent NEVER writes RPMB; NVRAM read-only. Never program WDT2 from
the chainloader (trap 322). No config change — it would change module vermagic.

SAFETY: hard hazards (BOOTCTR_FCTRY 0 / >0x32 / failed write / flat pack) remain.
UPDATE THE DOCS as you go and commit. WRAP-UP: only the operator decides when
the session stops. Then commit privately and publish the snapshot.
```
