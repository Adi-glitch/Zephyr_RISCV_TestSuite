/*
 * Phase 4 (dual-core) — idiomatic Zephyr suite.
 *
 * Mirrors the bare-metal suite's P4 with Zephyr SMP. The suite hand-rolled the
 * second hart's life cycle (park loop + CLINT IPI wake); here the kernel boots
 * both CPUs and we steer work with pinned threads:
 *
 *   P4.1 wake hart 1          -> thread pinned to CPU 1 runs there
 *   P4.2 fence prod/consumer  -> data written before an atomic flag is seen
 *                                after it (atomics carry acquire/release);
 *                                both directions (CPU0->CPU1 and CPU1->CPU0),
 *                                the suite's P4.1A/P4.1B
 *   P4.3 atomic stress        -> 2 CPUs x 100 atomic_inc -> exactly 200
 *   P4.4 spinlock mutex       -> k_spinlock guards a plain counter -> exact total
 *   P4.5 ping-pong            -> two k_sems bounce between pinned threads
 *
 * Pinning uses k_thread_cpu_pin (CONFIG_SCHED_CPU_MASK), legal only on
 * not-yet-started threads: create with K_FOREVER, pin, then k_thread_start.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/spinlock.h>
#include <zephyr/arch/cpu.h>

#define STACK_SIZE (1024 + CONFIG_TEST_EXTRA_STACK_SIZE)
#define WORKER_PRIO K_PRIO_PREEMPT(1)
#define SPIN_LIMIT  (10 * 1000 * 1000)

static K_THREAD_STACK_ARRAY_DEFINE(worker_stacks, 2, STACK_SIZE);
static struct k_thread workers[2];

/* create on a given CPU, pinned, ready to run */
static k_tid_t start_worker_on(int cpu, int idx, k_thread_entry_t fn,
			       void *p1, void *p2, void *p3)
{
	k_tid_t tid = k_thread_create(&workers[idx], worker_stacks[idx],
				      STACK_SIZE, fn, p1, p2, p3,
				      WORKER_PRIO, 0, K_FOREVER);

	zassert_ok(k_thread_cpu_pin(tid, cpu), "pin to CPU %d failed", cpu);
	k_thread_start(tid);
	return tid;
}

/* P4.1: a thread pinned to CPU 1 really executes on CPU 1. */
static volatile int observed_cpu = -1;

static void record_cpu_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	unsigned int key = arch_irq_lock(); /* don't migrate mid-read */

	observed_cpu = arch_curr_cpu()->id;
	arch_irq_unlock(key);
}

ZTEST(p4_dualcore, test_thread_runs_on_second_cpu)
{
	observed_cpu = -1;

	k_tid_t tid = start_worker_on(1, 0, record_cpu_entry, NULL, NULL, NULL);

	zassert_ok(k_thread_join(tid, K_SECONDS(5)), "worker did not finish");
	zassert_equal(observed_cpu, 1, "pinned thread ran on CPU %d", observed_cpu);
}

/* P4.2: producer/consumer ordering across cores. The producer fills a buffer
 * then sets an atomic flag; the consumer (other CPU) spins on the flag and
 * must then see every buffer write. RISC-V atomics carry .aq/.rl semantics —
 * the suite did this with explicit fence w,w / fence r,r. */
#define BUF_WORDS 64
static volatile uint32_t shared_buf[BUF_WORDS];
static atomic_t publish_flag;
static volatile int consumer_bad_words;

static void producer_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	for (int i = 0; i < BUF_WORDS; i++) {
		shared_buf[i] = 0xC0DE0000u | i;
	}
	atomic_set(&publish_flag, 1);
}

static void consumer_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	int spins = 0;

	while (atomic_get(&publish_flag) == 0) {
		if (++spins > SPIN_LIMIT) {
			consumer_bad_words = -1; /* flag never seen */
			return;
		}
	}

	for (int i = 0; i < BUF_WORDS; i++) {
		if (shared_buf[i] != (0xC0DE0000u | i)) {
			consumer_bad_words++;
		}
	}
}

ZTEST(p4_dualcore, test_fence_producer_consumer)
{
	atomic_set(&publish_flag, 0);
	consumer_bad_words = 0;
	for (int i = 0; i < BUF_WORDS; i++) {
		shared_buf[i] = 0;
	}

	k_tid_t consumer = start_worker_on(1, 0, consumer_entry, NULL, NULL, NULL);
	k_tid_t producer = start_worker_on(0, 1, producer_entry, NULL, NULL, NULL);

	zassert_ok(k_thread_join(producer, K_SECONDS(5)), "producer stuck");
	zassert_ok(k_thread_join(consumer, K_SECONDS(5)), "consumer stuck");

	zassert_not_equal(consumer_bad_words, -1, "consumer never saw the flag");
	zassert_equal(consumer_bad_words, 0,
		      "%d stale words seen after flag", consumer_bad_words);
}

