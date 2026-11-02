# 130 - RE: the QNX per-panel parameter table (Hitachi 1024x600)

Status: **complete for questions 1-5.** Every claim below is quoted with the vaddr of the
instruction that establishes it. Inferences are explicitly marked `[inference]`. Where a
question cannot be answered statically it says so instead of guessing.

Headline answers:

1. The Hitachi/LG decision is **one `tst`/`beq` pair at 0x1c5e8/0x1c5ec** inside the function
   at **0x1c580**, and it selects **nothing but the SPI word table** (9 words at 0x2e578 vs
   11 words at 0x2e58c). No other parameter is panel-identity dependent.
2. Yes, there is a per-panel/per-mode parameter structure: **240 bytes**, symbols
   `DSI1_RGB888_VID_MODE_1024_600_<fps>fps` (Winchester 1024x600) plus
   `DSI1_RGB888_VID_MODE_1280_768_Colt*` (Colt 1280x768). It is keyed by **platform + refresh
   rate, not by panel ID**; it is copied into `g_DISPC_DSI_Configuration_DSI1` (0x2ee08).
   Full field table in §2.
3. **No LVDS single/dual-link or divider field exists anywhere in the QNX set.** `LV_CFG`
   (0x049C) is written exactly once, with **1**, at **0x13fd0**; there is exactly one `movw
   #0x49c` in the whole binary. The bridge script (0x13e50) is unconditional and
   panel-independent. The LVDS output is configured for **single link, 1024x600**, hard-coded.
