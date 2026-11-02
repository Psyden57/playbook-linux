# 126 — RE: the QNX TC358764 bridge-GPIO self-test (`winch_lcdctl -t / -T`)

**Question.** Reproduce, statically, the TC358764 bridge-GPIO self-test that `winch_lcdctl`
runs for `-t <n>` / `-T <n>`, so it can be re-implemented from Linux: which bridge registers
it writes (in order), what the OMAP side is configured to and expected to read, what `-c`
changes, and whether anything on the bridge is read back.

**Answer in one paragraph.** The test is one small function at **vaddr `0x196dc`**, taking the
repetition count in `r0`. It enables the bridge's GPIO block with **one** 32-bit write,
`reg 0x0520 ← 0x00000005`, waits 10 µs, switches the two OMAP pins to **inputs** by setting
`GPIO2_OE` bit 4 and `GPIO6_OE` bit 11, then loops `n` times over four *patterns*. Each
pattern is a single write to **`reg 0x0524`** (`1`, `4`, `5`, `0`) followed by 10 µs and two
OMAP `DATAIN` reads. Writing `0x0524` bit 0 drives the bridge pin on the **GPIO36** net
(OMAP `GPIO2` bit 4); bit 2 drives the pin on the **GPIO171** net (OMAP `GPIO6` bit 11).
The observed OMAP level must equal the written bit in every case. **Nothing is read back from
the bridge** — no BTA, no SYS_ID, only OMAP `DATAIN`. `-c` sets the four
`PPI_DnS_CLRSIPOCOUNT` registers (`0x0164/0x0168/0x016C/0x0170`), instantiated **4** by
default, but it only takes effect in the *full* bridge init at `0x13e50`, which runs *after*
the test — so `-c` cannot influence the `-t` result.

---

## 1. Method

* Binary: `/home/psyden/playbook-linux/local/proprietary/dumped4869ifs/proc/boot/winch_lcdctl`
  (byte-identical to `/home/psyden/rea-work/playbook-dev/dumped4869ifs/proc/boot/winch_lcdctl`,
  193 024 B, 2011-09-28). Read-only; nothing under `/home/psyden/rea-work` was modified.
* QNX ARM `ELF32 ET_DYN`, stripped. First `PT_LOAD`: `off=0x0 vaddr=0x0 filesz=0x28048`
  (`r-x`) ⇒ **text vaddr == file offset**. RW `PT_LOAD`: `off=0x28348 vaddr=0x29348`
  (`filesz 0x525c`, `memsz 0x5bb8`) ⇒ **file offset = vaddr − 0x1000** for that segment, and
  BSS runs up to `0x2EF00`.
* Raw disassembly of the first segment:
  `arm-linux-objdump -D -b binary -m arm --adjust-vma=0 <first-PT_LOAD slice>`
  (objdump: `/home/psyden/SSDdrive/wsl-migration/home/psyden/toolchains/armv7-eabihf/bin/arm-linux-objdump`).
* **PIC anchor `r4 = 0x2B000`** (= `DT_PLTGOT`, confirmed in the dynamic section) is
  re-materialised in every function as `ldr r4,[pc,#imm]` + `add r4,pc,r4`. Verified twice:
  self-test `0x196e4`/`0x196e8` → `0x196f0 + 0x11910 = 0x2B000`; `main` `0x1a66c`/`0x1a670`
  → `0x1a678 + 0x10988 = 0x2B000`. Therefore **a `pc`-relative literal whose value is small
  (e.g. `0x164`) is a GOT-slot offset; small value + `0x2B000` = slot address, and the slot
  holds a pointer to the real variable. A negative-valued literal + `0x2B000` is a direct
  string address.**
* PLT stubs were decoded (`add ip,pc,#0 ; add ip,ip,#0x26000 ; ldr pc,[ip,#imm]` ⇒ slot =
  site+8+0x26000+imm) and named from `DT_JMPREL` (`0x3e98`, 61 × 8 B, `DT_SYMTAB 0x4f0` /
  `DT_STRTAB 0xc80`). The names used below:
  `0x13ce0` = bridge generic-write helper (local), `0x40f0` = **usleep**, `0x40a8` = **printf**,
  `0x4120` = **puts**, `0x424c` = **slogf**, `0x427c` = **getopt**, `0x4150` = **strtoul**,
  `0x4330` = **exit**.
