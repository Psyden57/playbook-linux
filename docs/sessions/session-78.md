# Session 78 (absolute) — the open microkernel takes and completes jobs

> Numbering: absolute `78` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era session 72**
> (`absolute = Linux-era + 6`).  Continues session 77, which answered the kick
> question and left the dispatch question open.  Written at the operator's
> direction to keep going until the GPU renders — the session landed the
> dispatch breakthrough; rendering (Mesa + real render jobs) is the next
> mountain.

Date: 2026-11-27.  Unit at the last boot: **Linux `7.3.3`** on hd3 (root
`/dev/loop0` ext4 rw), region 0 **`0x02C1DDC2`** (no flash all session),
region 1 **`20a5edd3…`**, `BOOTCTR_FCTRY` **`0x28`** (the Linux top-up fired
during the session's heavy rebooting — self-maintaining), `card1` = `powervr_
sgx` running the open microkernel **`kick10`**, desktop llvmpipe, `wlan0`
down (the earlier GPU resets), `usb0` up.  Sixteen boots, all firmware/module
file swaps on hd3 — no media writes.

## Goal of this session

HANDOFF-78 §0b (does `DMS_AGE` unstick the dispatch?), then whatever the
measurements pointed at next.  The operator's direction: "keep going until I
say so — the goal is the GPU rendering the desktop and apps."

## The result

**The open microkernel dispatches continuously and completes jobs** — the
first real job completions in the port's history:

* `submit-null-job`: **null job signalled in 946 µs / 427 µs** (was the
  driver's 2,000,000 µs timeout);
* `submit-null-job-many`: **600 null jobs in 91 ms**;
* the whole null-job family passes (`-timeline`, `-chain`, `-cache-ranges`,
  `-vm-destroyed`, `submit-all-or-nothing`, `submit-invalid`,
  `submit-other-file`, `submit-wait-unsignalled`, `rt-dataset-*`,
  `context-*`, `mmap-bo`) — **30/50**, and `dmesg` has no timeout lines;
* `resets: 0` for the whole suite (before the fix, every job cost a GPU
  reset — trap 388's signature since session 74);
* `ccb wo 103 ro 103 issued 615 consumed 615` — the ring drains;
* `irqs: 5`, `int 0x2/0x2` — SW_EVENT/ACTIVE_POWER signalling live.

**The fix was ONE BIT: `EVENT_PDS_INFO` (0x0a70) `0x00013d01 → 0x00013c01` —
`USE_PIPELINE` was TOGGLE (round-robin between the two USE pipes); the
vendor's derivation is `USE_PIPELINE_0`** (sgxinit.c:3640-3646).  Under
round-robin the event tasks alternated pipes and a stuck pipe blocked the
toggle forever — the one-dispatch-then-stuck signature.  Report 281.

## Confirmed (measured)

* **The tick counter works** — `init_set` seeds it GPU-side (0x100 → word
  0x2c) and the dispatch pass reads 0x101 at exactly the named word.  The
  GPU's initial view of the control-block tail is not the CPU's zeros — the
  old counter read 0xffffffff, +1 wrapped to 0, and looked dead (trap 409).
  The mystery word-0x2a=1 vanished with the old code.
* **The true entry count was 2 per life** — "4" was the old `UK_ENTRYLOG`
  counter still emitting beside `UK_ENTRYFIRST` (trap 410; found by
  disassembling the entry top).
* **The timer's dispatch is real and singular** — `UK_TIMERDIV=100`: the
  EDM-init line at +20 ms reads `event status 0x00000000` (no edge yet) and
  the second fetch waits for the first edge.  After the fix the counters
  climb continuously.
* **`DMS_AGE = ENABLE | 0x2c`**: sticks (0x0001002c), no effect (the
  vendor's own default is 0 — "disable data master aging").
* **The vendor launch-script diff is complete**: six registers were the whole
  remaining delta (guards checked against sgx540defs.h).  Bisect:
  **`CACHE_CTRL = 0x0000ffff` — the vendor's own value — breaks event
  dispatch entirely** (trap 411; not written).  The other five (`USE_CTRL`
  also removed in the bisect, `USE_MOE`, `ISP_DEPTHSORT`, `USE_G0/G1`,
  `DMS_AGE`) are compatible and programmed.
* **The task-end emit with the vendor's operand derivation fires and does not
  fetch** — session 76's negative was measured with garbage operands
  (withdrawn; retried correctly, same result).  The init_set-end launch fault
  was plausibly the same bogus address.
* **The vendor's working SGX540 image has no `phas` and no op0xff/op0x100** —
  and `UK_NO_PHAS` changes nothing; our task-lifecycle shape now matches the
  vendor's and the blocker was elsewhere.
* **The DOUTU task-control words** were SGX545-shaped (`0x04000000` sets a
  reserved DOUTU1 bit and leaves MODE=PARALLEL; word 2 was 1) — fixed to the
  vendor's `MODE_PERINSTANCE` shape (kept; not sufficient alone).

## Dead ends and wrong assumptions (valuable — do not skip)

* **"LOOPBACK b0=1 means the FIFO is full"** — backwards: the vendor's
  `and.testz p2 …; p2 ba ELB_FifoFull` loops while the bit is ZERO, so 1 =
  space.
* **"The PIXEL event-PDS pair being zero is the bug"** — that pair belongs to
  the VDM context-store terminate program; a single-context design never
  uses it.
* **"The launch's second fetch is launch-internal"** — killed by the
  TIMERDIV=100 boot (the second fetch waits for the first timer edge).
* **"DMS aging is the blocker"** — no effect.
* **"4 fetches mean two die at the phas"** — double counting (trap 410).
* **"The vendor's launch register set is complete in our driver"** — it was
  not; completing it BROKE dispatch via CACHE_CTRL — then taught us that
  register is load-bearing.
* **"The emit doesn't re-fetch" (session 76)** — garbage operands; retried
  correctly, same negative.  The emit line is closed correctly now.

## Unknown / open questions

* **A real render job** — the driver's job_types query advertises only the
  NULL job; the TA/3D path (`do_ta`/`do_3d` and the carried-over register
  tables 0x618/0x62c/0x694/TA 0x200/3D 0x428, all hypothesis) has never run.
  *Settle by:* wiring the render job type and driving one TA/3D scene; the
  `pds`/`status` nodes make the programmed state visible.
* **The suite's SGX545-shaped expectations** — parametrize core_id, va_bits
  and heap addresses by the query (a test-side fix).
* **`CACHE_CTRL`'s PARTDM semantics** — sensitive; measure on a working
  system before writing.
* **The owed patch-series export** (`rootfs/powervr-sgx/`) for all the
  session-77/78 driver+firmware changes — nothing is upstream (DECISIONS
  §110).
* Unchanged: `tick2`'s reproducibility (uncommitted ukernel work), the
  DECISIONS §114 catacomb fix, and the DPM/TA/ISP/TSP/PBE/PDS state map.

## Superseded / uncertain

* **"The dispatch is dead after the launch" (session 77 / report 280)** —
  refined then FIXED: the blocker was `EVENT_PDS_INFO`'s USE_PIPELINE bit.
* **"Entry = 2 means init + one dispatch pass" (session 76)** — confirmed,
  after this session's double-counting detour.
* **"The emit does not re-fetch" (session 76)** — withdrawn as measured
  (garbage operands); re-measured correctly, same negative.

## Artefacts produced

* **Report 281** and the capture extended to sixteen boots.
* **Firmware kick4-kick10** (all gated: test_emu 8/8 both cores, SGX545
  golden byte-identical) — instruments (kick counter, seeds, markers), the
  snapshot-limm fix, the tick counter fix.  Live = **`kick10`**
  (`3fbbd11b…`), preserved as `.working` and with every predecessor.
* **Driver changes**: the DMS_AGE write; the vendor launch-script completion
  (minus CACHE_CTRL/USE_CTRL per the bisect); the corrected `UKFW_PDS_ADDR/
  SIZE` emit operands; the DOUTU task-control words (`MODE_PERINSTANCE`);
  **the `USE_PIPELINE_0` fix**; `LOOPBACK`/`USE_SERV_EVENT` in status; the
  doorbell node.  Live module = **`10c6f435…`**, preserved as `.working` with
  every predecessor.
* **Owed:** the patch-series export for all the session-77/78 changes.

## Note on reverting

* The working pair is preserved AND live: `sgx540_ukernel.bin.working` and
  `powervr_sgx.ko.working`.  Reverting to the session-76 state:
  `cp …sgx540_ukernel.bin.fix1w …sgx540_ukernel.bin`;
  `cp …powervr_sgx.ko.pre-doorbell …powervr_sgx.ko`; `reboot -f`.
* Region 0 untouched all session; no restore is required.

