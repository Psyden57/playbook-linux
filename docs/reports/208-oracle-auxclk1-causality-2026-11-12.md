# 208 — Oracle causality test: SCRM AUXCLK1 is the WM8994 MCLK1 (session 43)

Date: 2026-11-12 (session 43).  Bench: the unit is on the **minimal 1917 oracle**
(report 207) — `qconn` on `169.254.0.1:9090`, root ssh.  Reference audio HAL
(`io-audio -d omap4abe-wm8994 aif=0,secure_capture -o enable_buffering`) running.
Volatile only: SCRM AUXCLK1 and codec registers were written and restored; no
NVRAM/RPMB/MCT write, no flash.

## 0. Headline

**A causal test on the live reference proves the codec's MCLK1 is the SCRM
`AUXCLK1` / `fref_clk2_out` clock.**  With `AUXCLK1 = 0x00020100` the WM8994
FLL1 locks (`R0x732 = 0x6020`); with only the AUXCLK1 **gate bit cleared**
(`0x00020000`), a codec software reset followed by the reference's exact FLL1
power-up sequence leaves it **unlocked** (`R0x732 = 0x6000`).  Restoring
`AUXCLK1` and restarting `io-audio` re-locks it.

This is the first *causal* confirmation (not just register equality) that
`AUXCLK1` is the codec clock.  It means our Linux model is right — the failure
is that the identical `AUXCLK1` value does not produce a clock at the pin under
Linux, exactly as report 206 concluded, now with the model itself proven.

## 1. Reference baseline (live, io-audio running)

```
SCRM AUXCLK1  0x4A30A314 = 0x00020100   (gate on, src sys_clkin, /3 = 12.8 MHz)
CORE pad 0x19C 0x4A10019C = 0x41000000  (low half 0x0000 = fref_clk2_out mux 0)
codec R0x200 = 0x0010   R0x210 = 0x0083   R0x302 = 0x4000
codec R0x220 = 0x0005   R0x221 = 0x0700   R0x222 = 0xAE14
codec R0x223 = 0x00E0   R0x224 = 0x0C80   R0x732 = 0x6020  (FLL1_LOCK_STS = 1)
```

The codec FLL file is byte-identical to our Linux image's (report 206).  Full
codec dumps (`0x200-0x240`, `0x300-0x310`, `0x730-0x740`) are in
`docs/captures/wm8994-oracle-live-2026-11-12.txt`.

## 2. The causality tests

**A. AUXCLK1 on — the toggle works.**
`R0x220 <- 0x0000` → `R0x732 = 0x6000` (unlocked); `R0x220 <- 0x0005` →
`R0x732 = 0x6020` (locked).  So the FLL can be disabled/re-enabled from software
and the lock bit tracks it.

**B. AUXCLK1 off — a stale/sticky lock bit.**  With `AUXCLK1 = 0x00020000`, a
plain disable/re-enable left `R0x732 = 0x6020` even though `R0x220` read back
`0x0000`.  The lock detector does not update when its reference clock is absent
(the bit freezes at its last value).  A bare toggle is therefore **not** a valid
test with the clock removed.

**C. AUXCLK1 off + codec reset + full FLL power-up — decisive.**
```
AUXCLK1 <- 0x00020000
R0x000 <- 0x0001        (software reset; R0x732 reads 0x6000)
R0x220 <- 0x0000 ; R0x221 <- 0x0700 ; R0x223 <- 0x00E0
R0x220 <- 0x0004 ; R0x222 <- 0xAE14 ; delay(100ms)
R0x220 <- 0x0001 ; delay(500ms)
R0x732 = 0x6000         <-- NO LOCK, with the reference clock gated off
```
Restoring `AUXCLK1 = 0x00020100` and restarting `io-audio` returns
`R0x732 = 0x6020`.  The codec's **control interface kept working throughout**
(the writes returned ok and read back), so the failed lock is attributable to
the missing reference clock, not a frozen codec.

## 3. SoC capture (live reference)

```
SCRM AUXCLK1/2/3  0x4A30A314 = 0x00020100, 0x4A30A318 = 0, 0x4A30A31C = 0
CORE pads 0x190 = 0x000f0003  0x194 = 0x00000008  0x198 = 0x00000000  0x19C = 0x41000000
CM2 DPLL_PER  0x4A008140 = 0x00000007
              0x4A008144 = 0x00000001
              0x4A008148 = 0x00000001
              0x4A00814c = 0x00002801
              0x4A008150 = 0x00000808
              0x4A008154 = 0x0000000d
              0x4A008158 = 0x0000022c
              0x4A00815c = 0x00000329   (the "CM_DIV_M5 = 0x329" of the display path)
```

## 4. Where this leaves the MCLK1 question

* **Confirmed:** the codec's MCLK1 is SCRM `AUXCLK1` → `fref_clk2_out`; the
  reference's 12.8 MHz reference is real; our Linux `AUXCLK1 = 0x00020100` is
  the same value.
* **Still open:** why the identical register does not produce a clock at the pin
  under Linux.  The remaining difference is below the registers or dynamic
  (clock-domain/power state, pad output buffer, or write sequencing).  The
  oracle now lets us compare the *full* SoC clock state (CM1/CM2/SCRM/pads/PMIC)
  live and then at the same points on Linux.
* Next: flash the Linux image back (`boot/chainloader/kernel.img` at `0x825`),
  capture the same register set, and diff — with the model confirmed, a
  difference (if any) is the answer; if there is none, the fault is the pad
  output/domain state, which the oracle can probe by toggling on a working
  system.

## 5. Tooling notes (durable)

* The WM8994 **write ABI** is the reference driver's 20-byte inline `devctl`
  struct `{addr,2,4,1,reg,val}` with reg/val big-endian — **not** `devctlv` with
  iovecs (those writes fail).  `tools/pbi2c` now has `wr16r` for this;
  `qnx-i2c.c` documents it.
* `memdump3` and `memw32` (built `-static` with the QNX toolchain) and `pbi2c`
  live in **`/accounts/devuser/bin`** (the UFS), so they survive reboots — do
  not push them to `/tmp`.
* The capture is `docs/captures/wm8994-oracle-live-2026-11-12.txt`.
