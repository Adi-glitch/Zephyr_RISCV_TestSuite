# Zephyr RISC-V Test Suite — bare-metal port + idiomatic ztest suite

Porting an **RV64GC(V) bare-metal test suite** (memory/MMU, interrupts, atomics, dual-core)
onto **Zephyr / QEMU `virt`**, two ways:

1. **`baremetal/`** — *exact transfer*: Zephyr boots, then (behind a gate) **bypasses its own
   kernel** and runs the suite's `tests/p*.c` **verbatim**, reusing the suite's M/S-mode trap
   framework. Verdict via the QEMU `sifive_test` finisher.
2. **`zephyr-suite/`** — *idiomatic*: the **full Zephyr kernel runs** (S-mode) and the same
   chip features are tested with **Zephyr APIs + ztest**, reported by **twister**. This effort
   also added real **Sv39 MMU support to the Zephyr RISC-V kernel** (`CONFIG_RISCV_MMU`).

`rv64gcv-baremetal-testsuite-main/` is the original suite being ported (reference).

## History: the "before → now" diff

This repo is two commits so the change set is explicit:

| Commit | |
|---|---|
| **1 — baseline** | upstream Zephyr `example-application` (v4.4.0) + the original RV64GCV bare-metal suite; pristine Zephyr @ `046a4308909` |
| **2 — our work** | `baremetal/` + `zephyr-suite/` apps + `zephyr-changes/` (the gated kernel patch) |

```sh
git diff --stat HEAD~1 HEAD     # exactly what the port adds
```

## Layout

```
baremetal/                     exact-transfer apps (p0_sanity … p4_dualcore) + common/ + PORTING_NOTES.md
zephyr-suite/                  idiomatic ztest apps (p0_sanity … p4_dualcore) + NOTES.md
zephyr-changes/                gated Zephyr RISC-V kernel patch + how-to-apply README
rv64gcv-baremetal-testsuite-main/   original bare-metal suite (reference being ported)
<example-application scaffolding: app/ boards/ drivers/ dts/ lib/ tests/ west.yml …>
```

Read first: `zephyr-suite/NOTES.md` (design, suite-coverage map, kernel-change table) and
`baremetal/PORTING_NOTES.md` (every gated edit + why).

## Reproduce the workspace (teammates)

The 9 GB Zephyr + modules tree is **not** in this repo — rebuild it with west, then apply the
kernel patch and point west at these apps:

```sh
# 1. a west workspace with the exact Zephyr base this was built on
west init -m https://github.com/zephyrproject-rtos/zephyr --mr 046a4308909 ws
cd ws && west update && west zephyr-export
pip install -r zephyr/scripts/requirements.txt

# 2. apply the gated kernel changes (see zephyr-changes/README.md)
cd zephyr && git apply /path/to/zephyr-changes/zephyr-riscv-port.patch && cd ..

# 3. put this repo's apps where west can build them (e.g. clone alongside, or copy
#    baremetal/ and zephyr-suite/ into the workspace)
git clone https://github.com/Adi-glitch/Zephyr_RISCV_TestSuite.git
export ZEPHYR_BASE=$PWD/zephyr
```

(Or just build from a checkout of this repo with `ZEPHYR_BASE` pointing at your patched Zephyr.)

## Build & run

S-mode board for P0–P3; the SMP board for P4. Bound the QEMU run so output flushes.

```sh
# idiomatic ztest phase (example: P1 MMU)
west build -p always -b qemu_riscv64/qemu_virt_riscv64/smode -d build/p1a zephyr-suite/p1a_mmu_enable
perl -e 'alarm 30; exec @ARGV' west build -t run -d build/p1a

# dual-core uses the SMP board
west build -p always -b qemu_riscv64/qemu_virt_riscv64/smp   -d build/p4 zephyr-suite/p4_dualcore

# bare-metal exact-transfer phase
west build -p always -b qemu_riscv64/qemu_virt_riscv64/smode -d build/bm_p1a baremetal/p1a_mmu_enable
perl -e 'alarm 30; exec @ARGV' west build -t run -d build/bm_p1a    # => "Phase 1A: ALL TESTS PASSED" / [PASS]

# whole idiomatic suite via twister
west twister -T zephyr-suite \
  -p qemu_riscv64/qemu_virt_riscv64/smode -p qemu_riscv64/qemu_virt_riscv64/smp
```

## Status

- **Idiomatic suite (`zephyr-suite/`): twister green — 8/8 configurations, 49/49 test cases**
  (P0 5, P1A 3, P1B 12, P1C 5, P1D 5, P2 5, P3 8, P4 6).
- **Bare-metal suite (`baremetal/`): P0, P1A–P1D, P2, P3, P4 all pass.**
- All Zephyr-tree edits are gated (`CONFIG_BAREMETAL_SUITE`, `CONFIG_RISCV_MMU`, default off) —
  stock Zephyr builds are unaffected. Coverage caveats (a few fix-retry / timer-ownership
  cases) are documented in `zephyr-suite/NOTES.md`.
