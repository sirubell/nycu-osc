#ifndef TRAP_H
#define TRAP_H

#include <stdint.h>

/*
 * Trap frame saved by handle_exception in start.S (35 slots × 8 bytes = 280).
 * The field order must match the sd/ld offsets in handle_exception exactly.
 *
 *  slot  offset   field
 *   0      0      ra
 *   1      8      sp   (original sp at trap time)
 *   2–3   16      gp, tp
 *   4–6   32      t0–t2
 *   7–8   56      s0–s1
 *   9–16  72      a0–a7
 *  17–26 136      s2–s11
 *  27–30 216      t3–t6
 *  31    248      sepc
 *  32    256      sstatus
 *  33    264      scause
 *  34    272      stval
 */
struct trap_frame {
    uint64_t ra, sp, gp, tp;
    uint64_t t0, t1, t2;
    uint64_t s0, s1;
    uint64_t a0, a1, a2, a3, a4, a5, a6, a7;
    uint64_t s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    uint64_t t3, t4, t5, t6;
    uint64_t sepc, sstatus, scause, stval;
};

/* C trap dispatcher called from handle_exception in start.S. */
void do_trap(struct trap_frame *frame);

#endif /* TRAP_H */
