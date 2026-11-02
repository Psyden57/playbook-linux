# 106 — RE: `proc/boot/setup-core-inactive` (QNX/ARM, OMAP4430 "core inactive" one-time setup)

Static-only analysis. The target was **not executed** and no hardware was touched.
Method: `rizin 0.8.2` (`aa`, `pd`, `pdf`, `izz`) on the ELF, plus raw `python3` struct/word
decoding of the RX/RW segments, plus name resolution against the local OMAP4430 TRM text
(`bootdumps-2026-09-11/swpu231ap.pdf.txt`) and the kernel headers in
`playbook-dev/kernel-clean/arch/arm/mach-omap2/` (`prm44xx.h`, `cm44xx.h`, `vc44xx_data.c`,
`omap_twl.c`). `objdump` on this host is x86-only and cannot decode ARM, and `arm-linux-objdump`
is not installed, so rizin was the primary disassembler.

---

## 0. TL;DR — the VC bypass writes (priority #2)

`cpupm_init: prcm_vc_bypass_write 0x3a` / `0x40` are emitted by `fcn.0000d688(reg, slave, data)`,
which packs and writes **PRM_VC_VAL_BYPASS**.

| order | call | register (physical) | 32-bit value written |
|---|---|---|---|
| 1 | `0xd688(0x12, 0x56, 0x3A)` | **`0x4A307BA0`** = `PRM_VC_VAL_BYPASS` (`DEVICE_PRM` + 0xA0) | **`0x013A5612`** |
| 2 | `0xd688(0x12, 0x55, 0x40)` | **`0x4A307BA0`** = `PRM_VC_VAL_BYPASS` | **`0x01405512`** |

Value construction (verified instruction-by-instruction at `0x0d688`):

```
r1 = (arg2 << 24) >> 16          ; arg2 -> bits 15:8
r0 = arg1 & 0x7f                 ; arg1 -> bits 6:0
r1 = r0 | r1
r2 = (arg3 << 16) & 0x00ff0000 | 0x01000000   ; arg3 -> bits 23:16, VALID(bit24)=1
val = r2 | r1
*(volatile u32*)(prcm[0x4c0] + 0xa0) = val
while (*(volatile u32*)(prcm[0x4c0] + 0xa0) & 0x01000000) ;   /* wait VALID self-clear */
```

Bitfield layout is the TRM's (`swpu231ap.pdf.txt` Table 3-873, p.772):

```
PRM_VC_VAL_BYPASS   offset 0xA0   physical 0x4A30 7BA0   instance DEVICE_PRM
 31:25 RESERVED
   24  VALID     - write 1 to launch; HW clears it on SMPS ACK (or on error)
 23:16 DATA      - data sent to the Power IC
  15:8 REGADDR   - Power-IC register index
    7  RESERVED
   6:0 SLAVEADDR - I2C slave address
```

Decoded:

| # | SLAVEADDR | REGADDR | DATA | meaning |
|---|---|---|---|---|
| 1 | `0x12` | `0x56` | `0x3A` | slave `0x12` = `OMAP4_SRI2C_SLAVE_ADDR`; reg `0x56` = `OMAP4_VDD_MPU_SR_CMD_REG` (see `kernel-clean/.../omap_twl.c:31-34`) |
| 2 | `0x12` | `0x55` | `0x40` | reg `0x55` = `OMAP4_VDD_MPU_SR_VOLT_REG` |

So both bypass writes go to the **VDD_MPU SmartReflex registers of the TWL6030-class companion
PMIC at 7-bit I2C address 0x12**. Note: VDD_MPU, not VDD_CORE — despite the `Core voltage is
1225/1250` strings elsewhere in the image.

There is **no timeout** on the `VALID` polling loop (`0xd6c8..0xd6d8`). See §9.

Cross-check: `fcn.0000d92c(cfg, off)` builds `PRM_VC_VAL_CMD_VDD_{CORE_L,MPU_L,IVA_L}`
(ON[31:24]/ONLP[23:16]/RET[15:8]/OFF[7:0], TRM Table 3-871/872) — same base, offsets
`0x94/0x98/0x9c`.

---

## 1. Binary facts

| | |
|---|---|
| path | `playbook-dev/dumped4869ifs/proc/boot/setup-core-inactive` |
| type | ELF32 ARM EABI5, `DYN`, stripped, interpreter `/usr/lib/ldqnx.so.2` |
| size / build | 173 116 B, Build-ID `26a7697718d0d089ced3e7994a32e07b` |
| QNX_info | `NAME=setup-core-inactive` / `DESCRIPTION=one time setup to allow for core inactive mode` / `DATE=2011/07/27-13:10:37-EDT` / `HOST=gusbuild5` / `VERSION=6.5.0` / `TAGID=1784` |
| QNX_usage | `%C - one time set up for core inactve state` (sic) |
| entry | `0x68ec` (`entry0` → `__libc_start(main)`) |
| LOAD 1 (RX) | file `0x0` == vaddr `0x0`, filesz `0x1c2d4` (**all code + all literals + all rodata strings**) |
| LOAD 2 (RW) | file `0x1cf44` → vaddr `0x1df44`, filesz `0xc320`, memsz `0xd8e4` |
| `_GLOBAL_OFFSET_TABLE_` | **`0x25000`** (PLTGOT) |
| imports | libc.so.3 only: `printf, puts, fprintf, strcmp, strstr, memset, mmap, nanospin_ns, atexit, exit, confstr, __stack_chk_fail, __register_frame_info, __deregister_frame_info, __cxa_finalize, _init_array, _fini_array, _preinit_array, _init_libc` |
| **not** imported | `mmap_device_io`, `devctl`, `MsgSend`, `open` — the process relies on a **1:1 device mapping** (physical addresses are used directly as virtual addresses) |

