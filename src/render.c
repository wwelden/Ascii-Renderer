#include "render.h"

#include <stdlib.h>
#include <string.h>

/* Worst-case escape sizes, used to pre-size output buffers so the per-frame
   emit path never allocates or reallocates:
     move:  ESC [ <row> ; <col> H     with 10-digit coordinates -> 24 bytes
     SGR:   ESC [ 38;2;255;255;255 m  (48;2 background is the same) -> 19 bytes
   A text cell emits at most move+SGR+1 byte of glyph; a half-block cell at
   most move+2*SGR+3 (the glyph is 3 bytes of UTF-8). */
#define MOVE_MAX 24
#define SGR_MAX 19
#define TEXT_CELL_MAX (MOVE_MAX + SGR_MAX + 1)
#define PIXEL_CELL_MAX (MOVE_MAX + 2 * SGR_MAX + 3)

/* U+2580 UPPER HALF BLOCK in UTF-8: top pixel -> foreground color, bottom
   pixel -> background color, two pixels per terminal cell. */
#define HALF_BLOCK "\xe2\x96\x80"
#define HALF_BLOCK_LEN 3

static int color_eq(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

/* --- Output buffer: accumulate one frame, then flush in a single write. ---

   Capacity is reserved up front (worst case: every cell changes), so appends
   during a frame are plain memcpys. The growth path in buf_ensure survives
   only as a safety net; with correct sizing it never runs. */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} OutBuf;

