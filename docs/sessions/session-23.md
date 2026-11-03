# Session 23 (absolute) — docs audit, public snapshot, and repository management

> Numbering: absolute `23`; the reports call this Linux-era **session 17**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-02 → 2026-11-03
Unit state at the end: **unchanged — no USB or bench work this session.**  The
last measured state stands: the reference (`0x50175127`) is armed and sealed,
`BOOTCTR_FCTRY` is present (`32 00 00 00`, Factory mode, counter around 50),
the sealed `os_type` 6 IFS is on media (the exact asset was not read back), the
region-1 Linux root is in place, and TabletOS was up at the last measurement.
See `STATE.md`.

## Goal of this session

The kickoff offered two tasks: the 2.1.0.1917 IFS writer (`tools/ifs_build.py`,
milestone 1 of `IFS-REBUILD.md` §6) and the documentation audit in report 167.
The operator asked for the docs task first.  It grew into a full docs re-read, a
public GitHub publication, and a repository-management guide.  **The IFS writer
was not started**; it remains the next task.

## Confirmed

* Report 167's audit was executed: findings #1–#14 plus two it missed
  (`SAFETY.md` §6's reference-read-only row and two-unit inventory;
  `DEVELOPMENT.md` §1's spare-only bench).  The ownership model, the
  `docs_audit.py` guard, the "superseded" banners on reports 90/92 and the
  pointers on report 160 / `session-21.md` are in place.
* A second full read found the root `README.md` (still said 2026-11-15, a black
  panel, "RPMB not started", a 29-file patch, "never mount the eMMC") and the
  contradictions index were stale.  Both were corrected.
* All 16 contradiction files were re-read.  `118-vs-121` and `111-vs-122` had
  the impossible date 2026-11-15; `111-vs-122`, `111-vs-123` and
  `open-dispc` still described the bridge as unprogrammed/unreadable;
  `71-bootctr…` still called a settled mechanism open; `rpmb-key-kek…` still
  said the monitor refuses Linux with `0xFF05`; `safety-no-emmc…` still said a
  writable mount was forbidden; `bootctr-drain…`'s rate is settled by session
  22's `50 → 49 → 47 → 49 → 50`; `66` and `expired-counter…` still offered the
  dead spare as a test platform.  The index now ends with an explicit
  **"Still open"** list.
* The public snapshot was built and pushed to
  `git@github.com:Psyden57/playbook-linux.git`: initial release `f2e65be`, then
  the doc commits through `1c4962d`.  The full private history was not pushed.
* Licence: **GPL-3.0-or-later** (`LICENSE`), with `NOTICE` recording the
  GPL-2.0-only kernel patch, the GPL-3.0 `tools/bbusb.py` derivation, and the
  excluded proprietary corpus.
* Repository management is documented in `docs/project/REPO-MANAGEMENT.md`,
  with `tools/publish_public.sh` (dry run by default; `git archive HEAD` →
  rsync into the public checkout; never commits or pushes).
* Deleted the earlier-phase document tree (`docs/history/`) and `SECURITY.md`;
  removed trap 41; corrected the `UFS = region 1` statement, the "two 32 GB
  units in this state" note, and the "BB10 2.1.0.1917" wording; dropped the
  per-doc `Verified at session` stamps.
* No device or USB access, no RPMB write, no media write, no reboot.

## Inferred

* The GitHub repository is **private**: both the web page and the API returned
  404 to anonymous requests at session end.  No token was used to inspect the
  setting; the operator's browser action flips it (Settings → Danger Zone).
* The tracked `docs/reports/data/` captures (including the RPMB pages) are
  public by the operator's explicit decision; keys, the device password and the
  PIN/BSN are not treated as sensitive.  The proprietary PDFs/TRM and the
  bb10mt source remain excluded.
* The public tree is now a mechanical snapshot of the private tracked tree:
  the only public-only wording differences (`DEVELOPMENT.md`, `tools/README.md`)
  were unified, so `publish_public.sh` shows only real changes.

## Dead ends and wrong assumptions

* **The first docs pass was not a verification.**  It scanned `docs/project` and
  `docs/README.md` but never opened the root `README.md` or the contradictions
  files.  The operator's challenge caught it; the second pass corrected it.
* **The freshness-stamp convention** (`Verified at session N (YYYY-MM-DD)` in
  every canonical doc) was implemented and then dropped as noise; the guard no
  longer requires or checks it.  Report 167 §11 records the reversal.
* **`UFS (region 1)`** was wrong.  The UFS is the `hd3` FS partition (MCT type
  `0x1C`); region 1 is the second OS slot (another IFS/RCFS).  Fixed in
  `ARMING-TUTORIAL.md`.
* **"The two 32 GB units in this state"** was wrong: one has the expired-counter
  failure; the other had its NVRAM partition erased and refuses to boot with
  `nvram signature invalid`.  Fixed in `FACTORY-PATH.md`.
* **"BB10 2.1.0.1917"** was wrong: 2.1.0.1917 is PlayBook OS 2.x, not BB10.
  Removed from `ROADMAP`, `IFS-REBUILD`, `STATE`, `HANDOFF` and `OVERVIEW`.
* **The public README carried two agent-facing bullets** (`0x3B04298E` and the
  sealed-IFS restore).  They were removed from the public page; the operational
  notes remain in `SAFETY.md` / `STATE.md`.
* **A public-only wording divergence** in `DEVELOPMENT.md` / `tools/README.md`
  would have been overwritten by every snapshot; both trees now use the same
  wording.
* **The first `publish_public.sh` dry run flagged every file** because
  `git archive` rewrites mtimes; it now uses `--checksum` and filters
  directory/timestamp-only lines.

## Unknown / open

* **The public repository is still private.**  The operator must flip it in
  Settings → Danger Zone; the guide explains the check.
* **The historical corpus still mentions the earlier abandoned phase** in 33
  report/session files.  The canonical docs and the deleted `docs/history/`
  tree are clean; scrubbing the historical corpus is a separate, larger edit.
* The four genuinely open contradictions remain: the expired-counter branch,
  `GPIO2_24` as RESX (not blocking), the writer of the RPMB page slots, and the
  session-10 drain anomaly.  Residuals: `66`'s `count` source and `100`'s MCT
  question.
* **The IFS writer and milestones 2–4 were not started.**  `tools/ifs_build.py`
  is still the next task.

## Superseded / uncertain

* The per-doc `Verified at session N` stamp convention — dropped (report 167
  §11); the guard's other checks remain.
* `docs/history/` and `SECURITY.md` — deleted; do not recreate them.
* The "two threads / documentation audit" framing in the handoff — the docs
  task is done; the next handoff has one task (the IFS rebuild).
* The "whether every retail PlayBook starts unarmed" paragraph — deleted; every
  retail unit starts unarmed except rare not-for-sale developer units.
* The public README's `0x3B04298E` warning and sealed-IFS restore instruction —
  removed from the public page (kept in the operational docs).

## Artefacts produced

* Private commits (newest first): `4574455`, `e8a3abc`, `a07781b`, `81a5751`,
  `da4f805`, `3e446e9`, `64491b0`, `dd5c9b1`, `304d8d2`, `85d11ae`, `c78ab88`,
  `51711b2`, `45c315a`.
* Public repository: `git@github.com:Psyden57/playbook-linux.git`; initial
  release `f2e65be`, doc commits through `1c4962d` (the wrap-up commit follows).
* New: `docs/project/REPO-MANAGEMENT.md`, `tools/publish_public.sh`,
  `LICENSE`, `NOTICE`, `.gitattributes`, `.github/workflows/ci.yml`,
  `docs/contradictions/ldr01-0x10000-header.md`.
* Changed: `tools/docs_audit.py` (ownership checks, contradiction dates/index,
  no stamp), report 167 (§8–§11 execution record), the canonical docs and the
  root README.
* Deleted: `docs/history/` (84 tracked files plus the ignored 11 MB tree),
  `SECURITY.md`.
* No images or bench artefacts were produced.
