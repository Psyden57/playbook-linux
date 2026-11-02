# OMAP4430 boot ROM (0x40028000) - consolidated working notes

Status: in progress. Sources: LLVM-23 disassembly + Python over the raw dump.
REA native providers unavailable; all items below are LLVM-derived observations
unless marked "prior claim".

## Identity

- File: `omap4430-bootrom-0x40028000.bin`, 49152 bytes, base 0x40028000.
- SHA-256 `fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`.
- Mixed ARM (A32) + Thumb-2, little-endian, ARMv7-A (Cortex-A9), no file header.

## Entry, vectors, bootstrap

- 0x40028000 ARM: `b 0x40028910` (reset entry).
- 0x40028004-0x4002801C: 7x `ldr pc,[pc,#0x18]`; each loads from
  0x40028024-0x4002803C, which hold IRAM pointers 0x4030D004..0x4030D01C.
- 0x40028020 = 0x1CF48309 (unknown signature / checksum word), followed by the
  IRAM pointer list above.
- Second vector table at 0x4002FFC0: 8x `ldr pc,[pc,#0x18]` + target words at
  0x4002FFE0..0x4002FFFC:
  idx0 0x00028090 (hang `b .`), idx1 0x00028080 (hang), idx2 0x00028084 (hang),
  idx3 0x000289E4 (abort handler: `mrc c6,c0,2` + `mrc c5,c0,1` -> pc=0x40028A10),
  idx4 0x000289F0 (abort handler: `mrc c6,c0,0` + `mrc c5,c0,0` -> pc=0x40028A14),
  idx5 0x00028090 (hang), idx6 0x00029420 (IRQ handler), idx7 0x00028098 (hang).
- IRAM copy/bootstrap table at 0x4002FF80 (4 records of {r0, r1, r2, func}):
  1. {0x0002FFC0, 0x4030D000, 0x40,  0x00028870}  -> memcpy vector table
  2. {0x00030000, 0x4030D100, 0x08,  0x00028870}  -> memcpy 8 bytes
  3. {0x00030008, 0x4030D108, 0x98,  0x00028870}  -> memcpy config records
  4. {0x000300A0, 0x4030D1A0, 0xABC, 0x00028898}  -> memset (zero) 0xABC bytes
  (0x00028870 = ARM memcpy, 0x00028898 = ARM memset; near-end of table 0x4002FFBC.)

## Reset / CPU bring-up (ARM)

- 0x400280C0 `do_warm_reset`: checks control reg 0x4A0022C4 bits[11:8]==3, then
  reads 0x4A307B04 (PRM_RSTST), writes it back (clear), sets 0x4A307B00
  (PRM_RSTCTRL) bit0 -> warm reset. (PRM labels are inferred from low bits/usage.)
- 0x40028100 CPU1 wake path: if MIDR[1:0]!=0: poll 0x48281400 bit 0x200,
  `mov r0,#1; mov r1,#0x200; movw r12,#0x107; smc #0` (clear WKG_CONTROL),
  then `movw r12,#0x103; smc #0` (read AUX_CORE_BOOT_0/1); on r0!=0 dispatch to
  0x400281C0 with r10=r1 (CPU1 boot address). CPU0 branch (0x40028154) writes
  PRCM words at 0x4A108020/0x4A108024, calls 0x4002F384 (ICIALLU) and
  0x4002F310 (invalidate D-cache by set/way), TLBIALL, then enables I-cache.
- 0x400289A0 CPU1 secondary variant: poll 0x48281400+0x400 bit 0x200, SMC 0x107,
  then read 0x48281400+0x800 (nonzero check) and 0x48281400+0x804 -> `bx r1`
  (jump to trampoline address supplied by secure/PRCM state).
- 0x40028910 CPU0 path: `mrc c0,c0,5` MIDR; device/type read via 0x4A0022C4;
  `msr CPSR_c,#0xD3` (SVC, IRQ/FIQ off); VBAR (`mcr c12,c0,0`) = 0x00028000
  (boot alias); SP = 0x4030D000; r0 = [0x4A307B04]; `blx 0x4002A4D8` (save reset
  reason); `mov r0,#0; blx 0x4002A51C` (set progress bit 0); then 8x `bl .+4`
  filler; `mrc c1` + `orr #0x1800` + `mcr c1` (enable I-cache); `b 0x4002882C`.

## Startup trampoline (ARM, 0x4002882C-0x400288D0)

- 0x4002882C-0x40028864 walk the 0x4002FF80 table (r10=base via words at
  0x40028868 = 0x00007718/0x00007758 added to pc), calling func with r0/r1/r2.
- 0x40028870 = memcpy(src=r0, dst=r1, len=r2); 0x40028898 = memset(dst=r1,len=r2).
- 0x400288D0: `ldr sp,=0x4030D000`; `blx 0x400288C4` (Thumb veneer, calls ARM
  prologue at 0x4002F2F0-ish); `ldr r12,=0x00000135; add r12,r12,pc` ->
  0x40028A19 (odd = Thumb) then `bx r12`: transition to Thumb main at 0x40028A18.
- 0x400288C4/0x400288CC are Thumb veneers to ARM helpers; 0x400288F0 is an
  ARM<->Thumb switch helper (`add r12,pc,#1; bx r12`).

## Thumb main / boot flow (0x40028A18+)

- 0x40028A18 entry: reads PRM_RSTST via 0x4002A4C0 into stack; progress bit 1;
  calls 0x40029AE6(6,1), 0x4002A41C, 0x4002A430, 0x4002A478(0x40028A74),
  0x4002A454; if reset status==1 also bit 2 + clears [0x40028A78=0x4002A5xx?].
