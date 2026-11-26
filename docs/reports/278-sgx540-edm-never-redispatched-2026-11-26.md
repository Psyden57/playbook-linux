# Report 278 — the SGX540 EDM is never re-dispatched, and the "passing" job was the driver's timeout

Date: 2026-11-26 (session 75).  Bench session on the reference unit
(`0x50175127`, OMAP4430 ES2.2).  Follows report 277, which took the open
`powervr_sgx` SGX540 microkernel from a universal `consumed 0` timeout to a NULL
job that printed `1/1 passed`.

**Headline: HANDOFF-74 §0b is answered, and the answer changes the question.**
The microkernel's event program is entered **once** (its init kick) and **never
again**: a counter at `kick` entry read 0 through an entire job window, six
consecutive jobs cost six GPU resets, and every "passing" NULL job took
**2033835 / 2016806 / 2020499 µs** — the driver's own `job_timeout_ms = 2000`.
A `1/1 passed` here is the reset signalling the fence, not a completion.

Evidence: `docs/captures/sgx540-ukernel-kick-counter-2026-11-26.txt` (this
session's only new capture); `rootfs/powervr-sgx/0003-sgx540-kick-counter.patch`
(the instrument).

## 1. Why the session-74 trace could not answer §0b

§0b asked for *one* counter the EDM advances per dispatch.  The session-74
kernel-CCB trace stored the WO/RO pair the microkernel read at the timer test and
at `kick` entry, and for a job that "passed" **all four words read 0**.  That is
consistent with two opposite worlds:

* the event never reached `kick` at all (the doorbell did not open), or
* `kick` ran, read the ring, found `WO == RO`, and returned.

A stored *state* cannot separate them; only a count of *entries* can.  So this
session's instrument counts entries, not state:

```
UK_KICKLOG=1  ->  control-block word 0x3d:  kick entries
                  control-block word 0x3e:  empty-CCB exits (kick_log_empty)
```

Both are free words — `0x3d`/`0x3e`/`0x3f` are `UK_FRAMEPROF`'s busy-tick words
and that flag is off in this build (trap 386's lesson: diff every candidate word
against `cores/<core>.py` **and** the diagnostic flags, not just one of them).
The increment is placed **after** the `WO`/`RO` read, deliberately: an
instrumentation store at the top of the event entry **faults** on this core (the
session-74 capture), so the counter must not add an admission path.

Host-readable through the existing `ctlblk` debugfs node (words 0x3c-0x3f are
its last line) — **no `/dev/mem`**, which trap 384 closed.

## 2. The measurement

One boot with the instrumented image (`open microkernel kick1`), sampling the
control block every 150 ms through the whole 2 s job window, and the driver's own
ring in the same call:

```
job 1, from a clean boot (resets 0):
  null job signalled in 2033835 us
  ok
1/1 passed
[drm] *ERROR* job timed out (ccb 1, consumed 0, done word 0x0 for 0x0, events 0xa0000000/0x0); resetting
[drm] microkernel started again

every sample, 12/12 over the window:
  0x38-0x3f = 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000

during: ccb wo 1 ro 0 issued 9 consumed 8   (the kernel ring holds the command)
        ctlblk 0x38-0x3f: all zero         (the microkernel's view: WO 0 / RO 0)
```

Six jobs back to back, no reboot:

```
run 1: 0/1        run 2: 1/1 (2016806 us)   run 3: 0/1
run 4: 1/1 (2020499 us)   run 5: 0/1        run 6: 0/1
resets: 2 -> 8      ctlblk 0x38-0x3f: all zero
```

So: **`kick` never ran once**, the doorbell never opened, and every job was the
timeout.  This is outcome 3 of §0b's four:

> *"The counter never advances, yet job 1 passes → the pass does not come from
> the microkernel's completion path at all.  Then the driver's own accounting is
> the thing to trust."*

**The instrument is not dead by construction.**  Driving the same image through
the offline emulator (`init` + three bare doorbell events with an empty kernel
CCB) gives `0x3d = 3`, `0x3e = 3`.  The counters move offline and stay 0 on
silicon, which is a measurement.

## 3. What the "pass" actually is (from the kernel sources)

`drm/tests/powervr_sgx_test.c`'s `submit-null-job` waits on a syncobj with
`syncobj_wait(fd, s, 2000000000ll)` and asserts it returns 0.  Chain:

1. The signalled syncobj is bound to **`drm_sched`'s** `s_fence->finished`:
   `drm_syncobj_replace_fence(sig->syncobj, done)` with
   `done = dma_fence_get(&job->base.s_fence->finished)` (`pvrsgx_job.c`) — not to
   `job->hw_fence`.
2. At 2000 ms `pvrsgx_timedout_job()` runs: it prints the timeout line, sets
   `-ETIME` on `job->hw_fence` and on every job's `hw_fence`, signals them,
   stops the scheduler, restarts the microkernel, and calls
   `drm_sched_start(&sdev->sched, 0)`.
3. `drm_sched_start(sched, 0)` re-adds a callback on each job's **parent** fence
   and finishes it with `drm_sched_job_done(s_job, fence->error ?: errno)` where
   `errno` is **0**; `drm_sched_job_done_cb` then passes `f->error` of the
   *parent*, also 0.  So `s_fence->finished` is signalled **without an error**.
4. `drm_syncobj_array_wait_timeout()` returns 0 as soon as
   `dma_fence_is_signaled()`; it never inspects the fence's error.

Both timeouts are 2000 ms, so the race is invisible in the output.  The two
`1/1 passed` runs landed at 2016806 and 2020499 µs — the timeout, to the
millisecond — and the context was then marked `GUILTY`, which is why the very
next run on it reads `0/1`.

**Consequence:** on this stack the pass/fail column of the uAPI suite is not
evidence about the microkernel.  What is evidence is `resets` in
`/sys/kernel/debug/dri/1/status` (it rises once per job) and the new `kick`
counters (they never move).

## 4. Two corrections to the session-74 record

* **`consumed 2..5` measured nothing.**  `pvrsgx_fw_start()` calls
  `pvrsgx_ccb_reset()`, which sets `ccb WO := 0`, `RO := 0` and
  `ccb_reset_seq := ccb_issued`.  `pvrsgx_ccb_consumed()` returns
  `ccb_issued - min(pending, issued - reset_seq)`, so with an empty ring it is
  exactly `ccb_issued`.  The idle boot of this session read
  `ccb wo 0 ro 0 issued 1 consumed 1`.  Report 277's "the microkernel now takes
  commands off the CCB" is **withdrawn**: the number is a ring-reset artefact.
  The honest reading of the ring is `wo`/`ro` (and the microkernel's own
  `UK_CCBLOG` words 0x39/0x3a), which showed `1/0` on the kernel side and `0/0`
  on the microkernel side for the entire window.
* **The boot ran `ccb2`, not `invfix`.**  The session-74 handoff records the
  durable firmware as `invfix` (md5 `12f12abb…`), and that is what was on hd3 at
  the session-75 start — but `dmesg` showed `open microkernel ccb2`, the
  session-74 `UK_CCBLOG` trace build (`7056fc74…`), because the file had been
  swapped after that boot's module load.  The firmware is read only at module
  load, so the swap was inert.  Both builds start the microkernel and neither
  dispatches.

## 5. Also measured, and relevant to the next step

Twelve seconds after a `microkernel started again`, with nothing submitted:

```
firmware running: 1   lost: 0   uk-lockups 0
EVENT_HOST_ENABLE  0x130  0x20004000   (SW_EVENT | TIMER)
EVENT_STATUS       0x12c  0xa0000000   (identical to the value EDM init printed)
EVENT_STATUS2      0x118  0x00000000
```

`EVENT_STATUS` does not change, nothing is acked, and no tick occurs — consistent
with §2: the event program is never re-entered, so it never clears or services
anything.  The timer source is enabled (the session-74 `SINGLE_CONTEXT` init
ORs `EVENT_HOST_ENABLE_TIMER_MASK` in) and the driver programs
`EVENT_PDS_ENABLE = 0x2204f00f`, so the *mask* side of the dispatch is in place;
what is unproven is whether the **doorbell** reaches the core.

## 6. What this does not prove, and the next unit of work

* **Why the doorbell does not open is not measured.**  `ccb_write()` writes
  `EUR_CR_EVENT_KICK` (0xac8, `NOW` b0) and then polls the ring; whether that
  write reaches the core is now the question.  It needs a register read **from
  inside the driver** (trap 384) — a debugfs line of
  `EVENT_KICK`/`EVENT_KICKER`/`EVENT_PDS_ENABLE` read back around the doorbell
  is the cheapest form, and the driver already has all three in its `regs`
  table.
* **Two cheaper explanations were checked statically and both are clean** (the
  session's last work, after this report's measurements):
  * The init kick going to `EVENT_KICK2` (0x0ac0) while jobs use `EVENT_KICK`
    (0x0ac8) is the DDK kernel driver's own `SGX_FEATURE_MULTI_EVENT_KICK`
    behaviour, and `pvrsgx_regs.h`'s `EVENT_KICK2 = 0x0ae8` is an unused
    SGX545-family constant.  **An earlier draft of this report called it a
    sufficient cause; that is withdrawn.**  [trap 392;
    `docs/contradictions/sgx540-event-kick2-offset.md`, resolved]
  * `event_pds_addr()` has the vendor's shape (`addr − pds_pixel.base` when
    `EDM_VERTEX_PDSADDR_FULL_RANGE` is clear — which `srvinit` also gates on),
    evaluates to `0x804090` (8.02 MiB) inside `EUR_CR_EVENT_OTHER_PDS_EXEC`'s
    **22-bit / 64 MiB** field (b4-25, `MASK 0x03FFFFF0`), and the driver's
    `check_event_pds()` read-back passes.  That
    gate cannot detect "the PDS program is in the wrong heap", because both
    sides of the comparison come from our own layout (trap 393) — but the
    derivation itself agrees with the vendor's.  [fact sheet, "Events and kicks"]
