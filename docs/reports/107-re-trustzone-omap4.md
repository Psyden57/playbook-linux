# 107 — Static RE of `trustzone-omap4` (TabletOS `/proc/boot/trustzone-omap4`)

Target: `/home/psyden/rea-work/playbook-dev/dumped4869ifs/proc/boot/trustzone-omap4`
ELF32 ARM EABI5 DYN, stripped, ~61 KB, `NEEDED libhuapi.so.5, libc.so.3`,
`INTERP /usr/lib/ldqnx.so.2`. Read-only analysis; nothing was executed.

Method: `readelf -l/-r --use-dynamic`, `rizin` (imports + the surviving local
symbol names in `.dynsym`), full-text disassembly with
`arm-linux-objdump -D -b binary -m arm` (file offset == vaddr for the R-X
segment), manual data-flow over the SMC path, and address/bit cross-checks
against `swpu231ap.pdf.txt` (OMAP4430 TRM).

---

## 0. Answer to the question that was asked

**This driver does not implement, arm, or service anything that runs on a
timer, and it never talks to the secure world during startup.** It performs
**zero SMC calls** in `main()`/`TrustZoneInit()`; the image contains exactly
one SMC instruction (`SMC #1` at `0x7C4C`) and its single caller is reached only
from the `devctl()` handler, i.e. on demand from a root client that wants the
device (BSIS) key. There is no heartbeat, no periodic
servicing, no secure timer, no watchdog, no FIQ/IRQ setup, no GIC programming,
and no TZPC/TZASC/SMCC/"SECRAM" access anywhere in the image.

The only persistent state it leaves on the SoC after startup is **two PRCM
register writes**, and both are *power/clock plumbing for the L4SEC crypto
domain*, not a security handshake:

| register | write | meaning |
|---|---|---|
| `0x4A00_9580` `CM_L4SEC_CLKSTCTRL` | `CLKTRCTRL (bits1:0) = 0b11` (RMW `|=3`) | L4SEC domain → `HW_AUTO` (hardware-supervised), i.e. *stop forcing it awake / let it auto-idle*; TRM reset value is already `0x3` |
| `0x4A00_4304` `CM_MPU_STATICDEP` | `bit14 (L4SEC_STATDEP) = 1` (RMW `|=0x4000`) | statically couple the Cortex-A9 clock domain to the L4SEC clock domain (TRM default = **0/Disabled**) |

Everything else it does at startup is QNX resource-manager boilerplate plus
two mappings. So: **if the secure world of this unit requires a periodic
handshake or a fixed-time service from the non-secure OS, that requirement is
not expressed by this binary** — TabletOS itself never performs one, and
TabletOS runs indefinitely on the same silicon.

That is a useful *negative* result: it removes `trustzone-omap4` from the list
of suspected causes of the ~1.40–1.53 s halt (report 105), and it narrows what a
non-secure Linux can be expected to "satisfy" to exactly the two PRCM writes
above. See §7 for the ranked hypotheses and §8 for the experiments that can
still put the secure world on trial — including a direct SMC probe using the
ABI recovered in §3.

---

## 1. Provenance / self-description

QNX `QNX_info` section (`0xDE4C`):

```
NAME=trustzone-omap4
DESCRIPTION=TrustZone resmgr for Playbook, Colt and any QNX device
DATE=2011/10/21-14:08:32-EDT
STATE=internal
HOST=gusbuild3   USER=builder   VERSION=6.5.0   TAGID=292
```

"Colt" is the second RIM QNX device of that generation — so this is a
generic-QNX RIM component, not PlayBook-specific firmware. Build date
2011-10-21 puts it in the TabletOS 2.0 era.

Imports of interest (`iz`): `mmap_device_io`, `mmap`, `mem_offset64`,
`ThreadCtl`, `dispatch_*`, `resmgr_attach`, `iofunc_*`, `procmgr_daemon`,
`setuid/setgid`, `pthread_*` (create/attr/join), `MsgSendv`, `open/read/write`,
`libhuapi` (`hu_GlobalCtxCreateDefault`, `hu_SHA512*`, `hu_UtilDigestMsg`,
`hu_YieldCreate`), `sb_malloc/free`.
**There is not a single timer, delay, nanosleep, TimerTimeout, interrupt-attach
or signal import in the image.**

Surviving local symbols (`.dynsym` kept locals): `main`, `wkupCtrlVirtBase`,
`target_uid`, `target_gid`, `rattr`, `ioattr`, `io_funcs`, `connect_funcs`,
`optd`, `optv`, `dpp`, `ctp`, `SHA512_IV`, `husw_SHA512*`, `secp521r1_b`,
`secp521r1_ma`, `fpPlus521a`, `fpmod521a`, `fp_Sub521a`, `midG`, `mid273G`.

---

## 2. Startup sequencing (goal 1)

`main()` @ `0x8C84`, `argc/argv` in r0/r1:

