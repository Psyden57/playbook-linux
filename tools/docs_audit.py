#!/usr/bin/env python3
"""Mechanical guard for the canonical documentation.

Checks, across ``docs/project/*.md`` plus ``docs/README.md`` and
``docs/sessions/{README,NUMBERING}.md``:

* session totals and ranges whose number is older than the newest note;
* a curated list of stale phrases (report 167 §4), unless the line carries
  ``<!-- audit-ok: reason -->``;
* removed tool names (``bbinfo``, ``bbflash``, ``pbreset``; ``qcfm`` only as a
  bare command, because it is also the container format's and ``qcfm.pas``'s
  name);
* ``report NNN`` references that do not resolve to a file under ``docs/reports/``.

Exit 0 = clean, 1 = findings.  Run it as the last step of the wrap-up checklist
in ``docs/sessions/README.md``.

Usage:
    python3 tools/docs_audit.py            # check, print findings
    python3 tools/docs_audit.py --verbose  # also print the files checked
"""

from __future__ import annotations

import argparse
import datetime
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
REPORTS = DOCS / "reports"

# Report 167 §4's starting list.  Keep lines that quote history exempt with
# `<!-- audit-ok: ... -->` so the exemption is visible in the document itself.
STALE_PHRASES = (
    "never programmed",
    "reboots unconditionally",
    "180 s",
    "no root filesystem",
    "twelve sessions",
    "two working 64 GB",
    "never arm the reference",
    "both live units",
    "CFP.exe resetqnxosregion",
    "half-armed",
    "do not reboot",
    "image-stock",
    "0x8250000",
)

DATE_RE = re.compile(r"\b(20\d{2})-(\d{2})-(\d{2})\b")

REMOVED_TOOLS_RE = re.compile(r"\b(bbinfo|bbflash|pbreset)\b")
# A bare `qcfm` command; not `qcfm.pas`, `QCFM`, or `bb10mt/qcfm`.
QCFM_COMMAND_RE = re.compile(r"(?:^|[\s`'\"])qcfm(?:[\s`'\"]|$)")

REPORT_REF_RE = re.compile(r"\breports?\s+`?(\d{1,3})`?")

RANGE_SESSION_RE = re.compile(r"session-0?1\s*(?:…|\.\.\.)\s*session-(\d{1,2})")
RANGE_ABSOLUTE_RE = re.compile(r"absolute\s+(\d{1,2})\s*(?:…|\.\.\.)\s*(\d{1,2})")
COUNT_SPANS_RE = re.compile(
    r"(?:the\s+)?project\b[^.\n]{0,40}\b(?:spans|has\s+run|ran)\b[^.\n]{0,30}?"
    r"\b([a-z-]+|\d+)\s+sessions",
    re.IGNORECASE,
)
COUNT_SO_FAR_RE = re.compile(r"\b([a-z-]+|\d+)\s+sessions\s+so\s+far", re.IGNORECASE)

NUMBERS = {
    "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
    "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11,
    "twelve": 12, "thirteen": 13, "fourteen": 14, "fifteen": 15,
    "sixteen": 16, "seventeen": 17, "eighteen": 18, "nineteen": 19,
    "twenty": 20, "twenty-one": 21, "twenty-two": 22, "twenty-three": 23,
    "twenty-four": 24, "twenty-five": 25, "twenty-six": 26,
    "twenty-seven": 27, "twenty-eight": 28, "twenty-nine": 29, "thirty": 30,
}


def as_number(token: str) -> int | None:
    token = token.lower()
    if token.isdigit():
        return int(token)
    return NUMBERS.get(token)


def newest_note() -> int:
    numbers = []
    for path in (DOCS / "sessions").glob("session-[0-9][0-9].md"):
        suffix = path.stem.split("-", 1)[1]
        if suffix.isdigit():
            numbers.append(int(suffix))
    return max(numbers, default=0)


def canonical_docs() -> list[Path]:
    return [ROOT / "README.md"] + sorted((DOCS / "project").glob("*.md")) + [
        DOCS / "README.md",
        DOCS / "sessions" / "README.md",
        DOCS / "sessions" / "NUMBERING.md",
    ]


def contradiction_docs() -> list[Path]:
    return sorted(
        p for p in (DOCS / "contradictions").glob("*.md") if p.name != "README.md"
    )


def check_history_dates(path: Path) -> list[str]:
    """History docs may quote old claims, but an impossible date is still a bug."""
    findings: list[str] = []
    rel = path.relative_to(ROOT)
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except UnicodeDecodeError:
        return [f"{rel}: not valid UTF-8"]
    for lineno, raw in enumerate(lines, start=1):
        if "audit-ok" in raw.lower():
            continue
        for match in DATE_RE.finditer(raw):
            try:
                found = datetime.date(
                    int(match.group(1)), int(match.group(2)), int(match.group(3))
                )
            except ValueError:
                findings.append(f"{rel}:{lineno}: invalid date {match.group(0)!r}")
                continue
            if found > datetime.date.today():
                findings.append(f"{rel}:{lineno}: future date {match.group(0)}")
    return findings


