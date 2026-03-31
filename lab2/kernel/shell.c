#include "shell.h"
#include "cpio.h"
#include "string.h"
#include "uart.h"

/* SBI Extension IDs */
#define SBI_EXT_SHUTDOWN 0x08
#define SBI_EXT_BASE 0x10

/* SBI Base Function IDs */
enum sbi_ext_base_fid {
    SBI_EXT_BASE_GET_SPEC_VERSION,
    SBI_EXT_BASE_GET_IMP_ID,
    SBI_EXT_BASE_GET_IMP_VERSION,
    SBI_EXT_BASE_PROBE_EXT,
};

struct sbiret {
    long error;
    long value;
};

static struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
                               unsigned long arg1, unsigned long arg2,
                               unsigned long arg3, unsigned long arg4,
                               unsigned long arg5) {
    struct sbiret ret;
    register unsigned long a0 asm("a0") = (unsigned long)arg0;
    register unsigned long a1 asm("a1") = (unsigned long)arg1;
    register unsigned long a2 asm("a2") = (unsigned long)arg2;
    register unsigned long a3 asm("a3") = (unsigned long)arg3;
    register unsigned long a4 asm("a4") = (unsigned long)arg4;
    register unsigned long a5 asm("a5") = (unsigned long)arg5;
    register unsigned long a6 asm("a6") = (unsigned long)fid;
    register unsigned long a7 asm("a7") = (unsigned long)ext;
    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");
    ret.error = a0;
    ret.value = a1;
    return ret;
}

static void sbi_shutdown(void) {
    sbi_ecall(SBI_EXT_SHUTDOWN, 0, 0, 0, 0, 0, 0, 0);
}

static long sbi_get_spec_version(void) {
    struct sbiret ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_SPEC_VERSION,
                                  0, 0, 0, 0, 0, 0);
    return ret.value;
}

static long sbi_get_impl_id(void) {
    struct sbiret ret =
        sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_IMP_ID, 0, 0, 0, 0, 0, 0);
    return ret.value;
}

static long sbi_get_impl_version(void) {
    struct sbiret ret =
        sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_IMP_VERSION, 0, 0, 0, 0, 0, 0);
    return ret.value;
}

static void exec_command(const char *buffer, void *initrd_base) {
    if (strlen(buffer) == 0)
        return;

    if (strcmp(buffer, "help") == 0) {
        uart_puts("Available commands:\n");
        uart_puts("  help  - show all commands.\n");
        uart_puts("  hello - print 'Hello, world!'.\n");
        uart_puts("  info  - show system information.\n");
        uart_puts("  ls    - list files in initrd.\n");
        uart_puts("  cat   - print file content in initrd.\n");
    } else if (strcmp(buffer, "hello") == 0) {
        uart_puts("Hello, world!\n");
    } else if (strcmp(buffer, "info") == 0) {
        uart_puts("System information:\n");
        uart_puts("  SBI specification version: ");
        uart_hex(sbi_get_spec_version());
        uart_puts("\n");
        uart_puts("  SBI implementation ID: ");
        uart_hex(sbi_get_impl_id());
        uart_puts("\n");
        uart_puts("  SBI implementation version: ");
        uart_hex(sbi_get_impl_version());
        uart_puts("\n");
    } else if (strcmp(buffer, "ls") == 0) {
        if (initrd_base)
            initrd_list(initrd_base);
        else
            uart_puts("initrd not found\n");
    } else if (strncmp(buffer, "cat ", 4) == 0) {
        if (initrd_base)
            initrd_cat(initrd_base, buffer + 4);
        else
            uart_puts("initrd not found\n");
    } else {
        uart_puts("Unknown command: ");
        uart_puts(buffer);
        uart_puts("\n");
    }
}

void shell_run(void *initrd_base) {
    char buffer[128];
    int idx = 0;

    uart_puts("opi-rv2> ");

    while (1) {
        char c = uart_getc();

        /* Skip ANSI Escape Sequences (e.g., Arrow keys, Delete key) */
        if (c == ASCII_ESC) {
            if (uart_getc() == '[') {
                while (1) {
                    char tmp = uart_getc();
                    if ((tmp >= 'A' && tmp <= 'Z') ||
                        (tmp >= 'a' && tmp <= 'z') || tmp == '~')
                        break;
                }
            }
            continue;
        }

        /* Handle Ctrl+D (EOF) */
        if (c == ASCII_EOT) {
            uart_puts("\n[EOF] System Halting...\n");
            sbi_shutdown();
            while (1)
                ;
        }
        /* Handle Backspace or Delete */
        else if (c == ASCII_BS || c == ASCII_DEL) {
            if (idx > 0) {
                idx--;
                uart_puts("\b \b");
            }
        }
        /* Handle Enter (Newline) */
        else if (c == ASCII_LF || c == ASCII_CR) {
            buffer[idx] = '\0';
            uart_putc('\n');
            exec_command(buffer, initrd_base);
            idx = 0;
            uart_puts("opi-rv2> ");
        }
        /* Handle printable characters */
        else if (idx < (int)sizeof(buffer) - 1) {
            buffer[idx++] = c;
            uart_putc(c);
        }
    }
}
