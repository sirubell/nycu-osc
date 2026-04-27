#include "task.h"
#include "mm.h"

#define SSTATUS_SIE (1ULL << 1) /* Supervisor Interrupt Enable */

static task_node_t *task_queue_head = NULL;

/* ── add_task ───────────────────────────────────────────────────────────── */

void add_task(task_callback_t callback, void *arg, int priority) {
    task_node_t *node = (task_node_t *)allocate(sizeof(task_node_t));
    if (!node)
        return;

    node->callback = callback;
    node->arg = arg;
    node->priority = priority;
    node->next = NULL;

    /* Insert in descending priority order (highest priority at head). */
    if (!task_queue_head || priority > task_queue_head->priority) {
        node->next = task_queue_head;
        task_queue_head = node;
    } else {
        task_node_t *cur = task_queue_head;
        while (cur->next && cur->next->priority >= priority)
            cur = cur->next;
        node->next = cur->next;
        cur->next = node;
    }
}

/* ── run_tasks ──────────────────────────────────────────────────────────── */

/*
 * Drain the entire task queue.  Each task runs with SIE=1 so that a
 * higher-priority interrupt can arrive, enqueue a new task, and be
 * processed in the next iteration.
 *
 * Called at the end of do_trap with SIE already disabled (default trap
 * entry state).  We toggle SIE around each task body.
 */
/*
 * Guard against re-entrant calls.  THRE interrupts fire during a task's
 * uart_puts (SIE=1), which would otherwise trigger a nested run_tasks and
 * start a new task mid-output of the current one.  With the guard, nested
 * callers return immediately; the outer while-loop picks up any newly-added
 * high-priority tasks at the next iteration — satisfying the spec's
 * "check priority before returning to the previous interrupt handler."
 */
static volatile int in_run_tasks = 0;

void run_tasks(void) {
    if (in_run_tasks)
        return;
    in_run_tasks = 1;

    uint64_t prev;
    asm volatile("csrr %0, sstatus" : "=r"(prev));

    while (task_queue_head) {
        task_node_t *t = task_queue_head;
        task_queue_head = t->next;

        /* Enable SIE while running the task so a higher-priority interrupt
         * can fire and enqueue a new task; the while loop picks it up at
         * the next iteration. */
        asm volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE));
        if (t->callback)
            t->callback(t->arg);
        asm volatile("csrc sstatus, %0" : : "r"(SSTATUS_SIE));

        free(t);
    }

    /* Restore SIE to what it was on entry so run_tasks is safe to call
     * from any context (interrupt handler or normal S-mode code). */
    if (prev & SSTATUS_SIE)
        asm volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE));

    in_run_tasks = 0;
}
