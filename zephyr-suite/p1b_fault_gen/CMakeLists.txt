# Idiomatic Zephyr suite — p1b_fault_gen (full kernel, S-mode). See ../NOTES.md.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_suite_p1b_fault_gen)

target_sources(app PRIVATE src/p1b_fault_gen.c)

# Tests drive the arch MMU API + gated raw helpers (arch_mem_map,
# z_riscv_mmu_map_raw, z_riscv_mmu_pte_lookup); prototypes live in the
# kernel-internal include dirs (precedent: tests/arch/arm64/arm64_mmu).
target_include_directories(app PRIVATE
  ${ZEPHYR_BASE}/kernel/include
  ${ZEPHYR_BASE}/arch/riscv/include
)
