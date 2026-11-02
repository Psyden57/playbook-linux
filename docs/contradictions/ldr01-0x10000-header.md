# LDR_01: is there a `0x10000`-byte header?

Status: **resolved — there is no header.**  `file_offset = VA - 0x80100000`.

## The two claims

**Claim A — there is a `0x10000`-byte header.**
`docs/sessions/session-22.md` ("Confirmed", and again in "Dead ends and wrong
assumptions") says:

> The LDR_01 image has a `0x10000` header, so
> `file_offset = (VA - 0x80100000) + 0x10000`.

That note also records the consequence it drew: reading LDR_01 at
`VA − 0x80100000` put the reader in string data, which made the session doubt a
subagent claim.

**Claim B — there is no header.**
Report `164-ldr01-opcode-map.md` §"Addendum (2026-11-01, main session)" states:

> **There is no header — the mapping is simply `file_offset = VA - 0x80100000`.**
> (This paragraph first claimed a `0x10000`-byte header; report 165 checked it and
> the claim was wrong, so it is corrected here rather than deleted.)  The image's
> first bytes are `02 00 00 ea` (the ARM boot vector) followed by the `0xD7D32D1F`
> tag, and the image's own load address `0x80100000` sits at **`+0x0C`**.

The session-23 handoff's "CORRECTIONS since the previous handoff (all committed,
do not re-derive)" repeats Claim B.  The `0x3F18` / `0x80103F18` correspondence
in report 164 was verified independently in the same session with
`r2 -m 0x80100000 -a arm -b 32`.

## Why this is recorded rather than silently fixed

Session notes are evidence of what that session believed; they are not rewritten
by later sessions (`docs/sessions/README.md`).  Session 22's note is therefore
left as written, including the header claim and the dead end it caused.  The
canonical reading is Claim B: report 164 is the artefact that re-checked it, and
report 164's own address table is correct as written.

## What would settle it (and did)

The image's first 16 bytes: `02 00 00 ea` is the ARM branch, and the load address
`0x80100000` appears at `+0x0C`, not after a `0x10000` pad.  A file-offset read
of the `movw r0,0x2074` pair lands at `0x3F18` = VA `0x80103F18` under the
no-header rule.

## See also

* `docs/reports/164-ldr01-opcode-map.md` §"Addendum (2026-11-01, main session)".
* `docs/sessions/session-22.md` — the superseded belief, kept as evidence.
