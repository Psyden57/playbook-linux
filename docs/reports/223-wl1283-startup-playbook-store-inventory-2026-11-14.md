# 223 — `startup-playbook`: the complete MMIO store inventory

**Date 2026-11-14.**
Status: **static, file-only** — no bench, no USB, no network, nothing under `tools/` executed.
This is the completion/correction pass over reports **192** (GPIO table, WDT) and **219 §Q1**
(partial store list).  Everything below is re-derived from the image; where 192/219 are wrong
the correction is stated with the VA that proves it.

---

## 0. Headline

* Image: `local/work/1917/ifs1917.imagefs` → `local/work/s74/startup-playbook.bin`,
  `0x44008` B @ imagefs `0x908`, sha256 `bc7d3a414fb1c9f67bdaf71349792af2ee22f1beba7c8691ae8daa8650a4ecda`
  (matches 192). Raw ARM, base `0x80100000`.
* `objdump` on this host has **no ARM backend**, so the disassembly is capstone 5.0.7
  (`startup-playbook.dis`, 68 722 instructions, 69 634 lines; every word kept, invalid words as
  `.word`).
* **549 stores resolve to a peripheral address** (340 distinct targets); **193 of them fall in the
  two requested windows** — 182 in `0x4A000000-0x4A31FFFF`, 11 in `0x48000000-0x480DFFFF`.
  **42 distinct peripheral constants** are reconstructed from `movw/movt` pairs and literal pools.
* **Q1 — yes.** `CM_L4PER_MMCSD5_CLKCTRL (0x4A009560) = 2` at `0x80101764` (219 correct), inside a
  full L4PER_CM2 sweep.  **New:** the image's own I²C driver also writes
  `CM_L4PER_I2C1..4_CLKCTRL = 2` and `= 0` at run time (`0x80114740`, `0x801146cc`).
* **Q2 — no.** Nothing — no store, no constant, no literal word — targets
  `0x4A307000-0x4A3072FF`, `0x4A307300-0x4A3073FF` or `0x4A307400-0x4A3075FF`.
  `RM_L4PER_MMCSD5_CONTEXT (0x4A307564)` is never written by this image (TRM Table 3-599).
* **Q3 — partly.** No direct padconf store exists at all; the pad walker writes
  `0x4A100000`/`0x4A31E000` + a table-supplied offset.  Of the asked addresses, `0x4A100050`,
  `0x4A100074`, `0x4A100148`, `0x4A10014C`, `0x4A100150` are reachable; `0x4A100058`,
  `0x4A100078`, `0x4A10007A`, `0x4A10007C`, `0x4A10014A/14E/152` and `0x4A31E058` are **not**.
* **Q4 — SYSCONFIG only, directly.** `0x4A310010` and `0x48055010/57010/59010/5B010/5D010`
  are read-modify-written with `|= 1`.  OE/DATAOUT/detect/debounce are written **only** by the
  table walker, bit = `1 << (id & 31)`, values from the record.
* **Q5 — I²C1 and I²C4 only.** Five slave-selected transactions on `0x48070000` and one on
  `0x48350000`; full slave/register/value list in §8.  I²C2 (`0x48072000`) and I²C3 (`0x48060000`)
  are *named* (CM sweep and the base→CLKCTRL compare) but never opened.
* **Q6 — yes for DPLL_MPU and the PRM voltage controller.** `CM_CLKMODE_DPLL_MPU (0x4A004160)`
  is put into idle-bypass (`0x5`) and MN-bypass (`0x4`) and then locked (`0x7`);
  `CM_CLKSEL_DPLL_MPU (0x4A00416C)` is programmed; `CM_DIV_M3_DPLL_CORE (0x4A004134)` is touched.
  `PRM_VC_*` at `0x4A307BA0-0xBF8` is written; **no `PRM_VP_*` register** is written at all.
* **Q7 — the table is 75 records at `0x80122750`**, decoded completely in §9; the record layout is
  read out of the walker at `0x8010cb9c` and the direction polarity is settled by TRM §25.4.8
  (`GPIO_OE` bit **0** = output) plus the live `GPIO1_OE/GPIO2_OE` values.

### Corrections to the earlier reports

| report | claim | correction (with VA) |
|---|---|---|
| 192 §5 | `WCRR = ~0` | `WCRR (0x4A314028) = 0xFFE2B400` @`0x8010379c`; the `~0` is `WDLY (0x4A314044) = 0xFFFFFFFF` @`0x80103778`.  `WLDR (0x4A31402C) = 0xFFE2B400` @`0x80103758`. |
| 192 §5 | WSPR sequence `0xAAAA … 0xBBBB/0x4444` | Complete sequence is `0xAAAA`@`0x801036b4`, `0x5555`@`0x801036d4`, `0xAAAA`@`0x801036f8`, `0x5555`@`0x80103718` (the two-step unlock is done twice, once per entry path), then `0xBBBB`@`0x801037bc`, `0x4444`@`0x801037dc`. |
| 192 §3 | "GPIO walker writes `GPIO_DATAIN`-adjacent uses" | The walker never reads/writes `DATAIN (0x138)`.  It writes `OE 0x134`, `RISINGDETECT 0x148`, `FALLINGDETECT 0x14C`, `DEBOUNCENABLE 0x150`, `DEBOUNCINGTIME 0x154`, `IRQWAKEN_0 0x44`, `CLEARDATAOUT 0x190`, `SETDATAOUT 0x194` (`0x8010cc28-0x8010cce8`). |
| 192 §4a | "gpio 3/19 read as inputs although the table says output" | Also **gpio 23**: the three table-`dir=1` lines that read back as inputs in the live `GPIO1_OE=0x6FFB1FAA` are 3, 19, 23. |
| 219 §2.2 | "`0x480B8000`/`0x480BA000` (MCSPI3/4)" listed as written registers | These are **bases stored into a driver-argument struct** (`strd r2,r3,[sp,#0x68]` @`0x80101cd8`, `@0x80101d5c`), not register writes.  The only MCSPI writes with a literal base in the image are MCSPI1 (`0x48098110`…), §3. |
| 219 §2 | "144 stores resolve" | 549 (340 distinct).  The method here also resolves pointer-argument/context objects, so it sees driver-internal MMIO that a literal-only pass misses (and vice versa). |
| 219 §2.4 | TWL `0x4A:07=11/08=F0` | There is a second, mutually exclusive path: `{07←0x11, 08←0x7D}` @`0x80100b70-0x80100b84`, taken when the device revision word equals 8 (`cmp r3,#8; beq 0x80100b54` at `0x80100aec`). |
| 219 §2.1 | I²C module clocks only from the sweep | The I²C driver also writes `CM_L4PER_I2Cx_CLKCTRL` itself: `=2` @`0x80114740`, `=0` @`0x801146cc`. |

---

## 1. Method and artefacts

Everything is reproducible from `local/work/s74/`:

| artefact | what |
|---|---|
| `startup-playbook.bin` | extracted image, sha256 `bc7d3a41…`, `0x44008` B |
| `startup-playbook.dis` | full linear ARM disassembly, one line per 32-bit word |
| `s74dis.py` | capstone disassembler |
| `s74flow2.py` | constant reconstruction + register/memory dataflow + call-argument/return fixpoint |
| `trmparse.py`, `trm_regs.json` | TRM `SWPU231AP` register-map parser and the 4 775-register address→name database used for every name and table citation below |
| `inventory.json` | all resolved stores/reads/constants/call sites |
| `stores.txt` | flat annotated store list (all 549) |
| `annotate.py` | joins inventory + TRM |

The dataflow pass is deliberately redundant, because a raw image has no symbols and the code
passes peripheral bases through context objects:

1. **Constant sources.** `movw`/`movt` pairs, `mov`/`mvn` immediates, `adr`, and PC-relative
   literal-pool loads.  Every reconstructed 32-bit value in `0x4A000000-0x4A31FFFF` or
   `0x48000000-0x480DFFFF` is listed in §2.
2. **Register tracking** with three sweeps: *strict* (state dropped at every branch target),
   *relaxed* (kept across conditional branches and calls, ABI-clobbered regs dropped after `bl`),
   *loose* (only `bx lr`/`pop {pc}` and function entries reset the state).  A store found only by
   the loose sweep is marked `loose` in §3 — it is a may-write on a path-insensitive reading.
3. **Context objects.** `sp` is modelled as a frame pointer, so `add r1,sp,#0x10` is a pointer into
   a stack object; stores to `[ptr+N]` are remembered and loads from the same object recover the
   value.  `bl`/`blx`/`b`-to-function record the four argument values; a fixpoint binds `ARGk` of the
   callee; symbolic returns (`mov r0,rN; bx lr`) are substituted with *this* call site's argument.
   This is what resolves the I²C driver (its base lives in `ctx+4`).
4. **Cross-checks.** (a) full-image scan of every 4-byte word for values in the queried ranges
   (catches literal pools independently of the dataflow); (b) the register-name join against the
   TRM, which rejects any "resolved address" that is not a real register.

Residual uncertainty is quantified: **1 518 stores do not resolve to an address at all** (bases held
in heap/global structures).  They are not in the margins of the requested windows as far as a
constant scan can tell — see §11.

---

## 2. Every reconstructed peripheral constant and its consumers

Consumers are the instructions that *use* the register holding the constant (`str`/`ldr`/`strh`,
read-modify-write, or a call argument).  "movw/movt @VA" = where the pair lives.

