/*
 * Phase 1B (page fault generation) — idiomatic Zephyr suite.
 * Mirrors rv64gcv-baremetal-testsuite tests/p1b_fault_gen.c, test for test.
 * Suite PTE configurations are built with z_riscv_mmu_map_raw (the kernel's
 * pt_map_page equivalent, gated under CONFIG_RISCV_MMU) at the suite's own
 * VAs (0xD00x_0000 / 0xE000_0000):
 *
 *   P1B.1 load  from V=0 page (PPN+perms intact)        -> cause 13
 *   P1B.2 store to   V=0 page                           -> cause 15
 *   P1B.3 exec  from V=0 page                           -> cause 12
 *   P1B.4 load  from R=0 (execute-only) page            -> cause 13
 *   P1B.5 store to   W=0 (read-only) page               -> cause 15
 *   P1B.6 exec  from X=0 (read-only) page               -> cause 12
 *   P1B.7 S-mode load from U=1 page with sstatus.SUM=0  -> cause 13
 *   P1B.8 misaligned megapage (level-1 leaf, PPN[0]&1)  -> cause 13
 *
 * Each test pins the exact scause AND stval (the suite's verify_fault) via
 * the expected-fault handler contract below. Difference vs the suite: its
 * S-trap handler SKIPS the faulting instruction and the run continues
 * (handler_action=1) or fixes the PTE and retries (P1B.3/6,
 * handler_action=0); Zephyr's fatal path instead ends the faulted test
 * thread, so each fault is its own ZTEST and the retry halves live in
 * P1C-land (see p1c_fault_handle.c for why those stay in baremetal).
 *
 * API-flavor twins (kept per the keep-both decision, suffixed 'b'/'9'):
 * the same fault classes provoked through the portable APIs
 * (k_mem_map/k_mem_unmap/arch_mem_map + pte_lookup).
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/mm.h>
#include <zephyr/sys/printk.h>
#include <zephyr/arch/riscv/csr.h>
#include <kernel_arch_interface.h>
#include <stdint.h>

#define PAGE_SIZE_4K 4096

/* RISC-V exception causes (priv spec); Zephyr's irq.h has only ecall/break. */
#define EXC_INST_PAGE_FAULT  12
#define EXC_LOAD_PAGE_FAULT  13
#define EXC_STORE_PAGE_FAULT 15

/* Suite VAs, one per test — no cross-test interference even though faulted
 * test threads never reach their cleanup. */
#define VA_B1 0xD0000000UL
#define VA_B2 0xD0010000UL
#define VA_B3 0xD0030000UL
#define VA_B4 0xD0040000UL
#define VA_B5 0xD0050000UL
#define VA_B6 0xD0060000UL
#define VA_B7 0xD0070000UL
#define VA_B8 0xE0000000UL /* suite's misaligned-megapage VA */
#define VA_B1B 0xD0080000UL /* API-flavor: surgical V-clear on a live PTE */

/* Backing pages we own (.bss, identity => address == PA). */
static uint64_t bp_data[512] __aligned(PAGE_SIZE_4K);
static uint64_t bp_code[512] __aligned(PAGE_SIZE_4K);

/* The suite's g_trap contract: tests declare expected scause/stval; the
 * handler reads the live CSRs (we are inside the S-mode exception path) and
 * compares. Reaching the handler at all proves medeleg delegation. */
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

	printk("Caught expected fault: scause=%lu stval=0x%lx\n", scause, stval);
	if (scause != expected_scause || stval != expected_stval) {
		printk("MISMATCH: want scause=%lu stval=0x%lx\n",
		       expected_scause, expected_stval);
		ztest_test_fail();
	}
	ztest_test_pass();
}

static void expect(unsigned long scause, unsigned long stval)
{
	expected_scause = scause;
	expected_stval = stval;
	expect_fault = true;
}

/* P1B.1: load from a V=0 page — PPN and permissions present, ONLY V clear
 * (suite: pt_map_page(..., PTE_R|PTE_W, valid=0)). */
ZTEST(p1b_fault_gen, test_p1b_1_load_v0_page)
{
	z_riscv_mmu_map_raw(VA_B1, (uintptr_t)bp_data,
			    PTE_R | PTE_W | PTE_A | PTE_D, 0); /* no PTE_V */

	expect(EXC_LOAD_PAGE_FAULT, VA_B1);
	(void)*(volatile uint64_t *)VA_B1;
	zassert_unreachable("load from V=0 page did not fault");
}

