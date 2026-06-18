/*
 * Phase 3 (atomics) — idiomatic Zephyr suite.
 *
 * Mirrors the bare-metal suite's P3 features with Zephyr's atomic API. On
 * rv64 with the A extension these compile to the same instructions the suite
 * hand-coded: atomic_t is 64-bit (amo*.d / lr.d+sc.d), and the RISC-V arch
 * header <zephyr/arch/riscv/atomic.h> adds direct amoswap/amomax/amomin
 * wrappers (cf. tests/arch/riscv/atomic).
 *
 *   suite LR/SC + CAS      -> atomic_cas (success + failure paths)
 *   suite amoswap.d        -> atomic_swap / atomic_set (returns old)
 *   suite amoadd.d         -> atomic_add / atomic_sub / atomic_inc / atomic_dec
 *   suite amoand/or/xor.d  -> atomic_and / atomic_or / atomic_xor
 *   suite amomin/max[u].d  -> atomic_min / atomic_max / atomic_minu / atomic_maxu
 *
 * P3.1 (raw lr.d/sc.d incl. .aq/.rl) and P3.3 (32-bit .w AMOs) go via raw
 * inline asm: Zephyr's atomic_t is 64-bit on rv64 and the atomic API hides
 * lr/sc, so neither is reachable through the portable API -- the instruction
 * itself is the chip feature under test (same doctrine as our csr/sfence/ecall
 * asm). lr.d/sc.d run under irq_lock so a timer IRQ can't drop the reservation
 * (the kernel preempts, unlike the bare-metal suite).
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/arch/riscv/atomic.h>
#include <stdint.h>

/* --- raw RV64 atomics (suite test.h equivalents) --- */
static inline uint64_t lr_d(volatile uint64_t *p)
{
	uint64_t v;

	__asm__ volatile("lr.d %0, (%1)" : "=r"(v) : "r"(p) : "memory");
	return v;
}
static inline uint64_t lr_d_aq(volatile uint64_t *p)
{
	uint64_t v;

	__asm__ volatile("lr.d.aq %0, (%1)" : "=r"(v) : "r"(p) : "memory");
	return v;
}
/* sc returns 0 on success, nonzero on failure */
static inline uint64_t sc_d(volatile uint64_t *p, uint64_t v)
{
	uint64_t r;

	__asm__ volatile("sc.d %0, %2, (%1)" : "=r"(r) : "r"(p), "r"(v) : "memory");
	return r;
}
static inline uint64_t sc_d_rl(volatile uint64_t *p, uint64_t v)
{
	uint64_t r;

	__asm__ volatile("sc.d.rl %0, %2, (%1)" : "=r"(r) : "r"(p), "r"(v) : "memory");
	return r;
}

#define DEF_AMO_W(name, instr)                                                 \
	static inline uint32_t name(volatile uint32_t *p, uint32_t v)           \
	{                                                                      \
		uint32_t old;                                                  \
		__asm__ volatile(instr " %0, %2, %1"                           \
				 : "=r"(old), "+A"(*p) : "r"(v) : "memory");   \
		return old;                                                    \
	}
DEF_AMO_W(amo_swap_w, "amoswap.w")
DEF_AMO_W(amo_add_w, "amoadd.w")
DEF_AMO_W(amo_and_w, "amoand.w")
DEF_AMO_W(amo_or_w, "amoor.w")
DEF_AMO_W(amo_xor_w, "amoxor.w")
DEF_AMO_W(amo_min_w, "amomin.w")
DEF_AMO_W(amo_max_w, "amomax.w")
DEF_AMO_W(amo_minu_w, "amominu.w")
DEF_AMO_W(amo_maxu_w, "amomaxu.w")

ZTEST(p3_atomics, test_cas)
{
	atomic_t v = ATOMIC_INIT(42);

	/* failure path: wrong expected old value -> no write, returns false */
	zassert_false(atomic_cas(&v, 41, 99), "CAS with wrong old succeeded");
	zassert_equal(atomic_get(&v), 42, "CAS failure modified target");

	/* success path */
	zassert_true(atomic_cas(&v, 42, 99), "CAS with right old failed");
	zassert_equal(atomic_get(&v), 99, "CAS success did not write");
}

ZTEST(p3_atomics, test_swap_set)
{
	atomic_t v = ATOMIC_INIT(21);

	zassert_equal(atomic_swap(&v, 7), 21, "swap old value");
	zassert_equal(atomic_get(&v), 7, "swap new value");

	zassert_equal(atomic_set(&v, -5), 7, "set returns old");
	zassert_equal(atomic_get(&v), -5, "set new value");
}

ZTEST(p3_atomics, test_add_sub)
{
	atomic_t v = ATOMIC_INIT(100);

	zassert_equal(atomic_add(&v, 23), 100, "add returns old");
	zassert_equal(atomic_get(&v), 123, "add result");

	zassert_equal(atomic_sub(&v, 24), 123, "sub returns old");
	zassert_equal(atomic_get(&v), 99, "sub result");

	zassert_equal(atomic_inc(&v), 99, "inc returns old");
	zassert_equal(atomic_dec(&v), 100, "dec returns old");
	zassert_equal(atomic_get(&v), 99, "inc/dec net result");

	/* 64-bit width: wrap a value that would truncate in 32 bits */
	atomic_set(&v, 0x100000000LL);
	atomic_add(&v, 1);
	zassert_equal(atomic_get(&v), 0x100000001LL, "64-bit add");
}

