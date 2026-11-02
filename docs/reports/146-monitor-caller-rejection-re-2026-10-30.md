# 146 — Monitor caller rejection (0xFF05): static RE of every SMC call site

Date: 2026-10-30
Status: **complete for all six questions.** Q1, Q3, Q4, Q5 answered from bytes.
Q2 answered negatively (no such constant exists in any image; the meaning is not
recoverable without the monitor image). Q6 answered: nothing found.
Scope: **STATIC ONLY.** No device access, no USB, no `/dev/ttyACM0`, no flashing,
no ssh. Local binutils only (`tools/env.sh`, `arm-linux-objdump`,
`$QNX_HOST/usr/bin/arm-unknown-nto-qnx6.6.0eabi-objdump`) plus a small Python
scanner for instruction encodings.
Only file created: this report.

Evidence markers: **[P]** proven from bytes (offset/address + quoted
disassembly) · **[I]** inferred (reasoning stated) · **[U]** unresolved from the
bytes available.

---

## 0. Method note — how "every smc" was enumerated (and one correction)

`smc` has exactly two encodings on ARMv7-A, verified by assembling with
`arm-linux-as -mcpu=cortex-a9`:

```
   0: f7f0 8000   smc  #0        <- Thumb: halfwords (0xF7F0|imm4, 0x8000)
   4: f7f1 8000   smc  #1
   8: e1600070    smc  0         <- ARM: 0xE1600070|imm4
   c: e1600071    smc  1
```

So the Thumb form is the **32-bit** `0xF7F0|imm4, 0x8000` (bytes `fN f7 00 80`),
**not** `0xDFxx`. A naive `0xDFxx` scan produces dozens of false positives (e.g.
`trustzone-omap4` file 0x009c lies inside the ELF program-header table). Every
hit below is a real `smc` under the two encodings above.

All five images were scanned (ARM + Thumb), plus `procnto-smp-instr`:

| image | ARM `smc` | Thumb `smc` | total |
|---|---:|---:|---:|
| `emmc-first1mb.bin` | 6 | 1 | **7** |
| `bootblob_arm9000.bin` | 6 | 1 | 7 (not independent — see below) |
| `trustzone-omap4` | 1 | 0 | **1** |
| `rpmb-omap4` | 1 | 0 | **1** |
| `devb-mmcsd-winchester` | 0 | 0 | **0** |
| `procnto-smp-instr` | 0 | 0 | **0** |

**[P]** `bootblob_arm9000.bin` is byte-for-byte identical to
`emmc-first1mb.bin[0x9000 : 0xe9000]` — `cmp -l` reports **0 differing bytes**
over all 86016. It is a slice of the same stage and contributes no independent
call site. (Its sites are the same ones reported for `emmc-first1mb.bin` at
0x9094 and 0x1939c–0x193ec, i.e. file 0x94 and 0x1039c–0x103ec in bootblob
coordinates.)

**The three ARM veneers are byte-identical [P]:**

```
rpmb-omap4        0x21f0 : f05f2de9 ff60a0e3 00c0a0e3 d50f07ee 4ff07ff5 6ff07ff5 5ff07ff5 710060e1 f05fbde8 1eff2fe1
trustzone-omap4   0x7c30 : (identical, 40 bytes)
emmc-first1mb.bin 0x9078 : (identical, 40 bytes)
```

i.e. `push {r4-r11,fp,ip,lr}` / `mov r6,#0xff` / `mov ip,#0` /
`mcr p15,0,r0,c7,c5,6` / `dsb sy` / `isb sy` / `dmb sy` / `smc #1` (`e1600071`)
/ `pop` / `bx lr`. The bootloader's Thumb copy at **0x800043b4** is the same
program in Thumb:

```
800043b4: 2de9f05f   push.w  {r4-r11,fp,ip,lr}
800043b8: 4ff0ff06   mov.w   r6, #0xff
800043bc: 4ff0000c   mov.w   ip, #0
800043c0: 07eed50f   mcr     p15, 0, r0, c7, c5, 6
800043c4: bff34f8f   dsb     sy
800043c8: bff36f8f   isb     sy
800043cc: bff35f8f   dmb     sy
800043d0: f7f1 8000  smc     #1
800043d4: bde8f05f   pop.w   {r4-r11,fp,ip,lr}
800043d8: 7047       bx      lr
```

### Bases assumed for the position-dependent images

| Image | Base | Basis |
|---|---|---|
| `local/proprietary/device-binaries/emmc-first1mb.bin` | **0x80000000**, file offset == addr − 0x80000000 | given; corroborated by the ARM vector table at file 0x9000 (`8740 00ea` = `b` to 0x800191A4) |
| `local/proprietary/device-binaries/bootblob_arm9000.bin` | n/a | byte-identical slice of the above at file 0x9000 |
| `.../proc/boot/rpmb-omap4` | ET_DYN; whole first PT_LOAD has `p_vaddr 0` (file 0 == vaddr 0); 2nd PT_LOAD file 0x6f24 → vaddr 0x7f24 | `readelf -l`: `LOAD 0x000000 0x00000000 0x8063a000 0x060d4` |
| `.../proc/boot/trustzone-omap4` | ET_DYN; `LOAD 0x000000 0x00000000 0x8062b000 0x0c6bc`, `LOAD 0x00cf1c 0x0000df1c` | `readelf -l` |
| `.../proc/boot/devb-mmcsd-winchester` | ET_DYN, as above | `readelf -l` |

For these three ELF files **link-time vaddr == file offset** throughout the text
segment, so the two numbers coincide; both are quoted where useful.

### Layout of the first 1 MiB (needed to place the stages) [P]

`emmc-first1mb.bin` opens with a **named-section catalogue**, 0x1C bytes per
entry — `{u32 off, u32 size, u32, u32, u32, char name[8]}`:

```
00000000: 0040 0000 704d 0000 00000000 00000000 00000000   "MLO"
0000001c: 000c 0000 b832 0000 00000000 00000000 00000000   "PRIMAPP"
00000038: 0002 0000 6009 0000 00000000 00000000 00000000   "KEYS"
00000054: ffffffffffffffff ...                               (terminator)
```

