/* p1c_fault_handle.c — Phase 1C: Page fault handling, PTE fix, retry, A/D bits */

#include "test.h"

int test_main(void)
{
    uart_puts("\n========================================\n");
    uart_puts("Phase 1C: Fault Handling & Retry\n");
    uart_puts("========================================\n");

    pt_reset();
    uint64_t *root = pt_setup_identity_kernel();
    mmu_enable_sv39(root);
    TEST_LOG("MMU enabled");

    g_trap.handler_action = 0;  /* fix PTE mode */

    /* P1C.1 — Load fault: handler fixes V=0→V=1, load retries */
    TEST_LOG("P1C.1 — Load fault fix and retry...");
    {
        uint64_t va = 0xD0100000UL;
        uint64_t pa = SRAM_BASE + 0x1B0000;

        volatile uint64_t *pa_ptr = (volatile uint64_t *)pa;
        *pa_ptr = 0xFEEDFACECAFE0001UL;

        pt_map_page(root, va, pa, PTE_R | PTE_W, 0);
        sfence_vma_all();
        g_trap.fault_count = 0;

        volatile uint64_t *va_ptr = (volatile uint64_t *)va;
        uint64_t loaded_val = *va_ptr;

        ASSERT_EQ(g_trap.fault_count, 1, "P1C.1 fault_count", 210);
        ASSERT_EQ(g_trap.scause, CAUSE_LOAD_PAGE_FAULT, "P1C.1 scause", 211);
        ASSERT_EQ(loaded_val, 0xFEEDFACECAFE0001UL, "P1C.1 loaded value mismatch", 212);

        TEST_LOG("P1C.1 — Load fault fix + retry: OK");
    }

    /* P1C.2 — Store fault: handler adds W=1, store retries */
    TEST_LOG("P1C.2 — Store fault fix and retry...");
    {
        uint64_t va = 0xD0110000UL;
        uint64_t pa = SRAM_BASE + 0x1B1000;

        pt_map_page(root, va, pa, PTE_R, 1);  /* read-only */
        sfence_vma_all();
        g_trap.fault_count = 0;

        volatile uint64_t *va_ptr = (volatile uint64_t *)va;
        *va_ptr = 0xDEADBEEF00000002UL;

        ASSERT_EQ(g_trap.fault_count, 1, "P1C.2 fault_count", 220);
        ASSERT_EQ(g_trap.scause, CAUSE_STORE_PAGE_FAULT, "P1C.2 scause", 221);

        volatile uint64_t *pa_ptr = (volatile uint64_t *)pa;
        ASSERT_EQ(*pa_ptr, 0xDEADBEEF00000002UL, "P1C.2 store value mismatch", 222);

        TEST_LOG("P1C.2 — Store fault fix + retry: OK");
    }

    /* P1C.3 — Execute fault: handler adds X=1, fetch retries */
    TEST_LOG("P1C.3 — Execute fault fix and retry...");
    {
        uint64_t va = 0xD0120000UL;
        uint64_t pa = SRAM_BASE + 0x1B2000;

        volatile uint32_t *insn = (volatile uint32_t *)pa;
        *insn = 0x00008067;  /* ret */
        fence_i();

        pt_map_page(root, va, pa, PTE_R, 1);  /* no X */
        sfence_vma_all();
        g_trap.fault_count = 0;

        void (*func)(void) = (void (*)(void))va;
        func();

        ASSERT_EQ(g_trap.fault_count, 1, "P1C.3 fault_count", 230);
        ASSERT_EQ(g_trap.scause, CAUSE_INST_PAGE_FAULT, "P1C.3 scause", 231);

        TEST_LOG("P1C.3 — Execute fault fix + retry: OK");
    }

    /* P1C.4 — 8 sequential faults on different addresses */
    TEST_LOG("P1C.4 — Multiple sequential faults...");
    {
        g_trap.fault_count = 0;
        #define NUM_FAULTS 8
        uint64_t base_va = 0xD0200000UL;
        uint64_t base_pa = SRAM_BASE + 0x1C0000;

        for (int i = 0; i < NUM_FAULTS; i++) {
            uint64_t va = base_va + i * PGSIZE;
            uint64_t pa = base_pa + i * PGSIZE;
            volatile uint64_t *pa_ptr = (volatile uint64_t *)pa;
            *pa_ptr = 0xA000000000000000UL | i;
            pt_map_page(root, va, pa, PTE_R | PTE_W, 0);
        }
        sfence_vma_all();

        for (int i = 0; i < NUM_FAULTS; i++) {
            uint64_t va = base_va + i * PGSIZE;
            volatile uint64_t *va_ptr = (volatile uint64_t *)va;
            uint64_t val = *va_ptr;
            uint64_t expected = 0xA000000000000000UL | i;
            ASSERT_EQ(val, expected, "P1C.4 value mismatch", 240 + i);
        }

        ASSERT_EQ(g_trap.fault_count, NUM_FAULTS, "P1C.4 total fault count", 250);

        uart_puts("    Triggered and handled ");
        uart_put_dec(NUM_FAULTS);
        uart_puts(" sequential page faults: OK\n");
        TEST_LOG("P1C.4 — Multiple sequential faults: OK");
    }

    /* P1C.5 — A/D bit behavior detection (svadu vs svade) */
    TEST_LOG("P1C.5 — A/D bit behavior detection...");
    {
        uint64_t va = 0xD0300000UL;
        uint64_t pa = SRAM_BASE + 0x1D0000;

        int idx2 = (va >> 30) & 0x1FF;
        int idx1 = (va >> 21) & 0x1FF;
        int idx0 = (va >> 12) & 0x1FF;

        if (!(root[idx2] & PTE_V)) {
            uint64_t *l1 = pt_alloc_page();
            root[idx2] = (((uint64_t)l1 >> PGSHIFT) << PTE_PPN_SHIFT) | PTE_V;
        }
        uint64_t *l1 = (uint64_t *)((root[idx2] >> PTE_PPN_SHIFT) << PGSHIFT);
        if (!(l1[idx1] & PTE_V)) {
            uint64_t *l2 = pt_alloc_page();
            l1[idx1] = (((uint64_t)l2 >> PGSHIFT) << PTE_PPN_SHIFT) | PTE_V;
        }
        uint64_t *l2 = (uint64_t *)((l1[idx1] >> PTE_PPN_SHIFT) << PGSHIFT);

        /* Leaf PTE: V=1, R=1, W=1, but A=0, D=0 */
        uint64_t ppn = pa >> PGSHIFT;
        l2[idx0] = (ppn << PTE_PPN_SHIFT) | PTE_V | PTE_R | PTE_W;

        sfence_vma_all();
        g_trap.fault_count = 0;

        volatile uint64_t *pa_ptr = (volatile uint64_t *)pa;
        *pa_ptr = 0x1234567890ABCDEFUL;

        volatile uint64_t *va_ptr = (volatile uint64_t *)va;
        uint64_t val = *va_ptr;

        if (g_trap.fault_count == 0) {
            uart_puts("    A/D mode: SVADU (hardware sets A/D bits)\n");
            ASSERT_EQ(val, 0x1234567890ABCDEFUL, "P1C.5 svadu load val", 260);
            uint64_t pte_now = l2[idx0];
            if (pte_now & PTE_A)
                uart_puts("    Confirmed: A bit set by hardware\n");
        } else {
            uart_puts("    A/D mode: SVADE (software must manage A/D)\n");
            ASSERT_EQ(g_trap.scause, CAUSE_LOAD_PAGE_FAULT, "P1C.5 svade fault type", 261);
            ASSERT_EQ(val, 0x1234567890ABCDEFUL, "P1C.5 svade load val", 262);
        }

        TEST_LOG("P1C.5 — A/D bit behavior: OK (mode detected)");
    }

    uart_puts("\n========================================\n");
    uart_puts("Phase 1C: ALL TESTS PASSED\n");
    uart_puts("========================================\n");

    return 0;
}
