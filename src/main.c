/* Feature-test macros, before any include: expose POSIX.1-2008 (clock_gettime,
   nanosleep) under strict -std=c11, while keeping ISO C library additions like
   snprintf visible on macOS, whose headers gate them on the requested level.
   _DEFAULT_SOURCE covers glibc; _DARWIN_C_SOURCE covers macOS. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE 1

#include "canvas.h"
#include "framebuffer.h"
#include "render.h"
#include "snake.h"
#include "term.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
        /* Reads are non-blocking, so idle here instead of spinning the CPU.
           ~15ms is a responsive poll for resize/quit without any real clock. */
        struct timespec idle = {0, 15 * 1000000L};
        nanosleep(&idle, NULL);
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

/* --- Phase 1 demo: an animated scene driven by the diffing renderer. --- */

#define TARGET_FPS 60
#define NS_PER_SEC 1000000000LL

/* Monotonic nanosecond clock for frame pacing and benchmarking. */
static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * NS_PER_SEC + ts.tv_nsec;
}

/* Sleep until `deadline` (a now_ns() value), so frames land ~1/TARGET_FPS
   apart. If we are already past the deadline, return immediately. */
static void sleep_until(int64_t deadline) {
    int64_t remaining = deadline - now_ns();
    if (remaining <= 0) {
        return;
    }
    struct timespec ts = {(time_t)(remaining / NS_PER_SEC),
                          (long)(remaining % NS_PER_SEC)};
    nanosleep(&ts, NULL);
}

/* Write a plain (uncolored) string into the canvas starting at (x, y). */
static void put_text(Canvas *canvas, int x, int y, const char *text) {
    for (int i = 0; text[i] != '\0'; ++i) {
        canvas_put(canvas, x + i, y, text[i], color_rgb(220, 220, 220));
    }
}

/* Draw one animation frame into `canvas`: a dim gradient backdrop with a
   solid box bouncing across it. `frame` advances the box; the moving box is
   what exercises the diff (only its leading/trailing edges change). */
static void draw_scene(Canvas *canvas, int frame) {
    int w = canvas->width;
    int h = canvas->height;

    /* Backdrop: a static horizontal gradient in cool blues. */
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double t = w > 1 ? (double)x / (w - 1) : 0.0;
            Color bg = color_rgb((unsigned char)(20 + t * 20),
                                 (unsigned char)(20 + t * 40),
                                 (unsigned char)(60 + t * 120));
            canvas_put(canvas, x, y, '.', bg);
        }
    }

    /* A bouncing box. Reflect its position off the walls using a triangle wave
       over the available travel, so it never leaves the canvas. */
    int bw = 10, bh = 5;
    int span_x = (w - bw) > 0 ? (w - bw) : 1;
    int span_y = (h - bh) > 0 ? (h - bh) : 1;
    int px = frame % (2 * span_x);
    int py = (frame / 2) % (2 * span_y);
    if (px >= span_x) {
        px = 2 * span_x - px;
    }
    if (py >= span_y) {
        py = 2 * span_y - py;
    }

    Color box = color_rgb(255, 180, 40);
    for (int y = 0; y < bh; ++y) {
        for (int x = 0; x < bw; ++x) {
            canvas_put(canvas, px + x, py + y, '#', box);
        }
    }
}

