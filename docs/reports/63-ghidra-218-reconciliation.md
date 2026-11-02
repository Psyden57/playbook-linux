# 63 — Ghidra 12.1.4's 218 procedures vs the established mode map

Status: reconciliation of the native-provider inventory with `50-code-map.md`
and `60-mode-map-resolution.md`.
Date: 2026-10-19 (continuation session).
Target: `omap4430-bootrom-0x40028000.bin`, 49152 bytes, base `0x40028000`,
SHA-256 `fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`.

Provenance:

- **REA Evidence (Ghidra 12.1.4)** for the procedure inventory, overview,
  pseudocode and raw reads. Cited Evidence IDs are from the live 6.7.0 server.
- **LLVM 23-derived observations** only where explicitly marked: a fresh
  `wrap.elf` at `0x40028000` was used to disassemble the handful of stubs that
  Ghidra did not promote to functions. Those checks are **not** REA Evidence.
- The 2026-10-19 earlier bundle (`rea-ghidra/evidence-bundle.json`) is unchanged.
  This session exported `rea-ghidra/evidence-bundle-2026-10-19b.json`
  (22 records, 0 unknowns, 191,550 bytes).

## 1. What was run

```
open_binary(path=omap4430-bootrom-0x40028000.bin,
            format="raw-binary", architecture="arm",
            entry_point="0x40028000", provider_id="ghidra")
binary_overview      -> 1 segment 0x40028000-0x40034000, 49152 B, 218 procedures, 5 strings
list_procedures      -> 218 starts
procedure_pseudo_code / procedure_assembly / read_bytes -> cited below
export_evidence_bundle -> analysis/rea-ghidra/evidence-bundle-2026-10-19b.json
```

Evidence:

- overview `ev_bdcb80e5c405a1149c83025a60af2f9599036bd6637d6a5c9526bfa7fc6fefb9`
  (same value as the earlier session because the input/profile digest is identical)
- list_procedures `ev_b7981d108fb1ae2ceb69c3831de430d01b8462d7dd46aa3bed3d14d4dbd396ef`

The comparison set is the 282-row start inventory in `50-code-map.md` §2b,
corrected by the byte-verified boundaries in `60-mode-map-resolution.md`. A
machine-readable union of both inventories is in
`rea-ghidra/procedure-reconciliation.csv` (297 rows: 282 map + 15 Ghidra extras).

## 2. Headline result

| measure | count |
|---|---:|
| Ghidra procedures | 218 |
| Ghidra starts that are exact starts in the 282-row map | 203 |
| Ghidra starts **not** in the 282-row map | 15 |
| 282-row map starts **not** Ghidra procedures | 79 |
| Ghidra starts at/after the code/data boundary `0x4002F578` | 0 |
| mode contradictions between Ghidra starts and the resolved mode map | 0 |

**No Ghidra procedure lies in the data region.** Every one of the 218 starts is
inside a code range of the authoritative map. The mixed-mode boundary claims of
`60-mode-map-resolution.md` are therefore not contradicted by the native
provider; in particular Ghidra independently places
`0x4002B740` (ARM) before `0x4002B79C` (Thumb) and does not treat
`0x4002B798` as code.

Ghidra's 218 is **not** a replacement for the 282-row map and is **not** 218 new
findings. It is a decompilable subset (203 exact starts) plus 15 legitimate
boundary additions. The map, in turn, over-counts by a small number of
non-functions (see §4).

## 3. The 15 Ghidra starts absent from the 282-row map

All 15 lie inside a code range of the resolved mode map, but each was checked
and is a real entry or a real boundary refinement, not a data false positive.

| Ghidra start | mode | why it is legitimate |
|---|---|---|
| `0x4002877C` | ARM | Second ARM leaf documented in `50-code-map.md` §2a; omitted from §2b. |
| `0x40028910` | ARM | CPU0 reset entry (`50` §2a); omitted from §2b. |
| `0x400293C2` | Thumb | GIC software-IRQ dispatcher (`40` §4, `50` §2a); omitted from §2b. |
| `0x40029800` | Thumb | `tbb` case target of `0x40029AE6` (`50` §2a lists it as a target). |
| `0x40029866` | Thumb | `tbb` case target of `0x40029AE6`. |
| `0x4002992C` | Thumb | `tbb` case target of `0x40029AE6`. |
| `0x4002997E` | Thumb | `tbb` case target of `0x40029AE6`. |
| `0x40029B10` | Thumb | Real helper; `0x40029CA0` case 9 tail-calls it (`FUN_40029ca0` pseudocode). |
| `0x4002BCA2` | Thumb | Real boot-media per-driver loop (`push.w {r4-r8,lr}`; loops over device type `0x41..0x44`). LLVM-derived boundary check. |
| `0x4002BD5C` | Thumb | Entry `0x4002BD5D` in the peripheral-driver vector table (`0x4002F5E4`). |
| `0x4002BEC2` | Thumb | Entry `0x4002BEC3` in the same peripheral-driver vector table. |
| `0x4002DA1C` | Thumb | Real helper; calls `0x4002D9E0` and branches into `0x4002D930`. LLVM-derived boundary check. |
| `0x4002E1E2` | Thumb | Real CPSR helper wrapper; calls `0x4002F548`, `0x4002E158`, `0x4002E1CC`. |
| `0x4002E550` | Thumb | Real flash-path routine (`push.w {r4-r8,lr}`, `movw r8,#0x147`). |
| `0x4002F574` | ARM | `bx lr` at `0x4002F574`; `0x4002F570` is `b 0x4002F558`. The map's "`b` + `bx lr` veneer" is two 4-byte halves; Ghidra promotes the `bx lr`. |

