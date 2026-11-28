# `st.a.d` immediate offset: 1-based (ISA.md) vs 0-based (the working TICKLOG store)

Raised session 84 (2026-11-28).  Status: **open** — the two readings cannot both
be right, and a ctlblk store instrument now depends on which is true.

## The two claims

1. **`ISA.md:1484-1486` says the immediate offset is 1-BASED.**  Quoting the
   document: "`ld.a.d` and `st.a.d` with an immediate offset `#i` access the dword
   at base + 4·(i + 1).  `st.a.d sa0, #0` wrote word 1 of the buffer whose address
   was in sa0, `#2` wrote word 3, and `ld.a.d #2` read word 3 back."  That section
   is tagged hardware (`tools/iso` `sanity_st` and `p2_ld_ukcode`, 2026-11-02), so
   it rests on a measurement on this silicon.

2. **The shipped UK_TICKLOG marker writes `st.a.d r17, #0x37` and lands
   `0x22222222` at word 0x37 — not word 0x38.**  `ukernel/ukernel.py`:

   ```
   a.i("limm r4, #0x22222222")
   a.i(f"st.a.d r17, #{0x37}, r4 [moe]")      # marker: reached the tick site
   ```

   and on the unit, `word 0x37 = 0x22222222` in the ctlblk dump
   (`/sys/kernel/debug/dri/129/ctlblk`, `ta3dctl @ 0x0f003000`), where the dump
   rows are 8 words from word 0).  If the offset were 1-based this value would be
   at word 0x38.

## Why it matters

Session 84 built a ctlblk store instrument (`UK_ADDRLOG`, `UK_STAGELOG`) to trace
what the TA actually programs, and **every version read 0 at words 0x3d/0x3e —
including a positive control stamped at the `kick` entry, which demonstrably
executes**.  So something about the store form, not the microkernel, is wrong, and
the offset convention is the leading suspect (trap 442).  A second ctlblk
instrument cannot be trusted until this is settled.

Note that hypothesis 2 was tried in session 84 — using `#(W−1)` for word W — and it
*also* read 0, which is why the contradiction is unresolved rather than simply
"ISA.md is right".  Either both readings are wrong, or the failure is elsewhere
(e.g. word 0x3e specifically is special, or the store is being suppressed) and the
offset question is a red herring.

## What is uncertain

* Whether the `(i+1)` rule in `ISA.md` applies to the `sa`-relative form (base in a
  secondary attribute, e.g. `sa0`) but **not** to the register form
  (`st.a.d r17, #i`).  The ISA note's evidence is described in terms of `sa0` and a
  buffer, and the `#0`/`#2` examples are all `sa`/plain-buffer forms.
* Whether the offset field's encoding differs when the base is `r17` (the ctlblk
  self-pointer patched into the image) versus an ordinary register.
* Whether word 0x3e is writable at all — nothing in the vendor headers or the port
  says so either way, and it is the word session 84 chose.

## What would settle it

One boot, no render, no fault.  In a gated build, copy the **proven** TICKLOG store
verbatim and add a second store of a distinct constant to a different word:

```
limm r4, #0x22222222
st.a.d r17, #0x37, r4 [moe]      # known-good control
limm r4, #0x12345678
st.a.d r17, #0x3e, r4 [moe]      # the question
```

then read words 0x37, 0x3d, 0x3e and 0x3f by index:

* **0x37 = `0x22222222` and 0x3e = `0x12345678`** → `#N` addresses word N (claim 2);
  `ISA.md`'s `(i+1)` is specific to the `sa`/buffer form, and session 84's failure
  lies elsewhere.
* **0x37 = `0x22222222` and 0x3f = `0x12345678`** → 1-based (claim 1); the TICKLOG
  comment's `#0x37`→word 0x37 is a coincidence that needs explaining.
* **0x37 correct and 0x3e/0x3f both unchanged** → the offset is not the problem;
  word 0x3e (or that store site) is suppressed, and a different free word must be
  used.

## Provenance

`local/work/powervr_sgx/docs/ISA.md:1484-1486` (the hardware-tagged offset note);
`ukernel/ukernel.py` (the UK_TICKLOG site and the `stage()` helper);
`/sys/kernel/debug/dri/129/ctlblk` (the live `0x37 = 0x22222222` reading, session
84); trap 442; `docs/sessions/session-84.md` (twelfth part).