/* P4.1B: the reverse direction — producer on CPU1, consumer on CPU0. Same
 * ordering guarantee must hold the other way across the cores. */
ZTEST(p4_dualcore, test_fence_producer_consumer_rev)
{
	atomic_set(&publish_flag, 0);
	consumer_bad_words = 0;
	for (int i = 0; i < BUF_WORDS; i++) {
		shared_buf[i] = 0;
	}

	k_tid_t consumer = start_worker_on(0, 0, consumer_entry, NULL, NULL, NULL);
	k_tid_t producer = start_worker_on(1, 1, producer_entry, NULL, NULL, NULL);

	zassert_ok(k_thread_join(producer, K_SECONDS(5)), "producer stuck");
	zassert_ok(k_thread_join(consumer, K_SECONDS(5)), "consumer stuck");

	zassert_not_equal(consumer_bad_words, -1, "consumer never saw the flag");
	zassert_equal(consumer_bad_words, 0,
		      "%d stale words seen after flag (rev)", consumer_bad_words);
}

/* P4.3: both CPUs hammer one counter with atomic_inc — no lost updates.
 * Same expected total as the suite: 2 x 100 = 200. */
#define INC_PER_CPU 100
static atomic_t stress_counter;

static void inc_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	for (int i = 0; i < INC_PER_CPU; i++) {
		atomic_inc(&stress_counter);
	}
}

ZTEST(p4_dualcore, test_atomic_increment_stress)
{
	atomic_set(&stress_counter, 0);

	k_tid_t t0 = start_worker_on(0, 0, inc_entry, NULL, NULL, NULL);
	k_tid_t t1 = start_worker_on(1, 1, inc_entry, NULL, NULL, NULL);

	zassert_ok(k_thread_join(t0, K_SECONDS(5)), "CPU0 worker stuck");
	zassert_ok(k_thread_join(t1, K_SECONDS(5)), "CPU1 worker stuck");

	zassert_equal(atomic_get(&stress_counter), 2 * INC_PER_CPU,
		      "counter %ld != %d", (long)atomic_get(&stress_counter),
		      2 * INC_PER_CPU);
}

/* P4.4: k_spinlock protects a plain (non-atomic) read-modify-write from both
 * CPUs. Same expected total as the suite: 2 x 50 = 100. */
#define LOCKED_INC_PER_CPU 50
static struct k_spinlock counter_lock;
static volatile int locked_counter;

static void spinlock_inc_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	for (int i = 0; i < LOCKED_INC_PER_CPU; i++) {
		k_spinlock_key_t key = k_spin_lock(&counter_lock);

		locked_counter = locked_counter + 1; /* deliberately non-atomic */
		k_spin_unlock(&counter_lock, key);
	}
}

ZTEST(p4_dualcore, test_spinlock_mutual_exclusion)
{
	locked_counter = 0;

	k_tid_t t0 = start_worker_on(0, 0, spinlock_inc_entry, NULL, NULL, NULL);
	k_tid_t t1 = start_worker_on(1, 1, spinlock_inc_entry, NULL, NULL, NULL);

	zassert_ok(k_thread_join(t0, K_SECONDS(5)), "CPU0 worker stuck");
	zassert_ok(k_thread_join(t1, K_SECONDS(5)), "CPU1 worker stuck");

	zassert_equal(locked_counter, 2 * LOCKED_INC_PER_CPU,
		      "counter %d != %d", locked_counter, 2 * LOCKED_INC_PER_CPU);
}

/* P4.5: ping-pong — strict alternation between CPUs via two semaphores
 * (the suite bounced a shared variable; k_sem is the kernel's signalling
 * primitive, and gives across cores via the scheduler IPI). */
#define PINGPONG_ROUNDS 20
static struct k_sem ping_sem, pong_sem;
static volatile int pong_rounds;

static void ponger_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	for (int i = 0; i < PINGPONG_ROUNDS; i++) {
		if (k_sem_take(&ping_sem, K_SECONDS(5)) != 0) {
			return; /* rounds counter exposes the miss */
		}
		pong_rounds++;
		k_sem_give(&pong_sem);
	}
}

ZTEST(p4_dualcore, test_ping_pong)
{
	k_sem_init(&ping_sem, 0, 1);
	k_sem_init(&pong_sem, 0, 1);
	pong_rounds = 0;

	k_tid_t ponger = start_worker_on(1, 0, ponger_entry, NULL, NULL, NULL);

	for (int i = 0; i < PINGPONG_ROUNDS; i++) {
		k_sem_give(&ping_sem);
		zassert_ok(k_sem_take(&pong_sem, K_SECONDS(5)),
			   "no pong in round %d", i);
	}

	zassert_ok(k_thread_join(ponger, K_SECONDS(5)), "ponger stuck");
	zassert_equal(pong_rounds, PINGPONG_ROUNDS,
		      "completed %d rounds, want %d", pong_rounds, PINGPONG_ROUNDS);
}

ZTEST_SUITE(p4_dualcore, NULL, NULL, NULL, NULL, NULL);
