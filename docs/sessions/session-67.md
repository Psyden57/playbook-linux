# Session 67 (absolute) — the mailbox fault is a comparator lifetime bug; fixed and validated

> Numbering: absolute `67` (the project's chronological scheme — see
> `NUMBERING.md`).  The reports call this **Linux-era session 61**
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-23.
Unit state at the end: Linux/Nura on hd3, region 0 = **`0x3547D613`** (ARMv7
`#109 SMP` dual-core, **stock `omap_musb_set_mailbox()` restored**, comparator
fix, `panic=0`); `BOOTCTR_FCTRY = 0x28`; USB link up at the close.

## Goal of this session

Run HANDOFF-66 §0d's one experiment: **audit the TWL6030 `phy_companion`
lifetime statically**, not with more markers.  Done = the reason
`phy->comparator->set_vbus` is a slab address is explained, and — if a fix
follows — the stock `omap_musb_set_mailbox()` call runs on dual-core for 3+
clean boots.

## Unit state inherited and left behind

* Inherited: region 0 `0xC9FEE2D1` (device-mode-only workaround), hd3 the Nura
  ext4 `07555bde…`, patch 65 files / 9 110 lines (`6f9bef24…`),
  `BOOTCTR_FCTRY` last known `0x26`.
* **Left behind:** region 0 **`0x3547D613`** (10,448,868 B, `7.3.3 #109 SMP`,
  the stock mailbox call, the comparator lifetime fix, `panic=0`); hd3 and
  region 1 untouched.  `BOOTCTR_FCTRY = 0x28` (the three validation boots read
  `0x2a`, `0x29`, `0x28`).  No restore required.

## Confirmed (measured, read in a file, or seen on the bench)

* **The comparator is a freed `devm` object.**  Report 264's faulting `r0`
  (the `phy->comparator` value) is `c18d5280`; the kernel decodes it as
  `slab kmalloc-192 start c18d5240 pointer offset 64 size 192`, i.e. a
  **`devm_kzalloc`** allocation (data at offset 64 = `sizeof(struct devres)`,
  the same offset the `otg`/`glue`/`musb` registers show).  Its first word is
  `c2c05800`, a `kmalloc-1k` heap pointer — not `twl6030_set_vbus` text.
  `struct twl6030_usb` is ~120 B + 64 = `kmalloc-192`, so this is a
  freed/reused TWL comparator.  [capture
  `docs/captures/musb-mailbox-fault-2026-11-19.txt`; report 265 §2]