* GOT slots relevant here: `0x2B164 → *0x2B1D4` (display/GPIO handle; `+0x118` = mapped base
  of OMAP **GPIO2**, `+0x138` = mapped base of OMAP **GPIO6**, `+0x118/+0x138` == 0 means
  "not mapped"), `0x2B180 → 0x2EABC` (`-t/-T` count), `0x2B168 → 0x2EAC0` (`-T` exit-2 flag),
  `0x2B178 → 0x2E55C` (`-c` value), `0x2B11C → 0x2EA9C` (verbosity counter, options struct
  base = `0x2EA9C`).
* `0x2EA9C` (the options/verbosity word) is in BSS, i.e. it loads as 0 unless `-v` is given;
  `main` increments it at `0x1a85c` and the self-test reads it at `0x198b0`.

---

## 2. The self-test sequence

Entry **`0x196dc(int n)`** (only call site in the whole image: **`0x19364`**; there is no
function-pointer/literal reference to `0x196dc` anywhere else).

### 2.1 Prologue and OMAP direction setup

| # | vaddr | instruction(s) | effect |
|---|---|---|---|
| 1 | `0x196e4`/`0x196e8` | `ldr r4,[pc,#1460]`; `add r4,pc,r4` | `r4 = 0x2B000` (PIC anchor). `0x196ec: mov r9,r0` saves `n`. |
| 2 | `0x196f0`–`0x196f8` | `mov r0,#0x520`; `mov r1,#5`; `bl 0x13ce0` | **bridge write `0x0520 ← 0x00000005`** — GPIO enable/direction for pins 0 and 2, i.e. exactly the two pins the test then drives (register *name* inferred, behaviour proven; see §5.1) |
| 3 | `0x196fc`/`0x19700` | `mov r0,#10`; `bl 0x40f0` | **`usleep(10)` = 10 µs** |
| 4 | `0x1974c`–`0x19758` | `…ldr r5,[r3,#0x118]`; `add r5,r5,#0x134` | r5 = GPIO2 base + `0x134` |
| 5 | `0x197a4`–`0x197bc` | `ldr r3,[r3,#0x118]`; `add r3,r3,#0x134`; `ldr r3,[r3]`; `orr r3,r3,#0x10`; `str r3,[r5]` | **`GPIO2_OE (+0x134) |= 0x00000010`** → bit 4 = global **GPIO36** becomes an **input** |
| 6 | `0x19808`–`0x19814` | `…ldr r5,[r3,#0x138]`; `add r5,r5,#0x134` | r5 = GPIO6 base + `0x134` |
| 7 | `0x19860`–`0x19878` | `ldr r3,[r3,#0x138]`; `add r3,r3,#0x134`; `ldr r3,[r3]`; `orr r3,r3,#0x800`; `str r3,[r5]` | **`GPIO6_OE (+0x134) |= 0x00000800`** → bit 11 = global **GPIO171** becomes an **input** |

(The "not mapped" branches at `0x19704`–`0x19744`, `0x1975c`–`0x197a0`, `0x197c0`–`0x19804`,
`0x19818`–`0x1985c` are the compiler's `mmap`-null checks; they call
`slogf(99,1,"not mapped %d function %s ",line,"mt_tc358764")` and are not part of the test.)

OE convention (TRM *swpu231ap*, quoted in report 122 §0): **OE bit = 1 ⇒ input, 0 ⇒ output**.
`orr` (setting the bit) therefore makes the pin an input, which is what a read-back test needs.

### 2.2 The loop (`for (i = 0; i < n; i++)`, `r7` = i, `r5` = fail counter)

Loop setup `0x1987c`–`0x198a8` (`r7 = 0`, `r5 = 0`, `r6 = 0x2EA9C` verbose,
`fp = 0x27660 "DSI test tries  %d\n"`), then `b 0x19c38` (test-at-bottom, so `n ≤ 0` runs
nothing and returns "pass").