| addr | action |
|---|---|
| `0x8C9C` | `ThreadCtl(_NTO_TCTL_IO = 1, 0)` — acquire I/O privilege; on failure: `"TrustZone resmgr: can't acquire IO privilege, err %d"` (`0xC2F4`) → exit 1 |
| `0x8CCC` | `ThreadCtl(_NTO_TCTL_RUNMASK = 6, {1})` — pin to CPU0; failure `"can't set runmask"` (`0xC32C`) |
| `0x8D14` | `parse_args()` @ `0x8828` — `getopt("vU:d")`: `-v`→`optv`, `-d`→`optd` (suppress daemonising), `-U uid:gid`→`target_uid`/`target_gid` (boot script uses `-U99:102`); errors `"invalid uid in -U: %d"` / `"...gid..."` |
| `0x8D18` | `dispatch_create()` → `dpp->dispatch`; failure `"couldn't dispatch_create"` |
| `0x8D90` | fill `resmgr_attr_t rattr`: `flags=0`, `nparts_max=1`, `msg_max_size=0x24` |
| `0x8DB0` | `iofunc_func_init(_RESMGR_CONNECT_NFUNCS=8, connect_funcs, _RESMGR_IO_NFUNCS=0x1D, io_funcs)` |
| `0x8DC4` | `io_funcs.devctl = 0x89DC` (the **only** non-default handler; no read/write/io_write) |
| `0x8DD8` | `iofunc_attr_init(ioattr, 0x81C8, 0, 0)` → char device, mode `0710` |
| `0x8E18` | `resmgr_attach(dpp, &rattr, "/dev/trustzone", _FTYPE_ANY=0, flags=0x200, &connect_funcs, &io_funcs, &ioattr)` |
| `0x8E60` | `setgid(target_gid)`; failure `"can't set target GID"` |
| `0x8E90` | `TrustZoneInit()` @ `0x97EC`; returns 0 → `"TrustZone resmgr: TrustZoneInit failed"`, exit 1 |
| `0x8EB4` | `drop_root_uid()` @ `0x979C` — `getuid() != target_uid ? setuid(target_uid)`; failure `"can't drop root UID"` |
| `0x8ED8` | `dispatch_context_alloc()` |
| `0x8EF4` | if `!optd` → `procmgr_daemon(0, 6)`; failure `"unable to background"` |
| `0x8F4C` | `for(;;) { ctp = dispatch_block(dpp); dispatch_handler(dpp, ctp); }` |

**No thread is created at startup** (`pthread_create` appears exactly once, in
the on-demand ECDH path, §5.2).

### 2.1 `TrustZoneInit()` @ `0x97EC` (the interesting part)

```
0x97FC  hu_GlobalCtxCreateDefault(...)            ; libhuapi software ctx (pure SW)
0x9808  cfg_l4sec_crypto_domain();                ; 0x7C58  (see below)
0x9814  p_ocm = mmap(0, 0x1000, PROT_R|W|NOCACHE /*0xB00*/,
                     flags 0x00010001, NOFD, 0x40300000)   ; phys, shared
        *(u32*)0xEDE8 = p_ocm
0x9864  p_dma = mmap(0, 0x1000, 0xB00, flags 0x00090002,
                     NOFD, 0)                     ; phys-contiguous NC anon buf
        *(u32*)0xEDC8 = p_dma
0x98B4  mem_offset64(p_dma, NOFD, &phys64, 0)     ; → *(u64*)0xEDD0 = phys(p_dma)
0x98F4  *(u32*)0xEDE0 = *(u32*)0xEDC8             ; ctx->smc_buf = p_dma
        return 1 (success) / 0 (failure)
```

`0x7C94` (called first from `TrustZoneInit`) does the three MMIO maps:

```
mmap_device_io(0x800, 0x4A300000) -> [0xEDC4] "wkupCtrlVirtBase"   ; L4_WKUP AP window
mmap_device_io(0x1000,0x4A009000) -> [0xEDBC]                      ; CM2 window (L4PER_CM2 @+0x400)
mmap_device_io(0x100, 0x4A004300) -> [0xEDC0]                      ; MPU_CM1 instance
```

`0x7C58` then performs the two PRCM writes that are this driver's entire
startup hardware footprint:

```
A = *(u32*)0xEDBC                ; 0x4A009000 mapping
A[0x580] |= 3                    ; 0x4A009580 CM_L4SEC_CLKSTCTRL.CLKTRCTRL = HW_AUTO(3)
B = *(u32*)0xEDC0                ; 0x4A004300 mapping
B[0x004] |= 0x4000               ; 0x4A004304 CM_MPU_STATICDEP[14] L4SEC_STATDEP = 1
```

