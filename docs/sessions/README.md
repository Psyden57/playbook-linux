# Session notes

Verified at session 23 (2026-11-02).

One file per working session, **absolute numbered**: `session-NN.md`.  Read
`NUMBERING.md` first — the reports and handoffs call the Linux-era sessions
"session 1…N", which are absolute `N+6`.

Each note is written **by that session**, from its own context.  Later sessions
treat the notes as *evidence*; the canonical documents under `docs/project/` are
the *product*.

## Why this exists

The project is long-running; `NUMBERING.md` has the current count and the full
absolute ↔ Linux-era mapping.  Historically those handoffs were written at
roughly 50–60 % of the context budget
rather than at the end (the private working history records this; it is not
included in the public snapshot), which is why they
are lossy: a handoff keeps what the next session needed and drops what looked
unimportant at the time.  **That is a record of how the notes came about, not an
instruction** — an agent working on this repository must not budget or trim its
work against an assumed context window; the operator decides when to stop.  The
notes are the backfill trail — they try to recover the reasoning, measurements,
dead ends and hardware facts that no longer appear in the canonical docs.  The
first six sessions in particular hold the device's core facts (boot ROM,
second-stage loader, NVRAM, RPMB, the CRC path) that the later display work only
refers to in passing.

## Rules for writing a session note

1. **Never delete or rewrite another session's note.**  If you disagree with one,
   say so in your own note and, if it matters, add a file under
   `docs/contradictions/`.
2. **Separate the three kinds of statement** (headings in `_TEMPLATE.md`):
   *Confirmed* (measured, read in a file, or seen on the bench), *Inferred*
   (reasoning from evidence), *Unknown* (open questions).
3. **Cite the artefact**, not the memory: a report file and section, a register
   read, a commit, a path in the tree.
4. **Do not silently resolve contradictions.**  If your session believed X and the
   canonical docs now say Y, record X, the evidence you had, and why it was
   superseded — or why you think it was not.
5. **Older sessions must not overwrite newer corrections.**  Anything that
   conflicts with a newer statement goes in your note under
   *Superseded / uncertain*, never into the canonical docs.
6. Compress.  What would a new developer need that is *not* already in
   `docs/project/`?  Duplicating the canonical docs is not useful.

## Which reports belong to which phase

Use this to tell a session what to read; the numbers are in `docs/reports/`.

| absolute sessions | phase | reports |
|---|---|---|
| 1 – 6 | research and boot chain: boot ROM, `hd0` second-stage loader, monitor/security, IRAM/ROM tables, NVRAM records (`0x2801`), RPMB, the CRC recipe, the loader audit, pre-OS IFS reflash | `10` … `102` |
| 7 – 18 | Linux bring-up (Linux-era 1…12): clean 6.15.11 on the CRC path, capture v2, the stall/VFS panic, WDT2/eMMC/USB/console, display RE + live QNX capture, backlight/TCON/four lanes, the write path, the region-1 root, RPMB feasibility and the boot-log fixes | `103` … `142` |

## Backfill procedure (operator)

Traverse **12 → 1** (newest first), then return to **12** for the sanity check.

For each session, open it and paste the prompt below with `NN` replaced by its
absolute number and `PHASE`/`REPORTS` taken from the table above.  A Linux-era
session should also be told its report-era number (`NN - 6`).

```
This repository is the consolidated workspace for the PlayBook Linux bring-up.
The earlier work happened in `$HOME/rea-work`, which is now read-only; do not
modify it.

You are session NN (absolute, chronological). <If applicable: in the
reports and handoffs your session is called "session NN-6", because numbering
only started with the Linux work.>

Read, in this order:
  docs/sessions/NUMBERING.md
  docs/project/OVERVIEW.md, STATE.md, ARCHITECTURE.md, DECISIONS.md,
    KNOWN-ISSUES.md, HANDOFF.md
  docs/sessions/README.md and every docs/sessions/session-*.md that already
    exists (sessions newer than yours are already written — they are newer
    than you)
  docs/reports/HANDOFF-2026-10-27-session6.md and
    docs/reports/NEXT-SESSION-BOOTSTRAP-2026-10-26.md  (current state + manual)
  the reports for PHASE (docs/reports/): REPORTS
  if yours is a pre-Linux session, also the private `docs/history/` tree (not
  included in the public snapshot; the kexec-phase docs and the operator's notes)

Then write docs/sessions/session-NN.md from your own session context: the
measurements, reasoning, dead ends and device facts this session had that are
missing from the repository. Follow docs/sessions/_TEMPLATE.md.

Rules:
- Add only. Never edit or delete another session's note, and do not rewrite
  docs/project/* just because your interpretation differs.
- If your memory conflicts with a newer document, put it in your note under
  "Superseded / uncertain" with the evidence you had; if it is substantive, add
  docs/contradictions/<slug>.md. Do not silently resolve it.
- Distinguish confirmed facts from inferences from unknowns. Cite the artefact
  (report file and section, register read, file path) for every non-obvious
  claim.
- Pre-Linux sessions: prioritise the core device knowledge that everything else
  depends on — boot ROM behaviour and its limits, the second-stage loader, the
  media/region layout, the NVRAM and RPMB rules (what may never be written), the
  CRC/signature path, and which tools were built and why.
- Use your native read/write tools, no python3/sed.
- When done: `git add docs/sessions/session-NN.md` (plus any contradictions
  file) and commit as `docs: backfill session NN knowledge`.

Reply with a short list of what you added and what you deliberately left out.
```

