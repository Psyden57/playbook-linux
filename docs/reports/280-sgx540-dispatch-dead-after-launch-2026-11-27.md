# Report 280 — the kick entry is never entered, and no dispatch source works after the launch

Date: 2026-11-27 (session 77, Linux-era 71).  Bench session on the reference
unit (`0x50175127`, OMAP4430 ES2.2).  Follows report 279, which corrected the
entry counter and answered the USSE-emit question negatively; this session ran
HANDOFF-77 §0b.

**Headline: the kick path never reaches the microkernel — and neither does
anything else.**  A re-instrumented counter on the `kick` entry's first
instruction, on a mechanically verified control-block word, reads **0 across
eight job submits that each cost a GPU reset**.  Combined with two more
measurements, the conclusion is broader than §0b asked: the EDM fetched the
event program **twice during launch** and **never again** — not for a job kick,
not for the 1 kHz timer, and (per the session-76 wrap-up's `relaunch` node) not
for a re-issued launch kick either.  Every `EVENT_PDS_ENABLE`-masked source is
dead; only the launch path (`EVENT_KICK2` at boot) ever dispatched.

Evidence: `docs/captures/sgx540-kick-entry-counter-2026-11-27.txt` (three
boots, in order); the firmware `kick2` (`12847879…`) and the module `ea10d582…`
are deployed.

## 1. The instrument, and the mechanical check that preceded it

`UK_KICKLOG` was re-instrumented per HANDOFF §0b: the session-75 counters
(words 0x3d/0x3e, placed after the WO/RO read) read 0 in every build, so the
count now sits on the **first instruction of the `kick` entry** — it counts the
entry itself — on control-block word **0x3f**.

The free-word verification was done mechanically before any store was written
(trap 403's check, redone for the current tree): no constant in
`cores/sgx540.py`, no other `r17`-relative access, and no `st_sa(SA_SELF)` site
touches 0x3d/0x3e/0x3f with `UK_REARM` and `UK_FRAMEPROF` off; `st.a.d r17,
#63` assembles directly, so no rebase; and the emitted image was
**disassembled and read back** — the kick entry's first instruction is the
counter (`st.a.d r17, #63`), and the tick site is `st.a.d r17, #44` = word 0x2c
as intended.  Gates: `test_emu.py` 8/8 both cores, SGX545 golden byte-identical
(`83b5b0a7…`), patches exactly {ctlblk, pdtable}.

Every path into `kick` is a same-task branch, so `r17` (patched at the event
top) is live there — the same shape as `UK_ENTRYLOG`, whose word 0x07 provably
works and reads **2** in the same dumps, which is the negative control that
makes the zero on 0x3f sound (DECISIONS §113).

## 2. Boot 1 — the measurement

Firmware `kick2` booted cleanly (the new store does not break the launch), then
the uAPI suite's job set:

    BEFORE:  entry(0x07)=2  SC_VERSION(0x18)=0x10000  tick(0x2c)=0
             kick(0x3f)=0   empty(0x3e)=0    word 0x2a = 1
    AFTER 8 job submits:
             entry=2 (UNCHANGED)  tick=0  kick(0x3f)=0  empty=0  0x2a=1
    driver:  resets 0 -> 8   issued 8   irqs 0;  suite 24/50 (trap-388
             timeouts; `consumed == issued` is the ring-reset artefact)

**HANDOFF §0b's first outcome: the core resets before the event program is ever
fetched for a kick.**  The kick entry is never entered.  The fault is upstream
of the event program — or, as boots 2–3 showed, the whole dispatch path is.

## 3. Boot 2 — the doorbell read from inside `ccb_write`, and its control

§0b's prescribed follow-up.  `ccb_write()` now samples `EVENT_KICK` (0xac8)
once before and 8× back-to-back after the doorbell write, with `EVENT_KICKER`
and `EVENT_STATUS` in the window; `pvrsgx_fw_start()` samples the same register
around the **launch** kick — which provably lands — as the control (new
`doorbell` debugfs node; module `801d08a5…`).

    job kicks (8):   before 0x00000000; 8 samples all 0; KICKER 0x0f002010
    launch (control): before 0x00000000; 8 samples ALL 0; KICKER 0x0f002010

**The control is decisive: even the landing launch kick reads `EVENT_KICK` as 0
in every immediate sample.**  All-zeros is what a landing doorbell looks like
on this silicon, so `EVENT_KICK` read-backs cannot show landing here.  The
window does establish that the kicker address register is correctly programmed
and the writes went out.  (Trap 407.)

## 4. Boot 3 — the complete dispatch-register dump, and the sharper conclusion

Module `ea10d582…` adds the never-dumped dispatch registers to `status`.  Fresh
idle boot, nothing submitted:

| register | value | note |
|---|---|---|
| `EVENT_STATUS` 0x12c | `0x20000000` | b29 TIMER **latched since boot** |
| `EVENT_PDS_ENABLE` 0x0a58 | `0x2204b00f` | TIMER set |
| `EVENT_PDS_EXEC/DATA/INFO` 0x0a68+ | `0x008039f0` / 1 / `0x00013d01` | the OTHER pair, programmed |
| `DMS_AGE` 0x0a78 | **`0x00000000`** | the vendor's launch writes `ENABLE \| MAX_DMS_AGE` |
| `EVENT_PDS_ENABLE2` 0x0a50 | 0 | the vendor never writes it either |
| `EVENT_PIXEL_PDS_EXEC/DATA/INFO` 0x0a5c+ | **0/0/0** | the PIXEL pair entirely unprogrammed |
| `EVENT_KICK1/KICK3` 0x0ab0/0x0ad8 | 0/0 | |

And the ctlblk baseline is identical on a **third** boot: entry = 2, tick = 0,
kick = 0, word 0x2a = 1 (reproduced — a deterministic early write).

Three independent measurements now say one thing:

1. **The kick entry never runs** (boot 1).  A bare doorbell leaves no
   `EVENT_STATUS` bit, so a landed-and-fetched kick would fall through to
   `ba kick` and count.  It does not.
2. **The timer does not dispatch either.**  A dispatching timer pass would ack
   b29 and increment the tick counter at 1 kHz; b29 is latched, entry static at
   2, tick 0.  **Session 76's "one init pass and one dispatch pass, and the
   dispatch pass is the timer's" is wrong in its second half — there is no
   dispatch pass.**  The `tick = 1` reading that correction was built on was
   very likely the adjacent word 0x2a misparsed (trap 406).
3. **Re-issuing `EVENT_KICK2` does not re-fetch** (session-76 wrap-up,
   valid instrument).

So: the EDM fetched the event program **twice during launch** (the launch
sequence evidently runs two of our five PDS stubs, all of which DOUTU to entry
0x0, cf. the vendor's PRIMARY1/SECONDARY/PRIMARY2) and **never again for any
trigger**.  This is the "task admission" class session 76 suspected, and it is
upstream of the doorbell question §0b started with: the doorbell may well
land; nothing dispatches regardless.

## 5. What differs between the launch and everything else (next candidates)

The launch script registers this driver never writes, now partially measured:
`DMS_AGE` = 0 (vendor: `ENABLE | PVRSRV_SGX_MAX_DMS_AGE`; the value remains
underivable from the DDK 1.8 drop), and still unmeasured `EUR_CR_PDS_DMS_CTRL2`,
`USE_G0/G1`, `USE_CTRL`, `USE_MOE`, and whether `USE_CODE_BASE` is written.
The natural first write experiment is `DMS_AGE = ENABLE | <age>`: if the DMS
holds the launch task(s) as never-completed and aging is disabled, every later
dispatch would be blocked — exactly the observed shape.  The zero PIXEL pair is
the second candidate (a PIXEL-routed dispatch would fetch from offset 0).

## 6. Two corrections this session made to its own instruments

* **The `UK_ENTRYLOG` STATUS-snapshot store has never worked** (present in
  fix1 too): it intends word 0x39 → byte 0xe4 (its comment says so) but runs
  `limm r5, #0x39` — **bytes, not the ×4** (`imae` has no scaling) — so the
  store lands misaligned at byte 0x39 and word 0x39 has read 0 in every dump
  of every build.  Trap 406; fix the limm to `#{ENTRY_STATUS * 4}` before
  citing word 0x39.
* **"The dispatch pass is the timer's" is withdrawn** (§4.2); every
  `tick(0x2c) = 1` figure from sessions 75–76 is a misparse candidate until
  re-measured on a dump parsed by word index.  The boot counter reading and
  the register values stand.

## 7. Bench at the close

Region 0 `0x02C1DDC2`, region 1 `20a5edd3…` — **no media writes this
session**.  hd3 = the session-71 rebake, live root `/dev/loop0` ext4 rw.
Firmware live = `kick2` (`12847879…`) with `.fix1w` (`f468a71d…`) preserved;
module = `ea10d582…` with `.pre-doorbell` (`94eaea61…`) and `.predump`
(`801d08a5…`) preserved.  `BOOTCTR_FCTRY` **`0x23`** read fresh on boot 1;
boots 2–3 cost one unit each (0x22/0x21 expected, not yet re-read).
`wlan0` DOWN after the GPU resets (the session-76 caution); `usb0` up.
