#define _POSIX_C_SOURCE 200809L

#include "framebuffer.h"
#include "test.h"

#include <unistd.h>

#define HALF_BLOCK "\xe2\x96\x80"

static void test_framebuffer_creation(void) {
    CHECK(framebuffer_create(0, 2) == NULL);
    CHECK(framebuffer_create(2, 0) == NULL);
    CHECK(framebuffer_create(-1, 2) == NULL);
    CHECK(framebuffer_create(2, -1) == NULL);
    Framebuffer *fb = framebuffer_create(3, 2);
    CHECK(fb != NULL);
    CHECK(fb->width == 3 && fb->height == 2);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) {
            CHECK_COLOR(framebuffer_get(fb, x, y), 0, 0, 0);
        }
    }
    framebuffer_destroy(fb);
    framebuffer_destroy(NULL);
}

static void test_framebuffer_pixels_and_bounds(void) {
    Framebuffer *fb = framebuffer_create(3, 2);
    CHECK(fb != NULL);
    framebuffer_fill(fb, color_rgb(1, 2, 3));
    framebuffer_set(fb, 2, 1, color_rgb(4, 5, 255));
    CHECK_COLOR(framebuffer_get(fb, 2, 1), 4, 5, 255);
    const int outside[][2] = {{-1, 0}, {3, 0}, {0, -1}, {0, 2}};
    for (size_t i = 0; i < sizeof(outside) / sizeof(outside[0]); ++i) {
        framebuffer_set(fb, outside[i][0], outside[i][1], color_rgb(9, 9, 9));
        CHECK_COLOR(framebuffer_get(fb, outside[i][0], outside[i][1]), 0, 0, 0);
    }
    for (int i = 0; i < 5; ++i) {
        CHECK_COLOR(fb->pixels[i], 1, 2, 3);
    }
    CHECK_COLOR(fb->pixels[5], 4, 5, 255);
    framebuffer_destroy(fb);
    framebuffer_fill(NULL, color_rgb(1, 2, 3));
    framebuffer_set(NULL, 0, 0, color_rgb(1, 2, 3));
    CHECK_COLOR(framebuffer_get(NULL, 0, 0), 0, 0, 0);
}

static void test_framebuffer_scaling(void) {
    Framebuffer *src = framebuffer_create(3, 2);
    CHECK(src != NULL);
    for (int i = 0; i < 6; ++i) {
        src->pixels[i] = color_rgb((unsigned char)(i + 1), 0, 0);
    }
    Framebuffer *up = framebuffer_scaled(src, 6, 4);
    CHECK(up != NULL && up->width == 6 && up->height == 4);
    const unsigned char up_rows[4][6] = {
        {1, 1, 2, 2, 3, 3}, {1, 1, 2, 2, 3, 3},
        {4, 4, 5, 5, 6, 6}, {4, 4, 5, 5, 6, 6},
    };
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 6; ++x) {
            CHECK_COLOR(framebuffer_get(up, x, y), up_rows[y][x], 0, 0);
        }
    }
    Framebuffer *down = framebuffer_scaled(src, 2, 1);
    CHECK(down != NULL && down->width == 2 && down->height == 1);
    CHECK_COLOR(framebuffer_get(down, 0, 0), 1, 0, 0);
    CHECK_COLOR(framebuffer_get(down, 1, 0), 2, 0, 0);
    Framebuffer *mixed = framebuffer_scaled(src, 2, 3);
    CHECK(mixed != NULL && mixed->width == 2 && mixed->height == 3);
    const unsigned char mixed_rows[3][2] = {{1, 2}, {1, 2}, {4, 5}};
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 2; ++x) {
            CHECK_COLOR(framebuffer_get(mixed, x, y), mixed_rows[y][x], 0, 0);
        }
    }
    Framebuffer *copy = framebuffer_scaled(src, 3, 2);
    CHECK(copy != NULL && copy->pixels != src->pixels);
    framebuffer_set(copy, 0, 0, color_rgb(99, 0, 0));
    CHECK_COLOR(framebuffer_get(src, 0, 0), 1, 0, 0);
    CHECK(framebuffer_scaled(NULL, 2, 2) == NULL);
    CHECK(framebuffer_scaled(src, 0, 2) == NULL);
    CHECK(framebuffer_scaled(src, 2, -1) == NULL);
    framebuffer_destroy(copy);
    framebuffer_destroy(mixed);
    framebuffer_destroy(down);
    framebuffer_destroy(up);
    framebuffer_destroy(src);
}

/* Named temporary files exercise the real image decoder without repository
   fixtures, fixed paths, or assumptions about the working directory. */
