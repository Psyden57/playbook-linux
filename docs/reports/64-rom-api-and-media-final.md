# 64 — ROM API labels, media descriptor, command-word flags, secure-path status

Status: closes the three 2026-10-19 continuation items in
`HANDOFF.md` as far as the available evidence permits.
Date: 2026-10-19 (continuation session).
Target: `omap4430-bootrom-0x40028000.bin` (49152 B, base `0x40028000`,
SHA-256 `fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`).

Provenance split:

- **REA Evidence (Ghidra 12.1.4)** for pseudocode, assembly and raw reads.
  Evidence IDs are cited in each section.
- **Public-document context** for the TRM (`swpu231ap.pdf`) and the external
  ROM-API headers. These are not evidence about the dump.
- **LLVM 23-derived** only for the cmd-8 stub at `0x4002BC10`, which Ghidra did
  not promote to a function. Marked inline.

---

## Part 1 — The 48-word ROM API table at `0x40028400`

### 1.1 External list found

A public API list does exist in OMAP4 bootloader sources. Four independent
copies agree on the base and offsets:

| source | base / offsets |
|---|---|
| `simu/usbboot-omap4` `include/omap4/omap4_rom.h` | `PUBLIC_API_BASE_4430 0x28400`; `PUBLIC_GET_DRIVER_MEM_OFFSET 0x04`, `GET_DRIVER_PER 0x08`, `GET_DEVICE_MEM 0x80`, `GET_DEVICE_PER 0x84` |
| `swetland/omap4boot` `include/omap4/omap4_rom.h` + `aboot.c` | same four offsets; `rom_get_mem_driver(&io, device)` / `rom_get_per_device(&boot)` usage |
| `bigunclemax/sync3flash` `target/include/common/omap_rom.h` | adds `PUBLIC_IRQ_INITIALIZE 0x40`, WDT `0x50/0x54/0x58/0x5C`, I2C `0x60/0x64/0x68/0x6C`, `GET_DEVICE_DATA 0x88`, CM `0xA0/0xA4`, CTRL `0xA8`; also monitor IDs `0x100-0x108` |
| `minoca/os` `uefi/include/dev/tirom.h` | same four offsets; full `mem_device` / `per_handle` / `mem_driver` / `per_driver` layouts and device IDs `0x40-0x46` |

TI's public position (E2E forum answer on "OMAP4460 ROM API?") is that only the
TRM is released; there is no separate public description of every table entry.
The headers above are the usable external list.

### 1.2 Address rule and confirmation

`API(0x28400 + off) = *(u32 *)(0x28400 + off)`; the boot alias `0x28400` maps to
the dump's `0x40028400`. Each table word is an odd `0x0002xxxx` Thumb pointer;
`target = 0x40000000 | (raw & ~1)`.

The external offsets land exactly on known accessor stubs:

| API offset | table index | raw word | target | external name |
|---|---:|---|---|---|
| `0x04` | 1 | `0x00028E2B` | `0x40028E2A` | `get_driver_mem` |
| `0x08` | 2 | `0x00028E4B` | `0x40028E4A` | `get_driver_per` |
| `0x40` | 16 | `0x00029301` | `0x40029300` | `irq_initialize` |
| `0x50` | 20 | `0x0002A455` | `0x4002A454` | `wdtimer_enable` |
| `0x54` | 21 | `0x0002A431` | `0x4002A430` | `wdtimer_disable` |
| `0x58` | 22 | `0x0002A41D` | `0x4002A41C` | `wdtimer_reset` |
| `0x5C` | 23 | `0x0002A479` | `0x4002A478` | `wdtimer_set_timeout` |
| `0x60` | 24 | `0x0002956D` | `0x4002956C` | `i2c_driver_init` |
| `0x64` | 25 | `0x00029687` | `0x40029686` | `i2c_driver_write` |
| `0x68` | 26 | `0x000295FB` | `0x400295FA` | `i2c_driver_read` |
| `0x6C` | 27 | `0x00029739` | `0x40029738` | `i2c_driver_close` |
| `0x80` | 32 | `0x00028DFD` | `0x40028DFC` | `get_device_mem` |
| `0x84` | 33 | `0x00028E0B` | `0x40028E0A` | `get_device_per` |
| `0x88` | 34 | `0x00028E1B` | `0x40028E1A` | `get_device_data` |
| `0xA0` | 40 | `0x00029AE7` | `0x40029AE6` | `cm_enable_module_clocks` |
| `0xA4` | 41 | `0x00029CA1` | `0x40029CA0` | `cm_disable_module_clocks` |
| `0xA8` | 42 | `0x0002A95B` | `0x4002A95A` | `ctrl_configure_pads` |

