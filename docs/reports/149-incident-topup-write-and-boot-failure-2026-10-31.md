# 149 — INCIDENT: the first Linux RPMB top-up was written, and the unit stopped booting

**Session 20 (absolute), Linux-era session 14.  2026-10-31, ~22:38.**
Status: **the unit does not enumerate after a reboot; the operator reports a
blinking red LED.**  This report is the factual record, written before a host
reboot.  **No further RPMB write is permitted**; the recovery path is the
operator's CFP.exe (`bb10mt`) route, or the eMMC procedure if the record is
damaged.

---

## 1. What was written, in order

All writes went through `pbrpmb` on the Linux test image **`0x0A52B4B9`**, with
the frame MAC computed with the pinned frame key and verified host-side before
each write.

1. **`replay-ident 0x43 yes`** — a rehearsal: read block 0x43 from media and
   re-send *its own 256 bytes*.  Result: `write ioctl 0, result=0`, read-back
   frame MAC ok, `data matches`.  **This proves the frame construction, the
   write-counter handling and the device's acceptance.**  Content unchanged by
   construction.
2. **`replay 2 yes`** — the top-up: write the captured genuine block 2
   (`BOOTCTR_FCTRY = 32 00 00 00` **and** `TEST_PAD = de ad be ef`) over the
   decremented block.  Result: `result=0`, read-back frame MAC ok, `data
   matches`, `PASS - media holds the captured 0x32 block again`.

Capture and counters: block-2 capture from `RpmbInit` page paddr `0xbf0dc000`
(frame paddr `0xbfb2a000`); write counter 4427 for the top-up.

## 2. What happened next

The sealed-IFS restore was started (22:38:56) and the unit was told to reboot.
`docs/reports/data/session20-capture/fr3.log` shows it stopped at
`waiting for bootrom ...`: **the flasher never wrote a block.**  The command was
then aborted.  So **region 0 was not modified** by that attempt.

The unit now: **no `0fca` USB device at all**; the host's xHCI port repeatedly
tries to enumerate a **full-speed** device and fails (`device descriptor
read/64, error -110`, `-71`, `-62`, `unable to enumerate`).  Operator: unit does
not boot, **red LED blinking**.

## 3. Two hypotheses, not yet distinguished

* **H-power.** The unit has run on USB bus power all session; a flat/weak
  battery would explain a blinking red LED and a USB device that cannot
  complete enumeration.  No evidence against this.
* **H-record.** The replayed block may not unseal to a valid record if the
  monitor's seal is **write-counter-dependent** (or otherwise mixes state we
  do not control).  In that case the bootloader sees a damaged
  `BOOTCTR_FCTRY`, which on the factory path is the documented hard-lock
  (`SAFETY.md` §2).  The device accepted and stored our bytes; the *read-back
  proves storage, not semantic validity*.

Evidence bearing on H-record, both ways:

* *For determinism:* the zero-plaintext ciphertext block `608c40a6 49bb6c8b
  3d1d4bb4 0e69ce42` appears identically in two different record blocks
  captured on different boots (block 0x43 in session 19, block 2 in session 20).
* *Against determinism:* session 19's block-0x43 pair never reproduced with any
  key/framing, and neither did the fresh block-2 pair — consistent with the
  transform mixing state that changes between calls (counter/nonce) rather than
  with a plain block cipher we have not keyed correctly.
* *Decisive but unavailable:* whether the bootloader accepts the record now.

## 4. Evidence preserved in the repository

`docs/reports/data/session20-capture/` (copied out of `/tmp` before the host
reboot):

