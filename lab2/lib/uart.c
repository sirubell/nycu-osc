#include "fdt.h"

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
#define LSR_DR   (1 << 0)  /* Data Ready: RX FIFO has data */
#define LSR_TDRQ (1 << 5)  /* TX Data Request: TX FIFO has room */

static unsigned long fdt64_to_cpu(const unsigned char *p) {
    return ((unsigned long)p[0] << 56) | ((unsigned long)p[1] << 48) |
           ((unsigned long)p[2] << 40) | ((unsigned long)p[3] << 32) |
           ((unsigned long)p[4] << 24) | ((unsigned long)p[5] << 16) |
           ((unsigned long)p[6] << 8)  | ((unsigned long)p[7]);
}

static unsigned int fdt32_to_cpu_local(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8)  | ((unsigned int)p[3]);
}

void uart_init(const void *dtb) {
    int len;
    unsigned long parsed_addr = 0;

    /* Try /soc/serial first, fall back to /soc/uart */
    const unsigned char *reg = fdt_getprop(dtb, "/soc/serial", "reg", &len);
    if (!reg)
        reg = fdt_getprop(dtb, "/soc/uart", "reg", &len);

    if (reg) {
        /* reg cells are big-endian; 8-byte cell = 64-bit address */
        if (len >= 8)
            parsed_addr = fdt64_to_cpu(reg);
        else
            parsed_addr = fdt32_to_cpu_local(reg);
    }

#ifdef QEMU
    /* QEMU virt machine exposes UART at a fixed address */
    if (parsed_addr == 0)
        parsed_addr = 0x10000000UL;
#else
    /*
     * The OrangePi RV2 DTB stores the bus address (0xa4017000).
     * The CPU sees the peripheral at the physical address (0xd4017000).
     * Translate, and fall back to the physical address if the DTB gives 0.
     */
    if (parsed_addr == 0xa4017000u)
        parsed_addr = 0xd4017000u;
    else if (parsed_addr == 0)
        parsed_addr = 0xd4017000u;
#endif

    uart_base = (volatile uart_reg_t *)parsed_addr;
}

unsigned long uart_get_base(void) { return (unsigned long)uart_base; }

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

void uart_hex(unsigned long h) {
    uart_puts("0x");
    unsigned long n;
    for (int c = 60; c >= 0; c -= 4) {
        n = (h >> c) & 0xf;
        n += n > 9 ? 0x57 : '0';
        uart_putc(n);
    }
}