so **MLO sits at file 0x4000 (size 0x4d70 → 0x4000–0x8d70)** and the ARM stage
starts at **0x9000** (name `arm9000`, which is what this project's
`bootblob_arm9000.bin` is). The Thumb `smc` stage therefore lives inside MLO at
0x4000–0x8d70, and the ARM `smc` stage inside the 0x9000+ image. That split
matters for Q1 and for "what to try next".

---

## Q1. Does any QNX/early-boot component perform a secure-world registration / initialisation SMC that our Linux does not?

### Q1 answer: **No. There is no registration, no "register OS", no "open secure world", no init SMC anywhere.** Every SMC in every image is one of only three things: the single shared `r6=0xff` veneer, the `r12=0x1xx` ROM L2-cache services, or a call to a *service number* from the observed set.

### Q1.1 Complete `smc` enumeration with service (r0) and domain (r2)

#### (a) `trustzone-omap4` — 1 `smc` at file 0x007c4c [P]

DT_HASH enumerates its defined symbols: `main = 0x8c84`, `_btext = 0x19c0`,
`_init = 0x1680`. The veneer is at 0x7c30; **the only `BL` to it is at 0x9240**,
inside the marshaller `0x9188`; the marshaller has exactly **three** call sites:

```
    8adc:	eb0001a9 	bl	0x9188      <- raw passthrough (devctl)
    92e4:	ebffffa7 	bl	0x9188      <- service 0x31
    941c:	ebffff59 	bl	0x9188      <- service 0x37
```

| site | r0 | r1 | r2 (domain) | r3 (count) | args | what it is |
|---|---|---|---|---|---|---|
| `0x8adc` | **from client** | from client | **from client** | from client | 4 words from client | raw SMC passthrough |
| `0x92e4` | `0x31` | `0` | **`4`** | `1` | `{r4}` | KEK-state query |
| `0x941c` | `0x37` | `r7` | **`4`** | `4` | `{[sp+0x20], r4, [sp+0x28], r4}` | 4-arg service |

`0x8adc` is the important one: a **generic pass-through** — the client supplies
`r0..r3` verbatim:

```
    8ad8:	e995000f 	ldmib	r5, {r0, r1, r2, r3}
    8adc:	eb0001a9 	bl	0x9188
    8ae0:	e5850000 	str	r0, [r5]
```

guarded by a devctl-code check at 0x8a5c–0x8a78 (`movw`/`movt` ⇒ **0xC0280501**
and **0xC0280502**, `movne r0,#0x59` on mismatch). So `trustzone-omap4` is a
**proxy**: userland asks it to issue any service, and it forwards r0–r3 + 4 stack
words to the monitor. It imposes **no** service policy of its own.

#### (b) Does `trustzone-omap4`'s main call the monitor *before* its devctl handler? **No.** [P]/[I]

* `main = 0x8c84`. The three call sites above are the *only* route to `smc`.
* `0x8adc` sits inside the devctl handler (it *is* the passthrough).
* `0x92e4` / `0x941c` are inside a static function at `0x9268`
  (`push {r4-r11,lr}; sub sp,#0x7c`), called from `main` at **0x9628**, and that
  call is inside a **request loop** that first receives a 168-byte message:

```
    95e0:	e2856c0d 	add	r6, r5, #3328	@ 0xd00     ; request buffer
    95e8:	e3a020a8 	mov	r2, #168	@ 0xa8
    95ec:	ebffe063 	bl	0x1780                     ; PLT: receive 168 B
    95f0:	e35000a8 	cmp	r0, #168
    95f4:	0a000006 	beq	0x9614
    ...
    9620:	e1a0200b 	mov	r2, fp
    9624:	e28d3074 	add	r3, sp, #116	@ 0x74
    9628:	ebffff0e 	bl	0x9268
```

So 0x31/0x37 are issued **per client request**, not at init. [I] this loop runs
after `resmgr_attach`, because clients must be able to connect; the bytes do not
let me order it against the PLT call without resolving imports, but nothing in
`main`'s prologue (0x8c84–0x8f94: `ThreadCtl`, clock init at 0x8828, slog) ever
touches the monitor.

This **refines report 145 §3.2**: "its init does not call the monitor" is
correct, but `trustzone`'s *main* does call 0x31 and 0x37 — on behalf of a
client, always with domain 4.

#### (c) `rpmb-omap4` — 1 `smc` at file 0x00220c [P]

Veneer 0x21f0, marshaller 0x22e8, **one** `BL` to the veneer at 0x241c, and
**six** `BL`s to the marshaller:

| marshaller call site | r0 service | r1 | r2 domain | r3 count | stack args |
|---|---|---|---|---|---|
| `0x24d8` | **0x12** | 0 | **4** | 2 | `{phys(buf), len}` |
| `0x251c` | **0x33** | 0 | **4** | 2 | `{phys(key), 16}` |
| `0x26a0` | **0x2F** | 0 | **4** | 1 | `{phys(descriptor)}` |
| `0x27ac` | **0x2E** | 0 | **4** | 2 | `{fp, r5}` |
| `0x2800` | **0x32** | 0 | **4** | 2 | `{phys(buf), r4}` |
| `0x2840` | **0x31** | 0 | **4** | 1 | `{index}` |

Representative quoted site (`0x2840`, service 0x31):

```
    2830:	e3a00031 	mov	r0, #49	@ 0x31
    2834:	e3a01000 	mov	r1, #0
    2838:	e3a02004 	mov	r2, #4
    283c:	e3a03001 	mov	r3, #1
    2840:	ebfffea8 	bl	0x22e8
```

The marshaller passes `r1 = x1` and `r2 = domain` straight through, builds
`page[0] = count` and `page[1..] = args`, then at 0x2410–0x241c:

```
    2410:	e1a00005 	mov	r0, r5          ; r0 = service
    2414:	e1a01006 	mov	r1, r6          ; r1 = x1
    2418:	e59d2018 	ldr	r2, [sp, #24]   ; r2 = saved r2  = DOMAIN
    241c:	ebffff73 	bl	0x21f0          ; r3 = phys(page), still live from 0x2374
```

#### (d) `devb-mmcsd-winchester` — **0 `smc`** [P]. See Q5.

#### (e) `procnto-smp-instr` — **0 `smc`** [P]. procnto issues **no** monitor call at
startup or ever. There is no OS→monitor handshake in the microkernel.

#### (f) `emmc-first1mb.bin` — 7 sites, in three unrelated groups [P]