Address/bit verification (TRM `swpu231ap`):
* `0x4A00 9580` = `CM_L4SEC_CLKSTCTRL`, instance `L4PER_CM2` (TRM Table 3-1409).
  `CLKTRCTRL[1:0]`: `0`=NO_SLEEP, `1`=SW_SLEEP, `2`=SW_WKUP, `3`=HW_AUTO, reset `0x3`.
  ⇒ the write is "release the domain to hardware auto-transition", i.e. it
  *removes* a software force-on that an earlier boot stage left behind.
* `0x4A00 4304` = `CM_MPU_STATICDEP`, instance `MPU_CM1` (TRM Table 3-1034/3-133);
  bit 14 = `L4SEC_STATDEP` "Static dependency towards L4SEC clock domain",
  **default 0 (Disabled)** (TRM line 55710 and Table 3-133 "CD_L4_SEC Disabled").

There is no readback, no polling, no waiting on `IDLEST`, and no restore of
either register — they are fire-and-forget.

### 2.2 Data layout of the driver's globals (recovered)

```
0xEDBC  void*  cm2_base        (0x4A009000)          <- L4SEC clock control
0xEDC0  void*  mpu_cm1_base    (0x4A004300)          <- CM_MPU_STATICDEP
0xEDC4  void*  wkupCtrlVirtBase(0x4A300000)          <- L4_WKUP AP window
0xEDC8+0x00 ctx->smc_buf      = p_dma (VA of the 4 KiB NC phys-contiguous SMC descriptor block)
0xEDD0         ctx->smc_buf_phys (u64, from mem_offset64)
0xEDE0         ctx->smc_buf2   (== smc_buf; the word 0x9188() writes into)
0xEDE8         p_ocm           (VA of phys 0x40300000, L3 OCM RAM, 4 KiB window)
0xEDEC target_uid, 0xEDF0 target_gid, 0xEE70 optd, 0xEEC0 optv
0xEDF8 io_funcs, 0xEE98 connect_funcs, 0xEE74 rattr, 0xEEC8 ioattr
```

`0x4030_0000` is **L3 OCM_RAM** (TRM: `0x4030 0000–0x4030 DFFF`, 56 KB, "L3
OCM_RAM"). The driver only uses the *upper* half of its 4 KiB window:

```
0x4030_0000 – 0x4030_0BFF   3 KiB pthread stack (pthread_attr_setstackaddr/size)
0x4030_0C00 – 0x4030_0C8F   KEK output block (128 B ciphertext + 16 B key area padded to 0x90)
                             +0x50 = SHA-512 of the first 66 bytes (integrity tag)
0x4030_0D00 – 0x4030_0DA7   168-byte "BSIS_PRIV" record read from RPMB
```

Nothing at `0x4030_0000+0` is touched, i.e. the driver deliberately avoids the
bottom 3 KiB of OCM (the region the ROM/SecMon boot path uses on this SoC).

---

## 3. The recovered SMC interface (goal 2)

There is **exactly one SMC instruction in the whole binary**:

```
0x7C30  push {r4-r12,lr}
0x7C34  mov  r6, #0xFF          ; dead (macro remnant)
0x7C38  mov  r12, #0            ; dead (macro remnant) -- see note below
0x7C3C  mcr  p15, 0, r0, c7, c5, 6   ; BPIALL  (r0 used as the (ignored) data operand)
0x7C40  dsb  sy
0x7C44  isb  sy
0x7C48  dmb  sy
0x7C4C  .word 0xE1600071        ; SMC #1   (imm4 = 1)
0x7C50  pop  {r4-r12,pc}        ; returns the monitor's r0
```

Single call site: `0x9240`, inside the varargs wrapper `smc_call()` @ `0x9188`.

### 3.1 `smc_call(cmd, a1, a2, nwords, w0..wn)` @ `0x9188`

```
push {r0,r1,r2,r3}                       ; spill the 4 register args
buf = ctx->smc_buf                       ; the 4 KiB NC phys-contiguous buffer
*(u32*)buf = nwords (arg3)               ; word[0] = number of following words
for (i = 1; i <= nwords; i++)            ; copy the varargs
        ((u32*)buf)[i] = va_arg;
phys = virt_to_phys_within_smc_buf(buf)  ; 0x8FFC
if (phys == -1) return 1;                ; failure
l4sec_crypto_clocks_on();                ; 0x7B40
r0 = cmd (arg0); r1 = a1 (arg1); r2 = a2 (arg2); r3 = phys;
smc #1;                                  ; 0x7C30
l4sec_crypto_clocks_off();               ; 0x7BC4
return monitor_r0;                       /* 0 == success */
```

`0x8FFC` (`virt_to_phys`) is a small helper: it verifies that the pointer is in
the same 8 KiB-aligned region as `ctx->smc_buf` (masking bits 0–12), converts
to an offset (`< 0x1000`, else `-1`), and returns `offset + smc_buf_phys`
(falling back to `-1` when the buffer has no physical offset).

**So the calling convention is:**

```
r0 = function id          (0x31, 0x37, or a client-supplied value)
r1 = argument 1
r2 = argument 2           (always 4 at both call sites)
r3 = PHYSICAL address of the descriptor block (32-bit words, word[0] = count)
entry = SMC #1, after BPIALL + DSB/ISB/DMB
return value in r0, 0 == success
```

### 3.2 Recovered function IDs

| id | name (string in binary) | r1 | r2 | descriptor `word[0..n]` | purpose |
|---|---|---|---|---|---|
| `0x31` (49) | "select_kek" | `0` | `4` | `{1, 0}` | select/validate the HW KEK in the secure world |
| `0x37` (55) | "encrypt_with_kek" | block index `i` (0..7) | `4` | `{4, phys_a, 16, phys_b, 16}` | one 16-byte block KEK transform, block `i` of 8 |
| *client* | *(arbitrary)* | client | `4` | `{n, ...}` | raw pass-through, devctl `0xC0285001`, root only |

The matching error strings are `"error: failed to call select_kek"` (0xC4CC),
`"error: secure debug enabled"` (0xC4F0), `"error: failed to choose hw kek"`
(0xC50C), `"Error in encrypt_with_kek"` (0xC52C). All are reachable **only**
from `devctl` — there is no call from `main()`.

### 3.3 Note on `SMC #1` and `r12`

Prior work (report 30, §1.2) established that the PlayBook boot ROM uses the
`SMC` immediate as a *class* selector: `smc #0` with `r12` = service
(`0x107`, `0x103`, `0x0F0`, `0x9`) and `smc #1` with `r12` = cache-op selector
(`0x00`, `0x0FE`, `0x0F1`), always preceded by BPIALL and DSB/ISB/DMB.

This driver's thunk is byte-for-byte the same idiom (`mcr p15,0,r0,c7,c5,6` +
`dsb/isb/dmb` + `smc #1`) but it **does not program `r12`**: at the SMC, `r12`
still holds a leftover stack address from the copy loop in `smc_call()`. It is
therefore *provably not* a selector in this ABI — the secure side must be
dispatching on `r0`. (Equivalently: `SMC #1` is the general monitor-entry
opcode on this platform and the `r12` selector scheme is ROM-internal only.)
This matters for anyone re-implementing the call from Linux: **do not copy the
ROM's `r12` convention for `0x31/0x37`.**

