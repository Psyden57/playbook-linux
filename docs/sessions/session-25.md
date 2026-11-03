# Session 25 (absolute) — BB10 target fixed to a Z10 OMAP build; read-only prep

> Numbering: absolute `25`; the reports call this Linux-era **session 19**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-03
Unit state at the end: **unchanged — PlayBook OS 2.1.0.1917 running from our
IFS (CRC `0x38EE7B46`), our edited `/base`, and the vendor `.5.ufs` with the
operator's `autorun::0` change.**  `BOOTCTR_FCTRY = 0x32` (topped up by the
operator's `/accounts/devuser/startup.sh`).  **No USB/device access and no
write of any kind this session.**  See `STATE.md`.

## Goal of this session

Start `ROADMAP.md` item 8 / `BB10-PORT.md`: boot BB10 on the PlayBook.  The
first task is the operator's, not ours — **name the target image/version and its
path** — because the docs forbid assuming any BB10 property.

**Result: the target was named ("a Z10 OMAP build") but the image is not in
hand**, so the session did read-only preparation only: verify the environment
and tool surface, correct a device/SoC claim that would otherwise steer the
whole port wrong, and fix the intake procedure so the next step needs no
re-derivation.

## Confirmed (measured, read in a file, or seen on the bench)

* **No BB10 image exists anywhere in this workspace.**  `find local -iname
  '*bb10*'` returns only `local/bb10mt/` and the vendor tool copies; `local/images/`
  is entirely PlayBook 4869/1917 material.  Nothing to copy, parse or hash.
* **The toolchains here are QNX 6.6 + ARM Buildroot only** — `local/toolchains/`
  is `armv7-eabihf` and `qnx660-master`.  `. tools/env.sh` resolves
  `dumpifs`, `mkifs`, `mkrcfs`, `ntoarmv7-gcc` under
  `local/toolchains/qnx660-master/host/linux/x86/usr/bin/`.  There is no QNX 8
  anything on disk.
* **`mkrcfs` is shipped twice and the copies are byte-identical**:
  `local/vendor/bb10mt/mkrcfs` and the QNX 6.6 SDP's
  `host/linux/x86/usr/bin/mkrcfs` both hash
  `410a04a576b54785bfca7c17ac07b6f2e8dc8762223ab62da62bee6d16c7fcd2`.
  It is an unstripped 32-bit i386 ELF (`mkrcfs.c`, debug info, `lzo1x_999` +
  SHA-1/256/384/512).  Its CLI is `mkrcfs [options] <input-dir> <output-file>`
  and `options`' getopt string is `b:c:lp:t:vxhL:D:`.  A scratch run
  (`/tmp/rcfs-test/in` with one file) produced a 20,480-byte image; the
  superblock strings are at image offset **`0x1000`**
  (`-- RCFS Super Block @ (000001000)`, `-- RCFS Inode Table @ (%08x)`).
  **Not used as a format spec** (see *Dead ends*).
* **`bb10mt` 0.5.9.999 has the container surface the intake needs**:
  `autoloader extract` / `autoloader loaders` / `autoloader create`,
  `split` (`-i`), `unpack -c <container>`, `raw dump` (raw flash image →
  partitions), `raw nvram`, `bar`, `qnx6 …`, and `rcfs extract -i IMAGE -o DIR`.
  All present; **none of them has been run against a BB10 image yet**.
* **`bbusb.py` still has `parse`/`split`/`unpack`** and remains the
  hardware-proven path for PlayBook-style `.signed` containers (reports
  169/170).  Whether a Z10 autoloader needs it at all is unknown.
* **`bb10mt rcfs extract` is not a drop-in reader for the PlayBook `/base`.**
  Run on a copy of our known-good 1917 RCFS (`local/work/rcfs-1917.rcfs`) with
  output to the scratch dir, it printed a stream path then
  `Error executing command: Stream read error`, exit 1, and extracted nothing.
  That is expected (the PlayBook `/base` is a plain QNX6 filesystem), and it
  means the tool expects a different container — **it does not establish
  anything about the BB10 `/base`**, which has not been tested.
* **`bb10mt.ini` maps a 32-bit device ID to a RAM loader.**  The PlayBook's
  `0x06001A06` maps to **`LDR_01`**, which matches the project's own loader
  facts (`LDR_01`, 238,128 B, uploaded over USB and never on media).  The Z10's
  ID is not identified here, so which loader `bb10mt` would pick for a Z10 is
  unknown — relevant only if a future step flashes over USB rather than
  through the PlayBook's pre-OS `0xF7` loop.

## Inferred (reasoning from evidence, not directly measured)

* If the Z10 image is a vendor **autoloader** (CAP/CFP/`.exe`), `bb10mt
  autoloader extract`/`split` is the likely first tool; if it is a **raw eMMC
  dump**, `bb10mt raw dump` is.  This is inference from the subcommand names and
  their help text, not from a tested image.
* `bb10mt rcfs extract` "for an rcfs image" may be the correct reader for a BB10
  `/base`, since the PlayBook RCFS needed `qnx6 mount` instead.  Untested; the
  distinction is exactly the open question (`BB10-PORT.md` §3 question 2).
* The QNX 8 `mkrcfs` is the only credible way to *repack* a BB10 `/base`
  honestly, so the port is gated on obtaining it even if the reader works.

## Dead ends and wrong assumptions (valuable — do not skip)

* **I started treating the QNX 6.6 `mkrcfs` as the RCFS spec.**  I recovered
  its superblock location (`0x1000`), its CLI/optstring and its symbol-level
  structure and was decoding struct offsets.  **The operator stopped this**:
  the 6.6 tool is not to be trusted and nothing may be based on it pending the
  QNX 8.0.0 `mkrcfs`.  No format claim from that work is kept as fact; the only
  surviving facts are the *tool's existence/location*, its identical hash, and
  that it builds an image.  This is the session's main course correction.
* **"Z10 + OMAP4460" looked like a single target.**  It is not: the OMAP Z10 SKU
  is STL100-1 and it is **OMAP4470**; OMAP4460 is the **Dev Alpha A** (and the
  PlayBook 4G LTE).  Had I accepted the phrase, I would have looked for an image
  that no retail Z10 ships.  Caught by checking the model tables before
  accepting the operator's shorthand as a device fact.

## Unknown / open questions

* **Which device + BB10 version + path?**  The operator said "a Z10 OMAP build";
  the SKU (STL100-1 vs a Snapdragon Z10) and the version are still unnamed, and
  the file is not present.  Resolve by the operator supplying the path, or by
  naming the exact build.
* **Is the OMAP4470 (Z10) image even the right analogue for an OMAP4430
  PlayBook?**  "Compatible" is the operator's claim; SGX544 vs SGX540 and a
  different clock tree argue it needs verification, not assumption.
* **Does the BB10 `/base` RCFS match any tool here?**  Unresolved until the QNX 8
  `mkrcfs` and an image exist.
* **Does the PlayBook loader accept a BB10 image at all** (`os_type`, CRC,
  `LoadEnd`, length, MCT extents)?  The operator's 10.0.9.1103 report is the
  only evidence; `BB10-PORT.md` §3 question 5.
* **QNX 8.0.0 SDP availability** — the operator is locating it.

## Superseded / uncertain

* **"BB10 reports as QNX 8"** — operator-supplied, still unverified; no BB10
  binary has been read.
* **"The Z10 is OMAP-based … OMAP4460"** (`BB10-PORT.md` §1, operator) —
  correct in spirit (one Z10 SKU is TI OMAP) but the part is **OMAP4470**
  (STL100-1); OMAP4460 is the Dev Alpha A.  Web sources, not measured here:
  Wikipedia's Z10/Q10/OMAP/PlayBook tables and the Dev Alpha A/B spec
  comparison (e.g. <https://en.wikipedia.org/wiki/BlackBerry_Z10>,
  <https://en.wikipedia.org/wiki/Texas_Instruments_OMAP>,
  <https://en.wikipedia.org/wiki/BlackBerry_PlayBook>).  Recorded in
  `BB10-PORT.md` §1 as a check with sources; the operator's wording is left
  visible.
* **"Early BB10 10.0.9.1103 already runs on the PlayBook"** — operator-supplied,
  not reproduced; still the strongest existence proof for the whole goal.

## Artefacts produced

* `docs/project/BB10-PORT.md` — §1 SoC check (Z10 SKU/part correction, sources);
  §3 questions 1/2/4 updated; new **§4a** (session-25 read-only preparation: the
  confirmed tool surface, the `mkrcfs` caution, and the fixed intake procedure).
* `docs/project/ROADMAP.md` — item 8 rewritten around the named target, the
  OMAP4470/SKU ambiguity, and the two outstanding inputs.
* `docs/project/STATE.md` — the BB10 next-goal paragraph, the "active work"
  paragraph and "What is next" updated; the unit/media state is untouched.
* `docs/sessions/session-25.md` — this note.
* Nothing else.  **No write to the unit, no flash, no RPMB, no media change.**
