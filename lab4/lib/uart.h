#ifndef UART_H
#define UART_H

#include <stdint.h>

/* ASCII Constants */
#define ASCII_EOT 0x04 /* End of Transmission (Ctrl+D) */
#define ASCII_BS 0x08  /* Backspace */
#define ASCII_LF 0x0A  /* Line Feed (\n) */
#define ASCII_CR 0x0D  /* Carriage Return (\r) */
#define ASCII_ESC 0x1B /* Escape */
#define ASCII_DEL 0x7F /* Delete */

void uart_init(const void *dtb);
void uart_putc(char c);
void uart_puts(const char *s);
char uart_getc_raw(void);
char uart_getc(void);
void uart_hex(uint64_t h);
void uart_put_u64(uint64_t v); /* decimal */
uint64_t uart_get_base(void);

/* Switch TX from polling to interrupt-driven ring buffer.
 * Call once after PLIC and global interrupts are enabled. */
void uart_enable_tx_irq(void);

/* Block until the TX ring buffer has been fully drained to hardware.
 * Call before switch_to_user() so buffered output appears before the
 * user program starts generating trap output via the polling path.    */
void uart_tx_flush(void);

/* Called from the PLIC interrupt handler (handle_plic_interrupt in main.c).
 * Handles both RX (data available) and TX (THRE) UART interrupts.         */
void uart_isr(void);

#endif /* UART_H */