4. The resolution *is* in the table (0x18 = 600 lines, 0x1c = 1024 pixels, 0x24 = 3072 =
   3x1024) plus a DISPC H/V timing sextet (0x28..0x3c) and a DSI clock-domain sextet
   (0x40..0x5c) and 21 more DSI timing fields (0x74..0xb4). An explicit pixel-clock-in-Hz
   value is **not present**. The **panel-facing** timing comes from the TC358764 `VP_*`
   registers, which decode (with report 129's register names) to
   **1263 x 623, ~47.19 MHz, HBP 120 / HFP 118 / HPW 1 / VBP 10 / VFP 12 / VPW 1** - that is
   the number to give a Linux panel driver. See §3 item 3 and §4.
5. The 9-word table is **not decoded anywhere** in the boot set: no comment, no 9-field
   struct, no name; the two tables are inlined constants with the counts `mov r1,#9` /
   `mov r1,#11` in code. §5 gives the exact on-wire bytes and the Hitachi-vs-LG diff.

## Question

Does the QNX boot set contain a *panel parameter table* keyed by panel identity
(Hitachi/LG or a panel ID), and can we extract from it the facts observation cannot give us:
LVDS single vs dual link, the LVDS pixel-clock divider, H/V timing, the pixel clock, and the
TC358764 LV_CFG / VP_* values?

## Method

* Read-only. Nothing outside `docs/reports/130-*.md` was written.
* Files: `local/proprietary/dumped4869ifs/proc/boot/` was listed. The only display-relevant
  members are `winch_lcdctl` (193024 B), `libdisputil.so.2`, `backlight_win`,
  `spi-omap4430.so`, `spi-master`, `i2c-omap35xx-omap4`. No other file contains the string
  "Hitachi" (checked all of them) and no other file contains the SPI table bytes (checked all
  files in that directory for the byte pattern `29 00 25 00 40 08 ad 00`; single hit,
  file offset 0x2d578).
* `libdisputil.so.2` was checked: it contains **no** panel data at all - only generic
  `disp_*` helpers (`disp_mmap_device_memory`, `disp_phys_addr`, `disp_mode_get_entry`,
  `disp_crtc_calc`, ...). All panel data lives in `winch_lcdctl`. This report therefore
  concentrates on `winch_lcdctl`.
* Address mapping used throughout (verified from `readelf -l`): first `PT_LOAD`
  off=0 vaddr=0 filesz=0x28048 (**vaddr == file offset**); RW `PT_LOAD` off=0x28348
  vaddr=0x29348 filesz=0x525c (**file offset = vaddr - 0x1000**); `DT_PLTGOT = 0x2b000`
  (PIC anchor r4). The RW load ends at vaddr 0x2e5a4, i.e. the two SPI tables sit in the
  **last 44 bytes of .data**.
* Full disassembly:
  `arm-linux-objdump -D -b binary -m arm --adjust-vma=0 winch_lcdctl` (46785 lines) into the
  session scratchpad, then machine-searched rather than eyeballed.
* **Key methodological break:** although `readelf -s` fails (no section headers for the
  symbol table), the ELF still has `DT_SYMTAB`/`DT_STRTAB` (offset 0x4f0 / 0xc80, 121
  symbols, `DT_HASH` at 0x180 giving nchain=121). Parsing it with python recovers the *names
  and addresses of every global data object*, including the per-mode tables. This is what
  turned an otherwise anonymous .rodata blob into a named, sized table set. Everything in §2
  rests on that symbol list.

  | symbol | vaddr | size |
  |---|---|---|
  | `DSI1_RGB888_VID_MODE_1280_768_Colt` | 0x26044 | 240 |
  | `DSI1_RGB888_VID_MODE_1280_768_Colt_TMD` | 0x26134 | 240 |
  | `DSI1_RGB888_VID_MODE_1280_768_Colt_CMI` | 0x26224 | 240 |
  | `DSI1_RGB888_VID_MODE_1280_768_Colt_LG` | 0x26314 | 240 |
  | `DSI1_RGB888_VID_MODE_1024_600_30fps` | 0x26404 | 240 |
  | `..._40fps` / `_50fps` / `_60fps` | 0x264f4 / 0x265e4 / 0x266d4 | 240 |
  | `..._70fps` / `_80fps` / `_90fps` | 0x267c4 / 0x268b4 / 0x269a4 | 240 |
  | `..._100fps` / `_110fps` / `_120fps` | 0x26a94 / 0x26b84 / 0x26c74 | 240 |
  | `DSS_RGB888_864x480_Default_setup` | 0x26e54 | 668 |
  | `g_DISPC_DSI_Configuration_DSI1` | 0x2ee08 | 240 (runtime copy target) |
  | `platform` | 0x2e548 | 4 |
  | (unnamed) refresh rate | 0x2e54c | 4 |
  | `backlight_level` / `xpos` / `ypos` / `ppi_cnt` | 0x2e550/4/8/0x2e55c | 4 each |
  | `CRED_instVirtAddr` | 0x2b1d4 | 1776 |

* `.data` defaults (read from the file, vaddr-0x1000) - these are the as-shipped values:
  `0x2e548 = 1` (platform = Winchester), `0x2e54c = 0x3c = 60` (refresh = 60 fps),
  `0x2e550 = 0x80 = 128` (backlight), `0x2e554/0x2e558 = -1` (xpos/ypos = auto-centre),
  `0x2e55c = 4` (`ppi_cnt`).

## 1. Panel identity detection and what it selects

### 1.1 The whole decision is one function

`0x1c580` is the complete "which panel is this" routine. It takes no arguments and returns
nothing. Instruction by instruction:

```
1c580  push {r4,lr}
1c588  ldr  r4,[pc,#184]  @ 0x1c648      ; literal 0x0000ea6c
1c58c  add  r4,pc,r4                      ; r4 = 0x1c594 + 0xea6c = 0x2b000  (PIC anchor, DT_PLTGOT)
1c590  ldr  r3,[pc,#180]  @ 0x1c64c      ; literal 0x00000164 -> GOT/.data slot 0x2b164
1c594  ldr  r3,[r4,r3]                    ; r3 = *(0x2b164)          = display handle
1c598  ldr  r3,[r3,#0x138]                ; r3 = handle->0x138       = OMAP GPIO6 mapped base
1c59c  cmp  r3,#0
1c5a0  bne  0x1c5d4                       ; mapped -> continue
        ; unmapped path: slogf("not mapped %d function %s ") with the string at
        ; 0x2b000-0x5278 = 0x25d88 and the caller name at 0x2b000-0x33a8 = 0x27c58 ("map fail"),
        ; then deliberately faults (mov r3,#0 ; ldr r3,[r3] at 0x1c5c8/0x1c5cc)
1c5d4  ldr  r3,[pc,#112]  @ 0x1c64c      ; slot 0x164 again
1c5d8  ldr  r3,[r4,r3]                    ; display handle
1c5dc  ldr  r3,[r3,#0x138]                ; GPIO6 base
1c5e0  add  r3,r3,#0x138                  ; + 0x138
1c5e4  ldr  r3,[r3]                       ; read GPIO6 base+0x138 = OMAP GPIO_DATAIN
1c5e8  tst  r3,#65536    @ 0x10000        ; <<<< THE TEST: GPIO6 DATAIN bit 16
1c5ec  beq  0x1c618                       ; bit16 == 0  -> LG
        ; ---- bit16 == 1 : HITACHI ----
1c5f0  mov  r0,#99 ; mov r1,#1
1c5f8  ldr  r2,[pc,#88]  @ 0x1c658        ; literal 0xffffcd9c = -0x3264
1c5fc  add  r2,r4,r2                      ; r2 = 0x2b000-0x3264 = 0x27d9c = "LCD is Hitachi"
1c600  bl   0x424c                        ; slogf(99,1,"LCD is Hitachi")
1c604  ldr  r0,[pc,#80]  @ 0x1c65c        ; literal 0x00003578
1c608  add  r0,r4,r0                      ; r0 = 0x2b000+0x3578 = 0x2e578  <-- Hitachi table
1c60c  mov  r1,#9                         ; 9 words
1c610  bl   0x1c4dc                       ; send_table(table,9)
1c614  b    0x1c640                       ; return
        ; ---- bit16 == 0 : LG ----
1c618  ...
1c620  ldr  r2,[pc,#56]  @ 0x1c660        ; literal 0xffffcdac = -0x3254
1c624  add  r2,r4,r2                      ; r2 = 0x27dac = "LCD is LG"
1c628  bl   0x424c                        ; slogf(99,1,"LCD is LG")
1c62c  ldr  r3,[pc,#40]  @ 0x1c65c        ; literal 0x00003578
1c630  add  r4,r4,r3                      ; r4 = 0x2e578
1c634  add  r0,r4,#20                     ; r0 = 0x2e58c  <-- LG table (0x2e578+20)
1c638  mov  r1,#11                        ; 11 words
1c63c  bl   0x1c4dc                       ; send_table(table,11)
```

* branch: **0x1c5e8 `tst r3,#65536` / 0x1c5ec `beq 0x1c618`**
* Hitachi path target: **0x1c5f0**, table argv **0x2e578**, count 9 (0x1c60c)
* LG path target: **0x1c618**, table argv **0x2e58c**, count 11 (0x1c638)
* polarity: `tst ... #0x10000` + `beq LG` means **bit16 == 1 => Hitachi**, matching the
  observed "LCD is Hitachi" for GPIO6 bit 16 = 1.

The register offset is confirmed by the two-step dereference: handle+0x138 = GPIO6 base
(0x1c598/0x1c5dc) then base+0x138 = the GPIO data-input register (0x1c5e0/0x1c5e4), which is
OMAP4 `GPIO_DATAIN` (0x138). The other bank, handle+0x118 (GPIO2), is used elsewhere
(e.g. 0x19078 `ldr r3,[r3,#280] @ 0x118`).

### 1.2 What it selects - and what it does not

It selects **only the SPI word table**. There is no other consumer of the decision: the
routine has no return value, writes no global, and its only memory effects are the two
`slogf` calls and the SPI writes. The called function `0x1c4dc` (see §5) just opens
`/dev/spi3` and writes N 16-bit words.

### 1.3 Where the routine is called, and only for Winchester

`0x1c580` has exactly one caller, at the end of the display sequencer:

```
193b0  ldr  r3,[pc,#68] @ 0x193fc     ; slot 0x100 = &platform (0x2e548)
193b4  ldr  r3,[r4,r3]
193b8  ldr  r3,[r3]                   ; r3 = platform
193bc  cmp  r3,#1
193c0  bne  0x193d4                  ; not Winchester -> skip
193c4  bl   0x1c580                  ; <<< panel identity + SPI table only on platform==1
193c8  mov  r0,#1
193cc  bl   0x1c6f8
193d0  b    0x193e4
193d4  cmp  r3,#2
193d8  bne  0x193e4
193dc  mov  r0,#1
193e0  bl   0x1c16c                  ; platform==2 (Colt) -> a completely different path
```

`platform == 1` is Winchester (default in .data, string "Platform Winchester" at 0x27974),
`platform == 2` is Colt (string "Platform Colt" at 0x27964). So the GPIO6-bit16 +
MCSPI4 mechanism is **Winchester-only**; the Colt tablet instead probes its panel over I2C
(§2.4).

## 2. The per-panel parameter structure (fields, offsets, Hitachi values)

### 2.1 There are 15 of them, 240 bytes each, and they are *named*

`DSI1_RGB888_VID_MODE_1024_600_60fps` (0x266d4) and its 9 siblings are 240-byte records in
.rodata. They are *not* panel-identity keyed; they are **refresh-rate keyed**, and the
key exists because the DSI clock (and therefore every DSI-side timing) changes with refresh.
For Colt there are also four *panel* variants (`Colt`, `_TMD`, `_CMI`, `_LG`) - and they are
**byte-identical** (verified: 240/240 bytes equal for all four). So even where QNX *has* a
panel-ID-keyed table, it holds no per-panel differences.

### 2.2 How a record becomes live

`main` (0x1a664, size 5616) does:

```
1aa04  ldr r3,[pc,#3080] @ 0x1b614    ; literal 0x369c
1aa08  add r3,r4,r3                   ; 0x2b000+0x369c = 0x2e69c
1aa0c  ldr r3,[r3,#-340]              ; r3 = *(0x2e548) = platform
1aa10  cmp r3,#2
1aa14  bne 0x1aa34
1aa18  ldr r3,[pc,#3104] @ 0x1b640     ; 0x148 -> slot &g_DISPC_DSI_Configuration_DSI1
1aa1c  ldr r0,[r4,r3]                 ; dst = 0x2ee08
1aa20  ldr r3,[pc,#3100] @ 0x1b644    ; 0x144 -> slot &DSI1_RGB888_VID_MODE_1280_768_Colt
1aa24  ldr r1,[r4,r3]                 ; src = 0x26044
1aa28  mov r2,#240
1aa2c  bl  0x4108                     ; memcpy(dst,src,240)
1aa30  b   0x1af10                    ; Colt: done, no fps switch
...
1aa34  ldr r3,[pc,#3032] @ 0x1b614     ; literal 0x369c
1aa38  add r3,r4,r3                    ; 0x2e69c
1aa3c  ldr r3,[r3,#-336]               ; r3 = *(0x2e54c) = refresh rate
1aa40  cmp r3,#70  ; 1aa44 beq 0x1ab1c   -> src slot 0x13c = 70fps
1aa4c  cmp r3,#40  ; 1aa50 beq 0x1aac8   -> src slot 0x1c0 = 40fps
1aa58  cmp r3,#30  ; 1aa5c bne ...       -> src slot 0x184 = 30fps  (0x1aab4)
1aa64  cmp r3,#50  ; 1aa68 beq 0x1aae4   -> src slot 0x1b4 = 50fps
1aa6c  cmp r3,#60  ; 1aa70 bne ...       -> src slot 0x118 = 60fps  (0x1ab08)
1aa78  cmp r3,#100 ; 1aa7c beq 0x1ab70   -> src slot 0x138 = 100fps
1aa84  cmp r3,#80  ; 1aa88 beq 0x1ab38   -> src slot 0x120 = 80fps
1aa8c  cmp r3,#90  ; 1aa90 bne 0x1abc4   -> src slot 0x1a8 = 90fps  (0x1ab54)
1aa98  cmp r3,#110 ; 1aa9c beq 0x1ab8c   -> src slot 0x160 = 110fps
1aaa0  cmp r3,#120 ; 1aaa4 bne 0x1abc4   -> src slot 0x19c = 120fps
1abc4  ... printf("%s -R refresh out of range [30,40,50,60,70,80,90,110,120] defaulting to 60fps")
1abe4  ... memcpy from slot 0x118 = 60fps   (default)
```

Every selected record is memcpy'd (240 bytes, `mov r2,#240 @ 0xf0` at 0x1aac0 etc.) into
**`g_DISPC_DSI_Configuration_DSI1` at 0x2ee08**. No field is patched afterwards in this path.

The rate at 0x2e54c comes from the `-R` option: the getopt string at 0x27a34 is
`vbrspR:t:T:f:l:x:y:c:U:` and the range diagnostic at 0x27a68 is
`"%s -R refresh out of range [30,40,50,60,70,80,90,110,120] defaulting to 60fps"`.
`[inference]` on the exact store site (we did not trace `getopt` back to the write); the
.data default of 0x3c = 60 is a fact.

### 2.3 The record, field by field (Winchester 1024x600 @60 fps, at 0x266d4)

The record is read by three functions, each of which copies fields into an L1DSS register
descriptor block:

* **`0x162ec(dsiCfg, ctx)`** - builds the DISPC (LCD manager) parameter block at **0x2e5c4**
  (computed as `r0 = r4 + 0x35c4`, 0x162fc/0x16300).
* **`0x179dc(dsiCfg)`** - builds the DSI video-mode/PHY descriptor, incl. the DSI PLL and DSI
  timing descriptors.
* **`0x17094(dsiCfg)`** - DSI "second pass"; reads only 0x00 and 0x04 (0x17278, 0x173a8).

"d" = destination offset inside the block the reading function fills. All values below are
read directly from the file at 0x266d4 (Winchester/60fps) and 0x26044 (Colt, for contrast).

| off | width | read at | Win 1024x600@60 | Colt 1280x768 | destination / transform | notes |
|---|---|---|---|---|---|---|
| 0x00 | u32 | 0x17278 | 1 | 1 | compared with 1 (`cmp r3,#1` 0x1727c) | DSI config selector |
| 0x04 | u32 | 0x173a8, 0x163ac | 1 | 1 | (a) `ldr r3,[r2,#4]; cmp r3,r1` with `r1=0` (0x17394) -> if !=0 store 3 else 0 at +0x130; (b) at 0x163b0 `cmp r0,#1`: if ==1 -> d+0x50=0 and d+0x70h=0, else 1 and 300 | |
| 0x08 | u32 | 0x1637c | 8 | 8 | `cmp #6` (0x16380); if ==6 -> d+0x54=1 else d+0x54=3 | |
| 0x0c | u32 | - | 1 | 1 | (no read found in the traced functions) | |
| 0x10 | u32 | - | 0 | 0 | (no read found) | |
| 0x14 | u32 | - | 1 | 1 | (no read found) | |
| **0x18** | u16 | 0x163f8 `ldrh ip,[r2,#24]` | **600** | 1280 | `d+0x88 = v-1` (`sub ip,#1` 0x163fc, `strh` 0x16400) | vertical size |
| **0x1c** | u16 | 0x16404 `ldrh ip,[r2,#28]` | **1024** | 768 | `d+0x8a = v-1` (0x1640c) | horizontal size |
| 0x20 | u32 | - | 8 | 8 | (no read found) | |
| 0x24 | u32 | - | 3072 | 2304 | (no read found in traced fns) | = 3 x field@0x1c (24bpp bytes/line) `[inference]` |
| 0x28 | u16 | 0x16330 | 12 | 9 | `d+0x08 = v` (0x16334) | part of H/V timing |
| 0x2c | u8 | 0x16338 `ldrb` | 1 | 4 | `d+0x0a = v-1` (0x16340) | byte-sized -> a porch field |
| 0x30 | u16 | 0x16328 | 12 | 3 | `d+0x06 = v` (0x1632c) | |
| 0x34 | u16 | 0x16310 | 162 | 126 | `d+0x02 = v-1` (0x16318) | |
| 0x38 | u8 | 0x1631c `ldrb` | 20 | 1 | `d+0x04 = v-1` (0x16324) | byte-sized -> a porch field |
| 0x3c | u16 | 0x16304 | 138 | 45 | `d+0x00 = v-1` (0x1630c) | |
| 0x40 | u16 | 0x1852c `ldrh [r5,#64]` | 1008 | 705 | DSI PLL desc `+0x22` (0x18530) | |
| 0x44 | u32 | 0x1851c | 12 | 9 | PLL desc `+0x20` | |
| 0x48 | u32 | 0x18514 | 1 | 4 | PLL desc `+0x1f` | |
| 0x4c | u32 | 0x18524 | 12 | 3 | PLL desc `+0x21` | |
| 0x50 | u16 | 0x18534 `ldrh [r5,#80]` | 600 | 1280 | PLL desc `+0x24` | equals field@0x18 in both records |
| 0x54 | u16 | 0x184fc `ldrh [r5,#84]` | 121 | 94 | PLL desc `+0x1a` | |
| 0x58 | u32 | 0x184f4 | 1 | 1 | PLL desc `+0x18` (strb) | |
| 0x5c | u16 | 0x18504 `ldrh [r5,#92]` | 116 | 32 | PLL desc `+0x1c` | |
| 0x60 | u32 | 0x1642c | 2 | 2 | `strb d+0x0c` (0x16430) | |
| 0x64 | u32 | 0x16424 | 1 | 1 | `strb d+0x0b` (0x16428) | |
| 0x68 | u32 | - | 60 | 60 | (no read found) | likely "target refresh, Hz" `[inference]`; constant 60 even in the 30/120 fps records |
| 0x6c | u32 | - | 90 | 90 | (no read found) | |
| 0x70 | u32 | - | 300 | 300 | (no read found) | |
| 0x74 | u32 | 0x17dc0 | 5 | 5 | DSI desc `+0x249` (strb) | |
| 0x78 | u32 | 0x17db8 | 5 | 5 | DSI desc `+0x248` (strb) | |
| 0x7c | u32 | 0x17dd4 | 95 | 51 | DSI desc `+0x24c` (strb) | varies with refresh |
| 0x80 | u16 | 0x17dcc | 756 | 877 | DSI desc `+0x24a` | **exactly 12.6 x refresh** (378/504/630/756/882/1008/1134/1260/1386/1512 for 30..120) |
| 0x84 | u32 | 0x18004 | 11 | 18 | DSI desc byte `+0x140` | clock-domain |
| 0x88 | u32 | 0x1800c | 25 | 41 | `+0x141` | clock-domain |
| 0x8c | u32 | 0x18014 | 13 | 19 | `+0x142` | clock-domain |
| 0x90 | u32 | 0x1801c | 19 | 32 | `+0x143` | clock-domain |
| 0x94 | u32 | 0x18064 | 9 | 15 | `+0x150` | clock-domain |
| 0x98 | u32 | 0x1804c | 34 | 59 | `+0x149` | clock-domain |
| 0x9c | u32 | 0x18044 | 10 | 16 | `+0x148` | clock-domain |
| 0xa0 | u32 | 0x1803c | 4 | 6 | `+0x147` | clock-domain |
| 0xa4 | u16 | 0x18544 | 11 | 16 | PLL desc `+0x32` | |
| 0xa8 | u16 | 0x1854c | 9 | 14 | PLL desc `+0x34` | |
| 0xac | u32 | 0x18560 | 14 | 23 | DSI desc `+0x26` (strb) | |
| 0xb0 | u32 | 0x18568 | 11 | 14 | DSI desc `+0x27` (strb) | |
| 0xb4 | u32 | 0x17d50 | 5 | 10 | DSI desc `+0x230` (strb) | |
| 0xb8 | u32 | 0x18024 | 2 | 2 | DSI desc `+0x144` (strb) | |
| 0xbc | u32 | 0x1802c | 0 | 0 | `+0x145` | |
| 0xc0 | u32 | 0x18034 | 2 | 2 | `+0x146` | |
| 0xc4 | u32 | 0x18098 | 5 | 5 | `+0x1cc` (word) | |
| 0xc8 | u32 | 0x180a8 | 4 | 4 | `+0x1d4` | |
| 0xcc | u32 | 0x180b8 | 3 | 2 | `+0x1dc` | |
| 0xd0 | u32 | 0x180c8 | 2 | 1 | `+0x1e4` | |
| 0xd4 | u32 | 0x180d8 | 1 | 3 | `+0x1ec` | |
| 0xd8 | u32 | 0x18090 | 0 | 1 | `+0x1c9` (strb) | |
| 0xdc | u32 | 0x180a0 | 0 | 1 | `+0x1d0` (strb) | |
| 0xe0 | u32 | 0x180b0 | 0 | 0 | `+0x1d8` (strb) | |
| 0xe4 | u32 | 0x180c0 | 0 | 0 | `+0x1e0` (strb) | |
| 0xe8 | u32 | 0x180d0 | 0 | 1 | `+0x1e8` (strb) | |
| 0xec | u32 | - | 62 | 62 | (no read found) | constant in all 15 records |

Facts that fall straight out of the table:

* **Nothing in 0x00..0x7c or 0xc4..0xec changes with refresh rate** - only 0x80..0xb4 does.
  That is the fingerprint of "panel timing is fixed, the DSI clock is what the -R switch
  changes": the values at 0x80..0xb4 are linear in the refresh rate (0x80 = 12.6 x fps
  exactly, and the rest scale the same way), i.e. they are **DSI bit-period counts**, not
  line counts.
