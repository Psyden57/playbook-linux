# Report 276 — Scoping an SGX540 port to the open `powervr_sgx` stack

Date: 2026-11-26 (session 72).  Host only; no device writes.  Follows report 275
(Kumo cannot render because the closed pvr GBM cannot export dmabufs) and the
operator's question: is the open/RE graphics route better than our blobs?

> **Addendum (session 73).**  Three of this report's claims are corrected by
> reading the upstream tree as it stands (commit `95f78ca`, 2026-11-25) and by the
> first hardware read of our SGX540:
>
> 1. **§1's core check is stricter than the code.**  `pvrsgx_core_family_usable()`
>    accepts `va_bits` **28 or 32** and **`num_dir_lists == 1`** for a core without
>    `MULTIPLE_MEM_CONTEXTS`; it does not demand ≥32 bits or ≥2 directory lists.
>    The SGX530 is now a **complete family entry** (28-bit heaps, its own
>    `pvrsgx_cmd_core`, BRN-28889, `allow_unisolated`) — the work §2 called "not
>    per-core yet" has largely landed.
> 2. **The errata list in §2 is wrong.**  `sgxerrata.h` gives the SGX540 **three**
>    at rev 120 (`FIX_HW_BRN_26620`, `_28011`, `_34028`); the 29954–31780 group is
>    the SGX543's.
> 3. **`USE_CODE_BASE` is not a clean "no".**  `sgx540defs.h` defines it with a
>    **24-bit** address and DM at bit 24, unlike the 535/545 (25-bit, DM at 25), so
>    the header is written for a different layout rather than copied.  Whether the
>    registers exist on rev 120 is still unmeasured.
>
> And the first hardware read is done: **`CORE_ID` id `0x0114`,
> `CORE_REVISION` `0x00010200` (Rev 120), module `rev` `0x40000000`** — the
> expected identity, and a `CORE_ID` that distinguishes the 540 from the 545
> (0x0115) and 530 (0x0112).  See `docs/reference/sgx540-factsheet.md` §"Identity"
> and `docs/captures/sgx540-identity-2026-11-26.txt`.  The recommendation below
> stands; the sequencing changes (see "What this changes", §8).

## 0. Headline

**The open stack is the durable fix, and our SGX540 is a harder port than a
table entry — but we are unusually well equipped to do it.**  The upstream
`powervr_sgx` project supports the whole SGX family with one kernel driver, one
Mesa driver and one microkernel, adding a core "as data, not code"
([porting guide](https://github.com/The1TrueJoe/powervr_sgx/blob/main/docs/porting/README.md)).
SGX540 is **USSE (same ISA family as the supported SGX545)** and listed
**"Wanted"**.  But the vendored GPL DDK 1.17 headers show our SGX540 is a
**28-bit, single-memory-context** core with **no `USE_CODE_BASE`**, so it does
**not** meet the open driver's core check (32-bit space, multiple memory
contexts, ≥2 directory lists) — it needs the same non-per-core work the SGX530
bring-up needs, plus OMAP4 platform glue.  It is a **multi-session project**, not
a configuration change, and it should be the deliberate long-term route while
the closed stack keeps the desktop running.

## 1. What the upstream project requires of a new core

From `docs/porting/kernel.md`, `microkernel.md`, `isa.md`, `state.md`,
`mesa.md`, and the SGX530 fact sheet (the closest worked example):

* A new core is a `struct pvrsgx_core_family` (+ a `pvrsgx_core_info` per part)
  in `drm/powervr_sgx/pvrsgx_core.c`, carrying: `core`, expected `core_id`,
  per-core `regs` (offset/field differences), `features` (must include the
  required set), `isa`, `num_dir_lists`, `va_bits`, `init_regs` (the per-core
  init script), plus `core_rev`, `clock_hz`, `fw_name`.
* **The core check refuses a core with fewer than 32 address bits, without
  `MULTIPLE_MEM_CONTEXTS`/`MULTI_EVENT_KICK`, or with fewer than two directory
  lists.**  The SGX530 meets none; the SGX540 meets only `MULTI_EVENT_KICK`
  (§2), so it needs the same "28-bit core" code path.
* **Not per-core yet** (so code, not a row): the heap layout for 28-bit cores,
  the shader code-base windows (**535 and 540 have no `USE_CODE_BASE`
  registers**), the microkernel command constants, and the microkernel start
  (needs `EVENT_KICK2`, which the SGX540 has).
* The display side: the SGX renders, another driver scans out, buffers cross as
  dma-bufs — exactly our `omapdrm` + `pvr` split.

## 2. The SGX540 facts we already have (from the vendored GPL DDK 1.17)

`kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` → `sgx540defs.h`,
`sgxfeaturedefs.h`, `sgxerrata.h`, `sgxmmu.h` (all GPL-2; the project's "K"
source):

| fact | value | source |
|---|---|---|
| friendly name | `SGX540` | `sgxfeaturedefs.h` |
| `CORE_ID` offset | **0x0020** (config b0-15, id b16-31) | `sgx540defs.h:165` |
| `CORE_REVISION` offset | **0x0024** (maint/minor/major/designer) | `sgx540defs.h:171` |
| `SOFT_RESET` | 0x0080, bits BIF/VDM/DPM/TE/MTE/ISP/ISP2/TSP/PDS/PBE/CACHEL2… | `sgx540defs.h:189` |
| clock gating | `CLKGATECTL` 0x0000 (ISP/ISP2/TSP/TE/MTE/DPM/VDM/PDS/IDXFIFO/TA/SYSTEM), `CLKGATECTL2` 0x0004 (PBE/CACHEL2/UCACHEL2/USE0…) | `sgx540defs.h:45-83` |
| address space | **28-bit** (`SGX_FEATURE_ADDRESS_SPACE_SIZE 28`) | `sgxfeaturedefs.h` SGX540 block (l.89) |
| memory contexts | **single** — no `MULTIPLE_MEM_CONTEXTS`, no `BIF_NUM_DIRLISTS` | idem |
| event kicks | **`MULTI_EVENT_KICK`** | idem |
| USE pipes | **4** | idem |
| code base | **no `USE_CODE_BASE` registers** (like 535; the guide names 540) | `kernel.md` "not per-core" |
| monolithic ukernel | **not set** | idem |
| errata | BRN 26620, 28011, 34028, 29954, 29997, 30954, 31093, 31195, 31272, 31278, 31542, 31620, 31780 (revision-dependent) | `sgxerrata.h` |

So the SGX540 is **USSE1, 4-pipe, multi-kick, 28-bit, single-context, no code
base** — a modern register/reset layout but the SGX530's address-space class.

## 3. What we already have that shortens the port

* **A working SGX540 on the unit** with the vendor DDK 1.17 (`pvrsrvkm`
  `56000000.gpu`, `renderD129`, `card1`): the init script, clock and power
  behaviour, MMU setup and state programming are all **observable** on real
  hardware.  The porting guide says the per-core init script is "partly
  unnamed" and must be captured from a working boot — we have one.
* **The GPL DDK headers** give the "K"-tagged register/feature/errata/MMU facts
  without any proprietary source (the project forbids proprietary material;
  these are GPL-2 and acceptable).
* **The QNX reference (oracle)** and the vendor userspace give black-box state
  evidence, which is how the project itself works (cleanroom: observe, don't
  copy).
* **`Garnet/sgx540-reversing`** has SGX540 ISA/PDS/microkernel RE tooling — the
  same core, directly useful for the "U" state facts.
* **Our kernel/DT**: `ti-sysc` `sgx_module` at `0x56000000`, `sgx_pwrdm`, the
  OMAP4 clock tree, and the upstream binding `img,powervr-sgx540` already in
  `img,powervr-sgx.yaml`.  A new board of a known core is "only platform glue".

## 4. The work, in the porting guide's order

1. **Fact sheet** `docs/porting/cores/sgx540.md` — the SGX530 sheet is the
   template.  Fill it from §2's headers (K), the mainline OMAP4 DT/ti-sysc (L),
   and hardware reads on our unit (C).  Cheap, no risk; the first deliverable.
2. **Publish the device** — a platform driver in `pvrsgx_drv.c` with an
   `of_device_id` carrying core 540, enabling the `sgx` clocks and `sgx_pwrdm`
   through `ti-sysc`, mapping `reg`.  Read `CORE_ID`/`CORE_REVISION`
   (0x0020/0x0024) and log them — the first hardware milestone.
3. **The 28-bit / single-context code** — a 28-bit heap layout; BIF with one
   memory context (`DIR_LIST_BASE0`, `INVALDC` on VM switch, and BRN-28889
   ordering if the revision needs it); the code-base window path for cores
   without `USE_CODE_BASE` (540 like 535).  This is the bulk of the "not
   per-core yet" work.
4. **Microkernel** — build for 540 (USSE1, 4 pipes), with the 540 register
   tables, reset bits, clock/status and PDS/USE bases; start via `EVENT_KICK2`
   (available).  Then the first kick / first frame tests (`powervr_sgx_frame`,
   `powervr_sgx_blit`).
5. **Mesa** — the `sgx` gallium driver (NIR→USSE) for 540, with its limits from
   the fact sheet; then dEQP-GLES2/piglit.
6. **The payoff** — a modern Mesa (26.x) gives **GBM dmabuf export and dmabuf
   feedback**, which is exactly what WebKit 2.54 needs and what the closed
   DDK's GBM cannot do (report 275).  It also removes the closed DDK entirely.

## 5. Risks and unknowns

* **The biggest unknown is the DPM/TA/ISP/TSP/PBE/PDS state**, which the GPL
  headers do not contain (they list only host-touched registers).  It must be
  decoded from our own microkernel's accesses, the DDK's host workarounds,
  `Garnet`'s RE, and QNX captures — the project's "U" facts.
* **28-bit heap + code-base windows** are code that the project has not finished
  for any core yet (535/540 are the first); expect driver work, not a table row.
* **Hardware time on our unit**: each test boot spends one `BOOTCTR_FCTRY` unit
  (replenished below `0x1e`), kernel printk goes to the panel, and a wedged GPU
  can need a **cold boot** (the guide's "never flash a kernel still being
  brought up; boot from RAM" applies — our chainloader already boots a
  RAM-loaded zImage, which fits the method).
* **Cleanroom**: no proprietary source/binaries/disassembly in the open tree.
  Our DDK headers are GPL; the closed userspace/blobs and QNX vendor material
  are **black-box evidence only**.

## 6. Recommendation

**Pursue it as the deliberate long-term route; keep the closed stack in
service meanwhile** (the desktop and apps work; only WebKit is blocked).  Start
with the two zero-risk steps: (1) write `cores/sgx540.md` from the vendored
headers and mainline OMAP4 sources, and (2) open a "New SGX core" issue with
our hardware (OMAP4430 PlayBook, kernel 7.3.3) and what we can already run (the
vendor DDK works; we can read the core ID and capture the init script) — the
project asks for that first.  Then take the kernel platform driver + core table
entry, the 28-bit/code-base code, the microkernel, and Mesa in that order.

**Do not switch the stack now**: `powervr_sgx` does not support SGX540 yet, so a
switch today loses hardware 3D; `Garnet` is tooling; `pvrports`/`phoc` are the
same blobs; `zsavajji` is SGX530.

## 7. Artefacts

* The vendored DDK headers (gitignored payload):
  `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` →
  `sgx540defs.h`, `sgxfeaturedefs.h`, `sgxerrata.h`, `sgxmmu.h`, `sgxdefs.h`.
  The same headers are in the upstream tree's
  `kernel/src/services4/srvkm/hwdefs/` (SGX535/540/543/544/545 all present).
* Upstream: `docs/porting/{README,kernel,microkernel,isa,state,mesa}.md` and
  `docs/porting/cores/{sgx530,clovertrail}.md` in
  `github.com/The1TrueJoe/powervr_sgx`; `CONTRIBUTING.md` (the cleanroom and
  evidence rules).
* This repository: report 275 (the closed-stack wall), DECISIONS §107/§108,
  `docs/project/ROADMAP.md` 7a.
* The drafted fact sheet: `docs/reference/sgx540-factsheet.md` (upstream format,
  tagged, with the gaps table).
* The identity read (session 73): `tools/bench/gpu/sgxcoreid.py` and
  `docs/captures/sgx540-identity-2026-11-26.txt`.

## 8. What this changes (session 73 addendum)

The port is **smaller than §2–§4 assume, and its first milestone is met**.

* **What is already done in the open tree** (read at commit `95f78ca`):
  the 28-bit core class exists end to end — `sgx530_regs`, a 28-bit
  `sgx530_heaps`, `pvrsgx_cmd_core_sgx530` (`.va_bits = 28`),
  `PVRSGX_QUIRK_BRN_28889`, and `powervr_sgx.allow_unisolated=1` for the
  single-memory-context case, which the driver gates on exactly
  `!MULTIPLE_MEM_CONTEXTS`.  The platform glue exists too: `pvrsgx_of.c` already
  handles a TI `ti-sysc` parent (no own clocks/power domain) with an
  `of_device_id` table, and the SGX530 entry is there.
* **What an SGX540 entry still needs** (all data plus a captured init script):
  `sgx540_regs` (CORE_ID/REVISION 0x20/0x24, the 28-bit request bases, the
  `USE_CODE_BASE` shifts from the 540 header — **not** the 545's), a 28-bit heap
  layout that satisfies `pds_reach()` without
  `EDM_VERTEX_PDSADDR_FULL_RANGE`, `sgx540_init_regs` captured from a working
  boot, a `pvrsgx_cmd_core_sgx540`, and `of_device_id` `img,powervr-sgx540` →
  core 540.  The `allow_unisolated` gate then applies unchanged.
* **What is still genuinely unknown** (the §5 "U" facts, unchanged): the
  DPM/TA/ISP/TSP/PBE/PDS state map, whether `USE_CODE_BASE` exists on rev 120,
  the 28-bit heap's exact layout, and the microkernel's 540 tables.
* **The first zero-risk milestone is met:** the identity read returned the
  expected `CORE_ID`/`CORE_REVISION` and the module `rev` the quirk matches, so
  a "New SGX core" issue can now carry measured identity for an OMAP4430 rev-120
  SGX540 rather than an inferred one.
* **Revised order:** (1) the fact sheet is written with **C** facts
  (`docs/reference/sgx540-factsheet.md`); (2) open the upstream issue with the
  identity read; (3) add `sgx540_regs` + the `of_device_id` entry and let the
  driver log the core ID (the guide's stage 1–3), booting from RAM as our
  chainloader already does; (4) the heap layout and init script; (5) the
  microkernel; (6) Mesa.  The closed stack keeps the desktop throughout.

**Cleanroom note.**  The upstream tree already ships the Imagination GPL headers
(including `sgx540defs.h`), so quoting them in our fact sheet is the same class
of K evidence the project itself uses; nothing closed is copied.  The vendor
*userspace* and the QNX material stay black-box evidence only.