**Group 1 — the Thumb MLO veneer at 0x800043b4 (smc at 0x800043d0).** Exactly one
caller, the 4-register-argument passthrough at **0x80004c26**:

```
80004c26: b40f        push  {r0,r1,r2,r3}
80004c28: b510        push  {r4,lr}
80004c2a: ab05        add   r3, sp, #20
80004c2c: 9a04        ldr   r2, [sp, #16]   ; r2 = caller's r2  (passthrough)
80004c2e: f7ff fbc1   bl    0x800043b4
```

`0x80004c26` has **20+ callers**, all inside 0x80004600–0x80004b20, one per
service — a **service wrapper library**:

| wrapper | service r0 | r2 domain | r3 count |
|---|---|---:|---:|
| 0x8000468e | 0x0E | 5 | — |
| 0x800046aa | 0x0F | — | — |
| 0x800046c6 | 0x09 | — | — |
| 0x800046e2 | 0x0A | — | — |
| 0x80004702 | 0x2D | 5 | — |
| 0x80004720 | 0x33 | 5 | — |
| 0x80004786 | 0x2E | 5 | 2 |
| 0x8000484e | 0x2F | 5 | — |
| 0x80004936 | 0x34 | — | — |
| 0x8000494c | 0x35 | — | — |
| 0x80004972 | 0x36 | 5 | — |
| 0x80004990 | 0x30 | 5 | — |
| 0x800049b0 | 0x31 | 5 | — |
| 0x80004a0a | 0x32 | 5 | — |
| 0x80004a28 | 0x37 | 5 | 4 |
| 0x80004a58 | 0x12 | **0** | 2 |
| 0x80004ac2 | 0x38 | 5 | 4 |
| 0x80004b0a | 0x39 | 5 | 2 |
| 0x80004b36 | **0x3A** | 5 | (r3 not set) |
| 0x80004b68 | **0x3C** | 5 | 4 |

**Group 2 — the ARM veneer at 0x80009078.** One `BL` caller, `0x80018414`, inside
the wrapper at 0x800183dc; that wrapper has **28 callers**, all in
0x80017500–0x80018100 (a "secure-module status" routine — the strings immediately
before it at file 0x18348–0x183d8 are `"…clock status: %x"`,
`"PK modu"+"le clock status"`, `"RNG modu"+"le clock status"`,
`"SHA2/MD5 modu"+"le clock status"`, `"CRYPTOMDMA modu"+"le clock status"`).
Services used there: **0x09, 0x0A, 0x0E, 0x0F, 0x12, 0x2D, 0x2E, 0x2F, 0x30,
0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37** — with **r2 = 5** at every site
except the 0x12 site at 0x80017d74, which uses **r2 = 0**:

```
80017d68:	e3a02000 	mov	r2, #0
80017d70:	e3a00012 	mov	r0, #18
80017d74:	eb000198 	bl	0x800183dc
```

**Group 3 — the ROM monitor PL310 L2-cache services** (see §Q1.2).
`0x80019394 / a8 / bc / d0 / e4`, six instructions apart, each
`mov ip,#imm` + `smc #1`, wrapper shape `push {r1-r11,fp,ip,lr}` … `pop {…,pc}`:

```
80019394: e92d5ffe  push {r1,r2,r3,r4,r5,r6,r7,r8,r9,sl,fp,ip,lr}
80019398: e300c102  movw ip, #258     @ 0x102
8001939c: e1600071  smc 1
800193a0: e8bd9ffe  pop  {r1,...,ip,pc}
...
800193bc  mov ip,#0x100   800193d0/193e4  movw ip,#0x112
```

### Q1.2 What are those `r12=0x1xx` calls? Not RPMB, not registration — the **documented ROM-code HLOS services for the PL310 L2 cache** [P]

TRM `hardware/swpu231ap.pdf.txt` §27.5 "Services for HLOS Support" (line 396778)
says verbatim:

> "These services are implemented in monitor mode (the service must be called by
> writing the function ID into the R12 register and using the SMC instruction)
> … `SMC 0x1 POP {R1-R12, PC}`"

and Tables 27-57…27-62 give

| R12 | meaning |
|---|---|
| `0x100` | write PL310 **Debug** control register (arg r0) |
| `0x101` | clean+invalidate a phys range (r0, r1) |
| `0x102` | write PL310 **Control** register, enable/disable (r0) |
| `0x109` | write PL310 **Auxiliary Control** register (r0) |
| `0x112` | write **Tag/Data RAM latency** register (r0, r1) |
| `0x113` | write **Prefetch Control** register (r0) |

The exact instruction sequence in the TRM's own code example — `PUSH {R1-R12, LR}`
/ `LDR R12,=…` / `SMC 0x1` / `POP {R1-R12, PC}` — is instruction-for-instruction
identical to 0x80019394 etc. **Therefore the 5 extra `smc` sites in the
bootloader are L2-cache configuration, unrelated to RPMB, and are not a
handshake.** They are reached from the cache driver at 0x80009c98 and 0x80009f1e
(`blx 0x80019380`, the ICIALLU/TLB helper next door).

The TRM has **no** documentation of service numbers 0x09/0x0A/0x0E/0x0F/0x12/
0x2D–0x3C: `grep -i rpmb` on the TRM yields only two hits (both about the RPMB
partition as storage) and `grep -i "secure monitor call"` yields only the §27.5
HLOS text. The service family we care about is **undocumented and lives in the
monitor, which we do not have**.

### Q1.3 Is there any service in the 0x30–0x3A range at startup?

* `procnto`: none (0 `smc`). [P]
* bootloader: **0x30…0x3A (and 0x3C) all exist as wrappers**, but I can prove
  **no in-image caller and no pointer to any of them** [P]:

```
scan for ARM BL, Thumb BL and literal words 0x80004xxx (±1) over the whole 1 MiB:
0x37 wrapper 0x80004a1a  callers: []     0x3A wrapper 0x80004b2e  callers: []
0x38 wrapper 0x80004a8e  callers: []     0x3C wrapper 0x80004b46  callers: []
0x39 wrapper 0x80004ae0  callers: []     (same for the 0x12 wrapper and the 0x4c26 veneer wrapper)
no literal pointers found
```

  So on the evidence available the 0x38/0x39/0x3A/0x3C wrappers are **not
  executed from anything inside the first 1 MiB**. [I] They are either a library
  for a stage past 0x100000 or dead code. They are the only plausible
  "OS registration" candidates in the whole corpus, and I **cannot** show they
  run at boot — so the answer to "is there a registration call" remains **no
  evidence of one**.

