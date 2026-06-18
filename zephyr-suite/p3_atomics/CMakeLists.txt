# Idiomatic Zephyr suite — Phase 3 (atomics), full kernel in S-mode.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_suite_p3_atomics)

target_sources(app PRIVATE src/p3_atomics.c)