The Ghidra pseudocode matches these names:

- `0x40028E2A`: `idx 1..8 -> *out = 0x4002F578 + idx*12` (memory driver getter).
- `0x40028E4A`: `id 0x41..0x46 -> *out = 0x4002F5E4 + (id-0x40)*20`
  (peripheral driver getter).
- `0x40028DFC` / `0x40028E0A` / `0x40028E1A`: write the static media
  (`0x4030D238`), peripheral (`0x4030D210`) and device-data (`0x4030D258`)
  descriptor pointers to the caller's out slot (return 6 on NULL input, else 0).
- `0x4002A95A`: 16-bit mux/pad field writes at `0x4A100000+0x40..0x1C8`
  (`CTRL_CONFIGUREPADS`), not a PRCM module enable.
- `0x40029CA0`: clears CM module bits (`& ~3`) in the same PRCM blocks that
  `0x40029AE6` enables (`CM_DISABLEMODULECLOCKS`).
- `0x4002956C` / `0x40029738`: I2C controller init/close register sequences
  (`+0xB0/+0xB4/+0xB8/+0xA4/+0xA8`, status poll at `+0x90`).

Evidence: `0x40028E72` `ev_661b972c…`; `0x4002A560` `ev_ffedc5a8…`;
`0x40029AE6` pseudocode `ev_4c35cc1f…` and assembly `ev_da442eca…`;
`0x4002A95A` `ev_650581cc…`; `0x40029CA0` `ev_b878a958…`;
`0x4002956C` `ev_b1eef334…`; `0x40029738` `ev_21f3a71a…`.

### 1.3 Full 48-entry table (external name where documented)

Only 17 entries are named by the external headers. The rest carry descriptive
labels from this analysis; they are **not** externally documented.

