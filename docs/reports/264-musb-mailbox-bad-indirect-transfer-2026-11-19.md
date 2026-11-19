# 264 — the bad indirect transfer in `omap_musb_set_mailbox` is `otg->set_vbus` → a slab address

Date: 2026-11-19 (session 66, Linux-era 60).  Bench + disassembly.

## 0. Headline

The session-65 HANDOFF's one experiment is complete.  An instrumented
stock-mailbox kernel (winchester early return removed, `set_mailbox_lock`
mutex kept, `panic=0`) was booted twice on the ARMv7 dual-core build; the
`PBMBX` markers and the Oops were recovered from the DRAM ring via a warm
reset + read-back image.

* **The pointers are valid at entry.**  `omap_musb_set_mailbox()` is entered
  once, from PID 1's gadget bind, with `status = 4` (`MUSB_VBUS_OFF`), a valid
  `xceiv`, a valid `otg`, and **`otg->set_vbus = 0xc0729b94`** — that address is
  `omap_usb_set_vbus` in `System.map`, i.e. correct kernel text.  The notifier
  head is `NULL`.  So the "stale `usb_phy`/`usb_otg` at entry" branch of the
  handoff is **not** what happens.
* **The fault is the `otg_set_vbus()` indirect call in the VBUS-off branch,
  one level down.**  Per-branch markers show `sv` still valid immediately
  before the call; the very next instruction faults.  The Oops PC is
  **`0xc2c05800`**, which the kernel's own register decoder identifies as
  **a `kmalloc-1k` slab object (offset 0)** — not kernel text.
* **The bad pointer is `phy->comparator->set_vbus`.**  `otg->set_vbus` points
  to `omap_usb_set_vbus()` (`drivers/phy/ti/phy-omap-usb2.c`), which is a
  tail-call (`bx r3`): it loads `otg->usb_phy → struct omap_usb → comparator
  (struct phy_companion *) → comparator->set_vbus` and branches to it.  The
  comparator's `set_vbus` holds `0xc2c05800`, a slab address, instead of the
  `twl6030_set_vbus` text pointer that `twl6030_usb_probe()` installs.  So the
  comparator object is stale/corrupt: a **PHY-lifecycle** problem, exactly the
  remaining branch the handoff named, but in the TWL6030 comparator registered
  into the `omap-usb2` PHY, not in `musb->xceiv` itself.
* The device-mode-only workaround stays: a clean boot needs the stock call
  skipped.  The next step is the PHY lifecycle audit (§5), not more markers.

## 1. The experiment

Edits in the gitignored tree (`local/kernel-tree`, backed up to
`local/work/omap2430.c.session66-orig` and
`local/work/omap4-winchester.dts.session66-orig`):

1. `drivers/usb/musb/omap2430.c`: removed the winchester early return in
   `omap2430_musb_enable()` so the stock `omap_musb_set_mailbox(glue)` runs
   from the bind path; kept the `set_mailbox_lock` mutex.
2. Added `printascii()` markers using `snprintf()` into a static buffer:
   an **entry** marker (cpu, status, `glue`, `musb`, `xceiv`, `otg`,
   `otg->set_vbus`, `xceiv->notifier.head`) before the lock, a **prechain**
   marker before `atomic_notifier_call_chain()`, and — on the second build —
   `pre-periph` / `post-periph` / `pre-setvbus` / `post-setvbus` markers around
   `musb_set_peripheral()` and `otg_set_vbus()` in the VBUS-off branch.
3. Board DTS `bootargs`: `panic=10` → **`panic=0`** (trap 335: a fault hangs
   instead of boot-looping and draining `BOOTCTR_FCTRY`).
4. Rebuilt `zImage` + DTB, wrapped, flashed region 0, booted.

Builds:

| image | CRC | markers |
|---|---|---|
| `local/work/kernel-mbxmark-panic0.img` | `0xF1AA4568` | entry + prechain |
| `local/work/kernel-mbxmark2-panic0.img` | `0x405E8997` | + the four branch markers |

Both builds required a warm reset and a reflash of the quiet read-back image
(`kernel-readback-quiet.img`, `0x5BBCD22E`) to recover the ring; the
chainloader hand-off (trap 321) preserved the hung boot's characters.  Raw
dumps and the verbatim decode are in
`docs/captures/musb-mailbox-fault-2026-11-19.txt`.

## 2. What the ring says

Decoded entry marker (build 2, cleanest capture; some bytes corrupted by the
shared unlocked ring, trap 332):

