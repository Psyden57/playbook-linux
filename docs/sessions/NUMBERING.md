# Session numbering

The project spans **twenty-three sessions** so far (and continues).  Two numbering schemes are in use and they
must not be confused:

> **Scope.**  These sessions are *this* project: a native, CRC-path boot of
> a clean mainline kernel.  The pre-Linux sessions are boot-chain research, not a
> separate OS project.

* **Absolute** (`session-01` … `session-23`) — chronological, used by the note
  files in this directory.  This is the scheme the backfill uses.
* **Linux-era** ("session 1…13") — used inside the reports and handoffs, because
  numbering only began when the CRC boot path was confirmed and the Linux work
  started.  **Linux session N = absolute session N + 6.**

So:

| absolute | Linux-era | phase |
|---|---|---|
| 1 – 6 | — | boot-chain research: boot ROM, second-stage loader, NVRAM, RPMB, the CRC/signature path, the pre-OS reflash, the first chainloader (native boot) |
| **7** | **1** | clean 6.15.11 on the CRC path |
| **8** | **2** | clean kernel, capture v2 |
| **9** | **3** | the "1.4 s global stall" (later proved to be the VFS panic) |
| **10** | **4** | WDT2 / eMMC / USB gadget / console |
| **11** | **5** | display RE + live QNX register capture (`111`…`119`) |
| **12** | **6** | backlight, SPI TCON, four-lane discovery, bridge retired (`120`…`122`) |
| **13** | **7** | the reference *does* program the bridge; QNX SPI framing; byte-identical PLL; the first "partial image" (later withdrawn) (`123`…`127`) |
| **14** | **8** | **bridge delivery fixed** (the VC0/HS null packet starts the DSI clock); HS video still not received (`128`…`131`) |
| **15** | **9** | the **QNX DSI register oracle**; `PPI_TX_RX_TA` latched at `PPI_STARTPPI`; the `VP_HTIM1/2` porch bug; the TCON table at bring-up; **the DISPC core clock — and with it, the display works** (`132`, `133`) |
| **16** | **10** | eMMC read proof under the DSS clock regime; the media-offset correction; the hd3 shrink attempt and the `bb10mt` UFS recovery (`134`, `135`) |
| **17** | **11** | the Linux MMC **write path** measured (report 136); a smaller hd3 FS is **reformatted and reseeded** at boot (report 138); the QNX6 read path (`137` superseded) |
| **18** | **12** | a Linux **root in region 1** via the pre-OS `0xF7` loop (reports 139/140); the V1 `qcfp` misroute and the RCFS recovery; the **RPMB top-up is feasible** and the Linux read path is proven (report 141); the boot log is clean (report 142) |
| **19** | **13** | the RPMB **frame key is pinned** (report 143); the 224-byte record block is **encrypted on media by the monitor**, so the plaintext patch is invalid (report 144); the monitor call was implemented, the first hang was the DRAM bias, and every service answers **`0xFF05`** (report 145); static RE of every smc call site — no handshake, `r3` suspect (report 146) |
| **20** | **14** | the monitor's **address view is pinned** with the `0x12` oracle — identity addresses, every page below `0xB0000000`, any domain (report 147, superseding 145/146's "global rejection" and the DRAM bias); the seal ABI is accepted end to end but the transform **does not reproduce** the device's block-0x43 pair with any key/framing/op variant (report 148) |
| **21** | **15** | read-only boot-chain verification, the **same-boot RPMB pair**, the trustzone dead end (report 157), and the **measured armed state** — record-armed but carrying the stock type-4 image.  The operator flashed the sealed `os_type` 6 IFS on 2026-11-01, so the unit is now armed **and** sealed (report 160 is the pre-flash measurement) |
| **22** | **16** | **tooling, mostly offline**: `tools/bbusb.py` with CFP-style subcommands (`info`, `flash`, `reset-osregion`, `pack`/`unpack`/`split`); static RE of CFP.exe and LDR_01 — the eight `info` opcodes, **`RESETQNXOSREGION` = `0xE8 {4}` = delete NVRAM `0x2074`**, and `0x39` shown to be the CREAD opcode so path 2 cannot resolve (`161`…`164`) |
| **23** | **17** | the canonical-docs audit (report 167) executed: the ownership model, the freshness stamps and `tools/docs_audit.py`; the offline IFS rebuild work (`IFS-REBUILD.md`, reports 158/159) |

## Anchors (evidence for the mapping)

The Linux-era sessions are anchored by their own handoffs, all in `docs/reports/`:

| handoff file | produced by |
|---|---|
| `HANDOFF-2026-10-26-clean-kernel.md` | Linux session 1 (absolute 7) |
| `HANDOFF-2026-10-26-clean-kernel-v2.md` | Linux session 2 (absolute 8) |
| `HANDOFF-2026-10-26-global-stall.md` | Linux session 3 (absolute 9) |
| `HANDOFF-2026-10-27-session5.md` | Linux session 4 (absolute 10) — its title says "session 4 → session 5" |
| `HANDOFF-2026-10-27-session6.md` | Linux session 5 (absolute 11) — originally titled "session 5 → session 6" |
| (this repository's `docs/project/HANDOFF.md`) | Linux session 6 (absolute 12) |
| `docs/sessions/session-13.md` | Linux session 7 (absolute 13) — written by the session |
| `docs/sessions/session-14.md` | Linux session 8 (absolute 14) — written by the session |
| `docs/sessions/session-15.md` | Linux session 9 (absolute 15) — written by the session |
| `docs/sessions/session-16.md` | Linux session 10 (absolute 16) — written by the session |
| `docs/sessions/session-17.md` | Linux session 11 (absolute 17) — written by the session |
| `docs/sessions/session-18.md` | Linux session 12 (absolute 18) — written by the session |
| `docs/sessions/session-19.md` | Linux session 13 (absolute 19) — written by the session |
| `docs/sessions/session-20.md` | Linux session 14 (absolute 20) — written by the session |
| `docs/sessions/session-21.md` | Linux session 15 (absolute 21) — written by the session |
| `docs/sessions/session-22.md` | Linux session 16 (absolute 22) — written by the session |

The pre-Linux phase (absolute 1…6) has these artefacts, in chronological order. <!-- audit-ok: pre-Linux phase range -->
The exact assignment to 1…6 is **not** reconstructed here — the operator knows it,
and each session knows its own position:

| artefact | date / topic |
|---|---|
| `HANDOFF.md` in `docs/reports/` | boot ROM RE around `0x40028000` |
| `BOOTROM-0x40028000-REPORT.md` | boot ROM API and flash tables |
| `HANDOFF-2026-10-20-next-session.md` | 2026-10-20 → next |
| `HANDOFF-2026-10-21-next-session.md` | 2026-10-21 (final) → next |
| `HANDOFF-cellebrite-modified-ramloader.md` | the Cellebrite / modified-ramloader track |
| `HANDOFF-2026-10-26-next-session.md` | 2026-10-26: CRC path, pre-OS reflash |

Reports `10`…`102` are the pre-Linux phase's output (boot media, monitor
security, IRAM tables, the ROM API, `hd0`, NVRAM records `0x2801`, RPMB, the CRC
recipe, the loader audit, the pre-OS IFS reflash).
`103`…`146` are the Linux-era sessions.

## Why the two schemes are kept

Renaming the reports or the handoffs would be a large, lossy edit of documents
that later sessions cite section by section ("report 121 §11", "HANDOFF §4").
Instead: the *notes* use absolute numbers, everything else keeps its original
wording, and this file is the translation.
