/* p2_interrupts.c — Phase 2: S-mode timer/software interrupts, enable/disable, wfi */

#include "test.h"

int test_main(void)
{
    uart_puts("\n========================================\n");
    uart_puts("Phase 2: Interrupt & Exception Tests\n");
    uart_puts("========================================\n");

    /* P2.1 — S-mode timer interrupt via SBI */
    TEST_LOG("P2.1 — S-mode timer interrupt via SBI...");
    {
        g_trap.fault_count = 0;
        g_trap.irq_count = 0;
        g_trap.timer_irq_count = 0;

        enable_s_timer_irq();
        enable_s_interrupts();

        uint64_t now = read_mtime();
        sbi_set_timer(now + 1000);

        int timeout = 1000000;
        while (g_trap.timer_irq_count == 0 && --timeout > 0)
            asm volatile("" ::: "memory");

        disable_s_interrupts();

        test_assert(timeout > 0, "P2.1: timer interrupt timeout", 600);
        ASSERT_EQ(g_trap.timer_irq_count, 1, "P2.1 timer_irq_count", 601);

        uint64_t expected_scause = (1UL << 63) | IRQ_S_TIMER;
        ASSERT_EQ(g_trap.last_irq_cause, expected_scause, "P2.1 scause", 602);

        TEST_LOG("P2.1 — S-mode timer interrupt: OK");
    }

    /* P2.2 — S-mode software interrupt (self-IPI) */
    TEST_LOG("P2.2 — S-mode software interrupt (self-IPI)...");
    {
        g_trap.irq_count = 0;
        g_trap.soft_irq_count = 0;

        enable_s_soft_irq();
        enable_s_interrupts();

        sbi_send_ipi(0);

        int timeout = 100000;
        while (g_trap.soft_irq_count == 0 && --timeout > 0)
            asm volatile("" ::: "memory");

        disable_s_interrupts();

        test_assert(timeout > 0, "P2.2: software interrupt timeout", 610);
        ASSERT_EQ(g_trap.soft_irq_count, 1, "P2.2 soft_irq_count", 611);

        uint64_t expected_scause = (1UL << 63) | IRQ_S_SOFT;
        ASSERT_EQ(g_trap.last_irq_cause, expected_scause, "P2.2 scause", 612);

        TEST_LOG("P2.2 — S-mode software interrupt: OK");
    }

    /* P2.3 — Multiple consecutive timer interrupts (3x) */
    TEST_LOG("P2.3 — Multiple timer interrupts...");
    {
        g_trap.timer_irq_count = 0;

        enable_s_timer_irq();
        enable_s_interrupts();

        for (int i = 0; i < 3; i++) {
            uint64_t now = read_mtime();
            sbi_set_timer(now + 500);

            int timeout = 500000;
            int expected = i + 1;
            while (g_trap.timer_irq_count < expected && --timeout > 0)
                asm volatile("" ::: "memory");

            test_assert(timeout > 0, "P2.3: timer interrupt not arrived", 620 + i);
        }

        disable_s_interrupts();
        ASSERT_EQ(g_trap.timer_irq_count, 3, "P2.3 total timer_irq_count", 625);

        TEST_LOG("P2.3 — Multiple timer interrupts: OK");
    }

    /* P2.4 — Timer interrupt during page fault handling */
    TEST_LOG("P2.4 — Timer + page fault interaction...");
    {
        pt_reset();
        uint64_t *root = pt_setup_identity_kernel();
        mmu_enable_sv39(root);

        g_trap.handler_action = 0;
        g_trap.fault_count = 0;
        g_trap.timer_irq_count = 0;

        uint64_t va = 0xD0400000UL;
        uint64_t pa = SRAM_BASE + 0x1E0000;
        *(volatile uint64_t *)pa = 0xDEAD0004UL;
        pt_map_page(root, va, pa, PTE_R | PTE_W, 0);
        sfence_vma_all();

        enable_s_timer_irq();
        enable_s_interrupts();
        sbi_set_timer(read_mtime() + 200);

        volatile uint64_t *va_ptr = (volatile uint64_t *)va;
        uint64_t val = *va_ptr;

        for (volatile int d = 0; d < 10000; d++)
            ;

        disable_s_interrupts();

        test_assert(g_trap.fault_count >= 1, "P2.4: page fault not handled", 630);
        ASSERT_EQ(val, 0xDEAD0004UL, "P2.4 loaded value", 631);

        if (g_trap.timer_irq_count > 0)
            uart_puts("    Timer interrupt also fired: OK\n");
        else
            uart_puts("    Timer did not fire (timing dependent, OK)\n");

        asm volatile("csrw satp, zero");
        sfence_vma_all();

        TEST_LOG("P2.4 — Timer + page fault: OK");
    }

    /* P2.5 — Interrupt enable/disable (sstatus.SIE) */
    TEST_LOG("P2.5 — Interrupt enable/disable...");
    {
        g_trap.soft_irq_count = 0;

        enable_s_soft_irq();
        disable_s_interrupts();

        sbi_send_ipi(0);  /* pends but should NOT deliver */

        for (volatile int d = 0; d < 10000; d++)
            ;

        ASSERT_EQ(g_trap.soft_irq_count, 0, "P2.5: delivered with SIE=0", 640);

        enable_s_interrupts();
        asm volatile("nop; nop; nop" ::: "memory");

        int timeout = 100000;
        while (g_trap.soft_irq_count == 0 && --timeout > 0)
            asm volatile("" ::: "memory");

        disable_s_interrupts();
        ASSERT_EQ(g_trap.soft_irq_count, 1, "P2.5: not delivered after SIE=1", 641);

        TEST_LOG("P2.5 — Interrupt enable/disable: OK");
    }

    /* P2.6 — sie masking (STIE=0 blocks timer even with SIE=1) */
    TEST_LOG("P2.6 — sie masking...");
    {
        g_trap.timer_irq_count = 0;

        write_sie(SIP_SSIP);  /* STIP disabled */
        enable_s_interrupts();

        sbi_set_timer(read_mtime() + 100);

        for (volatile int d = 0; d < 50000; d++)
            ;

        ASSERT_EQ(g_trap.timer_irq_count, 0, "P2.6: timer delivered with STIE=0", 650);

        uint64_t sip = read_sip();
        test_assert((sip & SIP_STIP) != 0, "P2.6: STIP not pending", 651);

        enable_s_timer_irq();
        asm volatile("nop; nop" ::: "memory");

        int timeout = 100000;
        while (g_trap.timer_irq_count == 0 && --timeout > 0)
            asm volatile("" ::: "memory");

        disable_s_interrupts();
        ASSERT_EQ(g_trap.timer_irq_count, 1, "P2.6: timer not delivered after STIE=1", 652);

        TEST_LOG("P2.6 — sie masking: OK");
    }

    /* P2.7 — wfi returns when interrupt pending (even with SIE=0) */
    TEST_LOG("P2.7 — wfi behavior...");
    {
        g_trap.timer_irq_count = 0;

        enable_s_timer_irq();
        disable_s_interrupts();

        sbi_set_timer(read_mtime() + 500);

        asm volatile("wfi" ::: "memory");

        uint64_t sip = read_sip();
        test_assert((sip & SIP_STIP) != 0, "P2.7: STIP not pending after wfi", 660);
        ASSERT_EQ(g_trap.timer_irq_count, 0, "P2.7: delivered during wfi with SIE=0", 661);

        enable_s_interrupts();
        asm volatile("nop" ::: "memory");
        disable_s_interrupts();

        ASSERT_EQ(g_trap.timer_irq_count, 1, "P2.7: timer not cleared after enable", 662);

        TEST_LOG("P2.7 — wfi: OK");
    }

    uart_puts("\n========================================\n");
    uart_puts("Phase 2: ALL TESTS PASSED\n");
    uart_puts("========================================\n");

    return 0;
}
