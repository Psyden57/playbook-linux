# Pad / signal tables

Addresses: `VA = 0x80100000 + file offset`.  Two different 20/32-byte tables sit
next to each other; both are **PIC-relative data**, so nothing in them is an
absolute pointer.

## 1. Signal-group table at `0x80122638` (14 × 20 bytes)

Runs up to the GPIO table at `0x80122750`.  Records are keyed by **pad signal
id** — the same numbering the GPIO table stores at `+0x18`, and the same ids the
`hwio-config-winchester.so` pad table uses (report 189: `0x52` = sdmmc5_clk,
`0x53` = sdmmc5_cmd, `0x54`–`0x57` = sdmmc5_dat0..3, `0x58` = WL_EN,
`0x59` = WL_IRQ).

| VA | sig | +0x04 | +0x08 | +0x0c | +0x10 |
|---|---|---|---|---|---|
| `0x80122638` | `0x52` | `0x0f000004` | 1 | 0 | 8 |
| `0x8012264c` | `0x53` | `0x0f000004` | 1 | 0 | 0 |
| `0x80122660` | `0x54` | `0x0f000004` | 1 | 0 | 8 |
| `0x80122674` | `0x55` | `0x0f000004` | 1 | 0 | 8 |
| `0x80122688` | `0x56` | `0x0f000004` | 1 | 0 | 8 |
| `0x8012269c` | `0x57` | `0x0f000004` | 1 | 0 | 8 |
| `0x801226b0` | `0x58` | `0x0f000004` | 1 | 0 | 8 |
| `0x801226c4` | `0x59` | `0x0f000004` | 1 | 0 | 8 |
| `0x801226d8` | `0x5a` | `0x0f000004` | 1 | 0 | 8 |
| `0x801226ec` | `0x5b` | `0x0f000004` | 1 | 0 | 8 |
| `0x80122700` | `0x5c` | `0x0f000004` | 1 | 0 | 8 |
| `0x80122714` | `0x5d` | `0x0f000004` | 1 | 0 | 8 |
| `0x80122728` | `0xa1` | `0x0f000007` | 1 | `0x63` | `0x05` |
| `0x8012273c` | `0x8d` | `0x0f000003` | 1 | `0xb2` | `0x1008` |

**Reading.**  The twelve rows `0x52`–`0x5d` are a single group — the **sdmmc5
(WLAN SDIO) + WLAN control pads** — configured identically; `0xa1` and `0x8d`
are two individual pads with their own parameters.  The high byte `0x0f` in
`+0x04` matches the silicon-revision field used by the GPIO table, and the low
byte (`4`, `7`, `3`) is the per-record selector; `+0x08` is an enable.

`0x1008` on signal `0x8d` is the same "extra" value report 189 recorded for one
half of pad `0x4A10019C`; `0x05` on `0xa1` matches another of its entries.  That
cross-check says the `+0x10` field is the hwio `extra` (pull/detect attributes),
but the exact bit meanings are **not yet confirmed** — do not build on them.

Field-level confirmation is a small job: resolve the pad-id → padconf offset with
the `hwio-omap4.so` tables (report 189 §2.1, file VAs `0x4FF8`/`0x5394`/`0x5730`)
and compare the resulting pad values with the live QNX reads (`0x148`–`0x152` =
`0x0118`, pad `0x19C` = `0x0000`).

## 2. GPIO table at `0x80122750` (75 × 32 bytes)

See [`gpio-pins.md`](gpio-pins.md).  Its `+0x18` field is the signal id used by
the table above, which is what ties the two together: e.g. gpio 53 → signal
`0x59` (WL_IRQ) → the `0x59` row here; gpio 54 → `0x58` (WL_EN) → the `0x58` row.

## 3. What this means for Linux

* The reference's WLAN/SDIO pad group is configured **once, as a group**, and the
  group includes the two control signals (`WL_EN`, `WL_IRQ`) — not just the four
  SDIO lines.  Our DTS has the sdmmc5 pads in `sdmmc5_pins` but the two control
  pads live in separate groups (`wl1283_ctrl_pins`, `bridge_pins`), which is the
  same set of pads described differently.
* The GPIO table's **WL_EN = output, level 0** is the important behavioural fact:
  the reference holds the WLAN in reset at boot, and only the driver raises it
  (with the 120 ms delay in `hPlatform_DevicePowerOn`).  Any future WLAN attempt
  must raise gpio 54 explicitly; nothing in the boot path does it for us.
* Neither table is consumed by our Linux boot today.  The pads our DTS does not
  mention stay at their reset values, which is the class of bug traps 135/138
  describe (a pad left in safe mode or in an output state).

## 4. Open

* the field semantics of the `+0x04` low byte and `+0x10`;
* the padconf offset for each signal id (needs the `hwio-omap4.so` resolver);
* whether `startup-playbook` also writes per-pad values elsewhere (the pad
  configuration may be applied from the hwio `.so` rather than from this binary —
  report 189 shows the resolver lives there, and this table may only *select*
  the group).
