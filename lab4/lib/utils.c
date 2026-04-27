#include "utils.h"
#include "uart.h"

__attribute__((noreturn)) void panic(const char *msg) {
    uart_puts("[PANIC] ");
    uart_puts(msg);
    uart_puts("\n");
    for (;;)
        ;
}

uint64_t parse_u64(const char *s) {
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        uint64_t v = 0;
        for (; *s; s++) {
            char c = *s;
            if (c >= '0' && c <= '9')
                v = v * 16 + (uint64_t)(c - '0');
            else if (c >= 'a' && c <= 'f')
                v = v * 16 + (uint64_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                v = v * 16 + (uint64_t)(c - 'A' + 10);
            else
                break;
        }
        return v;
    }
    uint64_t v = 0;
    for (; *s >= '0' && *s <= '9'; s++)
        v = v * 10 + (uint64_t)(*s - '0');
    return v;
}

uint32_t hex_to_int(const char *s, int len) {
    uint32_t val = 0;
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

int count_digits(uint32_t n) {
    if (n == 0)
        return 1;
    int d = 0;
    while (n > 0) {
        d++;
        n /= 10;
    }
    return d;
}
