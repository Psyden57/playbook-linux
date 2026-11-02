# 87 — Next-session subagent workstreams (copy-paste prompts)

Purpose: the next session should launch independent read-only/static tracks as
subagents in parallel, and keep the **single live device and all writes** in
the main session. This is the efficiency plan for `85`.

## Rules for every subagent

- Read-only against the live unit. No NVRAM/RPMB/flash writes, no `nvram
  patch`, no bootrom/loader uploads, no reboots. Only the main session touches
  the connected 64 GB unit or the spare 64 GB test bed.
- Use the session scratchpad for intermediate files; write the final report to
  the exact path given (distinct per agent).
- Cite exact addresses/files; do not modify the target binaries or `.pyc`.
- If a step needs the device, stop and hand it back to the main session.

Suggested launch: A, B, C in one turn (parallel); D and E while waiting.
Main session: collate reports, run the live read-only audit, then execute
Phase 1/2 only after reading A–C.

## A — Cellebrite / ramloader audit (highest priority)

Targets:

- `cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A` — signed
  PlayBook loader, 159,968 B, `D7D32D1F` at +4, load `0x80100000`, tail
  `D7C82D1F`.
- `cellebrite-blackberry/ufedblackberrypack.epr/05001A06_0000000A` — Qualcomm
  loader, load `0x02000008`.
- `cellebrite-blackberry/BlackBerry OS cellebrite exploit - wumbomumbo.txt`
  and `messages.txt`.
- `bbusb/loaders/loader_06001A06-00.bin` (`LDR_01`), `analysis/66-*`,
  `analysis/72-*`, `analysis/86-cellebrite-loader-lead.md`.

Goal: decide whether `06001A06` exposes arbitrary eMMC read/write, and produce
a test plan for the bricked 32 GB units.

- Disassemble at base `0x80100000`; compare dispatcher and opcode map to
  `LDR_01`.
- Audit `0xE5`/`0x39`/`0x3A` CREAD, `0xF7`/`0xF8` write, `0xEE` erase,
  `0xC4` READVERIFY, `0xE0` installer, `0xC0` COMPLETE for address/length/
  offset/sector-index checks.
- Check whether the Cellebrite patch pattern (`6A0000EA` -> `DB9F07EB`) or the
  BBOS three-address `WRITE_RAM_SETUP` weakness has any BB10 analogue.
- Inspect `ufedblackberrypack.epr/BB.bin` and other pack entries for
  loaders/payloads relevant to PlayBook/QNX.

Report: `analysis/88-cellebrite-ramloader-audit.md`.

## B — IFS / region / CRC recipe

Inputs: `analysis/playbook-live-2026-10-21/`, `83`, `85`,
`deckard-2.1.0.1917/platform/winchester.py`.

- Parse the QNX IFS header at `hd1+0x100000` (load `0x800FF800`, entry
  `0x803BF610`, cookie `0xD7A82D1F`) and find the image size/end.
- Confirm `region[0xd4]` (live 0), the ECDSA trailer at `end-0x230`, and the
  CRC trailer at `end-4`.
- Write a host-side CRC-32/ISO-HDLC builder/verifier matching
  `FUN_80014788` (`init 0xFFFFFFFF`, reflected poly `0xEDB88320`, final XOR).
- Produce exact steps to make a CRC-valid copy with `+0xd4=6`, and to verify
  both A/B regions.

Report: `analysis/89-ifs-crc-recipe.md`.

## C — A/B switch dry run

Inputs: `deckard-2.1.0.1917/platform/osregion.py`, `base.py`,
`winchester.py`, `83`, `85`.

- Produce exact `Platform.switch()`/`sane()` sequences and the equivalent
  `nvram patch 0x2074` byte writes for: requested=1, actual=0,
  `boot_count_0/1=0`, `max_boot_count=0`, `epoch=now`.
- Read-back/restore sequence and the expected fallback to region 0.
- Backup checklist and failure/recovery path (bootrom/loader).
- No device writes.

Report: `analysis/90-ab-switch-runbook.md`.

## D — Linux entry state / payload wrapper

Inputs: `85`, `70`, `71`, `hd0head.bin`, the IFS header dump.

- Reverse the IFS entry (`0x803BF610`) and the `0x80014A90` enter-OS path.
- Document ARM state, MMU/caches, memory map, and what a minimal Linux
  chainloader must set up before jumping to the kernel.
- Identify the smallest IFS-compatible payload swap that keeps `+0xd4=6` and a
  valid CRC trailer.

Report: `analysis/91-linux-entry-state.md`.

## E — RPMB factory write runbook (static, read-only)

Inputs: `69`, `71`, `77`, `78`, `rpmb-omap4`, `nvram-editor` notes.

- Document the exact `/dev/rpmb` `BOOTCTR_FCTRY` read/write/verify sequence and
  the authenticated-record requirements.
- Risk checklist: backup, both A/B regions CRC-valid, recovery path, the
  working 64 GB reference vs the spare 64 GB test bed.
- No writes; the main session executes Phase 4 only after A–C are reviewed.

Report: `analysis/92-rpmb-factory-write-runbook.md`.