* **`EVENT_PDS_ENABLE`'s value is not read back.**  The driver programs
  `0x2204f00f`; the DDK's own derivation says the same; nothing has read the
  register.  One read answers whether the write stuck.
* **`0xb14`'s write is still unmeasured** (trap 387) and the microkernel's
  register tables (0x618, 0x62c, 0x694, TA 0x200, 3D 0x428) remain carried-over
  hypothesis — untouched by this session, and correctly so: they sit downstream
  of a dispatch that never happens.

No render, no geometry, no pixels.  The desktop stays on `swrast`.

## 7. Bench at the close

Region 0 **`0x02C1DDC2`** (no flash this session — the firmware swap is a file on
hd3; reverted to `invfix`, md5 `12f12abb…`, proven by `md5sum`), region 1
`20a5edd3…`, hd3 the Nura ext4 rw 56.5 G; `BOOTCTR_FCTRY` **`0x1e`**; battery
Full 4.151 V; `nproc` 2, PID 1 affinity `0,1`; `wlan0` `192.168.0.231`;
`card1` = `powervr_sgx`, `card0` = `omapdrm`; `firmware running: 1`,
`uk-lockups 0`, `BIF_FAULT 0`, `resets 9`.

## 8. Added after this report's own measurements: the timer never fires, and the
instrument that showed it