Evidence: `list_procedures`
`ev_b7981d108fb1ae2ceb69c3831de430d01b8462d7dd46aa3bed3d14d4dbd396ef`;
`0x40029CA0` pseudocode
`ev_b878a95811b5f773b3ac38cdf0666b632762ebb4d4d09789917def9766c6877a`.
`0x4002BCA2`, `0x4002DA1C`, `0x4002E1E2`, `0x4002E550`, `0x4002F56C-0x4002F577`
are **LLVM-derived** boundary checks.

## 4. The 79 map starts absent from Ghidra

Breakdown (the categories do not overlap):

| category | count | addresses / note |
|---|---:|---|
| fake starts: filler `bl .+4` | 7 | `0x40028974`, `0x40028978`, `0x4002897C`, `0x40028980`, `0x40028984`, `0x40028988`, `0x4002898C` — two-byte filler instructions, not functions. (The eighth filler at `0x40028970` is not in §2b.) |
| data mis-listed as code | 1 | `0x4002B798` is the literal `0x4A100000`; `60` already resolves this and Thumb resumes at `0x4002B79C`. Ghidra is correct to reject it. |
| mode-switch veneers | 2 | `0x4002F56C` is a 2-byte Thumb `bx pc`; `0x4002F570` is an ARM `b` tail branch. Neither is a normal function entry. |
| API-table targets only | 4 | `0x400284C0`, `0x400285CA`, `0x40028604`, `0x40028620` (public API entries 12, 14, 44, 45). Ghidra's raw import has no pointer-driven entry seeding from `0x40028400`. A fifth API target, `0x40028612` (entry 11), is absent from the 282-row map as well, so it is outside this 79-row comparison. |
| genuine starts not reached by recursive descent | 65 | Mostly Thumb entries reachable only through literal pools, `blx` register targets, or table pointers; plus ARM islands/veneer-adjacent functions. See representative list below. |
| **total** | **79** | 66 Thumb, 13 ARM. 7 + 1 + 2 + 4 + 65 = 79. |

Four further `tbb` case targets (`0x4002987C`, `0x400298A2`, `0x40029A06`,
`0x40029A5E`) are absent from **both** inventories; they are not members of the
79. The other four `tbb` targets (`0x40029800`, `0x40029866`, `0x4002992C`,
`0x4002997E`) appear in §3 as Ghidra extras.

Representative genuine misses:

- startup/trampoline: `0x4002882C` (walker), `0x400288F8` (Thumb `bl 0x4002F2F0`,
  no caller);
- mode islands: `0x4002CE90` (Thumb helper), `0x4002CEB0` (ARM `0xF0` veneer),
  `0x4002CEBC` (Thumb resume), `0x4002F310`/`0x4002F384` (ARM cache/TLB library);
- MMCHS/card/flash primitives: `0x4002DCD6`, `0x4002DDD4`, `0x4002DE60`,
  `0x4002E25C`, `0x4002E6BA`, `0x4002E7AC`, `0x4002E844`, `0x4002E8CE`,
  `0x4002E904`, `0x4002E930`, `0x4002EA5E`, `0x4002EB92`, `0x4002ECA4`,
  `0x4002ECE8`, `0x4002ED48`, `0x4002ED76`, `0x4002EF14`;
- small accessor/veneer cluster: `0x4002E0A8`, `0x4002E0B6`, `0x4002E0CE`,
  `0x4002E0DE`, `0x4002E0FC`, `0x4002E202`;
- `0x4002B970` (flash identify), `0x4002BC10` (cmd-8 SDRAM-boot open),
  `0x4002BFD4`, `0x4002CF5A`…`0x4002D40C`.

Why these were missed: the REA raw-binary import uses **one entry point**
(`0x40028000`) plus Ghidra's default analyzers. It follows direct `bl`/`b` flow
and some pointer tables, but it does not seed every Thumb function pointer in
the ROM's literal pools, every `tbb` case, or every API-table entry. Ghidra's
own auto-analysis ran in 6 s and reported no no-return repair sites. This is a
provider **coverage** limitation, not a mode-map conflict.

## 5. Consequences and next steps

1. Treat the authoritative start set as:
   **282 map rows − 7 filler − 1 literal − 2 veneers + 15 Ghidra additions
   ≈ 287 genuine starts.** The exact count depends on whether the two
   mode-switch veneers are counted as functions.
2. For the 65 genuine misses, REA has no operation to create a Ghidra function.
   A future session can either (a) run a small Ghidra post-script over the union
   address list, or (b) decompile those ranges with LLVM (already done for the
   principal functions). Do not call them "new" Ghidra findings until an entry
   is created.
3. The mode map in `60-mode-map-resolution.md` remains authoritative. Ghidra
   confirms it wherever both have a function start and never places a procedure
   in data.
4. Re-use the exported 22-record bundle
   `analysis/rea-ghidra/evidence-bundle-2026-10-19b.json` for this session's
   Evidence; MCP Evidence IDs are scoped to the live server process.
