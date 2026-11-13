# 210 — The reference audio chain, RE'd: the board pin database and the codec-GPIO init (session 44)

Date: 2026-11-12 (session 44).  Static RE of the 1917 QNX userland, no bench
writes.  Files (gitignored): `/base/lib/dll/{deva-ctrl-omap4abe-wm8994.so,
hwio-omap4.so, hwio-config-winchester.so}` and
`/proc/boot/startup-playbook` from `local/work/1917/`.  Tool: Ghidra 12.1.4 via
REA, reconciled with `ntoarmv7-readelf/objdump`; raw data parsed directly with
Python.

## 0. Why this report exists

The session-44 power-domain/CM comparison was negative, and the operator's
direction is to stop treating the software surface as exhausted and **reverse
the reference's own chain** (bootloader → IFS → drivers) instead.  Two concrete
artifacts came out of that: the board's complete pad database (§1) and the audio
driver's codec-init code (§3), including three GPIOs the Linux driver never
touches.

## 1. The board pin database is `hwio-config-winchester.so`

`/base/lib/dll/hwio-config-winchester.so` (13 968 B, sha256 `f7e7c66e…`) is not
a code module worth much; it is the **board's pad/GPIO database**.  Its
`.rodata` holds a **249-record, 20-byte pad table** starting at file `0x604`.

Layout (from `pad_get_id` and `pad_io_setup` in `hwio-omap4.so`):

```
+0x00  pad id (0..0xE6)          +0x0c  hwio signal id (0 = no board signal)
+0x04  alternate function        +0x10  extra (pull/input/off-mode attrs)
+0x06  rev_min   +0x07  rev_max
```

`pad_get_id(signal)` walks this table for `+0x0c == signal` and a revision window
containing the running silicon revision, and returns the pad id.

The pad-id → register resolver is in **`hwio-omap4.so`** (30 632 B, sha256
`41c42754…`), three `u32[0xE7]` tables at file offsets `0x4FF8` (bank),
`0x5394` (offset), `0x5730` (half), and `pad_io_setup` computes:

```
addr = 0x4A100000 + offset[pad] + (bank[pad] ? 0x21E000 : 0)
```

with `half` selecting bits 15:0 or 31:16 of the 32-bit PADCONF word.  Validated
against the known pads: `0xB7 → 0x4A10019C` low (`fref_clk2_out`),
`0x94/0x95 → 0x4A100148` low/high (`sdmmc5_clk/cmd`).

**The complete table is committed** as
`docs/reference/qnx-hwio/pad-table-winchester.csv`, with the resolver recipe in
its README.  This is the authoritative board pin map; every future pad question
should start here rather than from inference.

## 2. `startup-playbook` and this table agree

The same 20-byte records (for the board signals) appear in
`startup-playbook`'s image (`file 0x21828`, `0x2183c`, `0x21850`, …), so the
start-up binary and the hwio config module share one source database.  The
signal-group table report 189 decoded at `0x80122638` is a different, smaller
table (the WLAN/SDIO group).

## 3. The audio driver's codec init — `codec_mixer` / inlined `codec_gpio_init`

`deva-ctrl-omap4abe-wm8994.so` (190 844 B, sha256 `2b346de3…`), function
`codec_mixer` at file `0x2e6e0` (the inlined `codec_gpio_init` is its first
half).  Sequence:

1. `open("/dev/i2c%d")`, `devctl(…, 0x80040502, 400000)` — 400 kHz.
2. `gpio_init()` + `pad_init()` (hwio).
3. **signal `0x92` ("ldo")** — `pad_get_id`/`pad_set_af(3)`/`pad_set_dir(1)`,
   then `gpio_set_dir(out)`, `gpio_set_output(0)`, **`delay(100)`**,
   `gpio_set_output(1)` — a reset/power pulse.
4. **signal `0x93` ("spk")** — `pad_set_af(3)`, output, **high**.
5. **signal `0x94` ("hp")** — `pad_set_af(3)`, output, direction **input**.
   Logs `Got GPIOs, ldo=%x, spk=%x, hp=%x`.
6. `powman_create("audio_boost", …)` and a second powerman handle; the "audio
   boost" is requested off here and turned on by the powerman at stream start.
