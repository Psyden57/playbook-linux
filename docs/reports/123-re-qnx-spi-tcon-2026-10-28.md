# Report 123 — How the QNX reference OS configures the SPI controller for the panel TCON init table

**Date:** 2026-10-28
**Scope:** Static reverse engineering only. No device access, no `/dev/ttyACM0`, no bench, no PlayBook.
Read-only analysis of binaries in `/home/psyden/playbook-linux/local/proprietary/dumped4869ifs/proc/boot/`,
the identical copies under `/home/psyden/rea-work/playbook-dev/dumped4869ifs/proc/boot/`, the QNX 6.6
headers under `/home/psyden/SSDdrive/wsl-migration/home/psyden/qnx660-master/target/qnx6/usr/include`,
and the in-tree Linux driver `/home/psyden/playbook-linux/local/kernel-tree/drivers/spi/spi-omap2-mcspi.c`.

**Status:** COMPLETE — the SPI configuration is answered in §4.

---

## 1. Question

How does the QNX reference OS configure the OMAP4430 McSPI controller at the moment it pushes the
panel TCON initialisation table, so that a Linux `spidev` client can reproduce byte-for-byte
equivalent transfers on the same panel?

The single most important unknown was the **SPI mode (CPOL/CPHA)**. Secondary unknowns: bits per
word, bit/byte order, clock rate, chip select (which bus, which channel, what polarity), whether CS
is asserted per word or per whole table, and the transfer count/length/framing.

---

## 2. Method

1. **Recover the client-side ABI from the shipped QNX headers first.** `hw/spi-master.h` gives
   `spi_cfg_t` (mode + clock_rate) and the devctl used to set it; `devctl.h` gives the direction bits
   so the DCMD integer can be computed and then *looked for* in the binary.