static void write_fixture(char *path, const unsigned char *bytes, size_t size) {
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    FILE *out = fdopen(fd, "wb");
    CHECK(out != NULL);
    CHECK(fwrite(bytes, 1, size, out) == size);
    CHECK(fclose(out) == 0);
}

static void test_framebuffer_image_loading(void) {
    char rgb_path[] = "/tmp/ascii-rgb-XXXXXX";
    const unsigned char rgb[] = "P6\n2 2\n255\n"
        "\xff\x00\x00\x00\xff\x00\x00\x00\xff\x01\x02\x03";
    write_fixture(rgb_path, rgb, sizeof(rgb) - 1);
    Framebuffer *fb = framebuffer_load(rgb_path);
    CHECK(unlink(rgb_path) == 0);
    CHECK(fb != NULL && fb->width == 2 && fb->height == 2);
    CHECK_COLOR(framebuffer_get(fb, 0, 0), 255, 0, 0);
    CHECK_COLOR(framebuffer_get(fb, 1, 0), 0, 255, 0);
    CHECK_COLOR(framebuffer_get(fb, 0, 1), 0, 0, 255);
    CHECK_COLOR(framebuffer_get(fb, 1, 1), 1, 2, 3);
    framebuffer_destroy(fb);
    CHECK(framebuffer_load(rgb_path) == NULL);

    char gray_path[] = "/tmp/ascii-gray-XXXXXX";
    const unsigned char gray[] = "P5\n2 1\n255\n\x00\x80";
    write_fixture(gray_path, gray, sizeof(gray) - 1);
    fb = framebuffer_load(gray_path);
    CHECK(unlink(gray_path) == 0);
    CHECK(fb != NULL && fb->width == 2 && fb->height == 1);
    CHECK_COLOR(framebuffer_get(fb, 0, 0), 0, 0, 0);
    CHECK_COLOR(framebuffer_get(fb, 1, 0), 128, 128, 128);
    framebuffer_destroy(fb);

    char bad_path[] = "/tmp/ascii-invalid-XXXXXX";
    const unsigned char bad[] = "not an image";
    write_fixture(bad_path, bad, sizeof(bad) - 1);
    CHECK(framebuffer_load(bad_path) == NULL);
    CHECK(unlink(bad_path) == 0);
}

static void test_framebuffer_halfblock_colors(void) {
    Framebuffer *fb = framebuffer_create(3, 2);
    FILE *out = tmpfile();
    CHECK(fb != NULL && out != NULL);
    framebuffer_set(fb, 0, 0, color_rgb(255, 0, 0));
    framebuffer_set(fb, 1, 0, color_rgb(255, 0, 0));
    framebuffer_set(fb, 2, 0, color_rgb(0, 255, 0));
    framebuffer_set(fb, 0, 1, color_rgb(0, 0, 255));
    framebuffer_set(fb, 1, 1, color_rgb(255, 255, 0));
    framebuffer_set(fb, 2, 1, color_rgb(255, 255, 0));
    framebuffer_render_halfblocks(fb, out);
    framebuffer_render_halfblocks(NULL, out);
    framebuffer_render_halfblocks(fb, NULL);
    check_output(out, "\x1b[38;2;255;0;0m\x1b[48;2;0;0;255m" HALF_BLOCK
                      "\x1b[48;2;255;255;0m" HALF_BLOCK
                      "\x1b[38;2;0;255;0m" HALF_BLOCK "\x1b[0m\n");
    CHECK(fclose(out) == 0);
    framebuffer_destroy(fb);
}

static void test_framebuffer_odd_height(void) {
    Framebuffer *fb = framebuffer_create(2, 3);
    FILE *out = tmpfile();
    CHECK(fb != NULL && out != NULL);
    framebuffer_fill(fb, color_rgb(255, 255, 255));
    framebuffer_render_halfblocks(fb, out);
    check_output(out, "\x1b[38;2;255;255;255m\x1b[48;2;255;255;255m"
                      HALF_BLOCK HALF_BLOCK "\x1b[0m\n"
                      "\x1b[38;2;255;255;255m\x1b[48;2;0;0;0m"
                      HALF_BLOCK HALF_BLOCK "\x1b[0m\n");
    CHECK(fclose(out) == 0);
    framebuffer_destroy(fb);
}

int main(void) {
    RUN(test_framebuffer_creation);
    RUN(test_framebuffer_pixels_and_bounds);
    RUN(test_framebuffer_scaling);
    RUN(test_framebuffer_image_loading);
    RUN(test_framebuffer_halfblock_colors);
    RUN(test_framebuffer_odd_height);
    return EXIT_SUCCESS;
}
