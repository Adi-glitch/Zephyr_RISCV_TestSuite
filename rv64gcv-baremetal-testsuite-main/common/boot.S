/* boot.S — M-mode boot: stacks, PMP, delegation, S-mode drop, Hart 1 park loop */

#include "asm_defs.h"

    .section .text.entry, "ax"
    .globl _start
_start:
    csrr    t0, mhartid

    /* Per-hart stack */
    la      sp, _stack0_top
    bnez    t0, .Lhart1_stack
    j       .Lstack_done
.Lhart1_stack:
    la      sp, _stack1_top
.Lstack_done:

    /* Hart 0: clear BSS */
    bnez    t0, .Lskip_bss
    la      t1, _bss_start
    la      t2, _bss_end
.Lbss_loop:
    bge     t1, t2, .Lbss_done
    sd      zero, 0(t1)
    addi    t1, t1, 8
    j       .Lbss_loop
.Lbss_done:
.Lskip_bss:

    /* M-mode trap vector */
    la      t0, _m_trap_vector
    csrw    mtvec, t0

    /* PMP: full RWX access */
    li      t0, -1
    csrw    pmpaddr0, t0
    li      t0, (PMP_R | PMP_W | PMP_X | PMP_NAPOT)
    csrw    pmpcfg0, t0

    /* Delegate exceptions + interrupts to S-mode */
    li      t0, MEDELEG_DEFAULT
    csrw    medeleg, t0
    li      t0, MIDELEG_DEFAULT
    csrw    mideleg, t0

    /* Enable M-mode software + timer interrupts */
    li      t0, (MIE_MSIE | MIE_MTIE)
    csrs    mie, t0

    /* Set MPP=S-mode for mret */
    csrr    t0, mstatus
    li      t1, MSTATUS_MPP_MASK
    not     t1, t1
    and     t0, t0, t1
    li      t1, MSTATUS_MPP_S
    or      t0, t0, t1
    csrw    mstatus, t0

    /* Hart 0 → S-mode; Hart 1 → park */
    csrr    t0, mhartid
    bnez    t0, .Lhart1_smode

    la      t0, s_mode_entry
    csrw    mepc, t0
    mret

/* Hart 1: Drop directly to S-mode park loop */
.Lhart1_smode:
    la      t0, core1_smode_park_loop
    csrw    mepc, t0
    mret

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
