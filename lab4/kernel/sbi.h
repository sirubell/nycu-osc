#ifndef SBI_H
#define SBI_H

#include <stdint.h>

struct sbiret {
    int64_t error;
    int64_t value;
};

/* SBI Extension IDs. */
#define SBI_EID_LEGACY_TIMER 0x00
#define SBI_EID_TIMER 0x54494D45 /* "TIME" - not supported on all boards */
#define SBI_EID_LEGACY_SHUTDOWN 0x08
#define SBI_EID_BASE 0x10

/* Program the supervisor timer; also clears STIP. */
struct sbiret sbi_set_timer(uint64_t stime_value);

/* Halt the system via SBI. */
void sbi_shutdown(void);

/* SBI Base Extension queries. */
int64_t sbi_get_spec_version(void);
int64_t sbi_get_impl_id(void);
int64_t sbi_get_impl_version(void);
int64_t sbi_probe_extension(long ext_id);

/* Print SBI version/impl info and probe known extensions to UART. */
void sbi_print_info(void);

#endif /* SBI_H */
