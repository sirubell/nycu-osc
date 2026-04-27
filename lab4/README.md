[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/cXN49mJO)

# Lab 4 — Interrupts and Exception Handling

Implementation of interrupt-driven I/O, a software timer, deferred task execution, and user-mode program loading for the OrangePi RV2 (RISC-V) kernel.

---

## Basic Exercise 1 — UART Interrupt (30%)

Replace the polling UART driver with a fully interrupt-driven one so the CPU is free between keystrokes.

**Implementation:**

- `kernel/plic.c` initialises the Platform-Level Interrupt Controller (PLIC): sets IRQ priority to 1, enables the UART IRQ in the S-mode enable register for the boot hart, and clears the S-mode threshold so all priorities ≥ 1 are forwarded.
- `lib/uart.c` enables the NS16550A Received Data Available (RDA) interrupt in IER and asserts MCR.OUT2 to route the UART IRQ to the PLIC.
- `kernel/trap.c` dispatches PLIC external interrupts (scause interrupt code 9) to `uart_isr()`, which drains the hardware RX FIFO into a 256-byte ring buffer and feeds the TX ring buffer into the hardware TX FIFO on THRE interrupts.
- Ring buffer indices are `volatile uint8_t` so they wrap at 256 naturally; the full-check is `(uint8_t)(tx_head + 1) == tx_tail`, avoiding the modular-arithmetic errors that arise with wider integer types.
- `uart_putc_raw` detects ISR context by reading `sstatus.SIE`: when SIE=0 (inside a trap handler) it polls LSR directly instead of using the ring buffer, avoiding a deadlock when the ring buffer is full and THRE cannot fire.

---

## Basic Exercise 2 — Timer Interrupt (30%)

Implement a sorted one-shot timer queue driven by the SBI timer, with a periodic boot ticker and a `settimeout` shell command.

**Implementation:**

- `kernel/timer.c` reads `timebase-frequency` from the DTB `/cpus` node at runtime (fallback: 24 MHz for SpacemiT K1).
- `add_timer(cb, arg, sec)` allocates a `timer_node_t`, computes the absolute `rdtime` deadline, and inserts it into a singly-linked list sorted by expiry. `sbi_set_timer` is called with the earliest deadline whenever the head changes.
- `run_expired_timers()` is invoked from the timer interrupt handler (scause interrupt code 5). It pops and fires all nodes whose deadline has passed, then reprograms the SBI timer for the next pending node (or writes `UINT64_MAX` to suppress further interrupts when the queue is empty).
- STIE is enabled in `timer_init()` before `add_timer` (which internally calls `sbi_set_timer`). Any spurious timer interrupt from the stale `mtimecmp=0` left by OpenSBI is handled gracefully: `run_expired_timers()` finds no expired nodes and writes `UINT64_MAX` to suppress further interrupts.
- The SBI Timer Extension (EID 0x54494D45) is absent on the SpacemiT K1 despite the SBI 1.0 spec mandating it; the legacy EID 0x00 is used instead (confirmed at runtime via `sbi_probe_extension`).
- A boot ticker fires every 2 seconds and re-enqueues itself, printing seconds elapsed since boot.
- `settimeout N MSG` enqueues a one-shot timer to print MSG after N seconds.
- Both `boot_timer_cb` and `print_message_cb` (settimeout) call `shell_clear_line()` before printing and `shell_reprint_prompt()` after, so timer output never splits the shell prompt or truncates partially-typed input.

---

## Basic Exercise 3 — User-Mode Execution (20%)

Load and run raw binaries in unprivileged U-mode from the initramfs with a kernel return path via `sys_exit`.

**Implementation:**

- `USER_LOAD_ADDR` is defined in `kernel/mm.h` with platform-specific values (`0x80400000` for QEMU, `0x00400000` for the board). `mm_init()` calls `record_reserved("user-program", USER_LOAD_ADDR, USER_LOAD_ADDR + 0x200000)` so the buddy allocator never hands out pages in that 2 MiB region.
- `cmd_exec` in `kernel/shell.c` finds the file in the initramfs, copies it to `USER_LOAD_ADDR`, and allocates a 4 KiB user stack via the buddy allocator.
- `switch_to_user(entry, user_sp)` in `kernel/start.S`:
  1. Writes the entry point to `sepc`.
  2. Clears `sstatus.SPP` (return to U-mode) and sets `sstatus.SPIE` (interrupts enabled after `sret`).
  3. Saves the kernel `ra` and `sp` to BSS globals (`saved_kernel_ra`, `saved_kernel_sp`) so the exit syscall can resume the kernel.
  4. Stores the kernel `sp` in `sscratch` (used by `handle_exception` to detect U-mode traps) and switches to the user stack via `sret`.
