# 133 — The display works: the DISPC core clock was below the pixel clock

**Session 15 (absolute), Linux-era session 9.  2026-10-29.**
Build: `0x068C867F` (`local/images/kernel-0x068C867F-dssfck.img`).
Predecessor: `0x01F0F890` (everything the same except the clock fix) — glitchy.

The panel now shows a **clean, stable image**: solid white, solid red, and the
kernel console as text.  The last defect was not in the DSI link, the bridge or
the panel at all — it was the **DSS functional clock, which is also the DISPC's
core clock**.

---

## 1. The symptom that gave it away

The operator's observation, on a boot with a **black** framebuffer and no
probing:

> *"It's mostly a black background, with white horizontal lines shifting
> around, and some yellow/orange pixels near the top… I would expect a solid,
> stable black screen."*

That is the decisive framing.  With black content, a correct link can only show
black.  The corruption was being **generated**, not carried: content did reach
the panel (painting magenta gave magenta, painting white gave white), but only
the left ~5 % of each line was ever perfect and the rest carried sparse black
pixels in vertical stripes that moved.

Vertical stripes with a clean left edge are a **per-line timing failure**: the
display starts each line correctly and loses it as the line progresses.  That is
what a pixel pipeline serviced too slowly looks like — not a data/packet
problem.

## 2. Root cause

`/sys/kernel/debug/omapdss/clk` on the glitchy build (`0x01F0F890`):

```
- DSS -
FCK = 48000000
- DISPC -
dispc fclk source = FCK
fck   48000000
- DISPC-CORE-CLK -
lck   48000000   lck div 1
- LCD -
LCD clk source = PLL1:1
lck   100800000  lck div 1
pck   50400000   pck div 2
```

* the **pixel clock** (pck) is 50.4 MHz, from the DSS PLL (PLL1) / 2 — correct;
* the **DISPC core clock** is the DSS functional clock (FCK), which was
  **48 MHz** — *below* the pixel clock it has to service;
* the reference OS runs the same DSS at **100.8 MHz** (report 129 §7e noticed
  this and it was never resolved).

The boot log had been saying so all along:

```
dss_setup_default_clock: clk rate mismatch: 48000000 != 128000000
```

`dss_setup_default_clock()` asks for `fck_freq_max`, but the DSS fck is a
**clock gate** (`l3-dss-clkctrl:0000:8`, parent `dpll_per_m5x2_ck`) that does
not propagate `clk_set_rate` to its parent, so the request is silently dropped
and the clock stays at whatever the boot chain left (48 MHz — the PlayBook has
no u-boot to set a display clock).

## 3. The fix

Board-gated, in `dss_probe()` right after `dss_setup_default_clock()`: ask the
fck's **parent** for the reference's rate.

```c
if (of_machine_is_compatible("blackberry,omap4-winchester")) {
        struct clk *pc = clk_get_parent(dss->dss_clk);
        int rr = clk_set_rate(pc, 100800000);
        pr_err("pb dss: parent want 100800000 rr %d -> %lu, fck %lu\n", ...);
}
```

Result (`0x068C867F`):

```
DSS: pb dss: parent want 100800000 rr 0 -> 96000000, fck 96000000
```

```
- DSS -               FCK = 96000000
- DISPC -             dispc fclk source = FCK, fck 96000000
- DISPC-CORE-CLK -    lck 96000000, lck div 1
- LCD -               LCD clk source = PLL1:1, lck 100800000, pck 50400000, div 2
```

The clock tree rounded 100.8 MHz to **96 MHz**, which is comfortably above the
50.4 MHz pixel clock.  The picture is immediately correct.

The PER PLL itself was **not** reprogrammed — only its output dividers moved:

| reg | before | after |
|---|---|---|
| `4A008140` (DPLL_PER M/N) | `00000007` | `00000007` (unchanged) |
| `4A008150` (M4) | `00000822` | `00000802` |
| `4A008154` (M5, the DSS) | `00000023` | `00000003` |
| `4A008158` (M6) | `00000023` | `00000003` |
| `4A00815C` (M7) | `00000228` | `00000204` |

## 4. Evidence on the fixed build

* panel: **solid clean white**, then **solid clean red**, and the console text
  is legible on the panel (operator);
