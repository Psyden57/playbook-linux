# ROM API table and flash geometry tables — byte-exact decode

Status: resolves the two previously unidentified data blocks in the
`0x40028000` dump.

> Update 2026-10-19b: public OMAP4 ROM-API headers (`simu/usbboot-omap4`,
> `swetland/omap4boot`, `bigunclemax/sync3flash`, `minoca/os`) name 17 of the
> 48 entries and correct several descriptive labels. In particular
> `0x4002A454` is `wdtimer_enable` (WSPR `0xBBBB`/`0x4444`) and `0x4002A430`
> is `wdtimer_disable` (`0xAAAA`/`0x5555`); `0x4002A95A` is
> `ctrl_configure_pads`; `0x4002956C`-`0x40029738` are the I2C driver entries.
> See `64-rom-api-and-media-final.md` §1 for the full mapping.
Date: 2026-10-19. All byte values and disassembly below are **LLVM 23-derived
observations**, not REA Evidence (no native provider configured). The TRM
citations are public-document context, not target evidence.

Files:
- dump: `omap4430-bootrom-0x40028000.bin` (base `0x40028000`,
  SHA-256 `fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`)
- TRM: `swpu231ap.pdf` (OMAP4430/4460 TRM), §27.4.2.1 and Figure 27-6.

## 1. Summary

| Address | Size | Identity | Confidence |
|---|---|---|---|
| `0x40028400`–`0x400284BF` | `0xC0` (48 words) | **ROM API table**: fixed-address Thumb function pointers exported for external callers | **High** — TRM Figure 27-6 places the "API table" at `0x28400`, and the bytes end exactly where code resumes at `0x400284C0` |
| `0x4002FEB4`–`0x4002FECE` | `0x1B` (9 × 3) | Flash geometry records, indexed by the pair table | **High** for shape/indexing; field meaning partly inferred |
| `0x4002FECF`–`0x4002FF2A` | `0x5C` (46 × 2) + `00` | Flash device-ID → geometry-index pair table, NUL-terminated at `0x4002FF2B` | **High** |
| `0x4002FEE0`–`0x4002FF2A` | — | **Not a separate table.** It is the middle/tail of the pair table, starting one byte into record 8 | **High** — see §3.3 |

## 2. `0x40028400` — the ROM API table

### 2.1 Identity

The TRM's ROM memory map (Figure 27-6, p. 5638) shows:

```
0x33FFF  ROM code version
         Code
0x28400  API table
0x28100  Code
0x28080  Dead loops
0x28020  ROM CRC
0x28000  ROM exception vectors
```

The TRM describes it as: "The purpose of this table is to allow external code
access to system maintenance, utility, and device driver functions which are
used for ensuring the ROM code boot functionality. These functions can be reused
at run time by calling a fixed address hardcoded in this table."

The dump agrees exactly: **48 words at `0x40028400`–`0x400284BF` (0xC0 bytes),
then Thumb code at `0x400284C0`.** There is no padding or terminator, which is
why the table length is inferred from the code boundary. (The TRM's `0x28400`
is the boot alias; the dump is the same ROM at `0x40028400`, since
`0x00028000` maps to `0x40028000`.)

### 2.2 All entries are valid Thumb pointers

Every word is odd and in the `0x0002xxxx` range, i.e. a Thumb function address in
the ROM's `0x00028000` boot alias:

- raw `0x00028F05` → target `0x40028F04` (Thumb bit stripped; alias base
  `0x00028000` maps to `0x40028000`).
- all 48 targets land on valid Thumb instruction boundaries; most are listed as
  function starts in `50-code-map.md`.
- no entry is even (ARM) and none points outside the `0x00028000`–`0x00034000`
  window.

### 2.3 Full table

`target = 0x40000000 | (raw & ~1)`; `raw & 1` is the Thumb bit.

