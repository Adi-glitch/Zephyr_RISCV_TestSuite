/* smode.c — S-mode runtime: trap handler, page tables, UART, HTIF */

#include "platform.h"
#include "test.h"

extern char _page_table_start[];
extern char _page_table_end[];
extern volatile uint64_t tohost;
extern volatile uint64_t fromhost;
extern void _s_trap_vector(void);

static uint64_t *const pt_pool = (uint64_t *)&_page_table_start;
static int pt_pool_next_page = 0;

volatile struct trap_state g_trap = {0};

/* --- UART --- */

void uart_putc(char c)
{
#ifdef SPIKE_BUILD
    while (tohost != 0)
        ;
    tohost = ((uint64_t)1 << 56) | ((uint64_t)1 << 48) | (unsigned char)c;
    while (tohost != 0)
        ;
    if (fromhost != 0)
        fromhost = 0;
#else
#if UART_REG_SHIFT == 0
    volatile uint8_t *thr = (volatile uint8_t *)UART0_THR;
    volatile uint8_t *lsr = (volatile uint8_t *)UART0_LSR;
#else
    volatile uint32_t *thr = (volatile uint32_t *)UART0_THR;
    volatile uint32_t *lsr = (volatile uint32_t *)UART0_LSR;
#endif
    while ((*lsr & UART0_LSR_THRE) == 0)
        ;
    *thr = (unsigned char)c;
#endif
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_put_hex(uint64_t val)
{
    static const char hex[] = "0123456789abcdef";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4)
        uart_putc(hex[(val >> i) & 0xF]);
}

void uart_put_dec(int val)
{
    if (val < 0) { uart_putc('-'); val = -val; }
    char buf[20];
    int i = 0;
    do { buf[i++] = '0' + (val % 10); val /= 10; } while (val > 0);
    while (--i >= 0) uart_putc(buf[i]);
}

/* --- HTIF --- */

static void htif_write(uint64_t val)
{
    while (tohost != 0)
        fromhost = 0;
    tohost = val;
}

void test_pass(void)
{
    uart_puts("[PASS]\n");
    htif_write(1);
    while (1) asm volatile("wfi");
}

void test_fail(int code)
{
    uart_puts("[FAIL] code=");
    uart_put_dec(code);
    uart_putc('\n');
    htif_write((code << 1) | 1);
    while (1) asm volatile("wfi");
}

void test_assert(int condition, const char *msg, int fail_code)
{
    if (!condition) {
        uart_puts("ASSERT FAILED: ");
        uart_puts(msg);
        uart_putc('\n');
        test_fail(fail_code);
    }
}

/* --- CSR helpers --- */

static inline uint64_t read_csr_satp(void)
{
    uint64_t val; asm volatile("csrr %0, satp" : "=r"(val)); return val;
}

static inline void write_csr_satp(uint64_t val)
{
    asm volatile("csrw satp, %0" :: "r"(val));
}

void sfence_vma_all(void)
{
    asm volatile("sfence.vma zero, zero" ::: "memory");
}

void sfence_vma_addr(uint64_t addr)
{
    asm volatile("sfence.vma %0, zero" :: "r"(addr) : "memory");
}

void fence_i(void)
{
    asm volatile("fence.i" ::: "memory");
}

static inline void write_csr_stvec(uint64_t val)
{
    asm volatile("csrw stvec, %0" :: "r"(val));
}

/* --- Page table utilities --- */

uint64_t *pt_alloc_page(void)
{
    uint64_t *page = pt_pool + (pt_pool_next_page * PTES_PER_PT);
    pt_pool_next_page++;
    for (int i = 0; i < PTES_PER_PT; i++) page[i] = 0;
    return page;
}

void pt_reset(void) { pt_pool_next_page = 0; }

static inline uint64_t pt_ppn(uint64_t *pt) { return ((uint64_t)pt) >> PGSHIFT; }

static inline uint64_t pte_branch(uint64_t *child_pt)
{
    return (pt_ppn(child_pt) << PTE_PPN_SHIFT) | PTE_V;
}

static inline uint64_t pte_leaf(uint64_t phys_addr, uint64_t perm)
{
    return ((phys_addr >> PGSHIFT) << PTE_PPN_SHIFT) | perm | PTE_V | PTE_A | PTE_D;
}