static int run_animation(void) {
    if (term_init() == -1) {
        fprintf(stderr, "error: stdin/stdout must be a terminal\n");
        return 1;
    }

    TermSize size = term_size();
    Renderer *renderer = renderer_create(size.cols, size.rows);
    if (renderer == NULL) {
        fprintf(stderr, "error: failed to allocate renderer\n");
        return 1;
    }

    int64_t start = now_ns();
    int64_t frame_ns = NS_PER_SEC / TARGET_FPS;
    int64_t total_bytes = 0;
    int frame = 0;
    int running = 1;

    while (running) {
        int64_t deadline = start + (int64_t)(frame + 1) * frame_ns;

        if (term_resized()) {
            size = term_size();
            renderer_resize(renderer, size.cols, size.rows);
        }

        Canvas *canvas = renderer_canvas(renderer);
        draw_scene(canvas, frame);

        /* Live HUD: elapsed FPS and bytes emitted last frame. */
        double elapsed = (double)(now_ns() - start) / NS_PER_SEC;
        double fps = elapsed > 0.0 ? frame / elapsed : 0.0;
        char hud[80];
        snprintf(hud, sizeof(hud), " frame %d  %.1f fps  q to quit ", frame,
                 fps);
        put_text(canvas, 1, 0, hud);

        total_bytes += (int64_t)renderer_present(renderer, stdout);
        ++frame;

        sleep_until(deadline);

        unsigned char byte;
        int n = term_read_byte(&byte);
        if (n == -1) {
            running = 0;
        } else if (n == 1 && (byte == 'q' || byte == 'Q' || byte == KEY_CTRL_C)) {
            running = 0;
        }
    }

    double elapsed = (double)(now_ns() - start) / NS_PER_SEC;
    renderer_destroy(renderer);
    term_shutdown();

    /* Printed on the main screen after restore, so it survives the demo. */
    printf("rendered %d frames in %.2fs = %.1f fps\n", frame, elapsed,
           elapsed > 0.0 ? frame / elapsed : 0.0);
    printf("emitted %lld bytes total = %.0f bytes/frame\n",
           (long long)total_bytes,
           frame > 0 ? (double)total_bytes / frame : 0.0);
    return 0;
}

/* --- Phase 2 demo: load an image and print it with the half-block trick. --- */

static int run_image(const char *path) {
    Framebuffer *img = framebuffer_load(path);
    if (img == NULL) {
        fprintf(stderr, "error: could not load image '%s'\n", path);
        return 1;
    }

    /* Fit within the terminal, preserving aspect. Each text row is two pixel
       rows tall, so the height budget is (rows - 1) * 2 pixels; leave one row
       so the shell prompt does not scroll the top of the image away. */
    TermSize size = term_size();
    int max_w = size.cols;
    int max_h = (size.rows - 1) * 2;
    double sx = (double)max_w / img->width;
    double sy = (double)max_h / img->height;
    double scale = sx < sy ? sx : sy;
    int dw = (int)(img->width * scale);
    int dh = (int)(img->height * scale);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    Framebuffer *scaled = framebuffer_scaled(img, dw, dh);
    framebuffer_destroy(img);
    if (scaled == NULL) {
        fprintf(stderr, "error: could not scale image\n");
        return 1;
    }

    framebuffer_render_halfblocks(scaled, stdout);
    framebuffer_destroy(scaled);
    return 0;
}

/* --- Phase 3 demo: Snake, driven by input + the diffing renderer. --- */

#define SNAKE_TICKS_PER_SEC 10

/* Drain all bytes currently buffered on stdin into `buf` (reads are
   non-blocking, so this never stalls). Returns the count read. */
static int drain_input(unsigned char *buf, int max) {
    int n = 0;
    unsigned char byte;
    while (n < max && term_read_byte(&byte) == 1) {
        buf[n++] = byte;
    }
    return n;
}

/* Paint a grid cell as two terminal columns wide (so cells look square despite
   the ~2:1 cell aspect), offset by one row to leave the HUD on row 0. */
static void put_grid_cell(Canvas *c, int gx, int gy, char ch, Color color) {
    canvas_put(c, gx * 2, gy + 1, ch, color);
    canvas_put(c, gx * 2 + 1, gy + 1, ch, color);
}

/* Draw the whole game frame into the renderer's canvas: HUD, food, and snake,
   plus a game-over overlay when dead. */
static void draw_game(Canvas *c, const Snake *s) {
    canvas_clear(c, ' ');

    char hud[80];
    snprintf(hud, sizeof(hud), " snake  score %d   wasd/arrows to steer, q quits ",
             s->score);
    put_text(c, 1, 0, hud);

    put_grid_cell(c, s->food.x, s->food.y, '@', color_rgb(230, 60, 60));

    for (int i = 0; i < s->length; ++i) {
        /* Head brighter than the body for legibility. */
        Color col = (i == 0) ? color_rgb(180, 255, 140) : color_rgb(60, 200, 90);
        put_grid_cell(c, s->body[i].x, s->body[i].y, '#', col);
    }

    if (!s->alive) {
        char over[80];
        snprintf(over, sizeof(over), " GAME OVER - score %d - r restarts, q quits ",
                 s->score);
        put_text(c, (c->width - (int)strlen(over)) / 2, c->height / 2, over);
    }
}

