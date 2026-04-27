#ifndef UTILS_H
#define UTILS_H

/* Print message to UART and halt the CPU. Never returns. */
__attribute__((noreturn)) void panic(const char *msg);

#endif /* UTILS_H */