static inline int vpn2(uint64_t va) { return (va >> 30) & 0x1FF; }
static inline int vpn1(uint64_t va) { return (va >> 21) & 0x1FF; }
static inline int vpn0(uint64_t va) { return (va >> 12) & 0x1FF; }

uint64_t *pt_setup_identity_kernel(void)
{
    uint64_t *root = pt_alloc_page();
    int sram_vpn2 = vpn2(SRAM_BASE);
    root[sram_vpn2] = pte_leaf(SRAM_BASE & ~(GIGAPAGE_SIZE - 1),
                               PTE_R | PTE_W | PTE_X);
    /* I/O gigapage at VA 0 */
    root[0] = pte_leaf(0x00000000UL, PTE_R | PTE_W);
    return root;
}

void pt_map_page(uint64_t *root, uint64_t va, uint64_t pa,
                 uint64_t perm, int valid)
{
    int idx2 = vpn2(va), idx1 = vpn1(va), idx0 = vpn0(va);
    if (!(root[idx2] & PTE_V)) { root[idx2] = pte_branch(pt_alloc_page()); }
    uint64_t *l1 = (uint64_t *)((root[idx2] >> PTE_PPN_SHIFT << PGSHIFT));
    if (!(l1[idx1] & PTE_V)) { l1[idx1] = pte_branch(pt_alloc_page()); }
    uint64_t *l2 = (uint64_t *)((l1[idx1] >> PTE_PPN_SHIFT << PGSHIFT));
    if (valid) l2[idx0] = pte_leaf(pa, perm);
    else       l2[idx0] = ((pa >> PGSHIFT) << PTE_PPN_SHIFT) | (perm & ~PTE_V);
}

uint64_t *pt_walk(uint64_t *root, uint64_t va, int *level)
{
    int idx2 = vpn2(va);
    uint64_t pte = root[idx2];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X))) { *level = 2; return &root[idx2]; }
    if (!(pte & PTE_V)) { *level = 2; return &root[idx2]; }
    uint64_t *l1 = (uint64_t *)((pte >> PTE_PPN_SHIFT) << PGSHIFT);
    int idx1 = vpn1(va);
    pte = l1[idx1];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X))) { *level = 1; return &l1[idx1]; }
    if (!(pte & PTE_V)) { *level = 1; return &l1[idx1]; }
    uint64_t *l2 = (uint64_t *)((pte >> PTE_PPN_SHIFT) << PGSHIFT);
    int idx0 = vpn0(va);
    *level = 0;
    return &l2[idx0];
}

void mmu_enable_sv39(uint64_t *root)
{
    uint64_t satp_val = (SATP_MODE_SV39 << SATP_MODE_SHIFT) | pt_ppn(root);
    write_csr_satp(satp_val);
    sfence_vma_all();
}

/* --- S-mode trap handler --- */