### Function inventory (rizin `aa`/`afl`, 40 procedures, all stripped)

```
0x68ec entry0      0x69c8 entry.fini0  0x6a44 entry.init0
0x6abc PARTIAL-setup   0x6de4 cpupm_init (FULL/one-time)   0x72dc main
0x9a38 clkdm/clkstctrl set   0x9ff4 CM-module CLKCTRL set   0xb99c PRM DEVICE_PRM init
0xc794 / 0xccac / 0xce20 / 0xd688(vc_bypass_write) / 0xd92c(vc cmd val)
0xd998 (VC/VP channel programming)   0xd688 vc_bypass_write
0xf334 dmnstate table init   0x131c4  0x13a80 module disable  0x13f70 prcm_device_init
0x1482c  0x14f1c  0x15648 sr_interface_config_domain  0x15a7c vc_global_init
0x15b2c configure_volcon  0x15c80 prcm table init  0x16b4c __udivsi3
```

### Two critical data anchors

Reverse-engineering the PIC idiom `ldr rN,[pool] ; add rN,pc,rN` (note: rizin does **not**
resolve the literal for you — `add r4,pc,#0x1e20c` at `0x6dec` = `0x6df4 + 0x1e20c = 0x25000`):

* `GOT[0x230]` = `0x00025368` → the global **`prcm` descriptor block** (a `u32` array of
  physical/1:1 base addresses, `memset` to `0xFF` over `0x14d0` bytes by `fcn.00015c80`
  before being populated, then terminated with `0x4B000000` at `prcm+0x524`).
  A module's base is simply `*(u32*)(prcm + byte_offset)`.
* `GOT[0x100]` = `0x00022810` → **CM module table**, 28 entries × 56 bytes (`ids 0x00..0x1b`);
  `[entry+4]` = index `m` into the prcm array, `[entry+0x20]` = register offset;
  index `0xf12` is the "invalid" sentinel → return error `0xAA`.
* `GOT[0x164]` → clock-domain table (56-byte entries) used by `fcn.00009a38`.

Struct offsets that matter (all resolved against the in-image table and the TRM):

| `prcm+X` | physical | TRM instance name |
|---|---|---|
| `0x2e8` | `0x4A004100` | **CM1** (CKGEN_CM1) register window |
| `0x308` | `0x4A008100` | **CM2** (CKGEN_CM2) register window |
| `0x310` | `0x4A008700` | **CORE_CM2** |
| `0x478` | `0x4A306000` | `OCP_SOCKET_PRM` |
| `0x47c` | `0x4A306100` | `CKGEN_PRM` |
| `0x480` | `0x4A306300` | `MPU_PRM` |
| `0x494` | `0x4A306700` | `CORE_PRM` |
| `0x4b4` | `0x4A307700` | `WKUP_PRM` |
| `0x4c0` | **`0x4A307B00`** | **`DEVICE_PRM`** ← VC/VP block |
| `0x4cc` | `0x4A30A000` | **`SCRM`** |
| `0x230`/`0x234` | `0x48243400`/`0x48243800` | `CORTEXA9_CPU0` / `CORTEXA9_CPU1` |

> Caveat found while decoding: in the separate in-image *name* table (vaddr `0x23974`, 149 rows
> of `{phys, size, name_ptr, off}`) the 4th column is exactly `struct_offset + 4`, i.e. the
> name-table's `off` and the offsets the code uses differ by one slot. The addresses above were
> taken from what the **code actually loads** (`*(prcm+X)`), and every one of them was confirmed
> against the TRM (e.g. `PRM_VC_VAL_BYPASS` @`0x4A307BA0` = `DEVICE_PRM+0xA0`,
> `CM_SDMA_STATICDEP` @`0x4A008A04` = `CORE_CM2+0x304`).

---

## 2. `main()` @ `0x72dc`

```c
int main(int argc, char **argv) {
    int r = prcm_table_init();                 // fcn.00015c80
    if (r) { fprintf(stdout, "PRCM init failed: %d\n", r);  return 1; }   // str @0x16ed8
    cpupm_init();                              // fcn.00006de4  <-- always runs
    if (argc > 1 && !strcmp(argv[1], "full")) {                           // str @0x16ef0 = "full"
        puts("FULL core inactive setup");      // str @0x16ef8
        fcn.00006abc();                        // per-module teardown (60 modules)
    } else {
        puts("PARTIAL core inactive setup");   // str @0x16f14
        cm_module_set(6, 2, 0);                // fcn.00009ff4  L3INIT_CM2 CLKTRCTRL = 2
        cm_module_set(7, 2, 0);                // fcn.00009ff4  L4PER_CM2  CLKTRCTRL = 2
    }
    *(u32*)0x48243400 = 0x00030107;            // CORTEXA9_CPU0
    *(u32*)0x48243800 = 0x00030107;            // CORTEXA9_CPU1
    return 0;
}
```