2. **Find the SPI code in `winch_lcdctl` by string anchor.** `/dev/spi3`, `SPI open failed, fd = %d
   errno = %d` and `spi_setcfg failed.` sit at file offsets 0x27d54 / 0x27d60 / 0x27d88. In this
   binary a data reference is `ldr rX,[pc,#n]` (loads a pool word) followed by
   `add rX, r4, rX`, where **r4 = 0x2b000** (DT_PLTGOT) and the pool word is `target − 0x2b000`
   (two's complement when the target is below 0x2b000). Searching for `0x27d54 − 0x2b000 =
   0xffffcd54` etc. located every reference.
3. **Resolve the client library calls.** `winch_lcdctl` imports only `libdisputil`, `libm`, `libc`
   (libspi-master is *not* in `DT_NEEDED`), yet it contains a copy of the QNX SPI client library:
   `spi_open` @0x22be8, `spi_close` @0x22bdc, `spi_setcfg` @0x22bf8, `spi_write` @0x22c90. Only
   libc imports travel through the tiny stubs at 0x40xx–0x43xx (`add ip,pc,#0; add ip,ip,#0x26000;
   ldr pc,[ip,#imm]` → GOT slot 0x2b000+0x1000+imm), which is what let me identify `usleep`
   (stub 0x40f0 → GOT 0x2b028) and `MsgSendv` (stub 0x42a0 → GOT 0x2b0b8).
4. **Follow the value into the driver.** `spi-omap4430.so` is unstripped (`.dynsym` has
   `omap4430_setcfg`, `omap4430_cfg`, `omap4430_setup`, `omap4430_xfer`, `omap4430_init`), so the
   mode bits can be followed instruction-by-instruction into `MCSPI_CHxCONF`.
5. **Cross-check the register bit map against the Linux driver in this same workspace**
   (`spi-omap2-mcspi.c`), which programs the identical silicon and defines every `CHCONF` bit by name.
   This is what makes "the QNX driver's `POL` bit" and "Linux `SPI_CPOL`" the same thing.

Disassembly convention (verified here, as in reports 111/117/122): `winch_lcdctl` is ARM ELF32
`ET_DYN`, PT_LOAD#0 has `off=0, vaddr=0, filesz=0x28048`, so **text/rodata vaddr == file offset**.
PT_LOAD#1 has `off=0x28348, vaddr=0x29348` (**vaddr = file offset + 0x1000**). Tool:
`arm-linux-objdump -D -b binary -m arm --adjust-vma=<vaddr>`.
`spi-omap4430.so`: PT_LOAD#0 `off=0, vaddr=0, filesz=0x3ac8`; data PT_LOAD `off=0x3f24,
vaddr=0x4f24`; PIC anchor **0x5000**.

---

## 3. Evidence

### 3.1 Shipped QNX headers (the ABI the client must satisfy)

`.../qnx660-master/target/qnx6/usr/include/hw/spi-master.h`:

```c
typedef struct {
	uint32_t	mode;                                    /* offset 0 */
#define	SPI_MODE_CHAR_LEN_MASK	(0xFF)		/* bits 7..0  : char length   */
#define	SPI_MODE_CKPOL_HIGH	(1 <<  8)	/* bit  8                     */
#define	SPI_MODE_CKPHASE_HALF	(1 <<  9)	/* bit  9                     */
#define	SPI_MODE_BODER_MSB	(1 << 10)	/* bit 10                     */
#define	SPI_MODE_CSPOL_MASK	(1 << 11)
#define	SPI_MODE_CSPOL_HIGH	(1 << 11)	/* bit 11                     */
#define	SPI_MODE_CSSTAT_HIGH	(1 << 12)	/* bit 12                     */
#define	SPI_MODE_CSHOLD_HIGH	(1 << 13)	/* bit 13                     */
#define	SPI_MODE_RDY_MASK	(3 << 14)	/* bits 15..14                */
#define	SPI_MODE_IDLE_INSERT	(1 << 16)	/* bit 16                     */
#define	SPI_MODE_LOCKED		(1 << 31)
	uint32_t	clock_rate;                          /* offset 4 */
} spi_cfg_t;                                             /* sizeof == 8 */
...
#define	_DCMD_SPI			_DCMD_MISC               /* devctl.h: 0x05 */
#define DCMD_SPI_SET_CONFIG		__DIOT (_DCMD_SPI, 0x11, spi_cfg_t)
```

`devctl.h`: `_DCMD_MISC 0x05`, `_POSIX_DEVDIR_TO 0x80000000`,
`__DIOT(class,cmd,data) = (sizeof(data)<<16) + (class<<8) + cmd + _POSIX_DEVDIR_TO`

⇒ **`DCMD_SPI_SET_CONFIG = (8<<16) | (0x05<<8) | 0x11 | 0x80000000 = 0x80080511`**

### 3.2 `winch_lcdctl` — the config call (`vaddr 0x1c410`, "open + setcfg")

```
   1c418: e59f40a8  ldr   r4, [pc, #168]     @ 0x1c4c8   ; pool 0x0000ebdc
   1c41c: e08f4004  add   r4, pc, r4                     ; r4 = 0x1c41c+8+0xebdc = 0x2b000  (GOT anchor)
   1c420: e59f00a4  ldr   r0, [pc, #164]     @ 0x1c4cc   ; pool 0xffffcd54
   1c424: e0840000  add   r0, r4, r0                     ; r0 = 0x2b000+0xffffcd54 = 0x27d54 = "/dev/spi3"
   1c428: eb0019ee  bl    0x22be8                        ; spi_open(path)  -> open(path, O_RDWR)
   1c42c: e2505000  subs  r5, r0, #0                      ; r5 = fd
   1c430: aa00000c  bge   0x1c468
        ... error path: fprintf(stderr,"SPI open failed, fd = %d errno = %d", fd, errno)
   1c468: e30c3508  movw  r3, #50440        @ 0xc508     ; <<< mode low half
   1c46c: e58d3000  str   r3, [sp]                        ; cfg.mode        (offset 0)
   1c470: e30d3dd0  movw  r3, #56784        @ 0xddd0
   1c474: e3403006  movt  r3, #6            @ 0x0006     ; r3 = 0x0006ddd0 = 450000
   1c478: e58d3004  str   r3, [sp, #4]                    ; cfg.clock_rate  (offset 4)
   1c47c: e1a00005  mov   r0, r5                         ; fd
   1c480: e3a0120a  mov   r1, #-1610612736 @ 0xa0000000  ; SPI_DEV_DEFAULT|SPI_DEV_UNLOCK, id 0
   1c484: e1a0200d  mov   r2, sp                         ; &cfg
   1c488: eb0019da  bl    0x22bf8                        ; spi_setcfg(fd, 0xa0000000, &cfg)
   1c48c: e3500000  cmp   r0, #0
   1c490: 0a000009  beq   0x1c4bc                        ; success -> return fd
   1c49c: ...       fwrite(stderr, "spi_setcfg failed.", 1, 19)  ; pool 0x1c4d8 -> 0x27d88
```

⇒ **`cfg.mode = 0x0000C508`, `cfg.clock_rate = 450000`.** These are the *only* two values in the
struct, and they are placed at offsets 0 and 4 exactly as `spi_cfg_t` declares.

### 3.3 `winch_lcdctl` — the client library actually encodes `DCMD_SPI_SET_CONFIG`

`spi_setcfg` @ **0x22bf8** builds a QNX io message by hand:

```
   22c00: e58d100c  str   r1, [sp, #12]                   ; iov[1] = &device (4 bytes)
   22c04: e6ff1071  uxth  r1, r1                          ; device & 0xFFFF
   22c08: e1a0c002  mov   ip, r2                          ; ip = cfg
   22c0c: e30f3fff  movw  r3, #65535        @ 0xffff
   22c10: e1510003  cmp   r1, r3
   22c14: 13520000  cmpne r2, #0
   22c18: 03a00016  moveq r0, #22                         ; EINVAL if id==NONE or cfg==NULL
   22c20: e3003106  movw  r3, #262          @ 0x106
   22c24: e1cd32b8  strh  r3, [sp, #40]                   ; msg type      = 0x0106
   22c28: e3a0301c  mov   r3, #28
   22c2c: e1cd32ba  strh  r3, [sp, #42]                   ; msg 2nd word  = 28
   22c30: e3003511  movw  r3, #1297         @ 0x511
   22c34: e3483008  movt  r3, #32776        @ 0x8008
   22c38: e58d302c  str   r3, [sp, #44]                   ; *** dcmd = 0x80080511 ***
   22c3c: e3a0300c  mov   r3, #12
   22c40: e58d3030  str   r3, [sp, #48]                   ; nbytes = 12 = 4 (device) + 8 (cfg)
   22c44: e3a03000  mov   r3, #0
   22c48: e58d3034  str   r3, [sp, #52]
   22c4c: e28d2028  add   r2, sp, #40
   22c50: e58d2010  str   r2, [sp, #16]                   ; iov[0] = hdr (16 bytes)
   22c54: e3a02010  mov   r2, #16
   22c58: e58d2014  str   r2, [sp, #20]                   ;          len 16
   22c5c: e28d200c  add   r2, sp, #12
   22c60: e58d2018  str   r2, [sp, #24]                   ; iov[1] = &device
   22c64: e3a02004  mov   r2, #4
   22c68: e58d201c  str   r2, [sp, #28]                   ;          len 4
   22c6c: e58dc020  str   ip, [sp, #32]                   ; iov[2] = cfg
   22c70: e3a02008  mov   r2, #8
   22c74: e58d2024  str   r2, [sp, #36]                   ;          len 8   <<< sizeof(spi_cfg_t)
   22c7c: e28d1010  add   r1, sp, #16                     ; parts
   22c80: e3a02003  mov   r2, #3                          ; 3 parts
   22c84: ebff8585  bl    0x42a0                          ; MsgSendv (stub -> GOT 0x2b0b8)
```

**0x80080511 is bit-for-bit the `DCMD_SPI_SET_CONFIG` computed from the shipped header**, and the
payload length 8 confirms `spi_cfg_t` as compiled into the client. This is the linchpin that makes
the `mode`/`clock_rate` fields trustworthy.

### 3.4 `winch_lcdctl` — the TCON table sender (`vaddr 0x1c4dc`)

```
   1c4ec: ebffffc7  bl    0x1c410                        ; fd = spi_open("/dev/spi3") + spi_setcfg()
   1c4f0: e1a08000  mov   r8, r0                          ; fd
   1c4fc: e3a04000  mov   r4, #0                          ; i = 0
   1c500: e28d900e  add   r9, sp, #14                     ; &w (16-bit staging buffer)
   1c504: e1a0b004  mov   fp, r4                          ; device = 0
   1c508: e30836a0  movw  r3, #34464        @ 0x86a0
   1c50c: e3403001  movt  r3, #1                           ; 0x000186a0 = 100000
   1c510: e58d3004  str   r3, [sp, #4]                    ; delay argument = 100000
   ; ---- loop body ----
   1c514: e1a03084  lsl   r3, r4, #1
   1c518: e19a30b3  ldrh  r3, [sl, r3]                     ; W = tab[i]   (16-bit, little-endian load)
   1c51c: e1a02423  lsr   r2, r3, #8                      ; r2 = W >> 8
   1c520: e1823403  orr   r3, r2, r3, lsl #8               ; W' = bswap16(W)
   1c524: e1cd30be  strh  r3, [sp, #14]                    ; mem = { W>>8 , W&0xFF }  -> MSB byte first
   1c528: e1a00008  mov   r0, r8                          ; fd
   1c52c: e1a0100b  mov   r1, fp                          ; device = 0
   1c530: e1a02009  mov   r2, r9                          ; &w
   1c534: e3a03002  mov   r3, #2                          ; len = 2 bytes
   1c538: eb0019d4  bl    0x22c90                         ; spi_write(fd, 0, &w, 2)
   1c53c: e2845001  add   r5, r4, #1                      ; i+1
   1c540: e1a07005  mov   r7, r5
   1c544: e3540000  cmp   r4, #0
   1c548: 11550006  cmpne r5, r6                          ; (i==0) || (i+1==count)
   1c54c: 0a000001  beq   0x1c558
   1c550: e3540002  cmp   r4, #2                          ; || (i==2)
   1c554: 1a000001  bne   0x1c560
   1c558: e59d0004  ldr   r0, [sp, #4]                    ; 100000
   1c55c: ebff9ee3  bl    0x40f0                          ; usleep(100000)  == 100 ms
   1c560: e1560007  cmp   r6, r7
   1c564: c1a04005  movgt r4, r5                          ; i++
   1c568: caffffe9  bgt   0x1c514
   ; ---- done ----
   1c56c: e1a00008  mov   r0, r8
   1c570: ebffffa3  bl    0x1c404                        ; spi_close(fd)
```

`0x40f0` is the libc import stub for **`usleep`** (stub → GOT slot 0x2b028, which JMPREL names
`usleep`; verified by decoding `add ip,pc,#0; add ip,ip,#0x26000; ldr pc,[ip,#0xf30]`).

`spi_write` @0x22c90 wraps the buffer in a `spi_msg_t`-style message for the resmgr unless
`(device & 0xFFFF) == 0xFFFF`, in which case it degrades to plain `write(fd,buf,len)`.
It is called with `device = 0`, so the SPI message path is used with **device/channel 0**.

### 3.5 Which tables, how many words, and from where

```
   1c604: e59f0050  ldr   r0, [pc, #80]      @ 0x1c65c   ; pool 0x3578 -> 0x2b000+0x3578 = 0x2e578
   1c608: e0840000  add   r0, r4, r0                     ; Hitachi table
   1c60c: e3a01009  mov   r1, #9
   1c610: ebffffb1  bl    0x1c4dc                        ; send(tab, 9)
   1c62c: e59f3028  ldr   r3, [pc, #40]      @ 0x1c65c   ; 0x3578
   1c630: e0844003  add   r4, r4, r3                     ; 0x2e578
   1c634: e2840014  add   r0, r4, #20                    ; 0x2e58c  (LG table)
   1c638: e3a0100b  mov   r1, #11
   1c63c: ebffffa6  bl    0x1c4dc                        ; send(tab, 11)
```

Reached from the splash path: `0x193c4: eb000c6d bl 0x1c580` (the function containing 0x1c604/0x1c63c
starts at 0x1c580) — this is the "0x1935c..0x193c0" call site identified in report 122 §7.1.

Table bytes (file offsets; `vaddr = file + 0x1000`):

| table | vaddr | words | file bytes | u16 values (LE read) | bytes on the wire (QNX order) |
|---|---|---|---|---|---|
| Hitachi | 0x2e578 | 9 | `29 00 25 00 40 08 ad 00 30 04 5f 38 a4 3c 09 34 41 40` | 0029 0025 0840 00ad 0430 385f 3ca4 3409 4041 | `00 29  00 25  08 40  00 ad  04 30  38 5f  3c a4  34 09  40 41` |
| LG | 0x2e58c | 11 | `29 00 25 00 30 04 40 08 5f 38 a4 3c 00 34 43 08 28 28 41 40 ad 00` | 0029 0025 0430 0840 385f 3ca4 3400 0843 2828 4041 00ad | `00 29  00 25  04 30  08 40  38 5f  3c a4  34 00  08 43  28 28  40 41  00 ad` |

The tables are stored as little-endian `u16`; the client byte-swaps each word before handing it to
`spi_write`, so **each `u16` goes out most-significant byte first**.

### 3.6 `spi-omap4430.so` — `omap4430_cfg()` turns `mode` into `MCSPI_CHxCONF` (vaddr 0x17e8)

```
   17fc: e5913000  ldr   r3, [r1]                        ; r3 = cfg->mode      (spi_cfg_t+0)
   1800: e20320ff  and   r2, r3, #255                    ; charlen = mode & 0xFF
   1804: e2428001  sub   r8, r2, #1                      ; r8 = charlen-1
   1808: e2422005  sub   r2, r2, #5
   180c: e352001c  cmp   r2, #28
   1810: 8a000023  bhi   0x18a4                          ; reject charlen outside 5..33
   1814: e1a08388  lsl   r8, r8, #7                      ; WL   = (charlen-1) << 7   (bits 11..7)
   1818: e3130b02  tst   r3, #2048   @ 0x800             ; SPI_MODE_CSPOL_HIGH (bit 11)
   181c: 03888040  orreq r8, r8, #64 @ 0x40              ; EPOL (bit 6) = !CSPOL_HIGH
   1820: e3130c01  tst   r3, #256    @ 0x100             ; SPI_MODE_CKPOL_HIGH (bit 8)
   1824: 03888002  orreq r8, r8, #2                      ; POL  (bit 1) = !CKPOL_HIGH   <-- inverted
   1828: e3130c02  tst   r3, #512    @ 0x200             ; SPI_MODE_CKPHASE_HALF (bit 9)
   182c: 13888001  orrne r8, r8, #1                      ; PHA  (bit 0) = CKPHASE_HALF
   1830: e3c88806  bic   r8, r8, #393216     @ 0x60000    ; clear DPE1(17), IS(18)
   1834: e3888801  orr   r8, r8, #65536      @ 0x10000    ; set   DPE0(16)   [4-wire host config]
   1838: e5903100  ldr   r3, [r0, #256]                   ; hdl->0x100 (DMA-request flag, default 1)
   1840: 13c88406  bicne r8, r8, #100663296  @ 0x6000000  ; clear bits 26,25
   1844: 13888402  orrne r8, r8, #33554432   @ 0x2000000  ; set   bit 25
   1848: e59050ac  ldr   r5, [r0, #172]      @ 0xac       ; hdl->0xac = SPI input clock (48 000 000)
   184c: e5916004  ldr   r6, [r1, #4]                     ; r6 = cfg->clock_rate (spi_cfg_t+4)
   ; loop: for (i=0..12) if (48000000 / speed_divisor[i]) <= clock_rate -> CLKD = i
   189c: e1880104  orr   r0, r8, r4, lsl #2                ; CLKD = i  (bits 5..2)
```

`speed_divisor[]` at file offset 0x4114 (vaddr 0x5114) is
`{1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096}` — i.e. `CLKD = i = log2(divisor)`.
`hdl->0xac` defaults to **48 000 000** (`0x26f8`: `movw r3,#0x6c00; movt r3,#0x2dc; str r3,[r4,#0xac]`
= 0x02DC6C00) and `.script` does not override it.

For `mode = 0xC508`, `clock_rate = 450000`:

| step | value |
|---|---|
| charlen = 0xC508 & 0xFF | **8** → WL = 7 << 7 = 0x380 |
| CSPOL_HIGH (bit11) = 0 | **EPOL = 1** → 0x40 |
| CKPOL_HIGH (bit8) = 1 | **POL = 0** |
| CKPHASE_HALF (bit9) = 0 | **PHA = 0** |
| DPE0/DPE1/IS | 0x10000 / 0 / 0 |
| hdl->0x100 = 1 | bit 25 = 1 → 0x2000000 |
| 48 MHz / 128 = 375 000 ≤ 450 000 (first i that fits) | **CLKD = 7** → 0x1C |
| **`MCSPI_CH0CONF` written by the driver** | **0x020103DC** |

### 3.7 Where the value is committed, and how CS is driven

`omap4430_setcfg` @0x259c stores the 8-byte `spi_cfg_t` into the per-device table
(`devcfg[dev] + 20`, stride 28 = `sizeof(spi_devinfo_t)`), calls `omap4430_cfg`, and **caches** the
computed CHCONF word at 0x5224 + dev*4 (`0x25f8: str r0,[r4,r5,lsl #2]`). Nothing hits the hardware
until a transfer.

`omap4430_setup` @0x1f28 then writes it:

```
   1f4c: e7963105  ldr   r3, [r6, r5, lsl #2]            ; r3 = cached CHCONF for channel ch
   1f50: e287702c  add   r7, r7, #44         @ 0x2c      ; io + 0x2C
   1f54: e0852105  add   r2, r5, r5, lsl #2
   1f58: e0877102  add   r7, r7, r2, lsl #2               ; + ch * 0x14  -> MCSPI_CHxCONF
   1f5c: e5873000  str   r3, [r7]                          ; *** MCSPI_CHxCONF = 0x020103DC ***
```

`omap4430_init` @0x26f8 (once, at driver start):
```
   29b0: e5b32028  ldr   r2, [r3, #40]!      @ 0x28       ; MCSPI_MODULCTRL
   29b4: e3c22005  bic   r2, r2, #5                       ; clear SINGLE(0) and MS(2)
   29b8: e3822001  orr   r2, r2, #1                       ; SINGLE = 1 ; MS = 0 (master)
   29bc: e5832000  str   r2, [r3]
```

`omap4430_xfer` @0x2228, per `spi_write` call:
```
   2308: mov r1,#0x100000 ; mov r2,r1 ; bl set_port   ; CHxCONF.FORCE (bit 20) = 1
   2318: mov r1,#0x18000000 ; mov r2,r1 ; bl set_port ; CHxCONF.FFET|FFER (bits 27,28) = 1
   2350: r0 = CHxCTRL ; r1 = 1 ; r2 = 1 ; bl set_port ; MCSPI_CHxCTRL.EN = 1
   2324: movw r2,#0x4240 ; movt r2,#0x0f               ; XFERLEVEL = (len<<16)|0xF00|0x0F
   2338: str r3,[r7+0x7c]                              ;   (FIFO thresholds)
   23a4: str r5,[r8,#28]!                              ; IRQENABLE = (4<<(4*ch))|0x20000
        ... push len bytes, poll CHxSTAT.RXS, pop RX bytes ...
   2488: set_port(CHxCTRL, 1, 0)                       ; MCSPI_CHxCTRL.EN = 0   (CS released)
   249c: str r2,[r8]                                   ; IRQENABLE = 0
   24a4: set_port(CHxCONF, 0x100000, 0)                ; CHxCONF.FORCE = 0
```
`set_port(reg, mask, val)` is 0x1f10: `*reg = (*reg & ~mask) | (val & mask)`.

### 3.8 Bus instance, chip select, and the Linux-side cross-check

`.script` (binary-searchable string): `spi-master -P 0600 -u 3 -U 99:99 -d omap4430
base=0x480ba100,irq=80,sdma=0 /dev/spi3` — i.e. **MCSPI4** (base 0x480BA000, +0x100 in the option
string; see §5), node `/dev/spi3`, `waitfor /dev/spi3` before `winch_lcdctl`. Consistent with reports
111/118/121/122 and with `initramfs/console.c:995` and `kernel/source/omap4-winchester.dts`
(`aliases { spi3 = &mcspi4; }`, `tcon@0` spidev, `spi-max-frequency = <450000>`).

Linux `drivers/spi/spi-omap2-mcspi.c` (same workspace) names the very bits the QNX driver sets:

```c
#define OMAP2_MCSPI_CHCONF_PHA   BIT(0)
#define OMAP2_MCSPI_CHCONF_POL   BIT(1)
#define OMAP2_MCSPI_CHCONF_CLKD_MASK (0x0f << 2)
#define OMAP2_MCSPI_CHCONF_EPOL  BIT(6)
#define OMAP2_MCSPI_CHCONF_WL_MASK   (0x1f << 7)
#define OMAP2_MCSPI_CHCONF_DPE0  BIT(16)   /* DPE1 BIT(17), IS BIT(18) */
#define OMAP2_MCSPI_CHCONF_FORCE BIT(20)
#define OMAP2_MCSPI_CHCONF_FFET  BIT(27)   /* FFER BIT(28) */
...
	if (!(spi->mode & SPI_CS_HIGH)) l |=  OMAP2_MCSPI_CHCONF_EPOL;  /* active-low; normal */
	if (spi->mode & SPI_CPOL)       l |=  OMAP2_MCSPI_CHCONF_POL;
	if (spi->mode & SPI_CPHA)       l |=  OMAP2_MCSPI_CHCONF_PHA;
```
Six independent bit positions (WL 11:7, EPOL 6, CLKD 5:2, DPE0/DPE1/IS 16/17/18, FORCE 20,
FFET/FFER 27/28) agree exactly between the QNX driver and the Linux driver, so the *bit numbering*
of POL (bit 1) and PHA (bit 0) is not in doubt — only the QNX driver's polarity convention is (§5).

---

## 4. The SPI configuration

Everything below is what the OMAP4430 McSPI4 controller actually does for the TCON table.

| Parameter | Value | Evidence |
|---|---|---|
| **SPI mode** | **Mode 0 — CPOL = 0, CPHA = 0** | `MCSPI_CH0CONF.POL` (bit 1) = 0 and `.PHA` (bit 0) = 0; from `spi-omap4430.so` 0x1820–0x182c applied to `mode = 0xC508` (0x1c468) |
| **Bits per word** | **8** | `mode & 0xFF = 8`; `omap4430_cfg` 0x1800–0x1814 → `WL = (8−1) << 7 = 0x380` |
| **Bit order** | **MSB first** | McSPI has no LSB-first option and `omap4430_cfg` never tests `mode` bit 10 (`SPI_MODE_BODER_MSB`); it is a no-op (bits 14/15 likewise) |
| **Byte order within each `u16`** | **most-significant byte first** (big-endian per word) | `0x1c51c-0x1c524`: `lsr r2,r3,#8` / `orr r3,r2,r3,lsl #8` / `strh` → buffer = `{W>>8, W&0xFF}` |
| **Clock, requested** | **450 000 Hz** | `cfg.clock_rate = 0x0006DDD0`, 0x1c470–0x1c478 |
| **Clock, actual** | **375 000 Hz** (48 MHz / 128, `CLKD = 7`) | `hdl->0xac = 48e6` (init 0x2744); `speed_divisor[7] = 128` (vaddr 0x5130); loop 0x1864–0x1898; `orr r0,r8,r4,lsl #2` at 0x189c |
| **Controller / bus** | **MCSPI4**, QNX node `/dev/spi3` (`-u 3`, `base=0x480ba100`) | `.script`; `0x27d54` string; DTS `spi3 = &mcspi4` |
| **Chip select** | **hardware CS0 / SPIEN0** — `device = 0` on both `spi_setcfg` (0xa0000000 → id 0) and every `spi_write` (`fp = 0`) | 0x1c480, 0x22c04 (`uxth`), 0x1c504/0x1c52c |
| **CS polarity** | **active low** (`EPOL = 1`), because `mode` bit 11 `CSPOL_HIGH` is clear | 0x1818–0x181c; matches Linux `!(mode & SPI_CS_HIGH)` |
| **CS asserted per word, or per table?** | **Per word.** Each `u16` is a separate `spi_write` → separate resmgr message → separate `omap4430_xfer()` → CS asserted (`CHxCONF.FORCE = 1`) before `CHxCTRL.EN = 1` and released (`EN = 0`, then `FORCE = 0`) afterwards. CS is *not* held across the table: `CSHOLD_HIGH` (mode bit 13) is clear and the driver never reads bits 12/13 at all. | 0x1c538 loop; xfer 0x2308/0x235c/0x2488/0x24a4 |
| **Transfer count / length** | **9** messages of **2 bytes** each (Hitachi) or **11** messages of 2 bytes (LG); one `open` + one `setcfg` per table, `close` at the end | 0x1c60c/0x1c610 (9), 0x1c638/0x1c63c (11), 0x1c4ec, 0x1c570 |
| **Word/frame structure** | 8 bits × 2 per CS pulse, full-duplex (`TRM = 00`), FIFOs enabled (`FFER|FFET`), status polled in PIO (sDMA disabled by `sdma=0`) | xfer 0x2318, 0x2324–0x2338, 0x23e8–0x2484 |
| **Inter-word delays** | **100 ms** (`usleep(100000)`) *after* word index 0, index 2, and the last index | 0x1c508 (=0x186a0), 0x1c544–0x1c55c, stub 0x40f0 = `usleep` |
| **Final register value** | `MCSPI_CH0CONF = 0x020103DC` at setup, `0x1A2103DC` while a word is in flight (`| FORCE | FFET | FFER`) | 0x1f5c + xfer bit sets |

`MCSPI_CH0CONF = 0x020103DC` decomposed: `CLKD = 7` (bits 5:2 = 0x1C), `EPOL = 1` (bit 6 = 0x40),
`WL = 7` (bits 11:7 = 0x380), `DPE0 = 1` (bit 16 = 0x10000), bit 25 = 0x2000000, `POL = PHA = 0`.

---

## 5. What remains uncertain

1. **The `CKPOL_HIGH` → `POL` inversion is a property of the QNX BSP, and I cannot rule out that it
   is a bug.** The *register value* is certain (`POL = 0`, `PHA = 0`), so the pins see mode 0. But the
   client's own request bit 8 (`SPI_MODE_CKPOL_HIGH`) is inverted by `orreq` at 0x1824, whereas
   `CKPHASE_HALF` (bit 9) and `CSPOL_HIGH` (bit 11) are not inverted. Two readings are possible and I
   cannot separate them statically:
   (a) QNX's `CKPOL_HIGH` means "clock is high while asserted", the driver is correct, and the panel
   genuinely runs CPOL = 0;
   (b) the QNX driver inverts a bit it should not, and whoever wrote `winch_lcdctl` was compensating.
   Either way the *hardware* is at CPOL = 0, which is what a Linux client must reproduce. The missing
   evidence to settle the intent is the QNX 6.6 `spi-master` *documentation* for `SPI_MODE_CKPOL_HIGH`
   (not present in the SDP tree — only `hw/spi-master.h`, which has no comment) or a driver for a
   second QNX SoC to compare conventions against (none in the image).
2. **`CHCONF` bit 25.** Set whenever `hdl->0x100 != 0`, and the driver's default for `hdl->0x100` is 1
   (0x277c), and `.script` does not override it. The Linux driver has no name for bit 25 and leaves it
   0. It is almost certainly a DMA-request routing bit (it is set in the same breath as clearing bits
   26/25), but I have no TRM here to prove that, and I did not chase it because it cannot change the
   wire protocol. **Missing evidence:** an OMAP4430 TRM `MCSPI_CH(i)CONF` bit table for bits 23–26.
3. **`DPE0`/`DPE1`/`IS` disagree with Linux.** QNX writes `DPE0 = 1, DPE1 = 0, IS = 0`; the Linux
   driver's default `pin_dir` path in this tree writes `IS = 1, DPE1 = 1, DPE0 = 0`
   (`spi-omap2-mcspi.c:949-957`, `pin_dir = MCSPI_PINDIR_D0_OUT_D1_IN`). These are pin-direction /
   DMA-request bits, not protocol bits, but if a bench test ever shows MOSI activity on the wrong pad
   this is the first place to look. **Missing evidence:** the OMAP4430 TRM definition of `IS`, or a QNX
   readback of the register while running.
4. **`base=0x480ba100`** in `.script` is exactly 0x100 above the documented MCSPI4 page base
   (0x480BA000), yet the driver maps the value verbatim and uses `+0x28`/`+0x2C`/`+0x7C` offsets. I did
   not resolve whether OMAP4430's MCSPI4 register block starts at +0x100 (making 0x480ba100 correct) or
   whether the QNX driver carries a 0x100 skew. Prior reports (111, 122 §"MCSPI4 base") already
   concluded "this is MCSPI4", and the association is consistent with the pads in report 121, so I did
   not re-open it. It does not affect any of the values in §4 because they are all register-relative.
5. The **`FORCE` bit polarity** (does `FORCE = 1` hold SPIEN active or inactive?) is ambiguous between
   the QNX code and the Linux `omap2_mcspi_set_cs()` comment. It does not affect the conclusion: QNX
   sets and clears `FORCE` inside a single `spi_write`, so the CS envelope is exactly one 2-byte word
   either way. **Missing evidence:** OMAP4430 TRM `MCSPI_CH(i)CONF[20:19]` text.

---

## 6. What this means for the Linux console

1. **The mode is not the difference.** QNX runs the TCON link at **mode 0 (CPOL = 0, CPHA = 0), 8 bits,
   MSB-first, CS0 active-low** — and `kernel/source/omap4-winchester.dts` already gives `tcon@0` no
   `spi-cpol`/`spi-cpha`, so `spidev` is already at mode 0. Changing the mode will change the pin
   behaviour (mode 0 → 1/2/3 flips CPHA and/or CPOL), but nothing in the QNX reference supports doing
   so. If the console still fails with mode 0, the fault is elsewhere.
2. **What *does* differ, in priority order:**
   - **Byte order.** QNX sends each `u16` **high byte first**. The console currently sends low byte
     first. (Already tried both ways per the brief — but note the table in §3.5 so the "both ways" test
     is against the right reference: the wire bytes should be
     `00 29 00 25 08 40 00 AD 04 30 38 5F 3C A4 34 09 40 41` for Hitachi.)
   - **Clock granularity.** QNX gets 375 000 Hz from the plain power-of-two divider (`CLKD = 7`).
     Linux's `omap2_mcspi_setup_transfer()` takes the `CLKG` branch for 450 kHz and programs
     `divisor = ceil(48e6/450e3) = 107` → `CLKD = 10, EXTCLK = 6` → **448 598 Hz**. That is ~1.2× QNX's
     clock. To land near 375 kHz without leaving `CLKG` mode, set `spi-max-frequency = <375000>`
     (gives 48e6/129 = 372 093 Hz) — Linux cannot express the exact ÷128 step unless the requested
     rate drops below 11 718 Hz.
   - **`CHCONF` bits 25 / `DPE0`-`DPE1`-`IS`** (§5 items 2–3). If a scope shows clocks but wrong data,
     these are the remaining register-level candidates.
3. **CS/framing already matches.** One message per `u16`, CS toggling per word, no CS hold across the
   table, 9 or 11 messages, 100 ms after word 0 / word 2 / last — all identical to the QNX sequence
   (`usleep(100000)` at 0x1c55c, condition at 0x1c544–0x1c554).
4. **The controller is configured once per table**, not per word: `spi_open` + `spi_setcfg` at 0x1c4ec,
   then N × `spi_write`, then `spi_close` at 0x1c570. A Linux client that re-issues `SPI_IOC_WR_MODE`
   between words is doing something QNX never does.
5. **If a mode experiment is still wanted**, the physically meaningful candidates are mode 1 and mode 3
   (CPHA = 1), because CPOL only moves the idle level (both QNX and Linux drive the clock with the
   simple divider and sample on the leading edge). But there is no static evidence for CPHA = 1 in any
   QNX binary: `PHA` is 0 at the only `spi_setcfg` call site in `winch_lcdctl`.