### Q1.4 Explicit negative result for the "registration" hypothesis

Set of service numbers that appear **anywhere** in the corpus [P]:
`0x09, 0x0A, 0x0E, 0x0F, 0x12, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33,
0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3C`, plus ROM `0x100–0x113`.
There is no call with a distinctive tag/register combination other than
`r6=0xff, ip=0`. There is no second veneer variant, no `smc #0`, no `hvc`, no
"magic" argument anywhere.

**Our Linux veneer is byte-identical to QNX's and to the bootloader's [P], and
still fails. Therefore the discriminator is not in the veneer and not in the
service dispatch — it is either in an argument value (Q3) or inside the monitor
(Q2).**

---

## Q2. What does return code 0xFF05 mean?

### Q2 answer (short): **Nothing in any available byte explains it.** `0xFF05` does not appear as a constant, immediate, or string in *any* of the five images (nor in `procnto`), and the TRM does not document this service family or any error-code scheme for it. The monitor image is not available, so **the meaning is [U] unresolved**. What *can* be proven is the surrounding contract.

### Q2.1 `0xFF05` / 65285 is absent from every image [P]

```
word 0xff05 (bytes 05 ff 00 00) in emmc-first1mb.bin ........... 0 hits
                               in rpmb-omap4 ................. 0 hits
                               in trustzone-omap4 ............ 0 hits
                               in devb-mmcsd-winchester ...... 0 hits
disassembly grep 'ff05|65285|#0xff…' in rpmb/trustzone ........ 0 relevant hits
TRM grep '0xFF05|65285|R6 = 0x' ............................... 0 hits
```

(The only `0xFF05` anywhere in the repo is report 93, where `movw r1,#0xff05` at
0x800152d2 is the Cellebrite ramloader's **command code 0xF005** — an unrelated
coincidence, not an error code.)

Consequence [P]: **QNX and the bootloader never name, decode, or test for this
value.** Every caller normalises "non-zero" to failure and throws the value away:

```
rpmb-omap4, after every marshal call (e.g. 0x24d8, 0x251c):
    24dc:	e2700001 	rsbs	r0, r0, #1     ; r0 = 1 - ret
    24e0:	33a00000 	movcc	r0, #0         ; ret>1 -> 0
bootloader (Thumb), e.g. after service 0x33:
    80004724: 0001        movs  r1, r0
    80004726: f04f 0001   mov.w r0, #1
    8000472a: d000        beq.n 0x8000472e     ; ret==0 -> success
    8000472c: 2000        movs  r0, #0
```

**So the monitor's success code is 0**, and these wrappers convert it to a C
boolean. That matters: **0xFF05 is genuinely the monitor's `r0`**, passed through
the marshaller unchanged (`0x2374 mov r3,r0` … `0x2380 beq`; the only substitution
is `r0 = 0` when the *physical-address translation* fails, a different path).
65285 is not a QNX-side artefact.

### Q2.2 Does the bootloader's 0x31 also return an error? — not measurable statically; the code is written to survive it

* Report 116 §3 (a live probe, not this session) records `select_kek` (service
  `0x31`) returning `-1`, and concludes TabletOS ran the *fallback* key. That is
  consistent with a monitor that can refuse 0x31 on this unit.
* `rpmb-omap4`'s own key path is written to survive 0x31 failing: `0x2814` =
  `select_kek(index,out)` (the 0x31 wrapper, see §Q4.1), and the key setup at
  ~0x2ec0 does `select_kek(1, …)` then `select_kek(0, …)`, with a caller that
  copies a hard-coded 32-byte ASCII seed when both fail (report 116 §3:
  `"retreating is advancing in the other direction"`).
* A "bootloader returns 0xFF05 too" claim needs the bootloader's return value,
  which is not recoverable statically. **Marked [U].**

### Q2.3 Is it "invalid function", "not initialised", "bad OS id"? — undecidable; here is what the bytes forbid and permit

Forbidden by the bytes:

* **Not a code the OS is expected to interpret.** No decode table, no comparison
  against `0xFFxx`, no error-string index anywhere in the corpus.