def check_contradiction_index() -> list[str]:
    """Every contradiction file is indexed, and every indexed file exists."""
    findings: list[str] = []
    readme = DOCS / "contradictions" / "README.md"
    rel = readme.relative_to(ROOT)
    text = readme.read_text(encoding="utf-8")
    files = contradiction_docs()
    for path in files:
        if f"`{path.name}`" not in text:
            findings.append(f"{rel}: {path.name} is not indexed")
    for match in re.finditer(r"`([^`/]+\.md)`", text):
        name = match.group(1)
        if name == "README.md":
            continue
        candidates = (
            DOCS / "contradictions" / name,
            DOCS / "project" / name,
            DOCS / name,
        )
        if not any(candidate.is_file() for candidate in candidates):
            findings.append(f"{rel}: indexed file {name} does not exist")
    return findings


def report_exists(number: int) -> bool:
    if not REPORTS.is_dir():
        return False
    for name in (f"{number}", f"{number:03d}"):
        if any(REPORTS.glob(f"{name}-*.md")):
            return True
        if (REPORTS / f"{name}.md").is_file():
            return True
    return False


def check(path: Path, newest: int) -> list[str]:
    findings: list[str] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except UnicodeDecodeError:
        return [f"{path.relative_to(ROOT)}: not valid UTF-8"]

    rel = path.relative_to(ROOT)

    for lineno, raw in enumerate(lines, start=1):
        line = raw.strip()
        if not line:
            continue
        lowered = line.lower()
        exempt = "audit-ok" in lowered
        normalised = re.sub(r"[`*_]", "", line)

        # 2. session totals / ranges
        if not exempt:
            for match in RANGE_SESSION_RE.finditer(normalised):
                if int(match.group(1)) < newest:
                    findings.append(
                        f"{rel}:{lineno}: session range ends at "
                        f"{match.group(1)}, older than session {newest}"
                    )
            for match in RANGE_ABSOLUTE_RE.finditer(normalised):
                end = int(match.group(2))
                if end < newest:
                    findings.append(
                        f"{rel}:{lineno}: absolute range ends at {end}, "
                        f"older than session {newest}"
                    )
            for regex in (COUNT_SPANS_RE, COUNT_SO_FAR_RE):
                for match in regex.finditer(normalised):
                    number = as_number(match.group(1))
                    if number is not None and number < newest:
                        findings.append(
                            f"{rel}:{lineno}: session count "
                            f"'{match.group(1)}' is older than session {newest}"
                        )

        # 3. curated stale phrases
        if not exempt:
            for phrase in STALE_PHRASES:
                if phrase in lowered:
                    findings.append(
                        f"{rel}:{lineno}: stale phrase {phrase!r} "
                        f"(add '<!-- audit-ok: reason -->' if it is history)"
                    )

        # 4. removed tool names
        if not exempt:
            for match in REMOVED_TOOLS_RE.finditer(line):
                findings.append(
                    f"{rel}:{lineno}: removed tool name {match.group(1)!r}"
                )
            if QCFM_COMMAND_RE.search(line) and "qcfm.pas" not in line:
                findings.append(
                    f"{rel}:{lineno}: removed tool name 'qcfm' "
                    f"(containers are 'bbusb.py pack/unpack')"
                )

        # 5. report references
        for match in REPORT_REF_RE.finditer(line):
            number = int(match.group(1))
            if not report_exists(number):
                findings.append(
                    f"{rel}:{lineno}: report {number} does not resolve "
                    f"under docs/reports/"
                )

        # 6. impossible dates (a doc once carried a date a fortnight in the future)
        if not exempt:
            for match in DATE_RE.finditer(line):
                try:
                    found = datetime.date(
                        int(match.group(1)), int(match.group(2)), int(match.group(3))
                    )
                except ValueError:
                    findings.append(f"{rel}:{lineno}: invalid date {match.group(0)!r}")
                    continue
                if found > datetime.date.today():
                    findings.append(
                        f"{rel}:{lineno}: future date {match.group(0)}"
                    )

    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--verbose", action="store_true",
                        help="print each canonical file checked")
    args = parser.parse_args()

    newest = newest_note()
    if newest == 0:
        print("docs_audit: no docs/sessions/session-NN.md notes found", file=sys.stderr)
        return 1

    files = canonical_docs()
    findings: list[str] = []
    for path in files:
        if not path.is_file():
            findings.append(f"{path.relative_to(ROOT)}: missing canonical doc")
            continue
        if args.verbose:
            print(f"checking {path.relative_to(ROOT)}")
        findings.extend(check(path, newest))

    history = contradiction_docs()
    for path in history:
        if args.verbose:
            print(f"checking {path.relative_to(ROOT)} (dates only)")
        findings.extend(check_history_dates(path))
    findings.extend(check_contradiction_index())

    if findings:
        print(f"docs_audit: {len(findings)} finding(s) "
              f"(newest session note: {newest})", file=sys.stderr)
        for item in findings:
            print(item, file=sys.stderr)
        return 1

    print(f"docs_audit: clean ({len(files)} canonical files, "
          f"{len(history)} contradiction files, newest session note: {newest})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