| file | what |
|---|---|
| `page20.bin/.txt` | the full 4 KiB `RpmbInit` page, first capture (BOOTCTR only), paddr `0xbfb86000` |
| `frame20.bin/.txt` | the matching frame at `0xbf0cf000` (read response, addr 2) |
| `page_tp.bin/.txt` | the page **with `TEST_PAD` in block 2**, paddr `0xbf0dc000` |
| `frame_tp.bin/.txt` | **the exact 256 bytes written to block 2** (read response, addr 2, result 0) |
| `page_tp2.bin/.txt` | the page later showing a block-0x43 seal in progress |
| `frame43.bin/.txt` | the frame at `0xbfb2a000` (block 0x43 write request, data zeroed by the resmgr) |
| `replay_dry.txt`, `replay_frame.bin` | the built top-up frame and the console dry-run output |
| `pair2.h` | the generated pair header from the first block-2 capture |
| `f*.log`, `fr*.log` | every flasher log of the session (`fr3.log` is the aborted restore) |

Also in the repo: `docs/reports/data/rpmb-block2-replay-2026-10-31.txt` (the
written block hex + its plaintext, parsed live), `kernel/source/pbmon-page.h`
(the whole page embedded for the window sweep).

## 5. What may and may not be done next

* **Allowed:** power/battery recovery; reading the unit over any interface that
  appears; re-flashing **region 0** (IFS) — that path is independent of RPMB and
  is the documented recovery; the operator's CFP.exe / `bb10mt` route
  (`DEVELOPMENT.md` §9).
* **Forbidden now:** any further RPMB write, any "repair" write to
  `BOOTCTR_FCTRY`, and any attempt to *delete* the record from another OS (there
  is no writer without a booted OS; the bootloader cannot write it).
* If the unit boots again, the **first** action is to read
  `BOOTCTR_FCTRY` and confirm its value, then decide whether the replay result
  was semantically valid or not.

## 6. Reaching the unit again: the ladder (read-only on the unit)

The unit presents a **full-speed** USB device that cannot answer
`GET_DESCRIPTOR` (`-110`, `-71`, `-62`; Windows CFP "unknown descriptor").  Two
hosts failed, but **both were this machine's AMD xHCI controllers**, so the host
is the first variable to eliminate.  In order, cheapest first:

1. **A different USB path.**  Another computer, or — on this machine — a
   **USB 2.0 hub** between the unit and the port (its transaction translator
   re-times full-speed devices, which is exactly the case here).  Cheap, and it
   is the one untested variable.
2. **Power.**  A wall charger for a while; hold the power button ~20 s and
   release; then reconnect with a USB-level capture running.
3. **Capture what the unit actually does on the wire** (`usbmon`, host side,
   needs root):
   ```bash
   sudo modprobe usbmon
   ls /sys/kernel/debug/usb/usbmon/        # pick the bus the unit is on
   sudo tcpdump -i usbmon3 -w /tmp/usb.pcap    # then connect/power the unit
   tshark -r /tmp/usb.pcap -V | head -100
   ```
   What the answer means:
   * **SET_ADDRESS answered, GET_DESCRIPTOR times out or returns short/garbage
     →** firmware is alive but crashing or reset-looping during enumeration
     (the reset-loop hypothesis: WDT or a fault, USB re-initialised each cycle).
   * **The device repeatedly disappears/reappears →** reset loop confirmed.
   * **No response after the initial reset →** the stage is dead before USB;
     the USB pull-up we see is the PHY, not a stack.
   * **A descriptor with a TI VID (`0x0451`)** → the OMAP boot ROM is in its
     peripheral-boot mode (recoverable through the loader protocol, not CFP).
   * **`0fca:0001`** → RIM's loader is up: run
     `tools/pre_os_ifs_flash.py --template-ifs local/images/ifs0.sealed.bin`
     (with `--delay` if it appears only periodically).
4. **Try the flasher blind.**  `pre_os_ifs_flash.py` waits for `0fca:0001`;
   with `--delay <s>` it samples across a reset cycle.  If the loader ever
   appears, the sealed IFS goes back without touching region 0's neighbours.
5. **If nothing enumerates at all**, the documented routes are the operator's
   CFP.exe/UFS restore (`/home/psyden/Pdrive/bb10mt/`) or the eMMC procedure —
   and the static RE (reports 150/151) is what tells us whether a damaged record
   can even produce this symptom.

