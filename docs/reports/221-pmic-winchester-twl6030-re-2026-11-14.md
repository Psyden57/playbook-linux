# 221 — `pmic-winchester-twl6030`, the TWL6030 regulator resource manager

Date 2026-11-14. **Static, file-only** (no bench, no USB, no network, no sudo, nothing under
`tools/` run). Target: `local/work/1917/ifs/proc/boot/pmic-winchester-twl6030`
(38 250 B, ELF32 ARM EABI5, `DYN`, stripped, `INTERP /usr/lib/ldqnx.so.2`,
`QNX_info NAME=pmic-winchester-twl6030 VERSION=6.5.0 TAGID=118`, built 2012-03-24).

Method: `readelf`, `strings`, `xxd`, and a `python3` + capstone 5.0.7 ARM-disassembly pass
(plain `objdump` here is x86-only). All addresses are **VAs**; for the first `PT_LOAD`
(`off 0x0 → VA 0x0`, filesz `0x79a4`) **VA == file offset**, so every quote below can be
read straight out of the file with `xxd -s <VA>`. The RW segment is `off 0x7f24 → VA 0x8f24`,
i.e. **file = VA − 0x1000** from `0x8f24` up. The GOT base is `0x9000`
(`main` at `0x1eac`: `ldr r6,[pc,#0x488]` → literal `0x7148`, `add r6,pc,r6` → `0x9000`);
string literals are reached as `add rX, r6, #(str−0x9000)`.

Cross-checked against the local tree: `local/kernel-tree/include/linux/mfd/twl.h`,
`drivers/mfd/twl-core.c`, `drivers/regulator/twl6030-regulator.c`,
`drivers/clk/clk-twl.c`, `kernel/source/omap4-winchester.dts`,
`docs/project/KNOWN-ISSUES.md`, reports 118/119/192/219.

---

## 0. Headline answers