static void buf_free(OutBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* Grow the buffer to hold at least `cap` bytes. Returns -1 on OOM. */
static int buf_reserve(OutBuf *b, size_t cap) {
    if (cap <= b->cap) {
        return 0;
    }
    char *grown = realloc(b->data, cap);
    if (grown == NULL) {
        return -1;
    }
    b->data = grown;
    b->cap = cap;
    return 0;
}

/* Ensure room for `extra` more bytes. Should be a no-op after buf_reserve;
   kept as a best-effort fallback (on OOM the append is dropped). */
static int buf_ensure(OutBuf *b, size_t extra) {
    if (b->len + extra <= b->cap) {
        return 0;
    }
    size_t cap = b->cap ? b->cap : 4096;
    while (cap < b->len + extra) {
        cap *= 2;
    }
    return buf_reserve(b, cap);
}

static void buf_append(OutBuf *b, const char *bytes, size_t n) {
    if (buf_ensure(b, n) != 0) {
        return;
    }
    memcpy(b->data + b->len, bytes, n);
    b->len += n;
}

/* Write the decimal form of `v` at `p`; returns one past the last digit.
   Hand-rolled because snprintf dominated the emit profile: it re-parses its
   format string per call, and present() calls this several times per changed
   cell. */
static char *fmt_uint(char *p, unsigned v) {
    char digits[10];
    int n = 0;
    do {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v > 0);
    while (n > 0) {
        *p++ = digits[--n];
    }
    return p;
}

/* Append an absolute cursor move to 0-based (row, col): ESC[<r+1>;<c+1>H. */
static void emit_move(OutBuf *b, int row, int col) {
    char tmp[MOVE_MAX];
    char *p = tmp;
    *p++ = '\x1b';
    *p++ = '[';
    p = fmt_uint(p, (unsigned)(row + 1));
    *p++ = ';';
    p = fmt_uint(p, (unsigned)(col + 1));
    *p++ = 'H';
    buf_append(b, tmp, (size_t)(p - tmp));
}

/* Append a 24-bit SGR color escape: ESC[<c>8;2;<r>;<g>;<b>m, where `channel`
   is '3' for foreground (38;2) or '4' for background (48;2). */
static void emit_sgr(OutBuf *b, char channel, Color c) {
    char tmp[SGR_MAX];
    char *p = tmp;
    *p++ = '\x1b';
    *p++ = '[';
    *p++ = channel;
    *p++ = '8';
    *p++ = ';';
    *p++ = '2';
    *p++ = ';';
    p = fmt_uint(p, c.r);
    *p++ = ';';
    p = fmt_uint(p, c.g);
    *p++ = ';';
    p = fmt_uint(p, c.b);
    *p++ = 'm';
    buf_append(b, tmp, (size_t)(p - tmp));
}

/* Flush an accumulated frame in one write. Returns bytes written. */
static size_t buf_flush(OutBuf *b, FILE *out) {
    if (b->len > 0) {
        fwrite(b->data, 1, b->len, out);
        fflush(out);
    }
    return b->len;
}

/* --- Text renderer (canvas of characters + foreground colors) --- */

struct Renderer {
    int width;
    int height;

    Canvas *back;         /* the surface the caller draws into */
    char *front_cells;    /* last-presented characters, parallel to back->cells */
    Color *front_colors;  /* last-presented colors, parallel to back->colors */
    int dirty;            /* if set, the next present() repaints every cell */

    OutBuf out;           /* per-frame output bytes, worst-case pre-sized */
};

Renderer *renderer_create(int width, int height) {
    if (width <= 0 || height <= 0) {
        return NULL;
    }

    Renderer *r = calloc(1, sizeof(*r));
    if (r == NULL) {
        return NULL;
    }

    size_t count = (size_t)width * (size_t)height;
    r->width = width;
    r->height = height;
    r->back = canvas_create(width, height);
    r->front_cells = malloc(count);
    r->front_colors = malloc(count * sizeof(Color));
    if (r->back == NULL || r->front_cells == NULL || r->front_colors == NULL ||
        buf_reserve(&r->out, count * TEXT_CELL_MAX) != 0) {
        renderer_destroy(r);
        return NULL;
    }

    /* Front starts blank; dirty forces the first present() to paint everything
       regardless, but keep the memory defined for clean diffs thereafter. */
    memset(r->front_cells, ' ', count);
    memset(r->front_colors, 0, count * sizeof(Color));
    r->dirty = 1;
    return r;
}

void renderer_destroy(Renderer *renderer) {
    if (renderer == NULL) {
        return;
    }
    canvas_destroy(renderer->back);
    free(renderer->front_cells);
    free(renderer->front_colors);
    buf_free(&renderer->out);
    free(renderer);
}

Canvas *renderer_canvas(Renderer *renderer) {
    return renderer ? renderer->back : NULL;
}

int renderer_resize(Renderer *renderer, int width, int height) {
    if (renderer == NULL || width <= 0 || height <= 0) {
        return -1;
    }

    size_t count = (size_t)width * (size_t)height;
    Canvas *back = canvas_create(width, height);
    char *front_cells = malloc(count);
    Color *front_colors = malloc(count * sizeof(Color));
    if (back == NULL || front_cells == NULL || front_colors == NULL ||
        buf_reserve(&renderer->out, count * TEXT_CELL_MAX) != 0) {
        canvas_destroy(back);
        free(front_cells);
        free(front_colors);
        return -1;
    }

    canvas_destroy(renderer->back);
    free(renderer->front_cells);
    free(renderer->front_colors);

    renderer->width = width;
    renderer->height = height;
    renderer->back = back;
    renderer->front_cells = front_cells;
    renderer->front_colors = front_colors;
    memset(front_cells, ' ', count);
    memset(front_colors, 0, count * sizeof(Color));
    renderer->dirty = 1;
    return 0;
}

void renderer_mark_dirty(Renderer *renderer) {
    if (renderer != NULL) {
        renderer->dirty = 1;
    }
}

size_t renderer_present(Renderer *renderer, FILE *out) {
    if (renderer == NULL || out == NULL) {
        return 0;
    }

    Renderer *r = renderer;
    r->out.len = 0;

    /* Cursor/color tracking, carried across changed cells within this frame:
         cursor_col == the column the terminal cursor sits at (0-based), on
         cursor_row, immediately after the last cell we emitted. have_cursor is
         0 until we have positioned the cursor at least once. have_color/active
         track the SGR foreground color currently set on the terminal. */
    int have_cursor = 0;
    int cursor_row = 0;
    int cursor_col = 0;
    int have_color = 0;
    Color active = {0, 0, 0};

    for (int y = 0; y < r->height; ++y) {
        for (int x = 0; x < r->width; ++x) {
            size_t idx = (size_t)y * (size_t)r->width + (size_t)x;
            char ch = r->back->cells[idx];
            Color col = r->back->colors[idx];

            int changed = r->dirty || ch != r->front_cells[idx] ||
                          !color_eq(col, r->front_colors[idx]);
            if (!changed) {
                continue;
            }
            /* Move only if the cursor isn't already sitting exactly here. */
            if (!(have_cursor && cursor_row == y && cursor_col == x)) {
                emit_move(&r->out, y, x);
            }
            /* Color only if it differs from what's active. */
            if (!have_color || !color_eq(col, active)) {
                emit_sgr(&r->out, '3', col);
            }
            buf_append(&r->out, &ch, 1);

            /* Update the model of terminal state for the next cell. */
            have_cursor = 1;  cursor_row = y;  cursor_col = x + 1;
            have_color  = 1;  active = col;

            /* Front buffer now matches what we (will have) put on screen. */
            r->front_cells[idx] = ch;
            r->front_colors[idx] = col;
        }
    }

    r->dirty = 0;
    return buf_flush(&r->out, out);
}

/* --- Pixel renderer (framebuffer -> half-block cells) --- */

struct PixelRenderer {
    int width;    /* in pixels */
    int height;   /* in pixels; occupies ceil(height/2) terminal rows */

    Color *front; /* width * height last-presented pixels, row-major */
    int dirty;    /* if set, the next render_frame() repaints every cell */

    OutBuf out;   /* per-frame output bytes, worst-case pre-sized */
};

/* Worst-case output for a w x h pixel grid: every half-block cell changes. */
static size_t pixel_out_cap(int width, int height) {
    size_t cells = (size_t)width * (size_t)((height + 1) / 2);
    return cells * PIXEL_CELL_MAX;
}

PixelRenderer *pixel_renderer_create(int width, int height) {
    if (width <= 0 || height <= 0) {
        return NULL;
    }
    PixelRenderer *r = calloc(1, sizeof(*r));
    if (r == NULL) {
        return NULL;
    }
    size_t count = (size_t)width * (size_t)height;
    r->width = width;
    r->height = height;
    r->front = malloc(count * sizeof(Color));
    if (r->front == NULL || buf_reserve(&r->out, pixel_out_cap(width, height)) != 0) {
        pixel_renderer_destroy(r);
        return NULL;
    }
    memset(r->front, 0, count * sizeof(Color));
    r->dirty = 1;
    return r;
}

void pixel_renderer_destroy(PixelRenderer *renderer) {
    if (renderer == NULL) {
        return;
    }
    free(renderer->front);
    buf_free(&renderer->out);
    free(renderer);
}

int pixel_renderer_resize(PixelRenderer *renderer, int width, int height) {
    if (renderer == NULL || width <= 0 || height <= 0) {
        return -1;
    }
    size_t count = (size_t)width * (size_t)height;
    Color *front = malloc(count * sizeof(Color));
    if (front == NULL ||
        buf_reserve(&renderer->out, pixel_out_cap(width, height)) != 0) {
        free(front);
        return -1;
    }
    free(renderer->front);
    renderer->front = front;
    renderer->width = width;
    renderer->height = height;
    memset(front, 0, count * sizeof(Color));
    renderer->dirty = 1;
    return 0;
}

void pixel_renderer_mark_dirty(PixelRenderer *renderer) {
    if (renderer != NULL) {
        renderer->dirty = 1;
    }
}

size_t render_frame(PixelRenderer *renderer, const Framebuffer *fb, FILE *out) {
    if (renderer == NULL || fb == NULL || fb->pixels == NULL || out == NULL) {
        return 0;
    }
    /* Follow the source's size; the resize is the one path that allocates. */
    if (fb->width != renderer->width || fb->height != renderer->height) {
        if (pixel_renderer_resize(renderer, fb->width, fb->height) != 0) {
            return 0;
        }
    }

    PixelRenderer *r = renderer;
    r->out.len = 0;

    size_t w = (size_t)r->width;
    int h = r->height;
    const Color black = {0, 0, 0};

    /* Same terminal-state model as renderer_present, with the color channel
       split in two: fg tracks the last-emitted 38;2 escape, bg the last 48;2.
       Each elides independently — a scanline of sky over changing ground
       re-emits only the bg escape per cell. */
    int have_cursor = 0;
    int cursor_row = 0;
    int cursor_col = 0;
    int have_fg = 0, have_bg = 0;
    Color fg = black, bg = black;

    for (int row = 0; row * 2 < h; ++row) {
        int ty = row * 2;
        int by = ty + 1;
        for (int x = 0; x < r->width; ++x) {
            size_t ti = (size_t)ty * w + (size_t)x;
            Color top = fb->pixels[ti];
            Color bottom = by < h ? fb->pixels[ti + w] : black;

            /* A cell changed if either of its two pixels did. An odd final
               row has no bottom pixel; its front slot simply never differs. */
            int changed = r->dirty || !color_eq(top, r->front[ti]) ||
                          (by < h && !color_eq(bottom, r->front[ti + w]));
            if (!changed) {
                continue;
            }
            if (!(have_cursor && cursor_row == row && cursor_col == x)) {
                emit_move(&r->out, row, x);
            }
            if (!have_fg || !color_eq(top, fg)) {
                emit_sgr(&r->out, '3', top);
                fg = top;
                have_fg = 1;
            }
            if (!have_bg || !color_eq(bottom, bg)) {
                emit_sgr(&r->out, '4', bottom);
                bg = bottom;
                have_bg = 1;
            }
            buf_append(&r->out, HALF_BLOCK, HALF_BLOCK_LEN);

            have_cursor = 1;  cursor_row = row;  cursor_col = x + 1;
            r->front[ti] = top;
            if (by < h) {
                r->front[ti + w] = bottom;
            }
        }
    }

    r->dirty = 0;
    return buf_flush(&r->out, out);
}
