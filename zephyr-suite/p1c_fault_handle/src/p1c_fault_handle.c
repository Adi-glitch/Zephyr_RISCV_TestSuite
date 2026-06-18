/*
 * Phase 1C (fault handling & retry) — idiomatic Zephyr suite.
 * Mirrors rv64gcv-baremetal-testsuite tests/p1c_fault_handle.c, test for test.
 *
 *   P1C.1 load fault  -> resolver sets V, the faulting load RETRIES, value loads
 *   P1C.2 store fault -> resolver adds W, the store retries
 *   P1C.3 exec fault  -> resolver adds X, the fetch retries
 *   P1C.4 eight sequential fix+retry faults on distinct pages
 *   P1C.5 A/D-bit mode detection (SVADU vs SVADE)
 *
 * Fix-and-retry mechanism (C.1-4): the kernel gained a gated resumable
 * page-fault hook (z_riscv_mmu_set_fault_fixup / z_riscv_fault dispatch) — the
 * same control-flow slot arm64 (z_arm64_do_demand_paging) and x86
 * (z_x86_page_fault_handler) already have. The KERNEL owns the mechanism
 * (call resolver; if it returns true, return without advancing mepc so the
 * instruction re-executes); this TEST owns the policy via the resolver
 * p1c_fixup() below, which repairs the leaf PTE with z_riscv_mmu_pte_lookup.
 *
 * This is NOT Zephyr demand paging: C.2/C.3 are permission-upgrade faults
 * (store-on-RO=15, exec-on-noX=12), which no arch treats as paging events
 * (cf. x86 fatal.c "must not call k_mem_page_fault — the page is present").
 * That is exactly why the resolver is a general PTE fix-up, not k_mem_page_fault.
 *
 * The hook is default-inert: p1c_before() clears the resolver before every
 * test, so only C.1-4 (which register it) are resumable; C.5 stays callback-
 * free (SVADU => no fault on QEMU; SVADE => fault handled by the fatal hook).
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/arch/riscv/csr.h>
#include <kernel_arch_func.h>
#include <stdint.h>

#define PAGE_SIZE_4K 4096
#define EXC_INST_PAGE_FAULT  12
#define EXC_LOAD_PAGE_FAULT  13
#define EXC_STORE_PAGE_FAULT 15

/* suite VAs */
#define VA_C1      0xD0100000UL
#define VA_C2      0xD0110000UL
#define VA_C3      0xD0120000UL
#define VA_C4_BASE 0xD0200000UL
#define VA_C5      0xD0300000UL
#define C4_N       8

/* backing pages (.bss, identity => address == PA) */
static uint64_t bp_c1[512] __aligned(PAGE_SIZE_4K);
static uint64_t bp_c2[512] __aligned(PAGE_SIZE_4K);
static uint64_t bp_c3[512] __aligned(PAGE_SIZE_4K);
static uint64_t bp_c4[C4_N][512] __aligned(PAGE_SIZE_4K);
static uint64_t bp_c5[512] __aligned(PAGE_SIZE_4K);

static volatile int fix_count;

/* The fix-and-retry POLICY (suite's handler_action=0). Repairs the leaf PTE
 * for the faulting address according to the cause, then returns true so the
 * kernel retries the instruction. */
static bool p1c_fixup(unsigned long scause, unsigned long stval)
{
	uint64_t *pte = z_riscv_mmu_pte_lookup(stval);

	if (pte == NULL) {
		return false;
	}

	switch (scause) {
	case EXC_LOAD_PAGE_FAULT:
		*pte |= PTE_V | PTE_R | PTE_A;
		break;
	case EXC_STORE_PAGE_FAULT:
		*pte |= PTE_V | PTE_R | PTE_W | PTE_A | PTE_D;
		break;
	case EXC_INST_PAGE_FAULT:
		*pte |= PTE_V | PTE_R | PTE_X | PTE_A;
		break;
	default:
		return false;
	}

	__asm__ volatile("sfence.vma" ::: "memory");
	fix_count++;
	return true;
}

/* C.5 dual-outcome + unexpected-fault catcher. C.1-4 never reach here (their
 * faults are resolved-and-retried by p1c_fixup). */
static volatile bool expect_fault;
static volatile unsigned long expected_scause;
static volatile unsigned long expected_stval;

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	unsigned long scause = csr_read(scause);
	unsigned long stval = csr_read(stval);

	ARG_UNUSED(esf);

	if (!expect_fault) {
		printk("UNEXPECTED fault (reason %u scause=%lu), aborting\n",
		       reason, scause);
		ztest_test_fail();
	}
	expect_fault = false;

	if (scause != expected_scause || stval != expected_stval) {
		printk("MISMATCH: got scause=%lu stval=0x%lx want %lu/0x%lx\n",
		       scause, stval, expected_scause, expected_stval);
		ztest_test_fail();
	}
	printk("A/D mode: SVADE (software must manage A/D) - fault %lu OK\n",
	       scause);
	ztest_test_pass();
}

