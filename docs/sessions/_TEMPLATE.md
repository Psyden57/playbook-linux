# Session NN (absolute) — <one line: what this session did>

> Numbering: absolute `NN` (the project's chronological scheme — see
> `NUMBERING.md`).  If this is a Linux-era session, the reports call it
> "session NN-6" — say so here so the cross-references make sense.

Date / span: <YYYY-MM-DD, or the span if it covered several days>
Unit state at the end: <e.g. TabletOS from the sealed IFS, `BOOTCTR_FCTRY = 0x32`>

## Goal of this session

<What you set out to do, and what "done" looked like.>

## Confirmed (measured, read in a file, or seen on the bench)

<Bullet list. Each item: the claim, then the artefact in brackets —
`docs/reports/<file>.md` §x, a register read like
`DSI_COMPLEXIO_CFG1 = 0x2A054321`, a tree path, or "operator saw X".>

## Inferred (reasoning from evidence, not directly measured)

<Bullet list, each with the evidence it rests on and how strong it is.>

## Dead ends and wrong assumptions (valuable — do not skip)

<What you believed, what disproved it, and what the disproving measurement was.
This is the part handoffs lose.>

## Unknown / open questions

<Bullet list. For each: what would resolve it (a measurement, a datasheet, a
relay question to another session).>

## Superseded / uncertain

<Anything your session concluded that a *newer* document contradicts. Say what
you believed, the evidence you had, and that the newer statement is the current
one. Never edit the newer document — this section exists so the history is
visible.>

## Artefacts produced

<Files created or changed: reports, tools, DTS/driver changes, images (with CRC),
scripts. One line each.>