### 3.4 No "secure service channel" registers

There is no SMCC/PPA mailbox, no `SECRAM` window, no `SMC_SET/GET` register
pair. The only channel is the descriptor block in ordinary non-cached RAM whose
**physical** address is passed in `r3`; the monitor reads/writes non-secure RAM
at physical addresses supplied in that block (`{4, phys_a, 16, phys_b, 16}` =
two (addr,len) pairs). Consequences:
* the descriptor block address is dynamic (whatever `mmap` returned), so the
  secure side performs *arbitrary* NS physical accesses — this is not
  TZASC-scoped, so a Linux re-implementation can use any physically contiguous
  NC buffer;
* results are returned *in RAM* at the addresses given in the block, not in
  registers; `r0` only carries status.

---

## 4. Secure/privileged register accesses (goal 3)

Complete inventory. The image contains no other `mmap*`, `open`, `mcr/mrc`
or fixed-address constants (verified by scanning *every* `movw`/`movt`
immediate in `.text`: the only physical addresses present are `0x4A009000`,
`0x4A004300`, `0x4A300000`, `0x40300000`).

| address | name (TRM) | access | where |
|---|---|---|---|
| `0x4A00_9580` | `CM_L4SEC_CLKSTCTRL` (L4PER_CM2, off 0x180) | RMW `\|=3` → `CLKTRCTRL=HW_AUTO` | `0x7C64` init |
| `0x4A00_4304` | `CM_MPU_STATICDEP` (MPU_CM1, off 0x4) | RMW `\|= 0x4000` → bit14 `L4SEC_STATDEP=1` | `0x7C80` init |
| `0x4A00_95A0` | `CM_L4SEC_AES1_CLKCTRL` | MODULEMODE `= 2` (HW_AUTO) before SMC, `= 0` after | `0x7B40`/`0x7BC4` |
| `0x4A00_95A8` | `CM_L4SEC_AES2_CLKCTRL` | idem | idem |
| `0x4A00_95B0` | `CM_L4SEC_DES3DES_CLKCTRL` | idem | idem |
| `0x4A00_95B8` | `CM_L4SEC_PKA_CLKCTRL` | idem | idem |
| `0x4A00_95C0` | `CM_L4SEC_RNG_CLKCTRL` | MODULEMODE **`= 1` (ENABLED, forced)** before SMC, `= 0` after | idem |
| `0x4A00_95C8` | `CM_L4SEC_SHA2MD5_CLKCTRL` | MODULEMODE `= 2` before, `= 0` after | idem |
| `0x4A30_0100` | L4_WKUP AP window (undocumented offset) | **read**, test `bit4` = "secure debug enabled" | `0x9318` |
| `0x4A30_0104` | L4_WKUP AP window (undocumented offset) | **read**, `bits[10:0]`; `== 0x7FF` ⇒ "failed to choose hw kek" | `0x9328` |
| `0x4030_0000` | L3 OCM_RAM | `mmap` phys (R/W, NC) — scratch/key area | `0x9834` |