| # | off | raw | target | external API name | analysis label |
|---:|---:|---|---|---|---|
| 0 | `0x00` | `0x00028F05` | `0x40028F04` | — | cache-op / SMC argument wrapper |
| 1 | `0x04` | `0x00028E2B` | `0x40028E2A` | **`get_driver_mem`** | memory-driver vector getter (stride 12) |
| 2 | `0x08` | `0x00028E4B` | `0x40028E4A` | **`get_driver_per`** | peripheral-driver vector getter (stride 20) |
| 3 | `0x0C` | `0x00029D45` | `0x40029D44` | — | boot/display helper |
| 4 | `0x10` | `0x00028D4F` | `0x40028D4E` | — | media probe / class selector |
| 5 | `0x14` | `0x00028C45` | `0x40028C44` | — | large Thumb body entry |
| 6 | `0x18` | `0x00029CD1` | `0x40029CD0` | — | PRCM/boot helper |
| 7 | `0x1C` | `0x00029EDF` | `0x40029EDE` | — | boot helper |
| 8 | `0x20` | `0x00029795` | `0x40029794` | — | device-type byte read (`[0x4A0022C4] & 0xff`) |
| 9 | `0x24` | `0x0002979D` | `0x4002979C` | — | feature/device-type helper |
| 10 | `0x28` | `0x0002A4C1` | `0x4002A4C0` | — | read `PRM_RSTST` reset reason |
| 11 | `0x2C` | `0x00028613` | `0x40028612` | — | `0x50000000` helper |
| 12 | `0x30` | `0x000284C1` | `0x400284C0` | — | `0x50000000` enable/poll |
| 13 | `0x34` | `0x00028567` | `0x40028566` | — | `0x50000000` helper |
| 14 | `0x38` | `0x000285CB` | `0x400285CA` | — | `0x50000000` helper |
| 15 | `0x3C` | `0x000284D7` | `0x400284D6` | — | `0x50000000` value helper |
| 16 | `0x40` | `0x00029301` | `0x40029300` | **`irq_initialize`** | GIC/CPU init helper |
| 17 | `0x44` | `0x00029343` | `0x40029342` | — | GIC/CPU helper |
| 18 | `0x48` | `0x0002938B` | `0x4002938A` | — | GIC/CPU helper |
| 19 | `0x4C` | `0x000293C3` | `0x400293C2` | — | GIC dispatcher |
| 20 | `0x50` | `0x0002A455` | `0x4002A454` | **`wdtimer_enable`** | WDT start (WSPR `0xBBBB`/`0x4444`) |
| 21 | `0x54` | `0x0002A431` | `0x4002A430` | **`wdtimer_disable`** | WDT stop (WSPR `0xAAAA`/`0x5555`) |
| 22 | `0x58` | `0x0002A41D` | `0x4002A41C` | **`wdtimer_reset`** | WDT sysconfig/reset poll |
| 23 | `0x5C` | `0x0002A479` | `0x4002A478` | **`wdtimer_set_timeout`** | WDT load/timeout |
| 24 | `0x60` | `0x0002956D` | `0x4002956C` | **`i2c_driver_init`** | I2C controller init |
| 25 | `0x64` | `0x00029687` | `0x40029686` | **`i2c_driver_write`** | I2C write |
| 26 | `0x68` | `0x000295FB` | `0x400295FA` | **`i2c_driver_read`** | I2C read |
| 27 | `0x6C` | `0x00029739` | `0x40029738` | **`i2c_driver_close`** | I2C close |
| 28 | `0x70` | `0x0002A2CB` | `0x4002A2CA` | — | EMIF/DDR descriptor init (see Part 2) |
| 29 | `0x74` | `0x00029F9D` | `0x40029F9C` | — | EMIF indirect index write |
| 30 | `0x78` | `0x00029F7D` | `0x40029F7C` | — | EMIF indirect data write |
| 31 | `0x7C` | `0x00029FD7` | `0x40029FD6` | — | EMIF indirect data read |
| 32 | `0x80` | `0x00028DFD` | `0x40028DFC` | **`get_device_mem`** | media device descriptor getter |
| 33 | `0x84` | `0x00028E0B` | `0x40028E0A` | **`get_device_per`** | peripheral device descriptor getter |
| 34 | `0x88` | `0x00028E1B` | `0x40028E1A` | **`get_device_data`** | device-data getter |
| 35 | `0x8C` | `0x0002A561` | `0x4002A560` | — | 16-byte-record base lookup |
| 36 | `0x90` | `0x0002925D` | `0x4002925C` | — | helper |
| 37 | `0x94` | `0x00029051` | `0x40029050` | — | helper |
| 38 | `0x98` | `0x00028FB5` | `0x40028FB4` | — | image-load helper |
| 39 | `0x9C` | `0x000297B7` | `0x400297B6` | — | feature read helper |
| 40 | `0xA0` | `0x00029AE7` | `0x40029AE6` | **`cm_enable_module_clocks`** | PRCM clock-enable `tbb` dispatcher |
| 41 | `0xA4` | `0x00029CA1` | `0x40029CA0` | **`cm_disable_module_clocks`** | PRCM clock-disable dispatcher |
| 42 | `0xA8` | `0x0002A95B` | `0x4002A95A` | **`ctrl_configure_pads`** | CTRL pad/mux configuration |
| 43 | `0xAC` | `0x000297C7` | `0x400297C6` | — | feature/device-type read |
| 44 | `0xB0` | `0x00028605` | `0x40028604` | — | `0x50000000` helper |
| 45 | `0xB4` | `0x00028621` | `0x40028620` | — | `0x50000000` helper |
| 46 | `0xB8` | `0x0002869D` | `0x4002869C` | — | snapshot `0x50000240` block |
| 47 | `0xBC` | `0x000286D5` | `0x400286D4` | — | `0x50000000` control word |

### 1.4 Corrections forced by the external list