* **No field anywhere in the record is LVDS related.** There is no "link mode", no
  "single/dual", no divider, no bridge register value. The record is a DSI + DISPC record.

### 2.4 The only true panel-ID-keyed table in the entire boot set (and it is empty)

For information, because it is the pattern the task was looking for: the Colt path **does**
key a table off a panel ID. Function `0x14604` (called from the sequencer at 0x1914c, result
stored at [sp+12] and consumed at 0x19150..0x19188) probes a panel over I2C and then:

```
14708  cmp  r0,#1
1470c  ble  0x14734            ; <=1 byte read -> "Invalid or no panel connected" and give up
14710  ldrb r3,[sp,#141]       ; panel id byte
14714  and  r3,r3,#3
14718  cmp  r3,#3
1471c  addls pc,pc,r3,lsl #2   ; jump table
14720  b    0x15d9c            ; default
14724  b    0x14f0c            ; id 0 -> "TMD panel detected" (0x25ee4)  -> slot 0x1c4 = Colt_TMD
14728  b    0x1474c            ; id 1 -> "CMI panel detected" (0x25ed0)  -> slot 0x1a0 = Colt_CMI
1472c  b    0x14f0c            ; id 2 -> TMD block again
14730  b    0x156e8            ; id 3 -> "LG panel detected"  (0x25ef8)  -> slot 0x108 = Colt_LG
```

