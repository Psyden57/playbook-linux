# 128 - RE: QNX's DSI second pass (0x17094), the VC enable (0x16bd8), and the state the bridge programmer runs in

**Static analysis only.** Nothing was executed; no device, USB, GDB or `/dev/ttyACM0` was
touched. `/home/psyden/rea-work` was not read or modified. The only file written is this one.

---

## Question

Linux (6.15.11, OMAP4430 "winchester") programs the TC358764 DSI→LVDS bridge with DSI
"generic long write" packets on OMAP DSI hardware virtual channel 1, packet VC ID 0,
mirroring QNX's `winch_lcdctl` helper at `0x13ce0`. The OMAP DSI reports the packets sent
(`DSI_VC_IRQSTATUS(1)` = PACKET_SENT, TX FIFO empty, no error bits) but the bridge never
reacts: writing bridge register `0x0524` through the reference's own GPIO self-test
(`0x196dc`) does not move the bridge status pin (OMAP `GPIO2_4` / "GPIO36" stays high for
every pattern). On QNX the same test passes.

Five questions, in priority order:

1. What exactly does `0x17094` do — every register write, in order, with values and delays
   (specifically: `DSI_CTRL` 0x40, `DSI_VC_CTRL(0)/(1)` 0x100/0x120, `DSI_VC_TE`,
   `DSI_CLK_CTRL` 0x54, `DSI_TIMING1/2` 0x58/0x5C, `DSI_COMPLEXIO_CFG1` 0x48, the PHY
   registers, a PLL/GO register), and where "interface enable" / "LP clock enable" happen.
2. What does the "DSI VC enable" at `0x16bd8` write, and to which VC?
3. In what order, from `0x19020` to the bridge programming at `0x1938c`, and what is the
   DSI state at the moment `0x13e50` runs?
4. Is the bridge ever programmed over I²C?
5. Does the binary ever write `DSI_VC_CTRL(1)` bit 9 (MODE_SPEED, HS)?

### Answers in one paragraph

`0x17094` is **`L1DSS_DSI1_lp_mode_start_DSI_QNX`** (the name is in the image, see §1). It
writes exactly **two** MMIO registers itself — `VC_SHORT_PACKET_HEADER(1) = 0` and
`VC_SHORT_PACKET_HEADER(0) = 0`, i.e. MIPI "null packets" to start the HS clock — and does
everything else through seven calls: `DSI_VcConf` (`0x21200`, TX/RX FIFO VC sizes 0x70/0x74),
`DSI_VcParamsConf` (`0x21638`, a full rewrite of `VC_CTRL(1)` twice and `VC_CTRL(0)` three
times), `DSI_ParamsConf` (`0x21994`, ten read-modify-writes of `DSI_CTRL`),
`DSI_CIoLaneConf` (`0x1ded4`, a full rewrite of `DSI_COMPLEXIO_CFG1` 0x48), `DSI_Status`
(`0x1fdd8`, read-only), `DSI_TimingTaFTxStop_Read` (`0x20e40`, read-only) and
`TAAL_CheckPktSent` (`0x16888`). It **never** writes `DSI_CLK_CTRL` (0x54), `DSI_TIMING1/2`
(0x58/0x5C), `DSI_VC_TE`, the DSI PHY block (0x48044200) or the DSI PLL block (0x48044300);
those are all programmed earlier, in `0x179dc` = `L1DSS_DSI1_setup_config_QNX`.
`0x16bd8` is **`L1DSS_DSI1_hs_mode_start_DSI_QNX`**: it writes `DSI_CTRL` and `VC_CTRL(0)`
(three times, ending with the fixed value `0x20800791`, bit 9 = 1 = HS) and **never touches
`VC_CTRL(1)`** — and it runs **after** the bridge programming, so it is not a precondition
for it. Every write to `VC_CTRL(1)` anywhere in the image has **bit 9 = 0 (LP)**: the
command VC is an LP/L4 command channel, and the earlier claim that QNX runs it in HS is
wrong (the measured `0x20009D81` also has bit 9 = 0 — `0x9D81 & 0x200 == 0`). The bridge is
**never** touched over I²C: the image contains an I²C client, but it only ever addresses
slave `0x4A` on `/dev/i2c0`, never `0x1C-0x1F` / `0x38-0x3F`.

---

## Method

* Binary: `/home/psyden/playbook-linux/local/proprietary/dumped4869ifs/proc/boot/winch_lcdctl`
  (193 024 B, QNX ARM `ELF32 ET_DYN`, stripped, read-only).
* Segments (`arm-linux-readelf -l`): `PT_LOAD #0 off=0 vaddr=0 filesz=0x28048 r-x`
  ⇒ **text vaddr == file offset**; `PT_LOAD #1 off=0x28348 vaddr=0x29348 filesz=0x525c
  memsz=0x5bb8 rw` ⇒ **file offset = vaddr − 0x1000**, file-backed up to vaddr `0x2E5A4`,
  BSS up to `0x2EF00`.
* Raw disassembly of the text segment:
  `dd if=winch_lcdctl of=text.bin bs=1 count=0x28048` then
  `/home/psyden/SSDdrive/wsl-migration/home/psyden/toolchains/armv7-eabihf/bin/arm-linux-objdump
  -D -b binary -m arm --adjust-vma=0 text.bin` (40 749 lines, addresses are canonical
  vaddrs throughout this report).
* **PIC anchor `r4 = 0x2B000`** (`DT_PLTGOT`), re-materialised per function; e.g. in `0x17094`:
  `1709c: ldr r4,[pc,#2300] @0x179a0` (=`0x13F58`) + `170a0: add r4,pc,r4` with `pc=0x170a8`
  ⇒ `0x170a8 + 0x13F58 = 0x2B000`. A small positive literal added to `r4` is a GOT/data
  offset; a negative literal added to `r4` is a direct string address.
* **PLT stubs** (`add ip,pc,#0 ; add ip,ip,#0x26000 ; ldr pc,[ip,#imm]!`) were resolved from
  `DT_JMPREL` (`--use-dynamic -r`) and used to name the imported calls quoted below:
  `0x40f0 = usleep` (slot `0x2B028`), `0x40a8 = printf`, `0x4120 = puts`, `0x424c = slogf`,
  `0x421c = disp_mmap_device_memory`, `0x4204 = devctl`, `0x4348 = open`, `0x436c = close`,
  `0x4330 = exit`, `0x41a4 = calloc`, `0x41c8 = nanospin_ns`, `0x42dc = ThreadCtl`.
* **Two independent addressing layers were recovered**, both resolving to the same physical
  blocks:
  * *hardware-block table*: `mem[0x2B164]` holds `0x2B1D4` (a `R_ARM_RELATIVE` pointer), and
    `0x2B1D4` is a flat array of mapped block base addresses indexed by **`hwid/4`**
    ("not mapped %d" logs pass `hwid/4`, e.g. `r3=#70` for `hwid 0x118`).
  * *unit table*: the DSI helpers take a unit id `55 = 0x37` and index the same array at
    `55*4 = 0xDC`, i.e. **`hwid 0xDC`**.
  * The block map itself is embedded in `.data` as 16-byte `{hwid, base, size, name_ptr}`
    records (parsed from `0x296C0`…`0x2AB00`, 439 entries). Relevant rows:
    `0x0CC 0x48040000 sz 0x60 DSS top`, `0x0D0 0x48041000 sz 0x814 DISPC`,
    `0x0DC 0x48044000 sz 0x180 DSI_PROTOCOL_ENGINE1`,
    `0x0E0 0x48044200 sz 0x40 DSI_PHY1`, `0x0E4 0x48044300 sz 0x24 PLLCTRL1`,
    `0x0E8 0x48045000 sz 0x180 DSI_PROTOCOL_ENGINE2`, `0x118 0x48055000 sz 0x198 GPIO2`,
    `0x128 0x48059000 sz 0x198 GPIO4`, `0x138 0x4805D000 sz 0x198 GPIO6`,
    `0x3D4 0x4A100000 DEVICE_CORE_CONTROL_MODULE (padconf)`. The same blocks are also listed
    from the L3 view (`0x5E4 0x58004000 DSI_PROTOCOL_ENGINE1`, …), so `0x4804_4xxx` and
    `0x5800_4xxx` are two aliases of the same registers.
* **The image carries its own API vocabulary as strings**, which is what makes this
  reversible with confidence:
  * `0x2714C…0x27454` — stringified `RD_*` macro expressions, e.g.
    `0x2714C "((RD_REG_32(DSS_SS_FROM_L4_PER__DSI_PE_ADDR, DSI_PROTOCOL_ENGINE__DSI_CTRL))&0x1)"`,
    `0x27250 "((RD_BUNDLE_ARRAY_32(…, DSI_PROTOCOL_ENGINE__DSI_VIRTUAL_CHANNEL, DSI_PROTOCOL_ENGINE__DSI_VC_CTRL, 1))&0x1)"`.
  * `0x27E1C…0x28048` — a **DSI driver function-name table**
    (`DSI_PllSscCfg, DSI_PllCfg, DSI_PllGo, DSI_PllStatus, DSI_PllCtrl, DSI_CIoLaneUlpsConf,
    DSI_CIoLaneConf, DSI_CIoConf_Read, DSI_CIoConf, DSI_ClkConf, DSI_VcIrqStatusClear,
    DSI_VcIrq, DSI_CioIrqStatusClear, DSI_CioIrq, DSI_IrqStatusClear, DSI_Intr,
    DSI_ResetRead, DSI_Reset, DSI_TearingEffect, DSI_Status, DSI_TEHVSyncTiming,
    DSI_DDRTiming, DSI_VideoModeTiming, DSI_HsLpTiming, DSI_TimingTaFTxStop_Read,
    DSI_TimingTaFTxStop, DSI_PacketFormating, DSI_VcConf, DSI_OcpConf, DSI_VcParamsConf,
    DSI_GnqParams_Read, DSI_ParamsConf, DSI_VideoportConf`). Each such function passes
    `0x27E1C + own_offset` as the 5th argument of its `slogf("not mapped %d")` macro, which
    names it. (Verified example: `0x21638` does `add r3,r3,#476 @0x1dc` at `0x21670`
    ⇒ `0x27E1C + 0x1DC = 0x27FF8 = "DSI_VcParamsConf"`.)
  * `0x25F7C…0x26038` — a scenario table
    (`L1DSS_Clocks_config, TAAL_CheckPktSent, L1DSS_DISP_LCD1_Enable,
    L1DSS_DSI1_hs_mode_start_DSI_QNX, L1DSS_DSI1_lp_mode_start_DSI_QNX,
    L1DSS_DSI1_setup_config_QNX, L1DSS_DSI_Lane_config, L1DSS_Reset`), used the same way.