| constant | register (TRM) | produced at / consumed by |
|---|---|---|
| `0x48000000` |  | `mov @0x80102484`; `arg0 of call @0x80102488` |
| `0x48020000` | UART_THR | `movw/movt @0x80101a28`; `movw/movt @0x80101a34`; `arg0 of call @0x80101a4c`; `arg2 of call @0x80101a4c` |
| `0x48055000` | GPIO_REVISION | `movw/movt @0x80101630` |
| `0x48057000` | GPIO_REVISION | `movw/movt @0x80101648`; `movw/movt @0x80104458` |
| `0x48059000` | GPIO_REVISION | `movw/movt @0x80101658` |
| `0x4805b000` | GPIO_REVISION | `movw/movt @0x80101670` |
| `0x4805d000` | GPIO_REVISION | `movw/movt @0x80101638`; `movw/movt @0x801043e4` |
| `0x48060000` | I2C_REVNB_LO | `movw/movt @0x80101f54`; `movw/movt @0x80101f60`; `arg0 of call @0x80101f78`; `arg2 of call @0x80101f78`; `movw/movt @0x8011392c` |
| `0x4806a000` | UART_THR | `movw/movt @0x801018ec`; `movw/movt @0x801018f8`; `arg0 of call @0x80101954` |
| `0x4806c000` | UART_THR | `movw/movt @0x801019a0`; `movw/movt @0x801019ac`; `arg0 of call @0x801019c4`; `arg2 of call @0x801019c4` |
| `0x4806e000` | UART_THR | `movw/movt @0x80101ab0`; `movw/movt @0x80101abc`; `arg0 of call @0x80101ad4`; `arg2 of call @0x80101ad4` |
| `0x48070000` | I2C_REVNB_LO | `movw/movt @0x80100abc`; `arg0 of call @0x80100ad4`; `movw/movt @0x80100d58`; `arg0 of call @0x80100d70`; `movw/movt @0x80101dec`; `movw/movt @0x80101e44`; `arg0 of call @0x80101e94`; `movw/movt @0x80107ab0`; `arg0 of call @0x80107ac8`; `movw/movt @0x801138f4` |
| `0x48072000` | I2C_REVNB_LO | `movw/movt @0x80101ed0`; `movw/movt @0x80101edc`; `arg0 of call @0x80101ef4`; `arg2 of call @0x80101ef4`; `movw/movt @0x80113910` |
| `0x48098000` |  | `movw/movt @0x80101b4c`; `movw/movt @0x80101bb0`; `arg0 of call @0x80101c00`; `movw/movt @0x80103580`; `movw/movt @0x80103594` |
| `0x4809a000` |  | `movw/movt @0x80101c3c`; `movw/movt @0x80101c48`; `arg0 of call @0x80101c60`; `arg2 of call @0x80101c60` |
| `0x480b4000` | MMCHS_HL_REV | `movw/movt @0x80102284`; `arg0 of call @0x80102288`; `movw/movt @0x8010229c`; `arg0 of call @0x801022a0` |
| `0x480b4100` |  | `movw/movt @0x80104c44` |
| `0x480b8000` |  | `movw/movt @0x80101cc0`; `movw/movt @0x80101ccc`; `arg0 of call @0x80101ce4`; `arg2 of call @0x80101ce4` |
| `0x480ba000` |  | `movw/movt @0x80101d44`; `movw/movt @0x80101d50`; `arg0 of call @0x80101d68`; `arg2 of call @0x80101d68` |
| `0x48350000` | I2C_REVNB_LO | `movw/movt @0x80101fd8`; `movw/movt @0x80101fe4`; `arg0 of call @0x80101ffc`; `arg2 of call @0x80101ffc`; `movw/movt @0x80107ce4`; `arg0 of call @0x80107cf4`; `movw/movt @0x80113948` |
| `0x4a002000` |  | `movw/movt @0x801010fc`; `movw/movt @0x80101128`; `movw/movt @0x80101244`; `movw/movt @0x801012fc`; `movw/movt @0x8010450c`; `arg3 of call @0x80104564`; `movw/movt @0x80107c18`; `movw/movt @0x80107e04` |
| `0x4a004000` | REVISION_CM1 | `movw/movt @0x80100f10`; `movw/movt @0x80100fdc`; `movw/movt @0x80101034`; `arg1 of call @0x8010105c`; `movw/movt @0x80101574`; `movw/movt @0x801015b8`; `movw/movt @0x801015f4` |
| `0x4a004134` | CM_DIV_M3_DPLL_CORE | `movw/movt @0x8010795c`; `arg0 of call @0x80107960` |
| `0x4a004160` | CM_CLKMODE_DPLL_MPU | `movw/movt @0x801014c8`; `arg0 of call @0x801014cc`; `arg0 of call @0x801014d8` |
| `0x4a008000` | REVISION_CM2 | `movw/movt @0x801014e8`; `movw/movt @0x8010152c`; `movw/movt @0x80101688`; `movw/movt @0x80101868` |
| `0x4a009000` | CM_CAM_CLKSTCTRL | `movw/movt @0x80101604`; `movw/movt @0x801017d4`; `movw/movt @0x801017e0`; `movw/movt @0x80101814`; `movw/movt @0x8010184c`; `movw/movt @0x801028ac`; `movw/movt @0x801028e4`; `movw/movt @0x8010360c`; `movw/movt @0x80107c1c`; `movw/movt @0x80107e00` |
| `0x4a056000` | DMA4_REVISION | `movw/movt @0x801027b4`; `arg3 of call @0x80102814`; `movw/movt @0x80104c4c` |
| `0x4a056008` |  | `movw/movt @0x801027d0` |
| `0x4a064000` | UHH_REVISION | `movw/movt @0x80103614`; `movw/movt @0x8010365c` |
| `0x4a0ab000` |  | `movw/movt @0x80102928`; `movw/movt @0x80107c64`; `movw/movt @0x80107e08` |
| `0x4a100000` | CONTROL_PADCONF_CORE_REVISI | `movw/movt @0x80100a80`; `movw/movt @0x801028b0`; `movw/movt @0x801028e8`; `movw/movt @0x80107c10`; `movw/movt @0x80107c34` |
| `0x4a300000` |  | `arg2 of call @0x80100ce0` |
| `0x4a304000` |  | `movw/movt @0x801051a0`; `movw/movt @0x8011433c` |
| `0x4a306000` | REVISION_PRM | `movw/movt @0x80100878`; `movw/movt @0x80100ed0`; `movw/movt @0x80100f04`; `arg3 of call @0x80100f24`; `movw/movt @0x801015f8`; `movw/movt @0x80101864`; `movw/movt @0x80103414`; `movw/movt @0x801077d0`; `movw/movt @0x80107860` |
| `0x4a307000` | PM_CAM_PWRSTCTRL | `movw/movt @0x801009a4`; `arg3 of call @0x801009c4`; `movw/movt @0x80100dd0`; `movw/movt @0x8010145c`; `movw/movt @0x80101690`; `movw/movt @0x8010335c`; `arg2 of call @0x801033ac`; `movw/movt @0x801051e8`; `movw/movt @0x80105a3c`; `arg3 of call @0x80105a5c`; `movw/movt @0x80107750`; `movw/movt @0x80107788`; `movw/movt @0x801077c4` |
| `0x4a307b00` | PRM_RSTCTRL | `literal @0x80100608 (ldr @0x801005d8)`; `arg1 of call @0x801005dc` |
| `0x4a30a000` | REVISION_SCRM | `movw/movt @0x80100910`; `movw/movt @0x80100a40`; `movw/movt @0x80100d18`; `movw/movt @0x8010527c`; `movw/movt @0x80105ad8`; `movw/movt @0x80107988`; `movw/movt @0x801079a0` |
| `0x4a310000` | GPIO_REVISION | `movw/movt @0x80101608` |
| `0x4a314000` | WDT_WIDR | `movw/movt @0x801020c0`; `movw/movt @0x801020d4`; `arg0 of call @0x80102120`; `movw/movt @0x80103690`; `movw/movt @0x801036ac`; `movw/movt @0x801036c0`; `movw/movt @0x801036f0`; `movw/movt @0x80103704`; `movw/movt @0x80103724`; `movw/movt @0x80103740`; `movw/movt @0x80103764`; `movw/movt @0x80103784`; `movw/movt @0x801037a8`; `movw/movt @0x801037c8` |
| `0x4a318000` | GPT_TIDR | `literal @0x80100304 (ldr @0x801002d4)`; `arg1 of call @0x801002d8`; `literal @0x801006ac (ldr @0x80100674)`; `arg1 of call @0x80100678`; `movw/movt @0x801033a4`; `arg1 of call @0x801033ac` |
| `0x4a318010` | GPT1MS_TIOCP_CFG | `add/sub @0x801033b4` |
| `0x4a318034` | GPT_TWPS | `add/sub @0x801033cc` |

Notes:

* `0x4A307000` is materialised only as the base for `CM_WKUP` (`+0x800`) and `PRM_VC` (`+0xBA0`).
* There is **no** constant `0x4A307300`, `0x4A307400`, `0x4A307500`, no constant for any
  `RM_L4PER_*` register, and no padconf constant other than the `0x4A100000` base.
* The only `0x4A30A000` (SCRM) uses are `AUXCLK1/AUXCLK3/AUXCLK4` and `PMICSETUPTIME`
  (0x4A30A104).

---

## 3. Complete store inventory for the two requested windows

193 stores, sorted by target.  `sweep` is the weakest analysis that produced it (`strict` is
block-local and needs no path assumption).  Values shown as `(ldr(addr) | mask)` are
read-modify-writes; the read is at the same address.