Each block stores the selected record pointer (`str r3,[r8]` at 0x14768 / 0x14f28 / ...) and
then runs its own I2C register script. **But the four records are byte-identical** (verified),
so the selection changes only which init script runs and which string is logged - not a
single timing value. On Winchester this whole path returns NULL (the I2C probe does not
answer), and the sequencer falls back to `g_DISPC_DSI_Configuration_DSI1`
(`ldr r0,[sp,#12]; cmp r0,#0; beq 0x1918c` at 0x19150/0x19154/0x19158).

## 3. LVDS link mode / pixel-clock divider: where it is set

**Answer: nowhere. It is hard single-link.** The evidence:

1. **The only write to `LV_CFG` (0x049C) in the whole image is with the value 1**, at
   0x13fd0, and there is exactly one `movw #0x49c` in the 193024-byte file:

   ```
   13fd0  movw r0,#1180  @ 0x49c
   13fd4  mov  r1,#1
   13fd8  bl   0x13ce0            ; i2c_write_reg(0x049C, 0x00000001)
   ```

   `0x13ce0` is the bridge's register-write helper: it builds a 6-byte I2C buffer
   `{reg_lo, reg_hi, v0, v1, v2, v3}` (0x13ce8..0x13d0c: register address little-endian in
   bytes 0-1, then the 32-bit value **little-endian** in bytes 2-5) and issues it with
   `len=6` on I2C instance 1 (0x13d10..0x13d2c). So the LV_CFG write is `9C 04 01 00 00 00`
   on the wire.

2. **The bridge init script is unconditional and takes no panel information at all.**
   `0x13e50` is `push {r4,lr}` followed by exactly 30 straight-line `bl 0x13ce0` calls
   (13e64 13e70 13e84 13e90 13e9c 13ea8 13eb4 13ec0 13ecc 13ed8 13ee4 13ef4 13f04 13f14
   13f24 13f34 13f44 13f54 13f64 13f74 13f84 13f94 13fa4 13fb4 13fcc 13fd8 13fe4 13ff0
   13ffc 14008); it has exactly
   one `bl` inside its body (the 100 us delay at 0x13fb8 -> 0x40f0 = usleep(100)) and no
   conditional branch. It is called once, from the display sequencer, at **0x1938c**, on
   every platform.

