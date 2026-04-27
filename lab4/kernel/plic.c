#include "plic.h"
#include <stdint.h>

/* Provided by start.S (.bss). */
extern uint64_t boot_cpu_hartid;

/* Platform-specific PLIC base address. */
#ifdef QEMU
#define PLIC_BASE 0x0c000000UL
#else
#define PLIC_BASE 0xe0000000UL
#endif

/*
 * Standard RISC-V PLIC register layout.
 *
 * S-mode context for hart h is context index (2*h + 1).
 * The enable register stride is 0x80 bytes per context; the base for context 1
 * (hart-0 S-mode) is PLIC_BASE + 0x2000 + 1*0x80 = PLIC_BASE + 0x002080.
 * For subsequent harts the per-hart stride is 2*0x80 = 0x100 bytes.
 *
 * The threshold/claim stride is 0x1000 bytes per context; the base for
 * context 1 is PLIC_BASE + 0x200000 + 1*0x1000 = PLIC_BASE + 0x201000.
 * Per-hart stride: 2*0x1000 = 0x2000 bytes.
 */

/* PLIC register map offsets (relative to PLIC_BASE) */
#define PLIC_PRIORITY_OFF 0x000000UL    /* priority regs: + irq*4          */
#define PLIC_S_ENABLE_OFF 0x002080UL    /* S-mode enable, hart 0 (ctx 1)   */
#define PLIC_S_ENABLE_STRIDE 0x000100UL /* per-hart stride (2 ctxs * 0x80) */
#define PLIC_S_THRESH_OFF 0x201000UL    /* S-mode threshold, hart 0        */
#define PLIC_S_THRESH_STRIDE                                                   \
    0x002000UL                      /* per-hart stride (2 ctxs * 0x1000)       \
                                     */
#define PLIC_S_CLAIM_OFF 0x000004UL /* claim/complete: threshold + 4   */

/* IRQ word/bit helpers for the enable bitmap */
#define PLIC_IRQ_WORD(irq) ((irq) >> 5)   /* 32-bit word index */
#define PLIC_IRQ_BIT(irq) ((irq) & 0x1Fu) /* bit within that word */

#define PLIC_PRIORITY(irq)                                                     \
    (*(volatile uint32_t *)(PLIC_BASE + PLIC_PRIORITY_OFF +                    \
                            (uint64_t)(irq) * 4UL))

#define PLIC_S_ENABLE(hart, word)                                              \
    (*(volatile uint32_t *)(PLIC_BASE + PLIC_S_ENABLE_OFF +                    \
                            (uint64_t)(hart) * PLIC_S_ENABLE_STRIDE +          \
                            (uint64_t)(word) * 4UL))

#define PLIC_S_THRESHOLD(hart)                                                 \
    (*(volatile uint32_t *)(PLIC_BASE + PLIC_S_THRESH_OFF +                    \
                            (uint64_t)(hart) * PLIC_S_THRESH_STRIDE))

#define PLIC_S_CLAIM(hart)                                                     \
    (*(volatile uint32_t *)(PLIC_BASE + PLIC_S_THRESH_OFF +                    \
                            (uint64_t)(hart) * PLIC_S_THRESH_STRIDE +          \
                            PLIC_S_CLAIM_OFF))

/* sie / sstatus bit positions */
#define SIE_SEIE (1ULL << 9)
#define SSTATUS_SIE (1ULL << 1)

void plic_init(void) {
    uint64_t hart = boot_cpu_hartid;

    PLIC_PRIORITY(PLIC_UART_IRQ) = 1; /* any non-zero priority enables it */
    PLIC_S_ENABLE(hart, PLIC_IRQ_WORD(PLIC_UART_IRQ)) |=
        (1u << PLIC_IRQ_BIT(PLIC_UART_IRQ));
    PLIC_S_THRESHOLD(hart) = 0; /* accept all priorities ≥ 1 */
}

uint32_t plic_claim(void) { return PLIC_S_CLAIM(boot_cpu_hartid); }

void plic_complete(uint32_t irq) { PLIC_S_CLAIM(boot_cpu_hartid) = irq; }

void irq_enable(void) {
    /*
     * Enable only SEIE here; STIE is enabled in timer_init() after the first
     * SBI timer is programmed.  Enabling STIE before sbi_set_timer() would
     * trigger an immediate interrupt storm because OpenSBI leaves mtimecmp=0
     * (already expired) on startup.
     */
    asm volatile("csrs sie, %0" : : "r"(SIE_SEIE));
    asm volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE));
}
