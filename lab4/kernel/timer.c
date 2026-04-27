#include "timer.h"
#include "fdt.h"
#include "mm.h"
#include "sbi.h"
#include "shell.h"
#include "uart.h"

uint64_t cpu_freq = 0;
uint64_t boot_time = 0;

static timer_node_t *timer_head = NULL;

static uint64_t read_time(void) {
    uint64_t t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

/* ── add_timer ──────────────────────────────────────────────────────────── */

void add_timer(void (*cb)(void *), void *arg, int sec) {
    timer_node_t *node = (timer_node_t *)allocate(sizeof(timer_node_t));
    if (!node)
        return;

    uint64_t now = read_time();
    node->expire = now + (uint64_t)sec * cpu_freq;
    node->callback = cb;
    node->arg = arg;
    node->next = NULL;

    if (!timer_head || node->expire < timer_head->expire) {
        node->next = timer_head;
        timer_head = node;
        sbi_set_timer(timer_head->expire);
    } else {
        timer_node_t *cur = timer_head;
        while (cur->next && cur->next->expire <= node->expire)
            cur = cur->next;
        node->next = cur->next;
        cur->next = node;
    }
}

/* ── run_expired_timers ─────────────────────────────────────────────────── */

void run_expired_timers(void) {
    uint64_t now = read_time();

    while (timer_head && timer_head->expire <= now) {
        timer_node_t *t = timer_head;
        timer_head = t->next;
        t->callback(t->arg);
        free(t);
    }

    if (timer_head)
        sbi_set_timer(timer_head->expire);
    else
        sbi_set_timer((uint64_t)-1ULL); /* no pending timers; clear STIP */
}

/* ── Periodic boot timer ────────────────────────────────────────────────── */

static void boot_timer_cb(void *arg) {
    (void)arg;
    uint64_t now = read_time();
    shell_clear_line();
    uart_puts("[Timer] seconds since boot: ");
    uart_put_u64((now - boot_time) / cpu_freq);
    uart_putc('\n');
    shell_reprint_prompt();
    add_timer(boot_timer_cb, 0, 2);
}

/* ── timer_init ─────────────────────────────────────────────────────────── */

/* sie.STIE bit position */
#define SIE_STIE (1ULL << 5)

/* Fallback timebase frequency (SpacemiT K1 default: 24 MHz). */
#define DEFAULT_CPU_FREQ 24000000ULL

void timer_init(const void *dtb) {
    int len = 0;
    const unsigned char *p = (const unsigned char *)fdt_getprop(
        dtb, "/cpus", "timebase-frequency", &len);
    if (p && len >= 4)
        cpu_freq = fdt_be32(p);
    else
        cpu_freq = DEFAULT_CPU_FREQ;

    boot_time = read_time();

    /*
     * Enable STIE only after the first timer is programmed.  Enabling it
     * earlier would trigger an interrupt storm from the leftover mtimecmp=0
     * that OpenSBI leaves at startup.
     */
    asm volatile("csrs sie, %0" : : "r"(SIE_STIE));
    add_timer(boot_timer_cb, 0, 2);
}
