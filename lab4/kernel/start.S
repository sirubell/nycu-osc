.section ".text.boot"
.global _start

/*
 * Trap-frame layout (each slot = 8 bytes, 35 slots total = 280 bytes):
 *
 *  slot  offset   register
 *   0      0      ra
 *   1      8      sp (original – user sp or kernel sp at trap time)
 *   2     16      gp
 *   3     24      tp
 *   4     32      t0
 *   5     40      t1
 *   6     48      t2
 *   7     56      s0
 *   8     64      s1
 *   9     72      a0
 *  10     80      a1
 *  11     88      a2
 *  12     96      a3
 *  13    104      a4
 *  14    112      a5
 *  15    120      a6
 *  16    128      a7
 *  17    136      s2
 *  18    144      s3
 *  19    152      s4
 *  20    160      s5
 *  21    168      s6
 *  22    176      s7
 *  23    184      s8
 *  24    192      s9
 *  25    200      s10
 *  26    208      s11
 *  27    216      t3
 *  28    224      t4
 *  29    232      t5
 *  30    240      t6
 *  31    248      sepc
 *  32    256      sstatus
 *  33    264      scause
 *  34    272      stval
 */

_start:
    /* Standard RISC-V boot convention: a0 = hartid, a1 = dtb. */
    mv s0, a0   /* hartid */
    mv s1, a1   /* dtb    */

    /* Zero BSS with 8-byte stores (link.ld aligns __bss_start to 8). */
    la   a3, __bss_start
    la   a4, __bss_stop
    ble  a4, a3, bss_done
clear_bss:
    sd   zero, 0(a3)
    addi a3, a3, 8
    blt  a3, a4, clear_bss
bss_done:
    /* Store hartid AFTER BSS clear so the zero-fill doesn't overwrite it. */
    la   a3, boot_cpu_hartid
    sd   s0, 0(a3)
    /* Install trap vector (direct mode, bit[1:0] = 0). */
    la   a3, handle_exception
    csrw stvec, a3

    /* sscratch = 0 marks "currently in S-mode" to the trap handler. */
    csrw sscratch, zero

    /* Stack top = _end (0x4000 bytes reserved after BSS by link.ld). */
    la   sp, _end

    /* Tail-call start_kernel(dtb); dtb is in s1. */
    mv   a0, s1
    tail start_kernel


/* ═══════════════════════════════════════════════════════════════════════════
 * handle_exception – universal trap entry point (exceptions + interrupts)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * sscratch semantics:
 *   0        →  trap came from S-mode; sp is already the kernel sp.
 *   non-zero →  trap came from U-mode; sscratch holds the kernel sp that
 *               switch_to_user stashed there before doing sret.
 */
.align 2
handle_exception:
    /* Atomic swap: sp ↔ sscratch.
     *   S-mode path: sp ← 0,         sscratch ← old kernel sp
     *   U-mode path: sp ← kernel sp, sscratch ← user sp            */
    csrrw sp, sscratch, sp
    bnez  sp, .Lsave_ctx        /* non-zero ⇒ U-mode, sp = kernel sp */

    /* S-mode path: sp is now 0, sscratch holds the real kernel sp. */
    csrr  sp, sscratch          /* sp ← kernel sp (sscratch unchanged) */

