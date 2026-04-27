#include "fdt.h"
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

volatile uart_reg_t *uart_base = 0;

/* Line Status Register bits */
#define LSR_DR (1 << 0)   /* Data Ready: RX FIFO has data */
#define LSR_TDRQ (1 << 5) /* TX Data Request: TX FIFO has room */

void uart_init(const void *dtb) {
    int len;
    uint64_t parsed_addr = 0;

    /* Try /soc/serial first, fall back to /soc/uart */
    const unsigned char *reg = fdt_getprop(dtb, "/soc/serial", "reg", &len);
    if (!reg)
        reg = fdt_getprop(dtb, "/soc/uart", "reg", &len);

    if (reg) {
        /* reg cells are big-endian; 8-byte cell = 64-bit address */
        if (len >= 8)
            parsed_addr = fdt_be64(reg);
        else
            parsed_addr = fdt_be32(reg);
    }

    uart_base = (volatile uart_reg_t *)parsed_addr;
}

uint64_t uart_get_base(void) { return (uint64_t)uart_base; }

/* Raw read — no character translation, used for binary UART protocol. */
char uart_getc_raw(void) {
    while (!(uart_base[5] & LSR_DR))
        ;
    return (char)uart_base[0];
}

/* Interactive read — converts CR to LF for terminal compatibility. */
char uart_getc(void) {
    char c = uart_getc_raw();
    return c == '\r' ? '\n' : c;
}

static void uart_putc_raw(char c) {
    while (!(uart_base[5] & LSR_TDRQ))
        ;
    uart_base[0] = c;
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
    uint64_t n;
    for (int c = 60; c >= 0; c -= 4) {
        n = (h >> c) & 0xf;
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
