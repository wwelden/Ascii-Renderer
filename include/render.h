#ifndef ASCII_RENDERER_RENDER_H
#define ASCII_RENDERER_RENDER_H

#include <stddef.h>
#include <stdio.h>

#include "canvas.h"
#include "framebuffer.h"

/* A double-buffered terminal renderer.

   Draw into the back-buffer canvas (renderer_canvas), then call
   renderer_present() to bring the terminal up to date. present() diffs the
   back buffer against a snapshot of what is currently on screen (the "front"
   buffer) and emits only the ANSI needed for the cells that changed:

     - cursor-move escapes only where changed cells are not contiguous, and
     - color escapes only where the color changes,

   all accumulated into one buffer and written with a single fwrite(). This is
   the core of hitting 60fps: cost scales with what moved, not screen area.

   Zero-allocation guarantee (Phase 4): all buffers — including the output
   byte buffer, sized for the worst case where every cell changes — are
   allocated in renderer_create()/renderer_resize(). renderer_present() and
   render_frame() perform no heap allocation. */

typedef struct Renderer Renderer;

/* Allocate a renderer with a width x height cell grid. Returns NULL on
   failure. The next present() repaints every cell. */
Renderer *renderer_create(int width, int height);

/* Free a renderer created by renderer_create. Safe to call with NULL. */
void renderer_destroy(Renderer *renderer);

/* The back-buffer canvas to draw the current frame into. Owned by the
   renderer; do not destroy it. Valid until the renderer is resized/destroyed. */
Canvas *renderer_canvas(Renderer *renderer);

/* Resize the cell grid, preserving nothing. Forces a full repaint on the next
   present(). Returns 0 on success, -1 on allocation failure (the renderer is
   left unchanged on failure). */
int renderer_resize(Renderer *renderer, int width, int height);

/* Discard the front-buffer state so the next present() repaints every cell.
   Call this after you clear or otherwise disturb the screen yourself (e.g. on
   resize, or after leaving/re-entering the alternate screen). */
void renderer_mark_dirty(Renderer *renderer);

/* Diff the back buffer against the front buffer and emit minimal ANSI to `out`
   to update the terminal, then flush. The front buffer is updated to match the
   back buffer. Returns the number of bytes written this frame (useful for
   benchmarking); 0 if nothing changed. */
size_t renderer_present(Renderer *renderer, FILE *out);

/* --- Pixel renderer: the library entry point for framebuffer sources. ---

   A PixelRenderer presents an RGB Framebuffer to the terminal using the
   half-block trick (each text cell shows two vertically stacked pixels:
   top -> foreground, bottom -> background of U+2580 "▀"), with the same
   diff-and-elide strategy as renderer_present(): only cells whose pixel pair
   changed are touched, cursor moves are elided across contiguous runs, and
   fg/bg SGR escapes are each elided independently while they repeat.

   This is the API an external engine drives: it owns its game state and frame
   loop, fills a Framebuffer, and calls render_frame() once per frame. A
   width x height pixel grid occupies width columns and ceil(height/2) rows of
   the terminal, anchored at the top-left. */

typedef struct PixelRenderer PixelRenderer;

/* Allocate a pixel renderer for width x height *pixels* (not cells). Returns
   NULL on failure. The first render_frame() paints every cell. */
PixelRenderer *pixel_renderer_create(int width, int height);

/* Free a pixel renderer. Safe to call with NULL. */
void pixel_renderer_destroy(PixelRenderer *renderer);

/* Resize the pixel grid. Forces a full repaint on the next render_frame().
   Returns 0 on success, -1 on allocation failure (renderer unchanged). */
int pixel_renderer_resize(PixelRenderer *renderer, int width, int height);

/* Discard the front-buffer snapshot so the next render_frame() repaints every
   cell (e.g. after the screen was cleared or the terminal resized). */
void pixel_renderer_mark_dirty(PixelRenderer *renderer);

/* Present one frame: diff `fb` against the last-presented frame and emit
   minimal ANSI to `out`, then flush. If `fb`'s dimensions differ from the
   renderer's, the renderer resizes itself first (full repaint, and the one
   place this path may allocate). Returns bytes written; 0 if nothing changed
   or on error. */
size_t render_frame(PixelRenderer *renderer, const Framebuffer *fb, FILE *out);

#endif /* ASCII_RENDERER_RENDER_H */
