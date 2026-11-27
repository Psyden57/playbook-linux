# 279 — The SGX interrupt reaches the CPU; the entry counter corrected; a PDS-task emit does not re-fetch

Date: 2026-11-26 (session 76, Linux-era 70).
Bench: the Nura root read-write from hd3, region 0 `0x02C1DDC2`, kernel
`7.3.3`; driver `powervr_sgx` (open) plus the microkernel built from
`local/work/powervr_sgx/ukernel`.

## 0. Headline

* **The SGX's interrupt had never reached the CPU.**  On OMAP4 the core's
  request enters the `ti,sysc` wrapper's `IRQSTATUS_2` (window **0xfe38**) and
  is forwarded only while `IRQENABLE_SET_2` (**0xfe44**) bit 0 is set.  The
  driver mapped 0x4000 of a 64 KiB window and never touched it.  Opened, **irq
  147 ran 0 → 100000**.
* **Five more real defects fell with it**, all fixed and measured: the launch
  program's 12-word data segment in PDS0's 0x18-byte slot; `EVENT_PDS_DATA` 3
  (48 B) for a 24 B slot; `EUR_CR_PDS` never written; `FIX_HW_BRN_30852`; and
  **`EUR_CR_DMS_CTRL` never written at all** (the EDM ran on its reset default
  of 32 tasks where rev-120 needs 63).
* **The session's central figure was wrong and is corrected.**  The entry
  counter sat on control-block word **0x1c = `CLEANUP_DEFER_CT`**, which
  `init_set` clears.  Session 75's "the event program is entered once and never
  again", and this session's own three "fetched exactly once" conclusions, are
  **withdrawn**.  On word 0x07 the event program is fetched **twice**:
  `entry = 2`, `tick = 1`.
* **A PDS-task emit from the USSE task end executes and produces no re-fetch.**
  The hypothesis this session set out to test; answered, negatively.

## 1. The interrupt, and why it matters

The port had been reading `EVENT_STATUS` MASTER_INTERRUPT set while the GIC line
sat at count 0, and had concluded "the program is not re-fetched".  Both were
symptoms of one thing: **the core's interrupt was never wired to the CPU.**

* The vendor's OMAP4 code enables exactly this pair on every clock-enable
  (`eurasia_km/services4/system/omap4/sysconfig.c`, `SysEnableSGXInterrupts()`:
  `IRQSTATUS_2 <- 1` then `IRQENABLE_SET_2 <- 1`) and maps the whole 64 KiB
  window (`SYS_OMAP4430_SGX_REGS_SIZE` 0xFFFF,
  `gpvOCPRegsLinAddr = gsSGXRegsCPUVAddr`).  On Linux the same drop defines
  `SGX_OCP_NO_INT_BYPASS`, so `THALIA_INT_BYPASS` is deliberately not used.
* **Userspace cannot do this**: a `/dev/mem` write to the wrapper is an L4
  external abort, so it went in the driver (`pvrsgx_ocp_irq()`, called from
  `irq_setup`).
* With the gate open, irq 147 climbed to exactly **100000** — the kernel's
  unhandled-interrupt limit — and the line was disabled.  **The storm was ours:**
  `init_set` ORed `EVENT_TIMER` (b29) into `EVENT_HOST_ENABLE`, which says which
  sources interrupt the *host*; `srvinit` writes SW_EVENT only (`0x4000`).  The
  *dispatch* mask is a different register entirely, `EVENT_PDS_ENABLE`.

## 2. Five defects, each measured

| # | defect | before → after |
|---|---|---|
| 1 | `edm_init` launch assembled a 12-word data segment into PDS0's 0x18-byte slot; PDS1 at +0x20 overwrote its `DS1[0]` and its `DOUTU`/`HALT` | 4 words (`data,data,data,MOVS→DOUTU,HALT`), verified through the new `pds` node |
| 2 | `EVENT_PDS_DATA` (0x0a6c) described 48 B of attributes for a 24 B slot | `3` → `1` (16-byte units) |
| 3 | `EUR_CR_PDS` (0x0abc) never written — `DOUT_TIMEOUT_DISABLE` (b6) left the data-store timeout armed | → `0x40`, as `srvinit` writes it |
| 4 | `FIX_HW_BRN_30852`: the PDS must not auto-gate its clock | `CLKGATECTL` `0x000aa6aa` → `0x000a66aa` |
| 5 | **`EUR_CR_DMS_CTRL` (0x0a74) never written** — the EDM ran on the reset default | `0x00208200` → `0x003f8200` (EDM 32 → **63**, `FIX_HW_BRN_23632`) |

**Defect 5 did not change the symptom**, and that is recorded so it is not
re-run expecting a dispatch.  It is a real defect all the same: the vendor's
comment is *"At least 33 EDM tasks is needed to fix BRN23632"*, and the bit is
in the rev-120 errata this core answers to.

## 3. The correction: the entry counter was on the wrong word

This session built an entry counter at the top of the `event` entry — before the
`ui32InitStatus` test, so it counts every fetch — and read **1** everywhere:
96 s idle, eight host kicks, a host re-arm of the launch kick, and a build with
the counter moved to the program's first instruction.

**All of it was measurement of a control-block word the microkernel writes.**
The constant was written `0x07 * 4` and consumed as a **word index** (which is
what `ld.a.d`/`st.a.d` take), so the counter sat on word **0x1c =
`CLEANUP_DEFER_CT`** (`cores/sgx540.py:65`), which `init_set` clears and the
cleanup path writes twice more.