`0x4A30_0000–0x7FF` is the TRM's *"L4_WKUP configuration — Address/protection
(AP)"* block (Table 13-282/13-284); the TRM explicitly does **not** publish its
register offsets ("computed based on the physical implementation",
Table 13-286). The driver only ever **reads** two words in it (`+0x100`,
`+0x104`) and never writes it — so it does **not** reprogram any firewall,
region or protection group at startup or at any other time.

Bit-level meaning of the two AP reads (inferred from the error strings):
`AP[0x100].bit4 == 1` ⇒ the SoC is in *secure-debug* state and the KEK path is
refused; `AP[0x104].bits[10:0] == 0x7FF` (all-ones = "no valid selector") ⇒ no
hardware KEK is fused/available. Both are read *after* `select_kek` succeeds.

### 4.1 Explicitly **absent** (searched for and not found)

* **TZPC**, **TZASC**, any `SECRAM`/`SEC_CTRL` or `CONTROL.SEC_*`: no reference
  at all (the public TRM does not document TZPC/TZASC at all — the point is
  that the complete `movw`/`movt` immediate scan of the image yields exactly
  four physical addresses, `0x4A009000`, `0x4A004300`, `0x4A300000`,
  `0x40300000`, and the only `mmap*`/`open` call sites are the seven listed).
  Note the driver never even maps `CTRL_CORE` (`0x4A00_2000`).
* **GIC** (`0x4820_1000`/`0x4820_2000`, `GICD_IGROUPR`, `GICD_ISENABLER`,
  `GICC_*`): zero occurrences of `0x4820` in the image.
* **PRM** (`0x4A00_6000`), `CM_WKUP`/`PRM_WKUP` (`0x4A31_C000/E000`),
  `CM_ABE`, `CM_L3INIT`: untouched.
* **Watchdogs**: no `WDTIMER1/2/3` (`0x4A31_8000`, `0x4A31_4000`, `0x4013_C000`)
  reference; `WDT_WSPR`/`WDT_WWPS` sequences absent.
* **Secure timers / 32 kHz / GPTIMER** (`0x4A31_8000`, `0x4832_0000`-family,
  32K sync): no reference.
* **FIQ/IRQ enable**: no `cps`, no `msr`, no `mrc/mcr ... c12/c1`, no `wfi`,
  no `wfe`, no `sev`. The image contains exactly **one** real coprocessor
  instruction (the BPIALL in the SMC thunk).
* **Fixed timeouts**: no `1500`/`0x5DC`, `1000`, `1400`, `1530`, `32768`,
  `0x8000` or ms→ticks arithmetic anywhere in `.text`/`.data` (whole-image
  32-bit word scan). The only large immediates are the secp521r1/SHA-512
  constants and QNX devctl codes.

The bank of `0x7B40`/`0x7BC4` writes is a *clock-gating bracket around the SMC
call*: the crypto engines the secure monitor uses (AES1, AES2, DES3DES, PKA,
RNG, SHA2MD5) are given a MODULEMODE before the call and set back to
`DISABLED (0)` immediately afterwards. Note the asymmetry: **RNG alone is
forced to `ENABLED (1)`**, everything else is only `HW_AUTO (2)` — consistent
with a keyed transform plus a hardware random source. Its only consequence for
a Linux re-implementation is that a probe must set `MODULEMODE` before
`SMC #1` and that leaving them disabled after the call is the shipped
behaviour.

---

## 5. What the driver actually *does* (the functional core)

### 5.1 devctl surface (`/dev/trustzone`, mode `0710`)

`iofunc_devctl_default()` first; unknown dcmds fall through. Two dcmds:

* **`0xC0285001`** — *raw SMC pass-through* (root only).
  Requires `iofunc_check_access(ctp, info, 0x800, 0)`, i.e. read+write
  (root, per `"trustzone devctl needs root privelige"`), and ≥40 bytes of
  transaction payload (`"trustzone devctl msg length less than transaction
  data"`). The client message carries, starting at `msg+0x14`:
  `{u32 funcid, u32 a1, u32 a2, u32 nwords, u32 w[4]}` (result is written back
  to `msg+0x10`). It is forwarded verbatim to `smc_call()`.
  **This is the single most useful handle for probing the monitor from Linux:
  the exact call shape is known (§3.1) and needs no key material as long as
  the client-supplied `funcid` is 0x31/0x37-like.**