| # | raw word | target | short label (where established) |
|---:|---|---|---|
| 0 | `0x00028F05` | `0x40028F04` | cache-op / SMC argument wrapper |
| 1 | `0x00028E2B` | `0x40028E2A` | media/image helper |
| 2 | `0x00028E4B` | `0x40028E4A` | image-op lookup helper |
| 3 | `0x00029D45` | `0x40029D44` | boot/display helper |
| 4 | `0x00028D4F` | `0x40028D4E` | media probe / class selector |
| 5 | `0x00028C45` | `0x40028C44` | large Thumb body entry |
| 6 | `0x00029CD1` | `0x40029CD0` | PRCM/boot helper |
| 7 | `0x00029EDF` | `0x40029EDE` | boot helper |
| 8 | `0x00029795` | `0x40029794` | feature/device-type helper |
| 9 | `0x0002979D` | `0x4002979C` | feature/device-type helper |
| 10 | `0x0002A4C1` | `0x4002A4C0` | read `PRM_RSTST` reset reason |
| 11 | `0x00028613` | `0x40028612` | `0x50000000` helper |
| 12 | `0x000284C1` | `0x400284C0` | `0x50000000` enable/poll |
| 13 | `0x00028567` | `0x40028566` | `0x50000000` helper |
| 14 | `0x000285CB` | `0x400285CA` | `0x50000000` helper |
| 15 | `0x000284D7` | `0x400284D6` | `0x50000000` value helper |
| 16 | `0x00029301` | `0x40029300` | GIC/CPU helper |
| 17 | `0x00029343` | `0x40029342` | GIC/CPU helper |
| 18 | `0x0002938B` | `0x4002938A` | GIC/CPU helper |
| 19 | `0x000293C3` | `0x400293C2` | GIC dispatcher |
| 20 | `0x0002A455` | `0x4002A454` | watchdog kick (BBBB/4444) |
| 21 | `0x0002A431` | `0x4002A430` | watchdog kick (AAAA/5555) |
| 22 | `0x0002A41D` | `0x4002A41C` | watchdog enable/poll |
| 23 | `0x0002A479` | `0x4002A478` | watchdog config + callback |
| 24 | `0x0002956D` | `0x4002956C` | device/boot helper |
| 25 | `0x00029687` | `0x40029686` | device/boot helper |
| 26 | `0x000295FB` | `0x400295FA` | device/boot helper |
| 27 | `0x00029739` | `0x40029738` | device/boot helper |
| 28 | `0x0002A2CB` | `0x4002A2CA` | boot-media descriptor query |
| 29 | `0x00029F9D` | `0x40029F9C` | boot-media helper |
| 30 | `0x00029F7D` | `0x40029F7C` | boot-media helper |
| 31 | `0x00029FD7` | `0x40029FD6` | boot-media helper |
| 32 | `0x00028DFD` | `0x40028DFC` | helper |
| 33 | `0x00028E0B` | `0x40028E0A` | helper |
| 34 | `0x00028E1B` | `0x40028E1A` | helper |
| 35 | `0x0002A561` | `0x4002A560` | small range-check helper |
| 36 | `0x0002925D` | `0x4002925C` | helper |
| 37 | `0x00029051` | `0x40029050` | helper |
| 38 | `0x00028FB5` | `0x40028FB4` | image-load helper |
| 39 | `0x000297B7` | `0x400297B6` | feature read helper |
| 40 | `0x00029AE7` | `0x40029AE6` | PRCM clock dispatcher (`tbb`) |
| 41 | `0x00029CA1` | `0x40029CA0` | PRCM helper |
| 42 | `0x0002A95B` | `0x4002A95A` | PRCM module enable |
| 43 | `0x000297C7` | `0x400297C6` | feature/device-type read |
| 44 | `0x00028605` | `0x40028604` | `0x50000000` helper |
| 45 | `0x00028621` | `0x40028620` | `0x50000000` helper |
| 46 | `0x0002869D` | `0x4002869C` | snapshot `0x50000240` block |
| 47 | `0x000286D5` | `0x400286D4` | `0x50000000` control word |

Labels are descriptive; the only identity claim here is that the block is the
TRM's API table and that each word is a valid Thumb function pointer.

### 2.4 No internal cross-reference