/* P1C.1 — load fault: resolver sets V=1, load retries and gets the value. */
ZTEST(p1c_fault_handle, test_p1c_1_load_fix_retry)
{
	bp_c1[0] = 0xFEEDFACECAFE0001UL; /* via identity PA */

	z_riscv_mmu_map_raw(VA_C1, (uintptr_t)bp_c1, PTE_R | PTE_W, 0); /* no V */
	z_riscv_mmu_set_fault_fixup(p1c_fixup);

	uint64_t val = *(volatile uint64_t *)VA_C1; /* fault 13 -> fix -> retry */

	zassert_equal(fix_count, 1, "expected exactly one fault, got %d", fix_count);
	zassert_equal(val, 0xFEEDFACECAFE0001UL, "value after retry");
	printk("P1C.1: load fix+retry OK (fix_count=%d val=0x%llx)\n",
	       fix_count, (unsigned long long)val);
}

/* P1C.2 — store fault: resolver adds W, store retries and lands. */
ZTEST(p1c_fault_handle, test_p1c_2_store_fix_retry)
{
	z_riscv_mmu_map_raw(VA_C2, (uintptr_t)bp_c2, PTE_V | PTE_R, 0); /* RO */
	z_riscv_mmu_set_fault_fixup(p1c_fixup);

	*(volatile uint64_t *)VA_C2 = 0xDEADBEEF00000002UL; /* fault 15 -> +W */

	zassert_equal(fix_count, 1, "expected one fault, got %d", fix_count);
	zassert_equal(bp_c2[0], 0xDEADBEEF00000002UL, "stored value via PA alias");
	printk("P1C.2: store fix+retry OK (fix_count=%d)\n", fix_count);
}

/* P1C.3 — exec fault: resolver adds X, fetch retries and the ret runs. */
ZTEST(p1c_fault_handle, test_p1c_3_exec_fix_retry)
{
	volatile uint32_t *insn = (volatile uint32_t *)bp_c3;

	insn[0] = 0x00008067; /* ret */
	__asm__ volatile("fence.i" ::: "memory");

	z_riscv_mmu_map_raw(VA_C3, (uintptr_t)bp_c3, PTE_V | PTE_R, 0); /* no X */
	z_riscv_mmu_set_fault_fixup(p1c_fixup);

	((void (*)(void))VA_C3)(); /* fault 12 -> +X -> retry -> returns */

	zassert_equal(fix_count, 1, "expected one fault, got %d", fix_count);
	printk("P1C.3: exec fix+retry OK (fix_count=%d)\n", fix_count);
}

/* P1C.4 — eight sequential fix+retry faults on distinct pages. */
ZTEST(p1c_fault_handle, test_p1c_4_sequential_faults)
{
	for (int i = 0; i < C4_N; i++) {
		bp_c4[i][0] = 0xA000000000000000UL | i;
		z_riscv_mmu_map_raw(VA_C4_BASE + (uintptr_t)i * PAGE_SIZE_4K,
				    (uintptr_t)bp_c4[i], PTE_R | PTE_W, 0); /* no V */
	}
	z_riscv_mmu_set_fault_fixup(p1c_fixup);

	for (int i = 0; i < C4_N; i++) {
		uint64_t val = *(volatile uint64_t *)
			(VA_C4_BASE + (uintptr_t)i * PAGE_SIZE_4K);

		zassert_equal(val, 0xA000000000000000UL | i,
			      "value mismatch at page %d", i);
	}

	zassert_equal(fix_count, C4_N, "expected %d faults, got %d", C4_N, fix_count);
	printk("P1C.4: %d sequential fix+retry faults OK\n", C4_N);
}

/* P1C.5 — A/D mode detection: leaf V|R|W with A=0,D=0; a load either succeeds
 * with hardware setting A (SVADU, QEMU) or faults 13 (SVADE). No resolver is
 * registered (p1c_before cleared it), so an SVADE fault reaches the fatal hook. */
ZTEST(p1c_fault_handle, test_p1c_5_ad_bit_mode)
{
	bp_c5[0] = 0x1234567890ABCDEFUL;

	z_riscv_mmu_map_raw(VA_C5, (uintptr_t)bp_c5, PTE_V | PTE_R | PTE_W, 0);

	expected_scause = EXC_LOAD_PAGE_FAULT;
	expected_stval = VA_C5;
	expect_fault = true;

	uint64_t val = *(volatile uint64_t *)VA_C5;

	/* still here => no fault => SVADU */
	expect_fault = false;
	zassert_equal(val, 0x1234567890ABCDEFUL, "SVADU load value mismatch");

	uint64_t *pte = z_riscv_mmu_pte_lookup(VA_C5);

	zassert_not_null(pte, "no leaf PTE");
	zassert_true((*pte & PTE_A) != 0, "SVADU but A bit not set by hardware");
	printk("A/D mode: SVADU (hardware sets A/D); PTE now 0x%016llx\n",
	       (unsigned long long)*pte);
}

/* Reset before every test: clear the resolver (so only C.1-4 are resumable),
 * the fault counter, and the C.5 expectation. */
static void p1c_before(void *fixture)
{
	ARG_UNUSED(fixture);
	z_riscv_mmu_set_fault_fixup(NULL);
	fix_count = 0;
	expect_fault = false;
	expected_scause = 0;
	expected_stval = 0;
}

ZTEST_SUITE(p1c_fault_handle, NULL, NULL, p1c_before, NULL, NULL);
