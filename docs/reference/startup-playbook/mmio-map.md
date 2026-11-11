# MMIO map — the register blocks `startup-playbook` touches

Addresses: `VA = 0x80100000 + file offset`.  The list below is every base that
the binary materialises as a 32-bit constant with a `movw`/`movt` pair (401
pairs in total; the MMIO ones are listed here).  It is a *static* inventory:
a constant being present means the block is referenced somewhere, not that it is
written on every boot.

| base | OMAP4430 block | what the start-up does there |
|---|---|---|
| `0x48020000` | UART3 | console init (baud/format) |
| `0x48055000` | GPIO2 | bank init; WL_IRQ/WL_EN/BT_EN live here (gpio 53/54/55) |
| `0x48057000` | GPIO3 | bank init |
| `0x48059000` | GPIO4 | bank init |
| `0x4805B000` | GPIO5 | bank init (the bank our `devmem` poke aborted on — trap 158) |
| `0x4805D000` | GPIO6 | bank init |
| `0x48060000` | I2C3 | controller init |
| `0x4806A000` | UART1 | init |
| `0x4806C000` | UART2 | init (the WL1283's BT/HCI UART) |
| `0x4806E000` | UART4 | init |
| `0x48070000` | I2C1 | controller init (PMIC, charger, gauge) |
| `0x48072000` | I2C2 | controller init (touch at `0x65`) |
| `0x48098000` | MCSPI1 | controller init |
| `0x4809A000` | MCSPI2 | controller init |
| `0x480B4000` | MMC2 (+`0x100`) | eMMC host init |
| `0x480B8000` | MCSPI3 | controller init |
| `0x480BA000` | MCSPI4 | controller init (panel TCON) |
| `0x48242000` | ? | referenced, not decoded |
| `0x48281010`, `0x48281020` | ? | referenced, not decoded |
| `0x48350000` | I2C4 | controller init (sensors, WM8994) |
| `0x4A002000` | CTRL_MODULE_CORE | USB/PHY and general control bits |
| `0x4A004000` | CM1 | module modes / clock enables |
| `0x4A008000`, `0x4A009000` | CM2 | module modes / clock enables |
| `0x4A056000` | ? | referenced, not decoded |
| `0x4A064000` | ? | referenced, not decoded |
| `0x4A0AB000` | USB OTG (MUSB) | controller/PHY init |
| `0x4A100000` | CORE control / padconf | pad configuration (the board's pad table) |
| `0x4A304000` | PRM block | power/clock-domain state |
| `0x4A306000` | PRM | power/clock-domain state, reset status |
| `0x4A307000` | PRM (device/always-on) | power state |
| `0x4A30A000` | **SCRM** | **`AUXCLK1` = 12.8 MHz** and other SCRM fields (see `clocks-and-wdt.md`) |
| `0x4A310000` | GPIO1 | bank init (GPIO1_13 FAN5702 EN, GPIO1_14 touch power) |
| `0x4A314000` | **WDT2** | **58.6 s window, started** (see `clocks-and-wdt.md` §3) |
| `0x4A318000` | ? | referenced, not decoded |

Notes:

* The **pad table** lives at `0x80122750` (the GPIO records, decoded in
  `gpio-pins.md`) and the block at `0x801230b0` is a 20-byte-record table — most
  likely the pad configuration — whose layout is still open.
* There is **no direct TWL6030 MMIO**: the PMIC is reached over I²C1
  (`0x48070000`) through the I²C helper functions, so the PMIC register values
  are not in this inventory.
* The `?` rows are recorded rather than guessed.  They are cheap to resolve: each
  has its materialisation site, so a `movw/movt` scan plus the TRM chapter index
  names them.
* `0x4A100620`/`0x4A100628` are touched with `orr 0x14400000`/`0x0E000000`
  (`0x801028b4`…); those offsets are past the padconf table, i.e. in the CORE
  control module's other registers, not pads.
