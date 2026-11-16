# The DEBUG_LL ring after a QNX boot — `0xAA` fill vs earlier successful reads

Status: **unresolved**.  Neither claim is withdrawn; the measurements disagree and
the difference is not understood.

## Claim A (this project's history, and the operator)

The `0x88000000` DEBUG_LL ring can be read **from the reference OS** after a
failed Linux boot: earlier sessions read rings "full of console text" through the
QNX door (`tools/pb_ring.py` drives `/accounts/devuser/memdump3` over that door)
days *before* the pruned/minimal RCFS existed, i.e. with the stock 1917 RCFS on
media.  Evidence: `docs/captures/playbook-live-spare-2026-10-26-clean-ring-88000000.txt`
— 32 KiB captured at `0x88000000` whose first words are the loader header
`4c424b43`, `00000003`, `4b45524e` followed by real content (e.g. `edfe0dd0`, the
DTB magic, at `+0x54`), not fill.  Also `…-clean-full-boot-from-printk-ring.txt`.

## Claim B (measured, session 61)

After booting the oracle and reading the same addresses from `qconn`
(`memdump3`, `tools/bench/ringdump.c`), `0x88000000`, `0x90000000` **and** the
pbmon shadow `0x90200000` read as `0xAA` fill with **no printable run in 64 KiB**,
while the IFS load address `0x80000000` still holds real data (`0e40009c`).  So
procnto initialises the RAM it considers *free*, and the ring pages are free RAM
to it.  Report 254 §2, capture
`docs/captures/oracle-live-2026-11-16-ring-and-cpu.txt` §1.

## What is uncertain

* **Which QNX image** the earlier successful reads used (stock full RCFS before
  the session-43 prune, per the operator) and whether that image's startup
  allocated or filled the ring pages at all.
* **Which boot's content** those captures held — the ring is a persistent DRAM
  buffer, so a capture can show a *much older* boot's text even if the engine
  that produced it would fill the page on the next boot.
* Whether the `0xAA` is written by `startup`, by `procnto`, or by the loader's
  own clearing, and at which point in the boot.

## What would settle it

1. Boot the **stock full 1917 RCFS** (not the pruned one) with a UFS in place and
   read `0x88000000` from `qconn` at the earliest possible moment — if it has
   content, Claim A holds for that image and the prune/service set is the
   variable.
2. Boot the oracle twice in a row and compare the same pages each time: if the
   second boot's page still holds the first boot's text, nothing fills it.
3. Find the writer: search the reference's `startup`/`procnto` for a `0xAA`
   fill (RE, `tools/` + the QNX toolchain), or watch the page across a boot.
