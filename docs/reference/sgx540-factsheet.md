# SGX540 (TI OMAP4430/4460/4470) — fact sheet

Draft for the upstream `powervr_sgx` project's `docs/porting/cores/sgx540.md`.
It collects what our sources say, compares the core with the SGX545 and SGX530 the
open stack already runs, and marks what a hardware read has now confirmed.  The
first hardware read was made in session 73 (see §"Confirmed on hardware").

Method and tags are the project's (see the upstream `docs/porting/state.md`): every fact carries
a tag and stays below **C** until our own hardware read confirms it.

| Tag | Meaning |
|---|---|
| **L** | Mainline Linux 7.3.3: the device tree, its binding, or a driver. |
| **K** | The GPL kernel side of the SGX DDK 1.17.4948957 we vendor: its register, feature, errata and MMU headers. It says what that DDK programs, not the silicon itself. |
| **D** | Derived from L or K, often with the SGX545/530 results. |
| **C** | Confirmed on our SGX540 hardware (OMAP4430 "winchester", rev 120). |
| **G** | A guess. |
| **U** | Unknown. |

## Sources

- **L:** `Documentation/devicetree/bindings/gpu/img,powervr-sgx.yaml` (this tree),
  `arch/arm/boot/dts/ti/omap/omap4.dtsi`, `omap443x.dtsi`, `drivers/bus/ti-sysc.c`.
- **K, SGX540:** the GPL-2 headers of DDK 1.17.4948957:
  `sgx540defs.h`, `sgxfeaturedefs.h`, `sgxerrata.h`, `sgxmmu.h` (vendored at
  `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz`).  The same headers are
  already in the upstream `kernel/src/services4/srvkm/hwdefs/` tree (SGX545 and
  SGX530 defined; SGX540 present but `#if`-gated and unused).
- **Upstream method:** `powervr_sgx` `docs/porting/{README,kernel}.md`,
  `cores/sgx530.md` (the closest worked example — now a full family entry, not
  only a fact sheet).
- No closed userspace, firmware or non-GPL vendor file was used.

The DDK's register headers list only the registers the host kernel touches.  The
TA, 3D, DPM, ISP, TSP, PBE and PDS **state** registers the microkernel and the
command streams program are not in them; everything about those is **U**.

## Identity — confirmed on hardware (session 73)

Read with `tools/bench/gpu/sgxcoreid.py` through `/dev/mem` while `pvrsrvkm`
clocked the GPU (`56000000.gpu` bound, `renderD129`/`card1`).  Capture:
`docs/captures/sgx540-identity-2026-11-26.txt`.

| Fact | Value | Tag |
|---|---|---|
| DT compatible | `"ti,omap4430-gpu", "img,powervr-sgx540"` (binding lists OMAP4430 as **Rev 120**) | L |
| `CORE_ID` offset | **0x0020**; config b0-15, id b16-31 | K, **C** (offset) |
| `CORE_ID` id field | **0x0114** — the SGX545's is 0x0115 and the SGX530's (read on an OMAP3630) 0x0112, so the id distinguishes all three | **C** |
| `CORE_REVISION` offset | **0x0024**; maintenance b0-7, minor b8-15, major b16-23, designer b24-31 | K, **C** (offset) |
| `CORE_REVISION` | **`0x00010200`** = major 1, minor 2, maintenance 0 → **Rev 120** | **C** |
| `DESIGNER_REV_FIELD1/2` | 0x0028 / 0x002c; both read **0** on this unit | K, **C** (value) |
| `BIF_INT_STAT` at idle | `0x00008000` = bit 15 **`FLUSH_COMPLETE`** only — the bound DDK's BIF idle with a completed flush | **C** (value) |
| DDK errata for **rev 120** | `FIX_HW_BRN_26620`, `FIX_HW_BRN_28011`, `FIX_HW_BRN_34028` | K |

The expected `CORE_REVISION` matched exactly, so the "Rev 120" in the DT binding
and the DDK's rev-120 errata table both describe our silicon.  The `CORE_ID` id
field **0x0114** is the load-bearing new fact for a driver: it distinguishes the
SGX540 from the SGX545 (0x0115) and SGX530 (0x0112 on an OMAP3630) **before** any
register write.

