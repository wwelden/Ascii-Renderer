#include "canvas.h"
#include "test.h"

static void check_cells(const Canvas *canvas, const char *expected) {
    CHECK(strlen(expected) == (size_t)canvas->width * (size_t)canvas->height);
    CHECK(memcmp(canvas->cells, expected, strlen(expected)) == 0);
}

static void test_canvas_creation(void) {
    CHECK(canvas_create(0, 2) == NULL);
    CHECK(canvas_create(2, 0) == NULL);
    CHECK(canvas_create(-1, 2) == NULL);
    CHECK(canvas_create(2, -1) == NULL);
    Canvas *canvas = canvas_create(3, 2);
    CHECK(canvas != NULL);
    CHECK(canvas->width == 3 && canvas->height == 2);
    check_cells(canvas, "      ");
    for (int i = 0; i < 6; ++i) {
        CHECK_COLOR(canvas->colors[i], 255, 255, 255);
    }
    CHECK_COLOR(color_rgb(0, 128, 255), 0, 128, 255);
    canvas_destroy(canvas);
    canvas_destroy(NULL);
}

static void test_canvas_cells_and_bounds(void) {
    Canvas *canvas = canvas_create(3, 2);
    CHECK(canvas != NULL);
    Color red = color_rgb(255, 0, 0);
    canvas_put(canvas, 2, 1, '@', red);
    CHECK(canvas_get(canvas, 2, 1) == '@');
    CHECK_COLOR(canvas->colors[5], 255, 0, 0);
    canvas_set(canvas, 2, 1, '#');
    CHECK_COLOR(canvas->colors[5], 255, 0, 0);
    canvas_set_color(canvas, 2, 1, color_rgb(0, 5, 9));
    CHECK(canvas_get(canvas, 2, 1) == '#');
    CHECK_COLOR(canvas->colors[5], 0, 5, 9);

    const int outside[][2] = {{-1, 0}, {3, 0}, {0, -1}, {0, 2}};
    for (size_t i = 0; i < sizeof(outside) / sizeof(outside[0]); ++i) {
        int x = outside[i][0], y = outside[i][1];
        canvas_set(canvas, x, y, '!');
        canvas_set_color(canvas, x, y, red);
        canvas_put(canvas, x, y, '?', red);
        CHECK(canvas_get(canvas, x, y) == '\0');
    }
    check_cells(canvas, "     #");
    for (int i = 0; i < 5; ++i) {
        CHECK_COLOR(canvas->colors[i], 255, 255, 255);
    }
    canvas_clear(canvas, '.');
    check_cells(canvas, "......");
    CHECK_COLOR(canvas->colors[5], 0, 5, 9);
    canvas_destroy(canvas);

    canvas_clear(NULL, '.');
    canvas_set(NULL, 0, 0, '.');
    canvas_set_color(NULL, 0, 0, red);
    canvas_put(NULL, 0, 0, '.', red);
    CHECK(canvas_get(NULL, 0, 0) == '\0');
}

static void test_canvas_lines(void) {
    const struct {
        int x0, y0, x1, y1;
        const char *cells;
    } cases[] = {
        {0, 0, 4, 4, "#...." ".#..." "..#.." "...#." "....#"},
        {4, 4, 0, 0, "#...." ".#..." "..#.." "...#." "....#"},
        {4, 0, 0, 4, "....#" "...#." "..#.." ".#..." "#...."},
        {0, 0, 4, 2, "#...." ".##.." "...##" "....." "....."},
        {0, 0, 2, 4, "#...." ".#..." ".#..." "..#.." "..#.."},
        {0, 4, 4, 2, "....." "....." "...##" ".##.." "#...."},
        {4, 2, 0, 2, "....." "....." "#####" "....." "....."},
        {2, 4, 2, 0, "..#.." "..#.." "..#.." "..#.." "..#.."},
        {-2, 2, 6, 2, "....." "....." "#####" "....." "....."},
        {2, 2, 2, 2, "....." "....." "..#.." "....." "....."},
    };
    Canvas *canvas = canvas_create(5, 5);
    CHECK(canvas != NULL);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        canvas_clear(canvas, '.');
        canvas_draw_line(canvas, cases[i].x0, cases[i].y0,
                         cases[i].x1, cases[i].y1, '#');
        check_cells(canvas, cases[i].cells);
    }
    canvas_destroy(canvas);
}

