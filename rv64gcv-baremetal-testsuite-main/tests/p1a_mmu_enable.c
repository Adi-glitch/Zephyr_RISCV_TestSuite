/* p1a_mmu_enable.c — Phase 1A: Sv39 enable, identity map, 4KB page, ifetch */

#include "test.h"

int test_main(void)
{
    uart_puts("\n========================================\n");
    uart_puts("Phase 1A: MMU Enable & Identity Map\n");
    uart_puts("========================================\n");

    /* P1A.1 — Enable Sv39 identity map (SRAM gigapage + I/O gigapage) */
    TEST_LOG("P1A.1 — Enable Sv39 with identity-mapped kernel...");

    pt_reset();
    uint64_t *root = pt_setup_identity_kernel();

    uart_puts("    Root PT at PA ");
    uart_put_hex((uint64_t)root);
    uart_putc('\n');

    mmu_enable_sv39(root);

    TEST_LOG("P1A.1 — MMU enabled, identity map: OK");

    volatile uint64_t *test_addr = (volatile uint64_t *)(SRAM_BASE + 0x100000);
    *test_addr = 0xCAFEBABE12345678UL;
    uint64_t readback = *test_addr;
    ASSERT_EQ(readback, 0xCAFEBABE12345678UL,
              "Store/load through identity map failed", 11);
    TEST_LOG("P1A.1 — Store/load through identity map: OK");

    uart_puts("    UART via I/O gigapage: OK\n");

    /* P1A.2 — 4 KB page mapping load/store (3-level walk) */
    TEST_LOG("P1A.2 — 4 KB page mapping load/store...");

    uint64_t test_va = 0xC0000000UL;
    uint64_t test_pa = SRAM_BASE + 0x180000;

    pt_map_page(root, test_va, test_pa, PTE_R | PTE_W, 1);
    sfence_vma_all();

    volatile uint64_t *va_ptr = (volatile uint64_t *)test_va;
    *va_ptr = 0xA5A5A5A5A5A5A5A5UL;

    volatile uint64_t *pa_ptr = (volatile uint64_t *)test_pa;
    ASSERT_EQ(*pa_ptr, 0xA5A5A5A5A5A5A5A5UL,
              "4KB page mapping: PA readback mismatch", 12);
    ASSERT_EQ(*va_ptr, 0xA5A5A5A5A5A5A5A5UL,
              "4KB page mapping: VA readback mismatch", 13);

    TEST_LOG("P1A.2 — 4 KB page load/store: OK");

    /* P1A.3 — Instruction fetch via mapped page (ret at PA, call via VA) */
    TEST_LOG("P1A.3 — Instruction fetch via mapped page...");

    uint64_t exec_pa = SRAM_BASE + 0x180000;
    uint64_t exec_va = 0xC0010000UL;

    volatile uint32_t *code_page = (volatile uint32_t *)exec_pa;
    code_page[0] = 0x00008067;  /* ret */
    fence_i();  /* flush I-cache so fetch sees the store */

    pt_map_page(root, exec_va, exec_pa, PTE_R | PTE_X, 1);
    sfence_vma_all();

    void (*func_ptr)(void) = (void (*)(void))exec_va;
    func_ptr();

    TEST_LOG("P1A.3 — Instruction fetch via mapped page: OK");

    uart_puts("\n========================================\n");
    uart_puts("Phase 1A: ALL TESTS PASSED\n");
    uart_puts("========================================\n");

    return 0;
}
