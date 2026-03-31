#include "fdt.h"
#include "shell.h"
#include "uart.h"

static unsigned long get_current_pc(void) {
    unsigned long pc;
    __asm__ volatile("auipc %0, 0" : "=r"(pc));
    return pc;
}

static void *read_initrd_from_dtb(const void *dtb) {
    int len;
    const unsigned char *prop =
        fdt_getprop(dtb, "/chosen", "linux,initrd-start", &len);
    if (!prop || len < 4)
        return 0;
    unsigned long addr;
    if (len >= 8) {
        addr = ((unsigned long)prop[0] << 56) | ((unsigned long)prop[1] << 48) |
               ((unsigned long)prop[2] << 40) | ((unsigned long)prop[3] << 32) |
               ((unsigned long)prop[4] << 24) | ((unsigned long)prop[5] << 16) |
               ((unsigned long)prop[6] << 8)  | (unsigned long)prop[7];
    } else {
        addr = ((unsigned long)prop[0] << 24) | ((unsigned long)prop[1] << 16) |
               ((unsigned long)prop[2] << 8)  | (unsigned long)prop[3];
    }
    return (void *)addr;
}

void start_kernel(void *dtb) {
    uart_init(dtb);

    /* Wait for Enter: screen may not be connected yet when we boot.
     * Kernel sits here silently; user presses Enter in screen to trigger boot.
     */
    while (uart_getc() != '\n')
        ;

    /* Retrieve initrd address from DTB /chosen (written by U-Boot FIT image) */
    void *initrd_base = read_initrd_from_dtb(dtb);

    uart_puts("\n================================\n");
    uart_puts("          OS Kernel\n");
    uart_puts("================================\n");

    uart_puts("[LOG] Kernel executing at: ");
    uart_hex(get_current_pc());
    uart_puts("\n");

    uart_puts("[LOG] UART at: ");
    uart_hex(uart_get_base());
    uart_puts("\n");

    uart_puts("[LOG] Device Tree Blob at: ");
    uart_hex((unsigned long)dtb);
    uart_puts("\n");

    uart_puts("[LOG] Initramfs at: ");
    uart_hex((unsigned long)initrd_base);
    uart_puts("\n\n");

    shell_run(initrd_base);
}
