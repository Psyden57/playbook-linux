# The RPMB key: secure-world/KEK-derived vs a 32-byte literal in `rpmb-omap4`

**Status: resolved (session 18); the key was pinned in session 19.**  The key is
**not** a secure-world-only secret.  For every authenticated transaction `rpmb-omap4` hands `smc #1` a
descriptor with a **pointer to the 32-byte key in normal-world, non-cached
memory** (`0x2774`–`0x2790`), so the monitor is a crypto engine with a
caller-supplied key, not its custodian.  The key is a monitor-computed function
`f(seed)` of a **seed that ships in the binary** (the 32-byte literal
`"retreating is advancing in the o"` at file `0x71b4`) or, on the KEK path, of
the eMMC **CID**; the derived 32-byte frame key is written to `page+0x460`, also
normal-world memory.  **Claim A is refuted in substance** (a normal-world holder
can compute a valid MAC); its narrow reading — the static `hd0` image carries no
key — stands.  **Claim B is confirmed as the code path** (both KEK queries fail
and the literal is copied), with one correction: the literal is the **seed**, not
the frame key.  The remaining unknown at that point was `f`, settled by the live
page read in session 19 (report 143); the write still needs the monitor's block
seal.  Full evidence: **report 141** §3.4/§3.5/§6.4
(`docs/reports/141-linux-rpmb-topup-feasibility-2026-10-30.md`).

The original claim text is kept below as history.

> **Session 19 update (report 143).**  The key question is closed: the live
> `rpmb-omap4` page gives the seed (CID-derived, with the literal's tail) at
> `page+0x440` and the **32-byte frame key** at `page+0x460`
> (`dfeec0ce…2274`), and HMAC-SHA256 of it over the captured 284-byte input
> equals the device MAC — so the seed is the CID path, not the literal.  The
> block HMAC key is `seed[0:16]`.  This **confirms the resolution above** and
> the "normal-world key" reading.  It does **not** make the Linux top-up
> possible by itself: the 224-byte record block is **encrypted on media by the
> monitor** (report 144), so the write needs the monitor's seal.  The caller is
> now solved — `0xFF05` was a page-validation error, not a refusal (report 147) —
> and the monitor serves the whole ABI, but the `0x2F` transform still does not
> reproduce the store (reports 148/156).  The consequence "Linux can do the
> `BOOTCTR_FCTRY` top-up" is therefore **conditional on the transform**, not on
> the key.

**Related files:** this is RPMB, not the `BOOTCTR_FCTRY` *value*
(`71-bootctr-stage-decrement-vs-provision-only.md`,
`bootctr-drain-vs-session10-readings.md`,
`expired-counter-nuke-vs-retry-halt.md`).  Those assume the store is authentic;
this file asks what authenticates it.

## The claims

**Claim A — the key is secure-world/KEK-derived, and the static image has no key
(session 3, `71` §7b; restated as an inference in `session-03.md`).**
The record read path (`0x80010A34`) gets a 16-byte nonce from secure service
`0x12`, computes the 32-byte MAC through secure service `0x2E` with a **runtime**
context pointer (`0x80033760`), and compares it with the stored MAC; the root
key is not in the static image, and the OS resmgr derives it from a hardware KEK
(`"RPMB key gen with KEK %d"`, `select_kek`, `choose_hw_kek`).  Stated
consequence: *"the RPMB key is secure-world/KEK-derived, so a normal-world patch
cannot forge it."*

**Claim B — on this unit the OS runs on a static fallback literal (report `116`
§3; adopted as an inference in `session-10.md`).**
`rpmb-omap4` first tries `FUN_00012814(0, …)` then `FUN_00012814(1, …)` (KEK 0,
then KEK 1) and, **when both fail**, copies the first `0x20` bytes of the string
`"retreating is advancing in the other direction"` over its key buffer — i.e. the
literal `"retreating is advancing in the o"`.  Our `select_kek` SMC (`0x31`)
returns `−1` at every probe point, so *"TabletOS itself runs on the fallback …
the RPMB key on this box is therefore almost certainly those 32 bytes"*.

## Evidence for each

**For A:**

* The bootloader's own record read is a MAC over the payload produced by
  **secure service `0x2E`**, with the nonce from service `0x12` and the key
  context in a runtime buffer above the file image; no key bytes exist in
  `hd0head.bin` (`docs/reports/71-hd0-mode-map-and-verifier.md` §7b;
  `session-03.md` §6).
* The 14-byte name compare in the record parser is a plain `memcmp` and is *not*
  the boundary — that reading is unchanged and is what makes the MAC path the
  only thing to attack.
* The record table and block buffers (`0x8002C760`, `0x8002A4E0`, `0x80033F80`,
  `0x80025FB0`, `0x80033760`) are all runtime-populated; nothing in the image
  resolves to key material.

**For B:**

* The decompiled `rpmb-omap4` shows the KEK attempt followed by the literal
  `memcpy` on failure, with the driver's own log strings
  (`"RPMB key gen with KEK %d failed, retrying"`) — a deliberate fallback, not an
  artefact of the decompiler (`docs/reports/116-rpmb-bootctr-plan-2026-10-27.md`
  §3, quoting `rpmbout/decompiled.txt` line 1999).
* The same 46-byte string is present in `proc/boot/rpmb-omap4` (33 601 B) **and**
  in the signed ramloader `loader_06001A06-00.bin` (string at file offset
  `0x38da0`) — it is part of the shipping product, not a local patch.
* `select_kek` (`0x31`) returned `−1` at every probe point
  (`docs/reports/116-rpmb-bootctr-plan-2026-10-27.md` §3, which cites `109` §6;
  `session-10.md` *Unknown* keeps it as an open question), which is the condition
  under which the fallback runs.
* OS-side RPMB writes demonstrably work: the fixed top-up script wrote `0x32`
  and the counter read back as written (`session-11.md`, report `116` addenda).
  Something authenticates those writes, and on this unit it is not the KEK path.

## Why they cannot both be right (in the strong form)

An eMMC RPMB partition has **one** key, programmed once.  If TabletOS's writes
are accepted while its key is the static literal, then the device's RPMB key *is*
that literal (or is derived from it), and any normal-world holder of the literal
can compute a valid MAC for an arbitrary block — which directly refutes A's
consequence, "a normal-world patch cannot forge it".

They can still *both* be partly true in a weaker form: A describes the
**bootloader's read** (the monitor computes the MAC, so the bootloader never sees
the key), while B describes the **OS resmgr's write**, and the two could use
different key domains over the same store.  That would be a design flaw rather
than a contradiction, but nothing read so far proves it: a second key domain is
not visible in either the loader's service calls or the resmgr's strings.

