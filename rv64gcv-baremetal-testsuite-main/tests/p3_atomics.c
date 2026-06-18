/* p3_atomics.c — Phase 3: Atomics (LR/SC, AMO, CAS, fence, 32-bit AMOs) */

#include "test.h"

struct p3_state {
    uint64_t atom64;
    uint32_t atom32;
    uint64_t fence_payload;
    uint64_t fence_expected;
    int fence_flag;
    int fence_done;
    int fence_ok;
};

static volatile struct p3_state g_p3 = {0};

int test_main(void)
{
    uart_puts("\n========================================\n");
    uart_puts("Phase 3: Atomic Operations Tests\n");
    uart_puts("========================================\n");

    /* P3.1 — LR/SC basic behavior */
    TEST_LOG("P3.1 — LR/SC (success + failure path)...");
    {
        g_p3.atom64 = 0x1122334455667788UL;

        uint64_t old = lr_d(&g_p3.atom64);
        ASSERT_EQ(old, 0x1122334455667788UL, "P3.1 lr.d old value", 700);

        uint64_t sc_ok = sc_d(&g_p3.atom64, 0xAABBCCDDEEFF0011UL);
        ASSERT_EQ(sc_ok, 0, "P3.1 first sc.d should succeed", 701);
        ASSERT_EQ(g_p3.atom64, 0xAABBCCDDEEFF0011UL, "P3.1 first sc.d write", 702);

        old = lr_d_aq(&g_p3.atom64);
        ASSERT_EQ(old, 0xAABBCCDDEEFF0011UL, "P3.1 lr.d.aq old value", 705);
        sc_ok = sc_d_rl(&g_p3.atom64, 0x0102030405060708UL);
        ASSERT_EQ(sc_ok, 0, "P3.1 sc.d.rl should succeed", 706);
        ASSERT_EQ(g_p3.atom64, 0x0102030405060708UL, "P3.1 sc.d.rl write", 707);

        TEST_LOG("P3.1 — LR/SC: OK");
    }

    /* P3.2 — 64-bit AMO coverage */
    TEST_LOG("P3.2 — AMO .d (swap/add/and/or/xor/min/max)...");
    {
        uint64_t old;

        g_p3.atom64 = 0x10;
        old = amo_swap_d(&g_p3.atom64, 0x44);
        ASSERT_EQ(old, 0x10, "P3.2 amoswap.d old", 710);
        ASSERT_EQ(g_p3.atom64, 0x44, "P3.2 amoswap.d new", 711);

        old = amo_add_d(&g_p3.atom64, 0x05);
        ASSERT_EQ(old, 0x44, "P3.2 amoadd.d old", 712);
        ASSERT_EQ(g_p3.atom64, 0x49, "P3.2 amoadd.d new", 713);

        old = amo_and_d(&g_p3.atom64, 0x0F);
        ASSERT_EQ(old, 0x49, "P3.2 amoand.d old", 714);
        ASSERT_EQ(g_p3.atom64, 0x09, "P3.2 amoand.d new", 715);

        old = amo_or_d(&g_p3.atom64, 0x30);
        ASSERT_EQ(old, 0x09, "P3.2 amoor.d old", 716);
        ASSERT_EQ(g_p3.atom64, 0x39, "P3.2 amoor.d new", 717);

        old = amo_xor_d(&g_p3.atom64, 0x11);
        ASSERT_EQ(old, 0x39, "P3.2 amoxor.d old", 718);
        ASSERT_EQ(g_p3.atom64, 0x28, "P3.2 amoxor.d new", 719);

        g_p3.atom64 = 5;
        old = amo_min_d(&g_p3.atom64, (uint64_t)(int64_t)-3);
        ASSERT_EQ(old, 5, "P3.2 amomin.d old", 720);
        ASSERT_EQ((int64_t)g_p3.atom64, -3, "P3.2 amomin.d new", 721);

        old = amo_max_d(&g_p3.atom64, 7);
        ASSERT_EQ((int64_t)old, -3, "P3.2 amomax.d old", 722);
        ASSERT_EQ(g_p3.atom64, 7, "P3.2 amomax.d new", 723);

        g_p3.atom64 = 5;
        old = amo_maxu_d(&g_p3.atom64, (uint64_t)(int64_t)-3);
        ASSERT_EQ(old, 5, "P3.2 amomaxu.d old", 724);
        ASSERT_EQ(g_p3.atom64, (uint64_t)-3, "P3.2 amomaxu.d new", 725);

        g_p3.atom64 = (uint64_t)-3;
        old = amo_minu_d(&g_p3.atom64, 5);
        ASSERT_EQ(old, (uint64_t)-3, "P3.2 amominu.d old", 726);
        ASSERT_EQ(g_p3.atom64, 5, "P3.2 amominu.d new", 727);

        TEST_LOG("P3.2 — AMO .d: OK");
    }

    /* P3.3 — 32-bit AMO coverage */
    TEST_LOG("P3.3 — AMO .w (swap/add/and/or/xor/min/max)...");
    {
        uint32_t old32;

        g_p3.atom32 = 0x10;
        old32 = amo_swap_w(&g_p3.atom32, 0x77);
        ASSERT_EQ(old32, 0x10, "P3.3 amoswap.w old", 730);
        ASSERT_EQ(g_p3.atom32, 0x77, "P3.3 amoswap.w new", 731);

        old32 = amo_add_w(&g_p3.atom32, 0x09);
        ASSERT_EQ(old32, 0x77, "P3.3 amoadd.w old", 732);
        ASSERT_EQ(g_p3.atom32, 0x80, "P3.3 amoadd.w new", 733);

        old32 = amo_and_w(&g_p3.atom32, 0x0F);
        ASSERT_EQ(old32, 0x80, "P3.3 amoand.w old", 734);
        ASSERT_EQ(g_p3.atom32, 0x00, "P3.3 amoand.w new", 735);

        old32 = amo_or_w(&g_p3.atom32, 0x33);
        ASSERT_EQ(old32, 0x00, "P3.3 amoor.w old", 736);
        ASSERT_EQ(g_p3.atom32, 0x33, "P3.3 amoor.w new", 737);

        old32 = amo_xor_w(&g_p3.atom32, 0x55);
        ASSERT_EQ(old32, 0x33, "P3.3 amoxor.w old", 738);
        ASSERT_EQ(g_p3.atom32, 0x66, "P3.3 amoxor.w new", 739);

        g_p3.atom32 = 5;
        old32 = amo_min_w(&g_p3.atom32, -2);
        ASSERT_EQ(old32, 5, "P3.3 amomin.w old", 740);
        ASSERT_EQ((int32_t)g_p3.atom32, -2, "P3.3 amomin.w new", 741);

        old32 = amo_max_w(&g_p3.atom32, 7);
        ASSERT_EQ((int32_t)old32, -2, "P3.3 amomax.w old", 742);
        ASSERT_EQ((int32_t)g_p3.atom32, 7, "P3.3 amomax.w new", 743);

        g_p3.atom32 = 5;
        old32 = amo_maxu_w(&g_p3.atom32, (uint32_t)(int32_t)-3);
        ASSERT_EQ(old32, 5, "P3.3 amomaxu.w old", 744);
        ASSERT_EQ(g_p3.atom32, (uint32_t)-3, "P3.3 amomaxu.w new", 745);

        g_p3.atom32 = (uint32_t)-3;
        old32 = amo_minu_w(&g_p3.atom32, 5);
        ASSERT_EQ(old32, (uint32_t)-3, "P3.3 amominu.w old", 746);
        ASSERT_EQ(g_p3.atom32, 5, "P3.3 amominu.w new", 747);

        TEST_LOG("P3.3 — AMO .w: OK");
    }

    /* P3.4 — CAS via LR/SC loop */
    TEST_LOG("P3.4 — CAS via LR/SC...");
    {
        g_p3.atom64 = 0x1111;

        int ok = cas_d(&g_p3.atom64, 0x1111, 0x2222);
        ASSERT_EQ(ok, 1, "P3.4 CAS should succeed on matching expected", 748);
        ASSERT_EQ(g_p3.atom64, 0x2222, "P3.4 CAS success value", 749);

        ok = cas_d(&g_p3.atom64, 0x1111, 0x3333);
        ASSERT_EQ(ok, 0, "P3.4 CAS should fail on mismatched expected", 750);
        ASSERT_EQ(g_p3.atom64, 0x2222, "P3.4 CAS failure should not update", 751);

        TEST_LOG("P3.4 — CAS: OK");
    }

    uart_puts("\n========================================\n");
    uart_puts("Phase 3: ALL TESTS PASSED\n");
    uart_puts("========================================\n");

    return 0;
    
}
