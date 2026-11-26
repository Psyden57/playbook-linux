# Session 75 (absolute) — the EDM never re-dispatches; the timer fires but nothing consumes it; a dispatch bit is absent from the silicon

> Numbering: absolute `75` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era session 69**
> (`absolute = Linux-era + 6`).  Continues session 74, which got the open
> `powervr_sgx` SGX540 microkernel to start and to print `1/1 passed` for a NULL
> job.

Date: 2026-11-26.  Unit at the end: **Linux `7.3.3 #5 SMP`** on hd3, region 0
**`0x02C1DDC2`**, region 1 **`20a5edd3…`**, `BOOTCTR_FCTRY` **`0x2f`** (topped to
`0x32` mid-session; the experiment boots spent the difference; `/init` refills
below `0x1e`), `nproc` 2 / PID 1 affinity **`0,1`**, battery Full 4.130 V,
panel 0 DSI errors, `wlan0` **`192.168.0.231/24`** (`usb0`
`169.254.0.1/16`), `card1` = `powervr_sgx`, `firmware running: 1`,
`uk-lockups 0`, `BIF_FAULT 0`, `resets 0` (the last two boots submitted nothing).

## Goal of this session

HANDOFF-74 §0b, and nothing else: **one** counter that the EDM advances per
dispatch, read from inside the driver, with two NULL jobs submitted, interpreted
against the four outcomes that document lists.  The instruction was explicit —
"do not ask another register question before that".

Three follow-up measurements followed in the same session, each because the
previous instrument's *shape* could not answer the question it was pointed at.

## Confirmed (measured, read in a file, or seen on the bench)

**Measurement 1 — entry counting (job window).**

* **The event program is entered once and never again.**  Entry counters
  (control-block words **0x3d** = `kick` entries, **0x3e** = empty-CCB exits) read
  **0 in all 12 samples across a whole 2 s job window**, while the kernel ring
  held the command (`ccb wo 1 ro 0`) and the microkernel's own read of the same
  ring stayed `0/0`.  [capture
  `docs/captures/sgx540-ukernel-kick-counter-2026-11-26.txt`]
* **Six back-to-back NULL jobs cost six GPU resets**, with no counter moving.
* **A `1/1 passed` here is the driver's 2 s timeout** (trap 388): the passing
  runs took **2016806** and **2020499** µs against `job_timeout_ms = 2000`; the
  syncobj is bound to `drm_sched`'s `s_fence->finished`, which the reset finishes
  with `errno = 0` and no error bit, and `drm_syncobj_array_wait_timeout()`
  returns 0 without inspecting the error.  The context is then `GUILTY`, which is
  why the next run on it reads `0/1`.  [report 278 §3]
* **`consumed 2..5` (report 277) measured nothing** (trap 389): a reset sets
  `ccb_reset_seq := ccb_issued`, so an idle ring reports `consumed == issued`
  (this boot read `issued 1 consumed 1`).
* **The running build was `ccb2`, not `invfix`** (trap 390): hd3's file had been
  swapped after the module load, and the firmware is read only at module load.
* **hd3's dropbear has no `scp`/`sftp-server`** (trap 391); a firmware push is
  `cat FILE | tools/pbssh.sh 'cat > /path'`, verified by `md5sum`.

**Measurement 2 — a dynamic tick counter (one boot).**

* **The timer branch does not run.**  `UK_TICKLOG` increments a 32-bit counter in
  the microkernel's own memory — free control-block words **0x2c** (low 16) and
  **0x2d** (high 16), with a carry — once per pass through the timer branch; the
  host compares two reads.  After **12 s idle: 0**, `resets 0`, IRQ 147 never
  asserted.  [capture `docs/captures/sgx540-tick-counter-2026-11-26.txt`]
* **Negative control, in the emulator:** `init` + three timer events + one bare
  doorbell gives `0x2c` = 0 → 3 → 3, i.e. it advances once per timer event and a
  doorbell is not a tick.
* **The init task really does run**, on three independent proofs, none of them
  the `microkernel started` line: `SC_VERSION` (control block `0x18`) =
  `0x00010000`, a microkernel-only store; `EVENT_HOST_ENABLE` b29 set
  (`0x0130` = `0x20004000`), ORed in by `init_set` after the timer write; and
  `BIF_FAULT 0` throughout.