* A small pattern scanner (literal-pool resolution + symbolic register tracking for the
  `ldr rB,[pc]` / `ldr rB,[r4,rB]` / `ldr rB,[rB,#hwid]` / `add rB,rB,#off` / `ldr|str` idiom)
  was used to enumerate MMIO sites; every claim below was then read back in the disassembly
  and is quoted with its vaddr.

### Register-offset convention used below

All DSI offsets are **relative to the DSI protocol-engine block** (`hwid 0xDC`, i.e.
`0x4804_4000` via L4_PER or `0x5800_4000` via L3/DSS — the size field `0x180` matches the
highest offset used, `0x17C`). Derived from the code itself, not from memory of the TRM:

| off | register | evidence |
|---|---|---|
| `0x10` | DSI_SYSCONFIG | `0x1d398` (`DSI_Reset` path) writes `0x10`; `0x17c5c`; `0x17d18` region |
| `0x14` | DSI_SYSSTATUS (bit 0 = RESET_DONE) | `0x1fdd8` reads `+0x14`, `and #1`; string `0x273B8 "RD_FIELD_32(…DSI_SYSSTATUS, …RESET_DONE)"` |
| `0x40` | DSI_CTRL (bit 0 = IF_EN) | `0x15f18` `ldr r1,[r3,#64]!` / `orr|bic #1` / poll bit 0; `0x21994` writes 10 bits of it; string `0x2714C` |
| `0x48` | DSI_COMPLEXIO_CFG1 (bit 29 = RESET_DONE) | `0x1ded4` full write; `0x1fdd8` `add r3,r3,#72`+`ubfx #29`; string `0x27454` |
| `0x54` | DSI_CLK_CTRL | `0x1e5a8` = `DSI_ClkConf` writes `+0x54` (`add r3,r3,#84` at `0x1e614`) |
| `0x58` | DSI_TIMING1 (bit 15 = TA/FT-XSTOP flag) | `0x20e40` = `DSI_TimingTaFTxStop_Read` reads `+0x58`; `0x16074` `ldr r2,[r3,#88]!` `orr #0x8000`; string `0x274E0 "((RD_REG_32(…DSI_TIMING1))&(0x1<<15))"` |
| `0x60`,`0x64` | VM timing (video mode) | `0x2062c` = `DSI_VideoModeTiming` writes `+0x60`,`+0x64` |
| `0x68` | read by `DSI_CIoConf_Read` | `0x1e0cc`, `add r3,r3,#104` |
| `0x6C` | DSI "DDR timing" | `0x203ac` = `DSI_DDRTiming`, `add r7,r7,#108` |
| `0x70`,`0x74` | DSI_TX_FIFO_VC_SIZE / DSI_RX_FIFO_VC_SIZE | `0x21200` = `DSI_VcConf` writes both; field layout proves it (4-bit/3-bit alternating, one field per VC) |
| `0x7C` | written by `DSI_CIoConf` | `0x1e28c`, `add r3,r3,#124` |
| `0x84` | TX-FIFO free-space status, one byte per VC | `0x13a70` reads `base+0x84`, shifts by `(vc&3)*8`, waits until the byte ≥ 7 |
| `0x100+0x20n` | DSI_VC_CTRL(n) | `0x21638` writes `(n+8)<<5`; `0x15fbc`, `0x13ae8` |
| `+0x04` | DSI_VC_TE(n) | (not written anywhere in the image; see §1) |
| `+0x08` | DSI_VC_LONG_PACKET_HEADER(n) | `0x13ae8` `0x4100+vc*0x20+8`; `0x21080` `base+0x108+vc*0x20` |
| `+0x0C` | DSI_VC_LONG_PACKET_PAYLOAD(n) | `0x13ae8` `0x4100+vc*0x20+12` |
| `+0x10` | DSI_VC_SHORT_PACKET_HEADER(n) | `0x13ae8`? no — `0x21080` `base+0x110+vc*0x20`; `0x17094` writes `0x110`/`0x130` |
| `+0x18` | DSI_VC_IRQSTATUS(n) (bit 2 = PACKET_SENT) | `0x16888` `base+0x118+vc*0x20`, `tst #4` |

---

## 1. 0x17094: every register write

### 1.0 Identity and shape

`0x17094` spans `0x17094…0x179DC` (594 instructions) and is
**`L1DSS_DSI1_lp_mode_start_DSI_QNX`**: every `slogf` inside it passes
`0x25F7C + 0x64 = 0x25FE0` as the trace name, and `0x25FE0` is exactly that string
(`0x17094` translates the pool literal `0xFFFFAF7C`, adds `#100` at `0x17110`, and stores it
as the 5th argument at `0x17134`/`0x171F4`/`0x172FC`/…).

Signature: `void lp_mode_start(struct dsi_cfg *arg)`. It only ever reads `arg[0]` and
`arg[4]`:

```
17274: ldr r2,[sp,#12]   ; arg
17278: ldr r3,[r2]       ; if (arg->[0] == 1) ...
173a4: ldr r2,[sp,#12]
173a8: ldr r3,[r2,#4]    ; if (arg->[4] == 0) cfg[0x130]=0 else cfg[0x130]=3
```

It works on two data objects: the driver state/config block at **`0x2E5C4`** (`r4+0x35C4`;
BSS) and, inside it, the 0x50-byte *VC_CTRL field struct* at **`state+0xF0`**, whose 10 first
bytes `0xF0…0xF9` are the ten `DSI_CTRL` bit fields (see `0x21994` in §1.2) and whose bytes
`0x120…0x13E` are the `VC_CTRL` field struct (see `0x21638`). All `state[...]` writes below
are plain BSS writes, **not MMIO**.

### 1.1 What it writes *itself* — only two registers

```
17830: ldr r3,[pc,#372] @0x179ac        ; 0x164  -> hardware block table
17834: ldr r3,[r4,r3]
17838: ldr r3,[r3,#220]  @0xdc          ; table[0xDC/4] = DSI_PROTOCOL_ENGINE1 base
1783c: cmp r3,#0 ; bne  → else slogf("not mapped %d",55) + null deref
17884: add r3,r3,#304    @0x130         ; VC_CTRL(1)+0x10 = VC_SHORT_PACKET_HEADER(1)
17888: mov r2,#0
1788c: str r2,[r3]                      ; *** SPH(1) = 0 ***   (MIPI null packet, DT=0x00)
17890: bl 0x16888   (55, 1, 0x1E8480)   ; TAAL_CheckPktSent on VC1, 2 000 000 polls
178a4: cmp r0,#1 ; 178b4 puts("Could not send inital null packet to start HS clock")
...
17900: ldr r3,[r3,#220]  @0xdc
1790c: add r3,r3,#272    @0x110         ; VC_CTRL(0)+0x10 = VC_SHORT_PACKET_HEADER(0)
17910: mov r1,#0
17914: str r1,[r3]                      ; *** SPH(0) = 0 ***   (null packet on VC0)
17924: bl 0x16888   (55, 0, 0x1E8480)   ; TAAL_CheckPktSent on VC0
```

That is all. A full scan of the function's MMIO (validated by hand) gives:

| vaddr | access | target |
|---|---|---|
| `17164` | read | `DSI_PE+0x120` = `VC_CTRL(1)` (poll bit 0 = VC_EN) |
| `17224` | read | `DSI_PE+0x120` = `VC_CTRL(1)` (poll bit 15) |
| `1732C` | read | `DSI_PE+0x120` = `VC_CTRL(1)` (poll bit 0) |
| `17458` | read | `DSI_PE+0x100` = `VC_CTRL(0)` (poll bit 0) |
| `17534` | read | `DSI_PE+0x40` = `DSI_CTRL` (poll bit 0 = IF_EN) |
| `176A8` | read | `DSI_PE+0x100` = `VC_CTRL(0)` (poll bit 0) |
| `17784` | read | `DSI_PE+0x40` = `DSI_CTRL` (poll bit 0) |
| `1788C` | **write 0** | `DSI_PE+0x130` = `VC_SHORT_PACKET_HEADER(1)` |
| `17914` | **write 0** | `DSI_PE+0x110` = `VC_SHORT_PACKET_HEADER(0)` |

**No write to `DSI_CLK_CTRL` (0x54), `DSI_TIMING1/2` (0x58/0x5C), `DSI_VC_TE`, the
`COMPLEXIO_IRQ_*` registers, the DSI PHY block (`hwid 0xE0`) or the PLL block (`hwid 0xE4`)
happens in this function.** "Interface enable" *is* done here (through `0x21994`), "LP clock
enable" is **not** — see §1.2 and §3.

### 1.2 The full ordered write list, including everything it delegates

Order = execution order for the `arg` values this unit uses. Each block quotes the call site
and then what the callee writes. `cfg` = `state+0xF0`.

**(a) `170B0-170CC` — initialise the config struct (BSS, no MMIO)**

```
170b4: str r6,[r5,#300]  @0x12c  = 0      ; cfg[0x3C]  -> VC_CTRL bits 23:21
170bc: strb r7,[r5,#311] @0x137  = 1      ; cfg[0x47]  -> VC_CTRL bit 8
170c0: strb r6,[r5,#317] @0x13d  = 0      ; cfg[0x4D]  -> VC_CTRL bit 1 (SOURCE) = L4
170c4: strb r6,[r5,#314] @0x13a  = 0      ; cfg[0x4A]  -> VC_CTRL bit 4
170c8: strb r6,[r5,#310] @0x136  = 0      ; cfg[0x46]  -> VC_CTRL bit 9 (MODE_SPEED) = LP
170cc: str r6,[r5,#304]  @0x130  = 0      ; cfg[0x40]  -> VC_CTRL bits 19:17
```