A whole-file scan of both readable dumps found **no** word, halfword, literal,
or ARM/Thumb branch reference to `0x40028400` (or to its Thumb-alias forms
`0x00028400`/`0x00028401`). This is expected for an export table: external
software calls a fixed address from the table, so the ROM itself never needs to
reference the table base. It also explains why the table has no terminator.

### 2.5 Relation to the monitor services

The API table is **not** the SMC dispatch table. Its entries are ordinary
function calls into ROM code; the SMC services are a separate interface
(`R12` function ID + `SMC`), documented in TRM §27.5 and analysed in
`62-monitor-services.md`. Several API entries wrap SMC-using helpers
(e.g. entry 0 → `0x40028F04`), but the table itself does not encode service IDs.

## 3. Flash geometry and device-ID tables

### 3.1 Geometry records at `0x4002FEB4`

Nine 3-byte records; the table ends exactly at `0x4002FECF`, where the pair table
begins:

```
0x4002FEB4: 00 00 00
0x4002FEB7: 22 B6 01
0x4002FEBA: 22 B6 11
0x4002FEBD: 23 B6 01
0x4002FEC0: 23 B6 11
0x4002FEC3: 23 C6 01
0x4002FEC6: 23 C6 11
0x4002FEC9: 23 C7 01
0x4002FECC: 23 C7 11
0x4002FECF: <pair table starts>
```

Record 0 is an all-zero sentinel. Records 1–8 are the eight real geometries.

### 3.2 Pair table at `0x4002FECF`

46 records of `{code, index}`, then a `00` terminator at `0x4002FF2B`:

```
0x4002FECF: 83 03  93 04  84 03  94 04  85 03  95 04  86 03  96 04
0x4002FEDF: 87 03  97 04  F0 01  C0 02  A0 01  B0 02  A2 01  B2 02
0x4002FEEF: F2 01  C2 02  F1 01  C1 02  A1 01  B1 02  DA 03  CA 04
0x4002FEFF: AA 03  BA 04  DC 03  CC 04  AC 03  BC 04  D3 05  C3 06
0x4002FF0F: A3 05  B3 06  D5 07  C5 08  A5 07  B5 08  D7 07  C7 08
0x4002FF1F: A7 07  B7 08  DE 07  CE 08  AE 07  BE 08  00
```

Byte-exact records with addresses:

| # | addr | code | idx | # | addr | code | idx |
|---:|---|---:|---:|---:|---|---:|---:|
| 0 | `0x4002FECF` | `83` | 3 | 23 | `0x4002FEFD` | `CA` | 4 |
| 1 | `0x4002FED1` | `93` | 4 | 24 | `0x4002FEFF` | `AA` | 3 |
| 2 | `0x4002FED3` | `84` | 3 | 25 | `0x4002FF01` | `BA` | 4 |
| 3 | `0x4002FED5` | `94` | 4 | 26 | `0x4002FF03` | `DC` | 3 |
| 4 | `0x4002FED7` | `85` | 3 | 27 | `0x4002FF05` | `CC` | 4 |
| 5 | `0x4002FED9` | `95` | 4 | 28 | `0x4002FF07` | `AC` | 3 |
| 6 | `0x4002FEDB` | `86` | 3 | 29 | `0x4002FF09` | `BC` | 4 |
| 7 | `0x4002FEDD` | `96` | 4 | 30 | `0x4002FF0B` | `D3` | 5 |
| 8 | `0x4002FEDF` | `87` | 3 | 31 | `0x4002FF0D` | `C3` | 6 |
| 9 | `0x4002FEE1` | `97` | 4 | 32 | `0x4002FF0F` | `A3` | 5 |
| 10 | `0x4002FEE3` | `F0` | 1 | 33 | `0x4002FF11` | `B3` | 6 |
| 11 | `0x4002FEE5` | `C0` | 2 | 34 | `0x4002FF13` | `D5` | 7 |
| 12 | `0x4002FEE7` | `A0` | 1 | 35 | `0x4002FF15` | `C5` | 8 |
| 13 | `0x4002FEE9` | `B0` | 2 | 36 | `0x4002FF17` | `A5` | 7 |
| 14 | `0x4002FEEB` | `A2` | 1 | 37 | `0x4002FF19` | `B5` | 8 |
| 15 | `0x4002FEED` | `B2` | 2 | 38 | `0x4002FF1B` | `D7` | 7 |
| 16 | `0x4002FEEF` | `F2` | 1 | 39 | `0x4002FF1D` | `C7` | 8 |
| 17 | `0x4002FEF1` | `C2` | 2 | 40 | `0x4002FF1F` | `A7` | 7 |
| 18 | `0x4002FEF3` | `F1` | 1 | 41 | `0x4002FF21` | `B7` | 8 |
| 19 | `0x4002FEF5` | `C1` | 2 | 42 | `0x4002FF23` | `DE` | 7 |
| 20 | `0x4002FEF7` | `A1` | 1 | 43 | `0x4002FF25` | `CE` | 8 |
| 21 | `0x4002FEF9` | `B1` | 2 | 44 | `0x4002FF27` | `AE` | 7 |
| 22 | `0x4002FEFB` | `DA` | 3 | 45 | `0x4002FF29` | `BE` | 8 |