1. **The regulator table is at VA `0x9168` (file `0x8168`), 20 records × 32 B** — not 19 at
   `0x8170` (report 219 §4.1 is off by 8 bytes and one record; the 19 *names* it lists are
   right, it just missed `vcore3` and the table's own base). Symbol `regulator_info`
   (`0x9168`, size 640 in `.dynsym`); `write_state` indexes it as
   `regulator_info[id]`, `add r2,r2,r1,lsl #5` @`0x440c`.
2. **The binary performs exactly four TWL writes at start-up, all of them "off"**
   (`0x5e34`): `0x48:0xAF←0xE0`, `0x48:0xAD←0x00`, `0x48:0xB2←0xE0`, `0x48:0xB0←0x00`
   (@`0x5fac`,`0x5ff4`,`0x6044`,`0x608c`). It **never enables anything by itself**.
3. **Exactly one code path can write a STATE register with an "on" value:**
   `write_state` (`0x43ec`, value `…|0x01`) reached from the devctl handler `0x4ea0`
   (dcmd `0x80140505`, i.e. `set_state`), **and** its GPIO twin `0x5c80` which drives
   `GPIO1_2`/`GPIO1_15` instead when the DVT3+ remap is armed. Both need an external
   client: at boot `/proc/boot/pmic-twl6030-ctl -c /etc/system/config/pmic-twl6030.cfg`
   (`set_state regen1 on app`, `set_state regen2 on app`), later `powerman`.
4. **REGEN1/REGEN2 = "off and detached from all groups" is PROVEN** for MBID≥5 / unknown
   revision: `STATE←0xE0` (= all three groups `0x20|0x40|0x80`, state `0x00`=OFF — bit-for-bit
   Linux's `twl6030reg_disable()`) then `GRP←0x00` (detach), twice. This is precisely the
   live `0xAD..0xAF = 00/01/00` and `0xB0..0xB2 = 00/01/00`, and precisely trap 18's manual
   sequence `i2c w 0 48 af e0` + `i2c w 0 48 ad 00`.
5. **It publishes no PPS object at all** (no `pps_*` import in the 93-entry `.dynsym`, no
   `/pps` string). Its whole external surface is one resmgr name, `/dev/pmic/regulator`
   (`0x685c`), with 23 devctls.
6. **It does touch GPIO1** — `mmap_device_io(0x1000, 0x4A310000)` @`0x5e58` — and the two
   lines are **`GPIO1_2` (regen1)** and **`GPIO1_15` (regen2)**, via
   `GPIO_SETDATAOUT 0x194` / `GPIO_CLEARDATAOUT 0x190`, and it *reads* `GPIO_DATAOUT 0x13c`
   in the getter. It **never writes GPIO_OE and never drives them at start-up** — at boot it
   only reads. Our DTS `gpio1-en-2-hog` (`gpios = <2 …>`, `output-high`,
   `line-name = "regen1-gpio"`) **is the same line**. `GPIO1_15` is modelled by nothing.
7. **Verdict: nothing here gates the WL1283.** No VAUX/VMMC/VCXIO/V1V8 enable, no clock, no
   WL_EN (gpio 54) / WL_IRQ (gpio 53), no rail absent from Linux's `twl6030` driver + our
   DTS. The only rail-like thing this binary does that Linux does not is the
   REGEN1/REGEN2 *disable/detach* — and those are the display/panel pair, not the radio
   (§5.2). The 1.8 V SDIO rail is not in this binary's vocabulary. Details in §9.

---

## 1. Regulator table — decoded completely

### 1.1 Record layout (derived from the code, not guessed)

| off | type | field | evidence |
|---|---|---|---|
| `+0x00` | u32 | **id** (0…19) | `reg_read` compares `r1 > 0x13` → "Invalid REGULATOR: %d" (`0x490c`, str `0x7348`) |
| `+0x04` | u32 | **class**: 0=LDO, 1=ExtReg(REGEN), 2=SMPS, 3=VCORE-SMPS | four-way dispatch in `reg_read` `0x4958`, `write_voltage` `0x47f8`, `write_state` `0x4414` |
| `+0x08` | u32 | **name** pointer | printed as `'%s'` in every error string |
| `+0x0c` | u8 | **base**: `GRP=base`, `TRANS=base+1`, `STATE=base+2`, `VOLTAGE=base+3`, `SMPS VSEL=base+4` | `write_state` `ldrb r2,[r2,#0xc]; add r2,r2,#2` @`0x4428`; same +1/+0/+3/+4 in `0x45c8`,`0x475c`,`0x4824`,`0x4854` |
| `+0x0d` | u8 | **pull-down register** (`0xF4`=CFG_LDO_PD1, `0xF5`=CFG_LDO_PD2, `0xF6`=CFG_SMPS_PD, `0`=none) | `set_pd` `ldrb r2,[r4,#0xd]` @`0x4194` |
| `+0x0e` | u8 | **pull-down bit mask** (one-hot) | `ldrb r1,[r1,#0xe]` @`0x416c` |
| `+0x0f` | u8 | **SMPS bit index** inside `SMPS_OFFSET(0xE0)`/`SMPS_MULT(0xE3)`; 0 for non-SMPS | `ldrb r3,[r1,#0xf]; … bic r3,r1,r0,lsl r3` @`0x3ae8` |
| `+0x10` | u16[8] | **allowed output voltages in mV**, little-endian, 0-terminated | `write_voltage` "Specified vout (%d) not allowed by regulator (%s)" (`0x7414`) scans it; dump prints `Voltage: %.2fV [%s]` (`0x6ca8`) |

`regulator_vals` (`0x9448`, 80 B, `.bss`) is a **20 × 4-byte cache** of
`{grp, trans, state, voltage}` written by `reg_read` (`0x48f0`) and read by
`get_groups`/`get_trans`/`get_state` — `GOT[0x154]=0x9448` @`0x4b1c`.

### 1.2 The 20 records (VA of the record, name-pointer VA, name)

`base` is the byte at `+0x0c`. Linux column = `base − 0x30`, the module-relative offset
mainline's `TWL6030_*_LDO/LDO` macros use, and the text of `twl.h` / `twl6030_regs[]` where an
entry exists (see §2.2).

| id | rec VA (file) | class | name @VA | name | base | GRP/TRANS/STATE/VOLT | PD reg | PD mask | mbit | mV menu | Linux name |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `0x9168` (`0x8168`) | 0 LDO | `0x76bc` | `vana` | `0x80` | 80/81/82/83 | `F4` | `01` | 0 | 2100 | VANA (0x50) |
| 1 | `0x9188` (`0x8188`) | 0 LDO | `0x76c4` | `vaux1` | `0x84` | 84/85/86/87 | `F4` | `02` | 0 | 1000,1300,1800,2500,2800,2900,3000 | VAUX1 (0x54) |
| 2 | `0x91a8` (`0x81a8`) | 0 LDO | `0x76cc` | `vaux2` | `0x88` | 88/89/8A/8B | `F4` | `04` | 0 | 1200,1800,2500,2750,2800 | VAUX2 (0x58) |
| 3 | `0x91c8` (`0x81c8`) | 0 LDO | `0x76d4` | `vaux3` | `0x8C` | 8C/8D/8E/8F | `F4` | `08` | 0 | 1000,1200,1300,1800,2500,2800,3000 | VAUX3 (0x5c) |
| 4 | `0x91e8` (`0x81e8`) | 0 LDO | `0x76dc` | `vcxio` | `0x90` | 90/91/92/93 | `F4` | `10` | 0 | 1800 | VCXIO (0x60) |
| 5 | `0x9208` (`0x8208`) | 0 LDO | `0x76e4` | `vdac` | `0x94` | 94/95/96/97 | `F4` | `20` | 0 | 1800 | VDAC (0x64) |
| 6 | `0x9228` (`0x8228`) | 0 LDO | `0x76ec` | `vmmc` | `0x98` | 98/99/9A/9B | `F4` | `40` | 0 | 1200,1800,2800,2900,3000 | VMMC (0x68) |
| 7 | `0x9248` (`0x8248`) | 0 LDO | `0x76f4` | `vpp` | `0x9C` | 9C/9D/9E/9F | `F4` | `80` | 0 | 1800…2500 step 100 | VPP (0x6c) |
| 8 | `0x9268` (`0x8268`) | 0 LDO | `0x76f8` | `vrtc` | `0xC2` | C2/C3/C4/C5 | `00` | `00` | 0 | 1800,2500 | — (no `twl6030_regs[]` entry) |
| 9 | `0x9288` (`0x8288`) | 0 LDO | `0x7700` | `vusb` | `0xA0` | A0/A1/A2/A3 | `F5` | `02` | 0 | 3300 | VUSB (0x70) |
| 10 | `0x92a8` (`0x82a8`) | 0 LDO | `0x7708` | `vusim` | `0xA4` | A4/A5/A6/A7 | `F5` | `01` | 0 | 1200,1800,2500,2900 | VUSIM (0x74) |
| 11 | `0x92c8` (`0x82c8`) | **1 ExtReg** | `0x7710` | `regen1` | **`0xAD`** | AD/AE/AF/— | `00` | `00` | 0 | — | (no mainline node) |
| 12 | `0x92e8` (`0x82e8`) | **1 ExtReg** | `0x7718` | `regen2` | **`0xB0`** | B0/B1/B2/— | `00` | `00` | 0 | — | (no mainline node) |
| 13 | `0x9308` (`0x8308`) | 2 SMPS | `0x7720` | `v1v29` | `0x40` | 40/41/42/(44) | `F6` | `01` | 0 | 1225,1800,1900 | V1V29, id 35 (0x10)¹ |
| 14 | `0x9328` (`0x8328`) | 2 SMPS | `0x7728` | `v1v8` | `0x46` | 46/47/48/(4A) | `F6` | `02` | 1 | 1800 | V1V8 (0x16) |
| 15 | `0x9348` (`0x8348`) | 2 SMPS | `0x7730` | `v2v1` | `0x4C` | 4C/4D/4E/(50) | `F6` | `04` | 2 | 1800,2100 | V2V1 (0x1c) |
| 16 | `0x9368` (`0x8368`) | 2 SMPS | `0x7738` | `vmem` | `0x64` | 64/65/66/(68) | `F6` | `40` | 6 | 1225,1350,1800 | VMEM, id 33 (0x34)¹ |
| 17 | `0x9388` (`0x8388`) | 3 VCORE | `0x7740` | `vcore1` | `0x52` | 52/53/54/— | `F6` | `08` | 3 | — | VDD1 (0x22)² |
| 18 | `0x93a8` (`0x83a8`) | 3 VCORE | `0x7748` | `vcore2` | `0x58` | 58/59/5A/— | `F6` | `10` | 4 | — | VDD2 (0x28)² |
| 19 | `0x93c8` (`0x83c8`) | 3 VCORE | `0x7750` | `vcore3` | `0x5E` | 5E/5F/60/— | `F6` | `20` | 5 | — | VDD3 (0x2e)² |

Notes on things that are easy to get wrong:

* `vpp` has **no string of its own in `strings` output** (only 3 characters, below the
  default minimum): the pointer `0x76f4` points at the bytes `76 70 70 00` between
  `"vmmc"`@`0x76ec` and `"vrtc"`@`0x76f8`. That is why report 219 §4.1 listed nine LDOs and
  skipped vpp while quoting vpp's base anyway.
* `vusb`/`vusim` use **CFG_LDO_PD2 (`0xF5`)**, not `0xF4`, and their bits are `0x02`/`0x01`
  (not one-hot in record order). The three PD registers are separate silicon registers, so
  the bit number is *not* the record index — that is what `+0x0e` is for.
* `vrtc` (`0xC2`) has **no** pull-down register and **no** SMPS index: `+0x0d..+0x0f` are 0.
* Class 2 (`v1v29…vmem`) reads a **4th byte at `base+4`** (`reg_read` `0x4a34`:
  `add r2,r2,#4`, `0x4a58`: `add r3,r3,#3` → `regulator_vals[id].voltage = reg[base+4]`).
  Class 0 LDOs take the voltage from `base+3`. Class 1/3 get `voltage = 0xFF` ("--",
  `mvn r3,#0; strb r3,[r5,#3]` @`0x49e0` / `0x4aa8`).
* `vpp`'s menu is 1800,1900,2000,2100,2200,2300,2400,2500 (`0x8258..0x8264`).
* ¹ `VMEM`/`V1V29` exist only as `twl.h` enum ids (33/35, "non-SR compliant dc-to-dc buck
  convertors", `twl.h:746-751`); `twl6030_regs[]` registers no regulator for them, so the
  Linux column there is arithmetic (`0x10`/`0x34` + `0x30`), not a quoted macro. Same for
  `vrtc` (`0xC2`), which has no entry at all.
* ² `TWL6030_ADJUSTABLE_SMPS(VDD1/2/3)` deliberately sets **no `.base`**
  (`twl6030-regulator.c:510-520`, `:597-599`) because the core voltage is driven by OMAP's
  PRM voltage controller — exactly the `0x2718` path in this binary (§3.4). The
  `0x22/0x28/0x2e` mapping is arithmetic + enum, and is the one row group in this table I
  would call *inference* rather than a quoted value.

### 1.3 Which records are Winchester-specific?

**None of the 20 rails is Winchester-only, and nothing in the table is Winchester-only
either — but the *handling of records 11 and 12 is*.** Concretely:

* Records 11/12 (`regen1` `0xAD`, `regen2` `0xB0`) are the **only class-1 (ExtReg)**
  entries, and class 1 is what triggers the Winchester code:
  `write_state` → `winchester_extreg_*` (`0x43ec`/`0x4ea0`/`0x4df8`/`0x5c80`/`0x5d34`),
  the `confstr(5)`+`"Rev:"` probe, the `GPIO1` mapping and the
  `"Detected MBID=5+, regen1 and regen2 GPIO controlled"` (`0x7868`) /
  `"Unable to determine board revision so setting regen1 and regen2 GPIO controlled"`
  (`0x7804`) paths. Every one of those strings is in `winchester_init`/`winchester_extreg_*`
  (`0x5c80`, `0x5d34`, `0x5e34`).
* Everything else is the stock TWL6030 resource set: ids 0–10 are the PM-receiver LDOs,
  13–19 are `VMEM/V2V1/V1V29/V1V8/VDD1/VDD2/VDD3` — all present in
  `include/linux/mfd/twl.h:743-751` as *TWL6030* resources (so report 219 §4.1's
  "TWL6032-only names" for these is wrong; only `LDO2..LDO5` and `CLK32KAUDIO` are TWL6032).
* The **voltage menus** are board policy, not silicon: e.g. `vana` is pinned to a single
  2100 mV and `vusb` to a single 3300 mV, while mainline describes these as fixed rails too —
  consistent.
* Records **13–19 are never touched** on this board: the Winchester config file
  (`ifs/etc/system/config/pmic-twl6030.cfg`) names only
  `vpp, vmmc, vusim, vaux1, vaux2, vaux3, vusb, regen1, regen2`.
* And there is **no clock resource in the table at all** — which settles the `0x8C`
  argument by construction: in *this* ABI `0x8C` is `vaux3` (record 3). Confirms report 219
  §4.2 / §3.21 and refutes any reading of `0x8C` as `CLK32KG` (that is the TWL6032 table in
  `drivers/clk/clk-twl.c:123-136`).

---

## 2. I²C mechanics

### 2.1 The one write primitive

```
twl6030_i2c_write_byte(ctx /*r0*/, slave /*r1*/, reg /*r2*/, value /*r3*/)   @ 0x1d30
    sp+0xf = value; iov = { {slave,len=2,…}, {&reg,1}, {&value,1} }
    devctlv(ctx->fd, 0x80100505, 3, iov, …)                                   @ 0x1cf0
    on failure: "pmic-twl6030:   twl6030_i2c_write fail" (0x6714)              @ 0x1cfc
```
`twl6030_i2c_write_msg(ctx, slave, reg, buf, len)` @`0x1c74` is the same 3-part
`devctlv(…, 0x80100505, 3, …)` with `len` data bytes (`0x1ce4`).
`twl6030_i2c_read` @`0x1b8c` (msg `0x80100505`, `devctlv`) with
`"twl6030_i2c_read fail"` (`0x66ec`).
The bus is opened by `i2c_open` @`0x1d7c`: `open("/dev/i2c0", 2)` (`0x67cc`, literal at
`0x1e10`) then `devctl(fd, 0x80040502, &speed, 4)` with `speed = 400000`
(`movw r3,#0x1a80; movt r3,#6` @`0x1fa4`) — errors `0x673c`/`0x6770`.

**Every transfer in the image uses slave `0x48`** (`mov r1,#0x48` at `0x378c`, `0x3994`,
`0x3a2c`, `0x3b34`, `0x3cf0`, `0x3d44`, `0x3e44`, `0x4190`, `0x4430`, `0x45d0`, `0x4758`,
`0x4828`, `0x4a0c`,`0x4a50`, `0x4a88`, `0x55d8`,…, `0x5fa0`,…). No `0x49`/`0x4A`/`0x4B`
access exists: the file `pmic_twl6030_cfg` writes `0x49` and `0x38`, this resmgr does not.

### 2.2 Register-block convention and the −0x30 correction

`base+0/1/2/3` = GRP/TRANS/STATE/VOLTAGE is exactly mainline's
`VREG_GRP/VREG_TRANS/VREG_STATE/VREG_VOLTAGE` (`twl6030-regulator.c:47-52`).
Mainline's `.base` values look 0x30 lower (`VANA 0x50`, `VAUX3 0x5c`, `VMMC 0x68`, …)
because `twl_i2c_write()` adds `twl_map[mod].base` and
`TWL6030_BASEADD_PM_SLAVE_MISC = 0x0030 /* PM_RECEIVER */`
(`drivers/mfd/twl-core.c:101-104`, `:351`, `:459`). **0x50+0x30 = 0x80, 0x5c+0x30 = 0x8C,
0x68+0x30 = 0x98, 0x74+0x30 = 0xA4.** So the QNX table and mainline agree on the hardware
addresses; only the source-level naming differs. (Report 192 §7's parenthetical "mainline
`VANA 0x50`…" is correct as a mainline *source* value but has no bearing on what a bus
sniffer sees.)

### 2.3 STATE-register encoding (needed to read §4/§5)

Four independent confirmations, all agreeing:

* `write_state` writes `groupmask | code` where `groupmask ∈ {0, 0x20, 0x40, 0x80}` from
  `h_4ea0` `0x4f68-0x4fb8`, and `code = table[state]` from the 3-byte table at **`0x6E98` =
  `{0x00, 0x03, 0x01}`** for state index `{off, sleep, on}`.
* `decode_state2` (`0x2924`) maps `0→"OFF"` (`0x6a88`), `1→"SLEEP"` (`0x6a98`),
  `2→"ON"` (`0x6a94`) — literal loads at `0x297c`/`0x2978`/`0x2980`; the sibling
  `decode_state1` (`0x2860`) maps `1→"AMS"` (`0x6a84`), `2→"ACTIVE"` (`0x6a8c`), `0→"OFF"`
  and is what the dump/`get_trans` path uses.
* `reg_read`'s inverse table at **`0x6CE0` = `{0, 2, 0, 1}`** maps the 2-bit field
  `0b00→OFF, 0b01→ON, 0b10→OFF2, 0b11→SLEEP`.
* And mainline says it verbatim: `TWL6030_CFG_STATE_OFF 0x00`, `_ON 0x01`, `_OFF2 0x02`,
  `_SLEEP 0x03`, `_GRP_SHIFT 5` (`twl6030-regulator.c:63-69`).

So: **STATE = `(APP?0x20)|(CON?0x40)|(MOD?0x80) | state`** — that part is direct from the
encoder. *Inference, but a forced one:* bits 7:5 are **write-only command selectors** that
read back as the per-group status fields `bits[3:2]=APP, [5:4]=CON, [7:6]=MOD` — which is
exactly how `get_state` (`0x4df8`) unpacks the byte (`and #3`, `ubfx #2,#2`, `ubfx #4,#2`,
`lsr #6`) — and why a regen read back `0x00`, not `0xE0`, after `0xE0` was written to it.

---

## 3. Every I²C write, in order, by code path

`ctx` is the 0x90-byte OCB (`calloc(1,0x90)` @`0x1f54`); `fd` is OCB`+0x74`.
Only writes are listed; **the reads are** `reg_read` `0x48f0` (1–4 B from `base`, `base+4`),
`read_msk_transition` `0x37f0` (`0x48:0x20`), `get_smps_mult` `0x391c` (`0x48:0xE3`),
`get_smps_offset` `0x3c30` (`0x48:0xE0`), `get_pd` `0x3f44` (`0x48:0xF4/F5/F6`, each cached in
the 3-byte `pd_vals` `0x9444`).

### 3.1 init — `winchester_init` @`0x5e34` (called from `main` @`0x201c`, BEFORE the resmgr is attached)

Order inside the function: map GPIO → `confstr(5)`+`strstr("Rev:")` → `strtoul` → gate →
four writes. The mapping happens **first** and unconditionally; only the writes are gated.

| # | VA | bus | slave | reg | value | meaning |
|---|---|---|---|---|---|---|
| 1 | `0x5fac` | `/dev/i2c0` | `0x48` | `0xAF` (`base+2`, from `regulator_info+0x16c` = rec 11 `+0x0c`) | **`0xE0`** | regen1 STATE = OFF in APP+CON+MOD |
| 2 | `0x5ff4` | | `0x48` | `0xAD` (rec 11 `+0x0c`) | **`0x00`** | regen1 GRP = detached from all groups |
| 3 | `0x6044` | | `0x48` | `0xB2` (rec 12 `+0x18c` = 0xB0, +2) | **`0xE0`** | regen2 STATE = OFF in APP+CON+MOD |
| 4 | `0x608c` | | `0x48` | `0xB0` (rec 12) | **`0x00`** | regen2 GRP = detached |

Each is followed by `cmp r0,#0; beq <next>`. **A failure does not continue:** it prints
`"…%s: Failed to disable regen1"` (`0x78b4`) / `"…Failed to detach regen1 from all groups"`
(`0x78e4`) / `"…Failed to disable regen2"` (`0x7924`) / `"…Failed to detach regen2 from all
groups"` (`0x7954`), then falls into the common exit `0x60b8` → `bl gpio_unmap`
(`0x60bc`) → `mvn r0,#0` (`0x60c0`), i.e. **returns −1**, and `main` @`0x2020`
(`cmn r0,#1; beq 0x2304`) aborts the whole service. So on the reference those four writes
must have succeeded — worth knowing before replaying them from Linux (trap 18).

The gate (`0x5eb4-0x5f88`):

```
confstr(5, buf, 0x80)                      ; 0x5ec0  (_CS_MACHINE in QNX)
if (ret == 0)              -> skip (no writes at all)       0x5ed0
p = strstr(buf, "Rev:")                    ; 0x5ee4/0x5eec (lit 0x7804 area)
if (!p)                    -> skip                          0x5ef4
*errno = 0; v = strtoul(p+4, 0, 10);       ; 0x5f10
if (v == 0 && errno == 0x16) -> "Unable to determine board revision so setting
                                regen1 and regen2 GPIO controlled" (0x7804), ready=1
else if (v > 4)              -> "Detected MBID=5+, regen1 and regen2 GPIO controlled"
                                (0x7868), ready=1
else                         -> skip (ready stays 0)        0x5f4c/0x5f7c
```

`ready` is the global at **`0x9438`** (`.bss`, `GOT+0x438`); the mapped GPIO VA is at
**`0x943c`** (`GOT+0x43c`). Both `winchester_extreg_set_state` (`0x5c8c`) and
`winchester_extreg_get_state` (`0x5d34`) test `0x9438` first, so on a DVT2 board (Rev ≤ 4)
those two functions are inert and regen1/2 behave like ordinary PMIC regulators.

Note the ordering consequence: this all happens **before** `resmgr_attach`, so no client can
race it.

### 3.2 enable/disable — `write_state` @`0x43ec` (the only STATE writer)

```
write_state(ctx, id, value)        0x43ec
  if (regulator_info[id].class > 3)  -> unreachable with this table (all classes are 0..3)
  twl6030_i2c_write_byte(ctx, 0x48, regulator_info[id].base + 2, value)   0x4438
  then read_state(ctx, id, 0) to refresh the cache                       0x448c
  failure: "…%s: Failed to write REGULATOR STATE for '%s'" (0x71d0)      0x4474
```
Dynamic, so the concrete address depends on `id`; the reachable values are
`(mask)|{0x00,0x01,0x03}`. Its callers are `0x4ea0` (devctl `set_state`) and the
**unreachable** `0x553c` (§3.6).

### 3.3 group / transition / voltage setters (same primitive, same slave)

| function | VA | slave:reg | value | reached from |
|---|---|---|---|---|
| `write_trans` | `0x458c` (write at `0x45d8`) | `0x48:(base+1)` | three 2-bit per-group fields, `(a<<4)\|(c<<2)\|m` from encode table `0x6CD8` (class≠1) / `0x6CDC` (class 1) — build at `0x5060-0x506c` | devctl `0x80140503` |
| `write_grp` | `0x471c` (write at `0x4760`) | `0x48:(base+0)` | `bit0\|bit1<<1\|bit2<<2` from payload `{app,con,mod}` (build at `0x51b4-0x51dc`) — **no-op for class 1** (early return `0x519c`) | devctl `0x80140501` |
| `write_voltage` | `0x47d0` — class0 `0x4830` → `0x48:(base+3)`, class2 `0x4858` → `0x48:(base+4)`, class3 `0x4880` → **PRM VC bypass** (no I²C from this process), class1 `0x48c8` → error | VSEL byte | devctl `0x80140507` (`set_vout`), dead `0x553c` |
| `set_pd` | `0x40a8` (write at `0x419c`) | `0x48:regulator_info[id].pd_reg` (`0xF4/F5/F6`) | cached `pd_vals` byte ± `pd_mask` | devctl `0x8014050b` |
| `unmask_preq` / `mask_preq` `0x3850` | write `0x390c` | `0x48:0x20` (**MSK_TRANSITION_REG**) | cached `preq_mask` (`0x9440`) OR / BIC `(app?0x20)\|(con?0x40)\|(mod?0x80)` (`0x387c-0x38a4`) | devctl `0x80140514` / `0x80140515` |
| `twl6030_smps_set_mult` | `0x3a48` (public, validates class then read-modify-writes, write at `0x3b40`/`0x3a38`); bare bit-twiddles at `0x3998` (`orr #0x80`) and `0x39f4` (`and #0x7f`) | `0x48:0xE3` (**SMPS_MULT**) | one bit of `smps_mult` (`0x9442`) at `+0x0f`, plus bit 7 — which the dump labels `(LOCKED)` (`0x6c6c`) | devctl `0x80140513`; bare set/clear of bit 7 at `0x4014050f` / `0xc014050e` |
| `twl6030_smps_set_offset` | `0x3d5c` (public, validates class then read-modify-writes, write at `0x3d4c`); bare bit-twiddles at `0x3cac` (`orr #0x80`) and `0x3d08` (`and #0x7f`) | `0x48:0xE0` (**SMPS_OFFSET**) | one bit of `smps_offset` (`0x9441`) at `+0x0f`, plus bit 7 (same `(LOCKED)` notion) | devctl `0x80140511`; bare set/clear of bit 7 at `0xc014050d` / `0xc014050c` |

The three "PMIC global" registers are cached at init by `twl6030_ldo_init` (`0x52b4`,
strings `CFG_LDO_PD1: %02x`, `CFG_LDO_PD2: %02x`, `CFG_SMPS_PD: %02x`, `SMPS_OFFSET: %02x`,
`SMPS_MULT:   %02x`, `PREQ_MASK:   %02x` at `0x6b34`…). It performs **reads only**. The
canonical addresses are confirmed by mainline's own constants:
`TWL6030_BASEADD_PM_MISC 0x00E2`, `TWL6030_BASEADD_PM_PUPD 0x00F0`
(`twl-core.c:105-106`) → `0xF0+4/5/6 = 0xF4/F5/F6` are the pull-up/pull-down registers.

### 3.4 the VCORE path (no I²C) — `0x2718`

For class 3, `write_voltage` calls `0x2718(ocb, 0x12, base+4, vsel)` (`0x4880`), which
programs the **OMAP PRM voltage-controller I²C bypass**: `PRM_VC_VAL_BYPASS` at
`ocb->[0x7c]+0x1b80+0x20`, `PRM_VC_CFG_I2C_MODE` `+0x28`, `PRM_VC_CFG_I2C_CLK` `+0x2c`
(= `0x6026`, `0x2750`), sets `VALID` (`0x1000000`) and polls it clear (`0x2788`). With
`ocb->[0x7c] = 0x4A306000` that is `0x4A307BA0/BA8/BAC` — the same registers
`startup-playbook` writes (report 219 §2.2). **But `map_prm` (`0x27f4`), the function that
would set `ocb->[0x7c]`, has no caller anywhere in the image** (no `bl`, no branch, no
literal; `ocb->[0x7c]` is initialised to 0 at `0x1fb4`). So a `set_vout` on
`vcore1/2/3` writes to `0x1BA0`-ish user addresses. *Inference:* it faults; either way it
does not touch the PMIC. Flagged, not exercised.

### 3.5 multi-byte writes — `write_config` @`0x553c` — **DEAD CODE**

`0x553c` writes whole register blocks in one `devctlv`: 4 B at `base` (`0x55e0`), 3 B at
`base` (`0x560c`, `0x5638`, `0x569c`), 1 B at `base+4` (`0x566c`). It also calls
`write_state`/`write_trans`/`write_grp`/`write_voltage`. **It has no caller**: zero `bl`
sites, and the constant `0x553c` appears nowhere in the file (checked as a 32-bit literal).
So the "apply a whole regulator config in one transaction" path is compiled in but
unreachable. (Everything a client can do goes through the single-byte primitives above.)

### 3.6 Total

Program-order write inventory: **4 writes at start-up** (§3.1) and **8 write *sites***
behind devctls (§3.3) plus the GPIO pokes (§7). Nothing else in the image issues an I²C
write — every other `0x1c74` caller is inside the dead `0x553c`.

---

## 4. **Which paths ENABLE a rail, and what triggers them**

A rail is "enabled" here only by writing `STATE` with low bits `0x01` (ON) — or, for
`regen1/2` on DVT3+, by driving the corresponding GPIO high.

### 4.1 The enable path

```
client → devctl(/dev/pmic/regulator, 0x80140505, {id, app, con, mod, state})
  twl6030_devctl 0x15d8  → 0x1940  → h_4ea0 0x4ea0
      if (regulator_info[id].class == 1) {          ; 0x4ed0 regen1/regen2
          if (winchester_extreg_get_state(id) <= 0) return 0;   ; 0x4ed8 -> 0x4fcc
      }
      read_state → out{id,state,app,con,mod}        ; 0x4eec = h_4df8 0x4df8
      value = (app?0x20:0)|(con?0x40:0)|(mod?0x80:0)|code[state]   ; 0x4f68-0x4fb8
      write_state(ctx, id, value)                   ; 0x4fbc
```
* `code[2] = 0x01` → **this is the only place a rail is turned ON in the PMIC.**
* `app/con/mod` are "which resource groups get this command". `set_state vaux3 on app`
  → `0x48:0x8E ← 0x21`. `set_state vaux3 on app con mod` → `0x48:0x8E ← 0xE1`.
* Optimisation: if the requested per-group status already equals `state`, and
  `ocb->[0x80]` (force-update, devctl `0x80040507`) is 0, the write is skipped (`0x4f4c`).
  `set_force_update 1` disables that check.

### 4.2 The regen/GPIO variant (class 1)

```
h_4ea0 0x4ed8 → winchester_extreg_get_state 0x5d34   ; returns >0 only if ready && GPIO high
    ... (protocol as above, but) ...
h_4ea0 0x4eec → h_4df8 0x4df8
    if (class == 1) winchester_extreg_set_state 0x5c80(id, &req)      ; 0x4e28
```
and `winchester_extreg_set_state` (`0x5c80`) is the *actuator*:

```
p = 0x943c;  if (!0x9438) return 1;                       ; 0x5c8c
addr = req->state ? p + 0x194 : p + 0x190;                ; SET/CLEARDATAOUT
if (id == 11) { *(u32*)addr = 0x00000004; return 0; }     ; GPIO1_2
if (id == 12) { *(u32*)addr = 0x00008000; return 0; }     ; GPIO1_15
print "…%s: Invalid regulator: %d" (0x77a0); return -1;
```
It returns 0, and `h_4ea0`'s `cmp r0,#0; ble 0x4fcc` (`0x4ee0`) then returns **without
touching the PMIC**. So on DVT3+ the regen STATE/GRP registers are never written after
start-up — only the GPIOs move. (Reverse-read: `winchester_extreg_get_state` `0x5d34`
reads `DATAOUT 0x13c` bit 2 / bit 15 and synthesises `state = 2` ("ON") or `0`.)

### 4.3 Triggers, in the order they happen on a real boot

| when | who | what | enables? |
|---|---|---|---|
| IFS boot script, after `pmic-winchester-twl6030 -U 99:99` | `.script` record `Initialize regulators` → `/proc/boot/pmic-twl6030-ctl_script` → `pmic-twl6030-ctl -c /etc/system/config/pmic-twl6030.cfg` | devctls over the file below | **yes, two rails** |
| run level 1 | `powerman` (registered for `regen1,regen2,vaux3` — report 219 §6) | `set_state … off` | no (turns off) |
| any client, privileged | `/dev/pmic/regulator` | any of the 23 devctls | yes |

And the cfg file (`ifs/etc/system/config/pmic-twl6030.cfg`) is explicit about the two
enables:

```
# Set default state for regen1 and regen2 to ON. Powerman will eventually tell us to turn these off.
# On DVT3+ (ie: DVT4+ on the sticker), this goes to GPIOs 2 and 15.
set_state regen1 on app
set_state regen2 on app
```
Everything else in that file is *off* (`set_state vpp/vmmc/vusim off app con mod`,
`set_pd vpp off`, `set_groups vmmc/vpp/vusim`, `set_trans vaux1/2/3,vusb sleep=ams act=ams`,
`unmask_preq 1`), with `set_groups regen1 app` / `set_groups regen2 app` wrapped in
`BEGIN BLOCK / MACHINE Winchester:3- / … / END BLOCK` so it applies only to DVT2 and earlier.

**Therefore the only rails this board's software ever turns ON are REGEN1 and REGEN2 — and on
DVT3+ only as GPIO1_2 / GPIO1_15, never as a PMIC write.** The three open/close events in
order are: the resmgr forces them OFF at its own start (§3.1), the boot cfg turns them ON
(as GPIOs), powerman turns them OFF. **No LDO and no SMPS is ever enabled by the PMIC stack
on this board.**

Boot order in `main` (`0x1ea0`), for the record:
`calloc(0x90)` `0x1f54` → `twl6030_options` `0x1fd8` (parses `n:p:vc:ufU:`, `0x69d8`) →
`i2c_open` `0x2008` → **`winchester_init` `0x201c`** → `dispatch_create` `0x2028` →
`iofunc_func_init` `0x20b0` → `twl6030_ldo_init` `0x20f0` (reads, caches PD/OFFSET/MULT/PREQ)
→ `resmgr_attach("/dev/pmic/regulator")` `0x2170` → `dispatch_context_alloc` → drop
privileges (`0x23e8`, `setgid/setuid` via `getpwnam`) → `dispatch_block` loop `0x2294`.

---

## 5. REGEN1 / REGEN2: proof, and what they physically are

### 5.1 The state question — proven from the binary alone

The two statements "off" and "detached from all groups" are literally the two writes in
§3.1, and their operands are the two fields of the same record:

* `ldrb r2,[r3,#0x16c]` @`0x5f94` where `r3 = regulator_info` (`0x9168`):
  `0x9168+0x16c = 0x92D4` = record `0x11` `+0x0c` → `0xAD` → `+2` = **`0xAF` = STATE**.
  Value `0xE0` = `0x20|0x40|0x80` = APP+CON+MOD with state `0x00` = **OFF**. This is
  byte-identical to mainline's `twl6030reg_disable()`
  (`P1|P2|P3) << 5 | TWL6030_CFG_STATE_OFF`, `twl6030-regulator.c:170-175`).
* `ldrb r2,[r3,#0x16c]` with `r3 = 0` @`0x5ff4` → **`0xAD` = GRP** ← `0x00`. GRP is the
  group-membership register (`twlreg_grp()`, `twl6030-regulator.c:105-110`) — writing 0
  removes the rail from APP/CON/MOD, i.e. **detached**.
* Same pair for regen2 with `+0x18c` (`0x92F4` = record 12 `+0x0c` → `0xB0`) →
  `0xB2←0xE0`, `0xB0←0x00` (`0x6044`, `0x608c`).

Live read-back `0xAD..0xAF = 00/01/00` and `0xB0..0xB2 = 00/01/00` (report 219 §4.4,
trap 135) is exactly `GRP=0x00`, `TRANS=0x01` (untouched, reset value), `STATE=0x00` (all
group status bits off, §2.3). **Report 219 §4 is confirmed, and the mechanism is now
identified line by line.** Trap 18's manual recipe (`i2c w 0 48 af e0`, `i2c w 0 48 ad 00`)
is this code, byte for byte — and its "wedges the I²C bus" warning applies to *replaying the
same two writes on Linux* while the WL1283 is being probed. Note that the resmgr itself
treats any failure of those two writes as fatal (§3.1), so a replay wedges the bus *and*
kills the PMIC service.

Gate: only when `confstr(5)` contains `Rev:` and the parsed number is **> 4** (or when the
string is present but unparsable with `errno==EINVAL`, `0x5f1c`). On `MACHINE Winchester:3-`
(≤DVT2) the code skips **all four writes** and `ready` stays 0, so regen1/2 remain ordinary
PMIC rails and the cfg's `set_groups regen1 app` block is what gives them a group.

### 5.2 Physical rail — from local sources, with inference marked

* **`drivers/clk/clk-twl.c` is irrelevant to `0x8C` here.** `twl6032_clks[]` places
  `clk32kg` at base `0x8C` (`clk-twl.c:123-136`) — that is the **TWL6032** table. In this
  binary `0x8C` is `vaux3`'s record (`0x91c8`, `+0x0c = 0x8C`). Trap 135 / §3.21 already
  corrected this; the table is a second, independent proof.
* **`include/linux/mfd/twl.h` has no `REGEN1/REGEN2` register define** — only
  `#define RES_REGEN 21` (`twl.h:507`, the TWL4030 resource-provider id). So Linux cannot
  even model these two rails: they are absent from `twl6030_regs[]` and from
  `twl6030.dtsi`, which is consistent with our DTS covering them with plain GPIO hogs.
* **`drivers/regulator/twl6030-regulator.c`** therefore gives us only the *mechanism*:
  `twl6030reg_disable()` writing `0xE0` is exactly the value in §5.1 — a strong
  cross-check, not an identification.
* **Identification: report 192 §7 / trap 135** — "`0xB0`–`0xB2` is **REGEN2** … the
  display/panel rail (report 187), not WLAN", with REGEN1 as its partner
  (`0xAD`–`0xAF`). Trap 135 additionally records that `startup-playbook` drives
  `GPIO1` bit 15 low as an output (trap 135's list: bits 0/4/15/18/28/31).
  **This is a report-level attribution, i.e. inference in the strict sense** — nothing in
  this binary names a load. What the binary *does* prove is (a) the two are a pair handled
  by Winchester-only code, (b) on DVT3+/MBID≥5 they are taken out of the PMIC's control
  and reduced to two GPIO lines, and (c) they are switched ON by the boot cfg and then OFF
  by powerman — a pattern that fits a panel/backlight rail and fits nothing about SDIO.
* Negative evidence for "regen = WLAN": trap 136 toggled exactly these lines
  (`GPIO1_OE` bits 0/4/15/18/28/31) on the live 1917 and the radio stayed up
  (`ti0` RUNNING, `wifi_power::on`). Regens are not the radio.

**So: "off and detached from all groups" — proven. "The panel/backlight rail" — inferred from
report 187/192/trap 135, not from this file.**

---

## 6. External surface: PPS objects and devctls

### 6.1 PPS: none

* `.dynsym` has 93 entries and **no `pps_*`** symbol; imports are `resmgr_attach`,
  `dispatch_*`, `iofunc_*`, `devctl`, `devctlv`, `MsgReply`, `open/close/read/write`,
  `calloc/free`, pthread, signal, `slog*` (`__slog_once`, `__slog_mux`, `_slogfd`).
* No `"/pps"` string in the file (`strings -a | grep -i pps` → empty), and the only
  filesystem names present are `/dev/i2c0` (`0x67cc`), `/dev/pmic/regulator` (`0x685c`),
  `/dev/slog` (`0x7994`).
* Therefore this service cannot be steered through PPS at all; `powerman` must be using the
  devctl interface (or its own device) — report 219 §6's "powerman clients for
  regen1,regen2,vaux3" is consistent with that, but *that registration is not in this
  binary* (no powerman import here either).

### 6.2 `/dev/pmic/regulator` — the 23 devctls

`twl6030_devctl` @`0x15d8` (size 1460) first calls `iofunc_devctl_default()`
(`"…iofunc_devctl_default() failed"`, `0x6628`), logs every call once verbose
(`"…msg->i.dcmd = 0x%x, msg->i.nbytes = %d"`, `0x6658`), then dispatches on
`msg->i.dcmd` with a comparison tree (`0x164c`…`0x1894`). Unknown → `"…Unknown command : %x"`
(`0x66c4`) and `EINVAL` (`0x59`). Payload word 0 is the regulator id; ids > 0x13 are
rejected with `"…devctl with unknown LDO id: 0x%x"` (`0x6690`).

The dcmd word's own layout reads as `dir<<30 | req_len<<16 | 0x50/0x51<<8 | sub`
(`0x80140505` = write-direction, 20-byte request). **23 valid commands**, extracted by
simulating the `movw`/`movt` state through the whole tree (not by eye — a hand transcription
of these constants is easy to transpose):

| dcmd | dir | req | handler | does |
|---|---|---|---|---|
| `0x80140501` | w | 20 | `0x5174` | **`set_groups`** → `write_grp` (`0x48:base+0`); **early-returns 0 for class 1** |
| `0x80140503` | w | 20 | `0x4fe0` | **`set_trans`** → `write_trans` (`0x48:base+1`) |
| `0x80140505` | w | 20 | `0x4ea0` | **`set_state`** → `write_state` (`0x48:base+2`) **or the regen GPIO** ← §4 |
| `0x80140507` | w | 20 | `0x4b24` | **`set_vout`** (`"…Setting vout for VCore SMPS (%s) only allowed when -f specified"`, `0x73b4`) → `write_voltage` |
| `0x8014050b` | w | 20 | `0x40a8` | **`set_pd`** → `0x48:0xF4/F5/F6` (`"…Setting pull-down not allowed on regulator (%s), restart with -f to force"`, `0x70f0`) |
| `0x80140511` | w | 20 | `0x3d5c` | **`set SMPS_OFFSET`** (bit `+0x0f`, validated) → `0x48:0xE0` |
| `0x80140513` | w | 20 | `0x3a48` | **`set SMPS_MULT`** (bit `+0x0f`, validated) → `0x48:0xE3` |
| `0x80140514` | w | 20 | `0x3850` | **`unmask_preq`** — OR into `preq_mask`, write `0x48:0x20` |
| `0x80140515` | w | 20 | `0x3850` | **`mask_preq`** — BIC out of `preq_mask`, write `0x48:0x20` |
| `0x80040507` | w | 4 | inline `0x1b08` | **set force-update** (`ocb->[0x80] = payload[0] != 0`) |
| `0xc0140500` | rw | 20 | `0x5224` | **`get_groups`** — reply `{id, app, con, mod, ?}` from GRP bits 0/1/2 |
| `0xc0140502` | rw | 20 | `0x50b4` | **`get_trans`** — reply from TRANS bits 1:0/3:2/5:4 |
| `0xc0140504` | rw | 20 | `0x4df8` | **`get_state`** — reply `{id, state, app, con, mod}` from STATE's four 2-bit fields |
| `0xc0140506` | rw | 20 | `0x4d14` | **`get_vout`** (class 3 → `"VCore SMPS (%s) don't support get_vout"`, `0x7478`) |
| `0xc014050a` | rw | 20 | `0x4048` | **`get_pd`** |
| `0xc014050c` / `0xc014050d` | rw | 4 | `0x3d08` / `0x3cac` | **unlock / lock SMPS_OFFSET** (`0x48:0xE0` bit 7) |
| `0xc014050e` / `0x4014050f` | rw / r | 4 | `0x39f4` / `0x3998` | **unlock / lock SMPS_MULT** (`0x48:0xE3` bit 7) |
| `0x40140510` / `0x40140512` | r | 4 | `0x3e74` / `0x3b60` | **get SMPS_OFFSET / SMPS_MULT** |
| `0x40140516` | r | 4 | `0x37f0` | **get MSK_TRANSITION** (`0x48:0x20`) |
| `0x40040506` | r | 4 | inline `0x1aec` | **get force-update** (reply `ocb->[0x80]`) |

`0xc014050b` is a **hole**: the binary-search arm `bge` on `>= 0xc014050b` lands on a block
that only tests `0xc014050c`/`0xc014050d`, so `0xc014050b` falls into "Unknown command".
That matches `pmic-twl6030-ctl`'s documented verb list exactly when the two directions are
paired up (get/set groups, trans, state, vout, pd, force-update, preq unlock/mask, SMPS
offset/mult and their lock bits) — i.e. **there is no devctl here that is not reachable from
the documented CLI**, and none that reaches a rail without `set_state`.

Two observations worth carrying forward:

* **`set_state` (`0x80140505`) is the rail switch.** Any process with write access to
  `/dev/pmic/regulator` can turn any of the 18 PMIC rails on, and (on DVT3+) can pull
  `GPIO1_2`/`GPIO1_15` high. There is no per-command privilege check beyond the QNX
  `iofunc` default (the process runs as the `-U` user, `99:99` in the boot script, and
  `main` calls `setgid`/`setuid` from `getpwnam` at `0x23e8`).
* The id check at `0x16a4` is **skipped** for exactly five dcmds — `0x80140514`,
  `0x80140515`, `0x80040507`, `0x40040506`, `0x40140516` — because the compiler hoisted it
  into only two of the three comparison paths. The handlers that use an id re-validate
  inside `reg_read`/`write_*` (`0x490c`), so this is harmless, but it is why
  `0x80140514` reaches `0x3850` with an unvalidated payload[0].

---

## 7. GPIO

Yes — but only `GPIO1`, and only through the bank's data registers.

* Mapping: `mmap_device_io(0x1000, 0x4A310000)` @`0x5e58-0x5e68`
  (`movw r2,#0; movt r2,#0x4a31` @`0x5e60`, length `0x1000` @`0x5e58`), VA stored at
  `0x943c` (`str r0,[r3,#4]` @`0x5e74`), `munmap_device_io(…,0x1000)` in `gpio_unmap`
  `0x5e00`. This is the OMAP4 **GPIO1** bank base.
* Registers used (all relative to `0x4A310000`):
  | offset | register | use | VA |
  |---|---|---|---|
  | `0x13C` | `GPIO_DATAOUT` | **read only**, to synthesise the regen state | `winchester_extreg_get_state` `0x5d34`: `ldr r3,[ip,#4]` @`0x5d68`, `add r3,r3,#0x13c` @`0x5d6c`, `ldr r3,[r3]` @`0x5d70`, `and r3,r3,#4` @`0x5d74` (and `#0x8000` @`0x5d98`) |
  | `0x194` | `GPIO_SETDATAOUT` | **drive high** | `0x5cb8` |
  | `0x190` | `GPIO_CLEARDATAOUT` | **drive low** | `0x5ccc` |
* Lines and values:
  * **`GPIO1_2`** ← `*(u32*)(base+0x194) = 0x00000004` for `regen1` (`0x5cd8`), or
    `*(u32*)(base+0x190) = 0x00000004` to release it.
  * **`GPIO1_15`** ← `0x00008000` for `regen2` (`0x5cec`), same pair.
  * Any other id reaching `0x5c80` prints `"…Invalid regulator: %d"` and returns −1.
* It **never touches `GPIO_OE (0x134)`, `GPIO_DATAIN (0x138)`, edge detect (`0x148/0x14C`)
  or debounce (`0x150`)** — there is no `str` to any offset in `0x130..0x158` anywhere in
  the image. So whether the pin is even an output is somebody else's job
  (`startup-playbook`'s board GPIO table — trap 135/§3.22 marks `GPIO1` bits
  0/4/15/18/28/31 as outputs driven low, which includes **bit 15 but not bit 2**).
* **Our DTS: same line.** `kernel/source/omap4-winchester.dts:1240-1245`
  `gpio1-en-2-hog { gpios = <2 GPIO_ACTIVE_HIGH>; output-high; line-name = "regen1-gpio"; }`
  drives exactly `GPIO1_2`, i.e. the QNX `regen1` line — with the **opposite polarity from
  QNX's start-up** (QNX leaves it alone at driver start and only *disables* the PMIC half;
  our hog forces it high unconditionally, and `regulator-fixed`'s "loses the race with
  async probe" reasoning from trap 22 is why it is a hog).
* **`GPIO1_15` (QNX `regen2`) is modelled by nothing in our DTS** — not in the hog list
  (`gpio1-ref-low-0/18/28/31`, `gpio1-en-2/6/14`) and not in any `regulator-fixed`. Given
  §5.2 (display/panel), that is a *possible* panel-power gap, not a radio one.

---

## 8. Corrections to earlier reports

1. **Report 219 §4.1**: the table is at file **`0x8168`** (= VA `0x9168`), **20 × 32 B**
   (= 640 B, the `.dynsym` size of `regulator_info`), not `0x8170` / 19 × 32. The 19-name
   list is otherwise right, but `vcore3` (`0x7750`, base `0x5E`) is also in it, and `vpp`
   is record 7 @`0x9248` with name bytes at `0x76f4` (a 3-char string `strings` hides).
2. **Report 219 §4.1's "TWL6032-only names v1v29/v1v8/v2v1/vmem/vcore1/vcore2"** is wrong:
   `TWL6030_REG_VMEM/V2V1/V1V29/V1V8` (33–36) and `VDD1/2/3` (30–32) are TWL6030 resources
   (`twl.h:743-751`), and for every rail mainline actually registers (VANA…VUSIM, V1V8,
   V2V1) the QNX base equals mainline's `.base + 0x30` (§2.2).
3. **Report 219 §4.4's claim is confirmed** and now has a mechanism (§5.1), including the
   exact operands and the `Rev:`-gate; the earlier note "on DVT3+/MBID>=5" is right, with
   the addition that an *unparsable* `Rev:` also takes the GPIO path (`0x5f20`).
4. **Report 192 §7 / §3.21's "0x8C is CLK32KG"** reading is refuted twice over: the QNX
   table makes `0x8C` `vaux3` (record 3), and `clk-twl.c`'s `0x8C` is the TWL6032 table.
   The `0xBC`–`0xBE` "unidentified non-regulator resource" statement survives — nothing in
   this binary maps it (no record has base `0xBC`).
5. **New, and relevant to "can another service switch a rail?":** `write_config` (`0x553c`)
   and `map_prm` (`0x27f4`) are compiled in and **unreachable**; the exported
   `twl6030_read`/`-a` dump (`0x2988`) likewise has **no in-image caller**. Do not assume
   the dump's code path ever runs, and do not assume `set_vout` on `vcore1..3` is safe
   (§3.4).

---

## 9. Verdict: does this binary hide a WL1283 power gate?

**No. Plainly, no.**

What the file can and cannot do, exhaustively:

* **Rails it can affect:** all 20 rows of §1.2 — but the only *writes* it ever performs are (a) the four regen "off/detach" bytes at start-up and (b) whatever a client
  asks for through `/dev/pmic/regulator`. Of those, the boot configuration turns on
  **REGEN1/REGEN2 only**, and only for the window between `pmic-twl6030-ctl` and powerman.
* **No `VMMC`/`VAUX3`/`VCXIO`/`V1V8` enable anywhere.** `VMMC` is switched *off*
  (`set_state vmmc off app con mod`) and detached, `VAUX1/2/3` and `VUSB` are given
  `sleep=ams act=ams` transitions but no state change, `VPP`/`VUSIM` are turned off and
  detached, and `v1v29/v1v8/v2v1/vmem/vcore*` are never named by the Winchester cfg at all.
* **No clock resource, no PREQ unmask that gates a rail** (`unmask_preq 1` only unmasks the
  MSK_TRANSITION register, `0x48:0x20`, bits APP/CON/MOD — it does not enable anything).
* **No WL1283 line.** `WL_EN` is gpio 54 and `WL_IRQ` gpio 53 per trap 135/§3.22, i.e.
  **GPIO2_22 / GPIO2_21** — not `GPIO1_2`/`GPIO1_15`, and this binary only maps the GPIO1
  bank at `0x4A310000` (`0x5e58`). It never reads or writes `0x48055xxx`.
* **Against Linux:** `twl6030-regulator.c` + `twl6030.dtsi` cover every rail that exists in
  this table except the two REGENs (unrepresentable — `twl.h` has no such register define),
  and our DTS covers `GPIO1_2` with the `regen1-gpio` hog. **The only thing missing is
  `GPIO1_15` (regen2).** For the radio that is irrelevant; `&mmc5 { vmmc-supply = <&vwl1283>; }`
  points at a `regulator-fixed` stub with **no control input at all**
  (`omap4-winchester.dts:49-55`), and this binary never enables `VMMC` (base `0x98`) or any
  1.8 V rail at boot either.

So the difference between the two OSes on the radio's power path is **not** in this
program. If the WL1283's supply is gated, the gate is in `pmic_twl6030_cfg` (the pre-driver
writer, report 219 §3: `0x48:0x98/0x99/0x9A/0x9B` = VMMC GRP/TRANS/STATE/VOLT and
`0x48:0x84..0x8F` = VAUX1/2/3 — note it writes `0x98..0x9B` and `0x9A=0x21`!), in
`startup-playbook`'s GPIO table, or in the OMAP PRCM — not in
`pmic-winchester-twl6030`.

> Cross-lead for the next session (from *this* file, not proven): `pmic_twl6030_cfg` writes
> `0x48:0x9A ← 0x21` (VMMC **STATE** = APP-group ON, since `0x21 = 0x20|0x01`) at boot,
> before this resmgr runs — and this resmgr's cfg then writes `set_state vmmc off app con
> mod` (`0x9A ← 0xE0`). If `VMMC` is the SDIO rail, the reference's *net* state after
> `pmic-twl6030-ctl` is OFF, which would make VMMC a dead end; if `VMMC` is the eMMC rail,
> the `0x98..0x9B` block is the interesting one for a different reason. Either way the
> answer is in `pmic_twl6030_cfg`, not here.
