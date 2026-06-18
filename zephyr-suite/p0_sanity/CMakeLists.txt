# Idiomatic Zephyr suite — Phase 0 (sanity), full kernel in S-mode.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_suite_p0_sanity)

target_sources(app PRIVATE src/p0_sanity.c)