Two follow-ups were run with one more boot.  Evidence:
`docs/captures/sgx540-tick-counter-2026-11-26.txt` and the reading that suggested
it, `docs/captures/sgx540-timer-clock-2026-11-26.txt`.

* **The EDM timer does not run at all.**  §2's instrument read `ctlblk`, which is
  *static memory* — a 1 kHz event can never move a word there, so its zeros said
  nothing about the timer (trap 394).  `UK_TICKLOG` instead increments a 32-bit
  value in the microkernel's own memory, split across free control-block words
  0x2c/0x2d, and the host compares two reads: **0 after twelve seconds idle**,
  with `resets 0` and IRQ 147 never having asserted.  So nothing re-enters the
  event program after the init task — the same statement §2 reached from the
  `kick` counters, now measured from the dispatch side too.
* **RETRACTED, one boot later: the timer period is correct, and the timer runs.**
  This section first reported the driver's `HOSTCTL_UK_TIMER_CLOCK` = 307 200 as
  "1000× the DDK's 307".  That compared a **count** against a **rate**:
  307 200 counts / 307.2 MHz = **1.000 ms**, exactly the 1 kHz requested, and the
  DDK's expression is the same quotient.  `pvrsgx_dump.c` then gained the seven
  dispatch registers, and `EUR_CR_EVENT_TIMER` reads **`0x010228fe`** with
  `ENABLE` set, cycling inside its period on every sample.  See §9.
* **The init task does run**, on three independent proofs — `SC_VERSION` (0x18 =
  `0x00010000`, a microkernel-only store), `EVENT_HOST_ENABLE` b29 set
  (`0x20004000`, ORed in by `init_set` after the timer write), `BIF_FAULT 0`.
  Necessary to say because with `edm_init=1` the **host** writes
  `ui32InitStatus` itself, so the `microkernel started` log line is not the
  microkernel's report (trap 396); §1's claim that it "is live" rested on that
  line and on `firmware running: 1`, which is the same bit.

