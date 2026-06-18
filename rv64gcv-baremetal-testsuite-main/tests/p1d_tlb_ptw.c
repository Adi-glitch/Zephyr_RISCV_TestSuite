/* p1d_tlb_ptw.c — Phase 1D: TLB flush, stale TLB, thrash, megapage, gigapage, PTW cache */

#include "test.h"

int test_main(void)
{
    uart_puts("\n========================================\n");
    uart_puts("Phase 1D: TLB & Page Walker Tests\n");
    uart_puts("========================================\n");

    pt_reset();
    uint64_t *root = pt_setup_identity_kernel();
    mmu_enable_sv39(root);
    g_trap.handler_action = 0;
    TEST_LOG("MMU enabled");

    /* P1D.1 — Targeted sfence.vma: flush VA_A only, VA_B stays cached */
    TEST_LOG("P1D.1 — Targeted sfence.vma...");
    {
        uint64_t va_a = 0xD1000000UL, va_b = 0xD1010000UL;
        uint64_t pa_a = SRAM_BASE + 0x1E0000;
        uint64_t pa_b = SRAM_BASE + 0x1E1000;
        uint64_t pa_a_new = SRAM_BASE + 0x1E2000;

        *(volatile uint64_t *)pa_a = 0xAAAAAAAA00000001UL;
        *(volatile uint64_t *)pa_b = 0xBBBBBBBB00000002UL;
        *(volatile uint64_t *)pa_a_new = 0xAAAAAAAA00000003UL;

        pt_map_page(root, va_a, pa_a, PTE_R | PTE_W, 1);
        pt_map_page(root, va_b, pa_b, PTE_R | PTE_W, 1);
        sfence_vma_all();

        volatile uint64_t val_a = *(volatile uint64_t *)va_a;
        volatile uint64_t val_b = *(volatile uint64_t *)va_b;
        ASSERT_EQ(val_a, 0xAAAAAAAA00000001UL, "P1D.1 initial VA_A", 310);
        ASSERT_EQ(val_b, 0xBBBBBBBB00000002UL, "P1D.1 initial VA_B", 311);

        /* Remap VA_A → pa_a_new, flush only VA_A */
        int level;
        uint64_t *pte_a = pt_walk(root, va_a, &level);
        uint64_t ppn_new = pa_a_new >> PGSHIFT;
        *pte_a = (ppn_new << PTE_PPN_SHIFT) | PTE_V | PTE_R | PTE_W | PTE_A | PTE_D;
        asm volatile("fence w, w" ::: "memory");
        sfence_vma_addr(va_a);

        val_a = *(volatile uint64_t *)va_a;
        ASSERT_EQ(val_a, 0xAAAAAAAA00000003UL, "P1D.1 VA_A after targeted flush", 312);
        val_b = *(volatile uint64_t *)va_b;
        ASSERT_EQ(val_b, 0xBBBBBBBB00000002UL, "P1D.1 VA_B after targeted flush", 313);

        TEST_LOG("P1D.1 — Targeted sfence.vma: OK");
    }

    /* P1D.2 — Stale TLB: PTE change without sfence.vma (either old or new is legal) */
    TEST_LOG("P1D.2 — Stale TLB (no sfence.vma after PTE change)...");
    {
        uint64_t va = 0xD1100000UL;
        uint64_t pa_old = SRAM_BASE + 0x1E4000;
        uint64_t pa_new = SRAM_BASE + 0x1E5000;

        *(volatile uint64_t *)pa_old = 0x1111111111111111UL;
        *(volatile uint64_t *)pa_new = 0x2222222222222222UL;

        pt_map_page(root, va, pa_old, PTE_R | PTE_W, 1);
        sfence_vma_all();

        volatile uint64_t val = *(volatile uint64_t *)va;
        ASSERT_EQ(val, 0x1111111111111111UL, "P1D.2 initial", 320);

        int level;
        uint64_t *pte = pt_walk(root, va, &level);
        uint64_t ppn_new = pa_new >> PGSHIFT;
        *pte = (ppn_new << PTE_PPN_SHIFT) | PTE_V | PTE_R | PTE_W | PTE_A | PTE_D;
        asm volatile("fence w, w" ::: "memory");
        /* Deliberately NO sfence.vma */

        val = *(volatile uint64_t *)va;
        int got_old = (val == 0x1111111111111111UL);
        int got_new = (val == 0x2222222222222222UL);

        uart_puts("    Without sfence.vma, read returned: ");
        uart_put_hex(val);
        if (got_old) uart_puts(" (old — stale TLB hit)");
        else if (got_new) uart_puts(" (new — TLB re-walked)");
        else uart_puts(" (UNEXPECTED — BUG)");
        uart_putc('\n');

        test_assert(got_old || got_new, "P1D.2: value must be old or new", 321);

        sfence_vma_addr(va);
        val = *(volatile uint64_t *)va;
        ASSERT_EQ(val, 0x2222222222222222UL, "P1D.2 after sfence should get new", 322);

        TEST_LOG("P1D.2 — Stale TLB behavior: OK (spec-compliant)");
    }

    /* P1D.3 — TLB thrash: 128 pages (TLB=64), 2 passes */
    TEST_LOG("P1D.3 — TLB thrash (128 pages, TLB=64)...");
    {
        #define THRASH_COUNT 128
        uint64_t base_va = 0xD2000000UL;
        uint64_t base_pa = SRAM_BASE + 0x100000;

        for (int i = 0; i < THRASH_COUNT; i++) {
            uint64_t va = base_va + (uint64_t)i * PGSIZE;
            uint64_t pa = base_pa + (uint64_t)i * PGSIZE;
            *(volatile uint64_t *)pa = 0xCC00000000000000UL | i;
            pt_map_page(root, va, pa, PTE_R | PTE_W, 1);
        }
        sfence_vma_all();

        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < THRASH_COUNT; i++) {
                uint64_t va = base_va + (uint64_t)i * PGSIZE;
                volatile uint64_t val = *(volatile uint64_t *)va;
                uint64_t expected = 0xCC00000000000000UL | i;
                if (val != expected) {
                    uart_puts("    TLB thrash mismatch at page ");
                    uart_put_dec(i);
                    uart_puts(" pass ");
                    uart_put_dec(pass);
                    uart_putc('\n');
                    test_fail(330 + pass * 130 + i);
                }
            }
        }

        uart_puts("    128 pages, 2 passes: all correct\n");
        TEST_LOG("P1D.3 — TLB thrash: OK");
    }

    /* P1D.4 — 2 MB megapage translation */
    TEST_LOG("P1D.4 — 2 MB megapage translation...");
    {
        uint64_t va = 0xE0200000UL;
        uint64_t pa = SRAM_BASE;  /* 2MB aligned */

        int idx2 = (va >> 30) & 0x1FF;
        int idx1 = (va >> 21) & 0x1FF;

        if (!(root[idx2] & PTE_V)) {
            uint64_t *l1 = pt_alloc_page();
            root[idx2] = (((uint64_t)l1 >> PGSHIFT) << PTE_PPN_SHIFT) | PTE_V;
        }
        uint64_t *l1 = (uint64_t *)((root[idx2] >> PTE_PPN_SHIFT) << PGSHIFT);

        uint64_t ppn = pa >> PGSHIFT;
        l1[idx1] = (ppn << PTE_PPN_SHIFT)
                   | PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D;
        sfence_vma_all();

        /* Offset 0: should hit start of SRAM (reset vector, non-zero) */
        volatile uint64_t *ptr0 = (volatile uint64_t *)(va + 0);
        uint64_t val0 = *ptr0;
        test_assert(val0 != 0, "P1D.4 megapage offset 0 read zero", 500);

        /* Offset 4KB: different 4KB page within megapage */
        volatile uint64_t *ptr1 = (volatile uint64_t *)(va + 0x1000);
        (void)*ptr1;

        #if (SRAM_SIZE >= 0x200000)
        /* Near end of megapage, one page below HTIF */
        volatile uint64_t *ptr2 = (volatile uint64_t *)(va + 0x1FE000);
        *ptr2 = 0x000E6A9A6E0FULL;
        ASSERT_EQ(*ptr2, 0x000E6A9A6E0FULL, "P1D.4 megapage end offset", 501);
        #endif

        uart_puts("    Megapage reads at offset 0, 0x1000: OK\n");
        TEST_LOG("P1D.4 — 2 MB megapage: OK");
    }

    /* P1D.5 — Gigapage verification (identity map root entry is a leaf) */
    TEST_LOG("P1D.5 — Gigapage verification...");
    {
        int sram_idx = (SRAM_BASE >> 30) & 0x1FF;
        uint64_t pte = root[sram_idx];
        test_assert((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)),
                    "P1D.5: identity map should be a gigapage leaf", 510);

        uart_puts("    root[");
        uart_put_dec(sram_idx);
        uart_puts("] = ");
        uart_put_hex(pte);
        uart_puts(" (gigapage leaf)\n");

        TEST_LOG("P1D.5 — Gigapage: OK");
    }

    /* P1D.6 — PTW reads PTE through L1 D-cache (functional check) */
    TEST_LOG("P1D.6 — PTW through L1 D-cache...");
    {
        uint64_t va = 0xD3000000UL;
        uint64_t pa = SRAM_BASE + 0x1E8000;

        *(volatile uint64_t *)pa = 0xCAC0E01700000006UL;
        pt_map_page(root, va, pa, PTE_R | PTE_W, 0);
        sfence_vma_all();
        g_trap.fault_count = 0;

        /* fault → handler fixes PTE in D-cache → sfence → PTW re-reads from cache */
        volatile uint64_t *va_ptr = (volatile uint64_t *)va;
        uint64_t val = *va_ptr;

        ASSERT_EQ(g_trap.fault_count, 1, "P1D.6 fault_count", 560);
        ASSERT_EQ(val, 0xCAC0E01700000006UL, "P1D.6 value after PTW cache", 561);

        TEST_LOG("P1D.6 — PTW through D-cache: OK");
    }

    uart_puts("\n========================================\n");
    uart_puts("Phase 1D: ALL TESTS PASSED\n");
    uart_puts("========================================\n");

    return 0;
}
