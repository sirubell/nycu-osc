.section ".text.boot"
.global _start

_start:
    /*
     * U-Boot / bootloader passes arguments in registers per the RISC-V calling
     * convention:
     *   a0 = DTB pointer
     * a0 must not be clobbered while we clear BSS and set up the stack, because
     * it will be forwarded directly to start_kernel(void *dtb) below.
     */

    /* Zero out the BSS section using 8-byte stores.
     * link.ld aligns __bss_start to 8 bytes, so sd (store doubleword) is safe. */
    la a3, __bss_start
    la a4, __bss_stop
    ble a4, a3, 1f
clear_bss:
    sd zero, 0(a3)
    addi a3, a3, 8
    blt a3, a4, clear_bss
1:
    /* Point the stack pointer to the top of the reserved stack region (_end).
     * The 0x4000-byte region is allocated by link.ld right after BSS. */
    la sp, _end

    /* Tail-call into the C entry point.  'tail' is a pseudo-instruction that
     * expands to 'auipc + jalr' with ra = x0, so it acts as a plain jump.
     * Because a0 is still untouched, start_kernel receives the DTB pointer. */
    tail start_kernel
