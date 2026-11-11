# Known-issues history — display resolution and fixed ledgers

Archived from `docs/project/KNOWN-ISSUES.md`.  The open traps, corrections and
current documentation defects stay in the canonical file.

## Resolved: the display works

## 1. The display works

**Resolved 2026-10-29 (session 15, report 133).**  The panel shows a clean,
stable image — solid colours and the kernel console as text.  Three defects had
to be fixed, each necessary:

1. **Bridge delivery** (session 14): the DSI HS clock was never started; fixed
   with the reference's VC0/HS null packet (report 131).
2. **Video configuration and panel TCON** (session 15): our `VP_HTIM1/2` kept
   the LVDS porches in the reserved top byte, so the bridge held HBP = HFP = 0
   (report 132 §5), and the Hitachi TCON table was only ever sent by hand.  The
   porches are `0x00780001`/`0x00760400` and `/init` sends the table at bring-up.
3. **The DISPC core clock** (report 133): the DSS functional clock was 48 MHz
   while the pixel clock is 50.4 MHz, so the controller could not service the
   pixel stream; the picture was correct on the left of each line and failed
   progressively across it.  `dss_probe()` now asks the PER PLL for the
   reference's rate and gets 96 MHz.

**Two measurements that misled earlier sessions are withdrawn.**  `LANESTATUS0`
and `SYSSTAT` read 0 on a **working** TabletOS display (report 132 §4), so they
were never fault indicators; and the session-13 "partial live image" was never
ours (report 129 §5).

The history below is kept because the method matters: every real defect in this
chain was found by reading the same register on a working system and diffing,
never by reasoning about undecoded bits.

### History: what was falsified or settled along the way (reports 124–133)

* The **timing-encoding**, **PHY-word** and **DISPC I/O-pad** hypotheses
  (report 124).
* **"The bridge needs no programming"** — the reference programs it at
  `winch_lcdctl 0x13e50` (report 125).
* **The panel is a Hitachi** — `GPIO6_DATAIN = 0x00010800` (bit 16 = 1) and
  slog `LCD is Hitachi` on TabletOS.
* **The PLL, the DSI block, the PHY, `DSS_CONTROL` and every PMIC rail** match
  the reference.
* **`PPI_TX_RX_TA (0x013C)` decides whether bridge reads answer**, and it is
  latched by `PPI_STARTPPI`: `4` → 50 % failures, `0x00040006` → none
  (report 132 §3).
