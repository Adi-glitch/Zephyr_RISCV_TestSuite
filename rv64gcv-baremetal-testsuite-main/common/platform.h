#ifndef PLATFORM_H
#define PLATFORM_H

/* SRAM */
#ifndef SRAM_SIZE
#define SRAM_SIZE 0x00200000UL /* 2 MB default */
#endif
#define SRAM_BASE 0x80000000UL
#define SRAM_END (SRAM_BASE + SRAM_SIZE)

/* Peripherals */
#define DEBUG_BASE 0x00000000UL
#define DEBUG_SIZE 0x00001000UL
#define BOOTROM_BASE 0x00001000UL
#define BOOTROM_SIZE 0x00001000UL

/* CLINT */
#define CLINT_BASE 0x02000000UL
#define CLINT_SIZE 0x0000C000UL
#define CLINT_MSIP(hart) (CLINT_BASE + 0x0000 + (hart) * 4)
#define CLINT_MTIMECMP(hart) (CLINT_BASE + 0x4000 + (hart) * 8)
#define CLINT_MTIME (CLINT_BASE + 0xBFF8)

/* PLIC */
#define PLIC_BASE 0x0C000000UL
#define PLIC_SIZE 0x04000000UL

/* UART0 */
#define UART0_BASE 0x10000000UL
#define UART0_SIZE 0x00001000UL
#ifndef UART_REG_SHIFT
#define UART_REG_SHIFT 2 /* RTL default: 4-byte stride */
#endif
#define UART0_THR (UART0_BASE + (0x00 << UART_REG_SHIFT))
#define UART0_LSR (UART0_BASE + (0x05 << UART_REG_SHIFT))
#define UART0_LSR_THRE (1 << 5)

/* SPI0 / GPIO / DMA */
#define SPI0_BASE 0x10001000UL
#define SPI0_SIZE 0x00001000UL
#define GPIO_BASE 0x10002000UL
#define GPIO_SIZE 0x00001000UL
#define DMA_BASE 0x10003000UL
#define DMA_SIZE 0x00001000UL
#define DMA_SRC_ADDR (DMA_BASE + 0x00)
#define DMA_DST_ADDR (DMA_BASE + 0x08)
#define DMA_LENGTH (DMA_BASE + 0x10)
#define DMA_CONTROL (DMA_BASE + 0x18)
#define DMA_STATUS (DMA_BASE + 0x20)

/* HTIF — at end of SRAM, polled by testbench */
#define HTIF_TOHOST (SRAM_END - 0x1000)
#define HTIF_FROMHOST (SRAM_END - 0x0FF8)

/* Cores */
#define NUM_HARTS 2
#define HART_0 0
#define HART_1 1
#define STACK_SIZE 0x4000 /* 16 KB per core */

/* Sv39 MMU */
#define PGSHIFT 12
#define PGSIZE (1UL << PGSHIFT)
#define MEGAPAGE_SIZE (PGSIZE * 512)
#define GIGAPAGE_SIZE (MEGAPAGE_SIZE * 512)
#define PTES_PER_PT 512
#define PTE_PPN_SHIFT 10

#define PTE_V 0x001
#define PTE_R 0x002
#define PTE_W 0x004
#define PTE_X 0x008
#define PTE_U 0x010
#define PTE_G 0x020
#define PTE_A 0x040
#define PTE_D 0x080

#define SATP_MODE_BARE 0UL
#define SATP_MODE_SV39 8UL
#define SATP_MODE_SV48 9UL
#define SATP_MODE_SHIFT 60

/* Exception causes */
#define CAUSE_INST_MISALIGNED 0
#define CAUSE_INST_ACCESS 1
#define CAUSE_ILLEGAL_INST 2
#define CAUSE_BREAKPOINT 3
#define CAUSE_LOAD_MISALIGNED 4
#define CAUSE_LOAD_ACCESS 5
#define CAUSE_STORE_MISALIGNED 6
#define CAUSE_STORE_ACCESS 7
#define CAUSE_ECALL_UMODE 8
#define CAUSE_ECALL_SMODE 9
#define CAUSE_ECALL_MMODE 11
#define CAUSE_INST_PAGE_FAULT 12
#define CAUSE_LOAD_PAGE_FAULT 13
#define CAUSE_STORE_PAGE_FAULT 15

/* Interrupt causes */
#define IRQ_S_SOFT 1
#define IRQ_M_SOFT 3
#define IRQ_S_TIMER 5
#define IRQ_M_TIMER 7
#define IRQ_S_EXT 9
#define IRQ_M_EXT 11

/* mstatus / sstatus bits */
#define MSTATUS_SIE (1UL << 1)
#define MSTATUS_MIE (1UL << 3)
#define MSTATUS_SPIE (1UL << 5)
#define MSTATUS_MPIE (1UL << 7)
#define MSTATUS_SPP (1UL << 8)
#define MSTATUS_MPP_MASK (3UL << 11)
#define MSTATUS_MPP_S (1UL << 11)
#define MSTATUS_MPP_M (3UL << 11)
#define MSTATUS_SUM (1UL << 18)
#define MSTATUS_MXR (1UL << 19)

/* mie / mip / sie / sip bits */
#define MIP_SSIP (1UL << 1)
#define MIP_MSIP (1UL << 3)
#define MIP_STIP (1UL << 5)
#define MIP_MTIP (1UL << 7)
#define MIP_SEIP (1UL << 9)
#define MIP_MEIP (1UL << 11)
#define SIP_SSIP MIP_SSIP
#define SIP_STIP MIP_STIP
#define SIP_SEIP MIP_SEIP

/* SBI function IDs */
#define SBI_SET_TIMER 0
#define SBI_SEND_IPI 1
#define SBI_CLEAR_IPI 2

/* medeleg: bits 0-9, 12, 13, 15 */
#define MEDELEG_DEFAULT 0x0000B3FFUL
/* Without ecall-S delegation (bit 9 cleared) for SBI */
#define MEDELEG_WITH_SBI 0x0000B1FFUL

/* PMP */
#define PMP_R 0x01
#define PMP_W 0x02
#define PMP_X 0x04
#define PMP_NAPOT 0x18
#define PMP_TOR 0x08

#endif
