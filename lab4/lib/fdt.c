#include "fdt.h"
#include "string.h"

/* ── Internal helpers ─────────────────────────────────────────────────────────
 */

static const char *fdt_get_string(const void *fdt, uint32_t off) {
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;
    return (const char *)fdt +
           fdt_be32((const unsigned char *)&hdr->off_dt_strings) + off;
}

static int node_name_eq(const char *node_name, const char *search_name) {
    int len = 0;
    while (search_name[len] && search_name[len] != '@')
        len++;
    if (strncmp(node_name, search_name, len) == 0)
        if (node_name[len] == '\0' || node_name[len] == '@')
            return 1;
    return 0;
}

/* ── fdt_path_offset ──────────────────────────────────────────────────────────
 */

int fdt_path_offset(const void *fdt, const char *path) {
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;
    if (fdt_be32((const unsigned char *)&hdr->magic) != FDT_MAGIC)
        return -1;

    const char *p = (const char *)fdt +
                    fdt_be32((const unsigned char *)&hdr->off_dt_struct);
    const char *path_ptr = path;

    if (*path_ptr == '/')
        path_ptr++;
    if (*path_ptr == '\0')
        return (int)(p - (const char *)fdt);

    int depth = 0;
    int current_depth_match = 0;

    while (1) {
        uint32_t tag = fdt_be32((const unsigned char *)p);
        p += 4;

        if (tag == FDT_BEGIN_NODE) {
            const char *name = p;
            p += FDT_ALIGN4(strlen(name) + 1);
            depth++;

            if (depth == 1 && name[0] == '\0' && current_depth_match == 0) {
                current_depth_match = 1;
                continue;
            }

            if (depth == current_depth_match + 1) {
                const char *next_slash = path_ptr;
                while (*next_slash && *next_slash != '/')
                    next_slash++;
                int segment_len = next_slash - path_ptr;

                char segment[64];
                for (int i = 0; i < segment_len && i < 63; i++)
                    segment[i] = path_ptr[i];
                segment[segment_len < 63 ? segment_len : 63] = '\0';

                if (node_name_eq(name, segment)) {
                    current_depth_match++;
                    path_ptr = next_slash;
                    if (*path_ptr == '/')
                        path_ptr++;
                    if (*path_ptr == '\0')
                        return (int)((p - FDT_ALIGN4(strlen(name) + 1) - 4) -
                                     (const char *)fdt);
                }
            }
        } else if (tag == FDT_END_NODE) {
            if (depth == current_depth_match)
                current_depth_match--;
            depth--;
        } else if (tag == FDT_PROP) {
            uint32_t len = fdt_be32((const unsigned char *)p);
            p += 8 + FDT_ALIGN4(len);
        } else if (tag == FDT_NOP) {
            continue;
        } else if (tag == FDT_END) {
            break;
        }
    }

    return -1;
}

/* ── fdt_getprop ──────────────────────────────────────────────────────────────
 */

const void *fdt_getprop(const void *fdt, const char *path, const char *name,
                        int *lenp) {
    int offset = fdt_path_offset(fdt, path);
    if (offset < 0)
        return 0;

    const char *p = (const char *)fdt + offset;
    if (fdt_be32((const unsigned char *)p) != FDT_BEGIN_NODE)
        return 0;

    const char *node_name = p + 4;
    p += 4 + FDT_ALIGN4(strlen(node_name) + 1);

    while (1) {
        uint32_t tag = fdt_be32((const unsigned char *)p);
        p += 4;

        if (tag == FDT_BEGIN_NODE) {
            /* Skip subnode */
            int depth = 1;
            p += FDT_ALIGN4(strlen(p) + 1);
            while (depth > 0) {
                tag = fdt_be32((const unsigned char *)p);
                p += 4;
                if (tag == FDT_BEGIN_NODE) {
                    p += FDT_ALIGN4(strlen(p) + 1);
                    depth++;
                } else if (tag == FDT_END_NODE) {
                    depth--;
                } else if (tag == FDT_PROP) {
                    uint32_t len = fdt_be32((const unsigned char *)p);
                    p += 8 + FDT_ALIGN4(len);
                }
            }
        } else if (tag == FDT_PROP) {
            uint32_t len = fdt_be32((const unsigned char *)p);
            uint32_t nameoff = fdt_be32((const unsigned char *)p + 4);
            const char *prop_name = fdt_get_string(fdt, nameoff);

            if (strcmp(prop_name, name) == 0) {
                if (lenp)
                    *lenp = (int)len;
                return p + 8;
            }
            p += 8 + FDT_ALIGN4(len);
        } else if (tag == FDT_END_NODE || tag == FDT_END) {
            break;
        } else if (tag == FDT_NOP) {
            continue;
        }
    }

    return 0;
}