ZTEST(p3_atomics, test_bitwise)
{
	atomic_t v = ATOMIC_INIT(0xF0F0);

	zassert_equal(atomic_or(&v, 0x0F0F), 0xF0F0, "or returns old");
	zassert_equal(atomic_get(&v), 0xFFFF, "or result");

	zassert_equal(atomic_and(&v, 0xFF00), 0xFFFF, "and returns old");
	zassert_equal(atomic_get(&v), 0xFF00, "and result");

	zassert_equal(atomic_xor(&v, 0xFFFF), 0xFF00, "xor returns old");
	zassert_equal(atomic_get(&v), 0x00FF, "xor result");
}

ZTEST(p3_atomics, test_min_max_signed)
{
	atomic_t v = ATOMIC_INIT(5);

	zassert_equal(atomic_max(&v, -8), 5, "max returns old");
	zassert_equal(atomic_get(&v), 5, "max keeps larger (signed)");

	zassert_equal(atomic_min(&v, -8), 5, "min returns old");
	zassert_equal(atomic_get(&v), -8, "min takes smaller (signed)");
}

ZTEST(p3_atomics, test_min_max_unsigned)
{
	unsigned long u = 5;

	/* high-bit value: huge unsigned, negative if treated signed */
	zassert_equal(atomic_maxu(&u, 0xFFFFFFFF00000000UL), 5, "maxu returns old");
	zassert_equal(u, 0xFFFFFFFF00000000UL, "maxu unsigned compare");

	u = 5;
	zassert_equal(atomic_minu(&u, 0xFFFFFFFF00000000UL), 5, "minu returns old");
	zassert_equal(u, 5, "minu unsigned compare");
}

/* P3.1: raw lr.d/sc.d, plain and .aq/.rl. irq_lock keeps the reservation from
 * being dropped by a timer IRQ between the LR and the SC. */
ZTEST(p3_atomics, test_p3_1_lr_sc)
{
	static volatile uint64_t atom = 0x1122334455667788UL;
	unsigned int key = irq_lock();

	uint64_t old = lr_d(&atom);
	uint64_t sc = sc_d(&atom, 0xAABBCCDDEEFF0011UL);

	irq_unlock(key);
	zassert_equal(old, 0x1122334455667788UL, "lr.d old value");
	zassert_equal(sc, 0, "sc.d should succeed");
	zassert_equal(atom, 0xAABBCCDDEEFF0011UL, "sc.d wrote new value");

	key = irq_lock();
	old = lr_d_aq(&atom);
	sc = sc_d_rl(&atom, 0x0102030405060708UL);
	irq_unlock(key);
	zassert_equal(old, 0xAABBCCDDEEFF0011UL, "lr.d.aq old value");
	zassert_equal(sc, 0, "sc.d.rl should succeed");
	zassert_equal(atom, 0x0102030405060708UL, "sc.d.rl wrote new value");
}

/* P3.3: 32-bit AMOs (no Zephyr 32-bit atomic API on rv64). Values mirror the
 * bare-metal suite's P3.3 checks. */
ZTEST(p3_atomics, test_p3_3_amo_w)
{
	uint32_t a = 0x10;

	zassert_equal(amo_swap_w(&a, 0x77), 0x10, "amoswap.w old");
	zassert_equal(a, 0x77, "amoswap.w new");
	zassert_equal(amo_add_w(&a, 0x09), 0x77, "amoadd.w old");
	zassert_equal(a, 0x80, "amoadd.w new");
	zassert_equal(amo_and_w(&a, 0x0F), 0x80, "amoand.w old");
	zassert_equal(a, 0x00, "amoand.w new");
	zassert_equal(amo_or_w(&a, 0x33), 0x00, "amoor.w old");
	zassert_equal(a, 0x33, "amoor.w new");
	zassert_equal(amo_xor_w(&a, 0x55), 0x33, "amoxor.w old");
	zassert_equal(a, 0x66, "amoxor.w new");

	a = 5;
	zassert_equal(amo_min_w(&a, (uint32_t)-2), 5, "amomin.w old");
	zassert_equal((int32_t)a, -2, "amomin.w new (signed)");
	zassert_equal((int32_t)amo_max_w(&a, 7), -2, "amomax.w old");
	zassert_equal((int32_t)a, 7, "amomax.w new (signed)");

	a = 5;
	zassert_equal(amo_maxu_w(&a, (uint32_t)-3), 5, "amomaxu.w old");
	zassert_equal(a, (uint32_t)-3, "amomaxu.w new (unsigned)");
	a = (uint32_t)-3;
	zassert_equal(amo_minu_w(&a, 5), (uint32_t)-3, "amominu.w old");
	zassert_equal(a, 5, "amominu.w new (unsigned)");
}

ZTEST_SUITE(p3_atomics, NULL, NULL, NULL, NULL, NULL);