/* P1B.1b (API flavor): same fault class, but on a LIVE mapping made by
 * arch_mem_map whose V bit is then surgically cleared via pte_lookup —
 * proves V alone gates an already-used translation. */
ZTEST(p1b_fault_gen, test_p1b_1b_vbit_clear_live)
{
	volatile uint64_t *va = (volatile uint64_t *)VA_B1B;

	arch_mem_map((void *)VA_B1B, (uintptr_t)bp_data,
		     PAGE_SIZE_4K, K_MEM_PERM_RW);
	*va = 0x1122334455667788UL;
	zassert_equal(bp_data[0], 0x1122334455667788UL, "mapping not live");

	uint64_t *pte = z_riscv_mmu_pte_lookup(VA_B1B);

	zassert_not_null(pte, "no leaf PTE");
	zassert_equal((*pte >> PTE_PPN_SHIFT) << 12, (uintptr_t)bp_data,
		      "leaf PPN is not bp_data");
	printk("leaf PTE before: 0x%016llx; clearing V only\n",
	       (unsigned long long)*pte);

	*pte &= ~(uint64_t)PTE_V;
	__asm__ volatile("sfence.vma" ::: "memory");

	expect(EXC_LOAD_PAGE_FAULT, VA_B1B);
	(void)*va;
	zassert_unreachable("load through V=0 live PTE did not fault");
}

/* P1B.2: store to a V=0 page -> store page fault. */
ZTEST(p1b_fault_gen, test_p1b_2_store_v0_page)
{
	z_riscv_mmu_map_raw(VA_B2, (uintptr_t)bp_data,
			    PTE_R | PTE_W | PTE_A | PTE_D, 0); /* no PTE_V */

	expect(EXC_STORE_PAGE_FAULT, VA_B2);
	*(volatile uint64_t *)VA_B2 = 0xBEEF;
	zassert_unreachable("store to V=0 page did not fault");
}

/* P1B.3: execute from a V=0 page -> instruction page fault. (The suite then
 * fix-PTE-retries so the planted ret executes; Zephyr's fatal path cannot
 * resume, so the fault-generation half lives here — see p1c notes.) */
ZTEST(p1b_fault_gen, test_p1b_3_exec_v0_page)
{
	volatile uint32_t *insn = (volatile uint32_t *)bp_code;

	insn[0] = 0x00008067; /* ret */
	__asm__ volatile("fence.i" ::: "memory");

	z_riscv_mmu_map_raw(VA_B3, (uintptr_t)bp_code,
			    PTE_R | PTE_X | PTE_A | PTE_D, 0); /* no PTE_V */

	expect(EXC_INST_PAGE_FAULT, VA_B3);
	((void (*)(void))VA_B3)();
	zassert_unreachable("exec from V=0 page did not fault");
}

/* P1B.4: load from an execute-only (R=0, X=1) page — a permission combo the
 * portable flags cannot express; map_raw can. */
ZTEST(p1b_fault_gen, test_p1b_4_load_xonly_page)
{
	z_riscv_mmu_map_raw(VA_B4, (uintptr_t)bp_data,
			    PTE_V | PTE_X | PTE_A | PTE_D, 0);

	expect(EXC_LOAD_PAGE_FAULT, VA_B4);
	(void)*(volatile uint64_t *)VA_B4;
	zassert_unreachable("load from execute-only page did not fault");
}

/* P1B.5: store to a read-only (W=0) page. */
ZTEST(p1b_fault_gen, test_p1b_5_store_ro_page)
{
	z_riscv_mmu_map_raw(VA_B5, (uintptr_t)bp_data,
			    PTE_V | PTE_R | PTE_A | PTE_D, 0);

	expect(EXC_STORE_PAGE_FAULT, VA_B5);
	*(volatile uint64_t *)VA_B5 = 0xBEEF;
	zassert_unreachable("store to read-only page did not fault");
}

/* P1B.5b (API flavor): read-only via k_mem_map (no K_MEM_PERM_RW).
 * K_MEM_MAP_UNINIT because k_mem_map zeroes through the new mapping, which
 * would itself fault on a RO page. VA chosen by the kernel. */
ZTEST(p1b_fault_gen, test_p1b_5b_store_ro_kmemmap)
{
	volatile uint64_t *p = k_mem_map(PAGE_SIZE_4K, K_MEM_MAP_UNINIT);

	zassert_not_null((void *)p, "k_mem_map failed");
	(void)*p; /* reads are fine */

	expect(EXC_STORE_PAGE_FAULT, (unsigned long)p);
	*p = 1;
	zassert_unreachable("store to k_mem_map RO page did not fault");
}