| earlier statement | correction |
|---|---|
| `0x4002A430` = WDT start (`0xAAAA`/`0x5555`), `0x4002A454` = stop (`0xBBBB`/`0x4444`) (`20` §1.1, `50` §2a) | **Reversed.** API offset `0x50` = enable → `0x4002A454` (`0xBBBB`/`0x4444`); offset `0x54` = disable → `0x4002A430` (`0xAAAA`/`0x5555`). The standard OMAP WSPR start pair is `0xBBBB`/`0x4444`. |
| `0x4002A95A` = PRCM module enable (`50` §2a) | **CTRL_CONFIGUREPADS**: 16-bit mux/pad field writes at `0x4A100000+0x40..0x1C8`. |
| `0x40029AE6` = generic PRCM dispatcher | `CM_ENABLEMODULECLOCKS`; `0x40029CA0` is the matching disable. |
| `0x4002956C…0x40029738` = device/boot helpers | I2C init/write/read/close. |
| `0x40029300` = GIC/CPU helper | `IRQ_INITIALIZE`. |
| `0x40028DFC/E0A/E1A` = generic helpers | `get_device_mem` / `get_device_per` / `get_device_data`. |
| image-handler `'A'..'F'` = image-op families; `0x45` = "`'E'` family" | Device IDs: `0x41..0x46` = **UART1, UART2, UART3, UART4, USB, USBEXT** (`0x40` = NULL). `'G'` = `0x47` is correctly rejected by `0x40028E4A`. |

### 1.5 Struct/ABI confirmation

The external headers validate the record layouts already inferred in
`10-boot-media.md` §4 and `20-config-parser.md` §3:

- `struct mem_device`: `initialized +0x00`, `device_type +0x04`,
  `trials_count +0x05`, `xip_device +0x08`, `search_size +0x0C`,
  `base_address +0x10`, `hs_toc_mask +0x14`, `gp_toc_mask +0x16`,
  `device_data +0x18`, `boot_options +0x1C`.
- `struct mem_driver`: 3 methods `init/read/configure` → the stride-12 media
  vectors at `0x4002F578`.
- `struct per_handle`: `set_to_null +0x00`, `callback +0x04`, `data +0x08`,
  `length +0x0C`, `options +0x10`, `xfer_mode +0x14`, `device_type +0x18`,
  `status +0x1C`, `hs_toc_mask +0x20`, `gp_toc_mask +0x22`,
  `config_timeout +0x24`.
- `struct per_driver`: 5 methods `init/read/write/close/config` → the stride-20
  peripheral vectors at `0x4002F5E4`.

---

## Part 2 — Media details

### 2.1 `0x4002F9F8` is an EMIF/DDR descriptor, not a media blob

`0x4002A2CA(desc, out)` pseudocode (`ev_a15c53b0…`) does:

1. enable PRCM module 9 (`0x40029AE6(9,0)`, `(9,1)`);
2. write `desc[+0x48]` to **`0x4E000040` = `DMM_LISA_MAP_0`**;
3. toggle bit 0 of `[0x4A004110]` (`0x4002A2BC(1)`);
4. use `desc[+0x4C]` as a channel mask:
   - low 2 bits select EMIF1 (`0`) subchannel 0/1/both;
   - bits `[3:2]` do the same for EMIF2 record 1;
5. `0x4002A212(desc, ch)` programs EMIF `SDRAM_CONFIG`, `REF_CTRL`,
   `TIM_1..3`, `PWR_MGMT_CTRL`;
6. `0x4002A1BA(desc, ch)` programs `DDR_PHY_CTRL_1/2`;
7. `0x4002A0E6(desc, ch, sub, ...)` runs the EMIF `+0x50`/`+0x40` indirect
   register protocol and returns 1 or 2 in `*out` depending on `+0x40` bit 1.

The EMIF1/EMIF2 bases are `0x4C000000`/`0x4D000000`; DMM is `0x4E000000`
(TRM §15, Table 16-19).

**Descriptor layout** (`0x4002F9F8`, 0x50 bytes used):

| offset | meaning |
|---:|---|
| `+0x00..+0x23` | EMIF record 0 (EMIF1), 0x24 bytes |
| `+0x24..+0x47` | EMIF record 1 (EMIF2), 0x24 bytes |
| `+0x48` | `DMM_LISA_MAP_0` value (written to `0x4E000040`) |
| `+0x4C` | channel/subchannel mask |

Each 0x24-byte record maps directly to EMIF registers:

| record field | EMIF register(s) | EMIF offset | record 0 value |
|---:|---|---:|---:|
| `+0x00` | `EMIF_SDRAM_CONFIG` | `0x08` | `0x80800C00` |
| `+0x04` | `EMIF_SDRAM_REF_CTRL` / `_SHDW` | `0x10` / `0x14` | `0x000000C3` |
| `+0x08` | `EMIF_SDRAM_TIM_1` / `_SHDW` | `0x18` / `0x1C` | `0x110D1624` |
| `+0x0C` | `EMIF_SDRAM_TIM_2` / `_SHDW` | `0x20` / `0x24` | `0x3058161B` |
| `+0x10` | `EMIF_SDRAM_TIM_3` / `_SHDW` | `0x28` / `0x2C` | `0x000060B2` |
| `+0x14` | `EMIF_PWR_MGMT_CTRL` / `_SHDW` | `0x38` / `0x3C` | `0x00000000` |
| `+0x18` | `EMIF_DDR_PHY_CTRL_1` / `_SHDW` | `0xE4` / `0xE8` | `0x901FF416` |
| `+0x1C` | `EMIF_DDR_PHY_CTRL_2` | `0xEC` | `0x00000000` |
| `+0x20` | value used by the `+0x50`/`+0x40` indirect PHY/DLL sequence (indices 1,2,3) | `0x50` / `0x40` | `0x00020123` |

Record 1 (`+0x24..+0x47`) is entirely zero. The `2` at descriptor `+0x4C` is
the channel mask, not a record field.

Observed header values: `desc[+0x48] = 0`; `desc[+0x4C] = 2` (EMIF1,
subchannel 1 only). So this dump's descriptor initializes **EMIF1 only** and
leaves the EMIF2 record unused.

Evidence: `0x4002A2CA` `ev_a15c53b0…`; `0x4002A0E6` `ev_4b04a815…`;
`0x4002A1BA` `ev_ea568aa2…`; `0x4002A212` `ev_f390dc1a…`;
`0x4002A2BC` `ev_26fe93bf…`; `0x40029F7C` `ev_ea3cd6c8…`;
`0x40029F9C` `ev_e58bfa0e…`; `0x40029FD6` `ev_afbff7a6…`.

#### Consumers

1. **cmd-8 open `0x4002BC10` (Thumb, LLVM-derived).** It zeroes a 4-word
   request, loads `0x0002F9F8` from the literal at `0x4002BC78`, calls
   `0x4002A2CA`, and accepts only `return == 0 && sp[1] == 2`. On success it
   sets `outer[+0x08]=1`, `outer[+0x05]=1`, `outer[+0x0C]=1`,
   `outer[+0x10]=0xC0000000`, `outer[+0x14]=3`, `outer[+0x18]=0`,
   `outer[+0x00]=1`.
2. **`0x4002C6C0`.** If a record at `param_5` has first word `0xC0C0C0C2`
   (`DAT_4002C804`) and a nonzero byte at `+4`, it calls
   `0x4002A2CA(param_5 + 8, 0)`. So the same EMIF descriptor can be supplied
   inside a `0xC0C0C0C2` config header.

`0xC0000000` is documented as the **DDR-SDRAM CS1 address space** (1 GB,
EMIF1-CS1 / EMIF2-CS1; TRM memory map). The cmd-8 read routine `0x4002BC58`
then copies from `outer[+0x10] + (off<<9)` for `len<<9` bytes. **Command 8 is
therefore the SDRAM/memory-boot path**, not an opaque "mmap source".

Immediately after the descriptor, `0x4002FA48..0x4002FA83` is a **separate**
peripheral table, not part of the 0x50-byte EMIF object:
`0x4806A000`, `0x4806C000`, `0x48020000`, `0x4806E000` (UART1-4 bases);
`0x48`, `0x49`, `0x4A`, `0x46` (IRQs 72, 73, 74, 70); then
`0`, `3`, `1`, `0`, `0x00003C01`, `0x000D001A`, `4`.

### 2.2 `0x4002F7C4` gating — exact

`0x40028E72(flags, dst, src)` pseudocode (`ev_661b972c…`):

