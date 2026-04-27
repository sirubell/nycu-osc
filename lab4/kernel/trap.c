#include "trap.h"
#include "plic.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include <stdint.h>

/* Saved by switch_to_user in start.S; restored by the exit syscall. */
extern uint64_t saved_kernel_ra;
extern uint64_t saved_kernel_sp;

/* Interrupt codes (scause with the interrupt bit cleared). */
#define INT_TIMER 5
#define INT_EXTERNAL 9

/* Exception code for environment call from U-mode. */
#define EXC_ECALL_U 8

/* Linux-compatible exit syscall numbers. */
#define SYS_EXIT 93
#define SYS_EXIT_GROUP 94

/* sstatus CSR bits */
#define SSTATUS_SIE (1ULL << 1) /* Supervisor Interrupt Enable */
#define SSTATUS_SPP (1ULL << 8) /* Previous Privilege: set = S-mode */

/* scause MSB: set means interrupt, clear means exception */
#define SCAUSE_INT_BIT (1ULL << 63)

/* RISC-V base instruction size in bytes */
#define RISCV_INSN_SIZE 4

static void handle_timer_interrupt(void) { run_expired_timers(); }

static void handle_external_interrupt(void) {
    uint32_t irq = plic_claim();
    if (irq == PLIC_UART_IRQ)
        uart_isr();
    if (irq)
        plic_complete(irq);
}

static void handle_syscall(struct trap_frame *frame) {
    uart_puts("=== S-Mode trap ===\n");
    uart_puts("scause: ");
    uart_put_u64(frame->scause);
    uart_puts("\nsepc: ");
    uart_hex(frame->sepc);
    uart_puts("\nstval: ");
    uart_put_u64(frame->stval);
    uart_puts("\n");

    if (frame->a7 == SYS_EXIT || frame->a7 == SYS_EXIT_GROUP) {
        /* Restore kernel context so sret returns to the shell. */
        frame->sepc = saved_kernel_ra;
        frame->sp = saved_kernel_sp;
        frame->sstatus |= SSTATUS_SPP; /* SPP=1 → sret to S-mode */
        return;
    }

    frame->sepc += RISCV_INSN_SIZE;
}

/*
 * do_trap – C trap dispatcher called from handle_exception in start.S.
 *
 * Handlers may modify frame fields (sepc, sp, sstatus); the assembly epilogue
 * writes them back to the CSRs before sret executes.
 */
void do_trap(struct trap_frame *frame) {
    uint64_t scause = frame->scause;

    if (scause & SCAUSE_INT_BIT) {
        uint64_t code = scause & ~SCAUSE_INT_BIT;
        if (code == INT_TIMER)
            handle_timer_interrupt();
        else if (code == INT_EXTERNAL)
            handle_external_interrupt();
    } else {
        if (scause == EXC_ECALL_U)
            handle_syscall(frame);
        else {
            uart_puts("[Exception] sepc=");
            uart_hex(frame->sepc);
            uart_puts(" scause=");
            uart_hex(scause);
            uart_puts(" stval=");
            uart_hex(frame->stval);
            uart_puts("\n");
        }
    }

    /*
     * Run the deferred task queue with SIE=1 so higher-priority interrupts
     * can preempt running tasks (Advanced Exercise 2).
     */
    asm volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE));
    run_tasks();
    asm volatile("csrc sstatus, %0" : : "r"(SSTATUS_SIE));
}
