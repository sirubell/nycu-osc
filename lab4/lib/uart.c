#include "uart.h"
#include "fdt.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Register access width differs by platform:
 *   QEMU virt:      8-bit access (standard NS16550 compatible)
 *   OrangePi RV2:   32-bit access required by the DesignWare APB UART
 *
 * Using a platform-specific typedef keeps all register reads/writes correct
 * without scattering #ifdefs throughout the code.  uart_base[N] automatically
 * computes (base + N * sizeof(uart_reg_t)), so on the board uart_base[5] gives
 * base + 20 (LSR at offset 0x14), matching the hardware register map.
 */
#ifdef QEMU
typedef unsigned char uart_reg_t;
#else
typedef unsigned int uart_reg_t;
#endif

volatile uart_reg_t *uart_base = NULL;

/* Line Status Register bits */
#define LSR_DR (1 << 0)   /* Data Ready:      RX FIFO has data */
#define LSR_TDRQ (1 << 5) /* TX Data Request: TX FIFO has room */

/* IER bits */
#define IER_RDA 0x01  /* Received Data Available interrupt        */
#define IER_THRE 0x02 /* Transmitter Holding Register Empty intr  */

/* MCR bits */
#define MCR_OUT2 0x08 /* routes UART IRQ line to interrupt controller */

/* sstatus CSR bits */
#define SSTATUS_SIE (1ULL << 1) /* Supervisor Interrupt Enable */

/* IIR[3:1] interrupt type (bit 0 = 0 means pending) */
#define IIR_THRE 0x02 /* THRE:             TX buffer drained      */
#define IIR_RDA 0x04  /* Received data     available              */
#define IIR_CTI 0x0C  /* Character timeout (also RX data)         */

/* IIR field masks */
#define IIR_INT_MASK 0x0F  /* lower nibble: pending flag + type bits  */
#define IIR_NO_INT 0x01    /* bit 0 == 1 → no interrupt pending       */
#define IIR_TYPE_MASK 0x0E /* bits [3:1] → interrupt source type      */

/* NS16550A register offsets (index into uart_base[]) */
#define UART_DR                                                                \
    0 /* Receive / Transmit data register (RHR on read, THR on write) */
#define UART_IER                                                               \
    1 /* Interrupt Enable Register                                    */
#define UART_IIR                                                               \
    2 /* Interrupt Identification Register                            */
#define UART_MCR                                                               \
    4 /* Modem Control Register                                       */
#define UART_LSR                                                               \
    5 /* Line Status Register                                         */

/* ── Ring buffers ────────────────────────────────────────────────────────── */

#define RX_BUF_SIZE 256
#define TX_BUF_SIZE 256

static char rx_buf[RX_BUF_SIZE];
static volatile uint8_t rx_head = 0; /* written by uart_isr */
static volatile uint8_t rx_tail = 0; /* read    by uart_getc */

static char tx_buf[TX_BUF_SIZE];
static volatile uint8_t tx_head = 0; /* written by uart_putc */
static volatile uint8_t tx_tail = 0; /* read    by uart_isr  */

/* 0 = polling TX (bootloader); 1 = interrupt-driven TX (kernel). */
static volatile int tx_irq_mode = 0;

void uart_enable_tx_irq(void) { tx_irq_mode = 1; }

void uart_tx_flush(void) {
    while (tx_head != tx_tail)
        ; /* wait for ring buffer to drain via THRE interrupts */
}

/* ── uart_init ───────────────────────────────────────────────────────────── */

void uart_init(const void *dtb) {
    int len = 0;
    uint64_t parsed_addr = 0;

    /* Try /soc/serial first, fall back to /soc/uart */
    const unsigned char *reg = fdt_getprop(dtb, "/soc/serial", "reg", &len);
    if (!reg)
        reg = fdt_getprop(dtb, "/soc/uart", "reg", &len);

    if (reg) {
        if (len >= 8)
            parsed_addr = fdt_be64(reg);
        else
            parsed_addr = fdt_be32(reg);
    }

    uart_base = (volatile uart_reg_t *)parsed_addr;

    /*
     * IER bit 0 (RDA)  – Received Data Available interrupt.
     * IER bit 6 (UUE)  – PXA non-standard: UART Unit Enable; must be
     *                    preserved with |= or the entire UART is disabled.
     * MCR bit 3 (OUT2) – routes the UART IRQ line to the PLIC on the board.
     */
    uart_base[UART_IER] |= IER_RDA;  /* enable RDA; preserve UUE (bit 6)  */
    uart_base[UART_MCR] |= MCR_OUT2; /* OUT2 → route IRQ to PLIC          */
}

uint64_t uart_get_base(void) { return (uint64_t)uart_base; }

/* ── uart_isr – called from handle_plic_interrupt ────────────────────────── */

void uart_isr(void) {
    uint8_t iir;

    /*
     * IIR (Interrupt Identification Register) layout — lower 4 bits:
     *
     *   bit  0     : interrupt-pending flag  (0 = pending, 1 = none)
     *   bits [3:1] : interrupt source type   (IIR_RDA, IIR_THRE, IIR_CTI …)
     *
     * We loop until bit 0 is 1, meaning no more interrupts are pending.
     * The upper 4 bits are FIFO-status flags we do not need here, so we
     * mask them off with IIR_INT_MASK before testing.
     */
    while (
        !((iir = (uint8_t)(uart_base[UART_IIR] & IIR_INT_MASK)) & IIR_NO_INT)) {
        uint8_t type = iir & IIR_TYPE_MASK; /* isolate the interrupt source */

        if (type == IIR_RDA || type == IIR_CTI) {
            /*
             * RX — Received Data Available or Character Timeout Indication.
             *
             * Both mean the hardware RX FIFO contains data we should read.
             * LSR_DR (Data Ready, bit 0 of LSR) stays set as long as the
             * FIFO is non-empty, so we drain every waiting byte in one go.
             *
             * rx_head is uint8_t, so incrementing past 255 wraps back to 0
             * automatically — no explicit modulo needed.
             */
            while (uart_base[UART_LSR] & LSR_DR)
                rx_buf[rx_head++] = (char)uart_base[UART_DR];

        } else if (type == IIR_THRE) {
            /*
             * TX — Transmitter Holding Register Empty.
             *
             * The hardware TX FIFO has room; push bytes from our ring buffer
             * while (a) the FIFO still has space (LSR_TDRQ) and
             *       (b) there is unsent data in the ring buffer (tail != head).
             *
             * tx_tail is uint8_t, so it wraps at 256 automatically.
             */
            while ((uart_base[UART_LSR] & LSR_TDRQ) && tx_tail != tx_head)
                uart_base[UART_DR] = (uart_reg_t)tx_buf[tx_tail++];

            /*
             * If the ring buffer is now empty, disarm the THRE interrupt.
             * The TX FIFO is always empty when idle, so leaving THRE enabled
             * with nothing to send would cause it to fire continuously
             * (interrupt storm). We re-arm it in uart_putc_raw when new data
             * is enqueued.
             */
            if (tx_tail == tx_head)
                uart_base[UART_IER] &= ~(uart_reg_t)IER_THRE;
        }
    }
}

/* ── uart_getc – blocking ring-buffer read ───────────────────────────────── */

char uart_getc(void) {
    while (rx_head == rx_tail)
        ;
    char c = rx_buf[rx_tail++]; /* uint8_t wraps at 256 */
    return c == '\r' ? '\n' : c;
}

char uart_getc_raw(void) {
    while (!(uart_base[UART_LSR] & LSR_DR))
        ;
    return (char)uart_base[UART_DR];
}

/* ── TX ──────────────────────────────────────────────────────────────────── */

static void uart_putc_raw(char c) {
    if (tx_irq_mode) {
        uint64_t sstatus;
        asm volatile("csrr %0, sstatus" : "=r"(sstatus));
        if (sstatus & SSTATUS_SIE) {
            /* Normal context (SIE=1): use interrupt-driven ring buffer.
             * Disable SIE for the write+arm to prevent re-entrant tx_head
             * modification from a timer callback calling uart_puts. */
            while ((uint8_t)(tx_head + 1) == tx_tail)
                ; /* spin until not full; THRE ISR drains while we wait */
            uint64_t prev;
            asm volatile("csrrc %0, sstatus, %1"
                         : "=r"(prev)
                         : "r"(SSTATUS_SIE));
            tx_buf[tx_head++] = c;
            uart_base[UART_IER] |= (uart_reg_t)IER_THRE;
            if (prev & SSTATUS_SIE)
                asm volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE));
        } else {
            /* ISR context (SIE=0): poll LSR directly.
             * Using the ring buffer here would deadlock if it is full because
             * the THRE interrupt cannot fire with SIE=0. */
            while (!(uart_base[UART_LSR] & LSR_TDRQ))
                ;
            uart_base[UART_DR] = (uart_reg_t)c;
        }
    } else {
        /* Polling path: used by bootloader (no interrupts). */
        while (!(uart_base[UART_LSR] & LSR_TDRQ))
            ;
        uart_base[UART_DR] = (uart_reg_t)c;
    }
}

/* Expand LF to CR+LF so terminals display newlines correctly. */
void uart_putc(char c) {
    if (c == '\n')
        uart_putc_raw('\r');
    uart_putc_raw(c);
}

void uart_puts(const char *s) {
    while (*s)
        uart_putc(*s++);
}

void uart_hex(uint64_t h) {
    uart_puts("0x");
    for (int c = 60; c >= 0; c -= 4) {
        uint64_t n = (h >> c) & 0xf;
        n += n > 9 ? 0x57 : '0';
        uart_putc((char)n);
    }
}

void uart_put_u64(uint64_t v) {
    if (v == 0) {
        uart_putc('0');
        return;
    }
    char buf[20];
    int i = 0;
    while (v > 0) {
        buf[i++] = '0' + (int)(v % 10);
        v /= 10;
    }
    for (int j = i - 1; j >= 0; j--)
        uart_putc(buf[j]);
}
