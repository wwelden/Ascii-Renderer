#include "snake.h"

#include <stdlib.h>

/* --- Small helpers used by the step logic --- */

/* Advance the linear-congruential RNG and return a 15-bit value. Self-contained
   so food placement is reproducible from the seed and never touches rand(). */
static unsigned rng_next(Snake *s) {
    s->rng = s->rng * 1103515245u + 12345u;
    return (s->rng >> 16) & 0x7fffu;
}

static int point_eq(Point a, Point b) {
    return a.x == b.x && a.y == b.y;
}

/* Is p outside the playfield walls? */
static int snake_hits_wall(const Snake *s, Point p) {
    return p.x < 0 || p.y < 0 || p.x >= s->width || p.y >= s->height;
}

/* Does p overlap the snake's own body? The tail segment (the last cell) is
   excluded on purpose: during a normal move it vacates as the head advances, so
   chasing your own tail is legal. */
static int snake_hits_body(const Snake *s, Point p) {
    for (int i = 0; i < s->length - 1; ++i) {
        if (point_eq(s->body[i], p)) {
            return 1;
        }
    }
    return 0;
}

/* Insert `head` at the front of the body, shifting every segment down by one
   and growing length by one. A non-growing move drops the tail again afterward
   by decrementing length. */
static void snake_prepend(Snake *s, Point head) {
    for (int i = s->length; i > 0; --i) {
        s->body[i] = s->body[i - 1];
    }
    s->body[0] = head;
    s->length += 1;
}

/* Drop food on a random cell that the snake does not currently occupy. */
static void snake_place_food(Snake *s) {
    for (;;) {
        Point p = {(int)(rng_next(s) % (unsigned)s->width),
                   (int)(rng_next(s) % (unsigned)s->height)};
        int on_body = 0;
        for (int i = 0; i < s->length; ++i) {
            if (point_eq(s->body[i], p)) {
                on_body = 1;
                break;
            }
        }
        if (!on_body) {
            s->food = p;
            return;
        }
    }
}

static int is_opposite(Direction a, Direction b) {
    return (a == DIR_UP && b == DIR_DOWN) || (a == DIR_DOWN && b == DIR_UP) ||
           (a == DIR_LEFT && b == DIR_RIGHT) || (a == DIR_RIGHT && b == DIR_LEFT);
}

/* --- Public API --- */

Snake *snake_create(int width, int height, unsigned seed) {
    if (width < 4 || height < 1) {
        return NULL;
    }
    Snake *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    s->capacity = width * height;
    s->body = malloc((size_t)s->capacity * sizeof(Point));
    if (s->body == NULL) {
        free(s);
        return NULL;
    }

    s->width = width;
    s->height = height;
    s->rng = seed ? seed : 1u;
    s->dir = DIR_RIGHT;
    s->pending = DIR_RIGHT;
    s->alive = 1;
    s->score = 0;

    /* Head at center, two segments trailing to the left. */
    int cx = width / 2;
    int cy = height / 2;
    s->length = 3;
    s->body[0] = (Point){cx, cy};
    s->body[1] = (Point){cx - 1, cy};
    s->body[2] = (Point){cx - 2, cy};

    snake_place_food(s);
    return s;
}

void snake_destroy(Snake *s) {
    if (s == NULL) {
        return;
    }
    free(s->body);
    free(s);
}

void snake_turn(Snake *s, Direction dir) {
    if (s == NULL || !s->alive) {
        return;
    }
    /* Reject reversing straight into the neck once there is a body to hit. */
    if (s->length > 1 && is_opposite(dir, s->dir)) {
        return;
    }
    s->pending = dir;
}

void snake_step(Snake *s) {
    if (s == NULL || !s->alive) {
        return;
    }

    /* Commit the buffered turn, then compute the cell the head moves into. */
    s->dir = s->pending;
    Point head = s->body[0];
    switch (s->dir) {
        case DIR_UP:    head.y -= 1; break;
        case DIR_DOWN:  head.y += 1; break;
        case DIR_LEFT:  head.x -= 1; break;
        case DIR_RIGHT: head.x += 1; break;
    }

    /* TODO(human): resolve the move onto `head`.

       Decide, in this order, what happens now that the head wants to enter
       `head`, using the helpers above and the snake's public fields:

         1. Death first. If snake_hits_wall(s, head) OR snake_hits_body(s, head),
            the snake dies: set s->alive = 0 and return, leaving the body as-is
            so the caller can draw a game-over screen.
         2. Otherwise the snake advances. Remember whether `head` is the food
            cell (point_eq(head, s->food)) BEFORE you move, then insert the new
            head with snake_prepend(s, head).
         3. If it ate the food, it grows: bump s->score and drop new food with
            snake_place_food(s) (the prepend already kept the extra length).
            If it did NOT eat, it just moved: undo the growth by decrementing
            s->length so the tail is dropped and the length stays constant.

       Order matters: check death BEFORE moving; check food AFTER prepending. */
       if ( snake_hits_wall(s, head) || snake_hits_body(s, head)){
        s->alive = 0;
        return;
       }

       int ate = point_eq(head, s->food);

       snake_prepend(s, head);

        if (ate){
            s->score +=1;
            snake_place_food(s);
        }else{
            s->length -= 1;
        }
}
