# 228 — The BT-core liveness probe, done properly: `BT_EN` driven, UART path verified, still silent

Date: 2026-11-14 (session 50, Linux-era 44).
Bench: the unit on our Linux image **`0xBAFD9B3B`** (kernel `7.3.3` #57),
reached over ssh (`tools/pbssh.sh`).  No flash, no NVRAM/MCT/RPMB write; the
probe is a `/tmp` tool plus two GPIO/UART reads.

## 0. Headline

1. **The session-29 probe is superseded, not merely "invalid".**  It ran with
   `BT_EN` (gpio 55 = `GPIO2_23`, pad `0x4A10007C` bits 31:16) undriven.  This
   session drove it **low → 50 ms → high** — the reference's own `btp-btmgr`
   sequence — and the chip **still sends nothing back** to `HCI_Reset`
   (`01 03 0c 00`) at 115200 8N1, on three fresh pulses.
2. **The Linux-side measurement is now sound.**  The pad is already mux 3 for
   both halves (`0x00030003`), so no DTS change was needed; the pulse really
   moves `GPIO2_DATAOUT` bit 23 (0 → 1); the UART2 pads are mux 0 with the RX
   input buffer enabled (`0x4118`, `INPUT_EN` = bit 8); and after the write
   `UART2 LSR = 0x60` (`THRE`/`TEMT` set) — the four bytes left the UART.
3. **But silence does not prove the die is unpowered.**  The chip's power,
   VIO and 26 MHz TCXO are independently proven good on this unit by the
   oracle's **live** WLAN bring-up (report 217).  The reference's **BT** half
   has never been demonstrated live here (report 216 is static), and BT can
   require more than `BT_EN` (the 32.768 kHz SLOW_CLK is the named candidate).
   So the plan's inference "silence → power/TCXO/reset, SDIO controller
   exonerated" is **too strong**: the probe says the BT core does not come up
   with `BT_EN` alone under our boot, and it does **not** clear the SDIO
   controller.

## 1. Why the session-29 result was not a measurement

Report 216 (session 49) found that `BT_EN` is a separate pin from `WL_EN`, is
driven by `btp-btmgr` (`UART_BTPower(0)` → 50 ms → `UART_BTPower(1)`), and — at
the time of the probe — **our DTS never muxed or drove it**.  Session 29's
`HCI_Reset` therefore reached a BT core whose enable had never been asserted,
and "no response" was recorded without saying so.  The handoff called it
"invalid, not negative"; this report replaces it with a real negative.

## 2. What was run

`rootfs/hci.c` (→ `/bin/pbhci`) was extended:

* resolve the GPIO2 controller's **dynamic sysfs base** (`512` here) from
  `/sys/class/gpio/gpiochip*/device` → `48055000.gpio`, export `base+23`, set
  `direction=out`, drive `0`, `usleep(50000)`, drive `1`, wait 100 ms;
* read the pad, `GPIO2_OE`, `GPIO2_DATAIN` and `GPIO2_DATAOUT` through
  `/dev/mem` before and after the pulse;
* open `/dev/ttyS1` raw at 115200 8N1 (`~CRTSCTS`), write the one `HCI_Reset`,
  then print `UART2 LSR` and the uart2 pad values;
* read for `04 0e 04 01 03 0c 00` for 2 s; up to `--tries` fresh pulses.

The binary was pushed to `/tmp/pbhci` (the region-1 image still carries the
session-29 build; the source is the durable artefact).  Capture:
`docs/captures/wl1283-bt-probe-2026-11-14.txt`.

## 3. Result

| check | value | verdict |
|---|---|---|
| pad `0x4A10007C` | `0x00030003` | BT_EN mux 3 — **already GPIO on our image** |
| `GPIO2_OE` bit 23 | 0 | output |
| `GPIO2_DATAOUT` bit 23, before → after | 0 → 1 | **BT_EN really went high** |
| uart2_rx pad `0x11C` | `0x00004118` | mux 0, `INPUT_EN` set, pull-up |
| uart2_tx pad `0x11E` | `0x00030000` | mux 0 output |
| `UART2 LSR` after write | `0x60` | THRE = 1, TEMT = 1, DR = 0 — **command transmitted** |
| bytes received | none in 2 s × 3 pulses | **silent** |

The baseline (`--no-pulse`) reproduces the session-29 silence, confirming the
tool and the environment are the same in both cases.

## 4. What it means — and what it does not

**Does.**  `BT_EN` is necessary but **not sufficient** for the BT core to
answer HCI on this bench.  A session that wants a die-liveness signal from the
BT half must first establish that the *reference* BT stack answers here.

**Does not.**  Kill the power/TCXO/reset class for the WLAN.  Two facts block
that inference:

* The reference's WLAN driver runs the chip **live** on this unit (report 217),
  so VBAT/VIO/the 26 MHz TCXO are good *when the reference boot has run*.  The
  BT probe does not tell us what our Linux boot leaves powered.
* The reference's BT half was only ever decoded statically (report 216).  There
  is no capture of `btp-btmgr` answering HCI on this hardware, so "BT_EN high
  ⇒ the BT core runs" is an assumption, not a measurement.  The 32.768 kHz
  SLOW_CLK, in particular, has not been verified (see report 229 §4).

So the probe is **inconclusive for the WLAN**, and it should not be cited as
evidence that the SDIO controller is innocent.

## 5. What would settle it

Run the reference's own BT serial driver and manager on the minimal oracle, the
way report 217 ran `devnp-ti1283.so`: `devc-seromap-winbt` on `0x4806c000`
(`-b115200`, shift 2, irq 105), then `btp-btmgr` with `WL1283.bts`, drive
`BT_EN` through its devctl, and see whether `HCI_Reset` is answered there.  If
it is, the Linux side has a real BT bug to find; if it is not, BT is not a
liveness oracle on this unit at all and the lead closes.

## 6. Artefacts

* `rootfs/hci.c` — the BT_EN pulse + UART self-check (source only; the image
  was not rebuilt).
* `docs/captures/wl1283-bt-probe-2026-11-14.txt`.
* Traps 234 (below) and the existing 219/220.
