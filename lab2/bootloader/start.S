.section ".text.boot"
.global _start

_start:
    /* Get current runtime address of _start */
    /* la t0, _start works in medany as PC-relative to the global symbol */
    la t0, _start

    /* Get linked address of _start from the constant block */
    lla t4, 1f
    ld t1, 0(t4)

    /* If we are already at the target, skip to BSS clearing */
    beq t0, t1, 2f

    /* Get linked address of _end */
    lla t4, 3f
    ld t2, 0(t4)

copy_loop:
    ld t3, 0(t0)
    sd t3, 0(t1)
    addi t0, t0, 8
    addi t1, t1, 8
    blt t1, t2, copy_loop

    /* Flush I-cache before jumping to relocated code */
    fence.i

    /* Absolute jump to the relocated relocate_done label */
    lla t4, 4f
    ld t0, 0(t4)
    jr t0

2:  /* relocate_done */
    la a3, __bss_start
    la a4, __bss_stop
    ble a4, a3, 5f
clear_bss:
    sd zero, 0(a3)
    addi a3, a3, 8
    blt a3, a4, clear_bss
5:  /* clear_bss_done */
    la sp, _end
    mv a0, a1               /* Preserve FDT pointer for start_kernel */
    tail start_kernel

/* Constants holding the 64-bit absolute addresses */
.align 3
1: .dword _start
3: .dword _end
4: .dword 2b
