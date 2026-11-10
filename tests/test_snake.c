#include "snake.h"
#include "test.h"

#include <limits.h>

static void check_point(Point actual, int x, int y) {
    CHECK(actual.x == x);
    CHECK(actual.y == y);
}

static void check_food_is_free(const Snake *s) {
    CHECK(s->food.x >= 0 && s->food.x < s->width);
    CHECK(s->food.y >= 0 && s->food.y < s->height);
    for (int i = 0; i < s->length; ++i) {
        CHECK(s->food.x != s->body[i].x || s->food.y != s->body[i].y);
    }
}

static void creation_and_invalid_fields(void) {
    CHECK(snake_create(3, 4, 1) == NULL);
    CHECK(snake_create(0, 4, 1) == NULL);
    CHECK(snake_create(-1, 4, 1) == NULL);
    CHECK(snake_create(4, 0, 1) == NULL);
    CHECK(snake_create(4, -1, 1) == NULL);

    Snake *s = snake_create(10, 6, 123);
    CHECK(s != NULL);
    CHECK(s->width == 10 && s->height == 6);
    CHECK(s->capacity == 60 && s->length == 3);
    CHECK(s->dir == DIR_RIGHT && s->pending == DIR_RIGHT);
    CHECK(s->score == 0 && s->alive == 1);
    check_point(s->body[0], 5, 3);
    check_point(s->body[1], 4, 3);
    check_point(s->body[2], 3, 3);
    check_food_is_free(s);
    snake_destroy(s);

    s = snake_create(4, 1, 1);
    CHECK(s != NULL);
    check_point(s->food, 3, 0);
    snake_destroy(s);
    snake_destroy(NULL);
    snake_step(NULL);
    snake_turn(NULL, DIR_UP);
}

static void seeded_food_is_reproducible(void) {
    for (unsigned seed = 0; seed < 32; ++seed) {
        Snake *a = snake_create(12, 8, seed);
        Snake *b = snake_create(12, 8, seed ? seed : 1);
        CHECK(a != NULL && b != NULL);
        check_food_is_free(a);
        check_point(a->food, b->food.x, b->food.y);
        CHECK(a->rng == b->rng);
        snake_destroy(a);
        snake_destroy(b);
    }
}

static void movement_shifts_body_without_growing(void) {
    Snake *s = snake_create(12, 8, 1);
    CHECK(s != NULL);
    s->food = (Point){0, 0};
    const Direction turns[] = {DIR_RIGHT, DIR_UP, DIR_LEFT, DIR_DOWN};
    const Point heads[] = {{7, 4}, {7, 3}, {6, 3}, {6, 4}};
    for (size_t i = 0; i < sizeof(turns) / sizeof(turns[0]); ++i) {
        Point old_head = s->body[0];
        Point old_neck = s->body[1];
        snake_turn(s, turns[i]);
        snake_step(s);
        CHECK(s->alive == 1 && s->length == 3 && s->score == 0);
        CHECK(s->dir == turns[i]);
        check_point(s->body[0], heads[i].x, heads[i].y);
        check_point(s->body[1], old_head.x, old_head.y);
        check_point(s->body[2], old_neck.x, old_neck.y);
    }
    snake_destroy(s);
}

static void turns_reject_reversal_against_committed_heading(void) {
    Snake *s = snake_create(12, 8, 1);
    CHECK(s != NULL);
    s->food = (Point){0, 0};
    snake_turn(s, DIR_LEFT);
    CHECK(s->pending == DIR_RIGHT);
    snake_turn(s, DIR_UP);
    CHECK(s->dir == DIR_RIGHT && s->pending == DIR_UP);
    snake_turn(s, DIR_LEFT); /* A rapid second input cannot reverse into neck. */
    CHECK(s->pending == DIR_UP);
    snake_step(s);
    check_point(s->body[0], 6, 3);
    CHECK(s->dir == DIR_UP);
    snake_turn(s, DIR_DOWN);
    CHECK(s->pending == DIR_UP);
    snake_turn(s, DIR_LEFT);
    snake_turn(s, DIR_RIGHT); /* Last legal buffered turn wins. */
    CHECK(s->pending == DIR_RIGHT);
    snake_step(s);
    check_point(s->body[0], 7, 3);
    snake_destroy(s);
}