- 0x40028A7C: config-string parser. Reads bytes from a table (sp+0x18), values
  1..8 -> handler 0x4002AD5C; ASCII 'A'..'G' (0x41-0x47) -> handlers including
  0x4002AFFE with result store; writes parsed fields into structure at
  0x40028BF8+0x64.
- 0x4002A4C0: `ldr.w r1,[0x4A307000+0xB04]` (=PRM_RSTST) -> *r0; returns 0/6.
- 0x4002A4D8: initialises a descriptor/global (literals 0x4002A558/0x4002A55C)
  with reset reason; writes 3 words and status.
- 0x4002A51C: sets bit r0 (0..0x5F) in a 3-word bitmap (progress/trace vector);
  returns 0 or 1. Called with 1,2,3,4,5,6,8,9... as boot milestones.
- Boot image / config names live at 0x4002FF2C (see below).

## Strings / boot image names

- 0x4002FF2C: "R&D", 0x4002FF31 "2ND", 0x4002FF35 "CHMMCSD",
  0x4002FF3D "CH", 0x4002FF40 "CHFLASH", 0x4002FF48 "CHRAM",
  0x4002FF4E "PRIMAPP", 0x4002FF56 "X-LOADER", 0x4002FF60 "CHSETTINGS",
  0x4002FF6B "KEYS", 0x4002FF71? "ISSW".
  Preceded by 16-bit table at 0x4002FEF0-0x4002FF20 (values 0x01C2..0x08DE,
  likely per-name offsets/lengths) - unverified.
- 0x4002BC00: `"MLO        "` (padded 12-byte field inside a data structure).

## Peripheral / controller constants (LE words found in the image)

- MMC controller base table at 0x4002FE20:
  0x4809C000, 0x480B4000, 0x480B4000, 0x480AD000, 0x480D1000, 0x480D5000,
  0x480B6000. Referenced as 0x0002FE20 from 0x4002E048 and 0x4002E0A4.
- 0x4002FE3C: table of 16-bit pairs (0x03CB/0x0062, 0x000A/0x0001, 0x0329,
  0x02EB, 0x0287, 0x01E6, 0x0176, 0x0144, 0x0116, 0x00F3, 0x00D8, 0x00BB,
  0x00B1, 0x00A2, 0x008B, 0x007A ...) referenced as 0x0002FE3C from 0x4002EBB4
  - likely per-device clock/divider settings, unverified.
- 0x4002FEB4 / 0x4002FEE0: further 16-bit tables (not yet interpreted).
- UART/USB-class constants seen: 0x48020000, 0x4806A000, 0x4806C000,
  0x4806E000, 0x48070000, 0x48072000, 0x48078000, 0x48078880, 0x48079001,
  0x48086000 (labels unverified).
- PRM/PRCM/control: 0x4A307B00/04 (RSTCTRL/RSTST), 0x4A108000/020/024,
  0x4A0022C4, 0x48281400 (+0x400/+0x800/+0x804), 0x4A326A00/0x4A327000,
  0x4A0022C4.

## SMC / monitor call sites

- `smc #0` (service id in r12):
  0x40028134 r12=0x107 (clear WKG_CONTROL, r0=1 r1=0x200)
  0x4002813C r12=0x103 (read AUX_CORE_BOOT_0/1)
  0x400289C8 r12=0x107 (CPU1 secondary path)
  0x4002CEB8 r12=0x0F0 (ARM stub inside Thumb region, context TBD)
- `smc #1` (ARM cache library 0x4002F4AC/0x4002F4F4), r12 then barrier:
  0x4002F4C8 r12=0x00, 0x4002F4E8 r12=0xFE,
  0x4002F510 r12=0x00, 0x4002F530 r12=0xF1.
  These are inside ARM wrappers that flush caches/barriers around the SMC.
- The 4-KB non-readable hole 0x40026F28-0x40027FFF (separate lower dump) remains
  the best candidate for the SMC dispatch/handler code; not present in this file.

## ARM cache/TLB maintenance library (0x4002F2B0-0x4002F550)

- 0x4002F2B0 read-unaligned-u32, 0x4002F2D0 write-u32.
- 0x4002F310/0x4002F3F0 loops use CCSIDR (`mrc c0,c0,1`) + `mcr c7,c6,2`
  (DCISW) / `mcr c7,c10,2` (DCCSW).
- 0x4002F358 ICIMVAU (c7,c5,1); 0x4002F384 ICIALLU (c7,c5,0);
  0x4002F38C `mcr c7,c1,0`; 0x4002F394 DCIMVAC (c7,c6,1);
  0x4002F3C0 DCIMVAC+ICIMVAU; 0x4002F438 DCCMVAU (c7,c11,1);
  0x4002F464 DSB; 0x4002F46C `mcr c8,c3,1` (TLBIASID-class);
  0x4002F4A0 BPIALL (c7,c5,6).
- 0x4002F4AC/0x4002F4F4 wrappers: BPIALL + barriers + `smc #1`.

## Open questions / next passes

1. Decode 0x4002FE3C / 0x4002FEB4 / 0x4002FEE0 tables and the 0x4002E048 /
   0x4002E0A4 / 0x4002EBB4 users (boot media selection, MMC init).
2. Full 0x40028A7C config parser semantics; what 0x4002AD5C / 0x4002AFFE do;
   which structure at 0x40028BF8+0x64.
3. Meaning of 0x40030000 8-byte header and the 0x40030008 0x98-byte records
   (4 records, 0x20 bytes each, mirrored values) and the 0x400300A0 0xABC blob.
4. 0x4002CEB8 r12=0xF0 context; ARM stub callers.
5. 0x4002B0xx ARM stub identity.
6. 0x4002BC00 "MLO" record and the 0x4002BC7C function reached from many sites.
7. Whether any NS-visible code reads the 0x4A0022C4 device-type field to gate
   features (GP vs HS).