```c
zero(dst, 8);
if (flags & 0x200) {                  /* bit 9: stored config block */
    for (i = 0; i < 4; i++) dst[i] = src[2 + 2*i];
} else {
    devtype = *(u8 *)0x4A0022C4;      /* 0x40029794 */
    rec = 0x4002F7C4 + (devtype & 0x1F) * 16;
    base = (devtype & 0x20) ? rec : rec + 8;   /* bit 5 selects half */
    for (i = 0; i < 4; i++) {
        v = *(u16 *)(base + 2*i);
        if ((v & 0x8000) || (flags & 1)) {
            dst[n++] = v & 0xFF;      /* bit15 masked off */
            if ((v & 0x7FFF) >> 8) dst[n++] = (v >> 8) & 0xFF;
        }
    }
}
```

So the gating is:

- **flags bit 9 (`0x200`)**: use four bytes from the stored config block
  (`src+2`, `+4`, `+6`, `+8`), bypassing the table.
- **otherwise**: index = low 5 bits of `[0x4A0022C4]`; bit 5 of that byte selects
  the lower (`+0..+6`) or upper (`+8..+14`) half of the 16-byte record.
- **bit 15 of each u16** = "include unconditionally".
- **flags bit 0 (`0x1`)** = include entries whose bit 15 is clear.

The table at `0x4002F7C4` has six populated 16-byte records (`0x60` bytes):

| idx | lower half (u16 ×4) | upper half (u16 ×4) |
|---:|---|---|
| 0 | `0x8006, 0x0045, 0, 0` | `0x0045, 0x8006, 0, 0` |
| 1 | `0x8001, 0x0045, 0, 0` | `0x0045, 0x8001, 0, 0` |
| 2 | `0x8002, 0x0045, 0, 0` | `0x0045, 0x8002, 0, 0` |
| 3 | `0x8003, 0x0045, 0, 0` | `0x0045, 0x8003, 0, 0` |
| 4 | `0x8008, 0x0045, 0, 0` | `0x0045, 0x8008, 0, 0` |
| 5 | `0x8005, 0x0045, 0, 0` | `0x0045, 0x8005, 0, 0` |

Emitted command bytes are therefore `[media, 0x45]` or `[0x45, media]`, where
`media ∈ {1,2,3,5,6,8}` and `0x45` is **DEVICE_USB** (external header). The
`0x8000` flag makes the media command unconditional; `0x45` is emitted only when
flags bit 0 is set. Indices 6..31 are outside the six records (the caller masks
with `0x1F`, but only 0..5 are populated).

### 2.3 MMCHS command-word flag bits — complete

`0x4002DDD4` writes its `r1` argument **verbatim** to `MMCHS_CMD` (`0x20C`)
after writing `r2` to `MMCHS_ARG` (`0x208`). `0x4002DE34` extracts
`ubfx r1, r1, #0x10, #2` and only reads `RSP32/54/76` when that value is 1.

TRM Table 24-84 (`MMCHS_CMD`) gives the fields; all observed words follow them:

| bits | field | meaning |
|---:|---|---|
| `31:30` | reserved | 0 |
| `29:24` | `INDX` | command index (0..63) |
| `23:22` | `CMD_TYPE` | 0 normal, 1/2 suspend/resume, 3 abort |
| `21` | `DP` | data present |
| `20` | `CICE` | command index check enable |
| `19` | `CCCE` | command CRC check enable |
| `18` | reserved | 0 |
| `17:16` | `RSP_TYPE` | `00` none, `01` 136-bit (R2), `10` 48-bit (R1/R3/R5/R6/R7), `11` 48-bit + busy (R1b/R5b) |
| `15:6` | reserved | 0 |
| `5` | `MSBS` | multi/single block |
| `4` | `DDIR` | 0 write, 1 read |
| `3` | reserved | 0 |
| `2` | `ACEN` | auto-CMD12 enable |
| `1` | `BCE` | block-count enable |
| `0` | `DE` | **DMA Enable**, not "data enable" |

Observed words:

