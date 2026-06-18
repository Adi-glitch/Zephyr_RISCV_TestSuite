# Bare-metal RV64 test suite — Phase 1A (Sv39 enable, identity map, 4KB page,
# instruction fetch), on Zephyr/QEMU (kernel-bypass).
# See ../common/baremetal.cmake and CLAUDE.md §5.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(baremetal_p1a_mmu_enable)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/baremetal.cmake)
baremetal_app(p1a_mmu_enable)
