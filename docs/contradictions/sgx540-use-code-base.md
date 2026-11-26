# SGX540 `USE_CODE_BASE`: present in the DDK header, "absent" per the upstream guide

## The claims

**A. The upstream `powervr_sgx` porting guide says the SGX540 has no
`USE_CODE_BASE` registers.**  `docs/porting/kernel.md`, "What a core needs from
you" → "What is not per-core yet":

> the shader code-base windows (535 and 540 have no `USE_CODE_BASE` registers)

**B. Our vendored GPL DDK 1.17.4948957 defines them for the SGX540.**  In
`sgx540defs.h` (from
`kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz`):

```c
/* Table EUR_CR_USE_CODE_BASE */
#define EUR_CR_USE_CODE_BASE(X)     (0x0A0C + (4 * (X)))
#define EUR_CR_USE_CODE_BASE_ADDR_MASK      0x00FFFFFFU
#define EUR_CR_USE_CODE_BASE_ADDR_SHIFT     0
#define EUR_CR_USE_CODE_BASE_DM_MASK        0x03000000U
#define EUR_CR_USE_CODE_BASE_DM_SHIFT       24
/* Number of entries in table EUR_CR_USE_CODE_BASE */
#define EUR_CR_USE_CODE_BASE_SIZE_UINT32 16
#define EUR_CR_USE_CODE_BASE_NUM_ENTRIES 16
```

That header is SGX540-specific (it ends with `#endif /* _SGX540DEFS_KM_H_ */`),
and the macro is not guarded by an `#if`/revision.

## Evidence for each, and what it establishes

* **A** is the maintainers' statement, written for the open driver's porting
  work; it is a claim about the *hardware* ("no registers"), not about what their
  code currently uses.
* **B** is what a working closed DDK (the one that runs on our unit) exports for
  the SGX540.  It proves the *header* names the register and its fields; it does
  **not** prove the DDK writes them on this part, nor that our rev-120 silicon
  implements them (a header can be a superset covering revisions or the family).

Both are "K/L-class" facts about code, not an **C** (measured-on-our-silicon)
fact.  Neither source overrides the other.

## What is uncertain

* Whether `sgx540defs.h`'s `USE_CODE_BASE` is used by the DDK for the SGX540 at
  rev 120, or is a family-wide leftover.
* Whether the register exists only on some SGX540 revisions (the header has no
  revision gate; the upstream guide may know a specific one).
* Whether the guide means "the registers do not exist" or "the open driver's
  code-base windows are not implemented for 535/540" (its phrasing is the
  former, but the surrounding text is about driver work).

## What would settle it

One read-only-then-write test on our unit with the vendor DDK clocking the GPU
(`pvrsrvkm 56000000.gpu`): write a distinctive value to `0x0A0C + 4n` and read it
back, for `n` in a couple of entries, then restore.  If it sticks, the SGX540 has
the register (then a USSE task using a code base confirms it works); if it reads
back unchanged/zero, the guide is right for this part.  This is one of the
§Gaps items in `docs/reference/sgx540-factsheet.md` (#7).  A question to the
upstream maintainers ("which SGX540 revision did you find without
`USE_CODE_BASE`?") would settle the intended reading.

Do not run it as part of a blind register sweep; name the register and bound the
test.

## Why it matters

If the registers exist, the SGX540 code-base window is the SGX545's
`USE_CODE_BASE` path and one piece of "not per-core yet" work disappears; if
they do not, the port needs the 535-style code-base path.  Until measured,
`docs/reference/sgx540-factsheet.md` and report 276 assume the **535-style**
path (the conservative reading) and mark it a gap.

**Status: open** (session 72; not settled — needs a hardware read).