| stage | bridge write | vaddr of the write | delay | OMAP read 1 (GPIO2 `DATAIN` bit 4 = GPIO36) | OMAP read 2 (GPIO6 `DATAIN` bit 11 = GPIO171) | "pass" print |
|---|---|---|---|---|---|---|
| — | `0x0524 ← 0x00000001` | `0x198c8`–`0x198d0` | `usleep(10)` `0x198d4` | **must be 1** (`tst #0x10`, `beq 0x199a0` fail @`0x19928`) | **must be 0** (`tst #0x800`, `bne 0x199a0` fail @`0x19980`) | `puts("DSI stage 1 pass")` @`0x19990` |
| — | `0x0524 ← 0x00000004` | `0x199a4`–`0x199ac` | `usleep(10)` `0x199b0` | **must be 0** (`bne 0x19a7c` fail @`0x19a08`) | **must be 1** (`beq 0x19a7c` fail @`0x19a5c`) | `puts("DSI stage 2 pass")` @`0x19a6c` |
| — | `0x0524 ← 0x00000005` | `0x19a80`–`0x19a88` | `usleep(10)` `0x19a8c` | **must be 1** (`beq 0x19b58` fail @`0x19ae4`) | **must be 1** (`beq 0x19b58` fail @`0x19b38`) | `puts("DSI stage 3 pass")` @`0x19b48` |
| — | `0x0524 ← 0x00000000` | `0x19b5c`–`0x19b64` | `usleep(10)` `0x19b68` | **must be 0** (`bne 0x19c34` fail @`0x19bc0`) | **must be 0** (`bne 0x19c34` fail @`0x19c14`) | `puts("DSI stage 4 pass")` @`0x19c24` |

Each failed stage does `add r5,r5,#1` (`0x199a0`, `0x19a7c`, `0x19b58`, `0x19c34`) and skips
its "stage N pass" line. Bottom test `0x19c38: cmp r7,r9 ; blt 0x198ac`.

### 2.3 Register-write list, in order, exactly as performed

Payload format of every bridge write (helper `0x13ce0` at `0x13ce8`–`0x13d2c`): 6-byte DSI
**generic long write** `{ reg_lo, reg_hi, v0, v1, v2, v3 }` (little-endian), `DT = 0x29`,
`WC = 6`, sent through DSI **VC1**'s register block (`base + 0x4108 + vc*0x20`) with the DSI
packet VC field **0** (`0x13ae8` builds the header word `0x29 | (6<<8) | (0<<24)` at
`0x13b7c`–`0x13b84`; the helper passes channel `1` at `0x13d20`, the VC byte `0` at `0x13d24`).

The test itself performs exactly **five** bridge writes:

| order | vaddr | register | value | meaning proven by the code |
|---|---|---|---|---|
| 1 | `0x196f0` | `0x0520` | `0x00000005` | GPIO-block enable for pins 0 and 2 (both are driven afterwards) |
| 2 | `0x198c8` | `0x0524` | `0x00000001` | drive pin **0 high**, pin 2 low |
| 3 | `0x199a4` | `0x0524` | `0x00000004` | drive pin **0 low**, pin 2 **high** |
| 4 | `0x19a80` | `0x0524` | `0x00000005` | drive **both high** |
| 5 | `0x19b5c` | `0x0524` | `0x00000000` | drive **both low** |

That is every bridge transaction of the test (a BL-target sweep over `0x196dc…0x19c9c` finds
only `0x13ce0`, `0x40f0`, `0x40a8`, `0x4120`, `0x424c`). `0x0520` is written **once**, before
the loop; `0x0524` is rewritten each stage. **Nothing is restored after the loop** — the test
leaves both bridge GPIOs low. (The full bridge init `0x13e50`, which the caller runs next at
`0x1938c`, ends by writing `0x0520 ← 5` (`0x13fdc`) and `0x0524 ← 5` (`0x13fe8`), i.e. both
pins high again; the in-tree Linux driver does the same pair
`local/kernel-tree/drivers/gpu/drm/bridge/tc358764.c:283-284`.)

