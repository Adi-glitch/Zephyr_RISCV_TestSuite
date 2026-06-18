/*
 * baremetal_main.c — kernel-bypass glue for running the bare-metal suite.
 *
 * Zephyr's arch/riscv/core/reset.S (under CONFIG_BAREMETAL_SUITE) performs the
 * normal M-mode setup and M->S transition, then calls baremetal_main() instead
 * of z_prep_c(). We are already in S-mode here, on Zephyr's boot stack, with
 * the suite's _m_trap_vector installed as the M-mode trap vector.
 *
 * s_mode_entry() (suite, smode.c) sets stvec to the suite's S-mode vector and
 * runs test_main(); it never returns (ends in test_pass()/test_fail()).
 */

extern void s_mode_entry(void);

void baremetal_main(void)
{
	s_mode_entry();

	/* Unreachable: s_mode_entry() ends in a wfi loop after the verdict. */
	for (;;) {
		__asm__ volatile("wfi");
	}
}

/*
 * The Zephyr build links libkernel, which references main(). The kernel never
 * starts (reset.S diverted above), so this is never called — it exists only to
 * satisfy the linker.
 */
int main(void)
{
	return 0;
}
