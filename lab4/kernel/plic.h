#ifndef PLIC_H
#define PLIC_H

#include <stdint.h>

/* PLIC IRQ line used by the platform UART. */
#ifdef QEMU
#define PLIC_UART_IRQ 10
#else
#define PLIC_UART_IRQ 42
#endif

/* Set up PLIC priority, enable, and threshold for the boot hart. */
void plic_init(void);

/* Enable SEIE in sie and set sstatus.SIE.  Call after plic_init(). */
void irq_enable(void);

/* Claim the highest-priority pending IRQ from the PLIC. */
uint32_t plic_claim(void);

/* Signal completion of an IRQ to the PLIC. */
void plic_complete(uint32_t irq);

#endif /* PLIC_H */
