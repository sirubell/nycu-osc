#include "cpio.h"
#include "string.h"
#include "uart.h"

static unsigned int hex_to_int(const char *s, int len) {
    unsigned int val = 0;
    for (int i = 0; i < len; i++) {
        val <<= 4;
        if (s[i] >= '0' && s[i] <= '9')
            val += s[i] - '0';
        else if (s[i] >= 'A' && s[i] <= 'F')
            val += s[i] - 'A' + 10;
        else if (s[i] >= 'a' && s[i] <= 'f')
            val += s[i] - 'a' + 10;
    }
    return val;
}

static void uart_dec(unsigned int n) {
    if (n == 0) {
        uart_putc('0');
        return;
    }
    char buf[10];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    for (int j = i - 1; j >= 0; j--)
        uart_putc(buf[j]);
}

static int count_digits(unsigned int n) {
    if (n == 0)
        return 1;
    int d = 0;
    while (n > 0) {
        d++;
        n /= 10;
    }
    return d;
}

void initrd_list(const void *base) {
    /* First pass: count entries */
    unsigned int count = 0;
    const char *ptr = (const char *)base;
    while (1) {
        struct cpio_newc_header *hdr = (struct cpio_newc_header *)ptr;
        if (strncmp(hdr->c_magic, "070701", 6) != 0)
            break;
        unsigned int namesize = hex_to_int(hdr->c_namesize, 8);
        unsigned int filesize = hex_to_int(hdr->c_filesize, 8);
        const char *name = ptr + 110;
        if (strcmp(name, "TRAILER!!!") == 0)
            break;
        count++;
        ptr += (110 + namesize + 3) & ~3;
        ptr += (filesize + 3) & ~3;
    }

    uart_puts("Total ");
    uart_dec(count);
    uart_puts(" files.\n");

    /* Second pass: print size (left-aligned, 16-char column) + name */
    ptr = (const char *)base;
    while (1) {
        struct cpio_newc_header *hdr = (struct cpio_newc_header *)ptr;
        if (strncmp(hdr->c_magic, "070701", 6) != 0)
            break;
        unsigned int namesize = hex_to_int(hdr->c_namesize, 8);
        unsigned int filesize = hex_to_int(hdr->c_filesize, 8);
        const char *name = ptr + 110;
        if (strcmp(name, "TRAILER!!!") == 0)
            break;

        uart_dec(filesize);
        for (int i = count_digits(filesize); i < 8; i++)
            uart_putc(' ');
        uart_puts(name);
        uart_puts("\n");

        ptr += (110 + namesize + 3) & ~3;
        ptr += (filesize + 3) & ~3;
    }
}

void initrd_cat(const void *base, const char *filename) {
    const char *ptr = (const char *)base;
    while (1) {
        struct cpio_newc_header *hdr = (struct cpio_newc_header *)ptr;
        if (strncmp(hdr->c_magic, "070701", 6) != 0) {
            uart_puts("Invalid CPIO magic\n");
            break;
        }

        unsigned int namesize = hex_to_int(hdr->c_namesize, 8);
        unsigned int filesize = hex_to_int(hdr->c_filesize, 8);
        const char *name = ptr + 110;

        if (strcmp(name, "TRAILER!!!") == 0) {
            uart_puts("File not found: ");
            uart_puts(filename);
            uart_puts("\n");
            break;
        }

        if (strcmp(name, filename) == 0) {
            const char *data = ptr + ((110 + namesize + 3) & ~3);
            for (unsigned int i = 0; i < filesize; i++)
                uart_putc(data[i]);
            uart_puts("\n");
            return;
        }

        ptr += (110 + namesize + 3) & ~3;
        ptr += (filesize + 3) & ~3;
    }
}
