/* p0_sanity.c — Phase 0: Pre-MMU sanity (UART, CSR, trap, SRAM, HTIF) */

#include "test.h"

extern char _bss_start[], _bss_end[];
extern char _page_table_start[], _page_table_end[];
extern char _heap_start[], _heap_end[];

int test_main(void)
{
    uart_puts("\n========================================\n");
    uart_puts("Phase 0: Pre-MMU Sanity Tests\n");
    uart_puts("========================================\n");

    /* P0.1 — UART TX */
    TEST_LOG("P0.1 — UART TX: OK (you are reading this)");

    /* P0.2 — CSR read */
    TEST_LOG("P0.2 — CSR read/write...");
    uint64_t sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    uart_puts("    sstatus = ");
    uart_put_hex(sstatus);
    uart_putc('\n');
    TEST_LOG("P0.2 — CSR read: OK");

    /* P0.3 — S-mode trap handler (ecall) */
    TEST_LOG("P0.3 — S-mode trap handler (ecall)...");
    g_trap.fault_count = 0;
    g_trap.scause = 0;

    /* Non-SBI ecall: a7=0xFF so M-mode forwards to S-mode handler */
    asm volatile("li a7, 0xFF; ecall" ::: "a7", "memory");

    ASSERT_EQ(g_trap.scause, CAUSE_ECALL_SMODE,
              "ecall should produce scause=9", 31);
    ASSERT_EQ(g_trap.fault_count, 1,
              "fault_count should be 1 after ecall", 32);
    TEST_LOG("P0.3 — S-mode trap handler: OK");

    /* P0.4 — SRAM read/write sweep */
    TEST_LOG("P0.4 — SRAM read/write sweep...");
    volatile uint64_t *heap = (volatile uint64_t *)_heap_start;
    uint64_t heap_size_words = ((uint64_t)_heap_end - (uint64_t)_heap_start) / 8;

    if (heap_size_words > 1024)
        heap_size_words = 1024;

    for (uint64_t i = 0; i < heap_size_words; i++)
        heap[i] = 0xDEAD0000UL | i;

    int mem_errors = 0;
    for (uint64_t i = 0; i < heap_size_words; i++) {
        uint64_t expected = 0xDEAD0000UL | i;
        if (heap[i] != expected) {
            if (mem_errors < 3) {
                uart_puts("    MEM ERROR at offset ");
                uart_put_dec((int)i);
                uart_puts(": got ");
                uart_put_hex(heap[i]);
                uart_puts(" expected ");
                uart_put_hex(expected);
                uart_putc('\n');
            }
            mem_errors++;
        }
    }
    ASSERT_EQ(mem_errors, 0, "SRAM read/write errors detected", 41);
    uart_puts("    Tested ");
    uart_put_dec((int)heap_size_words);
    uart_puts(" 64-bit words: OK\n");
    TEST_LOG("P0.4 — SRAM read/write sweep: OK");

    /* P0.5 — HTIF tested by final PASS signal */
    TEST_LOG("P0.5 — HTIF will be tested by final PASS signal");

    uart_puts("\n========================================\n");
    uart_puts("Phase 0: ALL TESTS PASSED\n");
    uart_puts("========================================\n");

    return 0;
}
