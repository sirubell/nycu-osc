#ifndef FDT_H
#define FDT_H

#define FDT_MAGIC 0xd00dfeed
#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE 0x00000002
#define FDT_PROP 0x00000003
#define FDT_NOP 0x00000004
#define FDT_END 0x00000009

struct fdt_header {
    unsigned int magic;
    unsigned int totalsize;
    unsigned int off_dt_struct;
    unsigned int off_dt_strings;
    unsigned int off_mem_rsvmap;
    unsigned int version;
    unsigned int last_comp_version;
    unsigned int boot_cpuid_phys;
    unsigned int size_dt_strings;
    unsigned int size_dt_struct;
};

struct fdt_prop {
    unsigned int len;
    unsigned int nameoff;
};

const void *fdt_getprop(const void *fdt, const char *path, const char *name,
                        int *lenp);
int fdt_path_offset(const void *fdt, const char *path);

#endif /* FDT_H */
