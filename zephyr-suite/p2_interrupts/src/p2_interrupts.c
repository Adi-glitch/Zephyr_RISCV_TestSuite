/*
 * Phase 2 (interrupts) — idiomatic Zephyr suite.
 *
 * Mirrors the bare-metal suite's P2 on a full Zephyr kernel. On the S-mode
 * board the timer path is the suite's P2.1 flow verbatim (verified in source):
 *
 *   k_timer_start
 *     -> drivers/timer/riscv_supervisor_timer.c sbi_set_timer(): SBI_EXT_TIME
 *        ecall (S -> M)                       [suite: test.h sbi_set_timer()]
 *     -> arch/riscv/core/sbi.S writes CLINT mtimecmp
 *                                             [suite: m_trap.c SBI_SET_TIMER]
 *     -> MTIP fires in M-mode; sbi.S "forwards to S-mode by raising STIP"
 *        (csrs mip, MIP_STIP)                 [suite: m_trap.c reflects MTIP]
 *     -> S-timer interrupt, scause = Interrupt|5 (IRQ_S_TIMER)
 *     -> driver timer_isr -> sys_clock_announce -> k_timer expiry callback
 *                                             [suite: s_trap_handler counts]
 *
 * The expiry callback runs nested inside that S-timer ISR, where scause still
 * holds the live trap cause — so the callback captures scause/sie and P2.1
 * asserts them: direct chip-level evidence that the interrupt delivering the
 * callback was the S-timer interrupt injected by M-mode, not just "a k_timer
 * callback ran".
 *
 *   P2.1 S-timer IRQ via SBI   -> k_timer one-shot; assert ISR context AND
 *                                 scause == Interrupt|IRQ_S_TIMER, sie.STIE set
 *   P2.2 soft IRQ (self-IPI)   -> irq_offload(): run a handler in IRQ context
 *   P2.3 repeated timer IRQs   -> periodic k_timer, count N expirations
 *   P2.4 timer x page-fault    -> a fix-retried page fault (the P1C MMU hook)
 *                                 while a periodic timer is running: the load
 *                                 lands AND the timer keeps ticking
 *   P2.5/6 SIE/STIE masking    -> irq_lock(): expiry held off while locked,
 *                                 delivered promptly after unlock
 *   (ISR->thread signalling via k_sem throughout, the idiomatic pattern)
 *
 * Still in baremetal/ only (fight the kernel's ownership of the system timer):
 * P2.6 per-bit sie.STIE mask, P2.7 raw wfi.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/irq_offload.h>
#include <zephyr/sys/printk.h>
#include <zephyr/arch/riscv/csr.h>
#include <zephyr/arch/riscv/irq.h>
#include <kernel_arch_func.h>
#include <stdint.h>

#define PAGE_SIZE_4K 4096
#define EXC_LOAD_PAGE_FAULT 13
#define VA_P24 0xD0400000UL /* suite P2.4 VA */

static struct k_timer timer;
static struct k_sem timer_sem;
static volatile int timer_count;
static volatile bool timer_cb_in_isr;
static volatile unsigned long cb_scause;
static volatile unsigned long cb_sie;

/* P2.4 page-fault resolver (load only): set V on the leaf and retry. */
static uint64_t bp_p24[512] __aligned(PAGE_SIZE_4K);
static volatile int fix_count;

static bool p2_fixup(unsigned long scause, unsigned long stval)
{
	uint64_t *pte;

	if (scause != EXC_LOAD_PAGE_FAULT) {
		return false;
	}
	pte = z_riscv_mmu_pte_lookup(stval);
	if (pte == NULL) {
		return false;
	}
	*pte |= PTE_V | PTE_R | PTE_A;
	__asm__ volatile("sfence.vma" ::: "memory");
	fix_count++;
	return true;
}

static void timer_expiry(struct k_timer *t)
{
	ARG_UNUSED(t);
	timer_count++;
	timer_cb_in_isr = k_is_in_isr();
	/* Nested in the delivering ISR: scause still holds the live cause. */
	cb_scause = csr_read(scause);
	cb_sie = csr_read(sie);
	k_sem_give(&timer_sem);
}

static void *p2_setup(void)
{
	k_timer_init(&timer, timer_expiry, NULL);
	k_sem_init(&timer_sem, 0, K_SEM_MAX_LIMIT);
	return NULL;
}

static void p2_before(void *fixture)
{
	ARG_UNUSED(fixture);
	k_timer_stop(&timer);
	k_sem_reset(&timer_sem);
	timer_count = 0;
	timer_cb_in_isr = false;
	cb_scause = 0;
	cb_sie = 0;
	z_riscv_mmu_set_fault_fixup(NULL);
	fix_count = 0;
}