## 7. Mechanism — resolved by the static RE (reports 150/151)

The symptom is no longer a mystery, and it is **not** "the CPU crashed":

* a bad `BOOTCTR_FCTRY` (length ≠ 4, value `0`/`>0x32`, or a read error) selects
  path **2**; **and** the bootloader's own *decrement write* runs after the
  region verify, and **its failure is ignored** by the caller — either way boot
  main prints `"BootModeCount expended"` and **halts at `0x800094BE`**
  [P] report 150 §1/§2;
* the halt is a bare `b .` with **WDT2 armed** (`WCRR=WLDR=0xFFFC1800`,
  ≈7.8 s) and **not kicked**, so the SoC **globally resets** and re-enters the
  same path — an **endless ~7.8 s reset loop** [P] report 150 §5;
* `arm9000` programmes the MUSB core (`0x4A0AB000`) but never the TLL/PHY and
  has **no descriptors and no `GET_DESCRIPTOR` handler**, so any link that
  appears is a **full-speed device that cannot enumerate** — exactly the two
  host reports [P] report 151 §4;
* a corrupt record **cannot** produce a data abort (record fields are `u16`
  offsets into a base in `0x8003xxxx`; `SCTLR.A` is never set) [P] report 150 §3.

**Later correction from the bench (operator, 2026-10-31 ~11:30).**  The unit
does **not** present any USB device at all now (no host notification on either
OS), and it shows a **deliberate red-LED blink code**, measured from video:

```
on 03.508-03.838 | 04.742-05.034 | 05.357-05.687 | 06.578-06.875 | 07.172-07.502
repeat at 10.855
```

i.e. **1 blink – 2 blinks – 2 blinks**, ~300–330 ms on, ~300 ms between blinks
in a group, ~900 ms between groups, ~3.35 s between cycles: **the whole cycle is
≈7.35 s**.  That is close to the WDT2 period (≈7.8 s at `0xFFFC1800`/32.768 kHz),
so the two readings are: either the loader **deliberately** blinks an error code
and the WDT resets it anyway, or each reset re-emits the same code — report
**153** is the static hunt for that routine.  What this *does* settle: the code
**runs and signals**, so this is not "a dead CPU", and the "catch the bootrom
window with `--delay`" idea is much weaker than it looked, because **no USB
device ever enumerates**.

Consequences: (a) this failure mode is **reachable on a valid record too** — a
failed decrement write halts the unit just like a bad record, so every boot is a
write that can fail; (b) the recovery question now hinges on decoding the blink
code (report 153) and on whether the ROM can be made to enter USB boot at all
(report 152).

## 8. Resolution: the LED pattern is error code `0x5B`, and nothing resets

Report **153** decoded the red LED: it is a **FAN5702 I²C LED driver** (7-bit
`0x36`, red channel) driven bit-serially as **error code `0x5B`** — `1` = ON/OFF
20 units, `0` = OFF 40 units, end = OFF 200 units, which for `0x5B = 1011011`
gives exactly the measured **1-2-2** groups and a **7344 ms** period against the
measured 7347 ms.

`0x5B` means **"NV-record / RPMB subsystem unusable"**, emitted from four sites:
catalogue mount, RPMB probe/provision, the `BOOTCTR_FCTRY` selector, and the
counter re-seal.  **The SoC is not resetting** — the emitter loops
(`0x80014D04 b.n 0x80014CB4`) and kicks WDT2 ~48 000 times per cycle — and
`arm9000` never brings up a USB device that can answer descriptors, so the unit
is unreachable from any host.

What this means for the record: the unit is in the terminal state the bootloader
enters when **the NV/RPMB subsystem cannot be used at all**, which is consistent
with (and does not distinguish between) "the record we wrote is unusable" and
"the store is unusable".  Either way, **the only documented recovery is the eMMC
route**; there is no host-side path, and no further write should be attempted
from any OS.
