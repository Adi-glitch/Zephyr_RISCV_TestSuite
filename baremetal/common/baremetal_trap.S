/*
 * baremetal_trap.S — the suite's M-mode and S-mode trap vectors.
 *
 * Extracted VERBATIM from the suite's common/boot.S (the _start / M-mode boot
 * portion is intentionally omitted — Zephyr's reset.S does the boot and the
 * M->S transition). _m_trap_vector is installed as mtvec by reset.S (under
 * CONFIG_BAREMETAL_SUITE); _s_trap_vector is installed as stvec by the suite's
 * s_mode_entry(). They call m_trap_handler()/s_trap_handler() (C).
 */

/* M-mode trap vector: save regs, call m_trap_handler, restore, mret */
	.section .text
	.globl  _m_trap_vector
	.align  4
_m_trap_vector:
	addi    sp, sp, -128
	sd      ra,   0(sp)
	sd      t0,   8(sp)
	sd      t1,  16(sp)
	sd      t2,  24(sp)
	sd      a0,  32(sp)
	sd      a1,  40(sp)
	sd      a2,  48(sp)
	sd      a3,  56(sp)
	sd      a4,  64(sp)
	sd      a5,  72(sp)
	sd      a6,  80(sp)
	sd      a7,  88(sp)
	sd      t3,  96(sp)
	sd      t4, 104(sp)
	sd      t5, 112(sp)
	sd      t6, 120(sp)

	csrr    a0, mcause
	csrr    a1, mepc
	csrr    a2, mtval
	mv      a3, sp
	call    m_trap_handler
	csrw    mepc, a0

	ld      ra,   0(sp)
	ld      t0,   8(sp)
	ld      t1,  16(sp)
	ld      t2,  24(sp)
	ld      a0,  32(sp)
	ld      a1,  40(sp)
	ld      a2,  48(sp)
	ld      a3,  56(sp)
	ld      a4,  64(sp)
	ld      a5,  72(sp)
	ld      a6,  80(sp)
	ld      a7,  88(sp)
	ld      t3,  96(sp)
	ld      t4, 104(sp)
	ld      t5, 112(sp)
	ld      t6, 120(sp)
	addi    sp, sp, 128
	mret

/* S-mode trap vector: save all regs + sepc, call s_trap_handler, restore, sret */
	.globl  _s_trap_vector
	.align  4
_s_trap_vector:
	addi    sp, sp, -256
	sd      ra,   0(sp)
	sd      gp,   8(sp)
	sd      tp,  16(sp)
	sd      t0,  24(sp)
	sd      t1,  32(sp)
	sd      t2,  40(sp)
	sd      s0,  48(sp)
	sd      s1,  56(sp)
	sd      a0,  64(sp)
	sd      a1,  72(sp)
	sd      a2,  80(sp)
	sd      a3,  88(sp)
	sd      a4,  96(sp)
	sd      a5, 104(sp)
	sd      a6, 112(sp)
	sd      a7, 120(sp)
	sd      s2, 128(sp)
	sd      s3, 136(sp)
	sd      s4, 144(sp)
	sd      s5, 152(sp)
	sd      s6, 160(sp)
	sd      s7, 168(sp)
	sd      s8, 176(sp)
	sd      s9, 184(sp)
	sd      s10,192(sp)
	sd      s11,200(sp)
	sd      t3, 208(sp)
	sd      t4, 216(sp)
	sd      t5, 224(sp)
	sd      t6, 232(sp)

	csrr    t0, sepc
	sd      t0, 240(sp)        /* frame[30] = sepc (handler can modify) */

	csrr    a0, scause
	csrr    a1, stval
	csrr    a2, sepc
	mv      a3, sp
	call    s_trap_handler

	ld      t0, 240(sp)
	csrw    sepc, t0

	ld      ra,   0(sp)
	ld      gp,   8(sp)
	ld      tp,  16(sp)
	ld      t0,  24(sp)
	ld      t1,  32(sp)
	ld      t2,  40(sp)
	ld      s0,  48(sp)
	ld      s1,  56(sp)
	ld      a0,  64(sp)
	ld      a1,  72(sp)
	ld      a2,  80(sp)
	ld      a3,  88(sp)
	ld      a4,  96(sp)
	ld      a5, 104(sp)
	ld      a6, 112(sp)
	ld      a7, 120(sp)
	ld      s2, 128(sp)
	ld      s3, 136(sp)
	ld      s4, 144(sp)
	ld      s5, 152(sp)
	ld      s6, 160(sp)
	ld      s7, 168(sp)
	ld      s8, 176(sp)
	ld      s9, 184(sp)
	ld      s10,192(sp)
	ld      s11,200(sp)
	ld      t3, 208(sp)
	ld      t4, 216(sp)
	ld      t5, 224(sp)
	ld      t6, 232(sp)
	addi    sp, sp, 256
	sret
