/* test.h — Test API. Every test includes this and implements test_main(). */
#ifndef TEST_H
#define TEST_H

#include <stdint.h>
#include "platform.h"

/* HTIF / UART */
void test_pass(void);
void test_fail(int code);
void test_assert(int condition, const char *msg, int fail_code);
void uart_puts(const char *s);
void uart_putc(char c);
void uart_put_hex(uint64_t val);
void uart_put_dec(int val);

/* Page table utilities */
uint64_t *pt_alloc_page(void);
void      pt_reset(void);
uint64_t *pt_setup_identity_kernel(void);
void      pt_map_page(uint64_t *root, uint64_t va, uint64_t pa,
                      uint64_t perm, int valid);
uint64_t *pt_walk(uint64_t *root, uint64_t va, int *level);
void      mmu_enable_sv39(uint64_t *root);

/* CSR helpers */
void sfence_vma_all(void);
void sfence_vma_addr(uint64_t addr);
void fence_i(void);

/* Trap state — set by handler, read by test */
struct trap_state {
    uint64_t scause;
    uint64_t stval;
    uint64_t sepc;
    int      fault_count;
    int      expected_cause;
    uint64_t expected_stval;
    int      handler_action;  /* 0=fix PTE, 1=skip insn, 2=custom */
    volatile int      irq_count;
    volatile int      timer_irq_count;
    volatile int      soft_irq_count;
    volatile uint64_t last_irq_cause;
};
extern volatile struct trap_state g_trap;

/* Test entry point */
int test_main(void);

#define TEST_LOG(msg) do { uart_puts("  [TEST] "); uart_puts(msg); uart_putc('\n'); } while(0)

#define ASSERT_EQ(actual, expected, msg, code) do { \
    if ((uint64_t)(actual) != (uint64_t)(expected)) { \
        uart_puts("  ASSERT_EQ FAILED: "); uart_puts(msg); \
        uart_puts("\n    actual="); uart_put_hex((uint64_t)(actual)); \
        uart_puts(" expected="); uart_put_hex((uint64_t)(expected)); \
        uart_putc('\n'); \
        test_fail(code); \
    } \
} while(0)

#define BARRIER() asm volatile("" ::: "memory")

static inline uint64_t read_cycles(void)
{
    uint64_t val;
    asm volatile("rdcycle %0" : "=r"(val));
    return val;
}

/* --- SBI helpers (S-mode ecall to M-mode) --- */