## 9. Third boot: the timer is alive, and one PDS bit does not stick

`pvrsgx_dump.c`'s register table gained seven read-only entries —
`EVENT_TIMER` (0x0acc), `EVENT_KICKER` (0xac4), `EVENT_KICK` (0xac8),
`EVENT_PDS_ENABLE/DATA/INFO` (0x0a58/0x0a6c/0x0a70) and `EVENT_PDS_EXEC`
(0x0a68) — every one a register the driver already writes or already reads back,
so no unnamed-read hazard (trap 369).  Verified the tree is the installed module
plus exactly this change: reverted, the build reproduces `2e138afc…`.

Read on the unit, idle:

```
EVENT_STATUS      0x012c 0xa0000000     b29 TIMER latched, and it stays latched
EVENT_HOST_ENABLE 0x0130 0x20004000     TIMER | SW_EVENT
EVENT_TIMER       0x0acc 0x010228fe     ENABLE b24, VALUE cycling 0..0x04B000
EVENT_KICKER      0x0ac4 0x0f002010     the driver's own kicker word
EVENT_KICK        0x0ac8 0x00000000     self-clearing; reading it proves nothing
EVENT_PDS_ENABLE  0x0a58 0x2204b00f     *** the driver writes 0x2204f00f ***
EVENT_PDS_EXEC    0x0a68 0x00804090     as written
EVENT_PDS_DATA    0x0a6c 0x00000003     as written
EVENT_PDS_INFO    0x0a70 0x00013d01     as written
```

So of the whole dispatch configuration, **exactly one field is not what the
driver wrote**: `EVENT_PDS_ENABLE`'s bit 14, `SW_EVENT` — a difference of
`0x4000`, stable across reads and not self-clearing.  That is the bit session 74
added as "the decisive fix", and it is the microkernel→host signal; if this core
will not accept it as a dispatch source, no host interrupt can be raised, which
is consistent with IRQ 147 never firing and with the latched TIMER event never
being acknowledged.

The next test is narrow and one boot: a per-bit write/read-back of **0x0a58**
from inside the driver, to separate "not implemented" from "store masked".  Note
that "the event program is not entered" and "the bit does not stick" are not yet
proven to be the same fact — they are two measurements that happen to agree.

## 10. Fourth boot: bit 14 is absent, and session 74's PDS correction was a no-op

A debugfs node (`disp_enable`) whose `.show` **writes** 0x0a58 — the full word,
then each bit of it alone, reading back between each, then restores.  Safe by
construction (no address, no side effect beyond which sources dispatch), and the
restore is read back.

```
  full     0x2204f00f -> 0x2204b00f DIFFERS
  bit 14   0x00004000 -> 0x00000000 ** does NOT stick **
  (every other bit of the word -> sticks)
bits of 0x2204f00f that stick alone: 0x2204b00f
```

**Bit 14 fails when written alone, so it is absent, not masked.**  Two
consequences, the second of them uncomfortable:

1. `EU_CR_EVENT_PDS_ENABLE_SW_EVENT_MASK` is in `sgx540defs.h`, but this silicon
   does not implement it *in that register*.  It **is** implemented in
   `EVENT_HOST_ENABLE` (0x130 reads `0x20004000`), so the field is real and the
   header is not simply wrong — it is mis-attributed by register, which is a new
   kind of error for the fact sheet (its first **C** fact contradicting a **K**).
2. **Session 74's `SGX540_CR_EVENT_PDS_ENABLE_ALL 0x2204f00f` had no effect.**
   §3 of report 277 lists it as one of the four corrections, and it writes a bit
   the hardware ignores, so it is functionally identical to the SGX545 constant it
   replaced.  The other three corrections stand.  And the change was already
   unattributable: §3/§4 of *this* report show the `1/1 passed` it was credited
   with is the driver's timeout (trap 388) and the `consumed 0 → 2..5` is a
   ring-reset artefact (trap 389), so nothing measured in session 74 improved.

The dispatch picture is now: a PDS source (TIMER) that is enabled and fires, an
event (`EVENT_STATUS` b29) that latches and is never cleared, a host interrupt
(IRQ 147) that has never fired, and an event program that is never entered.  The
unexamined step between "a source fires" and "the program is entered" is the PDS
exec address itself — `EVENT_PDS_EXEC` = `0x00804090`, which reads back as written
but has never been shown to point at code this core executes.