- `handle_syscall` in `kernel/trap.c` handles `SYS_EXIT` (93) and `SYS_EXIT_GROUP` (94): it restores `sepc` and `sp` from the saved kernel context, sets `sstatus.SPP=1` to `sret` back to S-mode, and returns normally to the shell.
- `uart_tx_flush()` is called before `switch_to_user` to drain the interrupt-driven TX ring buffer so the "exec: jumping to user mode" line is fully visible before `sret` fires.

**sscratch protocol:**

| Value | Meaning |
|-------|---------|
| 0 | Currently in S-mode; `sp` is the kernel stack |
| non-zero | Currently in U-mode; value is the kernel `sp` for the next trap |

---

## Advanced Exercise 1 — Deferred Task Queue (10%)

Execute kernel work items deferred from interrupt context via a priority-sorted task queue.

**Implementation:**

- `kernel/task.c` maintains a singly-linked queue sorted in descending priority order.
- `add_task(cb, arg, priority)` inserts a `task_node_t` at the correct position in O(n).
- `run_tasks()` is called at the end of every `do_trap` invocation (after the primary interrupt handler). It pops and runs each task in priority order, freeing the node after the callback returns.

---

## Advanced Exercise 2 — Interrupt Preemption During Task Execution (10%)

Allow higher-priority interrupts to arrive and enqueue new tasks while the task queue is being drained.

**Implementation:**

- `run_tasks()` enables `sstatus.SIE` around each individual task callback so a higher-priority interrupt can fire mid-task, run `do_trap`, and enqueue new tasks.
- A static `in_run_tasks` flag prevents re-entrant calls: if a THRE or timer interrupt fires while a task is running (SIE=1), the nested `do_trap` calls `run_tasks` but returns immediately. The outer while-loop then picks up any newly-enqueued tasks at the next iteration, preserving priority ordering.
- SIE is cleared after each task returns and restored to its entry value after the queue is drained, making `run_tasks` safe to call from both interrupt and normal S-mode context.

---

## Trap Frame

`handle_exception` in `kernel/start.S` saves all 31 general-purpose registers plus `sepc`, `sstatus`, `scause`, and `stval` into a 280-byte frame on the kernel stack (35 slots × 8 bytes). The frame address is passed to `do_trap` as a `struct trap_frame *`.

```
slot  offset  register
  0      0    ra
  1      8    sp (original — user or kernel)
 2–3    16    gp, tp
 4–6    32    t0–t2
 7–8    56    s0–s1
9–16    72    a0–a7
17–26  136    s2–s11
27–30  216    t3–t6
  31   248    sepc
  32   256    sstatus
  33   264    scause
  34   272    stval
```

---

## SBI Usage

| Extension | EID | Available on SpacemiT K1 |
|-----------|-----|--------------------------|
| Legacy Timer | 0x00000000 | yes |
| Timer Extension | 0x54494D45 | no |
| Legacy Shutdown | 0x00000008 | yes |

SBI version and extension availability are reported at startup and via the `info` shell command.

---

## Boot and Init Flow

```
OpenSBI
  │  a0 = hartid,  a1 = pointer to Flattened Device Tree (FDT/DTB)
  ▼
kernel/start.S  _start
  │  clears BSS, saves hartid to boot_cpu_hartid
  │  installs handle_exception into stvec (direct mode)
  │  sets sscratch = 0 (S-mode marker)
  │  sets up kernel stack, tail-calls start_kernel(dtb)
  ▼
kernel/main.c  start_kernel()
  │  plic_init()          — PLIC priority/enable/threshold
  │  uart_init(dtb)       — UART base from DTB, enables RDA + OUT2
  │  irq_enable()         — sets sie.SEIE, sstatus.SIE
  │  uart_enable_tx_irq() — switches TX to interrupt-driven ring buffer
  │  mm_init(dtb)         — buddy system + chunk allocator
  │  timer_init(dtb)      — reads timebase-frequency, enables STIE,
  │                          schedules first 2-second boot ticker
  │  (waits for Enter key)
  │  print_system_info()  — hartid, timebase-frequency, SBI version,
  │                          extension probe results
  │  test_task_queue()    — enqueue priority-ordered tasks (Adv. Ex. 2 test)
  │  run_tasks()          — drain task queue before shell starts
  │  shell_run(dtb)
  ▼
interactive shell
```