/* P2.1: one S-timer IRQ fires, exactly once, in ISR context — and the CSRs
 * prove it came in as the S-timer interrupt (the M-mode-injected STIP path),
 * with the S-timer interrupt-enable set. */
ZTEST(p2_interrupts, test_timer_irq_fires_once)
{
	k_timer_start(&timer, K_MSEC(10), K_NO_WAIT);

	zassert_ok(k_sem_take(&timer_sem, K_MSEC(100)), "timer IRQ never fired");
	zassert_true(timer_cb_in_isr, "timer callback not in ISR context");

	printk("callback saw scause=0x%016lx sie=0x%lx\n", cb_scause, cb_sie);
	zassert_equal(cb_scause, RISCV_MCAUSE_IRQ_BIT | IRQ_S_TIMER,
		      "scause 0x%lx != S-timer interrupt (Interrupt|%d)",
		      cb_scause, IRQ_S_TIMER);
	zassert_true((cb_sie & BIT(IRQ_S_TIMER)) != 0,
		     "sie.STIE clear (sie=0x%lx)", cb_sie);

	/* one-shot: give it room to misfire, then check the count is still 1 */
	k_msleep(30);
	zassert_equal(timer_count, 1, "timer fired %d times, want 1", timer_count);
}

/* P2.2: run code in IRQ context on demand (the suite's self-IPI equivalent). */
static volatile bool offload_ran;
static volatile bool offload_in_isr;

static void offload_handler(const void *param)
{
	ARG_UNUSED(param);
	offload_ran = true;
	offload_in_isr = k_is_in_isr();
}

ZTEST(p2_interrupts, test_soft_irq_via_offload)
{
	offload_ran = false;
	offload_in_isr = false;

	irq_offload(offload_handler, NULL);

	zassert_true(offload_ran, "offload handler did not run");
	zassert_true(offload_in_isr, "offload handler not in ISR context");
}

/* P2.3: periodic timer delivers repeated IRQs. */
ZTEST(p2_interrupts, test_timer_irq_repeats)
{
	const int want = 5;

	k_timer_start(&timer, K_MSEC(5), K_MSEC(5));

	for (int i = 0; i < want; i++) {
		zassert_ok(k_sem_take(&timer_sem, K_MSEC(100)),
			   "missed expiry %d", i);
	}
	k_timer_stop(&timer);

	zassert_true(timer_count >= want, "only %d expirations", timer_count);
}

/* P2.5/6: with IRQs locked the expiry is held pending; it is delivered
 * promptly once unlocked. (irq_lock clears sstatus.SIE, the suite's P2.5.) */
ZTEST(p2_interrupts, test_irq_lock_masks_timer)
{
	unsigned int key = irq_lock();

	k_timer_start(&timer, K_MSEC(5), K_NO_WAIT);

	/* spin well past the expiry without sleeping (can't sleep locked) */
	k_busy_wait(20 * 1000);
	zassert_equal(timer_count, 0,
		      "timer callback ran with IRQs locked (count %d)", timer_count);

	irq_unlock(key);

	/* pending IRQ must be delivered now */
	zassert_ok(k_sem_take(&timer_sem, K_MSEC(100)),
		   "pending timer IRQ not delivered after unlock");
	zassert_equal(timer_count, 1, "timer count %d after unlock", timer_count);
}

/* P2.4: a page fault is fixed-and-retried (the P1C MMU hook) while a periodic
 * timer is running — both mechanisms coexist. Maps a V=0 page with a known
 * value, lets timer IRQs flow, faults on the load (resolved + retried), then
 * checks the load landed AND the timer kept ticking. */
ZTEST(p2_interrupts, test_p2_4_timer_during_fault)
{
	bp_p24[0] = 0xDEAD0004UL; /* via identity PA */

	z_riscv_mmu_set_fault_fixup(p2_fixup);
	z_riscv_mmu_map_raw(VA_P24, (uintptr_t)bp_p24, PTE_R | PTE_W, 0); /* no V */

	k_timer_start(&timer, K_MSEC(1), K_MSEC(1)); /* periodic, running */

	uint64_t val = *(volatile uint64_t *)VA_P24; /* fault 13 -> fix -> retry */

	k_msleep(20); /* let several timer ticks land alongside */
	k_timer_stop(&timer);

	printk("P2.4: fixed load=0x%llx fix_count=%d timer_count=%d\n",
	       (unsigned long long)val, fix_count, timer_count);
	zassert_equal(val, 0xDEAD0004UL, "load after fix+retry");
	zassert_true(fix_count >= 1, "page fault was not handled");
	zassert_true(timer_count > 0, "timer did not tick alongside fault handling");
}

ZTEST_SUITE(p2_interrupts, NULL, p2_setup, p2_before, NULL, NULL);