3. The LVDS/"video-path" registers it writes are hard-coded to **1024 x 600**:

   ```
   13f68  mov  r0,#1104 @ 0x450 ;  13f6c movw r1,#0x120 ; 13f70 movt r1,#0x3f0
          -> i2c_write_reg(0x0450, 0x03F00120)
   13f78  movw r0,#0x454 ; movw r1,#1     ; movt r1,#0x78  -> i2c_write_reg(0x0454, 0x00780001)
   13f88  movw r0,#0x458 ; movw r1,#1008  ; movt r1,#0x76  -> i2c_write_reg(0x0458, 0x00760400)  ; 0x400 = 1024
   13f98  movw r0,#0x45c ; movw r1,#1     ; movt r1,#0x0a  -> i2c_write_reg(0x045C, 0x000A0001)
   13fa8  mov  r0,#1120 @ 0x460 ; movw r1,#0x258 ; movt r1,#0x0c
          -> i2c_write_reg(0x0460, 0x000C0258)                                              ; 0x258 = 600
   14000  movw r0,#0x464 ; mov r1,#1      -> i2c_write_reg(0x0464, 1)
   ```

   Registers 0x0458 and 0x0460 carry 1024 and 600 in their low halves. `[inference]`: these
   are the video-path active-size fields (H then V); the literal constants 0x400/0x258 are
   the only place in the bridge script where the panel geometry appears.

   **This is now confirmed, not inferred** - cross-referencing report 129 §6, which recovered
   the register names from the Android driver `mipi_tc358764_dsi2lvds.c`
   (`android-msm-hammerhead-3.4-kk-r1`):

   | reg | report-129 name | QNX value | decoded |
   |---|---|---|---|
   | 0x0450 | `VPCTRL` | 0x03F00120 | event mode (cf. 0x01000120 in the Android driver) |
   | 0x0454 | `VP_HTIM1 = (HBP<<16)\|HPW` | 0x00780001 | **HBP = 120, HPW = 1** |
   | 0x0458 | `VP_HTIM2 = (HFP<<16)\|HACT` | 0x00760400 | **HFP = 118, HACT = 1024** |
   | 0x045C | `VP_VTIM1 = (VBP<<16)\|VPW` | 0x000A0001 | **VBP = 10, VPW = 1** |
   | 0x0460 | `VP_VTIM2 = (VFP<<16)\|VACT` | 0x000C0258 | **VFP = 12, VACT = 600** |
   | 0x0464 | `VFUEN` | 1 | video-frame-update enable |
   | 0x049C | `LV_CFG` | 1 | LVEN, single link, PCLK = DCLK |
   | 0x0520 | `GPIOC`, 0x0524 `GPIOO` | 5, 5 | (power-on); power-off writes `GPIOO = 0` at 0x19b64 |
   | 0x0504 | — | 4 | unnamed |
   | 0x04A0 / 0x04A4 | — | 0x0440C02D, then 0x0400C02D / 0 | unnamed |

   The HACT = 0x400 = 1024 and VACT = 0x258 = 600 landing exactly in the two registers report
   129 names as the active-size fields is decisive: **the bridge is told this is a 1024x600
   panel, in the same 6-register video-path block, with no panel-dependent alternative.**

   Because VPCTRL selects event mode, the bridge does **not** forward the DSI blanking: it
   regenerates the LVDS timing from `VP_*TIM1/TIM2`. So the timing the *panel* actually sees
   is:

   ```
   H:  HACT 1024 + HBP 120 + HFP 118 + HPW 1  = 1263 pixel clocks
   V:  VACT  600 + VBP  10 + VFP  12 + VPW 1  =  623 lines
   LVDS pclk = 1263 x 623 x 60 = 47.19 MHz      (if the frame rate is the nominal 60)
   ```

   while the DISPC/DSI timing in §4.2 (1344 x 625 = 50.4 MHz) is the *transmitter* side. The
   two are independent by construction in event mode. **The 1263 x 623 set is the one to give
   a Linux panel driver**, because it is what the panel's LVDS receiver is actually fed.

4. The remaining LVDS-block writes are bit/lane permutation tables, identical for both
   panels: `0x0480=0x03020100`, `0x0484=0x08050704`, `0x0488=0x0F0E0A09`,
   `0x048C=0x100D0C0B`, `0x0490=0x12111716`, `0x0494=0x1B151413`, `0x0498=0x061A1819`
   (0x13ef8..0x13f64). The 28 bytes are a permutation of 0..0x1B (4 lanes x 7 bits)
   `[inference]` - i.e. **four LVDS data pairs**, which is consistent with a single
   24bpp link, not with dual link (dual link would need independent odd/even mapping).

5. The PLL/clock side of the bridge, written around a 100 us delay:
   ```
   13ee8  mov r0,#1184 @ 0x4a0 ; movw r1,#0xc02d ; movt r1,#0x44
          -> i2c_write_reg(0x04A0, 0x0440C02D)
   13edc  movw r0,#0x4a4 ; mov r1,#0
          -> i2c_write_reg(0x04A4, 0x00000000)
   13fb8  mov r0,#100 ; bl 0x40f0            ; 100 us (0x40f0 is the small-delay helper)
   13fc0  mov r0,#1184 @ 0x4a0 ; movw r1,#0xc02d ; movt r1,#0x04
          -> i2c_write_reg(0x04A0, 0x0400C02D)   ; bit 26 dropped
   13fd0  LV_CFG = 1
   13fdc  i2c_write_reg(0x0520, 5) ; 13fe8 i2c_write_reg(0x0524, 5)
   13ff4  i2c_write_reg(0x0504, 4) ; 14000 i2c_write_reg(0x0464, 1)
   ```

   The 0x04A0 double write (bit 26 set, delay, bit 26 clear) is the only "clock-ish"
   sequencing in the whole script. **We cannot name the field statically** - no string, no
   comment, no second use of the constant exists in the image, and 0x04A0/0x04A4 are not
   mentioned in the task's driver notes. If 0x04A0 is the LVDS PLL control, the divider is
   baked into 0x0400C02D and is not selectable.

6. On the SoC side nothing chooses a link mode either: the DSS/DSI layer only ever sees one
   fixed 240-byte record per refresh rate (§2), and no record contains an LVDS field (§2.3).

The other 5 bridge writes in the image are in the power-off routine at **0x196dc**
(`0x0520 = 5` at 0x196f0/0x196f8, `0x0524 = 0` at 0x19b5c/0x19b64, plus teardown writes at
0x198d0, 0x199ac, 0x19a88). They also never touch 0x049C.

So: **QNX never writes `LV_CFG` with anything other than 1**, and it configures the link mode
nowhere else. Single link, `PCLK = DCLK` (LVEN only), is the only mode QNX ever programs for
this panel.

## 4. Timing and pixel clock for this panel

### 4.1 Resolution - present, unambiguous

* field **0x18 = 600** (u16, read at 0x163f8) and **0x1c = 1024** (u16, read at 0x16404);
  these are stored minus 1 into the DISPC parameter block (`d+0x88`, `d+0x8a`).
* field **0x24 = 3072 = 3 x 0x1c** - bytes per line for 24 bpp `[inference]`.
* the record *symbol name itself* is `DSI1_RGB888_VID_MODE_1024_600_60fps`.
* the framebuffer/splash code independently agrees: platform==1 clamps/centres the splash with
  `rsb r2,r7,#1024` (x, 0x1cb54) and `rsb r2,r8,#600` (y, 0x1cbd8), while platform==2 uses
  1280/768 (0x1cb74/0x1cbf8) - so the Winchester framebuffer is 1024 wide x 600 tall.
  Separately `0x1cc3c mov r2,#1024; str r2,[r3,#0xf4]` and `0x1cc44 mov r2,#512;
  str r2,[r3,#0xf8]` put 1024 and 512 into the LCD context; those two are later copied into the
  DISPC block at +0xb6/+0xb8 (`ldrh [r0,#0xf4]`/`ldrh [r0,#0xf8]` at 0x164b4/0x164bc). The 512
  is *not* the panel height, so it is some intermediate/line-buffer dimension `[inference]` -
  recorded here only so the number is not mistaken for geometry later.
* the bridge agrees: 1024 and 600 in TC358764 0x0458/0x0460 (§3.3).

`[inference, high confidence]`: 0x18 = vertical (lines), 0x1c = horizontal (pixels). This is
forced by the fact that 0x24 = 3 x 0x1c in *both* families (3072 = 3x1024 for Winchester,
2304 = 3x768 for Colt): whichever dimension is multiplied by 3 to get bytes-per-line must be
the horizontal one, and it is 0x1c in both. It also matches the fact that Colt's Z10-class
panel scans natively 768 wide x 1280 lines.

### 4.2 The DISPC timing sextet (0x28..0x3c) and what it becomes

`0x162ec` unpacks six fields into the DISPC parameter block at 0x2e5c4:

| record off | Win value | -> block off | transform |
|---|---|---|---|
| 0x3c | 138 | +0x00 | -1 |
| 0x34 | 162 | +0x02 | -1 |
| 0x38 | 20 (u8) | +0x04 | -1 |
| 0x30 | 12 | +0x06 | as-is |
| 0x28 | 12 | +0x08 | as-is |
| 0x2c | 1 (u8) | +0x0a | -1 |