---

## Shell Commands

| Command | Description |
|---------|-------------|
| `help` | List all commands |
| `hello` | Print `Hello, world!` |
| `info` | System info: hartid, timebase frequency, SBI version and extensions |
| `probe [EID]` | Probe all known SBI extensions, or a specific EID (decimal or `0x` hex) |
| `ls` | List files in initramfs |
| `cat <file>` | Print file content from initramfs |
| `exec <file>` | Load and run a raw binary in U-mode |
| `settimeout N MSG` | Print MSG after N seconds (N accepts `0x` hex) |
| Ctrl+D | Shutdown via SBI |

---

## How to Build and Run

### QEMU

```sh
make qemu
# QEMU prints: char device redirected to /dev/pts/X
```

In a second terminal, connect and send the kernel:

```sh
uv run send_kernel.py /dev/pts/X kernel/payload_os.bin
```

### OrangePi RV2 (Board)

```sh
make board        # builds kernel/payload_os.bin
# Press Reset on the board, then:
uv run send_kernel.py /dev/ttyUSB0 kernel/payload_os.bin
```

Press **Enter** in `screen` after the kernel loads to trigger the banner.

---

## Sample Output (OrangePi RV2)

```
================================
       OS Kernel  (Lab 4)
================================
System information:
  Boot hart ID: 0
  Timebase frequency: 24000000 Hz
  SBI specification version: 1.0
  SBI implementation: OpenSBI v1.7
  SBI extensions: Legacy Timer (0x00000000): yes, Timer Extension (0x54494D45): no, Legacy Shutdown (0x00000008): yes
opi-rv2> settimeout 3 hello
settimeout: message "hello" in 3 second(s)
opi-rv2> 
[Timer] seconds since boot: 2
opi-rv2> 
[Timer] seconds since boot: 4
opi-rv2> 
hello
opi-rv2> exec hello.elf
exec: jumping to user mode, entry=0x0000000000400000
Hello from user mode!
=== S-Mode trap ===
scause: 8
sepc: 0x000000000040005c
stval: 0
opi-rv2>
```

---

## Directory Structure

```
lab4/
├── Makefile                  # Top-level: make qemu / make board / make clean
│
├── bootloader/               # UART bootloader (unchanged from Lab 3)
│   ├── main.c
│   ├── start.S
│   ├── link.ld
│   ├── bootloader.its
│   ├── initramfs.cpio
│   ├── x1_orangepi-rv2.dtb
│   └── Makefile
│
├── kernel/                   # OS Kernel payload
│   ├── main.c                # Entry: plic_init, uart_init, mm_init, timer_init, shell
│   ├── start.S               # _start, handle_exception, switch_to_user
│   ├── trap.[ch]             # struct trap_frame, do_trap dispatcher
│   ├── plic.[ch]             # PLIC init, irq_enable, plic_claim/complete
│   ├── sbi.[ch]              # SBI ecall wrappers, sbi_print_info
│   ├── timer.[ch]            # Sorted timer queue, run_expired_timers, timer_init
│   ├── task.[ch]             # Priority-sorted deferred task queue
│   ├── shell.[ch]            # Interactive shell, exec (raw binary loader); shell_clear_line/reprint_prompt async helpers
│   ├── mm.[ch]               # Buddy system + chunk allocator; USER_LOAD_ADDR, record_reserved for user-program region
│   └── link.ld
│
├── lib/                      # Shared drivers and utilities
│   ├── uart.[ch]             # Interrupt-driven UART: RX/TX ring buffers, uart_isr
│   ├── fdt.[ch]              # FDT parser
│   ├── cpio.[ch]             # CPIO initramfs parser
│   ├── string.[ch]           # strcmp, strlen, memcpy, memset
│   ├── list.h                # Intrusive doubly-linked list
│   └── utils.[ch]            # panic(), parse_u64, hex_to_int, count_digits
│
└── send_kernel.py            # Host-side UART transfer script
```