| word | value | INDX | CMD_TYPE | DP | CICE | CCCE | RSP_TYPE | MSBS | DDIR | ACEN | BCE | DE |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| CMD0 | `0x00000000` | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| CMD1 | `0x01020000` | 1 | 0 | 0 | 0 | 0 | 2 | 0 | 0 | 0 | 0 | 0 |
| CMD2 | `0x02090000` | 2 | 0 | 0 | 0 | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| CMD3 | `0x031A0000` | 3 | 0 | 0 | 1 | 1 | 2 | 0 | 0 | 0 | 0 | 0 |
| CMD4 | `0x04040000` | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| CMD6 (SD) | `0x061A0000` | 6 | 0 | 0 | 1 | 1 | 2 | 0 | 0 | 0 | 0 | 0 |
| CMD6 (MMC) | `0x061B0000` | 6 | 0 | 0 | 1 | 1 | 3 | 0 | 0 | 0 | 0 | 0 |
| CMD7 | `0x071B0000` | 7 | 0 | 0 | 1 | 1 | 3 | 0 | 0 | 0 | 0 | 0 |
| CMD8 | `0x081A0000` | 8 | 0 | 0 | 1 | 1 | 2 | 0 | 0 | 0 | 0 | 0 |
| CMD9 | `0x09090000` | 9 | 0 | 0 | 0 | 1 | 1 | 0 | 0 | 0 | 0 | 0 |
| CMD16 | `0x101A0000` | 16 | 0 | 0 | 1 | 1 | 2 | 0 | 0 | 0 | 0 | 0 |
| CMD17 | `0x113A0010` | 17 | 0 | 1 | 1 | 1 | 2 | 0 | 1 | 0 | 0 | 0 |
| CMD55 | `0x371A0000` | 55 | 0 | 0 | 1 | 1 | 2 | 0 | 0 | 0 | 0 | 0 |
| ACMD41 | `0x29020000` | 41 | 0 | 0 | 0 | 0 | 2 | 0 | 0 | 0 | 0 | 0 |
| CMD45/vendor | `0x2D3A0010` | 45 | 0 | 1 | 1 | 1 | 2 | 0 | 1 | 0 | 0 | 0 |
| transfer setup | `0x00200030` | 0 | 0 | 1 | 0 | 0 | 0 | 1 | 1 | 0 | 0 | 0 |

Notes:

- **`DE = 0` on CMD17/CMD45 is correct.** `DE` is DMA Enable. The ROM performs
  CPU FIFO transfers (`0x4002DE60` reads `MMCHS_DATA`), so it leaves DMA off.
  This removes the earlier "low byte/DE unexplained" caveat.
- `RSP_TYPE = 1` is the only value that makes `0x4002DDD4` copy four response
  words; `RSP_TYPE = 0/2/3` store only `RSP10`. This matches CMD2/CMD9 (R2) as
  the only 136-bit responses in the image.
- CMD4 (`0x04040000`) sets **bit 18**, which TRM Table 24-84 marks reserved.
  The write is architecturally ignored; it is an image anomaly, not a decoded
  field.
- `0x2D3A0010` is a data-read command with index 45. The flags are now exact,
  but index 45 with args `0xA1000000`/`0xB1000000` remains vendor-specific (not
  standard SD/MMC); the earlier "unidentified" caveat stands only for the
  vendor meaning, not the bit layout.

---

## Part 3 — Secure/JTAG path: unavailable

Host inventory at the time of this session:

- `lsusb`: only root hubs, a USB audio device, a USB-C audio adapter, a keyboard,
  a mouse and an SD/MMC card reader. **No OMAP/JTAG/debug probe.**
- `/dev/ttyUSB*`, `/dev/ttyACM*`, `/dev/serial/by-id`: none.
- `openocd`, `JLinkExe`, `ds-5`, `arm-none-eabi-gdb`, `gdb-multiarch`: absent.
- `adb devices -l`: empty.

There is therefore **no secure/JTAG path** from this host. The two blocked
dumps could not be taken:

- `0x40026F28`-`0x40027FFF` (the ~4 KB non-readable hole) — not obtainable.
- live IRAM `0x4030D000` (and `0x4030DC1C`) — not obtainable.

Consequently the secure-side semantics of monitor IDs `0xF0`/`0xFE`/`0xF1` and
the `0xF0` descriptor fields remain exactly as classified in
`62-monitor-services.md`: the caller-side structure is observed, the secure
effect is not. No new claim about signature/verification/execution is made here.

