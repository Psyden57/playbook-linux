# Session 22 (absolute) — tooling: a verified QCFM/flash/record toolchain, and static RE of CFP.exe + LDR_01

> Numbering: absolute `22`.  The reports also call this **Linux-era session 16**
> (absolute = Linux-era + 6).  21 absolute sessions existed before this one.

Date / span: 2026-11-01
Unit state at the end: **armed and sealed.**  The reference (`0x50175127`) is in
Boot Mode `Factory` with the counter around `50` (it rises on an OS boot — the devuser
top-up), and **the operator flashed the sealed `os_type 6` IFS during the session so the
unit would boot**; the pre-flash stock `os_type 4` state is what session 21 had
measured.  Which sealed asset is on media has not been read back yet.  **This session wrote nothing to the device and did not touch
USB.**  The spare is still down (`0x5B`, report 153).

## Goal of this session

It began as an audit: the operator wanted an exact walkthrough of the `bb10mt`
commands the previous session had used, because the arming tutorial was unclear.
That exposed several wrong claims in my own work, which snowballed into:

1. Correct the `bb10mt` story from its source rather than from guesses.
2. Build the missing pieces in *our* tooling — flashing, `info`, and the
   Windows-only `resetqnxosregion` recovery — so the project stops depending on
   bb10mt and CFP.
3. Use static RE of `CFP.exe` and the LDR_01 ramloader as the authority for
   anything the source did not settle.

## Confirmed (measured, read in a file, or seen on the bench)

* **`bb10mt pack` writes a `.signed` container, not an `.exe`**; `.exe` autoloaders
  come from `autoloader create`.  `-v 1,2` is **required** by this loader
  (`-v 2` is rejected `0x11`, report 139) — operator correction, then re-measured.
* **`split` is for `.exe` autoloaders and `unpack` is for `.signed`** — operator
  correction; confirmed by the source (`uautoloader.pas` vs `qcfm.pas`).
* **QCFM record semantics**, verified three ways (`qcfm.pas:559`/`:582`, the vendor
  `2.1.0.1917.signed`, and fresh `bb10mt pack` runs): the **V1 `qcfp`** record
  carries `Delta + BlockIndex`, the **V2 `rrcq`** record carries the plain block
  index.  Vendor deltas: MBR `0`/`8192`, RCFS `192`, sig `15`, IFS `16`, UFS `16640`.
* **`bbusb.py pack|unpack|split` reproduces bb10mt byte for byte** on real files: `build` (0
  differing bytes at `Delta` 16 and 192), `split` (`34f53ae9…`), `extract`
  (`1884be93…`), and `extract --only 0,3,4` of the 1.1 GB vendor container
  (`.mbr`/`.sig`/`.ifs` all identical, same `.lst` deltas).
* **`RESETQNXOSREGION` = loader op `0xE8 {u32 4}` = delete NVRAM record `0x2074`**,
  from two independent binaries: CFP's handler `0x404908` (report 163) and
  LDR_01 `0x80103F18 movw r0,0x2074 / bl 0x8010b8bc` (report 164), verified by me
  in `r2` at instruction level.
* **`0x2074` is one 24-byte record doing three jobs**: flash `COMPLETE` (`0xC0`)
  writes it, `0xE9` reads it (CFP mislabels that read "boot count info"), and
  `0xE8/4` deletes it.
* **CFP's eight unidentified `info` reads** (report 162), each measurable at a named
  address and matched by LDR_01's dispatcher at `0x8011737C`: `0xBD/0xBE` HW
  override, `0xBF/0xD9` DRAM, `0xE1/0xC8` installer metrics, `0xB2/0xC8` DSP
  metrics, `0xD8/0xC8` OS metrics, `0xE9/0xFB` record, `0xDB/0xCB` vendor ID,
  `0xB5/0xD3` CSD/ExtCSD.  **`0xB2` is a stub** in LDR_01 — always zeros.
* **The factory counter**: reads over 2026-11-01 were `50` → `49` → `47` → `49`.  The
  falls were boots into the IFS (operator: splash screen, then power off); loader/
  `info` sessions do not consume it.  The rises are the **OS top-up** (`devuser/startup.sh` refills toward `0x32`), which
  explains `47 → 49 → 50`; inside a loader session the counter does not move.
* **A rejected password leaves the boot rom silent**, not re-challenging — one wrong
  entry on 2026-11-01 made every later run fail without a prompt until the unit booted
  the OS.  Our tool used to retry the whole handshake (with a reboot) on that failure;
  it now aborts, sends a supplied password once, and re-prompts only on a real
  challenge.
* **The `0x39` is not an MCT partition type — it is the CREAD opcode.** Partition
  resolution uses a fixed four-entry table at `0x80136998`
  (`{0x31,0x2B,0x2C,0xFF}`), so **path 2 cannot resolve** and path 1 (a V1
  `qcfp` offset) is the only write path.
* **`0xDE` with `action != 0` never answers**: `0x80103E60` logs and returns via
  `0x80103F48` with no frame, behind a model-id gate.
* **CFP is the only correct `info`** for this unit; `bb10mt info` is mostly wrong
  (operator).  CFP's baseline is preserved in `docs/reports/data/cfp-2026-11-01/`.
* **bb10mt's `-s true` is a deliberate *dummy* signature** (8 bytes, not 560) and
  is what you use when flashing a modified UFS after skipping the `.sig`
  component (operator).  The real 560-byte `QNXH` trailer belongs to the unmodified
  image.
