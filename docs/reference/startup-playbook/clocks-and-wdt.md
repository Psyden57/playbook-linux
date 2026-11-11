# Clocks, SCRM and the watchdog

Addresses: `VA = 0x80100000 + file offset`.  Source:
`local/work/1917/ifs/proc/boot/startup-playbook`.

## 1. SCRM `AUXCLK1` (0x4A30A314) — the codec's MCLK1 source

**This is the finding that matters for the audio work.**  The start-up binary
programs `AUXCLK1` directly; our Linux kernel does not run this code, and the
difference is visible in the register (reports 203/204).

Two writers, both writing the whole register:

### 1a. `0x80100908` — AUXCLK1 off, divider 1

```
80100908  movw r3, #0xa000          ; r3 = 0x4a30a000 (SCRM)
8010090c  mov  r2, #0x10000         ; r2 = 0x00010000
80100910  movt r3, #0x4a30
80100914  str  r2, [r3, #788]       ; 0x4a30a314 = 0x00010000
80100918  bx   lr
```

`0x00010000` = divider field `1`, **gate bit 8 clear** → AUXCLK1 stopped,
divider set for ÷2.  This is the "off" state.

### 1b. `0x80107980` — AUXCLK1 = 0x00020100 (÷3, 12.8 MHz, enabled)

```
80107980  movw r3, #0xa000          ; r3 = 0x4a30a000
80107984  movw r2, #0x100           ; r2 = 0x00000100
80107988  movt r3, #0x4a30
8010798c  movt r2, #2               ; r2 = 0x00020100
80107990  str  r2, [r3, #788]       ; 0x4a30a314 = 0x00020100
80107994  bx   lr
```

Decoded against the upstream OMAP4 clock binding
(`omap44xx-clocks.dtsi`, `auxclk1_*` @ reg `0x314`): gate (bit 8) **on**,
source mux (bits 2:1) **0 = `sys_clkin_ck`**, divider (bits 19:16) **2 → ÷3**.
With `sys_clkin` = 38.4 MHz that is **12.8 MHz**, exactly the reference the
WM8994 FLL1 expects (report 201: `N = 7`, `K = 0xAE14`, `OUTDIV = 7` →
12.288 MHz = 256 fs @ 48 kHz).

A third function, `0x80107998`, writes the same `0x00020100` and then reads a
context field (`ldr r2,[r1,#0x24]`) — a map-and-check wrapper.

**Consequence for Linux.**  On our image a boot can leave `AUXCLK1 =
0x00000100` (÷1 = 38.4 MHz, gate on) until the codec driver's first
`set_sysclk(MCLK1, 12800000)` runs `clk_set_rate()`; the reference has
`0x00020100` from the start-up onward.  38.4 MHz is outside the WM8994 FLL's
0.032–13.5 MHz input range.  The correct Linux expression of this fact is an
`assigned-clock-rates = <12800000>` on `auxclk1_ck` (the DTS `sound`/codec node
already claims `<&auxclk1_ck>`), or an explicit `clk_set_rate()` before the
codec interface is enabled — **not** a raw register poke.

Caveat from session 40: forcing the divider (÷3…÷8) on the live part did **not**
make the FLL lock either, so this is a real defect but not proven to be the
whole cause.  Recorded here because it is the reference's own value.

### 1c. Other SCRM writes

| address | value | site | note |
|---|---|---|---|
| `0x4A30A314` | `0x00010000` | `0x80100908` | AUXCLK1 off, div 1 |
| `0x4A30A314` | `0x00020100` | `0x80107980`, `0x80107998` | AUXCLK1 on, ÷3 = 12.8 MHz |
| `0x4A30A31C` | `0x00000100` | `0x80100d20` | next AUXCLK register (AUXCLK2/3), gate on, ÷1 |
| `0x4A30A104` | `0x00000000` | `0x80100a48`, `0x80105ae0` | SCRM block +0x104 |
| `0x4A30A104` | `0x00160016` | `0x80105288` | SCRM block +0x104, two 16-bit fields |

`0x4A30A104` is not decoded yet (it is inside the SCRM block, not one of the
`AUXCLKn` registers at `0x310`–`0x31C`).

**Important correction:** earlier session notes credited `startup-playbook` with
the `AUXCLK1 = 0x00020100` write at "image 0x7980".  That is this function
(`0x80107980` = file `0x7980`) — the note was right, and the audio driver
(`deva-ctrl-omap4abe-wm8994.so`) *also* re-asserts the gate bit (report 201).
So both the start-up and the driver expect AUXCLK1 to already be configured.

## 2. Clock-manager writes (summary)

The binary touches `CM1` (`0x4A004000`), `CM2` (`0x4A008000`, `0x4A009000`) and
`PRM` (`0x4A304000`, `0x4A306000`, `0x4A307000`) through a small set of helpers
that set module modes and poll status.  Examples seen:

```
0x8010156c  CM1 0x4A004420 = 1, 0x4A004400 = 2, poll 0x4A004400 & 0x100
0x801015b0  CM1 0x4A004500 = 2, poll 0x4A004500 & 0x3d00
0x80101618  CM1 0x4A004500 = 3          ; (r6+0x500) after the wait
0x80101878  CM2 0x4A008200 = 0, 0x4A008338 = 1, 0x4A008340 = 2, 0x4A008358 = 2 ...
0x80100884  PRM 0x4A306110 = 5
0x8010786c  PRM 0x4A306110 = 7
```

These are the OMAP4 module-mode / clock-domain enables for the peripherals the
start-up brings up (UART, SPI, I²C, MMC, GPIO).  They are enumerated in
[`mmio-map.md`](mmio-map.md); decoding each one to a TRM register name is still
open.

## 3. WDT2 — the 58.6 s window, set by the reference

`0x801036a4`–`0x801037e8` (two nearly identical blocks, `0x801036e4`…):

```
WSPR (0x4A314048) = 0xAAAA      ; stop
poll WWPS (0x4A314034) == 0
WSPR = 0x5555                   ; release the stop
poll WWPS
WCLR  (0x4A314024) = 0
poll WWPS
WLDR  (0x4A31402C) = 0xFFE2B400
poll WWPS
WTGR  (0x4A314044) = 0xFFFFFFFF
poll WWPS
WCRR  (0x4A314028) = 0xFFE2B400
poll WWPS
WSPR = 0xBBBB                   ; start
poll WWPS
WSPR = 0x4444
poll WWPS
```

`WLDR = 0xFFE2B400` at 32.768 kHz is **58.59 s**
(`(0xFFFFFFFF − 0xFFE2B400) / 32768`).  This is exactly the policy session 31
reproduced in the kernel (`WLDR = 0xFFE2B400`, `WSPR` stop/start with a `WWPS`
poll, pet every 10 s) — so the kernel side matches the reference, and the
loader's 7.8 s window is only the pre-kernel window.

The `bl`-visible helpers around it (`0x80103680`…`0x801037e8`) are the
watchdog start/stop/posted-write primitives; the poll loops are the TRM's
required `WWPS` waits (session 31's trap 115).