.Lsave_ctx:
    /* sp = kernel sp; sscratch = original sp (kernel or user). */
    addi sp, sp, -280           /* allocate 35-slot context frame */

    /* Save t0 first so we can use it as a scratch register. */
    sd   t0,  4*8(sp)

    /* Read original sp from sscratch and save it in the frame. */
    csrr t0,  sscratch
    sd   t0,  1*8(sp)

    /* Re-mark S-mode while handling the trap. */
    csrw sscratch, zero

    /* Save all other GP registers. */
    sd   ra,  0*8(sp)
    /* slot 1 = sp: done above */
    sd   gp,  2*8(sp)
    sd   tp,  3*8(sp)
    /* slot 4 = t0: done above */
    sd   t1,  5*8(sp)
    sd   t2,  6*8(sp)
    sd   s0,  7*8(sp)
    sd   s1,  8*8(sp)
    sd   a0,  9*8(sp)
    sd   a1, 10*8(sp)
    sd   a2, 11*8(sp)
    sd   a3, 12*8(sp)
    sd   a4, 13*8(sp)
    sd   a5, 14*8(sp)
    sd   a6, 15*8(sp)
    sd   a7, 16*8(sp)
    sd   s2, 17*8(sp)
    sd   s3, 18*8(sp)
    sd   s4, 19*8(sp)
    sd   s5, 20*8(sp)
    sd   s6, 21*8(sp)
    sd   s7, 22*8(sp)
    sd   s8, 23*8(sp)
    sd   s9, 24*8(sp)
    sd   s10,25*8(sp)
    sd   s11,26*8(sp)
    sd   t3, 27*8(sp)
    sd   t4, 28*8(sp)
    sd   t5, 29*8(sp)
    sd   t6, 30*8(sp)

    /* Save trap CSRs. */
    csrr t0, sepc;    sd t0, 31*8(sp)
    csrr t0, sstatus; sd t0, 32*8(sp)
    csrr t0, scause;  sd t0, 33*8(sp)
    csrr t0, stval;   sd t0, 34*8(sp)

    /* Call C trap dispatcher, passing frame pointer in a0. */
    mv   a0, sp
    call do_trap

    /* ── Epilogue ─────────────────────────────────────────────────────────
     * If we are returning to U-mode (sstatus.SPP == 0), re-arm sscratch
     * with the kernel sp so the NEXT trap from U-mode can find it.      */
    ld   t0, 32*8(sp)       /* load saved sstatus */
    li   t1, (1 << 8)       /* SPP bit */
    and  t0, t0, t1
    bnez t0, .Lno_sscratch  /* SPP=1 → returning to S-mode, skip */
    /* SPP=0 → returning to U-mode: kernel sp = frame_base + 280 */
    addi t0, sp, 280
    csrw sscratch, t0
.Lno_sscratch:

    /* Restore sepc and sstatus from (possibly modified) frame. */
    ld   t0, 31*8(sp); csrw sepc,    t0
    ld   t0, 32*8(sp); csrw sstatus, t0

    /* Restore GP registers (sp last, since it is our frame base). */
    ld   ra,  0*8(sp)
    /* sp restored after all other registers */
    ld   gp,  2*8(sp)
    ld   tp,  3*8(sp)
    ld   t0,  4*8(sp)
    ld   t1,  5*8(sp)
    ld   t2,  6*8(sp)
    ld   s0,  7*8(sp)
    ld   s1,  8*8(sp)
    ld   a0,  9*8(sp)
    ld   a1, 10*8(sp)
    ld   a2, 11*8(sp)
    ld   a3, 12*8(sp)
    ld   a4, 13*8(sp)
    ld   a5, 14*8(sp)
    ld   a6, 15*8(sp)
    ld   a7, 16*8(sp)
    ld   s2, 17*8(sp)
    ld   s3, 18*8(sp)
    ld   s4, 19*8(sp)
    ld   s5, 20*8(sp)
    ld   s6, 21*8(sp)
    ld   s7, 22*8(sp)
    ld   s8, 23*8(sp)
    ld   s9, 24*8(sp)
    ld   s10,25*8(sp)
    ld   s11,26*8(sp)
    ld   t3, 27*8(sp)
    ld   t4, 28*8(sp)
    ld   t5, 29*8(sp)
    ld   t6, 30*8(sp)
    ld   sp,  1*8(sp)   /* restore original sp last */
    sret


/* ═══════════════════════════════════════════════════════════════════════════
 * switch_to_user(void *entry, void *user_sp)
 *   a0 = user entry point
 *   a1 = user stack pointer
 *
 * Sets up sstatus for U-mode return, stores kernel sp in sscratch, then
 * sret to user space.
 * ═══════════════════════════════════════════════════════════════════════════
 */
.global switch_to_user
switch_to_user:
    csrw sepc, a0           /* trap return address = user entry */

    li   t0, (1 << 8)       /* SPP bit: clear → return to U-mode */
    csrc sstatus, t0

    li   t0, (1 << 5)       /* SPIE bit: set → interrupts on after sret */
    csrs sstatus, t0

    /* Save caller's ra and sp so the exit syscall can resume the kernel. */
    la   t0, saved_kernel_ra
    sd   ra, 0(t0)
    la   t0, saved_kernel_sp
    sd   sp, 0(t0)

    csrw sscratch, sp       /* stash kernel sp; handle_exception reads it */
    mv   sp, a1             /* switch to user stack */
    sret


/* ═══════════════════════════════════════════════════════════════════════════
 * BSS globals
 * ═══════════════════════════════════════════════════════════════════════════
 */
.section ".bss"
.global boot_cpu_hartid
boot_cpu_hartid:
    .space 8

/* Saved on every switch_to_user; restored by the exit syscall handler. */
.global saved_kernel_ra
saved_kernel_ra:
    .space 8
.global saved_kernel_sp
saved_kernel_sp:
    .space 8
