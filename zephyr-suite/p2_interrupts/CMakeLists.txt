# Idiomatic Zephyr suite — Phase 2 (interrupts), full kernel in S-mode.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_suite_p2_interrupts)

target_sources(app PRIVATE src/p2_interrupts.c)

# P2.4 calls the arch MMU API + gated fix-retry hook (z_riscv_mmu_map_raw,
# z_riscv_mmu_pte_lookup, z_riscv_mmu_set_fault_fixup) — kernel-internal headers.
target_include_directories(app PRIVATE
  ${ZEPHYR_BASE}/kernel/include
  ${ZEPHYR_BASE}/arch/riscv/include
)
