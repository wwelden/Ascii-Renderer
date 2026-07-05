#include "canvas.h"
#include "term.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- Phase 0 milestone: enter raw mode, draw a border, exit cleanly. --- */

#define KEY_CTRL_C 0x03

/* Move the cursor to a 1-based (row, col) position. */
static void move_to(FILE *out, int row, int col) {
    fprintf(out, "\x1b[%d;%dH", row, col);
}

/* Write a centered string on the given row, clipped to the interior of the
   border (columns 2..cols-1). */
static void put_centered(FILE *out, int row, int cols, const char *text) {
    int len = (int)strlen(text);
    int interior = cols - 2;
    if (interior <= 0) {
        return;
    }
    if (len > interior) {
        len = interior;
    }
    move_to(out, row, 2 + (interior - len) / 2);
    fwrite(text, 1, (size_t)len, out);
}

static void draw_frame(TermSize size) {
    fputs("\x1b[2J", stdout); /* clear the (alternate) screen */

    /* Border: corners '+', horizontal '-', vertical '|'. */
    move_to(stdout, 1, 1);
    fputc('+', stdout);
    for (int x = 2; x < size.cols; ++x) {
        fputc('-', stdout);
    }
    fputc('+', stdout);

    for (int y = 2; y < size.rows; ++y) {
        move_to(stdout, y, 1);
        fputc('|', stdout);
        move_to(stdout, y, size.cols);
        fputc('|', stdout);
    }

    move_to(stdout, size.rows, 1);
    fputc('+', stdout);
    for (int x = 2; x < size.cols; ++x) {
        fputc('-', stdout);
    }
    fputc('+', stdout);

    char status[64];
    snprintf(status, sizeof(status), "%d x %d", size.cols, size.rows);
    put_centered(stdout, size.rows / 2, size.cols, "ascii-renderer / phase 0");
    put_centered(stdout, size.rows / 2 + 1, size.cols, status);
    put_centered(stdout, size.rows / 2 + 2, size.cols,
                 "resize the window; press q to quit");

    fflush(stdout);
}

static int run_border_demo(void) {
    if (term_init() == -1) {
        fprintf(stderr, "error: stdin/stdout must be a terminal\n");
        return 1;
    }

    draw_frame(term_size());

    for (;;) {
        if (term_resized()) {
            draw_frame(term_size());
        }
        unsigned char byte;
        int n = term_read_byte(&byte);
        if (n == -1) {
            return 1; /* atexit handler restores the terminal */
        }
        if (n == 1 && (byte == 'q' || byte == 'Q' || byte == KEY_CTRL_C)) {
            return 0;
        }
    }
}

/* --- Legacy demo: radial gradient rendered through the canvas API. --- */

#define CANVAS_WIDTH 320
#define CANVAS_HEIGHT 200

/* Map an intensity in [0, 1] to a black-body-style heat color:
   black -> red -> orange -> yellow -> white. */
static Color heat_color(double t) {
    if (t < 0.0) {
        t = 0.0;
    }
    if (t > 1.0) {
        t = 1.0;
    }
    double r = fmin(1.0, t * 3.0);
    double g = fmin(1.0, fmax(0.0, t * 3.0 - 1.0));
    double b = fmin(1.0, fmax(0.0, t * 3.0 - 2.0));
    return color_rgb((unsigned char)(r * 255.0),
                     (unsigned char)(g * 255.0),
                     (unsigned char)(b * 255.0));
}

static int run_gradient_demo(void) {
    Canvas *canvas = canvas_create(CANVAS_WIDTH, CANVAS_HEIGHT);
    if (canvas == NULL) {
        fprintf(stderr, "error: failed to allocate canvas\n");
        return 1;
    }

    /* Radial gradient: brightest at the center, fading to the edges. Intensity
       drives both the ASCII ramp character and a heat-palette color. Terminal
       cells are roughly twice as tall as wide, so scale the vertical axis to
       keep the falloff circular. */
    double cx = (CANVAS_WIDTH - 1) / 2.0;
    double cy = (CANVAS_HEIGHT - 1) / 2.0;
    double max_dist = sqrt(cx * cx + (cy * 2.0) * (cy * 2.0));

    for (int y = 0; y < CANVAS_HEIGHT; ++y) {
        for (int x = 0; x < CANVAS_WIDTH; ++x) {
            double dx = x - cx;
            double dy = (y - cy) * 2.0;
            double dist = sqrt(dx * dx + dy * dy);
            double intensity = 1.0 - dist / max_dist;
            canvas_put(canvas, x, y, canvas_ramp_char(intensity),
                       heat_color(intensity));
        }
    }

    canvas_render_color(canvas, stdout);
    canvas_destroy(canvas);
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--gradient") == 0) {
        return run_gradient_demo();
    }
    return run_border_demo();
}
