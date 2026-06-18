/* Assembly-only constants. C code should use platform.h. */
#ifndef ASM_DEFS_H
#define ASM_DEFS_H

#define CLINT_BASE 0x02000000
#define CLINT_MTIMECMP0 (CLINT_BASE + 0x4000)
#define CLINT_MTIME (CLINT_BASE + 0xBFF8)

#define MSTATUS_MPP_MASK (3 << 11)
#define MSTATUS_MPP_S (1 << 11)
#define MSTATUS_MPP_M (3 << 11)

/* medeleg: bits 0-8, 12, 13, 15 (ecall-S NOT delegated) */
#define MEDELEG_DEFAULT 0xB1FF
/* mideleg: S-mode software(1), timer(5), external(9) */
#define MIDELEG_DEFAULT 0x222

#define IRQ_M_SOFT 3
#define IRQ_M_TIMER 7
#define IRQ_S_TIMER 5

#define MIP_STIP (1 << 5)
#define MIP_MTIP (1 << 7)
#define MIE_MSIE (1 << 3)
#define MIE_MTIE (1 << 7)

#define SBI_SET_TIMER 0
#define SBI_SEND_IPI 1
#define SBI_CLEAR_IPI 2
#define SBI_FORWARD_ECALL 0xFF

#define PMP_R 0x01
#define PMP_W 0x02
#define PMP_X 0x04
#define PMP_NAPOT 0x18

#define CAUSE_ECALL_SMODE 9

#endif