To settle it, one of the following is required: a secure-context dump, a JTAG
probe with the OMAP4430 boundary scan / debug access, or a live NS-readable
mapping of the IRAM static-variable area. None is present.

---

## Evidence index (this session)

| operation | Evidence ID |
|---|---|
| `binary_overview` | `ev_bdcb80e5c405a1149c83025a60af2f9599036bd6637d6a5c9526bfa7fc6fefb9` |
| `list_procedures` | `ev_b7981d108fb1ae2ceb69c3831de430d01b8462d7dd46aa3bed3d14d4dbd396ef` |
| `procedure_pseudo_code 0x40028E72` | `ev_661b972cd533010dee8662465f7981749eed9082af57f80e368dd844da790964` |
| `procedure_pseudo_code 0x4002A560` | `ev_ffedc5a8250df6f298f06c5e3f92ed95503cfdc4cbc84e161d025a12374f5441` |
| `procedure_pseudo_code 0x4002A2CA` | `ev_a15c53b09cbfed1cc57d71bb9346381c754ef6eb7b73688b44d2101a6fd3d649` |
| `procedure_pseudo_code 0x4002A0E6` | `ev_4b04a815d50597f6645a8d4f65b75575e21c6248767eef9fac5eb9afd7479f09` |
| `procedure_pseudo_code 0x4002A1BA` | `ev_ea568aa2bc2317b837f4bd01c0cf868ad701bc47ae535ae20432d005a4680ec0` |
| `procedure_pseudo_code 0x4002A212` | `ev_f390dc1ab6f2d4814ab53ff1bf1339574a025af2cc5b8c6041d140149dee743f` |
| `procedure_pseudo_code 0x4002A2BC` | `ev_26fe93bf5c164acc2ed24c766ed89ab8bc1b3bd43ed4112479288213ba524583` |
| `procedure_pseudo_code 0x4002BC7C` | `ev_1c91487ea4008a52b35dc9cd8b4acf4a7e61248300737ee72920de5462e427a2` |
| `procedure_pseudo_code 0x40029F7C` | `ev_ea3cd6c8695290533def53cda70833024e3a11c934d9424a2830d1567d88ff33` |
| `procedure_pseudo_code 0x40029F9C` | `ev_e58bfa0e188aeb5a6e333edecb815954bddf96a9463395ac5544e6f0deab4c88` |
| `procedure_pseudo_code 0x40029FD6` | `ev_afbff7a64d8133f60a1140ba753d3a20c44af59b76df26a51b738e42b569313a` |
| `procedure_pseudo_code 0x4002C6C0` | `ev_ec95a74008466a9c4a7fc442c2d2d296322ca7d64074bf834f2f8b169fc2950e` |
| `procedure_pseudo_code 0x40029794` | `ev_12649c0b97f1c79d16f1d920d447e88380ef5cf58b57b14b0bde9d81f8d84ad6` |
| `procedure_pseudo_code 0x40029AE6` | `ev_4c35cc1fbc668db38bedba58efd75b1e6ded92569670fc59fa04d80512bbf736` |
| `procedure_assembly 0x40029AE6` | `ev_da442eca395e1c48a2e1670e01021529df0f59b1eea6126e297e42d7d5c4b784` |
| `procedure_pseudo_code 0x4002A95A` | `ev_650581cc743e7bc79d9eb15b6676d195ea5fcd13084ea7d644d6ca9da9bda02c` |
| `procedure_pseudo_code 0x40029CA0` | `ev_b878a95811b5f773b3ac38cdf0666b632762ebb4d4d09789917def9766c6877a` |
| `procedure_pseudo_code 0x4002956C` | `ev_b1eef334f39c8f10c62fdf17a662a71905d9ad78cb4a9f2287790157951461de` |
| `procedure_pseudo_code 0x40029738` | `ev_21f3a71a99ff8754b5dc8eebba4dcef9117773b5861b91015e8f3bb56fea6505` |

External sources are public-document context, not target Evidence:
`simu/usbboot-omap4` `include/omap4/omap4_rom.h`;
`swetland/omap4boot` `include/omap4/omap4_rom.h` and `aboot.c`;
`bigunclemax/sync3flash` `target/include/common/omap_rom.h`;
`minoca/os` `uefi/include/dev/tirom.h`.
