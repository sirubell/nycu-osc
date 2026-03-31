#include "fdt.h"
#include "string.h"

static unsigned int fdt32_to_cpu(unsigned int val) {
    return ((val & 0xFF000000) >> 24) | ((val & 0x00FF0000) >> 8) |
           ((val & 0x0000FF00) << 8) | ((val & 0x000000FF) << 24);
}

static const char *fdt_get_string(const void *fdt, unsigned int off) {
    const struct fdt_header *hdr = fdt;
    return (const char *)fdt + fdt32_to_cpu(hdr->off_dt_strings) + off;
}

static int node_name_eq(const char *node_name, const char *search_name) {
    int len = 0;
    while (search_name[len] && search_name[len] != '@')
        len++;

    if (strncmp(node_name, search_name, len) == 0) {
        if (node_name[len] == '\0' || node_name[len] == '@') {
            return 1;
        }
    }
    return 0;
}

int fdt_path_offset(const void *fdt, const char *path) {
    const struct fdt_header *hdr = fdt;
    if (fdt32_to_cpu(hdr->magic) != FDT_MAGIC)
        return -1;

    const char *p = (const char *)fdt + fdt32_to_cpu(hdr->off_dt_struct);
    const char *path_ptr = path;

    if (*path_ptr == '/')
        path_ptr++;
    if (*path_ptr == '\0')
        return (int)((const char *)p - (const char *)fdt);

    int depth = 0;
    int current_depth_match = 0;

    while (1) {
        unsigned int tag = fdt32_to_cpu(*(unsigned int *)p);
        p += 4;

        if (tag == FDT_BEGIN_NODE) {
            const char *name = p;
            p += (strlen(name) + 1 + 3) & ~3;
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
                        return (
                            int)((const char *)(p -
                                                ((strlen(name) + 1 + 3) & ~3) -
                                                4) -
                                 (const char *)fdt);
                }
            }
        } else if (tag == FDT_END_NODE) {
            if (depth == current_depth_match)
                current_depth_match--;
            depth--;
        } else if (tag == FDT_PROP) {
            unsigned int len = fdt32_to_cpu(*(unsigned int *)p);
            p += 8;
            p += (len + 3) & ~3;
        } else if (tag == FDT_NOP) {
            continue;
        } else if (tag == FDT_END) {
            break;
        }
    }

    return -1;
}

const void *fdt_getprop(const void *fdt, const char *path, const char *name,
                        int *lenp) {
    int offset = fdt_path_offset(fdt, path);
    if (offset < 0)
        return 0;

    const char *p = (const char *)fdt + offset;
    unsigned int tag = fdt32_to_cpu(*(unsigned int *)p);
    if (tag != FDT_BEGIN_NODE)
        return 0;

    const char *node_name = p + 4;
    p += 4 + ((strlen(node_name) + 1 + 3) & ~3);

    while (1) {
        tag = fdt32_to_cpu(*(unsigned int *)p);
        p += 4;

        if (tag == FDT_BEGIN_NODE) {
            // Skip subnode
            int depth = 1;
            p += (strlen(p) + 1 + 3) & ~3;
            while (depth > 0) {
                tag = fdt32_to_cpu(*(unsigned int *)p);
                p += 4;
                if (tag == FDT_BEGIN_NODE) {
                    p += (strlen(p) + 1 + 3) & ~3;
                    depth++;
                } else if (tag == FDT_END_NODE) {
                    depth--;
                } else if (tag == FDT_PROP) {
                    unsigned int len = fdt32_to_cpu(*(unsigned int *)p);
                    p += 8 + ((len + 3) & ~3);
                }
            }
        } else if (tag == FDT_PROP) {
            unsigned int len = fdt32_to_cpu(*(unsigned int *)p);
            unsigned int nameoff = fdt32_to_cpu(*(unsigned int *)(p + 4));
            const char *prop_name = fdt_get_string(fdt, nameoff);

            if (strcmp(prop_name, name) == 0) {
                if (lenp)
                    *lenp = len;
                return p + 8;
            }
            p += 8 + ((len + 3) & ~3);
        } else if (tag == FDT_END_NODE || tag == FDT_END) {
            break;
        } else if (tag == FDT_NOP) {
            continue;
        }
    }

    return 0;
}
