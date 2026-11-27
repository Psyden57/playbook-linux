# Session 77 (absolute) — the kick entry is never entered, and no dispatch source works after the launch

> Numbering: absolute `77` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era session 71**
> (`absolute = Linux-era + 6`).  Continues session 76, which wired the SGX's
> interrupt to the CPU, corrected the entry counter, and answered the USSE-emit
> question negatively.

Date: 2026-11-27.  Unit at the end: **Linux `7.3.3`** on hd3 (root
`/dev/loop0` ext4 rw), region 0 **`0x02C1DDC2`**, region 1 **`20a5edd3…`**,
`BOOTCTR_FCTRY` **`0x23`** read fresh on boot 1 (boots 2–3 cost one unit each
by arithmetic — **not re-read**), `card1`/`renderD129` = `powervr_sgx` running
the open microkernel **`kick2`**, panel `bl_power 0` / 0 DSI errors, `wlan0`
**down** (the GPU resets), `usb0` up.  Full bench record: STATE.md, "Current
state — 2026-11-27 (session 77)".

## Goal of this session

HANDOFF-77 §0b, and nothing else at first: does the **kick** path reach the
microkernel at all?  Re-instrument the `kick` entry with a counter on a
control-block word **proven free** (the check that failed invisibly via 0x1c in
session 76), read ctlblk before and after submitting a job, interpret against
the four outcomes.  The operator's direction at the start: reboot before
testing anything, and verify what image/driver/firmware is actually on the
unit — both proved right (the previous boot's `open microkernel` line had
wrapped out of dmesg; the reboot also made the boot counter readable).

## Confirmed (measured, read in a file, or seen on the bench)

**Boot 1 — the kick measurement (firmware `kick2`, `12847879…`).**

* **The kick entry is never entered.**  The counter sits on the **first
  instruction of the `kick` entry**, control-block word **0x3f** — mechanically
  verified free before any store was written (no `cores/sgx540.py` constant,
  no other `r17`-relative access, no `st_sa(SA_SELF)` site with `UK_REARM`/
  `UK_FRAMEPROF` off; `st.a.d r17, #63` assembles directly, so no rebase), and
  **verified by disassembling the emitted image** (`st.a.d r17, #63` at 0x440;
  the tick site reads `st.a.d r17, #44` = word 0x2c).  Every path into `kick`
  is a same-task branch, so `r17` is live there.
  Measured: `entry(0x07)=2, SC_VERSION(0x18)=0x10000, tick(0x2c)=0,
  kick(0x3f)=0, empty(0x3e)=0, word 0x2a=1` — **unchanged across eight job
  submits** (`resets 0→8`, `issued 8`, `irqs 0`, suite 24/50 = the trap-388
  timeouts), stable over 20 s.  The negative control rides in the same dump:
  the entry counter (same store form, same mechanism) reads 2, so the zero on
  0x3f is sound (DECISIONS §113).  *Artefact:* capture
  `sgx540-kick-entry-counter-2026-11-27.txt`; firmware `kick2` deployed.
* **BOOTCTR_FCTRY = 0x23, read fresh** (`pbrpmb: topup: … = 23000000`) — the
  reading the previous boot's wrapped dmesg could not give.

**Boot 2 — the doorbell read from inside `ccb_write` (module `801d08a5…`).**

* **`EVENT_KICK` read-backs cannot show landing on this silicon.**  `ccb_write()`
  now samples the register before and 8× back-to-back after the doorbell
  write (new `doorbell` debugfs node); `pvrsgx_fw_start()` samples the same
  register around the **launch** kick — which provably lands — as the control.
  **Both read all-zero**; `EVENT_KICKER` = `0x0f002010` (correct) in both
  windows.  So an all-zero read-back is what a landing doorbell looks like,
  and the job kicks' zeros are indistinguishable from it (trap 407).  What
  stands: 8 writes went out, the kicker address register is right.

**Boot 3 — the complete dispatch-register dump (module `ea10d582…`).**

