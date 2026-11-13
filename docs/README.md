# Documentation index

Current state lives in **`project/STATE.md`** and only there.  The next-session
kickoff is **`project/HANDOFF.md`**.  Everything else describes a mechanism or
preserves evidence, and must not restate status.

## Editing these documents (operator rule, session 45)

**Edit documentation with the native read/edit tools, never with `python`,
`sed`, `awk` or any script.**  Read the whole target document first, then make
the change in place.  Scripted edits have repeatedly left duplicated
paragraphs, broken tables and stale text that a later session then has to audit
and repair — roughly every ten sessions the docs have needed a dedicated
cleanup pass, and that is the cause.  A script is fine for *generating* an
artefact (a patch, an image, a capture); it is not fine for rewriting prose you
have not read in full.  (Trap 200 in `project/KNOWN-ISSUES.md`.)

## Ownership — one document per kind of fact

| doc | owns | must not contain |
|---|---|---|
| `project/STATE.md` | **the** current state, the blockers in play, the unit, the current images, the next step | per-session history (that is in `history/`) |
| `project/HANDOFF.md` | the next-session kickoff, copied at wrap-up | anything not needed to start |
| `project/OVERVIEW.md` | what/why, hardware, boot chain | status tables, unit state, "what works" |
| `project/ROADMAP.md` | open work only, and what closes it | closed-item prose (that is in `history/`) |
| `project/ARCHITECTURE.md` | components and how they interact | status |
| `project/KNOWN-ISSUES.md` | open traps, current corrections, documentation defects | resolved display history and fixed ledgers (those are in `history/`) |
| `project/FACTORY-PATH.md` | the product/ECDSA vs factory/CRC paths and the switch procedure | current unit state |
| `project/BOOT-CHAIN.md` | the boot stages, stage by stage | current unit state |
| `project/SAFETY.md` | what may be written, by whom | status |
| `project/DECISIONS.md` | decisions with their evidence, and the open policy questions | current state |
| `project/SETUP.md` | host toolchains, paths, prerequisites | status |
| `project/DEVELOPMENT.md` | build → wrap → flash → boot loop, and the bench command reference | status |
| `project/KERNEL-PORT.md` | the current kernel base and patch (short) | the completed port plan (that is in `history/`) |
| `project/ARMING-TUTORIAL.md` | the operator's arming procedure, step by step | current unit state |
| `project/REPO-MANAGEMENT.md` | the private working tree vs the public snapshot, and the publish procedure | status |
| `history/` | superseded state, closed roadmap items, fixed ledgers, the completed port plan | nothing current |
| `parked/` | operator-parked work (BB10, PlayBook OS image rebuild) | nothing current |
| `reference/` | our reconstruction of a reference-OS artifact (what it does, pins, registers) | status, session history, the vendor binary itself |
| `README.md` | this index and the ownership table | session counts or ranges |
| root `README.md` | the project front page and quick start | status (point to `STATE.md`) |

## `project/` — the canonical documentation

| file | what |
|---|---|
| `STATE.md` | where it stands, the blockers, the unit, the current images and the next step |
| `HANDOFF.md` | self-contained handoff for the next session |
| `OVERVIEW.md` | what the project is, the hardware, the boot chain, why it exists |
| `ARCHITECTURE.md` | components and how they interact (chainloader, kernel/DTB, initramfs, display chain, USB, the QNX oracle) |
| `ROADMAP.md` | open work in order, and what would close each item |
| `KNOWN-ISSUES.md` | operational traps, corrections to older reports, and documentation/tree defects |
| `FACTORY-PATH.md` | the product/ECDSA vs factory/CRC boot paths, and the operator-only switch procedure (read after `SAFETY.md`) |
| `BOOT-CHAIN.md` | the boot chain stage by stage, and the read-only verification protocol |
| `SAFETY.md` | **read before writing anything to the device**: the two non-volatile stores, `BOOTCTR_FCTRY` values that brick, the media write path, and who may write |
| `DECISIONS.md` | decisions taken with their evidence, and the open policy questions |
| `SETUP.md` | host toolchains, paths, prerequisites |
| `DEVELOPMENT.md` | build → wrap → flash → boot loop, and the bench console command reference |
| `KERNEL-PORT.md` | the current 7.3.3 base, config and patch (short) |
| `ARMING-TUTORIAL.md` | the arming procedure as a tutorial, for outside readers |
| `REPO-MANAGEMENT.md` | the private/public repository split, `tools/publish_public.sh`, and the commit/push rules |