So the mode selector is the **literal argument string `"full"`** (`0x16ef0`); anything else
(including no argument) is the partial path. `cpupm_init()` is common to both modes.

---

## 3. Ordered sequence — `cpupm_init` / `fcn.00006de4` (runs in **both** modes)

`r3` = `*(GOT+0x230)` = `prcm`. All the `bic ... ,0x100` ops are read-modify-writes.

### Phase A — clear `DPLL_CLKOUT_GATE_CTRL` (bit 8) on every DPLL output divider
TRM (Table 3-981): `CM_DIV_M*_DPLL_*`: `[4:0] DPLL_CLKOUT_DIV`, bit 5 `DIVCHACK`,
bit 8 **`DPLL_CLKOUT_GATE_CTRL`** ("0 = automatically gate this clock when no dependency,
1 = force enabled"), bit 9 `ST_DPLL_CLKOUT`. Clearing bit 8 = **enable automatic DPLL CLKOUT
gating** — the core prerequisite for a domain to go inactive.

| # | address | register | op |
|---|---|---|---|
| 1 | `0x4A004170` | `CM_DIV_M2_DPLL_MPU` | `&= ~0x100` |
| 2 | `0x4A004130` | `CM_DIV_M2_DPLL_CORE` | `&= ~0x100` |
| 3 | `0x4A004138` | `CM_DIV_M4_DPLL_CORE` | `&= ~0x100` |
| 4 | `0x4A00413C` | `CM_DIV_M5_DPLL_CORE` | `&= ~0x100` |
| 5 | `0x4A004140` | `CM_DIV_M6_DPLL_CORE` | `&= ~0x100` |
| 6 | `0x4A004144` | `CM_DIV_M7_DPLL_CORE` | `&= ~0x100` |
| 7 | `0x4A0041B8` | `CM_DIV_M4_DPLL_IVA` | `&= ~0x100` |
| 8 | `0x4A0041BC` | `CM_DIV_M5_DPLL_IVA` | `&= ~0x100` |
| 9 | `0x4A0041F0` | `CM_DIV_M2_DPLL_ABE` | `&= ~0x100` |
| 10 | `0x4A0041F0` | `CM_DIV_M2_DPLL_ABE` | `&= ~0x400` (bit 10; TRM marks bits 31:10 RESERVED) |
| 11 | `0x4A0041F4` | `CM_DIV_M3_DPLL_ABE` | `&= ~0x100` |
| 12 | `0x4A008150` | `CM_DIV_M2_DPLL_PER` | `&= ~0x100` |
| 13 | `0x4A008150` | `CM_DIV_M2_DPLL_PER` | `&= ~0x400` |
| 14 | `0x4A008158` | `CM_DIV_M4_DPLL_PER` | `&= ~0x100` |
| 15 | `0x4A008160` | `CM_DIV_M6_DPLL_PER` | `&= ~0x100` |
| 16 | `0x4A008164` | `CM_DIV_M7_DPLL_PER` | `&= ~0x100` |
| 17 | `0x4A008190` | `CM_DIV_M2_DPLL_USB` | `&= ~0x100` |
| 18 | `0x4A0081B4` | `CM_CLKDCOLDO_DPLL_USB` | `&= ~0x100` |

### Phase B — `fcn.00009ff4(module_id, target_state, 0)`
Writes the 2-bit `[1:0]` field of the CM instance's leading register and then polls status bit 8
(up to 10 iterations, error `0x97` on timeout). `target_state` = `1`/`2`/`3` matches CLKTRCTRL
encoding (`1=SW_SLEEP, 2=SW_WKUP, 3=HW_AUTO`).

| # | call | module | address | field |
|---|---|---|---|---|
| 19 | `cm_module_set(5, 2, 0)` | CAM_CM2 | `0x4A009000` | `[1:0] = 2` |
| 20 | `cm_module_set(4, 2, 0)` | DSS_CM2 | `0x4A009100` | `[1:0] = 2` |
| 21 | `cm_module_set(8, 3, 0)` | SGX_CM2 | `0x4A009200` | `[1:0] = 3` |
| 22 | `cm_module_set(1, 3, 0)` | DSP_CM1 | `0x4A004400` | `[1:0] = 3` |
| 23 | `cm_module_set(3, 3, 0)` | ABE_CM1 | `0x4A004500` | `[1:0] = 3` |
| 24 | `cm_module_set(2, 3, 0)` | IVAHD_CM2 | `0x4A008F00` | `[1:0] = 3` |

(Full module-id → address map derived from `GOT[0x100]` is in §6.)

### Phase C — `prcm_vc_global_init(0x3, 1)` = `fcn.00015a7c(3,1)`
Pure **software** step (no MMIO): switch on channel 3 → `voltdm->on = 1`, `voltdm->onlp = 1`,
then `voltdm->[0x28] = 1` (arg2), `voltdm->[0x18] = 0`, `voltdm->[0x1c] = 0`, returns 0.
On error: `"cpupm_init: prcm_vc_global_init(0x%x,1) returned %d\n"` (`0x16d68`) and return 5.
Channels: `0 → (0,1)`, `1 → (1,0)`, `2 → (0,0)`, `3 → (1,1)` for the `(on, onlp)` pair.

### Phase D — `prcm_configure_volcon()` = `fcn.00015b2c` → `fcn.0000d998`
Copies three globals into `voltdm->[0x18]/[0x1c]/[0x14]`, then `fcn.0000d998(voltdm)` programs
the **VC** block at `DEVICE_PRM` (`0x4A307B00`), one channel per iteration, shifting the field
by 8 bits per channel (channel 0 → bits 7:0, channel 1 → 15:8, channel 2 → 23:16):

| register | address | fields |
|---|---|---|
| `PRM_VC_SMPS_SA` | `0x4A307B88` | `[6:0]` and `[14:8]`, `[22:16]` = cfg `[0x10]` |
| `PRM_VC_VAL_SMPS_RA_VOL` | `0x4A307B8C` | 8-bit slices = cfg `[0x14]` |
| `PRM_VC_VAL_SMPS_RA_CMD` | `0x4A307B90` | 8-bit slices = cfg `[0x18]` |
| `PRM_VC_CFG_CHANNEL` | `0x4A307BA4` | `[0]`=cfg`[0x1c]`,`[1]`=cfg`[0x20]`,`[2]`=cfg`[0x24]`,`[3]`=cfg`[0x2c]`,`[4]`=cfg`[0x28]` (shifted per channel) |
| `PRM_VC_CFG_I2C_MODE` | `0x4A307BA8` | `[2:0]`,`[3]`,`[4]`,`[6]` from cfg |
| `PRM_VC_CFG_I2C_CLK` | `0x4A307BAC` | composed from cfg `[4]`,`[8]`,`[0xc]`,`[0x10]` |

plus `fcn.0000d92c(cfg, 0x94/0x98/0x9c)` for the three `PRM_VC_VAL_CMD_VDD_{CORE,MPU,IVA}_L`
registers at `0x4A307B94 / 0x4A307B98 / 0x4A307B9C`
(`ON[31:24]`, `ONLP[23:16]`, `RET[15:8]`, `OFF[7:0]`, read-modify-write preserving `[15:0]`'s low
half). Error string: `"cpupm_init: prcm_configure_volcon() returned %d\n"` (`0x16da0`).

### Phase E — `prcm_device_init()` = `fcn.00013f70` → `fcn.0000b99c(cfg)`
Programs `DEVICE_PRM` (`0x4A307B00`, TRM: "256 bytes"): `+0x00 = PRM_RSTCTRL`,
`+0xE4 = PRM_DEVICE_OFF_CTRL`). Writes observed, values composed from the config struct:

| address | value source |
|---|---|
| `0x4A307B08` | `cfg[0xb4] | (cfg[0xb8] << 10)` |
| `0x4A307B0C` | `cfg[0xb0]` |
| `0x4A307B10` | `cfg[0x8c]<<14 | cfg[0x90]<<13 | cfg[0xa8] | cfg[0x94]<<12 | cfg[0x98]<<9 | cfg[0x9c]<<8 | cfg[0xa0]<<4 | cfg[0xa4]<<2` |
| `0x4A307B14` | `cfg[0xac]` |
| `0x4A307B18`, `0x4A307B1C`, `0x4A307B20`, `0x4A307B24`, … | further `cfg[...]` fields |
> Error string: `"cpupm_init: prcm_device_init() returned %d\n"` (`0x16dd4`).
> **Unresolved:** the numeric values depend on a config structure built by `fcn.00015c80`
> (`prcm` table init, 0x14d0-byte block); extracting that would need another pass.

### Phase F — `prcm_reinit_dmnstates()` = `fcn.0000f334`
**Software only** — re-initialises a 0x70-byte state table (not MMIO):
`{1,0,0,0,0,0,2,2,0,1,1,0,0,0,1,0,0,0,1,0,1,1,0,0,0,0,0,0}` at word offsets
`0x00,0x04,0x08,0x0c,0x10,0x14,0x18,0x1c,0x20,0x24,0x28,0x2c,0x30,0x34,
0x3c,0x40,0x44,0x4c,0x38,0x50,0x58,0x5c,0x48,0x54,0x60,0x64,0x68,0x6c`.
Error string `"cpupm_init: prcm_reinit_dmnstates() returned %d\n"` (`0x16e00`).

### Phase G — `prcm_sr_interface_config_domain(0, 2)` = `fcn.00015648(0,2)`
```
if (sr_enabled[d] != 1) fcn.0001482c(...);   /* d = 0 */
sr_enabled[0] = 0;
fcn.00014f1c(0, 2);
if (already_configured) return;
fcn.0000d998(2);              /* re-program VC channel 2 with arg 2 */
sr_enabled[0] = 1;  already_configured = 1;
```
Error string: `"cpupm_init: prcm_sr_interface_config_domain() returned %d\n"` (`0x16e34`).

### Phase H — arm the "VC configured" flag
`*(u32*)(GOT-slot) = 1` (`0x7158`).

### Phase I / J — the two **PRM_VC_VAL_BYPASS** writes  ← **the priority-2 answer**

