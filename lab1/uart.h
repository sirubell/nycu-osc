#ifndef UART_H
#define UART_H

/* ASCII Constants */
#define ASCII_EOT 0x04 /* End of Transmission (Ctrl+D) */
#define ASCII_BS 0x08  /* Backspace */
#define ASCII_LF 0x0A  /* Line Feed (\n) */
#define ASCII_CR 0x0D  /* Carriage Return (\r) */
#define ASCII_ESC 0x1B /* Escape */
#define ASCII_DEL 0x7F /* Delete */

void uart_putc(char c);
char uart_getc(void);
void uart_puts(const char *s);
void uart_hex(unsigned long h);

#endif /* UART_H */
