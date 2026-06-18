# Bare-metal RV64 test suite — Phase 2 (S-mode timer/software interrupts, sie
# masking, wfi), on Zephyr/QEMU (kernel-bypass).
# See ../common/baremetal.cmake and CLAUDE.md §5.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(baremetal_p2_interrupts)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/baremetal.cmake)
baremetal_app(p2_interrupts)
