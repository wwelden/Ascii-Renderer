#include "canvas.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Classic 70-level ramp (Paul Bourke), reversed so index grows with density. */
const char ascii_ramp[ASCII_RAMP_LEVELS + 1] =
    " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";

/* Catch any miscount of the ramp string at compile time. */
_Static_assert(sizeof(ascii_ramp) == ASCII_RAMP_LEVELS + 1,
               "ascii_ramp must contain exactly ASCII_RAMP_LEVELS characters");

Color color_rgb(unsigned char r, unsigned char g, unsigned char b) {
    Color color = {r, g, b};
    return color;
}

Canvas *canvas_create(int width, int height) {
    if (width <= 0 || height <= 0) {
        return NULL;
    }

    Canvas *canvas = malloc(sizeof(*canvas));
    if (canvas == NULL) {
        return NULL;
    }

    size_t count = (size_t)width * (size_t)height;
    canvas->width = width;
    canvas->height = height;
    canvas->cells = malloc(count);
    canvas->colors = malloc(count * sizeof(Color));
    if (canvas->cells == NULL || canvas->colors == NULL) {
        free(canvas->cells);
        free(canvas->colors);
        free(canvas);
        return NULL;
    }

    canvas_clear(canvas, ' ');
    memset(canvas->colors, 0xFF, count * sizeof(Color)); /* default to white */
    return canvas;
}

void canvas_destroy(Canvas *canvas) {
    if (canvas == NULL) {
        return;
    }
    free(canvas->cells);
    free(canvas->colors);
    free(canvas);
}

void canvas_clear(Canvas *canvas, char fill) {
    if (canvas == NULL) {
        return;
    }
    size_t count = (size_t)canvas->width * (size_t)canvas->height;
    for (size_t i = 0; i < count; ++i) {
        canvas->cells[i] = fill;
    }
}

void canvas_set(Canvas *canvas, int x, int y, char ch) {
    if (canvas == NULL) {
        return;
    }
    if (x < 0 || x >= canvas->width || y < 0 || y >= canvas->height) {
        return;
    }
    canvas->cells[(size_t)y * (size_t)canvas->width + (size_t)x] = ch;
}

char canvas_get(const Canvas *canvas, int x, int y) {
    if (canvas == NULL) {
        return '\0';
    }
    if (x < 0 || x >= canvas->width || y < 0 || y >= canvas->height) {
        return '\0';
    }
    return canvas->cells[(size_t)y * (size_t)canvas->width + (size_t)x];
}

void canvas_set_color(Canvas *canvas, int x, int y, Color color) {
    if (canvas == NULL) {
        return;
    }
    if (x < 0 || x >= canvas->width || y < 0 || y >= canvas->height) {
        return;
    }
    canvas->colors[(size_t)y * (size_t)canvas->width + (size_t)x] = color;
}

void canvas_put(Canvas *canvas, int x, int y, char ch, Color color) {
    canvas_set(canvas, x, y, ch);
    canvas_set_color(canvas, x, y, color);
}

void canvas_draw_line(Canvas *canvas, int x0, int y0, int x1, int y1, char ch) {
    /* Bresenham's line algorithm, integer-only. */
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        canvas_set(canvas, x0, y0, ch);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void canvas_draw_rect(Canvas *canvas, int x, int y, int w, int h, char ch) {
    if (w <= 0 || h <= 0) {
        return;
    }
    int right = x + w - 1;
    int bottom = y + h - 1;
    canvas_draw_line(canvas, x, y, right, y, ch);
    canvas_draw_line(canvas, x, bottom, right, bottom, ch);
    canvas_draw_line(canvas, x, y, x, bottom, ch);
    canvas_draw_line(canvas, right, y, right, bottom, ch);
}

void canvas_fill_rect(Canvas *canvas, int x, int y, int w, int h, char ch) {
    for (int row = y; row < y + h; ++row) {
        for (int col = x; col < x + w; ++col) {
            canvas_set(canvas, col, row, ch);
        }
    }
}

char canvas_ramp_char(double intensity) {
    if (intensity <= 0.0) {
        return ascii_ramp[0];
    }
    if (intensity >= 1.0) {
        return ascii_ramp[ASCII_RAMP_LEVELS - 1];
    }
    int index = (int)(intensity * (ASCII_RAMP_LEVELS - 1) + 0.5);
    return ascii_ramp[index];
}

void canvas_shade(Canvas *canvas, int x, int y, double intensity) {
    canvas_set(canvas, x, y, canvas_ramp_char(intensity));
}

void canvas_render(const Canvas *canvas, FILE *out) {
    if (canvas == NULL || out == NULL) {
        return;
    }
    for (int y = 0; y < canvas->height; ++y) {
        const char *row = &canvas->cells[(size_t)y * (size_t)canvas->width];
        fwrite(row, 1, (size_t)canvas->width, out);
        fputc('\n', out);
    }
}

void canvas_render_color(const Canvas *canvas, FILE *out) {
    if (canvas == NULL || out == NULL) {
        return;
    }
    for (int y = 0; y < canvas->height; ++y) {
        /* Emit a color escape only when the color changes within the row, then
           reset at the end so the color never bleeds past the canvas. */
        int have_last = 0;
        Color last = {0, 0, 0};
        for (int x = 0; x < canvas->width; ++x) {
            size_t idx = (size_t)y * (size_t)canvas->width + (size_t)x;
            Color c = canvas->colors[idx];
            if (!have_last || c.r != last.r || c.g != last.g || c.b != last.b) {
                fprintf(out, "\x1b[38;2;%u;%u;%um", c.r, c.g, c.b);
                last = c;
                have_last = 1;
            }
            fputc(canvas->cells[idx], out);
        }
        fputs("\x1b[0m\n", out);
    }
}
