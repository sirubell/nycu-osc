#ifndef SHELL_H
#define SHELL_H

/* Print the same output as the 'info' command. */
void print_system_info(void);

void shell_run(const void *dtb);

/*
 * Async output helpers — call these around any message printed from an
 * interrupt context while the shell may be waiting for input.
 *
 * shell_clear_line()   : erase the current terminal line (prompt + partial
 *                        input) so the async message has a clean line.
 * shell_reprint_prompt(): redraw "opi-rv2> " and whatever the user had typed
 *                        so far, restoring the shell state visually.
 *
 * Both are no-ops before shell_run() is called.
 */
void shell_clear_line(void);
void shell_reprint_prompt(void);

#endif /* SHELL_H */