7. `LAB_0002ef54`: `mmap_device_memory(…, 0x4A30A314, …)` — **maps SCRM
   AUXCLK1**; then `StereoAudioCodecInit(__ptr)` → `MixerInitAvp()`, and
   `FUN_0002decc` becomes the mixer reset function.
8. A Winchester2-only branch additionally drives gpio 159 (signal `0x9f`) low;
   not this board.

`codec_set_clock_rate` (file `0x1b5f8`) writes WM8994 `R0x200=0`, `R0x210`
(rate code: 8k→0x09, 16k→0x36, 32k→0x64, 44.1k→0x73, 48k→0x83), `R0x200=1`,
and only if `mcbsp_port_status(...) & 1`.

### Resolving the three GPIOs

| signal | pad id | register (half) | ball | OMAP gpio | meaning |
|---|---|---|---|---|---|
| `0x92` | `0x82` | `0x4A100120` low | `hdq_sio` | **127** (GPIO4_31) | `ldo` |
| `0x93` | `0x2C` | `0x4A100068` low | `gpmc_a20` | **44** (GPIO2_12) | `spk` |
| `0x94` | `0x31` | `0x4A100070` high | `gpmc_a25` | **49** (GPIO2_17) | `hp` |

They do **not** collide with the display's `gpmc_a21`/`gpmc_a24`
(gpio_45/gpio_48) — different halves of adjacent registers.

## 4. What the Linux side does with them

Our board DTS already muxes all three (`0x120` low = `0x0003`, `0x068` low =
`0x0003`, `0x072` = `0x4103`), and the pads read that way live.  But **nothing
drives them**: gpio_127 is left an input, gpio_44 is not raised, and the
machine driver never performs the reference's pulse.  That is a real chain
difference (and the only currently known one in the audio path that is not a
register value).

**Bench caveat (session 44).**  A sysfs replay (export 639 = "gpio_127", pulse
low/200 ms/high, plus gpio_44 high and gpio_49 input) followed by a codec reset
and the freeze detector did **not** restore MCLK1 (`R0x220` still dropped).  The
operator's standing rule is that ad-hoc raw I²C/devmem/sysfs pokes are not a
trustworthy test vehicle: implement the sequence in the **driver**, rebuild and
flash.  That is the correct next test.

**Correction (same session): that replay drove the wrong pins altogether.**
Linux's `/sys/class/gpio` numbers lines by probe order, not by OMAP bank, so
639 is GPIO5_31, not `gpio_127` (= 607).  The implementation and the corrected
numbering are in report 211 (traps 185).  The replay is therefore invalid on
two independent counts.

## 5. Open RE items (the chain, not exhausted)

* **Where the driver writes AUXCLK1.**  `codec_mixer` maps `0x4A30A314`; the
  write itself is inside `MixerInitAvp`/`FUN_0002a33c`/`FUN_000296c4` and has
  not yet been located.  Its **value and ordering** relative to the codec reset
  are worth pinning (report 208 says the register ends at `0x00020100`).
* **The `audio_boost` powerman mode list** (`DAT_0003c3c0`, `mode_list`) — what
  the boost does at stream start/stop.
* **`FUN_000296c4` twice** (`audio_profile`, `0x3b488`) — the codec profile
  tables; they may set board-level things beyond the register file.
* **`MixerInitAvp`** (file `0x1632c`) — the full AVP mixer init.
* **`startup-playbook`** — the remaining MMIO writes (report 189's mmio-map) and
  whether any touch the audio clock path at boot rather than at driver load.
* **The bootloader → IFS hand-off** — the pre-OS state the two OSes share
  (already established: the same RIM loader runs both).

## 6. A function-level QNX "translation layer" is feasible

The operator's idea — run the QNX binaries on Linux — is tractable **at
function level**, not as a full OS emulation:

* the driver is ARMv7 ELF and its MMIO is `mmap_device_memory`-based;
* the interesting routines (`codec_gpio_init`, `codec_set_clock_rate`,
  `StereoAudioCodecInit`) use a small import set (`open/devctl`, `gpio_*`,
  `pad_*`, `mmap_device_memory`, `delay`, `calloc`);
* a Unicorn harness with stubs that log and return success, a fake hwio
  database and a fake syspage could execute those routines and **log every
  MMIO/IPC access**.  That would give the exact sequence, including paths we
  have not statically read, without a QNX userland.

This is proposed as a follow-up tool (`tools/qnxemu.py`), to be built only if
static RE leaves a gap.