```
19.  0x4A307BA0  <-  0x013A5612    (VALID=1, DATA=0x3A, REGADDR=0x56, SLAVEADDR=0x12)
     while (0x4A307BA0 & 0x01000000) ;      /* unbounded spin, no timeout */
20.  0x4A307BA0  <-  0x01405512    (VALID=1, DATA=0x40, REGADDR=0x55, SLAVEADDR=0x12)
     while (0x4A307BA0 & 0x01000000) ;
```
Error strings: `"cpupm_init: prcm_vc_bypass_write 0x3a returned %d\n"` (`0x16e70`) and
`"... 0x40 ..."` (`0x16ea4`). Note the strings are hard-coded (`0x3a`/`0x40` are the *DATA*
byte, not the register address) — the two calls differ only in `REGADDR`/`DATA`.

### Phase K — power-state poke + static-dependency clear

| # | address | register | value |
|---|---|---|---|
| K1 | `0x4A306304` | `PM_MPU_PWRSTST` (MPU_PRM+4, TRM: **R**/status) | `0x03000000` (bits 24,25) |
| K2 | `0x48243404` | `CORTEXA9_CPU0`+4 | `0x03000000` |
| K3 | `0x4A30A320` | `SCRM`+0x320 | `&= ~0x100` |
| K4 | `0x4A008A04` | `CM_SDMA_STATICDEP` (CORE_CM2+0x304) | `= 0` |

### Phase L — clock-domain sleep, then re-target
`fcn.00009a38(clkdm_id, state)` writes `[2:0] = state` into the clkdm `CLKSTCTRL`
(CM2 window `0x4A008100` for ids 0/1/6, CM1 window `0x4A004100` for ids 2/3/4/5;
offset from the table at `GOT[0x164] + id*56 + 0x10`).

| # | call | field |
|---|---|---|
| L1 | `clkdm_set(1, 1)` | `1` (SW_SLEEP) |
| L2 | `clkdm_set(6, 1)` | `1` |
| L3 | `clkdm_set(2, 1)` | `1` |
| L4 | `clkdm_set(4, 1)` | `1` |
| L5 | `fcn.0000c794(1, 0)`, `fcn.0000c794(2, 0)` | (domain-specific helper) |
| L6 | `cm_module_set(1, 3, 0)` | DSP_CM1 `0x4A004400` `[1:0]=3` |
| L7 | `cm_module_set(3, 3, 0)` | ABE_CM1 `0x4A004500` `[1:0]=3` |
| L8 | `cm_module_set(2, 3, 0)` | IVAHD_CM2 `0x4A008F00` `[1:0]=3` |
| L9 | `return 0` | |

---

## 4. FULL vs PARTIAL

| | FULL (`argv[1] == "full"`) | PARTIAL (default) |
|---|---|---|
| common | `prcm_table_init`, `cpupm_init()` (all of §3) | same |
| extra | `puts("FULL core inactive setup")` + **`fcn.00006abc()`** | `puts("PARTIAL core inactive setup")` + `cm_module_set(6,2,0)` (`0x4A009300` L3INIT_CM2) + `cm_module_set(7,2,0)` (`0x4A009400` L4PER_CM2) |
| tail | `*(0x48243400)=0x030107`, `*(0x48243800)=0x030107` | same |

`fcn.00006abc()` (the FULL-only extra) is a **mass per-module teardown**: 58 calls to
`fcn.00013a80(module_id)` (`{push; fcn_000081d8(id,0); if state!=1 {special-case 8 and 0x12};
fcn_00007e80(id,0);}` = "disable/idle module"), in this order:

```
3, 1, 2                                        (MPU_CM1, DSP_CM1, IVAHD_CM2)
0xCE20(8); 0xCCAC(8); 0xCE20(2); 0xCCAC(2)     (child-clock setup/teardown for 8 and 2)
0x39,0x35,0x3A,0x36,0x37,0x38,0x3B,0x2F,0x2C,0x2D,0x2E,0x27,0x28
0x131C4(0x28)
*(u32*)(prcm[0x1a0] + 0x110) |= 2
0x28
cm_module_set(7,2,0)                           (L4PER_CM2)
0x29,0x2A,0x2B,0x3C,0x3D,0x31,0x32,0x34,0x3E
0x16,0x17,0x20,0x1B,0x1A,0x11
8, 4, 0xE, 5, 6, 7, 9, 0xA, 0xB, 0xC, 0xD, 0xF
*(u32*)(prcm[0x308] + 0x54) &= ~0x100          /* 0x4A008154 */
0x16, 0x131C4(0x16)
*(u32*)(prcm[0x1b0] + 0x10) |= 1               /* 0x4809C010 */
0x16, 0x17, 0x20
0x1B, 0x131C4(0x1B)
*(u32*)(prcm[0x364] + 0x10) |= 1               /* 0x4A064010 */
*(u32*)(prcm[0x364] + 0x10) &= ~0xC            /* 0x4A064010 */
0x1B, 0x1A, 0x131C4(0x1A)
*(u32*)(prcm[0x37c] + 0x210) |= 2              /* 0x4A0A9210 */
*(u32*)(prcm[0x37c] + 0x210) &= ~0xC           /* 0x4A0A9210 */
0x1A, 0x1C, 0x131C4(0x1C)
*(u32*)(prcm[0x384] + 0x404) |= 2              /* 0x4A0AB404 */
0x1C, 0x1F, 0x1E
cm_module_set(6, 2, 0)                         /* L3INIT_CM2 -> 2 */
*(u32*)(prcm[0x2dc] + 0x300) |= 1              /* 0x4A002300 */
return 0;
```

