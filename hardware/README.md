# hardware/ — reference documents (local only)

This directory holds reference material for the SoC and the PMIC.  **Nothing in
it is committed**, because it is not ours to redistribute:

| file | what | why excluded |
|---|---|---|
| `swpu231ap.pdf.txt` | TI OMAP4430 TRM (SWPU231AP), text extraction | TI copyright |
| `swpu231ap.pdf` | the same TRM as PDF | TI copyright |
| `twl6030.pdf` | TWL6030 PMIC datasheet | TI copyright |
| `TC358764XBG.PDF` | **TC358764 DSI→LVDS bridge datasheet** — the register map behind reports 123–127: the DSI RX/PLL config, `VP_HTIM*`/`VP_VTIM*`, the `LV_MX` mapping and `LV_CFG`.  Directly relevant to the half-frame problem | Toshiba copyright |

They are the working copies the bring-up used: the TRM is where the pad tables
(`CONTROL_CORE_PAD*`), the PRCM register maps, the DSI `COMPLEXIO_CFG1` /
`DSI_CTRL` field layouts (Table 10-530) and the DISPC timing encodings come from.
A new contributor needs their own copy of SWPU231AP (it is a public TI document),
the TWL6030 datasheet, and the TC358764 datasheet.

`TC358764XBG.PDF` was added to the workspace after it was created (it was missed
by the initial copy because of a `*.pdf` filter); it is the bridge's own datasheet
and the authority for the register work in reports 123–127.

Everything the project *derived* from them is written down in `docs/reports/` —
pin numbers, pad offsets, register values and bit fields are all quoted there
with the TRM table referenced, so the reports are usable without the PDFs.

`.gitignore` excludes `hardware/*.txt` and `hardware/*` PDFs **case-insensitively**
(a `TC358764XBG.PDF` with an uppercase extension was tracked by mistake until
session 23; it has been untracked, but it remains in the repository's earlier
history — do not publish that history without removing the blob).  This README is
the only tracked file here.
