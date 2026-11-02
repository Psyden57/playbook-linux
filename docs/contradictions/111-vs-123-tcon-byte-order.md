# The Hitachi TCON table byte order: low-first or high-first?

**Status: resolved 2026-10-29 (session 15) — high byte first, i.e. the static-RE
answer.**  The bench now shows that the byte order does not decide "works vs
does not work"; it flips the polarity.  See "Session 15" at the end.

## The claims

**Low-first (report 111 §3.5).**  The 9-word Hitachi table is recorded as the
wire sequence `29 00 25 00 40 08 AD 00 30 04 5F 38 A4 3C 09 34 41 40`, i.e. each
u16 low byte first.

**High-first (report 123 §2, 2026-10-28).**  Static RE of `winch_lcdctl`'s SPI
send found a byte swap before the transfer
(`0x1c51c: lsr r2,r3,#8` / `0x1c520: orr r3,r2,r3,lsl #8` / `0x1c524: strh`),
so the wire order is high byte first: `00 29 00 25 08 40 00 AD 04 30 38 5F 3C A4
34 09 40 41`.

The table in memory (vaddr `0x2e578`) is unambiguous:
`29 00 25 00 40 08 AD 00 30 04 5F 38 A4 3C 09 34 41 40` (low-first u16s).  The
disagreement is only what the driver puts on the wire.

## Bench evidence (report 127)

On build `0x2F128BCE`, with the bridge programmed and the host streaming:

* `spi tcon` (low-first) → the panel **held a full white field** (static; it did
  not follow the framebuffer).
* `spi tconbe` (high-first) → a **brief flash with the left half white/grey and
  the right half black/lines** (the first partial live image), then black.

Neither is a stable live image, so the bench does not yet settle which order is
correct.  One reading is that high-first is the real table (partial video
appears) and low-first merely leaves the TCON in a white test state; another is
that low-first is the real table and the white field is the panel's "no valid
video yet" state.

## Session 14 update — the previous evidence is weaker than it reads

Three things changed the picture (reports 129, 131):

* The "partial live image" is **withdrawn**: with the framebuffer painted
  magenta the flash was **grey**, so it was never our content.
* Bridge delivery was broken at the time (the DSI HS clock was never started);
  the bridge was in fact **unprogrammed** during both tests above.  So neither
  observation was made with a working LVDS output.
* The decisive test the previous session proposed was run: paint **magenta
  first**, then send the **low-first** table.  The panel stayed **black** — not
  magenta, and not the session-13 white either.  The same test with the bridge
  now programmed (`LV_CFG = 1`) does not reproduce the white.

So the "low-first gives white" observation depends on the bridge being
unprogrammed, and with the bridge programmed **neither order shows an image**.
Neither order can be called correct yet.

## What would settle it

1. A pad-level capture of the MCSPI4 `simo`/`clk` lines during
   `winch_lcdctl -s splash` on the reference (the definitive measurement).
2. A careful re-read of the SPI send path with the transfer buffer inspected at
   the `ioctl`/`devctl` boundary — the static RE must show which byte is at
   offset 0 of the buffer actually handed to the driver.
3. Once delivery is confirmed (`kmsg pb selftest` on `0x5DC2711F`), try both
   orders with the video running and a colour that contrasts with the TCON's
   default state.

Do not "fix" this by choosing the order that looks better; record the capture.

## Session 15: settled — both orders work, high-first is the reference's

Item 3 above is now done on the bench.  With the bridge **correctly programmed**
(the `VP_HTIM1/2` porch bug fixed in session 15, report 132 §5) and the
framebuffer painted magenta:

| order | result (operator) |
|---|---|
| `spi tconbe` (high byte first) | a ~1 cm **magenta** band at the top, then alternating **black/white** lines and static |
| `spi tcon` (low byte first) | *"the same sort of pattern … but **green** instead of magenta, and the **white and black swapped** places"* |

So the order toggles colour (magenta↔green = a red/blue swap) and inversion
(black↔white); it is not a "the table is or is not accepted" discriminator.  The
static RE (report 123 §2) is the only evidence about **what the reference
actually sends**, and it says high byte first
(`winch_lcdctl 0x1c51c/0x1c520` swaps before the transfer).  **Keep high-first**
(`spi tconbe`, and whatever we put in the kernel's boot path).

The remaining glitch — alternating black/white lines after the first band — is
therefore **not** a byte-order symptom; it is the frame-corruption problem of
report 132 §11 (next: the TCON table in the boot path, then the DISPC deltas).
The low-first result is useful in a different way: the inversion it causes is a
hint that one of the TCON words carries a polarity/inversion bit and that the
halves of that word are being swapped, so a future capture should look at that
word rather than at the whole table.

## Follow-up (later session 15) — the frame glitch is gone

The alternating black/white lines were **not** a TCON-byte-order symptom: report
133 found the last defect (the DISPC core clock below the pixel clock) and the
panel now shows a clean, stable image.  The byte-order answer is unchanged:
**high-first** is what the reference sends and what the boot path uses.  This
file is closed.