static void test_canvas_rectangles(void) {
    Canvas *canvas = canvas_create(5, 4);
    CHECK(canvas != NULL);
    canvas_clear(canvas, '.');
    canvas_draw_rect(canvas, 1, 0, 3, 4, '#');
    check_cells(canvas, ".###." ".#.#." ".#.#." ".###.");
    canvas_clear(canvas, '.');
    canvas_draw_rect(canvas, -1, -1, 4, 4, '#');
    check_cells(canvas, "..#.." "..#.." "###.." ".....");
    canvas_clear(canvas, '.');
    canvas_draw_rect(canvas, 1, 1, 1, 1, '#');
    check_cells(canvas, "....." ".#..." "....." ".....");
    canvas_draw_rect(canvas, 0, 0, 0, 4, '!');
    canvas_draw_rect(canvas, 0, 0, 4, -1, '!');
    check_cells(canvas, "....." ".#..." "....." ".....");

    canvas_clear(canvas, '.');
    canvas_fill_rect(canvas, 1, 1, 3, 2, '#');
    check_cells(canvas, "....." ".###." ".###." ".....");
    canvas_clear(canvas, '.');
    canvas_fill_rect(canvas, -1, 2, 4, 4, '#');
    check_cells(canvas, "....." "....." "###.." "###..");
    canvas_fill_rect(canvas, 0, 0, 0, 4, '!');
    canvas_fill_rect(canvas, 0, 0, 4, -1, '!');
    check_cells(canvas, "....." "....." "###.." "###..");
    canvas_destroy(canvas);
}

static void test_canvas_brightness(void) {
    CHECK(strlen(ascii_ramp) == ASCII_RAMP_LEVELS);
    CHECK(canvas_ramp_char(-2.0) == ' ');
    CHECK(canvas_ramp_char(0.0) == ' ');
    CHECK(canvas_ramp_char(1.0) == '$');
    CHECK(canvas_ramp_char(2.0) == '$');
    for (int level = 0; level < ASCII_RAMP_LEVELS; ++level) {
        CHECK(canvas_ramp_char((double)level / 69.0) == ascii_ramp[level]);
    }
    CHECK(canvas_ramp_char(10.49 / 69.0) == ascii_ramp[10]);
    CHECK(canvas_ramp_char(10.51 / 69.0) == ascii_ramp[11]);
    Canvas *canvas = canvas_create(1, 1);
    CHECK(canvas != NULL);
    canvas_set_color(canvas, 0, 0, color_rgb(1, 2, 3));
    canvas_shade(canvas, 0, 0, 1.0);
    CHECK(canvas_get(canvas, 0, 0) == '$');
    CHECK_COLOR(canvas->colors[0], 1, 2, 3);
    canvas_shade(canvas, 1, 0, 0.0);
    CHECK(canvas_get(canvas, 0, 0) == '$');
    canvas_destroy(canvas);
}

static void test_canvas_plain_output(void) {
    Canvas *canvas = canvas_create(3, 2);
    FILE *out = tmpfile();
    CHECK(canvas != NULL && out != NULL);
    memcpy(canvas->cells, "ABCd e", 6);
    canvas_set_color(canvas, 0, 0, color_rgb(1, 2, 3));
    canvas_render(canvas, out);
    canvas_render(NULL, out);
    canvas_render(canvas, NULL);
    check_output(out, "ABC\nd e\n");
    CHECK(fclose(out) == 0);
    canvas_destroy(canvas);
}

static void test_canvas_color_output(void) {
    Canvas *canvas = canvas_create(3, 2);
    FILE *out = tmpfile();
    CHECK(canvas != NULL && out != NULL);
    Color red = color_rgb(255, 0, 0), green = color_rgb(0, 255, 0);
    canvas_put(canvas, 0, 0, 'A', red);
    canvas_put(canvas, 1, 0, 'B', red);
    canvas_put(canvas, 2, 0, 'C', green);
    canvas_put(canvas, 0, 1, 'D', green);
    canvas_put(canvas, 1, 1, 'E', green);
    canvas_put(canvas, 2, 1, 'F', green);
    canvas_render_color(canvas, out);
    canvas_render_color(NULL, out);
    canvas_render_color(canvas, NULL);
    check_output(out, "\x1b[38;2;255;0;0mAB\x1b[38;2;0;255;0mC\x1b[0m\n"
                      "\x1b[38;2;0;255;0mDEF\x1b[0m\n");
    CHECK(fclose(out) == 0);
    canvas_destroy(canvas);
}

int main(void) {
    RUN(test_canvas_creation);
    RUN(test_canvas_cells_and_bounds);
    RUN(test_canvas_lines);
    RUN(test_canvas_rectangles);
    RUN(test_canvas_brightness);
    RUN(test_canvas_plain_output);
    RUN(test_canvas_color_output);
    return EXIT_SUCCESS;
}