* **"microkernel started" is the host's own bit** under `edm_init=1` (trap 396):
  the branch does `msleep(20)`, checks `BIF_INT_STAT`, then writes
  `INIT_COMPLETE` itself, and `init_set` writes it too.

**Measurement 3 — the dispatch registers (one boot, one module change).**

* **The timer fires.**  `EUR_CR_EVENT_TIMER` (`0x0acc`) reads **`0x010228fe`**,
  `ENABLE` b24 set; five samples 2 s apart all sit inside `0..0x04B000`, i.e. the
  counter cycles in its period at ~1 kHz.  [capture
  `docs/captures/sgx540-dispatch-regs-2026-11-26.txt`]
* **Nothing consumes it.**  `EVENT_STATUS` b29 (TIMER) is latched at every
  reading and never cleared; the GPU's IRQ (**147**, `GIC_SPI 21`) has never
  fired.
* **Every other dispatch register reads back as written:** `EVENT_KICKER`
  (`0x0ac4`) = `0x0f002010`, `EVENT_PDS_EXEC` (`0x0a68`) = `0x00804090`,
  `EVENT_PDS_DATA` (`0x0a6c`) = `0x00000003`, `EVENT_PDS_INFO` (`0x0a70`) =
  `0x00013d01`.
* **`EVENT_PDS_ENABLE` (`0x0a58`) does not hold what is written:** the driver
  writes `0x2204f00f`, the register reads `0x2204b00f`, stable across reads.
* Both `removed`-module and patched-module builds were proved: with the dump edit
  reverted the tree rebuilds the previously installed module `2e138afc…`
  byte-for-byte.

**Measurement 4 — the per-bit sweep (one boot, one module change).**

* **`EVENT_PDS_ENABLE` bit 14 (`SW_EVENT`) is absent on this silicon.**  A
  debugfs node writes the register: the full word, then each bit of that word
  **alone**, reading back between each.  Every bit of `0x2204f00f` sticks
  **except 14**, which returns 0 even when written on its own — absent, not
  masked.  [capture `docs/captures/sgx540-pds-enable-sweep-2026-11-26.txt`]
* **`SW_EVENT` *is* implemented in `EVENT_HOST_ENABLE`** (`0x0130` = `0x20004000`,
  bit 14 set), so the header's field is real and mis-attributed by register.  This
  is the fact sheet's first **C** fact to contradict a **K** one.
* **Session 74's `SGX540_CR_EVENT_PDS_ENABLE_ALL 0x2204f00f` was therefore a
  no-op** — functionally identical to the SGX545's `0x2204b00f` it replaced.  Of
  the four corrections report 277 §3 lists, this one changed nothing; the other
  three stand.

**Bench facts re-read at the close (not carried from earlier notes).**

* Region 0 media trailer, read from `/dev/mmcblk1` at `0x8100000 + 10450520 − 8`:
  `1f 2d c8 d7 c2 dd c1 02` = **`0x02C1DDC2`**.
* Region 1, 64 MiB at raw `0x29000000`: sha256
  **`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`**.
* hd3: ext4 magic `0x53EF` at `0x49000438`, block count `15273984`; root
  `/dev/loop0 ext4 rw,relatime,nobarrier`, 56.5 G with 698.1 M used.
* `BOOTCTR_FCTRY` **`0x2f`** (`dmesg | grep pbrpmb`: `0x2f >= threshold 0x1e, no
  write`).
* Battery **Full, 100 %, 4.130 V**; panel: **0** `dsi_if_enable`/`NO_PACKET_SENT`
  lines in this boot's `dmesg`; `wlan0` **`192.168.0.231/24`**, default via
  `192.168.0.1`.

## Inferred (reasoning from evidence, not directly measured)

* **The fault sits between "a PDS source fires" and "the event program is
  entered".**  Strength: strong as a *location*, since four measurements bracket
  it — the source is enabled and fires, the event latches, the host IRQ never
  fires, and the counters show the entry is never reached.  What is *not*
  established is which link in that chain fails.
