# RV64 Bare-Metal Test Suite on ZephyrOS / QEMU

The RV64 bare-metal test suite (P0–P4) run **verbatim** on ZephyrOS under QEMU. Zephyr does
the early boot and the M→S transition, then (behind a build flag) hands control to the
suite instead of starting the kernel. **All phases pass.**

- **Why / how / every decision:** see [`PORTING_NOTES.md`](PORTING_NOTES.md).
- **Original suite behavior & per-phase inventory:** see
  `../../rv64gcv-baremetal-testsuite-main/BAREMETAL_REFERENCE.md`.

## What's here

```
common/          shared harness (kernel-bypass glue, trap vectors, smode.c/m_trap.c copies,
                 baremetal.cmake helper). Only change vs the original: verdict goes to the
                 QEMU finisher instead of HTIF.
p0_sanity/       \
p1a_mmu_enable/   |  one thin dir per phase: CMakeLists.txt (baremetal_app(<phase>)) + prj.conf.
p1b_fault_gen/    |  The phase's test is compiled VERBATIM from the original suite.
...               |
p4_dualcore/     /
zephyr-patches/  baremetal-suite.patch  (the gated edits to the Zephyr tree — see below)
```

## Prerequisites

1. A Zephyr **west workspace** with the Zephyr SDK (this app was developed against Zephyr
   `main` @ `046a4308909`).
2. The original suite checked out **next to `my-app`** (the app compiles tests from it via a
   relative path `../../../rv64gcv-baremetal-testsuite-main`):
   ```
   <workspace>/
     my-app/                          ← this repo
     rv64gcv-baremetal-testsuite-main/ ← the original suite (sibling)
     zephyr/  modules/  ...
   ```

## Apply the Zephyr patch (required)

The suite needs 3 small, **gated** edits to the Zephyr tree (a new `CONFIG_BAREMETAL_SUITE`
option, a boot divert in `reset.S`, and `mmu=on` in the QEMU board). They live in the
west-managed `zephyr/` clone, so `west update` would wipe them — apply the patch after
updating:

```sh
cd <workspace>/zephyr
git apply ../my-app/baremetal/zephyr-patches/baremetal-suite.patch
```
With `CONFIG_BAREMETAL_SUITE=n` (the default) these edits are inert — normal Zephyr builds
are unaffected.

## Build & run a phase

```sh
# from the workspace root, with west + SDK on PATH
west build -p always -b qemu_riscv64/qemu_virt_riscv64/smode -d build/p0 my-app/baremetal/p0_sanity
west build -t run -d build/p0        # bound it if you like: perl -e 'alarm 25; exec @ARGV' west build -t run -d build/p0
```
Phases: `p0_sanity`, `p1a_mmu_enable`, `p1b_fault_gen`, `p1c_fault_handle`, `p1d_tlb_ptw`,
`p2_interrupts`, `p3_atomics`, `p4_dualcore` (P4 adds `CONFIG_MP_MAX_NUM_CPUS=2` +
`CONFIG_QEMU_ICOUNT=n` in its `prj.conf` for the 2nd hart).

**Pass** = the suite prints `Phase N: ALL TESTS PASSED` + `[PASS]` and QEMU exits 0.