static inline void sbi_set_timer(uint64_t val)
{
    register uint64_t a0 asm("a0") = val;
    register uint64_t a7 asm("a7") = SBI_SET_TIMER;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static inline void sbi_send_ipi(uint64_t target_hart)
{
    register uint64_t a0 asm("a0") = target_hart;
    register uint64_t a7 asm("a7") = SBI_SEND_IPI;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static inline void sbi_clear_ipi(void)
{
    register uint64_t a7 asm("a7") = SBI_CLEAR_IPI;
    asm volatile("ecall" : : "r"(a7) : "memory");
}

/* --- S-mode CSR helpers --- */

static inline uint64_t read_sstatus(void)
{
    uint64_t v; asm volatile("csrr %0, sstatus" : "=r"(v)); return v;
}

static inline void write_sstatus(uint64_t v)
{
    asm volatile("csrw sstatus, %0" :: "r"(v));
}

static inline uint64_t read_sie(void)
{
    uint64_t v; asm volatile("csrr %0, sie" : "=r"(v)); return v;
}

static inline void write_sie(uint64_t v)
{
    asm volatile("csrw sie, %0" :: "r"(v));
}

static inline uint64_t read_sip(void)
{
    uint64_t v; asm volatile("csrr %0, sip" : "=r"(v)); return v;
}

static inline void clear_sip_ssip(void)
{
    asm volatile("csrc sip, %0" :: "r"(SIP_SSIP));
}

static inline void enable_s_interrupts(void)
{
    asm volatile("csrs sstatus, %0" :: "r"(MSTATUS_SIE));
}

static inline void disable_s_interrupts(void)
{
    asm volatile("csrc sstatus, %0" :: "r"(MSTATUS_SIE));
}

static inline void enable_s_timer_irq(void)
{
    asm volatile("csrs sie, %0" :: "r"(SIP_STIP));
}

static inline void enable_s_soft_irq(void)
{
    asm volatile("csrs sie, %0" :: "r"(SIP_SSIP));
}

static inline void enable_s_ext_irq(void)
{
    asm volatile("csrs sie, %0" :: "r"(SIP_SEIP));
}

static inline uint64_t read_mtime(void)
{
    return *(volatile uint64_t *)CLINT_MTIME;
}

/* --- Dual-core (Phase 4) --- */


/* OLD CORE 1 WAKE*/

// extern void (*volatile core1_func)(void);

// static inline void wake_core1(void (*func)(void))
// {
//     core1_func = func;
//     asm volatile("fence w, w" ::: "memory");
//     *(volatile uint32_t *)CLINT_MSIP(1) = 1;
// }

/* END OLD CORE 1 WAKE*/

void wake_core1_smode(void (*func)(void));

static inline void spin_wait(volatile int *flag, int expected)
{
    while (*flag != expected)
        asm volatile("" ::: "memory");
}

/* --- Atomics (Phase 3) --- */

static inline uint64_t amo_swap_d(volatile uint64_t *addr, uint64_t val)
{
    uint64_t r;
    asm volatile("amoswap.d %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint64_t amo_add_d(volatile uint64_t *addr, uint64_t val)
{
    uint64_t r;
    asm volatile("amoadd.d %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint64_t amo_and_d(volatile uint64_t *addr, uint64_t val)
{
    uint64_t r;
    asm volatile("amoand.d %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint64_t amo_or_d(volatile uint64_t *addr, uint64_t val)
{
    uint64_t r;
    asm volatile("amoor.d %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint64_t amo_xor_d(volatile uint64_t *addr, uint64_t val)
{
    uint64_t r;
    asm volatile("amoxor.d %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint64_t amo_min_d(volatile uint64_t *addr, uint64_t val)
{
    uint64_t r;
    asm volatile("amomin.d %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint64_t amo_max_d(volatile uint64_t *addr, uint64_t val)
{
    uint64_t r;
    asm volatile("amomax.d %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint64_t amo_minu_d(volatile uint64_t *addr, uint64_t val)
{
    uint64_t r;
    asm volatile("amominu.d %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint64_t amo_maxu_d(volatile uint64_t *addr, uint64_t val)
{
    uint64_t r;
    asm volatile("amomaxu.d %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint32_t amo_minu_w(volatile uint32_t *addr, uint32_t val)
{
    uint32_t r;
    asm volatile("amominu.w %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint32_t amo_maxu_w(volatile uint32_t *addr, uint32_t val)
{
    uint32_t r;
    asm volatile("amomaxu.w %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint32_t amo_swap_w(volatile uint32_t *addr, uint32_t val)
{
    uint32_t r;
    asm volatile("amoswap.w %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint32_t amo_add_w(volatile uint32_t *addr, uint32_t val)
{
    uint32_t r;
    asm volatile("amoadd.w %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint32_t amo_and_w(volatile uint32_t *addr, uint32_t val)
{
    uint32_t r;
    asm volatile("amoand.w %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint32_t amo_or_w(volatile uint32_t *addr, uint32_t val)
{
    uint32_t r;
    asm volatile("amoor.w %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint32_t amo_xor_w(volatile uint32_t *addr, uint32_t val)
{
    uint32_t r;
    asm volatile("amoxor.w %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint32_t amo_min_w(volatile uint32_t *addr, int32_t val)
{
    uint32_t r;
    asm volatile("amomin.w %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint32_t amo_max_w(volatile uint32_t *addr, int32_t val)
{
    uint32_t r;
    asm volatile("amomax.w %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint64_t lr_d(volatile uint64_t *addr)
{
    uint64_t r;
    asm volatile("lr.d %0, (%1)" : "=r"(r) : "r"(addr) : "memory");
    return r;
}

static inline uint64_t sc_d(volatile uint64_t *addr, uint64_t val)
{
    uint64_t r;
    asm volatile("sc.d %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

static inline uint64_t lr_d_aq(volatile uint64_t *addr)
{
    uint64_t r;
    asm volatile("lr.d.aq %0, (%1)" : "=r"(r) : "r"(addr) : "memory");
    return r;
}

static inline uint64_t sc_d_rl(volatile uint64_t *addr, uint64_t val)
{
    uint64_t r;
    asm volatile("sc.d.rl %0, %2, (%1)" : "=r"(r) : "r"(addr), "r"(val) : "memory");
    return r;
}

/* CAS via LR/SC. Returns 1 on success, 0 on failure. */
static inline int cas_d(volatile uint64_t *addr, uint64_t expected, uint64_t desired)
{
    uint64_t old, sc_result;
    asm volatile(
        "1: lr.d.aq  %0, (%2)\n"
        "   bne      %0, %3, 2f\n"
        "   sc.d.rl  %1, %4, (%2)\n"
        "   bnez     %1, 1b\n"
        "2:"
        : "=&r"(old), "=&r"(sc_result)
        : "r"(addr), "r"(expected), "r"(desired)
        : "memory"
    );
    return (old == expected) ? 1 : 0;
}

#endif
