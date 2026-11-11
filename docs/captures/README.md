# Captures — live text evidence

These are raw captures taken from the unit during bench sessions: register
dumps, DRAM ring buffers, NV/RPMB reads and console transcripts.  They are
**evidence**: do not edit them.  When a report quotes a register value, check it
here before changing the report.

Naming is `playbook-live[-spare]-<date>-<topic>.txt` (the `spare` captures are
from the unit that is now down).  `docs/reports/data/` holds the binary captures
and the larger dumps.

## Exact duplicates

These pairs are byte-identical; keep one of each when pruning.

| file | duplicate of |
|---|---|
| `playbook-live-spare-2026-10-26-clean-mmc2-noidle-ring2.txt` | `…-clean-mmc2-noidle-ring.txt` |
| `docs/reports/data/live-info-2026-11-01/dsp_os_metrics.bin` | `…/installer_metrics.bin` (the DSP metrics read is a stub; both are zero) |
| `playbook-live-2026-10-21-after-0x2004.txt` | `playbook-live-2026-10-21-nv-0x2004.txt` |
| `playbook-live-2026-10-21-after-0x2040.txt` | `playbook-live-2026-10-21-nv-0x2040.txt` |
| `playbook-live-2026-10-21-after-0x2801.txt` | `playbook-live-2026-10-21-nv-0x2801.txt` |
| `playbook-live-2026-10-21-after-0x2802.txt` | `playbook-live-2026-10-21-nv-0x2802.txt` |

## Near-duplicates

These are distinct captures but differ only in one or a few bytes; keep the
pair only when a report needs a before/after comparison.

* `after-0x2074` vs `nv-0x2074`: 1 byte.
* The 13 `…-ring.txt` files of 648 bytes are distinct captures that differ by
  up to 4 bytes (the ring header/count).
* The 19 `…-ring.txt` files of 32,549 bytes are **distinct** captures from
  different experiments, not copies.
* The 8 `…-ring-*.txt` files of 6,080 bytes are the four breadcrumb rings from
  two sessions (distinct contents).

## What proves what

| capture group | used by |
|---|---|
| `playbook-live-2026-10-20-*` | boot ROM / IRAM mapping (reports 65, 152) |
| `playbook-live-2026-10-21-*` | NVRAM records `0x2002`/`0x2004`/`0x2040`/`0x2074`/`0x2801`/`0x2802` (reports 74–79) |
| `playbook-live-2026-10-25-*` | spare-unit RPMB/NVRAM read-only sweep and the `TEST_PAD` rehearsal (reports 92, 95) |
| `playbook-live-spare-2026-10-26-clean-*` | the clean-kernel boot captures, ring buffers and per-experiment event logs (reports 103–109) |
| `playbook-live-spare-2026-10-27-*` | live QNX display/register captures (reports 118, 121) |
| `twl6030-{linux,qnx}-2026-11-08.txt` | the both-OS TWL comparison (report 192) |

`docs/reports/data/` is indexed by the reports that produced it; the report
INDEX (`docs/reports/INDEX.md`) says which report is current for a topic.
