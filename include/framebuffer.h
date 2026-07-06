#ifndef ASCII_RENDERER_FRAMEBUFFER_H
#define ASCII_RENDERER_FRAMEBUFFER_H

#include <stdio.h>

#include "canvas.h" /* for Color */

/* A row-major grid of RGB pixels: the "image space" that sits below the ASCII
   cell grid. Images load into one (framebuffer_load), and later phases render
   game frames into one. Displayed via the half-block trick, where two vertical
   pixels share a single terminal cell, so a framebuffer H pixels tall needs
   only ceil(H/2) text rows. */
typedef struct {
    int width;
    int height;
    Color *pixels; /* width * height, row-major */
} Framebuffer;

/* Allocate a width x height framebuffer, cleared to black. NULL on failure. */
Framebuffer *framebuffer_create(int width, int height);

/* Free a framebuffer created here. Safe to call with NULL. */
void framebuffer_destroy(Framebuffer *fb);

/* Set every pixel to `color`. */
void framebuffer_fill(Framebuffer *fb, Color color);

/* Set/read a single pixel. Out-of-bounds writes are ignored; out-of-bounds
   reads return black. */
void framebuffer_set(Framebuffer *fb, int x, int y, Color color);
Color framebuffer_get(const Framebuffer *fb, int x, int y);

/* Load an image (PNG/JPEG/BMP/PNM/...) from disk into a new framebuffer via
   stb_image, forcing 3-channel RGB. Returns NULL on failure. */
Framebuffer *framebuffer_load(const char *path);

/* Nearest-neighbor resample of `src` into a new dst_w x dst_h framebuffer.
   Returns NULL on failure. */
Framebuffer *framebuffer_scaled(const Framebuffer *src, int dst_w, int dst_h);

/* Render the framebuffer to `out` using the Unicode upper-half-block trick:
   each text row encodes two pixel rows (top pixel -> foreground color, bottom
   pixel -> background color of U+2580 "▀"), doubling vertical resolution and
   keeping pixels roughly square. */
void framebuffer_render_halfblocks(const Framebuffer *fb, FILE *out);

#endif /* ASCII_RENDERER_FRAMEBUFFER_H */