* `pb late scan: line 230 -> 267 -> 304` (37 lines/ms — unchanged, the pixel
  rate was never wrong);
* bridge reads still `8/8` (`0214=0 0218=1d 0220=10400880 0300=c0800000
  0500=0 049c=1 0230=ffffffff 0110=208e`);
* the backlight I²C still reads (`bl dump`: `1d = f9`) and `/dev/mmcblk1` is
  still present — the divider change did not break the other PER-PLL users.

## 5. What this closes

The display milestone **is met**: an image on the DSI → TC358764 → LVDS panel
(1024x600).  Three fixes were needed, in this order of discovery:

1. **the bridge delivery fix** (session 14): the VC0/HS null packet that starts
   the DSI clock — report 131;
2. **the LVDS porches** (`VP_HTIM1/2 = 0x00780001/0x00760400`) **and the Hitachi
   TCON table sent at bring-up** — without these the panel stayed black even
   though the bridge was programmed (report 132, and this session's `0xD9CB7D72`
   build);
3. **the DISPC core clock** at or above the pixel clock — this report.

Each one alone was necessary; with any one missing the panel is black or
corrupt.

## 6. Caveats and the proper long-term form

* The `clk_set_rate()` call runs in `dss_probe()`, i.e. **after some peripherals
  have probed**.  Any driver that computed a divider from the PER-PLL rate
  *before* the DSS probed will now be off.  The backlight (`LM3532`), the TCON
  SPI (`spidev`) and the eMMC all still work here, but this should become a
  *clock-initialisation* property rather than a probe-time side effect — e.g.
  the DT assigning the PER-PLL output rates, or the chainloader/firmware setting
  the DSS clock the way a normal OMAP bootloader does.  Recorded in
  `KNOWN-ISSUES.md`.
* The change is global to the PER PLL's output dividers.  eMMC is on M2, which
  did not move (`00000001`), so the storage path is untouched — worth re-checking
  when a root filesystem is added.
* `CONFIG_OMAP2_DSS_DEBUGFS` is now enabled in `kernel/winchester.config`: it is
  what produced the table in §2, and it costs nothing at runtime.
* Reading DSS/DSI registers from userspace **perturbs the live picture** on this
  board — repeatedly, the operator saw the image shift right after a `peek` of
  the DSI block or a `cat` of the DSS register dumps.  On the fixed build the
  picture is stable; do not probe while judging it.

## 7. Tooling changes

* `initramfs/init.c`: sends the Hitachi TCON table (high byte first, 450 kHz,
  100 ms after words 0/2/last) right after `dss_probe`'s display is up — the
  reference's own order (`winch_lcdctl 0x193c4`) — and mounts debugfs, dumping
  `omapdss/clk` and `omapdss/dispc` to kmsg at boot.
* `kernel/.../dss.c`: the board-gated PER-PLL rate request (§3).
* `kernel/winchester.config` + `kernel/patches/winchester.config` +
  `local/kernel-tree/.config`: `CONFIG_OMAP2_DSS_DEBUGFS=y`.
* Kernel patch re-exported at the end of the session: **4 169 lines / 32 files**
  (`dss.c` is the 32nd), verified against a fresh pristine tree.
* `kernel/.../dsi.c`, `tc358764.c`: `PPI_TX_RX_TA = 0x00040006` restored (the
  reference-value build `0xD2602864` showed it makes no difference to the
  picture); the corrected `VP_HTIM1/2` porches from report 132 stay.

## 8. Images from this continuation

| image | change | result |
|---|---|---|
| `0xD9CB7D72` | TCON table sent from `/init` | first mostly-correct image (magenta with noise) |
| `0xD2602864` | + reference `PPI_TX_RX_TA = 4` | not better; reverted |
| `0x95B03A6C` | + debugfs dump (DSS debugfs still off) | diagnostic only |
| `0x01F0F890` | + `CONFIG_OMAP2_DSS_DEBUGFS` | produced the `clk` table in §2 |
| **`0x068C867F`** | **+ DSS fck 48 → 96 MHz** | **clean, stable image** |
| **`0x8C146805`** | same source, rebuilt from the moved workspace with a repo-relative `CONFIG_INITRAMFS_SOURCE` | re-verified on the bench: **solid clean magenta** |