void s_trap_handler(uint64_t scause, uint64_t stval, uint64_t sepc,
                    uint64_t *frame)
{
    g_trap.scause = scause;
    g_trap.stval = stval;
    g_trap.sepc = sepc;
    g_trap.fault_count++;

    int is_interrupt = (scause >> 63) & 1;
    uint64_t cause_code = scause & 0x7FFFFFFFFFFFFFFFUL;

    if (is_interrupt) {
        g_trap.irq_count++;
        g_trap.last_irq_cause = scause;

        switch (cause_code) {
        case IRQ_S_TIMER:
            g_trap.timer_irq_count++;
            /* Clear STIP via SBI set_timer(max) */
            {
                register uint64_t a0 asm("a0") = 0xFFFFFFFFFFFFFFFFUL;
                register uint64_t a7 asm("a7") = SBI_SET_TIMER;
                asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
            }
            return;
        case IRQ_S_SOFT:
            g_trap.soft_irq_count++;
            asm volatile("csrc sip, %0" :: "r"(SIP_SSIP));
            return;
        case IRQ_S_EXT:
            return;
        default:
            uart_puts("UNEXPECTED S-MODE INTERRUPT: cause=");
            uart_put_hex(scause);
            uart_putc('\n');
            test_fail(100 + (int)cause_code);
            return;
        }
    }

    /* Exceptions */
    switch (cause_code) {
    case CAUSE_INST_PAGE_FAULT:
    case CAUSE_LOAD_PAGE_FAULT:
    case CAUSE_STORE_PAGE_FAULT:
    {
        uart_puts("  [TRAP] Page fault: cause=");
        uart_put_dec((int)cause_code);
        uart_puts(" stval=");
        uart_put_hex(stval);
        uart_puts(" sepc=");
        uart_put_hex(sepc);
        uart_putc('\n');

        if (g_trap.handler_action == 1) {
            /* Skip faulting instruction */
            if (cause_code == CAUSE_INST_PAGE_FAULT) {
                uart_puts("  [TRAP] ERROR: skip unsupported for ifetch faults\n");
                test_fail(201);
                return;
            }
            uint16_t insn_lo = *(volatile uint16_t *)sepc;
            int insn_len = ((insn_lo & 0x3) == 0x3) ? 4 : 2;
            uint64_t *sepc_slot = frame + 30;
            *sepc_slot = sepc + insn_len;
            return;
        }

        /* Default: fix PTE */
        uint64_t satp = read_csr_satp();
        uint64_t *root = (uint64_t *)((satp & 0x00000FFFFFFFFFFFUL) << PGSHIFT);
        int level;
        uint64_t *pte_ptr = pt_walk(root, stval, &level);
        if (pte_ptr == (void*)0) { test_fail(200); return; }

        uart_puts("  [TRAP] PTE before fix: ");
        uart_put_hex(*pte_ptr);
        uart_putc('\n');

        uint64_t fixed = *pte_ptr;
        fixed |= PTE_V | PTE_A | PTE_D;
        if (cause_code == CAUSE_LOAD_PAGE_FAULT)       fixed |= PTE_R;
        else if (cause_code == CAUSE_STORE_PAGE_FAULT)  fixed |= PTE_R | PTE_W;
        else if (cause_code == CAUSE_INST_PAGE_FAULT)   fixed |= PTE_R | PTE_X;
        *pte_ptr = fixed;

        uart_puts("  [TRAP] PTE after fix:  ");
        uart_put_hex(*pte_ptr);
        uart_putc('\n');

        asm volatile("fence w, w" ::: "memory");
        sfence_vma_addr(stval);
        return;
    }

    case CAUSE_ILLEGAL_INST:
        uart_puts("  [TRAP] Illegal instruction at sepc=");
        uart_put_hex(sepc);
        uart_putc('\n');
        test_fail(300);
        return;

    case CAUSE_ECALL_SMODE:
        uart_puts("  [TRAP] ecall from S-mode at sepc=");
        uart_put_hex(sepc);
        uart_putc('\n');
        {
            uint64_t *sepc_slot = frame + 30;
            *sepc_slot = sepc + 4;
        }
        return;

    default:
        uart_puts("  [TRAP] Unexpected exception: cause=");
        uart_put_dec((int)cause_code);
        uart_puts(" stval=");
        uart_put_hex(stval);
        uart_putc('\n');
        test_fail(400 + (int)cause_code);
        return;
    }
}

static void (*volatile g_smode_target)(void) = 0;
static volatile int g_core1_smode_ready = 0;

/* Core 1's S-mode park loop */
void core1_smode_park_loop(void)
{
    // Define stvec for core1 and enable software interrupts
    write_csr_stvec((uint64_t)&_s_trap_vector);
    enable_s_soft_irq();
    enable_s_interrupts();

    g_core1_smode_ready = 1; // signal the core is ready and parked
    asm volatile("fence w, w" ::: "memory");

    while (1) {
        clear_sip_ssip(); // clear interrupt that woke core 1 up
        
        while (g_smode_target == 0)
            asm volatile("wfi"); // wait for s-mode interrupt
        
        g_smode_target(); // run the test in s-mode
        
        g_smode_target = 0; // reset and wait for next test
        asm volatile("fence w, w" ::: "memory");
    }
}

void wake_core1_smode(void (*func)(void))
{
    // Wait and ensure core 1 is properly booted
    while (g_core1_smode_ready == 0)
        asm volatile("" ::: "memory");

    g_smode_target = func; // point to the target core1 has to run
    asm volatile("fence w, w" ::: "memory");
    sbi_send_ipi(1); // wake the core with an interrupt
}

/* --- S-mode entry point --- */

extern int test_main(void);

void s_mode_entry(void)
{
    write_csr_stvec((uint64_t)&_s_trap_vector);
    uart_puts("\n=== S-mode entry, stvec set ===\n");
    int result = test_main();
    if (result == 0) test_pass();
    else             test_fail(result);
}
