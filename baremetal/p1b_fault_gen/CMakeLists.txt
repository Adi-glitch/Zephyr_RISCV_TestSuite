# Bare-metal RV64 test suite — Phase 1B (page-fault generation: causes 12/13/15),
# on Zephyr/QEMU (kernel-bypass). See ../common/baremetal.cmake and CLAUDE.md §5.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(baremetal_p1b_fault_gen)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/baremetal.cmake)
baremetal_app(p1b_fault_gen)
