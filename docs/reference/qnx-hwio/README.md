# QNX hwio board database — winchester

`pad-table-winchester.csv` is the **complete pad configuration table** extracted
from the reference OS's `hwio-config-winchester.so`
(`/base/lib/dll/`, sha256 `f7e7c66e…` — the file is gitignored; the table is ours).

It is the board's own pin database: 249 records, 20 bytes each, in `.rodata`
starting at file offset `0x604`.  Record layout (confirmed by decompiling
`pad_get_id`/`pad_io_setup` in `hwio-omap4.so`):

| offset | field |
|---|---|
| `+0x00` | pad id (0…0xE6) — the index into the resolver tables |
| `+0x04` | alternate function (`af`, 0…7) |
| `+0x06` | `rev_min` silicon revision gate |
| `+0x07` | `rev_max` |
| `+0x0c` | hwio **signal id** (0 = no board signal; the row only sets the pad safely) |
| `+0x10` | `extra` (pull / input / off-mode attributes, hwio encoding) |

The pad id → register resolver is in `hwio-omap4.so`, three `u32[0xE7]` tables
at file offsets **`0x4FF8` (bank), `0x5394` (offset), `0x5730` (half)**:

```
addr = 0x4A100000 + offset[pad] + (bank[pad] ? 0x21E000 : 0)
half = 0   -> bits 15:0 of the 32-bit PADCONF word
half != 0  -> bits 31:16
```

Validated against the known pads (`0xB7` → `0x4A10019C` low = `fref_clk2_out`;
`0x94`/`0x95` → `0x4A100148` low/high = `sdmmc5_clk`/`sdmmc5_cmd`).

## Audio-relevant rows

| idx | pad | af | signal | extra | register | meaning |
|---|---|---|---|---|---|---|
| 29 | `0xB7` | 0 | `0x064` | `0` | `0x4A10019C` lo | `fref_clk2_out` = the codec MCLK1 |
| 155 | `0xE6` | 0 | `0x08A` | `0x1008` | `0x4A10019C` hi | `sys_nirq1` |
| 51 | `0x82` | 3 | `0x092` | `0` | `0x4A100120` lo | `hdq_sio` = gpio_127, driver calls it **ldo** |
| 52 | `0x2C` | 3 | `0x093` | `0` | `0x4A100068` lo | `gpmc_a20` = gpio_44, driver calls it **spk** |
| 53 | `0x31` | 3 | `0x094` | `0x1008` | `0x4A100070` hi | `gpmc_a25` = gpio_49, driver calls it **hp** |

Note the audio GPIOs are `gpmc_a20`/`gpmc_a25`, **not** the display's
`gpmc_a21`/`gpmc_a24` (gpio_45/gpio_48) that the board DTS uses for the panel
enable group — so the two functions do not collide.

## `gpio-table-winchester.csv` — the board GPIO database

`gpio_config` (`hwio-config-winchester.so` file offset `0x1964`, `gpio_config_size`
= **72**, symbol size 2304 = 72 × 32 B) decoded by report 216 §5.2.  Record
layout (verified against `gpio_get_id` @`0x1798` and `gpio_set_output` @`0x1be0`
in `libhwio-omap4.so.1`):

| offset | field |
|---|---|
| `+0x00` | `u8` gpio id (`bank*32 + line`) |
| `+0x04` | `u32` direction (1 = output, 0 = input) |
| `+0x08` | `u32` initial level |
| `+0x0c` | `u32` edge detect (2 = falling) |
| `+0x10` | `u8` attr (always 0 here) |
| `+0x11` | `u8` pull/enable |
| `+0x12`/`+0x13` | debounce enable / debounce time |
| `+0x14`/`+0x15` | `u8` rev_min / rev_max |
| `+0x16` | `u8` phase |
| `+0x18` | `u32` **pad signal id** (`gpio_get_id()` matches on this) |

The CSV adds the pad row resolved from that signal id (`pad_id`, `af`,
`pad_extra`, `pad_bank`, `pad_offset`, `pad_half`, `pad_addr`).  hwio carries
**no human-readable signal or ball names** — names in the `note` column come
from the TRM/DTS/report 189 and are marked as such.

## Corrections (report 216)

* The pad resolver arrays are **not** at `0x4FF8`/`0x5394`/`0x5730` (that is
  0x198 too low and lands in the error strings).  They are consecutive
  `u32[231]` arrays at **`0x5190` (bank), `0x552C` (offset), `0x58C8` (half)**.
  With those offsets every row of `pad-table-winchester.csv` recomputes
  exactly.
* `pad_config_size` (data at `0x2264`) is **248** (4960 B), so the table is
  `0x604`…`0x1964`.  Row **idx 248** in `pad-table-winchester.csv`
  (pad `0xAD`) is therefore **not** a pad record — it is the first 20 bytes of
  `gpio_config[0]`.  Ignore that row.

## Caveats

* The table is the **static** database; `pad_get_id(signal)` picks the row whose
  `signal` matches and whose revision window contains the running silicon
  revision.  Duplicate signal ids across revisions exist (e.g. `0xA9`…).
* `extra` is hwio's own encoding (not a raw PADCONF value); its exact bit
  meanings are still open (report 189 §2.1).  The final PADCONF value is
  `(extra-derived bits) | af`.
* The vendor `.so` is under the gitignored `local/`; only this extraction is
  committed.  The extraction recipe is in report 210 §2.
