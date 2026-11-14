# 218 — Stock TabletOS with `ti0` up: the readable board state, diffed against our Linux boot (negative)

Date: 2026-11-14 (session 49, Linux-era 43).
Bench: the unit was booted into **stock TabletOS 2.1.0.1917** (shim IFS + the
full 1917 RCFS) for the capture, then returned to our Linux image.  No NVRAM,
MCT or RPMB write.  Device-side files: none new (the oracle's `/var/ti1283` from
report 217 §2.3 remains on `hd3`).

## 0. Headline

This is the experiment report 217 §6 asked for: **capture the reference at the
moment its WLAN comes up, and diff it against our Linux boot.**  It was done,
and the answer is **negative on every readable register**: the WLAN pads, the
WL_EN/WL_IRQ GPIO directions and levels, the `RM_L4PER_MMCSD5_CONTEXT` flag and
all TWL6030 rails either match or are in a state the reference **itself runs the
chip with**.  So the remaining difference is not in readable board state.

The one thing the exercise did surface is a **documentation defect**: the
recorded "way back to stock TabletOS" (`local/work/minimal/wayback.signed`)
does not boot this armed unit, and its RCFS has no root hook (§1).

## 1. Getting stock TabletOS up (and the way-back correction)

`wayback.signed` is IFS + RCFS only, and both are the **vendor** ones:

* its IFS is the vendor `PlayBook_2.1.0.1917.4.ifs`, `os_type` **4**.  An armed
  unit boots on the factory/CRC path, which requires `os_type == 6` (SAFETY §3,
  trap 68) — flashing it and rebooting halts with the red error blink.
* its RCFS is the vendor RCFS, so `/base/scripts/startup.sh` has **no**
  `/accounts/devuser/startup.sh` hook and there is no door and no root ssh.

The pair that works — and the one already used for the oracle — is:

```bash
# unit in the bootrom (0fca:0001)
python3 tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs \
        --dest-block 0x825 --from-bootrom --password 0000 --apply
python3 tools/bbusb.py flash --component local/work/rcfs-1917.rcfs \
        --dest-block 0x8D5 --from-bootrom --password 0000 --apply
```

Both are `os_type` 6 (`ifs-shim-sealed.ifs`, sha256 `b52195bb…`) and the
**modified** RCFS (`local/work/rcfs-1917.rcfs`, sha256 `c4ab6e34…`, the one
`back-1917.signed` uses).  After that the unit boots 2.1.0.1917 with `/base`
mounted, root ssh through the door, and its own wifi service brings `ti0` up by
itself:

```
ti0: flags=…RUNNING… address: 14:74:11:8f:9c:c3
     inet 192.168.0.212 …
```

i.e. the reference joined the operator's AP — the strongest possible proof that
the chip, its firmware and its SDIO path are live on this unit.

## 2. The capture

Taken with `ifconfig ti0` RUNNING (WL_IRQ high), using `memdump3` and `pbi2c`
pushed to `/tmp`:

| range | words | file |
|---|---|---|
| CORE pads `0x4A100000`–`0x4A1005FC` | 384 | `docs/captures/wlan-qnx-state-2026-11-14.txt` |
| WKUP pads `0x4A31E040`–`0x4A31E09C` | 24 | same |
| `GPIO1`/`GPIO2` `0x130`–`0x13C` | 4 + 4 | same |
| `CM_PER` `0x4A008000`–`0x4A0081FC` | 128 | same |
| `CM2` `0x4A009000`–`0x4A0095FC` | 384 | same |
| `PRM` `0x4A307000`–`0x4A3072FC` + `0x4A307540`–`0x4A30757C` | 192 + 16 | same |
| TWL6030 `0x80`–`0xB5` (8-bit regs, `pbi2c dump`) | 54 | same |

The Linux counterpart is `docs/captures/wlan-linux-state-2026-11-14.txt`
(1 174 lines, the same ranges read with `devmem`/`i2cget` on the clean image).

