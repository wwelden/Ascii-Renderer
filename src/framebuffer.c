#include "framebuffer.h"

#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

/* U+2580 UPPER HALF BLOCK, encoded in UTF-8. Drawn in every image cell: the
   glyph's top half takes the foreground color, its bottom half the background,
   so one cell shows two vertically-stacked pixels. */
#define HALF_BLOCK "\xe2\x96\x80"

static int color_same(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

Framebuffer *framebuffer_create(int width, int height) {
    if (width <= 0 || height <= 0) {
        return NULL;
    }
    Framebuffer *fb = malloc(sizeof(*fb));
    if (fb == NULL) {
        return NULL;
    }
    size_t count = (size_t)width * (size_t)height;
    fb->width = width;
    fb->height = height;
    fb->pixels = calloc(count, sizeof(Color)); /* black */
    if (fb->pixels == NULL) {
        free(fb);
        return NULL;
    }
    return fb;
}

void framebuffer_destroy(Framebuffer *fb) {
    if (fb == NULL) {
        return;
    }
    free(fb->pixels);
    free(fb);
}

void framebuffer_fill(Framebuffer *fb, Color color) {
    if (fb == NULL) {
        return;
    }
    size_t count = (size_t)fb->width * (size_t)fb->height;
    for (size_t i = 0; i < count; ++i) {
        fb->pixels[i] = color;
    }
}

void framebuffer_set(Framebuffer *fb, int x, int y, Color color) {
    if (fb == NULL || x < 0 || x >= fb->width || y < 0 || y >= fb->height) {
        return;
    }
    fb->pixels[(size_t)y * (size_t)fb->width + (size_t)x] = color;
}

Color framebuffer_get(const Framebuffer *fb, int x, int y) {
    if (fb == NULL || x < 0 || x >= fb->width || y < 0 || y >= fb->height) {
        return color_rgb(0, 0, 0);
    }
    return fb->pixels[(size_t)y * (size_t)fb->width + (size_t)x];
}

Framebuffer *framebuffer_load(const char *path) {
    int w = 0, h = 0, channels = 0;
    /* Force 3 channels: stb converts grayscale/RGBA to plain RGB for us. */
    unsigned char *data = stbi_load(path, &w, &h, &channels, 3);
    if (data == NULL) {
        return NULL;
    }
    Framebuffer *fb = framebuffer_create(w, h);
    if (fb == NULL) {
        stbi_image_free(data);
        return NULL;
    }
    for (int i = 0; i < w * h; ++i) {
        fb->pixels[i] = color_rgb(data[i * 3], data[i * 3 + 1], data[i * 3 + 2]);
    }
    stbi_image_free(data);
    return fb;
}

Framebuffer *framebuffer_scaled(const Framebuffer *src, int dst_w, int dst_h) {
    if (src == NULL || dst_w <= 0 || dst_h <= 0) {
        return NULL;
    }
    Framebuffer *dst = framebuffer_create(dst_w, dst_h);
    if (dst == NULL) {
        return NULL;
    }
    /* Nearest-neighbor: map each dst pixel back to the closest src pixel. */
    for (int y = 0; y < dst_h; ++y) {
        int sy = (int)((long)y * src->height / dst_h);
        for (int x = 0; x < dst_w; ++x) {
            int sx = (int)((long)x * src->width / dst_w);
            dst->pixels[(size_t)y * dst_w + x] =
                src->pixels[(size_t)sy * src->width + sx];
        }
    }
    return dst;
}

void framebuffer_render_halfblocks(const Framebuffer *fb, FILE *out) {
    if (fb == NULL || out == NULL) {
        return;
    }

    /* One text row per pair of pixel rows; round up so an odd final row still
       shows (its absent bottom pixel is treated as black). */
    for (int row = 0; row * 2 < fb->height; ++row) {
        int ty = row * 2;
        int by = row * 2 + 1;

        /* Color-elision state, reset per row so nothing bleeds past the
           newline. have_fg / have_bg say whether the escape currently on the
           terminal is known, and fg / bg what it is set to. */
        int have_fg = 0, have_bg = 0;
        Color fg = {0, 0, 0}, bg = {0, 0, 0};

        for (int x = 0; x < fb->width; ++x) {
            Color top = fb->pixels[(size_t)ty * (size_t)fb->width + (size_t)x];
            Color bottom = (by < fb->height)
                ? fb->pixels[(size_t)by * (size_t)fb->width + (size_t)x]
                : color_rgb(0, 0, 0);

            /* top -> foreground, bottom -> background of the HALF_BLOCK glyph.
               Emit each color escape only when it changed from the previous
               cell in this row (the same elision as renderer_present, with a
               background channel added). */
            if (!have_fg || !color_same(top, fg)) {
                fprintf(out, "\x1b[38;2;%u;%u;%um", top.r, top.g, top.b);
                fg = top;
                have_fg = 1;
            }
            if (!have_bg || !color_same(bottom, bg)) {
                fprintf(out, "\x1b[48;2;%u;%u;%um", bottom.r, bottom.g, bottom.b);
                bg = bottom;
                have_bg = 1;
            }
            fputs(HALF_BLOCK, out);
        }

        fputs("\x1b[0m\n", out); /* reset SGR, end the row */
    }
    fflush(out);
}