/* ── fdt_foreach_memory ───────────────────────────────────────────────────────
 */

void fdt_foreach_memory(const void *fdt, fdt_region_cb cb, void *data) {
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;
    if (fdt_be32((const unsigned char *)&hdr->magic) != FDT_MAGIC)
        return;

    const char *strings = (const char *)fdt +
                          fdt_be32((const unsigned char *)&hdr->off_dt_strings);
    const char *p = (const char *)fdt +
                    fdt_be32((const unsigned char *)&hdr->off_dt_struct);

    int depth = 0;
    const char *cur_name = "";

    while (1) {
        uint32_t tag = fdt_be32((const unsigned char *)p);
        p += 4;

        if (tag == FDT_BEGIN_NODE) {
            const char *name = p;
            p += FDT_ALIGN4(strlen(name) + 1);
            depth++;
            if (depth == 2)
                cur_name = name;

        } else if (tag == FDT_END_NODE) {
            depth--;

        } else if (tag == FDT_PROP) {
            uint32_t plen = fdt_be32((const unsigned char *)p);
            uint32_t nameoff = fdt_be32((const unsigned char *)p + 4);
            const unsigned char *pdata = (const unsigned char *)p + 8;
            p += 8 + FDT_ALIGN4(plen);

            /* Only process "reg" properties of top-level "memory" nodes. */
            if (depth == 2 && node_name_eq(cur_name, "memory") &&
                strcmp(strings + nameoff, "reg") == 0) {
                int n =
                    (int)plen / 16; /* 8-byte base + 8-byte size per entry */
                for (int i = 0; i < n; i++) {
                    uint64_t base = fdt_be64(pdata + i * 16);
                    uint64_t size = fdt_be64(pdata + i * 16 + 8);
                    if (size > 0)
                        cb(cur_name, base, size, data);
                }
            }

        } else if (tag == FDT_NOP) {
            continue;
        } else {
            break;
        }
    }
}

/* ── fdt_foreach_reserved_memory ──────────────────────────────────────────────
 */

void fdt_foreach_reserved_memory(const void *fdt, fdt_region_cb cb,
                                 void *data) {
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;
    if (fdt_be32((const unsigned char *)&hdr->magic) != FDT_MAGIC)
        return;

    const char *strings = (const char *)fdt +
                          fdt_be32((const unsigned char *)&hdr->off_dt_strings);
    const char *p = (const char *)fdt +
                    fdt_be32((const unsigned char *)&hdr->off_dt_struct);

    int depth = 0;
    int resmem_depth = -1;
    const char *cur_name = "";

    while (1) {
        uint32_t tag = fdt_be32((const unsigned char *)p);
        p += 4;

        if (tag == FDT_BEGIN_NODE) {
            const char *name = p;
            p += FDT_ALIGN4(strlen(name) + 1);
            depth++;

            if (resmem_depth < 0 && depth == 2 &&
                strcmp(name, "reserved-memory") == 0)
                resmem_depth = depth;

            if (resmem_depth >= 0 && depth == resmem_depth + 1)
                cur_name = name;

        } else if (tag == FDT_END_NODE) {
            if (resmem_depth >= 0 && depth == resmem_depth)
                resmem_depth = -1;
            depth--;

        } else if (tag == FDT_PROP) {
            uint32_t plen = fdt_be32((const unsigned char *)p);
            uint32_t nameoff = fdt_be32((const unsigned char *)p + 4);
            const unsigned char *pdata = (const unsigned char *)p + 8;
            p += 8 + FDT_ALIGN4(plen);

            if (resmem_depth >= 0 && depth > resmem_depth &&
                strcmp(strings + nameoff, "reg") == 0 && plen >= 16) {
                uint64_t s = fdt_be64(pdata);
                uint64_t sz = fdt_be64(pdata + 8);
                if (sz > 0)
                    cb(cur_name, s, sz, data);
            }

        } else if (tag == FDT_NOP) {
            continue;
        } else {
            break;
        }
    }
}
