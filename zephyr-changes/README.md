# Zephyr kernel changes (RISC-V) — apply on top of upstream

These are the **gated** edits to the Zephyr RISC-V arch that this project adds, captured as a
single patch against pristine upstream Zephyr. Everything is behind config flags (default
**off**), so a stock Zephyr build is byte-for-byte unchanged; only the apps in this repo turn
them on.

## Base commit

```
zephyrproject-rtos/zephyr @ 046a4308909   ("arch: fix various typos")
```

## Apply

```sh
cd <your-west-workspace>/zephyr
git checkout 046a4308909          # the exact base this patch was cut against
git apply /path/to/zephyr-changes/zephyr-riscv-port.patch
# (sanity: `git apply --check ...` first)
```

## What changed and why

| File | Gate | Purpose |
|------|------|---------|
| `arch/riscv/core/reset.S` | `CONFIG_BAREMETAL_SUITE` | divert M→S boot into the bare-metal suite's entry/trap vector (kernel-bypass port); secondary-hart park for P4 |
| `arch/riscv/Kconfig` | — | defines `CONFIG_BAREMETAL_SUITE`, `CONFIG_RISCV_MMU` (selects `MMU`), `CONFIG_RISCV_MMU_NUM_TABLES` |
| `arch/riscv/core/mmu.c` *(new)* | `CONFIG_RISCV_MMU` | Sv39 MMU: static page-table pool, `arch_mem_map`/`arch_mem_unmap`, boot `z_riscv_mmu_init` (satp), raw helpers `z_riscv_mmu_map_raw`/`z_riscv_mmu_pte_lookup`, and the resumable page-fault hook `z_riscv_mmu_set_fault_fixup` |
| `arch/riscv/core/prep_c.c` | `CONFIG_RISCV_MMU` | call `z_riscv_mmu_init()` before `z_cstart()` |
| `arch/riscv/core/fatal.c` | `CONFIG_RISCV_MMU` | in `z_riscv_fault`, dispatch page faults (12/13/15) to the registered fix-up resolver and return-to-retry (same slot as arm64/x86); default-inert |
| `arch/riscv/core/CMakeLists.txt` | `CONFIG_RISCV_MMU` | compile `mmu.c` |
| `arch/riscv/include/kernel_arch_func.h` | `CONFIG_RISCV_MMU` | prototypes for the above |
| `include/zephyr/arch/riscv/common/linker.ld` | non-XIP | emit `z_mapped_start` (kernel-image bound the generic MMU layer needs) |
| `boards/qemu/riscv64/board.cmake` | `CONFIG_BAREMETAL_SUITE` / `CONFIG_RISCV_MMU` | add `,mmu=on` to the QEMU virt CPU so `satp` is writable |

Full rationale, per-decision: see `../baremetal/PORTING_NOTES.md` (the exact-transfer port)
and `../zephyr-suite/NOTES.md` (§2 kernel-change table, design choices, pitfalls).
