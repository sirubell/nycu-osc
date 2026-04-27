OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
    _reloc_target = KERNEL_START + 0x200000;
    . = _reloc_target;
    _start = .;
    .text : { *(.text.boot) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    . = ALIGN(8);
    .bss : {
        __bss_start = .;
        *(.sbss) *(.bss)
        __bss_stop = .;
    }
    . = ALIGN(16);
    . = . + 0x4000;
    _end = .;
}
