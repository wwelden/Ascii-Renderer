#define _POSIX_C_SOURCE 200809L

#include "term.h"
#include "test.h"

#include <unistd.h>

typedef struct {
    int saved_stdin;
    int saved_stdout;
    int input_writer;
    FILE *output;
} RedirectedIO;

/* Always use a pipe/file, regardless of whether the test runner has a tty. */
static RedirectedIO redirect_io(void) {
    RedirectedIO io;
    int input[2];
    CHECK(fflush(stdout) == 0);
    io.saved_stdin = dup(STDIN_FILENO);
    io.saved_stdout = dup(STDOUT_FILENO);
    CHECK(io.saved_stdin >= 0 && io.saved_stdout >= 0);
    CHECK(pipe(input) == 0);
    io.output = tmpfile();
    CHECK(io.output != NULL);
    CHECK(dup2(input[0], STDIN_FILENO) == STDIN_FILENO);
    CHECK(dup2(fileno(io.output), STDOUT_FILENO) == STDOUT_FILENO);
    CHECK(close(input[0]) == 0);
    io.input_writer = input[1];
    return io;
}

static void restore_io(RedirectedIO *io) {
    CHECK(fflush(stdout) == 0);
    CHECK(dup2(io->saved_stdin, STDIN_FILENO) == STDIN_FILENO);
    CHECK(dup2(io->saved_stdout, STDOUT_FILENO) == STDOUT_FILENO);
    CHECK(close(io->saved_stdin) == 0);
    CHECK(close(io->saved_stdout) == 0);
    if (io->input_writer >= 0) {
        CHECK(close(io->input_writer) == 0);
    }
    check_output(io->output, "");
    CHECK(fclose(io->output) == 0);
}

static void non_tty_session_is_rejected_without_output(void) {
    RedirectedIO io = redirect_io();
    CHECK(term_init() == -1);
    CHECK(term_init() == -1);
    term_shutdown();
    term_shutdown();
    restore_io(&io);
}

static void size_falls_back_when_output_is_not_a_terminal(void) {
    RedirectedIO io = redirect_io();
    TermSize size = term_size();
    CHECK(size.rows == 24);
    CHECK(size.cols == 80);
    CHECK(term_resized() == 0);
    CHECK(term_resized() == 0);
    restore_io(&io);
}

static void input_preserves_each_byte_and_reports_eof(void) {
    RedirectedIO io = redirect_io();
    const unsigned char bytes[] = {'q', 0x03, 0x1b, 0x00, 0xff};
    CHECK(write(io.input_writer, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes));
    CHECK(close(io.input_writer) == 0);
    io.input_writer = -1;
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        unsigned char byte = 0;
        CHECK(term_read_byte(&byte) == 1);
        CHECK(byte == bytes[i]);
    }
    unsigned char byte = 123;
    CHECK(term_read_byte(&byte) == 0);
    CHECK(byte == 123);
    restore_io(&io);
}

static void input_reports_read_errors(void) {
    RedirectedIO io = redirect_io();
    CHECK(close(STDIN_FILENO) == 0);
    unsigned char byte = 123;
    CHECK(term_read_byte(&byte) == -1);
    CHECK(byte == 123);
    restore_io(&io);
}

int main(void) {
    RUN(non_tty_session_is_rejected_without_output);
    RUN(size_falls_back_when_output_is_not_a_terminal);
    RUN(input_preserves_each_byte_and_reports_eof);
    RUN(input_reports_read_errors);
    return EXIT_SUCCESS;
}