* **Not "the OS reported a fault":** 0xFF05 arrives with the output buffer
  untouched (`0x12` leaves `aa aa aa aa`, report 145). A service that ran and
  failed would normally have been *entered*; the untouched buffer shows the call
  is refused **before any payload transfer**. [P from the caller side + report
  145's observation]

Permitted, and the strongest inference:

* The arguments **constant** across all six failing calls are
  `(r1=0, r2=domain, r3=page address, r6=0xff, r12=0, smc imm4=1)`. `r0` varies
  and changes nothing. So the monitor's refusal keys on one of the **constant**
  arguments, or on internal state. [I]
* `r3` (the command-page address) is the only constant that is also an
  *address*, and the recovered "−0x10000000 bias" proves the monitor's view of
  DRAM is **not** the non-secure physical view — the address is translated before
  use. An address-window/ownership check is therefore structurally plausible. [I]
* The pre-bias **hang** is decisive context: an address the monitor could not use
  did not produce 0xFF05, it stalled the bus. So 0xFF05 is what the monitor
  returns when it *did* access the page and then decided not to proceed — a
  policy/precondition failure, not a simple "bad pointer". [I]
* "Invalid function" would have to mean *every* valid service is invalid, which
  is indistinguishable from a global refusal — but `0x31` (count 1, args
  `{index}`, no payload address) failing identically to `0x12`/`0x33` shows the
  refusal is not payload-specific. [I]

**Which register/argument it validates: not determinable without the monitor
image. [U]** The candidate set is exactly `{r2, r3, r6, r12, imm4}` (or
monitor-internal state); `r0` is excluded by evidence.

---

## Q3. Bootloader caller vs QNX vs ours

### Q3 answer: the three callers are **the same program**. The only difference in the register set is the **domain: 5 (bootloader) vs 4 (QNX)** — with one bootloader site using **0** — and the monitor demonstrably tolerates more than one domain value.

| | bootloader MLO (Thumb `0x80004c26`) | bootloader ARM (`0x800183dc`) | `rpmb-omap4` `0x22e8` | `trustzone` `0x9188` | our `pbmon.c` |
|---|---|---|---|---|---|
| r0 = service | per wrapper (table in §Q1.1f) | per call site | 0x12/0x2E/0x2F/0x31/0x32/0x33 | 0x31 / 0x37 / client | 0x12/0x31/0x33/0x2F |
| r1 = x1 | 0 | **0** | **0** | 0 / r7 | 0 |
| r2 = **domain** | **5**, except 0x12 → **0** | **5**, except 0x12 → **0** | **4** (all six) | **4** (both) | 4 |
| r3 | **ptr to command page** | **ptr to command page** | `phys(page)` | `phys(page)` | `phys(page)−0x10000000` |
| r6 | 0xff — *set inside the veneer* | same | same | same | same |
| r12 | 0 — *set inside the veneer* | same | same | same | same |
| extra regs | none | none | none | none | none |
| `smc` | `#1` (`f7f1 8000`) | `#1` (`e1600071`) | `#1` | `#1` | `#1` |
| cache ops | ICIALLU + dsb/isb/dmb inside the veneer | same | same | same | same |

### Q3.1 The domain is **not** a fixed required value [P]

`0x12` is issued with domain **0** by the bootloader and with domain **4** by
QNX; `0x31` with **5** by the bootloader and **4** by QNX. Unless *both* the
bootloader and TabletOS are broken (contradicting "the bootloader reads RPMB at
every boot" and TabletOS being up), the monitor **cannot** be validating `r2`
against a single constant. So **r2 = 4 vs 5 is not the cause of 0xFF05.**

Caveat [U]: the bootloader's ARM wrapper reloads `r2` from `[sp,#0x18]`, which
with its frame (`push {r0-r3}` then `push {r4,r5,r6,lr}`) is the *saved r6* slot,
not the saved r2 slot; the Thumb wrapper at 0x80004c26 unambiguously passes the
caller's r2 through (`ldr r2,[sp,#16]`). Since **both** families' call sites set
`r2 = 5` explicitly, the bootloader's effective domain is 5 either way; what is
[U] is only which slot the ARM wrapper's reload corresponds to in the source.

### Q3.2 Any additional registers before the `smc`? **No.** [P]

Both veneer forms save `r4-r11,fp,ip,lr`, set `r6=0xff` and `ip=0`, do
`mcr p15,0,r0,c7,c5,6` (ICIALLU on `r0`, which the monitor is expected to
clobber), `dsb`/`isb`/`dmb`, `smc #1`, restore. Nothing else — no `SCR`/`NSACR`
write, no `VBAR`, no stack switch, no `cpsid`, no TZPC/TZASC programming, no L2
state beyond the group-3 ROM services. Our veneer is byte-identical (§0).
**There is no register our kernel fails to set.**

### Q3.3 Command-page layout: **identical** [P]

Bootloader, Thumb, `0x8000479a` onward (the 0x2E/0x2F descriptor build):

```
800047be: 2501        movs  r5, #1
800047c2: f04f 0802   mov.w r8, #2
800047c6: f8ad 5008   strh.w r5, [sp, #8]      ; +0x00 = 1
800047ca..800047e4  strh.w {r6|r5|r8},[sp,#10] ; +0x02 = 0 / 1 / 2   (op)
800047ea: f88d 500c   strb.w r5, [sp, #12]     ; +0x04 = dir (1 = seal)
800047f4: f88d 600c   strb.w r6, [sp, #12]     ; +0x04 = 0 (unseal)
80004804: f8ad 501c   strh.w r5, [sp, #28]     ; +0x1c
80004812: 9305        str   r3, [sp, #20]      ; +0x14
80004814: 9309        str   r3, [sp, #36]      ; +0x24  (= len, r3 = 0xe0)
80004816: 930b        str   r3, [sp, #44]      ; +0x2c  (= key2 len, 16)
8000481a..80004836   [sp,#24] = data, [sp,#32] = out, [sp,#40] = key2
80004820: f8bd 000a   ldrh.w r0, [sp, #10]     ; op
80004826: f440 6080   orr.w  r0, r0, #1024     ; op |= 0x400   <-- when r4 == 0
8000482a: f8ad 000a   strh.w r0, [sp, #10]
8000482e: 4650        mov   r0, sl
80004830: f7ff ff6f   bl    0x80004712         ; 0x33 key load
...
80004848: 202f        movs  r0, #47 @ 0x2f
8000484e: f000 f9ea   bl    0x80004c26
```

vs `rpmb-omap4`'s builder `0x2530`:

```
    25b0:	strh	r3, [r4]      ; +0x00 = 1
    25b8:	strb	r3, [r4, #5]  ; +0x05 = 0
    25bc:	str	r9, [r4, #12] ; +0x0c = len
    25c0:	str	r9, [r4, #28] ; +0x1c = len
    25c4:	str	r9, [r4, #36] ; +0x24 = len
    25d0:	str	r0, [r4, #24] ; +0x18 = phys(data)
    25e4:	str	r0, [r4, #32] ; +0x20 = phys(out)
    25f0:	strb	sl, [r4, #4]  ; +0x04 = dir
    2608:	strh	r8, [r4, #2]  ; +0x02 = mode
    2610:	cmp	r8, #1
    262c:	str	r0, [r4, #40] ; +0x28 = phys(key2)  (mode 1 only)
    263c:	str	r3, [r4, #44] ; +0x2c = key2 len   (mode 1 only)
    2648:	ldrh	r3, [r4, #2]
    264c:	orr	r3, r3, #1024 ; op |= 0x400        <-- when the 4th arg == 0
    2650:	strh	r3, [r4, #2]
    265c:	bl	0x24ec        ; 0x33 key load
```

**Same field offsets, same `|= 0x400` rule, same "issue a 0x33 key load when you
set 0x400" rule.** The ABI we transcribed is correct.

### Q3.4 Does the bootloader pass an "OS type"/personality? **Nothing found.** [P]

No extra register, no extra page word (page[0] is a *count* in both callers), no
different `smc` immediate, no distinguishing flag. The only numeric divergence is
`r2 ∈ {0,4,5}`.

---

## Q4. Is rpmb-omap4's `0x2F op=1` key-derivation call a required init before `0x33` / `0x2F op=0`?

### Q4 answer: **No — it is not a prerequisite.** On the bytes, `op=1` is a *different service* (a 32-byte → 16-byte keyed derivation that needs a second key), it is issued only by the KEK-provisioning routine, and the seal/unseal data path never issues it.

### Q4.1 `op` is not a literal 0/1 field — it is the **device mode from `confstr(0x205)`**, and on this unit it is 0 [P]

`0x29cc` is the "which platform" function:

```
    29fc:	e3000205 	movw	r0, #517	@ 0x205      ; confstr name
    2a08:	ebfffaa4 	bl	0x14a0                     ; confstr(0x205, NULL, 0) -> len
    2a10:	ebfffa9c 	bl	0x1488                     ; confstr(0x205, buf, len)
    2a44:	e1a00005 	mov	r0, r5
    2a50:	ebfffabf 	bl	0x1554                     ; strstr(buf, <string @ file 0x5aa3>)
    2a54:	e3500000 	cmp	r0, #0
    2a6c/2a74                           ; store 0 or 1 into the cached global
```

and the string at `rpmb-omap4` file **0x5aa3** is **`"Winchester"`**. So `0x29cc`
returns **0 on Winchester**, 1 otherwise. That value is what lands in descriptor
`+0x02` (after the `|= 0x400` step). This matches report 141's observation
exactly. **On this PlayBook the descriptor's `+0x02` is `0x0400`.**

### Q4.2 Where `op=1` actually lives — the four builder call sites [P]

`0x2530(mode=r0, x1=r1, dir=r2, flag=r3, [sp]=data, [sp+4]=len, [sp+8]=out,
[sp+0xc]=key2, [sp+0x10]=key2len, [sp+0x14]=key33, [sp+0x18]=key33len)`
with `r7 = (flag == 0)` (`0x256c: rsbs r7,r6,#1` … `0x2570: movcc r7,#0`).

| site | mode r0 | dir r2 | flag r3 | data / len | out | key2 | 0x33 key load | meaning |
|---|---|---|---|---|---|---|---|---|
| `0x2ba4` | `r5 = 0x29cc()` → **0** | `0` = **unseal** | `0` → `op\|=0x400` | `[sp]`, len | `r8` | — | **yes** | block unseal (read path) |
| `0x2c88` | `r5 = 0x29cc()` → **0** | `1` = **seal** | `0` → `op\|=0x400` | `r8`, `0xE0` | `r6` | `r7` | **yes** | block seal (write path) |
| `0x2e60` | **1** | `1` = seal | `r6` | `[sp]`, **32** | `r8`, **16** | `phys([item+0x34])`, 16 | if flag==0 | **KDF: 32 B seed → 16 B** |
| `0x2f64` | **1** | `1` = seal | **`0`** → `op\|=0x400` | `[sp]`, **32** | `r8`, **16** | `phys([item+0x34])`, 16 | **yes** | **KDF + KEK readback** |

`0x2f64` quoted (this is the "`0x2F op=1` key derivation" the brief asks about;
the cited `0x2e24` is the sibling site at `0x2e60`, whose instruction stream
passes 32-byte data, a 16-byte output and a 16-byte `key2` at 0x2e28–0x2e60):

```
    2f54:	e3a00001 	mov	r0, #1          ; mode/op = 1
    2f58:	e3a01000 	mov	r1, #0
    2f5c:	e1a02000 	mov	r2, r0          ; dir = 1 (seal)
    2f60:	e1a03001 	mov	r3, r1          ; flag = 0  -> op |= 0x400 + 0x33
    2f64:	ebfffd71 	bl	0x2530
    2f68:	e5953028 	ldr	r3, [r5, #40]   ; [obj+0x28]
    2f6c:	e3530001 	cmp	r3, #1
    2f70:	1a000002 	bne	0x2f80
    2f74:	e3a00000 	mov	r0, #0
    2f78:	e28d1034 	add	r1, sp, #52	@ 0x34
    2f7c:	ebfffe24 	bl	0x2814          ; select_kek(0, &tmp) = SMC 0x31
```

and the same function first probes the KEKs at 0x2ec0–0x2f04:

```
    2ec8:	e5953028 	ldr	r3, [r5, #40]
    2ecc:	e3530000 	cmp	r3, #0
    2ed0:	1a000005 	bne	0x2eec
    2ed4:	e3a00001 	mov	r0, #1
    2ed8:	e2851028 	add	r1, r5, #40
    2edc:	ebfffe4c 	bl	0x2814          ; select_kek(1, …)   [SMC 0x31, index 1]
    ...
    2ef4:	e3a00000 	mov	r0, #0
    2ef8:	e2811028 	add	r1, r1, #40
    2efc:	ebfffe44 	bl	0x2814          ; select_kek(0, …)   [SMC 0x31, index 0]
```

### Q4.3 The data path does **not** use `op=1`, and it *does* require `0x31` first when a flag says so [P]

`0x2aa4` = the block unseal used by the read path:

```
    2ac0:	ebffffc1 	bl	0x29cc              ; mode = Winchester ? 0 : 1
    2ac4:	e1a05000 	mov	r5, r0
    2ad0:	e35700e0 	cmp	r7, #224	@ 0xe0  ; len must be 0xE0
    2ae0:	e5933028 	ldr	r3, [r3, #40]	@ 0x28
    2ae4:	e3530001 	cmp	r3, #1
    2ae8:	1a000004 	bne	0x2b00
    2aec:	e3a00001 	mov	r0, #1
    2af0:	e28d1024 	add	r1, sp, #36	@ 0x24
    2af4:	ebffff46 	bl	0x2814              ; select_kek(1, &tmp)  = SMC 0x31 !
    2af8:	e3500000 	cmp	r0, #0
    2afc:	0a000036 	beq	0x2bdc              ; 0x31 failure -> unseal fails
    2b00:	e28670e4 	add	r7, r6, #228	@ 0xe4 ; data = frame + 0xE4
```

So the only sequencing dependency the bytes show is
**`0x31` (select_kek) → `0x2F op=0`**, gated by `[obj+0x28] == 1`; **`0x31` →
`0x33`** in the descriptor builder; and **`0x31` → `0x2F op=1`** in the
provisioning routine. There is **no** `0x2F op=1` → `0x33`/`op=0` dependency
anywhere.

(This restates/corrects report 145 §4.2 "try a 0x2F op=1 derivation first": it is
indeed the one call in the QNX sequence we had not issued, but it is **not** a
precondition for `0x33` or `0x2F op=0`, and issuing it cannot plausibly change
0xFF05, because every service including the payload-free `0x31` already fails.)

### Q4.4 What does the monitor do with op=1 vs op=0? — from the bytes: two different primitives [I]

* `op=1` carries a **second key** (`key2` at `+0x28`/`+0x2c`), and the builder
  *rejects* mode values outside {1,2} at 0x2610–0x261c, and takes **32 bytes in,
  16 bytes out**. 32→16 with a 16-byte key is the shape of a keyed
  derivation / MAC-truncate (a KDF).
* `op ∈ {0,2}` (+`0x400`) carries no `key2`; the data path uses len `0xE0` (224)
  in and 224 out — a block cipher transform.
* The monitor's own code is not in our possession, so this is shape inference
  only. **[U]** for the algorithm.

---

## Q5. What does `devb-mmcsd-winchester`'s `crypto=enable` do?

### Q5 answer: it is **not** a devb option at all — it is `fs-qnx6`'s mount option. The driver contains **zero** `smc`, **no AES/HMAC code**, and its RPMB support is raw MMC command traffic.

* `devb-mmcsd-winchester`: **0** ARM `smc`, **0** Thumb `smc` [P] (§0 scan).
* It contains no `crypto` string: `strings` and a regex over the whole blob for
  `crypt|secur|key` yield only `SD_SECURITY : %x` (file 0x11448) and the `RPMB :`
  messages.
* The **`crypto=` option string lives in `fs-qnx6.so`** [P]:

```
$ strings -a local/proprietary/dumped4869ifs/proc/boot/fs-qnx6.so
crypto
fs-qnx6: %s crypto error (%d) detected in balloc
crypto=      Enable or disable encryption support
```

The live command line is
`blk cache=10M,noatime,hookdll=vfs-hooks-rim.so cam quiet,cache qnx6 crypto=enable`.
After the `qnx6` fstype token the remaining options go to the filesystem, not the
driver. So `crypto=enable` enables **fs-qnx6 filesystem encryption** and has
nothing to do with the RPMB record block or the monitor.

* The driver's RPMB support is **command-level** (raw 512-byte frames), not
  monitor-mediated [P]:

```
  111c4 RPMB : wait for card ready failed!
  111e8 RPMB : send set blk cnt cmd failed!
  1120c RPMB : wait for set blk cnt cmd failed!
  11234 RPMB : send read/write cmd failed!
  11258 RPMB : wait for read/write cmd failed!
  11280 RPMB : wait for PIO done failed!
  124c0 RPMB : resolving new shared buffer location 0x%x!
  124f4 RPMB : new virt buffer location: 0x%x!
  1251c RPMB: read/write frame failed
```

**Consequences for us:** `devb-mmcsd-winchester` is *evidence that a component can
do RPMB on this SoC with no monitor call at all*, and it is **not** a reference
for the sealed-record path. The 224-byte record block's seal/unseal is
`rpmb-omap4`'s private layer (report 144), reached only through the monitor.
`libc.so.3` contains `/dev/crypto` (the QNX `crypto-service` device), a separate
resource-manager path — again with no `smc` in the driver.

---

## Q6. Anything else that distinguishes "the OS QNX" from "an unknown OS"?

### Q6 answer: **Nothing found.** No OS-type field, no registration structure, no boot-descriptor OS id, and no secure-world state the OS asserts — in any of the five images or in `procnto`.

Checks performed and results [P]:

1. **Service the bootloader hands over to.** The only handover-ish candidates are
   `0x38`, `0x39`, `0x3A`, `0x3C`, which exist solely in the MLO Thumb library
   (§Q1.3) and have **no in-image caller and no literal pointer**. Nothing to
   hand over that we can show runs.
2. **"OS registration" structure.** No `osid`/`ostype`/`register`-style string
   exists in `emmc-first1mb.bin`. Its only security-adjacent strings are
   `"Secure:failed to write user wp. retCode:0x%x"` (file 0x106b4) and
   `"L4_SECURE_GICLK: %x"` / `"L3_SECURE_GICLK: %x"` (0x182c1 / 0x182d9) —
   clock status printouts, not an OS identity.
3. **NVRAM / boot descriptor OS field.** The boot container is the name+offset+
   size catalogue of §0 (`MLO`, `PRIMAPP`, `KEYS`, `CertISW`, `arm9000`); every
   field is an offset, a size or a name. There is no OS/personality field.
4. **A secure-world state the OS must assert.** The only OS-supplied state that
   reaches the monitor is `r0…r3`; `r6`/`r12`/imm4 are set by the veneer. So the
   OS can only assert something through `r2` (domain) — and `r2` takes at least
   three values across working callers (§Q3.1), so it is not an identity.
5. **`procnto`.** 0 `smc`. The microkernel never touches the monitor, so there is
   no kernel-level handshake our kernel is "supposed" to perform.
6. **`trustzone-omap4`** is a transparent proxy (§Q1.1a), not a gatekeeper: it
   forwards whatever the client asks and returns the monitor's `r0` unchanged
   (`0x8ae0: str r0,[r5]`).

**Conclusion [I]:** on the bytes, the monitor has no OS-identity channel that QNX
uses and our Linux omits. The rejection must be decided inside the monitor, on
`{r2, r3}` values or on monitor-internal state, neither of which is visible in
the corpus.

---

## 7. What we can now rule out (summary)

| hypothesis | verdict |
|---|---|
| wrong veneer / missing tag / wrong `smc` immediate | **ruled out** [P] — byte-identical to QNX and the bootloader (§0) |
| missing `r6=0xff` or wrong `ip` | **ruled out** [P] — set inside the veneer |
| wrong service number | **ruled out** [P] — 0x12/0x31/0x33/0x2F used verbatim by QNX |
| wrong domain `r2` | **ruled out** [P] — the monitor accepts ≥3 values (§Q3.1) |
| wrong command-page layout | **ruled out** [P] — identical field offsets and `|=0x400` rule (§Q3.3) |
| missing CM2/L4SEC clock setup | **ruled out** [P] — `0x7c58`/`0x2218` match; already replicated |
| a missing registration / init service | **no evidence one exists** [P] (§Q1.3, §Q1.4, Q6) |
| `0x2F op=1` as a required prerequisite | **ruled out** [P] (§Q4.3) |
| `crypto=enable` involvement | **ruled out** [P] (§Q5) |
| missing cache maintenance | **ruled out** [P] — ICIALLU + dsb/isb/dmb inside the veneer |
| **the monitor refuses on the command-page address / DRAM window** | **surviving** [I] |
| **the monitor needs internal secure-world state we cannot see** | **surviving** [U] |

---

## What to try next (read-only first)

Order matters: each in-guest `smc` that hangs costs one factory boot and a
~7.8 s WDT cycle (report 145 §5). Everything in steps 0–1 is free.

### Step 0 — static, no device (do this before spending any boot)

0.1 **Confirm how our image boots**, specifically whether the **MLO stage at file
    0x4000–0x8d70** — the Thumb library that calls services 0x09–0x3C through
    veneer `0x800043b4` — actually executes on our path, or whether the
    chainloader starts at 0x9000 and skips it. Our `bootblob_arm9000.bin` is
    exactly the 0x9000+ image and **begins with the ARM veneer**, so if the
    chainloader is loaded at 0x9000 the MLO stage is bypassed. This is the one
    remaining *structural* difference we can still test without the device.
    Read-only: confirm the section boundaries in the existing 1 MiB dump and
    check the chainloader's load address.
0.2 Resolve `trustzone-omap4`'s PLT → import names to settle whether `main`'s
    request loop starts before or after `resmgr_attach` (nice-to-have; does not
    change the conclusion).
0.3 Find the RPMB record path's *first* call in `rpmb-omap4` `main` (0x516c) to
    confirm `0x31` really runs at startup on TabletOS. **If TabletOS's own `0x31`
    also fails, this unit's monitor simply does not serve this family and the
    whole seal/unseal plan is dead** — that is the most valuable single finding
    still available, and it is purely static.

### Step 1 — static, decide between the two surviving hypotheses

The survivors differ in one testable way: **does the monitor reject on `r3`
(address) or on internal state?** Find a bootloader/MLO call whose *success* is
checked and whose command page we can locate, and recover the physical address it
passes as `r3`. If that address is in a narrow, specific band (e.g. the top few MB
of DRAM, or secure-visible SRAM below 0x80000000), that proves a restricted
window and hands us the address to use. If it is wherever malloc landed, the
window hypothesis weakens sharply.

### Step 2 — one in-guest probe image, three calls, no writes

Only if steps 0–1 do not settle it. Put all three probes behind `/dev/…` so one
boot tests all three. **No `0x2F` at all.**

| # | call | page | what a result means |
|---|---|---|---|
| **1 (first, log before anything else)** | `smc(0x3A, 0, domain=5, count=0)` — **zero-argument** service | none used | **ret == 0** ⇒ the monitor is reachable and the refusal is per-call/argument ⇒ the problem is the page/address, so go to #3. **0xFF05** ⇒ global rejection; stop chasing callers and go to #2 only to confirm the domain is irrelevant. |
| 2 | `smc(0x31, 0, domain=0, count=1, {0})` — the **exact bootloader form**; repeat with domain 5 | coherent page | **ret == 0** ⇒ the domain *was* the discriminator, contrary to §Q3.1; redo `0x33`/`0x2F` with that domain. **0xFF05** ⇒ domain irrelevant, confirming §Q3.1. |
| 3 | `smc(0x12, 0, domain=0, count=2, {phys(buf)−0x10000000, 16})` with the buffer allocated as **high** as QNX's page (aim NS phys ≥ 0xbf000000, i.e. monitor 0xaf000000) | high DRAM | **buffer changes** ⇒ window confirmed and we know a usable band ⇒ proceed to `0x33` then a *read-only* `0x2F` unseal of block 0x43 (never a seal). **0xFF05 and buffer untouched** ⇒ the window is not the issue; the monitor's state is. |

Interpretation key: #1 is the cheapest possible discriminator (no address, no
buffer, one register), and it must be issued **first** with its result logged, so
a hang in #2/#3 does not lose it.

### Step 3 — only if a seal is ever needed

Do not guess an address on `0x2F` (report 145 §4.3). Reproduce block 0x43's media
bytes with a read-only unseal first; only then consider the single `0x32` write.

---

## Explicit stop

All six questions are answered as far as the available bytes allow.

1. **Q1 — answered.** No registration/init SMC exists anywhere. Full enumeration:
   `rpmb-omap4` 6 sites all domain 4; `trustzone-omap4` 3 sites all domain 4 and
   all per-request; `procnto` 0; `devb-mmcsd` 0; bootloader Thumb 20 wrappers
   domain 5 except 0x12→0; bootloader ARM 28 sites domain 5 except 0x12→0; 5 ROM
   L2 services `r12 = 0x100/0x102/0x109/0x112`, documented in TRM §27.5.
2. **Q2 — cannot be answered from the available bytes.** 0xFF05 appears in no
   image and no TRM text, and the monitor image is absent. Proven: success == 0,
   so 0xFF05 is a genuine monitor error and not an OS artefact; the untouched 0x12
   buffer proves refusal precedes payload transfer; `r0` is excluded as the
   discriminator. Candidate validation inputs are `{r2, r3, r6, r12, imm4}`.
3. **Q3 — answered.** The only register difference is domain 5 (bootloader) vs 4
   (QNX), and it is not a required constant. Command-page layout identical. No
   extra registers, no OS personality.
4. **Q4 — answered.** `op=1` is a keyed 32→16 derivation with a second key, used
   only by the KEK-provisioning routine; **not** a prerequisite for `0x33` or
   `0x2F op=0`. The real dependency is `0x31` → `{0x2F, 0x33}`.
5. **Q5 — answered.** `crypto=enable` is `fs-qnx6`'s option; the driver has zero
   `smc`, no AES/HMAC, and does RPMB with raw MMC commands.
6. **Q6 — answered negatively.** No OS-type field, no registration structure, no
   handover service with a caller, no kernel-side handshake in procnto.

**Unanswerable from these bytes, and stated as such:** the meaning of 0xFF05; which
register/argument the monitor validates; what `0x2F op=1` computes; whether the
bootloader's own `0x31` succeeds on this unit; whether `trustzone`'s request loop
precedes `resmgr_attach`; which stack slot the bootloader's ARM wrapper reloads as
`r2`.
