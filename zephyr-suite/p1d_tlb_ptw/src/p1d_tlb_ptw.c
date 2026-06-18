/*
 * Phase 1D (TLB & page-walker) — idiomatic Zephyr suite.
 * Mirrors rv64gcv-baremetal-testsuite tests/p1d_tlb_ptw.c, test for test:
 *
 *   P1D.1 targeted sfence.vma: re-point VA_A's leaf, flush ONLY VA_A;
 *         A reads the new page, B still reads its (unchanged) value
 *   P1D.2 stale TLB: re-point a leaf with NO sfence — the read may legally
 *         return old (stale TLB hit) or new (re-walk); after sfence -> new
 *   P1D.3 TLB thrash: 128 4 KB mappings (TLB=64 on the target), 2 passes
 *   P1D.4 2 MB megapage translation (level-1 leaf via z_riscv_mmu_map_raw)
 *   P1D.5 gigapage: a root-level leaf exists and one PTE covers 1 GB —
 *         our kernel's is the low-MMIO gigapage (root[0]); the kernel maps
 *         the image with 4 KB pages, unlike the suite's SRAM gigapage
 *   P1D.6 PTW-through-D-cache is NOT implementable: it needs the
 *         fix-PTE-AND-RETRY trap flow (see p1c_fault_handle.c header). Its
 *         functional essence — PTE edits made through the D-cache are seen
 *         by the page walker after sfence.vma — is exactly what D.1 and D.2
 *         exercise.
 *
 * PTE edits use z_riscv_mmu_pte_lookup (live tables, identity-mapped);
 * targeted flushes use raw `sfence.vma rs1` per the porting doctrine.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/arch/riscv/csr.h>
#include <kernel_arch_interface.h>
#include <stdint.h>

#define PAGE_SIZE_4K 4096

/* suite VAs */
#define VA_D1_A 0xD1000000UL
#define VA_D1_B 0xD1010000UL
#define VA_D2   0xD1100000UL
#define VA_D3   0xD2000000UL /* thrash range base */
#define VA_D4   0xE0200000UL /* megapage VA */

#define THRASH_COUNT 128

/* Backing pages (.bss, identity => address == PA). pages[0..2] for D.1,
 * pages[3..4] for D.2. */
static uint64_t pages[5][512] __aligned(PAGE_SIZE_4K);
/* D.3: 128 backing pages = 512 KB inside the boot-mapped image. */
static uint8_t thrash_buf[THRASH_COUNT * PAGE_SIZE_4K] __aligned(PAGE_SIZE_4K);

/* No test here expects a fault. */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);
	printk("UNEXPECTED fault (reason %u scause=%lu stval=0x%lx)\n",
	       reason, csr_read(scause), csr_read(stval));
	ztest_test_fail();
}

static inline void sfence_vma_addr(uintptr_t va)
{
	__asm__ volatile("sfence.vma %0" :: "r"(va) : "memory");
}

static inline uint64_t leaf(uintptr_t pa, uint64_t perms)
{
	return (((uint64_t)pa >> 12) << PTE_PPN_SHIFT) | perms |
	       PTE_V | PTE_A | PTE_D;
}

/* P1D.1: targeted sfence.vma — flush VA_A only; VA_B keeps translating. */
ZTEST(p1d_tlb_ptw, test_p1d_1_targeted_sfence)
{
	pages[0][0] = 0xAAAAAAAA00000001UL; /* pa_a     */
	pages[1][0] = 0xBBBBBBBB00000002UL; /* pa_b     */
	pages[2][0] = 0xAAAAAAAA00000003UL; /* pa_a_new */

	z_riscv_mmu_map_raw(VA_D1_A, (uintptr_t)pages[0],
			    PTE_V | PTE_R | PTE_W | PTE_A | PTE_D, 0);
	z_riscv_mmu_map_raw(VA_D1_B, (uintptr_t)pages[1],
			    PTE_V | PTE_R | PTE_W | PTE_A | PTE_D, 0);

	zassert_equal(*(volatile uint64_t *)VA_D1_A, 0xAAAAAAAA00000001UL,
		      "initial VA_A");
	zassert_equal(*(volatile uint64_t *)VA_D1_B, 0xBBBBBBBB00000002UL,
		      "initial VA_B");

	/* re-point VA_A's leaf -> pa_a_new, flush ONLY VA_A (suite P1D.1) */
	uint64_t *pte_a = z_riscv_mmu_pte_lookup(VA_D1_A);

	zassert_not_null(pte_a, "no leaf for VA_A");
	*pte_a = leaf((uintptr_t)pages[2], PTE_R | PTE_W);
	__asm__ volatile("fence w, w" ::: "memory");
	sfence_vma_addr(VA_D1_A);

	zassert_equal(*(volatile uint64_t *)VA_D1_A, 0xAAAAAAAA00000003UL,
		      "VA_A after targeted flush");
	zassert_equal(*(volatile uint64_t *)VA_D1_B, 0xBBBBBBBB00000002UL,
		      "VA_B after targeted flush");

	printk("P1D.1: targeted sfence.vma OK\n");
}

/* P1D.2: stale TLB — PTE re-pointed with NO sfence; old or new are both
 * spec-legal; after the sfence the new value is mandatory. (This is the
 * QEMU-caches-translations behavior baremetal P1D.2 documented.) */
