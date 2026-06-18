/* p4_dualcore.c — Phase 4: Dual Core functionality tests */

#include "test.h"

#define ATOMIC_LOOP_COUNT 100
#define SPINLOCK_LOOP_COUNT 50
#define CORE1_MAGIC_NUMBER 0xDEADBEEF
#define PING_PONG_COUNT 100

typedef struct {
    // P4.0 - smoke test
    volatile uint64_t core1_payload;
    volatile int core1_wrote_payload;

    // P4.1 - fence state variables
    uint64_t fence_payload;
    uint64_t fence_expected;
    int fence_flag;
    int fence_done;
    int fence_ok;
    
    // P4.2 - atomic counter state variables
    volatile uint64_t shared_counter;
    volatile int core1_atomic_done;

    // P4.3 - mutual exclusion state variables
    volatile uint64_t lock;
    volatile uint64_t shared_var;
    volatile int core1_lock_done;

    // P4.4 - ping pong variables
    volatile uint64_t count;
    volatile int turn;
} p4_state_t;

static volatile p4_state_t g_p4 = {0};

/* --- Core 1 Worker Functions --- */

/* P4.0 Core1 function */
static void core1_magic_writer(void)
{
    g_p4.core1_payload = CORE1_MAGIC_NUMBER;
    asm volatile ("fence w, w" ::: "memory");
    g_p4.core1_wrote_payload = 1;
}

/* P4.1 Core1 functions */
static void core1_fence_consumer(void)
{
    while (g_p4.fence_flag == 0)
        asm volatile("" ::: "memory");

    asm volatile("fence r, rw" ::: "memory");
    g_p4.fence_ok = (g_p4.fence_payload == g_p4.fence_expected) ? 1 : 0;
    asm volatile("fence w, w" ::: "memory");
    g_p4.fence_done = 1;
}

static void core1_fence_producer(void)
{
    g_p4.fence_payload = g_p4.fence_expected;
    asm volatile("fence w, w" ::: "memory");
    g_p4.fence_flag = 1;

    spin_wait(&g_p4.fence_done, 1);
}

/* P4.2 Core1 function */
static void core1_atomic_incrementer(void)
{
    for (int i = 0; i < ATOMIC_LOOP_COUNT; i++) {
        amo_add_d(&g_p4.shared_counter, 1);
    }
    g_p4.core1_atomic_done = 1;
}

/* P4.3 Core1 functions */

static inline void spinlock_acquire(volatile uint64_t *lock) {
    while (amo_swap_d(lock, 1) != 0) {
        asm volatile("" ::: "memory");
    }
    asm volatile("fence r, rw" ::: "memory");
}

static inline void spinlock_release(volatile uint64_t *lock) {
    asm volatile("fence rw, w" ::: "memory");
    amo_swap_d(lock,0);
}

static void core1_spinlock_worker(void)
{
    for (int i = 0; i < SPINLOCK_LOOP_COUNT; i++) {
        spinlock_acquire(&g_p4.lock);
        
        // --- CRITICAL SECTION START ---
        uint64_t temp = g_p4.shared_var;
        for (volatile int j = 0; j < 10; j++); // Artificial delay to stretch race window
        g_p4.shared_var = temp + 1;
        // --- CRITICAL SECTION END ---
        
        spinlock_release(&g_p4.lock);
    }
    
    asm volatile("fence w, w" ::: "memory");
    g_p4.core1_lock_done = 1;
}

/* P4.4 Core1 function */
static void ping(void)
{
    for (int i = 0; i < PING_PONG_COUNT; i++)
    {
        spin_wait(&g_p4.turn,1);

        asm volatile("fence r, rw" ::: "memory");
        g_p4.count += 1;
        asm volatile("fence rw, w" ::: "memory");
        g_p4.turn = 0;
    }
}