Pin ↔ net assignment (`0x0524` bit → OMAP pin) is proven by the four stages: bit 0 always
tracks the GPIO2-bit-4 read and bit 2 always tracks the GPIO6-bit-11 read, in both polarities
and in the both-high / both-low patterns.

### 2.4 Loop count, delays, return value

* **Loops `n` times** — `n` is the function argument (`r9`), from the `-t/-T` argument
  (see §4). `n ≤ 0` ⇒ no iterations ⇒ pass.
* **Delays**: `usleep(10)` (10 µs) after the initial `0x0520` write, and one `usleep(10)`
  after each `0x0524` write. So per iteration: 4 bridge writes, 4 × 10 µs, 8 `DATAIN` reads.
  No other delay, no polling, no debounce, no retry, no timeout anywhere in the function.
* **Return**: `r0 = 1` if the fail counter is 0 (`0x19c74`–`0x19c94`), else `r0 = 0`
  (`0x19c48`–`0x19c70`). Output is gated on verbosity (options word at `0x2EA9C`):
  `verbose == 0` ⇒ silent; `verbose ≥ 1` ⇒ `puts("\nDSI - TEST PASSED\n")` or
  `printf("\nDSI - TEST FAILED\nFails %d\n", fails)`; `verbose > 2` ⇒ additionally
  `printf("DSI test tries  %d\n", i+1)` per iteration and `puts("DSI stage N pass")` per
  passing stage.

---

## 3. Expected OMAP reads

| pin | bank / bit | register | pad (report 122) | expected |
|---|---|---|---|---|
| **GPIO36** | GPIO2 bit 4 (`0x00000010`) | `GPIO2 + 0x138` (`DATAIN`) | `CTRL_CORE_PAD 0x058` (gpmc_ad12), mux **3** | follows `0x0524` bit 0 |
| **GPIO171** | GPIO6 bit 11 (`0x00000800`) | `GPIO6 + 0x138` (`DATAIN`) | `CTRL_CORE_PAD 0x17C` (kpd_col3), mux **3** | follows `0x0524` bit 2 |

Expected matrix per iteration (identical every iteration):

| `0x0524` written | GPIO36 (`GPIO2.DATAIN & 0x10`) | GPIO171 (`GPIO6.DATAIN & 0x800`) |
|---|---|---|
| `0x00000001` | **1** | **0** |
| `0x00000004` | **0** | **1** |
| `0x00000005` | **1** | **1** |
| `0x00000000` | **0** | **0** |

**Pass condition:** across all `n` iterations, **all 8 reads per iteration** match the table
(address decoding: `tst`+conditional branch; see the vaddrs in §2.2). Any single mismatch
increments the fail counter once; the test still runs to completion (it is not fail-fast), and
the count is reported as "Fails %d".

Live corroboration: report 122 §7.7/§0 records both pins reading **high** on the reference
unit, consistent with the bridge init leaving `0x0524 = 5`.

Register-offset note: this code **read-modify-writes `+0x134`** (a direction register — a RMW
on a data register would be meaningless) and **only reads `+0x138`**. `+0x194` is used
elsewhere in the binary as `SETDATAOUT` (report 122 §7). So within this image
`0x134 = GPIO_OE`, `0x138 = GPIO_DATAIN`, and `0x13C` behaves like the readable
`GPIO_DATAOUT` (`0x1b7ac` reads `GPIO2+0x13C` and tests bit 13 = GPIO2_13, which the display
init set through `SETDATAOUT` at `0x192d4`).

---

## 4. Invocation and flags (`-t`, `-T`, `-c`)

