# Shared glue for running rv64gcv bare-metal suite phases on Zephyr/QEMU.
#
# Each phase app does:
#     find_package(Zephyr ...)
#     project(...)
#     include(${CMAKE_CURRENT_SOURCE_DIR}/../common/baremetal.cmake)
#     baremetal_app(<phase>)        # e.g. baremetal_app(p1a_mmu_enable)
#
# which compiles the suite's tests/<phase>.c VERBATIM alongside the shared
# kernel-bypass harness. Pair with prj.conf: CONFIG_BAREMETAL_SUITE=y.

set(BAREMETAL_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR})
# .../my-app/baremetal/common -> .../rv64gcv-baremetal-testsuite-main
set(BAREMETAL_SUITE ${CMAKE_CURRENT_LIST_DIR}/../../../rv64gcv-baremetal-testsuite-main)

function(baremetal_app test_name)
  # Suite headers: platform.h, test.h, asm_defs.h
  target_include_directories(app PRIVATE ${BAREMETAL_SUITE}/common)

  # QEMU virt NS16550 uses a 1-byte register stride (reg-shift 0). Zephyr builds
  # strict ISO C (-std=c11), which disables the GNU `asm` keyword the suite uses.
  target_compile_definitions(app PRIVATE UART_REG_SHIFT=0 asm=__asm__)

  target_sources(app PRIVATE
    # Shared kernel-bypass harness.
    ${BAREMETAL_COMMON_DIR}/baremetal_main.c
    ${BAREMETAL_COMMON_DIR}/baremetal_trap.S
    ${BAREMETAL_COMMON_DIR}/baremetal_syms.S
    ${BAREMETAL_COMMON_DIR}/smode.c
    ${BAREMETAL_COMMON_DIR}/m_trap.c
    # The phase under test, compiled verbatim from the original suite.
    ${BAREMETAL_SUITE}/tests/${test_name}.c
  )
endfunction()