The `|= 1 / |= 2 / &= ~0xC` RMWs above are **L3/L4 interconnect** target/initiator
idle/standby-control registers (they sit in the 0x4809_xxxx L4_CFG/L4_PER windows and the
0x4A00_2000 / 0x4A06_4000 / 0x4A0A_9xxx L3-target windows), i.e. the "let the interconnect go
idle" half of the core-inactive setup. `fcn.000131c4(id)` is a companion helper invoked with the
same module id immediately before those RMWs.

Interpretation: **FULL** additionally *idles/disabled ~58 individual CM modules* (MPU, DSP,
IVAHD, DSS, CAM, SGX, USB, L4PER, CORE sub-modules, GPIO/timer blocks…) so that every clock in
CORE/MPU/PER can be auto-gated, then applies `CLKTRCTRL = 2` on L3INIT/L4PER. **PARTIAL** skips
all of that and only forces `SW_WKUP` on L3INIT_CM2 and L4PER_CM2.

---

## 5. DPLL/OPP strings and SmartReflex

* The `dpll_*_cfg_opp*` strings (`0x1789..0x2186`) and `prcm_power_domain`,
  `prcm_clk_domain`, `prcm_dpll`, `prcm_vc_configured`, `prcm_hal_sr_init_executed`,
  `prcm_sr_voltage_opp_defs`, `prcm_sr_smart_reflex_enabled`, `prcm_sr_accum_nand_calibration`,
  `prcm_sr_error_in_sr_irq`, `disable_done_SRx`, `prcm_sysclk_value`,
  `prcm_slimbus_source_clock_value`, … are **keys of a configuration/parameter dictionary**
  consumed by `fcn.00015c80` (`prcm_table_init`) — they select DPLL/OPP settings from a table
  and populate the `prcm` block. They are **not** executed branch targets in this binary; no
  DPLL M/N programming is done by `setup-core-inactive` itself (only the `DPLL_CLKOUT_GATE_CTRL`
  clears of Phase A).
* SmartReflex: the only SR-relevant MMIO in this image is
  (a) the `sr_interface_config_domain` call chain (`fcn.00015648` → `fcn.0001482c`,
  `fcn.00014f1c`, `fcn.0000d998`) which configures the **VC** side, and
  (b) the two `PRM_VC_VAL_BYPASS` writes to the **TWL6030 VDD_MPU SR** registers.
  No `PRM_VP_*` MMIO and no SR interrupt-enable writes were found on this path.
* `"Core voltage is 1225"` / `"1250"` / `"0"` / `"Core voltage is %u\n"` (`0x1c0e0..0x1c124`)
  are **not touched by `main()`** — they belong to a voltage-reporting helper and are dead code
  in the boot path.

---

## 6. CM module table (module id → register) — from `GOT[0x100] = 0x22810`

`reg = *(u32*)(prcm + 4*[entry+4]) + [entry+0x20]`

| id | addr | TRM instance | id | addr | TRM instance |
|---|---|---|---|---|---|
| 0 | `0x4A004300` | MPU_CM1 | 0xE | `0x4A008900` | `CM_MPU_M3_CLKSTCTRL` (CORE_CM2+0x200) |
| 1 | `0x4A004400` | DSP_CM1 | 0xF | — | invalid (`0xf12`) |
| 2 | `0x4A008F00` | IVAHD_CM2 | 0x10 | `0x4A009600` | (L4PER/USB area) |
| 3 | `0x4A004500` | ABE_CM1 | 0x11 | `0x4A009580` | L4PER_CM2+0x180 |
| 4 | `0x4A009100` | DSS_CM2 | 0x12 | `0x4A008C00` | CORE_CM2+0x500 |
| 5 | `0x4A009000` | CAM_CM2 | 0x13 | `0x4A008700` | CORE_CM2 |
| 6 | `0x4A009300` | L3INIT_CM2 | 0x14 | `0x4A008800` | CORE_CM2+0x100 |
| 7 | `0x4A009400` | L4PER_CM2 | 0x15 | `0x4A008A00` | CORE_CM2+0x300 |
| 8 | `0x4A009200` | SGX_CM2 | 0x16 | `0x4A008B00` | CORE_CM2+0x400 |
| 9 | `0x4A307800` | WKUP_CM | 0x17 | `0x4A008E00` | CORE_CM2+0x700 |
| A | `0x4A307A00` | EMU_CM | 0x18 | `0x4A008D00` | CORE_CM2+0x600 |
| B,C,D | `0x4A008600` | ALWAYS_ON_CM2 | 0x19-0x1B | — | invalid |

---

## 7. Complete PRM / CM / SCRM address list touched by this binary