```
PBMBX entry cpu=0 status=4 glue=c1e460c0 musb=c18ee040 xceiv=c2838440 otg=c1fd34c0 sv=c0729b94 nh=00000000
PBMBX pre-periph  … sv=c0729b94 nh=00000000 st=1
PBMBX post-periph … sv=c0729b94 nh=00000000 st=1
PBMBX pre-setvbus … sv=c0729b94 nh=00000000 st=1
[    1.419006] Unable to handle kernel execution of memory at virtual address c2c05800 when execute
[    1.419891] PC is at 0xc2c05800
[    1.419891] LR is at omap_musb_set_mailbox+0x1e0/0x4bc
[    1.421508] Register r3 information: slab kmalloc-1k start c2c05800 pointer offset 0 size 1024
```

* `status=4` is `MUSB_VBUS_OFF` (`include/linux/usb/musb.h` enum: undefined 0,
  ID_GROUND 1, ID_FLOAT 2, VBUS_VALID 3, VBUS_OFF 4).  So the bind path enters
  the `MUSB_ID_FLOAT`/`MUSB_VBUS_OFF` branch: `last_event = USB_EVENT_NONE;
  musb_set_peripheral(); otg_set_vbus(otg, 0); omap_control_usb_set_mode(…
  USB_MODE_DISCONNECT)`.
* `sv = 0xc0729b94` appears in **all five** markers, including the one
  immediately before `otg_set_vbus()`.  `grep` of `System.map` resolves
  `c0729b94 t omap_usb_set_vbus`.  The pointer is valid right up to the call.
* There is **no `PBMBX prechain` line**: the fault is before the notifier
  chain, inside the VBUS-off branch.
* Build 1's equivalent capture jumps to `0xc2d49400`, also decoded as a
  `kmalloc-1k` slab object — the same class of bad pointer, different slab
  instance.

## 3. Pinning the faulting instruction

`omap_musb_set_mailbox+0x1e0/0x4bc` from build 2 disassembles (via
`arm-linux-objdump -d local/kernel-tree/vmlinux`) to the `blx` in the inlined
`otg_set_vbus()`:

```
c0a23e24: ldr r1, [r4, #756]      @ r1 = musb->xceiv
c0a23e28: ldr r2, [r1, #20]       @ r2 = xceiv->otg      (= c1fd34c0)
c0a23e30: ldr r6, [r2, #32]       @ r6 = otg->set_vbus   (= c0729b94, valid)
c0a23e48: blx r6                  @ call otg->set_vbus(otg, 0)
c0a23e4c: …                       @ +0x1e0 = LR on the fault
```

`omap_usb_set_vbus()` itself is a short tail-call chain
(`c0729b94 <omap_usb_set_vbus>`):

```
ldr r3, [r0, #8]      @ otg->usb_phy
ldr r0, [r3, #0xc0]   @ phy->comparator
ldr r3, [r0]          @ comparator->set_vbus   <-- c2c05800
cmp r3, #0
bx  r3                @ jump to comparator->set_vbus
```

The Oops registers agree: `r2 = c1fd34c0` (`otg`, a `kmalloc-128` object at
offset 64), `r3 = c2c05800` (the jump target, `kmalloc-1k`), `r6 = c0729b94`
(the valid `omap_usb_set_vbus` text address).  So the indirect transfer that
faults is **`phy->comparator->set_vbus`**, not `otg->set_vbus`.

Source (`drivers/phy/ti/phy-omap-usb2.c`, `drivers/usb/phy/phy-twl6030-usb.c`):

```c
/* phy-omap-usb2.c */
static int omap_usb_set_vbus(struct usb_otg *otg, bool enabled)
{
	struct omap_usb *phy = phy_to_omapusb(otg->usb_phy);
	if (!phy->comparator || !phy->comparator->set_vbus)
		return -ENODEV;
	return phy->comparator->set_vbus(phy->comparator, enabled);  /* <-- fault */
}
/* probe: otg->set_vbus = omap_usb_set_vbus (when OMAP_USB2_HAS_SET_VBUS) */
/* twl6030_usb_probe(): twl->comparator.set_vbus = twl6030_set_vbus; then
 * omap_usb2_set_comparator(&twl->comparator) stores that pointer in phy. */
```

The comparator pointer was written once at probe; a stale/freed or corrupted
`struct phy_companion` explains a slab address where a function pointer
should be.

## 4. Result matrix (handoff decision tree)

The handoff asked: valid pointers at entry + later fault ⇒ corruption during
the call; wild pointer at entry ⇒ stale object, audit PHY lifecycle.  The
measured answer refines both:

| handoff branch | observed | verdict |
|---|---|---|
| wild `xceiv`/`otg`/`sv` at entry | all valid, `sv` = kernel text | **ruled out at the `musb->xceiv` level** |
| fault during the call, notifier chain | no `prechain` marker; fault before it | not the notifier chain |
| corruption of a nested object during the call | `otg->set_vbus` (omap-usb2) valid, but `comparator->set_vbus` = slab | **confirmed one level down** — the TWL6030 comparator companion in the omap-usb2 PHY |