static int run_snake(void) {
    if (term_init() == -1) {
        fprintf(stderr, "error: stdin/stdout must be a terminal\n");
        return 1;
    }

    TermSize size = term_size();
    Renderer *renderer = renderer_create(size.cols, size.rows);
    if (renderer == NULL) {
        fprintf(stderr, "error: failed to allocate renderer\n");
        return 1;
    }

    /* Grid is half as wide (2 columns per cell) and one row shorter (HUD). */
    int gw = size.cols / 2;
    int gh = size.rows - 1;
    Snake *snake = snake_create(gw, gh, (unsigned)now_ns());
    if (snake == NULL) {
        fprintf(stderr, "error: terminal too small for a game\n");
        return 1;
    }

    int64_t start = now_ns();
    int64_t tick_ns = NS_PER_SEC / SNAKE_TICKS_PER_SEC;
    int64_t tick = 0;
    int running = 1;

    while (running) {
        int64_t deadline = start + (tick + 1) * tick_ns;

        /* Rebuild the field on resize (the grid dimensions change). */
        if (term_resized()) {
            size = term_size();
            renderer_resize(renderer, size.cols, size.rows);
            gw = size.cols / 2;
            gh = size.rows - 1;
            snake_destroy(snake);
            snake = snake_create(gw, gh, (unsigned)now_ns());
            if (snake == NULL) {
                running = 0;
                break;
            }
        }

        unsigned char buf[32];
        int n = drain_input(buf, (int)sizeof(buf));
        for (int i = 0; i < n; ++i) {
            unsigned char byte = buf[i];
            if (byte == 0x1b && i + 2 < n && buf[i + 1] == '[') {
                switch (buf[i + 2]) {
                    case 'A': snake_turn(snake, DIR_UP); break;
                    case 'B': snake_turn(snake, DIR_DOWN); break;
                    case 'C': snake_turn(snake, DIR_RIGHT); break;
                    case 'D': snake_turn(snake, DIR_LEFT); break;
                }
                i += 2;
            } else if (byte == 'q' || byte == 'Q' || byte == KEY_CTRL_C) {
                running = 0;
            } else if (byte == 'w' || byte == 'W') {
                snake_turn(snake, DIR_UP);
            } else if (byte == 's' || byte == 'S') {
                snake_turn(snake, DIR_DOWN);
            } else if (byte == 'a' || byte == 'A') {
                snake_turn(snake, DIR_LEFT);
            } else if (byte == 'd' || byte == 'D') {
                snake_turn(snake, DIR_RIGHT);
            } else if ((byte == 'r' || byte == 'R') && !snake->alive) {
                snake_destroy(snake);
                snake = snake_create(gw, gh, (unsigned)now_ns());
                if (snake == NULL) {
                    running = 0;
                    break;
                }
            }
        }
        if (!running) {
            break;
        }

        snake_step(snake); /* no-op while dead: the frame freezes on game over */

        draw_game(renderer_canvas(renderer), snake);
        renderer_present(renderer, stdout);

        ++tick;
        sleep_until(deadline);
    }

    int final_score = snake ? snake->score : 0;
    snake_destroy(snake);
    renderer_destroy(renderer);
    term_shutdown();

    printf("snake: final score %d\n", final_score);
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--gradient") == 0) {
        return run_gradient_demo();
    }
    if (argc > 1 && strcmp(argv[1], "--animate") == 0) {
        return run_animation();
    }
    if (argc > 1 && strcmp(argv[1], "--snake") == 0) {
        return run_snake();
    }
    if (argc > 2 && strcmp(argv[1], "--image") == 0) {
        return run_image(argv[2]);
    }
    return run_border_demo();
}
