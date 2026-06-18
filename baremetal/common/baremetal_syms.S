/*
 * baremetal_syms.S — linker symbols the suite expects from its own link.ld.
 *
 * In the bare-metal suite these come from common/link.ld. Here we build under
 * Zephyr's linker, so we provide them as reserved .bss space instead. Sizes
 * are within the original 2 MB layout; QEMU virt has 256 MB at 0x8000_0000, so
 * this all lives in the first 2 MB and the rest of RAM is simply unused.
 */

	.section .bss

	/* Page-table pool (smode.c: pt_pool = &_page_table_start). 32 KB, 4 KB aligned. */
	.balign 4096
	.globl  _page_table_start
_page_table_start:
	.skip   32768
	.globl  _page_table_end
_page_table_end:

	/* Heap used by the P0.4 SRAM sweep (p0_sanity.c: _heap_start.._heap_end). */
	.balign 16
	.globl  _heap_start
_heap_start:
	.skip   65536
	.globl  _heap_end
_heap_end:

	/* M-mode panic scratch (m_trap.c: _m_trap_info[0..2]). */
	.balign 8
	.globl  _m_trap_info
_m_trap_info:
	.skip   32

	/* Stack for the secondary hart (P4). reset.S sets sp = _bm_secondary_stack_top
	 * before dropping hart 1 into core1_smode_park_loop(). 16 KB, like the suite's
	 * _stack1. Only used when the board runs with -smp >1. */
	.balign 16
_bm_secondary_stack:
	.skip   16384
	.globl  _bm_secondary_stack_top
_bm_secondary_stack_top:
