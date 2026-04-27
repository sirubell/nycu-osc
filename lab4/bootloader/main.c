#include "uart.h"

#ifdef QEMU
#define KERNEL_LOAD_ADDR 0x80200000UL
#else
#define KERNEL_LOAD_ADDR 0x00200000UL
#endif

static uint32_t recv_u32(void) {
    unsigned char b[4];
    for (int i = 0; i < 4; i++)
        b[i] = uart_getc_raw();
    return (uint32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

static void recv_bytes(unsigned char *dst, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        dst[i] = uart_getc_raw();
}

void start_kernel(unsigned long hartid, void *dtb) {
    uart_init(dtb);

    uart_puts("Waiting for BOOT\n");

    /* Scan the byte stream for BOOT magic: 'B' 'O' 'O' 'T' */
    while (1) {
        if (uart_getc_raw() != 0x42)
            continue;
        if (uart_getc_raw() != 0x4F)
            continue;
        if (uart_getc_raw() != 0x4F)
            continue;
        if (uart_getc_raw() != 0x54)
            continue;
        break;
    }

    uint32_t kernel_size = recv_u32();
    uart_puts("Loading kernel: ");
    uart_hex(kernel_size);
    uart_puts(" bytes -> ");
    uart_hex(KERNEL_LOAD_ADDR);
    uart_puts("\n");
    recv_bytes((unsigned char *)KERNEL_LOAD_ADDR, kernel_size);

    uart_puts("Jumping to kernel...\n");

    /* * 1. Hardware Data Barrier + Compiler Memory Barrier
     * Ensures all previous memory writes (like recv_bytes) are physically
     * completed in RAM and interconnects before proceeding.
     */
    __asm__ volatile("fence rw, rw" ::: "memory");

    /* * 2. Hardware Instruction Barrier + Compiler Memory Barrier
     * Flushes the CPU pipeline and invalidates the I-Cache so the CPU
     * is forced to fetch the fresh instructions we just wrote to RAM.
     */
    __asm__ volatile("fence.i" ::: "memory");

    void (*entry)(unsigned long, void *) =
        (void (*)(unsigned long, void *))KERNEL_LOAD_ADDR;
    entry(hartid, dtb);
}