Option parsing is a plain `getopt` loop in `main` (`0x1a664`), optstring at `0x27A34`:
**`vbrspR:t:T:f:l:x:y:c:U:`**. Options live in a struct at **`0x2EA9C`** (`+0x00` verbosity,
`+0x14` `-b`, `+0x18` `-r`, `+0x1C` `-s`, `+0x20` test count, `+0x24` `-T` flag, `+0x28` `-p`).
Separate globals: `0x2E548` board id (1 = Winchester, 2 = Colt), `0x2E54C` `-R`, `0x2E550` `-l`,
`0x2E554` `-x`, `0x2E558` `-y`, `0x2E55C` `-c`.

| flag | vaddr | code | effect |
|---|---|---|---|
| `-t <n>` | `0x1a8b0`–`0x1a8c4` | `strtoul(optarg,NULL,0)` → `[r5,#0x20]` (`0x2EABC`) | repetition count only |
| `-T <n>` | `0x1a8cc`–`0x1a8e4` | same store to `0x2EABC`, **plus** `mov r9,#1`; `str r9,[r5,#0x24]` (`0x2EAC0`) | count **and** "exit 2 on failure" |
| `-c <v>` | `0x1a950`–`0x1a964` | `strtoul(optarg,NULL,0)` → `0x2E55C` | `DxS_CLRSIPOCOUNT` value (§4.2) |

`-t/-T` have **no dedicated code path**. The count is consumed inside the display bring-up
function **`0x19020`** (single site `0x19350`–`0x19388`):

```
0x19350  ldr r3,[0x2B180] ; ldr r0,[r3]        ; r0 = -t/-T count
0x1935c  cmp r0,#0 ; beq 0x1938c              ; count == 0 → no test
0x19364  bl 0x196dc                           ; run the self-test
0x19368  cmp r0,#0 ; bne 0x1938c              ; pass → continue
0x19370  ldr r3,[0x2B168] ; ldr r3,[r3]       ; -T flag?
0x1937c  cmp r3,#0 ; beq 0x1938c              ; no → continue anyway
0x19384  add r0,r0,#2 ; bl 0x4330             ; exit(2)
0x1938c  bl 0x13e50                           ; full bridge init
```

Consequences:
* `-t n` / `-T n` must be combined with a command that runs the display bring-up:
  `0x19020` is called from the wrapper `0x1c900` at `0x1cc78`, and `0x1c900` is called from
  `main` at `0x1b0c0`, `0x1b540`, `0x1b894` and `0x1b998` (the `power_on` / `splash` /
  `shared` command handlers; `puts("Splash")` @`0x1b518`/`0x1b988`, `"Shared Buffer look for
  /dev/shmem/framebuffer"` @`0x1b88c`). Practical form: **`winch_lcdctl -t 3 power_on`**
  (or `-T 3`), never bare `-t`.
* With `-t` (no `-T`) a failure is only printed; init continues. With `-T` a failure ends the
  process with **exit code 2** (matches the usage text "Exit with 2 on failure").
* The test runs **before** the full bridge init (`0x1938c`), i.e. before `0x13e50` programs
  PPI/DSI/LVDS.

### 4.1 A *different*, related test: the `status` command

The strings `"Fail GPIO36 off"` (`0x27610`), `"Fail GPIO171 off"` (`0x27620`) and
`"GPIO status good (GPIO36 and GPIO171 high)"` (`0x27634`) do **not** belong to `0x196dc`.
They live in a second function **`0x19418`** (single call site `0x1b7e0`, the `status`
command — string `"status"` @`0x27B40`; the help line `"Get DSI to LVDS Bridge GPIO status"` is
printed from `0x1b7d4` just before the call):

* sets `GPIO2_OE |= 0x10` (`0x194d4`/`0x194d8`) and `GPIO6_OE |= 0x800` (`0x19588`/`0x1958c`),
* reads `GPIO2.DATAIN & 0x10` → if 0: `puts("Fail GPIO36 off")` and **`exit(1)`** (`0x19600`/`0x19610`),
* reads `GPIO6.DATAIN & 0x800` → if 0: `puts("Fail GPIO171 off")` and **`exit(1)`** (`0x19684`/`0x19694`),
* otherwise `puts("GPIO status good (GPIO36 and GPIO171 high)")` (`0x196a8`) and **`exit(0)`**
  (`0x196b4`) — it *always* exits, it never returns,