Four files in `project/` are **link stubs** for parked work: `BB10-PORT.md`,
`BB10-IMAGE-GUIDE.md`, `IFS-REBUILD.md` and `IMAGE-REBUILD-GUIDE.md`.  Their
content is in `parked/`.

## `history/` — superseded material

Append-only or completed material moved out of the canonical docs.  Nothing here
is current state.

| file | was |
|---|---|
| `STATE-HISTORY.md` | per-session state snapshots and the older image inventory |
| `KNOWN-ISSUES-HISTORY.md` | resolved display history and the per-session fixed ledgers |
| `ROADMAP-CLOSED.md` | closed and parked roadmap items in full |
| `KERNEL-PORT.md` | the full 7.3.3 port plan and checkpoint log |

## `parked/` — operator-parked work

| folder/file | what |
|---|---|
| `BB10-PORT.md`, `BB10-IMAGE-GUIDE.md` | the BB10-on-PlayBook analysis and image guide (parked session 25) |
| `IFS-REBUILD.md`, `IMAGE-REBUILD-GUIDE.md` | the PlayBook OS IFS/RCFS/UFS rebuild procedure (parked session 25) |

The BB10 notes and the image-rebuild guides overlap by design; they share the
IFS/RCFS/container toolchain.  Keep the set together until the operator decides
which parts to retain.

## `reference/` — reconstructions of the reference OS

Our own write-ups of what a vendor binary does: decoded tables, addresses and
pseudo-code we produced.  The vendor binaries are gitignored under `local/` and
are never committed.

| folder | what |
|---|---|
| `reference/startup-playbook/` | the 1917 `proc/boot/startup-playbook` board bring-up: the board/silicon identity, the 75-record GPIO table, the WLAN/BT pin selection, the SCRM `AUXCLK1` programming, the WDT2 policy and the MMIO map |

## `reports/` — the analysis this is built on

Numbered reports written during the bring-up: live register captures of the
reference OS, static reverse engineering, bench results and their corrections.
`reports/INDEX.md` says which report is current for a topic and which are
superseded.

**Reports are history and are left as written**, except where a later report
corrects a specific claim (those corrections are applied in place and listed in
`project/KNOWN-ISSUES.md` §3), or where a runbook's *state assumption* has
changed — those carry a dated "superseded" banner at the top (reports **90** and
**92**).  In particular: 118 §8.2 (DSI link rate) and 111 §8 item 4 (bridge
programming) are corrected inline.

## `sessions/` — one note per session

One file per working session, `session-NN.md`, with **absolute chronological
numbering**.  Read `sessions/NUMBERING.md` first: the reports and handoffs call
the Linux-era sessions "session 1…N" (`absolute = Linux-era + 6`).  Each note is
written by that session and treated by later ones as *evidence*; `project/` is
the *product*.

## `captures/` — live text captures

Register dumps, DRAM ring buffers and console transcripts taken from the device
during bench sessions.  `captures/README.md` indexes them and records which files
are exact duplicates.  Useful when a register value in a report needs checking
against the raw data.

## `contradictions/` — recorded disagreements

Each file states the conflicting claims, the evidence for each, what is uncertain
and what would settle it.  Resolved ones are kept for their history.  The index
puts the open questions first.

## Where the binaries are

The proprietary QNX dumps, the TI TRM, the device images, the sealed IFS, the
SSH keys and the kernel build tree live in the gitignored `local/` directory at
the repository root.  Nothing there is committed, and `.gitignore` states why.
