# SGX540 (TI OMAP4430/4460/4470) — fact sheet

Draft for the upstream `powervr_sgx` project's `docs/porting/cores/sgx540.md`.
It collects what our sources say, compares the core with the SGX545 the open
stack already runs, and lists what a hardware test must confirm before any of it
goes into the driver tables.  **Nothing here has been read from the SGX540's
GPU registers on our unit yet** (see §Gaps); it is the K/L groundwork for that
read.

Method and tags are the project's (see the upstream `docs/porting/state.md`): every fact carries
a tag and stays below **C** until our own hardware read confirms it.

| Tag | Meaning |
|---|---|
| **L** | Mainline Linux 7.3.3: the device tree, its binding, or a driver. |
| **K** | The GPL kernel side of the SGX DDK 1.17.4948957 we vendor: its register, feature, errata and MMU headers. It says what that DDK programs, not the silicon itself. |
| **D** | Derived from L or K, often with the SGX545/530 results. |
| **C** | Confirmed on our SGX540 hardware. **None yet in this sheet.** |
| **G** | A guess. |
| **U** | Unknown. |

## Sources

- **L:** `Documentation/devicetree/bindings/gpu/img,powervr-sgx.yaml` (this tree),
  `arch/arm/boot/dts/ti/omap/omap4.dtsi`, `omap443x.dtsi`, `drivers/bus/ti-sysc.c`.
- **K, SGX540:** the GPL-2 headers of DDK 1.17.4948957:
  `sgx540defs.h`, `sgxfeaturedefs.h`, `sgxerrata.h`, `sgxmmu.h` (vendored at
  `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz`).
- **Upstream method:** `powervr_sgx` `docs/porting/{README,kernel}.md`,
  `cores/sgx530.md` (the closest worked example).
- No closed userspace, firmware or non-GPL vendor file was used.

The DDK's register headers list only the registers the host kernel touches.  The
TA, 3D, DPM, ISP, TSP, PBE and PDS **state** registers the microkernel and the
command streams program are not in them; everything about those is **U**.

## Identity