*Caught by:* the `rearm2` build booting with the counter at **0** while
`SC_VERSION = 0x10000` proved the init task had run — impossible for a program
that executes.  A scan of every control-block constant the core reaches through
`r17` then confirmed 0x07 free, 0x1c not.

**The corrected measurement** (firmware `fix1`, wrapped md5 `f468a71d…`):

    entry (0x07) = 2      tick (0x2c) = 1      SC_VERSION (0x18) = 0x10000

stable over 94 s and **unchanged across eight host kicks** (`resets` moved, so
they reached the core).  So there is one init pass and one dispatch pass, and
the dispatch pass is the **timer's**.

## 4. The re-emit: built, correct, firing — and it does not re-fetch

The hypothesis: the vendor emits the PDS task with `emitpds.tasks.taske` from
the USSE side (`EmitLoopBack`, `sgx_utils.asm:2206`); our design emits nothing,
so no PDS task is ever pending and perhaps the program is never fetched.

* **The encode was verified byte-identical** to the vendor's own instruction:
  `echo 'emit.t2 r0, r2, r4 [f12 f13 imm=3138]' | sgx-asm -b -c sgx540`
  → `00800104 fb20b188`, round-tripping.  (An earlier claim that the toolchain
  "cannot encode it" was my own invocation error — the assembler takes stdin
  with `-b` — and is withdrawn.)
* `UK_REARM` places it on `tmr_tick_end`, the one dispatch pass that runs, with
  the vendor's resolved payload `imm=0x843` and its two loader-patched operands
  (`pds_size`, `pds_addr`).  Measured: **the one-shot flag goes 1 → 0, so the
  emit executed, and `entry`/`tick` do not move.**  Control `fix1`, the same
  instrument without the emit, reads identically.
* On the way, a hard constraint was measured: **emitting from `init_set`'s task
  end faults the launch outright** —
  `microkernel did not start (EDM launch faulted: BIF int 0xe000 fault
  0xfef4000)`, probe `-5`, no `card1`.  The EDM must not be re-emitted while it
  is starting.
* And the deeper block: after eight host kicks the flag is **still armed**, so
  **no `a.end()` runs at all** in that configuration.  `init_set`'s is the only
  task end, and an idle boot apparently never reaches a second.  The next
  question is therefore not "who re-emits" but **does the kick path reach the
  microkernel at all** (HANDOFF-77 §0b).

## 5. The vendor's microkernel, as source and as a binary

The DDK 1.8 drop (`/home/psyden/Pdrive/GFX_Linux_DDK-master`, outside the repo)
contains the SGX540 microkernel as source **and** as a prebuilt binary
(`binary2_540_omap4430_android_{debug,release}/…/ukernel.bin`, 22 192 B
`cfc577aa…` / 19 984 B `ff69e400…`).

* `services4/srvinit/devices/sgx/eventhandler.pds.asm` is the program
  `EVENT_PDS_EXEC` points at — a **PDS dispatcher** that publishes the EDM's
  interrupt registers and `halt`s after one `movs doutu`.  It does no command
  processing.
* **The event bit map is not `EUR_CR_EVENT_STATUS`**:
  `EURASIA_PDS_IR0_EDM_EVENT_TIMER` is **bit 1** there, versus b29 in STATUS,
  which is what our entry polls.
* The dispatcher's `INPUT_*` constants are **host-patched at load**
  (`sgxinit.c:2507-2510`; word indices in the generated `eventhandler_pds.h`),
  and **our loader patches only `ctlblk` and `pdtable`** — no PDS-input routing
  at all.  That is the largest structural difference between the designs.
* **Reference only** (operator direction, DECISIONS §115): the open driver
  compiles its own microkernel.

## 6. The desktop, incidentally

Worked at the operator's direction part way through the session.

* **llvmpipe had been lost by the session-71 rebake** (6.9 MB softpipe, no
  `libLLVM*` on the root).  Restored from the cached Alpine Mesa 21.3.9 gallium
  build: `epitaph` and `catacomb` both map `libLLVM-13.so`,
  `GL_RENDERER = llvmpipe (LLVM 13.0.1, 128 bits)`.  An offscreen 1024×585
  `glClear+glFinish` measures **4.3 ms vs softpipe's 48.0 ms**.  **The hd3
  flasher image still lacks both**, so a reflash loses it.
* **The fast-tap panel blank is catacomb's own double-tap sleep gesture**, not a
  rendering race.  kprobes on `drm_panel_disable`/`_enable`/`bl_power_store`: a
  fast-tap burst gives **4 disable + 4 enable, all `comm="catacomb"`**, one
  cycle 34 ms apart, and `bl_power_store` 0 hits.  `windows/mod.rs
  on_double_tap()` sleeps for a double-tap in the centre 20 % of the handler;
  `input.rs:505` wakes on *every* double-tap.  A guard
  (`gesture-handle --height 0`) is deployed and measured — **temporary**, the
  real fix is one guarded line in catacomb (DECISIONS §114).

## 7. Evidence

* `docs/captures/sgx540-entry-counter-2026-11-26.txt` — the instrument, the
  correction, the re-emit rounds, and **an evidence inventory** saying which of
  this session's numbers may be cited.
* `docs/captures/sgx540-vendor-ukernel-source-2026-11-26.txt` — the vendor
  source, entry table, PDS patch indices, four retractions.
* Live reads on the unit: `status` (registers incl. `DMS_CTRL`, `DMS_AGE`, the
  OCP pair), `ctlblk` (words 0x07 / 0x2c / 0x18 / 0x3f), `pds`, `relaunch`.
* Traps 398–404.
