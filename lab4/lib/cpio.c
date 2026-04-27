#include "cpio.h"
#include "string.h"
#include "uart.h"
#include "utils.h"
#include <stddef.h>

/* CPIO newc format constants */
#define CPIO_MAGIC "070701"
#define CPIO_MAGIC_LEN 6
#define CPIO_HDR_SIZE 110 /* fixed header size in newc format */
#define CPIO_TRAILER "TRAILER!!!"

/* Strip a leading "./" so names stored as "./foo" appear as "foo". */
static const char *cpio_name(const char *raw) {
    return (raw[0] == '.' && raw[1] == '/') ? raw + 2 : raw;
}

void initrd_list(const void *base) {
    /* First pass: count entries (skip "." directory entry) */
    uint32_t count = 0;
    const char *ptr = (const char *)base;
    while (1) {
        struct cpio_newc_header *hdr = (struct cpio_newc_header *)ptr;
        if (strncmp(hdr->c_magic, CPIO_MAGIC, CPIO_MAGIC_LEN) != 0)
            break;
        uint32_t namesize = hex_to_int(hdr->c_namesize, 8);
        uint32_t filesize = hex_to_int(hdr->c_filesize, 8);
        const char *name = cpio_name(ptr + CPIO_HDR_SIZE);
        if (strcmp(ptr + CPIO_HDR_SIZE, CPIO_TRAILER) == 0)
            break;
        if (name[0] != '\0') /* skip the bare "." entry */
            count++;
        ptr += (CPIO_HDR_SIZE + namesize + 3) & ~3;
        ptr += (filesize + 3) & ~3;
    }

    uart_puts("Total ");
    uart_put_u64(count);
    uart_puts(" files.\n");

    /* Second pass: print size + name */
    ptr = (const char *)base;
    while (1) {
        struct cpio_newc_header *hdr = (struct cpio_newc_header *)ptr;
        if (strncmp(hdr->c_magic, CPIO_MAGIC, CPIO_MAGIC_LEN) != 0)
            break;
        uint32_t namesize = hex_to_int(hdr->c_namesize, 8);
        uint32_t filesize = hex_to_int(hdr->c_filesize, 8);
        const char *name = cpio_name(ptr + CPIO_HDR_SIZE);
        if (strcmp(ptr + CPIO_HDR_SIZE, CPIO_TRAILER) == 0)
            break;

        if (name[0] != '\0') {
            uart_put_u64(filesize);
            for (int i = count_digits(filesize); i < 8; i++)
                uart_putc(' ');
            uart_puts(name);
            uart_puts("\n");
        }

        ptr += (CPIO_HDR_SIZE + namesize + 3) & ~3;
        ptr += (filesize + 3) & ~3;
    }
}

void initrd_cat(const void *base, const char *filename) {
    const char *ptr = (const char *)base;
    while (1) {
        struct cpio_newc_header *hdr = (struct cpio_newc_header *)ptr;
        if (strncmp(hdr->c_magic, CPIO_MAGIC, CPIO_MAGIC_LEN) != 0) {
            uart_puts("Invalid CPIO magic\n");
            break;
        }

        uint32_t namesize = hex_to_int(hdr->c_namesize, 8);
        uint32_t filesize = hex_to_int(hdr->c_filesize, 8);
        const char *raw = ptr + CPIO_HDR_SIZE;
        const char *name = cpio_name(raw);

        if (strcmp(raw, CPIO_TRAILER) == 0) {
            uart_puts("File not found: ");
            uart_puts(filename);
            uart_puts("\n");
            break;
        }

        if (strcmp(name, filename) == 0) {
            const char *data = ptr + ((CPIO_HDR_SIZE + namesize + 3) & ~3);
            for (uint32_t i = 0; i < filesize; i++)
                uart_putc(data[i]);
            uart_puts("\n");
            return;
        }

        ptr += (CPIO_HDR_SIZE + namesize + 3) & ~3;
        ptr += (filesize + 3) & ~3;
    }
}

/* ── initrd_find ─────────────────────────────────────────────────────────── */

const void *initrd_find(const void *base, const char *filename,
                        unsigned int *sizep) {
    const char *ptr = (const char *)base;
    while (1) {
        struct cpio_newc_header *hdr = (struct cpio_newc_header *)ptr;
        if (strncmp(hdr->c_magic, CPIO_MAGIC, CPIO_MAGIC_LEN) != 0)
            break;

        uint32_t namesize = hex_to_int(hdr->c_namesize, 8);
        uint32_t filesize = hex_to_int(hdr->c_filesize, 8);
        const char *raw = ptr + CPIO_HDR_SIZE;
        const char *name = cpio_name(raw);

        if (strcmp(raw, CPIO_TRAILER) == 0)
            break;

        if (strcmp(name, filename) == 0) {
            if (sizep)
                *sizep = (unsigned int)filesize;
            return ptr + ((CPIO_HDR_SIZE + namesize + 3) & ~3);
        }

        ptr += (CPIO_HDR_SIZE + namesize + 3) & ~3;
        ptr += (filesize + 3) & ~3;
    }
    return NULL;
}
