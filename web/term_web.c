/* Browser backend for term.h, used by the WebAssembly build (`make web`).

   There is no tty in the browser: the page hosts an xterm.js terminal and
   exposes a few hooks on the Emscripten Module object (see
   web/public/main.js):

     Module.termCols()        -> current terminal width in columns
     Module.termRows()        -> current terminal height in rows
     Module.termTakeResized() -> true once after each resize
     Module.termReadByte()    -> next queued input byte, or -1 if none

   Output needs no hook: the program writes ANSI to stdout as usual, and the
   page routes stdout bytes straight into xterm.js. The enter/leave sequences
   below go through stdout too, so they stay ordered with frame output.

   Raw mode and signal handlers have no browser equivalent and are not
   needed: xterm.js already delivers every keystroke unbuffered. The atexit()
   restore is kept, because demos (e.g. the border milestone) return from
   main() without calling term_shutdown() and rely on it to leave the
   alternate screen. The build uses -sEXIT_RUNTIME=1 so atexit handlers run. */

#include "term.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>

#define SEQ_ENTER "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H" /* alt screen, hide cursor, clear */
#define SEQ_LEAVE "\x1b[0m\x1b[?25h\x1b[?1049l"       /* reset attrs, show cursor, main screen */

EM_JS(int, js_term_cols, (void), {
    return Module.termCols ? Module.termCols() : 80;
});

EM_JS(int, js_term_rows, (void), {
    return Module.termRows ? Module.termRows() : 24;
});

EM_JS(int, js_term_take_resized, (void), {
    return Module.termTakeResized && Module.termTakeResized() ? 1 : 0;
});

EM_JS(int, js_term_read_byte, (void), {
    return Module.termReadByte ? Module.termReadByte() : -1;
});

static int g_term_active = 0;

int term_init(void) {
    if (g_term_active) {
        return 0;
    }
    g_term_active = 1;
    static int registered = 0;
    if (!registered) {
        registered = 1;
        atexit(term_shutdown);
    }
    fputs(SEQ_ENTER, stdout);
    fflush(stdout);
    return 0;
}

void term_shutdown(void) {
    if (!g_term_active) {
        return;
    }
    g_term_active = 0;
    fputs(SEQ_LEAVE, stdout);
    fflush(stdout);
}

TermSize term_size(void) {
    TermSize size = {24, 80};
    int cols = js_term_cols();
    int rows = js_term_rows();
    if (cols > 0 && rows > 0) {
        size.cols = cols;
        size.rows = rows;
    }
    return size;
}

int term_resized(void) {
    return js_term_take_resized();
}

int term_read_byte(unsigned char *out) {
    int byte = js_term_read_byte();
    if (byte < 0) {
        return 0; /* nothing queued: same as a non-blocking read timing out */
    }
    *out = (unsigned char)byte;
    return 1;
}
