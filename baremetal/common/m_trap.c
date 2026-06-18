/* m_trap.c — M-mode trap handler: SBI dispatch, timer/IPI forwarding
 *
 * Copy of the suite's common/m_trap.c. The ONLY change vs the original is the
 * panic path: instead of writing the HTIF `tohost` word, it writes the QEMU
 * virt `sifive_test` finisher so the emulator exits with a failure code.
 * Dispatch/forwarding logic (including the unknown-ecall reflection to S-mode
 * that Phase 0's P0.3 relies on) is untouched.
 */

#include "platform.h"
#include <stdint.h>

extern volatile struct trap_state g_trap;

static inline uint64_t read_mcause(void)  { uint64_t v; asm volatile("csrr %0, mcause"  : "=r"(v)); return v; }
static inline uint64_t read_mepc(void)    { uint64_t v; asm volatile("csrr %0, mepc"    : "=r"(v)); return v; }
static inline uint64_t read_mhartid(void) { uint64_t v; asm volatile("csrr %0, mhartid" : "=r"(v)); return v; }

static inline void set_csr_bits_mip(uint64_t bits)   { asm volatile("csrs mip, %0"   :: "r"(bits)); }
static inline void clear_csr_bits_mip(uint64_t bits)  { asm volatile("csrc mip, %0"   :: "r"(bits)); }
static inline void set_csr_bits_mie(uint64_t bits)    { asm volatile("csrs mie, %0"   :: "r"(bits)); }
static inline void clear_csr_bits_mie(uint64_t bits)  { asm volatile("csrc mie, %0"   :: "r"(bits)); }

static inline void write_csr_scause(uint64_t v)  { asm volatile("csrw scause, %0" :: "r"(v)); }
static inline void write_csr_stval(uint64_t v)   { asm volatile("csrw stval, %0"  :: "r"(v)); }
static inline void write_csr_sepc(uint64_t v)    { asm volatile("csrw sepc, %0"   :: "r"(v)); }

uint64_t m_trap_handler(uint64_t mcause, uint64_t mepc, uint64_t mtval,
                        uint64_t *frame)
{
    int is_interrupt = (mcause >> 63) & 1;
    uint64_t cause_code = mcause & 0x7FFFFFFFFFFFFFFFUL;

    if (is_interrupt) {
        switch (cause_code) {

        case IRQ_M_TIMER:
            /* Disable MTI, set mtimecmp=max, inject STIP */
            clear_csr_bits_mie(MIP_MTIP);
            {
                uint64_t hart = read_mhartid();
                volatile uint64_t *mtimecmp =
                    (volatile uint64_t *)CLINT_MTIMECMP(hart);
                *mtimecmp = 0xFFFFFFFFFFFFFFFFUL;
            }
            set_csr_bits_mip(MIP_STIP);
            return mepc;

        case IRQ_M_SOFT:
            /* Clear MSIP, inject SSIP */
            {
                uint64_t hart = read_mhartid();
                volatile uint32_t *msip =
                    (volatile uint32_t *)CLINT_MSIP(hart);
                *msip = 0;
            }
            set_csr_bits_mip(MIP_SSIP);
            return mepc;

        default:
            goto panic;
        }
    }

    /* Exceptions */
    switch (cause_code) {

    case CAUSE_ECALL_SMODE:
    {
        /* frame[11]=a7 (SBI func), frame[4]=a0 (arg) */
        uint64_t sbi_func = frame[11];
        uint64_t sbi_a0   = frame[4];

        switch (sbi_func) {

        case SBI_SET_TIMER:
            {
                uint64_t hart = read_mhartid();
                volatile uint64_t *mtimecmp =
                    (volatile uint64_t *)CLINT_MTIMECMP(hart);
                *mtimecmp = sbi_a0;
            }
            clear_csr_bits_mip(MIP_STIP);
            set_csr_bits_mie(MIP_MTIP);
            return mepc + 4;

        case SBI_SEND_IPI:
            {
                uint64_t target = sbi_a0;
                if (target < NUM_HARTS) {
                    volatile uint32_t *msip =
                        (volatile uint32_t *)CLINT_MSIP(target);
                    *msip = 1;
                }
            }
            return mepc + 4;

        case SBI_CLEAR_IPI:
            clear_csr_bits_mip(MIP_SSIP);
            return mepc + 4;

        default:
            /* Non-SBI ecall: reflect to S-mode trap handler */
            write_csr_scause(CAUSE_ECALL_SMODE);
            write_csr_sepc(mepc);
            write_csr_stval(0);
            {
                uint64_t stvec;
                asm volatile("csrr %0, stvec" : "=r"(stvec));
                stvec &= ~3UL;
                uint64_t mstatus;
                asm volatile("csrr %0, mstatus" : "=r"(mstatus));
                mstatus |= (1UL << 8);  /* SPP = S-mode */
                asm volatile("csrw mstatus, %0" :: "r"(mstatus));
                return stvec;
            }
        }
    }

    default:
        break;
    }

panic:
    {
        extern uint64_t _m_trap_info[];
        _m_trap_info[0] = mcause;
        _m_trap_info[1] = mepc;
        _m_trap_info[2] = mtval;

        /* QEMU virt "sifive_test" finisher: FAIL with code 500+cause. */
        uint32_t code = 500U + (uint32_t)cause_code;
        *(volatile uint32_t *)0x00100000UL = 0x3333U | (code << 16);
        while (1)
            asm volatile("wfi");
    }
    return mepc;
}
