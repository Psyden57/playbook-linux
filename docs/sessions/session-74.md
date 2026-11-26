# Session 74 (absolute) — the SGX540 open microkernel starts and completes a job

> Numbering: absolute `74` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era session 68**
> (`absolute = Linux-era + 6`).  Continues session 73, which had measured the
> SGX540's identity, bound the open driver on hardware, and built a microkernel
> the driver rejected for want of a list-0 path.

Date: 2026-11-26.  Unit at the end: **Linux `7.3.3 #5 SMP`** on hd3, region 0
**`0x02C1DDC2`**, region 1 `20a5edd3…`, `BOOTCTR_FCTRY` **`0x1f`**, `nproc` 2 /
PID 1 affinity `0,1`, battery Full 4.150 V, panel 0 DSI errors, `wlan0`
`192.168.0.231`, `card1` = `powervr_sgx`, `firmware running: 1`,
`uk-lockups 0`, `BIF_FAULT 0`.

## Goal of this session

Track A of HANDOFF-73 §0b: give the SGX540 microkernel a list-0 / PD-table
path so `powervr_sgx` accepts and runs it, then a kick.  "Done" was the driver
starting the microkernel on the SGX540.

## Confirmed (measured, read in a file, or seen on the bench)

* **The image is accepted and the microkernel starts.**  `open microkernel
  invfix, interface 1.0, features 0x8` then `microkernel started`, with the
  driver's debugfs agreeing: `firmware running: 1`, `hostctl init 0x1`,
  `uk-lockups 0`, `resets 0`, `BIF_INT_STAT 0x00008000` (FLUSH_COMPLETE only,
  **no fault**), `BIF_FAULT 0`, `EVENT_STATUS 0x20000000`.
  [`docs/captures/sgx540-ukernel-start-2026-11-26.txt`]
* **`edm_init=1` is required to start it**, and without it the probe fails with
  `BIF int 0xc002, fault 0x8779000`, `error -110` — the driver's documented §9
  single-context launch path.  Repeated on several boots.
  [same capture]
* **The NULL job passes** (`submit-null-job` → `ok`, `1/1 passed`) and the driver
  now reports the command *consumed* (`consumed 2..5`, was universally `0`).
  [same capture; `docs/reports/277` §5]
* **The image's four SGX545 assumptions, corrected**: one directory list (no
  `BIF_DIR_LIST_BASE1` 0xc38, no `BIF_BANK0` 0xc78); the HW context's
  `page_dir` word is a **slot** into the driver's PD table at
  `kernel_code.base + 0xf000`; `features = 0x8` (SINGLE_CONTEXT set, SECURE
  clear) with a `pdtable` patch; `EUR_CR_PDS_CACHE_STATUS` (0x138) has **one**
  completion group (bits 0-4), not the SGX545's two.  The last is the decisive
  one.  [`docs/captures/sgx540-ukernel-list0-2026-11-26.txt`; report 277 §2]
* **`EVENT_PDS_ENABLE` (0x0a58) is the real per-source dispatcher**, and for
  this core the DDK's own `srvinit` derives **`0x2204f00f`**; the driver programs
  `0x2204b00f`, one bit short — `SW_EVENT` (0x4000).  Fixed for the `sgx540`
  entry.  [`docs/reference/sgx540-ddk18-facts.md` §2]
* **The rev-120 SGX540 errata list is ~50 BRNs, not three** (three are
  "workaround in uKernel").  [same file §1]
