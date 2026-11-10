#include "render.h"
#include "test.h"

#define BLOCK "\xe2\x96\x80"
#define WHITE "\x1b[38;2;255;255;255m"
#define BLACK_FG "\x1b[38;2;0;0;0m"
#define BLACK_BG "\x1b[48;2;0;0;0m"

static void expect_text(Renderer *renderer, const char *expected) {
    FILE *out = tmpfile();
    CHECK(out != NULL);
    CHECK(renderer_present(renderer, out) == strlen(expected));
    check_output(out, expected);
    CHECK(fclose(out) == 0);
}

static void expect_pixels(PixelRenderer *renderer, const Framebuffer *fb,
                          const char *expected) {
    FILE *out = tmpfile();
    CHECK(out != NULL);
    CHECK(render_frame(renderer, fb, out) == strlen(expected));
    check_output(out, expected);
    CHECK(fclose(out) == 0);
}

static void text_invalid_inputs(void) {
    CHECK(renderer_create(0, 2) == NULL);
    CHECK(renderer_create(2, 0) == NULL);
    CHECK(renderer_create(-1, 2) == NULL);
    CHECK(renderer_create(2, -1) == NULL);
    CHECK(renderer_canvas(NULL) == NULL);
    CHECK(renderer_resize(NULL, 2, 2) == -1);
    renderer_destroy(NULL);
    renderer_mark_dirty(NULL);
    expect_text(NULL, "");

    Renderer *r = renderer_create(1, 1);
    CHECK(r != NULL);
    CHECK(renderer_present(r, NULL) == 0);
    /* An invalid output stream must not consume the pending repaint. */
    expect_text(r, "\x1b[1;1H" WHITE " ");
    renderer_destroy(r);
}

static void text_first_frame_and_unchanged(void) {
    Renderer *r = renderer_create(3, 2);
    CHECK(r != NULL);
    Canvas *canvas = renderer_canvas(r);
    CHECK(canvas != NULL);
    CHECK(canvas->width == 3 && canvas->height == 2);
    /* Even the default spaces must paint initially; rows need cursor moves. */
    expect_text(r, "\x1b[1;1H" WHITE "   \x1b[2;1H   ");
    expect_text(r, "");
    canvas_set(canvas, 1, 1, 'X');
    expect_text(r, "\x1b[2;2H" WHITE "X");
    expect_text(r, "");
    renderer_destroy(r);
}

static void text_changed_runs_and_colors(void) {
    Renderer *r = renderer_create(4, 1);
    CHECK(r != NULL);
    Canvas *canvas = renderer_canvas(r);
    expect_text(r, "\x1b[1;1H" WHITE "    ");

    canvas_set(canvas, 0, 0, 'A');
    canvas_set(canvas, 1, 0, 'B');
    expect_text(r, "\x1b[1;1H" WHITE "AB");

    canvas_set(canvas, 0, 0, 'C');
    canvas_set(canvas, 3, 0, 'D');
    /* Skip the unchanged gap, but keep the active color across the move. */
    expect_text(r, "\x1b[1;1H" WHITE "C\x1b[1;4HD");

    canvas_set_color(canvas, 1, 0, color_rgb(0, 10, 255));
    canvas_set_color(canvas, 2, 0, color_rgb(255, 0, 100));
    expect_text(r, "\x1b[1;2H\x1b[38;2;0;10;255mB"
                   "\x1b[38;2;255;0;100m ");
    expect_text(r, "");
    renderer_destroy(r);
}