Note on sampling: the Linux capture is taken **after** the failed probe, so
`CM2 0x560` (`CM_L4PER_MMCSD5_CLKCTRL`) reads `0x00030000` (module idle) there
and `0x00000002` (MODULEMODE=2) on the reference.  At the probe itself ours is
`0x2` too (report 180's variant-3 `probe-pm fck=48000000 idlest=0`), so this is
a time-of-sample artefact, not a difference.  Do not read it as one.

## 3. The diff (136 of 1 120 common words differ)

**Everything WLAN-relevant matches:**

| item | Linux | reference |
|---|---|---|
| pads `0x078`/`0x07A` (WL_IRQ, mux 3 + wakeup) | `410B4103` | `410B4103` |
| pad `0x07C` (WL_EN + BT_EN, both mux 3) | `00030003` | `00030003` |
| pads `0x148`/`0x14C`/`0x150` (sdmmc5) | `01180118` | `01180118` |
| `GPIO2_OE` bits 21/22 | in / out | in / out |
| `GPIO2_DATAOUT` bit 22 (WL_EN) | **1** | **1** |
| `GPIO2_DATAIN` bit 21 (WL_IRQ) | **0** | **1** |
| `RM_L4PER_MMCSD5_CONTEXT` (`PRM 0x4A307564`) | 1 | 1 |

**The rest, classified:**

* **~80 CORE/WKUP pad words** in `0x050`–`0x1D4`: our pinctrl resets unclaimed
  pads to safe mode (`0x010F`/`0x011F`) while the reference leaves the board
  init's values (`0x0003`, `0x4103`, `0x000F`, …).  Same class as report 180 §6;
  none is a rail, enable or clock.
* **GPIO1 direction** — the reference's outputs are bits
  `[0,2,4,6,13,14,15,18,28,31]`, and among them it drives **0, 4, 18, 28, 31
  LOW** (15 is low because it is not playing audio; it is the `audio_boost`
  line).  Ours are `[2,6,13,14,15]`.  So the five QNX-only lines read **low**
  either way — **there is no missing enable here**, which is exactly what trap
  136's live test showed from the other direction.  GPIO2 is the same story
  (reference drives 5/7 low, ours leaves them input; ours drives 20 low).
* **`GPIO_CTRL` (`0x130`) = 2** on the reference for GPIO1/GPIO2, `0` on ours —
  a module control bit, not an enable; the reference runs with it and so do we.
* **`CM_PER 0x15C`** = `0x204` vs the reference's `0x329`: `CM_DIV_M5_DPLL_PER`,
  the DSS divider — trap 116 says never write the reference's value (it glitches
  the panel), and its output is running either way.
* **TWL6030**: 16 registers differ, all group/transition/state *encodings*.
  No rail is off on our side; where the on/off bits differ they differ in the
  direction Linux **ON** / reference **OFF** (`0xAF`, `0xB2` — REGEN2 and its
  neighbour, the display rail family, trap 135/§3.21).  `CLK32KG` (`0x8C`–`0x8E`)
  reads STATE ON, APP ON in both.

## 4. Verdict

**No readable board state can explain a chip that stays down under Linux.**  The
reference holds WL_EN high, WL_IRQ high, GPIO1_0/4/18/28/31 and GPIO2_5/7 low;
our image has the same pads, the same WL_EN, the same rails, and leaves those
five-plus-two lines as inputs that read low.  Combined with report 217 (the
host program and the EN ordering replayed, both negative), the remaining classes
are:

1. **inside the unreadable MMCHS5 block** — the only state neither OS lets a
   third process read (trap 101), and the reference's own writes for it are now
   known and replayed;
2. **the chip-side pins** (VBAT, the 26 MHz TCXO, SLOW_CLK, RESET_N) — trap 165
   has no probe.

The honest next step is therefore either a logic analyser / board view (outside
this bench, and the operator has none) or a different unit whose WLAN has never
been brought up.  Do not spend another session on readable-register diffs.

## 5. Traps

* **226.** `local/work/minimal/wayback.signed` does **not** boot an armed unit:
  its IFS is the vendor `os_type` 4 image (the factory/CRC path halts — red
  blink) and its RCFS has no `/accounts/devuser/startup.sh` hook, so there is no
  root either.  The way back to TabletOS is `local/work/ifs-shim-sealed.ifs` at
  `0x825` **plus** `local/work/rcfs-1917.rcfs` at `0x8D5`.
* **227.** The region-0 RCFS slot (`0x8D5`) now holds the **full** 1917 RCFS, not
  the pruned oracle one.  `local/work/minimal/setB.signed` is still the pruned
  pair; re-flash it if the minimal oracle is wanted again.

## 6. Sources

* Captures: `docs/captures/wlan-qnx-state-2026-11-14.txt`,
  `docs/captures/wlan-linux-state-2026-11-14.txt`.
* Reports 180 §6, 192 §4/§8, 196, 217; traps 101, 116, 134/136, 135/§3.21, 165.