* **The next untested link is the PDS exec address.**  `EVENT_PDS_EXEC` reads back
  as `0x00804090`, but the only check on it (`check_event_pds()`) compares it with
  a value derived from our own heap layout, so it proves the store landed and
  nothing about the target.  Strength: moderate — it is the only register on the
  path nobody has independently verified, so it is the best remaining candidate
  by elimination rather than by evidence.
* **The event entry may not execute at all past init.**  `k_getmiscinfo` needs no
  client PD, so a `GETMISCINFO` submit would prove it.  Strength: weak but cheap
  to settle; the driver sends one at init, which is why `SC_VERSION` is set.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The job passed, so the microkernel must be running."**  Killed by timing:
  the two "passes" took 2016806 and 2020499 µs against a 2000 ms timeout, and the
  reset count rose on every one.  The pass was the fence being signalled *by the
  reset*, without an error.
* **"A counter in the control block will show the task running."**  Wrong *shape*
  of instrument: `ctlblk` is static memory read by the host, so a 1 kHz event can
  never move a word there.  Killed by building a dynamic counter instead, which
  answered immediately.  (The static count of `kick` *entries* was still valid —
  it counts events, not time — so only the timer question was affected.)
* **"`consumed` proves the microkernel took commands."**  Withdrawn; it is a
  ring-reset artefact.  The ring itself (`wo`/`ro`) says the opposite.
* **"The driver's timer period is 1000× too long."**  Killed by reading the
  register: the counter cycles inside its period.  The comparison put a **count of
  clock cycles** (307 200) against a **rate** (307 kHz) and called the ratio a
  factor.  Units first, factor second.
* **"The init and job kicks going to different registers (`EVENT_KICK2` 0x0ac0 vs
  `EVENT_KICK` 0x0ac8) is the bug."**  Killed by the DDK's own kernel driver,
  which does exactly the same under `SGX_FEATURE_MULTI_EVENT_KICK`; the
  `0x0ae8` in `pvrsgx_regs.h` is an unused SGX545-family constant.  No boot
  spent — found by reading the vendor's call sites rather than its headers.
* **"`EVENT_PDS_ENABLE` bit 14 is a defect we can fix."**  It is not
  implemented on this silicon at all, so the "fix" that added it in session 74
  wrote a bit the hardware ignores.
* **"A passing read-back validates the value."**  `check_event_pds()` compares our
  derivation against our own write; it cannot detect a wrong target.  Narrowed the
  confidence in that gate rather than in the register.
* **Operational near-miss:** a variant build with no explicit output path wrote
  its JSON over `ukernel/sgx540_ukernel.bin`, and the offline emulator then read
  garbage.  Both default artifacts were rebuilt and both suites re-run before
  anything was deployed.  **Always give a variant build explicit output paths.**

## Unknown / open questions

* **Does `EVENT_PDS_EXEC` (`0x00804090`) point at code this core will execute?**
  *Settle by:* reading the PDS program's first words through the CPU-side mapping
  (`sdev->fw.cpu[PVRSGX_FW_PDS0]`) and comparing the entry at that address with
  the manifest's `event` entry.  This is HANDOFF §0b.