* **`0xC0285002`** — *wrap/unwrap + ECDH*.
  Payload ≥56 bytes: `u32 len` at `msg+0x34`, client data at `msg+0x38`
  (needs `len <= srcmsglen-0x38`); reply is 66 bytes (`0x42`) written back to
  `msg+0x38` with `msg+0x34 = 0x42`, and the reply iov is set to
  `(msg, msg+0x6A)`. Strings: `"trustzone transaction payload buffer not big
  enough to get pub key"`, `"failed to calculate ECDH key in devctl trustzone
  resmgr"`, `"... not big enough to return ECDH key"`.

### 5.2 The key path (`0x957C` → `0x9268`)

```
open("/dev/rpmb/BSIS_PRIV")                     ; 0xC548
read(fd, OCM+0xD00, 0xA8)                       ; 168-byte record
kek_unwrap(OCM+0xD00, 168, OCM+0xC00, &len)     ; 0x9268
pthread_create(worker, stack = OCM+0x000, size 0xC00, arg = {...})
   worker: secp521r1 ECDH over huapi (hu_SHA512*, bignum 0x74D4/0x4FF8)
pthread_join
memcpy(reply, OCM+0xC00, 0x42)
```

`kek_unwrap()` @ `0x9268`:

```
if (len != 0xA8 || out == NULL || plen == NULL) return 0;
memset(out, 0, 0x90);
if (smc_call(0x31, 0, 4, 1, 0))            → "error: failed to call select_kek"
if (AP[0x100] & 4)                         → "error: secure debug enabled"
if ((AP[0x104] & 0x7FF) == 0x7FF)          → "error: failed to choose hw kek"
for (i = 0; i < 8; i++)                    /* 8 × 16 B = 128 B */
    phys_a = mem_offset64(out     + 16*i);
    phys_b = mem_offset64(blob+4  + 16*i);
    smc_call(0x37, i, 4, 4, phys_a, 16, phys_b, 16)
                                           → "Error in encrypt_with_kek"
hu_SHA512Begin/Hash/End(0x42 bytes of out) ; compare with out[0x50..0x8F]
*plen = 0x42; return 1;
```

Record layout (derived): `blob[0..3]` = 4-byte header (not KEK-processed),
`blob[4..0x83]` = 128 bytes ciphertext (8 AES-class blocks),
`blob[0x84..0xA7]` = 36 trailing bytes (IV/MAC/uid — untouched by the driver);
plaintext = `out[0..0x41]` = 66-byte **secp521r1 private key** (521-bit scalar
+ pad), `out[0x42..0x4F]` padding, `out[0x50..0x8F]` = SHA-512 of the 66 bytes.
Integrity is checked *after* decryption (SHA-512, not a MAC keyed by the KEK).

So the whole component is a **device-key (BSIS) unwrap + ECDH oracle**: RPMB
record → hardware KEK via 9 SMCs → secp521r1 private key → ECDH with the
caller's public key → 66-byte shared-secret/public-key answer. Nothing in that
path is on any boot-time timer.

---

## 6. Goal 4 answers, item by item

| question | answer from this binary |
|---|---|
| (a) secure timer / watchdog / heartbeat requiring periodic service | **None.** No timer API, no timer peripheral, no periodic thread, no SMC in any periodic loop. The 9 SMC calls happen once per devctl request. |
| (b) FIQ or secure IRQ routed to the non-secure world | **None.** No GIC access, no `SCR`/`CPSR`/`cps`/`msr`, no vector-table manipulation, no `_NTO_INTR_*` attach. |
| (c) "boot complete"/"adopt"/"handshake" SMC the NS OS must perform | **None at boot.** The only SMCs are `0x31 select_kek` and `0x37 encrypt_with_kek`, both purely on demand, both idempotent-looking (no state other than "which KEK" is kept on the NS side). There is no "hello", no "adopt", no "release". |
| (d) fixed timeout programming (1500, 0x5DC, 32768, ms→ticks) | **None anywhere in the image** (whole-image constant scan). |
| (e) secure register blocks (TZPC/TZASC/SMCC/SECRAM/L4 firewall/GIC secure) | **Only** the two PRCM writes in §2.1 plus read-only probes of two undocumented L4_WKUP AP words during the key path. No TZPC/TZASC/SMCC/SECRAM/GIC access at all. |

---

## 7. Hypotheses for the ~1.40–1.53 s halt, and how this binary bears on them

The observed signature (report 105) — *any* NS code, IRQs enabled, tick IRQ
stops, SoC otherwise alive until WDT2 resets at ~55 s — means the A9 stopped
executing or stopped receiving interrupts. This driver constrains the
"secure-world" family of explanations as follows.

### H1 — *"The secure world needs periodic servicing that Linux does not do."*
**Rejected by this evidence.** TabletOS runs indefinitely with this exact
driver, and the driver performs *no* periodic secure work; a requirement it
never satisfies cannot be a requirement. Unless the servicing is done by a
*third* QNX component (none appears in the boot script between
`trustzone-omap4` and the watchdog kicker), there is no NS-side heartbeat.

