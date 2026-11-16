# 248 — the touch position ghost is fixed; the backspace/enter repeat is squeekboard's

Date: 2026-11-16 (session 58, Linux-era 52).
Bench: the unit boots the Nura overlay root on **region 0 `0xED58EF36`**
(`0x967CE0` B, `boot/chainloader/kernel.img` sha256 `c67b75c5…`, kernel
`7.3.3 #74`) and **hd3 `f7858110…`**.  The touch position ghost is gone; a
separate backspace/enter repeat was located downstream of the driver.

## 0. Headline

* **The position ghost is fixed.**  A fast `hello` now types correctly.  The
  fix, in `playbook-munin.c`: the poll keeps sampling while the controller
  asserts its IRQ line (and for a 200 ms grace period after the last contact),
  and a new contact that reappears at exactly its slot's previous coordinates
  within 300 ms is held back one sample and reported with the fresh position.
* **The backspace/enter repeat is not the touch driver.**  One tap = one clean
  contact (`down=01` → `down=00`, evdev `-1`), but squeekboard 1.43.1 sends
  three `0x7f` bytes for it (+0/+194/+234 ms).  A letter never repeats (one
  tap = one byte; a 1.3 s hold = one byte).  Upstream catacomb 1.7.0 replaced
  squeekboard with wvkbd; that is the clean fix.
* **DPMS-off cannot be woken by touch** on this image: the kernel receives the
  taps but the panel stays dark.  Kill `swayidle` on the bench; `catacomb msg
  dpms on` restores it.
* The kernel export is now **64 files / 8 981 lines**, sha256 `ca17a057…`,
  0 rejects / 64/64 byte-identical.

## 1. What the report actually contains

The controller asserts its IRQ line for a whole gesture and reuses contact slots
across the taps of a burst.  Two consequences, both measured:

1. A fast burst can produce an **all-up sample between two contacts** while the
   line is still low.  The old poll stopped at that sample, so the rest of the
   burst was never read — a fast `hello` arrived as `h,e,o`.  The install also
   allows a release to be missed, which shows up as a held key.
2. The first sample of a **new** contact can still carry the slot's **previous
   coordinates**, because the chip updates its report register after the IRQ.
   That is the ghost: `h,e,h,h,e` instead of `h,e,l,l,o`, and an in-contact move
   from the old position to the new one.

The vendor's own QNX driver (`libmtouch-munin.so`, report 194) uses the same
`is_contact_down`/`get_coords` parsing we do: `b3` is only a down flag, and
`get_contact_id` returns the digit index — there is no hidden contact-id
filter to copy.  The fix therefore lives in our sampling policy.

## 2. The fix (kernel `#74`)

* **Poll while the line is asserted, plus a grace window.**  The cause of the
  dropped burst taps is that the poll stopped at an all-up sample between
  contacts.  `munin_active()` keeps the 10 ms poll armed while any contact is
  down, while a down is pending confirmation, while `irq-gpios` reads asserted,
  and for `MUNIN_POLL_GRACE_MS` (200 ms) after the last contact.  The IRQ
  handler arms the poll too.  This also catches a release whose falling/rising
  edge the GPIO path does not deliver.
* **Confirm a new down on a fresh sample.**  For a slot that goes up→down, if
  the coordinates equal that slot's previous contact exactly and that contact
  lifted within `MUNIN_REPEAT_MS` (300 ms), the down is held back one sample and
  then reported with the current coordinates.  A genuine double tap is
  confirmed by the next sample (≤ one sample of extra latency); a stale first
  sample is replaced by the real position.
* **`irq-gpios = <&gpio2 3 GPIO_ACTIVE_LOW>`** in the board DTS gives the
  driver the line level (`gpiod_get_value` = 1 when asserted); the descriptor
  is optional, so a gpiolib conflict falls back to the grace window.
* The `trace` parameter now also prints `down=<mask>` and `line=<level>`.

The vendor's `get_seq_id` (an incrementing per-packet sequence, `arg+0xac`) and
`MTOUCH_FLAGS_INCONSISTENT_DIGIT_ORDER` were considered; the Munin driver does
not set them, and the measured data shows the slots are ordered.  The
down-confirm needs no contact id.

## 3. Evidence

Capture: `docs/captures/touch-munin-2026-11-16-session58.txt`.

* Fast `hello` on `#74`: the operator reports correct text (was `h,e,h,h,e` and
  once `heo`).
* Clean run, `a b c` then one backspace: four contacts, each one down and one
  up; the backspace contact is 131 ms at display (969,519), PBTS
  `raw=8a850401…` → `down=01`, then `up … down=00`.
* `keylog.py` on the terminal's stdin for that backspace tap: `7f` at 0 ms,
  `7f` at +194 ms, `7f` at +234 ms — three backspaces from one contact.
* Controls: one `a` tap → one `0x61`; an `a` held 1.3 s → one `0x61`.
* DPMS: with `bl_power=4`, taps still advanced `/proc/interrupts` for `1-0065`
  and appeared in evdev and `PBTS`, but `bl_power` stayed 4.

## 4. The repeat, and where it lives

`keylog.py` receiving three bytes for one contact rules out the driver (the
kernel's own evdev stream has one contact with a clean release).  Letters do
not repeat at all, including a 1.3 s hold, while BackSpace and Return do.  That
is squeekboard 1.43.1's action-key repeat (~200 ms delay, ~40 ms interval),
and the repeats continue ~100 ms past the driver's release — the key-release
event reaches squeekboard after the timer has fired.  Upstream **catacomb
1.7.0 switched the default virtual keyboard to wvkbd**, removing the
dependency on the unmaintained squeekboard (verified 2026-11-16).  The image's
`postmarketos-ui-catacomb` still depends on squeekboard; upgrading it is a
package job, because the catacomb GLES2 shim patch must be rebased onto the
newer catacomb/smithay.

## 5. Kernel, image, export

* Region 0 `0xED58EF36` (`0x967CE0` B, sha256 `c67b75c5…`, kernel `#74`),
  flashed with `bbusb.py --component … --dest-block 0x825 --from-bootrom`;
  `COMPLETE 0x40C0 -> cmd 0x4006`, `done`.  Region 0 only (the touch driver is
  built-in); hd3 unchanged.
* The boot resolved the optional line: `using DT …/touchscreen@65 for 'irq'
  GPIO lookup`; `Munin touchscreen ready (irq 110)`.
* Export: `kernel/patches/0001-playbook-winchester-7.3.3.patch`, 64 files /
  8 981 lines, sha256 `ca17a057…`, verified by applying to pristine 7.3.3
  (0 rejects) and comparing 64/64 byte-identical.  The walk excludes the
  vendored SGX driver (271 files) and `scripts/` (2 files) — without those
  exclusions it reports 337/337.

## 6. Next

1. **The upload-path profile** (`PBGL_PROFILE` catacomb build, staged) — the
   remaining performance item, and a candidate contributor to the late
   key-release delivery.
2. **Move the OSK to wvkbd** (catacomb 1.7.x) — the clean fix for the
   backspace/enter repeat; needs the GLES2 shim rebased.
3. `epitaph` (trap 292) and Kumo/WPEWebKit stay as before.
