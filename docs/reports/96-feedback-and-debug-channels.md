# 96 — Feedback and debug channels for the CRC-booted Linux payload

Status: design, 2026-10-25. Read-only; no device writes. Inputs: `LED-RE.md`,
`KEXEC-DESIGN.md`, `kexec/README.md`, `probe.S`, `hello.S`, `qnx2linux.c`, `88`, `91`,
`69`, `66`.

## 0. The constraint that shapes everything

The CRC payload entered from the bootloader is **not** in QNX: no `/dev/*`, no services,
no USB, no filesystem. Entry state (`91`): ARM SVC, IRQ/FIQ masked, **MMU off**, caches not
cleared by the boot path, WDT2 running and kicked immediately before the jump. Physical
addresses are directly usable.

So the feedback channels must be **bare-metal**. There are four that work without a driver,
plus two that only become available once Linux is up.

## 1. LED — the live human channel (proven)

**What works:** the FAN5702 enable pin is **GPIO1_13**, `GPIO1 @ 0x4A310000`:
`CLEARDATAOUT +0x190` (EN low = chip reset, LED off), `SETDATAOUT +0x194` (EN high =
registers default, LED lit). This is **NS-writable and proven live** (`probe.S`).

**What does not:** colours need I2C4 (`0x48350000`, FAN5702 addr `0x36`, `GENERAL 0x10`).
I2C4 is clock-gated at idle and **PRCM CLKCTRL writes abort from NS** (`probe.S` note,
`LED-RE.md` §6). In the CRC/bootloader context I2C4 has not been initialised by QNX, so
colour is not available unless we find a legal clock-enable path.

**Practical encoding:** blink `step` times, then a long gap; a distinct pattern for
"failed at step N" (e.g. N fast blinks, pause, repeat) and a solid-on "parked". This gives
"alive + how far" with no host, no RAM readback, no QNX. It is the only channel that works
even if the readback path is broken.

**Open:** whether the bootloader leaves `GENERAL` at a colour before the jump; if so, EN
high would light that colour rather than white. Test with a one-shot payload.

## 2. RAM breadcrumbs and rings — the precise channel (proven on kexec)

The kexec project already characterises the survivable DRAM band and the format:

- **Primary block: `0x90000000`.** Mirrors: `0x94000000`, `0x88000000`, `0x9FE00000`.
  Survivor band `0x85–0xA1`; `0x9F000000` is churned by the reboot; `0x9FE00000` is the
  most reliable mirror in the kexec notes.
- **Format** (`qnx2linux.c`, `hello.S`): `[0] magic 0x4c424b43`, `[1] step`,
  then channel-specific words. The kernel DEBUG_LL rings use, in the same pages,
  `count @+0x80`, `index @+0x84`, `1024 chars @+0x100`.
- **Stores:** with MMU off, plain physical stores. If the boot path left L2 on, flush the
  breadcrumb line by PA: PL310 `0x48242768` (CIPA) + `0x48242730` (sync); by-way ops
  deadlock and PL310 config is NS-filtered (`SESSION-RECORD`).
- **Preservation:** a **WDT2 warm reset** preserves DRAM. Write the final step, then hold
  both cores (`RM_PDA_CPU0/1_RSTCTRL` `0x4824340C`/`0x4824380C`, bit 0) and stop kicking
  WDT2 (`0x4A314000`, kick `+0x30`, ~58.6 s). Do **not** let the TWL6030 PMIC watchdog
  fire (127 s, power-off destroys RAM).
- **Readback:** from QNX via `memdump3`: `on -C 0 /tmp/memdump3 90000000 0x18`, plus the
  ring pages. This is how the kexec project reads every jump result.

**The coupling problem:** the readback needs the next boot to be QNX. In the CRC/bootloader
setup region 0 is the Linux payload, so after the WDT2 reset the bootloader re-runs the same
payload — the breadcrumbs are never read by an OS, and the unit can loop. See §5.

## 3. UART3 and the monitor ring — the console if we can see it

- UART3 `0x48020000`, 115200 8N1. Writing is safe and survives (`probe.S` step 57).
- **External pads were not found** in a 2-hour hunt (`kexec/README.md` #11,
  `uart-hunt/README.md`). Do not rely on a visible console.
- **Possible capture:** the kexec notes call `0x94000100` "ring3, the secure monitor's
  UART3 capture — persistent across reboots". If that is real for the pre-OS context, UART3
  writes become a durable text log without pads. **Unverified** for a bootloader-stage
  payload; cheap to test (write a marker, warm-reset, read `0x94000080`/`0x94000100`).

## 4. Kernel logs once Linux is up

Reuse the kexec project's `DEBUG_PLAYBOOK_BC` (`debug/omap4bc.S`): every early debug char
goes to UART3 **and** into the three rings, with PL310 clean+invalidate by PA per char.
That is already in the kernel tree and proven. So the same ring readback covers
pre-decompression, earlyprintk and panic output. Later, once the display or eMMC works, the
kernel can dump the rings itself (framebuffer or a reserved sector).

## 5. The readback/recovery coupling — the important design decision

There are two ways to close the loop:

**A. Bring Linux up via kexec first (recommended for early milestones).**
QNX stays on flash; a failed jump is followed by the WDT2 warm reset and **QNX boots
again**, so `memdump3` can read the breadcrumbs automatically. The kexec project already
has T2/T3 and the ring capture. Use it to validate kernel entry, DTB, UART/ring output and
early drivers. Only transplant a known-good payload into the CRC image afterwards.

**B. Make the CRC payload self-restoring.**
Keep a copy of the TabletOS IFS (and the trailing sectors) in **region 1** or another
reserved area. The chainloader:
1. writes breadcrumbs/LED for each stage;
2. on failure, timeout, or a "recovery" flag/button, copies the TabletOS image back over
   region 0 (`/dev/hd1` from 0x100000) and **then** forces the WDT2 reset;
3. the next boot is TabletOS, which can be read over SSH and can read the breadcrumbs.

This requires the payload to drive eMMC (or call the bootloader's MMC code, still resident
at `0x80000000`). It is the only way to iterate safely once region 0 no longer contains a
bootable OS. Without A or B, the first bad Linux payload is a one-way door.

**Possible third path:** the signed loader's `0xF7/0xF8` write is gated only by the
MCT-derived allow-list (`88` §5). If the OS/IFS region is inside that allow-list, the
bootrom+loader can re-flash the original IFS with no OS at all. `88`'s unknown **U1**
(NVRAM partition membership) should be extended to "is the OS region writable via
`0xF7/0xF8`?" — that would give a pre-OS restore path and make B unnecessary.

## 6. Loader logs and CFP `-V6 info`

These diagnose the **loader/handoff** phase, not the Linux payload, but they are useful and
mostly already supported:

- **`0xB0` Bugdisp log** — `bbusb.py` already drains it (`bugdisp_log`, 0xB5 pages until
  0xD0). `LDR_01` pages; the Cellebrite `06001A06` build sends a single `0xD0` (`88` §3).
- **`0xB3` with a host string** — appends to NVRAM **`0x2033` `NV_OSSTORE_CFP_LAL_NUM`**
  (capped 0x200), readable later from QNX/CFP. This is a persistent, on-media log the host
  can write before/after a run.
- **`0xAF`** — sets the loader mode flag `*DAT_80118A00 = arg`, consumed by `0xF7`/`0xFD`.
  This is the most likely mechanism behind CFP `-V6` verbose: set the flag, then read the
  bugdisp log. **Needs RE confirmation**, but it is a small experiment: send `0xAF` with a
  verbose value from `bbusb.py`, then drain `0xB0`, and diff the output.
- **CFP `-V6 info`** cannot run under Wine; the path is to RE its verbose flag and log
  reads and add them to `bbusb.py`/`bb10mt`. The NVRAM records `0x2033` (loader action log)
  and `0x2058` (`CfpBugDispLog`) are the persistent counterparts.

What the payload itself cannot do: write the loader's bugdisp log after the jump — the
loader is gone and USB is down. If a persistent payload log is wanted, use NVRAM
`0x2033`/`0x2058` via the bootloader's NVRAM writer (still resident at `0x80000000`), or a
reserved eMMC sector; both are later work.

## 7. Recommended payload design (first version)

A "stage probe" chainloader, not a kernel yet:

1. Entry: disable I/D caches (`SCTLR.C=0,I=0`), set the stack.
2. Publish a boot nonce/magic at `0x90000000` and the mirrors; step `1`.
3. LED: blink 1, write UART3 `PB:1`, step `2`.
4. DTB sanity (magic `0xedfe0dd0`, totalsize); step `3`; LED blink 2.
5. Optional: MMU on/off with the flat table (reuse the kexec IRAM table); step `4`.
6. Kernel entry: branch to `Image`/`zImage` with `r0=0, r1=~0, r2=DTB`; step `5`; LED blink 3
   then off (kernel owns the LED state).
7. On any failure: step `0xE0|stage`, LED error pattern, hold both cores → WDT2 reset.

Readback: LED immediately; rings after the next QNX boot (kexec route) or after the
self-restore (CRC route).

## 8. Test order

1. **LED blink + ring write, no jump** (replace the OS payload temporarily only if the
   restore path is ready; otherwise test via kexec first). Confirms GPIO1 EN and the survivor
   band.
2. **Ring survival across a bootloader re-run** — a new unknown: the kexec survivor band was
   tested across a QNX reboot, not across the hd0 stage re-reading the image. Write a nonce
   and see whether it survives the next boot.
3. **UART3/ring3 capture** — write `PB-UART`, warm-reset, read `0x94000080`/`0x94000100`.
4. **Kernel DEBUG_LL rings** via kexec T3 — already the project's next milestone.
5. Only then the CRC-booted kernel payload, with either kexec-proven code or the
   self-restore wrapper.

## 9. Bottom line

- **LED on/off** is the reliable live marker; **RAM rings at 0x90000000/mirrors** are the
  precise post-mortem; **UART3** is a bonus if the monitor captures it; **kernel DEBUG_LL**
  extends the same rings into Linux.
- The loader/CFP logs are real but diagnose the loader, not Linux; `0xAF` + `0xB0` is the
  concrete CFP `-V6` lead to integrate into `bbusb.py`.
- The hard problem is not logging, it is **reading the log back**. Solve that with
  **kexec-first bring-up** or a **self-restoring CRC payload**; otherwise the first bad
  payload has no way home.