Note the -1 discipline: the first group (0x3c/0x34/0x38) is "value-1", the second
(0x30/0x28/0x2c) is mixed. `[inference]` This is the DISPC `TIMING_H`/`TIMING_V` register pair,
whose fields are "pulse/porch minus one" on some fields and raw on others; the two groups are
therefore the H triplet {138,162,20} and the V triplet {12,12,1}, but **the exact
porch/sync permutation inside each triplet cannot be established statically** (see §7).

Consequences, *if* the natural assignment (h: 138 sync / 162 back / 20 front, v: 12 sync /
12 back / 1 front) holds `[inference]`:

```
H_total = 1024 + 138 + 162 + 20 = 1344
V_total =  600 +  12 +  12 +  1 =  625
pclk    = 1344 x 625 x 60 = 50.40 MHz
```

50.4 MHz is a very ordinary 1024x600 pixel clock, which is weak corroboration but not proof.

**Important scoping:** the totals above are the **DSI transmitter** side (DISPC video mode).
The timing the *panel* receives is generated inside the TC358764 from its own `VP_*` registers
and is **1263 x 623** (§3, item 3). Both are real; they are not supposed to be equal, because
`VPCTRL` selects event mode. For a Linux panel definition use the 1263 x 623 set.

### 4.3 Is a pixel clock in Hz present? No.

There is **no field that is a frequency in Hz** anywhere in the record. The closest things:

* 0x68 = **60** in all 15 records (even the 30 fps and 120 fps ones) - `[inference]` a nominal
  refresh in Hz, used only as a default/limit; we found no reader for it in the traced
  functions.
* 0xec = **62** in all 15 records - no reader found; meaning unknown.
* **0x80 = 12.6 x refresh exactly** (378, 504, 630, 756, 882, 1008, 1134, 1260, 1386, 1512 for
  30..120 fps) - a *clock-domain* quantity, not a frequency in Hz.
* 0x40 / 0x50 = 1008 / 600 (Win), 705 / 1280 (Colt) - copied into the DSI PLL descriptor
  (bytes +0x22 and +0x24) together with 0x44/0x48/0x4c and 0x54/0x58/0x5c. `[inference]`
  these are the DSI PLL divider/multiplier operands; the actual PLL output frequency is
  computed on-chip from them and is not stored as a number.
* The TC358764 PLL value 0x0400C02D (§3) is likewise a register image, not a Hz value.

So the pixel clock must be derived (from §4.2) or measured.

The best-supported number is the **panel-facing LVDS clock of 47.19 MHz** (1263 x 623 x 60),
derived from the TC358764 `VP_*` registers whose HACT/VACT fields are named and whose values
are 1024/600. The DSI-side 50.40 MHz is the second candidate and applies to the link, not the
panel. Both rest on "60 Hz"; if the real frame rate differs, both scale linearly. Measuring
the LVDS clock at the panel connector (or DSI byte clock) settles it in one probe.

### 4.4 The DSI timing fields (0x74..0xb4)

These are the counts that scale with the selected refresh; for 60 fps:
0x74=5, 0x78=5, 0x7c=95, 0x80=756, 0x84=11, 0x88=25, 0x8c=13, 0x90=19, 0x94=9, 0x98=34,
0x9c=10, 0xa0=4, 0xa4=11, 0xa8=9, 0xac=14, 0xb0=11, 0xb4=5.
They are stored one byte at a time into the DSI descriptor (0x140..0x150, 0x1c0..0x1ec,
0x248..0x24c, 0x230), and the descriptor is then handed to the L1DSS writers with instance id
55 (`mov r0,#55 @ 0x37`) at 0x18078 (`bl 0x1e28c`), 0x18084 (`bl 0x1e0cc`), 0x180f0
(`bl 0x1ded4`), 0x1855c (`bl 0x2062c`, the DSI PLL field packer) and 0x18100 (`bl 0x1fdd8`).
Those writers consume *field bundles*, not raw register addresses (0x1e28c for example
re-assembles a big-endian 32-bit value from descriptor bytes 0..3 and writes it), so mapping
each field to a named DSI register requires decoding the L1DSS ABI - not done here.

## 5. The 9-word Hitachi SPI table, decoded or not

**Not decoded.** There is no name, comment, struct, or second interpretation of the words
anywhere in the boot set:

* `strings` finds no symbol near them; the dynamic symbol table has no entry for 0x2e578 or
  0x2e58c (both are anonymous .data, sitting in the last 44 bytes of the RW segment).
* the only code that touches them is 0x1c580 (§1) and the generic sender 0x1c4dc. The counts
  9 and 11 exist only as immediates (`mov r1,#9` 0x1c60c, `mov r1,#11` 0x1c638).
* the "Hitachi" string is used only in the `slogf`, never as a key into a struct.

### 5.1 Exact contents (verified from the file)

```
vaddr 0x2e578 (file 0x2d578), 9 u16, little-endian in memory:
   29 00  25 00  40 08  AD 00  30 04  5F 38  A4 3C  09 34  41 40
   = LE u16:  0029 0025 0840 00AD 0430 385F 3CA4 3409 4041

vaddr 0x2e58c (file 0x2d58c) = 0x2e578 + 20, 11 u16:
   29 00  25 00  30 04  40 08  5F 38  A4 3C  00 34  43 08  28 28  41 40  AD 00
   = LE u16:  0029 0025 0430 0840 385F 3CA4 3400 0843 2828 4041 00AD
```

This matches reports 111 §3.5 and 123 §3.5 byte for byte, and confirms the LG table starts at
0x2e578+20 (the code computes it as `add r0,r4,#20` after `r4 = 0x2e578`, 0x1c630/0x1c634).

### 5.2 What the sender does (0x1c4dc)

```
1c4e4  mov  sl,r0                  ; table pointer
1c4e8  mov  r6,r1                  ; word count
1c4ec  bl   0x1c410                ; open("/dev/spi3")  (string 0x27d54, see 0x1c420/0x1c424)
1c4f4  cmp  r6,#0 ; ble -> done
loop (r4 = index):
1c514  lsl  r3,r4,#1
1c518  ldrh r3,[sl,r3]             ; word
1c51c  lsr  r2,r3,#8
1c520  orr  r3,r2,r3,lsl #8        ; byte swap
1c524  strh r3,[sp,#14]
1c528..1c538  bl 0x22c90           ; spi_write(dev, chan=0, &word, len=2)
1c544  cmp  r4,#0
1c548  cmpne r5,r6                 ; r5 = index+1
1c54c  beq  0x1c558                ; after index 0 and after the LAST word:
1c550  cmp  r4,#2
1c554  bne  0x1c560                ; also after index 2:
1c558  ldr  r0,[sp,#4]  (=0x186A0) ; 100000
1c55c  bl   0x40f0                 ; delay 100000 us = 100 ms
1c560  cmp  r6,r7 ; movgt r4,r5 ; bgt loop
```

and the device setup (0x1c410 -> 0x1c468..0x1c488):

```
1c468  movw r3,#0xc508 ; str r3,[sp]        ; cfg word 0 = 0x0000C508
1c470  movw r3,#0xddd0 ; movt r3,#6
1c478  str  r3,[sp,#4]                      ; cfg word 1 = 0x0006DDD0 = 450000
1c47c  mov  r0,r5                           ; fd
1c480  mov  r1,#0xa0000000
1c484  mov  r2,sp
1c488  bl   0x22bf8                         ; spi_setcfg(fd, 0xA0000000, {0xC508, 450000})
```