| Fact | Value | Tag |
|---|---|---|
| DT compatible | `"ti,omap4430-gpu", "img,powervr-sgx540"` (binding lists OMAP4430 as **Rev 120**) | L |
| `CORE_ID` offset | **0x0020**; config b0-15, id b16-31 | K |
| `CORE_REVISION` offset | **0x0024**; maintenance b0-7, minor b8-15, major b16-23, designer b24-31 | K |
| Expected `CORE_REVISION` | `0x00010200` (1.2.0, from "Rev 120" as decimal digits — same rule that reads the SGX545's `1014` as `0x0001000e`) | D |
| Expected `CORE_ID` id field | unknown (the SGX545's is `0x0115`) | U |
| `DESIGNER_REV_FIELD1`, `FIELD2` | 0x0028, 0x002c | K |
| DDK errata for SGX540 | BRN 26620, 28011, 34028, 29954, 29997, 30954, 31093, 31195, 31272, 31278, 31542, 31620, 31780 — **revision-dependent** (the header selects by `SGX_CORE_REV`) | K |

## Register map differences from the SGX545

Offsets from the start of the GPU's register window.  Unlike the SGX530, the
SGX540 shares the SGX545's **clock-gating and reset layout**; the identity
registers move and the address space/memory-context model differs (§BIF).

| Register | SGX540 | SGX545 | Tag |
|---|---|---|---|
| `CLKGATECTL` | 0x0000 | 0x0000 | K |
| `CLKGATECTL2` | 0x0004 | 0x0004 | K |
| `CLKGATESTATUS` | 0x0008 | 0x0008 | K |
| `CLKGATECTLOVR` | 0x000c | 0x000c | K |
| `CORE_ID` | **0x0020** | 0x001c | K |
| `CORE_REVISION` | **0x0024** | 0x0020 | K |
| `DESIGNER_REV_FIELD1/2` | **0x0028 / 0x002c** | 0x0024 / 0x002c | K |
| `EVENT_KICK1` / `EVENT_KICK2` | **0x0ab0 / 0x0ac0** | 0x0ae4 / 0x0ae8 | K |
| `EVENT_KICKER` | 0x0ac4 (address b4-27) | 0x0ac4? (32-bit) | K |
| `PDS_EXEC_BASE` | 0x0ab8 | 0x0ab8 | K |

`CORE_ID` and `CORE_REVISION` moved by 4, so a driver that hard-codes the SGX545
offsets reads the wrong words on this core.  The bus must name the core (the DT
compatible does).

## Clock gating

| | SGX540 | Tag |
|---|---|---|
| Gated units (`CLKGATECTL`, 0x0000) | ISP b0-1, ISP2 b2-3, TSP b4-5, TE b6-7, MTE b8-9, DPM b10-11, VDM b12-13, PDS b14-15, IDXFIFO b16-17, TA b18-19, system b28; auto/manual b24 | K |
| `CLKGATECTL2` (0x0004) | PBE b0-1, CACHEL2 b2-3, UCACHEL2 b4-5, USE0 b6-7, ITR0 b8-9, TEX0 b10-11, … | K |
| `CLKGATESTATUS` (0x0008) | per-unit status | K |
| `CLKGATECTLOVR` (0x000c) | the override layout | K |
| Automatic clock gating | `SGX_FEATURE_AUTOCLOCKGATING` | K |

This is the SGX545's fine-grained split, not the SGX530's coarse one — the
`tools/state/busysplit.py` groups should map with little change (D).

## Reset

`SOFT_RESET` at 0x0080 (K), the modern unit set:

| Bit | Unit |
|---|---|
| 0 | BIF |
| 1 | VDM |
| 2 | DPM |
| 3 | TE |
| 4 | MTE |
| 5 | ISP |
| 6 | ISP2 |
| 7 | TSP |
| 8 | PDS |
| 9 | PBE |
| 10 | CACHEL2 |
| … | more above |

## Events and kicks

* `EVENT_STATUS` (0x012c), `EVENT_HOST_ENABLE`/`CLEAR` (0x0130/0x0134),
  `EVENT_STATUS2`/`HOST_ENABLE2`/`HOST_CLEAR2` (0x0118/0x0110/0x0114).  The
  bit positions follow the SGX545 for the shared events (K); the per-bit list is
  to be transcribed before use.
* **Multi-event kick:** `EVENT_KICK1` 0x0ab0 (`NOW` b0-7), `EVENT_KICK2` 0x0ac0
  (`NOW` b0), `EVENT_KICK` 0x0ac8 (`NOW` b0), `EVENT_KICKER` 0x0ac4 (address
  b4-27), `EVENT_TIMER` 0x0acc (`ENABLE` b24, `VALUE` b0-23).
  `SGX_FEATURE_MULTI_EVENT_KICK` is set — the microkernel start can use
  `EVENT_KICK2` (the SGX530 cannot).
* `EVENT_KICKER`'s address is **28-bit** (b4-27), matching the device address
  space (§BIF).

## The BIF and the MMU

| Fact | SGX540 | Tag |
|---|---|---|
| Device address space | **28-bit** (`SGX_FEATURE_ADDRESS_SPACE_SIZE 28`; `BIF_FAULT` addr b12-27, `*_REQ_BASE` b20-27) | K |
| Memory contexts | **one**: `DIR_LIST_BASE0` 0x0c84; **no `MULTIPLE_MEM_CONTEXTS`**, no `BIF_NUM_DIRLISTS`, no per-requestor bank registers | K |
| Page size | variable: PDE page-size bits `0x0e` → 4K/16K/64K/256K/1M/4M (`sgxmmu.h`) | K |
| PTE flags | valid b0, write-only b1, read-only b2, cache-consistent b3, EDM-protect b4 | K |
| PDE flags | valid b0, page size b1-3, … | K |
| `BIF_CTRL` 0x0c00, `BIF_INT_STAT` 0x0c04, `BIF_FAULT` 0x0c08, `BIF_MEM_REQ_STAT` 0x0ca8, `BIF_TA_REQ_BASE` 0x0c90, `BIF_3D_REQ_BASE` 0x0cac, `BIF_ZLS_REQ_BASE` 0x0cb0 | present | K |

So the MMU has the SGX545's page-size machinery but a **28-bit, single-context**
view — the SGX530's address-space class (§"What this means").

## The USSE and its code base

* The SGX540 runs the **USSE (first-generation)** instruction set, the same
  family as the SGX545 — so the open stack's NIR→USSE compiler is the right one
  (D).
* **4 USE pipes** (`SGX_FEATURE_NUM_USE_PIPES 4`), like the SGX545 (K).
* `SGX_FEATURE_USE_NO_INSTRUCTION_PAIRING` and `SGX_FEATURE_USE_UNLIMITED_PHASES`
  are **not** set for the SGX540 (K) — the SGX545 sets both.  What that means
  for encodings/scheduling is **U** (the feature names suggest the SGX540 pairs
  instructions and limits program phases, as the SGX530 does) (G).
* **`USE_CODE_BASE`: conflicting.**  `sgx540defs.h` **defines**
  `EUR_CR_USE_CODE_BASE(X) = 0x0A0C + 4X`, 16 entries, address b0-23, data-master
  b24-25 (K).  The upstream `kernel.md` says "535 and 540 have no `USE_CODE_BASE`
  registers".  One of the two is wrong for our rev-120 core; a hardware read
  settles it (U).  If present, the SGX545-style code-base window works as-is;
  if absent, the 535-style path is needed.
* **Monolithic microkernel: not set** for the SGX540 (K) — the open microkernel
  is not the monolithic variant.

## Other features

Set for the SGX540 in the DDK (K): `MULTI_EVENT_KICK`, `AUTOCLOCKGATING`,
`PT_CACHE_ENTRIES_PER_LINE 1`, 28-bit address space, 4 USE pipes.

**Not** set for the SGX540 (K), i.e. like the SGX530 and unlike the SGX545:
`MULTIPLE_MEM_CONTEXTS`, `MONOLITHIC_UKERNEL`, volume textures, the
texture-stride extension, host allocation from the DPM,
`PDS_DATA_INTERLEAVE_2DWORDS`, external Z for the ZLS, native backward blits,
512 TA render targets, the write-back DCU,
`BIF_WIDE_TILING_AND_4K_ADDRESS`, the full-range vertex PDS address.

Whether the unlisted 2D unit and other blocks are useful is **U**.

## Limits

Unknown until a hardware test or dEQP limit query; the SGX545 column is from its
fact sheet.  All SGX540 entries are **U**.

| Limit | SGX545 | SGX540 |
|---|---|---|
| Max texture size | 2048 | U |
| Max render target | kernel `rt_max_*` | U |
| Cube levels | 12 | U |
| Samplers | 8 | U |
| VS in/out, FS in | 16 / 11 / 10 | U |
| Temporaries | 64 | U |
| USE pipes | 4 | **4** (K) |
| Device address space | 4 GiB | **256 MiB** (K) |

## The OMAP glue

| Fact | Value | Tag |
|---|---|---|
| Interconnect module | `sgx_module: target-module@56000000`, `"ti,sysc-omap4", "ti,sysc"` | L |
| Module registers | `rev` at 0x5600fe00, `sysc` at 0x5600fe10 | L |
| GPU node | `gpu@0`: reg `<0x0 0x2000000>` (**32 MiB**), `interrupts = <GIC_SPI 21 IRQ_TYPE_LEVEL_HIGH>` | L |
| Power domain | `power-domains = <&prm_gfx>` (the `sgx_pwrdm`/GFX island) | L |
| Clock | parent `l3_gfx_clkctrl OMAP4_GPU_CLKCTRL` `fck`; `omap443x.dtsi` assigns it to **`dpll_per_m7x2_ck` at 307.2 MHz** | L |
| Who manages clocks/power | the parent `ti-sysc` module enables them (the binding forbids `clocks`/`power-domains` on the GPU node for TI parts) | L |
| SGX quirk | `ti,sysc-omap4` applies the SGX quirk (OCP interrupt bypass) on module enable | L |
| Reset line | none in the DT; whether one exists is U | L/U |
| Power loss | `sgx_pwrdm` off loses all GPU state (D, as on the SGX530): reload the microkernel and BIF setup on every power-up | D |

`info.clock_hz` for the microkernel timer is therefore ≈ **307 200 000** (L, the
assigned rate).  The vendor DDK binds `56000000.gpu` and its `renderD129`/
`card1` are the same device on our unit (D).

## What this means for the stack

The open driver's core check (`pvrsgx_core.c`) requires `MULTIPLE_MEM_CONTEXTS`,
`MULTI_EVENT_KICK`, a **32-bit** address space and ≥2 directory lists.  The
SGX540 meets **only** `MULTI_EVENT_KICK`.  So an SGX540 entry is **more than a
table row**, and shares the SGX530's non-per-core work:

- **28-bit heap layout.**  The fixed layout in `pvrsgx_fw.h` assumes 32 bits;
  the SGX540 needs a 256 MiB one.
- **One memory context.**  Each job's page directory goes into
  `DIR_LIST_BASE0`, with a BIF flush and `INVALDC` on every VM switch.
- **Code-base windows.**  Resolve the `USE_CODE_BASE` conflict (§USSE); if the
  registers are absent, take the 535-style path.
- **The register offsets above** go into the family's `regs` table (CORE_ID/
  REVISION, the clock/reset/event fields, the 28-bit request bases).
- **The microkernel:** its register tables, reset bits, clock/status and PDS/USE
  bases for the SGX540, started via `EVENT_KICK2` (present, unlike the SGX530).
- **Mesa:** the `sgx` driver for the SGX540's limits (all U until measured).

Easier than the SGX530: multi-event kick and 4 USE pipes.  Harder or the same:
the 28-bit/single-context class, and a still-unknown DPM/TA/ISP/TSP/PBE/PDS
state map.

## Gaps

Each needs a read or test on our SGX540.  Read only named registers.

| # | Gap | Test |
|---|---|---|
| 1 | `CORE_ID` and `CORE_REVISION` (expect `0x00010200`) | Read 0x0020/0x0024/0x0028/0x002c with the module enabled (the vendor DDK already clocks it). |
| 2 | The module `rev` and the SGX quirk | Read 0x5600fe00; check the OCP-bypass bit after enable. |
| 3 | The interrupt reaches the CPU | Enable the timer event and count `GIC_SPI 21` in `/proc/interrupts`. |
| 4 | Clock-gating/status layout | `CLKGATESTATUS` idle vs during a TA pass. |
| 5 | Soft-reset bits | Reset one unit at a time; watch status/events. |
| 6 | BIF: 28-bit space, variable pages, single context | Map one page (valid bit only), read it from the microkernel, fault an unmapped address, read `BIF_FAULT`; switch `DIR_LIST_BASE0` with `INVALDC`. |
| 7 | `USE_CODE_BASE` present or not | Write/read 0x0a0c+n and see whether it sticks; run one USSE task. |
| 8 | The ISA (pairing, phases, `efo`, F16, C10) | The instruction-level hardware tests, once a USSE task runs. |
| 9 | Texture/render-target limits, samplers, varyings, temporaries | Render at increasing sizes; read the TSP/ISP state from a capture; dEQP limit queries. |
| 10 | DPM/TA/ISP/TSP/PBE/PDS state | Absent from the K headers; decode from our own microkernel's accesses, the DDK workarounds, `Garnet/sgx540-reversing` and QNX captures. |
| 11 | `sgx_pwrdm` state loss | Suspend/resume via runtime PM; confirm the re-init covers it. |
| 12 | The 32 MiB window past the registers | Read nothing until a register is named. |

## Recording

Add each confirmed fact with the tag **C**, the date and the board, and record
the read/test in the project's build log, as `state.md` describes.

## Relationship to this repository

This is the K/L groundwork for report 276's scoping and DECISIONS §108 (the open
`powervr_sgx` SGX540 port as the long-term graphics route).  It is a draft for
the upstream project's `docs/porting/cores/sgx540.md`; nothing here is a claim
about our silicon until the §Gaps reads are done.