### H2 — *"The secure world has its own (secure) timer and, at expiry, does
something that freezes the A9."* — **still open, and the best fit.**
Mechanisms that produce *exactly* this signature (CPU frozen, tick gone, SoC
alive, no reset):
1. **Monitor-mode hang.** The A9 is inside the secure monitor (taking a Group-0
   interrupt / FIQ that belongs to secure) and the monitor spins on a resource
   that is not clocked/powered (e.g. L4SEC crypto engines gated, or a DPLL
   that no longer locks), or on a semaphore the NS OS was supposed to release.
   Duration ≈ 1.4 s ⇒ something inside the secure world is periodic.
2. **Forced MPU power-domain transition.** Something with PRCM access forces
   the MPU domain to RET/OFF (or gates `CM_MPU_MPU_CLKCTRL`) while the CPU is
   running. Only the secure world (or a secure watchdog routing) can do that
   asynchronously to a *running* CPU; NS software would have to use WFI.
3. **Secure-only watchdog with a "local" (MPU-domain) reset action** rather
   than a warm SoC reset — e.g. `WDTIMER1` (always-on domain, `0x4A31_8000`),
   which the NS OS may be unable to touch if it is marked secure. QNX only ever
   kicks `WDTIMER2` (`0x4A31_4000`, per report 105) — WDT1's state is unknown
   and *never inspected by anything in TabletOS that we have read so far*.
   A ~1.4 s WDT1 period with a "reset MPU domain only" action would look
   precisely like this (the SoC survives, so WDT2 still fires at 55 s).

### H3 — *"The two PRCM writes QNX makes are the state Linux lacks."*
**New, cheap, and directly testable.** QNX's *only* secure-adjacent startup
state is `CM_MPU_STATICDEP[14] = L4SEC_STATDEP` *enabled* (TRM default: 0) and
`CM_L4SEC_CLKSTCTRL = HW_AUTO`. If Linux's OMAP4 PRM/clockdomain init clears
`CM_MPU_STATICDEP` (or leaves L4SEC in `SW_SLEEP`), the L4SEC clock domain can
idle/gate while the MPU domain is active. A secure monitor that reaches for the
L4SEC crypto engines (which is exactly what this driver's whole design shows it
does: `AES1/AES2/DES3DES/PKA/RNG/SHA2MD5`) would then hang on its first
crypto access — i.e. H2.1 with a concrete, *avoidable* trigger.
This is the only causal chain from this binary to the wall that does not
require the secure world to have a timer of its own.