i.e. **SPI clock 450 kHz**, config word 0xC508 (which encodes mode 0 / MSB-first per the QNX
`spi.h` flag layout used elsewhere in this image - the task's established facts already fix
this as mode 0, MSB-first, high byte first), and **the byte swap at 0x1c51c/0x1c520 is what
makes the high byte go first**. The identification of the callee as `spi_setcfg` is not a
guess: its failure path at 0x1c49c..0x1c4b4 `fwrite`s the literal "spi_setcfg failed."
(0x27d88), and the open path at 0x1c43c..0x1c458 `fprintf`s "SPI open failed, fd = %d
errno = %d" (0x27d60).

Helper identities used in this report were resolved from `.rel.plt` (DT_JMPREL 0x3e98, 61
entries) with stub(i) = 0x409C + 12*i, which yields usleep = 0x40f0 (idx 7), memcpy = 0x4108
(idx 9), slogf = 0x424c (idx 36), write = 0x41b0 (idx 23), exit = 0x4330 (idx 55). So the
delay at 0x1c55c with 100000 is `usleep(100000)` = **100 ms**, and the one at 0x13fb8 with
100 is `usleep(100)` = 100 us.

So each table entry is emitted as a standalone 2-byte transfer, with 100 ms pauses after
entry 0, entry 2 and the last entry.

### 5.3 The only structural fact worth carrying forward

Treat the transmitted values as the logical 16-bit constants (the swap at 0x1c51c/0x1c520
undoes the little-endian storage, so the 16-bit value put on the wire equals the LE u16 in
memory; the actual byte stream is `00 29 00 25 08 40 00 AD 04 30 38 5F 3C A4 34 09 40 41`):

```
Hitachi (9): 0029 0025 0840 00AD 0430 385F 3CA4 3409 4041
LG     (11): 0029 0025 0430 0840 385F 3CA4 3400 0843 2828 4041 00AD
```

* 8 of the 9 Hitachi words appear in the LG table.
* Hitachi has `3409` where LG has `3400` **and** `0843` **and** `2828`.
* The shared prefix `0029 0025` and the shared tail `4041` are suggestive of a
  "start - configuration - end" sequence rather than an address/value pair list: 9 is an odd
  count, and no (register,value) split of the 18 bytes yields plausible register addresses
  for any device we can identify. `[inference]` this is an opaque panel/TCON configuration
  blob, order-sensitive (the delays after index 0, 2 and the last entry only make sense for
  a state machine).

The device on MCSPI4 is **not identified** statically. The TC358764 is I2C-only (all its
writes go through 0x13ce0 on I2C instance 1), so the SPI4 target is some other part
(panel-side TCON / level shifter / second bridge). No string in the image names it.

## 6. What this means for Linux

1. **You do not need a dual-link or divider decision.** QNX programs the TC358764 with
   `LV_CFG = 0x00000001` (LVEN, single link, PCLK = DCLK) and never anything else. Use
   single link in the Linux `tc358764` bridge setup for this panel.
2. **Panel timing: 1024x600, and the porches the panel actually sees come from the bridge.**
   With the report-129 register names, the QNX bridge writes decode to
   `VP_HTIM1 = 0x00780001` (HBP 120, HPW 1), `VP_HTIM2 = 0x00760400` (HFP 118, HACT 1024),
   `VP_VTIM1 = 0x000A0001` (VBP 10, VPW 1), `VP_VTIM2 = 0x000C0258` (VFP 12, VACT 600) -
   i.e. the LVDS frame is 1263 x 623 at ~47.19 MHz. Give the Linux panel definition that.
   The DISPC/DSI side (H {138,162,20}, V {12,12,1} from record offsets 0x3c/0x34/0x38 and
   0x30/0x28/0x2c) only has to satisfy the bridge, and it is the *same for Hitachi and LG* -
   QNX uses one record for both.
3. **The Hitachi/LG difference is only the MCSPI4 sequence.** Reproduce it exactly:
   `/dev/spi3`-equivalent (MCSPI4), mode 0, MSB-first, 450 kHz, one 16-bit word per transfer,
   100 ms delay after word 0, word 2 and the last word; 9 words
   `0029 0025 0840 00AD 0430 385F 3CA4 3409 4041` for GPIO6 bit16 = 1 (Hitachi), else the 11
   LG words. Note the delay pattern means a plain `spi_write(buf, 18)` will *not* reproduce
   it.
4. **The bridge is programmed identically for both panels** (30 I2C register writes in a
   fixed order, 0x13e50): the important ones are 0x0450=0x03F00120, 0x0454=0x00780001,
   0x0458=0x00760400 (H active 1024), 0x045C=0x000A0001, 0x0460=0x000C0258 (V active 600),
   0x0464=1, the bit-permutation block 0x0480..0x0498, 0x04A0=0x0400C02D (after a first write
   of 0x0440C02D and a 100 us delay), 0x04A4=0, 0x049C=1, 0x0520=5, 0x0524=5, 0x0504=4.
   QNX's I2C framing is *6-byte messages: 16-bit register LE, then 32-bit value LE* - check
   that against regmap's default endianness before reusing the mainline driver's tables.
5. `ppi_cnt` (0x2e55c, .data value **4**) is written into TC358764 0x0164/0x0168/0x016C/0x0170
   (0x13e7c..0x13ea8). Whatever those PPI registers mean, QNX gives all four the value 4.
   `[inference]` that is the DSI lane count (4 lanes).
6. **Refresh is a free parameter**: 30/40/50/60/70/80/90/100/110/120 fps are all shipped, and
   only the DSI clock-domain fields change. 60 is the built-in default (0x2e54c = 0x3c).
   If Linux needs to raise the DSI clock for stability, those 10 variants are a ready-made
   ladder - but only 0x80..0xb4 differ, so you only have to change DSI timings, never the
   panel timing.

## 7. What remains uncertain

1. **Which of {138,162,20} is sync / back-porch / front-porch.** The record order (0x3c,0x34,
   0x38) and the -1 discipline are known exactly, but the L1DSS parameter block consumes them
   in the order (+0x00,+0x02,+0x04) via a register-descriptor writer (`bl 0x1e28c` etc.) whose
   field→register mapping we did not decode. The candidate pclk of 50.4 MHz in §4.2 depends
   on this assignment *for the split between H and V* only in as much as the totals are
   right; the assignment within each triplet does not change the total, so §4.2's totals are
   robust even though the labels are not.
2. **No pixel clock in Hz exists in the data.** Any Hz value must be derived from the totals
   or measured on the LVDS clock.
3. **Semantics of TC358764 0x04A0/0x04A4** (the only divider-like writes) are unknown: no
   string in the image, and neither report 129 (which names 0x0450/0x0454/0x0458/0x045C/0x0460/
   0x0464/0x049C/0x0520/0x0524/0x0528/0x0580) nor anything else in this workspace names them.
   Only the bit-26 set/clear sequence and the 100 us spacing are established. They are the
   most likely home of an LVDS-PLL divider (`0x0400C02D`), so if the LVDS clock turns out not
   to be ~47.19 MHz, look here first.
4. **The identity of the MCSPI4 device** (the thing that consumes the 9/11-word tables) is
   unknown. No string names it; the TC358764 is I2C-only.
5. **Fields 0x0c, 0x10, 0x14, 0x20, 0x24, 0x68, 0x6c, 0x70, 0xec of the record** have no
   reader in the three functions we traced (0x162ec, 0x17094, 0x179dc). They may be read by
   code paths we did not follow (other platform branches, DSI command mode) or be vestigial.
6. **The DSI descriptor field→register mapping** (0x140..0x150, 0x1c0..0x1ec, 0x230,
   0x248..0x24c, and the PLL block +0x18..+0x24) is recorded only as descriptor byte offsets.
   Naming them (`DSI_VM_TIMING*`, `DSI_PLL_CONFIGURATION*`) would need the L1DSS ABI or the
   OMAP4 TRM field tables; we deliberately did not guess.