/* P1B.6: execute from a no-X (R-only) page. */
ZTEST(p1b_fault_gen, test_p1b_6_exec_noexec_page)
{
	volatile uint32_t *insn = (volatile uint32_t *)bp_code;

	insn[0] = 0x00008067; /* ret */
	__asm__ volatile("fence.i" ::: "memory");

	z_riscv_mmu_map_raw(VA_B6, (uintptr_t)bp_code,
			    PTE_V | PTE_R | PTE_A | PTE_D, 0); /* no X */

	expect(EXC_INST_PAGE_FAULT, VA_B6);
	((void (*)(void))VA_B6)();
	zassert_unreachable("exec from no-X page did not fault");
}

/* P1B.6b (API flavor): no-exec via k_mem_map(RW): the fetch faults before
 * the page contents even matter. */
ZTEST(p1b_fault_gen, test_p1b_6b_exec_noexec_kmemmap)
{
	void *buf = k_mem_map(PAGE_SIZE_4K, K_MEM_PERM_RW);

	zassert_not_null(buf, "k_mem_map failed");

	expect(EXC_INST_PAGE_FAULT, (unsigned long)buf);
	((void (*)(void))buf)();
	zassert_unreachable("exec from k_mem_map RW page did not fault");
}

/* P1B.7: S-mode access to a U=1 page with sstatus.SUM=0 — supervisor may not
 * touch user pages unless SUM is set. */
ZTEST(p1b_fault_gen, test_p1b_7_upage_sum0)
{
	z_riscv_mmu_map_raw(VA_B7, (uintptr_t)bp_data,
			    PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D, 0);

	csr_clear(sstatus, SSTATUS_SUM); /* suite: sstatus &= ~MSTATUS_SUM */

	expect(EXC_LOAD_PAGE_FAULT, VA_B7);
	(void)*(volatile uint64_t *)VA_B7;
	zassert_unreachable("S-mode load from U page (SUM=0) did not fault");
}

/* P1B.8: misaligned megapage — a level-1 (2 MB) leaf whose PPN[0] is not
 * zero is reserved => page fault. map_raw asserts alignment (it guards
 * legitimate users), so build an aligned leaf first and then corrupt the PPN
 * surgically via pte_lookup, exactly like the suite pokes l1[idx1]. */
ZTEST(p1b_fault_gen, test_p1b_8_misaligned_megapage)
{
	z_riscv_mmu_map_raw(VA_B8, 0x80000000UL,
			    PTE_V | PTE_R | PTE_W | PTE_A | PTE_D, 1);

	uint64_t *pte = z_riscv_mmu_pte_lookup(VA_B8);

	zassert_not_null(pte, "no megapage leaf");
	/* suite: bad_ppn = (SRAM_BASE >> PGSHIFT) | 0x1 */
	*pte = (((0x80000000UL >> 12) | 0x1) << PTE_PPN_SHIFT) |
	       PTE_V | PTE_R | PTE_W | PTE_A | PTE_D;
	__asm__ volatile("sfence.vma" ::: "memory");

	expect(EXC_LOAD_PAGE_FAULT, VA_B8);
	(void)*(volatile uint64_t *)VA_B8;
	zassert_unreachable("load via misaligned megapage did not fault");
}

/* P1B.9 (API flavor, no suite twin): load from a VA after k_mem_unmap —
 * tests the kernel's unmap semantics: frame freed, whole PTE zeroed, TLB
 * flushed (a stale translation would let the load succeed). */
ZTEST(p1b_fault_gen, test_p1b_9_unmapped_after_kmemunmap)
{
	volatile uint64_t *p = k_mem_map(PAGE_SIZE_4K, K_MEM_PERM_RW);

	zassert_not_null((void *)p, "k_mem_map failed");
	*p = 0xDEADBEEF;
	zassert_equal(*p, 0xDEADBEEF, "mapped write/read");

	k_mem_unmap((void *)p, PAGE_SIZE_4K);

	expect(EXC_LOAD_PAGE_FAULT, (unsigned long)p);
	(void)*p;
	zassert_unreachable("load from unmapped VA did not fault");
}

ZTEST_SUITE(p1b_fault_gen, NULL, NULL, NULL, NULL, NULL);
