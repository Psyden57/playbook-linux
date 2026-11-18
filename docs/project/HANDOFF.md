# Handoff — session 65 (absolute) → session 66

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**, with
a GPU-accelerated compositor.

**Where it stands.**  The unit runs **dual-core at 1008 MHz on the ARMv7 kernel**
and **Nura on hd3**, with `catacomb` compositing on the SGX540.  The session-64
USB device-mode workaround is still required: restoring the stock
`omap_musb_set_mailbox()` call panics on dual-core **even with both callers
serialized by a mutex**, so the fault is not merely the two-caller race.  The
kernel had also been built `-march=armv6k`; that is fixed (`-march=armv7-a`).

* **Region 0 = `local/work/kernel-armv7-console-cpu1.img`, CRC `0xC9FEE2D1`**
  (10,450,108 B) — `#105 SMP`, ARMv7, fast chainloader, loud DTB without
  `maxcpus`, the DSI-init TCON hook, the device-mode-only MUSB workaround, and
  an inert `console=ttyGS0,115200` bootarg.  The 6/6 reboot set ran on
  `0x158D8A85` (the same zImage); this image booted once for a 376 s soak with
  the gadget link alive.
* **hd3 = the Nura root** — `local/work/nura/images/blackberry-playbook-root-osk-wvkbd.img`
  (`07555bde…`, 625,999,872 B) flashed at block `0x4915`, grown to **62.5 G**,
  with the ARMv7 module set (`/lib/modules/7.3.3`) installed and `depmod` run.
  `pvrsrvkm` binds, `catacomb` runs, dropbear is up without an entropy seed.
* **Region 1 = the BusyBox ext4 `6f5b8ca7…`** at `0x29000000` — the fallback
  root, not booted.
* **`BOOTCTR_FCTRY = 0x26`** (above `0x1e`, no write).  The session-65 panic loop
  drained it `0x22 → 0x1c`; the recovery image topped it `0x1c → 0x32`.
* The unit is on **Linux**, not TabletOS.  **No restore is required.**  The way
  back is the 1917 IFS at block `0x825` plus the vendor UFS at `0x4915`.

## 0a. THE NEXT EXPERIMENT — identify the faulting indirect transfer

One variable, primary: **instrument the entry of `omap_musb_set_mailbox()` and
boot the stock-mailbox path with `panic=0`.**  The mutex result (report 263 §1)
eliminated the pure-race explanation; the surviving possibilities are a bad
pointer already present at entry (corruption of the `usb_phy`/`usb_otg`) or a
call that corrupts it during the state-machine update.

**Build (gitignored tree, zImage + DTB only; no `.config` change):**

1. In `local/kernel-tree/drivers/usb/musb/omap2430.c`:
   * remove the winchester early return in `omap2430_musb_enable()` so the stock
     `omap_musb_set_mailbox(glue)` runs from the bind path (keep the
     `set_mailbox_lock` mutex);
   * at the **entry** of `omap_musb_set_mailbox()`, before the lock, emit one
     line via `printascii()` (the PB marker mechanism in
     `arch/arm/kernel/smp.c` / `kernel/smp.c`): `printascii()` takes a plain
     string, so format with `snprintf()` into a static buffer:

     ```
     PBMBX entry cpu=%d status=%d glue=%08x musb=%08x xceiv=%08x otg=%08x
           sv=%08x nh=%08x
     ```
     where `sv = xceiv->otg->set_vbus` and `nh = xceiv->notifier.head`
     (`(unsigned)` casts, `%08x`).  Add a second marker immediately before the
     final `atomic_notifier_call_chain()` with `last_event`, `otg->state`,
     `sv` and `nh`.
2. Put `panic=0` in the board DTS `bootargs` (instead of `panic=10`) so a fault
   **hangs** and cannot loop over `BOOTCTR_FCTRY` (trap 335).
3. Rebuild `zImage` + DTB, wrap, flash region 0; boot it on the bench.

**Exact inputs:** the ARMv7 tree with the tracked config (`CONFIG_SMP=y`,
`CONFIG_NR_CPUS=2`, no `ARCH_MULTI_V6`); the DTB has **no** `maxcpus`; the
gadget is created by `/init`; Nura is not needed for this test (the BusyBox
region-1 root is a fine test root, but it boots from region 1 only if
`ROOTFS_RAW_OFF` is changed — the current initramfs points at hd3, so use Nura).

**What each result means:**

* **The entry marker shows valid kernel-text pointers and the unit faults
  later** → the corruption happens inside the call.  The second marker tells you
  whether `sv`/`nh` changed; if `sv` is still fine, the fault is the notifier
  chain (`nh`) or a branch that calls `musb_set_peripheral()`/`musb_set_host()`.
  Next: one marker before each branch call.
