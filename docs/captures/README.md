# Captures — live text evidence

These are raw captures taken from the unit during bench sessions: register
dumps, DRAM ring buffers, NV/RPMB reads and console transcripts.  They are
**evidence**: do not edit them.  When a report quotes a register value, check it
here before changing the report.

Naming is `playbook-live[-spare]-<date>-<topic>.txt` (the `spare` captures are
from the unit that is now down).  `docs/reports/data/` holds the binary captures
and the larger dumps.

## WM8994 reference codec, full register file (session 41)

Two complete 1905-register dumps of the **working** WM8994 on the reference
(2.1.0.1917, `io-audio` running), read with `tools/pbi2c dump16` over the door.
**Both were taken during playback.**

| file | when |
|---|---|
| `wm8994-reference-live-2026-11-11.txt` | playback |
| `wm8994-reference-playing-2026-11-11.txt` | playback, moments later |

They differ in **only two registers** (`R0x402`/`R0x403`, DAC1 volumes
`0x007d → 0x0073`) — a volume change, not a state difference.  **Neither is an
idle capture:** the reference's idle state (`R0x200 = 0x0010`,
`R0x003 = 0x0000`, `R0x04C = 0x1F25`, `R0x732 = 0x6020`) was measured with
targeted single-register `pbi2c rd16` reads and is recorded in report 205 §5g,
not in these files.

These are the baseline for any future WM8994 comparison: **diff our live register
file against these first** before theorising (report 205 §5f).


## WM8994 oracle + Linux captures (session 43)

Taken to settle why MCLK1 does not clock under Linux (reports 207/208).

| file | when |
|---|---|
| `wm8994-oracle-live-2026-11-12.txt` | minimal 1917 oracle, `io-audio` running: codec ranges, SCRM `AUXCLK1`/pad/CM2, and the AUXCLK1 causality tests |
| `wm8994-linux-capture-2026-11-12.txt` | our Linux image: idle/stream codec, the SoC-vs-reference diff table, the codec freeze detector, the PRM/CKGEN diff, and the ruled-out list |


## WM8994 PM/CM comparison and the ABE-refclk fix (session 44)

Taken to run and close the power-domain/CM hypothesis, then to fix
`CM_ABE_PLL_REF_CLKSEL` (report 209).

| file | when |
|---|---|
| `wm8994-pm-cm-2026-11-12.txt` | both OSes: `PM_*_PWSTST` and `CM*_CLKSTCTRL`, the oracle causal test (Linux CM policy on the working reference), the Linux detector runs and the write-order replay, and the three-image DPLL_ABE comparison |
| `wm8994-e5-master-srg-2026-11-13.txt` | session 45, both E5 boots (the wrong 12.288 MHz SRG base and the correct 49.152 MHz one), the clock tree, the rate measurements, the codec reads, the PCM-ring and FIFO-status diagnostics, the pad dump, the full register diff against the reference, and E6/E9 (oracle pads during playback; the two-link build and the stalled data port) |


## WM8994 E10 — the codec is on McBSP3, and the stereo path works (session 46)

| file | when |
|---|---|
| `wm8994-e10-mcbsp3-2026-11-13.txt` | the oracle's routing log (`AIF 0 Downlink: …->MCBSP3`), the full pad range, the McBSP3 register block, the Linux two-link pads/`XCR2 = 0x8041`, the codec registers, and the operator's both-ears verdict |

This is the capture behind report 214.  Note that user-space reads of the
idle McBSP1/2 register blocks abort (`SIGBUS`); only the active McBSP3 target
reads (trap 206).

## WM8994 speaker path — line outputs, `spk` low, GPIO1_15 high (session 47)

| file | when |
|---|---|
| `wm8994-speaker-2026-11-13.txt` | (A) the minimal 1917 oracle with `Headphone Select` = false and `audio_out` playing — the full 1865-register dump that shows the speaker route (`PM3 = 0x3cf0`, `R0x01e = 0`, `R0x034/035 = 1`, `R0x036 = 0`, `R0x038 = 0x80`, `R0x039 = 0x1c8`, `R0x04c = 0x1f25`, `R0x060 = 0`); (B) our Linux image during the same speaker playback (readable registers via the regmap debugfs) |

This is the capture behind report 215.  Read it with the caveat that the regmap
debugfs side (B) is the driver's cached/intended state, not a silicon read
(trap 207).  The decisive board facts are the GPIO states recorded in the report
(`spk` out lo, `boost` out hi while playing).

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
