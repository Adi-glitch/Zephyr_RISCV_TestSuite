/* link.ld — 2 MB SRAM at 0x8000_0000, HTIF at end */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    SRAM (rwx) : ORIGIN = 0x80000000, LENGTH = 2M
}

SECTIONS
{
    . = ORIGIN(SRAM);

    .text     : { *(.text.entry) *(.text .text.*) } > SRAM
    .rodata   : { *(.rodata .rodata.*) *(.srodata .srodata.*) } > SRAM
    .data     : { *(.data .data.*) *(.sdata .sdata.*) } > SRAM

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON)
        . = ALIGN(4096);
        _page_table_start = .;
        . += 32K;
        _page_table_end = .;
        _bss_end = .;
    } > SRAM

    . = ALIGN(16);
    .stacks (NOLOAD) : {
        _stack1_bottom = .; . += 16K; _stack1_top = .;
        _stack0_bottom = .; . += 16K; _stack0_top = .;
    } > SRAM

    . = ALIGN(8);
    _heap_start = .;
    _heap_end = 0x80000000 + 2M - 0x1000;

    .htif 0x801FF000 (NOLOAD) : {
        PROVIDE(tohost = .); . += 8;
        PROVIDE(fromhost = .); . += 8;
    } > SRAM

    .m_trap_info (NOLOAD) : {
        PROVIDE(_m_trap_info = .); . += 32;
    } > SRAM

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
