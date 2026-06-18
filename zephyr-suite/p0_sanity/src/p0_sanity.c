/*
 * Phase 0 (sanity) — idiomatic Zephyr suite.
 *
 * Mirrors the bare-metal suite's P0 features on a full Zephyr kernel:
 *   P0.1 UART TX        -> printk over the board's UART console driver
 *   P0.2 CSR read       -> csr_read(sstatus), S-mode visible state
 *   P0.3 ecall from S   -> raw ecall (a7=0xFF, the suite's stimulus) into the
 *                          resident M-mode SBI runtime; assert the SBI-spec
 *                          outcome (a0 == SBI_ERR_NOT_SUPPORTED, resumed)
 *   P0.4 SRAM r/w sweep -> 1024-word pattern write/verify
 *   (bonus) trap caught -> provoke an access fault, catch via fatal handler
 *
 * P0.3 firmware-policy note: the chip flow is the suite's exactly — ecall from
 * S-mode traps to M-mode (medeleg keeps cause 9 there), the firmware decodes
 * it and S-mode resumes at mepc+4. What differs is what the firmware does with
 * an UNKNOWN call: the suite's custom m_trap.c reflected it back to S-mode as
 * an exception (scause=9), while Zephyr's sbi.S — like OpenSBI on real silicon
 * — follows the SBI spec and returns SBI_ERR_NOT_SUPPORTED in a0. The positive
 * SBI path (a recognized ecall) is exercised constantly on this board: the
 * kernel timer driver issues SBI_EXT_TIME ecalls under every k_timer/k_msleep.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/arch/riscv/csr.h>
#include <zephyr/arch/riscv/sbi.h>
#include <stdint.h>

/* RISC-V exception cause (priv spec table); Zephyr's irq.h only defines the
 * ecall/breakpoint ones. */
#define EXC_LOAD_ACCESS 5

/* The suite's g_trap contract, idiomatically: tests declare the expected
 * scause/stval, the handler reads the live CSRs and compares. The handler runs
 * inside the S-mode exception path, so scause/stval still hold what hardware
 * wrote on trap entry. Reaching it at all already proves delegation: medeleg
 * sends all non-ecall exceptions to S-mode, and an exception arriving in
 * M-mode instead would spin forever in sbi.S m_mode_unhandled. */
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

	printk("Caught expected fault: reason=%u scause=%lu stval=0x%lx\n",
	       reason, scause, stval);
	if (scause != expected_scause || stval != expected_stval) {
		printk("MISMATCH: want scause=%lu stval=0x%lx\n",
		       expected_scause, expected_stval);
		ztest_test_fail();
	}
	ztest_test_pass();
}

/* P0.1: every ztest line on the console already exercises the UART TX path;
 * make it explicit anyway. */
ZTEST(p0_sanity, test_uart_tx)
{
	/* ASCII only: twister's QEMU console reader decodes byte-by-byte and
	 * chokes on multi-byte UTF-8. */
	printk("P0.1 - UART TX: OK (you are reading this)\n");
}

/* P0.2: read an S-mode CSR. UXL (sstatus[33:32]) must say U-mode is 64-bit. */
ZTEST(p0_sanity, test_csr_read_sstatus)
{
	unsigned long sstatus = csr_read(sstatus);
	unsigned long uxl = (sstatus >> 32) & 0x3;

	printk("sstatus = 0x%016lx\n", sstatus);
	zassert_equal(uxl, 2, "sstatus.UXL %lu != 2 (RV64)", uxl);
}

/* P0.3: ecall from S-mode reaches the M-mode firmware and we resume. a7=0xFF
 * is the suite's exact unknown-extension stimulus; per the SBI spec the
 * runtime returns SBI_ERR_NOT_SUPPORTED in a0 and resumes at mepc+4 — merely
 * reaching the asserts proves the resume. Routing is implicitly asserted too:
 * if medeleg wrongly delegated cause 9 to S-mode, Zephyr would treat the ecall
 * as an exception and fatal (expect_fault is false here, so that would fail
 * the test). */
ZTEST(p0_sanity, test_ecall_smode_sbi)
{
	register long a0 __asm__("a0") = 0;
	register long a1 __asm__("a1") = 0;
	register long a6 __asm__("a6") = 0;    /* SBI function ID (unused) */
	register long a7 __asm__("a7") = 0xFF; /* unknown SBI extension ID */

	__asm__ volatile("ecall"
			 : "+r"(a0), "+r"(a1)
			 : "r"(a6), "r"(a7)
			 : "memory");

	printk("ecall a7=0xFF returned a0=%ld\n", a0);
	zassert_equal(a0, SBI_ERR_NOT_SUPPORTED,
		      "unknown ecall returned %ld, want SBI_ERR_NOT_SUPPORTED (%d)",
		      a0, SBI_ERR_NOT_SUPPORTED);
	/* a1 is undefined on error per the SBI spec: not asserted. */
}

/* Bonus trap-class check (not a suite P0 item): a synchronous trap is caught
 * by the S-mode handler with the right cause. Load from an address with no
 * RAM/device behind it (QEMU virt: nothing at 0xC000_0000) -> load ACCESS
 * fault (scause=5, stval=the address) -> S-mode exception path -> our handler
 * compares scause/stval. Teaching contrast: this P0 build has no MMU, so an
 * unbacked address is an *access* fault; P1's same-shape test under Sv39 sees
 * a load *page* fault (13) instead — the same cause-class distinction behind
 * the suite's P1A mmu=on debugging story (store access fault 7 vs page fault). */
ZTEST(p0_sanity, test_trap_is_caught)
{
	volatile uint32_t *bogus = (volatile uint32_t *)0xC0000000UL;

	expected_scause = EXC_LOAD_ACCESS;
	expected_stval = (unsigned long)bogus;
	expect_fault = true;
	(void)*bogus;
	zassert_unreachable("load from unbacked address did not trap");
}

/* P0.4: SRAM read/write sweep, 1024 64-bit words (same count as the suite). */
ZTEST(p0_sanity, test_sram_rw_sweep)
{
	static uint64_t buf[1024];

	for (int i = 0; i < ARRAY_SIZE(buf); i++) {
		buf[i] = 0xA5A5A5A500000000UL | i;
	}
	for (int i = 0; i < ARRAY_SIZE(buf); i++) {
		zassert_equal(buf[i], 0xA5A5A5A500000000UL | i,
			      "pattern 1 mismatch at %d", i);
	}

	for (int i = 0; i < ARRAY_SIZE(buf); i++) {
		buf[i] = ~(0xA5A5A5A500000000UL | i);
	}
	for (int i = 0; i < ARRAY_SIZE(buf); i++) {
		zassert_equal(buf[i], ~(0xA5A5A5A500000000UL | i),
			      "pattern 2 mismatch at %d", i);
	}

	printk("Tested %d 64-bit words: OK\n", (int)ARRAY_SIZE(buf));
}

ZTEST_SUITE(p0_sanity, NULL, NULL, NULL, NULL, NULL);