| address | TRM name | written value / op |
|---|---|---|
| `0x4A004130` | CM_DIV_M2_DPLL_CORE | `&= ~0x100` |
| `0x4A004138` | CM_DIV_M4_DPLL_CORE | `&= ~0x100` |
| `0x4A00413C` | CM_DIV_M5_DPLL_CORE | `&= ~0x100` |
| `0x4A004140` | CM_DIV_M6_DPLL_CORE | `&= ~0x100` |
| `0x4A004144` | CM_DIV_M7_DPLL_CORE | `&= ~0x100` |
| `0x4A004170` | CM_DIV_M2_DPLL_MPU | `&= ~0x100` |
| `0x4A0041B8` | CM_DIV_M4_DPLL_IVA | `&= ~0x100` |
| `0x4A0041BC` | CM_DIV_M5_DPLL_IVA | `&= ~0x100` |
| `0x4A0041F0` | CM_DIV_M2_DPLL_ABE | `&= ~0x100`, then `&= ~0x400` |
| `0x4A0041F4` | CM_DIV_M3_DPLL_ABE | `&= ~0x100` |
| `0x4A004400` | DSP_CM1 | `[1:0] = 3` (HW_AUTO) |
| `0x4A004500` | ABE_CM1 | `[1:0] = 3` |
| `0x4A008150` | CM_DIV_M2_DPLL_PER | `&= ~0x100`, then `&= ~0x400` |
| `0x4A008158` | CM_DIV_M4_DPLL_PER | `&= ~0x100` |
| `0x4A008160` | CM_DIV_M6_DPLL_PER | `&= ~0x100` |
| `0x4A008164` | CM_DIV_M7_DPLL_PER | `&= ~0x100` |
| `0x4A008190` | CM_DIV_M2_DPLL_USB | `&= ~0x100` |
| `0x4A0081B4` | CM_CLKDCOLDO_DPLL_USB | `&= ~0x100` |
| `0x4A008F00` | IVAHD_CM2 | `[1:0] = 3` |
| `0x4A009000` | CAM_CM2 | `[1:0] = 2` |
| `0x4A009100` | DSS_CM2 | `[1:0] = 2` |
| `0x4A009200` | SGX_CM2 | `[1:0] = 3` |
| `0x4A009300` | L3INIT_CM2 | `[1:0] = 2` (partial mode / full-module path) |
| `0x4A009400` | L4PER_CM2 | `[1:0] = 2` (partial mode) |
| `0x4A008A04` | CM_SDMA_STATICDEP | `= 0` |
| `0x4A306304` | PM_MPU_PWRSTST (MPU_PRM+4) | `= 0x03000000` |
| `0x4A307B08..0x4A307B24+` | DEVICE_PRM config regs | see Phase E |
| `0x4A307B88` | PRM_VC_SMPS_SA | per-channel slave addr |
| `0x4A307B8C` | PRM_VC_VAL_SMPS_RA_VOL | per-channel |
| `0x4A307B90` | PRM_VC_VAL_SMPS_RA_CMD | per-channel |
| `0x4A307B94` | PRM_VC_VAL_CMD_VDD_CORE_L | ON/ONLP/RET/OFF |
| `0x4A307B98` | PRM_VC_VAL_CMD_VDD_MPU_L | ON/ONLP/RET/OFF |
| `0x4A307B9C` | PRM_VC_VAL_CMD_VDD_IVA_L | ON/ONLP/RET/OFF |
| **`0x4A307BA0`** | **PRM_VC_VAL_BYPASS** | **`0x013A5612`, then `0x01405512`** |
| `0x4A307BA4` | PRM_VC_CFG_CHANNEL | channel cfg |
| `0x4A307BA8` | PRM_VC_CFG_I2C_MODE | MCODE/SREN/HSEN |
| `0x4A307BAC` | PRM_VC_CFG_I2C_CLK | SCL/HS timings + slave addr |
| `0x4A30A320` | SCRM+0x320 | `&= ~0x100` |
| `0x48243400` | CORTEXA9_CPU0 | `= 0x00030107` |
| `0x48243404` | CORTEXA9_CPU0+4 | `= 0x03000000` |
| `0x48243800` | CORTEXA9_CPU1 | `= 0x00030107` |

---

## 8. Helper semantics (for re-use)

* **`fcn.0000d688` = `prcm_vc_bypass_write(regaddr, slaveaddr, data)`** — see §0. Polls
  `VALID` with `tst/bne`, **no timeout**.
* **`fcn.0000d92c(cfg, reg_off)` = VC command-value writer** for `PRM_VC_VAL_CMD_VDD_*_L`.
* **`fcn.0000d998(channel/voltdm)` = VC/VP channel programming** (Phase D).
* **`fcn.00009ff4(module_id, state, poll)`** = CM module `[1:0]` field + status-bit-8 poll
  (bounded, 10 iterations, error `0x97`). Valid ids `0..0x1b`; `0xf12` sentinel → error `0xAA`.
* **`fcn.00009a38(clkdm_id, state)`** = clkdm `CLKSTCTRL[2:0] = state`, no polling.
* **`fcn.00013a80(module_id)`** = "module → disabled/idle": `fcn_000081d8(id,0)`, special cases
  for id `8` (`CM1+0x08 &= ~0x100`) and id `0x12` (poll `fcn_0000845c` until `3`), then
  `fcn_00007e80(id, 0)`.
* **`fcn.00015648(dom, arg)` = `prcm_sr_interface_config_domain`**, **`fcn.00015a7c` =
  `prcm_vc_global_init`**, **`fcn.00015b2c` = `prcm_configure_volcon`**,
  **`fcn.00013f70` = `prcm_device_init`**, **`fcn.0000f334` = `prcm_reinit_dmnstates`**,
  **`fcn.00015c80` = `prcm_table_init`** (memsets 0x14d0 B to `0xFF`, parses the
  `dpll_*/prcm_*` parameter dictionary, builds the `prcm` block; `main` aborts with
  `"PRCM init failed: %d"` if it fails).

---