## After the sweep — the sanity check

Return to session **12** (or start a fresh session) and run the reconciliation
pass: treat the session notes as evidence and `docs/project/*` as the product;
reconcile where the evidence allows it, keep unresolved contradictions explicit,
verify the important claims against the tree, and leave the documentation
GitHub-ready.  Do not delete a session note to make the docs tidy — the trail is
the point.

**Status: the pre-Linux notes are written and the first reconciliation pass has been
run** (2026-10-28).  It produced `docs/project/SAFETY.md`, rebuilt the
contradictions index (ten files, four of them open), fixed the numbering and
cross-references in the notes, and cleared the tree defects listed in
`docs/project/KNOWN-ISSUES.md` §4.  A later pass is still welcome — the open
contradictions and the instrumentation left in the patch are the obvious
starting points.

**Session 13** (absolute; Linux-era "session 7") was written by the session
itself on 2026-10-28 (`session-13.md`), not backfilled.  It covers the display
work (reports 123–127) and is the first note written after the reconciliation
pass.

**Session 14** (absolute; Linux-era "session 8", `session-14.md`) covers the
bridge-delivery fix and the HS-video investigation (reports 128–131).  It also
records the bench state the next session inherits, re-read at close rather than
recalled.

**Sessions 15–19** (`session-15.md` … `session-19.md`) were written by the
sessions themselves.  Session 18 covers the region-1 Linux root, the V1 `qcfp`
misroute and recovery, the RPMB top-up feasibility and the boot-log fixes
(reports 139–142).  Session 19 pins the RPMB frame key, finds the record block
sealed by the monitor, implements the monitor call (DRAM bias, then `0xFF05`) and
closes the RE leads (reports 143–146).

---

## Wrapping up a session (paste this when you stop)

**The operator decides when a session stops.**  Do not time the wrap-up yourself and
do not shorten the work on the assumption that a context window is running out —
work the problem at full scope until told to stop, then record everything before
finishing.  If the note is being written under interruption, put the skeleton down
first (headings + one line each) and fill it in; a skeleton that survives is worth
more than a perfect note that does not.

```
We're stopping here. Record everything properly before we finish, in this order.
Do not rely on the transcript surviving; write it to the repository.

1. BENCH STATE FIRST (what the next session inherits)
   - Is the sealed IFS on media? If not, restore it (local/images/ifs0.sealed.bin),
     verify sha256 c72b0530…, and read BOOTCTR_FCTRY (expect 0x32).
   - Record: which OS the unit is running, door/ssh state, BOOTCTR value, the
     image CRC on media, and any device-side file you deployed.

2. SESSION NOTE: docs/sessions/session-NN.md, following _TEMPLATE.md.
   Capture what is NOT already in the docs:
   - Confirmed — each claim plus the artefact that proves it (register read,
     report section, file path, or an explicit operator observation).
   - Inferred — the evidence it rests on and how strong it is.
   - Dead ends and wrong assumptions — with the measurement that killed each.
     This is the part handoffs lose; it is the most valuable section.
   - Unknown — open questions, each with what would settle it.
   - Superseded — anything you believed that a newer document contradicts: the
     old belief, the evidence you had, and that the newer one is current.
   - Artefacts — reports, tools, DTS/driver changes, images (with CRC).

3. CANONICAL DOCS — facts only, no narration:
   - docs/project/STATE.md       what changed in the state table, and the single
                                 current blocker
   - docs/project/HANDOFF.md     the next step: the one experiment, its exact
                                 inputs, and what each possible result means
   - docs/project/KNOWN-ISSUES.md  new traps; move fixed defects to the fixed list
   - docs/project/DECISIONS.md   only if a decision was actually taken, with the
                                 evidence
   - docs/project/ROADMAP.md     if priorities moved
   - docs/project/SAFETY.md      only if a safety rule changed

4. CONTRADICTIONS — if two sources disagree and you cannot settle it with
   evidence, add docs/contradictions/<slug>.md (claims, evidence for each, what
   is uncertain, what would settle it) and index it in that directory's README.
   Never resolve a contradiction silently.

5. VERIFY WHAT YOU WROTE
   - every path you cited exists;
   - every register value and CRC you quoted came from a measurement in this
     session (not from memory or an older note);
   - every command you documented was run in this session, or is marked as
     untested;
   - run `python3 tools/docs_audit.py` — it must print `clean` (stamps, session
     counts/ranges, stale phrases, removed tool names, report references);
   - git status is clean after the commit.

6. COMMIT: `docs: session NN wrap-up` (session note + doc updates).

Then reply with: the single next step, the open questions, and anything you could
not verify.
```

### What "good" looks like, from this project

The wrap-up that produced the best next session had four properties, all worth
copying:

1. **It quoted numbers, not adjectives** — `COMPLEXIO_CFG1 = 0x2A054321`,
   `LINE_STATUS` advancing, `bl_power = 0`, `crc 0xE1F79294`.  A number can be
   re-checked; "the display looked better" cannot.
2. **It said what was *ruled out*, and by what measurement.**  Half the value of
   the session-6 handoff was the list of hypotheses that were already dead.
3. **It gave the next step as a single experiment with a decision table** — "if
   X happens, do Y; if not, Z is the remaining suspect".
4. **It recorded the bench state** — sealed IFS, counter value, image CRC, door
   state — so the next session did not spend its first ten minutes discovering
   the unit was in the ramloader.