static void text_dirty_and_resize(void) {
    Renderer *r = renderer_create(2, 1);
    CHECK(r != NULL);
    Canvas *canvas = renderer_canvas(r);
    canvas_set(canvas, 1, 0, 'X');
    expect_text(r, "\x1b[1;1H" WHITE " X");
    renderer_mark_dirty(r);
    expect_text(r, "\x1b[1;1H" WHITE " X");
    expect_text(r, "");

    CHECK(renderer_resize(r, 0, 1) == -1);
    CHECK(renderer_resize(r, 1, -1) == -1);
    CHECK(renderer_canvas(r) == canvas);
    CHECK(canvas_get(canvas, 1, 0) == 'X');
    expect_text(r, "");

    CHECK(renderer_resize(r, 1, 2) == 0);
    canvas = renderer_canvas(r);
    CHECK(canvas->width == 1 && canvas->height == 2);
    CHECK(canvas_get(canvas, 0, 0) == ' ');
    CHECK_COLOR(canvas->colors[0], 255, 255, 255);
    expect_text(r, "\x1b[1;1H" WHITE " \x1b[2;1H ");
    expect_text(r, "");

    CHECK(renderer_resize(r, 1, 2) == 0);
    expect_text(r, "\x1b[1;1H" WHITE " \x1b[2;1H ");
    renderer_destroy(r);
}

static void text_multidigit_cursor(void) {
    Renderer *r = renderer_create(12, 12);
    CHECK(r != NULL);
    FILE *out = tmpfile();
    CHECK(out != NULL);
    CHECK(renderer_present(r, out) > 0);
    CHECK(fclose(out) == 0);
    canvas_set(renderer_canvas(r), 11, 10, '!');
    expect_text(r, "\x1b[11;12H" WHITE "!");
    renderer_destroy(r);
}

static void pixel_invalid_inputs(void) {
    CHECK(pixel_renderer_create(0, 2) == NULL);
    CHECK(pixel_renderer_create(2, 0) == NULL);
    CHECK(pixel_renderer_create(-1, 2) == NULL);
    CHECK(pixel_renderer_create(2, -1) == NULL);
    CHECK(pixel_renderer_resize(NULL, 2, 2) == -1);
    pixel_renderer_destroy(NULL);
    pixel_renderer_mark_dirty(NULL);

    PixelRenderer *r = pixel_renderer_create(1, 1);
    Framebuffer *fb = framebuffer_create(1, 1);
    CHECK(r != NULL && fb != NULL);
    expect_pixels(NULL, fb, "");
    expect_pixels(r, NULL, "");
    Framebuffer missing_pixels = {1, 1, NULL};
    expect_pixels(r, &missing_pixels, "");
    CHECK(render_frame(r, fb, NULL) == 0);
    expect_pixels(r, fb, "\x1b[1;1H" BLACK_FG BLACK_BG BLOCK);
    pixel_renderer_destroy(r);
    framebuffer_destroy(fb);
}

static void pixel_first_frame_and_odd_height(void) {
    PixelRenderer *r = pixel_renderer_create(2, 3);
    Framebuffer *fb = framebuffer_create(2, 3);
    CHECK(r != NULL && fb != NULL);
    expect_pixels(r, fb, "\x1b[1;1H" BLACK_FG BLACK_BG BLOCK BLOCK
                         "\x1b[2;1H" BLOCK BLOCK);
    expect_pixels(r, fb, "");

    framebuffer_set(fb, 1, 2, color_rgb(255, 10, 0));
    expect_pixels(r, fb, "\x1b[2;2H\x1b[38;2;255;10;0m" BLACK_BG BLOCK);
    expect_pixels(r, fb, "");
    pixel_renderer_destroy(r);
    framebuffer_destroy(fb);
}

static void pixel_independent_color_elision(void) {
    PixelRenderer *r = pixel_renderer_create(3, 2);
    Framebuffer *fb = framebuffer_create(3, 2);
    CHECK(r != NULL && fb != NULL);
    framebuffer_set(fb, 0, 0, color_rgb(255, 0, 0));
    framebuffer_set(fb, 1, 0, color_rgb(255, 0, 0));
    framebuffer_set(fb, 2, 0, color_rgb(0, 0, 255));
    framebuffer_set(fb, 1, 1, color_rgb(0, 255, 0));
    framebuffer_set(fb, 2, 1, color_rgb(0, 255, 0));
    expect_pixels(r, fb, "\x1b[1;1H\x1b[38;2;255;0;0m" BLACK_BG BLOCK
                         "\x1b[48;2;0;255;0m" BLOCK
                         "\x1b[38;2;0;0;255m" BLOCK);
    expect_pixels(r, fb, "");

    /* Either pixel changing must redraw the entire pair with both colors. */
    framebuffer_set(fb, 1, 0, color_rgb(10, 20, 30));
    expect_pixels(r, fb, "\x1b[1;2H\x1b[38;2;10;20;30m"
                         "\x1b[48;2;0;255;0m" BLOCK);
    framebuffer_set(fb, 1, 1, color_rgb(40, 50, 60));
    expect_pixels(r, fb, "\x1b[1;2H\x1b[38;2;10;20;30m"
                         "\x1b[48;2;40;50;60m" BLOCK);
    expect_pixels(r, fb, "");
    pixel_renderer_destroy(r);
    framebuffer_destroy(fb);
}

