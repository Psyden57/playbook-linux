# 153 — The red-LED blink code on the PlayBook (OMAP4430): routine, register, pattern table, meaning

Date: 2026-10-31
Status: **complete for all five questions.**
Scope: **STATIC RE ONLY.** No device, no USB, no `/dev/ttyACM0`, no ssh, no
network, no flashing. Only bytes already in this workspace were read
(`tools/env.sh` → `arm-linux-objdump` 2.43.1, plus local `python3` for
byte/word scans and for replaying the blink emitter's arithmetic).
Only file created: this report.

Evidence markers: **[P]** proven from bytes (address + quoted instruction
words / register values) · **[I]** inferred (reasoning stated) · **[U]**
unresolved from the bytes available.

---

## 0. Headline answers

| Q | Answer |
|---|---|
| **1. How is the LED driven?** | **Not** by an OMAP GPIO. The red LED is a **FAN5702 I²C LED driver at 7-bit slave address `0x36`** (8-bit form `0x6C`), on an OMAP4 I²C master. The bootloader's LED primitive is the pair **`0x800095D8` = LED on / `0x800095B4` = LED off**, which set a single channel-enable bit (register `0x10` bit 1) through `0x800096CC` → `0x80009A2A` (I²C write) / `0x80009AC4` (I²C read). A GPIO1–GPIO6 base table exists in the image (`0x8001C88C`, `0x80008CFC`) but is **referenced by nothing**. |
| **2. The pattern table** | There is no table of `1,2,2`. The pattern is **generated from the bits of the error code** by `0x80014C98`: for each bit from the highest set bit down to bit 0, a `1` emits *on 20 units, off 20 units* and a `0` emits *off 40 units*; then a trailing *off 200 units*; the whole sequence repeats for ever. One unit = 100 × `delay(100000)` at `0x80014D56`. |
| **3. Which code path** | The blink is called **instead of** the halt. Every `bl 0x80014C98` with level `0` (all four `0x5B` sites) **never returns**, so the `bl 0x800094BE` that follows at `0x80014A5E` — the "PMIC 0x90 + `b .`" halt of reports 150/151 — is **dead code** on those paths. |
| **4. Meaning** | The measured **1-2-2** is produced by **exactly one code in the whole emitter: `0x5B` = 91 decimal** (bits `1 0 1 1 0 1 1`). `0x5B` is the stage's *"RPMB / NV record subsystem unavailable or malformed"* error, emitted from four sites, all in the NV-record/RPMB path. No other code in the image gives 1-2-2. |
| **5. Does the blink loop reset?** | **No.** It is a pure delay loop that **kicks WDT2 100 times per unit** (48 000 kicks per cycle). The SoC is neither halted nor reset; the pattern repeats from the software loop alone, for ever. The observed 7.35 s period is the loop period, not a reboot period. |

---

## 1. Inputs, base, and method

* `emmc-first1mb.bin`, 1 048 576 bytes, address = file offset + `0x80000000`
  (as given, and as confirmed by report 151 §0.2).
* `bootrom-0x40028000.bin`, 49 152 bytes, base `0x40028000` (checked for the
  LED; see §1.5).
* Reports 150 and 151 were used only to locate the halt / PMIC / verification
  paths (§4).

Disassembly commands are in §7. Thumb regions were decoded with
`-M force-thumb`, ARM islands without it; the relevant code is all Thumb.

---

## 2. Q1 — How the red LED is driven on this board

### 2.1 The LED primitives [P]

Two two-instruction-level entry points exist, and they are the *only* things in
the 1 MiB that switch an indicator on and off:

```
800095b4: 4837      ldr  r0,[pc,#220]  @ (0x80009694) = 0x80025E78
800095b6: b510      push {r4,lr}
800095b8: 7800      ldrb r0,[r0,#0]              ; r0 = LED "mode" byte
800095ba: b108      cbz  r0,0x800095c0           ; mode 0 -> proceed
800095bc: 2801      cmp  r0,#1
800095be: d10a      bne.n 0x800095d6             ; mode >= 2 -> RETURN, no hardware access
800095c0: 2300      movs r3,#0
800095c2: 2101      movs r1,#1
800095c4: 461a      mov  r2,r3
800095c6: 4618      mov  r0,r3
800095c8: f7ff ffe8 bl   0x8000959c             ; *(u32*)0x80025E7C = 1   (state = "on")
800095cc: e8bd 4010 ldmia.w sp!,{r4,lr}
800095d0: 2100      movs r1,#0
800095d2: 4608      mov  r0,r1
800095d4: e7ba      b.n  0x8000954c             ; LED_apply(onoff = 0)   -> OFF
```

```
800095d8: 482e      ldr  r0,[pc,#184]  @ (0x80009694) = 0x80025E78
800095da: b510      push {r4,lr}
800095dc: 7800      ldrb r0,[r0,#0]
800095de: b108      cbz  r0,0x800095e4
800095e0: 2801      cmp  r0,#1
800095e2: d10a      bne.n 0x800095fa             ; mode >= 2 -> RETURN
800095e4: 2300      movs r3,#0
800095e6: 2101      movs r1,#1
800095e8: 461a      mov  r2,r3
800095ea: 4618      mov  r0,r3
800095ec: f7ff ffd6 bl   0x8000959c             ; *(u32*)0x80025E7C = 1
800095f0: e8bd 4010 ldmia.w sp!,{r4,lr}
800095f4: 2101      movs r1,#1
800095f6: 2000      movs r0,#0
800095f8: e7a8      b.n  0x8000954c             ; LED_apply(onoff = 1)   -> ON
```

So `0x800095D8` = **turn the indicator on**, `0x800095B4` = **turn it off**, and
both are gated on the mode byte at **`0x80025E78`** (0 and 1 both permitted;
≥2 means "no LED hardware, do nothing").

`0x8000954C` is the mode dispatcher [P]:

```
8000954c: b510      push {r4,lr}
8000954e: 460c      mov  r4,r1                  ; r4 = onoff (1 = ON)
80009550: b120      cbz  r0,0x8000955c
80009552: 4601      mov  r1,r0
80009554: 484e      ldr  r0,[pc,#312]           ; 0x800138C8 (a format string; see §2.6)
80009556: 2200      movs r2,#0
80009558: f00d fe55 bl   0x80017206            ; only reached for r0 != 0
8000955c: 484d      ldr  r0,[pc,#308]           ; 0x80025E78
8000955e: 494d      ldr  r1,[pc,#308]           ; 0x80025E78
80009560: 7800      ldrb r0,[r0,#0]             ; mode
80009562: 1d09      adds r1,r1,#4               ; r1 = &state (0x80025E7C)
80009564: b148      cbz  r0,0x8000957a           ; ------- mode 0 -------
80009566: 2801      cmp  r0,#1
80009568: d117      bne.n 0x8000959a            ; mode >= 2 -> nothing
8000956a: 6808      ldr  r0,[r1,#0]             ; ------- mode 1 -------
8000956c: 2c01      cmp  r4,#1
8000956e: b2c0      uxtb r0,r0
80009570: d10f      bne.n 0x80009592
80009572: ...       b.w  0x800097d2            ; mode1 + ON
8000957a: 6808      ldr  r0,[r1,#0]             ; ------- mode 0 -------
8000957c: 2c01      cmp  r4,#1
8000957e: b2c0      uxtb r0,r0
80009580: d103      bne.n 0x8000958a
80009582: ...       b.w  0x800096f4            ; mode0 + ON
8000958a: ...       b.w  0x80009720            ; mode0 + OFF
80009592: ...       b.w  0x80009804            ; mode1 + OFF
```

The state word `0x80025E7C` is therefore **always 1** on the LED path (both
`0x800095B4` and `0x800095D8` store 1 before dispatching), i.e. only
**channel 1** of the driver is ever addressed.

### 2.2 Mode 0 = FAN5702 at I²C `0x36` — the register set [P]

```
800096f4: b510      push {r4,lr}
800096f6: 4604      mov  r4,r0                 ; r4 = state (always 1 here)
800096f8: 2010      movs r0,#16                ; register 0x10
800096fa: f7ff ffcf bl   0x8000969c            ; v = i2c_read(0x10)
800096fe: 07e1      lsls r1,r4,#31             ; bit0 of state
80009700: d001      beq.n 0x80009706
80009702: f040 0002 orr.w r0,r0,#2             ;  set bit1
80009706: 07a1      lsls r1,r4,#30             ; bit1
80009708: d501      bpl.n 0x8000970e
8000970a: f040 0004 orr.w r0,r0,#4             ;  set bit2
8000970e: 0761      lsls r1,r4,#29             ; bit2
80009710: d501      bpl.n 0x80009716
80009712: f040 0008 orr.w r0,r0,#8             ;  set bit3
80009716: e8bd 4010 ldmia.w sp!,{r4,lr}
8000971a: 4601      mov  r1,r0
8000971c: 2010      movs r0,#16
8000971e: e7d5      b.n  0x800096cc            ; i2c_write(0x10, v)
```
`0x80009720` is the mirror image (`bic.w r0,r0,#2 / #4 / #8` = clear the same
bits). So **register `0x10` bit 1 = the channel that is blinked**.

The register accessors [P]:

```
8000969c: b511      push {r0,r4,lr}            ; 0x8000969C(reg) -> returns one byte
8000969e: 2000      movs r0,#0
800096a0: b081      sub  sp,#4
800096a2: 240d      movs r4,#13                ; 13 retries
800096a4: 216c      movs r1,#0x6c              ; ***** 8-bit I2C address 0x6C *****
800096a6: aa01      add  r2,sp,#4
800096a8: 9000      str  r0,[sp,#0]            ; read buffer = 0
800096aa: 4838      ldr  r0,[pc,#224]          ; 0x800267C0 = transaction descriptor
800096ac: f000 f85a bl   0x80009764            ; build msg {addr=0x6C, n_prefix=1, p=&reg}
800096b0: 4836      ldr  r0,[pc,#216]          ; 0x800267C0
800096b2: 2201      movs r2,#1
800096b4: 4669      mov  r1,sp
800096b6: f000 fa05 bl   0x80009ac4            ; I2C READ
...
800096cc: b513      push {r0,r1,r4,lr}         ; 0x800096CC(reg, value)
800096ce: 240d      movs r4,#13
800096d0: 482e      ldr  r0,[pc,#184]          ; 0x800267C0
800096d2: 216c      movs r1,#0x6c              ; 8-bit I2C address 0x6C
800096d4: 466a      mov  r2,sp                 ; &reg
800096d6: f000 f845 bl   0x80009764
800096da: 482c      ldr  r0,[pc,#176]          ; 0x800267C0
800096dc: 2201      movs r2,#1
800096de: a901      add  r1,sp,#4               ; &value
800096e0: f000 f9a3 bl   0x80009a2a            ; I2C WRITE
```

`0x80009764` builds the 32-byte descriptor at **`0x800267C0`** [P]:

```
80009764: b570      push {r4,r5,r6,lr}
80009766: 460e      mov  r6,r1
80009768: 2120      movs r1,#32
8000976a: 4604      mov  r4,r0
8000976c: 4615      mov  r5,r2
8000976e: f00f eb22 blx  0x80018db4           ; memset(desc, 0, 32)
80009772: 2003      movs r0,#3
80009774: 8026      strh r6,[r4,#0]             ; +0x00  u16  I2C address byte (0x6C / 0x72)
80009776: 6060      str  r0,[r4,#4]             ; +0x04  bus index = 3
80009778: 2000      movs r0,#0
8000977a: 2101      movs r1,#1
8000977c: 60a0      str  r0,[r4,#8]
8000977e: 7321      strb r1,[r4,#12]            ; +0x0C  number of prefix bytes = 1
80009780: e9c4 5004 strd r5,r0,[r4,#16]         ; +0x10  pointer to the prefix byte
80009784: 61a0      str  r0,[r4,#24]
80009786: 61e0      str  r0,[r4,#28]
```

and `0x80009A2A` / `0x80009AC4` are the transfer engines; the register offsets
they write identify the IP unambiguously [P]:

```
80009a44: 8839      ldrh r1,[r7,#0]
80009a46: ea4f 0651 mov.w r6,r1,lsr #1          ; r6 = 0x6C >> 1 = 0x36  (7-bit slave address)
...
80009a5e: f8a8 0098 strh.w r0,[r8,#152]         ; 0x98 = I2C_CNT = prefix + data length
80009a62: f8a8 60ac strh.w r6,[r8,#172]         ; 0xAC = I2C_SA  = 0x36
80009a66: f248 6103 movw r1,#0x8603
80009a6a: f8a8 10a4 strh.w r1,[r8,#164]         ; 0xA4 = I2C_CON = 0x8603  (EN|MST|TRX|STT|STP)
80009a74: f814 1b01 ldrb.w r1,[r4],#1
80009a7a: f7ff ffb3 bl   0x800099e4             ; push one byte
80009a7e: 2800      cmp  r0,#0
...
80009a9c: 4650      mov  r0,sl                 ; -1 on failure
```

and the "clear FIFO / poll status" side:

```
80009a06: f8a2 509c strh.w r5,[r2,#156]         ; 0x9C = I2C_DATA
80009a0e: f8a1 0088 strh.w r0,[r1,#136]         ; 0x88 = I2C IRQ status (write-1-to-clear)
80009a1a: f8a5 0088 strh.w r0,[r5,#136]
80009a22: f8a0 3094 strh.w r3,[r0,#148]         ; 0x94 = I2C_BUF  (0x4040: TX-FIFO clear)
```

**Conclusion [P]:** the LED driver is an I²C slave at **7-bit address `0x36`**,
whose **register `0x10` bits 1/2/3** enable three channels and whose
**registers `0x30`, `0x40`, `0x50`** take **6-bit** numbers (`and #0x3f` at
`0x8000974C`/`0x80009754`/`0x8000975C`) — i.e. per-channel current settings.

### 2.3 The part is the FAN5702 [P, from the workspace's own driver]

`local/proprietary/device-binaries/led-fan5702.so` is a QNX shared object with
the build string

```
NAME=led-fan5702.so
DESCRIPTION=Fairchild Semiconductor FAN5702 I2C LED Driver
DATE=2011/05/27-14:03:49-EDT
```

and symbols `fan5702_probe`, `fan5702_init`, `fan5702_fini`, `led_write_reg`,
`led_read_reg`, `led_update_reg`, plus the PPS attribute strings
`red::on` / `red::off` / `green::on` / `green::off` / `blue::on` / `blue::off`
and log lines "turn on RED_LED" / "turn on GREEN_LED" / "turn on BLUE_LED".
[P] **The PlayBook's indicator is a FAN5702 three-channel I²C LED driver, and
the OS addresses the three channels as red/green/blue.**

Combining that with §2.1/§2.2: the bootloader writes register `0x10` bit 1
only, i.e. it lights **one** of the three channels. Given the operator's
observation that the blink is **red**, [I] channel 1 (register `0x10` bit 1) is
the red channel on this board. This is consistent, not proof — the channel →
colour assignment is a board wiring detail.

### 2.4 Mode 0 vs mode 1, and the probe [P]

`0x800095FC` decides the mode:

```
800095fc: b510      push {r4,lr}
800095fe: 2402      movs r4,#2                 ; default: no LED at all
80009600: 2100      movs r1,#0
80009602: 2010      movs r0,#16
80009604: f000 f862 bl   0x800096cc            ; i2c_write(slave 0x36, reg 0x10, 0)
80009608: b908      cbnz r0,0x8000960e         ; failed -> try the other device
8000960a: 2400      movs r4,#0                 ; ***** mode 0: FAN5702 @ 0x36 *****
8000960c: e004      b.n  0x80009618
8000960e: 2000      movs r0,#0
80009610: f000 f8c0 bl   0x80009794            ; single-byte command 0x00 to slave 0x39
80009614: b900      cbnz r0,0x80009618
80009616: 2401      movs r4,#1                 ; ***** mode 1: device @ 0x39 *****
80009618: 4620      mov  r0,r4
```

`0x80009794` uses the **second** address byte, `0x72` (7-bit **`0x39`**), with
**no** register prefix (`strb r1,[r0,#12]` writes 0 to the byte-count field at
`0x800097AE`) and a single command byte:

```
80009794: b531      push {r0,r4,r5,lr}
80009796: 240d      movs r4,#13
80009798: 4827      ldr  r0,[pc,#156]          ; 0x800267E0 (a second descriptor)
8000979a: 2120      movs r1,#32
8000979c: f00f eb0a blx  0x80018db4            ; memset(desc2, 0, 32)
800097a0: 4825      ldr  r0,[pc,#148]
800097a2: 2172      movs r1,#0x72              ; ***** 8-bit I2C address 0x72 *****
800097a4: 8001      strh r1,[r0,#0]
800097a6: 2103      movs r1,#3
800097a8: 6041      str  r1,[r0,#4]            ; bus index 3
800097aa: 2100      movs r1,#0
800097ac: 6081      str  r1,[r0,#8]
800097ae: 7301      strb r1,[r0,#12]           ; 0 prefix bytes
...
800097be: f000 f934 bl   0x80009a2a            ; I2C WRITE, 1 byte, no register address
```

Mode 1's ON/OFF values are single bytes `0x80`/`0x9F` (channel A),
`0x60`/`0x7F` (channel B), `0x40`/`0x5F` (channel C):

```
800097d2: b570      push {r4,r5,r6,lr}
800097d4: 2400      movs r4,#0
800097d6: 4605      mov  r5,r0
800097d8: 07c0      lsls r0,r0,#31             ; state bit0
800097da: d003      beq.n 0x800097e4
800097dc: 2080      movs r0,#0x80              ; ON
800097de: f7ff ffd9 bl   0x80009794
...
800097e8: 2060      movs r0,#0x60
...
800097f6: 2040      movs r0,#0x40
; and the OFF twin at 0x80009804: 0x9F / 0x7F / 0x5F
```

Both modes therefore give "one channel on / one channel off" from the same
state bit, and the blink engine (§3) is mode-independent.

### 2.5 The LED module is initialised very early — before `boot main` [P]

There is exactly **one** `bl` to the LED init `0x8000961C` in the whole 1 MiB,
at `0x800159E0`, inside the routine entered from the ARM reset stub:

```
80019260: fafff1d1 blx  0x800159ac            ; reset-path step 8 (report 151 §1.2)
...
800159ac: b538      push {r3,r4,r5,lr}
800159ae: 2500      movs r5,#0
800159b0: 4c2a      ldr  r4,[pc,#168]          ; 0x80015A5C = status block
800159b2: 482b      ldr  r0,[pc,#172]          ; 0x84D93A96 (status magic)
800159b4: 6821      ldr  r1,[r4,#0]
800159b6: 4281      cmp  r1,r0
800159b8: d008      beq.n 0x800159cc
...
800159d8: f7f3 ffc0 bl   0x8000995c
800159dc: f7fd fe5f bl   0x8001369e            ; PRM/WDT1 setup (report 151 §5.2 note)
800159e0: f7f3 fe1c bl   0x8000961c            ; ***** LED init *****
800159e4: 481f      ldr  r0,[pc,#124]
800159e6: 2101      movs r1,#1
800159e8: 6001      str  r1,[r0,#0]
800159ea: f7f3 fd74 bl   0x800094d6
800159ee: f7f4 f988 bl   0x80009d02
800159f2: f7fe fa6f bl   0x80013ed4
800159f6: f000 fa7d bl   0x80015ef4
800159fa: b118      cbz  r0,0x80015a04
800159fc: 2117      movs r1,#23              ; 0x17  -> blink code 0x17
800159fe: 2000      movs r0,#0
80015a00: f7ff f94a bl   0x80014c98            ; ***** blink engine, code 0x17 *****
```

`0x8000961C` itself [P]:

```
8000961c: b510      push {r4,lr}
8000961e: 2200      movs r2,#0
80009620: 491b      ldr  r1,[pc,#108]          ; 0x800138C8; subs r1,#0x24 -> 0x800138A4
80009622: 4c1d      ldr  r4,[pc,#116]          ; 0x0000D50D  (hook id)
80009624: 3924      subs r1,#36
80009626: 4620      mov  r0,r4
80009628: f7ff fef2 bl   0x80009410            ; register(id 0xD50D, handler 0x800138A4, 0)
8000962c: 2100      movs r1,#0
8000962e: 4620      mov  r0,r4
80009630: f7ff fd92 bl   0x80009158
80009634: f7ff ffe2 bl   0x800095fc            ; probe -> mode
80009638: 4916      ldr  r1,[pc,#88]           ; 0x80025E78
8000963a: 7008      strb r0,[r1,#0]            ; ***** mode stored *****
8000963c: b2c0      uxtb r0,r0
8000963e: cbz  r0,0x8000965a              ; --- mode 0 init ---
80009640: 2801      cmp  r0,#1
80009642: d124      bne.n 0x8000968e         ; --- mode >= 2: nothing ---
80009644: 2000      movs r0,#0                ; --- mode 1 init ---
80009646: f000 f8a5 bl   0x80009794
8000964a: 203f      movs r0,#0x3f
8000964c: f000 f8a2 bl   0x80009794
80009650: 2007      movs r0,#7
80009652: f000 f8be bl   0x800097d2         ; all three channels ON
80009656: 2400      movs r4,#0
80009658: e012      b.n  0x80009680
; --- mode 0 init ---
8000965a: 2100      movs r1,#0
8000965c: 2010      movs r0,#16
8000965e: f000 f835 bl   0x800096cc            ; reg 0x10 = 0   (all channels off)
80009662: 2100      movs r1,#0
80009664: 2020      movs r0,#32
80009666: f000 f831 bl   0x800096cc            ; reg 0x20 = 0
8000966a: 2036      movs r0,#54
8000966c: f000 f86e bl   0x8000974c            ; reg 0x30 = 54 & 0x3f = 54
80009670: 2036      movs r0,#54
80009672: f000 f86f bl   0x80009754            ; reg 0x40 = 54
80009676: e8bd 4010 ldmia.w sp!,{r4,lr}
8000967a: 2036      movs r0,#54
8000967c: f000 b86e b.w  0x8000975c            ; reg 0x50 = 54
; --- mode 1 self-test ---
80009680: f7ff ffaa bl   0x800095d8            ; LED ON
80009684: f7ff ff96 bl   0x800095b4            ; LED OFF
80009688: 1c64      adds r4,r4,#1
8000968a: 2c08      cmp  r4,#8
8000968c: dbf8      blt.n 0x80009680            ; 8 immediate flashes, no delay
8000968e: bd10      pop  {r4,pc}
```

Consequences [P]:

* In **mode 0** the FAN5702 is initialised as: `reg 0x10 = 0` (all channels
  off), `reg 0x20 = 0`, and the three current registers
  `0x30 = 0x40 = 0x50 = 54` (`54/63` of full scale).
* In **mode 1** the second device is initialised with commands `0x00`, `0x3F`,
  then "all on" (`0x80 0x60 0x40`) and an **8-flash self-test**.
* Mode ≥ 2 ⇒ the LED primitives are no-ops; a blink pattern would then be
  invisible. **The operator sees the pattern, so mode ∈ {0,1} on this unit
  [I].**

### 2.6 Which I²C bus — and a data-pointer defect in this image [P]/[I]/[U]

`0x80009A2A` indexes a bus-base table with `desc[+4] = 3`:

```
80009a30: 4968      ldr  r1,[pc,#416]          ; literal @0x80009BD4
80009a34: 6840      ldr  r0,[r0,#4]            ; = 3
80009a40: f851 8020 ldr.w r8,[r1,r0,lsl#2]     ; base = table[3]
```

**[P] the literal at `0x80009BD4` is `0x8001390C`, which is inside this
stage's own Thumb text** (`8001390c: f8cd 9000 str.w r9,[sp]` …), not a table
of I²C bases. The only I²C base table in the 1 MiB is at **`0x8001C90C`**:

| index | value | bus |
|---|---|---|
| 0 | `0x48070000` | I2C1 |
| 1 | `0x48072000` | I2C2 |
| 2 | `0x48060000` | I2C3 |
| 3 | `0x48350000` | I2C4 |

and it is byte-identical to the table the mask ROM carries at `0x4002F680`
(`0x48070000, 0x48072000, 0x48060000, 0x48350000`).

**[P] `0x8001C90C = 0x8001390C + 0x9000`, and `0x9000` is exactly this stage's
file offset.** The same `+0x9000` relation holds for the other text-pointing
data literal in this module: `0x800138C8` (the "format string" passed to
`0x80017206` at `0x80009554`) `+ 0x9000 = 0x8001C8C8`, which is exactly the
string

```
8001c8c8: "SetLedColourSync: Invalid LED type (%d) - Setting LED type to (%d)"
```

**[I]** the low-region code of this stage carries data pointers that are
`0x9000` too low, i.e. computed as if the stage were linked at `0x80000000`
instead of `0x80009000`. (Report 151 §8 #6 already flagged two literals of this
image that point into the middle of its own text — `0x8001431C` and
`0x80014654` — and `0x80014654` is exactly the value at `0x80013758`, the frame
pointer used by the halt's callee `0x800135FC`. It is the same defect class.)

**[I]** with the `0x9000` correction applied, bus index 3 selects
**`0x48350000` = I2C4**. **[U]** which physical bus the FAN5702 is actually
wired to on the PlayBook — the image gives the *index*, not the board wiring.
(The alternative reading — that the pointer is repaired by whatever loads this
stage — leads to the same table.)

### 2.7 The GPIO path exists but is dead [P]

Two copies of a six-entry GPIO base table exist:

```
8001c88c: 0x4A310000  GPIO1        80008cf4(=80008cfc len4): 0x4A310000  GPIO1
8001c890: 0x48055000  GPIO2        80008d00: 0x48055000  GPIO2
8001c894: 0x48057000  GPIO3        80008d04: 0x48057000  GPIO3
8001c898: 0x48059000  GPIO4        80008d08: 0x48059000  GPIO4
8001c89c: 0x4805B000  GPIO5        80008d0c: 0x4805B000  GPIO5
8001c8a0: 0x4805D000  GPIO6        80008d10: 0x4805D000  GPIO6
```

Neither table is reached: a 32-bit literal search for `0x8001C88C`, `0x80008CFC`
and every entry address returns **no hits**, and no instruction computes those
addresses PC-relative. There is also no GPIO pad-mux (`CTRL_CORE_PADCONF`)
write anywhere near the LED code. **The LED is not on a GPIO.** [P]

Similarly the whole `SetLedColourSync` data block at `0x8001C8A4…0x8001C8C8`
(`03 00 00 00 | 01 00 00 00 | 0…`, then the string) has no referrer: it is
linked-in but unused in this build. [P]

### 2.8 The mask ROM does not drive the LED [P]/[U]

The 49 KiB ROM at `0x40028000` contains **no** LED strings, and its only
peripheral literals are clock/PRM/CM/WDT/USB machinery —
ROM/CM registers (`0x4A307000`, `0x4A307840`, `0x4A304000`, `0x4A318000`), the
MUSB core (`0x4A0AB000`, `0x4A0AB102`), MMC (`0x4809C000`, `0x480B4000`),
UART (`0x48020000`, `0x4806A000`…), and one **generic I²C base table**
(`0x48070000 / 0x48072000 / 0x48060000 / 0x48350000` at `0x4002F680`, identical
to the one in the eMMC image). **[P]** No ROM code path was found that reaches
`0x36`, `0x6C`, `0x39` or `0x72` as an I²C slave address in the context of an
LED register. **[U]** whether any ROM routine programs the FAN5702 — the ROM
does carry I²C machinery, but there is no LED-semantics evidence in it, and the
blink emitter (§3) does not exist in the ROM at all.

---

## 3. Q2 — The blink pattern itself: how it is generated, and the timing ladder

### 3.1 `0x80014C98` — the blink-code engine [P]

Signature `void blink_error(int level /*r0→sl*/, uint32_t code /*r1→r8*/)`;
returns only if `level != 0` **and** the USB service asks to abort.

```
80014c98: e92d 47f0  stmdb sp!,{r4,r5,r6,r7,r8,r9,sl,lr}
80014c9c: 4682       mov  sl,r0                  ; sl = level (0 = blink and never return)
80014c9e: 4688       mov  r8,r1                  ; r8 = CODE
80014ca0: f7f4 fc88  bl   0x800095b4            ; LED OFF (baseline)
80014ca4: 251f       movs r5,#31
80014ca6: e002       b.n  0x80014cae
80014ca8: 1e6d       subs r5,r5,#1               ; find the highest set bit ...
80014caa: 2d01       cmp  r5,#1
80014cac: dd02       ble.n 0x80014cb4
80014cae: fa58 f005  asrs.w r0,r8,r5
80014cb2: d0f9       beq.n 0x80014ca8             ; ... of the code -> r5
80014cb4: 2600       movs r6,#0                  ; <<< OUTER LOOP TARGET
80014cb6: f04f 0996  mov.w r9,#150               ; (bounded, but reset below)
80014cba: f7f4 fc13  bl   0x800094e4            ; WDT2 kick
80014cbe: 1e2c       subs r4,r5,#0
80014cc0: db17       blt.n 0x80014cf2            ; ---- per-bit loop, MSB -> bit0 ----
80014cc2: 2728       movs r7,#40                 ; default gap = 40 units  ("0" bit)
80014cc4: fa48 f004  asr.w r0,r8,r4
80014cc8: 07c0       lsls r0,r0,#31
80014cca: d00a       beq.n 0x80014ce2             ; bit clear -> no pulse
80014ccc: f7f4 fc84  bl   0x800095d8            ; ***** LED ON  *****
80014cd0: 2014       movs r0,#20
80014cd2: 4651       mov  r1,sl
80014cd4: f000 f83f  bl   0x80014d56            ; delay 20 units
80014cd8: 2800       cmp  r0,#0
80014cda: d114       bne.n 0x80014d06            ; (abort -> return, level != 0 only)
80014cdc: 2714       movs r7,#20                 ; after a "1": gap = 20 units
80014cde: f7f4 fc69  bl   0x800095b4            ; ***** LED OFF *****
80014ce2: 4651       mov  r1,sl
80014ce4: 4638       mov  r0,r7
80014ce6: f000 f836  bl   0x80014d56            ; gap delay: 20 (after "1") or 40 (after "0")
80014cea: 2800       cmp  r0,#0
80014cec: d10b       bne.n 0x80014d06
80014cee: 1e64       subs r4,r4,#1
80014cf0: d5e7       bpl.n 0x80014cc2             ; next bit
80014cf2: 20c8       movs r0,#200                ; ---- end of one pattern ----
80014cf4: 4651       mov  r1,sl
80014cf6: f000 f82e  bl   0x80014d56            ; trailing gap = 200 units
80014cfa: 2800       cmp  r0,#0
80014cfc: d103       bne.n 0x80014d06
80014cfe: 1c76       adds r6,r6,#1
80014d00: 454e       cmp  r6,r9
80014d02: dbda       blt.n 0x80014cba
80014d04: e7d6       b.n  0x80014cb4            ; <<< 150 reached -> r6 = 0 and GO AGAIN
80014d06: e8bd 87f0  ldmia.w sp!,{r4,r5,r6,r7,r8,r9,sl,pc}
```

**So the "pattern table" is not a table at all — it is the binary
representation of the code [P].** MSB first:

| bit value | emitted |
|---|---|
| `1` | LED **ON** for 20 units, then **OFF** for 20 units |
| `0` | **OFF** for 40 units (no pulse) |

and after the last (bit-0) stage, **OFF for 200 units**. The whole thing then
repeats.

### 3.2 The unit: `0x80014D56` [P]

```
80014d56: b570      push {r4,r5,r6,lr}
80014d58: 460d      mov  r5,r1                  ; r5 = level
80014d5a: 2164      movs r1,#100
80014d5c: fb00 f401 mul.w r4,r0,r1               ; r4 = units * 100
80014d60: b16c      cbz  r4,0x80014d7e
80014d62: 4e08      ldr  r6,[pc,#32]            ; 0x80014D84 = 0x000186A0 = 100000
80014d64: b125      cbz  r5,0x80014d70           ; level == 0 -> no service call
80014d66: f7fd fbd6 bl   0x80012516            ; USB/service hook (abort source)
80014d6a: b108      cbz  r0,0x80014d70
80014d6c: 2001      movs r0,#1                  ; abort -> return 1
80014d6e: bd70      pop  {r4,r5,r6,pc}
80014d70: f7f4 fbb8 bl   0x800094e4            ; ***** WDT2 KICK, every unit tick *****
80014d74: 4630      mov  r0,r6
80014d76: f005 ed2e blx  0x8001a7d4            ; delay(100000)
80014d7a: 1e64      subs r4,r4,#1
80014d7c: d1f2      bne.n 0x80014d64
80014d7e: 2000      movs r0,#0
80014d80: bd70      pop  {r4,r5,r6,pc}
80014d84: 86a0 0001                          ; 0x000186A0
```

so **1 unit = 100 × (WDT2 kick + `delay(100000)`)**, and the delay itself is a
pure CPU spin with a loop count taken from a byte in the image:

```
8001a7d4: e59f201c ldr  r2,[pc,#28]   @ 0x8001A7F8 = 0x80014650
8001a7d8: e5d22000 ldrb r2,[r2]       @ byte @0x80014650 = 0xC5 = 197
8001a7dc: e1a01002 mov  r1,r2
8001a7e0: e3a03011 mov  r3,#17
8001a7e4: e0020293 mul  r2,r3,r2      @ 17*197
8001a7e8: e0500002 subs r0,r0,r2
8001a7ec: e0500001 subs r0,r0,r1      @ loop: r0 -= 197
8001a7f0: aafffffd bge  0x8001a7ec
8001a7f4: e12fff1e bx   lr
```

i.e. ≈ `100000/197 − 16 ≈ 491` iterations of a 2-instruction loop per
`delay(100000)`. **[P] for all values; [U] for the absolute CPU clock** — the
loop does not read a timer, so the absolute unit length is set by the core
clock and can only be *calibrated*, which §3.3 does.

### 3.3 Calibration against the operator's measurements — exact fit

Let one unit be `u` seconds. The emitted timeline for a code is fully
determined by §3.1. For **code `0x5B` = `0b1011011`** (7 bits, MSB first
`1 0 1 1 0 1 1`):

| stage | units consumed | cumulative (units) |
|---|---|---|
| ON 20 (bit6=1) | 20 | 0 – 20 |
| OFF 20 (bit6) + OFF 40 (bit5=0) | 60 | 20 – 80 |
| ON 20 (bit4=1) | 20 | 80 – 100 |
| OFF 20 (bit4) | 20 | 100 – 120 |
| ON 20 (bit3=1) | 20 | 120 – 140 |
| OFF 20 (bit3) + OFF 40 (bit2=0) | 60 | 140 – 200 |
| ON 20 (bit1=1) | 20 | 200 – 220 |
| OFF 20 (bit1) | 20 | 220 – 240 |
| ON 20 (bit0=1) | 20 | 240 – 260 |
| OFF 20 (bit0) + trailing 200 | 220 | 260 – 480 |

Predicted observables, in units: **5 pulses of 20**, gaps **60, 20, 60, 20**,
final gap **220**, period **480**.

Measured (operator video, red LED on):

| observable | measured (ms) | units | implied `u` |
|---|---|---|---|
| pulse 1 length | 330 | 20 | 16.5 |
| gap 1→2 | 904 | 60 | 15.07 |
| pulse 2 length | 292 | 20 | 14.6 |
| gap 2→3 (in-group) | 323 | 20 | 16.15 |
| pulse 3 length | 330 | 20 | 16.5 |
| gap 3→4 | 891 | 60 | 14.85 |
| pulse 4 length | 297 | 20 | 14.85 |
| gap 4→5 (in-group) | 297 | 20 | 14.85 |
| pulse 5 length | 330 | 20 | 16.5 |
| gap 5→1' (cycle) | 3353 | 220 | 15.24 |
| **full period** | **7347** | **480** | **15.31** |

The four independent gap classes **60 / 20 / 60 / 20 / 220** match the measured
**904 / 323 / 891 / 297 / 3353 ms** to within ±3 %, which is the frame
quantisation of a ~30 fps phone video. **[P] the structure; [P] the fit; [I]
`u ≈ 15.3 ms`**, i.e. one outer tick of `0x80014D56`'s loop ≈ **153 µs**.

That 153 µs is a *calibration*, not a derivation: the whole tick is
`WDT2 kick + delay(100000)`, and `delay(100000)` is only ≈ 491 iterations of
`subs/bge` (§3.2). 153 µs for ~491 two-instruction iterations (~311 ns each)
implies a very slow instruction stream — consistent with the A9 running at a
pre-DPLL boot clock and/or with instruction fetch stalls, but the image
contains no timer reference that would let the core clock be recovered. **[U]**
(§8 #5). Only the *ratios* 20 : 60 : 220 are provable from the bytes, and they
are what identify the code.

The group shape — **1 blink, gap, 2 blinks, gap, 2 blinks, long gap** — is
`[1, 2, 2]`, exactly as the operator reported.

### 3.4 Code → blink-group map (the decoder table)

Replaying §3.1 for every code the image actually emits. "Groups" = runs of
blinks separated by a ≥60-unit gap:

| code | bits (MSB→b0) | gap sequence (20-unit quanta) | **group shape** | level(s) used |
|---:|---|---|---|---|
| `0x17` | 10111 | 3,1,1,1 | 1-3 | 0 |
| `0x2B` | 101011 | 3,3,1,1 | 1-1-2 | 0 |
| `0x2F` | 101111 | 3,1,1,1,1 | 1-4 | 0 |
| `0x35` | 110101 | 1,3,3,1 | 2-1-1 | 0 |
| `0x37` | 110111 | 1,3,1,1,1 | 2-3 | 0 |
| `0x3B`* | 111011 | 1,1,3,1,1 | 3-2 | — |
| `0x3D` | 111101 | 1,1,1,3,1 | 4-1 | 0 |
| `0x3F` | 111111 | 1,1,1,1,1,1 | 6 | 0 |
| **`0x5B`** | **1011011** | **3,1,3,1,1** | **1-2-2** ← *the observed pattern* | **0** |
| `0x5D` | 1011101 | 3,1,1,3,1 | 1-3-1 | 0 |
| `0x5F` | 1011111 | 3,1,1,1,1,1 | 1-5 | 0 / 1 |
| `0x6D` | 1101101 | 1,3,1,3,1 | 2-2-1 | 0 |
| `0x6F` | 1101111 | 1,3,1,1,1,1 | 2-4 | 0 |
| `0x75` | 1110101 | 1,1,3,3,1 | 3-1-1 | 0 |
| `0x77` | 1110111 | 1,1,3,1,1,1 | 3-3 | 0 |
| `0x7B` | 1111011 | 1,1,1,3,1,1 | 4-2 | 0 |
| `0x7D` | 1111101 | 1,1,1,1,3,1 | 5-1 | 0 |
| `0x7F` | 1111111 | 1,1,1,1,1,1,1 | 7 | 0 |
| `0xAB` | 10101011 | 3,3,3,1,1 | 1-1-1-2 | 0 |
| `0xAD` | 10101101 | 3,3,1,3,1 | 1-1-2-1 | 0 |
| `0xAF` | 10101111 | 3,3,1,1,1,1 | 1-1-4 | 0 |
| `0x03`† | 11 | 1,1(+200) | 2 | 1 |
| `0x05`† | 101 | 3,1 | 1-1 | 1 |
| `0x07`† | 111 | 1,1,1 | 3 | 1 |

`*` `0x3B` is not emitted by any site found; listed for completeness.
`†` the verify-status codes of report 151 §2.5 (3/5/7/0x0F) reach the emitter
*with level 1*, at `0x80014A6C` (`movs r0,#1` / `ldr r1,[sp,#16]` /
`bl 0x80014C98`). `0x0F` = `1111` ⇒ groups `[4]`.

**[P] `0x5B` is the only code in this list whose group shape is `1-2-2`.**
The mapping is injective for the observed pattern, so the operator's 1-2-2
identifies the code as **`0x5B` = 91** with no ambiguity.

### 3.5 The complete set of call sites and their codes

Every site that reaches the blink engine, with the code passed in `r1` and the
level in `r0` — 71 direct `bl 0x80014C98` plus 11 `b`/`b.w 0x80014C98`
tail-calls (the tail-calls are `0x80011728`, `0x80011730`, `0x80011746`,
`0x80014D26`, `0x80014D2C`, `0x80014D32`, `0x80014D38`, `0x80014D3E`,
`0x80014D44`, `0x80014D4A`, `0x80015428`):

| code | `r0` (level) | call sites |
|---:|---:|---|
| `0x5B` | **0** | `0x800090F6`, `0x80014940`, `0x80014958`, `0x80014A5A` |
| `0x5F` | 0 / 1 | `0x80009174`, `0x8000923E`, `0x800092F8`, `0x800093EA`, `0x80009422`, `0x8000E9D4`(1), `0x8000F1F8`, `0x8000F41C`(1) |
| `0x3D` | 0 | `0x80009CF4`, `0x80009E3A`, `0x80009E72`, `0x8000A03C`, `0x8000A0A4`, `0x8000A1EA`, `0x8000A34A`, `0x8000A3DE`, `0x8000A922` |
| `0x3F` | 0 | `0x8000E8C0`, `0x8000E936` |
| `0x2F` | 0 | `0x80011416`, `0x800114C6`, `0x800115DE`, `0x80014516`, `0x80014DA6`, `0x80014DBA`, `0x80014DCE`, `0x8001539A`, `0x800153AA`, `0x800153BE`, `0x800153D4`, `0x80015634` |
| `0x2B` | 0 | `0x80011728`, `0x80011730`, `0x8001173A`, `0x80011746`, `0x80011750`, `0x8001175E`, `0x8001176C`, `0x80011BA2`, `0x80011BB8`, `0x80011BC8`, `0x80011C72`, `0x80011C88`, `0x80011F6E`, `0x80012A68`, `0x80012A74`, `0x80012B06`, `0x800130A6`, `0x80013142`, `0x80013228`, `0x8001327E`, `0x8001329C`, `0x80013308` |
| `0x37` | 0 | `0x80014E7E`, `0x80014FBE` |
| `0x35` | 0 | `0x80015642`, `0x800157D2` |
| `0x5D` | 0 | `0x8001549E`, `0x800154E8`, `0x80015524`, `0x80015580`, `0x80016376`, `0x80016436` |
| `0x17` | 0 | `0x80014AA0`, `0x800152EA`, `0x80015A00` |
| `0xAF` | 0 | `0x8001605A` |
| `0x6D` | 0 | `0x80014D0A`…`0x80014D54` (ten 4-instruction stubs) |
| `0x6F/0x75/0x77/0x7B/0x7D/0x7F/0xAB/0xAD` | 0 | `0x800153FC` only — the exception dispatcher (§4.3) |
| *runtime* | 1 | `0x80014A72` (`ldr r1,[sp,#16]` = the verify status) |

---

## 4. Q3 — Which code path calls it, and its relation to the halt / PMIC paths

### 4.1 The blink **replaces** the halt on every `0x5B` site [P]

`0x80014C98` can only return if `level != 0` **and** the USB service
`0x80012516` returns non-zero (`0x80014D64: cbz r5,0x80014d70` skips the service
entirely when `r5 == sl == 0`). **All four `0x5B` sites pass `level = 0`.**
Therefore, from any of them, `0x80014C98` never returns and the following
instruction is unreachable.

Concretely, the "BootModeCount expended" path of reports 150/151:

```
80014a4e: b940      cbnz r0,0x80014a62
80014a50: a04c      adr  r0,0x80014b84           ; "\nBootModeCount expended\n"
80014a52: f002 fbd8 bl   0x80017206
80014a56: 215b      movs r1,#0x5B
80014a58: 2000      movs r0,#0                   ; level = 0
80014a5a: f000 f91d bl   0x80014c98             ; >>> 1-2-2 FOR EVER <<<
80014a5e: f7f4 fd2e bl   0x800094be             ; *** DEAD CODE: PMIC 0x90 + b . ***
```

So the sequence on a real unit is:

```
...failure...
  -> LED OFF baseline            (0x80014CA0)
  -> 1-2-2 pattern, repeating     (0x80014C98, never returns)
  -> [never] PMIC command         (0x800094BE)
  -> [never] b .                  (0x800094CA)
```

**This is a correction to reports 150 and 151**, both of which describe the
end state as "`0x800094BE` … sends a PMIC command byte 0x90 over I²C, retried up
to 13 times … then spins forever". The code at `0x800094BE` is real and is
quoted correctly, but on every path that emits code `0x5B` it is **not
reached**. `0x800094BE` *is* reached on paths that do not emit `0x5B` first
(e.g. `0x80014A76` → `0x80014A82`, and the ten other sites listed in
report 150 §5.3).

### 4.2 The four `0x5B` sites, in boot order

| # | site | condition | report-151/150 context |
|---|---|---|---|
| 1 | **`0x80014940`** | `0x800112DC` (mount / build the record catalogue from the medium, report 150 §1.2) returned non-zero | boot main, right after the RPMB probe; message `log(0,0x5B)` |
| 2 | **`0x80014958`** | reached when `0x80010D3C` (RPMB subsystem probe) returned **0**, **or** when `0x800090A0(1)` (provision `BOOTCTR_FCTRY = 0x32`) returned 0 | boot main; report 150 §1.7's "`0x80014954: 215b ... log(0,0x5B); b 0x80014972`" |
| 3 | **`0x800090F6`** | inside the selector `0x800090CE`: `record_read("BOOTCTR_FCTRY")` returned a status other than `3`, or `stored_len != 4`, or the value is `0`/`>0x32` (report 150 §1.5) | every later selector evaluation (boot main + the verify driver) |
| 4 | **`0x80014A5A`** | the NV counter decrement/re-seal `0x8000911E` failed, i.e. `record_write` failed after the re-seal (report 150 §2.1–2.5) | after `puts("\nBootModeCount expended\n")` |

All four are **RPMB / NV-record failures** — not media CRC failures, not
signature failures, not region-selection failures. **[P]**

Ordering note: site 2 is *before* the region verification, so on a unit that
blinks 1-2-2 **without** ever having verified a region, site 1 or 2 is the
culprit; if the region verify ran first, site 4 is. The image cannot tell them
apart; the *code* (`0x5B`) is the same for all four, which is why a single
pattern was chosen for all of them. **[I]**

### 4.3 The exception dispatcher, for completeness [P]

`0x800153FC` maps an exception index to a blink code and calls the same engine:

```
800153fc: 2807      cmp  r0,#7
800153fe: d215      bcs.n 0x8001542c            ; index >= 7 -> 0xAD
80015400: e8df f000 tbb  [pc,r0]                ; byte table at 0x80015404
80015404: 06 04 0a 08 0e 0c 00 10               ; target = 0x80015404 + 2*byte
8001540c: 216f      movs r1,#0x6f
80015410: 2175      movs r1,#0x75
80015414: 2177      movs r1,#0x77
80015418: 217b      movs r1,#0x7b
8001541c: 217d      movs r1,#0x7d
80015420: 217f      movs r1,#0x7f
80015424: 21ab      movs r1,#0xab
80015426: 2000      movs r0,#0
80015428: f7ff bc36 b.w  0x80014c98
8001542c: 21ad      movs r1,#0xad               ; default
```

Decoded through `tbb` (`target = 0x80015404 + 2*byte[index]`):

| exception index `r0` | code emitted | group shape (§3.4) |
|---:|---:|---|
| 0 | `0x75` | 3-1-1 |
| 1 | `0x6F` | 2-4 |
| 2 | `0x7B` | 4-2 |
| 3 | `0x77` | 3-3 |
| 4 | `0x7F` | 7 |
| 5 | `0x7D` | 5-1 |
| 6 | *(table byte `0x00` → target `0x80015404`, i.e. into the table itself — a defect in this build)* | n/a |
| ≥ 7 | `0xAD` | 1-1-2-1 |

`0x800191B4…0x80019204` are the eight exception stubs that call it
(`mov r0,#k; blx 0x800153FC`, report 151 §5.4), so a fault also produces a
blink code — but none of `0x6F/0x75/0x77/0x7B/0x7D/0x7F/0xAB/0xAD` gives 1-2-2
(§3.4), so the observed unit did **not** take an exception. **[P]**

---

## 5. Q4 — What the code means

### 5.1 Proven mapping

**`0x5B` (decimal 91) = the NV-record / RPMB subsystem is unusable.** The four
emission sites (§4.2) are the record catalogue mount, the RPMB probe, the
`BOOTCTR_FCTRY` selector, and the counter re-seal. There is **no** site that
emits `0x5B` for a CRC mismatch, a signature failure, or a region-selection
failure — those use different codes and different levels.

Cross-check against the stage's own status codes (report 151 §2.5):

| code | where report 151 puts it | reaches the blinker? |
|---|---|---|
| 3 | verifier "pending" | **yes**, at `0x80014A6C` with level 1 (§3.4 `†`) |
| 5 | CRC mismatch (`0x8001483C`) | **yes**, via the same `0x80014A6C` (pattern 1-1) |
| 7 | CRC word unprogrammed (`0x800147E2`) | **yes**, `0x80014A6C` (pattern 3) |
| `0x0F` | product-mode, no signature path (`0x80014290`) | **yes**, `0x80014A6C` (pattern 4) |
| `0x17` | final path, `0x80015E14` non-zero | **yes**, `0x80014AA0`, `0x800152EA`, `0x80015A00` |
| `0x2F` | media write-back failed | **yes**, 12 sites |
| `0x5B` | record/RPMB failure | **yes**, 4 sites — **this is the observed 1-2-2** |
| `0x2B`, `0x3D`, `0x3F`, `0x35`, `0x37`, `0x5D`, `0x5F`, `0x6D`, `0xAF` | other internal error classes | yes |
| `0x25`, `0x40`, `0x90` | **not** blink codes — they are the `(r0=0, r1=0x25, r2=0x40)` arguments of the halt's `bl 0x800135FC` at `0x800094C6`. In `0x800135FC`, `r0` selects the I²C sub-address (`0x90`/`0x92`/`0x94` at `0x800135D2`/`0x80013618`), `r1` is the register byte, and `r2` is the frame pointer; the transaction is retried 13 times (`movs r4,#13`) with a WDT kick per retry, exactly as report 151 §5.1 describes | — |

**So the fault on the dead unit is in the RPMB / NV-record path, and it is not
a CRC or signature failure of a boot region.** [P] for the code identity;
[I] which of the four sites fired.

### 5.2 What is *not* proven

* Which of the four `0x5B` sites fired on the operator's unit — [U]. All four
  produce an indistinguishable 1-2-2.
* Which physical I²C bus carries the FAN5702 — [U] (§2.6).
* Whether the observed channel (register `0x10` bit 1) is the *red* die — [I],
  from the operator's observation plus the OS driver's red/green/blue model.
* The absolute CPU clock implied by `u = 15.3 ms` — [U]; the delay loop is a
  pure spin with no timer reference (§3.2).

---

## 6. Q5 — Does the blink loop reset? **No.**

This is the decisive question and the bytes answer it unambiguously [P]:

1. **The loop is infinite by construction.** After 150 pattern repetitions the
   code branches **backwards to `0x80014CB4`**, which re-zeroes the counter and
   reloads `r9 = 150`:
   ```
   80014cfe: 1c76  adds r6,r6,#1
   80014d00: 454e  cmp  r6,r9
   80014d02: dbda  blt.n 0x80014cba      ; inner repeat
   80014d04: e7d6  b.n  0x80014cb4      ; <<< re-arm r6 = 0, r9 = 150, go again
   ```
   `0x80014CB4: movs r6,#0` / `0x80014CB6: mov.w r9,#150`. The 150 count is
   therefore decorative: the sequence is `for(;;) { for (i<150) { emit } }`.
2. **The natural exit is unreachable on this path.** The only `pop {…,pc}` is
   at `0x80014D06`, reached from three `bne` on the delay's return value; the
   delay returns 1 only when `sl != 0` **and** the USB service aborts
   (`0x80014D64`/`0x80014D66`). `0x5B` is emitted with `sl = 0`.
3. **The watchdog is fed continuously, so no reset can occur either.**
   `0x80014D70: f7f4 fbb8 bl 0x800094e4` is inside the per-unit loop:
   100 kicks per unit × 480 units per cycle = **48 000 WDT2 kicks per 7.35 s
   cycle**. `0x800094E4` is the standard kick
   (`ldr r0,[0x4A314000+0x30]; adds r0,#1; str r0,[0x4A314000+0x30]`,
   report 151 §5.1). WDT2 is armed with `WCRR = WLDR = 0xFFFC1800`
   (≈ 7.8 s at 32.768 kHz) at `0x80019248`, i.e. ~ two orders of magnitude
   longer than the 153 µs between kicks.
4. **The measured period agrees with the loop, not with a reboot.** The
   predicted loop period is 480 units; the measured repetition period is
   7.347 s (§3.3), and the *long* gap is 3.353 s ≈ 220 units. If the cycle were
   a watchdog reset, the period would be `pattern + reset/ROM-boot time`, the
   long gap would be `200 units + ROM-boot time`, and the ROM banner
   (`"Starting RIM bootrom with UART_DEBUG enabled"`, `0x80014AE8`) would be
   re-printed every 7.35 s. Neither is observed.

**Therefore:** the 1-2-2 is a **deliberate, self-perpetuating error-indication
loop**, not "a reset loop that emits a code each cycle". The unit is not
resetting; it is sitting in `0x80014CB4…0x80014D04` for ever, blinking `0x5B`,
with WDT2 kept alive.

Two useful corollaries:

* The unit is **not** in the `b .` spin at `0x800094CA` that reports 150/151
  describe, and the PMIC/I²C command at `0x800094BE` never runs.
* Because WDT2 is fed, the unit will **never** reboot out of this state on its
  own. Only an external power cycle (or a second, independent reset source)
  changes anything.

---

## 7. Reproduce

```sh
cd /home/psyden/SSDdrive/playbook-linux
. tools/env.sh
IMG=local/proprietary/device-binaries/emmc-first1mb.bin
TH="-D -b binary -m arm --adjust-vma=0x80000000 -M force-thumb"

# The LED primitives, the mode dispatch, the I2C accessors, the probe, the init
arm-linux-objdump $TH --start-address=0x8000954c --stop-address=0x8000969c "$IMG"
arm-linux-objdump $TH --start-address=0x8000969c --stop-address=0x80009840 "$IMG"
arm-linux-objdump $TH --start-address=0x800099a0 --stop-address=0x80009b40 "$IMG"

# The transfer descriptor and the (stale) bus-table literal
arm-linux-objdump $TH --start-address=0x80009760 --stop-address=0x80009790 "$IMG"

# The blink engine and its delay ladder
arm-linux-objdump $TH --start-address=0x80014c98 --stop-address=0x80014d88 "$IMG"
arm-linux-objdump -D -b binary -m arm --adjust-vma=0x80000000 \
    --start-address=0x8001a7d4 --stop-address=0x8001a7f8 "$IMG"     # ARM delay

# The sole call into the LED init, from the ARM reset stub
arm-linux-objdump -D -b binary -m arm --adjust-vma=0x80000000 \
    --start-address=0x80019244 --stop-address=0x8001926c "$IMG"     # ARM
arm-linux-objdump $TH --start-address=0x800159ac --stop-address=0x80015a10 "$IMG"

# The four 0x5B sites and the (dead) halt after them
arm-linux-objdump $TH --start-address=0x80014930 --stop-address=0x80014960 "$IMG"
arm-linux-objdump $TH --start-address=0x80014a4e --stop-address=0x80014a60 "$IMG"

# The exception -> code table
arm-linux-objdump $TH --start-address=0x800153fc --stop-address=0x80015430 "$IMG"

# The I2C base table and the SetLedColourSync string (data)
python3 -c "d=open('$IMG','rb').read(); \
  print(d[0x1c90c:0x1c91c].hex()); print(d[0x1c8c8:0x1c908])"

# The QNX driver that names the part
strings local/proprietary/device-binaries/led-fan5702.so | grep -E 'FAN5702|red::|green::|blue::'
```

The tabulation in §3.4 was produced by replaying §3.1 over the code list; the
same can be checked by hand from any code's binary representation.

---

## 8. Residual unknowns

| # | question | why unresolved |
|---|---|---|
| 1 | Which of the four `0x5B` sites fired? | all four emit the identical 1-2-2 pattern; distinguishing them needs the UART log or a JTAG/PMIC trace |
| 2 | Which OMAP I²C instance carries the FAN5702? | the descriptor stores only a bus *index* (3); the index→base literal at `0x80009BD4` is `0x8001390C`, which is text. Applying the image's consistent `+0x9000` data-pointer correction gives `0x48350000` (I2C4), but the board wiring is not in the bytes |
| 3 | Is register `0x10` bit 1 the red die? | the bootloader only ever drives one channel; the colour assignment is a board detail. The OS driver (`led-fan5702.so`) proves the part exposes red/green/blue, not which output is which |
| 4 | Mode 0 or mode 1 on this unit? | the probe result is computed at run time (`0x800095FC`) from whether an ACK arrives from `0x36` or `0x39`; both are FAN-family devices, and the blink arithmetic is identical for both |
| 5 | Absolute CPU clock / exact unit length | the `delay(100000)` loop at `0x8001A7D4` is a pure spin with no timer reference; only the *ratio* 20:60:220 is provable from the bytes, the absolute `u ≈ 15.3 ms` is a calibration against the operator's video |
| 6 | What the second device at I²C `0x39` is | mode 1 writes single bytes `0x00/0x3F/0x80/0x9F/0x60/0x7F/0x40/0x5F` with no register prefix; no part name appears anywhere in the 1 MiB |
| 7 | Does anything repair the `0x9000`-low data pointers? | `0x80009BD4`→`0x8001390C`, `0x80009690`→`0x800138C8` and `0x80013758`→`0x80014654` all point into this stage's own text; either the loader patches them or those paths are degraded. Not decidable from the image |
