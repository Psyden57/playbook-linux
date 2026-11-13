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

## Caveats

* The table is the **static** database; `pad_get_id(signal)` picks the row whose
  `signal` matches and whose revision window contains the running silicon
  revision.  Duplicate signal ids across revisions exist (e.g. `0xA9`…).
* `extra` is hwio's own encoding (not a raw PADCONF value); its exact bit
  meanings are still open (report 189 §2.1).  The final PADCONF value is
  `(extra-derived bits) | af`.
* The vendor `.so` is under the gitignored `local/`; only this extraction is
  committed.  The extraction recipe is in report 210 §2.
