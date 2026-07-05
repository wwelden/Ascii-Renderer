#ifndef ASCII_RENDERER_RENDER_H
#define ASCII_RENDERER_RENDER_H

#include <stddef.h>
#include <stdio.h>

#include "canvas.h"

/* A double-buffered terminal renderer.

   Draw into the back-buffer canvas (renderer_canvas), then call
   renderer_present() to bring the terminal up to date. present() diffs the
   back buffer against a snapshot of what is currently on screen (the "front"
   buffer) and emits only the ANSI needed for the cells that changed:

     - cursor-move escapes only where changed cells are not contiguous, and
     - color escapes only where the color changes,

   all accumulated into one buffer and written with a single fwrite(). This is
   the core of hitting 60fps: cost scales with what moved, not screen area. */

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

#endif /* ASCII_RENDERER_RENDER_H */