* **The TCON byte order is high-first** (the reference's): both orders drive the
  panel, but low-first swaps magenta↔green and black↔white (report 132 §8).
* **`dss_setup_default_clock()`'s "clk rate mismatch: 48000000 != 128000000" was
  the display bug**, not a harmless warning (report 133 §2).

### Session 18: the boot log is clean (report 142)

* **`DSI CIO error, cio irqstatus 800000` is benign when it is one per bridge
  read.**  `0x800000` is bit 23 = `DSI_CIO_IRQ_ERRCONTENTIONLP1_2`, the LP
  contention of a DSI generic read's bus turnaround; it fires on successful
  reads too (the 121 boot errors were exactly the 121 `pb` reads).  The
  boot-time probes are now behind `pb_probe` (default off).
* **The duplicate `pb_reg` sysfs warning** (the encoder is probed twice) is
  fixed by creating the attribute through the driver's `.dev_groups`.
* **`clk rate mismatch: 48000000 != 128000000` is fixed**:
  `dss_setup_default_clock` uses `clk_round_rate` on winchester and
  `dss_probe` records the real 96 MHz.
* A normal boot still contains only benign messages: dummy regulators, the
  optional-DT GPIO lookups, `hw perfevents: no irqs for PMU`, clkdev's
  `device ID is greater than 24`, `Warning: unable to open an initial console`,
  and the `/dev/kmsg` binary record (still open, §4).


### Fixed ledger — session 40

### Fixed in session 40 (documentation)

* **Trap 151's wording** — "MCLK1 does not clock on this unit" is corrected to
  "does not clock under our image": the reference locks FLL1 from MCLK1
  (report 204).
* **Trap 152's opening claim** — "the WM8994 stops accepting register writes
  after boot" now points at trap 155, the actual rule.
* **Report 203** carries a pointer to 204 at its headline, and its §5 inference
  is marked too strong.
* **The session-39 note that `startup-playbook` writes `AUXCLK1 = 0x00020100`
  at `0x7980`** is now confirmed at instruction level
  (`docs/reference/startup-playbook/clocks-and-wdt.md` §1).


### Fixed ledger — sessions 15–38 and the consolidation

### Fixed in session 38 (documentation)

* **`tools/pbssh.sh` claimed the 1917 image "only accepts devuser".**  Root
  works (`uid:0 euid:0`); the early `Permission denied (publickey,password)` was
  the door-hold key upload not yet done.  The comment now says so.  (Session
  38; operator correction.)
* **The boot-budget prose read as if every test boot were a budget risk.**  The
  Linux `/init` tops `BOOTCTR_FCTRY` up at ≤`0x1e` and TabletOS writes `0x32`,
  so the counter self-replenishes; `SAFETY.md` §2, `STATE.md`, trap 33 and
  `HANDOFF.md` now say so, with the hard hazards unchanged.  (Session 38;
  operator direction.)

### Fixed in session 35 (documentation)

* **The `CLK32KG` register labels were swapped** in report 180 §4 and the
  session-35 draft: with `clk-twl.c`'s `VREG_GRP 0` / `VREG_TRANS 1` /
  `VREG_STATE 2`, `0x8D` is `CFG_TRANS` (identical `0x05`) and `0x8E` is
  `CFG_STATE` (`0x05` vs `0x55`; APP bits 3:2 = `01` in both, CON/MOD differ).
  Corrected in report 180 (inline), report 192, STATE, HANDOFF, ROADMAP, the
  contradiction file and both captures; listed as §3.21.
* **The contradictions index is current again:** `twl-clk32kg…` is resolved
  2026-11-08 (report 192 §7), the open list is six, and the `0xBC` identity is
  the only residual.

### Fixed in session 33 (USB warm-reset regression, stale patch)

* **The USB warm-reset re-enumeration regression is fixed** (trap 130).  The
  host missed the gadget connect until a re-plug; the warm-reboot A/B isolated
  the session-32 `pb_wl1283_reset_pulse()` (GPIO2_22 low 500 ms → high):
  `0x90BDE221` (pulse on) fails, `0x0353A241` (pulse off) enumerates by itself
  in 14 s (gadget configured 5.28 s).  The pulse and the FREF2 enable are
  removed from the tree; the final image is `0x0353A241`.
* **The 7.3.3 patch is current again**: re-exported with the removed calls,
  **7 258 lines / 47 files**, sha256 `e7b7fc2e…`, 0 rejects and 47/47 files
  byte-identical against pristine 7.3.3.  The session-33 MMC test knobs
  (`post-power-on-delay-ms`, `MMC_CAP2_NO_SDIO_RESET`) are not in the patch.

### Fixed in session 30 (7.3.3 port, CM3217 remove, patch export)

* **The CM3217 remove path no longer dereferences NULL** (trap 107):
  `i2c_set_clientdata(client, indio_dev)` was added in `cm3217_probe()`, in
  `local/kernel-tree` and `kernel/source/cm3217.c`.  Verified by an
  unbind/bind cycle on 7.3.3 (shell alive, lux 30).
* **The patch export is tree-derived** (trap 106): `kernel/patches/README.md`
  now takes the file list from the tree and the verification applies the patch
  and compares every file.  The current 7.3.3 patch is **42/42 files
  byte-identical**.  The 6.15.11 patch remains incomplete by history and is
  reference-only.
* **The kernel base is 7.3.3** (ROADMAP item 10): boot, panel, eMMC and the
  peripheral regression were verified on `0x0F6AC47F`.

### Fixed in session 29 (WL1283 bus, CM3217, two probe bugs)

* **The WL1283's bus and board lines are known** (trap 97): MMC5, WL_EN on
  GPIO2_22, WL_IRQ on GPIO2_21 as an input.  The session-28 DTS comment and
  report 176's "mmc1" are corrected.
* **The CM3217 works** (traps 95/104, report 182): `cm3217.ko` binds `3-0010`
  and reads 32–37 lux (dark) / 191 (lit).  Trap 95's protocol question is
  answered, and `CONFIG_CM3217=m` + the DTS node are in the tree.
* **Two defects of this session's own making, both fixed and verified**
  (traps 99/100): the GPIO walk one bank past GPIO6 that reset-looped the unit,
  and the register dump inside `omap_hsmmc_set_ios()` that killed the console on
  a runtime rebind.  The `PBGPIO:` dump now runs once, in probe, with `iounmap`.
* **`ThreadCtl`/`SIGBUS` on MMCHS5 is understood** (trap 101): block-specific,
  only io-pkt can read it.
* **The QNX-side CM3217 readings are explained** (trap 103): they were races
  against RIM's own driver, not properties of the part.

### Fixed in session 28 (userland, sensors, flasher)

* **The DTS sensor-bus inversion** — `&i2c4` (`0x48350000`) was `disabled`;
  it is enabled with the BMA150/HMC5883L/MPU3050 and FAN5702 nodes, and
  `&i2c3` (`0x48060000`) stays enabled-but-empty (trap 91; reports 176/177).
* **The BMA150 driver had no `of_match`** — added (the driver predates DT);
  HMC5883L/MPU3050 bound through their existing tables.
* **The `=m` module closure** — `industrialio`,
  `industrialio-triggered-buffer`, `kfifo_buf`, `i2c-mux`, `evdev` and
  `hwmon` are installed by `build_rootfs.py --modules` and modprobed by rcS
  (trap 94).
* **The FAN5702 EN** — `leds-fan5702` with the pad `0x1b0` pinctrl and
  `enable-gpios` (trap 96).
* **`rootfs/etc/init.d/rcS` was untracked** — the `*.d` ignore rule shadowed
  it; force-added and a `.gitignore` negation added.
* **The `bbusb.py` bootrom window** — 10 s → 60 s; it is the flasher of record
  (trap 93's flashing workaround, `DECISIONS.md` §29).
* **The inittab `umount -a -r`** — removed (`sync`), trap 93.
* **The LED class name duplication** — the source labels are now
  `red:status`/`green:status`/`blue:status`; the media image still has
  `playbook:playbook:*` until the next region-0 build.

### Fixed in session 17 (write path, tooling, policy)

* **The Linux eMMC write path was not a driver defect.**  `omap_hsmmc` is fine:
  256 KiB `CMD25`s, 5.5–7.0 MB/s with the trace off.  The old 17–32 KB/s was
  `recv`'s tty-sized buffered writes plus 4–7 `pr_info`s per request to fbcon;
  the trace is now behind `/sys/module/omap_hsmmc/parameters/trace` (default
  off) and `blkwr` is the tested large-write primitive.  Trap 49 replaced
  (report 136).
* **`tools/env.sh` no longer shadows the host `make`.**  It put the QNX host bin
  first, and that tree's GNU make 3.82 broke kernel builds (>= 4.0 required);
  the QNX bin is appended last now.
* **hd3's raw offset is `0x49000000`,** not `0x48F00000`; corrected in SAFETY,
  STATE, trap 47, report 134 and the console `qnx6` default (report 138).
* **The read-only-mount policy is resolved** (operator): read-only eMMC/QNX6
  mounts are allowed, writable are not; SAFETY §4/§6 and DECISIONS §17.
* **New capability: read-only QNX6 access** (`CONFIG_QNX6FS_FS=y` + console
  `qnx6`), used to diagnose the reseed (report 138).
* **A smaller hd3 FS is reseeded at boot** — trap 54; an FS-only shrink is dead.

### Fixed in session 15 (display and tooling)

* **The display.**  Three defects, each necessary: bridge delivery (report 131),
  the LVDS porches plus the TCON table at bring-up (report 132), and the DISPC
  core clock (report 133).  §1 above records them; do not re-open.
* **`LANESTATUS0`/`SYSSTAT` as fault indicators** — withdrawn (report 132 §4).
* **`VP_HTIM1/2` "don't store the upper half"** — our constant; fixed
  (report 132 §5).
* **The console's `v` op was unreachable** — `sscanf` returns 1 for a bare `v`
  and the code rejected `n < 2`; fixed (report 132 §10).
* **`/init` no longer auto-reboots after 180 s** — the timer spent a factory <!-- audit-ok: fixed-list history -->
  boot per iteration and the console made it unnecessary (trap 15/33).
* **`CONFIG_INITRAMFS_SOURCE` is repo-relative** (`"../../initramfs/root"`), so
  no clone needs a machine-specific path (trap 43).
* **The host tools derive every path from the repository** and take
  `PB_KEY`/`PB_PUB`/`PB_SYSTEMMAP`/`PB_IFS_TEMPLATE`/`QNX660` overrides; the two
  QNX binaries are stripped.  [SETUP.md §8; commits `f47fbef`, `f28fd53`]

### Fixed during the consolidation (recorded so nobody re-fixes them)

* `CONFIG_INITRAMFS_SOURCE` in `kernel/winchester.config` and
  `kernel/patches/winchester.config` named the old `rea-work` initramfs tree; both
  now name `initramfs/root` here.
* **`local/kernel-tree/.config` (the live build tree) still named the `rea-work`
  tree until session 15**, which is why initramfs changes silently did not reach
  any image.  Corrected; see trap 43.
* `tools/pre_os_ifs_flash.py` passed `ROOT + '/bbusb/loaders'` to bbusb; it now
  passes `bbusb.DEFAULT_LOADERS` (`local/loaders` here, `tools/loaders` or
  `$BBUSB_LOADERS` if present).
* `tools/README.md` claimed the vendor `TEMPLATE` was unused by the
  `--template-ifs` flow; it is read on every flash, and the README now says so.
* The board DTS described "2 data lanes" (it documents four), and
  `kernel/patches/README.md` quoted 2 482 lines / 28 files (it is 2 922 / 29).
* `docs/reports/HANDOFF-2026-10-27-session6.md` §0 called the patch set "stale"
  while its own §3 says it was re-exported; §0 now matches §3.
* The host tools under `tools/` no longer hard-code `$HOME/rea-work`, and
  `tools/force_reset_loader.py` is in the tree.