Terminator: `0x4002FF2B = 00`. The next byte `0x4002FF2C = 00` is separate
padding before the string block at `0x4002FF2D`.

Grouped by geometry index:

| idx | device-ID codes | geometry |
|---:|---|---|
| 1 | `F0 A0 A2 F2 F1 A1` | `22 B6 01` |
| 2 | `C0 B0 B2 C2 C1 B1` | `22 B6 11` |
| 3 | `83 84 85 86 87 DA AA DC AC` | `23 B6 01` |
| 4 | `93 94 95 96 97 CA BA CC BC` | `23 B6 11` |
| 5 | `D3 A3` | `23 C6 01` |
| 6 | `C3 B3` | `23 C6 11` |
| 7 | `D5 A5 D7 A7 DE AE` | `23 C7 01` |
| 8 | `C5 B5 C7 B7 CE BE` | `23 C7 11` |

### 3.3 Why `0x4002FEE0` is not a separate table

`0x4002FEE0` is **17 bytes** into the pair table:

```
0x4002FECF  record 0: 83 03
...
0x4002FEDF  record 8: 87 03
                     ^  ^
                     |  +-- 0x4002FEE0, the index byte
                     +----- 0x4002FEDF, the code byte
```

Reading halfwords from `0x4002FEE0` therefore produces `0x9703`, `0xF004`,
`0xC001`, `0xA002`, … — a shifted view of the same bytes (the low byte is the
tail of one record, the high byte is the start of the next). That is why the
earlier "16-bit table" hypothesis looked plausible and why no reference to
`0x4002FEE0` exists: the real base is `0x4002FECF`, and the only consumer
loads that base from the literal at `0x4002F000`.

The earlier "per-name offset/length table" hypothesis (`20-config-parser.md`
§4.3) is rejected for the same reason: the bytes are a `{device-ID, geometry
index}` map, not offsets/lengths.

### 3.4 Consumer at `0x4002EF14`

Disassembly (Thumb), with the literal pool at `0x4002F000`:

```
4002ef14  push {r3,r4,r5,r6,r7,lr}
4002ef16  mov  r4,r0                 ; r4 = output device record
4002ef18  mov  r1,sp
4002ef1a  bl   0x4002e25c            ; read flash ID -> {man, dev, ...} on stack
4002ef1e  cmp  r0,#0
4002ef20  bne  0x4002effc            ; fail
4002ef22  ldrb r2,[sp,#1]            ; r2 = device ID byte
4002ef26  ldr  r3,[pc,#0xd8]         ; r3 = 0x0002FECF (at 0x4002F000)
4002ef28  b    0x4002eff4

4002eff4  ldrb r1,[r3,r0,lsl #1]     ; pair[i].code
4002eff8  cmp  r1,#0
4002effa  bne  0x4002ef2a
4002effc  mov  r0,#1                 ; not found
4002effe  pop  {r3,r4,r5,r6,r7,pc}

4002ef2a  cmp  r2,r1                 ; device ID == pair[i].code?
4002ef2c  bne  0x4002eff0            ; i++ ; loop
4002ef2e  add  r0,r3,r0,lsl #1       ; &pair[i]
4002ef32  ldr  r3,[pc,#0xcc]         ; r3 = 0x0002FECF again
4002ef34  mov  r6,#1
4002ef36  ldrb r0,[r0,#1]            ; r0 = pair[i].index
4002ef38  subs r3,r3,#0x1b           ; r3 = 0x4002FEB4
4002ef3a  add  r1,r0,r0,lsl #1       ; index * 3
4002ef3e  add  r0,r1,r3              ; &geometry[index]
4002ef40  ldrb r2,[r0,#1]
...
```

