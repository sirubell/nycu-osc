#include "sbi.h"
#include "uart.h"

/* SBI EID/FID constants not exposed in the public header. */
#define SBI_EID_BASE 0x10
#define SBI_FID_GET_SPEC_VERSION 0
#define SBI_FID_GET_IMPL_ID 1
#define SBI_FID_GET_IMPL_VERSION 2
#define SBI_FID_PROBE_EXTENSION 3

static struct sbiret sbi_ecall(int ext, int fid, uint64_t a0, uint64_t a1,
                               uint64_t a2, uint64_t a3, uint64_t a4,
                               uint64_t a5) {
    struct sbiret ret;
    register uint64_t r_a0 asm("a0") = a0;
    register uint64_t r_a1 asm("a1") = a1;
    register uint64_t r_a2 asm("a2") = a2;
    register uint64_t r_a3 asm("a3") = a3;
    register uint64_t r_a4 asm("a4") = a4;
    register uint64_t r_a5 asm("a5") = a5;
    register uint64_t r_a6 asm("a6") = (uint64_t)fid;
    register uint64_t r_a7 asm("a7") = (uint64_t)ext;
    asm volatile("ecall"
                 : "+r"(r_a0), "+r"(r_a1)
                 : "r"(r_a2), "r"(r_a3), "r"(r_a4), "r"(r_a5), "r"(r_a6),
                   "r"(r_a7)
                 : "memory");
    ret.error = (int64_t)r_a0;
    ret.value = (int64_t)r_a1;
    return ret;
}

struct sbiret sbi_set_timer(uint64_t stime_value) {
    /*
     * SpacemiT K1 OpenSBI does not register SBI_EID_TIMER (0x54494D45) even
     * though the spec version is 1.0; the legacy EID 0x00 works on all boards.
     */
    return sbi_ecall(SBI_EID_LEGACY_TIMER, 0, stime_value, 0, 0, 0, 0, 0);
}

void sbi_shutdown(void) {
    sbi_ecall(SBI_EID_LEGACY_SHUTDOWN, 0, 0, 0, 0, 0, 0, 0);
}

int64_t sbi_get_spec_version(void) {
    return sbi_ecall(SBI_EID_BASE, SBI_FID_GET_SPEC_VERSION, 0, 0, 0, 0, 0, 0)
        .value;
}

int64_t sbi_get_impl_id(void) {
    return sbi_ecall(SBI_EID_BASE, SBI_FID_GET_IMPL_ID, 0, 0, 0, 0, 0, 0).value;
}

int64_t sbi_get_impl_version(void) {
    return sbi_ecall(SBI_EID_BASE, SBI_FID_GET_IMPL_VERSION, 0, 0, 0, 0, 0, 0)
        .value;
}

int64_t sbi_probe_extension(long ext_id) {
    return sbi_ecall(SBI_EID_BASE, SBI_FID_PROBE_EXTENSION, (uint64_t)ext_id, 0,
                     0, 0, 0, 0)
        .value;
}

static const char *sbi_impl_name(int64_t id) {
    switch (id) {
    case 0:
        return "Berkeley Boot Loader (BBL)";
    case 1:
        return "OpenSBI";
    case 2:
        return "Xvisor";
    case 3:
        return "KVM";
    case 4:
        return "RustSBI";
    case 5:
        return "Diosix";
    case 6:
        return "Coffer";
    case 7:
        return "Xen Project";
    case 8:
        return "PolarFire Hart Software Services";
    case 9:
        return "coreboot";
    case 10:
        return "oreboot";
    case 11:
        return "bhyve";
    default:
        return "Unknown";
    }
}

void sbi_print_info(void) {
    int64_t spec = sbi_get_spec_version();
    int64_t impl_id = sbi_get_impl_id();
    int64_t impl_v = sbi_get_impl_version();

    uart_puts("  SBI specification version: ");
    uart_put_u64((uint64_t)((spec >> 24) & 0x7f));
    uart_putc('.');
    uart_put_u64((uint64_t)(spec & 0xffffff));
    uart_puts("\n");

    uart_puts("  SBI implementation: ");
    uart_puts(sbi_impl_name(impl_id));
    if (impl_id == 1) { /* OpenSBI encodes version as (major<<16)|minor */
        uart_puts(" v");
        uart_put_u64((uint64_t)((impl_v >> 16) & 0xffff));
        uart_putc('.');
        uart_put_u64((uint64_t)(impl_v & 0xffff));
    } else {
        uart_puts(" (version ");
        uart_hex((uint64_t)impl_v);
        uart_putc(')');
    }
    uart_puts("\n");

    uart_puts("  SBI extensions: ");
    uart_puts("Legacy Timer (0x00000000): ");
    uart_puts(sbi_probe_extension(SBI_EID_LEGACY_TIMER) ? "yes" : "no");
    uart_puts(", Timer Extension (0x54494D45): ");
    uart_puts(sbi_probe_extension(SBI_EID_TIMER) ? "yes" : "no");
    uart_puts(", Legacy Shutdown (0x00000008): ");
    uart_puts(sbi_probe_extension(SBI_EID_LEGACY_SHUTDOWN) ? "yes" : "no");
    uart_puts("\n");
}
