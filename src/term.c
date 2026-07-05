/* Feature-test macros, before any include: expose POSIX.1-2008 (sigaction)
   under strict -std=c11, while keeping the non-POSIX extensions this file
   needs (SIGWINCH, TIOCGWINSZ / struct winsize) visible.
   _DEFAULT_SOURCE covers glibc; _DARWIN_C_SOURCE covers macOS. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE 1

#include "term.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* Escape sequences for entering and leaving our screen state. The restore
   string is written from a signal handler, so it is a single constant that
   can be pushed out with one async-signal-safe write(). */
#define SEQ_ENTER "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H" /* alt screen, hide cursor, clear */
#define SEQ_LEAVE "\x1b[0m\x1b[?25h\x1b[?1049l"       /* reset attrs, show cursor, main screen */

static struct termios g_orig_termios;
static volatile sig_atomic_t g_term_active = 0;
static volatile sig_atomic_t g_resized = 0;

/* Restore the terminal using only async-signal-safe calls (write, tcsetattr),
   so it can run from a fatal-signal handler as well as from atexit(). */
static void restore_terminal(void) {
    if (!g_term_active) {
        return;
    }
    g_term_active = 0;
    /* Best-effort: if these fail there is nothing useful we can do. */
    ssize_t ignored = write(STDOUT_FILENO, SEQ_LEAVE, sizeof(SEQ_LEAVE) - 1);
    (void)ignored;
    /* TCSANOW, not TCSAFLUSH: the restore path must never block. TCSAFLUSH
       waits for pending output to drain, which hangs forever if the reader
       side has stalled (Ctrl-S flow control, dead ssh peer, ...). */
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
}

static void on_fatal_signal(int sig) {
    restore_terminal();
    /* Re-raise with the default action so the exit status still reflects the
       signal (and SIGSEGV etc. still produce a core dump where enabled). */
    signal(sig, SIG_DFL);
    raise(sig);
}

static void on_sigwinch(int sig) {
    (void)sig;
    g_resized = 1;
}

static int install_handlers(void) {
    static const int fatal_signals[] = {SIGINT,  SIGTERM, SIGHUP,
                                        SIGQUIT, SIGSEGV, SIGBUS,
                                        SIGFPE,  SIGILL,  SIGABRT};
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_fatal_signal;
    sigemptyset(&sa.sa_mask);
    for (size_t i = 0; i < sizeof(fatal_signals) / sizeof(fatal_signals[0]); ++i) {
        if (sigaction(fatal_signals[i], &sa, NULL) == -1) {
            return -1;
        }
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigwinch;
    sigemptyset(&sa.sa_mask);
    /* No SA_RESTART: a resize should interrupt a blocking read so the main
       loop can react promptly. */
    return sigaction(SIGWINCH, &sa, NULL);
}

int term_init(void) {
    if (g_term_active) {
        return 0;
    }
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return -1;
    }
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) {
        return -1;
    }

    struct termios raw = g_orig_termios;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(tcflag_t)OPOST;
    raw.c_cflag |= (tcflag_t)CS8;
    /* Poll-style reads: return after 100ms even with no input, so the main
       loop stays responsive to resize flags and can pace frames later. */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return -1;
    }

    g_term_active = 1;
    if (install_handlers() == -1 || atexit(term_shutdown) != 0) {
        restore_terminal();
        return -1;
    }

    ssize_t ignored = write(STDOUT_FILENO, SEQ_ENTER, sizeof(SEQ_ENTER) - 1);
    (void)ignored;
    return 0;
}

void term_shutdown(void) {
    restore_terminal();
}

TermSize term_size(void) {
    TermSize size = {24, 80};
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
        ws.ws_row > 0) {
        size.rows = ws.ws_row;
        size.cols = ws.ws_col;
    }
    return size;
}

int term_resized(void) {
    if (g_resized) {
        g_resized = 0;
        return 1;
    }
    return 0;
}

int term_read_byte(unsigned char *out) {
    ssize_t n = read(STDIN_FILENO, out, 1);
    if (n == 1) {
        return 1;
    }
    if (n == 0 || (n == -1 && errno == EINTR)) {
        return 0; /* timeout, or interrupted by a signal (e.g. SIGWINCH) */
    }
    return -1;
}