* it **writes nothing to the bridge** (no `0x13ce0` call at all) and requires LCD power already
  on: `0x1b79c`–`0x1b7c4` tests `GPIO2.DATAOUT(+0x13C) & 0x2000` (GPIO2_13) and prints
  `"LCD Power off no status"` and skips otherwise.

The two `puts` for the fail/good lines are themselves gated on `verbose > 1` (word at
`0x2EA9C`, `cmp #1` at `0x1967c`/`0x196a0`) — with `-v` you get no text, only the exit code.

### 4.2 What `-c` changes, and the instantiated value

`0x2E55C` (the `-c` value) is referenced at exactly **one** instruction in the whole image —
`0x13e74` (`ldr r3,[pc,#408] @ 0x14014` = `0x178` → `0x2B178 → 0x2E55C`), inside the full
bridge init `0x13e50`:

```
0x13e74  ldr r3,[0x2B178] ; ldr r4,[r4,r3]     ; r4 = &(-c value)
0x13e7c  mov r0,#0x164 ; ldr r1,[r4] ; bl 0x13ce0   ; PPI_D0S_CLRSIPOCOUNT ← v
0x13e88  mov r0,#0x168 ; ldr r1,[r4] ; bl 0x13ce0   ; PPI_D1S_CLRSIPOCOUNT ← v
0x13e94  mov r0,#0x16C ; ldr r1,[r4] ; bl 0x13ce0   ; PPI_D2S_CLRSIPOCOUNT ← v
0x13ea0  mov r0,#0x170 ; ldr r1,[r4] ; bl 0x13ce0   ; PPI_D3S_CLRSIPOCOUNT ← v
```

So `-c v` sets all four lane assertion timers to the same 32-bit value (`strtoul` base 0).
**Instantiated (default) value = 4**: the variable at `0x2E55C` is statically initialised to
`0x00000004` in the RW image (read of file offset `0x2D55C`). That is independently confirmed
by the in-tree Linux driver, which writes 4 to all four registers
(`local/kernel-tree/drivers/gpu/drm/bridge/tc358764.c:250-253`,
`PPI_D0S_CLRSIPOCOUNT 0x0164 … PPI_D3S_CLRSIPOCOUNT 0x0170` at lines 33-36). Note `0x13e50`
runs **after** the self-test, so `-c` cannot change the self-test's outcome in the same run; it
affects the bridge's HS-receive timing from that point on (and in any later `power_on`).

Also from `0x13e50`, for context: the register block is `0x013C ← 4` (`PPI_TX_RX_TA`),
`0x0114 ← 4` (`PPI_LPTXTIMECNT`), `0x0210 ← 0x1f`, `0x0134 ← 0x1f`, `0x0104 ← 1`,
`0x0204 ← 1`, `0x04A4 ← 0`, `0x04A0 ← 0x0044C02D`, LVDS maps, `VP_*`, `usleep(100)` (100 µs,
`0x13fb8`), then `0x04A0 ← 0x0004C02D`, `0x049C ← 1`, **`0x0520 ← 5`**, **`0x0524 ← 5`**,
`0x0504 ← 4` (`SYS_RST_LCD`), `0x0464 ← 1`.

### 4.3 Is anything read back from the bridge (BTA)?

**No.** The self-test's complete bridge traffic is the five 6-byte generic long writes listed
in §2.3. Its call set is `0x13ce0` (write), `0x40f0` (usleep), `0x40a8` (printf),
`0x4120` (puts), `0x424c` (slogf) — no read helper, no `SYS_ID` (`0x0580`) access, no BTA
request. The write helper `0x13ce0` → `0x13ae8` only fills the VC TX FIFO and polls a VC
status word for bit `0x10000` (returns −1 on error); it neither requests nor waits for a
bridge reply. Even the failure path reads only OMAP `DATAIN`. So the test is a pure
"write bridge → observe OMAP pin" loop, which is exactly why it can pass on a bridge whose DSI
*read* path (BTA) is broken.

---

## 5. What remains uncertain