**(b) `170DC` — `bl 0x21200` = `DSI_VcConf`** → writes **two** registers, each a full 32-bit
store built from 8 words of the caller's FIFO config at `0x2ED64`:

```
21260/21270: ldr r3,[…,#220] ; 212f0: add r3,r3,#112 @0x70
2134c: str r2,[r3]                        ; *** DSI_TX_FIFO_VC_SIZE (0x70) ***
213a4: add r3,r3,#116 @0x74
21478: str fp,[r2]                        ; *** DSI_RX_FIFO_VC_SIZE (0x74) ***
```
Field layout of each (from `212f4`-`21478`): bits `31:28 | 26:24 | 23:20 | 18:16 | 15:12 |
10:8 | 7:4 | 2:0`, i.e. one 4-bit + one 3-bit field per virtual channel 0…3. Called **twice**
(`0x170DC` and `0x1729C`).

**(c) `170F0` — `bl 0x21638` = `DSI_VcParamsConf`, vc = 1, cfg**

`0x170E4-0x170EC`: `mov r0,#55 ; mov r1,r7(=1) ; add r2,r5,#0xF0`.
The callee builds `(vc+8)<<5` (`216a8-216ac`) and performs **one** store (`218a0: str r4,[r7]`)
to **`DSI_VC_CTRL(1) = 0x120`**, rewriting these fields (masks read from the code):

| VC_CTRL bits | source | value in this call |
|---|---|---|
| 31 | cfg[0x30] | untouched by 0x17094 |
| 30 | cfg[0x31] | untouched (`0x13D`… no: `0x121`) |
| 29:27 | cfg[0x34] | untouched |
| 26:24 | cfg[0x38] | untouched |
| 23:21 | cfg[0x3C] = state[0x12C] | **0** |
| 19:17 | cfg[0x40] = state[0x130] | **0** |
| 13 | cfg[0x44] | untouched |
| 12 | cfg[0x45] | untouched |
| **9 = MODE_SPEED** | cfg[0x46] = state[0x136] | **0 → LP** |
| 8 | cfg[0x47] = state[0x137] | **1** |
| 7 | cfg[0x48] | untouched |
| 6 | cfg[0x49] | untouched |
| 4 | cfg[0x4A] = state[0x13A] | **0** |
| 3 | cfg[0x4B] | untouched |
| 2 | cfg[0x4C] | untouched |
| 1 = SOURCE | cfg[0x4D] = state[0x13D] | **0 → L4** |
| 0 = VC_EN | cfg[0x4E] = state[0x13E] | **0** |

Bits 5, 10, 11, 14, 15, 16, 20 and 28 are **never** modified by `0x21638` (they are not in
any `bic` mask), i.e. they read back whatever hardware/earlier code left there.

**(d) `17128-17174` — poll `VC_CTRL(1)` bit 0 == 0**
`ldr r3,[r7,#220]` (`17128`) … `add r3,r3,#288 @0x120` (`17160`) … `tst r3,#1` (`17168`);
≤10 000 iterations, each `usleep(1)` (`bl 0x40f0` at `0x17124`). Timeout ⇒
`slogf("%s wait timed out %s", "L1DSS_DSI1_lp_mode_start_DSI_QNX",
"((RD_BUNDLE_ARRAY_32(…DSI_VC_CTRL, 1))&0x1)")` at `0x171D8`, then `return 0` (`0x1793C`).

**(e) `171E8-17234` — poll `VC_CTRL(1)` bit 15 == 0** (`tst r3,#32768` at `0x17228`;
string `0x272DC "…VC_CTRL, 1))&(0x1<<15)"`).

**(f) `17274-1728C`** — `if (arg->[0] == 1) mem[0x2ED64+0x10] = 2;` (BSS only).

**(g) `1729C` — `bl 0x21200` again** → `TX/RX_FIFO_VC_SIZE` (0x70/0x74) rewritten.

**(h) `172A8-172B8` — `bl 0x21638`, vc = 1** with `cfg[0x4E] = 1` (`172ac: strb r1,[r2,#318]`)
⇒ `VC_CTRL(1)` = same field set, **MODE_SPEED = 0 (LP), VC_EN = 1**.

**(i) `172F0-17330` — poll `VC_CTRL(1)` bit 0 == 1.**

**(j) `1737C-173C8` — config struct update**

```
17384: str r2,[r3,#300] @0x12c = 4        ; bits 23:21 = 4
17390: strb r2,[r3,#311] @0x137 = 1       ; bit 8
17398: strb r1,[r3,#317] @0x13d = 0       ; bit 1 (SOURCE) = L4
1739c: strb r1,[r3,#314] @0x13a = 0       ; bit 4
173a0: strb r2,[r3,#310] @0x136 = 1       ; *** bit 9 = HS ***
173c8: strne r2,[r3,#304] @0x130 = 3      ; bits 19:17 = 3, else 0 (0x173c8 vs 0x173b8)
```

**(k) `173D4-173E4` — `bl 0x21638`, vc = 0** (`173d4: mov r1,#0`) ⇒ **`VC_CTRL(0) = 0x100`**
with `MODE_SPEED = 1 (HS)`, bits 23:21 = 4, bits 19:17 = 0/3, bit 8 = 1, SOURCE = 0,
`VC_EN = 0`.

**(l) `1741C-17460` — poll `VC_CTRL(0)` bit 0 == 0.**

**(m) `174B4-174D8` — `bl 0x21994` = `DSI_ParamsConf` with IF_EN = 0**

`174b4: strb r3,[r1,#249] @0xf9 = 0` (the IF_EN field of the 10-byte struct at `state+0xF0`),
then `0x21994`. The callee performs **ten separate read-modify-writes of `DSI_CTRL` (0x40)**:

```
21a60: bic r2,r2,#0x1000000 ; 21a70 orr  → DSI_CTRL bit 24 = cfg[0]
21b2c: bic #0x800000        ; …           → bit 23 = cfg[1]
21bf8: bic #0x400000                     → bit 22 = cfg[2]
21cc4: bic #0x200000                     → bit 21 = cfg[3]
21d90: bic #0x100000                     → bit 20 = cfg[4]
21e5c: bic #0x80000                      → bit 19 = cfg[5]
21f28: bic #8                            → bit 3  = cfg[6]
21ff4: bic #4                            → bit 2  = cfg[7]
220c0: bic #2                            → bit 1  = cfg[8]
2218c: bic #1                            → *** bit 0 = IF_EN = cfg[9] = 0 ***
```
(All ten stores are at `21a74 / 21b40 / 21c0c / 21cd8 / 21da4 / 21e70 / 21f3c / 22008 /
220d4 / 2219c`.) The `DSI_CTRL` bits 4…18, 25…31 are left untouched.

**(n) `174F0-1753C` — poll `DSI_CTRL` bit 0 == 0** (string `0x2714C`).

**(o) `17590-1759C` — `bl 0x1ded4` = `DSI_CIoLaneConf`** ⇒ full rewrite of
**`DSI_COMPLEXIO_CFG1 (0x48)`** from cfg bytes `0x80…0xAC` (= `state[0x170…0x19C]`):

```
1df40/1df44: ldr r3,[r3,#72] @0x48        ; current value (used for nothing but the log check)
1e0ac: str r2,[r3]                        ; *** COMPLEXIO_CFG1 = rebuilt value ***
```
Field map (from `1dff8-1e0a8`): `31←cfg[0x80]`, `30←cfg[0x81]`, `28:27←cfg[0x84]`,
`20←cfg[0x88]`, `19←cfg[0x89]`, `18:16←cfg[0x8C]`, `15←cfg[0x90]`, `11←cfg[0x98]`,
`10:8←cfg[0x94]`, `7←cfg[0xA0]`, `6:4←cfg[0x9C]`, `3←cfg[0xA8]`, `2:0←cfg[0xA4]`.
`0x17094` never modifies those cfg bytes — it re-writes the *lane configuration the setup
pass established* (report 118 measured the reference value `DSI_COMPLEXIO_CFG1 = 0x2A054321`,
whose fields `2:0 = 1`, `6:4 = 2`, `10:8 = 3`, `18:16 = 5`).

**(p) `175AC-175E8` — `bl 0x1fdd8` = `DSI_Status`** (read-only: `+0x14` bit 0 RESET_DONE,
`+0x48` bit 29 RESET_DONE, …) in a retry loop; on exhaustion
`puts("DSI_Status timeout")` (`0x175E8`, string `0x27370`).

**(q) `17600-17634` — config struct update, then `bl 0x21638`, vc = 0, twice**

```
17600: strb r7,[r6,#314] @0x13a = 0
17608: strb r8,[r6,#310] @0x136 = 1      ; bit 9 = HS
1760c: strb r7,[r6,#289] @0x121 = 0      ; bit 30
17620: bl 0x21638   (r1 = r7 = 0)        ; VC_CTRL(0): VC_EN = cfg[0x4E] (still 0)
17624: strb r8,[r6,#318] @0x13e = 1
17634: bl 0x21638   (r1 = 0)             ; VC_CTRL(0): VC_EN = 1
```

**(r) `17664-176B0` — poll `VC_CTRL(0)` bit 0 == 1.**

**(s) `17700-17710` — `strb r3,[r1,#249] @0xf9 = 1` then `bl 0x21994` ⇒ `DSI_CTRL` IF_EN = 1**
(plus the same ten RMWs; `cfg[1]`…`cfg[5]` and `cfg[6]`…`cfg[8]` are all 0 in this path,
only bits 8/7/0 of `VC_CTRL`-side matter — `DSI_CTRL` ends with bits 24…19 = 0, 3…1 = 0,
bit 0 = 1, everything else preserved).

**(t) `17740-1778C` — poll `DSI_CTRL` bit 0 == 1.**

**(u) `177D4-1782C` — `bl 0x20e40` = `DSI_TimingTaFTxStop_Read`**, which reads
`DSI_TIMING1 (0x58)` and splits it (`bit31→[0]`, `bit30→[1]`, `bit29→[2]`, `bits28:16→[4]`,
`bit15→[6]`, `bit14→[7]`, `bit13→[8]`, `bits12:0→[10]`; `20eb0-20ef0`). The loop at
`0x17800-0x17820` polls `obj[6]` (i.e. **`TIMING1` bit 15**) until it is 1, ≤10 000 ×
`usleep(1)`; timeout ⇒ `puts("DSI_TimingTaFTxStop_Read timeout")` (`0x1782C`, string
`0x2722C`). **Read-only** — `TIMING1` is never written here.