* **`USE_CODE_BASE`'s address shift is derived**: `INSTRUCTION_CACHE_LINE_BITS
  + 3` = **8** for the 540, **7** for the 545; both match our entries.  [§6]
* **The `ta3dctl` layout derives to `sKernelCCB` +0x58 / `sKernelPDDevPAddr`
  +0x5c**, corroborating our driver's `static_assert`s and the image's `0x16`
  read.  [§9]
* **`0xb14` is `EUR_CR_USE1_SERV_PIXEL`** — a USSE pixel service queue
  (COUNT b0-15, EMPTY b16), not an EDM task-progress register.  [§3]
* **`/dev/mem` no longer reaches the GPU window**: all windows return
  `Errno 13`, `CONFIG_STRICT_DEVMEM` is off, `IO_STRICT_DEVMEM` is not in the
  config, and `/proc/iomem` shows `56000000-56003fff : 56000000.gpu
  powervr_sgx`.  [report 277 §7]
* **The SGX545 serial image is byte-identical** (`83b5b0a7…`, 14536 B) after
  every change, checked each time with `ukernel/golden/check.py`.
* **Region 0 `0x02C1DDC2` is byte-true on media**: trailer
  `1f2dc8d7 c2ddc102`, and the zImage embeds the corrected microkernel
  (`image_crc32 0xc1eebfaf`, build `invfix`), verified by decompressing the XZ
  payload and reading the wrapped image's own header CRC.

## Inferred (reasoning from evidence, not directly measured)

* **The EDM runs once and then stops dispatching.**  Strong for the *observed*
  boots: one or two NULL jobs pass right after a GPU reset, then none, while a
  kernel-CCB trace showed not one of the microkernel's own stores running even
  for a passing job.  The weakness is that "not one store" also fits "the
  microkernel never dispatched at all on that boot", so the two readings have
  not been separated.
* **The remaining failure is a state/initialisation class**, not another
  field-layout bug: the difference between the first job and the third is what
  a reset reinitialises.  Weak — this is a pattern, not a mechanism.
* **`0xb14` being a service queue makes the keep-alive wrong**, which would
  explain a quiescent EDM on a core whose microkernel relies on that counter to
  stay alive.  Moderate: it is a K fact that the register is a queue, and a
  plain inference that the open image's write is thereby ineffective.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`edm_init` is a workaround."**  It is the driver's documented §9 launch for
  single-context cores.  Calling it a hack would have hidden the right model;
  the driver's own comment names the path.
* **"The masks are fine, the stall is the doorbell."**  Killed by the DDK 1.8
  header: `0x138` has one group on this core, so `0x27`/`0x4b` could never be
  satisfied.  Fixing them turned `consumed 0` into `1/1 passed`.
* **"`0xa74` is mis-modelled as TA flags."**  Wrong, and retracted: `0x208200`
  decodes as `DMS_CTRL`'s 32/32/32 tasks, exactly what the DDK computes for our
  core.  Killed by decoding the value against the field map.
* **"`DMS_AGE` is missing."**  Wrong: it is an optional *build* flag, and our
  init already writes `0xa78 = 0x2c`.  Killed by reading `sgx_options.h`.
* **"The CCB trace shows the timer ticking and the kick never entered."**  Wrong
  — the trace wrote `0x3a`/`0x3b` (which `UK_WAITLOG` also uses) and `0x3c`
  (the cleanup force-complete marker), so it could not work.  Killed by
  cross-checking the words against the core config; the corrected trace is the
  one in §Confirmed.
* **"`FIX_HW_BRN_31620`'s dummy page is the stall."**  Measured and rejected:
  the microkernel started, the first run looked better (two passes), then
  repeats read 0/5 and 1/5.  Reverted, and the rebuild is byte-identical.
* **"A running `powervr_sgx` still permits the `/dev/mem` register read."**
  Killed by the `Errno 13` and the `/proc/iomem` reservation.
* **"One pass means one job consumed."**  Killed by seeing `1/1 passed` and
  `consumed 0` on the same boot.

## Unknown / open questions

* **Does the microkernel's task run after the init one?**  Settle with a counter
  the *EDM* advances and the host can read without `/dev/mem` — a driver debugfs
  node, or the image's own `SC_COMPLETED` made visible — then submit two NULL
  jobs.  This is the next experiment.
* **Is the `0xb14` write harmful or merely ineffective?**  Read the register
  around a kick from inside the driver; the K fact says it is a queue, so it may
  also be *consuming* something.
* **Why does the image's `SC_COMPLETED` stay 0 after a passing job?**  It should
  bump per retired command.  Either the pass does not come from the
  microkernel's completion path, or the store is not landing.
* **Does `USE_CODE_BASE` exist on rev-120 silicon?**  Write/read-back **inside
  the driver's reset sequence** — never a userspace read (trap 376).
* **`sgx540_init_regs`**: still not derivable statically; needs a capture from a
  working microkernel boot.
* **The DPM/TA/ISP/TSP/PBE/PDS state map** (fact sheet Gap 10).
* **Which of the ~50 rev-120 BRNs actually bite**, in particular the three
  marked "workaround in uKernel".

## Superseded / uncertain

* **The fact sheet's "exactly three BRNs at rev 120"** — superseded by
  `sgx540-ddk18-facts.md` §1 (~50).  The earlier correction was itself wrong;
  the sheet now records both.
* **Report 276's "the microkernel is a `cores/sgx540.py` data file"** reasoning
  — superseded by session 73 (it is a two-context image) and now by this
  session's list-0/pdtable work.
* **Trap 380's "the GPU firmware must live in the initramfs"** — measured to be
  narrower: with the driver a module, the firmware is read from hd3 after it is
  mounted, and the initramfs copy was inert.  Recorded in
  `sgx540-ukernel-start-2026-11-26.txt`; the trap is left standing because it is
  right for a built-in/early driver.
* **My own CCB-trace reading from earlier in this session** — withdrawn; see
  dead ends.

## Artefacts produced

* `docs/reports/277-sgx540-ukernel-first-job-2026-11-26.md` — this session's report.
* `docs/captures/sgx540-ukernel-list0-2026-11-26.txt` — the host-side image work.
* `docs/captures/sgx540-ukernel-start-2026-11-26.txt` — the first start on silicon.
* `docs/captures/sgx540-ukernel-first-job-2026-11-26.txt` — the NULL job, the
  pass/stall pattern, BRN-31620, the `/dev/mem` constraint.
* `docs/reference/sgx540-ddk18-facts.md` — every K fact from the DDK 1.8 drop.
* `rootfs/powervr-sgx/0001-sgx540-core-entry.patch` — regenerated (adds the
  `SGX540_CR_EVENT_PDS_ENABLE_ALL` correction).
* `rootfs/powervr-sgx/0002-sgx540-one-context-list0.patch` — new: the list-0/pdtable
  path, the `0x138` mask, and the `test_pd_table_load` case.
* `rootfs/powervr-sgx/{sgx540.py,README.md,firmware/}` — refreshed; the firmware
  is the wrapped `invfix` image (md5 `12f12abb…`).
* `kernel/`… no kernel patch change; the **module** was rebuilt on hd3 only.
* Images: region 0 **`0x02C1DDC2`** (`boot/chainloader/kernel.img`, 10,450,520 B,
  sha256 `70474c01…`), on media; `0x8F1B3C75` and `0xBC5FB130` superseded.

## Note on reverting

The BRN-31620 module experiment was reverted and the revert **proven**: the
rebuild reproduces `2e138afc0e832a4e7b41d97f74452508`, byte-identical to the
module installed on hd3.  The experiment's module is kept on the unit as
`powervr_sgx.ko.brn31620` (inert).  Region 0 is at `0x02C1DDC2`, which is the
durable image (the fixed firmware), not a test image.  No restore is required:
the unit is on Linux by the operator's direction, and the desktop's software
rendering is expected until the open driver draws.