1. **Register names of `0x0520` / `0x0524`.** The code proves the *behaviour* (writing `0x0524`
   drives the pins; `0x0520 ← 5` is written once before the pins are driven and enables exactly
   bits 0 and 2), but neither the binary nor the in-tree Linux driver names these two registers
   — the driver writes them as bare `0x0520`/`0x0524` (`tc358764.c:283-284`). The TC358764
   datasheet is not in this workspace. **Missing evidence:** datasheet §GPIO register map
   (0x0500 block) to say whether `0x0520` is GPIOEN/GPIO direction and `0x0524` GPIOOUT.
2. **Which GPIO bank sits in which handle field** is inferred, not proven: `+0x118` is taken to
   be GPIO2 (`0x48055000`) and `+0x138` to be GPIO6 (`0x4805D000`) because (a) the pin checked
   on `+0x118` bit 4 is called "GPIO36" and (b) 32·1+4 = 36 while 32·5+11 = 171. The actual
   `mmap` bases are loaded at runtime by the QNX display resource manager. **Missing
   evidence:** the mmap/init code that fills `*0x2B1D4 + 0x118` and `+0x138`, or a live dump
   of those two words. (Report 122 §7.7 reached the same assignment.)
3. **Bridge DSI-RX state at test time.** The test issues DSI generic writes *before*
   `0x13e50` programs/starts the bridge's PPI/DSI (`0x0104`, `0x0204`). Statically we cannot
   tell whether the bridge accepts the `0x0520`/`0x0524` writes on a cold bridge, or only
   because a previous `power_on` left DSI-RX running. **Missing evidence:** a `-t` run log on
   the reference, or the bridge's POR default for the DSI-RX start bit.
4. **Pad muxing** is not done by `winch_lcdctl` (report 122 §0: the QNX boot set never writes
   `CTRL_CORE_PAD`), so the test silently depends on the RIM bootloader having muxed
   `0x058`/`0x17C` to GPIO mode 3. If a Linux kernel has remuxed either pad, the test will fail
   for reasons unrelated to the bridge.
5. `usleep(10)` is 10 µs by the QNX libc prototype; whether the real hardware needs *more*
   settling time between the DSI write and the OMAP read cannot be determined statically
   (there is no retry loop, so the reference evidently treats 10 µs + packet time as enough).
6. Not investigated (out of scope per the task): the `0x17094` call that precedes the test, and
   the meaning of the two `slogf` "not mapped" paths.

---

## 6. How to reproduce from Linux

Everything below is the minimum faithful port of `0x196dc`; the OMAP side is plain MMIO, the
bridge side needs a working DSI generic-long-write path with BTA **not** required.

```
/* 1. pads: gpio_36 = CTRL_CORE_PAD 0x4A100000+0x058 (gpmc_ad12) mode 3
 *          gpio_171 = 0x4A100000+0x17C (kpd_col3) mode 3
 *    (QNX never writes these; only needed if our kernel remuxed them)   */

/* 2. OMAP direction: OE bit = 1 means input.
 *    Bases 0x48055000 (GPIO2) / 0x4805D000 (GPIO6) are the OMAP4430 TRM values used in the
 *    QNX base-address tables (reports 118/122); the offsets come from this binary.       */
#define GPIO2 0x48055000
#define GPIO6 0x4805D000
#define GPIO_OE     0x134
#define GPIO_DATAIN 0x138
w32(GPIO2+GPIO_OE, rd32(GPIO2+GPIO_OE) | 0x00000010);   /* GPIO36  <- input */
w32(GPIO6+GPIO_OE, rd32(GPIO6+GPIO_OE) | 0x00000800);   /* GPIO171 <- input */

/* 3. bridge generic long write: DT 0x29, WC 6, payload {reg_lo,reg_hi,v0..v3},
 *    DSI VC1 register block, packet VC field 0                         */
dsi_gen_write(0x0520, 0x00000005);
usleep(10);

/* 4. four patterns x n iterations, 10 us after each write */
for (i = 0; i < n; i++) {
    static const uint32_t pat[4] = { 1, 4, 5, 0 };
    for (s = 0; s < 4; s++) {
        dsi_gen_write(0x0524, pat[s]);
        usleep(10);
        int g36  = !!(rd32(GPIO2+GPIO_DATAIN) & 0x00000010);
        int g171 = !!(rd32(GPIO6+GPIO_DATAIN) & 0x00000800);
        if (g36  != !!(pat[s] & 1)) fails++;
        if (g171 != !!(pat[s] & 4)) fails++;
    }
}
/* pass iff fails == 0 ; exit(2) instead of continuing if you want -T semantics */
```

