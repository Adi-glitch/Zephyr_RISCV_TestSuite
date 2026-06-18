# Idiomatic Zephyr suite — Phase 4 (dual-core), full kernel, SMP.
#
# Runs on the qemu .../smp board variant (M-mode): stock Zephyr's secondary-
# hart boot does not do the M->S transition (reset.S boot_secondary_core), so
# S-mode + SMP is unsupported in-tree — same reason baremetal P4 needed its own
# gated secondary divert. The dual-core chip features under test are
# privilege-agnostic.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_suite_p4_dualcore)

target_sources(app PRIVATE src/p4_dualcore.c)
