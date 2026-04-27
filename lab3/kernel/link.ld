OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
    . = KERNEL_START;

    /* Exported for mm.c: physical boundaries of the kernel image */
    _start = .;
    _text_start = .;

    .text : { *(.text.boot) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }

    /* Align to 8 bytes so BSS can be zeroed with 8-byte stores (sd) in start.S */
    . = ALIGN(8);
    _bss_start = .;
    .bss : {
        __bss_start = .;
        *(.sbss) *(.bss)
        __bss_stop = .;
    }
    _bss_end = .;

    /* 0x4000-byte stack region reserved right after BSS */
    . = ALIGN(16);
    . = . + 0x4000;
    _end = .;
}