7. `platform` is 1 = Winchester / 2 = Colt, established from 0x193b0-0x193e0 and the
   splash-geometry code at 0x1cb44/0x1cbc8. We did not find the code that *writes* 0x2e548
   (it is set in `main`, 0x1a664..0x1bc14, by option/`strstr("colt")` logic we did not
   exhaustively trace).
8. **Whether the LVDS frame rate really is 60 Hz.** In event mode the bridge free-runs from
   its own PLL, so `H_total x V_total x 60 = 47.19 MHz` is an inference from "QNX asked DISPC
   for 60 fps", not an independent measurement. QNX driving the panel correctly does not prove
   60.000 Hz - the bridge would tolerate a small mismatch (with tearing) rather than show a
   wrong image. One scope probe on the LVDS clock settles both this and §4.3.

## 8. Evidence index (vaddr -> fact)

| vaddr | fact |
|---|---|
| 0x1c580 | panel identity function: reads GPIO6 DATAIN, logs, sends SPI table |
| 0x1c588/0x1c58c | PIC anchor r4 = 0x2b000 (0x2b000 = DT_PLTGOT) |
| 0x1c590-0x1c598 | GOT/.data slot 0x2b164 -> display handle -> +0x138 = GPIO6 base |
| 0x1c5e0/0x1c5e4 | GPIO6 base + 0x138 = OMAP GPIO DATAIN |
| **0x1c5e8/0x1c5ec** | **`tst r3,#0x10000` / `beq 0x1c618` - the Hitachi(1)/LG(0) decision** |
| 0x1c5f0-0x1c614 | Hitachi: slogf("LCD is Hitachi") + send_table(0x2e578, 9) |
| 0x1c618-0x1c63c | LG: slogf("LCD is LG") + send_table(0x2e58c, 11) |
| 0x1c4dc-0x1c57c | SPI table sender: open /dev/spi3, per-word byte swap, 2-byte writes, 100 ms after idx 0/2/last |
| 0x1c410-0x1c4b8 | /dev/spi3 open + `spi_setcfg(fd, 0xA0000000, {0xC508, 450000})` |
| 0x193b0-0x193e0 | panel/SPI init runs only when platform == 1 (Winchester); Colt -> 0x1c16c |
| 0x1938c | `bl 0x13e50` - TC358764 script called unconditionally |
| 0x193c4 | `bl 0x1c580` - the only caller of the identity function |
| 0x13e50-0x1400c | TC358764 init: 30 I2C writes, no branches, no panel input |
| **0x13fd0/0x13fd4/0x13fd8** | **`movw r0,#0x49c; mov r1,#1; bl 0x13ce0` - the only LV_CFG write, value 1** |
| 0x13f68-0x13fb4 | VPCTRL=0x03F00120, VP_HTIM1=0x00780001 (HBP 120, HPW 1), VP_HTIM2=0x00760400 (HFP 118, HACT 1024), VP_VTIM1=0x000A0001 (VBP 10, VPW 1), VP_VTIM2=0x000C0258 (VFP 12, VACT 600) |
| 0x14000 | `i2c_write_reg(0x0464, 1)` = VFUEN |
| 0x13ee8-0x13fd8 | 0x04A0=0x0440C02D, 0x04A4=0, 100 us, 0x04A0=0x0400C02D |
| 0x13ce0-0x13d34 | i2c_write_reg(reg,val): 6-byte msg, 16-bit reg LE, 32-bit val LE |
| 0x13e7c-0x13ea8 | `ppi_cnt` (=4) written to TC358764 0x0164/0x168/0x16C/0x170 |
| 0x162ec-0x16438 | DISPC parameter block builder; all record reads with -1 transforms (§4.2) |
| 0x16444-0x16610 | second (manager) block builder, from the ctx at 0x2eec8, not from the record |
| 0x17094-0x1799c | DSI "second pass"; reads record 0x00 (0x17278) and 0x04 (0x173a8) |
| 0x179dc-0x189ac | DSI video-mode/PHY/PLL descriptor builder; reads record 0x04..0xe8 (§2.3) |
| 0x17dcc-0x17dd8 | 0x80 -> +0x24a, 0x74 -> +0x249, 0x78 -> +0x248, 0x7c -> +0x24c |
| 0x18514-0x18574 | 0x48/0x44/0x4c/0x40/0x50 -> PLL desc +0x1f..+0x24; `bl 0x2062c` |
| 0x19020-0x193f0 | display sequencer: DSI cfg, 0x14604 probe, 0x17094, 0x13e50, 0x16bd8, 0x1c580 |
| 0x1914c/0x19150 | `bl 0x14604` panel probe; NULL result -> use g_DISPC_DSI_Configuration_DSI1 |
| 0x1a664 | `main` (dynsym, size 5616) - contains the mode-table selection |
| 0x1aa04-0x1abc0 | platform==2 -> Colt record; else refresh switch 30..120 -> record |
| 0x1aa18-0x1aa2c | memcpy(g_DISPC_DSI_Configuration_DSI1, Colt, 240) |
| 0x1abc4-0x1abec | "refresh out of range" printf, then 60 fps default |
| 0x1b640-0x1b66c | literal pool: slot numbers 0x148 (dest), 0x144 (Colt), 0x184/0x1c0/0x1b4/0x118/0x13c/0x120/0x1a8/0x138/0x160/0x19c (30..120 fps) |
| 0x14708-0x14730 | Colt panel-ID jump table (`addls pc,pc,r3,lsl #2`) on `[sp+0x8d] & 3` |
| 0x1474c/0x14760 | CMI: slogf + slot 0x1a0 |
| 0x14f0c/0x14f20 | TMD: slogf + slot 0x1c4 |
| 0x156e8 | LG (Colt): slogf + slot 0x108 |
| 0x1cb44-0x1cc04 | splash geometry: platform 1 -> 1024x600, platform 2 -> 1280x768 |
| 0x1cc3c/0x1cc44 | 1024 and 512 stored into the LCD context (0xf4/0xf8); 512 is not the panel height |
| 0x2e548 | `platform` = 1 (Winchester) in .data |
| 0x2e54c | refresh = 0x3c = 60 in .data |
| 0x2e55c | `ppi_cnt` = 4 in .data |
| **0x2e578** | **Hitachi SPI table, 9 u16 (0029 0025 0840 00AD 0430 385F 3CA4 3409 4041)** |
| **0x2e58c** | **LG SPI table, 11 u16 (0029 0025 0430 0840 385F 3CA4 3400 0843 2828 4041 00AD)** |
| 0x2e5c4 | DISPC parameter block (bss) filled by 0x162ec |
| 0x2ee08 | `g_DISPC_DSI_Configuration_DSI1`, the live 240-byte record |
| 0x2eec8 | LCD context struct passed to the sequencer (0x1cc6c `add r0,r4,#0x3ac8`) |
| 0x26044 / 0x26134 / 0x26224 / 0x26314 | Colt / TMD / CMI / LG records - all four byte-identical |
| 0x26404..0x26c74 | 1024x600 records for 30/40/50/60/70/80/90/100/110/120 fps |
| 0x266d4 | `DSI1_RGB888_VID_MODE_1024_600_60fps` (the default record) |
| 0x27d9c / 0x27dac | "LCD is Hitachi" / "LCD is LG" |
| 0x25eac-0x25f2c | "Invalid or no panel connected" / "CMI|TMD|LG panel detected" / "Unsupported or no panel detected" |
| 0x27d54 / 0x27d88 | "/dev/spi3" / "spi_setcfg failed." |