**(v) `1788C` / `17914` — the two null packets** (see §1.1): `SPH(1) = 0`,
`TAAL_CheckPktSent(55, 1, 2 000 000)`, then `SPH(0) = 0`, `TAAL_CheckPktSent(55, 0,
2 000 000)`. `0x16888` busy-polls `VC_IRQSTATUS(vc) = 0x118+vc*0x20` with `tst #4`
(`1690c`) — bit 2 = PACKET_SENT. Failure ⇒ `puts("Could not send inital null packet to start
HS clock")` (`0x178B4`/`0x17938`, string `0x27384`) and return.

**(w) `17954`/`17980` — one more `DSI_TimingTaFTxStop_Read` and one more `DSI_Status`**, then
`1793C: mov r0,#0` — the function always returns 0.

### 1.3 Delays

The only delay mechanism is `usleep` (`0x40f0`, PLT slot `0x2B028 = usleep` — resolved from
`DT_JMPREL`), always with `r0 = 1`, inside the polling loops (`17124`, `171E4`, `172EC`,
`17418`, `174F4`, `17668`, `17744`, `17800`). There is **no** fixed settle delay in `0x17094`
and **no** `nanospin_ns`. The two `TAAL_CheckPktSent` calls use a *poll count* of
`0x1E8480 = 2 000 000`, not a time.

---

## 2. 0x16bd8 and the VC enable path

### 2.1 Identity

`0x16bd8…0x17064` is **`L1DSS_DSI1_hs_mode_start_DSI_QNX`**:
`16c24: add r9,r9,#64 @0x40` applied to the scenario-table anchor `0x25F7C`
⇒ `0x25FBC = "L1DSS_DSI1_hs_mode_start_DSI_QNX"`. It also uses the `DSI_CTRL` (0x2714C) and
`VC_CTRL(0)` (0x271A0) condition strings and `DSI_TimingTaFTxStop_Read timeout` (0x2722C).

It takes the same `struct dsi_cfg *` argument and reads `arg[0]`, `arg[4]`, `arg[0x20]`,
`arg[0x24]`.

### 2.2 Ordered effect

```
16bf8: strb r3,[r1,#249] @0xf9 = 0
16c04: bl 0x21994                     ; DSI_CTRL: IF_EN = 0 (+9 other bit RMWs)
16c3c-16c78: poll DSI_CTRL bit0 == 0   (10000 × usleep(1))
16cd4: strb r1,[r2,#318] @0x13e = 0
16ce0: bl 0x21638  (r1 = 0)           ; VC_CTRL(0) rewritten, VC_EN = 0
16d18-16d58: poll VC_CTRL(0) bit0 == 0
16db4-16dec: cfg bit4 (0x13A) = (arg->[4]==0 ? 0 : 1)
             cfg bit9 (0x136) = 1                       <-- HS
             cfg bit30 (0x121) = (arg->[4]==0 ? 1 : 0)
16dfc: strb r3,[r2,#317] @0x13d = arg->[0]              <-- VC_CTRL bit 1 = SOURCE
16e10: bl 0x21638  (r1 = 0)           ; VC_CTRL(0) rewritten, VC_EN = 1
16e18-16e98: if (arg->[4] == 1):
               if (arg->[0x20] == 8) obj[0x20] = 0xEC00003E | (arg->[0x24] << 8)
               if (arg->[0x20] == 6) obj[0x20] = 0xEC00000E | (arg->[0x24] << 8)
               bl 0x21080 (55, 0, 0)   ; DSI_PacketFormating -> VC_LONG_PACKET_HEADER(0)
16ef4: movw r3,#0x0791 ; 16ef8: movt r3,#0x2080
16efc: str r3,[r2]                    ; *** VC_CTRL(0) = 0x20800791  (fixed) ***
16f0c: strb r3,[r1,#249] @0xf9 = 1
16f18: bl 0x21994                     ; DSI_CTRL: IF_EN = 1
16f50-16f90: poll DSI_CTRL bit0 == 1
16fe8-17020: poll DSI_TIMING1 bit15 == 1 via 0x20e40
             else puts("DSI_TimingTaFTxStop_Read timeout")
17038: return 0
```

where `0x21080` (`DSI_PacketFormating`) is:

```
210f8-21110: base+0x108+(vc<<5) ; 2110c: ldr r3,[r4,#32] ; 21110: str r3,[r1]   (which==0)
21160-21174: base+0x110+(vc<<5) ; 21174: ldr r3,[r4,#40] ; 21178: str r3,[r1]   (which==1)
```
i.e. it writes `VC_LONG_PACKET_HEADER(vc)` from `obj[0x20]` or `VC_SHORT_PACKET_HEADER(vc)`
from `obj[0x28]`.

**`0x20800791` decoded:** bit 29, bit 23, bit 10, bit 9, bit 8, bit 7, bit 4, bit 0
(`0x20800791 = 0x20000000 + 0x00800000 + 0x791`). So `VC_CTRL(0)` ends with
**`MODE_SPEED = 1` (HS), `VC_EN = 1`, `SOURCE = 0` (L4), `DCS/…` bits 4/7/8/10 set,
bit 23 and bit 29 set.**

### 2.3 Which VC?

| callee | VC touched | registers |
|---|---|---|
| `0x21994` | – | `DSI_CTRL` (0x40), 10 bits, twice (IF_EN 0 then 1) |
| `0x21638` ×2 | **VC 0** | `VC_CTRL(0)` = 0x100 |
| `0x21080` (conditional) | **VC 0** | `VC_LONG_PACKET_HEADER(0)` = 0x108 |
| direct store `0x16efc` | **VC 0** | `VC_CTRL(0)` = 0x100 |

**`0x16bd8` never reads or writes `VC_CTRL(1)` (0x120) at all.** Only `0x17094` and
`0x179dc` touch VC 1.

### 2.4 Ordering consequence

The sequencer calls `0x17094` at **`0x1934C`**, `0x13e50` at **`0x1938C`** and `0x16bd8` at
**`0x19398`** — i.e. **the "HS mode start" runs *after* the bridge has been programmed.** It
is therefore not a precondition for the bridge writes and cannot explain a bridge that
ignores them; if anything it is the *panel video* start.

Note also that the task's description of `0x16bd8` as "DSI VC enable" is a misnomer: the
image's own name is `L1DSS_DSI1_hs_mode_start_DSI_QNX`, and its net effect is "disable
interface + VC0, reconfigure VC_CTRL(0), force `VC_CTRL(0)=0x20800791`, re-enable interface".
There *are* two `printf` format strings that would fit a "dsi_vc_enable" (0x25F30
`"dsi_vc_enable enable channel %d %x\n"`, 0x25F54 `"dsi_vc_enable disable channel %d %x\n"`),
but no function in the image references them in this path.

---

## 3. DSI state at the moment 0x13e50 runs (ordered register map)

### 3.1 Call list, `0x19020` entry → `0x1938C`

`0x19020` clears a local, logs (`19040-19058`, `slogf` with the strings at `0x193F4`/
`0x193F8`), and dispatches on `mem[0x2B100][0]` ("mode"):

```
1905C: r3 = *(mem[0x2B100]) ; cmp r3,#1
19070-190D0:  mode == 1:  *[table[0x118/4] + 0x194] = 0x2000   ; GPIO2 SETDATAOUT bit 13
                          (GPIO2_11 high), then 0x1C6F8(0)
190E0:        mode == 2:  0x1C16C(0)
```

then, unconditionally, the DSI bring-up:

| vaddr | call | identity / effect |
|---|---|---|
| `190E8` | `0x18CE8()` | unnamed sub-init; calls `0x4FD4`, `0x4638`, `0x47F8` (not DSI MMIO) |
| `190EC` | `0x18B6C()` | no MMIO detected |
| `190FC` | `0x16444(r5)` | (only if `r6 = arg1 != 0`) |
| `1910C` | `0x162EC(mem[0x2B148], r5)` | (only if `r6 != 0`) |
| `19110` | `0x18A7C()` | calls `0x12120, 0x11F00, 0x11CD0, 0x1337C, 0x11ACC, 0x7BA0, 0x78FC, 0x7768, 0x751C, 0x7230` (DISPC/panel-enable cluster) |
| `1911C` | `0x179DC(mem[0x2B148])` | **`L1DSS_DSI1_setup_config_QNX`** — the whole DSI configuration (see 3.2) |
| `19124` | `0x16610(r5)` | writes `hwid 0xCC + 0x40` = **`DSS_CONTROL` @ `0x4804_0040`** (`0x166E4`) — LCD1/DSS output enable |

then the branch at `0x19134`:

* `mode == 2` → `0x1913C-0x19198`: `0x17094(mem[0x2B148])`, `0x14604(&local)` (builds a
  **second** DSI instance object), and if that is non-zero
  `0x162EC / 0x18A7C / 0x179DC / 0x16610 / 0x17094(local) / 0x16BD8(local)`;
  otherwise `0x16BD8(mem[0x2B148])`. (This is the dual-DSI variant — PE2 at `hwid 0xE8`.)
* `mode != 2` and `mem[0x2B12C][0] != 0` → `0x191B0-0x19278`:
  **`*(hwid 0x128 + 0x190) = 0x40`**, `usleep(20000)`, **`*(hwid 0x128 + 0x194) = 0x40`**,
  `usleep(20000)` — a 20 ms low pulse on **GPIO4_6** (`0x48059000`, `SET/CLEARDATAOUT`
  bit 6), then `b 0x19344`.
* `mode != 2` and `mem[0x2B12C][0] == 0` → `0x1927C-0x19340` (this unit):
  **`*(hwid 0x118 + 0x190) = 0x01000000`** (`192D0/192D4`) — `GPIO2_CLEARDATAOUT` bit 24,
  i.e. **GPIO2_24 / "GPIO36" / bridge RESETN low**;
  `usleep(20000)` (`192D8-192DC`, 20 ms);
  **`*(hwid 0x118 + 0x194) = 0x01000000`** (`19330/19334`) — RESETN high;
  `usleep(20000)` (`19338-19340`, 20 ms);
  then falls into `0x19344`.

