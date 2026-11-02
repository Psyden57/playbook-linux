# 65 — Live PlayBook dump: SAR monitor window, IRAM, and the secure-hole boundary

Status: live-device follow-up to `30`, `62` and `64`.
Date: 2026-10-20 (device time; host UTC).
Target device: BlackBerry PlayBook, OMAP4430 Winchester ES2.2 HS, QNX 6.6
(`uname -a`: `OMAP4430_Winchester_ES2.2_HS_PVT4_Rev:07 armle`).

This document corrects the earlier "no secure/JTAG path" conclusion for the
specific case of a **rooted, booted PlayBook**. There is still no JTAG, but
root SSH plus the RIM monitor's NS-readable SAR window gives real live
evidence. The hidden ROM page remains firewalled.

## 1. Access path actually used

1. Host saw the PlayBook RNDIS/CDC device as `0fca:8010` ("Connect to Windows
   mode"). The host-side interface `enp8s0f3u1` had no IPv4 address; a
   NetworkManager profile was created with `169.254.0.2/16`, after which
   `169.254.0.1` was reachable and the qconn door on TCP **4455** was open.
2. `BerryShell-V4.py` performed the qconn pairing/handshake and uploaded the
   workspace SSH key (`/home/psyden/rea-work/rsa`). It was then run in `hold`
   mode to keep the door open with 4 s heartbeats.
3. Plain `ssh -i /home/psyden/rea-work/rsa root@169.254.0.1 ...` worked.
4. `memdump3` (the QNX `mmap_device_memory` tool, source in the workspace) was
   copied to `/tmp/memdump3` and run pinned to CPU0 with `on -C 0`.

All reads below are **read-only**. No register, flash, NVRAM, RPMB or monitor
state was written. The tool uses `PROT_READ | PROT_NOCACHE |
MAP_SHARED | MAP_PHYS`; a bad address produces a **contained SIGBUS** in the
tool, not a kernel crash.

## 2. Raw artifacts

In `analysis/playbook-live-2026-10-20/`:

| file | content |
|---|---|
| `sar-monitor-4A326A00.txt` / `.bin` | `0x4A326A00`-`0x4A326CCF`, 0x2D0 bytes, NS-readable SAR/monitor window |
| `iram-4030D000.txt` / `.bin` | `0x4030D000`-`0x4030DFFF`, 0x1000 bytes, live IRAM static-variable/vector area |
| `prehole.txt` | `0x40026F00`-`0x40026FFF` (pre-hole + first 0xD8 bytes of the hole) |
| `hole-start.txt` | `0x40026F28`-`0x40026FFF` readback before SIGBUS |
| `hole-page.txt` | `0x40027000` probe: SIGBUS, no words |
| `hole-end.txt` | `0x40027F00` probe: SIGBUS |
| `secure-hole-40026F28.txt` | single-word probe at `0x40026F28` |
| `secure-hole-alias-00026F28.txt` | single-word probe at `0x00026F28`: SIGBUS |
| `ring3-94000000.txt` | `0x94000000` monitor UART ring window (not text in this state) |
| `qnx-iram-vectors.txt` | `0x800101B4` physical readback (zeros; the IRAM targets are VAs) |

The memdump text prints each u32 as a numeric word; the `.bin` files were
reconstructed with `struct.pack('<I', word)` (the docs' "reverse each 4-byte
group").

## 3. The secure hole is not uniformly 4 KB

Observation (live, NS, QNX 6.6):

```
0x40026F00-0x40026F27   readable; matches bootrom-lower-0x40020000.bin
0x40026F28-0x40026FFF   readable; every word reads 0x00000000 (0xD8 bytes)
0x40027000-0x40027FFF   SIGBUS on the first word (the 4 KB page is firewalled)
0x00026F28              SIGBUS (the low boot alias is not mapped)
```

The earlier statement "the ~4 KB hole `0x40026F28`-`0x40027FFF` is
non-readable" is therefore too coarse. The **first 0xD8 bytes** are readable
and zero; the **4 KB page at `0x40027000`** is the protected region. This is
the page that contains the SMC dispatcher/secure entry; it remains unreadable
from NS.

The `0x40026F28` zero read is not evidence that the ROM contains zero there:
it is the readable tail of the preceding page. The protected page begins at
`0x40027000`.

## 4. SAR/monitor window `0x4A326A00`-`0x4A326CCF`

This is the NS-readable portion of PRM_MPU/SAR RAM. It is the CPU1 warm-reset
trampoline plus its context table, not the hidden SMC dispatcher.

### 4.1 Layout

| range | content |
|---|---|
| `0x4A326A00`-`0x4A326AFF` | high-entropy monitor/SAR data; `0x4A326A04` and `0x4A326A08` point to `0x4A326B00`; `0x4A326A0C` = `0x8B872211` |
| `0x4A326B00`-`0x4A326C27` | ARM code: I-cache invalidate, monitor calls, MIDR-based table select, full CP15/context restore |
| `0x4A326C28`-`0x4A326C4F` | trampoline constants: `0x4A326C28 = 1`, `0x4A326C2C = 0x810`, `0x4A326C3C = 1`, `0x4A326C40 = 0` |
| `0x4A326C50`-`0x4A326CCF` | CPU1 context table (CP15 values, per-mode SP/LR, GPRs) |
| `0x4A326D50` | CPU0 context table (outside the NS-readable window) |

### 4.2 Monitor ABI observed in the trampoline

The trampoline calls the monitor with **`r0` = service ID**, not `r12`:

```
4a326b00  mov  r1,#0
4a326b04  mcr  p15,0,r1,c7,c5,0     ; ICIALLU
4a326b08  ldr  r0,[pc,#0x11c]       ; = 0x4A326C2C = 0x810
4a326b0c  cmp  r0,#0
4a326b10  beq  0x4a326b3c
4a326b14  mov  r0,#0x26             ; service 0x26
4a326b18  mov  r1,#0
4a326b1c  mov  r2,#4
4a326b20  add  r3,pc,#0x100         ; = 0x4A326C28 (value 1)
4a326b24  mov  r6,#0xff
4a326b28  mov  r12,#0
4a326b2c  mcr  p15,0,r12,c7,c5,6    ; BPIALL
4a326b30  dsb  sy
4a326b34  dmb  sy
4a326b38  smc  #0
...
4a326b3c  ldr  r0,[pc,#0xfc]        ; = 0x4A326C40 = 0
4a326b40  cmp  r0,#0
4a326b44  beq  0x4a326b70           ; 0x27 is SKIPPED in this live state
4a326b48  mov  r0,#0x27
...
4a326b6c  smc  #0
```

In this live state the `0x26` guard (`0x4A326C2C`) is `0x810` (nonzero), so
**service 0x26 is called**; the `0x27` guard (`0x4A326C40`) is `0`, so
**service 0x27 is skipped**. Both calls, when taken, pass `r1=0`, `r2=4`,
`r3` = pointer to a 4-byte value (`1`), `r6=0xFF`, `r12=0`, and `SMC #0`.

This is direct evidence that the RIM monitor's ABI is **r0-based** for these
services, while the public TI ROM's HLOS services use `r12`. The hidden
dispatcher at `0x40027000` is what interprets the service ID; it remains
unreadable.

### 4.3 Context restore

After the monitor calls, the trampoline reads MIDR (`mrc p15,0,r1,c0,c0,5`),
tests `MIDR[3:0]`, and with the low bits nonzero (CPU1) selects the table at
`0x4A326C50`. It restores, in order:

```
CPSR mode 0x12 (IRQ)  -> SP, LR
CPSR mode 0x13 (SVC)  -> SP, LR
CPSR mode 0x17 (ABT)  -> SP, LR
CPSR mode 0x1B (UND)  -> SP, LR
CPSR mode 0x1F (SYS)  -> r4-r7
  mcr p15,0,r4,c1,c0,2      ; ACTLR
  mcr p15,0,r5,c2,c0,0      ; TTBR0
  mcr p15,0,r6,c2,c0,1      ; TTBR1
  mcr p15,0,r7,c2,c0,2      ; TTBCR
  mcr p15,0,r4,c3,c0,0      ; DACR
  mcr p15,0,r5,c10,c2,0     ; PRRR
  mcr p15,0,r6,c10,c2,1     ; NMRR
  mcr p15,0,r4,c13,c0,1     ; FCSE PID
  mcr p15,0,r5,c13,c0,2     ; CONTEXTIDR
  mcr p15,0,r6,c13,c0,3     ; TPIDRURW
  mcr p15,0,r7,c13,c0,4     ; TPIDRURO
  mrc p15,0,r4,c12,c0,0     ; read VBAR
  mcr p15,0,r2,c7,c5,6      ; BPIALL
  mcr p15,0,r2,c8,c5,0      ; TLBIALL
  mcr p15,0,r2,c8,c6,0      ; BPIALLIS
  mcr p15,0,r4,c1,c0,0      ; SCTLR last
  ldm ip,{r4-r11,sp,pc}     ; resume
```

This is the "monitor-verified context restore" the kexec work already relies
on. The live dump confirms the exact register order and the `0x26`/`0x27`
guard values; it also confirms that the CPU1 context cannot be forged from NS.

## 5. Live IRAM `0x4030D000`-`0x4030DFFF`

The ROM's IRAM static-variable area is still present but has been reused by
QNX. Key observations:

| address | live value | meaning |
|---|---|---|
| `0x4030D000`-`0x4030D01F` | 8 × `E59FF018` | the ROM's copied IRAM vector table (`ldr pc,[pc,#0x18]`) |
| `0x4030D020` | `0x80028090` | QNX-patched reset/handler target |
| `0x4030D024`-`0x4030D03C` | `0x800101B4`, `0x800101C0`, `0x800101CC`, `0x800101D8`, `0x800101E4`, `0x800101F0`, `0x800101FC` | QNX-patched exception/IRQ/FIQ targets (virtual addresses) |
| `0x4030D040` | `0x0000001F` | ROM progress bitmap word (bits 0-4 set) |
| `0x4030D060` | `0x4030D040` | ring control block pointer |
| `0x4030D064` | `0x4030D04C` | ring node B |
| `0x4030D108` | `0x000032C0` | USB template prefix bytes `C0 32 00 00` |
| `0x4030D10C` | `0x01120001` | USB device descriptor bytes `01 00 12 01` |
| `0x4030D204` | `0xFFFFFFFF` | config-parser "current result" initial value |
| `0x4030D238` / `0x4030D258` | `1` / `1` | media / device-data descriptors |
| `0x4030DC1C` | `0` | the `0xF0` shared descriptor: **never filled** |
| `0x4030DC5C` | `0x0893454D` | first word past the ROM's 0xABC BSS |

Two consequences:

1. **The IRAM vector indirection is live and QNX uses it.** The ROM's second
   vector table was copied to `0x4030D000`, and QNX patched the target words
   to its own `0x8001xxxx` virtual handlers. This is exactly the mechanism
   described in `40-iram-and-tables.md`.
2. **The `0xF0` descriptor is zero in the running OS.** The public ROM's
   `0x4030DC1C` request object, which `0x4002CE90`/`0x4002CE36` dispatch, has
   `descriptor[0] = 0` after boot. This supports the prior classification of
   `0xF0` as a boot-time registration/hand-off: QNX does not use it, and the
   secure side never filled it on this boot.

The QNX vector targets are virtual addresses; a physical read at
`0x800101B4` returns zeros, so they cannot be disassembled through
`memdump3` without QNX's translation. That is expected and not a fault.

## 6. What is settled, and what is still blocked

**Settled by live evidence:**

- Root access is possible without JTAG: RNDIS link-local + qconn door 4455 +
  BerryShell key upload + SSH root.
- The SAR window `0x4A326A00`-`0x4A326CCF` is NS-readable and contains the
  CPU1 trampoline and context table.
- The RIM monitor ABI for the SAR services is `r0`=service, `r6=0xFF`,
  `r12=0`, `SMC #0`; services `0x26`/`0x27` take a 4-byte pointer argument.
- In the current live state, `0x26` is called and `0x27` is skipped.
- The secure hole is `0xD8` readable-zero bytes at `0x40026F28`-`0x40026FFF`
  followed by a firewalled 4 KB page at `0x40027000`-`0x40027FFF`.
- The IRAM vector table is live and QNX-patched; the `0xF0` descriptor is zero.

**Still blocked:**

- The hidden page `0x40027000`-`0x40027FFF` (the SMC dispatcher/secure entry)
  is not readable from NS. The low alias is not mapped.
- The secure-side effect of `0xF0`/`0xFE`/`0xF1` and the `0xF0` descriptor
  fields `+4/+8/+0xC/+0x10/+0x14/+0x18` therefore remain unobserved. The
  structure in `62-monitor-services.md` stands; no new claim about
  verification/decryption/execution is made.
- The ramloader is not a substitute for a secure read: the boot ROM is HS and
  verifies the first-stage loader, `CREAD` (`$E4`/`$E5`) is RSA-gated, and
  the loader exposes no arbitrary RAM/ROM read. `$B7 HASH_BOOTROM` could give
  a hash of the boot ROM but not its bytes.

## 7. Safe next probes (if a ramloader session is available)

1. `$B7 HASH_BOOTROM` / `$B0 bugdisp log` — confirm whether the hidden page is
   covered by the boot-ROM hash and whether the log names the secure entry.
2. `$B4`/`$B5` flash-region info — useful for the Linux/boot-chain work, not
   for the hidden ROM.
3. Do **not** use unknown SMC IDs (`0xF0`, `0xFE`, `0xF1`, or undocumented
   `0x00`-`0x2D`) without a recovery plan. The known `0x100`-`0x113` L2/SCU
   services and `0x26`/`0x27` are the only ones with a live safety record.

## 8. Operational note

The qconn door is held by `BerryShell-V4.py hold` and must be stopped after a
session; plain SSH/scp stop working when the door closes. This live session
performed no writes and left the device running normally.
