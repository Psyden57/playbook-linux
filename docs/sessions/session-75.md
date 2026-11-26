# Session 75 (absolute) — the EDM never re-dispatches, its timer never fires, and the "passing" job was the timeout

> Numbering: absolute `75` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era session 69**
> (`absolute = Linux-era + 6`).  Continues session 74, which got the open
> `powervr_sgx` SGX540 microkernel to start and to print `1/1 passed` for a NULL
> job.

Date: 2026-11-26.  Unit at the end: **Linux `7.3.3 #5 SMP`** on hd3, region 0
**`0x02C1DDC2`**, region 1 `20a5edd3…`, `BOOTCTR_FCTRY` **`0x32`** (the last
boot topped up `0x1d → 0x32`, `PASS`), `nproc` 2 / PID 1 affinity `0,1`,
battery Full 4.152 V, panel 0 DSI errors, `wlan0` `10.195.132.240/13` (`usb0`
`169.254.0.1/16`), `card1` = `powervr_sgx`, `firmware running: 1`,
`uk-lockups 0`, `BIF_FAULT 0`, `resets 0` (the last boot was the tick
measurement and submitted nothing).

## Goal of this session

HANDOFF-74 §0b, and nothing else: **one** counter that the EDM advances per
dispatch, read from inside the driver, with two NULL jobs submitted, interpreted
against the four outcomes that document lists.  The instruction was explicit —
"do not ask another register question before that".

Two follow-ups were then run in the same session, both because the first
instrument's *shape* turned out to be wrong for the question: the static
`ctlblk` word cannot show a periodic event (trap 394).  They are §"Second
measurement" below.

## Confirmed (measured, read in a file, or seen on the bench)

* **The event program is entered once and never again.**  An entry counter
  (control-block words **0x3d** = `kick` entries, **0x3e** = empty-CCB exits)
  read **0 in all 12 samples across a whole 2 s job window**; the kernel ring
  held the command (`ccb wo 1 ro 0`) while the microkernel's own read of the
  same ring stayed `0/0`.  So the doorbell never opened and `kick` never ran.
  [capture `docs/captures/sgx540-ukernel-kick-counter-2026-11-26.txt`]
* **Six back-to-back NULL jobs cost six GPU resets**, with no counter moving.
* **A `1/1 passed` here is the driver's 2 s timeout** (trap 388): the passing
  runs took **2016806** and **2020499** µs against `job_timeout_ms = 2000`, the
  syncobj is bound to `drm_sched`'s `s_fence->finished`, and the reset finishes
  that fence with `errno = 0` and no error bit, so
  `drm_syncobj_array_wait_timeout()` returns 0.  The context is then `GUILTY`,
  which is why the next run on it reads `0/1`.
* **`consumed 2..5` (report 277) measured nothing** (trap 389): a reset sets
  `ccb_reset_seq := ccb_issued`, so an idle ring reports `consumed == issued`
  (this boot read `issued 1 consumed 1`).
* **The running build was `ccb2`, not `invfix`** (trap 390): hd3's file had been
  swapped after the module load, and the firmware is read only at module load.
* **The microkernel stays alive while idle without dispatching**: 12 s after a
  restart, `firmware running: 1`, `uk-lockups 0`, and `EVENT_STATUS` unchanged
  at the value `EDM init` printed.
* **hd3's dropbear has no `scp`/`sftp-server`** (trap 391), so the firmware push
  is `cat FILE | tools/pbssh.sh 'cat > /path'`; `md5sum` verified it.

## Second measurement — the timer, and why it needed a different instrument

Same session, one more boot.  Reading only; the firmware is reverted.

* **The EDM timer never fires.**  `UK_TICKLOG` increments a 32-bit **dynamic**
  counter — free control-block words `0x2c` (low 16) and `0x2d` (high 16), with a
  carry — once per pass through the timer branch, and the host compares two
  reads.  After **12 s idle**: **0**, `resets 0`, IRQ 147 never asserted.  At the
  1 kHz the driver asks for, `0x2c` would be ≈12 000 even under the divisor
  below, so this is not a period question: the dispatch does not happen.
* **The driver's timer period is 1000× the DDK's count.**  `pvrsgx_fw_start()`
  stores `pvrsgx_clock_hz() / UKERNEL_TIMER_HZ` = 307 200 000 / 1000 = **307 200**,
  and `init_set` programs that word verbatim into `EUR_CR_EVENT_TIMER`, whose
  `VALUE` is b0-23.  The DDK computes the same quotient
  (`sgxpower.c:125`, `ui32CoreClockSpeed / ui32uKernelFreq`) with
  `ui32uKernelFreq` = 1000, i.e. **307** for its 1 kHz tick — the driver applies
  the 1 kHz and then divides by it again.  **Not established** that this is why
  the timer never fires; reading `0x0acc` back is the next step.
* **The init task does run** — three independent proofs, none of them the
  `microkernel started` line: `SC_VERSION` (0x18) = `0x00010000`, a
  microkernel-only store; `EVENT_HOST_ENABLE` b29 set (`0x20004000`), ORed in by
  `init_set` after the timer write; `BIF_FAULT 0`.
* **But "microkernel started" is the host's own bit** under `edm_init=1`: the
  driver `msleep(20)`s, checks `BIF_INT_STAT`, then writes `INIT_COMPLETE`
  itself, and `init_set` writes it too.  The log line is not a microkernel report
  (trap 396).

## Inferred (reasoning from evidence, not directly measured)

* **The fault is on the doorbell side, not in the microkernel's handlers.**  The
  counters prove the entry is not reached; nothing yet says *why*.  Two
  candidates, both cheap: the `EVENT_KICK` write (`0xac8`, `NOW` b0) may not
  land, or the entry may be reached but rejected before its first work (the
  session-74 entry-top store faulted, which is that shape).