So the dominant reading is the handoff's *second* branch (stale object → audit
the PHY lifecycle), but the stale object is the `phy_companion` registered by
`twl6030_usb_probe()`, not `musb->xceiv` itself.  The session-64 observation of
a slab address in `otg->set_vbus` was likely this same nested pointer seen
through a different symbolisation.

## 5. Next step — the PHY lifecycle audit (NOT more markers)

The instrumentation question is answered; adding branch markers further has
no value.  Audit, in this order:

1. **Comparator registration/lifetime**: `omap_usb2_set_comparator()` in
   `drivers/phy/ti/phy-omap-usb2.c` (called from `twl6030_usb_probe()` at
   `drivers/usb/phy/phy-twl6030-usb.c:358`) stores `&twl->comparator` in
   `phy->comparator`.  `twl` is `devm_kzalloc`'d.  Check for a probe
   deferral / rebind that frees `twl` while leaving `phy->comparator`
   pointing at freed memory (trap 337's `twl6030_charger` module probing with
   `vmax -22 imax: -22` is a live clue that this PMIC block is being touched
   by more than one driver).
2. **The other writer of `phy->comparator`**: `cpcap-charger.c:921/940` also
   calls `omap_usb2_set_comparator(&ddata->comparator)` / `(NULL)`.  On this
   board cpcap should not probe, but confirm it does not clobber the pointer.
3. **Physical readback**: with the markers, also print `phy->comparator`,
   `phy->comparator->set_vbus` and `xceiv->otg->usb_phy` at entry, and the raw
   word at `otg + 0x20` again after `musb_set_peripheral()`, to catch a write
   during the call.  This is only needed if the static audit is inconclusive.

The device-mode-only workaround stays until this is understood and fixed; a
fix must restore the stock `omap_musb_set_mailbox()` call for OTG host mode.

## 6. Bench state at the close

After the experiment the working image was restored: region 0 =
`local/work/kernel-armv7-console-cpu1.img`, CRC **`0xC9FEE2D1`** (10,450,108 B,
sha256 `b16f553a…`), flashed at block `0x825` (`flash-restore-session66.log`:
`COMPLETE 0x40C0 -> cmd 0x4006`, `done`).  The unit booted to the Nura root on
hd3 (operator observed a clean boot after a few reset cycles); the USB link
subsequently dropped at ~60 s, consistent with the intermittent VUSB teardown
(§0d of the handoff, trap 337), while the unit kept running.  The gitignored
build tree was returned to the session-65 state: both edited sources match
their backups byte-for-byte (sha256 `c57c3e24…` for `omap2430.c`,
`46dffeb2…` for the DTS), the rebuilt `vmlinux` has zero `PBMBX` strings and
the DTB bootargs are back to `panic=10`.  No tracked patch/DTS change: the
export remains 65 files / 9 110 lines, sha256 `6f9bef24…`.

## 7. Evidence

* Decoded ring: `docs/captures/musb-mailbox-fault-2026-11-19.txt`; raw dumps
  `local/work/r2-session66.bin`, `local/work/r2-session66-m2.bin`.
* Images: `local/work/kernel-mbxmark-panic0.img` (`0xF1AA4568`),
  `local/work/kernel-mbxmark2-panic0.img` (`0x405E8997`), the read-back image
  `local/work/kernel-readback-quiet.img` (`0x5BBCD22E`), and the restored
  `local/work/kernel-armv7-console-cpu1.img` (`0xC9FEE2D1`).
* Flash logs: `local/work/flash-session66.log`, `flash-readback-session66.log`,
  `flash-session66-m2.log`, `flash-readback-session66-m2.log`,
  `flash-restore-session66.log`.
* Source backups / experiments: `local/work/omap2430.c.session66-{orig,markers}`,
  `local/work/omap4-winchester.dts.session66-{orig,panic0}`.
* Disassembly: `arm-linux-objdump -d local/kernel-tree/vmlinux` at
  `omap_musb_set_mailbox` (`c0a23c6c`, `+0x1e0`) and `omap_usb_set_vbus`
  (`c0729b94`); `System.map`; `local/kernel-tree/drivers/phy/ti/phy-omap-usb2.c`
  and `local/kernel-tree/drivers/usb/phy/phy-twl6030-usb.c`.
* Prior: reports 262/263 (gadget-bind trigger, mutex failure), 261 (DSI-init
  TCON hook made the panic visible), 258 (IPI sent/serviced); trap 335
  (`panic=10` boot-loop).
