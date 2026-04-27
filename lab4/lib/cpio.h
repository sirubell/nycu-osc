#ifndef CPIO_H
#define CPIO_H

struct cpio_newc_header {
    char c_magic[6];
    char c_ino[8];
    char c_mode[8];
    char c_uid[8];
    char c_gid[8];
    char c_nlink[8];
    char c_mtime[8];
    char c_filesize[8];
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesize[8];
    char c_check[8];
};

void initrd_list(const void *base);
void initrd_cat(const void *base, const char *filename);

/*
 * initrd_find – locate a file by name in a newc CPIO archive.
 *
 * Returns a pointer to the first byte of the file's data, or 0 if not
 * found.  If sizep is non-null, *sizep receives the file size in bytes.
 */
const void *initrd_find(const void *base, const char *filename,
                        unsigned int *sizep);

#endif /* CPIO_H */
