#include "utils.h"
#include "uart.h"

__attribute__((noreturn)) void panic(const char *msg) {
    uart_puts("[PANIC] ");
    uart_puts(msg);
    uart_puts("\n");
    for (;;)
        ;
}
