#include "render.h"

#include <stdlib.h>
#include <string.h>

struct Renderer {
    int width;
    int height;

    Canvas *back;         /* the surface the caller draws into */
    char *front_cells;    /* last-presented characters, parallel to back->cells */
    Color *front_colors;  /* last-presented colors, parallel to back->colors */
    int dirty;            /* if set, the next present() repaints every cell */

    char *out;            /* growable byte buffer built up over one present() */
    size_t out_len;
    size_t out_cap;
};

static int color_eq(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

/* --- Output buffer: accumulate one frame, then flush in a single write. --- */

/* Ensure the buffer can hold `extra` more bytes. Returns -1 on OOM, in which
   case the append is dropped (best-effort output). */
static int buf_ensure(Renderer *r, size_t extra) {
    if (r->out_len + extra <= r->out_cap) {
        return 0;
    }
    size_t cap = r->out_cap ? r->out_cap : 4096;
    while (cap < r->out_len + extra) {
        cap *= 2;
    }
    char *grown = realloc(r->out, cap);
    if (grown == NULL) {
        return -1;
    }
    r->out = grown;
    r->out_cap = cap;
    return 0;
}

static void buf_append(Renderer *r, const char *bytes, size_t n) {
    if (buf_ensure(r, n) != 0) {
        return;
    }
    memcpy(r->out + r->out_len, bytes, n);
    r->out_len += n;
}

/* Emit an absolute cursor move to 0-based (row, col) as ESC[<row+1>;<col+1>H. */
static void emit_move(Renderer *r, int row, int col) {
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "\x1b[%d;%dH", row + 1, col + 1);
    buf_append(r, tmp, (size_t)n);
}

/* Emit a 24-bit foreground color as ESC[38;2;<r>;<g>;<b>m. */
static void emit_color(Renderer *r, Color c) {
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "\x1b[38;2;%u;%u;%um", c.r, c.g, c.b);
    buf_append(r, tmp, (size_t)n);
}

/* Emit a single visible character at the cursor. */
static void emit_char(Renderer *r, char ch) {
    buf_append(r, &ch, 1);
}

/* --- Lifecycle --- */

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
    if (r->back == NULL || r->front_cells == NULL || r->front_colors == NULL) {
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
    free(renderer->out);
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
    if (back == NULL || front_cells == NULL || front_colors == NULL) {
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

/* --- The diff + emit pass --- */

size_t renderer_present(Renderer *renderer, FILE *out) {
    if (renderer == NULL || out == NULL) {
        return 0;
    }

    Renderer *r = renderer;
    r->out_len = 0;

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
                emit_move(r, y, x);
            }
            /* Color only if it differs from what's active. */
            if (!have_color || !color_eq(col, active)) {
                emit_color(r, col);
            }
            emit_char(r, ch);

            /* Update the model of terminal state for the next cell. */
            have_cursor = 1;  cursor_row = y;  cursor_col = x + 1;
            have_color  = 1;  active = col;

            /* Front buffer now matches what we (will have) put on screen. */
            r->front_cells[idx] = ch;
            r->front_colors[idx] = col;
        }
    }

    r->dirty = 0;

    if (r->out_len > 0) {
        fwrite(r->out, 1, r->out_len, out);
        fflush(out);
    }
    return r->out_len;
}
