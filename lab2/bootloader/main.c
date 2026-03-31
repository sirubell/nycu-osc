#include "fdt.h"
#include "string.h"
#include "uart.h"

#ifdef QEMU
#define KERNEL_LOAD_ADDR 0x80200000UL
#else
#define KERNEL_LOAD_ADDR 0x00200000UL
#endif

static unsigned long get_current_pc(void) {
    unsigned long pc;
    __asm__ volatile("auipc %0, 0" : "=r"(pc));
    return pc;
}

static unsigned int recv_u32(void) {
    unsigned char b[4];
    for (int i = 0; i < 4; i++)
        b[i] = uart_getc_raw();
    return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

static void recv_bytes(unsigned char *dst, unsigned int len) {
    for (unsigned int i = 0; i < len; i++)
        dst[i] = uart_getc_raw();
}

void start_kernel(void *dtb) {
    uart_init(dtb);

    uart_puts("[LOG] Bootloader executing at: ");
    uart_hex(get_current_pc());
    uart_puts("\n");

    uart_puts("\n================================\n");
    uart_puts("       UART Bootloader\n");
    uart_puts("================================\n");
    uart_puts("Waiting for BOOT header...\n");

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

    unsigned int kernel_size = recv_u32();
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

    /* Pass dtb; kernel reads initrd address from DTB /chosen */
    void (*entry)(void *) = (void (*)(void *))KERNEL_LOAD_ADDR;
    entry(dtb);
}
