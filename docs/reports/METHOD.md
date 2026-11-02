# REA / tooling method for the OMAP4430 boot ROM dump

Target: `/home/psyden/rea-work/bootdumps-2026-09-11/omap4430-bootrom-0x40028000.bin`
Base: 0x40028000, size 0x C000 (49152), SHA-256
`fc2ad391e3c3333d041cd325c1fd6fa64767effc02493c14b5b59452798b4961`

## Tooling status (updated 2026-10-19)

REA 6.7.0 is registered for Command Code and OpenCode. Native analysis now uses
Ghidra 12.1.4 at `/home/psyden/tools/ghidra_12.1.4_PUBLIC` with a full 64-bit
JDK 21 at `/usr/lib/jvm/java-21-openjdk`. The Command Code MCP registration in
`~/.commandcode/mcp.json` carries `GHIDRA_INSTALL_DIR`, `JAVA_HOME`, and
`REA_ANALYSIS_PROVIDER=ghidra`. Restart Command Code after registration changes;
the MCP server exposes 207 tools.

REA/Ghidra Evidence is available. The LLVM listings below remain an independent
cross-check and must be labelled LLVM-derived, **not** REA Evidence.

## REA / Ghidra recipe (primary)

CLI (runs the same Ghidra provider the MCP server uses):

```
npx -y rea-agents@6.7.0 analyze \
  /home/psyden/rea-work/bootdumps-2026-09-11/omap4430-bootrom-0x40028000.bin \
  --target-format raw-binary --architecture arm --entry-point 0x40028000 \
  --provider ghidra --json

npx -y rea-agents@6.7.0 decompile \
  /home/psyden/rea-work/bootdumps-2026-09-11/omap4430-bootrom-0x40028000.bin \
  0x40028000 \
  --target-format raw-binary --architecture arm --entry-point 0x40028000 \
  --provider ghidra --json
```

Export `GHIDRA_INSTALL_DIR`, `JAVA_HOME`, and `REA_ANALYSIS_PROVIDER=ghidra` in
the shell running the CLI (the MCP registration already has them).

MCP equivalent: `open_binary` with `format: "raw-binary"`,
`architecture: "arm"`, `entry_point: "0x40028000"`, `provider_id: "ghidra"`,
then `binary_overview`, `analyze_function`, or `procedure_pseudo_code`.

Raw imports use Ghidra language `ARM:LE:32:v7`; `REA_GHIDRA_LANGUAGE_ID` cannot
override a raw-binary import. Ghidra auto-analysis handles the mixed ARM/Thumb
image, but boundary claims still follow the mode map in
`60-mode-map-resolution.md`.

Verified setup evidence lives in `analysis/rea-ghidra/`:

- `overview.json` - one segment 0x40028000-0x40034000, 49152 bytes,
  218 procedures, 5 strings.
- `entry-40028000.json` - reset entry / CPU1 wait path / SMC / ARM-Thumb handoff.
- `thumb-4002a51c.json` - Thumb progress-bitmap setter (independent check).

## Independent LLVM cross-check

## Build the two wrappers (raw binary -> ELF at 0x40028000)

```
mkdir -p /tmp/opencode/rea-omap && cd /tmp/opencode/rea-omap
cat > wrap.s <<'EOF'
.syntax unified
.arm
.section .text
.global _start
_start:
.incbin "/home/psyden/rea-work/bootdumps-2026-09-11/omap4430-bootrom-0x40028000.bin"
EOF
llvm-mc -triple=armv7-none-eabi -mcpu=cortex-a9 -filetype=obj wrap.s -o wrap.o
ld.lld -Ttext=0x40028000 wrap.o -o wrap.elf
```

## Disassemble a range in a chosen mode

ARM:
`llvm-objdump -d --triple=armv7-none-eabi --mcpu=cortex-a9 --no-show-raw-insn --start-address=0x40028000 --stop-address=0x40028100 wrap.elf`

Thumb-2:
`llvm-objdump -d --triple=thumbv7-none-eabi --mcpu=cortex-a9 --no-show-raw-insn --start-address=0x40028000 --stop-address=0x40028100 wrap.elf`

`llvm-objdump --triple=...` **overrides** the ELF mapping, so one wrapper ELF is
enough; force the mode per range. Drop `--no-show-raw-insn` to see encodings.

Whole-file listings already generated:
`/tmp/opencode/rea-omap/arm.txt` (ARM), `thumb_full.txt` (forced Thumb-2).

## Python helpers (word dump / xref)

```python
import struct
d=open('/home/psyden/rea-work/bootdumps-2026-09-11/omap4430-bootrom-0x40028000.bin','rb').read()
base=0x40028000
w=lambda a: struct.unpack_from('<I',d,a-base)[0]
h=lambda a: struct.unpack_from('<H',d,a-base)[0]
```

xreffing a literal: scan all 4-byte offsets for LE words equal to the target
address (Thumb `ldr rX,[pc,#imm]` pools and ARM pools). Addresses may also appear
with bit0 set (Thumb function pointers) or as `0x0002xxxx` boot-alias values.

## Established mode map (verify per range, do not assume)

- ARM: 0x40028000-0x400284BF, 0x40028870-0x40028A17,
  0x4002CEB0-0x4002CEBB (stub), 0x4002F2B0-0x4002F550 (cache lib), plus at
  least one small ARM stub near 0x4002B0xx.
- Thumb-2: 0x400284C0-0x4002886F, 0x40028A18-0x4002CEAF,
  0x4002CEBC-0x4002F2AF, 0x4002F550-~0x4002FF20.
- Data/strings begin ~0x4002FF20; 0x40030000-0x40033FFF is tables/payload.
  Ignore disassembly in data regions (strings decode as plausible Thumb).

## Boundary / provenance notes

- Dump is the TRM 48-KB window at 0x40028000-0x40033FFF, NS-readable.
- A separate dump `bootrom-lower-0x40020000.bin` covers 0x40020000-0x40026F24;
  0x40026F28-0x40027FFF is a non-readable ~4-KB hole (monitor/SMC dispatch
  candidate). Only reference it as separate evidence.
- Prior work exists in `README.md` and `BOOTROM-RE.md` in the same directory.
  Treat it as *prior claims to verify*, not ground truth.
