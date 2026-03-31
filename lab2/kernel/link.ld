OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
    . = KERNEL_START;
    _start = .;
    .text : { *(.text.boot) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }

    . = ALIGN(8); /* Align to 8 bytes so BSS can be zeroed with 8-byte stores (sd) in start.S */
    .bss : {
        __bss_start = .;
        *(.sbss) *(.bss)
        __bss_stop = .;
    }
    . = ALIGN(16);
    . = . + 0x4000;
    _end = .;
}