**Corrected from the first draft of this sheet:** its errata list ("BRN 26620,
28011, 34028, 29954, 29997, 30954, ... 31780, revision-dependent") mixed the
SGX540 and SGX543 blocks.  `sgxerrata.h` gives the SGX540 **exactly three** at
rev 120.  The 29954–31780 group belongs to the SGX543 family.

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
| `CORE_ID` | **0x0020** | 0x001c | K, **C** |
| `CORE_REVISION` | **0x0024** | 0x0020 | K, **C** |
| `DESIGNER_REV_FIELD1/2` | **0x0028 / 0x002c** | 0x0024 / 0x002c | K, **C** |
| `EVENT_KICK1` / `EVENT_KICK2` | **0x0ab0 / 0x0ac0** | 0x0ae4 / 0x0ae8 | K |
| `EVENT_KICK` | 0x0ac8 (`NOW` b0) | 0x0ac8 | K |
| `EVENT_KICKER` | 0x0ac4 (`ADDRESS` b4-27, a 24-bit field at shift 4) | 0x0ac4 (32-bit) | K |
| `PDS_EXEC_BASE` | 0x0ab8 | 0x0ab8 | K |
| `EVENT_TIMER` | 0x0acc (`ENABLE` b24, `VALUE` b0-23) | 0x0acc | K |

`CORE_ID` and `CORE_REVISION` moved by 4, so a driver that hard-codes the SGX545
offsets reads the wrong words on this core.  The bus must name the core (the DT
compatible does).

The all-zero `CLKGATECTL`/`CLKGATECTL2`/`CLKGATESTATUS`/`CLKGATECTLOVR` and
`SOFT_RESET` we read are the **quiescent** state with the DDK bound and idle;
they confirm the offsets return named registers rather than garbage, not the
meaning of any bit.

## Clock gating

| | SGX540 | Tag |
|---|---|---|
| Gated units (`CLKGATECTL`, 0x0000) | ISP b0-1, ISP2 b2-3, TSP b4-5, TE b6-7, MTE b8-9, DPM b10-11, VDM b12-13, PDS b14-15, IDXFIFO b16-17, TA b18-19, system b28; auto/manual b24 | K |
| `CLKGATECTL2` (0x0004) | PBE b0-1, CACHEL2 b2-3, UCACHEL2 b4-5, USE0 b6-7, ITR0 b8-9, TEX0 b10-11, MADD0 b12-13, USE1 b14-15, ITR1 b16-17, TEX1 b18-19, MADD1 b20-21 | K |
| `CLKGATESTATUS` (0x0008) | one bit per unit: ISP b0, ISP2 b1, TSP b2, TE b3, … | K |
| `CLKGATECTLOVR` (0x000c) | the override layout | K |
| Automatic clock gating | `SGX_FEATURE_AUTOCLOCKGATING` set | K |

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
  `EVENT_STATUS2`/`HOST_ENABLE2`/`HOST_CLEAR2` (0x0118/0x0110/0x0114) — the
  modern SGX545 layout, not the SGX530's.  The bit positions follow the SGX545
  for the shared events (K); the per-bit list is to be transcribed before use.
* **Multi-event kick:** `EVENT_KICK1` 0x0ab0 (`NOW` b0-7), `EVENT_KICK2` 0x0ac0
  (`NOW` b0), `EVENT_KICK` 0x0ac8 (`NOW` b0), `EVENT_KICKER` 0x0ac4 (address
  b4-27), `EVENT_TIMER` 0x0acc (`ENABLE` b24, `VALUE` b0-23).
  `SGX_FEATURE_MULTI_EVENT_KICK` **is set** — the microkernel start can use
  `EVENT_KICK2` (the SGX530 cannot).
* `EVENT_KICKER`'s address is **28-bit** (b4-27), matching the device address
  space (§BIF).

## The BIF and the MMU

| Fact | SGX540 | Tag |
|---|---|---|
| Device address space | **28-bit** (`SGX_FEATURE_ADDRESS_SPACE_SIZE 28`; `BIF_FAULT` addr b12-27, `*_REQ_BASE` b20-27) | K |
| Memory contexts | **one**: `DIR_LIST_BASE0` 0x0c84; **no `MULTIPLE_MEM_CONTEXTS`**, no `BIF_NUM_DIRLISTS`, no per-requestor bank registers | K |
| Page size | variable: PDE page-size bits `MASK 0x0e` → 4K/16K/64K/256K/1M/4M (mask `0x0e`) (`sgxmmu.h`) | K |
| PTE flags | valid b0, write-only b1, read-only b2, cache-consistent b3, EDM-protect b4 | K |
| PDE flags | valid b0, page size b1-3, … | K |
| `BIF_CTRL` 0x0c00, `BIF_INT_STAT` 0x0c04, `BIF_FAULT` 0x0c08, `BIF_MEM_REQ_STAT` 0x0ca8, `BIF_TA_REQ_BASE` 0x0c90, `BIF_3D_REQ_BASE` 0x0cac, `BIF_ZLS_REQ_BASE` 0x0cb0 | present | K |

So the MMU has the SGX545's page-size machinery but a **28-bit, single-context**
view — the SGX530's address-space class, with the newer page-size bits.

## The USSE and its code base

* The SGX540 runs the **USSE (first-generation)** instruction set, the same
  family as the SGX545 — so the open stack's NIR→USSE compiler is the right one
  (D).
* **4 USE pipes** (`SGX_FEATURE_NUM_USE_PIPES 4`), like the SGX545 (K).
* `SGX_FEATURE_USE_NO_INSTRUCTION_PAIRING` and `SGX_FEATURE_USE_UNLIMITED_PHASES`
  are **not** set for the SGX540 (K) — the SGX545 sets both.  What that means
  for encodings/scheduling is **U** (the feature names suggest the SGX540 pairs
  instructions and limits program phases, as the SGX530 does) (G).
* **`USE_CODE_BASE`: conflicting, and the encoding is the SGX530's, not the SGX545's.**
  `sgx540defs.h` **defines** `EUR_CR_USE_CODE_BASE(X) = 0x0A0C + 4X`, 16 entries,
  with **address b0-23 (`MASK 0x00FFFFFF`) and data-master b24-25
  (`MASK 0x03000000`, `SHIFT 24`)** (K).  Compare the other two cores in the same
  DDK drop: the **SGX535** and **SGX545** (`sgx535defs.h:641`,
  `sgx545defs.h:634`) both give **address b0-24 (`MASK 0x01FFFFFF`) and DM b25-26
  (`MASK 0x06000000`, `SHIFT 25`)**.  So the SGX540 header's field layout is the
  24-bit class — the same shape the open driver's SGX530 `regs` uses
  (`use_code_base_addr_shift = 8` there, because the SGX530 counts in 256-byte
  units; the SGX540's own unit is **U**).  This is a stronger hint than the
  guide's prose: at minimum a driver must **not** copy the SGX545's shifts.  The
  upstream `kernel.md` still says "535 and 540 have no `USE_CODE_BASE`
  registers", which the 540 header contradicts.  A hardware write/read-back
  settles whether the registers exist on rev 120 at all (U — see
  `docs/contradictions/sgx540-use-code-base.md`).
* **Monolithic microkernel: not set** for the SGX540 (K) — the open microkernel
  is not the monolithic variant.

## Other features

Set for the SGX540 in the DDK (K): `MULTI_EVENT_KICK`, `AUTOCLOCKGATING`,
28-bit address space, 4 USE pipes.

**Not** set for the SGX540 (K), i.e. like the SGX530 and unlike the SGX545:
`MULTIPLE_MEM_CONTEXTS`, `MONOLITHIC_UKERNEL`, `PDS_DATA_INTERLEAVE_2DWORDS`,
volume textures, the texture-stride extension, host allocation from the DPM,
external Z for the ZLS, native backward blits, 512 TA render targets, the
write-back DCU, `BIF_WIDE_TILING_AND_4K_ADDRESS`, and the full-range vertex PDS
address (`EDM_VERTEX_PDSADDR_FULL_RANGE`).

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
| Module registers | `rev` at 0x5600fe00, `sysc` at 0x5600fe10 | L, **C** |
| Module `rev` | **`0x40000000`** (same value the SGX530's module reads on OMAP3) | L, **C** |
| Module `sysc` | **`0x0000003c`**, read while the DDK held the module enabled (the GPU idle); the bit decode is U | **C** (value) |
| GPU node | `gpu@0`: reg `<0x0 0x2000000>` (**32 MiB**), `interrupts = <GIC_SPI 21 IRQ_TYPE_LEVEL_HIGH>` | L |
| Power domain | `power-domains = <&prm_gfx>` (the `sgx_pwrdm`/GFX island) | L |
| Clock | parent `l3_gfx_clkctrl OMAP4_GPU_CLKCTRL` `fck`; `omap443x.dtsi` assigns it to **`dpll_per_m7x2_ck` at 307.2 MHz** | L |
| Who manages clocks/power | the parent `ti-sysc` module enables them (the binding forbids `clocks`/`power-domains` on the GPU node for TI parts) | L |
| SGX quirk | `ti,sysc-omap4` matches `rev` `0x40000000` and applies `SYSC_MODULE_QUIRK_SGX`: on module enable it sets bit 31 (`THALIA_INT_BYPASS`) of `OCP_DEBUG_CONFIG` at module offset `0xff08` | L |
| Reset line | none in the DT; whether one exists is U | L/U |
| Power loss | `sgx_pwrdm` off loses all GPU state (D, as on the SGX530): reload the microkernel and BIF setup on every power-up | D |

`info.clock_hz` for the microkernel timer is therefore ≈ **307 200 000** (L, the
assigned rate).  The vendor DDK binds `56000000.gpu` and its `renderD129`/
`card1` are the same device on our unit (C).

The open driver's platform glue is what remains for OMAP4: an `of_device_id`
entry `{ .compatible = "img,powervr-sgx540", .data = (void *)540 }` (the SGX530
entry already exists in `pvrsgx_of.c`), and — because the OMAP4 SGX is a
**single-memory-context** core — the device must be run with
`powervr_sgx.allow_unisolated=1`, which the driver already gates on exactly this
condition.

## What this means for the stack (updated against the code)

The upstream tree has moved since report 276 scoped this port.  The core check is
**not** "≥32 bits, multiple contexts, ≥2 directory lists":

* `pvrsgx_core_family_usable()` accepts `va_bits` **28 or 32**, `num_dir_lists`
  **exactly 1** when the core has no `MULTIPLE_MEM_CONTEXTS`, and requires the
  event-PDS registers plus `EVENT_KICK2` **exactly when** it is present.
* **The whole SGX530 class is implemented as data**: `sgx530_regs`, a 28-bit
  `sgx530_heaps` layout, `pvrsgx_cmd_core_sgx530` (`.va_bits = 28`), the
  `PVRSGX_QUIRK_BRN_28889` split-invalidate, and `allow_unisolated` for the
  single-context case.
* So an SGX540 entry is **closer to a table entry than report 276 said**, but not
  free: it needs its own `regs` (CORE_ID/REVISION at 0x20/0x24, the 28-bit
  request bases, its own `USE_CODE_BASE` shifts), a 28-bit heap layout (the
  SGX530's is a starting point, not a copy), its `init_regs` captured from a
  working boot, and the `USE_CODE_BASE` question settled.

### The microkernel: it assembles, but the image is built for `srvinit`, not `powervr_sgx`

An SGX540 microkernel now **builds** (`ukernel/cores/sgx540.py`, delta from the
SGX545 image = **3 bytes**: `MISC_CORE` 545→540, `CORE_ID` 0x1c→0x20,
`CORE_REVISION` 0x20→0x24) and **passes all 7 offline emulator tests**.  The
SGX545 image stayed byte-identical to its golden hash, so `sgx-asm` learning the
core (an enum, a support clause, `<gen>` widened to 54000-54599) regressed nothing.

On hardware the driver **finds** it but rejects it — and the reason is
structural, not packaging:

* The probe runs at t≈0.9 s, **before hd3 is mounted**, so the firmware must be in
  the **initramfs**, not `/lib/firmware` on the root (error `-2` → "not a
  microkernel file" once moved).
* "not a microkernel file" is `ukfw_parse()` finding no `SGX5`/`UKRN` header; the
  image needs `drm/tools/mkukfw.py`, which for a **single-context** core demands
  `SINGLE_CONTEXT` set, `SECURE` clear, **and a `pdtable` patch** — the driver
  gives the microkernel a PD table at `kernel_code.base + 0xf000` and the
  microkernel must **load directory list 0 from it**.
* This image cannot: it routes TA/3D to **list 1** via `BIF_BANK0` and relies on
  list 0 being `srvinit`'s shared kernel PD.  Its own source says so — *"under
  `powervr_sgx` list 0 is an empty idle PD and the transfer BIF-faults on a client
  address"*.  So it is a **two-context image for a 32-bit core under `srvinit`**,
  and running it under `powervr_sgx` on a **one-context, 28-bit** core is real
  work: add the `pdtable` patch site and load list 0 from it, fix the feature
  bits, and re-check the 39 places the image assumes list 1 exists.  Only after
  that do the unmeasured register tables get tested.

Capture: `docs/captures/sgx540-ukernel-2026-11-26.txt`.

### The entry binds and identifies the core on hardware (session 73) — **C**

The open driver was built into a PlayBook kernel (`local/kernel-tree-sgx`, a copy
of `local/kernel-tree` with the closed DDK off; DECISIONS §111), flashed to region
0 and booted.  It **probed the GPU and identified it from the hardware**:

```
powervr_sgx 56000000.gpu: [drm] SGX core id 0x01140000 revision 0x00010200:
                                 TI OMAP4430 (SGX540 1.2.0)
```

That is the string the entry's `core_id`/`core_rev`/`cores[]` were written to
produce from the identity read earlier in the session, so **the `regs` offsets,
the `core_id` match and the part lookup are confirmed on silicon** — a C fact, not
a table guess.  It then failed at `request_firmware()` for
`powervr_sgx/sgx540_ukernel.bin` (`-2`): **no microkernel exists for this core
yet**, which is the next piece of work.  The `allow_unisolated` guard also behaved
as designed — the driver refused the single-context core until
`powervr_sgx.allow_unisolated=1` was on the command line.

Capture: `docs/captures/sgx540-bind-2026-11-26.txt`.

**Caveat measured on the same boots:** `CONFIG_SGX=n` removes only the *built-in*
closed DDK.  hd3's `/etc/modules` loads `pvrsrvkm_omap4_sgx540_120`, which wins the
probe race and holds `56000000.gpu`
(`/sys/bus/platform/devices/56000000.gpu/driver -> …/pvrsrvkm`; `powervr_sgx` has
no device).  The identification is still valid — it read the real device — but a
boot meant to *run* the open driver must blacklist `pvrsrvkm` (trap 379).

### The entry is written and its table tests pass (session 73, host)

A first-cut SGX540 entry now exists in our `local/work/powervr_sgx` clone (committed
here as `rootfs/powervr-sgx/0001-sgx540-core-entry.patch`), and it is validated
**without publishing anything upstream** (DECISIONS §110):

| what | result |
|---|---|
| The entry compiles against the real 7.3.3 tree (`local/kernel-tree`, ARMv7) | **23/23 objects, zero warnings** (21 in `powervr_sgx-y` + `debugfs`/`pm`, both `=y`) |
| The driver's own KUnit suite on x86_64 (`powervr_sgx_core`) | **8/8 pass**, including the new `core_test_sgx540` |
| A negative control (one expectation forced wrong) | **fails as it should** — the test really reads the entry |
| The heap layout against `heaps_fit()`/`pds_reach()` and non-overlap | `tools/bench/gpu/sgx540-heaps-check.py`: pass |
| In-tree ARMv7 build with the DRM helpers enabled | links into `drivers/gpu/drm/built-in.a`; **but see below — that build used the wrong tree** |

The last row proves the driver compiles and links for ARM with its helpers
enabled (`DRM_EXEC`/`DRM_GPUVM`/`DRM_SCHED`/`DRM_GEM_SHMEM_HELPER`; our bench
config has none — trap 375).

**It does not prove the image boots, and the first attempt did not.**  That build
came from `local/pristine/linux-7.3.3`, which carries **no PlayBook support** (no
`pbmon.c`, no Munin, no WM8994, no `omap4-winchester.dts`); the image flashed and
the unit fell back to bootrom, and region 0 had to be restored (trap 378).  A
runnable image must be built from `local/kernel-tree`.  Also note there is **no
RAM-boot path** on this device — region-0 IFS flash is the only way to boot a
kernel (operator, session 73), so every GPU test costs a flash and a
`BOOTCTR_FCTRY` boot.

**The dma-buf export question looks better than expected.**  The driver registers
only `.gem_prime_import_sg_table`, but `DRM_IOCTL_PRIME_HANDLE_TO_FD` is served by
the generic GEM path, and the driver's own tests exercise it
(`drm/tests/powervr_sgx_test.c` asserts a handle becomes an fd, and
`pvrsgx_prime.c`'s `prime_move()` exports from one device and imports into
another).  So a working SGX540 on this stack should be able to *export* dmabufs —
which is precisely what the closed pvr GBM cannot do (report 275) — but this is a
reading of the code, not a measurement, and stays **D** until a real export runs.

The entry carries: `sgx540_regs` (CORE_ID 0x0020, CORE_REVISION 0x0024, the
28-bit request bases, no BIF bank registers, `USE_CODE_BASE` DM at bit 24 /
256-byte units), the 28-bit `sgx540_heaps` from the DDK's `sgxconfig.h`, a
minimal `sgx540_init_regs`, `pvrsgx_cmd_core_sgx540`, `core_id` 0x01140000 with
`core_rev` 0x00010200, and the `of_device_id` row `img,powervr-sgx540` → 540.

**Two facts the entry made concrete, both from the GPL headers, both now *load
bearing*:**

* **`CLKGATECTL` gates more units than the SGX545's.**  The 28-bit core also gates
  IDXFIFO (b16-17) and TA (b18-19), so the `SGX_CLKG_ALL` mask is
  `GENMASK(19, 0)`, not the SGX545's `GENMASK(15, 0)`.  A mask copied from the
  545 would silently leave the TA ungated.
* **`BIF_INT_STAT` has a different field layout.**  Fault requests are b0-13 and
  b14 is `PF_N_RW`; there is no separate fault-type field.  The SGX545's b0-15 /
  b16-18 do not apply.

**The `sgxconfig.h` heap layout is the real source.**  The vendored DDK's
`devices/sgx/sgxconfig.h` has an explicit **`SGX_FEATURE_ADDRESS_SPACE_SIZE == 28`**
block — the one our 28-bit core compiles — giving every heap base: general
0x00001000, PDS pixel 0x0dc00000, kernel code 0x0e400000, PDS vertex 0x0e800000,
kernel data 0x0f000000, USSE pixel 0x0f400000, USSE vertex 0x0fc00000.  It has
**no texture heap** (textures come from general), so, as `sgx530_heaps` does, the
texture heap is carved from the top of the general range — ours, not the DDK's.
`pds_reach()` passes because kernel code and PDS vertex are within 64 MiB above
`PDS_EXEC_BASE`.

**The three rev-120 errata need no driver workaround** on this core:
`BRN_26620` and `BRN_34028` touch `MNE_CR_CTRL`, which is
`SGX_FEATURE_SYSTEM_CACHE` (not set for the SGX540); `BRN_28011` only re-writes
`DIR_LIST_BASE0` when the EDM's list index is not 0, which a one-list core cannot
have.  So `quirks = 0` is right, unlike the SGX530's `BRN_28889`.

What is still **code, not a row**:

- **The heap layout's texture split.**  The DDK gives no texture heap; where the
  general/texture boundary sits is our choice and worth revisiting once Mesa's
  texture sizes are known.
- **The `USE_CODE_BASE` granule.**  The field is 24-bit with DM at b24; a byte
  unit cannot reach 0x0e400000, so 256-byte units (the SGX530's) are assumed.
  Gap 7 still needs a hardware write/read-back.
- **`sgx540_init_regs`** is the SGX530's minimal set; the SGX545's longer init
  script has unnamed registers whose SGX540 values are only obtainable from a
  working boot.
- **The event PDS registers** (`EVENT_PDS_*`) are the SGX545's offsets, a guess
  (`event_pds_guessed`), not in the 540 header.
- **The microkernel:** its own `CORE_NAME` (`sgx540`) and register tables; the
  firmware is not built yet.
- **Mesa:** the `sgx` USSE1 driver for the SGX540's limits (all U until measured).

Easier than the SGX530: multi-event kick and 4 USE pipes.  Harder or the same:
the 28-bit/single-context class, and a still-unknown DPM/TA/ISP/TSP/PBE/PDS
state map.

## Confirmed on hardware

Session 73, 2026-11-26, on the reference unit (`0x50175127`, OMAP4430 ES2.2)
running Linux `7.3.3 #109 SMP` with `pvrsrvkm_omap4_sgx540_120` bound.  Every
line above tagged **C** was read with `tools/bench/gpu/sgxcoreid.py`; the raw
output is `docs/captures/sgx540-identity-2026-11-26.txt`.  Nothing was written.

## Gaps

Each needs a read or test on our SGX540.  Read only named registers.

| # | Gap | Test |
|---|---|---|
| 1 | `CORE_ID` and `CORE_REVISION` | **DONE (C):** `CORE_ID` id `0x0114`, `CORE_REVISION` `0x00010200`. |
| 2 | The module `rev` and the SGX quirk | **Partly done (C):** `rev` = `0x40000000`, `sysc` = `0x0000003c` at idle.  Still to check: `OCP_DEBUG_CONFIG` bit 31 after enable. |
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

This is the K/L groundwork plus the first hardware **C** facts for report 276's
scoping and DECISIONS §108 (the open `powervr_sgx` SGX540 port as the long-term
graphics route).  It is a draft for the upstream project's
`docs/porting/cores/sgx540.md`.