The common tail:

| vaddr | call | effect |
|---|---|---|
| `19344-1934C` | `0x17094(mem[0x2B148])` | LP-mode start (§1) |
| `19350-19388` | `0x196DC(*mem[0x2B110])` if non-zero | bridge GPIO self-test (report 126); on failure with the `-T` flag set → `exit(r0+2)` (`0x19388: bl 0x4330`) |
| **`1938C`** | **`0x13E50()`** | **bridge init: 32 × `0x13CE0(reg,value)` DSI generic long writes on VC1** |
| `19398` | `0x16BD8(mem[0x2B148])` | HS-mode start (§2) |

### 3.2 What `0x179DC` (`L1DSS_DSI1_setup_config_QNX`) puts in place before this

`0x179DC…0x18A7C`. Ordered calls (each name is from the image's own table):

| vaddr | call | function | target registers it writes |
|---|---|---|---|
| `17D60` | `0x1E5A8` | `DSI_ClkConf` | **`DSI_CLK_CTRL` (0x54)** and `+0x88` (`add r3,r3,#84` @`1e614`, `#136` @`1e5dc`) |
| `17D6C`,`17D88` | `0x1D9E8` | `DSI_PllStatus` | read `PLLCTRL1` (`hwid 0xE4`) |
| `17E14` | `0x1D398` | `DSI_PllCfg` | `PLLCTRL1 + 0x0C` (`1d404: add r3,r3,#12`) |
| `17E24` | `0x1D8EC` | `DSI_PllGo` | `PLLCTRL1` GO + status |
| `17E30`,`17E4C` | `0x1D9E8` | `DSI_PllStatus` | read |
| `17F34`,`17FF8` | (inline) | `L1DSS_DSI_Lane_config` | **`*(hwid 0x3D4 + 0x618)`** = padconf `0x4A10_0618` RMW (DSI pad mux) |
| `18078` | `0x1E28C` | `DSI_CIoConf` | `+0x7C` (`add r3,r3,#124` @`1e2c0`) |
| `18084` | `0x1E0CC` | `DSI_CIoConf_Read` | read `+0x68` |
| `180F0` | `0x1DED4` | `DSI_CIoLaneConf` | **`DSI_COMPLEXIO_CFG1` (0x48)** |
| `18100`,`18120` | `0x1FD D8` | `DSI_Status` | read `+0x14`, `+0x48` |
| `18228`,`1823C`,`18438` | `0x21994` | `DSI_ParamsConf` | **`DSI_CTRL` (0x40)** bits 24…19, 3…1, 0 |
| `18270` | `0x21200` | `DSI_VcConf` | **`0x70` / `0x74`** (TX/RX FIFO VC sizes) |
| `1829C`,`182BC` | `0x21498` | `DSI_OcpConf` | `+0x10` and others |
| `183CC` | `0x221BC` | `DSI_VideoportConf` | **`DSI_CTRL` (0x40)** |
| `1844C` | `0x1F9F0` | `DSI_Reset` | `+0x10` (and `+0x40`, `1fb28: add r7,r7,#64`) |
| `184A0` | `0x21638` | `DSI_VcParamsConf` | **`VC_CTRL(1)` = 0x120** — `cfg[0x46]` = 0 ⇒ LP, `SOURCE = 1` (VP), bits 19:17 = 3 |
| `184E8` | `0x21638` | `DSI_VcParamsConf` | **`VC_CTRL(0)` = 0x100** (`184e0: mov r1,#0`) |
| `1855C` | `0x2062C` | `DSI_VideoModeTiming` | `+0x60`, `+0x64` |
| `185B0` | `0x203AC` | `DSI_DDRTiming` | `+0x6C` |
| `18620`,`18634`,`188E0` | `0x20F10` | `DSI_TimingTaFTxStop` | **`TIMING1` (0x58)** (`add r3,r3,#88` @`20f7c`) |
| `186A8`,`18954` | (inline) | – | read `0x58` |
| `1872C` | `0x21638` | `DSI_VcParamsConf` | **`VC_CTRL(1)` = 0x120**, `184..`: `state[0x13E]=1`, `state[0x137]=1`, `state[0x13D]=0`, `state[0x13A]=0`, `state[0x136]=0`, `state[0x130]=3` ⇒ LP, L4, VC_EN = 1 |
| `187A0` | (inline) | – | read `VC_CTRL(1)` |
| `18808` | `0x21994` | `DSI_ParamsConf` | `DSI_CTRL` |
| `1887C` | (inline) | – | read `0x40` |
| `18A04` | `0x1FDD8` | `DSI_Status` | read |

### 3.3 The register state `0x13E50` actually runs in

Between `0x19020` and `0x1938C` the only MMIO writes are: the GPIO reset pulse (§3.1), the
`DSS_CONTROL` write at `0x16610`, and everything inside `0x179DC` (via the table above) and
`0x17094` (§1). Reconstructing:

| register | value / field state when `0x13E50` is entered | set by |
|---|---|---|
| `DSI_SYSCONFIG` 0x10 | written by `DSI_Reset`/setup | `0x179DC`→`0x1F9F0` |
| `DSI_SYSSTATUS` 0x14 | RESET_DONE = 1 (polled) | HW, polled by `0x1FDD8` |
| `DSI_CTRL` 0x40 | **IF_EN = 1**; bits 24…19 = 0, bits 3…1 = 0 (this path's cfg); all other bits as setup left them | `0x17094`→`0x21994` (`0x17710`) |
| `DSI_COMPLEXIO_CFG1` 0x48 | rebuilt lane map from cfg (reference reads `0x2A054321`) | `0x17094`→`0x1DED4` (`0x1759C`) |
| `DSI_CLK_CTRL` 0x54 | written by `DSI_ClkConf` (TAAL/`L1DSS_Clocks_config`); **not touched again** | `0x179DC`→`0x1E5A8` (`0x17D60`) |
| `DSI_TIMING1` 0x58 | written by `DSI_TimingTaFTxStop`; then **read-only** (bit 15 polled to 1) | `0x179DC`→`0x20F10`; polls `0x17094`, `0x16BD8` |
| `DSI_TIMING2` 0x5C | not written by anything in this path (only `0x1E5A8`'s `+0x88`/`0x1E28C`'s `+0x7C` neighbours) | – |
| `DSI_TX/RX_FIFO_VC_SIZE` 0x70/0x74 | from `0x2ED64`'s FIFO config | `0x17094`→`0x21200` (`0x170DC`, `0x1729C`) |
| `DSI_PHY1` (`0x4804_4200`) | configured by the PLL/PHY cluster during setup; **no write in 0x17094 / 0x13E50** | `0x179DC` |
| `DSI PLLCTRL1` (`0x4804_4300`) | programmed (`CFG` @ +0x0C, GO), locked, never touched again | `0x179DC`→`0x1D398`/`0x1D8EC` |
| `DSI_VC_CTRL(0)` 0x100 | `MODE_SPEED = 1` (HS), `VC_EN = 1`, `SOURCE = 0`, bits 23:21 = 4, 19:17 = 0/3, bit 8 = 1 | `0x17094`→`0x21638` (`0x17634`) |
| **`DSI_VC_CTRL(1)` 0x120** | **`MODE_SPEED = 0` (LP), `VC_EN = 1`, `SOURCE = 0` (L4), bits 23:21 = 0, 19:17 = 0, bit 8 = 1** | `0x17094`→`0x21638` (`0x172B8`) |
| `DSI_VC_TE(0)/(1)` 0x104/0x124 | **never written anywhere in the image** | – |
| `DSI_VC_SHORT_PACKET_HEADER(1)` 0x130 | **0** (null packet), `VC_IRQSTATUS(1).bit2` = PACKET_SENT observed | `0x1788C` + `0x16888` |
| `DSI_VC_SHORT_PACKET_HEADER(0)` 0x110 | **0** (null packet), PACKET_SENT observed | `0x17914` + `0x16888` |
| `GPIO2` 0x4805_5000 | `OE` bit 4 = **input** (`orr #0x10`, `0x197B8`), `CLEARDATAOUT`/`SETDATAOUT` bit 24 pulsed | `0x1927C` block, `0x196DC` |
| `GPIO6` 0x4805_D000 | `OE` bit 11 = input (`orr #0x800`, `0x19874`) | `0x196DC` |
| `DSS_CONTROL` 0x4804_0040 | written by `0x16610` | `0x19124` |

### 3.4 The bridge transport itself

`0x13E50` writes 32 bridge registers, every one via `0x13CE0(reg, value)`:

```
13d10: mov r3,#6 ; 13d14: str r3,[sp]      ; length = 6
13d18: mov r1,#0 ; 13d1c: str r1,[sp,#4]   ; header high byte = 0
13d20: mov r0,#1                            ; *** hw VC = 1 ***
13d24: mov r2,#0x29                         ; *** DT = 0x29 = MIPI generic long write ***
13d28: add r3,sp,#8                         ; payload = {reg_lo,reg_hi,v0,v1,v2,v3}
13d2c: bl 0x13AE8
```
`0x13AE8` (`vc` in `fp`):

```
13b18-13b34: if (vc <= 3) clear bit 1 (SOURCE) of VC_CTRL(vc)      ; force L4 path
13b50-13b58: read VC_CTRL(vc); if (bit 16) return -1              ; "busy"
13b68-13b84: *(DSS+0x4000+0x100+vc*0x20+0x08) = (hdr24<<24)|(len<<8)|dt   ; LONG_PACKET_HEADER
13bcc-13c0c: payload words -> +0x0C, 4 bytes at a time, preceded by 0x13A70(vc,7)
13c90-13cc8: tail bytes      -> +0x0C
```
`0x13A70(vc, 7)` reads `DSS+0x4000+0x84`, extracts the byte `(vc&3)*8` and loops until it is
`>= 7` (TX-FIFO free words), with `usleep(2)` on overflow (`13ad4-13ad8`).

So the bridge sees: **hardware VC 1, L4 source, DT 0x29 (packet VC field = bits 7:6 of 0x29 =
0), 6-byte payload = 16-bit register address + 32-bit value, one packet per register.**

`0x13E50`'s writes in order (register: value; `mov`/`movw`+`movt` at each site):

```
0x13CE0(0x13C, 0x00000004)   13e5c
0x13CE0(0x114, 0x00000004)   13e68
0x13CE0(0x164, <mem[0x2B178]>) 13e7c   (PPI_DnS_CLRSIPOCOUNT, -c)
0x13CE0(0x168, <same>)       13e88
0x13CE0(0x16C, <same>)       13e94
0x13CE0(0x170, <same>)       13ea0
0x13CE0(0x210, 0x0000001F)   13eac
0x13CE0(0x134, 0x0000001F)   13eb8
0x13CE0(0x104, 0x00000001)   13ec4
0x13CE0(0x204, 0x00000001)   13ed0
0x13CE0(0x4A4, 0x00000000)   13edc
0x13CE0(0x4A0, 0x4400C02D)   13ee8   (movw 0xC02D / movt 0x0044)
0x13CE0(0x480, 0x03020100)   13ef8
0x13CE0(0x484, 0x08050704)   13f08
0x13CE0(0x488, 0x0F0E0A09)   13f18
0x13CE0(0x48C, 0x100D0C0B)   13f28
0x13CE0(0x490, 0x12111716)   13f38
0x13CE0(0x494, 0x1B151413)   13f48
0x13CE0(0x498, 0x16011918)   13f58   (movw 0x1819 / movt 0x061A → 0x061A1819)
0x13CE0(0x450, 0x03F00120)   13f68
0x13CE0(0x454, 0x00780001)   13f78
0x13CE0(0x458, 0x00760400)   13f88
0x13CE0(0x45C, 0x000A0001)   13f98
0x13CE0(0x460, 0x000C0258)   13fa8
usleep(100)                  13fb8   (bl 0x40f0, r0=100 → 100 µs)
0x13CE0(0x4A0, 0x0400C02D)   13fc0
0x13CE0(0x49C, 0x00000001)   13fd0
0x13CE0(0x520, 0x00000005)   13fdc
0x13CE0(0x524, 0x00000005)   13fe8
0x13CE0(0x504, 0x00000004)   13ff4
0x13CE0(0x464, 0x00000001)   14000
```

(Values are the literal constructions at each site; `0x13CE0`'s argument order is
`(r0 = reg, r1 = value)` — `13ce8: strb r0,[sp,#8]` / `13cf4: strb r1,[sp,#10]`.)

---

## 4. Is the bridge ever programmed over I²C?

**No.**

Evidence:

* The image contains exactly three I²C-related strings, all inside one small module:
  `0x27C64 "Failed to write i2c, err %d"`, `0x27C84 "i2c_write_reg fail, err %d "`,
  `0x27DB8 "/dev/i2c0"`. There is no other `i2c`/`I2C` byte sequence anywhere in the file
  except the OMAP hwinfo *block names* (`ATTILA_L4PER_TA_I2C1..4`, `MSHSI2COCP1..4`), which are
  part of the generic OMAP4430 register database and are not referenced by this code path.
* The client is `0x1C7AC` (read) and `0x1C85C` (write): each does
  `open("/dev/i2c0", O_RDWR)` (`1c86c-1c87c: bl 0x4348`, `r1=2`; error path uses
  `__get_errno_ptr` + `printf` with the strings above) and then either
  `devctl(fd, 0xC0140507, &msg, 22)` (`1c120-1c130`, `devctl` = `0x4204`) or
  `devctl(fd, 0x8010505, &msg, 19)` (`1c088-1c098`), then `close(fd)` (`0x436c`).
* **Every one of the four call sites passes slave `0x4A` (74) and internal register `0x0A`**:
  `0x1C810`/`0x1C82C` (`mov r1,#74 ; mov r2,#10`, read, sub-index 0/1) and
  `0x1C8C0`/`0x1C8E0` (`mov r1,#74 ; mov r2,#10`, write). No site uses `0x1C…0x1F` or
  `0x38…0x3F` — the TC358764's 7-bit (`0x1C-0x1F`) / 8-bit (`0x38-0x3E`) addresses are never
  constructed anywhere in the image.
* Callers of `0x1C7AC` are `0x1B038`, `0x1B0E4`, `0x1B1A0`, `0x1B3B8`, `0x1B474`, `0x1B564`,
  `0x1BA54` — the panel-strap/detect module (its string neighbours are
  `0x25EB0 "Invalid or no panel connected"`, `0x25ED0 "CMI panel detected"`,
  `0x25EE4 "TMD panel detected"`, `0x25EF8 "LG panel detected"`). `0x1BA54` is the
  "panel strap read" that report 122 places between the DSI reset pulse and the bridge init.
* Neither `0x13E50` (bridge init) nor `0x196DC` (self-test) nor `0x17094` nor `0x16BD8` nor
  `0x13AE8`/`0x13CE0`/`0x13A70` reference `0x40A8/0x4120/0x4204/0x4348/0x436C` (the I²C
  primitives) — their only external calls are `usleep`/`nanospin_ns`/`puts`/`slogf`.

**Conclusion:** the TC358764 is programmed **exclusively** over DSI generic long writes on
hardware VC 1 with packet VC 0. The I²C client in this binary talks to one non-bridge device
at slave `0x4A`, register `0x0A` (an 8-bit-valued read/write pair), for panel identification.

---

## 5. VC_CTRL(1) and MODE_SPEED: what the binary really writes

### 5.1 Every writer of `VC_CTRL(1)` in the image

`VC_CTRL` is only ever written by `0x21638` (`DSI_VcParamsConf`, a full field-set rewrite)
and by the *dead* helper `0x15FBC`. Calls with `vc = 1`:

| vaddr | inside | `cfg[0x46]` = bit 9 | other fields |
|---|---|---|---|
| `0x184A0` | `0x179DC` setup_config | **0 → LP** (`18488: strb r7,[r6,#310]`, `r7 = 0`) | bit 8 = 1, bit 1 **= 1 (VP)**, bits 19:17 = 3, bits 23:21 = 0, VC_EN from `0x13E` |
| `0x1872C` | `0x179DC` setup_config | **0 → LP** (`18718: strb r3,[r2,#310] = 0`) | bit 8 = 1, bit 1 **= 0 (L4)**, bits 19:17 = 3, VC_EN = 1 |
| `0x170F0` | `0x17094` lp_mode_start | **0 → LP** (`170c8`) | bit 8 = 1, bit 1 = 0, bits 19:17 = 0, VC_EN = 0 |
| `0x172B8` | `0x17094` lp_mode_start | **0 → LP** | bit 8 = 1, bit 1 = 0, bits 19:17 = 0, **VC_EN = 1** |

So **`DSI_VC_CTRL(1)` is LP (bit 9 = 0) at every point in the life of the driver**, and the
final pre-bridge value has `VC_EN = 1`, `SOURCE = 0` (L4), `MODE_SPEED = 0` (LP).

The only code that can set bit 9 on a chosen VC is `0x15FBC`, and **it has no callers**:

```
15fe4: subs r6,r1,#0 ; 13a0601
15fec: ubfx r2,r2,#9,#1 ; 15ff0: cmp r6,r2 ; 15ff4: beq 0x16088  (already equal → return)
15ff8-16000: clear VC_CTRL(vc).bit0 (VC_EN)
16008: bl 0x15F18                       ; IF_EN = 0, poll
16014-1602C: if (hs) VC_CTRL(vc) |= 0x200          ; *** would set MODE_SPEED ***
16034-1604C: else   VC_CTRL(vc) &= ~0x200
16060-16068: VC_CTRL(vc) |= 1            ; VC_EN back on
16074-1607C: DSI_TIMING1 (0x58) |= 0x8000
16084: bl 0x15F18                        ; IF_EN = 1, poll
```
A whole-image grep for `bl 0x15fbc` finds no caller; likewise `0x15F18` (the clean
`DSI_CTRL` bit-0 IF_EN setter with the 100 × `nanospin_ns` poll) is only called from
`0x16008`/`0x16084`, i.e. only from that dead function. **The MODE_SPEED setter and the
IF_EN setter are dead code in this image** — the live paths go through `0x21994`/`0x21638`.

### 5.2 Which VC does get HS

`VC_CTRL(0)` (0x100):

| vaddr | value / fields |
|---|---|
| `0x173E4` | bits 23:21 = 4, 19:17 = 0/3, **bit 9 = 1**, bit 8 = 1, bit 1 = 0, VC_EN = 0 |
| `0x17620` | same, VC_EN = 0 |
| `0x17634` | same, VC_EN = 1 |
| `0x16EFC` | **`0x20800791`** — bit 29, 23, 10, **9**, 8, 7, 4, 0 |

### 5.3 Reconciliation with the QNX measurement `0x20009D81`

`0x20009D81 & 0x200 = 0` — **bit 9 is clear**, i.e. the measured QNX value is LP, exactly what
the code writes. Report 127 §3 decoded this value as "bit 9 = 1 / HS"; that decode is
arithmetically wrong (the value contains `0x9D81`, which has bits 0, 7, 8, 10, 11, 12, 15 —
not bit 9). Report 127's *implementation* nevertheless wrote `0x20009D81`, so the Linux side
has in fact been running the command VC in LP all along; only the comment and the conclusion
were wrong.

Which bits of `0x20009D81` can `0x21638` produce? `0x21638` never touches bits 5, 10, 11, 14,
15, 16, 20, 28. The measured value has bits **10, 11, 15** set, so those must come from
hardware or from other code (bit 15 is the flag `0x17094` polls as "must be 0", so it is a
live status bit). Bits 0 (`VC_EN`), 7, 8, 12, 29 are all consistent with the cfg-driven
writes. Hence the measurement and the code agree; only the bit-9 conclusion was wrong.

---

## 6. What this means for Linux

Facts (all verified above), in the order the reference does them, then what they imply.
**Everything in §6.2 is inference**, flagged as such.

### 6.1 The reference's DSI/bridge order, in one list

1. Full DSI setup (`0x179DC`): `DSI_CLK_CTRL`, PLL (CFG/GO/STATUS), DSI pad mux, ComplexIO,
   lane config (`COMPLEXIO_CFG1`), `DSI_CTRL` params, TX/RX FIFO VC sizes, OCP, video-port
   config, reset, `VC_CTRL(1)` (**LP, VP→L4**), video/DDR/TA timings.
2. `DSS_CONTROL` ← `0x16610`.
3. Bridge RESETN pulse: `GPIO2_24` **low 20 ms → high 20 ms** (`0x192D0`…`0x19340`), or
   `GPIO4_6` on the alternate branch.
4. `0x17094` `L1DSS_DSI1_lp_mode_start_DSI_QNX`:
   FIFO VC sizes → `VC_CTRL(1)` **LP/L4/VC_EN** → `VC_CTRL(0)` **HS** →
   `DSI_CTRL` IF_EN **0** → `COMPLEXIO_CFG1` rewrite → `VC_CTRL(0)` re-config →
   `DSI_CTRL` IF_EN **1** → poll `TIMING1`.bit15 →
   **null packet on VC1**, **null packet on VC0**, wait PACKET_SENT on each.
5. `0x196DC` self-test (optional): drives bridge `0x520`/`0x524` and reads GPIO2_4 / GPIO6_11.
6. `0x13E50`: 32 bridge register writes on **DSI hw VC1, DT 0x29, packet VC 0, 6-byte
   payload**.
7. `0x16BD8` `L1DSS_DSI1_hs_mode_start_DSI_QNX`: `VC_CTRL(0) = 0x20800791`, IF_EN 1 — the
   video pipe starts **last**.

### 6.2 Consequences to test on Linux (inference)

* **The command VC must be LP, `VC_EN = 1`, `SOURCE = L4`.** `MODE_SPEED = 1` on VC1 is not
  what the reference does (and `SOURCE = VP` on VC1 would disconnect the L4 FIFO that carries
  the packets). The Linux side should force `DSI_VC_CTRL(1)` to a value with bit 9 = 0, bit 1
  = 0 and bit 0 = 1 immediately before the bridge sequence.
* **`DSI_VC_TE` is never used** by the reference. If the Linux side enables TE
  (`VC_TE` bits) on VC1, the channel waits for a tearing-effect edge that this panel does not
  produce — that alone would show "FIFO written, IRQ PACKET_SENT-or-not" asymmetry. Check
  `DSI_VC_TE(1) == 0`.
* **Send the null packet.** The reference explicitly writes
  `VC_SHORT_PACKET_HEADER(1) = 0` (and `(0) = 0`) and waits for PACKET_SENT before the first
  bridge write. If Linux skips this, the clock lane may stay in LP and the bridge's DSI
  receiver may never sample the LP data — consistent with "OMAP says sent, bridge does
  nothing" **if** the bridge is configured for HS-clock operation. This is the single
  cheapest thing to try next.
* **Program `DSI_TX/RX_FIFO_VC_SIZE` (0x70/0x74) at least once** with plausible per-VC
  sizes and rewrite them right before the sequence (the reference does it twice).
* **`DSI_COMPLEXIO_CFG1` lane map** must match the reference (`0x2A054321` measured;
  fields `2:0 = 1`, `6:4 = 2`, `10:8 = 3`, `18:16 = 5`). A wrong lane order still "transmits"
  from the OMAP's point of view.
* **The packet VC field must stay 0** (DT `0x29`, not `0x69`): the hardware VC is 1 but the
  DSI packet's virtual-channel field is 0. That is what the reference does and what Linux is
  reportedly already doing — keep it.
* **The reset pulse must precede the DSI writes by ≥20 ms and be released ≥20 ms before the
  first bridge packet** (step 3 vs step 6), and `PWDN`/`RESX` must be released while
  `VC_CTRL(1)` is already enabled — the reference's ordering is: reset low, reset high, then
  LP-mode start, then bridge writes.
* **Video (VC0) last.** The reference does not force `VC_CTRL(0) = 0x20800791` until after the
  bridge configuration, so the bridge never sees a running video stream while it is being
  configured. If Linux starts the video pipeline first, the TC358764 may be re-syncing to a
  pixel stream while it is being written.
* **`DSI_CTRL` bits 19…24 and 3…1 are part of the reference's configuration** (`0x21994`),
  not just bit 0. The reference's own final `DSI_CTRL` is `0x00EAEF9D` (report 127 §4,
  measured) — compare the Linux value field by field, in particular bits 19…24 which
  `0x21994` treats as a group.

### What remains uncertain

1. **Which alias is used at runtime.** The block table entry for `hwid 0xDC` contains
   `0x4804_4000` *in the file*, and nothing in the image writes that table (`R_ARM_RELATIVE`
   entries only), so the DSI helpers address `0x4804_4000`. The *packet sender* uses a
   different base: `0x15E74` calls `disp_mmap_device_memory(0x5800_0000, 0, 0x8000, 0xB00)`
   (`15ebc-15ecc`, PLT slot `0x2B08C`) and stores the result through
   `mem[0x2E5C0]`, which `0x13AE8`/`0x13A70` dereference. Both are aliases of the same
   registers on OMAP4430, but I cannot *prove* statically that the runtime value of table
   entry 55 is `0x4804_4000` rather than a QNX-mapped vaddr; the function `0x1A9E4` calls
   `0x15E74` and could rewrite the table. This does not change any offset in this report.
2. **TRM names for the DSI_CTRL bits 19…24 / 3…1 and the COMPLEXIO_CFG1 bit groups.** I quote
   the QNX field indices (verified) rather than TRM mnemonics, because no OMAP4430 TRM is
   available offline in this workspace and the local kernel tree
   (`kernel/source/`) ships `dsi.c` but not `dsi.h`.
3. **`0x20009D81`'s bits 10, 11 and 15.** `0x21638` never writes them, so I cannot attribute
   them statically. Bit 15 is at least *used* as a live flag (`0x17094` polls it to 0,
   `0x20E40` reads it as `DSI_TIMING1`-style `TA_FT_XSTOP` semantics on `VC_CTRL`? — the two
   functions read different registers, so this is not the same bit).
4. **The exact numeric values** of the VC_CTRL fields that `0x17094` does *not* set
   (bits 31, 30, 29:27, 26:24, 13, 12, 7, 6, 4?, 3, 2). They are inherited from `0x179DC`,
   whose cfg sources are themselves filled from `mem[0x2B148]` (the panel/board descriptor).
   I can prove *who* writes them and *which* path leaves them at 0/1, but not the descriptor
   contents (that is populated at run time from the panel-detect result).
5. **`0x18CE8`, `0x18B6C`, `0x16444`, `0x162EC`, `0x18A7C`** are named nowhere in the image
   and I did not fully decode them; `0x18CE8` calls `0x4FD4/0x4638/0x47F8` and `0x18A7C`
   calls `0x12120/0x11F00/0x11CD0/0x1337C/0x11ACC/0x7BA0/0x78FC/0x7768/0x751C/0x7230`
   (a DISPC / panel-enable cluster). They contain no DSI register writes that I found, but I
   did not prove that exhaustively.
6. **The I²C slave number `0x4A`** is passed as a bare 32-bit value in a QNX `devctl` message;
   whether the driver expects a 7-bit or 8-bit form is not visible statically. Either way it
   is not `0x1C-0x1F`/`0x38-0x3F`, so the "bridge over I²C" question is answered regardless.
7. `0x13E50`'s `0x498` write: I read the immediate pair as `movw 0x1819 / movt 0x061A`
   ⇒ `0x061A1819`; the `movw 0x1819` is at `0x13f5c` and the `movt` at `0x13f60`. If the
   bridge's `0x498` is a byte-lane `PPI_…` register the intended value may be a different
   byte order; report 126 should be treated as authoritative for the bridge register
   *semantics*, this report for the sequence.
8. `0x196DC` was decoded in report 126 and I only re-derived its GPIO side: it sets
   `GPIO2.OE` bit 4 (`orr #0x10` at `0x197B8`, OMAP `OE` bit 1 = input ⇒ **GPIO2_4 becomes an
   input**) and `GPIO6.OE` bit 11 (`orr #0x800` at `0x19874`), then samples `DATAIN`
   (`+0x138`). Its bridge writes are `0x520 ← 5` (`0x16F0`) and `0x524 ← pattern`
   (`0x18C8`), i.e. plain `0x13CE0` DSI writes on VC1 — so the self-test exercises exactly
   the transport the Linux side is failing at.

---

## 7. Evidence index (vaddr → fact)

### Functions identified (name taken from the image's own string tables)

| vaddr | identity | how proven |
|---|---|---|
| `0x17094` | `L1DSS_DSI1_lp_mode_start_DSI_QNX` | `slogf` 5th arg `0x25F7C+0x64` (`0x17108-0x17110`) = `0x25FE0` |
| `0x16BD8` | `L1DSS_DSI1_hs_mode_start_DSI_QNX` | `0x25F7C+0x40` (`0x16C1C-0x16C24`) = `0x25FBC` |
| `0x179DC` | `L1DSS_DSI1_setup_config_QNX` | `0x25F7C+0x88` = `0x26004` |
| `0x16888` | `TAAL_CheckPktSent` | `0x25F7C+0x14` (`0x168BC`) = `0x25F90` |
| `0x21200` | `DSI_VcConf` | `0x27E1C+0x1C4` (`0x212CC`) = `0x27FE0` |
| `0x21638` | `DSI_VcParamsConf` | `0x27E1C+0x1DC` (`0x21670`) = `0x27FF8` |
| `0x21994` | `DSI_ParamsConf` | `0x27E1C+0x204` (`0x219CC`) = `0x28020` |
| `0x1DED4` | `DSI_CIoLaneConf` | `0x27E1C+0x58` (`0x1DF08`) = `0x27E74` |
| `0x1FDD8` | `DSI_Status` | `0x27E1C+0x12C` (`0x1FE10`) = `0x27F48` |
| `0x20E40` | `DSI_TimingTaFTxStop_Read` | `0x27E1C+0x180` (`0x20E74`) = `0x27F9C` |
| `0x21080` | `DSI_PacketFormating` | `0x27E1C+0x1B0` (`0x210CC`) = `0x27FCC` |
| `0x20F10` | `DSI_TimingTaFTxStop` | `0x27E1C+0x19C` = `0x27FB8` |
| `0x1E5A8` | `DSI_ClkConf` | `0x27E1C+0x88` = `0x27EA4` |
| `0x1D398` | `DSI_PllCfg` | `0x27E1C+0x10` = `0x27E2C` |
| `0x1D8EC` | `DSI_PllGo` | `0x27E1C+0x1C` = `0x27E38` |
| `0x1D9E8` | `DSI_PllStatus` | `0x27E1C+0x28` = `0x27E44` |
| `0x1E28C` | `DSI_CIoConf` | `0x27E1C+0x7C` = `0x27E98` |
| `0x1E0CC` | `DSI_CIoConf_Read` | `0x27E1C+0x68` = `0x27E84` |
| `0x1F9F0` | `DSI_Reset` | `0x27E1C+0x10C` = `0x27F28` |
| `0x203AC` | `DSI_DDRTiming` | `0x27E1C+0x14C` = `0x27F68` |
| `0x2062C` | `DSI_VideoModeTiming` | `0x27E1C+0x15C` = `0x27F78` |
| `0x21498` | `DSI_OcpConf` | `0x27E1C+0x1D0` = `0x27FEC` |
| `0x221BC` | `DSI_VideoportConf` | `0x27E1C+0x214` = `0x28030` |
| `0x13CE0` | bridge generic-long-write wrapper (reg, value) | `13d20: mov r0,#1` (vc), `13d24: mov r2,#0x29` (DT), `13d10: mov r3,#6` (len) |
| `0x13AE8` | DSI packet sender | `13b68-13b84` header, `13bdc-13c0c` payload, `13a70` FIFO wait |
| `0x13A70` | per-VC TX-FIFO free-word wait (≥ 7) | `13a88-13ab0`, `DSI_PE+0x84` byte `(vc&3)*8` |
| `0x13E50` | bridge init (32 registers) | 32 × `bl 0x13CE0`, list in §3.4 |
| `0x196DC` | bridge GPIO self-test | report 126; re-verified GPIO side here |
| `0x15F18` | clean `DSI_CTRL` bit-0 (IF_EN) setter — **dead** | only callers `0x16008`/`0x16084` (inside `0x15FBC`) |
| `0x15FBC` | clean MODE_SPEED setter — **dead** | no `bl 0x15fbc` anywhere in the image |
| `0x15E74` | `disp_mmap_device_memory(0x5800_0000, 0, 0x8000, 0xB00)` → `mem[0x2E5C0]` | `15ebc-15ed0`; PLT `0x2B08C` |

### Individual instructions cited

| vaddr | instruction / fact |
|---|---|
| `0x1709C`/`0x170A0` | `ldr r4,[pc,#2300]` + `add r4,pc,r4` ⇒ `r4 = 0x2B000` (PIC anchor) |
| `0x170B4`…`0x170CC` | cfg init: `0x12C=0, 0x137=1, 0x13D=0, 0x13A=0, 0x136=0, 0x130=0` |
| `0x170E0` | `strb r6,[r5,#318]` ⇒ cfg IF_EN field = 0 |
| `0x170DC`, `0x1729C` | `bl 0x21200` (`DSI_VcConf`, 0x70/0x74) |
| `0x170F0`, `0x172B8` | `bl 0x21638` with `r1 = 1` ⇒ `VC_CTRL(1)` |
| `0x17110` | `add r9,r9,#100` ⇒ trace name `0x25FE0 = L1DSS_DSI1_lp_mode_start_DSI_QNX` |
| `0x17128`-`0x17168` | read `[table[0xDC/4] + 0x120]`, `tst #1` |
| `0x171E4`-`0x17228` | read `+0x120`, `tst #0x8000` |
| `0x17274`-`0x1728C` | `if (*arg == 1) mem[0x2ED64+0x10] = 2` |
| `0x173A8`-`0x173C8` | `arg->[4]` selects `state[0x130] = 0` or `3`; `state[0x136] = 1` |
| `0x173D4`/`0x173E4` | `mov r1,#0` + `bl 0x21638` ⇒ `VC_CTRL(0)` |
| `0x174B4`/`0x174C0` | `state[0xF9] = 0` + `bl 0x21994` ⇒ `DSI_CTRL` IF_EN = 0 |
| `0x17590`/`0x1759C` | `state[0x1C4] = 1` + `bl 0x1DED4` ⇒ `COMPLEXIO_CFG1` |
| `0x17608`-`0x17634` | `state[0x136] = 1`, `state[0x121] = 0`, 2 × `bl 0x21638` (VC0) |
| `0x17700`/`0x17710` | `state[0xF9] = 1` + `bl 0x21994` ⇒ `DSI_CTRL` IF_EN = 1 |
| `0x1788C` | `str r2,[r3]` with `r3 = base+0x130`, `r2 = 0` ⇒ `SPH(1) = 0` |
| `0x178A0` | `bl 0x16888` (`(55, 1, 0x1E8480)`), `17894: mov r1,#1`, `17898/1789C: movw/movt 0x1E8480` |
| `0x17914` | `str r1,[r3]` with `r3 = base+0x110`, `r1 = 0` ⇒ `SPH(0) = 0` |
| `0x17924` | `bl 0x16888` (`(55, 0, 0x1E8480)`) |
| `0x16BF8`/`0x16C04` | IF_EN = 0 (`VC_CTRL` untouched) |
| `0x16CD4`/`0x16CE0` | `VC_CTRL(0)` rewritten, VC_EN = 0 |
| `0x16DFC` | `strb r3,[r2,#317]` ⇒ `VC_CTRL` bit 1 (SOURCE) = `arg->[0]` |
| `0x16E10` | `bl 0x21638` with `r1 = 0` |
| `0x16E48`/`0x16E5C`, `0x16E88`/`0x16E9C` | `obj[0x20] = 0xEC00003E|(arg[0x24]<<8)` / `0xEC00000E|…` + `bl 0x21080` ⇒ `LONG_PACKET_HEADER(0)` |
| `0x16EF4`-`0x16EFC` | `movw r3,#0x791` / `movt r3,#0x2080` / `str r3,[r2]` ⇒ **`VC_CTRL(0) = 0x20800791`** |
| `0x16F0C`/`0x16F18` | IF_EN = 1 |
| `0x19020` entry | reads `mem[0x2B100]` (mode) at `0x1905C`; `0x1C6F8`/`0x1C16C` per mode |
| `0x190E8`/`0x190EC`/`0x190FC`/`0x1910C`/`0x19110` | `0x18CE8` / `0x18B6C` / `0x16444` / `0x162EC` / `0x18A7C` |
| `0x1911C` | `bl 0x179DC` (`L1DSS_DSI1_setup_config_QNX`) |
| `0x19124` | `bl 0x16610` (`DSS_CONTROL` @ `0x4804_0040`) |
| `0x1919C`/`0x1927C` | branch on `mem[0x2B12C]` |
| `0x1920C`/`0x19270` | `*(hwid 0x128 + 0x190/0x194) = 0x40` + `usleep(20000)` (GPIO4_6 variant) |
| `0x192D0`-`0x192D4` | `*(hwid 0x118 + 0x190) = 0x01000000` ⇒ `GPIO2_CLEARDATAOUT` bit 24 |
| `0x192D8`-`0x192DC` | `movw r0,#20000` + `bl 0x40F0` (`usleep`) = 20 ms |
| `0x19330`-`0x19340` | `*(hwid 0x118 + 0x194) = 0x01000000` ⇒ `GPIO2_SETDATAOUT` bit 24, then 20 ms |
| `0x19344`-`0x1934C` | `bl 0x17094` |
| `0x1935C`-`0x19388` | `0x196DC` + optional `exit(r0+2)` (`bl 0x4330`) |
| `0x1938C` | `bl 0x13E50` |
| `0x19398` | `bl 0x16BD8` |
| `0x197B4`-`0x197BC` | `orr r3,r3,#16`; `str` ⇒ `GPIO2.OE` bit 4 (input) |
| `0x19870`-`0x19878` | `orr r3,r3,#2048`; `str` ⇒ `GPIO6.OE` bit 11 |
| `0x19C38` loop | 4 patterns to bridge `0x524`, 10 µs each, `DATAIN` (`+0x138`) sampled |
| `0x15F18` | `ldr r1,[r3,#64]!` / `orr|bic #1` / `str` / poll `tst #1` ×100 with `nanospin_ns` |
| `0x15FBC` | MODE_SPEED setter (clear VC_EN → IF_EN 0 → set/clear bit 9 → VC_EN → `TIMING1 |= 0x8000` → IF_EN 1); **no callers** |
| `0x2134C`/`0x21478` | `DSI_TX/RX_FIFO_VC_SIZE` (0x70/0x74) stores |
| `0x218A0` | `str r4,[r7]` — the single `VC_CTRL(vc)` store in `0x21638` |
| `0x219A0`-`0x2219C` | the ten `DSI_CTRL` bit RMWs (masks `0x1000000, 0x800000, 0x400000, 0x200000, 0x100000, 0x80000, 8, 4, 2, 1`) |
| `0x1E0AC` | `DSI_COMPLEXIO_CFG1` store |
| `0x16908`-`0x1690C` | `tst r3,#4` on `VC_IRQSTATUS(vc)` (PACKET_SENT) |
| `0x13B50`-`0x13B58` | `tst r1,#0x10000` on `VC_CTRL(vc)` ⇒ return −1 (busy) |
| `0x13B68`-`0x13B84` | `LONG_PACKET_HEADER(vc)` = `(hdr<<24)|(len<<8)|dt` |
| `0x13C0C`/`0x13CC8` | payload word/tail stores to `LONG_PACKET_PAYLOAD(vc)` |

### External calls (PLT slot → symbol)

`0x40F0 → 0x2B028 usleep` · `0x40A8 → 0x2B010 printf` · `0x4120 → 0x2B038 puts` ·
`0x424C → 0x2B09C slogf` · `0x4204 → 0x2B084 devctl` · `0x421C → 0x2B08C
disp_mmap_device_memory` · `0x4348 → 0x2B0F0 open` · `0x436C → 0x2B0FC close` ·
`0x41A4 → 0x2B064 calloc` · `0x41C8 → 0x2B070 nanospin_ns` · `0x4330 → 0x2B0E8 exit` ·
`0x42DC → 0x2B0CC ThreadCtl`.
