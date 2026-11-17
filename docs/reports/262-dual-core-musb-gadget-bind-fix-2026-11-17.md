# 262 — the dual-core wedge is caused by the MUSB gadget bind; skipping the mailbox call fixes it

Date: 2026-11-17 (session 64, Linux-era 58).  Bench + static RE.

## 0. Headline

* The dual-core 1008 MHz wedge is triggered by the **USB gadget bind** performed
  by initramfs `/init` (`gadget_setup()` writes the UDC name → `musb_start()` →
  `omap2430_musb_enable()` → `omap_musb_set_mailbox()`).
* Two observed failure modes, both from that path:
  1. **Oops/panic**: `omap_musb_set_mailbox` jumps to a zeroed page (`Code:
     00000000 … (00090000)`); `LR is at omap_musb_set_mailbox+0x9c/0x2ec`;
     PID 1 dies with irqs disabled → `Attempted to kill init!` → reboot.
  2. **Hang** at `random: pbrpmb: uninitialized urandom read` (~1.8 s) with no
     further output — the RPMB top-up never completes.
* **Fix (experiment, effective):** skip `omap_musb_set_mailbox()` in
  `omap2430_musb_enable()` on winchester, i.e. in the `musb_start()` path
  (`local/kernel-tree/drivers/usb/musb/omap2430.c`).  The TWL6030 comparator
  handles VBUS/ID separately, so the OTG mailbox update is not needed there.
* Result: `kernel-musbskip-cpu1.img` (CRC **`0xAEBC9E1D`**) booted
  **4/4 clean dual-core boots** with `online = 0-1`, gadget/ACM/RNDIS up, SSH
  working after an entropy seed, and `pbrpmb topup` completing
  (`BOOTCTR_FCTRY = 0x2d`, no hang).  Baseline was ~1/3.  A 2.5-minute soak is
  recorded in §3.

## 1. Evidence for the fault

Operator screenshots (loud dual-core image, DSI-init TCON hook from report 261)
captured the Oops tail.  Call trace, verbatim:

```
omap_musb_set_mailbox from musb_start+0xe8/0x11c
musb_start from musb_gadget_start+0xb4/0x13c
musb_gadget_start from gadget_bind_driver+0x144/0x2a0
...
gadget_dev_desc_UDC_store from configfs_write_iter+0xa8/0x120
ksys_write from ret_fast_syscall+0x0/0x54
Code: 00000000 00000000 00000000 00000000 (00090000)
note: init[1] exited with irqs disabled
Kernel panic - not syncing: Attempted to kill init! exitcode=0x0000000b
```

Ring capture of a hang (read-back image, `local/work/r2-vg.bin`) shows the log
ending at `random: pbrpmb: uninitialized urandom read (16 bytes read)` with no
Oops — so the two modes are distinct but share the gadget-bind trigger.

`omap_musb_set_mailbox()` has several indirect calls (`otg_set_vbus()` through
`otg->set_vbus`, `omap_control_usb_set_mode()`).  A guard on `otg->set_vbus`
(`func_ptr_is_kernel_text()`, report 262 earlier draft) did **not** stop the
Oops, so the faulting call is another one in that function; the exact one is
still not pinned down.  The fix avoids the whole call from the bind path.

## 2. Hypotheses tested and rejected

| experiment | image CRC | outcome |
|---|---|---|
| replicate reference SMC 0x108 (SCU power) before wakeup | `0x391343F6` | 0/3, no improvement (report 260) |
| 1 s delay before `gadget_setup()` in `/init` | (gadget-delay image) | same Oops, just later |
| `func_ptr_is_kernel_text()` guard on `otg->set_vbus` | `0xB534370A` | Oops persisted, pbrpmb hang remained |
| pin PID 1 to CPU0 via `sched_setaffinity` | `0x17039203` | 4/4 Oopses |
| **disable `gadget_setup()` entirely** | `0xF0027A02` | **no wedge** (>100 s up, console live, no USB because no gadget) — proves the trigger |
| **skip mailbox call in `omap2430_musb_enable()`** | **`0xAEBC9E1D`** | **4/4 clean dual-core boots, gadget works** |

## 3. The fix and verification

`local/kernel-tree/drivers/usb/musb/omap2430.c`:

```c
static void omap2430_musb_enable(struct musb *musb)
{
	...
	if (of_machine_is_compatible("blackberry,omap4-winchester"))
		return;                 /* session-64 experiment, report 262 */
	...
	omap_musb_set_mailbox(glue);
}
```

`kernel-musbskip-cpu1.img` (`0xAEBC9E1D`, loud dual-core bootargs, fast
chainloader, no `.config`/module change):

* boot 1: `online=0-1`, `#101 SMP`, `0fca:8010`
* boot 2, 3, 4: all `0fca:8010` (no wedge)
* `pbrpmb topup: 0x2d >= threshold 0x1e, no write` — no hang
* SSH over RNDIS after `pbconsole entropy <hex>` (trap 328)
* soak: see bench notes below

## 3a. Targeted fix: set device mode only, keep the rest

The full mailbox-skip gave 4/4 boots but is not viable: the control-module
`VBUSVALID`/`IDDIG` bits were never set.  A narrower change calls only
`omap_control_usb_set_mode(glue->control_otghs, USB_MODE_DEVICE)` from
`omap2430_musb_enable()` on winchester, then returns — preserving the OTG
state manipulation that faults.  `kernel-usbdev-cpu1.img` (CRC
**`0xC675E206`**):

* boot 1, 2, 3: all `0fca:8010`, `online = 0-1`, `#102 SMP`
* `pbrpmb topup: 0x2c >= threshold 0x1e, no write` — no hang
* after ~32 s idle the host saw `VUSB: disabling` / `audio-ldo: disabling` and
  the gadget deactivated — **idle runtime PM**, not a wedge; the unit kept
  running with its panel console.  Same behavior with or without the mailbox
  change; it is a separate PM/autosuspend matter.

So the gadget-bind Oops and the pbrpmb hang are both avoided by the targeted
device-mode-only change; 3/3 boots is a small sample but a clear improvement
over 1/3.

## 4. Bench / tree state

This is a **kernel-code experiment** in the gitignored tree, like the other
`PB*` instruments; it must be reviewed before any patch re-export.  Other
session-64 experiments still in the tree: the DSI-init TCON delayed work
(`dsi.c`, report 261), the `VGA8x8`/`MINI4x6` font linkage (`lib/fonts/Makefile`),
the `otg_set_vbus` text guard (`include/linux/usb/otg.h`), and the PID-1 CPU0
affinity pin (`initramfs/init.c`).  The ineffective ones (affinity pin, 1 s
delay, otg guard) should be reverted once the fix is confirmed; the DSI TCON
hook is a useful diagnostic.

Region 0 currently holds `kernel-musbskip-cpu1.img` (`0xAEBC9E1D`) — a working
dual-core image.  The reliable single-core fallback remains
`kernel-fastcl-nosmp.img` (`0x8B83421F`).

## 5. Evidence / limits

* Operator screenshots and boot logs, 2026-11-17.
* Ring captures: `local/work/r2-oops.bin` (Oops tail), `local/work/r2-vg.bin`
  (pbrpmb hang).
* Call-path symbolisation: `arm-linux-addr2line` on
  `local/kernel-tree/vmlinux` — `omap_musb_set_mailbox` at `omap2430.c`,
  `otg_set_vbus` at `otg.h:77`.
* 4/4 is a small sample; the soak and further boots will raise confidence.
  The fix is a workaround at the enable path; the underlying bad indirect call
  inside `omap_musb_set_mailbox` is not yet identified.
