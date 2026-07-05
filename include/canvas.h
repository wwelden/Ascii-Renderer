#ifndef ASCII_RENDERER_CANVAS_H
#define ASCII_RENDERER_CANVAS_H

#include <stdio.h>

/* Number of distinct brightness levels in the ASCII ramp. */
#define ASCII_RAMP_LEVELS 70

/* A 70-character brightness ramp ordered from dimmest (space) to densest ('$').
   Index 0 is the darkest glyph; ASCII_RAMP_LEVELS - 1 is the brightest. */
extern const char ascii_ramp[ASCII_RAMP_LEVELS + 1];

/* A 24-bit RGB color. */
typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} Color;

/* Convenience constructor for an RGB color. */
Color color_rgb(unsigned char r, unsigned char g, unsigned char b);

/* A row-major grid of characters, each with its own RGB color. */
typedef struct {
    int width;
    int height;
    char *cells;    /* width * height characters, row-major */
    Color *colors;  /* width * height colors, parallel to cells */
} Canvas;

/* Allocate a canvas of the given size. Returns NULL on failure. */
Canvas *canvas_create(int width, int height);

/* Free a canvas created by canvas_create. Safe to call with NULL. */
void canvas_destroy(Canvas *canvas);

/* Reset every cell to the given fill character. */
void canvas_clear(Canvas *canvas, char fill);

/* Write a character at (x, y), leaving its color unchanged. Out-of-bounds
   writes are ignored. */
void canvas_set(Canvas *canvas, int x, int y, char ch);

/* Read the character at (x, y). Returns '\0' if out of bounds. */
char canvas_get(const Canvas *canvas, int x, int y);

/* Set the RGB color of a cell, leaving its character unchanged. */
void canvas_set_color(Canvas *canvas, int x, int y, Color color);

/* Set both the character and the color of a cell at once. */
void canvas_put(Canvas *canvas, int x, int y, char ch, Color color);

/* Draw a line between two points using Bresenham's algorithm. */
void canvas_draw_line(Canvas *canvas, int x0, int y0, int x1, int y1, char ch);

/* Draw the outline of a rectangle. */
void canvas_draw_rect(Canvas *canvas, int x, int y, int w, int h, char ch);

/* Draw a filled rectangle. */
void canvas_fill_rect(Canvas *canvas, int x, int y, int w, int h, char ch);

/* Map an intensity in [0.0, 1.0] to a character from the 70-level ramp.
   0.0 yields the dimmest glyph, 1.0 the densest; out-of-range values clamp. */
char canvas_ramp_char(double intensity);

/* Shade a cell from an intensity in [0.0, 1.0] using the ramp. */
void canvas_shade(Canvas *canvas, int x, int y, double intensity);

/* Print the canvas to the given stream as plain text, one row per line.
   Colors are ignored. */
void canvas_render(const Canvas *canvas, FILE *out);

/* Print the canvas using ANSI 24-bit ("truecolor") escape codes, so each cell
   is drawn in its color. Requires a terminal with truecolor support. */
void canvas_render_color(const Canvas *canvas, FILE *out);

#endif /* ASCII_RENDERER_CANVAS_H */