ZTEST(p1d_tlb_ptw, test_p1d_2_stale_tlb)
{
	pages[3][0] = 0x1111111111111111UL; /* pa_old */
	pages[4][0] = 0x2222222222222222UL; /* pa_new */

	z_riscv_mmu_map_raw(VA_D2, (uintptr_t)pages[3],
			    PTE_V | PTE_R | PTE_W | PTE_A | PTE_D, 0);
	zassert_equal(*(volatile uint64_t *)VA_D2, 0x1111111111111111UL,
		      "initial read");

	uint64_t *pte = z_riscv_mmu_pte_lookup(VA_D2);

	zassert_not_null(pte, "no leaf");
	*pte = leaf((uintptr_t)pages[4], PTE_R | PTE_W);
	__asm__ volatile("fence w, w" ::: "memory");
	/* deliberately NO sfence.vma */

	uint64_t val = *(volatile uint64_t *)VA_D2;
	bool got_old = (val == 0x1111111111111111UL);
	bool got_new = (val == 0x2222222222222222UL);

	printk("P1D.2: without sfence.vma read 0x%llx (%s)\n",
	       (unsigned long long)val,
	       got_old ? "old - stale TLB hit" :
	       got_new ? "new - TLB re-walked" : "UNEXPECTED");
	zassert_true(got_old || got_new, "value must be old or new");

	sfence_vma_addr(VA_D2);
	zassert_equal(*(volatile uint64_t *)VA_D2, 0x2222222222222222UL,
		      "after sfence must read new");
}

/* P1D.3: TLB thrash — 128 mappings (twice a 64-entry TLB), 2 passes. */
ZTEST(p1d_tlb_ptw, test_p1d_3_tlb_thrash)
{
	for (int i = 0; i < THRASH_COUNT; i++) {
		uintptr_t pa = (uintptr_t)&thrash_buf[i * PAGE_SIZE_4K];

		*(volatile uint64_t *)pa = 0xCC00000000000000UL | i;
		z_riscv_mmu_map_raw(VA_D3 + (uintptr_t)i * PAGE_SIZE_4K, pa,
				    PTE_V | PTE_R | PTE_W | PTE_A | PTE_D, 0);
	}

	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < THRASH_COUNT; i++) {
			uintptr_t va = VA_D3 + (uintptr_t)i * PAGE_SIZE_4K;
			uint64_t val = *(volatile uint64_t *)va;

			zassert_equal(val, 0xCC00000000000000UL | i,
				      "thrash mismatch page %d pass %d", i, pass);
		}
	}
	printk("P1D.3: %d pages, 2 passes: all correct\n", THRASH_COUNT);
}

/* P1D.4: 2 MB megapage — one level-1 leaf covers the whole region. Maps the
 * kernel-image RAM read-only (2 MB aligned both sides); compares against the
 * identity mapping inside the image, plus offset-0 nonzero (the reset/text
 * area, suite's check) and a far-offset read through the same single leaf. */
ZTEST(p1d_tlb_ptw, test_p1d_4_megapage_2mb)
{
	const uintptr_t image_base = 0x80000000UL;
	size_t span = (uintptr_t)z_mapped_end - image_base;
	const size_t offs[] = { 0x0, 0x1000, (span / 2) & ~7UL,
				(span - 0x1000) & ~7UL };

	z_riscv_mmu_map_raw(VA_D4, image_base,
			    PTE_V | PTE_R | PTE_A | PTE_D, 1);

	zassert_not_equal(*(volatile uint64_t *)VA_D4, 0,
			  "megapage offset 0 read zero (reset vector?)");

	for (int i = 0; i < ARRAY_SIZE(offs); i++) {
		zassert_equal(*(volatile uint64_t *)(VA_D4 + offs[i]),
			      *(volatile uint64_t *)(image_base + offs[i]),
			      "megapage alias mismatch at +0x%lx",
			      (unsigned long)offs[i]);
	}

	/* far end of the 2 MB: identity isn't mapped there, but the read must
	 * translate through the same single leaf without faulting */
	(void)*(volatile uint64_t *)(VA_D4 + 0x1FF000);

	uint64_t *pte = z_riscv_mmu_pte_lookup(VA_D4);

	zassert_not_null(pte, "no leaf");
	zassert_true((*pte & PTE_R) != 0 && (*pte & PTE_W) == 0,
		     "megapage leaf not read-only");
	zassert_equal(pte, z_riscv_mmu_pte_lookup(VA_D4 + 0x1FF000),
		      "not a single level-1 superpage leaf");
	printk("P1D.4: megapage leaf 0x%016llx covers 2 MB\n",
	       (unsigned long long)*pte);

	arch_mem_unmap((void *)VA_D4, PAGE_SIZE_4K); /* zeroes the superpage leaf */
}

/* P1D.5: gigapage — a root-level leaf where one PTE covers 1 GB. Our kernel's
 * boot map has one: the low-MMIO gigapage (root[0]); lookups for two
 * addresses ~256 MB apart inside it must return the SAME leaf PTE. (Design
 * difference vs the suite: it identity-maps SRAM as a gigapage; our kernel
 * maps the image with 4 KB pages and reserves the gigapage for MMIO.) */
ZTEST(p1d_tlb_ptw, test_p1d_5_gigapage_leaf)
{
	uint64_t *pte_uart = z_riscv_mmu_pte_lookup(0x10000000UL); /* UART */
	uint64_t *pte_clint = z_riscv_mmu_pte_lookup(0x02000000UL); /* CLINT */

	zassert_not_null(pte_uart, "no leaf for MMIO");
	zassert_true((*pte_uart & PTE_V) != 0 &&
		     (*pte_uart & (PTE_R | PTE_W | PTE_X)) != 0,
		     "MMIO root entry is not a leaf");
	zassert_equal(pte_uart, pte_clint,
		      "UART and CLINT not covered by one gigapage leaf");
	printk("P1D.5: root[0] = 0x%016llx (gigapage leaf)\n",
	       (unsigned long long)*pte_uart);
}

ZTEST_SUITE(p1d_tlb_ptw, NULL, NULL, NULL, NULL, NULL);