Checks that must hold, and what a failure means:

| symptom | meaning |
|---|---|
| GPIO36 never follows bit 0 (or GPIO171 never follows bit 2) | pad not muxed to GPIO, or the OMAP `DATAIN` address/bit is wrong, or the bridge never received the writes |
| all four stages fail in the same direction (both stuck high) | bridge GPIO block never enabled (`0x0520` write lost) — or the bridge is held in reset / unpowered |
| stage 1/2 cross-talk (both high or both low regardless of pattern) | `0x0524` writes are not landing (DSI RX not started / wrong VC) |
| passes once but not on a cold boot | see §5.3 — the bridge's DSI-RX state before the test |

Note the test's absolute polarity, which is easy to get backwards: `0x0524` bit set ⇒ the OMAP
pin reads **1**, because the bridge GPIO drives the net high and the OMAP pin is an **input**
(`OE` bit = 1). And do not forget that the reference writes `0x0520 ← 5` exactly once — with
`0x0520 = 0` the bridge pins are not driven and all four stages fail identically.

---

## 7. Evidence index (vaddr → fact)

| vaddr | fact |
|---|---|
| `0x196dc` | self-test entry; `r0` = iteration count |
| `0x196f0`–`0x196f8` | `0x0520 ← 5` |
| `0x196fc`–`0x19700` | `usleep(10)` |
| `0x197b4`–`0x197bc` | `GPIO2+0x134 |= 0x10` |
| `0x19870`–`0x19878` | `GPIO6+0x134 |= 0x800` |
| `0x198c8` / `0x199a4` / `0x19a80` / `0x19b5c` | `0x0524 ← 1 / 4 / 5 / 0` |
| `0x198d4` / `0x199b0` / `0x19a8c` / `0x19b68` | `usleep(10)` × 4 |
| `0x19914`–`0x19928`, `0x19968`–`0x19980` | stage 1 reads + tests (GPIO2 b4, GPIO6 b11) |
| `0x199f0`–`0x19a08`, `0x19a44`–`0x19a5c` | stage 2 reads + tests |
| `0x19acc`–`0x19ae4`, `0x19b20`–`0x19b38` | stage 3 reads + tests |
| `0x19ba8`–`0x19bc0`, `0x19bfc`–`0x19c14` | stage 4 reads + tests |
| `0x19c38` | loop back-edge (`cmp r7,r9; blt`) |
| `0x19c48`–`0x19c70` | fail path: `printf("\nDSI - TEST FAILED\nFails %d\n", fails)`, return 0 |
| `0x19c74`–`0x19c94` | pass path: `puts("\nDSI - TEST PASSED\n")`, return 1 |
| `0x19350`–`0x19388` | the only caller: count gate, test, `-T` ⇒ `exit(2)`, then `0x13e50` |
| `0x19418`–`0x196b8` | `status` command: OE setup, 2 reads, `"Fail GPIO36 off"`/`"Fail GPIO171 off"`/`"GPIO status good…"`, always `exit()` |
| `0x1a8b0` / `0x1a8cc` / `0x1a950` | `-t` / `-T` / `-c` handlers in `main` |
| `0x13e74`–`0x13ea8` | the only consumer of `-c`: `0x164/0x168/0x16C/0x170 ← v` |
| `0x2E55C` (init data `0x00000004`) | default `DxS_CLRSIPOCOUNT` value |
| `0x13ce0` | 6-byte generic long write, DT `0x29`, VC1 block, packet VC 0 |
