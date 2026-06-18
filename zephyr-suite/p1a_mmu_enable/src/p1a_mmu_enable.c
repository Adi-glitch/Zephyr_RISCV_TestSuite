/*
 * Phase 1A (MMU enable & identity map) — idiomatic Zephyr suite.
 * Mirrors rv64gcv-baremetal-testsuite tests/p1a_mmu_enable.c, test for test:
 *
 *   P1A.1 Enable Sv39 + identity map + UART via I/O mapping
 *         suite: pt_setup_identity_kernel() + mmu_enable_sv39()
 *         here:  the kernel (CONFIG_RISCV_MMU) did both at boot — assert satp
 *                says Sv39, store/load through the identity mapping, printk
 *                (console MMIO goes through the low-1GB gigapage)
 *   P1A.2 4 KB page mapping load/store (3-level walk)
 *         chosen VA 0xC000_0000 -> chosen PA (static page), write via VA and
 *         READ BACK VIA THE PA ALIAS + the reverse — the alias is the proof
 *         translation reached the intended physical page
 *   P1A.3 Instruction fetch via mapped page (ret at PA, call via VA)
 *
 * Mapping API: arch_mem_map/arch_mem_unmap (the kernel functions
 * CONFIG_RISCV_MMU adds) — the suite's pt_map_page equivalent at the portable
 * level. VAs sit in Sv39 root[3], colliding with nothing (kernel image and VM
 * region live under 0x8000_0000+8MB; MMIO gigapage is root[0]).
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/mm.h>
#include <zephyr/sys/printk.h>
#include <zephyr/arch/riscv/csr.h>
#include <kernel_arch_interface.h>
#include <stdint.h>

#define PAGE_SIZE_4K 4096
#define TEST_RW_VA   0xC0000000UL /* suite P1A.2 VA */
#define TEST_EXEC_VA 0xC0010000UL /* suite P1A.3 VA */

/* The chosen PA: a page we own in .bss; identity boot mapping makes its
 * address its physical address (the suite's SRAM_BASE + 0x180000 page). */
static uint64_t backing_page[512] __aligned(PAGE_SIZE_4K);

/* No test here expects a fault; report any as a test failure. */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);
	printk("UNEXPECTED fault (reason %u scause=%lu), aborting\n",
	       reason, csr_read(scause));
	ztest_test_fail();
}

/* P1A.1: Sv39 is on (kernel enabled it at boot), identity store/load works,
 * and the console itself proves the MMIO mapping (every line you read went
 * out through the low-1GB gigapage). k_msleep adds "scheduler still runs". */
ZTEST(p1a_mmu_enable, test_p1a_1_sv39_identity)
{
	unsigned long satp = csr_read(satp);
	unsigned long mode = satp >> 60;
	unsigned long root_ppn = satp & ((1UL << 44) - 1);

	zassert_equal(mode, SATP_MODE_SV39, "satp mode %lu != Sv39", mode);
	zassert_not_equal(root_ppn, 0, "satp root PPN is zero");
	printk("satp = 0x%lx (Sv39, root PT at PA 0x%lx)\n",
	       satp, root_ppn << 12);

	/* store/load through the identity map (suite: SRAM_BASE + 0x100000) */
	backing_page[0] = 0xCAFEBABE12345678UL;
	zassert_equal(backing_page[0], 0xCAFEBABE12345678UL,
		      "store/load through identity map failed");
	printk("Store/load through identity map: OK\n");
	printk("UART via MMIO gigapage: OK (you are reading this)\n");

	k_msleep(10);
}

/* P1A.2: 4 KB page mapping, 3-level walk, with the PA-alias proof. */
ZTEST(p1a_mmu_enable, test_p1a_2_page_map_alias)
{
	volatile uint64_t *va = (volatile uint64_t *)TEST_RW_VA;

	arch_mem_map((void *)TEST_RW_VA, (uintptr_t)backing_page,
		     PAGE_SIZE_4K, K_MEM_PERM_RW);

	/* VA -> PA direction (suite's 0xA5A5... pattern), several words */
	for (int i = 0; i < 8; i++) {
		va[i * 64] = 0xA5A5A5A5A5A5A500UL | i;
	}
	for (int i = 0; i < 8; i++) {
		zassert_equal(backing_page[i * 64], 0xA5A5A5A5A5A5A500UL | i,
			      "PA alias readback mismatch at word %d", i * 64);
	}

	/* PA -> VA direction */
	backing_page[7] = 0x5A5A5A5A5A5A5A5AUL;
	zassert_equal(va[7], 0x5A5A5A5A5A5A5A5AUL, "VA readback mismatch");

	printk("P1A.2: VA 0x%lx <-> PA 0x%lx alias verified\n",
	       (unsigned long)TEST_RW_VA, (unsigned long)backing_page);

	arch_mem_unmap((void *)TEST_RW_VA, PAGE_SIZE_4K);
}

/* P1A.3: instruction fetch through a created mapping (ret at PA, call via
 * VA). The fetch faults before decoding if translation or X is wrong, so
 * returning from the call is the proof. fence.i is raw asm per the porting
 * doctrine — the instruction is the feature. */
ZTEST(p1a_mmu_enable, test_p1a_3_ifetch_via_map)
{
	volatile uint32_t *code = (volatile uint32_t *)backing_page;

	code[0] = 0x00008067; /* ret */
	__asm__ volatile("fence.i" ::: "memory");

	/* K_MEM_PERM_EXEC and no RW -> R|X leaf, the suite's PTE_R|PTE_X */
	arch_mem_map((void *)TEST_EXEC_VA, (uintptr_t)backing_page,
		     PAGE_SIZE_4K, K_MEM_PERM_EXEC);

	void (*func)(void) = (void (*)(void))TEST_EXEC_VA;

	func();

	printk("P1A.3: ifetch via VA 0x%lx (ret at PA 0x%lx) returned\n",
	       (unsigned long)TEST_EXEC_VA, (unsigned long)backing_page);

	arch_mem_unmap((void *)TEST_EXEC_VA, PAGE_SIZE_4K);
}

ZTEST_SUITE(p1a_mmu_enable, NULL, NULL, NULL, NULL, NULL);