### H4 — *"The wall is pre-Linux state (`setup-core-inactive`, VC/VP, CPU1
park), not the secure world."*
This binary neither supports nor refutes it, but it removes the main competing
candidate from the "QNX does something Linux doesn't" list, which raises H4's
relative weight (it is already suspect #1 in report 105).

### H5 — *"The secure world is simply absent/unconfigured in our boot path, and
a secure-side exception/hang occurs on the first secure event."*
Testable directly with the ABI in §3 (experiment E1).

---

## 8. Concrete experiments (in priority order)

All addresses are physical; all are safe read/writes except where noted.

**E1 — SMC liveness probe (the decisive test for the whole secure-world
family).** From a `late_initcall`/`arch_initcall` in Linux, at t≈0.3 s, 0.8 s and
1.3 s, issue the driver's own call with a physically contiguous, non-cached
descriptor buffer (e.g. `dma_alloc_coherent(…, 4096)` mapped NC). Wrap it with
a TWD/GPT deadline so a hang is visible as "no breadcrumb after the call":

```c
static int pb_smc1(u32 cmd, u32 a1, u32 a2, phys_addr_t desc)
{
        register u32 r0 asm("r0") = cmd;
        register u32 r1 asm("r1") = a1;
        register u32 r2 asm("r2") = a2;
        register u32 r3 asm("r3") = desc;
        asm volatile("mcr p15,0,%0,c7,c5,6\n dsb sy\n isb sy\n dmb sy\n"
                     "smc #1\n" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3) : "memory");
        return r0;
}

/* probe A: select_kek  -- desc[0]=1, desc[1]=0 */
((u32*)d)[0] = 1; ((u32*)d)[1] = 0;
ret = pb_smc1(0x31, 0, 4, dma_addr);
/* if ret==0 also read 0x4A300100 bit4 and 0x4A300104 bits[10:0] */
```
Interpretation: `ret == 0` ⇒ the monitor is alive and the recovered ABI is
correct (and then the `AP[0x100]/AP[0x104]` reads tell you the secure-debug /
KEK state). A *hang* at 1.3 s but not at 0.3 s would nail H2. A SIGILL/UNDEF or
an abort ⇒ the monitor is not present in our boot path (H5).

**E2 — Replicate QNX's two PRCM writes.** In early boot (before the wall),
`readl(0x4A009580)`/`readl(0x4A004304)`, log both, then force QNX's values
(`|= 3` and `|= 0x4000`) and re-read. If the wall moves/disappears → H3.
Also log what Linux's own PRM/clockdomain init does to these two registers
(compare the value at 0.1 s vs 1.3 s) — a difference from QNX's shipped value
is itself the finding.

**E3 — Catch the transition red-handed.** In the spin test, poll in the *tight
loop* (write into the DEBUG_LL ring on change) these four registers:
`CM_MPU_CLKSTCTRL 0x4A004300`, `CM_MPU_MPU_CLKCTRL 0x4A004320`,
`CM_MPU_STATICDEP 0x4A004304`, `CM_L4SEC_CLKSTCTRL 0x4A009580`, plus
`PM_MPU_PWRSTST`. If `IDLEST`/`CLKTRCTRL`/power-state change exactly at the
wall, the MPU domain was transitioned by *someone else* (H2.2) and the culprit
is not software on CPU0. If they do not change, it is the monitor (H2.1) or the
clock source — then also poll `CM_CLKMODE_DPLL_MPU`/`CM_IDLEST_MPU`.

**E4 — Prove the A9 *clock* is still alive after the wall.** Program a spare
GPTIMER (e.g. `GPTIMER5/6`, `0x4830_4000`/`0x4830_6000`) in one-shot mode just
before the wall, and have the *recovery path* (the DRAM event area is readable
once TabletOS is back, per report 105 §6) read its counter. A counter that kept
running while CPU0 was dead ⇒ the SoC/L3 clocks are fine and only the MPU
domain/monitor is stuck (H2.1/H2.2). A counter that also froze ⇒ PRCM clock
event, not a CPU hang.

**E5 — Interrogate WDTIMER1 (always-on, `0x4A31_8000`).** Before the wall:
`readl(base+0x10/0x14/0x24/0x28/0x2C/0x30)` twice, 100 ms apart. Non-zero
counter/`CRR` and a plausible `LDR` would be a smoking gun if the period is
~1.4 s. Then attempt the `WDT_WSPR` stop sequence (`0xAAAA` then `0x5555`) and
see whether the register accepts writes (i.e. whether it is firewalled as
secure). If it is locked and running, try *rewarding* the question: is WDT1's
reset routed to a domain-local reset (RM_RSTCTRL) rather than a warm reset?
(That is H2.3.)

**E6 — GIC group census.** Early in Linux, dump `GICD_IGROUPR0..31`,
`GICD_ISENABLER`, `GICD_ITARGETSR` and compare against what Linux wrote. A
large Group-0 (secure) set, or SPIs still targeted at CPU0 that Linux never
enabled, supports the "a secure IRQ takes the CPU into the monitor" mechanism
(H2.1). Cheap, read-only, no risk.

**E7 — Differential boot (does QNX's prior execution matter at all?).**
Compare the wall time for (i) cold boot straight into the Linux kernel via
`force_reset_loader.py` and (ii) warm reboot from a fully-booted TabletOS into
the kernel. If both halt at ~1.4 s, no state left behind by QNX (and therefore
nothing this driver does) is causal; if only (ii) halts, the secure world's
state is inherited from the QNX run and §2.1's writes become prime suspects.

**E8 — (destructive, last) Neutered-IFS A/B.** Remove `trustzone-omap4 -U99:102`
from the IFS `.script` and reflash. If TabletOS still boots and runs (the
component is only needed for BSIS/RPMB key operations, which the boot path does
not use), that independently confirms "not a boot-time secure handshake" — and
any change in the Linux wall afterwards would be attributable to the two PRCM
writes, since that is all the component ever leaves behind.

---

## 9. Residual unknowns / caveats

* The secure side is a black box: function IDs `0x31`/`0x37` are known, but not
  the KEK algorithm (the 16-byte block granularity and the six bracketed
  `L4SEC` engines point at AES-128, most likely ECB or CBC with an IV from the
  36 trailing bytes; not proven) nor whether `0x37` is encrypt-only or a
  generic direction-selecting transform.
* `AP[0x100]`/`AP[0x104]` field meanings are inferred from the error strings
  only; the TRM does not document the L4_WKUP AP register map.
* Which of the two `0x8FFC` "phys" arguments of `0x37` is source vs destination
  could not be resolved statically (the monitor may treat them either way).
* `rattr.msg_max_size = 0x24` (36) coexists with a 106-byte devctl payload;
  either the field is something else in this QNX version or the 0x5001/0x5002
  commands rely on `_RESMGR_FLAG_*`/direct-iov behaviour. Not relevant to the
  boot question, flagged for completeness.
* Nothing here says the PlayBook secure world has *no* timer; it only says this
  component neither creates nor services one. E1/E3/E5 are the tests that can
  settle it without reading secure-side code.
