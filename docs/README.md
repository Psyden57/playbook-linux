# Documentation index

Verified at session 23 (2026-11-02).

Current state lives in **`project/STATE.md`** and only there; the next-session
kickoff is **`project/HANDOFF.md`**.  Everything else describes a mechanism or
preserves evidence, and must not restate status.

## Ownership — one document per kind of fact

| doc | owns | must not contain |
|---|---|---|
| `project/STATE.md` | **the** current state, date-stamped, and the single current blocker | history above its divider (keep history below it) |
| `project/HANDOFF.md` | the next-session kickoff, copied at wrap-up | anything not needed to start |
| `project/OVERVIEW.md` | what/why, hardware, boot chain | status tables, unit state, "what works" |
| `project/ROADMAP.md` | what is next and what closes it | prose about the present |
| `project/ARCHITECTURE.md` | components and how they interact | status |
| `project/KNOWN-ISSUES.md` | traps, and corrections to older reports | current state |
| `project/FACTORY-PATH.md` | the product/ECDSA vs factory/CRC paths and the switch procedure | current unit state |
| `project/BOOT-CHAIN.md` | the boot stages, stage by stage | current unit state |
| `project/IFS-REBUILD.md` | the 2.1.0.1917 image rebuild: layout, format, milestones | current unit state |
| `project/ARMING-TUTORIAL.md` | the operator's arming procedure, step by step | current unit state |
| `project/SAFETY.md` | what may be written, by whom | status |
| `project/DECISIONS.md` | decisions with their evidence, and the open ones | current state |
| `project/SETUP.md` | host toolchains, paths, prerequisites | status |
| `project/DEVELOPMENT.md` | build → wrap → flash → boot loop, and the bench command reference | status |
| `README.md` | this index and the ownership table | session counts or ranges |
| root `README.md` | the project front page and quick start | status (point to `STATE.md`) |

**Freshness stamp.**  Directly below the title of every canonical doc is
`Verified at session N (YYYY-MM-DD)`.  A stamp older than the newest session
note means the document is suspect by definition; refresh it when you verify
the file.  `tools/docs_audit.py` checks the stamps and the cross-references.

## `project/` — the canonical documentation

| file | what |
|---|---|
| `STATE.md` | where it stands right now, per subsystem, plus the last image CRC and the single current blocker |
| `HANDOFF.md` | self-contained handoff for the next session |
| `OVERVIEW.md` | what the project is, the hardware, the boot chain, why it exists |
| `ARCHITECTURE.md` | components and how they interact (chainloader, kernel/DTB, initramfs, display chain, USB, the QNX oracle) |
| `ROADMAP.md` | what is next, in what order, and what would close each item |
| `KNOWN-ISSUES.md` | the operational traps, the corrections to older reports, and the display work's history |
| `FACTORY-PATH.md` | the product/ECDSA vs factory/CRC boot paths, what a clean tablet looks like, and the operator-only switch procedure (read after `SAFETY.md`) |
| `BOOT-CHAIN.md` | the boot chain stage by stage, and the read-only verification protocol |
| `IFS-REBUILD.md` | the working notes for the modified 2.1.0.1917 IFS |
| `ARMING-TUTORIAL.md` | the arming procedure as a tutorial, for outside readers |
| `SAFETY.md` | **read before writing anything to the device**: the two non-volatile stores, `BOOTCTR_FCTRY` values that brick, the media write path, and who may write |
| `DECISIONS.md` | decisions taken with their evidence, and the open ones (license, visibility, proprietary corpus) |
| `SETUP.md` | host toolchains, paths, prerequisites |
| `DEVELOPMENT.md` | build → wrap → flash → boot loop, and the bench console command reference |

## `reports/` — the analysis this is built on

Numbered reports written during the bring-up: live register captures of the
reference OS, static reverse engineering, bench results and their corrections.
`HANDOFF-2026-10-27-session6.md` and `NEXT-SESSION-BOOTSTRAP-2026-10-26.md` in
this directory are the session-6 handoff and the long-form manual (the copies in
`project/` are the canonical, path-adjusted versions).

**Reports are history and are left as written**, except where a later report
corrects a specific claim (those corrections are applied in place and listed in
`project/KNOWN-ISSUES.md` §"Corrections"), or where a runbook's *state
assumption* has changed — those carry a dated "superseded" banner at the top
(reports **90** and **92**).  In particular: 118 §8.2 (DSI link rate) and 111 §8
item 4 (bridge programming) are corrected inline.

## `sessions/` — one note per session

One file per working session, `session-NN.md`, with **absolute chronological
numbering**.  Read `sessions/NUMBERING.md` first: the reports and handoffs call
the Linux-era sessions "session 1…N" (`absolute = Linux-era + 6`).  Each note is
written by that session and treated by later ones as *evidence*; `project/` is
the *product*.  See `sessions/README.md` for the rules, the backfill procedure,
and **how to wrap up a session** (the paste-ready prompt is at the end).

## `captures/` — live text captures

Register dumps, DRAM ring buffers and console transcripts taken from the device
during bench sessions.  Useful when a register value in a report needs checking
against the raw data.

## `contradictions/` — recorded disagreements

Each file states the conflicting claims, the evidence for each, what is uncertain
and what would settle it.  Resolved ones are kept for their history.

## Where the binaries are

The proprietary QNX dumps, the TI TRM, the device images, the sealed IFS, the
SSH keys and the kernel build tree live in the gitignored `local/` directory at the
repository root.  Nothing there is committed, and `.gitignore` states why.