* **The LDR_01 image has a `0x10000` header**, so
  `file_offset = (VA - 0x80100000) + 0x10000` (report 164 addendum).

## Inferred (reasoning from evidence, not directly measured)

* That `0x2074` *is* the "region dirty" marker the QNX side sets.  CFP's naming and
  the delete path imply it; a QNX-side write site has not been matched to that
  record id.
* Why the device accepts a dummy signature.  Strong reading: the real trailer
  describes the unmodified image, so a changed UFS cannot carry it — but the
  mechanism is not established.
* That `0x2E` on the `0xE8` delete means failure.  Report 164 records `0x2E` on
  both the success and the failure path, so the status alone is not decisive; the
  read-back is.  This is why `bbusb.py reset-osregion` verifies rather than trusts.
* That the DTS/reports' "boot count info" label is CFP's, not the record's real
  meaning.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`bb10mt pack` produces an `.exe`" and "`bb10mt split` handles `.signed`"** —
  both wrong; the operator corrected them and the source confirmed the correction.
* **"bb10mt drops `Delta` for `.ifs`, so an IFS container would write at `0x815`"**
  — **my error**, and I had turned it into a safety warning.  The second container
  in that test had been packed from a `.lst` that `bb10mt unpack` had **rewritten**,
  so I was not packing the file I thought.  A byte-exact comparison against
  `qcfm.pas` refuted it.  Retracted in the contradiction file, which keeps the bad
  table so the retraction is auditable.
* **"`device_pin()`'s `0x02FD8C17` is exactly CFP's `50175127`"** — wrong; CFP's PIN
  is `0x02FD9C97`.  The relation is now recorded as *unreconciled* rather than
  asserted.
* **"Path 2 must resolve, because the operator has flashed firmware with bb10mt"** —
  wrong reasoning; the container addresses path 1, so the operator's history never
  required path 2.  Report 164 closed it: `0x39` is CREAD.
* **Reading LDR_01 by file offset = VA − base** — wrong because of the `0x10000`
  header; it put me in string data and made me doubt a subagent claim that was
  actually correct.
* **`bb10mt unpack` exits 192 while writing correct files** — its exit codes are not
  a success signal.  (Same class of bug as the earlier `rc=0`-from-`tail` mistake.)
* **`0xDE` write actions**: `bbusb.set_boot_mode()`/`set_boot_count()` look like dead
  code.  No doc relied on them, but they are traps.

## Unknown / open questions

Carried in HANDOFF §"CARRY-FORWARD open questions": `0xB5`'s CSD/ExtCSD split,
`0xDC`'s hash, CFP's USB write layer, `0x20EE`'s unexplained fields, the PIN
encoding, and whether `0x2074` is really the QNX "region dirty" marker.

Also open:
* The QNX6 host tools (`qnx6 mount/mkfs/compact/fsck`, the in-place shrink) are
  still bb10mt-only — the last real dependency.
* The repository licence, now with an explicit GPL-3 addendum in `DECISIONS.md`:
  the container code in `tools/bbusb.py` is a transcription of GPL-3 bb10mt code and embeds its 560-byte
  constant.
* Nothing in this session's new code has run against a device.

## Superseded / uncertain

* **Report 98 §9.3/§9.6's "drops `Delta`"** — corrected inline in that report: it
  applies to the V2 record only, where it is intentional.
* **My earlier statements** about `-s true`, the `0x815` hazard, the PIN, and
  "path 2 must resolve" are all superseded by the measurements above; the
  contradiction file and the reports carry the corrections and the retraction.
* **`bb10mt info`** must not be cited as evidence for this unit again (operator:
  it is mostly wrong).

## Artefacts produced

Commits (`af41d60..45b916c`): `62fa41b`, `6ab11e9`, `ac5de09`, `ed3468e`,
`7d8ef6b`, `b4bc44b`, `abf986e`, `6fc6ff9`, `45b916c`.

Reports: **161** (CFP `info`/`flashinfo` baseline — armed state, region 0 = 4869,
WP table, ExtCSD), **162** (CFP `info` opcodes + metrics blob offsets), **163**
(`RESETQNXOSREGION` dispatch and frame), **164** (LDR_01 opcode map: 48 accepted
opcodes, NVRAM API, the `0x39` answer, plus my addenda on the `0x10000` shift and
`0xDE`).  Data: `docs/reports/data/cfp-2026-11-01/`.

**One tool**: `tools/bbusb.py`, CFP-style — `info` (read-only, all eight CFP info
reads), `flash` (container or bare component, dry run by default), `reset-osregion`
(the OSREGION delete with a read-back), `boot-mode`, `reset-loader`, and
`pack`/`unpack`/`split`/`parse` (byte-identical to bb10mt), plus
`tools/tests/test_offline.py`.  Also **moved
`bbusb.py` out of `local/vendor/` into `tools/`** (operator's ruling: it is our code,
not vendor material); the ramloader blob it uploads is RIM material and moved to
`local/loaders/`, with `bbusb.DEFAULT_LOADERS` resolving it.  That makes it a second
GPL-3-derived tracked file, which is recorded in `DECISIONS.md`.

Docs: `DEVELOPMENT.md` §9-§14 (bb10mt facts, qcfm, bbflash, pbreset, tests),
`STATE.md`, `HANDOFF.md` (recovery path + carry-forward questions),
`KNOWN-ISSUES.md` trap 69, `DECISIONS.md` licence addendum,
`contradictions/100-…` (resolved, with the retraction kept) and its index.
