#ifndef ASCII_RENDERER_TEST_H
#define ASCII_RENDERER_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Unlike assert(), checks stay enabled in release builds with NDEBUG. */
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

#define RUN(test) do { \
    test(); \
    printf("ok - %s\n", #test); \
} while (0)

#define CHECK_COLOR(actual, red, green, blue) do { \
    Color test_color = (actual); \
    CHECK(test_color.r == (red)); \
    CHECK(test_color.g == (green)); \
    CHECK(test_color.b == (blue)); \
} while (0)

/* Check all bytes, including UTF-8 and ANSI escapes, and reject trailing data. */
static inline void check_output(FILE *stream, const char *expected) {
    CHECK(fflush(stream) == 0);
    CHECK(fseek(stream, 0, SEEK_END) == 0);
    long size = ftell(stream);
    CHECK(size >= 0);
    CHECK((size_t)size == strlen(expected));
    rewind(stream);
    for (size_t i = 0; expected[i] != '\0'; ++i) {
        CHECK(fgetc(stream) == (unsigned char)expected[i]);
    }
    CHECK(fgetc(stream) == EOF);
    CHECK(!ferror(stream));
}

#endif