Equivalent pseudo-code:

```c
int identify_flash(dev_record *out) {
    uint8_t id[4];
    if (read_flash_id(id)) return 1;
    const pair *p = (const pair *)0x4002FECF;   /* literal 0x0002FECF */
    for (unsigned i = 0; ; i++) {
        if (p[i].code == 0) return 1;           /* NUL terminator */
        if (p[i].code == id[1]) {
            const geo *g = (const geo *)(0x4002FECF - 0x1B + 3*p[i].index);
            /* g = 0x4002FEB4 + 3*index */
            ...
            return 0;
        }
    }
}
```

The subtraction `0x4002FECF - 0x1B = 0x4002FEB4` is the byte-exact proof that
the pair table and the geometry table are one structure: 9 records × 3 bytes =
`0x1B`, ending exactly where the pair table begins.

### 3.5 Geometry field decode

For a record `g[0..2]` (`g[0]=0x22/0x23`, `g[1]=0xB6/0xB7/0xC6/0xC7`,
`g[2]=0x01/0x11`), the consumer writes:

| field | source | observed values | inferred meaning |
|---|---|---|---|
| `dev[0x15]` | `g[0] & 0x0F` | `2` or `3` | device class/technology |
| `dev[0x16]` | `g[0] >> 4` | `2` | device family |
| `dev[0x10]` | `g[1] >> 4` | `0xB` (11) or `0xC` (12) | page-size exponent |
| `dev[0xC]` | `1 << (g[1] >> 4)` | `0x800` or `0x1000` | page size in bytes |
| `dev[0x12]` | `1 << (g[1] & 0x0F)` | `64` (low 6) or `128` (low 7) | pages per block or sub-page count |
| `dev[0x18]` | `g[2] & 0x0F` | `1` | device width class (1) |
| `dev[0x19]` | `1 << (g[2] & 0x0F)` if low nibble is 1 or 2 | `2` | width/interface mask |
| `dev[0x14]` | `g[2] >> 4` | `0` (idx 1,3,5,7), `1` (idx 2,4,6,8) | even/odd geometry flag; consistent with 8-bit vs 16-bit bus |

The `0xB6/0xB7` records therefore describe 2 KB (`0x800`) pages, and `0xC6/0xC7`
describe 4 KB (`0x1000`) pages. The `01` vs `11` in `g[2]` flips only
`dev[0x14]`, which is the most direct evidence for an 8-bit/16-bit width
selection; the exact hardware field names are **inferred**, and the TRM's NAND
geometry terminology was not matched one-to-one.

### 3.6 Special cases and fallback

After the geometry lookup:

- If `id[0] == 0x98` and `id[1]` is in `0x83`–`0x87` or `0x93`–`0x97`, the
  record is accepted unchanged (`0x4002EF84`–`0x4002EF9C`).
- Otherwise, if `dev[0x16] + (g[0] & 0xF) <= 4`, it is also accepted.
- Otherwise a fallback derives geometry from `id[3]`:
  - `dev[0xC] = 0x400 << (id[3] & 3)`
  - `dev[0x10] = 10 + (id[3] & 3)`
  - `dev[0x12] = (0x10000 << ((id[3] >> 4) & 3)) >> dev[0x10]`
  - if `id[0] == 0x98` and `((id[3] >> 2) & 3) == 2`, set
    `dev[0x18] = 2`, `dev[0x19] = 0x34`.

This is the same function's fallback for device IDs whose table entry does not
fit the expected family, not a second table.
