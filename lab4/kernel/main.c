#include "mm.h"
#include "plic.h"
#include "shell.h"
#include "task.h"
#include "timer.h"
#include "uart.h"

#define SSTATUS_SIE (1ULL << 1)

/* ── Advanced Exercise 2: task queue test callbacks ─────────────────────── */

static void test_task_cb(void *arg) {
    uart_puts("[Task] priority=");
    uart_put_u64((uint64_t)(uintptr_t)arg);
    uart_putc('\n');
}

/* Runs at priority 2; adds a priority-9 task mid-execution to verify that
 * newly enqueued higher-priority tasks jump ahead of lower-priority ones. */
static void preempt_task_cb(void *arg) {
    (void)arg;
    uart_puts("[Task] priority=2 start — adding priority=9\n");
    add_task(test_task_cb, (void *)9UL, 9);
    uart_puts("[Task] priority=2 end\n");
}

/* Enqueue tasks out-of-priority-order; they run on the next interrupt.
 *
 * Expected output:
 *   [Task] priority=5          <- priority ordering
 *   [Task] priority=3
 *   [Task] priority=2 start — adding priority=9
 *   [Task] priority=2 end
 *   [Task] priority=9          <- preemption: 9 beats remaining 1
 *   [Task] priority=1
 *   [Task] priority=1
 */
static void test_task_queue(void) {
    /* Drain the TX ring buffer first so system-info output is fully sent
     * before we touch interrupts or the task queue. */
    uart_tx_flush();

    /*
     * Build the queue with SIE=0 so no THRE or timer interrupt can call
     * run_tasks between add_task calls and drain a partial queue.
     * uart_puts with SIE=0 uses the polling path (no ring buffer, no THRE),
     * so the "armed" message is sent directly and cannot trigger run_tasks.
     */
    asm volatile("csrc sstatus, %0" : : "r"(SSTATUS_SIE)); /* SIE=0 */
    uart_puts("[Test] task queue armed — tasks run on next interrupt\n");
    add_task(test_task_cb, (void *)1UL, 1);
    add_task(test_task_cb, (void *)5UL, 5);
    add_task(test_task_cb, (void *)3UL, 3);
    add_task(preempt_task_cb, 0, 2);
    add_task(test_task_cb, (void *)1UL, 1);
    asm volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE)); /* SIE=1 */
}

void start_kernel(void *dtb) {
    plic_init();
    uart_init(dtb);
    irq_enable();
    uart_enable_tx_irq();
    mm_init(dtb);
    timer_init(dtb);

    while (uart_getc() != '\n')
        ;

    uart_puts("\n================================\n");
    uart_puts("       OS Kernel  (Lab 4)\n");
    uart_puts("================================\n");
    print_system_info();

    test_task_queue();
    run_tasks(); /* drain test queue now; shell_run starts with empty queue */

    shell_run(dtb);
}
