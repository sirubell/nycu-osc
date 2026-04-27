#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

/* Print message to UART and halt the CPU. Never returns. */
__attribute__((noreturn)) void panic(const char *msg);

/* Parse a decimal or 0x/0X-prefixed hex string to uint64_t. */
uint64_t parse_u64(const char *s);

/* Parse a fixed-length uppercase/lowercase hex string (no "0x" prefix). */
uint32_t hex_to_int(const char *s, int len);

/* Count decimal digits in n (returns 1 for n==0). */
int count_digits(uint32_t n);

#endif /* UTILS_H */
