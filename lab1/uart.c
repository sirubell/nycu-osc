#ifdef QEMU
#define UART_BASE 0x10000000UL
#define UART_REG_WIDTH 1
#else
#define UART_BASE 0xd4017000UL
#define UART_REG_WIDTH 4
#endif

#define UART_RBR ((volatile unsigned char *)(UART_BASE + 0x00 * UART_REG_WIDTH))
#define UART_THR ((volatile unsigned char *)(UART_BASE + 0x00 * UART_REG_WIDTH))
#define UART_LSR ((volatile unsigned char *)(UART_BASE + 0x05 * UART_REG_WIDTH))

#define LSR_DR (1 << 0)
#define LSR_TDRQ (1 << 5)

char uart_getc(void) {
    while ((*UART_LSR & LSR_DR) == 0)
        ;
    char c = (char)(*UART_RBR);
    return c == '\r' ? '\n' : c;
}

void uart_putc(char c) {
    if (c == '\n') {
        uart_putc('\r');
    }
    while ((*UART_LSR & LSR_TDRQ) == 0)
        ;
    *UART_THR = c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_hex(unsigned long h) {
    uart_puts("0x");
    unsigned long n;
    for (int c = 60; c >= 0; c -= 4) {
        n = (h >> c) & 0xf;
        n += n > 9 ? 0x57 : '0';
        uart_putc(n);
    }
}
