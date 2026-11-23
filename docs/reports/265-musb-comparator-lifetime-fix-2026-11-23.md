# 265 — the mailbox fault is a TWL6030 comparator lifetime bug; fix validated on dual-core

Date: 2026-11-23 (session 67, Linux-era 61).  Static audit + bench.

## 0. Headline

* **Root cause identified statically.**  `twl6030_usb_probe()` registers
  `&twl->comparator` into the `omap-usb2` PHY (`omap_usb2_set_comparator()`,
  `drivers/usb/phy/phy-twl6030-usb.c:358`) **before** the probe can still fail.
  The next fallible step, `twl6030_usb_ldo_init()`, does
  `regulator_get(dev, "usb")` (the board's `usb-supply = <&vusb>`); a
  not-yet-ready regulator returns `-EPROBE_DEFER`, but the code collapsed
  **every** error to `-ENODEV`.  The probe therefore failed **permanently**
  (no retry), devres freed the `devm_kzalloc`'d `twl`, and the PHY kept a
  dangling `phy->comparator`.  Later the `kmalloc` object is reused, its first
  word (`set_vbus`) becomes a heap pointer, and the stock
  `omap_musb_set_mailbox()` jumps through it — report 264's Oops.
* **The report-264 registers prove it.**  At the fault `r0` (the value of
  `phy->comparator`, passed as `set_vbus`'s first argument) is `c18d5280`,
  decoded by the kernel as `slab kmalloc-192 start c18d5240 pointer offset 64`
  — a **devm** allocation (`struct devres` is 64 bytes, so `devm_kzalloc` data
  starts at offset 64, exactly like the `otg`/`glue`/`musb` registers).  The
  word at its offset 0 (`comparator->set_vbus`) is `c2c05800`, a `kmalloc-1k`
  object.  A live `twl` would have `twl6030_set_vbus` kernel text there.  A
  freed-and-reused `struct twl6030_usb` (`~120 B` + 64 = `kmalloc-192`) is the
  only object that fits.
* **Fix (three changes + restore the stock call):**
  1. `twl6030_usb_ldo_init()` returns `PTR_ERR(twl->usb3v3)`, so
     `-EPROBE_DEFER` propagates and the probe retries instead of failing
     permanently;
  2. a devm action (`twl6030_usb_clear_comparator`) calls
     `omap_usb2_set_comparator(NULL)` on any probe failure and on remove, so the
     PHY never keeps a pointer to a freed `twl`;
  3. `omap_usb2_set_comparator()` drops the `usb_get_phy()` device/module
     reference with `usb_put_phy()` (the API docstring requires it; the old code
     leaked one reference per call, which the new deferral retry would repeat).
  The winchester early return in `omap2430_musb_enable()` is removed (the
  `set_mailbox_lock` mutex stays), and the board DTB uses `panic=0` for the
  bring-up so a fault hangs rather than looping and spending
  `BOOTCTR_FCTRY` (trap 335).
* **Validated on the bench.**  Image **`0x3547D613`** (`7.3.3 #109 SMP`,
  armv7l) flashed to region 0 at block `0x825`; **3/3 clean dual-core boots**
  (`online=0-1`), the gadget binds at ~1.41 s, and there is no Oops/panic.
  `BOOTCTR_FCTRY` read `0x2a → 0x29 → 0x28` across the three boots.

## 1. The static audit

The two writers of `phy->comparator` are the only candidates:

| writer | when | on this board |
|---|---|---|
| `twl6030_usb_probe()` (`phy-twl6030-usb.c:358`) | probe | **the bug** |
| `cpcap-charger.c:921/940` | probe / shutdown | **inert**: `CONFIG_CHARGER_CPCAP=m`, no CPCAP node in the board DTS, module not loaded |

Nothing else writes `phy->comparator` (`grep omap_usb2_set_comparator` finds
only these two drivers).

The bug in full (`drivers/usb/phy/phy-twl6030-usb.c`, upstream verbatim, also in
7.3.3 and 6.15.11):

```c
	twl = devm_kzalloc(dev, sizeof(*twl), GFP_KERNEL);
	...
	twl->comparator.set_vbus	= twl6030_set_vbus;
	twl->comparator.start_srp	= twl6030_start_srp;

	ret = omap_usb2_set_comparator(&twl->comparator);   /* stores &twl into phy */
	if (ret == -ENODEV) {
		dev_info(&pdev->dev, "phy not ready, deferring probe");
		return -EPROBE_DEFER;                       /* nothing stored: safe */
	}
	...
	err = twl6030_usb_ldo_init(twl);
	if (err) {
		dev_err(&pdev->dev, "ldo init failed\n");
		return err;                                 /* twl freed, phy dangles */
	}
	...
err_put_regulator:
	regulator_put(twl->usb3v3);
	return status;                                      /* same */
```

and `twl6030_usb_ldo_init()`:

```c
	twl->usb3v3 = regulator_get(twl->dev, "usb");
	if (IS_ERR(twl->usb3v3))
		return -ENODEV;                 /* -EPROBE_DEFER lost here */
```

Three failure points leave the dangling pointer: `ldo_init()`, the first
`request_threaded_irq()`, and the second `request_threaded_irq()`.  The
interesting one is `ldo_init()` because it is the ordinary deferred-probe
race: `twl6030_usb` is a `subsys_initcall`, the `omap-usb2` PHY is a
`module_platform_driver` (device initcall), and the `vusb` regulator is a
child of the same TWL MFD — so the first probe defers (PHY absent), and the
late deferred-probe retry can still find `vusb` unregistered.  Returning
`-ENODEV` then makes the deferral permanent.

Why the workaround hid it: the winchester early return in
`omap2430_musb_enable()` skipped `otg_set_vbus()` entirely, so the dangling
pointer was never dereferenced.  Restoring the stock call (session 65/66)
exposed it.

## 2. Why the report-264 evidence matches

* `r0` = `phy->comparator` = `c18d5280`; the kernel decoder says
  `kmalloc-192 start c18d5240 pointer offset 64 size 192`.  Every devm object
  in the same Oops is reported the same way (`otg` `kmalloc-128` offset 64,
  `glue` offset 64, `musb` `kmalloc-8k` offset 64), because `devm_kzalloc`
  places `data[]` 64 bytes into a `struct devres`.  So the comparator is a
  **devm** `kmalloc-192` object: `struct twl6030_usb`.
* `r3` = `comparator->set_vbus` = `c2c05800`, `kmalloc-1k start offset 0` — a
  heap pointer, not text.  A live `twl` has `twl6030_set_vbus` here.
* The two boots that faulted (session 66) and the three that now run are the
  same kernel plus this fix: the faulting boots must have taken the
  `ldo_init()` → `-ENODEV` → devres-free path; the fixed boots either get a
  ready regulator or, if it defers, clear the comparator and retry.  The boot
  log's `twl6030_usb … Initialized TWL6030 USB module` at ~0.8 s means the
  probe ultimately succeeded on the validated boots.

The static audit is conclusive on the lifetime defect; the bench result
confirms the restored stock call now runs on dual-core without the Oops.

## 3. The fix

`drivers/usb/phy/phy-twl6030-usb.c`:

```c
	twl->usb3v3 = regulator_get(twl->dev, "usb");
	if (IS_ERR(twl->usb3v3))
		return PTR_ERR(twl->usb3v3);

static void twl6030_usb_clear_comparator(void *data)
{
	omap_usb2_set_comparator(NULL);
}
	...
	ret = omap_usb2_set_comparator(&twl->comparator);
	if (ret == -ENODEV) { … return -EPROBE_DEFER; }

	ret = devm_add_action_or_reset(dev, twl6030_usb_clear_comparator, NULL);
	if (ret)
		return ret;
```

`drivers/phy/ti/phy-omap-usb2.c`:

```c
	phy = phy_to_omapusb(x);
	phy->comparator = comparator;
	usb_put_phy(x);
	return 0;
```

`drivers/usb/musb/omap2430.c`: the `of_machine_is_compatible(
"blackberry,omap4-winchester")` early return is removed from
`omap2430_musb_enable()`; the `set_mailbox_lock` mutex and the stock
`omap_musb_set_mailbox(glue)` call remain.

Board DTS: `panic=10` → `panic=0` (trap 335; revert to 10 once the bring-up is
past deliberate-fault images).

## 4. Bench validation

* Build: `7.3.3 #109 SMP Mon Nov 23 14:31:39 -03 2026 armv7l`; zImage
  7,468,216 B; image **`0x3547D613`**, 10,448,868 B (image `0x9F6FE4` B <
  `0xB00000`).
* Flash: `bbusb.py flash --component boot/chainloader/kernel.img
  --dest-block 0x825 --from-bootrom --password 0000 --apply` →
  `10551328/10551328 payload bytes, seq 644`, `COMPLETE 0x40C0 -> cmd 0x4006`,
  `done` (`local/work/flash-session67.log`).
* Three boots (the post-flash boot + two `reboot -f` cycles), each:
  `online=0-1`; `twl6030_usb … Initialized TWL6030 USB module` at ~0.8 s;
  `udc musb-hdrc.0.auto: binding gadget driver [g1]` at ~1.41 s; no
  `Unable to handle` / `Internal error` / `Kernel panic`.  The only
  `dmesg` line matching "Oops" is `mtdoops: mtd device … must be supplied`.
* `BOOTCTR_FCTRY`: `0x2a` (boot 1) → `0x29` (boot 2) → `0x28` (boot 3), each
  `>= 0x1e, no write`; `/init` will top up below `0x1e`.
* Evidence: `local/work/session67-boot3-evidence.txt`, the flash log, and the
  ACM transcripts (`local/work/reboot_verify.py`).

## 5. Patch

Re-exported with the session-65 method (reconstruct the shipped 65 files from
pristine + the previous patch, apply the four intended files, `diff -u -N`,
apply to a fresh pristine scratch and compare).  New export:

* **67 files / 9 136 lines**, sha256 `d19adff8…` (was 65 / 9 110,
  `6f9bef24…`).
* Verified **0 rejects / 67/67 identical**.
* Added files: `drivers/usb/phy/phy-twl6030-usb.c`,
  `drivers/phy/ti/phy-omap-usb2.c`.  Changed: `drivers/usb/musb/omap2430.c`
  (early return gone, mutex kept) and the board DTS (`panic=0`).
* Tree-vs-pristine walk (trap 211): 100 differing source files; 67 are the
  patch, the rest are generated build artifacts plus the known excluded
  diagnostics (`arch/arm/kernel/smp.c`, `omap-smp.c`, `omap-smc.S` blank line,
  `local/kernel-tree/kernel/smp.c`, `lib/fonts/Makefile`, `include/linux/usb/otg.h`).  Nothing
  functional was missed.

## 6. Open

* **The USB/VUSB teardown (trap 337) is unchanged.**  It is a separate
  fault: the only writer is `twl6030_usb_irq()` on a `VBUS_DET=0` read.  A
  `VBUS_DET`/`linkstat` log in that handler is still the way to settle it.
* Longer soak and more dual-core boots (3/3 here) — the fix removes a
  failure class, but the deferred-probe race's frequency is not measured.
* OTG host mode is now reachable again (the stock mailbox runs), but untested.
* `panic=0` is the bring-up setting; restore `panic=10` once deliberate-fault
  images are done.
