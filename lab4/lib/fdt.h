#ifndef FDT_H
#define FDT_H

#include <stdint.h>

#define FDT_MAGIC 0xd00dfeed
#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE 0x00000002
#define FDT_PROP 0x00000003
#define FDT_NOP 0x00000004
#define FDT_END 0x00000009

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

struct fdt_prop {
    uint32_t len;
    uint32_t nameoff;
};

/* Round n up to the next 4-byte boundary (DTB pads all data to 4 bytes). */
#define FDT_ALIGN4(n) (((n) + 3) & ~3)

/* ── Big-endian cell readers ──────────────────────────────────────────────────
 */

/* Read a 4-byte big-endian value from an unaligned byte pointer. */
static inline uint32_t fdt_be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | ((uint32_t)p[3]);
}

/* Read an 8-byte big-endian value from an unaligned byte pointer. */
static inline uint64_t fdt_be64(const unsigned char *p) {
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) | ((uint64_t)p[7]);
}

/* ── Node / property lookup ───────────────────────────────────────────────────
 */

const void *fdt_getprop(const void *fdt, const char *path, const char *name,
                        int *lenp);
int fdt_path_offset(const void *fdt, const char *path);

/* ── Chosen-node helpers ──────────────────────────────────────────────────────
 */

/*
 * Read a /chosen address property that may be either 32-bit (4 bytes) or
 * 64-bit (8 bytes) big-endian, as determined by the property length.
 *
 * DTB encodes addresses as one or two 32-bit "cells" (controlled by
 * #address-cells).  On 64-bit platforms (address-cells = 2) each address
 * is stored as 8 bytes; older/32-bit DTBs use 4 bytes.  len >= 8 detects
 * the 64-bit case.
 *
 * Returns 0 if the property does not exist.
 */
static inline uint64_t fdt_get_chosen_addr(const void *fdt, const char *name) {
    int len;
    const unsigned char *p =
        (const unsigned char *)fdt_getprop(fdt, "/chosen", name, &len);
    if (!p)
        return 0;
    return (len >= 8) ? fdt_be64(p) : fdt_be32(p);
}

/* ── Reserved-memory iteration ────────────────────────────────────────────────
 */

/*
 * Walk every child node of /reserved-memory that has a "reg" property and
 * invoke cb(start, size, data) for each one.
 */
typedef void (*fdt_region_cb)(const char *name, uint64_t start, uint64_t size,
                              void *data);
void fdt_foreach_reserved_memory(const void *fdt, fdt_region_cb cb, void *data);

/*
 * Walk every top-level /memory node and invoke cb(name, base, size, data)
 * for each (base, size) pair in its "reg" property.
 */
void fdt_foreach_memory(const void *fdt, fdt_region_cb cb, void *data);

#endif /* FDT_H */
