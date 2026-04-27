#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

typedef struct timer_node {
    uint64_t expire; /* absolute rdtime tick when callback fires */
    void (*callback)(void *);
    void *arg;
    struct timer_node *next;
} timer_node_t;

/* Timebase frequency and time at kernel boot (defined in timer.c). */
extern uint64_t cpu_freq;
extern uint64_t boot_time;

/* Read timebase frequency from DTB, arm the first 2-second tick. */
void timer_init(const void *dtb);

/* Schedule callback(arg) to fire after 'sec' seconds. */
void add_timer(void (*cb)(void *), void *arg, int sec);

/* Fire all expired timers; reprogram the SBI timer for the next one. */
void run_expired_timers(void);

#endif /* TIMER_H */