int test_main(void)
{
    uart_puts("\n========================================\n");
    uart_puts("Phase 4: Dual Core Test\n");
    uart_puts("========================================\n");

    g_p4 = (p4_state_t){0};

    /* P4.0 — Dual core smoke test */
    TEST_LOG("P4.0 — Dual core smoke test (core1 write + core 0 read)");
    {
        wake_core1_smode(core1_magic_writer); // let core1 write the payload into the g_p4 struct
        spin_wait(&g_p4.core1_wrote_payload, 1); // core 0 waits for completion signal

        ASSERT_EQ(g_p4.core1_payload, CORE1_MAGIC_NUMBER, "Core 0 reads different from what Core 1 wrote", 760);
        TEST_LOG("P4.0 — dual core smoke test: OK");

    }

    g_p4 = (p4_state_t){0};

    /* P4.1 — Fence ordering with dual-core producer/consumer */
    TEST_LOG("P4.1 — fence ordering between cores...");
    {
        /* core0 producer -> core1 consumer */
        g_p4.fence_expected = CORE1_MAGIC_NUMBER;

        wake_core1_smode(core1_fence_consumer);

        g_p4.fence_payload = g_p4.fence_expected;
        asm volatile("fence w, w" ::: "memory");
        g_p4.fence_flag = 1;

        // Wait for Core 1 to signal completion
        spin_wait(&g_p4.fence_done, 1);

        // ensure proper read on fence done before validating test 
        asm volatile("fence r, rw" ::: "memory");
        ASSERT_EQ(g_p4.fence_ok, 1, "P4.1A: core1 observed stale payload", 761);
        TEST_LOG("P4.1A — fence ordering - core0 -> core1: OK");

        /* core1 producer -> core0 consumer */
        g_p4 = (p4_state_t){0};
        g_p4.fence_expected = CORE1_MAGIC_NUMBER;

        wake_core1_smode(core1_fence_producer);

        while (g_p4.fence_flag == 0)
            asm volatile("" ::: "memory");

        asm volatile("fence r, rw" ::: "memory");
        g_p4.fence_ok = (g_p4.fence_payload == g_p4.fence_expected) ? 1 : 0;
        asm volatile("fence w, w" ::: "memory");
        g_p4.fence_done = 1;

        ASSERT_EQ(g_p4.fence_ok, 1, "P4.1B: core0 observed stale payload", 762);
        TEST_LOG("P4.1B — fence ordering - core1 -> core0: OK");

        TEST_LOG("P4.1 — fence ordering: OK");
    }

    g_p4 = (p4_state_t){0};

    /* P4.2 — Atomic concurrence stress test (amoadd.d) */
    TEST_LOG("P4.2 — Atomic concurrence stress test (amoadd.d)...");
    {
        g_p4.shared_counter = 0;
        g_p4.core1_atomic_done = 0;

        wake_core1_smode(core1_atomic_incrementer);

        // Core 0 also increments the shared counter concurrently
        for (int i = 0; i < ATOMIC_LOOP_COUNT; i++) {
            amo_add_d(&g_p4.shared_counter, 1);
        }

        // Wait for Core 1 to signal completion
        spin_wait(&g_p4.core1_atomic_done, 1);

        uint64_t expected_total = (uint64_t)ATOMIC_LOOP_COUNT * 2;
        ASSERT_EQ(g_p4.shared_counter, expected_total, "P4.2: final counter value mismatch", 771);

        TEST_LOG("P4.2 — Atomic concurrence stress test: OK");
    }

    g_p4 = (p4_state_t){0};

    /* P4.3 —  Mutual exclusion test 64bit (amoswap.d) */
    TEST_LOG("P4.3 — Mutual exclusion test (amoswap.d)...");
    {
        g_p4.lock = 0;
        g_p4.shared_var = 0;
        g_p4.core1_lock_done = 0;

        wake_core1_smode(core1_spinlock_worker);

        for (int i = 0; i < SPINLOCK_LOOP_COUNT; i++) {
            spinlock_acquire(&g_p4.lock);

            // --- CRITICAL SECTION START ---            
            uint64_t temp = g_p4.shared_var;
            for (volatile int j = 0; j < 10; j++); // Artificial delay
            g_p4.shared_var = temp + 1;
            // --- CRITICAL SECTION END ---
            
            spinlock_release(&g_p4.lock);
        }

        spin_wait(&g_p4.core1_lock_done, 1);

        uint64_t expected_total = (uint64_t)SPINLOCK_LOOP_COUNT * 2;
        ASSERT_EQ(g_p4.shared_var, expected_total, "P4.3: mutual exclusion failed", 781);

        TEST_LOG("P4.3 — Mutual exclusion test: OK");
    }

    g_p4 = (p4_state_t){0};

    /* P4.4 —  Ping Pong test */
    TEST_LOG("P4.4 — Ping Pong test ...");
    {
        wake_core1_smode(ping);

        for (int i = 0; i < PING_PONG_COUNT; i++)
        {
            spin_wait(&g_p4.turn,0); // wait for core0's turn

            asm volatile("fence r, rw" ::: "memory");
            g_p4.count += 1; // increment the count
            asm volatile("fence rw, w" ::: "memory");
            g_p4.turn = 1; // send it to core1
        }
        
        // Wait for Core 1 to pass the turn back one last time
        spin_wait(&g_p4.turn, 0);
        asm volatile("fence r, rw" ::: "memory");

        uint64_t expected_total = (uint64_t)PING_PONG_COUNT * 2;
        ASSERT_EQ(g_p4.count, expected_total, "P4.4: ping pong count mismatch", 790);
        TEST_LOG("P4.4 — Ping Pong test: OK");
    }

    uart_puts("\n========================================\n");
    uart_puts("Phase 4: ALL TESTS PASSED\n");
    uart_puts("========================================\n");

    return 0;

}