## What is still uncertain

*These were the open questions at the session-18/19 resolution.  The key and its
MAC were pinned (report 143), so the "same key" and "B was proven" questions are
answered; what remains is the **block seal's transform** (reports 148/156), a
different question from the key.*

* **Whether the monitor's `0x2E` key is the same 32 bytes.**  If it is, A's
  "secure-world only" framing is wrong in substance (the key is secure *by
  custody*, not by derivation) and the forgeability consequence follows.  If it
  is not, the store would have to accept two keys, which RPMB does not do — so
  this is the decisive question.
* **Whether `select_kek == −1` is real.**  `session-10.md` records it as a
  measured return value but lists "wrong descriptor / missing L4SEC setup" as the
  alternative explanation, and `116` §7 keeps "whether the KEK path would work if
  L4SEC were set up as `trustzone-omap4` does" open.  A probe artefact would leave
  B untested rather than false.
* **Whether B was ever proven, or only inferred.**  `116` §3 says the literal
  "is therefore almost certainly those 32 bytes, which E2 proves with a read" —
  but E2 (an authenticated read of block 0, nonce + HMAC, expecting `result == 0`)
  is a step in the *planned* ladder; no executed-E2 result is recorded in the
  notes.  The claim rests on the decompilation plus `select_kek == −1`.
* **Whether a forged record would actually boot.**  Even with a valid MAC, the
  record must satisfy the RPMB **write counter** (monotonic; `0x31` query) and the
  resmgr's metadata/block consistency rules, and the bootloader's own parser has
  the `0x5983BC2D` magic/name/length checks.  A correct MAC is necessary, not
  obviously sufficient.

## What would settle it

1. **E2, run (read-only):** an authenticated read of RPMB block 0 with the
   32-byte literal as key — nonce + HMAC-SHA256, expect `result == 0` and a
   matching MAC.  Success proves the key; failure kills B and un-bricks nothing.
2. **Same-store check (read-only):** with that key, authenticate a read of the
   block holding `BOOTCTR_FCTRY` and compare the stored value with
   `/dev/rpmb/BOOTCTR_FCTRY`.  Agreement proves the OS view and the raw partition
   are one keyspace.
3. **The monitor side:** instrument service `0x2E` on a sacrificial unit and
   compare the MAC it computes over a known block with the one computed from the
   literal.  Equal MACs settle the "same key" question without extracting a key.
4. **Operator-only, on a unit they can afford to lose:** write a record whose
   MAC was produced host-side from the literal and see whether the bootloader
   accepts it.  This is a write — `116` §5's ladder forbids it on the working
   unit, a wrong result there is a brick, and there is no spare left.

Until then: keep `116` §5's rule (no RPMB write on the working unit), and read
Claim A as "not recoverable from the static **hd0** image" rather than "cannot be
forged from the normal world".

## References

* `docs/reports/71-hd0-mode-map-and-verifier.md` §7b — the secure-service `0x2E`
  MAC path, the nonce, the runtime key pointer, "not in the static image".
* `docs/sessions/session-03.md` §6 / *Inferred* — the "secure-world/KEK-derived,
  so a normal-world patch cannot forge it" statement, and its later parenthetical
  pointing at `116` §3.
* `docs/reports/116-rpmb-bootctr-plan-2026-10-27.md` §3 — the recovered literal,
  the KEK-then-fallback code, `select_kek == −1`, and the E2 step that would prove
  it; §5 for the write ladder; §7 for the open KEK/L4SEC question.
* `docs/sessions/session-10.md` *Inferred*'s `select_kek` bullet and *Unknown*
  (first bullet) — the "QNX runs on its hardcoded fallback key" inference and the
  "wrong descriptor / missing L4SEC setup" alternative.
* `docs/sessions/session-11.md` *Confirmed* — RPMB writes land on this unit
  (`bootctr: wrote 0x32, readback ok`).
* `docs/reports/116-rpmb-bootctr-plan-2026-10-27.md` §6 and
  `docs/reports/92-rpmb-factory-write-runbook.md` §0/§6 — why software recovery is
  absent for the bricked units and what a write risks.