static void eating_grows_scores_and_places_free_food(void) {
    Snake *s = snake_create(12, 8, 1);
    CHECK(s != NULL);
    for (int meal = 0; meal < 3; ++meal) {
        Point previous[5];
        int old_length = s->length;
        memcpy(previous, s->body, (size_t)old_length * sizeof(Point));
        s->food = (Point){s->body[0].x + 1, s->body[0].y};
        Point eaten = s->food;
        snake_step(s);
        CHECK(s->alive == 1);
        CHECK(s->length == old_length + 1 && s->score == meal + 1);
        check_point(s->body[0], eaten.x, eaten.y);
        for (int i = 0; i < old_length; ++i) {
            check_point(s->body[i + 1], previous[i].x, previous[i].y);
        }
        check_food_is_free(s);
    }
    snake_destroy(s);
}

static void wall_collisions_leave_body_and_score_unchanged(void) {
    const Direction dirs[] = {DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT};
    const Point heads[] = {{3, 0}, {3, 5}, {0, 3}, {7, 3}};
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        Snake *s = snake_create(8, 6, 1);
        CHECK(s != NULL);
        s->body[0] = heads[i];
        s->dir = dirs[i];
        s->pending = dirs[i];
        Point before[3];
        memcpy(before, s->body, sizeof(before));
        Point food = s->food;
        unsigned rng = s->rng;
        snake_step(s);
        CHECK(s->alive == 0 && s->length == 3 && s->score == 0);
        CHECK(memcmp(before, s->body, sizeof(before)) == 0);
        check_point(s->food, food.x, food.y);
        CHECK(s->rng == rng);
        snake_turn(s, dirs[(i + 1) % 4]);
        snake_step(s);
        CHECK(s->dir == dirs[i] && s->pending == dirs[i]);
        CHECK(memcmp(before, s->body, sizeof(before)) == 0);
        CHECK(s->length == 3 && s->score == 0);
        snake_destroy(s);
    }
}

static void self_collision_keeps_the_body_intact(void) {
    Snake *s = snake_create(8, 6, 1);
    CHECK(s != NULL);
    const Point body[] = {{3, 2}, {3, 3}, {4, 3}, {4, 2}, {5, 2}};
    memcpy(s->body, body, sizeof(body));
    s->length = 5;
    s->dir = DIR_UP;
    s->pending = DIR_UP;
    s->food = (Point){0, 0};
    snake_turn(s, DIR_RIGHT);
    snake_step(s);
    CHECK(s->alive == 0 && s->length == 5 && s->score == 0);
    CHECK(memcmp(body, s->body, sizeof(body)) == 0);
    snake_destroy(s);
}

static void moving_into_vacating_tail_is_legal(void) {
    Snake *s = snake_create(8, 6, 1);
    CHECK(s != NULL);
    const Point body[] = {{3, 2}, {3, 3}, {4, 3}, {4, 2}};
    memcpy(s->body, body, sizeof(body));
    s->length = 4;
    s->dir = DIR_UP;
    s->pending = DIR_UP;
    s->food = (Point){0, 0};
    snake_turn(s, DIR_RIGHT);
    snake_step(s);
    CHECK(s->alive == 1 && s->length == 4 && s->score == 0);
    check_point(s->body[0], 4, 2);
    for (int i = 0; i < 3; ++i) {
        check_point(s->body[i + 1], body[i].x, body[i].y);
    }
    snake_destroy(s);
}

static void filling_the_board_finishes_without_searching_for_food(void) {
    Snake *s = snake_create(4, 1, 1);
    CHECK(s != NULL);
    snake_step(s); /* The only free cell is directly in front of the head. */
    CHECK(s->length == s->capacity && s->length == 4);
    CHECK(s->score == 1 && s->alive == 0);
    for (int i = 0; i < 4; ++i) {
        check_point(s->body[i], 3 - i, 0);
    }
    snake_turn(s, DIR_UP);
    snake_step(s);
    CHECK(s->length == 4 && s->score == 1 && s->alive == 0);
    check_point(s->body[0], 3, 0);
    snake_destroy(s);
}

static void oversized_fields_are_rejected(void) {
    CHECK(snake_create(INT_MAX, 2, 1) == NULL);
    CHECK(snake_create(4, INT_MAX, 1) == NULL);
}

int main(void) {
    RUN(creation_and_invalid_fields);
    RUN(seeded_food_is_reproducible);
    RUN(movement_shifts_body_without_growing);
    RUN(turns_reject_reversal_against_committed_heading);
    RUN(eating_grows_scores_and_places_free_food);
    RUN(wall_collisions_leave_body_and_score_unchanged);
    RUN(self_collision_keeps_the_body_intact);
    RUN(moving_into_vacating_tail_is_legal);
    RUN(filling_the_board_finishes_without_searching_for_food);
    RUN(oversized_fields_are_rejected);
    return EXIT_SUCCESS;
}