* **The entry marker already shows a wild `sv`/`nh` (e.g. a `0xc......` slab
  address)** → the `usb_phy`/`usb_otg` object is stale before the mailbox is
  entered.  Stop instrumenting the mailbox and audit the PHY lifecycle:
  `devm_usb_get_phy_by_phandle` in `omap2430_musb_init()`, the
  `phy-twl6030-usb` comparator vs the `omap-usb2` PHY instance, and any
  probe-deferral/rebind.
* **It boots clean** → the markers changed the timing; rerun 5×.  If it stays
  clean, the remaining difference from the session-65 failure is the DTB
  (`panic=` only) — re-check the build actually removed the early return and
  that the marker is compiled in.

Capture the result from the **panel** (the DSI-init TCON hook prints early) or
from the ring via the read-back image (`tools/pb_ring.py`, HANDOFF §0b of
session 65's note); a panic with `panic=0` hangs, so the unit needs a power
cycle afterwards.  **Do not flash a stock-mailbox image with `panic=10`.**

**Secondary, only if the USB link drops:** the session-65 capture
`local/work/kmsgcap.py` streams `/dev/kmsg` to the host and survives a whole
boot; run it before the link dies.  On the final boot the link survived 240 s of
streaming plus 90 s idle, so the teardown is intermittent (trap 337;
`docs/contradictions/usb-vusb-drop-idle-vs-intermittent.md`).

**Do not** change `.config` for this experiment (vermagic); a marker-only /
driver-only edit is a zImage+DTB rebuild.

## 0b. Reading the ring

* ring2 = `0x90000000` (`BC_PRIMARY`); hand-off magic `RHND` at `+0xB8`, prev/
  prevprev headers at `+0xBC/+0xC4`, chars `+0x100..` append-only.
  `tools/pb_ring.py decode <words> --boot prev` (or `all`).
* Cross-build `tools/bench/ringdump.c` to a static ARM binary and push it to
  `/tmp` paced (8 KiB/5 ms, trap 302).  Run
  `ringdump 90000000 8000 /tmp/r2.bin`, pull it back, convert with
  `local/work/raw2words.py <bin> 90000000 <out.words>`, then decode.
  `dd if=/dev/mem` fails on the `no-map` pages.  A power-off wipes the ring; a
  warm reset (the flasher's) keeps it.
* Worked examples: `local/work/r2-64.*` (IPI markers), `r2-oops.*` (Oops tail),
  `r2-vg.*` (pbrpmb hang).

## 0c. Media, and the way back

* **Region 0 = `local/work/kernel-armv7-console-cpu1.img` (`0xC9FEE2D1`)** —
  dual-core ARMv7, loud bootargs, device-mode-only MUSB workaround.  The ARMv7
  base without the console bootarg is `0x158D8A85`; the session-64 known-good
  dual-core image is `kernel-usbdev-cpu1.img` (`0xC675E206`) and is the
  reliable recovery image (it tops the counter up).
* **hd3 = the Nura ext4** (`07555bde…`, grown to 62.5 G, ARMv7 modules).  The
  flasher-restorable base is the same image at block `0x4915`.
* **Region 1** = the BusyBox ext4 at `0x29000000`, sha256 `6f5b8ca7…`
  (inherited, not re-hashed this session; modules mismatch, trap 320).
* Way back to the QNX oracle: `ufs-1917-devuser.ufs` (`482e0473…`) at `0x4915`
  + shim IFS (`ifs-shim-sealed.ifs`) at `0x825`; qconn `169.254.0.1:9090`.
  Do NOT restore `local/images/ifs0.sealed.bin` (trap 84).  No restore needed.

## 0d. Bench

* **`BOOTCTR_FCTRY = 0x26`** at wrap-up.  Read with `dmesg | grep pbrpmb`.
* SSH on Nura: `PB_LINK=usb tools/pbssh.sh 'cmd'` (root, key-only,
  `169.254.0.1:22`); the ACM shell is `playbook:~#` (not `pb> `, trap 274), so
  `tools/pbcon.py` does **not** drive it.  dropbear came up without an entropy
  seed (crng init at 13.1 s); if it blocks, trap 328 applies.
* The DTB carries an inert `console=ttyGS0,115200` — the gadget serial driver
  has no kernel-console path, so kernel printk still goes to the panel
  (`tty0`/fbcon).  Use the panel, or `cat /dev/kmsg` from the shell.
* `/tmp` is tmpfs: re-push `ringdump`.  Flash: start the flasher, then reset or
  power on; the power-on bootrom is caught reliably and the 626 MB hd3 write
  takes ~20 min.  Wrappers: `local/work/flash825{,-long}.sh` (region 0);
  hd3 needs `python3 tools/bbusb.py flash --component <img> --dest-block 0x4915
  --from-bootrom --password 0000 --apply`.
* Read dmesg UNFILTERED.  One command at a time.  An agent never writes RPMB;
  NVRAM read-only.  A boot-looping image drains `BOOTCTR_FCTRY`: stop it and
  reflash `0xC675E206`.  NEVER program WDT2 from the chainloader (trap 322).
* **The eMMC stall is still live**: `mmc1: Card stuck being busy` + a lost
  async WRITE during write bursts (27 s/33 s in the Nura boots).  Prefer clean
  `reboot`s; the rw root is ext4 `nobarrier`, and power cuts have already
  damaged it once (trap 336).

## 0e. The tree (gitignored kernel tree; remove before any patch re-export)

`local/kernel-tree/.config` = `CONFIG_SMP=y`, `CONFIG_NR_CPUS=2`, **no
`ARCH_MULTI_V6`** (so `-march=armv7-a`); the tracked
`kernel/winchester-7.3.3.config` now matches it.  Instruments still in the tree:
session-62 `PBDBG` + `head.S` CRR stamp; `PBCPU:`/`PBCPU1:`/`PBIPI1:`/`PBSMF:`
markers (`local/work/carve/*.session63-orig` backups); the session-64 DSI-init
TCON delayed work (`dsi.c`), the MUSB device-mode workaround (`omap2430.c`),
the MINI4x6 font link (`lib/fonts/Makefile`); session 65 adds the
`set_mailbox_lock` mutex and removes the `otg.h` text guard (reverted to
stock).  `kernel/source/omap4-winchester.dts` and the tree DTS are identical:
no `maxcpus`, plus the inert console bootarg.  Ghidra MCP needs
`GHIDRA_INSTALL_DIR` in `~/.config/opencode/opencode.json` + restart.

## 1. Traps / reports

Traps 334–337 added (ARMv6 codegen, panic-loop budget, hd3 ext4 corruption,
intermittent VUSB teardown); trap 333 corrected (the mutex does not fix it).
Reports 262 and **263** are current for the CPU/USB work; report 258 is
superseded.  The new contradiction file is
`docs/contradictions/usb-vusb-drop-idle-vs-intermittent.md`.

## 2. Paste-ready kickoff prompt (session 66)

```
PlayBook (OMAP4430 "winchester") — session 66 (absolute; Linux-era 60)

Workspace: this repository — a git checkout.  Map: docs/README.md.  The earlier
work tree $HOME/rea-work is READ-ONLY.

READ FIRST: SAFETY.md, STATE.md, this HANDOFF, ROADMAP 7a, KNOWN-ISSUES §2
(traps 333–337) and §3, DECISIONS 93–96, reports 263, 262, 259, 257, and
docs/sessions/session-65.md.

WHERE IT STANDS: dual-core at 1008 MHz on the ARMv7 kernel (region 0 =
kernel-armv7-console-cpu1.img, CRC 0xC9FEE2D1, #105 SMP), Nura on hd3 grown to
62.5 G with the ARMv7 modules, catacomb on the SGX540, BOOTCTR_FCTRY=0x26.  The
session-64 device-mode-only MUSB workaround is still required: the stock
omap_musb_set_mailbox() panics on dual-core even with both callers serialized by
a mutex.  The USB teardown is intermittent (30 s / 90 s / not in 6 min), not an
idle timer.

NEXT EXPERIMENT: instrument the entry of omap_musb_set_mailbox() (printascii
marker: cpu, status, glue, musb, xceiv, otg, set_vbus, notifier.head), restore
the stock bind-path call, set panic=0, build zImage+DTB, flash region 0, boot.
Valid pointers at entry + later fault ⇒ corruption during the call (next marker
before each branch); a wild pointer at entry ⇒ stale usb_phy/usb_otg, audit the
PHY lifecycle.  Capture from the panel or the ring; do not use panic=10.

BENCH: hd3 = Nura (07555bde...), region 1 = BusyBox ext4 at 0x29000000.  SSH:
PB_LINK=usb tools/pbssh.sh (the ACM shell is playbook:~#, not pb>; pbcon.py
does not drive it).  Kernel printk goes to the panel; the console=ttyGS0
bootarg is inert.  Flash region 0 at 0x825; hd3 at 0x4915 (--component).  Start
the flasher, then reset/power on.  /tmp is tmpfs; re-push ringdump.  Read dmesg
UNFILTERED.  The eMMC stall still fires on write bursts; prefer clean reboots.
An agent NEVER writes RPMB; NVRAM read-only.  Never program WDT2 from the
chainloader (trap 322).  No config change for a marker-only experiment.

SAFETY: hard hazards (BOOTCTR_FCTRY 0 / >0x32 / failed write / flat pack) remain.
UPDATE THE DOCS as you go and commit. WRAP-UP: only the operator decides when
the session stops. Then commit privately and publish the snapshot.
```