* **The doorbell is the remaining candidate, not the kick offsets.**  Late in
  the session the differing init/job kick offsets (`EVENT_KICK2` 0x0ac0 in
  `pvrsgx_fw_start()`, `EVENT_KICK` 0x0ac8 in `ccb_write()`) looked like a
  sufficient cause and were written down as one.  **They are not: the DDK's own
  kernel driver does exactly the same** when `SGX_FEATURE_MULTI_EVENT_KICK` is
  set, and `pvrsgx_regs.h`'s `EVENT_KICK2 = 0x0ae8` is an unused SGX545-family
  constant.  Retracted at the session's end from the vendor's source, with no
  boot spent; the contradiction file is closed and trap 392 carries the lesson.
  What survives of the idea is narrower — see the two cleared checks below.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The instrument will show the task running because the job passed."**  The
  entry counter was chosen precisely because a *state* word could not separate
  "never reached" from "reached and empty"; that choice is what produced an
  answer.  Also, the counter was **negative-controlled** in the emulator
  (`init` + 3 empty events → `0x3d=3`, `0x3e=3`) before its zeros were believed
  — without that, "0" is indistinguishable from a broken instrument.
* **"`consumed` proves the microkernel took commands."**  Withdrawn; it is a
  ring-reset artefact.  The ring itself (`wo`/`ro`) says the opposite.
* **"`1/1 passed` means one job completed."**  Killed by the 2016806 µs times and
  by the reset count rising while nothing moved.
* **A near miss worth recording:** the first attempt to rebuild the instrumented
  image also clobbered `ukernel/sgx540_ukernel.bin` (a default-output build wrote
  the JSON there), which made the emulator read garbage.  Both default artifacts
  were rebuilt and both emulator suites were re-run before anything was deployed;
  the outputs for a variant build must always go to explicit paths.

## Unknown / open questions

* **Why the doorbell does not open.**  §0b of the new handoff is the experiment:
  read `EVENT_PDS_ENABLE` (0x0a58), `EVENT_KICKER` (0xac4), `EVENT_KICK` (0xac8)
  and `EVENT_STATUS` (0x12c) back **from inside the driver** around one NULL job.
  Two cheaper checks were done at the session's end and both came back clean, so
  do not repeat them: the kick offsets match the vendor's `MULTI_EVENT_KICK`
  behaviour (trap 392), and `event_pds_addr()` has `srvinit`'s shape, lands 8 MiB
  inside `OTHER_PDS_EXEC`'s 18-bit field, and its read-back gate passes — though
  that gate compares our derivation with our own write (trap 393).
* **Does the event entry execute at all past init?**  `k_getmiscinfo` needs no
  client PD, so a `GETMISCINFO` submit would prove the entry runs (the driver
  sends one at init, which is why `SC_VERSION` reads 1).
* Unchanged: `0xb14`'s write (trap 387), `sgx540_init_regs`, the microkernel
  register tables (hypothesis), `USE_CODE_BASE` existence, the DPM/TA/ISP/TSP/
  PBE/PDS state map.

## Superseded / uncertain

* **Report 277's "the microkernel now takes commands off the CCB"
  (`consumed 2..5`)** — withdrawn; trap 389.
* **Report 277's / HANDOFF-74's "one or two jobs pass per GPU reset, then it
  stalls"** — the pattern is real but its interpretation was wrong: every job is
  a timeout, and whether the fence signals is a race between two 2000 ms timers.
* **HANDOFF-74's record of the loaded firmware (`invfix`)** — the boot was on
  `ccb2`; trap 390.  Both builds start the microkernel; neither dispatches.

## Artefacts produced

* `docs/reports/278-sgx540-edm-never-redispatched-2026-11-26.md` — this
  session's report.
* `docs/captures/sgx540-ukernel-kick-counter-2026-11-26.txt` — the instrument,
  its negative control, the measurement and the timing mechanism.
* `docs/contradictions/sgx540-event-kick2-offset.md` (new) — raised **and
  resolved/retracted in this session**; kept as the record of a wrong reading
  that would otherwise be re-derived.
* `docs/captures/sgx540-timer-clock-2026-11-26.txt` — the reading that suggested
  the timer (the 1000× period, the control block, the `edm_init` bit).
* `docs/captures/sgx540-tick-counter-2026-11-26.txt` — the measurement: the
  dynamic counter, its emulator control, and the zeros on the unit.
* `rootfs/powervr-sgx/0004-sgx540-tick-counter.patch` — the dynamic instrument.
* `rootfs/powervr-sgx/0003-sgx540-kick-counter.patch` — the instrument plus the
  session-74 CCB-trace word relocation that had never been in 0002; applying
  0001→0003 to a pristine `95f78ca` reproduces both `ukernel.py` and the
  deployed firmware md5 `229b838c…` byte-for-byte.
* `local/work/powervr_sgx` (gitignored) — `ukernel/ukernel.py` carries
  `UK_KICKLOG`; the same tree with the flag unset builds `7056fc74…`.
* No kernel patch change, no region-0 flash, no module change.

## Note on reverting

The experiment was a **firmware file** swap, not a flash.  Reverted and proven:
hd3's `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` is `invfix` again (md5
`12f12abb297d8d0b8aebfb0b10467ee5`), with the tested builds kept inert as
`.kick1` (`229b838c…`) and `.tick1` (`c94d8d2f…`), and the pre-session copy as
`.before-kick1`.  Region 0 is
untouched at `0x02C1DDC2`.  In the source tree, `UK_KICKLOG` unset reproduces the
previous image byte-for-byte, so the code revert is one flag.  No restore is
required: the unit is on Linux by the operator's direction and the desktop's
software rendering is expected until the open driver draws.