static void pixel_changed_runs(void) {
    PixelRenderer *r = pixel_renderer_create(4, 2);
    Framebuffer *fb = framebuffer_create(4, 2);
    CHECK(r != NULL && fb != NULL);
    expect_pixels(r, fb, "\x1b[1;1H" BLACK_FG BLACK_BG BLOCK BLOCK BLOCK BLOCK);
    framebuffer_set(fb, 0, 0, color_rgb(1, 2, 3));
    framebuffer_set(fb, 1, 0, color_rgb(1, 2, 3));
    expect_pixels(r, fb, "\x1b[1;1H\x1b[38;2;1;2;3m" BLACK_BG BLOCK BLOCK);
    framebuffer_set(fb, 0, 0, color_rgb(4, 5, 6));
    framebuffer_set(fb, 3, 0, color_rgb(4, 5, 6));
    expect_pixels(r, fb, "\x1b[1;1H\x1b[38;2;4;5;6m" BLACK_BG BLOCK
                         "\x1b[1;4H" BLOCK);
    expect_pixels(r, fb, "");
    pixel_renderer_destroy(r);
    framebuffer_destroy(fb);
}

static void pixel_dirty_resize_and_source_resize(void) {
    PixelRenderer *r = pixel_renderer_create(1, 2);
    Framebuffer *fb = framebuffer_create(1, 2);
    CHECK(r != NULL && fb != NULL);
    framebuffer_set(fb, 0, 0, color_rgb(12, 34, 56));
    const char *first = "\x1b[1;1H\x1b[38;2;12;34;56m" BLACK_BG BLOCK;
    expect_pixels(r, fb, first);
    pixel_renderer_mark_dirty(r);
    expect_pixels(r, fb, first);
    expect_pixels(r, fb, "");

    CHECK(pixel_renderer_resize(r, 0, 2) == -1);
    CHECK(pixel_renderer_resize(r, 1, -2) == -1);
    expect_pixels(r, fb, "");
    CHECK(pixel_renderer_resize(r, 1, 2) == 0);
    expect_pixels(r, fb, first);

    Framebuffer *larger = framebuffer_create(2, 3);
    CHECK(larger != NULL);
    expect_pixels(r, larger, "\x1b[1;1H" BLACK_FG BLACK_BG BLOCK BLOCK
                             "\x1b[2;1H" BLOCK BLOCK);
    expect_pixels(r, larger, "");
    /* A subsequent smaller source resizes and repaints too. */
    expect_pixels(r, fb, first);
    expect_pixels(r, fb, "");
    CHECK(pixel_renderer_resize(r, 2, 3) == 0);
    expect_pixels(r, larger, "\x1b[1;1H" BLACK_FG BLACK_BG BLOCK BLOCK
                             "\x1b[2;1H" BLOCK BLOCK);
    framebuffer_destroy(larger);
    framebuffer_destroy(fb);
    pixel_renderer_destroy(r);
}

int main(void) {
    RUN(text_invalid_inputs);
    RUN(text_first_frame_and_unchanged);
    RUN(text_changed_runs_and_colors);
    RUN(text_dirty_and_resize);
    RUN(text_multidigit_cursor);
    RUN(pixel_invalid_inputs);
    RUN(pixel_first_frame_and_odd_height);
    RUN(pixel_independent_color_elision);
    RUN(pixel_changed_runs);
    RUN(pixel_dirty_resize_and_source_resize);
    return EXIT_SUCCESS;
}
