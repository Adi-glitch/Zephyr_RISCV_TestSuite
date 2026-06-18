# RV64GC Bare-Metal Test Suite

Bare-metal verification tests for an RV64GC dual-core SoC with Sv39 MMU.
Runs on the Spike ISA simulator for logic validation, and on Verilator for RTL verification.

## Test Phases

| Phase | File | What it tests |
|-------|------|---------------|
| P0 | `p0_sanity.c` | UART, CSR, traps, SRAM, HTIF (pre-MMU) |
| P1A | `p1a_mmu_enable.c` | Sv39 enable, identity map, 4KB page, instruction fetch |
| P1B | `p1b_fault_gen.c` | Page fault generation (V=0, R=0, W=0, X=0, U-page, misaligned superpage) |
| P1C | `p1c_fault_handle.c` | Fault handling: PTE fix + retry, A/D bit detection |
| P1D | `p1d_tlb_ptw.c` | TLB flush, stale TLB, TLB thrash, megapage, gigapage, PTW through cache |
| P2 | `p2_interrupts.c` | S-mode timer/software interrupts via SBI, enable/disable, wfi |
| P3 | `p3_atomics.c` | LR/SC, AMO (swap/add/and/or/xor/min/max), CAS, fence, 32-bit AMOs |
| P4 | `p4_dualcore.c` | Core 1 wake, coherence, ping-pong, atomic increment, spinlock, TLB shootdown, IPI latency |