* **The never-written dispatch registers, measured:** `DMS_AGE` (0x0a78) =
  **0** — the vendor's launch writes `ENABLE | PVRSRV_SGX_MAX_DMS_AGE`; the
  **PIXEL event-PDS pair entirely 0** (`EVENT_PIXEL_PDS_EXEC/DATA/INFO` =
  `0x0a5c/0x0a60/0x0a64`); `EVENT_PDS_ENABLE2` (0x0a50) = 0 (the vendor never
  writes it either); `EVENT_KICK1/KICK3` = 0.  The OTHER pair is programmed
  (`0x008039f0`/1/`0x00013d01`) and `EVENT_PDS_ENABLE` carries TIMER.
* **The sharper conclusion: the dispatch path is dead after the launch, for
  every trigger.**  Three independent measurements: (1) the kick counter;
  (2) b29 TIMER latched since boot with `EVENT_TIMER` cycling while the entry
  stays 2 and the tick stays 0 — a dispatching timer pass would ack b29 and
  climb at 1 kHz; (3) the session-76 wrap-up's `relaunch` node — re-writing
  `EVENT_KICK2` itself does not re-fetch.  So the EDM fetched the event
  program **twice during launch** and **never again for any trigger**.  This
  is the "task admission" class session 76 suspected, upstream of the
  doorbell.  *Artefact:* report 280, traps 406–408.
* **`word 0x2a = 1` reproduced on three boots.**  Writer unidentified:
  nothing in the image stores there (the full `st.a.d` inventory of the
  disassembled image has no r17 store at #42 and no rebased store targeting
  r17+0xa8), and the driver memsets the block and writes only words 0x00–0x17.
  Candidate (unverified): the PDS0 launch stub's DOUTU deposit (our loader
  does no per-word DOUTU address patching).

## Inferred (reasoning from evidence, not directly measured)

* **The launch sequence fetches the event program twice** (entry = 2): the
  driver fills all five PDS stubs with program 0's offset when the manifest
  row is all-zero, and the vendor's launch runs PRIMARY1/SECONDARY/PRIMARY2.
  Consistent with every measurement; not directly observed.
* **A stuck, never-completed launch task with DMS aging disabled would block
  every later dispatch** — exactly the observed shape, which is why
  `DMS_AGE = ENABLE | <age>` is HANDOFF-78 §0b's one experiment.  A
  hypothesis until the write is tried.
* The DOUTU-deposit explanation of word 0x2a (above).

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The dispatch pass is the timer's" (session 76's corrected reading) is
  withdrawn — there is no dispatch pass.**  The `tick(0x2c) = 1` readings of
  sessions 75–76 are misparse candidates: control-block **word 0x2a reads 1**
  (three boots running), 0x2a is the **3rd word of dump row 6** while 0x2c is
  the **5th**, and a word-index parse reads tick = 0 on every session-77 boot.
  The entry counts and register values stand; the tick figures do not.
* **`UK_ENTRYLOG`'s STATUS-snapshot store has never worked** — in `fix1` too.
  It runs `limm r5, #0x39` — **bytes** — where its own comment says
  "word 0x39 → 0xe4" (`imae rD,rA,#imm,rB` is `rA*imm + rB` with **no**
  scaling, cf. `imae r6, r3, #32, r6` for the CCB command address), so the
  store lands misaligned at byte 0x39.  Word 0x39 has read 0 in every dump of
  every build.  Found by disassembling the emitted image, one minute of
  reading — after two sessions of trusting the numeral.  Fix the limm to
  `#{ENTRY_STATUS * 4}` before citing word 0x39.  (Trap 406.)
* **"The doorbell read-back will show whether the write lands."**  Killed by
  the landing control: even the provably-landing launch kick reads all-zero
  (trap 407).  The control was built in from the start — which is what turned
  a potentially wrong conclusion into an instrument limitation.
* **"The kick counter should go on words 0x3d/0x3e again."**  No: the handoff
  said they cannot be trusted as they stood, and the re-instrumentation (fresh
  word 0x3f, first instruction, disassembly-verified) is what made the zero
  interpretable.
* (Carried from the operator's opening note: the running image/driver/firmware
  could not be verified mid-boot — the `open microkernel` line had wrapped out
  of dmesg — so the session rebooted first and measured on boots whose dmesg
  proves the build.)

## Unknown / open questions

* **Who writes ctlblk word 0x2a (= 1)?**  Reproduced three boots; candidate
  the PDS0 stub's DOUTU deposit.  *Settle by:* disassembling the stub's DOUTU
  words against the vendor's patched forms, or a test deposit to a known word.
