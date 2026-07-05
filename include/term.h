#ifndef ASCII_RENDERER_TERM_H
#define ASCII_RENDERER_TERM_H

/* Terminal lifecycle: raw mode, alternate screen buffer, cursor visibility,
   size queries, and resize notification. The terminal is always restored on
   exit — normal return, exit(), or a fatal signal (SIGINT/SIGTERM/SIGSEGV/...).

   Only one terminal session may be active per process. */

typedef struct {
    int rows;
    int cols;
} TermSize;

/* Enter raw mode on the controlling terminal, switch to the alternate screen
   buffer, and hide the cursor. Registers atexit() and fatal-signal handlers
   so the terminal is restored no matter how the program dies.
   Returns 0 on success, -1 on failure (e.g. stdin is not a tty). */
int term_init(void);

/* Restore the original terminal state: show cursor, reset attributes, leave
   the alternate screen, and re-apply the saved termios settings. Safe to call
   more than once; also runs automatically at exit. */
void term_shutdown(void);

/* Query the current terminal size via ioctl(TIOCGWINSZ). Falls back to 80x24
   if the query fails. */
TermSize term_size(void);

/* Returns 1 (and clears the flag) if the terminal was resized (SIGWINCH)
   since the last call, 0 otherwise. */
int term_resized(void);

/* Poll stdin for one byte, waiting up to ~100ms. Returns 1 if a byte was
   read into *out, 0 on timeout, -1 on read error. */
int term_read_byte(unsigned char *out);

#endif /* ASCII_RENDERER_TERM_H */
