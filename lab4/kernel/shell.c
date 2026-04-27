#include "shell.h"
#include "cpio.h"
#include "fdt.h"
#include "mm.h"
#include "sbi.h"
#include "string.h"
#include "timer.h"
#include "uart.h"
#include "utils.h"

/* switch_to_user is implemented in start.S */
extern void switch_to_user(void *entry, void *user_sp);

/* ── User-mode stack size ───────────────────────────────────────────────── */
#define USER_STACK_SIZE 4096

#define SHELL_PROMPT "opi-rv2> "

/* Lifted to file scope so shell_reprint_prompt() can read them from an
 * interrupt context while shell_run() is blocked waiting for input. */
static char shell_buf[128];
static int shell_idx = 0;
static int shell_active = 0; /* 0 until shell_run() starts */

/* ── Async output helpers ───────────────────────────────────────────────── */

void shell_clear_line(void) {
    if (!shell_active)
        return;
    /* \r   : move cursor to start of line
     * \x1b[2K : ANSI "erase entire line" */
    uart_puts("\r\x1b[2K");
}

void shell_reprint_prompt(void) {
    if (!shell_active)
        return;
    uart_puts(SHELL_PROMPT);
    for (int i = 0; i < shell_idx; i++)
        uart_putc(shell_buf[i]);
}

/* ── exec helper: load raw binary from initramfs, jump to U-mode ─────────  */

static void cmd_exec(const char *filename, void *initrd_base) {
    if (!initrd_base) {
        uart_puts("initrd not found\n");
        return;
    }

    uint32_t size = 0;
    const void *data = initrd_find(initrd_base, filename, &size);
    if (!data) {
        uart_puts("File not found: ");
        uart_puts(filename);
        uart_puts("\n");
        return;
    }

    memcpy((void *)USER_LOAD_ADDR, data, (size_t)size);

    void *user_stack = allocate(USER_STACK_SIZE);
    if (!user_stack) {
        uart_puts("exec: out of memory for user stack\n");
        return;
    }
    void *user_sp = (char *)user_stack + USER_STACK_SIZE;

    uart_puts("exec: jumping to user mode, entry=");
    uart_hex(USER_LOAD_ADDR);
    uart_puts("\n");
    uart_tx_flush();

    switch_to_user((void *)USER_LOAD_ADDR, user_sp);
}

/* ── settimeout callback and command ────────────────────────────────────── */

static void print_message_cb(void *arg) {
    shell_clear_line();
    uart_puts((const char *)arg);
    uart_putc('\n');
    free(arg);
    shell_reprint_prompt();
}

static void cmd_set_timeout(const char *args, void *initrd_base) {
    (void)initrd_base;

    /* Parse leading integer (decimal or 0x hex) as seconds. */
    int seconds = (int)parse_u64(args);
    while (*args && *args != ' ')
        args++;
    if (*args == ' ')
        args++;

    /* Copy the remainder as the message string. */
    size_t len = strlen(args);
    char *msg = (char *)allocate(len + 1);
    if (!msg) {
        uart_puts("settimeout: out of memory\n");
        return;
    }
    memcpy(msg, args, len + 1);

    add_timer(print_message_cb, msg, seconds);

    uart_puts("settimeout: message \"");
    uart_puts(msg);
    uart_puts("\" in ");
    uart_put_u64((uint64_t)seconds);
    uart_puts(" second(s)\n");
}

/* ── print_system_info – shared by startup and 'info' command ───────────── */

void print_system_info(void) {
    uart_puts("System information:\n");
    extern uint64_t boot_cpu_hartid;
    uart_puts("  Boot hart ID: ");
    uart_put_u64(boot_cpu_hartid);
    uart_puts("\n");
    uart_puts("  Timebase frequency: ");
    uart_put_u64(cpu_freq);
    uart_puts(" Hz\n");
    sbi_print_info();
}

/* ── exec_command ───────────────────────────────────────────────────────── */

static void exec_command(const char *buffer, void *initrd_base) {
    if (strlen(buffer) == 0)
        return;

    if (strcmp(buffer, "help") == 0) {
        uart_puts("Available commands:\n");
        uart_puts("  help              - show all commands\n");
        uart_puts("  hello             - print 'Hello, world!'\n");
        uart_puts("  info              - show system and SBI information\n");
        uart_puts("  probe [EID]       - probe all known (or a specific) SBI "
                  "extension\n");
        uart_puts("  ls                - list files in initrd\n");
        uart_puts("  cat <file>        - print file content from initrd\n");
        uart_puts("  exec <file>       - load and run binary in U-mode\n");
        uart_puts("  settimeout N MSG  - print MSG after N seconds\n");
    } else if (strcmp(buffer, "hello") == 0) {
        uart_puts("Hello, world!\n");
    } else if (strcmp(buffer, "info") == 0) {
        print_system_info();
    } else if (strcmp(buffer, "probe") == 0) {
        sbi_print_info();
    } else if (strncmp(buffer, "probe ", 6) == 0) {
        long eid = (long)parse_u64(buffer + 6);
        uart_puts("EID ");
        uart_hex((uint64_t)eid);
        uart_puts(": ");
        uart_puts(sbi_probe_extension(eid) ? "available" : "not available");
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
    } else if (strncmp(buffer, "exec ", 5) == 0) {
        cmd_exec(buffer + 5, initrd_base);
    } else if (strncmp(buffer, "settimeout ", 11) == 0) {
        cmd_set_timeout(buffer + 11, initrd_base);
    } else {
        uart_puts("Unknown command: ");
        uart_puts(buffer);
        uart_puts("\n");
    }
}

/* ── shell_run ──────────────────────────────────────────────────────────── */

void shell_run(const void *dtb) {
    void *initrd_base = (void *)fdt_get_chosen_addr(dtb, "linux,initrd-start");

    shell_active = 1;
    uart_puts(SHELL_PROMPT);

    while (1) {
        char c = uart_getc();

        /* Skip ANSI escape sequences (arrow keys, Delete, etc.). */
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

        /* Ctrl+D → shutdown. */
        if (c == ASCII_EOT) {
            uart_puts("\n[EOF] System Halting...\n");
            sbi_shutdown();
            while (1)
                ;
        }
        /* Backspace / Delete */
        else if (c == ASCII_BS || c == ASCII_DEL) {
            if (shell_idx > 0) {
                shell_idx--;
                uart_puts("\b \b");
            }
        }
        /* Enter */
        else if (c == ASCII_LF || c == ASCII_CR) {
            shell_buf[shell_idx] = '\0';
            uart_putc('\n');
            exec_command(shell_buf, initrd_base);
            shell_idx = 0;
            uart_puts(SHELL_PROMPT);
        }
        /* Printable characters */
        else if (shell_idx < (int)sizeof(shell_buf) - 1) {
            shell_buf[shell_idx++] = c;
            uart_putc(c);
        }
    }
}
