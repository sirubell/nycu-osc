#ifndef TASK_H
#define TASK_H

typedef void (*task_callback_t)(void *arg);

/* ── Task node (singly-linked, sorted by priority descending) ────────────── */
typedef struct task_node {
    task_callback_t callback;
    void *arg;
    int priority; /* higher value = higher priority */
    struct task_node *next;
} task_node_t;

/*
 * add_task – enqueue a task with the given priority.
 *
 * Inserted in descending priority order so dequeue always yields the
 * highest-priority pending task.
 * Requires mm_init() (uses allocate()).
 */
void add_task(task_callback_t callback, void *arg, int priority);

/*
 * run_tasks – drain the task queue, running each task with SIE=1
 * so that a higher-priority interrupt can preempt the current task.
 *
 * Called at the end of do_trap (Advanced Exercise 2).
 */
void run_tasks(void);

#endif /* TASK_H */
