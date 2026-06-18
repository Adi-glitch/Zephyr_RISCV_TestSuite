/* p1b_fault_gen.c — Phase 1B: Page fault generation (causes 12, 13, 15) */

#include "test.h"

static void verify_fault(const char *name, int expected_cause,
                         uint64_t expected_stval, int test_code)
{
    ASSERT_EQ(g_trap.scause, (uint64_t)expected_cause, name, test_code);
    ASSERT_EQ(g_trap.stval, expected_stval, name, test_code + 1);

    uart_puts("    ");
    uart_puts(name);
    uart_puts(": scause=");
    uart_put_dec((int)g_trap.scause);
    uart_puts(" stval=");
    uart_put_hex(g_trap.stval);
    uart_puts(" OK\n");
}

int test_main(void)
{
    uart_puts("\n========================================\n");
    uart_puts("Phase 1B: Page Fault Generation\n");
    uart_puts("========================================\n");

    pt_reset();
    uint64_t *root = pt_setup_identity_kernel();
    mmu_enable_sv39(root);
    TEST_LOG("MMU enabled with identity kernel map");

    g_trap.handler_action = 1;  /* skip faulting instruction */

    /* P1B.1 — Load from V=0 page (cause 13) */
    TEST_LOG("P1B.1 — Load from V=0 page...");
    {
        uint64_t va = 0xD0000000UL;
        uint64_t pa = SRAM_BASE + 0x1A0000;
        pt_map_page(root, va, pa, PTE_R | PTE_W, 0);
        sfence_vma_all();
        g_trap.fault_count = 0;
        g_trap.scause = 0;

        volatile uint64_t val;
        asm volatile("ld %0, 0(%1)" : "=r"(val) : "r"(va) : "memory");

        ASSERT_EQ(g_trap.fault_count, 1, "P1B.1 fault_count", 110);
        verify_fault("P1B.1 Load V=0", CAUSE_LOAD_PAGE_FAULT, va, 111);
    }

    /* P1B.2 — Store to V=0 page (cause 15) */
    TEST_LOG("P1B.2 — Store to V=0 page...");
    {
        uint64_t va = 0xD0010000UL;
        uint64_t pa = SRAM_BASE + 0x1A1000;
        pt_map_page(root, va, pa, PTE_R | PTE_W, 0);
        sfence_vma_all();
        g_trap.fault_count = 0;
        g_trap.scause = 0;

        asm volatile("sd %0, 0(%1)" : : "r"(0xBEEFUL), "r"(va) : "memory");

        ASSERT_EQ(g_trap.fault_count, 1, "P1B.2 fault_count", 120);
        verify_fault("P1B.2 Store V=0", CAUSE_STORE_PAGE_FAULT, va, 121);
    }

    /* P1B.3 — Execute from V=0 page (cause 12, uses fix-PTE handler) */
    TEST_LOG("P1B.3 — Execute from V=0 page...");
    {
        g_trap.handler_action = 0;

        uint64_t va = 0xD0030000UL;
        uint64_t pa = SRAM_BASE + 0x1A3000;

        volatile uint32_t *target_insn = (volatile uint32_t *)pa;
        *target_insn = 0x00008067;  /* ret */
        fence_i();

        pt_map_page(root, va, pa, PTE_R | PTE_X, 0);
        sfence_vma_all();
        g_trap.fault_count = 0;
        g_trap.scause = 0;

        void (*func_ptr)(void) = (void (*)(void))va;
        func_ptr();

        ASSERT_EQ(g_trap.fault_count, 1, "P1B.3 fault_count", 130);
        ASSERT_EQ(g_trap.scause, CAUSE_INST_PAGE_FAULT, "P1B.3 scause", 131);
        TEST_LOG("P1B.3 — Execute from V=0 page: OK");

        g_trap.handler_action = 1;
    }

    /* P1B.4 — Load from R=0 page (X=1 only → execute-only) */
    TEST_LOG("P1B.4 — Load from R=0 page...");
    {
        uint64_t va = 0xD0040000UL;
        uint64_t pa = SRAM_BASE + 0x1A4000;
        pt_map_page(root, va, pa, PTE_X, 1);
        sfence_vma_all();
        g_trap.fault_count = 0;
        g_trap.scause = 0;

        volatile uint64_t val;
        asm volatile("ld %0, 0(%1)" : "=r"(val) : "r"(va) : "memory");

        ASSERT_EQ(g_trap.fault_count, 1, "P1B.4 fault_count", 140);
        verify_fault("P1B.4 Load R=0", CAUSE_LOAD_PAGE_FAULT, va, 141);
    }

    /* P1B.5 — Store to W=0 page (R=1 only → read-only) */
    TEST_LOG("P1B.5 — Store to W=0 page...");
    {
        uint64_t va = 0xD0050000UL;
        uint64_t pa = SRAM_BASE + 0x1A5000;
        pt_map_page(root, va, pa, PTE_R, 1);
        sfence_vma_all();
        g_trap.fault_count = 0;
        g_trap.scause = 0;

        asm volatile("sd %0, 0(%1)" : : "r"(0xBEEFUL), "r"(va) : "memory");

        ASSERT_EQ(g_trap.fault_count, 1, "P1B.5 fault_count", 150);
        verify_fault("P1B.5 Store W=0", CAUSE_STORE_PAGE_FAULT, va, 151);
    }

    /* P1B.6 — Execute from X=0 page (R=1 only, uses fix-PTE handler) */
    TEST_LOG("P1B.6 — Execute from X=0 page...");
    {
        uint64_t va = 0xD0060000UL;
        uint64_t pa = SRAM_BASE + 0x1A6000;
        g_trap.handler_action = 0;

        volatile uint32_t *insn = (volatile uint32_t *)pa;
        *insn = 0x00008067;
        fence_i();

        pt_map_page(root, va, pa, PTE_R, 1);
        sfence_vma_all();
        g_trap.fault_count = 0;
        g_trap.scause = 0;

        void (*func_ptr)(void) = (void (*)(void))va;
        func_ptr();

        ASSERT_EQ(g_trap.fault_count, 1, "P1B.6 fault_count", 160);
        ASSERT_EQ(g_trap.scause, CAUSE_INST_PAGE_FAULT, "P1B.6 scause", 161);
        TEST_LOG("P1B.6 — Execute from X=0 page: OK");

        g_trap.handler_action = 1;
    }

    /* P1B.7 — S-mode access to U=1 page with SUM=0 */
    TEST_LOG("P1B.7 — S-mode access to U=1 page (SUM=0)...");
    {
        uint64_t va = 0xD0070000UL;
        uint64_t pa = SRAM_BASE + 0x1A7000;
        pt_map_page(root, va, pa, PTE_R | PTE_W | PTE_U, 1);
        sfence_vma_all();

        uint64_t sstatus;
        asm volatile("csrr %0, sstatus" : "=r"(sstatus));
        sstatus &= ~MSTATUS_SUM;
        asm volatile("csrw sstatus, %0" :: "r"(sstatus));

        g_trap.fault_count = 0;
        g_trap.scause = 0;

        volatile uint64_t val;
        asm volatile("ld %0, 0(%1)" : "=r"(val) : "r"(va) : "memory");

        ASSERT_EQ(g_trap.fault_count, 1, "P1B.7 fault_count", 170);
        verify_fault("P1B.7 U-page from S-mode", CAUSE_LOAD_PAGE_FAULT, va, 171);
    }

    /* P1B.8 — Misaligned superpage (megapage with PPN[0]!=0) */
    TEST_LOG("P1B.8 — Misaligned superpage...");
    {
        uint64_t va = 0xE0000000UL;
        int idx2 = (va >> 30) & 0x1FF;
        int idx1 = (va >> 21) & 0x1FF;

        if (!(root[idx2] & PTE_V)) {
            uint64_t *l1 = pt_alloc_page();
            root[idx2] = ((((uint64_t)l1) >> PGSHIFT) << PTE_PPN_SHIFT) | PTE_V;
        }
        uint64_t *l1 = (uint64_t *)((root[idx2] >> PTE_PPN_SHIFT) << PGSHIFT);

        /* Megapage leaf with misaligned PPN (PPN[0] bit 0 set) */
        uint64_t bad_ppn = (SRAM_BASE >> PGSHIFT) | 0x1;
        l1[idx1] = (bad_ppn << PTE_PPN_SHIFT) | PTE_V | PTE_R | PTE_W | PTE_A | PTE_D;

        sfence_vma_all();
        g_trap.fault_count = 0;
        g_trap.scause = 0;

        volatile uint64_t val;
        asm volatile("ld %0, 0(%1)" : "=r"(val) : "r"(va) : "memory");

        ASSERT_EQ(g_trap.fault_count, 1, "P1B.8 fault_count", 180);
        verify_fault("P1B.8 Misaligned megapage", CAUSE_LOAD_PAGE_FAULT, va, 181);
    }

    uart_puts("\n========================================\n");
    uart_puts("Phase 1B: ALL TESTS PASSED\n");
    uart_puts("========================================\n");

    return 0;
}