* **Does the launch's second fetch exist, and what is it?**  (Primary1 +
  secondary, or a raced init re-run?)  *Settle by:* the DMS_AGE experiment's
  effect on the entry counter, or an entry-counter at the init entry.
* **Does `DMS_AGE` unstick the dispatch?**  HANDOFF-78 §0b — one boot.
* Then the remaining unwritten launch registers: `EUR_CR_PDS_DMS_CTRL2`,
  `USE_G0/G1`, `USE_CTRL`, `USE_MOE`, and whether `USE_CODE_BASE` is written.
* Unchanged and still open: `0xb14`'s write (trap 387), `sgx540_init_regs`'s
  remaining values, the microkernel register tables (0x618/0x62c/0x694/TA
  0x200/3D 0x428, hypothesis), `USE_CODE_BASE` existence, the
  DPM/TA/ISP/TSP/PBE/PDS state map, `tick2`'s reproducibility (uncommitted
  ukernel work), and the DECISIONS §114 catacomb fix.

## Superseded / uncertain

* **Session 76's "one init pass and one dispatch pass, and the dispatch pass
  is the timer's"** — withdrawn in its second half (no dispatch pass); its
  `tick = 1` figure is a misparse candidate (trap 406).  The `entry = 2`
  figure stands.
* **Session 75–76's `tick(0x2c) = 1` readings** — re-measure before citing.
* **"The doorbell may not land" as a live question** — replaced by "the
  dispatch is dead after the launch"; the doorbell question is moot until the
  dispatch works (and unmeasurable by read-back, trap 407).

## Artefacts produced

* **Capture:** `docs/captures/sgx540-kick-entry-counter-2026-11-27.txt` — the
  three boots in order, the mechanical word verification, the doorbell control,
  the dispatch-register dump, and the next-candidate list.
* **Report 280.** Traps 406–408; DECISIONS §116.
* **Microkernel change** (`ukernel/ukernel.py`): `UK_KICKLOG` re-instrumented —
  counter moved to the `kick` entry's first instruction, word 0x3d → **0x3f**.
  Firmware **`kick2`** (`12847879…`) built with explicit output paths, both
  gates green (`test_emu.py` 8/8 both cores; SGX545 golden `83b5b0a7…`),
  wrapped (`mkukfw --manifest`) and deployed.  `.fix1w` (`f468a71d…`) preserved
  on the unit beside it.
* **Driver changes** (`local/work/powervr_sgx`, mirrored into
  `local/kernel-tree-sgx` to build): the `doorbell` debugfs node and the
  `ccb_write()`/launch-kick doorbell sampling (`pvrsgx_ccb.c`, `pvrsgx_fw.c`,
  `pvrsgx_device.h`); six new `status` registers (`pvrsgx_dump.c`:
  `EVENT_PDS_ENABLE2`, the PIXEL pair, `EVENT_KICK1/3`).  Module **`ea10d582…`**
  deployed; `.predump` (`801d08a5…`) and `.pre-doorbell` (`94eaea61…`)
  preserved.  **The session-77 module/firmware changes are NOT yet exported to
  `rootfs/powervr-sgx/` patch files** — owed before any upstream move.
* **Three boots, no media writes.** Region 0 untouched at `0x02C1DDC2`.

## Caution for the next session

* **`BOOTCTR_FCTRY` is `0x21` by arithmetic** (0x23 read on boot 1, two module
  reboots since) — **read it on the next boot** and quote the byte string; the
  Linux `/init` tops up below `0x1e`, so this is a health check, not a budget.
* **GPU resets take `wlan0` down** (reproduced); bring it back up or note it.
* **Reboot before measuring anything** if the boot's `open microkernel` line
  has wrapped out of dmesg (trap 390; the operator's opening instruction).

## Note on reverting

* Firmware: `cp /lib/firmware/powervr_sgx/sgx540_ukernel.bin.fix1w
  /lib/firmware/powervr_sgx/sgx540_ukernel.bin` + `reboot -f` restores the
  session-76 wrap-up state.
* Module: `cp …powervr_sgx.ko.pre-doorbell …powervr_sgx.ko` (=`94eaea61…`) +
  `reboot -f`.
* Both device files are instrumented test builds; the durable pair
  (`.tick2` firmware, `.before-hostirq` module) is one `cp` away.  No restore
  is required — the unit is on Linux by the operator's direction.
