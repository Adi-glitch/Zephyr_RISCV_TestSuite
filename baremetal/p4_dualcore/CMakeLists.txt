# Bare-metal RV64 test suite — Phase 4 (dual-core: wake, fence, atomic stress,
# spinlock, ping-pong), on Zephyr/QEMU (kernel-bypass).
# See ../common/baremetal.cmake and CLAUDE.md §5.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(baremetal_p4_dualcore)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/baremetal.cmake)
baremetal_app(p4_dualcore)
