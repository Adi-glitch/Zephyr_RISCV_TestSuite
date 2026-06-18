# Idiomatic Zephyr suite — p1c_fault_handle (full kernel, S-mode). See ../NOTES.md.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_suite_p1c_fault_handle)

target_sources(app PRIVATE src/p1c_fault_handle.c)

# Tests drive the arch MMU API + gated raw helpers (arch_mem_map,
# z_riscv_mmu_map_raw, z_riscv_mmu_pte_lookup); prototypes live in the
# kernel-internal include dirs (precedent: tests/arch/arm64/arm64_mmu).
target_include_directories(app PRIVATE
  ${ZEPHYR_BASE}/kernel/include
  ${ZEPHYR_BASE}/arch/riscv/include
)