| target | store VA | op | value | register (TRM) | table | enclosing function | sweep |
|---|---|---|---|---|---|---|---|
| `48055010` | `801016ec` | str | `(ldr(0x48055010) \| 0x1)` | GPIO_SYSCONFIG | 25-18 | `80101440` | strict |
| `48057010` | `801016f8` | str | `(ldr(0x48057010) \| 0x1)` | GPIO_SYSCONFIG | 25-18 | `80101440` | strict |
| `48059010` | `80101708` | str | `(ldr(0x48059010) \| 0x1)` | GPIO_SYSCONFIG | 25-19 | `80101440` | strict |
| `48070034` | `80113a20` | str | `0x00006f6f` | I2C_WE | 23-31 | `80113838` | loose |
| `48098110` | `80103588` | str | `0x0000030a` | MCSPI_SYSCONFIG | 23-303 | `80103578` | strict |
| `48098118` | `801035b4` | str | `0xffffffff` | MCSPI_IRQSTATUS | 23-303 | `80103578` | strict |
| `4809811c` | `801035ac` | str | `?` |  |  | `80103578` | strict |
| `48098128` | `801035dc` | str | `((ldr(0x48098128) &~ 0x5) \| 0x1)` | MCSPI_MODULCTRL | 23-303 | `80103578` | strict |
| `4809812c` | `801035cc` | str | `((((ldr(0x4809812c) &~ 0x70000) &~ 0x7f) \| 0x10000) \| 0x44)` |  |  | `80103578` | strict |
| `4809812c` | `801035e8` | str | `(ldr(0x4809812c) &~ 0x100000)` |  |  | `80103578` | strict |
| `48098134` | `801035a8` | str | `(ldr(0x48098134) &~ 0x1)` |  |  | `80103578` | strict |
| `4a002300` | `80107c48` | str | `0x00000001` | CONTROL_DEV_CONF | 18-113 | `80107b94` | strict |
| `4a002300` | `80107c50` | str | `0x00000000` | CONTROL_DEV_CONF | 18-113 | `80107b94` | strict |
| `4a002300` | `80107e18` | str | `0x00000001` | CONTROL_DEV_CONF | 18-113 | `80107b94` | strict |
| `4a002320` | `80104558` | str | `0x0401040f` | CONTROL_LDOSRAM_IVA_VOLTAGE_CTRL | 18-113 | `801044d8` | strict |
| `4a002324` | `8010455c` | str | `0x0401040f` | CONTROL_LDOSRAM_MPU_VOLTAGE_CTR | 18-113 | `801044d8` | strict |
| `4a002328` | `80104560` | str | `0x0401040f` | CONTROL_LDOSRAM_CORE_VOLTAGE_CT | 18-113 | `801044d8` | strict |
| `4a002330` | `80101138` | str | `((ldr(0x4a002330) &~ 0x3f) \| 0x29)` | CONTROL_DPLL_NWELL_TRIM_0 | 18-113 | `801010d8` | strict |
| `4a004134` | `8010c8f8` | str | `((ldr(0x4a004134) &~ 0x1f) \| 0x13)` | CM_DIV_M3_DPLL_CORE | 3-940 | `8010c8e4` | strict |
| `4a004134` | `8010c904` | str | `(ldr(0x4a004134) \| 0x100)` | CM_DIV_M3_DPLL_CORE | 3-940 | `8010c8e4` | strict |
| `4a004160` | `80100fb8` | str | `((ldr(0x4a004160) &~ 0x7) \| 0x5)` | CM_CLKMODE_DPLL_MPU | 3-940 | `80100ecc` | strict |
| `4a004160` | `80101010` | str | `(ldr(0x4a004160) \| 0x7)` | CM_CLKMODE_DPLL_MPU | 3-940 | `80100ecc` | strict |
| `4a004160` | `8010c8c4` | str | `((ldr(0x4a004160) &~ 0x7) \| 0x4)` | CM_CLKMODE_DPLL_MPU | 3-940 | `8010c8ac` | strict |
| `4a00416c` | `80101000` | str | `?` | CM_CLKSEL_DPLL_MPU | 3-940 | `80100ecc` | strict |
| `4a004400` | `80101590` | str | `0x00000002` | CM_DSP_CLKSTCTRL | 3-1040 | `80101440` | strict |
| `4a004420` | `80101584` | str | `0x00000001` | CM_DSP_DSP_CLKCTRL | 3-1040 | `80101440` | strict |
| `4a004500` | `801015cc` | str | `0x00000002` | CM1_ABE_CLKSTCTRL | 3-1049 | `80101440` | strict |
| `4a004500` | `80101618` | str | `0x00000003` | CM1_ABE_CLKSTCTRL | 3-1049 | `80101440` | strict |
| `4a004528` | `8010162c` | str | `0x00000002` | CM1_ABE_AESS_CLKCTRL | 3-1049 | `80101440` | strict |
| `4a004530` | `80101634` | str | `0x00000002` | CM1_ABE_PDM_CLKCTRL | 3-1049 | `80101440` | strict |
| `4a004538` | `8010163c` | str | `0x00000002` | CM1_ABE_DMIC_CLKCTRL | 3-1049 | `80101440` | strict |
| `4a004540` | `80101644` | str | `0x00000002` | CM1_ABE_MCASP_CLKCTRL | 3-1049 | `80101440` | strict |
| `4a004548` | `8010164c` | str | `0x00000000` | CM1_ABE_MCBSP1_CLKCTRL | 3-1049 | `80101440` | strict |
| `4a004550` | `80101654` | str | `0x00000000` | CM1_ABE_MCBSP2_CLKCTRL | 3-1049 | `80101440` | strict |
| `4a004558` | `8010165c` | str | `0x00000000` | CM1_ABE_MCBSP3_CLKCTRL | 3-1049 | `80101440` | strict |
| `4a004560` | `8010166c` | str | `0x00000f02` | CM1_ABE_SLIMBUS_CLKCTRL | 3-1049 | `80101440` | strict |
| `4a004568` | `80101674` | str | `0x00000002` | CM1_ABE_GPTIMER5_CLKCTRL | 3-1049 | `80101440` | strict |
| `4a004570` | `8010167c` | str | `0x00000002` | CM1_ABE_GPTIMER6_CLKCTRL | 3-1049 | `80101440` | strict |
| `4a004578` | `80101684` | str | `0x00000002` | CM1_ABE_GPTIMER7_CLKCTRL | 3-1049 | `80101440` | strict |
| `4a004580` | `8010168c` | str | `0x00000002` | CM1_ABE_GPTIMER8_CLKCTRL | 3-1049 | `80101440` | strict |
| `4a004588` | `80101694` | str | `0x00000002` | CM1_ABE_WDTIMER3_CLKCTRL | 3-1049 | `80101440` | strict |
| `4a008640` | `80101898` | str | `0x00000000` | CM_ALWON_USBPHY_CLKCTRL | 3-1203 | `80101440` | strict |
| `4a008828` | `80101798` | str | `0x00000001` | CM_L3_2_GPMC_CLKCTRL | 3-1214 | `80101440` | strict |
| `4a008900` | `80101504` | str | `0x00000001` | CM_MPU_M3_CLKSTCTRL | 3-1214 | `80101440` | strict |
| `4a008920` | `801014f8` | str | `0x00000001` | CM_MPU_M3_MPU_M3_CLKCTRL | 3-1214 | `80101440` | strict |
| `4a008a00` | `80101784` | str | `0x00000000` | CM_SDMA_CLKSTCTRL | 3-1214 | `80101440` | strict |
| `4a008b00` | `80101788` | str | `0x00000003` | CM_MEMIF_CLKSTCTRL | 3-1214 | `80101440` | strict |
| `4a008b30` | `8010178c` | str | `0x00000001` | CM_MEMIF_EMIF_1_CLKCTRL | 3-1214 | `80101440` | strict |
| `4a008b38` | `80101790` | str | `0x00000001` | CM_MEMIF_EMIF_2_CLKCTRL | 3-1214 | `80101440` | strict |
| `4a008c00` | `80101794` | str | `0x00000003` | RESERVED | 3-1214 | `80101440` | strict |
| `4a008e20` | `8010179c` | str | `0x00000001` | CM_L3INSTR_L3_3_CLKCTRL | 3-1214 | `80101440` | strict |
| `4a008e28` | `801017a0` | str | `0x00000001` | CM_L3INSTR_L3_INSTR_CLKCTRL | 3-1214 | `80101440` | strict |
| `4a008e40` | `801017a4` | str | `0x00000001` | CM_L3INSTR_OCP_WP1_CLKCTRL | 3-1214 | `80101440` | strict |
| `4a008f00` | `8010154c` | str | `0x00000002` | CM_IVAHD_CLKSTCTRL | 3-1279 | `80101440` | strict |
| `4a008f20` | `8010153c` | str | `0x00000001` | CM_IVAHD_IVAHD_CLKCTRL | 3-1279 | `80101440` | strict |
| `4a008f28` | `80101544` | str | `0x00000001` | CM_IVAHD_SL2_CLKCTRL | 3-1279 | `80101440` | strict |
| `4a009000` | `801017b4` | str | `0x00000002` | CM_CAM_CLKSTCTRL | 3-1290 | `80101440` | strict |
| `4a009000` | `801017f8` | str | `0x00000000` | CM_CAM_CLKSTCTRL | 3-1290 | `80101440` | strict |
| `4a009020` | `801017b8` | str | `0x00000102` | CM_CAM_ISS_CLKCTRL | 3-1290 | `80101440` | strict |
| `4a009028` | `801017bc` | str | `0x00000002` | CM_CAM_FDIF_CLKCTRL | 3-1290 | `80101440` | strict |
| `4a009100` | `80101800` | str | `0x00000002` | CM_DSS_CLKSTCTRL | 3-1301 | `80101440` | strict |
| `4a009100` | `80101834` | str | `0x00000000` | CM_DSS_CLKSTCTRL | 3-1301 | `80101440` | strict |
| `4a009120` | `80101808` | str | `0x00000f02` | CM_DSS_DSS_CLKCTRL | 3-1301 | `80101440` | strict |
| `4a009200` | `8010183c` | str | `0x00000002` | CM_SGX_CLKSTCTRL | 3-1310 | `80101440` | strict |
| `4a009200` | `80101878` | str | `0x00000000` | CM_SGX_CLKSTCTRL | 3-1310 | `80101440` | strict |
| `4a009220` | `80101840` | str | `0x01000002` | CM_SGX_SGX_CLKCTRL | 3-1310 | `80101440` | strict |
| `4a009328` | `80101754` | str | `0x01000002` | CM_L3INIT_HSMMC1_CLKCTRL | 3-1319 | `80101440` | strict |
| `4a009328` | `801028bc` | str | `(ldr(0x4a009328) \| 0x1000000)` | CM_L3INIT_HSMMC1_CLKCTRL | 3-1319 | `8010275c` | strict |
| `4a009328` | `801028f4` | str | `(ldr(0x4a009328) \| 0x1000000)` | CM_L3INIT_HSMMC1_CLKCTRL | 3-1319 | `8010275c` | strict |
| `4a009330` | `80101758` | str | `0x01000002` | CM_L3INIT_HSMMC2_CLKCTRL | 3-1319 | `80101440` | strict |
| `4a009338` | `8010187c` | str | `0x00000001` | CM_L3INIT_HSI_CLKCTRL | 3-1319 | `80101440` | strict |
| `4a009340` | `80101880` | str | `0x00000002` | RESERVED | 3-1319 | `80101440` | strict |
| `4a009358` | `80101884` | str | `0x00000002` | CM_L3INIT_HSUSBHOST_CLKCTRL | 3-1319 | `80101440` | strict |
| `4a009358` | `80103628` | str | `(ldr(0x4a009358) \| 0x3000000)` | CM_L3INIT_HSUSBHOST_CLKCTRL | 3-1319 | `80103578` | strict |
| `4a009360` | `80101888` | str | `0x00000001` | CM_L3INIT_HSUSBOTG_CLKCTRL | 3-1319 | `80101440` | strict |
| `4a009360` | `80107c44` | str | `0x00000001` | CM_L3INIT_HSUSBOTG_CLKCTRL | 3-1319 | `80107b94` | strict |
| `4a009360` | `80107e20` | str | `0x00000000` | CM_L3INIT_HSUSBOTG_CLKCTRL | 3-1319 | `80107b94` | strict |
| `4a009368` | `8010188c` | str | `0x00000001` | CM_L3INIT_HSUSBTLL_CLKCTRL | 3-1319 | `80101440` | strict |
| `4a0093d0` | `80101890` | str | `0x00000002` | CM_L3INIT_FSUSB_CLKCTRL | 3-1319 | `80101440` | strict |
| `4a0093e0` | `80101894` | str | `0x00000001` | CM_L3INIT_USBPHY_CLKCTRL | 3-1319 | `80101440` | strict |
| `4a0093e0` | `80107c78` | str | `0x00000101` | CM_L3INIT_USBPHY_CLKCTRL | 3-1319 | `80107b94` | strict |
| `4a0093e0` | `80107e14` | str | `0x00000000` | CM_L3INIT_USBPHY_CLKCTRL | 3-1319 | `80107b94` | strict |
| `4a009400` | `8010169c` | str | `0x00000002` | CM_L4PER_CLKSTCTRL | 3-1342 | `80101440` | strict |
| `4a009428` | `801016a4` | str | `0x00000002` | CM_L4PER_GPTIMER10_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009430` | `801016a8` | str | `0x00000002` | CM_L4PER_GPTIMER11_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009438` | `801016ac` | str | `0x00000002` | CM_L4PER_GPTIMER2_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009440` | `801016b0` | str | `0x00000002` | CM_L4PER_GPTIMER3_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009448` | `801016b4` | str | `0x00000002` | CM_L4PER_GPTIMER4_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009450` | `801016b8` | str | `0x00000002` | CM_L4PER_GPTIMER9_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009460` | `801016bc` | str | `0x00000001` | CM_L4PER_GPIO2_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009468` | `801016c0` | str | `0x00000001` | CM_L4PER_GPIO3_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009470` | `801016c4` | str | `0x00000001` | CM_L4PER_GPIO4_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009478` | `801016c8` | str | `0x00000001` | CM_L4PER_GPIO5_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009480` | `801016cc` | str | `0x00000001` | CM_L4PER_GPIO6_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009488` | `80101728` | str | `0x00000002` | CM_L4PER_HDQ1W_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a0094a0` | `80101730` | str | `0x00000002` | CM_L4PER_I2C1_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a0094a8` | `80101734` | str | `0x00000002` | CM_L4PER_I2C2_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a0094b0` | `80101738` | str | `0x00000002` | CM_L4PER_I2C3_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a0094b8` | `8010173c` | str | `0x00000002` | CM_L4PER_I2C4_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a0094e0` | `80101740` | str | `0x00000002` | CM_L4PER_MCBSP4_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a0094f0` | `80101744` | str | `0x00000002` | CM_L4PER_MCSPI1_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a0094f8` | `80101748` | str | `0x00000002` | CM_L4PER_MCSPI2_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009500` | `8010174c` | str | `0x00000002` | CM_L4PER_MCSPI3_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009508` | `80101750` | str | `0x00000002` | CM_L4PER_MCSPI4_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009520` | `8010175c` | str | `0x00000002` | CM_L4PER_MMCSD3_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009528` | `80101760` | str | `0x00000002` | CM_L4PER_MMCSD4_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009540` | `80101768` | str | `0x00000002` | CM_L4PER_UART1_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009548` | `8010176c` | str | `0x00000002` | CM_L4PER_UART2_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009550` | `80101770` | str | `0x00000002` | CM_L4PER_UART3_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009558` | `80101774` | str | `0x00000002` | CM_L4PER_UART4_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a009560` | `80101764` | str | `0x00000002` | CM_L4PER_MMCSD5_CLKCTRL | 3-1342 | `80101440` | strict |
| `4a056008` | `801027f4` | str | `0xffffffff` |  |  | `8010275c` | strict |
| `4a05600c` | `801027fc` | str | `0xffffffff` |  |  | `8010275c` | strict |
| `4a056010` | `80102804` | str | `0xffffffff` |  |  | `8010275c` | strict |
| `4a056014` | `8010280c` | str | `0xffffffff` |  |  | `8010275c` | strict |
| `4a056018` | `801027f0` | str | `0x00000000` |  |  | `8010275c` | strict |
| `4a05601c` | `801027f8` | str | `0x00000000` |  |  | `8010275c` | strict |
| `4a056020` | `80102800` | str | `0x00000000` |  |  | `8010275c` | strict |
| `4a056024` | `80102808` | str | `0x00000000` |  |  | `8010275c` | strict |
| `4a05602c` | `801027ec` | str | `0x00002011` | DMA4_OCP_SYSCONF | 16-23 | `8010275c` | strict |
| `4a056078` | `801027e4` | str | `((ldr(0x4a056078) \| 0xff0000) \| 0x10)` | DMA4_GCR | 16-23 | `8010275c` | strict |
| `4a064010` | `80103630` | str | `0x00000001` | UHH_SYSCONFIG | 23-1069 | `80103578` | strict |
| `4a064010` | `80103660` | str | `0x00000014` | UHH_SYSCONFIG | 23-1069 | `80103578` | strict |
| `4a064040` | `80103668` | str | `0x0000001c` | UHH_HOSTCONFIG | 23-1069 | `80103578` | strict |
| `4a0ab001` | `8010292c` | strb | `0x00000000` |  |  | `80102920` | strict |
| `4a0ab404` | `80107c6c` | str | `0x0000100d` | OTG_SYSCONFIG | 23-1191 | `80107b94` | strict |
| `4a0ab404` | `80107e1c` | str | `0x00000000` | OTG_SYSCONFIG | 23-1191 | `80107b94` | strict |
| `4a0ab40c` | `80107c70` | str | `0x00000000` | OTG_INTERFSEL | 23-1191 | `80107b94` | strict |
| `4a100600` | `801028c8` | str | `(ldr(0x4a100600) \| 0x14400000)` | CONTROL_PBIASLITE | 18-321 | `8010275c` | strict |
| `4a100600` | `80102900` | str | `(ldr(0x4a100600) \| 0x14400000)` | CONTROL_PBIASLITE | 18-321 | `8010275c` | strict |
| `4a100600` | `80102918` | str | `(ldr(0x4a100600) \| 0x100000)` | CONTROL_PBIASLITE | 18-321 | `8010275c` | strict |
| `4a100604` | `80100a88` | str | `0x11111111` | CONTROL_I2C_0 | 18-321 | `80100a50` | strict |
| `4a100628` | `801028d4` | str | `(ldr(0x4a100628) \| 0xe000000)` | CONTROL_MMC1 | 18-321 | `8010275c` | strict |
| `4a100628` | `8010290c` | str | `(ldr(0x4a100628) \| 0xe000000)` | CONTROL_MMC1 | 18-321 | `8010275c` | strict |
| `4a300110` | `80100cdc` | str | `0x00000007` |  |  | `80100cc0` | strict |
| `4a306010` | `801018a0` | str | `?` | PRM_IRQSTATUS_MPU_A9 | 3-361 | `80101440` | strict |
| `4a306010` | `801077d8` | str | `?` | PRM_IRQSTATUS_MPU_A9 | 3-361 | `80107748` | strict |
| `4a30610c` | `8010161c` | str | `0x00000000` | CM_ABE_PLL_REF_CLKSEL | 3-384 | `80101440` | strict |
| `4a306110` | `80100884` | str | `0x00000005` | CM_SYS_CLKSEL | 3-384 | `801007f0` | strict |
| `4a306110` | `8010786c` | str | `0x00000007` | CM_SYS_CLKSEL | 3-384 | `80107804` | strict |
| `4a307800` | `80101778` | str | `0x00000003` | CM_WKUP_CLKSTCTRL | 3-759 | `80101440` | strict |
| `4a307800` | `80103374` | str | `(ldr(0x4a307800) \| 0x3)` | CM_WKUP_CLKSTCTRL | 3-759 | `80103350` | strict |
| `4a307830` | `801017ac` | str | `0x00000002` | CM_WKUP_WDTIMER2_CLKCTRL | 3-759 | `80101440` | strict |
| `4a307838` | `8010177c` | str | `0x00000001` | CM_WKUP_GPIO1_CLKCTRL | 3-759 | `80101440` | strict |
| `4a307840` | `80101780` | str | `0x01000002` | CM_WKUP_GPTIMER1_CLKCTRL | 3-759 | `80101440` | strict |
| `4a307840` | `80103388` | str | `(((ldr(0x4a307840) &~ 0x1000000) &~ 0x3) \| 0x2)` | CM_WKUP_GPTIMER1_CLKCTRL | 3-759 | `80103350` | strict |
| `4a307878` | `801017b0` | str | `0x00000000` | CM_WKUP_KEYBOARD_CLKCTRL | 3-759 | `80101440` | strict |
| `4a307ba0` | `80107778` | str | `?` | PRM_VC_VAL_BYPASS | 3-792 | `80107748` | strict |
| `4a307ba0` | `8010777c` | str | `(ARG0 \| 0x1000000)` | PRM_VC_VAL_BYPASS | 3-792 | `80107748` | strict |
| `4a307ba8` | `801009b0` | str | `0x00000000` | PRM_VC_CFG_I2C_MODE | 3-792 | `80100994` | strict |
| `4a307ba8` | `801051f8` | str | `0x00000000` | PRM_VC_CFG_I2C_MODE | 3-792 | `801051cc` | strict |
| `4a307ba8` | `80105a48` | str | `0x00000000` | PRM_VC_CFG_I2C_MODE | 3-792 | `80105a2c` | strict |
| `4a307bac` | `801009c0` | str | `0x00001e1a` | PRM_VC_CFG_I2C_CLK | 3-792 | `80100994` | strict |
| `4a307bac` | `80105200` | str | `0x00006026` | PRM_VC_CFG_I2C_CLK | 3-792 | `801051cc` | strict |
| `4a307bac` | `80105a58` | str | `0x00001e1a` | PRM_VC_CFG_I2C_CLK | 3-792 | `80105a2c` | strict |
| `4a307bb4` | `80100ddc` | str | `0x00000001` | PRM_SRAM_WKUP_SETUP | 3-792 | `80100db8` | strict |
| `4a307bb4` | `80101468` | str | `0x00000001` | PRM_SRAM_WKUP_SETUP | 3-792 | `80101440` | strict |
| `4a307bb8` | `80100de4` | str | `0x00000001` | PRM_LDO_SRAM_CORE_SETUP | 3-792 | `80100db8` | strict |
| `4a307bb8` | `8010146c` | str | `0x00000001` | PRM_LDO_SRAM_CORE_SETUP | 3-792 | `80101440` | strict |
| `4a307bc0` | `80100de8` | str | `0x00000001` | PRM_LDO_SRAM_MPU_SETUP | 3-792 | `80100db8` | strict |
| `4a307bc0` | `80101470` | str | `0x00000001` | PRM_LDO_SRAM_MPU_SETUP | 3-792 | `80101440` | strict |
| `4a307bc8` | `80100dec` | str | `0x00000001` | PRM_LDO_SRAM_IVA_SETUP | 3-792 | `80100db8` | strict |
| `4a307bc8` | `80101474` | str | `0x00000001` | PRM_LDO_SRAM_IVA_SETUP | 3-792 | `80101440` | strict |
| `4a307bf8` | `80107774` | str | `(ldr(0x4a307bf8) & 0x73f3f3f)` | PRM_VC_ERRST | 3-792 | `80107748` | strict |
| `4a307bf8` | `801077c8` | str | `(ldr(0x4a307bf8) & 0x73f3f3f)` | PRM_VC_ERRST | 3-792 | `80107748` | strict |
| `4a30a104` | `80100a48` | str | `0x00000000` | PMICSETUPTIME | 3-1490 | `80100994` | strict |
| `4a30a104` | `80105288` | str | `0x00160016` | PMICSETUPTIME | 3-1490 | `801051cc` | strict |
| `4a30a104` | `80105ae0` | str | `0x00000000` | PMICSETUPTIME | 3-1490 | `80105a2c` | strict |
| `4a30a314` | `80100914` | str | `0x00010000` | AUXCLK1 | 3-1490 | `801007f0` | strict |
| `4a30a314` | `80107990` | str | `0x00020100` | AUXCLK1 | 3-1490 | `80107804` | strict |
| `4a30a314` | `801079b0` | str | `0x00020100` | AUXCLK1 | 3-1490 | `80107804` | strict |
| `4a30a31c` | `80100d20` | str | `0x00010100` | AUXCLK3 | 3-1490 | `80100cc0` | strict |
| `4a30a320` | `801079c0` | str | `?` | AUXCLK4 | 3-1490 | `80107804` | strict |
| `4a310010` | `801016e0` | str | `(ldr(0x4a310010) \| 0x1)` | GPIO_SYSCONFIG | 25-18 | `80101440` | strict |
| `4a314024` | `80103734` | str | `?` | WDT_WCLR | 22-96 | `801036e8` | strict |
| `4a314028` | `8010379c` | str | `0xffe2b400` | WDT_WCRR | 22-96 | `801036e8` | strict |
| `4a31402c` | `80103758` | str | `0xffe2b400` | WDT_WLDR | 22-96 | `801036e8` | strict |
| `4a314044` | `80103778` | str | `0xffffffff` | WDT_WDLY | 22-96 | `801036e8` | strict |
| `4a314048` | `801036b4` | str | `0x0000aaaa` | WDT_WSPR | 22-96 | `801036a4` | strict |
| `4a314048` | `801036d4` | str | `0x00005555` | WDT_WSPR | 22-96 | `801036a4` | strict |
| `4a314048` | `801036f8` | str | `0x0000aaaa` | WDT_WSPR | 22-96 | `801036e8` | strict |
| `4a314048` | `80103718` | str | `0x00005555` | WDT_WSPR | 22-96 | `801036e8` | strict |
| `4a314048` | `801037bc` | str | `0x0000bbbb` | WDT_WSPR | 22-96 | `801036e8` | strict |
| `4a314048` | `801037dc` | str | `0x00004444` | WDT_WSPR | 22-96 | `801036e8` | strict |
| `4a318010` | `801033bc` | str | `0x00000002` | GPT1MS_TIOCP_CFG | 22-21 | `80103350` | strict |
| `4a318010` | `801033d4` | str | `0x00000215` | GPT1MS_TIOCP_CFG | 22-21 | `80103350` | strict |
| `4a31801c` | `80103408` | str | `0x00000001` | GPT_TIER | 22-21 | `80103350` | strict |
| `4a318020` | `801033dc` | str | `0x00000001` | GPT_TWER | 22-21 | `80103350` | strict |
| `4a318024` | `801033e0` | str | `?` | GPT_TCLR | 22-21 | `80103350` | strict |
| `4a318024` | `80103410` | str | `0x00000043` | GPT_TCLR | 22-21 | `80103350` | strict |
| `4a318028` | `801033f8` | str | `?` | GPT_TCRR | 22-21 | `80103350` | strict |
| `4a31802c` | `801033f0` | str | `?` | GPT_TLDR | 22-21 | `80103350` | strict |
| `4a318038` | `80103400` | str | `0xffffffff` | GPT_TMAR | 22-21 | `80103350` | strict |

**Resolved peripheral reads** (83; `ldr`/`ldrh` only, i.e. the other half of every RMW and every
poll loop):

| target | read VA | op | register (TRM) | table |
|---|---|---|---|---|
| `48055010` | `801016e4` | ldr | GPIO_SYSCONFIG | 25-18 |
| `48057010` | `801016f0` | ldr | GPIO_SYSCONFIG | 25-18 |
| `48057138` | `80104460` | ldr | GPIO_DATAIN | 25-18 |
| `48059010` | `80101700` | ldr | GPIO_SYSCONFIG | 25-19 |
| `4805d138` | `801043e8` | ldr | GPIO_DATAIN | 25-19 |
| `48098114` | `8010358c` | ldr | MCSPI_SYSSTATUS | 23-303 |
| `48098128` | `801035d0` | ldr | MCSPI_MODULCTRL | 23-303 |
| `4809812c` | `801035b8` | ldr |  |  |
| `4809812c` | `801035e0` | ldr |  |  |
| `48098134` | `801035a0` | ldr |  |  |
| `4a002204` | `80101250` | ldr | CONTROL_ID_CODE | 18-113 |
| `4a002204` | `8010129c` | ldr | CONTROL_ID_CODE | 18-113 |
| `4a002204` | `80101318` | ldr | CONTROL_ID_CODE | 18-113 |
| `4a002218` | `80101104` | ldr | CONTROL_STD_FUSE_PROD_ID_1 | 18-113 |
| `4a002268` | `80101108` | ldr | CONTROL_STD_FUSE_OPP_DPLL_1 | 18-113 |
| `4a0022c4` | `80101290` | ldr | CONTROL_STATUS | 18-113 |
| `4a0022c4` | `8010130c` | ldr | CONTROL_STATUS | 18-113 |
| `4a002314` | `80104534` | ldr | CONTROL_LDOVBB_IVA_VOLTAGE_CTRL | 18-113 |
| `4a002318` | `80104540` | ldr | CONTROL_LDOVBB_MPU_VOLTAGE_CTRL | 18-113 |
| `4a002320` | `80104510` | ldr | CONTROL_LDOSRAM_IVA_VOLTAGE_CTRL | 18-113 |
| `4a002324` | `8010451c` | ldr | CONTROL_LDOSRAM_MPU_VOLTAGE_CTR | 18-113 |
| `4a002328` | `80104528` | ldr | CONTROL_LDOSRAM_CORE_VOLTAGE_CT | 18-113 |
| `4a002330` | `8010112c` | ldr | CONTROL_DPLL_NWELL_TRIM_0 | 18-113 |
| `4a004134` | `8010c8ec` | ldr | CM_DIV_M3_DPLL_CORE | 3-940 |
| `4a004134` | `8010c8fc` | ldr | CM_DIV_M3_DPLL_CORE | 3-940 |
| `4a004160` | `80100fa0` | ldr | CM_CLKMODE_DPLL_MPU | 3-940 |
| `4a004160` | `80101004` | ldr | CM_CLKMODE_DPLL_MPU | 3-940 |
| `4a004160` | `8010c8ac` | ldr | CM_CLKMODE_DPLL_MPU | 3-940 |
| `4a004160` | `8010c8c8` | ldr | CM_CLKMODE_DPLL_MPU | 3-940 |
| `4a004164` | `80100fcc` | ldr | CM_IDLEST_DPLL_MPU | 3-940 |
| `4a004164` | `80100fe0` | ldr | CM_IDLEST_DPLL_MPU | 3-940 |
| `4a004164` | `80101024` | ldr | CM_IDLEST_DPLL_MPU | 3-940 |
| `4a004164` | `80101038` | ldr | CM_IDLEST_DPLL_MPU | 3-940 |
| `4a00416c` | `80100f28` | ldr | CM_CLKSEL_DPLL_MPU | 3-940 |
| `4a004170` | `80100f30` | ldr | CM_DIV_M2_DPLL_MPU | 3-940 |
| `4a004400` | `801015a4` | ldr | CM_DSP_CLKSTCTRL | 3-1040 |
| `4a004500` | `801015e0` | ldr | CM1_ABE_CLKSTCTRL | 3-1049 |
| `4a008900` | `80101518` | ldr | CM_MPU_M3_CLKSTCTRL | 3-1214 |
| `4a008f00` | `80101560` | ldr | CM_IVAHD_CLKSTCTRL | 3-1279 |
| `4a009020` | `801017c0` | ldr | CM_CAM_ISS_CLKCTRL | 3-1290 |
| `4a009028` | `801017d8` | ldr | CM_CAM_FDIF_CLKCTRL | 3-1290 |
| `4a009120` | `8010180c` | ldr | CM_DSS_DSS_CLKCTRL | 3-1301 |
| `4a009220` | `80101844` | ldr | CM_SGX_SGX_CLKCTRL | 3-1310 |
| `4a009328` | `801028b4` | ldr | CM_L3INIT_HSMMC1_CLKCTRL | 3-1319 |
| `4a009328` | `801028ec` | ldr | CM_L3INIT_HSMMC1_CLKCTRL | 3-1319 |
| `4a009358` | `8010361c` | ldr | CM_L3INIT_HSUSBHOST_CLKCTRL | 3-1319 |
| `4a056078` | `801027c4` | ldr | DMA4_GCR | 16-23 |
| `4a064010` | `80103640` | ldr | UHH_SYSCONFIG | 23-1069 |
| `4a100600` | `801028c0` | ldr | CONTROL_PBIASLITE | 18-321 |
| `4a100600` | `801028f8` | ldr | CONTROL_PBIASLITE | 18-321 |
| `4a100600` | `80102910` | ldr | CONTROL_PBIASLITE | 18-321 |
| `4a100620` | `80107c20` | ldr | CONTROL_USB2PHYCORE | 18-321 |
| `4a100620` | `80107ca0` | ldr | CONTROL_USB2PHYCORE | 18-321 |
| `4a100628` | `801028cc` | ldr | CONTROL_MMC1 | 18-321 |
| `4a100628` | `80102904` | ldr | CONTROL_MMC1 | 18-321 |
| `4a304010` | `801051a8` | ldr |  |  |
| `4a304010` | `801051b0` | ldr |  |  |
| `4a304010` | `80114340` | ldr |  |  |
| `4a306010` | `8010189c` | ldr | PRM_IRQSTATUS_MPU_A9 | 3-361 |
| `4a306010` | `801077d4` | ldr | PRM_IRQSTATUS_MPU_A9 | 3-361 |
| `4a306104` | `80100f14` | ldr | RESERVED | 3-384 |
| `4a306110` | `80100ed8` | ldr | CM_SYS_CLKSEL | 3-384 |
| `4a306110` | `80103420` | ldr | CM_SYS_CLKSEL | 3-384 |
| `4a307800` | `80103368` | ldr | CM_WKUP_CLKSTCTRL | 3-759 |
| `4a307840` | `80103378` | ldr | CM_WKUP_GPTIMER1_CLKCTRL | 3-759 |
| `4a307840` | `80103390` | ldr | CM_WKUP_GPTIMER1_CLKCTRL | 3-759 |
| `4a307ba0` | `80107780` | ldr | PRM_VC_VAL_BYPASS | 3-792 |
| `4a307bf8` | `80107760` | ldr | PRM_VC_ERRST | 3-792 |
| `4a307bf8` | `80107794` | ldr | PRM_VC_ERRST | 3-792 |
| `4a310010` | `801016d0` | ldr | GPIO_SYSCONFIG | 25-18 |
| `4a314034` | `80103694` | ldr | WDT_WWPS | 22-96 |
| `4a314034` | `801036b8` | ldr | WDT_WWPS | 22-96 |
| `4a314034` | `801036d8` | ldr | WDT_WWPS | 22-96 |
| `4a314034` | `801036fc` | ldr | WDT_WWPS | 22-96 |
| `4a314034` | `8010371c` | ldr | WDT_WWPS | 22-96 |
| `4a314034` | `80103738` | ldr | WDT_WWPS | 22-96 |
| `4a314034` | `8010375c` | ldr | WDT_WWPS | 22-96 |
| `4a314034` | `8010377c` | ldr | WDT_WWPS | 22-96 |
| `4a314034` | `801037a0` | ldr | WDT_WWPS | 22-96 |
| `4a314034` | `801037c0` | ldr | WDT_WWPS | 22-96 |
| `4a314034` | `801037e0` | ldr | WDT_WWPS | 22-96 |
| `4a318010` | `801033c0` | ldr | GPT1MS_TIOCP_CFG | 22-21 |
| `4a318034` | `801033e4` | ldr | GPT_TWPS | 22-21 |

---

## 4. Q1 — CM2 L4PER (and the rest of CM2)

**Yes.** `CM_L4PER_MMCSD5_CLKCTRL (0x4A009560) = 0x00000002` at

```
80101764  e5832560  str r2, [r3, #0x560]     ; r3 = 0x4A009000 (movw @0x801015fc, movt @0x80101604)
                                             ; r2 = 2           (mov  @0x80101624)
```

`MODULEMODE[1:0] = 0b10` = explicitly enabled (not `HW_AUTO`).  This confirms report 219 §2.1.

The same block writes the whole L4PER_CM2 page (`0x4A009400-0x4A009560`) plus the CAM/L3INIT/DSS/SGX
sub-blocks.  Full list in §3; the L4PER part is:

| register | value | VA |
|---|---|---|
| `CM_L4PER_CLKSTCTRL 0x4A009400` | 2 | `0x8010169c` |
| `CM_L4PER_GPTIMER10/11/2/3/4/9` `0x428/430/438/440/448/450` | 2 each | `0x801016a4-a8-ac-b0-b4-b8` |
| `CM_L4PER_GPIO2/3/4/5/6` `0x460…0x480` | **1** each | `0x801016bc-c0-c4-c8-cc` |
| `CM_L4PER_HDQ1W 0x4A009488` | 2 | `0x80101728` |
| `CM_L4PER_I2C1/2/3/4` `0x4A0094A0/A8/B0/B8` | 2 each | `0x80101730-34-38-3c` |
| `CM_L4PER_MCBSP4 0x4A0094E0` | 2 | `0x80101740` |
| `CM_L4PER_MCSPI1/2/3/4` `0x4A0094F0/F8/500/508` | 2 each | `0x80101744-48-4c-50` |
| `CM_L4PER_MMCSD3/4` `0x4A009520/528` | 2 each | `0x8010175c/760` |
| **`CM_L4PER_MMCSD5` `0x4A009560`** | **2** | **`0x80101764`** |
| `CM_L4PER_UART1/2/3/4` `0x4A009540/548/550/558` | 2 each | `0x80101768-6c-70-74` |

**Second, independent path (new).**  The image contains an I²C client/driver whose init
(`0x80113838`) compares its physical base against `0x48070000/0x48072000/0x48060000/0x48350000`
and stores the matching `CM_L4PER_I2C1..4_CLKCTRL` address into its context:

```
801138f0  movw r3,#0
801138f4  movt r3,#0x4807          ; 0x48070000
801138fc/13904  → 0x4A0094A0        ; I2C1
8011390c/13914  → 0x4A0094A8        ; I2C2
80113928/13930  → 0x4A0094B0        ; I2C3
80113944/1394c  → 0x4A0094B8        ; I2C4
801139a8  str r0,[r4,#0xc]          ; ctx+0xc = that CLKCTRL address
```

and then programs it through `ctx+0xc`:

```
80114740  e5832000  str r2,[r3]     ; r2 = 2   → CM_L4PER_I2Cx_CLKCTRL = 2  (module ON)
801146cc  e5832000  str r2,[r3]     ; r2 = 0   → CM_L4PER_I2Cx_CLKCTRL = 0  (module OFF)
```

(§3 does not list these two targets because the base is a context field; the code path is
unambiguous and the same object model resolves `base+0x34` below.)

The other CM2 sub-blocks written: `CM_CAM_CLKSTCTRL=2` (`0x801017b4`), `CM_CAM_ISS_CLKCTRL=0x102`
(`0x801017b8`), `CM_CAM_FDIF_CLKCTRL=2` (`0x801017bc`), `CM_L3INIT_HSMMC1/2_CLKCTRL=0x01000002`
(`0x80101754/758`), `CM_L3INIT_HSI=1`, `HSUSBHOST=2`, `HSUSBOTG=1`, `HSUSBTLL=1`, `FSUSB=2`,
`USBPHY=1`, `CM_DSS_CLKSTCTRL=2`, `CM_DSS_DSS_CLKCTRL=0xF02`, `CM_SGX_CLKSTCTRL=2`,
`CM_SGX_SGX_CLKCTRL=0x01000002` — all in §3.

**Answer in one line:** MMCSD5's module clock is written to `0x2` exactly once, at `0x80101764`;
no other write to `0x4A009560` exists in the image.

---

## 5. Q2 — PRM L3INIT / L4PER (`0x4A307000-0x4A3072FF`, `0x4A307400-0x4A3075FF`)

**It does not.**  Three independent negative proofs:

1. **No constant.**  The complete constant ledger (§2) contains no value in
   `0x4A307300-0x4A3075FF`; the only `0x4A307xxx` constants are `0x4A307000` and `0x4A307B00`.
2. **No literal.**  A scan of every 4-byte word of the image for values in
   `0x4A307000-0x4A3072FF` and `0x4A307400-0x4A3075FF` returns **zero** hits (the same scan finds
   `0x4A318000` twice, so the scan is live).
3. **No store.**  The 182 resolved stores in `0x4A000000-0x4A31FFFF` contain nothing in either
   window; every store based on `0x4A307000` is at `+0x800` or above:

| VA | target | value | register (TRM) |
|---|---|---|---|
| `0x80101778` | `0x4A307800` | 3 | `CM_WKUP_CLKSTCTRL` |
| `0x80103374` | `0x4A307800` | read \| 3 | `CM_WKUP_CLKSTCTRL` |
| `0x801017ac` | `0x4A307830` | 2 | `CM_WKUP_WDTIMER2_CLKCTRL` |
| `0x8010177c` | `0x4A307838` | 1 | `CM_WKUP_GPIO1_CLKCTRL` |
| `0x80101780` | `0x4A307840` | `0x01000002` | `CM_WKUP_GPTIMER1_CLKCTRL` |
| `0x80103388` | `0x4A307840` | RMW → 2 | `CM_WKUP_GPTIMER1_CLKCTRL` |
| `0x801017b0` | `0x4A307878` | 0 | `CM_WKUP_KEYBOARD_CLKCTRL` |
| `0x80107778/77c` | `0x4A307BA0` | — / arg\|`0x01000000` | `PRM_VC_VAL_BYPASS` |
| `0x801009b0/1051f8/105a48` | `0x4A307BA8` | 0 | `PRM_VC_CFG_I2C_MODE` |
| `0x801009c0/105a58` | `0x4A307BAC` | `0x1E1A` | `PRM_VC_CFG_I2C_CLK` |
| `0x80105200` | `0x4A307BAC` | `0x6026` | `PRM_VC_CFG_I2C_CLK` |
| `0x80100ddc/101468` | `0x4A307BB4` | 1 | `PRM_SRAM_WKUP_SETUP` |
| `0x80100de4/10146c` | `0x4A307BB8` | 1 | `PRM_LDO_SRAM_CORE_SETUP` |
| `0x80100de8/101470` | `0x4A307BC0` | 1 | `PRM_LDO_SRAM_MPU_SETUP` |
| `0x80100dec/101474` | `0x4A307BC8` | 1 | `PRM_LDO_SRAM_IVA_SETUP` |
| `0x80107774/77c8` | `0x4A307BF8` | `0x073F3F3F` | `PRM_VC_ERRST` |

So **`RM_L4PER_MMCSD5_CONTEXT (0x4A307564)`, `PM_L4PER_MMCSD5_WKDEP (0x4A307560)` and
`PM_L3INIT_PWRSTCTRL (0x4A307300)` are never written by the boot path** — the only writer remains
`devnp-ti1283.so` (`0x4A307564 = 1`, report 180 §5).  Report 219's negative stands, now with the
constant-level and literal-level proof as well.

Note on the address map (TRM Table 3-599 / 3-564 / 3-792): PRM_L4PER lives at
`0x4A307400-0x4A3075FF`, PRM_L3INIT at `0x4A307300-0x4A3073FF`, and the **VP/VC block is not in
`0x4A307000-0x4A3072FF`** — it is `PRM_VC_*` at `0x4A307B88-0x4A307BF8` (PRM_DEVICE page).  The
`0x4A307000-0x4A3072FF` window holds `PM_CAM/DSS/SGX_PWRSTCTRL` and `RM_CAM/DSS/SGX_CONTEXT`.

---

## 6. Q3 — padconf `0x4A100050/058/074/078/07A/07C/148-152`, WKUP `0x4A31E058`

**No instruction in the image stores to a padconf address directly.**  Pad programming is entirely
table-driven, in one routine:

```
8010ccf4  pad_walk(rev=r0, phase=r1, table=r2, count=r3)
   record = 20 bytes; match on table[i].rev_min(+6) <= rev <= rev_max(+7) and phase(+8)==r1
   offset = A[id]         ; A = 231-word copy of 0x80129410 (entries 0..164 carry offsets)
   wkup   = W[id]         ; W = 231-word flag array built @ +0x73c (ids 4-10,29-31,193-210 = 1)
   half   = H[id]         ; H = 231-word table copied from 0x801297ac into the frame @ +4
   base   = 0x4A100000 + (wkup ? 0x21E000 : 0)          ; 0x4A31E000 for WKUP pads
   val    = AF(+4) | flagbits(+0x10)                    ; bits: 0→0x8 1→0x10 3→0x100 4→0x200
                                                        ;       5→0x400 7→0x800 9→0x1000
                                                        ;      10→0x2000 12→0x4000
   word   = [base+offset]; write half (H==0 low, H!=0 high) ; str @0x8010ceb4
```

`A` is a 165-entry map, pad id → padconf offset: ids 0-31 → `0x0A0-0x0E0`, 32-63 → `0x08C-0x11C`,
64-126 → `0x120-0x1D4`, 127-164 → `0x040-0x19C` (the GPMC group restarts at `0x040`).  The walker is called from
`0x80100e24` (§ board descriptor) four times: pad phase 1 (`0x80100e4c`), GPIO phase 1
(`0x80100e60`), pad phase 2 (`0x80100e90`), pad phase 3 (`0x80100eb8`).

The pad **record tables** are not statically referenced (the board descriptor is built at run time),
so they were identified by content — 20-byte stride, `id<165`, `AF<8`, `rev_min≤rev_max≤15`,
`phase∈1..3`, `flags` inside the mask above.  Thirty-six such runs of ≥4 records exist between
`0x80121544` and `0x801288E4`; they are listed at the end of the reachability table below.  Two of
them hold the SDMMC5 pads (ids 82-87) and are therefore the ones a WL1283 image must keep.

For each address in the question — "reachable" means *some* pad table contains a pad id whose
table entry maps to that word, i.e. the walker **can** write it on some board:

| address | reachable | pad id (half) | tables that contain it |
|---|---|---|---|
| `4a100050` | yes | 134 hi | `0x80121db4`, `0x80123a54`, `0x801267b8`, `0x80128394` |
| `4a100058` | **no** | - | - |
| `4a100074` | yes | 143 lo, 144 hi | `0x80123a54`, `0x80128394` |
| `4a100078` | **no** | - | - |
| `4a10007a` | **no** | - | - |
| `4a10007c` | **no** | - | - |
| `4a100148` | yes | 82 lo, 83 hi | `0x80122638`, `0x80123464`, `0x80127de0` |
| `4a10014a` | **no** | - | - |
| `4a10014c` | yes | 84 lo, 85 hi | `0x80122638`, `0x80123464`, `0x80127de0` |
| `4a10014e` | **no** | - | - |
| `4a100150` | yes | 86 lo, 87 hi | `0x80122638`, `0x80123464`, `0x80127de0` |
| `4a100152` | **no** | - | - |
| `4a31e058` | **no** | - | - |

pad tables (content-identified, 36):
`0x80121544`(14), `0x80121724`(4), `0x80121828`(4), `0x80121aa8`(6), `0x80121b34`(7), `0x80121d3c`(4), `0x80121db4`(6), `0x80122174`(4), `0x80122368`(4), `0x801224e4`(4), `0x80122638`(12), `0x8012316c`(4), `0x801231d0`(13), `0x801232fc`(7), `0x80123464`(58), `0x801239b4`(6), `0x80123a54`(25), `0x80123c5c`(12), `0x80123ff4`(18), `0x8012424c`(5), `0x80125f48`(14), `0x80126128`(4), `0x8012622c`(4), `0x801264ac`(6), `0x80126538`(7), `0x80126740`(4), `0x801267b8`(6), `0x80126b78`(4), `0x80126d6c`(4), `0x80127b38`(25), `0x80127de0`(33), `0x8012809c`(22), `0x80128308`(5), `0x80128394`(24), `0x80128588`(9), `0x801288e4`(18)

So, plainly:

* `0x4A100050` — **yes** (pad id 134, high half; tables `0x80121DB4`, `0x80123A54`, `0x801267B8`,
  `0x80128394`).
* `0x4A100058` — **no**.  There is no pad id mapping to `0x58` in `A`; no other writer exists.
* `0x4A100074` — **yes** (ids 143 low, 144 high; `0x80123A54`, `0x80128394`).
* `0x4A100078`, `0x4A10007A`, `0x4A10007C` — **no**.  `A` jumps from `0x074` to `0x080`.
* `0x4A100148` / `0x14A` — **yes for the word** `0x148` (ids 82 low, 83 high): this is
  `CONTROL_CORE_PAD0_SDMMC5_CLK…`, i.e. the WL1283 SDIO pads.  Writers: `0x80122638`,
  `0x80123464`, `0x80127DE0`.
* `0x4A10014C` / `0x14E` — **yes for the word** `0x14C` (ids 84, 85).
* `0x4A100150` / `0x152` — **yes for the word** `0x150` (ids 86, 87).
* `0x4A31E058` — **no**.  The WKUP ids that exist are 4-10 and 29-31, whose offsets are
  `0xA8-0xB4` and `0xDC-0xE0` → `0x4A31E0A8/AC/B0/B4` and `0x4A31E0DC/E0`.  Nothing maps to
  `0x4A31E058`; the `gpio_wk31` pad is therefore **not** muxed by this image (report 192's
  identification of `gpio 31 → 0x4A31E058` is about the board wiring, not a startup write).

Caveat, stated because it is the one soft spot: the *selection* of which pad table the descriptor
points at is runtime, so this answers "which padconf words the image can write", not "which ones the
1917 run wrote".  Both facts are recorded; no bench is used.

**Cross-check against report 220 (the GPIO1/pad audit).**  That report, from the live Linux side,
records that the *reference* has `0x4A100050`, `0x4A100058` and `0x4A100074` muxed to mode 3.
This report says `startup-playbook`'s own pad walker cannot write `0x4A100058` at all (no pad id in
`A` maps to `0x58`).  Both can be true: the mux of `0x058` (and of any pad) must then come from a
different reference binary — `hwio-config-winchester.so` in `/base` — not from `startup-playbook`.
The negative here is scoped to **this image** and does not contradict 220's live reading.

---

## 7. Q4 — GPIO1 (`0x4A310000`) and GPIO2 (`0x48055000`) direction/data

**Direct, constant-base writes — SYSCONFIG only:**

| target | value | VA |
|---|---|---|
| `GPIO1_SYSCONFIG 0x4A310010` | `read \| 1` | `0x801016e0` |
| `GPIO2_SYSCONFIG 0x48055010` | `read \| 1` | `0x801016ec` |
| `GPIO3_SYSCONFIG 0x48057010` | `read \| 1` | `0x801016f8` |
| `GPIO4_SYSCONFIG 0x48059010` | `read \| 1` | `0x80101708` |
| `GPIO5_SYSCONFIG 0x4805B010` | `read \| 1` | `0x80101714` |
| `GPIO6_SYSCONFIG 0x4805D010` | `read \| 1` | `0x80101724` |

(Bit 0 = `AUTOIDLE`.  The bases are `movw/movt` @`0x80101600-0x80101670`; the RMW is
`ldr`+`orr rX,rX,#1`+`str` at `0x801016d0-0x80101724`.)

**Direction / data / detect — table-driven only**, in `0x8010cb9c` (the function report 192
described).  Per matching record it writes, with the line bit `1 << (id & 31)`:

| register | offset | value | VA |
|---|---|---|---|
| `GPIO_OE` | `+0x134` | `read \| bit` when record `+4 == 0` (input) | `0x8010cc30` |
| `GPIO_OE` | `+0x134` | `read & ~bit` when record `+4 == 1` (output) | `0x8010cce8` |
| `GPIO_SETDATAOUT` | `+0x194` | `bit` when level byte `+8 != 0` | `0x8010ccd0` |
| `GPIO_CLEARDATAOUT` | `+0x190` | `bit` when level byte `+8 == 0` | `0x8010ccd4` |
| `GPIO_RISINGDETECT` | `+0x148` | `read \| bit` when `+0xc ∈ {1,3}` | `0x8010cc54` |
| `GPIO_FALLINGDETECT` | `+0x14C` | `read \| bit` when `+0xc ∈ {2,3}` | `0x8010cc6c` |
| `GPIO_DEBOUNCENABLE` | `+0x150` | `read \| bit` when `+0x12 != 0` | `0x8010cc8c` |
| `GPIO_DEBOUNCINGTIME` | `+0x154` | byte `+0x13` | `0x8010cc94` |
| `GPIO_IRQWAKEN_0` | `+0x44` | `read \| bit` when `+0x11 != 0` | `0x8010ccb0` |

**Polarity (checked, not assumed).**  TRM §25.4.8: "*When configured as an output (the desired bit
reset in the `GPIOi.GPIO_OE` register)…*" — so **OE bit 0 = output**, the OMAP4 inversion.  That
matches the code (`dir=1` → BIC OE) and the live values: the table's `dir=1` GPIO1 ids
(0,2,3,4,6,13,14,15,18,19,23,28,31) are exactly the live `GPIO1_OE` zero bits, except 3/19/23,
which a later driver re-touches.  Report 192's 1 = output is therefore **correct**; its reading of
"live QNX OE = exactly those lines as outputs" is also consistent once the inversion is applied.

Which bits (from the 75-record table, §9):
* GPIO1: ids `0,2,3,4,6,7,8,9,10,13,14,15,18,19,20,21,22,23,24,25,26,28,29,31`
  (outputs: 0,2,3,4,6,13,14,15,18,19,23,28,31; inputs: 7,8,9,10,20,21,22,24,25,26,29).
* GPIO2: ids `32,33,34,35,36,37,38,39,40,41,44,45,46,47,48,49,50,51,52,53,54,55,56,59,60,61,62`
  (outputs: 37,39,41,44,45,48,54,55,56,60; **WL_EN = 54**, **WL_IRQ = 53** input+falling+wake,
  **BT_EN = 55** output-low).
* GPIO3/4/5/6: ids 83,96,97,100,101,102,103,104,124,127,138,161,171,172,173,174,175,176,
  177,178,190 as listed in §9.

The **bank→base** step is `base = 0x48055000 + bank_table[bank]` (`0x8010cc10-0x8010cc18`), where
`bank = id >> 5`.  The 6-entry table is at `0x801293F8` in the image; the values stored there
(`0x8C,0x98,0x98,0x9C,0x9C,0xA0`) cannot be bank bases, and the second GPIO routine
(`0x8010cec4`) instead indexes a **zero-filled** table at `0x8013EB60` (literal @`0x8010D010`),
i.e. the bank-base table is a run-time (BSS) object.  This is the one piece of Q4 that is not
statically resolvable — flagged rather than guessed.  For bank 0 the table must hold `0x022BC000`
(`0x48055000 + 0x22BC000 = 0x4A310000`); no such word exists in the image, confirming the table is
filled at run time.

* `GPIO_DATAIN` is **read** only twice with a constant base: `0x48057138` (GPIO3_6) @`0x80104460`
  and `0x4805D138` (GPIO6_6) @`0x801043e8` — a bit-6 strap read, not a WL line.  No GPIO1 or
  GPIO2 data register is read or written by this image outside the table walker.

**Two further GPIO touches through runtime bases** (not constant-base, so not in §3):
`0x80107D20` `SETDATAOUT(+0x194) = 0x20000000` then `OE(+0x134) &= ~0x20000000` (bit 29, the
"take touch pad out of reset" path), and `0x80107D38/0x80107D5C` clear it again; `0x80107D40`
`OE &= ~0x2000` (bit 13).  Their bank comes from the same board struct.

---

## 8. Q5 — I²C transactions

The image contains an I²C **client** library plus the OMAP HS-I2C register layer.  Transactions are
built at the call sites; the controller is programmed through `ctx+4`/`ctx+0xc`.

**Buses opened** (`i2c_init` = `0x80113838`, `open` → `devctl`-style client):

| VA | base | result |
|---|---|---|
| `0x80100ad4` | `0x48070000` (**I²C1**) | TWL path A |
| `0x80100d70` | `0x48070000` (I²C1) | TWL path B |
| `0x80107ac8` | `0x48070000` (I²C1) | fuel-gauge/VANA path |
| `0x80107cf4` | `0x48350000` (**I²C4**) | device `0x36` |

**I²C2 (`0x48072000`) and I²C3 (`0x48060000`) are never opened** — they appear only as literals in
the sweep and in the base→`CM_L4PER_I2Cn_CLKCTRL` compare (`0x80101ed0`, `0x80101f54`).

**Transactions (slave ← register, value):**

| # | bus | slave | operation | VA (call) | evidence |
|---|---|---|---|---|---|
| 1 | I²C1 | `0x4A` | `0x07 ← 0x11`, `0x08 ← 0xF0` (3-byte write from reg 7) | `0x80100af8` set-slave, `0x80100b24` send | buffer `strb` @`0x80100b10/14/1c` (`sp+0x408..40A`) |
| 1b | I²C1 | `0x4A` | `0x07 ← 0x11`, `0x08 ← 0x7D` (alternate, when rev word == 8) | `0x80100b58`, `0x80100b84` | buffer `0x80100b70/74/7c`; `cmp r3,#8; beq 0x80100b54` @`0x80100aec` |
| 2 | I²C1 | `0x48` | `0x2C ← 0x7F` | `0x80100d80`, `0x80100d90` | `mov r1,#0x2c; mov r2,#0x7f` @`0x80100d88/8c` |
| 3 | I²C1 | `0x48` | `0x2C ← 0x7F` | `0x80107adc`, `0x80107aec` | `0x80107ae0/7e8` |
| 4 | I²C1 | `0x55` | **read 8 bytes** (fuel gauge; compared with `0xD00` = 3328 mV) | `0x80107af8`, `0x80107b04` | `mov r1,#8`; `ldrh r3,[r0]; cmp r3,#0xd00` @`0x80107b20/24` |
| 5 | I²C1 | `0x48` | `0x2C ← 0x7F` (retry path / GPIO1_6 drop) | `0x80107b58`, `0x80107b68` | `0x80107b5c/60/64` |
| 6 | I²C1 | `0x48` | `0x80 ← 0x00`, `0x82 ← 0x01` (VANA base / base+2) | `0x80107b9c`, `0x80107bac`, `0x80107bbc` | `0x80107ba0/ba4`, `0x80107bb0/bb4` |
| 7 | I²C1 | `0x49` | **read `0xE3`** | `0x80107bc8`, `0x80107bd4` | `mov r1,#0xe3` |
| 8 | I²C1 | `0x48` | `0x25 ← 0x07` | `0x80107dd8` | `mov r1,#0x25; mov r2,#7` |
| 9 | I²C4 | `0x36` | **read `0x10`**, modify (`\|6` when r0=1, `&0xF9` when r0=0), **write `0x10`**; done twice with 500/250-tick gaps | `0x80107d98` → `0x80107a24` (r0=1), `0x80107da8` → `0x80107a24` (r0=0) | read @`0x80107a48`, write @`0x80107a70`; `orreq r2,r2,#6` / `andne r2,r2,#0xf9` @`0x80107a64/68`; delays `0x80107d9c` (0x1F4), `0x80107dac` (0xFA) |
| 10 | I²C4 | `0x36` | **read `0x10`** then **write `0x10 ← 0x00`** (the first pass) | `0x80107d08`, `0x80107d6c`, `0x80107d84` | `mov r1,#0x36`; `mov r1,#0x10`; `mov r2,r6(0)` |

Controller registers the driver touches (base = `ctx+4`, all from the same object model):
`base+0x10 = I2C_SYSC` ← `1` @`0x801146c0` and ← `0x21D` @`0x801147cc`;
`base+0x34 = I2C_WE` ← `0x6F6F` @`0x80113a20` (resolved for `0x48070034` and `0x48350034`).

**Which of these run on a given boot** is gated by `open()` return values and by the revision
compare; §8 lists them with their guards so they can be replayed either way.  Nothing here writes a
TWL resource that report 219 did not already name; the only additions are the alternate
`{07,11,7D}` write and the two `0x48:0x25 ← 7` / `0x48:0x80,0x82` sequences in the second path.

---

## 9. Q6 — DPLL / PRM VP-VC-SR

**DPLLs.**  Only `DPLL_MPU` is touched, in `CM1`:

| VA | target | value | meaning (TRM §3.7.4.5.4, `DPLL_EN[2:0]`) |
|---|---|---|---|
| `0x80100fb8` | `CM_CLKMODE_DPLL_MPU 0x4A004160` | `read & ~7 \| 5` | idle low-power bypass (unlock) |
| `0x80101010` | `CM_CLKMODE_DPLL_MPU 0x4A004160` | `read \| 7` | lock (enable) |
| `0x8010c8c4` | `CM_CLKMODE_DPLL_MPU 0x4A004160` | `read & ~7 \| 4` | MN bypass (unlock, helper) |
| `0x80101000` | `CM_CLKSEL_DPLL_MPU 0x4A00416C` | `(r6 & ~0xFFF00) \| (M << 8)` | M/N programming |
| `0x8010c8f8` | `CM_DIV_M3_DPLL_CORE 0x4A004134` | `0x13` | core M3 divider |
| `0x8010c904` | `CM_DIV_M3_DPLL_CORE 0x4A004134` | `read \| 0x100` | `CLKOUTX2_M3` |

No write to `CM_CLKMODE_DPLL_CORE/PER/IVA/ABE/USB/UNIPRO` or to any `CM_CLKSEL_DPLL_*` other than
MPU exists in the image (the constant ledger contains only `0x4A004000`, `0x4A004134`, `0x4A004160`;
`0x4A004134/160/16C` are the only sub-addresses used).

Clock-source side effects, also in §3: `CM_SYS_CLKSEL (0x4A306110) = 5` @`0x80100884`
(and `= 7` @`0x80100cdc` / `0x8010786c`), `CM_ABE_PLL_REF_CLKSEL (0x4A30610C) = 0` @`0x8010161c`,
`AUXCLK1 (0x4A30A314) = 0x00010000` @`0x80100914`, `AUXCLK1/3/4` @`0x80107990/79b0/80100d20/801079c0`,
`PMICSETUPTIME (0x4A30A104)` @`0x80100a48/105288/105ae0`.

**PRM VP/VC/SR.**  `PRM_VC_*` is written (`PRM_VC_CFG_I2C_MODE=0`, `PRM_VC_CFG_I2C_CLK=0x1E1A/0x6026`,
`PRM_VC_VAL_BYPASS`, `PRM_VC_ERRST=0x073F3F3F`, `PRM_SRAM_WKUP_SETUP` and the three
`PRM_LDO_SRAM_*_SETUP = 1`).  **No `PRM_VP_*` register (`0x4A307B40-0x4A307B84`) is written, and no
SmartReflex register** (`PRM_SR_*` — not present in this TRM's PRM map) is written.  The voltage
controller is configured but no voltage is commanded by this image; the actual VP/VC sequencing is
`setup-core-inactive` (report 219 §5).

**Q6 in one line:** it unlocks and locks `DPLL_MPU`, programs its M/N and the core M3 divider, and
configures `PRM_VC_*`; it does not touch any other DPLL, any `PRM_VP_*`, or SmartReflex.

---

## 10. Q7 — the GPIO table at `0x80122750` (75 × 32 B, ends `0x801230B0`)

Record layout, read from the walker `0x8010cb9c` (`lsl r5,r5,#5` → 32 B stride):

| offset | size | field | how the code uses it |
|---|---|---|---|
| `+0x00` | u8 | gpio id = `bank*32 + line` | `ldrb sl,[r2,r5]` @`0x8010cc00`; `bank=id>>5`, `line=id&31` |
| `+0x04` | u32 | direction: **1 = output, 0 = input** | `cmp r8,#1; beq 0x8010ccb8` @`0x8010cc0c` |
| `+0x08` | u32 | output level (0 → CLEARDATAOUT, else SETDATAOUT) | `ldrb sb,[r4,#8]` @`0x8010ccb8` |
| `+0x0C` | u32 | edge detect: 1 rising, 2 falling, 3 both | `0x8010cc34-0x8010cc6c` |
| `+0x11` | u8 | wake enable (→ `IRQWAKEN_0`) | `0x8010cc98-0x8010ccb0` |
| `+0x12` | u8 | debounce enable | `0x8010cc70-0x8010cc8c` |
| `+0x13` | u8 | debounce time | `0x8010cc90-0x8010cc94` |
| `+0x14` | u8 | rev min | `ldrb r6,[r4,#0x14]` @`0x8010cbc0` |
| `+0x15` | u8 | rev max | `0x8010cbcc` |
| `+0x16` | u8 | phase | `0x8010cbd8` |
| `+0x18` | u32 | signal/pad id (opaque; not used by this walker) | — |

Match rule: `rev_min <= rev <= rev_max && phase == arg`.  The three Winchester-phase records at
`+0x14..+0x16 = 0..15/1` apply; the table also carries rev-qualified variants (id 19 has four,
ids 23/24/25/26/161 have narrowed rev ranges).

All 75 records:

| # | gpio | bank:line | dir | lvl | det | wake | deb | rev | ph | sig | VA |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 173 | GPIO6_13 | in | 0 | 0 | 0 | - | 0-15 | 0 | 0x00 | 0x80122750 |
| 1 | 174 | GPIO6_14 | in | 0 | 0 | 0 | - | 0-15 | 0 | 0x00 | 0x80122770 |
| 2 | 177 | GPIO6_17 | in | 0 | 0 | 0 | - | 0-15 | 0 | 0x00 | 0x80122790 |
| 3 | 178 | GPIO6_18 | in | 0 | 0 | 0 | - | 0-15 | 0 | 0x00 | 0x801227b0 |
| 4 | 0 | GPIO1_0 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x00 | 0x801227d0 |
| 5 | 18 | GPIO1_18 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0xac | 0x801227f0 |
| 6 | 35 | GPIO2_3 | in | 0 | 2 | 1 | - | 0-15 | 1 | 0x16 | 0x80122810 |
| 7 | 39 | GPIO2_7 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x0e | 0x80122830 |
| 8 | 41 | GPIO2_9 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x2f | 0x80122850 |
| 9 | 44 | GPIO2_12 | out | 1 | 0 | 0 | - | 0-15 | 1 | 0x93 | 0x80122870 |
| 10 | 45 | GPIO2_13 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x98 | 0x80122890 |
| 11 | 48 | GPIO2_16 | out | 1 | 0 | 0 | - | 0-15 | 1 | 0x8b | 0x801228b0 |
| 12 | 49 | GPIO2_17 | in | 0 | 3 | 0 | - | 0-15 | 1 | 0x94 | 0x801228d0 |
| 13 | 53 | GPIO2_21 | in | 0 | 2 | 1 | - | 0-15 | 1 | 0x59 | 0x801228f0 |
| 14 | 54 | GPIO2_22 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x58 | 0x80122910 |
| 15 | 55 | GPIO2_23 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x5d | 0x80122930 |
| 16 | 56 | GPIO2_24 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x9a | 0x80122950 |
| 17 | 60 | GPIO2_28 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x2e | 0x80122970 |
| 18 | 83 | GPIO3_19 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x22 | 0x80122990 |
| 19 | 101 | GPIO4_5 | out | 1 | 0 | 0 | - | 0-15 | 1 | 0x14 | 0x801229b0 |
| 20 | 104 | GPIO4_8 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x15 | 0x801229d0 |
| 21 | 124 | GPIO4_28 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x00 | 0x801229f0 |
| 22 | 127 | GPIO4_31 | out | 1 | 0 | 0 | - | 0-15 | 1 | 0x92 | 0x80122a10 |
| 23 | 175 | GPIO6_15 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0x9f | 0x80122a30 |
| 24 | 176 | GPIO6_16 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0xa0 | 0x80122a50 |
| 25 | 31 | GPIO1_31 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x9e | 0x80122a70 |
| 26 | 32 | GPIO2_0 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0x0a | 0x80122a90 |
| 27 | 33 | GPIO2_1 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0xa1 | 0x80122ab0 |
| 28 | 36 | GPIO2_4 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0x9b | 0x80122ad0 |
| 29 | 37 | GPIO2_5 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x9c | 0x80122af0 |
| 30 | 38 | GPIO2_6 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0x8d | 0x80122b10 |
| 31 | 50 | GPIO2_18 | in | 0 | 1 | 1 | - | 0-15 | 1 | 0x3a | 0x80122b30 |
| 32 | 51 | GPIO2_19 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0x95 | 0x80122b50 |
| 33 | 52 | GPIO2_20 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0x96 | 0x80122b70 |
| 34 | 59 | GPIO2_27 | in | 0 | 1 | 1 | - | 0-15 | 1 | 0xa2 | 0x80122b90 |
| 35 | 62 | GPIO2_30 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0xa5 | 0x80122bb0 |
| 36 | 100 | GPIO4_4 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0xa6 | 0x80122bd0 |
| 37 | 103 | GPIO4_7 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0xa7 | 0x80122bf0 |
| 38 | 171 | GPIO6_11 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0x9d | 0x80122c10 |
| 39 | 172 | GPIO6_12 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0x97 | 0x80122c30 |
| 40 | 13 | GPIO1_13 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0xaa | 0x80122c50 |
| 41 | 20 | GPIO1_20 | in | 0 | 3 | 0 | 1/255 | 0-15 | 1 | 0x11 | 0x80122c70 |
| 42 | 21 | GPIO1_21 | in | 0 | 3 | 0 | 1/255 | 0-15 | 1 | 0x09 | 0x80122c90 |
| 43 | 22 | GPIO1_22 | in | 0 | 3 | 0 | 1/255 | 0-15 | 1 | 0x12 | 0x80122cb0 |
| 44 | 28 | GPIO1_28 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x8c | 0x80122cd0 |
| 45 | 6 | GPIO1_6 | out | 1 | 0 | 0 | - | 0-15 | 1 | 0x08 | 0x80122cf0 |
| 46 | 7 | GPIO1_7 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0x00 | 0x80122d10 |
| 47 | 8 | GPIO1_8 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0x00 | 0x80122d30 |
| 48 | 29 | GPIO1_29 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0xae | 0x80122d50 |
| 49 | 9 | GPIO1_9 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0x00 | 0x80122d70 |
| 50 | 10 | GPIO1_10 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0x00 | 0x80122d90 |
| 51 | 4 | GPIO1_4 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0xad | 0x80122db0 |
| 52 | 96 | GPIO4_0 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x00 | 0x80122dd0 |
| 53 | 97 | GPIO4_1 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x00 | 0x80122df0 |
| 54 | 102 | GPIO4_6 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x8e | 0x80122e10 |
| 55 | 2 | GPIO1_2 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0xa8 | 0x80122e30 |
| 56 | 3 | GPIO1_3 | out | 1 | 0 | 0 | - | 0-15 | 1 | 0x00 | 0x80122e50 |
| 57 | 14 | GPIO1_14 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0xab | 0x80122e70 |
| 58 | 15 | GPIO1_15 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0xa9 | 0x80122e90 |
| 59 | 19 | GPIO1_19 | out | 1 | 0 | 0 | - | 0-9 | 1 | 0x00 | 0x80122eb0 |
| 60 | 19 | GPIO1_19 | out | 0 | 0 | 0 | - | 10-10 | 1 | 0x00 | 0x80122ed0 |
| 61 | 19 | GPIO1_19 | out | 1 | 0 | 0 | - | 11-11 | 1 | 0x00 | 0x80122ef0 |
| 62 | 19 | GPIO1_19 | out | 0 | 0 | 0 | - | 12-15 | 1 | 0x00 | 0x80122f10 |
| 63 | 161 | GPIO6_1 | out | 0 | 0 | 0 | - | 10-15 | 1 | 0x00 | 0x80122f30 |
| 64 | 25 | GPIO1_25 | in | 0 | 3 | 1 | - | 10-15 | 1 | 0xb0 | 0x80122f50 |
| 65 | 26 | GPIO1_26 | in | 0 | 3 | 1 | - | 0-15 | 1 | 0xaf | 0x80122f70 |
| 66 | 23 | GPIO1_23 | out | 0 | 0 | 0 | - | 12-15 | 1 | 0x00 | 0x80122f90 |
| 67 | 24 | GPIO1_24 | in | 0 | 1 | 1 | - | 12-15 | 1 | 0x00 | 0x80122fb0 |
| 68 | 34 | GPIO2_2 | in | 0 | 2 | 1 | - | 0-15 | 1 | 0x0f | 0x80122fd0 |
| 69 | 40 | GPIO2_8 | in | 0 | 0 | 0 | - | 0-15 | 1 | 0x01 | 0x80122ff0 |
| 70 | 46 | GPIO2_14 | in | 0 | 1 | 1 | - | 0-15 | 1 | 0x0b | 0x80123010 |
| 71 | 47 | GPIO2_15 | out | 0 | 0 | 0 | - | 0-15 | 1 | 0x0d | 0x80123030 |
| 72 | 61 | GPIO2_29 | out | 1 | 0 | 0 | - | 0-15 | 1 | 0xa4 | 0x80123050 |
| 73 | 190 | GPIO6_30 | in | 0 | 1 | 1 | - | 0-15 | 1 | 0xb1 | 0x80123070 |
| 74 | 138 | GPIO5_10 | in | 0 | 3 | 0 | - | 0-15 | 1 | 0x00 | 0x80123090 |

Record 0-3 are bank 5 (GPIO6), then the table is ordered by function, not by id.

---

## 11. What a minimal image would skip

A minimal “bring up WL1283” IFS could drop, without touching anything the SDIO path needs:

* **The entire CM1/CM2/PRM sweep's peripheral half** — `CM1_ABE_*` (SLIMBUS `0xF02` included),
  `CM_DSP_*`, `CM_IVAHD_*`, `CM_SDMA_*`, `CM_MEMIF_*`, `CM_CAM_*`, `CM_DSS_*`, `CM_SGX_*`,
  `CM_L3INIT_HSUSB*/FSUSB`, the GPTIMER/UART/MCSPI/MCBSP/HDQ clocks.  Keep only
  `CM_L4PER_CLKSTCTRL=2`, `CM_L4PER_MMCSD5_CLKCTRL=2`, the GPIO2..6 clocks (GPIO2 is WL_IRQ) and
  `CM_WKUP_*`.
* **The I²C client stack and every TWL write it makes** (§8) — it costs nothing to keep, but
  nothing in it is *needed* by the SDIO host: the only rail-ish item is `0x48:0x80/0x82`
  (VANA group/state) and the `0x4A` write, and both are testable independently.
* **WDT2 reprogramming** (`0x801036a4-0x801037e0`) — a boot-policy action, not a WL item; keep the
  `0xAAAA/0x5555` disable if a watchdog is armed by the ROM.
* **The USB/OTG/PHY block** (`0x4A064010/40`, `0x4A0AB404/40C`, `0x4A009358/360/368/3E0`),
  **SDMA** (`0x4A05602C/078`), **MCSPI1** (`0x48098110…`, 0x30A/`0xFFFFFFFF`/CH0CONF), the
  **PBIASLITE/MMC1/I2C0 pad-control block** (`0x4A100600/604/628` — these are display/MMC1
  biases, not SDMMC5), and the **UART/SPI driver inits** at `0x801018d8+`.
* **The pad and GPIO tables** are the one thing not to drop blindly:
  `CONTROL_CORE_PAD0_SDMMC5_*` (`0x4A100148/14C/150`, table ids 82-87) and `gpio 53/54/55`
  (WL_IRQ/WL_EN/BT_EN) come from them.  Everything else in the two tables (GPMC, display, keys,
  sensors) is skippable.

What a minimal image **must not** lose, from this image's own actions: `CM_L4PER_MMCSD5_CLKCTRL=2`
(0x80101764), `CM_L4PER_CLKSTCTRL=2` (0x8010169c), `CM_L3INIT_HSMMC1/2=0x01000002`
(0x80101754/758) if the other MMC controllers are used, the SDMMC5 pads and the GPIO2/54/53 rows.
Everything else the boot path does to power/clock before `wifi.sh` is either matched on Linux or
duplicated by the driver.

---

## 12. Limitations

* Branch-target/argument resolution is a fixpoint over a linear sweep, not a full CFG.  Stores that
  depend on a heap pointer or on a value produced by a call with no symbolic return stay
  unresolved (1 518 stores, 0 of them in the requested windows by constant scan).
* The GPIO bank-base table and the pad-table selection are runtime objects
  (`0x8013EB60` is zero in the image).  The per-line fields, bits and register offsets are exact;
  the bank→address binding is not statically recoverable and is marked as such in §7.
* `setup-core-inactive` (report 219 §5) is a separate ELF and is **not** covered here; it is the
  only other pre-`wifi.sh` actor that programs DPLL/VP/VC.
* The WDT `WCLR` value is whatever the preceding `WWPS` read returned (`0` on an idle timer);
  the code at `0x80103734` stores that register, not an immediate.

---

## 13. Provenance

* Image and hash: `local/work/s74/startup-playbook.bin` (`bc7d3a41…`), from
  `local/work/ifs1917.imagefs` @`0x908`+`0x44008` (report 192 §1).
* Disassembly: `local/work/s74/startup-playbook.dis` (capstone 5.0.7; objdump has no ARM backend
  on this host).
* Inventory: `local/work/s74/inventory.json`, `stores.txt`.
* TRM: `hardware/swpu231ap.pdf.txt`, tables cited inline; parser `trmparse.py` → `trm_regs.json`.
* Reports 180, 185, 189, 190, 191, 192, 217, 219.