* **Why the doorbell does not open.**  *Settle by:* reading `EVENT_KICK` (0xac8),
  `EVENT_KICKER` (0xac4) and `EVENT_STATUS` (0x12c) from inside the driver around
  one NULL job.  Two cheaper checks already came back clean (the kick offsets, and
  `event_pds_addr()`'s derivation) — do not repeat them.
* **Does the event entry execute at all past init?**  *Settle by:* a
  `GETMISCINFO` submit, which needs no client PD.
* **Why the latched TIMER event is never acknowledged.**  *Settle by:* the same
  doorbell read — if `EVENT_STATUS` b29 stays set across an entry that ran, the
  ack is missing; if the entry never ran, it explains itself.
* Unchanged and still open: `0xb14`'s write (trap 387), `sgx540_init_regs`, the
  microkernel register tables (0x618/0x62c/0x694/TA 0x200/3D 0x428, all
  hypothesis), `USE_CODE_BASE` existence, and the DPM/TA/ISP/TSP/PBE/PDS state map.

## Superseded / uncertain

* **Report 277 §3's `SGX540_CR_EVENT_PDS_ENABLE_ALL 0x2204f00f` as a fix** —
  superseded by report 278 §10: the bit is absent on this silicon, so the change
  is inert.  Report 277 is left as written; 278 is current.
* **Report 277's "the microkernel now takes commands off the CCB"
  (`consumed 2..5`)** — withdrawn; trap 389.
* **Report 277's / HANDOFF-74's "one or two jobs pass per GPU reset, then it
  stalls"** — the *pattern* is real but its interpretation was wrong: every job is
  a timeout, and whether the fence signals is a race between two 2000 ms timers.
* **Report 277's and this session's own early claim that the driver's timer period
  was 1000× too long** — withdrawn; trap 395.  The period is correct.
* **HANDOFF-74's record of the loaded firmware (`invfix`)** — the first boot of
  this session ran `ccb2`; trap 390.  Both builds start the microkernel; neither
  dispatches.
* **This session's own mid-session "the wlan0 lease moved to 10.195.132.240"** —
  it was a transient DHCP lease; the unit is back on **`192.168.0.231/24`**.  The
  SSH path is over `usb0` either way.

## Artefacts produced

* `docs/reports/278-sgx540-edm-never-redispatched-2026-11-26.md` — this
  session's report (§1–§6 the first measurement, §8 timer, §9 registers, §10
  sweep).
* Captures, all new:
  `docs/captures/sgx540-ukernel-kick-counter-2026-11-26.txt`,
  `docs/captures/sgx540-timer-clock-2026-11-26.txt` (contains a retracted claim,
  annotated),
  `docs/captures/sgx540-tick-counter-2026-11-26.txt`,
  `docs/captures/sgx540-dispatch-regs-2026-11-26.txt`,
  `docs/captures/sgx540-pds-enable-sweep-2026-11-26.txt`.
* `docs/contradictions/sgx540-event-kick2-offset.md` (new) — raised **and
  resolved/retracted in this session**; kept so the wrong reading is not
  re-derived.
* Patch series, four new entries in `rootfs/powervr-sgx/`:
  `0003-sgx540-kick-counter.patch` (entry counters **and** the session-74 CCB
  trace word relocation that was never in 0002),
  `0004-sgx540-tick-counter.patch` (the dynamic counter),
  `0005-sgx540-dispatch-regs-dump.patch` (seven read-only registers in
  `status`), `0006-sgx540-pds-enable-sweep.patch` (the per-bit sweep node).
  Applying 0001→0006 to a pristine `95f78ca` reproduces every deployed artifact;
  the firmware md5s `229b838c…` (kick1) and `c94d8d2f…` (tick1) and the module's
  `.text`/`.rodata` were all checked.
* `local/work/powervr_sgx` (gitignored) — `ukernel/ukernel.py` carries
  `UK_KICKLOG`/`UK_TICKLOG`, `pvrsgx_dump.c` the seven registers,
  `pvrsgx_debugfs.c` the sweep node.
* **No kernel patch change, no region-0 flash, no region-1 or hd3 write.**  The
  only device-side changes are files on hd3 (below).

## Note on reverting

Every experiment was a **file swap on hd3**, not a flash — firmware and module
both.  Reverted and proven by `md5sum` at the close:

* `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`12f12abb…`** (`invfix`, the
  durable image).  Inert builds beside it: `.kick1` (`229b838c…`), `.tick1`
  (`c94d8d2f…`), `.before-kick1` (a duplicate of `invfix`), `.session73-raw`.
* `/lib/modules/7.3.3/.../powervr_sgx.ko` = **`9a0ebbe3…`** (the module plus the
  read-only register dump, patch 0005).  Beside it: `.sweep` (`a4aa13a9…`, the
  per-bit node — **it writes on read, do not leave it installed**),
  `.dumpregs`/`.dumpregs-prev` (same hash as live), `.live-prev` (`2e138afc…`,
  the pre-session module), `.bak`, `.brn31620`.

Region 0 is untouched at `0x02C1DDC2`.  In the source tree the instruments are
gated on environment flags (`UK_KICKLOG`, `UK_TICKLOG`), so the un-instrumented
build reproduces `7056fc74…` byte-for-byte.  No restore is required: the unit is
on Linux by the operator's direction and the desktop's software rendering is
expected until the open driver draws.
