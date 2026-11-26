# SGX540 port to `powervr_sgx` — our patch series

The durable record of the open-stack SGX540 port.  The working clone is
**gitignored** at `local/work/powervr_sgx` (upstream
`github.com/The1TrueJoe/powervr_sgx`, commit `95f78ca` plus these patches), so
the patches live here instead.  Apply them in order, from the clone root:

```sh
cd local/work/powervr_sgx
git checkout -- .                 # start from the pristine clone
for p in ../../../rootfs/powervr-sgx/*.patch; do git apply --check "$p" && git apply "$p"; done
```

## Nothing here is upstream yet

**Do not open an upstream issue or PR until the core runs on the unit**
(DECISIONS §110).  These patches are developed and reviewed locally; the public
artefact is written only once there is a hardware result to attach.  The
project's own rule is that a core is acceptable "as long as what it claims is
tested".

## Series

| patch | what |
|---|---|
| `0001-sgx540-core-entry.patch` | the core entry: `sgx540_regs`, `sgx540_heaps`, `sgx540_init_regs`, `pvrsgx_cmd_core_sgx540`, the `cores[]` part, the `of_device_id` row, the Kconfig text and a KUnit case |
| `0002-sgx540-one-context-list0.patch` | the microkernel's one-context path: list 0 instead of `DIR_LIST_BASE1`/`BIF_BANK0` (neither exists on this core), the `pdtable` slot resolution, the `SINGLE_CONTEXT`/`SECURE` feature bits, and the `test_pd_table_load` case |

## What `0002` established (session 74)

Four SGX545-shaped assumptions in the open image, each corrected from a GPL-2
primary source; full detail in `docs/reference/sgx540-ddk18-facts.md` and
`docs/captures/sgx540-ukernel-{list0,start,first-job}-2026-11-26.txt`.

| the image assumed | the SGX540 has | effect of the fix |
|---|---|---|
| client PD in `BIF_DIR_LIST_BASE1` (0xc38), routed via `BIF_BANK0` (0xc78) | **neither register exists** — one directory list, `DIR_LIST_BASE0` 0xc84 | the image now loads list 0 and writes no bank register |
| a HW context's `page_dir` word is a directory address | a **slot** into the driver's PD table at `kernel_code.base + 0xf000` (`pdtable_init`) | resolved through a new `pdtable` patch site (`r14`) |
| `features` = SECURE, no pdtable patch | single-context: `SINGLE_CONTEXT` set, `SECURE` clear, pdtable patch required | the driver's `ukfw_load` now accepts the image |
| `EUR_CR_PDS_CACHE_STATUS` (0x138) masks `0x27`/`0x4b` (two groups) | **one** group, `CSC_INV` b0 / `DSC_INV0` b1 / `DSC_INV1` b2 / `DSC_INV3` b4 | mask `0x1F`; **the NULL job went from `consumed 0` to `1/1 passed`** |

Every change is behind the per-core `SINGLE_CONTEXT` flag, so the SGX545 serial
image is still byte-identical (`83b5b0a7…`, 14536 B) and `mkukfw.py --check`
passes on the wrapped SGX540 image (`features 0x8`, both `ctlblk` and
`pdtable` patches).

**Measured on the unit, same session:** the driver binds, accepts the image
(`open microkernel … features 0x8`), starts the microkernel
(`microkernel started`) and one NULL job completes per GPU reset, with
`BIF_INT_STAT 0x8000` (no fault), `uk-lockups 0`, and a clean recovery from the
remaining timeout (`microkernel started again`).  Starting it needs
`modprobe powervr_sgx edm_init=1` on a cold module load — the §9 single-context
launch path, which the driver documents for exactly this core class.

## What `0001` was validated against (session 73)

| check | result |
|---|---|
| Compiles against the real 7.3.3 tree (`local/kernel-tree`, ARMv7) | **23/23 objects, zero warnings** (21 in `powervr_sgx-y` + `debugfs`/`pm`) |
| Driver's own KUnit suite, x86_64 (`powervr_sgx_core`) | **8/8 pass**, including `core_test_sgx540` |
| Negative control (one expectation forced wrong) | fails exactly there — the test reads the entry |
| Heap layout: `heaps_fit()`, `pds_reach()`, non-overlap | `tools/bench/gpu/sgx540-heaps-check.py`: pass |
| Boots on the unit: the driver identifies the core | `SGX core id 0x01140000 revision 0x00010200: TI OMAP4430 (SGX540 1.2.0)` (`docs/captures/sgx540-bind-2026-11-26.txt`) |

**The ARM build above was made from `local/pristine/linux-7.3.3`, which is NOT a
PlayBook tree — flashing it fails to boot (trap 378).**  It proves the driver
compiles and links for ARM; it does **not** produce a runnable image.  A runnable
one must be built from `local/kernel-tree` (the tree with
`kernel/patches/0001-playbook-winchester-7.3.3.patch` applied), or from a pristine
copy with that patch applied first.

A bench kernel needs four helpers our bench config never enabled, or the link
fails at modpost with `symbol 'drm_gpuvm_exec_lock' undefined` (trap 375):

```
CONFIG_DRM_EXEC=y
CONFIG_DRM_GPUVM=y
CONFIG_DRM_SCHED=y
CONFIG_DRM_GEM_SHMEM_HELPER=y
CONFIG_DRM_POWERVR_SGX=y
CONFIG_DRM_POWERVR_SGX_OF=y
```

Build **from a PlayBook tree** (not pristine), with the initramfs path absolute
when using `O=`:

```sh
export PATH=$PWD/local/toolchains/armv7-eabihf/bin:$PATH
make -C local/kernel-tree O=$PWD/local/work/kbench ARCH=arm \
     CROSS_COMPILE=arm-buildroot-linux-gnueabihf- -j12 \
     CONFIG_INITRAMFS_SOURCE=$PWD/initramfs/root vmlinux
```

Reproduce the KUnit run (x86_64, the driver's own suite):

```sh
# sync the driver in-tree (the clone's cmd_defs files are symlinks)
cp -RL --remove-destination local/work/powervr_sgx/drm/powervr_sgx/. \
      local/pristine/linux-7.3.3/drivers/gpu/drm/powervr_sgx/
# add powervr_sgx to drivers/gpu/drm/{Kconfig,Makefile} if absent
local/pristine/linux-7.3.3/tools/testing/kunit/kunit.py run --arch=x86_64 \
  --build_dir=$PWD/local/work/kunit/build-x86 \
  --kunitconfig=$PWD/local/work/kunit/kunitconfig-x86 'powervr_sgx_core'
```

## What is still a guess in `0001` (do not treat as fact)

* `use_code_base_addr_shift = 8` — the field is 24-bit with DM at b24; a byte
  unit cannot reach `0x0e400000`, so 256-byte units are assumed (the SGX530's).
  Fact sheet Gap 7 needs a hardware write/read-back to settle it.
* `sgx540_init_regs` is the SGX530's minimal set, not a capture from a working
  boot.
* The `EVENT_PDS_*` offsets are the SGX545's (`event_pds_guessed`); they are not
  in `sgx540defs.h`.
* The general/texture split inside the general region is ours (the DDK's 28-bit
  block has no texture heap).

See `docs/reference/sgx540-factsheet.md` for the facts and the gap table.
