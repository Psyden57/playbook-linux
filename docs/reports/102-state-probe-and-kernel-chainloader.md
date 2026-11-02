# 102 — CRC state-probe + kernel chainloader (2026-10-26)

Status: live on the spare. State probe **succeeded**; kernel chainloader **built, flashed,
but not yet diagnosed** (recovery restored TabletOS).

## 1. State probe

Image `chainloader/probe.img` (Variant A: base `ifs0.sealed.bin`, 428-byte chainloader at
`0x800`, CRC `0xDB0ECED3`, `os_type=6`). Flashed via the pre-OS `0xF7` path; the loader ran
it; it parked and the WDT2 reset; the recovery tool restored TabletOS; the breadcrumbs were
read with `memdump3`.

All four pages (`0x90000000`, `0x94000000`, `0x88000000`, `0x9FE00000`) were identical and
survived the watchdog reset + bootrom + loader upload + recovery flash + TabletOS boot:

```
[0x00] 4C424B43  "KBLB"          [0x04] 000000DE  parked
[0x08] 50424F42  "PBob"          [0x0C] A2AAAABA  (unused)
[0x10] 00C50878  SCTLR: MMU/C/I off
[0x14] 60000193  CPSR: SVC, I=1, F=1, T=0
[0x18] 80100100  VBAR (our park loop)
[0x1C] 80054059  TTBR0 (loader value)      [0x20] 00000000 TTBCR
[0x24] 55555555  DACR (all manager)        [0x28] 800F0000 SP
[0x2C] 800FF800  loader jump-target global
[0x30] 00000000  descriptor global (not at 0x8002F144)
[0x34] 800FF800  LoadStart   [0x38] 803BF610 LoadEnd   [0x3C] 800FF800 LoadBase
[0x40] 800FF82C  MetricsPtr
[0x44..0x4C] 0x65/0/0 — probe read hdr+0x7C/0x9C/0xD4; the real fields are at
             desc+0x7C/0x9C/0xD4 = hdr+0xA8/0xC8/0x100 (fix in the next probe).
```

LED blinked 1 (entered), 2 (state/header), then off; UART3 marker emitted (invisible
without pads). The boot confirmed: header word 0 branches to `0x80100000`, ARM/SVC,
MMU/caches off, and the breadcrumb channel survives the recovery cycle.

## 2. Kernel chainloader

`chainloader/kernel.img` (Variant B):

| item | value |
|---|---|
| image size / LoadEnd | `0x7D0864` / `0x808D0064` |
| CRC | `0xA6FF2D25` (corrected image) |
| chainloader | 428 B at `0x80100000` |
| zImage | 5,223,233 B at image `0x2C0000` (load `0x803BF800`) |
| DTB | 87,321 B at image `0x7BB348` (load `0x808BAB48`) |
| chainloader copies | zImage → `0x84000000`; DTB → `0x844FB348` |
| jump | `r0=0, r1=~0, r2=0x844FB348`, `pc=0x84000000` |
| zImage header | 8 NOPs, `b` at `+0x20`, magic `0x016F2818`; entry at base is correct |

Host-side structural + gate + CRC checks all PASS. The ECDSA trailer-shape check FAILs at
the new end; that is informational on the factory path (the bootloader does not use it).

Flash: 509 `0xF7` frames accepted; `0x40F9` → `0x4006`; `0x40C0` → `0x4015`; loader reset.
The device showed a ~5–10 s USB connect/disconnect cadence; the recovery tool caught a
bootrom and reflashed the original TabletOS at 03:30:53.

**Diagnosis (post-recovery ring read):** all four pages held
`magic=KBLB, stage=0xE1, nonce="KERN"` — the chainloader's **DTB-magic failure** path, not a
kernel entry. The check compared the little-endian word against `0xD00DFEED`; the in-memory
LE word is **`0xEDFE0DD0`**. Fixed in `chainloader_kernel.S.in`; the rebuilt image has CRC
`0xA6FF2D25`. The corrected kernel image has **not** been flashed yet.

## 3. Next

1. Recover SSH (reboot/power-cycle), verify the media is the original sealed image and the
   counter; read the rings with `on -C 0 /accounts/devuser/memdump3 90000000 0x500`
   (also `0x94000000`, `0x88000000`, `0x9FE00000`) and parse `+0x80` count / `+0x100` chars.
2. Correlate the USB timeline (`journalctl -k` / `usb_watch.py`) with the chainloader LED
   stages and the rings to see how far the kernel got.
3. If the rings are empty, add a chainloader eMMC log or USB, or fall back to a smaller
   kernel/initramfs and the kexec project's T3-proven kernel.
