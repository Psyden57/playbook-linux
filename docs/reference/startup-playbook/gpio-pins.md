# GPIO pins and the board-variant pin selection

All addresses are `VA = 0x80100000 + file offset`; the file is
`local/work/1917/ifs/proc/boot/startup-playbook`.

## 1. The Winchester GPIO table — 75 records at `0x80122750`

Records are **32 bytes**; the table runs `0x80122750`–`0x801230AF` and is
followed immediately by a different (20-byte-record) table at `0x801230b0`
(layout still open — see the README's status table).

Field map (confirmed by the pins whose roles are known independently):

| offset | field |
|---|---|
| `+0x00` | GPIO number (QNX numbering: `N = (bank-1)*32 + bit`, e.g. 54 = GPIO2_22) |
| `+0x04` | direction: `1` = output, `0` = input |
| `+0x08` | initial level (outputs) |
| `+0x0c` | detect (inputs): `0` none, `1` ?, `2` falling, `3` both edges |
| `+0x14`,`+0x15` | silicon-revision gate (a record applies only when the running revision matches; see the `19` and `161` groups) |
| `+0x16` | phase / variant selector (`0x01` for most; `0x00` for the first four records) |
| `+0x18` | **pad signal id** (the hwio signal number: e.g. `0x58` = WL_EN, `0x59` = WL_IRQ) |

Decoded table (`dir` is the initial direction; `det` as above):

| # | gpio | dir | lvl | det | sig | # | gpio | dir | lvl | det | sig |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 173 | in | 0 | 0 | – | 38 | 171 | in | 0 | 0 | 0x9d |
| 1 | 174 | in | 0 | 0 | – | 39 | 172 | in | 0 | 0 | 0x97 |
| 2 | 177 | in | 0 | 0 | – | 40 | 13 | out | 0 | 0 | 0xaa |
| 3 | 178 | in | 0 | 0 | – | 41 | 20 | in | 0 | 3 | 0x11 |
| 4 | 0 | out | 0 | 0 | – | 42 | 21 | in | 0 | 3 | 0x09 |
| 5 | 18 | out | 0 | 0 | 0xac | 43 | 22 | in | 0 | 3 | 0x12 |
| 6 | **35** | **in** | 0 | **2** | **0x16** | 44 | 28 | out | 0 | 0 | 0x8c |
| 7 | 39 | out | 0 | 0 | 0x0e | 45 | 6 | out | 1 | 0 | 0x08 |
| 8 | 41 | out | 0 | 0 | 0x2f | 46 | 7 | in | 0 | 0 | – |
| 9 | 44 | out | 1 | 0 | 0x93 | 47 | 8 | in | 0 | 0 | – |
| 10 | 45 | out | 0 | 0 | 0x98 | 48 | 29 | in | 0 | 0 | 0xae |
| 11 | 48 | out | 1 | 0 | 0x8b | 49 | 9 | in | 0 | 0 | – |
| 12 | 49 | in | 0 | 3 | 0x94 | 50 | 10 | in | 0 | 0 | – |
| 13 | **53** | **in** | 0 | **2** | **0x59** | 51 | 4 | out | 0 | 0 | 0xad |
| 14 | **54** | **out** | **0** | 0 | **0x58** | 52 | 96 | out | 0 | 0 | – |
| 15 | **55** | **out** | **0** | 0 | **0x5d** | 53 | 97 | out | 0 | 0 | – |
| 16 | 56 | out | 0 | 0 | 0x9a | 54 | 102 | out | 0 | 0 | 0x8e |
| 17 | 60 | out | 0 | 0 | 0x2e | 55 | 2 | out | 0 | 0 | 0xa8 |
| 18 | 83 | out | 0 | 0 | 0x22 | 56 | 3 | out | 1 | 0 | – |
| 19 | 101 | out | 1 | 0 | 0x14 | 57 | 14 | out | 0 | 0 | 0xab |
| 20 | 104 | out | 0 | 0 | 0x15 | 58 | 15 | out | 0 | 0 | 0xa9 |
| 21 | 124 | out | 0 | 0 | – | 59 | 19 | out | 1 | 0 | rev 0x09 |
| 22 | 127 | out | 1 | 0 | 0x92 | 60 | 19 | out | 0 | 0 | rev 0x0a |
| 23 | 175 | in | 0 | 0 | 0x9f | 61 | 19 | out | 1 | 0 | rev 0x0b |
| 24 | 176 | in | 0 | 0 | 0xa0 | 62 | 19 | out | 0 | 0 | rev 0x0c |
| 25 | 31 | out | 0 | 0 | 0x9e | 63 | 161 | out | 0 | 0 | rev 0x0a |
| 26 | 32 | in | 0 | 0 | 0x0a | 64 | 25 | in | 0 | 3 | rev 0x0a |
| 27 | 33 | in | 0 | 0 | 0xa1 | 65 | 26 | in | 0 | 3 | 0xaf |
| 28 | 36 | in | 0 | 0 | 0x9b | 66 | 23 | out | 0 | 0 | rev 0x0c |
| 29 | 37 | out | 0 | 0 | 0x9c | 67 | 24 | in | 0 | 1 | rev 0x0c |
| 30 | 38 | in | 0 | 0 | 0x8d | 68 | 34 | in | 0 | 2 | 0x0f |
| 31 | 50 | in | 0 | 1 | 0x3a | 69 | 40 | in | 0 | 0 | 0x01 |
| 32 | 51 | in | 0 | 0 | 0x95 | 70 | 46 | in | 0 | 1 | 0x0b |
| 33 | 52 | in | 0 | 0 | 0x96 | 71 | 47 | out | 0 | 0 | 0x0d |
| 34 | 59 | in | 0 | 1 | 0xa2 | 72 | 61 | out | 1 | 0 | 0xa4 |
| 35 | 62 | in | 0 | 0 | 0xa5 | 73 | 190 | in | 0 | 1 | 0xb1 |
| 36 | 100 | in | 0 | 0 | 0xa6 | 74 | 138 | in | 0 | 3 | – |
| 37 | 103 | out | 0 | 0 | 0xa7 | | | | | | |

The three rows that matter for the WLAN and the one for the touch:

* **gpio 53** — input, falling, signal `0x59` → **WL_IRQ** (GPIO2_21).
* **gpio 54** — output, **level 0**, signal `0x58` → **WL_EN** (GPIO2_22).
  The reference boots with the WLAN **disabled** and the driver raises it.
* **gpio 55** — output, level 0, signal `0x5d` → **BT_EN** (GPIO2_23).
* **gpio 35** — input, falling, signal `0x16` → the **Munin touch IRQ**
  (GPIO2_3, pad `0x056` `gpmc_ad11`), matching session 38's `tp_intr=1035`.

This confirms the project's independent findings (reports 180/186/192): on this
board WL_EN = GPIO2_22 = gpio 54, WL_IRQ = GPIO2_21 = gpio 53, BT_EN = GPIO2_23 =
gpio 55.  It also explains why the board is silent on the WLAN bus at boot: the
start-up *drives WL_EN low*; anything that wants the chip must raise it (the
reference's WLAN driver does, with a 120 ms delay).

## 2. The variant pin selection — `WLAN_EN_GPIO`/`BT_EN_GPIO`

These strings are **pre-formatted log lines**, not a config table:

| string | VA |
|---|---|
| `WLAN_EN_GPIO=151,WLAN_INT_GPIO=152` | `0x8012b35c` |
| `WLAN_EN_GPIO=151,WLAN_INT_GPIO=163` | `0x8012b380` |
| `WLAN_EN_GPIO=54,WLAN_INT_GPIO=53` | `0x8012b3a4` |
| `BT_EN_GPIO=171` | `0x8012b3d0` |
| `BT_EN_GPIO=55` | `0x8012b3e0` |

Their pointer table is at `0x80102400`… (PIC-relative; base `0x8013fa70`):

| table word | points to | meaning |
|---|---|---|
| `0x8010242c` | `0x8012b3a4` | **WLAN 54/53** (the default branch) |
| `0x80102454` | `0x8012b35c` | WLAN 151/152 |
| `0x80102458` | `0x8012b380` | WLAN 151/163 |
| `0x8010244c` | `0x8012b3d0` | BT 171 |
| `0x80102434` | `0x8012b3e0` | BT 55 |

The selection, in `FUN_801018d8` (entry `0x801018d8`, the start-up main; the
Ghidra name is `FUN_801037f0` because of a non-contiguous tail entry):

```
0x8010219c  ldr  r2, [r4, iVar2]      ; r2 = board-type pointer
0x8010219c  cmp  r2, #4               ; **(int**)… == 4
0x801021a0  beq  0x80102358           ; board type 4: pick between 151/152 and 151/163
0x801021a4  …                         ; else: WLAN_EN_GPIO=54,WLAN_INT_GPIO=53
0x80102358  ldr  r3, [r4, DAT_80102450]
0x80102360  ldr  r3, [r3]
0x80102364  cmp  r3, #3               ; sub-variant <= 3
0x80102368  ldrls r3, [DAT_80102454]  ;   WLAN 151/152
0x80102378  ldr   r3, [DAT_80102458]  ;   else WLAN 151/163
```

and for BT, at `0x80102220` (`ldr r3,[DAT_8010244c]` → BT 171) with a fallback to
`DAT_80102434` (BT 55) when the board type is not 4.

**Conclusion for this unit.**  Winchester is *not* board type 4, so it takes the
default branch: **WLAN_EN_GPIO = 54, WLAN_INT_GPIO = 53, BT_EN_GPIO = 55**.
The unit's own syspage publishes exactly that pair (`WLAN_EN_GPIO=54,
WLAN_INT_GPIO=53` — report 189, a live read from an earlier session), so this is
the selected value, not merely the branch's default.  The
`151/152`, `151/163` and `171` sets belong to a different board (board type 4,
split by a sub-variant), which is why they never appeared in the WLAN bring-up
code — we were not missing a pin.  The linux-era attempts used gpio 54/53, which
is the correct Winchester pair; the failure was elsewhere (the chip is held in
reset by WL_EN low until a driver raises it, and the 1917 driver's own
`hPlatform_DevicePowerOn` does `power(1); delay(120)`).

### Open

* the numeric value of the *board type* enum (the code compares `== 4`; the name
  table's index order is not yet proven to be the enum order) — needs the caller
  of `Board type = %s` (`0x8012ba8c`);
* what `DAT_80102450`'s sub-variant is (a version field of the MFG record);
* which pad signal ids map to which pad offsets for the records above (the pad
  table is the next 20-byte-record block at `0x801230b0`).
