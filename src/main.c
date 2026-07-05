#include "canvas.h"

#include <math.h>
#include <stdio.h>

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

int main(void) {
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