## 9. What could halt a non-secure CPU at a fixed ~1.4 s

Ranked hypotheses from this image:

1. **Unbounded `PRM_VC_VAL_BYPASS` VALID poll (best candidate).**
   `fcn.0000d688` writes `0x013A5612` to `0x4A307BA0` and then spins
   `ldr r3,[0x4A307BA0]; tst r3,#0x01000000; bne` with **no timeout and no interrupts
   disabled/waits**. `VALID` is only cleared by hardware after the VC's I²C/SR transaction to
   the PMIC at slave `0x12` completes (TRM 3-873). If the I²C/vc path is not functional — PMIC
   not present/held in reset, SR clock or L4_WKUP clock gated, or the transaction NACKs — the CPU
   spins here **forever**. This is a hard, deterministic stall at a fixed point in the boot, one
   I²C-attempt (~ms) after `cpupm_init` starts. It is also the *last* MMIO touch of the PMIC in
   the sequence, so it is a natural "stops at ~1.4 s" tripwire.
2. **`REGADDR = 0x56` / `0x55` on slave `0x12`.** These are the TWL6030 **VDD_MPU SmartReflex**
   registers. If the companion chip is a different PMIC (or the SR register block is absent),
   the write NACKs → `VALID` may still clear on error, but a following SR interaction can leave
   the MPU domain waiting.
3. **`*(u32*)0x48243400 = 0x00030107` and `*(u32*)0x48243800 = 0x00030107`** (last action of
   `main`). `0x48243400/0x48243800` are the `CORTEXA9_CPU0/CPU1` private blocks. Those A9
   private registers (SCU/GIC) are protected by the L4 firewall / secure-world ownership on
   OMAP4430; a **non-secure** write there raises a firewall error → external abort, which on a
   QNX/ARM startup can present as a silent hang. Timing is right after the whole PMIC sequence.
4. **`PM_MPU_PWRSTST` write (`0x4A306304 = 0x03000000`)** — TRM marks this register **read-only**;
   writing it is at best ignored, at worst a firewall/slave error if secure-gated.
5. **`CM_SDMA_STATICDEP = 0` (`0x4A008A04`)** — a static-dependency *enable* for SDMA on the
   CORE domain; combined with Phase A/B and `CLKTRCTRL = SW_SLEEP` (Phase L), a still-running
   SDMA/DMA master without an idle handshake can hold CORE out of the target state (this stalls
   *hardware*, not the CPU, so it would not spin — low probability for a CPU stall).
6. Not candidates: `fcn.00009ff4`'s status polls are bounded (10 tries); `fcn.00006de4`'s
   Phase A is pure RMW; Phases C/F/G/H are software-only.

---

## 10. Residual unknowns / next steps

* Values written in Phase E (`DEVICE_PRM+0x08..0x24+`) are derived from the config struct built
  by `fcn.00015c80`; extracting the dictionary values (`prcm_*` keys) would pin them exactly.
* **`fcn.0000d998` calling convention.** It starts `subs r4, r0, 0 ; moveq r0, #0x7d ; bne ...` —
  i.e. `arg1 == 0` is an error and otherwise `arg1` is **dereferenced as a struct**:
  `r5 = *(u32*)(arg1 + 0x14)` is the active channel descriptor, and `[arg1]`, `[arg1+8]`,
  `[arg1+0xc]`, `[arg1+0x10]` supply `PRM_VC_CFG_I2C_MODE`/`_CLK`. `prcm_configure_volcon`
  passes a valid pointer, but **`prcm_sr_interface_config_domain(0,2)` passes the literal `2`**
  (`0x156c0: mov r0,r7`) — under this convention that is a kernel-space pointer `2`, so the
  first `ldr r5,[r4,0x14]` reads address `0x16`. Either it is a latent bug in this build, or
  `d998` has a second entry convention that was not resolved. It is a potential **data-abort /
  hang** point, but it is reached only if the "already configured" flag is still 0 at that
  moment, so it is a weaker stall candidate than §9.1.
* `fcn.00006abc` (FULL-only) is now reconstructed end-to-end (§4).
* The `prcm` block is `memset` to `0xFF` then filled at runtime by `fcn.00015c80`; the static
  image content was used here and cross-validated against the TRM, but a runtime dump of
  `prcm[0x4c0]` would be the definitive confirmation of `0x4A307B00`.
* `GOT[0x164]` (clock-domain table) offsets were not enumerated; the `fcn.00009a38` target
  registers are therefore given as "clkdm `CLKSTCTRL` in CM1/CM2" rather than by exact name.
* The exact `[1:0]` register at each CM instance base written by `fcn.00009ff4` was not named
  from the TRM (the instance bases are certain — MPU_CM1 `0x4A004300`, DSP_CM1 `0x4A004400`,
  ABE_CM1 `0x4A004500`, IVAHD_CM2 `0x4A008F00`, CAM_CM2 `0x4A009000`, DSS_CM2 `0x4A009100`,
  SGX_CM2 `0x4A009200`, L3INIT_CM2 `0x4A009300`, L4PER_CM2 `0x4A009400`, WKUP_CM `0x4A307800`,
  EMU_CM `0x4A307A00`); the poll on **bit 8** after the write is likewise unnamed
  (`fcn.00009a38` writes bits `[2:0]` of the clkdm `CLKSTCTRL` with no poll).