* **The bug is in `twl6030_usb_probe()`.**  It stores `&twl->comparator` in the
  PHY (`phy-twl6030-usb.c:358`) before `twl6030_usb_ldo_init()`; that function
  calls `regulator_get(dev, "usb")` (the board's `usb-supply = <&vusb>`) and
  returns `-ENODEV` for **any** error, including `-EPROBE_DEFER`.  A deferred
  `vusb` therefore fails the probe permanently, devres frees `twl`, and
  `phy->comparator` dangles.  The IRQ-request failures leave the same dangling
  pointer.  [report 265 §1]
* **The other writer is inert.**  `cpcap-charger.c:921/940` is the only other
  `omap_usb2_set_comparator()` caller; `CONFIG_CHARGER_CPCAP=m` and there is no
  CPCAP node in the board DTS, so it does not probe.  [report 265 §1]
* **The fix compiles and boots.**  `twl6030_usb_ldo_init()` returns
  `PTR_ERR()`, a devm action clears the comparator on probe failure/remove, and
  `omap_usb2_set_comparator()` drops its `usb_get_phy()` reference.  With the
  winchester early return removed, image `0x3547D613` (#109 SMP) gave **3/3
  clean dual-core boots** (`online=0-1`), gadget `binding gadget driver [g1]`
  at ~1.41 s, `twl6030_usb … Initialized` at ~0.8 s, and no
  Oops/panic.  [flash log `local/work/flash-session67.log`; evidence
  `local/work/session67-boot3-evidence.txt`]
* **`BOOTCTR_FCTRY` was healthy across the cycles:** `0x2a → 0x29 → 0x28`,
  each read as `>= 0x1e, no write`.  [ACM]
* **The patch re-exports cleanly:** 67 files / 9 136 lines, sha256
  `d19adff8…`, 0 rejects / 67/67 identical against pristine 7.3.3; the tree
  walk (trap 211) found only the known excluded diagnostics and generated
  artifacts outside the patch.  [report 265 §5]

## Inferred (reasoning from evidence, not directly measured)

* **The faulting session-66 boots took the `ldo_init()`-fails path.**  Their
  `dmesg` was never captured (panic=0, ring only), so this is inferred from
  the pointer class plus the code: a live `twl` cannot have a heap pointer in
  its first word, and the only free path is a failed probe (or remove, which
  cannot happen for this built-in driver).
* **The deferred-probe race is the trigger.**  `twl6030_usb` is a
  `subsys_initcall`, the `omap-usb2` PHY is a device initcall, and `vusb` is a
  sibling TWL-MFD child; the late deferred-probe retry can still find `vusb`
  unregistered.  The three validated boots happened to get a ready regulator
  (all logged `Initialized`), so the fix's retry path was not necessarily
  exercised — but the dangling/free path is removed either way.
* **The workaround was hiding the bug, not fixing it.**  The device-mode-only
  early return skipped `otg_set_vbus()`, which is the only reader of
  `phy->comparator`; the dangling pointer was harmless until the stock call
  was restored.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The comparator object is live and corrupted in place."**  Rejected: the
  object is a `devm` `kmalloc-192` allocation (offset 64) whose first word is
  a heap pointer; that is a freed/reused slab, not an in-place overwrite of a
  live `twl` (which would keep `twl6030_set_vbus` text unless something else
  wrote it — no such writer exists).
* **"`cpcap-charger` clobbers the pointer."**  False on this board: the driver
  is a module and has no DT node.
* **"The report-265 fix must change `.config`."**  No: it is driver-only; the
  vermagic is unchanged and no module rebuild is needed.

## Unknown / open questions

* **What drives the intermittent VUSB teardown?**  Unchanged (trap 337).  The
  only writer is `twl6030_usb_irq()` on a `VBUS_DET=0` read; a `VBUS_DET`/
  `linkstat` log in that handler would settle it.  This session's three boots
  did not drop during the (short) checks.
* **How often does the probe's regulator actually defer?**  Not measured; the
  fix makes it safe and self-healing.  A boot-time log (or reading the deferred
  probe state) would quantify it.
* **OTG host mode** is reachable again but untested.
* **MPU OPP**, memory read throughput, UI performance, the hd3 rebake — all
  unchanged from session 66.

## Superseded / uncertain

* **Trap 338 / report 264's "next: audit the comparator lifetime"** is
  answered: the comparator is freed by a failed probe, not corrupted in place.
  Report **265** is current for this fault.
* **Session 65's "serializing the callers does not fix it"** stands; the mutex
  is kept as hardening but is not the fix.
* Nothing else this session contradicts a newer document.

## Artefacts produced

* Report **265** (`docs/reports/265-musb-comparator-lifetime-fix-2026-11-23.md`).
* Patch re-export: **67 files / 9 136 lines**, sha256 `d19adff8…`;
  `kernel/source/` mirrors refreshed (`omap4-winchester.dts`,
  `phy-twl6030-usb.c`, `phy-omap-usb2.c`) and two stale ones corrected
  (`dsi.c`, `tc358764.c`).
* Region-0 image `boot/chainloader/kernel.img` — **`0x3547D613`**
  (10,448,868 B, sha256 `6ecd068d…`).
* Bench helpers/evidence (gitignored): `local/work/reboot_verify.py`,
  `local/work/flash-session67.log`,
  `local/work/session67-boot3-evidence.txt`,
  `local/work/{phy-twl6030-usb.c,phy-omap-usb2.c,omap2430.c,omap4-winchester.dts}.session67-orig`.
* Traps 339 added; DECISIONS §98; STATE/HANDOFF/ROADMAP/KNOWN-ISSUES/report
  INDEX updated.
