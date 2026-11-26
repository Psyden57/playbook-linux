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
