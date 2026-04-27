.section ".text.boot"
.global _start

_start:
    /*
     * U-Boot passes arguments following the RISC-V boot convention:
     *   a0 = hart ID
     *   a1 = DTB pointer
     *
     * a1 must be preserved through relocation and BSS clearing so it can be
     * passed to start_kernel(void *dtb) as the first C argument (a0).
     */

    /* Get our current runtime address (where U-Boot actually loaded us). */
    la t0, _start

    /* Get the linked address of _start from the constant pool below. */
    lla t4, linked_start_addr
    ld t1, 0(t4)

    /* If runtime == linked address, no relocation needed. */
    beq t0, t1, relocate_done

    /* Get the linked address of _end to know how many bytes to copy. */
    lla t4, linked_end_addr
    ld t2, 0(t4)

copy_loop:
    ld t3, 0(t0)
    sd t3, 0(t1)
    addi t0, t0, 8
    addi t1, t1, 8
    blt t1, t2, copy_loop

    /* Flush I-cache before jumping into the relocated code. */
    fence.i

    /*
     * Absolute jump to relocate_done at its linked address.
     * A plain 'j relocate_done' would be PC-relative and land in the old copy;
     * we need to jump to the new copy, so we load the linked address explicitly.
     */
    lla t4, linked_relocate_done_addr
    ld t0, 0(t4)
    jr t0

relocate_done:
    la a3, __bss_start
    la a4, __bss_stop
    ble a4, a3, bss_done
clear_bss:
    sd zero, 0(a3)
    addi a3, a3, 8
    blt a3, a4, clear_bss
bss_done:
    la sp, _end

    /*
     * U-Boot puts the DTB pointer in a1, but start_kernel(void *dtb) expects
     * it as the first C argument (a0).  Move it before the call.
     */
    mv a0, a1
    tail start_kernel

/* ── Constant pool: 64-bit linked addresses ─────────────────────────────────── */
.align 3
linked_start_addr:         .dword _start
linked_end_addr:           .dword _end
linked_relocate_done_addr: .dword relocate_done
